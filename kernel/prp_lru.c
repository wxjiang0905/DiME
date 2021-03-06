#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/init.h>      // included for __init and __exit macros
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <asm/paravirt.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <asm/pgtable_types.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <asm/pgtable_types.h>

#include "da_mem_lib.h"

#include "prp_lru.h"
#include "../common/da_debug.h"
#include "common.h"



/*****
 *
 *  Module params
 *
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abhishek Ghogare, Dhantu");
MODULE_DESCRIPTION("DiME LRU page replacement policy");


static int		kswapd_sleep_ms			= 1;
static ulong	free_list_max_size		= 4000ULL;

module_param(kswapd_sleep_ms, int, 0644);
module_param(free_list_max_size, ulong, 0644);
#define MIN_FREE_PAGES_PERCENT 	25				// percentage of local memory available in free list

MODULE_PARM_DESC(kswapd_sleep_ms, "Sleep time in ms of dime_kswapd thread");
MODULE_PARM_DESC(free_list_max_size, "Max size of free list");


static inline struct prp_lru_struct *to_prp_lru_struct(struct page_replacement_policy_struct *prp) {
	return container_of(prp, struct prp_lru_struct, prp);
}



#define PROCFS_MAX_SIZE		102400
#define PROCFS_NAME			"dime_prp_config"
static char procfs_buffer[PROCFS_MAX_SIZE];     // The buffer used to store character for this module
static unsigned long procfs_buffer_size = 0;    // The size of the buffer

static ssize_t procfile_read(struct file*, char*, size_t, loff_t*);
static ssize_t procfile_write(struct file *, const char *, size_t, loff_t *);

struct proc_dir_entry *dime_config_entry;

static struct file_operations cmd_file_ops = {  
	.owner = THIS_MODULE,
	.read = procfile_read,
	.write = procfile_write,
};

int init_dime_prp_config_procfs(void) {
	dime_config_entry = proc_create(PROCFS_NAME, S_IFREG | S_IRUGO, NULL, &cmd_file_ops);

	if (dime_config_entry == NULL) {
		remove_proc_entry(PROCFS_NAME, NULL);

		DA_ALERT("could not initialize /proc/%s\n", PROCFS_NAME);
		return -ENOMEM;
	}

	/*
	 * KUIDT_INIT is a macro defined in the file 'linux/uidgid.h'. KGIDT_INIT also appears here.
	 */
	proc_set_user(dime_config_entry, KUIDT_INIT(0), KGIDT_INIT(0));
	proc_set_size(dime_config_entry, 37);

	DA_INFO("proc entry \"/proc/%s\" created\n", PROCFS_NAME);
	return 0;
}

void cleanup_dime_prp_config_procfs(void) {
	remove_proc_entry(PROCFS_NAME, NULL);
	DA_INFO("proc entry \"/proc/%s\" removed\n", PROCFS_NAME);
}

