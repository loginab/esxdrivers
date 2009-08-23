/*
 * workqueue.h --- work queue handling for Linux.
 */

#ifndef _LINUX_WORKQUEUE_H
#define _LINUX_WORKQUEUE_H

#include <linux/timer.h>
#include <linux/linkage.h>
#include <linux/bitops.h>
#if defined(__VMKLNX__)
#include "vmklinux26_dist.h"
extern int LinuxWorkQueue_Init(void);
extern void LinuxWorkQueue_Cleanup(void);
#endif

struct workqueue_struct;

#if defined(__VMKLNX__)

struct work_struct_plus;
typedef void (*work_func_t)(struct work_struct_plus *work);
typedef void (*old_work_func_t)(void *data);

typedef union {
   work_func_t new;
   old_work_func_t old;
} work_func_union_t;


struct work_struct_plus {
	unsigned long pending;
	struct list_head entry;
	work_func_union_t func;
	volatile void *wq_data;
        vmk_ModuleID module_id;
};

struct delayed_work {
	struct work_struct_plus work;
        struct timer_list timer;
        void *data;
};

#if defined(__USE_COMPAT_LAYER_2_6_18_PLUS__)
#define work_struct work_struct_plus
#else
#define work_struct delayed_work
#endif 

#define __WORK_PLUS_INITIALIZER(n, wtype, ftype, f) {           \
        .pending    = wtype,                                    \
        .entry      = { &(n).entry, &(n).entry },               \
        .func.ftype = (f),                                      \
        .wq_data    = (NULL),                                   \
        .module_id  = (VMK_INVALID_MODULE_ID),                  \
        }

// define pending bits
#define __WORK_PENDING                  0
#define __WORK_OLD_COMPAT               1

// define pending bits by numeric value
#define __WORK_PENDING_BIT      (1 << __WORK_PENDING)
#define __WORK_OLD_COMPAT_BIT   (1 << __WORK_OLD_COMPAT)

#else /* !defined(__VMKLNX__) */
struct work_struct {
	unsigned long pending;
	struct list_head entry;
	void (*func)(void *);
        void *data;
	volatile void *wq_data;
	struct timer_list timer;
};
#endif /* defined(__VMKLNX__) */

struct execute_work {
	struct work_struct work;
};

#if defined(__VMKLNX__) && !defined(__USE_COMPAT_LAYER_2_6_18_PLUS__)

#define __WORK_INITIALIZER(n, f, d) {				             \
        .work  = __WORK_PLUS_INITIALIZER(n.work, (__WORK_OLD_COMPAT_BIT), old, f), \
        .timer = TIMER_INITIALIZER(NULL, 0, 0),			             \
	.data = (d),						             \
	}

#elif defined (__VMKLNX__) /* && defined(__USE_COMPAT_LAYER_2_6_18_PLUS__) */

#define __WORK_INITIALIZER(n, f) __WORK_PLUS_INITIALIZER(n, (0), new, f)

#define __DELAYED_WORK_INITIALIZER(n, f) {                      \
        .work = __WORK_INITIALIZER((n).work, (f)),              \
        .timer = TIMER_INITIALIZER(NULL, 0, 0),                 \
        }
#else /* !defined(__VMKLNX__) */
#define __WORK_INITIALIZER(n, f, d) {				\
	.entry  = { &(n).entry, &(n).entry },			\
	.func = (f),						\
	.data = (d),						\
	.timer = TIMER_INITIALIZER(NULL, 0, 0),			\
	}
#endif /* defined(__VMKLNX__) && !defined(__USE_COMPAT_LAYER_2_6_18_PLUS__) */

#if defined(__VMKLNX__) && defined(__USE_COMPAT_LAYER_2_6_18_PLUS__)
#define DECLARE_WORK(n, f)					\
	struct work_struct n = __WORK_INITIALIZER(n, f)
#else /* !defined(__VMKLNX__) || !defined(__USE_COMPAT_LAYER_2_6_18_PLUS__) */
/**
 *  DECLARE_WORK - Initialize a work_struct
 *  @n: name of the work_struct to declare
 *  @f: function to be called from the work queue
 *  @d: data argument to be passed to function
 *
 *  Initializes a work_struct with the given name, using the 
 *  specified callback function, work queue, and the given data
 *  argument for the callback function.  The callback function
 *  executes in the context of a work queue thread.
 *
 *  This macro is defined for drivers built both with and without
 *  __USE_COMPAT_LAYER_2_6_18_PLUS__, but the parameters are slightly
 *  different in the two cases.
 *
 *  If __USE_COMPAT_LAYER_2_6_18_PLUS__ is set, then there is no
 *  data argument.
 *
 *  SYNOPSIS:
 *
 *  #if defined __USE_COMPAT_LAYER_2_6_18_PLUS__
 *
 *  #define DECLARE_WORK(n, f)
 *
 *  #else
 *
 *  #define DECLARE_WORK(n, f, d)
 *
 *  #endif
 *
 *  RETURN VALUE:
 *  NONE
 */
