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
 * linux_scsi_transport.c
 *
 *      Linux scsi transport emulation. Covers transport functionality of 
 *      SCSI drivers - pSCSI, FC(with NPIV) and SAS
 *
 * From linux-2.6.18-8/drivers/scsi/scsi_transport_fc.c:
 *
 * Copyright (c) 2003 Silicon Graphics, Inc.  All rights reserved.
 * Copyright (C) 2004-2005   James Smart, Emulex Corporation
 *
 * From linux-2.6.18-8/drivers/scsi/scsi_transport_sas.c:
 *
 * Copyright (C) 2005-2006 Dell Inc.
 *
 * From linux-2.6.18-8/drivers/scsi/scsi_transport_spi.c:
 *
 * Copyright (c) 2003 Silicon Graphics, Inc.  All rights reserved.
 * Copyright (c) 2004, 2005 James Bottomley <James.Bottomley@SteelEye.com>
 *
 * From linux-2.6.18-8/drivers/scsi/scsi.c:
 *
 * Copyright (C) 1992 Drew Eckhardt
 * Copyright (C) 1993, 1994, 1995, 1999 Eric Youngdale
 * Copyright (C) 2002, 2003 Christoph Hellwig
 *
 ******************************************************************/

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_spi.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_transport_sas.h>
#include <vmklinux26/vmklinux26_scsi.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include "vmkapi.h"

#include "linux_scsi.h" /* To be used for SCSI emulation */
#include "linux_scsi_transport.h" /* To be used for SCSI transport */
#include "linux_stubs.h"

/* 
 * The PPR values at which you calculate the period in ns by multiplying
 * by 4 
 */
#define SPI_STATIC_PPR	0x0c

static const int ppr_to_ps[] = {
	/* The PPR values 0-6 are reserved, fill them in when
	 * the committee defines them */
	-1,			/* 0x00 */
	-1,			/* 0x01 */
	-1,			/* 0x02 */
	-1,			/* 0x03 */
	-1,			/* 0x04 */
	-1,			/* 0x05 */
	-1,			/* 0x06 */
	 3125,			/* 0x07 */
	 6250,			/* 0x08 */
	12500,			/* 0x09 */
	25000,			/* 0x0a */
	30300,			/* 0x0b */
	50000,			/* 0x0c */
};
static atomic_t fc_event_seq;

/*
 * dev_loss_tmo: the default number of seconds that the FC transport
 *   should insulate the loss of a remote port.
 *   The maximum will be capped by the value of SCSI_DEVICE_BLOCK_MAX_TIMEOUT.
 */
static unsigned int fc_dev_loss_tmo = 10;		/* seconds */
/*
 * remove_on_dev_loss: controls whether the transport will
 *   remove a scsi target after the device loss timer expires.
 *   Removal on disconnect is modeled after the USB subsystem
 *   and expects subsystems layered on SCSI to be aware of
 *   potential device loss and handle it appropriately. However,
 *   many subsystems do not support device removal, leaving situations
 *   where structure references may remain, causing new device
 *   name assignments, etc., if the target returns.
*/
static unsigned int fc_remove_on_dev_loss = 0;

static void 
spi_dv_device_internal(struct scsi_device *sdev, u8 *buffer);
static enum spi_compare_returns 
spi_dv_device_compare_inquiry(struct scsi_device *sdev, u8 *buffer,
			      u8 *ptr, const int retries);
static enum spi_compare_returns 
spi_dv_retrain(struct scsi_device *sdev, u8 *buffer, u8 *ptr, 
		enum spi_compare_returns 
	       (*compare_fn)(struct scsi_device *, u8 *, u8 *, int));
static int spi_execute(struct scsi_device *sdev, const void *cmd,
		       enum dma_data_direction dir,
		       void *buffer, unsigned bufflen,
		       struct scsi_sense_hdr *sshdr);
static int fc_queue_work(struct Scsi_Host *shost, struct work_struct *work);
static void fc_timeout_deleted_rport(void  *data);
static void fc_timeout_fail_rport_io(void  *data);
static void fc_scsi_scan_rport(void *data);
static void fc_starget_delete(void *data);
static void fc_rport_final_delete(void *data);
static void fc_flush_work(struct Scsi_Host *shost);
static void fc_flush_devloss(struct Scsi_Host *shost);
static int fc_queue_devloss_work(struct Scsi_Host *shost, 
	struct work_struct *work, unsigned long delay);
static void fc_rport_dev_release(struct device *dev);
static void fc_vport_dev_release(struct device *dev);
static void fc_vport_sched_delete(void *data);
static int vmk_fc_vport_terminate(struct fc_vport *vport);

static void sas_phy_release(struct device *dev);
static void sas_port_release(struct device *dev);
static void sas_end_device_release(struct device *dev);
static void sas_expander_release(struct device *dev);
static int sas_assign_scsi_target_id(struct sas_rphy *rphy,
                                  struct sas_host_attrs *sas_host);
static int sas_scsi_target_list_del(struct device *dev, void *data);

/**
 **********************************************************************
 * vmklnx_alloc_scsimod --                                       */ /**
 *
 * \brief alloc and init a vmklnx_ScsiModule
 *
 * \param type transport type
 * \param data transport data
 *
 * \retval initialized vmklnx_ScsiModule
 * \retval NULL out of memory
 *
 **********************************************************************
 */
static struct vmklnx_ScsiModule *
vmklnx_alloc_scsimod(vmklnx_ScsiTransportType type, void *data)
{
   struct vmklnx_ScsiModule *vmklnx26ScsiModule;

   vmklnx26ScsiModule = VMKLinux26_Alloc(sizeof(struct vmklnx_ScsiModule));
   VMK_ASSERT(vmklnx26ScsiModule);

   if (!vmklnx26ScsiModule) {
      vmk_WarningMessage("%s - Unable to allocate memory for"
	" vmklnx_ScsiModule\n", __FUNCTION__);
      return NULL;
   }

   vmklnx26ScsiModule->moduleID = vmk_ModuleStackTop();
   vmklnx26ScsiModule->transportType = type;
   vmklnx26ScsiModule->transportData = data;

   return vmklnx26ScsiModule;
}

/**
 **********************************************************************
 * \globalfn vmklnx_generic_san_attach_transport --              */ /**
 *
 * \brief attach a generic SAN transport vmklnx_ScsiModule
 *                                           
 *  Allocate and initialize all the vmklinux data structures and attach the
 *  passed in pointer to the xsan_function_template.
 *
 *  With generic SAN transport type, it allows unique adapter/target ID, RDMs and
 *  periodic rescanning for new LUNs.
 *
 * \param ft pointer to the xsan_function_template
 * \param target_size size of target(transport) attributes
 * \param host_size size of host attributes
 *
 * \retval initialized scsi_transport_template
 * \retval NULL out of memory
 *
 * \comments caller should free allocated generic transport when
 *            finished with vmklnx_generic_san_release_transport
 *
 **********************************************************************
 */
struct scsi_transport_template *
vmklnx_generic_san_attach_transport(
   struct xsan_function_template *ft,
   size_t target_size,
   size_t host_size
)
{
   struct vmklnx_ScsiModule *vmklnx26ScsiModule;
   struct xsan_internal *i = VMKLinux26_Alloc(sizeof(struct xsan_internal));
   
   if (unlikely(!i)) {
      vmk_WarningMessage("%s - Unable to allocate memory for xsan_internal\n",
        __FUNCTION__);
      return NULL;
   }

   vmklnx26ScsiModule =
      vmklnx_alloc_scsimod(VMKLNX_SCSI_TRANSPORT_TYPE_XSAN, i);

   if (!vmklnx26ScsiModule) {
      VMKLinux26_Free(i);
      return NULL;
   }

   i->t.module = (void *)vmklnx26ScsiModule;
   i->t.target_size = target_size;
   i->t.host_size = host_size;
   i->f = ft;

   return &i->t;
}

/**
 **********************************************************************
 * \globalfn vmklnx_generic_san_release_transport --             */ /**
 *
 * \brief Release generic transport
 *
 *  Releases an generic SAN transport previously registered with
 *  vmklnx_generic_san_attach_transport.
 *
 * \param t transport template
 *
 * \retval none
 *
 * \comments caller must have previously allocated generic transport
 *           with vmklnx_generic_san_attach_transport
 *
 **********************************************************************
 */
void
vmklnx_generic_san_release_transport(struct scsi_transport_template *t)
{
   struct xsan_internal *i = to_xsan_internal(t);

   VMK_ASSERT(t->module);
   VMK_ASSERT(((struct vmklnx_ScsiModule *)(t->module))->transportData == i);

   VMKLinux26_Free(t->module);
   VMKLinux26_Free(i);
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_xsan_host_setup --
 *
 *  Initialize generic SAN host attributes
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */
int
vmklnx_xsan_host_setup(struct Scsi_Host *shost)
{
   struct xsan_internal *i;
   int ret = 0;

   VMK_ASSERT(shost);
   /*
    * Create Mgmt Adapter Instance to our management
    */
   if (XsanLinuxAttachMgmtAdapter(shost)) {
      return -ENOMEM;
   }

   VMK_ASSERT(shost->transportt);
   i = to_xsan_internal(shost->transportt);

   if (i->f->setup_host_attributes) {
      VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(shost), ret,
         i->f->setup_host_attributes, shost);
   }
   return ret;
}

/*
 *----------------------------------------------------------------------
 *
 * xsan_setup_transport_attrs  -- 
 *
 *     Initialize generic SAN transport attributes
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None. 
 * 
 *----------------------------------------------------------------------
 */
int
xsan_setup_transport_attrs( struct Scsi_Host *shost, struct scsi_target *starget)
{
   struct xsan_internal *i = to_xsan_internal(shost->transportt);
   int ret = 0;

   if (i->f->setup_transport_attributes) {
      VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(shost), ret,
         i->f->setup_transport_attributes, shost, starget);
   }
   return ret;
}

/**                                          
 *  spi_attach_transport - Attach pSCSI transport
 *  @ft: Pointer to the spi_function_template
 *                                           
 *  Allocate and initialize all the vmklinux data structures and attach the
 *  passed in pointer to the spi_function_template.
 *
 *  ESX Deviation Notes:                     
 *  This function also does the necessary initialization of data structures for
 *  the vmklinux storage stack
 * 
 *  RETURN VALUE:
 *  non-NULL is a success and is a pointer to the new template
 *  NULL is a failure.
 */                                          
/* _VMKLNX_CODECHECK_: spi_attach_transport */
struct scsi_transport_template *
spi_attach_transport(struct spi_function_template *ft)
{
   struct vmklnx_ScsiModule *vmklnx26ScsiModule;
   struct spi_internal *i = VMKLinux26_Alloc(sizeof(struct spi_internal));

   if (unlikely(!i)) {
      vmk_WarningMessage("%s - Unable to allocate memory for spi_internal\n",
	__FUNCTION__);
      return NULL;
   }

   vmklnx26ScsiModule =
      vmklnx_alloc_scsimod(VMKLNX_SCSI_TRANSPORT_TYPE_PSCSI, i);

   if (!vmklnx26ScsiModule) {
      VMKLinux26_Free(i);
      return NULL;
   }

   i->t.module = (void *)vmklnx26ScsiModule;
   i->t.target_size = sizeof(struct spi_transport_attrs);
   i->t.host_size = sizeof(struct spi_host_attrs);
   i->f = ft;

   return &i->t;
}

/**                                          
 *  spi_release_transport - Releases pSCSI transport       
 *  @t: pointer to scsi_transport_template
 *                                           
 *  Releases pSCSI transport
 *                                           
 *  RETURN VALUE:
 *  None                             
 */                                          
/* _VMKLNX_CODECHECK_: spi_release_transport */
void spi_release_transport(struct scsi_transport_template *t)
{
   struct spi_internal *i = to_spi_internal(t);

   /*
    * Free up the module structure
    */
   VMKLinux26_Free(t->module);

   /*
    * Free up the transport_internal structure
    */
   VMKLinux26_Free(i);
}

/*
 *----------------------------------------------------------------------
 *
 * spi_setup_transport_attrs --
 *
 *     Initialize pSCSI transport attributes
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */
int 
spi_setup_transport_attrs(struct scsi_target *starget)
{
   spi_period(starget) = -1;	/* illegal value */
   spi_min_period(starget) = 0;
   spi_offset(starget) = 0;	/* async */
   spi_max_offset(starget) = 255;
   spi_width(starget) = 0;	/* narrow */
   spi_max_width(starget) = 1;
   spi_iu(starget) = 0;	/* no IU */
   spi_dt(starget) = 0;	/* ST */
   spi_qas(starget) = 0;
   spi_wr_flow(starget) = 0;
   spi_rd_strm(starget) = 0;
   spi_rti(starget) = 0;
   spi_pcomp_en(starget) = 0;
   spi_hold_mcs(starget) = 0;
   spi_dv_pending(starget) = 0;
   spi_dv_in_progress(starget) = 0;
   spi_initial_dv(starget) = 0;
   mutex_init(&spi_dv_mutex(starget));
   spi_attr_initialized(starget) = 1;

   return 0;
}


/**	spi_dv_device - Do Domain Validation on the device
 *	@sdev:		scsi device to validate
 *
 *	Performs the domain validation on the given device in the
 *	current execution thread.  Since DV operations may sleep,
 *	the current thread must have user context.  Also no SCSI
 *	related locks that would deadlock I/O issued by the DV may
 *	be held.
 */
/* _VMKLNX_CODECHECK_: spi_dv_device */
void
spi_dv_device(struct scsi_device *sdev)
{
   struct scsi_target *stgt = sdev->sdev_target;
   u8 *buffer;
   const int len = SPI_MAX_ECHO_BUFFER_SIZE*2;

   VMK_ASSERT(sdev);

   if (unlikely(spi_dv_in_progress(stgt))) {
      return;
   }

   if (unlikely(scsi_device_get(sdev))) {
      return;
   }

   spi_dv_in_progress(stgt) = 1;

   buffer = VMKLinux26_Alloc(len);

   if (!buffer) {
      spi_dv_in_progress(stgt) = 0;
      scsi_device_put(sdev);
      return;
   }

   /*
    * Drain out all commands on target before we start domain validation
    * Also dont accept commands from the storage stack
    */
   scsi_target_quiesce(stgt);

   spi_dv_pending(stgt) = 1;
   mutex_lock(&spi_dv_mutex(stgt));

   vmk_LogDebug(vmklinux26Log, 0, "Beginning Domain Validation\n");

   spi_dv_device_internal(sdev, buffer);

   vmk_LogDebug(vmklinux26Log, 0, "Ending Domain Validation\n");

   mutex_unlock(&spi_dv_mutex(stgt));

   spi_dv_pending(stgt) = 0;

   scsi_target_resume(stgt);

   spi_initial_dv(stgt) = 1;

   VMKLinux26_Free(buffer);
   spi_dv_in_progress(stgt) = 0;
   scsi_device_put(sdev);
}

/**
 *	scsi_device_quiesce - Block user issued commands.
 *	@sdev:	scsi device to quiesce.
 *
 *	This works by trying to transition to the SDEV_QUIESCE state
 *	(which must be a legal transition).  When the device is in this
 *	state, only special requests will be accepted, all others will
 *	be deferred.  Unlike Linux behavior, requeues are handled 
 *      outside the vmklinux layer rather than as special requests 
 *      within the vmklinux layer.
 *
 *	Returns zero if unsuccessful or an error if not.
 **/
