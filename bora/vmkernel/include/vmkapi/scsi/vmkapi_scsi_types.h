/***************************************************************************
 * Copyright 2004 - 2009 VMware, Inc.  All rights reserved.
 ***************************************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * SCSI-specific types                                            */ /**
 * \addtogroup SCSI
 * @{
 *
 * \defgroup SCSItypes SCSI Types
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_SCSI_TYPES_H_
#define _VMKAPI_SCSI_TYPES_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_lock.h"
#include "base/vmkapi_const.h"
#include "base/vmkapi_compiler.h"
#include "scsi/vmkapi_scsi_ext.h"
#include "scsi/vmkapi_scsi_mgmt_types.h"

#define VMK_SECTOR_SIZE 512

/**  
 * \brief sense data structure maintained in each SCSI device
 * SPC 3 r23, Section 4.5.3 table 26
 *
 * NB: The "valid" bit in the data structure does NOT tell whether the sense is
 * actually valid and thus the name is really badly chosen (even though it is
 * the official name from the SCSI II specification).  The SCSI II spec.
 * states "A valid bit of zero indicates that the information field is not as
 * defined in this International Standard".  we have seen that many tape drives
 * are capable of returning sense without this bit set
 */
typedef struct vmk_ScsiSenseData {
   /** \brief error type and format of sense data (see SPC 3 r23, Sec. 4.5.1) */
   vmk_uint8 error         :7,
/* \brief sense data is for "current command" */
#define VMK_SCSI_SENSE_ERROR_CURCMD  0x70
/* \brief sense data is for an earlier command */
#define VMK_SCSI_SENSE_ERROR_PREVCMD 0x71
   /** \brief set to one indicates the 'info' field is valid */
             valid         :1;
   /** \brief obsolete */
   vmk_uint8 segment;
   /** \brief generic information describing the error or exception condition */
   vmk_uint8 key           :4,
                           :1,
   /** \brief see above spc section */
             ili           :1,
   /** \brief see above spc section */
             eom           :1,
   /** \brief see above spc section */
             filmrk        :1;
   /** \brief see above spc section */
   vmk_uint8 info[4];
   /** \brief see above spc section */
   vmk_uint8 optLen;
   /** \brief see above spc section */
   vmk_uint8 cmdInfo[4];
   /** \brief further information about to the condition reported in 'key' */
   vmk_uint8 asc;
   /** \brief detailed information about to the additional sense code in 'asc' */
   vmk_uint8 ascq;
   /** \brief Field Replacable Unit code (see SPC 3 r23, 4.5.2.5) */
   vmk_uint8 fru;
   /** \brief Sense-key specific fields (see SPC 3 r23, 4.5.2.4) */
   vmk_uint8 bitpos        :3,
             bpv           :1,
                           :2,
             cd            :1,
   /** \brief Set to indicate sense-key specific fields are valid */
             sksv          :1;
   /** \brief field indicates which byte of the CDB or param data was in error */
   vmk_uint16 epos;
   
   /*
    * \brief may contain vendor specific data further defining exception cond.
    *
    * Some vendors want to return additional data which
    * requires a sense buffer of up to 64 bytes.
    *
    * See SPC 3 r23, Section 4.5
    */
   vmk_uint8 additional[46];
} VMK_ATTRIBUTE_PACKED vmk_ScsiSenseData;

/*
 * Commands
 */

/**
 * \brief SCSI command ID
 */

typedef struct vmk_ScsiCommandId {
   /** \brief unique token of originator of cmd */
   void                        *initiator;
   /** \brief unique serial of cmd */
   vmk_uint64                serialNumber;
} vmk_ScsiCommandId;

struct vmk_ScsiCommand;

typedef void (*vmk_ScsiCommandDoneCbk)(struct vmk_ScsiCommand *cmd);

/**
 * \brief Flag definitions for vmk_ScsiCommand.flags field.
 *
 * \cond nodoc
 * Be sure to modify VMK_SCSI_COMMAND_FLAGS_REISSUE_SCRUB_MASK if a newly
 * added flag should be cleared if caller chooses to re-issue the command
 * using the same command structure.
 * \endcond
 *
 * NOTES:
 *     - If a command has the VMK_SCSI_COMMAND_FLAGS_RESERVATION_SENSITIVE
 *       flag set, the plugin should _not_ retry the IO upon failure.
 *     - If you add a new flag that shouldn't be carried across command
 *       retries, make sure you add it to 
 *       VMK_SCSI_COMMAND_FLAGS_REISSUE_SCRUB_MASK.
 *
 *  ISSUE_WITH_ORDERED_TAG, instructs the lower level drivers to issue this
 *                          command with an ORDERED tag if tagged queuing
 *                          is being used - if tagged queuing is not in
 *                          effect for the target it can be ignored
 *  VPORT, command being issued is destined to NPIV virtual port
 *  NEEDS_COMPLETION, used internally by the framework's ScsiDevice layer
 *  USE_PRIORITY_QUEUE, used internally by the framework's ScsiDevice layer.
 *  NO_CONNECT_IF_APD, instructs an MP Plugin to fail the I/O with a
 *                     VMK_SCSI_HOST_NO_CONNECT host status if it finds
 *                     that a vmk_ScsiDevice is in an all-paths-dead
 *                     condition.  Normally an MP Plugin should keep
 *                     retrying the I/O until a path becomes available or
 *                     it recieves a task mamagement abort or reset
 *                     request.
 *  VM_CLUSTER, command issued by a VM that is a guest cluster node
 *              Cluster may use SCSI reservation as a mechanism to determine
 *              node membership. As a result, VM cluster is particularly
 *              sensitive to RESERVATION CONFLICT command status. The MPP
 *              must avoid issuing VM cluster generated IO over an active
 *              path that doesn't hold reservation. Doing that would create
 *              an artificial reservation conflict that would interfere with
 *              the way VM cluster works.
 * SKIPRETRY [set internally] indicates the command was aborted as part of a
 *       SCSI_DeviceVirtReset call and so should not be retried, even if
 *       the command timeout has not yet expired.
 */
typedef enum {
   VMK_SCSI_COMMAND_FLAGS_ISSUE_WITH_ORDERED_TAG  = 0x00010000,
   VMK_SCSI_COMMAND_FLAGS_RESERVATION_SENSITIVE   = 0x00008000,
   VMK_SCSI_COMMAND_FLAGS_VPORT                   = 0x00004000,
   VMK_SCSI_COMMAND_FLAGS_SKIPRETRY               = 0x00002000,
   VMK_SCSI_COMMAND_FLAGS_USE_PRIORITY_QUEUE      = 0x00001000,
   VMK_SCSI_COMMAND_FLAGS_NO_CONNECT_IF_APD       = 0x00000800,
   VMK_SCSI_COMMAND_FLAGS_VM_CLUSTER              = 0x00000400,
} /** \cond nodoc */ VMK_ATTRIBUTE_PACKED VMK_ATTRIBUTE_ALIGN(1) /* \endcond*/
vmk_ScsiCommandFlags;

