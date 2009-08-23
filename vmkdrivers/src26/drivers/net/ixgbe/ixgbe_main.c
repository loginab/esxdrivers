/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2007 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/ipv6.h>
#ifdef NETIF_F_TSO
#include <net/checksum.h>
#ifdef NETIF_F_TSO6
#include <net/ip6_checksum.h>
#endif
#endif
#ifdef SIOCETHTOOL
#include <linux/ethtool.h>
#endif
#ifdef NETIF_F_HW_VLAN_TX
#include <linux/if_vlan.h>
#endif

#ifndef IXGBE_NO_LRO
#include <net/tcp.h>
#endif

#include "ixgbe.h"


#if defined(__VMKLNX__) && defined(__VMKNETDDI_QUEUEOPS__)
static int ixgbe_netqueue_ops(vmknetddi_queueops_op_t op, void *args);
#endif /* defined(__VMKLNX__) && defined(__VMKNETDDI_QUEUEOPS__) */

char ixgbe_driver_name[] = "ixgbe";
static char ixgbe_driver_string[] =
                               "Intel(R) 10 Gigabit PCI Express Network Driver";
#define EXTRASTRING

#ifndef IXGBE_NO_LRO
#define LRO "-lro"
#else
#define LRO
#endif
#define DRV_HW_PERF

#ifndef CONFIG_IXGBE_NAPI
#define DRIVERNAPI
#else
#define DRIVERNAPI "-NAPI"
#endif

#define DRV_VERSION "1.3.36_NETQ" LRO DRIVERNAPI DRV_HW_PERF EXTRASTRING

char ixgbe_driver_version[] = DRV_VERSION;
static char ixgbe_copyright[] = "Copyright (c) 1999-2007 Intel Corporation.";

/* ixgbe_pci_tbl - PCI Device ID Table
 *
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static struct pci_device_id ixgbe_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IXGBE_DEV_ID_82598AF_DUAL_PORT)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IXGBE_DEV_ID_82598AF_SINGLE_PORT)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IXGBE_DEV_ID_82598AT)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IXGBE_DEV_ID_82598EB_CX4)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IXGBE_DEV_ID_82598_CX4_DUAL_PORT)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IXGBE_DEV_ID_82598_DA_DUAL_PORT)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IXGBE_DEV_ID_82598_SR_DUAL_PORT_EM)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, IXGBE_DEV_ID_82598EB_XF_LR)},
	/* required last entry */
	{0, }
};
MODULE_DEVICE_TABLE(pci, ixgbe_pci_tbl);

#ifdef IXGBE_DCA
static int ixgbe_notify_dca(struct notifier_block *, unsigned long event,
                            void *p);
static struct notifier_block dca_notifier = {
	.notifier_call = ixgbe_notify_dca,
	.next          = NULL,
	.priority      = 0
};
#endif

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION("Intel(R) 10 Gigabit PCI Express Network Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

#define DEFAULT_DEBUG_LEVEL_SHIFT 3

#if defined(__VMKLNX__) 
static void __devinit ixgbe_set_num_queues(struct ixgbe_adapter *adapter);
static int ixgbe_set_rx_ring_size(struct ixgbe_adapter *adapter);
static int __devinit ixgbe_set_interrupt_capability(struct ixgbe_adapter *adapter);
static void ixgbe_reset_interrupt_capability(struct ixgbe_adapter *adapter);
static int ixgbe_setup_all_rx_resources(struct ixgbe_adapter *adapter);
static void ixgbe_free_all_rx_resources(struct ixgbe_adapter *adapter);
#endif

static void ixgbe_release_hw_control(struct ixgbe_adapter *adapter)
{
	u32 ctrl_ext;

	/* Let firmware take over control of h/w */
	ctrl_ext = IXGBE_READ_REG(&adapter->hw, IXGBE_CTRL_EXT);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_CTRL_EXT,
	                ctrl_ext & ~IXGBE_CTRL_EXT_DRV_LOAD);
}

static void ixgbe_get_hw_control(struct ixgbe_adapter *adapter)
{
	u32 ctrl_ext;

	/* Let firmware know the driver has taken over */
	ctrl_ext = IXGBE_READ_REG(&adapter->hw, IXGBE_CTRL_EXT);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_CTRL_EXT,
	                ctrl_ext | IXGBE_CTRL_EXT_DRV_LOAD);
}

static void ixgbe_set_ivar(struct ixgbe_adapter *adapter, u16 int_alloc_entry,
                           u8 msix_vector)
{
	u32 ivar, index;

	msix_vector |= IXGBE_IVAR_ALLOC_VAL;
	index = (int_alloc_entry >> 2) & 0x1F;
	ivar = IXGBE_READ_REG(&adapter->hw, IXGBE_IVAR(index));
	ivar &= ~(0xFF << (8 * (int_alloc_entry & 0x3)));
	ivar |= (msix_vector << (8 * (int_alloc_entry & 0x3)));
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_IVAR(index), ivar);
}

static void ixgbe_unmap_and_free_tx_resource(struct ixgbe_adapter *adapter,
                                             struct ixgbe_tx_buffer *tx_buffer_info)
{
	if (tx_buffer_info->dma) {
		pci_unmap_page(adapter->pdev, tx_buffer_info->dma,
		               tx_buffer_info->length, PCI_DMA_TODEVICE);
		tx_buffer_info->dma = 0;
	}
	if (tx_buffer_info->skb) {
		dev_kfree_skb_any(tx_buffer_info->skb);
		tx_buffer_info->skb = NULL;
	}
	/* tx_buffer_info must be completely set up in the transmit path */
}

static inline bool ixgbe_check_tx_hang(struct ixgbe_adapter *adapter,
                                       struct ixgbe_ring *tx_ring,
                                       unsigned int eop)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 head, tail;

	/* Detect a transmit hang in hardware, this serializes the
	 * check with the clearing of time_stamp and movement of eop */
	head = IXGBE_READ_REG(hw, tx_ring->head);
	tail = IXGBE_READ_REG(hw, tx_ring->tail);
	adapter->detect_tx_hung = false;
	if ((head != tail) &&
	    tx_ring->tx_buffer_info[eop].time_stamp &&
	    time_after(jiffies, tx_ring->tx_buffer_info[eop].time_stamp + HZ) &&
	    !(IXGBE_READ_REG(&adapter->hw, IXGBE_TFCS) & IXGBE_TFCS_TXOFF)) {
		/* detected Tx unit hang */
		union ixgbe_adv_tx_desc *tx_desc;
		tx_desc = IXGBE_TX_DESC_ADV(*tx_ring, eop);
		DPRINTK(DRV, ERR, "Detected Tx Unit Hang\n"
			"  Tx Queue             <%d>\n"
			"  TDH, TDT             <%x>, <%x>\n"
			"  next_to_use          <%x>\n"
			"  next_to_clean        <%x>\n"
			"tx_buffer_info[next_to_clean]\n"
			"  time_stamp           <%lx>\n"
			"  jiffies              <%lx>\n",
			tx_ring->queue_index,
			head, tail,
			tx_ring->next_to_use, eop,
			tx_ring->tx_buffer_info[eop].time_stamp, jiffies);
		return true;
	}

	return false;
}

#define IXGBE_MAX_TXD_PWR	14
#define IXGBE_MAX_DATA_PER_TXD	(1 << IXGBE_MAX_TXD_PWR)

/* Tx Descriptors needed, worst case */
#define TXD_USE_COUNT(S) (((S) >> IXGBE_MAX_TXD_PWR) + \
			 (((S) & (IXGBE_MAX_DATA_PER_TXD - 1)) ? 1 : 0))
#ifdef MAX_SKB_FRAGS
#define DESC_NEEDED TXD_USE_COUNT(IXGBE_MAX_DATA_PER_TXD) /* skb->data */ + \
	MAX_SKB_FRAGS * TXD_USE_COUNT(PAGE_SIZE) + 1      /* for context */
#else
#define DESC_NEEDED TXD_USE_COUNT(IXGBE_MAX_DATA_PER_TXD)
#endif

static void ixgbe_tx_timeout(struct net_device *netdev);

/**
 * ixgbe_clean_tx_irq - Reclaim resources after transmit completes
 * @adapter: board private structure
 **/
static bool ixgbe_clean_tx_irq(struct ixgbe_adapter *adapter,
                               struct ixgbe_ring *tx_ring)
{
	struct net_device *netdev = adapter->netdev;
	union ixgbe_adv_tx_desc *tx_desc, *eop_desc;
	struct ixgbe_tx_buffer *tx_buffer_info;
	unsigned int i, eop;
	bool cleaned = false;
	unsigned int total_tx_bytes = 0, total_tx_packets = 0;

	i = tx_ring->next_to_clean;
	eop = tx_ring->tx_buffer_info[i].next_to_watch;
	eop_desc = IXGBE_TX_DESC_ADV(*tx_ring, eop);
	while (eop_desc->wb.status & cpu_to_le32(IXGBE_TXD_STAT_DD)) {
		cleaned = false;
		while (!cleaned) {
			tx_desc = IXGBE_TX_DESC_ADV(*tx_ring, i);
			tx_buffer_info = &tx_ring->tx_buffer_info[i];
			cleaned = (i == eop);

			tx_ring->q_stats.bytes += tx_buffer_info->length;
			if (cleaned) {
				struct sk_buff *skb = tx_buffer_info->skb;
#ifdef NETIF_F_TSO
				unsigned int segs, bytecount;
				segs = skb_shinfo(skb)->gso_segs ?: 1;
				/* multiply data chunks by size of headers */
				bytecount = ((segs - 1) * skb_headlen(skb)) +
				            skb->len;
				total_tx_packets += segs;
				total_tx_bytes += bytecount;
#else
				total_tx_packets++;
				total_tx_bytes += skb->len;
#endif
			}
			ixgbe_unmap_and_free_tx_resource(adapter,
			                                 tx_buffer_info);
			tx_desc->wb.status = 0;

			i++;
			if (i == tx_ring->count)
				i = 0;
		}

		tx_ring->q_stats.packets++;

		eop = tx_ring->tx_buffer_info[i].next_to_watch;
		eop_desc = IXGBE_TX_DESC_ADV(*tx_ring, eop);

		/* weight of a sort for tx, avoid endless transmit cleanup */
		if (total_tx_packets >= tx_ring->work_limit)
			break;
	}

	tx_ring->next_to_clean = i;

#define TX_WAKE_THRESHOLD (DESC_NEEDED * 2)
	if (total_tx_packets && netif_carrier_ok(netdev) &&
	    (IXGBE_DESC_UNUSED(tx_ring) >= TX_WAKE_THRESHOLD)) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
#ifdef CONFIG_NETDEVICES_MULTIQUEUE
		if (__netif_subqueue_stopped(netdev, tx_ring->queue_index) &&
		    !test_bit(__IXGBE_DOWN, &adapter->state)) {
			netif_wake_subqueue(netdev, tx_ring->queue_index);
			adapter->restart_queue++;
		}
#else
		if (netif_queue_stopped(netdev) &&
		    !test_bit(__IXGBE_DOWN, &adapter->state)) {
			netif_wake_queue(netdev);
			adapter->restart_queue++;
		}
#endif
	}

	if (adapter->detect_tx_hung) {
		if (ixgbe_check_tx_hang(adapter, tx_ring, i)) {
			/* schedule immediate reset if we believe we hung */
			DPRINTK(PROBE, INFO,
			        "tx hang %d detected, resetting adapter\n",
			        adapter->tx_timeout_count + 1);
			ixgbe_tx_timeout(adapter->netdev);
		}
	}

	/* re-arm the interrupt */
	if (total_tx_packets >= tx_ring->work_limit)
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EICS, tx_ring->v_idx);

	tx_ring->total_bytes += total_tx_bytes;
	tx_ring->total_packets += total_tx_packets;
	adapter->net_stats.tx_bytes += total_tx_bytes;
	adapter->net_stats.tx_packets += total_tx_packets;
	cleaned = total_tx_packets ? true : false;
	return cleaned;
}

#ifdef IXGBE_DCA
static void ixgbe_update_rx_dca(struct ixgbe_adapter *adapter,
                                struct ixgbe_ring *rxr)
{
	u32 rxctrl;
	int cpu = get_cpu();
	int q = rxr - adapter->rx_ring;

	if (rxr->cpu != cpu) {
		rxctrl = IXGBE_READ_REG(&adapter->hw, IXGBE_DCA_RXCTRL(q));
		rxctrl &= ~IXGBE_DCA_RXCTRL_CPUID_MASK;
#if defined(__VMKLNX__)
		rxctrl |= vmklnx_dca_get_tag(cpu);
#else /* !defined(__VMKLNX__) */
		rxctrl |= dca_get_tag(cpu);
#endif /* defined(__VMKLNX__) */
		rxctrl |= IXGBE_DCA_RXCTRL_DESC_DCA_EN;
		rxctrl |= IXGBE_DCA_RXCTRL_HEAD_DCA_EN;
	  	rxctrl |= IXGBE_DCA_RXCTRL_DATA_DCA_EN;
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_DCA_RXCTRL(q), rxctrl);
		rxr->cpu = cpu;
	}
	put_cpu();
}

static void ixgbe_update_tx_dca(struct ixgbe_adapter *adapter,
                                struct ixgbe_ring *txr)
{
	u32 txctrl;
	int cpu = get_cpu();
	int q = txr - adapter->tx_ring;

	if (txr->cpu != cpu) {
		txctrl = IXGBE_READ_REG(&adapter->hw, IXGBE_DCA_TXCTRL(q));
		txctrl &= ~IXGBE_DCA_TXCTRL_CPUID_MASK;
#if defined(__VMKLNX__)
		txctrl |= vmklnx_dca_get_tag(cpu);
#else /* !defined(__VMKLNX__) */
		txctrl |= dca_get_tag(cpu);
#endif /* defined(__VMKLNX__) */
		txctrl |= IXGBE_DCA_TXCTRL_DESC_DCA_EN;
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_DCA_TXCTRL(q), txctrl);
		txr->cpu = cpu;
	}
	put_cpu();
}

static void ixgbe_setup_dca(struct ixgbe_adapter *adapter)
{
	int i;

	if (!(adapter->flags & IXGBE_FLAG_DCA_ENABLED))
		return;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		adapter->tx_ring[i].cpu = -1;
		ixgbe_update_tx_dca(adapter, &adapter->tx_ring[i]);
	}
	for (i = 0; i < adapter->num_rx_queues; i++) {
		adapter->rx_ring[i].cpu = -1;
		ixgbe_update_rx_dca(adapter, &adapter->rx_ring[i]);
	}
}

static int __ixgbe_notify_dca(struct device *dev, void *data)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	unsigned long event = *(unsigned long *)data;

	switch (event) {
	case DCA_PROVIDER_ADD:
		adapter->flags |= IXGBE_FLAG_DCA_ENABLED;
		/* Always use CB2 mode, difference is masked
		 * in the CB driver. */
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_DCA_CTRL, 2);
#if defined(__VMKLNX__)
		if (vmklnx_dca_add_requester(dev) == IXGBE_SUCCESS) {
#else /* !defined(__VMKLNX__) */
		if (dca_add_requester(dev) == IXGBE_SUCCESS) {
#endif /* defined(__VMKLNX__) */
			ixgbe_setup_dca(adapter);
			DPRINTK(PROBE, ERR, "Intel end-point DCA enabled\n");
			break;
		}
		/* Fall Through since DCA is disabled. */
	case DCA_PROVIDER_REMOVE:
		if (adapter->flags & IXGBE_FLAG_DCA_ENABLED) {
#if defined(__VMKLNX__)
		        vmklnx_dca_remove_requester(dev);
#else /* !defined(__VMKLNX__) */
			dca_remove_requester(dev);
#endif /* defined(__VMKLNX__) */
			adapter->flags &= ~IXGBE_FLAG_DCA_ENABLED;
			IXGBE_WRITE_REG(&adapter->hw, IXGBE_DCA_CTRL, 1);
		}
		break;
	}

	return IXGBE_SUCCESS;
}

#endif /* IXGBE_DCA */
/**
 * ixgbe_receive_skb - Send a completed packet up the stack
 * @adapter: board private structure
 * @skb: packet to send up
 * @is_vlan: packet has a VLAN tag
 * @tag: VLAN tag from descriptor
 **/
static void ixgbe_receive_skb(struct ixgbe_adapter *adapter,
			      struct sk_buff *skb, bool is_vlan, u16 tag)
{
	int ret;
#ifdef CONFIG_IXGBE_NAPI
	if (!(adapter->flags & IXGBE_FLAG_IN_NETPOLL)) {
#ifdef NETIF_F_HW_VLAN_TX
		if (adapter->vlgrp && is_vlan)
			vlan_hwaccel_receive_skb(skb, adapter->vlgrp, tag);
		else
			netif_receive_skb(skb);
#else
		netif_receive_skb(skb);
#endif
	} else {
#endif /* CONFIG_IXGBE_NAPI */

#ifdef NETIF_F_HW_VLAN_TX
		if (adapter->vlgrp && is_vlan)
			ret = vlan_hwaccel_rx(skb, adapter->vlgrp, tag);
		else
			ret = netif_rx(skb);
#else
		ret = netif_rx(skb);
#endif
#ifndef CONFIG_IXGBE_NAPI
		if (ret == NET_RX_DROP)
			adapter->rx_dropped_backlog++;
#endif
#ifdef CONFIG_IXGBE_NAPI
	}
#endif /* CONFIG_IXGBE_NAPI */
}

/**
 * ixgbe_rx_checksum - indicate in skb if hw indicated a good cksum
 * @adapter: address of board private structure
 * @status_err: hardware indication of status of receive
 * @skb: skb currently being received and modified
 **/
static inline void ixgbe_rx_checksum(struct ixgbe_adapter *adapter,
                                     u32 status_err, struct sk_buff *skb)
{
	skb->ip_summed = CHECKSUM_NONE;

	/* Ignore Checksum bit is set, or rx csum disabled */
	if ((status_err & IXGBE_RXD_STAT_IXSM) ||
	    !(adapter->flags & IXGBE_FLAG_RX_CSUM_ENABLED))
		return;

	/* if IP and error */
	if ((status_err & IXGBE_RXD_STAT_IPCS) &&
	    (status_err & IXGBE_RXDADV_ERR_IPE)) {
		adapter->hw_csum_rx_error++;
		return;
	}
		
	if (!(status_err & IXGBE_RXD_STAT_L4CS))
		return;

	if (status_err & IXGBE_RXDADV_ERR_TCPE) {
		adapter->hw_csum_rx_error++;
		return;
	}

	/* It must be a TCP or UDP packet with a valid checksum */
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	adapter->hw_csum_rx_good++;
}

/**
 * ixgbe_alloc_rx_buffers - Replace used receive buffers; packet split
 * @adapter: address of board private structure
 **/
static void ixgbe_alloc_rx_buffers(struct ixgbe_adapter *adapter,
                                   struct ixgbe_ring *rx_ring,
                                   int cleaned_count)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	union ixgbe_adv_rx_desc *rx_desc;
	struct ixgbe_rx_buffer *bi;
	unsigned int i;
	unsigned int bufsz = adapter->rx_buf_len + NET_IP_ALIGN;

	i = rx_ring->next_to_use;
	bi = &rx_ring->rx_buffer_info[i];

	while (cleaned_count--) {
		rx_desc = IXGBE_RX_DESC_ADV(*rx_ring, i);

		if (!bi->page && (adapter->flags & IXGBE_FLAG_RX_PS_ENABLED)) {
			bi->page = alloc_page(GFP_ATOMIC);
			if (!bi->page) {
				adapter->alloc_rx_page_failed++;
				goto no_buffers;
			}
			bi->page_dma = pci_map_page(pdev, bi->page, 0,
			                            PAGE_SIZE,
			                            PCI_DMA_FROMDEVICE);
		}

		if (!bi->skb) {
			struct sk_buff *skb = netdev_alloc_skb(netdev, bufsz);

			if (!skb) {
				adapter->alloc_rx_buff_failed++;
				goto no_buffers;
			}

			/*
			 * Make buffer alignment 2 beyond a 16 byte boundary
			 * this will result in a 16 byte aligned IP header after
			 * the 14 byte MAC header is removed
			 */
			skb_reserve(skb, NET_IP_ALIGN);

			bi->skb = skb;
			bi->dma = pci_map_single(pdev, skb->data, bufsz,
			                         PCI_DMA_FROMDEVICE);
		}
		/* Refresh the desc even if buffer_addrs didn't change because
		 * each write-back erases this info. */
		if (adapter->flags & IXGBE_FLAG_RX_PS_ENABLED) {
			rx_desc->read.pkt_addr = cpu_to_le64(bi->page_dma);
			rx_desc->read.hdr_addr = cpu_to_le64(bi->dma);
		} else {
			rx_desc->read.pkt_addr = cpu_to_le64(bi->dma);
		}

		i++;
		if (i == rx_ring->count)
			i = 0;
		bi = &rx_ring->rx_buffer_info[i];
	}

no_buffers:
	if (rx_ring->next_to_use != i) {
		rx_ring->next_to_use = i;
		if (i-- == 0)
			i = (rx_ring->count - 1);

		/*
		 * Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.  (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64).
		 */
		wmb();
		writel(i, adapter->hw.hw_addr + rx_ring->tail);
	}
}

#ifndef IXGBE_NO_LRO
static int lromax = 44;

/**
 * lro_flush - Indicate packets to upper layer.
 * Update IP and TCP header part of head skb if more than one
 * skb's chained and indicate packets to upper layer.
 **/
static void ixgbe_lro_ring_flush(struct ixgbe_lro_list *lrolist,
                                 struct ixgbe_adapter *adapter,
                                 struct ixgbe_lro_desc *lrod, bool is_vlan,
                                 u16 tag)
{
	struct iphdr *iph;
	struct tcphdr *th;
	struct sk_buff *skb;
	u32 *ts_ptr;
	struct ixgbe_lro_info *lro_data = &adapter->lro_data;
	struct net_device *netdev = adapter->netdev;

	hlist_del(&lrod->lro_node);
	lrolist->active_cnt--;

	skb = lrod->skb;

	if (lrod->append_cnt) {
#if defined(__VMKLNX__)
                u32 ethhdr_sz = eth_header_len((struct ethhdr *)skb->data);
		/* incorporate ip header and re-calculate checksum */
		iph = (struct iphdr *)(skb->data + ethhdr_sz);
		iph->tot_len = ntohs(skb->len - ethhdr_sz);
#else /* !defined(__VMKLNX__) */
		/* incorporate ip header and re-calculate checksum */
                iph = (struct iphdr *)skb->data;
		iph->tot_len = ntohs(skb->len);
#endif /* defined(__VMKLNX__) */
		iph->check = 0;
		iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);

		/* incorporate the latest ack into the tcp header */
#if defined(__VMKLNX__)
		th = (struct tcphdr *) ((char *)skb->data + sizeof(*iph) + ethhdr_sz);
#else /* !defined(__VMKLNX__) */
                th = (struct tcphdr *) ((char *)skb->data + sizeof(*iph));
#endif /* defined(__VMKLNX__) */
		th->ack_seq = lrod->ack_seq;
		th->window = lrod->window;

		/* incorporate latest timestamp into the tcp header */
		if (lrod->timestamp) {
			ts_ptr = (u32 *)(th + 1);
			ts_ptr[1] = htonl(lrod->tsval);
			ts_ptr[2] = lrod->tsecr;
		}
#if defined(__VMKLNX__)
        	vmk_PktSetLargeTcpPacket(skb->pkt, lrod->mss);
#endif /* defined(__VMKLNX__) */
	}

	ixgbe_receive_skb(adapter, skb, is_vlan, tag);

	netdev->last_rx = jiffies;
	lro_data->stats.coal += lrod->append_cnt + 1;
	lro_data->stats.flushed++;

	lrod->skb = NULL;
	lrod->last_skb = NULL;
	lrod->timestamp = 0;
	lrod->append_cnt = 0;
	lrod->data_size = 0;
	hlist_add_head(&lrod->lro_node, &lrolist->free);
}

static void ixgbe_lro_ring_flush_all(struct ixgbe_lro_list *lrolist,
                                     struct ixgbe_adapter *adapter,
                                     bool is_vlan, u16 tag)
{
	struct ixgbe_lro_desc *lrod;
	struct hlist_node *node, *node2;

	hlist_for_each_entry_safe(lrod,
			node, node2, &lrolist->active, lro_node)
		ixgbe_lro_ring_flush(lrolist, adapter, lrod, is_vlan, tag);
}

/*
 * lro_header_ok - Main LRO function.
 **/
static int ixgbe_lro_header_ok(struct ixgbe_lro_info *lro_data,
                               struct sk_buff *new_skb, struct iphdr *iph,
                               struct tcphdr *th)
{
	int opt_bytes, tcp_data_len;
	u32 *ts_ptr = NULL;
	/* If we see CE codepoint in IP header, packet is not mergeable */
	if (INET_ECN_is_ce(ipv4_get_dsfield(iph)))
		return -1;

	/* ensure there are no options */
	if ((iph->ihl << 2) != sizeof(*iph))
		return -1;

	/* .. and the packet is not fragmented */
	if (iph->frag_off & htons(IP_MF|IP_OFFSET))
		return -1;

	/* ensure no bits set besides ack or psh */
	if (th->fin || th->syn || th->rst ||
	    th->urg || th->ece || th->cwr || !th->ack)
		return -1;

	/* ensure that the checksum is valid */
	if (new_skb->ip_summed != CHECKSUM_UNNECESSARY)
		return -1;

	/*
	 * check for timestamps. Since the only option we handle are timestamps,
	 * we only have to handle the simple case of aligned timestamps
	 */

	opt_bytes = (th->doff << 2) - sizeof(*th);
	if (opt_bytes != 0) {
		ts_ptr = (u32 *)(th + 1);
		if ((opt_bytes != TCPOLEN_TSTAMP_ALIGNED) ||
			(*ts_ptr != ntohl((TCPOPT_NOP << 24) |
			(TCPOPT_NOP << 16) | (TCPOPT_TIMESTAMP << 8) |
			TCPOLEN_TIMESTAMP))) {
			return -1;
		}
	}

