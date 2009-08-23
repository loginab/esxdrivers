/* **********************************************************
 * Copyright 2008 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Stress                                                         */ /**
 * \defgroup Stress Stress Options
 *
 * The stress option interfaces allow access to special environment
 * variables that inform code whether or not certain stress code
 * should be run.
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_STRESS_H_
#define _VMKAPI_STRESS_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMKDRIVERS
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_status.h"

/*
 * Useful stress option names
 */
/** \cond nodoc */
#define VMK_STRESS_OPT_NET_GEN_TINY_ARP_RARP          "NetGenTinyArpRarp"
#define VMK_STRESS_OPT_NET_IF_CORRUPT_ETHERNET_HDR    "NetIfCorruptEthHdr"
#define VMK_STRESS_OPT_NET_IF_CORRUPT_RX_DATA         "NetIfCorruptRxData"
#define VMK_STRESS_OPT_NET_IF_CORRUPT_RX_TCP_UDP      "NetIfCorruptRxTcpUdp"
#define VMK_STRESS_OPT_NET_IF_CORRUPT_TX              "NetIfCorruptTx"
#define VMK_STRESS_OPT_NET_IF_FAIL_HARD_TX            "NetIfFailHardTx"
#define VMK_STRESS_OPT_NET_IF_FAIL_RX                 "NetIfFailRx"
#define VMK_STRESS_OPT_NET_IF_FAIL_TX_AND_STOP_QUEUE  "NetIfFailTxAndStopQueue"
#define VMK_STRESS_OPT_NET_IF_FORCE_HIGH_DMA_OVERFLOW "NetIfForceHighDMAOverflow"
#define VMK_STRESS_OPT_NET_IF_FORCE_RX_SW_CSUM        "NetIfForceRxSWCsum"
#define VMK_STRESS_OPT_NET_NAPI_FORCE_BACKUP_WORLDLET "NetNapiForceBackupWorldlet"
#define VMK_STRESS_OPT_NET_BLOCK_DEV_IS_SLUGGISH      "NetBlockDevIsSluggish"

#define VMK_STRESS_OPT_SCSI_ADAPTER_ISSUE_FAIL        "ScsiAdapterIssueFail"

#define VMK_STRESS_OPT_VMKLINUX_DROP_CMD_SCSI_DONE    "VmkLinuxDropCmdScsiDone"
#define VMK_STRESS_OPT_VMKLINUX_ABORT_CMD_FAILURE     "VmkLinuxAbortCmdFailure"

#define VMK_STRESS_OPT_USB_BULK_DELAY_PROCESS_URB        "USBBulkDelayProcessURB"
#define VMK_STRESS_OPT_USB_BULK_URB_FAKE_TRANSIENT_ERROR "USBBulkURBFakeTransientError"
#define VMK_STRESS_OPT_USB_DELAY_PROCESS_TD              "USBDelayProcessTD"
#define VMK_STRESS_OPT_USB_FAIL_GP_HEAP_ALLOC            "USBFailGPHeapAlloc"
#define VMK_STRESS_OPT_USB_STORAGE_DELAY_SCSI_DATA_PHASE "USBStorageDelaySCSIDataPhase"
#define VMK_STRESS_OPT_USB_STORAGE_DELAY_SCSI_TRANSFER "USBStorageDelaySCSITransfer"
/** \endcond nodoc */

/**
 * \brief Opaque stress option handle.
 */
typedef vmk_uint64 vmk_StressOptionHandle;

/*
 ***********************************************************************
 * vmk_StressOptionOpen --                                        */ /**
 *
 * \ingroup Stress
 * \brief Open a handle to stress option
 *
 * \param[in]  name            A stress option name
 * \param[out] handle          Handle to the stress option
 *
 * \retval     VMK_OK          Successful
 * \retval     VMK_BAD_PARAM   The stress option id was invalid
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_StressOptionOpen(
   const char *name, 
   vmk_StressOptionHandle *handle);

/*
 ***********************************************************************
 * vmk_StressOptionClose --                                       */ /**
 *
 * \ingroup Stress
 * \brief Close a handle to stress option
 *
 * \param[in]  handle          Handle to the stress option
 *
 * \retval     VMK_OK          Successful
 * \retval     VMK_BAD_PARAM   The stress option handle was invalid
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_StressOptionClose(
   vmk_StressOptionHandle handle);

/*
 ***********************************************************************
 * vmk_StressOptionValue --                                       */ /**
 *
 * \ingroup Stress
 * \brief Get stress option value
 *
 * \param[in]  handle          Handle to the stress option
 * \param[out] result          Stress option value
 *
 * \retval     VMK_OK          Successful
 * \retval     VMK_BAD_PARAM   The stress option handle was invalid
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_StressOptionValue(
   vmk_StressOptionHandle handle, 
   vmk_uint32 *result);

/*
 ***********************************************************************
 * vmk_StressOptionCounter --                                     */ /**
 *
 * \ingroup Stress
 * \brief Increment stress option counter every Nth (value) 
 *        randomized call if the option is enabled (value > 0)
 *
 * \param[in]  handle          Handle to the stress option
 * \param[out] result          VMK_TRUE if counter was incremented,
 *                             VMK_FALSE otherwise
 *
 * \retval     VMK_OK          Successful
 * \retval     VMK_BAD_PARAM   The stress option handle was invalid
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_StressOptionCounter(
   vmk_StressOptionHandle handle, 
   vmk_Bool *result);

#endif /* _VMKAPI_STRESS_H_ */
/** @} */
