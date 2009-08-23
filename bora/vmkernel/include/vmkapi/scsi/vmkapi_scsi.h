/***************************************************************************
 * Copyright 2004 - 2009 VMware, Inc.  All rights reserved.
 ***************************************************************************/

/*
 ***********************************************************************
 * SCSI                                                           */ /**
 * \addtogroup Storage
 * @{
 * \defgroup SCSI SCSI Interfaces
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_SCSI_H_
#define _VMKAPI_SCSI_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_heap.h"
#include "base/vmkapi_memory.h"
#include "base/vmkapi_scatter_gather.h"
#include "base/vmkapi_status.h"
#include "base/vmkapi_types.h"
#include "base/vmkapi_const.h"
#include "base/vmkapi_revision.h"
#include "scsi/vmkapi_scsi_const.h"
#include "scsi/vmkapi_scsi_types.h"
#include "device/vmkapi_vector.h"

/*
 * Physical Path
 */

/*
 ***********************************************************************
 * vmk_ScsiDeviceClassToString --                                 */ /**
 *
 * \ingroup SCSI
 *
 * \brief Convert a SCSI class identifier into a human-readable text
 *        string.
 *
 * \param[in] sclass    SCSI class to convert.
 *
 * \return The description string.
 *
 ***********************************************************************
 */
const char *vmk_ScsiDeviceClassToString(
   vmk_ScsiDeviceClass sclass);