	tcp_data_len = ntohs(iph->tot_len) - (th->doff << 2) - sizeof(*iph);

	if (tcp_data_len == 0)
		return -1;

	return tcp_data_len;
}

/**
 * ixgbe_lro_ring_queue - if able, queue skb into lro chain
 * @lrolist: pointer to structure for lro entries
 * @adapter: address of board private structure
 * @new_skb: pointer to current skb being checked
 * @is_vlan: does this packet have a vlan tag?
 * @tag: vlan tag if any
 *
 * Checks whether the skb given is eligible for LRO and if that's
 * fine chains it to the existing lro_skb based on flowid. If an LRO for
 * the flow doesn't exist create one.
 **/
static int ixgbe_lro_ring_queue(struct ixgbe_lro_list *lrolist,
                                struct ixgbe_adapter *adapter,
                                struct sk_buff *new_skb, bool is_vlan, u16 tag)
{
	struct ethhdr *eh;
	struct iphdr *iph;
	struct tcphdr *th, *header_th;
	int  opt_bytes, header_ok = 1;
	u32 *ts_ptr = NULL;
	struct sk_buff *lro_skb;
	struct ixgbe_lro_desc *lrod;
	struct hlist_node *node;
	u32 seq;
	struct ixgbe_lro_info *lro_data = &adapter->lro_data;
	int tcp_data_len;
#if defined(__VMKLNX__)
        u32 ethhdr_sz;
        vmk_uint16 nr_frags;

        if (configNoLRO) {
           return -1;
        }

        ethhdr_sz = eth_header_len((struct ethhdr *)new_skb->data);
	eh = (struct ethhdr *)new_skb->data;
	iph = (struct iphdr *)(new_skb->data + ethhdr_sz);
#else /* !defined(__VMKLNX__) */
        /* Disable LRO for jumbo MTUs, No RX CSO, promiscuous mode */
	if (adapter->netdev->flags & IFF_PROMISC)
                return -1;
        
	eh = (struct ethhdr *)skb_mac_header(new_skb);
	iph = (struct iphdr *)(eh + 1);
#endif /* defined(__VMKLNX__) */

	/* check to see if it is TCP */
	if ((eh->h_proto != htons(ETH_P_IP)) || (iph->protocol != IPPROTO_TCP))
			return -1;

	/* find the TCP header */
	th = (struct tcphdr *) (iph + 1);

	tcp_data_len = ixgbe_lro_header_ok(lro_data, new_skb, iph, th);
	if (tcp_data_len == -1)
		header_ok = 0;

#if defined(__VMKLNX__)
	/* 
         * make sure any packet we are about to chain doesn't include any pad,
         * moreover we cannot trim the ethernet header here as it is still
         * necessary in vmkernel.
         */
        skb_trim(new_skb, ntohs(iph->tot_len) + ethhdr_sz);
#else /* !defined(__VMKLNX__) */
        /* make sure any packet we are about to chain doesn't include any pad */
	skb_trim(new_skb, ntohs(iph->tot_len));
#endif /* defined(__VMKLNX__) */

	opt_bytes = (th->doff << 2) - sizeof(*th);
	if (opt_bytes != 0)
		ts_ptr = (u32 *)(th + 1);

	seq = ntohl(th->seq);
	/*
	 * we have a packet that might be eligible for LRO,
	 * so see if it matches anything we might expect
	 */

#if defined(__VMKLNX__)
        nr_frags = vmk_PktFragsNb(new_skb->pkt);
#endif /* defined(__VMKLNX__) */

	hlist_for_each_entry(lrod, node, &lrolist->active, lro_node) {
		if (lrod->source_port == th->source &&
			lrod->dest_port == th->dest &&
			lrod->source_ip == iph->saddr &&
			lrod->dest_ip == iph->daddr &&
			lrod->vlan_tag == tag) {

#if defined(__VMKLNX__)
                        if (lrod->append_frags + nr_frags > VMK_PKT_FRAGS_MAX_LENGTH) {
                           ixgbe_lro_ring_flush(lrolist, adapter, lrod,
                                                is_vlan, tag);
                           return -1;
                        }
#endif /* defined(__VMKLNX__) */
 
			if (!header_ok) {
				ixgbe_lro_ring_flush(lrolist, adapter, lrod,
				                     is_vlan, tag);
				return -1;
			}

			if (seq != lrod->next_seq) {
				/* out of order packet */
				ixgbe_lro_ring_flush(lrolist, adapter, lrod,
				                     is_vlan, tag);
				return -1;
			}

			if (lrod->timestamp) {
				u32 tsval = ntohl(*(ts_ptr + 1));
				/* make sure timestamp values are increasing */
				if (lrod->tsval > tsval || *(ts_ptr + 2) == 0) {
					ixgbe_lro_ring_flush(lrolist, adapter,
					                     lrod, is_vlan,
					                     tag);
					return -1;
				}
				lrod->tsval = tsval;
				lrod->tsecr = *(ts_ptr + 2);
			}

			lro_skb = lrod->skb;

			lro_skb->len += tcp_data_len;
			lro_skb->data_len += tcp_data_len;
			lro_skb->truesize += tcp_data_len;

			lrod->next_seq += tcp_data_len;
			lrod->ack_seq = th->ack_seq;
			lrod->window = th->window;
			lrod->data_size += tcp_data_len;
			if (tcp_data_len > lrod->mss)
				lrod->mss = tcp_data_len;

#if defined(__VMKLNX__)
			/* Remove Ethernet, IP and TCP header*/
			skb_pull(new_skb, ethhdr_sz + ntohs(iph->tot_len) - tcp_data_len);
#else /* !defined(__VMKLNX__) */
                        /* Remove IP and TCP header*/
			skb_pull(new_skb, ntohs(iph->tot_len) - tcp_data_len);
#endif /* defined(__VMKLNX__) */

			/* Chain the new skb */
			if (skb_shinfo(lro_skb)->frag_list != NULL )
				lrod->last_skb->next = new_skb;
			else
				skb_shinfo(lro_skb)->frag_list = new_skb;

			lrod->last_skb = new_skb ;

			lrod->append_cnt++;
#if defined(__VMKLNX__)
                        lrod->append_frags += nr_frags;
#endif /* defined(__VMKLNX__) */

			/* New packet with push flag, flush the whole packet. */
			if (th->psh) {
				header_th =
#if defined(__VMKLNX__)
				(struct tcphdr *)(lro_skb->data + sizeof(*iph) + ethhdr_sz);
#else /* !defined(__VMKLNX__) */
                                (struct tcphdr *)(lro_skb->data + sizeof(*iph));
#endif /* defined(__VMKLNX__) */
				header_th->psh |= th->psh;
				ixgbe_lro_ring_flush(lrolist, adapter, lrod,
				                     is_vlan, tag);
				return 0;
			}

			if (lrod->append_cnt >= lro_data->max)
				ixgbe_lro_ring_flush(lrolist, adapter, lrod,
				                     is_vlan, tag);

			return 0;
		} /*End of if*/
	}

	/* start a new packet */
	if (header_ok && !hlist_empty(&lrolist->free)) {
		lrod = hlist_entry(lrolist->free.first, struct ixgbe_lro_desc,
		                   lro_node);

		lrod->skb = new_skb;
		lrod->source_ip = iph->saddr;
		lrod->dest_ip = iph->daddr;
		lrod->source_port = th->source;
		lrod->dest_port = th->dest;
		lrod->next_seq = seq + tcp_data_len;
		lrod->mss = tcp_data_len;
		lrod->ack_seq = th->ack_seq;
		lrod->window = th->window;
		lrod->data_size = tcp_data_len;
		lrod->vlan_tag = tag;
#if defined(__VMKLNX__)
                lrod->append_frags = nr_frags;
#endif /* defined(__VMKLNX__) */

		/* record timestamp if it is present */
		if (opt_bytes) {
			lrod->timestamp = 1;
			lrod->tsval = ntohl(*(ts_ptr + 1));
			lrod->tsecr = *(ts_ptr + 2);
		}
		/* remove first packet from freelist.. */
		hlist_del(&lrod->lro_node);
		/* .. and insert at the front of the active list */
		hlist_add_head(&lrod->lro_node, &lrolist->active);
		lrolist->active_cnt++;

		return 0;
	}

	return -1;
}

static void ixgbe_lro_ring_exit(struct ixgbe_lro_list *lrolist)
{
	struct hlist_node *node, *node2;
	struct ixgbe_lro_desc *lrod;

	hlist_for_each_entry_safe(lrod, node, node2, &lrolist->active,
	                          lro_node) {
		hlist_del(&lrod->lro_node);
		kfree(lrod);
	}
}

static void ixgbe_lro_ring_init(struct ixgbe_lro_list *lrolist,
                                struct ixgbe_adapter *adapter)
{
	int j, bytes;
	struct ixgbe_lro_desc *lrod;

	bytes = sizeof(struct ixgbe_lro_desc);

	INIT_HLIST_HEAD(&lrolist->free);
	INIT_HLIST_HEAD(&lrolist->active);

	for (j = 0; j < IXGBE_LRO_MAX; j++) {
		lrod = kzalloc(bytes, GFP_KERNEL);
		if (lrod != NULL) {
			INIT_HLIST_NODE(&lrod->lro_node);
			hlist_add_head(&lrod->lro_node, &lrolist->free);
		} else {
			DPRINTK(PROBE, ERR,
			        "Allocation for LRO descriptor %u failed\n", j);
		}
	}
}

#endif /* IXGBE_NO_LRO */
#ifdef CONFIG_IXGBE_NAPI
#if defined(__VMKLNX__)
static bool ixgbe_clean_rx_irq(struct ixgbe_adapter *adapter,
                               struct ixgbe_ring *rx_ring,
                               int *work_done, int work_to_do, u8 rxq_id)
#else /* !defined(__VMKLNX__) */
static bool ixgbe_clean_rx_irq(struct ixgbe_adapter *adapter,
                               struct ixgbe_ring *rx_ring,
                               int *work_done, int work_to_do)
#endif /*__VMKLNX__*/
#else
#if defined(__VMKLNX__)
static bool ixgbe_clean_rx_irq(struct ixgbe_adapter *adapter,
                               struct ixgbe_ring *rx_ring, u8 rxq_id)
#else /* !defined(__VMKLNX__) */
static bool ixgbe_clean_rx_irq(struct ixgbe_adapter *adapter,
                               struct ixgbe_ring *rx_ring)
#endif /* defined(__VMKLNX__) */
#endif /*CONFIG_IXGBE_NAPI*/
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	union ixgbe_adv_rx_desc *rx_desc, *next_rxd;
	struct ixgbe_rx_buffer *rx_buffer_info, *next_buffer;
	struct sk_buff *skb;
	unsigned int i;
	u32 upper_len, len, staterr;
	u16 hdr_info, vlan_tag;
	bool is_vlan, cleaned = false;
	int cleaned_count = 0;
#ifndef CONFIG_IXGBE_NAPI
	int work_to_do = rx_ring->work_limit, local_work_done = 0;
	int *work_done = &local_work_done;
#endif
	unsigned int total_rx_bytes = 0, total_rx_packets = 0;

	i = rx_ring->next_to_clean;
	upper_len = 0;
	rx_desc = IXGBE_RX_DESC_ADV(*rx_ring, i);
	staterr = le32_to_cpu(rx_desc->wb.upper.status_error);
	rx_buffer_info = &rx_ring->rx_buffer_info[i];
	is_vlan = (staterr & IXGBE_RXD_STAT_VP);
	vlan_tag = le16_to_cpu(rx_desc->wb.upper.vlan);

	while (staterr & IXGBE_RXD_STAT_DD) {
		if (*work_done >= work_to_do)
			break;
		(*work_done)++;

		if (adapter->flags & IXGBE_FLAG_RX_PS_ENABLED) {
			hdr_info =
			       le16_to_cpu(rx_desc->wb.lower.lo_dword.hdr_info);
			len = (hdr_info & IXGBE_RXDADV_HDRBUFLEN_MASK) >>
			       IXGBE_RXDADV_HDRBUFLEN_SHIFT;
			if (hdr_info & IXGBE_RXDADV_SPH)
				adapter->rx_hdr_split++;
			if (len > IXGBE_RX_HDR_SIZE)
				len = IXGBE_RX_HDR_SIZE;
			upper_len = le16_to_cpu(rx_desc->wb.upper.length);
		} else {
			len = le16_to_cpu(rx_desc->wb.upper.length);
		}

#ifndef IXGBE_NO_LLI
		if (staterr & IXGBE_RXD_STAT_DYNINT)
			adapter->lli_int++;
#endif

		cleaned = true;
		skb = rx_buffer_info->skb;
		prefetch(skb->data - NET_IP_ALIGN);
		rx_buffer_info->skb = NULL;

		if (len && !skb_shinfo(skb)->nr_frags) {
			pci_unmap_single(pdev, rx_buffer_info->dma,
			                 adapter->rx_buf_len + NET_IP_ALIGN,
			                 PCI_DMA_FROMDEVICE);
			skb_put(skb, len);
		}

		if (upper_len) {
			pci_unmap_page(pdev, rx_buffer_info->page_dma,
			               PAGE_SIZE, PCI_DMA_FROMDEVICE);
			rx_buffer_info->page_dma = 0;
			skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags,
			                   rx_buffer_info->page, 0, upper_len);
			rx_buffer_info->page = NULL;

			skb->len += upper_len;
			skb->data_len += upper_len;
			skb->truesize += upper_len;
		}

		i++;
		if (i == rx_ring->count)
			i = 0;
		next_buffer = &rx_ring->rx_buffer_info[i];

		next_rxd = IXGBE_RX_DESC_ADV(*rx_ring, i);
		prefetch(next_rxd);

		cleaned_count++;
		if (staterr & IXGBE_RXD_STAT_EOP) {
			rx_ring->q_stats.packets++;
			rx_ring->q_stats.bytes += skb->len;
		} else {
			rx_buffer_info->skb = next_buffer->skb;
			rx_buffer_info->dma = next_buffer->dma;
			next_buffer->skb = skb;
			adapter->non_eop_descs++;
			goto next_desc;
		}

		if (staterr & IXGBE_RXDADV_ERR_FRAME_ERR_MASK) {
			dev_kfree_skb_irq(skb);
			goto next_desc;
		}

		ixgbe_rx_checksum(adapter, staterr, skb);

		/* probably a little skewed due to removing CRC */
		total_rx_bytes += skb->len;
		total_rx_packets++;

		skb->protocol = eth_type_trans(skb, netdev);
#ifndef IXGBE_NO_LRO
		if (ixgbe_lro_ring_queue(&rx_ring->lrolist,
				adapter, skb, is_vlan, vlan_tag) == 0) {
			netdev->last_rx = jiffies;
			rx_ring->q_stats.packets++;
			rx_ring->q_stats.bytes += skb->len;
			goto next_desc;
		}
#endif
#if defined(__VMKLNX__) && defined(__VMKNETDDI_QUEUEOPS__)
		vmknetddi_queueops_set_skb_queueid (skb,
						    VMKNETDDI_QUEUEOPS_MK_RX_QUEUEID(rxq_id));
#endif /* defined(__VMKLNX__) && defined(__VMKNETDDI_QUEUEOPS__) */
		ixgbe_receive_skb(adapter, skb, is_vlan, vlan_tag); 
		netdev->last_rx = jiffies;

next_desc:
		rx_desc->wb.upper.status_error = 0;

		/* return some buffers to hardware, one at a time is too slow */
		if (cleaned_count >= IXGBE_RX_BUFFER_WRITE) {
			ixgbe_alloc_rx_buffers(adapter, rx_ring, cleaned_count);
			cleaned_count = 0;
		}

		/* use prefetched values */
		rx_desc = next_rxd;
		rx_buffer_info = next_buffer;

		staterr = le32_to_cpu(rx_desc->wb.upper.status_error);
		is_vlan = (staterr & IXGBE_RXD_STAT_VP);
		vlan_tag = le16_to_cpu(rx_desc->wb.upper.vlan);
	}

	rx_ring->next_to_clean = i;
#ifndef IXGBE_NO_LRO
	ixgbe_lro_ring_flush_all(&rx_ring->lrolist, adapter, is_vlan, vlan_tag);
#endif /* IXGBE_NO_LRO */
	cleaned_count = IXGBE_DESC_UNUSED(rx_ring);

	if (cleaned_count)
		ixgbe_alloc_rx_buffers(adapter, rx_ring, cleaned_count);

	rx_ring->total_packets += total_rx_packets;
	rx_ring->total_bytes += total_rx_bytes;
	adapter->net_stats.rx_bytes += total_rx_bytes;
	adapter->net_stats.rx_packets += total_rx_packets;

#ifndef CONFIG_IXGBE_NAPI
	/* re-arm the interrupt if we had to bail early and have more work */
	if (*work_done >= work_to_do)
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EICS, rx_ring->v_idx);
#endif
	return cleaned;
}

#ifdef CONFIG_IXGBE_NAPI
static int ixgbe_clean_rxonly(struct napi_struct *, int);
#endif
/**
 * ixgbe_configure_msix - Configure MSI-X hardware
 * @adapter: board private structure
 *
 * ixgbe_configure_msix sets up the hardware to properly generate MSI-X
 * interrupts.
 **/
static void ixgbe_configure_msix(struct ixgbe_adapter *adapter)
{
	struct ixgbe_q_vector *q_vector;
	int i, j, q_vectors, v_idx, r_idx;
	u32 mask;

	q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;

	/* Populate the IVAR table and set the ITR values to the
	 * corresponding register.
	 */
	for (v_idx = 0; v_idx < q_vectors; v_idx++) {
		q_vector = &adapter->q_vector[v_idx];
		/* XXX for_each_bit(...) */
		r_idx = find_first_bit(q_vector->rxr_idx,
		                       adapter->num_rx_queues);

		for (i = 0; i < q_vector->rxr_count; i++) {
			j = adapter->rx_ring[r_idx].reg_idx;
			ixgbe_set_ivar(adapter, IXGBE_IVAR_RX_QUEUE(j), v_idx);
			r_idx = find_next_bit(q_vector->rxr_idx,
			                      adapter->num_rx_queues,
			                      r_idx + 1);
		}
		r_idx = find_first_bit(q_vector->txr_idx,
		                       adapter->num_tx_queues);

		for (i = 0; i < q_vector->txr_count; i++) {
			j = adapter->tx_ring[r_idx].reg_idx;
			ixgbe_set_ivar(adapter, IXGBE_IVAR_TX_QUEUE(j), v_idx);
			r_idx = find_next_bit(q_vector->txr_idx,
			                      adapter->num_tx_queues,
			                      r_idx + 1);
		}

		/* if this is a tx only vector halve the interrupt rate */
		if (q_vector->txr_count && !q_vector->rxr_count)
			q_vector->eitr = (adapter->eitr_param >> 1);
		else
			/* rx only */
			q_vector->eitr = adapter->eitr_param;

		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EITR(v_idx),
		                EITR_INTS_PER_SEC_TO_REG(q_vector->eitr));
	}

	ixgbe_set_ivar(adapter, IXGBE_IVAR_OTHER_CAUSES_INDEX, v_idx);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EITR(v_idx), 1950);
#ifdef IXGBE_TCP_TIMER
	ixgbe_set_ivar(adapter, IXGBE_IVAR_TCP_TIMER_INDEX, ++v_idx);
#endif

	/* set up to autoclear timer, and the vectors */
	mask = IXGBE_EIMS_ENABLE_MASK;
	mask &= ~(IXGBE_EIMS_OTHER | IXGBE_EIMS_LSC);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIAC, mask);
}

enum latency_range {
	lowest_latency = 0,
	low_latency = 1,
	bulk_latency = 2,
	latency_invalid = 255
};

/**
 * ixgbe_update_itr - update the dynamic ITR value based on statistics
 * @adapter: pointer to adapter
 * @eitr: eitr setting (ints per sec) to give last timeslice
 * @itr_setting: current throttle rate in ints/second
 * @packets: the number of packets during this measurement interval
 * @bytes: the number of bytes during this measurement interval
 *
 *      Stores a new ITR value based on packets and byte
 *      counts during the last interrupt.  The advantage of per interrupt
 *      computation is faster updates and more accurate ITR for the current
 *      traffic pattern.  Constants in this function were computed
 *      based on theoretical maximum wire speed and thresholds were set based
 *      on testing data as well as attempting to minimize response time
 *      while increasing bulk throughput.
 *      this functionality is controlled by the InterruptThrottleRate module
 *      parameter (see ixgbe_param.c)
 **/
static u8 ixgbe_update_itr(struct ixgbe_adapter *adapter,
                           u32 eitr, u8 itr_setting,
                           int packets, int bytes)
{
	unsigned int retval = itr_setting;
	u32 timepassed_us;
	u64 bytes_perint;

	if (packets == 0)
		goto update_itr_done;


	/* simple throttlerate management
	 *    0-20MB/s lowest (100000 ints/s)
	 *   20-100MB/s low   (20000 ints/s)
	 *  100-1249MB/s bulk (8000 ints/s)
	 */
	/* what was last interrupt timeslice? */
	timepassed_us = 1000000/eitr;
	bytes_perint = bytes / timepassed_us; /* bytes/usec */

	switch (itr_setting) {
	case lowest_latency:
		if (bytes_perint > adapter->eitr_low) {
			retval = low_latency;
		}
		break;
	case low_latency:
		if (bytes_perint > adapter->eitr_high) {
			retval = bulk_latency;
		}
		else if (bytes_perint <= adapter->eitr_low) {
			retval = lowest_latency;
		}
		break;
	case bulk_latency:
		if (bytes_perint <= adapter->eitr_high) {
			retval = low_latency;
		}
		break;
	}

update_itr_done:
	return retval;
}

static void ixgbe_set_itr_msix(struct ixgbe_q_vector *q_vector)
{
	struct ixgbe_adapter *adapter = q_vector->adapter;
	struct ixgbe_hw *hw = &adapter->hw;
	u32 new_itr;
	u8 current_itr, ret_itr;
	int i, r_idx, v_idx = ((void *)q_vector - (void *)(adapter->q_vector)) /
	                      sizeof(struct ixgbe_q_vector);
	struct ixgbe_ring *rx_ring, *tx_ring;

	r_idx = find_first_bit(q_vector->txr_idx, adapter->num_tx_queues);
	for (i = 0; i < q_vector->txr_count; i++) {
		tx_ring = &(adapter->tx_ring[r_idx]);
		ret_itr = ixgbe_update_itr(adapter, q_vector->eitr,
		                           q_vector->tx_itr,
		                           tx_ring->total_packets,
		                           tx_ring->total_bytes);
		/* if the result for this queue would decrease interrupt
		 * rate for this vector then use that result */
		q_vector->tx_itr = ((q_vector->tx_itr > ret_itr) ?
		                    q_vector->tx_itr - 1 : ret_itr);
		r_idx = find_next_bit(q_vector->txr_idx, adapter->num_tx_queues,
		                      r_idx + 1);
	}

	r_idx = find_first_bit(q_vector->rxr_idx, adapter->num_rx_queues);
	for (i = 0; i < q_vector->rxr_count; i++) {
		rx_ring = &(adapter->rx_ring[r_idx]);
		ret_itr = ixgbe_update_itr(adapter, q_vector->eitr,
		                           q_vector->rx_itr,
                                           rx_ring->total_packets,
                                           rx_ring->total_bytes);
		/* if the result for this queue would decrease interrupt
		 * rate for this vector then use that result */
		q_vector->rx_itr = ((q_vector->rx_itr > ret_itr) ?
		                    q_vector->rx_itr - 1 : ret_itr);
		r_idx = find_next_bit(q_vector->rxr_idx, adapter->num_rx_queues,
		                      r_idx + 1);
	}

	current_itr = max(q_vector->rx_itr, q_vector->tx_itr);

	switch (current_itr) {
	/* counts and packets in update_itr are dependent on these numbers */
	case lowest_latency:
		new_itr = 100000;
		break;
	case low_latency:
		new_itr = 20000; /* aka hwitr = ~200 */
		break;
	case bulk_latency:
	default:
		new_itr = 8000;
		break;
	}

	if (new_itr != q_vector->eitr) {
		u32 itr_reg;
		/* do an exponential smoothing */
		new_itr = ((q_vector->eitr * 90)/100) + ((new_itr * 10)/100);
		q_vector->eitr = new_itr;
		itr_reg = EITR_INTS_PER_SEC_TO_REG(new_itr);
		/* must write high and low 16 bits to reset counter */
		DPRINTK(TX_ERR, DEBUG, "writing eitr(%d): %08X\n", v_idx, itr_reg);
		IXGBE_WRITE_REG(hw, IXGBE_EITR(v_idx), itr_reg | (itr_reg)<<16);
	}

	return;
}

static void ixgbe_check_fan_failure(struct ixgbe_adapter *adapter, u32 eicr)
{
	struct ixgbe_hw *hw = &adapter->hw;

	if ((adapter->flags & IXGBE_FLAG_FAN_FAIL_CAPABLE) &&
	    (eicr & IXGBE_EICR_GPI_SDP1)) {
		DPRINTK(PROBE, CRIT, "Fan has stopped, replace the adapter\n");
		/* write to clear the interrupt */
		IXGBE_WRITE_REG(hw, IXGBE_EICR, IXGBE_EICR_GPI_SDP1);
	}
}

static void ixgbe_check_lsc(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;

	adapter->lsc_int++;
	adapter->flags |= IXGBE_FLAG_NEED_LINK_UPDATE;
	adapter->link_check_timeout = jiffies;
	if (!test_bit(__IXGBE_DOWN, &adapter->state)) {
		IXGBE_WRITE_REG(hw, IXGBE_EIMC, IXGBE_EIMC_LSC);
		schedule_work(&adapter->watchdog_task);
	}
}

static irqreturn_t ixgbe_msix_lsc(int irq, void *data)
{
	struct net_device *netdev = data;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u32 eicr = IXGBE_READ_REG(hw, IXGBE_EICR);

	if (eicr & IXGBE_EICR_LSC)
		ixgbe_check_lsc(adapter);

	ixgbe_check_fan_failure(adapter, eicr);

	if (!test_bit(__IXGBE_DOWN, &adapter->state))
		IXGBE_WRITE_REG(hw, IXGBE_EIMS, IXGBE_EIMS_OTHER);

	return IRQ_HANDLED;
}

