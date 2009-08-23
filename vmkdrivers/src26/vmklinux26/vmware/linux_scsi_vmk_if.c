/*
 * ****************************************************************
 * Portions Copyright 1998 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

/*
 * linux_scsi_vmk_if.c --
 *
 *      Interface functions that deal with vmkernel storage stack.
 */
#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_transport_sas.h>
#include <vmklinux26/vmklinux26_scsi.h>

#include "vmkapi.h"
#include "linux_stubs.h" 

#include "linux_scsi.h"
#include "linux_scsi_transport.h"
#include "linux_stress.h"
#include "linux_stubs.h"

/*
 *  Externs
 */
extern struct list_head	linuxBHCommandList;
extern vmk_BottomHalf linuxSCSIBHNum; 
extern vmk_SpinlockIRQ linuxSCSIBHLock;

/*
 *  Static
 */
static vmk_AdapterStatus SCSILinuxAdapterStatus(void *vmkAdapter);
static vmk_RescanLinkStatus SCSILinuxRescanLink(void *vmkAdapter);
static vmk_FcLinkSpeed FcLinuxLinkSpeed(void *vmkAdapter);
static vmk_uint64 FcLinuxAdapterPortName(void *vmkAdapter);
static vmk_uint64 FcLinuxAdapterNodeName(void *vmkAdapter);
static vmk_uint32 FcLinuxAdapterPortId(void *vmkAdapter);
static vmk_FcPortType FcLinuxAdapterPortType(void *vmkAdapter);
static vmk_FcPortState FcLinuxAdapterPortState(void *vmkAdapter);
static VMK_ReturnStatus FcLinuxTargetAttributes(void *vmkAdapter,
                                  vmk_FcTargetAttrs *vmkFcTarget,
                                  vmk_uint32 channelNo, vmk_uint32 targetNo);

