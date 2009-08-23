/* ****************************************************************
 * Portions Copyright 1998 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

/*
 * linux_scsi.h --
 *
 *      Linux SCSI Midlayer Emulation.
 */

#ifndef _LINUX_SCSI_H_
#define _LINUX_SCSI_H_

#include "vmkapi.h"

#include <linux/compiler.h>
#include <vmklinux26/vmklinux26_scsi.h>

#define SCSI_GET_NAME(hostPtr)  (((vmk_ScsiAdapter *)((struct vmklnx_ScsiAdapter *)hostPtr->adapter)->vmkAdapter)->name)
#define SCSI_MAX_NAME_LEN	40
#define SCSI_GET_MODULE_ID(hostPtr)     (((vmk_ScsiAdapter *)((struct vmklnx_ScsiAdapter *)hostPtr->adapter)->vmkAdapter)->moduleID)
/* Path name */
#define SCSI_PATH_NAME_FMT "%s:%d:%d"
#define SCSI_PATH_NAME(scsiHost, channel, target, lun) SCSI_GET_NAME(scsiHost),(target),(lun)

/* 
 * Might have to move this to vmkapi 
 */
#define VMK_NOTIFY_QUEUE_DEPTH_CHANGE   0x01

/*
 * Default timeout
 */
#define SCSI_TIMEOUT (2*HZ)
#define ABORT_TIMEOUT SCSI_TIMEOUT
#define RESET_TIMEOUT SCSI_TIMEOUT

#define SCSI_ANON_START_ID      VMK_CONST64U(32)
#define SCSI_MAX_CMDS_FOR_BH    25

/*
 * Convert linux combined status into vmkapi scsi command statuses.
 */
#define VMKLNX_SCSI_HOST_STATUS(status)   ((vmk_ScsiHostStatus)\
                                           (((status) >> 16) & 0xff))
#define VMKLNX_SCSI_DEVICE_STATUS(status) ((vmk_ScsiDeviceStatus)\
                                           ((status) & 0xff))

/* Drop the suggest nibble from a linux scsi status */
#define VMKLNX_SCSI_STATUS_NO_SUGGEST(status)  ((status) & 0x0fffffff)

/* Drop the driver and suggest bytes from a linux scsi status */
#define VMKLNX_SCSI_STATUS_NO_LINUX(status) ((status) & 0x00ffffff)

#define SCSILinuxCompleteCommand(cmd, hostStatus, deviceStatus)   \
   do {                                                           \
      SCSILinuxCompleteCommandInt(cmd, hostStatus, deviceStatus); \
      /* Ensure that the command isn't reused */                  \
      cmd = NULL;                                                 \
   } while(0)

#define VMKLNX_FC_HOST_READY		0x01
#define VMKLNX_FC_HOST_REMOVING		0x02

struct sas_rphy;

/*
 * Hold vmklnx SCSI module information
 */
struct vmklnx_ScsiModule {
   vmk_ModuleID  moduleID;
   struct scsi_host_template *sht;
   vmklnx_ScsiTransportType transportType; 
   void   *transportData;
};

/*
 * Internal Command Structure - Used by pSCSI to send down commands
 */
struct vmklnx_ScsiIntCmd {
   struct scsi_cmnd scmd;
   struct semaphore sem;
   vmk_Timer scmd_timer;
   vmk_ScsiCommand *vmkCmdPtr;
};

/*
 * Command Pool
 */
struct scsi_host_cmd_pool {
   struct kmem_cache_s	*slab;
   unsigned int		users;
   const char		*name;
   unsigned int		slab_flags;
   gfp_t		gfp_mask;
};

extern vmk_atomic64 SCSILinuxSerialNumber;

extern vmk_HeapID vmklnxScsiCmdHeap; /* linux_scsi.c && lld_if.c */
extern vmk_HeapID vmklnxScsiSgHeap; /* linux_scsi.c && lld_if.c */

/* 
 * Stress option handles
 */
extern vmk_StressOptionHandle stressScsiAdapterIssueFail;

static inline void
SCSILinuxCompleteCommandInt(vmk_ScsiCommand *cmd,
                            vmk_ScsiHostStatus hostStatus,
                            vmk_ScsiDeviceStatus deviceStatus)
{
   cmd->status.host = hostStatus;
   cmd->status.device = deviceStatus;
   cmd->done(cmd);
}

static inline unsigned long
SCSILinuxGetSerialNumber(void)
{
   unsigned long sn;
   
   sn = (unsigned long)vmk_AtomicReadInc64(&SCSILinuxSerialNumber);
   if (unlikely(sn == 0)) {
     sn = (unsigned long)vmk_AtomicReadInc64(&SCSILinuxSerialNumber);
   }

   return sn ;
}

struct scsi_target *scsi_alloc_target(struct device *parent,
					     int channel, uint id);
struct scsi_target *vmklnx_scsi_alloc_target(struct device *parent,
					     int channel, uint id);
struct scsi_device *scsi_device_lookup_by_target(struct scsi_target *stgt,
						 uint lun);
struct scsi_device *scsi_alloc_sdev(struct scsi_target *stgt,
					   unsigned int lun, void *hostdata);
void scsi_destroy_sdev(struct scsi_device *sdev);

int SCSILinuxGetDataDirection(struct scsi_cmnd * cmdPtr, 
				vmk_ScsiCommandDirection guestDataDirection);
void SCSILinuxInitCommand(struct scsi_device *devPtr, 
				struct scsi_cmnd *cmdPtr);
void SCSILinuxInitInternalCommand(struct scsi_device *sdev, 
		struct vmklnx_ScsiIntCmd *vmklnx26_ScsiIntCmd);
