/***************************************************************************
 * Copyright 2004 - 2009 VMware, Inc.  All rights reserved. -- VMware Confidential
 ***************************************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * MPP                                                            */ /**
 * \addtogroup Storage
 * @{
 * \defgroup MPP Multi-Pathing Plugin Interfaces
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_MPP_H_
#define _VMKAPI_MPP_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_heap.h"
#include "base/vmkapi_memory.h"
#include "base/vmkapi_status.h"
#include "base/vmkapi_types.h"
#include "base/vmkapi_const.h"
#include "base/vmkapi_revision.h"
#include "scsi/vmkapi_scsi.h"
#include "scsi/vmkapi_scsi_mgmt_types.h"
#include "scsi/vmkapi_scsi_ext.h"
#include "scsi/vmkapi_scsi_const.h"
#include "scsi/vmkapi_scsi_types.h"
#include "mpp/vmkapi_mpp_types.h"

/** \cond never */
#define VMK_SCSI_REVISION_MAJOR 1
#define VMK_SCSI_REVISION_MINOR 0
#define VMK_SCSI_REVISION_PATCH 0
#define VMK_SCSI_REVISION_DEVEL 0
#define VMK_SCSI_REVISION VMK_REVISION_NUMBER(VMK_SCSI)
/** \endcond never */

/** \brief Max length of vendor name string including terminating nul. */
#define VMK_SCSI_VENDOR_NAME_LENGTH (VMK_SCSI_INQUIRY_VENDOR_LENGTH + 1)

//** \brief Max length of model name string including terminating nul. */
#define VMK_SCSI_MODEL_NAME_LENGTH (VMK_SCSI_INQUIRY_MODEL_LENGTH + 1)

/** \brief Default name used for unregistered devices */
#define VMK_SCSI_UNREGISTERED_DEV_NAME  "Unregistered Device"

/**
 * \brief Choices for probe rate for vmk_ScsiSetDeviceProbeRate.
 */
typedef enum {
   /** \brief For normal operation (once/5 Sec.) */
   VMK_SCSI_PROBE_RATE_DEFAULT = 1,
   /** \brief Selected when the device is in APD (once/Sec.) */
   VMK_SCSI_PROBE_RATE_FAST = 2
} vmk_ScsiDeviceProbeRate;

/**
 * \brief Flags defined for vmk_ScsiSetDeviceProbeRate
 */
typedef enum {
   /** \brief Revert to VMK_SCSI_PROBE_RATE_DEFAULT after next probe. */
   VMK_SCSI_ONE_PROBE_ONLY = 0x00000001
} vmk_ScsiSetDeviceProbeRateOption;


/*
 ***********************************************************************
 * vmk_ScsiGetCachedPathStandardUID --                            */ /**
 *
 * \ingroup MPP
 *
 * \brief Get the cached NAA, EUI, IQN, or TIO UID for physical path.
 *
 * The function returns the cached UID saved with the path as recorded
 * during the last rescan (since rescans will cause UIDs of all paths
 * to be verified). Success may be returned even if the path to the
 * device is not working. Plugins can use this API function in the
 * plugin's \em pathClaim entrypoint.
 *
 * \note This is a non-blocking call.
 *
 * \param[in] path   Path to obtain cached UID for.
 * \param[out] uid   Obtained UID on success.
 *
 * \retval VMK_BAD_PARAM  The passed in path or uid was null.
 * \retval VMK_FAILURE    The path does not have a standard UID.
 * \retval VMK_OK         Otherwise.
 *
 ***********************************************************************
 */ 
VMK_ReturnStatus vmk_ScsiGetCachedPathStandardUID(
   vmk_ScsiPath *path,
   vmk_ScsiUid *uid);

/*
 ***********************************************************************
 * vmk_ScsiReadPathStandardUID --                                 */ /**
 *
 * \ingroup MPP
 *
 * \brief Read the NAA, EUI, IQN, or TIO UID for physical path from
 *        the device.
 *
 * This function reads the UID from the SCSI device logical unit
 * that the path refers to and will return failure if the path is
 * not working. This API function is useful to check whether the UID
 * for a given path has changed; for example, in the plugin's
 * \em pathProbe entrypoint.
 *
 * \note This will regenerate the UID saved with the path.
 * \note This is a blocking call.
 *
 * \param[in] path   Path to acquire standard UID for.
 * \param[out] uid   Obtained UID on success.
 *
 * \retval VMK_OK         The UID was successfully obtained.
 * \retval VMK_BAD_PARAM  The passed in path or uid was null.
 * \retval Other          Failed to obtain the UID.
 *
 ***********************************************************************
 */ 
VMK_ReturnStatus vmk_ScsiReadPathStandardUID(
   vmk_ScsiPath *path,
   vmk_ScsiUid *uid);

/*
 ***********************************************************************
 * vmk_ScsiVerifyPathUID --                                       */ /**
 *
 * \ingroup MPP
 *
 * \brief Re-read path standard or legacy UID and verify that 
 *        it did not change.
 *
 * This function will reread the standard path UID from the device and
 * compare it against the cached UID obtained during the last rescan.
 *
 * \note This is a blocking call.
 *
 * \param[in] path   Path to check UID for.
 * \param[out] uid   UID read from disk.
 *
 * \retval VMK_OK             UID was generated and did not change.
 * \retval VMK_UID_CHANGED    New UID was detected.
 * \retval VMK_NO_CONNECT     Path connectivity has failed.
 * \retval VMK_BAD_PARAM      vmkPath or uid is NULL.
 *
 ***********************************************************************
 */ 
VMK_ReturnStatus vmk_ScsiVerifyPathUID(
   vmk_ScsiPath *path,
   vmk_ScsiUid *uid);

/*
 ***********************************************************************
 * vmk_ScsiComputeUidFromEvpd83 --                                */ /**
 *
 * \ingroup MPP
 *
 * \brief Get NAA, EUI, IQN, or TIO UID for physical device using
 *        caller supplied inquiry evpd page 0x83 data.
 *
 * This function will compute the UID from the passed in evpd83Buf.
 *
 * \note This is a non-blocking call.
 * \note The size of the \em evpd83Buf has to to be at least as big as
 *       indicated by its 16-bit page length field.  
 *
 * \param[in] vmkPath    Path to acquire inquiry evpd page 0x83.
 * \param[in] evpd83Buf  Buffer to store evpd page 0x83 data.
 * \param[out] uid       UID.
 *
 * \retval VMK_OK             UID was generated and did not change.
 * \retval VMK_FAILURE        No usable UID type (NAA/EUI/IQN/T10)
 *                            was found in the passed buffer.
 ***********************************************************************
 */ 
VMK_ReturnStatus
vmk_ScsiComputeUidFromEvpd83(
   vmk_ScsiPath *vmkPath,
   vmk_uint8 *evpd83Buf,
   vmk_ScsiUid *uid);

/*
 ***********************************************************************
 * vmk_ScsiGetPathTransportUID --                                 */ /**
 *
 * \ingroup MPP
 *
 * \brief Get Path Transport UID for the physical path.
 *
 * This function will obtain a pair of UIDs - one UID for the HBA
 * endpoint and one for the target endpoint of the path. The UID is
 * transport dependent - for FC each endpoint will be of the form
 * WWNN:WWPN and for other transports each endpoint will be whatever
 * unique identifier is used in place for traditional target IDs
 * (e.g. for iSCSI each endpoint will be an IQN).
 *
 * \note This is a non-blocking call.
 *
 * \param[in] path   Path to acquire transport uids for.
 * \param[out] uid   Obtained UIDs on success.
 *
 * \retval VMK_OK             UID successfully obtained.
 * \retval VMK_BAD_PARAM      Either path or uid is NULL.
 * \retval VMK_NOT_FOUND      The UIDs could not be obtained.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiGetPathTransportUID(
   vmk_ScsiPath *path,
   vmk_ScsiPathUid *uid);

/*
 ***********************************************************************
 * vmk_ScsiUIDsAreEqual --                                        */ /**
 *
 * \ingroup MPP
 *
 * \brief compare two uids and return true if they are equal.
 *
 * \note This is a non-blocking call.
 *
 * \param[in] uid1   First UID to compare.
 * \param[in] uid2   Second UID to compare.
 *
 * \retval VMK_TRUE   The UIDs are equal.
 * \retval VMK_FALSE  The UIDs are not equal.
 *
 ***********************************************************************
 */