static vmk_uint64 SasLinuxInitiatorAddress(void *pAdapter);
static VMK_ReturnStatus SasLinuxTargetAttributes(void *pAdapter,
				vmk_SasTargetAttrs *sasAttrib,
				vmk_uint32 channelNo, vmk_uint32 targetNo);

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxDiscover --
 *
 *      Create/Configure/Destroy a new paths
 *
 * Results:
 *      VMK_OK Success
 *      Other An error occured
 *      
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus
SCSILinuxDiscover(void *clientData, vmk_ScanAction action,
		  int channel, int target, int lun,
		  void **deviceData)         
{
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter = 
		(struct vmklnx_ScsiAdapter *) clientData; 
   struct Scsi_Host    *sh = vmklnx26ScsiAdapter->shost;
   vmk_ScsiAdapter    *vmkAdapter = vmklnx26ScsiAdapter->vmkAdapter;
   struct scsi_device  *sdev;
   VMK_ReturnStatus status;

   VMK_ASSERT(sh);
   VMK_ASSERT(sh->transportt);

   switch (action) {
   case VMK_SCSI_SCAN_CREATE_PATH: {
      vmk_LogDebug(vmklinux26Log, 5, "Create path %s:%d:%d:%d",
	  vmkAdapter->name, channel, target, lun);

      status = SCSILinuxCreatePath(sh, channel, target, lun, &sdev);

      if (status == VMK_OK) {
         break;
      } else {
         return status;
      }
      break;
   }

   case VMK_SCSI_SCAN_CONFIGURE_PATH: {
      vmk_LogDebug(vmklinux26Log, 5, "Keep path %s:%d:%d:%d \n",
	  vmkAdapter->name, channel, target, lun);

      status = SCSILinuxConfigurePath(sh, channel, target, lun);

      return status;
   }

   case VMK_SCSI_SCAN_DESTROY_PATH: {
      vmk_LogDebug(vmklinux26Log, 5, "Destroy path %s:%d:%d:%d \n",
	  vmkAdapter->name, channel, target, lun);

      status = SCSILinuxDestroyPath(sh, channel, target, lun);

      if (status == VMK_OK) {
	 sdev = NULL;
      } else {
	 return status;
      }
      break;
   }

   default:
      vmk_WarningMessage("Unknown path discover request %s:%d:%d:%d \n",
	  vmkAdapter->name, channel, target, lun);
      VMK_ASSERT(FALSE);
      return VMK_NOT_IMPLEMENTED;
   }

   *deviceData = (void *)sdev;

   return VMK_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxCommand --
 *
 *      Process a SCSI command.
 *
 * Results:
 *      VMK_OK if the IO was sent successfully.
 *      Error code otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus
SCSILinuxCommand(void *clientData, vmk_ScsiCommand *vmkCmdPtr, 
		 void *deviceData)
{
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter = 
		(struct vmklnx_ScsiAdapter *) clientData; 
   struct Scsi_Host    *sh = vmklnx26ScsiAdapter->shost;
   struct scsi_device *sdev = (struct scsi_device *) deviceData;

   if (sdev == NULL) {
      VMK_ASSERT(sdev);
      return VMK_FAILURE;
   }
   
   /* Never issue a command to the host ID */
   VMK_ASSERT(sdev->id != sh->this_id);

   if (VMKLNX_STRESS_DEBUG_COUNTER(stressScsiAdapterIssueFail)) {
      vmk_WarningMessage("%s - SCSI_ADAPTER_ISSUE_FAIL Stress Counter "
	"Triggered\n", __FUNCTION__);
      return VMK_WOULD_BLOCK;
   }
   
   return SCSILinuxQueueCommand(sh, sdev, vmkCmdPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxProcInfo --
 *
 *      Call proc handler for the given SCSI host adapter. 
 *
 * Results:
 *      VMK_OK on success,  VMK_FAILURE if proc handler fails.
 *      VMK_NOT_SUPPORTED if host template has no proc handler. 
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus 
SCSILinuxProcInfo(void *clientData, 
                  char* buf, 
                  uint32_t offset,
                  uint32_t count,
                  uint32_t *nbytes,
		  int isWrite)
{
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter = 
		(struct vmklnx_ScsiAdapter *) clientData; 
   struct Scsi_Host    *sh = vmklnx26ScsiAdapter->shost;

   if (sh->hostt) {
      if (sh->hostt->proc_info) {
	 char *start = buf;
	 vmk_LogDebug(vmklinux26Log, 5, "%s - off=%d count=%d\n", 
			__FUNCTION__, offset, count);  

	 VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(sh), *nbytes, sh->hostt->proc_info,
                    sh, buf, &start, offset, count, isWrite); 

	 VMK_ASSERT(start == buf || *nbytes == 0);

         if ((int32_t)*nbytes < 0) {
             vmk_LogDebug(vmklinux26Log, 5, "%s - failed %d", 
                          __FUNCTION__, *nbytes);
             return VMK_FAILURE;
         }

	 /* 
          * We don't support proc nodes that generate one page of data or more
          */
	 VMK_ASSERT(*nbytes <= count);
	 vmk_LogDebug(vmklinux26Log, 2, "%s - nbytes=%d\n", __FUNCTION__, 
		*nbytes);  
	 return VMK_OK;
      } else if (!isWrite) {
	 if (sh->hostt->info) {
            const char *tmp_info;
            VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(sh), tmp_info, sh->hostt->info, sh);
	    *nbytes = snprintf(buf, count, "%s\n", tmp_info);
	 } else {
	    *nbytes = snprintf(buf, count,
			       "The driver does not yet support the proc-fs\n");
	 }
	 buf[count - 1] = '\0';

	 /* 
          * We don't support proc nodes that generate one page of data or more
          */
	 VMK_ASSERT(*nbytes <= count);

	 if (*nbytes < offset) {
	    *nbytes = 0;
	 }
	 return VMK_OK;
      }
   }
   
   *nbytes = 0;
   return VMK_NOT_SUPPORTED;
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxClose --
 *
 *      Close the SCSI device.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
void 
SCSILinuxClose(void *clientData)
{
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter = 
		(struct vmklnx_ScsiAdapter *) clientData; 
   struct Scsi_Host    *sh = vmklnx26ScsiAdapter->shost;

   if (sh->hostt->release != NULL) {
      /*
       * This function should only be called if we are unloading the vmkernel.
       * The module cleanup code is going to release this scsi adapter 
       * also.  
       */
      vmk_LogDebug(vmklinux26Log, 0, "Releasing adapter\n");
      VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(sh), sh->hostt->release, sh);
   } else {
      vmk_LogDebug(vmklinux26Log, 0, "No release function. Manually unload\n");
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxIoctl --
 *
 *      Special ioctl commands.
 *
 * Results:
 *      VMK_OK - success, (Driver retval in out arg)
 *      VMK_NOT_SUPPORTED - no ioctl() for this driver. 
 *      VMK_INVALID_TARGET - coudln't find device<target,lun>,
 *      
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

VMK_ReturnStatus
SCSILinuxIoctl(void *clientData,    // IN:
               void *deviceData,    // IN:
               uint32_t fileFlags,    // IN: ignored for SCSI ioctls
               uint32_t cmd,          // IN: 
               vmk_VirtAddr userArgsPtr,      // IN/OUT:
               vmk_IoctlCallerSize callerSize,   // IN: ioctl caller size
               int32_t *drvErr)       // OUT: error returned by driver.
{
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter = 
		(struct vmklnx_ScsiAdapter *) clientData; 
   struct Scsi_Host    *sh = vmklnx26ScsiAdapter->shost;
   struct scsi_device *sdev = (struct scsi_device *) deviceData;
   int done;

   vmk_LogDebug(vmklinux26Log, 1, "%s -  start\n", __FUNCTION__); 

   if (sh == NULL
       || sh->hostt == NULL
       || sh->hostt->ioctl == NULL) {
      return VMK_NOT_SUPPORTED; /* most likely the third test failed. */
   }

   if (sdev == NULL) {
      return VMK_INVALID_TARGET; 
   }

   mutex_lock(&vmklnx26ScsiAdapter->ioctl_mutex);
   done = 0;
   if (callerSize == VMK_IOCTL_CALLER_32 && sh->hostt->compat_ioctl) {
      VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(sh), *drvErr, sh->hostt->compat_ioctl,
                 sdev, cmd, (void *)userArgsPtr);
      if (*drvErr != -ENOIOCTLCMD) {
         done = 1;
      }
   }
   if (!done) {
      VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(sh), *drvErr, sh->hostt->ioctl,
                 sdev, cmd, (void *)userArgsPtr);
   }
   mutex_unlock(&vmklnx26ScsiAdapter->ioctl_mutex);

   vmk_LogDebug(vmklinux26Log, 1, "%s - ret=%d\n", __FUNCTION__,  *drvErr); 

   return VMK_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxQueryDeviceQueueDepth
 *
 * Result:
 *     This returns the queue depth set by the device.
 *
 * Side effects:
 *     None
 *----------------------------------------------------------------------
 */
int
SCSILinuxQueryDeviceQueueDepth(void *clientData,
                               void *deviceData)
{
   struct scsi_device  *sdev = (struct scsi_device *) deviceData;

   VMK_ASSERT(sdev);

   /*
    * We pass in clientData (aka vmklnx_ScsiAdapter) just in case
    * we need it in the future.
    */
   clientData = clientData;

   return sdev ? sdev->queue_depth : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxModifyDeviceQueueDepth
 *
 *     If possible set a new queue depth.
 *
 * Result:
 *     This returns the queue depth set by the device.
 *
 * Side effects:
 *     None
 *----------------------------------------------------------------------
 */
int
SCSILinuxModifyDeviceQueueDepth(void *clientData,
                                int qDepth,
                                void *deviceData)
{
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter = 
		(struct vmklnx_ScsiAdapter *) clientData; 
   struct Scsi_Host    *sh = vmklnx26ScsiAdapter->shost;
   struct scsi_device  *sdev = (struct scsi_device *) deviceData;

   VMK_ASSERT(sdev);

   if (sdev == NULL) {
      return 0; 
   }

   /*
    * Check if the drivers can set the new queue depth
    * else, return the current queue depth
    */
   if (sh->hostt->change_queue_depth) {
      int ret;

      /*
       * Most drivers will turn around and call scsi_adjust_queue_depth here
       * So be careful about reentrant code
       */
      VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(sh), ret, sh->hostt->change_queue_depth,
              sdev, qDepth);
   }

   return sdev->queue_depth;
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxDumpCommand --
 *
 *      Queue up a SCSI command when dumping.
 *
 * Results:
 *      VMK_OK - cmd queued;
 *      VMK_FAILURE - couldn't queue cmd;
 *      VMK_WOULD_BLOCK - device queue full or host not accepting cmds;
 *      VMK_BUSY - eh active. 
 *
 * Side effects:
 *      No one else can issue commands to this device anymore.
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus
SCSILinuxDumpCommand(void *clientData,
		     vmk_ScsiCommand *vmkCmdPtr, 
		     void *deviceData)
{
   struct vmklnx_ScsiAdapter *vmklnx26_ScsiAdapter = 
		(struct vmklnx_ScsiAdapter *) clientData; 
   struct Scsi_Host    *sh = vmklnx26_ScsiAdapter->shost;
   struct scsi_cmnd *scmd;
   struct scsi_device *sdev = (struct scsi_device *) deviceData;
   unsigned long flags;
   int status = 0;
   static struct vmklnx_ScsiIntCmd vmklnx26_ScsiIntCmd;

   VMK_ASSERT(sdev);

   if (sdev == NULL) {
      vmk_AlertMessage("Invalid sdev ptr during dump\n"); ;
      return VMK_FAILURE; 
   }

   scmd = &vmklnx26_ScsiIntCmd.scmd;
   memset(&vmklnx26_ScsiIntCmd, 0, sizeof(vmklnx26_ScsiIntCmd));
   /*
    * Update the vmkCmdPtr
    */
   vmklnx26_ScsiIntCmd.vmkCmdPtr = vmkCmdPtr;

   /*
    * Fill in values required for command structure
    * Fill in vmkCmdPtr with vmklnx26_ScsiIntCmd pointer
    * Fill scsi_done with InternalCmdDone routine
    */
   SCSILinuxInitInternalCommand(sdev, &vmklnx26_ScsiIntCmd);

   /* Never issue a command to the host ID */
   VMK_ASSERT(sdev->id != sh->this_id);

   spin_lock_irqsave(sh->host_lock, flags);

   sdev->dumpActive = 1;

   /*
    * First, make sure dumpCmd can be queued. 
    */
   if (scsi_host_in_recovery(sh)) {
      vmk_AlertMessage("%s - In error recovery - %d \n",
                       __FUNCTION__, sh->shost_state);
   }

   if (sh->host_self_blocked 
       || (sdev->device_busy == sdev->queue_depth) 
       || (sh->host_busy == sh->can_queue)) {
      vmk_LogDebug(vmklinux26Log, 0, "%s - devcif=%u depth=%u hostcif=%u "
	"can_queue=%d self_blocked=%u\n",
	  __FUNCTION__,
          sdev->device_busy, sdev->queue_depth,
          sh->host_busy, sh->can_queue,
          sh->host_self_blocked);
      spin_unlock_irqrestore(sh->host_lock, flags);
      return VMK_WOULD_BLOCK;
   }

   ++scmd->device->host->host_busy;
   ++sdev->device_busy;
   spin_unlock_irqrestore(sh->host_lock, flags);

   /* 
    * should never fail unless someone changes SCSI_Dump
    */
   VMK_ASSERT(vmkCmdPtr->sgArray->nbElems == 1);

   /*
    * We should only get one element here
    */
   memcpy(scmd->cmnd, vmkCmdPtr->cdb, vmkCmdPtr->cdbLen);
   scmd->request_bufflen = vmkCmdPtr->sgArray->elem[0].length;
   scmd->request_bufferMA = (dma_addr_t)vmkCmdPtr->sgArray->elem[0].addr;
   scmd->request_buffer = phys_to_virt(scmd->request_bufferMA);
   scmd->use_sg = 0;
   scmd->cmd_len = COMMAND_SIZE(scmd->cmnd[0]);
   scmd->vmkCmdPtr = (void *)&vmklnx26_ScsiIntCmd;
   scmd->underflow = scmd->request_bufflen;
   scmd->sc_data_direction = SCSILinuxGetDataDirection(scmd,
   						vmkCmdPtr->dataDirection);

   /*
    * Mark this as Dump Request
    */
   spin_lock_irqsave(&scmd->vmklock, flags);
   scmd->vmkflags = VMK_FLAGS_NEED_CMDDONE | VMK_FLAGS_DUMP_REQUEST;
   spin_unlock_irqrestore(&scmd->vmklock, flags);

   spin_lock_irqsave(sh->host_lock, flags);
   VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(sh), status, sh->hostt->queuecommand, 
		      scmd, SCSILinuxInternalCommandDone);

   spin_unlock_irqrestore(sh->host_lock, flags);
   if (unlikely(status)) {
      vmk_AlertMessage("%s - queuecommand failed with status = 0x%x %s \n",
               __FUNCTION__, status, vmk_StatusToString(status));
      return VMK_FAILURE;
   } else {
      return VMK_OK;
   }
}

#define  VM_NAME_ARRAY_FILL_CHAR    0xFF


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxVportOp --
 * 
 * Entry point in vmklinux module for doing virtual link ops.
 * Converts arguments to the appropriate form for the NPIV aware
 * driver, and calls the driver's fc_transport NPIV handler entry
 * points as needed. The interface used is based on Linux 2.6.23.
 *
 * Results:
 *      VMK_OK - success, (Driver retval in out arg)
 *      VMK_NOT_SUPPORTED - no NPIV support for this driver. 
 *      VMK_FAILURE - for all driver related errors
 *
 * Side effects:
 *      When executing the VMK_VPORT_CREATE command, the driver will
 *      call scsi_add_host() to add the VPORT.
 *      When executing the VMK_VPORT_DELETE command, the driver will
 *      call scsi_remove_host() to remove the VPORT.
 *
 *----------------------------------------------------------------------
 */

VMK_ReturnStatus
SCSILinuxVportOp(void *clientData,    // IN:
		 uint32_t cmd,          // IN: 
		 void *args,          // IN/OUT:
		 int *drvErr)         // OUT: error returned by driver.
{
   struct Scsi_Host *shp, *shost;
   vmk_ScsiVportArgs *vmkargs;
   struct vmk_VportData data;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   vmk_ScsiAdapter *vmkAdapter;

   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *)clientData;
   VMK_ASSERT(vmklnx26ScsiAdapter);
   shp = vmklnx26ScsiAdapter->shost;
   VMK_ASSERT(shp);
   vmkargs = (vmk_ScsiVportArgs *) args;
   VMK_ASSERT(vmkargs);

   vmk_LogDebug(vmklinux26Log, 5, "SCSILinuxVportOp: client=%p, host=%p, cmd=0x%x, uargs=%p\n", 
          clientData, shp, cmd, (void *)vmkargs);

   if (shp == NULL
       || shp->transportt == NULL ||
       shp->adapter == NULL) {
      *drvErr = -1;
      return VMK_NOT_SUPPORTED;
   }

   // need further checks for NPIV to work
   vmkAdapter = vmklnx26ScsiAdapter->vmkAdapter;
   VMK_ASSERT(vmkAdapter);
   if (vmkAdapter->mgmtAdapter.transport != VMK_STORAGE_ADAPTER_FC) {
      *drvErr = -1;
      return VMK_NOT_SUPPORTED;
   }

   switch (cmd) {
   case VMK_VPORT_CREATE:
      {
      struct   vmk_ScsiAdapter   *aPhys, *aVport;

      vmk_LogDebug(vmklinux26Log, 2, "SCSILinuxVportOp: VMK_VPORT_CREATE: data=%p, args=%p\n",
            (void *)&data, (void *)vmkargs->arg);
      memset(&data, 0, sizeof(struct vmk_VportData));
      memcpy(&data.node_name, &vmkargs->wwnn, sizeof(vmkargs->wwnn));
      memcpy(&data.port_name, &vmkargs->wwpn, sizeof(vmkargs->wwpn));
      if ((vmkargs->name != NULL) &&
            ((unsigned char)vmkargs->name[0] != VM_NAME_ARRAY_FILL_CHAR)) {
         strncpy((char *)&data.symname, vmkargs->name, sizeof(data.symname) - 1);
      }
      // not used yet...
      data.role_id = VMK_VPORT_ROLE_FCP_INITIATOR;

      // finally set the api version for drivers use
      data.api_version = VMK_VPORT_API_VERSION;

      *drvErr = vmk_fc_vport_create(shp, 0, scsi_get_device(shp), &data);
      if (*drvErr) {
         goto handle_driver_error;
      }

      vmkargs->virthost = data.vport_shost;
      VMK_ASSERT(vmkargs->virthost);

      aPhys = vmklnx26ScsiAdapter->vmkAdapter;  // Physical hba
      shost = (struct Scsi_Host *)vmkargs->virthost;
      vmklnx26ScsiAdapter = shost->adapter; // Vport's lnxscsi adapter
      aVport = vmklnx26ScsiAdapter->vmkAdapter;  // Vport hba
      vmkargs->virtAdapter = aVport;
      
      // set the pae flag to match the physical hba
      aVport->paeCapable = aPhys->paeCapable;

      // set the sg fields from the physical hba
      aVport->sgElemSizeMult = aPhys->sgElemSizeMult;
      aVport->sgElemAlignment = aPhys->sgElemAlignment;

      scsi_scan_host(data.vport_shost);

      }
      break;
   case VMK_VPORT_DELETE:
      vmk_LogDebug(vmklinux26Log, 2, "SCSILinuxVportOp: VMK_VPORT_DELETE: virthost=%p, shp=%p\n",
            (void *)vmkargs->virthost, (void *)shp);
      *drvErr = vmk_fc_vport_delete(vmkargs->virthost);
      break;
   case VMK_VPORT_INFO:
      {
      void     *host;
      if (vmkargs->virthost) {
         // use virtual host if passed
         host = vmkargs->virthost;
      }
      else {
         // otherwise, use the physical host
         host = shp;
      }
      vmk_LogDebug(vmklinux26Log, 5, "SCSILinuxVportOp: VMK_VPORT_INFO: host=%p, args=%p\n",
            (void *)host, (void *)vmkargs->arg);
      // the returned structure is already allocated by the vmkernel
      *drvErr = vmk_fc_vport_getinfo(host, vmkargs->arg);
      vmk_LogDebug(vmklinux26Log, 5, "SCSILinuxVportOp: drvErr %x\n", *drvErr);
      }
      break;
   default:
      *drvErr = -ENOENT;
      return VMK_NOT_SUPPORTED;
   }

handle_driver_error:
   switch(*drvErr) {
      case  0:
         return VMK_OK;
      case  -ENOENT:
         // NPIV not support by this driver
         return VMK_NOT_SUPPORTED;
      case  -ENOMEM:
         // out of memory
         return VMK_NOT_SUPPORTED;
      case  -ENOSPC:
         // no more vports
         return VMK_NOT_SUPPORTED;
      default:
         return VMK_FAILURE;
   }
 
   return VMK_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxVportUpdate
 *
 * Stub called by scsi_add_host to handle updating the vport with
 * scsi host data
 *
 *----------------------------------------------------------------------
 */
void SCSILinuxVportUpdate(void *clientdata, void *device)
{
   struct Scsi_Host *sh;
   struct device *dev;
   struct fc_vport *vport;

   sh = (struct Scsi_Host *) clientdata;
   dev = (struct device *)device;

   vport = dev_to_vport(dev);
   vport->vhost = sh;
}

#define VPORT_DISCOVERY_RETRY_COUNT 10
#define VPORT_DISCOVERY_DELAY_COUNT 2000000   // delay in us = 2 sec

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxVportDiscover
 *
 * Special front-end for SCSIScanPaths used to scan LUNs on a VPORT and
 * delete vport related paths
 *
 * Result:
 *    See SCSIScanPaths
 *
 * Side Effects:
 *    Creates the LUN path if it is not found
 *    vmkAdapter is set to the correct pointer
 *    deviceData is set to the vmklnx_ScsiAdapter pointer
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus 
SCSILinuxVportDiscover(void *clientData, vmk_ScanAction action,
                       int ctlr, int devNum, int lun,
                       vmk_ScsiAdapter **vmkAdapter,
                       void **deviceData)
{
   struct Scsi_Host *hostPtr;
   vmk_ScsiAdapter *adapter;
   struct scsi_device  *dp;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   VMK_ReturnStatus status;
   uint32_t retryCount = 0;
   unsigned long flags;

   // Scsi_host pointer is passed
   hostPtr = (struct Scsi_Host *)clientData;
   VMK_ASSERT(hostPtr);
   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *)hostPtr->adapter;
   VMK_ASSERT(vmklnx26ScsiAdapter);
   adapter = vmklnx26ScsiAdapter->vmkAdapter;
   VMK_ASSERT(adapter);

   switch (action) {
   case VMK_SCSI_SCAN_CREATE_PATH: {

      vmk_LogDebug(vmklinux26Log, 2, "SCSILinuxVportDiscover: client=%p, host=%p, adapter=%p\n", 
             clientData, hostPtr, adapter);

      vmk_LogDebug(vmklinux26Log, 1, "VPORT Adapter name=%s [%p]\n",adapter->name, adapter); 
      if (vmkAdapter) {
         *vmkAdapter = adapter;
      }

      /*
       * scsi_device_lookup takes a reference on the scsi_device and that
       * reference is released as part of VMK_SCSI_SCAN_DESTROY_PATH for
       * vport devices
       */
      if ((devNum != VMK_SCSI_PATH_ANY_TARGET) && (lun != VMK_SCSI_PATH_ANY_LUN)) {
         dp = scsi_device_lookup(hostPtr, ctlr, devNum, lun);
         if (dp) {
            if (deviceData) {
               *deviceData = dp;
            }
            // don't scan if the lun already exist
            return VMK_OK;
         }
      }

      vmk_WorldAssertIsSafeToBlock();

      while (((status = vmk_ScsiScanPaths(adapter->name, ctlr, devNum, lun)) ==
              VMK_BUSY) && (retryCount++ < VPORT_DISCOVERY_RETRY_COUNT)) {
         vmk_WorldSleep(VPORT_DISCOVERY_DELAY_COUNT);
         vmk_LogDebug(vmklinux26Log,1, "Retrying vmk_ScsiScanPaths %dth" 
               " time on VPORT Adapter name=%s [%p]",
            retryCount, adapter->name, adapter);
      }

      if ((status == VMK_BUSY) && (retryCount >= VPORT_DISCOVERY_RETRY_COUNT)) {
         vmk_WarningMessage("Failed Discovery on VPORT Adapter name=%s [%p] with error: %d", 
                adapter->name, adapter, status);
         return status;
      }

      if ((devNum != VMK_SCSI_PATH_ANY_TARGET) && (lun != VMK_SCSI_PATH_ANY_LUN)) {
         // If this is for a specific LUN/TARGET, see if LUN is really found
         dp = scsi_device_lookup(hostPtr, ctlr, devNum, lun);
         if (dp) {
            if (deviceData) {
               *deviceData = dp;
            }
            return VMK_OK;
         }
      }
      else {
         return VMK_OK;
      }

      // LUN was not found, return failure
      break;
   }

   case VMK_SCSI_SCAN_DESTROY_PATH: {
      vmk_LogDebug(vmklinux26Log, 2, "Destroy vport path %s:%d:%d:%d deviceData=%p\n",
          adapter->name, ctlr, devNum, lun, *deviceData);

      spin_lock_irqsave(hostPtr->host_lock, flags);
      dp = __scsi_device_lookup(hostPtr, ctlr, devNum, lun);
      spin_unlock_irqrestore(hostPtr->host_lock, flags);
      if (dp && (*deviceData != NULL)) {
         /*
          * Release reference that was taken while doing SCSI_SCAN_CREATE_PATH
          * operation as part of vport discovery.
          */
         scsi_device_put(dp);
      }
      status = SCSILinuxDestroyPath(hostPtr, ctlr, devNum, lun);
      return status;
      break;
   }

   case VMK_SCSI_SCAN_CONFIGURE_PATH:
   // This is not used, added to make compiler happy
   default:
      vmk_WarningMessage("Unknown path discover request %s:%d:%d:%d \n",
          adapter->name, ctlr, devNum, lun);
      VMK_ASSERT(FALSE);
      return VMK_NOT_IMPLEMENTED;
   }

   // Return failure if we reach here
   return VMK_FAILURE;
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxTaskMgmt --
 *
 *      Process a task managemnet request.
 *
 * Results:
 *      VMK_OK
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus
SCSILinuxTaskMgmt(void *clientData, vmk_ScsiTaskMgmt *vmkTaskMgmt,
		  void *deviceData) 
{
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter = 
		(struct vmklnx_ScsiAdapter *) clientData; 
   struct Scsi_Host    *sh = vmklnx26ScsiAdapter->shost;
   struct scsi_device *sdev = (struct scsi_device *) deviceData;
   VMK_ReturnStatus status = VMK_FAILURE;
   unsigned long flags;
   VMK_ASSERT(sdev);

   /* Never issue a command to the host ID */
   VMK_ASSERT(sdev->id != sh->this_id);

   vmk_LogDebug(vmklinux26Log, 4, "%s - Start processing %s of "
	"initiator:%p S/N:%#"VMK_FMT64"x on "
       SCSI_PATH_NAME_FMT, __FUNCTION__,
       vmk_ScsiGetTaskMgmtTypeName(vmkTaskMgmt->type),
       vmkTaskMgmt->cmdId.initiator, vmkTaskMgmt->cmdId.serialNumber,
       SCSI_PATH_NAME(sh, sdev->channel, sdev->id, sdev->lun));

   switch (vmkTaskMgmt->type) {
   case VMK_SCSI_TASKMGMT_ABORT:
      status = SCSILinuxAbortCommands(sh, sdev, vmkTaskMgmt);
      break;

   case VMK_SCSI_TASKMGMT_LUN_RESET:
   case VMK_SCSI_TASKMGMT_DEVICE_RESET:
   case VMK_SCSI_TASKMGMT_BUS_RESET:
       /* 
        * Set the state to recovery, some of the drivers dont like to
        * receive commands at this time. See PR236958 for more info
        */
       atomic_inc(&vmklnx26ScsiAdapter->tmfFlag); 

       spin_lock_irqsave(sh->host_lock, flags);
       sh->shost_state = SHOST_RECOVERY;
       spin_unlock_irqrestore(sh->host_lock, flags);

      /*
       * Destructive reset
       */
      status = SCSILinuxResetCommand(sh, sdev, vmkTaskMgmt);
      /* 
       * Set the state back to normal 
       */
      if (atomic_dec_and_test(&vmklnx26ScsiAdapter->tmfFlag)) {

         spin_lock_irqsave(sh->host_lock, flags);
         sh->shost_state = SHOST_RUNNING;
         spin_unlock_irqrestore(sh->host_lock, flags);

      }
      break;

   case VMK_SCSI_TASKMGMT_VIRT_RESET:
      status = SCSILinuxAbortCommands(sh, sdev, vmkTaskMgmt);
      break;

   default:
      vmk_WarningMessage("%s - Unknown task management type 0x%x", 
			__FUNCTION__, vmkTaskMgmt->type);
      VMK_ASSERT(FALSE);
   }

   vmk_LogDebug(vmklinux26Log, 4, "%s - Done\n", __FUNCTION__);

   return status;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxDumpQueue --
 *
 *      Dump Queue details.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
void
SCSILinuxDumpQueue(void *clientData)
{
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter = 
		(struct vmklnx_ScsiAdapter *) clientData; 
   struct Scsi_Host    *sh = vmklnx26ScsiAdapter->shost;
   struct scsi_device *sdev;
   struct scsi_cmnd *scmd;
   unsigned long flags, cmdFlags;

   VMK_ASSERT(sh);

   spin_lock_irqsave(sh->host_lock, flags);
   shost_for_each_device(sdev, sh) {
      spin_lock_irqsave(&sdev->list_lock, cmdFlags);
      list_for_each_entry(scmd, &sdev->cmd_list, list) {
         vmk_LogDebug(vmklinux26Log, 3, "%p", scmd);
      }
      spin_unlock_irqrestore(&sdev->list_lock, cmdFlags);
   }
   spin_unlock_irqrestore(sh->host_lock, flags);
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxCheckTarget --
 *
 *      Check if a Target exists
 *
 * Results:
 *      VMK_OK if the Target exists.
 *      Error code otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus
SCSILinuxCheckTarget(void *clientData,
      int channelNo, int targetNo)
{
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter = 
		(struct vmklnx_ScsiAdapter *) clientData; 
   struct Scsi_Host    *shost;
   unsigned long        flags;
   struct scsi_target  *sTarget;

   VMK_ASSERT(vmklnx26ScsiAdapter);

   shost = vmklnx26ScsiAdapter->shost;
   VMK_ASSERT(shost);

   spin_lock_irqsave(shost->host_lock, flags);
   sTarget = vmklnx_scsi_find_target(shost, channelNo, targetNo);
   spin_unlock_irqrestore(shost->host_lock, flags);

   if (!sTarget) {
      vmk_LogDebug(vmklinux26Log, 5, "%s - No Valid targets bound\n", 
	__FUNCTION__);  
      return VMK_FAILURE;
   }

   return VMK_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * FcLinuxLinkSpeed
 *
 *     Get FC Link Speed
 *
 * Result:
 *
 *     VMK_FC_SPEED_UNKNOWN
 *     VMK_FC_SPEED_1GBIT
 *     VMK_FC_SPEED_2GBIT
 *     VMK_FC_SPEED_4GBIT
 *     VMK_FC_SPEED_10GBIT
 *----------------------------------------------------------------------
 */
static vmk_FcLinkSpeed
FcLinuxLinkSpeed(void *clientData)
{
   vmk_FcLinkSpeed speed = VMK_FC_SPEED_UNKNOWN;
   vmk_ScsiAdapter    *vmkAdapter = (vmk_ScsiAdapter *)clientData;;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   struct fc_internal *i;
   struct fc_host_attrs *fc_host;
   struct Scsi_Host    *shost;

   VMK_ASSERT(vmkAdapter);

   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *) vmkAdapter->clientData; 
   VMK_ASSERT(vmklnx26ScsiAdapter);

   shost = vmklnx26ScsiAdapter->shost;
   VMK_ASSERT(shost);

   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);

   i = to_fc_internal(shost->transportt);
   VMK_ASSERT(i);

   if(i->f->get_host_speed) {
      VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), 
		i->f->get_host_speed, shost);

      switch (fc_host->speed) {
         case FC_PORTSPEED_1GBIT:
            speed = VMK_FC_SPEED_1GBIT;
            break;
         case FC_PORTSPEED_2GBIT:
            speed = VMK_FC_SPEED_2GBIT;
            break;
         case FC_PORTSPEED_4GBIT:
            speed = VMK_FC_SPEED_4GBIT;
            break;
         case FC_PORTSPEED_10GBIT:
            speed = VMK_FC_SPEED_10GBIT;
            break;
         case FC_PORTSPEED_8GBIT:
            speed = VMK_FC_SPEED_8GBIT;
            break;
         case FC_PORTSPEED_16GBIT:
            speed = VMK_FC_SPEED_16GBIT;
            break;
         case FC_PORTSPEED_NOT_NEGOTIATED:
         case FC_PORTSPEED_UNKNOWN:
         default:
            speed = VMK_FC_SPEED_UNKNOWN;
      } 
   }
   return speed;
}


/*
 *----------------------------------------------------------------------
 *
 * FcLinuxAdapterPortName
 *
 *     Get FC Adapter Port Name
 *
 * Result:
 *     Return 64 bit port name
 *----------------------------------------------------------------------
 */
static vmk_uint64
FcLinuxAdapterPortName(void *clientData)
{
   vmk_ScsiAdapter    *vmkAdapter = (vmk_ScsiAdapter *)clientData;;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   struct fc_host_attrs *fc_host;
   struct Scsi_Host    *shost;

   VMK_ASSERT(vmkAdapter);

   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *) vmkAdapter->clientData; 
   VMK_ASSERT(vmklnx26ScsiAdapter);

   shost = vmklnx26ScsiAdapter->shost;
   VMK_ASSERT(shost);

   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);

   return (fc_host->port_name);
}

