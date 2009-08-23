/* **********************************************************
 * Copyright 2008 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * Management of IOAT/DCA DMA
 *
 * XXX
 * The functionality of registering and requesting DMA channels should
 * probably be moved up to vmkapi so that IOAT/DCA can be used outside of
 * vmklinux.
 */
#include <linux/dmaengine.h>
#include "linux_stubs.h"
#include "linux_ioat.h"
#include "vmkapi.h"

#define VMKLNX_LOG_HANDLE LinIoat
#include "vmklinux26_log.h"

#define MAX_IOAT_CTX 16

/*
 * This lock needs to be higher than tcp lock
 */
#define SP_RANK_IOAT_CTX (VMK_SP_RANK_SCSI_LOWEST)

/*
 * We need this indirection as the ioat module may not always be available
 */
struct ioatOps {
   void (*client_unregister)(struct dma_client *client);
   dma_cookie_t (*dma_memcpy)(struct dma_chan *chan, dma_addr_t dest,
                              dma_addr_t src, size_t len);
   enum dma_status (*dma_complete)(struct dma_chan *chan, dma_cookie_t cookie,
                                     dma_cookie_t *last, dma_cookie_t *used);
   void (*dma_flush)(struct dma_chan *chan);
};

struct dcaOps {
   u8 (*dca_get_tag)(int cpu);
   int (*dca_add_requester)(struct device *dev);
   int (*dca_remove_requester)(struct device *dev);
 };

static struct ioatOps ioatFuncs;
static struct dcaOps  dcaFuncs;
static struct dma_client *dmaClient = NULL;

static vmklnx_IoatContext ioatctxList[MAX_IOAT_CTX];

/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_dca_get_tag --
 *
 *    Get tag for cpu, which is used in TAG field of PCIe Message
 *
 * Results:
 *    Channel context.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

uint8_t vmklnx_dca_get_tag(int cpu)
{
   if (dcaFuncs.dca_get_tag) {
      return dcaFuncs.dca_get_tag(cpu);
   }
   else {
      return 0;  /* Bit[0] = 0b - DCA is disabled */
   }
}

/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_dca_add_requester --
 *
 *  Add DCA requester, so DCA engine can bookkeep all requesters.
 *
 * Results:
 *    Channel context.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
int vmklnx_dca_add_requester(struct device *dev)
{
   if (dcaFuncs.dca_add_requester) {
      return dcaFuncs.dca_add_requester(dev);
   }
   else {
      return -ENODEV;
   }
}

/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_dca_remove_requester --
 *
 *  Remove DCA requester, so DCA engine can bookkeep all requesters.
 *
 * Results:
 *    Channel context.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

int vmklnx_dca_remove_requester(struct device *dev)
{
    if (dcaFuncs.dca_remove_requester){
       return dcaFuncs.dca_remove_requester(dev);
    }
    else {
       return -ENODEV; 
    }
}

/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_IoatRequestChannel --
 *
 *    Request a DMA channel for use
 *
 * Results:
 *    Channel context.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

vmklnx_IoatContext*
vmklnx_IoatRequestChannel(void)
{
   int i;

   for (i = 0; i < MAX_IOAT_CTX; i++) {
      vmklnx_IoatContext *ctx = &ioatctxList[i];
      vmk_SPLock(&ctx->lock);
      if (ctx->free && ctx->channel) {
         ctx->free = VMK_FALSE;
         memset(&ctx->stats, 0, sizeof(ctx->stats));
         vmk_SPUnlock(&ctx->lock);
         return ctx;
      }
      vmk_SPUnlock(&ctx->lock);
   }
   return NULL;
}


/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_IoatReleaseChannel --
 *
 *    Release the DMA channel
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

void
vmklnx_IoatReleaseChannel(vmklnx_IoatContext *ctx)
{
   vmk_SPLock(&ctx->lock);
   VMK_ASSERT(!ctx->free);
   ctx->free = VMK_TRUE;
   vmk_SPUnlock(&ctx->lock);
}


