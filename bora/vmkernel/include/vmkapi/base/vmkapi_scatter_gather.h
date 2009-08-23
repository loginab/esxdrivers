/* **********************************************************
 * Copyright 2005 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * ScatterGather                                                  */ /**
 * \defgroup ScatterGather Scatter Gather Buffer Management
 *
 * Interfaces to manage discontiguous regions of machine memory that
 * form a virtually contiguous buffer.
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_SCATTER_GATHER_H_
#define _VMKAPI_SCATTER_GATHER_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMMEXT
#define INCLUDE_ALLOW_VMNIXMOD
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_status.h"
#include "base/vmkapi_heap.h"
#include "base/vmkapi_compiler.h"
#include "base/vmkapi_memory.h"

/*
 ***********************************************************************
 * VMK_SGARRAY_SIZE     --                                        */ /**
 *
 * \ingroup ScatterGather
 * \brief Compute the number of bytes necessary to contain a
 *        scatter-gather array that holds, at most, nbElems
 *        scatter-gather elements.
 *
 * \param[in]  nbElems  Number of elements in the scatter-gather array.
 *
 * \returns Number of bytes necessary to contain a scatter-gather array
 *          that holds, at most, the specified number of scatter-gather
 *          elements.
 *
 ***********************************************************************
*/
#define VMK_SGARRAY_SIZE(nbElems)  (sizeof(vmk_SgArray) + (nbElems) * sizeof(vmk_SgElem))

/**
 * \brief
 * Element for scatter-gather array that represents a machine-contiguous
 * region of memory.
 */
typedef struct vmk_SgElem {
   /**
    * \brief
    * Starting machine address of the machine-contiguous region this
    * element represents.
    */
   vmk_MachAddr		addr;
   /**
    * \brief
    * Length of the machine-contiguous region this element represents.
    */
   vmk_uint32		length;
} vmk_SgElem;


/**
 * \brief Scatter-gather array
 */
typedef struct  vmk_SgArray {
   /** \brief Maximum possible elements this scatter-gather array can hold */
   vmk_uint32		maxElems;
   
   /** \brief Number of elements in use */
   vmk_uint32		nbElems;
   
   /** \brief Elements of the array */
   vmk_SgElem		elem[0];
} vmk_SgArray;

/**
 * \brief Opaque handle for scatter-gather operations.
 */
typedef struct vmk_SgOpsHandleInt *vmk_SgOpsHandle;


/*
 ***********************************************************************
 * vmk_SgArrayOpAlloc--                                           */ /**
 *
 * \ingroup ScatterGather
 * \brief Callback to allocate and initialize a new scatter-gather
 *        array.
 *
 * The returned array should have it's maxElems field set correctly
 * and nbElems field set to zero.
 *
 * \param[in]  handle      Opaque scatter-gather ops handle.
 * \param[out] sg          Variable to return the newly allocated
 *                         buffer in.
 * \param[in]  numElems    Max elements the new array must support.
 * \param[in]  private     Private data from vmk_SgCreateOpsHandle().
 *
 * \retval VMK_OK          The allocation succeeded.
 * \retval VMK_NO_MEMORY   Not enough memory to allocate a new
 *                         scatter-gather element.
 ***********************************************************************
 */
typedef VMK_ReturnStatus (*vmk_SgArrayOpAlloc)(vmk_SgOpsHandle handle,
                                               vmk_SgArray **sg,
                                               vmk_uint32 numElems,
                                               void *private);

/*
 ***********************************************************************
 * vmk_SgArrayOpFree--                                            */ /**
 *
 * \ingroup ScatterGather
 * \brief Callback to free an existing scatter-gather array.
 *
 * \param[in] handle    Opaque scatter-gather ops handle.
 * \param[in] sg        scatter-gather array to free.
 * \param[in] private   Private data from vmk_SgCreateOpsHandle().
 *
 * \retval VMK_OK    The free succeeded.
 *
 ***********************************************************************
 */
typedef VMK_ReturnStatus (*vmk_SgArrayOpFree)(vmk_SgOpsHandle handle,
                                              vmk_SgArray *sg,
                                              void *private);