/*
 ***********************************************************************
 * vmk_ScsiScanPaths --                                           */ /**
 *
 * \ingroup SCSI
 *
 * \brief Scan one or more physical paths.
 *
 * The discovered paths are automatically registered with the
 * storage stack.
 *
 * The sparse luns, max lun id and lun mask settings affect which
 * paths are actually scanned.
 *
 * \note If this routine returns an error, some paths may have been
 *       sucessfully discovered.
 * \note This function may block.
 *
 * \param[in] adapterName  Name of the adapter to scan, or
 *                         VMK_SCSI_PATH_ANY_ADAPTER to scan all
 *                         adapters.
 * \param[in] channel      Channel to scan, or VMK_SCSI_PATH_ANY_CHANNEL
 *                         to scan all channels.
 * \param[in] target       Target id to scan, or VMK_SCSI_PATH_ANY_TARGET
 *                         to scan all targets.
 * \param[in] lun          LUN id to scan, or VMK_SCSI_PATH_ANY_LUN to
 *                         scan all LUNs.
 *
 *
 * \retval VMK_INVALID_NAME     The requested adapter was not found.
 * \retval VMK_BUSY             The requested adapter is currently being 
 *                              scanned by some other context.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiScanPaths(
   const char *adapterName,
   vmk_uint32 channel,
   vmk_uint32 target,
   vmk_uint32 lun);

/*
 ***********************************************************************
 * vmk_ScsiScanAndClaimPaths --                                   */ /**
 *
 * \ingroup SCSI
 *
 * \brief Scan one or more physical paths and run the plugin claim rules.
 *
 * The discovered paths are automatically registered with the
 * storage stack. Path claim is invoked after a successful scan.
 * This may result in new SCSI devices being registered with VMkernel.
 *
 * The sparse luns, max lun id and lun mask settings affect which
 * paths are actually scanned.
 *
 * \note This function may block.
 *
 * \param[in] adapterName  Name of the adapter to scan, or
 *                         \em VMK_SCSI_PATH_ANY_ADAPTER to scan all
 *                         adapters.
 * \param[in] channel      Channel to scan, or 
 *                         \em VMK_SCSI_PATH_ANY_CHANNEL to scan all 
 *                         channels.
 * \param[in] target       Target id to scan, or 
 *                         \em VMK_SCSI_PATH_ANY_TARGET to scan all 
 *                         targets.
 * \param[in] lun          LUN id to scan, or \em VMK_SCSI_PATH_ANY_LUN 
 *                         to scan all LUNs.
 *
 * \return It can return all the errors that vmk_ScsiScanPaths() can return.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiScanAndClaimPaths(
   const char *adapterName,
   vmk_uint32 channel,
   vmk_uint32 target,
   vmk_uint32 lun);

/*
 ***********************************************************************
 * vmk_ScsiNotifyPathStateChange --                               */ /**
 *
 * \ingroup SCSI
 *
 * \brief Notify the VMkernel of a possible path state change (sync).
 *
 * Path is identified by \em vmkAdapter, \em channel, \em target
 * and \em lun.
 * This interface does the path probe in the calling context.
 * The function returns the result from an attempt to probe the 
 * state of the path(s) without retrying on error conditions.
 *
 * \note This function may block.
 * \note No spin lock may be held when calling this function.
 *
 * \param[in] vmkAdapter   Name of the adapter for state change.
 * \param[in] channel      Channel for state change.
 * \param[in] target       Target for state change.
 * \param[in] lun          LUN for state change. If -1, scan all
 *                         LUNs on the given adapter, channel,
 *                         and target.
 *
 * \retval VMK_NOT_FOUND   Requested path was not found. This will
 *                         be returned only when a specific \em lun 
 *                         is mentioned (not for \em lun==-1).
 * \retval VMK_BUSY        Requested path is already being probed.
 *
 * \return Any other error indicates some plugin specific error while
 *          probing for the path.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiNotifyPathStateChange(
   vmk_ScsiAdapter *vmkAdapter,
   vmk_int32 channel,
   vmk_int32 target,
   vmk_int32 lun);

/*
 ***********************************************************************
 * vmk_ScsiNotifyPathStateChangeAsync --                          */ /**
 *
 * \ingroup SCSI
 *
 * \brief Notify the VMkernel of a possible path state change (async).
 *
 * Path is identified by \em vmkAdapter, \em channel, \em target, 
 * \em lun. Unlike vmk_ScsiNotifyPathStateChange(), which does the
 * path probe in the calling context, this function only schedules 
 * the path probe to happen in separate context. Hence there is no 
 * restriction on the calling context (interrupt, or bottom-half, 
 * or kernel context) and the locks held on entry. 
 *
 * The function returns the result of an attempt to schedule a
 * probe of the state of the path(s). The result of the probe 
 * itself is not returned.  In the face of error conditions, 
 * the probe will be retried a large number of times.
 *
 * \param[in] vmkAdapter   Name of the adapter for state change.
 * \param[in] channel      Channel for state change.
 * \param[in] target       Target for state change.
 * \param[in] lun          LUN for state change. If -1, scan all
 *                         LUNs on the given adapter, channel,
 *                         and target.
 * 
 * \retval VMK_NO_MEMORY      Out of memory.
 * \retval VMK_NO_RESOURCES   Failed to schedule the asynchronous path 
 *                            probe due to lack of resources.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiNotifyPathStateChangeAsync(
   vmk_ScsiAdapter *vmkAdapter,
   vmk_int32 channel,
   vmk_int32 target,
   vmk_int32 lun);

/*
 ***********************************************************************
 * vmk_ScsiInitTaskMgmt --                                        */ /**
 *
 * \ingroup SCSI
 *
 * \brief Create a task management request filter.
 *
 * A task management request filter consists of two things:
 *
 * - A TASK MANAGEMENT action (abort, device reset, bus reset, lun 
 *   reset etc). This is the \em type parameter.
 *
 * - A rule to define the commands on which the above action should
 *   be performed. This is defined by the \em cmdId parameter.
 *
 * A task management request filter thus created can then be used by any 
 * subsequent functions that expect a \em vmk_ScsiTaskMgmt argument.
 * 
 * \param[in] vmkTask   The task management structure to initialize.
 * \param[in] type      Type of task management request.
 * \param[in] cmdId     Identifier of the command(s) to abort.
 *                      This field is only valid for abort and virtual
 *                      reset. It is ignored otherwise. 
 *                      \em VMK_SCSI_TASKMGMT_ANY_INITIATOR is only
 *                      valid for virtual reset.
 *                      cmdID.initiator may one of the following:
 *                      - cmdID.initiator from the vmk_ScsiCommand
 *                        structure.
 *                      - \em VMK_SCSI_TASKMGMT_ANY_INITIATOR.
 *                      - NULL
 *                      If initiator is from the vmk_ScsiCommand 
 *                      structure and type is \em VMK_SCSI_TASKMGMT_ABORT,
 *                      then cmdID.serialNumber can also be obtained
 *                      from vmk_ScsiCommand (cmdID.serialNumber)
 *                      else 0.
 *                      cmdID.serialNumber is only valid for abort.
 *                      It is ignored otherwise.
 * \param[in] worldId   World on behalf of whom the command was issued.
 *                      This argument is only valid for abort and
 *                      virtual reset.  It is ignored otherwise.
 *
 * \par Examples:
 * - Abort a specific I/O:
 *    \code
 *    vmk_ScsiInitTaskMgmt(task, VMK_SCSI_TASKMGMT_ABORT,
 *                         vmkCmd->cmdId, vmkCmd->worldId); 
 *    \endcode
 * - Abort all i/os issued by a single world:
 *    \code
 *    cmdId.initiator = VMK_SCSI_TASKMGMT_ANY_INITIATOR;
 *    cmdId.serialNumber = 0;
 *    vmk_ScsiInitTaskMgmt(task, VMK_SCSI_TASKMGMT_VIRT_RESET,
 *                         cmdId, worldId);
 *    \endcode
 * - Abort all i/os issued by a single world from a specific initiator:
 *    \code
 *    cmdId.initiator = originalInitiator:
 *    cmdId.serialNumber = 0;
 *    vmk_ScsiInitTaskMgmt(task, VMK_SCSI_TASKMGMT_VIRT_RESET,
 *                         cmdId, worldId);
 *    \endcode
 * - Abort all i/os issued by all worlds:
 *    \code
 *    cmdId.initiator = NULL;
 *    cmdId.serialNumber = 0;
 *    vmk_ScsiInitTaskMgmt(task, VMK_SCSI_TASKMGMT_LUN_RESET,
 *                         cmdId, worldId);
 *    \endcode
 *    The worldID should be the World ID of the reset issuer.
 *
 ***********************************************************************
 */