/*
 *----------------------------------------------------------------------
 *
 * FcLinuxAdapterPortId
 *
 *     Get FC Adapter Port Id
 *
 * Result:
 *     Return 32 bit port id
 *----------------------------------------------------------------------
 */
static vmk_uint32
FcLinuxAdapterPortId(void *clientData)
{
   vmk_uint32 portId = 0;
   vmk_ScsiAdapter    *vmkAdapter = (vmk_ScsiAdapter *)clientData;;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   struct fc_host_attrs *fc_host;
   struct fc_internal *i;
   struct Scsi_Host    *shost;

   VMK_ASSERT(vmkAdapter);

   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *) vmkAdapter->clientData; 
   VMK_ASSERT(vmklnx26ScsiAdapter);

   shost = vmklnx26ScsiAdapter->shost;
   VMK_ASSERT(shost);

   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);

   i = to_fc_internal(shost->transportt);
   VMK_ASSERT(i);

   if (i->f->get_host_port_id) {
      VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), 
		i->f->get_host_port_id, shost);
      portId = fc_host->port_id;
   }

   return portId;
}

/*
 *----------------------------------------------------------------------
 *
 * FcLinuxAdapterNodeName
 *
 *     Get FC Adapter Node Name
 *
 * Result:
 *     Return 64 bit node name
 *----------------------------------------------------------------------
 */