vmk_Bool vmk_ScsiUIDsAreEqual(
   vmk_ScsiUid *uid1,
   vmk_ScsiUid *uid2);

/*
 ***********************************************************************
 * vmk_ScsiAllocateDevice --                                      */ /**
 *
 * \ingroup MPP
 *
 * \brief Allocate a logical storage device data structure.
 *
 * This function will allocate space for a vmk_ScsiDevice structure.
 * Obtaining this structure also ensure that the call to register
 * the device will not fail due to max. number of devices already
 * registered. Since the success of this call takes up a slot for a
 * registered SCSI device it is important that such allocations are
 * not left around idle, but are either fully registered or freed
 * again as soon as possible through vmk_ScsiFreeDevice().
 *
 * \see vmk_ScsiFreeDevice().
 *
 * \see vmk_ScsiRegisterDevice().
 *
 * \note This is a blocking call.
 * \note The call also allocates and initializes various associated
 *       VMkernel private data structures.
 *
 * \post The obtained device MUST be freed through vmk_ScsiFreeDevice().
 *
 * \param[in] plugin Plugin allocating the device.
 *
 * \return Pointer to vmk_ScsiDevice or NULL if allocation fails or
 *         if max. number of SCSI devices are already registered.
 *
 ***********************************************************************
 */
vmk_ScsiDevice *vmk_ScsiAllocateDevice(
   vmk_ScsiPlugin *plugin);

/*
 ***********************************************************************
 * vmk_ScsiRegisterDevice --                                      */ /**
 *
 * \ingroup MPP
 *
 * \brief Add a logical storage device.
 *
 * This function will attempt to register a logical SCSI device with
 * the VMkernel. There must be at least one UID passed in and precisely
 * one of the UIDs must have \em VMK_SCSI_UID_FLAG_PRIMARY set in its
 * \em uid->idFlags. The \em device->ops field must point to a structure
 * filled with at least the \em startCommand, \em taskMgmt, \em open,
 * \em close, \em probe, \em getInquiry, \em issueDumpCmd,
 * \em isPseudoDevice and \em u.mpDeviceOps.getPathnames function
 * pointers.
 *
 * \see vmk_ScsiDeviceUIDAdd().
 *      vmk_ScsiUnregisterDevice().
 *
 * \note This is a blocking call.
 * \note The plugin will be pinned (unable to unload) until the device
 *       has been unregistered again
 *
 * \pre The caller MUST have obtained the passed vmk_ScsiDevice
 *      through a call to vmk_ScsiAllocatedevice().
 * \pre The device MUST be ready to accept I/O when the call is made.
 *
 * \param[in] device    Device to register.
 * \param[in] uid       Pointer to an array of numUids uids
 *                      to register for this device.
 * \param[in] numUids   Number of UIDs in the **uid list.
 *
 * \retval VMK_OK                      Device successfully registered.
 * \retval VMK_BAD_PARAM               The value of numUids is less than 1,
 *                                     there is not exactly one uid marked
 *                                     with the VMK_SCSI_UID_FLAG_PRIMARY,
 *                                     the device->ops are not specified
 *                                     correctly, or same as
 *                                     vmk_ScsiDeviceUIDAdd().
 * \retval VMK_NOT_SUPPORTED           The plugin controlling the device
 *                                     is not marked as
 *                                     VMK_SCSI_PLUIGN_TYPE_MULTIPATHING
 *                                     or same as vmk_ScsiDeviceUIDAdd().
 * \retval VMK_NOT_FOUND               No paths are specified for the device.
 * \retval VMK_EXISTS                  Same as vmk_ScsiDeviceUIDAdd().
 * \retval VMK_DUPLIATE_UID            Same as vmk_ScsiDeviceUIDAdd().
 * \retval VMK_TOO_MANY_ELEMENTS       Same as vmk_ScsiDeviceUIDAdd().
 * \retval VMK_FAILURE                 Could not determine the legacy UID
 *                                     for the device.
 * \retval VMK_NO_CONNECT              A path to the device is in the
 *                                     process of  being removed.
 * \retval VMK_TIMEOUT                 I/O command did not complete within
 *                                     the timeout time due to a transient
 *                                     errors.
 * \retval VMK_ABORTED                 I/O command did not complete and
 *                                     was aborted.
 * \retval VMK_BUSY                    I/O command did not complete because
 *                                     the device was busy or there was a
 *                                     race to register/unregister the same
 *                                     device with another thread.
 * \retval VMK_RESERVATION_CONFLICT    I/O command did not complete due
 *                                     to a SCSI reservation on the device.
 * \retval VMK_STORAGE_RETRY_OPERATION I/O command did not complete
 *                                     due to a transient error.
 * \retval VMK_HBA_ERROR               I/O command did not complete due
 *                                     to an HBA or  driver error.
 * \retval VMK_IO_ERROR                I/O command did not complete due
 *                                     to an  unknown error.
 * \retval VMK_NOT_SUPPORTED           I/O command did not complete due
 *                                     to an unspecified error.
 * \retval VMK_MEDIUM_NOT_FOUND        I/O command did not complete on a
 *                                     removeable media device and media
 *                                     is not present.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiRegisterDevice(
   vmk_ScsiDevice *device,
   vmk_ScsiUid **uid,
   vmk_int32 numUids);

/*
 ***********************************************************************
 * vmk_ScsiUnregisterDevice --                                    */ /**
 *
 * \ingroup MPP
 *
 * \brief Remove a logical storage device.
 *
 * This function will wait for outstanding Task Management operations
 * on the device to drain before finally unregistering the device.
 * If the device is open by any world the call will fail.
 *
 * \see vmk_ScsiRegisterDevice().
 * \see vmkScsiFreeDevice().
 *
 * \note This is a blocking call.
 * \note The caller should not hold any semaphores.
 *
 * \pre The device MUST have been successfully registered with a call
 *      to vmk_ScsiRegisterDevice().
 *
 * \param[in] device    Device to unregister.
 *
 * \retval VMK_OK          Device successfully unregistered.
 * \retval VMK_BUSY        Device not unregistered as the device
 *                         was busy.
 * \retval VMK_BAD_PARAM   Device has already been unregistered.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiUnregisterDevice(
   vmk_ScsiDevice *device);

/*
 ***********************************************************************
 * vmk_ScsiFreeDevice --                                          */ /**
 *
 * \ingroup MPP
 *
 * \brief Free the storage associated with a logical storage device.
 *
 * This function will undo the work of vmk_ScsiAllocateDevice() and
 * will both free the structure and also clean up some associated
 * VMkernel internal structures. Among this is the freeing of the
 * device "slot" that will limit the total number of allocated devices.
 * It is therefore important that this function is used to free the
 * vmk_ScsiDevice structure allocated through vmk_ScsiAllocateDevice().
 *
 * \see vmk_ScsiUnregisterDevice().
 * \see vmk_ScsiAllocateDevice().
 *
 * \note This is a blocking call.
 *
 * \pre The device MUST have been unregistered (or never registered).
 *
 * \param[in] device    Device to free.
 *
 ***********************************************************************
 */
void vmk_ScsiFreeDevice(
   vmk_ScsiDevice *device); 

/*
 ***********************************************************************
 * vmk_ScsiSetDeviceState--                                       */ /**
 *
 * \ingroup MPP
 *
 * \brief Set current device state.
 *
 * This function allows a plugin to set the state of a device. The
 * intent of this call is to allow the user to see the state of a
 * device through the UI or CLI.
 *
 * \note This is a non-blocking call.
 * \note No actions are currently taken by the PSA on changes to the 
 *       device state. However this could change in a future release
 *       to add some events to notify upper layers (or VOB notifications)
 *       of device state changes. Decisions on whether to issue I/Os 
 *       to the devices are NOT made on the basis of the device state.
 *
 * \param[in] device    Device whose state should be set.
 * \param[in] state     The new state of the device.
 *
 ***********************************************************************
 */