static ssize_t procfile_read(struct file *file, char *buffer, size_t length, loff_t *offset) {
	int ret;
	int seg_size;
	
	if(*offset == 0) {
		// offset is 0, so first call to read the file.
		// Initialize buffer with config parameters currently set
		int i;
		//											 1  1A         1B            4         5        6         7        8         9          10        11         12        13         14         15          16         17          18        19         20        21         22        23        24        25	     26           27
		procfs_buffer_size = sprintf(procfs_buffer, "id kswp_sleep free_max_size free_size apc_size inpc_size aan_size inan_size free_evict apc_evict inpc_evict aan_evict inan_evict fapc_evict finpc_evict faan_evict finan_evict apc->free inpc->free aan->free inan->free apc->inpc inpc->apc aan->inan inan->aan inpc->apc_pf inan->aan_pf\n");
		for(i=0 ; i<dime.dime_instances_size ; ++i) {
			struct prp_lru_struct *prp = to_prp_lru_struct(dime.dime_instances[i].prp);
			ulong free_list_size = (MIN_FREE_PAGES_PERCENT * dime.dime_instances[i].local_npages)/100;
			free_list_size = free_list_size < free_list_max_size ? free_list_size : free_list_max_size;
			procfs_buffer_size += sprintf(procfs_buffer+procfs_buffer_size, 
											//1  1A   1B       4   5   6   7    8    9     10   11    12   13    14    15    16    17    18   19    20   21    22   23   24   25   26    27
											"%2d %10d %13lu %9lu %8lu %9lu %8lu %9lu %10lu %9lu %10lu %9lu %10lu %10lu %11lu %10lu %11lu %9lu %10lu %9lu %10lu %9lu %9lu %9lu %9lu %12lu %12lu\n", 
																		dime.dime_instances[i].instance_id,						// 1
																		kswapd_sleep_ms,										// 1A
																		free_list_size,											// 1B
																		atomic_long_read(&prp->free.size),						// 4
																		atomic_long_read(&prp->active_pc.size),					// 5
																		atomic_long_read(&prp->inactive_pc.size),				// 6
																		atomic_long_read(&prp->active_an.size),					// 7
																		atomic_long_read(&prp->inactive_an.size),				// 8
																		atomic_long_read(&prp->stats.free_evict),				// 9
																		atomic_long_read(&prp->stats.active_pc_evict),			// 10
																		atomic_long_read(&prp->stats.inactive_pc_evict),		// 11
																		atomic_long_read(&prp->stats.active_an_evict),			// 12
																		atomic_long_read(&prp->stats.inactive_an_evict),		// 13
																		atomic_long_read(&prp->stats.force_active_pc_evict),	// 14
																		atomic_long_read(&prp->stats.force_inactive_pc_evict),	// 15
																		atomic_long_read(&prp->stats.force_active_an_evict),	// 16
																		atomic_long_read(&prp->stats.force_inactive_an_evict),	// 17
																		atomic_long_read(&prp->stats.pc_active_to_free_moved),	// 18
																		atomic_long_read(&prp->stats.pc_inactive_to_free_moved),	// 19
																		atomic_long_read(&prp->stats.an_active_to_free_moved),		// 20
																		atomic_long_read(&prp->stats.an_inactive_to_free_moved),	// 21
																		atomic_long_read(&prp->stats.pc_active_to_inactive_moved),	// 22
																		atomic_long_read(&prp->stats.pc_inactive_to_active_moved),	// 23
																		atomic_long_read(&prp->stats.an_active_to_inactive_moved),	// 24
																		atomic_long_read(&prp->stats.an_inactive_to_active_moved),	// 25
																		atomic_long_read(&prp->stats.pc_inactive_to_active_pf_moved),	// 26
																		atomic_long_read(&prp->stats.an_inactive_to_active_pf_moved));	// 27
		}
	}

	// calculate max size of block that can be read
	seg_size = length < procfs_buffer_size ? length : procfs_buffer_size;
	if (*offset >= procfs_buffer_size) {
		ret  = 0;   // offset value beyond the available data to read, finish reading
	} else {
		memcpy(buffer, procfs_buffer, seg_size);
		*offset += seg_size;    // increment offset value
		ret = seg_size;         // return number of bytes read
	}

	return ret;
}

static ssize_t procfile_write(struct file *file, const char *buffer, size_t length, loff_t *offset) {
	return length;
}

// Append second lpl(local page list) to first
void append_local_page_list(struct lpl * first, struct lpl * second) {
	write_lock(&second->lock);
	if(!list_empty(&second->head)) {
		write_lock(&first->lock);

		second->head.prev->next	= &first->head;
		second->head.next->prev	= first->head.prev;

		first->head.prev->next	= second->head.next;
		first->head.prev		= second->head.prev;

		atomic_long_add(atomic_long_read(&second->size), &first->size);

		write_unlock(&first->lock);
	}
	write_unlock(&second->lock);
}

struct lpl_node_struct * evict_first_page(struct lpl *from_list) {
	struct lpl_node_struct * node_to_evict = NULL;

	write_lock(&from_list->lock);
	
	node_to_evict = list_first_entry_or_null(&from_list->head, struct lpl_node_struct, list_node);
	
	if(node_to_evict) {
		struct task_struct		* i_ts		= NULL;
		struct mm_struct		* i_mm		= NULL;
		pte_t					* i_ptep	= NULL;

		// remove node from list
		list_del_rcu(&node_to_evict->list_node);
		atomic_long_dec(&from_list->size);

		write_unlock(&from_list->lock);

		// get pte pointer
		i_ts	= pid_task(node_to_evict->pid_s, PIDTYPE_PID);
		i_mm	= (i_ts == NULL ? NULL : i_ts->mm);
		i_ptep	= (i_mm == NULL ? NULL : ml_get_ptep(i_mm, node_to_evict->address));

		// protect page
		ml_protect_pte(i_mm, node_to_evict->address, i_ptep);
	} else {
		write_unlock(&from_list->lock);
	}

	return node_to_evict;
}


struct lpl_node_struct * evict_single_page(struct lpl *from_list, struct lpl *active_list, int * from_to_active_moved) {
	struct lpl_node_struct * node_to_evict = NULL;
	struct lpl tmp_list =	{
								.head = LIST_HEAD_INIT(tmp_list.head),
								.size = ATOMIC_LONG_INIT(0),
								.lock = __RW_LOCK_UNLOCKED(tmp_list.lock)
							};
	struct list_head *iternode;