void SCSILinuxInitScmd(struct scsi_device *sdev, struct scsi_cmnd *scmd);
VMK_ReturnStatus SCSILinuxResetVirtCommand(struct Scsi_Host *shost, 
				struct scsi_device *sdev, 
				vmk_ScsiTaskMgmt *vmkTaskMgmtPtr);
int SCSILinuxTryAbortCommand(struct scsi_cmnd * scmd, int timeout);
vmk_ModuleID SCSILinuxGetModuleID(struct Scsi_Host *sh, struct pci_dev *pdev);
VMK_ReturnStatus SCSILinuxAbortCommands(struct Scsi_Host *shost, 
					struct scsi_device *sdev,
                       			vmk_ScsiTaskMgmt *vmkTaskMgmtPtr);
VMK_ReturnStatus SCSILinuxResetCommand(struct Scsi_Host *shost, 
					struct scsi_device *sdevice,
		      			vmk_ScsiTaskMgmt *vmkTaskMgmtPtr);
int SCSILinuxGetDataDirection(struct scsi_cmnd * cmdPtr, 
				vmk_ScsiCommandDirection guestDataDirection);
VMK_ReturnStatus SCSILinuxQueueCommand(struct Scsi_Host *sh, 
                        		struct scsi_device *devPtr, 
                        		vmk_ScsiCommand *vmkCmdPtr);
void SCSILinuxDumpCmdDone(struct vmklnx_ScsiIntCmd *vmklnx26_ScsiIntCmd);
void SCSILinuxCmdDone(struct scsi_cmnd *cmdPtr);
VMK_ReturnStatus SCSILinuxDiscover(void *clientData, 
		  vmk_ScanAction action,
		  int channel, int target, int lun,
		  void **deviceData);
VMK_ReturnStatus SCSILinuxCommand(void *clientData, vmk_ScsiCommand *vmkCmdPtr, 
                 			 void *deviceData);
VMK_ReturnStatus SCSILinuxProcInfo(void *clientData, char* buf, uint32_t offset,
               			  uint32_t count, uint32_t *nbytes,
                                  int isWrite);
void SCSILinuxClose(void *clientData);
VMK_ReturnStatus SCSILinuxIoctl(void *clientData, void *deviceData, 
                                uint32_t fileFlags,  uint32_t cmd, 
                                vmk_VirtAddr userArgsPtr,
                                vmk_IoctlCallerSize callerSize,
                                int32_t *drvErr);
int SCSILinuxModifyDeviceQueueDepth(void *clientData,
                                    int qDepth,
                                    void *deviceData);
int SCSILinuxQueryDeviceQueueDepth(void *clientData,
                                   void *deviceData);
VMK_ReturnStatus SCSILinuxCheckTarget(void *clientData,
                                      int channelNo,
                                      int targetNo);

VMK_ReturnStatus SCSILinuxDumpCommand(void *clientData, 
                                             vmk_ScsiCommand *vmkCmdPtr, 
					     void *deviceData);
VMK_ReturnStatus SCSILinuxVportOp(void *clientData, uint32_t cmd, 
					 void *args, int *drvErr);         
void SCSILinuxVportUpdate(void *clientdata, void *device);
VMK_ReturnStatus SCSILinuxVportDiscover(void *clientData,
                                       vmk_ScanAction action,
                                       int ctlr, int devNum, int lun,
                                       vmk_ScsiAdapter **vmkAdapter, 
                                       void **deviceData);
VMK_ReturnStatus SCSILinuxTaskMgmt(void *clientData, 
				   vmk_ScsiTaskMgmt *vmkTaskMgmt,
				     void *deviceData);
void SCSILinuxDumpQueue(void *clientData);
void SCSILinuxSetName(vmk_ScsiAdapter *vmkAdapter, struct Scsi_Host const *sHost,
                      struct pci_dev *pciDev);
void SCSILinuxNameAdapter(char *adapterName, struct pci_dev *pciDev);
void SCSILinuxInternalCommandTimedOut(unsigned long data);
void SCSILinuxInternalCommandDone(struct scsi_cmnd *scmd);
VMK_ReturnStatus SCSILinuxAddPaths(vmk_ScsiAdapter *vmkAdapter, 
	unsigned int channel, unsigned int target, unsigned int lun);
int vmklnx_fc_host_setup(struct Scsi_Host *shost);
void vmklnx_fc_host_free(struct Scsi_Host *shost);
struct scsi_target * vmklnx_scsi_find_target(struct Scsi_Host *sh,
					      int channel, uint id);
int FcLinuxAttachMgmtAdapter(struct Scsi_Host *shost);
void FcLinuxReleaseMgmtAdapter(struct Scsi_Host *shost);
int vmklnx_sas_host_setup(struct Scsi_Host *shost);
struct sas_rphy *sas_find_rphy(struct Scsi_Host *sh, uint id);
int SasLinuxAttachMgmtAdapter(struct Scsi_Host *shost);
void SasLinuxReleaseMgmtAdapter(struct Scsi_Host *shost);
void scsi_target_offline(struct device *dev);
struct scatterlist *scsi_alloc_sgtable(unsigned int nbElems);
VMK_ReturnStatus SCSILinuxCreatePath(struct Scsi_Host *shost,
		int channel, int target, int lun, struct scsi_device **sdev);
VMK_ReturnStatus SCSILinuxConfigurePath(struct Scsi_Host *shost,
		int channel, int target, int lun);
VMK_ReturnStatus SCSILinuxDestroyPath(struct Scsi_Host *shost,
		int channel, int target, int lun);
void SCSILinuxCmdTimedOut(struct scsi_cmnd *scmd);
int XsanLinuxAttachMgmtAdapter(struct Scsi_Host *shost);
void XsanLinuxReleaseMgmtAdapter(struct Scsi_Host *shost);
#endif /*_LINUX_SCSI_H_ */