void
vmklnx_scsi_device_quiesce(struct scsi_device *sdev, void *ref)
{
   sdev->sdev_state = SDEV_QUIESCE;

   /*
    * Give some time before all commands are flushed out
    */
   while (sdev->device_busy) {
      /*
       * There is no way to know if all the commands are flushed out
       * This is an arbitary number that seems to work with mptspi
       * and aic79xx driver.  Use caution when modifying this value
       */
      if(!in_interrupt()) {
         msleep_interruptible(1);
      } else {
         mdelay(1);
      }
   }
}

/**
 *	scsi_device_resume - Restart user issued commands to a quiesced device.
 *	@sdev:	scsi device to resume.
 *
 *	Moves the device from quiesced back to running and restarts the
 *	queues.
 *
 *	Must be called with user context, may sleep.
 **/
void
vmklnx_scsi_device_resume(struct scsi_device *sdev, void *ref)
{
   sdev->sdev_state = SDEV_RUNNING;
}

void
scsi_target_quiesce(struct scsi_target *starget)
{
   VMK_ASSERT(starget);

   starget_for_each_device(starget, NULL, vmklnx_scsi_device_quiesce);
}

void
scsi_target_resume(struct scsi_target *starget)
{
   VMK_ASSERT(starget);

   starget_for_each_device(starget, NULL, vmklnx_scsi_device_resume);
}

/**
 * starget_for_each_device  -  helper to walk all devices of a target
 * @starget:    target whose devices we want to iterate over.
 *
 * Using host_lock instead of reference counting
 * This traverses over each devices of @shost.  The devices have
 * a reference that must be released by scsi_host_put when breaking
 * out of the loop. host_lock can not be held on this
 */
/* _VMKLNX_CODECHECK_: starget_for_each_device */
void 
starget_for_each_device(struct scsi_target *stgt, void * data,
                     void (*fn)(struct scsi_device *, void *))
{
   struct Scsi_Host *sh;
   struct scsi_device *sdev;

   VMK_ASSERT(stgt);

   sh = dev_to_shost(stgt->dev.parent);
   VMK_ASSERT(sh);

   shost_for_each_device(sdev, sh) {
      if ((sdev->channel == stgt->channel) &&
          (sdev->id == stgt->id))
           fn(sdev, data);
   }
}

/*
 *----------------------------------------------------------------------
 *
 * spi_dv_device_internal --
 *
 *     Process DV for pSCSI
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
spi_dv_device_internal(struct scsi_device *sdev, u8 *buffer)
{
   struct spi_internal *i = to_spi_internal(sdev->host->transportt);
   struct scsi_target *starget = sdev->sdev_target;
   struct Scsi_Host *shost = sdev->host;
   int len = sdev->inquiry_len;

   VMK_ASSERT(sdev);
   VMK_ASSERT(buffer);

   /* 
    * first set us up for narrow async 
    */
   DV_SET(offset, 0);
   DV_SET(width, 0);
	
   if (spi_dv_device_compare_inquiry(sdev, buffer, buffer, DV_LOOPS)
	    != SPI_COMPARE_SUCCESS) {
      vmk_LogDebug(vmklinux26Log, 0, "Domain Validation Initial Inquiry"
	" Failed for adapter %s, channel %d, id %d\n", 
	sdev->host->hostt->name, sdev->channel, sdev->id );
      return;
   }

   /* test width */
   if (i->f->set_width && spi_max_width(starget) && scsi_device_wide(sdev)) {
      VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), i->f->set_width, starget, 1);

      if (spi_dv_device_compare_inquiry(sdev, buffer, buffer + len,
					DV_LOOPS) != SPI_COMPARE_SUCCESS) {
         vmk_LogDebug(vmklinux26Log, 0, "Wide transfers failed for "
		"adapter %s, channel %d, id %d\n", 
		sdev->host->hostt->name, sdev->channel, sdev->id );
         VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), i->f->set_width, 
		starget, 0);
      }
   }

   if (!i->f->set_period) {
      return;
   }

   /* device can't handle synchronous */
   if (!scsi_device_sync(sdev) && !scsi_device_dt(sdev)) {
      return;
   }

   /* 
    * len == -1 is the signal that we need to ascertain the
    * presence of an echo buffer before trying to use it.  len ==
    * 0 means we don't have an echo buffer 
    */
   len = -1;

   /* now set up to the maximum */
   DV_SET(offset, spi_max_offset(starget));
   DV_SET(period, spi_min_period(starget));

   /* try QAS requests; this should be harmless to set if the
    * target supports it */
   if (scsi_device_qas(sdev)) {
      DV_SET(qas, 1);
   } else {
      DV_SET(qas, 0);
   }

   if (scsi_device_ius(sdev) && spi_min_period(starget) < 9) {
      /* This u320 (or u640). Set IU transfers */
	DV_SET(iu, 1);
	/* Then set the optional parameters */
	DV_SET(rd_strm, 1);
	DV_SET(wr_flow, 1);
	DV_SET(rti, 1);
	if (spi_min_period(starget) == 8)
		DV_SET(pcomp_en, 1);
   } else {
	DV_SET(iu, 0);
   }

   /* 
    * now that we've done all this, actually check the bus
    * signal type (if known).  Some devices are stupid on
    * a SE bus and still claim they can try LVD only settings 
    */
   if (i->f->get_signalling) {
      VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), i->f->get_signalling, shost);
   }

   if (spi_signalling(shost) == SPI_SIGNAL_SE ||
    spi_signalling(shost) == SPI_SIGNAL_HVD ||
    !scsi_device_dt(sdev)) {
       DV_SET(dt, 0);
   } else {
      DV_SET(dt, 1);
   }

   /* Do the read only INQUIRY tests */
   spi_dv_retrain(sdev, buffer, buffer + sdev->inquiry_len,
		       spi_dv_device_compare_inquiry);
   /* See if we actually managed to negotiate and sustain DT */
   if (i->f->get_dt) {
       VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), i->f->get_dt, starget);
   }

   /*
    * Linux does additional tests, like reading and writing tests
    * But these tests dont set any parameters in the driver. So for now
    * behaving as though the device has no echo buffer in place
    */
   return;
}

/*
 *----------------------------------------------------------------------
 *
 * spi_dv_retrain --
 *
 *     Perform various DV 
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */
static enum spi_compare_returns
spi_dv_retrain(struct scsi_device *sdev, u8 *buffer, u8 *ptr,
	       enum spi_compare_returns 
	       (*compare_fn)(struct scsi_device *, u8 *, u8 *, int))
{
   struct spi_internal *i = to_spi_internal(sdev->host->transportt);
   struct scsi_target *starget = sdev->sdev_target;
   int period = 0, prevperiod = 0; 
   enum spi_compare_returns retval;
   struct Scsi_Host *shost = sdev->host;

   for (;;) {
      int newperiod;
      retval = compare_fn(sdev, buffer, ptr, DV_LOOPS);

      if (retval == SPI_COMPARE_SUCCESS
		    || retval == SPI_COMPARE_SKIP_TEST) {
			break;
      }

      /* OK, retrain, fallback */
      if (i->f->get_iu) {
         VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), i->f->get_iu, starget);
      }
      if (i->f->get_qas) {
         VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), i->f->get_qas, starget);
      }
      if (i->f->get_period) {
         VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), i->f->get_period, 
		starget);
      }

      /* 
       * Here's the fallback sequence; first try turning off
       * IU, then QAS (if we can control them), then finally
       * fall down the periods 
       */
      if (i->f->set_iu && spi_iu(starget)) {
         vmk_LogDebug(vmklinux26Log, 0, "Domain Validation disabling"
		" information %s, channel %d, id %d\n", 
		sdev->host->hostt->name, sdev->channel, sdev->id );
	 DV_SET(iu, 0);
      } else if (i->f->set_qas && spi_qas(starget)) {
         vmk_LogDebug(vmklinux26Log, 0, "Domain Validation disabling"
		" quick arbitration selection %s, channel %d, id %d\n", 
		sdev->host->hostt->name, sdev->channel, sdev->id );
	 DV_SET(qas, 0);
      } else {
         newperiod = spi_period(starget);
	 period = newperiod > period ? newperiod : period;
	 if (period < 0x0d) {
	    period++;
         } else {
	    period += period >> 1;
 	 }

	 if (unlikely(period > 0xff || period == prevperiod)) {
	    /* Total failure; set to async and return */
            vmk_LogDebug(vmklinux26Log, 0, "Domain Validation failure, "
		"dropping back to async %s, channel %d, id %d\n", 
		sdev->host->hostt->name, sdev->channel, sdev->id );
	    DV_SET(offset, 0);
	    return SPI_COMPARE_FAILURE;
	 }
         vmk_LogDebug(vmklinux26Log, 0, "Domain Validation failure,"
		" dropping back for %s, channel %d, id %d\n", 
		sdev->host->hostt->name, sdev->channel, sdev->id );
	 DV_SET(period, period);
	 prevperiod = period;
      }
   }
   return retval;
}

/*
 *----------------------------------------------------------------------
 *
 * spi_dv_device_compare_inquiry --
 *
 *    This is for the simplest form of Domain Validation: a read test
 * on the inquiry data from the device 
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */
static enum spi_compare_returns
spi_dv_device_compare_inquiry(struct scsi_device *sdev, u8 *buffer,
			      u8 *ptr, const int retries)
{
   int r, result, readlen = VMK_SECTOR_SIZE; /* Required for mptscsi */ 
   const int len = sdev->inquiry_len;
   const char spi_inquiry[] = {
	INQUIRY, 0, 0, 0, len, 0
   };

   VMK_ASSERT(sdev);
   VMK_ASSERT(retries != 0);

   if (readlen < len) {
      readlen = len;
   }

   for (r = 0; r < retries; r++) {
      memset(ptr, 0, readlen);

      result = spi_execute(sdev, spi_inquiry, DMA_FROM_DEVICE,
				     ptr, readlen, NULL); 
		
      if(result || !scsi_device_online(sdev)) {
         return SPI_COMPARE_FAILURE;
      }

      /* If we don't have the inquiry data already, the
       * first read gets it */
      if (ptr == buffer) {
         ptr += readlen;
	 --r;
	 continue;
      }

      if (memcmp(buffer, ptr, len) != 0) {
         /* failure */
         return SPI_COMPARE_FAILURE;
      }
   }
   return SPI_COMPARE_SUCCESS;
}

/*
 *----------------------------------------------------------------------
 *
 * spi_execute --
 *
 *     Send down Commands for DV
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */
static int 
spi_execute(struct scsi_device *sdev, const void *cmd,
		       enum dma_data_direction dir,
		       void *buffer, unsigned bufflen,
		       struct scsi_sense_hdr *sshdr)
{
   int i, result;
   unsigned char sense[SCSI_SENSE_BUFFERSIZE];

   VMK_ASSERT(sdev);

   for(i = 0; i < DV_RETRIES; i++) {
      result = scsi_execute(sdev, cmd, dir, buffer, bufflen,
		      sense, DV_TIMEOUT, /* retries */ 1,
		      REQ_FAILFAST);
      if (result & DRIVER_SENSE) {
         struct scsi_sense_hdr sshdr_tmp;
	 if (!sshdr) {
	    sshdr = &sshdr_tmp;
         }

         if (scsi_normalize_sense(sense, SCSI_SENSE_BUFFERSIZE, sshdr)
			    && sshdr->sense_key == UNIT_ATTENTION) {
	    continue;
	 }
      }
      break;
   }
   return result;
}

/*
 *----------------------------------------------------------------------
 *
 * sprint_frac --
 *
 *     Print the fraction details
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */
static int 
sprint_frac(char *dest, int value, int denom)
{
   int frac = value % denom;
   int result = sprintf(dest, "%d", value / denom);

   if (frac == 0) {
      return result;
   }
   dest[result++] = '.';

   do {
      denom /= 10;
      sprintf(dest + result, "%d", frac / denom);
      result++;
      frac %= denom;
   } while (frac);

   dest[result++] = '\0';
   return result;
}

/**                                          
 * spi_display_xfer_agreement - Prints transfer details       
 * @starget: pointer to scsi_target
 *                                           
 * Each SPI port is required to maintain a transfer agreement for each
 * other port on the bus.  This function prints a one-line summary of
 * the current agreement; 
 *                                           
 * RETURN VALUE:
 * None                                           
 */                                          
/* _VMKLNX_CODECHECK_: spi_display_xfer_agreement */
void 
spi_display_xfer_agreement(struct scsi_target *starget)
{
   struct spi_transport_attrs *tp;

   VMK_ASSERT(starget);
   tp = (struct spi_transport_attrs *)&starget->starget_data;
   VMK_ASSERT(tp);

   if (tp->offset > 0 && tp->period > 0) {
      unsigned int picosec, kb100;
      char *scsi = "FAST-?";
      char tmp[8];

      if (tp->period <= SPI_STATIC_PPR) {
         picosec = ppr_to_ps[tp->period];
	 switch (tp->period) {
	    case  7: scsi = "FAST-320"; break;
	    case  8: scsi = "FAST-160"; break;
	    case  9: scsi = "FAST-80"; break;
	    case 10:
	    case 11: scsi = "FAST-40"; break;
	    case 12: scsi = "FAST-20"; break;
	 }
      } else {
         picosec = tp->period * 4000;
	 if (tp->period < 25) {
	    scsi = "FAST-20";
         } else if (tp->period < 50) {
	    scsi = "FAST-10";
         } else
	    scsi = "FAST-5";
      }

      kb100 = (10000000 + picosec / 2) / picosec;
      if (tp->width) {
         kb100 *= 2;
      }
      sprint_frac(tmp, picosec, 1000);

      vmk_LogDebug(vmklinux26Log, 0,
		 "%s %sSCSI %d.%d MB/s %s%s%s%s%s%s%s%s (%s ns, offset %d)\n",
			 scsi, tp->width ? "WIDE " : "", kb100/10, kb100 % 10,
			 tp->dt ? "DT" : "ST",
			 tp->iu ? " IU" : "",
			 tp->qas  ? " QAS" : "",
			 tp->rd_strm ? " RDSTRM" : "",
			 tp->rti ? " RTI" : "",
			 tp->wr_flow ? " WRFLOW" : "",
			 tp->pcomp_en ? " PCOMP" : "",
			 tp->hold_mcs ? " HMCS" : "",
			 tmp, tp->offset);
   } else {
      vmk_LogDebug(vmklinux26Log, 0, "%sasynchronous\n", 
		tp->width ? "wide " : "");
   }
}

/**
 **********************************************************************
 * \globalfn spi_populate_ppr_msg -- Populate the message fields
 *
 * \param  Pointer to message field
 * \param  Period
 * \param  Offset
 * \param  Width
 * \param  Options
 * \return 8
 * \par Include
 * scsi/scsi_transport_spi.h
 * \par ESX Deviation Notes None
 * \sa None.
 **********************************************************************
 */
int 
spi_populate_ppr_msg(unsigned char *msg, int period, int offset,
		int width, int options)
{
   msg[0] = EXTENDED_MESSAGE;
   msg[1] = 6;
   msg[2] = EXTENDED_PPR;
   msg[3] = period;
   msg[4] = 0;
   msg[5] = offset;
   msg[6] = width;
   msg[7] = options;
   return 8;
}

/**
 **********************************************************************
 * \globalfn spi_populate_width_msg -- Populate the width message fields
 *
 * \param  Pointer to message field
 * \param  Width
 * \return 4
 * \par Include
 * scsi/scsi_transport_spi.h
 * \par ESX Deviation Notes None
 * \sa None.
 **********************************************************************
 */
