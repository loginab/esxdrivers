/* **********************************************************
 * Copyright 2006 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Network Types                                                  */ /**
 * \addtogroup Network
 *@{
 ***********************************************************************
 */
#ifndef _VMKAPI_NET_TYPES_H_
#define _VMKAPI_NET_TYPES_H_

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

#include "base/vmkapi_status.h"
#include "base/vmkapi_types.h"

/*
 ***********************************************************************
 * Vlan                                                           */ /**
 * \defgroup Vlan Virtual Lan
 *@{
 ***********************************************************************
 */

/**
 * \brief Identifier number for vlan group.
 */

typedef vmk_uint16        vmk_VlanID;

/**
 * \brief 802.1p priority value for vlan tag.
 */

typedef vmk_uint8        vmk_VlanPriority;

/*
 * \brief Symbolic names for 802.1p priority values.
 *
 * Following the published version in 802.1Q-2005 Annex G.
 * Actual ranking in order of least to most important is not strictly numerical.
 * Priority 0 is the default priority and is ranked specially.
 * The ranking, from least to most important, is
 *    1,  0,  2,  3,  4,  5,  6,  7
 * or, using the corresponding 2-letter acronyms from 802.1Q,
 *    BK, BE, EE, CA, VI, VO, IC, NC
 */
enum {
   VMK_VLAN_PRIORITY_MINIMUM = 0,

   VMK_VLAN_PRIORITY_BE = VMK_VLAN_PRIORITY_MINIMUM,
   VMK_VLAN_PRIORITY_BEST_EFFORT = VMK_VLAN_PRIORITY_BE,

   VMK_VLAN_PRIORITY_BK = 1,
   VMK_VLAN_PRIORITY_BACKGROUND = VMK_VLAN_PRIORITY_BK,

   VMK_VLAN_PRIORITY_EE = 2,
   VMK_VLAN_PRIORITY_EXCELLENT_EFFORT = VMK_VLAN_PRIORITY_EE,

   VMK_VLAN_PRIORITY_CA = 3,
   VMK_VLAN_PRIORITY_CRITICAL_APPS = VMK_VLAN_PRIORITY_CA,

   VMK_VLAN_PRIORITY_VI = 4,
   VMK_VLAN_PRIORITY_VIDEO = VMK_VLAN_PRIORITY_VI,

   VMK_VLAN_PRIORITY_VO = 5,
   VMK_VLAN_PRIORITY_VOICE = VMK_VLAN_PRIORITY_VO,

   VMK_VLAN_PRIORITY_IC = 6,
   VMK_VLAN_PRIORITY_INTERNETWORK_CONROL = VMK_VLAN_PRIORITY_IC,

   VMK_VLAN_PRIORITY_NC = 7,
   VMK_VLAN_PRIORITY_NETWORK_CONROL = VMK_VLAN_PRIORITY_NC,

   VMK_VLAN_PRIORITY_MAXIMUM = VMK_VLAN_PRIORITY_NC,

   VMK_VLAN_NUM_PRIORITIES = VMK_VLAN_PRIORITY_MAXIMUM+1,

   VMK_VLAN_PRIORITY_INVALID = (vmk_VlanPriority)~0U
};

/** @} */

/*
 ***********************************************************************
 * Uplink                                                         */ /**
 * \addtogroup Uplink
 *@{
 ***********************************************************************
 */

/**
 * \brief Structure to represent an uplink.
 */
typedef struct UplinkDev  vmk_Uplink;

#endif /* _VMKAPI_NET_TYPES_H_ */
/** @} */
/** @} */
