/*
 * Mutexes: blocking mutual exclusion locks
 *
 * started by Ingo Molnar:
 *
 *  Copyright (C) 2004, 2005, 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * This file contains the main data structure and API definitions.
 */
#ifndef __LINUX_MUTEX_H
#define __LINUX_MUTEX_H

#if defined(__VMKLNX__)
#include <asm/semaphore.h>
#include "vmkapi.h"

struct mutex {
	struct semaphore lock;
#if defined(VMX86_DEBUG)
	vmk_int32 owner;
#endif /* defined(VMX86_DEBUG) */
};

#if defined(VMX86_DEBUG)
#define DEFINE_MUTEX(mutexname) \
   struct mutex mutexname = { \
      .lock = __SEMAPHORE_INITIALIZER((mutexname).lock, 1), \
      .owner = 0, \
   }
#else /* defined(VMX86_DEBUG) */
/**
 *  DEFINE_MUTEX - Declares a mutex
 *  @mutexname: name of the mutex to declare
 *
 *  Declares a mutex, initializing its lock field.
 *
 *  SYNOPSIS:
 *  #define DEFINE_MUTEX(mutexname)
 *
 *  SEE ALSO:
 *  __SEMAPHORE_INITIALIZER
 *
 *  RETURN VALUE:
 *  NONE
 */
/* _VMKLNX_CODECHECK_: DEFINE_MUTEX */
#define DEFINE_MUTEX(mutexname) \
   struct mutex mutexname = { \
      .lock = __SEMAPHORE_INITIALIZER((mutexname).lock, 1) \
   }
#endif /* defined(VMX86_DEBUG) */

/**                                          
 *  mutex_init - Initialize mutex
 *  @m: mutex in question
 *                                           
 *  Initialize mutex
 *
 */                                          
/* _VMKLNX_CODECHECK_: mutex_init */
static inline void 
mutex_init(struct mutex *m)
{
   init_MUTEX(&m->lock);
#if defined(VMX86_DEBUG)
   m->owner  = 0;
#endif /* defined(VMX86_DEBUG) */
}

static inline void 
mutex_destroy(struct mutex *m)
{
}

/**                                          
 *  mutex_lock - Lock the mutex
 *  @m: mutex in question
 *                                           
 *  Lock the mutex.
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: mutex_lock */
static inline void 
mutex_lock(struct mutex *m)
{
#if defined(VMX86_DEBUG)
   vmk_WorldID world = vmk_WorldGetID();
   VMK_ASSERT(world != VMK_INVALID_WORLD_ID);

   BUG_ON(m->owner == world);
   down(&m->lock);
   m->owner = world;
#else /* !defined(VMX86_DEBUG) */
   down(&m->lock);
#endif /* defined(VMX86_DEBUG) */
}

/**                                          
 *  mutex_unlock - Unlock mutex
 *  @m: mutex in question
 *                                           
 *  Unlock the mutex
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: mutex_unlock */
static inline void 
mutex_unlock(struct mutex *m)
{
#if defined(VMX86_DEBUG)
   vmk_WorldID world = vmk_WorldGetID();
   VMK_ASSERT(world != VMK_INVALID_WORLD_ID);

   BUG_ON(m->owner != world);
   m->owner = 0;
   up(&m->lock);
#else /* !defined(VMX86_DEBUG) */
   up(&m->lock);
#endif /* defined(VMX86_DEBUG) */
}

/**                                          
 *  mutex_trylock - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: mutex_trylock */
static inline int 
mutex_trylock(struct mutex *m)
{
#if defined(VMX86_DEBUG)
   int s;
   vmk_WorldID world = vmk_WorldGetID();
   VMK_ASSERT(world != VMK_INVALID_WORLD_ID);

   s = down_trylock(&m->lock);

   if (s == 0) {
      m->owner = world;
      return 1;
   } else {
      return 0;
   }
#else /* !defined(VMX86_DEBUG) */
   return down_trylock(&m->lock) == 0 ? 1 : 0;
#endif /* defined(VMX86_DEBUG) */
}

/**                                          
 *  mutex_lock_interruptible - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: mutex_lock_interruptible */
static inline int 
mutex_lock_interruptible(struct mutex *m)
{
#if defined(VMX86_DEBUG)
   int s;
   vmk_WorldID world = vmk_WorldGetID();
   VMK_ASSERT(world != VMK_INVALID_WORLD_ID);

   BUG_ON(m->owner == world);
   s = down_interruptible(&m->lock);
   if (s) {
      return s;
   }
   m->owner = world;
   return 0;
#else /* !defined(VMX86_DEBUG) */
   return down_interruptible(&m->lock);
#endif /* defined(VMX86_DEBUG) */
}

