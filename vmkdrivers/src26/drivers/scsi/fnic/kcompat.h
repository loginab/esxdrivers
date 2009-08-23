/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * [Insert appropriate license here when releasing outside of Cisco]
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#ident "$Id: kcompat.h 21906 2008-12-15 18:45:47Z jre $"

#ifndef _KCOMPAT_H_
#define _KCOMPAT_H_


#include <linux/version.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

/*
 * Kernel backward-compatibility defintions
 */

#ifndef ioread8
#define ioread8 readb
#endif

#ifndef ioread16
#define ioread16 readw
#endif

#ifndef ioread32
#define ioread32 readl
#endif

#ifndef iowrite8
#define iowrite8 writeb
#endif

#ifndef iowrite16
#define iowrite16 writew
#endif

#ifndef iowrite32
#define iowrite32 writel
#endif

#ifndef readq
static inline u64 readq(void __iomem *addr)
{
	return ioread32(addr) | (((u64)ioread32(addr + 4)) << 32);
}
#endif

#ifndef writeq
static inline void writeq(u64 val, void __iomem *addr)
{
	iowrite32((u32)val, addr);
	iowrite32((u32)(val >> 32), addr + 4);
}
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif

#ifndef DMA_40BIT_MASK
#define DMA_40BIT_MASK	0x000000ffffffffffULL
#endif

#ifndef NETIF_F_GSO
#define gso_size tso_size
#endif

#ifndef NETIF_F_TSO6
#define NETIF_F_TSO6 0
#endif

#ifndef NETIF_F_TSO_ECN
#define NETIF_F_TSO_ECN 0
#endif

#ifndef NETIF_F_LRO
#define NETIF_F_LRO 0
#endif

#ifndef CHECKSUM_PARTIAL
#define CHECKSUM_PARTIAL CHECKSUM_HW
#define CHECKSUM_COMPLETE CHECKSUM_HW
#endif

#ifndef IRQ_HANDLED
#define irqreturn_t void
#define IRQ_HANDLED
#define IRQ_NONE
#endif

#ifndef IRQF_SHARED
#define IRQF_SHARED SA_SHIRQ
#endif

#ifndef PCI_VDEVICE
#define PCI_VDEVICE(vendor, device) \
	PCI_VENDOR_ID_##vendor, (device), \
	PCI_ANY_ID, PCI_ANY_ID, 0, 0
#endif

#ifndef round_jiffies
#define round_jiffies(j) (j)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 14))
static inline signed long schedule_timeout_uninterruptible(signed long timeout)
{
	set_current_state(TASK_UNINTERRUPTIBLE);
	return schedule_timeout(timeout);
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 15))
static inline void *kzalloc(size_t size, unsigned int flags)
{
	void *mem = kmalloc(size, flags);
	if (mem)
		memset(mem, 0, size);
	return mem;
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18))
static inline int kcompat_skb_linearize(struct sk_buff *skb, int gfp)
{
	return skb_linearize(skb, gfp);
}
#undef skb_linearize
#define skb_linearize(skb) kcompat_skb_linearize(skb, GFP_ATOMIC)
#endif

#if !defined(__VMKLNX__)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19))

#ifndef RHEL_RELEASE_CODE
#define RHEL_RELEASE_CODE 0
#endif
#ifndef RHEL_RELEASE_VERSION
#define RHEL_RELEASE_VERSION(a, b) 0
#endif

#if (!((RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(4, 4)) && \
       (RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(5, 0)) || \
       (RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(5, 0))))
typedef irqreturn_t (*irq_handler_t)(int, void*, struct pt_regs *);
#endif

typedef irqreturn_t (*kcompat_irq_handler_t)(int, void *);
static inline int kcompat_request_irq(unsigned int irq,
	kcompat_irq_handler_t handler, unsigned long flags,
	const char *devname, void *dev_id)
{
	irq_handler_t irq_handler = (irq_handler_t)handler;
	return request_irq(irq, irq_handler, flags, devname, dev_id);
}
#undef request_irq
#define request_irq(irq, handler, flags, devname, dev_id) \
	kcompat_request_irq(irq, handler, flags, devname, dev_id)
