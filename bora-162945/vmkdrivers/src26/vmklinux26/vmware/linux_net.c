/* ****************************************************************
 * Portions Copyright 2007 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

/******************************************************************
 *
 *  linux_net.c
 *
 * From linux-2.6.24-7/include/linux/netdevice.h:
 *
 * Authors:     Ross Biro
 *              Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *              Corey Minyard <wf-rch!minyard@relay.EU.net>
 *              Donald J. Becker, <becker@cesdis.gsfc.nasa.gov>
 *              Alan Cox, <Alan.Cox@linux.org>
 *              Bjorn Ekwall. <bj0rn@blox.se>
 *              Pekka Riikonen <priikone@poseidon.pspt.fi>
 *
 * From linux-2.6.27-rc9/net/core/dev.c:
 *
 * Authors:     Ross Biro
 *              Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *              Mark Evans, <evansmp@uhura.aston.ac.uk>
 *
 * Additional Authors:
 *              Florian la Roche <rzsfl@rz.uni-sb.de>
 *              Alan Cox <gw4pts@gw4pts.ampr.org>
 *              David Hinds <dahinds@users.sourceforge.net>
 *              Alexey Kuznetsov <kuznet@ms2.inr.ac.ru>
 *              Adam Sulmicki <adam@cfar.umd.edu>
 *              Pekka Riikonen <priikone@poesidon.pspt.fi>
 *
 * From linux-2.6.27-rc9/net/sched/sch_generic.c:
 *
 * Authors:     Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *              Jamal Hadi Salim, <hadi@cyberus.ca> 990601
 *
 ******************************************************************/

#define NET_DRIVER      // Special case for Net portion of VMKLINUX

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/ethtool.h>
#include <linux/rtnetlink.h> /* BUG_TRAP */
#include <linux/workqueue.h>
#include <asm/uaccess.h>
#include <asm/page.h> /* phys_to_page */

#include "vmkapi.h"
#include "linux_stubs.h"
#include "linux_pci.h"
#include "linux_stress.h"
#include "linux_task.h"
#include "linux_net.h"

#define VMKLNX_LOG_HANDLE LinNet
#include "vmklinux26_log.h"

/* default watchdog timeout value and timer period for device */
#define WATCHDOG_DEF_TIMEO 5 * HZ
#define WATCHDOG_DEF_TIMER 1000

/* defined NAPI related thresholds */
#define NAPI_STACK_PUSH_WORK_MIN 2
#define NAPI_STACK_SKIP_PUSH_MAX 3
/* 
 * this value starts at 0x10000 as we don't want to collide with
 * the genCount used by vNICs port.
 */
#define PKT_COMPL_GEN_COUNT_INIT 0x10000

/* values for softq->state */
enum {
   LIN_NET_QUEUE_UNBLOCKED = 0x0001,   /* queue management via device block/unblock*/
   LIN_NET_QUEUE_STARTED   = 0x0002    /* queue management via the watchdog timer */
};

enum {
   LIN_NET_HARD_QUEUE_XOFF = 0x0001,   /* hardware queue is stopped */
};

/* values used to describe the state of a napi object during polling */
typedef enum {
   NAPI_NO_WORK = 0,         /* No work needs to be done for the napi context */
   NAPI_POLL_WORK = 1,       /* Polling needs to be done for the napi context */
   NAPI_STACK_PUSH_WORK = 2, /* Packets list needs to be flushed for the napi context */
} NapiPollState;

/*
 * NOTE: Try not to put any critical (data path) fields in LinNetDev.
 *       Instead, embed them in net_device, where they are next to
 *       their cache line brethrens.
 */ 
struct LinNetDev {
   unsigned int       napiNextId; /* Next unique id for napi context. */
   unsigned short     padded;     /* Padding added by alloc_netdev() */
   struct net_device  linNetDev __attribute__((aligned(NETDEV_ALIGN)));
   /* 
    * WARNING: linNetDev must be last because it is assumed that
    * private data area follows immediately after.
    */
};

typedef struct LinNetDev LinNetDev;
typedef int (*PollHandler) (void* clientData, vmk_uint32 vector);

#define get_LinNetDev(net_device)                                \
   ((LinNetDev*)(((char*)net_device)-(offsetof(struct LinNetDev, linNetDev))))

static vmk_Timer  devWatchdogTimer;
static void link_state_work_cb(struct work_struct_plus *work);
static void watchdog_work_cb(struct work_struct_plus *work);
static struct delayed_work linkStateWork = {
   .work  = __WORK_PLUS_INITIALIZER(linkStateWork.work, (0), new, link_state_work_cb),
   .timer = TIMER_INITIALIZER(NULL, 0, 0)
};
static struct delayed_work watchdogWork  = {
   .work  = __WORK_PLUS_INITIALIZER(watchdogWork.work, (0), new, watchdog_work_cb),
   .timer = TIMER_INITIALIZER(NULL, 0, 0)
};
static unsigned      linkStateTimerPeriod;
static vmk_ConfigParamHandle linkStateTimerPeriodConfigHandle;
static vmk_ConfigParamHandle maxNetifTxQueueLenConfigHandle;
static unsigned blockTotalSleepMsec;
static vmk_ConfigParamHandle blockTotalSleepMsecHandle;
struct net_device    *dev_base = NULL;
DEFINE_RWLOCK(dev_base_lock);
struct softnet_data  softnet_data[NR_CPUS] __cacheline_aligned;
int                  netdev_max_backlog = 300;
static const unsigned eth_crc32_poly_le = 0xedb88320;
static unsigned      eth_crc32_poly_tbl_le[256];
static uint64_t max_phys_addr;

static vmk_ConfigParamHandle useHwIPv6CsumHandle;
static vmk_ConfigParamHandle useHwCsumForIPv6CsumHandle;
static vmk_ConfigParamHandle useHwTSO6Handle;

/* The global socket buffer cache */
static kmem_cache_t *vmklnx_skb_cache = NULL;

/* The global generated counter for packet completion */
static vmk_PktCompletionData globalGenCount;

/* Stress option handles */
static vmk_StressOptionHandle stressNetGenTinyArpRarp;
static vmk_StressOptionHandle stressNetIfCorruptEthHdr;
static vmk_StressOptionHandle stressNetIfCorruptRxData;
static vmk_StressOptionHandle stressNetIfCorruptRxTcpUdp;
static vmk_StressOptionHandle stressNetIfCorruptTx;
static vmk_StressOptionHandle stressNetIfFailHardTx;
static vmk_StressOptionHandle stressNetIfFailRx;
static vmk_StressOptionHandle stressNetIfFailTxAndStopQueue;
static vmk_StressOptionHandle stressNetIfForceHighDMAOverflow;
static vmk_StressOptionHandle stressNetIfForceRxSWCsum;
static vmk_StressOptionHandle stressNetNapiForceBackupWorldlet;
static vmk_StressOptionHandle stressNetBlockDevIsSluggish;

/* LRO config option */
static vmk_ConfigParamHandle vmklnxLROEnabledConfigHandle;
static vmk_ConfigParamHandle vmklnxLROMaxAggrConfigHandle;
unsigned int vmklnxLROEnabled;
unsigned int vmklnxLROMaxAggr;

extern void LinStress_SetupStress(void);
extern void LinStress_CleanupStress(void);
extern void LinStress_CorruptSkbData(struct sk_buff*, unsigned int,
   unsigned int);
extern void LinStress_CorruptRxData(vmk_PktHandle*, struct sk_buff *);
extern void LinStress_CorruptEthHdr(struct sk_buff *skb);

static VMK_ReturnStatus map_pkt_to_skb(struct net_device *dev, 
                                       struct netdev_queue *queue,
                                       vmk_PktHandle *pkt, 
                                       struct sk_buff **pskb);
static void do_free_skb(struct sk_buff *skb);
static struct sk_buff *do_alloc_skb(kmem_cache_t *skb);
static VMK_ReturnStatus BlockNetDev(void *clientData);
static void SetNICLinkStatus(struct net_device *dev);

static inline VMK_ReturnStatus
marshall_from_vmknetq_id(vmk_NetqueueQueueId vmkqid, 
                         vmknetddi_queueops_queueid_t *qid);
static inline u16
get_embedded_queue_mapping(vmknetddi_queueops_queueid_t queueid);

/*
 * Section: Receive path
 */

/*
 *----------------------------------------------------------------------------
 *
 *  netif_rx_common --
 *
 *    Common receive path from napi and non-napi packet reception. Hands
 *    off received packet up the stack.
 *
 *  Results:
 *    NET_RX_SUCCESS on sccesss; NET_RX_DROP if the packet is dropped.
 *
 *  Side effects:
 *    Drops packet on the floor if unsuccessful.
 *
 *----------------------------------------------------------------------------
 */
static int
netif_rx_common(struct sk_buff *skb)
{
   vmk_PktHandle *pkt;
   struct net_device *dev = skb->dev;
   vmk_Bool needCompl = VMK_FALSE;

   if (unlikely(skb->len == 0)) {
      static uint32_t logThrottleCounter = 0;
      VMKLNX_THROTTLED_INFO(logThrottleCounter,
                            "dropping zero length packet "
                            "(skb->len=%u, skb->data_len=%u)\n",
                            skb->len, skb->data_len);
      goto drop;
   }

   /* we need to ensure the blocked status */
   if (unlikely(test_bit(__LINK_STATE_BLOCKED, &dev->state))) {
      goto drop;
   }
   atomic_inc(&dev->rxInFlight);

   pkt = skb->pkt;
   vmk_PktAdjust(pkt, skb_headroom(skb), skb_headlen(skb));
   
   if (skb_shinfo(skb)->frag_list != NULL) {
      VMK_ReturnStatus status = VMK_OK;
      struct sk_buff *frag_skb = skb_shinfo(skb)->frag_list;

      while (frag_skb) {
         status = vmk_PktAppend(pkt, frag_skb->pkt,
                                skb_headroom(frag_skb), skb_headlen(frag_skb));
         if (status != VMK_OK) {
            goto drop_dec_rx_inflight;
         }
         frag_skb = frag_skb->next;
      }
      
      vmk_PktSetCompletionData(pkt, skb, dev->genCount, VMK_TRUE);
      needCompl = VMK_TRUE;
   }
   
   if (unlikely(vmk_PktFrameLenSet(pkt, skb->len) != VMK_OK)) {
      printk("unable to set skb->pkt %p frame length with skb->len = %u\n", 
             pkt, skb->len);
      VMK_ASSERT(VMK_FALSE);
      goto drop_dec_rx_inflight;
   }
   
   /*
    * The following extracts vlan tag from skb.
    * The check just looks at a field of skb, so we
    * don't bother to check whether vlan is enabled.
    */
   if (vlan_rx_tag_present(skb)) {
      VMK_ASSERT(vmk_PktVlanIDGet(pkt) == 0);

      if  ((vlan_rx_tag_get(skb) & VLAN_VID_MASK) > VLAN_MAX_VALID_VID) {
         static uint32_t logThrottleCounter = 0;
         VMKLNX_THROTTLED_INFO(logThrottleCounter,
                               "invalid vlan tag: %d dropped",
                               vlan_rx_tag_get(skb) & VLAN_VID_MASK);
         goto drop_dec_rx_inflight;
      }
      vmk_PktVlanIDSet(pkt, vlan_rx_tag_get(skb) & VLAN_VID_MASK);
      vmk_PktPrioritySet(pkt,
                         (vlan_rx_tag_get(skb) & VLAN_1PTAG_MASK) >> VLAN_1PTAG_SHIFT);
      VMKLNX_DEBUG(2, "%s: vlan tag %u present with priority %u",
                   dev->name, vmk_PktVlanIDGet(pkt), vmk_PktPriorityGet(pkt));

#ifdef VMX86_DEBUG
      {
         // generate arp/rarp frames that are < ETH_MIN_FRAME_LEN to
         // create test cases for PR 106153.
	 struct ethhdr *eh = (struct ethhdr *)vmk_PktFrameMappedPointerGet(pkt);
                    
         if ((eh->h_proto == ntohs(ETH_P_ARP)
	      || eh->h_proto == ntohs(ETH_P_RARP))
	     && VMKLNX_STRESS_DEBUG_COUNTER(stressNetGenTinyArpRarp)) {
            int old_frameMappedLen;
            int target_len = (ETH_ZLEN - VLAN_HLEN);

	    old_frameMappedLen = vmk_PktFrameMappedLenGet(pkt);

            if (target_len <= old_frameMappedLen) {
               int old_len;
	       int len;

	       old_len = vmk_PktFrameLenGet(pkt);
               vmk_PktFrameLenSet(pkt, target_len);
	       len = vmk_PktFrameLenGet(pkt);
               VMKLNX_DEBUG(1, "shorten arp/rarp pkt to %d from %d",
                            len, old_len);
            }
         }
      }
#endif
   }

   if (skb->ip_summed != CHECKSUM_NONE &&
       !VMKLNX_STRESS_DEBUG_OPTION(stressNetIfForceRxSWCsum)) {
      vmk_PktSetCsumVfd(pkt);
   }

   if (VMKLNX_STRESS_DEBUG_COUNTER(stressNetIfCorruptRxTcpUdp)) {
      LinStress_CorruptSkbData(skb, 40, 14);
   }

   if (VMKLNX_STRESS_DEBUG_COUNTER(stressNetIfCorruptRxData)) {
      LinStress_CorruptRxData(pkt, skb);
   }

   if (VMKLNX_STRESS_DEBUG_COUNTER(stressNetIfCorruptEthHdr)) {
      LinStress_CorruptEthHdr(skb);
   }

   if (VMKLNX_STRESS_DEBUG_COUNTER(stressNetIfFailRx)) {
      VMKLNX_DEBUG(2,"Failing To Queue Packet: skb = %p, "
                   "pkt = %p, device = %s\n",
                   skb, pkt, dev->name);
   }

   dev->linnet_rx_packets++;

   if (!needCompl) {
      do_free_skb(skb);
      atomic_dec(&dev->rxInFlight);
   }

   return NET_RX_SUCCESS;

 drop_dec_rx_inflight:
   atomic_dec(&dev->rxInFlight);
   if (needCompl) {
      vmk_PktClearCompletionData(pkt);
   }
 drop:
   dev_kfree_skb_any(skb);
   dev->linnet_rx_dropped++;
   return NET_RX_DROP;
}

/**
 * netif_rx - post buffer to the network code
 * @skb: buffer to post
 *
 * This function receives a packet from a device driver and queues it for
 * the upper (protocol) levels to process.  It always succeeds. The buffer
 * may be dropped during processing for congestion control or by the
 * protocol layers.
 *
 *  RETURN VALUE:
 *  NET_RX_SUCCESS (no congestion)
 *  NET_RX_DROP	(packet was dropped)
 *
 */
/* _VMKLNX_CODECHECK_: netif_rx */
int
netif_rx(struct sk_buff *skb)
{
   struct net_device *dev = skb->dev;
   vmk_PktHandle *pkt;
   int status;

   VMK_ASSERT(dev);

   VMKLNX_DEBUG(1, "Napi is not enabled for device %s\n", dev->name);
   
   pkt = skb->pkt;
   VMK_ASSERT(pkt);

   status = netif_rx_common(skb);
   if (likely(status == NET_RX_SUCCESS)) {
      vmk_PktQueueForRxProcess(pkt, dev->uplinkDev);
   }

   return status;
}

/**
 * netif_receive_skb - process receive buffer from network
 * @skb: buffer to process
 *
 * netif_receive_skb() is the main receive data processing function.
 * It always succeeds. The buffer may be dropped during processing
 * for congestion control or by the protocol layers.
 *
 * ESX Deviation Notes:
 * This function may only be called from the napi poll callback routine.
 *
 *  RETURN VALUE:
 *  NET_RX_SUCCESS (no congestion)
 *  NET_RX_DROP	(packet was dropped)
 */
/* _VMKLNX_CODECHECK_: netif_receive_skb */
int
netif_receive_skb(struct sk_buff *skb)
{
   struct net_device *dev = skb->dev;
   vmk_Worldlet worldlet;
   struct napi_wdt_priv *wdtPriv;
   struct napi_struct *napi;
   vmk_PktHandle *pkt;   
   int status;
   
   VMK_ASSERT(dev);

   if (skb->napi == NULL) {
      if (unlikely(vmk_WorldletGetCurrent(&worldlet, (void **) &wdtPriv) != VMK_OK)) {
         if (unlikely(vmklnx_is_panic())) {
            napi = NULL;
            list_for_each_entry(napi, &dev->napi_list, dev_list) {
               if (napi != NULL) {
                  VMKLNX_DEBUG(1, "found a napi:%p.", napi);
                  break;
               }
            }
            VMK_ASSERT(napi != NULL);
            skb->napi = napi;
         } else {
            VMK_ASSERT(VMK_FALSE);
            dev_kfree_skb_any(skb);
            dev->linnet_rx_dropped++;
            status = NET_RX_DROP;
            goto done;
         }
      } else {
         VMK_ASSERT(wdtPriv != NULL && wdtPriv->napi != NULL);
         skb->napi = wdtPriv->napi;
      }
   }
   VMK_ASSERT(skb->napi != NULL);
   
   /*
    * check if the driver uses its own lro implementation or
    * if the skb has already been processed by the vmklinux
    * lro engine
    */
   if (!vmklnxLROEnabled ||
       (dev->features & NETIF_F_SW_LRO) ||
       skb->lro_ready) {
      
      pkt = skb->pkt;
      napi = skb->napi;
      
      status = netif_rx_common(skb);
      if (likely(status == NET_RX_SUCCESS)) {
         VMK_ASSERT(napi);
         VMK_ASSERT(pkt);
         vmk_PktListAddToTail(&napi->pktList, pkt);
      }
   } else {
      status = lro_receive_skb(&skb->napi->lro_mgr, skb, NULL);
   }
   
 done:
   return status;
}


/*
 *----------------------------------------------------------------------------
 *
 *  napi_poll_work_pending --
 *
 *    Check if any work is required for the napi context.
 *
 *  Results:
 *    NapiPollState describing the work to be done.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static inline NapiPollState
napi_poll_work_pending(struct napi_struct *napi, vmk_uint32 skippedPushCount)
{
   vmk_uint32 count = 0;
   NapiPollState result = NAPI_NO_WORK;

   VMK_ASSERT(napi);
   if (test_bit(NAPI_STATE_SCHED, &napi->state)) {
      result |= NAPI_POLL_WORK;
   }
   
   count = vmk_PktListCount(&napi->pktList);
   if (count > NAPI_STACK_PUSH_WORK_MIN) {
        /* If there are enough packets in the pktList, push them up */
   	result |= NAPI_STACK_PUSH_WORK;

   } else if (count > 0) {
	if (result & NAPI_POLL_WORK) {
	   /* If there are not enough packets in the pktList but we have skipped
            * pushing twice or packets are pending from the previous invocation,
            * push them now */
       	   if (skippedPushCount == 0 || skippedPushCount >  NAPI_STACK_SKIP_PUSH_MAX) {
	      result |= NAPI_STACK_PUSH_WORK;
	   } 
 	} else  {
	  /* If we are not polling again, push even if there are few packets */
      	   result |= NAPI_STACK_PUSH_WORK;
   	}
   }
   return result;
}

/*
 *----------------------------------------------------------------------------
 *
 *  napi_poll --
 *
 *    Worldlet handler dedicated for a napi context. This handler is
 *    responsible of polling a napi context and pushing the resulting
 *    packet list. 
 *
 *  Results:
 *    VMK_OK always.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
napi_poll(vmk_Worldlet worldlet,
          void *private,
          vmk_WorldletState *state)
{
   struct napi_wdt_priv * wdtPriv = private;
   struct napi_struct *napi;
   int status;
   vmk_Bool yield = VMK_TRUE;
   VMK_ReturnStatus ret;
   NapiPollState work;
   vmk_uint32 skippedPushCount = 0;

   VMK_ASSERT(wdtPriv);
   VMK_ASSERT(wdtPriv->napi);

   napi = wdtPriv->napi;
   work = napi_poll_work_pending(napi, skippedPushCount);
   while (1) {

      /* We favor looking at packets picked up from the driver but not
       * sent up the stack instead of new packets.  This creates back-pressure
       * to the driver and remote transmitters.
       */

      if (work & NAPI_STACK_PUSH_WORK) {
	 skippedPushCount = 1;
         /* netif_rx placed packets in napi->pktList */
         vmk_PktListRxProcess(&napi->pktList, napi->dev->uplinkDev);
      } else {
	 skippedPushCount++;
      }
      
      if (work & NAPI_POLL_WORK) {
         VMKAPI_MODULE_CALL(napi->dev->module_id, status, napi->poll, napi,
                            napi->weight);
         if (vmklnxLROEnabled && !(napi->dev->features & NETIF_F_SW_LRO)) {
            /* Flush all the lro sessions as we are done polling the napi context */
            lro_flush_all(&napi->lro_mgr);
         }
      }

      work = napi_poll_work_pending(napi, skippedPushCount);
      if (work == NAPI_NO_WORK) {
         /* We're done; suspend the worldlet.*/
         *state = VMK_WDT_SUSPEND;
         break;
      }

      ret = vmk_WorldletShouldYield(worldlet, &yield);
      if (ret != VMK_OK || yield == VMK_TRUE) {
         /* We should yield; resubmit the request. */
         *state = VMK_WDT_READY;
         break;
      }
   }

   return VMK_OK;
}

/*
 *----------------------------------------------------------------------------
 *
 *  netdev_poll --
 *
 *    Worldlet handler dedicated for napi context which were not able to create
 *    their own worldlet. This handler is called by the device worldlet, also called
 *    backup worldlet.
 *
 *    This handler is responsible of polling the different napi context and pushing 
 *    the resulting packet lists. 
 *
 *  Results:
 *    VMK_OK always.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netdev_poll(vmk_Worldlet worldlet,
            void *private,
            vmk_WorldletState *state)
{
   struct napi_wdt_priv *wdtPriv = private;
   struct net_device *dev;
   struct napi_struct *napi;
   int status;
   vmk_Bool yield = VMK_TRUE;
   VMK_ReturnStatus ret;
   NapiPollState work;
   vmk_Bool needWork;

   VMK_ASSERT(wdtPriv);
   VMK_ASSERT(wdtPriv->dev);

   dev = wdtPriv->dev;

   while (VMK_TRUE) {
      
      needWork = VMK_FALSE;      
      work = NAPI_NO_WORK;

      spin_lock(&dev->napi_lock);
      list_for_each_entry(napi, &dev->napi_list, dev_list) {             
         if (napi->dev_poll &&
             ((work = napi_poll_work_pending(napi, 0)) != NAPI_NO_WORK)) {
            needWork = VMK_TRUE;
            list_move_tail(&napi->dev_list, &dev->napi_list);           
            break;
         }
      }
      spin_unlock(&dev->napi_lock);
      
      if (!needWork) {
         break;
      }

      wdtPriv->napi = napi;
      /* We favor looking at packets picked up from the driver but not
       * sent up the stack instead of new packets.  This creates back-pressure
       * to the driver and remote transmitters.
       */
      
      if (work & NAPI_STACK_PUSH_WORK) {
         /* netif_rx placed packets in napi->pktList */
         vmk_PktListRxProcess(&napi->pktList, napi->dev->uplinkDev);
      }
      
      ret = vmk_WorldletShouldYield(worldlet, &yield);
      if (ret != VMK_OK || yield == VMK_TRUE) {
         goto abort_yield;
      }
      
      if (work & NAPI_POLL_WORK) {
         VMKAPI_MODULE_CALL(napi->dev->module_id, status, napi->poll, napi,
                            napi->weight);
         if (vmklnxLROEnabled && !(napi->dev->features & NETIF_F_SW_LRO)) {
            /* Flush all the lro sessions as we are done polling the napi context */
            lro_flush_all(&napi->lro_mgr);
         }
      }
      
      ret = vmk_WorldletShouldYield(worldlet, &yield);
      if (ret != VMK_OK || yield == VMK_TRUE) {
         goto abort_yield;
      }
   }

   /* We're done; suspend the worldlet.*/
   *state = VMK_WDT_SUSPEND;

 done:
   wdtPriv->napi = NULL;
   return VMK_OK;
   
  abort_yield:
   /* Resubmit the request. */
   *state = VMK_WDT_READY;
   goto done;
}

/*
 *----------------------------------------------------------------------------
 *
 *  napi_poll_init --
 *
 *    Initialize a napi context . If the function is unable to create a unique
 *    worldlet it will attach the napi context to the one provided by the device
 *    it belongs to.
 *
 *  Results:
 *    VMK_OK always.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */   
