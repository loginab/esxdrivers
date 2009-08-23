/* interrupt.h */
#ifndef _LINUX_INTERRUPT_H
#define _LINUX_INTERRUPT_H

#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/bitops.h>
#include <linux/preempt.h>
#include <linux/cpumask.h>
#include <linux/irqreturn.h>
#include <linux/hardirq.h>
#include <linux/sched.h>
#include <linux/irqflags.h>
#include <asm/atomic.h>
#include <asm/ptrace.h>
#include <asm/system.h>

/*
 * These correspond to the IORESOURCE_IRQ_* defines in
 * linux/ioport.h to select the interrupt line behaviour.  When
 * requesting an interrupt without specifying a IRQF_TRIGGER, the
 * setting should be assumed to be "as already configured", which
 * may be as per machine or firmware initialisation.
 */
#define IRQF_TRIGGER_NONE	0x00000000
#define IRQF_TRIGGER_RISING	0x00000001
#define IRQF_TRIGGER_FALLING	0x00000002
#define IRQF_TRIGGER_HIGH	0x00000004
#define IRQF_TRIGGER_LOW	0x00000008
#define IRQF_TRIGGER_MASK	(IRQF_TRIGGER_HIGH | IRQF_TRIGGER_LOW | \
				 IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)
#define IRQF_TRIGGER_PROBE	0x00000010

/*
 * These flags used only by the kernel as part of the
 * irq handling routines.
 *
 * IRQF_DISABLED - keep irqs disabled when calling the action handler
 * IRQF_SAMPLE_RANDOM - irq is used to feed the random generator
 * IRQF_SHARED - allow sharing the irq among several devices
 * IRQF_PROBE_SHARED - set by callers when they expect sharing mismatches to occur
 * IRQF_TIMER - Flag to mark this interrupt as timer interrupt
 */
#define IRQF_DISABLED		0x00000020
#define IRQF_SAMPLE_RANDOM	0x00000040
#define IRQF_SHARED		0x00000080
#define IRQF_PROBE_SHARED	0x00000100
#define IRQF_TIMER		0x00000200
#define IRQF_PERCPU		0x00000400

/*
 * Migration helpers. Scheduled for removal in 1/2007
 * Do not use for new code !
 */
#define SA_INTERRUPT		IRQF_DISABLED
#define SA_SAMPLE_RANDOM	IRQF_SAMPLE_RANDOM
#define SA_SHIRQ		IRQF_SHARED
#define SA_PROBEIRQ		IRQF_PROBE_SHARED
#define SA_PERCPU		IRQF_PERCPU

#define SA_TRIGGER_LOW		IRQF_TRIGGER_LOW
#define SA_TRIGGER_HIGH		IRQF_TRIGGER_HIGH
#define SA_TRIGGER_FALLING	IRQF_TRIGGER_FALLING
#define SA_TRIGGER_RISING	IRQF_TRIGGER_RISING
#define SA_TRIGGER_MASK		IRQF_TRIGGER_MASK

#if !defined(__VMKLNX__)
struct irqaction {
	irqreturn_t (*handler)(int, void *, struct pt_regs *);
	unsigned long flags;
	cpumask_t mask;
	const char *name;
	void *dev_id;
	struct irqaction *next;
	int irq;
	struct proc_dir_entry *dir;
};

extern irqreturn_t no_action(int cpl, void *dev_id, struct pt_regs *regs);

extern int request_irq(unsigned int,
		       irqreturn_t (*handler)(int, void *, struct pt_regs *),
		       unsigned long, const char *, void *);
#else /* defined(__VMKLNX__) */

#if defined(__COMPAT_LAYER_2_6_18_PLUS__)
typedef irqreturn_t (*irq_handler_t)(int, void *);
#endif /* defined(__COMPAT_LAYER_2_6_18_PLUS__) */

#if defined(__USE_COMPAT_LAYER_2_6_18_PLUS__)

typedef irq_handler_t irqhandler_t;

#define IRQHANDLER_TYPE_ID VMKLNX_IRQHANDLER_TYPE2
#define IRQHANDLER_TYPE    handler_type2

#else /* !defined(__COMPAT_LAYER_2_6_18_PLUS__) */