/* _VMKLNX_CODECHECK_: DECLARE_WORK */
#define DECLARE_WORK(n, f, d)					\
	struct work_struct n = __WORK_INITIALIZER(n, f, d)
#endif /* defined(__VMKLNX__) && defined(__USE_COMPAT_LAYER_2_6_18_PLUS__) */

#if defined(__VMKLNX__) && defined(__USE_COMPAT_LAYER_2_6_18_PLUS__)
/*
 * initialize a work-struct's func and data pointers:
 */
#define PREPARE_WORK(_work, _func)			        \
	do {							\
		(_work)->func.new = _func;			\
	} while (0)

/*
 * Initialize all of a work-struct.
 * Documented under !defined(__USE_COMPAT_LAYER_2_6_18_PLUS__) section.
 */
#define INIT_WORK(_work, _func)	    			        \
	do {							\
		INIT_LIST_HEAD(&(_work)->entry);		\
		(_work)->pending = 0;	                        \
		PREPARE_WORK((_work), (_func));	                \
		(_work)->module_id = VMK_INVALID_MODULE_ID;	\
		(_work)->wq_data = NULL;			\
	} while (0)

#define INIT_DELAYED_WORK(_work, _func)                         \
        do {                                                    \
                INIT_WORK(&(_work)->work, (_func));             \
                init_timer(&(_work)->timer);                    \
        } while (0)
#elif defined (__VMKLNX__) /* && !defined(__USE_COMPAT_LAYER_2_6_18_PLUS__) */
/*
 * initialize a work-struct's func and data pointers:
 */
/**
 *  PREPARE_WORK - Initialize a work_struct
 *  @_dwork: name of the work_struct to prepare
 *  @_func: function to be called from the work queue
 *  @_data: data argument to be passed to function
 *
 *  Initializes the given work_struct (@_dwork), using the 
 *  specified callback function (@_func), and the given data
 *  argument (@_data) for the callback function.  The callback function
 *  executes in the context of a work queue thread.
 *
 *  This macro is defined for drivers built both with and without
 *  __USE_COMPAT_LAYER_2_6_18_PLUS__, but the parameters are slightly
 *  different in the two cases.
 *
 *  If __USE_COMPAT_LAYER_2_6_18_PLUS__ is set, then there is no
 *  @_data argument.
 *
 *  SYNOPSIS:
 *
 *  #if defined __USE_COMPAT_LAYER_2_6_18_PLUS__
 *
 *  #define PREPARE_WORK(_dwork, _func)
 *
 *  #else
 *
 *  #define PREPARE_WORK(_dwork, _func, _data)
 *
 *  #endif
 *
 *  RETURN VALUE:
 *  NONE
 */
/* _VMKLNX_CODECHECK_: PREPARE_WORK */
#define PREPARE_WORK(_dwork, _func, _data)			\
	do {							\
		(_dwork)->work.func.old = _func;		\
		(_dwork)->data = _data;			        \
	} while (0)

/*
 *  INIT_WORK - Initialize all of a work_struct
 *  @_dwork: name of the work_struct to prepare
 *  @_func: function to be called from the work queue
 *  @_data: data argument to be passed to function
 *
 *  Initializes the given work_struct (@_dwork)
 *
 *  This macro is defined for drivers built both with and without
 *  __USE_COMPAT_LAYER_2_6_18_PLUS__, but the parameters are slightly
 *  different in the two cases.
 *
 *  If __USE_COMPAT_LAYER_2_6_18_PLUS__ is set, then there is no
 *  @_data argument.
 *
 *  SYNOPSIS:
 *
 *  #if defined __USE_COMPAT_LAYER_2_6_18_PLUS__
 *
 *  #define INIT_WORK(_dwork, _func)
 *
 *  #else
 *
 *  #define INIT_WORK(_dwork, _func, _data)
 *
 *  #endif
 *
 *  RETURN VALUE:
 *  	None
 */
/* _VMKLNX_CODECHECK_: INIT_WORK */
#define INIT_WORK(_dwork, _func, _data)				        \
	do {							        \
		INIT_LIST_HEAD(&(_dwork)->work.entry);		        \
		(_dwork)->work.pending = __WORK_OLD_COMPAT_BIT;	        \
		PREPARE_WORK((_dwork), (_func), (_data));	        \
		(_dwork)->work.module_id = VMK_INVALID_MODULE_ID;	\
		(_dwork)->work.wq_data = NULL;			        \
		init_timer(&(_dwork)->timer);			        \
	} while (0)
#else /* !defined(__VMKLNX__) */
/*
 * initialize a work-struct's func and data pointers:
 */