#define VMK_SCSI_COMMAND_FLAGS_REISSUE_SCRUB_MASK \
   (VMK_SCSI_COMMAND_FLAGS_SKIPRETRY)

#define VMK_SCSI_MAX_CDB_LEN        16
#define VMK_SCSI_MAX_SENSE_DATA_LEN 64 

/**
 * \brief SCSI command structure
 */
typedef struct vmk_ScsiCommand {
   /** \brief command completion callback for async i/o */
   vmk_ScsiCommandDoneCbk       done;
   /** \brief command completion callback argument for async i/o */
   void                         *doneData;
   /** \brief scatter/gather list pointing to i/o's data buffer */
   vmk_SgArray                  *sgArray;
   /** \brief hint indicating the direction of data transfer */
   vmk_ScsiCommandDirection     dataDirection;
   /** \brief command flags, see above */
   vmk_uint32                   flags;
   /** \brief hint indicating whether a cmd is a read i/o */
   vmk_Bool                     isReadCdb;
   /** \brief hint indicating whether a cmd is a write i/o */
   vmk_Bool                     isWriteCdb;
   /** \brief reserved */
   vmk_uint8                    reserved1[2];
   /** \brief minimum data transfer length. must be <= vmk_GetSgDataLen(sgArray)
    */
   vmk_uint32                   requiredDataLen;
   /** \brief worldId of the world on behalf of whom cmd is issued */
   vmk_uint32                   worldId;
   /**
    * \brief number of blocks to be xfer'd
    *
    *   only valid for isReadCdb || isWriteCdb
    */
   vmk_uint32                   lbc;
   /**
    * \brief address of first block to be xfer'd
    *
    *   only valid for isReadCdb || isWriteCdb
    */
   vmk_uint64                   lba;
   /**
    * \brief # of Msecs since system boot before cmd is timed out
    *
    *   0 indicates the cmd will not be timed out. nonzero indicates the system
    *   time beyond which the PSA framework will issue an abort task management
    *   request and fail the i/o with a timeout status
    */
   vmk_uint64                   absTimeoutMs;
   /** \brief unique tag for this command */
   vmk_ScsiCommandId            cmdId;
   /** \brief SCSI command descriptor block */
   vmk_uint8                    cdb[VMK_SCSI_MAX_CDB_LEN];
   /** \brief # valid bytes in cdb, must be <= VMK_SCSI_MAX_CDB_LEN */
   vmk_uint32                   cdbLen;
   /** \brief command completion status */
   vmk_ScsiCmdStatus            status;
   /** \brief # of bytes xfer'd to/from data buffer */
   vmk_uint32                   bytesXferred;
   /** \brief reserved */
   vmk_uint32                   reserved2[3];
   /** \brief SCSI sense, only valid if vmk_ScsiCmdStatusIsCheck(cmd->status) */
   vmk_ScsiSenseData            senseData;
   /** \brief reserved */
   vmk_VirtAddr                 reserved3[2];
   /** \brief command completion status */
} vmk_ScsiCommand;

/**
 * \brief SCSI task management action
 */
typedef enum { 
   VMK_SCSI_TASKMGMT_ACTION_IGNORE          = 0,
   VMK_SCSI_TASKMGMT_ACTION_ABORT           = 1,
} vmk_ScsiTaskMgmtAction;

/**
 * \brief Used to abort all commands, regardless of initiator and s/n
 */
#define VMK_SCSI_TASKMGMT_ANY_INITIATOR (void*)0xA11

/**
 * \brief SCSI task management
 */
typedef struct vmk_ScsiTaskMgmt {
   /** \brief magic */
   vmk_uint32               magic;
   /** \brief task mgmt type */
   vmk_ScsiTaskMgmtType     type;
   /** \brief command status */
   vmk_ScsiCmdStatus        status;
   /** \brief cmdId of the cmd to abort.  cmdId.serialNumber is only
    * applicable for VMK_SCSI_TASKMGMT_ABORT and ignored for all
    * other vmk_ScsiTaskMgmtType values.  cmdId.initiator is only
    * applicable for VMK_SCSI_TASKMGMT_ABORT and
    * VMK_SCSI_TASKMGMT_VIRT_RESET, ignored for all other
    * vmk_ScsiTaskMgmtType values
    */
   vmk_ScsiCommandId        cmdId;
   /** \brief worldId of the cmd(s) to abort.  only applicable for
    * VMK_SCSI_TASKMGMT_ABORT and VMK_SCSI_TASKMGMT_VIRT_RESET, ignored
    * for all other vmk_ScsiTaskMgmtType values
    */
   vmk_uint32               worldId;
} vmk_ScsiTaskMgmt;

/*
 * SCSI Adapter
 */

typedef enum vmk_ScanAction {
   VMK_SCSI_SCAN_CREATE_PATH,
   VMK_SCSI_SCAN_CONFIGURE_PATH,
   VMK_SCSI_SCAN_DESTROY_PATH,
} vmk_ScanAction;

/* Maximum length of name fields (including 1 byte for the terminating NUL) */
#define VMK_SCSI_ADAPTER_NAME_LENGTH 32
#define VMK_SCSI_DRIVER_NAME_LENGTH 32
#define VMK_SCSI_ADAPTER_PROC_NAME_LENGTH 32
#define VMK_SCSI_TRANSPORT_NAME_LENGTH 32

/**
 * \brief bitmasks for adapter events
 *
 * Events are bit fields because users can
 * wait for multiple events
 *
 */
typedef enum vmk_ScsiAdapterEvents {
   VMK_SCSI_ADAPTER_EVENT_FC_LOOP_UP                = 0x00000001,
   VMK_SCSI_ADAPTER_EVENT_FC_LOOP_DOWN              = 0x00000002,
   VMK_SCSI_ADAPTER_EVENT_FC_RSCN                   = 0x00000004,
   VMK_SCSI_ADAPTER_EVENT_FC_NEW_TARGET             = 0x00000008,
   VMK_SCSI_ADAPTER_EVENT_FC_REMOVED_TARGET         = 0x00000010,
   VMK_SCSI_ADAPTER_EVENT_FC_NEW_VPORT              = 0x00000020,
   VMK_SCSI_ADAPTER_EVENT_FC_REMOVED_VPORT          = 0x00000040,
   VMK_SCSI_ADAPTER_EVENT_HOT_PLUG_ADD              = 0x00000080,
   VMK_SCSI_ADAPTER_EVENT_HOT_PLUG_REMOVE           = 0x00000100,
   VMK_SCSI_ADAPTER_EVENT_FC_TARGET_SCAN_DONE       = 0x00000200,
} vmk_ScsiAdapterEvents;

   /** \brief Event handler callback entry */
