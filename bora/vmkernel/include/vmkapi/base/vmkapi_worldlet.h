/* **********************************************************
 * Copyright 2007 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Worldlets                                                      */ /**
 * \defgroup Worldlet Worldlets
 *
 * A worldlet is an object that performs a specified function at 
 * some point in the future (relative to the time of invocation, which
 * is accomplished via vmk_WorldletActivate).
 *
 * \note
 * All functions of this API require that callers do not hold
 * any locks with rank _VMK_SP_RANK_IRQ_WORLDLET.
 *
 * @{
 ***********************************************************************
 */
#ifndef _VMKAPI_WORLDLET_H_
#define _VMKAPI_WORLDLET_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_VMKDRIVERS
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_status.h"
#include "base/vmkapi_accounting.h"

/**
 * \brief Handle for vmk_Worldlet
 */
typedef struct vmk_WorldletInt* vmk_Worldlet;

/**
 * \brief States of worldlets.
 */
typedef enum {
   VMK_WDT_RELEASE = 1,
   VMK_WDT_SUSPEND = 2,
   VMK_WDT_READY = 4,
} vmk_WorldletState;

/**
 * \brief Size of the name of a worldlet. All names will be truncated if
 *        necessary so that the name including NULL termination is of
 *        this length or less.
 */
#define VMK_WDT_NAME_SIZE_MAX       32


/*
 ***********************************************************************
 * vmk_WorldletFn --                                              */ /**
 *
 * \ingroup Worldlet
 * \brief Prototype for Worldlet callback function.
 *
 * \param[in]   wdt     Worldlet handle representing the executing worldlet.
 * \param[in]   private Private data as specified to vmk_WorldletCreate.
 * \param[out]  state   The Worldlet sets this to define its state upon
 *                      completion of the callback function:
 *                VMK_WDT_RELEASE: Implies  call to vmk_WorldletDestroy 
 *                                 and VMK_WDT_SUSPEND if the worldlet
 *                                 is not freed.
 *                VMK_WDT_SUSPEND: The worldlet will not execute unless
 *                                 directed to do so by a vmk_WorldletActivate
 *                                 call.
 *                VMK_WDT_READY:   The worldlet will be called again when
 *                                 the system decides to grant it CPU time.
 *
 * \retval VMK_OK The worldlet function executed correctly.
 *                This is not a status of whether the actions of the function
 *                were successfully completed, but rather an indication that
 *                the code of the function executed.  The return of any other
 *                value may have undefined side-effects.
 *
 ***********************************************************************
 */
typedef VMK_ReturnStatus (*vmk_WorldletFn)(vmk_Worldlet wdt, void *data,
                                           vmk_WorldletState *state);


/*
 ***********************************************************************
 * vmk_WorldletCreate --                                          */ /**
 *
 * \ingroup Worldlet
 * \brief Create a worldlet object.
 *
 * \param[out] worldlet   Pointer to new vmk_Worldlet.
 * \param[in]  name       Descriptive name for the worldlet.
 * \param[in]  serviceID  Service ID against which the worldlet is charged.
 * \param[in]  moduleID   Id of module making request.
 * \param[in]  worldletFn Pointer to function called when dispatching the 
 *                        request.
 * \param[in]  private    Arbitrary data to associate with the vmk_Worldlet.
 *
 * \retval VMK_OK               The worldlet was successfully initialized.
 * \retval VMK_NO_MEMORY        The worldlet could not be allocated.
 * \retval VMK_INVALID_HANDLE   The specified service ID is invalid.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldletCreate(
   vmk_Worldlet       *worldlet,
   const char         *name,
   vmk_ServiceAcctId  serviceID,
   vmk_ModuleID       moduleID,
   vmk_WorldletFn     worldletFn,
   void               *private);


/*
 ***********************************************************************
 * vmk_WorldletCheckState --                                      */ /**
 *
 * \ingroup Worldlet
 * \brief Query the state of a worldlet.
 *
 * \param[in]  worldlet Pointer to new vmk_Worldlet.
 * \param[out] state    State of the worldlet.
 *
 * \retval VMK_OK             The worldlet state was successfully returned.
 * \retval VMK_BAD_PARAM      "state" is a bad pointer.
 * \retval VMK_INVALID_HANDLE "worldlet" is invalid or corrupted.
 *
 ***********************************************************************
 */
VMK_ReturnStatus
vmk_WorldletCheckState(vmk_Worldlet worldlet, vmk_WorldletState *state);