	write_lock(&from_list->lock);
	for(iternode = from_list->head.next ; iternode != &from_list->head ; iternode = iternode->next) {
		struct lpl_node_struct	* i_node	= NULL;
		struct task_struct		* i_ts		= NULL;
		struct mm_struct		* i_mm		= NULL;
		pte_t					* i_ptep	= NULL;
		int accessed, dirty;


		i_node = list_entry(iternode, struct lpl_node_struct, list_node);

		//if(c_pid == i_pid && c_addr == node->address)
			// if faulting page is same as the one we want to evict, continue,
			// since it is going to get protected and will recursively called
			// LESS likely happen
		//	continue;

		// remove iter node from list
		iternode = iternode->prev;
		list_del_rcu(&(i_node->list_node));
		atomic_long_dec(&from_list->size);

		i_ts	= pid_task(i_node->pid_s, PIDTYPE_PID);
		i_mm	= (i_ts == NULL ? NULL : i_ts->mm);
		i_ptep	= (i_mm == NULL ? NULL : ml_get_ptep(i_mm, i_node->address));
		if(!i_ptep) {
			node_to_evict = i_node;
			break;
		}

		accessed = pte_young(*i_ptep);
		dirty = pte_dirty(*i_ptep);
		// TODO:: what to do with dirty page?

		if(accessed) {
			list_add_tail_rcu(&(i_node->list_node), &tmp_list.head);
			atomic_long_inc(&tmp_list.size);
			(*from_to_active_moved)++;
		} else {
			node_to_evict = i_node;
			ml_protect_pte(i_mm, i_node->address, i_ptep);
			break;
		}
	}

	// reposition list head to this point, so that next time we wont scan again previously scanned nodes
	//	NO NEED TO REPOSITION HEAD, since it always will be pointing to latest
	//if(node_to_evict && iternode != &from_list->head) {
	//	list_del_rcu(&from_list->head);
	//	list_add_rcu(&from_list->head, iternode);
	//}
	write_unlock(&from_list->lock);


	// append all temp list nodes to active list
	append_local_page_list(active_list, &tmp_list);

	return node_to_evict;
}


