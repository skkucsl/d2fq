#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/iocontext.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <asm/current.h>
#include <linux/d2fq.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/jiffies.h> /* Taget */
#include <linux/nvme.h>
#include <linux/list.h>
#include <linux/workqueue.h>

#include "blk-mq.h"
#include "d2fq.h"

#include "gholder.h"

#ifdef CONFIG_IOSCHED_D2FQ

#define D2FQ_CLS_WGHT_MIN	3		/* around 20KB based on weight 1 flow*/
#define D2FQ_CLS_WGHT_MAX	256		/* around 20KB based on weight 1 flow*/

unsigned int sysctl_d2fq_read_weight = 1;	/* read weight */
unsigned int sysctl_d2fq_write_weight = 1;	/* write weight */

unsigned int sysctl_d2fq_thrsh = 8192;		/* 8MB */
unsigned int sysctl_d2fq_cls_base = D2FQ_CLS_WGHT_MIN;		/* class base weight */
         int        next_cap_weight = 0;
         int sysctl_d2fq_md_ratio = 70;		/* medium range ratio - auto set on neg. val */

unsigned int sysctl_d2fq_di_period = 50;	/* criteria for deceptive idlenss */
						/* 1=0.004(sec) 25=0.1(sec) 50=0.2(sec) */

unsigned int sysctl_d2fq_dw_en = 0; 		/* dynamic weight change enable */
unsigned int sysctl_d2fq_dw_period = 50;	/* dynamic weight change period 1 = 1/250 (sec) */
						/*  1=0.004(sec) 25=0.1(sec) 50=0.2(sec) */
#define GRP_L		1

struct d2fq_global_data {
	struct request_queue	*q;
	struct list_head 	dd_list;
	struct timer_list 	timer;		/* weight setting */
	struct timer_list 	rs_timer;	/* reset timer */
	union global_holder 	gh;
	atomic_t nr_flows;
	atomic_t nr_active_flows;

	struct workqueue_struct *d2fq_workqueue;
	struct work_struct set_work;
} __attribute__ ((aligned(64)));

struct d2fq_data {
	atomic64_t	lvt;			/* local virtual time */
	atomic64_t	lvt_last;		/* accounted virtual time on last period */

	atomic_t	pws_cnt_h; 		/* priority weight timeslice */
	atomic_t	pws_cnt_m;
	atomic_t	pws_cnt_l;

	atomic_t	ir_cnt;			/* inflight requset counter */

#ifdef CONFIG_IOSCHED_D2FQ_VERBOSE
	atomic_t	cnt_h;			/* count # of request sent to each class */
	atomic_t	cnt_m;
	atomic_t	cnt_l;
#endif
	struct timer_list 	di_timer;	/* deceptive idleness */
	bool active;

	int		pid;
	bool		exit_flag;

	unsigned	weight;
	unsigned	vt_unit;

	int 		slwdwn;
	struct list_head 	ddlist;

	struct d2fq_global_data	*dgd;
}__attribute__ ((aligned(64)));

ssize_t queue_d2fq_en_show(struct request_queue *q, char *page)
{
	return sprintf(page, "%u\n", q->d2fq_en);
}

ssize_t queue_d2fq_en_store(struct request_queue *q, const char *page,
				  size_t count)
{
	unsigned int val;
	int err, ret;

	err = kstrtou32(page, 10, &val);
	if (err || !(val == 0 || val == 1))
		return -EINVAL;

	if (val == 0) {
		sysctl_d2fq_cls_base = 0;
	} else if (val == 1) {
		sysctl_d2fq_cls_base = 6;
	}

	ret = q->mq_ops->set_weight(q->queue_hw_ctx[1], sysctl_d2fq_cls_base);

	if (ret < 0) {
		sysctl_d2fq_cls_base = 0;
		q->d2fq_en = false;
		printk(KERN_INFO "d2fq: current system fail to build wrr queues");
	} else
		q->d2fq_en = val;

	return count;
}

void d2fq_call_set_weight(struct work_struct *work)
{
	struct d2fq_global_data *dgd;

	dgd = container_of(work, struct d2fq_global_data, set_work);

	sysctl_d2fq_cls_base = next_cap_weight;
	if (dgd->q->mq_ops->set_weight)
		dgd->q->mq_ops->set_weight(dgd->q->queue_hw_ctx[1], sysctl_d2fq_cls_base);
	next_cap_weight = 0;
}