void vmk_ScsiInitTaskMgmt(
   vmk_ScsiTaskMgmt *vmkTask,
   vmk_ScsiTaskMgmtType type,
   vmk_ScsiCommandId cmdId,
   vmk_uint32 worldId);

/*
 ***********************************************************************
 * vmk_ScsiQueryTaskMgmt --                                       */ /**
 *
 * \ingroup SCSI
 *
 * \brief   Matches a SCSI command against a task management request
 *          filter, and returns the action to be taken for that command.
 *
 * The task management request filter is obtained by a call to
 * vmk_ScsiInitTaskMgmt()
 *
 * \param[in] vmkTaskMgmt     Task management request.
 * \param[in] cmd             Command to check.
 *
 * \retval VMK_SCSI_TASKMGMT_ACTION_IGNORE   No task management action to
 *                                           be taken for this command.
 * \retval VMK_SCSI_TASKMGMT_ACTION_ABORT    Complete this command with
 *                                           status 
 *                                           \em vmkTaskMgmt->\em status
 *
 ***********************************************************************
 */
vmk_ScsiTaskMgmtAction vmk_ScsiQueryTaskMgmt(
   const vmk_ScsiTaskMgmt *vmkTaskMgmt,
   const vmk_ScsiCommand *cmd);

/*
 ***********************************************************************
 * vmk_ScsiGetTaskMgmtTypeName --                                 */ /**
 *
 * \ingroup SCSI
 *
 * \brief Returns a human readable description of the task management
 *        request.
 *
 * \param[in] taskMgmtType    Task management type to convert to a
 *                            string.
 *
 * \return The description string.
 *
 ***********************************************************************
 */
const char *vmk_ScsiGetTaskMgmtTypeName(
   vmk_ScsiTaskMgmtType taskMgmtType);

/*
 ***********************************************************************
 * vmk_ScsiDebugDropCommand --                                    */ /**
 *
 * \ingroup SCSI
 *
 * \brief Tell whether a command should be dropped.
 *
 * This is used for fault injection.
 *
 * \param[in] adapter   Adapter the command would be issued to.
 * \param[in] cmd       Command to check.
 *
 * \retval VMK_TRUE     The command should be dropped.
 * \retval VMK_FALSE    The command should not be dropped.
 *
 * \note vmk_ScsiDebugDropCommand() always returns VMK_FALSE for release
 *       builds.
 *
 ***********************************************************************
 */
vmk_Bool
vmk_ScsiDebugDropCommand(vmk_ScsiAdapter *adapter, vmk_ScsiCommand *cmd);