static VMK_ReturnStatus
napi_poll_init(struct napi_struct *napi)
{
   VMK_ReturnStatus ret;
   vmk_ServiceAcctId serviceID;

   VMK_ASSERT(napi);
   vmk_PktListInit(&napi->pktList);

   spin_lock(&napi->dev->napi_lock);
   napi->napi_id = get_LinNetDev(napi->dev)->napiNextId++;
   spin_unlock(&napi->dev->napi_lock);

   ret = vmk_ServiceGetID("netdev", &serviceID);
   VMK_ASSERT(ret == VMK_OK);

   napi->napi_wdt_priv.dev = napi->dev;
   napi->napi_wdt_priv.napi = napi;
   napi->dev_poll = VMK_FALSE;
   napi->vector = 0;

   ret = vmk_WorldletCreate(&napi->worldlet,
                            "",
                            serviceID,
                            vmklinuxModID,
                            napi_poll, &napi->napi_wdt_priv);
   if (ret != VMK_OK) {
      VMKLNX_WARN("Unable to create napi worldlet for %s, using backup\n",
                  napi->dev->name);
      if (napi->dev->reg_state == NETREG_REGISTERED) {
         napi->worldlet = napi->dev->napi_worldlet;

         /*
          * Use device global worldlet for polling this napi_struct,
          * if worldlet creation fails
          */
         napi->dev_poll = VMK_TRUE;
      } else {
         napi->dev->reg_state = NETREG_EARLY_NAPI_ADD_FAILED;
         return VMK_FAILURE;
      }
   }

   spin_lock(&napi->dev->napi_lock);
   list_add(&napi->dev_list, &napi->dev->napi_list);   
   spin_unlock(&napi->dev->napi_lock);

   /*
    * Keep track of which worldlet is (most probably) driving the
    * default queue. For netqueue capable nics, we call 
    * VMKNETDDI_QUEUEOPS_OP_GET_DEFAULT_QUEUE to figure out the
    * default worldlet. For non-netqueue nics, the first suceessful
    * netif_napi_add wins.
    */
   if (!napi->dev->default_worldlet && napi->worldlet) {
      napi->dev->default_worldlet = napi->worldlet;
   }

   return VMK_OK;
}

/*
 *----------------------------------------------------------------------------
 *
 *  netdev_poll_init --
 *
 *    Initialize a device's backup worldlet for the napi context not able to
 *    create their own. 
 *
 *  Results:
 *    VMK_OK if everything is ok, VMK_* otherwise.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netdev_poll_init(struct net_device *dev)
{
   VMK_ReturnStatus ret;
   vmk_ServiceAcctId serviceID;
   
   VMK_ASSERT(dev);

   ret = vmk_ServiceGetID("netdev", &serviceID);
   VMK_ASSERT(ret == VMK_OK);

   dev->napi_wdt_priv.dev = dev;
   dev->napi_wdt_priv.napi = NULL;

   ret = vmk_WorldletCreate(&dev->napi_worldlet, 
                            "",
                            serviceID,
                            vmklinuxModID,
                            netdev_poll, &dev->napi_wdt_priv);
   return ret;
}

/*
 *----------------------------------------------------------------------------
 *
 *  netdev_set_worldlet_name --
 *
 *    Initialize a device's worldlet name.
 *
 *  Results:
 *    VMK_OK if everything is ok, VMK_* otherwise.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netdev_set_worldlets_name(struct net_device *dev)
{
   VMK_ReturnStatus status;
   struct napi_struct *napi;
   char name[VMK_WDT_NAME_SIZE_MAX];

   VMK_ASSERT(dev);

   snprintf(name, VMK_WDT_NAME_SIZE_MAX, "%s-napi-backup", dev->name);   
   status = vmk_WorldletNameSet(dev->napi_worldlet, name);
   VMK_ASSERT(status == VMK_OK);

   spin_lock(&dev->napi_lock);
   list_for_each_entry(napi, &dev->napi_list, dev_list) {             
      snprintf(name, VMK_WDT_NAME_SIZE_MAX, "%s-napi-%d",
               napi->dev->name, napi->napi_id);
      status = vmk_WorldletNameSet(napi->worldlet, name);
      VMK_ASSERT(status == VMK_OK);
   }
   spin_unlock(&dev->napi_lock);

   return VMK_OK;
}

/*
 *----------------------------------------------------------------------------
 *
 *  napi_poll_cleanup --
 *
 *    Cleanup a napi structure.
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static void
napi_poll_cleanup(struct napi_struct *napi)
{
   VMK_ASSERT(napi);
   VMK_ASSERT(vmk_PktListIsEmpty(&napi->pktList));
   if (likely(!napi->dev_poll)) {
      if (napi->vector) {
         vmk_WorldletVectorUnSet(napi->worldlet);
         napi->vector = 0;
      }
      vmk_WorldletUnref(napi->worldlet);
   }
   list_del(&napi->dev_list);
}

/*
 *----------------------------------------------------------------------------
 *
 *  netdev_poll_cleanup --
 *
 *    Cleanup all napi structures associated with the device.
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static void
netdev_poll_cleanup(struct net_device *dev)
{
   VMK_ASSERT(dev);   
   struct list_head *ele, *next;
   struct napi_struct *napi;

   /*
    * Cleanup all napi structs
    */
   list_for_each_safe(ele, next, &dev->napi_list) {
      napi = list_entry(ele, struct napi_struct, dev_list);
      napi_poll_cleanup(napi);
   }

   if (dev->napi_worldlet) {
      vmk_WorldletUnref(dev->napi_worldlet);
   }
}

/**
 * __napi_schedule - schedule for receive
 * @napi: entry to schedule
 *
 * The entry's receive function will be scheduled to run
 *
 * RETURN VALUE:
 * None
 */
/* _VMKLNX_CODECHECK_: __napi_schedule */
void 
__napi_schedule(struct napi_struct *napi)
{
   vmk_uint32 myVector = 0;
   vmk_Bool inIntr = vmk_ContextIsInterruptHandler(&myVector);
   VMK_ASSERT(napi);

   if (unlikely(napi->vector != myVector)) {
      if (likely(inIntr)) {
         vmk_WorldletVectorSet(napi->worldlet, myVector);
         napi->vector = myVector;
      }
   }

   vmk_WorldletActivate(napi->worldlet);
}

/**
 *      napi_disable - prevent NAPI from scheduling
 *      @napi: napi context
 *
 * Stop NAPI from being scheduled on this context.
 * Waits till any outstanding processing completes.
 *
 * RETURN VALUE:
 * None
 */
/* _VMKLNX_CODECHECK_: napi_disable */
void
napi_disable(struct napi_struct *napi)
{
   vmk_WorldletState state;
   VMK_ReturnStatus ret;

   VMK_ASSERT(napi);

   set_bit(NAPI_STATE_DISABLE, &napi->state);
   while (1) {
      ret = vmk_WorldletCheckState(napi->worldlet, &state);
      VMK_ASSERT(ret == VMK_OK);

      /* If the worldlet isn't running/set to run, then we see if we can
       * disable it from running in the future by blocking off
       * NAPI_STATE_SCHED.
       */
      if (state == VMK_WDT_SUSPEND  && 
          !test_and_set_bit(NAPI_STATE_SCHED, &napi->state)) {
         VMK_DEBUG_ONLY(
            ret = vmk_WorldletCheckState(napi->worldlet, &state);
            VMK_ASSERT(state == VMK_WDT_SUSPEND);
            );
         break;
      }
      /**
       * Give the flush a chance to run.
       */
      schedule_timeout_interruptible(1);
   }

   clear_bit(NAPI_STATE_DISABLE, &napi->state);
}

/**
 * netif_napi_add - initialize a napi context
 * @dev:  network device
 * @napi: napi context
 * @poll: polling function
 * @weight: default weight
 *
 * netif_napi_add() must be used to initialize a napi context prior to calling
 * *any* of the other napi related functions.
 *
 * RETURN VALUE:
 * None
 */
/* _VMKLNX_CODECHECK_: netif_napi_add */
void 
netif_napi_add(struct net_device *dev,
               struct napi_struct *napi,
               int (*poll)(struct napi_struct *, int),
               int weight)
{
        struct net_lro_mgr *lro_mgr;

        napi->poll = poll;
        napi->weight = weight;
        napi->dev = dev;

        lro_mgr = &napi->lro_mgr;
        lro_mgr->dev = dev;
        lro_mgr->features = LRO_F_NAPI;
        lro_mgr->ip_summed = CHECKSUM_UNNECESSARY;
        lro_mgr->ip_summed_aggr = CHECKSUM_UNNECESSARY;
        lro_mgr->max_desc = LRO_DEFAULT_MAX_DESC;
        lro_mgr->lro_arr = napi->lro_desc;
        lro_mgr->get_skb_header = vmklnx_net_lro_get_skb_header;
        lro_mgr->get_frag_header = NULL;
        lro_mgr->max_aggr = vmklnxLROMaxAggr;
        lro_mgr->frag_align_pad = 0;

        napi_poll_init(napi);

        set_bit(NAPI_STATE_SCHED, &napi->state); 
}

/**
 *  netif_napi_del - remove a napi context
 *  @napi: napi context
 *
 *  netif_napi_del() removes a napi context from the network device napi list
 *
 * RETURN VALUE:
 * None
 */
/* _VMKLNX_CODECHECK_: netif_napi_del */
void 
netif_napi_del(struct napi_struct *napi)
{
   napi_poll_cleanup(napi);
}

/**
 *      napi_enable - enable NAPI scheduling
 *      @n: napi context
 *
 * Resume NAPI from being scheduled on this context.
 * Must be paired with napi_disable.
 * 
 * RETURN VALUE:
 * None
 */
/* _VMKLNX_CODECHECK_: napi_enable */
void
napi_enable(struct napi_struct *napi)
{
   struct net_lro_mgr *lro_mgr;
   int idx;

   BUG_ON(!test_bit(NAPI_STATE_SCHED, &napi->state));

   lro_mgr = &napi->lro_mgr;
   for (idx = 0; idx < lro_mgr->max_desc; idx++) {
      memset(&napi->lro_desc[idx], 0, sizeof(struct net_lro_desc));
   }

   smp_mb__before_clear_bit();
   clear_bit(NAPI_STATE_SCHED, &napi->state);  
}


/*
 * Section: Skb helpers
 */

/*
 *----------------------------------------------------------------------------
 *
 *  do_init_skb_bits --
 *
 *    Initialize a socket buffer.
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static inline void
do_init_skb_bits(struct sk_buff *skb, kmem_cache_t *cache)
{
   skb->qid = VMKNETDDI_QUEUEOPS_INVALID_QUEUEID;
   skb->next = NULL;
   skb->prev = NULL;
   skb->head = NULL;
   skb->data = NULL;
   skb->tail = NULL;
   skb->end = NULL;
   skb->dev = NULL;
   skb->pkt = NULL;
   atomic_set(&skb->users, 1);
   skb->cache = cache;
   skb->mhead = 0;
   skb->len = 0;
   skb->data_len = 0;
   skb->ip_summed = CHECKSUM_NONE;
   skb->csum = 0;
   skb->priority = 0;
   skb->protocol = 0;
   skb->truesize = 0;
   skb->mac.raw = NULL;
   skb->nh.raw = NULL;
   skb->h.raw = NULL;
   skb->napi = NULL;
   skb->lro_ready = 0;

   /* VLAN_RX_SKB_CB shares the same space so this is sufficient */
   VLAN_TX_SKB_CB(skb)->magic = 0;
   VLAN_TX_SKB_CB(skb)->vlan_tag = 0;

   atomic_set(&(skb_shinfo(skb)->dataref), 1);
   atomic_set(&(skb_shinfo(skb)->fragsref), 1);
   skb_shinfo(skb)->nr_frags = 0;
   skb_shinfo(skb)->frag_list = NULL;
   skb_shinfo(skb)->gso_size = 0;
   skb_shinfo(skb)->gso_segs = 0;
   skb_shinfo(skb)->gso_type = 0;
   skb_shinfo(skb)->ip6_frag_id = 0;
}

/*
 *----------------------------------------------------------------------------
 *
 *  do_bind_skb_to_pkt --
 *
 *    Bind a socket buffer to a packet handle.
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static inline void
do_bind_skb_to_pkt(struct sk_buff *skb, vmk_PktHandle *pkt, unsigned int size)
{
   vmk_PktFrag frag;

   if (vmk_PktFragGet(pkt, &frag, 0) != VMK_OK) {
      VMK_ASSERT(0);
      return;
   }

   skb->pkt = pkt;
   skb->head = phys_to_virt(frag.addr);
   skb->end = skb->head + size;
   skb->data = skb->head;
   skb->tail = skb->head;

#ifdef VMX86_DEBUG
   /* Added to assert earlier to catch PR 62560 */
   virt_to_phys(skb->tail);

   /*
    * linux guarantees physical contiguity of the pages backing
    * skb's returned by this routine, so the drivers assume that.
    * we guarantee this by backing the buffers returned from 
    * vmk_PktAlloc with a large-page, low-memory heap which is
    * guaranteed to be physically contiguous, so we just double 
    * check it here.
    */
   {
      vmk_Bool isFlat;
      
      isFlat = vmk_PktIsFlatBuffer(pkt);
      VMK_ASSERT(isFlat);
   }  
#endif // VMX86_DEBUG
}  

/*
 *----------------------------------------------------------------------------
 *
 *  do_alloc_skb --
 *
 *    Allocate a socket buffer.
 *
 *  Results:
 *    A pointer to the allocated socket buffer.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static struct sk_buff *
do_alloc_skb(kmem_cache_t *cache)
{
   struct sk_buff *skb;

   if (!cache) {
      if (vmklnx_skb_cache) {    
         cache = vmklnx_skb_cache;
      } else {
         return NULL;
      }
   }

   skb = vmklnx_kmem_cache_alloc(cache);
   if (unlikely(skb == NULL)) {
      return NULL;
   }

   do_init_skb_bits(skb, cache);
   return skb;
}

/*
 *----------------------------------------------------------------------------
 *
 *  vmklnx_net_alloc_skb --
 *
 *    Allocate a socket buffer for a specified size and bind it to a packet handle.
 *
 *  Results:
 *    A pointer the allocated socket buffer.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
struct sk_buff *
vmklnx_net_alloc_skb(kmem_cache_t *cache, unsigned int size)
{
   vmk_PktHandle *pkt;
   struct sk_buff *skb;

   skb = do_alloc_skb(cache);

   if (unlikely(skb == NULL)) {
      goto done;
   }

   vmk_PktAlloc(size, &pkt);      
  
   if (unlikely(pkt == NULL)) {
      do_free_skb(skb);
      skb = NULL;
      goto done;
   }

   do_bind_skb_to_pkt(skb, pkt, size);

 done:   
   return skb;
}

/*
 *----------------------------------------------------------------------------
 *
 *  vmklnx_net_skb_complete --
 *
 *    Completion handler for packet coming from vmklinux and requiring it.
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
vmklnx_net_skb_complete(void *reserved,
                        vmk_UplinkCompletionData data,
                        vmk_PktList *pktList)
{
   vmk_PktHandle *pkt;
   struct sk_buff *skb;
   struct sk_buff *skbToRelease;
   struct sk_buff *next_skb;
   struct net_device *dev = data;
   vmk_PktCompletionData genCount;

   while (!vmk_PktListIsEmpty(pktList)) {
      pkt = vmk_PktListPopHead(pktList);
      
      vmk_PktGetCompletionData(pkt, 
                               (vmk_PktCompletionData *)&skb, &genCount);

      /* Check if the packet really belongs to this device */
      if (likely(genCount == dev->genCount)) {
         skbToRelease = skb_shinfo(skb)->frag_list;
         while (skbToRelease) {
            next_skb = skbToRelease->next;         
            vmk_PktReleaseAfterComplete(skbToRelease->pkt);
            do_free_skb(skbToRelease);
            skbToRelease = next_skb;
         }

         dev->linnet_pkt_completed++;
         atomic_dec(&dev->rxInFlight);
         vmk_PktClearCompletionData(pkt);      
         vmk_PktReleaseAfterComplete(pkt);
         do_free_skb(skb);
      } else {
         VMKLNX_WARN("Orphan packet genCount=%p instead of %p\n",
                     genCount, dev->genCount);
         vmk_PktClearCompletionData(pkt);      
         vmk_PktReleaseAfterComplete(pkt);
      }
   }

   VMK_ASSERT(atomic_read(&dev->rxInFlight) >= 0);
   return VMK_OK;
}

/*
 *----------------------------------------------------------------------------
 *
 *  do_free_skb --
 *
 *    Release socket buffer.
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static void
do_free_skb(struct sk_buff *skb)
{
   vmklnx_kmem_cache_free(skb->cache, skb);
}

/**
 *  __kfree_skb - private function
 *  @skb: buffer
 *
 *  Free an sk_buff. Release anything attached to the buffer.
 *  Clean the state. This is an internal helper function. Users should
 *  always call kfree_skb
 *
 * RETURN VALUE:
 * None
 */
/* _VMKLNX_CODECHECK_: __kfree_skb */
void
__kfree_skb(struct sk_buff *skb)
{
   if (unlikely(!atomic_dec_and_test(&skb->users))) {
      return;
   }

   skb_release_data(skb);   
   do_free_skb(skb);
}

/*
 *----------------------------------------------------------------------------
 *
 *  skb_debug_info -- 
 *      Debug function to print contents of a socket buffer.
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *----------------------------------------------------------------------------
 */
void
skb_debug_info(struct sk_buff *skb)
{
   int f;
   skb_frag_t *frag;

   printk(KERN_ERR "skb\n"
          "   head     <%p>\n"
          "   mhead    <%u>\n"
          "   data     <%p>\n"
          "   tail     <%p>\n"
          "   end      <%p>\n"
          "   data_len <%u>\n"
          "   nr_frags <%u>\n"
          "   dataref  <%u>\n"
          "   gso_size <%u>\n",
          skb->head, skb->mhead,
          skb->data, skb->tail, skb->end,
          skb->data_len,
          skb_shinfo(skb)->nr_frags,
          atomic_read(&(skb_shinfo(skb)->dataref)),
          skb_shinfo(skb)->gso_size);

   for (f = 0; f < skb_shinfo(skb)->nr_frags; f++) {
      frag = &skb_shinfo(skb)->frags[f];
      printk(KERN_ERR "skb frag %d\n"
             "   page         <0x%llx>\n"
             "   page_offset  <%u>\n"
             "   size         <%u>\n",
             f, page_to_phys(frag->page),
             frag->page_offset, frag->size);
   }
}


/*
 * Section: Transmit path
 */

/*
 *----------------------------------------------------------------------------
 *
 *  process_tx_queue --
 *
 *    Transmit queued packets.  The following coordination discipline is 
 *    is used to determine when it is safe to safe to avoid calling 
 *    netif_schedule, which is expensive.  Conceptually, when 
 *    process_tx_queue fails to process all the pkts on dev->outputList,
 *    netif_schedule is needed to try again later.  However, the following
 *    two most common situations are "optimized" to avoid that.  
 *     (a) netif_queue_stopped.
 *         If netif_queue_stopped is true, netif_wake_queue is responsible
 *         for calling netif_schedule.  To avoid race where pkts are left
 *         unprocessed on dev->outputList, the check for netif_queue_stopped
 *         is done while holding dev->queueLock.  Note that the processing of 
 *         dev->outputList triggered by 
 *             netif_wake_queue->netif_schedule->LinNetProcessTxQueue
 *         cannot finish until it takes dev->queue_lock.  
 *     (b) another thread is also in LinNetProcessTxQueue, has taken xmit_lock,
 *         and will be looping back to process dev->outputList.
 *         We introduce a varaible in dev's structure "processing_tx"
 *         which is used to indicate whether a thread is in LinNetProcessTxQueue
 *         for the device.  This is only written to (set/clear) while holding
 *         dev->xmit_lock.  Furthermore, it is only checked while holding 
 *         dev->queueLock.  The thread that has set dev->processing_tx
 *         guanratees that it will be taking dev->queueLock, and then check
 *         for pkts in dev->outputList after getting that lock.  
 *    The synchronization handshake here is conservative in that it may in
 *    some cases call netif_schedule unnecessarily, but it ensures that 
 *    pkts are not left stranded on dev->outputList.
 *
 *   Results:
 *    Returns 1 if rescheduled; 0 if all packets were drained.
 *
 *   Side effects:
 *    May return list of packets that need to be freed. We can't free packets 
 *    while holding locks because of opportunistic polling in the completion 
 *    path.
 *
 *----------------------------------------------------------------------------
 */
static int
process_tx_queue(struct netdev_queue *queue, vmk_PktList *freePktsList)
{
   vmk_PktHandle *pkt;
   struct sk_buff *skb;
   uint32_t iter = 0;
   struct net_device *dev = queue->dev;
   struct netdev_soft_queue *softq = &queue->softq;
   vmk_PktList *pktList = &softq->outputList;

   VMK_ASSERT(spin_is_locked(&softq->queue_lock));

   if (VMKLNX_STRESS_DEBUG_COUNTER(stressNetIfFailTxAndStopQueue)) {
      netif_tx_stop_queue(queue);
   }

   while (!vmk_PktListIsEmpty(pktList) && (iter < softq->outputListMaxSize)) {
      int xmit_status = -1;

      VMK_ASSERT(dev->flags & IFF_UP);
      
      if (VMKLNX_STRESS_DEBUG_COUNTER(stressNetIfFailHardTx)) {
         pkt = vmk_PktListPopHead(pktList);
         VMK_ASSERT(pkt);
         VMKLNX_DEBUG(1, "Failing Hard Transmit. pkt = %p, device = %s\n", 
                      pkt, dev->name);
         vmk_PktListAddToTail(freePktsList, pkt);
         continue;
      }  
      
      /*
       * Try to Tx packet
       */
      if (!spin_trylock(&queue->_xmit_lock)) {
         if (queue->processing_tx) {
            /* 
             * Another xmit in progress. Give up cpu without rescheduling.
             */
            VMKLNX_DEBUG(2, "didn't get _xmit_lock; done_unfinished");
            goto done_unfinished;
         } else {
            // must conservatively reschedule.
            VMKLNX_DEBUG(2, "didn't get xmit_lock; reschedule");
            goto reschedule;
         }  
      }
      
      queue->processing_tx = 1;
      
      if (unlikely(netif_tx_queue_stopped(queue))) {
         queue->processing_tx = 0;
         spin_unlock(&queue->_xmit_lock);
         /*
          *  Rescheduling will be handled by netif_wake_queue;
          *  race with rescheduled invocation of LinNetProcessTxQueue is
          *  prevented by softq->queue_lock.
          */
         VMKLNX_DEBUG(3, "queue stopped;");
         goto done_unfinished;
      }
      
      pkt = vmk_PktListPopHead(pktList);
      VMK_ASSERT(pkt);

      /* 
       * Release queue and call driver.
       * Note: 
       *  1) We give up queue lock because we'll like to reduce cpu burn when 
       *     multiple threads are trying to transmit.
       *
       *  2) queue_lock can't be given up before xmit_lock has been acquired
       *     because then you have a race that can reorder packets.
       */
      spin_unlock(&softq->queue_lock);
      
      /*
       * Map packets to skbs
       *
       * XXX: Revisit batching pkt->skb mapping (on whole or partial list) 
       *      before we dunk into the driver
       */
      if (unlikely(map_pkt_to_skb(dev, queue, pkt, &skb) != VMK_OK)) {
         VMKLNX_DEBUG(0, "%s: Unable to map packet to skb. Dropping", 
                      dev->name);
         vmk_PktListAddToTail(freePktsList, pkt);
         queue->processing_tx = 0;
	 spin_unlock(&queue->_xmit_lock);
         spin_lock(&softq->queue_lock);
         continue;
      }

      VMKAPI_MODULE_CALL(dev->module_id, xmit_status, 
                         *dev->hard_start_xmit, skb, dev);
      
      queue->processing_tx = 0;
      spin_unlock(&queue->_xmit_lock);
      spin_lock(&softq->queue_lock);
      
      if (xmit_status != 0) {
         VMKLNX_DEBUG(1, "hard_start_xmit failed (status %d; Q stopped %d. "
	              "Queuing packet. pkt=%p dev=%s\n", 
	              xmit_status, netif_tx_queue_stopped(queue),
                      skb->pkt, dev->name);

         /* destroy skb and its resources besides the packet handle itself. */
         atomic_inc(&(skb_shinfo(skb)->fragsref));
         dev_kfree_skb_any(skb);
         
         /*
          * sticking pkt back this way may cause tx re-ordering, 
          * but this should be very rare.
          */
         vmk_PktListAddToHead(pktList, pkt);
         if (netif_tx_queue_stopped(queue)) {
            // netif_wake_queue will trigger subsequent xmit
            VMKLNX_DEBUG(3, "queue still stopped.");
            goto done_unfinished;
         } else {
            /*
             * rare, but can happen if wakeup happen concurrently on either
             * another PCPU or on same PCPU in an interrupt context.
             */
            VMKLNX_DEBUG(2, "queue no longer stopped; reschedule");
            goto reschedule;
         }
      }
      
      dev->linnet_tx_packets++;
      iter++;
   }
   
   if (!vmk_PktListIsEmpty(pktList)) {
      __netif_schedule(queue);
   }

   return 0;

 reschedule:
   __netif_schedule(queue);
 done_unfinished:
   return 1;
}

/*
 *----------------------------------------------------------------------------
 *
 *  net_tx_action -- 
 *
 *      Delayed/rescheduled transmit, called from a softirq context.
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *----------------------------------------------------------------------------
 */