static void d2fq_timer_fn(struct timer_list *t)
{
	struct d2fq_global_data *dgd = from_timer(dgd, t, timer);
	struct d2fq_data *dd;
	/* check low class usage for time slice */
	u64 max_vt = 0, max_usage_chk = 0;
	u64 min_vt = U64_MAX;
	int min_id;

	u64 max_per_vt = 0, min_per_vt = U64_MAX;

	u64 FAIR_THRSH = 2*(u64)sysctl_d2fq_thrsh;
	u64 pws_cnt_h, pws_cnt_m, pws_cnt_l, pws_cnt_t;
	u64 lvt, lvt_last;
	u64 slwdwn, max_slwdwn = 0;

	u32 l_wght, m_wght, h_wght, base;

	if (unlikely(sysctl_d2fq_dw_en == 0))
		return;

	if (unlikely(next_cap_weight != 0))
		return;

	l_wght = 256 / sysctl_d2fq_cls_base;
	base = int_sqrt(sysctl_d2fq_cls_base);
	if (sysctl_d2fq_cls_base > base*base)
		base++;
	m_wght = l_wght * base;
	h_wght = l_wght * sysctl_d2fq_cls_base;

	/* calculate slowdown for each queue */
	list_for_each_entry(dd, &dgd->dd_list, ddlist) {

		pws_cnt_h 	= atomic_xchg(&dd->pws_cnt_h, 0);
		pws_cnt_m 	= atomic_xchg(&dd->pws_cnt_m, 0);
		pws_cnt_l 	= atomic_xchg(&dd->pws_cnt_l, 0);
		pws_cnt_t 	= pws_cnt_h + pws_cnt_m + pws_cnt_l;

		lvt 		= atomic64_read(&dd->lvt);
		lvt_last 	= atomic64_xchg(&dd->lvt_last, lvt);

		if (unlikely(!pws_cnt_t))
			continue;

		/* for increasing vt */
		/* get largest VT gap btw flow */
		if (max_vt < lvt) {
			max_vt = lvt;
			max_per_vt = lvt - lvt_last;
		}
		if (min_vt > lvt && dd->active) {
			min_vt = lvt;
			min_id = dd->pid;
			min_per_vt = lvt - lvt_last;
		}

		/* for decreasing vt */
		/* calculate slowdown */
		slwdwn  = ((pws_cnt_h * h_wght) / h_wght)
			+ ((pws_cnt_m * h_wght) / m_wght)
			+ ((pws_cnt_l * h_wght) / l_wght)
			+ pws_cnt_t - 1;
		slwdwn /= pws_cnt_t;
		if (max_slwdwn < slwdwn) {
			max_slwdwn = slwdwn;
			/* check whether slowest queue use low class more than 50% */
			max_usage_chk = (2*pws_cnt_l) / pws_cnt_t;
		}
	}

	/* NO need to change scheme w/ only one flow exist */
	if (atomic_read(&dgd->nr_active_flows) < 2) {
		return;
	}

	/* weight change check */
	if (sysctl_d2fq_dw_en && next_cap_weight == 0)
	{
		/* decrease weight */
		if ( (max_slwdwn < sysctl_d2fq_cls_base)
			&& (sysctl_d2fq_cls_base > 3) )
		{
			next_cap_weight = max_slwdwn;
			if (next_cap_weight < D2FQ_CLS_WGHT_MIN)
				next_cap_weight = D2FQ_CLS_WGHT_MIN;
		}
		/* increase weight */
		else if (max_vt > min_vt + FAIR_THRSH
		    && max_usage_chk && sysctl_d2fq_cls_base < D2FQ_CLS_WGHT_MAX)
		{
			if (max_per_vt > min_per_vt) {
				next_cap_weight
				= ((max_per_vt * sysctl_d2fq_cls_base) + min_per_vt - 1)
					/ min_per_vt;
			}
			if (next_cap_weight > D2FQ_CLS_WGHT_MAX)
				next_cap_weight = D2FQ_CLS_WGHT_MAX;
		}

		if (next_cap_weight == sysctl_d2fq_cls_base) {
			next_cap_weight = 0;
		}
		/* need to send work to nvme device */
		if (next_cap_weight != 0) {
			INIT_WORK(&dgd->set_work, d2fq_call_set_weight);
			queue_work(dgd->d2fq_workqueue, &dgd->set_work);
		}
	}

	return;
}

static void d2fq_di_timer_fn(struct timer_list *t)
{
	struct d2fq_data *dd = from_timer(dd, t, di_timer);
	struct d2fq_global_data *dgd = dd->dgd;

	if (atomic_read(&dd->ir_cnt) > 0)
		return;

	dd->active = false;

	gh_release_min(&dgd->gh, dd->pid);
	if (atomic_dec_return(&dgd->nr_active_flows) < 1)
		mod_timer(&dgd->rs_timer, jiffies + D2FQ_RS_SLICE);

	if (unlikely(dd->exit_flag)) {
		atomic_dec(&dgd->nr_flows);
		kfree(dd);
	}

	return;
}