int 
spi_populate_width_msg(unsigned char *msg, int width)
{
   msg[0] = EXTENDED_MESSAGE;
   msg[1] = 2;
   msg[2] = EXTENDED_WDTR;
   msg[3] = width;
   return 4;
}

/**
 **********************************************************************
 * \globalfn spi_populate_sync_msg -- Populate the sync message fields
 *
 * \param  Pointer to message field
 * \param  Period
 * \param  Offset
 * \return 5
 * \par Include
 * scsi/scsi_transport_spi.h
 * \par ESX Deviation Notes None
 * \sa None.
 **********************************************************************
 */
int 
spi_populate_sync_msg(unsigned char *msg, int period, int offset)
{
   msg[0] = EXTENDED_MESSAGE;
   msg[1] = 3;
   msg[2] = EXTENDED_SDTR;
   msg[3] = period;
   msg[4] = offset;
   return 5;
}

/**                                          
 *  fc_attach_transport - attaches an FC transport
 *  @ft: functions used to communicate with driver
 *                                           
 *  Registers an FC transport with vmklinux
 *                                           
 *  RETURN VALUES:
 *  A populated scsi_transport_template upon success,
 *  NULL if memory could not be allocated to register the transport
 *                                           
 *  SEE ALSO:
 *  fc_release_transport                                           
 */                                          
/* _VMKLNX_CODECHECK_: fc_attach_transport */
struct scsi_transport_template *
fc_attach_transport(struct fc_function_template *ft)
{
   struct vmklnx_ScsiModule *vmklnx26ScsiModule;
   struct fc_internal *i = VMKLinux26_Alloc(sizeof(struct fc_internal));

   VMK_ASSERT(ft);
   VMK_ASSERT(i);

   if (unlikely(!i)) {
      vmk_WarningMessage("%s - Unable to allocate memory for fc_internal\n",
	__FUNCTION__);
      return NULL;
   }

   vmklnx26ScsiModule = vmklnx_alloc_scsimod(VMKLNX_SCSI_TRANSPORT_TYPE_FC, i);

   if (!vmklnx26ScsiModule) {
      VMKLinux26_Free(i);
      return NULL;
   }

   /* 
    * FC Transport uses the shost workq for scsi scanning 
    * rport->scan_work = consumer
    */
   i->t.create_work_queue = 1;
	
   i->t.module = (void *)vmklnx26ScsiModule;
   i->t.host_size = sizeof(struct fc_host_attrs);
   i->t.target_size = sizeof(struct fc_starget_attrs);

   i->f = ft;

   return &i->t;
}

/**                                          
 *  fc_release_transport - releases an FC transport
 *  @t: scsi_transport_template as returned by fc_attach_transport
 *                                           
 *  Releases an FC transport previously registered with fc_attach_transport
 *                                           
 *  SEE ALSO:
 *  fc_attach_transport                                           
 *
 *  RETURN VALUE:
 *  This function does not return a value
 */                                          
/* _VMKLNX_CODECHECK_: fc_release_transport */
void fc_release_transport(struct scsi_transport_template *t)
{
   struct fc_internal *i;
   VMK_ASSERT(t);

   i = to_fc_internal(t);
   VMK_ASSERT(i);

   /*
    * Free up the module structure
    */
   VMKLinux26_Free(t->module);

   /*
    * Free up the transport_internal structure
    */
   VMKLinux26_Free(i);
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_fc_host_setup
 *
 *      Slightly deviated version of fc_host_setup. Exports information
 * required by vmk_FcAdapter 
 *
 * Results:
 *      0 on Success
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
int 
vmklnx_fc_host_setup(struct Scsi_Host *shost)
{
   struct fc_host_attrs *fc_host;

   VMK_ASSERT(shost);

   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);

   /*
    * Create Mgmt Adapter Instance to our management
    */
   if (FcLinuxAttachMgmtAdapter(shost)) {
      vmk_WarningMessage("Failed to attach FC attributes to VMKernel\n");
      return -ENOMEM;
   }

   /* 
    * Set default values easily detected by the midlayer as
    * failure cases.  The scsi lldd is responsible for initializing
    * all transport attributes to valid values per host.
    */
   fc_host->node_name = -1;
   fc_host->port_name = -1;
   fc_host->permanent_port_name = -1;
   fc_host->supported_classes = FC_COS_UNSPECIFIED;
   memset(fc_host->supported_fc4s, 0, sizeof(fc_host->supported_fc4s));
   fc_host->supported_speeds = FC_PORTSPEED_UNKNOWN;
   fc_host->maxframe_size = -1;
   fc_host->max_npiv_vports = 0;
   memset(fc_host->serial_number, 0, sizeof(fc_host->serial_number));

   fc_host->port_id = -1;
   fc_host->port_type = FC_PORTTYPE_UNKNOWN;
   fc_host->port_state = FC_PORTSTATE_UNKNOWN;
   memset(fc_host->active_fc4s, 0, sizeof(fc_host->active_fc4s));
   fc_host->speed = FC_PORTSPEED_UNKNOWN;
   fc_host->fabric_name = -1;
   memset(fc_host->symbolic_name, 0, sizeof(fc_host->symbolic_name));
   memset(fc_host->system_hostname, 0, sizeof(fc_host->system_hostname));

   fc_host->tgtid_bind_type = FC_TGTID_BIND_BY_WWPN;

   INIT_LIST_HEAD(&fc_host->rports);
   INIT_LIST_HEAD(&fc_host->rport_bindings);
   INIT_LIST_HEAD(&fc_host->vports);
   fc_host->next_rport_number = 0;
   fc_host->next_target_id = 0;
   fc_host->next_vport_number = 0;
   fc_host->npiv_vports_inuse = 0;

   /*
    * Create a work queue to handle FC requests
    * Refer fc_queue_work to handle below consumers
    *	rport_delete_work
    *	stgt_delete_work
    */
   snprintf(fc_host->work_q_name, FC_MAX_WORK_QUEUE_NAME, "fc_wq_%d",
	shost->host_no);
   fc_host->work_q = create_singlethread_workqueue(fc_host->work_q_name);
   if (!fc_host->work_q) {
      vmk_WarningMessage("Error: Could not create FC WQ\n");
      FcLinuxReleaseMgmtAdapter(shost);
      return -ENOMEM;
   }

   /*
    * Create a work queue to handle FC devloss requests
    * Refer fc_queue_devloss_work for below consumers
    *		dev_loss_work, 
    * 		fail_io_work
    */
   snprintf(fc_host->devloss_work_q_name, FC_MAX_WORK_QUEUE_NAME, "fc_dl_%d",
	shost->host_no);
   fc_host->devloss_work_q = create_singlethread_workqueue(
				fc_host->devloss_work_q_name);
   if (!fc_host->devloss_work_q) {
      vmk_WarningMessage("Error: Could not create FC Devloss WQ\n");
      FcLinuxReleaseMgmtAdapter(shost);
      destroy_workqueue(fc_host->work_q);
      fc_host->work_q = NULL;
      return -ENOMEM;
   }

   /*
    * This flag is used to catch cases where
    * some drivers tend to do rport operation
    * after fc_host is destroyed
    */
   atomic_set(&fc_host->vmklnx_flag, VMKLNX_FC_HOST_READY);
   return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_fc_host_free
 *
 *      Free's up resources allocated to the FC host
 *
 * Returns:
 *      0 on Success
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
void
vmklnx_fc_host_free(struct Scsi_Host *shost)
{
   struct fc_host_attrs *fc_host;
   struct workqueue_struct *work_q;

   VMK_ASSERT(shost);

   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);

   /*
    * Free up the WQ
    */
   if (fc_host->work_q) {
      work_q = fc_host->work_q;
      fc_host->work_q = NULL;
      destroy_workqueue(work_q);
   }

   if (fc_host->devloss_work_q) {
      work_q = fc_host->devloss_work_q;
      fc_host->devloss_work_q = NULL;
      destroy_workqueue(work_q);
   }

   return;
}
/**                                          
 *  fc_remove_host - Called to terminate any fc_transport related elements
 *	             for a scsi host
 *  @shost: Pointer to struct Scsi_Host
 *                                           
 *    This routine is expected to be called immediately preceeding the
 *    call from the driver to scsi_remove_host()
 *
 *  ESX Deviation Notes:
 *  Removes vports along with rports
 *                                           
 *  RETURN VALUE:
 *  None
 */                                          
/* _VMKLNX_CODECHECK_: fc_remove_host */
void
fc_remove_host(struct Scsi_Host *shost)
{
   struct fc_vport *vport, *next_vport;
   struct fc_rport *rport, *next_rport;
   struct fc_host_attrs *fc_host;
   unsigned long flags;

   VMK_ASSERT(shost);

   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);

   vmk_LogDebug(vmklinux26Log, 0, "%s - for %s\n", 
	__FUNCTION__, shost->hostt->name);

   atomic_set(&fc_host->vmklnx_flag, VMKLNX_FC_HOST_REMOVING);

   spin_lock_irqsave(shost->host_lock, flags);
   /* Remove any vports */
   list_for_each_entry_safe(vport, next_vport,
         &fc_host->vports, peers) {
         fc_queue_work(shost, &vport->vport_delete_work);
   }

   /* Remove any remote ports */
   list_for_each_entry_safe(rport, next_rport,
	&fc_host->rports, peers) {
      list_del(&rport->peers);
      rport->port_state = FC_PORTSTATE_DELETED;
      fc_queue_work(shost, &rport->rport_delete_work);
   }

   list_for_each_entry_safe(rport, next_rport,
	&fc_host->rport_bindings, peers) {
      list_del(&rport->peers);
      rport->port_state = FC_PORTSTATE_DELETED;
      fc_queue_work(shost, &rport->rport_delete_work);
   }
   spin_unlock_irqrestore(shost->host_lock, flags);

   /*
    * Flush all work items
    */
   fc_flush_devloss(shost);
   fc_flush_work(shost);

   /* flush all scan work items */
   scsi_flush_work(shost);

   /*
    * The WQ associated with this request will be removed during the
    * scsi_host_dev_release call
    */

   return;
}

/*
 *----------------------------------------------------------------------
 *
 * fc_rport_create ---
 *    allocates and creates a remote FC port.
 *
 * Results:
 *    Pointer to new rport that is created
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */
struct fc_rport *
fc_rport_create(struct Scsi_Host *shost, int channel,
	struct fc_rport_identifiers  *ids)
{
   struct fc_internal *fci;
   struct fc_host_attrs *fc_host;
   struct fc_rport *rport;
   struct device *dev;
   unsigned long flags;
   size_t size;
   int error;

   VMK_ASSERT(shost);
   VMK_ASSERT(shost->transportt);
   VMK_ASSERT(ids);

   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);

   fci = to_fc_internal(shost->transportt);

   size = (sizeof(struct fc_rport) + fci->f->dd_fcrport_size);
   rport = (struct fc_rport *) VMKLinux26_Alloc(size);
   if (unlikely(!rport)) {
      vmk_WarningMessage("%s: allocation failure\n", __FUNCTION__);
      return NULL;
   }
   vmk_LogDebug(vmklinux26Log, 2, "%s - Start \n", __FUNCTION__);

   rport->maxframe_size = -1;
   rport->supported_classes = FC_COS_UNSPECIFIED;
   rport->dev_loss_tmo = fc_dev_loss_tmo;
   memcpy(&rport->node_name, &ids->node_name, sizeof(rport->node_name));
   memcpy(&rport->port_name, &ids->port_name, sizeof(rport->port_name));
   rport->port_id = ids->port_id;
   rport->roles = ids->roles;
   rport->port_state = FC_PORTSTATE_ONLINE;
   if (fci->f->dd_fcrport_size) {
      rport->dd_data = &rport[1];
   }
   rport->channel = channel;
   rport->fast_io_fail_tmo = -1;

   INIT_WORK(&rport->dev_loss_work, fc_timeout_deleted_rport, rport);
   INIT_WORK(&rport->fail_io_work, fc_timeout_fail_rport_io, rport);
   INIT_WORK(&rport->scan_work, fc_scsi_scan_rport, rport);
   INIT_WORK(&rport->stgt_delete_work, fc_starget_delete, rport);
   INIT_WORK(&rport->rport_delete_work, fc_rport_final_delete, rport);

   spin_lock_irqsave(shost->host_lock, flags);

   rport->number = fc_host->next_rport_number++;

   if (rport->roles & FC_RPORT_ROLE_FCP_TARGET) {
      rport->scsi_target_id = fc_host->next_target_id++;
   } else {
      rport->scsi_target_id = -1;
   }

   /*
    * Add the rport to the list
    */
   list_add_tail(&rport->peers, &fc_host->rports);
   get_device(&shost->shost_gendev);       /* for fc_host->rport list */
   spin_unlock_irqrestore(shost->host_lock, flags);

   /*
    * Fill in references for the parents. This is used by scsi_alloc_target
    */
   dev = &rport->dev;
   device_initialize(dev);
   dev->parent = get_device(&shost->shost_gendev); /* parent reference */
   dev->dev_type = FC_RPORT_TYPE;
   dev->release = fc_rport_dev_release;
   sprintf(dev->bus_id, "rport-%d:%d-%d",
		shost->host_no, channel, rport->number);
   error = device_add(dev);
   if (error) {
      vmk_WarningMessage("FC Remote Port device_add failed\n");
      spin_lock_irqsave(shost->host_lock, flags);
      list_del(&rport->peers);
      put_device(&shost->shost_gendev);       /* for fc_host->rport list */
      spin_unlock_irqrestore(shost->host_lock, flags);
      put_device(dev->parent);
      kfree(rport);
      rport = NULL;
      goto exit_rport_create;
   }

   if (rport->roles & FC_RPORT_ROLE_FCP_TARGET) {
      /* initiate a scan of the target */
      rport->flags |= FC_RPORT_SCAN_PENDING;
      vmk_AtomicInc64(&shost->pendingScanWorkQueueEntries);
      scsi_queue_work(shost, &rport->scan_work);
   }

exit_rport_create:
   vmk_LogDebug(vmklinux26Log, 2, "%s - End \n", __FUNCTION__);
   return rport;
}

/**
 *      fc_remote_port_add - notifies the fc transport of the existence
 *	                     of a remote FC port
 *      @shost: scsi host the remote port is connected to
 *      @channel: Channel on shost port connected to
 *      @ids: The world wide names, fc address, and FC4 port
 *	      roles for the remote port
 *
 *      The LLDD calls this routine to notify the transport of the existence
 *      of a remote port. The LLDD provides the unique identifiers (wwpn,wwn)
 *      of the port, its FC address (port_id), and the FC4 roles that are
 *      active for the port.
 *
 *      For ports that are FCP targets (aka scsi targets), the FC transport
 *      maintains consistent target id bindings on behalf of the LLDD.
 *      A consistent target id binding is an assignment of a target id to
 *      a remote port identifier, which persists while the scsi host is
 *      attached. The remote port can disappear, then later reappear, and
 *      its target id assignment remains the same. This allows for shifts
 *      in FC addressing (if binding by wwpn or wwnn) with no apparent
 *      changes to the scsi subsystem which is based on scsi host number and
 *      target id values.  Bindings are only valid during the attachment of
 *      the scsi host. If the host detaches, then later reattaches, target
 *      id bindings may change. Whenever a remote port is allocated, a new 
 *      fc_remote_port class device is created. This routine should not be 
 *      called from interrupt context and assumes no locks are held on entry.
 *      The routine will search the list of remote ports it maintains
 *      internally on behalf of consistent target id mappings. If found, the
 *      remote port structure will be reused. Otherwise, a new remote port
 *      structure will be allocated.
 *	
 *	RETURN VALUE:
 *      Returns a remote port structure
 *
 **/
