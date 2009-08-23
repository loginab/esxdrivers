/* **********************************************************
 * Copyright 2008 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Accounting                                                     */ /**
 * \defgroup Accounting System Time Accounting
 *
 * System time accounting allows work for a particular service to be
 * charged to a world. This allows work to be offloaded to several worlds
 * or other contexts but charged to the appropriate world on whose behalf
 * the work is being done.
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_ACCOUNTING_H_
#define _VMKAPI_ACCOUNTING_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_world.h"

/*
 * Well-known accounting service names
 */
#define VMK_SERVICE_ACCT_NAME_KERNEL  "kernel"
#define VMK_SERVICE_ACCT_NAME_SCSI    "scsi"

/**
 * \ingroup Accounting
 * \brief Opaque handle representing a service to charge to.
 */
typedef vmk_uint64 vmk_ServiceAcctId;

/**
 * \ingroup Accounting
 * \brief Opaque handle to a world-related accounting context.
 */
typedef struct Sched_SysAcctContext *vmk_ServiceTimeContext;

/**
 * \ingroup Accounting
 * \brief A service time context that isn't associated with any service.
 */
#define VMK_SERVICE_TIME_CONTEXT_NONE ((vmk_ServiceTimeContext) 0)

/*
 ***********************************************************************
 * vmk_ServiceGetID --                                            */ /**
 *
 * \ingroup Accounting
 * \brief Lookup a service accounting ID handle by name
 *
 * \param[in]  name        Well-known accounting service name to lookup.
 * \param[out] serviceId   Service identifier handle corresponding to
 *                         the name.
 *
 * \retval VMK_INVALID_NAME   The specified service name is invalid.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ServiceGetID(
   const char *name,
   vmk_ServiceAcctId *serviceId);

/*
 ***********************************************************************
 * vmk_ServiceTimeChargeBeginWorld --                             */ /**
 *
 * \ingroup Accounting
 * \brief Begin charging work for a serivce to a world ID
 *
 * \param[in] serviceId    Type of service for the work.
 * \param[in] worldId      World on whose behalf the work is being done.
 *                         May be VMK_INVALID_WORLD_ID if the caller is
 *                         charging to a particular service category
 *                         not on behalf of any particular world.
 *
 * \note A world should not deschedule between the invocation of
 *       vmk_ServiceTimeChargeBeginWorld() and its corresponding
 *       vmk_ServiceTimeChargeEndWorld().
 *
 * \return A handle to an accounting context.
 *
 ***********************************************************************
 */
vmk_ServiceTimeContext vmk_ServiceTimeChargeBeginWorld(
   vmk_ServiceAcctId serviceId,
   vmk_WorldID worldId);

/*
 ***********************************************************************
 * vmk_ServiceTimeChargeEndWorld --                               */ /**
 *
 * \ingroup Accounting
 * \brief Stop charging work against a world.
 *
 * \param[in] context   Accounting context to cease charging against.
 *                      May be VMK_SERVICE_TIME_CONTEXT_NONE  in which 
 *                      case no action is taken.
 *
 ***********************************************************************
 */
void vmk_ServiceTimeChargeEndWorld(
   vmk_ServiceTimeContext context);

/*
 ***********************************************************************
 * vmk_ServiceTimeChargeSetWorld --                               */ /**
 *
 * \ingroup Accounting
 * \brief Set the worldID for charging the work.
 *
 * \param[in] context   Accounting context used to charge.
 *                      "context" can be VMK_SERVICE_TIME_CONTEXT_NONE in
 *                      which case, the currently established context
 *                      will be used to charge for given worldId 
 *
 * \param[in] worldId   World on whose behalf the work is being done.
 *
 ***********************************************************************
 */
void
vmk_ServiceTimeChargeSetWorld(vmk_ServiceTimeContext context,
                              vmk_WorldID worldId);

#endif /* _VMKAPI_ACCOUNTING_H_ */
/** @} */