typedef void (*vmk_EventHandlerCbk) (void *clientData, vmk_uint32 eventType);

/**
 * \brief bitmasks for flags field of SCSI adapter
 *
 * Through this flags, give vmkernel information how handle the adapter.
 *
 */
typedef enum vmk_ScsiAdapterFlags {
   VMK_SCSI_ADAPTER_FLAG_BLOCK                   = 0x00000001,
   /** This adapter is an NPIV VPORT */
   VMK_SCSI_ADAPTER_FLAG_NPIV_VPORT              = 0x00000002,
   /** Do not scan when registering this adapter */
   VMK_SCSI_ADAPTER_FLAG_REGISTER_WITHOUT_SCAN   = 0x00000004,
   /** Do not scan periodically */
   VMK_SCSI_ADAPTER_FLAG_NO_PERIODIC_SCAN        = 0x00000008,
   /** Ok to probe periodically, but no adapter rescan */
   VMK_SCSI_ADAPTER_FLAG_NO_ADAPTER_RESCAN       = 0x00000010,
   /** This adapter is a Legacy NPIV VPORT */
   VMK_SCSI_ADAPTER_FLAG_NPIV_LEGACY_VPORT       = 0x00000020,
} vmk_ScsiAdapterFlags;

/**
 * \brief main data structure for SCSI adapter
 *
 * The discover() entrypoint passes back a deviceData.
 * This deviceData must be able to completely identify
 * the channel:target:lun of the device as it's the only
 * thing that will be passed back once it's set by the
 * discover() entrypoint.
 */
typedef struct vmk_ScsiAdapter {
   /** \brief max # of scatter/gather entries */
   int                        sgSize;
   /** \brief required scatter/gather element alignment */
   int                  sgElemAlignment;
   /** \brief if non-zero, scatter/gather element length multiple */
   int                  sgElemSizeMult;
   /** \brief max # of blocks per i/o */
   int                        hostMaxSectors;
   /** \brief adapter's ->can_queue entrypoint */
   vmk_uint32           *qDepthPtr;
   /** \brief adapter creator's private convenience scratch data ptr */
   void                 *clientData;
   /** \brief adapter support physical address extensions? */
   vmk_Bool             paeCapable;
   /** \brief reserved */
   vmk_uint8            reserved1[1];
   /** \brief Unique adapter number. Set by vmk_ScsiAllocateAdapter. */
   vmk_uint16           hostNum;
   /** \brief SCSI target id of adapter, or -1 if n/a */
   int                        targetId;
   /** \brief Issue a SCSI command to the specified device */
   VMK_ReturnStatus (*command)(
      void *clientData,
      struct vmk_ScsiCommand *cmd,
      void *deviceData);
   /** \brief Issue a SCSI task management */
   VMK_ReturnStatus (*taskMgmt)(
      void *clientData,
      struct vmk_ScsiTaskMgmt *taskMgmt,
      void *deviceData);
   /** \brief Issue a SCSI command during a core dump */
   VMK_ReturnStatus (*dumpCommand)(
      void *clientData,
      vmk_ScsiCommand *cmd,
      void *deviceData);
   /** \brief Destroy the adapter */
   void (*close)(void *clientData);
   /** \brief generate the adapter's proc node information */ 
   VMK_ReturnStatus (*procInfo)(
      void* clientData,
      char* buf,
      vmk_uint32 offset,
      vmk_uint32 count,
      vmk_uint32* nbytes,
      int isWrite);
   /** \brief Log the current adapter queue */
   void (*dumpQueue)(void *clientData);
   /** \brief Run the adapter's BH, called on the dump device during a PSOD
    *  Interrupts are disabled and BHs aren't running.
    */
   void (*dumpBHHandler)(void *clientData);
   /** \brief arg to dumpQueue() */
   void *dumpBHHandlerData;
   /** \brief driver specific ioctl */
   VMK_ReturnStatus (*ioctl)(
      void *clientData,
      void *deviceData,
      vmk_uint32 fileFlags,
      vmk_uint32 cmd,
      vmk_VirtAddr userArgsPtr, 
      vmk_IoctlCallerSize callerSize,
      vmk_int32 *drvErr);
   /** \brief discover & destroy a device */
   VMK_ReturnStatus (*discover)(
      void *clientData,
      vmk_ScanAction action,
      int channel,
      int targetId,
      int lunId,
      void **deviceData);
   /** \brief send NPIV specific commands to a device */
   VMK_ReturnStatus (*vportop)(
      void *handle,
      vmk_uint32 cmd,
      void *arg,
      vmk_int32 *drvErr);
   /** \brief scan a single LUN on a vport */
   VMK_ReturnStatus (*vportDiscover)(
      void *clientData,
      vmk_ScanAction action,
      int ctlr,
      int devNum,
      int lun,
      struct vmk_ScsiAdapter **vmkAdapter,
      void **deviceData);
   /** \brief try changing the path queue depth */
   int (*modifyDeviceQueueDepth)(
      void *clientData,
      int qDepth,
      void *deviceData);
   /** \brief query the path queue depth */
   int (*queryDeviceQueueDepth)(
      void *clientData,
      void *deviceData);
   /** \brief checks if a target exists */
   VMK_ReturnStatus (*checkTarget)(
      void *clientData,
      int channel,
      int targetId);
   /** \brief adapter flags, see vmk_ScsiAdapterFlags above */
   vmk_uint32                flags;
   /** \brief block data */
   int                        blockData;
   /** \brief id of module running this adapter */
   vmk_ModuleID                moduleID;
   /** \brief # of channels */
   int                        channels;
   /** \brief # of targets per channel */
   int                        maxTargets;
   /** \brief max # of logical units per target */
   int                        maxLUNs;
   /** \brief pci bus # or 0xffff if n/a */
   vmk_uint16           bus;
   /** \brief pci devfn (slot and function num) or 0xffff if n/a */
   vmk_uint16           devfn;
   /** \brief reserved */
   vmk_uint8            reserved2[4];
   /** \brief adapter name */
   char                 name[VMK_SCSI_ADAPTER_NAME_LENGTH];
   /** \brief block device name
    *    only valid for mgmtAdapter.transport == VMK_STORAGE_ADAPTER_BLOCK
    */
   char                        blockDevName[VMK_SCSI_ADAPTER_NAME_LENGTH];
   /** \brief driver name */
   char                 driverName[VMK_SCSI_DRIVER_NAME_LENGTH];
   /** \brief /proc directory */
   char                        procName[VMK_SCSI_ADAPTER_PROC_NAME_LENGTH];
   /** \brief reserved */
   vmk_uint32           reserved3[4];
   /** \brief reserved */
   vmk_VirtAddr         reserved4[4];
   /** \brief transport specific mgmtAdapter hooks */
   vmk_SCSITransportMgmt mgmtAdapter;
} vmk_ScsiAdapter; 

