/* **********************************************************
 * Copyright 2004 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Semaphores                                                     */ /**
 * \defgroup Semaphores Semaphores
 * 
 * \par Binary Semaphore Ranks:
 * Binary semaphores are semaphores which may be treated like blocking
 * locks. As such, they take a rank and sub-rank in a manner analogous
 * the the lock ranking used for spinlocks.\n
 * \n
 * When a world locks a binary semaphore \em BS1 with major rank \em R1 and
 * minor rank \em r1, it may only lock another binary semaphore BS2 with
 * major rank \em R2 and minor rank \em r2 when:
 * \n
 *   \em R2 > \em R1                         \n
 * or                                        \n
 *   \em R2 == \em R1 and \em r2 > \em r1    \n
 * \n
 * Be aware that rank checking is only performed on debug builds.
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_SEM_H_
#define _VMKAPI_SEM_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_VMKDRIVERS
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_module.h"
#include "base/vmkapi_status.h"

/** \cond nodoc */
/* Private definitions */
#define _VMK_SEMA_RANK_UNRANKED 0x10000
#define _VMK_SEMA_RANK_MAX      0xffff
#define _VMK_SEMA_RANK_MIN      0

#define _VMK_SEMA_RANK_LEAF     _VMK_SEMA_RANK_MAX
#define _VMK_SEMA_RANK_STORAGE  0x8000

typedef vmk_uint32 _vmk_SemaRank, _vmk_SemaRankMinor;
/** \endcond nodoc */

/**
 * \brief unranked rank
 */
#define VMK_SEMA_RANK_UNRANKED _VMK_SEMA_RANK_UNRANKED

/**
 * \brief Leaf rank for semaphores
 */
#define VMK_SEMA_RANK_LEAF _VMK_SEMA_RANK_LEAF

/**
 * \brief Maximum rank for semaphores
 */
#define VMK_SEMA_RANK_MAX _VMK_SEMA_RANK_MAX

/**
 * \brief Minimum rank for semaphores
 */
#define VMK_SEMA_RANK_MIN _VMK_SEMA_RANK_MIN

/**
 * \brief Rank for semaphores in storage components
 */
#define VMK_SEMA_RANK_STORAGE  _VMK_SEMA_RANK_STORAGE

/**
 * \brief Rank for semaphores
 */
typedef _vmk_SemaRank vmk_SemaphoreRank;

/**
 * \brief Sub rank for semaphores
 */
typedef _vmk_SemaRankMinor vmk_SemaphoreRankMinor;

/**
 * \brief Opaque handle for semaphores
 */
typedef struct vmk_SemaphoreInt *vmk_Semaphore;

/*
 ***********************************************************************
 * vmk_SemaCreate --                                              */ /**
 *
 * \ingroup Semaphores
 * \brief Allocate and initialize a counting semaphore
 *
 * \note Requires that the module heap be initialized.
 *
 * \param[out] sema              New counting semaphore.
 * \param[in]  moduleID          Module on whose behalf the semaphore
 *                               is created.
 * \param[in]  name              Human-readable name of the semaphore.
 * \param[in]  value             Initial count.
 *
 * \retval VMK_OK The semaphore was successfully created
 * \retval VMK_NO_MEMORY The semaphore could not be allocated
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_SemaCreate(
   vmk_Semaphore *sema,
   vmk_ModuleID moduleID,
   const char *name,
   vmk_int32 value);

/*
 ***********************************************************************
 * vmk_BinarySemaCreate --                                        */ /**
 *
 * \ingroup Semaphores
 * \brief Allocate and initialize a binary semaphore
 *
 * \note Requires that the module heap be initialized.
 *
 * \param[out] sema              New counting semaphore.
 * \param[in]  moduleID          Module on whose behalf the semaphore
 *                               is created.
 * \param[in]  name              Human-readable name of the semaphore.
 * \param[in]  majorRank         Major rank of the semaphore.
 *                               The rank value must be greater than or
 *                               equal to VMK_SEMA_RANK_MIN and less than
 *                               or equal to VMK_SEMA_RANK_MAX.
 * \param[in]  minorRank         Minor rank of the semaphore.
 *                               The rank value must be greater than or
 *                               equal to VMK_SEMA_RANK_MIN and less than
 *                               or equal to VMK_SEMA_RANK_MAX.
 *
 * \retval VMK_OK The semaphore was successfully created
 * \retval VMK_NO_MEMORY The semaphore could not be allocated
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_BinarySemaCreate(
   vmk_Semaphore *sema,
   vmk_ModuleID moduleID,
   const char *name,
   vmk_SemaphoreRank majorRank,
   vmk_SemaphoreRankMinor minorRank);

/*
 ***********************************************************************
 * _vmkSemaIsLocked
 *
 * This is used by VMK_ASSERT_SEMA_LOCKED and VMK_ASSERT_SEMA_UNLOCKED.
 * VMKAPI users should not call this function directly.
 *
 ***********************************************************************
 */