static void
net_tx_action(struct softirq_action *h)
{
   vmk_PktList freeList;
   struct softnet_data *sd = &softnet_data[smp_processor_id()];

   vmk_PktListInit(&freeList);

   if (sd->output_queue) {
      struct netdev_queue *head;

      local_irq_disable();
      head = sd->output_queue;
      sd->output_queue = NULL;
      local_irq_enable();

      while (head) {
         vmk_uint32 pktsCount;
         struct netdev_queue *q = head;
         struct netdev_soft_queue *sq = &q->softq;

         head = head->next_sched;

         smp_mb__before_clear_bit();
         clear_bit(__QUEUE_STATE_SCHED, &q->state);

         if (spin_trylock(&sq->queue_lock)) {
            process_tx_queue(q, &freeList);
            spin_unlock(&sq->queue_lock);

	    pktsCount = vmk_PktListCount(&freeList);
            if (pktsCount) {
               q->dev->linnet_tx_dropped += pktsCount;
               vmk_PktListReleasePkts(&freeList);
            }
         } else {
            /*
             * Don't need need to do anything.  
             * Whoever is holding the queue_lock is 
             * responsible for making sure pkts on
             * outputList are processed eventually.
             */
            VMKLNX_DEBUG(3, "*NO* reschedule for %s; #pkts %d", 
                         q->dev->name, 
                         vmk_PktListCount(&sq->outputList));
         }
      }
   }
}

/*
 *----------------------------------------------------------------------------
 *
 *  ipv6_set_hraw --
 *
 *    Parse an IPv6 skb to find the appropriate value for initializing 
 *    skb->h.raw.  If skb->h.raw is initialized, also sets *protocol to 
 *    the last nexthdr found.
 *
 *   Results:
 *    None
 *
 *   Side effects:
 *    None
 *
 *----------------------------------------------------------------------------
 */
static void
ipv6_set_hraw(struct sk_buff *skb, vmk_uint8 *protocol)
{
   vmk_uint8 nextHdr = skb->nh.ipv6h->nexthdr;
   vmk_uint8 *nextHdrPtr = (vmk_uint8 *) (skb->nh.ipv6h + 1);

   if (nextHdrPtr > skb->end) {
      // this happens if the source doesn't take care to map entire header
      return;
      
   }
   // take care of most common situation:
   if ((nextHdr == IPPROTO_TCP)
       || (nextHdr == IPPROTO_UDP)
       || (nextHdr == IPPROTO_ICMPV6)) {
      skb->h.raw = nextHdrPtr;
      (*protocol) = nextHdr;
      return;
   } 

   /*
    * This will be the value if "end" not found within
    *   linear region.
    */
   VMK_ASSERT(skb->h.raw == NULL);
   do {
      switch (nextHdr) {
      case IPPROTO_ROUTING:
      case IPPROTO_HOPOPTS:
      case IPPROTO_DSTOPTS:
         // continue searching
         nextHdr = *nextHdrPtr;
         nextHdrPtr += nextHdrPtr[1] * 8 + 8;
         break;

      case IPPROTO_AH:
         // continue searching
         nextHdr = *nextHdrPtr;
         nextHdrPtr += nextHdrPtr[1] * 4 + 8;
         break;

      /*
       * We do NOT handle the IPPROTO_FRAGMENT case here. Thus,
       * if any packet has a IPv6 fragment header, this function
       * will return protocol == IPPROTO_FRAGMENT and *not*
       * find the L4 protocol. As the returned protocol is only 
       * used for TSO and CSUM cases, and a fragment header is 
       * not allowed in either case, this behavior is desirable,
       * as it allows handling this case in the caller.
       */

      default:
         // not recursing
         skb->h.raw = nextHdrPtr;
         (*protocol) = nextHdr;
         return;
         break;
      }
   } while (nextHdrPtr < skb->end);
}

/*
 *----------------------------------------------------------------------------
 *
 *  map_pkt_to_skb --
 *
 *    Converts PktHandle to sk_buff before handing packet to linux driver.
 *
 *   Results:
 *    Returns VMK_ReturnStatus
 *
 *   Side effects:
 *    This is ugly. Too much memory writes / packet. We should look at
 *    optimizing this. Maybe an skb cache or something, instead of 
 *    having to touch 20+ variables for each packet.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
map_pkt_to_skb(struct net_device *dev,
               struct netdev_queue *queue,
               vmk_PktHandle *pkt,
               struct sk_buff **pskb)
{
   struct sk_buff *skb;
   int i;
   vmk_uint16 sgInspected;
   unsigned int headLen, bytesLeft;
   vmk_uint32 frameLen;
   VMK_ReturnStatus ret = VMK_OK;
   vmk_PktFrag frag;
   vmk_VirtAddr frameMapped;
   vmk_Bool must_vlantag, must_tso, must_csum, pkt_ipv4;
   vmk_uint8 protocol;
   struct ethhdr *eh;
   unsigned short ehLen;

   skb = do_alloc_skb(dev->skb_pool);
   
   if (unlikely(skb == NULL)) {
      ret = VMK_NO_MEMORY;
      goto done;
   }

   skb->pkt = pkt;
   skb->queue_mapping = queue - dev->_tx;

   VMK_ASSERT(dev);
   VMK_ASSERT(pkt);
#ifdef VMX86_DEBUG
 {
    vmk_Bool consistent;
    
    consistent = vmk_PktCheckInternalConsistency(pkt);
    VMK_ASSERT(consistent);
 }
#endif

   if (vmk_PktFragGet(pkt, &frag, 0) != VMK_OK) {
      ret = VMK_FAILURE;
      goto done;
   }
   skb->head = phys_to_virt(frag.addr);
   
   frameLen = vmk_PktFrameLenGet(pkt);

   skb->len = frameLen;
   skb->dev = dev;
   skb->data = skb->head;

   frameMapped = vmk_PktFrameMappedPointerGet(pkt);

   eh = (struct ethhdr *) frameMapped;   
   ehLen = eth_header_len(eh);
   skb->protocol = eth_header_frame_type(eh);
   skb->mac.raw = skb->data;   
   skb->nh.raw = skb->mac.raw + ehLen;
   sgInspected = 1;
   headLen = min(frag.length, frameLen);
   skb->end = skb->tail = skb->head + headLen;

   if (eth_header_is_ipv4(eh)) {
      skb->h.raw = skb->nh.raw + skb->nh.iph->ihl*4;
      pkt_ipv4 = VMK_TRUE;
      protocol = skb->nh.iph->protocol;
   } else {
      pkt_ipv4 = VMK_FALSE;
      protocol = 0xff;  // unused value.
      if (skb->protocol == ntohs(ETH_P_IPV6)) {
         ipv6_set_hraw(skb, &protocol);
      }
   }

   VMKLNX_DEBUG(10, "head: %u bytes at MA 0x%"VMK_FMT64"x",
                headLen, frag.addr);
   /*
    * See if the packet requires VLAN tagging
    */

   must_vlantag = vmk_PktMustVlanTag(pkt);

   if (must_vlantag) { 
      vmk_VlanID vlanID;
      vmk_VlanPriority priority;

      vlanID = vmk_PktVlanIDGet(pkt);
      priority = vmk_PktPriorityGet(pkt);

      vlan_put_tag(skb, vlanID | (priority << VLAN_1PTAG_SHIFT));
   }
      
   /*
    * See if the packet requires checksum offloading or TSO
    */

   must_tso = vmk_PktIsLargeTcpPacket(pkt);
   must_csum = vmk_PktIsMustCsum(pkt);

   if (must_tso) {
      vmk_uint32 tsoMss = vmk_PktGetLargeTcpPacketMss(pkt);
      unsigned short inetHdrLen;

      /*
       * backends should check the tsoMss before setting MUST_TSO flag
       */
      VMK_ASSERT(tsoMss);

      if (!pkt_ipv4 &&
          (skb->protocol != ntohs(ETH_P_IPV6))) {
         static uint32_t throttle = 0;
         VMKLNX_THROTTLED_WARN(throttle,
                               "%s: non-ip packet with TSO (proto=0x%x)",
                               dev->name,
                               skb->protocol);
         ret = VMK_FAILURE;
         goto done;
      }
          
      if (!skb->h.raw ||
          (protocol != IPPROTO_TCP)) {
         /*
          * This check will also trigger for IPv6 packets that
          * have a fragment header, as ipv6_set_hraw() sets protocol
          * to IPPROTO_FRAGMENT.
          */
         static uint32_t throttle = 0;
         VMKLNX_THROTTLED_WARN(throttle,
                               "%s: non-tcp packet with TSO (ip%s, proto=0x%x, hraw=%p)",
                               dev->name,
                               pkt_ipv4 ? "v4" : "v6",
                               protocol, skb->h.raw);
         ret = VMK_FAILURE;
         goto done;
      }
           
      /*
       * Perform some sanity checks on TSO frames, because buggy and/or
       * malicious guests might generate invalid packets which may wedge
       * the physical hardware if we let them through.
       */
      inetHdrLen = (skb->h.raw + tcp_hdrlen(skb)) - skb->nh.raw;

      // Reject if the frame doesn't require TSO in the first place
      if (unlikely(frameLen - ehLen - inetHdrLen <= tsoMss)) {
         static uint32_t throttle = 0;
         VMKLNX_THROTTLED_WARN(throttle, 
                               "%s: runt TSO packet (tsoMss=%d, frameLen=%d)",
                               dev->name, tsoMss, frameLen);
         ret = VMK_FAILURE;
         goto done;
      }

      // Reject if segmented frame will exceed MTU
      if (unlikely(tsoMss + inetHdrLen > dev->mtu)) {
         static uint32_t logThrottleCounter = 0;
         VMKLNX_THROTTLED_WARN(logThrottleCounter,
                               "%s: oversized tsoMss: %d, mtu=%d",
                               dev->name, tsoMss, dev->mtu);
         ret = VMK_FAILURE;
         goto done;
      }

      skb_shinfo(skb)->gso_size = tsoMss;
      skb_shinfo(skb)->gso_segs = (skb->len + tsoMss - 1) / tsoMss;
      skb_shinfo(skb)->gso_type = pkt_ipv4 ? SKB_GSO_TCPV4 : SKB_GSO_TCPV6;

      /* 
       * If congestion window has been reduced due to the 
       *  previous TCP segment 
       */
      if (unlikely(skb->h.th->cwr == 1)) {
         skb_shinfo(skb)->gso_type |= SKB_GSO_TCP_ECN;
      }
   } else {
      /*
       * We are dropping packets that are larger than the MTU of the NIC
       * since they could potentially wedge the NIC or PSOD in the driver.
       */
      if (unlikely(frameLen - ehLen > dev->mtu)) {
         static uint32_t linuxTxWarnCounter;
         VMKLNX_THROTTLED_WARN(linuxTxWarnCounter,
                               "%s: %d bytes packet couldn't be sent (mtu=%d)",
                               dev->name, frameLen, dev->mtu);
         ret = VMK_FAILURE;
         goto done;
      }
   }

   if (must_csum || must_tso) {
      VMK_ASSERT(skb->h.raw);

      switch (protocol) {
            
      case IPPROTO_TCP:
         skb->csum = 16;
         skb->ip_summed = CHECKSUM_HW;
         break;
            
      case IPPROTO_UDP:
         skb->csum = 6;
         skb->ip_summed = CHECKSUM_HW;
         break;
            
      /*
       * XXX add cases for other protos once we use NETIF_F_HW_CSUM
       * in some device.  I think the e1000 can do it, but the Intel
       * driver doesn't advertise so.
       */
            
      default:
         VMKLNX_DEBUG(0, "%s: guest driver requested xsum offload on "
                      "unsupported type %d", 
                      dev->name, skb->nh.iph->protocol);
         skb->ip_summed = CHECKSUM_NONE;
      }

   } else {
      skb->ip_summed = CHECKSUM_NONE; // XXX: for now
   }
   
   bytesLeft = frameLen - headLen;
   for (i = sgInspected; bytesLeft > 0; i++) {
      skb_frag_t *skb_frag;
    
      if (unlikely(i - sgInspected >= MAX_SKB_FRAGS)) {
         static uint32_t fragsThrottleCounter = 0;
         VMKLNX_THROTTLED_INFO(fragsThrottleCounter,
		               "too many frags (> %u) bytesLeft %d", 
                               MAX_SKB_FRAGS, bytesLeft);
#ifdef VMX86_DEBUG
	 VMK_ASSERT(VMK_FALSE);
#endif
    	 ret = VMK_FAILURE;
         goto done;
      }

      if (vmk_PktFragGet(pkt, &frag, i) != VMK_OK) {
    	 ret = VMK_FAILURE;
         goto done;
      }
      skb_frag = &skb_shinfo(skb)->frags[i - sgInspected];
      /* Going to use the frag->page to store page number and 
         frag->page_offset for offset within that page */
      skb_frag->page = phys_to_page(frag.addr);
      skb_frag->page_offset = offset_in_page(frag.addr);
      skb_frag->size = min(frag.length, bytesLeft);
      VMKLNX_DEBUG(10, "frag: %u bytes at MA 0x%llx", 
                   skb_frag->size, page_to_phys(skb_frag->page) + skb_frag->page_offset);
      skb->data_len += skb_frag->size;
      bytesLeft -= skb_frag->size;
      skb_shinfo(skb)->nr_frags++;
		 
      vmk_AssertMemorySupportsIO(frag.addr, frag.length);
   }

   if (VMKLNX_STRESS_DEBUG_COUNTER(stressNetIfCorruptTx)) {
      LinStress_CorruptSkbData(skb, 60, 0);
   }

 done:

   if ((ret != VMK_OK) && (skb != NULL)) {
      do_free_skb(skb);
      skb = NULL;
   }

   *pskb = skb;

   return ret;
}

/*
 *----------------------------------------------------------------------------
 *
 * netdev_queue_tx_pkts_locked --
 *
 *    Queue Tx packets
 *
 * Results:
 *    VMK_ReturnStatus
 *
 * Side effects:
 *    pktList is not guaranteed to be empty on error. Caller must clear
 *    pktList on eror. We do this because vmk_PktListReleasePkts can't be 
 *    called with locks held.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netdev_queue_tx_pkts_locked(struct netdev_queue *queue, vmk_PktList *pktList)
{
   VMK_ReturnStatus ret = VMK_OK;
   vmk_uint32 outputListCount, pktListCount;
   struct netdev_soft_queue *softq = &queue->softq;
   vmk_PktList *outputList = &softq->outputList;

   VMK_ASSERT(spin_is_locked(&softq->queue_lock));

   outputListCount = vmk_PktListCount(outputList);
   pktListCount = vmk_PktListCount(pktList);
   
   if (likely(pktListCount + outputListCount < softq->outputListMaxSize)) {
      vmk_PktListJoin(outputList, pktList);
   } else {
      int listSpaceLeft = softq->outputListMaxSize - outputListCount;
      /*
       * The count of outputList may exceed the configured max by 1.
       * This happens when transmit loop drops the queue_lock while
       * waiting for hardware, the outputList can now grow to the
       * configured max. The hardware transmit fails and the failed
       * pkt is added back to the outputList. See process_tx_queue
       */
      if (listSpaceLeft > 0) {
         vmk_PktListAppendN(outputList, pktList, listSpaceLeft);
      }
      ret = VMK_NO_RESOURCES;
   }

   return ret;
}

/*
 *----------------------------------------------------------------------------
 *
 * netdev_pick_tx_queue --
 *
 *    Pick device tx subqueue for transmission. The upper layers must ensure
 *    that all packets in pktList are destined for the same queue.
 *
 * Results:
 *    pointer to netdev_queue
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static inline struct netdev_queue *
netdev_pick_tx_queue(struct net_device *dev, vmk_PktList *pktList)
{
   int queue_idx = 0;
   vmknetddi_queueops_queueid_t queueid = VMKNETDDI_QUEUEOPS_INVALID_QUEUEID;
   VMK_ReturnStatus status;
   vmk_PktHandle *pkt = vmk_PktListGetHead(pktList);
   vmk_NetqueueQueueId vmkqid;

   VMK_ASSERT(pkt);
   vmkqid = vmk_PktQueueIdGet(pkt);
   if (!vmkqid) {
      goto out;
   }

#ifdef VMX86_DEBUG
   {
      while (pkt != NULL) {
         pkt = vmk_PktListGetNext(pktList, pkt);
         if (!pkt) {
            break;
         }
         VMK_ASSERT(vmk_PktQueueIdGet(pkt) == vmkqid);
      }
   }
#endif

   status = marshall_from_vmknetq_id(vmkqid, &queueid);
   VMK_ASSERT(status == VMK_OK);
   if (status == VMK_OK) {
      queue_idx = get_embedded_queue_mapping(queueid);
      if (queue_idx >= dev->real_num_tx_queues ||
          queue_idx >= dev->num_tx_queues) {
         queue_idx = 0;
      }
   }

 out:
   VMK_ASSERT(queue_idx < dev->num_tx_queues);
   VMK_ASSERT(queue_idx >= 0);
   return &dev->_tx[queue_idx];
}

/*
 *----------------------------------------------------------------------------
 *
 * netdev_tx --
 *
 *    Transmit packets
 *
 * Results:
 *    VMK_ReturnStatus indicating the outcome.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netdev_tx(struct net_device *dev, vmk_PktList *pktList)
{
   VMK_ReturnStatus ret;
   vmk_PktList freeList;
   vmk_uint32 pktsCount;
   struct netdev_queue *queue;
   struct netdev_soft_queue *softq;

   if (unlikely(test_bit(__LINK_STATE_BLOCKED, &dev->state)) ||
       VMKLNX_STRESS_DEBUG_COUNTER(stressNetIfFailHardTx)) {
      vmk_PktListReleasePkts(pktList);
      return VMK_NO_RESOURCES;
   }
   
   vmk_PktListInit(&freeList);

   queue = netdev_pick_tx_queue(dev, pktList);
   VMK_ASSERT(queue);
   softq = &queue->softq;

   /*
    * Queue them
    */
   spin_lock(&softq->queue_lock);
   if (softq->state != (LIN_NET_QUEUE_UNBLOCKED|LIN_NET_QUEUE_STARTED)) {
      ret = VMK_FAILURE;
      goto out_unlock;
   }

   ret = netdev_queue_tx_pkts_locked(queue, pktList);

   /*
    * Transmit now or later
    */
   process_tx_queue(queue, &freeList);

 out_unlock:
   spin_unlock(&softq->queue_lock);

   /*
    * Free whatever is left on pktList
    */
   pktsCount = vmk_PktListCount(pktList);
   if (unlikely(pktsCount)) {
      VMK_ASSERT(ret != VMK_OK);
      dev->linnet_tx_dropped += pktsCount;
      vmk_PktListReleasePkts(pktList);
   }

   /*
    * Free whatever could not be txed
    */
   pktsCount = vmk_PktListCount(&freeList);
   if (unlikely(pktsCount)) {
      dev->linnet_tx_dropped += pktsCount;
      vmk_PktListReleasePkts(&freeList);
   }

   return ret;
}

/*
 * Section: Control operations and queue management
 */

/*
 *----------------------------------------------------------------------------
 *
 * unblock_tx_soft_queue --
 *
 *    Unblock device transmit soft queue.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static void
unblock_tx_soft_queue(struct netdev_queue *queue)
{
   struct netdev_soft_queue *softq = &queue->softq;

   spin_lock(&softq->queue_lock);
   softq->state |= LIN_NET_QUEUE_UNBLOCKED;
   spin_unlock(&softq->queue_lock);
}

/*
 *----------------------------------------------------------------------------
 *
 * block_tx_soft_queue --
 *
 *    Block device transmit soft queue.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Frees up any queued packets.
 *
 *----------------------------------------------------------------------------
 */
static void
block_tx_soft_queue(struct netdev_queue *queue)
{
   vmk_PktList freeList;
   vmk_uint32 pktsCount;
   struct netdev_soft_queue *softq = &queue->softq;

   vmk_PktListInit(&freeList);

   /*
    * Remove packets waiting to be txed
    */
   spin_lock(&softq->queue_lock);
   vmk_PktListJoin(&freeList, &softq->outputList);
   softq->state &= ~LIN_NET_QUEUE_UNBLOCKED;
   spin_unlock(&softq->queue_lock);

   pktsCount = vmk_PktListCount(&freeList);

   /*
    * Go free packets
    */
   vmk_PktListReleasePkts(&freeList);
}

/*
 *----------------------------------------------------------------------------
 *
 * start_tx_soft_queue --
 *
 *    Start (previously stopped) transmit soft queue. Queues are stopped
 *    and started by the watchdog timer.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static void
start_tx_soft_queue(struct netdev_queue *queue)
{
   struct netdev_soft_queue *softq = &queue->softq;

   spin_lock(&softq->queue_lock);
   if (!(softq->state & LIN_NET_QUEUE_STARTED)) {
      softq->state |= LIN_NET_QUEUE_STARTED;
   }
   spin_unlock(&softq->queue_lock);
}

/*
 *----------------------------------------------------------------------------
 *
 * stop_tx_soft_queue --
 *
 *    Stop transmit soft queue. Queues are stopped and started by the watchdog 
 *    timer.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static void
stop_tx_soft_queue(struct netdev_queue *queue)
{
   vmk_PktList freeList;
   vmk_uint32 pktsCount;
   struct netdev_soft_queue *softq = &queue->softq;

   vmk_PktListInit(&freeList);

   /*
    * Remove packets waiting to be txed
    */
   spin_lock(&softq->queue_lock);
   vmk_PktListJoin(&freeList, &softq->outputList);
   softq->state &= ~LIN_NET_QUEUE_STARTED;
   spin_unlock(&softq->queue_lock);

   pktsCount = vmk_PktListCount(&freeList);

   /*
    * Go free packets
    */
   vmk_PktListReleasePkts(&freeList);
}

/*
 *----------------------------------------------------------------------------
 *
 * start_all_tx_soft_queues --
 *
 *    Start all transmit soft queues.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static void
start_all_tx_soft_queues(struct net_device *dev)
{
   unsigned int i;
   for (i = 0; i < dev->num_tx_queues; i++) {
      struct netdev_queue *queue = &dev->_tx[i]; 
      start_tx_soft_queue(queue);
   }
}

/*
 *----------------------------------------------------------------------------
 *
 * stop_all_tx_soft_queues --
 *
 *    Stop all transmit soft queues
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static void
stop_all_tx_soft_queues(struct net_device *dev)
{
   unsigned int i;
   for (i = 0; i < dev->num_tx_queues; i++) {
      struct netdev_queue *queue = &dev->_tx[i]; 
      stop_tx_soft_queue(queue);
   }
}

/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_netif_start_tx_queue --
 *
 *    queue started.
 *
 *    XXX: dummy state stored for now. proper implementation will propogate
 *    queue state all the way up to the uplink layer.
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
vmklnx_netif_start_tx_queue(struct netdev_queue *queue)
{
   struct netdev_soft_queue *softq = &queue->softq;
   softq->hardState &= ~LIN_NET_HARD_QUEUE_XOFF;   
}


/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_netif_stop_tx_queue --
 *
 *    queue stopped
 *
 *    XXX: dummy state stored for now. proper implementation will propogate
 *    queue state all the way up to the uplink layer.
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
vmklnx_netif_stop_tx_queue(struct netdev_queue *queue)
{
   struct netdev_soft_queue *softq = &queue->softq;
   softq->hardState |= LIN_NET_HARD_QUEUE_XOFF;   
}

/**
 * dev_close - shutdown an interface.
 * @dev: device to shutdown
 *
 * This function moves an active device into down state. The device's 
 * private close function is invoked. 
 *
 * ESX Deviation Notes:
 *  netdev notifier chain is not called.
 *
 * RETURN VALUE:
 *  0
 */
/* _VMKLNX_CODECHECK_: dev_close */
int
dev_close(struct net_device *dev)
{
   unsigned int i;

   ASSERT_RTNL();

   BlockNetDev(dev);
   
#ifdef VMX86_DEBUG
   {
      VMK_ASSERT(atomic_read(&dev->rxInFlight) == 0);
      VMK_ASSERT(test_bit(__LINK_STATE_BLOCKED, &dev->state));
      VMK_ASSERT(test_bit(__LINK_STATE_START, &dev->state));
      VMK_ASSERT(dev->flags & IFF_UP);
   }
#endif

   for (i = 0; i < dev->num_tx_queues; i++) {
      struct netdev_queue *queue = &dev->_tx[i]; 
      spin_unlock_wait(&queue->_xmit_lock);
   }
   
   clear_bit(__LINK_STATE_START, &dev->state);  
   smp_mb__after_clear_bit(); /* Commit netif_running(). */

   if (dev->stop) {
      VMKLNX_DEBUG(0, "Calling device stop %p", dev->stop);
      VMKAPI_MODULE_CALL_VOID(dev->module_id, dev->stop, dev);
      VMKLNX_DEBUG(0, "Device stopped");
   }

   dev->flags &= ~IFF_UP;

   return 0;
}

/*
 *----------------------------------------------------------------------------
 *
 * init_watchdog_timeo --
 *
 *    Init watchdog timeout
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static void
init_watchdog_timeo(struct net_device *dev)
{
   if (dev->tx_timeout
       && dev->watchdog_timeo <= 0) {
      dev->watchdog_timeo = WATCHDOG_DEF_TIMEO;
   }
}

/**
 *  dev_open	- prepare an interface for use.
 *  @dev:	device to open
 *
 *  Takes a device from down to up state. The device's private open
 *  function is invoked. 
 *
 * ESX Deviation Notes:
 *  Device's notifier chain is not called. 
 *  Device is put in promiscuous mode after it is opened.
 *
 *  Calling this function on an active interface is a nop. On a failure
 *  a negative errno code is returned.
 *
 * RETURN VALUE:
 *  0 on success
 *  negative error code returned by the device on error
 *  
 */
