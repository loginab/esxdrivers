/* **********************************************************
 * Copyright 2007 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Contexts                                                       */ /**
 * \defgroup Contexts Execution Context Information
 *
 * There are several types of execution contexts available to run code
 * in vmkernel.
 *
 * @{
 ***********************************************************************
 */
 
#ifndef _VMKAPI_CONTEXT_H_
#define _VMKAPI_CONTEXT_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_status.h"

/**
 * \brief Possible execution contexts
 */
typedef enum {
   VMK_CONTEXT_TYPE_UNKNOWN=0,
   VMK_CONTEXT_TYPE_NMI=1,
   VMK_CONTEXT_TYPE_INTERRUPT=2,
   VMK_CONTEXT_TYPE_BOTTOM_HALF=3,
   VMK_CONTEXT_TYPE_WORLD=4,
} vmk_ContextType;

/*
 ***********************************************************************
 * vmk_ContextGetCurrentType --                                   */ /**
 *
 * \ingroup Contexts
 * \brief Get the current execution context type the caller is
 *        executing in.
 *
 * \return The current execution context.
 *
 ***********************************************************************
 */
vmk_ContextType vmk_ContextGetCurrentType(
   void);

/*
 ***********************************************************************
 * vmk_ContextGetCurrentType --                                   */ /**
 *
 * \ingroup Contexts
 * \brief Determine if the current context is an interrupt handler and
 *        return the interrupt vector being handled.
 *
 * \return VMK_TRUE   if running an interrupt handler.
 * \return VMK_FALSE  if not running an interrupt handler.
 *
 ***********************************************************************
 */
vmk_Bool vmk_ContextIsInterruptHandler(
   vmk_uint32 *vector);

/*
 ***********************************************************************
 * vmk_ContextTypeCanBlock --                                     */ /**
 *
 * \ingroup Contexts
 * \brief Determine if a particular context type allows blocking calls.
 *
 * This API only indicates if the given context allows blocking in
 * general. It does NOT indicate whether the context \em currently allows
 * blocking. For instance, it is forbidden to block while holding
 * a spinlock even from a blockable context. If this call were made
 * while a spinlock were held, it would return the same value as if
 * a spinlock were not held. So it may not be used to determine if
 * a context is \em currently blockable.
 *
 * \param[in] type   The context type to check.
 *
 * \retval VMK_TRUE     The supplied context type allows blocking calls.
 * \retval VMK_FALSE    The supplied context type does not allow
 *                      blocking calls.
 *
 ***********************************************************************
 */
vmk_Bool vmk_ContextTypeCanBlock(
   vmk_ContextType type);

/*
 ***********************************************************************
 * vmk_ContextTypeToString --                                     */ /**
 *
 * \ingroup Contexts
 * \brief Convert a context type to a human-readable string.
 *
 * \param[in] type   The context type to convert to a string.
 *
 * \return The string corresponding to the context type.
 *
 ***********************************************************************
 */
const char * vmk_ContextTypeToString(
   vmk_ContextType type);

#endif /* _VMKAPI_CONTEXT_H_ */
/** @} */
