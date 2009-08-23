/* ***************************************************************************
 * Copyright 2005-2008 VMware, Inc.  All rights reserved.
 *
 * **************************************************************************/

/*
 * vmklinux_module.c --
 *
 *	This file defines the entry points for the early module init and late
 *	cleanup routines for modules that uses the services from vmklinux26.
 *
 *      Currently, the early module init routine is used to create the
 *	module private heap and the late cleanup routine is used to destroy
 *	the heap. The private heap guarantees that modules and other kernel
 *	code don't step on each other's toes. This functionality is available
 *	to any module that links with this code. Options specifying the size
 *	of the private heap, among other things, must be present within the 
 *	module's Makefile, providing defined preprocessing values to the 
 *	compiler. Those preprocessing defines should be:
 *
 *	Optional:
 *	- VMKLINUX_MODULE_USE_EXTERNAL_HEAP
 *	- VMKLINUX_MODULE_HEAP_HIGH_MEM
 *	- VMKLINUX_MODULE_HEAP_ANY_MEM
 *	- VMKLINUX_MODULE_HEAP_4GB_MEM
 *	- VMKLINUX_MODULE_HEAP_PHYS_ANY_CONTIGUITY
 *	- VMKLINUX_MODULE_HEAP_PHYS_CONTIGUOUS
 *	- VMKLINUX_MODULE_HEAP_PHYS_DISCONTIGUOUS
 *
 *	Required if VMKLINUX_MODULE_USE_EXTERNAL_HEAP is not defined:
 *	
 *	- LINUX_MODULE_HEAP_INITIAL=<number-in-bytes-guaranteed-allocated>
 *	- LINUX_MODULE_HEAP_MAX=<number-in-bytes-of-maximum-allocation>
 *	- LINUX_MODULE_HEAP_NAME=<string>
 *
 *      These optional parameters provide the means to control exactly which 
 *      type of heap is created for a given module. The default is for a heap
 *     	to be 4GB_MEM and CONTIGUOUS, to match the type of memory returned to
 *     	drivers in Linux.
 *	
 *      However, if at all possible, drivers should be transitioned from 
 *      LOW->ANY, and from CONTIGUOUS->ANY_CONTIGUITY. The remaining two options,
 *      HIGH and DISCONTIGUOUS, should be mainly used while performing driver
 *      functionality sanity checks while making these transitions.
 */

#include "vmkapi.h"
#include "vmklinux_version_dist.h"
#include "vmklinux26_dist.h"
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>

#ifndef MODULE
#error "You can only compile and link vmklinux_module with modules, which" \
       "means that MODULE has to be defined when compiling it..."
#endif

/******************************************************************************
 *                                                                            *
 *               M O D U L E   H E A P   S E C T I O N                        *
 *                                                                            *
 ******************************************************************************/
#ifndef VMKLINUX_MODULE_USE_EXTERNAL_HEAP
/*
 * By default, we use an internal module private heap,
 * unless the module compilation explicitly especified
 * to use an external heap.
 */
#define VMKLINUX_MODULE_USE_INTERNAL_HEAP
#endif

#ifdef VMKLINUX_MODULE_USE_INTERNAL_HEAP /* { */
/*
 * This is the beginning of a big #ifdef section
 */

#ifndef LINUX_MODULE_HEAP_INITIAL
#error
#endif

#ifndef LINUX_MODULE_HEAP_MAX
#error
#endif

#ifndef LINUX_MODULE_HEAP_NAME
#error
#endif

#if LINUX_MODULE_HEAP_INITIAL > LINUX_MODULE_HEAP_MAX
#error
#endif

#if LINUX_MODULE_HEAP_INITIAL < 0
#error
#endif

/* Can only define one HEAP_PHYS attribute */
#ifdef VMKLINUX_MODULE_HEAP_PHYS_ANY_CONTIGUITY
#ifdef VMKLINUX_MODULE_HEAP_PHYS_CONTIGUOUS
#error
#endif
#ifdef VMKLINUX_MODULE_HEAP_PHYS_DISCONTIGUOUS
#error
#endif

#define PHYS_TYPE (VMK_HEAP_PHYS_ANY_CONTIGUITY)

#elif VMKLINUX_MODULE_HEAP_PHYS_DISCONTIGUOUS
#ifdef VMKLINUX_MODULE_HEAP_PHYS_CONTIGUOUS
#error
#endif

#define PHYS_TYPE (VMK_HEAP_PHYS_DISCONTIGUOUS)

#else // Nothing defined or PHYS_CONTIGOUS

#define PHYS_TYPE (VMK_HEAP_PHYS_CONTIGUOUS)

#endif 

