/* **********************************************************
 * Copyright 1998 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Heap                                                           */ /**
 * \defgroup Heap Heaps
 *
 * vmkernel has local heaps to help isolate VMKernel subsystems from
 * one another. Benefits include:
 * \li Makes it easier to track per-subssystem memory consumption,
 *     enforce a cap on  how much memory a given subsystem can allocate,
 *     locate the origin of memory leaks, ...
 * \li Confines most memory corruptions to the guilty subsystem.
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_HEAP_H_
#define _VMKAPI_HEAP_H_

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
#include "base/vmkapi_status.h"

#define VMK_INVALID_HEAP_ID NULL

/** /brief Max length of a heap name, including terminating nul. */
#define VMK_MAX_HEAP_NAME       32

/** /brief Max length of a unique heap name, including terminating nul. */
#define VMK_MAX_HEAP_UNIQUE_NAME       52

typedef struct Heap* vmk_HeapID;

/**
 * \ingroup Heap
 * \brief Type of memory the heap will allocate
 */ 
typedef enum {
   /** \brief Allocate memory without any address restrictions */
   VMK_HEAP_ANY_MEM,

   /**
    * \brief Allocate memory above 4GB.
    *
    * Generally used when memory will not be accessed by devices.
    */
   VMK_HEAP_HIGH_MEM,

   /**
     * \brief Allocate memory below 1GB.
     *
     * Should be used sparingly, only when absolutely necessary for
     * handling a device (never for general purpose use - even in a
     * device driver).
     */
   VMK_HEAP_1GB_MEM,

   /**
     * \brief Allocate memory below 2GB.
     *
     * Should be used sparingly, only when absolutely necessary for
     * handling a device (never for general purpose use - even in a
     * device driver).
     */
   VMK_HEAP_2GB_MEM,

   /**
     * \brief Allocate memory below 4GB.
     *
     * Typically when needed for device access.
     */
   VMK_HEAP_4GB_MEM,
} vmk_HeapMemType;


/**
 * \brief Physical contiguity of heap memory.
 */ 
typedef enum {
   VMK_HEAP_PHYS_CONTIGUOUS,
   VMK_HEAP_PHYS_ANY_CONTIGUITY,
   VMK_HEAP_PHYS_DISCONTIGUOUS,
} vmk_HeapPhysType;

/*
 ***********************************************************************
 * vmk_HeapCreate --                                              */ /**
 *
 * \ingroup Heap
 * \brief Create a heap that can grow dynamically up to the max size.
 *
 * \param[in]  name      Name associated with this heap.
 * \param[in]  initial   Initial size of the heap in bytes.
 * \param[in]  max       Maximum size of the heap in bytes.
 * \param[in]  physType  Physical contiguity allocated from this heap.
 * \param[in]  memType   Type of memory (low/high/any) allocated from
 *                       this heap.
 * \param[out] heapID    Newly created heap or VMK_INVALID_HEAP_ID
 *                       on failure.
 *
 * \retval VMK_NO_MEM The heap could not be allocated
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_HeapCreate(char *name, vmk_uint32 initial, vmk_uint32 max,
                                vmk_HeapPhysType physType, vmk_HeapMemType memType,
                                vmk_HeapID *heapID);

/*
 ***********************************************************************
 * vmk_HeapDestroy --                                             */ /**
 *
 * \ingroup Heap
 * \brief Destroy a dynamic heap
 *
 * \param[in] heap   Heap to destroy.
 *
 * \pre All memory allocated on the heap should be freed before the heap
 *      is destroyed.
 *
 ***********************************************************************
 */
void vmk_HeapDestroy(vmk_HeapID heap);

/*
 ***********************************************************************
 * vmk_HeapFree --                                                */ /**
 *
 * \ingroup Heap
 * \brief Free memory allocated with vmk_HeapAlloc.
 *
 * \param[in] heap   Heap that memory to be freed was allocated from.
 * \param[in] mem    Memory to be freed. Should not be NULL.
 *
 ***********************************************************************
 */
void vmk_HeapFree(vmk_HeapID heap, void *mem);