/*
 * Physical Path
 */

#define VMK_SCSI_PATH_ANY_ADAPTER        "*"
#define VMK_SCSI_PATH_ANY_CHANNEL        ~0
#define VMK_SCSI_PATH_ANY_TARGET        ~0
#define VMK_SCSI_PATH_ANY_LUN                ~0

typedef enum {
   VMK_SCSI_PLUGIN_STATE_ENABLED,
   VMK_SCSI_PLUGIN_STATE_DISABLING,
   VMK_SCSI_PLUGIN_STATE_DISABLED,
   VMK_SCSI_PLUGIN_STATE_CLAIM_PATHS,
} vmk_ScsiPluginState;

/**
 * \brief Format of INQUIRY request block.
 * SPC 3 r23, Section 6.4.1 table 80
 */
typedef struct vmk_ScsiInquiryCmd {
   /** \brief see above spc section */
   vmk_uint8 opcode;
   /** \brief see above spc section */
   vmk_uint8 evpd  :1,
   /** \brief see above spc section */
         cmddt :1,
   /** \brief see above spc section */
         resv12:3,
   /** \brief see above spc section */
         lun   :3;
   /** \brief see above spc section */
   vmk_uint8 pagecode;
   /** \brief see above spc section */
   vmk_uint8 reserved;
   /** \brief see above spc section */
   vmk_uint8 len;
   /** \brief see above spc section */
   vmk_uint8 ctrl;
} vmk_ScsiInquiryCmd;

/**
 * \brief Format of INQUIRY response block.
 * SPC 3 r23, Section 6.4.2 table 81
 */
typedef struct vmk_SCSIInquiryResponse {
   /** \brief see above spc section */
   vmk_uint8 devclass    :5,
#define VMK_SCSI_CLASS_DISK       0x00        // disk drive
#define VMK_SCSI_CLASS_TAPE       0x01        // tape drive
#define VMK_SCSI_CLASS_PRINTER    0x02        // printer
#define VMK_SCSI_CLASS_CPU        0x03        // processor device
#define VMK_SCSI_CLASS_WORM       0x04        // WORM drive
#define VMK_SCSI_CLASS_CDROM      0x05        // CD-ROM drive
#define VMK_SCSI_CLASS_SCANNER    0x06        // scanner
#define VMK_SCSI_CLASS_OPTICAL    0x07        // optical disk
#define VMK_SCSI_CLASS_MEDIA      0x08        // media changer
#define VMK_SCSI_CLASS_COM        0x09        // communication device
#define VMK_IDE_CLASS_CDROM       0x0a        // IDE CD-ROM drive
#define VMK_IDE_CLASS_OTHER       0x0b        // Generic IDE
#define VMK_SCSI_CLASS_RAID       0x0c        // RAID controller (SCSI-3, reserved in SCSI-2)
#define VMK_SCSI_CLASS_SES        0x0d        // SCSI Enclosure Services device (t10 SES)
#define VMK_SCSI_CLASS_UNKNOWN    0x1f        // unknown device
   /** \brief see above spc section */
         pqual             :3;
#define VMK_SCSI_PQUAL_CONNECTED     0  // device described is connected to the LUN
#define VMK_SCSI_PQUAL_NOTCONNECTED  1  // target supports such a device, but none is connected
#define VMK_SCSI_PQUAL_NODEVICE      3  // target does not support a physical device for this LUN
   /** \brief see above spc section */
   vmk_uint8    :7,
   /** \brief see above spc section */
         rmb:1;
   /** \brief see above spc section */
   vmk_uint8 ansi             :3,
#define VMK_SCSI_ANSI_SCSI1      0x0   // device supports SCSI-1
#define VMK_SCSI_ANSI_CCS        0x1   // device supports the CCS
#define VMK_SCSI_ANSI_SCSI2      0x2   // device supports SCSI-2
#define VMK_SCSI_ANSI_SCSI3_SPC  0x3   // device supports SCSI-3 version SPC
#define VMK_SCSI_ANSI_SCSI3_SPC2 0x4   // device supports SCSI-3 version SPC-2
#define VMK_SCSI_ANSI_SCSI3_SPC3 0x5   // device supports SCSI-3 version SPC-3
#define VMK_SCSI_ANSI_SCSI3_SPC4 0x6   // device supports SCSI-3 version SPC-4
   /** \brief see above spc section */
         ecma             :3,
   /** \brief see above spc section */
         iso             :2;
   /** \brief see above spc section */
   vmk_uint8 dataformat  :4,
   /** \brief see above spc section */
                     :1,
   /** \brief see above spc section */
         naca             :1,
   /** \brief see above spc section */
         tio             :1,
   /** \brief see above spc section */
         aen             :1;
   /** \brief see above spc section */
   vmk_uint8 optlen;
   /** \brief see above spc section */
   vmk_uint8         protect   :1,
   /** \brief see above spc section */
         rsrv              :2,
   /** \brief see above spc section */
          tpcp              :1,
   /** \brief see above spc section */
         tpgs        :2,
   /** \brief see above spc section */
         acc             :1,
   /** \brief see above spc section */
         sccs             :1;
#define VMK_SCSI_TPGS_NONE                       0x0
#define VMK_SCSI_TPGS_IMPLICIT_ONLY              0x1
#define VMK_SCSI_TPGS_IMPLICIT                   VMK_SCSI_TPGS_IMPLICIT_ONLY 
#define VMK_SCSI_TPGS_EXPLICIT_ONLY              0x2
#define VMK_SCSI_TPGS_EXPLICIT                   VMK_SCSI_TPGS_EXPLICIT_ONLY
#define VMK_SCSI_TPGS_BOTH_IMPLICIT_AND_EXPLICIT 0x3
#define VMK_SCSI_TPGS_BOTH                       VMK_SCSI_TPGS_BOTH_IMPLICIT_AND_EXPLICIT
   /** \brief see above spc section */
   vmk_uint8 adr16             :1,
   /** \brief see above spc section */
         adr32             :1,
   /** \brief see above spc section */
         arq             :1,
   /** \brief see above spc section */
         mchngr             :1,
   /** \brief see above spc section */
         dualp             :1,
   /** \brief see above spc section */
         port             :1,
   /** \brief see above spc section */
                     :2;
   /** \brief see above spc section */
   vmk_uint8 sftr             :1,
   /** \brief see above spc section */
         que             :1,
   /** \brief see above spc section */
         trndis             :1,
   /** \brief see above spc section */
         link             :1,
   /** \brief see above spc section */
         sync             :1,
   /** \brief see above spc section */
         w16             :1,
   /** \brief see above spc section */
         w32             :1,
   /** \brief see above spc section */
         rel             :1;
   /** \brief see above spc section */
   vmk_uint8 manufacturer[8];
   /** \brief see above spc section */
   vmk_uint8 product[16];
   /** \brief see above spc section */
   vmk_uint8 revision[4];
   /** \brief see above spc section */
   vmk_uint8 vendor1[20];
   /** \brief see above spc section */
   vmk_uint8 reserved[40];
} vmk_ScsiInquiryResponse;

