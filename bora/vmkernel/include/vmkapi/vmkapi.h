/* **********************************************************
 * Copyright 2004 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 * VMKernel external API 
 */ 

#ifndef _VMKAPI_H_
#define _VMKAPI_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"
/** \endcond never */

/*
 ***********************************************************************
 * Devkit-based Header Inclusions.
 *********************************************************************** 
 */

/*
 * If you are adding a new API header please add it to the appropriate
 * devkit include section. If you need a new devkit include section please
 * contact the appropriate devkit group.
 */

/* @@@ DEVKIT_DEFINE VMKAPI_BASE base @@@ */
#ifdef VMK_DEVKIT_HAS_API_VMKAPI_BASE
#include "base/vmkapi_revision.h"
#include "base/vmkapi_status.h"
#include "base/vmkapi_compiler.h"
#include "base/vmkapi_const.h"
#include "base/vmkapi_context.h"
#include "base/vmkapi_types.h"
#include "base/vmkapi_accounting.h"
#include "base/vmkapi_bits.h"
#include "base/vmkapi_atomic.h"
#include "base/vmkapi_lock.h"
#include "base/vmkapi_sem.h"
#include "base/vmkapi_heap.h"
#include "base/vmkapi_libc.h"
#include "base/vmkapi_module.h"
#include "base/vmkapi_char.h"
#include "base/vmkapi_memory.h"
#include "base/vmkapi_scatter_gather.h"
#include "base/vmkapi_module.h"
#include "base/vmkapi_revision.h"
#include "base/vmkapi_status.h"
#include "base/vmkapi_system.h"
#include "base/vmkapi_types.h"
#include "base/vmkapi_list.h"
#include "base/vmkapi_slist.h"
#include "base/vmkapi_cslist.h"
#include "base/vmkapi_assert.h"
#include "base/vmkapi_world.h"
#include "base/vmkapi_time.h"
#include "base/vmkapi_logging.h"
#include "base/vmkapi_bottom_half.h"
#include "base/vmkapi_platform.h"
#include "base/vmkapi_mempool.h"
#include "base/vmkapi_worldlet.h"
#include "base/vmkapi_config.h"
#include "base/vmkapi_entropy.h"
#include "base/vmkapi_proc.h"
#include "base/vmkapi_slab.h"
#include "base/vmkapi_stress.h"
#include "base/vmkapi_helper.h"
#include "base/vmkapi_util.h"
#endif

/* @@@ DEVKIT_DEFINE VMKAPI_DEVICE device @@@ */
#ifdef VMK_DEVKIT_HAS_API_VMKAPI_DEVICE
#include "device/vmkapi_acpi.h"
#include "device/vmkapi_device.h"
#include "device/vmkapi_device_name.h"
#include "device/vmkapi_isa.h"
#include "device/vmkapi_pci.h"
#include "device/vmkapi_input.h"
#include "device/vmkapi_vector.h"
#endif

/* @@@ DEVKIT_DEFINE VMKAPI_DVFILTER dvfilter @@@ */
#ifdef VMK_DEVKIT_HAS_API_VMKAPI_DVFILTER
#include "dvfilter/vmkapi_dvfilter.h"
#endif

/* @@@ DEVKIT_DEFINE VMKAPI_DVS dvs @@@ */
#ifdef VMK_DEVKIT_HAS_API_VMKAPI_DVS
#include "dvs/vmkapi_dvs_ether.h"
#include "dvs/vmkapi_dvs_portset.h"
#endif

/* @@@ DEVKIT_DEFINE VMKAPI_NET net @@@ */
#ifdef VMK_DEVKIT_HAS_API_VMKAPI_NET
#include "net/vmkapi_net.h"
#endif

/* @@@ DEVKIT_DEFINE VMKAPI_SOCKETS sockets @@@ */
#ifdef VMK_DEVKIT_HAS_API_VMKAPI_SOCKETS
#include "sockets/vmkapi_socket.h"
#include "sockets/vmkapi_socket_ip.h"
#include "sockets/vmkapi_socket_ip6.h"
#include "sockets/vmkapi_socket_vmklink.h"
#endif

/* @@@ DEVKIT_DEFINE VMKAPI_SCSI scsi @@@ */
#ifdef VMK_DEVKIT_HAS_API_VMKAPI_SCSI
#include "scsi/vmkapi_scsi.h"
#include "scsi/vmkapi_scsi_trace.h"
#include "scsi/vmkapi_scsi_mgmt_types.h"
#endif

/* @@@ DEVKIT_DEFINE VMKAPI_MPP mpp @@@ */
#ifdef VMK_DEVKIT_HAS_API_VMKAPI_MPP
#include "mpp/vmkapi_mpp.h"
#endif

/* @@@ DEVKIT_DEFINE VMKAPI_NMP nmp @@@ */
#ifdef VMK_DEVKIT_HAS_API_VMKAPI_NMP
#include "nmp/vmkapi_nmp.h"
#include "nmp/vmkapi_nmp_psp.h"
#include "nmp/vmkapi_nmp_satp.h"
#endif

/* @@@ DEVKIT_DEFINE VMKAPI_NPIV npiv @@@ */
#ifdef VMK_DEVKIT_HAS_API_VMKAPI_NPIV
#include "npiv/vmkapi_npiv.h"
#include "npiv/vmkapi_npiv_wwn.h"
#endif

/* @@@ DEVKIT_DEFINE VMKAPI_VSI "vsi" @@@ */
#ifdef VMK_DEVKIT_HAS_API_VMKAPI_VSI
#include "vsi/vmkapi_vsi.h"
#endif

/*
 ***********************************************************************
 * "Top Level" documentation groups.
 *
 * These are documentation groups that don't necessarily have a single
 * file that they can logically reside in. Most doc groups can simply
 * be defined in a specific header.
 *
 *********************************************************************** 
 */

/**
 * \defgroup Storage Storage
 * \{ \}
 */

/**
 * \defgroup Network Network
 * \{ \}
 */

/*
 ***********************************************************************
 * API Version information.
 *********************************************************************** 
 */
#define VMKAPI_REVISION_MAJOR 1
#define VMKAPI_REVISION_MINOR 0
#define VMKAPI_REVISION_PATCH 0
#define VMKAPI_REVISION_DEVEL 0

#define VMKAPI_REVISION  VMK_REVISION_NUMBER(VMKAPI)

#endif /* _VMKAPI_H_ */