void vmk_ScsiSetDeviceState(
   vmk_ScsiDevice *device,
   vmk_ScsiDeviceState state);

/*
 ***********************************************************************
 * vmk_ScsiDeviceStateToString --                                 */ /**
 *
 * \ingroup MPP
 *
 * \brief Convert a device state into a human readable text string.
 *
 * \see vmk_ScsiSetDeviceState().
 * \see vmk_ScsiGetDeviceState().
 *
 * \note This is a non-blocking call.
 *
 * \param[in] state  Device state to convert to string.
 *
 * \return String with human readable representation of device state.
 *
 ***********************************************************************
 */
const char *vmk_ScsiDeviceStateToString(
   vmk_ScsiDeviceState state);

/*
 ***********************************************************************
 * vmk_ScsiGetDeviceState --                                      */ /**
 *
 * \ingroup MPP
 *
 * \brief Get the current device state.
 *
 * \see vmk_ScsiSetDeviceState().
 * \see vmk_ScsiDeviceStateToString().
 *
 * \note The purpose of having a common device state is to show 
 *       consistent information to the user. Plugins may have command 
 *       line tools that display among other things the plugin device 
 *       state. The recommendation is to use vmk_ScsiGetDeviceState()
 *       and vmk_ScsiDeviceStateToString() to return consistent 
 *       information.
 *
 * \note This is a non-blocking call.
 *
 * \param[in] device    Device whose state to acquire.
 *
 * \return Device's state
 *
 ***********************************************************************
 */
vmk_ScsiDeviceState vmk_ScsiGetDeviceState(
   vmk_ScsiDevice *device);

/*
 ***********************************************************************
 * vmk_ScsiGetDeviceClass --                                      */ /**
 *
 * \ingroup MPP
 *
 * \brief Get the current device class.
 *
 * This function returns the device class of the device, which is
 * mostly according to the SCSI spec, but a few unsupported types
 * have been "overloaded" - VMK_IDE_CLASS_CDROM is a synonym for
 * VMK_SCSI_CLASS_ASCA and VMK_IDE_CLASS_OTHER is a synonym for
 * VMK_SCSI_CLASS_ASCB.
 *
 * \note This is a non-blocking call.
 *
 * \param[in] device    Device whose class to acquire.
 *
 * \return Integer representing device class
 *
 ***********************************************************************
 */
vmk_ScsiDeviceClass vmk_ScsiGetDeviceClass(
   vmk_ScsiDevice *device);

/*
 ***********************************************************************
 * vmk_ScsiGetDeviceNumBlocks --                                  */ /**
 *
 * \ingroup MPP
 *
 * \brief Return the number of blocks as reported by Read Capacity.
 *
 * \note This is a non-blocking call.
 *
 * \param[in] device    Device whose # of blocks to acquire.
 *
 * \return The number of blocks.
 *
 ***********************************************************************
 */
vmk_uint64 vmk_ScsiGetDeviceNumBlocks(
   vmk_ScsiDevice *device);

/*
 ***********************************************************************
 * vmk_ScsiGetDeviceBlockSize --                                  */ /**
 *
 * \ingroup MPP
 *
 * \brief Return the block size as reported by Read Capacity.
 *
 * \note This is a non-blocking call.
 *
 * \param[in] device    Device whose block size to acquire.
 *
 * \return The block size used by the device.
 *
 ***********************************************************************
 */
vmk_uint32 vmk_ScsiGetDeviceBlockSize(
   vmk_ScsiDevice *device);

/*
 ***********************************************************************
 * vmk_ScsiGetDeviceMaxQDepth --                                  */ /**
 *
 * \ingroup MPP
 *
 * \brief Return the maximum number of I/Os queueable to the MPP by
 *        the framework.
 *
 * This function will get the queue depth of the logical device
 * registered with the PSA layer and will be the maximum number of
 * I/Os that is allowed to be outstanding to the logical device's
 * owning MP plugin.
 *
 * \see vmk_ScsiSetDeviceMaxQDepth().
 *
 * \note This is a non-blocking call.
 * \note This is \b not the maximum queue depth on the physical device.
 * \note You may see less I/Os queued on the logical device even when
 *       multiple VMs are very busy on a VMFS volume. This is due to
 *       the throttling mechanism in ESX that ensure fairness.
 *
 * \param[in] device    Device to acquire max. queue depth for.
 *
 * \return Device's queue depth.
 *
 ***********************************************************************
 */
vmk_uint16 vmk_ScsiGetDeviceMaxQDepth(
   vmk_ScsiDevice *device);

/*
 ***********************************************************************
 * vmk_ScsiSetDeviceMaxQDepth --                                  */ /**
 *
 * \ingroup MPP
 *
 * \brief Set the maximum number of I/Os queuable to the MPP by
 *        the framework. 
 *
 * This function will set the queue depth of the logical device
 * registered with the PSA layer. It will be the maximum number of
 * I/Os that is allowed to be outstanding on the logical device.
 * A MPP plugin can use this to increase the number of I/Os if it
 * needs to distribute them on many paths (load balancing).
 *
 * \see vmk_ScsiGetDeviceMaxQDepth().
 *
 * \note This is a non-blocking call.
 * \note This is \b not the maximum queue depth on the physical device.
 * \note You may see less I/Os queued on the logical device even when
 *       multiple VMs are very busy on a VMFS volume. This is due to
 *       the throttling mechanism in ESX that ensure fairness.
 *
 * \param[in]  device   Device whose max queueing depth to set.
 * \param[out] qDepth   Depth to set.
 *
 * \retval VMK_OK         The queue depth was successfully changed.
 * \retval VMK_BAD_PARAM  The qDepth parameter value was 0.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiSetDeviceMaxQDepth(
   vmk_ScsiDevice *device,
   vmk_uint16 qDepth);

/*
 ***********************************************************************
 * vmk_ScsiSetDeviceProbeRate --                                  */ /**
 *
 * \ingroup MPP
 *
 * \brief Set a device's periodic path state update rate.
 *
 * This function can select one of two probe rates for a logical device.
 * The fast probe rate would normally be requested in case of path
 * failures to quickly get an update of how many paths are affected.
 *
 * \note This is a non-blocking call.
 * \note  The device should be probed at least as frequently as specified
 *        but may be probed more frequently than specified.
 *
 * \param[in] vmkDevice     Targeted device.
 * \param[in] newProbeRate  New probe rate ("DEFAULT" or "FAST").
 * \param[in] flags         Specifies any options, such as
 *                          VMK_SCSI_ONE_PROBE_ONLY.
 *
 * \retval VMK_OK Probe rate set successfully.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiSetDeviceProbeRate(
   vmk_ScsiDevice *vmkDevice,
   vmk_ScsiDeviceProbeRate newProbeRate,
   vmk_uint32 flags);

/*
 ***********************************************************************
 * vmk_ScsiSwitchDeviceProbeRate --                               */ /**
 *
 * \ingroup MPP
 *
 * \brief Set a device's periodic path state update rate and
 *        return the current probe rate settings.
 *
 * \note This is a non-blocking call.
 * \note  The device should be probed at least as frequently as
 *        specified but may be probed more frequently than specified.
 *
 * \param[in]  vmkDevice              Targeted device.
 * \param[in]  newProbeRate           New probe rate
 *                                    ("DEFAULT" or "FAST").
 * \param[in]  flags                  Options if any
 *                                    (i.e. VMK_SCSI_ONE_PROBE_ONLY).
 * \param[out] currentProbeRate       Probe rate prior to this call.
 * \param[out] currentProbeRateFlags  Probe flags prior to this call.
 *
 * \retval VMK_OK Probe rate set successfully.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiSwitchDeviceProbeRate(
   vmk_ScsiDevice *vmkDevice,
   vmk_ScsiDeviceProbeRate newProbeRate,
   vmk_ScsiSetDeviceProbeRateOption flags,
   vmk_ScsiDeviceProbeRate *currentProbeRate,
   vmk_ScsiSetDeviceProbeRateOption *currentProbeRateFlags);

/*
 ***********************************************************************
 * vmk_ScsiGetDeviceName --                                       */ /**
 *
 * \ingroup MPP
 *
 * \brief Get the logical device's name.
 *
 * This function can be used to provide a consistent device name
 * among logs from PSA and MP plugins.
 *
 * \note This is a non-blocking call.
 *
 * \param[in] device    Device to acquire name for.
 *
 * \return The name of the device as a human readable string.
 *
 ***********************************************************************
 */