#define VMK_SCSI_STANDARD_INQUIRY_MIN_LENGTH 36

/**
 * \brief Format of INQUIRY EVPD responses
 * SPC 3 r23, Section 7.6
 */
typedef struct vmk_ScsiInquiryVPDResponse {
   /** \brief see above spc section */
   vmk_uint8 devclass    :5,
   /** \brief see above spc section */
             pqual       :3;
   /** \brief see above spc section */
   vmk_uint8 pageCode;
   /** \brief see above spc section */
   vmk_uint8 reserved;
   /** \brief see above spc section */
   vmk_uint8 payloadLen;
   /** \brief see above spc section */
   vmk_uint8 payload[0];
} VMK_ATTRIBUTE_PACKED VMK_ATTRIBUTE_ALIGN(1) vmk_ScsiInquiryVPDResponse;

/*
 * \brief Format of INQUIRY EVPD Page 83 response
 *  SPC 3 r23, Section 7.6.3.1 table 294
 */
typedef struct vmk_ScsiInquiryVPD83Response {
   /** \brief see above spc section */
   vmk_uint8 devclass    :5,
   /** \brief see above spc section */
             pqual       :3;
   /** \brief see above spc section */
   vmk_uint8 pageCode;
   /** \brief see above spc section */
   vmk_uint16 payloadLen;
   /** \brief see above spc section */
   vmk_uint8 payload[0];
} VMK_ATTRIBUTE_PACKED VMK_ATTRIBUTE_ALIGN(1) vmk_ScsiInquiryVPD83Response;

/*
 * \brief Format of INQUIRY EVPD Page 83 response id descriptor
 *  SPC 3 r23, Section 7.6.3.1 table 295
 */
typedef struct vmk_ScsiInquiryVPD83IdDesc {
   /** \brief see above spc section */
   vmk_uint8 codeSet     :4,
   /** \brief see above spc section */
             protocolId  :4;
#define VMK_SCSI_EVPD83_ID_VENDOR        (0x0)
#define VMK_SCSI_EVPD83_ID_T10           (0x1)
#define VMK_SCSI_EVPD83_ID_EUI           (0x2)
#define VMK_SCSI_EVPD83_ID_NAA           (0x3)
#define VMK_SCSI_EVPD83_ID_RTP           (0x4)
#define VMK_SCSI_EVPD83_ID_TPG           (0x5)
#define VMK_SCSI_EVPD83_ID_LUG           (0x6)
#define VMK_SCSI_EVPD83_ID_MD5           (0x7)
#define VMK_SCSI_EVPD83_ID_SCSINAME      (0x8)
   /** \brief see above spc section */
   vmk_uint8 idType      :4,
#define VMK_SCSI_EVPD83_ASSOCIATION_LU   (0x0)
#define VMK_SCSI_EVPD83_ASSOCIATION_PORT (0x1)
#define VMK_SCSI_EVPD83_ASSOCIATION_DEV  (0x2)
   /** \brief see above spc section */
             association :2,
   /** \brief see above spc section */
             reserved1   :1,
   /** \brief see above spc section */
             piv         :1;
   /** \brief see above spc section */
   vmk_uint8 reserved2;
   /** \brief see above spc section */
   vmk_uint8 idLen;
   /** \brief see above spc section */
   vmk_uint8 id[0];
} VMK_ATTRIBUTE_PACKED VMK_ATTRIBUTE_ALIGN(1) vmk_ScsiInquiryVPD83IdDesc;

#define VMK_SCSI_EVPD83_NAA_EXTENDED     (0x2)
#define VMK_SCSI_EVPD83_NAA_REGISTERED   (0x5)
#define VMK_SCSI_EVPD83_NAA_REG_EXT      (0x6)

/**
 * \brief Format of the 6 byte version of MODE SELECT
 * SPC 3 r23, Section 6.9.1 table 97
 */
typedef struct vmk_ScsiModeSenseCmd {
   /** \brief see above spc section */
   vmk_uint8    opcode;
   /** \brief see above spc section */
   vmk_uint8          :3,
   /** \brief see above spc section */
            dbd          :1,
                  :1,
   /** \brief see above spc section */
            lun          :3;
   /** \brief see above spc section */
   vmk_uint8    page          :6,
#define VMK_SCSI_MS_PAGE_VENDOR   0x00     // vendor-specific (ALL)
#define VMK_SCSI_MS_PAGE_RWERROR  0x01     // read/write error (DISK/TAPE/CDROM/OPTICAL)
#define VMK_SCSI_MS_PAGE_CONNECT  0x02     // disconnect/connect (ALL)
#define VMK_SCSI_MS_PAGE_FORMAT   0x03     // format (DISK)
#define VMK_SCSI_MS_PAGE_PARALLEL 0x03     // parallel interface (PRINTER)
#define VMK_SCSI_MS_PAGE_UNITS    0x03     // measurement units (SCANNER)
#define VMK_SCSI_MS_PAGE_GEOMETRY 0x04     // rigid disk geometry (DISK)
#define VMK_SCSI_MS_PAGE_SERIAL   0x04     // serial interface (PRINTER)
#define VMK_SCSI_MS_PAGE_FLEXIBLE 0x05     // flexible disk geometry (DISK)
#define VMK_SCSI_MS_PAGE_PRINTER  0x05     // printer operations (PRINTER)
#define VMK_SCSI_MS_PAGE_OPTICAL  0x06     // optical memory (OPTICAL)
#define VMK_SCSI_MS_PAGE_VERIFY   0x07     // verification error (DISK/CDROM/OPTICAL)
#define VMK_SCSI_MS_PAGE_CACHE    0x08     // cache (DISK/CDROM/OPTICAL)
#define VMK_SCSI_MS_PAGE_PERIPH   0x09     // peripheral device (ALL)
#define VMK_SCSI_MS_PAGE_CONTROL  0x0a     // control mode (ALL)
#define VMK_SCSI_MS_PAGE_MEDIUM   0x0b     // medium type (DISK/CDROM/OPTICAL)
#define VMK_SCSI_MS_PAGE_NOTCH    0x0c     // notch partitions (DISK)
#define VMK_SCSI_MS_PAGE_CDROM    0x0d     // CD-ROM (CDROM)
#define VMK_SCSI_MS_PAGE_CDAUDIO  0x0e     // CD-ROM audio (CDROM)
#define VMK_SCSI_MS_PAGE_COMPRESS 0x0f     // data compression (TAPE)
#define VMK_SCSI_MS_PAGE_CONFIG   0x10     // device configuration (TAPE)
#define VMK_SCSI_MS_PAGE_EXCEPT   0x1c     // informal exception (ALL:SCSI-3)
#define VMK_SCSI_MS_PAGE_CDCAPS   0x2a     // CD-ROM capabilities and mechanical status (CDROM)
// more defined...
#define VMK_SCSI_MS_PAGE_ALL      0x3f     // all available pages (ALL)
   /** \brief see above spc section */
            pcf          :2;
#define VMK_SCSI_MS_PCF_CURRENT   0x00     // current values
#define VMK_SCSI_MS_PCF_VOLATILE  0x01     // changeable values
#define VMK_SCSI_MS_PCF_DEFAULT   0x02     // default values
#define VMK_SCSI_MS_PCF_SAVED     0x03     // saved values
   /** \brief see above spc section */
   vmk_uint8    subpage;
   /** \brief see above spc section */
   vmk_uint8    length;
   /** \brief see above spc section */
   vmk_uint8    ctrl;
} vmk_ScsiModeSenseCmd;

