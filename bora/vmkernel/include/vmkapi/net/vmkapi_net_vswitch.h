/* **********************************************************
 * Copyright 2006 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Vswitch                                                        */ /**
 * \addtogroup Network
 *@{
 * \defgroup Vswitch Virtual Switch
 *@{ 
 *
 * \par Vswitch:
 *
 * In vmkernel, many different instances could need to communicate with
 * the external world but also between them.
 * These internal communications are done through a virtual switch which
 * is roughly a set of port with policies connecting a set of instances
 * together.
 *
 * Each instance is connected to a port and all the inbound/outbound
 * network packets are going through it.
 * To emulate a physical switch behavior, every port owns a chain of command
 * processing, filtering the packet and post them to their next destination.
 * For more information about port's chain of command refer to iochain vmkapi.
 *
 ***********************************************************************
 */

#ifndef _VMKAPI_NET_VSWITCH_H_
#define _VMKAPI_NET_VSWITCH_H_

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

#include "base/vmkapi_const.h"
#include "base/vmkapi_types.h"

/** Invalid identification number for a port */
#define VMK_VSWITCH_INVALID_PORT_ID 0

/**
 * \brief Identifier number for port on a virtual switch.
 */

typedef vmk_uint32 vmk_VswitchPortID;

/**
 * \brief Event identifier for vswitch notifications.
 */

typedef vmk_uint64 vmk_VswitchEvent;

/** Port has been connected */
#define VMK_VSWITCH_EVENT_PORT_CONNECT    0x1

/** Port has been disconnected */
#define VMK_VSWITCH_EVENT_PORT_DISCONNECT 0x2

/** Port has been blocked */
#define VMK_VSWITCH_EVENT_PORT_BLOCK      0x4

/** Port has been unblocked */
#define VMK_VSWITCH_EVENT_PORT_UNBLOCK    0x8

/** Port ethernet frame policy has been updated */
#define VMK_VSWITCH_EVENT_PORT_L2ADDR     0x10

/** Port has been enabled */
#define VMK_VSWITCH_EVENT_PORT_ENABLE     0x20

/** Port has been disabled */
#define VMK_VSWITCH_EVENT_PORT_DISABLE    0x40

/** Port event mask */
#define VMK_VSWITCH_EVENT_MASK_ALL        0x7f

/** Event callback used for vswitch notification */
typedef void (*vmk_VswitchEventCB)(vmk_VswitchPortID, vmk_VswitchEvent, void *);

/*
 ***********************************************************************
 * vmk_VswitchRegisterEventCB --                                  */ /**
 *
 * \ingroup Vswitch
 * \brief  Register a handler to receive vswitch event notifications.
 *
 * \note These are asynchronous event notifications, meaning that the event
 *       handler should examine the port to determine current state at
 *       the time the callback is made.
 *
 * \param[in]  cb          Handler to call to notify a vswitch event
 * \param[in]  cbData      Data to pass to the handler
 * \param[out] handle      Handle needed to passed in order to unregister the
 *                         handler
 *
 * \retval     VMK_OK      Registration succeeded
 * \retval     VMK_FAILURE Otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_VswitchRegisterEventCB(vmk_VswitchEventCB cb,
					    void *cbData,
					    void **handle);

/*
 ***********************************************************************
 * vmk_VswitchUnregisterEventCB --                                */ /**
 *
 *  \ingroup Vswitch
 *  \brief  Unregister a handler to receive vswitch event notifications.
 *
 *  \param[in] handle Handle return by register process
 *
 *  \retval    VMK_OK Always
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_VswitchUnregisterEventCB(void *handle);

#endif /* _VMKAPI_NET_VSWITCH_H_ */
/** @} */
/** @} */