const char *vmk_ScsiGetDeviceName(
   vmk_ScsiDevice *device);

/*
 ***********************************************************************
 * vmk_ScsiDeviceUIDAdd --                                        */ /**
 *
 * \ingroup MPP
 *
 * \brief Add a UID to a device.
 *
 * Valid UIDs (vmk_ScsiUid.id) are nul-terminated C strings that
 * may contain only the following characters:
 * - '0' through '9'
 * - 'a' through 'z'
 * - 'A' through 'Z'
 * - '_', ':', ',', '.'
 *
 * Valid UIDs should be persistent across power cycles, HBA
 * reordering, and SAN reconfiguration.  Additionally, a valid UID
 * should be the same on all ESX hosts that can access the same
 * physical storage. This function can only be called on a registered
 * device. Only one UID of type Primary ID is permitted.
 *
 * \note This function can only be called for registered devices.
 * \note This is a non-blocking call.
 *
 * \param[in] vmkDevice    Device to add uid to.
 * \param[in] uidToAdd     UID to add to device.
 *
 * \retval VMK_OK                The UID was successfully added.
 * \retval VMK_EXISTS            The UID matches the uid of a
 *                               different device.
 * \retval VMK_BAD_PARAM         The UID has invalid flags or is
 *                               composed of disallowed characters.
 * \retval VMK_DUPLICATE_UID     The UID to be added to the device is
 *                               already associated with the device.
 * \retval VMK_TOO_MANY_ELEMENTS UID to be added is a primary UID and
 *                               the device already has a primary UID.
 * \retval VMK_NOT_SUPPORTED     The specified device is unregistered.
 * \retval VMK_NAME_TOO_LONG     UID is too long (>128 bytes).
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiDeviceUIDAdd(
   vmk_ScsiDevice *vmkDevice, 
   vmk_ScsiUid *uidToAdd);

/*
 ***********************************************************************
 * vmk_ScsiDeviceUIDRemove --                                     */ /**
 *
 * \ingroup MPP
 *
 * \brief  Remove a UID from a device.
 *
 * \see vmk_ScsiDeviceUIDAdd().
 *
 * \note This is a non-blocking call.
 *
 * \param[in] device       Device to remove UID from.
 * \param[in] uidToRemove  UID to remove from device.
 *
 * \retval VMK_OK             The UID was successfully removed.
 * \retval VMK_READ_ONLY      The UID is the primary uid or a legacy UID
 *                            for a device and cannot be removed.
 * \retval VMK_NOT_FOUND      The UID is not associated with the device.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiDeviceUIDRemove(
   vmk_ScsiDevice *device,
   vmk_ScsiUid *uidToRemove);

/*
 ***********************************************************************
 * vmk_ScsiGetPathState--                                         */ /**
 *
 * \ingroup MPP
 *
 * \brief Return current path state.
 *
 * This function returns the current path state. It does not block
 * nor does it acquire another spin lock. So it is safe to call it
 * while holding a spin lock without having to worry about blocking
 * or lock ranking issues.
 *
 * \note This is a non-blocking call.
 *
 * \param[in] path   Path to get state for.
 *
 * \return The state of the path.
 *
 ***********************************************************************
 */
vmk_ScsiPathState
vmk_ScsiGetPathState(vmk_ScsiPath *path);

/*
 ***********************************************************************
 * vmk_ScsiSetPathState--                                         */ /**
 *
 * \ingroup MPP
 *
 * \brief Set PSA framework current path state.
 *
 * This function may only be called by the MP Plugin that is
 * managing the path. Other callers wishing to change a path's
 * state should instead use vmk_ScsiSetPluginPathState(). This
 * function exists so that an MP Plugin may notify the PSA
 * framework of a path state change. vmk_ScsiSetPluginPathState()
 * exists so that other callers can notify the MP Plugin of a
 * path state change (with the expectation that the MP Plugin will
 * duly call vmk_ScsiSetPathState() after doing any internal
 * bookkeeping).
 *
 * \see vmk_ScsiGetPathState().
 * \see vmk_ScsiSetPluginPathState().
 *
 * \note This is a non-blocking call.
 *
 * \param[in] plugin    Plugin that owns the path.
 * \param[in] path      Path to set state on.
 * \param[in] state     State to set.
 *
 * \retval VMK_OK             The path state was successfully set.
 * \retval VMK_NO_PERMISSION  The plugin is not the owner of the path.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiSetPathState(
   vmk_ScsiPlugin *plugin,
   vmk_ScsiPath *path,
   vmk_ScsiPathState state);

/*
 ***********************************************************************
 * vmk_ScsiSetPluginPathState--                                   */ /**
 *
 * \ingroup MPP
 *
 * \brief Set MP Plugin current path state.
 *
 * This function may only be called by non-MP Plugin callers. An
 * MP Plugin wishing to change a path's state should instead use
 * vmk_ScsiSetPathState().  This function exists so that
 * non-MP Plugin callers can notify a plugin of a path state
 * change.  vmk_ScsiSetPluginState() exists so that an MP Plugin
 * can notify the PSA framework of a path state change, with the
 * expectation that the MP Plugin will duly call
 * vmk_ScsiSetPathState() after its internal bookkeeping as a
 * of vmk_ScsiSetPluginPathState(). The path state change is
 * limited to admistrative change only to enable a disabled path
 * and vice versa.
 *
 * \see vmk_ScsiSetPathState().
 *
 * \note This is a blocking call.
 * \note Only VMK_SCSI_PATH_STATE_{ON/OFF} are allowed.
 *
 * \param[in] path   Path to set state on.
 * \param[in] state  State to set.
 *
 * \retval VMK_OK         Path state was successfully set.
 * \retval VMK_BAD_PARAM  Invalid path state was passed.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiSetPluginPathState(
   vmk_ScsiPath *path,
   vmk_ScsiPathState state);

/*
 ***********************************************************************
 * vmk_ScsiPathStateToString--                                    */ /**
 *
 * \ingroup MPP
 *
 * \brief Get a text string describing the current path state.
 *
 * \see vmk_ScsiGetPathState().
 *
 * \note This is a non-blocking call.
 *
 * \param[in] state  State to convert to string.
 *
 * \return Human-readable string describing the path state or
 *         "Unknown path state" in case an invalid path state is passed.
 *
 ***********************************************************************
 */
const char *vmk_ScsiPathStateToString(
   vmk_ScsiPathState state);

/*
 ***********************************************************************
 * vmk_ScsiGetPathLUN --                                         */ /**
 *
 * \ingroup MPP
 *
 * \brief Get path LUN.
 *
 * \note This is a non-blocking call.
 *
 * \param[in] path   Path to acquire logical unit number.
 *
 * \return LUN for the given path.
 *
 ***********************************************************************
 */
vmk_int32 vmk_ScsiGetPathLUN(
   vmk_ScsiPath *path);

/*
 ***********************************************************************
 * vmk_ScsiGetPathTarget --                                       */ /**
 *
 * \ingroup MPP
 *
 * \brief Get path target.
 *
 * \note This is the target ID as returned from the driver layer and
 *       in case of FC/iSCSI this is thus a logical mapping of the
 *       real target ID (which is a WWNN or IQN).
 * \note This is a non-blocking call.
 *
 * \param[in] path   Path to acquire target id for.
 *
 * \return Target ID for the given path.
 *
 ***********************************************************************
 */