#define PREPARE_WORK(_dwork, _func, _data)			\
	do {							\
		(_work)->func = _func;				\
		(_work)->data = _data;				\
	} while (0)

/*
 * initialize all of a work-struct:
 */
#define INIT_WORK(_work, _func, _data)				\
	do {							\
		INIT_LIST_HEAD(&(_work)->entry);		\
		(_work)->pending = 0;				\
		PREPARE_WORK((_work), (_func), (_data));	\
		init_timer(&(_work)->timer);			\
	} while (0)
#endif /* defined(__VMKLNX__) && defined(__USE_COMPAT_LAYER_2_6_18_PLUS__) */

extern struct workqueue_struct *__create_workqueue(const char *name,
						    int singlethread);
/**                                          
 *  create_workqueue - create a multithreaded workqueue
 *  @name: pointer to given wq name
 *                                                     
 *  Create and initialize a workqueue struct
 *  
 *  SYNOPSIS:
 *     #define create_workqueue(name)
 *  
 *  RETURN VALUE:
 *  If successful: pointer to the workqueue struct
 *  NULL otherwise
 *  
 *  ESX Deviation Notes:
 *  For ESX, the callback function is not bound to a physical CPU.
 *  For multi-threaded workqueues under Linux, each callback
 *  function stays bound to a single CPU once it starts executing.
 */
/* _VMKLNX_CODECHECK_: create_workqueue */
#define create_workqueue(name) __create_workqueue((name), 0)

/**                                          
 *  create_singlethread_workqueue - create a singlethreaded workqueue
 *  @name: pointer to given wq name
 *                                                     
 *  Create and initialize a singlethreaded workqueue struct
 *  
 *  SYNOPSIS:
 *     #define create_singlethread_workqueue(name)
 *  
 *  RETURN VALUE:
 *  If successful: pointer to the workqueue struct
 *  NULL otherwise
 *  
 *  ESX Deviation Notes:
 *  For ESX, the callback function is not bound to a physical CPU.
 *  For single threaded workqueues, Linux binds all callback functions to
 *  a single CPU.  
 */
/* _VMKLNX_CODECHECK_: create_singlethread_workqueue */
#define create_singlethread_workqueue(name) __create_workqueue((name), 1)

extern void destroy_workqueue(struct workqueue_struct *wq);

#if !defined(__VMKLNX__)
extern int FASTCALL(queue_work(struct workqueue_struct *wq, struct work_struct *work));
#else /* defined(__VMKLNX__) */
extern int FASTCALL(vmklnx_queue_work(struct workqueue_struct *wq, struct work_struct_plus *work));

/**                                          
 *  queue_work - queue a work struct on the given work queue.
 *  @wq: Workqueue on which to queue work
 *  @work: Workstruct to queue
 *  
 *  ESX Deviation Notes:                     
 *  For ESX, the callback function is not bound to a physical CPU.
 *  For single threaded workqueues, Linux binds all callback functions to
 *  a single CPU.  For multi-threaded workqueues under Linux, each callback
 *  function stays bound to a single CPU once it starts executing.
 */                                          
/* _VMKLNX_CODECHECK_: queue_work */
#if defined(__USE_COMPAT_LAYER_2_6_18_PLUS__)
static inline int queue_work(struct workqueue_struct *wq,
                             struct work_struct_plus *work)
{
   return vmklnx_queue_work(wq, work);
}
#else /* !defined(__USE_COMPAT_LAYER_2_6_18_PLUS__) */
static inline int queue_work(struct workqueue_struct *wq,
                             struct delayed_work *dwork)
{
   return vmklnx_queue_work(wq, &dwork->work);
}
#endif /* defined(__USE_COMPAT_LAYER_2_6_18_PLUS__) */
#endif /* !defined(__VMKLNX__) */
extern int FASTCALL(queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork, unsigned long delay));
extern int queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
	struct work_struct *work, unsigned long delay);
extern void FASTCALL(flush_workqueue(struct workqueue_struct *wq));

#if !defined(__VMKLNX__)
extern int FASTCALL(schedule_work(struct work_structs *work));
#else /* defined(__VMKLNX__) */
extern int FASTCALL(vmklnx_schedule_work(struct work_struct_plus *work));

/**
 *  schedule_work - put work task in global workqueue
 *  @work: job to be done
 *
 *  This puts a job in the kernel-global workqueue.
 *
 *  RETURN VALUE:
 *  0 on success, non-zero on error 
 */                                          
/* _VMKLNX_CODECHECK_: schedule_work */
#if defined(__USE_COMPAT_LAYER_2_6_18_PLUS__)
static inline int schedule_work(struct work_struct_plus *work)
{
   return vmklnx_schedule_work(work);
}
#else /* !defined(__USE_COMPAT_LAYER_2_6_18_PLUS__) */
static inline int schedule_work(struct delayed_work *dwork)
{
   return vmklnx_schedule_work(&dwork->work);
}
#endif /* defined(__USE_COMPAT_LAYER_2_6_18_PLUS__) */
#endif /* !defined(__VMKLNX__) */
extern int FASTCALL(schedule_delayed_work(struct delayed_work *dwork, unsigned long delay));

