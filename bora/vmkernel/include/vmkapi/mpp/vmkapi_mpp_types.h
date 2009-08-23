/***************************************************************************
 *
 * Copyright 2004 - 2009 VMware, Inc.  All rights reserved. -- VMware Confidential
 *
 ***************************************************************************/

/*
 ***********************************************************************
 * MPP Types                                                      */ /**
 * \addtogroup MPP
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_MPP_TYPES_H_
#define _VMKAPI_MPP_TYPES_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_lock.h"
#include "base/vmkapi_const.h"
#include "scsi/vmkapi_scsi_types.h"

/**
 * \brief Private data container for SCSI path
 */

typedef struct vmk_ScsiPath {
   /** \brief claiming plugin's priv data */
   void *pluginPrivateData;
} vmk_ScsiPath;

/*
 * \brief SCSI plugin type.
 */
typedef enum {
   VMK_SCSI_PLUGIN_TYPE_INVALID,
   VMK_SCSI_PLUGIN_TYPE_MULTIPATHING,
} vmk_ScsiPluginType;


typedef enum {
   VMK_SCSI_PLUGIN_PRIORITY_UNKNOWN       = 0x00000000,
#define VMK_SCSI_PLUGIN_PRIORITY_SIMPLE (VMK_SCSI_PLUGIN_PRIORITY_HIGHEST)
   VMK_SCSI_PLUGIN_PRIORITY_HIGHEST       = 0x00000001,
   VMK_SCSI_PLUGIN_PRIORITY_VIRUSSCAN     = 0x00020000,
   VMK_SCSI_PLUGIN_PRIORITY_DEDUPLICATION = 0x00040000,
   VMK_SCSI_PLUGIN_PRIORITY_COMPRESSION   = 0x00080000,
   VMK_SCSI_PLUGIN_PRIORITY_ENCRYPTION    = 0x00100000,
   VMK_SCSI_PLUGIN_PRIORITY_REPLICATION   = 0x00200000,
   VMK_SCSI_PLUGIN_PRIORITY_LOWEST        = 0xffffffff
} vmk_ScsiPluginPriority;

/** \cond nodoc */
typedef struct vmk_ScsiDevice vmk_ScsiDevice;
/** \endcond */

typedef enum vmk_ScsiPluginStatelogFlag {
   VMK_SCSI_PLUGIN_STATELOG_GLOBALSTATE = 0x00000001,
   VMK_SCSI_PLUGIN_STATE_LOG_CRASHDUMP  = 0x00000002
} vmk_ScsiPluginStatelogFlag;

#define VMK_SCSI_UID_MAX_ID_LEN       256

/**
 ***********************************************************************
 *                                                                */ /**
 * \struct vmk_ScsiMPPluginSpecific
 * \brief vmk_ScsiPlugin specific data & operations for plugin type
 *        VMK_SCSI_PLUGIN_TYPE_MULTIPATHING
 *
 ***********************************************************************
 */
struct vmk_ScsiPlugin;