/* _VMKLNX_CODECHECK_: fc_remote_port_add */
struct fc_rport *
fc_remote_port_add(struct Scsi_Host *shost, int channel,
	struct fc_rport_identifiers  *ids)
{
   struct fc_internal *fci;
   struct fc_rport *rport;
   unsigned long flags;
   int match = 0;
   struct fc_host_attrs *fc_host;

   VMK_ASSERT(shost);
   VMK_ASSERT(shost->transportt);
   VMK_ASSERT(ids);

   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);

   if (atomic_read(&fc_host->vmklnx_flag) == VMKLNX_FC_HOST_REMOVING) {
      vmk_WarningMessage("ERROR: FC host '%s' is being removed"
	"can not add rports now\n", shost->hostt->name);
      return NULL;
   }

   fci = to_fc_internal(shost->transportt);

   /* ensure any stgt delete functions are done */
   fc_flush_work(shost);

   /*
    * Search the list of "active" rports, for an rport that has been
    * deleted, but we've held off the real delete while the target
    * is in a "blocked" state.
    */
   spin_lock_irqsave(shost->host_lock, flags);

   list_for_each_entry(rport, &fc_host->rports, peers) {
      if ((rport->port_state == FC_PORTSTATE_BLOCKED) &&
	(rport->channel == channel)) {

         switch (fc_host->tgtid_bind_type) {
	    case FC_TGTID_BIND_BY_WWPN:
	    case FC_TGTID_BIND_NONE:
	       if (rport->port_name == ids->port_name) {
		  match = 1;
               }
	       break;
	    case FC_TGTID_BIND_BY_WWNN:
	       if (rport->node_name == ids->node_name) {
		  match = 1;
               }
	       break;
	    case FC_TGTID_BIND_BY_ID:
	       if (rport->port_id == ids->port_id) {
	          match = 1;
               }
	       break;
	 }

	 if (match) {
	    struct work_struct *work = &rport->dev_loss_work;

	    memcpy(&rport->node_name, &ids->node_name,
					sizeof(rport->node_name));
	    memcpy(&rport->port_name, &ids->port_name,
					sizeof(rport->port_name));
	    rport->port_id = ids->port_id;

	    rport->port_state = FC_PORTSTATE_ONLINE;
	    rport->roles = ids->roles;

	    spin_unlock_irqrestore(shost->host_lock, flags);

	    if (fci->f->dd_fcrport_size) {
		memset(rport->dd_data, 0, fci->f->dd_fcrport_size);
            }

            vmk_LogDebug(vmklinux26Log, 2, "%s - Active Port. Target Id %s:%d\n",
	    	__FUNCTION__, vmklnx_get_vmhba_name(shost), 
		rport->scsi_target_id);

	    /*
	     * If we were blocked, we were a target.
	     * If no longer a target, we leave the timer
	     * running in case the port changes roles
	     * prior to the timer expiring. If the timer
	     * fires, the target will be torn down.
	     */
	    if (!(ids->roles & FC_RPORT_ROLE_FCP_TARGET)) {
	       return rport;
            }

	    /* restart the target */

	    /*
	     * Stop the target timers first. Take no action
	     * on the del_timer failure as the state
	     * machine state change will validate the
	     * transaction.
	     */
	    if (!cancel_delayed_work(&rport->fail_io_work)) {
	       fc_flush_devloss(shost);
            }
	    if (!cancel_delayed_work(work)) {
	       fc_flush_devloss(shost);
            }

	    spin_lock_irqsave(shost->host_lock, flags);
	    rport->flags &= ~FC_RPORT_DEVLOSS_PENDING;

	    /* initiate a scan of the target */
	    rport->flags |= FC_RPORT_SCAN_PENDING;
	    vmk_AtomicInc64(&shost->pendingScanWorkQueueEntries);
	    scsi_queue_work(shost, &rport->scan_work);
	    spin_unlock_irqrestore(shost->host_lock, flags);
	    scsi_target_unblock(&rport->dev);
	    return rport;
	 }
      }
   }

   /* Search the bindings array */
   if (fc_host->tgtid_bind_type != FC_TGTID_BIND_NONE) {

      /* search for a matching consistent binding */
      list_for_each_entry(rport, &fc_host->rport_bindings, peers) {
      if (rport->channel != channel) {
         continue;
      }
      vmk_LogDebug(vmklinux26Log, 2, "%s - In passive Queue. Target Id %s:%d\n",
	    	__FUNCTION__, vmklnx_get_vmhba_name(shost), 
	 	rport->scsi_target_id);

      switch (fc_host->tgtid_bind_type) {
         case FC_TGTID_BIND_BY_WWPN:
            if (rport->port_name == ids->port_name) {
	        match = 1;
            }
	    break;
	 case FC_TGTID_BIND_BY_WWNN:
	    if (rport->node_name == ids->node_name) {
	       match = 1;
            }
	    break;
	 case FC_TGTID_BIND_BY_ID:
	    if (rport->port_id == ids->port_id) {
	       match = 1;
            }
	    break;
	 case FC_TGTID_BIND_NONE: /* to keep compiler happy */
	    break;
         }

         if (match) {
            list_move_tail(&rport->peers, &fc_host->rports);
            break;
         }
      }

      if (match) {
         memcpy(&rport->node_name, &ids->node_name, sizeof(rport->node_name));
	 memcpy(&rport->port_name, &ids->port_name, sizeof(rport->port_name));
	 rport->port_id = ids->port_id;
	 rport->roles = ids->roles;
	 rport->port_state = FC_PORTSTATE_ONLINE;

	 if (fci->f->dd_fcrport_size) {
	     memset(rport->dd_data, 0, fci->f->dd_fcrport_size);
         }

	 if (rport->roles & FC_RPORT_ROLE_FCP_TARGET) {
	    /* initiate a scan of the target */
	    rport->flags |= FC_RPORT_SCAN_PENDING;
	    vmk_AtomicInc64(&shost->pendingScanWorkQueueEntries);
	    scsi_queue_work(shost, &rport->scan_work);
	    spin_unlock_irqrestore(shost->host_lock, flags);
            scsi_target_unblock(&rport->dev);
	 } else {
	    spin_unlock_irqrestore(shost->host_lock, flags);
         }
         vmk_LogDebug(vmklinux26Log, 2, "%s - Passive Port \n", __FUNCTION__);
	 return rport;
      }
   }

   spin_unlock_irqrestore(shost->host_lock, flags);

   /* No consistent binding found - create new remote port entry */
   rport = fc_rport_create(shost, channel, ids);
   
   vmk_ScsiAdapterEvent(((struct vmklnx_ScsiAdapter *)shost->adapter)->vmkAdapter,
         VMK_SCSI_ADAPTER_EVENT_FC_NEW_TARGET);

   return rport;
}


/**
 *      fc_remote_port_delete - notifies the fc transport that a remote
 *		                port is no longer in existence
 *      @rport: The remote port that no longer exists
 *
 *      The LLDD calls this routine to notify the transport that a remote
 *      port is no longer part of the topology. Although a port
 *      may no longer be part of the topology, it may persist in the remote
 *      ports displayed by the fc_host. This is done under 2 conditions. First,
 *      if the port was a scsi target, we delay its deletion by "blocking" it.
 *      This allows the port to temporarily disappear, then reappear without
 *      disrupting the SCSI device tree attached to it. During the "blocked"
 *      period the port will still exist.
 *      Second, if the port was a scsi target and disappears for longer than we
 *      expect, we'll delete the port and the tear down the SCSI device tree
 *      attached to it. However, we want to semi-persist the target id assigned
 *      to that port if it eventually does exist. The port structure will
 *      remain (although with minimal information) so that the target id
 *      bindings remails.
 *
 *      If the remote port is not an FCP Target, it will be fully torn down
 *      and deallocated, including the fc_remote_port class device.
 *
 *      If the remote port is an FCP Target, the port will be placed in a
 *      temporary blocked state. From the LLDD's perspective, the rport no
 *      longer exists. From the SCSI midlayer's perspective, the SCSI target
 *      exists, but all sdevs on it are blocked from further I/O. We can then 
 *      expect the following two conditions. First, if the remote port does not 
 *      return (signaled by a LLDD call to fc_remote_port_add()) 
 *      within the dev_loss_tmo timeout, then the scsi target is removed,
 *      thereby killing all outstanding i/o and removing the
 *      scsi devices attached ot it. The port structure will be marked Not
 *      Present and be partially cleared, leaving only enough information to
 *      recognize the remote port relative to the scsi target id binding if
 *      it later appears.  The port will remain as long as there is a valid
 *      binding (e.g. until the user changes the binding type or unloads the
 *      scsi host with the binding).
 *
 *      Second, if the remote port returns within the dev_loss_tmo value 
 *      (and matches according to the target id binding type), 
 *      the port structure will be reused. If it is no longer a SCSI target, 
 *      the target will be torn down. If it continues to be a SCSI target, 
 *      then the target will be unblocked (allowing i/o to be resumed), 
 *      and a scan will be activated to ensure that all luns are detected.
 *
 *      This function cannot be called from interrupt context and assumes no 
 *      locks are held on entry.
 *
 *      ESX Deviation Notes:
 *      The link timeout can be set by the driver in
 *      vmkAdapter->mgmtAdapter.t.fc->linkTimeout.
 *      The lesser of fast IO fail timeout of the rport and the 
 *      link timeout is selected as the time period to wait before 
 *      the rport is freed.
 *
 *	RETURN VALUE:
 *	None
 *
 **/
/* _VMKLNX_CODECHECK_: fc_remote_port_delete */
void
fc_remote_port_delete(struct fc_rport  *rport)
{
   struct Scsi_Host *shost;
   struct fc_internal *i;
   int timeout;
   unsigned long flags;
   struct vmklnx_ScsiAdapter *vmklnx26ScsiAdapter;
   vmk_ScsiAdapter *vmkAdapter;
   struct fc_host_attrs *fc_host;

   VMK_ASSERT(rport);

   shost = rport_to_shost(rport);
   VMK_ASSERT(shost);
   VMK_ASSERT(shost->transportt);

   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);

   if (atomic_read(&fc_host->vmklnx_flag) == VMKLNX_FC_HOST_REMOVING) {
      vmk_WarningMessage("ERROR: FC host '%s' is being removed"
	"rports will be deleted as part of host removal\n", shost->hostt->name);
      return;
   }

   i = to_fc_internal(shost->transportt);
   VMK_ASSERT(i);

   vmklnx26ScsiAdapter = (struct vmklnx_ScsiAdapter *) shost->adapter;
   VMK_ASSERT(vmklnx26ScsiAdapter);

   vmkAdapter = vmklnx26ScsiAdapter->vmkAdapter;
   VMK_ASSERT(vmkAdapter);

   VMK_ASSERT(vmkAdapter->mgmtAdapter.transport == VMK_STORAGE_ADAPTER_FC);

   vmk_LogDebug(vmklinux26Log, 2, "%s ----\n", __FUNCTION__);

   /*
    * No need to flush the fc_host work_q's, as all adds are synchronous.
    *
    * We do need to reclaim the rport scan work element, so eventually
    * (in fc_rport_final_delete()) we'll flush the scsi host work_q if
    * there's still a scan pending.
    */

   spin_lock_irqsave(shost->host_lock, flags);

   /* If no scsi target id mapping, delete it */
   if (rport->scsi_target_id == -1) {
      list_del(&rport->peers);
      rport->port_state = FC_PORTSTATE_DELETED;
      fc_queue_work(shost, &rport->rport_delete_work);
      spin_unlock_irqrestore(shost->host_lock, flags);
      vmk_LogDebug(vmklinux26Log, 2, "%s - Not a scsi target.. deleting----\n", 
	__FUNCTION__);
      return;
   }

   rport->port_state = FC_PORTSTATE_BLOCKED;
   rport->flags |= FC_RPORT_DEVLOSS_PENDING;
   spin_unlock_irqrestore(shost->host_lock, flags);

   scsi_target_block(&rport->dev);

   /*
    * If the user has set this value, use it. If not,
    * use the value set by vmklinux or the FC driver
    */
   timeout = vmkAdapter->mgmtAdapter.t.fc->linkTimeout ?
        vmkAdapter->mgmtAdapter.t.fc->linkTimeout : rport->dev_loss_tmo;

   /* see if we need to kill io faster than waiting for device loss */
   if ((rport->fast_io_fail_tmo != -1) &&
    (rport->fast_io_fail_tmo < timeout) && (i->f->terminate_rport_io))
	fc_queue_devloss_work(shost, &rport->fail_io_work,
	rport->fast_io_fail_tmo * HZ);

   /* cap the length the devices can be blocked until they are deleted */
   if (fc_queue_devloss_work(shost, &rport->dev_loss_work, timeout * HZ) != 1) {
      vmk_LogDebug(vmklinux26Log, 2, "%s - Failed to queue devloss----\n", 
	__FUNCTION__);
   } else {
      vmk_LogDebug(vmklinux26Log, 2, "%s - Link Timeout = %d, Target Id "
	"%s:%d\n", __FUNCTION__, timeout, vmklnx_get_vmhba_name(shost), 
	rport->scsi_target_id);
   }
   vmk_ScsiAdapterEvent(((struct vmklnx_ScsiAdapter *)shost->adapter)->vmkAdapter,
         VMK_SCSI_ADAPTER_EVENT_FC_REMOVED_TARGET);
}

/**
 * fc_remote_port_rolechg - notifies the fc transport that the roles
 *		on a remote may have changed
 * @rport:	The remote port that changed
 * @roles:	Private (Transport-managed) Attribute
 *
 * The LLDD calls this routine to notify the transport that the roles
 * on a remote port may have changed. The largest effect of this is
 * if a port now becomes a FCP Target, it must be allocated a
 * scsi target id.  If the port is no longer a FCP target, any
 * scsi target id value assigned to it will persist in case the
 * role changes back to include FCP Target. No changes in the scsi
 * midlayer will be invoked if the role changes (in the expectation
 * that the role will be resumed. If it doesn't normal error processing
 * will take place).
 *
 * Should not be called from interrupt context.
 *
 * Notes:
 *	This routine assumes no locks are held on entry
 *
 * RETURN VALUE:
 *	None
 *
 * ESX Deviation Notes:
 * 	If the FC host is being removed, we do not change roles, 
 * 	but return immediately
 **/