vmk_int32 vmk_ScsiGetPathTarget(
   vmk_ScsiPath *path);

/*
 ***********************************************************************
 * vmk_ScsiGetPathChannel --                                      */ /**
 *
 * \ingroup MPP
 *
 * \brief Get path Channel.
 *
 * \note This is a non-blocking call.
 *
 * \param[in] path   Path to acquire channel number.
 *
 * \return Channel number for the given path.
 *
 ***********************************************************************
 */
vmk_int32 vmk_ScsiGetPathChannel(
   vmk_ScsiPath *path);

/*
 ***********************************************************************
 * vmk_ScsiGetPathAdapter --                                      */ /**
 *
 * \ingroup MPP
 *
 * \brief Get path's Adapter name.
 *
 * \note This is a non-blocking call.
 *
 * \param[in] path   Path to acquire adapter name for.
 *
 * \return Adapter name for the given path.
 *
 ***********************************************************************
 */
const char *vmk_ScsiGetPathAdapter(
   vmk_ScsiPath *path);

/*
 ***********************************************************************
 * vmk_ScsiGetAdapterTransport --                                 */ /**
 *
 * \ingroup MPP
 *
 * \brief Get the adapter transport name.
 *
 * Get the adapter transport type (fc/iscsi/..) as a printable string.
 *
 * \note This is a non-blocking call.
 *
 * \param[in]  adapName          Adapter to acquire transport from.
 * \param[out] transport         Transport buffer.
 * \param[in]  transportLength   Size of transport buffer.
 *
 * \retval VMK_OK         Successfully obtained transport type.
 * \retval VMK_NOT_FOUND  The passed adapter could not be found.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiGetAdapterTransport(
   const char *adapName,
   char *transport,
   vmk_uint32 transportLength);

/*
 ***********************************************************************
 * vmk_ScsiGetAdapterPendingCmdInfo --                            */ /**
 *
 * \ingroup MPP
 *
 * \brief Get the queued and active cmd counts for the adapter.
 *
 * Get the number of active and queued commands on the given adapter.
 *
 * \note This is a non-blocking call.
 * \note This information is only a snapshot of the values and they
 *       may even have changed as the function returns.
 *
 * \param[in]  adapterName    Adapter name.
 * \param[out] ioCmdCounts    If this pointer is non-NULL the memory
 *                            specified will be filled in with a
 *                            snapshot of the adapter active / queued
 *                            command count structure
 *                            \em vmk_ScsiIOCmdCounts. 
 * \param[out] queueDepthPtr  If the pointer is non-NULL the memory
 *                            specified will be filled in with a
 *                            snapshot of the adapter maximum queue
 *                            depth.
 *
 * \retval VMK_OK         Successfully obtained the pending cmd. info.
 * \retval VMK_NOT_FOUND  The passed adapter could not be found.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiGetAdapterPendingCmdInfo(
   const char *adapterName,
   vmk_ScsiIOCmdCounts *ioCmdCounts,
   vmk_int32  *queueDepthPtr);

/*
 ***********************************************************************
 * vmk_ScsiGetPathInquiry --                                      */ /**
 *
 * \ingroup MPP
 *
 * \brief Get the path's cached inquiry data.
 *
 * This function is used to obtain a copy of cached inquiry data for
 * a path. The call can fail if the data has not yet been obtained.
 *
 * \note This is a non-blocking call.
 * \note \em inqBuf may be NULL in which case only \em *pageLen is
 *       returned. This can be used to find the right buffer size up
 *       front and see whether the data is available at all.
 * 
 * \param[in]  path           Scsi path from which we are retrieving
 *                            inquiry data.
 * \param[out] inqBuf         Buffer to which inquiry data will be copied.
 * \param[in]  inqBufLen      Size of the inquiry buffer in bytes.
 * \param[in]  vmkScsiPage    Page type for the inquiry data to be
 *                            copied into the inquiry buffer.
 * \param[out] pageLen        Optional actual page length based on page
 *                            header.
 *
 * \retval VMK_OK             Successfully obtained the inquiry data.
 * \retval VMK_NOT_SUPPORTED  The data was not yet available or the
 *                            page type was invalid.
 * 
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiGetPathInquiry(
   vmk_ScsiPath *path, 
   vmk_uint8 *inqBuf,
   vmk_uint32 inqBufLen,
   vmk_ScsiInqType vmkScsiPage,
   vmk_uint32 *pageLen);

/*
 ***********************************************************************
 * vmk_ScsiGetPathVendor --                                       */ /**
 *
 * \ingroup MPP
 *
 * \brief Get the path's vendor from its cached inquiry data.
 *
 * This function will retrieve the vendor string from the cached
 * inquiry data. 
 * 
 * \note This is a non-blocking call.
 * \note The passed string buffer must have space for at least
 *       VMK_SCSI_VENDOR_NAME_LENGTH characters.
 *
 * \param[in]  path     SCSI path from which we are retrieving inq
 *                      vendor.
 * \param[out] vendor   Buffer to which inquiry vendor will be copied.
 *
 ***********************************************************************
 */
void vmk_ScsiGetPathVendor(
   vmk_ScsiPath *path, 
   char vendor[VMK_SCSI_VENDOR_NAME_LENGTH]);

/*
 ***********************************************************************
 * vmk_ScsiGetPathModel --                                        */ /**
 *
 * \ingroup MPP
 *
 * \brief Get the path's model from its cached inquiry data.
 * 
 * \note This is a non-blocking call.
 * \note The passed string buffer must have space for at least
 *       VMK_SCSI_MODEL_NAME_LENGTH characters.
 *
 * \param[in]  path     Scsi path from which we are retrieving inquiry
 *                      model.
 * \param[out] model    Buffer to which inquiry model will be copied.
 *
 * \return Human readable string with the inquiry data's model.
 *
 ***********************************************************************
 */
void vmk_ScsiGetPathModel(
   vmk_ScsiPath *path, 
   char model[VMK_SCSI_MODEL_NAME_LENGTH]);

/*
 ***********************************************************************
 * vmk_ScsiGetPathName --                                         */ /**
 *
 * \ingroup MPP
 *
 * \brief Get the current path name.
 *
 * \note This is a non-blocking call.
 * \note The obtained string should never be modified/freed.
 *
 * \param[in] path   Path whose name to acquire.
 *
 * \return Human readable string with the path's name.
 *
 ***********************************************************************
 */
const char *vmk_ScsiGetPathName(
   vmk_ScsiPath *path);