/* _VMKLNX_CODECHECK_: dev_open */
int
dev_open(struct net_device *dev)
{
   int ret = 0;

   ASSERT_RTNL();

   if (dev->flags & IFF_UP) {
      return 0;
   }

   set_bit(__LINK_STATE_START, &dev->state);
   if (dev->open) {
      VMKAPI_MODULE_CALL(dev->module_id, ret, dev->open, dev);
      if (ret == 0) {
         VMKLNX_DEBUG(0, "%s opened successfully\n", dev->name);
	 
	 init_watchdog_timeo(dev);

         /* block by default. we'll unblock it when we're ready */
         BlockNetDev(dev);

         dev->flags |= (IFF_UP | IFF_PROMISC);
	 if (dev->set_multicast_list) {
	    VMKAPI_MODULE_CALL_VOID(dev->module_id, 
                                    dev->set_multicast_list, 
                                    dev);
	 }
      } else {
         clear_bit(__LINK_STATE_START, &dev->state);
      }
   }

   return ret;
}

/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_free_netdev
 *
 *    Internal implementation of free_netdev, frees net_device and associated 
 *    structures. Exposed verion of free_netdev is an inline because it
 *    touches driver private data structs.
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
vmklnx_free_netdev(struct vmklnx_pm_skb_cache *pmCache, struct net_device *dev)
{
   unsigned long flags;
   LinNetDev *linDev = get_LinNetDev(dev);

   if (dev->skb_pool) {
      spin_lock_irqsave(&pmCache->lock, flags);
      VMK_ASSERT(pmCache->count > 0);
      VMK_ASSERT(dev->skb_pool == pmCache->cache);
      if (--pmCache->count == 0) {
         vmklnx_kmem_cache_destroy(pmCache->cache);
         pmCache->cache = NULL;
         dev->skb_pool = NULL;
      }
      spin_unlock_irqrestore(&pmCache->lock, flags);
   }

   kfree(dev->_tx);
   kfree((char *)linDev - linDev->padded);
}

static void
netdev_init_one_queue(struct net_device *dev,
                      struct netdev_queue *queue,
                      void *_unused)
{
   queue->dev = dev;
}

static void 
netdev_init_queues(struct net_device *dev)
{
   netdev_for_each_tx_queue(dev, netdev_init_one_queue, NULL);
}

/**
 * alloc_netdev_mq - allocate network device
 * @sizeof_priv:	size of private data to allocate space for
 * @name:		device name format string
 * @setup:		callback to initialize device
 * @queue_count:	the number of subqueues to allocate
 *
 * Allocates a struct net_device with private data area for driver use
 * and performs basic initialization.  Also allocates subquue structs
 * for each queue on the device at the end of the netdevice.
 *
 * RETURN VALUE:
 *  Pointer to allocated struct net_device on success
 *  %NULL on error
 */
/* _VMKLNX_CODECHECK_: alloc_netdev_mq */
struct net_device *
alloc_netdev_mq(int sizeof_priv, 
                const char *name,
                void (*setup)(struct net_device *), 
                unsigned int queue_count)
{
   LinNetDev *linDev;
   struct netdev_queue *tx;
   struct net_device *dev;
   int alloc_size;
   void *p;

   BUG_ON(strlen(name) >= sizeof(dev->name));

   alloc_size = sizeof(struct LinNetDev);

   if (sizeof_priv) {
      /* ensure 32-byte alignment of private area */
      alloc_size = (alloc_size + NETDEV_ALIGN_CONST) & ~NETDEV_ALIGN_CONST;
      alloc_size += sizeof_priv;
   }

   /* ensure 32-byte alignment of whole construct */
   alloc_size += NETDEV_ALIGN_CONST;

   p = kzalloc(alloc_size, GFP_KERNEL);
   if (!p) {
      printk(KERN_ERR "alloc_netdev: Unable to allocate device.\n");
      return NULL;
   }

   tx = kzalloc(sizeof(struct netdev_queue) * queue_count, GFP_KERNEL);
   if (!tx) {
      printk(KERN_ERR "alloc_netdev: Unable to allocate "
             "tx qdiscs.\n");
      kfree(p);
      return NULL;
   }

   linDev = (LinNetDev *)
      (((long)p + NETDEV_ALIGN_CONST) & ~NETDEV_ALIGN_CONST);
   linDev->padded = (char *)linDev - (char *)p;

   dev = &linDev->linNetDev;
   dev->_tx = tx;
   dev->num_tx_queues = queue_count;
   dev->real_num_tx_queues = queue_count;

   if (sizeof_priv) {
      dev->priv = ((char *)dev +
                   ((sizeof(struct net_device) + NETDEV_ALIGN_CONST)
                    & ~NETDEV_ALIGN_CONST));
   }

   netdev_init_queues(dev);

   dev->module_id = vmk_ModuleStackTop();
   INIT_LIST_HEAD(&dev->napi_list);
   spin_lock_init(&dev->napi_lock);
   atomic_set(&dev->rxInFlight, 0);
   set_bit(__NETQUEUE_STATE, (void*)&dev->netq_state);

   VMKAPI_MODULE_CALL_VOID(dev->module_id, setup, dev);
   strcpy(dev->name, name);

   return dev;
}

#ifndef ARPHRD_ETHER
#define ARPHRD_ETHER  1       /* Ethernet 10Mbps.  */
#endif

/**
 *  ether_setup - setup the given Ethernet network device
 *  @dev: network device
 *                                           
 *  Initializes fields of the given network device with Ethernet-generic
 *  values
 *                                           
 *  ESX Deviation Notes:                     
 *  This function does not initialize any function pointers in the
 *  given net_device
 *
 *  RETURN VALUE:
 *  This function does not return a value
 */
/* _VMKLNX_CODECHECK_: ether_setup */
void
ether_setup(struct net_device *dev)
{
   dev->type		= ARPHRD_ETHER;
   dev->hard_header_len = ETH_HLEN; /* XXX should this include 802.1pq? */
   dev->mtu		= ETH_DATA_LEN; /* eth_mtu */
   dev->addr_len	= ETH_ALEN;
   /* XXX */
   dev->tx_queue_len	= 100;	/* Ethernet wants good queues */

   memset(dev->broadcast, 0xFF, ETH_ALEN);

   dev->flags		= IFF_BROADCAST|IFF_MULTICAST;
}

/*
 *----------------------------------------------------------------------------
 *
 * vmklnx_alloc_etherdev_mq --
 *
 *    Internal implementation of alloc_etherdev_mq, Allocates an ethernet "type"
 *    net_device. Exposed verion of alloc_etherdev_mq is an inline because it
 *    touches driver private data structs.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
struct net_device *
vmklnx_alloc_etherdev_mq(struct vmklnx_pm_skb_cache *pmCache,
                         vmk_HeapID skbCacheHeap,
                         int sizeof_priv,
                         unsigned int queue_count)
{
   struct net_device *dev;
   unsigned long flags;

   dev = alloc_netdev_mq(sizeof_priv, "vmnic%d", ether_setup, queue_count);
   if (!dev) {
      goto done;
   }

   spin_lock_irqsave(&pmCache->lock, flags);
   if (pmCache->count != 0) {
      VMK_ASSERT(pmCache->count > 0);
      VMK_ASSERT(pmCache->cache != NULL);
      dev->skb_pool = pmCache->cache;
      ++pmCache->count;
      spin_unlock_irqrestore(&pmCache->lock, flags);
   } else {
      dev->skb_pool = vmklnx_kmem_cache_create(skbCacheHeap, 
                                               "skb_cache",
                                               sizeof(struct sk_buff) + 
                                               sizeof(struct skb_shared_info),
                                               0, 
                                               NULL, 
                                               NULL, 
                                               1, 
                                               100);
      if (!dev->skb_pool) {
         spin_unlock_irqrestore(&pmCache->lock, flags);
         vmk_WarningMessage("socket buffer cache creation failed for %s\n", 
                            dev->name);
      } else {
         pmCache->cache = dev->skb_pool;
         pmCache->count = 1;
         spin_unlock_irqrestore(&pmCache->lock, flags);
         vmk_LogMessage("socket buffer cache creation succeeded for %s\n", 
                        dev->name);
      }
   }

 done:
   return dev;
}

/**
 * netif_device_attach - mark device as attached
 * @dev: network device
 *
 * Mark device as attached from system and restart if needed.
 *
 * RETURN VALUE:
 *  None
 */
/* _VMKLNX_CODECHECK_: netif_device_attach */
void 
netif_device_attach(struct net_device *dev)
{
	if (!test_and_set_bit(__LINK_STATE_PRESENT, &dev->state) &&
	    netif_running(dev)) {
                netif_tx_wake_all_queues(dev);
 		__netdev_watchdog_up(dev);
	}
}

/**
 * netif_device_detach - mark device as removed
 * @dev: network device
 *
 * Mark device as removed from system and therefore no longer available.
 * 
 * RETURN VALUE:
 *  None
 */
/* _VMKLNX_CODECHECK_: netif_device_detach */
void 
netif_device_detach(struct net_device *dev)
{
	if (test_and_clear_bit(__LINK_STATE_PRESENT, &dev->state) &&
	    netif_running(dev)) {
                netif_tx_stop_all_queues(dev);
	}
}

static void 
__netdev_init_queue_locks_one(struct net_device *dev,
                              struct netdev_queue *queue,
                              void *_unused)
{
   VMK_ReturnStatus status;
   struct netdev_soft_queue *softq = &queue->softq;

   spin_lock_init(&queue->_xmit_lock);
   queue->xmit_lock_owner = -1;
   queue->processing_tx = 0;

   spin_lock_init(&softq->queue_lock);
   softq->state = 0;
   vmk_PktListInit(&softq->outputList);
   status = vmk_ConfigParamGetUint(maxNetifTxQueueLenConfigHandle, 
                                   &softq->outputListMaxSize);
   VMK_ASSERT(status == VMK_OK);
}

/*
 *----------------------------------------------------------------------------
 *
 * netdev_init_queue_locks --
 *
 *    Init device queues locks.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static void 
netdev_init_queue_locks(struct net_device *dev)
{
   netdev_for_each_tx_queue(dev, __netdev_init_queue_locks_one, NULL);
}

/**
 * register_netdevice	- register a network device
 * @dev: device to register
 *
 * Take a completed network device structure and add it to the kernel
 * interfaces. 0 is returned on success. A negative errno code is returned
 * on a failure to set up the device, or if the name is a duplicate.
 *
 * RETURN VALUE:
 *  0 on success
 *  negative errno code on error
 */
/* _VMKLNX_CODECHECK_: register_netdevice */
int
register_netdevice(struct net_device *dev)
{
   int ret = 0;

   /*
    * netif_napi_add can be called before register_netdev, unfortunately. 
    * fail register_netdev, if the prior napi_add had failed. it's most
    * likely a low memory condition and we'll fail somewhere further down
    * the line if we go on.
    */
   if (dev->reg_state == NETREG_EARLY_NAPI_ADD_FAILED) {
      VMKLNX_WARN("%s: early napi registration failed, bailing\n", dev->name);
      ret = -EIO;
      goto out;
   }
 
   netdev_init_queue_locks(dev);
   dev->iflink = -1;
   dev->vlan_group = NULL;

   /* Init, if this function is available */
   int rv = 0;
   if (dev->init != 0) {
      VMKAPI_MODULE_CALL(dev->module_id, rv, dev->init, dev);
      if (rv != 0) {
         ret = -EIO;
         goto out;
      }
   }

   if (netdev_poll_init(dev) != VMK_OK) {      
      ret = -ENOMEM;
      goto err_uninit;
   }

   set_bit(__LINK_STATE_PRESENT, &dev->state);

   write_lock(&dev_base_lock);
   dev->next = dev_base;
   dev_base = dev;
   dev->genCount = globalGenCount++;
   if (globalGenCount < (vmk_PktCompletionData) PKT_COMPL_GEN_COUNT_INIT) {
      globalGenCount = (vmk_PktCompletionData) PKT_COMPL_GEN_COUNT_INIT;
   }
   write_unlock(&dev_base_lock);

   dev_hold(dev);
   dev->reg_state = NETREG_REGISTERED;

 out:
   return ret;

 err_uninit:
   if (dev->uninit) {
      VMKAPI_MODULE_CALL_VOID(dev->module_id, dev->uninit, dev);
   }
   goto out;
}

/*
 *----------------------------------------------------------------------------
 *
 *  netdev_ioctl --
 *    Process an ioctl request for a given device.
 *
 *  Results:
 *    VMK_ReturnStatus indicating the outcome.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netdev_ioctl(struct net_device *dev, uint32_t cmd, void *args, uint32_t *result,
             vmk_IoctlCallerSize callerSize, vmk_Bool callerHasRtnlLock)
{  
   VMK_ReturnStatus ret = VMK_OK;
   
   VMK_ASSERT(dev);
   
   if (args && result) {
      if (cmd == SIOCGIFHWADDR) {
         struct ifreq *ifr = args;
         memcpy(ifr->ifr_hwaddr.sa_data, dev->dev_addr, 6);
         ifr->ifr_hwaddr.sa_family = dev->type;
         *result = 0;
         return VMK_OK;
      }

      if (cmd == SIOCETHTOOL) {
         struct ifreq *ifr = args;

         if (callerHasRtnlLock == VMK_FALSE) {
            rtnl_lock();
         }

         ret = vmklnx_ethtool_ioctl(dev, ifr, result, callerSize);

         if (callerHasRtnlLock == VMK_FALSE) {
            rtnl_unlock();
         }

         return ret;
      }  
      
      if (dev->do_ioctl) {
         VMKAPI_MODULE_CALL(dev->module_id, *result, dev->do_ioctl, dev,
            args, cmd);
         ret = VMK_OK;
      } else { 
         ret = VMK_NOT_SUPPORTED;
      }  
   } else {
      VMKLNX_DEBUG(0, "net_device: %p, cmd: 0x%x, args: %p, result: %p",
          dev, cmd, args, result);
      ret = VMK_FAILURE;
   }  
   
   return ret;
}

/*
 *----------------------------------------------------------------------------
 *
 *  link_state_work_cb --
 *
 *    Periodic work function to check the status of various physical NICS.
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static void
link_state_work_cb(struct work_struct_plus *work)
{  
   struct net_device *cur;
   uint32_t result; 
   unsigned speed = 0, duplex = 0, linkState = 0;
   VMK_ReturnStatus status;
   unsigned newLinkStateTimerPeriod;
   
   /*
    * Since the ethtool ioctls require the rtnl_lock,
    * we should acquire the lock first before getting 
    * dev_base_lock. This is the order used by other
    * code paths that require both locks.
    */
   rtnl_lock();
   write_lock(&dev_base_lock);

   cur = dev_base;
   while (cur) {
      struct ethtool_cmd *cmd;
      struct ifreq ifr;

      vmk_Bool link_changed = VMK_FALSE;

      cmd = compat_alloc_user_space(sizeof(*cmd));  

      memset(&ifr, 0, sizeof(ifr));
      memcpy(ifr.ifr_name, cur->name, sizeof(ifr.ifr_name));

      /* get link speed and duplexity */
      put_user(ETHTOOL_GSET, &cmd->cmd);
      ifr.ifr_data = (void *) cmd;
      if (netdev_ioctl(cur, SIOCETHTOOL, &ifr, &result,
                       VMK_IOCTL_CALLER_64, VMK_TRUE) == VMK_OK) {
         get_user(speed, &cmd->speed);
         get_user(duplex, &cmd->duplex);
      }
      
      /* get link state */
      put_user(ETHTOOL_GLINK, &cmd->cmd);
      ifr.ifr_data = (void *) cmd;
      if (netdev_ioctl(cur, SIOCETHTOOL, &ifr, &result,
                       VMK_IOCTL_CALLER_64, VMK_TRUE) == VMK_OK) {
         struct ethtool_value value;
         copy_from_user(&value, cmd, sizeof(struct ethtool_value));
         linkState = value.data ?  VMK_UPLINK_LINK_UP : VMK_UPLINK_LINK_DOWN;
      }

      /* set speed, duplexity and link state if changed */
      if (cur->link_state != linkState) {
         cur->link_state = linkState;
         link_changed = VMK_TRUE;
      }
      if (netif_carrier_ok(cur)) {
         if (cur->full_duplex != duplex) {
            cur->full_duplex = duplex;
            link_changed = VMK_TRUE;
         }
         if (cur->link_speed != speed) {
            cur->link_speed = speed;
            link_changed = VMK_TRUE;
         }
      }
      if (link_changed) {
         SetNICLinkStatus(cur);
      }

      cur = cur->next;
   }

   write_unlock(&dev_base_lock);
   rtnl_unlock();

   status = vmk_ConfigParamGetUint(linkStateTimerPeriodConfigHandle, 
                                   &newLinkStateTimerPeriod);
   VMK_ASSERT(status == VMK_OK);
   if (linkStateTimerPeriod != newLinkStateTimerPeriod) {
      linkStateTimerPeriod = newLinkStateTimerPeriod;
   }
   schedule_delayed_work(&linkStateWork, 
                         msecs_to_jiffies(linkStateTimerPeriod));

   /* Periodic update of the LRO config option */
   status = vmk_ConfigParamGetUint(vmklnxLROEnabledConfigHandle, 
                                   &vmklnxLROEnabled);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_ConfigParamGetUint(vmklnxLROMaxAggrConfigHandle, 
                                   &vmklnxLROMaxAggr);
   VMK_ASSERT(status == VMK_OK);
}

/*
 *----------------------------------------------------------------------------
 *
 *  netdev_watchdog --
 *
 *    Device watchdog
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static void
netdev_watchdog(struct net_device *dev)
{
   int some_queue_stopped = 0;

   netif_tx_lock(dev);
   if (netif_device_present(dev) &&
       netif_running(dev) &&
       netif_carrier_ok(dev)) {
      unsigned int i;

      for (i = 0; i < dev->real_num_tx_queues; i++) {
         struct netdev_queue *txq;

         txq = netdev_get_tx_queue(dev, i);
         if (netif_tx_queue_stopped(txq)) {
            some_queue_stopped = 1;
            break;
         }
      }

      if (some_queue_stopped &&
          time_after(jiffies, (dev->trans_start +
                               dev->watchdog_timeo))) {
         VMKLNX_WARN("NETDEV WATCHDOG: %s: "
                     "transmit timed out\n",
                     dev->name);

         dev->watchdog_timeohit_stats++;
         vmk_UplinkWatchdogTimeoutHit(dev->uplinkDev);

         /* call driver to reset the device */
         VMKAPI_MODULE_CALL_VOID(dev->module_id, dev->tx_timeout, dev);
         WARN_ON_ONCE(1);

#ifdef VMX86_DEBUG
         // PR 167776: Reset counter every hour or so. We'll panic
         // only if we go beyond a certain number of watchdog timouts
         // in an hour.
         if (time_after(jiffies,
                        dev->watchdog_timeohit_period_start + NETDEV_TICKS_PER_HOUR)) {
            dev->watchdog_timeohit_cnt = 0;
            dev->watchdog_timeohit_period_start = jiffies;
         }

         if (!VMKLNX_STRESS_DEBUG_OPTION(stressNetIfFailTxAndStopQueue)) {
            dev->watchdog_timeohit_cnt++;      

            if (dev->watchdog_timeohit_cnt >= dev->watchdog_timeohit_cfg) {
               dev->watchdog_timeohit_cnt = 0;
               if (dev->watchdog_timeohit_panic == VMK_UPLINK_WATCHDOG_PANIC_MOD_ENABLE) {
                  VMK_ASSERT_BUG(VMK_FALSE);
               }
            }
         }
#endif
      }
   }
   netif_tx_unlock(dev);

   /*
    * if the device can't possibly tx anything we shouldn't be queueing
    * above it.  anything that gets queued down here for a long time 
    * will stuff up virtual devices that do zero copy and wait for the
    * completion events.
    */
   if (!netif_device_present(dev) ||
       !netif_running(dev) ||
       !netif_carrier_ok(dev)) {
      stop_all_tx_soft_queues(dev);
   } else {
      start_all_tx_soft_queues(dev);
   }
}

/*
 *----------------------------------------------------------------------------
 *
 *  watchdog_timer_cb --
 *
 *    Watchdog timer callback for all registered devices.
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static void
watchdog_work_cb(struct work_struct_plus *work)
{
   struct net_device *dev = NULL;
   
   write_lock(&dev_base_lock);
   
   for (dev = dev_base; dev; dev = dev->next) {
      netdev_watchdog(dev);
   }
   
   write_unlock(&dev_base_lock);

   schedule_delayed_work(&watchdogWork, 
                         msecs_to_jiffies(WATCHDOG_DEF_TIMER));
}

/**
 * dev_get_by_name - find a device by its name
 * @name: name to find
 *
 * Find an interface by name. The returned handle does not have the 
 * usage count incremented and the caller must be careful defore using
 * the handle.  %NULL is returned if no matching device is found.
 *
 * RETURN VALUE:
 *  Pointer to device structure on success
 *  %NULL is returned if no matching device is found
 */
/* _VMKLNX_CODECHECK_: __dev_get_by_name */
struct net_device *
__dev_get_by_name(const char *name)
{
   struct net_device *dev;

   read_lock(&dev_base_lock);

   dev = dev_base;
   while (dev) {
      if (!strncmp(dev->name, name, sizeof(dev->name))) {
         break;
      }      
      dev = dev->next;
   }   

   read_unlock(&dev_base_lock);

   return dev;
}

/**
 * dev_get_by_name - find a device by its name
 * @name: name to find
 *
 * Find an interface by name. The returned handle has the usage count
 * incremented and the caller must use dev_put() to release it when it
 * is no longer needed. %NULL is returned if no matching device is 
 * found.
 * 
 * RETURN VALUE:
 *  Pointer to device structure on success
 *  %NULL is returned if no matching device is found
 */
/* _VMKLNX_CODECHECK_: dev_get_by_name */
struct net_device *
dev_get_by_name(const char *name)
{
   struct net_device *dev;

   dev = __dev_get_by_name(name);
   if (dev) {
      dev_hold(dev);
   }
   return dev;
}

/**
 * dev_alloc_name - allocate a name for a device
 * @dev: device
 * @name: name format string
 *
 * Passed a format string - eg "lt%d" it will try and find a suitable
 * id. It scans list of devices to build up a free map, then chooses
 * the first empty slot. Returns the number of the unit assigned or 
 * a negative errno code.
 *
 * RETURN VALUE:
 *  Number of the unit assigned on success
 *  Negative errno code on error
 */
/* _VMKLNX_CODECHECK_: dev_alloc_name */
int
dev_alloc_name(struct net_device *dev, const char *name)
{
   int i;
   char buf[32];
   char *p;
   vmk_String uplinkName;

   p = strchr(name, '%');
   if (p && (p[1] != 'd' || strchr(p+2, '%'))) {
      return -EINVAL;
   }

   for (i = 0; i < 100; i++) {
      snprintf(buf,sizeof(buf),name,i);
      VMK_STRING_SET(&uplinkName, buf, sizeof(buf), strlen(buf));

      if (vmk_UplinkIsNameAvailable(&uplinkName)) {
         strcpy(dev->name, buf);
         return i;
      }
   }
   return -ENFILE;
}

/*
 *----------------------------------------------------------------------------
 *
 *  set_device_pci_name --
 *
 *    Set device's pci name
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static void
set_device_pci_name(struct net_device *dev, struct pci_dev *pdev)
{
   /* We normally have the pci device name, because
    * execfg-init or esxcfg-init-eesx generates the pci device names.
    *
    * We just override it with the one named by the driver.
    */
   VMK_ASSERT_ON_COMPILE(VMK_DEVICE_NAME_MAX_LENGTH >= IFNAMSIZ);
   if (LinuxPCI_IsValidPCIBusDev(pdev)) {
      LinuxPCIDevExt *pe = container_of(pdev, LinuxPCIDevExt, linuxDev);
      vmk_PCISetDeviceName(pe->vmkDev, dev->name);
      strncpy(pdev->name, dev->name,  sizeof(pdev->name));
   }
   if (strnlen(dev->name, VMK_DEVICE_NAME_MAX_LENGTH) > (IFNAMSIZ - 1)) {
      VMKLNX_WARN("Net device name length(%zd) exceeds IFNAMSIZ - 1(%d)",
                  strnlen(dev->name, VMK_DEVICE_NAME_MAX_LENGTH), IFNAMSIZ - 1);
   }
}

/**
 * register_netdev	- register a network device
 * @dev: device to register
 *
 * Take a completed network device structure and add it to the kernel
 * interfaces. 0 is returned on success. A negative errno code is returned
 * on a failure to set up the device, or if the name is a duplicate.
 *
 * This is a wrapper around register_netdevice that expands the device name 
 * if you passed a format string to alloc_netdev.
 *
 * RETURN VALUE:
 *  0 on success
 *  negative errno code on error
 */
/* _VMKLNX_CODECHECK_: register_netdev */
int
register_netdev(struct net_device *dev)
{
   int err = 0;

   rtnl_lock();

   if (strchr(dev->name, '%')) {
      err = dev_alloc_name(dev, dev->name);
   } else if (dev->name[0]==0 || dev->name[0]==' ') {
      err = dev_alloc_name(dev, "vmnic%d");
   }

   if (err >= 0) {
      struct pci_dev *pdev = dev->pdev;

      if (dev->useDriverNamingDevice) {
         /* net_device already named, we need update the PCI device name list */
         set_device_pci_name(dev, pdev);
      }
      err = register_netdevice(dev);
   }

   rtnl_unlock();

   if (dev->pdev == NULL) {
      /*
       * For pseudo network interfaces, we connect and open the 
       * uplink at this point. For Real PCI NIC's, they do 
       * this in pci_announce_device() and vmk_PCIPostInsert()
       * respectively.
       */
      vmk_String uplinkName;
      VMK_STRING_SET(&uplinkName, dev->name, sizeof(dev->name), strlen(dev->name));
      if (LinNet_ConnectUplink(dev, NULL)
          || (vmk_UplinkOpen(&uplinkName) != VMK_OK)) {
         err = -EIO;
      }
   }

   return err;
}