/*
 ***********************************************************************
 * vmk_SgArrayOpInit--                                            */ /**
 *
 * \ingroup ScatterGather
 * \brief Callback to initialize a scatter-gather array with the
 *        machine address ranges for a given virtual buffer.
 *
 * \param[in] handle             Opaque scatter-gather ops handle.
 * \param[in] sg                 scatter-gather array to update.
 * \param[in] vaddr              Virtual adress of the beginning of the
 *                               buffer to represent in the scatter-gather
 *                               array.
 * \param[in] size               Size in bytes of the buffer to represent
 *                               in the scatter-gather array.
 * \param[in] startingSGEntry    Entry in elem array to begin representing
 *                               the buffer.
 * \param[in] private            Private data from vmk_SgCreateOpsHandle().
 *
 * \retval VMK_OK          The initialization succeeded.
 * \retval VMK_BAD_PARAM   The buffer has too many machine address ranges
 *                         to be represented in the scatter-gather array
 *                         with the given parameters.
 *
 ***********************************************************************
 */
typedef VMK_ReturnStatus (*vmk_SgArrayOpInit)(vmk_SgOpsHandle handle,
                                              vmk_SgArray *sg,
                                              vmk_uint8 *vaddr,
                                              vmk_uint32 size,
                                              vmk_uint32 startingSGEntry,
                                              void *private);

/*
 ***********************************************************************
 * vmk_SgArrayOpResize--                                          */ /**
 *
 * \ingroup ScatterGather
 *
 * \brief Callback to recalculate an existing scatter-gather array when
 *        the virtual buffer it represents changes in size. 
 *
 * \param[in]  handle   Opaque scatter-gather ops handle.
 * \param[out] sg       Newly sized scatter-gather array.
 * \param[in]  vaddr    Virtual address of the buffer being resized.
 * \param[in]  currsize Original size in bytes of the buffer.
 * \param[in]  newsize  New size in bytes of the buffer.
 * \param[in]  private  Private data from vmk_SgCreateOpsHandle().
 *
 * \retval VMK_OK    The resize succeeded.
 *
 ***********************************************************************
 */
typedef VMK_ReturnStatus (*vmk_SgArrayOpResize)(vmk_SgOpsHandle handle,
                                                vmk_SgArray **sg,
                                                vmk_uint8 *vaddr,
                                                vmk_uint32 currsize,
                                                vmk_uint32 newsize,
                                                void *private);

/*
 ***********************************************************************
 * vmk_SgArrayOpCopy--                                            */ /**
 *
 * \ingroup ScatterGather
 * \brief Callback to copy a portion of the data represented by one
 *        scatter-gather array to another.
 *
 * \param[in]  handle            Opaque scatter-gather ops handle.
 * \param[in]  dstArray          scatter-gather array representing the
 *                               destination for the data to be copied.
 * \param[in]  initialDstOffset  Starting entry in dstArray to copy
 *                               the data to.
 * \param[in]  srcArray          scatter-gather array representing the
 *                               source for the data to be copied.
 * \param[in]  initialSrcOffset  Starting entry in the srcArray to copy
 *                               the data from.
 * \param[in]  bytesToCopy       Number of bytes to copy.
 * \param[out] totalBytesCopied  Number of bytes actually moved when
 *                               this callback completes.
 * \param[in]  private           Private data from vmk_SgCreateOpsHandle().
 *
 * \retval VMK_OK       The copy completed successfully
 * \retval VMK_FAILURE  The copy failed. Some bytes may have been copied.
 *
 ***********************************************************************
 */
typedef VMK_ReturnStatus (*vmk_SgArrayOpCopy)(vmk_SgOpsHandle handle,
                                              vmk_SgArray *dstArray,
                                              int initialDstOffset,
                                              vmk_SgArray *srcArray,
                                              int initialSrcOffset,
                                              int bytesToCopy,
                                              int *totalBytesCopied,
                                              void *private);

/**
 * \brief Scatter-gather array operations. 
 *
 *  Routines not implemented by the caller must be set to NULL.
 *
 *  Caller may override default behavior for any routine by supplying 
 *  the routines.
 */
typedef struct vmk_SgArrayOps {
   /** Handler used when allocating scatter-gather arrays */
   vmk_SgArrayOpAlloc alloc;
   
   /** Handler used when freeing scatter-gather arrays */
   vmk_SgArrayOpFree free;
   
   /** Handler used when initializing a scatter-gather array */
   vmk_SgArrayOpInit init;
   
   /** Handler used when initializing a scatter-gather array */
   vmk_SgArrayOpResize resize;
   
   /** Handler used when copying data between scatter-gather arrays */
   vmk_SgArrayOpCopy copy;
} vmk_SgArrayOps;

