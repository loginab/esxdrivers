/************************************************************
 * Copyright 2007-2008 VMware, Inc.  All rights reserved.
 ************************************************************/

/*
 *
 * iscsi_linux.h --
 *
 *  Annex iSCSI Transport Code: 3rd party media driver Linux Interface
 *
 *
 */

#ifndef _ISCSI_LINUX_H_
#define _ISCSI_LINUX_H_

/*
 * iscsi_register_transport
 * Description:  Register this Media Driver with the transport Mid-Layer.
 *               This is the first step required in enabling the Media
 *               Driver to function in the Open iSCSI framework.  The
 *               Mid-Layer will then query the driver for various
 *               information as well as signal when to start operation.
 * Arguments:    tptr - identifies this Media Driver
 *               size - size of the template
 * Returns:      The transport template
 * Side effects: XXXSEFD
 */
struct scsi_transport_template *
iscsi_register_transport(struct iscsi_transport *tptr);

/*
 * iscsi_unregister_transport
 * Description:  This function is called in the clean up path of the Media
 *               Driver when we are trying to unload it from the kernel.
 * Arguments:    tptr - The iSCSI transport template we previously registered 
 *                      using the iscsi_register_transport call.
 * Returns:      Always zero. I suppose it should return an ERROR if the
 *               driver is still in use.
 * Side effects: XXXSEFD
 */
vmk_int32
iscsi_unregister_transport(struct iscsi_transport *tptr);

/*
 * iscsi_alloc_session
 * Description:  Initialize the ML specific class object that represents a
 *               session. Note that the size of iscsi_cls_session will vary
 *               depending on the ML implementation. (IE: Open iSCSI version
 *               vs VMWare proprietary version) The new session is not added
 *               to the ML list of sessions for this Media Driver. Media
 *               Drivers should use iscsi_create_session.
 * Arguments:    hptr - Pointer to the scsi_host that will own this target
 *               tptr - Pointer to The transport template passed in as part of 
 *                      iscsi_register_transport
 * Returns:      pointer to the newly allocated session.
 * Side effects: XXXSEFD
 */
struct iscsi_cls_session *
iscsi_alloc_session(struct Scsi_Host *hptr, struct iscsi_transport *tptr);

/*
 * iscsi_add_session
 * Description:  Adds the session object to the ML thus exposing the target
 *               to the Operating System as a SCSI target.
 * Arguments:    sptr      - A session previously created with
 *                           iscsi_alloc_session
 *               target_id - The target ID for this session
 * Returns:      0 on success, otherwise an OS ERROR.
 * Side effects: XXXSEFD
 */
vmk_int32
iscsi_add_session(struct iscsi_cls_session *sptr, vmk_uint32 target_id,
   vmk_uint32 channel);

/*
 * iscsi_create_session
 * Description:  This function allocates a new iscsi_cls_session object and
 *               adds it to the Media Drivers list of sessions. This is done
 *               by calling iscsi_alloc_session followed by
 *               iscsi_add_session.  Most Media Drivers should use this
 *               interface.
 * Arguments:    hptr      - The host that will be used to create the session
 *               tptr      - Pointer to the iscsi_transport template that was
 *                           previously registered for this host (Media Driver)
 *               target_id - Target ID for the session
 *               channel   - Channel for the session
 * Returns:      pointer to the session created
 * Side effects: XXXSEFD
 */
struct iscsi_cls_session *
iscsi_create_session(void *hptr, struct iscsi_transport *tptr,
                     vmk_uint32 target_id, vmk_uint32 channel);

/*
 * iscsi_offline_session
 * Description:  Prevent additional I/O from being sent to the Media Driver
 *               through queue command.  In addition update scsi_device
 *               states to mark this device SDEV_OFFLINE.  Then notify
 *               the upper layer to rescan the path.
 * Arguments:    sptr - The session to offline
 * Returns:      nothing, function must always succeed.
 * Side effects: XXXSEFD
 */
void
iscsi_offline_session(struct iscsi_cls_session *ptr);

/*
 * iscsi_remove_session
 * Description:  Remove the specified session from the Mid Layer. The Mid
 *               Layer is responsible for removing the scsi target from the
 *               SCSI layer as well as ensuring any recovery work is cleaned
 *               up.
 * Arguments:    sptr - The session to remove from the ML
 * Returns:      nothing
 * Side effects: XXXSEFD
 */
void
iscsi_remove_session(struct iscsi_cls_session *sptr);

/*
 * iscsi_free_session
 * Description:  Release the scsi device associated with this session, this
 *               should have the effect of cleaning up the session object if
 *               this is the last reference to the session.
 * Arguments:    sptr - Pointer to the session class object freeing it's memory
 * Returns:      nothing
 * Side effects: XXXSEFD
 */
void
iscsi_free_session(struct iscsi_cls_session *sptr);