int
unregister_netdevice(struct net_device *dev)
{
   struct net_device **cur;

   VMK_ASSERT(atomic_read(&dev->refcnt) == 1);

   if (!Linux_UnregisterDevice(dev, dev->name)) {
      VMKLNX_WARN("Couldn't unregister device %s", dev->name);
   }

   if (dev->nicMajor > 0) {
      vmk_CharDevUnregister(dev->nicMajor, 0, dev->name);
   }

   if (dev->flags & IFF_UP) {
      dev_close(dev);
   }

   VMK_ASSERT(dev->reg_state == NETREG_REGISTERED);
   dev->reg_state = NETREG_UNREGISTERING;
   
   if (dev->uninit) {
      VMKAPI_MODULE_CALL_VOID(dev->module_id, dev->uninit, dev);
   }

   write_lock(&dev_base_lock);
   cur = &dev_base;
   while (*cur && *cur != dev) {
      cur = &(*cur)->next;
   }
   if (*cur) {
      *cur = (*cur)->next;
   }
   write_unlock(&dev_base_lock);

   dev->reg_state = NETREG_UNREGISTERED;

   netdev_poll_cleanup(dev);

   VMK_ASSERT(dev->vlan_group == NULL);
   if (dev->vlan_group) {
      vmk_HeapFree(vmklinux_HeapID, dev->vlan_group);
      dev->vlan_group = NULL;
   }

   /*
    * Disassociate the pci_dev from this net device
    */
   if (dev->pdev != NULL) {
      dev->pdev->netdev = NULL;
      dev->pdev = NULL;
   }

   dev_put(dev);
   return 0;
}

/**
 * unregister_netdev - remove device from the kernel
 * @dev: device
 *
 * This function shuts down a device interface and removes it from the 
 * kernel tables.
 *
 * This is just a wrapper for unregister_netdevice. In general you want 
 * to use this and not unregister_netdevice.
 *
 * RETURN VALUE:
 *  None
 */
/* _VMKLNX_CODECHECK_: unregister_netdev */
void
unregister_netdev(struct net_device *dev)
{
   unsigned long warning_time;

   VMKLNX_DEBUG(0, "Unregistering %s", dev->name);

   if (dev->pdev == NULL) {
      /*
       * Close and disconnect the uplink here if
       * the device is a pseudo NIC. For real PCI
       * NIC, the uplink is closed and disconnected
       * via vmk_PCIDoPreRemove().
       */
      vmk_String uplinkName;
      VMK_STRING_SET(&uplinkName, dev->name, sizeof(dev->name), strlen(dev->name));
      vmk_UplinkClose(&uplinkName);
   }

   /*
    * Fixed PR366444 - Moved the 'refcnt' check here from within
    * unregister_netdevice()
    *
    * We will be stuck in the while loop below if someone forgot
    * to drop the reference count.
    */
   warning_time = jiffies;
   rtnl_lock();
   while (atomic_read(&dev->refcnt) > 1) {
      rtnl_unlock();

      if ((jiffies - warning_time) > 10*HZ) {
         VMKLNX_WARN("waiting for %s to become free. Usage count = %d\n",
                     dev->name, atomic_read(&dev->refcnt));
         warning_time = jiffies;
      }

      current->state = TASK_INTERRUPTIBLE;
      schedule_timeout(HZ/4);
      current->state = TASK_RUNNING;

      rtnl_lock();
   }

   unregister_netdevice(dev);
   rtnl_unlock();
   VMKLNX_DEBUG(0, "Done Unregistering %s", dev->name);
}

/*
 *-----------------------------------------------------------------------------
 *
 * create_dev_name --
 *
 * create a unique name for a network device.
 *
 * Results:
 *      none
 *
 * Side effects:
 *      pdev->name field is set to vmnic%d
 *
 *-----------------------------------------------------------------------------
 */
static void
create_dev_name(char *name, int length)
{
   /*
    * We use 32 as the starting number because we do not want to overlap with
    * the names used in the initprocess.  It is assumed that the first 32
    * devices (vmnic0 - vmnic31) may be used during boot.
    */
   #define NET_ANON_START_ID VMK_CONST64U(32)
   static vmk_atomic64 nameCounter = NET_ANON_START_ID;

   snprintf(name, length, "vmnic%"VMK_FMT64"u",
            vmk_AtomicReadInc64(&nameCounter));
}

/*
 *-----------------------------------------------------------------------------
 *
 * netdev_name_adapter --
 *
 *      Set the PCI adapter name, if not already set.  If the PCI adapter
 *      already has a name and the name is registered as an uplink then
 *      create a new name for a new uplink port.  Copy it to the net_device
 *      structure.
 *
 * Results:
 *      none
 *
 * Side effects:
 *      dev->name field is set.
 *
 *-----------------------------------------------------------------------------
 */
static void
netdev_name_adapter(struct net_device *dev, struct pci_dev *pdev)
{  
   LinuxPCIDevExt *pe;
   char devName[VMK_DEVICE_NAME_MAX_LENGTH];
   char *name = NULL;
   
   if (pdev == NULL) {
      // Pseudo devices may handle their own naming.
      if (dev->name[0] != 0) {
         return;
      }
      create_dev_name(dev->name, sizeof dev->name);
      VMKLNX_INFO("Pseudo device %s", dev->name);
      return;
   }
   
   pe = container_of(pdev, LinuxPCIDevExt, linuxDev);

   /* Make sure a name exists */
   devName[0] = '\0';
   vmk_PCIGetDeviceName(pe->vmkDev, devName, sizeof devName);

   /*
    * If we do not have a name for the physical device then create one, else
    * if the uplink port has already been registered then we assume that the
    * we are called for a new port on the device and therefore create a new
    * which we are not passing on to the physical device.
    */
   if (devName[0] == '\0') {
      create_dev_name(pdev->name, sizeof pdev->name);
      vmk_PCISetDeviceName(pe->vmkDev, pdev->name);
      name = pdev->name;
      VMKLNX_INFO("%s at " PCI_DEVICE_BUS_ADDRESS, pdev->name,
                  pdev->bus->number, 
                  PCI_SLOT(pdev->devfn), 
                  PCI_FUNC(pdev->devfn));
   } else {
      vmk_String uplinkName;
      
      VMK_STRING_SET(&uplinkName, devName, sizeof(devName), strlen(devName));

      if (!vmk_UplinkIsNameAvailable(&uplinkName)) {
	 create_dev_name(pdev->name, sizeof pdev->name);
         name = pdev->name;
      } else {
         name = devName;
      }
   }

   /*
    * Give the PCI device name to net_device
    */
   snprintf(dev->name, sizeof (dev->name), "%s", name);
   
}

/*
 *----------------------------------------------------------------------------
 *
 *  netdev_query_capabilities --
 *
 *    Checks hardware device's capability and return the information in a 
 *    32 bit "capability" value
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static vmk_UplinkCapabilities 
netdev_query_capabilities(struct net_device *dev)
{
   vmk_UplinkCapabilities capability = 0;
   VMK_ReturnStatus status;
   unsigned int permitHwIPv6Csum = 0;
   unsigned int permitHwCsumForIPv6Csum = 0;
   unsigned int permitHwTSO6 = 0;

   status = vmk_ConfigParamGetUint(useHwIPv6CsumHandle, &permitHwIPv6Csum);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_ConfigParamGetUint(useHwCsumForIPv6CsumHandle, &permitHwCsumForIPv6Csum);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_ConfigParamGetUint(useHwTSO6Handle, &permitHwTSO6);
   VMK_ASSERT(status == VMK_OK);
   
   VMKLNX_DEBUG(0, "Checking device: %s's capabilities", dev->name);
   if (dev->features & NETIF_F_HW_VLAN_TX) {
      VMKLNX_DEBUG(0, "device: %s has hw_vlan_tx capability", dev->name);
      vmk_UplinkCapabilitySet(&capability, VMK_UPLINK_CAP_HW_TX_VLAN, VMK_TRUE);
   }  
   if (dev->features & NETIF_F_HW_VLAN_RX) {
      VMKLNX_DEBUG(0, "device: %s has hw_vlan_rx capability", dev->name);
      vmk_UplinkCapabilitySet(&capability, VMK_UPLINK_CAP_HW_RX_VLAN, VMK_TRUE);
   }  
   if (dev->features & NETIF_F_IP_CSUM) {
      VMKLNX_DEBUG(0, "device: %s has IP CSUM capability", dev->name);
      vmk_UplinkCapabilitySet(&capability, VMK_UPLINK_CAP_IP4_CSUM, VMK_TRUE);
   }  
   if (permitHwIPv6Csum
       && (dev->features & NETIF_F_IPV6_CSUM)) {
      VMKLNX_DEBUG(0, "device: %s has IPV6 CSUM capability", dev->name);
      vmk_UplinkCapabilitySet(&capability, VMK_UPLINK_CAP_IP6_CSUM, VMK_TRUE);
   }  
   if (dev->features & NETIF_F_HW_CSUM) {
      VMKLNX_DEBUG(0, "device: %s has HW CSUM capability", dev->name);
      // IP is the subset of HW we support.
      vmk_UplinkCapabilitySet(&capability, VMK_UPLINK_CAP_IP4_CSUM, VMK_TRUE);
      if (permitHwCsumForIPv6Csum) {
         VMKLNX_DEBUG(0, "device: %s has HW CSUM => IPv6 CSUM capability", dev->name);
         vmk_UplinkCapabilitySet(&capability, VMK_UPLINK_CAP_IP6_CSUM, VMK_TRUE);
      }
   }  
   if ((dev->features & NETIF_F_SG) &&
       (MAX_SKB_FRAGS >= VMK_PKT_FRAGS_MAX_LENGTH)) {
      VMKLNX_DEBUG(0, "device: %s has SG capability", dev->name);
      vmk_UplinkCapabilitySet(&capability, VMK_UPLINK_CAP_SG, VMK_TRUE);
   }
   if (!(dev->features & NETIF_F_FRAG_CANT_SPAN_PAGES)) {
      VMKLNX_DEBUG(0, "device: %s has Frag Span Pages capability", dev->name);
      vmk_UplinkCapabilitySet(&capability, VMK_UPLINK_CAP_SG_SPAN_PAGES,
                              VMK_TRUE);
   }
   if (dev->features & NETIF_F_HIGHDMA) {
      VMKLNX_DEBUG(0, "device: %s has high dma capability", dev->name);
      vmk_UplinkCapabilitySet(&capability, VMK_UPLINK_CAP_HIGH_DMA, VMK_TRUE);
   }  
   if (dev->features & NETIF_F_TSO) {
      VMKLNX_DEBUG(0, "device: %s has TSO capability", dev->name);
      vmk_UplinkCapabilitySet(&capability, VMK_UPLINK_CAP_TSO, VMK_TRUE);
   }  
   
   if (permitHwTSO6
       && (dev->features & NETIF_F_TSO6)) {
      VMKLNX_DEBUG(0, "device: %s has TSO6 capability", dev->name);
      vmk_UplinkCapabilitySet(&capability, VMK_UPLINK_CAP_TSO6, VMK_TRUE);
   } 

   /*
    * PR #324545: Artificially turn this feature on so that the VMkernel
    * doesn't activate any unnecessary & wasteful SW workaround.
    * The VMkernel shouldn't generate this kind of frames anyway.
    */
   if (VMK_TRUE) {
      VMKLNX_DEBUG(0, "device: %s has TSO256k capability", dev->name);
      vmk_UplinkCapabilitySet(&capability, VMK_UPLINK_CAP_TSO256k, VMK_TRUE);
   }

   if (dev->features & NETIF_F_TSO) {
      /*
       *  If a pNIC can do TSO, but not any of the following,
       *  our software path for any of these missing functions
       *  may end up trying to allocate very large buffers and
       *  not able to do it.  We'd like to know about such 
       *  devices during development.  In production mode, if
       *  hardware is unable to do CSUM, we will not use hardware 
       *  TSO.  
       * NB: we already know that some e1000 devices, 
       *      e.g. 82544EI (e1000 XT), can do TSO but not High_DMA.
       */
      VMK_ASSERT((dev->features & NETIF_F_IP_CSUM)
             || (dev->features & NETIF_F_HW_CSUM));
      VMK_ASSERT(dev->features & NETIF_F_SG);
      VMK_ASSERT(!(dev->features & NETIF_F_FRAG_CANT_SPAN_PAGES));
      
      if (!((dev->features & NETIF_F_IP_CSUM) || 
            (dev->features & NETIF_F_HW_CSUM))) {
         VMKLNX_WARN("%s: disabling hardware TSO because dev "
                     "has no hardware CSUM",
                     dev->name);
	 vmk_UplinkCapabilitySet(&capability, VMK_UPLINK_CAP_TSO, VMK_FALSE);
      } 

      if (!(dev->features & NETIF_F_SG) ||
          (dev->features & NETIF_F_FRAG_CANT_SPAN_PAGES)) {
         VMKLNX_WARN("%s: disabling hardware TSO because dev "
                     "has no hardware SG",
                     dev->name);
	 vmk_UplinkCapabilitySet(&capability, VMK_UPLINK_CAP_TSO, VMK_FALSE);
      }  
   }  
   

   VMKLNX_DEBUG(0, "device %s vmnet cap is 0x%"VMK_FMT64"x", 
                dev->name, capability);
   
   return capability;
}

/*
 * Section: calltable functions, called through vmk_UplinkFunctions 
 */

/*
 *----------------------------------------------------------------------------
 *
 *  IoctlNetDev --
 *
 *    Handle an ioctl request from the VMKernel for the given device name.
 *
 *  Results:
 *    VMK_ReturnStatus indicating the outcome.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
IoctlNetDev(vmk_String *uplinkName, uint32_t cmd, void *args, uint32_t *result)
{
   VMK_ReturnStatus status;
   struct net_device *dev;

   VMK_STRING_CHECK_CONSISTENCY(uplinkName);

   dev = dev_get_by_name(uplinkName->buffer);
   if (!dev) {
      return VMK_NOT_FOUND;
   }

   status = netdev_ioctl(dev, cmd, args, result, VMK_IOCTL_CALLER_64, VMK_FALSE);
   dev_put(dev);
   return status;
}

/*
 *-----------------------------------------------------------------------------
 *
 * SetNICLinkStatus --
 *
 *      Push new link status up to the vmkernel.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May cause teaming failover events to be scheduled.
 *
 *-----------------------------------------------------------------------------
 */
void
SetNICLinkStatus(struct net_device *dev)
{
   vmk_UplinkLinkInfo linkInfo;

   linkInfo.linkState  = dev->link_state;
   linkInfo.linkSpeed  = linkInfo.linkState ? dev->link_speed  : 0;
   linkInfo.fullDuplex = linkInfo.linkState ? dev->full_duplex : VMK_FALSE;

   /* Test if the uplink is connected (for a pseudo device) */
   if (dev->uplinkDev) {
      vmk_UplinkUpdateLinkState(dev->uplinkDev, &linkInfo);
   }
}

/*
 *----------------------------------------------------------------------------
 *
 * DevStartTxImmediate --
 *
 *    External entry point for transmitting packets. Packets are queued and
 *    then Tx-ed immediately.
 *   
 *
 * Results:
 *    VMK_ReturnStatus indicating the outcome.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
DevStartTxImmediate(void *clientData, vmk_PktList *pktList)
{
   return netdev_tx((struct net_device *)clientData, pktList);
}  



/*
 *----------------------------------------------------------------------------
 *
 *  OpenNetDev --
 *
 *    Handler for calling the device's open function. If successful, the device
 *    state is changed to indicate that the device has been opened.
 *
 *  Results:
 *    Returns whatever the device's open function returns.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
OpenNetDev(void *clientData)
{
   struct net_device *dev = (struct net_device *)clientData;
   int status;

   if (dev->open == NULL) {
      VMKLNX_WARN("NULL open function for device %s", dev->name);
      return 1;
   }

   rtnl_lock();
   status = dev_open(dev);
   rtnl_unlock();

   return status == 0 ? VMK_OK : VMK_FAILURE;
}

/*
 *----------------------------------------------------------------------------
 *
 *  CloseNetDev --
 * 
 *    Handler for closing the device. If successful, the device state is
 *    modified to indicate that the device is now non-functional.
 *
 *  Results:
 *    Returns whatever the stop function of the module owning the device
 *    returns.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
CloseNetDev(void *clientData)
{
   struct net_device *dev = (struct net_device *)clientData;
   int status;

   VMK_ASSERT(dev->stop != NULL);
   VMKLNX_DEBUG(0, "Stopping device %s", dev->name);

   rtnl_lock();
   status = dev_close(dev);
   rtnl_unlock();

   return status == 0 ? VMK_OK : VMK_FAILURE;
}


/*
 *----------------------------------------------------------------------------
 *
 *  BlockNetDev --
 * 
 *    Handler for blocking the device. If successful, the device state is
 *    modified to indicate that the device is now blocked.
 *
 *  Results:
 *    VMK_OK always.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
BlockNetDev(void *clientData)
{
   unsigned int i;
   struct net_device *dev = (struct net_device *)clientData;
   unsigned singleSleepMsec = 50;
   unsigned totalSleepMsec = blockTotalSleepMsec;
   
   if (test_and_set_bit(__LINK_STATE_BLOCKED, &dev->state)) {
      goto done;
   }
   
   /* Wait for any scheduled TX_SOFTIRQ to complete on all queues */
   for (i = 0; i < dev->num_tx_queues; i++) {
      struct netdev_queue *queue = &dev->_tx[i]; 

      block_tx_soft_queue(queue);
      while (test_and_set_bit(__QUEUE_STATE_SCHED, &queue->state)) {
         if (totalSleepMsec < 0) {
            VMKLNX_WARN("Waiting for %s tx schedule on queue %d : timeout\n", 
                        dev->name, i);
            VMK_ASSERT(VMK_FALSE);
            break;
         }
         msleep(singleSleepMsec);
         totalSleepMsec -= singleSleepMsec;
      }
   }

   totalSleepMsec = blockTotalSleepMsec;
   do {
      if (totalSleepMsec < 0) {
         VMKLNX_WARN("Waiting for %s rx inflight : timeout\n", dev->name);
         VMK_ASSERT(VMK_FALSE);
         break;
      }
      msleep(singleSleepMsec);
      totalSleepMsec -= singleSleepMsec;
   } while (atomic_read(&dev->rxInFlight) != 0);
   
   /* Emulate a case where it takes longer to complete the rx packets in flight */
   if (VMKLNX_STRESS_DEBUG_COUNTER(stressNetBlockDevIsSluggish) &&
       (totalSleepMsec < blockTotalSleepMsec)) {
      msleep(blockTotalSleepMsec - totalSleepMsec);
   }
   
 done:
   return VMK_OK;
}

/*
 *----------------------------------------------------------------------------
 *
 *  UnblockNetDev --
 * 
 *    Handler for unblocking the device. If successful, the device state is
 *    modified to indicate that the device is now unblocked.
 *
 *  Results:
 *    VMK_OK always.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
UnblockNetDev(void *clientData)
{
   unsigned int i;
   struct net_device *dev = (struct net_device *)clientData;

   if (!test_bit(__LINK_STATE_BLOCKED, &dev->state)) {
      goto done;
   }
   
   VMK_ASSERT(atomic_read(&dev->rxInFlight) == 0);

   for (i = 0; i < dev->num_tx_queues; i++) {
      struct netdev_queue *queue = &dev->_tx[i]; 
      clear_bit(__QUEUE_STATE_SCHED, &queue->state);
      unblock_tx_soft_queue(queue);
   }

   smp_mb__before_clear_bit();
   clear_bit(__LINK_STATE_BLOCKED, &dev->state);

 done:
   return VMK_OK;
}


/*
 *-----------------------------------------------------------------------------
 *
 * SetupVlanGroupDevice --
 *
 *      Enable HW vlan and add new vlan id's based on the bitmap.
 *      If enable is FALSE, hardware vlan is expected to be enabled
 *      already, If bitmap is null, just do enable.
 *
 * Results:
 *      Return VMK_OK if there is VLan HW tx/rx acceleration support;
 *      Return VMK_VLAN_NO_HW_ACCEL otherwise.
 *
 * Side effects:
 *      hw vlan register is updated.
 *
 *-----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
SetupVlanGroupDevice(void *clientData, vmk_Bool enable, void *bitmap)
{
   struct net_device *dev = (struct net_device *) clientData;
   struct vlan_group *grp = dev->vlan_group;

   VMK_ASSERT(dev->features & NETIF_F_HW_VLAN_RX);

   /* call driver's vlan_rx_register handler to enable vlan */
   if (enable) {
      VMK_ASSERT(dev->vlan_rx_register);
      if (!dev->vlan_rx_register) {
         VMKLNX_DEBUG(0, "%s: no vlan_rx_register handler", dev->name);
         return VMK_VLAN_NO_HW_ACCEL;
      }

      VMK_ASSERT(grp == NULL);
      if (grp == NULL) {
         grp = vmk_HeapAlloc(vmklinux_HeapID, sizeof (struct vlan_group));
         if (grp == NULL) {
            VMKLNX_DEBUG(0, "%s: failed to allocate vlan_group", dev->name);
            return VMK_NO_MEMORY;
         }
         vmk_Memset(grp, 0, sizeof (struct vlan_group));
         dev->vlan_group = grp;
      }

      VMKLNX_DEBUG(0, "%s: enabling vlan", dev->name);
      VMKAPI_MODULE_CALL_VOID(dev->module_id, dev->vlan_rx_register, dev, grp);
   }

   /* if hw doesn't support rx vlan filter, bail out here */
   if (!(dev->features & NETIF_F_HW_VLAN_FILTER)) {
      return VMK_OK;
   }

   /* now compare bitmap with vlan_group and make up the difference */
   if (bitmap) {
      vmk_VlanID vid;
      VMK_ASSERT(dev->vlan_rx_add_vid);
      if (!dev->vlan_rx_add_vid) {
         VMKLNX_DEBUG(0, "%s: driver has no vlan_rx_add_vid handler",
                     dev->name);
         return VMK_FAILURE;
      }

      for (vid = 0; vid < VLAN_GROUP_ARRAY_LEN; vid++) {
         if (test_bit(vid, bitmap) && grp->vlan_devices[vid] == NULL) {
            grp->vlan_devices[vid] = dev;
            VMKLNX_DEBUG(1, "%s: adding vlan id %d", dev->name, (int)vid);
            VMKAPI_MODULE_CALL_VOID(dev->module_id, dev->vlan_rx_add_vid, dev,
                                    vid);
         }
      }
   }

   return VMK_OK;
}

/*
 *-----------------------------------------------------------------------------
 *
 * RemoveVlanGroupDevice --
 *
 *      Delete vlan id's based on bitmap and disable hw vlan.
 *      Either bitmap or disable should be set, but not both.
 *      If neither is set, there is no work to do (illegal?).
 *
 * Results:
 *      VMK_OK if successfully added/deleted.
 *      VMK_FAILURE otherwise.
 * 
 * Side effects:
 *      HW vlan table is updated. HW will may stop passing vlan.
 *
 *-----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
RemoveVlanGroupDevice(void *clientData, vmk_Bool disable, void *bitmap)
{
   struct net_device *dev = (struct net_device *) clientData;
   struct vlan_group *grp = dev->vlan_group;

   VMK_ASSERT(dev->features & NETIF_F_HW_VLAN_RX);

   /* Unregister vid's if hardware supports vlan filter */
   if (dev->features & NETIF_F_HW_VLAN_FILTER) {
      vmk_VlanID vid;
      VMK_ASSERT(dev->vlan_rx_kill_vid);
      if (!dev->vlan_rx_kill_vid) {
         VMKLNX_DEBUG(0, "%s: no vlan_rx_kill_vid handler", dev->name);
         return VMK_FAILURE;
      }

      for (vid = 0; vid < VLAN_GROUP_ARRAY_LEN; vid++) {
         if (grp->vlan_devices[vid] == NULL) {
            continue;
         }
         /* delete all if disable is true, else consult bitmap */
         if (disable || (bitmap && !test_bit(vid, bitmap))) {
            grp->vlan_devices[vid] = NULL;
            VMKLNX_DEBUG(1, "%s: deleting vlan id %d", dev->name, (int)vid);
            VMKAPI_MODULE_CALL_VOID(dev->module_id, dev->vlan_rx_kill_vid, dev,
                                    vid);
         }
      }
   }

   if (disable) {
      VMK_ASSERT(dev->vlan_rx_register);
      if (!dev->vlan_rx_register) {
         VMKLNX_DEBUG(0, "%s: no vlan_rx_register handler", dev->name);
         return VMK_VLAN_NO_HW_ACCEL;
      }

      VMKLNX_DEBUG(0, "%s: disabling vlan", dev->name);
      VMKAPI_MODULE_CALL_VOID(dev->module_id, dev->vlan_rx_register, dev, NULL);

      VMK_ASSERT(grp);
      if (grp) {
         dev->vlan_group = NULL;
         vmk_HeapFree(vmklinux_HeapID, grp);
      }
   }
   return VMK_OK;
}

