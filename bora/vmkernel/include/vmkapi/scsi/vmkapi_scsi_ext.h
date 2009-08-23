/* **********************************************************
 * Copyright 2007 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */


/*
 ***********************************************************************
 * SCSI Externally Exported Interfaces                            */ /**
 * \addtogroup SCSI
 * @{
 *
 * \defgroup SCSIext SCSI Interfaces Exported to User Mode
 *
 * Vmkernel-specific SCSI constants & types which are shared with
 * user-mode code.
 * @{
 *
 ***********************************************************************
 */

#ifndef _VMKAPI_SCSI_EXT_H_
#define _VMKAPI_SCSI_EXT_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMNIXMOD
#define INCLUDE_ALLOW_VMCORE
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"

/**
 * \brief  Maximum number of logical devices supported by this version.
 *
 * When this count is reached, further calls to
 * vmk_ScsiAllocateDevice() will fail.
 */
#define VMK_SCSI_MAX_DEVICES 256

/**
 * \brief Maximum number of physical paths supported by this version.
 *
 * When this count is reached, the vmkernel will drop any
 * additional paths discovered during a scan.
 */
#define VMK_SCSI_MAX_PATHS  1024

/**
 * \brief Length of the device class description,
 *        including the trailing NUL character.
 */
#define VMK_SCSI_CLASS_MAX_LEN	18

/** \cond nodoc */
#define VMK_SCSI_DEVICE_CLASSES \
   VMK_SCSI_DEVICE_CLASS_NUM(VMK_SCSI_CLASS_DISK, 0, "Direct-Access    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_TAPE,        "Sequential-Access") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_PRINTER,     "Printer          ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_CPU,         "Processor        ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_WORM,        "WORM             ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_CDROM,       "CD-ROM           ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_SCANNER,     "Scanner          ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_OPTICAL,     "Optical Device   ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_MEDIA,       "Medium Changer   ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_COM,         "Communications   ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_ASCA,        "ASC IT8 0xA      ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_ASCB,        "ASC IT8 0xB      ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RAID,        "RAID Ctlr        ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_ENCLOSURE,   "Enclosure Svc Dev") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_SIMPLE_DISK, "Simple disk      ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV1,       "Reserved 0xF     ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV2,       "Reserved 0x10    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV3,       "Reserved 0x11    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV4,       "Reserved 0x12    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV5,       "Reserved 0x13    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV6,       "Reserved 0x14    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV7,       "Reserved 0x15    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV8,       "Reserved 0x16    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV9,       "Reserved 0x17    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV10,      "Reserved 0x18    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV11,      "Reserved 0x19    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV12,      "Reserved 0x1A    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV13,      "Reserved 0x1B    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV14,      "Reserved 0x1C    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV15,      "Reserved 0x1D    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_RESV16,      "Reserved 0x1E    ") \
   VMK_SCSI_DEVICE_CLASS(VMK_SCSI_CLASS_UNKNOWN,     "No device type   ") \

#define VMK_SCSI_DEVICE_CLASS(name, description) \
   /** \brief description */ name,
#define VMK_SCSI_DEVICE_CLASS_NUM(name, value, description) \
   /** \brief description */ name = value,
/** \endcond */

/**
 * \brief SCSI device classes.
 */
typedef enum {
   VMK_SCSI_DEVICE_CLASSES
   VMK_SCSI_DEVICE_CLASS_LAST
} vmk_ScsiDeviceClass;

/** \cond nodoc */
#undef VMK_SCSI_DEVICE_CLASS
#undef VMK_SCSI_DEVICE_CLASS_NUM
/** \endcond */

/** \cond nodoc */
#define VMK_SCSI_DEVICE_STATES \
   VMK_SCSI_DEVICE_STATE_NUM(VMK_SCSI_DEVICE_STATE_ON, 0, "on", \
                             "The device is operational.") \
   VMK_SCSI_DEVICE_STATE(VMK_SCSI_DEVICE_STATE_OFF, "off", \
                         "The device has been disabled by user intervention.") \
   VMK_SCSI_DEVICE_STATE(VMK_SCSI_DEVICE_STATE_DEAD, "dead", \
                         "There are no paths to the device.") \
   VMK_SCSI_DEVICE_STATE(VMK_SCSI_DEVICE_STATE_QUIESCED, "quiesced", \
                         "The device is not accepting I/Os temporarily.") \

