/***************************************************************************
 * Copyright 2004 - 2009 VMware, Inc.  All rights reserved.
 ***************************************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * SCSI Trace APIs                                                */ /**
 * \addtogroup SCSI
 * @{
 *
 * \defgroup SCSItrace SCSI Tracing Interfaces
 * @{
 * This header is exported to user-mode.
 ***********************************************************************
 */

#ifndef _VMKAPI_SCSI_TRACE_H_
#define _VMKAPI_SCSI_TRACE_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "scsi/vmkapi_scsi_types.h"
#include "scsi/vmkapi_scsi_ext.h"

#define VMK_SCSI_TRACE_TIMESTAMP_HZ 1000000

typedef enum {
   VMK_SCSI_TRACE_GET_BPF_VERSION = '0',
   VMK_SCSI_TRACE_SET_PROGRAM,
   VMK_SCSI_TRACE_SET_CAPTURED_DATA_LEN,
   VMK_SCSI_TRACE_READ_PACKET,
   VMK_SCSI_TRACE_GET_STATS,
} vmk_ScsiTraceIoctl;

/**
 * \brief Compact issue side vmk_ScsiCommand for tracing purposes
 */
typedef struct vmk_ScsiTraceIssueCommand {
   /** \brief cdb command descriptor block */
   vmk_uint8			cdb[VMK_SCSI_MAX_CDB_LEN];
   /** \brief size of data buffer */
   vmk_uint32			sgDataLen;
   /** \brief logical unit number */
   vmk_uint32                   lun;
   /** \brief required data xfer length */
   vmk_uint32			requiredDataLen;
   /** \brief direction of data xfer */
   vmk_ScsiCommandDirection	direction;
   /** \brief valid bytes in cdb, must be <= VMK_SCSI_MAX_CDB_LEN */
   vmk_uint8			cdbLen;
   /** \brief reserved */
   vmk_uint8                    reserved[3];
   /** \brief data */
   vmk_uint8			data[0];
} vmk_ScsiTraceIssueCommand;

/**
 * \brief Compact completion side vmk_ScsiCommand for tracing purposes
 */
typedef struct vmk_ScsiTraceCompleteCommand {
   /** \brief size of data buffer */
   vmk_uint32			sgDataLen;
   /** \brief logical unit number */
   vmk_uint32                   lun;
   /** \brief data xfer length */
   vmk_uint32			xferredDataLen;
   /** \brief command completion status */
   vmk_ScsiCmdStatus		status;
   /** \brief direction of data xfer */
   vmk_ScsiCommandDirection	direction;
   /** \brief sense data, only valid on check condition */
   vmk_ScsiSenseData    senseData;
   /** \brief data */
   vmk_uint8			data[0];
} vmk_ScsiTraceCompleteCommand;

/**
 * \brief Compact vmk_ScsiTaskMgmt for tracing purposes.
 */
typedef struct vmk_ScsiTraceTaskMgmt {
   /** \brief type of task management request */
   vmk_ScsiTaskMgmtType		type;
   /** \brief command completion status */
   vmk_ScsiCmdStatus		status;
} vmk_ScsiTraceTaskMgmt;

/**
 * \brief Trace events.
 */
typedef enum {
   VMK_SCSI_TRACE_NONE,
   VMK_SCSI_TRACE_ISSUE_CMD,
   VMK_SCSI_TRACE_COMPLETE_CMD,
   VMK_SCSI_TRACE_START_TASK_MGMT,
   VMK_SCSI_TRACE_END_TASK_MGMT,
} vmk_ScsiTraceEvent;

/**
 * \brief Trace packet
 */
typedef struct vmk_ScsiTracePacket {
   /** \brief event to trace */
   vmk_ScsiTraceEvent   event;
   /** \brief captured length */
   vmk_uint32		capturedLen;
   /** \brief on the wire length */
   vmk_uint32		wireLen;
   /** \brief worldid */
   vmk_uint32		worldId;
   /** \brief destinition id */
   vmk_uint64		destId;
   /** \brief task id */
   vmk_uint64		taskId;
   /** \brief timestamp */
   vmk_uint64		timestamp;
   /** \brief originator's handle */
   vmk_uint64		originHandle;
   /** \brief original serial number */
   vmk_uint64		originSN;
} vmk_ScsiTracePacket;

/**
 * \brief trace set program
 */
typedef struct vmk_ScsiTraceSetProgram {
   /** \brief program to set */
   void		*program;

   /** \brief program length */
   vmk_uint32	programLen;
} vmk_ScsiTraceSetProgram;

/**
 * \brief trace packet read
 */
typedef struct vmk_ScsiTraceReadPacket {
   /** \brief vmk_ScsiTracePacket * */
   vmk_uint64           packetAddr;
   /** \brief buffer length */
   vmk_uint32		bufferLen;
   /** \brief header length */
   vmk_uint32		headerLen;
} vmk_ScsiTraceReadPacket;

/**
 * \brief trace stats
 */
typedef struct vmk_ScsiTraceStats {
   /** \brief Total number of packets traced in (including dropped) */
   vmk_uint64		tracedIn;
   /** \brief Total number of packets filtered out */
   vmk_uint64		tracedOut;
   /** \brief Number of packets dropped due to lack of buffer space */
   vmk_uint64		dropped;
} vmk_ScsiTraceStats;

/**
 * \brief trace ioctl
 */
typedef struct vmk_ScsiTraceIoctlData {
   /** \brief private ioctl */
   vmk_uint32 privIoctl;

   /** \brief private ioctl data */
   vmk_uint64 data;
} /** \cond never */ __attribute__((packed, aligned(4))) /** \endcond */
  vmk_ScsiTraceIoctlData;

#endif /* _VMKAPI_SCSI_TRACE_H_ */
/** @} */
/** @} */