int add_page(struct dime_instance_struct *dime_instance, struct pid * c_pid, ulong c_addr) {
	struct task_struct		* c_ts				= pid_task(c_pid, PIDTYPE_PID);
	struct mm_struct		* c_mm				= (c_ts == NULL ? NULL : c_ts->mm);
	pte_t					* c_ptep			= (c_mm == NULL ? NULL : ml_get_ptep(c_mm, c_addr));
	struct page				* c_page			= (c_ptep == NULL ? NULL : pte_page(*c_ptep));

	struct lpl_node_struct	* node_to_evict		= NULL;
	struct prp_lru_struct	* prp_lru			= to_prp_lru_struct(dime_instance->prp);
	int 					ret_execute_delay	= 1;

	if (dime_instance->local_npages == 0) {
		// no need to add this address
		// we can treat this case as infinite local pages, and no need to inject delay on any of the page
		ret_execute_delay = 1;
		goto EXIT_ADD_PAGE;
	} else if (dime_instance->local_npages > atomic_long_read(&prp_lru->lpl_count)) {
		// Since there is still free space locally for remote pages, delay should not be injected
		ret_execute_delay = 1;
		node_to_evict = (struct lpl_node_struct*) kmalloc(sizeof(struct lpl_node_struct), GFP_KERNEL);

		if(!node_to_evict) {
			DA_ERROR("unable to allocate memory");
			goto EXIT_ADD_PAGE;
		} else {
			atomic_long_inc(&prp_lru->lpl_count);
			atomic_long_inc(&prp_lru->stats.free_evict);
		}
	} else {
		while(node_to_evict == NULL) {
			int from_to_active_moved = 0;

			// TODO:: switch to the list that we had evicted from for last pagefault,
			//		  so that we dont have to iterate over all previous lists again unless kswapd thread has gone through all lists

			// search in free list
			write_lock(&prp_lru->free.lock);
			node_to_evict = list_first_entry_or_null(&prp_lru->free.head, struct lpl_node_struct, list_node);
			if(node_to_evict) {
				list_del_rcu(&node_to_evict->list_node);
				write_unlock(&prp_lru->free.lock);
				atomic_long_dec(&prp_lru->free.size);
				atomic_long_inc(&prp_lru->stats.free_evict);
				goto FREE_NODE_FOUND;
			} else {
				write_unlock(&prp_lru->free.lock);
			}

			// search from pagecache inactive list
			from_to_active_moved = 0;
			node_to_evict = evict_single_page(&prp_lru->inactive_pc, &prp_lru->active_pc, &from_to_active_moved);
			atomic_long_add(from_to_active_moved, &prp_lru->stats.pc_inactive_to_active_pf_moved);
			if(node_to_evict) {
				atomic_long_inc(&prp_lru->stats.inactive_pc_evict);
				goto FREE_NODE_FOUND;
			}

			// search from anon inactive list
			from_to_active_moved = 0;
			node_to_evict = evict_single_page(&prp_lru->inactive_an, &prp_lru->active_an, &from_to_active_moved);
			atomic_long_add(from_to_active_moved, &prp_lru->stats.an_inactive_to_active_pf_moved);
			if(node_to_evict) {
				atomic_long_inc(&prp_lru->stats.inactive_an_evict);
				goto FREE_NODE_FOUND;
			}

			// search from pagecache active list
			from_to_active_moved = 0;
			node_to_evict = evict_single_page(&prp_lru->active_pc, &prp_lru->active_pc, &from_to_active_moved);
			if(node_to_evict) {
				atomic_long_inc(&prp_lru->stats.active_pc_evict);
				goto FREE_NODE_FOUND;
			}

			// search from anon active list
			from_to_active_moved = 0;
			node_to_evict = evict_single_page(&prp_lru->active_an, &prp_lru->active_an, &from_to_active_moved);
			if(node_to_evict) {
				atomic_long_inc(&prp_lru->stats.active_an_evict);
				goto FREE_NODE_FOUND;
			}

			// forcefully select from pagecache inactive list
			node_to_evict = evict_first_page(&prp_lru->inactive_pc);
			if(node_to_evict) {
				atomic_long_inc(&prp_lru->stats.force_inactive_pc_evict);
				goto FREE_NODE_FOUND;
			}

			// forcefully select from anon inactive list
			node_to_evict = evict_first_page(&prp_lru->inactive_an);
			if(node_to_evict) {
				atomic_long_inc(&prp_lru->stats.force_inactive_an_evict);
				goto FREE_NODE_FOUND;
			}
			
			// forcefully select from pagecache active list
			node_to_evict = evict_first_page(&prp_lru->active_pc);
			if(node_to_evict) {
				atomic_long_inc(&prp_lru->stats.force_active_pc_evict);
				goto FREE_NODE_FOUND;
			}

			// forcefully select from anon active list
			node_to_evict = evict_first_page(&prp_lru->active_an);
			if(node_to_evict) {
				atomic_long_inc(&prp_lru->stats.force_active_an_evict);
				goto FREE_NODE_FOUND;
			}
			
			DA_WARNING("retrying to evict a page");
		}
	}


FREE_NODE_FOUND:

	node_to_evict->address = c_addr;
	node_to_evict->pid_s = c_pid;
	
	// Sometimes bulk pagefault requests come and evicting any page from these requests will again trigger pagefault.
	// This happens recursively if accessed bit is not set for each requested page.
	// So, set accessed bit to all requested pages
	// TODO:: verify requirement of this
	//ml_set_accessed_pte(c_mm, c_addr, c_ptep);
	ml_set_inlist_pte(c_mm, c_addr, c_ptep);


	if(c_page) {
		if( ((unsigned long)(c_page->mapping) & (unsigned long)0x01) != 0 ) {
			write_lock(&prp_lru->active_an.lock);
			list_add_tail_rcu(&(node_to_evict->list_node), &prp_lru->active_an.head);
			atomic_long_inc(&prp_lru->active_an.size);
			write_unlock(&prp_lru->active_an.lock);

			atomic_long_inc(&dime_instance->an_pagefaults);
		} else {
			write_lock(&prp_lru->active_pc.lock);
			list_add_tail_rcu(&(node_to_evict->list_node), &prp_lru->active_pc.head);
			atomic_long_inc(&prp_lru->active_pc.size);
			write_unlock(&prp_lru->active_pc.lock);

			atomic_long_inc(&dime_instance->pc_pagefaults);
		}
	} else {
		DA_ERROR("invalid c_page mapping : %p : %p", c_page, c_page->mapping);
	}

EXIT_ADD_PAGE:

	return ret_execute_delay;
}
/*
LRU pages belong to one of two linked list, the "active" and the "inactive" list. 
Page movement is driven by memory pressure. Pages are taken from the end of the inactive list to be freed. 
If the page has the reference bit set, it is moved to the beginning of the active list and the reference bit is cleared. 
If the page is dirty, writeback is commenced and the page is moved to the beginning of the inactive list. 
If the page is unreferenced and clean, it can be reused.

The kswapd thread in woken up by the physical page allocator only when the number of available free pages is less than pages_low.
zone->pages_low = (zone->pages_min * 5) / 4;   // in file mm/page_alloc.c

pagecache list size: As long as the working set is smaller than half of the file cache, it is completely protected from the page eviction code.
size of anonymous inactive list = Maybe 30% of anonymous pages on a 1GB system, but 1% of anonymous pages on a 1TB system?
*/

/*
 *	Returns statistics of moved pages around active/inactive lists.
 *	This function always sets values for pagecache statistic variables, calling function should update correct stats in original prp_struct.
 */
struct stats_struct balance_lists(struct lpl *active_list, struct lpl *inactive_list, int target, struct lpl *free) {
	struct stats_struct	stats					= {0};
	struct list_head 	* iternode				= NULL;
	struct lpl 			local_free_list			= {
													.head = LIST_HEAD_INIT(local_free_list.head),
													.size = ATOMIC_LONG_INIT(0),
													.lock = __RW_LOCK_UNLOCKED(local_free_list.lock)
												};
	struct lpl 			local_inactive_list		= {
													.head = LIST_HEAD_INIT(local_inactive_list.head),
													.size = ATOMIC_LONG_INIT(0),
													.lock = __RW_LOCK_UNLOCKED(local_inactive_list.lock)
												};
	struct lpl 			local_active_list		= {
													.head = LIST_HEAD_INIT(local_active_list.head),
													.size = ATOMIC_LONG_INIT(0),
													.lock = __RW_LOCK_UNLOCKED(local_active_list.lock)
												};