/*
 ***********************************************************************
 * vmk_ScsiAdapterEvent --                                        */ /**
 *
 * \ingroup SCSI
 *
 * \brief Notifies the VMkernel of a specific event on the
 *        specified adapter.
 *
 * \param[in] adapter   Pointer to the adapter to signal the event on.
 * \param[in] eventType Event to signal.
 * 
 * \retval VMK_BUSY        Another adapter event is being processed.
 * \retval VMK_NO_MEMORY   Out of memory.
 *
 * \return  Any other error value indicates some internal error
 *          encountered while notifying VMkernel.
 *
 ***********************************************************************
 */
VMK_ReturnStatus
vmk_ScsiAdapterEvent(vmk_ScsiAdapter *adapter, vmk_uint32 eventType);

/*
 ***********************************************************************
 * vmk_ScsiAdapterIsPAECapable --                                 */ /**
 *
 * \ingroup SCSI
 *
 * \brief Determines if the adapter supports DMA beyond 32 bits of
 *        machine-address space.
 *
 * \param[in] adapter   Adapter to check.
 *
 * \retval VMK_TRUE     The adapter supports DMA beyond 32 bits.
 * \retval VMK_FALSE    The adapter is limited to DMA in the lower
 *                      32 bits of machine-address space
 *
 ***********************************************************************
 */
static inline vmk_Bool
vmk_ScsiAdapterIsPAECapable(vmk_ScsiAdapter *adapter)
{
   return adapter->paeCapable;
}

/*
 ***********************************************************************
 * vmk_ScsiCmdStatusIsGood --                                     */ /**
 *
 * \ingroup SCSI
 *
 * \brief Determine if a command status indicates a successful completion.
 *
 * \param[in] status    Status to check.
 *
 * \retval VMK_TRUE     The status indicates successful completion.
 * \retval VMK_FALSE    The status indicates some error. Use
 *                      vmk_ScsiCmdStatusIsCheck() and
 *                      vmk_ScsiExtractSenseData() to get the actual
 *                      error.
 *
 ***********************************************************************
 */
static inline vmk_Bool
vmk_ScsiCmdStatusIsGood(vmk_ScsiCmdStatus status)
{
   return (status.device == VMK_SCSI_DEVICE_GOOD &&
           status.host == VMK_SCSI_HOST_OK &&
           status.plugin == VMK_SCSI_PLUGIN_GOOD) ? VMK_TRUE : VMK_FALSE;
}

/*
 ***********************************************************************
 * vmk_ScsiCmdStatusIsResvConflict --                             */ /**
 *
 * \ingroup SCSI
 *
 * \brief Determine if a command status indicates a reservation
 *        conflict.
 *
 * \param[in] status    Status to check.
 *
 * \retval VMK_TRUE     The status is reservation conflict.
 * \retval VMK_FALSE    The status is not a reservation conflict.
 *
 ***********************************************************************
 */
static inline vmk_Bool
vmk_ScsiCmdStatusIsResvConflict(vmk_ScsiCmdStatus status)
{
   return (status.device == VMK_SCSI_DEVICE_RESERVATION_CONFLICT &&
           status.host == VMK_SCSI_HOST_OK &&
           status.plugin == VMK_SCSI_PLUGIN_GOOD);
}

/*
 ***********************************************************************
 * vmk_ScsiCmdStatusIsCheck --                                    */ /**
 *
 * \ingroup SCSI
 *
 * \brief Determine if a command status indicates a check condition.
 *
 * \param[in] status    Status to check.
 *
 * \retval VMK_TRUE     The status indicates a check condition.
 * \retval VMK_FALSE    The status is not a check condition.
 *
 ***********************************************************************
 */
static inline vmk_Bool
vmk_ScsiCmdStatusIsCheck(vmk_ScsiCmdStatus status)
{
   return (status.device == VMK_SCSI_DEVICE_CHECK_CONDITION &&
           status.host == VMK_SCSI_HOST_OK &&
           status.plugin == VMK_SCSI_PLUGIN_GOOD) ? VMK_TRUE : VMK_FALSE;
}

