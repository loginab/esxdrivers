/* **********************************************************
 * Copyright 2007 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * BottomHalves                                                   */ /**
 * \defgroup BottomHalves Bottom-Halves
 *
 * Bottom-halves are soft-interrupt-like contexts that run below
 * the priority of hardware interrupts but outside the context
 * of a schedulable entity like a World. This means that bottom-half
 * callbacks may not block while they execute.
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_BOTTOM_HALF_H_
#define _VMKAPI_BOTTOM_HALF_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMKDRIVERS
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_status.h"

typedef vmk_uint32 vmk_BottomHalf;

typedef void (*vmk_BottomHalfCallback)(vmk_AddrCookie data);

/**
 * \brief Maximum length of a bottom-half's name string, including
 *        the terminating nul.
 */
#define VMK_BH_NAME_MAX  32

/*
 ***********************************************************************
 * vmk_BottomHalfRegister --                                      */ /**
 *
 * \ingroup BottomHalves
 * \brief Register a function that can be scheduled as a bottom-half.
 *
 * \param[in]  callback    Bottom-half callback function.
 * \param[in]  data        Data to be passed to the callback function.
 * \param[out] newBH       Bottom-half identifier
 * \param[in]  name        Name associated with this bottom-half
 *
 * \retval VMK_NO_RESOURCES Bottom-half registration table was full,
 *                          new bottom-half was not registered.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_BottomHalfRegister(
   vmk_BottomHalfCallback callback, 
   vmk_AddrCookie data,
   vmk_BottomHalf *newBH,
   const char *name);

/*
 ***********************************************************************
 * vmk_BottomHalfUnregister --                                    */ /**
 *
 * \ingroup BottomHalves
 * \brief Unregister a previously registered bottom-half callback.
 *
 * \param[in] bottomHalf   Bottom-half to unregister.
 *
 ***********************************************************************
 */
void vmk_BottomHalfUnregister(
   vmk_BottomHalf bottomHalf);

/*
 ***********************************************************************
 * vmk_BottomHalfSchedulePCPU --                                  */ /**
 *
 * \ingroup BottomHalves
 * \brief Schedule the execution of a bottom-half on a particular PCPU
 *
 * This schedules the function which has been registered and associated 
 * with the bottom-half identifier by vmk_BottomHalfRegister to
 * run as bottom-half with the given physical CPU.
 *
 * \warning Do not schedule a bottom-half on another CPU too frequently
 *          since the bottom-half cache-line is kept on the local CPU.
 *
 * \param[in] bottomHalf   Bottom-half to schedule.
 * \param[in] pcpu         PCPU on which the bottom-half should run.
 *
 * \retval VMK_OK The given bottom-half has been scheduled successfully.
 * \retval VMK_INVALID_TARGET The specified PCPU is not available.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_BottomHalfSchedulePCPU(
   vmk_BottomHalf bottomHalf, 
   vmk_uint32 pcpu);

/*
 ***********************************************************************
 * vmk_BottomHalfScheduleAnyPCPU --                                */ /**
 *
 * \ingroup BottomHalves
 * \brief Schedule the execution of a bottom-half on any PCPU
 *
 * \param[in] bottomHalf   Bottom-half to schedule.
 *
 ***********************************************************************
 */
void vmk_BottomHalfScheduleAnyPCPU(
   vmk_BottomHalf bottomHalf);


/*
 ***********************************************************************
 * vmk_BottomHalfCheck --                                         */ /**
 *
 * \ingroup BottomHalves
 * \brief Execute pending bottom-half handlers on the local pcpu.
 *
 * Afterwards, if a reschedule is pending and "canReschedule" is VMK_TRUE
 * then invoke the scheduler.
 *
 * \param[in] canReschedule   If VMK_TRUE then invoke the scheduler after
 *                            pending bottom-halves execute.
 *
 ***********************************************************************
 */
void vmk_BottomHalfCheck(
   vmk_Bool canReschedule);

#endif /* _VMKAPI_BOTTOM_HALF_H_ */
/** @} */
