#ifndef __COMMON_H__
#define __COMMON_H__

#include <linux/mm.h>
#include "../common/da_debug.h"

struct dime_instance_struct;

struct page_replacement_policy_struct {
	int		(*add_page)		(struct dime_instance_struct *dime_instance, struct mm_struct * mm, ulong address);
	void	(*clean)		(struct dime_instance_struct *dime_instance);
};

struct dime_instance_struct {
	int		instance_id;
	int		pid[1000];
	int		pid_count;
	ulong	latency_ns;
	ulong	bandwidth_bps;
	ulong	local_npages;
	ulong	page_fault_count;

	struct page_replacement_policy_struct *prp;
};

extern struct dime_instance_struct dime_instance;


int register_page_replacement_policy(struct page_replacement_policy_struct *prp);
int deregister_page_replacement_policy(struct page_replacement_policy_struct *prp);

#endif //__COMMON_H__