/* ****************************************************************
 * Portions Copyright 1998 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

/******************************************************************
 *
 *  linux_scsi.c
 *
 *      vmklinux26 main entry points and scsi utility functions
 *
 * From linux-2.4.31/drivers/scsi/scsi_error.c:
 *
 * Copyright (C) 1997 Eric Youngdale
 *
 ******************************************************************/

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_tcq.h>
#include <linux/pci.h>

#include "vmkapi.h"
#include "linux_stubs.h" 
#include "linux_scsi.h"
#include "linux_stress.h"
#include "linux_pci.h"

/*
 *  Static Local Functions
 ********************************************************************
 */
static int SCSILinuxTryBusDeviceReset(struct scsi_cmnd * SCpnt, int timeout);
static int SCSILinuxTryBusReset(struct scsi_cmnd * SCpnt);
static void SCSILinuxProcessStandardInquiryResponse(struct scsi_cmnd *scmd);
static void SCSILinuxBH(void *clientData);
static VMK_ReturnStatus SCSILinuxComputeSGArray(struct scsi_cmnd *scmd, 
						vmk_ScsiCommand *vmkCmdPtr);
static VMK_ReturnStatus SCSILinuxAbortCommand(struct Scsi_Host *sh, 
					      struct scsi_cmnd *cmdPtr, 
					      int32_t abortReason);
static void SCSIProcessCmdTimedOut(void *data);

/*
 * Globals
 ********************************************************************
 */


/*
 * Vmklinux26 has a single BH to process completed commands
 * The completed commands are put into list and protected by lock
 */
vmk_BottomHalf 		scsiLinuxBH; 

typedef struct scsiLinuxTLS {
  /*
   * isrDoneCmds is a per-PCPU list of completions; we enqueue/dequeue
   * from this list with local interrupts disabled.
   */
  struct list_head isrDoneCmds;
  /*
   * bhDoneCmds is a per-PCPU list of completions, only accessed at BH
   * time. So we can access it without locks.
   */
  struct list_head bhDoneCmds;
  /*
   * runningBH is a per-PCPU flag to indicate whether the BH is currently
   * scheduled to run, or actually running.
   */
  vmk_Bool runningBH;
  char pad[0] VMK_ATTRIBUTE_L1_ALIGNED; /* effectively pad to 128 bytes */
} scsiLinuxTLS_t;

scsiLinuxTLS_t *scsiLinuxTLS[NR_CPUS] VMK_ATTRIBUTE_L1_ALIGNED;

/*
 * SCSI Adapter list and lock associated with this
 */
struct list_head	linuxSCSIAdapterList;
vmk_SpinlockIRQ 	linuxSCSIAdapterLock;

/*
 * Command Serial Number
 *
 * DO NOT access this directly. Instead, use SCSILinuxGetSerialNumber()
 */
vmk_atomic64 SCSILinuxSerialNumber = 1;

/* 
 * Stress option handles
 */
vmk_StressOptionHandle stressScsiAdapterIssueFail;
static vmk_StressOptionHandle stressVmklnxDropCmdScsiDone;
static vmk_StressOptionHandle stressVmklinuxAbortCmdFailure;

/*
 * Work Queue used to destroy individual adapter instance
 * and handle I/O timeouts.
 */
struct workqueue_struct *linuxSCSIWQ;
struct work_struct linuxSCSIIO_work;

/*
 * Externs
 ********************************************************************
 */
extern struct mutex host_cmd_pool_mutex;

/*
 * Heap for allocating commands for all paths
 * As a system ESX has to support 32K commands. 
 * This is the min heap size that is used for calculation
 * How do we get this number - 
 * Each Scsi_cmd takes ~500(448 but rounding it off) bytes. 
 * Multiply this with 32K commands that need to be supported
 * Calculation is 500(scsi_cmnd)*32(Queue Depth)*1024(Physical Paths)
 * Rounding off 16MB to 20
 ********************************************************************
 */
#define VMKLNX_SCSICMD_HEAP        "vmklnxScsiCmdHeap"
#define VMKLNX_SCSI_CMD_HEAP_MIN   (20*1024*1024)
#define VMKLNX_SCSI_CMD_HEAP_MAX   (50*1024*1024)
vmk_HeapID vmklnxScsiCmdHeap = VMK_INVALID_HEAP_ID;

/*
 * The following heap is used for allocation of SG tables
 * Drivers that use fast path(read FC, SAS and iSCSI) should be modified
 * to read sg array from vmk_ScsiCmd. They will not use this heap
 * Only slow path drivers use sg array allocated from this pool. 
 *
 * What is the minimum number of adapters that can fit with this heap?
 * Assuming each adapter has queue depth of 512
 * Assume each command takes MAX_SG elements
 * Each SG takes 32 bytes. So we end up with 4+MB
 * With 10MB we can support 2 such adapters easily
 * Note - Most local adapters have much lesser queue depth and IO transfer
 * capability(Some even have a Queue Depth of 1) In the long run, we should
 * convert all drivers to use vmk_ScsiCmd for performance and memory reasons
 */
#define VMKLNX_SCSISG_HEAP        "vmklnxScsiSgHeap"
#define VMKLNX_SCSI_SG_HEAP_MIN   (1024*1024)
#define VMKLNX_SCSI_SG_HEAP_MAX   (32*1024*1024)
vmk_HeapID vmklnxScsiSgHeap = VMK_INVALID_HEAP_ID;

/* Dummy allocs to pre-grow sg heap to 10MB */
#define VMKLNX_SCSI_SG_HEAP_DUMMY_ALLOCS   6

/* Throttle timeout */
#define THROTTLE_TO               (15 * 60)	// 15 mins

/*
 * Implementation
 ********************************************************************
 */