#define VMK_SCSI_DEVICE_STATE(name,description,longDesc) \
      /** \brief longDesc */ name,
#define VMK_SCSI_DEVICE_STATE_NUM(name,value,description,longDesc) \
      /** \brief longDesc */ name = value,
/** \endcond */

/**
 * \brief SCSI device states.
 */
typedef enum {
   VMK_SCSI_DEVICE_STATES
   VMK_SCSI_DEVICE_STATE_LAST
} vmk_ScsiDeviceState;

/** \cond nodoc */
#undef VMK_SCSI_DEVICE_STATE
#undef VMK_SCSI_DEVICE_STATE_NUM
/** \endcond */

/*
 * Paths
 */

/**
 * \brief Maximum SCSI path name length.
 *
 * Path names are of the form "<adapter name>:C%u:T%u:L%u";
 * Their length is limited by SCSI_DISK_ID_LEN (44) because, absent
 * a better ID, the pathname may be used as ID on-disk.  This leaves
 * 11 bytes beyond VMK_SCSI_ADAPTER_NAME_LENGTH (32), which is enough
 * for "...:Cn:Tnn:Lnn" or "...:Cn:Tn:Ln    nn".
 */
#define VMK_SCSI_PATH_NAME_MAX_LEN 44

/** \cond nodoc */
#define VMK_SCSI_PATHSTATES \
   VMK_SCSI_PATH_STATE_NUM(VMK_SCSI_PATH_STATE_ON,   0,    "on")             \
   VMK_SCSI_PATH_STATE(VMK_SCSI_PATH_STATE_OFF,            "off")            \
   VMK_SCSI_PATH_STATE(VMK_SCSI_PATH_STATE_DEAD,           "dead")           \
   VMK_SCSI_PATH_STATE(VMK_SCSI_PATH_STATE_STANDBY,        "standby")        \
   VMK_SCSI_PATH_STATE(VMK_SCSI_PATH_STATE_DEVICE_CHANGED, "device_changed") \

#define VMK_SCSI_PATH_STATE(name,description) \
   /** \brief description */ name,
#define VMK_SCSI_PATH_STATE_NUM(name,value,description) \
   /** \brief description */ name = value,

/** \endcond */

/**
 * \brief State of a SCSI path.
 */
typedef enum {
   VMK_SCSI_PATHSTATES
   VMK_SCSI_PATH_STATE_LAST
} vmk_ScsiPathState;

/** \cond nodoc */
#undef VMK_SCSI_PATH_STATE
#undef VMK_SCSI_PATH_STATE_NUM
/** \endcond */

/*
 * Commands
 */

#define VMK_SCSI_MAX_CDB_LEN        16
#define VMK_SCSI_MAX_SENSE_DATA_LEN 64

typedef enum {
   VMK_SCSI_COMMAND_DIRECTION_UNKNOWN,
   VMK_SCSI_COMMAND_DIRECTION_WRITE,
   VMK_SCSI_COMMAND_DIRECTION_READ,
   VMK_SCSI_COMMAND_DIRECTION_NONE,
} vmk_ScsiCommandDirection;

/**
 * \brief Plugin specific status for a SCSI command.
 * \note The vmk_ScsiPluginStatus is a status value returned from the MP plugin that was 
 * processing the I/O cmd.  If an error is returned it means that the command could 
 * not be issued or needs to be retried etc. 
 */
typedef enum vmk_ScsiPluginStatus {
   /** \brief No error. */
   VMK_SCSI_PLUGIN_GOOD,
   /** \brief An unspecified error occurred. 
    * \note The I/O cmd should be retried. 
    */
   VMK_SCSI_PLUGIN_TRANSIENT,
   /** \brief The device is a deactivated snapshot. 
    * \note The I/O cmd failed because the device is a deactivated snapshot and so
    * the LUN is read-only. 
    */
   VMK_SCSI_PLUGIN_SNAPSHOT,
   /** \brief SCSI-2 reservation was lost. */
   VMK_SCSI_PLUGIN_RESERVATION_LOST,
} vmk_ScsiPluginStatus;