static void d2fq_rs_timer_fn(struct timer_list *t)
{
	struct d2fq_global_data *dgd = from_timer(dgd, t, rs_timer);

	drain_workqueue(dgd->d2fq_workqueue);
        next_cap_weight = 0;
	gh_reset_min(&dgd->gh);
	return;
}

unsigned d2fq_ioprio_to_weight(void)
{
	struct io_context *ioc = current->io_context;
	unsigned ioprio;

	if (!ioc) {
		printk(KERN_ERR "d2fq: Fail to get weight\n");
		return 0;
	}

	ioprio =  (unsigned)(ioc->ioprio);
	ioprio &= 0x000F;

	return (8-ioprio);
}

void d2fq_exit_dgd(struct request_queue *q)
{
	struct d2fq_global_data *dgd = q->dgd;

	del_timer_sync(&dgd->timer);
	del_timer_sync(&dgd->rs_timer);
	dgd->q = NULL;

	return;
}

void d2fq_init_dgd(struct request_queue *q)
{
	struct d2fq_global_data *dgd
		= kmalloc(sizeof(struct d2fq_global_data), GFP_KERNEL);
	dgd->q = q;
	q->dgd = dgd;

	INIT_LIST_HEAD(&dgd->dd_list);
	timer_setup(&dgd->timer, d2fq_timer_fn, 0);
	timer_setup(&dgd->rs_timer, d2fq_rs_timer_fn, 0);
	gh_reset_min (&dgd->gh);

	atomic_set(&dgd->nr_flows, 0);
	atomic_set(&dgd->nr_active_flows, 0);

	dgd->d2fq_workqueue = alloc_workqueue("kd2fqd",
		WQ_HIGHPRI, WQ_MAX_ACTIVE);
	if (!dgd->d2fq_workqueue)
		panic("Failed to create kd2fqd");

	INIT_WORK(&dgd->set_work, d2fq_call_set_weight);

	return;
}
EXPORT_SYMBOL(d2fq_init_dgd);

struct d2fq_data* d2fq_assign_dd(void *dgd)
{
	struct d2fq_data *dd;
	unsigned long flags;
	u64 gvt;

	/* alloc nd to task struct */
	if (unlikely(!current->d2fq)) {
		spin_lock_irqsave(&current->group_leader->d2fq_lock, flags);
		if (!current->group_leader->d2fq)
			goto new_dd;
		else
			current->d2fq = current->group_leader->d2fq;
		spin_unlock_irqrestore(&current->group_leader->d2fq_lock, flags);
	}

	dd = (struct d2fq_data*)(current->d2fq);
	if (unlikely(dd->active == false))
	{
		dd->active 	= true;
		atomic_inc(&dd->dgd->nr_active_flows);

		gvt = gh_return_min(&dd->dgd->gh);
		atomic64_set(&dd->lvt, gvt);
		atomic64_set(&dd->lvt_last, gvt);
	}

	return dd;

new_dd:
	dd = kmalloc(sizeof(struct d2fq_data), GFP_KERNEL);

#ifdef GRP_L
	current->group_leader->d2fq = (void*)dd;
#endif
	current->d2fq = (void*)dd;

	dd->dgd		= dgd;
	dd->vt_unit	= 1;
	atomic64_set(&dd->lvt, gh_return_min(&dd->dgd->gh));
	atomic64_set(&dd->lvt_last, gh_return_min(&dd->dgd->gh));

#ifdef CONFIG_IOSCHED_D2FQ_VERBOSE
	atomic_set(&dd->cnt_h, 0);
	atomic_set(&dd->cnt_m, 0);
	atomic_set(&dd->cnt_l, 0);
#endif
	atomic_set(&dd->pws_cnt_h, 0);
	atomic_set(&dd->pws_cnt_m, 0);
	atomic_set(&dd->pws_cnt_l, 0);

	INIT_LIST_HEAD(&dd->ddlist);
	list_add(&dd->ddlist, &dd->dgd->dd_list);

	dd->active 	= true;
	atomic_inc(&dd->dgd->nr_active_flows);
	timer_setup(&dd->di_timer, d2fq_di_timer_fn, 0);
#ifdef GRP_L
	dd->pid		= current->group_leader->pid;
#else
	dd->pid		= current->pid;
#endif

	atomic_set(&dd->ir_cnt, 0);
	dd->exit_flag	= 0;

	atomic_inc(&dd->dgd->nr_flows);

#ifdef CONFIG_IOSCHED_D2FQ_VERBOSE
	printk(KERN_INFO "d2fq: (%5d)%s assign_dd: unit %u | lvt %llx",
	dd->pid, current->comm, dd->vt_unit, atomic64_read(&dd->lvt));
#endif
	spin_unlock_irqrestore(&current->group_leader->d2fq_lock, flags);

