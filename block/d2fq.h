#define Q_STAT_ONWORK		0x0004
#define D2FQ_PW_SLICE ((u64)(1))	/* weight setting slice */
#define D2FQ_DI_SLICE ((u64)(1))	/* deceptive idleness slice */
#define D2FQ_RS_SLICE ((u64)(5*HZ))	/* reset timer */

#ifdef D2FQ_STAT
struct d2fq_cpu_latency {
        unsigned long long cnt;
        unsigned long long tot;
        unsigned long long tmp;
} __attribute__ ((aligned(64)));
#endif

extern ssize_t queue_d2fq_en_show(struct request_queue *q, char *page);
extern ssize_t queue_d2fq_en_store(struct request_queue *q, const char *page, size_t count);

struct d2fq_data* d2fq_assign_dd(void *dgd);
int __d2fq_start_request(struct d2fq_data *dd);