/* _VMKLNX_CODECHECK_: fc_remote_port_rolechg */
void
fc_remote_port_rolechg(struct fc_rport  *rport, u32 roles)
{
   struct Scsi_Host *shost;
   struct fc_host_attrs *fc_host;
   unsigned long flags;
   int create = 0;

   VMK_ASSERT(rport);
   shost = rport_to_shost(rport);
   VMK_ASSERT(shost);

   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);

   if (atomic_read(&fc_host->vmklnx_flag) == VMKLNX_FC_HOST_REMOVING) {
      vmk_WarningMessage("ERROR: FC host '%s' is being removed"
	"can not role change now\n", shost->hostt->name);
      return;
   }

   spin_lock_irqsave(shost->host_lock, flags);
   if (roles & FC_RPORT_ROLE_FCP_TARGET) {
      if (rport->scsi_target_id == -1) {
         rport->scsi_target_id = fc_host->next_target_id++;
	 create = 1;
      } else if (!(rport->roles & FC_RPORT_ROLE_FCP_TARGET)) {
	 create = 1;
      }
   }

   rport->roles = roles;
   spin_unlock_irqrestore(shost->host_lock, flags);

   if (create) {
	/*
	 * There may have been a delete timer running on the
	 * port. Ensure that it is cancelled as we now know
	 * the port is an FCP Target.
	 * Note: we know the rport is exists and in an online
	 *  state as the LLDD would not have had an rport
	 *  reference to pass us.
	 *
	 * Take no action on the del_timer failure as the state
	 * machine state change will validate the
	 * transaction.
	 */
      if (!cancel_delayed_work(&rport->fail_io_work)) {
         fc_flush_devloss(shost);
      }
      if (!cancel_delayed_work(&rport->dev_loss_work)) {
         fc_flush_devloss(shost);
      }

      spin_lock_irqsave(shost->host_lock, flags);
      rport->flags &= ~FC_RPORT_DEVLOSS_PENDING;
      spin_unlock_irqrestore(shost->host_lock, flags);

      /* ensure any stgt delete functions are done */
      fc_flush_work(shost);

      /* initiate a scan of the target */
      spin_lock_irqsave(shost->host_lock, flags);
      rport->flags |= FC_RPORT_SCAN_PENDING;
      vmk_AtomicInc64(&shost->pendingScanWorkQueueEntries);
      scsi_queue_work(shost, &rport->scan_work);
      spin_unlock_irqrestore(shost->host_lock, flags);

      scsi_target_unblock(&rport->dev);
   }
}

/*
 *----------------------------------------------------------------------
 *
 * fc_timeout_deleted_rport - Timeout handler for a deleted remote port that
 *                       was a SCSI target (thus was blocked), and failed
 *                       to return in the alloted time.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */
static void
fc_timeout_deleted_rport(void  *data)
{
   struct fc_rport *rport;
   struct Scsi_Host *shost;
   struct fc_host_attrs *fc_host;
   unsigned long flags;

   VMK_ASSERT(data);
   rport = (struct fc_rport *)data;

   shost = rport_to_shost(rport);
   VMK_ASSERT(shost);

   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);

   vmk_LogDebug(vmklinux26Log, 2, "%s ----\n", __FUNCTION__);

   spin_lock_irqsave(shost->host_lock, flags);

   rport->flags &= ~FC_RPORT_DEVLOSS_PENDING;

   /*
    * If the port is ONLINE, then it came back. Validate it's still an
    * FCP target. If not, tear down the scsi_target on it.
    */
   if ((rport->port_state == FC_PORTSTATE_ONLINE) &&
    !(rport->roles & FC_RPORT_ROLE_FCP_TARGET)) {
	dev_printk(KERN_ERR, &rport->dev,
	"blocked FC remote port time out: no longer"
	" a FCP target, removing starget\n");
	spin_unlock_irqrestore(shost->host_lock, flags);
	scsi_target_unblock(&rport->dev);
	fc_queue_work(shost, &rport->stgt_delete_work);
	return;
   }

   if (rport->port_state != FC_PORTSTATE_BLOCKED) {
      spin_unlock_irqrestore(shost->host_lock, flags);
      dev_printk(KERN_ERR, &rport->dev,
		"blocked FC remote port time out: leaving target alone\n");
      return;
   }

   if (fc_host->tgtid_bind_type == FC_TGTID_BIND_NONE) {
      list_del(&rport->peers);
      rport->port_state = FC_PORTSTATE_DELETED;
      dev_printk(KERN_ERR, &rport->dev,
		"blocked FC remote port time out: removing target\n");
      fc_queue_work(shost, &rport->rport_delete_work);
      spin_unlock_irqrestore(shost->host_lock, flags);
      return;
   }

   if (fc_remove_on_dev_loss) {
      dev_printk(KERN_ERR, &rport->dev,
	"blocked FC remote port time out: removing target and "
	"saving binding\n");
   } else {
	dev_printk(KERN_ERR, &rport->dev,
	"blocked FC remote port time out: saving binding\n");
   }

   list_move_tail(&rport->peers, &fc_host->rport_bindings);

   /*
    * Note: We do not remove or clear the hostdata area. This allows
    *   host-specific target data to persist along with the
    *   scsi_target_id. It's up to the host to manage it's hostdata area.
    */

   /*
    * Reinitialize port attributes that may change if the port comes back.
    */
   rport->maxframe_size = -1;
   rport->supported_classes = FC_COS_UNSPECIFIED;
   rport->roles = FC_RPORT_ROLE_UNKNOWN;
   rport->port_state = FC_PORTSTATE_NOTPRESENT;

   /* remove the identifiers that aren't used in the consisting binding */
   switch (fc_host->tgtid_bind_type) {
      case FC_TGTID_BIND_BY_WWPN:
 	 rport->node_name = -1;
	 rport->port_id = -1;
	 break;
      case FC_TGTID_BIND_BY_WWNN:
	 rport->port_name = -1;
	 rport->port_id = -1;
	 break;
      case FC_TGTID_BIND_BY_ID:
	 rport->node_name = -1;
	 rport->port_name = -1;
	 break;
      case FC_TGTID_BIND_NONE:	/* to keep compiler happy */
	 break;
   }

   /*
    * As this only occurs if the remote port (scsi target)
    * went away and didn't come back - we'll remove
    * all attached scsi devices.
    */
   spin_unlock_irqrestore(shost->host_lock, flags);

   scsi_target_unblock(&rport->dev);
   fc_queue_work(shost, &rport->stgt_delete_work);
}

/*
 *----------------------------------------------------------------------
 *
 * fc_timeout_fail_rport_io - Timeout handler for a fast io failing on a
 *                       disconnected SCSI target.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */
static void
fc_timeout_fail_rport_io(void  *data)
{
   struct fc_rport *rport;
   struct Scsi_Host *shost;
   struct fc_internal *i;

   VMK_ASSERT(data);
   rport = (struct fc_rport *)data;

   shost = rport_to_shost(rport);
   VMK_ASSERT(shost);
   VMK_ASSERT(shost->transportt);

   i = to_fc_internal(shost->transportt);
   VMK_ASSERT(i);

   vmk_LogDebug(vmklinux26Log, 2, "%s ----\n", __FUNCTION__);

   if (rport->port_state != FC_PORTSTATE_BLOCKED) {
      return;
   }

   VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), 
	i->f->terminate_rport_io, rport);
}

/*
 *----------------------------------------------------------------------
 *
 * fc_scsi_scan_rport - called to perform a scsi scan on a remote port.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */
static void
fc_scsi_scan_rport(void *data)
{
   struct fc_rport *rport;
   struct Scsi_Host *shost;
   unsigned long flags;

   VMK_ASSERT(data);
   rport = (struct fc_rport *)data;

   shost = rport_to_shost(rport);
   VMK_ASSERT(shost);

   if ((rport->port_state == FC_PORTSTATE_ONLINE) &&
    (rport->roles & FC_RPORT_ROLE_FCP_TARGET)) {
      scsi_scan_target(&rport->dev, rport->channel,
      rport->scsi_target_id, SCAN_WILD_CARD, 1);
   }

   spin_lock_irqsave(shost->host_lock, flags);
   rport->flags &= ~FC_RPORT_SCAN_PENDING;
   vmk_AtomicDec64(&shost->pendingScanWorkQueueEntries);
   spin_unlock_irqrestore(shost->host_lock, flags);
}

/**
 *	scsi_is_fc_rport - Check if the scsi device is fc rport
 *	@dev:	scsi device
 *
 * 	Check if the scsi device is fc rport
 *
 *	 RETURN VALUE:
 *	 TRUE if the device is a fc rport, FALSE otherwise
 *----------------------------------------------------------------------
 */
/* _VMKLNX_CODECHECK_: scsi_is_fc_rport */
int 
scsi_is_fc_rport(const struct device *dev)
{
   return dev->dev_type == FC_RPORT_TYPE;
}

/**                                          
 *  fc_get_event_number - obtain the next sequential FC event number
 *
 *  Returns the next sequential FC event number
 *
 *  RETURN VALUE:
 *  The next sequential FC event number
 */                                          
/* _VMKLNX_CODECHECK_: fc_get_event_number */
u32
fc_get_event_number(void)
{
   return atomic_add_return(1, &fc_event_seq);
}

/**                                          
 *  fc_host_post_vendor_event - non-operational function
 *  @shost: ignored
 *  @event_number: ignored
 *  @data_len: ignored
 *  @data_buf: ignored
 *  @vendor_id: ignored
 *                                           
 *  This function is a non-operational function provided to help reduce
 *  kernel ifdefs.  It is not supported in this release of ESX.
 *                                           
 *  ESX Deviation Notes:                     
 *  This function is a non-operational function provided to help reduce
 *  kernel ifdefs.  It is not supported in this release of ESX.
 *
 *  RETURN VALUE:
 *  This function does not return a value
 */                                          
/* _VMKLNX_CODECHECK_: fc_host_post_vendor_event */
void
fc_host_post_vendor_event(struct Scsi_Host *shost, u32 event_number,
		u32 data_len, char * data_buf, u64 vendor_id)
{
   VMK_ASSERT(shost);

   vmk_LogDebug(vmklinux26Log, 5, "%s - Netlink is not supported in vmklinux "
	"So no notifications were sent\n", __FUNCTION__);

   return;
}

/**                                          
 *  fc_host_post_event - non-operational function
 *  @shost: ignored
 *  @event_number: ignored
 *  @event_code: ignored
 *  @event_data: ignored
 *                                           
 *  This function is not implemented.
 *                                           
 *  This function is a non-operational function provided to help reduce
 *  kernel ifdefs.  It is not supported in this release of ESX.
 *                                           
 *  ESX Deviation Notes:                     
 *  This function is a non-operational function provided to help reduce
 *  kernel ifdefs.  It is not supported in this release of ESX.
 *
 *  RETURN VALUE:
 *  This function does not return a value
 */                                          
/* _VMKLNX_CODECHECK_: fc_host_post_event */
void
fc_host_post_event(struct Scsi_Host *shost, u32 event_number,
		enum fc_host_event_code event_code, u32 event_data)
{
   VMK_ASSERT(shost);

   vmk_LogDebug(vmklinux26Log, 5, "%s - Netlink is not supported in vmklinux "
	"So no notifications were sent up [%x]\n", __FUNCTION__, event_code);

   return;
}


/*
 *----------------------------------------------------------------------
 *
 * fc_queue_work --
 *
 *   Queue  work to the fc_host workqueue.   
 *
 * Results:
 * 	1 - work queued for execution
 *	0 - work is already queued
 *	-EINVAL - work queue doesn't exist
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
static int
fc_queue_work(struct Scsi_Host *shost, struct work_struct *work)
{
   VMK_ASSERT(shost);
   VMK_ASSERT(work);

   if (unlikely(!fc_host_work_q(shost))) {
      vmk_WarningMessage("ERROR: FC host '%s' attempted to queue work, "
	"when no workqueue created.\n", shost->hostt->name);

	return -EINVAL;
   }
   return queue_work(fc_host_work_q(shost), work);
}

/*
 *----------------------------------------------------------------------
 *
 * fc_flush_work --
 *
 *    Flush a fc_host's workqueue.
 *
 * Results:
 *    None
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
static void
fc_flush_work(struct Scsi_Host *shost)
{
   VMK_ASSERT(shost);

   if (!fc_host_work_q(shost)) {
      vmk_WarningMessage("ERROR: FC host '%s' attempted to flush work, "
		"when no workqueue created.\n", shost->hostt->name);
      return;
   }
   flush_workqueue(fc_host_work_q(shost));
}

/*
 *----------------------------------------------------------------------
 *
 * fc_queue_devloss_work --
 *
 *    Schedule work for the fc_host devloss workqueue.
 *
 * Results:
 *    1 on success / 0 already queued / < 0 for error
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */
static int
fc_queue_devloss_work(struct Scsi_Host *shost, struct work_struct *work,
				unsigned long delay)
{
   VMK_ASSERT(shost);
   VMK_ASSERT(work);

   if (unlikely(!fc_host_devloss_work_q(shost))) {
      vmk_WarningMessage("ERROR: FC host '%s' attempted to queue work, "
	 "when no workqueue created.\n", shost->hostt->name);
      return -EINVAL;
   }

   if (delay == 0) {
      return queue_work(fc_host_devloss_work_q(shost), work);
   } else {
      /*
       * Log the delay
       */
      vmk_LogDebug(vmklinux26Log, 2, "Delayed work =  0x%"VMK_FMT64"x",delay); 
   }

   return queue_delayed_work(fc_host_devloss_work_q(shost), work, delay);
}

/*
 *----------------------------------------------------------------------
 *
 * fc_flush_devloss ---
 *
 *   Flush a fc_host's devloss workqueue.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */
static void
fc_flush_devloss(struct Scsi_Host *shost)
{
   VMK_ASSERT(shost);

   if (!fc_host_devloss_work_q(shost)) {
      vmk_WarningMessage("ERROR: FC host '%s' attempted to flush work, "
	 "when no workqueue created.\n", shost->hostt->name);
      return;
   }

   flush_workqueue(fc_host_devloss_work_q(shost));
}


/*
 *----------------------------------------------------------------------
 *
 * fc_starget_delete ---
 *
 *  Called to delete the scsi decendents of an rport (target and all sdevs)
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */
static void
fc_starget_delete(void *data)
{
   struct fc_rport *rport;
   struct Scsi_Host *shost;
   unsigned long flags;
   struct fc_internal *i;
   struct fc_host_attrs *fc_host;

   VMK_ASSERT(data);
   rport = (struct fc_rport *)data;

   shost = rport_to_shost(rport);
   VMK_ASSERT(shost);
   VMK_ASSERT(shost->transportt);

   fc_host = shost_to_fc_host(shost);

   i = to_fc_internal(shost->transportt);
   VMK_ASSERT(i);

   /*
    * Involve the LLDD if possible. All io on the rport is to
    * be terminated, either as part of the dev_loss_tmo callback
    * processing, or via the terminate_rport_io function.
    */
   if (i->f->dev_loss_tmo_callbk) {
      VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), 
	i->f->dev_loss_tmo_callbk, rport);
   } else if (i->f->terminate_rport_io) {
      VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), 
	i->f->terminate_rport_io, rport);
   }

   spin_lock_irqsave(shost->host_lock, flags);
   if (rport->flags & FC_RPORT_DEVLOSS_PENDING) {
      spin_unlock_irqrestore(shost->host_lock, flags);
      if (!cancel_delayed_work(&rport->fail_io_work)) {
         fc_flush_devloss(shost);
      }
      if (!cancel_delayed_work(&rport->dev_loss_work)) {
         fc_flush_devloss(shost);
      }
      spin_lock_irqsave(shost->host_lock, flags);
      rport->flags &= ~FC_RPORT_DEVLOSS_PENDING;
   }
   spin_unlock_irqrestore(shost->host_lock, flags);

   /*
    * Try to remove the target when fc_host is in
    * the process of being released
    */
   if ((fc_remove_on_dev_loss) || 
	(atomic_read(&fc_host->vmklnx_flag) == VMKLNX_FC_HOST_REMOVING)) {
      scsi_remove_target(&rport->dev);
   } else {
      vmk_LogDebug(vmklinux26Log, 2, "%s - Marking the target offline\n", 
	__FUNCTION__);
      scsi_target_offline(&rport->dev);
   }
}


/*
 *----------------------------------------------------------------------
 *
 * fc_rport_final_delete ---
 *    finish rport termination and delete it.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */
static void
fc_rport_final_delete(void *data)
{
   struct fc_rport *rport;
   struct Scsi_Host *shost;
   struct fc_internal *i;
   struct device *dev;

   VMK_ASSERT(data);
   rport = (struct fc_rport *)data;

   shost = rport_to_shost(rport);
   VMK_ASSERT(shost);
   VMK_ASSERT(shost->transportt);

   i = to_fc_internal(shost->transportt);
   VMK_ASSERT(i);

   dev = &rport->dev;
   /*
    * if a scan is pending, flush the SCSI Host work_q so that 
    * that we can reclaim the rport scan work element.
    */
   if (rport->flags & FC_RPORT_SCAN_PENDING) {
      scsi_flush_work(shost);
   }

   /* Delete SCSI target and sdevs */
   if (rport->scsi_target_id != -1) {
      fc_starget_delete(data);
   } else if (i->f->dev_loss_tmo_callbk) {
      VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), i->f->dev_loss_tmo_callbk,
	 rport);
   } else if (i->f->terminate_rport_io) {
      VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost), i->f->terminate_rport_io,
	 rport);
   }

   device_del(dev);
   put_device(&shost->shost_gendev);	/* for fc_host->rport list */
   put_device(dev);			/* for self-reference */
}


/*
 * NPIV support code
 */

/**
 * vmk_fc_vport_create - allocates and creates a FC virtual port.
 * Allocates and creates the vport structure, calls the parent host
 * to instantiate the vport, the completes w/ class and sysfs creation.
 * shost - physical host to create vport on
 * channel - unused
 * pdev - parent device
 * args - virtual port data passed in, wwn, etc.
 *
 * Notes:
 *	This routine assumes no locks are held on entry.
 **/
int
vmk_fc_vport_create(struct Scsi_Host *shost,
      int channel, struct device *pdev,
      void *data)
{
   struct fc_host_attrs *fc_host;
   struct fc_internal *fci;
   struct fc_vport *vport;
   struct device *dev;
   struct vmk_VportData *args;
   unsigned long flags;
   size_t size;
   int error;

   VMK_ASSERT(shost);
   VMK_ASSERT(pdev);
   args = (struct vmk_VportData *) data;
   VMK_ASSERT(args);
   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);
   fci = to_fc_internal(shost->transportt);
   VMK_ASSERT(fci);
   if ( ! fci->f->vport_create) {
      return -ENOENT;
   }

   size = (sizeof(struct fc_vport) + fci->f->dd_fcvport_size);
   vport = (struct fc_vport *)VMKLinux26_Alloc(size);
   if (unlikely(!vport)) {
      vmk_LogDebug(vmklinux26Log, 2, "%s: allocation failure\n", __FUNCTION__);
      return -ENOMEM;
   }

   vport->vport_state = FC_VPORT_UNKNOWN;
   vport->vport_last_state = FC_VPORT_UNKNOWN;
   vport->node_name = wwn_to_u64(args->node_name);
   vport->port_name = wwn_to_u64(args->port_name);
   // we are always an initiator for now
   vport->roles = FC_PORT_ROLE_FCP_INITIATOR;
   vport->vport_type = FC_PORTTYPE_NPIV;
   if (fci->f->dd_fcvport_size) {
      vport->dd_data = &vport[1];
   }
   vport->shost = shost;
   vport->channel = channel;
   vport->flags = FC_VPORT_CREATING;
   INIT_WORK(&vport->vport_delete_work, fc_vport_sched_delete, vport);

   spin_lock_irqsave(shost->host_lock, flags);

   if (fc_host->npiv_vports_inuse >= fc_host->max_npiv_vports) {
      spin_unlock_irqrestore(shost->host_lock, flags);
      kfree(vport);
      return -ENOSPC;
   }
   fc_host->npiv_vports_inuse++;
   vport->number = fc_host->next_vport_number++;
   list_add_tail(&vport->peers, &fc_host->vports);
   get_device(&shost->shost_gendev);	/* for fc_host->vport list */

   spin_unlock_irqrestore(shost->host_lock, flags);

   dev = &vport->dev;
   device_initialize(dev);			/* takes self reference */
   dev->parent = get_device(pdev);		/* takes parent reference */
   dev->dev_type = FC_VPORT_TYPE;
   dev->release = fc_vport_dev_release;
   sprintf(dev->bus_id, "vport-%d:%d-%d",
      shost->host_no, channel, vport->number);

   error = device_add(dev);
   if (error) {
      vmk_LogDebug(vmklinux26Log, 0, "FC Virtual Port device_add failed\n");
      goto delete_vport;
   }

   /*
    * call the driver to do the actual virtual port create
    * this in turn will create a Scsi_Host struct to
    * represent this vport
    */
   VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(shost), error,
         fci->f->vport_create, vport, FALSE);
   if (error) {
      vmk_LogDebug(vmklinux26Log, 0, "FC Virtual Port LLDD Create failed\n");
      goto delete_vport_all;
   }

   spin_lock_irqsave(shost->host_lock, flags);
   vport->flags &= ~FC_VPORT_CREATING;
   spin_unlock_irqrestore(shost->host_lock, flags);

   vmk_LogDebug(vmklinux26Log, 0, 
      "%s created via shost%d channel %d\n", dev->bus_id,
      shost->host_no, channel);

   // need to return the vport shost pointer 
   args->vport_shost = vport->vhost;

   vmk_ScsiAdapterEvent(((struct vmklnx_ScsiAdapter *)shost->adapter)->vmkAdapter,
         VMK_SCSI_ADAPTER_EVENT_FC_NEW_VPORT);
   return 0;

delete_vport_all:
   device_del(dev);
delete_vport:
   spin_lock_irqsave(shost->host_lock, flags);
   list_del(&vport->peers);
   put_device(&shost->shost_gendev);	/* for fc_host->vport list */
   fc_host->npiv_vports_inuse--;
   spin_unlock_irqrestore(shost->host_lock, flags);
   put_device(dev->parent);
   kfree(vport);

   return error;
}

int vmk_fc_vport_delete(struct Scsi_Host *shost)
{
   struct fc_vport *vport;

   VMK_ASSERT(shost);
   if (shost->shost_gendev.parent->dev_type != FC_VPORT_TYPE) {
      return -ENOENT;
   }
   vport = dev_to_vport(shost->shost_gendev.parent);
   VMK_ASSERT(vport);

   return (vmk_fc_vport_terminate(vport));
}

/**
 * fc_vport_sched_delete - workq-based delete request for a vport
 *
 * @work:	vport to be deleted.
 **/
static void
fc_vport_sched_delete(void *data)
{
   struct work_struct *work;
   struct fc_vport *vport;
   int stat;

   work = (struct work_struct *) data;
   VMK_ASSERT(work);
   vport = container_of(work, struct fc_vport, vport_delete_work);
   VMK_ASSERT(vport);
   stat = vmk_fc_vport_terminate(vport);
   if (stat) {
      vmk_LogDebug(vmklinux26Log, 0, "%s: %s could not be deleted created via "
         "shost%d channel %d - error %d\n", __FUNCTION__,
         vport->dev.bus_id, vport->shost->host_no,
         vport->channel, stat);
   }
}

      

/**
 * Calls the LLDD vport_delete() function, then deallocates and removes
 * the vport from the shost and object tree.
 *
 * Notes:
 *	This routine assumes no locks are held on entry.
 **/
static int
vmk_fc_vport_terminate(struct fc_vport *vport)
{
   struct Scsi_Host *shost;
   struct fc_host_attrs *fc_host;
   struct fc_internal *i;
   struct device *dev;
   unsigned long flags;
   int stat;

   VMK_ASSERT(vport);
   shost = vport_to_shost(vport);
   VMK_ASSERT(shost);
   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);
   i = to_fc_internal(shost->transportt);
   VMK_ASSERT(i);
   dev = &vport->dev;
   VMK_ASSERT(dev);

   spin_lock_irqsave(shost->host_lock, flags);
   if (vport->flags & FC_VPORT_CREATING) {
      spin_unlock_irqrestore(shost->host_lock, flags);
      return -EBUSY;
   }
   if (vport->flags & (FC_VPORT_DEL)) {
      spin_unlock_irqrestore(shost->host_lock, flags);
      return -EALREADY;
   }
   vport->flags |= FC_VPORT_DELETING;
   spin_unlock_irqrestore(shost->host_lock, flags);

   if (i->f->vport_delete) {
      VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(shost), stat,
            i->f->vport_delete, vport);
   } else {
      stat = -ENOENT;
   }

   spin_lock_irqsave(shost->host_lock, flags);
   vport->flags &= ~FC_VPORT_DELETING;
   if (!stat) {
      vport->flags |= FC_VPORT_DELETED;
      list_del(&vport->peers);
      fc_host->npiv_vports_inuse--;
      put_device(&shost->shost_gendev);  /* for fc_host->vport list */
   }
   spin_unlock_irqrestore(shost->host_lock, flags);

   if (stat)
      return stat;

   if (dev->parent != &shost->shost_gendev) {
      sysfs_remove_link(&shost->shost_gendev.kobj, dev->bus_id);
   }
   device_del(dev);

   /*
    * Removing our self-reference should mean our
    * release function gets called, which will drop the remaining
    * parent reference and free the data structure.
    */
   put_device(dev);			/* for self-reference */

   vmk_ScsiAdapterEvent(((struct vmklnx_ScsiAdapter *)shost->adapter)->vmkAdapter,
         VMK_SCSI_ADAPTER_EVENT_FC_REMOVED_VPORT);
   return 0; /* SUCCESS */
}


/*
 * vmk_fc_vport_getinfo
 *
 */
int
vmk_fc_vport_getinfo(struct Scsi_Host *shost, void *info)
{
   struct vmk_VportInfo *pi;
   struct fc_host_attrs *fc_host;
   struct fc_vport *vport;
   struct fc_internal *i;

   VMK_ASSERT(shost);

   if ((struct vmklnx_ScsiModule *)shost->transportt->module == NULL ||
       ((struct vmklnx_ScsiModule *)shost->transportt->module)->transportType !=
               VMKLNX_SCSI_TRANSPORT_TYPE_FC ) {
      vmk_LogDebug(vmklinux26Log, 2, "vmk_fc_vport_getinfo: not supported:"
	" shost=%p\n", shost); 
      return -ENOENT;
   }

   i = to_fc_internal(shost->transportt);
   VMK_ASSERT(i);
   fc_host = shost_to_fc_host(shost);
   VMK_ASSERT(fc_host);

   if (fc_host->port_type == FC_PORTTYPE_UNKNOWN) {
      if (i->f && i->f->get_host_port_type) {
         VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost),
                                 i->f->get_host_port_type, shost);
      }
      /* If port type is still unknown, bail out */
      if (fc_host->port_type == FC_PORTTYPE_UNKNOWN) {
          vmk_LogDebug(vmklinux26Log, 2, "FC port type is unknown"
             " shost=%p\n", shost);
         return -ENOENT;
      }
   }

   /* 
    * make sure this is not a vport before we check for vport capability 
    */
   if (i->f && (fc_host->port_type != FC_PORTTYPE_NPIV && 
	(! i->f->vport_create))) {
      vmk_LogDebug(vmklinux26Log, 2, "vmk_fc_vport_getinfo: not supported2:"
	" shost=%p, i=%p, f=%p\n", shost, i, i->f); 
      return -ENOENT;
   }

   VMK_ASSERT(info);
   pi = (struct vmk_VportInfo *) info;

   vmk_LogDebug(vmklinux26Log, 2, "vmk_fc_vport_getinfo: shost=%p, info=%p\n", 
	shost, info); 

   /* 
    * If this is not a physical Host, fill in something else
    */
   if (fc_host->port_type == FC_PORTTYPE_NPIV) {
      vport = dev_to_vport(&shost->shost_gendev);
      VMK_ASSERT(vport);
      pi->linktype = VMK_VPORT_TYPE_VIRTUAL;
      pi->vports_max = VMK_VPORT_CNT_INVALID;
      pi->vports_inuse = VMK_VPORT_CNT_INVALID;
      pi->rpi_max = VMK_VPORT_CNT_INVALID;
      pi->rpi_inuse = VMK_VPORT_CNT_INVALID;
      goto check_state;
   }

   vmk_LogDebug(vmklinux26Log, 2, "vmk_fc_vport_getinfo: physical port:"
	" shost=%p, info=%p\n", shost, info); 

   /* 
    * must be a physical port
    * just fill in the important blanks
    */
   pi->vports_max = fc_host->max_npiv_vports;
   /*
    * PR 266660 - some drivers set max_npiv_vports
    * to VMK_VPORT_CNT_INVALID from a u16.
    * max_npiv_vports is a u32 and so is vports_max.
    */
   if (pi->vports_max == 0xffff) {
      vmk_LogDebug(vmklinux26Log, 0, "vmk_fc_vport_getinfo: PR 266660:"
            " vports_max=%x\n", pi->vports_max); 
      pi->vports_max = VMK_VPORT_CNT_INVALID;
   }
   pi->vports_inuse = fc_host->npiv_vports_inuse;
   pi->linktype = VMK_VPORT_TYPE_PHYSICAL;
   pi->fail_reason = VMK_VPORT_FAIL_UNKNOWN;
   pi->prev_fail_reason = VMK_VPORT_FAIL_UNKNOWN;

   u64_to_wwn(fc_host->node_name, pi->node_name);
   u64_to_wwn(fc_host->port_name, pi->port_name);

   /* 
    * check the state
    */
check_state:
   if (i->f->get_host_port_state) {
      VMKAPI_MODULE_CALL_VOID(SCSI_GET_MODULE_ID(shost),
                i->f->get_host_port_state, shost);
   }

   switch (fc_host->port_state) {
      case FC_PORTSTATE_OFFLINE:
      case FC_PORTSTATE_NOTPRESENT:
      case FC_PORTSTATE_BLOCKED:
         pi->state = VMK_VPORT_STATE_OFFLINE;
         break;
      case FC_PORTSTATE_ONLINE:
         pi->state = VMK_VPORT_STATE_ACTIVE;
         break;
      default:
         pi->state = VMK_VPORT_STATE_FAILED;
         break;
   }

   /*
    * set the version we are supporting
    */
   pi->api_version = VMK_VPORT_API_VERSION;
   return 0;
}


/*
 * SAS transport functions
 */

/*
 * SAS host attributes
 */
                                                                                
/**
 *
 *  \globalfn vmklnx_sas_host_setup -- initialize sas_host_attrs 
 *
 *  \param shost pointer to a Scsi_Host structure 
 *  \return 0 for SUCCESS; negative values if failed 
 *  \par Include:
 *  scsi/scsi_transport_sas.h
 *  \par ESX Deviation Notes:
 *
 *  \sa None.
 *  \comments - 
 *
 */
int vmklnx_sas_host_setup(struct Scsi_Host *shost)
{
   struct sas_host_attrs *sas_host = to_sas_host_attrs(shost);


   if (sas_host) {
      if ((sas_host->rphy_list.prev == NULL) || 
          (sas_host->rphy_list.next == NULL)) {
         /*
          * sas_host_attrs has not been initialized yet.
          */
         INIT_LIST_HEAD(&sas_host->rphy_list);
         mutex_init(&sas_host->lock);
         sas_host->next_target_id = 0;
	 INIT_LIST_HEAD(&sas_host->freed_rphy_list);
         sas_host->next_expander_id = 0;
         sas_host->next_port_id = 0;
        /*
         * Create Mgmt Adapter Instance to our management
         */
         if (SasLinuxAttachMgmtAdapter(shost)) {
            return -ENOMEM;
         }
      }
      return 0;
   } else {
      return(-1);
   }
}

/**                                    
 *  sas_phy_free - free a SAS PHY      
 *  @phy: SAS PHY to free
 *                                           
 *  Frees the specified SAS PHY.
 *
 */                                          