/*
 ***********************************************************************
 * vmk_SgComputeMaxEntries--                                      */ /**
 *
 * \brief Compute worst-case maximum number of scatter-gather entries
 *        necessary to accomodate a memory region starting at a given
 *        virtual address, of a given size.
 *
 * \param[in]  vaddr       Virtual address of the buffer
 * \param[in]  size        Length of the buffer in bytes
 * \param[out] numElems    Number of scatter-gather entries needed
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_SgComputeMaxEntries(vmk_uint8 *vaddr,
                                         vmk_uint32 size,
                                         vmk_uint32 *numElems);

/*
 ***********************************************************************
 * vmk_SgCreateOpsHandle--                                        */ /**
 *
 * \ingroup ScatterGather
 * \brief Create an opaque handle for scatter-gather operations.
 *
 * The handle is used by other routines to invoke callbacks and track
 * other state related to scatter-gather operations.
 *
 * \param[in]  heapId      HeapID to allocate memory on. 
 * \param[out] handle      Opaque scatter-gather ops handle.
 * \param[in]  ops         Scatter-gather ops to associate with
 *                         the opaque handle.
 * \param[in]  private     Context data private to the caller.
 *
 ***********************************************************************
*/
VMK_ReturnStatus vmk_SgCreateOpsHandle(vmk_HeapID heapId,
				       vmk_SgOpsHandle *handle,
                                       vmk_SgArrayOps *ops,
                                       void *private);
/*
 ***********************************************************************
 * vmk_SgDestroyOpsHandle--                                       */ /**
 *
 * \ingroup ScatterGather
 * \brief Destroy opaque handle for scatter-gather operations.
 *
 * \param[in] handle  Opaque scatter-gather ops handle to be destroyed.
 *
 ***********************************************************************
*/
VMK_ReturnStatus vmk_SgDestroyOpsHandle(vmk_SgOpsHandle handle);

/*
 ***********************************************************************
 * vmk_SgCreateSimpleOpsHandle--                                  */ /**
 *
 * \ingroup ScatterGather
 * \brief Create an opaque handle for scatter-gather operations that
 *        uses only default callbacks.
 *
 * May be destroyed using vmk_SgDestroyOpsHandle.
 *
 * \param[in]      heapID     Heap to allocate handle on. 
 * \param[in,out]  handle     Opaque scatter-gather ops handle.
 *
 ***********************************************************************
*/
VMK_ReturnStatus vmk_SgCreateSimpleOpsHandle(vmk_HeapID heapID,
					     vmk_SgOpsHandle *handle);


/*
 ***********************************************************************
 * vmk_SgProtectHandle --                                         */ /**
 *
 * \ingroup ScatterGather
 * \brief Set protection on a handle to prevent unwanted/accidental
 *        deletion.
 *
 * \param[in] handle    Opaque scatter-gather ops handle to be protected.
 *
 ***********************************************************************
*/
VMK_ReturnStatus vmk_SgProtectHandle(vmk_SgOpsHandle handle);


/*
 ***********************************************************************
 * vmk_SgUnprotectHandle --                                       */ /**
 *
 * \ingroup ScatterGather
 * \brief Clear protection on the handle so it can be deleted.
 *
 * \param[in] handle     Opaque scatter-gather ops handle to clear
 *                       protection on.
 *
 ***********************************************************************
*/
VMK_ReturnStatus vmk_SgUnprotectHandle(vmk_SgOpsHandle handle);


/*
 ***********************************************************************
 * vmk_SgDestroySimpleOpsHandle--                                 */ /**
 *
 * \ingroup ScatterGather
 * \brief Destroy Simple SG Ops handle passed into the SG routines. 
 *
 * Memory will be freed internally holding the structures pointed
 * to by handle, allocated in in vmk_SgCreateOpsHandle.
 *
 * \param[in] handle  Opaque scatter-gather ops handle to be destroyed.
 *
 ***********************************************************************
*/
VMK_ReturnStatus vmk_SgDestroySimpleOpsHandle(vmk_SgOpsHandle handle);