/*
 ***********************************************************************
 * vmk_WorldletActivate --                                        */ /**
 *
 * \ingroup Worldlet
 * \brief Activate a worldlet object.  The worldlet's callback function will
 *        be called at least once following the successful execution of this
 *        function.
 *
 * \param[in] worldlet Worldlet to activate.
 *
 * \retval VMK_OK       The worldlet was successfully activated.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldletActivate(
   vmk_Worldlet  worldlet);

/*
 ***********************************************************************
 * vmk_WorldletUnref --                                           */ /**
 *
 * \ingroup Worldlet
 * \brief Release a worldlet (thereby decrementing internal ref count).
 *        The caller is responsible for ensuring that when this is called
 *        to release the last reference, that the worldlet is not awaiting
 *        pending activation and will not be activated in the future.
 *        This implies that the worldlet must be in a VMK_WDT_SUSPEND state.
 *
 * \param[in]  worldlet  Worldlet to destroy.
 *
 * \retval VMK_OK       The worldlet was successfully released.
 * \retval VMK_BUSY     The worldlet is still in use.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldletUnref(
   vmk_Worldlet  worldlet);

/*
 ***********************************************************************
 * vmk_WorldletShouldYield --                                     */ /**
 *
 * \ingroup Worldlet
 * \brief Returns an indicator of whether a worldlet should yield.
 *
 * \param[in]  worldlet       Currently executing worldlet.
 * \param[out] yield          Set to VMK_TRUE/VMK_FALSE to indicate if
 *                            worldlet should yield.
 *
 * \retval VMK_OK             Success.
 * \retval VMK_INVALID_HANDLE Worldlet is invalid or not running.
 * \retval VMK_BAD_PARAM      "yield" is a bad pointer.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldletShouldYield(
   vmk_Worldlet   worldlet,
   vmk_Bool       *yield);

/*
 ***********************************************************************
 * vmk_WorldletGetCurrent --                                      */ /**
 *
 * \ingroup Worldlet
 * \brief Returns current executing worldlet and private data, if the.
 *        calling code is in fact running in a worldlet.
 *
 * \param[out] worldlet       Currently executing worldlet.
 * \param[out] private        Private data associated with worldlet.
 *
 * \retval VMK_OK             Success.
 * \retval VMK_NOT_FOUND      Not running on a worldlet. 
 * \retval VMK_BAD_PARAM      Invalid "worldlet" or "private" pointers.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldletGetCurrent(
   vmk_Worldlet   *worldlet,
   void           **private);

/*
 ***********************************************************************
 * vmk_WorldletSetAffinityToWorldlet --                           */ /**
 *
 * \ingroup Worldlet
 * \brief Sets the affinity of one worldlet to another, meaning that the
 *        system will attempt to execute the "worldlet" worldlet on the 
 *        same CPU as the "target" worldlet.  Unsetting this affinity
 *        is accomplished by using a NULL value for the "target" worldlet.
 *      
 *        This function alters internal reference counts therefore if
 *        a worldlet is used as a "target" worldlet the affinity to
 *        that worldlet must be torn down prior to it being destroyed
 *        (i.e. vmk_WorldletUnref will not return VMK_OK).
 *
 * \param[in]  worldlet       Worldlet whose affinity will be changed.
 * \param[in]  target         Worldlet to which "worldlet" will have 
 *                            affinity to. (May be NULL.)
 * \param[in]  exact          Affinity is exact core (TRUE) or LLC (FALSE)
 *
 * \retval VMK_OK             Success.
 * \retval VMK_INVALID_HANDLE "worldlet" or "target" are invalid.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldletSetAffinityToWorldlet(
   vmk_Worldlet   worldlet,
   vmk_Worldlet   target,
   vmk_Bool       exact);

/*
 ***********************************************************************
 * vmk_WorldletVectorSet --                                       */ /**
 *
 * \ingroup Worldlet
 * \brief Sets the interrupt vector for the worldlet. Once set, the
 *        the worldlet scheduler takes over interrupt scheduling. When
 *        the worldlet moves, it's corresponding interrupt is moved
 *        too.
 *
 * \param[in]  worldlet       Worldlet the vector is associated to
 * \param[in]  vector         Interrupt vector
 *
 * \retval VMK_OK             Success.
 * \retval VMK_INVALID_HANDLE "worldlet" or "target" are invalid.
 *
 ***********************************************************************
 */
VMK_ReturnStatus
vmk_WorldletVectorSet(vmk_Worldlet worldlet, vmk_uint32 vector);


/*
 ***********************************************************************
 * vmk_WorldletVectorUnSet --                                     */ /**
 *
 * \ingroup Worldlet
 * \brief Disassociate interrupt vector previously associated with the
 *        worldlet. 
 *
 * \param[in]  worldlet       Worldlet the vector is associated to
 *
 * \retval VMK_OK             Success.
 * \retval VMK_INVALID_HANDLE "worldlet" or "target" are invalid.
 *
 ***********************************************************************
 */
VMK_ReturnStatus
vmk_WorldletVectorUnSet(vmk_Worldlet worldlet);

/*
 ***********************************************************************
 * vmk_WorldletNameSet --                                         */ /**
 *
 * \ingroup Worldlet
 * \brief Set the name of a worldlet object.
 *
 * \param[in] worldlet   worldlet object.
 * \param[in] name       Descriptive name for the worldlet.

 * \retval VMK_OK               Success.
 * \retval VMK_INVALID_HANDLE   "worldlet" or "target" are invalid.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldletNameSet(vmk_Worldlet worldlet, const char *name);

#endif /* _VMKAPI_WORLDLET_H_ */
/** @} */