#ifdef IXGBE_TCP_TIMER
static irqreturn_t ixgbe_msix_pba(int irq, void *data)
{
	struct net_device *netdev = data;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	int i;

	u32 pba = readl(adapter->msix_addr + IXGBE_MSIXPBA);
	for (i = 0; i < MAX_MSIX_COUNT; i++) {
		if (pba & (1 << i))
			adapter->msix_handlers[i](irq, data, regs);
		else
			adapter->pba_zero[i]++;
	}

	adapter->msix_pba++;
	return IRQ_HANDLED;
}

static irqreturn_t ixgbe_msix_tcp_timer(int irq, void *data)
{
	struct net_device *netdev = data;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	adapter->msix_tcp_timer++;

	return IRQ_HANDLED;
}
#endif /* IXGBE_TCP_TIMER */

static irqreturn_t ixgbe_msix_clean_tx(int irq, void *data)
{
	struct ixgbe_q_vector *q_vector = data;
	struct ixgbe_adapter  *adapter = q_vector->adapter;
	struct ixgbe_ring     *txr;
	int i, r_idx;

	if (!q_vector->txr_count)
		return IRQ_HANDLED;

	r_idx = find_first_bit(q_vector->txr_idx, adapter->num_tx_queues);
	for (i = 0; i < q_vector->txr_count; i++) {
		txr = &(adapter->tx_ring[r_idx]);
#ifdef IXGBE_DCA
		if (adapter->flags & IXGBE_FLAG_DCA_ENABLED)
			ixgbe_update_tx_dca(adapter, txr);
#endif
		txr->total_bytes = 0;
		txr->total_packets = 0;
		ixgbe_clean_tx_irq(adapter, txr);
		r_idx = find_next_bit(q_vector->txr_idx, adapter->num_tx_queues,
		                      r_idx + 1);
	}

	/*
	 * possibly later we can enable tx auto-adjustment if necessary
	 *
	if (adapter->itr_setting & 3)
		ixgbe_set_itr_msix(q_vector);
	 */

	return IRQ_HANDLED;
}

/**
 * ixgbe_msix_clean_rx - single unshared vector rx clean (all queues)
 * @irq: unused
 * @data: pointer to our q_vector struct for this interrupt vector
 **/
static irqreturn_t ixgbe_msix_clean_rx(int irq, void *data)
{
	struct ixgbe_q_vector *q_vector = data;
	struct ixgbe_adapter  *adapter = q_vector->adapter;
	struct ixgbe_ring  *rxr;
	int r_idx;
#ifndef CONFIG_IXGBE_NAPI
	int i;
#endif

	r_idx = find_first_bit(q_vector->rxr_idx, adapter->num_rx_queues);
#ifndef CONFIG_IXGBE_NAPI
	for (i = 0; i < q_vector->rxr_count; i++) {
		rxr = &(adapter->rx_ring[r_idx]);
		rxr->total_bytes = 0;
		rxr->total_packets = 0;
#if defined(__VMKLNX__)
		ixgbe_clean_rx_irq(adapter, rxr, r_idx);
#else /* !defined(__VMKLNX__) */
		ixgbe_clean_rx_irq(adapter, rxr);
#endif /* defined(__VMKLNX__) */

#ifdef IXGBE_DCA
		if (adapter->flags & IXGBE_FLAG_DCA_ENABLED)
			ixgbe_update_rx_dca(adapter, rxr);

#endif
		r_idx = find_next_bit(q_vector->rxr_idx, adapter->num_rx_queues,
		                      r_idx + 1);
	}

	if (adapter->itr_setting & 3)
		ixgbe_set_itr_msix(q_vector);
#else
	if (!q_vector->rxr_count)
		return IRQ_HANDLED;

	rxr = &(adapter->rx_ring[r_idx]);
	/* disable interrupts on this vector only */
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC, rxr->v_idx);
	rxr->total_bytes = 0;
	rxr->total_packets = 0;
	netif_rx_schedule(adapter->netdev, &q_vector->napi);
#endif

	return IRQ_HANDLED;
}

static irqreturn_t ixgbe_msix_clean_many(int irq, void *data)
{
	ixgbe_msix_clean_rx(irq, data);
	ixgbe_msix_clean_tx(irq, data);

	return IRQ_HANDLED;
}

#ifdef CONFIG_IXGBE_NAPI
/**
 * ixgbe_clean_rxonly - msix (aka one shot) rx clean routine
 * @napi: napi struct with our devices info in it
 * @budget: amount of work driver is allowed to do this pass, in packets
 *
 **/
static int ixgbe_clean_rxonly(struct napi_struct *napi, int budget)
{
	struct ixgbe_q_vector *q_vector =
	                       container_of(napi, struct ixgbe_q_vector, napi);
	struct ixgbe_adapter *adapter = q_vector->adapter;
	struct net_device *netdev = adapter->netdev;
	struct ixgbe_ring *rxr = NULL;
	int work_done = 0, i;
	long r_idx;
	
	r_idx = find_first_bit(q_vector->rxr_idx, adapter->num_rx_queues);
#if defined(__VMKLNX__)
	/* The Intel version has a bug for the case of more than 1 queues are mapped 
 	   to per vector. The bug will lead "no communication" in case of NAPI and
	   MQ is enabled with more than 1 queues are mapped to single vector.
	 */
	for (i = 0; i < q_vector->rxr_count; i++) {
		rxr = &(adapter->rx_ring[r_idx]);
#ifdef IXGBE_DCA
		if (adapter->flags & IXGBE_FLAG_DCA_ENABLED)
			ixgbe_update_rx_dca(adapter, rxr);
#endif
		ixgbe_clean_rx_irq(adapter, rxr, &work_done, budget, r_idx);
		r_idx = find_next_bit(q_vector->rxr_idx, adapter->num_rx_queues,
		                      r_idx + 1);
	}
#else /* !defined(__VMKLNX__) */
	rxr = &(adapter->rx_ring[r_idx]);
#ifdef IXGBE_DCA
	if (adapter->flags & IXGBE_FLAG_DCA_ENABLED)
		ixgbe_update_rx_dca(adapter, rxr);
#endif

	ixgbe_clean_rx_irq(adapter, rxr, &work_done, budget);
#endif /* !defined(__VMKLNX__) */

	/* If all Rx work done, exit the polling mode */
	if ((work_done == 0) || !netif_running(netdev)) {
		netif_rx_complete(netdev, napi);
		if (adapter->itr_setting & 3)
			ixgbe_set_itr_msix(q_vector);
		if (!test_bit(__IXGBE_DOWN, &adapter->state))
			IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMS, rxr->v_idx);
		return 0;
	}

	return work_done;
}
#endif /* CONFIG_IXGBE_NAPI */


static inline void map_vector_to_rxq(struct ixgbe_adapter *a, int v_idx,
                                     int r_idx)
{
	a->q_vector[v_idx].adapter = a;
	set_bit(r_idx, a->q_vector[v_idx].rxr_idx);
	a->q_vector[v_idx].rxr_count++;
	a->rx_ring[r_idx].v_idx = 1 << v_idx;
#if defined(__VMKLNX__) && defined(__VMKNETDDI_QUEUEOPS__)
	a->rx_ring[r_idx].vector_idx = v_idx;
#endif
}

static inline void map_vector_to_txq(struct ixgbe_adapter *a, int v_idx,
                                     int r_idx)
{
	a->q_vector[v_idx].adapter = a;
	set_bit(r_idx, a->q_vector[v_idx].txr_idx);
	a->q_vector[v_idx].txr_count++;
	a->tx_ring[r_idx].v_idx = 1 << v_idx;
}

/**
 * ixgbe_map_rings_to_vectors - Maps descriptor rings to vectors
 * @adapter: board private structure to initialize
 * @vectors: allotted vector count for descriptor rings
 *
 * This function maps descriptor rings to the queue-specific vectors
 * we were allotted through the MSI-X enabling code.  Ideally, we'd have
 * one vector per ring/queue, but on a constrained vector budget, we
 * group the rings as "efficiently" as possible.  You would add new
 * mapping configurations in here.
 **/
static int ixgbe_map_rings_to_vectors(struct ixgbe_adapter *adapter, int vectors)
{
	int v_start = 0;
	int rxr_idx = 0, txr_idx = 0;
	int rxr_remaining = adapter->num_rx_queues;
	int txr_remaining = adapter->num_tx_queues;
	int i, j;
	int rqpv, tqpv;
	int err = IXGBE_SUCCESS;

	/* No mapping required if MSI-X is disabled. */
	if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED))
		goto out;

	/*
	 * The ideal configuration...
	 * We have enough vectors to map one per queue.
	 */
	if (vectors == adapter->num_rx_queues + adapter->num_tx_queues) {
		for (; rxr_idx < rxr_remaining; v_start++, rxr_idx++)
			map_vector_to_rxq(adapter, v_start, rxr_idx);

		for (; txr_idx < txr_remaining; v_start++, txr_idx++)
			map_vector_to_txq(adapter, v_start, txr_idx);

		goto out;
	}

	/*
	 * If we don't have enough vectors for a 1-to-1
	 * mapping, we'll have to group them so there are
	 * multiple queues per vector.
	 */
	/* Re-adjusting *qpv takes care of the remainder. */
	for (i = v_start; i < vectors; i++) {
		rqpv = DIV_ROUND_UP(rxr_remaining, vectors - i);
		for (j = 0; j < rqpv; j++) {
			map_vector_to_rxq(adapter, i, rxr_idx);
			rxr_idx++;
			rxr_remaining--;
		}
	}
	for (i = v_start; i < vectors; i++) {
		tqpv = DIV_ROUND_UP(txr_remaining, vectors - i);
		for (j = 0; j < tqpv; j++) {
			map_vector_to_txq(adapter, i, txr_idx);
			txr_idx++;
			txr_remaining--;
		}
	}

out:
	return err;
}

/**
 * ixgbe_request_msix_irqs - Initialize MSI-X interrupts
 * @adapter: board private structure
 *
 * ixgbe_request_msix_irqs allocates MSI-X vectors and requests
 * interrupts from the kernel.
 **/
static int ixgbe_request_msix_irqs(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	irqreturn_t (*handler)(int, void *);
	int i, vector, q_vectors, err;

	/* Decrement for Other and TCP Timer vectors */
	q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;

	/* Map the Tx/Rx rings to the vectors we were allotted. */
	err = ixgbe_map_rings_to_vectors(adapter, q_vectors);
	if (err)
		goto out;

#define SET_HANDLER(_v) (!(_v)->rxr_count) ? &ixgbe_msix_clean_tx : \
                        (!(_v)->txr_count) ? &ixgbe_msix_clean_rx : \
                        &ixgbe_msix_clean_many
	for (vector = 0; vector < q_vectors; vector++) {
		handler = SET_HANDLER(&adapter->q_vector[vector]);
		sprintf(adapter->name[vector], "%s:v%d-%s",
		        netdev->name, vector,
		        (handler == &ixgbe_msix_clean_rx) ? "Rx" :
		         ((handler == &ixgbe_msix_clean_tx) ? "Tx" : "TxRx"));
		err = request_irq(adapter->msix_entries[vector].vector,
		                  handler, 0, adapter->name[vector],
		                  &(adapter->q_vector[vector]));
		if (err) {
			DPRINTK(PROBE, ERR,
				"request_irq failed for MSIX interrupt "
				"Error: %d\n", err);
			goto free_queue_irqs;
		}
	}

	sprintf(adapter->name[vector], "%s:lsc", netdev->name);
	err = request_irq(adapter->msix_entries[vector].vector,
	                  &ixgbe_msix_lsc, 0, adapter->name[vector], netdev);
	if (err) {
		DPRINTK(PROBE, ERR,
		        "request_irq for msix_lsc failed: %d\n", err);
		goto free_queue_irqs;
	}

#ifdef IXGBE_TCP_TIMER
	vector++;
	sprintf(adapter->name[vector], "%s:timer", netdev->name);
	err = request_irq(adapter->msix_entries[vector].vector,
	                  &ixgbe_msix_tcp_timer, 0, adapter->name[vector],
	                  netdev);
	if (err) {
		DPRINTK(PROBE, ERR,
		        "request_irq for msix_tcp_timer failed: %d\n", err);
		/* Free "Other" interrupt */
		free_irq(adapter->msix_entries[--vector].vector, netdev);
		goto free_queue_irqs;
	}
	{

		int v = 0;
		printk(KERN_ERR"MSIX table dump %d vectors after the request_irq is done\n", adapter->num_msix_vectors);
		printk("Uppe-addr Addr  data control \n");
		//for (; v < vectors; v++) {
		for (; v < 20; v++) {
		//for (; i < 2; i++) {
		//printk("%016llX %016llX %016llX %016llX\n",
		printk("Vector [%d] 0x%x 0x%x 0x%x 0x%x \n", (v),
				//readl(adapter->msix_addr+(v*4)+1),
				//readl(adapter->msix_addr+(v*4)+0),
				//readl(adapter->msix_addr+(v*4)+2),
				//readl(adapter->msix_addr+(v*4)+3));
				readl(adapter->msix_addr+(v*16)+4),
				readl(adapter->msix_addr+(v*16)+0),
				readl(adapter->msix_addr+(v*16)+8),
				readl(adapter->msix_addr+(v*16)+12));
		}

	}
#endif

	return IXGBE_SUCCESS;

free_queue_irqs:
	for (i = vector - 1; i >= 0; i--)
		free_irq(adapter->msix_entries[--vector].vector,
		         &(adapter->q_vector[i]));
	adapter->flags &= ~IXGBE_FLAG_MSIX_ENABLED;
	pci_disable_msix(adapter->pdev);
	kfree(adapter->msix_entries);
	adapter->msix_entries = NULL;
out:
	return err;
}

static void ixgbe_set_itr(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_q_vector *q_vector = adapter->q_vector;
	u8 current_itr;
	u32 new_itr = q_vector->eitr;
	struct ixgbe_ring *rx_ring = &adapter->rx_ring[0];
	struct ixgbe_ring *tx_ring = &adapter->tx_ring[0];

	q_vector->tx_itr = ixgbe_update_itr(adapter, new_itr,
	                                   q_vector->tx_itr,
	                                   tx_ring->total_packets,
	                                   tx_ring->total_bytes);
	q_vector->rx_itr = ixgbe_update_itr(adapter, new_itr,
                                           q_vector->rx_itr,
                                           rx_ring->total_packets,
                                           rx_ring->total_bytes);

	current_itr = max(q_vector->rx_itr, q_vector->tx_itr);

	switch (current_itr) {
	/* counts and packets in update_itr are dependent on these numbers */
	case lowest_latency:
		new_itr = 100000;
		break;
	case low_latency:
		new_itr = 20000; /* aka hwitr = ~200 */
		break;
	case bulk_latency:
		new_itr = 8000;
		break;
	default:
		break;
	}

	if (new_itr != q_vector->eitr) {
		u32 itr_reg;
		/* do an exponential smoothing */
		new_itr = ((q_vector->eitr * 90)/100) + ((new_itr * 10)/100);
		q_vector->eitr = new_itr;
		itr_reg = EITR_INTS_PER_SEC_TO_REG(new_itr);
		/* must write high and low 16 bits to reset counter */
		IXGBE_WRITE_REG(hw, IXGBE_EITR(0), itr_reg | (itr_reg)<<16);
	}

	return;
}

static inline void ixgbe_irq_enable(struct ixgbe_adapter *adapter);

/**
 * ixgbe_intr - legacy mode Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 * @pt_regs: CPU registers structure
 **/
static irqreturn_t ixgbe_intr(int irq, void *data)
{
	struct net_device *netdev = data;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u32 eicr;

	/* for NAPI, using EIAM to auto-mask tx/rx interrupt bits on read
	 * therefore no explict interrupt disable is necessary */
	eicr = IXGBE_READ_REG(hw, IXGBE_EICR);
	if (!eicr) {
#ifdef CONFIG_IXGBE_NAPI
		/* shared interrupt alert!
		 * make sure interrupts are enabled because the read will
		 * have disabled interrupts due to EIAM */
		ixgbe_irq_enable(adapter);
#endif
		return IRQ_NONE;  /* Not our interrupt */
	}

	if (eicr & IXGBE_EICR_LSC)
		ixgbe_check_lsc(adapter);

	ixgbe_check_fan_failure(adapter, eicr);

#ifdef CONFIG_IXGBE_NAPI
	if (netif_rx_schedule_prep(netdev, &adapter->q_vector[0].napi)) {
		adapter->tx_ring[0].total_packets = 0;
		adapter->tx_ring[0].total_bytes = 0;
		adapter->rx_ring[0].total_packets = 0;
		adapter->rx_ring[0].total_bytes = 0;
		/* would disable interrupts here but EIAM disabled it */
		__netif_rx_schedule(netdev, &adapter->q_vector[0].napi);
	}

#else
	adapter->tx_ring[0].total_packets = 0;
	adapter->tx_ring[0].total_bytes = 0;
	adapter->rx_ring[0].total_packets = 0;
	adapter->rx_ring[0].total_bytes = 0;
#if defined(__VMKLNX__)
	ixgbe_clean_rx_irq(adapter, adapter->rx_ring, 0);
#else /* !defined(__VMKLNX__) */
	ixgbe_clean_rx_irq(adapter, adapter->rx_ring);
#endif /* defined(__VMKLNX__) */
	ixgbe_clean_tx_irq(adapter, adapter->tx_ring);

	/* dynamically adjust throttle */
	if (adapter->itr_setting & 3)
		ixgbe_set_itr(adapter);
#endif

	return IRQ_HANDLED;
}

static inline void ixgbe_reset_q_vectors(struct ixgbe_adapter *adapter)
{
	int i, q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;

	for (i = 0; i < q_vectors; i++) {
		struct ixgbe_q_vector *q_vector = &adapter->q_vector[i];
		bitmap_zero(q_vector->rxr_idx, MAX_RX_QUEUES);
		bitmap_zero(q_vector->txr_idx, MAX_TX_QUEUES);
		q_vector->rxr_count = 0;
		q_vector->txr_count = 0;
	}
}

/**
 * ixgbe_request_irq - initialize interrupts
 * @adapter: board private structure
 *
 * Attempts to configure interrupts using the best available
 * capabilities of the hardware and kernel.
 **/
static int ixgbe_request_irq(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int err;

	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
		err = ixgbe_request_msix_irqs(adapter);
	} else if (adapter->flags & IXGBE_FLAG_MSI_ENABLED) {
		err = request_irq(adapter->pdev->irq, &ixgbe_intr, 0,
		                  netdev->name, netdev);
	} else {
		err = request_irq(adapter->pdev->irq, &ixgbe_intr, IRQF_SHARED,
		                  netdev->name, netdev);
	}

	if (err)
		DPRINTK(PROBE, ERR, "request_irq failed, Error %d\n", err);

	return err;
}

static void ixgbe_free_irq(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
		int i, q_vectors;

		q_vectors = adapter->num_msix_vectors;
		
		i = q_vectors - 1;
#ifdef IXGBE_TCP_TIMER
		free_irq(adapter->msix_entries[i].vector, netdev);
		i--;
#endif
		free_irq(adapter->msix_entries[i].vector, netdev);

		i--;
		for (; i >= 0; i--) {
			free_irq(adapter->msix_entries[i].vector,
			         &(adapter->q_vector[i]));
		}

		ixgbe_reset_q_vectors(adapter);
	} else {
		free_irq(adapter->pdev->irq, netdev);
	}
}

/**
 * ixgbe_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 **/
static inline void ixgbe_irq_disable(struct ixgbe_adapter *adapter)
{
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC, ~0);
	IXGBE_WRITE_FLUSH(&adapter->hw);
	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
		int i;
		for (i = 0; i < adapter->num_msix_vectors; i++)
			synchronize_irq(adapter->msix_entries[i].vector);
	} else {
		synchronize_irq(adapter->pdev->irq);
	}
}

/**
 * ixgbe_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 **/
static inline void ixgbe_irq_enable(struct ixgbe_adapter *adapter)
{
	u32 mask;
	mask = IXGBE_EIMS_ENABLE_MASK;
	if (adapter->flags & IXGBE_FLAG_FAN_FAIL_CAPABLE)
		mask |= IXGBE_EIMS_GPI_SDP1;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMS, mask);
	IXGBE_WRITE_FLUSH(&adapter->hw);
}

/**
 * ixgbe_configure_msi_and_legacy - Initialize PIN (INTA...) and MSI interrupts
 *
 **/
static void ixgbe_configure_msi_and_legacy(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;

	IXGBE_WRITE_REG(hw, IXGBE_EITR(0),
	                EITR_INTS_PER_SEC_TO_REG(adapter->eitr_param));

	ixgbe_set_ivar(adapter, IXGBE_IVAR_RX_QUEUE(0), 0);
	ixgbe_set_ivar(adapter, IXGBE_IVAR_TX_QUEUE(0), 0);

	map_vector_to_rxq(adapter, 0, 0);
	map_vector_to_txq(adapter, 0, 0);

	DPRINTK(HW, INFO, "Legacy interrupt IVAR setup done\n");
}

/**
 * ixgbe_configure_tx - Configure 8254x Transmit Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Tx unit of the MAC after a reset.
 **/
static void ixgbe_configure_tx(struct ixgbe_adapter *adapter)
{
	u64 tdba;
	struct ixgbe_hw *hw = &adapter->hw;
	u32 i, j, tdlen, txctrl;

	/* Setup the HW Tx Head and Tail descriptor pointers */
	for (i = 0; i < adapter->num_tx_queues; i++) {
		j = adapter->tx_ring[i].reg_idx;
		tdba = adapter->tx_ring[i].dma;
		tdlen = adapter->tx_ring[i].count *
		        sizeof(union ixgbe_adv_tx_desc);
		IXGBE_WRITE_REG(hw, IXGBE_TDBAL(j),
		                (tdba & DMA_32BIT_MASK));
		IXGBE_WRITE_REG(hw, IXGBE_TDBAH(j), (tdba >> 32));
		IXGBE_WRITE_REG(hw, IXGBE_TDLEN(j), tdlen);
		IXGBE_WRITE_REG(hw, IXGBE_TDH(j), 0);
		IXGBE_WRITE_REG(hw, IXGBE_TDT(j), 0);
		adapter->tx_ring[i].head = IXGBE_TDH(j);
		adapter->tx_ring[i].tail = IXGBE_TDT(j);
		/* Disable Tx Head Writeback RO bit, since this hoses
		 * bookkeeping if things aren't delivered in order.
		 */
		txctrl = IXGBE_READ_REG(hw, IXGBE_DCA_TXCTRL(i));
		txctrl &= ~IXGBE_DCA_TXCTRL_TX_WB_RO_EN;
		IXGBE_WRITE_REG(hw, IXGBE_DCA_TXCTRL(i), txctrl);
	}
}

#define PAGE_USE_COUNT(S) (((S) >> PAGE_SHIFT) + \
			(((S) & (PAGE_SIZE - 1)) ? 1 : 0))

#define IXGBE_SRRCTL_BSIZEHDRSIZE_SHIFT			2
/**
 * ixgbe_configure_rx - Configure 8254x Receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 **/
static void ixgbe_configure_rx(struct ixgbe_adapter *adapter)
{
	u64 rdba;
	struct ixgbe_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	int max_frame = netdev->mtu + ETH_HLEN + ETH_FCS_LEN;
	int i, j;
	u32 rdlen, rxctrl, rxcsum;
	u32 random[10];
	u32 fctrl, hlreg0;
	u32 pages;
	u32 reta = 0, mrqc, srrctl;
#ifdef CONFIG_IXGBE_VMDQ
	u32 vmdctl;
	u32 rdrxctl;
#endif

#ifndef IXGBE_NO_LRO
	adapter->lro_data.max = lromax;

	if (lromax * netdev->mtu > (1 << 16))
		adapter->lro_data.max = ((1 << 16) / netdev->mtu) - 1;

#endif
	/* Decide whether to use packet split mode or not */
	if (netdev->mtu > ETH_DATA_LEN) {
		if (adapter->flags & IXGBE_FLAG_RX_PS_CAPABLE)
			adapter->flags |= IXGBE_FLAG_RX_PS_ENABLED;
		else
			adapter->flags &= ~IXGBE_FLAG_RX_PS_ENABLED;
	} else {
		if (adapter->flags & IXGBE_FLAG_RX_1BUF_CAPABLE) {
			adapter->flags &= ~IXGBE_FLAG_RX_PS_ENABLED;
		} else
			adapter->flags |= IXGBE_FLAG_RX_PS_ENABLED;
	}

	/* Set the RX buffer length according to the mode */
	if (adapter->flags & IXGBE_FLAG_RX_PS_ENABLED) {
		adapter->rx_buf_len = IXGBE_RX_HDR_SIZE;
	} else {
		if (netdev->mtu <= ETH_DATA_LEN)
			adapter->rx_buf_len = MAXIMUM_ETHERNET_VLAN_SIZE;
		else
			adapter->rx_buf_len = ALIGN(max_frame, 1024);
	}

	fctrl = IXGBE_READ_REG(&adapter->hw, IXGBE_FCTRL);
	fctrl |= IXGBE_FCTRL_BAM;
	fctrl |= IXGBE_FCTRL_DPF; /* discard pause frames when FC enabled */
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_FCTRL, fctrl);

	hlreg0 = IXGBE_READ_REG(hw, IXGBE_HLREG0);
	if (adapter->netdev->mtu <= ETH_DATA_LEN)
		hlreg0 &= ~IXGBE_HLREG0_JUMBOEN;
	else
		hlreg0 |= IXGBE_HLREG0_JUMBOEN;
#if defined(__VMKLNX__)
	/* Disabling the length validation as this causes frames to be dropped
	   if actual packet sizes do not match lenth field in mac header.
	   This breaks the beaconing used in NIC teaming. Workaround for now
	   is to disable this in the h/w. (PR 239225).
	 */
	hlreg0 &= ~IXGBE_HLREG0_RXLNGTHERREN;