static vmk_uint64
FcLinuxAdapterNodeName(void *clientData)
{
   vmk_ScsiAdapter    *vmkAdapter = (vmk_ScsiAdapter *)clientData;;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   struct fc_host_attrs *fc_host;
   struct Scsi_Host    *shost;

   VMK_ASSERT(vmkAdapter);

   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *) vmkAdapter->clientData; 
   VMK_ASSERT(vmklnx26ScsiAdapter);

   shost = vmklnx26ScsiAdapter->shost;
   VMK_ASSERT(shost);

   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);

   return (fc_host->node_name);
}

/*
 *----------------------------------------------------------------------
 *
 * FcLinuxAdapterPortType
 *
 *     Get FC Adapter Port Type
 *
 * Result:
 *     VMK_FC_PORTTYPE_UNKNOWN
 *     VMK_FC_PORTTYPE_OTHER
 *     VMK_FC_PORTTYPE_NOTPRESENT
 *     VMK_FC_PORTTYPE_NPORT		
 *     VMK_FC_PORTTYPE_NLPORT	
 *     VMK_FC_PORTTYPE_LPORT
 *     VMK_FC_PORTTYPE_PTP
 *----------------------------------------------------------------------
 */
static vmk_FcPortType
FcLinuxAdapterPortType(void *clientData)
{
   vmk_FcPortType portType = VMK_FC_PORTTYPE_UNKNOWN;
   vmk_ScsiAdapter    *vmkAdapter = (vmk_ScsiAdapter *)clientData;;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   struct fc_internal *i;
   struct fc_host_attrs *fc_host;
   struct Scsi_Host    *shost;

   VMK_ASSERT(vmkAdapter);

   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *) vmkAdapter->clientData; 
   VMK_ASSERT(vmklnx26ScsiAdapter);

   shost = vmklnx26ScsiAdapter->shost;
   VMK_ASSERT(shost);

   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);

   i = to_fc_internal(shost->transportt);
   VMK_ASSERT(i);

   if (i->f->get_host_port_type) {
      VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), 
	i->f->get_host_port_type, shost);

      switch (fc_host->port_type) {
         case FC_PORTTYPE_OTHER:
            portType = VMK_FC_PORTTYPE_OTHER;
            break;
         case FC_PORTTYPE_NOTPRESENT:
            portType = FC_PORTTYPE_NOTPRESENT;
            break;
         case FC_PORTTYPE_NPORT:
            portType = VMK_FC_PORTTYPE_NPORT;
            break;
         case FC_PORTTYPE_NLPORT:
            portType = VMK_FC_PORTTYPE_NLPORT;
            break;
         case FC_PORTTYPE_LPORT:
            portType = VMK_FC_PORTTYPE_LPORT;
            break;
         case FC_PORTTYPE_PTP:
            portType = VMK_FC_PORTTYPE_PTP;
            break;
         case FC_PORTTYPE_NPIV:
            portType = VMK_FC_PORTTYPE_NPIV;
            break;
         case FC_PORTTYPE_UNKNOWN:
         default:
            portType = VMK_FC_PORTTYPE_UNKNOWN;
      } 
   }
   return portType;
}