/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_IoatMemcpy --
 *
 *    Post a DMA memcpy operation
 *
 * Results:
 *    cookie used for checking completion status
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

vmklnx_IoatXfer
vmklnx_IoatMemcpy(vmklnx_IoatContext *ctx, vmk_MachAddr dest, vmk_MachAddr src,
                  vmk_size_t len)
{
   dma_cookie_t cookie;
   vmk_SPLock(&ctx->lock);
   ctx->stats.memcpyCount++;
   if (ctx->channel) {
      cookie = ioatFuncs.dma_memcpy(ctx->channel, dest, src, len);
      if (cookie >= 0) {
         ctx->stats.memcpyBytes += len;
      }
   } else {
      cookie = -1;
   }
   vmk_SPUnlock(&ctx->lock);
   return (vmklnx_IoatXfer)cookie;
}


/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_IoatComplete --
 *
 *    Check for completion of a DMA memcpy operation
 *
 * Results:
 *    DMA memcpy result
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

vmklnx_IoatStatus
vmklnx_IoatComplete(vmklnx_IoatContext *ctx, vmklnx_IoatXfer xfer)
{
   enum dma_status status;
   dma_cookie_t cookie = (dma_cookie_t)xfer;
   
   vmk_SPLock(&ctx->lock);
   ctx->stats.completeCount++;
   if (ctx->channel) {
      status = ioatFuncs.dma_complete(ctx->channel, cookie, NULL, NULL);
   } else {
      status = DMA_ERROR;
   }
   vmk_SPUnlock(&ctx->lock);
   switch (status) {
      case DMA_SUCCESS:
         return VMKLNX_IOAT_SUCCESS;
      case DMA_IN_PROGRESS:
         return VMKLNX_IOAT_IN_PROGRESS;
      case DMA_ERROR:
         return VMKLNX_IOAT_ERROR;
      default:
         VMKLNX_WARN("driver returned %x for dma_complete", status);
         return VMKLNX_IOAT_ERROR;
   }
}


/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_IoatFlush --
 *
 *    Flush outstanding DMA memcpy request to hardware
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

void
vmklnx_IoatFlush(vmklnx_IoatContext *ctx)
{
   vmk_SPLock(&ctx->lock);
   ctx->stats.flushCount++;
   if (ctx->channel) {
      ioatFuncs.dma_flush(ctx->channel);
   }
   vmk_SPUnlock(&ctx->lock);
}

/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_IoatAddChannel --
 *
 *    Add a IOAT CB memcpy channel
 *
 * Results:
 *    status: DMA_ACK or DMA_NAK
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static enum dma_state_client
vmklnx_IoatAddChannel(struct dma_chan *chan)
{
   int i; 
   VMKLNX_DEBUG(0, "Adding I/OAT channel: %p", chan);
   for (i = 0; i < MAX_IOAT_CTX; i++) {
      vmklnx_IoatContext *ctx = &ioatctxList[i];
      vmk_SPLock(&ctx->lock);
      if (!ctx->channel) {
         VMK_ASSERT(ctx->free);
         ctx->channel = chan;
         vmk_SPUnlock(&ctx->lock);
         return DMA_ACK;
      }
      vmk_SPUnlock(&ctx->lock);
   }
   VMKLNX_DEBUG(0, "Too many channels: skipping");
   return DMA_NAK;
}

/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_IoatRemoveChannel --
 *
 *    Remove a IOAT CB memcpy channel
 *
 * Results:
 *    status: DMA_ACK or DMA_NAK.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static enum dma_state_client
vmklnx_IoatRemoveChannel(struct dma_chan *chan)
{
   int i;
   VMKLNX_DEBUG(0, "Removing I/OAT channel: %p", chan);
   for (i = 0; i < MAX_IOAT_CTX; i++) {
      vmklnx_IoatContext *ctx = &ioatctxList[i];
      vmk_SPLock(&ctx->lock);
      if (ctx->channel == chan) {
         VMK_ASSERT(ctx->free);
         ctx->channel = NULL;
         vmk_SPUnlock(&ctx->lock);
         return DMA_ACK;
      }
      vmk_SPUnlock(&ctx->lock);
   }
   return DMA_NAK;
}