#endif /* defined(__VMKLNX__) */
	IXGBE_WRITE_REG(hw, IXGBE_HLREG0, hlreg0);

	pages = PAGE_USE_COUNT(adapter->netdev->mtu);

#ifdef CONFIG_IXGBE_VMDQ
	/* FIXME: Program SRRCTL(X) according to vmdq_enabled flag */
#endif
	srrctl = IXGBE_READ_REG(&adapter->hw, IXGBE_SRRCTL(0));

	srrctl &= ~IXGBE_SRRCTL_BSIZEHDR_MASK;
	srrctl &= ~IXGBE_SRRCTL_BSIZEPKT_MASK;

	if (adapter->flags & IXGBE_FLAG_RX_PS_ENABLED) {
		srrctl |= PAGE_SIZE >> IXGBE_SRRCTL_BSIZEPKT_SHIFT;
		srrctl |= IXGBE_SRRCTL_DESCTYPE_HDR_SPLIT_ALWAYS;
		srrctl |= ((IXGBE_RX_HDR_SIZE <<
			    IXGBE_SRRCTL_BSIZEHDRSIZE_SHIFT) &
			   IXGBE_SRRCTL_BSIZEHDR_MASK);
	} else {
		srrctl |= IXGBE_SRRCTL_DESCTYPE_ADV_ONEBUF;

		if (adapter->rx_buf_len == MAXIMUM_ETHERNET_VLAN_SIZE)
			srrctl |= IXGBE_RXBUFFER_2048 >>
			          IXGBE_SRRCTL_BSIZEPKT_SHIFT;
		else
			srrctl |= adapter->rx_buf_len >>
			          IXGBE_SRRCTL_BSIZEPKT_SHIFT;
	}
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_SRRCTL(0), srrctl);

	rdlen = adapter->rx_ring[0].count * sizeof(union ixgbe_adv_rx_desc);
	/* disable receives while setting up the descriptors */
	rxctrl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, rxctrl & ~IXGBE_RXCTRL_RXEN);

	/* Setup the HW Rx Head and Tail Descriptor Pointers and
	 * the Base and Length of the Rx Descriptor Ring */
	for (i = 0; i < adapter->num_rx_queues; i++) {
		rdba = adapter->rx_ring[i].dma;
		j = adapter->rx_ring[i].reg_idx;
		IXGBE_WRITE_REG(hw, IXGBE_RDBAL(j), (rdba & DMA_32BIT_MASK));
		IXGBE_WRITE_REG(hw, IXGBE_RDBAH(j), (rdba >> 32));
		IXGBE_WRITE_REG(hw, IXGBE_RDLEN(j), rdlen);
		IXGBE_WRITE_REG(hw, IXGBE_RDH(j), 0);
		IXGBE_WRITE_REG(hw, IXGBE_RDT(j), 0);
		adapter->rx_ring[i].head = IXGBE_RDH(j);
		adapter->rx_ring[i].tail = IXGBE_RDT(j);
#ifdef CONFIG_IXGBE_VMDQ
#if defined(__VMKLNX__)
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_SRRCTL(i), srrctl);
#endif /* defined(__VMKLNX__) */
#endif
	}
#ifdef CONFIG_IXGBE_VMDQ
	/*
	 * For VMDq support of different descriptor types or
	 * buffer sizes through the use of multiple SRRCTL
	 * registers, RDRXCTL.MVMEN must be set to 1
	 *
	 * also, the manual doesn't mention it clearly but DCA hints
	 * will only use queue 0's tags unless this bit is set.  Side
	 * effects of setting this bit are only that SRRCTL must be
	 * fully programmed [0..15]
	 */
#define IXGBE_RDRXCTL_MVMEN 0x00000020
	rdrxctl = IXGBE_READ_REG(hw, IXGBE_RDRXCTL);
	rdrxctl |= IXGBE_RDRXCTL_MVMEN;
	IXGBE_WRITE_REG(hw, IXGBE_RDRXCTL, rdrxctl);

	if (adapter->flags & IXGBE_FLAG_VMDQ_ENABLED) {
	        IXGBE_WRITE_REG(hw, IXGBE_MRQC, 0x0);
	        vmdctl = IXGBE_READ_REG(hw, IXGBE_VMD_CTL);
	        IXGBE_WRITE_REG(hw, IXGBE_VMD_CTL, vmdctl | IXGBE_VMD_CTL_VMDQ_EN);
        }

#endif

	if (adapter->flags & IXGBE_FLAG_RSS_ENABLED) {
		/* Fill out redirection table */
		for (i = 0, j = 0; i < 128; i++, j++) {
			if (j == adapter->ring_feature[RING_F_RSS].indices)
				j = 0;
			/* reta = 4-byte sliding window of
			 * 0x00..(indices-1)(indices-1)00..etc. */
			reta = (reta << 8) | (j * 0x11);
			if ((i & 3) == 3)
				IXGBE_WRITE_REG(hw, IXGBE_RETA(i >> 2), reta);
		}

		/* Fill out hash function seeds */
		/* XXX use a random constant here to glue certain flows */
		get_random_bytes(&random[0], 40);
		for (i = 0; i < 10; i++)
			IXGBE_WRITE_REG(hw, IXGBE_RSSRK(i), random[i]);

		mrqc = IXGBE_MRQC_RSSEN
		    /* Perform hash on these packet types */
		       | IXGBE_MRQC_RSS_FIELD_IPV4
		       | IXGBE_MRQC_RSS_FIELD_IPV4_TCP
		       | IXGBE_MRQC_RSS_FIELD_IPV4_UDP
		       | IXGBE_MRQC_RSS_FIELD_IPV6_EX_TCP
		       | IXGBE_MRQC_RSS_FIELD_IPV6_EX
		       | IXGBE_MRQC_RSS_FIELD_IPV6
		       | IXGBE_MRQC_RSS_FIELD_IPV6_TCP
		       | IXGBE_MRQC_RSS_FIELD_IPV6_UDP
		       | IXGBE_MRQC_RSS_FIELD_IPV6_EX_UDP;
		IXGBE_WRITE_REG(hw, IXGBE_MRQC, mrqc);
	}

	rxcsum = IXGBE_READ_REG(hw, IXGBE_RXCSUM);

	if (adapter->flags & IXGBE_FLAG_RSS_ENABLED ||
	    adapter->flags & IXGBE_FLAG_RX_CSUM_ENABLED) {
		/* Disable indicating checksum in descriptor, enables
		 * RSS hash */
		rxcsum |= IXGBE_RXCSUM_PCSD;
	}
	if (!(rxcsum & IXGBE_RXCSUM_PCSD)) {
		/* Enable IPv4 payload checksum for UDP fragments
		 * if PCSD is not set */
		rxcsum |= IXGBE_RXCSUM_IPPCSE;
	}

	IXGBE_WRITE_REG(hw, IXGBE_RXCSUM, rxcsum);
}

#ifdef NETIF_F_HW_VLAN_TX
static void ixgbe_vlan_rx_register(struct net_device *netdev,
                                   struct vlan_group *grp)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	u32 ctrl;

	if (!test_bit(__IXGBE_DOWN, &adapter->state))
		ixgbe_irq_disable(adapter);
	adapter->vlgrp = grp;


	if (grp) {
		/* enable VLAN tag insert/strip */
		ctrl = IXGBE_READ_REG(&adapter->hw, IXGBE_VLNCTRL);
		ctrl |= IXGBE_VLNCTRL_VME | IXGBE_VLNCTRL_VFE;
		ctrl &= ~IXGBE_VLNCTRL_CFIEN;
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_VLNCTRL, ctrl);
#if defined(__VMKLNX__)
	/* The Intel version has a bug for the case of SW VLAN feature.
 	   Below code enables Oplin to not-drop tagged frames */
	} else {
		/* disable VLAN filtering and tag insert/strip */
		ctrl = IXGBE_READ_REG(&adapter->hw, IXGBE_VLNCTRL);
		ctrl &= ~(IXGBE_VLNCTRL_VME | IXGBE_VLNCTRL_VFE);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_VLNCTRL, ctrl);
#endif /* defined(__VMKLNX__) */
	}

	if (!test_bit(__IXGBE_DOWN, &adapter->state))
		ixgbe_irq_enable(adapter);
}

static void ixgbe_vlan_rx_add_vid(struct net_device *netdev, u16 vid)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct net_device *v_netdev;

	/* add VID to filter table */
	ixgbe_set_vfta(&adapter->hw, vid, 0, true);

	/* Copy feature flags from netdev to the vlan netdev for this vid.
	 * This allows things like TSO to bubble down to our vlan device.
	 */
#if !defined(__VMKLNX__)
	v_netdev = vlan_group_get_device(adapter->vlgrp, vid);
	v_netdev->features |= adapter->netdev->features;
	vlan_group_set_device(adapter->vlgrp, vid, v_netdev);
#endif /* !defined(__VMKLNX__) */
}

static void ixgbe_vlan_rx_kill_vid(struct net_device *netdev, u16 vid)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if (!test_bit(__IXGBE_DOWN, &adapter->state))
		ixgbe_irq_disable(adapter);

	vlan_group_set_device(adapter->vlgrp, vid, NULL);

	if (!test_bit(__IXGBE_DOWN, &adapter->state))
		ixgbe_irq_enable(adapter);

	/* remove VID from filter table */
	ixgbe_set_vfta(&adapter->hw, vid, 0, false);
}

static void ixgbe_restore_vlan(struct ixgbe_adapter *adapter)
{
	ixgbe_vlan_rx_register(adapter->netdev, adapter->vlgrp);

	if (adapter->vlgrp) {
		u16 vid;
		for (vid = 0; vid < VLAN_GROUP_ARRAY_LEN; vid++) {
			if (!vlan_group_get_device(adapter->vlgrp, vid))
				continue;
			ixgbe_vlan_rx_add_vid(adapter->netdev, vid);
		}
	}
}
#endif

static u8 *ixgbe_mc_list_itr(struct ixgbe_hw *hw, u8 **mc_addr_ptr, u32 *vmdq)
{
        struct dev_mc_list *mc_ptr;
        u8 *addr = *mc_addr_ptr;

        mc_ptr = container_of(addr, struct dev_mc_list, dmi_addr[0]);
        if (mc_ptr->next)
                *mc_addr_ptr = mc_ptr->next->dmi_addr;
        else
                *mc_addr_ptr = NULL;
        return addr;
}

/**
 * ixgbe_set_rx_mode - Unicast, Multicast and Promiscuous mode set
 * @netdev: network interface device structure
 *
 * The set_rx_method entry point is called whenever the unicast/multicast 
 * address list or the network interface flags are updated.  This routine is
 * responsible for configuring the hardware for proper unicast, multicast and
 * promiscuous mode, 
 **/
static void ixgbe_set_rx_mode(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u32 fctrl;
	u8 *addr_list = NULL;
	int addr_count=0;

	/* Check for Promiscuous and All Multicast modes */

	fctrl = IXGBE_READ_REG(hw, IXGBE_FCTRL);

	if (netdev->flags & IFF_PROMISC) {
		fctrl |= (IXGBE_FCTRL_UPE | IXGBE_FCTRL_MPE);
	} else if (netdev->flags & IFF_ALLMULTI) {
		fctrl |= IXGBE_FCTRL_MPE;
		fctrl &= ~IXGBE_FCTRL_UPE;
	} else {
		fctrl &= ~(IXGBE_FCTRL_UPE | IXGBE_FCTRL_MPE);
	}

	IXGBE_WRITE_REG(hw, IXGBE_FCTRL, fctrl);

	/* Setup packed array of MAC addresses as required by Shared function */
	addr_count = netdev->mc_count;
	if (addr_count)
		addr_list = netdev->mc_list->dmi_addr;

	ixgbe_update_mc_addr_list(hw, addr_list, addr_count, ixgbe_mc_list_itr);
}

static void ixgbe_napi_enable_all(struct ixgbe_adapter *adapter)
{
#ifdef CONFIG_IXGBE_NAPI
	int q_idx;
	struct ixgbe_q_vector *q_vector;
	int q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;

	/* legacy and MSI only use one vector */
	if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED))
		q_vectors = 1;

	for (q_idx = 0; q_idx < q_vectors; q_idx++) {
		q_vector = &adapter->q_vector[q_idx];
		if (!q_vector->rxr_count)
			continue;
		napi_enable(&q_vector->napi);
	}
#endif
}

static void ixgbe_napi_disable_all(struct ixgbe_adapter *adapter)
{
#ifdef CONFIG_IXGBE_NAPI
	int q_idx;
	struct ixgbe_q_vector *q_vector;
	int q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;

	/* legacy and MSI only use one vector */
	if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED))
		q_vectors = 1;

	for (q_idx = 0; q_idx < q_vectors; q_idx++) {
		q_vector = &adapter->q_vector[q_idx];
		if (!q_vector->rxr_count)
			continue;
		napi_disable(&q_vector->napi);
	}
#endif
}

#ifndef IXGBE_NO_LLI
static void ixgbe_configure_lli(struct ixgbe_adapter *adapter)
{
	u16 port;

	if (adapter->lli_port) {
		/* use filter 0 for port */
		port = ntohs((u16)adapter->lli_port);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_IMIR(0),
		                (port | IXGBE_IMIR_PORT_IM_EN));
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_IMIREXT(0),
		                (IXGBE_IMIREXT_SIZE_BP |
		                 IXGBE_IMIREXT_CTRL_BP));
	}

	if (adapter->flags & IXGBE_FLAG_LLI_PUSH) {
		/* use filter 1 for push flag */
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_IMIR(1),
		                (IXGBE_IMIR_PORT_BP | IXGBE_IMIR_PORT_IM_EN));
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_IMIREXT(1),
		                (IXGBE_IMIREXT_SIZE_BP |
		                 IXGBE_IMIREXT_CTRL_PSH));
	}

	if (adapter->lli_size) {
		/* use filter 2 for size */
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_IMIR(2),
		                (IXGBE_IMIR_PORT_BP | IXGBE_IMIR_PORT_IM_EN));
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_IMIREXT(2),
		                (adapter->lli_size | IXGBE_IMIREXT_CTRL_BP));
	}
}
#endif /* IXGBE_NO_LLI */

static void ixgbe_configure(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int i;

	ixgbe_set_rx_mode(netdev);

#ifdef NETIF_F_HW_VLAN_TX
	ixgbe_restore_vlan(adapter);
#endif

	ixgbe_configure_tx(adapter);
	ixgbe_configure_rx(adapter);

	/* Queues must be enabled before writing tail below */
	for (i = 0; i < adapter->num_rx_queues; i++) {
		int j = 0;
		u32 rxdctl = 0;
		j = adapter->rx_ring[i].reg_idx;
		rxdctl = IXGBE_READ_REG(&adapter->hw, IXGBE_RXDCTL(j));
		rxdctl |= IXGBE_RXDCTL_ENABLE;
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_RXDCTL(j), rxdctl);
	}

	for (i = 0; i < adapter->num_rx_queues; i++)
		ixgbe_alloc_rx_buffers(adapter, &adapter->rx_ring[i],
		                       IXGBE_DESC_UNUSED(&adapter->rx_ring[i]));
}

static int ixgbe_up_complete(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct ixgbe_hw *hw = &adapter->hw;
	int i, j = 0;
	int max_frame = netdev->mtu + ETH_HLEN + ETH_FCS_LEN;
#ifdef IXGBE_TCP_TIMER
	u32 tcp_timer;
#endif
	u32 txdctl, rxdctl, mhadd;
	u32 gpie;

	ixgbe_get_hw_control(adapter);

	if ((adapter->flags & IXGBE_FLAG_MSIX_ENABLED) ||
	    (adapter->flags & IXGBE_FLAG_MSI_ENABLED)) {
		if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
			gpie = (IXGBE_GPIE_MSIX_MODE | IXGBE_GPIE_EIAME |
			        IXGBE_GPIE_PBA_SUPPORT | IXGBE_GPIE_OCD);
		} else {
			/* MSI only */
			gpie = 0;
		}
		/* XXX: to interrupt immediately for EICS writes, enable this */
		/* gpie |= IXGBE_GPIE_EIMEN; */
		IXGBE_WRITE_REG(hw, IXGBE_GPIE, gpie);
#ifdef IXGBE_TCP_TIMER

		tcp_timer = IXGBE_READ_REG(hw, IXGBE_TCPTIMER);
		tcp_timer |= IXGBE_TCPTIMER_DURATION_MASK;
		tcp_timer |= (IXGBE_TCPTIMER_KS |
		              IXGBE_TCPTIMER_COUNT_ENABLE |
		              IXGBE_TCPTIMER_LOOP);
		IXGBE_WRITE_REG(hw, IXGBE_TCPTIMER, tcp_timer);
		tcp_timer = IXGBE_READ_REG(hw, IXGBE_TCPTIMER);
#endif
	}

#ifdef CONFIG_IXGBE_NAPI
	if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED)) {
		/* legacy interrupts, use EIAM to auto-mask when reading EICR,
		 * specifically only auto mask tx and rx interrupts */
		IXGBE_WRITE_REG(hw, IXGBE_EIAM, IXGBE_EICS_RTX_QUEUE);
	}

#endif
	/* Enable fan failure interrupt if media type is copper */
	if (adapter->flags & IXGBE_FLAG_FAN_FAIL_CAPABLE) {
		gpie = IXGBE_READ_REG(hw, IXGBE_GPIE);
		gpie |= IXGBE_SDP1_GPIEN;
		IXGBE_WRITE_REG(hw, IXGBE_GPIE, gpie);
	}

	mhadd = IXGBE_READ_REG(hw, IXGBE_MHADD);
	if (max_frame != (mhadd >> IXGBE_MHADD_MFS_SHIFT)) {
		mhadd &= ~IXGBE_MHADD_MFS_MASK;
		mhadd |= max_frame << IXGBE_MHADD_MFS_SHIFT;

		IXGBE_WRITE_REG(hw, IXGBE_MHADD, mhadd);
	}

	for (i = 0; i < adapter->num_tx_queues; i++) {
		j = adapter->tx_ring[i].reg_idx;
		txdctl = IXGBE_READ_REG(hw, IXGBE_TXDCTL(j));
		txdctl |= IXGBE_TXDCTL_ENABLE;
		IXGBE_WRITE_REG(hw, IXGBE_TXDCTL(j), txdctl);
	}

	for (i = 0; i < adapter->num_rx_queues; i++) {
		j = adapter->rx_ring[i].reg_idx;
		rxdctl = IXGBE_READ_REG(hw, IXGBE_RXDCTL(j));
		/* enable PTHRESH=32 descriptors (half the internal cache)
		 * and HTHRESH=0 descriptors (to minimize latency on fetch),
		 * this also removes a pesky rx_no_buffer_count increment */
		rxdctl |= 0x0020;
		rxdctl |= IXGBE_RXDCTL_ENABLE;
		IXGBE_WRITE_REG(hw, IXGBE_RXDCTL(j), rxdctl);
	}
	/* enable all receives */
	rxdctl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	rxdctl |= (IXGBE_RXCTRL_DMBYPS | IXGBE_RXCTRL_RXEN);
	IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, rxdctl);

	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED)
		ixgbe_configure_msix(adapter);
	else
		ixgbe_configure_msi_and_legacy(adapter);
#ifndef IXGBE_NO_LLI
	/* lli should only be enabled with MSI-X and MSI */
	if (adapter->flags & IXGBE_FLAG_MSI_ENABLED ||
	    adapter->flags & IXGBE_FLAG_MSIX_ENABLED)
		ixgbe_configure_lli(adapter);
#endif

	clear_bit(__IXGBE_DOWN, &adapter->state);
	ixgbe_napi_enable_all(adapter);

	/* clear any pending interrupts, may auto mask */
	IXGBE_READ_REG(hw, IXGBE_EICR);

	ixgbe_irq_enable(adapter);

	/* bring the link up in the watchdog, this could race with our first
	 * link up interrupt but shouldn't be a problem */
	adapter->flags |= IXGBE_FLAG_NEED_LINK_UPDATE;
	adapter->link_check_timeout = jiffies;
	mod_timer(&adapter->watchdog_timer, jiffies);
	return 0;
}

void ixgbe_reinit_locked(struct ixgbe_adapter *adapter)
{
	WARN_ON(in_interrupt());
	while (test_and_set_bit(__IXGBE_RESETTING, &adapter->state))
		msleep(1);
	ixgbe_down(adapter);
	ixgbe_up(adapter);
	clear_bit(__IXGBE_RESETTING, &adapter->state);
#if defined(__VMKLNX__)
        /* Invalidate netqueue state as filters have been lost after reinit */
        vmknetddi_queueops_invalidate_state(adapter->netdev);
#endif        
}

/**
 * PR295512: When changing MTU, reset the NIC and resize number of queues 
 * according to MTU size. This is needed when netPktHeap has limited 
 * low memory space.
 **/
void ixgbe_reinit_locked_change_queues(struct ixgbe_adapter *adapter)
{
        WARN_ON(in_interrupt());
        while (test_and_set_bit(__IXGBE_RESETTING, &adapter->state))
                msleep(1);

        ixgbe_down(adapter);
#if defined(__VMKLNX__)
        struct net_device *netdev = adapter->netdev;

        ixgbe_free_all_rx_resources(adapter);

	/* Resize number of rx queues */
        ixgbe_set_num_queues(adapter);

        /* Resize rx rings */
	ixgbe_set_rx_ring_size(adapter);
	ixgbe_setup_all_rx_resources(adapter);
        
        /* Reset interrupts and interrupt vectors when adjusting number of queues */
        ixgbe_free_irq(adapter);
        ixgbe_reset_interrupt_capability(adapter);
        ixgbe_set_interrupt_capability(adapter);
        ixgbe_request_irq(adapter);

#endif
        ixgbe_up(adapter);
        clear_bit(__IXGBE_RESETTING, &adapter->state);
#if defined(__VMKLNX__)
        /* Invalidate netqueue state as filters have been lost after reinit */
        vmknetddi_queueops_invalidate_state(netdev);
#endif 
}

int ixgbe_up(struct ixgbe_adapter *adapter)
{
	ixgbe_configure(adapter);

	return ixgbe_up_complete(adapter);
}

void ixgbe_reset(struct ixgbe_adapter *adapter)
{
	if (ixgbe_init_hw(&adapter->hw))
		DPRINTK(PROBE, ERR, "Hardware Error\n");

	/* reprogram the RAR[0] in case user changed it. */
	ixgbe_set_rar(&adapter->hw, 0, adapter->hw.mac.addr, IXGBE_RAH_AV);
}

#ifdef CONFIG_PM
static int ixgbe_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	u32 err;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR "ixgbe: Cannot enable PCI device from "
				"suspend\n");
		return err;
	}
	pci_set_master(pdev);

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	if (netif_running(netdev)) {
		err = ixgbe_request_irq(adapter);
		if (err)
			return err;
	}

	ixgbe_reset(adapter);

	if (netif_running(netdev))
		ixgbe_up(adapter);

	netif_device_attach(netdev);

	return 0;
}

#endif /* CONFIG_PM */
/**
 * ixgbe_clean_rx_ring - Free Rx Buffers per Queue
 * @adapter: board private structure
 * @rx_ring: ring to free buffers from
 **/
static void ixgbe_clean_rx_ring(struct ixgbe_adapter *adapter,
                                struct ixgbe_ring *rx_ring)
{
	struct pci_dev *pdev = adapter->pdev;
	unsigned long size;
	unsigned int i;

	/* Free all the Rx ring sk_buffs */

	for (i = 0; i < rx_ring->count; i++) {
		struct ixgbe_rx_buffer *rx_buffer_info;

		rx_buffer_info = &rx_ring->rx_buffer_info[i];
		if (rx_buffer_info->dma) {
			pci_unmap_single(pdev, rx_buffer_info->dma,
			                 adapter->rx_buf_len,
					 PCI_DMA_FROMDEVICE);
			rx_buffer_info->dma = 0;
		}
		if (rx_buffer_info->skb) {
			dev_kfree_skb(rx_buffer_info->skb);
			rx_buffer_info->skb = NULL;
		}
		if (!rx_buffer_info->page)
			continue;
		pci_unmap_page(pdev, rx_buffer_info->page_dma, PAGE_SIZE,
		               PCI_DMA_FROMDEVICE);
		rx_buffer_info->page_dma = 0;

		put_page(rx_buffer_info->page);
		rx_buffer_info->page = NULL;
	}

	size = sizeof(struct ixgbe_rx_buffer) * rx_ring->count;
	memset(rx_ring->rx_buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(rx_ring->desc, 0, rx_ring->size);

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;

	writel(0, adapter->hw.hw_addr + rx_ring->head);
	writel(0, adapter->hw.hw_addr + rx_ring->tail);
}

/**
 * ixgbe_clean_tx_ring - Free Tx Buffers
 * @adapter: board private structure
 * @tx_ring: ring to be cleaned
 **/
static void ixgbe_clean_tx_ring(struct ixgbe_adapter *adapter,
                                struct ixgbe_ring *tx_ring)
{
	struct ixgbe_tx_buffer *tx_buffer_info;
	unsigned long size;
	unsigned int i;

	/* Free all the Tx ring sk_buffs */

	for (i = 0; i < tx_ring->count; i++) {
		tx_buffer_info = &tx_ring->tx_buffer_info[i];
		ixgbe_unmap_and_free_tx_resource(adapter, tx_buffer_info);
	}

	size = sizeof(struct ixgbe_tx_buffer) * tx_ring->count;
	memset(tx_ring->tx_buffer_info, 0, size);

	memset(tx_ring->desc, 0, tx_ring->size);

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;