typedef irqreturn_t (*irqhandler_t)(int, void *, struct pt_regs *);

#define IRQHANDLER_TYPE_ID VMKLNX_IRQHANDLER_TYPE1
#define IRQHANDLER_TYPE    handler_type1

#endif /* defined(__COMPAT_LAYER_2_6_18_PLUS__) */

/**                                          
 *  request_irq - allocate interrupt line
 *  @irq: interrupt line to allocate
 *  @handler: function to be called when irq occurs
 *  @flags: interrupt type flags
 *  @device: An ascii name for the claiming device
 *  @dev_id: A cookie passed back to the handler funtion
 *
 *  This call allocates interrupt resources and enables the interrupt line and
 *  IRQ handling. From the point this call is made the specified handler 
 *  function may be invoked. Since the specified handler function must clear any
 *  interrupt the device raises, the caller of this service must first initialize
 *  the hardware.
 *
 *  Dev_id must be globally unique. Normally the address of the device data
 *  structure is used as the dev_id "cookie". Since the handler receives this
 *  value, the cookie, it can make sense to use the data structure address.
 *
 *  If the requested interrupt is shared, then a non-NULL dev_id is required,
 *  as it will be used when freeing the interrupt.
 *
 *  Flags:
 *
 *  IRQF_SHARED        Interrupt is shared
 *  IRQF_DISABLED      Disable interrupts while processing
 *  IRQF_SAMPLE_RANDOM The interrupt can be used for entropy
 *
 *  ESX Deviation Notes:                     
 *  All interrupt handlers are used as a source for entropy, regardless of 
 *  IRQF_SAMPLE_RANDOM being set in the flags or not. 
 *
 */                                          
/* _VMKLNX_CODECHECK_: request_irq */
static inline int
request_irq(unsigned int irq,
            irqhandler_t handler,
            unsigned long flags,
            const char *device,
            void *dev_id)
{
   vmklnx_irq_handler_t irq_handler;

   irq_handler.IRQHANDLER_TYPE = handler;
   return vmklnx_request_irq(irq,
                             IRQHANDLER_TYPE_ID,
                             irq_handler,
                             flags,
                             device,
                             dev_id);
}

#endif /* !defined(__VMKLNX__) */

extern void free_irq(unsigned int, void *);

/*
 * On lockdep we dont want to enable hardirqs in hardirq
 * context. Use local_irq_enable_in_hardirq() to annotate
 * kernel code that has to do this nevertheless (pretty much
 * the only valid case is for old/broken hardware that is
 * insanely slow).
 *
 * NOTE: in theory this might break fragile code that relies
 * on hardirq delivery - in practice we dont seem to have such
 * places left. So the only effect should be slightly increased
 * irqs-off latencies.
 */
#ifdef CONFIG_LOCKDEP
# define local_irq_enable_in_hardirq()	do { } while (0)
#else
# define local_irq_enable_in_hardirq()	local_irq_enable()
#endif

#ifdef CONFIG_GENERIC_HARDIRQS
extern void disable_irq_nosync(unsigned int irq);
extern void disable_irq(unsigned int irq);
extern void enable_irq(unsigned int irq);

/*
 * Special lockdep variants of irq disabling/enabling.
 * These should be used for locking constructs that
 * know that a particular irq context which is disabled,
 * and which is the only irq-context user of a lock,
 * that it's safe to take the lock in the irq-disabled
 * section without disabling hardirqs.
 *
 * On !CONFIG_LOCKDEP they are equivalent to the normal
 * irq disable/enable methods.
 */
static inline void disable_irq_nosync_lockdep(unsigned int irq)
{
	disable_irq_nosync(irq);
#ifdef CONFIG_LOCKDEP
	local_irq_disable();
#endif
}

/**                                          
 *  disable_irq_lockdep - - disable an irq and synchronously wait for completion
 *  @irq: irq to disable
 *                                           
 *  Disables interrupts for the given IRQ.  Then wait for any in progress
 *  interrupts on this irq (possibly executing on other CPUs) to complete.  
 *
 *  Enables and Disables nest, in the sense that n disables are needed to
 *  counteract n enables.
 * 
 *  If you use this function while holding any resource the IRQ handler
 *  may need, you will deadlock.
 *                                           
 *  ESX Deviation Notes:                     
 *  Since vmklinux does not support CONFIG_LOCKDEP, disable_irq_lockdep()
 *  is equivalent to disable_irq().
 *
 *  RETURN VALUE:
 *  This function does not return a value.
 */                                          