#else /* !defined(__VMKLNX__) */
#include <linux/list.h>
#include <linux/spinlock_types.h>
#include <linux/linkage.h>
#include <linux/lockdep.h>

#include <asm/atomic.h>

/*
 * Simple, straightforward mutexes with strict semantics:
 *
 * - only one task can hold the mutex at a time
 * - only the owner can unlock the mutex
 * - multiple unlocks are not permitted
 * - recursive locking is not permitted
 * - a mutex object must be initialized via the API
 * - a mutex object must not be initialized via memset or copying
 * - task may not exit with mutex held
 * - memory areas where held locks reside must not be freed
 * - held mutexes must not be reinitialized
 * - mutexes may not be used in irq contexts
 *
 * These semantics are fully enforced when DEBUG_MUTEXES is
 * enabled. Furthermore, besides enforcing the above rules, the mutex
 * debugging code also implements a number of additional features
 * that make lock debugging easier and faster:
 *
 * - uses symbolic names of mutexes, whenever they are printed in debug output
 * - point-of-acquire tracking, symbolic lookup of function names
 * - list of all locks held in the system, printout of them
 * - owner tracking
 * - detects self-recursing locks and prints out all relevant info
 * - detects multi-task circular deadlocks and prints out all affected
 *   locks and tasks (and only those tasks)
 */
struct mutex {
	/* 1: unlocked, 0: locked, negative: locked, possible waiters */
	atomic_t		count;
	spinlock_t		wait_lock;
	struct list_head	wait_list;
#ifdef CONFIG_DEBUG_MUTEXES
	struct thread_info	*owner;
	const char 		*name;
	void			*magic;
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
};

/*
 * This is the control structure for tasks blocked on mutex,
 * which resides on the blocked task's kernel stack:
 */
struct mutex_waiter {
	struct list_head	list;
	struct task_struct	*task;
#ifdef CONFIG_DEBUG_MUTEXES
	struct mutex		*lock;
	void			*magic;
#endif
};

#ifdef CONFIG_DEBUG_MUTEXES
# include <linux/mutex-debug.h>
#else
# define __DEBUG_MUTEX_INITIALIZER(lockname)
# define mutex_init(mutex) \
do {							\
	static struct lock_class_key __key;		\
							\
	__mutex_init((mutex), #mutex, &__key);		\
} while (0)
# define mutex_destroy(mutex)				do { } while (0)
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define __DEP_MAP_MUTEX_INITIALIZER(lockname) \
		, .dep_map = { .name = #lockname }
#else
# define __DEP_MAP_MUTEX_INITIALIZER(lockname)
#endif

#define __MUTEX_INITIALIZER(lockname) \
		{ .count = ATOMIC_INIT(1) \
		, .wait_lock = SPIN_LOCK_UNLOCKED \
		, .wait_list = LIST_HEAD_INIT(lockname.wait_list) \
		__DEBUG_MUTEX_INITIALIZER(lockname) \
		__DEP_MAP_MUTEX_INITIALIZER(lockname) }

#define DEFINE_MUTEX(mutexname) \
	struct mutex mutexname = __MUTEX_INITIALIZER(mutexname)

extern void __mutex_init(struct mutex *lock, const char *name,
			 struct lock_class_key *key);

/***
 * mutex_is_locked - is the mutex locked
 * @lock: the mutex to be queried
 *
 * Returns 1 if the mutex is locked, 0 if unlocked.
 */
static inline int fastcall mutex_is_locked(struct mutex *lock)
{
	return atomic_read(&lock->count) != 1;
}

/*
 * See kernel/mutex.c for detailed documentation of these APIs.
 * Also see Documentation/mutex-design.txt.
 */
extern void fastcall mutex_lock(struct mutex *lock);
extern int fastcall mutex_lock_interruptible(struct mutex *lock);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
extern void mutex_lock_nested(struct mutex *lock, unsigned int subclass);
extern int mutex_lock_interruptible_nested(struct mutex *lock, unsigned int subclass);
#else
# define mutex_lock_nested(lock, subclass) mutex_lock(lock)
# define mutex_lock_interruptible_nested(lock, subclass) mutex_lock_interruptible(lock)
#endif

/*
 * NOTE: mutex_trylock() follows the spin_trylock() convention,
 *       not the down_trylock() convention!
 */
extern int fastcall mutex_trylock(struct mutex *lock);
extern void fastcall mutex_unlock(struct mutex *lock);
#endif /* defined(__VMKLNX__) */

#endif
