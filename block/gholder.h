#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/limits.h>

#define bcas64(p, o, n)         __sync_bool_compare_and_swap(p, o, n)
#define gh_set(dst, src_val)	WRITE_ONCE((dst), (src_val)) /* atomic operation */

#define NOHOLDER        0xffff

union global_holder
{
        volatile struct
        {
                uint64_t gmin_pid       : 16;
                uint64_t gvt_min        : 48;
        } fields;
        volatile uint64_t all;
} __attribute__ ((aligned(64)));

uint64_t gh_return_min (union global_holder *gh);
uint64_t gh_update_min (union global_holder *gh, int flow_id, u64 lvt);
int gh_holder_min (union global_holder *gh);
void gh_release_min (union global_holder *gh, int flow_id);
void gh_reset_min (union global_holder *gh);
