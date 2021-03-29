#ifdef CONFIG_IOSCHED_D2FQ /* header file for d2fq.c */
#ifndef __NFSCHED_H
#define __NFSCHED_H

#include <linux/blkdev.h>
extern unsigned int sysctl_d2fq_read_weight;
extern unsigned int sysctl_d2fq_write_weight;
extern unsigned int sysctl_d2fq_thrsh;
extern unsigned int sysctl_d2fq_cls_base;
extern unsigned int sysctl_d2fq_perqw;
extern 		int sysctl_d2fq_md_ratio;
extern unsigned int sysctl_d2fq_di_period;
extern unsigned int sysctl_d2fq_dw_en;
extern unsigned int sysctl_d2fq_dw_period;

enum{
	D2FQ_CLS_HIGH	= 1,
	D2FQ_CLS_MEDIUM	= 2,
	D2FQ_CLS_LOW	= 3,
};

struct d2fq_data;
struct d2fq_global_data;

void exit_d2fq(struct task_struct *task);
int __d2fq_start_request(struct d2fq_data *dd);
void __d2fq_update_vt(struct d2fq_data *dd, unsigned cls, unsigned dlen);
void d2fq_init_dgd(struct request_queue *q);
struct d2fq_data* d2fq_assign_dd(void *dgd);

#ifdef CONFIG_IOSCHED_D2FQ_MULTISQ
void d2fq_set_request_cls(struct request *rq);
#else
unsigned int d2fq_set_request_cls(struct request_queue *q);
#endif
void d2fq_start_request(struct request *rq);
#endif
#endif