/*
 *----------------------------------------------------------------------
 *
 * FcLinuxAdapterPortState
 *
 *     Get FC Adapter Port State
 *
 * Result:
 *     VMK_FC_PORTSTATE_UNKNOWN
 *     VMK_FC_PORTSTATE_NOTPRESENT
 *     VMK_FC_PORTSTATE_ONLINE
 *     VMK_FC_PORTSTATE_OFFLINE		
 *     VMK_FC_PORTSTATE_BLOCKED
 *     VMK_FC_PORTSTATE_BYPASSED
 *     VMK_FC_PORTSTATE_DIAGNOSTICS
 *     VMK_FC_PORTSTATE_LINKDOWN
 *     VMK_FC_PORTSTATE_ERROR
 *     VMK_FC_PORTSTATE_LOOPBACK
 *     VMK_FC_PORTSTATE_DELETED
 *----------------------------------------------------------------------
 */
static vmk_FcPortState
FcLinuxAdapterPortState(void *clientData)
{
   vmk_FcPortType portState = VMK_FC_PORTSTATE_UNKNOWN;
   vmk_ScsiAdapter    *vmkAdapter = (vmk_ScsiAdapter *)clientData;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   struct fc_internal *i;
   struct fc_host_attrs *fc_host;
   struct Scsi_Host    *shost;

   VMK_ASSERT(vmkAdapter);

   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *) vmkAdapter->clientData; 
   VMK_ASSERT(vmklnx26ScsiAdapter);

   shost = vmklnx26ScsiAdapter->shost;
   VMK_ASSERT(shost);

   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);

   i = to_fc_internal(shost->transportt);
   VMK_ASSERT(i);

   if (i->f->get_host_port_state) {
      VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), 
		i->f->get_host_port_state, shost);

      switch (fc_host->port_state) {
         case FC_PORTSTATE_NOTPRESENT:
            portState = VMK_FC_PORTSTATE_NOTPRESENT;
            break;
         case FC_PORTSTATE_ONLINE:
            portState = VMK_FC_PORTSTATE_ONLINE;
            break;
         case FC_PORTSTATE_OFFLINE:
            portState = VMK_FC_PORTSTATE_OFFLINE;
            break;
         case FC_PORTSTATE_BLOCKED:
            portState = VMK_FC_PORTSTATE_BLOCKED;
            break;
         case FC_PORTSTATE_BYPASSED:
            portState = VMK_FC_PORTSTATE_BYPASSED;
            break;
         case FC_PORTSTATE_DIAGNOSTICS:
            portState = VMK_FC_PORTSTATE_DIAGNOSTICS;
            break;
         case FC_PORTSTATE_LINKDOWN:
            portState = VMK_FC_PORTSTATE_LINKDOWN;
            break;
         case FC_PORTSTATE_ERROR:
            portState = VMK_FC_PORTSTATE_ERROR;
            break;
         case FC_PORTSTATE_LOOPBACK:
            portState = VMK_FC_PORTSTATE_LOOPBACK;
            break;
         case FC_PORTSTATE_DELETED:
            portState = VMK_FC_PORTSTATE_DELETED;
            break;
         case FC_PORTSTATE_UNKNOWN:
         default:
            portState = VMK_FC_PORTSTATE_UNKNOWN;
      } 
   }
   return portState;
}