/*
 ***********************************************************************
 * vmk_ScsiGetLbaLbc --                                           */ /**
 *
 * \ingroup SCSI
 *
 * \brief Parse a SCSI CDB and pull out lba and lbc.
 *
 * Determine the lba and lbc for a given cdb.  This is most useful
 * for READ and WRITE cdb's.  This function will deal with converting
 * endianness & differences between the sizes of cdbs.
 *
 * \param[in]  cdb         SCSI cdb to parse.
 * \param[in]  cdbLen      Length of cdb in bytes.
 * \param[in]  devClass    SCSI Device Class.
 * \param[out] lba         Logical Block Address.
 * \param[out] lbc         Logical Block Count.
 *
 * \retval VMK_NOT_SUPPORTED     \em devClass is none of 
 *                               \em SCSI_CLASS_DISK, \em SCSI_CLASS_CDROM,
 *                               \em SCSI_CLASS_TAPE and 
 *                               \em IDE_CLASS_CDROM, or, \em devClass is
 *                               \em SCSI_CLASS_CDROM and \em cdb[0] does 
 *                               not contain 10/12 byte READ/WRITE SCSI
 *                               command opcode.
 * \retval VMK_BAD_PARAM         \em cdb[0] does not contain 6/10/12/16
 *                               byte READ/WRITE SCSI command opcode.
 *
 ***********************************************************************
 */
VMK_ReturnStatus
vmk_ScsiGetLbaLbc(unsigned char *cdb, vmk_size_t cdbLen,
                  vmk_ScsiDeviceClass devClass, vmk_uint64 *lba,
                  vmk_uint32 *lbc);

/*
 ***********************************************************************
 * vmk_ScsiSetLbaLbc --                                           */ /**
 *
 * \ingroup SCSI
 *
 * \brief Set lba and lbc fields in a given SCSI CDB.
 *
 * Set the lba and lbc for a given cdb. This is most useful
 * for READ and WRITE cdb's. This function will deal with converting
 * endianness & differences between the sizes of cdb's. Callers
 * must set \em cdb[0] before making this call.
 *
 * \param[in,out] cdb         SCSI cdb to set.
 * \param[in]     cdbLen      Length of cdb in bytes.
 * \param[in]     devClass    SCSI Device Class.
 * \param[out]    lba         Logical Block Address.
 * \param[out]    lbc         Logical Block Count.
 *
 * \retval VMK_NOT_SUPPORTED     \em devClass is none of 
 *                               \em SCSI_CLASS_DISK, 
 *                               \em SCSI_CLASS_CDROM and 
 *                               \em IDE_CLASS_CDROM, or,
 *                               \em devClass is \em SCSI_CLASS_CDROM and 
 *                               \em cdb[0] does not contain 10/12 byte
 *                               READ/WRITE SCSI command opcode.
 * \retval VMK_BAD_PARAM         \em cdb[0] does not contain 6/10/12/16
 *                               byte SCSI READ/WRITE command opcode.
 *
 ***********************************************************************
 */
VMK_ReturnStatus
vmk_ScsiSetLbaLbc(unsigned char *cdb, vmk_size_t cdbLen,
                  vmk_ScsiDeviceClass devClass, vmk_uint64 *lba,
                  vmk_uint32 *lbc);

/*
 ***********************************************************************
 * vmk_ScsiAllocateAdapter --                                     */ /**
 *
 * \ingroup SCSI
 *
 * \brief Allocate an adapter.
 *
 * After successful return, the adapter is in \em allocated state.
 * Use vmk_ScsiFreeAdapter() to free this adapter structure.
 *
 * \param[in] heapID    Heap to allocate the new adapter from.
 *
 * \return New adapter.
 * \retval NULL   Out of memory.
 *
 ***********************************************************************
 */
vmk_ScsiAdapter * vmk_ScsiAllocateAdapter(
   vmk_HeapID heapID);