	// move non-accessed pages to inactive list
	write_lock(&active_list->lock);
	for(iternode = active_list->head.next ; iternode != &active_list->head && target > 0; iternode = iternode->next) {
		struct lpl_node_struct	* i_node	= list_entry(iternode, struct lpl_node_struct, list_node);
		struct task_struct		* i_ts		= pid_task(i_node->pid_s, PIDTYPE_PID);
		struct mm_struct		* i_mm		= (i_ts == NULL ? NULL : i_ts->mm);
		pte_t					* i_ptep	= (i_mm == NULL ? NULL : ml_get_ptep(i_mm, i_node->address));

		if(!i_ptep) {
			iternode = iternode->prev;

			list_del_rcu(&i_node->list_node);
			atomic_long_dec(&active_list->size);

			list_add_tail_rcu(&i_node->list_node, &local_free_list.head);
			atomic_long_inc(&local_free_list.size);

			target--;
			atomic_long_inc(&stats.pc_active_to_free_moved);
			continue;
		}

		// if page was not accessed
		if(!pte_young(*i_ptep)) {
			iternode = iternode->prev;

			list_del_rcu(&i_node->list_node);
			atomic_long_dec(&active_list->size);

			list_add_tail_rcu(&i_node->list_node, &local_inactive_list.head);
			atomic_long_inc(&local_inactive_list.size);

			target--;
			atomic_long_inc(&stats.pc_active_to_inactive_moved);
		} else {
			// clear accessed bit
			*i_ptep = pte_mkold(*i_ptep);
		}
	}

	// reposition list head to this point, so that next time we wont scan again previously scanned nodes
	if(iternode != &active_list->head) {
		list_del_rcu(&active_list->head);
		list_add_tail_rcu(&active_list->head, iternode);
	}
	write_unlock(&active_list->lock);


	// move accessed pages to active list
	write_lock(&inactive_list->lock);
	for(iternode = inactive_list->head.next ; iternode != &inactive_list->head; iternode = iternode->next) {
		struct lpl_node_struct	* i_node	= list_entry(iternode, struct lpl_node_struct, list_node);
		struct task_struct		* i_ts		= pid_task(i_node->pid_s, PIDTYPE_PID);
		struct mm_struct		* i_mm		= (i_ts == NULL ? NULL : i_ts->mm);
		pte_t					* i_ptep	= (i_mm == NULL ? NULL : ml_get_ptep(i_mm, i_node->address));

		if(!i_ptep) {
			iternode = iternode->prev;

			list_del_rcu(&i_node->list_node);
			atomic_long_dec(&inactive_list->size);

			list_add_tail_rcu(&i_node->list_node, &local_free_list.head);
			atomic_long_inc(&local_free_list.size);

			target--;
			atomic_long_inc(&stats.pc_inactive_to_free_moved);
			continue;
		}

		// if page was accessed
		if(pte_young(*i_ptep)) {
			iternode = iternode->prev;

			list_del_rcu(&i_node->list_node);
			atomic_long_dec(&inactive_list->size);

			list_add_tail_rcu(&i_node->list_node, &local_active_list.head);
			atomic_long_inc(&local_active_list.size);

			atomic_long_inc(&stats.pc_inactive_to_active_moved);
			*i_ptep = pte_mkold(*i_ptep);
		}
	}
	write_unlock(&inactive_list->lock);


	// append local lists to corresponding prp lists
	append_local_page_list(free, &local_free_list);
	append_local_page_list(active_list, &local_active_list);
	append_local_page_list(inactive_list, &local_inactive_list);

	return stats;
}