/*
 *----------------------------------------------------------------------
 *
 * FcLinuxTargetAttributes
 *
 *     Get FC Target Attributes
 *
 * Result:
 *     Return FC Transport information for the specified target
 *----------------------------------------------------------------------
 */
static VMK_ReturnStatus
FcLinuxTargetAttributes(void *clientData, vmk_FcTargetAttrs *vmkFcTarget, 
vmk_uint32 channelNo, vmk_uint32 targetNo)
{
   struct scsi_target *sTarget;
   vmk_ScsiAdapter    *vmkAdapter = (vmk_ScsiAdapter *)clientData;;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   struct Scsi_Host    *shost;
   struct fc_internal *i; 
   unsigned long flags;
   struct fc_rport *rport;

   VMK_ASSERT(vmkAdapter);

   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *) vmkAdapter->clientData; 
   VMK_ASSERT(vmklnx26ScsiAdapter);

   shost = vmklnx26ScsiAdapter->shost;
   VMK_ASSERT(shost);

   i = to_fc_internal(shost->transportt);
   VMK_ASSERT(i);

   spin_lock_irqsave(shost->host_lock, flags);
   sTarget = vmklnx_scsi_find_target(shost, channelNo, targetNo);
   spin_unlock_irqrestore(shost->host_lock, flags);

   if (!sTarget) {
      vmk_LogDebug(vmklinux26Log, 0, "%s - No Valid FC targets bound\n", 
	__FUNCTION__);  
      return VMK_FAILURE;
   }

   rport = starget_to_rport(sTarget); 
   if (!rport) {
      vmk_LogDebug(vmklinux26Log, 0, "%s - No Valid rports found\n", 
	__FUNCTION__);  
      return VMK_FAILURE;
   }

   vmkFcTarget->nodeName = rport->node_name;
   vmkFcTarget->portName = rport->port_name;
   vmkFcTarget->portId = rport->port_id;

   return VMK_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxAdapterStatus
 *
 *     Get adapter status
 *
 * Result:
 *
 *     VMK_ADAPTER_STATUS_UNKNOWN - Unknown adapter Status
 *     VMK_ADAPTER_STATUS_ONLINE - Adapter is Online and working
 *     VMK_ADAPTER_STATUS_OFFLINE - Adapter is not capable of accepting new 
 *				    commands
 *----------------------------------------------------------------------
 */
static vmk_AdapterStatus
SCSILinuxAdapterStatus(void *vmkAdapter)
{
   return VMK_ADAPTER_STATUS_ONLINE;
   /* 
    * For now return as it is. We will have to add this entry point new 
    */
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxRescanLink
 *
 *     Rescan Link
 *
 * Result:
 *
 *     VMK_RESCAN_LINK_UNSUPPORTED - Rescanning the link not supported
 *     VMK_RESCAN_LINK_SUCCEEDED - Rescan succeeded
 *     VMK_RESCAN_LINK_FAILED - Rescan failed
 *----------------------------------------------------------------------
 */
static vmk_RescanLinkStatus
SCSILinuxRescanLink(void *clientData)
{
   return VMK_RESCAN_LINK_SUCCEEDED;
   /* 
    * For now return as it is. Newer kernels > 2.6.18 do support
    * link scan. If so, we can call them later
    */
}

/*
 *----------------------------------------------------------------------
 *
 * FcLinuxAttachMgmtAdapter
 *
 *     Attach FC mgmt Adapter structure
 *
 * Result:
 *     Attach to FC transport. Returns  0 on SUCCESS 
 *----------------------------------------------------------------------
 */
int
FcLinuxAttachMgmtAdapter(struct Scsi_Host *shost)
{
   vmk_FcAdapter *pFcAdapter;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter = 
		(struct vmklnx_ScsiAdapter *) shost->adapter; 
   vmk_ScsiAdapter *vmkAdapter;

   VMK_ASSERT(shost);
   VMK_ASSERT(vmklnx26ScsiAdapter);
   
   vmkAdapter = vmklnx26ScsiAdapter->vmkAdapter;
   VMK_ASSERT(vmkAdapter);

   pFcAdapter = 
	(vmk_FcAdapter *)VMKLinux26_Alloc(sizeof(vmk_FcAdapter));
   if (!pFcAdapter) {
      vmk_WarningMessage("No memory for FC adapter struct\n");
      return -ENOMEM;
   }

   pFcAdapter->getFcAdapterStatus = SCSILinuxAdapterStatus;
   pFcAdapter->rescanFcLink = SCSILinuxRescanLink;
   pFcAdapter->getFcNodeName = FcLinuxAdapterNodeName;
   pFcAdapter->getFcPortName = FcLinuxAdapterPortName;
   pFcAdapter->getFcPortId = FcLinuxAdapterPortId;
   pFcAdapter->getFcLinkSpeed = FcLinuxLinkSpeed;
   pFcAdapter->getFcPortType = FcLinuxAdapterPortType; 
   pFcAdapter->getFcPortState = FcLinuxAdapterPortState; 
   pFcAdapter->getFcTargetAttributes = FcLinuxTargetAttributes;

   vmkAdapter->mgmtAdapter.t.fc = pFcAdapter;

   vmkAdapter->mgmtAdapter.transport = VMK_STORAGE_ADAPTER_FC;

   return 0; 
}

/*
 *----------------------------------------------------------------------
 *
 * FcLinuxReleaseMgmtAdapter
 *
 *     Free up the FC mgmt Adapter structure
 *
 * Result:
 *     Unregister the FC transport
 *----------------------------------------------------------------------
 */
void
FcLinuxReleaseMgmtAdapter(struct Scsi_Host *shost)
{
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   vmk_ScsiAdapter *vmkAdapter;

   VMK_ASSERT(shost);

   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *) shost->adapter; 
   VMK_ASSERT(vmklnx26ScsiAdapter);
   
   vmkAdapter = vmklnx26ScsiAdapter->vmkAdapter;
   VMK_ASSERT(vmkAdapter);

   if ( vmkAdapter->mgmtAdapter.transport == VMK_STORAGE_ADAPTER_FC ) {
      /*
       * Make sure we mark this template as useless from now on
       */
      vmkAdapter->mgmtAdapter.transport = 
		VMK_STORAGE_ADAPTER_TRANSPORT_UNKNOWN;
      VMKLinux26_Free(vmkAdapter->mgmtAdapter.t.fc);
   }

   return;
}