/*
 ***********************************************************************
 * vmk_ScsiGetPathInfo --                                         */ /**
 *
 * \ingroup MPP
 *
 * \brief Get the path's adapter/channel/target/lun info.
 *
 * \note This is a non-blocking call.
 *
 * \param[in]  vmkPath              Path to get info for.
 * \param[out] adapterName          Adapter name for the given path.
 * \param[in]  adapterNameSize      Size of adapterName array in bytes.
 * \param[out] channel              Channel of the given path.
 * \param[out] target               Target id of the given path.
 * \param[out] lun                  LUN id of the given path.
 *
 * \retval VMK_OK          The path info was successfully obtained.
 * \retval VMK_BAD_PARAM   One of the input parameter(s) was NULL.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiGetPathInfo(
   vmk_ScsiPath *vmkPath,
   char *adapterName,
   int adapterNameSize,
   vmk_uint32 *channel,
   vmk_uint32 *target,
   vmk_uint32 *lun);

/*
 ***********************************************************************
 * vmk_ScsiGetPathPendingCmdInfo --                               */ /**
 *
 * \ingroup MPP
 *
 * \brief Get the queued and active cmd counts for the path.
 *
 * \note This is a non-blocking call.
 * \note This information is only a snapshot.
 *
 * \param[in]  vmkPath        Path to get info for.
 * \param[out] ioCmdCounts    If this is non-NULL the memory specified
 *                            will be filled in with a snapshot of the
 *                            path active / queued command count
 *                            structure vmk_ScsiIOCmdCounts. 
 * \param[out] queueDepthPtr  If this is is non-NULL the memory specified
 *                            will be filled in with a snapshot of the
 *                            path's maximum queue depth.
 *
 * \retval VMK_OK          Successfully obtained the pending cmd. info.
 * \retval VMK_BAD_PARAM   The path was invalid or NULL.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiGetPathPendingCmdInfo(
   vmk_ScsiPath *vmkPath,
   vmk_ScsiIOCmdCounts *ioCmdCounts,
   vmk_int32  *queueDepthPtr);

/*
 ***********************************************************************
 * vmk_ScsiIssuePathTaskMgmt--                                    */ /**
 *
 * \ingroup MPP
 *
 * \brief Issue a task management command on the specified path.
 *
 * This function issues a task management command to abort/reset one
 * or more outstanding I/Os. While a virtual reset will reset only
 * commands for the given world a device/LUN reset will reset all I/O
 * on the path. An abort is targeting a single I/O though that I/O may
 * have been split into several smaller ones and can thus cause several
 * I/Os to be aborted in the driver layer.
 *
 * Note that any I/Os aborted by the call will complete asynchronously
 * and may not have completed when the call returns. The abort/reset is
 * thus only meant as a mean to speed up completion of I/Os - normally
 * by returning the I/O with an error (aborted/reset status).
 *
 * The \em taskMgmt structure is obtained from a prior call to
 * vmk_ScsiInitTaskMgmt().
 *
 * \note This is a blocking call.
 *
 * \param[in] path      Path to issue task mgmt on.
 * \param[in] taskMgmt  Task management request to issue.
 *
 * \retval VMK_OK         Task management call was successfull. This
 *                        does \em not mean that everything will
 *                        complete since there are natural races in the
 *                        stack so retry the operation if things don't
 *                        complete soon after this (in a second or two).
 * \retval VMK_BAD_PARAM  The task management type in the passed
 *                        taskMgmt struct is invalid.
 * \retval VMK_NO_MEMORY  Memory needed to issue the command could
 *                        not be allocated.
 * \retval VMK_FAILURE    Could not abort/reset for other reason,
 *                        but the operation can be retried.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiIssuePathTaskMgmt(
   vmk_ScsiPath *path,
   vmk_ScsiTaskMgmt *taskMgmt);

/*
 ***********************************************************************
 * vmk_ScsiCreateCommand --                                       */ /**
 *
 * \ingroup MPP
 *
 * \brief Allocate and initialize a SCSI command.
 *
 * For performance reasons, this routine will initialize only 
 * some of the fields of the vmk_ScsiCommand (lba, lbc,
 * dataDirection, cdb, cdblen will NOT be initialized).
 *
 * \note This is a non-blocking call.
 * \note The command must be freed using vmk_ScsiDestroyCommand().
 *
 * \return a pointer to vmk_ScsiCommand or NULL if allocation failed.
 *
 ***********************************************************************
 */
vmk_ScsiCommand *vmk_ScsiCreateCommand(void);

/*
 ***********************************************************************
 * vmk_ScsiDestroyCommand --                                      */ /**
 *
 * \ingroup MPP
 *
 * \brief Free a SCSI command.
 *
 * \note This is a non-blocking call.
 *
 * \pre The supplied command must have been allocated via
 *      vmk_ScsiCreateCommand() or one of the utility
 *      vmk_ScsiCreateXXXCommand() functions.
 *
 * \param[in] command   Command to destroy.
 *
 ***********************************************************************
 */
void vmk_ScsiDestroyCommand(vmk_ScsiCommand *command);

/*
 ***********************************************************************
 * vmk_ScsiCreateInqCommand --                                    */ /**
 *
 * \ingroup MPP
 *
 * \brief Allocate and initialize a SCSI inquiry command.
 *
 * \note This is a non-blocking call.
 * \note The command must be freed using vmk_ScsiDestroyCommand().
 *
 * \return a pointer to vmk_ScsiCommand or NULL if allocation failed.
 *
 ***********************************************************************
 */
vmk_ScsiCommand *
vmk_ScsiCreateInqCommand(
   vmk_Bool evpd,
   vmk_uint8 evpdPage,
   vmk_uint32 minLen,
   vmk_uint32 maxLen);

/*
 ***********************************************************************
 * vmk_ScsiCreateTURCommand --                                    */ /**
 *
 * \ingroup MPP
 *
 * \brief Allocate and initialize a SCSI test unit ready command.
 *
 * \note This is a non-blocking call.
 * \note The command must be freed using vmk_ScsiDestroyCommand().
 *
 * \return a pointer to vmk_ScsiCommand or NULL if allocation failed.
 *
 ***********************************************************************
 */
vmk_ScsiCommand *vmk_ScsiCreateTURCommand(void);


/*
 ***********************************************************************
 * vmk_ScsiGetNextDeviceCommand --                                */ /**
 *
 * \ingroup MPP
 *
 * \brief Get the next command for a logical device from the IO
 *        scheduler.
 *
 * This function is meant to be used from within an MPP in its
 * \em startCommand entry point. This should be called in a loop since
 * the scheduler will automatically stop when enough I/O has been
 * queued (e.g. the PSA queues may still hold I/Os, but due to max.
 * device queue depth or throttling no more I/Os will be issued).
 *
 * \note This is a non-blocking call.
 *
 * \pre The caller should not hold any spinlocks.
 * \pre The caller should have acquired any needed resources that need
 *      to be tied to the I/O up front as I/Os cannot be returned to
 *      the device queue again.
 *
 * \param[in] vmkDev    Device for which to get the next command.
 *
 * \return Pointer to a SCSI command or NULL if there are no more
 *         commands to send at this time.
 *         In that case, SCSI will invoke the plugin's \em start() entry
 *         point again when the scheduler wants to issue I/Os again.
 *
 ***********************************************************************
 */ 
vmk_ScsiCommand *vmk_ScsiGetNextDeviceCommand(vmk_ScsiDevice *vmkDev);