/*
 ***********************************************************************
 * vmk_ScsiRegisterAdapter --                                     */ /**
 *
 * \ingroup SCSI
 *
 * \brief Register an adapter with the VMkernel.
 *
 * After successful return, the adapter is in \em enabled state and 
 * drivers can issue a scan on that adapter.
 *
 * If \em ScanOnDriverLoad config option is set and 
 * \em vmk_ScsiAdapter->\em flag
 * has \em VMK_SCSI_ADAPTER_FLAG_REGISTER_WITHOUT_SCAN bit clear, then
 * VMkernel will scan all the LUs connected to this adapter. Path 
 * claiming will be run which might result in new SCSI devices being 
 * registered with VMkernel.
 *
 * If you do not want this autoscan and claim process to happen, you can
 * set the \em VMK_SCSI_ADAPTER_FLAG_REGISTER_WITHOUT_SCAN in
 * \em vmk_ScsiAdapter->\em flag, before calling vmk_ScsiRegisterAdapter(). 
 * This can be useful for some drivers f.e. iSCSI, where a usual SCSI 
 * scan does not make sense.
 *
 * \note The scan and claim is done in a deferred context and hence might
 *       not have completed when vmk_ScsiRegisterAdapter() returns.
 * \note This function does not block.
 *
 * \param[in] adapter   Adapter to register. This should have been 
 *                      allocated with a prior call to 
 *                      vmk_ScsiAllocateAdapter().
 *
 * \retval VMK_OK          The adapter was successfully registered. Scan 
 *                         and path claim might be still running.
 * \retval VMK_EXISTS      An adapter with same name already registered.
 * \retval VMK_BAD_PARAM   One or more adapter fields are not initialized
 *                         correctly.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiRegisterAdapter(
   vmk_ScsiAdapter *adapter);

/*
 ***********************************************************************
 * vmk_ScsiUnregisterAdapter --                                   */ /**
 *
 * \ingroup SCSI
 *
 * \brief Unregister an adapter previously registered by
 *        vmk_ScsiRegisterAdapter().
 *
 * \note This function may block indefinitely, waiting for all the 
 *       references on the adapter to be released.
 *
 * \param[in] adapter   Adapter to unregister.
 *
 * \retval VMK_OK       Successfully unregistered. The adapter will go
 *                      to \em allocated state and can be used for a
 *                      subsequent vmk_ScsiRegisterAdapter() call.
 * \retval VMK_BUSY     One or more paths originating from this adapter
 *                      are claimed by some plugin.
 * \retval VMK_FAILURE  Adapter is neither in \em enabled, \em disabled
 *                      or \em allocated state. An appropriate message is
 *                      logged.
 * 
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiUnregisterAdapter(
   vmk_ScsiAdapter *adapter);

/*
 ***********************************************************************
 * vmk_ScsiFreeAdapter --                                         */ /**
 *
 * \ingroup SCSI
 *
 * \brief Free an adapter previously allocated by
 *        vmk_ScsiAllocateAdapter().
 * 
 * \warning Do not call vmk_ScsiFreeAdapter() for a registered adapter.
 *          The adapter should be in \em allocated state.
 *
 * \param[in]  adapter  Adapter to free.
 *
 ***********************************************************************
 */
void vmk_ScsiFreeAdapter(
   vmk_ScsiAdapter *adapter);

/*
 ***********************************************************************
 * vmk_ScsiRemovePath --                                          */ /**
 *
 * \ingroup SCSI
 *
 * \brief Destroy the path identified by \em adapter, \em channel, 
 *        \em targetId and \em lunId.
 *
 * If the path to be removed is claimed by an MP plugin, VMkernel will
 * first try to unclaim the path by calling the plugin's \em unclaim 
 * entrypoint.
 *
 * \note This function may block indefinitely, waiting for all the 
 *       references held on the path to be released.
 *
 * \param[in] adapter   Adapter for path to remove.
 * \param[in] channel   Channel for path to remove.
 * \param[in] targetId  Target ID for path to remove.
 * \param[in] lunId     LUN ID for path to remove.
 *
 * \retval VMK_TRUE     The path was removed successfully.
 * \retval VMK_FALSE    The path doesn't exist or the path exists but is
 *                      claimed by a plugin and could not be successfully 
 *                      unclaimed.
 *
 ***********************************************************************
 */
vmk_Bool vmk_ScsiRemovePath(
   vmk_ScsiAdapter *adapter,
   vmk_uint32 channel,
   vmk_uint32 targetId,
   vmk_uint32 lunId);