int try_to_free_pages(struct dime_instance_struct *dime_instance, struct lpl *pl, int target, struct lpl *free) {
	int						moved_free		= 0;
	struct list_head		* iternode 		= NULL;
	struct lpl 				local_free_list	= { 
												.head = LIST_HEAD_INIT(local_free_list.head),
												.size = ATOMIC_LONG_INIT(0),
												.lock = __RW_LOCK_UNLOCKED(local_free_list.lock)
											};


	write_lock(&pl->lock);
	for(iternode = pl->head.next ; iternode != &pl->head && target > 0; iternode = iternode->next) {
		struct lpl_node_struct	* i_node	= list_entry(iternode, struct lpl_node_struct, list_node);
		struct task_struct		* i_ts		= pid_task(i_node->pid_s, PIDTYPE_PID);
		struct mm_struct		* i_mm		= (i_ts == NULL ? NULL : i_ts->mm);
		pte_t					* i_ptep	= (i_mm == NULL ? NULL : ml_get_ptep(i_mm, i_node->address));

		if(!i_ptep) {
			iternode = iternode->prev;

			list_del_rcu(&i_node->list_node);
			atomic_long_dec(&pl->size);

			list_add_tail_rcu(&i_node->list_node, &local_free_list.head);
			atomic_long_inc(&local_free_list.size);

			target--;
			moved_free++;
			continue;
		}
		
		// if page was not accessed
		if(!pte_young(*i_ptep)) {
			iternode = iternode->prev;

			list_del_rcu(&i_node->list_node);
			atomic_long_dec(&pl->size);

			// inject delay if dirty page
			if(pte_dirty(*i_ptep)) {
				// emulate page flush, inject delay
				inject_delay(dime_instance, 0);

				// clear dirty bit
				*i_ptep = pte_mkclean(*i_ptep);
			}

			list_add_tail_rcu(&i_node->list_node, &local_free_list.head);
			atomic_long_inc(&local_free_list.size);

			ml_protect_pte(i_mm, i_node->address, i_ptep);
			target--;
			moved_free++;
		}
	}

	// reposition list head to this point, so that next time we wont scan again previously scanned nodes
	if(iternode != &pl->head) {
		list_del_rcu(&pl->head);
		list_add_tail_rcu(&pl->head, iternode);
	}
	write_unlock(&pl->lock);

	// append local free pages to prp free list
	append_local_page_list(free, &local_free_list);

	return moved_free;
}


// move inactive page from active list
int balance_local_page_lists(void) {
	int i = 0;
	struct prp_lru_struct *prp_lru = NULL;
	struct dime_instance_struct *dime_instance;

	for(i=0 ; i<dime.dime_instances_size ; ++i) {
		int free_target = 0;
		int required_free_size = 0;
		dime_instance = &(dime.dime_instances[i]);
		prp_lru = to_prp_lru_struct(dime_instance->prp);
		//if(prp_lru->lpl_count < dime_instance->local_npages)
			//|| prp_lru->free.size >= (MIN_FREE_PAGES_PERCENT * dime_instance->local_npages)/100)
			// no need to evict pages for this dime instance
		//	continue;

		required_free_size = (MIN_FREE_PAGES_PERCENT * dime_instance->local_npages)/100;
		required_free_size = required_free_size < free_list_max_size ? required_free_size : free_list_max_size;
		free_target = required_free_size - (atomic_long_read(&prp_lru->free.size) + dime_instance->local_npages - atomic_long_read(&prp_lru->lpl_count));
		if(free_target > 0) {
			/*int target_pi = prp_lru->inactive_pc.size - (18 * dime_instance->local_npages)/100;
			int target_pa = prp_lru->active_pc.size   - (27 * dime_instance->local_npages)/100;
			int target_ai = prp_lru->inactive_an.size - (18 * dime_instance->local_npages)/100;
			int target_aa = prp_lru->active_an.size   - (27 * dime_instance->local_npages)/100;
			int total_diff;

			target_pi = target_pi < 0 ? 0 : target_pi;
			target_pa = target_pa < 0 ? 0 : target_pa;
			target_ai = target_ai < 0 ? 0 : target_ai;
			target_aa = target_aa < 0 ? 0 : target_aa;

			total_diff = target_pi + target_pa + target_ai + target_aa;

			target_pi = (free_target*target_pi)/total_diff;
			target_pa = (free_target*target_pa)/total_diff;
			target_ai = (free_target*target_ai)/total_diff;
			target_aa = (free_target*target_aa)/total_diff;
			
			try_to_free_pages(dime_instance, &prp_lru->inactive_pc, target_pi, &prp_lru->free);
			try_to_free_pages(dime_instance, &prp_lru->inactive_an, target_ai, &prp_lru->free);
			try_to_free_pages(dime_instance, &prp_lru->active_pc, target_pa, &prp_lru->free);
			try_to_free_pages(dime_instance, &prp_lru->active_pc, target_aa, &prp_lru->free);
			*/

			free_target = required_free_size - (atomic_long_read(&prp_lru->free.size) + dime_instance->local_npages - atomic_long_read(&prp_lru->lpl_count));
			free_target = free_target > 0 ? try_to_free_pages(dime_instance, &prp_lru->inactive_pc, free_target, &prp_lru->free) : 0;
			atomic_long_add(free_target, &prp_lru->stats.pc_inactive_to_free_moved);
			
			free_target = required_free_size - (atomic_long_read(&prp_lru->free.size) + dime_instance->local_npages - atomic_long_read(&prp_lru->lpl_count));
			free_target = free_target > 0 ? try_to_free_pages(dime_instance, &prp_lru->inactive_an, free_target, &prp_lru->free) : 0;
			atomic_long_add(free_target, &prp_lru->stats.an_inactive_to_free_moved);
			
			free_target = required_free_size - (atomic_long_read(&prp_lru->free.size) + dime_instance->local_npages - atomic_long_read(&prp_lru->lpl_count));
			free_target = free_target > 0 ? try_to_free_pages(dime_instance, &prp_lru->active_pc, free_target, &prp_lru->free) : 0;
			atomic_long_add(free_target, &prp_lru->stats.pc_active_to_free_moved);
			
			free_target = required_free_size - (atomic_long_read(&prp_lru->free.size) + dime_instance->local_npages - atomic_long_read(&prp_lru->lpl_count));
			free_target = free_target > 0 ? try_to_free_pages(dime_instance, &prp_lru->active_pc, free_target, &prp_lru->free) : 0;
			atomic_long_add(free_target, &prp_lru->stats.an_active_to_free_moved);
		}

		/*if(prp_lru->inactive_pc.size < (prp_lru->active_pc.size+prp_lru->inactive_pc.size)*40/100)*/ {
			// need to move passive pages from active pagecache list to inactive list
			int target = atomic_long_read(&prp_lru->active_pc.size);//(prp_lru->active_pc.size+prp_lru->inactive_pc.size)*40/100 - prp_lru->inactive_pc.size;
			if(target>0) {
				struct stats_struct stats = balance_lists(&prp_lru->active_pc, &prp_lru->inactive_pc, target, &prp_lru->free);
				atomic_long_add(atomic_long_read(&stats.pc_inactive_to_free_moved)		, &prp_lru->stats.pc_inactive_to_free_moved);
				atomic_long_add(atomic_long_read(&stats.pc_active_to_free_moved)		, &prp_lru->stats.pc_active_to_free_moved);
				atomic_long_add(atomic_long_read(&stats.pc_inactive_to_active_moved)	, &prp_lru->stats.pc_inactive_to_active_moved);
				atomic_long_add(atomic_long_read(&stats.pc_active_to_inactive_moved)	, &prp_lru->stats.pc_active_to_inactive_moved);
			}
		}

		/*if(prp_lru->inactive_an.size < (prp_lru->active_an.size+prp_lru->inactive_an.size)*40/100)*/ {
			// need to move passive pages from active pagecache list to inactive list
			int target = atomic_long_read(&prp_lru->active_an.size);//(prp_lru->active_an.size+prp_lru->inactive_an.size)*40/100 - prp_lru->inactive_an.size;
			if(target>0) {
				struct stats_struct stats = balance_lists(&prp_lru->active_an, &prp_lru->inactive_an, target, &prp_lru->free);
				atomic_long_add(atomic_long_read(&stats.pc_inactive_to_free_moved)		, &prp_lru->stats.an_inactive_to_free_moved);
				atomic_long_add(atomic_long_read(&stats.pc_active_to_free_moved)		, &prp_lru->stats.an_active_to_free_moved);
				atomic_long_add(atomic_long_read(&stats.pc_inactive_to_active_moved)	, &prp_lru->stats.an_inactive_to_active_moved);
				atomic_long_add(atomic_long_read(&stats.pc_active_to_inactive_moved)	, &prp_lru->stats.an_active_to_inactive_moved);
			}
		}
	}

	return 0;
}