static enum dma_state_client
vmklnx_IoatEvent(struct dma_client *client, struct dma_chan *chan, 
      enum dma_state event)
{
   enum dma_state_client status = DMA_NAK;
   VMK_ASSERT(dmaClient);
   switch (event) {
      case DMA_RESOURCE_AVAILABLE:
	 status = vmklnx_IoatAddChannel(chan);
         break;
       case DMA_RESOURCE_REMOVED:
         status = vmklnx_IoatRemoveChannel(chan);
         break;
      default:
         break;
   }
   return status;
}

void
IoatLinux_Init(void)
{
   int i;
   VMK_ReturnStatus status;
   
   for (i = 0; i < MAX_IOAT_CTX; i++) {
      char buf[128];
      vmklnx_IoatContext *ctx = &ioatctxList[i];

      snprintf(buf, sizeof(buf) - 1, "ioatctx[%d].lock", i);
      status = vmk_SPCreate(&ctx->lock, vmklinuxModID, buf, NULL,
                            SP_RANK_IOAT_CTX);

      VMK_ASSERT(status == VMK_OK); 
      ctx->free = VMK_TRUE;
      ctx->channel = NULL;
   }
}

/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_IoatRegister --
 *
 *    Register a DMA device with vmkernel making all its channel available
 *    for use for vmkernel
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    All device channels are claimed.
 *
 *----------------------------------------------------------------------------
 */

int
vmklnx_IoatRegister(struct vmk_ioatdevice *dev)
{
    VMKLNX_CREATE_LOG();
    if (dmaClient == NULL) {
      dmaClient = kmalloc(sizeof(*dmaClient), GFP_KERNEL);
      if (!dmaClient){
	 VMKLNX_WARN("I/OAT client allocation failed");  
	 return -1;
      }
      dmaClient->event_callback = vmklnx_IoatEvent;
      VMKAPI_MODULE_CALL_VOID(vmk_ModuleStackTop(), dev->client_register, 
                              dmaClient);
      
      VMK_ASSERT(dev->vmk_dmadevice);
      ioatFuncs.client_unregister = dev->client_unregister;
      ioatFuncs.dma_memcpy = dev->vmk_dmadevice->device_memcpy_mach_buf_to_buf;
      ioatFuncs.dma_flush = dev->vmk_dmadevice->device_issue_pending;
      ioatFuncs.dma_complete = dev->vmk_dmadevice->device_memcpy_complete; 
      
      dcaFuncs.dca_get_tag = dev->dca_get_tag;
      dcaFuncs.dca_add_requester = dev->dca_add_requester;
      dcaFuncs.dca_remove_requester = dev->dca_remove_requester;
   }

   /* TODO: request one channel, will investigate multi-channel usage*/
   dmaClient->chans_desired = 1;
   VMKAPI_MODULE_CALL_VOID(vmk_ModuleStackTop(), dev->client_chan_request, 
                           dmaClient);
   return 0;
}


/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_IoatCleanup --
 *
 *    Release all channels back to the driver (called when driver is unloading)
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

void
vmklnx_IoatCleanup(void)
{
   if (dmaClient) {
      /*
       * No need to call client_unregister,
       * ioat_dma_remove will free all the channels.
       */           
      kfree(dmaClient);
      dmaClient = NULL;

   }
   ioatFuncs.client_unregister = NULL;
   ioatFuncs.dma_memcpy = NULL;
   ioatFuncs.dma_flush = NULL;
   ioatFuncs.dma_complete = NULL;
   dcaFuncs.dca_get_tag = NULL;
   dcaFuncs.dca_add_requester = NULL;
   dcaFuncs.dca_remove_requester = NULL;
   VMKLNX_DESTROY_LOG();
}