/*
 *-----------------------------------------------------------------------------
 *
 * NICGetMTU --
 *
 *      Returns the MTU value for the given NIC
 *
 * Results:
 *      MTU for the given device.
 * 
 * Side effects:
 *      none.
 *
 *-----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
NICGetMTU(void *device, vmk_uint32 *mtu)
{
   struct net_device *dev = (struct net_device *) device;
   
   *mtu = dev->mtu;

   return VMK_OK;
}

/*
 *-----------------------------------------------------------------------------
 *
 * NICSetMTU --
 *
 *      Set new MTU for the given NIC
 *
 * Results:
 *      VMK_OK if the new_mtu is accepted by the device.
 *      VMK_FAILURE or VMK_NOT_SUPPORTED otherwise.
 * 
 * Side effects:
 *      The device queue is stopped. For most devices the entire ring is
 *      reallocated, and the device is reset.
 *
 *-----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
NICSetMTU(void *device, vmk_uint32 new_mtu)
{
   int ret = 0;
   struct net_device *dev = (struct net_device *) device;

   if (!dev->change_mtu) { // 3Com doesn't even register change_mtu!
      VMKLNX_DEBUG(0, "Changing MTU not supported by device.");
      return VMK_NOT_SUPPORTED;
   }

   VMKAPI_MODULE_CALL(dev->module_id, ret, dev->change_mtu, dev, new_mtu);

   if (ret == 0) {
      VMKLNX_DEBUG(0, "%s: MTU changed to %d", dev->name, new_mtu);
   } else {
      VMKLNX_DEBUG(0, "%s: Failed to change MTU to %d", dev->name, new_mtu);
      return VMK_FAILURE;
   }

   return VMK_OK;
}

/*
 *----------------------------------------------------------------------------
 *
 *  NICSetLinkStatus --
 *    Set NIC hardware speed and duplex.
 *
 *  Results:
 *    VMK_OK or failure code.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
NICSetLinkStatus(void *clientData, vmk_UplinkLinkInfo *linkInfo)
{
   struct net_device *dev = (struct net_device *)clientData;
   struct ethtool_cmd cmd;
   uint32_t result; 
      
   /* get link speed and duplexity */
   cmd.cmd = ETHTOOL_SSET;
   cmd.speed = linkInfo->linkSpeed;
   cmd.duplex = linkInfo->fullDuplex;
   if (cmd.speed != 0) {
      cmd.autoneg = 0;
   } else {
      cmd.autoneg = 1;
      cmd.advertising =
            ADVERTISED_100baseT_Full |
            ADVERTISED_100baseT_Half |
            ADVERTISED_10baseT_Full |
            ADVERTISED_10baseT_Half |
            ADVERTISED_1000baseT_Full |
            ADVERTISED_1000baseT_Half;
   }

   /*
    * We call ethtool_ops directly to bypass copy_from_user(),
    * which doesn't handle in-kernel buffers (except for BH callers).
    *
    * See ethtool_set_settings()
    */
   if (!dev->ethtool_ops || !dev->ethtool_ops->set_settings) {
       return VMK_NOT_SUPPORTED;
   }

   VMKAPI_MODULE_CALL(dev->module_id, result, dev->ethtool_ops->set_settings,
                      dev, &cmd);
   dev_put(dev);
   return vmklnx_errno_to_vmk_return_status(result);
}

/*
 *---------------------------------------------------------------------------->  *
 *  NICResetDev --
 *
 *    Handler for resetting the device. If successful, the device state is
 *    reset and the link state should go down and then up.
 *
 *  Results:
 *    VMK_OK always.
 *
 *  Side effects:
 *    Link state should bounce as seen from physical switch.
 *
 *---------------------------------------------------------------------------->  */

static VMK_ReturnStatus
NICResetDev(void *clientData)
{
   struct net_device *dev = (struct net_device *)clientData;

   netif_tx_lock(dev);
   netif_stop_queue(dev);
   VMKAPI_MODULE_CALL_VOID(dev->module_id, dev->tx_timeout, dev);
   netif_tx_unlock(dev);

   return VMK_OK;
}


static inline VMK_ReturnStatus
marshall_to_vmknetq_features(vmknetddi_queueops_features_t features,
                             vmk_NetqueueFeatures *vmkfeatures)
{
   if (features & VMKNETDDI_QUEUEOPS_FEATURE_RXQUEUES) {
      vmk_NetqueueFeaturesRxQueuesSet(vmkfeatures);
   }
   if (features & VMKNETDDI_QUEUEOPS_FEATURE_TXQUEUES) {
      vmk_NetqueueFeaturesTxQueuesSet(vmkfeatures);
   }

   return VMK_OK;
}

static inline VMK_ReturnStatus
marshall_from_vmknetq_type(vmk_NetqueueQueueType vmkqtype,
                           vmknetddi_queueops_queue_t *qtype)
{
   if (vmk_NetqueueQueueTypeIsTx(vmkqtype)) {
      *qtype = VMKNETDDI_QUEUEOPS_QUEUE_TYPE_TX;
   } else if (vmk_NetqueueQueueTypeIsRx(vmkqtype)) {
      *qtype = VMKNETDDI_QUEUEOPS_QUEUE_TYPE_RX;
   } else {
      VMKLNX_DEBUG(0, "invalid vmkqueue type 0x%x", (uint32_t)vmkqtype);
      return VMK_FAILURE;
   }

   return VMK_OK;
}

static inline VMK_ReturnStatus
marshall_to_vmknetq_id(vmknetddi_queueops_queueid_t qid,
                       vmk_NetqueueQueueId *vmkqid)
{
   if ( !VMKNETDDI_QUEUEOPS_IS_TX_QUEUEID(qid) &&
        !VMKNETDDI_QUEUEOPS_IS_RX_QUEUEID(qid) ) {
      VMKLNX_DEBUG(0, "invalid queue id 0x%x", qid);
      return VMK_FAILURE;
   }

   if (VMKNETDDI_QUEUEOPS_IS_TX_QUEUEID(qid)) {
      vmk_NetqueueQueueMkTxQueueId(vmkqid, (u32)qid);
   } else if (VMKNETDDI_QUEUEOPS_IS_RX_QUEUEID(qid)) {
      vmk_NetqueueQueueMkRxQueueId(vmkqid, (u32)qid);
   }

   return VMK_OK;
}

static inline VMK_ReturnStatus
marshall_from_vmknetq_id(vmk_NetqueueQueueId vmkqid, 
                         vmknetddi_queueops_queueid_t *qid)
{
   if (unlikely(!vmk_NetqueueIsTxQueueId(vmkqid) &&
                !vmk_NetqueueIsRxQueueId(vmkqid))) {
      VMKLNX_DEBUG(0, "invalid vmk queue type 0x%"VMK_FMT64"x", vmkqid);
      return VMK_FAILURE;
   }

   *qid = (vmknetddi_queueops_queueid_t)vmk_NetqueueQueueIdVal(vmkqid);

   return VMK_OK;
}

static inline VMK_ReturnStatus
marshall_from_vmknetq_filter_type(vmk_NetqueueFilter *vmkfilter,
                                  vmknetddi_queueops_filter_t *filter)
{
   VMK_ASSERT(vmk_NetqueueFilterClassIsMacAddr(vmkfilter) || 
	  vmk_NetqueueFilterClassIsVlan(vmkfilter) ||
	  vmk_NetqueueFilterClassIsVlanMacAddr(vmkfilter));

  if (vmk_NetqueueFilterClassIsMacAddr(vmkfilter)) {
     filter->class = VMKNETDDI_QUEUEOPS_FILTER_MACADDR;
     memcpy(filter->u.macaddr, vmk_NetqueueFilterMacAddrGet(vmkfilter), 6);
  }
  else {
     VMKLNX_DEBUG(0, "unsupported vmk filter class");
     return VMK_NOT_SUPPORTED;
  }

  return VMK_OK;
}

static inline VMK_ReturnStatus
marshall_to_vmknetq_filter_id(vmknetddi_queueops_filterid_t fid, 
                              vmk_NetqueueFilterId *vmkfid)
{
   vmk_NetqueueMkFilterId(vmkfid, VMKNETDDI_QUEUEOPS_FILTERID_VAL(fid));
   return VMK_OK;
}

static VMK_ReturnStatus
marshall_from_vmknetq_filter_id(vmk_NetqueueFilterId vmkfid,
                                vmknetddi_queueops_filterid_t *fid)
{
   *fid = VMKNETDDI_QUEUEOPS_MK_FILTERID(vmk_NetqueueFilterIdVal(vmkfid));
   return VMK_OK;
}

static VMK_ReturnStatus
marshall_from_vmknetq_pri(vmk_NetqueuePriority vmkpri,
                                vmknetddi_queueops_tx_priority_t *pri)
{
   *pri = (vmknetddi_queueops_tx_priority_t)vmkpri;
   return VMK_OK;
}

/*
 *----------------------------------------------------------------------------
 *
 *  netqueue_op_get_version --
 *
 *    Get driver Netqueue version
 *
 *  Results:
 *    VMK_OK on success. VMK_NOT_SUPPORTED, if operation is not supported by 
 *    device. VMK_FAILURE, if operation fails.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netqueue_op_get_version(void *clientData, 
                        void *opArgs)
{
   int result;
   VMK_ReturnStatus ret;
   vmknetddi_queueop_get_version_args_t args;

   vmk_NetqueueOpGetVersionArgs *vmkargs = (vmk_NetqueueOpGetVersionArgs *)opArgs;
   struct net_device *dev = (struct net_device *)clientData;
   VMK_ASSERT(dev);

   if (dev->netqueue_ops) {
      VMKAPI_MODULE_CALL(dev->module_id, result, dev->netqueue_ops, 
                 VMKNETDDI_QUEUEOPS_OP_GET_VERSION, &args);
      if (result != 0) {
         ret = VMK_FAILURE;
      } else {
         vmk_NetqueueOpGetVersionArgsMajorSet(vmkargs,  args.major);
         vmk_NetqueueOpGetVersionArgsMinorSet(vmkargs,  args.minor);
         ret = VMK_OK;
      }
   } else {
      VMKLNX_DEBUG(0, "!dev->netqueue_ops");
      ret = VMK_NOT_SUPPORTED;
   }

   return ret;
}

/*
 *----------------------------------------------------------------------------
 *
 *  netqueue_op_get_features --
 *
 *    Get driver Netqueue features
 *
 *  Results:
 *    VMK_OK on success, VMK_FAILURE on error, VMK_NOT_SUPPORTED if
 *    Netqueue ops are not supprted by the driver
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netqueue_op_get_features(void *clientData, 
                         void *opArgs)
{
   int result;
   VMK_ReturnStatus ret;
   vmknetddi_queueop_get_features_args_t args;

   vmk_NetqueueOpGetFeaturesArgs *vmkargs = (vmk_NetqueueOpGetFeaturesArgs *)opArgs;
   struct net_device *dev = (struct net_device *)clientData;
   VMK_ASSERT(dev);
   args.netdev = dev;
   args.features = VMKNETDDI_QUEUEOPS_FEATURE_NONE;

   if (dev->netqueue_ops) {
      VMKAPI_MODULE_CALL(dev->module_id, result, dev->netqueue_ops, 
                 VMKNETDDI_QUEUEOPS_OP_GET_FEATURES, &args);
      if (result != 0) {
         ret = VMK_FAILURE;
      } else {
	 vmk_NetqueueFeatures features;
         ret = marshall_to_vmknetq_features(args.features, &features);
         VMK_ASSERT(ret == VMK_OK);
	 vmk_NetqueueOpGetFeaturesArgsFeatureSet(vmkargs, features);
      }
   } else {
      VMKLNX_DEBUG(0, "!dev->netqueue_ops");
      ret = VMK_NOT_SUPPORTED;
   }

   return ret;
}

/*
 *----------------------------------------------------------------------------
 *
 *  netqueue_op_get_queue_count --
 *
 *    Get count of tx or rx queues supprted by the driver
 *
 *  Results:
 *    VMK_OK on success, VMK_FAILURE on error, VMK_NOT_SUPPORTED if
 *    not supported
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netqueue_op_get_queue_count(void *clientData, 
                            void *opArgs)
{
   int result;
   VMK_ReturnStatus ret;
   vmknetddi_queueop_get_queue_count_args_t args;

   vmk_NetqueueOpGetQueueCountArgs *vmkargs = 
                                      (vmk_NetqueueOpGetQueueCountArgs *)opArgs;
   struct net_device *dev = (struct net_device *)clientData;
   VMK_ASSERT(dev);
   args.netdev = dev;

   if (marshall_from_vmknetq_type(vmk_NetqueueOpGetQueueCountArgsQueueTypeGet(vmkargs), 
                                  &args.type) != VMK_OK) {
      return VMK_FAILURE;
   }

   if (dev->netqueue_ops) {
      VMKAPI_MODULE_CALL(dev->module_id, result, dev->netqueue_ops, 
                         VMKNETDDI_QUEUEOPS_OP_GET_QUEUE_COUNT, &args);
      if (result != 0) {
         ret = VMK_FAILURE;
      } else {
         vmk_NetqueueOpGetQueueCountArgsCountSet(vmkargs, args.count);
         ret = VMK_OK;
      }
   } else {
      VMKLNX_DEBUG(0, "!dev->netqueue_ops");
      ret = VMK_NOT_SUPPORTED;
   }

   return ret;
}

/*
 *----------------------------------------------------------------------------
 *
 *  netqueue_op_get_filter_count --
 *
 *    Get number of rx filters supprted by the driver
 *
 *  Results:
 *    VMK_OK on success, VMK_FAILURE on error, VMK_NOT_SUPPORTED if
 *    not supported
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netqueue_op_get_filter_count(void *clientData, 
                             void *opArgs)
{
   int result;
   VMK_ReturnStatus ret;
   vmknetddi_queueop_get_filter_count_args_t args;

   vmk_NetqueueOpGetFilterCountArgs *vmkargs = (vmk_NetqueueOpGetFilterCountArgs *)opArgs;
   struct net_device *dev = (struct net_device *)clientData;
   VMK_ASSERT(dev);
   args.netdev = dev;

   if (marshall_from_vmknetq_type(vmk_NetqueueOpGetFilterCountArgsQueueTypeGet(vmkargs), 
                                  &args.type) != VMK_OK) {
      return VMK_FAILURE;
   }

   if (dev->netqueue_ops) {
      VMKAPI_MODULE_CALL(dev->module_id, result, dev->netqueue_ops, 
                 VMKNETDDI_QUEUEOPS_OP_GET_FILTER_COUNT, &args);
      if (result != 0) {
         ret = VMK_FAILURE;
      } else {
	 vmk_NetqueueOpGetFilterCountArgsCountSet(vmkargs, args.count);
         ret = VMK_OK;
      }
   } else {
      VMKLNX_DEBUG(0, "!dev->netqueue_ops");
      ret = VMK_NOT_SUPPORTED;
   }

   return ret;
}

/*
 *----------------------------------------------------------------------------
 *
 *  embed_queueid_queue_mapping --
 *
 *    Embed linux queue_mapping in queueid.
 *
 *    This is a HACK, so that we don't have to lookup linux queue mappings 
 *    for every data packet.
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static inline void
embed_queueid_queue_mapping(struct net_device *dev,
                            u16 queue_mapping, 
                            vmknetddi_queueops_queueid_t *queueid)
{
   *queueid = VMKNETDDI_QUEUEOPS_TX_QUEUEID_SET_QIDX(*queueid, queue_mapping);
}

/*
 *----------------------------------------------------------------------------
 *
 *  get_embedded_queue_mapping --
 *
 *    Get queue linux queue_mapping embedded in the queueid. 
 *
 *    This is a HACK, so that we don't have to lookup linux queue mappings 
 *    for every data packet.
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static inline u16
get_embedded_queue_mapping(vmknetddi_queueops_queueid_t queueid)
{
   return VMKNETDDI_QUEUEOPS_TX_QUEUEID_GET_QIDX(queueid);
}

/*
 *----------------------------------------------------------------------------
 *
 *  netqueue_op_alloc_queue --
 *
 *    Call driver netqueue_op for allocating queue
 *
 *  Results:
 *    VMK_OK on success, VMK_FAILURE on error, VMK_NOT_SUPPORTED if
 *    not supported
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netqueue_op_alloc_queue(void *clientData, 
                        void *opArgs)
{
   int result;
   VMK_ReturnStatus ret;
   vmknetddi_queueop_alloc_queue_args_t args;   
   vmk_NetqueueOpAllocQueueArgs *vmkargs = (vmk_NetqueueOpAllocQueueArgs *)opArgs;
   struct net_device *dev = (struct net_device *)clientData;
   vmk_NetqueueQueueType qtype = vmk_NetqueueOpAllocQueueArgsQueueTypeGet(vmkargs);

   VMK_ASSERT(dev);

   args.netdev = dev;
   args.napi = NULL;
   args.queue_mapping = 0;

   if (!vmk_NetqueueQueueTypeIsRx(qtype) && 
       !vmk_NetqueueQueueTypeIsTx(qtype)) {
      VMKLNX_DEBUG(0, "invalid vmkqueue type 0x%x", qtype);
      return VMK_FAILURE;
   }

   ret = marshall_from_vmknetq_type(qtype, &args.type);
   VMK_ASSERT(ret == VMK_OK);
   if (ret != VMK_OK) {
      return VMK_FAILURE;
   }

   if (dev->netqueue_ops) {
      VMKAPI_MODULE_CALL(dev->module_id, result, dev->netqueue_ops, 
                         VMKNETDDI_QUEUEOPS_OP_ALLOC_QUEUE, &args);
      if (result != 0) {
         ret = VMK_FAILURE;
      } else {
         vmk_NetqueueQueueId qid;

         if (vmk_NetqueueQueueTypeIsTx(qtype)) {
            embed_queueid_queue_mapping(dev, args.queue_mapping, &args.queueid);
         } else {
            VMK_ASSERT(vmk_NetqueueQueueTypeIsRx(qtype));
            if (args.napi != NULL) {
               vmk_NetqueueOpAllocQueueArgsWorldletSet(vmkargs, args.napi->worldlet);
            }
         }

         ret = marshall_to_vmknetq_id(args.queueid, &qid);
         VMK_ASSERT(ret == VMK_OK);
         if (unlikely(ret != VMK_OK)) {
            vmknetddi_queueop_free_queue_args_t freeargs;
            freeargs.netdev = dev;
            freeargs.queueid = qid;
            VMKAPI_MODULE_CALL(dev->module_id, result, dev->netqueue_ops, 
                               VMKNETDDI_QUEUEOPS_OP_FREE_QUEUE, &freeargs);

            VMKLNX_DEBUG(0, "invalid qid. freeing allocated queue");
         } else {
            vmk_NetqueueOpAllocQueueArgsQueueIdSet(vmkargs, qid);
         }
      }
   } else {
      VMKLNX_DEBUG(0, "!dev->netqueue_ops");
      ret = VMK_NOT_SUPPORTED;
   }

   return ret;
}

/*
 *----------------------------------------------------------------------------
 *
 *  netqueue_op_free_queue --
 *
 *    Free queue
 *
 *  Results:
 *    VMK_OK on success, VMK_FAILURE on error, VMK_NOT_SUPPORTED if
 *    not supported
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netqueue_op_free_queue(void *clientData,
                       void *opArgs)
{
   int result;
   VMK_ReturnStatus ret;
   vmknetddi_queueop_free_queue_args_t args;

   vmk_NetqueueOpFreeQueueArgs *vmkargs = (vmk_NetqueueOpFreeQueueArgs *)opArgs;
   vmk_NetqueueQueueId vmkqid = vmk_NetqueueOpFreeQueueArgsQueueIdGet(vmkargs);
   struct net_device *dev = (struct net_device *)clientData;
   VMK_ASSERT(dev);
   args.netdev = dev;

   ret = marshall_from_vmknetq_id(vmkqid, &args.queueid);
   VMK_ASSERT(ret == VMK_OK);
   if (ret != VMK_OK) {
      return ret;
   }
   
   if (dev->netqueue_ops) {
      VMKAPI_MODULE_CALL(dev->module_id, result, dev->netqueue_ops, 
                 VMKNETDDI_QUEUEOPS_OP_FREE_QUEUE, &args);
      if (result != 0) {
         ret = VMK_FAILURE;
      } else {
         ret = VMK_OK;
      }
   } else {
      VMKLNX_DEBUG(0, "!dev->netqueue_ops");
      ret = VMK_NOT_SUPPORTED;
   }

   return ret;
}

/*
 *----------------------------------------------------------------------------
 *
 *  netqueue_op_get_queue_vector --
 *
 *    Get interrupt vector for the queue
 *
 *  Results:
 *    VMK_OK on success, VMK_FAILURE on error, VMK_NOT_SUPPORTED if
 *    not supported
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netqueue_op_get_queue_vector(void *clientData, 
                             void *opArgs)
{
   int result;
   VMK_ReturnStatus ret;
   vmknetddi_queueop_get_queue_vector_args_t args;

   vmk_NetqueueOpGetQueueVectorArgs *vmkargs = (vmk_NetqueueOpGetQueueVectorArgs *)opArgs;
   struct net_device *dev = (struct net_device *)clientData;
   VMK_ASSERT(dev);
   args.netdev = dev;

   ret = marshall_from_vmknetq_id(vmk_NetqueueOpGetQueueVectorArgsQueueIdGet(vmkargs), 
                                  &args.queueid);
   VMK_ASSERT(ret == VMK_OK);
   if (ret != VMK_OK) {
      return ret;
   }

   if (dev->netqueue_ops) {
      VMKAPI_MODULE_CALL(dev->module_id, result, dev->netqueue_ops, 
                 VMKNETDDI_QUEUEOPS_OP_GET_QUEUE_VECTOR, &args);
      if (result != 0) {
         ret = VMK_FAILURE;
      } else {
	 vmk_NetqueueOpGetQueueVectorArgsVectorSet(vmkargs, args.vector);
         ret = VMK_OK;
      }
   } else {
      VMKLNX_DEBUG(0, "!dev->netqueue_ops");
      ret = VMK_NOT_SUPPORTED;
   }

   return ret;
}


/*
 *----------------------------------------------------------------------------
 *
 *  netqueue_op_get_default_queue --
 *
 *    Get default queue for tx/rx operations
 *
 *  Results:
 *    VMK_OK on success, VMK_FAILURE on error, VMK_NOT_SUPPORTED if
 *    not supported
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netqueue_op_get_default_queue(void *clientData, 
                              void *opArgs)
{
   int result;
   VMK_ReturnStatus ret;
   vmknetddi_queueop_get_default_queue_args_t args;
    vmk_NetqueueOpGetDefaultQueueArgs *vmkargs = 
      (vmk_NetqueueOpGetDefaultQueueArgs *)opArgs;
   struct net_device *dev = (struct net_device *)clientData;
   vmk_NetqueueQueueType qtype = vmk_NetqueueOpGetDefaultQueueArgsQueueTypeGet(vmkargs);

   VMK_ASSERT(dev);
   args.netdev = dev;
   args.napi = NULL; 
   args.queue_mapping  = 0;

   ret = marshall_from_vmknetq_type(qtype, &args.type);
   VMK_ASSERT(ret == VMK_OK);
   if (ret != VMK_OK) {
      return ret;
   }

   if (dev->netqueue_ops) {
      VMKAPI_MODULE_CALL(dev->module_id, result, dev->netqueue_ops, 
                 VMKNETDDI_QUEUEOPS_OP_GET_DEFAULT_QUEUE, &args);
      if (result != 0) {
         ret = VMK_FAILURE;
      } else {
	 vmk_NetqueueQueueId qid;

         if (vmk_NetqueueQueueTypeIsTx(qtype)) {
            embed_queueid_queue_mapping(dev, args.queue_mapping, &args.queueid);
         } else {
            VMK_ASSERT(vmk_NetqueueQueueTypeIsRx(qtype));
            if (args.napi != NULL) {
               vmk_NetqueueOpGetDefaultQueueArgsWorldletSet(vmkargs, args.napi->worldlet);
            }
         }

         ret = marshall_to_vmknetq_id(args.queueid, &qid);
         VMK_ASSERT(ret == VMK_OK);
         if (ret == VMK_OK) {
            vmk_NetqueueOpGetDefaultQueueArgsQueueIdSet(vmkargs, qid);
         } else {
            // no state to cleanup, just fall through
         }
      }
   } else {
      VMKLNX_DEBUG(0, "!dev->netqueue_ops");
      ret = VMK_NOT_SUPPORTED;
   }

   return ret;
}


/*
 *----------------------------------------------------------------------------
 *
 *  netqueue_op_apply_rx_filter --
 *
 *    Apply rx filter on queue
 *
 *  Results:
 *    VMK_OK on success, VMK_FAILURE on error, VMK_NOT_SUPPORTED if
 *    not supported
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netqueue_op_apply_rx_filter(void *clientData, 
                            void *opArgs)
{
   int result;
   VMK_ReturnStatus ret;
   vmknetddi_queueop_apply_rx_filter_args_t args;

   vmk_NetqueueOpApplyRxFilterArgs *vmkargs = 
      (vmk_NetqueueOpApplyRxFilterArgs *)opArgs;
   struct net_device *dev = (struct net_device *)clientData;
   VMK_ASSERT(dev);
   args.netdev = dev;

   ret = marshall_from_vmknetq_id(vmk_NetqueueOpApplyRxFilterArgsQueueIdGet(vmkargs), 
                                  &args.queueid);
   VMK_ASSERT(ret == VMK_OK);
   if (ret != VMK_OK) {
      return ret;
   }

   ret = marshall_from_vmknetq_filter_type(vmk_NetqueueOpApplyRxFilterArgsFilterGet(vmkargs), 
                                           &args.filter);
   VMK_ASSERT(ret == VMK_OK);
   if (ret != VMK_OK) {
      return ret;
   }

   if (dev->netqueue_ops) {
      VMKAPI_MODULE_CALL(dev->module_id, result, dev->netqueue_ops, 
                 VMKNETDDI_QUEUEOPS_OP_APPLY_RX_FILTER, &args);
      if (result != 0) {
         VMKLNX_DEBUG(0, "vmknetddi_queueops_apply_rx_filter returned %d", result);
         ret = VMK_FAILURE;
      } else {
	 vmk_NetqueueFilterId fid;

         ret = marshall_to_vmknetq_filter_id(args.filterid, &fid);
         VMK_ASSERT(ret == VMK_OK);         
	 vmk_NetqueueOpApplyRxFilterArgsFilterIdSet(vmkargs, fid);
      }
   } else {
      VMKLNX_DEBUG(0, "!dev->netqueue_ops");
      ret = VMK_NOT_SUPPORTED;
   }

   return ret;
}

/*
 *----------------------------------------------------------------------------
 *
 *  netqueue_op_remove_rx_filter --
 *
 *    Remove rx filter from queue
 *
 *  Results:
 *    VMK_OK on success, VMK_FAILURE on error, VMK_NOT_SUPPORTED if
 *    not supported
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netqueue_op_remove_rx_filter(void *clientData, 
                             void *opArgs)
{
   int result;
   VMK_ReturnStatus ret;
   vmknetddi_queueop_remove_rx_filter_args_t args;

   vmk_NetqueueOpRemoveRxFilterArgs *vmkargs = 
      (vmk_NetqueueOpRemoveRxFilterArgs *)opArgs;
   struct net_device *dev = (struct net_device *)clientData;
   VMK_ASSERT(dev);
   args.netdev = dev;

   ret = marshall_from_vmknetq_id(vmk_NetqueueOpRemoveRxFilterArgsQueueIdGet(vmkargs), 
                                  &args.queueid);
   VMK_ASSERT(ret == VMK_OK);
   if (ret != VMK_OK) {
      return ret;
   }

   ret = marshall_from_vmknetq_filter_id(vmk_NetqueueOpRemoveRxFilterArgsFilterIdGet(vmkargs), 
                                         &args.filterid);
   VMK_ASSERT(ret == VMK_OK);
   if (ret != VMK_OK) {
      return ret;
   }

   if (dev->netqueue_ops) {
      VMKAPI_MODULE_CALL(dev->module_id, result, dev->netqueue_ops, 
                 VMKNETDDI_QUEUEOPS_OP_REMOVE_RX_FILTER, &args);
      if (result != 0) {
         VMKLNX_DEBUG(0, "vmknetddi_queueops_remove_rx_filter returned %d", 
                      result);
         ret = VMK_FAILURE;
      } else {
         ret = VMK_OK;
      }
   } else {
      VMKLNX_DEBUG(0, "!dev->netqueue_ops");
      ret = VMK_NOT_SUPPORTED;
   }

   return ret;
}

/*
 *----------------------------------------------------------------------------
 *
 *  netqueue_op_get_queue_stats --
 *
 *    Get queue statistics
 *
 *  Results:
 *    VMK_OK on success, VMK_FAILURE on error, VMK_NOT_SUPPORTED if
 *    not supported
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netqueue_op_get_queue_stats(void *clientData, 
                            void *opArgs)
{
   return VMK_NOT_SUPPORTED;
}

/*
 *----------------------------------------------------------------------------
 *
 *  netqueue_op_set_tx_priority --
 *
 *    Set tx queue priority
 *
 *  Results:
 *    VMK_OK on success, VMK_FAILURE on error, VMK_NOT_SUPPORTED if
 *    not supported
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netqueue_op_set_tx_priority(void *clientData, 
                            void *opArgs)
{
   VMK_ReturnStatus ret;
   vmk_NetqueueOpSetTxPriorityArgs *vmkargs = opArgs;
   vmknetddi_queueop_set_tx_priority_args_t args;
   struct net_device *dev = (struct net_device *)clientData;

   args.netdev = dev;

   ret = marshall_from_vmknetq_id(vmk_NetqueueOpSetTxPriorityArgsQueueIdGet(vmkargs),
                                  &args.queueid);
   VMK_ASSERT(ret == VMK_OK);
   if (ret != VMK_OK) {
      return ret;
   }

   ret = marshall_from_vmknetq_pri(vmk_NetqueueOpSetTxPriorityArgsPriorityGet(vmkargs),
                                   &args.priority);
   VMK_ASSERT(ret == VMK_OK);
   if (ret != VMK_OK) {
      return ret;
   }

   if (dev->netqueue_ops) {
      int result;

      VMKAPI_MODULE_CALL(dev->module_id, result, dev->netqueue_ops,
                         VMKNETDDI_QUEUEOPS_OP_SET_TX_PRIORITY, &args);
      if (result != 0) {
         return VMK_FAILURE;
      } else {
         return VMK_OK;
      }
   } else {
      VMKLNX_DEBUG(0, "!dev->netqueue_ops");
      return VMK_NOT_SUPPORTED;
   }
}

/*
 *----------------------------------------------------------------------------
 *
 *  netqueue_op_getset_state --
 *    Get and Set Netqueue Valid State
 *
 *  Results:
 *    The previous netqueue state
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
netqueue_op_getset_state(void *clientData, 
                         void *opArgs)
{
   vmk_NetqueueOpGetSetQueueStateArgs *vmkargs = 
      (vmk_NetqueueOpGetSetQueueStateArgs *)opArgs;

   vmk_Bool old_state;
    
   struct net_device *dev = (struct net_device *)clientData;
   VMK_ASSERT(dev);
    
   if (dev->netqueue_ops) {
      old_state = vmknetddi_queueops_getset_state(dev,
                                  vmk_NetqueueOpGetSetQueueStateArgsGetNewState(vmkargs));
      vmk_NetqueueOpGetSetQueueStateArgsSetOldState(vmkargs, old_state);           
      return VMK_OK;            
   } else {  
      VMKLNX_DEBUG(0, "!dev->netqueue_ops");
      return VMK_NOT_SUPPORTED;
   }
}

/*
 *----------------------------------------------------------------------------
 *
 *  LinNetNetqueueOpFunc --
 *    Netqueue ops handler for vmklinux
 *
 *  Results:
 *    VMK_OK on success, VMK_FAILURE on error 
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
LinNetNetqueueOpFunc(void *clientData, 
                     vmk_NetqueueOp op,
                     void *opArgs)
{
   
   if (vmk_NetqueueOpIsGetVersion(op)) {
      return netqueue_op_get_version(clientData, opArgs);
   }

   if (vmk_NetqueueOpIsGetFeatures(op)) {
      return netqueue_op_get_features(clientData, opArgs);
   }

   if (vmk_NetqueueOpIsQueueCount(op)) {
      return netqueue_op_get_queue_count(clientData, opArgs);
   }

   if (vmk_NetqueueOpIsFilterCount(op)) {
      return netqueue_op_get_filter_count(clientData, opArgs);
   }

   if (vmk_NetqueueOpIsAllocQueue(op)) {
      return netqueue_op_alloc_queue(clientData, opArgs);
   }

   if (vmk_NetqueueOpIsFreeQueue(op)) {
      return netqueue_op_free_queue(clientData, opArgs);
   }

   if (vmk_NetqueueOpIsGetQueueVector(op)) {
      return netqueue_op_get_queue_vector(clientData, opArgs);
   }

   if (vmk_NetqueueOpIsGetDefaultQueue(op)) {
      return netqueue_op_get_default_queue(clientData, opArgs);
   }

   if (vmk_NetqueueOpIsApplyRxFilter(op)) {
      return netqueue_op_apply_rx_filter(clientData, opArgs);
   }

   if (vmk_NetqueueOpIsRemoveRxFilter(op)) {
      return netqueue_op_remove_rx_filter(clientData, opArgs);
   }

   if (vmk_NetqueueOpIsGetQueueStats(op)) {
      return netqueue_op_get_queue_stats(clientData, opArgs);
   }

   if (vmk_NetqueueOpIsSetTxPriority(op)) {
      return netqueue_op_set_tx_priority(clientData, opArgs);
   }

   if (vmk_NetqueueOpIsGetSetState(op)) {
      return netqueue_op_getset_state(clientData, opArgs);
   }

   return VMK_FAILURE;
}
 
/*
 *----------------------------------------------------------------------------
 *
 * GetMACAddr --
 *
 *    Return the MAC address of the NIC.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
GetMACAddr(void *clientData, vmk_uint8 *macAddr)
{
   struct net_device *dev = (struct net_device *)clientData;

   memcpy(macAddr, dev->dev_addr, 6);

   return VMK_OK;
}

/*
 *----------------------------------------------------------------------------
 *
 * GetDeviceName --
 *
 *    Return the system name of corresponding device 
 *
 * Results:
 *    None
 *
 * Side effects:
 *    When the dev->pdev is NULL, we return the dev->name (pseudo device name) 
 *    instead  
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
GetDeviceName(void *device, vmk_UplinkDeviceName *devName)
{
   struct net_device *dev = device;

   memset(devName->buffer, 0, devName->bufferSize);

   /* Check if the associated pdev is NULL (a pseudo device) */
   if (dev->pdev) {
      memcpy(devName->buffer, dev->pdev->name,
	     min(sizeof(dev->pdev->name), (size_t)(devName->bufferSize - 1)));
   } else {
      memcpy(devName->buffer, dev->name,
	     min(sizeof(dev->name), (size_t)(devName->bufferSize - 1)));
   }

   devName->stringLength = strlen((char *)devName->buffer);

   return VMK_OK;
}