typedef struct vmk_ScsiMPPluginSpecific {
   /**
    * \brief Tell the plugin we're about to start offering it paths
    *
    *   A series of pathClaim calls is always preceded by a
    *   pathClaimBegin call.
    */
   VMK_ReturnStatus (*pathClaimBegin)(struct vmk_ScsiPlugin *plugin);
   /**
    * \brief Offer a path to the plugin for claiming
    *
    *   Give the plugin a chance to claim a path. The plugin is the
    *   owner of the path for the duration of the call; To retain
    *   ownership of the path beyond that call, the plugin must return
    *   VMK_OK and set claimed to VMK_TRUE.
    *
    *   If an error occurs that prevents the plugin from claiming the
    *   path (e.g. out of memory, IO error, ...) it should return a non
    *   VMK_OK status to let SCSI know that a problem occured and
    *   that it should skip that path.
    *   The plugin can issue IOs on the path, change its state, sleep,
    *   ...
    *   If it decides not to claim the path, the plugin must drain all
    *   the IOs it has issued to the path before returning.
    *
    *   A series of pathClaim calls is always preceded by a
    *   pathClaimBegin call and followed by a pathClaimEnd call.
    */
   VMK_ReturnStatus (*pathClaim)(vmk_ScsiPath *path,
		                 vmk_Bool *claimed);
   /**
    * \brief Ask the plugin to unclaim a path
    *
    *   Tell the plugin to try and unclaim the specified path.
    *   The plugin can perform blocking tasks before returning.
    */
   VMK_ReturnStatus (*pathUnclaim)(vmk_ScsiPath *path);
   /**
    * \brief Tell the plugin we're done offering it paths
    *
    *   A series of pathClaim calls is always terminated by a
    *   pathClaimEnd call.  The PSA framework serializes this call and will
    *   not call into this entrypoint from more than a single world
    *   simultaneously.
    */
   VMK_ReturnStatus (*pathClaimEnd)(struct vmk_ScsiPlugin *plugin);
   /**
    * \brief Probe the path; update its current state
    *
    *   Issue a probe IO and update the current state of the path.
    *   The function blocks until the state of the path has been updated
    */
   VMK_ReturnStatus (*pathProbe)(vmk_ScsiPath *path);
   /**
    * \brief Set a path's state (on or off only)
    *
    *   Turn the path on or off per administrator action, or else mark the
    *   underlying adapter driver is informing the plugin that a path is dead.
    *   An MP Plugin should notify the PSA framework if a path state has changed
    *   via vmk_ScsiSetPathState() after it has performed any necessary internal
    *   bookkeeping.
    */
   VMK_ReturnStatus (*pathSetState)(vmk_ScsiPath *path,
                                    vmk_ScsiPathState state);
   /** 
    * \brief Get device name associated with a path 
    *
    *   On successful completion of this routine the MP plugin is expected to 
    *   return the device UID in the deviceName. The device UID string must
    *   be NULL terminated.
    *   If the path is not claimed by the plugin then an error status of
    *   of VMK_FAILURE has to be returned by the plugin.
    *   If the device corresponding to the path has not been registered with
    *   the PSA framework then an error status of VMK_NOT_FOUND has to be 
    *   returned by the plugin.
    *   PSA will return these errors to the callers of this routine.
    */ 
   VMK_ReturnStatus (*pathGetDeviceName)(vmk_ScsiPath *path,
                                      char deviceName[VMK_SCSI_UID_MAX_ID_LEN]); 
} vmk_ScsiMPPluginSpecific;

/**
 * \brief data for ioctl on SCSI plugin
 */

/**
 * \brief Ioctl data argument encapsulating every optional argument
 *        that can be passed to vmk_ScsiPlugin->pluginIoctl()
 */
typedef vmk_uint32 vmk_ScsiPluginIoctl;

typedef union vmk_ScsiPluginIoctlData {
   /** \brief plugin type specific union */
   union {
      /** \brief reserved */
      vmk_uint8 reserved[128];
   } u;
} vmk_ScsiPluginIoctlData;

#define VMK_SCSI_PLUGIN_NAME_MAX_LEN    40

/**
 * \brief SCSI plugin
 */
