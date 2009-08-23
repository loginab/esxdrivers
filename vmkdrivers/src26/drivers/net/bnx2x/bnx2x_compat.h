#ifndef __BNX2X_COMPAT_H__
#define __BNX2X_COMPAT_H__

#if (LINUX_VERSION_CODE < 0x020617)

#define skb_copy_from_linear_data_offset(skb, pad, new_skb_data, len) \
				memcpy(new_skb_data, skb->data + pad, len)

#define netif_rx_schedule(dev, X)	netif_rx_schedule(dev)
#define netif_rx_complete(X, Y)		netif_rx_complete(dev)

/* skb_buff accessors */
#define ip_hdr(skb)			(skb)->nh.iph
#define ipv6_hdr(skb)			(skb)->nh.ipv6h
#define ip_hdrlen(skb)			(ip_hdr(skb)->ihl * 4)
#define tcp_hdr(skb)			(skb)->h.th
#define tcp_hdrlen(skb)			(tcp_hdr(skb)->doff * 4)
#define udp_hdr(skb)			(skb)->h.uh
#define skb_mac_header(skb)		((skb)->mac.raw)
#define skb_network_header(skb)		((skb)->nh.raw)
#define skb_transport_header(skb)	((skb)->h.raw)

#endif


#ifndef CHECKSUM_PARTIAL
#define CHECKSUM_PARTIAL		CHECKSUM_HW
#endif


#ifndef SET_MODULE_OWNER
#define SET_MODULE_OWNER(dev)
#endif


#if (LINUX_VERSION_CODE < 0x020604)
#define MODULE_VERSION(version)
#endif


#if (LINUX_VERSION_CODE < 0x020600)
#define might_sleep()

#define num_online_cpus()		1