/*
 ***********************************************************************
 * vmk_ScsiIssueAsyncPathCommand --                               */ /**
 *
 * \ingroup MPP
 *
 * \brief Issue a command on the specified path.
 *
 * This function issues a SCSI command on a specific path. The command
 * will complete asynchronously at a later point using the command's
 * \em done() completion callback.
 *
 * \note This is a non-blocking call.
 *
 * \pre The caller should not hold any spinlocks.
 *
 * \param[in] path      Path to issue command to.
 * \param[in] command   Command to issue on path.
 *
 * \retval VMK_OK          Successfully issued the command.
 * \retval VMK_NO_CONNECT  Path is in process of being removed.
 * \retval VMK_NO_MEMORY   Unable to allocate memory for additional
 *                         resources to be associated with command.
 * \retval VMK_TIMEOUT     The command had a timeout set and the
 *                         timeout time had already been passed when
 *                         the command was to be issued.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiIssueAsyncPathCommand(
   vmk_ScsiPath *path,
   vmk_ScsiCommand *command);

/*
 ***********************************************************************
 * vmk_ScsiIssueSyncPathCommand --                                */ /**
 *
 * \ingroup MPP
 *
 * \brief Issue a synchronous command on the specified path.
 *
 * This function issues a command on a specific path and waits for
 * the command to complete before returning.
 *
 * \see vmk_ScsiIssueSyncPathCommandWithData().
 * \see vmk_ScsiIssueSyncPathCommandWithRetries().
 *
 * \note This is a blocking call.
 * \note The call specifies the buffer to use for data transfer (if
 *       needed) using \em command->sgArray.
 * \note The calling world sleeps until the command completes.
 *
 * \pre The caller should not hold any spinlock.
 *
 * \param[in] path      Path to issue command to.
 * \param[in] command   Command to issue on path.
 *
 * \retval VMK_OK          The command was issued successfully and
 *                         the command's status is valid.
 * \retval VMK_NO_CONNECT  Path is in process of being removed
 * \retval VMK_NO_MEMORY   Unable to allocate memory for additional
 *                         resources associated with command.
 * \retval VMK_TIMEOUT     The command had a timeout set and was
 *                         already timed when it was to be issued.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiIssueSyncPathCommand(
   vmk_ScsiPath *path,
   vmk_ScsiCommand *command);

/*
 ***********************************************************************
 * vmk_ScsiIssueSyncPathCommandWithData --                        */ /**
 *
 * \ingroup MPP
 *
 * \brief Issue a synchronous command on the specified path.
 *
 * This function issues a command on a specific path and waits for
 * the command to complete before returning. The caller can specify the
 * buffer to use for data transfer using the \em data and \em dataLen
 * parameters.  The passed data buffer is used to create an sgArray
 * for the command, so the \em command->sgArray must be NULL.
 *
 * \see vmk_ScsiIssueSyncPathCommand().
 * \see vmk_ScsiIssueSyncPathCommandWithRetries().
 *
 * \note This is a blocking call.
 * \note The calling world sleeps until the command completes.
 *
 * \pre The caller should not hold any spinlock.
 * \pre The \em command->sgArray must be NULL.
 *
 * \param[in]     path     Path to issue command to.
 * \param[in]     command  Command to issue on path.
 * \param[in,out] data     Data buffer.
 * \param[in]     dataLen  Length of data buffer.
 *
 * \retval VMK_OK          The command was issued successfully and
 *                         the command's status is valid.
 * \retval VMK_NO_CONNECT  Path is in process of being removed.
 * \retval VMK_NO_MEMORY   Unable to allocate memory for additional
 *                         resources associated with command.
 * \retval VMK_TIMEOUT     The command had a timeout set and was
 *                         already timed when it was to be issued.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiIssueSyncPathCommandWithData(
   vmk_ScsiPath *path,
   vmk_ScsiCommand *command,
   void *data,
   vmk_uint32 dataLen);

/*
 ***********************************************************************
 * vmk_ScsiIssueSyncPathCommandWithRetries --                     */ /**
 *
 * \ingroup MPP
 *
 * \brief Issue a synchronous command on the specified path.
 *
 * This function issues a command on a specific path and waits for
 * the command to complete before returning. The caller can specify
 * the buffer to use for any data transfer using either the data and
 * dataLen parameters or \em scsiCmd->sgArray, but not both.
 *
 * The issued command can complete with the errors listed below in
 * the completionStatus parameter.
 *    - VMK_MEDIUM_NOT_FOUND the device returned a medium not
        present error.
 *    - VMK_TIMEOUT the command timed out.
 *    - VMK_ABORTED the command was aborted.
 *    - VMK_NO_CONNECT the path is not connected.
 *
 * The issued command can also complete with the transient errors
 * listed below.
 *
 * Before these errors are returned in the completionStatus
 * parameter, the command will be retried a number of times.
 *    - VMK_RESERVATION_CONFLICT the device is reserved.
 *    - VMK_BUSY the device returned a busy status.
 *    - VMK_IN_TRANSITION the device is temporarily unavailable.
 *    - VMK_NO_MEMORY the system is out of memory.
 *    - VMK_HBA_ERROR the device driver returned an error.
 *    - VMK_STORAGE_RETRY_OPERATION the command failed with:
 *       - A check condition of power on/reset or unit attention.
 *       - A check condition with a sense key of aborted cmd.
 *       - A check condition with a sense key of not ready/becoming
 *         ready.
 *       - A host error status of retry.
 *       - The cmd was specified with a timeout value and it was
 *         terminated with an abort/reset prior to the expiration of
 *         the timeout value.
 *
 * \see vmk_ScsiIssueSyncPathCommand().
 * \see vmk_ScsiIssueSyncPathCommandWithData().
 *
 * \note This is a blocking call.
 * \note The calling world sleeps until the command completes.
 *
 * \param[in]  path              The path the cmd is to be issued to.
 * \param[in]  command           Command to be issued.
 * \param[in]  data              Read/write data associated with
 *                               the command.
 * \param[in]  dataLen           Length of the read/write data
 *                               associated with the command.
 * \param[out] completionStatus  Completion status of the command.
 *                               The completion status is only valid
 *                               if this function returns VMK_OK.
 *
 * \retval VMK_OK          The command was issued successfully and
 *                         the command's status is valid.
 * \retval VMK_NO_CONNECT  Path is in process of being removed.
 * \retval VMK_NO_MEMORY   Unable to allocate memory for additional
 *                         resources associated with command.
 * \retval VMK_TIMEOUT     The command had a timeout set and was
 *                         already timed when it was to be issued.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiIssueSyncPathCommandWithRetries(
   vmk_ScsiPath *path,
   vmk_ScsiCommand *command,
   vmk_uint8 *data,
   vmk_uint32 dataLen,
   VMK_ReturnStatus *completionStatus);

/*
 ***********************************************************************
 * vmk_ScsiIssueSyncDumpCommand --                                */ /**
 *
 * \ingroup MPP
 *
 * \brief Issue a dump command to a path during a system core dump.
 *
 * This function issues a dump command on the given path and
 * busywaits until the command completes. It is meant for an MP plugin
 * to call this from it's device->dumpCommand entry point.
 *
 * \note This is a blocking call.
 *
 * \param[in] path      Path to issue command to.
 * \param[in] command   Command to issue on path.
 *
 * \retval VMK_OK       Dump command was issued successfully and
 *                      the command's status is valid.
 * \retval VMK_FAILURE  The adapter backing the path is not enabled.
 * \retval VMK_TIMEOUT  Command timed out and should NOT be retried.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiIssueSyncDumpCommand(
   vmk_ScsiPath *path,
   vmk_ScsiCommand *command);

/*
 ***********************************************************************
 * vmk_ScsiSchedCommandCompletion --                              */ /**
 *
 * \ingroup MPP
 *
 * \brief Schedules a non-blocking context to complete the command.
 *
 * This function schedules a non-blocking context to complete a
 * command. The intent is to use this from the issuing path where
 * a command cannot be completed directly since that could lead to
 * stack exhaustion due to recursive calls to the issuing path from
 * the completion path.
 *
 * \note This is a non-blocking call.
 *
 * \param[in] command   The cmd to complete.
 *
 ***********************************************************************
 */
void vmk_ScsiSchedCommandCompletion(
   vmk_ScsiCommand *command);

/*
 ***********************************************************************
 * vmk_ScsiRegisterEventHandler --                                */ /**
 *
 * \ingroup MPP
 *
 * \brief Add an Event Handler for the specific masked event types
 *        on the specified adapter.
 *
 * This function registers a handler that will be called as certain
 * events on the passed adapter occur.
 *
 * \see vmk_ScsiAdapterEvents.
 * \see vmk_ScsiUnRegisterEventHandler().
 *
 * \note This is a non-blocking call.
 * \note When the callback is called, it should  not grab any locks
 *       below minor rank 7, major rank SP_RANK_SCSI.
 *
 * \param[in] adapterName  Adapter name to register callback for.
 * \param[in] mask         Events to be notified of as a bit mask.
 * \param[in] handlerCbk   Callback to be called when an event
 *                         occurs.
 *
 * \retval VMK_OK               Event handler successfully registered.
 * \retval VMK_INVALID_ADAPTER  The passed adapter does not exist.
 * \retval VMK_NO_MEMORY        Failed to allocate memory for event.
 *
 ***********************************************************************
 */
VMK_ReturnStatus
vmk_ScsiRegisterEventHandler(const char *adapterName, vmk_uint32 mask,
      vmk_EventHandlerCbk handlerCbk);

