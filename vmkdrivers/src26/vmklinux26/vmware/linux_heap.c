/* ****************************************************************
 * Portions Copyright 2004 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

/*
 * linux_heap.c --
 *
 *	This file enables a module to have its own private heap, replete with
 *	posion-value checks, etc. to guarantee that modules and other kernel
 *	code don't step on each other's toes. These functions assumes at the
 *      times that they are being called that the module private is already 
 *      been created. 
 */

#include <asm/page.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/proto.h>

#include "vmkapi.h"
#include "linux_stubs.h"
#include "vmklinux26_log.h"


void
vmklnx_kfree(vmk_HeapID heapID, const void *p)
{
   if (p != NULL) {
      vmk_HeapFree(heapID, (void *)p);
   }
}

void *
vmklnx_kmalloc(vmk_HeapID heapID, size_t size, void *ra)
{
   size_t actual;
   void *d;
   void *callerPC = ra;

   if (unlikely(size == 0)) {
      VMKLNX_WARN("vmklnx_kmalloc: size == 0\n");
      return NULL;
   }

   if (likely(ra == NULL)) {
      callerPC = __builtin_return_address(0);
   }

   /*
    * Allocate blocks in powers-of-2 (like Linux), so we tolerate any
    * driver bugs that don't show up because of the large allocation size.
    */
   actual = 1 << (fls(size) - 1);

   if (actual < size) {
      /*
       * The assert below guards against shitting the bit 
       * off.  In any case, we should not have such large
       * size request.
       */
      VMK_ASSERT(actual != ~((((size_t) -1) << 1) >> 1));
      actual <<=1;
   }

   d = vmk_HeapAllocWithRA(heapID, actual, callerPC);

   return d;
}

void *
vmklnx_kzmalloc(vmk_HeapID heapID, size_t size)
{
   void *p;

   p = vmklnx_kmalloc(heapID, size, __builtin_return_address(0));

   if (likely(p != NULL))
      memset(p, 0, size);

   return p;
}

