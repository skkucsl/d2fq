//#undef TRACE_SYSTEM
//#define TRACE_SYSTEM d2fq_trace
//
//#if !defined(_TRACE_D2FQ_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
//#define _TRACE_D2FQ_TRACE_H
//#include <linux/tracepoint.h>
//
//TRACE_EVENT(d2fq_wc,
//	TP_PROTO(unsigned long long rdtsc0, unsigned long long rdtsc1, unsigned long long rdtsc2,
//			unsigned long long rdtsc3, unsigned long long rdtsc4),
//	TP_ARGS(rdtsc0, rdtsc1, rdtsc2, rdtsc3, rdtsc4),
//	TP_STRUCT__entry(
//		__field( unsigned long long, rdtsc0)
//		__field( unsigned long long, rdtsc1)
//		__field( unsigned long long, rdtsc2)
//		__field( unsigned long long, rdtsc3)
//		__field( unsigned long long, rdtsc4)
//	),
//	TP_fast_assign(
//		__entry->rdtsc0 = rdtsc0;
//		__entry->rdtsc1 = rdtsc1;
//		__entry->rdtsc2 = rdtsc2;
//		__entry->rdtsc3 = rdtsc3;
//		__entry->rdtsc4 = rdtsc4;
//	),
//	TP_printk("%llu %llu %llu %llu %llu",
//				(unsigned long long)__entry->rdtsc0, (unsigned long long)__entry->rdtsc1,
//				(unsigned long long)__entry->rdtsc2, (unsigned long long)__entry->rdtsc3,
//				(unsigned long long)__entry->rdtsc4)
//);
//
//#endif
//#include <trace/define_trace.h>
//