#define dev_info(dev, format, args...) \
				printk(KERN_INFO "bnx2x: " format, ##args)

#define dev_err(dev, format, args...) \
				printk(KERN_ERR "bnx2x: " format, ##args)

static inline int dma_mapping_error(dma_addr_t mapping)
{
	return 0;
}

#define synchronize_irq(X)		synchronize_irq()
#define flush_scheduled_work()
#endif


#if (LINUX_VERSION_CODE < 0x020605)
static inline void pci_dma_sync_single_for_device(struct pci_dev *dev,
						  dma_addr_t map, size_t size,
						  int dir)
{
}
#endif


#if (LINUX_VERSION_CODE < 0x020547)
#define pci_set_consistent_dma_mask(X, Y)	(0)
#endif


#if (LINUX_VERSION_CODE < 0x020607)
#define msleep(x) \
	do { \
		current->state = TASK_UNINTERRUPTIBLE; \
		schedule_timeout((HZ * (x)) / 1000); \
	} while (0)

#ifndef ADVERTISE_1000XPAUSE
static inline struct mii_ioctl_data *if_mii(struct ifreq *rq)
{
	return (struct mii_ioctl_data *)&rq->ifr_ifru;
}
#endif

#define pci_enable_msix(X, Y, Z)	(-1)
#endif


#if (LINUX_VERSION_CODE < 0x020609)
#define msleep_interruptible(x) \
	do{ \
		current->state = TASK_INTERRUPTIBLE; \
		schedule_timeout((HZ * (x)) / 1000); \
	} while (0)

#endif


#if (LINUX_VERSION_CODE < 0x02060b)
#define pm_message_t			u32
#define pci_power_t			u32
#define PCI_D0				0
#define PCI_D3hot			3
#define pci_choose_state(pdev, state)	state
#endif


#if (LINUX_VERSION_CODE < 0x02060e)
#define touch_softlockup_watchdog()
#endif


#if (LINUX_VERSION_CODE < 0x020612)
static inline struct sk_buff *netdev_alloc_skb(struct net_device *dev,
					       unsigned int length)
{
	struct sk_buff *skb = dev_alloc_skb(length);

	if (skb)
		skb->dev = dev;
	return skb;
}
#endif


#ifndef IRQ_HANDLED
typedef void irqreturn_t;
#define IRQ_HANDLED
#define IRQ_NONE
#endif


#ifndef IRQF_SHARED
#define IRQF_SHARED			SA_SHIRQ
#endif


#ifndef NETIF_F_GSO
static inline void netif_tx_lock(struct net_device *dev)
{
	spin_lock(&dev->xmit_lock);
	dev->xmit_lock_owner = smp_processor_id();
}

static inline void netif_tx_unlock(struct net_device *dev)
{
	dev->xmit_lock_owner = -1;
	spin_unlock(&dev->xmit_lock);
}
#endif


#ifndef skb_shinfo
#define skb_shinfo(SKB)	((struct skb_shared_info *)(skb_end_pointer(SKB)))
#endif


#ifdef NETIF_F_TSO
#ifndef NETIF_F_GSO

static inline int skb_is_gso(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->tso_size;
}

#define gso_size			tso_size

#endif /* NETIF_F_GSO */

#ifndef NETIF_F_GSO_SOFTWARE
#define NETIF_F_GSO_SOFTWARE		(NETIF_F_TSO)
#endif

#endif /* NETIF_F_TSO */

#ifndef NETIF_F_TSO_ECN
#define NETIF_F_TSO_ECN			0
#endif


#if !defined(mmiowb)
#define mmiowb()
#endif

#if !defined(__iomem)
#define __iomem
#endif

#ifndef noinline
#define noinline
#endif

#ifndef WARN_ON
#define WARN_ON(X)
#endif

#if !defined(INIT_WORK)
#define INIT_WORK INIT_TQUEUE
#define schedule_work			schedule_task
#define work_struct			tq_struct
#endif


#if !defined(HAVE_NETDEV_PRIV) && (LINUX_VERSION_CODE != 0x020603) && (LINUX_VERSION_CODE != 0x020604) && (LINUX_VERSION_CODE != 0x020605)
#define netdev_priv(dev)		(dev)->priv
#endif

/* Missing defines */
#ifndef SPEED_2500
#define SPEED_2500			2500
#endif

#ifndef SUPPORTED_Pause
#define SUPPORTED_Pause			(1 << 13)
#endif
#ifndef SUPPORTED_Asym_Pause
#define SUPPORTED_Asym_Pause		(1 << 14)
#endif

#ifndef ADVERTISED_Pause
#define ADVERTISED_Pause		(1 << 13)
#endif

#ifndef ADVERTISED_Asym_Pause
#define ADVERTISED_Asym_Pause		(1 << 14)
#endif

#ifndef NETDEV_TX_BUSY
#define NETDEV_TX_BUSY			1 /* driver tx path was busy */
#endif

#ifndef NETDEV_TX_OK
#define NETDEV_TX_OK			0 /* driver took care of packet */
#endif


#ifndef DMA_64BIT_MASK
#define DMA_64BIT_MASK			((u64) 0xffffffffffffffffULL)
#define DMA_32BIT_MASK			((u64) 0x00000000ffffffffULL)
#endif

#ifndef PCI_CAP_ID_EXP
#define PCI_CAP_ID_EXP			0x10
#endif

#ifndef PCI_EXP_DEVCTL
#define PCI_EXP_DEVCTL			8	/* Device Control */
#endif

#ifndef PCI_EXP_DEVCTL_PAYLOAD
#define PCI_EXP_DEVCTL_PAYLOAD		0x00e0	/* Max_Payload_Size */
#endif

#ifndef PCI_EXP_DEVCTL_READRQ
#define PCI_EXP_DEVCTL_READRQ		0x7000	/* Max_Read_Request_Size */
#endif


#if (LINUX_VERSION_CODE < 0x020618)

#ifndef NETIF_F_HW_CSUM
#define NETIF_F_HW_CSUM			8
#endif

static inline int bnx2x_set_tx_hw_csum(struct net_device *dev, u32 data)
{
	if (data)
		dev->features |= NETIF_F_HW_CSUM;
	else
		dev->features &= ~NETIF_F_HW_CSUM;
	return 0;
}
#endif


/* If mutex is not available, use semaphore */
#ifndef __LINUX_MUTEX_H
#define mutex				semaphore
#define mutex_lock(x)			down(x)
#define mutex_unlock(x)			up(x)
#define mutex_init(x)			sema_init(x,1)
#endif


#ifndef KERN_CONT
#define KERN_CONT
#endif


#if (LINUX_VERSION_CODE < 0x020606) || defined(BNX2X_DRIVER_DISK) || defined(__VMKLNX__)

#define CRC32C_POLY_LE			0x82F63B78

static inline u32 crc32c_le(u32 crc, unsigned char const *p, size_t len)
{
	int i;

	while (len--) {
		crc ^= *p++;
		for (i = 0; i < 8; i++)
			crc = (crc >> 1) ^ ((crc & 1) ? CRC32C_POLY_LE : 0);
	}
	return crc;
}
#endif

#endif /* __BNX2X_COMPAT_H__ */