/* _VMKLNX_CODECHECK_: sas_phy_free */
void sas_phy_free(struct sas_phy *phy)
{
}

/**
 *
 *  \globalfn sas_phy_add -- add a SAS to the device hierarchy
 *
 *  \param sas_phy a pointer to a sas_phy structure 
 *  \return 0 for SUCCESS; Otherwise if failed 
 *  \par Include:
 *  scsi/scsi_transport_sas.h
 *  \par ESX Deviation Notes:
 *  No OP.
 *  \sa None.
 *  \comments - 
 *
 */
/**                                          
 *  sas_phy_add - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: sas_phy_add */
int sas_phy_add(struct sas_phy *phy)
{
    int error;

    error = device_add(&phy->dev);
    return error;
}

/**
 *
 *  \globalfn sas_phy_alloc -- allocate and initialize a SAS PHY structure
 *
 *  \param parent a pointer to the parent device 
 *  \param number a PHY index 
 *  \return a pointer to the allocated PHY structure; NULL if failed 
 *  \par Include:
 *  scsi/scsi_transport_sas.h
 *  \par ESX Deviation Notes:
 *
 *  \sa None.
 *  \comments - 
 *
 */
/**                                          
 *  sas_phy_alloc - allocate an intialize a SAS PHY structure
 *  @parent: parent device to connect sas phy structure to
 *  @number: a PHY index
 * 
 *  Allocates and intializes a SAS PHY structure. It will be added as a child in
 *  the device tree to the specified @parent. @parent must be a Scsi_Host or 
 *  sas_rphy
 * 
 */                                          
/* _VMKLNX_CODECHECK_: sas_phy_alloc */
struct sas_phy *sas_phy_alloc(struct device *parent, int number)
{
        struct Scsi_Host *shost;
        struct sas_phy *phy;

        shost = dev_to_shost(parent);
        VMK_ASSERT(shost);
        phy = (struct sas_phy *)VMKLinux26_Alloc(sizeof(struct sas_phy));
        if (!phy)
                return NULL;
        device_initialize(&phy->dev);
        phy->number = number;
        phy->dev.parent = get_device(parent);
        phy->dev.dev_type = SAS_PHY_DEVICE_TYPE;
        phy->dev.release = sas_phy_release;
        INIT_LIST_HEAD(&phy->port_siblings);
        if (scsi_is_sas_expander_device(parent)) {
                struct sas_rphy *rphy = dev_to_rphy(parent);
                sprintf(phy->dev.bus_id, "phy-%d:%d:%d", shost->host_no,
                        rphy->scsi_target_id, number);
        } else
                sprintf(phy->dev.bus_id, "phy-%d:%d", shost->host_no, number);
        return phy;
}

/**                                          
 *  sas_rphy_free - free a SAS remote PHY
 *  @rphy: SAS remote PHY to free
 * 
 *  Frees the specified SAS remote PHY.
 *  This function must only be called on a remote PHY that has not sucessfully
 *  been added using sas_rphy_add().
 *
 *  RETURN VALUE:
 *  None
 */                                          
/* _VMKLNX_CODECHECK_: sas_rphy_free */
void sas_rphy_free(struct sas_rphy *rphy)
{
        struct device *dev;
        struct Scsi_Host *shost = NULL;
        struct sas_host_attrs *sas_host = to_sas_host_attrs(shost);
        VMK_ASSERT(rphy);
        dev = &rphy->dev;
        shost = dev_to_shost(rphy->dev.parent->parent);
        VMK_ASSERT(shost);
        sas_host = to_sas_host_attrs(shost);
        VMK_ASSERT(sas_host);
        mutex_lock(&sas_host->lock);
        list_del(&rphy->list);
        mutex_unlock(&sas_host->lock);
	put_device(dev);
}

/**
 * sas_assign_scsi_target_id - assign a scsi target id to the SAS device
 * It first checks if it is a device that was previously removed and if it is,
 * it assigns the old target id back. If not, the device gets the next target
 * id.
 *
 * Returns 1 if a new id was assigned and 0 if none
 */
static int
sas_assign_scsi_target_id(struct sas_rphy *rphy,
                                      struct sas_host_attrs *sas_host)
{
   struct sas_rphy *t_rphy;
   struct sas_port *parent;
   struct Scsi_Host *shost;
   unsigned long flags;
   
   vmk_LogDebug(vmklinux26Log, 5,
                "%s: Looking for sas_address 0x%llx rphy:%p\n", 
                __FUNCTION__, rphy->identify.sas_address, rphy);
   VMK_ASSERT(rphy);
   parent = dev_to_sas_port(rphy->dev.parent);
   VMK_ASSERT(parent);
   shost = dev_to_shost(parent->dev.parent);
   VMK_ASSERT(shost);
   list_for_each_entry(t_rphy, &sas_host->freed_rphy_list, list) {
      vmk_LogDebug(vmklinux26Log, 5,
                         "%s: Examining "
                   "sas_address 0x%llx (matching with 0x%llx)\n",
                   __FUNCTION__, t_rphy->identify.sas_address, 
                   rphy->identify.sas_address);
      if (t_rphy->identify.sas_address == rphy->identify.sas_address) {
	 /* Grab host_lock as the list_del will happen on shost->__targets */
         spin_lock_irqsave(shost->host_lock, flags);
         /*
	  * The children of this rphy are scsi_target structs.
	  * Walk every child and remove it from shost's __targets list
	  */
	 device_for_each_child(&t_rphy->dev, NULL, sas_scsi_target_list_del);
         spin_unlock_irqrestore(shost->host_lock, flags);

         vmk_LogDebug(vmklinux26Log, 5, "%s: "
                      "Assigning target id %d for sas_address 0x%llx from "
                      "freed list; old rphy->scsi_target_id:0x%d; rphy:0x%p "
                      "t_rphy:0x%p\n", __FUNCTION__, t_rphy->scsi_target_id,
                      rphy->identify.sas_address, rphy->scsi_target_id, rphy,
                      t_rphy);
         /* Now assign the old target id to the new rphy */
         rphy->scsi_target_id = t_rphy->scsi_target_id;
         list_del(&t_rphy->list);

         vmk_LogDebug(vmklinux26Log, 5, "%s: t_rphy: %p - refcount before "
	    "put_device: %d\n", __FUNCTION__, t_rphy,
	    atomic_read(&t_rphy->dev.kobj.kref.refcount));

         /* There is a new rphy now. The old one can now be released */
         put_device(&t_rphy->dev);
         return 0;
      }
   }
   rphy->scsi_target_id = sas_host->next_target_id++;
   vmk_LogDebug(vmklinux26Log, 5, "%s: Assigning target id %d for sas_address "
                "0x%llx NOT from freed list\n",
                __FUNCTION__, rphy->scsi_target_id, rphy->identify.sas_address);
   return 1;
}

/**                                          
 *  sas_rphy_add - add a SAS remote PHY to the device hierachy
 *  @rphy: The remote PHY to be added
 *
 *  Publishes a SAS remote PHY to the rest of the system.
 *  Assumes sas_host->lock is held as needed
 *
 *  RETURN VALUE:
 *  0 on success
 *  negative errno code on error
 */                                          
/* _VMKLNX_CODECHECK_: sas_rphy_add */
int sas_rphy_add(struct sas_rphy *rphy)
{
        struct sas_port *parent;
        struct Scsi_Host *shost;
        struct sas_host_attrs *sas_host;
        struct sas_identify *identify;
        int error;
                                                                                
        VMK_ASSERT(rphy);
        parent = dev_to_sas_port(rphy->dev.parent);
        VMK_ASSERT(parent);
        shost = dev_to_shost(parent->dev.parent);
        VMK_ASSERT(shost);
        sas_host = to_sas_host_attrs(shost);
        identify = &rphy->identify;
        if (parent->rphy)
                return -ENXIO;
        parent->rphy = rphy;
        error = device_add(&rphy->dev);
        if (error) {
		vmk_LogDebug(vmklinux26Log, 1, "sas_rphy_add : rphy: %p device_add failed with %d\n", rphy, error);
                return error;
	}

        mutex_lock(&sas_host->lock);
        list_add_tail(&rphy->list, &sas_host->rphy_list);
        if (identify->device_type == SAS_END_DEVICE &&
            (identify->target_port_protocols &
             (SAS_PROTOCOL_SSP|SAS_PROTOCOL_STP|SAS_PROTOCOL_SATA))) {
                sas_assign_scsi_target_id(rphy, sas_host);
	}
        else if (identify->device_type == SAS_END_DEVICE)
                rphy->scsi_target_id = -1;
        mutex_unlock(&sas_host->lock);
                                                                                
        if (identify->device_type == SAS_END_DEVICE &&
            rphy->scsi_target_id != -1) {
                scsi_scan_target(&rphy->dev, 0,
                                rphy->scsi_target_id, ~0, 0);
        }
                                                                                
        return 0;
}

/**
 * sas_scsi_target_list_del - Remove the scsi_target from shost's list
 */
static int
sas_scsi_target_list_del(struct device *dev, void *data)
{
   struct scsi_target *stgt = to_scsi_target(dev);
   
   if (dev && stgt) {
	   list_del(&stgt->siblings);
            vmk_LogDebug(vmklinux26Log, 5,
		"%s: Found and deleted target (%p)\n", __FUNCTION__, stgt);
   }

   return 0;
}

/**
 *  sas_rphy_delete - remove SAS remote PHY
 *  @rphy: SAS remote PHY to remove
 *
 *  Removes the specified SAS remote PHY.
 *
 *  RETURN VALUE:
 *  None
 */                                          
/* _VMKLNX_CODECHECK_: sas_rphy_delete */
void sas_rphy_delete(struct sas_rphy *rphy)
{
        struct device *dev = &rphy->dev;
        struct sas_port *parent = dev_to_sas_port(dev->parent);
        struct Scsi_Host *shost = dev_to_shost(parent->dev.parent);
        struct sas_host_attrs *sas_host = to_sas_host_attrs(shost);
                                                                                
        if (NULL == sas_host) {
           return;
        }
        switch (rphy->identify.device_type) {
        case SAS_END_DEVICE:
                break;
        case SAS_EDGE_EXPANDER_DEVICE:
        case SAS_FANOUT_EXPANDER_DEVICE:
                break;
        default:
                break;
        }
        device_del(dev);

        mutex_lock(&sas_host->lock);
        list_del(&rphy->list);
        list_add_tail(&rphy->list, &sas_host->freed_rphy_list);
        vmk_LogDebug(vmklinux26Log, 3, "%s: rphy:%p - Added target id %d for "
	    "sas_address 0x%llx to freed list device_type:%d\n", __FUNCTION__,
	    rphy, rphy->scsi_target_id, rphy->identify.sas_address,
	    rphy->identify.device_type);
        mutex_unlock(&sas_host->lock);
        parent->rphy = NULL;
}


static void sas_rphy_initialize(struct sas_rphy *rphy)
{
        INIT_LIST_HEAD(&rphy->list);
}


/**
 *
 *  \globalfn sas_end_device_alloc -- allocate a SAS rphy and connect to
 *   its parent device 
 *
 *  \param sas_port a pointer to a struct sas_port 
 *  \return  allocated SAS PHY; NULL if failed 
 *  \par Include:
 *  scsi/scsi_transport_sas.h
 *  \par ESX Deviation Notes:
 *  None. 
 *  \sa None.
 *  \comments - 
 *
 */
/**                                          
 *  sas_end_device_alloc - allocate a SAS rphy and connect to its parent device
 *  @parent:  parent to connect SAS rphy port to 
 *                                           
 *  Allocates a SAS remote PHY structure, connected to @parent
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: sas_end_device_alloc */
struct sas_rphy *sas_end_device_alloc(struct sas_port *parent)
{
        struct Scsi_Host *shost;
        struct sas_end_device *rdev;
                                                                                
        VMK_ASSERT(parent);
        shost = dev_to_shost(&parent->dev);
        VMK_ASSERT(shost);
        rdev = (struct sas_end_device *)
		VMKLinux26_Alloc(sizeof(struct sas_end_device));
        if (!rdev) {
                return NULL;
        }
         
        device_initialize(&rdev->rphy.dev);
        rdev->rphy.dev.parent = get_device(&parent->dev);
        rdev->rphy.dev.dev_type = SAS_END_DEVICE_TYPE;
        rdev->rphy.dev.release = sas_end_device_release;
        if (scsi_is_sas_expander_device(parent->dev.parent)) {
                struct sas_rphy *rphy = dev_to_rphy(parent->dev.parent);
                sprintf(rdev->rphy.dev.bus_id, "end_device-%d:%d:%d",
                shost->host_no, rphy->scsi_target_id, parent->port_identifier);
        } else
                sprintf(rdev->rphy.dev.bus_id, "end_device-%d:%d",
                        shost->host_no, parent->port_identifier);
        rdev->rphy.identify.device_type = SAS_END_DEVICE;
        sas_rphy_initialize(&rdev->rphy);
        return &rdev->rphy;
}

/**
 *
 *  \globalfn sas_expander_alloc -- allocate an rphy for an end device 
 *
 *  \param sas_port a pointer to a struct sas_port 
 *  \param type a value from sas_device_type enum 
 *  \return allocated SAS PHY; NULL if failed 
 *  \par Include:
 *  scsi/scsi_transport_sas.h
 *  \par ESX Deviation Notes:
 * 
 *  \sa None.
 *  \comments - 
 *
 */
/**                                          
 *  sas_expander_alloc - allocate an rphy for an end device
 *  @parent: parent to connect SAS rphy to
 *  @type: device type
 *                                           
 *  Allocates a SAS remote PHY structure, connected to @parent and sets its type.
 *  Valid types are: SAS_EDGE_EXPANDER_DEVICE and SAS_FANOUT_EXPANDER_DEVICE
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: sas_expander_alloc */
struct sas_rphy *sas_expander_alloc(struct sas_port *parent,
                                    enum sas_device_type type)
{
        struct Scsi_Host *shost;
        struct sas_expander_device *rdev;
        struct sas_host_attrs *sas_host;
                                                                                
        VMK_ASSERT(parent);
        VMK_ASSERT((type == SAS_EDGE_EXPANDER_DEVICE) || 
		(type == SAS_FANOUT_EXPANDER_DEVICE));
        shost = dev_to_shost(&parent->dev);
        VMK_ASSERT(shost);
        sas_host = to_sas_host_attrs(shost);
        VMK_ASSERT(sas_host);
        rdev = (struct sas_expander_device *)
		VMKLinux26_Alloc(sizeof(struct sas_expander_device));
        if (!rdev) {
                return NULL;
        }
                                                                                
        device_initialize(&rdev->rphy.dev);
        rdev->rphy.dev.parent =  get_device(&parent->dev);
        rdev->rphy.dev.dev_type = SAS_EXPANDER_DEVICE_TYPE;
        rdev->rphy.dev.release = sas_expander_release;
        mutex_lock(&sas_host->lock);
        rdev->rphy.scsi_target_id = sas_host->next_expander_id++;
        mutex_unlock(&sas_host->lock);
        sprintf(rdev->rphy.dev.bus_id, "expander-%d:%d",
                shost->host_no, rdev->rphy.scsi_target_id);
        rdev->rphy.identify.device_type = type;
        sas_rphy_initialize(&rdev->rphy);
        return &rdev->rphy;
}

/**
 *
 *  \globalfn sas_is_sas_port -- check if it is a port device 
 *
 *  \param dev a pointer to a struct device 
 *  \return 1 if it is a SAS remote PHY; 0 otherwise 
 *  \par Include:
 *  scsi/scsi_transport_sas.h
 *  \par ESX Deviation Notes:
 *  It uses device instead. 
 *  \sa None.
 *  \comments - 
 *
 */