/* Can only define one HEAP MEM attribute */
#ifdef VMKLINUX_MODULE_HEAP_ANY_MEM
#ifdef VMKLINUX_MODULE_HEAP_4GB_MEM
#error
#endif
#ifdef VMKLINUX_MODULE_HEAP_HIGH_MEM
#error
#endif

#define MEM_TYPE (VMK_HEAP_ANY_MEM)

#elif VMKLINUX_MODULE_HEAP_HIGH_MEM
#ifdef VMKLINUX_MODULE_HEAP_4GB_MEM
#error
#endif

#define MEM_TYPE (VMK_HEAP_HIGH_MEM)

#else // Nothing defined or 4GB_MEM

#define MEM_TYPE (VMK_HEAP_4GB_MEM)

#endif

vmk_HeapID VMK_MODULE_HEAP_ID = VMK_INVALID_HEAP_ID;

static int heap_initial = LINUX_MODULE_HEAP_INITIAL;
module_param(heap_initial, int, 0444);
MODULE_PARM_DESC(heap_initial, "Initial heap size allocated for the driver.");

static int heap_max = LINUX_MODULE_HEAP_MAX;
module_param(heap_max, int, 0444);
MODULE_PARM_DESC(heap_max, "Maximum attainable heap size for the driver.");

/*
 * The following two functions are used to create and destroy the private module
 * heap. The init function MUST be called before any other code in the module
 * has a chance to call kfree, kmalloc, etc. And the destroy function MUST be
 * called after those operations are all finished. 
 *
 */
int
vmklnx_module_heap_init(void)
{
   VMK_ReturnStatus status;

   if (heap_initial > heap_max) {
      heap_initial = heap_max;
      vmk_WarningMessage("module heap : Initial heap size > max. Limiting to max!!!\n");
   }

   vmk_LogMessage("module heap : Initial heap size : %d, max heap size: %d\n",
           heap_initial, heap_max);
   status = vmk_HeapCreate(VMK_MODULE_HEAP_NAME,
                           heap_initial,
                           heap_max,
                           PHYS_TYPE,
                           MEM_TYPE,
                           &VMK_MODULE_HEAP_ID);

   if (status != VMK_OK) {
      vmk_LogMessage("module heap %s: creation failed - %s\n", 
                     VMK_MODULE_HEAP_NAME, vmk_StatusToString(status));
      return -1;
   } else {
      vmk_LogMessage("module heap %s: creation succeeded. id = %p\n",
                     VMK_MODULE_HEAP_NAME, VMK_MODULE_HEAP_ID);
   }

   vmk_ModuleSetHeapID(vmklnx_this_module_id, VMK_MODULE_HEAP_ID);
   
   return 0;
}

int
vmklnx_module_heap_cleanup(void)
{
   vmk_HeapDestroy(VMK_MODULE_HEAP_ID); 

   VMK_MODULE_HEAP_ID = VMK_INVALID_HEAP_ID;

   return 0;
}
#else /* } VMKLINUX_MODULE_USE_EXTERNAL_HEAP { */
int
vmklnx_module_heap_init(void)
{
   char module_name[32];

   VMK_ASSERT(VMK_MODULE_HEAP_ID != VMK_INVALID_HEAP_ID);

   vmk_ModuleSetHeapID(vmklnx_this_module_id, VMK_MODULE_HEAP_ID);
   vmk_ModuleGetName(vmklnx_this_module_id, module_name, sizeof(module_name));
   vmk_LogMessage("%s uses external heap %s id = %d\n", 
                  module_name, VMK_MODULE_HEAP_NAME, VMK_MODULE_HEAP_ID);
   return 0;
}

int
vmklnx_module_heap_cleanup(void)
{
   return 0;
}
#endif /* } VMKLINUX_MODULE_USE_EXTERNAL_HEAP */


/******************************************************************************
 *                                                                            *
 *                     S K B   H E A P   S E C T I O N                        *
 *                                                                            *
 ******************************************************************************/
#ifdef LINUX_MODULE_SKB_HEAP /* { */
vmk_HeapID VMK_MODULE_SKB_HEAP_ID = VMK_INVALID_HEAP_ID;

static int skb_heap_initial = LINUX_MODULE_SKB_HEAP_INITIAL;
module_param(skb_heap_initial, int, 0444);
MODULE_PARM_DESC(skb_heap_initial, "Initial private socket buffer heap size allocated for the driver.");

static int skb_heap_max = LINUX_MODULE_SKB_HEAP_MAX;
module_param(skb_heap_max, int, 0444);
MODULE_PARM_DESC(skb_heap_max, "Maximum attainable private socket buffer heap size for the driver.");

/*
 * net_device support for per-module (pm) unified skb kmem_cache
 */
struct vmklnx_pm_skb_cache vmklnx_pm_skb_cache;

