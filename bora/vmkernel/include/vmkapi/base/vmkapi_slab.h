/* **********************************************************
 * Copyright 1998 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Slabs                                                          */ /**
 * \defgroup Slab Slab Allocation
 *
 * ESX Server supports slab allocation for high performance driver/stack
 * implementations:
 * - Reduces memory fragmentation, especially for smaller data structures
 *   allocated in high volume.
 * - Reduces CPU consumption for data structure initialization/teardown.
 * - Improves CPU hardware cache performance.
 * - Provides finer grained control of memory consumption.
 * - Builds structure upon the VMKAPI heap layer.
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_SLAB_H_
#define _VMKAPI_SLAB_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMNIXMOD
#define INCLUDE_ALLOW_VMMEXT
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_MODULE
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_const.h"
#include "base/vmkapi_heap.h"
#include "base/vmkapi_status.h"


/**
 * \ingroup Slab
 * \brief Opaque handle for a slab cache
 */
typedef struct vmk_SlabIDInt *vmk_SlabID;

#define VMK_INVALID_SLAB_ID ((vmk_SlabID)NULL)

/* Max length of a slab name, including the trailing NUL character */
#define VMK_MAX_SLAB_NAME       32


/*
 ***********************************************************************
 * vmk_SlabItemConstructor --                                     */ /**
 *
 * \ingroup Slab
 * \brief Item constructor - optional user defined function.  Runs for
 *                           each object when a cluster of memory is
 *                           allocated from the heap.
 *
 * \note  When the control structure is placed inside the free object,
 *        then the constructor must take care not to modify the control
 *        structure.
 *
 * \param[in] object     Object to be constructed.
 * \param[in] size       Size of buffer (possibly greater than objSize).
 * \param[in] arg        constructorArg (see vmk_SlabProps).
 * \param[in] flags      Currently unused (reserved for future use).
 *
 * \retval VMK_OK to indicate object construction has succeded.
 * \return Other To indicate failure.
 *
 ***********************************************************************
 */
typedef VMK_ReturnStatus (*vmk_SlabItemConstructor)(void *object,
                                                    vmk_small_size_t size,
                                                    vmk_AddrCookie arg,
                                                    int flags);


/*
 ***********************************************************************
 *  vmk_SlabItemDestructor --                                     */ /**
 *
 * \ingroup Slab
 * \brief Item destructor - optional user defined function.  Runs for
 *                          each buffer just before a cluster of memory
 *                          is returned to the heap.
 *
 * \note  When the control structure is placed inside the free object,
 *        then the destructor must take care not to modify the control
 *        structure.
 *
 * \param[in] object     Object to be destroyed.
 * \param[in] size       Size of buffer (possibly greater than objSize).
 * \param[in] arg        constructorArg (see vmk_SlabProps).
 *
 ***********************************************************************
 */
typedef void (*vmk_SlabItemDestructor)(void *object, vmk_small_size_t size,
                                       vmk_AddrCookie arg);

/**
 * \ingroup Slab
 * \brief Types of slab
 */
typedef enum {
   VMK_SLAB_TYPE_SIMPLE=0,
   VMK_SLAB_TYPE_NOMINAL=1
} vmk_SlabType;

/**
 * \ingroup Slab
 * \brief Properties of a slab allocator
 */