#endif
#endif /* !defined(__VMKLNX__) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20))
#define csum_offset csum
#undef INIT_WORK
#define INIT_WORK(_work, _func) \
do { \
	INIT_LIST_HEAD(&(_work)->entry); \
	(_work)->pending = 0; \
	(_work)->func = (void (*)(void *))_func; \
	(_work)->data = _work; \
	init_timer(&(_work)->timer); \
} while (0)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 22))
#define ip_hdr(skb) (skb->nh.iph)
#define ipv6_hdr(skb) (skb->nh.ipv6h)
#define tcp_hdr(skb) (skb->h.th)
#define tcp_hdrlen(skb) (skb->h.th->doff << 2)
#define skb_transport_offset(skb) (skb->h.raw - skb->data)
#endif

#if !defined(__VMKLNX__)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 23))
#undef kmem_cache_create
#define kmem_cache_create(name, size, align, flags, ctor) \
	kmem_cache_create(name, size, align, flags, ctor, NULL)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 23))
#define scsi_sglist(sc) ((struct scatterlist *) (sc)->request_buffer)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24))
#define BIT(nr) (1UL << (nr))
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24))
#define for_each_sg(sglist, sg, nr, __i) \
	for (__i = 0, sg = (sglist); __i < (nr); __i++, sg++)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24))
struct napi_struct {
	struct net_device *netdev;
	int (*poll)(struct napi_struct *, int);
};
#define netif_rx_complete(netdev, napi) netif_rx_complete(netdev)
#define netif_rx_schedule_prep(netdev, napi) netif_rx_schedule_prep(netdev)
#define netif_rx_schedule(netdev, napi) netif_rx_schedule(netdev)
#define __netif_rx_schedule(netdev, napi) __netif_rx_schedule(netdev)
#define napi_enable(napi) netif_poll_enable(*napi.netdev)
#define napi_disable(napi) netif_poll_disable(*napi.netdev)
#define netif_napi_add(netdev, napi, _poll, _weight) \
	do { \
		struct napi_struct *__napi = napi; \
		netdev->poll = __enic_poll; \
		netdev->weight = _weight; \
		__napi->poll = _poll; \
		__napi->netdev = netdev; \
		netif_poll_disable(netdev); \
	} while (0)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24))
struct net_lro_desc {
};
struct net_lro_mgr {
	struct net_device *dev;
	unsigned long features;
#define LRO_F_NAPI		1
#define LRO_F_EXTRACT_VLAN_ID	2
	u32 ip_summed;
	u32 ip_summed_aggr;
	int max_desc;
	int max_aggr;
	struct net_lro_desc *lro_arr;
	int (*get_skb_header)(struct sk_buff *skb, void **ip_hdr,
		void **tcpudp_hdr, u64 *hdr_flags, void *priv);
#define LRO_IPV4	1
#define LRO_TCP		2
};
#define lro_vlan_hwaccel_receive_skb(m, skb, vlan_group, vlan, p) \
	vlan_hwaccel_receive_skb(skb, vlan_group, vlan)
#define lro_receive_skb(m, skb, p) \
	netif_receive_skb(skb)
#define lro_flush_all(m) do { } while (0)
#define skb_reset_network_header(skb) do { } while (0)
#define ip_hdrlen(skb) 0
#define skb_set_transport_header(skb, offset) do { } while (0)
#endif
#endif /* !defined(__VMKLNX__) */

#if defined(__VMKLNX__)
#define for_each_sg(start, var, count, index) \
		for ((var) = (start), (index) = 0; (index) < (count); \
			(var) = sg_next(var), (index)++)

#define kmem_cache kmem_cache_s
#endif /* __VMKLNX__ */

#ifdef CONFIG_MIPS
#include "kcompat_mips.h"
#endif


#endif /* _KCOMPAT_H_ */
