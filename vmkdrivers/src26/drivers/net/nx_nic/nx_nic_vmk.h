/*
 * Copyright (C) 2003 - 2007 NetXen, Inc.
 * All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 * 
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.
 * 
 * Contact Information:
 *    licensing@netxen.com
 * NetXen, Inc.
 * 3965 Freedom Circle, Fourth floor,
 * Santa Clara, CA 95054
 */
#ifndef __NX_NIC_VMK_H__
#define __NX_NIC_VMK_H__

// Use ESX if feature is supported in both ESX-3X and ESX-4X
#define ESX
#if (defined(__VMKERNEL_MODULE__) && defined(__VMKLNX__)) // ESX-4X only feature
#define ESX_4X 
#elif (defined(__VMKERNEL_MODULE__) && !defined(__VMKLNX__)) // ESX-3X only feature
#define ESX_3X
#endif

#if defined(__COMPAT_LAYER_2_6_18_PLUS__) 
#define NEW_NAPI
#endif

#if defined(ESX_4X)
#define VMK_SET_MODULE_VERSION(DRIVER_VERSION) 1

#else
#define VMK_SET_MODULE_VERSION(DRIVER_VERSION) \
	                vmk_set_module_version("%s", DRIVER_VERSION)

#endif

#ifndef PCI_CAP_ID_MSIX
#define PCI_CAP_ID_MSIX 0x11
#endif

#undef CONFIG_FW_LOADER
#undef CONFIG_FW_LOADER_MODULE

#define UNM_NETIF_F_TSO

#define local_bh_enable()
#define local_bh_disable()

#ifdef ESX_3X
#define pci_unmap_single(hwdev, dma_addr, size, direction) 
#define pci_unmap_page(hwdev, dma_addr, size, direction) 
#endif

#define BOUNCE_LOCK(__lock, flags) spin_lock_irqsave((__lock),flags)
#define BOUNCE_UNLOCK(__lock, flags) spin_unlock_irqrestore((__lock),flags)

#define RESET_POST_FAILED(adapter, ringid) (adapter->post_failed &= (~(1 << ringid)))
#define SET_POST_FAILED(adapter, ringid)  (adapter->post_failed |= (1 << ringid))
#define ALLOC_SKB(size, flag) alloc_skb(size, flag)

#define TX_TIMEOUT_LOCK(a) spin_lock(&(a->timeout_lock))
#define TX_TIMEOUT_UNLOCK(a) spin_unlock(&(a->timeout_lock))

#define NX_ADJUST_PKT_LEN(LEN) 			\
	do { 					\
		if ((LEN) < ETH_ZLEN)		\
			(LEN) = ETH_ZLEN;	\
	}while (0)

#define NX_FUSED_FW

#define LLC_CORRECTION 6;

#ifdef ESX_3X
#define PREPARE_TO_WAIT(WQ_HEAD, WQ_ENTRY, STATE) \
	do { 					\
		set_current_state((STATE)); \
		add_wait_queue((WQ_HEAD), (WQ_ENTRY));\
	}while (0)

#define SCHEDULE_TIMEOUT(EVENT, TIMEOUT, LOCK) \
	vmk_thread_wait_event((EVENT), (LOCK))

#else 
#define PREPARE_TO_WAIT(WQ_HEAD, WQ_ENTRY, STATE) \
	prepare_to_wait((WQ_HEAD), (WQ_ENTRY), (STATE))

#define SCHEDULE_TIMEOUT(EVENT, TIMEOUT, LOCK) \
	        schedule_timeout((TIMEOUT))

#endif

#ifdef ESX_3X
#define ESX_PHYS_TO_KMAP(MADDR, LEN) \
	vmk_phys_to_kmap(MADDR, LEN)

#define ESX_PHYS_TO_KMAP_FREE(VADDR) \
	vmk_phys_to_kmap_free(VADDR)
#else
#define ESX_PHYS_TO_KMAP(MADDR, LEN) \
	ioremap(MADDR, LEN)

#define ESX_PHYS_TO_KMAP_FREE(VADDR) \
	iounmap(VADDR)
#endif


#ifdef ESX_3X
/* Currently not defined in vmkernel but LRO and other feature need these */
static inline unsigned char *skb_push(struct sk_buff *skb, unsigned int len)
{
        skb->data-=len;
        skb->len+=len;
        if(skb->data<skb->head)
                out_of_line_bug();
        return skb->data;
}

static inline unsigned char * skb_pull(struct sk_buff *skb, unsigned int len)
{
        if (len > skb->len)
                return NULL;
        skb->len-=len;
        if (skb->len < skb->data_len)
                out_of_line_bug();
	skb_adjust_pkt_ma(skb,len);
        return  skb->data+=len;
}

#endif


#ifdef __VMKNETDDI_QUEUEOPS__

#define MULTICTX_IS_RX(type) ((type) == VMKNETDDI_QUEUEOPS_QUEUE_TYPE_RX)
#define MULTICTX_IS_TX(type) ((type) == VMKNETDDI_QUEUEOPS_QUEUE_TYPE_TX)
int nx_nic_netqueue_ops(vmknetddi_queueops_op_t op, void *args);

#define NX_SET_NETQ_OPS(DEV, OPS) \
		VMKNETDDI_REGISTER_QUEUEOPS((DEV), (OPS))

#define nx_set_skb_queueid(SKB, RCTX) 						  \
		do {								  \
			if(RCTX->this_id) {  					  \
				vmknetddi_queueops_set_skb_queueid(SKB,           \
				VMKNETDDI_QUEUEOPS_MK_RX_QUEUEID(RCTX->this_id)); \
			} 							  \
		}while (0) 
#endif

#endif