/* _VMKLNX_CODECHECK_: disable_irq_lockdep */
static inline void disable_irq_lockdep(unsigned int irq)
{
	disable_irq(irq);
#ifdef CONFIG_LOCKDEP
	local_irq_disable();
#endif
}

/**                                          
 *  enable_irq_lockdep - enables interrupts for the given IRQ
 *  @irq: interrupt to enable
 *                                           
 *  Enables interrupts for the given IRQ
 *
 *  RETURN VALUE:
 *  This function does not return a value
 */                                          
/* _VMKLNX_CODECHECK_: enable_irq_lockdep */
static inline void enable_irq_lockdep(unsigned int irq)
{
#ifdef CONFIG_LOCKDEP
	local_irq_enable();
#endif
	enable_irq(irq);
}

/* IRQ wakeup (PM) control: */
extern int set_irq_wake(unsigned int irq, unsigned int on);

static inline int enable_irq_wake(unsigned int irq)
{
	return set_irq_wake(irq, 1);
}

static inline int disable_irq_wake(unsigned int irq)
{
	return set_irq_wake(irq, 0);
}

#else /* !CONFIG_GENERIC_HARDIRQS */
/*
 * NOTE: non-genirq architectures, if they want to support the lock
 * validator need to define the methods below in their asm/irq.h
 * files, under an #ifdef CONFIG_LOCKDEP section.
 */
# ifndef CONFIG_LOCKDEP
#  define disable_irq_nosync_lockdep(irq)	disable_irq_nosync(irq)
#  define disable_irq_lockdep(irq)		disable_irq(irq)
#  define enable_irq_lockdep(irq)		enable_irq(irq)
# endif

#endif /* CONFIG_GENERIC_HARDIRQS */

#ifdef CONFIG_HAVE_IRQ_IGNORE_UNHANDLED
int irq_ignore_unhandled(unsigned int irq);
#else
#define irq_ignore_unhandled(irq) 0
#endif

#ifndef __ARCH_SET_SOFTIRQ_PENDING
#define set_softirq_pending(x) (local_softirq_pending() = (x))
#define or_softirq_pending(x)  (local_softirq_pending() |= (x))
#endif

/*
 * Temporary defines for UP kernels, until all code gets fixed.
 */
#ifndef CONFIG_SMP
static inline void __deprecated cli(void)
{
	local_irq_disable();
}
static inline void __deprecated sti(void)
{
	local_irq_enable();
}
static inline void __deprecated save_flags(unsigned long *x)
{
	local_save_flags(*x);
}
#define save_flags(x) save_flags(&x)
static inline void __deprecated restore_flags(unsigned long x)
{
	local_irq_restore(x);
}

static inline void __deprecated save_and_cli(unsigned long *x)
{
	local_irq_save(*x);
}
#define save_and_cli(x)	save_and_cli(&x)
#endif /* CONFIG_SMP */

extern void local_bh_disable(void);
extern void __local_bh_enable(void);
extern void _local_bh_enable(void);
extern void local_bh_enable(void);
extern void local_bh_enable_ip(unsigned long ip);

/* PLEASE, avoid to allocate new softirqs, if you need not _really_ high
   frequency threaded job scheduling. For almost all the purposes
   tasklets are more than enough. F.e. all serial device BHs et
   al. should be converted to tasklets, not to softirqs.
 */

enum
{
	HI_SOFTIRQ=0,
	TIMER_SOFTIRQ,
	NET_TX_SOFTIRQ,
	NET_RX_SOFTIRQ,
	BLOCK_SOFTIRQ,
	TASKLET_SOFTIRQ
};

/* softirq mask and active fields moved to irq_cpustat_t in
 * asm/hardirq.h to get better cache usage.  KAO
 */

struct softirq_action
{
	void	(*action)(struct softirq_action *);
	void	*data;
};