/** \cond nodoc */
VMK_ReturnStatus _vmkSemaIsLocked(
   vmk_Semaphore *sema,
   vmk_Bool *isLocked);
/** \endcond */

/*
 ***********************************************************************
 * VMK_ASSERT_SEMA_LOCKED --                                      */ /**
 *
 * \ingroup Semaphores
 * \brief Assert that a semaphore is currently locked only in
 *        debug builds.
 *
 * \param[in] sema   Semaphore to check.
 *
 ***********************************************************************
 */
#if defined(VMX86_DEBUG)
#define VMK_ASSERT_SEMA_LOCKED(sema)                                 \
   do {                                                              \
      vmk_Bool _vmkCheckLockState ;                                  \
      VMK_ASSERT(_vmkSemaIsLocked((sema),&_vmkCheckLockState) ==     \
                 VMK_OK);                                            \
      VMK_ASSERT(_vmkCheckLockState);                                \
   } while(0)
#else
#define VMK_ASSERT_SEMA_LOCKED(sema)
#endif

/*
 ***********************************************************************
 * VMK_ASSERT_SEMA_UNLOCKED --                                    */ /**
 *
 * \ingroup Semaphores
 * \brief Assert that a semaphore is currently unlocked only in
 *        debug builds.
 *
 * \param[in] sema   Semaphore to check.
 *
 ***********************************************************************
 */
#if defined(VMX86_DEBUG)
#define VMK_ASSERT_SEMA_UNLOCKED(sema)                               \
   do {                                                              \
      vmk_Bool _vmkCheckLockState ;                                  \
      VMK_ASSERT(_vmkSemaIsLocked((sema),&_vmkCheckLockState) ==     \
                 VMK_OK);                                            \
      VMK_ASSERT(!_vmkCheckLockState);                               \
   } while(0)
#else
#define VMK_ASSERT_SEMA_UNLOCKED(sema) 
#endif

/*
 ***********************************************************************
 * vmk_SemaLock --                                                */ /**
 *
 * \ingroup Semaphores
 * \brief Acquire a semaphore
 *
 * \pre Shall be called from a blockable context.
 * \pre The caller shall not already hold a semaphore of lower or equal
 *      rank if the semaphore is a binary semaphore.
 *
 * \param[in] sema   The semaphore to acquire.
 *
 ***********************************************************************
 */
void vmk_SemaLock(
   vmk_Semaphore *sema);

/*
 ***********************************************************************
 * vmk_SemaTryLock --                                             */ /**
 *
 * \ingroup Semaphores
 * \brief Try to acquire a semaphore.
 *
 * This tries to acquire the given semaphore once.
 * If the semaphore is already locked, it returns immediately.
 *
 * \pre  Shall be called from a blockable context.
 *
 * \param[in] sema   Semaphore to attempt to acquire.
 *
 * \retval VMK_OK       The semaphore was successfully acquired.
 * \retval VMK_BUSY     The semaphore is currently locked.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_SemaTryLock(
   vmk_Semaphore *sema);

/*
 ***********************************************************************
 * vmk_SemaUnlock --                                              */ /**
 *
 * \ingroup Semaphores
 * \brief Release a semaphore
 *
 * \param[in] sema   Semaphore to unlock.
 *
 ***********************************************************************
 */
void vmk_SemaUnlock(
   vmk_Semaphore *sema);

/*
 ***********************************************************************
 * vmk_SemaDestroy --                                             */ /**
 *
 * \ingroup Semaphores
 * \brief Destroy a semaphore
 *
 * Revert all side effects of vmk_SemaCreate or vmk_BinarySemaCreate.
 *
 * \param[in] sema   Semaphore to destroy.
 *
 ***********************************************************************
 */
void vmk_SemaDestroy(vmk_Semaphore *sema);

#endif /* _VMKAPI_SEM_H_ */
/** @} */