int scsi_is_sas_port(const struct device *dev)
{
    if (dev) {
        return(dev->dev_type == SAS_PORT_DEVICE_TYPE);
    } else {
        return(0);
    }
}

/**
 *
 *  \globalfn sas_is_sas_phy -- check if it is a phy device 
 *
 *  \param dev a pointer to a struct device 
 *  \return 1 if it is a SAS PHY; 0 otherwise 
 *  \par Include:
 *  scsi/scsi_transport_sas.h
 *  \par ESX Deviation Notes:
 *  It uses device instead. 
 *  \sa None.
 *  \comments - 
 *
 */
int scsi_is_sas_phy(const struct device *dev)
{
    if (dev) {
        return(dev->dev_type == SAS_PHY_DEVICE_TYPE);
    } else {
        return(0);
    }
}

/**
 *  scsi_is_sas_rphy - check if it is a rphy device
 *  @dev: a pointer to struct device
 *
 *  This function is used to identify if @dev is a SAS remote PHY.
 *
 *  RETURN VALUE:
 *  Returns TRUE if @dev is a SAS remote PHY, FALSE otherwise.
 *
 */
/* _VMKLNX_CODECHECK_: scsi_is_sas_rphy */
int scsi_is_sas_rphy(const struct device *dev)
{
    if (dev) {
        return(dev->dev_type == SAS_END_DEVICE_TYPE || 
		dev->dev_type == SAS_EXPANDER_DEVICE_TYPE);
    } else {
        return(0);
    }
}

/**
 *
 *  \globalfn sas_attach_transport -- Attach SAS transport
 *
 *  \param scmd
 *  \return None
 *  \par Include:
 *  scsi/scsi_transport_sas.h
 *  \par ESX Deviation Notes:
 * 
 *  \sa None.
 *  \comments 
 *
 */
/**                                          
 *  sas_attach_transport - instantiate SAS transport template
 *  @ft: SAS transport class function template
 *                                           
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: sas_attach_transport */
struct scsi_transport_template *
sas_attach_transport(struct sas_function_template *ft)
{
   struct vmklnx_ScsiModule *vmklnx26ScsiModule;
   struct sas_internal *i;

   VMK_ASSERT(ft);
   i = (struct sas_internal *) VMKLinux26_Alloc(sizeof(struct sas_internal));
   VMK_ASSERT(i);

   if (unlikely(!i)) {
      vmk_WarningMessage("%s - Unable to allocate memory for spi_internal\n", 
	__FUNCTION__);
      return NULL;
   }

   vmklnx26ScsiModule = vmklnx_alloc_scsimod(VMKLNX_SCSI_TRANSPORT_TYPE_SAS, i);

   if (!vmklnx26ScsiModule) {
      VMKLinux26_Free(i);
      return NULL;
   }

   i->t.module = (void *)vmklnx26ScsiModule;
   i->t.host_size = sizeof(struct sas_host_attrs);
   i->f = ft;

   return &i->t;
}

/**
 *
 *  \globalfn sas_release_transport -- Release SAS transport
 *
 *  \param scmd
 *  \return None
 *  \par Include:
 *   scsi/scsi_transport_sas.h
 *  \par ESX Deviation Notes:
 *  
 *  \sa None.
 *  \comments
 *
 */
/**                                          
 *  sas_release_transport - Release SAS transport template instance
 *  @t: transport template instance
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: sas_release_transport */
void sas_release_transport(struct scsi_transport_template *t)
{
   struct sas_internal *i = to_sas_internal(t);

   /*
    * Free up the module structure
    */
   VMKLinux26_Free(t->module);

   /*
    * Free up the transport_internal structure
    */
   VMKLinux26_Free(i);
}


void sas_phy_delete(struct sas_phy *phy)
{
   struct device *dev = &phy->dev;

   if(NULL == phy) {
      return;
   }
   device_del(dev);
   put_device(dev);

   return;
}

static int do_sas_phy_delete(struct device *dev, void *data)
{
        int pass = (int)(unsigned long)data;
                                                                                
        if (pass == 0 && scsi_is_sas_port(dev))
                sas_port_delete(dev_to_sas_port(dev));
        else if (pass == 1 && scsi_is_sas_phy(dev))
                sas_phy_delete(dev_to_phy(dev));
        return 0;
}

/**                                          
 *  sas_read_port_mode_page - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: sas_read_port_mode_page */
int sas_read_port_mode_page(struct scsi_device *sdev)
{
/* only need 8 bytes of data plus header (4 or 8) */
#define BUF_SIZE 64

        unsigned char *buffer;
        unsigned char *msdata;
        struct sas_rphy *rphy;
        struct sas_end_device *rdev;
        struct scsi_mode_data mode_data;
        int res, error;
                                                                                
        VMK_ASSERT(sdev);
        VMK_ASSERT(sdev->sdev_target);
        rphy = target_to_rphy(sdev->sdev_target);
        VMK_ASSERT(rphy);
        VMK_ASSERT(rphy->identify.device_type == SAS_END_DEVICE);
        rdev = rphy_to_end_device(rphy);
        buffer = (unsigned char *) VMKLinux26_Alloc(BUF_SIZE); 
        if (!buffer)
                return -ENOMEM;
                                                                                
        res = scsi_mode_sense(sdev, 1, 0x19, buffer, BUF_SIZE, 30*HZ, 3,
                              &mode_data, NULL);
                                                                                
        error = -EINVAL;
        if (!scsi_status_is_good(res))
                goto out;
                                                                                
        msdata = buffer +  mode_data.header_length +
                mode_data.block_descriptor_length;
                                                                                
        if (msdata - buffer > BUF_SIZE - 8)
                goto out;
                                                                                
        error = 0;
                                                                                
        rdev->ready_led_meaning = msdata[2] & 0x10 ? 1 : 0;
        rdev->I_T_nexus_loss_timeout = (msdata[4] << 8) + msdata[5];
        rdev->initiator_response_timeout = (msdata[6] << 8) + msdata[7];

 out:
        VMKLinux26_Free(buffer);
        return error;
}

/**
 *
 *  \globalfn sas_remove_children -- tear down a device SAS data structure 
 *
 *  \param dev a pointer to device belonging to the SAS object 
 *  \return void
 *  \par Include:
 *   scsi/scsi_transport_sas.h
 *  \par ESX Deviation Notes:
 *  
 *  \sa None.
 *  \comments
 *  It must be called just before scsi_remove_host for SAS HBAs.
 *
 */
void sas_remove_children(struct device *dev)
{
        VMK_ASSERT(dev);
        device_for_each_child(dev, (void *)0, do_sas_phy_delete);
        device_for_each_child(dev, (void *)1, do_sas_phy_delete);
}

/**
 *  sas_remove_host - terminate any sas_transport related elements
 *  @shost: Pointer to struct Scsi_Host
 *
 *  Terminates any sas_transport related elements for a scsi host.
 *  This routine is expected to be called immediately preceeding the call from 
 *  the driver to scsi_remove_host().
 *
 *  RETURN VALUE:
 *  None
 */                                                   
/* _VMKLNX_CODECHECK_: sas_remove_host */
void sas_remove_host(struct Scsi_Host *shost)
{
        VMK_ASSERT(shost);
        sas_remove_children(&shost->shost_gendev);
}

/**
 *
 *  \globalfn sas_port_alloc -- allocate and initialize a SAS port structure
 *
 *  \param parent a pointer to the parent device 
 *  \param port_id a port number 
 *  \return a pointer to the allocated PHY structure; NULL if failed 
 *  \par Include:
 *  scsi/scsi_transport_sas.h
 *  \par ESX Deviation Notes:
 *
 *  \sa None.
 *  \comments - 
 *
 */
struct sas_port *sas_port_alloc(struct device *parent, int port_id)
{
        struct Scsi_Host *shost = dev_to_shost(parent);
        struct sas_port *port;

        port = (struct sas_port *) VMKLinux26_Alloc(sizeof(*port));
        if (!port)
                return NULL;
        port->port_identifier = port_id;
        device_initialize(&port->dev);
        port->dev.parent = get_device(parent);
        port->dev.dev_type = SAS_PORT_DEVICE_TYPE;
	port->dev.release = sas_port_release;
        mutex_init(&port->phy_list_mutex);
        INIT_LIST_HEAD(&port->phy_list);
        if (scsi_is_sas_expander_device(parent)) {
                struct sas_rphy *rphy = dev_to_rphy(parent);
                sprintf(port->dev.bus_id, "port-%d:%d:%d", shost->host_no,
                        rphy->scsi_target_id, port->port_identifier);
        } else {
                sprintf(port->dev.bus_id, "port-%d:%d", shost->host_no,
                        port->port_identifier);
        }
        return port;
}

/**
 *  sas_port_alloc_num - allocate and initialize a SAS port structure
 *  @parent: a pointer to the parent device
 *
 *  Allocates a SAS port structure and a number to go with it. This interface is
 *  really for adapters where the port number has no meaning, so the sas class
 *  should manage them. It will be added to the device tree below the device
 *  specified by @parent which must be either a Scsi_Host or a
 *  sas_expander_device.
 *
 *  RETURN VALUE:
 *  Returns a pointer to the allocated PHY structure; Returns NULL on error.
 *
 */
/* _VMKLNX_CODECHECK_: sas_port_alloc_num */
struct sas_port *sas_port_alloc_num(struct device *parent)
{
        int index;
        struct Scsi_Host *shost;
        struct sas_host_attrs *sas_host;
        VMK_ASSERT(parent);
        shost = dev_to_shost(parent);
        VMK_ASSERT(shost);
        sas_host = to_sas_host_attrs(shost);
        VMK_ASSERT(sas_host);
                                                                                
        /* FIXME: use idr for this eventually */
        mutex_lock(&sas_host->lock);
        if (scsi_is_sas_expander_device(parent)) {
                struct sas_rphy *rphy = dev_to_rphy(parent);
                struct sas_expander_device *exp = rphy_to_expander_device(rphy);                                                                                
                index = exp->next_port_id++;
        } else
                index = sas_host->next_port_id++;
        mutex_unlock(&sas_host->lock);
        return sas_port_alloc(parent, index);
}

/**
 *  sas_port_mark_backlink - mark the sas port backlink
 *  @port: a pointer to struct sas_port
 *
 *  Marks the port as a backlink.
 *
 *  RETURN VALUE:
 *  This function does not return a value.
 *
 */
/* _VMKLNX_CODECHECK_: sas_port_mark_backlink */
void sas_port_mark_backlink(struct sas_port *port)
{
        struct device *parent;
        VMK_ASSERT(port);
        parent = port->dev.parent->parent->parent;
                                                                                
        if (port->is_backlink)
                return;
        port->is_backlink = 1;
}

/**                                          
 *  sas_port_add - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: sas_port_add */
int sas_port_add(struct sas_port *port)
{
   int error;

    /* No phys should be added until this is made visible */
   BUG_ON(!list_empty(&port->phy_list));

   error = device_add(&port->dev);

   return error;

}

/**                                          
 *  sas_port_add_phy - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: sas_port_add_phy */
void sas_port_add_phy(struct sas_port *port, struct sas_phy *phy)
{
        VMK_ASSERT(port);
        VMK_ASSERT(phy);
        mutex_lock(&port->phy_list_mutex);
        if (unlikely(!list_empty(&phy->port_siblings))) {
                /* make sure we're already on this port */
                struct sas_phy *tmp;
                                                                                
                list_for_each_entry(tmp, &port->phy_list, port_siblings)
                        if (tmp == phy)
                                break;
                /* If this trips, you added a phy that was already
                 * part of a different port */
                if (unlikely(tmp != phy)) {
                        dev_printk(KERN_ERR, &port->dev, "trying to add"
			" phy %s fails: it's already part of another port\n", 
			phy->dev.bus_id);
                        VMK_ASSERT(FALSE);
                }
        } else {
                list_add_tail(&phy->port_siblings, &port->phy_list);
                port->num_phys++;
        }
        mutex_unlock(&port->phy_list_mutex);
}

/**                                          
 *  sas_port_delete_phy - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: sas_port_delete_phy */
void sas_port_delete_phy(struct sas_port *port, struct sas_phy *phy)
{
        VMK_ASSERT(port);
        VMK_ASSERT(phy);
        mutex_lock(&port->phy_list_mutex);
        list_del_init(&phy->port_siblings);
        port->num_phys--;
        mutex_unlock(&port->phy_list_mutex);
}

/**                                          
 *  sas_port_delete - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: sas_port_delete */
void sas_port_delete(struct sas_port *port)
{
        struct device *dev;
        struct sas_phy *phy, *tmp_phy;

        VMK_ASSERT(port);
        dev = &port->dev;
                                                                                
        if (port->rphy) {
                sas_rphy_delete(port->rphy);
                port->rphy = NULL;
        }
                                                                                
        mutex_lock(&port->phy_list_mutex);
        list_for_each_entry_safe(phy, tmp_phy, &port->phy_list,
                                 port_siblings) {
                list_del_init(&phy->port_siblings);
        }
        mutex_unlock(&port->phy_list_mutex);
        if (port->is_backlink) {
                port->is_backlink = 0;
        }
        device_del(dev);
        put_device(dev);
}

/**
 **********************************************************************
 * \internalfn sas_find_rphy -- Find a matching rphy
 *
 * \param sh, id
 * \return None
 * \par Include:
 * scsi/scsi_host.h
 * \par ESX Deviation Notes:
 * Needs
 * \sa None.
 **********************************************************************
 */
struct sas_rphy *
sas_find_rphy(struct Scsi_Host *sh, uint id)
{
   struct sas_host_attrs *sas_host;
   struct sas_rphy *rphy, *found_rphy = NULL;
                                                                                 
   VMK_ASSERT(sh);
   sas_host = to_sas_host_attrs(sh);
   VMK_ASSERT(sas_host);
  /*
   * Search for an existing target for this sdev.
   */
   mutex_lock(&sas_host->lock);
   list_for_each_entry(rphy, &sas_host->rphy_list, list) {
      if (rphy->scsi_target_id == id) {
         found_rphy = rphy;
         break;
      }
   }
   mutex_unlock(&sas_host->lock);
   return found_rphy;
}

static void fc_rport_dev_release(struct device *dev)
{
   struct fc_rport *rport = dev_to_rport(dev);
   put_device(dev->parent);
   VMKLinux26_Free(rport);
}

static void fc_vport_dev_release(struct device *dev)
{
  struct fc_vport *vport = dev_to_vport(dev);
  put_device(dev->parent);                /* release kobj parent */
  VMKLinux26_Free(vport);
}

static void sas_phy_release(struct device *dev)
{
   struct sas_phy *phy = dev_to_phy(dev);

   put_device(dev->parent);
   VMKLinux26_Free(phy);
}

static void sas_port_release(struct device *dev)
{
   struct sas_port *port = dev_to_sas_port(dev);

   put_device(dev->parent);
   VMKLinux26_Free(port);
}

static void sas_end_device_release(struct device *dev)
{
   struct sas_rphy *rphy = dev_to_rphy(dev);
   struct sas_end_device *edev = rphy_to_end_device(rphy);

   put_device(dev->parent);
   VMKLinux26_Free(edev);
}

static void sas_expander_release(struct device *dev)
{
   struct sas_rphy *rphy = dev_to_rphy(dev);
   struct sas_expander_device *edev = rphy_to_expander_device(rphy);

   put_device(dev->parent);
   VMKLinux26_Free(edev);
}