	writel(0, adapter->hw.hw_addr + tx_ring->head);
	writel(0, adapter->hw.hw_addr + tx_ring->tail);
}

/**
 * ixgbe_clean_all_rx_rings - Free Rx Buffers for all queues
 * @adapter: board private structure
 **/
static void ixgbe_clean_all_rx_rings(struct ixgbe_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		ixgbe_clean_rx_ring(adapter, &adapter->rx_ring[i]);
}

/**
 * ixgbe_clean_all_tx_rings - Free Tx Buffers for all queues
 * @adapter: board private structure
 **/
static void ixgbe_clean_all_tx_rings(struct ixgbe_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		ixgbe_clean_tx_ring(adapter, &adapter->tx_ring[i]);
}

void ixgbe_down(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct ixgbe_hw *hw = &adapter->hw;
	u32 rxctrl;

	/* signal that we are down to the interrupt handler */
	set_bit(__IXGBE_DOWN, &adapter->state);

	/* disable receives */
	rxctrl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, rxctrl & ~IXGBE_RXCTRL_RXEN);

	netif_tx_disable(netdev);

	/* disable transmits in the hardware */

	/* flush both disables */
	IXGBE_WRITE_FLUSH(&adapter->hw);
	msleep(10);

	ixgbe_irq_disable(adapter);

	ixgbe_napi_disable_all(adapter);
	del_timer_sync(&adapter->watchdog_timer);
	/* can't call flush scheduled work here because it can deadlock
	 * if linkwatch_event tries to acquire the rtnl_lock which we are
	 * holding */
	while (adapter->flags & IXGBE_FLAG_IN_WATCHDOG_TASK)
		msleep(1);


	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	ixgbe_reset(adapter);
	ixgbe_clean_all_tx_rings(adapter);
	ixgbe_clean_all_rx_rings(adapter);
}

static int ixgbe_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
#ifdef CONFIG_PM
	int retval = 0;
#endif

	netif_device_detach(netdev);

	if (netif_running(netdev)) {
		ixgbe_down(adapter);
		ixgbe_free_irq(adapter);
	}

#ifdef CONFIG_PM
	retval = pci_save_state(pdev);
	if (retval)
		return retval;
#endif

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	ixgbe_release_hw_control(adapter);

	pci_disable_device(pdev);

	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

#ifndef USE_REBOOT_NOTIFIER
static void ixgbe_shutdown(struct pci_dev *pdev)
{
	ixgbe_suspend(pdev, PMSG_SUSPEND);
}

#endif
#ifdef CONFIG_IXGBE_NAPI
/**
 * ixgbe_poll - NAPI Rx polling callback
 * @napi: structure for representing this polling device
 * @budget: how many packets driver is allowed to clean
 *
 * This function is used for legacy and MSI, NAPI mode
 **/
static int ixgbe_poll(struct napi_struct *napi, int budget)
{
	struct ixgbe_q_vector *q_vector =
	                        container_of(napi, struct ixgbe_q_vector, napi);
	struct ixgbe_adapter *adapter = q_vector->adapter;
	int tx_cleaned, work_done = 0;

#ifdef IXGBE_DCA
	if (adapter->flags & IXGBE_FLAG_DCA_ENABLED) {
		ixgbe_update_tx_dca(adapter, adapter->tx_ring);
		ixgbe_update_rx_dca(adapter, adapter->rx_ring);
	}
#endif

	tx_cleaned = ixgbe_clean_tx_irq(adapter, adapter->tx_ring);
#if defined(__VMKLNX__)
	ixgbe_clean_rx_irq(adapter, adapter->rx_ring, &work_done, budget,
                           find_first_bit(q_vector->rxr_idx, 
                                          adapter->num_rx_queues));
#else /* !defined(__VMKLNX__) */
	ixgbe_clean_rx_irq(adapter, adapter->rx_ring, &work_done, budget);
#endif /* defined(__VMKLNX__) */

	/* If no Tx and not enough Rx work done, exit the polling mode */
	if ((!tx_cleaned && (work_done == 0)) ||
	    !netif_running(adapter->netdev)) {
		if (adapter->itr_setting & 3)
			ixgbe_set_itr(adapter);
		netif_rx_complete(adapter->netdev, napi);
		if (!test_bit(__IXGBE_DOWN, &adapter->state))
			ixgbe_irq_enable(adapter);
		return 0;
	}

	/* for a loop with tx cleanup only tell our budget that we did at least
	 * 1 work, or risk unregister_netdev forever looping */
	if (tx_cleaned && !work_done)
		work_done++;

	return work_done;
}
#endif /* CONFIG_IXGBE_NAPI */

#ifdef IXGBE_BG
void ixgbe_dump(struct ixgbe_adapter* adapter)
{
	struct ixgbe_ring *tx_ring = adapter->tx_ring;
	struct ixgbe_ring *rx_ring = adapter->rx_ring;
	struct ixgbe_hw* hw = &adapter->hw;
	int i, j;
#define NUM_REGS 31 /* 1-based count */
	u32 regs[NUM_REGS];
	u32 *regs_buff = regs;

	char *reg_name[] = {
	"CTRL",  "STATUS",
	"SRRCTL", "RDLEN", "RDH", "RDT", "EICR",
	"EICS", "TDBAL", "TDBAH", "TDLEN", "TDH", "TDT",
	"EIAM", "TXDCTL", "GPIE", "EITR0",
	"TDBAL1", "TDBAH1", "TDLEN1", "TDH1", "TDT1",
	"TXDCTL1", "EIMS",
	"CTRL_EXT", "EIMC",
	"IVAR0", "EIAC", "IMIR", "IMIREXT", "IMIRVP",
	};

	regs_buff[0]  = IXGBE_READ_REG(hw, IXGBE_CTRL);
	regs_buff[1]  = IXGBE_READ_REG(hw, IXGBE_STATUS);

	regs_buff[2]  = IXGBE_READ_REG(hw, IXGBE_SRRCTL(0));
	regs_buff[3]  = IXGBE_READ_REG(hw, IXGBE_RDLEN(0));
	regs_buff[4]  = IXGBE_READ_REG(hw, IXGBE_RDH(0));
	regs_buff[5]  = IXGBE_READ_REG(hw, IXGBE_RDT(0));
	regs_buff[6]  = IXGBE_READ_REG(hw, IXGBE_EICR);

	regs_buff[7]  = IXGBE_READ_REG(hw, IXGBE_EICS);
	regs_buff[8]  = IXGBE_READ_REG(hw, IXGBE_TDBAL(0));
	regs_buff[9]  = IXGBE_READ_REG(hw, IXGBE_TDBAH(0));
	regs_buff[10]  = IXGBE_READ_REG(hw,IXGBE_TDLEN(0));
	regs_buff[11]  = IXGBE_READ_REG(hw,IXGBE_TDH(0));
	regs_buff[12] = IXGBE_READ_REG(hw, IXGBE_TDT(0));
	regs_buff[13]  = IXGBE_READ_REG(hw, IXGBE_EIAM);
	regs_buff[14] = IXGBE_READ_REG(hw, IXGBE_TXDCTL(0));
	regs_buff[15]  = IXGBE_READ_REG(hw, IXGBE_GPIE);
	regs_buff[16]  = IXGBE_READ_REG(hw, IXGBE_EITR(0));

	regs_buff[17]  = IXGBE_READ_REG(hw,IXGBE_TDBAL(1));
	regs_buff[18]  = IXGBE_READ_REG(hw,IXGBE_TDBAH(1));
	regs_buff[19]  = IXGBE_READ_REG(hw,IXGBE_TDLEN(1));
	regs_buff[20]  = IXGBE_READ_REG(hw,IXGBE_TDH(1));
	regs_buff[21] = IXGBE_READ_REG(hw, IXGBE_TDT(1));
	regs_buff[22] = IXGBE_READ_REG(hw, IXGBE_TXDCTL(1));
	regs_buff[23]  = IXGBE_READ_REG(hw, IXGBE_EIMS);
	regs_buff[24] = IXGBE_READ_REG(hw, IXGBE_CTRL_EXT);
	regs_buff[25]  = IXGBE_READ_REG(hw, IXGBE_EIMC);

	regs_buff[26]  = IXGBE_READ_REG(hw, IXGBE_IVAR(0));
	regs_buff[27]  = IXGBE_READ_REG(hw, IXGBE_EIAC);
	regs_buff[28]  = IXGBE_READ_REG(hw, IXGBE_IMIR(0));
	regs_buff[29]  = IXGBE_READ_REG(hw, IXGBE_IMIREXT(0));
	regs_buff[30]  = IXGBE_READ_REG(hw, IXGBE_IMIRVP);
	DPRINTK(DRV, ERR, "Register dump\n");
	for (i = 0; i < NUM_REGS; i++) {
		printk(KERN_ERR "%-15s  %08x\n", reg_name[i], regs_buff[i]);
	}

#if defined(__VMKLNX__)
	for(j = 0; j < adapter->num_rx_queues; j++) {
		printk(KERN_ERR "SRRCTL(%d): %8.8x\n",
		       j,
		       IXGBE_READ_REG(hw, IXGBE_SRRCTL(j)));
	}

	for(j = 0; j < adapter->num_rx_queues; j++) {
		printk(KERN_ERR "RAR(%d): %8.8x:%8.8x\n",
		       j,
		       IXGBE_READ_REG(hw, IXGBE_RAH(j)),
		       IXGBE_READ_REG(hw, IXGBE_RAL(j)));
	}

	for (i = 0; i < adapter->num_rx_queues; i++) {
		j = adapter->rx_ring[i].reg_idx;
		printk(KERN_ERR "RDBAL(%d) : %8.8x\n",
		       i,
		       IXGBE_READ_REG(hw, IXGBE_RDBAL(j)));
		printk(KERN_ERR "RDLEN(%d) : %8.8x\n",
		       i,
		       IXGBE_READ_REG(hw, IXGBE_RDLEN(j)));
		printk(KERN_ERR "RDH(%d) : %8.8x\n",
		       i,
		       IXGBE_READ_REG(hw, IXGBE_RDH(j)));
		printk(KERN_ERR "RDT(%d) : %8.8x\n",
		       i,
		       IXGBE_READ_REG(hw, IXGBE_RDT(j)));
		printk(KERN_ERR "Head Reg Offset: %5.5x Tail Offset: %5.5x\n",
		       adapter->rx_ring[i].head,
		       adapter->rx_ring[i].tail);
	}
#endif /* defined(__VMKLNX__) */

#if defined(__VMKLNX__)
	/*
	 * transmit dump
	 */
	printk(KERN_ERR"TX Desc ring dump\n");

	for (i = 0; i < adapter->num_tx_queues; i++) {
		union ixgbe_adv_tx_desc *tx_desc;
		struct ixgbe_tx_buffer *tx_buffer_info;
		struct my_u { u64 a; u64 b;};
		struct my_u *u;
		printk("Tx ring - %u\n", i);
		printk("Tc[desc ]     [Ce CoCsIpceCoS] [MssHlLROm0Plen] "
		       "[bi->dma       ] leng  ntw timestmp bi->skb\n");
		printk("Td[desc ]     [address 63:0  ] [vl pt Sdcdt ln] "
		       "[bi->dma       ] leng  ntw timestmp bi->skb\n");
		for (j = 0; tx_ring->desc && (j < tx_ring->count); j++ ) {
			tx_desc = IXGBE_TX_DESC_ADV(*tx_ring, j);
			tx_buffer_info = &tx_ring->tx_buffer_info[j];
			u = (struct my_u *)tx_desc;
			printk("T%c[0x%03X]     %016llX %016llX %016llX %04X "
			       " %3X %016llX %p",
			       ((le64_to_cpu(u->b) & (1<<20)) ? 'd' : 'c'),
			       j, le64_to_cpu(u->a), le64_to_cpu(u->b),
			       (u64)tx_buffer_info->dma, tx_buffer_info->length,
			       tx_buffer_info->next_to_watch,
			       (u64)tx_buffer_info->time_stamp,
			       tx_buffer_info->skb);
			if (j == tx_ring->next_to_use &&
			    j == tx_ring->next_to_clean)
				printk(" NTC/U\n");
			else if (j == tx_ring->next_to_use)
				printk(" NTU\n");
			else if (j == tx_ring->next_to_clean)
				printk(" NTC\n");
			else
				printk("\n");
		}
		tx_ring++;
	}

	/*
	 * receive dump
	 */
	printk(KERN_ERR"\nRX Desc ring dump\n");

	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct ixgbe_legacy_rx_desc* rx_desc;
		struct ixgbe_rx_buffer *rx_buffer_info;
		struct my_u { u64 a; u64 b;};
		struct my_u *u;
		printk("Rx ring - %u\n", i);
		printk("R[desc]      [address 63:0  ] [vl er S cks ln] "
		       "[bi->dma       ] [bi->skb]\n");
		for (j = 0; rx_ring->desc && (j < rx_ring->count); j++ ) {
			rx_desc = IXGBE_RX_DESC(*rx_ring, j);
			rx_buffer_info = &rx_ring->rx_buffer_info[j];
			u = (struct my_u *)rx_desc;
			printk("R[0x%03X]     %016llX %016llX %016llX %p",
				j, le64_to_cpu(u->a),le64_to_cpu(u->b),
				(u64)rx_buffer_info->dma, rx_buffer_info->skb);
			if (j == rx_ring->next_to_use)
				printk(" NTU\n");
			else if (j == rx_ring->next_to_clean)
				printk(" NTC\n");
			else
				printk("\n");
		} /* for */
		rx_ring++;
	}
#endif /* defined(__VMKLNX__) */
}

#endif /* IXGBE_BG */
/**
 * ixgbe_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 **/
static void ixgbe_tx_timeout(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	/* Do the reset outside of interrupt context */
	schedule_work(&adapter->reset_task);
}

static void ixgbe_reset_task(struct work_struct *work)
{
	struct ixgbe_adapter *adapter;
	adapter = container_of(work, struct ixgbe_adapter, reset_task);

	adapter->tx_timeout_count++;

#ifdef IXGBE_BG
	ixgbe_dump(adapter);
#endif
	ixgbe_reinit_locked(adapter);
}

static void ixgbe_acquire_msix_vectors(struct ixgbe_adapter *adapter,
                                       int vectors)
{
	int err, vector_threshold;

	/* We'll want at least 3 (vector_threshold):
	 * 1) TxQ[0] Cleanup
	 * 2) RxQ[0] Cleanup
	 * 3) Other (Link Status Change, etc.)
	 * 4) TCP Timer (optional)
	 */
	vector_threshold = MIN_MSIX_COUNT;

	/* The more we get, the more we will assign to Tx/Rx Cleanup
	 * for the separate queues...where Rx Cleanup >= Tx Cleanup.
	 * Right now, we simply care about how many we'll get; we'll
	 * set them up later while requesting irq's.
	 */
	while (vectors >= vector_threshold) {
		err = pci_enable_msix(adapter->pdev, adapter->msix_entries,
		                      vectors);
		if (!err) /* Success in acquiring all requested vectors. */
			break;
		else if (err < IXGBE_SUCCESS)
			vectors = 0; /* Nasty failure, quit now */
		else /* err == number of vectors we should try again with */
			vectors = err;
	}

	if (vectors < vector_threshold) {
		/* Can't allocate enough MSI-X interrupts?  Oh well.
		 * This just means we'll go with either a single MSI
		 * vector or fall back to legacy interrupts.
		 */
		DPRINTK(HW, DEBUG, "Unable to allocate MSI-X interrupts\n");
		adapter->flags &= ~IXGBE_FLAG_MSIX_ENABLED;
		kfree(adapter->msix_entries);
		adapter->msix_entries = NULL;
		adapter->flags &= ~IXGBE_FLAG_RSS_ENABLED;
#ifdef CONFIG_IXGBE_VMDQ
		adapter->flags &= ~IXGBE_FLAG_VMDQ_ENABLED;
#endif
		adapter->num_tx_queues = 1;
#ifdef CONFIG_NETDEVICES_MULTIQUEUE
	/* Notify the stack of the (possibly) reduced Tx Queue count. */
	adapter->netdev->real_num_tx_queues = adapter->num_tx_queues;
#endif
		adapter->num_rx_queues = 1;
	} else {
		adapter->flags |= IXGBE_FLAG_MSIX_ENABLED; /* Woot! */
		adapter->num_msix_vectors = vectors;
	}
#ifdef IXGBE_TCP_TIMER
	{

		int v = 0;
		printk(KERN_ERR"MSIX table dump just after enable_msix %d vectors after the request_irq is done\n", adapter->num_msix_vectors);
		printk("Uppe-addr Addr  data control \n");
		//for (; v < vectors; v++) {
		for (; v < 20; v++) {
		//for (; i < 2; i++) {
		//printk("%016llX %016llX %016llX %016llX\n",
		printk("Vector [%d] 0x%x 0x%x 0x%x 0x%x \n", (v),
				//readl(adapter->msix_addr+(v*4)+1),
				//readl(adapter->msix_addr+(v*4)+0),
				//readl(adapter->msix_addr+(v*4)+2),
				//readl(adapter->msix_addr+(v*4)+3));
				readl(adapter->msix_addr+(v*16)+4),
				readl(adapter->msix_addr+(v*16)+0),
				readl(adapter->msix_addr+(v*16)+8),
				readl(adapter->msix_addr+(v*16)+12));
		}

	}
#endif
			
}

static void __devinit ixgbe_set_num_queues(struct ixgbe_adapter *adapter)
{
	int nrq, ntq;
	int feature_mask = 0, rss_i, rss_m;
#ifdef CONFIG_IXGBE_VMDQ
	int vmdq_i, vmdq_m;
#endif

	/* Number of supported queues */
	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82598EB:
#ifdef CONFIG_IXGBE_VMDQ
		vmdq_i = adapter->ring_feature[RING_F_VMDQ].indices;
		vmdq_m = 0;
#endif
		rss_i = adapter->ring_feature[RING_F_RSS].indices;
		rss_m = 0;
#ifdef CONFIG_IXGBE_VMDQ
		feature_mask |= IXGBE_FLAG_VMDQ_ENABLED;
#endif
#if !defined(__VMKLNX__)
		feature_mask |= IXGBE_FLAG_RSS_ENABLED;
#endif /* !defined(__VMKLNX__) */

		switch (adapter->flags & feature_mask) {
#ifdef CONFIG_IXGBE_VMDQ
		case (IXGBE_FLAG_RSS_ENABLED | IXGBE_FLAG_VMDQ_ENABLED):
			vmdq_i = min(4, vmdq_i);
			vmdq_m = 0x3 << 3;
			rss_m = 0xF;
			nrq = vmdq_i * rss_i;
			ntq = min(MAX_TX_QUEUES, vmdq_i * rss_i);
			break;
		case (IXGBE_FLAG_VMDQ_ENABLED):
			vmdq_m = 0xF;
			nrq = vmdq_i;
#if !defined(__VMKLNX__)
			ntq = vmdq_i;
#else /* defined(__VMKLNX__) */
			if (adapter->flags & IXGBE_FLAG_VMDQ_TX_ENABLED)
				ntq = nrq;
			else
				ntq = 1;
#endif /* !defined(__VMKLNX__) */
			break;
		case (IXGBE_FLAG_RSS_ENABLED):
			rss_m = 0xF;
			nrq = rss_i;
#ifdef CONFIG_NETDEVICES_MULTIQUEUE
			ntq = rss_i;
#else
			ntq = 1;
#endif
			break;
		case 0:
		default:
			rss_i = 0;
			rss_m = 0;
#ifdef CONFIG_IXGBE_VMDQ
			vmdq_i = 0;
			vmdq_m = 0;
#endif
			nrq = 1;
			ntq = 1;
#endif /* CONFIG_IXGBE_VMDQ */
			break;
		}

#ifdef CONFIG_IXGBE_VMDQ
		adapter->ring_feature[RING_F_VMDQ].indices = vmdq_i;
		adapter->ring_feature[RING_F_VMDQ].mask = vmdq_m;
#endif
		adapter->ring_feature[RING_F_RSS].indices = rss_i;
		adapter->ring_feature[RING_F_RSS].mask = rss_m;
		break;
	default:
		nrq = 1;
		ntq = 1;
		break;
	}
#if defined(__VMKLNX__)
        /*
         * See PR 295512: For Jumbo Frame, we limit number of netqueues
         * to 4 to support multiple ports with limited netPktHeap
         */
        if (adapter->netdev->mtu > ETH_DATA_LEN) {
                nrq = min(4, nrq);
        }

        /*
         * Limit the nrq and ntq to num_online_cpus
         * to make each vector services either tx or rx queue alone.
         */
	nrq = min((int)num_online_cpus(),nrq);
	ntq = min((int)num_online_cpus(),ntq);
#endif
	adapter->num_rx_queues = nrq;
	adapter->num_tx_queues = ntq;
#ifdef CONFIG_NETDEVICES_MULTIQUEUE
	/* Notify the stack of the (possibly) reduced Tx Queue count. */
	adapter->netdev->real_num_tx_queues = adapter->num_tx_queues;
#endif
}

/**
 * ixgbe_cache_ring_register - Descriptor ring to register mapping
 * @adapter: board private structure to initialize
 *
 * Once we know the feature-set enabled for the device, we'll cache
 * the register offset the descriptor ring is assigned to.
 **/
static void __devinit ixgbe_cache_ring_register(struct ixgbe_adapter *adapter)
{
	/* TODO: Remove all uses of the indices in the cases where multiple
	 *       features are OR'd together, if the feature set makes sense.
	 */
	int feature_mask = 0, rss_i;
	int i, txr_idx, rxr_idx;
#ifdef CONFIG_IXGBE_VMDQ
	int vmdq_i;
#endif

	/* Number of supported queues */
	switch (adapter->hw.mac.type) {
#ifdef CONFIG_IXGBE_MQ
	case ixgbe_mac_82598EB:
#ifdef CONFIG_IXGBE_VMDQ
		vmdq_i = adapter->ring_feature[RING_F_VMDQ].indices;
#endif
		rss_i = adapter->ring_feature[RING_F_RSS].indices;
		txr_idx = 0;
		rxr_idx = 0;
#ifdef CONFIG_IXGBE_VMDQ
		feature_mask |= IXGBE_FLAG_VMDQ_ENABLED;
#endif
		switch (adapter->flags & feature_mask) {
#ifdef CONFIG_IXGBE_VMDQ
#if !defined(__VMKLNX__)
		case (IXGBE_FLAG_RSS_ENABLED | IXGBE_FLAG_VMDQ_ENABLED):
			for (i = 0; i < vmdq_i; i++) {
			int j;
			for (j = 0; j < rss_i; j++) {
				adapter->rx_ring[rxr_idx].reg_idx = i << 4 | j;
				/* probably not correct for ntq < nrq case */
				adapter->tx_ring[txr_idx].reg_idx = i << 3 |
				                                    (j >> 1);
				rxr_idx++;
				if (j & 1)
					txr_idx++;
			}
			}
			break;
#endif /* !defined(__VMKLNX__) */
		case (IXGBE_FLAG_VMDQ_ENABLED):
#if defined(__VMKLNX__)
			for (i = 0; i < vmdq_i; i++)
				adapter->rx_ring[i].reg_idx = i;
			/* The Intel version has a bug for the case of ntq < nrq;
			   The bug will lead to memory corruption which 
			   resulted in "no communication" (only in OPT build).
			 */
			for (i = 0; i < adapter->num_tx_queues; i++)
				adapter->tx_ring[i].reg_idx = i;
#else /* !defined(__VMKLNX__) */
			for (i = 0; i < vmdq_i; i++) {
				adapter->rx_ring[i].reg_idx = i;
				adapter->tx_ring[i].reg_idx = i;
			}
#endif /* defined(__VMKLNX__) */
			break;
#endif
		case (IXGBE_FLAG_RSS_ENABLED):
			for (i = 0; i < adapter->num_rx_queues; i++)
				adapter->rx_ring[i].reg_idx = i;
			for (i = 0; i < adapter->num_tx_queues; i++)
				adapter->tx_ring[i].reg_idx = i;
			break;
		case 0:
		default:
			break;
		}
		break;
#endif
	default:
		break;
	}
}

/**
 * ixgbe_alloc_queues - Allocate memory for all rings
 * @adapter: board private structure to initialize
 *
 * We allocate one ring per queue at run-time since we don't know the
 * number of queues at compile-time.  The polling_netdev array is
 * intended for Multiqueue, but should work fine with a single queue.
 **/
static int __devinit ixgbe_alloc_queues(struct ixgbe_adapter *adapter)
{
	int i;
        
	adapter->tx_ring = kcalloc(adapter->num_tx_queues,
	                           sizeof(struct ixgbe_ring), GFP_KERNEL);
	if (!adapter->tx_ring)
		goto err_tx_ring_allocation;

	adapter->rx_ring = kcalloc(adapter->num_rx_queues,
	                           sizeof(struct ixgbe_ring), GFP_KERNEL);
	if (!adapter->rx_ring)
		goto err_rx_ring_allocation;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		adapter->tx_ring[i].count = IXGBE_DEFAULT_TXD;
		adapter->tx_ring[i].queue_index = i;
	}

#if defined(__VMKLNX__)
	ixgbe_set_rx_ring_size(adapter);
#else
	for (i = 0; i < adapter->num_rx_queues; i++) {
		adapter->rx_ring[i].count = rx_count;
		adapter->rx_ring[i].queue_index = i;
	}
#endif
        
	ixgbe_cache_ring_register(adapter);

	return IXGBE_SUCCESS;

err_rx_ring_allocation:
	kfree(adapter->tx_ring);
err_tx_ring_allocation:
	return -ENOMEM;
}

#if defined(__VMKLNX__)
int ixgbe_calculate_rx_ring_size(struct ixgbe_adapter *adapter)
{
        u32 rx_count = IXGBE_DEFAULT_RXD;

	if (adapter->netdev->mtu > ETH_DATA_LEN) {
                rx_count = min(IXGBE_MAX_RXD / adapter->num_rx_queues,
			       IXGBE_JUMBO_FRAME_DEFAULT_RXD); 
        } else {
                rx_count = min(IXGBE_MAX_RXD / adapter->num_rx_queues,
                               rx_count);
        }

        rx_count = max(rx_count, (u32)IXGBE_MIN_RXD);
	rx_count = ALIGN(rx_count, IXGBE_REQ_RX_DESCRIPTOR_MULTIPLE);
	
	return rx_count;                
}
/**
 * ixgbe_set_rx_ring_size
 **/