static struct task_struct *dime_kswapd;
static int dime_kswapd_fn(void *unused) {
	allow_signal(SIGKILL);
	while (!kthread_should_stop()) {
		msleep(kswapd_sleep_ms);
		//usleep_range(100,200);
		if (signal_pending(dime_kswapd))
			break;
		balance_local_page_lists();
	}
	DA_INFO("dime_kswapd thread STOPPING");
	do_exit(0);
	return 0;
}



void __lpl_CleanList (struct list_head *prp) {
	DA_ENTRY();

	while (!list_empty(prp)) {
		struct lpl_node_struct *node = list_first_entry(prp, struct lpl_node_struct, list_node);
		list_del_rcu(&node->list_node);
		kfree(node);
	}

	DA_EXIT();
}

void lpl_CleanList (struct dime_instance_struct *dime_instance) {
	struct prp_lru_struct *prp_lru = to_prp_lru_struct(dime_instance->prp);

	struct list_head * iternode = NULL;
	long int count = 0;
	for(iternode = prp_lru->free.head.next ; iternode != &prp_lru->free.head ; iternode=iternode->next) {
		count++;
	}
	if(count != atomic_long_read(&prp_lru->free.size)) {
		DA_ERROR("size mismatch : free : %ld expected %ld", count, atomic_long_read(&prp_lru->free.size));
	}

	count = 0;
	for(iternode = prp_lru->active_an.head.next ; iternode != &prp_lru->active_an.head ; iternode=iternode->next) {
		count++;
	}
	if(count != atomic_long_read(&prp_lru->active_an.size)) {
		DA_ERROR("size mismatch : active_an : %ld expected %ld", count, atomic_long_read(&prp_lru->active_an.size));
	}

	count = 0;
	for(iternode = prp_lru->inactive_an.head.next ; iternode != &prp_lru->inactive_an.head ; iternode=iternode->next) {
		count++;
	}
	if(count != atomic_long_read(&prp_lru->inactive_an.size)) {
		DA_ERROR("size mismatch : inactive_an : %ld expected %ld", count, atomic_long_read(&prp_lru->inactive_an.size));
	}

	count = 0;
	for(iternode = prp_lru->active_pc.head.next ; iternode != &prp_lru->active_pc.head ; iternode=iternode->next) {
		count++;
	}
	if(count != atomic_long_read(&prp_lru->active_pc.size)) {
		DA_ERROR("size mismatch : active_pc : %ld expected %ld", count, atomic_long_read(&prp_lru->active_pc.size));
	}
	
	count = 0;
	for(iternode = prp_lru->inactive_pc.head.next ; iternode != &prp_lru->inactive_pc.head ; iternode=iternode->next) {
		count++;
	}
	if(count != atomic_long_read(&prp_lru->inactive_pc.size)) {
		DA_ERROR("size mismatch : inactive_pc : %ld expected %ld", count, atomic_long_read(&prp_lru->inactive_pc.size));
	}

	__lpl_CleanList(&prp_lru->free.head);
	__lpl_CleanList(&prp_lru->active_an.head);
	__lpl_CleanList(&prp_lru->inactive_an.head);
	__lpl_CleanList(&prp_lru->active_pc.head);
	__lpl_CleanList(&prp_lru->inactive_pc.head);
}