/**
 * \brief Adapter specific status for a SCSI command.
 * \note The vmk_ScsiHostStatus is a status value from the driver/hba. Most errors here
 * mean that the I/O was not issued to the target.
 */
typedef enum {
   /** \brief No error. */
   VMK_SCSI_HOST_OK          = 0x00,
   /** \brief The HBA could not reach the target. */
   VMK_SCSI_HOST_NO_CONNECT  = 0x01,
   /** \brief The SCSI BUS was busy. 
    * \note This error is most relevant for parallel SCSI devices because SCSI uses a bus 
    * arbitration mechanism for multiple initiators. Newer transport
    * protocols are packetized and use switches, so this error does not occur. However,
    * some drivers will return this in other unrelated error cases - if a 
    * connection is temporarily lost for instance.
    */
   VMK_SCSI_HOST_BUS_BUSY    = 0x02,
   /** \brief Driver was unable to issue the command to the device. */
   VMK_SCSI_HOST_TIMEOUT     = 0x03,
   /** \brief Driver receives an I/O for the target ID of the initiator (HBA). 
    * \note Some drivers return this error when they 
    * really mean NO_CONNECT. Note that ESX should never cause a driver to return this
    * error if the driver has reported it's initiator ID correctly.
    */
   VMK_SCSI_HOST_BAD_TARGET  = 0x04,
   /** \brief The I/O was successfully aborted. */
   VMK_SCSI_HOST_ABORT       = 0x05,
   /** \brief A parity error was detected.
    * \note This error is most relevant to parallel SCSI devices  where the BUS uses 
    * a simple parity bit to check that transfers are not corrupted (it can detect 
    * only 1, 3,  5 and 7 bit errors).
    */
   VMK_SCSI_HOST_PARITY      = 0x06,
   /** \brief Generic error.
    * \note This is an error that the driver can return for
    * events not covered by other errors. For instance,  drivers will return this
    * error in the event of a data overrun/underrun. 
    */
   VMK_SCSI_HOST_ERROR       = 0x07,
   /** \brief Device was reset.
    * \note  This error  Indicates that the I/O was cleared from the HBA due to 
    * a BUS/target/LUN reset. 
    */
   VMK_SCSI_HOST_RESET       = 0x08,
   /** \brief Legacy error. 
    * \note This error is not expected to be returned and should be
    * treated as a VMK_SCSI_HOST_ERROR. 
    */
   VMK_SCSI_HOST_BAD_INTR    = 0x09,
   /** \brief Legacy error.
    * \note This error should nolonger be returned.
    */
   VMK_SCSI_HOST_PASSTHROUGH = 0x0a,
   /** \brief Legacy error.
    * \note This error is not expected to be returned.  It was meant as
    * a way for drivers to return an I/O that has failed due to temporary conditions
    * in the driver and should be retried. 
    */
   VMK_SCSI_HOST_SOFT_ERROR  = 0x0b,
   /** \brief Legacy error. 
    * \note This error is not expected to be returned.
    * I/O cmd should be reissued immediately.
    */
   VMK_SCSI_HOST_RETRY       = 0x0c,
   /** \brief A transient error has occured. 
    * \note This error indicates that the I/O cmd should be 
    * queued and reissued later. 
    */
   VMK_SCSI_HOST_REQUEUE     = 0x0d,
   VMK_SCSI_HOST_MAX_ERRORS, /* Add all error codes before this. */
} vmk_ScsiHostStatus;

/**
 * \brief Device specific status for a SCSI command.
 * \note The vmk_ScsiDeviceStatus is the status reported by the target/LUN itself.  The
 * values are defined  in the SCSI specification. 
 */