/*
 *----------------------------------------------------------------------------
 *
 * GetDeviceStats --
 *
 *    Return the stats of corresponding device.
 *
 *    There are two kinds of statistics :
 *
 *    - General statistics : retrieved by struct net_device_stats enclosed in
 *                           the struct net_device.
 *                           These stats are common to all device and stored in
 *                           stats array.
 *
 *    - Specific statistics : retrieved by ethtool functions provided by driver.
 *                            A global string is created in gtrings containing all
 *                            formatted statistics.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus 
GetDeviceStats(void *device, vmk_UplinkStats *stats)
{
   struct net_device *dev = device;
   struct net_device_stats *st = NULL; 
   struct ethtool_ops *ops = dev->ethtool_ops;
   struct ethtool_stats stat;
   u64 *data;
   char *buf;
   char *pbuf;
   int idx = 0;
   int pidx = 0;

   if (dev->get_stats) {
      VMKAPI_MODULE_CALL(dev->module_id, st, dev->get_stats, dev);
   }
   
   if (!st) {
      return VMK_FAILURE;
   } else {
      stats->rxpkt = st->rx_packets;
      stats->txpkt = st->tx_packets;
      stats->rxbytes = st->rx_bytes;
      stats->txbytes = st->tx_bytes;
      stats->rxerr = st->rx_errors;
      stats->txerr = st->tx_errors;
      stats->rxdrp = st->rx_dropped;
      stats->txdrp = st->tx_dropped;
      stats->mltcast = st->multicast;
      stats->col = st->collisions;
      stats->rxlgterr = st->rx_length_errors;
      stats->rxoverr = st->rx_over_errors;
      stats->rxcrcerr = st->rx_crc_errors;
      stats->rxfrmerr = st->rx_frame_errors;
      stats->rxfifoerr = st->rx_fifo_errors;
      stats->rxmisserr = st->rx_missed_errors;
      stats->txaborterr = st->tx_aborted_errors;
      stats->txcarerr = st->tx_carrier_errors;
      stats->txfifoerr = st->tx_fifo_errors;
      stats->txhearterr = st->tx_heartbeat_errors;
      stats->txwinerr = st->tx_window_errors;  
      stats->intrxpkt = dev->linnet_rx_packets;
      stats->inttxpkt = dev->linnet_tx_packets;
      stats->intrxdrp = dev->linnet_rx_dropped;  
      stats->inttxdrp = dev->linnet_tx_dropped;  
      stats->intpktcompl = dev->linnet_pkt_completed;
   }

   if (!ops || 
       !ops->get_ethtool_stats ||
       (!ops->get_stats_count && !ops->get_sset_count) ||
       !ops->get_strings) {
      goto done;
   }
   
   if (ops->get_stats_count) {
      /* 2.6.18 network drivers method to retrieve the number of stats */
      VMKAPI_MODULE_CALL(dev->module_id, stat.n_stats, ops->get_stats_count, dev);
   } else {
      /* 2.6.18+ network drivers method to retrieve the number of stats */
      VMKAPI_MODULE_CALL(dev->module_id, stat.n_stats, ops->get_sset_count, dev, ETH_SS_STATS);
   }
   data = kmalloc(stat.n_stats * sizeof(u64), GFP_ATOMIC);
   pbuf = buf = kmalloc(stat.n_stats * ETH_GSTRING_LEN, GFP_ATOMIC);
   
   if (!data) {
      goto done;
   }

   if (!buf) {
      kfree(data);
      goto done;
   }
   
   VMKAPI_MODULE_CALL_VOID(dev->module_id, ops->get_ethtool_stats, dev, &stat, data);
   VMKAPI_MODULE_CALL_VOID(dev->module_id, ops->get_strings, dev, ETH_SS_STATS, (vmk_uint8 *)buf);

   stats->privateStats.buffer[pidx++] = '\n';
   for (; (pidx < stats->privateStats.bufferSize - 1) && (idx < stat.n_stats); idx++) {
      char tmp[128];
       
      snprintf(tmp, 128, "   %s : %lld\n", pbuf, data[idx]);
      memcpy(stats->privateStats.buffer + pidx, tmp, min(strlen(tmp), 
             (size_t)(stats->privateStats.bufferSize - pidx - 1)));
	 
      pidx += min(strlen(tmp),
                  (size_t)stats->privateStats.bufferSize - pidx - 1);
      pbuf += ETH_GSTRING_LEN;
   }
   
   stats->privateStats.buffer[pidx] = 0;
   stats->privateStats.stringLength = strlen((char *)stats->privateStats.buffer);
   
   kfree(data);
   kfree(buf);
   
 done:

   return VMK_OK;
}


/*
 *----------------------------------------------------------------------------
 *
 * GetDriverInfo --
 *
 *    Return informations of the corresponding device's driver.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus 
GetDriverInfo(void *device, vmk_UplinkDriverInfo *driverInfo)
{
   struct net_device *dev = device;  
   struct ethtool_ops *ops = dev->ethtool_ops;
   struct ethtool_drvinfo drv;
   VMK_ReturnStatus status;

   snprintf((char *)driverInfo->moduleInterface.buffer, driverInfo->moduleInterface.bufferSize,  "vmklinux26");

   if (!ops || !ops->get_drvinfo) {
      sprintf((char *)driverInfo->driver.buffer, "(none)");
      sprintf((char *)driverInfo->version.buffer, "(none)");
      sprintf((char *)driverInfo->firmwareVersion.buffer, "(none)");      
      status = VMK_FAILURE;
   } else {
      memset(&drv, 0, sizeof(struct ethtool_drvinfo));
     
      VMKAPI_MODULE_CALL_VOID(dev->module_id, ops->get_drvinfo, dev, &drv); 
      
      memset(driverInfo->driver.buffer, 0, driverInfo->driver.bufferSize);
      memset(driverInfo->version.buffer, 0, driverInfo->version.bufferSize);
      memset(driverInfo->firmwareVersion.buffer, 0, driverInfo->firmwareVersion.bufferSize);
      
      memcpy(driverInfo->driver.buffer, drv.driver, 
	     min((size_t)(driverInfo->driver.bufferSize - 1),
                 sizeof(drv.driver)));
      memcpy(driverInfo->version.buffer, drv.version,
	     min((size_t)(driverInfo->version.bufferSize - 1),
                 sizeof(drv.version)));
      memcpy(driverInfo->firmwareVersion.buffer, drv.fw_version,
	     min((size_t)(driverInfo->firmwareVersion.bufferSize - 1),
                 sizeof(drv.fw_version)));   
      
      status = VMK_OK;
   }      

   driverInfo->driver.stringLength = strlen((char *)driverInfo->driver.buffer);
   driverInfo->version.stringLength = strlen((char *)driverInfo->version.buffer);
   driverInfo->firmwareVersion.stringLength = strlen((char *)driverInfo->firmwareVersion.buffer);
   driverInfo->moduleInterface.stringLength = strlen((char *)driverInfo->moduleInterface.buffer);

   return status;
}

/*
 *----------------------------------------------------------------------------
 *
 * wolLinuxCapsToVmkCaps --
 *
 *      translate from VMK wol caps to linux caps
 *
 * Results:
 *      vmk_wolCaps
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */
static vmk_UplinkWolCaps
wolLinuxCapsToVmkCaps(vmk_uint32 caps)
{
   vmk_UplinkWolCaps vmkCaps = 0;

   if (caps & WAKE_PHY) {
      vmkCaps |= VMK_UPLINK_WAKE_ON_PHY;
   }
   if (caps & WAKE_UCAST) {
      vmkCaps |= VMK_UPLINK_WAKE_ON_UCAST;
   }
   if (caps & WAKE_MCAST) {
      vmkCaps |= VMK_UPLINK_WAKE_ON_MCAST;
   }
   if (caps & WAKE_BCAST) {
      vmkCaps |= VMK_UPLINK_WAKE_ON_BCAST;
   }
   if (caps & WAKE_ARP) {
      vmkCaps |= VMK_UPLINK_WAKE_ON_ARP;
   }
   if (caps & WAKE_MAGIC) {
      vmkCaps |= VMK_UPLINK_WAKE_ON_MAGIC;
   }
   if (caps & WAKE_MAGICSECURE) {
      vmkCaps |= VMK_UPLINK_WAKE_ON_MAGICSECURE;
   }

   return vmkCaps;
}

/*
 *----------------------------------------------------------------------------
 *
 * GetWolState --
 *
 *      use the ethtool interface to populate a vmk_UplinkWolState 
 *
 * Results:
 *      vmk_UplinkWolState
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
GetWolState(void *device, vmk_UplinkWolState *wolState)
{
   struct net_device *dev = device;
   struct ethtool_ops *ops = dev->ethtool_ops;

   if (!ops || !ops->get_wol) {
      return VMK_NOT_SUPPORTED;
   } else {
      struct ethtool_wolinfo wolInfo[1];
      VMK_ReturnStatus status = VMK_OK;

      memset(wolInfo, 0, sizeof(wolInfo));
      VMKAPI_MODULE_CALL_VOID(dev->module_id, ops->get_wol, dev, wolInfo); 

      wolState->supported = wolLinuxCapsToVmkCaps(wolInfo->supported);
      wolState->enabled = wolLinuxCapsToVmkCaps(wolInfo->wolopts);

      if (strlen((char *)wolInfo->sopass) > 0) {
         vmk_uint32 length = strlen((char *)wolInfo->sopass);

         if (wolState->secureONPassword.buffer == 0 ||
             (wolState->secureONPassword.bufferSize < 1)) {
            return VMK_FAILURE;
         }
         memset(wolState->secureONPassword.buffer, 0,
                wolState->secureONPassword.bufferSize);

         wolState->secureONPassword.stringLength = length;
         length++;
         if (length > wolState->secureONPassword.bufferSize) {
            status = VMK_LIMIT_EXCEEDED; // truncated
            length = wolState->secureONPassword.bufferSize;
            wolState->secureONPassword.stringLength = length;
         }
         memcpy(wolState->secureONPassword.buffer, wolInfo->sopass, length);
      }
      return status;
   }
}

/*
 *----------------------------------------------------------------------------
 *
 * wolVmkCapsToLinuxCaps --
 *
 *      translate from VMK wol caps to linux caps
 *
 * Results:
 *      linux wol cap bits
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */
static vmk_uint32
wolVmkCapsToLinuxCaps(vmk_UplinkWolCaps vmkCaps)
{
   vmk_uint32 caps = 0;

   if (vmkCaps & VMK_UPLINK_WAKE_ON_PHY) {
      caps |= WAKE_PHY;
   }
   if (vmkCaps & VMK_UPLINK_WAKE_ON_UCAST) {
      caps |= WAKE_UCAST;
   }
   if (vmkCaps & VMK_UPLINK_WAKE_ON_MCAST) {
      caps |= WAKE_MCAST;
   }
   if (vmkCaps & VMK_UPLINK_WAKE_ON_BCAST) {
      caps |= WAKE_BCAST;
   }
   if (vmkCaps & VMK_UPLINK_WAKE_ON_ARP) {
      caps |= WAKE_ARP;
   }
   if (vmkCaps & VMK_UPLINK_WAKE_ON_MAGIC) {
      caps |= WAKE_MAGIC;
   }
   if (vmkCaps & VMK_UPLINK_WAKE_ON_MAGICSECURE) {
      caps |= WAKE_MAGICSECURE;
   }

   return caps;
}

/*
 *----------------------------------------------------------------------------
 *
 * SetWolState --
 *
 *      set wol state via ethtool from a vmk_UplinkWolState struct
 *
 * Results;
 *      VMK_OK, various other failues
 *
 * Side effects:
 *      can set state within the pNic
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
SetWolState(void *device, vmk_UplinkWolState *wolState)
{
   struct net_device *dev = device;
   struct ethtool_ops *ops = dev->ethtool_ops;
   VMK_ReturnStatus status = VMK_FAILURE;

   if (!ops || !ops->set_wol) {
      return VMK_NOT_SUPPORTED;
   } else {
      struct ethtool_wolinfo wolInfo[1];
      int error;

      wolInfo->supported = wolVmkCapsToLinuxCaps(wolState->supported);
      wolInfo->wolopts = wolVmkCapsToLinuxCaps(wolState->enabled);

      if (wolState->secureONPassword.buffer != 0 &&
          (wolState->secureONPassword.stringLength > 0)) {
         vmk_uint32 length = wolState->secureONPassword.stringLength + 1;
         if (length > sizeof(wolInfo->sopass)) {
            length = sizeof(wolInfo->sopass);
         }
         memcpy(wolInfo->sopass, wolState->secureONPassword.buffer, length);
      }
      VMKAPI_MODULE_CALL(dev->module_id, error, ops->set_wol, dev, wolInfo);
      if (error == 0) {
         status = VMK_OK;
      }
   }

   return status;
}

/*
 *----------------------------------------------------------------------------
 *
 *  GetNICState --
 *    For the given NIC, return device resource information such as its
 *    irq, memory range, flags and so on.
 *
 *  Results:
 *    VMK_OK if successful. Other VMK_ReturnStatus codes returned on failure.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
GetNICState(void *clientData, vmk_UplinkStates *states)
{
   if (clientData && states) {
      struct net_device *dev = (struct net_device *)clientData;

      if (test_bit(__LINK_STATE_PRESENT, &dev->state)) {
	 *states |= VMK_UPLINK_STATE_PRESENT;
      }

      if (!test_bit(__LINK_STATE_XOFF, &dev->state)) {
	 *states |= VMK_UPLINK_STATE_QUEUE_OK;
      }

      if (!test_bit(__LINK_STATE_NOCARRIER, &dev->state)) {
	 *states |= VMK_UPLINK_STATE_LINK_OK;
      }

      if (test_bit(__LINK_STATE_START, &dev->state)) {
	 *states |= VMK_UPLINK_STATE_RUNNING;
      }

      if (dev->flags & IFF_UP) {
	 *states |= VMK_UPLINK_STATE_READY;
      }

      if (dev->flags & IFF_PROMISC) {
	 *states |= VMK_UPLINK_STATE_PROMISC;
      }

      if (dev->flags & IFF_BROADCAST) {
	 *states |= VMK_UPLINK_STATE_BROADCAST;
      }

      if (dev->flags & IFF_MULTICAST) {
	 *states |= VMK_UPLINK_STATE_MULTICAST;
      }

      return VMK_OK;
   } else {
      VMKLNX_DEBUG(0, "clientData: %p, states %p", clientData, states);
      return VMK_FAILURE;
   }
}

static VMK_ReturnStatus
GetNICMemResources(void *clientData, vmk_UplinkMemResources *resources)
{
   if (clientData && resources) {
      struct net_device *dev = (struct net_device *) clientData;

      resources->baseAddr = (void *)dev->base_addr;
      resources->memStart = (void *)dev->mem_start;
      resources->memEnd = (void *)dev->mem_end;
      resources->dma = dev->dma;

      return VMK_OK;
   } else {
      VMKLNX_DEBUG(0, "clientData: %p, resources %p", clientData, resources);
      return VMK_FAILURE;
   }
}

static VMK_ReturnStatus
GetNICPCIProperties(void *clientData, vmk_UplinkPCIInfo *pciInfo)
{
   if (clientData && pciInfo) {
      struct net_device *dev = (struct net_device *)clientData;
      struct pci_dev *pdev = dev->pdev;

      pciInfo->bus = pdev->bus->number;
      pciInfo->slot = PCI_SLOT(pdev->devfn);
      pciInfo->func = PCI_FUNC(pdev->devfn);
      pciInfo->vendor = pdev->vendor;
      pciInfo->device = pdev->device;
      pciInfo->hotPlug = VMK_TRUE;

      return VMK_OK;
   } else {
      VMKLNX_DEBUG(0, "clientData: %p, pciInfo %p", clientData, pciInfo);
      return VMK_FAILURE;
   }
}


/*
 *----------------------------------------------------------------------------
 *
 *  GetNICPanicInfo --
 *    Fill in vmk_UplinkPanicInfo struct.
 *
 *  Results:
 *    VMK_OK if properties filled in. VMK_FAILURE otherwise.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
GetNICPanicInfo(void *clientData,
                vmk_UplinkPanicInfo *intInfo)
{
   if (clientData && intInfo) {
      LinuxDevInfo *devInfo = Linux_FindDevice(clientData, VMK_FALSE);
      struct net_device* dev = (struct net_device*)clientData;

      if (!devInfo) {
	 return VMK_NOT_FOUND;
      }

      if (dev->pdev == NULL) {
         /*
          * Pseudo NIC does not support remote
          * debugging.
          */
         intInfo->vector = 0;
         intInfo->clientData = NULL;
      } else {
         intInfo->vector = devInfo->vector;
         intInfo->clientData = dev;
      }
      
      Linux_ReleaseDevice(devInfo);

      return VMK_OK;
   } else {
      VMKLNX_DEBUG(0, "clientData: %p, intInfo %p", clientData, intInfo);
      return VMK_FAILURE;
   }
}