/**
 *
 *  \globalfn find_initiator_sas_address - get the lowest initiator sas address 
 *
 *  \param dev a pointer to a struct device 
 *  \param data a pointer to a u64 sas address
 *  \return 0  
 *  \par Include:
 *  scsi/scsi_transport_sas.h
 *  \par ESX Deviation Notes:
 *  None.
 *  \sa None.
 *  \comments -
 *
 */
static int
find_initiator_sas_address(struct device *dev, void *data)
{
   if (dev &&  data && (SAS_PHY_DEVICE_TYPE == dev->dev_type)) {
      u64 * pSasAddr = (u64 *) data;
      struct sas_phy *phy = dev_to_phy(dev);
      if (phy && (phy->identify.sas_address < (*pSasAddr))) {
         *pSasAddr = phy->identify.sas_address;
      }
   }
   return 0;
}

/**
 *
 *  \globalfn SasLinuxInitiatorAddress -- return inititator sas address 
 *
 *  \param pAdapter a pointer to a struct vmk_ScsiAdapter
 *  \return lowest initiator sas address or 0 if none 
 *  \par Include:
 *  scsi/scsi_transport_sas.h
 *  \par ESX Deviation Notes:
 *  None.
 *  \sa None.
 *  \comments -
 *
 */
vmk_uint64
SasLinuxInitiatorAddress(void *pAdapter)
{
   struct Scsi_Host   *shost;
   vmk_ScsiAdapter    *vmkAdapter;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   vmk_uint64 initiatorAddress = 0;

   vmkAdapter = (vmk_ScsiAdapter *)pAdapter;
   if (NULL == vmkAdapter) {
      return(initiatorAddress);
   }
   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *)vmkAdapter->clientData;
   if (NULL == vmklnx26ScsiAdapter) {
      return(initiatorAddress);
   }
   shost = vmklnx26ScsiAdapter->shost;
   if (NULL == shost) {
      return(initiatorAddress);
   }
   initiatorAddress = ~0; 
   device_for_each_child(&shost->shost_gendev,
                        (void *)&initiatorAddress, find_initiator_sas_address); 
   if((~0) == initiatorAddress) {
      /* In case not found, set initiator sas address to 0 */
      int ret = FAILED;
      struct sas_internal *i = to_sas_internal(shost->transportt);
      VMK_ASSERT(i);
      initiatorAddress = 0; 
      if (i->f && i->f->get_initiator_sas_identifier) {
         /*
          * This provides a workaround for hpsa SAS driver and
          * it should be removed when HP driver has real SAS transport support
          */
         VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(shost), ret,
                i->f->get_initiator_sas_identifier, shost, (u64 *)&initiatorAddress);
      }
   }
   return(initiatorAddress);
}

/**
 *
 *  \globalfn SasLinuxTargetAttributes -- retrieve target attributes 
 *
 *  \param pAdapter a pointer to a struct vmk_ScsiAdapter
 *  \param sasAttrib a pointer to vmk_SasTargetAttrs 
 *  \param channelNo  a channel number 
 *  \param targetNo  a target number 
 *  \return VMK_ReturnStatus 
 *  \par Include:
 *  scsi/scsi_transport_sas.h
 *  \par ESX Deviation Notes:
 *  None.
 *  \sa None.
 *  \comments -
 *
 */
VMK_ReturnStatus
SasLinuxTargetAttributes(void *pAdapter, vmk_SasTargetAttrs *sasAttrib,
                         vmk_uint32 channelNo, vmk_uint32 targetNo)
{
   unsigned long      flags;
   struct Scsi_Host   *shost;
   struct sas_internal *i;
   struct scsi_target *starget;
   vmk_ScsiAdapter    *vmkAdapter;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   int ret = FAILED;

   if ((NULL == pAdapter) || (NULL == sasAttrib)) {
      return(VMK_FAILURE);
   }
   memset(sasAttrib, 0, sizeof(vmk_SasTargetAttrs));
   vmkAdapter = (vmk_ScsiAdapter *)pAdapter;
   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *)vmkAdapter->clientData;
   if ((NULL == vmklnx26ScsiAdapter) || (NULL == vmklnx26ScsiAdapter->shost)) {
      return(VMK_FAILURE);
   }
   shost = vmklnx26ScsiAdapter->shost;
   spin_lock_irqsave(shost->host_lock, flags);   
   starget = vmklnx_scsi_find_target(shost, channelNo, targetNo);
   spin_unlock_irqrestore(shost->host_lock, flags);
   if (NULL == starget) {
      return(VMK_FAILURE);
   }
   i = to_sas_internal(shost->transportt);
   VMK_ASSERT(i);

   /* Let each individual driver to provide SAS target information */
   if (i->f && i->f->get_target_sas_identifier) {
      VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(shost), ret,
                i->f->get_target_sas_identifier, starget, (u64 *)&sasAttrib->sasAddress);
   }
   if (SUCCESS != ret) {
      vmk_WarningMessage("ret=0x%x - Cannot get SAS target identifier for Channel=%d Target=%d. Please make sure driver can provide SAS target information\n", ret, channelNo, targetNo);
      return(VMK_FAILURE);
   } else {
      return(VMK_OK);
   }   
}

/*
 *----------------------------------------------------------------------
 *
 * SasLinuxAttachMgmtAdapter
 *
 *     Attach SAS mgmt Adapter structure
 *
 * Result:
 *     Attach to SAS transport. Returns  0 on SUCCESS 
 *----------------------------------------------------------------------
 */
int
SasLinuxAttachMgmtAdapter(struct Scsi_Host *shost)
{
   vmk_SasAdapter *pSasAdapter;
   vmk_ScsiAdapter *vmkAdapter;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;

   VMK_ASSERT(shost);
   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *) shost->adapter; 
   VMK_ASSERT(vmklnx26ScsiAdapter);
   vmkAdapter = vmklnx26ScsiAdapter->vmkAdapter;
   VMK_ASSERT(vmkAdapter);
   pSasAdapter = (vmk_SasAdapter *)VMKLinux26_Alloc(sizeof(vmk_SasAdapter));
   if (NULL == pSasAdapter) {
      vmk_WarningMessage("No memory for SAS management adapter struct allocation\n");
      return -ENOMEM;
   }

   pSasAdapter->getInitiatorSasAddress = SasLinuxInitiatorAddress;
   pSasAdapter->getSasTargetAttributes = SasLinuxTargetAttributes;
   vmkAdapter->mgmtAdapter.t.sas = pSasAdapter;
   vmkAdapter->mgmtAdapter.transport = VMK_STORAGE_ADAPTER_SAS;
   return 0; 
}