/**
 * \brief Format of READ_CAPACITY(10) response block.
 * SBC 2 r16, section 5.10.1 table 34
 */
typedef struct {
   /** \brief see above sbc section */
   vmk_uint32 lbn;
   /** \brief see above sbc section */
   vmk_uint32 blocksize;
} vmk_ScsiReadCapacityResponse;

/*
 * Format of REPORT TARGET PORT GROUPS request and response blocks.
 */


/**
 * \brief target port group command for SCSI report
 * SPC 3 r23, section 6.25 table 162
 */
typedef struct vmk_ScsiReportTargetPortGroupsCmd {
   /** \brief see above spc section */
   vmk_uint8    opcode;
   /** \brief see above spc section */
   vmk_uint8    svc:5,
#define VMK_SCSI_RTPGC_SVC    0xA
   /** \brief see above spc section */
                res1:3;
   /** \brief see above spc section */
   vmk_uint8    reserved1[4];
   /** \brief see above spc section */
   vmk_uint32   length;
   /** \brief see above spc section */
   vmk_uint8    reserved2;
   /** \brief see above spc section */
   vmk_uint8    ctrl;
} VMK_ATTRIBUTE_PACKED vmk_ScsiReportTargetPortGroupsCmd;

/**
 * \brief target port group response for SCSI report
 * SPC 3 r23, section 6.25 table 163
 */
typedef struct vmk_ScsiReportTargetPortGroupsResponse {
   /** \brief see above spc section */
   vmk_uint32   length;
} VMK_ATTRIBUTE_PACKED vmk_ScsiReportTargetPortGroupsResponse;

/**
 * \brief target port group descriptor for SCSI report
 * SPC 3 r23, section 6.25 table 164
 */
typedef struct  vmk_ScsiReportTargetPortGroupDescriptor {
   /** \brief see above spc section */
   vmk_uint8   aas:4,
   /** \brief see above spc section */
               res1:3,
   /** \brief see above spc section */
               pref:1;
#define VMK_SCSI_TPGD_AAS_AO      0x00 //!< active/optimized
#define VMK_SCSI_TPGD_AAS_ANO     0x01 //!< active/nonoptomized
#define VMK_SCSI_TPGD_AAS_STBY    0x02 //!< standby
#define VMK_SCSI_TPGD_AAS_UNAVAIL 0x03 //!< unavailable
#define VMK_SCSI_TPGD_AAS_TRANS   0x0F //!< transitioning
   /** \brief see above spc section */
   vmk_uint8   aosup:1,
   /** \brief see above spc section */
               ansup:1,
   /** \brief see above spc section */
               ssup:1,
   /** \brief see above spc section */
               usup:1,
   /** \brief see above spc section */
               res2:3,
   /** \brief see above spc section */
               tsup:1;
   /** \brief see above spc section */
   vmk_uint16 targetPortGroup;
   /** \brief see above spc section */
   vmk_uint8  reserved1;
#define VMK_SCSI_TPGD_STAT_NONE 0x00 //!< no status available
#define VMK_SCSI_TPGD_STAT_EXP  0x01 //!< state altered by SET command
#define VMK_SCSI_TPGD_STAT_IMP  0x02 //!< state altered by implicit behavior
   /** \brief see above spc section */
   vmk_uint8  status;
   /** \brief see above spc section */
   vmk_uint8  vendorSpec;
   /** \brief see above spc section */
   vmk_uint8  targetPortCount;
} VMK_ATTRIBUTE_PACKED vmk_ScsiReportTargetPortGroupDescriptor;

/**
 * \brief SCSI target port descriptor format
 * SPC 3 r23, section 6.25 table 167
 */
typedef struct vmk_ScsiReportTargetPortDescriptor {
   /** \brief see above spc section */
   vmk_uint16  obsolete;
   /** \brief see above spc section */
   vmk_uint16  relativeTargetPortId;
} VMK_ATTRIBUTE_PACKED vmk_ScsiReportTargetPortDescriptor;

/**
 * \brief set target port groups command
 * SPC 3 r23, Section 6.31 table 178
 */
typedef struct vmk_ScsiSetTargetPortGroupsCmd{
   /** \brief see above spc section */
   vmk_uint8    opcode;
   /** \brief see above spc section */
   vmk_uint8    svc:5,
#define VMK_SCSI_STPGC_SVC    0xA
   /** \brief see above spc section */
      res1:3;
   /** \brief see above spc section */
   vmk_uint8    reserved1[4];
   /** \brief see above spc section */
   vmk_uint32   length;
   /** \brief see above spc section */
   vmk_uint8    reserved2;
   /** \brief see above spc section */
   vmk_uint8    ctrl;
} VMK_ATTRIBUTE_PACKED vmk_ScsiSetTargetPortGroupsCmd;

#define SCSI_STPG_PARAM_LIST_OFFSET 4

/**
 * \brief set target port group descriptor
 * SPC 3 r23, Section 6.31 table 180
 */
typedef struct vmk_ScsiSetTargetPortGroupDescriptor{
   /** \brief see above spc section */
   vmk_uint8  aas:4,
   /** \brief see above spc section */
      res1:4;
   /** \brief see above spc section */
   vmk_uint8  reserved1;
   /** \brief see above spc section */
   vmk_uint16 targetPortGroup;
} VMK_ATTRIBUTE_PACKED vmk_ScsiSetTargetPortGroupDescriptor;


/**
 * \brief parameter header of module sense for SCSI
 * SPC 3 r23, Section 7.4.3 table 239
 */