static int ixgbe_set_rx_ring_size(struct ixgbe_adapter *adapter)
{
        int i;
        u32 rx_count = IXGBE_DEFAULT_RXD;

        if(adapter->rx_ring){
                if (adapter->num_rx_queues > 1) {
                        rx_count = ixgbe_calculate_rx_ring_size(adapter); 
                        DPRINTK(PROBE, INFO, "using rx_count = %d \n", rx_count);
                }
		for (i = 0; i < adapter->num_rx_queues; i++) {
			adapter->rx_ring[i].count = rx_count;
			adapter->rx_ring[i].queue_index = i;
		}
                return IXGBE_SUCCESS;                
        }
        else
                return -ENOMEM;
}
#endif


/**
 * ixgbe_set_interrupt_capability - set MSI-X or MSI if supported
 * @adapter: board private structure to initialize
 *
 * Attempt to configure the interrupts using the best available
 * capabilities of the hardware and the kernel.
 **/
static int __devinit ixgbe_set_interrupt_capability(
                                                  struct ixgbe_adapter *adapter)
{
	int err = IXGBE_SUCCESS;
	int vector, v_budget;

	if (!(adapter->flags & IXGBE_FLAG_MSIX_CAPABLE))
		goto try_msi;

	/*
	 * It's easy to be greedy for MSI-X vectors, but it really
	 * doesn't do us much good if we have a lot more vectors
	 * than CPU's.  So let's be conservative and only ask for
	 * (roughly) twice the number of vectors as there are CPU's.
	 */
	v_budget = min(adapter->num_rx_queues + adapter->num_tx_queues,
	               (int)(num_online_cpus() * 2)) + NON_Q_VECTORS;

	/*
	 * At the same time, hardware can only support a maximum of
	 * MAX_MSIX_COUNT vectors.  With features such as RSS and VMDq,
	 * we can easily reach upwards of 64 Rx descriptor queues and
	 * 32 Tx queues.  Thus, we cap it off in those rare cases where
	 * the cpu count also exceeds our vector limit.
	 */
	v_budget = min(v_budget, MAX_MSIX_COUNT);

	/* A failure in MSI-X entry allocation isn't fatal, but it does
	 * mean we disable MSI-X capabilities of the adapter. */
	adapter->msix_entries = kcalloc(v_budget,
	                                sizeof(struct msix_entry), GFP_KERNEL);
	if (!adapter->msix_entries) {
#ifdef CONFIG_IXGBE_VMDQ
		adapter->flags &= ~IXGBE_FLAG_VMDQ_ENABLED;
#endif
		adapter->flags &= ~IXGBE_FLAG_RSS_ENABLED;
		ixgbe_set_num_queues(adapter);
		kfree(adapter->tx_ring);
		kfree(adapter->rx_ring);
		err = ixgbe_alloc_queues(adapter);
		if (err) {
			DPRINTK(PROBE, ERR, "Unable to allocate memory "
			                    "for queues\n");
			goto out;
		}

		goto try_msi;
	}

	for (vector = 0; vector < v_budget; vector++)
		adapter->msix_entries[vector].entry = vector;

	ixgbe_acquire_msix_vectors(adapter, v_budget);

	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED)
		goto out;

try_msi:
	if (!(adapter->flags & IXGBE_FLAG_MSI_CAPABLE))
		goto out;

	err = pci_enable_msi(adapter->pdev);
	if (!err) {
		adapter->flags |= IXGBE_FLAG_MSI_ENABLED;
	} else {
		DPRINTK(HW, DEBUG, "Unable to allocate MSI interrupt, "
		                   "falling back to legacy.  Error: %d\n", err);
		/* reset err */
		err = IXGBE_SUCCESS;
	}

out:

	return err;
}

static void ixgbe_reset_interrupt_capability(struct ixgbe_adapter *adapter)
{
	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
		adapter->flags &= ~IXGBE_FLAG_MSIX_ENABLED;
		pci_disable_msix(adapter->pdev);
		kfree(adapter->msix_entries);
		adapter->msix_entries = NULL;
	} else if (adapter->flags & IXGBE_FLAG_MSI_ENABLED) {
		adapter->flags &= ~IXGBE_FLAG_MSI_ENABLED;
		pci_disable_msi(adapter->pdev);
	}
	return;
}

/**
 * ixgbe_init_interrupt_scheme - Determine proper interrupt scheme
 * @adapter: board private structure to initialize
 *
 * We determine which interrupt scheme to use based on...
 * - Kernel support (MSI, MSI-X)
 *   - which can be user-defined (via MODULE_PARAM)
 * - Hardware queue count (num_*_queues)
 *   - defined by miscellaneous hardware support/features (RSS, etc.)
 **/
static int __devinit ixgbe_init_interrupt_scheme(struct ixgbe_adapter *adapter)
{
	int err;

	/* Number of supported queues */
	ixgbe_set_num_queues(adapter);

	err = ixgbe_alloc_queues(adapter);
	if (err) {
		DPRINTK(PROBE, ERR, "Unable to allocate memory for queues\n");
		goto err_alloc_queues;
	}

	err = ixgbe_set_interrupt_capability(adapter);
	if (err) {
		DPRINTK(PROBE, ERR, "Unable to setup interrupt capabilities\n");
		goto err_set_interrupt;
	}

	DPRINTK(DRV, INFO, "Multiqueue %s: Rx Queue count = %u, "
	                   "Tx Queue count = %u\n",
	        (adapter->num_rx_queues > 1) ? "Enabled" :
	        "Disabled", adapter->num_rx_queues, adapter->num_tx_queues);

	set_bit(__IXGBE_DOWN, &adapter->state);

	return IXGBE_SUCCESS;

err_set_interrupt:
	kfree(adapter->tx_ring);
	kfree(adapter->rx_ring);
err_alloc_queues:
	return err;
}

/**
 * ixgbe_sw_init - Initialize general software structures (struct ixgbe_adapter)
 * @adapter: board private structure to initialize
 *
 * ixgbe_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 **/
static int __devinit ixgbe_sw_init(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct pci_dev *pdev = adapter->pdev;
	int err;

	/* PCI config space info */

	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_device_id = pdev->subsystem_device;

	pci_read_config_byte(pdev, PCI_REVISION_ID, &hw->revision_id);

	err = ixgbe_init_shared_code(hw);
	if (err) {
		DPRINTK(PROBE, ERR, "Init_shared_code failed\n");
		goto out;
	}

	/* Set capability flags */
	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82598EB:
		if (ixgbe_get_media_type(&adapter->hw) == ixgbe_media_type_copper)
			adapter->flags |= IXGBE_FLAG_FAN_FAIL_CAPABLE;
		adapter->flags |= IXGBE_FLAG_DCA_CAPABLE;
		adapter->flags |= IXGBE_FLAG_MSI_CAPABLE;
		adapter->flags |= IXGBE_FLAG_MSIX_CAPABLE;
		if (adapter->flags & IXGBE_FLAG_MSIX_CAPABLE)
			adapter->flags |= IXGBE_FLAG_MQ_CAPABLE;
#ifdef CONFIG_IXGBE_RSS
		if (adapter->flags & IXGBE_FLAG_MQ_CAPABLE)
			adapter->flags |= IXGBE_FLAG_RSS_CAPABLE;
#endif
#ifdef CONFIG_IXGBE_VMDQ
		if (adapter->flags & IXGBE_FLAG_MQ_CAPABLE)
			adapter->flags |= IXGBE_FLAG_VMDQ_CAPABLE;
#endif
#if defined(__VMKLNX__)
		/* Disable explicitely the packet split for VMware. */
		adapter->flags &= ~IXGBE_FLAG_RX_PS_ENABLED;
		adapter->flags &= ~IXGBE_FLAG_RX_PS_CAPABLE;
#endif /* defined(__VMKLNX__) */
		break;
	default:
		break;
	}


	/* default flow control settings */
	hw->fc.original_type = ixgbe_fc_full;
	hw->fc.type = ixgbe_fc_full;
	hw->fc.high_water = IXGBE_DEFAULT_FCRTH;
	hw->fc.low_water = IXGBE_DEFAULT_FCRTL;
	hw->fc.pause_time = IXGBE_DEFAULT_FCPAUSE;
	hw->fc.send_xon = true;

	/* select 10G link by default */
	hw->mac.link_mode_select = IXGBE_AUTOC_LMS_10G_LINK_NO_AN;

	/* set defaults for eitr in MegaBytes */
	adapter->eitr_low = 10;
	adapter->eitr_high = 20;

	/* enable rx csum by default */
	adapter->flags |= IXGBE_FLAG_RX_CSUM_ENABLED;

	set_bit(__IXGBE_DOWN, &adapter->state);
out:
	return err;
}

/**
 * ixgbe_setup_tx_resources - allocate Tx resources (Descriptors)
 * @adapter: board private structure
 * @txdr:    tx descriptor ring (for a specific queue) to setup
 *
 * Return 0 on success, negative on failure
 **/
int ixgbe_setup_tx_resources(struct ixgbe_adapter *adapter,
                             struct ixgbe_ring *txdr)
{
	struct pci_dev *pdev = adapter->pdev;
	int size;

	size = sizeof(struct ixgbe_tx_buffer) * txdr->count;
	txdr->tx_buffer_info = vmalloc(size);
	if (!txdr->tx_buffer_info) {
		DPRINTK(PROBE, ERR,
		        "Unable to allocate memory for "
		        "the transmit descriptor ring\n");
		return -ENOMEM;
	}
	memset(txdr->tx_buffer_info, 0, size);

	/* round up to nearest 4K */
	txdr->size = txdr->count * sizeof(union ixgbe_adv_tx_desc);
	txdr->size = ALIGN(txdr->size, 4096);

	txdr->desc = pci_alloc_consistent(pdev, txdr->size, &txdr->dma);
	if (!txdr->desc) {
		vfree(txdr->tx_buffer_info);
		DPRINTK(PROBE, ERR,
		        "Unable to allocate memory for "
		        "the transmit descriptor ring\n");
		return -ENOMEM;
	}

	txdr->next_to_use = 0;
	txdr->next_to_clean = 0;
	txdr->work_limit = txdr->count;

	return 0;
}

/**
 * ixgbe_setup_all_tx_resources - allocate all queues Tx resources
 * @adapter: board private structure
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/

static int ixgbe_setup_all_tx_resources(struct ixgbe_adapter *adapter)
{
	int i, err = 0;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		err = ixgbe_setup_tx_resources(adapter, &adapter->tx_ring[i]);
		if (!err)
			continue;		
		DPRINTK(PROBE, ERR, "Allocation for Tx Queue %u failed\n", i);
		break;
	}
	return err;
}

/**
 * ixgbe_setup_rx_resources - allocate Rx resources (Descriptors)
 * @adapter: board private structure
 * @rxdr:    rx descriptor ring (for a specific queue) to setup
 *
 * Returns 0 on success, negative on failure
 **/

int ixgbe_setup_rx_resources(struct ixgbe_adapter *adapter,
                             struct ixgbe_ring *rxdr)
{
	struct pci_dev *pdev = adapter->pdev;
	int size;

	size = sizeof(struct ixgbe_rx_buffer) * rxdr->count;
	rxdr->rx_buffer_info = vmalloc(size);
	if (!rxdr->rx_buffer_info) {
		DPRINTK(PROBE, ERR,
		        "Unable to vmalloc buffer memory for "
		        "the receive descriptor ring\n");
		return -ENOMEM;
	}
	memset(rxdr->rx_buffer_info, 0, size);

	/* Round up to nearest 4K */
	rxdr->size = rxdr->count * sizeof(union ixgbe_adv_rx_desc);
	rxdr->size = ALIGN(rxdr->size, 4096);

	rxdr->desc = pci_alloc_consistent(pdev, rxdr->size, &rxdr->dma);

	if (!rxdr->desc) {
		DPRINTK(PROBE, ERR,
		        "Unable to allocate memory for "
		        "the receive descriptor ring\n");
		vfree(rxdr->rx_buffer_info);
		return -ENOMEM;
	}

	rxdr->next_to_clean = 0;
	rxdr->next_to_use = 0;
#ifndef CONFIG_IXGBE_NAPI
	rxdr->work_limit = rxdr->count / 2;
#endif

#ifndef IXGBE_NO_LRO
	ixgbe_lro_ring_init(&rxdr->lrolist, adapter);
#endif
	return 0;
}

/**
 * ixgbe_setup_all_rx_resources - allocate all queues Rx resources
 * @adapter: board private structure
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/

static int ixgbe_setup_all_rx_resources(struct ixgbe_adapter *adapter)
{
	int i, err = 0;

	for (i = 0; i < adapter->num_rx_queues; i++) {
		err = ixgbe_setup_rx_resources(adapter, &adapter->rx_ring[i]);
		if (!err)
			continue;
		DPRINTK(PROBE, ERR, "Allocation for Rx Queue %u failed\n", i);
		break;
	}
	return err;
}

/**
 * ixgbe_free_tx_resources - Free Tx Resources per Queue
 * @adapter: board private structure
 * @tx_ring: Tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 **/

static void ixgbe_free_tx_resources(struct ixgbe_adapter *adapter,
                             struct ixgbe_ring *tx_ring)
{
	struct pci_dev *pdev = adapter->pdev;

	ixgbe_clean_tx_ring(adapter, tx_ring);

	vfree(tx_ring->tx_buffer_info);
	tx_ring->tx_buffer_info = NULL;

	pci_free_consistent(pdev, tx_ring->size, tx_ring->desc, tx_ring->dma);

	tx_ring->desc = NULL;
}

/**
 * ixgbe_free_all_tx_resources - Free Tx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all transmit software resources
 **/
static void ixgbe_free_all_tx_resources(struct ixgbe_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		ixgbe_free_tx_resources(adapter, &adapter->tx_ring[i]);
}

/**
 * ixgbe_free_rx_resources - Free Rx Resources
 * @adapter: board private structure
 * @rx_ring: ring to clean the resources from
 *
 * Free all receive software resources
 **/
static void ixgbe_free_rx_resources(struct ixgbe_adapter *adapter,
                             struct ixgbe_ring *rx_ring)
{
	struct pci_dev *pdev = adapter->pdev;

#ifndef IXGBE_NO_LRO
	ixgbe_lro_ring_exit(&rx_ring->lrolist);
#endif
	ixgbe_clean_rx_ring(adapter, rx_ring);

	vfree(rx_ring->rx_buffer_info);
	rx_ring->rx_buffer_info = NULL;

	pci_free_consistent(pdev, rx_ring->size, rx_ring->desc, rx_ring->dma);

	rx_ring->desc = NULL;
}

/**
 * ixgbe_free_all_rx_resources - Free Rx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all receive software resources
 **/

static void ixgbe_free_all_rx_resources(struct ixgbe_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		ixgbe_free_rx_resources(adapter, &adapter->rx_ring[i]);
}

/**
 * ixgbe_change_mtu - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 **/
static int ixgbe_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	int max_frame = new_mtu + ETH_HLEN + ETH_FCS_LEN;
#if defined(__VMKLNX__)
        int old_mtu = netdev->mtu;        
#endif        
        
	if ((max_frame < (ETH_ZLEN + ETH_FCS_LEN)) ||
	    (max_frame > IXGBE_MAX_JUMBO_FRAME_SIZE))
		return -EINVAL;

	DPRINTK(PROBE, INFO, "changing MTU from %d to %d\n",
	        netdev->mtu, new_mtu);
	/* must set new MTU before calling down or up */
	netdev->mtu = new_mtu;

	if (netif_running(netdev))

#if defined(__VMKLNX__)
        {
                /* Ensure that oplin reset does not trip NetWatchDogTimeout,
                 * as chip re-init takes longer time after resizing rx_rings
                 * and MSIX vectors.
                 */
                netdev->trans_start = jiffies; 
                
                if ((old_mtu <= ETH_DATA_LEN && new_mtu > ETH_DATA_LEN) ||
                    (old_mtu > ETH_DATA_LEN && new_mtu <= ETH_DATA_LEN)) {
                        ixgbe_reinit_locked_change_queues(adapter);
                }
                else {                        
                        ixgbe_reinit_locked(adapter);
                }
        }        
#else        
       ixgbe_reinit_locked(adapter);
#endif                
	return 0;
}

/**
 * ixgbe_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 **/
static int ixgbe_open(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	int err;

	/* disallow open during test */
	if (test_bit(__IXGBE_TESTING, &adapter->state))
		return -EBUSY;
	/* allocate transmit descriptors */
	err = ixgbe_setup_all_tx_resources(adapter);
	if (err)
		goto err_setup_tx;

	/* allocate receive descriptors */
	err = ixgbe_setup_all_rx_resources(adapter);
	if (err)
		goto err_setup_rx;

	ixgbe_configure(adapter);

	err = ixgbe_request_irq(adapter);
	if (err)
		goto err_req_irq;

	err = ixgbe_up_complete(adapter);
	if (err)
		goto err_up;

	return IXGBE_SUCCESS;

err_up:
	ixgbe_release_hw_control(adapter);
	ixgbe_free_irq(adapter);
err_req_irq:
	ixgbe_free_all_rx_resources(adapter);
err_setup_rx:
	ixgbe_free_all_tx_resources(adapter);
err_setup_tx:
	ixgbe_reset(adapter);

	return err;
}

/**
 * ixgbe_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 **/
static int ixgbe_close(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	ixgbe_down(adapter);
	ixgbe_free_irq(adapter);

	ixgbe_free_all_tx_resources(adapter);
	ixgbe_free_all_rx_resources(adapter);

	ixgbe_release_hw_control(adapter);

	return 0;
}

/**
 * ixgbe_update_stats - Update the board statistics counters.
 * @adapter: board private structure
 **/
void ixgbe_update_stats(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u64 total_mpc = 0;
	u32 i, missed_rx = 0, mpc, bprc, lxon, lxoff, xon_off_tot;

	adapter->stats.crcerrs += IXGBE_READ_REG(hw, IXGBE_CRCERRS);
	for (i = 0; i < 8; i++) {
		/* for packet buffers not used, the register should read 0 */
		mpc = IXGBE_READ_REG(hw, IXGBE_MPC(i));
		missed_rx += mpc;
		adapter->stats.mpc[i] += mpc;
		total_mpc += adapter->stats.mpc[i];
		adapter->stats.rnbc[i] += IXGBE_READ_REG(hw, IXGBE_RNBC(i));
	}
	adapter->stats.gprc += IXGBE_READ_REG(hw, IXGBE_GPRC);
	/* work around hardware counting issue */
	adapter->stats.gprc -= missed_rx;

	/* 82598 hardware only has a 32 bit counter in the high register */
	adapter->stats.gorc += IXGBE_READ_REG(hw, IXGBE_GORCH);
	adapter->stats.gotc += IXGBE_READ_REG(hw, IXGBE_GOTCH);
	adapter->stats.tor += IXGBE_READ_REG(hw, IXGBE_TORH);
	bprc = IXGBE_READ_REG(hw, IXGBE_BPRC);
	adapter->stats.bprc += bprc;
	adapter->stats.mprc += IXGBE_READ_REG(hw, IXGBE_MPRC);
	adapter->stats.mprc -= bprc;
	adapter->stats.roc += IXGBE_READ_REG(hw, IXGBE_ROC);
	adapter->stats.prc64 += IXGBE_READ_REG(hw, IXGBE_PRC64);
	adapter->stats.prc127 += IXGBE_READ_REG(hw, IXGBE_PRC127);
	adapter->stats.prc255 += IXGBE_READ_REG(hw, IXGBE_PRC255);
	adapter->stats.prc511 += IXGBE_READ_REG(hw, IXGBE_PRC511);
	adapter->stats.prc1023 += IXGBE_READ_REG(hw, IXGBE_PRC1023);
	adapter->stats.prc1522 += IXGBE_READ_REG(hw, IXGBE_PRC1522);
	adapter->stats.rlec += IXGBE_READ_REG(hw, IXGBE_RLEC);
	adapter->stats.lxonrxc += IXGBE_READ_REG(hw, IXGBE_LXONRXC);
	adapter->stats.lxoffrxc += IXGBE_READ_REG(hw, IXGBE_LXOFFRXC);
	lxon = IXGBE_READ_REG(hw, IXGBE_LXONTXC);
	adapter->stats.lxontxc += lxon;
	lxoff = IXGBE_READ_REG(hw, IXGBE_LXOFFTXC);
	adapter->stats.lxofftxc += lxoff;
	adapter->stats.ruc += IXGBE_READ_REG(hw, IXGBE_RUC);
	adapter->stats.gptc += IXGBE_READ_REG(hw, IXGBE_GPTC);
	adapter->stats.mptc += IXGBE_READ_REG(hw, IXGBE_MPTC);
	/*
	 * 82598 errata - tx of flow control packets is included in tx counters
	 */
	xon_off_tot = lxon + lxoff;
	adapter->stats.gptc -= xon_off_tot;
	adapter->stats.mptc -= xon_off_tot;
	adapter->stats.gotc -= (xon_off_tot * (ETH_ZLEN + ETH_FCS_LEN));
	adapter->stats.ruc += IXGBE_READ_REG(hw, IXGBE_RUC);
	adapter->stats.rfc += IXGBE_READ_REG(hw, IXGBE_RFC);
	adapter->stats.rjc += IXGBE_READ_REG(hw, IXGBE_RJC);
	adapter->stats.tpr += IXGBE_READ_REG(hw, IXGBE_TPR);
	adapter->stats.ptc64 += IXGBE_READ_REG(hw, IXGBE_PTC64);
	adapter->stats.ptc64 -= xon_off_tot;
	adapter->stats.ptc127 += IXGBE_READ_REG(hw, IXGBE_PTC127);
	adapter->stats.ptc255 += IXGBE_READ_REG(hw, IXGBE_PTC255);
	adapter->stats.ptc511 += IXGBE_READ_REG(hw, IXGBE_PTC511);
	adapter->stats.ptc1023 += IXGBE_READ_REG(hw, IXGBE_PTC1023);
	adapter->stats.ptc1522 += IXGBE_READ_REG(hw, IXGBE_PTC1522);
	adapter->stats.bptc += IXGBE_READ_REG(hw, IXGBE_BPTC);

	/* Fill out the OS statistics structure */
	adapter->net_stats.multicast = adapter->stats.mprc;

	/* Rx Errors */
	adapter->net_stats.rx_errors = adapter->stats.crcerrs +
	                               adapter->stats.rlec;
	adapter->net_stats.rx_dropped = 0;
	adapter->net_stats.rx_length_errors = adapter->stats.rlec;
	adapter->net_stats.rx_crc_errors = adapter->stats.crcerrs;
	adapter->net_stats.rx_missed_errors = total_mpc;
}

/**
 * ixgbe_watchdog - Timer Call-back
 * @data: pointer to adapter cast into an unsigned long
 **/
static void ixgbe_watchdog(unsigned long data)
{
	struct ixgbe_adapter *adapter = (struct ixgbe_adapter *)data;
	struct ixgbe_hw *hw = &adapter->hw;

	/* Do the watchdog outside of interrupt context due to the lovely
	 * delays that some of the newer hardware requires */
	if (!test_bit(__IXGBE_DOWN, &adapter->state)) {
		/* Cause software interrupt to ensure rx rings are cleaned */
		if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
			u32 eics =
			 (1 << (adapter->num_msix_vectors - NON_Q_VECTORS)) - 1;
			IXGBE_WRITE_REG(hw, IXGBE_EICS, eics);
		} else {
			/* for legacy and MSI interrupts don't set any bits that
			 * are enabled for EIAM, because this operation would
			 * set *both* EIMS and EICS for any bit in EIAM */
			IXGBE_WRITE_REG(hw, IXGBE_EICS,
			             (IXGBE_EICS_TCP_TIMER | IXGBE_EICS_OTHER));
		}
		/* Reset the timer */
		mod_timer(&adapter->watchdog_timer,
		          round_jiffies(jiffies + 2 * HZ));
	}

	schedule_work(&adapter->watchdog_task);
}

/**
 * ixgbe_watchdog_task - worker thread to bring link up
 * @work: pointer to work_struct containing our data
 **/
static void ixgbe_watchdog_task(struct work_struct *work)
{
	struct ixgbe_adapter *adapter = container_of(work,
	                                             struct ixgbe_adapter,
	                                             watchdog_task);
	struct net_device *netdev = adapter->netdev;
	struct ixgbe_hw *hw = &adapter->hw;
	u32 link_speed = adapter->link_speed;
	bool link_up = adapter->link_up;
#if defined(__VMKLNX__)
	struct ixgbe_ring *tx_ring;
	int some_tx_pending = 0;
#endif /* defined(__VMKLNX__) */
#ifdef CONFIG_NETDEVICES_MULTIQUEUE
	int i;
#endif

	adapter->flags |= IXGBE_FLAG_IN_WATCHDOG_TASK;

	if (adapter->flags & IXGBE_FLAG_NEED_LINK_UPDATE) {
		ixgbe_check_link(hw, &link_speed, &link_up, false);
		if (link_up ||
		    time_after(jiffies, (adapter->link_check_timeout +
		                         IXGBE_TRY_LINK_TIMEOUT))) {
			IXGBE_WRITE_REG(hw, IXGBE_EIMS, IXGBE_EIMC_LSC);
			adapter->flags &= ~IXGBE_FLAG_NEED_LINK_UPDATE;
		}
		adapter->link_up = link_up;
		adapter->link_speed = link_speed;
	}

	if (link_up) {
		if (!netif_carrier_ok(netdev)) {
			u32 frctl = IXGBE_READ_REG(hw, IXGBE_FCTRL);
			u32 rmcs = IXGBE_READ_REG(hw, IXGBE_RMCS);
#define FLOW_RX (frctl & IXGBE_FCTRL_RFCE)
#define FLOW_TX (rmcs & IXGBE_RMCS_TFCE_802_3X)
			DPRINTK(LINK, ERR, "NIC Link is Up %s, "
			        "Flow Control: %s\n",
			        (link_speed == IXGBE_LINK_SPEED_10GB_FULL ?
			         "10 Gbps" :
			         (link_speed == IXGBE_LINK_SPEED_1GB_FULL ?
			          "1 Gbps" : "unknown speed")),
			        ((FLOW_RX && FLOW_TX) ? "RX/TX" :
			         (FLOW_RX ? "RX" :
			         (FLOW_TX ? "TX" : "None" ))));

			netif_carrier_on(netdev);
#ifdef CONFIG_NETDEVICES_MULTIQUEUE
			for (i = 0; i < adapter->num_tx_queues; i++)
				netif_wake_subqueue(netdev, i);
#else
			netif_wake_queue(netdev);
#endif
		} else {
			/* Force detection of hung controller */
			adapter->detect_tx_hung = true;
		}
	} else {
		if (netif_carrier_ok(netdev)) {
			DPRINTK(LINK, ERR, "NIC Link is Down\n");
			netif_carrier_off(netdev);
			netif_stop_queue(netdev);

#if defined(__VMKLNX__)
			for (i = 0; i < adapter->num_tx_queues; i++) {
				tx_ring = &adapter->tx_ring[i];
				if (tx_ring->next_to_use != tx_ring->next_to_clean) {
					some_tx_pending = 1;
					break;
				}
			}

			if (some_tx_pending) {
				/* We've lost link, so the controller stops DMA,
				 * but we've got queued Tx work that's never going
				 * to get done, so reset controller to flush Tx.
				 * (Do the reset outside of interrupt context).
				 * PR 384971 */
				schedule_work(&adapter->reset_task);
			}
#endif /* defined(__VMKLNX__) */
		}
	}

	ixgbe_update_stats(adapter);
	adapter->flags &= ~IXGBE_FLAG_IN_WATCHDOG_TASK;
}

