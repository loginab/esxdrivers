/* **********************************************************
 * Copyright 2007 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * MemPool                                                        */ /**
 * \defgroup MemPool Managed Machine-Page Pools
 *
 * Memory pools are used to manage machine memory resources for admission 
 * control and for better resource tracking. Each MemPool entity represents
 * a set of limits that the internal resource management algorithms honor.
 * The functions here provide operations to add such pools starting at
 * the root represented by kmanaged group and and also introduces APIs 
 * to allocate/free memory based on the restrictions of the mempool.
 * 
 * @{
 ***********************************************************************
 */
 
#ifndef _VMKAPI_MEMPOOL_H_
#define _VMKAPI_MEMPOOL_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_status.h"
#include "base/vmkapi_memory.h"

/** \brief Allocate memory anywhere on the machine. */
#define VMK_MEMPOOL_MAXPAGE_ANY  0

/** \brief Allocate memory only in the low 4 gigabytes. */
#define VMK_MEMPOOL_MAXPAGE_LOW  0x100000

/**
 * \brief Maximum number of pages that can be requested for a
 *        reservation or limit.
 */
#define VMK_MEMPOOL_MAX_NUM_PAGES  0xffffffff

/**
 * \ingroup MemPool
 * \brief Properties of a memory pool
 */
typedef struct vmk_MemPoolProps {
   /**
    * \brief Specifies the min num of guaranteed pages reserved for the pool.
    */
   vmk_uint32     reservation;
   
   /** \brief Specifies the max num of pages the pool can offer. */
   vmk_uint32     limit;
} vmk_MemPoolProps;

/**
 * \ingroup MemPool
 * \brief Properties of a memory pool allocation
 */
typedef struct vmk_MemPoolAllocProps {
   /** \brief Number of pages that the allocation  will be aligned on. */
   vmk_uint32      alignment;

   /** \brief Allocate pages with a page number less than or equal to this. */
   vmk_MachPage   maxPage;
} vmk_MemPoolAllocProps;

typedef struct vmk_MemPoolInt* vmk_MemPool;

/*
 ***********************************************************************
 * vmk_MemPoolCreate --                                           */ /**
 *
 * \ingroup MemPool
 * \brief Create a machine memory pool.
 *
 * \param[in]  name     Name associated with the new memory pool.
 * \param[in]  props    Properties of the new memory pool.
 * \param[out] pool     Handle to the newly created memory pool.
 *
 * \retval VMK_BAD_PARAM          The pool or properties arguments were NULL.
 * \retval VMK_MEM_MIN_GT_MAX     Reservation was larger than the limit
 *                                in the pool properties. The reservation
 *                                should always be less or equal to
 *                                the limit.
 * \retval VMK_MEM_ADMIT_FAILED   There aren't enough resources in the
 *                                pool's group to admit a pool of the
 *                                specified size. 
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_MemPoolCreate(
   const char *name,
   const vmk_MemPoolProps *props,
   vmk_MemPool *pool);


/*
 ***********************************************************************
 * vmk_MemPoolSetProps --                                         */ /**
 *
 * \ingroup MemPool
 * \brief Change the properties of an existing memory pool
 *
 * \param[in,out] pool   Memory pool to change
 * \param[in]     props  New properties for the memory pool
 *
 * \retval VMK_BAD_PARAM          The pool or properties arguments were NULL
 *                                or the pool was invalid.
 * \retval VMK_MEM_MIN_GT_MAX     Reservation was larger than the limit
 *                                in the pool properties. The reservation
 *                                should always be less or equal to
 *                                the limit.
 * \retval VMK_MEM_ADMIT_FAILED   There aren't enough resources in the
 *                                pool's group to admit a pool of the
 *                                specified size. 
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_MemPoolSetProps(
   vmk_MemPool pool,
   const vmk_MemPoolProps *props);

/*
 ***********************************************************************
 * vmk_MemPoolGetProps --                                         */ /**
 *
 * \ingroup MemPool
 * \brief   Get the properties of an existing memory pool
 *
 * \param[in]  pool     Memory pool to query
 * \param[out] props    Properties associated with the
 *                      memory pool
 *
 * \retval VMK_BAD_PARAM  The pool argument was invalid.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_MemPoolGetProps(
   vmk_MemPool pool,
   vmk_MemPoolProps *props);

/*
 ***********************************************************************
 * vmk_MemPoolDestroy --                                          */ /**
 *
 * \ingroup MemPool
 * \brief Destroy a memory pool.
 *
 * \param[in]  pool   Memory pool to destroy
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_MemPoolDestroy(
   vmk_MemPool pool);

/*
 ***********************************************************************
 * vmk_MemPoolAlloc --                                            */ /**
 *
 * \ingroup MemPool
 * \brief Allocate a contiguous range of machine pages from a specified
 *        memory pool.
 *
 * \param[in] pool            Memory pool to allocate from.
 * \param[in] props           Attributes for this allocation or NULL
 *                            for default attributes.
 * \param[in] numPages        Number of pages to allocate.
 * \param[in] wait            VMK_TRUE - Block and wait for the
 *                            requested memory to become available.\n
 *                            VMK_FALSE - If memory is not immediately
 *                            available then return with an error.
 * \param[out] startPage      The first page in the allocated page range.
 *
 * \retval VMK_BAD_PARAM         The pool argument was invalid or number
 *                               of pages requested was less than one.
 * \retval VMK_NOT_SUPPORTED     The maxPage property was set too low.
 * \retval VMK_MEM_ADMIT_FAILED  The requested allocation was rejected
 *                               because the pool or the pools group
 *                               did not have the resources to
 *                               fulfill it.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_MemPoolAlloc(
   vmk_MemPool pool,
   const vmk_MemPoolAllocProps *props,
   vmk_uint32 numPages,
   vmk_Bool wait,
   vmk_MachPage *startPage);

/*
 ***********************************************************************
 * vmk_MemPoolFree --                                             */ /**
 *
 * \ingroup MemPool
 * \brief Free a contiguous range of machine pages allocated from a
 *        memory pool.
 *
 * \param[in]  startPage   The first page of the page range to free.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_MemPoolFree(
   vmk_MachPage *startPage);

#endif /* _VMKAPI_MEMPOOL_H_ */
/** @} */