typedef struct vmk_Scsi4ByteModeSenseParameterHeader {
   /** \brief see above spc section */
   vmk_uint8   modeDataLength;
   /** \brief see above spc section */
   vmk_uint8   mediumType;
   /** \brief see above spc section */
   vmk_uint8   reserved1:4,
   /** \brief see above spc section */
               dpofua:1,
   /** \brief see above spc section */
               reserved2:2,
   /** \brief see above spc section */
               wp:1;
   /** \brief see above spc section */
   vmk_uint8   blockDescriptorLength;
} VMK_ATTRIBUTE_PACKED vmk_Scsi4ByteModeSenseParameterHeader;

/**  
 * \brief block descriptor for SCSI mode sense
 * SPC 3 r23, Section 7.4.4.1 table 241
 */
typedef struct vmk_ScsiModeSenseBlockDescriptor {
   /** \brief see above spc section */
   vmk_uint8   densityCode;
   /** \brief see above spc section */
   vmk_uint8   numberOfBlocks[3];
   /** \brief see above spc section */
   vmk_uint8   reserved1;
   /** \brief see above spc section */
   vmk_uint8   blockLength[3];
} VMK_ATTRIBUTE_PACKED vmk_ScsiModeSenseBlockDescriptor;


/**
 * \brief Format of READ/WRITE (6) request.
 * SBC 2 r16, Section 5.5 table 26 & 5.24 table 61
 */
typedef struct vmk_ScsiReadWrite6Cmd {
   /** \brief see above sbc section */
   vmk_uint32 opcode:8,
   /** \brief see above sbc section */
              lun:3,
   /** \brief see above sbc section */
              lbn:21;
   /** \brief see above sbc section */
   vmk_uint8  length;
   /** \brief see above sbc section */
   vmk_uint8  control;
} VMK_ATTRIBUTE_PACKED vmk_ScsiReadWrite6Cmd;

/**
 * \brief Format of READ/WRITE (10) request.
 * SBC 2 r16, Section 5.6 table 28 & 5.25 table 62
 */
typedef struct vmk_ScsiReadWrite10Cmd {
   /** \brief see above sbc section */
   vmk_uint8 opcode;
   /** \brief see above sbc section */
   vmk_uint8 rel   :1,
                   :2,
   /** \brief see above sbc section */
             flua  :1,
   /** \brief see above sbc section */
             dpo   :1,
   /** \brief see above sbc section */
             lun   :3;
   /** \brief see above sbc section */
   vmk_uint32 lbn;
   /** \brief see above sbc section */
   vmk_uint8 reserved;
   /** \brief see above sbc section */
   vmk_uint16 length;
   /** \brief see above sbc section */
   vmk_uint8 control;
} VMK_ATTRIBUTE_PACKED vmk_ScsiReadWrite10Cmd;

/**
 * \brief Format of READ/WRITE (12) request.
 * SBC 2 r16, Section 5.7 table 31 & 5.26 table 65
 */
typedef struct vmk_ScsiReadWrite12Cmd {
   /** \brief see above sbc section */
   vmk_uint8 opcode;
   /** \brief see above sbc section */
   vmk_uint8 rel   :1,
                   :2,
   /** \brief see above sbc section */
             flua  :1,
   /** \brief see above sbc section */
             dpo   :1,
   /** \brief see above sbc section */
             lun   :3;
   /** \brief see above sbc section */
   vmk_uint32 lbn;
   /** \brief see above sbc section */
   vmk_uint32 length;
   /** \brief see above sbc section */
   vmk_uint8 reserved;
   /** \brief see above sbc section */
   vmk_uint8 control;
} VMK_ATTRIBUTE_PACKED vmk_ScsiReadWrite12Cmd;

/**
 * \brief Format of READ/WRITE (16) request.
 * SBC 2 r16, Section 5.8 table 32 & 5.27 table 66
 */
typedef struct vmk_ScsiReadWrite16Cmd {
   /** \brief see above sbc section */
   vmk_uint8 opcode;
   /** \brief see above sbc section */
   vmk_uint8 rel   :1,
                   :2,
   /** \brief see above sbc section */
             flua  :1,
   /** \brief see above sbc section */
             dpo   :1,
                   :3;
   /** \brief see above sbc section */
   vmk_uint64 lbn;
   /** \brief see above sbc section */
   vmk_uint32 length;
   /** \brief see above sbc section */
   vmk_uint8 reserved;
   /** \brief see above sbc section */
   vmk_uint8 control;
} VMK_ATTRIBUTE_PACKED vmk_ScsiReadWrite16Cmd;

/**
 * \brief Scsi page types for inquiry data
 */
typedef enum {
   VMK_SCSI_INQ_TYPE_STD,
   VMK_SCSI_INQ_TYPE_EVPD80,
   VMK_SCSI_INQ_TYPE_EVPD83,
   VMK_SCSI_INQ_TYPE_VMW_RESV_INITDATA,
} vmk_ScsiInqType; 
   
/**
 * \brief Persistent Reserve Out Command
 * SPC 3 r23, Section 6.12.1 (Table 112)
 */
typedef struct vmk_ScsiPersistentReserveOutCmd {
   /** \brief see above spc section */
   vmk_uint8  opcode;
   /** \brief see above spc section */
   vmk_uint8  serviceAction :5,
   /** \brief see above spc section */
              reserved      :3;
   /** \brief see above spc section */
   vmk_uint8  type          :4,
   /** \brief see above spc section */
              scope         :4;
   /** \brief see above spc section */
   vmk_uint8  reserved1[2];
   /** \brief see above spc section */
   vmk_uint32 parameterListLength; 
   /** \brief see above spc section */
   vmk_uint8  control;
} VMK_ATTRIBUTE_PACKED vmk_ScsiPersistentReserveOutCmd;

/**
 * \brief Persistent Reserve Out parameter List. 
 * SPC 3 r23, Section 6.12.3 (Table 114)
 */
typedef struct vmk_ScsiPersistentReserveOutPList {
   /** \brief see above spc section */
   vmk_uint64 reservationKey;
   /** \brief see above spc section */
   vmk_uint64 serviceActionResKey;
   /** \brief see above spc section */
   vmk_uint8  obsolete1[4];
   /** \brief see above spc section */
   vmk_uint8  aptpl          :1,
   /** \brief see above spc section */
              reserved1      :1,
   /** \brief see above spc section */
              all_tg_pt      :1,
   /** \brief see above spc section */
              spec_i_pt      :1,
   /** \brief see above spc section */
              reserved2      :4;
   /** \brief see above spc section */
   vmk_uint8  reserved3;
   /** \brief see above spc section */
   vmk_uint8  obsolete2[2];
} VMK_ATTRIBUTE_PACKED vmk_ScsiPersistentReserveOutPList;