static int ixgbe_tso(struct ixgbe_adapter *adapter, struct ixgbe_ring *tx_ring,
                     struct sk_buff *skb, u32 tx_flags, u8 *hdr_len)
{
#ifdef NETIF_F_TSO
	struct ixgbe_adv_tx_context_desc *context_desc;
	unsigned int i;
	int err;
	struct ixgbe_tx_buffer *tx_buffer_info;
	u32 vlan_macip_lens = 0, type_tucmd_mlhl;
	u32 mss_l4len_idx, l4len;

	if (skb_is_gso(skb)) {
		if (skb_header_cloned(skb)) {
			err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
			if (err)
				return err;
		}
		l4len = tcp_hdrlen(skb);
		*hdr_len += l4len;

		if (skb->protocol == htons(ETH_P_IP)) {
			struct iphdr *iph = ip_hdr(skb);
			iph->tot_len = 0;
			iph->check = 0;
			tcp_hdr(skb)->check = ~csum_tcpudp_magic(iph->saddr,
			                                         iph->daddr, 0,
			                                         IPPROTO_TCP,
			                                         0);
			adapter->hw_tso_ctxt++;
#ifdef NETIF_F_TSO6
		} else if (skb_shinfo(skb)->gso_type == SKB_GSO_TCPV6) {
			struct ipv6hdr *ipv6h = ipv6_hdr(skb);
			ipv6h->payload_len = 0;
			tcp_hdr(skb)->check = ~csum_ipv6_magic(&ipv6h->saddr,
			                                       &ipv6h->daddr,
			                                       0, IPPROTO_TCP,
			                                       0);
			adapter->hw_tso6_ctxt++;
#endif
		}

		i = tx_ring->next_to_use;

		tx_buffer_info = &tx_ring->tx_buffer_info[i];
		context_desc = IXGBE_TX_CTXTDESC_ADV(*tx_ring, i);

		/* VLAN MACLEN IPLEN */
		if (tx_flags & IXGBE_TX_FLAGS_VLAN)
			vlan_macip_lens |=
			                  (tx_flags & IXGBE_TX_FLAGS_VLAN_MASK);
		vlan_macip_lens |= ((skb_network_offset(skb)) <<
		                    IXGBE_ADVTXD_MACLEN_SHIFT);
		*hdr_len += skb_network_offset(skb);
		vlan_macip_lens |=
		          (skb_transport_header(skb) - skb_network_header(skb));
		*hdr_len +=
		          (skb_transport_header(skb) - skb_network_header(skb));
		context_desc->vlan_macip_lens = cpu_to_le32(vlan_macip_lens);
		context_desc->seqnum_seed = 0;

		/* ADV DTYP TUCMD MKRLOC/ISCSIHEDLEN */
		type_tucmd_mlhl = (IXGBE_TXD_CMD_DEXT |
				    IXGBE_ADVTXD_DTYP_CTXT);

		if (skb->protocol == htons(ETH_P_IP))
			type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_IPV4;
		type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_L4T_TCP;
		context_desc->type_tucmd_mlhl = cpu_to_le32(type_tucmd_mlhl);

		/* MSS L4LEN IDX */
		mss_l4len_idx =
		          (skb_shinfo(skb)->gso_size << IXGBE_ADVTXD_MSS_SHIFT);
		mss_l4len_idx |= (l4len << IXGBE_ADVTXD_L4LEN_SHIFT);
		context_desc->mss_l4len_idx = cpu_to_le32(mss_l4len_idx);

		tx_buffer_info->time_stamp = jiffies;
		tx_buffer_info->next_to_watch = i;

		i++;
		if (i == tx_ring->count)
			i = 0;
		tx_ring->next_to_use = i;

		return true;
	}
#endif

	return false;
}

static bool ixgbe_tx_csum(struct ixgbe_adapter *adapter,
                          struct ixgbe_ring *tx_ring,
                          struct sk_buff *skb, u32 tx_flags)
{
	struct ixgbe_adv_tx_context_desc *context_desc;
	unsigned int i;
	struct ixgbe_tx_buffer *tx_buffer_info;
	u32 vlan_macip_lens = 0, type_tucmd_mlhl = 0;

	if (skb->ip_summed == CHECKSUM_PARTIAL ||
	    (tx_flags & IXGBE_TX_FLAGS_VLAN)) {
		i = tx_ring->next_to_use;
		tx_buffer_info = &tx_ring->tx_buffer_info[i];
		context_desc = IXGBE_TX_CTXTDESC_ADV(*tx_ring, i);

		if (tx_flags & IXGBE_TX_FLAGS_VLAN)
			vlan_macip_lens |= (tx_flags &
			                    IXGBE_TX_FLAGS_VLAN_MASK);
		vlan_macip_lens |= (skb_network_offset(skb) <<
		                    IXGBE_ADVTXD_MACLEN_SHIFT);
		if (skb->ip_summed == CHECKSUM_PARTIAL)
			vlan_macip_lens |= (skb_transport_header(skb) -
			                    skb_network_header(skb));

		context_desc->vlan_macip_lens = cpu_to_le32(vlan_macip_lens);
		context_desc->seqnum_seed = 0;

		type_tucmd_mlhl |= (IXGBE_TXD_CMD_DEXT |
		                    IXGBE_ADVTXD_DTYP_CTXT);

		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			switch (skb->protocol) {
			case __constant_htons(ETH_P_IP):
				type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_IPV4;
				if (ip_hdr(skb)->protocol == IPPROTO_TCP)
					type_tucmd_mlhl |=
					    IXGBE_ADVTXD_TUCMD_L4T_TCP;
				break;
			case __constant_htons(ETH_P_IPV6):
				/* XXX what about other V6 headers?? */
				if (ipv6_hdr(skb)->nexthdr == IPPROTO_TCP)
					type_tucmd_mlhl |= IXGBE_ADVTXD_TUCMD_L4T_TCP;
				break;
			default:
				if (unlikely(net_ratelimit())) {
					DPRINTK(PROBE, WARNING,
					 "partial checksum but proto=%x!\n",
					 skb->protocol);
				}
				break;
			}
		}

		context_desc->type_tucmd_mlhl = cpu_to_le32(type_tucmd_mlhl);
		context_desc->mss_l4len_idx = 0;

		tx_buffer_info->time_stamp = jiffies;
		tx_buffer_info->next_to_watch = i;

		adapter->hw_csum_tx_good++;
		i++;
		if (i == tx_ring->count)
			i = 0;
		tx_ring->next_to_use = i;

		return true;
	}

	return false;
}

static int ixgbe_tx_map(struct ixgbe_adapter *adapter,
                        struct ixgbe_ring *tx_ring, struct sk_buff *skb,
                        unsigned int first)
{
	struct ixgbe_tx_buffer *tx_buffer_info;
	unsigned int len = skb->len;
	unsigned int offset = 0, size, count = 0, i;
#ifdef MAX_SKB_FRAGS
	unsigned int nr_frags = skb_shinfo(skb)->nr_frags;
	unsigned int f;

	len -= skb->data_len;
#endif

	i = tx_ring->next_to_use;

	while (len) {
		tx_buffer_info = &tx_ring->tx_buffer_info[i];
		size = min(len, (unsigned int)IXGBE_MAX_DATA_PER_TXD);

		tx_buffer_info->length = size;
		tx_buffer_info->dma = pci_map_single(adapter->pdev,
		                                     skb->data + offset, size,
		                                     PCI_DMA_TODEVICE);
		tx_buffer_info->time_stamp = jiffies;
		tx_buffer_info->next_to_watch = i;

		len -= size;
		offset += size;
		count++;
		i++;
		if (i == tx_ring->count)
			i = 0;
	}

#ifdef MAX_SKB_FRAGS
	for (f = 0; f < nr_frags; f++) {
		struct skb_frag_struct *frag;

		frag = &skb_shinfo(skb)->frags[f];
		len = frag->size;
		offset = frag->page_offset;

		while (len) {
			tx_buffer_info = &tx_ring->tx_buffer_info[i];
			size = min(len, (unsigned int)IXGBE_MAX_DATA_PER_TXD);

			tx_buffer_info->length = size;
			tx_buffer_info->dma = pci_map_page(adapter->pdev,
			                                   frag->page, offset,
			                                   size,
			                                   PCI_DMA_TODEVICE);
			tx_buffer_info->time_stamp = jiffies;
			tx_buffer_info->next_to_watch = i;

			len -= size;
			offset += size;
			count++;
			i++;
			if (i == tx_ring->count)
				i = 0;
		}
	}
#endif

	if (i == 0)
		i = tx_ring->count - 1;
	else
		i = i - 1;
	tx_ring->tx_buffer_info[i].skb = skb;
	tx_ring->tx_buffer_info[first].next_to_watch = i;

	return count;
}

static void ixgbe_tx_queue(struct ixgbe_adapter *adapter,
                           struct ixgbe_ring *tx_ring, int tx_flags,
                           int count, u32 paylen, u8 hdr_len)
{
	union ixgbe_adv_tx_desc *tx_desc = NULL;
	struct ixgbe_tx_buffer *tx_buffer_info;
	u32 olinfo_status = 0, cmd_type_len = 0;
	unsigned int i;

	u32 txd_cmd = IXGBE_TXD_CMD_EOP | IXGBE_TXD_CMD_RS | IXGBE_TXD_CMD_IFCS;

	cmd_type_len |= IXGBE_ADVTXD_DTYP_DATA;

	cmd_type_len |= IXGBE_ADVTXD_DCMD_IFCS | IXGBE_ADVTXD_DCMD_DEXT;

	if (tx_flags & IXGBE_TX_FLAGS_VLAN)
		cmd_type_len |= IXGBE_ADVTXD_DCMD_VLE;

	if (tx_flags & IXGBE_TX_FLAGS_TSO) {
		cmd_type_len |= IXGBE_ADVTXD_DCMD_TSE;

		olinfo_status |= IXGBE_TXD_POPTS_TXSM <<
		                 IXGBE_ADVTXD_POPTS_SHIFT;

		if (tx_flags & IXGBE_TX_FLAGS_IPV4)
			olinfo_status |= IXGBE_TXD_POPTS_IXSM <<
			                 IXGBE_ADVTXD_POPTS_SHIFT;

	} else if (tx_flags & IXGBE_TX_FLAGS_CSUM)
		olinfo_status |= IXGBE_TXD_POPTS_TXSM <<
		                 IXGBE_ADVTXD_POPTS_SHIFT;

	olinfo_status |= ((paylen - hdr_len) << IXGBE_ADVTXD_PAYLEN_SHIFT);

	i = tx_ring->next_to_use;
	while (count--) {
		tx_buffer_info = &tx_ring->tx_buffer_info[i];
		tx_desc = IXGBE_TX_DESC_ADV(*tx_ring, i);
		tx_desc->read.buffer_addr = cpu_to_le64(tx_buffer_info->dma);
		tx_desc->read.cmd_type_len =
			cpu_to_le32(cmd_type_len | tx_buffer_info->length);
		tx_desc->read.olinfo_status = cpu_to_le32(olinfo_status);
		i++;
		if (i == tx_ring->count)
			i = 0;
	}

	tx_desc->read.cmd_type_len |= cpu_to_le32(txd_cmd);

	/*
	 * Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64).
	 */
	wmb();

	tx_ring->next_to_use = i;
	writel(i, adapter->hw.hw_addr + tx_ring->tail);
}

static int __ixgbe_maybe_stop_tx(struct net_device *netdev,
                                 struct ixgbe_ring *tx_ring, int size)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

#ifdef CONFIG_NETDEVICES_MULTIQUEUE
	netif_stop_subqueue(netdev, tx_ring->queue_index);
#else
	netif_stop_queue(netdev);
#endif
	/* Herbert's original patch had:
	 *  smp_mb__after_netif_stop_queue();
	 * but since that doesn't exist yet, just open code it. */
	smp_mb();

	/* We need to check again in a case another CPU has just
	 * made room available. */
	if (likely(IXGBE_DESC_UNUSED(tx_ring) < size))
		return -EBUSY;

	/* A reprieve! - use start_queue because it doesn't call schedule */
#ifdef CONFIG_NETDEVICES_MULTIQUEUE
	netif_wake_subqueue(netdev, tx_ring->queue_index);
#else
	netif_wake_queue(netdev);
#endif
	++adapter->restart_queue;
	return 0;
}

static int ixgbe_maybe_stop_tx(struct net_device *netdev,
                               struct ixgbe_ring *tx_ring, int size)
{
	if (likely(IXGBE_DESC_UNUSED(tx_ring) >= size))
		return 0;
	return __ixgbe_maybe_stop_tx(netdev, tx_ring, size);
}


static inline int ixgbe_xmit_frame_common(struct sk_buff *skb, struct net_device *netdev,
				   struct ixgbe_adapter *adapter, int r_idx)
{
	struct ixgbe_ring *tx_ring;
	unsigned int first;
	unsigned int tx_flags = 0;
	u8 hdr_len = 0;
	int tso;
	int count = 0;

#ifdef MAX_SKB_FRAGS
	unsigned int f;
#endif
	tx_ring = &adapter->tx_ring[r_idx];


#ifdef NETIF_F_HW_VLAN_TX
	if (adapter->vlgrp && vlan_tx_tag_present(skb)) {
		tx_flags |= vlan_tx_tag_get(skb);
		tx_flags <<= IXGBE_TX_FLAGS_VLAN_SHIFT;
		tx_flags |= IXGBE_TX_FLAGS_VLAN;
	}
#endif
	/* three things can cause us to need a context descriptor */
	if (skb_is_gso(skb) ||
	    (skb->ip_summed == CHECKSUM_PARTIAL) ||
	    (tx_flags & IXGBE_TX_FLAGS_VLAN))
		count++;

	count += TXD_USE_COUNT(skb_headlen(skb));
#ifdef MAX_SKB_FRAGS
	for (f = 0; f < skb_shinfo(skb)->nr_frags; f++)
		count += TXD_USE_COUNT(skb_shinfo(skb)->frags[f].size);
#endif

	if (ixgbe_maybe_stop_tx(netdev, tx_ring, count)) {
		adapter->tx_busy++;
		return NETDEV_TX_BUSY;
	}

	if (skb->protocol == htons(ETH_P_IP))
		tx_flags |= IXGBE_TX_FLAGS_IPV4;
	first = tx_ring->next_to_use;
	tso = ixgbe_tso(adapter, tx_ring, skb, tx_flags, &hdr_len);
	if (tso < 0) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (tso)
		tx_flags |= IXGBE_TX_FLAGS_TSO;
	else if (ixgbe_tx_csum(adapter, tx_ring, skb, tx_flags) &&
		 skb->ip_summed == CHECKSUM_PARTIAL)
		tx_flags |= IXGBE_TX_FLAGS_CSUM;

	ixgbe_tx_queue(adapter, tx_ring, tx_flags,
	               ixgbe_tx_map(adapter, tx_ring, skb, first),
	               skb->len, hdr_len);

	netdev->trans_start = jiffies;

	ixgbe_maybe_stop_tx(netdev, tx_ring, DESC_NEEDED);

	return NETDEV_TX_OK;
}

static int ixgbe_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	int r_idx = 0;

#ifdef CONFIG_NETDEVICES_MULTIQUEUE
#if defined(__VMKLNX__)
       /*
        * The Intel version has a bug for the case when num_tx_queues is not power of 2.
        * Multiple queues will be mapped to one tx_ring, which causes race condition
        * in accessing same tx_ring from multiple Tx queues.
	*/
        VMK_ASSERT(skb->queue_mapping < adapter->num_tx_queues);
        r_idx = skb->queue_mapping;
#else        
	r_idx = (adapter->num_tx_queues - 1) & skb->queue_mapping;
#endif        
#endif
	return ixgbe_xmit_frame_common(skb, netdev, adapter, r_idx);
}

/**
 * ixgbe_get_stats - Get System Network Statistics
 * @netdev: network interface device structure
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the timer callback.
 **/
static struct net_device_stats *ixgbe_get_stats(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	/* only return the current stats */
	return &adapter->net_stats;
}

/**
 * ixgbe_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/
static int ixgbe_set_mac(struct net_device *netdev, void *p)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(adapter->hw.mac.addr, addr->sa_data, netdev->addr_len);

	ixgbe_set_rar(&adapter->hw, 0, adapter->hw.mac.addr, IXGBE_RAH_AV);

	return 0;
}

#ifdef ETHTOOL_OPS_COMPAT
/**
 * ixgbe_ioctl -
 * @netdev:
 * @ifreq:
 * @cmd:
 **/
static int ixgbe_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCETHTOOL:
		return ethtool_ioctl(ifr);
	default:
		return -EOPNOTSUPP;
	}
}

#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void ixgbe_netpoll(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	/* XXX is disable_irq the right thing to do here instead? */
	ixgbe_irq_disable(adapter);
	adapter->flags |= IXGBE_FLAG_IN_NETPOLL;
	ixgbe_intr(adapter->pdev->irq, netdev);
	adapter->flags &= ~IXGBE_FLAG_IN_NETPOLL;
	ixgbe_irq_enable(adapter);
}

#endif

/**
 * ixgbe_link_config - set up initial link with default speed and duplex
 * @hw: pointer to private hardware struct
 *
 * Returns 0 on success, negative on failure
 **/
static int ixgbe_link_config(struct ixgbe_hw *hw)
{
	u32 autoneg = IXGBE_LINK_SPEED_10GB_FULL;

	/* must always autoneg for both 1G and 10G link */
	hw->mac.autoneg = true;

	if ((hw->mac.type == ixgbe_mac_82598EB) &&
	    (hw->phy.media_type == ixgbe_media_type_copper))
		autoneg = IXGBE_LINK_SPEED_82598_AUTONEG;
	return ixgbe_setup_link_speed(hw, autoneg, true, true);
}

#ifdef CONFIG_IXGBE_NAPI
/**
 * ixgbe_napi_add_all - prep napi structs for use
 * @adapter: private struct
 * helper function to napi_add each possible q_vector->napi
 */
static void ixgbe_napi_add_all(struct ixgbe_adapter *adapter)
{
	int i, q_vectors = adapter->num_msix_vectors - NON_Q_VECTORS;
	int (*poll)(struct napi_struct *, int);

	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
		poll = &ixgbe_clean_rxonly;

#if defined(__VMKLNX__)
	/* The Intel version has a bug for the case of a vector is sharing
	   both Rx and Tx queues. The bug will lead to PSOD on driver load
	   in VMDQ case.
	 */
		/* Re adjust the q_vectors value for the case of
		 * q_vectors is <= num_rx_queues.
		 */ 
		q_vectors = min(adapter->num_rx_queues, q_vectors);
#else /* !defined(__VMKLNX__) */
		/* only enable as many vectors as we have rx queues
		 * which works because rx vectors are initialized first */
		q_vectors -= adapter->num_tx_queues;
#endif /* defined(__VMKLNX__) */
	} else {
		poll = &ixgbe_poll;
		/* only one q_vector for legacy modes */
		q_vectors = 1;
	}

	for (i = 0; i < q_vectors; i++) {
		struct ixgbe_q_vector *q_vector = &adapter->q_vector[i];
		netif_napi_add(adapter->netdev, &q_vector->napi,
		               (*poll), 64);
	}
}

#endif
/**
 * ixgbe_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in ixgbe_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * ixgbe_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
static int __devinit ixgbe_probe(struct pci_dev *pdev,
                                 const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct ixgbe_adapter *adapter = NULL;
	struct ixgbe_hw *hw = NULL;
	static int cards_found = 0;
	int i, err, pci_using_dac;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	if (!pci_set_dma_mask(pdev, DMA_64BIT_MASK) &&
	    !pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK)) {
		pci_using_dac = 1;
	} else {
		err = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (err) {
			err = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
			if (err) {
				IXGBE_ERR("No usable DMA configuration, "
				          "aborting\n");
				goto err_dma;
			}
		}
		pci_using_dac = 0;
	}

	err = pci_request_regions(pdev, ixgbe_driver_name);
	if (err) {
		IXGBE_ERR("pci_request_regions failed 0x%x\n", err);
		goto err_pci_reg;
	}

	pci_set_master(pdev);

#ifdef CONFIG_NETDEVICES_MULTIQUEUE
	netdev = alloc_etherdev_mq(sizeof(struct ixgbe_adapter), MAX_TX_QUEUES);
#else
	netdev = alloc_etherdev(sizeof(struct ixgbe_adapter));
#endif
	if (!netdev) {
		err = -ENOMEM;
		goto err_alloc_etherdev;
	}

	SET_MODULE_OWNER(netdev);
	SET_NETDEV_DEV(netdev, &pdev->dev);

	pci_set_drvdata(pdev, netdev);
	adapter = netdev_priv(netdev);

	adapter->netdev = netdev;
	adapter->pdev = pdev;
	hw = &adapter->hw;
	hw->back = adapter;
	adapter->msg_enable = (1 << DEFAULT_DEBUG_LEVEL_SHIFT) - 1;

	hw->hw_addr = ioremap(pci_resource_start(pdev, 0),
	                      pci_resource_len(pdev, 0));
	if (!hw->hw_addr) {
		err = -EIO;
		goto err_ioremap;
	}

	for (i = 1; i <= 5; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
	}

	netdev->open = &ixgbe_open;
	netdev->stop = &ixgbe_close;
	netdev->hard_start_xmit = &ixgbe_xmit_frame;
	netdev->get_stats = &ixgbe_get_stats;
	netdev->set_multicast_list = &ixgbe_set_rx_mode;
	netdev->set_mac_address = &ixgbe_set_mac;
	netdev->change_mtu = &ixgbe_change_mtu;
#ifdef ETHTOOL_OPS_COMPAT
	netdev->do_ioctl = &ixgbe_ioctl;
#endif
	ixgbe_set_ethtool_ops(netdev);
#ifdef HAVE_TX_TIMEOUT
	netdev->tx_timeout = &ixgbe_tx_timeout;
	netdev->watchdog_timeo = 5 * HZ;
#endif
#ifdef NETIF_F_HW_VLAN_TX
	netdev->vlan_rx_register = ixgbe_vlan_rx_register;
	netdev->vlan_rx_add_vid = ixgbe_vlan_rx_add_vid;
	netdev->vlan_rx_kill_vid = ixgbe_vlan_rx_kill_vid;
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
	netdev->poll_controller = ixgbe_netpoll;
#endif
	strcpy(netdev->name, pci_name(pdev));

	adapter->bd_number = cards_found;

#ifdef IXGBE_TCP_TIMER
	adapter->msix_addr = ioremap(pci_resource_start(pdev, 3),
	                             pci_resource_len(pdev, 3));
	if (!adapter->msix_addr) {
		err = -EIO;
		printk("Error in ioremap of BAR3\n");
		goto err_map_msix;
	}

#endif
	/* setup the private structure */
	err = ixgbe_sw_init(adapter);
	if (err)
		goto err_sw_init;

	/* reset_hw fills in the perm_addr as well */
	err = ixgbe_reset_hw(hw);
	if (err) {
		DPRINTK(PROBE, ERR, "HW Init failed: %d\n", err);
		goto err_sw_init;
	}

	/* check_options must be called before setup_link_speed to set up
	 * hw->fc completely
	 */
	ixgbe_check_options(adapter);

#ifdef MAX_SKB_FRAGS
#ifdef NETIF_F_HW_VLAN_TX
	netdev->features = NETIF_F_SG |
			   NETIF_F_HW_CSUM |
			   NETIF_F_HW_VLAN_TX |
			   NETIF_F_HW_VLAN_RX |
			   NETIF_F_HW_VLAN_FILTER;
#else
	netdev->features = NETIF_F_SG | NETIF_F_HW_CSUM;
#endif

#ifdef NETIF_F_TSO
	netdev->features |= NETIF_F_TSO;
#ifdef NETIF_F_TSO6
	netdev->features |= NETIF_F_TSO6;
#endif
#endif

	if (pci_using_dac)
		netdev->features |= NETIF_F_HIGHDMA;

#endif
#if !defined(__VMKLNX__)
#ifdef CONFIG_NETDEVICES_MULTIQUEUE
	netdev->features |= NETIF_F_MULTI_QUEUE;
#endif
#endif

	/* make sure the EEPROM is good */
	err = ixgbe_validate_eeprom_checksum(hw, NULL);
	if (err < 0) {
		DPRINTK(PROBE, ERR, "The EEPROM Checksum Is Not Valid\n");
		err = -EIO;
		goto err_sw_init;
	}

	memcpy(netdev->dev_addr, hw->mac.perm_addr, netdev->addr_len);