/*
 *----------------------------------------------------------------------------
 *
 * FlushRxBuffers --
 *
 *    Called by the net debugger
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
FlushRxBuffers(void* clientData)
{
   struct net_device* dev = (struct net_device*)clientData;
   struct napi_struct* napi = NULL;
   VMKLNX_DEBUG(1, "client data, now net_device:%p", dev);

   list_for_each_entry(napi, &dev->napi_list, dev_list) {
      if (napi != NULL) {
         VMKLNX_DEBUG(1, "Calling Pkt List Rx Process on napi:%p", napi);
         VMK_ASSERT(napi->dev != NULL);
         vmk_PktListRxProcess(&napi->pktList, napi->dev->uplinkDev);
      }
   }

   return VMK_OK;
}

/*
 *----------------------------------------------------------------------------
 *
 *  PanicPoll --
 *    Poll for rx packets.
 *
 *  Results:
 *    result of napi->poll: the number of packets received and processed.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
static VMK_ReturnStatus
PanicPoll(void* clientData,
          vmk_uint32 budget,
          vmk_int32* workDone)
{
   struct net_device* dev = (struct net_device*)clientData;
   struct napi_struct* napi = NULL;
   vmk_int32 ret = 0;
   vmk_int32 modRet = 0;

   VMKLNX_DEBUG(1, "data:%p budget:%u", dev, budget);
   VMK_ASSERT(dev != NULL);
   
   list_for_each_entry(napi, &dev->napi_list, dev_list) {
      if (napi != NULL) {
         set_bit(NAPI_STATE_SCHED, &napi->state);
         VMKAPI_MODULE_CALL(napi->dev->module_id, modRet, napi->poll, napi,
                            budget);
         ret += modRet;
         VMKLNX_DEBUG(1, "poll:%p napi:%p budget:%u poll returned:%d",
                      napi->poll, napi, budget, ret);
      }
   }

   if (workDone != NULL) {
      *workDone = ret;
   }
   return VMK_OK;
}


static VMK_ReturnStatus 
GetWatchdogTimeoHitCnt(void *device, vmk_int16 *hitcnt)
{
   struct net_device *dev = device;

   *hitcnt = dev->watchdog_timeohit_cfg;
   
   return VMK_OK;
}

static VMK_ReturnStatus
SetWatchdogTimeoHitCnt(void *device, vmk_int16 hitcnt)
{
   struct net_device *dev = device;
   
   dev->watchdog_timeohit_cfg = hitcnt;
   
   return VMK_OK;
}

static VMK_ReturnStatus 
GetWatchdogTimeoStats(void *device, vmk_int16 *stats)
{
   struct net_device *dev = device;
   
   *stats = dev->watchdog_timeohit_stats;
   
   return VMK_OK;
}

static VMK_ReturnStatus 
GetWatchdogTimeoPanicMod(void *device, vmk_UplinkWatchdogPanicModState *state)
{
   struct net_device *dev = device;
   
   *state = dev->watchdog_timeohit_panic;
   
   return VMK_OK;
}

static VMK_ReturnStatus 
SetWatchdogTimeoPanicMod(void *device, vmk_UplinkWatchdogPanicModState state)
{
   struct net_device *dev = device;
   
   dev->watchdog_timeohit_panic = state;
   
   return VMK_OK;
}

#define NET_DEVICE_MAKE_PROPERTIES_FUNCTIONS     \
{                                                \
   getStates:          GetNICState,              \
   getMemResources:    GetNICMemResources,       \
   getPCIProperties:   GetNICPCIProperties,      \
   getPanicInfo:       GetNICPanicInfo,          \
   getMACAddr:         GetMACAddr,               \
   getName:            GetDeviceName,            \
   getStats:           GetDeviceStats,           \
   getDriverInfo:      GetDriverInfo,            \
   getWolState:        GetWolState,              \
   setWolState:        SetWolState,              \
}

#define NET_DEVICE_MAKE_WATCHDOG_FUNCTIONS       \
{                                                \
   getHitCnt:          GetWatchdogTimeoHitCnt,   \
   setHitCnt:          SetWatchdogTimeoHitCnt,   \
   getStats:           GetWatchdogTimeoStats,    \
   getPanicMod:        GetWatchdogTimeoPanicMod, \
   setPanicMod:        SetWatchdogTimeoPanicMod  \
}

#define NET_DEVICE_MAKE_NETQUEUE_FUNCTIONS       \
{                                                \
   netqOpFunc:         LinNetNetqueueOpFunc,     \
   netqXmit:           NULL,                     \
}

#define NET_DEVICE_MAKE_UPT_FUNCTIONS            \
{                                                \
   uptOpFunc:          NULL                      \
}

#define NET_DEVICE_MAKE_ESWITCH_FUNCTIONS        \
{                                                \
   eSwitchOpFunc:      NULL                      \
}

#define NET_DEVICE_MAKE_VLAN_FUNCTIONS           \
{                                                \
   setupVlan:         SetupVlanGroupDevice,      \
   removeVlan:        RemoveVlanGroupDevice      \
}

#define NET_DEVICE_MAKE_MTU_FUNCTIONS            \
{                                                \
   getMTU:            NICGetMTU,                 \
   setMTU:            NICSetMTU                  \
}

#define NET_DEVICE_MAKE_CORE_FUNCTIONS           \
{                                                \
   startTxImmediate:  DevStartTxImmediate,       \
   open:              OpenNetDev,                \
   close:             CloseNetDev,               \
   panicPoll:         PanicPoll,                 \
   flushRxBuffers:    FlushRxBuffers,            \
   ioctl:             IoctlNetDev,               \
   block:             BlockNetDev,               \
   unblock:           UnblockNetDev,             \
   setLinkStatus:     NICSetLinkStatus,          \
   reset:             NICResetDev                \
}

vmk_UplinkFunctions linNetFunctions = {
   coreFns:           NET_DEVICE_MAKE_CORE_FUNCTIONS,
   mtuFns:            NET_DEVICE_MAKE_MTU_FUNCTIONS,
   vlanFns:           NET_DEVICE_MAKE_VLAN_FUNCTIONS,
   propFns:           NET_DEVICE_MAKE_PROPERTIES_FUNCTIONS,
   watchdogFns:       NET_DEVICE_MAKE_WATCHDOG_FUNCTIONS,
   netqueueFns:       NET_DEVICE_MAKE_NETQUEUE_FUNCTIONS,
   uptFns:            NET_DEVICE_MAKE_UPT_FUNCTIONS,
   eSwitchFns:        NET_DEVICE_MAKE_ESWITCH_FUNCTIONS
};

static VMK_ReturnStatus
NicCharOpsIoctl(vmk_CharDevFdAttr *attr,
               unsigned int cmd,
               vmk_uintptr_t userData,
               vmk_IoctlCallerSize callerSize,
               vmk_int32 *result)
{
   struct net_device *dev;
   struct ifreq ifr;
   VMK_ReturnStatus status;

   if (copy_from_user(&ifr, (void *)userData, sizeof(ifr))) {
      return VMK_INVALID_ADDRESS;
   }

   dev = attr->clientData;
   if (!dev) {
      return VMK_NOT_FOUND;
   }

   status = netdev_ioctl(dev, cmd, &ifr, (uint32_t *) result, callerSize, VMK_FALSE);
   if (status == VMK_OK) {
      if (copy_to_user((void *)userData, &ifr, sizeof(ifr))) {
         return VMK_INVALID_ADDRESS;
      }
   }

   return status;
}

static struct net_device *
LinuxGetDevByNicMajor(int nicMajor)
{
   struct net_device *dev = NULL;

   if (nicMajor > 0) {
      read_lock(&dev_base_lock);

      dev = dev_base;
      while (dev) {
         if (dev->nicMajor == nicMajor) {
            dev_hold(dev);
            break;
         }

         dev = dev->next;
      }

      read_unlock(&dev_base_lock);
   }

   return dev;
}

static VMK_ReturnStatus
NicCharOpsOpen(vmk_CharDevFdAttr *attr)
{
   struct net_device *dev;

   rtnl_lock();

   dev = LinuxGetDevByNicMajor(attr->major);

   rtnl_unlock();

   if (!dev) {
      VMKLNX_INFO("Failed to find device %d", attr->major);
      return VMK_NOT_FOUND;
   }

   attr->clientData = dev;
   return VMK_OK;
}

static VMK_ReturnStatus
NicCharOpsClose(vmk_CharDevFdAttr *attr) 
{
   if (attr->clientData) {
      dev_put(attr->clientData);
   }

   return VMK_OK;
}


static vmk_CharDevOps nicCharOps = {
   NicCharOpsOpen,
   NicCharOpsClose,
   NicCharOpsIoctl,
   NULL,
   NULL,
   NULL,
   NULL
};

static int
register_nic_chrdev(struct net_device *dev)
{
   VMK_ReturnStatus status;

   if (dev->name) {
      status = vmk_CharDevRegister(dev->module_id, 0, 0, dev->name, &nicCharOps,
                                   &dev->nicMajor);
      if (status == VMK_OK) {
         return 0;
      } else if (status == VMK_BUSY) {
         return -EBUSY;
      }
   } else {
      printk("Device has no name\n");
   }

   return -EINVAL;
}

/*
 *----------------------------------------------------------------------------
 *
 *  LinNet_ConnectUplink --
 *
 *    Register the device with the vmkernel. Initializes various device fields
 *    and sets up PCI hotplug notification handlers.
 *
 *  Results:
 *    0 if successful, non-zero on failure.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
int
LinNet_ConnectUplink(struct net_device *dev, struct pci_dev *pdev)
{
   vmk_UplinkCapabilities capabilities = 0;
   vmk_ModuleID moduleID = VMK_INVALID_MODULE_ID;
   vmk_UplinkConnectInfo connectInfo;

   /*
    * We should only make this call once per net_device
    */
   VMK_ASSERT(dev->uplinkDev == NULL);

   /*
    * Driver should have made the association with
    * the PCI device via the macro SET_NETDEV_DEV()
    */
   VMK_ASSERT(dev->pdev == pdev);

   /*
    * Driver naming device already has the device name in net_device.
    */
   if (!dev->useDriverNamingDevice) {
      netdev_name_adapter(dev, pdev);
   }

   capabilities = netdev_query_capabilities(dev);

   moduleID = dev->module_id;

   VMK_ASSERT(moduleID != VMK_INVALID_MODULE_ID);

   connectInfo.devName.buffer = (vmk_uint8 *)dev->name;
   connectInfo.devName.bufferSize = sizeof(dev->name);
   connectInfo.devName.stringLength = strlen(dev->name);
   connectInfo.clientData = dev;
   connectInfo.moduleID = moduleID;
   connectInfo.functions = &linNetFunctions;
   connectInfo.maxSGLength = MAX_SKB_FRAGS + 1;
   connectInfo.cap = capabilities;   
   connectInfo.complHandler = vmklnx_net_skb_complete;
   connectInfo.complData = dev;

   /*
    * Pseudo NICs don't have real PCI properties
    */
   if (pdev == NULL) {
      connectInfo.functions->propFns.getPCIProperties = NULL;
   }

   if (dev->features & NETIF_F_HIDDEN_UPLINK) {
      connectInfo.flags = VMK_UPLINK_FLAG_HIDDEN;
   } else {
      connectInfo.flags = 0;
   }

   if (vmk_UplinkConnected(&connectInfo, &dev->uplinkDev) != VMK_OK) {
      goto fail;
   }  

   netdev_set_worldlets_name(dev);
   vmk_UplinkWorldletSet(dev->uplinkDev, dev->default_worldlet);

   Linux_RegisterDevice(dev, sizeof(struct net_device), dev->name, dev->name, NULL,
                        moduleID);

   dev->link_speed = -1;
   dev->full_duplex = 0;

   dev->link_state = VMK_UPLINK_LINK_DOWN;
   dev->watchdog_timeohit_cnt = 0;
   dev->watchdog_timeohit_cfg = VMK_UPLINK_WATCHDOG_HIT_CNT_DEFAULT;
   dev->watchdog_timeohit_stats = 0;
   dev->watchdog_timeohit_panic = VMK_UPLINK_WATCHDOG_PANIC_MOD_ENABLE;
   dev->watchdog_timeohit_period_start = jiffies;
   
   return register_nic_chrdev(dev);
   
 fail:
   return -1;
}

/*
 *----------------------------------------------------------------------------
 *
 *  vmklnx_netdev_high_dma_workaround --
 *    Make a copy of a skb buffer in low dma.
 *
 *  Results:
 *    If the copy succeeds then it releases the previous skb and
 *    returns the new one.
 *    If not it returns NULL.
 *
 *  Side effects:
 *    The skb buffer passed to the function might be released.
 *
 *----------------------------------------------------------------------------
 */
struct sk_buff *
vmklnx_netdev_high_dma_workaround(struct sk_buff *base)
{
   struct sk_buff *skb = skb_copy(base, GFP_ATOMIC);

   if (skb) {
      vmk_PktRelease(base->pkt);
   }

   return skb;
}

/*
 *----------------------------------------------------------------------------
 *
 *  vmklnx_netdev_high_dma_overflow --
 *    Check skb buffer's data are located beyond a specified dma limit.
 *
 *  Results:
 *    Returns TRUE if there is an overflow with the passed skb and FALSE
 *    otherwise.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
#define GB      (1024LL * 1024 * 1024)
int
vmklnx_netdev_high_dma_overflow(struct sk_buff *skb,
                                short gb_limit)
{
   uint64_t dma_addr;
   uint64_t dma_addr_limit;
   int idx_frags;
   int nr_frags;
   skb_frag_t *skb_frag;
   vmk_PktFrag pkt_frag;

   if (VMKLNX_STRESS_DEBUG_COUNTER(stressNetIfForceHighDMAOverflow)) {
      return VMK_TRUE;
   }

   dma_addr_limit = (uint64_t) gb_limit * GB;
   if (dma_addr_limit > max_phys_addr) {
      return VMK_FALSE;
   }

   if (vmk_PktFragGet(skb->pkt, &pkt_frag, 0) != VMK_OK) {
      return VMK_FALSE;      
   }
   
   dma_addr = pkt_frag.addr + (skb->end - skb->head);
   if (dma_addr >= dma_addr_limit) {
      return VMK_TRUE;
   }

   nr_frags = skb_shinfo(skb)->nr_frags;
   for (idx_frags = 0; idx_frags < nr_frags; idx_frags++) {
      skb_frag = &skb_shinfo(skb)->frags[idx_frags];
      dma_addr = page_to_phys(skb_frag->page) + skb_frag->page_offset + skb_frag->size;

      if (dma_addr >= dma_addr_limit) {
         return VMK_TRUE;
      }
   }

   return VMK_FALSE;
}

static void
LinNetComputeEthCRCTableLE(void)
{
   unsigned i, crc, j;

   for (i = 0; i < 256; i++) {
      crc = i;
      for (j = 0; j < 8; j++) {
         crc = (crc >> 1) ^ ((crc & 0x1)? eth_crc32_poly_le : 0);
      }
      eth_crc32_poly_tbl_le[i] = crc;
   }
}

static uint32_t
LinNetComputeEthCRCLE(unsigned crc, const unsigned char *frame, uint32_t frameLen)
{
   int i, j;

   for (i = 0; i + 4 <= frameLen; i += 4) {
      crc ^= *(unsigned *)&frame[i];
      for (j = 0; j < 4; j++) {
         crc = eth_crc32_poly_tbl_le[crc & 0xff] ^ (crc >> 8);
      }
   }

   while (i < frameLen) {
      crc = eth_crc32_poly_tbl_le[(crc ^ frame[i++]) & 0xff] ^ (crc >> 8);
   }

   return crc;
}

/**                                          
 *  crc32_le - Calculate bitwise little-endian Ethernet CRC       
 *  @crc: seed value for computation    
 *  @p: pointer to buffer over which CRC is run   
 *  @len: length of buffer p
 *                                           
 *  Calculates bitwise little-endian Ethernet CRC from an 
 *  initial seed value that could be 0 or a previous value if
 *  computing incrementally.
 *                                           
 *  RETURN VALUE:                     
 *  32-bit CRC value.                                         
 *
 */
/* _VMKLNX_CODECHECK_: crc32_le */
uint32_t
crc32_le(uint32_t crc, unsigned char const *p, size_t len)
{
   return LinNetComputeEthCRCLE(crc, p, len);
}

/*
 *----------------------------------------------------------------------------
 *
 * LinNet_InitSoftnetData --
 *
 *    Initialize the softnet data structures.
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
LinNet_InitSoftnetData(void)
{
   int i;
   for (i = 0; i < NR_CPUS; i++) {
       struct softnet_data *queue;
       queue = &softnet_data[i];
       queue->completion_queue = NULL;
   }
}

/*
 *----------------------------------------------------------------------------
 *
 * LinNet_Init --
 *
 *    Initialize LinNet data structures.
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
LinNet_Init(void)
{  
   VMK_ReturnStatus status;

   VMKLNX_CREATE_LOG();

   Linux_OpenSoftirq(NET_TX_SOFTIRQ, net_tx_action, NULL);
   LinNet_InitSoftnetData();
   LinStress_SetupStress();
   LinNetComputeEthCRCTableLE();
   
   /* set up link state timer */
   status = vmk_ConfigParamOpen("Net", "LinkStatePollTimeout", 
                                &linkStateTimerPeriodConfigHandle);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_ConfigParamGetUint(linkStateTimerPeriodConfigHandle, 
                                   &linkStateTimerPeriod);
   VMK_ASSERT(status == VMK_OK);

   status = vmk_ConfigParamOpen("Net", "VmklnxLROEnabled", 
                                &vmklnxLROEnabledConfigHandle);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_ConfigParamGetUint(vmklnxLROEnabledConfigHandle, 
                                   &vmklnxLROEnabled);
   VMK_ASSERT(status == VMK_OK);

   status = vmk_ConfigParamOpen("Net", "VmklnxLROMaxAggr", 
                                &vmklnxLROMaxAggrConfigHandle);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_ConfigParamGetUint(vmklnxLROMaxAggrConfigHandle, 
                                   &vmklnxLROMaxAggr);
   VMK_ASSERT(status == VMK_OK);

   schedule_delayed_work(&linkStateWork,
                         msecs_to_jiffies(linkStateTimerPeriod));
   
   schedule_delayed_work(&watchdogWork,
                         msecs_to_jiffies(WATCHDOG_DEF_TIMER));
   
   status = vmk_ConfigParamOpen("Net", "PortDisableTimeout", 
                                &blockTotalSleepMsecHandle);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_ConfigParamGetUint(blockTotalSleepMsecHandle, &blockTotalSleepMsec);
   VMK_ASSERT(status == VMK_OK);

   vmklnx_skb_cache = vmklnx_kmem_cache_create(VMK_MODULE_SKB_HEAP_ID, 
                                               "vmklnx_skb_cache",
                                               sizeof(struct sk_buff) + 
                                               sizeof(struct skb_shared_info),
                                               0, NULL, NULL, 1, 100);

   max_phys_addr = (uint64_t) vmk_GetLastValidMachPage() * PAGE_SIZE;
   globalGenCount = (vmk_PktCompletionData) PKT_COMPL_GEN_COUNT_INIT;

   VMK_ASSERT(vmklnx_skb_cache != NULL);
   if (!vmklnx_skb_cache) {
      VMKLNX_WARN("socket buffer cache creation failed for vmlinux\n");
   }
   status = vmk_ConfigParamOpen("Net", "MaxNetifTxQueueLen", 
                                &maxNetifTxQueueLenConfigHandle);
   VMK_ASSERT(status == VMK_OK);

   status = vmk_ConfigParamOpen("Net", "UseHwIPv6Csum", 
                                &useHwIPv6CsumHandle);
   VMK_ASSERT(status == VMK_OK);

   status = vmk_ConfigParamOpen("Net", "UseHwCsumForIPv6Csum", 
                                &useHwCsumForIPv6CsumHandle);
   VMK_ASSERT(status == VMK_OK);

   status = vmk_ConfigParamOpen("Net", "UseHwTSO6", &useHwTSO6Handle);
   VMK_ASSERT(status == VMK_OK);

   status = vmk_StressOptionOpen(VMK_STRESS_OPT_NET_GEN_TINY_ARP_RARP,
                                 &stressNetGenTinyArpRarp);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionOpen(VMK_STRESS_OPT_NET_IF_CORRUPT_ETHERNET_HDR,
                                 &stressNetIfCorruptEthHdr);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionOpen(VMK_STRESS_OPT_NET_IF_CORRUPT_RX_DATA,
                                 &stressNetIfCorruptRxData);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionOpen(VMK_STRESS_OPT_NET_IF_CORRUPT_RX_TCP_UDP,
                                 &stressNetIfCorruptRxTcpUdp);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionOpen(VMK_STRESS_OPT_NET_IF_CORRUPT_TX,
                                 &stressNetIfCorruptTx);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionOpen(VMK_STRESS_OPT_NET_IF_FAIL_HARD_TX,
                                 &stressNetIfFailHardTx);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionOpen(VMK_STRESS_OPT_NET_IF_FAIL_RX,
                                 &stressNetIfFailRx);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionOpen(VMK_STRESS_OPT_NET_IF_FAIL_TX_AND_STOP_QUEUE,
                                 &stressNetIfFailTxAndStopQueue);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionOpen(VMK_STRESS_OPT_NET_IF_FORCE_HIGH_DMA_OVERFLOW,
                                 &stressNetIfForceHighDMAOverflow);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionOpen(VMK_STRESS_OPT_NET_IF_FORCE_RX_SW_CSUM,
                                 &stressNetIfForceRxSWCsum);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionOpen(VMK_STRESS_OPT_NET_NAPI_FORCE_BACKUP_WORLDLET,
                                 &stressNetNapiForceBackupWorldlet);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionOpen(VMK_STRESS_OPT_NET_BLOCK_DEV_IS_SLUGGISH,
                                 &stressNetBlockDevIsSluggish);
   VMK_ASSERT(status == VMK_OK);
}

/*
 *----------------------------------------------------------------------------
 *
 * LinNet_Cleanup --
 *
 *    Cleanup function for linux_net. Release and cleanup all resources.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */
void LinNet_Cleanup(void)
{
   VMK_ReturnStatus status;

   LinStress_CleanupStress();
   vmklnx_cancel_work_sync(&linkStateWork.work, &linkStateWork.timer);
   vmklnx_cancel_work_sync(&watchdogWork.work, &watchdogWork.timer);
   vmk_TimerRemoveSync(devWatchdogTimer);
   if (vmklnx_skb_cache) {
      vmklnx_kmem_cache_destroy(vmklnx_skb_cache);
   }
   status = vmk_ConfigParamClose(linkStateTimerPeriodConfigHandle);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_ConfigParamClose(maxNetifTxQueueLenConfigHandle);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_ConfigParamClose(useHwIPv6CsumHandle);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_ConfigParamClose(useHwCsumForIPv6CsumHandle);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_ConfigParamClose(useHwTSO6Handle);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_ConfigParamClose(blockTotalSleepMsecHandle);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_ConfigParamClose(vmklnxLROEnabledConfigHandle);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_ConfigParamClose(vmklnxLROMaxAggrConfigHandle);
   VMK_ASSERT(status == VMK_OK);

   status = vmk_StressOptionClose(stressNetGenTinyArpRarp);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionClose(stressNetIfCorruptEthHdr);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionClose(stressNetIfCorruptRxData);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionClose(stressNetIfCorruptRxTcpUdp);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionClose(stressNetIfCorruptTx);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionClose(stressNetIfFailHardTx);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionClose(stressNetIfFailRx);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionClose(stressNetIfFailTxAndStopQueue);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionClose(stressNetIfForceHighDMAOverflow);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionClose(stressNetIfForceRxSWCsum);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionClose(stressNetNapiForceBackupWorldlet);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_StressOptionClose(stressNetBlockDevIsSluggish);
   VMK_ASSERT(status == VMK_OK);

   VMKLNX_DESTROY_LOG();
}