/*
 *----------------------------------------------------------------------
 *
 * SasLinuxReleaseMgmtAdapter
 *
 *     Free up the SAS mgmt Adapter structure
 *
 * Result:
 *     Unregister the SAS transport
 *----------------------------------------------------------------------
 */
void
SasLinuxReleaseMgmtAdapter(struct Scsi_Host *shost)
{
   vmk_ScsiAdapter *vmkAdapter;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;

   VMK_ASSERT(shost);
   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *) shost->adapter; 
   VMK_ASSERT(vmklnx26ScsiAdapter);
   vmkAdapter = vmklnx26ScsiAdapter->vmkAdapter;
   VMK_ASSERT(vmkAdapter);

   if (VMK_STORAGE_ADAPTER_SAS == vmkAdapter->mgmtAdapter.transport) {
      /*
       * Make sure we mark this template as useless from now on
       */
      vmkAdapter->mgmtAdapter.transport = VMK_STORAGE_ADAPTER_TRANSPORT_UNKNOWN;
      VMKLinux26_Free(vmkAdapter->mgmtAdapter.t.sas);
   }
   return;
}

/*
 *----------------------------------------------------------------------
 *
 * XsanLinuxInitiatorID
 *
 *     Return generic SAN inititator ID
 *
 * Result:
 *     Return generic SAN transport initiator ID in the initiatorID
 *----------------------------------------------------------------------
 */
static VMK_ReturnStatus
XsanLinuxInitiatorID(void *pAdapter, vmk_XsanID *initiatorID)
{
   struct Scsi_Host   *shost;
   vmk_ScsiAdapter    *vmkAdapter;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   struct xsan_internal *i;

   vmkAdapter = (vmk_ScsiAdapter *)pAdapter;
   if (NULL == vmkAdapter) {
      return VMK_FAILURE;
   }
   if (NULL == initiatorID) {
      return VMK_FAILURE;
   }
   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *)vmkAdapter->clientData;
   if (NULL == vmklnx26ScsiAdapter) {
      return VMK_FAILURE;
   }
   shost = vmklnx26ScsiAdapter->shost;
   if (NULL == shost) {
      return VMK_FAILURE;
   }

   i = to_xsan_internal(shost->transportt);
   VMK_ASSERT(i);

   if (i->f && i->f->get_initiator_xsan_identifier) {
      int ret = FAILED;

      VMK_ASSERT_ON_COMPILE(sizeof(vmk_XsanID) == sizeof(struct xsan_id));
      VMK_ASSERT_ON_COMPILE(offsetof(vmk_XsanID, L) == offsetof(struct xsan_id, L));
      VMK_ASSERT_ON_COMPILE(offsetof(vmk_XsanID, H) == offsetof(struct xsan_id, H));

      VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(shost), ret,
         i->f->get_initiator_xsan_identifier, shost, (struct xsan_id *)initiatorID);
      if (SUCCESS != ret) {
         vmk_WarningMessage(
            "ret=0x%x - Cannot get generic SAN initiator identifier for %s\n",
            ret, vmkAdapter->name);
         return VMK_FAILURE;
      }
   }
   return VMK_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * XsanLinuxTargetAttributes
 *
 *     Retrieve target attributes for generic SAN
 *
 * Result:
 *     Return generic SAN transport information for the specified target
 *     in the xsanAttrib.
 *----------------------------------------------------------------------
 */
static VMK_ReturnStatus
XsanLinuxTargetAttributes(void *pAdapter, vmk_XsanTargetAttrs *xsanAttrib,
                          vmk_uint32 channelNo, vmk_uint32 targetNo)
{
   unsigned long      flags;
   struct Scsi_Host   *shost;
   struct xsan_internal *i;
   struct scsi_target *starget;
   vmk_ScsiAdapter    *vmkAdapter;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   int ret = FAILED;

   if (NULL == pAdapter || NULL == xsanAttrib) {
      return  VMK_FAILURE;
   }
   memset(xsanAttrib, 0, sizeof(vmk_XsanTargetAttrs));
   vmkAdapter = (vmk_ScsiAdapter *)pAdapter;
   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *)vmkAdapter->clientData;
   if (NULL == vmklnx26ScsiAdapter || NULL == vmklnx26ScsiAdapter->shost) {
      return VMK_FAILURE;
   }
   shost = vmklnx26ScsiAdapter->shost;
   VMK_ASSERT(shost);
   spin_lock_irqsave(shost->host_lock, flags);   
   starget = vmklnx_scsi_find_target(shost, channelNo, targetNo);
   spin_unlock_irqrestore(shost->host_lock, flags);
   if (NULL == starget) {
      return VMK_FAILURE;
   }
   i = to_xsan_internal(shost->transportt);
   VMK_ASSERT(i);

   /* Let each individual driver to provide generic SAN target information */
   if (i->f && i->f->get_target_xsan_identifier) {
      VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(shost), ret,
         i->f->get_target_xsan_identifier, starget, (struct xsan_id *)&xsanAttrib->id);
   }
   if (SUCCESS != ret) {
      vmk_WarningMessage(
         "ret=0x%x - Cannot get generic SAN target identifier for Channel=%d Target=%d."
         " Please make sure driver can provide generic SAN target information\n",
         ret, channelNo, targetNo);
      return VMK_FAILURE;
   }   
   return VMK_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * XsanLinuxAttachMgmtAdapter
 *
 *     Attach generic SAN mgmt Adapter structure
 *
 * Result:
 *     Attach to generic SAN transport. Returns  0 on SUCCESS 
 *----------------------------------------------------------------------
 */
int
XsanLinuxAttachMgmtAdapter(struct Scsi_Host *shost)
{
   vmk_XsanAdapter *pXsanAdapter;
   vmk_ScsiAdapter *vmkAdapter;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;

   VMK_ASSERT(shost);
   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *) shost->adapter; 
   VMK_ASSERT(vmklnx26ScsiAdapter);
   vmkAdapter = vmklnx26ScsiAdapter->vmkAdapter;
   VMK_ASSERT(vmkAdapter);
   pXsanAdapter = (vmk_XsanAdapter *)VMKLinux26_Alloc(sizeof(vmk_XsanAdapter));
   if (NULL == pXsanAdapter) {
      vmk_WarningMessage(
         "No memory for generic SAN adapter struct allocation\n");
      return -ENOMEM;
   }

   pXsanAdapter->getXsanInitiatorID = XsanLinuxInitiatorID;
   pXsanAdapter->getXsanTargetAttributes = XsanLinuxTargetAttributes;
   vmkAdapter->mgmtAdapter.t.xsan = pXsanAdapter;
   vmkAdapter->mgmtAdapter.transport = VMK_STORAGE_ADAPTER_XSAN;
   return 0; 
}

/*
 *----------------------------------------------------------------------
 *
 * XsanLinuxReleaseMgmtAdapter
 *
 *     Free up the generic SAN mgmt Adapter structure
 *
 * Result:
 *     Unregister the generic SAN transport
 *----------------------------------------------------------------------
 */
void
XsanLinuxReleaseMgmtAdapter(struct Scsi_Host *shost)
{
   vmk_ScsiAdapter *vmkAdapter;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;

   VMK_ASSERT(shost);
   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *) shost->adapter; 
   VMK_ASSERT(vmklnx26ScsiAdapter);
   vmkAdapter = vmklnx26ScsiAdapter->vmkAdapter;
   VMK_ASSERT(vmkAdapter);

   if (VMK_STORAGE_ADAPTER_XSAN == vmkAdapter->mgmtAdapter.transport) {
      /*
       * Make sure we mark this template as useless from now on
       */
      vmkAdapter->mgmtAdapter.transport = VMK_STORAGE_ADAPTER_TRANSPORT_UNKNOWN;
      VMKLinux26_Free(vmkAdapter->mgmtAdapter.t.xsan);
   }
   return;
}