typedef struct {
   /** \brief Type of slab */
   vmk_SlabType type;

   /** \brief Byte Size of each object */
   vmk_small_size_t objSize;

   /** \brief Byte alignment for each object */
   vmk_small_size_t alignment;

   /** \brief Called after an object is allocated (or NULL for no action) */
   vmk_SlabItemConstructor constructor;
   
   /** \brief Called before an object is freed (or NULL for no action) */
   vmk_SlabItemDestructor destructor;

   /** \brief Arument for constructor/destructor calls */
   vmk_AddrCookie constructorArg;

   /** \brief Type-specific slab properties */
   union {
      /** \brief Properties for VMK_SLAB_TYPE_SIMPLE, */
      struct {
         /** \brief Backing store for the cache clusters. */
         vmk_HeapID heap;
         
         /** \brief  Max total objects. */         
         vmk_uint32 maxTotalObj;
         
         /** \brief  Max free objects (across all PCPUs). */
         vmk_uint32 maxFreeObj;
      } simple;

      /*
       * \brief Properties for VMK_SLAB_TYPE_NOMINAL
       *
       * Use VMK_SLAB_TYPE_NOMINAL for finer control of slab
       * properties.  
       */
      struct {
         /** \brief Backing store for the cache clusters */
         vmk_HeapID heap;
         
         /** \brief Max total objects */
         vmk_uint32 maxTotalObj;
         
         /** \brief Max free objects in all PCPU caches */    
         vmk_uint32 maxFreeObj;
         
         /** \brief Cap on percentage of the heap consumed by the slab */
         vmk_uint32 slabPercent;
         
         /** \brief See vmk_SlabControlSize */
         vmk_small_size_t ctrlOffset;
         
         /** 
          * \brief Don't free to the backing heap until
          *        vmk_SlabDestroy is called.
          */
         unsigned notrim:1;
      } nominal;
   } typeSpecific;
} vmk_SlabProps;


/*
 ***********************************************************************
 *  vmk_SlabCreate --                                             */ /**
 *
 * \ingroup Slab
 * \brief Create a slab allocator cache.
 *
 * \param [in]  name       Name associated with the new cache.
 * \param [in]  props      Properties of the new cache.
 * \param [out] cache      For use with vmk_SlabAlloc, etc.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_SlabCreate(const char *name,
                                vmk_SlabProps *props,
                                vmk_SlabID *cache);


/*
 ***********************************************************************
 *  vmk_SlabAlloc --
 *
 * \ingroup Slab
 * \brief Allocate an item from a slab.
 *
 *        The vmk_SlabItemConstructor (if defined) was previously called,
 *        or the object was previously freed via vmk_SlabFree().
 *
 * \note  When the control structure is placed inside the free object,
 *        the caller of vmk_SlabAlloc() must assume this portion of
 *        the object is uninitialized.
 *
 * \param[in] cache      Slab from which allocation will take place.
 *
 * \retval NULL   Memory could not be allocated.
 *
 ***********************************************************************
 */
void *vmk_SlabAlloc(vmk_SlabID cache);


/*
 ***********************************************************************
 *  vmk_SlabFree --
 *
 * \ingroup Slab
 * \brief Free memory allocated by vmk_SlabAlloc.
 *
 *        The memory object will be retained by the slab.  If at
 *        some point the slab chooses to give the memory back to the
 *        system, the vmk_SlabItemDestructor (if defined) will be called.
 *
 * \param[in] cache      Slab from which the item was allocated.
 * \param[in] object     object to be freed.
 *
 ***********************************************************************
 */
void vmk_SlabFree(
   vmk_SlabID cache,
   void *item);


/*
 ***********************************************************************
 *  vmk_SlabDestroy --
 *
 * \ingroup Slab
 * \brief Tear down a slab cache allocated previously created by
 *        vmk_SlabCreate.
 *
 * \param[in] cache      The cache to be destroyed.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_SlabDestroy(vmk_SlabID cache);


/*
 ***********************************************************************
 *  vmk_SlabControlSize --
 *
 * \ingroup Slab
 * \brief  Get the size of the per-object "control" structure.
 *
 *         The slab maintains a control structure for each free object
 *         cached by the slab.  When VMK_SLAB_TYPE_SIMPLE properties are
 *         used to create the slab, the control structure will be tacked
 *         past the end of the client's object.  To save space, the control
 *         structure can be placed within the user's free object using the
 *         ctrlOffset paramter to VMK_SLAB_TYPE_NOMINAL properties.
 *
 * \note   See vmk_SlabItemConstructor, vmk_SlabItemDestructor and
 *         and vmk_SlabAlloc for the constraints that must be obeyed
 *         when the control structure is placed inside the object.
 *
 * \return Size of the control structure in bytes.
 *
 ***********************************************************************
 */
vmk_small_size_t vmk_SlabControlSize(void);

#endif /* _VMKAPI_SLAB_H_ */
/** @} */
