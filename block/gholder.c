#include "gholder.h"

void init_gh (union global_holder *new, int flow_id, u64 lvt)
{
	new->fields.gmin_pid    = (uint64_t)flow_id;
	new->fields.gvt_min     = (uint64_t)lvt;
}

uint64_t gh_return_min (union global_holder *gh)
{
	return gh->fields.gvt_min;
}

uint64_t gh_update_min (union global_holder *gh, int flow_id, u64 lvt)
{
	union global_holder old, new;

	init_gh(&new, flow_id, lvt);
	while (true) {
		gh_set(old, *gh);
		if (likely(
		/* current holder - hold */
		    (old.fields.gmin_pid == new.fields.gmin_pid
		      && old.fields.gvt_min < new.fields.gvt_min) ||
		/* new holder - snatch */
		    (old.fields.gmin_pid != new.fields.gmin_pid
		      && old.fields.gvt_min > new.fields.gvt_min) ||
		/* no holder - be the one */
		    (old.fields.gmin_pid == NOHOLDER)))
		{
			if (bcas64(&gh->all, old.all, new.all)) {
				return 0;
			}
		} else {
			return 0;
		}
	}
}

int gh_holder_min (union global_holder *gh)
{
	return gh->fields.gmin_pid;
}

void gh_release_min (union global_holder *gh, int flow_id)
{
	union global_holder old, new;

	while (true) {
		gh_set(old, *gh);
		if (old.fields.gmin_pid == (uint64_t)flow_id) {
			init_gh(&new, NOHOLDER, old.fields.gvt_min);
			if (bcas64(&gh->all, old.all, new.all)) {
				return;
			}
		} else {
			return;
		}
	}
}

void gh_reset_min (union global_holder *gh)
{
	union global_holder new;

	init_gh(&new, NOHOLDER, 0);
	gh_set(*gh, new);
}
