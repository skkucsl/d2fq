#ifdef CONFIG_IOSCHED_D2FQ /* header file for mqfq.c */
#ifndef __MQFQ_H
#define __MQFQ_H

#include <linux/blkdev.h>
void exit_mqfq_mic(struct task_struct *tsk);
#endif
#endif
