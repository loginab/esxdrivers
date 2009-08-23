
/* **********************************************************
 * Copyright 1998, 2007-2008 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * vmklinux26_dist.h --
 *
 *      Prototypes for functions used in device drivers compiled for vmkernel.
 */

#ifndef _VMKLINUX26_DIST_H_
#define _VMKLINUX26_DIST_H_

#define INCLUDE_ALLOW_DISTRIBUTE

#if !defined(LINUX_MODULE_SKB_HEAP) && defined(NET_DRIVER)
#define LINUX_MODULE_SKB_HEAP
#endif

#include "linux/spinlock.h" /* for spinlock_t */
#include "linux/irqreturn.h" /* for irqreturn_t */
#include "vmklinux_version_dist.h"
#include "vmkapi.h"

#define VMKLNX_NOT_IMPLEMENTED()                                     \
      vmk_Panic("NOT_IMPLEMENTED %s:%d\n", __FILE__, __LINE__);

#define VMKLNX_ASSERT_NOT_IMPLEMENTED(condition)                     \
   do {                                                              \
      if (VMK_UNLIKELY(!(condition))) {                              \
         vmk_Panic("NOT_IMPLEMENTED %s:%d -- VMK_ASSERT(%s)\n",   \
                   __FILE__, __LINE__, #condition);                  \
      }                                                              \
   } while(0)

#define VMKLNX_ASSERT_BUG(bug, condition)                            \
   do {                                                              \
      if (VMK_UNLIKELY(!(condition))) {                              \
         vmk_Panic("bugNr%d at %s:%d -- VMK_ASSERT(%s)\n",           \
         bug, __FILE__, __LINE__, #condition);                       \
      }                                                              \
   } while(0)

#define JIFFIES_TO_USEC(j)	((vmk_uint64)(j)*10000)
#define USEC_TO_JIFFIES(u)	((signed long)((u)/10000))

#define VMKLNX_MODULE_CALL(modID, privData, callID, ret, func, args...) \
do {                                                                    \
   LinuxTask_SetPrivateData(privData, callID);                          \
   VMKAPI_MODULE_CALL(modID, ret, func, args);                          \
   LinuxTask_ClearPrivateData(privData, callID);                        \
} while (0)

#define VMKLNX_MODULE_CALL_VOID(modID, privData, callID, func, args...) \
do {                                                                    \
   LinuxTask_SetPrivateData(privData, callID);                          \
   VMKAPI_MODULE_CALL_VOID(modID, func, args);                          \
   LinuxTask_ClearPrivateData(privData, callID);                        \
} while (0)

struct task_struct;
struct scsi_cmnd;
struct device_driver;
struct work_struct_plus;

/*
 * Linux stubs functions.
 */

extern VMK_ReturnStatus vmklnx_errno_to_vmk_return_status(int error);
extern unsigned int vmklnx_get_dump_poll_retries(void);
extern unsigned int vmklnx_get_dump_poll_delay(void);

/*
 * Block device functions.
 */

#ifdef BLOCK_DRIVER

struct block_device_operations;

extern void vmklnx_block_init_start(void);
extern void vmklnx_block_init_done(int);
extern int vmklnx_register_blkdev(vmk_uint32 major, const char *name,
		               int bus, int devfn, void *data);
extern void vmklnx_block_register_sglimit(vmk_uint32 major, int sgSize);
extern void vmklnx_block_register_disk_maxXfer(vmk_uint32 major,
                                               int targetNum,
                                               vmk_uint64 maxXfer);
#endif // BLOCK_DRIVER

typedef enum {
   VMKLNX_IRQHANDLER_TYPE1 = 1,  /* linux 2.6.18 irq handler */
   VMKLNX_IRQHANDLER_TYPE2 = 2   /* linux 2.6.19 irq handler */
} vmklnx_irq_handler_type_id;

typedef union {
    irqreturn_t (*handler_type1)(int, void *, struct pt_regs *);
    irqreturn_t (*handler_type2)(int, void *);
} vmklnx_irq_handler_t;

extern unsigned int vmklnx_convert_isa_irq(unsigned int irq);
extern int vmklnx_request_irq(unsigned int irq,
			      vmklnx_irq_handler_type_id handler_type,
                              vmklnx_irq_handler_t handler,
                              unsigned long flags,
                              const char *device,
                              void *dev_id);

/*
 * Memory allocator functions
 */

#define __STR__(t)		# t
#define EXPAND_TO_STRING(n)	__STR__(n)
#define VMK_MODULE_HEAP_NAME	EXPAND_TO_STRING(LINUX_MODULE_HEAP_NAME)

#define __MAKE_MODULE_HEAP_ID(moduleID) moduleID ## _HeapID
#define __HEAP_ID(m) __MAKE_MODULE_HEAP_ID(m)
#define VMK_MODULE_HEAP_ID __HEAP_ID(LINUX_MODULE_HEAP_NAME)
extern vmk_HeapID VMK_MODULE_HEAP_ID;

#ifdef LINUX_MODULE_SKB_HEAP
#define __MAKE_MODULE_SKB_HEAP_NAME(moduleID) __STR__(moduleID ## _skb)
#define __SKB_HEAP_NAME(m) __MAKE_MODULE_SKB_HEAP_NAME(m)
#define VMK_MODULE_SKB_HEAP_NAME __SKB_HEAP_NAME(LINUX_MODULE_AUX_HEAP_NAME)

#define __MAKE_MODULE_SKB_HEAP_ID(moduleID) moduleID ## _SkbHeapID
#define __SKB_HEAP_ID(m) __MAKE_MODULE_SKB_HEAP_ID(m)
#define VMK_MODULE_SKB_HEAP_ID __SKB_HEAP_ID(LINUX_MODULE_AUX_HEAP_NAME)
extern vmk_HeapID VMK_MODULE_SKB_HEAP_ID;
#endif

#define __MAKE_MODULE_CODMA_HEAP_NAME(moduleID) __STR__(moduleID ## _codma)
#define __CODMA_HEAP_NAME(m) __MAKE_MODULE_CODMA_HEAP_NAME(m)
#define VMK_MODULE_CODMA_HEAP_NAME __CODMA_HEAP_NAME(LINUX_MODULE_AUX_HEAP_NAME)

struct kmem_cache_s;
struct device;
struct dma_pool;
struct pci_dev;

extern vmk_HeapID vmklnx_get_module_heap_id(void);
extern int vmklnx_module_heap_init(void);
extern int vmklnx_module_heap_cleanup(void);
extern void *vmklnx_kmalloc(vmk_HeapID heapID, size_t size, void *ra);
extern void *vmklnx_kzmalloc(vmk_HeapID heapID, size_t size);
extern void  vmklnx_kfree(vmk_HeapID heapID, const void *p);
extern void *vmklnx_kmalloc_align(vmk_HeapID heapID, size_t size, size_t align);
extern struct kmem_cache_s *
vmklnx_kmem_cache_create_with_props(vmk_HeapID heapID,
                                    const char *name,
                                    void (*ctor)(void *, struct kmem_cache_s *, unsigned long),
                                    void (*dtor)(void *, struct kmem_cache_s *, unsigned long),
                                    vmk_SlabProps *props);
extern struct kmem_cache_s *vmklnx_kmem_cache_create(vmk_HeapID heapID,
                                                     const char *name , 
                                                     size_t size, size_t offset, 
                                                     void (*ctor)(void *, struct kmem_cache_s *, unsigned long),
                                                     void (*dtor)(void *, struct kmem_cache_s *, unsigned long),
                                                     int trimFlag, int slabPercent);
extern int vmklnx_kmem_cache_destroy(struct kmem_cache_s *cache);
extern void *vmklnx_kmem_cache_alloc(struct kmem_cache_s *cache);
extern void vmklnx_kmem_cache_free(struct kmem_cache_s *cache, void *item);
extern vmk_ModuleID vmklnx_get_driver_module_id(const struct device_driver *drv);
extern char *vmklnx_kstrdup(vmk_HeapID heapID, const char *s, void *ra);
extern void vmklnx_dma_free_coherent_by_ma(struct device *dev, size_t size, dma_addr_t handle);
extern void vmklnx_dma_pool_free_by_ma(struct dma_pool *pool, dma_addr_t addr);
extern void vmklnx_pci_free_consistent_by_ma(struct pci_dev *hwdev, size_t size, dma_addr_t dma_handle);
extern void vmklnx_pci_pool_free_by_ma(struct dma_pool *pool, dma_addr_t addr);

struct vmklnx_pm_skb_cache {
   spinlock_t lock;
   int count;
   struct kmem_cache_s *cache;
};
extern struct vmklnx_pm_skb_cache vmklnx_pm_skb_cache;

struct vmklnx_codma {
   struct semaphore *mutex;
   u64 mask;
   vmk_HeapID heapID;
   char* heapName;
   u32 heapSize;
};
extern struct vmklnx_codma vmklnx_codma;

// per-module rcu data for vmklnx
struct vmklnx_rcu_data {
   // number of active RCU readlock(s)
   atomic64_t            nestingLevel;   /* number of RCU's entered */

   // Queue of callbacks waiting on this mnodule
   spinlock_t            lock;
   volatile int          qLen;           /* number of callsbacks */
   struct rcu_head       *nxtlist;       /* list of callbacks */
   struct rcu_head       **nxttail;      /* tail of callback list */

   // Batches completed
   long                  completed;

   // state of this module
   atomic64_t            generation;

   // tasklet to execute "call_rcu" callbacks
   struct tasklet_struct *callback;

   // timer for waiting on quiescent state
   struct timer_list     *delayTimer;
};
extern struct vmklnx_rcu_data vmklnx_rcu_data;

/*
 * Linux random functions.
 */
extern unsigned int vmklnx_register_add_randomness_functions(
   void (*add_interrupt_randomness)(int),
   void (*add_hardware_rng_device_randomness)(int),
   void (*add_keyboard_randomness)(int),
   void (*add_mouse_randomness)(int),
   void (*add_hid_other_randomness)(int),
   void (*add_storage_randomness)(int));
extern unsigned int vmklnx_unregister_add_randomness_functions(
   void (*add_interrupt_randomness)(int),
   void (*add_hardware_rng_device_randomness)(int),
   void (*add_keyboard_randomness)(int),
   void (*add_mouse_randomness)(int),
   void (*add_hid_other_randomness)(int),
   void (*add_storage_randomness)(int),
   int modID);

extern unsigned int vmklnx_register_get_random_byte_functions(
   vmk_GetEntropyFunction get_hardware_random_bytes,
   vmk_GetEntropyFunction get_hardware_non_blocking_random_bytes,
   vmk_GetEntropyFunction get_software_random_bytes,
   vmk_GetEntropyFunction get_software_only_random_bytes);
extern unsigned int vmklnx_unregister_get_random_byte_functions(
   vmk_GetEntropyFunction get_hardware_random_bytes,
   vmk_GetEntropyFunction get_hardware_non_blocking_random_bytes,
   vmk_GetEntropyFunction get_software_random_bytes,
   vmk_GetEntropyFunction get_software_only_random_bytes,
   int modID);

/*
 * workqueue functions.
 */

extern int vmklnx_cancel_work_sync(struct work_struct_plus *work,
                                   struct timer_list *timer);

/*
 * Misc vmklinux API
 */
#include "vmkapi.h"

static inline vmk_Bool
vmklnx_is_panic(void)
{
   return vmk_SystemCheckState(VMK_SYSTEM_STATE_PANIC);
}

extern vmk_ModuleID vmklnx_this_module_id;

#endif // _VMKLINUX26_DIST_H_