	return dd;
}
EXPORT_SYMBOL(d2fq_assign_dd);

/* update minimum virtual time among flows */
void d2fq_update_gvt(struct d2fq_data *dd)
{
	struct d2fq_global_data *dgd = dd->dgd;
	int cur_pid = dd->pid;
	u64 lvt = atomic64_read(&dd->lvt);

	/* struct, holder, value */
	gh_update_min(&dgd->gh, cur_pid, lvt);
	return;
}

/* backward vt progression */
void __d2fq_update_vt(struct d2fq_data *dd, unsigned cls, unsigned dlen)
{
	u64 tmp, lvt;

	tmp = (u64)((dd->vt_unit * dlen)>>10);

	switch (cls) {
#ifdef CONFIG_IOSCHED_D2FQ_VERBOSE
	case D2FQ_CLS_LOW:	atomic_add(tmp, &dd->pws_cnt_l); atomic_inc(&dd->cnt_l); break;
	case D2FQ_CLS_MEDIUM:	atomic_add(tmp, &dd->pws_cnt_m); atomic_inc(&dd->cnt_m); break;
	case D2FQ_CLS_HIGH:	atomic_add(tmp, &dd->pws_cnt_h); atomic_inc(&dd->cnt_h); break;
#else
	case D2FQ_CLS_LOW:	atomic_add(tmp, &dd->pws_cnt_l); break;
	case D2FQ_CLS_MEDIUM:	atomic_add(tmp, &dd->pws_cnt_m); break;
	case D2FQ_CLS_HIGH:	atomic_add(tmp, &dd->pws_cnt_h); break;
#endif
	default: break;
	}

	lvt = atomic64_add_return(tmp, &dd->lvt);
	if(unlikely(lvt >= U64_MAX-U8_MAX))
		printk(KERN_ERR "d2fq: overflow occurs on virtual time domain");
	/* Update global VT */
	d2fq_update_gvt(dd);
	/* no more inflight request */
	if(unlikely(atomic_dec_return(&dd->ir_cnt) < 1))
		timer_reduce(&dd->di_timer,
			jiffies + D2FQ_DI_SLICE*sysctl_d2fq_di_period);

	/* timer for dynamic weight adjustment */
	if (sysctl_d2fq_dw_en == 1)
		timer_reduce(&dd->dgd->timer,
			jiffies + D2FQ_PW_SLICE*sysctl_d2fq_dw_period);

	return;
}
EXPORT_SYMBOL(__d2fq_update_vt);

void exit_d2fq(struct task_struct *task)
{
	struct d2fq_data *dd = task->d2fq;
	u64 gvt;

	if (task == task->group_leader) {
#ifdef CONFIG_IOSCHED_D2FQ_VERBOSE
		gvt = gh_return_min(&dd->dgd->gh);

		printk(KERN_INFO
		"d2fq: (%5d) exit_d2fq: h=%9u | m=%9u | l=%9u || lvt=0x%9llx | gvt=0x%9llx |",
		current->pid,
		atomic_read(&dd->cnt_h), atomic_read(&dd->cnt_m), atomic_read(&dd->cnt_l),
		atomic64_read(&dd->lvt), gvt);

		list_del_init(&dd->ddlist);
#endif
		dd->exit_flag = 1;
		timer_reduce(&dd->di_timer,
			jiffies + D2FQ_DI_SLICE*sysctl_d2fq_di_period);
	}
}
EXPORT_SYMBOL(exit_d2fq);

int __d2fq_start_request(struct d2fq_data *dd)
{
	struct d2fq_global_data *dgd;
	u64 gvt, lvt, gap;
	unsigned int md_range;

	/* get global virtual time */
	dgd = (struct d2fq_global_data*)dd->dgd;
	gvt = gh_return_min(&dd->dgd->gh);
	lvt = atomic64_read(&dd->lvt);
	gap = lvt - gvt;

	/* inflight request counter */
	atomic_inc(&dd->ir_cnt);
	md_range = (sysctl_d2fq_thrsh * (100 - sysctl_d2fq_md_ratio)) / 100;

	/* if iam the only active_flow, send request to medium queue */
	if (unlikely(atomic_read(&dgd->nr_active_flows)==1)) {
		return D2FQ_CLS_MEDIUM;
	}
	/* LOW CLASS */
	else if (gap > (u64)sysctl_d2fq_thrsh) {
		return D2FQ_CLS_LOW;
	}
	/* MEDIUM CLASS */
	else if (gap > (u64)md_range) {
		return D2FQ_CLS_MEDIUM;
	}
	/* HIGH CLASS */
	else {
		return D2FQ_CLS_HIGH;
	}
	return 0;
}
EXPORT_SYMBOL(__d2fq_start_request);
#endif