/*
 ***********************************************************************
 * vmk_HeapFreeByMachAddr --                                      */ /**
 *
 * \ingroup Heap
 * \brief Free memory allocated with vmk_HeapAlloc by machine address.
 *
 * \param[in] heap   Heap that memory to be freed was allocated from.
 * \param[in] maddr  Machine address of memory to be freed.
 *                   Should not be 0.
 *
 * \pre The heap given must be a low-memory contiguous heap.
 *
 ***********************************************************************
 */
void vmk_HeapFreeByMachAddr(vmk_HeapID heap, vmk_MachAddr maddr);


/*
 ***********************************************************************
 * vmk_HeapAllocWithRA --                                         */ /**
 *
 * \ingroup Heap
 * \brief Allocate memory and specify the caller's address.
 *
 * This is useful when allocating memory from a wrapper function.
 *
 * \param[in] heap   Heap that memory to be freed was allocated from.
 * \param[in] size   Number of bytes to allocate.
 * \param[in] ra     Address to return to.
 *
 * \retval NULL Cannot allocate 'size' bytes from specified heap.
 * \return Address of allocated memory of the specified size.
 *
 ***********************************************************************
 */
void *vmk_HeapAllocWithRA(vmk_HeapID heap, vmk_uint32 size, void *ra);

/*
 ***********************************************************************
 * vmk_HeapAlignWithRA --                                         */ /**
 *
 * \ingroup Heap
 * \brief Allocate aligned memory and specify the caller's address
 *
 * This is useful when allocating memory from a wrapper function.
 *
 * \param[in] heap   Heap that memory to be freed was allocated from.
 * \param[in] size   Number of bytes to allocate.
 * \param[in] align  Number of bytes the allocation should be aligned on.
 * \param[in] ra     Address to return to after allocation.
 *
 * \retval NULL Cannot allocate 'size' bytes from specified heap.
 * \return Address of allocated memory of the specified size.
 *
 ***********************************************************************
 */
void *vmk_HeapAlignWithRA(vmk_HeapID heap, vmk_uint32 size,
			  vmk_uint32 align, void *ra);

/*
 ***********************************************************************
 * vmk_HeapAlloc --                                               */ /**
 *
 * \ingroup Heap
 * \brief Allocate memory in the specified heap
 *
 * \param[in] heap   Heap that memory to be freed was allocated from.
 * \param[in] size   Number of bytes to allocate.
 *
 * \retval NULL Cannot allocate 'size' bytes from specified heap.
 * \return Address of allocated memory of the specified size.
 *
 ***********************************************************************
 */
#define vmk_HeapAlloc(heap, size) \
   (vmk_HeapAllocWithRA((heap), (size), __builtin_return_address(0)))

/*
 ***********************************************************************
 * vmk_HeapAlign --                                               */ /**
 *
 * \ingroup Heap
 * \brief Allocate aligned memory
 *
 * \param[in] heap       Heap that memory to be freed was allocated from.
 * \param[in] size       Number of bytes to allocate.
 * \param[in] alignment  Number of bytes the allocation should be
 *                       aligned on.
 *
 * \retval NULL Cannot allocate 'size' bytes from specified heap.
 * \return Address of allocated memory of the specified size.
 *
 ***********************************************************************
 */
#define vmk_HeapAlign(heap, size, alignment) \
   (vmk_HeapAlignWithRA((heap), (size), (alignment), __builtin_return_address(0)))

/*
 ***********************************************************************
 * vmk_HeapGetUniqueName --                                       */ /**
 *
 * \ingroup Heap
 * \brief Get unique heap name.
 *
 * Name will be qualified to be unique kernel wide.
 *
 * \param[in]  heap      Heap that caller is interogating.
 * \param[out] name      Pointer to name string of the given heap.
 * \param[in]  nameSize  Size of name array in bytes including space
 *                       for the terminating nul.
 *
 ***********************************************************************
 */
VMK_ReturnStatus
vmk_HeapGetUniqueName(vmk_HeapID heap, char *name, int nameSize);

/*
 ***********************************************************************
 * vmk_HeapMaximumSize --                                         */ /**
 *
 * \ingroup Heap
 * \brief Get the maximum size of the heap.
 *
 * \param[in]  heap      Heap that caller is interogating.
 * \param[out] size      Pointer to the size.
 *
 ***********************************************************************
 */
VMK_ReturnStatus
vmk_HeapMaximumSize(vmk_HeapID heap, vmk_size_t *size);

#endif /* _VMKAPI_HEAP_H_ */
/** @} */