typedef struct vmk_ScsiPlugin {
   /** \brief Revision of the SCSI API implemented by the plugin */
   vmk_revnum  scsiRevision;
   /**
    * \brief Revision of the plugin.
    *
    * This field is used for support and debugging purposes only.
    */
   vmk_revnum  pluginRevision;
   /**
    * \brief Revision of the product implemented by this plugin.
    *
    * This field is used for support and debugging purposes only.
    */
   vmk_revnum  productRevision;
   /** \brief moduleID of the VMkernel module hosting the plugin. */
   vmk_ModuleID moduleID;
   /** \brief lock class of the VMkernel module hosting the plugin. */
   vmk_SpinlockClass  lockClass;
   /**
    * \brief Human readable name of the plugin.
    *
    * This field is used for logging and debugging only.
    */
   char        name[VMK_SCSI_PLUGIN_NAME_MAX_LEN];
   /** \brief plugin type specific union */
   union {
      /** \brief multipathing plugin specific data & entrypoints */
      vmk_ScsiMPPluginSpecific mp; // type == VMK_SCSI_PLUGIN_TYPE_MULTIPATHING
      /** \brief reserved */
      unsigned char            reserved[512];
   } u;

   /**
    * \brief Issue a management ioctl on the specified plugin
    *
    * - Not currently used
    */
   VMK_ReturnStatus (*pluginIoctl)(struct vmk_ScsiPlugin *plugin,
                                   vmk_ScsiPluginIoctl cmd,
                                   vmk_ScsiPluginIoctlData *data);
   /**
    * \brief Prompt a plugin to log its internal state
    *
    * Log a plugin's internal state
    */
   VMK_ReturnStatus (*logState)(struct vmk_ScsiPlugin *plugin,
                                const vmk_uint8 *logParam,
                                vmk_ScsiPluginStatelogFlag logFlags);
   /** \brief reserved */
   vmk_VirtAddr reserved1[4];
   /** \brief reserved */
   vmk_uint32   reserved2[3];
   /** \brief Type of the plugin
    *
    * Currently only MULTIPATHING is supported.  This field is used to
    * determine what capabilities the plugin is allowed to implement.
    */
   vmk_ScsiPluginType type;
} vmk_ScsiPlugin;

/*
 * SCSI Device Identifiers
 */
#define VMK_SCSI_UID_FLAG_PRIMARY          0x00000001
/** \brief This represents the legacy UID (starting with vml.xxx..) */
#define VMK_SCSI_UID_FLAG_LEGACY           0x00000002
/** \brief The uid is globally unique. */
#define VMK_SCSI_UID_FLAG_UNIQUE           0x00000004
/** \brief The uid does not change across reboots. */
#define VMK_SCSI_UID_FLAG_PERSISTENT       0x00000008

#define VMK_SCSI_UID_FLAG_DEVICE_MASK  \
   (VMK_SCSI_UID_FLAG_PRIMARY      |   \
    VMK_SCSI_UID_FLAG_LEGACY       |   \
    VMK_SCSI_UID_FLAG_UNIQUE       |   \
    VMK_SCSI_UID_FLAG_PERSISTENT)

/**
 * \brief SCSI identifier
 */
typedef struct vmk_ScsiUid {
   /** \brief null terminated printable identifier */
   char 	        id[VMK_SCSI_UID_MAX_ID_LEN];
   /** \brief id attributes, see uid_flags above */
   vmk_uint32 	        idFlags;
   /** \brief reserved */
   vmk_uint8            reserved[4];
} vmk_ScsiUid;

#define VMK_SCSI_UID_FLAG_PATH_MASK    \
   (VMK_SCSI_UID_FLAG_UNIQUE       |   \
    VMK_SCSI_UID_FLAG_PERSISTENT)

/**
 * \brief SCSI path identifier.
 *
 * Path identifier constructed from transport specific data
 */
typedef struct vmk_ScsiPathUid {
   /** \brief flags indicating uniqueness and persistence */
   vmk_uint64 		idFlags;
   /** \brief adapter id */
   char 		adapterId[VMK_SCSI_UID_MAX_ID_LEN];
   /** \brief target id */
   char 		targetId[VMK_SCSI_UID_MAX_ID_LEN];
   /** \brief device id (matches vmk_ScsiDevice's primary vmk_ScsiUid) */
   char 		deviceId[VMK_SCSI_UID_MAX_ID_LEN];
} vmk_ScsiPathUid;

/**
 * \brief Operations provided by a device managed by 
 *        VMK_SCSI_PLUGIN_TYPE_MULTIPATHING 
 */
typedef struct vmk_ScsiMPPluginDeviceOps { 
   /** \brief Get a list of path names associated with a device */
   VMK_ReturnStatus (*getPathNames)(vmk_ScsiDevice *device,
                                    vmk_HeapID *heapID,
                                    vmk_uint32 *numPathNames,
                                    char ***pathNames);
} vmk_ScsiMPPluginDeviceOps; 

/**
 * \brief Operations provided by a device to the storage stack
 */