void*
vmklnx_kmalloc_align(vmk_HeapID heapID, size_t size, size_t align)
{
   size_t actual;
   void *d;

   if (unlikely(size == 0)) {
      VMKLNX_WARN("vmklnx_kmalloc_align: size == 0\n");
      return NULL;
   }

   /*
    * Allocate blocks in powers-of-2 (like Linux), so we tolerate any
    * driver bugs that don't show up because of the large allocation size.
    */
   actual = 1 << (fls(size) - 1);

   if (actual < size) {
      /*
       * The assert below guards against shitting the bit 
       * off.  In any case, we should not have such large
       * size request.
       */
      VMK_ASSERT(actual != ~((((size_t) -1) << 1) >> 1));
      actual <<=1;
   }

   d = vmk_HeapAlignWithRA(heapID, actual, align,
                           __builtin_return_address(0));
   return d;
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_kstrdup
 *
 *      Allocate space for and copy an existing string
 *
 * Results:
 *      A pointer to the allocated area where the string has been copied
 *
 * Side effects:
 *      A memory is allocated from the module heap
 *
 *----------------------------------------------------------------------
 */
char *vmklnx_kstrdup(vmk_HeapID heapID, const char *s, void *ra)
{
   size_t len;
   char *buf;
   void *callerPC = ra;

   if (!s) {
      return NULL;
   }
   if (likely(ra == NULL)) {
      callerPC = __builtin_return_address(0);
   }
   len = strlen(s) + 1;
   buf = vmklnx_kmalloc(heapID, len, callerPC);
   if (buf) {
      memcpy(buf, s, len);
   }
   return buf;
}

/* kmem_cache implementation based upon vmkapi Slabs */

#define KMEM_CACHE_MAGIC        0xfa4b9c23

struct kmem_cache_s {
#ifdef VMX86_DEBUG
   vmk_uint32           magic;
#endif
   vmk_SlabID           slabID;
   vmk_HeapID           heapID;
   char                 slabName[VMK_MAX_SLAB_NAME];
   void (*ctor)(void *, struct kmem_cache_s *, unsigned long);
   void (*dtor)(void *, struct kmem_cache_s *, unsigned long);
   vmk_ModuleID         moduleID;
};


/*
 *----------------------------------------------------------------------
 *
 * VmklnxKmemCacheConstructor
 *
 *      kmem_alloc_cache constructor
 *
 * Results:
 *      VMK_OK
 *
 * Side effects:
 *      Calls the client's "ctor" function.
 *
 *----------------------------------------------------------------------
 */


static VMK_ReturnStatus
VmklnxKmemCacheConstructor(void *item, vmk_uint32 size,
                     vmk_AddrCookie cookie, int flags)
{
   struct kmem_cache_s *cache = cookie.ptr;

   VMKAPI_MODULE_CALL_VOID(cache->moduleID, cache->ctor, item, cache, 0);

   return VMK_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * VmklnxKmemCacheDestructor
 *
 *      kmem_alloc_cache destructor.
 *
 * Results:
 *      none
 *
 * Side effects:
 *      Calls the client's "dtor" function.
 *
 *----------------------------------------------------------------------
 */

static void
VmklnxKmemCacheDestructor(void *item, vmk_uint32 size, vmk_AddrCookie cookie)
{
   struct kmem_cache_s *cache = cookie.ptr;

   VMKAPI_MODULE_CALL_VOID(cache->moduleID, cache->dtor, item, cache, 0);
}

/*
 * vmklnx_kmem_cache_create_with_props
 *
 * This custom version assumes the caller sets up the slab properties,
 * other than ctor/dtor callbacks and it's data argument.
 *
 */
struct kmem_cache_s *
vmklnx_kmem_cache_create_with_props(vmk_HeapID heapID, const char *name ,
                  void (*ctor)(void *, struct kmem_cache_s *, unsigned long),
                  void (*dtor)(void *, struct kmem_cache_s *, unsigned long),
                  vmk_SlabProps *props)
{
   struct kmem_cache_s *cache;
   VMK_ReturnStatus status;
   vmk_size_t heapMaxSize;

   status = vmk_HeapMaximumSize(heapID, &heapMaxSize);
   if (status != VMK_OK) {
      VMKLNX_WARN("%s: couldn't get module heap size\n", __FUNCTION__);
      return NULL;
   }
   // make sure we are talking to correct heap
   VMK_ASSERT(props->typeSpecific.nominal.heap == heapID);
   VMK_ASSERT(props->typeSpecific.nominal.maxTotalObj * props->objSize < heapMaxSize);

   cache = vmk_HeapAlloc(heapID, sizeof(*cache));
   if (cache == NULL) {
      VMKLNX_WARN("%s: out of memory\n", __FUNCTION__);
      return NULL;
   }

   cache->heapID = heapID;
   cache->ctor = ctor;
   cache->dtor = dtor;
#ifdef VMX86_DEBUG
   cache->magic = KMEM_CACHE_MAGIC;
#endif
   cache->moduleID = vmk_ModuleStackTop();

   // set up rest of the slab members
   props->constructor = (ctor != NULL) ? VmklnxKmemCacheConstructor : NULL;
   props->destructor = (dtor != NULL) ? VmklnxKmemCacheDestructor : NULL;
   props->constructorArg.ptr = cache;

   // Remember the slab name.
   vmk_Snprintf(cache->slabName, VMK_MAX_SLAB_NAME, "%s", name);

   status = vmk_SlabCreate(cache->slabName, props, &cache->slabID);
   if (status != VMK_OK) {
      VMKLNX_WARN("%s: Slab creation failed for %s.\n", __FUNCTION__, cache->slabName);
      vmk_HeapFree(heapID, cache);
      return NULL;
   }

   return cache;
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_kmem_cache_create
 *
 *      Create a memory cache (Slab) that will hold elements of a given size.
 *
 *      This function sizes the aggregate of the per-PCPU caches at 5% of
 *      the per-module heap.  The actual size of the PCPU caches could be
 *      smaller, at the discretion of vmk_SlabCreate().
 *
 * Arguments
 *      ctor is a constructor that will initialize a cache element
 *           when allocated from the underlying module heap.
 *      dtor is a destructor that would normally be used on an
 *           element before it is returned to the underlying heap.
 *      trimFlag
 *           When set to 1 indicates that heap trimming should be done.
 *      slabPercent
 *           Cap on percentage of the heap that can be consumed by the slab.
 *
 * Results:
 *      A pointer to the cache to be used on subsequent calls to
 *           kmem_cache_{alloc, free, destroy} or NULL on error
 *
 * Side effects:
 *      A kmem cache heap object is allocated.
 *
 *----------------------------------------------------------------------
 */

struct kmem_cache_s *
vmklnx_kmem_cache_create(vmk_HeapID heapID, const char *name , 
		  size_t size, size_t offset,
		  void (*ctor)(void *, struct kmem_cache_s *, unsigned long),
		  void (*dtor)(void *, struct kmem_cache_s *, unsigned long),
                  int trimFlag, int slabPercent)
{
   vmk_SlabProps props;
   VMK_ReturnStatus status;
   vmk_size_t heapMaxSize;
   vmk_small_size_t objectSize, ctrlSize;

   if (offset != 0) {
      VMKLNX_WARN("kmem_cache_create: offset = %lu\n", offset);
      return NULL;
   }

   status = vmk_HeapMaximumSize(heapID, &heapMaxSize);
   if (status != VMK_OK) {
      VMKLNX_WARN("kmem_cache_create: couldn't get module heap size");
      return NULL;
   }

   ctrlSize = vmk_SlabControlSize();
   size = round_up(size, sizeof(void *));
   objectSize = size + ctrlSize;
   objectSize = round_up(objectSize, SMP_CACHE_BYTES);

   props.type = VMK_SLAB_TYPE_NOMINAL;
   props.typeSpecific.nominal.heap = heapID;
   props.objSize = size;
   props.alignment = SMP_CACHE_BYTES;
   props.typeSpecific.nominal.slabPercent = slabPercent;

   // heuristic: limiting slab to 95% of heap
   props.typeSpecific.nominal.maxTotalObj = ((heapMaxSize / objectSize) * 95) / 100;
   VMK_ASSERT(props.typeSpecific.nominal.maxTotalObj * objectSize < heapMaxSize);

   // heuristic: limiting the aggregate of all PCPU caches to 5% of heap max
   props.typeSpecific.nominal.maxFreeObj = props.typeSpecific.nominal.maxTotalObj / 20;

   // To keep alignment, place the Slab's control structure after the data.
   props.typeSpecific.nominal.ctrlOffset = size;

   // don't free back to the heap for compatibilty with linux
   props.typeSpecific.nominal.notrim = trimFlag ? 0 : 1;

   return vmklnx_kmem_cache_create_with_props(heapID, name, ctor, dtor, &props);
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_kmem_cache_destroy
 *
 *      Deallocate all elements in a kmem cache and destroy the
 *      kmem_cache object itself
 *
 * Results:
 *      Always 0
 *
 * Side effects:
 *      All cached objects and the kmem_cache object itself are freed
 *
 *----------------------------------------------------------------------
 */

int 
vmklnx_kmem_cache_destroy(struct kmem_cache_s *cache)
{
   VMK_ASSERT(cache != NULL);
   VMK_ASSERT(cache->magic == KMEM_CACHE_MAGIC);

   vmk_SlabDestroy(cache->slabID);
#ifdef VMX86_DEBUG
   cache->magic = 0;
#endif
   vmk_HeapFree(cache->heapID, cache);
   return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_kmem_cache_alloc
 *
 *      Allocate an element from the cache pool. If no free elements
 *      exist a new one will be allocated from the heap.
 *
 * Results:
 *      A pointer to the allocated object or NULL on error
 *
 * Side effects:
 *      Cache list may change or a new object is allocated from the heap
 *
 *----------------------------------------------------------------------
 */

void *
vmklnx_kmem_cache_alloc(struct kmem_cache_s *cache)
{
   VMK_ASSERT(cache != NULL);
   VMK_ASSERT(cache->magic == KMEM_CACHE_MAGIC);

   return vmk_SlabAlloc(cache->slabID);
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_kmem_cache_free
 *
 *      Release an element to the object cache. The memory will not be
 *      freed until the cache is destroyed
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Cache list will change
 *
 *----------------------------------------------------------------------
 */

void 
vmklnx_kmem_cache_free(struct kmem_cache_s *cache, void *item)
{
   VMK_ASSERT(cache != NULL);
   VMK_ASSERT(cache->magic == KMEM_CACHE_MAGIC);

   vmk_SlabFree(cache->slabID, item);
}