typedef enum {
   VMK_SCSI_DEVICE_GOOD                       = 0x00,
   VMK_SCSI_DEVICE_CHECK_CONDITION            = 0x02,
   VMK_SCSI_DEVICE_CONDITION_MET              = 0x04,
   VMK_SCSI_DEVICE_BUSY                       = 0x08,
   VMK_SCSI_DEVICE_INTERMEDIATE               = 0x10,
   VMK_SCSI_DEVICE_INTERMEDIATE_CONDITION_MET = 0x14,
   VMK_SCSI_DEVICE_RESERVATION_CONFLICT       = 0x18,
   VMK_SCSI_DEVICE_COMMAND_TERMINATED         = 0x22,
   VMK_SCSI_DEVICE_QUEUE_FULL                 = 0x28,
   VMK_SCSI_DEVICE_ACA_ACTIVE                 = 0x30,
   VMK_SCSI_DEVICE_TASK_ABORTED               = 0x40,
} vmk_ScsiDeviceStatus;

/**
 * \brief Status a for SCSI command.
 * \note The completion status for an I/O is a three-level hierarchy of
 * vmk_scsiPluginStatus, vmk_ScsiHostStatus, and vmk_ScsiDeviceStatus.
 *  - vmk_scsiPluginStatus is the highest level
 *  - vmk_scsiHostAtatus is the next level
 *  - vmk_scsiDeviceStatus is the lowest level
 *
 * An error reported at one level should not be considered valid if there is an error reported 
 * by a higher level of the hierarchy. For instance, if vmk_ScsiPluginStatus does not indicate an
 * error and an error is indicated in vmk_ScsiHostStatus, then 
 * the value of vmk_ScsiDeviceStatus is ignored.
 */
typedef struct vmk_ScsiCmdStatus {
   /** \brief Device specific command status.  
    * \note This is the lowest level error. 
    */
   vmk_ScsiDeviceStatus	device;
   /** \brief Adapter specific command status. */
   vmk_ScsiHostStatus	host;
   /** \brief Plugin specific command status. 
    *  \note This is the highest level error.
    */
   vmk_ScsiPluginStatus plugin;
} vmk_ScsiCmdStatus;

/* \cond nodoc */
#define VMK_SCSI_TASK_MGMT_TYPES(def) \
   def(VMK_SCSI_TASKMGMT_ABORT, "abort", "Abort single command.") \
   def(VMK_SCSI_TASKMGMT_VIRT_RESET, "virt reset", \
       "Abort all commands sharing a unique originator ID" ) \
   def(VMK_SCSI_TASKMGMT_LUN_RESET, "lun reset", "Reset a LUN.") \
   def(VMK_SCSI_TASKMGMT_DEVICE_RESET, "target reset", "Reset a target.") \
   def(VMK_SCSI_TASKMGMT_BUS_RESET, "bus reset", "Reset the bus.") \

#define VMK_SCSI_DEF_TASK_MGMT_NAME(name, desc, longDesc) name,
#define VMK_SCSI_DEF_TASK_MGMT_DESC(name, desc, longDesc) desc,
#define VMK_SCSI_DEF_TASK_MGMT_NAME_WITH_COMMENT(name, desc, longDesc) \
   /** \brief longDesc */ name,
/* \endcond */

/**
 * \brief Task management types.
 */
typedef enum {
   VMK_SCSI_TASK_MGMT_TYPES(VMK_SCSI_DEF_TASK_MGMT_NAME_WITH_COMMENT)
   VMK_SCSI_TASKMGMT_LAST
} vmk_ScsiTaskMgmtType;

typedef struct vmk_ScsiIOCmdCountFields {
   vmk_uint32  rdCmds;
   vmk_uint32  wrCmds;
   vmk_uint32  otherCmds;
   vmk_uint32  totalCmds;
} vmk_ScsiIOCmdCountFields;

/**
 * \brief I/O Stats for Path and Adapter
 */
typedef struct vmk_ScsiIOCmdCounts {
   vmk_ScsiIOCmdCountFields  active;
   vmk_ScsiIOCmdCountFields  queued;
} vmk_ScsiIOCmdCounts;

#endif  /* _VMKAPI_SCSI_EXT_H_ */
/** @} */
/** @} */