asmlinkage void do_softirq(void);
extern void open_softirq(int nr, void (*action)(struct softirq_action*), void *data);
extern void softirq_init(void);
#if defined(__VMKLNX__)
extern void __cpu_raise_softirq(int, int);
extern void __raise_softirq_irqoff(unsigned int);
#else /* !defined(__VMKLNX__) */
#define __raise_softirq_irqoff(nr) do { or_softirq_pending(1UL << (nr)); } while (0)
#endif /* defined(__VMKLNX__) */
extern void FASTCALL(raise_softirq_irqoff(unsigned int nr));
extern void FASTCALL(raise_softirq(unsigned int nr));

/* Tasklets --- multithreaded analogue of BHs.

   Main feature differing them of generic softirqs: tasklet
   is running only on one CPU simultaneously.

   Main feature differing them of BHs: different tasklets
   may be run simultaneously on different CPUs.

   Properties:
   * If tasklet_schedule() is called, then tasklet is guaranteed
     to be executed on some cpu at least once after this.
   * If the tasklet is already scheduled, but its excecution is still not
     started, it will be executed only once.
   * If this tasklet is already running on another CPU (or schedule is called
     from tasklet itself), it is rescheduled for later.
   * Tasklet is strictly serialized wrt itself, but not
     wrt another tasklets. If client needs some intertask synchronization,
     he makes it with spinlocks.
 */

struct tasklet_struct
{
	struct tasklet_struct *next;
	unsigned long state;
	atomic_t count;
	void (*func)(unsigned long);
	unsigned long data;
#if defined(__VMKLNX__)
        vmk_ModuleID *module_id;
#endif /* if defined(__VMKLNX__) */
};

#if defined(__VMKLNX__)
#define DECLARE_TASKLET(name, func, data)                \
struct tasklet_struct name = {	NULL,                    \
				0,                       \
				ATOMIC_INIT(0),          \
				func,                    \
				data,                    \
				&vmklnx_this_module_id }

#define DECLARE_TASKLET_DISABLED(name, func, data)       \
struct tasklet_struct name = {	NULL,                    \
				0,                       \
				ATOMIC_INIT(1),          \
				func,                    \
				data,                    \
				&vmklnx_this_module_id }
#else /* !defined(__VMKLNX__) */
#define DECLARE_TASKLET(name, func, data) \
struct tasklet_struct name = { NULL, 0, ATOMIC_INIT(0), func, data }

#define DECLARE_TASKLET_DISABLED(name, func, data) \
struct tasklet_struct name = { NULL, 0, ATOMIC_INIT(1), func, data }
#endif /* defined(__VMKLNX__) */


enum
{
	TASKLET_STATE_SCHED,	/* Tasklet is scheduled for execution */
	TASKLET_STATE_RUN	/* Tasklet is running (SMP only) */
};

#ifdef CONFIG_SMP
static inline int tasklet_trylock(struct tasklet_struct *t)
{
	return !test_and_set_bit(TASKLET_STATE_RUN, &(t)->state);
}

static inline void tasklet_unlock(struct tasklet_struct *t)
{
	smp_mb__before_clear_bit(); 
	clear_bit(TASKLET_STATE_RUN, &(t)->state);
}

static inline void tasklet_unlock_wait(struct tasklet_struct *t)
{
	while (test_bit(TASKLET_STATE_RUN, &(t)->state)) { barrier(); }
}
#else
#define tasklet_trylock(t) 1
#define tasklet_unlock_wait(t) do { } while (0)
#define tasklet_unlock(t) do { } while (0)
#endif

extern void FASTCALL(__tasklet_schedule(struct tasklet_struct *t));

/**                                          
 *  tasklet_schedule - schedule tasklet for execution
 *  @t: pointer to tasklet_struct of tasklet to be scheduled
 *                                           
 *  Schedule the specifed tasklet for execution at the next safe
 *  opportunity.
 *
 *  return value: none
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: tasklet_schedule */
static inline void tasklet_schedule(struct tasklet_struct *t)
{
#if defined(__VMKLNX__)
	/*
	 * If the system has panic already, let's
	 * call the tasklet function directly here
	 * since this tasklet may be needed to service
	 * write completion for the core dumping device.
	 */
	if (unlikely(vmklnx_is_panic())) {
		t->func(t->data);
		return;
	}
#endif /* defined(__VMKLNX__) */
	if (!test_and_set_bit(TASKLET_STATE_SCHED, &t->state))
		__tasklet_schedule(t);
}