extern int schedule_delayed_work_on(int cpu, struct delayed_work *dwork, unsigned long delay);
extern int schedule_on_each_cpu(void (*func)(void *info), void *info);

extern void flush_scheduled_work(void);
extern int current_is_keventd(void);
extern int keventd_up(void);

extern void init_workqueues(void);
void cancel_rearming_delayed_work(struct work_struct *work);
void cancel_rearming_delayed_workqueue(struct workqueue_struct *,
				       struct work_struct *);
int execute_in_process_context(void (*fn)(void *), void *,
			       struct execute_work *);


#if defined(__USE_COMPAT_LAYER_2_6_18_PLUS__)
/**                                          
 *  cancel_work_sync - block until a work_struct's callback has terminated
 *  @work: the work to be cancelled
 *                                           
 * cancel_work_sync() will cancel the work if it is queued.  If one or more
 * invocations of the work's callback function are running, cancel_work_sync()
 * will block until all invocations have completed.
 *
 * It is possible to use this function if the work re-queues itself. However,
 * if work jumps from one queue to another, then completion of the callback
 * is only guaranteed from the last workqueue.
 *
 * cancel_delayed_work_sync() should be used for cases where the
 * work structure was queued by queue_delayed_work().
 *
 * The caller must ensure that workqueue_struct on which this work was last
 * queued can't be destroyed before this function returns.
 *
 * RETURN VALUE:
 * 1 if the work structure was queued on a workqueue and 0 otherwise.
 */                                          
/* _VMKLNX_CODECHECK_: cancel_work_sync */
static inline int cancel_work_sync(struct work_struct_plus *work)
{
   return vmklnx_cancel_work_sync(work, NULL);
}

/**                                          
 *  cancel_delayed_work_sync - block until a work_struct's callback has terminated
 *  @dwork: the work to be cancelled
 *                                           
 * cancel_delayed_work_sync() will cancel the work if it is queued, or if the delay
 * timer is pending.  If one or more invocations of the work's callback function
 * are running, cancel_delayed_work_sync() will block until all invocations have
 * completed.
 *
 * It is possible to use this function if the work re-queues itself. However,
 * if work jumps from one queue to another, then completion of the callback
 * is only guaranteed from the last workqueue.
 *
 * The caller must ensure that workqueue_struct on which this work was last
 * queued can't be destroyed before this function returns.
 *
 * RETURN VALUE:
 * 1 if the work structure was waiting on a timer or was queued on a workqueue,
 * and 0 otherwise.
 */                                          
/* _VMKLNX_CODECHECK_: cancel_delayed_work_sync */
static inline int cancel_delayed_work_sync(struct delayed_work *dwork)
{
   return vmklnx_cancel_work_sync(&dwork->work, &dwork->timer);
}
#endif /* defined(__USE_COMPAT_LAYER_2_6_18_PLUS__) */

/*
 * Kill off a pending schedule_delayed_work().  Note that the work callback
 * function may still be running on return from cancel_delayed_work().  Run
 * flush_scheduled_work() to wait on it.
 */
/**                                          
 *  cancel_delayed_work - Cancel the timer associated with a delayed work structure
 *  @dwork: The work structure.
 *                                           
 *  The timer associated with the specified work structure, if previously
 *  passed to schedule_delayed_work, is cancelled.  However, if the timer
 *  has already fired this function has no effect.  Use function
 *  flush_workqueue() or cancel_delayed_work_sync() if it is necessary to
 *  wait for the callout function to terminate.
 *                                           
 *  RETURN VALUE:
 *  1 if the timer was pending at the time when cancel_delayed_work() was called,
 *  and 0 otherwise.
 */                                          
/* _VMKLNX_CODECHECK_: cancel_delayed_work */
#if defined(__VMKLNX__)
static inline int cancel_delayed_work(struct delayed_work *dwork)
{
   if(NULL != dwork) {
      int ret;
      ret = del_timer_sync(&dwork->timer);
      if (ret) {
         /*
          * Timer was still pending
 	  */
         clear_bit(__WORK_PENDING, &dwork->work.pending);
      }
      return ret;
   } else {
      return(0);
   }
}
#else /* !defined(__VMKLNX__) */
static inline int cancel_delayed_work(struct delayed_work *work)
{
	int ret;

	ret = del_timer_sync(&work->timer);
	if (ret)
		clear_bit(__WORK_PENDING, &work->pending);
	return ret;
}
#endif /* defined(__VMKLNX__) */

#endif