typedef struct vmk_ScsiDeviceOps {
   /**
    * \brief Notify the plugin that a command is available on the specified
    *        device.
    *
    * The plugin should fetch the command at the earliest opportunity
    * using vmk_ScsiGetNextDeviceCommand().
    *
    * SCSI invokes startCommand() only when queueing an IO in an empty
    * queue. SCSI does not invoke startCommand() again until
    * vmk_ScsiGetNextDeviceCommand() has returned NULL once. 
    *  
    * \note IO CPU accounting: 
    * MP plugin has to implement CPU accounting for processing IOs 
    * on behalf of each world via vmk_ServiceTimeChargeBeginWorld / 
    * vmk_ServiceTimeChargeEndWorld API in the issuing and task
    * management paths. CPU accounting for the completion path is 
    * done by the PSA path layer. Path probing and other auxillary 
    * tasks are not executed on behalf of any particular world and, 
    * therefore, do not require CPU accounting. 
    */
   VMK_ReturnStatus (*startCommand)(vmk_ScsiDevice *device);
   /**
    * \brief Issue a task management request on the specified device.
    * 
    * The plugin should:
    *
    * - invoke vmk_ScsiQueryTaskMgmtAction() for all the device's IOs
    *   currently queued inside the plugin or until
    *   vmk_ScsiGetNextDeviceCommand() returns
    *   VMK_SCSI_TASKMGMT_ACTION_BREAK or
    *   VMK_SCSI_TASKMGMT_ACTION_ABORT_AND_BREAK
    * - complete all IOs returning VMK_SCSI_TASKMGMT_ACTION_ABORT or
    *   VMK_SCSI_TASKMGMT_ACTION_ABORT_AND_BREAK with the status
    *   specified in the task management request
    *
    *   If vmk_ScsiQueryTaskMgmtAction() did not return with
    *   VMK_SCSI_TASKMGMT_ACTION_BREAK or
    *   VMK_SCSI_TASKMGMT_ACTION_ABORT_AND_BREAK:
    *
    * - forward the request to all paths with at least one IO in
    *   progress for that device, and
    * - one (live) path to the device
    */
   VMK_ReturnStatus (*taskMgmt)(vmk_ScsiDevice *device,
                                vmk_ScsiTaskMgmt *taskMgmt);
   /** \brief Open a device 
    * The plugin should fail the open if the device state is set 
    * to VMK_SCSI_DEVICE_STATE_OFF
    */
   VMK_ReturnStatus (*open)(vmk_ScsiDevice *device);
   /** \brief Close a device */
   VMK_ReturnStatus (*close)(vmk_ScsiDevice *device);
   /** \brief Probe a device */
   VMK_ReturnStatus (*probe)(vmk_ScsiDevice *device);
   /** \brief Get the inquiry data from the specified device */
   VMK_ReturnStatus (*getInquiry)(vmk_ScsiDevice *device,
                                  vmk_ScsiInqType inqPage,
                                  vmk_uint8 *inquiryData,
                                  vmk_uint32 inquirySize);
   /** \brief Issue a sync cmd to write out a core dump */
   VMK_ReturnStatus (*issueDumpCmd)(vmk_ScsiDevice *device,
                                    vmk_ScsiCommand *scsiCmd);
   /** \brief plugin type specific union */
   union {
      /** \brief multipathing plugin specific device ops */
      vmk_ScsiMPPluginDeviceOps mpDeviceOps; 
      /** \brief reserved */
      unsigned char            reserved[128];
   } u;
   /** \brief Identify if the device is a pseudo device */
   VMK_ReturnStatus (*isPseudoDevice)(vmk_ScsiDevice *device,
                                      vmk_Bool *isPseudo);
   /** \note reserved for future extension. */
   void (*reserved[4])(void);
} vmk_ScsiDeviceOps;


/** 
 * \brief SCSI device structure
 */
struct vmk_ScsiDevice {
   /** \brief producing plugin's priv data */
   void                 *pluginPrivateData;
   /** \brief device ops the PSA framework should use for this device */
   vmk_ScsiDeviceOps    *ops;
   /** \brief producing plugin's module id */
   vmk_ModuleID         moduleID;
   /** \brief reserved */
   vmk_uint8            reserved[4];
};

#endif /* _VMKAPI_MPP_TYPES_H_ */
/** @} */