int init_module(void) {
	int ret = 0;
	int i;
	DA_ENTRY();

	DA_INFO("creating dime_kswapd thread");
	dime_kswapd = kthread_create(dime_kswapd_fn, NULL, "dime_kswapd");
	if(dime_kswapd) {
		DA_INFO("dime_kswapd thread created successfully");
	} else {
		DA_ERROR("dime_kswapd thread creation failed");
		ret = -1;
		goto init_exit;
	}

	for(i=0 ; i<dime.dime_instances_size ; ++i) {
		struct prp_lru_struct *prp_lru = (struct prp_lru_struct*) kmalloc(sizeof(struct prp_lru_struct), GFP_KERNEL);
		if(!prp_lru) {
			DA_ERROR("unable to allocate memory");
			ret = -1; // TODO:: Error codes
			goto init_clean_kswapd;
		}

		rwlock_init(&(prp_lru->lock));
		rwlock_init(&(prp_lru->free.lock));
		rwlock_init(&(prp_lru->active_an.lock));
		rwlock_init(&(prp_lru->inactive_an.lock));
		rwlock_init(&(prp_lru->active_pc.lock));
		rwlock_init(&(prp_lru->inactive_pc.lock));
		write_lock(&prp_lru->lock);
		INIT_LIST_HEAD(&prp_lru->free.head);
		INIT_LIST_HEAD(&prp_lru->active_an.head);
		INIT_LIST_HEAD(&prp_lru->active_pc.head);
		INIT_LIST_HEAD(&prp_lru->inactive_an.head);
		INIT_LIST_HEAD(&prp_lru->inactive_pc.head);
		atomic_long_set(&prp_lru->lpl_count, 0);
		atomic_long_set(&prp_lru->free.size, 0);
		atomic_long_set(&prp_lru->active_pc.size, 0);
		atomic_long_set(&prp_lru->active_an.size, 0);
		atomic_long_set(&prp_lru->inactive_pc.size, 0);
		atomic_long_set(&prp_lru->inactive_an.size, 0);
		prp_lru->stats = (struct stats_struct) {0};

		prp_lru->prp.add_page = add_page;
		prp_lru->prp.clean = lpl_CleanList;


		// Set policy pointer at the end of initialization
		dime.dime_instances[i].prp = &(prp_lru->prp);
		write_unlock(&prp_lru->lock);
	}

	if(init_dime_prp_config_procfs()<0) {
		ret = -1;
		goto init_clean_instances;
	}

	ret = register_page_replacement_policy(NULL);
	DA_INFO("waking up dime_kswapd thread");
	wake_up_process(dime_kswapd);
	goto init_exit;

init_clean_instances:
	// TODO:: free allocated prp structs & deregister policy

init_clean_kswapd:
	if(dime_kswapd) {
		kthread_stop(dime_kswapd);
		DA_INFO("dime_kswapd thread stopped");
	}

init_exit:
	DA_EXIT();
	return ret;    // Non-zero return means that the module couldn't be loaded.
}
void cleanup_module(void) {
	int i;
	DA_ENTRY();

	if(dime_kswapd) {
		kthread_stop(dime_kswapd);
		DA_INFO("dime_kswapd thread stopped");
	}

	cleanup_dime_prp_config_procfs();

	for(i=0 ; i<dime.dime_instances_size ; ++i) {
		dime.dime_instances[i].prp->add_page = NULL;
		lpl_CleanList(&dime.dime_instances[i]);
		dime.dime_instances[i].prp = NULL;
	}

	deregister_page_replacement_policy(NULL);


	DA_INFO("cleaning up module complete");
	DA_EXIT();
}