extern void FASTCALL(__tasklet_hi_schedule(struct tasklet_struct *t));

static inline void tasklet_hi_schedule(struct tasklet_struct *t)
{
	if (!test_and_set_bit(TASKLET_STATE_SCHED, &t->state))
		__tasklet_hi_schedule(t);
}


static inline void tasklet_disable_nosync(struct tasklet_struct *t)
{
	atomic_inc(&t->count);
	smp_mb__after_atomic_inc();
}

static inline void tasklet_disable(struct tasklet_struct *t)
{
	tasklet_disable_nosync(t);
	tasklet_unlock_wait(t);
	smp_mb();
}

static inline void tasklet_enable(struct tasklet_struct *t)
{
	smp_mb__before_atomic_dec();
	atomic_dec(&t->count);
}

static inline void tasklet_hi_enable(struct tasklet_struct *t)
{
	smp_mb__before_atomic_dec();
	atomic_dec(&t->count);
}

extern void tasklet_kill(struct tasklet_struct *t);
extern void tasklet_kill_immediate(struct tasklet_struct *t, unsigned int cpu);
#if defined(__VMKLNX__)
/**                                          
 *  tasklet_init - initialize tasklet structure
 *  @t: pointer to tasklet_struct to initialize
 *  @func: pointer to tasklet_function
 *  @data: data to pass to tasklet_function when run
 *                                           
 *  used to initialize a tasklet_struct in preparation for 
 *  tasklet scheduling
 *                                           
 *  return value: none
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: tasklet_init */
static inline void 
tasklet_init(struct tasklet_struct *t,
		  void (*func)(unsigned long), unsigned long data)
{
	t->next = NULL;
	t->state = 0;
	atomic_set(&t->count, 0);
	t->func = func;
	t->data = data;
        t->module_id = &vmklnx_this_module_id;
}
#else /* !defined(__VMKLNX__) */
extern void tasklet_init(struct tasklet_struct *t,
			 void (*func)(unsigned long), unsigned long data);
#endif /* defined(__VMKLNX__) */

/*
 * Autoprobing for irqs:
 *
 * probe_irq_on() and probe_irq_off() provide robust primitives
 * for accurate IRQ probing during kernel initialization.  They are
 * reasonably simple to use, are not "fooled" by spurious interrupts,
 * and, unlike other attempts at IRQ probing, they do not get hung on
 * stuck interrupts (such as unused PS2 mouse interfaces on ASUS boards).
 *
 * For reasonably foolproof probing, use them as follows:
 *
 * 1. clear and/or mask the device's internal interrupt.
 * 2. sti();
 * 3. irqs = probe_irq_on();      // "take over" all unassigned idle IRQs
 * 4. enable the device and cause it to trigger an interrupt.
 * 5. wait for the device to interrupt, using non-intrusive polling or a delay.
 * 6. irq = probe_irq_off(irqs);  // get IRQ number, 0=none, negative=multiple
 * 7. service the device to clear its pending interrupt.
 * 8. loop again if paranoia is required.
 *
 * probe_irq_on() returns a mask of allocated irq's.
 *
 * probe_irq_off() takes the mask as a parameter,
 * and returns the irq number which occurred,
 * or zero if none occurred, or a negative irq number
 * if more than one irq occurred.
 */

#if defined(CONFIG_GENERIC_HARDIRQS) && !defined(CONFIG_GENERIC_IRQ_PROBE) 
static inline unsigned long probe_irq_on(void)
{
	return 0;
}
static inline int probe_irq_off(unsigned long val)
{
	return 0;
}
static inline unsigned int probe_irq_mask(unsigned long val)
{
	return 0;
}
#else
extern unsigned long probe_irq_on(void);	/* returns 0 on failure */
extern int probe_irq_off(unsigned long);	/* returns 0 or negative on failure */
extern unsigned int probe_irq_mask(unsigned long);	/* returns mask of ISA interrupts */
#endif

#endif