/*
 * iscsi_destroy_session
 * Description:  Reverse of iscsi_create_session.  Removes the session from
 *               the ML, dereferences any scsi devices. Frees all resources.
 * Arguments:    sptr - Session to shutdown/destroy.
 * Returns:      XXXRVFD
 * Side effects: XXXSEFD
 */
vmk_int32
iscsi_destroy_session(struct iscsi_cls_session *sptr);

/*
 * iscsi_create_conn
 * Description:  Create a new connection to the target on the specified session.
 * Arguments:    sptr         - session on which to create a new connection
 *               connectionID - The connection ID/number
 * Returns:      pointer to the newly created connection.
 * Side effects: XXXSEFD
 */
struct iscsi_cls_conn *
iscsi_create_conn(struct iscsi_cls_session *sptr, vmk_uint32 connectionID);

/*
 * iscsi_destroy_conn
 * Description:  Close the specified connection and remove it from the sessions
 *               list of connections. Leave the session in place.
 * Arguments:    cptr - The connection to shutdown
 * Returns:      0 on success otherwise an OS ERROR
 * Side effects: XXXSEFD
 */
vmk_int32
iscsi_destroy_conn(struct iscsi_cls_conn *cptr);

/*
 * iscsi_block_session
 * Description:  Prevent additional I/O from being sent to the Media Driver
 *               through queue command. Any session recovery work should
 *               still continue.
 * Arguments:    sptr - The session to block
 * Returns:      nothing, function must always succeed.
 * Side effects: XXXSEFD
 * Warning: XXX Currently takes no action
 */
void
iscsi_block_session(struct iscsi_cls_session *sptr);

/*
 * iscsi_unblock_session
 * Description:  Allow additional I/O work to be sent to the driver through
 *               it's queuecommand function. It may be necessary to block
 *               the Operating System IO queue.
 * Arguments:    sptr - The session to unblock
 * Returns:      nothing, function must always succeed.
 * Side effects: XXXSEFD
 * Warning: XXX Currently takes no action
 */
void
iscsi_unblock_session(struct iscsi_cls_session *sptr);
               
/*
 * iscsi_scan_target
 * Description:  Perform a scsi_scan_target on the specified target
 * Arguments:    dptr - Device Pointer
 *               chan - Channel
 *               id   - SCSI ID
 *               lun  - SCSI LUN
 * Returns:      Status Code
 * Side effects: XXXSEFD
 * Note: This function is called by iscsi_trans to perform the appropriate
 *       target scan.
 */
void
iscsi_scan_target(struct device *dptr, uint chan, uint id, uint lun, int rescan);
                                                                      
/*
 * iscsi_user_scan
 * Description:  Performs a scan of all targets registered to this transport.
 * Arguments:    hptr              - Device Host Pointer
 *               chan              - Channel
 *               id                - SCSI ID
 *               lun               - SCSI LUN
 *               iscsi_scan_target - target scanning function
 * Returns:      Status Code
 * Side effects: XXXSEFD
 */
static int 
iscsi_user_scan(struct Scsi_Host *hptr, uint chan, uint id, uint lun);
               
/*
 * iscsi_register_host
 * Description:  Register a management adapter on the hptr specified. Management
 *               adapters are registered as a member of vmk_ScsiAdapter so this 
 *               call would actualy just retreive the underlying vmk_ScsiAdapter
 *               from the struct Scsi_Host and call the vmk version of the API
 * Arguments:    hptr - Pointer to the SCSI host to be registered.
 *               tptr - The iSCSI transport template we previously registered 
 *                      using the iscsi_register_transport call.
 * Returns:      Status Code
 * Side effects: XXXSEFD
 */
vmk_int32
iscsi_register_host(struct Scsi_Host *hptr, struct iscsi_transport *tptr);

/*
 * iscsi_register_host
 * Description:  Unregister a management adapter on the hptr specified. 
 *               Management adapters are registered as a member of 
 *               vmk_ScsiAdapter so this call would actualy just retreive the 
 *               underlying vmk_ScsiAdapter from the struct Scsi_Host and call 
 *               the vmk version of the API.
 * Arguments:    hptr - Pointer to the SCSI host to be unregistered.
 *               tptr - The iSCSI transport template we previously registered 
 *                      using the iscsi_register_transport call.
 * Returns:      Status Code
 * Side effects: XXXSEFD
 */
vmk_int32
iscsi_unregister_host(struct Scsi_Host *hptr, struct iscsi_transport *tptr);

/*
 * iscsi_release_device_host
 * Description:  Release a Device. Currently does a scsi_host_put
 * Arguments:    dptr - pointer to the device whose host should be released
 * Returns:      nothing
 * Side effects: XXXSEFD
 */
void
iscsi_release_device_host(struct device *dptr);

/*
 * iscsi_sdevice_to_session
 *
 * Description:  return the session associated with a scsi device
 * Arguments:    pointer to the scsi_device
 * Returns:      pointer to the session
 * Side effects: none
 */
struct iscsi_cls_session *
iscsi_sdevice_to_session(struct scsi_device *sdev);

#endif
