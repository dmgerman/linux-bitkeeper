#ifndef _LINUX_PROFILE_H
#define _LINUX_PROFILE_H

#ifdef __KERNEL__
 
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/init.h>
#include <asm/errno.h>
 
enum profile_type {
	EXIT_TASK,
	EXIT_MMAP,
	EXEC_UNMAP
};

#ifdef CONFIG_PROFILING
 
struct notifier_block;
struct task_struct;
struct mm_struct;
 
/* task is in do_exit() */
void profile_exit_task(struct task_struct * task);
 
/* change of vma mappings */
void profile_exec_unmap(struct mm_struct * mm);

/* exit of all vmas for a task */
void profile_exit_mmap(struct mm_struct * mm);

int profile_event_register(enum profile_type, struct notifier_block * n);

int profile_event_unregister(enum profile_type, struct notifier_block * n);
 
#else

static inline int profile_event_register(enum profile_type t, struct notifier_block * n)
{
	return -ENOSYS;
}
 
static inline int profile_event_unregister(enum profile_type t, struct notifier_block * n)
{
	return -ENOSYS;
}
 
#define profile_exit_task(a) do { } while (0)
#define profile_exec_unmap(a) do { } while (0)
#define profile_exit_mmap(a) do { } while (0)
 
#endif /* CONFIG_PROFILING */
 
#endif /* __KERNEL__ */
 
#endif /* _LINUX_PROFILE_H */