/*
 ***********************************************************************
 * vmk_SgAlloc--                                                  */ /**
 *
 * \ingroup ScatterGather
 * \brief Allocate a scatter-gather array with enough entries to
 *        represent any  buffer in virtual address space of a given size.
 *
 * Allocate a SG array that's large enough to hold the machine
 * address SGEs describing a range of virtual addresses.
 *
 * \param[in] handle  Opaque scatter-gather ops handle.
 * \param[in] sg      New scatter-gather array.
 * \param[in] size    Used to compute number of SG Array elements.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_SgAlloc(vmk_SgOpsHandle handle,
                             vmk_SgArray **sg,
                             vmk_uint32 size);

/*
 ***********************************************************************
 * vmk_SgAllocWithInit--                                          */ /**
 *
 * \ingroup ScatterGather
 * \brief Allocate a scatter-gather array with enough entries to
 *        represent a given buffer in virtual address space.
 *
 * \param[in] handle  Opaque scatter-gather ops handle.
 * \param[in] sg      Scatter-gather array to initialize.
 * \param[in] vaddr   Virtual address of buffer.
 * \param[in] size    Size in bytes of the buffer.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_SgAllocWithInit(vmk_SgOpsHandle handle,
                                     vmk_SgArray **sg,
                                     vmk_uint8 *vaddr,
                                     vmk_uint32 size);

/*
 ***********************************************************************
 * vmk_SgInit--                                                   */ /**
 *
 * \ingroup ScatterGather
 * \brief Initialize a given scatter-gather array with the machine
 *        addresses representing a buffer in virtual address space.
 *
 * The scatter-gather array must have enough scatter-gather entries
 * to describe the machine address ranges backing the given buffer.
 *
 * \param[in] handle      Opaque scatter-gather ops handle.
 * \param[in] sg          Scatter-gather array to initialize.
 * \param[in] vaddr       Virtual address of buffer.
 * \param[in] size        Size in bytes of the buffer.
 * \param[in] initSGEntry Starting entry index for initialization.
 *
 * \sa vmk_SgComputeMaxEntries
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_SgInit(vmk_SgOpsHandle handle,
                            vmk_SgArray *sg,
                            vmk_uint8 *vaddr,
                            vmk_uint32 size,
                            vmk_uint32 initSGEntry);

/*
 ***********************************************************************
 * vmk_SgFree --                                                  */ /**
 *
 * \ingroup ScatterGather
 * \brief Free a scatter-gather array.
 *
 * \param[in] handle  Opaque scatter-gather ops handle.
 * \param[in] sgArray Pointer returned by vmk_AllocSgArray()
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_SgFree(vmk_SgOpsHandle handle,
                            vmk_SgArray *sgArray);
                                                                                                                     
/*
 ***********************************************************************
 * vmk_SgCopy --                                                  */ /**
 *
 * \ingroup ScatterGather
 * \brief Copy data represented by one scatter-gather array to another
 *
 * \param[in] handle             Opaque scatter-gather ops handle.
 * \param[in] dstArray           Destination scatter-gather array.
 * \param[in] initialDstOffset   Offset into dstArray to start copying to.
 * \param[in] srcArray           Scatter-gather to copy from.
 * \param[in] initialSrcOffset   Offset into srcArray to start copying from.
 * \param[in] bytesToCopy        Maximum length of data to copy.
 * \param[out] totalBytesCopied  Number of bytes actually copied.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_SgCopy(vmk_SgOpsHandle handle,
                            vmk_SgArray *dstArray, int initialDstOffset,
                            vmk_SgArray *srcArray, int initialSrcOffset,
                            int bytesToCopy, int * totalBytesCopied);

/*
 ***********************************************************************
 * vmk_GetSgDataLen --                                            */ /**
 *
 * \ingroup ScatterGather
 * \brief Compute the size of a scatter-gather list's payload.
 *
 ***********************************************************************
 */
vmk_uint32 vmk_GetSgDataLen(vmk_SgArray *sgArray);

/*
 ***********************************************************************
 * vmk_CopyToSg--                                                 */ /**
 *
 * \ingroup ScatterGather
 * \brief Copy data from a buffer to the machine addresses
 *        defined in a scatter-gather array.
 *
 * \retval VMK_OK The copy completed successfully
 * \retval VMK_FAILURE The copy failed. Some bytes may have been copied.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_CopyToSg(vmk_SgArray *sgArray,
                              void *dataBuffer,
                              unsigned int dataLen);
/*
 ***********************************************************************
 * vmk_CopyFromSg--                                               */ /**
 *
 * \ingroup ScatterGather
 * \brief Copy data from the machine addresses of a scatter-gather
 *        array to a buffer.
 *
 * \retval VMK_OK The copy completed successfully
 * \retval VMK_FAILURE The copy failed. Some bytes may have been copied.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_CopyFromSg(void *dataBuffer,
                                vmk_SgArray *sgArray,
                                unsigned int dataLen);
#endif /* _VMKAPI_SCATTER_GATHER_H_ */
/** @} */
