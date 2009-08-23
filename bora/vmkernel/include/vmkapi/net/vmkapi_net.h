/* **********************************************************
 * Copyright 2006 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Network                                                        */ /**
 * \addtogroup Network
 * @{ 
 *
 ***********************************************************************
 */

#ifndef _VMKAPI_NET_H_
#define _VMKAPI_NET_H_

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

#include "net/vmkapi_net_types.h"
#include "net/vmkapi_net_vswitch.h"
#include "net/vmkapi_net_netqueue.h"
#include "net/vmkapi_net_pkt.h"
#include "net/vmkapi_net_pktlist.h"
#include "net/vmkapi_net_uplink.h"

#endif /* _VMKAPI_NET_H_ */
/** @} */