/*
 ***********************************************************************
 * vmk_ScsiRegisterIRQ --                                         */ /**
 *
 * \ingroup SCSI
 *
 * \brief Registers an adapter's interrupt handler and interrupt vector
 *        with the VMkernel for polling during a core dump.
 *
 *  To unregister, simply call vmk_ScsiRegisterIRQ() with NULL for 
 *  \em intrHandler.
 *
 * \param[in] adapter            Adapter to register on behalf of.
 * \param[in] intrVector         Interrupt vector number to register.
 * \param[in] intrHandler        Interrupt handler callback to invoke
 *                               when an interrupt needs to be serviced.
 * \param[in] intrHandlerData    Private data to pass to the handler.
 *
 * \return  vmk_ScsiRegisterIRQ() always succeeds.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ScsiRegisterIRQ(
   void *adapter,
   vmk_uint32 intrVector,
   vmk_InterruptHandler intrHandler,
   void *intrHandlerData);

/*    
 ***********************************************************************
 * vmk_ScsiHostStatusToString --                                  */ /**
 *
 * \ingroup SCSI
 *
 * \brief Take a SCSI host status and return a static string describing it.
 *
 * \param[in] hostStatus   Status to convert.
 *
 * \return Host status as a human readable string.
 *                            
 ***********************************************************************
 */   
char *
vmk_ScsiHostStatusToString(vmk_ScsiHostStatus hostStatus);

/*
 ***********************************************************************
 * vmk_ScsiDeviceStatusToString --                                */ /**
 *
 * \ingroup SCSI
 *
 * \brief Take a SCSI device status and return a static string describing
 *        it.
 *
 * \param[in] deviceStatus    Status to convert.
 *
 * \return Device status as a human readable string.
 *
 ***********************************************************************
 */
char *
vmk_ScsiDeviceStatusToString(vmk_ScsiDeviceStatus deviceStatus);

/*
 ***********************************************************************
 * vmk_ScsiSenseKeyToString --                                    */ /**
 *
 * \ingroup SCSI
 *
 * \brief Take a SCSI sense key and return a static string describing it.
 *
 * \param[in] senseKey   SCSI sense key to convert.
 *
 * \return Sense key as a human readable string.
 *
 ***********************************************************************
 */
char *
vmk_ScsiSenseKeyToString(vmk_uint32 senseKey);

/*
 ***********************************************************************
 * vmk_ScsiAdditionalSenseToString --                             */ /**
 *
 * \ingroup SCSI
 *
 * \brief Take a SCSI ASC/ASCQ and return a static string describing it.
 *
 * \param[in] asc    SCSI ASC value to convert.
 * \param[in] ascq   SCSI ASCQ value to convert.
 *
 * \return ASC/ASCQ as a human readable string.
 *
 ***********************************************************************
 */
char *
vmk_ScsiAdditionalSenseToString(vmk_uint32 asc,
                                vmk_uint32 ascq);

/*                                                                                        
 **********************************************************************
 * vmk_ScsiExtractSenseData --                                   */ /**
 *
 * \ingroup SCSI
 *
 * \brief Extract the SCSI Check Condition.
 *
 * Examine the contents of the senseBuffer and return the SCSI check
 * condtion key, the Additional Sense Code, and the Additional Sense
 * Code Qualifier.
 *
 * \note The routine only handles sense buffers with a response
 *       code of 0x70 or 0x71 (Section 4.5.3 in the SCSI-3 spec).
 *       It does not handle response codes of 0x72, 0x73 or any
 *       vendor specific response codes.
 *
 * \param[in]  senseBuffer    The sense buffer to be examined.
 * \param[out] sense_key      The sense key to be extracted.
 * \param[out] asc            The Additional Sense Code to be extracted.
 * \param[out] ascq           The Additional Sense Code Qualifier to be
 *                            extracted.
 *
 * \retval VMK_TRUE     The sense buffer indicates a SCSI error condition,
 *                      and sense_key, asc and ascq have been correctly
 *                      set.
 * \retval VMK_FALSE    There was no error indicated on the current or
 *                      previous command.
 *
 ***********************************************************************
 */
vmk_Bool
vmk_ScsiExtractSenseData(vmk_ScsiSenseData *senseBuffer,
                         vmk_uint8 *sense_key,
                         vmk_uint8 *asc,
                         vmk_uint8 *ascq);