/*
 *----------------------------------------------------------------------
 *
 * SCSILinux_Init
 *
 *      This is the init entry point for SCSI. Called from vmklinux26 
 *    init from linux_stubs.c
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
void
SCSILinux_Init(void)
{
   VMK_ReturnStatus status;
   int i;
   void *sg_dummy[VMKLNX_SCSI_SG_HEAP_DUMMY_ALLOCS];

   status = vmk_SPCreateIRQ(&linuxSCSIAdapterLock, vmklinuxModID, 
			"scsiHostListLck", NULL, VMK_SP_RANK_IRQ_LEAF);

   if (status != VMK_OK) {
      vmk_AlertMessage("%s - Failed to initialize Adapter Spinlock\n",
	 __FUNCTION__);
      VMK_ASSERT(status == VMK_OK);
   }

   /* vmklnxScsiCmdHeap will be VMK_INVALID_HEAP_ID on failure */
   status = vmk_HeapCreate(VMKLNX_SCSICMD_HEAP,
                           VMKLNX_SCSI_CMD_HEAP_MIN,
                           VMKLNX_SCSI_CMD_HEAP_MAX,
                           VMK_HEAP_PHYS_ANY_CONTIGUITY,
                           VMK_HEAP_ANY_MEM, /* VMK_HEAP_LOW_MEM needed?? */
                           &vmklnxScsiCmdHeap);
   VMK_ASSERT(status == VMK_OK);

   /* vmklnxScsiSgHeap will be VMK_INVALID_HEAP_ID on failure */
   status = vmk_HeapCreate(VMKLNX_SCSISG_HEAP,
                           VMKLNX_SCSI_SG_HEAP_MIN,
                           VMKLNX_SCSI_SG_HEAP_MAX,
                           VMK_HEAP_PHYS_ANY_CONTIGUITY,
                           VMK_HEAP_ANY_MEM,
                           &vmklnxScsiSgHeap);

   VMK_ASSERT(status == VMK_OK);

   /* Pre-grow sg heap to 10MB - by doing dummy allocs and frees - pr# 348385 */
   for (i = 0; i < VMKLNX_SCSI_SG_HEAP_DUMMY_ALLOCS; i++) {
      sg_dummy[i] = vmk_HeapAlloc(vmklnxScsiSgHeap, VMKLNX_SCSI_SG_HEAP_MIN);
      VMK_ASSERT(sg_dummy[i]);
   }
   for (i = 0; i < VMKLNX_SCSI_SG_HEAP_DUMMY_ALLOCS; i++) {
      vmk_HeapFree(vmklnxScsiSgHeap, sg_dummy[i]);
   }

   VMK_ASSERT_ON_COMPILE(sizeof(scsiLinuxTLS_t) == VMK_L1_CACHELINE_SIZE);
   for (i = vmk_NumPCPUs()-1; i >= 0; i--) {
      scsiLinuxTLS_t *tls;

      tls = scsiLinuxTLS[i] = vmklnx_kmalloc_align(VMK_MODULE_HEAP_ID,
                                                   sizeof(*tls),
                                                   VMK_L1_CACHELINE_SIZE);
      if (tls == NULL) {
         vmk_Panic("Unable to allocate per-PCPU storage in vmklinux");
      }

      memset(tls, 0, sizeof *tls);
      INIT_LIST_HEAD(&tls->isrDoneCmds);
      INIT_LIST_HEAD(&tls->bhDoneCmds);
   }

   INIT_LIST_HEAD(&linuxSCSIAdapterList);

   status = vmk_BottomHalfRegister(SCSILinuxBH, NULL, &scsiLinuxBH, "linuxscsi");
   VMK_ASSERT_BUG(status == VMK_OK);

   mutex_init(&host_cmd_pool_mutex);

   linuxSCSIWQ = create_singlethread_workqueue("linuxSCSIWQ");
   VMK_ASSERT(linuxSCSIWQ);

   status = vmk_StressOptionOpen(VMK_STRESS_OPT_SCSI_ADAPTER_ISSUE_FAIL,
                                 &stressScsiAdapterIssueFail);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionOpen(VMK_STRESS_OPT_VMKLINUX_DROP_CMD_SCSI_DONE,
                                 &stressVmklnxDropCmdScsiDone);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionOpen(VMK_STRESS_OPT_VMKLINUX_ABORT_CMD_FAILURE,
                                 &stressVmklinuxAbortCmdFailure);
   VMK_ASSERT(status == VMK_OK);
   
   return;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinux_Cleanup
 *
 *      This is the cleanup entry point for SCSI. Called during vmklinux26 
 *    unload from linux_stubs.c
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
void
SCSILinux_Cleanup(void)
{
   VMK_ReturnStatus status;
   int i;

   vmk_BottomHalfUnregister(scsiLinuxBH);

   vmk_SPDestroyIRQ(&linuxSCSIAdapterLock);

   mutex_destroy(&host_cmd_pool_mutex);

   for (i = 0; i < NR_CPUS; i++) {
      if (scsiLinuxTLS[i]) {
         vmklnx_kfree(VMK_MODULE_HEAP_ID, scsiLinuxTLS[i]);
      }
   }

   if (vmklnxScsiCmdHeap != VMK_INVALID_HEAP_ID) {
      vmk_HeapDestroy(vmklnxScsiCmdHeap);
      vmklnxScsiCmdHeap = VMK_INVALID_HEAP_ID;
   }

   if (vmklnxScsiSgHeap != VMK_INVALID_HEAP_ID) {
      vmk_HeapDestroy(vmklnxScsiSgHeap);
      vmklnxScsiSgHeap = VMK_INVALID_HEAP_ID;
   }

   destroy_workqueue(linuxSCSIWQ);
   status = vmk_StressOptionClose(stressScsiAdapterIssueFail);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionClose(stressVmklnxDropCmdScsiDone);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionClose(stressVmklinuxAbortCmdFailure);
   VMK_ASSERT(status == VMK_OK);
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxSetName
 *
 *      Set adapter name with given name by the driver
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      If the device is associated with PCI bus, update the PCI device
 *      name list.
 *
 *----------------------------------------------------------------------
 */
void
SCSILinuxSetName(vmk_ScsiAdapter *vmkAdapter, struct Scsi_Host const *sHost,
                 struct pci_dev *pciDev)
{
   vmk_PCIDevice vmkDev;

   if (LinuxPCI_IsValidPCIBusDev(pciDev)) {
      vmk_PCIGetPCIDevice(pciDev->bus->number, PCI_SLOT(pciDev->devfn), 
			PCI_FUNC(pciDev->devfn), &vmkDev);
      vmk_PCISetDeviceName(vmkDev, sHost->name);
   }
                                                                                                                                                                      
   strncpy(vmkAdapter->name, sHost->name, sizeof(vmkAdapter->name));
   vmkAdapter->name[sizeof(vmkAdapter->name) - 1] = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxNameAdapter
 *
 *      Find a good adapter name for the given pci device
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
void
SCSILinuxNameAdapter(char *adapterName, struct pci_dev *pciDev)
{
   vmk_PCIDevice vmkDev = NULL;
   VMK_ReturnStatus status = VMK_FAILURE;

   if (pciDev != NULL) {

      status = vmk_PCIGetPCIDevice(pciDev->bus->number, 
                                PCI_SLOT(pciDev->devfn), 
                                PCI_FUNC(pciDev->devfn), &vmkDev);

      VMK_ASSERT(status == VMK_OK);

      status = vmk_PCIGetDeviceName(vmkDev, 
                                    adapterName, 
                                    VMK_SCSI_DRIVER_NAME_LENGTH);
   }

   if (status != VMK_OK) {
      /*
       * Get a new name.
       */
      vmk_ScsiAdapterUniqueName(adapterName);

      vmk_LogDebug(vmklinux26Log, 0, "%s - Adapter name %s\n", 
	__FUNCTION__, adapterName);

      if (vmkDev != NULL) {
         vmk_PCISetDeviceName(vmkDev, adapterName);
      }
   }
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxGetModuleID --
 *
 *
 * Results: 
 *      valid module id
 *      VMK_INVALID_MODULE_ID if the id cannot be determined 
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
vmk_ModuleID
SCSILinuxGetModuleID(struct Scsi_Host *sh, struct pci_dev *pdev)
{
   vmk_ModuleID moduleID = VMK_INVALID_MODULE_ID;
   struct vmklnx_ScsiModule *vmklnx26ScsiModule;

   /*
    * We will start with Module wide structures
    */
   if (pdev != NULL) {
      /*
       * PCI structure has the driver name. So start with it
       * This does not hold good for pseudo drivers
       */
      if (pdev->driver) {
         moduleID = pdev->driver->driver.owner->moduleID;
      }
   }

   if (moduleID != VMK_INVALID_MODULE_ID) {
      return(moduleID);
   }

   /*
    * Fall back 1. The transport parameters are module wide, so try to
    * extract the details from transport template if the driver has one
    * Drawback is some of the scsi drivers dont have transport structs
    */
   vmklnx26ScsiModule = (struct vmklnx_ScsiModule *)sh->transportt->module;

   if (vmklnx26ScsiModule) {
      moduleID = vmklnx26ScsiModule->moduleID;
      if (moduleID != VMK_INVALID_MODULE_ID) {
         return (moduleID);
      }
   }

   /*
    * Fallback 2 - Now it is possible that calling function is not the module
    * Fall back to 24 flow as below. Is USB a good example?
    */
   moduleID = vmk_ModuleStackTop();

   /*
    * Try to get if the SHT has module.
    */   
   if (sh->hostt->module != NULL) {
      /*
       * If possible extract the ID from the host template information
       * If don't match prefer the host template ID but Log just in case
       */
      vmk_ModuleID shtModuleID = sh->hostt->module->moduleID;

      if (shtModuleID != VMK_INVALID_MODULE_ID && shtModuleID != moduleID) {
         vmk_LogDebug(vmklinux26Log, 0, "%s - Current module ID is %"VMK_FMT64"x "
                      "but ht module ID is %"VMK_FMT64"x, using the latter "
                      "for driver that supports %s.\n", __FUNCTION__,
                      vmk_ModuleGetDebugID(moduleID),
                      vmk_ModuleGetDebugID(shtModuleID),
                      sh->hostt->name);
         moduleID = shtModuleID;
      }
   }

   if (moduleID == VMK_INVALID_MODULE_ID) {
      vmk_LogDebug(vmklinux26Log, 0, "%s - Could not get the module id for"
                   "the driver that supports %s."
                   "The device will not be registered correctly.\n",
                   __FUNCTION__, sh->hostt->name);
      VMK_ASSERT(moduleID != VMK_INVALID_MODULE_ID);
   }

   return(moduleID);
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxBH --
 *
 *      Bottom half to complete cmds returned from the adapter.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */
static void
SCSILinuxBH(void *clientData)
{
   vmk_ScsiHostStatus hostStatus;
   vmk_ScsiDeviceStatus deviceStatus;
   vmk_TimerCycles yield, now;
   unsigned myPCPU = vmk_GetPCPUNum();
   scsiLinuxTLS_t *tls = scsiLinuxTLS[myPCPU];

   /*
    * Flag should be set saying we need to run, and if it's
    * set there should always be work for us when we get here.
    */
   VMK_ASSERT(tls->runningBH == VMK_TRUE);
   VMK_ASSERT(!list_empty(&tls->isrDoneCmds) || !list_empty(&tls->bhDoneCmds));

   now = 0;
   yield = vmk_GetTimerCycles() + vmk_TimerUSToTC(5000); /* 5 ms */

   VMK_ASSERT_CPU_HAS_INTS_ENABLED();
   vmk_CPUDisableInterrupts();
replenish:
   list_splice_init(&tls->isrDoneCmds, &tls->bhDoneCmds);
   mb();
   vmk_CPUEnableInterrupts();

   while (!list_empty(&tls->bhDoneCmds) && now < yield) {
      struct scsi_cmnd *scmd;
#ifdef VMKLNX_TRACK_IOS_DOWN
      struct scsi_device *sdev;
#endif
      vmk_ScsiCommand *vmkCmdPtr;
      unsigned long flags;

      scmd = list_entry(tls->bhDoneCmds.next, struct scsi_cmnd, bhlist);
      list_del(&scmd->bhlist);

      /*
       * First we check for internal commands, which we complete directly.
       */
      if (scmd->vmkflags & VMK_FLAGS_INTERNAL_COMMAND) {
         scmd->done(scmd);
         now = vmk_GetTimerCycles();
         continue;
      }

      /*
       * Map the Linux driver status bytes in to something the VMkernel
       * knows how to deal with.
       */
      switch (driver_byte(VMKLNX_SCSI_STATUS_NO_SUGGEST(scmd->result))) {
      case DRIVER_OK:
         hostStatus = VMKLNX_SCSI_HOST_STATUS(scmd->result);
         deviceStatus = VMKLNX_SCSI_DEVICE_STATUS(scmd->result);
         break;

      case DRIVER_BUSY:
      case DRIVER_SOFT:
         hostStatus = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_BUSY;
         break;

      case DRIVER_MEDIA:
      case DRIVER_ERROR:
      case DRIVER_HARD:
      case DRIVER_INVALID:
         hostStatus = VMK_SCSI_HOST_ERROR;
         deviceStatus = VMK_SCSI_DEVICE_GOOD;
         break;

      case DRIVER_TIMEOUT:
         hostStatus = VMK_SCSI_HOST_TIMEOUT;
         deviceStatus = VMK_SCSI_DEVICE_GOOD;
         break;

      case DRIVER_SENSE:
         hostStatus = VMKLNX_SCSI_HOST_STATUS(scmd->result);
         deviceStatus = VMKLNX_SCSI_DEVICE_STATUS(scmd->result);
         VMK_ASSERT((hostStatus == VMK_SCSI_HOST_OK) &&
                    (deviceStatus == VMK_SCSI_DEVICE_CHECK_CONDITION));
         break;

      default:
         vmk_WarningMessage("Unknown driver code: %x\n",
                            VMKLNX_SCSI_STATUS_NO_SUGGEST(scmd->result));
         VMK_ASSERT(0);
         hostStatus = VMKLNX_SCSI_HOST_STATUS(scmd->result);
         deviceStatus = VMKLNX_SCSI_DEVICE_STATUS(scmd->result);
         break;
      }


#ifdef VMKLNX_TRACK_IOS_DOWN
      sdev = scmd->device
#endif

      vmkCmdPtr = scmd->vmkCmdPtr;
      vmkCmdPtr->bytesXferred = scmd->request_bufflen - scmd->resid;

       if (unlikely(vmkCmdPtr->bytesXferred > scmd->request_bufflen)) {
         if (likely((hostStatus != VMK_SCSI_HOST_OK) ||
                    (deviceStatus != VMK_SCSI_DEVICE_GOOD))) {
            vmkCmdPtr->bytesXferred = 0;

            vmk_WarningMessage("%s - Error BytesXferred > Requested Length "
	                          "Marking transfer length as 0 - vmhba = %s, "
                                  "Driver Name = %s, "
	                          "Requested length = %d, Resid = %d\n",
                                  __FUNCTION__,
		                  vmklnx_get_vmhba_name(scmd->device->host), 
		                  scmd->device->host->hostt->name,
                                  scmd->request_bufflen, 
		                  scmd->resid);
         } else {
            /*
             * We should not reach here
             */
            vmk_AlertMessage("%s - Error BytesXferred > Requested Length "
                             "but HOST_OK/DEVICE_GOOD!"
                             "vmhba = %s, Driver Name = %s, "
                             "Requested length = %d, Resid = %d\n",
                             __FUNCTION__,
                             vmklnx_get_vmhba_name(scmd->device->host), 
                             scmd->device->host->hostt->name,
                             scmd->request_bufflen, scmd->resid);
            vmkCmdPtr->bytesXferred = 0;
            scmd->result = DID_ERROR << 16;
            hostStatus = VMK_SCSI_HOST_ERROR;
            deviceStatus = VMK_SCSI_DEVICE_GOOD;
         }
      }

      /*
       * Copy the sense buffer whenever there's an error.
       *
       * The buffer should only really be valid when we
       * hit HOST_OK/CHECK_CONDITION but sometimes broken
       * drivers will return valuable debug data anyway.
       * So copy it here so that it can be logged/examined
       * later.
       */
      if (unlikely(!((hostStatus == VMK_SCSI_HOST_OK) &&
                     (deviceStatus == VMK_SCSI_DEVICE_GOOD)))) {
         VMK_ASSERT(deviceStatus != VMK_SCSI_DEVICE_CHECK_CONDITION ||
                    scmd->sense_buffer[0] != 0);
         memcpy(&vmkCmdPtr->senseData, scmd->sense_buffer,
                min(sizeof(vmkCmdPtr->senseData),
                    sizeof(scmd->sense_buffer)));
      } else if (unlikely(scmd->cmnd[0] == VMK_SCSI_CMD_INQUIRY)) {
	    SCSILinuxProcessStandardInquiryResponse(scmd);
      }

      spin_lock_irqsave(scmd->device->host->host_lock, flags);
      --scmd->device->host->host_busy;
      --scmd->device->device_busy;
      spin_unlock_irqrestore(scmd->device->host->host_lock, flags);
 
      /* 
       * Put back the scsi command before calling scsi upper layer
       * Otherwise, if a destroy path is issued by upper layer and
       * it will destroy scsi device which has list_lock required
       * by scsi_put_command. Even worse, list_lock is filled with 0 
       * and scsi_put_command interprets it as locked by cpu 0.
       * This causes cpu locked up.
       */
      scsi_put_command(scmd);

      SCSILinuxCompleteCommand(vmkCmdPtr, hostStatus, deviceStatus);

#ifdef VMKLNX_TRACK_IOS_DOWN
      /*
       * Decrement the ref count on this device now
       */
      put_device(&sdev->sdev_gendev);
#endif

      now = vmk_GetTimerCycles();
   }

   add_disk_randomness(NULL);

   VMK_ASSERT_CPU_HAS_INTS_ENABLED();
   vmk_CPUDisableInterrupts();
   if (now > yield) {
      if (!list_empty(&tls->bhDoneCmds) ||
          !list_empty(&tls->isrDoneCmds)) {
         vmk_CPUEnableInterrupts();
         vmk_BottomHalfSchedulePCPU(scsiLinuxBH, myPCPU);
         return;
      }
   } else if (!list_empty(&tls->isrDoneCmds)) {
      goto replenish;
   }
   tls->runningBH = VMK_FALSE;
   vmk_CPUEnableInterrupts();
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxQueueCommand --
 *
 *      Queue up a SCSI command.
 *
 * Results:
 *      VMK return status
 *      VMK_OK - cmd queued or will be completed with error via BH
 *      VMK_WOULD_BLOCK - cmd not queued because of QD limit or device quiesse etc 
 *      VMK_NO_MEMORY - cmd not queued because of failed alloc of scmd struct
 *
 * Side effects:
 *      A command is allocated, set up, and queueud.
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus
SCSILinuxQueueCommand(struct Scsi_Host *shost,
                      struct scsi_device *sdev, 
                      vmk_ScsiCommand *vmkCmdPtr)
{
   struct scsi_cmnd *scmd;
   int status = 0;
   vmk_ScsiAdapter *vmkAdapter;
   unsigned long flags;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   VMK_ReturnStatus vmkStatus;

   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *) shost->adapter; 
   vmkAdapter = vmklnx26ScsiAdapter->vmkAdapter;

   /* Never issue a command to the host ID */
   VMK_ASSERT(sdev);
   VMK_ASSERT(sdev->id != shost->this_id);

#ifdef SCSI_LINUX_DEBUG
   vmk_LogDebug(vmklinux26Log, 0, "%s - cmd 0x%x " SCSI_PATH_NAME_FMT " sn %d"
	" len %d \n", __FUNCTION__, vmkCmdPtr->cdb[0],
       SCSI_PATH_NAME(sh, devPtr->channel, devPtr->id, devPtr->lun),
       vmkCmdPtr->serialNumber, vmkCmdPtr->dataLength);
#endif

   scmd = scsi_get_command(sdev, GFP_KERNEL);
   if (unlikely(scmd == NULL)) {
      vmk_LogDebug(vmklinux26Log, 0, "%s - Insufficient memory\n",
	 __FUNCTION__);
      return VMK_NO_MEMORY; /* VMK_WOULD_BLOCK ? */
   }

   vmkStatus = SCSILinuxComputeSGArray(scmd, vmkCmdPtr);
   if (vmkStatus == VMK_NO_MEMORY) {
      scsi_put_command(scmd);
      vmk_LogDebug(vmklinux26Log, 0, "%s - SCSILinuxComputeSGArray failed\n",
	 __FUNCTION__);
      return VMK_NO_MEMORY; 	
   }

#ifdef VMKLNX_TRACK_IOS_DOWN
   if (!get_device(&sdev->sdev_gendev)) {
      scsi_put_command(scmd);
      vmk_LogDebug(vmklinux26Log, 0, "%s - Getting ref count on sdev failed\n",
	 __FUNCTION__);
      return VMK_NO_MEMORY; 	
   }
#endif

   VMK_ASSERT_ON_COMPILE(MAX_COMMAND_SIZE >= VMK_SCSI_MAX_CDB_LEN);

   /*
    * Get the serial number and update tag info etc 
    */
   SCSILinuxInitScmd(sdev, scmd);

   if (likely(scmd->device->tagged_supported)) {
      if (unlikely(vmkCmdPtr->flags & VMK_SCSI_COMMAND_FLAGS_ISSUE_WITH_ORDERED_TAG)) {
         scmd->tag = ORDERED_QUEUE_TAG;
      } else {
         scmd->tag = SIMPLE_QUEUE_TAG;
      }
   } else {
      scmd->tag = 0;
   }

   memcpy(scmd->cmnd, vmkCmdPtr->cdb, vmkCmdPtr->cdbLen);
   scmd->cmd_len = vmkCmdPtr->cdbLen;
   scmd->vmkCmdPtr = vmkCmdPtr;
   scmd->underflow = vmkCmdPtr->requiredDataLen;

   /*
    * Set the data direction on every command, so the residue dir is
    * not re-used.
    */
   scmd->sc_data_direction = SCSILinuxGetDataDirection(scmd,
                                                      vmkCmdPtr->dataDirection);

   /*
    * Verify that none of the vmware supported drivers are using these
    * fields. If so, we need to set them appropriately.
    * 
    * Note that the SCp and host_scribble fields can be used by the
    * driver without midlayer interference.
    */
   VMK_ASSERT(scmd->transfersize == 0);
   VMK_ASSERT(scmd->sglist_len == 0);

   VMK_DEBUG_ONLY(
      if (vmk_ScsiDebugDropCommand(vmkAdapter, vmkCmdPtr)) {
      vmk_WarningMessage("%s - Dropping command: SN %#"VMK_FMT64"x, initiator %p\n",
	      __FUNCTION__, vmkCmdPtr->cmdId.serialNumber,
	      vmkCmdPtr->cmdId.initiator);
      vmk_WarningMessage("%s - Call stack: %p <- %p <- %p <- %p <- %p <- %p\n",
               __FUNCTION__,
               __builtin_return_address(0),
               __builtin_return_address(1),
               __builtin_return_address(2),
               __builtin_return_address(3),
               __builtin_return_address(4),
               __builtin_return_address(5));
      spin_lock_irqsave(&scmd->vmklock, flags);
      scmd->vmkflags |= (VMK_FLAGS_DROP_CMD|VMK_FLAGS_NEED_CMDDONE);
      spin_unlock_irqrestore(&scmd->vmklock, flags);
      spin_lock_irqsave(shost->host_lock, flags);
      ++shost->host_busy;
      ++sdev->device_busy;
      spin_unlock_irqrestore(shost->host_lock, flags);
      return VMK_OK;
   }
   )

   VMK_ASSERT(scmd->use_sg <= vmkAdapter->sgSize);

   if (unlikely(scmd->cmd_len > shost->max_cmd_len)) {
      scmd->result = (DID_ABORT << 16)|SAM_STAT_GOOD;
      spin_lock_irqsave(shost->host_lock, flags);
      ++shost->host_busy;
      ++sdev->device_busy;
      scmd->vmkflags |= VMK_FLAGS_NEED_CMDDONE;
      spin_unlock_irqrestore(shost->host_lock, flags);
      SCSILinuxCmdDone(scmd);  
      return VMK_OK;
   }

   if (unlikely(shost->shost_state == SHOST_CANCEL)) {
      scmd->result = (DID_NO_CONNECT << 16)|SAM_STAT_GOOD;
      spin_lock_irqsave(shost->host_lock, flags);
      ++shost->host_busy;
      ++sdev->device_busy;
      scmd->vmkflags |= VMK_FLAGS_NEED_CMDDONE;
      spin_unlock_irqrestore(shost->host_lock, flags);
      SCSILinuxCmdDone(scmd);
      return VMK_OK;
   }

   spin_lock_irqsave(shost->host_lock, flags);
   /* linux tests all these states without holding the host_lock */
   if (unlikely((shost->host_self_blocked) || 
                (sdev->sdev_state == SDEV_BLOCK) ||
                (sdev->sdev_state == SDEV_QUIESCE) || 
                (shost->shost_state == SHOST_RECOVERY) ||
                (sdev->device_busy >= sdev->queue_depth) ||
                (shost->host_busy >= shost->can_queue))) {
      spin_unlock_irqrestore(shost->host_lock, flags);
#ifdef VMKLNX_TRACK_IOS_DOWN
      put_device(&sdev->sdev_gendev);
#endif
      scsi_put_command(scmd);
      vmk_LogDebug(vmklinux26Log, 0,
                   "h: sb=%d, b=%d d: %p s=%d, b=%d, cq=%d, qd=%d",
                   shost->host_self_blocked,
                   shost->host_busy,
                   sdev,
                   sdev->sdev_state,
                   sdev->device_busy,
                   shost->can_queue,
                   sdev->queue_depth);
      return VMK_WOULD_BLOCK;
   }

   scmd->vmkflags |= VMK_FLAGS_NEED_CMDDONE;
   ++shost->host_busy;
   ++sdev->device_busy;

   if (unlikely((sdev->sdev_state == SDEV_DEL) ||
                (sdev->sdev_state == SDEV_OFFLINE))) { 
      scmd->result = (DID_NO_CONNECT << 16)|SAM_STAT_GOOD;
      spin_unlock_irqrestore(shost->host_lock, flags);
      vmk_LogDebug(vmklinux26Log, 2, " - The device is up for delete");
      SCSILinuxCmdDone(scmd);  
      return VMK_OK;
   }

   if ((shost->xportFlags == VMKLNX_SCSI_TRANSPORT_TYPE_FC) &&
      (vmkAdapter->mgmtAdapter.t.fc->ioTimeout) &&
      (shost->hostt->eh_abort_handler)) {
      scmd->vmkflags |= VMK_FLAGS_IO_TIMEOUT;
      scsi_add_timer(scmd, vmkAdapter->mgmtAdapter.t.fc->ioTimeout, 
         SCSILinuxCmdTimedOut);
   }

   /*
    * Linux is weird and holds the host_lock while calling
    * the drivers queuecommand entrypoint.
    */
   VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(shost),
                      status,
                      shost->hostt->queuecommand,
                      scmd,
                      SCSILinuxCmdDone);
   spin_unlock_irqrestore(shost->host_lock, flags);

   if (unlikely(status)) {
      static atomic_t repeat_cnt = ATOMIC_INIT(0);
      static atomic64_t throttle_to = ATOMIC64_INIT(0);
      uint32_t cnt;
      unsigned long tto = 0;

      if (scmd->vmkflags & VMK_FLAGS_IO_TIMEOUT)
         scsi_delete_timer(scmd);

      scmd->result = (DID_OK << 16)|SAM_STAT_BUSY;

      /*
       * In order to limit the # of error messages printed, the below
       * heuristic will print it every THROTTLE_TO secs. Refer to PR 359133.
       * When the window to print opens up, there is a chance that multiple
       * messages can get printed. Also, when multiple messages get printed,
       * there is a chance that the repeat count will get printed out of order.
       * For example, if the repeat_cnt was 80 and then went to 1 (after a reset
       * to 0), it could print 1 and then 80. But these are not too bad and
       * so no logic is in place to prevent that.
       *
       * Maybe this can be expanded someday to make this per shost. For now,
       * lets keep it simple.
       */
      atomic_inc(&repeat_cnt);
      tto = atomic64_read(&throttle_to);

      if ((long)(jiffies - tto) >= 0) {
         tto = jiffies + THROTTLE_TO * HZ;
         atomic_set(&throttle_to, tto);
         cnt = atomic_xchg(&repeat_cnt, 0); // cnt will have old repeat_cnt val

         if (likely(cnt > 0)) {
            vmk_WarningMessage("%s - queuecommand failed with status = 0x%x %s "
	       "%s:%d:%d:%d "
               "(driver name: %s) - Message repeated %d time%s\n",
               __FUNCTION__, status,
               (status == SCSI_MLQUEUE_HOST_BUSY) ? "Host Busy" :
               vmk_StatusToString(status),
	       SCSI_GET_NAME(shost), sdev->channel, sdev->id, sdev->lun,
               shost->hostt->name ? shost->hostt->name : "NULL", cnt,
	       (cnt == 1) ? "" : "s");
         }
      }

      /* 
       * We really should not need to check NEED_CMDDONE here,
       * if it is not set, it is a driver bug. We don't need to
       * hold the vmk_lock because nobody else should be accessing
       * the scmd.
       */
      if (scmd->vmkflags & VMK_FLAGS_NEED_CMDDONE) {
         SCSILinuxCmdDone(scmd);
      } else {
         vmk_WarningMessage("%s: scsi_done called when queuecommand failed!\n",
                            __FUNCTION__);
      }
   }
   return VMK_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxComputeSGArray --
 *
 *      Compute the scatter-gather array for the vmk command
 *
 * Results:
 *      VMK return status
 *
 * Side effects:
 *      A scatter gather array is allocated and initialized
 *
 *----------------------------------------------------------------------
 */

static VMK_ReturnStatus
SCSILinuxComputeSGArray(struct scsi_cmnd *scmd, vmk_ScsiCommand *vmkCmdPtr)
{
   int i, sgArrLen;
   vmk_SgArray *sgArray = vmkCmdPtr->sgArray;
   vmk_ScsiAdapter *vmkAdapter;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;

   VMK_ASSERT(scmd->device);

   vmklnx26ScsiAdapter = 
	(struct vmklnx_ScsiAdapter *) scmd->device->host->adapter; 
   vmkAdapter = vmklnx26ScsiAdapter->vmkAdapter;

   sgArrLen = sgArray->nbElems;

   if (unlikely(sgArrLen == 0)) {
      /* 
       * There can be few commands without any SG. E.g TUR
       */
      vmk_LogDebug(vmklinux26Log, 5, "%s - Sglen is zero for Cmd 0x%x \n", 
		__FUNCTION__, vmkCmdPtr->cdb[0]);
      scmd->request_buffer = NULL; 
      scmd->request_bufferMA = 0; 
      scmd->use_sg = 0;
      return VMK_OK;
   }

   if (vmklnx26ScsiAdapter->vmkSgArray) {
      for (i = 0; i < sgArrLen; i++) {
         scmd->request_bufflen += sgArray->elem[i].length;
      }
      scmd->sgArray = scmd->vmksg;
      scmd->sgArray->vmksgel = scmd->sgArray->cursgel = sgArray->elem;
      scmd->sgArray->sg_type = SG_VMK;
   } else {
      scmd->sgArray = scsi_alloc_sgtable(sgArrLen);
      if (unlikely(scmd->sgArray == NULL)) {
         return VMK_NO_MEMORY;
      }

      for (i = 0; i < sgArrLen; i++) {
         /* Init sg page and offset */
         scmd->sgArray[i].page = phys_to_page(sgArray->elem[i].addr);
         scmd->sgArray[i].offset = offset_in_page(sgArray->elem[i].addr);
         scmd->sgArray[i].dma_address = (dma_addr_t)sgArray->elem[i].addr;
         scmd->sgArray[i].dma_length = sgArray->elem[i].length;
         scmd->sgArray[i].length = sgArray->elem[i].length;
         scmd->sgArray[i].sg_type = SG_LINUX;
         scmd->request_bufflen += sgArray->elem[i].length;
         vmk_AssertMemorySupportsIO(scmd->sgArray[i].dma_address, 
                                    scmd->sgArray[i].length);
      }
   }

   /* 
    * Legacy percraid assumes that all small commands do not use
    * scatter/gather
    */
   if (sgArrLen == 1) {
      scmd->request_bufferMA = sgArray[0].elem[0].addr;
      if (scmd->request_bufflen) {
         scmd->request_buffer = phys_to_virt(scmd->request_bufferMA);
      } else {
         scmd->request_buffer = 0;
      }
      scmd->use_sg = 0;
   } else {
      scmd->use_sg = sgArrLen;
      scmd->request_buffer = scmd->sgArray;
      scmd->request_bufferMA = 0;
   }

   return VMK_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxGetDataDirection --
 *
 *      This is a general purpose routine that will return the data
 *      direction for a SCSI command based on the actual command.
 *      Guests can send any SCSI command, so this routine must handle
 *      *all* scsi commands correctly.
 *
 *       This code is compliant with SPC-2 (ANSI X3.351:200x)
 *       - Except we do not support 16 byte CDBs
 *
 * Results:
 *      sc_data_direction value
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
int
SCSILinuxGetDataDirection(struct scsi_cmnd * cmdPtr, 
			  vmk_ScsiCommandDirection guestDataDirection)
{
   unsigned char cmd = cmdPtr->cmnd[0];

  /*
   * First check if the caller has specified the data direction. If the 
   * direction is set to READ, WRITE or NONE, we will just go by it. 
   * If the direction is set to UNKNOWN, then we will try to find out
   * the direction to the best of our knowledge. This approach will help us
   * support vendor specific commands without modifying the vmkernel.
   */
   if (likely(guestDataDirection != VMK_SCSI_COMMAND_DIRECTION_UNKNOWN)) {
      if (likely(guestDataDirection == VMK_SCSI_COMMAND_DIRECTION_READ)) {
         return DMA_FROM_DEVICE;
      } else if (likely(guestDataDirection == 
      					VMK_SCSI_COMMAND_DIRECTION_WRITE)) {
         return DMA_TO_DEVICE;
      } else {
         return DMA_NONE;
      }
   }

   switch (cmd) {
   case VMK_SCSI_CMD_FORMAT_UNIT	       :  //   0x04	//
   case VMK_SCSI_CMD_REASSIGN_BLOCKS       :  //   0x07	// 
 //case VMK_SCSI_CMD_INIT_ELEMENT_STATUS   :  //   0x07	// Media changer
   case VMK_SCSI_CMD_WRITE6	               :  //   0x0a	// write w/ limited addressing
 //case VMK_SCSI_CMD_PRINT	               :  //   0x0a	// print data
 //case VMK_SCSI_CMD_SEEK6	               :  //   0x0b	// seek to LBN
   case VMK_SCSI_CMD_SLEW_AND_PRINT	       :  //   0x0b	// advance and print
   case VMK_SCSI_CMD_MODE_SELECT	       :  //   0x15	// set device parameters
   case VMK_SCSI_CMD_COPY	               :  //   0x18	// autonomous copy from/to another device
   case VMK_SCSI_CMD_SEND_DIAGNOSTIC       :  //   0x1d	// initiate self-test
   case VMK_SCSI_CMD_SET_WINDOW	       :  //   0x24	// set scanning window
   case VMK_SCSI_CMD_WRITE10               :  //   0x2a	// write
   case VMK_SCSI_CMD_WRITE_VERIFY	       :  //   0x2e	// write w/ verify of success
   case VMK_SCSI_CMD_SEARCH_DATA_HIGH      :  //   0x30	// search for data pattern
   case VMK_SCSI_CMD_SEARCH_DATA_EQUAL     :  //   0x31	// search for data pattern
   case VMK_SCSI_CMD_SEARCH_DATA_LOW       :  //   0x32	// search for data pattern
   case VMK_SCSI_CMD_MEDIUM_SCAN	       :  //   0x38	// search for free area
   case VMK_SCSI_CMD_COMPARE               :  //   0x39	// compare data
   case VMK_SCSI_CMD_COPY_VERIFY	       :  //   0x3a	// autonomous copy w/ verify
   case VMK_SCSI_CMD_WRITE_BUFFER	       :  //   0x3b	// write data buffer
   case VMK_SCSI_CMD_UPDATE_BLOCK	       :  //   0x3d	// substitute block with an updated one
   case VMK_SCSI_CMD_WRITE_LONG            :  //   0x3f	// write data and ECC
   case VMK_SCSI_CMD_CHANGE_DEF 	       :  //   0x40	// set SCSI version
   case VMK_SCSI_CMD_WRITE_SAME            :  //   0x41	// 
   case VMK_SCSI_CMD_LOG_SELECT            :  //   0x4c	// select statistics
   case VMK_SCSI_CMD_MODE_SELECT10	       :  //   0x55	// set device parameters
   case VMK_SCSI_CMD_RESERVE_UNIT10        :  //   0x56	//
   case VMK_SCSI_CMD_SEND_CUE_SHEET        :  //   0x5d	// (CDR Related?)
   case VMK_SCSI_CMD_PERSISTENT_RESERVE_OUT:  //   0x5f	//
   case VMK_SCSI_CMD_WRITE12               :  //   0xaa	// write data
   case VMK_SCSI_CMD_WRITE_VERIFY12	       :  //   0xae	// write logical block, verify success
   case VMK_SCSI_CMD_SEARCH_DATA_HIGH12    :  //   0xb0	// search data pattern
   case VMK_SCSI_CMD_SEARCH_DATA_EQUAL12   :  //   0xb1	// search data pattern
   case VMK_SCSI_CMD_SEARCH_DATA_LOW12     :  //   0xb2	// search data pattern
   case VMK_SCSI_CMD_SEND_VOLUME_TAG       :  //   0xb6	//
 //case VMK_SCSI_CMD_SET_STREAMING         :  //   0xb6	// For avoiding over/underrun
   case VMK_SCSI_CMD_SEND_DVD_STRUCTURE    :  //   0xbf	// burning DVDs?
        return DMA_TO_DEVICE; 

   case VMK_SCSI_CMD_REQUEST_SENSE	       :  //   0x03	// return detailed error information
   case VMK_SCSI_CMD_READ_BLOCKLIMITS      :  //   0x05	//
   case VMK_SCSI_CMD_READ6                 :  //   0x08	// read w/ limited addressing
   case VMK_SCSI_CMD_READ_REVERSE	       :  //   0x0f	// read backwards
   case VMK_SCSI_CMD_INQUIRY               :  //   0x12	// return LUN-specific information
   case VMK_SCSI_CMD_RECOVER_BUFFERED      :  //   0x14	// recover buffered data
   case VMK_SCSI_CMD_MODE_SENSE            :  //   0x1a	// read device parameters
   case VMK_SCSI_CMD_RECV_DIAGNOSTIC       :  //   0x1c	// read self-test results
   case VMK_SCSI_CMD_READ_CAPACITY	       :  //   0x25	// read number of logical blocks
 //case VMK_SCSI_CMD_GET_WINDOW            :  //   0x25	// get scanning window
   case VMK_SCSI_CMD_READ10	               :  //   0x28	// read
   case VMK_SCSI_CMD_READ_GENERATION       :  //   0x29	// read max generation address of LBN
   case VMK_SCSI_CMD_READ_UPDATED_BLOCK    :  //   0x2d	// read specific version of changed block
   case VMK_SCSI_CMD_PREFETCH              :  //   0x34	// read data into buffer
 //case VMK_SCSI_CMD_READ_POSITION	       :  //   0x34	// read current tape position
   case VMK_SCSI_CMD_READ_DEFECT_DATA      :  //   0x37	// 
   case VMK_SCSI_CMD_READ_BUFFER	       :  //   0x3c	// read data buffer
   case VMK_SCSI_CMD_READ_LONG             :  //   0x3e	// read data and ECC
   case VMK_SCSI_CMD_READ_SUBCHANNEL       :  //   0x42	// read subchannel data and status
   case VMK_SCSI_CMD_GET_CONFIGURATION     :  //   0x46	// get configuration (SCSI-3)
   case VMK_SCSI_CMD_READ_TOC              :  //   0x43	// read contents table
   case VMK_SCSI_CMD_READ_HEADER	       :  //   0x44	// read LBN header
   case VMK_SCSI_CMD_GET_EVENT_STATUS_NOTIFICATION :  //   0x4a
   case VMK_SCSI_CMD_LOG_SENSE             :  //   0x4d	// read statistics
   case VMK_SCSI_CMD_READ_DISC_INFO        :  //   0x51	// info on CDRs
   case VMK_SCSI_CMD_READ_TRACK_INFO       :  //   0x52	// track info on CDRs (also xdread in SBC-2)
   case VMK_SCSI_CMD_MODE_SENSE10	       :  //   0x5a	// read device parameters
   case VMK_SCSI_CMD_READ_BUFFER_CAPACITY  :  //   0x5c	// CDR burning info.
   case VMK_SCSI_CMD_PERSISTENT_RESERVE_IN :  //   0x5e	//
   case VMK_SCSI_CMD_READ_CAPACITY16       :  //   0x9e     // read number of logic block
   case VMK_SCSI_CMD_REPORT_LUNS           :  //   0xa0	// 
   case VMK_SCSI_CMD_READ12	               :  //   0xa8	// read (SCSI-3)
   case VMK_SCSI_CMD_READ_DVD_STRUCTURE    :  //   0xad	// read DVD structure (SCSI-3)
   case VMK_SCSI_CMD_READ_DEFECT_DATA12    :  //   0xb7	// read defect data information
   case VMK_SCSI_CMD_READ_ELEMENT_STATUS   :  //   0xb8	// read element status
 //case VMK_SCSI_CMD_SELECT_CDROM_SPEED    :  //   0xb8	// set data rate
   case VMK_SCSI_CMD_READ_CD_MSF	       :  //   0xb9	// read CD information (all formats, MSF addresses)
   case VMK_SCSI_CMD_SEND_CDROM_XA_DATA    :  //   0xbc
 //case VMK_SCSI_CMD_PLAY_CD               :  //   0xbc
   case VMK_SCSI_CMD_MECH_STATUS           :  //   0xbd
   case VMK_SCSI_CMD_READ_CD               :  //   0xbe	// read CD information (all formats, MSF addresses)
        return DMA_FROM_DEVICE; 

   case VMK_SCSI_CMD_TEST_UNIT_READY       :  //   0x00	// test if LUN ready to accept a command
   case VMK_SCSI_CMD_REZERO_UNIT	       :  //   0x01	// seek to track 0
   case VMK_SCSI_CMD_RESERVE_UNIT	       :  //   0x16	// make LUN accessible only to certain initiators
   case VMK_SCSI_CMD_RELEASE_UNIT	       :  //   0x17	// make LUN accessible to other initiators
   case VMK_SCSI_CMD_ERASE                 :  //   0x19	// 
   case VMK_SCSI_CMD_START_UNIT            :  //   0x1b	// load/unload medium
 //case VMK_SCSI_CMD_SCAN                  :  //   0x1b	// perform scan
 //case VMK_SCSI_CMD_STOP_PRINT            :  //   0x1b	// interrupt printing
   case VMK_SCSI_CMD_MEDIUM_REMOVAL	       :  //   0x1e	// lock/unlock door
   case VMK_SCSI_CMD_SEEK10                :  //   0x2b	// seek LBN
 //case VMK_SCSI_CMD_POSITION_TO_ELEMENT   :  //   0x2b	// media changer
   case VMK_SCSI_CMD_VERIFY                :  //   0x2f	// verify success
   case VMK_SCSI_CMD_SET_LIMITS            :  //   0x33	// define logical block boundaries
   case VMK_SCSI_CMD_SYNC_CACHE            :  //   0x35	// re-read data into buffer
   case VMK_SCSI_CMD_LOCKUNLOCK_CACHE      :  //   0x36	// lock/unlock data in cache
   case VMK_SCSI_CMD_PLAY_AUDIO10	       :  //   0x45	// audio playback
   case VMK_SCSI_CMD_PLAY_AUDIO_MSF	       :  //   0x47	// audio playback starting at MSF address
   case VMK_SCSI_CMD_PLAY_AUDIO_TRACK      :  //   0x48	// audio playback starting at track/index
   case VMK_SCSI_CMD_PLAY_AUDIO_RELATIVE   :  //   0x49	// audio playback starting at relative track
   case VMK_SCSI_CMD_PAUSE                 :  //   0x4b	// audio playback pause/resume
   case VMK_SCSI_CMD_STOP_PLAY             :  //   0x4e	// audio playback stop
   case VMK_SCSI_CMD_RESERVE_TRACK         :  //   0x53	// leave space for data on CDRs
   case VMK_SCSI_CMD_RELEASE_UNIT10        :  //   0x57	//
   case VMK_SCSI_CMD_CLOSE_SESSION         :  //   0x5b	// close area/sesssion (recordable)
   case VMK_SCSI_CMD_BLANK                 :  //   0xa1	// erase RW media
   case VMK_SCSI_CMD_MOVE_MEDIUM	       :  //   0xa5	// 
 //case VMK_SCSI_CMD_PLAY_AUDIO12	       :  //   0xa5	// audio playback
   case VMK_SCSI_CMD_EXCHANGE_MEDIUM       :  //   0xa6	//
 //case VMK_SCSI_CMD_LOADCD                :  //   0xa6	//
   case VMK_SCSI_CMD_PLAY_TRACK_RELATIVE   :  //   0xa9	// audio playback starting at relative track
   case VMK_SCSI_CMD_ERASE12               :  //   0xac	// erase logical block
   case VMK_SCSI_CMD_VERIFY12              :  //   0xaf	// verify data
   case VMK_SCSI_CMD_SET_LIMITS12	       :  //   0xb3	// set block limits
   case VMK_SCSI_CMD_REQUEST_VOLUME_ELEMENT_ADDR :  //   0xb5 //
   case VMK_SCSI_CMD_AUDIO_SCAN            :  //   0xba	// fast audio playback
   case VMK_SCSI_CMD_SET_CDROM_SPEED       :  //   0xbb // (proposed)
        return DMA_NONE; 

   /*-
	the scsi committee chose to make certain optical/dvd commands
	move data in directions opposite to the spc-3 maintenance commands...
    */
   case VMK_SCSI_CMD_MAINTENANCE_OUT       :  //   0xa4	// maintenance cmds
// case VMK_SCSI_CMD_REPORT_KEY            :  //   0xa4     // report key SCSI-3
        switch (cmdPtr->device->type) {
	case VMK_SCSI_CLASS_OPTICAL:
	case VMK_SCSI_CLASS_CDROM:
	case VMK_SCSI_CLASS_WORM:
		/* REPORT_KEY */
		return DMA_FROM_DEVICE;
	}
        return DMA_TO_DEVICE; 

   case VMK_SCSI_CMD_MAINTENANCE_IN        :  //   0xa3	// maintenance cmds
// case VMK_SCSI_CMD_SEND_KEY              :  //   0xa3     // send key SCSI-3
        switch (cmdPtr->device->type) {
	case VMK_SCSI_CLASS_OPTICAL:
	case VMK_SCSI_CLASS_CDROM:
	case VMK_SCSI_CLASS_WORM:
		/* SEND_KEY */
		return DMA_TO_DEVICE;
	}
        return DMA_FROM_DEVICE; 
        
   /*
    *   Vendor Specific codes defined in SPC-2
    */
   case 0x02: case 0x06: case 0x09: case 0x0c: case 0x0d: case 0x0e: case 0x13:
   case 0x20: case 0x21: case 0x23: case 0x27:
        return DMA_FROM_DEVICE; 

// Codes defined in the Emulex driver; used by FSC.
#define MDACIOCTL_DIRECT_CMD                  0x22
#define MDACIOCTL_STOREIMAGE                  0x2C
#define MDACIOCTL_WRITESIGNATURE              0xA6
#define MDACIOCTL_SETREALTIMECLOCK            0xAC
#define MDACIOCTL_PASS_THRU_CDB               0xAD
#define MDACIOCTL_PASS_THRU_INITIATE          0xAE
#define MDACIOCTL_CREATENEWCONF               0xC0
#define MDACIOCTL_ADDNEWCONF                  0xC4
#define MDACIOCTL_MORE                        0xC6
#define MDACIOCTL_SETPHYSDEVPARAMETER         0xC8
#define MDACIOCTL_SETLOGDEVPARAMETER          0xCF
#define MDACIOCTL_SETCONTROLLERPARAMETER      0xD1
#define MDACIOCTL_WRITESANMAP                 0xD4
#define MDACIOCTL_SETMACADDRESS               0xD5
   case MDACIOCTL_DIRECT_CMD: 
   {
            switch (cmdPtr->cmnd[2]) {
            case MDACIOCTL_STOREIMAGE:
            case MDACIOCTL_WRITESIGNATURE:
            case MDACIOCTL_SETREALTIMECLOCK:
            case MDACIOCTL_PASS_THRU_CDB:
            case MDACIOCTL_CREATENEWCONF:
            case MDACIOCTL_ADDNEWCONF:
            case MDACIOCTL_MORE:
            case MDACIOCTL_SETPHYSDEVPARAMETER:
            case MDACIOCTL_SETLOGDEVPARAMETER:
            case MDACIOCTL_SETCONTROLLERPARAMETER:
            case MDACIOCTL_WRITESANMAP:
            case MDACIOCTL_SETMACADDRESS:
                  return DMA_TO_DEVICE;
            case MDACIOCTL_PASS_THRU_INITIATE:
                  if (cmdPtr->cmnd[3] & 0x80) {
                           return DMA_TO_DEVICE;
                  } else {
                           return DMA_FROM_DEVICE;
                  }
            default:
                  return DMA_FROM_DEVICE;
            }
   }

   /*
    *   Additional codes defined in SPC-2
    *   Can not support the 16 byte cdb commands, so only list others here
    */
   case 0x50                           :  //   0x50 // xdwrite 10
   case 0x54                           :  //   0x54 // send opc information
        return DMA_TO_DEVICE;

   case 0x59                           :  //   0x59 // read master cue
   case 0xb4                           :  //   0xb4 // read element status attached 12
        return DMA_FROM_DEVICE; 

   case 0x58                           :  //   0x58 // repair track
        return DMA_NONE; 

   case 0xee                           :  //   0xee // EMC specific
         vmk_LogDebug(vmklinux26Log, 5, "SCSILinuxGetDataDirection: EMC opcode: %x,%x,%x,%x,%x,%x [sn=%d]\n",
               cmdPtr->cmnd[0],
               cmdPtr->cmnd[1],
               cmdPtr->cmnd[2],
               cmdPtr->cmnd[3],
               cmdPtr->cmnd[4],
               cmdPtr->cmnd[5],
               (int)cmdPtr->serial_number
               );
        return DMA_TO_DEVICE; 
  case 0xef                           :  //   0xef // EMC specific
        vmk_LogDebug(vmklinux26Log, 5, "SCSILinuxGetDataDirection: EMC opcode: %x,%x,%x,%x,%x,%x [sn=%d]\n",
               cmdPtr->cmnd[0],
               cmdPtr->cmnd[1],
               cmdPtr->cmnd[2],
               cmdPtr->cmnd[3],
               cmdPtr->cmnd[4],
               cmdPtr->cmnd[5],
               (int)cmdPtr->serial_number
               );
        return DMA_FROM_DEVICE; 

   case 0xff                          : // Intel RAID cache service
        return DMA_BIDIRECTIONAL;

    /*
     * Vendor unique codes used by NEC (See PR 58167) that need a direction set
     * in the command. Some of these are standard SCSI commands that normally 
     * would not have a direction set (DATA_NONE) or are not defined (default).
     *
     * Commands 0x27, 0x2d, 0x3b, and 0x3c are handled earlier in the switch 
     * statement because they are standard SCSI-3 commands.
     * 
     * The special direction cases that are left are coded here.
     * Code	Name			Direction	Special
     * 0x10	READ_SUBSYSTEM_INFO	Read		Yes
     * 0x11	READ_SOLUTION_INFO	Read		Yes
     * 0x26	CHECKSUM_SELECT		Write		Yes
     * 0x27	CHECKSUM_SENSE		Read
     * 0x2c	DDRF_RDRF_SELECT	Write		Yes
     * 0x2d	DDRF_RDRF_SENSE		Read
     * 0x3b	WRITE_BUFFER		Write
     * 0x3c	READ_BUFFER		Read
     * 0xe6	FORCE_RESERVE		DataNone	Yes
     */
   case 0x26                           :  //   0x26 // Vendor Unique Command
   case 0x2c                           :  //   0x2c // erase logical block 10
	if (cmdPtr->request_bufflen)
        	return DMA_TO_DEVICE;   //   If any data, NEC specific command
	else				  //    otherwise, just set no data like normal command
        	return DMA_NONE; 

   case VMK_SCSI_CMD_WRITE_FILEMARKS       :  //   0x10 // 
 //case VMK_SCSI_CMD_SYNC_BUFFER	       :  //   0x10 // print contents of buffer
   case VMK_SCSI_CMD_SPACE                 :  //   0x11 // NEC does a data read with this
	if (cmdPtr->request_bufflen)
        	return DMA_FROM_DEVICE;    //   If any data, NEC specific command
					  //    otherwise, just set no data like normal command
   case 0xe6                           :  //   0xe6 // NEC specific
        return DMA_NONE; 
    
    /*
     * Vendor unique codes used by Fujitsu (See PR 58370)
     */
   case 0xe9                           :  //   0xe9 // Fujitsu specific
        return DMA_TO_DEVICE;
   case 0xeb                           :  //   0xeb // Fujitsu specific
        return DMA_FROM_DEVICE; 

    /*
     *  All 0x8x and 0x9x commands are 16 byte cdbs, these should come here
     */
   case 0x80                           :  //   0x80 // xdwrite extended 16
 //case 0x80                           :  //   0x80 // write filemarks 16
   case 0x81                           :  //   0x81 // rebuild 16 (disk-write)
 //case 0x81                           :  //   0x81 // read reverse 16 (tape-read)
   case 0x82                           :  //   0x82 // regenerate
   case 0x83                           :  //   0x83 // extended copy
   case 0x84                           :  //   0x84 // receive copy results
   case 0x86                           :  //   0x86 // access control in (SPC-3)
   case 0x87                           :  //   0x87 // access control out (SPC-3)
   case 0x88                           :  //   0x88 // read 16
   case 0x89                           :  //   0x89 // device locks (SPC-3)
   case 0x8a                           :  //   0x8a // write 16
   case 0x8c                           :  //   0x8c // read attributes (SPC-3)
   case 0x8d                           :  //   0x8d // write attributes (SPC-3)
   case 0x8e                           :  //   0x8e // write and verify 16
   case 0x8f                           :  //   0x8f // verify 16
   case 0x90                           :  //   0x90 // pre-fetch 16
 //case 0x91                           :  //   0x91 // synchronize cache 16
   case 0x91                           :  //   0x91 // space 16
 //case 0x92                           :  //   0x92 // lock unlock cache 16
   case 0x92                           :  //   0x92 // locate 16
 //case 0x93                           :  //   0x93 // write same 16
   case 0x93                           :  //   0x93 // erase 16
   case 0xa2                           :  //   0xa2 // send event
   case 0xa7                           :  //   0xa7 // move medium attached
 //case 0xa7                           :  //   0xa7 // set read ahead
   default:
        /*
         *  Should never get here unless we don't support the command,
         *  so print a message
         */
        vmk_LogDebug(vmklinux26Log, 0, "unknown opcode: 0x%x\n", cmd);
        return DMA_BIDIRECTIONAL;
   }
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxDumpCmdDone --
 *
 *      Callback when a SCSI dump command completes.
 *	NOTE: this routine is called by SCSI_Dump() explicitly in a
 *	non-interrupt context, so we can use Async_IODone() here, even
 *	though the token locks are non-IRQ locks.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      sets iodone flag in token that will signal SCSI_Dump
 *
 *----------------------------------------------------------------------
 */
void 
SCSILinuxDumpCmdDone(struct vmklnx_ScsiIntCmd *vmklnx26_ScsiIntCmd)
{
   vmk_ScsiCommand *vmkCmdPtr = vmklnx26_ScsiIntCmd->vmkCmdPtr;
   struct scsi_cmnd *scmd = &vmklnx26_ScsiIntCmd->scmd;
   uint32_t ownLock = 0;
   unsigned long flags = 0;

   ownLock = !vmklnx_spin_is_locked_by_my_cpu(scmd->device->host->host_lock);

   VMK_ASSERT_BUG(scmd->vmkflags & VMK_FLAGS_NEED_CMDDONE);

   if (ownLock) {
      spin_lock_irqsave(scmd->device->host->host_lock, flags);
   }

   scmd->vmkflags &= ~VMK_FLAGS_NEED_CMDDONE;
   scmd->serial_number = 0;
   --scmd->device->host->host_busy;
   --scmd->device->device_busy;

   if (ownLock) {
      spin_unlock_irqrestore(scmd->device->host->host_lock, flags);
   }

   vmkCmdPtr->bytesXferred = scmd->request_bufflen - scmd->resid;

   if (vmkCmdPtr->bytesXferred > scmd->request_bufflen) {
      if (likely(VMKLNX_SCSI_STATUS_NO_LINUX(scmd->result) != 0)) {
         vmkCmdPtr->bytesXferred = 0;

         vmk_WarningMessage("%s - Error BytesXferred > Requested Length "
	    "Marking transfer length as 0 - vmhba = %s, Driver Name = %s "
	    "Requested length = %d, Resid = %d  \n", 
	    __FUNCTION__, vmklnx_get_vmhba_name(scmd->device->host), 
	    scmd->device->host->hostt->name, scmd->request_bufflen, 
	    scmd->resid);
      } else {
         /*
          * We should not reach here
          */
         vmk_AlertMessage("%s - Error BytesXferred > Requested Length\n",
		__FUNCTION__);
               vmkCmdPtr->bytesXferred = 0;
               scmd->result = DID_ERROR << 16;
      }
   }

   if (unlikely(VMKLNX_SCSI_STATUS_NO_LINUX(scmd->result) ==
                ((DID_OK << 16) | SAM_STAT_CHECK_CONDITION))) {
      VMK_ASSERT(scmd->sense_buffer[0] != 0);
      memcpy(&vmkCmdPtr->senseData, scmd->sense_buffer,
             min(sizeof(vmkCmdPtr->senseData), sizeof(scmd->sense_buffer)));
   }

   SCSILinuxCompleteCommand(vmkCmdPtr, VMKLNX_SCSI_HOST_STATUS(scmd->result),
                            VMKLNX_SCSI_DEVICE_STATUS(scmd->result));
   return;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxAbortCommands --
 *
 *      Abort all commands originating from a SCSI command.
 *
 * Results:
 *      VMK_OK, driver indicated that the abort succeeded or the driver
 *	  indicated that it has already completed the command.
 *      VMK_FAILURE, the driver indicated that the abort failed in some way.
 *
 * Notes:
 *      This function no more returns VMK_ABORT_NOT_RUNNING as upper layers
 *      were not being acted on this status. Both LSILOGIC and BUSLOGIC
 *      emulation layers anyway wait for the original I/O to finish. Only
 *      sidenote is that in case of BUSLOGIC, there is a possibility that
 *      the emulation driver returns success to ABORT but later the actual
 *      I/O could be returned with success.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus
SCSILinuxAbortCommands(struct Scsi_Host *shost, struct scsi_device *sdev,
                       vmk_ScsiTaskMgmt *vmkTaskMgmtPtr)
{
   struct scsi_cmnd *scmd, *safecmd;
   VMK_ReturnStatus retval = VMK_OK;
   unsigned long flags, vmkflags;
   struct list_head abort_list;

   VMK_ASSERT(sdev != NULL);

   vmk_LogDebug(vmklinux26Log, 4, "%s entered\n",__FUNCTION__);   

   /*
    * This will hold all the commands that need to be aborted
    */
   INIT_LIST_HEAD(&abort_list);

   /*
    * In the first loop, identify the commands that need to be aborted
    * and put them to a local queue. We also mark each of the commands
    * we need to abort with DELAY_CMD_DONE to defer any command completions
    * that come during this time
    */
   spin_lock_irqsave(&sdev->list_lock, flags);
   list_for_each_entry_safe(scmd, safecmd, &sdev->cmd_list, list) {
      vmk_ScsiTaskMgmtAction taskMgmtAction;

      /*
       * Check if the Linux SCSI command has already been
       * completed or is the one PSA is interested in aborting
       */
      spin_lock_irqsave(&scmd->vmklock, vmkflags);
      if (scmd->vmkflags & VMK_FLAGS_NEED_CMDDONE && 
          !(scmd->vmkflags & VMK_FLAGS_CMDDONE_ATTEMPTED) &&
	  ((taskMgmtAction = vmk_ScsiQueryTaskMgmt(vmkTaskMgmtPtr, 
	    scmd->vmkCmdPtr)) & VMK_SCSI_TASKMGMT_ACTION_ABORT)) {

         if (scmd->vmkflags & VMK_FLAGS_DELAY_CMDDONE) {
            vmk_LogDebug(vmklinux26Log, 4, "cmd %p is already in the process of"
                   "being aborted", scmd);
            goto skip_sending_abort;
         } 
         scmd->vmkflags |= VMK_FLAGS_DELAY_CMDDONE;

         /*
          * Store the commands to be aborted in a temp list.
          * This is a reentrant function so we are keeping them in a local list
          * After the abort process is done, we put it back to sdev
          */
         list_move_tail(&scmd->list, &abort_list);

skip_sending_abort:;
      }
      spin_unlock_irqrestore(&scmd->vmklock, vmkflags);
   }
   spin_unlock_irqrestore(&sdev->list_lock, flags);

   /*
    * Drivers dont like to get called with locks held
    */
   list_for_each_entry(scmd, &abort_list, list) {
      VMK_ReturnStatus status;

      status = SCSILinuxAbortCommand(shost, scmd, DID_TIME_OUT);
      if ((status == VMK_OK) && (retval == VMK_OK)) {
         retval = VMK_OK; // Only change this the first time around
      } else if (status == VMK_FAILURE) {
         retval = VMK_FAILURE; // For errors, we always change final retval
         vmk_WarningMessage("%s Failed, Driver %s, for %s\n",
             __FUNCTION__, shost->hostt->name, vmklnx_get_vmhba_name(shost));
      }
   }

   /*
    * Time to complete commands to vmkernel that were completed during the
    * the time we were aborting. We dont have to hold the lock during
    * the entire time and instead lock just when we do a list_move_tail, 
    * but considering reentrancy, this makes the code read bit simpler
    */
   spin_lock_irqsave(&sdev->list_lock, flags);
   list_for_each_entry_safe(scmd, safecmd, &abort_list, list) {

      VMK_ASSERT(scmd->vmkflags & VMK_FLAGS_DELAY_CMDDONE);

      spin_lock_irqsave(&scmd->vmklock, vmkflags);
      scmd->vmkflags &= ~VMK_FLAGS_DELAY_CMDDONE;

      list_move_tail(&scmd->list, &sdev->cmd_list);

      if (scmd->vmkflags & VMK_FLAGS_CMDDONE_ATTEMPTED) {
         spin_unlock_irqrestore(&scmd->vmklock, vmkflags);
         /*
          * This will trigger a BH for completion.
          * Note that abort can complete before the BH had a
          * chance to run
          */
         scmd->scsi_done(scmd);
      } else {
         spin_unlock_irqrestore(&scmd->vmklock, vmkflags);
      }
   }
   spin_unlock_irqrestore(&sdev->list_lock, flags);

   vmk_LogDebug(vmklinux26Log, 4, "%s exit\n",__FUNCTION__);   

   return retval;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxAbortCommand --
 *
 *      Abort a single command
 *
 * Results:
 *      VMK_OK, if cmd was aborted or not running
 *      VMK_FAILURE, if abort failed for some reason
 *
 * Side effects:
 *      This function is called with  VMK_FLAGS_DELAY_CMDDONE set    
 * 
 *
 *----------------------------------------------------------------------
 */
static VMK_ReturnStatus
SCSILinuxAbortCommand(struct Scsi_Host *shost, struct scsi_cmnd *scmd, 
                      int32_t abortReason)
{
   int abortStatus = FAILED;
   unsigned long flags = 0;
   struct vmklnx_ScsiIntCmd *vmklnx26_ScsiIntCmd;

   vmk_LogDebug(vmklinux26Log, 4, "cmd=%p, ser=%lu, op=0x%x", 
         scmd, scmd->serial_number, scmd->cmnd[0]);

   if (vmkApiDebug && (scmd->vmkflags & VMK_FLAGS_DROP_CMD)) {
      // Fake a completion for this abort command
      scmd->result = (DID_ABORT << 16);
      if ((scmd->vmkflags & VMK_FLAGS_NEED_CMDDONE) &&
          VMKLNX_STRESS_DEBUG_COUNTER(stressVmklnxDropCmdScsiDone)) {
         VMK_ASSERT(scmd->vmkflags & 
		(VMK_FLAGS_NEED_CMDDONE|VMK_FLAGS_CMDDONE_ATTEMPTED));
         vmk_LogDebug(vmklinux26Log, 0, "dropCmd calling scsi_done \n");
         scmd->scsi_done(scmd);
      } else {
         vmk_LogDebug(vmklinux26Log, 0, "dropCmd NOT calling scsi_done \n");
      }
      if (VMKLNX_STRESS_DEBUG_COUNTER(stressVmklinuxAbortCmdFailure)) {
         vmk_LogDebug(vmklinux26Log, 0, "dropCmd returning FAILED \n");
         abortStatus = FAILED;
      } else {
         vmk_LogDebug(vmklinux26Log, 0, "dropCmd returning SUCCESS \n");
         abortStatus = SUCCESS;
      }
   } else if (VMKLNX_STRESS_DEBUG_COUNTER(stressVmklinuxAbortCmdFailure)) {
         vmk_LogDebug(vmklinux26Log, 0, "VMKLINUX_ABORT_CMD_FAILURE "
		"Stress Fired\n");
         abortStatus = FAILED;
   } else {
      abortStatus = SCSILinuxTryAbortCommand(scmd, ABORT_TIMEOUT);
   }

   // Convert FAILED/SUCCESS into vmkernel error handling codes
   switch (abortStatus) {
   case SUCCESS:
      spin_lock_irqsave(&scmd->vmklock, flags);
      if (scmd->vmkflags & VMK_FLAGS_INTERNAL_COMMAND) {
         spin_unlock_irqrestore(&scmd->vmklock, flags);

         if (abortReason == DID_TIME_OUT) {
            scmd->result = DID_TIME_OUT << 16;
         }
         scmd->serial_number = 0;

         /*
          * Get pointer to internal Command
          */
         vmklnx26_ScsiIntCmd = (struct vmklnx_ScsiIntCmd *) scmd->vmkCmdPtr;

         /*
          * For internal commands, we need to call up for the command's
          * semaphore.
          */
         up(&vmklnx26_ScsiIntCmd->sem);

         return VMK_OK;
      } else if (!(scmd->vmkflags & VMK_FLAGS_CMDDONE_ATTEMPTED)) {
         spin_unlock_irqrestore(&scmd->vmklock, flags);
         vmk_WarningMessage("%s - The driver failed to call scsi_done from its"
             "abort handler and yet it returned SUCCESS\n", __FUNCTION__);
         return VMK_FAILURE;
      } else {
         spin_unlock_irqrestore(&scmd->vmklock, flags);
         return VMK_OK;
      }
   default:
      vmk_WarningMessage("%s - unexpected abortStatus = %x \n", 
	__FUNCTION__, abortStatus);
   case FAILED:
      spin_lock_irqsave(&scmd->vmklock, flags);
      if (scmd->vmkflags & VMK_FLAGS_CMDDONE_ATTEMPTED) {
         spin_unlock_irqrestore(&scmd->vmklock, flags);
         return VMK_OK;
      }
      spin_unlock_irqrestore(&scmd->vmklock, flags);
      return VMK_FAILURE;
   }

   VMK_NOT_REACHED();
}

/*
 * Function:	SCSILinuxTryAbortCommand
 *
 * Purpose:	Ask host adapter to abort a running command.
 *
 * Returns:	FAILED		Operation failed or not supported.
 *		SUCCESS		Succeeded.
 *
 * Notes:   This function will not return until the user's completion
 *	         function has been called.  There is no timeout on this
 *          operation.  If the author of the low-level driver wishes
 *          this operation to be timed, they can provide this facility
 *          themselves.  Helper functions in scsi_error.c can be supplied
 *          to make this easier to do.
 *
 * Notes:	It may be possible to combine this with all of the reset
 *	         handling to eliminate a lot of code duplication.  I don't
 *	         know what makes more sense at the moment - this is just a
 *	         prototype.
 */
int
SCSILinuxTryAbortCommand(struct scsi_cmnd * scmd, int timeout)
{
   int status;

   if (scmd->device->host->hostt->eh_abort_handler == NULL) {
      return FAILED;
   }

   /* 
    * scsi_done was called just after the command timed out and before
    * we had a chance to process it. (DB)
    */
   if (scmd->serial_number == 0)
      return SUCCESS;

   VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(scmd->device->host),
              status, scmd->device->host->hostt->eh_abort_handler, scmd);

   return status;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxDoReset
 *
 *     Do a synchronous reset by calling appropriate driver entry point.
 * 
 * Results:
 *      VMK_OK, driver said reset succeeded.
 *      VMK_FAILURE, driver said reset failed in some way.
 *
 *----------------------------------------------------------------------
 */
static VMK_ReturnStatus
SCSILinuxDoReset(struct Scsi_Host *shost, vmk_ScsiTaskMgmt *vmkTaskMgmtPtr,
		 struct scsi_cmnd *scmd)
{
   VMK_ReturnStatus retval = VMK_OK;
   int resetStatus = FAILED;
   unsigned long flags = 0;

   switch(vmkTaskMgmtPtr->type) {
      case VMK_SCSI_TASKMGMT_LUN_RESET:
         spin_lock_irqsave(&scmd->vmklock, flags);
	 scmd->vmkflags |= VMK_FLAGS_USE_LUNRESET;
         spin_unlock_irqrestore(&scmd->vmklock, flags);
	 // Fall through
      case VMK_SCSI_TASKMGMT_DEVICE_RESET:
         resetStatus = SCSILinuxTryBusDeviceReset(scmd, 0);
	 break;
      case VMK_SCSI_TASKMGMT_BUS_RESET:
         resetStatus = SCSILinuxTryBusReset(scmd);
	 break;
      default:
	 VMKLNX_NOT_IMPLEMENTED();
	 break;
   }
   if (resetStatus == FAILED) {
         retval = VMK_FAILURE;
   }

   return retval;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxResetCommand --
 *
 *      Reset the SCSI bus. We would like the bus reset to always occur
 *      and therefore we create a cmdPtr and forces a SYNCHRONOUS reset
 *      on it. This is similar to what Linux does when you issue a
 *      reset through the Generic SCSI interface (the function
 *      scsi_reset_provider in drivers/scsi/scsi.c). Some drivers like
 *      Qlogic and mptscsi will call the completion for the cmdPtr we
 *      pass in if they cannot find the command (and they won't since
 *      we just create a cmdPtr to fill out).
 *
 * Results:
 *      VMK_OK, driver said reset succeeded.
 *      VMK_FAILURE, driver said reset failed in some way.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus
SCSILinuxResetCommand(struct Scsi_Host *shost, struct scsi_device *sdev,
		      vmk_ScsiTaskMgmt *vmkTaskMgmtPtr)
{
   struct scsi_cmnd *scmd; 
   VMK_ReturnStatus retval;
   unsigned long flags;
   struct vmklnx_ScsiIntCmd *vmklnx26_ScsiIntCmd;
 
   VMK_ASSERT(sdev != NULL);

   /* Since we may have split a command, we want to do a full synchronous
    * BUS reset to get all outstanding commands back, since simply picking
    * one fragment of a command may cause that reset to fail because that
    * fragment just happened to complete before we called SCSILinuxDoReset
    */
   vmklnx26_ScsiIntCmd = VMKLinux26_Alloc(sizeof(struct vmklnx_ScsiIntCmd));

   if(vmklnx26_ScsiIntCmd == NULL) {
      vmk_WarningMessage("Failed to Allocate SCSI internal Command");
      return VMK_NO_MEMORY;
   }

   VMK_DEBUG_ONLY(
      spin_lock_irqsave(&sdev->list_lock, flags);

      list_for_each_entry(scmd, &sdev->cmd_list, list) {
         // Fake a completion if we threw it away...
         if (scmd->serial_number &&
	     (scmd->vmkflags & VMK_FLAGS_DROP_CMD) &&
	     vmk_ScsiQueryTaskMgmt(vmkTaskMgmtPtr, scmd->vmkCmdPtr) != 
	     VMK_SCSI_TASKMGMT_ACTION_IGNORE) {
	    scmd->result = (DID_RESET << 16);
	    SCSILinuxCmdDone(scmd);
	 }
      }
      spin_unlock_irqrestore(&sdev->list_lock, flags);
   )

   /*
    * Fill in values required for command structure
    * Fill in vmkCmdPtr with vmklnx26_ScsiIntCmd pointer
    * Fill scsi_done with InternalCmdDone routine
    */
   SCSILinuxInitInternalCommand(sdev, vmklnx26_ScsiIntCmd);

   scmd = &vmklnx26_ScsiIntCmd->scmd;

   /*
    * Set the state of this command as timed out
    * This is only used in completion if drivers call this
    */
   scmd->eh_eflags = SCSI_STATE_TIMEOUT;
   spin_lock_irqsave(&scmd->vmklock, flags);
   scmd->vmkflags = VMK_FLAGS_TMF_REQUEST;
   spin_unlock_irqrestore(&scmd->vmklock, flags);

   retval = SCSILinuxDoReset(shost, vmkTaskMgmtPtr, scmd);

   /*
    * Free the command structure
    */
   VMKLinux26_Free(vmklnx26_ScsiIntCmd);

   return retval;
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxSignalBH --
 *
 *      Tell the BH to run if it's not already running or set to run.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static inline void
SCSILinuxSignalBH(scsiLinuxTLS_t *tls,
                  int myPCPU) {
   VMK_ASSERT(myPCPU == vmk_GetPCPUNum());
   VMK_ASSERT_CPU_HAS_INTS_DISABLED();
   if (tls->runningBH == VMK_FALSE) {
      tls->runningBH = VMK_TRUE;
      /*
       * Local PCPU, so sets a bit and doesn't do an IPI, so
       * cheap to do with interrupts disabled.
       */
      vmk_BottomHalfSchedulePCPU(scsiLinuxBH, myPCPU);
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxScheduleCompletion
 *
 *      Adds the given scsi_cmnd to the completion list and schedules
 *      a bottom half.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Mutates the given scsiLinuxTLS_t.
 *
 *----------------------------------------------------------------------
 */
static inline void SCSILinuxScheduleCompletion(struct scsi_cmnd *scmd,
					       scsiLinuxTLS_t *tls,
					       int myPCPU)
{
   if (vmk_CPUHasIntsEnabled()) {
      vmk_CPUDisableInterrupts();
      list_add_tail(&scmd->bhlist, &tls->isrDoneCmds);
      mb();
      SCSILinuxSignalBH(tls, myPCPU);
      vmk_CPUEnableInterrupts();
   } else {
      list_add_tail(&scmd->bhlist, &tls->isrDoneCmds);
      mb();
      SCSILinuxSignalBH(tls, myPCPU);
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxCmdDone --
 *
 *      Callback when a SCSI command completes. Typically from ISR 
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      This function can be called with list_lock held
 *
 *----------------------------------------------------------------------
 */

void 
SCSILinuxCmdDone(struct scsi_cmnd *scmd)
{
   unsigned long flags = 0;
   unsigned myPCPU = vmk_GetPCPUNum();
   scsiLinuxTLS_t *tls = scsiLinuxTLS[myPCPU];

#ifdef SCSI_LINUX_DEBUG
   vmk_LogDebug(vmklinux26Log, 0, "%s - cmd 0x%x sn=%d result=%d \n", 
       __FUNCTION__, scmd->cmnd[0],
       scmd->rid->token->originSN,
       scmd->result);
#endif

   spin_lock_irqsave(&scmd->vmklock, flags);
   if (unlikely((scmd->vmkflags & VMK_FLAGS_IO_TIMEOUT) != 0)) {
      spin_unlock_irqrestore(&scmd->vmklock, flags);
      scsi_delete_timer(scmd);
      spin_lock_irqsave(&scmd->vmklock, flags);
   }

   if (unlikely((scmd->vmkflags & VMK_FLAGS_TMF_REQUEST) != 0)) {
      spin_unlock_irqrestore(&scmd->vmklock, flags);
      vmk_LogDebug(vmklinux26Log, 2,"%s - Command Completion on TMF's scmd\n",
			 __FUNCTION__);
      return;
   }

   if (unlikely(!(scmd->vmkflags & VMK_FLAGS_NEED_CMDDONE))) {
      spin_unlock_irqrestore(&scmd->vmklock, flags);
      vmk_AlertMessage("Attempted double completion\n");
      VMK_NOT_REACHED();
   }

   if (unlikely((scmd->vmkflags & VMK_FLAGS_DELAY_CMDDONE) != 0)) {
      scmd->vmkflags |= VMK_FLAGS_CMDDONE_ATTEMPTED;
      spin_unlock_irqrestore(&scmd->vmklock, flags);
      return;
   }

   scmd->vmkflags &= ~VMK_FLAGS_NEED_CMDDONE;
   spin_unlock_irqrestore(&scmd->vmklock, flags);

   /*
    * Reset the Serial Number to indicate the command is no more
    * active with the driver
    */
   scmd->serial_number = 0; 

   /* Fail cmd on underflow since many drivers fail to do so */
   if (likely(scmd->result == DID_OK)) {
      if (unlikely(scmd->resid < 0)) {
         vmk_WarningMessage("%s - Driver reported -ve resid, yet had "
		"cmd success \n", __FUNCTION__);
         scmd->result = DID_ERROR << 16;
      } else if (unlikely(scmd->request_bufflen - scmd->resid <
                                                             scmd->underflow)) {
         scmd->result = DID_ERROR << 16;
         vmk_WarningMessage("%s - Underrun detected: SN %#"VMK_FMT64"x, "
		"initiator %p\n", __FUNCTION__,
          	scmd->vmkCmdPtr->cmdId.serialNumber,
          	scmd->vmkCmdPtr->cmdId.initiator);
      }
   }

   /*
    * Add to the completion list and schedule a BH.
    */
   SCSILinuxScheduleCompletion(scmd, tls, myPCPU);
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxInitCommand --
 *
 *      Initialize Linux SCSI command Pointer
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
SCSILinuxInitCommand(struct scsi_device *sdev, struct scsi_cmnd *scmd)
{
   VMK_ASSERT(sdev->host != NULL);

   scmd->serial_number = SCSILinuxGetSerialNumber();
   scmd->jiffies_at_alloc = jiffies;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxDVCommandDone --
 *
 *      Completion routine for internal DV commands.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Removes the timer associated with the DV command and calls
 *      "up" for its semaphore.
 *     
 *----------------------------------------------------------------------
 */
static void
SCSILinuxDVCommandDone(struct scsi_cmnd *scmd)
{
   struct vmklnx_ScsiIntCmd *vmklnx26_ScsiIntCmd;

   /*
    * vmkCmdPtr is valid for DV Commands
    */
   VMK_ASSERT(scmd->vmkCmdPtr);

   /*
    * Get pointer to internal Command
    */
   vmklnx26_ScsiIntCmd = (struct vmklnx_ScsiIntCmd *) scmd->vmkCmdPtr;

   /*
    * Remove timer
    */
   scsi_delete_timer(scmd);

   /*
    * Wake up the waiting thread
    */
   up(&vmklnx26_ScsiIntCmd->sem);
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxInitInternalCommand --
 *
 *      Initialize an vmklinux internal SCSI command
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
SCSILinuxInitInternalCommand(
                struct scsi_device *sdev,
                struct vmklnx_ScsiIntCmd *vmklnx26_ScsiIntCmd)
{
   struct scsi_cmnd *scmd;

   VMK_ASSERT(vmklnx26_ScsiIntCmd);
   VMK_ASSERT(sdev);

   scmd = &vmklnx26_ScsiIntCmd->scmd;
   VMK_ASSERT(scmd);

   scmd->vmkflags |= VMK_FLAGS_INTERNAL_COMMAND;
   INIT_LIST_HEAD(&scmd->bhlist);
   spin_lock_init(&scmd->vmklock);
   sema_init(&vmklnx26_ScsiIntCmd->sem, 0);
   scmd->device = sdev;

   SCSILinuxInitCommand(sdev, scmd);

   if (scmd->device->tagged_supported) {
        scmd->tag = SIMPLE_QUEUE_TAG;
   } else {
        scmd->tag = 0;
   } 
   scmd->done = SCSILinuxInternalCommandDone;
   scmd->scsi_done = SCSILinuxInternalCommandDone;
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxInitScmd --
 *
 *      Initialize a scsi command. Used in the IO path
 *
 * Results:
 *      None.
 *
 * Side effects:
 *       None
 *     
 *----------------------------------------------------------------------
 */
void
SCSILinuxInitScmd(struct scsi_device *sdev, struct scsi_cmnd *scmd)
{
   SCSILinuxInitCommand(sdev, scmd);
   scmd->scsi_done = SCSILinuxCmdDone;
   scmd->done = SCSILinuxCmdDone;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxTryBusDeviceReset --
 *
 *	Ask host adapter to perform a bus device reset for a given device.
 *
 * Results:
 *      FAILED or SUCCESS
 *
 * Side effects:
 * 	There is no timeout for this operation.  If this operation is
 *      unreliable for a given host, then the host itself needs to put a
 *      timer on it, and set the host back to a consistent state prior
 *      to returning.
 *     
 *----------------------------------------------------------------------
 */
static int
SCSILinuxTryBusDeviceReset(struct scsi_cmnd * scmd, int timeout)
{
   int rtn;

   if (scmd->device->host->hostt->eh_device_reset_handler == NULL) {
      return FAILED;
   }
  
   vmk_LogDebug(vmklinux26Log, 2,"%s - Start\n", __FUNCTION__);
   VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(scmd->device->host),
              rtn, scmd->device->host->hostt->eh_device_reset_handler, scmd);
   vmk_LogDebug(vmklinux26Log, 2,"%s - End\n", __FUNCTION__);

   return rtn;
}

#define BUS_RESET_SETTLE_TIME   5000 /* ms */

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxTryBusReset --
 *
 * 	Ask host adapter to perform a bus reset for a host.
 *
 * Results:
 *      SUCCESS or FAILED
 *
 * Side effects:
 *      None. Caller must host adapter lock
 *     
 *----------------------------------------------------------------------
 */
static int
SCSILinuxTryBusReset(struct scsi_cmnd * scmd)
{
   int rtn;

   if (scmd->device->host->hostt->eh_bus_reset_handler == NULL) {
      return FAILED;
   }

   vmk_LogDebug(vmklinux26Log, 2,"%s - Start\n", __FUNCTION__);
   VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(scmd->device->host),
              rtn, scmd->device->host->hostt->eh_bus_reset_handler, scmd);
   vmk_LogDebug(vmklinux26Log, 2,"%s - End\n", __FUNCTION__);

   /*
    * If we had a successful bus reset, mark the command blocks to expect
    * a condition code of unit attention.
    */
   vmk_WorldSleep(BUS_RESET_SETTLE_TIME * 1000); 

#if 0
   /*
    * Need to look up where this is cleared
    */
   if (rtn == SUCCESS) {
      struct scsi_device *sdev;
      shost_for_each_device(sdev, scmd->device->host) {
         if (scmd->device->channel == sdev->channel) {
            sdev->was_reset = 1;
            sdev->expecting_cc_ua = 1;
         }
      }
   }
#else
   /*
    * No consumers for this state. Will remove post all driver bring up
    */
#endif

   return rtn;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxProcessInquiryResponse --
 *
 *      Parse the response to a standard inquiry command and populate
 *      the device accordingly
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */
static void
SCSILinuxProcessStandardInquiryResponse(struct scsi_cmnd *scmd)
{
   struct scsi_device  *sdev;
   vmk_ScsiInquiryCmd *inqCmd;
   VMK_ReturnStatus status;
   int type;
   unsigned long flags;

   inqCmd = (vmk_ScsiInquiryCmd *) scmd->cmnd;

   if (inqCmd->opcode != VMK_SCSI_CMD_INQUIRY || inqCmd->evpd) {
      return;
   }

   sdev = scmd->device;

   vmk_LogDebug(vmklinux26Log, 5,"%s - Start\n", __FUNCTION__);

   spin_lock_irqsave(sdev->host->host_lock, flags);

   if (sdev->vmkflags & HAVE_CACHED_INQUIRY) {
      goto done;
   }

   status = vmk_CopyFromSg(sdev->inquiryResult,
                           scmd->vmkCmdPtr->sgArray,
                           min(sizeof(sdev->inquiryResult),
                                     (size_t)scmd->vmkCmdPtr->bytesXferred));
   if (status != VMK_OK) {
      goto done;
   }

   sdev->vendor = (char *) (sdev->inquiryResult + 8);
   sdev->model = (char *) (sdev->inquiryResult + 16);
   sdev->rev = (char *) (sdev->inquiryResult + 32);

   /* 
    * Some of the macro's need to access this data at a later time
    */
   sdev->inquiry = (unsigned char *) (sdev->inquiryResult);

   sdev->removable = (0x80 & sdev->inquiryResult[1]) >> 7;
   sdev->lockable = sdev->removable;

   sdev->inq_periph_qual = (sdev->inquiryResult[0] >> 5) & 7;
      
   sdev->soft_reset = (sdev->inquiryResult[7] & 1) && 
			((sdev->inquiryResult[3] & 7) == 2);
      
   sdev->scsi_level = sdev->inquiryResult[2] & 0x07;
   if (sdev->scsi_level >= 2 || 
       (sdev->scsi_level == 1 && (sdev->inquiryResult[3] & 0x0f) == 1)) {
      sdev->scsi_level++;
   }

   sdev->inquiry_len = sdev->inquiryResult[4] + 5;

   if (sdev->scsi_level >= SCSI_3 || (sdev->inquiry_len > 56 &&
		sdev->inquiryResult[56] & 0x04)) {
      sdev->ppr = 1;
   }

   if (sdev->inquiryResult[7] & 0x60) {
      sdev->wdtr = 1;
   }
   if (sdev->inquiryResult[7] & 0x10) {
      sdev->sdtr = 1;
   }

   sdev->use_10_for_rw = 1;
      
   /*
    * Currently, all sequential devices are assumed to be tapes, all random
    * devices disk, with the appropriate read only flags set for ROM / WORM
    * treated as RO.
    */
   type = (sdev->inquiryResult[0] & 0x1f);
   switch (type) {
   case TYPE_PROCESSOR:
   case TYPE_TAPE:
   case TYPE_DISK:
   case TYPE_PRINTER:
   case TYPE_MOD:
   case TYPE_SCANNER:
   case TYPE_MEDIUM_CHANGER:
   case TYPE_ENCLOSURE:
      sdev->writeable = 1;
      break;
   case TYPE_WORM:
   case TYPE_ROM:
      sdev->writeable = 0;
      break;
   default:
      break;
   }

   sdev->type = (type & 0x1f);

   /*
    * Set the tagged_queue flag for SCSI-II devices that purport to support
    * tagged queuing in the INQUIRY data.
    */
   if ((sdev->scsi_level >= SCSI_2) && (sdev->inquiryResult[7] & 2)) {
      sdev->tagged_supported = 1;
      sdev->current_tag = 0;
   }

   /*
    * VMKLINUX FLAG - Safe under host_lock
    */ 
   sdev->vmkflags |= HAVE_CACHED_INQUIRY;

   /*
    * The fields need to be initialized on a need basis, None so far
    * fields are
    * sdev->borken
    * sdev->select_no_atn
    * sdev->no_start_on_add
    * sdev->single_lun
    * sdev->skip_ms_page_8
    * sdev->skip_ms_page_3f
    * sdev->use_10_for_ms
    * sdev->use_192_bytes_for_3f
    * sdev->retry_hwerror
    */
done:
   spin_unlock_irqrestore(sdev->host->host_lock, flags);
   vmk_LogDebug(vmklinux26Log, 5,"%s - End\n", __FUNCTION__);
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxInternalCommandDone --
 *
 *      SCSI internal Command Completion routine 
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
SCSILinuxInternalCommandDone(struct scsi_cmnd *scmd)
{
   struct vmklnx_ScsiIntCmd *vmklnx26_ScsiIntCmd;
   unsigned long flags = 0;
   unsigned myPCPU = vmk_GetPCPUNum();
   scsiLinuxTLS_t *tls = scsiLinuxTLS[myPCPU];

   spin_lock_irqsave(&scmd->vmklock, flags);

   /*
    * Some drivers call Complete Command even on Reset requests
    */
   if (unlikely((scmd->vmkflags & VMK_FLAGS_TMF_REQUEST) != 0)) {
      spin_unlock_irqrestore(&scmd->vmklock, flags);
      vmk_LogDebug(vmklinux26Log, 2,"%s - Command Completion on TMF's scmd\n",
                         __FUNCTION__);
      return;
   }

   /*
    * vmkCmdPtr is valid for Dump and DV Commands
    */

   VMK_ASSERT(scmd->vmkCmdPtr);

   /*
    * Get pointer to internal Command
    */
   vmklnx26_ScsiIntCmd = (struct vmklnx_ScsiIntCmd *) scmd->vmkCmdPtr;

   /*
    * Check if this is dump completion; if so, we complete the
    * command directly.
    */
   if (unlikely((scmd->vmkflags & VMK_FLAGS_DUMP_REQUEST) != 0)) {
      spin_unlock_irqrestore(&scmd->vmklock, flags);
      SCSILinuxDumpCmdDone(vmklnx26_ScsiIntCmd);
      return;
   }

   /*
    * Internal commands are used only for Dump, Task Mgmt and
    * DV requests. We have handled the first 2 cases above. 
    * Third case - DV sets up a command timer
    * Check if the command has timed out already
    */
   if (scmd->vmkflags & VMK_FLAGS_NEED_CMDDONE) {
      scmd->serial_number = 0;
      scmd->vmkflags &= ~VMK_FLAGS_NEED_CMDDONE;
      spin_unlock_irqrestore(&scmd->vmklock, flags);

      scmd->done = SCSILinuxDVCommandDone;
      scmd->scsi_done = SCSILinuxDVCommandDone;

      /*
       * Add to the completion list and schedule a BH.
       */
      SCSILinuxScheduleCompletion(scmd, tls, myPCPU);
   } else {
      spin_unlock_irqrestore(&scmd->vmklock, flags);
      vmk_WarningMessage("%s - Command was timed out."
	" Dropping the completion\n", __FUNCTION__);
      return;
   }
}


/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxAddPaths--
 *
 *     Add new paths
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus 
SCSILinuxAddPaths(vmk_ScsiAdapter *vmkAdapter, unsigned int channel,
                 unsigned int target, unsigned int lun)
{
   int busyCount;
   VMK_ReturnStatus ret = VMK_FAILURE;
   unsigned int channelMap = channel, targetMap = target, lunMap = lun;

   vmk_WorldAssertIsSafeToBlock();

   /*
    * VMKernel does not deal with scan wild cards
    */
   if (channelMap == SCAN_WILD_CARD) {
      channelMap = VMK_SCSI_PATH_ANY_CHANNEL;
   }

   if (targetMap == SCAN_WILD_CARD) {
      targetMap = VMK_SCSI_PATH_ANY_TARGET;
   }

   if (lunMap == SCAN_WILD_CARD) {
      lunMap = VMK_SCSI_PATH_ANY_LUN;
   }

   for (busyCount = 0; busyCount < 60; ++busyCount) {
      ret = vmk_ScsiScanAndClaimPaths(vmkAdapter->name, channelMap, 
	targetMap, lunMap);

      if (ret == VMK_BUSY) {
         /*
          * Sleep in micro seconds
          */
         vmk_WorldSleep(1000000);
      } else {
         /*
          * Not a busy state. So break out
          */
         break;
      }
   }
   return ret;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxCreatePath --
 *
 *     Create a path. Returns status and pointer to sdev that is newly 
 * allocated
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus SCSILinuxCreatePath(struct Scsi_Host *sh,
                int channel, int target, int lun, struct scsi_device **sdevdata)
{
   struct scsi_device  *sdev;
   struct scsi_target  *stgt;
   unsigned long flags;

   if (sh->reverse_ordering) {
      vmk_WarningMessage("Scanning in reverse order not supported\n");
      return VMK_FAILURE;
   }

   stgt = vmklnx_scsi_alloc_target(&sh->shost_gendev, channel, target);

   if (!stgt) {
      return VMK_NO_CONNECT;
   }

   /*
    * Dont really care what state sdev is. As long as sdev is in the list
    * fail any path creation request. The drivers dont want to receive
    * any requests that is either created or being deleted
    */
   spin_lock_irqsave(sh->host_lock, flags);
   sdev = __scsi_device_lookup(sh, channel, target, lun);
   spin_unlock_irqrestore(sh->host_lock, flags);

   if (sdev) {
      vmk_WarningMessage("Trying to discover path (%s:%d:%d:%d) that is"
              "present with state %d\n", vmklnx_get_vmhba_name(sh), channel, 
		target, lun, sdev->sdev_state);

      return VMK_BUSY;
   }

   sdev = scsi_alloc_sdev(stgt, lun, NULL);

   if (!sdev) {
      return VMK_NO_MEMORY;
   }

   if (IS_ERR(sdev)) {
      return VMK_NO_CONNECT;
   }

   *sdevdata = sdev;

   return VMK_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxConfigurePath --
 *
 *     Configure a path if it is found. 
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus SCSILinuxConfigurePath(struct Scsi_Host *sh,
                int channel, int target, int lun)
{
   struct scsi_device  *sdev;
   unsigned long flags;

   sdev = scsi_device_lookup(sh, channel, target, lun);
   if (sdev) {
      /*
       * Down the reference count now
       */
      scsi_device_put(sdev);

      if (sh->hostt->slave_configure) {
         int ret;

         VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(sh), ret,
                    sh->hostt->slave_configure, sdev);

         if (ret) {
           return VMK_FAILURE;
         }
      }
      /*
      * If no_uld_attach is ON, return VMK_NOT_FOUND to hide the device
      * from upper layer.
      */
      if (sdev->no_uld_attach) {
        return VMK_NOT_FOUND;
      }
   } else {
      return VMK_FAILURE;
   }

   spin_lock_irqsave(sh->host_lock, flags);
   sdev->sdev_state = SDEV_RUNNING;
   spin_unlock_irqrestore(sh->host_lock, flags);

   return VMK_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxDestroyPath --
 *
 *     Destroy a given path. 
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus SCSILinuxDestroyPath(struct Scsi_Host *sh,
                int channel, int target, int lun)
{
   struct scsi_device  *sdev;
   unsigned long flags;

   /*
    * Does not matter what state sdev is
    */
   spin_lock_irqsave(sh->host_lock, flags);
   sdev = __scsi_device_lookup(sh, channel, target, lun);
   spin_unlock_irqrestore(sh->host_lock, flags);

   if (sdev) {
      scsi_destroy_sdev(sdev);
   } else {
      return VMK_FAILURE;
   }

   return VMK_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSILinuxCmdTimedOut --
 *
 *     Handle a timed-out command
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
SCSILinuxCmdTimedOut(struct scsi_cmnd *scmd)
{
   unsigned long flags;

   VMK_ASSERT((scmd->vmkflags & VMK_FLAGS_IO_TIMEOUT) ||
              (scmd->vmkflags & VMK_FLAGS_INTERNAL_COMMAND));

   spin_lock_irqsave(&scmd->vmklock, flags);

   scmd->vmkflags &= ~VMK_FLAGS_IO_TIMEOUT;

   /*
    * Check if the command timed out or completed already
    */
   if (scmd->vmkflags & VMK_FLAGS_NEED_CMDDONE) {
      if (unlikely((scmd->vmkflags & VMK_FLAGS_DELAY_CMDDONE) != 0)) {
         spin_unlock_irqrestore(&scmd->vmklock, flags);
         vmk_LogDebug(vmklinux26Log, 4, "cmd %p is already in the process of"
                 "being aborted", scmd);
         return;
      } else {
         scmd->vmkflags |= VMK_FLAGS_DELAY_CMDDONE;
      }
   } else {
      spin_unlock_irqrestore(&scmd->vmklock, flags);
      return;
   }

   /*
    * Note deviation -- we are clearing VMK_FLAGS_NEED_CMDDONE here,
    * when it would normally only be cleared by vmklinux when the
    * driver completes the IO.  It is possible that a completion for
    * the command is in-flight when we do this.
    */
   if (scmd->vmkflags & VMK_FLAGS_INTERNAL_COMMAND) {
      scmd->vmkflags &= ~VMK_FLAGS_NEED_CMDDONE;
      scmd->serial_number = 0;
   }

   spin_unlock_irqrestore(&scmd->vmklock, flags);

   /* Intialize work for timed-out command */
   INIT_WORK(&linuxSCSIIO_work, SCSIProcessCmdTimedOut, scmd);
   queue_work(linuxSCSIWQ, &linuxSCSIIO_work);

   return;
}

/*
 *----------------------------------------------------------------------
 *
 * SCSIProcessCmdTimedOut --
 *
 *     Handles timeout in case of command time outs
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
SCSIProcessCmdTimedOut(void *data)
{
   struct scsi_cmnd *scmd = (struct scsi_cmnd *)data;
   VMK_ReturnStatus status = VMK_OK;
   unsigned long vmkflags;


   vmk_LogDebug(vmklinux26Log, 4, "%s Aborting cmd\n", __FUNCTION__);

   status = SCSILinuxAbortCommand(scmd->device->host, scmd, DID_TIME_OUT);

   if (status == VMK_FAILURE) {
         vmk_WarningMessage("%s Failed, Driver %s, for %s\n",
             __FUNCTION__, scmd->device->host->hostt->name, vmklnx_get_vmhba_name(scmd->device->host));
   }

   /*
    * Time to complete command to vmkernel that were completed during the
    * the time we were aborting. 
    */
   
   VMK_ASSERT(scmd->vmkflags & VMK_FLAGS_DELAY_CMDDONE);

   spin_lock_irqsave(&scmd->vmklock, vmkflags);
   scmd->vmkflags &= ~VMK_FLAGS_DELAY_CMDDONE;

   if (unlikely((scmd->vmkflags & VMK_FLAGS_CMDDONE_ATTEMPTED) != 0)) {
      spin_unlock_irqrestore(&scmd->vmklock, vmkflags);
      /*
       * This will trigger a BH for completion.
       * Note that abort can complete before the BH had a
       * chance to run
       */
       scmd->scsi_done(scmd);
   } else {
      spin_unlock_irqrestore(&scmd->vmklock, vmkflags);
   }
   return;
}