#ifdef ETHTOOL_GPERMADDR
	memcpy(netdev->perm_addr, hw->mac.perm_addr, netdev->addr_len);

	if (ixgbe_validate_mac_addr(netdev->perm_addr)) {
		DPRINTK(PROBE, ERR, "invalid MAC address\n");
		err = -EIO;
		goto err_sw_init;
	}
#else
	if (ixgbe_validate_mac_addr(netdev->dev_addr)) {
		DPRINTK(PROBE, ERR, "invalid MAC address\n");
		err = -EIO;
		goto err_sw_init;
	}
#endif

	ixgbe_get_bus_info(hw);

	init_timer(&adapter->watchdog_timer);
	adapter->watchdog_timer.function = &ixgbe_watchdog;
	adapter->watchdog_timer.data = (unsigned long) adapter;

	INIT_WORK(&adapter->reset_task, ixgbe_reset_task);
	INIT_WORK(&adapter->watchdog_task, ixgbe_watchdog_task);

	err = ixgbe_init_interrupt_scheme(adapter);
	if (err)
		goto err_sw_init;

	/* print bus type/speed/width info */
	DPRINTK(PROBE, ERR, "(PCI Express:%s:%s) ",
		((hw->bus.speed == ixgbe_bus_speed_2500) ? "2.5Gb/s":"Unknown"),
		 (hw->bus.width == ixgbe_bus_width_pcie_x8) ? "Width x8" :
		 (hw->bus.width == ixgbe_bus_width_pcie_x4) ? "Width x4" :
		 (hw->bus.width == ixgbe_bus_width_pcie_x1) ? "Width x1" :
		 ("Unknown"));

	/* print the MAC address */
	for (i = 0; i < 6; i++)
		printk("%2.2x%c", netdev->dev_addr[i], i == 5 ? '\n' : ':');
	
	/* reset the hardware with the new settings */
	ixgbe_start_hw(&adapter->hw);

	/* link_config depends on ixgbe_start_hw being called at least once */
	err = ixgbe_link_config(hw);
	if (err) {
		DPRINTK(PROBE, ERR, "setup_link_speed FAILED %d\n", err);
		goto err_register;
	}

	netif_carrier_off(netdev);
	netif_stop_queue(netdev);
#ifdef CONFIG_NETDEVICES_MULTIQUEUE
	for (i = 0; i < adapter->num_tx_queues; i++)
		netif_stop_subqueue(netdev, i);
#endif

#if defined(__VMKLNX__) && defined(__VMKNETDDI_QUEUEOPS__)
	if (adapter->flags & IXGBE_FLAG_VMDQ_ENABLED) {
		DPRINTK(PROBE, INFO, "Registering for VMware NetQueue Ops\n");
		VMKNETDDI_REGISTER_QUEUEOPS(netdev, ixgbe_netqueue_ops);
	}
#endif /* defined(__VMKLNX__) */

#ifdef CONFIG_IXGBE_NAPI
	ixgbe_napi_add_all(adapter);

#endif
#if !defined(__VMKLNX__)
	strcpy(netdev->name, "eth%d");
#endif /* !defined(__VMKLNX__) */
	err = register_netdev(netdev);
	if (err)
		goto err_register;

#ifdef IXGBE_DCA
	if (adapter->flags & IXGBE_FLAG_DCA_CAPABLE) {
#if defined(__VMKLNX__)
	        if (vmklnx_dca_add_requester(&pdev->dev) == IXGBE_SUCCESS) {
#else /* !defined(__VMKLNX__) */
		if (dca_add_requester(&pdev->dev) == IXGBE_SUCCESS) {
#endif /* defined(__VMKLNX__) */
			adapter->flags |= IXGBE_FLAG_DCA_ENABLED;
			/* always use CB2 mode, difference is masked
			 * in the CB driver */
			IXGBE_WRITE_REG(hw, IXGBE_DCA_CTRL, 2);
			ixgbe_setup_dca(adapter);
			DPRINTK(PROBE, ERR, "Intel end-point DCA enabled\n");
		}
#if defined(__VMKLNX__)
		else {
		        adapter->flags &= ~IXGBE_FLAG_DCA_ENABLED;    
			DPRINTK(PROBE, ERR, "Intel end-point DCA disabled\n");
		}
		       
#endif /* defined(__VMKLNX__) */
		
	}
#endif

	DPRINTK(PROBE, ERR, "Intel(R) 10 Gigabit Network Connection\n");
	cards_found++;
	return 0;

err_register:
	ixgbe_release_hw_control(adapter);
err_sw_init:
	ixgbe_reset_interrupt_capability(adapter);
#ifdef IXGBE_TCP_TIMER
	iounmap(adapter->msix_addr);
err_map_msix:
#endif
	iounmap(hw->hw_addr);
err_ioremap:
	free_netdev(netdev);
err_alloc_etherdev:
	pci_release_regions(pdev);
err_pci_reg:
err_dma:
	pci_disable_device(pdev);
	return err;
}

/**
 * ixgbe_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * ixgbe_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void __devexit ixgbe_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	set_bit(__IXGBE_DOWN, &adapter->state);
	del_timer_sync(&adapter->watchdog_timer);

	flush_scheduled_work();

#ifdef IXGBE_DCA
	if (adapter->flags & IXGBE_FLAG_DCA_ENABLED) {
		adapter->flags &= ~IXGBE_FLAG_DCA_ENABLED;
#if defined(__VMKLNX__)
		vmklnx_dca_remove_requester(&pdev->dev);
#else /* !defined(__VMKLNX__) */
		dca_remove_requester(&pdev->dev);
#endif /* defined(__VMKLNX__) */
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_DCA_CTRL, 1);
	}

#endif
	unregister_netdev(netdev);

	ixgbe_reset_interrupt_capability(adapter);

	ixgbe_release_hw_control(adapter);

#ifdef IXGBE_TCP_TIMER
	iounmap(adapter->msix_addr);
#endif
	iounmap(adapter->hw.hw_addr);
	pci_release_regions(pdev);

	DPRINTK(PROBE, ERR, "complete\n");
	kfree(adapter->tx_ring);
	kfree(adapter->rx_ring);

	free_netdev(netdev);

	pci_disable_device(pdev);
}

u16 ixgbe_read_pci_cfg_word(struct ixgbe_hw *hw, u32 reg)
{
	u16 value;
	struct ixgbe_adapter *adapter = hw->back;

	pci_read_config_word(adapter->pdev, reg, &value);
	return value;
}

#ifdef CONFIG_IXGBE_PCI_ERS
/**
 * ixgbe_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t ixgbe_io_error_detected(struct pci_dev *pdev,
                                                pci_channel_state_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct ixgbe_adapter *adapter = netdev->priv;

	netif_device_detach(netdev);

	if (netif_running(netdev))
		ixgbe_down(adapter);
	pci_disable_device(pdev);

	/* Request a slot reset */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * ixgbe_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot.
 */
static pci_ers_result_t ixgbe_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct ixgbe_adapter *adapter = netdev->priv;

	if (pci_enable_device(pdev)) {
		DPRINTK(PROBE, ERR,
			"Cannot re-enable PCI device after reset.\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}
	pci_set_master(pdev);

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	ixgbe_reset(adapter);

	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * ixgbe_io_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells us that
 * its OK to resume normal operation.
 */
static void ixgbe_io_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct ixgbe_adapter *adapter = netdev->priv;

	if (netif_running(netdev)) {
		if (ixgbe_up(adapter)) {
			DPRINTK(PROBE, ERR, "ixgbe_up failed after reset\n");
			return;
		}
	}

	netif_device_attach(netdev);
}

static struct pci_error_handlers ixgbe_err_handler = {
	.error_detected = ixgbe_io_error_detected,
	.slot_reset = ixgbe_io_slot_reset,
	.resume = ixgbe_io_resume,
};

#endif
static struct pci_driver ixgbe_driver = {
	.name     = ixgbe_driver_name,
	.id_table = ixgbe_pci_tbl,
	.probe    = ixgbe_probe,
	.remove   = __devexit_p(ixgbe_remove),
#ifdef CONFIG_PM
	.suspend  = ixgbe_suspend,
	.resume   = ixgbe_resume,
#endif
#ifndef USE_REBOOT_NOTIFIER
	.shutdown = ixgbe_shutdown,
#endif
#ifdef CONFIG_IXGBE_PCI_ERS
	.err_handler = &ixgbe_err_handler
#endif
};


/**
 * ixgbe_init_module - Driver Registration Routine
 *
 * ixgbe_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 **/
static int __init ixgbe_init_module(void)
{
	printk(KERN_INFO "%s - version %s\n", ixgbe_driver_string,
	       ixgbe_driver_version);

	printk(KERN_INFO "%s\n", ixgbe_copyright);

#ifdef IXGBE_DCA
#if !defined(__VMKLNX__)
	dca_register_notify(&dca_notifier);
#endif /* !defined(__VMKLNX__) */
#endif
	return pci_register_driver(&ixgbe_driver);
}

module_init(ixgbe_init_module);

/**
 * ixgbe_exit_module - Driver Exit Cleanup Routine
 *
 * ixgbe_exit_module is called just before the driver is removed
 * from memory.
 **/
static void __exit ixgbe_exit_module(void)
{
#ifdef IXGBE_DCA
#if !defined(__VMKLNX__)
	dca_unregister_notify(&dca_notifier);
#endif /* !defined(__VMKLNX__) */
#endif
	pci_unregister_driver(&ixgbe_driver);
}

#ifdef IXGBE_DCA
static int ixgbe_notify_dca(struct notifier_block *nb, unsigned long event,
                            void *p)
{
	int ret_val;

	ret_val = driver_for_each_device(&ixgbe_driver.driver, NULL, &event,
	                                 __ixgbe_notify_dca);

	return ret_val ? NOTIFY_BAD : NOTIFY_DONE;
}
#endif /* IXGBE_DCA */

module_exit(ixgbe_exit_module);

#if defined(__VMKLNX__) && defined(__VMKNETDDI_QUEUEOPS__)
int ixgbe_get_num_tx_queues(struct net_device *netdev, int *count)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	*count = max(adapter->num_tx_queues - 1, 0);

	return 0;
}

int ixgbe_get_num_rx_queues(struct net_device *netdev, int *count)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	*count = max(adapter->num_rx_queues - 1, 0);

	return 0;
}

int ixgbe_set_rxqueue_macfilter(struct net_device *netdev, int queue,
          			u8 *mac_addr)
{
	int err = 0;
	u32 rah;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_ring *rxdr = &adapter->rx_ring[queue];

	if ((queue < 0) || (queue > adapter->num_rx_queues)) {
		DPRINTK(PROBE, ERR,
			"Invalid RX Queue %u specified\n", queue);
		return -EADDRNOTAVAIL;
	}

	/* Note: Broadcast address is used to disable the MAC filter*/
	if (!is_valid_ether_addr(mac_addr)) {

		memset(rxdr->mac_addr, 0xFF, NODE_ADDRESS_SIZE);

		/* Clear RAR */
		IXGBE_WRITE_REG(hw, IXGBE_RAL(queue), 0);
		IXGBE_WRITE_FLUSH(hw);
		IXGBE_WRITE_REG(hw, IXGBE_RAH(queue), 0);
		IXGBE_WRITE_FLUSH(hw);

		return -EADDRNOTAVAIL;
	}


	/* Store in ring */
	memcpy(rxdr->mac_addr, mac_addr, NODE_ADDRESS_SIZE);

	err = ixgbe_set_rar(&adapter->hw, queue, rxdr->mac_addr, 1);

	if (!err) {
		/* Set the VIND for the indicated queue's RAR Entry */
		rah = IXGBE_READ_REG(hw, IXGBE_RAH(queue));
		rah &= ~IXGBE_RAH_VIND_MASK;
		rah |= (queue << IXGBE_RAH_VIND_SHIFT);
		IXGBE_WRITE_REG(hw, IXGBE_RAH(queue), rah);
		IXGBE_WRITE_FLUSH(hw);
	}

	return err;
}

int ixgbe_get_rxqueue_macfilter(struct net_device *netdev, int queue,
	       			u8 *mac_addr)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_ring *rx_ring  = &adapter->rx_ring[queue];

	memcpy(mac_addr, rx_ring->mac_addr, NODE_ADDRESS_SIZE);

	return 0;
}

void ixgbe_print_rxq_filter (struct net_device *netdev)
{
	int ret, txq, rxq, q, i;
	uint8_t qmac_addr[NODE_ADDRESS_SIZE];

	/* get que numbers */
	if (0 != (ret = ixgbe_get_num_tx_queues(netdev, &txq))) {
	    printk(KERN_INFO "VMDQ1 fail to get TX queues: %d\n", ret);
	    return;
	}

	if (0 != (ret = ixgbe_get_num_rx_queues(netdev, &rxq))) {
	    printk(KERN_INFO "VMDQ1 fail to get RX queues: %d\n", ret);
	    return;
	}
	printk(KERN_INFO "VMDQ1 TX queues: %d   RX queues: %d\n", txq, rxq);

	/* show each MAC filter */
	for (q = 1; q < rxq; q++) {
	    if (0==(ret = ixgbe_get_rxqueue_macfilter(netdev, q, qmac_addr))) {
		printk(KERN_INFO "rx que %d: ", q);
		for (i = 0; i < NODE_ADDRESS_SIZE; i++)
		    printk(KERN_INFO"%2.2x%c", qmac_addr[i], i == 5 ? '\n' : ':');
	    } else {
		printk(KERN_INFO"VMDQ1 fail to get RX filter que: %d err: %d\n", q, ret);
	    }
	}
	return;
}

static int ixgbe_get_netqueue_features(vmknetddi_queueop_get_features_args_t *args)
{
	args->features = VMKNETDDI_QUEUEOPS_FEATURE_NONE;
	args->features |= VMKNETDDI_QUEUEOPS_FEATURE_RXQUEUES;
	args->features |= VMKNETDDI_QUEUEOPS_FEATURE_TXQUEUES;
	return VMKNETDDI_QUEUEOPS_OK;
}

static int ixgbe_get_queue_count(vmknetddi_queueop_get_queue_count_args_t *args)
{
	struct net_device *netdev = args->netdev;
	int count = 0;

	if (args->type == VMKNETDDI_QUEUEOPS_QUEUE_TYPE_TX) {
		ixgbe_get_num_tx_queues(netdev, &count);
	}
	else if (args->type == VMKNETDDI_QUEUEOPS_QUEUE_TYPE_RX) {
		ixgbe_get_num_rx_queues(netdev, &count);
	}
	else {
		printk("ixgbe_get_queue_count: invalid queue type\n");
		return VMKNETDDI_QUEUEOPS_ERR;
	}
	args->count = (u16)count;

	return VMKNETDDI_QUEUEOPS_OK;
}

static int ixgbe_get_filter_count(vmknetddi_queueop_get_filter_count_args_t *args)
{
	args->count = 1;
	return VMKNETDDI_QUEUEOPS_OK;
}

static int ixgbe_alloc_rx_queue(struct net_device *netdev,
                                vmknetddi_queueops_queueid_t *p_qid,
                                struct napi_struct **napi_p)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if (adapter->n_rx_queues_allocated >= adapter->num_rx_queues) {
		return VMKNETDDI_QUEUEOPS_ERR;
        }
	else {
		int i;
		for (i = 1; i < adapter->num_rx_queues; i++) {
			if (!adapter->rx_ring[i].allocated) {
                                u8 vector_idx;

				adapter->rx_ring[i].allocated = TRUE;
				*p_qid = VMKNETDDI_QUEUEOPS_MK_RX_QUEUEID(i);

                                vector_idx = adapter->rx_ring[i].vector_idx;
                                *napi_p = &adapter->q_vector[vector_idx].napi;

				adapter->n_rx_queues_allocated++;
				return VMKNETDDI_QUEUEOPS_OK;
			}
		}
		return VMKNETDDI_QUEUEOPS_ERR;
	}
}

static int ixgbe_alloc_tx_queue(struct net_device *netdev,
                                vmknetddi_queueops_queueid_t *p_qid,
                                u16 *queue_mapping)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if (adapter->n_tx_queues_allocated >= adapter->num_tx_queues) {
		return VMKNETDDI_QUEUEOPS_ERR;
        }
	else {
		int i;
		for (i = 1; i < adapter->num_tx_queues; i++) {
			if (!adapter->tx_ring[i].allocated) {
				adapter->tx_ring[i].allocated = TRUE;
				*p_qid = VMKNETDDI_QUEUEOPS_MK_TX_QUEUEID(i);
                                *queue_mapping = i;
				adapter->n_tx_queues_allocated++;
				return VMKNETDDI_QUEUEOPS_OK;
			}
		}
		return VMKNETDDI_QUEUEOPS_ERR;
	}

	return VMKNETDDI_QUEUEOPS_ERR;
}

static int ixgbe_alloc_queue(vmknetddi_queueop_alloc_queue_args_t *args)
{
	if (args->type == VMKNETDDI_QUEUEOPS_QUEUE_TYPE_TX) {
                return ixgbe_alloc_tx_queue(args->netdev, &args->queueid,
                                            &args->queue_mapping);
	}
	else if (args->type == VMKNETDDI_QUEUEOPS_QUEUE_TYPE_RX) {
                return ixgbe_alloc_rx_queue(args->netdev, &args->queueid,
                                            &args->napi);
	}
	else {
		printk("ixgbe_alloc_queue: invalid queue type\n");
		return VMKNETDDI_QUEUEOPS_ERR;
	}
}

static int ixgbe_free_rx_queue(struct net_device *netdev,
		     vmknetddi_queueops_queueid_t qid)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	u16 queue = VMKNETDDI_QUEUEOPS_QUEUEID_VAL(qid);

	if (!adapter->rx_ring[queue].allocated) {
		 printk("ixgbe_free_rx_queue: rx queue not allocated\n");
		return VMKNETDDI_QUEUEOPS_ERR;
	}

	adapter->rx_ring[queue].allocated = FALSE;
	adapter->n_rx_queues_allocated--;
	return VMKNETDDI_QUEUEOPS_OK;
}

static int ixgbe_free_tx_queue(struct net_device *netdev,
		     vmknetddi_queueops_queueid_t qid)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	u16 queue = VMKNETDDI_QUEUEOPS_QUEUEID_VAL(qid);

	if (!adapter->tx_ring[queue].allocated) {
		 printk("ixgbe_free_rx_queue: tx queue not allocated\n");
		return VMKNETDDI_QUEUEOPS_ERR;
	}

	adapter->tx_ring[queue].allocated = FALSE;
	adapter->n_tx_queues_allocated--;
	return VMKNETDDI_QUEUEOPS_OK;
}

static int ixgbe_free_queue(vmknetddi_queueop_free_queue_args_t *args)
{
	if (VMKNETDDI_QUEUEOPS_IS_TX_QUEUEID(args->queueid)) {
		return ixgbe_free_tx_queue(args->netdev, args->queueid);
	}
	else if (VMKNETDDI_QUEUEOPS_IS_RX_QUEUEID(args->queueid)) {
		return ixgbe_free_rx_queue(args->netdev, args->queueid);
	}
	else {
		printk("ixgbe_free_queue: invalid queue type\n");
		return VMKNETDDI_QUEUEOPS_ERR;
	}
}

static int ixgbe_get_queue_vector(vmknetddi_queueop_get_queue_vector_args_t *args)
{
	int qid;
	struct net_device *netdev = args->netdev;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	// Assuming RX queue id's are received
	qid = VMKNETDDI_QUEUEOPS_QUEUEID_VAL(args->queueid);
#ifdef CONFIG_PCI_MSI
	args->vector = adapter->msix_entries[qid].vector;
#endif

	return VMKNETDDI_QUEUEOPS_OK;
}

static int ixgbe_get_default_queue(vmknetddi_queueop_get_default_queue_args_t *args)
{
	struct ixgbe_adapter *adapter = netdev_priv(args->netdev);

	if (args->type == VMKNETDDI_QUEUEOPS_QUEUE_TYPE_RX) {
                u8 vector_idx;
                vector_idx = adapter->rx_ring[0].vector_idx;
                args->napi = &adapter->q_vector[vector_idx].napi;
		args->queueid = VMKNETDDI_QUEUEOPS_MK_RX_QUEUEID(0);
		return VMKNETDDI_QUEUEOPS_OK;
	}
	else if (args->type == VMKNETDDI_QUEUEOPS_QUEUE_TYPE_TX) {
                args->queueid = VMKNETDDI_QUEUEOPS_MK_TX_QUEUEID(0);
                args->queue_mapping = 0;
                return VMKNETDDI_QUEUEOPS_OK;
	}
	else {
		return VMKNETDDI_QUEUEOPS_ERR;
	}
}

static int ixgbe_apply_rx_filter(vmknetddi_queueop_apply_rx_filter_args_t *args)
{
	int rval;
	u8 *macaddr;
	u16 queue = VMKNETDDI_QUEUEOPS_QUEUEID_VAL(args->queueid);
	struct ixgbe_adapter *adapter = netdev_priv(args->netdev);

	if (!VMKNETDDI_QUEUEOPS_IS_RX_QUEUEID(args->queueid)) {
		printk("ixgbe_apply_rx_filter: not an rx queue 0x%x\n",
			args->queueid);
		return VMKNETDDI_QUEUEOPS_ERR;
	}

	if (vmknetddi_queueops_get_filter_class(&args->filter)
					!= VMKNETDDI_QUEUEOPS_FILTER_MACADDR) {
		printk("ixgbe_apply_rx_filter: only mac filters supported\n");
		return VMKNETDDI_QUEUEOPS_ERR;
	}

	if (!adapter->rx_ring[queue].allocated ) {
		return VMKNETDDI_QUEUEOPS_ERR;
	}

	if (adapter->rx_ring[queue].active) {
		return VMKNETDDI_QUEUEOPS_ERR;
	}

	macaddr = vmknetddi_queueops_get_filter_macaddr(&args->filter);

	rval = ixgbe_set_rxqueue_macfilter(args->netdev, queue, macaddr);
	if (rval == 0) {
		adapter->rx_ring[queue].active = TRUE;
		/* We only support one filter per queue - hard code
		 * filter id to zero index
		 */
		args->filterid = VMKNETDDI_QUEUEOPS_MK_FILTERID(0);
		return VMKNETDDI_QUEUEOPS_OK;
	}
	else {
		printk("ixgbe_apply_rx_filter: ixgbe_set_rxqueue_macfilter failed\n");
		return VMKNETDDI_QUEUEOPS_ERR;
	}
}

static int ixgbe_remove_rx_filter(vmknetddi_queueop_remove_rx_filter_args_t *args)
{
	int rval;
	u16 cidx = VMKNETDDI_QUEUEOPS_QUEUEID_VAL(args->queueid);
	u16 fidx = VMKNETDDI_QUEUEOPS_FILTERID_VAL(args->filterid);
	struct ixgbe_adapter *adapter = netdev_priv(args->netdev);
	u8 macaddr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	/* This will return an error because broadcast is not a valid
	 * Ethernet address, so ignore and carry on
	 */
	rval = ixgbe_set_rxqueue_macfilter(args->netdev, cidx, macaddr);

	adapter->rx_ring[cidx].active = FALSE;
	return VMKNETDDI_QUEUEOPS_OK;
}

static int ixgbe_get_queue_stats(vmknetddi_queueop_get_stats_args_t *args)
{
	return VMKNETDDI_QUEUEOPS_ERR;
}

static int ixgbe_get_netqueue_version(vmknetddi_queueop_get_version_args_t *args)
{
	return vmknetddi_queueops_version(args);
}

static int ixgbe_netqueue_ops(vmknetddi_queueops_op_t op, void *args)
{

	switch (op) {
		case VMKNETDDI_QUEUEOPS_OP_GET_VERSION:
                     
		return ixgbe_get_netqueue_version(
			(vmknetddi_queueop_get_version_args_t *)args);
		break;

		case VMKNETDDI_QUEUEOPS_OP_GET_FEATURES:
		return ixgbe_get_netqueue_features(
			(vmknetddi_queueop_get_features_args_t *)args);
		break;

		case VMKNETDDI_QUEUEOPS_OP_GET_QUEUE_COUNT:
		return ixgbe_get_queue_count(
			(vmknetddi_queueop_get_queue_count_args_t *)args);
		break;

		case VMKNETDDI_QUEUEOPS_OP_GET_FILTER_COUNT:
		return ixgbe_get_filter_count(
			(vmknetddi_queueop_get_filter_count_args_t *)args);
		break;

		case VMKNETDDI_QUEUEOPS_OP_ALLOC_QUEUE:
		return ixgbe_alloc_queue(
			(vmknetddi_queueop_alloc_queue_args_t *)args);
		break;

		case VMKNETDDI_QUEUEOPS_OP_FREE_QUEUE:
		return ixgbe_free_queue(
			(vmknetddi_queueop_free_queue_args_t *)args);
		break;

		case VMKNETDDI_QUEUEOPS_OP_GET_QUEUE_VECTOR:
		return ixgbe_get_queue_vector(
			(vmknetddi_queueop_get_queue_vector_args_t *)args);
		break;

		case VMKNETDDI_QUEUEOPS_OP_GET_DEFAULT_QUEUE:
		return ixgbe_get_default_queue(
			(vmknetddi_queueop_get_default_queue_args_t *)args);
		break;

		case VMKNETDDI_QUEUEOPS_OP_APPLY_RX_FILTER:
		return ixgbe_apply_rx_filter(
			(vmknetddi_queueop_apply_rx_filter_args_t *)args);
		break;

		case VMKNETDDI_QUEUEOPS_OP_REMOVE_RX_FILTER:
		return ixgbe_remove_rx_filter(
			(vmknetddi_queueop_remove_rx_filter_args_t *)args);
		break;

		case VMKNETDDI_QUEUEOPS_OP_GET_STATS:
		return ixgbe_get_queue_stats(
			(vmknetddi_queueop_get_stats_args_t *)args);
		break;

		default:
		printk("Unhandled NETQUEUE OP %d\n", op);
		return VMKNETDDI_QUEUEOPS_ERR;
	}

	return VMKNETDDI_QUEUEOPS_ERR;
}
#endif /* defined(__VMKLNX__) && defined(__VMKNETDDI_QUEUEOPS__) */
/* ixgbe_main.c */