/*
 ***********************************************************************
 * vmk_ScsiVPDPageSize --                                         */ /**
 *
 * \ingroup SCSI
 *
 * \brief Get the size of a VPD page.
 *
 * \param[in] response  SCSI VPD Inquiry response header. This is usually 
 *                      the SCSI Inquiry response obtained by a prior 
 *                      vmk_ScsiGetPathInquiry() call.
 *                                                                   
 * \return Size of VPD Page in bytes.
 *
 * \note The routine assumes that VPD length field is one byte for
 *       all pages except for page 83 which has 2 byte length.
 *
 ***********************************************************************
 */
vmk_uint16 
vmk_ScsiVPDPageSize(vmk_ScsiInquiryVPDResponse *response);

/*                                                                                        
 **********************************************************************
 * vmk_ScsiSetPathXferLimit --                                   */ /**
 *
 * \ingroup SCSI
 *
 * \brief Set the maximum single transfer size limit for a path.
 *                                                             
 * \param[in] vmkAdapter   The target adapter.
 * \param[in] channel      The target channel.
 * \param[in] target       The target number.
 * \param[in] lun          The target LUN.
 * \param[in] maxXfer      The new maximum transfer size in bytes.
 *
 * \retval VMK_OK          The max transfer size was set successfully.
 * \retval VMK_NOT_FOUND   The requested path was not found.
 *
 * \warning Do not set maximum transfer size to 0. This will result
 *          in undefined behaviour.
 *
 * \note    You should not set maximum transfer size to a value greater
 *          than the adapter's maximum transfer size. Most adapters 
 *          support a maximum transfer size of 256K, so it is safe to 
 *          set a \em maxXfer value <= 256K.
 *
 ***********************************************************************                  
 */
VMK_ReturnStatus
vmk_ScsiSetPathXferLimit(vmk_ScsiAdapter *vmkAdapter,
                         vmk_int32 channel,
                         vmk_int32 target,
                         vmk_int32 lun,
                         vmk_uint64 maxXfer);

/*                                                                                        
 **********************************************************************
 * vmk_ScsiModifyQueueDepth --                                   */ /**
 *
 * \ingroup SCSI
 *
 * \brief Set the queue depth of the path specified by \em vmkAdapter 
 *       \em channel, \em target, and \em lun.
 *
 * For multiple paths going through the same SCSI adapter, the path
 * queue depth controls the share of that path in the adapter's queue.
 * Setting path queue depth to a value greater than the underlying 
 * adapter's queue depth is inconsequential and should not be done.
 *        
 *
 * \param[in] vmkAdapter   The target adapter.
 * \param[in] channel      The target channel.
 * \param[in] target       The target number.
 * \param[in] lun          The target LUN.
 * \param[in] qdepth       The new queue depth value.
 *
 * \retval VMK_OK          The queue depth was set successfully.
 * \retval VMK_NOT_FOUND   The requested path was not found.
 *
 * \warning Do not set qdepth to 0. This will result in undefined 
 *          behaviour.
 *
 ***********************************************************************                  
 */
VMK_ReturnStatus
vmk_ScsiModifyQueueDepth(vmk_ScsiAdapter *vmkAdapter,
                         vmk_int32 channel,
                         vmk_int32 target,
                         vmk_int32 lun,
                         vmk_uint32 qdepth);

/*
 ***********************************************************************
 * vmk_ScsiAllFCPathsDown --                                      */ /**
 *
 * \brief Check if all Fibre Channel transport paths are dead.
 *
 * \retval VMK_TRUE  There are one or more Fibre Channel paths but all
 *                   of them are currently down.
 * \retval VMK_FALSE There are no Fibre Channel paths or one or more 
 *                   Fibre Channel paths are up.
 *
 ***********************************************************************
 */
vmk_Bool vmk_ScsiAllFCPathsDown(void);

/*
 ***********************************************************************
 * vmk_ScsiAdapterUniqueName --                                   */ /**
 *
 * \ingroup DeviceName
 * \brief Create a new unique adapter name.
 *
 * This function returns a new unique adapter name.
 *
 * \param[out] adapterName  Pointer to an array of characters at least
 *                          VMK_SCSI_ADAPTER_NAME_LENGTH long, to
 *                          receive the new adapter name.
 ***********************************************************************
 */
void vmk_ScsiAdapterUniqueName(char *adapterName);

#endif  /* _VMKAPI_SCSI_H_ */
/** @} */
/** @} */