/*
 ***********************************************************************
 * vmk_ScsiUnRegisterEventHandler --                              */ /**
 *
 * \ingroup MPP
 *
 * \brief Remove an Event Handler for the specific masked event types
 *    on the specified adapter.
 *
 * This function unregisters an event handler earlier registered by
 * vmk_ScsiRegisterEventHandler. All arguments need to match those that
 * were used to register the handler in order for the call to succeed.
 *
 * \see vmk_ScsiRegisterEventHandler().
 *
 * \note This is a non-blocking call.
 *
 * \param[in] adapterName  Name of the adapter to unregister for events.
 * \param[in] mask         Event bit mask to unregister.
 * \param[in] handlerCbk   Event callback to be unregistered.
 *
 * \retval VMK_OK               Event handler successfully unregistered.
 * \retval VMK_INVALID_ADAPTER  The passed adapter does not exist.
 *
 ***********************************************************************
 */
VMK_ReturnStatus
vmk_ScsiUnRegisterEventHandler(const char *adapterName, vmk_uint32 mask,
      vmk_EventHandlerCbk handlerCbk);

/*
 ***********************************************************************
 * vmk_ScsiAllocatePlugin --                                      */ /**
 *
 * \ingroup MPP
 *
 * \brief Allocate a plugin data structure.
 *
 * This function will allocate a plugin structure for use when
 * registering the plugin with PSA. The structure should be freed
 * with vmk_ScsiFreePlugin() again when it is no longer registered.
 *
 * \see vmk_ScsiRegisterPlugin().
 * \see vmk_ScsiUnregisterPlugin().
 * \see vmk_ScsiFreePlugin().
 *
 * \note This is a blocking call.
 *
 * \post The plugin MUST be freed through vmk_ScsiFreePlugin().
 *
 * \param[in] heapID       Heap id to allocate memory from.
 * \param[in] pluginName   Name of allocating plugin.
 *
 * \return Pointer to vmk_ScsiPlugin structure or NULL if memory
 *         could not be allocated for the structure.
 *
 ***********************************************************************
 */
vmk_ScsiPlugin *vmk_ScsiAllocatePlugin(
   vmk_HeapID heapID, char *pluginName);

/*
 ***********************************************************************
 * vmk_ScsiFreePlugin --                                          */ /**
 *
 * \ingroup MPP
 *
 * \brief Free a plugin data structure allocated by
 *        vmk_ScsiAllocatePlugin().
 *
 * \see vmk_ScsiAllocatePlugin().
 * \see vmk_ScsiUnregisterPlugin().
 *
 * \note This is a blocking call.
 *
 * \pre The plugin structure must first be unregistered with PSA (or
 *      never have been registered).
 *
 * \param[in] plugin    Plugin to free.
 *
 ***********************************************************************
 */
void vmk_ScsiFreePlugin(
   vmk_ScsiPlugin *plugin);

/*
 ***********************************************************************
 * vmk_ScsiRegisterPlugin --                                      */ /**
 *
 * \ingroup MPP
 *
 * \brief Register a plugin with SCSI.
 *
 * This function will register a plugin with PSA.
 *
 * \see vmk_ScsiAllocatePlugin().
 * \see vmk_ScsiUnregisterPlugin().
 *
 * \note This is a blocking call.
 *
 * \pre The vmk_ScsiPlugin structure MUST have been allocated through
 *      vmk_ScsiAllocatePlugin().
 *
 * \param[in] plugin    Plugin to register.
 *
 * \retval VKM_OK             Successfully registered the plugin.
 * \retval VMK_EXISTS         The plugin is already registered with PSA.
 * \retval VMK_NOT_SUPPORTED  The API revision the plugin is compiled
 *                            for is incompatible with the ESX server.
 * \retval VMK_BAD_PARAM      The plugin's type is invalid/unsupported
 *                            or some required entry points are not set.
 * 
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiRegisterPlugin(
   vmk_ScsiPlugin *plugin);

/*
 ***********************************************************************
 * vmk_ScsiUnregisterPlugin --                                    */ /**
 *
 * \ingroup MPP
 *
 * \brief Unregister a plugin previously registered via
 *        vmk_ScsiRegisterPlugin().
 *
 * This function is used to unregister a plugin that has previously
 * been successfully registered with PSA. Before a plugin can be
 * unregistered it must not have any paths claimed or devices
 * registered.
 * The plugin must also have a state of VMK_SCSI_PLUGIN_STATE_DISABLED
 * when this function is called, which has to be set through
 * vmkScsiSetPluginState() - the latter will ensure that no claim
 * operations will be passed to the plugin.
 *
 * \see vmk_ScsiFreePlugin().
 * \see vmk_ScsiSetPluginState().
 *
 * \note This is a blocking call.
 *
 * \pre The plugin should have released all its SCSI resources (paths,
 *      devices, ...).
 * \pre The plugin state must be VMK_SCSI_PLUGIN_STATE_DISABLED.
 *
 * \param[in] plugin    Plugin to unregister.
 *
 * \retval VMK_OK         Successfully unregistered the plugin.
 * \retval VMK_BAD_PARAM  The plugin was already not registered.
 * \retval VMK_BUSY       The plugin state was not set to disabled.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiUnregisterPlugin(
   vmk_ScsiPlugin *plugin);

/*
 ***********************************************************************
 * vmk_ScsiSetPluginState--                                       */ /**
 *
 * \ingroup MPP
 *
 * \brief Set the current state of the plugin.
 *
 * This function sets the plugin state to one of the possible values
 * defined by the vmk_ScsiPluginState enum. The plugin will normally
 * operate in ENABLED mode and when the \em pathClaimBegin entry point
 * is called the plugin should (temporarily) set it to CLAIM_PATHS and
 * then set it back to ENABLED when the \em pathClaimEnd entry point is
 * later invoked. Lastly it should be set to DISABLED before the
 * plugin is finally unregistered.
 *
 * \note This is a non-blocking call.
 *
 * \param[in] plugin    Plugin to set state to.
 * \param[in] state     New state to set.
 *
 ***********************************************************************
 */
void vmk_ScsiSetPluginState(
   vmk_ScsiPlugin *plugin,
   vmk_ScsiPluginState state);

/*
 ***********************************************************************
 * vmk_ScsiGetPluginState--                                       */ /**
 *
 * \ingroup MPP
 *
 * \brief Get the current state of the plugin.
 *
 * \see vmk_ScsiSetPluginState().
 *
 * \note This is a non-blocking call.
 *
 * \param[in] plugin    Plugin to get state from.
 *
 ***********************************************************************
 */
vmk_ScsiPluginState vmk_ScsiGetPluginState(
   vmk_ScsiPlugin *plugin);

/*
 ***********************************************************************
 * vmk_ScsiIncReserveGeneration --                                */ /**
 *
 * \ingroup MPP
 *
 * \brief Increment the reservation generation count of the device.
 *
 * This function is used to tell the PSA layer that a reservation was
 * broken/cleared by the MP plugin and thus PSA should fail any
 * reservation sensitive transactions using the previous generation.
 *
 * The intent is that an MP plugin calls this if it has to do a failover
 * while a SCSI-2 reservation is held on the device and thus has to
 * force a LU reset on the new path (or if it has to do a LU reset for
 * any other reason - or invokes/knows about any other operation that
 * will clear or has cleared the held reservation).
 *
 * \note This is a non-blocking call.
 *
 * \param[in] vmkDevice    Device whose reservation generation to bump.
 *
 ***********************************************************************
 */
void
vmk_ScsiIncReserveGeneration(vmk_ScsiDevice *vmkDevice);

/*
 ***********************************************************************
 * vmk_ScsiCommandMaxCommands --                                  */ /**
 *
 * \ingroup MPP
 *
 * \brief Maximum outstanding SCSI commands
 *
 ***********************************************************************
 */
vmk_uint32
vmk_ScsiCommandMaxCommands(void);

/*
 ***********************************************************************
 * vmk_ScsiCommandMaxFree --                                      */ /**
 *
 * \ingroup MPP
 *
 * \brief Maximum idle SCSI commands
 *
 ***********************************************************************
 */
vmk_uint32
vmk_ScsiCommandMaxFree(void);

#endif  /* _VMKAPI_MPP_H_ */
/** @} */
/** @} */