/**
 * \brief Persistent Reserve In Command
 * SPC 3 r23, Section 6.11.1 (Table 101)
 */
typedef struct vmk_ScsiPersistentReserveInCmd { 
   /** \brief see above spc section */
   vmk_uint8  opcode; 
   /** \brief see above spc section */
   vmk_uint8  serviceAction :5,
   /** \brief see above spc section */
              reserved      :3;
   /** \brief see above spc section */
   vmk_uint8  reserved1[5];
   /** \brief see above spc section */
   vmk_uint16 allocationLength;
   /** \brief see above spc section */
   vmk_uint8  control;
} VMK_ATTRIBUTE_PACKED vmk_ScsiPersistentReserveInCmd;

/**
 * \brief Persistent read reservation response
 * SPC 3 r23, Section 6.11.3.4 (Table 105)
 */
typedef struct vmk_ScsiReadReservationResp {
   /** \brief see above spc section */
   vmk_uint32 prGeneration;
   /** \brief see above spc section */
   vmk_uint32 additionalLength;
   /** \brief see above spc section */
   vmk_uint64 reservationKey;
   /** \brief see above spc section */
   vmk_uint8  obsolete[4];
   /** \brief see above spc section */
   vmk_uint8  reserved;
   /** \brief see above spc section */
   vmk_uint8  type      :4,
   /** \brief see above spc section */
              scope     :4;
   /** \brief see above spc section */
   vmk_uint8 obsolete1[2];
} VMK_ATTRIBUTE_PACKED vmk_ScsiReadReservationResp;

/**
 * \brief Format of REQUEST SENSE block.
 * SPC 3 r23, Section 6.27 table 170
 */
typedef struct vmk_ScsiRequestSenseCommand {
   /** \brief Operation Code (03h) */
   vmk_uint8 opcode;
   /** \brief DESC */
   vmk_uint8 desc:1,
   /** \brief Reserved */
             resv1:4,
   /** \brief Reserved in SPC3R23, and ignored */
             lun:3;
   /** \brief Reserved */
   vmk_uint8  resv2;
   /** \brief Reserved */
   vmk_uint8 resv3;
   /** \brief Allocation Length */
   vmk_uint8  len;
   /** \brief Control */
   vmk_uint8  control;
} VMK_ATTRIBUTE_PACKED vmk_ScsiRequestSenseCommand;

/**
 * \brief Format of MODE SELECT   block.
 * SPC 3 r23, Section 6.7 table 94
 */
typedef struct vmk_ScsiModeSelectCommand {
   /** \brief Operation Code (15h) */
   vmk_uint8 opcode;
   /** \brief Save Pages */
   vmk_uint8 sp:1,
   /** \brief Reserved */
             resv11:3,
   /** \brief Page Format */
             pf:1,
   /** \brief Actually, reserved */
             lun:3;
   /** \brief Reserved */
   vmk_uint8 resv2;
   /** \brief Reserved */
   vmk_uint8 resv3;
   /** \brief Allocation Length */
   vmk_uint8 len;
   /** \brief Control */
   vmk_uint8 control;
} VMK_ATTRIBUTE_PACKED vmk_ScsiModeSelectCommand;

/**
 * \brief Format of REPORT LUNS   block.
 * SPC 3 r23, Section 6.21 table 147
 */
typedef struct vmk_ScsiReportLunsCommand {
   /** \brief Operation Code (A0h) */
   vmk_uint8 opcode;
   /** \brief Reserved */
   vmk_uint8 resv1;
   /** \brief SELECT REPORT */
   vmk_uint8 selectReport;
   /** \brief Reserved */
   vmk_uint8 resv2;
   /** \brief Reserved */
   vmk_uint8 resv3;
   /** \brief Allocation Length MSB */
   vmk_uint8 msblen;
   /** \brief Allocation Length LSB */
   vmk_uint8 lsblen;
   /** \brief Reserved */
   vmk_uint8 resv4;
   /** \brief Control */
   vmk_uint8 control;
} VMK_ATTRIBUTE_PACKED vmk_ScsiReportLunsCommand;

/**
 * \brief Format of READ CAPACITY 10 byte  block.
 * SPC 3 r11, Section 5.12.1 table 43
 */
typedef struct vmk_ScsiReadCap10Command {
   /** \brief Operation Code (25h) */
   vmk_uint8 opcode;
   /** \brief Obsolete */
   vmk_uint8 obs:1,
             resv1:7;
   /** \brief Logical Block Address - MSB */
   vmk_uint8 msblba;
   /** \brief Logical Block Address - LSB */
   vmk_uint8 lsblba;
   vmk_uint8 resv2;
   vmk_uint8 resv3;
   /** \brief PMI */
   vmk_uint8 pmi:1,
             resv4:7;
   /** \brief Control */
   vmk_uint8 control;
} VMK_ATTRIBUTE_PACKED vmk_ScsiReadCap10Command;

/**
 * \brief Format of READ CAPACITY 16 byte  block.
 * SPC 3 r11, Section 5.13.1 table 45
 */
typedef struct vmk_ScsiReadCap16Command {
   /** \brief Operation Code (9eh) */
   vmk_uint8 opcode;
   /** \brief Service Action (10h) */
   vmk_uint8 sa:5,
             resv1:3;
   /** \brief Logical Block Address - MSB */
   vmk_uint8 msblba;
   /** \brief Logical Block Address - LSB */
   vmk_uint8 lsblba;
   /** \brief Allocation Length MSB */
   vmk_uint8 msblen;
   /** \brief Allocation Length LSB */
   vmk_uint8 lsblen;
   vmk_uint8 pmi:1,
             resv2:7;
   /** \brief Control */
   vmk_uint8 control;
} VMK_ATTRIBUTE_PACKED vmk_ScsiReadCap16Command;

/**
 * \brief Format of READ BUFFER   block.
 * SPC 3 r23, Section 6.15 table 126
 */
typedef struct vmk_ScsiReadBufferCommand {
   /** \brief Operation Code (3Ch) */
   vmk_uint8 opcode;
   /** \brief Mode */
   vmk_uint8 mode:5,
   /** \brief Reserved */
             resv1:3;
   /** \brief BUFFER ID */
   vmk_uint8 bufferId;
   /** \brief BUFFER OFFSET - MSB */
   vmk_uint8 msbbo;
   /** \brief BUFFER OFFSET - LSB */
   vmk_uint8 lsbbo;
   /** \brief Allocation Length MSB */
   vmk_uint8 msblen;
   /** \brief Allocation Length LSB */
   vmk_uint8 lsblen;
   /** \brief Control */
   vmk_uint8 control;
} VMK_ATTRIBUTE_PACKED vmk_ScsiReadBufferCommand;

#endif  /* _VMKAPI_SCSI_TYPES_H_ */
/** @} */
/** @} */