static int
vmklnx_module_skb_heap_init(void)
{
   VMK_ReturnStatus status;

   if (skb_heap_initial > skb_heap_max) {
      skb_heap_initial = skb_heap_max;
      vmk_WarningMessage("module skb heap : Initial heap size > max. Limiting to max!!!\n");
   }

   vmk_LogMessage("module skb heap : Initial heap size : %d, max heap size: %d\n",
                  skb_heap_initial, skb_heap_max);
   status = vmk_HeapCreate(VMK_MODULE_SKB_HEAP_NAME,
                           skb_heap_initial,
                           skb_heap_max,
                           VMK_HEAP_PHYS_ANY_CONTIGUITY,
                           VMK_HEAP_ANY_MEM,
                           &VMK_MODULE_SKB_HEAP_ID);

   if (status != VMK_OK) {
      vmk_LogMessage("module skb heap : creation failed\n");
      return -1;
   } else {
      vmk_LogMessage("module skb heap : creation succeeded\n");
   }
   
   return 0;
}

static int
vmklnx_module_skb_heap_cleanup(void)
{
   vmk_HeapDestroy(VMK_MODULE_SKB_HEAP_ID);   
   VMK_MODULE_SKB_HEAP_ID = VMK_INVALID_HEAP_ID;
   
   return 0;
}
#endif /* } LINUX_MODULE_SKB_HEAP */


/******************************************************************************
 *                                                                            *
 *               C O H E R E N T   D M A   S E C T I O N                      *
 *                     per-module DMA heap support                            *
 *                                                                            *
 ******************************************************************************/
vmk_HeapID VMK_MODULE_CODMA_HEAP_ID = VMK_INVALID_HEAP_ID;
struct vmklnx_codma vmklnx_codma;
static char *vmklnx_codma_name = VMK_MODULE_CODMA_HEAP_NAME;
static struct semaphore vmklnx_codma_mutex;


/******************************************************************************
 *                                                                            *
 *                           R C U   S E C T I O N                            *
 *                                                                            *
 ******************************************************************************/
struct vmklnx_rcu_data vmklnx_rcu_data;
struct tasklet_struct vmklnx_callback_tasklet;
struct timer_list vmklnx_rcu_timer;


/******************************************************************************
 *                                                                            *
 *                        M O D U L E   S E C T I O N                         *
 *                                                                            *
 ******************************************************************************/
vmk_ModuleID vmklnx_this_module_id;
struct module __this_module = {
   .moduleID = VMK_INVALID_MODULE_ID
};

int
vmk_early_init_module(void)
{
   int status;
   VMK_ReturnStatus vmk_status;

   /*
    * Verify that the module and VMkernel API revision are compatible
    */
   vmk_status = vmk_ModuleRegister(&vmklnx_this_module_id, VMKAPI_REVISION);
   if (vmk_status != VMK_OK) {
      vmk_WarningMessage("Registration failed (%#x): %s",
                         vmk_status, vmk_StatusToString(vmk_status));
      return vmk_status;
   }

   __this_module.moduleID = vmklnx_this_module_id;

   status = vmklnx_module_heap_init();

   if (status != 0) {
      return status;
   }

#ifdef LINUX_MODULE_SKB_HEAP
   status = vmklnx_module_skb_heap_init();

   if (status != 0) {
      vmklnx_module_heap_cleanup();
      return status;
   }
   
   spin_lock_init(&vmklnx_pm_skb_cache.lock);
#endif

   /*
    * Initialize the vmklinux coherent DMA structure using the
    * module heap (which gives us 32 bits of DMA).  If we need a
    * smaller mask later on, we can try to 
    */
   vmklnx_codma.heapID = VMK_MODULE_HEAP_ID;
   vmklnx_codma.mask = DMA_32BIT_MASK;
   vmklnx_codma.heapName = vmklnx_codma_name;
   vmklnx_codma.heapSize = LINUX_MODULE_HEAP_MAX;
   vmklnx_codma.mutex = &vmklnx_codma_mutex;
   sema_init(vmklnx_codma.mutex, 1);

   /*
    * Initialize RCU
    */
   vmklnx_rcu_init(&vmklnx_rcu_data, &vmklnx_callback_tasklet, &vmklnx_rcu_timer);

   return status;
}

int
vmk_late_cleanup_module(void)
{
   int status = 0;

   /*
    * Teardown RCU
    */
   vmklnx_rcu_cleanup(&vmklnx_rcu_data);

#ifdef LINUX_MODULE_SKB_HEAP
   status = vmklnx_module_skb_heap_cleanup();
#endif
   status = vmklnx_module_heap_cleanup() || status;
   if (vmklnx_codma.mask != DMA_32BIT_MASK) {
      vmk_HeapDestroy(vmklnx_codma.heapID);
   }

   return status;
}
