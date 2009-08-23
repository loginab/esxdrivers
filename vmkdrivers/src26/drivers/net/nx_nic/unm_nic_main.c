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
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
//#include <linux/kgdb-defs.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>

#include "nx_errorcode.h"
#include "nxplat.h"
#include "nxhal_nic_interface.h"
#include "nxhal_nic_api.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,21)
#include <linux/ethtool.h>
#endif
#include <linux/mii.h>
#include <linux/interrupt.h>
#include <linux/timer.h>

#include <linux/mm.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 0)
#include <linux/wrapper.h>
#endif
#ifndef _LINUX_MODULE_PARAMS_H
#include <linux/moduleparam.h>
#endif
#include <asm/system.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/checksum.h>
#include "kernel_compatibility.h"
#include "unm_nic_hw.h"
#include "unm_nic_config.h"
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
#include <net/net_namespace.h>
#endif


/* AEL1002 supports 3 device addresses: 1(PMA/PMD), 3(PCS), and 4(PHY XS) */
#define DEV_PMA_PMD 1
#define DEV_PCS     3
#define DEV_PHY_XS  4

/* Aeluros-specific registers use device address 1 */
#define AEL_POWERDOWN_REG   0xc011
#define AEL_TX_CONFIG_REG_1 0xc002
#define AEL_LOOPBACK_EN_REG 0xc017
#define AEL_MODE_SEL_REG    0xc001

#define PMD_RESET          0
#define PMD_STATUS         1
#define PMD_IDENTIFIER     2
#define PCS_STATUS_REG     0x20

#define PMD_ID_QUAKE    0x43
#define PMD_ID_MYSTICOM 0x240
#define PCS_CONTROL 0

#define PHY_XS_LANE_STATUS_REG 0x18

#undef UNM_LOOPBACK
#undef SINGLE_DMA_BUF
#define UNM_NIC_HW_CSUM
// #define UNM_SKB_TASKLET

#define PCI_CAP_ID_GEN  0x10


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define UNM_NETIF_F_TSO
#endif

#ifndef DMA_64BIT_MASK
#define DMA_64BIT_MASK			 0xffffffffffffffffULL
#endif

#ifndef DMA_39BIT_MASK
#define DMA_39BIT_MASK			 0x0000007fffffffffULL
#endif

#ifndef DMA_35BIT_MASK
#define DMA_35BIT_MASK                   0x00000007ffffffffULL
#endif

#ifndef DMA_32BIT_MASK
#define DMA_32BIT_MASK                   0x00000000ffffffffULL
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,19)
#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
		#include <linux/if_vlan.h>
		#define VLAN_CHUNK 128
#else
		#undef UNM_NIC_HW_VLAN
#endif
#else
	#undef UNM_NIC_HW_VLAN

#endif //LINUX_KERNEL_VERSION >= 2.4.19

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)
#define	CHECKSUM_HW	CHECKSUM_PARTIAL
#endif /* KERNEL_VERSION(2,4,19) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)
#define PCI_EXP_LNKSTA 18
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
#define	TASK_PARAM		struct work_struct *
#define	NX_INIT_WORK(a, b, c)	INIT_WORK(a, b)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define	TASK_PARAM		unsigned long
#define	NX_INIT_WORK(a, b, c)	INIT_WORK(a, (void (*)(void *))b, c)
#else
#define	TASK_PARAM		unsigned long
#define	NX_INIT_WORK(a, b, c)	INIT_TQUEUE(a, (void (*)(void *))b, c)
#endif

#include "unm_nic.h"

#if defined(ESX_3X)
#include <asm/checksum.h>
#include "vmklinux_dist.h"
#include "smp_drv.h"
#include "asm/page.h"
#endif

#define DEFINE_GLOBAL_RECV_CRB
#include "nic_phan_reg.h"

#include "unm_nic_ioctl.h"
#include "nic_cmn.h"
#include "nx_license.h"
#include "nxhal.h"
#include "unm_nic_config.h"
#include "unm_nic_lro.h"

#include "unm_nic_hw.h"
#include "unm_version.h"
#include "unm_brdcfg.h"
#include "nx_nic_linux_tnic_api.h"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_LICENSE("GPL");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
MODULE_VERSION(UNM_NIC_LINUX_VERSIONID);
#endif

static int use_msi = 1;
#if defined(ESX)
static int rss_enable = 0;
static int use_msi_x = 1;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,8))
static int rss_enable = 1;
static int use_msi_x = 1;
#else
static int rss_enable = 0;
static int use_msi_x = 0;
#endif
static int tx_desc		= MAX_CMD_DESCRIPTORS_HOST;
static int jumbo_desc		= NX_DEFAULT_JUMBO_RDS_SIZE;
static int lro_desc		= MAX_LRO_RCV_DESCRIPTORS;
static int rdesc_1g		= NX_DEFAULT_RDS_SIZE_1G;
static int rdesc_10g		= NX_DEFAULT_RDS_SIZE;
static int rx_chained		= 0;

static int fw_load = 1;
module_param(fw_load, int, S_IRUGO);
MODULE_PARM_DESC(fw_load, "Load firmware from file system or flash");

static int port_mode = UNM_PORT_MODE_AUTO_NEG;	// Default to auto-neg. mode
module_param(port_mode, int, S_IRUGO);
MODULE_PARM_DESC(port_mode, "Ports operate in XG, 1G or Auto-Neg mode");

static int wol_port_mode         = 5; // Default to restricted 1G auto-neg. mode
module_param(wol_port_mode, int, S_IRUGO);
MODULE_PARM_DESC(wol_port_mode, "In wol mode, ports operate in XG, 1G or Auto-Neg");

module_param(use_msi, bool, S_IRUGO);
MODULE_PARM_DESC(use_msi, "Enable or Disable msi");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,8)
module_param(use_msi_x, bool, S_IRUGO);
MODULE_PARM_DESC(use_msi_x, "Enable or Disable msi-x");

module_param(rss_enable, bool, S_IRUGO);
MODULE_PARM_DESC(rss_enable, "Enable or Disable RSS");
#endif

module_param(tx_desc, int, S_IRUGO);
MODULE_PARM_DESC(tx_desc, "Maximum Transmit Descriptors in Host");

module_param(jumbo_desc, int, S_IRUGO);
MODULE_PARM_DESC(jumbo_desc, "Maximum Jumbo Receive Descriptors");

#if 0
module_param(lro_desc, int, S_IRUGO);
MODULE_PARM_DESC(lro_desc, "Maximum LRO Receive Descriptors");
#endif

module_param(rdesc_1g, int, S_IRUGO);
MODULE_PARM_DESC(rdesc_1g, "Maximum Receive Descriptors for 1G");

module_param(rdesc_10g, int, S_IRUGO);
MODULE_PARM_DESC(rdesc_10g, "Maximum Receive Descriptors for 10G");

#if 1
module_param(rx_chained, int, S_IRUGO);
MODULE_PARM_DESC(rx_chained, "Rx buffer for Jumbo/LRO are chained");
#endif


char unm_nic_driver_name[] = DRIVER_NAME;
char unm_nic_driver_string[] = DRIVER_VERSION_STRING
    UNM_NIC_LINUX_VERSIONID
    "-" UNM_NIC_BUILD_NO " generated " UNM_NIC_TIMESTAMP;
uint8_t nx_nic_msglvl = NX_NIC_NOTICE;
char *nx_nic_kern_msgs[] = {
	KERN_EMERG,
	KERN_ALERT,
	KERN_CRIT,
	KERN_ERR,
	KERN_WARNING,
	KERN_NOTICE,
	KERN_INFO,
	KERN_DEBUG
};


struct loadregs {
    uint32_t function;
    uint32_t offset;
    uint32_t andmask;
    uint32_t ormask;
};

#define LOADREGCOUNT   8
struct loadregs driverloadregs_gen1[LOADREGCOUNT] = {
    { 0, 0xD8, 0x00000000, 0x000F1000  },
    { 1, 0xD8, 0x00000000, 0x000F1000  },
    { 2, 0xD8, 0x00000000, 0x000F1000  },
    { 3, 0xD8, 0x00000000, 0x000F1000  },
    { 4, 0xD8, 0x00000000, 0x000F1000  },
    { 5, 0xD8, 0x00000000, 0x000F1000  },
    { 6, 0xD8, 0x00000000, 0x000F1000  },
    { 7, 0xD8, 0x00000000, 0x000F1000  }
};

struct loadregs driverloadregs_gen2[LOADREGCOUNT] = {
    { 0, 0xC8, 0x00000000, 0x000F1000  },
    { 1, 0xC8, 0x00000000, 0x000F1000  },
    { 2, 0xC8, 0x00000000, 0x000F1000  },
    { 3, 0xC8, 0x00000000, 0x000F1000  },
    { 4, 0xC8, 0x00000000, 0x000F1000  },
    { 5, 0xC8, 0x00000000, 0x000F1000  },
    { 6, 0xC8, 0x00000000, 0x000F1000  },
    { 7, 0xC8, 0x00000000, 0x000F1000  }
};




static int adapter_count = 0;

static uint32_t msi_tgt_status[8] = {
	ISR_INT_TARGET_STATUS, ISR_INT_TARGET_STATUS_F1,
	ISR_INT_TARGET_STATUS_F2, ISR_INT_TARGET_STATUS_F3,
	ISR_INT_TARGET_STATUS_F4, ISR_INT_TARGET_STATUS_F5,
	ISR_INT_TARGET_STATUS_F6, ISR_INT_TARGET_STATUS_F7
};


static struct nx_legacy_intr_set legacy_intr[] = NX_LEGACY_INTR_CONFIG;

#ifndef ARCH_KMALLOC_MINALIGN
#define ARCH_KMALLOC_MINALIGN 0
#endif
#ifndef ARCH_KMALLOC_FLAGS
#define ARCH_KMALLOC_FLAGS SLAB_HWCACHE_ALIGN
#endif

#define UNM_NETDEV_WEIGHT	256

#define RCV_DESC_RINGSIZE(COUNT)    (sizeof(rcvDesc_t) * (COUNT))
#define STATUS_DESC_RINGSIZE(COUNT) (sizeof(statusDesc_t)* (COUNT))
#define TX_RINGSIZE(COUNT)          (sizeof(struct unm_cmd_buffer) * (COUNT))
#define RCV_BUFFSIZE(COUNT)         (sizeof(struct unm_rx_buffer) * (COUNT))

#define UNM_NIC_INT_RESET       0x2004
#define UNM_DB_MAPSIZE_BYTES    0x1000
#define UNM_CMD_PRODUCER_OFFSET                 0
#define UNM_RCV_STATUS_CONSUMER_OFFSET          0
#define UNM_RCV_PRODUCER_OFFSET                 0

#define MAX_RX_CTX 		1
#define MAX_TX_CTX 		1
/* Extern definition required for vmkernel module */

#ifdef UNM_NIC_HW_VLAN
/* following 2 functions are needed in order to enable
 * vlan hw acceleration
 */
static void unm_nic_vlan_rx_register(struct net_device *net_dev,
 	       			     struct vlan_group *grp);
static void unm_nic_vlan_rx_kill_vid(struct net_device *net_dev, unsigned short vid);
static void unm_nic_vlan_rx_add_vid(struct net_device *net_dev, unsigned short vid);
#endif

/* Local functions to UNM NIC driver */
static int __devinit unm_nic_probe(struct pci_dev *pdev,
				   const struct pci_device_id *ent);
static void __devexit unm_nic_remove(struct pci_dev *pdev);
void *nx_alloc(struct unm_adapter_s *adapter, size_t sz,
	       dma_addr_t * ptr, struct pci_dev **used_dev);
static int unm_nic_open(struct net_device *netdev);
static int unm_nic_close(struct net_device *netdev);
static int unm_nic_xmit_frame(struct sk_buff *, struct net_device *);
static int unm_nic_set_mac(struct net_device *netdev, void *p);
static int unm_nic_change_mtu(struct net_device *netdev, int new_mtu);
int receive_peg_ready(struct unm_adapter_s *adapter);
int unm_nic_hw_resources(struct unm_adapter_s *adapter);
static void nx_p2_nic_set_multi(struct net_device *netdev);
static void nx_p3_nic_set_multi(struct net_device *netdev);
void initialize_adapter_sw(struct unm_adapter_s *adapter, nx_host_rx_ctx_t *nxhal_host_rx_ctx);
int init_firmware(struct unm_adapter_s *adapter);
int unm_post_rx_buffers(struct unm_adapter_s *adapter,nx_host_rx_ctx_t *nxhal_host_rx_ctx, uint32_t type);
static int nx_alloc_rx_skb(struct unm_adapter_s *adapter,
			   nx_host_rds_ring_t * rcv_desc,
			   struct unm_rx_buffer *buffer);
static inline void unm_process_lro(struct unm_adapter_s *adapter,
				   nx_host_sds_ring_t *nxhal_sds_ring,
				   statusDesc_t *desc,
				   statusDesc_t *desc_list,
				   int num_desc);
static inline void unm_process_rcv(struct unm_adapter_s *adapter,
				   nx_host_sds_ring_t *nxhal_sds_ring,
				   statusDesc_t * desc,
				   statusDesc_t * frag_desc);

static int unm_nic_new_rx_context_prepare(struct unm_adapter_s *adapter, nx_host_rx_ctx_t *nxhal_rx_ctx);
static int unm_nic_new_tx_context_prepare(struct unm_adapter_s *adapter);
static int unm_nic_new_tx_context_destroy(struct unm_adapter_s *adapter);
static int unm_nic_new_rx_context_destroy(struct unm_adapter_s *adapter);
static void nx_nic_free_hw_rx_resources(struct unm_adapter_s *adapter, nx_host_rx_ctx_t *nxhal_rx_ctx);
static void nx_nic_free_host_rx_resources(struct unm_adapter_s *adapter, nx_host_rx_ctx_t *nxhal_rx_ctx);
static void nx_nic_free_host_sds_resources(struct unm_adapter_s *adapter,
                nx_host_rx_ctx_t *nxhal_host_rx_ctx);
int nx_nic_create_rx_ctx(struct net_device *netdev);

static void unm_tx_timeout(struct net_device *netdev);
static void unm_tx_timeout_task(TASK_PARAM adapid);
static int unm_process_cmd_ring(unsigned long data);
static void unm_nic_down(struct unm_adapter_s *adapter);
static int unm_nic_do_ioctl(struct unm_adapter_s *adapter, void *u_data);
static int initialize_adapter_hw(struct unm_adapter_s *adapter);
static void unm_watchdog_task(TASK_PARAM adapid);
void unm_nic_enable_all_int(unm_adapter * adapter, nx_host_rx_ctx_t *nxhal_rx_ctx);
void unm_nic_enable_int(unm_adapter * adapter, nx_host_sds_ring_t *nxhal_sds_ring);
void unm_nic_disable_all_int(unm_adapter *adapter,
			     nx_host_rx_ctx_t *nxhal_rx_ctx);
static void unm_nic_disable_int(unm_adapter *adapter,
				nx_host_sds_ring_t *nxhal_sds_ring);
static int nx_status_msg_nic_response_handler(struct net_device *netdev, void *data, 
					      unm_msg_t *msg, struct sk_buff *skb);
static int nx_status_msg_default_handler(struct net_device *netdev, void *data, 
					 unm_msg_t *msg, struct sk_buff *skb);
static uint32_t unm_process_rcv_ring(struct unm_adapter_s *,
				     nx_host_sds_ring_t *nxhal_sds_ring, int);
static void unm_nic_clear_stats(struct unm_adapter_s *);
static struct net_device_stats *unm_nic_get_stats(struct net_device *netdev);
static int unm_nic_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd);
int unm_read_blink_state(char *buf, char **start, off_t offset,
				int count, int *eof, void *data);
int unm_write_blink_state(struct file *file, const char *buffer,
		                unsigned long count, void *data);
int unm_read_blink_rate(char *buf, char **start, off_t offset,
				  int count, int *eof, void *data);
int unm_write_blink_rate(struct file *file, const char *buffer,
		                unsigned long count, void *data);
extern U32
issue_cmd(nx_dev_handle_t drv_handle,  U32 pci_fn, U32 version,
		U32 arg1, U32 arg2, U32 arg3, U32 cmd);


static void unm_pci_release_regions(struct pci_dev *pdev);
#ifdef UNM_NIC_NAPI
/* static int    unm_nic_rx_has_work(struct unm_adapter_s *adapter, int idx); */
static int unm_nic_tx_has_work(struct unm_adapter_s *adapter);
#ifdef NEW_NAPI
static int nx_nic_poll_sts(struct napi_struct *napi, int work_to_do);
#else
static int nx_nic_poll_sts(struct net_device *netdev, int *budget);
static int unm_nic_poll(struct net_device *dev, int *budget);
#endif
#endif

#ifdef CONFIG_NET_POLL_CONTROLLER
static void unm_nic_poll_controller(struct net_device *netdev);
#endif
#ifdef UNM_NETIF_F_TSO
static inline void unm_tso_check(struct unm_adapter_s *adapter,
				 cmdDescType0_t * desc,
				 uint32_t tagged,
                                 struct sk_buff *skb);

#endif

static inline void unm_tx_csum(struct unm_adapter_s *adapter,
                                cmdDescType0_t *desc,
                                struct sk_buff *skb);

#ifdef OLD_KERNEL
static void unm_intr(int irq, void *data, struct pt_regs *regs);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
static irqreturn_t unm_intr(int irq, void *data, struct pt_regs *regs);
#else
static irqreturn_t unm_intr(int irq, void *data);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,21)
extern void set_ethtool_ops(struct net_device *netdev);
#endif

extern int unm_init_proc_drv_dir(void);
extern void unm_cleanup_proc_drv_entries(void);
extern void unm_init_proc_entries(struct unm_adapter_s *adapter);
extern void unm_cleanup_proc_entries(struct unm_adapter_s *adapter);

static void unm_init_pending_cmd_desc(unm_pending_cmd_descs_t * pending_cmds);
static void unm_destroy_pending_cmd_desc(unm_pending_cmd_descs_t *
					 pending_cmds);
static void unm_proc_pending_cmd_desc(unm_adapter * adapter);
static inline int unm_get_pending_cmd_desc_cnt(unm_pending_cmd_descs_t *
					       pending_cmds);

#if defined(XGB_DEBUG)
static void dump_skb(struct sk_buff *skb);
static int skb_is_sane(struct sk_buff *skb);
#endif

int nx_nic_send_cmd_descs(struct net_device *dev,
			  cmdDescType0_t * cmd_desc_arr, int nr_elements);

int unm_loopback_test(struct net_device *netdev, int fint, void *ptr,
		      unm_test_ctr_t * testCtx);
static void nx_free_sts_rings(struct unm_adapter_s *adapter, nx_host_rx_ctx_t *nxhal_rx_ctx);
static int nx_alloc_sts_rings(struct unm_adapter_s *adapter, nx_host_rx_ctx_t *nxhal_rx_ctx ,int cnt);
static int nx_register_irq(struct unm_adapter_s *adapter,nx_host_rx_ctx_t *nxhal_host_rx_ctx);
static int nx_unregister_irq(struct unm_adapter_s *adapter, nx_host_rx_ctx_t *nxhal_host_rx_ctx);

int unm_nic_fill_statistics_128M(struct unm_adapter_s *adapter,
	struct unm_statistics *unm_stats);
int unm_nic_fill_statistics_2M(struct unm_adapter_s *adapter,
	struct unm_statistics *unm_stats);
int unm_nic_clear_statistics_128M(struct unm_adapter_s *adapter);
int unm_nic_clear_statistics_2M(struct unm_adapter_s *adapter);
static int nx_p3_nic_set_promisc(struct unm_adapter_s * adapter);

static int nx_init_status_msg_handler(struct unm_adapter_s *adapter);
static int nx_init_status_handler(struct unm_adapter_s *adapter);

int nx_nic_is_netxen_device(struct net_device *netdev);
int nx_nic_rx_register_msg_handler(struct net_device *netdev, uint8_t msgtype, void *data,
				   int (*nx_msg_handler) (struct net_device *netdev, void *data,
							  unm_msg_t *msg, struct sk_buff *skb));
void nx_nic_rx_unregister_msg_handler(struct net_device *netdev, uint8_t msgtype);
int nx_nic_rx_register_callback_handler(struct net_device *netdev, uint8_t interface_type,
					void *data);
void nx_nic_rx_unregister_callback_handler(struct net_device *netdev, uint8_t interface_type);
int nx_nic_get_adapter_revision_id(struct net_device *dev);
nx_tnic_adapter_t *nx_nic_get_lsa_adapter(struct net_device *netdev);
int nx_nic_get_device_port(struct net_device *netdev);
int nx_nic_get_device_ring_ctx(struct net_device *netdev);
struct pci_dev *nx_nic_get_pcidev(struct net_device *dev);
void nx_nic_get_lsa_version_number(struct net_device *netdev,
				    nic_version_t * version);
void nx_nic_get_nic_version_number(struct net_device *netdev,
				   nic_version_t * version);
int nx_nic_send_msg_to_fw(struct net_device *dev,
			  pegnet_cmd_desc_t * cmd_desc_arr, int nr_elements);
int nx_nic_cmp_adapter_id(struct net_device *dev1, struct net_device *dev2);
static int nx_nic_get_stats(struct net_device *dev, netxen_pstats_t *stats);
void nx_nic_handle_tx_timeout(struct unm_adapter_s *adapter);
nx_nic_api_t *nx_nic_get_api(void);

nx_nic_api_t nx_nic_api_struct = {
        .api_ver                        = NX_NIC_API_VER,
        .is_netxen_device               = nx_nic_is_netxen_device,
        .register_msg_handler           = nx_nic_rx_register_msg_handler,
        .unregister_msg_handler         = nx_nic_rx_unregister_msg_handler,
        .register_callback_handler      = nx_nic_rx_register_callback_handler,
        .unregister_callback_handler    = nx_nic_rx_unregister_callback_handler,
        .get_adapter_rev_id             = nx_nic_get_adapter_revision_id,
        .get_lsa_adapter                = nx_nic_get_lsa_adapter,
        .get_device_port                = nx_nic_get_device_port,
        .get_device_ring_ctx            = nx_nic_get_device_ring_ctx,
        .get_pcidev                     = nx_nic_get_pcidev,
        .get_lsa_ver_num                = nx_nic_get_lsa_version_number,
        .get_nic_ver_num                = nx_nic_get_nic_version_number,
        .send_msg_to_fw                 = nx_nic_send_msg_to_fw,
        .cmp_adapter_id                 = nx_nic_cmp_adapter_id,   
        .get_net_stats                  = nx_nic_get_stats   
};

static int nx_init_status_msg_handler(struct unm_adapter_s *adapter)
{
	int i = 0;

	for (i = 0; i < NX_MAX_SDS_OPCODE; i++) {
		adapter->nx_status_msg_handler_table[i].msg_type         = i;
		adapter->nx_status_msg_handler_table[i].data             = NULL;
		adapter->nx_status_msg_handler_table[i].handler          = nx_status_msg_default_handler;
		adapter->nx_status_msg_handler_table[i].registered       = 0;

		if (i == UNM_MSGTYPE_NIC_RESPONSE) {
			adapter->nx_status_msg_handler_table[i].handler    = nx_status_msg_nic_response_handler; 
			adapter->nx_status_msg_handler_table[i].registered = 1;
		}
	} 

	return 0;
}

static int nx_init_status_callback_handler(struct unm_adapter_s *adapter)
{
	int i = 0;

	for (i = 0; i < NX_NIC_CB_MAX; i++) {
		adapter->nx_status_callback_handler_table[i].interface_type = i;
		adapter->nx_status_callback_handler_table[i].data           = NULL;
		adapter->nx_status_callback_handler_table[i].registered     = 0;
		adapter->nx_status_callback_handler_table[i].refcnt         = 0;
	} 

	return 0;
}

static int nx_init_status_handler(struct unm_adapter_s *adapter)
{
	int i = 0;

	for (i = 0; i < NX_NIC_CB_MAX; i++) {
		spin_lock_init(&adapter->nx_status_callback_handler_table[i].lock);
	}

	nx_init_status_msg_handler(adapter);
	nx_init_status_callback_handler(adapter);	

	return 0;
}

#define DIDX_DIF(p, c)        \
   ((p >= c) ? (p - c) : (tx_desc - p + c))

#define CIS_WATERMARK         0x60

#define PEG_LOOP         1000	/* loop to pegstuck? */

#define QMAJ(hdr)        ((hdr >> 19) & 0xF)
#define QMIN(hdr)        (hdr & 0x3ffff)
#define QSUB(hdr)        ((hdr >> 18) & 0x1)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
/*
 * In unm_nic_down(), we must wait for any pending callback requests into
 * unm_watchdog_task() to complete; eg otherwise the watchdog_timer could be
 * reenabled right after it is deleted in unm_nic_down(). FLUSH_SCHEDULED_WORK()
 * does this synchronization.
 *
 * Normally, schedule_work()/flush_scheduled_work() could have worked, but
 * unm_nic_close() is invoked with kernel rtnl lock held. netif_carrier_off()
 * call in unm_nic_close() triggers a schedule_work(&linkwatch_work), and a
 * subsequent call to flush_scheduled_work() in unm_nic_down() would cause
 * linkwatch_event() to be executed which also attempts to acquire the rtnl
 * lock thus causing a deadlock.
 */
#define	SCHEDULE_WORK(tp)	queue_work(unm_workq, tp)
#define	FLUSH_SCHEDULED_WORK()	flush_workqueue(unm_workq)
static struct workqueue_struct *unm_workq;
static void unm_watchdog(unsigned long);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0) */

static char *
nx_errorcode2string(int rcode)
{
        switch (rcode) {
        case NX_RCODE_SUCCESS         : return "Success";
        case NX_RCODE_NO_HOST_MEM     : return "Error: No Host Memory";
        case NX_RCODE_NO_HOST_RESOURCE: return "Error: No Host Resources";
        case NX_RCODE_NO_CARD_CRB     : return "Error: No Card CRB";
        case NX_RCODE_NO_CARD_MEM     : return "Error: No Card Memory";
        case NX_RCODE_NO_CARD_RESOURCE: return "Error: No Card Resources";
        case NX_RCODE_INVALID_ARGS    : return "Error: Invalid Args";
        case NX_RCODE_INVALID_ACTION  : return "Error: Invalid Action";
        case NX_RCODE_INVALID_STATE   : return "Error: Invalid State";
        case NX_RCODE_NOT_SUPPORTED   : return "Error: Not Supported";
        case NX_RCODE_NOT_PERMITTED   : return "Error: Not Permitted";
        case NX_RCODE_NOT_READY       : return "Error: Not Ready";
        case NX_RCODE_DOES_NOT_EXIST  : return "Error: Does Not Exist";
        case NX_RCODE_ALREADY_EXISTS  : return "Error: Already Exists";
        case NX_RCODE_BAD_SIGNATURE   : return "Error: Bad Signature";
        case NX_RCODE_CMD_NOT_IMPL    : return "Error: Cmd Not Implemented";
        case NX_RCODE_CMD_INVALID     : return "Error: Cmd Invalid";
        case NX_RCODE_TIMEOUT         : return "Error: Timed Out";
        case NX_RCODE_CMD_FAILED      : return "Error: Cmd Failed";
        case NX_RCODE_MAX_EXCEEDED    : return "Error: Max Exceeded";
        case NX_RCODE_MAX:
        default:
                return "Error: Unknown code";
        }
}

/*
 * Allocate non-paged, non contiguous memory . Tag can be used for debug
 * purposes.
 */

U32 nx_os_alloc_mem(nx_dev_handle_t handle, void** addr,U32 len, U32 flags,
		    U32 dbg_tag)
{
	*addr = kmalloc(len, flags);
	if (*addr == NULL)
		return -ENOMEM;
	return 0;
}

/*
 * Free non-paged non contiguous memory
*/
U32 nx_os_free_mem(nx_dev_handle_t handle, void *addr, U32 len, U32 flags)
{
	kfree(addr);
	return 0;
}

void nx_os_nic_reg_read_w0(nx_dev_handle_t handle, U32 index, U32 * value)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)handle;
	unm_nic_read_w0(adapter, index, value);
}

void nx_os_nic_reg_write_w0(nx_dev_handle_t handle, U32 index, U32 value)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)handle;
	unm_nic_write_w0(adapter, index, value);
}

void nx_os_nic_reg_read_w1(nx_dev_handle_t handle, U64 off, U32 * value)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)handle;
#if 0
	/* This routine does a read - write and can't safely be used */
	*value = unm_nic_reg_read(adapter, off);
#else
	 adapter->unm_nic_hw_read_wx(adapter, off, value, 4);
//	void *addr;

//	UNM_READ_LOCK(&adapter->adapter_lock);
//	addr = CRB_NORMALIZE(adapter, off);
//	*value = UNM_NIC_PCI_READ_32(addr);
//	UNM_READ_UNLOCK(&adapter->adapter_lock);
#endif
}

void nx_os_nic_reg_write_w1(nx_dev_handle_t handle, U64 off, U32 val)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)handle;
	unm_nic_reg_write(adapter, off, val);
}

/*
 * Allocate non-paged dma memory
 */
U32 nx_os_alloc_dma_mem(nx_dev_handle_t handle, void** vaddr,
			nx_dma_addr_t* paddr, U32 len, U32 flags)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)handle;
	*vaddr = pci_alloc_consistent(adapter->pdev, len, paddr);
	if ((unsigned long long)((*paddr) + len) < adapter->dma_mask) {
		return 0;
	}
	pci_free_consistent(adapter->pdev, len, *vaddr, *paddr);
	paddr = NULL;
	return NX_RCODE_NO_HOST_MEM;
}

void nx_os_free_dma_mem(nx_dev_handle_t handle, void *vaddr,
			nx_dma_addr_t paddr, U32 len, U32 flags)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)handle;
	pci_free_consistent(adapter->pdev, len, vaddr, paddr);
}

U32 nx_os_send_cmd_descs(nx_host_tx_ctx_t *ptx_ctx, nic_request_t *req,
			 U32 nr_elements)
{
	nx_dev_handle_t handle = ptx_ctx->nx_dev->nx_drv_handle;
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)handle;
	U32 rv;

	rv = nx_nic_send_cmd_descs(adapter->netdev,
				   (cmdDescType0_t *) req, nr_elements);

	return rv;
}

nx_rcode_t nx_os_event_wait_setup(nx_dev_handle_t drv_handle,
				  nic_request_t *req, U64 *rsp_word,
				  nx_os_wait_event_t *wait)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)drv_handle;
	uint8_t  comp_id = 0;	
	uint8_t  index   = 0;
	uint64_t bit_map = 0;
	uint64_t i       = 0;

	init_waitqueue_head(&wait->wq);
	wait->active   = 1;
	wait->trigger  = 0;
	wait->rsp_word = rsp_word;

	for (i = 0; i < NX_MAX_COMP_ID; i++) {
		index   = (uint8_t)(i & 0xC0) >> 6; 
		bit_map = (uint64_t)(1 << (i & 0x3F));

		if (!(bit_map & adapter->wait_bit_map[index])) {
			adapter->wait_bit_map[index] |= bit_map;
			comp_id = (uint8_t)i;			
			break;
		}
	}

	if (i >= NX_MAX_COMP_ID) {
		nx_nic_print6(adapter, "%s: completion index exceeds max of 255\n", 
			      __FUNCTION__);
		return NX_RCODE_CMD_FAILED;		
	}

	wait->comp_id = comp_id;
	req->body.cmn.req_hdr.comp_id = comp_id;

	list_add(&wait->list, &adapter->wait_list);

	return NX_RCODE_SUCCESS;
}

nx_rcode_t nx_os_event_wait(nx_dev_handle_t drv_handle,
			    nx_os_wait_event_t *wait, 
			    I32 utimelimit)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)drv_handle;
	U32 rv = NX_RCODE_SUCCESS;
	uint8_t  index   = 0;
	uint64_t bit_map = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	DEFINE_WAIT(wq_entry);
#else
	DECLARE_WAITQUEUE(wq_entry, current);
#endif

#ifdef ESX_3X
	init_waitqueue_entry(&wq_entry, current);
#endif

	while (wait->trigger == 0) {
		if (utimelimit <= 0) {
			nx_nic_print6(adapter, "%s: timelimit up\n", __FUNCTION__);
			rv = NX_RCODE_TIMEOUT;
			break;
		}
		PREPARE_TO_WAIT(&wait->wq, &wq_entry, TASK_INTERRUPTIBLE);
		/* schedule out for 100ms */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,7)
		msleep(100);
#else
		SCHEDULE_TIMEOUT(&wait->wq, (HZ / 10), NULL);
#endif
		utimelimit -= (100000);
	}

	index   = (wait->comp_id & 0xC0) >> 6; 
	bit_map = (uint64_t)(1 << (((uint64_t)wait->comp_id) & 0x3F));
	adapter->wait_bit_map[index] &= ~bit_map; 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	finish_wait(&wait->wq, &wq_entry);
#else
	current->state = TASK_RUNNING;
	remove_wait_queue(&wait->wq, &wq_entry);
#endif
	list_del(&wait->list);
	
	return rv;
}

nx_rcode_t nx_os_event_wakeup_on_response(nx_dev_handle_t drv_handle,
					  nic_response_t *rsp)
{
	struct unm_adapter_s *adapter   = (struct unm_adapter_s *)drv_handle;
	nx_os_wait_event_t   *wait      = NULL;
	struct list_head     *ptr       = NULL;
	U8  compid   = rsp->rsp_hdr.nic.compid;
	U64 rsp_word = rsp->body.word;
	int found    = 0;	

	if (rsp->rsp_hdr.nic.opcode == NX_NIC_C2H_OPCODE_LRO_DELETE_RESPONSE
		|| rsp->rsp_hdr.nic.opcode == NX_NIC_C2H_OPCODE_LRO_ADD_FAILURE_RESPONSE) {
		nx_handle_lro_response(drv_handle, rsp);
		return NX_RCODE_SUCCESS;
	}

	nx_nic_print6(adapter, "%s: 0x%x: %d 0x%llx\n",
		      __FUNCTION__, rsp->rsp_hdr.nic.opcode,
		      rsp->rsp_hdr.nic.compid, rsp_word);

	list_for_each(ptr, &adapter->wait_list) {
		wait = list_entry(ptr, nx_os_wait_event_t, list);
		if (wait->comp_id == compid) {
			found = 1;
			break;
		}
	}

	if (!found) {
		nx_nic_print4(adapter, "%s: entry with comp_id = %d not found\n", 
			      __FUNCTION__, compid);
		return NX_RCODE_CMD_FAILED;
	}

	if (wait->active != 1) {
		nx_nic_print4(adapter, "%s: 0x%x: id %d not active\n",
			      __FUNCTION__, rsp->rsp_hdr.nic.opcode, compid);
		return NX_RCODE_CMD_FAILED;
	}

	if (wait->rsp_word != NULL) {
		*(wait->rsp_word) = rsp_word;
	}

	wait->trigger = 1;
	wait->active  = 0;

#ifdef ESX_3X
	vmk_thread_wakeup(&wait->wq);
#else
	wake_up_interruptible(&wait->wq);
#endif

	return NX_RCODE_SUCCESS;
}

inline void unm_nic_update_cmd_producer(struct unm_adapter_s *adapter,
					uint32_t crb_producer)
{
    int data = crb_producer;


	if (adapter->crb_addr_cmd_producer)
        adapter->unm_nic_hw_write_wx(adapter, adapter->crb_addr_cmd_producer, &data, 4);
	return;

}

inline void unm_nic_update_cmd_consumer(struct unm_adapter_s *adapter,
					uint32_t crb_consumer)
{
    int data = crb_consumer;

	switch (adapter->portnum) {
	case 0:
		adapter->unm_nic_hw_write_wx(adapter, CRB_CMD_CONSUMER_OFFSET,
						 &data, 4);
        return;
	case 1:
		adapter->unm_nic_hw_write_wx(adapter, CRB_CMD_CONSUMER_OFFSET_1,
						 &data, 4);
		return;
	case 2:
		adapter->unm_nic_hw_write_wx(adapter, CRB_CMD_CONSUMER_OFFSET_2,
						 &data, 4);
		return;
	case 3:
		adapter->unm_nic_hw_write_wx(adapter, CRB_CMD_CONSUMER_OFFSET_3,
						 &data, 4);
		return;
	default:
		nx_nic_print3(adapter,
				"Unable to update CRB_CMD_PRODUCER_OFFSET "
				"for invalid PCI function id %d\n",
				 adapter->portnum);
		return;
        }
}

/*
 * Checks the passed in module parameters for validity else it sets them to
 * default sane values.
 */
static void nx_verify_module_params(void)
{
	if (rdesc_1g < NX_MIN_DRIVER_RDS_SIZE ||
	    rdesc_1g > NX_MAX_SUPPORTED_RDS_SIZE ||
	    (rdesc_1g & (rdesc_1g - 1))) {
		nx_nic_print5(NULL, "Invalid module param rdesc_1g[%d]. "
			      "Setting it to %d\n",
			      rdesc_1g, NX_DEFAULT_RDS_SIZE_1G);
		rdesc_1g = NX_DEFAULT_RDS_SIZE_1G;
	}

	if (rdesc_10g < NX_MIN_DRIVER_RDS_SIZE ||
	    rdesc_10g > NX_MAX_SUPPORTED_RDS_SIZE ||
	    (rdesc_10g & (rdesc_10g - 1))) {
		nx_nic_print5(NULL, "Invalid module param rdesc_10g[%d]. "
			      "Setting it to %d\n",
			      rdesc_10g, NX_DEFAULT_RDS_SIZE);
		rdesc_10g = NX_DEFAULT_RDS_SIZE;
	}

	if (jumbo_desc < NX_MIN_DRIVER_RDS_SIZE ||
	    jumbo_desc > NX_MAX_SUPPORTED_JUMBO_RDS_SIZE ||
	    (jumbo_desc & (jumbo_desc - 1))) {
		nx_nic_print5(NULL, "Invalid module param jumbo_desc[%d]. "
			      "Setting it to %d\n",
			      jumbo_desc, NX_DEFAULT_JUMBO_RDS_SIZE);
		jumbo_desc = NX_DEFAULT_JUMBO_RDS_SIZE;
	}
}

/*
 *
 */
static void unm_check_options(unm_adapter *adapter)
{
	GET_BRD_FWTYPE_BY_BRDTYPE(adapter->ahw.boardcfg.board_type,
				  adapter->fwtype);

	switch (adapter->ahw.boardcfg.board_type) {
	case UNM_BRDTYPE_P3_XG_LOM:
	case UNM_BRDTYPE_P3_HMEZ:
	case UNM_BRDTYPE_P2_SB31_10G:
	case UNM_BRDTYPE_P2_SB31_10G_CX4:

	case UNM_BRDTYPE_P3_10G_CX4:
	case UNM_BRDTYPE_P3_10G_CX4_LP:
	case UNM_BRDTYPE_P3_IMEZ:
	case UNM_BRDTYPE_P3_10G_SFP_PLUS:
	case UNM_BRDTYPE_P3_10G_XFP:
	case UNM_BRDTYPE_P3_10000_BASE_T:
		adapter->msix_supported = 1;
		adapter->max_possible_rss_rings = CARD_SIZED_MAX_RSS_RINGS;
		adapter->MaxRxDescCount = rdesc_10g;
		break;

	case UNM_BRDTYPE_P2_SB31_10G_IMEZ:
	case UNM_BRDTYPE_P2_SB31_10G_HMEZ:
		adapter->msix_supported = 0;
		adapter->max_possible_rss_rings = 1;
		adapter->MaxRxDescCount = rdesc_10g;
		break;

	case UNM_BRDTYPE_P3_REF_QG:
	case UNM_BRDTYPE_P3_4_GB:
	case UNM_BRDTYPE_P3_4_GB_MM:
                adapter->msix_supported = 1;
                adapter->max_possible_rss_rings = 1;
                adapter->MaxRxDescCount = rdesc_1g;
		break;

	case UNM_BRDTYPE_P2_SB35_4G:
	case UNM_BRDTYPE_P2_SB31_2G:
		adapter->msix_supported = 0;
		adapter->max_possible_rss_rings = 1;
		adapter->MaxRxDescCount = rdesc_1g;
		break;
	case UNM_BRDTYPE_P3_10G_TROOPER:
		if (adapter->portnum < 2) {
			adapter->msix_supported = 1;
			adapter->max_possible_rss_rings = 
				CARD_SIZED_MAX_RSS_RINGS;
			adapter->MaxRxDescCount = rdesc_10g;
		} else {
			adapter->msix_supported = 1;
			adapter->max_possible_rss_rings = 1;
			adapter->MaxRxDescCount = rdesc_1g;
		}
		break;
	default:
		adapter->msix_supported = 0;
		adapter->max_possible_rss_rings = 1;
		adapter->MaxRxDescCount = rdesc_1g;

		nx_nic_print4(NULL, "Unknown board type(0x%x)\n",
			      adapter->ahw.boardcfg.board_type);
		break;
	}			// end of switch

	if (tx_desc >= 256 && tx_desc <= MAX_CMD_DESCRIPTORS &&
	    !(tx_desc & (tx_desc - 1))) {
		adapter->MaxTxDescCount = tx_desc;
	} else {
		nx_nic_print5(NULL, "Ignoring module param tx_desc. "
			      "Setting it to %d\n",
			      MAX_CMD_DESCRIPTORS_HOST);
		adapter->MaxTxDescCount = MAX_CMD_DESCRIPTORS_HOST;
	}

	adapter->MaxJumboRxDescCount = jumbo_desc;
	adapter->MaxLroRxDescCount = lro_desc;

	nx_nic_print6(NULL, "Maximum Rx Descriptor count: %d\n",
		      adapter->MaxRxDescCount);
	nx_nic_print6(NULL, "Maximum Tx Descriptor count: %d\n",
		      adapter->MaxTxDescCount);
	nx_nic_print6(NULL, "Maximum Jumbo Descriptor count: %d\n",
		      adapter->MaxJumboRxDescCount);
	nx_nic_print6(NULL, "Maximum LRO Descriptor count: %d\n",
		      adapter->MaxLroRxDescCount);
	return;
}

static inline void unm_tx_csum(struct unm_adapter_s *adapter,
                               cmdDescType0_t *desc,
                               struct sk_buff *skb)
{
#ifdef UNM_NIC_HW_CSUM
	if (skb->ip_summed == CHECKSUM_HW) {
		if (skb->protocol == htons(ETH_P_IP)) {	/* IPv4  */
			if (IP_HDR(skb)->protocol == IPPROTO_TCP) {
				desc->opcode = TX_TCP_PKT;
			} else if (IP_HDR(skb)->protocol == IPPROTO_UDP) {
				desc->opcode = TX_UDP_PKT;
			} else {
				return;
			}
#ifndef ESX_3X
		} else if (skb->protocol == htons(ETH_P_IPV6)) {	/* IPv6 */
			if (IPV6_HDR(skb)->nexthdr == IPPROTO_TCP) {
				desc->opcode = TX_TCPV6_PKT;
			} else if (IPV6_HDR(skb)->nexthdr == IPPROTO_UDP) {
				desc->opcode = TX_UDPV6_PKT;
			} else {
				return;
			}
#endif
		}
	}
	desc->tcpHdrOffset = TCP_HDR_OFFSET(skb);
	desc->ipHdrOffset = IP_HDR_OFFSET(skb);
#endif /* UNM_NIC_HW_CSUM */
}

#ifdef UNM_NETIF_F_TSO

#if (defined(USE_GSO_SIZE) || defined(ESX_4X) )
#define TSO_SIZE(x)   ((x)->gso_size)
#else
#define TSO_SIZE(x)   ((x)->tso_size)
#endif

static inline void unm_tso_check(struct unm_adapter_s *adapter,
				 cmdDescType0_t * desc,
				 uint32_t tagged,
                                 struct sk_buff *skb)
{
#ifndef UNM_NIC_HW_CSUM
#error "Hardware checksum must be enabled while running with LSO."
#endif

	desc->totalHdrLength = sizeof(struct ethhdr) +
	    (tagged * sizeof(struct vlan_hdr)) +
                           (NW_HDR_SIZE) +
                           (TCP_HDR(skb)->doff * sizeof(u32));

	if (skb->protocol == htons(ETH_P_IP))
		desc->opcode = TX_TCP_LSO;
#ifndef ESX_3X
	else if (skb->protocol == htons(ETH_P_IPV6))
		desc->opcode = TX_TCP_LSO6;
#endif

	desc->tcpHdrOffset = TCP_HDR_OFFSET(skb);
	desc->ipHdrOffset = IP_HDR_OFFSET(skb);
}
#endif /* UNM_NETIF_F_TSO */

/*  PCI Device ID Table  */
#define NETXEN_PCI_ID(device_id)   PCI_DEVICE(PCI_VENDOR_ID_NX, device_id)

static struct pci_device_id unm_pci_tbl[] __devinitdata = {
	{NETXEN_PCI_ID(PCI_DEVICE_ID_NX_QG)},
	{NETXEN_PCI_ID(PCI_DEVICE_ID_NX_XG)},
	{NETXEN_PCI_ID(PCI_DEVICE_ID_NX_CX4)},
	{NETXEN_PCI_ID(PCI_DEVICE_ID_NX_IMEZ)},
	{NETXEN_PCI_ID(PCI_DEVICE_ID_NX_HMEZ)},
	{NETXEN_PCI_ID(PCI_DEVICE_ID_NX_IMEZ_DUP)},
	{NETXEN_PCI_ID(PCI_DEVICE_ID_NX_HMEZ_DUP)},
	{NETXEN_PCI_ID(PCI_DEVICE_ID_NX_P3_XG)},
	{0,}
};

MODULE_DEVICE_TABLE(pci, unm_pci_tbl);

#define SUCCESS 0
static int is_flash_supported(struct unm_adapter_s *adapter)
{
	int locs[] = { 0, 0x4, 0x100, 0x4000, 0x4128 };
	int addr, val01, val02, i, j;

	/* if the flash size is not 4Mb, make huge war cry and die */
	for (j = 1; j < 4; j++) {
		addr = j * 0x100000;
		for (i = 0; i < (sizeof(locs) / sizeof(locs[0])); i++) {
			if (rom_fast_read(adapter, locs[i], &val01) == 0 &&
			    rom_fast_read(adapter, (addr + locs[i]),
					  &val02) == 0) {
				if (val01 == val02) {
					return -1;
				}
			} else
				return -1;
		}
	}

	return 0;
}

static int unm_get_flash_block(struct unm_adapter_s *adapter, int base,
			       int size, uint32_t * buf)
{
	int i, addr;
	uint32_t *ptr32;

	addr = base;
	ptr32 = buf;
	for (i = 0; i < size / sizeof(uint32_t); i++) {
		if (rom_fast_read(adapter, addr, ptr32) == -1) {
			return -1;
		}
		ptr32++;
		addr += sizeof(uint32_t);
	}
	if ((char *)buf + size > (char *)ptr32) {
		uint32_t local;

		if (rom_fast_read(adapter, addr, &local) == -1) {
			return -1;
		}
		memcpy(ptr32, &local, (char *)buf + size - (char *)ptr32);
	}

	return 0;
}

static int get_flash_mac_addr(struct unm_adapter_s *adapter, uint64_t mac[])
{
	uint32_t *pmac = (uint32_t *) & mac[0];
         if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
               uint32_t temp, crbaddr;
               uint16_t *pmac16 = (uint16_t *)pmac;

               // FOR P3, read from CAM RAM

               int pci_func= adapter->ahw.pci_func;
               pmac16 += (4*pci_func);
               crbaddr = CRB_MAC_BLOCK_START +
                               (4 * ((pci_func/2) * 3))+
                               (4 * (pci_func & 1));

               adapter->unm_nic_hw_read_wx(adapter, crbaddr, &temp, 4);
               if (pci_func & 1) {
                       *pmac16++ = (temp >> 16);
                        adapter->unm_nic_hw_read_wx(adapter, crbaddr+4, &temp, 4);
                       *pmac16++ = (temp & 0xffff);
                       *pmac16++ = (temp >> 16);
                       *pmac16=0;
               } else {
                       *pmac16++ = (temp & 0xffff);
                       *pmac16++ = (temp >> 16);
                        adapter->unm_nic_hw_read_wx(adapter, crbaddr+4, &temp, 4);
                       *pmac16++ = (temp & 0xffff);
                       *pmac16=0;
               }
               return 0;
       }


	if (unm_get_flash_block(adapter,
				USER_START + offsetof(unm_user_info_t,
						      mac_addr),
				FLASH_NUM_PORTS * sizeof(U64), pmac) == -1) {
		return -1;
	}
	if (*mac == ~0ULL) {
		if (unm_get_flash_block(adapter,
					USER_START_OLD +
					offsetof(unm_old_user_info_t, mac_addr),
					FLASH_NUM_PORTS * sizeof(U64),
					pmac) == -1) {
			return -1;
		}
		if (*mac == ~0ULL) {
			return -1;
		}
	}

	return 0;
}

/*
 * Initialize buffers required for the adapter in pegnet_nic case.
 */
static int initialize_dummy_dma(unm_adapter * adapter)
{
        uint64_t        addr;
        uint32_t        hi;
        uint32_t        lo;
        uint32_t        temp;

        adapter->dummy_dma.addr =
                pci_alloc_consistent(adapter->ahw.pdev,
                                     UNM_HOST_DUMMY_DMA_SIZE,
                                     &adapter->dummy_dma.phys_addr);
        if (adapter->dummy_dma.addr == NULL) {
                nx_nic_print3(NULL, "ERROR: Could not allocate dummy "
			      "DMA memory\n");
                return (-ENOMEM);
        }

	addr = (uint64_t) adapter->dummy_dma.phys_addr;
	hi = (addr >> 32) & 0xffffffff;
	lo = addr & 0xffffffff;

	read_lock(&adapter->adapter_lock);
        adapter->unm_nic_hw_write_wx(adapter, CRB_HOST_DUMMY_BUF_ADDR_HI, &hi, 4);
        adapter->unm_nic_hw_write_wx(adapter, CRB_HOST_DUMMY_BUF_ADDR_LO, &lo, 4);

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
        temp = DUMMY_BUF_INIT;
        adapter->unm_nic_hw_write_wx(adapter, CRB_HOST_DUMMY_BUF, &temp, 4);
	}

	read_unlock(&adapter->adapter_lock);

	return (0);
}

/*
 * Free buffers for the offload part from the adapter.
 */
void free_adapter_offload(unm_adapter * adapter)
{
	if (adapter->dummy_dma.addr) {
		pci_free_consistent(adapter->ahw.pdev, UNM_HOST_DUMMY_DMA_SIZE,
				    adapter->dummy_dma.addr,
				    adapter->dummy_dma.phys_addr);
		adapter->dummy_dma.addr = NULL;
	}
}

#define addr_needs_mapping(adapter, phys) \
        ((phys) & (~adapter->dma_mask))

#ifdef CONFIG_XEN

static inline int
in_dma_range(struct unm_adapter_s *adapter, dma_addr_t addr, unsigned int len)
{
	dma_addr_t last = addr + len - 1;

	if ((addr & ~PAGE_MASK) + len > PAGE_SIZE) {
		return 0;
	}

	return !addr_needs_mapping(adapter, last);
}

#elif !defined(ESX_3X)

static inline int
in_dma_range(struct unm_adapter_s *adapter, dma_addr_t addr, unsigned int len)
{
	dma_addr_t last = addr + len - 1;

	return !addr_needs_mapping(adapter, last);
}

#endif

#ifdef ESX_3X

static inline int
try_map_skb_data(struct unm_adapter_s *adapter, struct sk_buff *skb,
		 size_t size, int direction, dma_addr_t * dma)
{
	*dma = skb->headMA;
	return 0;
}

static inline int
try_map_frag_page(struct unm_adapter_s *adapter,
		  struct page *page, unsigned long offset, size_t size,
		  int direction, dma_addr_t * dma)
{
	*dma = page_to_phys(page) + offset;
	return 0;
}

#elif defined (CONFIG_XEN)

static inline int
try_map_skb_data(struct unm_adapter_s *adapter, struct sk_buff *skb,
		 size_t size, int direction, dma_addr_t * dma)
{
	int bounce;
	struct pci_dev *hwdev = adapter->pdev;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	if (!in_dma_range(adapter, virt_to_bus(skb->data), size)) {
		bounce = 1;
	} else 
#endif
	{
		*dma = pci_map_single(hwdev, skb->data, size, direction);
		bounce = 0;
	}

	return bounce;
}

static inline int
try_map_frag_page(struct unm_adapter_s *adapter,
		  struct page *page, unsigned long offset, size_t size,
		  int direction, dma_addr_t * dma)
{
	int bounce;
	struct pci_dev *hwdev = adapter->pdev;

	if (!in_dma_range(adapter, page_to_bus(page) + offset, size)) {
		bounce = 1;
	} else {
		*dma = pci_map_page(hwdev, page, offset, size, direction);;
		bounce = 0;
	}

	return bounce;
}

#else /* NATIVE LINUX */

static inline int
try_map_skb_data(struct unm_adapter_s *adapter, struct sk_buff *skb,
		 size_t size, int direction, dma_addr_t * dma)
{
	struct pci_dev *hwdev = adapter->pdev;
	dma_addr_t dma_temp;

	dma_temp = pci_map_single(hwdev, skb->data, size, direction);

	*dma = dma_temp;

	return (0);
}

static inline int
try_map_frag_page(struct unm_adapter_s *adapter,
		  struct page *page, unsigned long offset, size_t size,
		  int direction, dma_addr_t * dma)
{
	struct pci_dev *hwdev = adapter->pdev;
	dma_addr_t dma_temp;

	dma_temp = pci_map_page(hwdev, page, offset, size, direction);

	*dma = dma_temp;

	return (0);
}

#endif /* ESX */

#define	ADAPTER_LIST_SIZE	12
int unm_cards_found;

static void nx_reset_msix_bit(struct pci_dev *pdev)
{
#ifdef	UNM_HWBUG_9_WORKAROUND
	u32 control = 0x00000000;
	int pos;

	pos = pci_find_capability(pdev, PCI_CAP_ID_MSIX);
	pci_write_config_dword(pdev, pos, control);
#endif
}

static void cleanup_adapter(struct unm_adapter_s *adapter)
{
	struct pci_dev *pdev;
	struct net_device *netdev;

	if (!adapter) {
		return;
	}

	if (adapter->testCtx.tx_user_packet_data != NULL) {
		kfree(adapter->testCtx.tx_user_packet_data);
	}
	pdev = adapter->ahw.pdev;
	netdev = adapter->netdev;

	if (pdev == NULL || netdev == NULL) {
		return;
	}
#ifndef ESX_3X
	unm_cleanup_lro(adapter);
#endif
	unm_cleanup_proc_entries(adapter);
#if defined(CONFIG_PCI_MSI)
	if ((adapter->flags & UNM_NIC_MSIX_ENABLED)) {

		pci_disable_msix(pdev);
		nx_reset_msix_bit(pdev);
	} else if ((adapter->flags & UNM_NIC_MSI_ENABLED)) {
		pci_disable_msi(pdev);
	}
#endif

        nx_os_dev_free(adapter->nx_dev);

	if (adapter->cmd_buf_arr != NULL) {
		vfree(adapter->cmd_buf_arr);
		adapter->cmd_buf_arr = NULL;
	}

	if (adapter->ahw.pci_base0) {
		iounmap((uint8_t *) adapter->ahw.pci_base0);
		adapter->ahw.pci_base0 = 0UL;
	}
	if (adapter->ahw.pci_base1) {
		iounmap((uint8_t *) adapter->ahw.pci_base1);
		adapter->ahw.pci_base1 = 0UL;
	}
	if (adapter->ahw.pci_base2) {
		iounmap((uint8_t *) adapter->ahw.pci_base2);
		adapter->ahw.pci_base2 = 0UL;
	}
	if (adapter->ahw.db_base) {
		iounmap((uint8_t *) adapter->ahw.db_base);
		adapter->ahw.db_base = 0UL;
	}

	unm_pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
#ifdef OLD_KERNEL
	kfree(netdev);
#else
	free_netdev(netdev);
#endif
}

/*
 *
 */
static void nx_p2_start_bootloader(struct unm_adapter_s *adapter)
{
	int timeout = 0;
	u32 val = 0;

	UNM_NIC_PCI_WRITE_32(UNM_BDINFO_MAGIC,
			     CRB_NORMALIZE(adapter, UNM_CAM_RAM(0x1fc)));
	UNM_NIC_PCI_WRITE_32(1, CRB_NORMALIZE(adapter,
					      UNM_ROMUSB_GLB_PEGTUNE_DONE));
	/*
	 * bootloader0 writes zero to UNM_CAM_RAM(0x1fc) before calling
	 * bootloader1
	 */
	if (fw_load == 1) {
		do {
			val = UNM_NIC_PCI_READ_32(CRB_NORMALIZE(adapter,
								UNM_CAM_RAM
								(0x1fc)));
			if (timeout > 10000) {
				nx_nic_print3(adapter, "The bootloader did not"
					      " increment the CAM_RAM(0x1fc)"
					      " register\n");
				break;
			}
			timeout++;
			nx_msleep(1);
		} while (val == UNM_BDINFO_MAGIC);
		/*Halt the bootloader now */
		UNM_NIC_PCI_WRITE_32(1, CRB_NORMALIZE(adapter,
						      UNM_ROMUSB_GLB_CAS_RST));
	}
}

/*
 *
 */
int check_hw_init(struct unm_adapter_s *adapter)
{
	u32 val = 0;
	int ret = 0;

    adapter->unm_nic_hw_read_wx(adapter, UNM_CAM_RAM(0x1fc), &val, 4);
	nx_nic_print7(adapter, "read 0x%08x for init reg.\n", val);
	if (val == 0x55555555) {
		/* This is the first boot after power up */
        adapter->unm_nic_hw_read_wx(adapter, UNM_ROMUSB_GLB_SW_RESET, &val, 4);
		nx_nic_print7(adapter, "read 0x%08x for reset reg.\n", val);
		if (val != 0x80000f) {
			ret = -1;
		}

		if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
			nx_p2_start_bootloader(adapter);
		}
	}
	return ret;
}

/* nx_set_dma_mask()
 * Set dma mask depending upon kernel type and device capability
 */
static int nx_set_dma_mask(struct unm_adapter_s *adapter, uint8_t revision_id)
{
	struct pci_dev *pdev = adapter->ahw.pdev;
	int err;
	uint64_t mask;

#ifndef CONFIG_IA64
	if (revision_id >= NX_P3_B0) {
		adapter->dma_mask = DMA_39BIT_MASK;
		mask = DMA_39BIT_MASK;
//              adapter->dma_mask = DMA_64BIT_MASK;
//              mask = DMA_64BIT_MASK;
	} else if (revision_id == NX_P3_A2) {
		adapter->dma_mask = DMA_39BIT_MASK;
		mask = DMA_39BIT_MASK;
	} else if (revision_id == NX_P2_C1) {
		adapter->dma_mask = DMA_35BIT_MASK;
		mask = DMA_64BIT_MASK;
	} else {
		adapter->dma_mask = DMA_32BIT_MASK;
		mask = DMA_32BIT_MASK;
		goto set_32_bit_mask;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
	/*
	 * Consistent DMA mask is set to 32 bit because it cannot be set to
	 * 35 bits. For P3 also leave it at 32 bits for now. Only the rings
	 * come off this pool.
	 */
	if (pci_set_dma_mask(pdev, mask) == 0 &&
	    pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK) == 0)
#else
	if (pci_set_dma_mask(pdev, mask) == 0)
#endif
	{
#ifdef ESX
		if (revision_id == NX_P2_C0)
			adapter->pci_using_dac = 0;
		else
#endif
			adapter->pci_using_dac = 1;

		return (0);
	}
#else /* CONFIG_IA64 */
	adapter->dma_mask = DMA_32BIT_MASK;
#endif /* CONFIG_IA64 */

      set_32_bit_mask:
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
	if ((err = pci_set_dma_mask(pdev, PCI_DMA_32BIT)) ||
	    (err = pci_set_consistent_dma_mask(pdev, PCI_DMA_32BIT)))
#else
	if ((err = pci_set_dma_mask(pdev, PCI_DMA_32BIT)))
#endif
	{
		nx_nic_print3(adapter, "No usable DMA configuration, "
			      "aborting:%d\n", err);
                return err;
        }

	adapter->pci_using_dac = 0;
	return (0);
}

#ifdef UNM_NIC_SNMP
#ifdef UNM_NIC_SNMP_TRAP
void set_temperature_user_pid(unsigned int);
void unm_nic_send_snmp_trap(unsigned char);
#endif
#endif

int nx_get_lic_finger_print(struct unm_adapter_s *adapter, nx_finger_print_t *nx_lic_finger_print) {

	nic_request_t   req;
	nx_get_finger_print_request_t *lic_req;
	int          rv = 0;
	struct timeval tv;
	nx_os_wait_event_t swait;

	memset(&req, 0, sizeof(req));
	memset(adapter->nx_lic_dma.addr, 0, sizeof(nx_finger_print_t));
	req.opcode = NX_NIC_HOST_REQUEST;
	req.qmsg_type = UNM_MSGTYPE_NIC_REQUEST;
	req.body.cmn.req_hdr.opcode = NX_NIC_H2C_OPCODE_GET_FINGER_PRINT_REQUEST;
	req.body.cmn.req_hdr.ctxid = adapter->portnum;
	lic_req = (nx_get_finger_print_request_t *)&req.body;
	lic_req->ring_ctx = adapter->portnum;

	lic_req->dma_to_addr = (__uint64_t)(adapter->nx_lic_dma.phys_addr);
	lic_req->dma_size = (__uint32_t)(sizeof(nx_finger_print_t));
	do_gettimeofday(&tv); // Get Latest Time
	lic_req->nx_time = tv.tv_sec;

	/* Here req.body.cmn.req_hdr.comp_i will be set */
	rv = nx_os_event_wait_setup(adapter, &req, NULL, &swait);
	if(rv) {
		nx_nic_print3(adapter, "os event setup failed: \n");
		return rv;
	}

	rv = nx_nic_send_cmd_descs(adapter->netdev, (cmdDescType0_t *)&req, 1);
	if(rv) {
		nx_nic_print3(adapter, "Sending finger_print request to FW failed: %d\n", rv);
		return rv;
	}
	rv = nx_os_event_wait(adapter, &swait, 5000);
	if(rv) {
		nx_nic_print3(adapter, "nx os event wait failed\n");
		return rv;	
	}
	memcpy(nx_lic_finger_print, adapter->nx_lic_dma.addr,
			sizeof(nx_finger_print_t));
	return (rv);
}

int nx_install_license(struct unm_adapter_s *adapter) {

	nic_request_t   req;
	nx_install_license_request_t *lic_req;
	int          rv = 0;

	memset(&req, 0, sizeof(req));
	req.opcode = NX_NIC_HOST_REQUEST;
	req.qmsg_type = UNM_MSGTYPE_NIC_REQUEST;
	req.body.cmn.req_hdr.opcode = NX_NIC_H2C_OPCODE_INSTALL_LICENSE_REQUEST;
	req.body.cmn.req_hdr.ctxid = adapter->portnum;
	lic_req = (nx_install_license_request_t *)&req.body;
	lic_req->ring_ctx = adapter->portnum;
	lic_req->dma_to_addr = (__uint64_t)(adapter->nx_lic_dma.phys_addr);
	lic_req->dma_size = (__uint32_t)(sizeof(nx_install_license_t));
	
	rv = nx_nic_send_cmd_descs(adapter->netdev, (cmdDescType0_t *)&req, 1);

	if(rv) {
		nx_nic_print3(adapter, "Sending Install License request to FW failed: %d\n", rv);
		return rv;
	}
	return (rv);
}

int nx_get_capabilty_request(struct unm_adapter_s *adapter, nx_license_capabilities_t *nx_lic_capabilities) {

	nic_request_t   req;
	nx_get_license_capability_request_t *lic_req;
	int          rv = 0;
	nx_os_wait_event_t swait;

	memset(&req, 0, sizeof(req));
	memset(adapter->nx_lic_dma.addr, 0, sizeof(nx_license_capabilities_t));

	req.opcode = NX_NIC_HOST_REQUEST;
	req.qmsg_type = UNM_MSGTYPE_NIC_REQUEST;
	req.body.cmn.req_hdr.opcode = NX_NIC_H2C_OPCODE_GET_LICENSE_CAPABILITY_REQUEST;
	req.body.cmn.req_hdr.ctxid = adapter->portnum;
	lic_req = (nx_get_license_capability_request_t *)&req.body;
	lic_req->ring_ctx = adapter->portnum;

	lic_req->dma_to_addr = (__uint64_t)(adapter->nx_lic_dma.phys_addr);
	lic_req->dma_size = (__uint32_t)(sizeof(nx_license_capabilities_t));

	/* Here req.body.cmn.req_hdr.comp_i will be set */
	rv = nx_os_event_wait_setup(adapter, &req, NULL, &swait);
	if(rv) {
		nx_nic_print3(adapter,"os event setup failed: \n");
		return rv;
	}

	rv = nx_nic_send_cmd_descs(adapter->netdev, (cmdDescType0_t *)&req, 1);
	if(rv) {
		nx_nic_print3(adapter, "Sending get_capabilty request to FW failed: %d\n", rv);
		return rv;
	}
	rv = nx_os_event_wait(adapter, &swait, 5000);
	if(rv) {
		nx_nic_print3(adapter, "nx os event wait failed\n");
		return rv;
	}
	memcpy(nx_lic_capabilities, adapter->nx_lic_dma.addr, sizeof(nx_license_capabilities_t));
	return (rv);
}

static int
unm_nic_fill_adapter_macaddr_from_flash(struct unm_adapter_s *adapter)
{
	int i;
	unsigned char *p;
	uint64_t mac_addr[8 + 1];

	if (is_flash_supported(adapter) != 0)
		return -1;

	if (get_flash_mac_addr(adapter, mac_addr) != 0) {
		return -1;
	}

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		p = (unsigned char *)&mac_addr[adapter->ahw.pci_func];
	} else {
		p = (unsigned char *)&mac_addr[adapter->portnum];
	}

	for (i = 0; i < 6; ++i) {
		adapter->mac_addr[i] = p[5 - i];
	}

        if (!is_valid_ether_addr(adapter->mac_addr)) {
                nx_nic_print3(adapter, "Bad MAC address "
			      "%02x:%02x:%02x:%02x:%02x:%02x.\n",
			      adapter->mac_addr[0], adapter->mac_addr[1],
			      adapter->mac_addr[2], adapter->mac_addr[3],
			      adapter->mac_addr[4], adapter->mac_addr[5]);
                return -1;
        }

	return 0;
}

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,8)) || defined(ESX_3X))
/*
 * Initialize the msix entries.
 */
static void init_msix_entries(struct unm_adapter_s *adapter)
{
	int i;

	for (i = 0; i < MSIX_ENTRIES_PER_ADAPTER; i++) {
		adapter->msix_entries[i].entry = i;
	}
}
#endif

static inline int unm_pci_region_offset(struct pci_dev *pdev, int region)
{
	unsigned long val;
	u32 control;

	switch (region) {
	case 0:
		val = 0;
		break;
	case 1:
		pci_read_config_dword(pdev, UNM_PCI_REG_MSIX_TBL, &control);
		val = control + UNM_MSIX_TBL_SPACE;
		break;
	}
	return val;
}

static inline int unm_pci_region_len(struct pci_dev *pdev, int region)
{
	unsigned long val;
	u32 control;
	switch (region) {
	case 0:
		pci_read_config_dword(pdev, UNM_PCI_REG_MSIX_TBL, &control);
		val = control;
		break;
	case 1:
		val = pci_resource_len(pdev, 0) -
		    unm_pci_region_offset(pdev, 1);
		break;
	}
	return val;
}

static int unm_pci_request_regions(struct pci_dev *pdev, char *res_name)
{
	struct resource *res;
	unsigned int len;

	/*
	 * In P3 these memory regions might change and need to be fixed.
	 */
	len = pci_resource_len(pdev, 0);
	if (len <= NX_MSIX_MEM_REGION_THRESHOLD || !use_msi_x ||
	    unm_pci_region_len(pdev, 0) == 0 ||
	    unm_pci_region_len(pdev, 1) == 0) {
		res = request_mem_region(pci_resource_start(pdev, 0),
					 len, res_name);
		goto done;
	}

	/* In case of MSI-X  pci_request_regions() is not useful, because
	   pci_enable_msix() tries to reserve part of card's memory space for
	   MSI-X table entries and fails due to conflict, since nx_nic module
	   owns entire region.
	   soln : request region(s) leaving area needed for MSI-X alone */
	res = request_mem_region(pci_resource_start(pdev, 0) +
				 unm_pci_region_offset(pdev, 0),
				 unm_pci_region_len(pdev, 0), res_name);
	if (res == NULL) {
		goto done;
	}
	res = request_mem_region(pci_resource_start(pdev, 0) +
				 unm_pci_region_offset(pdev, 1),
				 unm_pci_region_len(pdev, 1), res_name);

	if (res == NULL) {
		release_mem_region(pci_resource_start(pdev, 0) +
				   unm_pci_region_offset(pdev, 0),
				   unm_pci_region_len(pdev, 0));
	}
      done:
	return (res == NULL);
}

static void unm_pci_release_regions(struct pci_dev *pdev)
{
	unsigned int len;

	len = pci_resource_len(pdev, 0);
	if (len <= NX_MSIX_MEM_REGION_THRESHOLD || !use_msi_x ||
	    unm_pci_region_len(pdev, 0) == 0 ||
	    unm_pci_region_len(pdev, 1) == 0) {
		release_mem_region(pci_resource_start(pdev, 0), len);
		return;
	}

	release_mem_region(pci_resource_start(pdev, 0) +
			   unm_pci_region_offset(pdev, 0),
			   unm_pci_region_len(pdev, 0));
	release_mem_region(pci_resource_start(pdev, 0) +
			   unm_pci_region_offset(pdev, 1),
			   unm_pci_region_len(pdev, 1));
}

/*
 * Linux system will invoke this after identifying the vendor ID and device Id
 * in the pci_tbl where this module will search for UNM vendor and device ID
 * for quad port adapter.
 */
static int __devinit unm_nic_probe(struct pci_dev *pdev,
				   const struct pci_device_id *ent)
{
	struct net_device *netdev = NULL;
	struct unm_adapter_s *adapter = NULL;
	uint8_t *mem_ptr0 = NULL;
	uint8_t *mem_ptr1 = NULL;
	uint8_t *mem_ptr2 = NULL;
	uint8_t *db_ptr = NULL;
	unsigned long mem_base, mem_len, db_base, db_len, pci_len0;
	unsigned long first_page_group_start, first_page_group_end;
	int i = 0, err, ver;
        int                        data = 0;
	int	                   temp;
	uint8_t nx_sev_error_mask = 0;
	uint32_t nx_sev_error_mask_dword = 0;
	struct unm_cmd_buffer *cmd_buf_arr = NULL;
	int pci_func_id = PCI_FUNC(pdev->devfn);
	int pos;
	int major, minor, sub;
	u32 first_boot;
	int pcie_cap;
	u16 lnk;
	uint8_t revision_id;
	nx_host_nic_t *nx_dev = NULL;
	struct nx_legacy_intr_set *legacy_intrp;
	int first_driver = 0;
        u32 control;
        u32 pdevfuncsave;
        u32 c8c9value;
        u32 chicken;


        if (pdev->class != 0x020000) {
                nx_nic_print3(NULL, "function %d, class 0x%x will not be "
			      "enabled.\n", pci_func_id, pdev->class);
                return -ENODEV;
        }

        if ((err = pci_enable_device(pdev))) {
                nx_nic_print3(NULL, "Cannot enable PCI device. Error[%d]\n",
			      err);
                return err;
        }

        if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
                nx_nic_print3(NULL, "Cannot find proper PCI device "
			      "base address, aborting. %p\n", pdev);
                err = -ENODEV;
                goto err_out_disable_pdev;
        }

        if ((err = unm_pci_request_regions(pdev, unm_nic_driver_name))) {
                nx_nic_print3(NULL, "Cannot find proper PCI resources. "
			      "Error[%d]\n", err);
                goto err_out_disable_pdev;
        }

	pci_set_master(pdev);

	pci_read_config_byte(pdev, PCI_REVISION_ID, &revision_id);

        nx_nic_print6(NULL, "Probe: revision ID = 0x%x\n", revision_id);

	netdev = alloc_etherdev(sizeof(struct unm_adapter_s));
	if (!netdev) {
		nx_nic_print3(NULL, "Failed to allocate memory for the "
			      "device block. Check system memory resource "
			      "usage.\n");
		err = -ENOMEM;
		unm_pci_release_regions(pdev);
	      err_out_disable_pdev:
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
		return err;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	SET_MODULE_OWNER(netdev);
#endif
	SET_NETDEV_DEV(netdev, &pdev->dev);

	adapter = netdev->priv;
	memset(adapter, 0, sizeof(struct unm_adapter_s));
	adapter->ahw.pdev = pdev;
	adapter->ahw.pci_func = pci_func_id;
	rwlock_init(&adapter->adapter_lock);
	spin_lock_init(&adapter->tx_lock);
	spin_lock_init(&adapter->lock);
	spin_lock_init(&adapter->buf_post_lock);
	spin_lock_init(&adapter->cb_lock);
#if defined(NEW_NAPI)
       spin_lock_init(&adapter->tx_cpu_excl);
#endif

	nx_init_vmklocks(adapter);
	adapter->ahw.qdr_sn_window = -1;
	adapter->ahw.ddr_mn_window = -1;
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	adapter->msglvl = NX_NIC_NOTICE;

	INIT_LIST_HEAD(&adapter->wait_list);
	for (i = 0; i < NX_WAIT_BIT_MAP_SIZE; i++) {
		adapter->wait_bit_map[i] = 0;
	}

	err = nx_init_status_handler(adapter);
	if (err) {
		nx_nic_print6(NULL, "Status descriptor handler initialization FAILED\n");
		goto err_ret;
	}

	if ((err = nx_set_dma_mask(adapter, revision_id))) {
		goto err_ret;
	}
	nx_nic_print6(NULL, "pci_using_dac: %u\n", adapter->pci_using_dac);

	/* remap phys address */
	mem_base = pci_resource_start(pdev, 0);	/* 0 is for BAR 0 */
	mem_len = pci_resource_len(pdev, 0);

	/* 128 Meg of memory */
	nx_nic_print6(NULL, "ioremap from %lx a size of %lx\n", mem_base,
		      mem_len);
        if (mem_len == UNM_PCI_128MB_SIZE) {
		    adapter->unm_nic_fill_statistics = &unm_nic_fill_statistics_128M;
		    adapter->unm_nic_clear_statistics = &unm_nic_clear_statistics_128M;
		    adapter->unm_nic_hw_write_wx = &unm_nic_hw_write_wx_128M;
		    adapter->unm_nic_hw_write_ioctl =
			    &unm_nic_hw_write_ioctl_128M;
		    adapter->unm_nic_hw_read_wx =
			    &unm_nic_hw_read_wx_128M;
		    adapter->unm_nic_hw_read_w0 = &unm_nic_hw_read_w0_128M;
		    adapter->unm_nic_hw_write_w0 = &unm_nic_hw_write_w0_128M;
		    adapter->unm_nic_hw_read_w1 = &unm_nic_hw_read_w1_128M;
		    adapter->unm_nic_hw_write_w1 = &unm_nic_hw_write_w1_128M;
		    adapter->unm_nic_hw_read_ioctl =
			    &unm_nic_hw_read_ioctl_128M;
		    adapter->unm_crb_writelit_adapter =
			    &unm_crb_writelit_adapter_128M;
		    adapter->unm_nic_pci_set_window =
                &unm_nic_pci_set_window_128M;
            adapter->unm_nic_pci_mem_read =
                &unm_nic_pci_mem_read_128M;
            adapter->unm_nic_pci_mem_write =
                &unm_nic_pci_mem_write_128M;
            adapter->unm_nic_pci_write_immediate =
			 	&unm_nic_pci_write_immediate_128M;
            adapter->unm_nic_pci_read_immediate =
			 	&unm_nic_pci_read_immediate_128M;
            adapter->unm_nic_pci_write_normalize =
			 	&unm_nic_pci_write_normalize_128M;
            adapter->unm_nic_pci_read_normalize =
			 	&unm_nic_pci_read_normalize_128M;

	        mem_ptr0 = ioremap(mem_base, FIRST_PAGE_GROUP_SIZE);
	        pci_len0 = FIRST_PAGE_GROUP_SIZE;
	        mem_ptr1 = ioremap(mem_base + SECOND_PAGE_GROUP_START,
				   SECOND_PAGE_GROUP_SIZE);
	        mem_ptr2 = ioremap(mem_base + THIRD_PAGE_GROUP_START,
				   THIRD_PAGE_GROUP_SIZE);
                first_page_group_start = FIRST_PAGE_GROUP_START;
                first_page_group_end   = FIRST_PAGE_GROUP_END;
            printk("128MB memmap\n");
        } else if (mem_len == UNM_PCI_32MB_SIZE) {
		    adapter->unm_nic_fill_statistics =
		    	&unm_nic_fill_statistics_128M;
		    adapter->unm_nic_clear_statistics =
			    &unm_nic_clear_statistics_128M;
		    adapter->unm_nic_hw_write_wx =
			    &unm_nic_hw_write_wx_128M;
		    adapter->unm_nic_hw_write_ioctl =
			    &unm_nic_hw_write_ioctl_128M;
		    adapter->unm_nic_hw_read_wx =
			    &unm_nic_hw_read_wx_128M;
		    adapter->unm_nic_hw_read_w0 = &unm_nic_hw_read_w0_128M;
		    adapter->unm_nic_hw_write_w0 = &unm_nic_hw_write_w0_128M;
		    adapter->unm_nic_hw_read_w1 = &unm_nic_hw_read_w1_128M;
		    adapter->unm_nic_hw_write_w1 = &unm_nic_hw_write_w1_128M;
		    adapter->unm_nic_hw_read_ioctl =
			    &unm_nic_hw_read_ioctl_128M;
		    adapter->unm_crb_writelit_adapter =
			    &unm_crb_writelit_adapter_128M;
		    adapter->unm_nic_pci_set_window =
                &unm_nic_pci_set_window_128M;
            adapter->unm_nic_pci_mem_read =
                &unm_nic_pci_mem_read_128M;
            adapter->unm_nic_pci_mem_write =
                &unm_nic_pci_mem_write_128M;
            adapter->unm_nic_pci_write_immediate =
				 &unm_nic_pci_write_immediate_128M;
            adapter->unm_nic_pci_read_immediate =
				 &unm_nic_pci_read_immediate_128M;
            adapter->unm_nic_pci_write_normalize =
				 &unm_nic_pci_write_normalize_128M;
            adapter->unm_nic_pci_read_normalize =
				 &unm_nic_pci_read_normalize_128M;

	        pci_len0 = 0;
	        mem_ptr1 = ioremap(mem_base, SECOND_PAGE_GROUP_SIZE);
	        mem_ptr2 = ioremap(mem_base + THIRD_PAGE_GROUP_START -
				   SECOND_PAGE_GROUP_START,
				   THIRD_PAGE_GROUP_SIZE);
		first_page_group_start = 0;
		first_page_group_end   = 0;
			printk("32MB memmap\n");
        } else if (mem_len == UNM_PCI_2MB_SIZE) {
		    adapter->unm_nic_fill_statistics =
					 &unm_nic_fill_statistics_2M;
		    adapter->unm_nic_clear_statistics =
					 &unm_nic_clear_statistics_2M;
		    adapter->unm_nic_hw_read_w0 = &unm_nic_hw_read_wx_2M;
		    adapter->unm_nic_hw_write_w0 = &unm_nic_hw_write_wx_2M;
		    adapter->unm_nic_hw_read_w1 = &unm_nic_hw_read_wx_2M;
		    adapter->unm_nic_hw_write_w1 = &unm_nic_hw_write_wx_2M;
		    adapter->unm_nic_hw_read_wx = &unm_nic_hw_read_wx_2M;
		    adapter->unm_nic_hw_write_wx = &unm_nic_hw_write_wx_2M;
		    adapter->unm_nic_hw_read_ioctl = &unm_nic_hw_read_wx_2M;
		    adapter->unm_nic_hw_write_ioctl = &unm_nic_hw_write_wx_2M;
		    adapter->unm_crb_writelit_adapter = &unm_crb_writelit_adapter_2M;
		    adapter->unm_nic_pci_set_window = &unm_nic_pci_set_window_2M;
            adapter->unm_nic_pci_mem_read = &unm_nic_pci_mem_read_2M;
            adapter->unm_nic_pci_mem_write = &unm_nic_pci_mem_write_2M;
            adapter->unm_nic_pci_write_immediate =
				 &unm_nic_pci_write_immediate_2M;
            adapter->unm_nic_pci_read_immediate =
				 &unm_nic_pci_read_immediate_2M;
            adapter->unm_nic_pci_write_normalize =
				 &unm_nic_pci_write_normalize_2M;
            adapter->unm_nic_pci_read_normalize =
				 &unm_nic_pci_read_normalize_2M;
            mem_ptr0 = ioremap(mem_base, mem_len);
            pci_len0 = mem_len;
            first_page_group_start = 0;
            first_page_group_end   = 0;

            adapter->ahw.ddr_mn_window = 0;
            adapter->ahw.qdr_sn_window = 0;

	    adapter->ahw.mn_win_crb = 0x100000 + PCIE_MN_WINDOW_REG(pci_func_id);

	    adapter->ahw.ms_win_crb = 0x100000 + PCIE_SN_WINDOW_REG(pci_func_id);

			printk("2MB memmap\n");
        } else {
		nx_nic_print3(NULL, "Invalid PCI memory mapped length\n");
		err = -EIO;
		goto err_ret;
        }

	nx_nic_print6(NULL, "ioremapped at 0 -> %p, 1 -> %p, 2 -> %p\n",
		      mem_ptr0, mem_ptr1, mem_ptr2);

	db_base = pci_resource_start(pdev, 4);	/* doorbell is on bar 4 */
	db_len = pci_resource_len(pdev, 4);

	nx_nic_print6(NULL, "doorbell ioremap from %lx a size of %lx\n",
		      db_base, db_len);

	db_ptr = ioremap(db_base, UNM_DB_MAPSIZE_BYTES);
	adapter->ahw.pci_base0 = (unsigned long)mem_ptr0;
	adapter->ahw.pci_len0 = pci_len0;
	adapter->ahw.first_page_group_start = first_page_group_start;
	adapter->ahw.first_page_group_end = first_page_group_end;
	adapter->ahw.pci_base1 = (unsigned long)mem_ptr1;
	adapter->ahw.pci_len1 = SECOND_PAGE_GROUP_SIZE;
	adapter->ahw.pci_base2 = (unsigned long)mem_ptr2;
	adapter->ahw.pci_len2 = THIRD_PAGE_GROUP_SIZE;
	adapter->ahw.crb_base =
	    PCI_OFFSET_SECOND_RANGE(adapter, UNM_PCI_CRBSPACE);
	adapter->ahw.db_base = (unsigned long)db_ptr;
	adapter->ahw.db_len = db_len;

	if (revision_id >= NX_P3_B0) {
		legacy_intrp = &legacy_intr[pci_func_id];
	} else {
		legacy_intrp = &legacy_intr[0];
	}

	adapter->legacy_intr.int_vec_bit = legacy_intrp->int_vec_bit;
	adapter->legacy_intr.tgt_status_reg = legacy_intrp->tgt_status_reg;
/* 		PCI_OFFSET_SECOND_RANGE(adapter, legacy_intrp->tgt_status_reg); */
	adapter->legacy_intr.tgt_mask_reg = legacy_intrp->tgt_mask_reg;
/* 		PCI_OFFSET_SECOND_RANGE(adapter, legacy_intrp->tgt_mask_reg); */
	adapter->legacy_intr.pci_int_reg = legacy_intrp->pci_int_reg;
/* 		PCI_OFFSET_SECOND_RANGE(adapter, legacy_intrp->pci_int_reg); */


	if ((mem_len != UNM_PCI_2MB_SIZE) &&
	    (((mem_ptr0 == 0UL) && (mem_len == UNM_PCI_128MB_SIZE)) ||
	    (mem_ptr1 == 0UL) || (mem_ptr2 == 0UL))) {
		nx_nic_print3(NULL, "Cannot remap adapter memory aborting.:"
			      "0 -> %p, 1 -> %p, 2 -> %p\n",
			      mem_ptr0, mem_ptr1, mem_ptr2);
		err = -EIO;
		goto err_ret;
	}
	if (db_len == 0) {
		nx_nic_print3(NULL, "doorbell is disabled\n");
		err = -EIO;
		goto err_ret;
	}
	if (db_ptr == 0UL) {
		nx_nic_print3(NULL, "Failed to allocate doorbell map.\n");
		err = -EIO;
		goto err_ret;
	}
	nx_nic_print6(NULL, "doorbell ioremapped at %p\n", db_ptr);

	/* This will be reset for mezz cards  */
	adapter->portnum = pci_func_id;
	adapter->status &= ~NETDEV_STATUS;

	if (NX_IS_REVISION_P3(revision_id)) {
		adapter->max_mc_count = UNM_MC_COUNT;
	} else {
		adapter->max_mc_count = (adapter->portnum > 1) ? 4 : 16;
	}

	/* CODE FOR P3-A2 ONLY!
	   This code turns off the TLP error severity bit and clears the UR
	   status bit.  This code should not be needed once the TLP bug is
	   fixed in P3-B0.   */
	if (NX_IS_REVISION_P3(revision_id)) {
		/* clear error bits */
		pci_read_config_byte(pdev, 0xDA, &nx_sev_error_mask);
		nx_sev_error_mask |= 0x0F;
		pci_write_config_byte(pdev, 0xDA, nx_sev_error_mask);

		/* turn off Malformed TLP severity */
                adapter->unm_nic_hw_read_wx(adapter, UNM_PCIE_REG(0x10C |
                                    ((pdev->devfn & 7) << 12)),
				   &nx_sev_error_mask_dword, 4);
		nx_sev_error_mask_dword &= 0xFFEBFFFF;
                adapter->unm_nic_hw_write_wx(adapter, UNM_PCIE_REG(0x10C |
                                    ((pdev->devfn & 7) << 12)),
				    &nx_sev_error_mask_dword, 4);

		/* write the same bits back to clear error status bits      */
                adapter->unm_nic_hw_read_wx(adapter, UNM_PCIE_REG(0x104 |
                                    ((pdev->devfn & 7) << 12)),
				   &nx_sev_error_mask_dword, 4);
                adapter->unm_nic_hw_write_wx(adapter, UNM_PCIE_REG(0x104 |
						 ((pdev->devfn & 7) << 12)),
				    &nx_sev_error_mask_dword, 4);
	}
#if defined(UNM_NIC_HW_CSUM)
	adapter->rx_csum = 1;
#endif

	netdev->open = unm_nic_open;
	netdev->stop = unm_nic_close;
	netdev->hard_start_xmit = unm_nic_xmit_frame;
	netdev->get_stats = unm_nic_get_stats;
	if (NX_IS_REVISION_P2(revision_id)) {
		netdev->set_multicast_list = nx_p2_nic_set_multi;
	} else {
		netdev->set_multicast_list = nx_p3_nic_set_multi;
	}
	netdev->set_mac_address = unm_nic_set_mac;
	netdev->change_mtu = unm_nic_change_mtu;
	netdev->do_ioctl = unm_nic_ioctl;
	netdev->tx_timeout = unm_tx_timeout;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,21)
	set_ethtool_ops(netdev);
	/* FIXME: maybe SET_ETHTOOL_OPS(netdev,&unm_nic_ethtool_ops); */
#endif
#if (defined(UNM_NIC_NAPI) && !defined(NEW_NAPI))
	netdev->poll = unm_nic_poll;
	netdev->weight = UNM_NETDEV_WEIGHT;
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
	netdev->poll_controller = unm_nic_poll_controller;
#endif
#ifdef UNM_NIC_HW_CSUM
	/* ScatterGather support */
	netdev->features = NETIF_F_SG;
	netdev->features |= NETIF_F_HW_CSUM;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,19)
#ifdef UNM_NIC_HW_VLAN
	/* Set features & pointers related to vlan hw acceleration */
	netdev->features |= NETIF_F_HW_VLAN_TX;
	netdev->features |= NETIF_F_HW_VLAN_RX;
	netdev->features	  |= NETIF_F_HW_VLAN_FILTER;
	netdev->vlan_rx_register   = unm_nic_vlan_rx_register;
	netdev->vlan_rx_kill_vid   = unm_nic_vlan_rx_kill_vid;
	netdev->vlan_rx_add_vid	   = unm_nic_vlan_rx_add_vid;	
#endif
	
#endif //LINUX_KERNEL_VERSION >= 2.4.19

#ifdef UNM_NETIF_F_TSO
	netdev->features |= NETIF_F_TSO;
#ifdef  NETIF_F_TSO6
	netdev->features |= NETIF_F_TSO6;
#endif
#endif

#ifndef ESX_4X
	NX_SET_NETQ_OPS(netdev, nx_nic_netqueue_ops);
#endif

	if (adapter->pci_using_dac)
		netdev->features |= NETIF_F_HIGHDMA;

	/*
	 * Set the CRB window to invalid. If any register in window 0 is
	 * accessed it should set the window to 0 and then reset it to 1.
	 */
	adapter->curr_window = 255;

	/*
	 * Initialize the HW so that we can get the board type first. Based on
	 * the board type the ring size is chosen.
	 */
	if (initialize_adapter_hw(adapter) != 0) {
		err = -EIO;
		goto err_ret;
	}
	/*raghu---- */
	/* Mezz cards have PCI function 0,2,3 enabled */
	if (adapter->ahw.boardcfg.board_type == UNM_BRDTYPE_P2_SB31_10G_IMEZ ||
	    adapter->ahw.boardcfg.board_type == UNM_BRDTYPE_P2_SB31_10G_HMEZ) {
		if (pci_func_id >= 2) {
			adapter->portnum = pci_func_id - 2;
		}
	}

        err = nx_os_dev_alloc(&nx_dev, adapter, adapter->portnum, MAX_RX_CTX,
                              MAX_TX_CTX);
        if (err) {
                nx_nic_print6(NULL, "Memory cannot be allocated");
                goto err_ret;
        }
        adapter->nx_dev = nx_dev;

	if (rom_fast_read(adapter, FW_VERSION_OFFSET, (int *)&ver) != 0) {
		nx_nic_print3(NULL, "Error in reading firmware version "
			      "from flash\n");
		return -1;
	}
	major = ver & 0xff;
	minor = (ver >> 8) & 0xff;
	sub = ver >> 16;

	if (NX_IS_REVISION_P3(revision_id)) {
		if (adapter->ahw.pci_func == 0) {
			first_driver=1;
		}
	} else {
		if (adapter->portnum == 0) {
			first_driver=1;
		}
	}
	adapter->ahw.revision_id = revision_id;

	unm_check_options(adapter);

	if (first_driver) {
		first_boot = adapter->unm_nic_pci_read_normalize(adapter,
						 UNM_CAM_RAM(0x1fc));
		if (check_hw_init(adapter) != 0) {
			nx_nic_print3(NULL, "ERROR: HW init sequence.\n");
			err = -ENODEV;
			goto err_ret;
		}

		if (adapter->ahw.boardcfg.board_type == UNM_BRDTYPE_P3_HMEZ ||
		    adapter->ahw.boardcfg.board_type == UNM_BRDTYPE_P3_XG_LOM) {
                        data = port_mode;     /* set to port_mode normally */
			if (port_mode == UNM_PORT_MODE_802_3_AP) {
				nx_nic_print6(NULL, "HW init sequence "
					      "for fixed 1G.\n");

			} else if (port_mode == UNM_PORT_MODE_XG) {
				nx_nic_print6(NULL, "HW init sequence "
					      "for fixed 10G.\n");

			} else if (port_mode == UNM_PORT_MODE_AUTO_NEG_1G) {
				nx_nic_print6(NULL, "HW init sequence "
					      "for Restricted (1G) "
					      "Auto-Negotiation.\n");

			} else if (port_mode == UNM_PORT_MODE_AUTO_NEG_XG) {
				nx_nic_print6(NULL, "HW init sequence "
					      "for Restricted (10G) "
					      "Auto-Negotiation.\n");

			} else {
				nx_nic_print6(NULL, "HW init sequence "
					      "for Auto-Negotiation.\n");
                                data = UNM_PORT_MODE_AUTO_NEG;
			}
            adapter->unm_nic_hw_write_wx(adapter, UNM_PORT_MODE_ADDR, &data, 4);
			
                        if ((wol_port_mode != UNM_PORT_MODE_802_3_AP) &&
			    (wol_port_mode != UNM_PORT_MODE_XG) &&
			    (wol_port_mode != UNM_PORT_MODE_AUTO_NEG_1G) &&
			    (wol_port_mode != UNM_PORT_MODE_AUTO_NEG_XG)) {
			    	wol_port_mode = UNM_PORT_MODE_AUTO_NEG;
			}
			nx_nic_print6(NULL, "wol_port_mode is %d\n", wol_port_mode);
			adapter->unm_nic_hw_write_wx(adapter, UNM_WOL_PORT_MODE, &wol_port_mode, 4);
		}

		/* Overwrite stale initialization register values */
		temp = 0;
		adapter->unm_nic_hw_write_wx(adapter, CRB_CMDPEG_STATE, &temp, 4);
		adapter->unm_nic_hw_write_wx(adapter, CRB_RCVPEG_STATE, &temp, 4);

		if (fw_load) {
#ifdef NX_FW_LOADER
			try_load_fw_file(adapter, fw_load);
#elif defined(NX_FUSED_FW)
			load_fused_fw(adapter, fw_load);
#endif
		} else {

			if ((major != _UNM_NIC_LINUX_MAJOR) ||
			    (minor != _UNM_NIC_LINUX_MINOR) ||
			    (sub != _UNM_NIC_LINUX_SUBVERSION)) {
				nx_nic_print4(NULL,
					      "There is a mismatch in Driver "
					      "(%d.%d.%d) and Firmware "
					      "(%d.%d.%d) version\n",
					      _UNM_NIC_LINUX_MAJOR,
					      _UNM_NIC_LINUX_MINOR,
					      _UNM_NIC_LINUX_SUBVERSION,
					      major, minor, sub);
				nx_nic_print4(NULL,
					      "Please update the flash "
					      "with firmware version %d.%d.%d "
					      "using nxflash\n",
					      _UNM_NIC_LINUX_MAJOR,
					      _UNM_NIC_LINUX_MINOR,
					      _UNM_NIC_LINUX_SUBVERSION);
			}
			if (first_boot != 0x55555555) {

				pinit_from_rom(adapter, 0);
				udelay(500);
				nx_nic_print5(NULL, "Loading the firmware "
					      "from flash\n");
				load_from_flash(adapter);
			}
		}


		adapter->unm_nic_hw_read_wx(adapter, UNM_PCIE_REG(PCIE_CHICKEN3),
					     &chicken, 4);
		chicken &= 0xFCFFFFFF;	// clear chicken3.25:24
	// if gen1 and B0, set F1020 - if gen 2, do nothing
	// if gen2 set to F1000
		pos = pci_find_capability(pdev, PCI_CAP_ID_GEN);
		if (pos == 0xC0) {
			pci_read_config_dword(pdev, pos + 0x10, &data);
			if ((data & 0x000F0000) != 0x00020000) {
				chicken |= 0x01000000;  // set chicken3.24 if gen1
			}
			printk("Gen2 strapping detected\n");
			c8c9value = 0xF1000;
		} else {
			chicken |= 0x01000000;  // set chicken3.24 if gen1
			printk("Gen1 strapping detected\n");
			if (revision_id == NX_P3_B0) {
				c8c9value = 0xF1020;
			} else {
				c8c9value = 0;
			}
		}
		adapter->unm_nic_hw_write_wx(adapter, UNM_PCIE_REG(PCIE_CHICKEN3),
					     &chicken, 4);

		pdevfuncsave = pdev->devfn;

		if ((pdevfuncsave & 0x07) == 0) {
		    if (c8c9value) {
			for (i = 0; i < 8; i++) {
			    pci_read_config_dword(pdev, pos + 8, &control);
			    pci_read_config_dword(pdev, pos + 8, &control);
			    pci_write_config_dword(pdev, pos + 8, c8c9value);
			    pdev->devfn++;
			}
			pdev->devfn = pdevfuncsave;
		    }
		}


		/*
		 * do this before waking up pegs so that we have valid dummy
		 * dma addr
		 */
		err = initialize_dummy_dma(adapter);
		if (err) {
			goto err_ret;
		}

		/*
		 * Tell the hardware our version number.
		 */
		i = ((_UNM_NIC_LINUX_MAJOR << 16) |
		     ((_UNM_NIC_LINUX_MINOR << 8)) |
		     (_UNM_NIC_LINUX_SUBVERSION));
		adapter->unm_nic_hw_write_wx(adapter, CRB_DRIVER_VERSION,
					     &i, 4);

		nx_nic_print7(NULL, "Bypassing PEGTUNE STUFF\n");
		//      UNM_NIC_PCI_WRITE_32(1, CRB_NORMALIZE(adapter,
		//                              UNM_ROMUSB_GLB_PEGTUNE_DONE));

		/* Handshake with the card before we register the devices. */
		nx_phantom_init(adapter, 0);

		nx_nic_print7(NULL, "State: 0x%0x\n",
			      adapter->unm_nic_hw_read_wx(adapter,
						CRB_CMDPEG_STATE, &i, 4));
	}

	if (NX_IS_REVISION_P3(revision_id)) {
		temp = UNM_CRB_READ_VAL_ADAPTER(UNM_MIU_MN_CONTROL, adapter);
		adapter->ahw.cut_through = NX_IS_SYSTEM_CUT_THROUGH(temp);
		nx_nic_print5(NULL, "Running in %s mode\n",
			      adapter->ahw.cut_through ? "'Cut Through'" :
			      "'Legacy'");
	}

	/*
	 * Now allocate space for network statistics.
	 */
	adapter->nic_net_stats.data = pci_alloc_consistent(adapter->pdev,
						sizeof (netxen_pstats_t),
						&adapter->nic_net_stats.phys);
	if (adapter->nic_net_stats.data == NULL) {
               	nx_nic_print3(NULL, "ERROR: Could not allocate space "
		      	      "for statistics\n");
               	err = (-ENOMEM);
		goto err_ret;
	}

	/* If RSS is not enabled set rings to 1 */
	if (!rss_enable) {
		adapter->max_possible_rss_rings = 1;
	}


#if defined(CONFIG_PCI_MSI)
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,8)) || defined(ESX_3X))
	/* This fix is for some system showing msi-x on always. */
	nx_reset_msix_bit(pdev);
	if (!use_msi_x) {
		adapter->msix_supported = 0;
	}

	if (major < NX_MSIX_SUPPORT_MAJOR ||
	    (major == NX_MSIX_SUPPORT_MAJOR &&
	     minor < NX_MSIX_SUPPORT_MINOR) ||
	    (major == NX_MSIX_SUPPORT_MAJOR &&
	     minor == NX_MSIX_SUPPORT_MINOR &&
	     sub < NX_MSIX_SUPPORT_SUBVERSION)) {
		nx_nic_print4(NULL, "Flashed firmware[%u.%u.%u] does not "
			      "support MSI-X, minimum firmware required is "
			      "%u.%u.%u\n",
			      major, minor, sub, NX_MSIX_SUPPORT_MAJOR,
			      NX_MSIX_SUPPORT_MINOR,
			      NX_MSIX_SUPPORT_SUBVERSION);
		adapter->msix_supported = 0;
	}

	/* For now we can only run msi-x on functions with 128 MB of
	 * memory. */
        if ((pci_resource_len(pdev,0) != UNM_PCI_128MB_SIZE) &&
            (pci_resource_len(pdev,0) != UNM_PCI_2MB_SIZE)) {
		adapter->msix_supported = 0;
	}

	if (adapter->msix_supported) {
		init_msix_entries(adapter);
		/* XXX : This fix is for super-micro slot perpetually
		   showing msi-x on !! */
		nx_reset_msix_bit(pdev);
	}
	if (adapter->msix_supported &&
	    !(pci_enable_msix(pdev, adapter->msix_entries,
			     MSIX_ENTRIES_PER_ADAPTER))) {
		adapter->flags |= UNM_NIC_MSIX_ENABLED;
		nx_nic_print7(NULL, "Using MSIX\n");
#ifdef	UNM_HWBUG_8_WORKAROUND
#define PCI_MSIX_FLAGS_ENABLE           (1 << 15)
		pos = pci_find_capability(pdev, PCI_CAP_ID_MSIX);
		nx_nic_print7(NULL, "pos: 0x%x\n", pos);
		if (pos) {
			pci_read_config_dword(pdev, pos, &control);
			control |= PCI_MSIX_FLAGS_ENABLE;
			pci_write_config_dword(pdev, pos, control);
		} else {
			/* XXX : What to do in this case ?? */
			nx_nic_print3(NULL, "Can not set MSIX cap in "
				      "pci config space!\n");
		}
#endif
		/* XXX : need to take care of following code */
		nx_nic_print7(NULL, "vector[0]: 0x%x\n",
			      adapter->msix_entries[0].vector);
		netdev->irq = adapter->msix_entries[0].vector;

	} else if (use_msi) {
#else
	if (use_msi) {
#endif
		if (!pci_enable_msi(pdev)) {
			adapter->flags |= UNM_NIC_MSI_ENABLED;
			nx_nic_print7(NULL, "Using MSI\n");
		} else {
			nx_nic_print3(NULL, "Unable to allocate MSI interrupt error\n");
		}
	}
#endif


	if (!(adapter->flags & UNM_NIC_MSIX_ENABLED)) {
		netdev->irq = pdev->irq;
	}

	NX_INIT_WORK(adapter->tx_timeout_task, unm_tx_timeout_task, netdev);

	cmd_buf_arr = vmalloc(TX_RINGSIZE(adapter->MaxTxDescCount));
	adapter->cmd_buf_arr = cmd_buf_arr;

        if (cmd_buf_arr == NULL) {
		nx_nic_print3(NULL, "Failed to allocate requested memory for"
				"TX cmd buffer. Setting MaxTxDescCount to 1024.\n");
		adapter->MaxTxDescCount = 1024;
	        cmd_buf_arr = vmalloc(TX_RINGSIZE(adapter->MaxTxDescCount));
		adapter->cmd_buf_arr = cmd_buf_arr;
		if (cmd_buf_arr == NULL) {
                	nx_nic_print3(NULL, "Failed to allocate memory for the "
				      "TX cmd buffer. Check system memory resource "
				      "usage.\n");
	                err = -ENOMEM;
	                goto err_ret;
		}
	}
	memset(cmd_buf_arr, 0, TX_RINGSIZE(adapter->MaxTxDescCount));

	init_timer(&adapter->watchdog_timer);
	adapter->ahw.linkup = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	adapter->watchdog_timer.function = &unm_watchdog;
#else
	adapter->watchdog_timer.function = &unm_watchdog_task;
#endif
	adapter->watchdog_timer.data = (unsigned long)adapter;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	NX_INIT_WORK(&adapter->watchdog_task, unm_watchdog_task, adapter);
#endif
	adapter->ahw.vendor_id = pdev->vendor;
	adapter->ahw.device_id = pdev->device;
	adapter->led_blink_rate = 1;
	pci_read_config_word(pdev, PCI_COMMAND, &adapter->ahw.pci_cmd_word);

	/* make sure Window == 1 */
//        unm_nic_pci_change_crbwindow(adapter,1);

	/*
	 * Initialize all the CRB registers here.
	 */
	/* Window = 1 */
	unm_nic_update_cmd_producer(adapter, 0);
	//unm_nic_update_cmd_consumer(adapter, 0);

	/* Synchronize with Receive peg */
	err = receive_peg_ready(adapter);
	if (err) {
		goto err_ret;
	}

	if (!unm_nic_fill_adapter_macaddr_from_flash(adapter)) {
		if (unm_nic_macaddr_set(adapter, adapter->mac_addr) != 0) {
			err = -EIO;
			goto err_ret;
		}
		memcpy(netdev->dev_addr, adapter->mac_addr, netdev->addr_len);
	}

	/*
	 * Initialize the pegnet_cmd_desc overflow data structure.
	 */
	unm_init_pending_cmd_desc(&adapter->pending_cmds);

	/* fill the adapter id field with the board serial num */
	unm_nic_get_serial_num(adapter);

	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	if ((err = register_netdev(netdev))) {
		nx_nic_print3(NULL, "register_netdev failed\n");
		goto err_ret;
	}

	/* name the proc entries different than ethx, since that can change */
	sprintf(adapter->procname, "dev%d", adapter_count++);
	
	/* initialize lro hash table */
#ifndef ESX_3X
	err = unm_init_lro(adapter);
	if (err != 0) {
		nx_nic_print3(NULL, "LRO Initialization failed\n");
		goto err_ret;
	}
#endif
	unm_init_proc_entries(adapter);
	pci_set_drvdata(pdev, netdev);

	/* Negotiated Link width */
	pcie_cap = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	pci_read_config_word(pdev, pcie_cap + PCI_EXP_LNKSTA, &lnk);
	adapter->link_width = (lnk >> 4) & 0x3f;

	/* Initialize default coalescing parameters */
	adapter->coal.normal.data.rx_packets =
	    NX_DEFAULT_INTR_COALESCE_RX_PACKETS;
	adapter->coal.normal.data.rx_time_us =
	    NX_DEFAULT_INTR_COALESCE_RX_TIME_US;
	adapter->coal.normal.data.tx_time_us =
	    NX_DEFAULT_INTR_COALESCE_TX_TIME_US;
	adapter->coal.normal.data.tx_packets =
	    NX_DEFAULT_INTR_COALESCE_TX_PACKETS;
#if 0
	nx_init_pexq_dbell(adapter, &adapter->pexq);
#endif
	switch (adapter->ahw.board_type) {
        case UNM_NIC_GBE:
                nx_nic_print5(NULL, "QUAD GbE board initialized\n");
                break;

        case UNM_NIC_XGBE:
                nx_nic_print5(NULL, "XGbE board initialized\n");
                break;
        }

	adapter->driver_mismatch = 0;
	return 0;

      err_ret:
	cleanup_adapter(adapter);
	return err;
}

void initialize_adapter_sw(struct unm_adapter_s *adapter,  nx_host_rx_ctx_t *nxhal_host_rx_ctx)
{
	int ring;
	uint32_t i;
	uint32_t num_rx_bufs = 0;
	rds_host_ring_t *host_rds_ring = NULL;
	nx_host_rds_ring_t *nxhal_host_rds_ring = NULL;

        adapter->freeCmdCount = adapter->MaxTxDescCount;
        nx_nic_print7(NULL, "initializing some queues: %p\n", adapter);

	for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
		struct unm_rx_buffer *rxBuf;
		nxhal_host_rds_ring = &nxhal_host_rx_ctx->rds_rings[ring];
		host_rds_ring =
		    (rds_host_ring_t *) nxhal_host_rds_ring->os_data;

		host_rds_ring->producer = 0;
		/* Initialize the queues for both receive and
		   command buffers */
		rxBuf = host_rds_ring->rx_buf_arr;
		num_rx_bufs = nxhal_host_rds_ring->ring_size;
		host_rds_ring->begin_alloc = 0;
		/* Initialize the free queues */
		TAILQ_INIT(&host_rds_ring->free_rxbufs.head);
		atomic_set(&host_rds_ring->alloc_failures, 0);
		/*
		 * Now go through all of them, set reference handles
		 * and put them in the queues.
		 */
		for (i = 0; i < num_rx_bufs; i++) {
			rxBuf->refHandle = i;
			rxBuf->state = UNM_BUFFER_FREE;
			rxBuf->skb = NULL;
			TAILQ_INSERT_TAIL(&host_rds_ring->free_rxbufs.head,
					  rxBuf, link);
			host_rds_ring->free_rxbufs.count++;
			nx_nic_print7(adapter, "Rx buf: i(%d) rxBuf: %p\n",
				      i, rxBuf);
			rxBuf++;
		}
	}

        nx_nic_print7(NULL, "initialized buffers for %s and %s\n",
		      "adapter->free_cmd_buf_list", "adapter->free_rxbuf");
        return;
}

static int initialize_adapter_hw(struct unm_adapter_s *adapter)
{
	uint32_t value = 0;
	unm_board_info_t *board_info = &(adapter->ahw.boardcfg);
	int ports = 0;

	if (unm_nic_get_board_info(adapter) != 0) {
            nx_nic_print3(NULL, "Error getting board config info.\n");
		return -1;
	}

	GET_BRD_PORTS_BY_TYPE(board_info->board_type, ports);
	if (ports == 0) {
                nx_nic_print3(NULL, "Unknown board type[0x%x]\n",
			      board_info->board_type);
	}
	adapter->ahw.max_ports = ports;

	return value;
}

int init_firmware(struct unm_adapter_s *adapter)
{
	uint32_t state = 0, loops = 0, err = 0;
    uint32_t   tempout;

	/* Window 1 call */
	read_lock(&adapter->adapter_lock);
    state = adapter->unm_nic_pci_read_normalize(adapter, CRB_CMDPEG_STATE);
	read_unlock(&adapter->adapter_lock);

	if (state == PHAN_INITIALIZE_ACK)
		return 0;

	while (state != PHAN_INITIALIZE_COMPLETE && loops < 200000) {
		udelay(100);
		schedule();
		/* Window 1 call */
		read_lock(&adapter->adapter_lock);
        state = adapter->unm_nic_pci_read_normalize(adapter, CRB_CMDPEG_STATE);
		read_unlock(&adapter->adapter_lock);

		loops++;
	}
	if (loops >= 200000) {
                nx_nic_print3(adapter, "Cmd Peg initialization not "
			      "complete:%x.\n", state);
		err = -EIO;
		return err;
	}
	/* Window 1 call */
	read_lock(&adapter->adapter_lock);
        tempout = PHAN_INITIALIZE_ACK;
        adapter->unm_nic_hw_write_wx(adapter, CRB_CMDPEG_STATE, &tempout, 4);
	read_unlock(&adapter->adapter_lock);

	return err;
}

void nx_free_rx_resources(struct unm_adapter_s *adapter,
				 nx_host_rx_ctx_t *nxhal_rx_ctx)
{
	struct unm_rx_buffer *buffer;
	rds_host_ring_t *host_rds_ring = NULL;
	nx_host_rds_ring_t *nxhal_host_rds_ring = NULL;
	int i;
	int ring;
	struct pci_dev *pdev = adapter->pdev;

	for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
		nxhal_host_rds_ring = &nxhal_rx_ctx->rds_rings[ring];
		host_rds_ring =
		    (rds_host_ring_t *) nxhal_host_rds_ring->os_data;
		for (i = 0; i < nxhal_host_rds_ring->ring_size; ++i) {
			buffer = &(host_rds_ring->rx_buf_arr[i]);
			if (buffer->state == UNM_BUFFER_FREE) {
				continue;
			}
			pci_unmap_single(pdev, buffer->dma,
					 host_rds_ring->dma_size,
					 PCI_DMA_FROMDEVICE);
			if (buffer->skb != NULL) {
				dev_kfree_skb_any(buffer->skb);
			}
		}
	}
}

static void unm_nic_free_ring_context(struct unm_adapter_s *adapter)
{
	nx_host_rx_ctx_t *nxhal_rx_ctx = NULL;
	int index = 0;
	if (adapter->ahw.cmdDescHead != NULL) {
		pci_free_consistent(adapter->ahw.cmdDesc_pdev,
				    (sizeof(cmdDescType0_t) *
				     adapter->MaxTxDescCount)
				    + sizeof(uint32_t),
				    adapter->ahw.cmdDescHead,
				    adapter->ahw.cmdDesc_physAddr);
		adapter->ahw.cmdDescHead = NULL;
	}

        if (adapter->nx_lic_dma.addr != NULL) {
                pci_free_consistent(adapter->ahw.pdev,
                                    sizeof(nx_finger_print_t),
                                    adapter->nx_lic_dma.addr,
                                    adapter->nx_lic_dma.phys_addr);
                adapter->nx_lic_dma.addr = NULL;
        }

#ifdef UNM_NIC_SNMP
	if (adapter->snmp_stats_dma.addr != NULL) {
		pci_free_consistent(adapter->ahw.pdev,
				    sizeof(struct unm_nic_snmp_ether_stats),
				    adapter->snmp_stats_dma.addr,
				    adapter->snmp_stats_dma.phys_addr);
		adapter->snmp_stats_dma.addr = NULL;
	}
#endif

#ifdef EPG_WORKAROUND
	if (adapter->ahw.pauseAddr != NULL) {
		pci_free_consistent(adapter->ahw.pause_pdev, 512,
				    adapter->ahw.pauseAddr,
				    adapter->ahw.pause_physAddr);
		adapter->ahw.pauseAddr = NULL;
	}
#endif
	for(index = 0; index < adapter->nx_dev->alloc_rx_ctxs ; index++) {
		if (adapter->nx_dev->rx_ctxs[index] != NULL) {
			nxhal_rx_ctx = adapter->nx_dev->rx_ctxs[index];
			nx_free_sts_rings(adapter,nxhal_rx_ctx);
			nx_nic_free_host_sds_resources(adapter,nxhal_rx_ctx);
			nx_free_rx_resources(adapter,nxhal_rx_ctx);
			nx_free_rx_vmkbounce_buffers(adapter, nxhal_rx_ctx);
			nx_nic_free_hw_rx_resources(adapter, nxhal_rx_ctx);
			nx_nic_free_host_rx_resources(adapter, nxhal_rx_ctx);
			nx_fw_cmd_create_rx_ctx_free(adapter->nx_dev,
					nxhal_rx_ctx);

		}
	}

	return;
}

static void unm_nic_free_ring_context_in_fw(struct unm_adapter_s *adapter)
{
	if (adapter->is_up != ADAPTER_UP_MAGIC)
		return;

	unm_nic_new_rx_context_destroy(adapter);
	unm_nic_new_tx_context_destroy(adapter);
}

void unm_nic_free_hw_resources(struct unm_adapter_s *adapter)
{
	unm_nic_free_ring_context_in_fw(adapter);
	unm_nic_free_ring_context(adapter);
}

static void __devexit unm_nic_remove(struct pci_dev *pdev)
{
	struct unm_adapter_s *adapter;
	struct net_device *netdev;
	int index = 0;
	nx_os_wait_event_t   *wait      = NULL;;
	struct list_head     *ptr, *tmp = NULL;

	netdev = pci_get_drvdata(pdev);
	if (netdev == NULL) {
		return;
	}

	adapter = netdev_priv(netdev);
	if (adapter == NULL) {
		return;
	}

	unm_nic_stop_port(adapter);
	if (UNM_IS_MSI_FAMILY(adapter)) {
		read_lock(&adapter->adapter_lock);
		for (index = 0; index < adapter->nx_dev->alloc_rx_ctxs;
				index++) {
			if (adapter->nx_dev->rx_ctxs[index] != NULL) {
				unm_nic_disable_all_int(adapter,
						adapter->nx_dev->rx_ctxs[index]);
			}
		}
		read_unlock(&adapter->adapter_lock);
	}

	unm_destroy_pending_cmd_desc(&adapter->pending_cmds);

	if (adapter->portnum == 0) {
		free_adapter_offload(adapter);
	}

	if (adapter->is_up == ADAPTER_UP_MAGIC) {
		for (index = 0; index < adapter->nx_dev->alloc_rx_ctxs;
				index++) {
			if (adapter->nx_dev->rx_ctxs[index] != NULL) {
				nx_unregister_irq(adapter,
						adapter->nx_dev->rx_ctxs[index]);
			}
		}
	}

	unregister_netdev(netdev);

	nx_free_vlan_buffers(adapter);
	nx_free_tx_vmkbounce_buffers(adapter);

	if (adapter->nic_net_stats.data != NULL) {
		pci_free_consistent(adapter->pdev, sizeof (netxen_pstats_t),
				    (void *)adapter->nic_net_stats.data,
				    adapter->nic_net_stats.phys);	
		adapter->nic_net_stats.data = NULL;
	}

	if (!list_empty(&adapter->wait_list)) {
		list_for_each_safe(ptr, tmp, &adapter->wait_list) {
			wait = list_entry(ptr, nx_os_wait_event_t, list);

			wait->trigger = 1;
			wait->active  = 0;
#ifdef ESX_3X
			vmk_thread_wakeup(&wait->wq);
#else
			wake_up_interruptible(&wait->wq);
#endif
		}		
	}
#if 0
	nx_free_pexq_dbell(adapter, &adapter->pexq);
#endif
	unm_nic_free_hw_resources(adapter);
	cleanup_adapter(adapter);
}

static int nx_config_rss(struct unm_adapter_s *adapter, int enable)
{
	nic_request_t req;
	rss_config_t *config;
	__uint64_t key[] = { 0xbeac01fa6a42b73bULL, 0x8030f20c77cb2da3ULL,
		0xae7b30b4d0ca2bcbULL, 0x43a38fb04167253dULL,
		0x255b0ec26d5a56daULL
	};
/* 	__uint64_t key[] = { 0x1111111111111111ULL, 0x1111111111111111ULL, */
/* 		0x1111111111111111ULL, 0x1111111111111111ULL, */
/* 		0x1111111111111111ULL */
/* 	}; */
	int rv;

	req.opcode = NX_NIC_HOST_REQUEST;
	config = (rss_config_t *) & req.body;
	config->req_hdr.opcode = NX_NIC_H2C_OPCODE_CONFIG_RSS;
	config->req_hdr.comp_id = 1;
	config->req_hdr.ctxid = adapter->portnum;
	config->req_hdr.need_completion = 0;

	config->hash_type_v4 = RSS_HASHTYPE_IP_TCP;
	config->hash_type_v6 = RSS_HASHTYPE_IP_TCP;
	config->enable = enable ? 1 : 0;
	config->use_indir_tbl = 0;
	config->indir_tbl_mask = 7;
	config->secret_key[0] = key[0];
	config->secret_key[1] = key[1];
	config->secret_key[2] = key[2];
	config->secret_key[3] = key[3];
	config->secret_key[4] = key[4];

	rv = nx_nic_send_cmd_descs(adapter->netdev, (cmdDescType0_t *) & req,
				   1);
	if (rv) {
		nx_nic_print3(adapter, "Sending RSS config to FW failed %d\n",
			      rv);
	}

	return (rv);
}


int nx_nic_multictx_get_filter_count(struct net_device *netdev, int queue_type)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	nx_host_nic_t* nx_dev;
	U32 pci_func;
	U32 max;
	U32 rcode;

	nx_dev = adapter->nx_dev;
	pci_func = adapter->nx_dev->pci_func;

	if (MULTICTX_IS_TX(queue_type)) {
		nx_nic_print3(adapter, "%s: TX filters not supported\n",
				__FUNCTION__);
		return -1;
	} else if(MULTICTX_IS_RX(queue_type)) {
		rcode = nx_fw_cmd_query_max_rules_per_ctx(nx_dev, pci_func,
					 &max) ;
	} else {
		nx_nic_print3(adapter, "%s: Invalid ctx type specified\n",
				__FUNCTION__);
		return -1;
	}

	if( rcode != NX_RCODE_SUCCESS){
		return -1;
	}

	return (max);
}


int nx_nic_multictx_get_ctx_count(struct net_device *netdev, int queue_type)
{
	nx_host_nic_t* nx_dev;
	U32 pci_func ;
	U32 max;
	U32 rcode;
	struct unm_adapter_s *adapter = netdev_priv(netdev);

	nx_dev = adapter->nx_dev;
	pci_func = adapter->nx_dev->pci_func;

	if (MULTICTX_IS_TX(queue_type)) {

		rcode = nx_fw_cmd_query_max_tx_ctx(nx_dev, pci_func, &max) ;

	} else if(MULTICTX_IS_RX(queue_type)) {

		rcode = nx_fw_cmd_query_max_rx_ctx(nx_dev, pci_func, &max) ;

	} else {
		nx_nic_print3(adapter, "%s: Invalid ctx type specified\n",__FUNCTION__);
		return -1;
	}

	if( rcode != NX_RCODE_SUCCESS){
		return -1;
	}

	return (max);
}

int nx_nic_multictx_get_queue_vector(struct net_device *netdev, int qid)
{
	nx_nic_print6(NULL, "%s: Operation not supported\n",__FUNCTION__);
	return -1;
}

int nx_nic_multictx_get_ctx_stats(struct net_device *netdev, int ctx,
				 struct net_device_stats *stats)
{
	nx_nic_print6(NULL, "%s: Operation not supported\n",__FUNCTION__);
	return -1;
}

int nx_nic_multictx_get_default_rx_queue(struct net_device *netdev)
{
	return 0;
}


int nx_nic_multictx_alloc_tx_ctx(struct net_device *netdev)
{
	nx_nic_print6(NULL, "%s: Opertion not supported\n", __FUNCTION__);
	return -1;
}

static void nx_nic_free_hw_rx_resources(struct unm_adapter_s *adapter,
                nx_host_rx_ctx_t *nxhal_host_rx_ctx)

{
        int ring;
        nx_host_rds_ring_t *nxhal_host_rds_ring = NULL;
        rds_host_ring_t *host_rds_ring = NULL;

        for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
                nxhal_host_rds_ring = &nxhal_host_rx_ctx->rds_rings[ring];
                host_rds_ring =
                        (rds_host_ring_t *) nxhal_host_rds_ring->os_data;
                if (nxhal_host_rds_ring->host_addr != NULL) {
                        pci_free_consistent(host_rds_ring->phys_pdev,
                                        RCV_DESC_RINGSIZE
                                        (nxhal_host_rds_ring->ring_size),
                                        nxhal_host_rds_ring->host_addr,
                                        nxhal_host_rds_ring->host_phys);
                        nxhal_host_rds_ring->host_addr = NULL;
                }
        }
}

static void nx_nic_free_host_sds_resources(struct unm_adapter_s *adapter,
                nx_host_rx_ctx_t *nxhal_host_rx_ctx)
{

        int ring;

	for (ring = 0; ring < nxhal_host_rx_ctx->num_sds_rings; ring++) {
		if (nxhal_host_rx_ctx->sds_rings[ring].os_data) {
			kfree(nxhal_host_rx_ctx->sds_rings[ring].os_data);
			nxhal_host_rx_ctx->sds_rings[ring].os_data = NULL;
                }
        }
}

static void nx_nic_free_host_rx_resources(struct unm_adapter_s *adapter,
                nx_host_rx_ctx_t *nxhal_host_rx_ctx)
{

	int ring;
	rds_host_ring_t *host_rds_ring = NULL;
	for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
		if (nxhal_host_rx_ctx->rds_rings[ring].os_data) {
			host_rds_ring =
				nxhal_host_rx_ctx->rds_rings[ring].os_data;
			vfree(host_rds_ring->rx_buf_arr);
			kfree(nxhal_host_rx_ctx->rds_rings[ring].os_data);
			nxhal_host_rx_ctx->rds_rings[ring].os_data = NULL;
		}
	}

}

static int nx_nic_alloc_hw_rx_resources(struct unm_adapter_s *adapter,
					nx_host_rx_ctx_t *nxhal_host_rx_ctx)
{

        nx_host_rds_ring_t *nxhal_host_rds_ring = NULL;
        rds_host_ring_t *host_rds_ring = NULL;
        int ring;
        int err = 0;
        void *addr;

        for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
                nxhal_host_rds_ring = &nxhal_host_rx_ctx->rds_rings[ring];
                host_rds_ring =
                        (rds_host_ring_t *) nxhal_host_rds_ring->os_data;
                addr = nx_alloc(adapter,
				RCV_DESC_RINGSIZE(nxhal_host_rds_ring->ring_size),
				(dma_addr_t *)&nxhal_host_rds_ring->host_phys,
				&host_rds_ring->phys_pdev);
                if (addr == NULL) {
                        nx_nic_print3(adapter, "bad return from nx_alloc\n");
                        err = -ENOMEM;
                        goto done;
                }
                nx_nic_print7(adapter, "rcv %d physAddr: 0x%llx\n",
			      ring, (U64)nxhal_host_rds_ring->host_phys);

                nxhal_host_rds_ring->host_addr = (I8 *) addr;
        }
        return 0;
  done:
        nx_nic_free_hw_rx_resources(adapter, nxhal_host_rx_ctx);
	return err;
}

static int nx_nic_alloc_host_sds_resources(struct unm_adapter_s *adapter,
		nx_host_rx_ctx_t *nxhal_host_rx_ctx)
{
	int ring = 0;
	int  err = 0;
	sds_host_ring_t *host_ring;

	for(ring = 0; ring < nxhal_host_rx_ctx->num_sds_rings; ring ++) {

		host_ring = (sds_host_ring_t *)kmalloc(sizeof(sds_host_ring_t),
				GFP_KERNEL);
		if (host_ring == NULL) {

			nx_nic_print3(adapter, "SDS ring memory allocation "
					"FAILED\n");
			err = -ENOMEM;
			goto err_ret;
		}
		memset(host_ring, 0,
				sizeof(sds_host_ring_t));
#ifdef NEW_NAPI 
	host_ring->ring = &nxhal_host_rx_ctx->sds_rings[ring];
#endif
	nxhal_host_rx_ctx->sds_rings[ring].os_data = host_ring;
	}

	return err;
err_ret:
	nx_nic_free_host_sds_resources(adapter, nxhal_host_rx_ctx);
	return err;	
}

static int nx_nic_alloc_host_rx_resources(struct unm_adapter_s *adapter,
                nx_host_rx_ctx_t *nxhal_host_rx_ctx)
{

        nx_host_rds_ring_t *nxhal_host_rds_ring = NULL;
        rds_host_ring_t *host_rds_ring = NULL;
        int ring;
        int err = 0;
        struct unm_rx_buffer *rx_buf_arr = NULL;

        for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
                nxhal_host_rx_ctx->rds_rings[ring].os_data =
                        kmalloc(sizeof(rds_host_ring_t), GFP_KERNEL);
                if (nxhal_host_rx_ctx->rds_rings[ring].os_data == NULL) {
                        nx_nic_print3(adapter, "RX ring memory allocation "
				      "FAILED\n");
                        err = -ENOMEM;
                        goto err_ret;
                }
                memset(nxhal_host_rx_ctx->rds_rings[ring].os_data, 0,
		       sizeof(rds_host_ring_t));
        }

        for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
                nxhal_host_rds_ring = &nxhal_host_rx_ctx->rds_rings[ring];
                host_rds_ring =
                    (rds_host_ring_t *)nxhal_host_rds_ring->os_data;
                switch (RCV_DESC_TYPE(ring)) {
                case RCV_RING_STD:
                        nxhal_host_rds_ring->ring_size =
                            adapter->MaxRxDescCount;
                        nxhal_host_rds_ring->ring_kind = RCV_DESC_NORMAL;
                        if (adapter->ahw.cut_through) {
                                nxhal_host_rds_ring->buff_size =
					NX_CT_DEFAULT_RX_BUF_LEN;
                                host_rds_ring->dma_size =
					nxhal_host_rds_ring->buff_size;
                        } else {
                                host_rds_ring->dma_size =
					NX_RX_NORMAL_BUF_MAX_LEN;
                                nxhal_host_rds_ring->buff_size =
					(host_rds_ring->dma_size +
					 IP_ALIGNMENT_BYTES);
                        }
			nx_nic_print6(adapter, "Buffer size = %d\n",
				      (int)nxhal_host_rds_ring->buff_size);
                        break;

                case RCV_RING_JUMBO:
                        nxhal_host_rds_ring->ring_size =
				adapter->MaxJumboRxDescCount;
                        nxhal_host_rds_ring->ring_kind = RCV_DESC_JUMBO;

                        if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
                                host_rds_ring->dma_size =
					NX_P2_RX_JUMBO_BUF_MAX_LEN;
                        } else {
				host_rds_ring->dma_size =
					NX_P3_RX_JUMBO_BUF_MAX_LEN;
                        }
                        nxhal_host_rds_ring->buff_size =
				(host_rds_ring->dma_size + IP_ALIGNMENT_BYTES);
                        break;

                case RCV_RING_LRO:
                        nxhal_host_rds_ring->ring_size =
				adapter->MaxLroRxDescCount;
                        nxhal_host_rds_ring->ring_kind = RCV_DESC_LRO;
                        host_rds_ring->dma_size = RX_LRO_DMA_MAP_LEN;
                        nxhal_host_rds_ring->buff_size =
				MAX_RX_LRO_BUFFER_LENGTH;
                        break;

                default:
                        nx_nic_print3(adapter,
				      "bad receive descriptor type %d\n",
				      RCV_DESC_TYPE(ring));
                        break;
                }
                rx_buf_arr =
                    vmalloc(RCV_BUFFSIZE(nxhal_host_rds_ring->ring_size));
                if (rx_buf_arr == NULL) {
                        nx_nic_print3(adapter, "Rx buffer alloc error."
				      "Check system memory resource usage.\n");
                        err = -ENOMEM;
                        goto err_ret;
                }
		memset(rx_buf_arr, 0,
                       RCV_BUFFSIZE(nxhal_host_rds_ring->ring_size));
                host_rds_ring->rx_buf_arr = rx_buf_arr;
        }

	initialize_adapter_sw(adapter, nxhal_host_rx_ctx);

        return err;

err_ret:
	nx_nic_free_host_rx_resources(adapter, nxhal_host_rx_ctx);
        return err;
}

int nx_nic_multictx_alloc_rx_ctx(struct net_device *netdev)
{
	int ctx_id = -1,ring = 0;
	int err = 0;
	nx_host_rx_ctx_t *nxhal_host_rx_ctx = NULL;
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	ctx_id = nx_nic_create_rx_ctx(netdev);
	if( ctx_id >= 0 ) {
		nxhal_host_rx_ctx = adapter->nx_dev->rx_ctxs[ctx_id];
		for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
			unm_post_rx_buffers(adapter,nxhal_host_rx_ctx, ring);
		}
		err = nx_register_irq(adapter,nxhal_host_rx_ctx);
		if(err) {
			goto err_ret;
		}
		read_lock(&adapter->adapter_lock);
		unm_nic_enable_all_int(adapter, nxhal_host_rx_ctx);
		read_unlock(&adapter->adapter_lock);

		return ctx_id;
	}
err_ret:
	return -1;

}
int nx_nic_create_rx_ctx(struct net_device *netdev)
{
        int err = 0;
        nx_host_rx_ctx_t *nxhal_host_rx_ctx = NULL;
        struct unm_adapter_s *adapter = netdev_priv(netdev);
	int num_rss_rings = 1;
	int num_rules = 1;
	if ((adapter->flags & UNM_NIC_MSIX_ENABLED))
                num_rss_rings =  adapter->max_possible_rss_rings;

        err = nx_fw_cmd_create_rx_ctx_alloc(adapter->nx_dev,
					    NUM_RCV_DESC_RINGS,
                                          num_rss_rings, num_rules,
                                          &nxhal_host_rx_ctx);
        if (err) {
                nx_nic_print3(adapter, "RX ctx memory allocation FAILED\n");
                goto err_ret;
        }
        if ((err = nx_nic_alloc_host_rx_resources(adapter,
						  nxhal_host_rx_ctx))) {
                nx_nic_print3(adapter, "RDS memory allocation FAILED\n");
                goto err_ret;
        }


        if ((err = nx_nic_alloc_hw_rx_resources(adapter, nxhal_host_rx_ctx))) {
                nx_nic_print3(adapter, "RDS Descriptor ring memory allocation "
			      "FAILED\n");
                goto err_ret;
        }

	if ((err = nx_nic_alloc_host_sds_resources(adapter,
						   nxhal_host_rx_ctx))) {
		nx_nic_print3(adapter, "SDS host memory allocation FAILED\n");
                goto err_ret;
	}

	err = nx_alloc_sts_rings(adapter, nxhal_host_rx_ctx,
				 nxhal_host_rx_ctx->num_sds_rings);
        if (err) {
		nx_nic_print3(adapter, "SDS memory allocation FAILED\n");
                goto err_ret;
        }

	if ((nx_setup_rx_vmkbounce_buffers(adapter, nxhal_host_rx_ctx)) != 0) {
		nx_nic_print3(adapter, "VMK bounce memory allocation "
			      "FAILED\n");
		goto err_ret;
	}

        if ((err = unm_nic_new_rx_context_prepare(adapter,
						  nxhal_host_rx_ctx))) {
                nx_nic_print3(adapter, "RX ctx allocation FAILED\n");
                goto err_ret;
        }
        return nxhal_host_rx_ctx->this_id;

  err_ret:
        if (nxhal_host_rx_ctx && adapter->nx_dev) {
		nx_free_sts_rings(adapter,nxhal_host_rx_ctx);
                nx_nic_free_host_sds_resources(adapter,nxhal_host_rx_ctx);
                nx_free_rx_resources(adapter,nxhal_host_rx_ctx);
		nx_free_rx_vmkbounce_buffers(adapter, nxhal_host_rx_ctx );
                nx_nic_free_hw_rx_resources(adapter, nxhal_host_rx_ctx);
                nx_nic_free_host_rx_resources(adapter, nxhal_host_rx_ctx);
                nx_fw_cmd_create_rx_ctx_free(adapter->nx_dev,
					     nxhal_host_rx_ctx);
        }

        return (-1);
}

int nx_nic_multictx_free_tx_ctx(struct net_device *netdev, int ctx_id)
{
        struct unm_adapter_s *adapter = netdev_priv(netdev);

	nx_nic_print3(adapter, "%s: Operation not supported\n", __FUNCTION__);
	return -1;
}

int nx_nic_multictx_free_rx_ctx(struct net_device *netdev, int ctx_id)
{
        nx_host_rx_ctx_t *nxhal_host_rx_ctx = NULL;
        struct unm_adapter_s *adapter = netdev_priv(netdev);

	if (ctx_id > adapter->nx_dev->alloc_rx_ctxs) {
		nx_nic_print4(adapter, "%s: Invalid context id\n",
			      __FUNCTION__);
		return -1;
	}

	nxhal_host_rx_ctx = adapter->nx_dev->rx_ctxs[ctx_id];
	if (nxhal_host_rx_ctx != NULL) {
		read_lock(&adapter->adapter_lock);
		unm_nic_disable_all_int(adapter, nxhal_host_rx_ctx);
		read_unlock(&adapter->adapter_lock);
		nx_unregister_irq(adapter,nxhal_host_rx_ctx);
		nx_fw_cmd_destroy_rx_ctx(nxhal_host_rx_ctx,
				NX_DESTROY_CTX_RESET);
		nx_free_sts_rings(adapter, nxhal_host_rx_ctx);
		nx_nic_free_host_sds_resources(adapter, nxhal_host_rx_ctx);
		nx_free_rx_resources(adapter, nxhal_host_rx_ctx);
		nx_free_rx_vmkbounce_buffers(adapter, nxhal_host_rx_ctx);
		nx_nic_free_hw_rx_resources(adapter, nxhal_host_rx_ctx);
		nx_nic_free_host_rx_resources(adapter, nxhal_host_rx_ctx);
		nx_fw_cmd_create_rx_ctx_free(adapter->nx_dev,
                                nxhal_host_rx_ctx);
	} else {
		return (-1);
	}

	return 0;
}

int nx_nic_multictx_set_rx_rule(struct net_device *netdev, int ctx_id, char* mac_addr)
{
	int rv;
	int i;
        nx_host_rx_ctx_t *nxhal_host_rx_ctx = NULL;
	nx_rx_rule_t *rx_rule = NULL;
        struct unm_adapter_s *adapter = netdev_priv(netdev);

	if(ctx_id > adapter->nx_dev->alloc_rx_ctxs) {
		nx_nic_print3(adapter, "%s: Invalid context id\n",__FUNCTION__);
		return -1;
	}

	nxhal_host_rx_ctx = adapter->nx_dev->rx_ctxs[ctx_id];

	if(!nxhal_host_rx_ctx) {
		nx_nic_print3(adapter, "%s: Ctx not active\n", __FUNCTION__);
		return -1;
	}

	if(nxhal_host_rx_ctx->active_rx_rules >= nxhal_host_rx_ctx->num_rules) {
		nx_nic_print3(adapter, "%s: Rules counts exceeded\n",__FUNCTION__);
		return -1;
	}

	for (i = 0; i < nxhal_host_rx_ctx->num_rules; i++) {
		if(!nxhal_host_rx_ctx->rules[i].active) {
			rx_rule = &nxhal_host_rx_ctx->rules[i];
			rx_rule->id = i;
			break;
		}
	}

	if(!rx_rule) {
		nx_nic_print3(adapter, "%s: No rule available\n",__FUNCTION__);
		return -1;
	}

	rv = nx_os_pf_add_l2_mac(adapter->nx_dev, nxhal_host_rx_ctx, mac_addr);

	if (rv == 0) {
		rx_rule->active = 1;
		nx_os_copy_memory(&(rx_rule->arg.m.mac), mac_addr, 6) ;
		rx_rule->type = NX_RX_RULETYPE_MAC;
		nxhal_host_rx_ctx->active_rx_rules++;
	} else {
		nx_nic_print3(adapter, "%s: Failed to set mac addr\n", __FUNCTION__);
		return -1;
	}

	return rx_rule->id;
}


int nx_nic_multictx_remove_rx_rule(struct net_device *netdev, int ctx_id, int rule_id)
{
	int rv;
        nx_host_rx_ctx_t *nxhal_host_rx_ctx = NULL;
	nx_rx_rule_t *rx_rule = NULL;
        struct unm_adapter_s *adapter = netdev_priv(netdev);
	char mac_addr[6];

	if(ctx_id > adapter->nx_dev->alloc_rx_ctxs) {
		nx_nic_print3(adapter, "%s: Invalid context id\n",__FUNCTION__);
		return -1;
	}

	nxhal_host_rx_ctx = adapter->nx_dev->rx_ctxs[ctx_id];

	if(!nxhal_host_rx_ctx) {
		nx_nic_print3(adapter, "%s: Ctx not active\n", __FUNCTION__);
		return -1;
	}

	if(rule_id >= nxhal_host_rx_ctx->num_rules) {
		nx_nic_print3(adapter, "%s: Invalid rule id specified\n",__FUNCTION__);
		return -1;
	}

	rx_rule = &nxhal_host_rx_ctx->rules[rule_id];

	if(!rx_rule->active) {
		nx_nic_print3(adapter, "%s: Deleting an inactive rule \n",__FUNCTION__);
		return -1;
	}

	nx_os_copy_memory(mac_addr, &(rx_rule->arg.m.mac), 6);

	rv = nx_os_pf_remove_l2_mac(adapter->nx_dev, nxhal_host_rx_ctx, mac_addr);

	if (rv == 0) {
		rx_rule->active = 0;
		nxhal_host_rx_ctx->active_rx_rules--;
	} else {
		nx_nic_print3(adapter, "%s: Failed to delete mac addr\n", __FUNCTION__);
		return -1;
	}

	return rv;
}

#ifdef NEW_NAPI
/*
 * Function to enable napi interface for all rss rings
 */
static void nx_napi_enable(nx_host_rx_ctx_t *nxhal_host_rx_ctx) {

	int i;
	nx_host_sds_ring_t      *nxhal_sds_ring = NULL;
	sds_host_ring_t         *host_sds_ring  = NULL;

	for (i = 0; i < nxhal_host_rx_ctx->num_sds_rings; i++) {
		nxhal_sds_ring = &nxhal_host_rx_ctx->sds_rings[i];
		host_sds_ring = (sds_host_ring_t *)nxhal_sds_ring->os_data;
		napi_enable(&host_sds_ring->napi);
	}
}

/*
 * Function to disable napi interface for all rss rings
 */
static void nx_napi_disable(nx_host_rx_ctx_t *nxhal_host_rx_ctx) {

	int i;
	nx_host_sds_ring_t      *nxhal_sds_ring = NULL;
	sds_host_ring_t         *host_sds_ring  = NULL;

	for (i = 0; i < nxhal_host_rx_ctx->num_sds_rings; i++) {
		nxhal_sds_ring = &nxhal_host_rx_ctx->sds_rings[i];
		host_sds_ring = (sds_host_ring_t *)nxhal_sds_ring->os_data;
		napi_disable(&host_sds_ring->napi);
	}
}
#endif


/*
 * Called when a network interface is made active
 * Returns 0 on success, negative value on failure
 */
static int unm_nic_open(struct net_device *netdev)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;
	int err = 0;
	int ring;
	int i;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0) && !defined(ESX))
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;
#endif
	mod_timer(&adapter->watchdog_timer, jiffies);

	if (adapter->is_up != ADAPTER_UP_MAGIC) {
		err = init_firmware(adapter);
		if (err != 0) {
			nx_nic_print3(adapter, "Failed to init firmware\n");
			err = -EIO;
			goto error_ret;
		}
		unm_nic_flash_print(adapter);
		/* setup all the resources for the Phantom... */
		/* this include the descriptors for rcv, tx, and status */
		unm_nic_clear_stats(adapter);
		err = unm_nic_hw_resources(adapter);
		if (err) {
			nx_nic_print3(adapter, "Error in setting hw "
				      "resources: %d\n", err);
			goto error_ret;
		}

		if ((nx_setup_vlan_buffers(adapter)) != 0) {
			nx_free_vlan_buffers(adapter);
			unm_nic_free_hw_resources(adapter);
			err = -ENOMEM;
			goto error_ret;
		}

		if ((nx_setup_tx_vmkbounce_buffers(adapter)) != 0) {
			nx_free_tx_vmkbounce_buffers(adapter);
			nx_free_vlan_buffers(adapter);
			unm_nic_free_hw_resources(adapter);
			err = -ENOMEM;
			goto error_ret;
		}

		for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
			unm_post_rx_buffers(adapter,adapter->nx_dev->rx_ctxs[0], ring);
		}

		err = nx_register_irq(adapter,adapter->nx_dev->rx_ctxs[0]);
		if (err) {
			nx_nic_print3(adapter, "Unable to register the "
				      "interrupt service routine\n");
			nx_free_tx_vmkbounce_buffers(adapter);
			nx_free_vlan_buffers(adapter);
			unm_nic_free_hw_resources(adapter);
			goto error_ret;
		}
		//              tasklet_enable(&adapter->tx_tasklet);

		adapter->is_up = ADAPTER_UP_MAGIC;
		//                update_core_clock(port);
	}

#ifdef ESX_3X
	    init_waitqueue_head(&nx_os_event_wq);
#endif

	/* Set up virtual-to-physical port mapping */

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		i = adapter->unm_nic_pci_read_normalize(adapter,
						 CRB_V2P(adapter->portnum));

		if (i != 0x55555555) {
			nx_nic_print6(adapter,
					 "PCI Function %d using Phy Port %d",
					adapter->portnum, i);
			adapter->physical_port = i;
		}
	} else {
		adapter->physical_port = (adapter->nx_dev->rx_ctxs[0])->port;
	}

	read_lock(&adapter->adapter_lock);
	unm_nic_enable_all_int(adapter, adapter->nx_dev->rx_ctxs[0]);
	read_unlock(&adapter->adapter_lock);

	spin_lock_init(&adapter->stats_lock);

	if (unm_nic_macaddr_set(adapter, adapter->mac_addr) != 0) {
                nx_nic_print3(adapter, "Cannot set Mac addr.\n");
		nx_free_tx_vmkbounce_buffers(adapter);
		nx_free_vlan_buffers(adapter);
		unm_nic_free_hw_resources(adapter);
		err = -EIO;
		goto error_ret;
	}
	memcpy(netdev->dev_addr, adapter->mac_addr, netdev->addr_len);

	if (unm_nic_init_port(adapter) != 0) {
		nx_nic_print3(adapter, "Failed to initialize the port %d\n",
			      adapter->portnum);
		unm_nic_down(adapter);
		err = -EIO;
		goto error_ret;
	}

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		nx_p2_nic_set_multi(netdev);
	} else {
		nx_p3_nic_set_multi(netdev);
	}

	if (!adapter->driver_mismatch) {
		netif_start_queue(netdev);
	}

	adapter->state = PORT_UP;

	if ((adapter->flags & UNM_NIC_MSIX_ENABLED) &&
	    adapter->max_possible_rss_rings > 1) {
		nx_nic_print6(adapter, "RSS being enabled\n");
		nx_config_rss(adapter, 1);
	}

	return 0;

      error_ret:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	FLUSH_SCHEDULED_WORK();
#endif
	del_timer_sync(&adapter->watchdog_timer);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0) && !defined(ESX))
	module_put(THIS_MODULE);
#endif

	return err;

}

void *nx_alloc(struct unm_adapter_s *adapter, size_t sz,
	       dma_addr_t * ptr, struct pci_dev **used_dev)
{
	struct pci_dev *pdev;
	void *addr;

	pdev = adapter->ahw.pdev;

	addr = pci_alloc_consistent(pdev, sz, ptr);
	if ((unsigned long long)((*ptr) + sz) < adapter->dma_mask) {
		*used_dev = pdev;
		return addr;
	}
	pci_free_consistent(pdev, sz, addr, *ptr);
#ifdef ESX
	return NULL;
#endif
	addr = pci_alloc_consistent(NULL, sz, ptr);
	*used_dev = NULL;
	return addr;
}

static void nx_free_sts_rings(struct unm_adapter_s *adapter,
			      nx_host_rx_ctx_t *nxhal_rx_ctx)
{
	int i;
	int ctx_id;
	nx_host_sds_ring_t	*nxhal_sds_ring;
	sds_host_ring_t		*host_sds_ring;
	ctx_id = nxhal_rx_ctx->this_id;
	for (i = 0; i < nxhal_rx_ctx->num_sds_rings; i++) {
		nxhal_sds_ring = &nxhal_rx_ctx->sds_rings[i];
		host_sds_ring = nxhal_sds_ring->os_data;
		if (((i != 0 && ctx_id == 0) || (i == 0 && ctx_id!= 0)) && host_sds_ring->netdev) {
			nx_nic_print7(adapter, "Freeing[%u] netdev\n", i);
			free_netdev(host_sds_ring->netdev);
			host_sds_ring->netdev = NULL;
		}
		if (nxhal_sds_ring->host_addr) {
			pci_free_consistent(host_sds_ring->pci_dev,
					    STATUS_DESC_RINGSIZE(adapter->
								 MaxRxDescCount),
					    nxhal_sds_ring->host_addr,
					    nxhal_sds_ring->host_phys);
			nxhal_sds_ring->host_addr = NULL;
		}
	}
}

/*
 * Allocate the receive status rings for RSS.
 */
static int nx_alloc_sts_rings(struct unm_adapter_s *adapter, nx_host_rx_ctx_t *nxhal_rx_ctx, int cnt)
{
	struct net_device *netdev;
	int i;
	int j;
	int ctx_id ;
	void *addr;
	nx_host_sds_ring_t *nxhal_sds_ring = NULL;
	sds_host_ring_t	   *host_sds_ring = NULL;
	ctx_id = nxhal_rx_ctx->this_id;
	for (i = 0; i < cnt; i++) {
		nxhal_sds_ring = &nxhal_rx_ctx->sds_rings[i];
                host_sds_ring = (sds_host_ring_t *)nxhal_sds_ring->os_data;
		addr = nx_alloc(adapter,
				STATUS_DESC_RINGSIZE(adapter->MaxRxDescCount),
                                &nxhal_sds_ring->host_phys,
				&host_sds_ring->pci_dev);

		if (addr == NULL) {
			nx_nic_print3(adapter, "Status ring[%d] allocation "
				      "failed\n", i);
			goto error_done;
		}
		for (j = 0; j < NUM_RCV_DESC_RINGS; j++) {
                        TAILQ_INIT(&host_sds_ring->free_rbufs[j].head);
		}

                nxhal_sds_ring->host_addr       = (I8 *)addr;
                host_sds_ring->adapter          = adapter;
                host_sds_ring->ring_idx         = i;
                nxhal_sds_ring->ring_size       = adapter->MaxRxDescCount;

		if (i == 0 && ctx_id == 0) {
                        host_sds_ring->netdev = adapter->netdev;
		} else {
			netdev = alloc_netdev(0, adapter->netdev->name,
					      ether_setup);
			if (!netdev) {
				nx_nic_print3(adapter, "Netdev[%d] alloc "
					      "failed\n", i);
				goto error_done;
			}
			sprintf(netdev->name, "%s:%d",
				adapter->netdev->name, i);

                        netdev->priv = nxhal_sds_ring;
#if (defined(UNM_NIC_NAPI) && !defined(NEW_NAPI))
			netdev->weight = UNM_NETDEV_WEIGHT;
			netdev->poll = nx_nic_poll_sts;
#endif
			set_bit(__LINK_STATE_START, &netdev->state);

                        host_sds_ring->netdev = netdev;
		}

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,8)) || defined(ESX_3X))
		if (adapter->flags & UNM_NIC_MSIX_ENABLED) {
			host_sds_ring->netdev->irq =
				adapter->msix_entries[i + ctx_id].vector;
			nxhal_sds_ring->msi_index =
				adapter->msix_entries[i + ctx_id].entry;
		}
#endif
	}
	return (0);

      error_done:
        nx_free_sts_rings(adapter,nxhal_rx_ctx);
	return (-ENOMEM);
	
}

/*
 *
 */
static int nx_register_irq(struct unm_adapter_s *adapter,
			   nx_host_rx_ctx_t *nxhal_host_rx_ctx)
{
	int i;
	int rv;
	int cnt;
        nx_host_sds_ring_t      *nxhal_sds_ring = NULL;
        sds_host_ring_t         *host_sds_ring = NULL;

	for (i = 0; i < nxhal_host_rx_ctx->num_sds_rings; i++) {
		nxhal_sds_ring = &nxhal_host_rx_ctx->sds_rings[i];
		host_sds_ring = (sds_host_ring_t *)nxhal_sds_ring->os_data;

		rv = request_irq(host_sds_ring->netdev->irq, &unm_intr,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
				SA_SHIRQ | SA_SAMPLE_RANDOM,
#else
				IRQF_SHARED|IRQF_SAMPLE_RANDOM,
#endif
				host_sds_ring->netdev->name, nxhal_sds_ring);
		if (rv) {
			nx_nic_print3(adapter, "%s Unable to register "
					"the interrupt service routine\n",
					host_sds_ring->netdev->name);
			cnt = i;
			goto error_done;
		}
#ifdef NEW_NAPI
		netif_napi_add(host_sds_ring->netdev, &host_sds_ring->napi,
				nx_nic_poll_sts,
				UNM_NETDEV_WEIGHT);
		napi_enable(&host_sds_ring->napi);
#endif
	}

	return (0);

  error_done:
	for (i = 0; i < cnt; i++) {
                nxhal_sds_ring = &nxhal_host_rx_ctx->sds_rings[i];
                host_sds_ring = (sds_host_ring_t *)nxhal_sds_ring->os_data;
                free_irq(host_sds_ring->netdev->irq, nxhal_sds_ring);
	}

	return (rv);

}

/*
 *
 */
static int nx_unregister_irq(struct unm_adapter_s *adapter,
			     nx_host_rx_ctx_t *nxhal_host_rx_ctx)
{
	
	int i;
	nx_host_sds_ring_t      *nxhal_sds_ring = NULL;
	sds_host_ring_t         *host_sds_ring = NULL;


	for (i = 0; i < nxhal_host_rx_ctx->num_sds_rings; i++) {
		nxhal_sds_ring = &nxhal_host_rx_ctx->sds_rings[i];
		host_sds_ring = (sds_host_ring_t *)nxhal_sds_ring->os_data;
#ifdef NEW_NAPI
		napi_disable(&host_sds_ring->napi);
#endif
		free_irq(host_sds_ring->netdev->irq, nxhal_sds_ring);
	}

	return (0);

}

/*
 * Utility to synchronize with receive peg.
 *  Returns   0 on sucess
 *         -EIO on error
 */
int receive_peg_ready(struct unm_adapter_s *adapter)
{
	uint32_t state = 0;
	int loops = 0, err = 0;

	/* Window 1 call */
	read_lock(&adapter->adapter_lock);
	state = adapter->unm_nic_pci_read_normalize(adapter, CRB_RCVPEG_STATE);
	read_unlock(&adapter->adapter_lock);

	while ((state != PHAN_PEG_RCV_INITIALIZED) && (loops < 20000)) {
		udelay(100);
		schedule();
		/* Window 1 call */

		read_lock(&adapter->adapter_lock);
		state = adapter->unm_nic_pci_read_normalize(adapter, CRB_RCVPEG_STATE);
		read_unlock(&adapter->adapter_lock);

		loops++;
	}

	if (loops >= 20000) {
		nx_nic_print3(adapter, "Receive Peg initialization not "
			      "complete: 0x%x.\n", state);
		err = -EIO;
	}

	return err;
}

/*
 * check if the firmware has been downloaded and ready to run  and
 * setup the address for the descriptors in the adapter
 */
int unm_nic_hw_resources(struct unm_adapter_s *adapter)
{
	struct _hardware_context *hw = &adapter->ahw;
	void *addr;
#ifdef EPG_WORKAROUND
	void *pause_addr;
#endif
	int err = 0;

        nx_nic_print6(adapter, "crb_base: %lx %lx", UNM_PCI_CRBSPACE,
		      PCI_OFFSET_SECOND_RANGE(adapter, UNM_PCI_CRBSPACE));
        nx_nic_print6(adapter, "cam base: %lx %lx", UNM_CRB_CAM,
		      pci_base_offset(adapter, UNM_CRB_CAM));
        nx_nic_print6(adapter, "cam RAM: %lx %lx", UNM_CAM_RAM_BASE,
		      pci_base_offset(adapter, UNM_CAM_RAM_BASE));
        nx_nic_print6(adapter, "NIC base:%lx %lx\n",NIC_CRB_BASE,
		      pci_base_offset(adapter, NIC_CRB_BASE));


	nx_nic_print6(adapter, "Command Peg ready..waiting for rcv peg\n");

	/* Synchronize with Receive peg */
	err = receive_peg_ready(adapter);
	if (err) {
		return err;
	}


        nx_nic_print6(adapter, "Receive Peg ready too. starting stuff\n");

	addr = nx_alloc(adapter,
			((sizeof(cmdDescType0_t) * adapter->MaxTxDescCount)
			 + sizeof(uint32_t)),
                        (dma_addr_t *)&hw->cmdDesc_physAddr,
			&adapter->ahw.cmdDesc_pdev);
        if (addr == NULL) {
                nx_nic_print3(adapter, "bad return from "
			      "pci_alloc_consistent\n");
                err = -ENOMEM;
                goto done;
        }

        adapter->cmdConsumer = (uint32_t *)(((char *) addr) +
					    (sizeof(cmdDescType0_t) *
					     adapter->MaxTxDescCount));
	/*changed right know*/
	adapter->crb_addr_cmd_consumer =
		(((unsigned long)hw->cmdDesc_physAddr) +
		 (sizeof(cmdDescType0_t) * adapter->MaxTxDescCount));

        nx_nic_print6(adapter, "cmdDesc_physAddr: 0x%llx\n",
		      (U64)hw->cmdDesc_physAddr);

#ifdef EPG_WORKAROUND
	pause_addr = nx_alloc(adapter, 512, (dma_addr_t *) & hw->pause_physAddr,
			      &hw->pause_pdev);

	if (pause_addr == NULL) {
                nx_nic_print3(adapter, "bad return from nx_alloc\n");
		err = -ENOMEM;
		goto done;
	}
	hw->pauseAddr = (char *)pause_addr;
	{
		uint64_t *ptr = (uint64_t *) pause_addr;
		*ptr++ = 0ULL;
		*ptr++ = 0ULL;
		*ptr++ = 0x200ULL;
		*ptr++ = 0x0ULL;
		*ptr++ = 0x2200010000c28001ULL;
		*ptr++ = 0x0100088866554433ULL;
	}
#endif

        hw->cmdDescHead = (cmdDescType0_t *)addr;

        adapter->nx_lic_dma.addr =
                pci_alloc_consistent(adapter->ahw.pdev,
                                     sizeof(nx_finger_print_t),
                                     &adapter->nx_lic_dma.phys_addr);

        if (adapter->nx_lic_dma.addr == NULL) {
                nx_nic_print3(adapter, "NX_LIC_RD: bad return from "
                              "pci_alloc_consistent\n");
                err = -ENOMEM;
                goto done;
        }

#ifdef UNM_NIC_SNMP
	adapter->snmp_stats_dma.addr =
		pci_alloc_consistent(adapter->ahw.pdev,
				     sizeof(struct unm_nic_snmp_ether_stats),
				     &adapter->snmp_stats_dma.phys_addr);
	if (adapter->snmp_stats_dma.addr == NULL) {
		nx_nic_print3(adapter, "SNMP: bad return from "
			      "pci_alloc_consistent\n");
		err = -ENOMEM;
		goto done;
	}
#endif

	/*
	 * Need to check how many CPUs are available and use only that
	 * many rings.
	 */
	err = nx_nic_create_rx_ctx(adapter->netdev);
	if (err < 0) {
                goto done;
        }

	if ((err = unm_nic_new_tx_context_prepare(adapter))) {
		goto done;
	}
      done:
	if (err) {
		unm_nic_free_ring_context(adapter);
	}

	return err;
}

static int unm_nic_new_rx_context_prepare(struct unm_adapter_s *adapter,
					  nx_host_rx_ctx_t *rx_ctx)
{
	nx_host_sds_ring_t *sds_ring = NULL;
	nx_host_rds_ring_t *rds_ring = NULL;
	int retval = 0;
	int i = 0;
	int nsds_rings = rx_ctx->num_sds_rings;
	int nrds_rings = NUM_RCV_DESC_RINGS;
	struct nx_dma_alloc_s hostrq;
	struct nx_dma_alloc_s hostrsp;

	//rx_ctx->chaining_allowed = 1;

	if (nx_fw_cmd_create_rx_ctx_alloc_dma(adapter->nx_dev, nrds_rings,
					      nsds_rings, &hostrq,
					      &hostrsp) == NX_RCODE_SUCCESS) {

		rx_ctx->chaining_allowed = rx_chained;
		retval = nx_fw_cmd_create_rx_ctx(rx_ctx, &hostrq, &hostrsp);

		nx_fw_cmd_create_rx_ctx_free_dma(adapter->nx_dev,
						 &hostrq, &hostrsp);
	}

	if (retval != NX_RCODE_SUCCESS) {
		nx_nic_print3(adapter, "Unable to create the "
			      "rx context, code %d %s\n",
			      retval, nx_errorcode2string(retval));
		goto failure_rx_ctx;
	}

	rds_ring = rx_ctx->rds_rings;
	for (i = 0; i < nrds_rings; i++) {
		rds_ring[i].host_rx_producer =
		    UNM_NIC_REG(rds_ring[i].host_rx_producer - 0x0200);
	}

	sds_ring = rx_ctx->sds_rings;
	for (i = 0; i < nsds_rings; i++) {
		uint32_t reg = 0;
		reg = UNM_NIC_REG(sds_ring[i].host_sds_consumer - 0x200);
                sds_ring[i].host_sds_consumer = reg;
		reg = UNM_NIC_REG(sds_ring[i].interrupt_crb - 0x200);
                sds_ring[i].interrupt_crb =  reg;
	}

  failure_rx_ctx:
	return retval;
}

static int unm_nic_new_rx_context_destroy(struct unm_adapter_s *adapter)
{
	nx_host_rx_ctx_t *rx_ctx;
	int retval = 0;
	int i;

	if (adapter->nx_dev == NULL) {
		nx_nic_print3(adapter, "nx_dev is NULL\n");
		return 1;
	}

	for (i = 0; i < adapter->nx_dev->active_rx_ctxs; ) {

		rx_ctx = adapter->nx_dev->rx_ctxs[i];
		if (rx_ctx == NULL) {
			continue;
		}

		retval = nx_fw_cmd_destroy_rx_ctx(rx_ctx, NX_DESTROY_CTX_RESET);

		if (retval) {
			nx_nic_print4(adapter, "Unable to destroy the "
				      "rx context, code %d %s\n", retval,
				      nx_errorcode2string(retval));
		}
		i++;

	}

	return 0;
}

static int unm_nic_new_tx_context_prepare(struct unm_adapter_s *adapter)
{
	nx_host_tx_ctx_t *tx_ctx = NULL;
	int retval = 0;
	int ncds_ring = 1;
	struct nx_dma_alloc_s hostrq;
	struct nx_dma_alloc_s hostrsp;

	retval = nx_fw_cmd_create_tx_ctx_alloc(adapter->nx_dev,
					       ncds_ring,
					       &tx_ctx);
        if (retval) {
                nx_nic_print4(adapter, "Could not allocate memory "
			      "for tx context\n");
                goto failure_tx_ctx;
        }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,8)
	tx_ctx->msi_index = adapter->msix_entries[adapter->portnum].entry;
#endif
#if 0
	tx_ctx->interrupt_ctl = 0;
	tx_ctx->msi_index = 0;
#endif
	tx_ctx->cmd_cons_dma_addr = adapter->crb_addr_cmd_consumer;
	tx_ctx->dummy_dma_addr = adapter->dummy_dma.phys_addr;

	tx_ctx->cds_ring[0].card_tx_consumer = NULL;
	tx_ctx->cds_ring[0].host_addr = NULL;
	tx_ctx->cds_ring[0].host_phys = adapter->ahw.cmdDesc_physAddr;
	tx_ctx->cds_ring[0].ring_size = adapter->MaxTxDescCount;
	
	if (nx_fw_cmd_create_tx_ctx_alloc_dma(adapter->nx_dev, ncds_ring,
					      &hostrq, &hostrsp) ==
	    NX_RCODE_SUCCESS) {

		retval = nx_fw_cmd_create_tx_ctx(tx_ctx, &hostrq, &hostrsp);

		nx_fw_cmd_create_tx_ctx_free_dma(adapter->nx_dev,
						 &hostrq, &hostrsp);
	}

	if (retval != NX_RCODE_SUCCESS) {
		nx_nic_print4(adapter, "Unable to create the tx "
			      "context, code %d %s\n", retval,
			      nx_errorcode2string(retval));
		nx_fw_cmd_create_tx_ctx_free(adapter->nx_dev, tx_ctx);
		goto failure_tx_ctx;
	}

	adapter->crb_addr_cmd_producer =
		UNM_NIC_REG(tx_ctx->cds_ring->host_tx_producer - 0x200);
	
 failure_tx_ctx:
	return retval;
}

static int unm_nic_new_tx_context_destroy(struct unm_adapter_s *adapter)
{
	nx_host_tx_ctx_t *tx_ctx;
	int retval = 0;
	int i;

	if (adapter->nx_dev == NULL) {
		nx_nic_print4(adapter, "nx_dev is NULL\n");
		return 1;
	}

	for (i = 0; i < adapter->nx_dev->alloc_tx_ctxs; i++) {

		tx_ctx = adapter->nx_dev->tx_ctxs[i];
		if (tx_ctx == NULL) {
			continue;
		}

		retval = nx_fw_cmd_destroy_tx_ctx(tx_ctx, NX_DESTROY_CTX_RESET);

		if (retval) {
			nx_nic_print4(adapter, "Unable to destroy the "
				      "tx context, code %d %s\n", retval,
				      nx_errorcode2string(retval));
		}

		nx_fw_cmd_create_tx_ctx_free(adapter->nx_dev, tx_ctx);
	}

	return 0;
}

void nx_free_tx_resources(struct unm_adapter_s *adapter)
{
	int i, j;
	struct unm_cmd_buffer *cmd_buff;
	struct unm_skb_frag *buffrag;

	cmd_buff = adapter->cmd_buf_arr;
	for (i = 0; i < adapter->MaxTxDescCount; i++) {
		buffrag = cmd_buff->fragArray;
		if (buffrag->dma) {
			pci_unmap_single(adapter->pdev, buffrag->dma,
					 buffrag->length, PCI_DMA_TODEVICE);
			buffrag->dma = (uint64_t) NULL;
		}
		for (j = 0; j < cmd_buff->fragCount; j++) {
			buffrag++;
			if (buffrag->dma) {
				pci_unmap_page(adapter->pdev, buffrag->dma,
						buffrag->length,
						PCI_DMA_TODEVICE);
				buffrag->dma = (uint64_t)NULL;
			}
		}
		/* Free the skb we received in unm_nic_xmit_frame */
		if (cmd_buff->skb) {
			dev_kfree_skb_any(cmd_buff->skb);
			cmd_buff->skb = NULL;
		}
		cmd_buff++;
	}
}

/*
 * This will be called when all the ports of the adapter are removed.
 * This will cleanup and disable interrupts and irq.
 */
static void unm_nic_down(struct unm_adapter_s *adapter)
{

	adapter->state = PORT_DOWN;
	nx_free_tx_resources(adapter);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	FLUSH_SCHEDULED_WORK();
#endif
	del_timer_sync(&adapter->watchdog_timer);
}

/**
 * unm_nic_close - Disables a network interface entry point
 **/
static int unm_nic_close(struct net_device *netdev)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);

	/* wait here for poll to complete */
	//netif_poll_disable(netdev);

	netif_carrier_off(netdev);
	netif_stop_queue(netdev);
	unm_nic_down(adapter);
	/*del_timer(&unm_nic_timer); */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0) && !defined(ESX))
	module_put(THIS_MODULE);
#endif

	return 0;
}

static int unm_nic_set_mac(struct net_device *netdev, void *p)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;
	int ret;

	if (netif_running(netdev))
		return -EBUSY;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	ret = unm_nic_macaddr_set(adapter, addr->sa_data);
	if (!ret) {
		memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
		memcpy(adapter->mac_addr, addr->sa_data, netdev->addr_len);
	}

	return ret;
}

static int nx_p3_nic_add_mac(struct unm_adapter_s *adapter, __u8 * addr,
			     mac_list_t ** add_list, mac_list_t ** del_list)
{
	mac_list_t *cur, *prev;

	/* if in del_list, move it to adapter->mac_list */
	for (cur = *del_list, prev = NULL; cur;) {
		if (memcmp(addr, cur->mac_addr, ETH_ALEN) == 0) {
			if (prev == NULL) {
				*del_list = cur->next;
			} else {
				prev->next = cur->next;
			}
			cur->next = adapter->mac_list;
			adapter->mac_list = cur;
			return 0;
		}
		prev = cur;
		cur = cur->next;
	}

	/* make sure to add each mac address only once */
	for (cur = adapter->mac_list; cur; cur = cur->next) {
		if (memcmp(addr, cur->mac_addr, ETH_ALEN) == 0) {
			return 0;
		}
	}
	/* not in del_list, create new entry and add to add_list */
	cur = kmalloc(sizeof(*cur), in_atomic()? GFP_ATOMIC : GFP_KERNEL);
	if (cur == NULL) {
		nx_nic_print3(adapter, "cannot allocate memory. MAC "
			      "filtering may not work properly from now.\n");
		return -1;
	}

	memcpy(cur->mac_addr, addr, ETH_ALEN);
	cur->next = *add_list;
	*add_list = cur;
	return 0;
}

static void nx_p3_nic_set_multi(struct net_device *netdev)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	mac_list_t *cur, *next, *del_list, *add_list = NULL;
	struct dev_mc_list *mc_ptr;
	__u8 bcast_addr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	if (adapter->ahw.revision_id >= NX_P3_A2) {
		nx_p3_nic_set_promisc(adapter);
	} else {
		unm_nic_set_promisc_mode(adapter);
	}

	/*
	 * Programming mac addresses will automaticly enabling L2 filtering.
	 * HW will replace timestamp with L2 conid when L2 filtering is
	 * enabled. This causes problem for LSA. Do not enabling L2 filtering
	 * until that problem is fixed.
	 */

	del_list = adapter->mac_list;
	adapter->mac_list = NULL;
	if (((netdev->flags & IFF_PROMISC) == 0) &&
	    netdev->mc_count <= adapter->max_mc_count) {

		nx_p3_nic_add_mac(adapter, adapter->mac_addr, &add_list,
				  &del_list);
		if (netdev->mc_count > 0) {
			nx_p3_nic_add_mac(adapter, bcast_addr, &add_list,
					  &del_list);
			for (mc_ptr = netdev->mc_list; mc_ptr;
			     mc_ptr = mc_ptr->next) {
				nx_p3_nic_add_mac(adapter, mc_ptr->dmi_addr,
						  &add_list, &del_list);
			}
		}
	} else {
		return;
	}

	for (cur = del_list; cur;) {
		nx_os_pf_remove_l2_mac(adapter->nx_dev,
				       adapter->nx_dev->rx_ctxs[0],
				       cur->mac_addr);
		next = cur->next;
		kfree(cur);
		cur = next;
	}
	for (cur = add_list; cur;) {
		nx_os_pf_add_l2_mac(adapter->nx_dev,
				    adapter->nx_dev->rx_ctxs[0],
				    cur->mac_addr);
		next = cur->next;
		cur->next = adapter->mac_list;
		adapter->mac_list = cur;
		cur = next;
	}
}

static void nx_p2_nic_set_multi(struct net_device *netdev)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	struct dev_mc_list *mc_ptr;
	__u8 null_addr[6] = { 0, 0, 0, 0, 0, 0 };
	int index = 0;

	if (netdev->flags & IFF_PROMISC ||
	    netdev->mc_count > adapter->max_mc_count) {

		unm_nic_set_promisc_mode(adapter);

		/* Full promiscuous mode */
		unm_nic_disable_mcast_filter(adapter);

		return;
	}

	if (netdev->mc_count == 0) {
		unm_nic_unset_promisc_mode(adapter);
		unm_nic_disable_mcast_filter(adapter);
		return;
	}

	unm_nic_set_promisc_mode(adapter);
	unm_nic_enable_mcast_filter(adapter);

	for (mc_ptr = netdev->mc_list; mc_ptr; mc_ptr = mc_ptr->next, index++)
		unm_nic_set_mcast_addr(adapter, index, mc_ptr->dmi_addr);

	if (index != netdev->mc_count) {
		nx_nic_print4(adapter, "Multicast address count mismatch\n");
	}

	/* Clear out remaining addresses */
	for (; index < adapter->max_mc_count; index++) {
		unm_nic_set_mcast_addr(adapter, index, null_addr);
	}
}

/*
 * Send the interrupt coalescing parameter set by ethtool to the card.
 */
int nx_nic_config_intr_coalesce(struct unm_adapter_s *adapter)
{
	nic_request_t req;
	int rv = 0;

	memcpy(&req.body, &adapter->coal, sizeof(req.body));
	req.opcode = NX_NIC_HOST_REQUEST;
	req.body.cmn.req_hdr.opcode = NX_NIC_H2C_OPCODE_CONFIG_INTR_COALESCE;
	req.body.cmn.req_hdr.comp_id = 0;
	req.body.cmn.req_hdr.ctxid = adapter->portnum;
	req.body.cmn.req_hdr.need_completion = 0;
	rv = nx_nic_send_cmd_descs(adapter->netdev, (cmdDescType0_t *) & req,
				   1);
	if (rv) {
		nx_nic_print3(adapter, "Setting Interrupt Coalescing "
			      "parameters failed\n");
	}
	return (rv);
}

static inline int not_aligned(unsigned long addr)
{
	return (addr & 0x7);
}

static int unm_nic_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	unsigned int nr_frags = 0, vlan_frag = 0;
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	struct _hardware_context *hw = &adapter->ahw;
	unsigned int firstSegLen;
	struct unm_skb_frag *buffrag;
	unsigned int i;

	uint32_t producer = 0;
	uint32_t saved_producer = 0;
	cmdDescType0_t *hwdesc;
	int k;
	struct unm_cmd_buffer *pbuf = NULL;
	static int dropped_packet = 0;
	int fragCount;
	uint32_t MaxTxDescCount = 0;
	uint32_t lastCmdConsumer = 0;
	int no_of_desc;
#ifndef UNM_NIC_NAPI
	unsigned long flags;
#endif
	unsigned int tagged = 0;
	struct nx_vlan_buffer *vlan_buf;
	unsigned int vlan_copy = 0;
	int do_bounce;
#ifdef UNM_NIC_HW_VLAN	
	uint32_t vlan_tag = 0;
	struct vlan_ethhdr *v = NULL;
#endif

	adapter->stats.xmitcalled++;

	if (unlikely(skb->len <= 0)) {
		dev_kfree_skb_any(skb);
		adapter->stats.badskblen++;
		return 0;
	}

	nr_frags = skb_shinfo(skb)->nr_frags;
	fragCount = 1 + nr_frags;


#ifdef UNM_NIC_HW_VLAN
	if(vlan_tx_tag_present(skb)) {
		vlan_tag = vlan_tx_tag_get(skb);
		if (vlan_tag == 0) {
			printk(KERN_ALERT"Error retrieving vlan tag\n");
			goto drop_packet;
		}
		if (skb_headroom(skb) < VLAN_HLEN) {
			if (skb_cow(skb, VLAN_HLEN) < 0) {
				printk(KERN_ALERT"skb_cow() failed..dropping packect\n");
				goto drop_packet;
			}
		}
		v = (struct vlan_ethhdr *)skb_push(skb, VLAN_HLEN);
		memmove(skb->data, skb->data + VLAN_HLEN, 2 * ETH_ALEN);
		v->h_vlan_proto = __constant_htons(ETH_P_8021Q);
		v->h_vlan_TCI = htons(vlan_tag);
		tagged = 1;
	}
#else
	tagged = is_packet_tagged(skb);
#endif
	if (fragCount > (MAX_BUFFERS_PER_CMD - tagged)) {

		int i;
		int delta = 0;
		struct skb_frag_struct *frag;

		for (i = 0; i < (fragCount - (MAX_BUFFERS_PER_CMD - tagged));
		     i++) {
			frag = &skb_shinfo(skb)->frags[i];
			delta += frag->size;
		}

		if (!__pskb_pull_tail(skb, delta)) {
			goto drop_packet;
		}

		fragCount = 1 + skb_shinfo(skb)->nr_frags;
	}

	firstSegLen = skb->len - skb->data_len;
/* TODO:This code get executed only when packet is tagged & is only meant
	 * to support vlan in  __VMKERNEL_MODULE__ & is not needed for UNM_NIC_HW_VLAN
	 * this is no more the case but still remove it later
	 */
#ifdef ESX
	if(NX_IS_REVISION_P2(adapter->ahw.revision_id) && tagged) {
		if (skb->ip_summed && not_aligned((unsigned long)(skb->data))) {
			if (firstSegLen > HDR_CP) {
				vlan_frag = 1;
				fragCount++;
			}
			vlan_copy = 1;
		}
	}
#endif

	/*
	 * Everything is set up. Now, we just need to transmit it out.
	 * Note that we have to copy the contents of buffer over to
	 * right place. Later on, this can be optimized out by de-coupling the
	 * producer index from the buffer index.
	 */
	TX_LOCK(&adapter->tx_lock, flags);
	producer = adapter->cmdProducer;
	/* There 4 fragments per descriptor */
	no_of_desc = (fragCount + 3) >> 2;
#ifdef UNM_NETIF_F_TSO
	if (TSO_ENABLED(netdev)) {
		if (TSO_SIZE(skb_shinfo(skb)) > 0) {
                        struct tcphdr *tmp_th = NULL;
                        U64 tcpHdrOffset = TCP_HDR_OFFSET(skb) ;
                        U64 tcp_opt_len = 0;
                        U64 tcphdr_size  = 0;
                        U64 tcp_offset  = 0;
                        U64 totalHdrLength = 0;

                        tmp_th =(struct tcphdr *) (SKB_MAC_HDR(skb) + tcpHdrOffset);

                        /* Calculate the TCP header size */
                        tcp_opt_len = ((tmp_th->doff - 5) * 4);
                        tcphdr_size = sizeof(struct tcphdr) + tcp_opt_len;
                        tcp_offset = tcpHdrOffset + tcphdr_size;

                        totalHdrLength = sizeof(struct ethhdr) +
                                            (tagged * sizeof(struct vlan_hdr)) +
                                            (NW_HDR_SIZE) +
                                            (TCP_HDR(skb)->doff * sizeof(u32));

                        if(rarely(tmp_th->doff < 5)) {
                                printk("%s: %s: Dropping packet, Illegal TCP header size in skb %d \n",
                                       unm_nic_driver_name, netdev->name, tmp_th->doff);
                                goto drop_packet;
                        }
                        if (rarely(tcphdr_size > MAX_TCP_HDR)) {
                                printk("%s: %s: Dropping packet, Too much TCP header %lld > %d\n",
                                        unm_nic_driver_name, netdev->name, tcphdr_size, MAX_TCP_HDR);
                                goto drop_packet;
                        }

                        if (rarely(tcp_offset != totalHdrLength)) {
                                printk("%s: %s: Dropping packet, Detected tcp_offset != totalHdrLength: %lld, %lld\n",
                                       unm_nic_driver_name, netdev->name, tcp_offset, totalHdrLength);
                                goto drop_packet;
                        }

			no_of_desc++;
			if ((NW_HDR_SIZE) +
			    (TCP_HDR(skb)->doff * sizeof(u32)) +
			    (tagged * sizeof(struct vlan_hdr)) +
			    sizeof(struct ethhdr) >
			    (sizeof(cmdDescType0_t) - IP_ALIGNMENT_BYTES)) {
				no_of_desc++;
			}
		}
	}
#endif
	k = adapter->cmdProducer;
	MaxTxDescCount = adapter->MaxTxDescCount;
	smp_mb();
	lastCmdConsumer = adapter->lastCmdConsumer;
	if ((k + no_of_desc) >=
	    ((lastCmdConsumer <= k) ? lastCmdConsumer + MaxTxDescCount :
	     lastCmdConsumer)) {
		goto requeue_packet;
	}
	k = get_index_range(k, MaxTxDescCount, no_of_desc);
	adapter->cmdProducer = k;

	/* Copy the descriptors into the hardware    */
	saved_producer = producer;
	hwdesc = &hw->cmdDescHead[producer];
	memset(hwdesc, 0, sizeof(cmdDescType0_t));
	/* Take skb->data itself */
	pbuf = &adapter->cmd_buf_arr[producer];

#ifdef UNM_NETIF_F_TSO
	if (TSO_ENABLED(netdev)) {
		if (TSO_SIZE(skb_shinfo(skb)) > 0) {
			pbuf->mss = TSO_SIZE(skb_shinfo(skb));
			hwdesc->mss = TSO_SIZE(skb_shinfo(skb));
		} else {
			pbuf->mss = 0;
			hwdesc->mss = 0;
		}
	} else
#endif
	{
		pbuf->mss = 0;
		hwdesc->mss = 0;
	}

	pbuf->totalLength = skb->len;
	pbuf->skb = skb;
	pbuf->cmd = TX_ETHER_PKT;
	pbuf->fragCount = fragCount;
	pbuf->port = adapter->portnum;
	buffrag = &pbuf->fragArray[0];

	if (!vlan_copy) {
		do_bounce = try_map_skb_data(adapter, skb, firstSegLen,
					     PCI_DMA_TODEVICE,
					     (dma_addr_t *) & buffrag->dma);
	} else {
		vlan_buf = &(pbuf->vlan_buf);
		if (vlan_frag) {
			memcpy(vlan_buf->data, skb->data, HDR_CP);
			buffrag->dma = vlan_buf->phys;

			hwdesc->buffer2Length = firstSegLen - HDR_CP;
			hwdesc->AddrBuffer2 = virt_to_phys(skb->data + HDR_CP);

			buffrag++;
			buffrag->length = hwdesc->buffer2Length;
			buffrag->dma = hwdesc->AddrBuffer2;

			if (adapter->bounce) {
				int len[MAX_PAGES_PER_FRAG] = {0};
				void *vaddr[MAX_PAGES_PER_FRAG] = {NULL};
				int tries = 0;

				vaddr[0] = (void *) (skb->data + HDR_CP);
				len[0] = hwdesc->buffer2Length;

				while(nx_handle_large_addr(adapter, buffrag,
							&hwdesc->AddrBuffer2,
							vaddr, len, len[0])){
					unsigned long flags;

					unm_process_cmd_ring((unsigned long)adapter);

					if(tries > 0xf) {
						adapter->cmdProducer = saved_producer;
						goto requeue_packet;
					}
					tries++;
				}

			} 
			firstSegLen = HDR_CP;
		} else {
			memcpy(vlan_buf->data, skb->data, firstSegLen);
			buffrag->dma = vlan_buf->phys;
		}
	}

	pbuf->fragArray[0].length = firstSegLen;
	hwdesc->totalLength = skb->len;
	hwdesc->numOfBuffers = fragCount;
	hwdesc->opcode = TX_ETHER_PKT;

        hwdesc->port = adapter->portnum; /* JAMBU: To be removed */

	hwdesc->ctx_id = adapter->portnum;
	hwdesc->buffer1Length = firstSegLen;
	hwdesc->AddrBuffer1 = pbuf->fragArray[0].dma;

#ifdef ESX
	if (adapter->bounce) {
		int len[MAX_PAGES_PER_FRAG] = {0};
		void *vaddr[MAX_PAGES_PER_FRAG] = {NULL};
		int tries = 0;

		vaddr[0] = (void *) (skb->data);
		len[0] = hwdesc->buffer1Length;

		while (nx_handle_large_addr(adapter, &pbuf->fragArray[0],
					&hwdesc->AddrBuffer1,
					vaddr, len, len[0])) {

			unm_process_cmd_ring((unsigned long)adapter);

			if(tries > 0xf)  {
				if (vlan_frag)
					nx_free_frag_bounce_buf(adapter, buffrag);

				adapter->cmdProducer = saved_producer;
				goto requeue_packet;
			}
			tries++;
		}
	} 
#endif

	for (i = 1, k = (1 + vlan_frag); i < (fragCount - vlan_frag); i++, k++) {
		struct skb_frag_struct *frag;
		int len, temp_len;
		unsigned long offset;
		dma_addr_t temp_dma = 0;

		/* move to next desc. if there is a need */
		if ((k & 0x3) == 0) {
			k = 0;

			producer = get_next_index(producer,
						  adapter->MaxTxDescCount);
			hwdesc = &hw->cmdDescHead[producer];
			memset(hwdesc, 0, sizeof(cmdDescType0_t));
		}
		frag = &skb_shinfo(skb)->frags[i - 1];
		len = frag->size;
		offset = frag->page_offset;

		temp_len = len;

		do_bounce = try_map_frag_page(adapter, frag->page,
					      offset, len,
					      PCI_DMA_TODEVICE, &temp_dma);

		buffrag++;

#ifdef ESX
		temp_dma    = page_to_phys(frag->page) + offset;
		if (adapter->bounce) {

			int len[MAX_PAGES_PER_FRAG] = {0};
			void *vaddr[MAX_PAGES_PER_FRAG] = {NULL};
			int p_i = 0; // Page index;                     
			int len_rem = temp_len;
			int tries = 0;
			if((temp_dma + temp_len)  >= adapter->dma_mask) {
				while(1) {
					if (len_rem <= PAGE_SIZE) {
						vaddr[p_i] =
							ESX_PHYS_TO_KMAP(temp_dma + p_i*PAGE_SIZE,
									len_rem);
						len[p_i] = len_rem;
						break;
					}

					vaddr[p_i] =
						ESX_PHYS_TO_KMAP(temp_dma + p_i*PAGE_SIZE,
								PAGE_SIZE);
					len[p_i] = PAGE_SIZE;
					p_i++;
					len_rem -= PAGE_SIZE;

				} 

				while (nx_handle_large_addr(adapter, buffrag, &temp_dma,
							vaddr, len, temp_len)) {
					int j, total;
					total = i + vlan_frag;

					unm_process_cmd_ring((unsigned long)adapter);

					if (tries > 0xf) {
						for(j = 0; j < total; j++) {
							nx_free_frag_bounce_buf(adapter,
									&pbuf->fragArray[j]);
						}

						adapter->cmdProducer = saved_producer;

						p_i = 0;
						while (vaddr[p_i]) {
							ESX_PHYS_TO_KMAP_FREE(vaddr[p_i]);
							p_i++;
						}
						goto requeue_packet;
					}
					tries++;
				}

				p_i = 0;
				while (vaddr[p_i]) {
					ESX_PHYS_TO_KMAP_FREE(vaddr[p_i]);
					p_i++;
				}
			} else {
				buffrag->bounce_buf[0] = NULL;
			}
		}

#endif

		buffrag->dma = temp_dma;
		buffrag->length = temp_len;

                nx_nic_print7(adapter, "for loop. i = %d k = %d\n", i, k);
                switch (k) {
                    case 0:
                            hwdesc->buffer1Length = temp_len;
                            hwdesc->AddrBuffer1   = temp_dma ;
                            break;
                    case 1:
                            hwdesc->buffer2Length = temp_len;
                            hwdesc->AddrBuffer2   = temp_dma ;
                            break;
                    case 2:
                            hwdesc->buffer3Length = temp_len;
                            hwdesc->AddrBuffer3   = temp_dma ;
                            break;
                    case 3:
                            hwdesc->buffer4Length = temp_len;
                            hwdesc->AddrBuffer4   = temp_dma ;
                            break;
                }
                frag ++;
        }
        producer = get_next_index(producer, adapter->MaxTxDescCount);

#ifdef UNM_NETIF_F_TSO
	/* might change opcode to TX_TCP_LSO */
	if ((TSO_ENABLED(netdev)) && (hw->cmdDescHead[saved_producer].mss)) {
		unm_tso_check(adapter, &hw->cmdDescHead[saved_producer],
			      tagged, skb);
         } else {
		unm_tx_csum(adapter, &hw->cmdDescHead[saved_producer], skb);
	 }
#else
		unm_tx_csum(adapter, &hw->cmdDescHead[saved_producer], skb);
#endif

#if (defined(ESX) || defined(UNM_NIC_HW_VLAN))
	if (tagged) {
		hw->cmdDescHead[saved_producer].flags |= FLAGS_VLAN_TAGGED;
	}
#endif

	/* For LSO, we need to copy the MAC/IP/TCP headers into
	 * the descriptor ring
	 */
	if ((hw->cmdDescHead[saved_producer].opcode == TX_TCP_LSO) ||
	    (hw->cmdDescHead[saved_producer].opcode == TX_TCP_LSO6)) {
		int hdrLen, firstHdrLen, moreHdr;
		hdrLen = hw->cmdDescHead[saved_producer].totalHdrLength;
		if (hdrLen > (sizeof(cmdDescType0_t) - IP_ALIGNMENT_BYTES)) {
			firstHdrLen = sizeof(cmdDescType0_t) -
			    IP_ALIGNMENT_BYTES;
			moreHdr = 1;
		} else {
			firstHdrLen = hdrLen;
			moreHdr = 0;
		}
		/* copy the MAC/IP/TCP headers to the cmd descriptor list */
		hwdesc = &hw->cmdDescHead[producer];

		/* copy the first 64 bytes */
		memcpy(((void *)hwdesc) + IP_ALIGNMENT_BYTES,
		       (void *)(skb->data), firstHdrLen);
		producer = get_next_index(producer, MaxTxDescCount);

		if (moreHdr) {
			hwdesc = &hw->cmdDescHead[producer];
			/* copy the next 64 bytes - should be enough except
			 * for pathological case
			 */
			memcpy((void *)hwdesc, (void *)(skb->data) +
			       firstHdrLen, hdrLen - firstHdrLen);

			producer = get_next_index(producer, MaxTxDescCount);
		}
	}
	adapter->stats.txbytes += hw->cmdDescHead[saved_producer].totalLength;

	hw->cmdProducer = adapter->cmdProducer;

	read_lock(&adapter->adapter_lock);
	unm_nic_update_cmd_producer(adapter, adapter->cmdProducer);
	read_unlock(&adapter->adapter_lock);


	adapter->stats.xmitfinished++;

	netdev->trans_start = jiffies;

        nx_nic_print7(adapter, "wrote CMD producer %x to phantom\n", producer);

        goto unm_nic_xmit_success;

requeue_packet:
	netif_stop_queue(netdev);
	adapter->status |= NETDEV_STATUS;
	TX_UNLOCK(&adapter->tx_lock, flags);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
	return NETDEV_TX_BUSY;
#else
	return 1;
#endif

drop_packet:
	adapter->stats.txdropped++;
        dev_kfree_skb_any(skb);
        if ((++dropped_packet & 0xff) == 0xff) {
                nx_nic_print6(adapter, "%s droppped packets = %d\n",
			      netdev->name, dropped_packet);
        }
        //netif_stop_queue(netdev);
unm_nic_xmit_success:
	TX_UNLOCK(&adapter->tx_lock, flags);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
	return NETDEV_TX_OK;
#else
	return 0;
#endif
}

static int
unm_loopback_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	unsigned long flags;
	int rv;

	local_irq_save(flags);
	rv = unm_nic_xmit_frame(skb, netdev);
	local_irq_restore(flags);
	return rv;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static void unm_watchdog(unsigned long v)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)v;
	SCHEDULE_WORK(&adapter->watchdog_task);
}
#endif

static int unm_nic_check_temp(struct unm_adapter_s *adapter)
{
	uint32_t temp, temp_state, temp_val;
	int rv = 0;
	if((adapter -> ahw.revision_id) == NX_P3_A2 ||
		(adapter -> ahw.revision_id) == NX_P3_A0) {
		return 0;
	}

	temp = adapter->unm_nic_pci_read_normalize(adapter, CRB_TEMP_STATE);

	temp_state = nx_get_temp_state(temp);
	temp_val = nx_get_temp_val(temp);

	if (temp_state == NX_TEMP_PANIC) {
                netdev_list_t *this;
                nx_nic_print1(adapter, "Device temperature %d degrees C "
			      "exceeds maximum allowed. Hardware has been "
			      "shut down.\n",
			      temp_val);
		for (this = adapter->netlist; this != NULL;
		     this = this->next) {
			netif_carrier_off(this->netdev);
			netif_stop_queue(this->netdev);
		}
#ifdef UNM_NIC_SNMP_TRAP
		unm_nic_send_snmp_trap(NX_TEMP_PANIC);
#endif
		rv = 1;
	} else if (temp_state == NX_TEMP_WARN) {
		if (adapter->temp == NX_TEMP_NORMAL) {
			nx_nic_print1(adapter, "Device temperature %d degrees "
				      "C exceeds operating range. Immediate "
				      "action needed.\n", temp_val);
#ifdef UNM_NIC_SNMP_TRAP
			unm_nic_send_snmp_trap(NX_TEMP_WARN);
#endif
		}
	} else {
		if (adapter->temp == NX_TEMP_WARN) {
			nx_nic_print6(adapter, "Device temperature is now %d "
				      "degrees C in normal range.\n",
				      temp_val);
		}
	}
	adapter->temp = temp_state;
	return rv;
}

static int nx_p3_set_vport_miss_mode(struct unm_adapter_s *adapter, int mode)
{
	nic_request_t req;

	req.opcode = NX_NIC_HOST_REQUEST;
	req.body.cmn.req_hdr.opcode = 
				NX_NIC_H2C_OPCODE_PROXY_SET_VPORT_MISS_MODE;
	req.body.cmn.req_hdr.comp_id = 0;
	req.body.cmn.req_hdr.ctxid = adapter->portnum;
	req.body.vport_miss_mode.mode = mode;

	return (nx_nic_send_cmd_descs(adapter->netdev, 
				      (cmdDescType0_t *) & req,
				      1));

}

static int nx_p3_nic_set_promisc(struct unm_adapter_s *adapter)
{
	struct net_device *netdev;
	__uint32_t mode = VPORT_MISS_MODE_DROP;

	netdev = adapter->netdev;

	/* bother the f/w only if flags have changed */
	if (((netdev->flags & IFF_PROMISC) | 
     	     (netdev->flags & IFF_ALLMULTI)) ^
		((adapter->promisc & IFF_PROMISC) | 
		(adapter->promisc & IFF_ALLMULTI))) {

		adapter->promisc = ((netdev->flags & IFF_PROMISC) | 
					(netdev->flags & IFF_ALLMULTI));

		if (netdev->flags & IFF_PROMISC)
			mode |= VPORT_MISS_MODE_ACCEPT_ALL;
		if (netdev->flags & IFF_ALLMULTI)
			mode |= VPORT_MISS_MODE_ACCEPT_MULTI;

		nx_p3_set_vport_miss_mode(adapter, mode);
	}
	return 0;
}

static void unm_watchdog_task(TASK_PARAM adapid)
{
	struct net_device *netdev;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
	struct unm_adapter_s *adapter = container_of(adapid,
						     struct unm_adapter_s,
						     watchdog_task);
#else
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)adapid;
#endif
//	unsigned long flags;

	TX_TIMEOUT_LOCK(adapter);
#ifdef ESX
	if (atomic_read(&adapter->tx_timeout)) {
		goto out;
	}
#endif
	if ((adapter->portnum == 0) && unm_nic_check_temp(adapter)) {
		/*We return without turning on the netdev queue as there
		 *was an overheated device
		 */
		goto out;
	}
	if (adapter->driver_mismatch) {
		/*We return without turning on the netdev queue as there
		 *was a mismatch in driver and firmware version
		 */
		goto out;
	}

//	UNM_WRITE_LOCK_IRQS(&adapter->adapter_lock, flags);
//	unm_nic_pci_change_crbwindow(adapter, 1);
//	UNM_WRITE_UNLOCK_IRQR(&adapter->adapter_lock, flags);

	unm_nic_handle_phy_intr(adapter);

	netdev = adapter->netdev;
	if ((netdev->flags & IFF_UP) && !netif_carrier_ok(netdev) &&
	    unm_link_ok(adapter)) {
		nx_nic_print6(adapter, "(port %d), Link is up\n",
			      adapter->portnum);
		netif_carrier_on(netdev);
		netif_wake_queue(netdev);
	} else if (!(netdev->flags & IFF_UP) && netif_carrier_ok(netdev)) {
		nx_nic_print6(adapter, "(port %d) Link is Down\n",
			      adapter->portnum);
		netif_carrier_off(netdev);
		netif_stop_queue(netdev);
	}
      out:
	mod_timer(&adapter->watchdog_timer, jiffies + 2 * HZ);
	TX_TIMEOUT_UNLOCK(adapter);
}

#ifdef ESX
static void unm_tx_timeout(struct net_device *netdev)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;
	dev_hold(netdev);
	netif_carrier_off(netdev);
	TX_TIMEOUT_LOCK(adapter);
	atomic_inc(&adapter->tx_timeout);
#ifdef ESX_4X
	SCHEDULE_WORK(adapter->tx_timeout_task);
#else
	schedule_task(adapter->tx_timeout_task);
#endif
	TX_TIMEOUT_UNLOCK(adapter);
}
#else
static void unm_tx_timeout(struct net_device *netdev)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	SCHEDULE_WORK(adapter->tx_timeout_task);
#else
	schedule_task(adapter->tx_timeout_task);
#endif
}
#endif


void nx_nic_handle_tx_timeout(struct unm_adapter_s *adapter) 
{

	struct net_device * netdev = adapter->netdev;
	int index = 0;
	nx_host_tx_ctx_t *tx_ctx;
	struct _hardware_context *hw = &adapter->ahw;
	struct unm_cmd_buffer *cmd_buf_arr = NULL;
	void *addr;
	int err = 0;


	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	adapter->state = PORT_DOWN;

	nx_free_tx_resources(adapter);

	read_lock(&adapter->adapter_lock);
	for (index = 0; index < adapter->nx_dev->alloc_rx_ctxs; index++) {
		if (adapter->nx_dev->rx_ctxs[index] != NULL) {
			unm_nic_disable_all_int(adapter,
					adapter->nx_dev->rx_ctxs[index]);
		}
	}
	read_unlock(&adapter->adapter_lock);

	unm_destroy_pending_cmd_desc(&adapter->pending_cmds);

	nx_free_vlan_buffers(adapter);
	nx_free_tx_vmkbounce_buffers(adapter);

	unm_nic_new_tx_context_destroy(adapter);
	if (adapter->ahw.cmdDescHead != NULL) {
		pci_free_consistent(adapter->ahw.cmdDesc_pdev,
				(sizeof(cmdDescType0_t) *
				 adapter->MaxTxDescCount)
				+ sizeof(uint32_t),
				adapter->ahw.cmdDescHead,
				adapter->ahw.cmdDesc_physAddr);
		adapter->ahw.cmdDescHead = NULL;
	}

	tx_ctx = adapter->nx_dev->tx_ctxs[0];
	nx_fw_cmd_create_tx_ctx_free(adapter->nx_dev, tx_ctx);

	if (adapter->cmd_buf_arr != NULL) {
		vfree(adapter->cmd_buf_arr);
		adapter->cmd_buf_arr = NULL;
	}

	cmd_buf_arr = vmalloc(TX_RINGSIZE(adapter->MaxTxDescCount));
	adapter->cmd_buf_arr = cmd_buf_arr;

	if (cmd_buf_arr == NULL) {
		nx_nic_print3(NULL, "Failed to allocate requested memory for"
				"TX cmd buffer. Setting MaxTxDescCount to 1024.\n");
		adapter->MaxTxDescCount = 1024;
		cmd_buf_arr = vmalloc(TX_RINGSIZE(adapter->MaxTxDescCount));
		adapter->cmd_buf_arr = cmd_buf_arr;
		if (cmd_buf_arr == NULL) {
			nx_nic_print3(NULL, "Failed to allocate memory for the "
					"TX cmd buffer. Check system memory resource "
					"usage.\n");
		return;
		}
	}

	memset(cmd_buf_arr, 0, TX_RINGSIZE(adapter->MaxTxDescCount));

	adapter->ahw.linkup = 0;

	unm_nic_update_cmd_producer(adapter, 0);

	adapter->cmdProducer = 0;
	adapter->lastCmdConsumer = 0;


	unm_init_pending_cmd_desc(&adapter->pending_cmds);

	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	addr = nx_alloc(adapter,
			((sizeof(cmdDescType0_t) * adapter->MaxTxDescCount)
			 + sizeof(uint32_t)),
			(dma_addr_t *)&hw->cmdDesc_physAddr,
			&adapter->ahw.cmdDesc_pdev);
	if (addr == NULL) {
		nx_nic_print3(adapter, "bad return from "
				"pci_alloc_consistent\n");
		return;
	}

	adapter->cmdConsumer = (uint32_t *)(((char *) addr) +
			(sizeof(cmdDescType0_t) *
			 adapter->MaxTxDescCount));
	/*changed right know*/
	adapter->crb_addr_cmd_consumer =
		(((unsigned long)hw->cmdDesc_physAddr) +
		 (sizeof(cmdDescType0_t) * adapter->MaxTxDescCount));


	hw->cmdDescHead = (cmdDescType0_t *)addr;

	 if ((err = unm_nic_new_tx_context_prepare(adapter))) {
		nx_nic_print3(adapter,"Warning :tx context not initialized."
				"Reset Not Complete\n");
		return;
        }

	if ((nx_setup_vlan_buffers(adapter)) != 0) { 
		nx_free_vlan_buffers(adapter); 
		nx_nic_print3(adapter,"Coudln't allocate VLAN buffers\n");
		return;
	} 

	if ((nx_setup_tx_vmkbounce_buffers(adapter)) != 0) { 
		nx_free_tx_vmkbounce_buffers(adapter); 
		nx_nic_print3(adapter,"Coudln't allocate bounce buffers\n");
		return;
	}

	
	read_lock(&adapter->adapter_lock);
        for (index = 0; index < adapter->nx_dev->alloc_rx_ctxs; index++) {
                if (adapter->nx_dev->rx_ctxs[index] != NULL) {
                        unm_nic_enable_all_int(adapter,
                                        adapter->nx_dev->rx_ctxs[index]);
                }
        }
	read_unlock(&adapter->adapter_lock);

	netif_start_queue(netdev);

	adapter->state = PORT_UP;
}



static void unm_tx_timeout_task(TASK_PARAM adapid)
{
	struct net_device *netdev = (struct net_device *)adapid;
#ifdef ESX
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;
	while (atomic_read(&adapter->isr_cnt)) {
		msleep(10);
	}

	nx_nic_handle_tx_timeout(adapter);

#if 0
	nx_disable_nic(adapter);
	nx_enable_nic(adapter);
#endif
	dev_put(netdev);
	atomic_dec(&adapter->tx_timeout);
#else
        struct unm_adapter_s *adapter = (struct unm_adapter_s*)netdev->priv;
#if 0
        unsigned long flags;

        nx_nic_print3(adapter, "transmit timeout, resetting.\n");
        spin_lock_irqsave(&adapter->lock, flags);
        unm_nic_close(netdev);
        unm_nic_open(netdev);
        spin_unlock_irqrestore(&adapter->lock, flags);
        netdev->trans_start = jiffies;
        netif_wake_queue(netdev);
#endif
#endif
        nx_nic_print3(adapter, "transmit timeout, NEED to reset.\n");
}

static void unm_nic_clear_stats(struct unm_adapter_s *adapter)
{
	memset(&adapter->stats, 0, sizeof(adapter->stats));
	return;
}

/*
 * unm_nic_get_stats - Get System Network Statistics
 * @netdev: network interface device structure
 */
static struct net_device_stats *unm_nic_get_stats(struct net_device *netdev)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	struct net_device_stats *stats = &adapter->net_stats;

	memset(stats, 0, sizeof(struct net_device_stats));

#if 0
	int i = 0;
	netxen_pstats_t card_stats;
	nx_nic_get_stats(netdev, &card_stats);

	for (i = 0; i < NETXEN_NUM_PEGS; i++) {
		stats->rx_packets += card_stats.peg[i].port[adapter->portnum].tcp_statistics.TcpInSegs;
		stats->tx_packets += card_stats.peg[i].port[adapter->portnum].tcp_statistics.TcpOutSegs;
		stats->rx_bytes   += card_stats.peg[i].port[adapter->portnum].l2_statistics.L2RxBytes;
		stats->tx_bytes   += card_stats.peg[i].port[adapter->portnum].l2_statistics.L2TxBytes;
	}
#endif

	/* total packets received   */
	stats->rx_packets += adapter->stats.no_rcv;
	/* total packets transmitted    */
	stats->tx_packets += adapter->stats.xmitedframes +
	    adapter->stats.xmitfinished;
	/* total bytes received     */
	stats->rx_bytes += adapter->stats.rxbytes;
	/* total bytes transmitted  */
	stats->tx_bytes += adapter->stats.txbytes;
	/* bad packets received     */
	stats->rx_errors = adapter->stats.rcvdbadskb;
	/* packet transmit problems */
	stats->tx_errors = adapter->stats.nocmddescriptor;
	/* no space in linux buffers    */
	stats->rx_dropped = adapter->stats.updropped;
	/* no space available in linux  */
	stats->tx_dropped = adapter->stats.txdropped;

	return stats;
}

/*
 * unm_nic_change_mtu - Change the Maximum Transfer Unit
 * Returns 0 on success, negative on failure
 */
static int unm_nic_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	int max_mtu;

	if (new_mtu & 0xffff0000)
		return -EINVAL;

        if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
                max_mtu = P3_MAX_MTU;
        } else {
                max_mtu = P2_MAX_MTU;
        }
        if (new_mtu > max_mtu) {
                nx_nic_print3(adapter, "MTU > %d is not supported\n",
			      max_mtu);
                return -EINVAL;
        }

	if (nx_fw_cmd_set_mtu(adapter->nx_dev->rx_ctxs[0],
			  adapter->ahw.pci_func,
			  new_mtu)) {

		return -EIO;
	}

	unm_nic_set_mtu(adapter, new_mtu);
	netdev->mtu = new_mtu;
	adapter->mtu = new_mtu;

	return 0;
}

/*
 *
 */
static inline int unm_nic_clear_int(unm_adapter *adapter,
				    nx_host_sds_ring_t *nxhal_sds_ring)
{
	uint32_t	mask;
	uint32_t	temp;
	uint32_t	our_int;
	uint32_t	status;

        nx_nic_print7(adapter, "Entered ISR Disable \n");

	read_lock(&adapter->adapter_lock);

	/* check whether it's our interrupt */
	if (!UNM_IS_MSI_FAMILY(adapter)) {

		/* Legacy Interrupt case */
		adapter->unm_nic_pci_read_immediate(adapter,
						 ISR_INT_VECTOR, &status);

		if (!(status & adapter->legacy_intr.int_vec_bit)) {
			read_unlock(&adapter->adapter_lock);
			return (-1);
		}

		if (adapter->ahw.revision_id >= NX_P3_B1) {
			adapter->unm_nic_pci_read_immediate(adapter,
						ISR_INT_STATE_REG, &temp);
			if (!ISR_IS_LEGACY_INTR_TRIGGERED(temp)) {
				nx_nic_print7(adapter, "state = 0x%x\n",
					      temp);
				read_unlock(&adapter->adapter_lock);
				return (-1);
			}
		} else if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {

			our_int = adapter->unm_nic_pci_read_normalize(adapter,
							CRB_INT_VECTOR);

			/* FIXME: Assumes pci_func is same as ctx */
			if ((our_int & (0x80 << adapter->portnum)) == 0) {
				if (our_int != 0) {
					/* not our interrupt */
					read_unlock(&adapter->adapter_lock);
					return (-1);
				}
				nx_nic_print3(adapter, "P2 Legacy interrupt "
					      "[bit 0x%x] without SW CRB[0x%x]"
					      " bit being set\n",
					      adapter->legacy_intr.int_vec_bit,
					      our_int);
			}
			temp = our_int & ~((u32)(0x80 << adapter->portnum));
			adapter->unm_nic_pci_write_normalize(adapter,
							     CRB_INT_VECTOR,
							     temp);
		}

		/* claim interrupt */
		temp = 0xffffffff;
		adapter->unm_nic_pci_write_immediate(adapter,
					adapter->legacy_intr.tgt_status_reg,
					&temp);

		adapter->unm_nic_pci_read_immediate(adapter, ISR_INT_VECTOR,
						    &mask);

		/*
		 * Read again to make sure the legacy interrupt message got
		 * flushed out
		 */
		adapter->unm_nic_pci_read_immediate(adapter, ISR_INT_VECTOR,
						    &mask);

	} else if (adapter->flags & UNM_NIC_MSI_ENABLED) {
		/* clear interrupt */
		temp = 0xffffffff;
		adapter->unm_nic_pci_write_immediate(adapter,
					msi_tgt_status[adapter->ahw.pci_func],
					&temp);
	}

	read_unlock(&adapter->adapter_lock);

	nx_nic_print7(adapter, "Done with Disable Int\n");
	return (0);
}

/*
 *
 */
static void unm_nic_disable_int(unm_adapter *adapter,
				nx_host_sds_ring_t *nxhal_sds_ring)
{
	__uint32_t	temp;

	temp = 0;
	adapter->unm_nic_hw_write_wx(adapter, nxhal_sds_ring->interrupt_crb,
				     &temp, 4);
}

void unm_nic_disable_all_int(unm_adapter *adapter,
			     nx_host_rx_ctx_t *nxhal_host_rx_ctx)
{
	int			i;
	nx_host_sds_ring_t      *nxhal_sds_ring = NULL;
	sds_host_ring_t         *host_sds_ring  = NULL;

	if (nxhal_host_rx_ctx == NULL) {
		return;
	}

	for (i = 0; i < nxhal_host_rx_ctx->num_sds_rings; i++) {
		if ((nxhal_host_rx_ctx->sds_rings + i) != NULL) {
			nxhal_sds_ring = &nxhal_host_rx_ctx->sds_rings[i];
			host_sds_ring =
				(sds_host_ring_t *)nxhal_sds_ring->os_data;
			unm_nic_disable_int(adapter, nxhal_sds_ring);
		}
	}
}

void unm_nic_enable_int(unm_adapter *adapter,
			nx_host_sds_ring_t *nxhal_sds_ring)
{
	u32 mask;
	u32 temp;

	nx_nic_print7(adapter, "Entered ISR Enable \n");

	temp = 1;
	adapter->unm_nic_hw_write_wx(adapter,
				     nxhal_sds_ring->interrupt_crb, &temp, 4);

	if (!UNM_IS_MSI_FAMILY(adapter)) {
		mask = 0xfbff;

		adapter->unm_nic_pci_write_immediate(adapter,
                                     adapter->legacy_intr.tgt_mask_reg,
                                     &mask);
	}

	nx_nic_print7(adapter, "Done with enable Int\n");
	return;
}

void unm_nic_enable_all_int(unm_adapter * adapter,
				nx_host_rx_ctx_t *nxhal_host_rx_ctx)
{
	int i;
        nx_host_sds_ring_t      *nxhal_sds_ring = NULL;
        sds_host_ring_t         *host_sds_ring  = NULL;


        for (i = 0; i < nxhal_host_rx_ctx->num_sds_rings; i++) {
                nxhal_sds_ring = &nxhal_host_rx_ctx->sds_rings[i];
                host_sds_ring = (sds_host_ring_t *)nxhal_sds_ring->os_data;
                unm_nic_enable_int(adapter, nxhal_sds_ring);
	}

}

#define UNM_NIC_INT_RESET 0x2004

#define find_diff_among(a,b,range) ((a)<=(b)?((b)-(a)):((b)+(range)-(a)))

#if defined(UNM_NIC_NAPI)
/*
 * Params:
 * adapter	- The interface adapter structure.
 * idx		- The particular status ring context idx.
 *
 * returns: non 0 if there is work for the receive side
 *          0 if there is none
 */
static inline int unm_nic_rx_has_work(struct unm_adapter_s *adapter, int idx)
{
	nx_host_sds_ring_t      *nxhal_sds_ring = &adapter->nx_dev->rx_ctxs[0]->sds_rings[idx];
        sds_host_ring_t         *host_sds_ring = (sds_host_ring_t *)nxhal_sds_ring->os_data;
        statusDesc_t            *sds_desc = (statusDesc_t *)nxhal_sds_ring->host_addr;
        return (sds_desc[host_sds_ring->consumer].owner & STATUS_OWNER_HOST);

}

/*
 * returns: 1 if there is work for the transmit side
 *          0 if there is none
 */
static int unm_nic_tx_has_work(struct unm_adapter_s *adapter)
{
	if (find_diff_among(adapter->lastCmdConsumer,
			    adapter->cmdProducer,
			    adapter->MaxTxDescCount) > 0) {
		/* (adapter->MaxTxDescCount >> 5)) { */

		return 1;

	}

	return 0;
}
#endif

#ifdef	OLD_KERNEL
#define IRQ_HANDLED
#define IRQ_NONE
#endif

/**
 * unm_intr - Interrupt Handler
 * @irq: interrupt number
 * data points to adapter structure (which may be handling more than 1 port
 **/
#ifdef OLD_KERNEL
void unm_intr(int irq, void *data, struct pt_regs *regs)
#elif  LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
irqreturn_t unm_intr(int irq, void *data, struct pt_regs * regs)
#else
irqreturn_t unm_intr(int irq, void *data)
#endif
{
	nx_host_sds_ring_t      *nxhal_sds_ring = (nx_host_sds_ring_t *)data;
        sds_host_ring_t         *host_sds_ring  =
				     (sds_host_ring_t *)nxhal_sds_ring->os_data;
	struct unm_adapter_s *adapter = host_sds_ring->adapter;
	struct net_device *netdev = host_sds_ring->netdev;
	int ret = 0;
	int tx_has_work = 0;

	if (unlikely(!irq)) {
		return IRQ_NONE;	/* Not our interrupt */
	}
#ifdef ESX
	atomic_inc(&adapter->isr_cnt);
	if (atomic_read(&adapter->tx_timeout)) {
		atomic_dec(&adapter->isr_cnt);
		return IRQ_HANDLED;
	}
#endif

	if (unm_nic_clear_int(adapter, nxhal_sds_ring) == -1) {
#ifdef ESX
		atomic_dec(&adapter->isr_cnt);
#endif
		return IRQ_NONE;
	}

	/* process our status queue */
	if (!netif_running(adapter->netdev) && 
	    !adapter->nx_status_callback_handler_table[NX_NIC_CB_LSA].registered) {
		goto done;
	}

        nx_nic_print7(adapter, "Entered handle ISR\n");
	host_sds_ring->ints++;
	adapter->stats.ints++;

#if !defined(UNM_NIC_NAPI)
	unm_process_rcv_ring(adapter, nxhal_sds_ring, MAX_STATUS_HANDLE);

	if (host_sds_ring->ring_idx == 0) {
		unm_process_cmd_ring((unsigned long)adapter);
	}
#else

	if (host_sds_ring->ring_idx == 0) {
		tx_has_work = unm_nic_tx_has_work(adapter);
	}

	if (unm_nic_rx_has_work(adapter,
				 host_sds_ring->ring_idx) || tx_has_work) {
#ifdef NEW_NAPI         
		ret = netif_rx_schedule_prep(netdev, &host_sds_ring->napi);
#else

		if (netif_running(adapter->netdev)) {
			ret = netif_rx_schedule_prep(netdev);
		} else {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16)
			ret = __netif_rx_schedule_prep(netdev);
#else
			/*
			 * There is no API in these versions of the kernel.
			 */
			ret = !test_and_set_bit(__LINK_STATE_RX_SCHED,
					&netdev->state);
#endif
		}
#endif
		if (ret) {
			/*
			 * Interrupts are already disabled.
			 */
#ifdef NEW_NAPI
			__netif_rx_schedule(netdev, &host_sds_ring->napi);
#else
			__netif_rx_schedule(netdev);
#endif
		} else {
			static unsigned int intcount = 0;
			if ((++intcount & 0xfff) == 0xfff) {
				//printk(KERN_ERR "%s: %s intr %d in poll\n",
				// unm_nic_driver_name, netdev->name, intcount);
			}
		}
		ret = 1;
	}
#endif
	if (ret == 0) {
		/* TODO: enable non-rx type interrupts */
		read_lock(&adapter->adapter_lock);
		unm_nic_enable_int(adapter, nxhal_sds_ring);
		read_unlock(&adapter->adapter_lock);
	}

done:
#ifdef ESX
	atomic_dec(&adapter->isr_cnt);
#endif
	return IRQ_HANDLED;
}

/*
 * Check if there are any command descriptors pending.
 */
static inline int unm_get_pending_cmd_desc_cnt(unm_pending_cmd_descs_t *
					       pending_cmds)
{
	return (pending_cmds->cnt);
}

#ifdef UNM_NIC_NAPI
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,21)
static inline void __netif_rx_complete(struct net_device *dev)
{
	if (!test_bit(__LINK_STATE_RX_SCHED, &dev->state))
		BUG();
	list_del(&dev->poll_list);
	smp_mb__before_clear_bit();
	clear_bit(__LINK_STATE_RX_SCHED, &dev->state);
}
#endif /* LINUX_VERSION_CODE < 2.4.23 */

/*
 * Per status ring processing.
 */
#ifdef NEW_NAPI
static int nx_nic_poll_sts(struct napi_struct *napi, int work_to_do)
{
	sds_host_ring_t *host_sds_ring  = container_of(napi, sds_host_ring_t, napi);
	nx_host_sds_ring_t *nxhal_sds_ring = (nx_host_sds_ring_t *)host_sds_ring->ring;
#else
static int nx_nic_poll_sts(struct net_device *netdev, int *budget)
{
	nx_host_sds_ring_t *nxhal_sds_ring = (nx_host_sds_ring_t *)netdev->priv;
	sds_host_ring_t    *host_sds_ring  =
		(sds_host_ring_t *)nxhal_sds_ring->os_data;
	int work_to_do = min(*budget, netdev->quota);
#endif
	struct unm_adapter_s    *adapter = host_sds_ring->adapter;
	int done = 1;
	int work_done;

	work_done = unm_process_rcv_ring(adapter, nxhal_sds_ring, work_to_do);

#ifndef NEW_NAPI
	netdev->quota -= work_done;
	*budget -= work_done;
#endif

	if (work_done >= work_to_do &&
	    unm_nic_rx_has_work(adapter, host_sds_ring->ring_idx) != 0) {
		done = 0;
	}

#if defined(NEW_NAPI)
	/* 
	 * Only one cpu must be processing Tx command ring.
	 */
	if(spin_trylock(&adapter->tx_cpu_excl)) {

		if (unm_process_cmd_ring((unsigned long)adapter) == 0) {
			done = 0;
		}
		spin_unlock(&adapter->tx_cpu_excl);
	}
#endif


        nx_nic_print7(adapter, "new work_done: %d work_to_do: %d\n",
		      work_done, work_to_do);
	if (done) {
#ifdef NEW_NAPI
		netif_rx_complete(host_sds_ring->netdev, napi);
#else

		netif_rx_complete(netdev);
#endif
		/*unm_nic_hw_write(adapter,(uint64_t)ISR_INT_VECTOR,
		  &reset_val,4); */
		read_lock(&adapter->adapter_lock);
		unm_nic_enable_int(adapter, nxhal_sds_ring);
		read_unlock(&adapter->adapter_lock);
	}
#if defined(NEW_NAPI)
	/*
	 * This comes after enabling the interrupt. The reason is that if done
	 * before the interrupt is turned and the following routine could fill
	 * up the cmd ring and the card could finish the processing and try
	 * interrupting the host even before the interrupt is turned on again.
	 * This could lead to missed interrupt and has an outside chance of
	 * deadlock.
	 */
	if(spin_trylock(&adapter->tx_cpu_excl)) {

		spin_lock(&adapter->tx_lock);
		if (unm_get_pending_cmd_desc_cnt(&adapter->pending_cmds)) {
			unm_proc_pending_cmd_desc(adapter);
		}
		spin_unlock(&adapter->tx_lock);
		spin_unlock(&adapter->tx_cpu_excl);
	}
	return work_done;
#else
	return (done ? 0 : 1);
#endif
}

#ifndef NEW_NAPI
static int unm_nic_poll(struct net_device *netdev, int *budget)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	int work_to_do = min(*budget, netdev->quota);
	int done = 1;
	int work_done = 0;

	adapter->stats.polled++;

	work_done = unm_process_rcv_ring(adapter,
			&adapter->nx_dev->rx_ctxs[0]->sds_rings[0],
			work_to_do);

	netdev->quota -= work_done;
	*budget -= work_done;

	if (work_done >= work_to_do && unm_nic_rx_has_work(adapter, 0)) {
		done = 0;
	}

	if (unm_process_cmd_ring((unsigned long)adapter) == 0) {
		done = 0;
	}

	nx_nic_print7(adapter, "new work_done: %d work_to_do: %d\n",
			work_done, work_to_do);
	if (done) {
		netif_rx_complete(netdev);
		/*unm_nic_hw_write(adapter,(uint64_t)ISR_INT_VECTOR,
		  &reset_val,4); */
		read_lock(&adapter->adapter_lock);
		unm_nic_enable_int(adapter,
				&adapter->nx_dev->rx_ctxs[0]->sds_rings[0]);
		read_unlock(&adapter->adapter_lock);
	}

	/*
	 * This comes after enabling the interrupt. The reason is that if done
	 * before the interrupt is turned and the following routine could fill
	 * up the cmd ring and the card could finish the processing and try
	 * interrupting the host even before the interrupt is turned on again.
	 * This could lead to missed interrupt and has an outside chance of
	 * deadlock.
	 */
	if (unm_get_pending_cmd_desc_cnt(&adapter->pending_cmds)) {
		spin_lock(&adapter->tx_lock);
		unm_proc_pending_cmd_desc(adapter);
		spin_unlock(&adapter->tx_lock);
	}

	return (done ? 0 : 1);
}
#endif
#endif

#ifdef CONFIG_NET_POLL_CONTROLLER
static void unm_nic_poll_controller(struct net_device *netdev)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	disable_irq(netdev->irq);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
	unm_intr(netdev->irq,  &adapter->nx_dev->rx_ctxs[0]->sds_rings[0], NULL);
#else
	unm_intr(netdev->irq, &adapter->nx_dev->rx_ctxs[0]->sds_rings[0]);
#endif
	enable_irq(netdev->irq);
}
#endif

static inline struct sk_buff *process_rxbuffer(struct unm_adapter_s *adapter,
					nx_host_sds_ring_t *nxhal_sds_ring,
					int desc_ctx, int totalLength,
					int csum_status, int index)
{

	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct sk_buff *skb = NULL;
	sds_host_ring_t  *host_sds_ring =
				 (sds_host_ring_t *)nxhal_sds_ring->os_data;
	rds_host_ring_t *host_rds_ring = NULL;
	nx_host_rds_ring_t *nxhal_rds_ring = NULL;
	nx_host_rx_ctx_t *nxhal_rx_ctx = nxhal_sds_ring->parent_ctx;
	struct unm_rx_buffer *buffer = NULL;


	nxhal_rds_ring = &nxhal_rx_ctx->rds_rings[desc_ctx];
	host_rds_ring = (rds_host_ring_t *) nxhal_rds_ring->os_data;
	buffer = &host_rds_ring->rx_buf_arr[index];

	pci_unmap_single(pdev, (buffer)->dma, host_rds_ring->dma_size,
			 PCI_DMA_FROMDEVICE);

	skb = (struct sk_buff *)(buffer)->skb;

	if (unlikely(skb == NULL)) {
		/*
		 * This should not happen and if it does, it is serious,
		 * catch it
		 */

		/* Window = 0 */
		unm_nic_write_w0(adapter, UNM_CRB_PEG_NET_0 + 0x3c, 1);
		unm_nic_write_w0(adapter, UNM_CRB_PEG_NET_1 + 0x3c, 1);
		unm_nic_write_w0(adapter, UNM_CRB_PEG_NET_2 + 0x3c, 1);
		unm_nic_write_w0(adapter, UNM_CRB_PEG_NET_3 + 0x3c, 1);
		unm_nic_write_w0(adapter, UNM_CRB_NIU + 0x70000, 0);

                nx_nic_print2(adapter, "NULL skb for index %d desc_ctx 0x%x "
			      "of %s type\n",
			      index, desc_ctx, RCV_DESC_TYPE_NAME(desc_ctx));
                nx_nic_print2(adapter, "Halted the pegs and stopped the NIU "
			      "consumer\n");

		adapter->stats.rcvdbadskb++;
		return NULL;
	}
#if defined(XGB_DEBUG)
	if (!skb_is_sane(skb)) {
		dump_skb(skb);
                nx_nic_print3(adapter, "index:%d skb(%p) dma(%p) not sane\n",
			      index, skb, buffer->dma);
		/* Window = 0 */
		unm_nic_write_w0(adapter, UNM_CRB_PEG_NET_0 + 0x3c, 1);
		unm_nic_write_w0(adapter, UNM_CRB_PEG_NET_1 + 0x3c, 1);
		unm_nic_write_w0(adapter, UNM_CRB_PEG_NET_2 + 0x3c, 1);
		unm_nic_write_w0(adapter, UNM_CRB_PEG_NET_3 + 0x3c, 1);
		unm_nic_write_w0(adapter, UNM_CRB_NIU + 0x70000, 0);

		return NULL;
	}
#endif

	nx_copy_and_free_vmkbounce_buffer(buffer, host_rds_ring, skb,
					  totalLength);
#if defined(UNM_NIC_HW_CSUM)
	if (likely((adapter->rx_csum) && (csum_status == STATUS_CKSUM_OK))) {
		adapter->stats.csummed++;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else {
		skb->ip_summed = CHECKSUM_NONE;
	}
#endif
	skb->dev = netdev;

	/*
	 * We just consumed one buffer so post a buffer.
	 * Buffers may come out of order in Rcv when LRO is turned ON.
	 */
	buffer->skb = NULL;
	buffer->state = UNM_BUFFER_FREE;
	buffer->lro_current_frags = 0;
	buffer->lro_expected_frags = 0;
	/* "process packet - allocate skb" sequence is having some performance
	 * implications. Until that is sorted out, don't allocate skbs here
	 */
	if (nx_alloc_rx_skb(adapter, nxhal_rds_ring, buffer)) {
		/*
		 * In case of failure buffer->skb is NULL and when trying to
		 * post the receive descriptor to the card that function tries
		 * allocating the buffer again.
		 */
		nx_nic_print6(adapter, "%s: allocation failed\n",
			      __FUNCTION__);
		atomic_inc(&host_rds_ring->alloc_failures);

	}
	host_sds_ring->free_rbufs[desc_ctx].count++;
	TAILQ_INSERT_TAIL(&host_sds_ring->free_rbufs[desc_ctx].head, buffer,
				 link);

	return skb;
}

/*
 * unm_process_rcv() send the received packet to the protocol stack.
 * and if the number of receives exceeds RX_BUFFERS_REFILL, then we
 * invoke the routine to send more rx buffers to the Phantom...
 */
static inline void unm_process_rcv(struct unm_adapter_s *adapter,
				   nx_host_sds_ring_t *nxhal_sds_ring,
				   statusDesc_t *desc,
				   statusDesc_t *frag_desc)
{
	struct net_device *netdev = adapter->netdev;
	int index = desc->referenceHandle;
	struct unm_rx_buffer *buffer;
	struct sk_buff *skb;
	uint32_t length = desc->totalLength;
	uint32_t desc_ctx;
	int ret;
	int i;
	struct sk_buff *head_skb = NULL;
	struct sk_buff *last_skb = NULL;
	int nr_frags = 0;
	sds_host_ring_t *host_sds_ring =
			 (sds_host_ring_t *)nxhal_sds_ring->os_data;
	nx_host_rx_ctx_t *nxhal_rx_ctx = nxhal_sds_ring->parent_ctx;
	nx_host_rds_ring_t *nxhal_host_rds_ring = NULL;
	rds_host_ring_t *host_rds_ring = NULL;
#ifdef UNM_NIC_HW_VLAN
 	struct vlan_group *vlgrp=  NULL;
 	struct vlan_hdr *vhdr = NULL ;
 	unsigned short vid = 0;
 	unsigned short vlan_TCI = 0;
#endif
	unm_msg_t msg;

	desc_ctx = desc->type;
//	nx_nic_print3(adapter, "type == %d\n", desc_ctx);
	if (unlikely(desc_ctx >= NUM_RCV_DESC_RINGS)) {
                nx_nic_print3(adapter, "Bad Rcv descriptor ring\n");
		return;
	}

	nxhal_host_rds_ring = &nxhal_rx_ctx->rds_rings[desc_ctx];
	host_rds_ring = (rds_host_ring_t *) nxhal_host_rds_ring->os_data;
	if (unlikely(index > nxhal_host_rds_ring->ring_size)) {
                nx_nic_print3(adapter, "Got a buffer index:%x for %s desc "
			      "type. Max is %x\n",
			      index, RCV_DESC_TYPE_NAME(desc_ctx),
			      nxhal_host_rds_ring->ring_size);
		return;
	}
	buffer = &host_rds_ring->rx_buf_arr[index];

	/*
	 * Check if the system is running very low on buffers. If it is then
	 * don't process this packet and just repost it to the firmware. This
	 * avoids a condition where the fw has no buffers and does not
	 * interrupt the host because it has no packets to notify.
	 */
	if (rarely(nxhal_host_rds_ring->ring_size -
		   atomic_read(&host_rds_ring->alloc_failures) <
		   NX_NIC_RX_POST_THRES)) {

		buffer = &host_rds_ring->rx_buf_arr[index];
		buffer->state = UNM_BUFFER_BUSY;
		host_sds_ring->free_rbufs[desc_ctx].count++;
		TAILQ_INSERT_TAIL(&host_sds_ring->free_rbufs[desc_ctx].head, buffer,
				  link);
		for (i = 0; frag_desc && i < desc->nr_frags; i++) {
			buffer =
			    &host_rds_ring->rx_buf_arr[frag_desc->
						       frag_handles[i]];
			buffer->state = UNM_BUFFER_BUSY;
			host_sds_ring->free_rbufs[desc_ctx].count++;
			TAILQ_INSERT_TAIL(&host_sds_ring->free_rbufs[desc_ctx].head,
					  buffer, link);
		}
		return;
	}

	skb = process_rxbuffer(adapter, nxhal_sds_ring, desc_ctx,
				 desc->totalLength,
				desc->status, index);
	if (!skb) {
		BUG();
		return;
	}

	if (desc_ctx == RCV_DESC_LRO_CTXID) {
		/* True length was only available on the last pkt */
		skb_put(skb, buffer->lro_length);
	} else {
		skb_put(skb,
			(length <
			 nxhal_host_rds_ring->
			 buff_size) ? length : nxhal_host_rds_ring->buff_size);
		skb_pull(skb, desc->pkt_offset);
	}

#if 1
	skb->protocol = eth_type_trans(skb, netdev);
#else
	/* this is an alternative, but we still need to check eth address */

	if (desc->prot == UNM_PROT_IP) {
		skb->protocol = htons(ETH_P_IP);
		skb->mac.raw = skb->data;
		skb_pull(skb, ETH_HLEN);
		skb->input_dev = netdev;
		eth = eth_hdr(skb);
		if (*eth->h_dest & 1) {
			if (memcmp(eth->h_dest, dev->broadcast, ETH_ALEN) == 0)
				skb->pkt_type = PACKET_BROADCAST;
			else
				skb->pkt_type = PACKET_MULTICAST;
		}

	} else {
		skb->protocol = eth_type_trans(skb, netdev);
	}
#endif
	if (frag_desc) {
		nr_frags = desc->nr_frags;
		head_skb = skb;

		for (i = 0; i < nr_frags; i++) {

			index = frag_desc->frag_handles[i];

			skb = process_rxbuffer(adapter, nxhal_sds_ring, desc_ctx,
					desc->totalLength, desc->status, index);
			if (skb == NULL)
				BUG();

			skb->next = NULL;
			skb->len = nxhal_host_rds_ring->buff_size;

			if (skb_shinfo(head_skb)->frag_list == NULL)
				skb_shinfo(head_skb)->frag_list = skb;

			if (last_skb)
				last_skb->next = skb;
			last_skb = skb;
		}
		last_skb->len =
		    length - (nr_frags * nxhal_host_rds_ring->buff_size);
		head_skb->len = length;
#ifdef EXTRA_L2_HEADER
		head_skb->len -= (desc->pkt_offset + ETH_HLEN);
#endif
		head_skb->data_len =
		    head_skb->len - nxhal_host_rds_ring->buff_size;
#ifdef EXTRA_L2_HEADER
		head_skb->data_len += (desc->pkt_offset + ETH_HLEN);

#endif
#ifndef ESX_3X
		head_skb->truesize = head_skb->len + sizeof(struct sk_buff);
#endif

		skb = head_skb;
	}

	if (adapter->testCtx.capture_input) {
		if (adapter->testCtx.rx_user_packet_data != NULL &&
		    (adapter->testCtx.rx_user_pos + (skb->len)) <=
		    adapter->testCtx.rx_datalen) {
			memcpy(adapter->testCtx.rx_user_packet_data +
			       adapter->testCtx.rx_user_pos, skb->data,
			       skb->len);
			adapter->testCtx.rx_user_pos += (skb->len);
		}
	}

	if (!netif_running(netdev)) {
		kfree_skb(skb);
		goto packet_done_no_stats;
	}

#ifndef ESX
	/* At this point we have got a valid skb.
	 * Check whether this is a SYN that pegnet needs to handle
	 */
	if (desc->opcode == UNM_MSGTYPE_NIC_SYN_OFFLOAD) {
		/* check whether pegnet wants this */
		spin_lock(&adapter->cb_lock);

		if (adapter->nx_status_msg_handler_table[UNM_MSGTYPE_NIC_SYN_OFFLOAD].handler != 
		    nx_status_msg_default_handler) {
			ret = adapter->nx_status_msg_handler_table[UNM_MSGTYPE_NIC_SYN_OFFLOAD].handler(netdev, 
				adapter->nx_status_msg_handler_table[UNM_MSGTYPE_NIC_SYN_OFFLOAD].data, NULL, skb);

			/* ok, it does */
			if (ret == 0) {
				kfree_skb(skb);
				spin_unlock(&adapter->cb_lock);
				goto packet_done;
			}
		}
		spin_unlock(&adapter->cb_lock);
	} else if (desc->opcode == UNM_MSGTYPE_CAPTURE) {
		msg.hdr.word       = desc->body[0];
		msg.body.values[0] = desc->body[0];
		msg.body.values[1] = desc->body[1];

		if (adapter->nx_status_msg_handler_table[UNM_MSGTYPE_CAPTURE].handler(netdev,
			NULL, &msg, skb)) {
		        nx_nic_print5(adapter, "%s: (parse error) dropping "
			              "captured offloaded skb %p\n", __FUNCTION__, skb); 
		        kfree_skb(skb);
		        goto packet_done;
		}	
	}
#endif

	nx_set_skb_queueid(skb, nxhal_rx_ctx);

	/* Check 1 in every NX_LRO_CHECK_INTVL packets for lro worthiness */
	if (rarely(((adapter->stats.uphappy & NX_LRO_CHECK_INTVL) == 0 ||
		    desc->opcode == UNM_MSGTYPE_NIC_SYN_OFFLOAD)  &&
		   adapter->lro.enabled != 0)) {
		nx_try_initiate_lro(adapter, skb, desc->hashValue,
				    (__uint32_t)desc->port);
	}

	/* fallthrough for regular nic processing */
#ifdef UNM_XTRA_DEBUG
        nx_nic_print7(adapter, "reading from %p index %d, %d bytes\n",
		      buffer->dma, index, length);
	for (i = 0; &skb->data[i] < skb->tail; i++) {
		printk("%02x%c", skb->data[i], (i + 1) % 0x10 ? ' ' : '\n');
	}
	printk("\n");
#endif
#if defined(UNM_LOOPBACK)
	if (adapter->testCtx.loopback_start) {
		for (i = 0; i < 6; i++) {
			tmp = skb->data[i];
			skb->data[i] = skb->data[i + 6];
			skb->data[i + 6] = tmp;
		}
		unm_nic_xmit_frame(skb, netdev);
	}
#else
	NX_ADJUST_PKT_LEN(skb->len);
#ifdef UNM_NIC_HW_VLAN
	vlgrp = adapter->vgrp;
	vid = 0;

	vhdr = (struct vlan_hdr *)(skb->data);
	if (vhdr == NULL || vlgrp == NULL ) {
		goto normal_path;
	}
	vlan_TCI = ntohs(vhdr->h_vlan_TCI);
	vid = (vlan_TCI & VLAN_VID_MASK);

	/* TODO: Actually we should use vlan_hw_accel_rx/ receive_skb
	 * helper functions but somehow its not working
	 * following code path hence has to be changed later
	 */

  normal_path:
#ifndef UNM_NIC_NAPI
	ret = netif_rx(skb);
#else
	ret = netif_receive_skb(skb);
#endif
	if (ret < 0 && vid > 0) {
		printk(KERN_ALERT"Error receiving vlan packet\n");
	}
#else
#ifndef UNM_NIC_NAPI
	ret = netif_rx(skb);
#else
	ret = netif_receive_skb(skb);
#endif
#endif
#endif

	/*
	 * RH: Do we need these stats on a regular basis. Can we get it from
	 * Linux stats.
	 */
	switch (ret) {
	case NET_RX_SUCCESS:
		adapter->stats.uphappy++;
		break;

	case NET_RX_CN_LOW:
		adapter->stats.uplcong++;
		break;

	case NET_RX_CN_MOD:
		adapter->stats.upmcong++;
		break;

	case NET_RX_CN_HIGH:
		adapter->stats.uphcong++;
		break;

	case NET_RX_DROP:
		adapter->stats.updropped++;
		break;

	default:
		adapter->stats.updunno++;
		break;
	}

	netdev->last_rx = jiffies;

      packet_done:
	adapter->stats.no_rcv++;
	adapter->stats.rxbytes += length;

      packet_done_no_stats:

	return;
}

static inline void lro2_adjust_skb(struct sk_buff *skb, statusDesc_t *desc)
{
	struct iphdr	*iph;
	struct tcphdr	*th;
	__uint16_t	length;

	iph = (struct iphdr *)skb->data;
	skb_pull(skb, iph->ihl << 2);
	th = (struct tcphdr *)skb->data;
	skb_push(skb, iph->ihl << 2);

	length = (iph->ihl << 2) + (th->doff << 2) + desc->lro2.length;
	iph->tot_len = htons(length);
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
	th->psh = desc->lro2.psh;
	th->seq = htonl(desc->lro2.seq_number);

//	nx_nic_print5(NULL, "Sequence Number [0x%x]\n", desc->lro2.seq_number);
}

static void unm_process_lro_contiguous(struct unm_adapter_s *adapter,
				       nx_host_sds_ring_t *nxhal_sds_ring,
				       statusDesc_t *desc)
{
	struct net_device	*netdev = adapter->netdev;
	struct sk_buff		*skb;
	uint32_t		length;
	uint32_t		data_offset;
	uint32_t		desc_ctx;
	sds_host_ring_t		*host_sds_ring;
	int			ret;
	nx_host_rx_ctx_t	*nxhal_rx_ctx = adapter->nx_dev->rx_ctxs[0];
	nx_host_rds_ring_t	*hal_rds_ring = NULL;
	rds_host_ring_t		*host_rds_ring = NULL;
	uint16_t		ref_handle;
	uint16_t		stats_idx;


	host_sds_ring = (sds_host_ring_t *)nxhal_sds_ring->os_data;

	desc_ctx = desc->lro2.type;
	if (unlikely(desc_ctx != 1)) {
		nx_nic_print3(adapter, "Bad Rcv descriptor ring\n");
		return;
	}

	hal_rds_ring = &nxhal_rx_ctx->rds_rings[desc_ctx];
	host_rds_ring = (rds_host_ring_t *)hal_rds_ring->os_data;

	adapter->lro.stats.contiguous_pkts++;
	stats_idx = desc->lro2.length >> 10;
	if (stats_idx >= NX_1K_PER_LRO) {
		stats_idx = (NX_1K_PER_LRO - 1);
	}
	adapter->lro.stats.bufsize[stats_idx]++;
	/*
	 * Check if the system is running very low on buffers. If it is then
	 * don't process this packet and just repost it to the firmware. This
	 * avoids a condition where the fw has no buffers and does not
	 * interrupt the host because it has no packets to notify.
	 */
	if (rarely(hal_rds_ring->ring_size -
		   atomic_read(&host_rds_ring->alloc_failures) <
						NX_NIC_RX_POST_THRES)) {
		struct unm_rx_buffer	*buffer;

		nx_nic_print3(adapter, "In rcv buffer crunch !!\n");

		ref_handle = desc->lro2.ref_handle;
		if (rarely(ref_handle >= hal_rds_ring->ring_size)) {
			nx_nic_print3(adapter, "Got a bad ref_handle[%u] for "
				      "%s desc type. Max[%u]\n",
				      ref_handle, RCV_DESC_TYPE_NAME(desc_ctx),
				      hal_rds_ring->ring_size);
			BUG();
			return;
		}
		buffer = &host_rds_ring->rx_buf_arr[ref_handle];
		buffer->state = UNM_BUFFER_BUSY;
		host_sds_ring->free_rbufs[desc_ctx].count++;
		TAILQ_INSERT_TAIL(&host_sds_ring->free_rbufs[desc_ctx].head,
				  buffer, link);
		return;
	}

	ref_handle = desc->lro2.ref_handle;
	length = desc->lro2.length;

	skb = process_rxbuffer(adapter, nxhal_sds_ring, desc_ctx,
			       hal_rds_ring->buff_size, STATUS_CKSUM_OK,
			       ref_handle);

	if (!skb) {
		BUG();
		return;
	}

	data_offset = (desc->lro2.l4_hdr_offset +
		       (desc->lro2.timestamp ?
			TCP_TS_HDR_SIZE : TCP_HDR_SIZE));

	skb_put(skb, data_offset + length);
	skb_pull(skb, desc->lro2.l2_hdr_offset);
	skb->protocol = eth_type_trans(skb, netdev);
#ifndef ESX_3X
	skb->truesize = skb->len + sizeof (struct sk_buff);
#endif
	lro2_adjust_skb(skb, desc);

	length = skb->len;

	if (!netif_running(netdev)) {
		kfree_skb(skb);
		return;
	}

	nx_set_skb_queueid(skb, nxhal_rx_ctx);

#ifndef UNM_NIC_NAPI
	ret = netif_rx(skb);
#else
	ret = netif_receive_skb(skb);
#endif

	netdev->last_rx = jiffies;

	adapter->stats.no_rcv++;
	adapter->stats.rxbytes += length;

	return;
}

static inline void lro_adjust_head_skb(struct sk_buff *head_skb,
				       struct sk_buff *tail_skb,
				       uint16_t incr_len,
				       nx_lro_hdr_desc_t *lro_hdr)
{
	struct iphdr	*iph;
	struct tcphdr	*head_th;
	struct tcphdr	*tail_th;
	__uint16_t	length;

	iph = (struct iphdr *)head_skb->data;
	length = ntohs(iph->tot_len) + incr_len;
	iph->tot_len = htons(length);
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);

	skb_pull(head_skb, iph->ihl << 2);
	head_th = (struct tcphdr *)head_skb->data;
	head_th->psh = lro_hdr->psh;
	/* tail skb data is pointing to payload */
	skb_push(tail_skb, head_th->doff << 2);
	tail_th = (struct tcphdr *)tail_skb->data;
	head_th->ack_seq = tail_th->ack_seq;
	if (head_th->doff > 5) {
		memcpy((head_skb->data + TCP_HDR_SIZE),
		       (tail_skb->data + TCP_HDR_SIZE),
		       TCP_TS_OPTION_SIZE);
	}
	skb_pull(tail_skb, head_th->doff << 2);
	skb_push(head_skb, iph->ihl << 2);
}

/*
 * unm_process_lro() send the lro packet to the protocol stack.
 * and if the number of receives exceeds RX_BUFFERS_REFILL, then we
 * invoke the routine to send more rx buffers to the Phantom...
 */
/* TODO: Large part of this function is common with unm_process_rcv()
 *  and should go to common fn */
static inline void unm_process_lro(struct unm_adapter_s *adapter,
				   nx_host_sds_ring_t *nxhal_sds_ring,
				   statusDesc_t *desc, statusDesc_t *desc_list,
				   int num_desc)
{
	struct net_device	*netdev = adapter->netdev;
	struct sk_buff		*skb;
	uint32_t		length;
	uint32_t		data_length;
	uint32_t		desc_ctx;
	sds_host_ring_t		*host_sds_ring;
	int			ret;
	int			ii;
	int			jj;
	struct sk_buff		*head_skb;
	struct sk_buff		*last_skb;
	int			nr_skbs;
	nx_lro_frags_desc_t	*lro_frags;
	nx_host_rx_ctx_t	*nxhal_rx_ctx = adapter->nx_dev->rx_ctxs[0];
	nx_host_rds_ring_t	*hal_rds_ring = NULL;
	rds_host_ring_t		*host_rds_ring = NULL;
	uint16_t		ref_handle;


	host_sds_ring = (sds_host_ring_t *)nxhal_sds_ring->os_data;

	nr_skbs = desc->lro_hdr.count;
	desc_ctx = desc->lro_hdr.type;
	if (unlikely(desc_ctx != 0)) {
		nx_nic_print3(adapter, "Bad Rcv descriptor ring\n");
		return;
	}
	hal_rds_ring = &nxhal_rx_ctx->rds_rings[desc_ctx];
	host_rds_ring = (rds_host_ring_t *)hal_rds_ring->os_data;

	adapter->lro.stats.chained_pkts++;
	if (nr_skbs <= NX_MAX_PKTS_PER_LRO) {
		adapter->lro.stats.accumulation[nr_skbs - 1]++;
	}
#if 0
	struct unm_rx_buffer	*buffer;

	/*
	 * Check if the system is running very low on buffers. If it is then
	 * don't process this packet and just repost it to the firmware. This
	 * avoids a condition where the fw has no buffers and does not
	 * interrupt the host because it has no packets to notify.
	 */
	if (rarely(hal_rds_ring->ring_size -
		   atomic_read(&host_rds_ring->alloc_failures) <
						NX_NIC_RX_POST_THRES)) {

		nx_nic_print3(adapter, "In rcv buffer crunch !!\n");

		ref_handle = desc->lro_hdr.ref_handle;
		if (rarely(ref_handle >= hal_rds_ring->ring_size)) {
			nx_nic_print3(adapter, "Got a bad ref_handle[%u] for "
				      "%s desc type. Max[%u]\n",
				      ref_handle, RCV_DESC_TYPE_NAME(desc_ctx),
				      hal_rds_ring->ring_size);
			BUG();
			return;
		}
		buffer = &host_rds_ring->rx_buf_arr[ref_handle];
		buffer->state = UNM_BUFFER_BUSY;
		host_sds_ring->free_rbufs[desc_ctx].count++;
		TAILQ_INSERT_TAIL(&host_sds_ring->free_rbufs[desc_ctx].head,
				  buffer, link);
		nr_skbs--;

		for (ii = 0; ii < num_desc; ii++) {
			lro_frags = &desc_list[ii].lro_frags;

			for (jj = 0;
			     jj < NX_LRO_PKTS_PER_STATUS_DESC && nr_skbs;
			     jj++, nr_skbs--) {

				ref_handle = lro_frags->pkts[jj].s.ref_handle;

				if (rarely(ref_handle >= hal_rds_ring->ring_size)) {
					nx_nic_print3(adapter, "Got a bad "
						      "ref_handle[%u] for %s "
						      "desc type. Max[%u]\n",
						      ref_handle,
						      RCV_DESC_TYPE_NAME(desc_ctx),
						      hal_rds_ring->ring_size);
					BUG();
					return;
				}
				buffer = &host_rds_ring->rx_buf_arr[ref_handle];
				buffer->state = UNM_BUFFER_BUSY;
				host_sds_ring->free_rbufs[desc_ctx].count++;
				TAILQ_INSERT_TAIL(&host_sds_ring->free_rbufs[desc_ctx].head,
						  buffer, link);
			}

		}
		return;
	}
#endif

	ref_handle = desc->lro_hdr.ref_handle;
	length = desc->lro_hdr.length;
	head_skb = process_rxbuffer(adapter, nxhal_sds_ring, desc_ctx,
				    hal_rds_ring->buff_size, STATUS_CKSUM_OK,
				    ref_handle);
	if (!head_skb) {
		BUG();
		return;
	}

	NX_ADJUST_PKT_LEN(length);

	skb_put(head_skb, desc->lro_hdr.data_offset + length);
	skb_pull(head_skb, desc->lro_hdr.l2_hdr_offset);
	head_skb->protocol = eth_type_trans(head_skb, netdev);
	nr_skbs--;
//	netif_receive_skb(head_skb);

	last_skb = NULL;
	data_length = 0;
	for (ii = 0; ii < num_desc; ii++) {
		lro_frags = &desc_list[ii].lro_frags;

		for (jj = 0; jj < NX_LRO_PKTS_PER_STATUS_DESC && nr_skbs;
		     jj++, nr_skbs--) {

			ref_handle = lro_frags->pkts[jj].s.ref_handle;
			length = lro_frags->pkts[jj].s.length;

			if (unlikely(ref_handle >= hal_rds_ring->ring_size)) {
				nx_print(KERN_ERR, "Got a bad ref_handle[%u] for %s "
					 "desc type. Max[%u]\n",
					 ref_handle,
					 RCV_DESC_TYPE_NAME(desc_ctx),
					 hal_rds_ring->ring_size);
				nx_print(KERN_ERR, "ii[%u], jj[%u], nr_skbs[%u/%u], num_desc[%u]\n", ii, jj, nr_skbs, desc->lro_hdr.count, num_desc);
				printk("Head: %016llx %016llx\n", desc->body[0], desc->body[1]);
				for (ii = 0; ii < num_desc; ii++) {
					printk("%016llx %016llx\n", desc_list[ii].body[0], desc_list[ii].body[1]);
				}
				BUG();
				return;
			}

			skb = process_rxbuffer(adapter, nxhal_sds_ring,
					       desc_ctx,
					       hal_rds_ring->buff_size,
					       STATUS_CKSUM_OK, ref_handle);
			if (!skb) {
				BUG();
				return;
			}

			data_length += length;
			skb_put(skb, desc->lro_hdr.data_offset + length);
//	skb_pull(skb, desc->lro_hdr.hdr_offset);
//	skb->protocol = eth_type_trans(skb, netdev);
//	netif_receive_skb(skb);
//	continue;
			skb_pull(skb, desc->lro_hdr.data_offset);

			skb->next = NULL;
			if (skb_shinfo(head_skb)->frag_list == NULL) {
				skb_shinfo(head_skb)->frag_list = skb;
			}

			/* Point to payload */
			if (last_skb) {
				last_skb->next = skb;
			}
			last_skb = skb;
		}
	}

//	printk("Length = %u\n", data_length);
//	return;
	head_skb->data_len = data_length;
	head_skb->len += head_skb->data_len;
#ifndef ESX_3X
	head_skb->truesize += data_length;
#endif
	if (last_skb) {
		lro_adjust_head_skb(head_skb, last_skb, data_length,
				    &desc->lro_hdr);
	}

	length = head_skb->len;

	if (!netif_running(netdev)) {
		kfree_skb(head_skb);
		return;
	}

	nx_set_skb_queueid(head_skb, nxhal_rx_ctx);

#ifndef UNM_NIC_NAPI
	ret = netif_rx(head_skb);
#else
	ret = netif_receive_skb(head_skb);
#endif

	netdev->last_rx = jiffies;

	adapter->stats.no_rcv++;
	adapter->stats.rxbytes += length;

	return;
}

/*
 * nx_post_rx_descriptors puts buffer in the Phantom memory
 */
static int nx_post_rx_descriptors(struct unm_adapter_s *adapter,
				  nx_host_rds_ring_t * nxhal_rds_ring,
				  uint32_t ringid, nx_free_rbufs_t * free_list)
{
	uint producer;
	rcvDesc_t *pdesc;
	struct unm_rx_buffer *buffer;
	int count = 0;
	int rv;
	nx_free_rbufs_t failure_list;
	rds_host_ring_t *host_rds_ring = NULL;
	u32 	data;

	TAILQ_INIT(&failure_list.head);
	failure_list.count = 0;

	host_rds_ring = (rds_host_ring_t *) nxhal_rds_ring->os_data;
	producer = host_rds_ring->producer;
	/* We can start writing rx descriptors into the phantom memory. */
	while (!TAILQ_EMPTY(&free_list->head)) {
		buffer = TAILQ_FIRST(&free_list->head);
		TAILQ_REMOVE(&free_list->head, buffer, link);

		if (buffer->skb == NULL) {
			rv = nx_alloc_rx_skb(adapter, nxhal_rds_ring, buffer);
			if (rv) {
				TAILQ_INSERT_TAIL(&failure_list.head,
						  buffer, link);
				failure_list.count++;
				/*
				 * TODO: We need to schedule the posting of
				 * buffers to the pegs.
				 */
				nx_nic_print7(adapter, "%s: allocated only %d "
					      "buffers\n",
					      __FUNCTION__, count);
				continue;
			}
			if (atomic_read(&host_rds_ring->alloc_failures) > 0) {
				atomic_dec(&host_rds_ring->alloc_failures);
			}
		}

		/* make a rcv descriptor  */
		pdesc = ((rcvDesc_t *) (nxhal_rds_ring->host_addr)) + producer;
		pdesc->AddrBuffer = buffer->dma;
		pdesc->referenceHandle = buffer->refHandle;
		pdesc->bufferLength = host_rds_ring->dma_size;

                nx_nic_print7(adapter, "done writing descripter\n");
		producer = get_next_index(producer, nxhal_rds_ring->ring_size);

		count++;	/* now there should be no failure */
	}

	if (failure_list.count) {
		spin_lock_bh(&adapter->buf_post_lock);
		TAILQ_MERGE(&host_rds_ring->free_rxbufs.head,
			    &failure_list.head, link);
		host_rds_ring->free_rxbufs.count += failure_list.count;
		spin_unlock_bh(&adapter->buf_post_lock);
		SET_POST_FAILED(adapter, ringid);
	} else {
		RESET_POST_FAILED(adapter, ringid);
	}

	host_rds_ring->producer = producer;

	/* if we did allocate buffers, then write the count to Phantom */
	if (count) {
		//adapter->stats.lastposted = count;
		//adapter->stats.posted    += count;

		/* Window = 1 */
		read_lock(&adapter->adapter_lock);
		data = (producer - 1)& (nxhal_rds_ring->ring_size-1);
		adapter->unm_nic_hw_write_wx(adapter, nxhal_rds_ring->host_rx_producer, &data, 4);
		read_unlock(&adapter->adapter_lock);
	}

	host_rds_ring->posting = 0;
	return (count);
}

/*
 *
 */
static void nx_post_freed_rxbufs(struct unm_adapter_s *adapter,
				 nx_host_sds_ring_t *nxhal_sds_ring)
{
	int ring;
	//unm_rcv_desc_ctx_t    *rcv_desc;
	nx_free_rbufs_t *free_rbufs;
	nx_free_rbufs_t free_list;
	sds_host_ring_t         *host_sds_ring  = (sds_host_ring_t *)nxhal_sds_ring->os_data;
	rds_host_ring_t *host_rds_ring = NULL;
	nx_host_rds_ring_t *nxhal_rds_ring = NULL;
	nx_host_rx_ctx_t *nxhal_rx_ctx = nxhal_sds_ring->parent_ctx;

	for (ring = 0; ring < NUM_RCV_DESC_RINGS; ring++) {
		nxhal_rds_ring = &nxhal_rx_ctx->rds_rings[ring];
		host_rds_ring = (rds_host_ring_t *) nxhal_rds_ring->os_data;
		free_rbufs = &host_sds_ring->free_rbufs[ring];

		if (!host_rds_ring->free_rxbufs.count && !free_rbufs->count) {
			continue;
		}

		spin_lock_bh(&adapter->buf_post_lock);
		if (!TAILQ_EMPTY(&free_rbufs->head)) {
			TAILQ_MERGE(&host_rds_ring->free_rxbufs.head,
				    &free_rbufs->head, link);
			TAILQ_INIT(&free_rbufs->head);

			host_rds_ring->free_rxbufs.count += free_rbufs->count;
			free_rbufs->count = 0;
		}
		if (!host_rds_ring->posting && host_rds_ring->free_rxbufs.count) {

			TAILQ_COPY(&host_rds_ring->free_rxbufs.head,
				   &free_list.head, link);
			TAILQ_INIT(&host_rds_ring->free_rxbufs.head);
			free_list.count = host_rds_ring->free_rxbufs.count;
			host_rds_ring->free_rxbufs.count = 0;

			host_rds_ring->posting = 1;
			spin_unlock_bh(&adapter->buf_post_lock);

			nx_post_rx_descriptors(adapter, nxhal_rds_ring, ring,
					       &free_list);
		} else {
			spin_unlock_bh(&adapter->buf_post_lock);
		}
	}
}

static int
nx_status_msg_nic_response_handler(struct net_device *netdev, void *data, unm_msg_t *msg, struct sk_buff *skb)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;

	nx_os_handle_nic_response((nx_dev_handle_t)adapter, (nic_response_t *) &msg->body);

	return 0;
}

static int
nx_status_msg_default_handler(struct net_device *netdev, void *data, unm_msg_t *msg, struct sk_buff *skb)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;

	if (msg != NULL) {
		nx_nic_print3(adapter, "%s: Error: No status handler for this msg = 0x%x\n", __FUNCTION__, msg->hdr.type);
	} else {
		nx_nic_print3(adapter, "%s: Error: No status handler for this NULL msg\n", __FUNCTION__);
	}

	return 0;
}

/* Process Receive status ring */
static uint32_t
unm_process_rcv_ring(struct unm_adapter_s *adapter,
		     nx_host_sds_ring_t *nxhal_sds_ring, int max)
{
	statusDesc_t    *descHead       = (statusDesc_t *)nxhal_sds_ring->host_addr;
	sds_host_ring_t *host_sds_ring  = (sds_host_ring_t *)nxhal_sds_ring->os_data;
	statusDesc_t    *desc           = NULL;			/* used to read status desc here */
	uint32_t        consumer        = host_sds_ring->consumer;
	int             count           = 0;
	uint32_t        tmp_consumer    = 0;
	statusDesc_t    *last_desc      = NULL;
	u32 temp;

	host_sds_ring->polled++;

        nx_nic_print7(adapter, "processing receive\n");
	/*
	 * we assume in this case that there is only one port and that is
	 * port #1...changes need to be done in firmware to indicate port
	 * number as part of the descriptor. This way we will be able to get
	 * the netdev which is associated with that device.
	 */
	while (count < max) {
		desc = &descHead[consumer];
#if defined(DEBUG)
		if (desc->owner != STATUS_OWNER_HOST &&
		    desc->owner != STATUS_OWNER_PHANTOM) {
                    nx_nic_print7(adapter, "desc(%p)owner is %x consumer:%d\n",
				  desc, desc->owner, consumer);
			break;
		}
#endif
		if (!(desc->owner & STATUS_OWNER_HOST)) {
                    nx_nic_print7(adapter, "desc %p ownedby %s\n", desc,
				  STATUS_OWNER_NAME(desc));
			//printk("desc %p ownedby %s\n", desc,
			//       STATUS_OWNER_NAME(desc));
			break;
		}

		if (desc->opcode == UNM_MSGTYPE_NIC_RXPKT_DESC ||
#if 0
		    desc->opcode == UNM_MSGTYPE_CAPTURE ||
#endif
		    desc->opcode == UNM_MSGTYPE_NIC_SYN_OFFLOAD) {

			last_desc = NULL;

			if (desc->nr_frags) {
				/* jumbo packets */
				tmp_consumer = ((consumer + 1) &
 						(adapter->MaxRxDescCount - 1));
				last_desc = &descHead[tmp_consumer];

				if (!(last_desc->owner & STATUS_OWNER_HOST)) {
					/*
					 * The whole queue message is not ready
					 * yet so break out of here.
					 */
					break;
				}
                                last_desc->owner = STATUS_OWNER_PHANTOM;
                                consumer = tmp_consumer;
			}
			unm_process_rcv(adapter, nxhal_sds_ring, desc,
					last_desc);

		} else if (desc->opcode == UNM_MSGTYPE_NIC_LRO_CONTIGUOUS) {
			unm_process_lro_contiguous(adapter, nxhal_sds_ring,
						   desc);
		} else if (desc->opcode == UNM_MSGTYPE_NIC_LRO_CHAINED) {

			int		desc_cnt, i;
			statusDesc_t	desc_list[5];

			desc_cnt = desc->descCnt - 1; /* First descriptor
							 already read so
							 decrement 1 */

			/* LRO packets */
			tmp_consumer = ((consumer + desc_cnt) &
					(adapter->MaxRxDescCount - 1));
			last_desc = &descHead[tmp_consumer];

			if (!(last_desc->owner & STATUS_OWNER_HOST)) {
				/*
				 * The whole queue message is not ready
				 * yet so break out of here.
				 */
				count = count ? 1 : 0;
				break;
			}
			desc->owner = STATUS_OWNER_PHANTOM;

			desc_cnt--;	/* Last desc is not very useful */
			for (i = 0; i < desc_cnt; i++) {
				consumer = ((consumer + 1) &
					    (adapter->MaxRxDescCount - 1));
				last_desc = &descHead[consumer];
				desc_list[i].body[0] = last_desc->body[0];
				desc_list[i].body[1] = last_desc->body[1];
				last_desc->owner = STATUS_OWNER_PHANTOM;
			}

			unm_process_lro(adapter, nxhal_sds_ring, desc,
					desc_list, desc_cnt);

			/*
			 * Place it at the last descriptor.
			 */
			consumer = ((consumer + 1) &
				    (adapter->MaxRxDescCount - 1));
			desc = &descHead[consumer];
		} else {
			unm_msg_t msg;
			int       cnt 	= desc->descCnt;
			int       index = 1;
			int       i     = 0;

			//cnt = desc->descCnt;

			/*
			 * Check if it is the extended queue message. If it is
			 * then read the rest of the descriptors also.
			 */
			if (cnt > 1) {

				*(uint64_t *) (&msg.hdr) = desc->body[0];
				msg.body.values[0] = desc->body[1];

				/*
				 *
				 */
				tmp_consumer = ((consumer + 4) &
						(adapter->MaxRxDescCount - 1));
				last_desc = &descHead[tmp_consumer];
				if (!(last_desc->owner & STATUS_OWNER_HOST)) {
					/*
					 * The whole queue message is not ready
					 * yet so break out of here.
					 */
					break;
				}
				desc->owner = STATUS_OWNER_PHANTOM;

				for (i = 0; i < (cnt - 2); i++) {
					consumer = ((consumer + 1) & 
				    		(adapter->MaxRxDescCount - 1));
					desc = &descHead[consumer];
					msg.body.values[index++] = desc->body[0];
					msg.body.values[index++] = desc->body[1];
					desc->owner = STATUS_OWNER_PHANTOM;
				}

				consumer = ((consumer + 1) &
					    (adapter->MaxRxDescCount - 1));
				desc = &descHead[consumer];
			} else {
				/*
				 * These messages expect the type field in the
				 * queue message header to be set and nothing
				 * else in the header to be set. So we copy
				 * the desc->body[0] into the header.
				 * desc->body[0] has the opcode which is same
				 * as the queue message type.
				 */
				msg.hdr.word = desc->body[0];
				msg.body.values[0] = desc->body[0];
				msg.body.values[1] = desc->body[1];
			}

			/* Call back msg handler */
			spin_lock(&adapter->cb_lock);

			adapter->nx_status_msg_handler_table[msg.hdr.type].handler(adapter->netdev, 
					adapter->nx_status_msg_handler_table[msg.hdr.type].data, &msg, NULL);

			spin_unlock(&adapter->cb_lock);

		}
		desc->owner = STATUS_OWNER_PHANTOM;
		consumer = (consumer + 1) & (adapter->MaxRxDescCount - 1);
		count++;
	}

	nx_post_freed_rxbufs(adapter, nxhal_sds_ring);

	/* update the consumer index in phantom */
	if (count) {
		host_sds_ring->consumer = consumer;

		/* Window = 1 */
		read_lock(&adapter->adapter_lock);
		temp = consumer;
		adapter->unm_nic_hw_write_wx(adapter,
					 nxhal_sds_ring->host_sds_consumer,
					 &temp, 4);
		read_unlock(&adapter->adapter_lock);
	}

	return (count);
}


/*
 * Initialize the allocated resources in pending_cmd_descriptors.
 */
static void unm_init_pending_cmd_desc(unm_pending_cmd_descs_t * pending_cmds)
{
	INIT_LIST_HEAD(&pending_cmds->free_list);
	INIT_LIST_HEAD(&pending_cmds->cmd_list);
	pending_cmds->curr_block = 0;
	pending_cmds->cnt = 0;
}

/*
 * Free up the allocated resources in pending_cmd_descriptors.
 */
static void unm_destroy_pending_cmd_desc(unm_pending_cmd_descs_t * pending_cmds)
{
	unm_pending_cmd_desc_block_t *block;

	if (pending_cmds->curr_block) {
		kfree(pending_cmds->curr_block);
	}

	while (!list_empty(&pending_cmds->free_list)) {

		block = list_entry(pending_cmds->free_list.next,
				   unm_pending_cmd_desc_block_t, link);
		list_del(&block->link);
		kfree(block);
	}

	while (!list_empty(&pending_cmds->cmd_list)) {

		block = list_entry(pending_cmds->cmd_list.next,
				   unm_pending_cmd_desc_block_t, link);
		list_del(&block->link);
		kfree(block);
	}
}

/*
 *
 */
static unm_pending_cmd_desc_block_t
    *unm_dequeue_pending_cmd_desc(unm_pending_cmd_descs_t * pending_cmds,
				  uint32_t length)
{
	unm_pending_cmd_desc_block_t *block;

	if (!list_empty(&pending_cmds->cmd_list)) {
		if (length < MAX_PENDING_DESC_BLOCK_SIZE) {
			return (NULL);
		}
		block = list_entry(pending_cmds->cmd_list.next,
				   unm_pending_cmd_desc_block_t, link);
		list_del(&block->link);
	} else {
		block = pending_cmds->curr_block;
		if (block == NULL || length < block->cnt) {
			return (NULL);
		}
		pending_cmds->curr_block = NULL;
	}

	pending_cmds->cnt -= block->cnt;

	return (block);
}

/*
 *
 */
static inline unm_pending_cmd_desc_block_t *nx_alloc_pend_cmd_desc_block(void)
{
	return (kmalloc(sizeof(unm_pending_cmd_desc_block_t),
			in_atomic()? GFP_ATOMIC : GFP_KERNEL));
}

/*
 *
 */
static void unm_free_pend_cmd_desc_block(unm_pending_cmd_descs_t * pending_cmds,
					 unm_pending_cmd_desc_block_t * block)
{
/*         printk("%s: Inside\n", __FUNCTION__); */

	block->cnt = 0;
	list_add_tail(&block->link, &pending_cmds->free_list);
}

/*
 * This is to process the pending q msges for chimney.
 */
static void unm_proc_pending_cmd_desc(unm_adapter * adapter)
{
	uint32_t producer;
	uint32_t consumer;
	struct unm_cmd_buffer *pbuf;
	uint32_t length = 0;
	unm_pending_cmd_desc_block_t *block;
	int i;

	producer = adapter->cmdProducer;
	consumer = adapter->lastCmdConsumer;
	if (producer == consumer) {
		length = adapter->MaxTxDescCount - 1;
	} else if (producer > consumer) {
		length = adapter->MaxTxDescCount - producer + consumer - 1;
	} else {
		/* consumer > Producer */
		length = consumer - producer - 1;
	}

	while (length) {
		block = unm_dequeue_pending_cmd_desc(&adapter->pending_cmds,
						     length);
		if (block == NULL) {
			break;
		}
		length -= block->cnt;

		for (i = 0; i < block->cnt; i++) {
			pbuf = &adapter->cmd_buf_arr[producer];
			pbuf->mss = 0;
			pbuf->totalLength = 0;
			pbuf->skb = NULL;
			pbuf->cmd = PEGNET_REQUEST;
			pbuf->fragCount = 0;
			pbuf->port = 0;

			adapter->ahw.cmdDescHead[producer] = block->cmd[i];

			producer = get_next_index(producer,
						  adapter->MaxTxDescCount);
		}
		unm_free_pend_cmd_desc_block(&adapter->pending_cmds, block);
	}

	if (adapter->cmdProducer == producer) {
		return;
	}
	adapter->cmdProducer = producer;


	adapter->ahw.cmdProducer = adapter->cmdProducer;

	/* write producer index to start the xmit */

	read_lock(&adapter->adapter_lock);
	unm_nic_update_cmd_producer(adapter, adapter->cmdProducer);
	read_unlock(&adapter->adapter_lock);
}

/*
 * This is invoked when we are out of descriptor ring space for
 * pegnet_cmd_desc. It queues them up and is to be processed when the
 * descriptor space is freed.
 */
static void nx_queue_pend_cmd_desc(unm_pending_cmd_descs_t * pending_cmds,
				   cmdDescType0_t * cmd_desc, uint32_t length)
{
	unm_pending_cmd_desc_block_t *block;

	if (pending_cmds->curr_block) {
		block = pending_cmds->curr_block;

	} else if (!list_empty(&pending_cmds->free_list)) {

		block = list_entry(pending_cmds->free_list.next,
				   unm_pending_cmd_desc_block_t, link);
		list_del(&block->link);
		block->cnt = 0;
	} else {
		nx_nic_print2(NULL, "%s: *BUG** This should never happen\n",
			      __FUNCTION__);
		return;
	}

	memcpy(&block->cmd[block->cnt], cmd_desc, length);
	block->cnt++;

	if (block->cnt == MAX_PENDING_DESC_BLOCK_SIZE) {
		/*
		 * The array is full, put it in the tail of the cmd_list
		 */
		list_add_tail(&block->link, &pending_cmds->cmd_list);
		block = NULL;
	}

	pending_cmds->cnt++;
	pending_cmds->curr_block = block;
}

/*
 *
 */
static int nx_make_available_pend_cmd_desc(unm_pending_cmd_descs_t * pend,
					   int cnt)
{
	unm_pending_cmd_desc_block_t *block;

	block = pend->curr_block;
	if (block && block->cnt <= (MAX_PENDING_DESC_BLOCK_SIZE - cnt)) {
		return (0);
	}

	if (!list_empty(&pend->free_list)) {
		return (0);
	}

	block = nx_alloc_pend_cmd_desc_block();
	if (block != NULL) {
		/*
		 * Puts it into the free list.
		 */
		unm_free_pend_cmd_desc_block(pend, block);
		return (0);
	}

	return (-ENOMEM);
}

/*
 *
 */
static int nx_queue_pend_cmd_desc_list(unm_adapter * adapter,
				       cmdDescType0_t * cmd_desc_arr,
				       int no_of_desc)
{
	int i;
	int rv;

	rv = nx_make_available_pend_cmd_desc(&adapter->pending_cmds,
					     no_of_desc);
	if (rv) {
		return (rv);
	}

	i = 0;
	do {
		nx_queue_pend_cmd_desc(&adapter->pending_cmds,
				       &cmd_desc_arr[i],
				       sizeof(cmdDescType0_t));
		i++;
	} while (i != no_of_desc);

	return (0);
}

/*
 * Compare adapter ids for two NetXen devices.
 */
int nx_nic_cmp_adapter_id(struct net_device *dev1, struct net_device *dev2)
{
	unm_adapter *adapter1 = (unm_adapter *) netdev_priv(dev1);
	unm_adapter *adapter2 = (unm_adapter *) netdev_priv(dev2);
	return memcmp(adapter1->id, adapter2->id, sizeof(adapter1->id));
}

/*
 * Checks if the specified device is a NetXen device.
 */
int nx_nic_is_netxen_device(struct net_device *netdev)
{
	return (netdev->do_ioctl == unm_nic_ioctl);
}

/*
 * Return the registered TNIC adapter structure.
 *
 * Returns:
 *	The Tnic adapter if already registered else returns NULL.
 */
nx_tnic_adapter_t *nx_nic_get_lsa_adapter(struct net_device *netdev)
{
	struct unm_adapter_s *adapter = (unm_adapter *) netdev_priv(netdev);

	if (adapter) {
		return (adapter->nx_status_callback_handler_table[NX_NIC_CB_LSA].data);
	}

	return (NULL);
}

/*
 * Register msg handler with Nic driver.
 *
 * Return:
 *	0	- If registered successfully.
 *	1	- If already registered.
 *	-1	- If port or adapter is not set.
 */
int nx_nic_rx_register_msg_handler(struct net_device *netdev, uint8_t msgtype, void *data,
				   int (*nx_msg_handler) (struct net_device *netdev, void *data,
						  	  unm_msg_t *msg, struct sk_buff *skb))
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;
	int rv = -1;

	if (adapter) {
		spin_lock_bh(&adapter->cb_lock);

		rv = 1;
		if (!adapter->nx_status_msg_handler_table[msgtype].registered) {
			adapter->nx_status_msg_handler_table[msgtype].msg_type          = msgtype;
			adapter->nx_status_msg_handler_table[msgtype].data              = data; 
			adapter->nx_status_msg_handler_table[msgtype].handler           = nx_msg_handler; 
			adapter->nx_status_msg_handler_table[msgtype].registered        = 1; 
			rv = 0;
		}
		spin_unlock_bh(&adapter->cb_lock);
	}

	return (rv);
}

/*
 * Unregisters a known msg handler
 */
void nx_nic_rx_unregister_msg_handler(struct net_device *netdev, uint8_t msgtype)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;

	spin_lock_bh(&adapter->cb_lock);

	if (adapter->nx_status_msg_handler_table[msgtype].registered) {
		adapter->nx_status_msg_handler_table[msgtype].msg_type	= msgtype;
		adapter->nx_status_msg_handler_table[msgtype].data 	= NULL; 
		adapter->nx_status_msg_handler_table[msgtype].handler 	= nx_status_msg_default_handler; 
		adapter->nx_status_msg_handler_table[msgtype].registered	= 0; 
	}

	spin_unlock_bh(&adapter->cb_lock);
}

/*
 * Register call back handlers (except msg handler) with Nic driver.
 *
 * Return:
 *	0	- If registered successfully.
 *	1	- If already registered.
 *	-1	- If port or adapter is not set.
 */
int nx_nic_rx_register_callback_handler(struct net_device *netdev, uint8_t interface_type,
					void *data)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;
	int rv = -1;

	if (adapter) {
		spin_lock_bh(&adapter->nx_status_callback_handler_table[interface_type].lock);
		rv = 1;
		if (!adapter->nx_status_callback_handler_table[interface_type].registered) {
			adapter->nx_status_callback_handler_table[interface_type].interface_type = interface_type;
			adapter->nx_status_callback_handler_table[interface_type].data           = data;
			adapter->nx_status_callback_handler_table[interface_type].registered     = 1;
			adapter->nx_status_callback_handler_table[interface_type].refcnt         = 0;
			rv = 0;
		}
		spin_unlock_bh(&adapter->nx_status_callback_handler_table[interface_type].lock);
	}

	return (rv);
}

/*
 * Unregisters callback handlers
 */
void nx_nic_rx_unregister_callback_handler(struct net_device *netdev, uint8_t interface_type)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;

	spin_lock_bh(&adapter->nx_status_callback_handler_table[interface_type].lock);

	if (adapter->nx_status_callback_handler_table[interface_type].registered) {
		adapter->nx_status_callback_handler_table[interface_type].data           = NULL;
		adapter->nx_status_callback_handler_table[interface_type].registered     = 0;
		adapter->nx_status_callback_handler_table[interface_type].refcnt         = 0;
	}

	spin_unlock_bh(&adapter->nx_status_callback_handler_table[interface_type].lock);
}

/*
 * Gets the port number of the device on a netxen adaptor if it is a netxen
 * device.
 *
 * Parameters:
 *	dev	- The device for which the port is requested.
 *
 * Returns:
 *	-1	- If not netxen device.
 *	port number on the adapter if it is netxen device.
 */
int nx_nic_get_device_port(struct net_device *netdev)
{
	struct unm_adapter_s *adapter = (unm_adapter *) netdev_priv(netdev);

	if (netdev->do_ioctl != unm_nic_ioctl) {
		return (-1);
	}

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id))
		return ((int)adapter->physical_port);
	else
		return ((int)adapter->portnum);
}

/*
 * Gets the ring context used by the device to talk to the card.
 *
 * Parameters:
 *	dev	- The device for which the ring context is requested.
 *
 * Returns:
 *	-EINVAL	- If not netxen device.
 *	Ring context number if it is netxen device.
 */
int nx_nic_get_device_ring_ctx(struct net_device *netdev)
{
	struct unm_adapter_s *adapter;

	if (netdev->do_ioctl != unm_nic_ioctl) {
		return (-EINVAL);
	}

	adapter = netdev->priv;
	/*
	 * TODO: send the correct context.
	 */
	return (adapter->portnum);
}

/*
 * Gets the pointer to pci device if the device maps to a netxen device.
 */
struct pci_dev *nx_nic_get_pcidev(struct net_device *dev)
{
	if (dev->priv) {
		return ((struct unm_adapter_s *)(dev->priv))->pdev;
	}
	return (NULL);
}

/*
 * Returns the revision id of card.
 */
int nx_nic_get_adapter_revision_id(struct net_device *dev)
{
	if (dev->priv) {
		return ((struct unm_adapter_s *)(dev->priv))->ahw.revision_id;
	}
	return -1;
}

/*
 * Send a group of cmd descs to the card.
 * Used for sending tnic message or nic notification.
 */
int nx_nic_send_cmd_descs(struct net_device *dev,
			  cmdDescType0_t * cmd_desc_arr, int nr_elements)
{
	uint32_t producer;
	struct unm_cmd_buffer *pbuf;
	cmdDescType0_t *cmd_desc;
	int i;
	int rv;
	unm_adapter *adapter = NULL;

	if (dev == NULL) {
                nx_nic_print4(NULL, "%s: Device is NULL\n", __FUNCTION__);
		return (-1);
	}

	adapter = (unm_adapter *) dev->priv;

#if defined(DEBUG)
        if (adapter == NULL) {
                nx_nic_print4(adapter, "%s Adapter not initialized, cannot "
			      "send request to card\n", __FUNCTION__);
                return (-1);
        }
#endif /* DEBUG */

	if (nr_elements > MAX_PENDING_DESC_BLOCK_SIZE || nr_elements == 0) {
		nx_nic_print4(adapter, "%s: Too many command descriptors in a "
			      "request\n", __FUNCTION__);
		return (-EINVAL);
	}

	i = 0;

	spin_lock_bh(&adapter->tx_lock);

	/* check if space is available */
	if (unm_get_pending_cmd_desc_cnt(&adapter->pending_cmds) ||
	    ((adapter->cmdProducer + nr_elements) >=
	     ((adapter->lastCmdConsumer <= adapter->cmdProducer) ?
	      adapter->lastCmdConsumer + adapter->MaxTxDescCount :
	      adapter->lastCmdConsumer))) {

		rv = nx_queue_pend_cmd_desc_list(adapter, cmd_desc_arr,
						 nr_elements);
		spin_unlock_bh(&adapter->tx_lock);
		return (rv);
	}

/*         adapter->cmdProducer = get_index_range(adapter->cmdProducer, */
/* 					       MaxTxDescCount, nr_elements); */

	producer = adapter->cmdProducer;
	do {
		cmd_desc = &cmd_desc_arr[i];

		pbuf = &adapter->cmd_buf_arr[producer];
		pbuf->mss = 0;
		pbuf->totalLength = 0;
		pbuf->skb = NULL;
		pbuf->cmd = 0;
		pbuf->fragCount = 0;
		pbuf->port = 0;

		/* adapter->ahw.cmdDescHead[producer] = *cmd_desc; */
		adapter->ahw.cmdDescHead[producer].word0 = cmd_desc->word0;
		adapter->ahw.cmdDescHead[producer].word1 = cmd_desc->word1;
		adapter->ahw.cmdDescHead[producer].word2 = cmd_desc->word2;
		adapter->ahw.cmdDescHead[producer].word3 = cmd_desc->word3;
		adapter->ahw.cmdDescHead[producer].word4 = cmd_desc->word4;
		adapter->ahw.cmdDescHead[producer].word5 = cmd_desc->word5;
		adapter->ahw.cmdDescHead[producer].word6 = cmd_desc->word6;
		adapter->ahw.cmdDescHead[producer].unused = cmd_desc->unused;

		producer = get_next_index(producer, adapter->MaxTxDescCount);
		i++;

	} while (i != nr_elements);

	adapter->cmdProducer = producer;

	adapter->ahw.cmdProducer = adapter->cmdProducer;

	/* write producer index to start the xmit */

	read_lock(&adapter->adapter_lock);
	unm_nic_update_cmd_producer(adapter, adapter->cmdProducer);
	read_unlock(&adapter->adapter_lock);

	spin_unlock_bh(&adapter->tx_lock);

	return (0);
}

/*
 * Send a TNIC message to the card.
 */
int nx_nic_send_msg_to_fw(struct net_device *dev,
			  pegnet_cmd_desc_t * cmd_desc_arr, int nr_elements)
{
	return nx_nic_send_cmd_descs(dev,
				     (cmdDescType0_t *) cmd_desc_arr,
				     nr_elements);
}

#if defined(XGB_DEBUG)
static void dump_skb(struct sk_buff *skb)
{
	printk("%s: SKB at %p\n", unm_nic_driver_name, skb);
	printk("    ->next: %p\n", skb->next);
	printk("    ->prev: %p\n", skb->prev);
	printk("    ->list: %p\n", skb->list);
	printk("    ->sk: %p\n", skb->sk);
	printk("    ->stamp: %lx\n", skb->stamp);
	printk("    ->dev: %p\n", skb->dev);
	//printk("    ->input_dev: %p\n", skb->input_dev);
	printk("    ->real_dev: %p\n", skb->real_dev);
	printk("    ->h.raw: %p\n", skb->h.raw);

	printk("    ->len: %lx\n", skb->len);
	printk("    ->data_len:%lx\n", skb->data_len);
	printk("    ->mac_len:%lx\n", skb->mac_len);
	printk("    ->csum:%lx\n", skb->csum);

	printk("    ->head:%p\n", skb->head);
	printk("    ->data:%p\n", skb->data);
	printk("    ->tail:%p\n", skb->tail);
	printk("    ->end:%p\n", skb->end);

	return;
}

static int skb_is_sane(struct sk_buff *skb)
{
	int ret = 1;

	if (skb_is_nonlinear(skb)) {
		nx_nic_print3(adapter, "Got a non-linear SKB @%p data_len:"
			      "%d back\n", skb, skb->data_len);
		return 0;
	}

#if 0
	if (skb->list) {
		return 0;
	}
#endif
	if (skb->dev != NULL || skb->next != NULL || skb->prev != NULL)
		return 0;
	if (skb->data == NULL || skb->data > skb->tail)
		return 0;
	if (*(unsigned long *)skb->head != 0xc0debabe) {
                nx_nic_print5(adapter, "signature not found\n");
		return 0;
	}

	return ret;
}
#endif

/* Process Command status ring */
static int unm_process_cmd_ring(unsigned long data)
{
	uint32_t lastConsumer;
	uint32_t consumer;
	unm_adapter *adapter = (unm_adapter *) data;
	int count1 = 0;
	int count2 = 0;
	struct unm_cmd_buffer *buffer;
	struct pci_dev *pdev;
	struct unm_skb_frag *frag;
	struct sk_buff *skb = NULL;
	int done;

	lastConsumer = adapter->lastCmdConsumer;
	consumer = *(adapter->cmdConsumer);

	if (lastConsumer == consumer) {	/* Ring is empty    */
                nx_nic_print7(adapter, "lastConsumer %d == consumer %d\n",
			      lastConsumer, consumer);
		return (1);
	}

	while ((lastConsumer != consumer) && (count1 < MAX_STATUS_HANDLE)) {
		buffer = &adapter->cmd_buf_arr[lastConsumer];
		pdev = adapter->pdev;
		frag = &buffer->fragArray[0];
		skb = buffer->skb;
		if (skb && (cmpxchg(&buffer->skb, skb, 0) == skb)) {
			uint32_t i;

			nx_free_frag_bounce_buf(adapter, frag);

			pci_unmap_single(pdev, frag->dma, frag->length,
					 PCI_DMA_TODEVICE);
			for (i = 1; i < buffer->fragCount; i++) {
                                nx_nic_print7(adapter, "get fragment no %d\n",
					      i);
				frag++;	/* Get the next frag */
				nx_free_frag_bounce_buf(adapter, frag);
				pci_unmap_page(pdev, frag->dma, frag->length,
					       PCI_DMA_TODEVICE);
			}
#if defined(UNM_LOOPBACK)
			if (adapter->testCtx.loopback_start
			    && (unm_post_skb(adapter, skb)
				== 0)) {
				continue;
			} else {
#endif /* UNM_LOOPBACK */

				//adapter->stats.skbfreed++;
				dev_kfree_skb_any(skb);
				skb = NULL;
#if defined(UNM_LOOPBACK)
			}
#endif
		}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
		if (unlikely(netif_queue_stopped(adapter->netdev)
			     && netif_carrier_ok(adapter->netdev))
		    && ((jiffies - adapter->netdev->trans_start) >
			adapter->netdev->watchdog_timeo)) {
			//SCHEDULE_WORK(adapter->tx_timeout_task + adapter->portnum);
			SCHEDULE_WORK(adapter->tx_timeout_task);
		}
#endif

		lastConsumer = get_next_index(lastConsumer,
					      adapter->MaxTxDescCount);
		count1++;
	}
	//adapter->stats.noxmitdone += count1;

	count2 = 0;
	while ((lastConsumer != consumer) && (count2 < MAX_STATUS_HANDLE)) {
		buffer = &adapter->cmd_buf_arr[lastConsumer];
		count2++;
		if (buffer->skb) {
			break;
		} else {
			lastConsumer = get_next_index(lastConsumer,
						      adapter->MaxTxDescCount);
		}
	}
	if (count1 || count2) {
		adapter->lastCmdConsumer = lastConsumer;
		smp_mb();
		if(netif_queue_stopped(adapter->netdev)
			&& (adapter->state == PORT_UP)
			&& (adapter->status & NETDEV_STATUS)) {
				spin_lock(&adapter->tx_lock);
				netif_wake_queue(adapter->netdev);
				adapter->status &= ~NETDEV_STATUS;
				spin_unlock(&adapter->tx_lock);
		}
	}

	/*
	 * If everything is freed up to consumer then check if the ring is full
	 * If the ring is full then check if more needs to be freed and
	 * schedule the call back again.
	 *
	 * This happens when there are 2 CPUs. One could be freeing and the
	 * other filling it. If the ring is full when we get out of here and
	 * the card has already interrupted the host then the host can miss the
	 * interrupt.
	 *
	 * There is still a possible race condition and the host could miss an
	 * interrupt. The card has to take care of this.
	 */
	consumer = *(adapter->cmdConsumer);
	done = (adapter->lastCmdConsumer == consumer);

	return (done);
}

#if defined(UNM_LOOPBACK)
/* FIXME: this causes ip checksum errors on smartbits, at times */
static int unm_post_skb(struct unm_adapter_s *adapter, struct sk_buff *skb)
{
	unm_recv_context_t *recv_ctx = &adapter->recv_ctx;
	uint producer = recv_ctx->rcvProducer;
	struct pci_dev *pdev = adapter->ahw.pdev;
	struct _hardware_context *hw = &(adapter->ahw);
	rcvDesc_t *pdesc;
	struct unm_rx_buffer *buffer;
	int do_bounce;

	if ((skb->end - skb->data) < rcv_desc->skb_size ||
	    (skb_shinfo(skb)->nr_frags > 0)) {
		return -1;
	}
	skb->data = skb->head;
	skb->tail = skb->head;
	skb->len = 0;
	skb->cloned = 0;
	skb->data_len = 0;

	buffer = &rcv_desc->rx_buf_arr[rcv_desc->begin_alloc];

	if (!adapter->ahw.cut_through) {
		skb_reserve(skb, IP_ALIGNMENT_BYTES);
	}

	buffer->skb = skb;
	buffer->state = UNM_BUFFER_BUSY;

#ifdef  ESX_3X
	buffer->dma = skb->headMA;
#else
	buffer->dma = pci_map_single(pdev, skb->data, rcv_desc->dma_size,
				     PCI_DMA_FROMDEVICE);
#endif

	pdesc = &adapter->recv_ctx.rcvDescHead[producer];
	pdesc->referenceHandle = buffer->refHandle;
	pdesc->bufferLength = rcv_desc->dma_size;
	pdesc->AddrBuffer = buffer->dma;

	producer = get_next_index(producer, adapter->MaxRxDescCount);
	recv_ctx->freeRxCount--;
	recv_ctx->pendingRxCount++;
	recv_ctx->rcvProducer = producer;
	/* Window = 1 */
	read_lock(&adapter->adapter_lock);
	#error "recv_crb_registers is obsolete"
	 data = (producer - 1) & (adapter->MaxRxDescCount - 1);
	 adapter->unm_nic_hw_write_wx(adapter,
			     recv_crb_registers[ctx].CRB_RCV_PRODUCER_OFFSET,
			     &data, 4);
	read_unlock(&adapter->adapter_lock);

	//adapter->stats.goodskbposts++;
	rcv_desc->begin_alloc = get_next_index(rcv_desc->begin_alloc,
					       rcv_desc->MaxRxDescCount);

	return 0;
}
#endif /* UNM_LOOPBACK */

/*
 *
 */
static int nx_alloc_rx_skb(struct unm_adapter_s *adapter,
			   nx_host_rds_ring_t * nxhal_rds_ring,
			   struct unm_rx_buffer *buffer)
{
	struct sk_buff *skb;
	dma_addr_t dma;
	int do_bounce;
	rds_host_ring_t *host_rds_ring = NULL;

	host_rds_ring = (rds_host_ring_t *) nxhal_rds_ring->os_data;
	skb = netdev_alloc_skb(adapter->netdev, nxhal_rds_ring->buff_size);

	if (unlikely(!skb)) {
		nx_nic_print6(adapter, "%s: alloc of skb failed requested size %lld\n",
			      __FUNCTION__, nxhal_rds_ring->buff_size);
		return (-ENOMEM);
	}
#if defined(XGB_DEBUG)
	*(unsigned long *)(skb->head) = 0xc0debabe;
	if (skb_is_nonlinear(skb)) {
		nx_nic_print3(adapter, "Allocated SKB %p is nonlinear\n", skb);
	}
#endif

	if (!adapter->ahw.cut_through) {
		skb_reserve(skb, IP_ALIGNMENT_BYTES);
	}

	/* This will be setup when we receive the
	 * buffer after it has been filled  FSL  TBD TBD
	 * skb->dev = netdev;
	 */
	do_bounce = try_map_skb_data(adapter, skb, host_rds_ring->dma_size,
				     PCI_DMA_FROMDEVICE, &dma);

#ifdef  ESX
	if ( NX_IS_REVISION_P2(adapter->ahw.revision_id) && 
			((dma + host_rds_ring->dma_size) >= adapter->dma_mask)) {
		struct vmk_bounce *bounce = &host_rds_ring->vmk_bounce;
		struct nx_cmd_struct *bounce_buf;
		unsigned long flags;

		BOUNCE_LOCK(&bounce->lock, flags);

		if (TAILQ_EMPTY(&bounce->free_vmk_bounce)) {
			dev_kfree_skb_any(skb);
			skb = NULL;
			buffer->skb = NULL;
			buffer->state = UNM_BUFFER_FREE;
			BOUNCE_UNLOCK(&bounce->lock, flags);
			return (-ENOMEM);
		}

		bounce_buf = TAILQ_FIRST(&bounce->free_vmk_bounce);
		TAILQ_REMOVE(&bounce->free_vmk_bounce, bounce_buf, link);

		dma = bounce_buf->phys;
		bounce_buf->busy = 1;
		buffer->bounce_buf = bounce_buf;
		BOUNCE_UNLOCK(&bounce->lock, flags);
	} else {
		buffer->bounce_buf = NULL;
	}
#endif
	buffer->skb = skb;
	buffer->state = UNM_BUFFER_BUSY;
	buffer->dma = dma;

	return (0);
}

/**
 * unm_post_rx_buffers puts buffer in the Phantom memory
 **/
int unm_post_rx_buffers(struct unm_adapter_s *adapter, nx_host_rx_ctx_t *nxhal_rx_ctx ,uint32_t ringid)
{
	rds_host_ring_t *host_rds_ring = NULL;
	nx_host_rds_ring_t *nxhal_rds_ring = NULL;
	nx_free_rbufs_t free_list;


	nxhal_rds_ring = &nxhal_rx_ctx->rds_rings[ringid];
	host_rds_ring = (rds_host_ring_t *) nxhal_rds_ring->os_data;

	spin_lock_bh(&adapter->buf_post_lock);
	if (host_rds_ring->posting
	    || TAILQ_EMPTY(&host_rds_ring->free_rxbufs.head)) {
		spin_unlock_bh(&adapter->buf_post_lock);
		return (0);
	}

	TAILQ_COPY(&host_rds_ring->free_rxbufs.head, &free_list.head, link);
	TAILQ_INIT(&host_rds_ring->free_rxbufs.head);
	free_list.count = host_rds_ring->free_rxbufs.count;
	host_rds_ring->free_rxbufs.count = 0;

	host_rds_ring->posting = 1;
	spin_unlock_bh(&adapter->buf_post_lock);

	return (nx_post_rx_descriptors
		(adapter, nxhal_rds_ring, ringid, &free_list));
}

/*
 * Test packets.
 */
unsigned char XMIT_DATA[] = { 0x50, 0xda, 0x2e, 0xfa, 0x77, 0x00, 0x02,
	0x2d, 0x8a, 0xa1, 0xde, 0x08, 0x00, 0x45, 0x00,
	0x00, 0x32, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11,
	0xc2, 0x95, 0xc0, 0xa8, 0x7b, 0x64, 0xc0, 0xa8,
	0x7b, 0x70, 0x80, 0x09, 0x1c, 0x0d, 0x00, 0x1e,
	0xb1, 0x5a, 0x1f, 0x00, 0x00, 0x00, 0x58, 0x58,
	0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58,
	0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58
};

unm_send_test_t tx_args;

int
tx_test(struct net_device *netdev, unm_send_test_t * arg,
	unm_test_ctr_t * testCtx)
{
	unsigned int count = arg->count;
	struct sk_buff *skb;
	unsigned int len;
	unsigned char *pkt_data;
	unsigned int sent = 0;

	if (testCtx->tx_user_packet_data == NULL) {
		pkt_data = XMIT_DATA;
		len = sizeof(XMIT_DATA);
	} else {
		pkt_data = testCtx->tx_user_packet_data;
		len = testCtx->tx_user_packet_length;
	}

	while (testCtx->tx_stop == 0) {

		skb = alloc_skb(len, GFP_KERNEL);
		if (!skb) {
			return -ENOMEM;
		}
		*((uint16_t *) (pkt_data + 14)) = (uint16_t) sent;
		memcpy(skb->data, pkt_data, len);
		skb_put(skb, len);
		unm_nic_xmit_frame(skb, netdev);
		++sent;
		if (arg->count && --count == 0) {
			break;
		}

		schedule();
		if (arg->ifg) {
			if (arg->ifg < 1024) {
				udelay(arg->ifg);
			} else if (arg->ifg < 5000) {
				mdelay(arg->ifg >> 10);
			} else {
				mdelay(5);
			}
		}
		if (signal_pending(current)) {
			break;
		}
	}
	//printk("unm packet generator sent %d bytes %d packets %d bytes each\n",
	//                      sent*len, sent, len);
	return 0;
}

static int
unm_send_test(struct net_device *netdev, void *ptr, unm_test_ctr_t * testCtx)
{
	unm_send_test_t args;

	if (copy_from_user(&args, ptr, sizeof(args))) {
		return -EFAULT;
	}
	switch (args.cmd) {
	case UNM_TX_START:
		if (tx_args.cmd == UNM_TX_SET_PARAM) {
			if (args.ifg == 0) {
				args.ifg = tx_args.ifg;
			}
			if (args.count == 0) {
				args.count = tx_args.count;
			}
		}
		testCtx->tx_stop = 0;
		return tx_test(netdev, &args, testCtx);
		break;

	case UNM_TX_STOP:
		testCtx->tx_stop = 1;
		break;

	case UNM_TX_SET_PARAM:
		tx_args = args;
		break;

	case UNM_TX_SET_PACKET:
		if (testCtx->tx_user_packet_data) {
			kfree(testCtx->tx_user_packet_data);
		}
		testCtx->tx_user_packet_data = kmalloc(args.count, GFP_KERNEL);
		if (testCtx->tx_user_packet_data == NULL) {
			return -ENOMEM;
		}
		if (copy_from_user(testCtx->tx_user_packet_data,
				   (char *)(uptr_t) args.ifg, args.count)) {
			kfree(testCtx->tx_user_packet_data);
			testCtx->tx_user_packet_data = NULL;
			return -EFAULT;
		}
		testCtx->tx_user_packet_length = args.count;
		break;

	case UNM_LOOPBACK_START:
		testCtx->loopback_start = 1;
		netdev->hard_start_xmit = &unm_loopback_xmit_frame;
		break;

	case UNM_LOOPBACK_STOP:
		testCtx->loopback_start = 0;
		netdev->hard_start_xmit = &unm_nic_xmit_frame;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int unm_irq_test(unm_adapter *adapter) 
{
	nx_dev_handle_t drv_handle;
	uint64_t pre_int_cnt, post_int_cnt;
	nx_rcode_t rcode = NX_RCODE_SUCCESS;
	drv_handle = adapter->nx_dev->nx_drv_handle;

	pre_int_cnt = adapter->stats.ints;
	rcode = issue_cmd(drv_handle,
			adapter->ahw.pci_func,
			NXHAL_VERSION,
			//adapter->nx_dev->rx_ctxs[0]->this_id,
			adapter->portnum,
			0,
			0,
			NX_CDRP_CMD_GEN_INT);
	
	if (rcode != NX_RCODE_SUCCESS) {
		return -1;
	}
	mdelay(50);
	post_int_cnt = adapter->stats.ints;
	
	return ((pre_int_cnt == post_int_cnt) ? (-1) : (0));
}

int
unm_loopback_test(struct net_device *netdev, int fint, void *ptr,
		  unm_test_ctr_t * testCtx)
{
	int ii, ret;
	unsigned char *data;
	unm_send_test_t args;
	struct unm_adapter_s *adapter;
	__uint32_t val;

	adapter = (struct unm_adapter_s *)netdev->priv;

	if ((fint) && (adapter->ahw.boardcfg.board_type == UNM_BRDTYPE_P2_SB31_10G_IMEZ
		|| adapter->ahw.boardcfg.board_type ==
		UNM_BRDTYPE_P2_SB31_10G_HMEZ || adapter->ahw.boardcfg.board_type == UNM_BRDTYPE_P3_10G_CX4_LP))
		return (-LB_NOT_SUPPORTED);
	
	if((!fint) && NX_IS_REVISION_P3(adapter->ahw.revision_id))
		return (-LB_NOT_SUPPORTED);

	if((fint) && (port_mode != UNM_PORT_MODE_XG))
		return (-LB_NOT_SUPPORTED);

	ret = 0;
	if (ptr) {
		if ((ret = copy_from_user(&args, ptr, sizeof(args)))) {
			return (-LB_UCOPY_PARAM_ERR);
		}
	} else
		memset(&args, 0, sizeof(args));

	if (testCtx->tx_user_packet_data != NULL) {
		kfree(testCtx->tx_user_packet_data);
		testCtx->tx_user_packet_data = NULL;
	}

	if (args.count != 0) {	/* user data */
		testCtx->tx_user_packet_data =
		    kmalloc(args.count + 16, GFP_KERNEL);
		if (testCtx->tx_user_packet_data == NULL)
			return (-LB_NOMEM_ERR);
		memset(testCtx->tx_user_packet_data, 0xFF, 16);
		if (ptr)
			if (copy_from_user(testCtx->tx_user_packet_data + 16,
					   (char *)(uptr_t) args.ifg,
					   args.count)) {
				kfree(testCtx->tx_user_packet_data);
				testCtx->tx_user_packet_data = NULL;
				return (-LB_UCOPY_DATA_ERR);
			}
		testCtx->tx_user_packet_length = args.count + 16;
		testCtx->rx_datalen =
		    (testCtx->tx_user_packet_length - 14) * 16;
	} else {		/* use local data */
		testCtx->tx_user_packet_data =
		    kmalloc(sizeof(XMIT_DATA) + 16, GFP_KERNEL);
		if (testCtx->tx_user_packet_data == NULL)
			return (-LB_NOMEM_ERR);
		memset(testCtx->tx_user_packet_data, 0xFF, 14);
		memcpy(testCtx->tx_user_packet_data + 16, XMIT_DATA,
		       sizeof(XMIT_DATA));
		testCtx->tx_user_packet_length = sizeof(XMIT_DATA) + 16;
		testCtx->rx_datalen = (sizeof(XMIT_DATA) + 2) * 16;
	}
	if ((testCtx->rx_user_packet_data =
	     kmalloc(testCtx->rx_datalen, GFP_KERNEL)) == NULL) {
		ret = -LB_NOMEM_ERR;
		goto end;
	}
	testCtx->rx_user_pos = 0;
	if (netif_running(netdev)) {
		netif_stop_queue(netdev);
	}

	if (fint == 1) {
		if (phy_lock(adapter))
			goto lock_fail;
		/* halt peg 0 */
		unm_nic_write_w0(adapter, UNM_CRB_PEG_NET_0 + 0x3c, 1);
		mdelay(1000);	/* wait for halt */
		phy_unlock(adapter);
		if (xge_loopback(adapter, 1)) {
			nx_nic_print3(adapter, "phy_lock Failed\n");
			goto lock_fail;
		}
		if (phy_lock(adapter))
			goto end;
		testCtx->loopback_start = 1;
	}

	testCtx->tx_stop = 0;
	testCtx->capture_input = 1;
	args.count = 16;
	if ((ret = tx_test(netdev, &args, testCtx)) != 0) {
		ret = -LB_TX_NOSKB_ERR;
		testCtx->capture_input = 0;
		goto end;
	}

	mdelay(1000);		/* wait for data to come back */
	testCtx->capture_input = 0;

	if (testCtx->rx_user_pos != testCtx->rx_datalen) {
		ret = -LB_SHORT_DATA_ERR;
		goto end;
	}

	data = testCtx->rx_user_packet_data;
	/* check received bytes against tx_user_packet_data */
	for (ii = 0; ii < 16; ++ii) {
		if (*((uint16_t *) data) != (uint16_t) ii) {
			ret = -LB_SEQUENCE_ERR;
			goto end;
		}
		if (memcmp(testCtx->tx_user_packet_data + 16, data + 2,
			   testCtx->tx_user_packet_length - 16)
		    != 0) {
			ret = -LB_DATA_ERR;
			goto end;
		}
		data += (testCtx->tx_user_packet_length - 14);
	}
	ret = LB_TEST_OK;

      end:
	testCtx->tx_stop = 1;
	if (fint == 1) {
		phy_unlock(adapter);
		if (xge_loopback(adapter, 0))
			nx_nic_print3(adapter, "phy_lock Failed\n");
		/* unhalt peg 0 */
		unm_nic_read_w0(adapter, UNM_CRB_PEG_NET_0 + 0x30, &val);
		unm_nic_write_w0(adapter, UNM_CRB_PEG_NET_0 + 0x30, val);
		testCtx->loopback_start = 0;
	}
      lock_fail:

	if (netif_running(netdev)) {
		netif_wake_queue(netdev);
	}

	if (testCtx->tx_user_packet_data != NULL) {
		kfree(testCtx->tx_user_packet_data);
		testCtx->tx_user_packet_data = NULL;
	}
	kfree(testCtx->rx_user_packet_data);
	testCtx->rx_user_packet_data = NULL;
	return ret;
}


#if defined(UNM_IP_FILTER)
/*
 * unm_ip_filter() - provides the hooks to enable the user application to
 * set ip filters on the UNM card.
 */

static int unm_ip_filter(struct unm_adapter_s *adapter, void *ptr)
{
	uint32_t producer;
	uint32_t next;
	controlCmdDesc_t controldesc;
	cmdDescType0_t *hwdesc;
	unm_ip_filter_request_t req;

	if (copy_from_user(&req, ptr, sizeof(req))) {
		return -EFAULT;
	}

	switch (req.cmd) {
	case UNM_IP_ADDR_ADD:
	case UNM_IP_ADDR_DEL:
		if (req.request.count <= 0 ||
		    req.request.count > UNM_IP_ADDR_MAX_COUNT) {
			return -EINVAL;
		}
		memcpy(controldesc.ip_addr, &req.request,
		       sizeof(req.request.count) +
		       req.request.count * sizeof(req.request.ip_addr[0]));
		controldesc.count = req.request.count;

		break;
	case UNM_IP_ADDR_SHOW:
	case UNM_IP_ADDR_CLEAR:
		controldesc.count = 0;
		break;

	default:
		return -EINVAL;
	}

	/*
	 * Now that we got the command, send it out.
	 */

	controldesc.opcode = UNM_CONTROL_OP;
	controldesc.cmd = req.cmd;
	spin_lock_bh(&adapter->tx_lock);
	producer = adapter->cmdProducer;
	if ((next = get_next_index(producer, adapter->MaxTxDescCount)) ==
	    adapter->lastCmdConsumer) {
		spin_unlock_bh(&adapter->tx_lock);
		return -EAGAIN;
	}
	hwdesc = &(adapter->ahw.cmdDescHead[producer]);
	memcpy(hwdesc, &controldesc, sizeof(controldesc));
	adapter->cmdProducer = next;
	adapter->ahw.cmdProducer = next;
	/* Window = 1 */

	read_lock(&adapter->adapter_lock);
	adapter->unm_nic_hw_write_wx(adapter, adapter->crb_addr_cmd_producer,
				      &next, 4);
	read_unlock(&adapter->adapter_lock);

	spin_unlock_bh(&adapter->tx_lock);

	return 0;
}
#endif /* UNM_IP_FILTER */

int unm_link_test(unm_adapter * adapter)
{
	int rv;
	rv = unm_link_ok(adapter);
	return ((rv == 0) ? (-1) : (0));
}

int unm_led_test(unm_adapter *adapter)
{
	int rv;
	long phy_id;

	adapter->ahw.LEDTestRet = LED_TEST_OK;
	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		return (-LED_TEST_NOT_SUPPORTED);
	}
	//For MEZZ boards we do not support LED Test
        if (adapter->ahw.boardcfg.board_type == UNM_BRDTYPE_P2_SB31_10G_IMEZ ||
	    adapter->ahw.boardcfg.board_type == UNM_BRDTYPE_P2_SB31_10G_HMEZ) {

                rv = adapter->ahw.LEDTestRet = LED_TEST_NOT_SUPPORTED;
                return rv;
        }
        //Determine if it is a quake or mysticom phy
        phy_id = unm_xge_mdio_rd(adapter, DEV_PMA_PMD, PMD_IDENTIFIER);

        if (adapter->ahw.LEDState) {   // already on? then turn it off

                adapter->ahw.LEDState = 0;
		if (phy_id == PMD_ID_MYSTICOM) {
			UNM_CRB_WRITELIT_ADAPTER(UNM_ROMUSB_GPIO(0), LED_OFF,
						 adapter);
			UNM_CRB_WRITELIT_ADAPTER(UNM_ROMUSB_GPIO(1), LED_OFF,
						 adapter);
		} else if (phy_id == PMD_ID_QUAKE) {
			//turn off
			unm_xge_mdio_wr(adapter, DEV_PMA_PMD, 0xd006, 0x4);
			unm_xge_mdio_wr(adapter, DEV_PMA_PMD, 0xd007, 0x4);
		} else {
			(adapter->ahw).LEDTestRet = LED_TEST_UNKNOWN_PHY;
				nx_nic_print4(adapter, "LED test UNKNOWN "
					      "PHY %ld\n", phy_id);
		}

	} else {		// off, so turn it on...

               adapter->ahw.LEDState = 1;
		if (phy_id == PMD_ID_MYSTICOM) {
			UNM_CRB_WRITELIT_ADAPTER(UNM_ROMUSB_GPIO(0), LED_ON,
						 adapter);
			UNM_CRB_WRITELIT_ADAPTER(UNM_ROMUSB_GPIO(1), LED_ON,
						 adapter);

		} else if (phy_id == PMD_ID_QUAKE) {
			//turn on
			unm_xge_mdio_wr(adapter, DEV_PMA_PMD, 0xd006, 0x5);
			unm_xge_mdio_wr(adapter, DEV_PMA_PMD, 0xd007, 0x5);
		} else {
			(adapter->ahw).LEDTestRet = LED_TEST_UNKNOWN_PHY;
			       nx_nic_print4(adapter, "LED test UNKNOWN "
					     "PHY %ld\n", phy_id);
		}
	}
	//Reinitialize LED back to original state
        if (adapter->ahw.LEDTestLast) {
		if (phy_id == PMD_ID_QUAKE) {
			/* set up LEDs for Quake */
			unm_xge_mdio_wr(adapter, DEV_PMA_PMD, 0xd006, 9);
			unm_xge_mdio_wr(adapter, DEV_PMA_PMD, 0xd007, 2);
		}
		//don't know what to do for MYSTICOM
	}

       rv = adapter->ahw.LEDTestRet;
	return (rv);
}

int unmtest_pegstuck(unm_crbword_t addr, U64 reg, int loop,
		     struct unm_adapter_s *adapter)
{
	int i;
	unm_crbword_t temp;

	for (i = 0; i < loop; ++i) {
		UNM_CRB_READ_CHECK_ADAPTER(reg, &temp, adapter);
		if (temp != addr)
			return (0);
	}
	return (-1);
}

#define NIC_NUMPEGS                3

static int unm_hw_test(unm_adapter *adapter)
{
	unm_crbword_t temp;
	int ii, rc = HW_TEST_OK;
	uint64_t base, address;

	/* DMA Status */
	UNM_CRB_READ_CHECK_ADAPTER(UNM_DMA_COMMAND(0), &temp, adapter);
	if ((temp & 0x3) == 0x3) {
		rc = -HW_DMA_BZ_0;
		goto done;
	}
	UNM_CRB_READ_CHECK_ADAPTER(UNM_DMA_COMMAND(1), &temp, adapter);
	if ((temp & 0x3) == 0x3) {
		rc = -HW_DMA_BZ_1;
		goto done;
	}
	UNM_CRB_READ_CHECK_ADAPTER(UNM_DMA_COMMAND(2), &temp, adapter);
	if ((temp & 0x3) == 0x3) {
		rc = -HW_DMA_BZ_2;
		goto done;
	}
	UNM_CRB_READ_CHECK_ADAPTER(UNM_DMA_COMMAND(3), &temp, adapter);
	if ((temp & 0x3) == 0x3) {
		rc = -HW_DMA_BZ_3;
		goto done;
	}

	/* SRE Status */
	UNM_CRB_READ_CHECK_ADAPTER(UNM_SRE_PBI_ACTIVE_STATUS, &temp, adapter);
	if ((temp & 0x1) == 0) {
		rc = -HW_SRE_PBI_HALT;
		goto done;
	}
	UNM_CRB_READ_CHECK_ADAPTER(UNM_SRE_L1RE_CTL, &temp, adapter);
	if ((temp & 0x20000000) != 0) {
		rc = -HW_SRE_L1IPQ;
		goto done;
	}
	UNM_CRB_READ_CHECK_ADAPTER(UNM_SRE_L2RE_CTL, &temp, adapter);
	if ((temp & 0x20000000) != 0) {
		rc = -HW_SRE_L2IFQ;
		goto done;
	}

	UNM_CRB_READ_CHECK_ADAPTER(UNM_SRE_INT_STATUS, &temp, adapter);
	if ((temp & 0xc0ff) != 0) {
		if ((temp & 0x1) != 0) {
			rc = -HW_PQ_W_PAUSE;
			goto done;
		}
		if ((temp & 0x2) != 0) {
			rc = -HW_PQ_W_FULL;
			goto done;
		}
		if ((temp & 0x4) != 0) {
			rc = -HW_IFQ_W_PAUSE;
			goto done;
		}
		if ((temp & 0x8) != 0) {
			rc = -HW_IFQ_W_FULL;
			goto done;
		}
		if ((temp & 0x10) != 0) {
			rc = -HW_MEN_BP_TOUT;
			goto done;
		}
		if ((temp & 0x20) != 0) {
			rc = -HW_DOWN_BP_TOUT;
			goto done;
		}
		if ((temp & 0x40) != 0) {
			rc = -HW_FBUFF_POOL_WM;
			goto done;
		}
		if ((temp & 0x80) != 0) {
			rc = -HW_PBUF_ERR;
			goto done;
		}
		if ((temp & 0x4000) != 0) {
			rc = -HW_FM_MSG_HDR;
			goto done;
		}
		if ((temp & 0x8000) != 0) {
			rc = -HW_FM_MSG;
			goto done;
		}
	}

	UNM_CRB_READ_CHECK_ADAPTER(UNM_SRE_INT_STATUS, &temp, adapter);

	if ((temp & 0x3f00) != 0) {
		if ((temp & 0x100) != 0) {
			rc = -HW_EPG_MSG_BUF;
			goto done;
		}
		if ((temp & 0x200) != 0) {
			rc = -HW_EPG_QREAD_TOUT;
			goto done;
		}
		if ((temp & 0x400) != 0) {
			rc = -HW_EPG_QWRITE_TOUT;
			goto done;
		}
		if ((temp & 0x800) != 0) {
			rc = -HW_EPG_CQ_W_FULL;
			goto done;
		}
		if ((temp & 0x1000) != 0) {
			rc = -HW_EPG_MSG_CHKSM;
			goto done;
		}
		if ((temp & 0x2000) != 0) {
			rc = -HW_EPG_MTLQ_TOUT;
			goto done;
		}
	}

	/* Pegs */
	for (ii = 0; ii < NIC_NUMPEGS; ++ii) {
		base = PEG_NETWORK_BASE(ii);
		address = base | CRB_REG_EX_PC;
		UNM_CRB_READ_CHECK_ADAPTER(address, &temp, adapter);
		rc = unmtest_pegstuck(temp, address, PEG_LOOP, adapter);
		if (rc != 0) {
			rc = -(HW_PEG0 + ii);
			goto done;
		}
	}

      done:
	return (rc);
}

static int unm_cis_test(unm_adapter *adapter)
{
	//unm_crbword_t         temp, temp1;
	int ret = CIS_TEST_OK;
	//ctx_msg         msg = {0};

	/* Read CAM RAM CRB registers: producer and consumer index for
	   command descriptors. Make sure that P==C or P-C=n0,
	   where n is acceptable number (what range?) */
	/*
	   UNM_CRB_READ_CHECK_ADAPTER(CRB_CMD_PRODUCER_OFFSET, &temp, adapter);
	   UNM_CRB_READ_CHECK_ADAPTER(CRB_CMD_CONSUMER_OFFSET, &temp1, adapter);
	 */
#if 0
	read_lock(&adapter->adapter_lock);
	*((uint32_t *) & msg) =
	    UNM_NIC_PCI_READ_32(DB_NORMALIZE(adapter,
					     UNM_CMD_PRODUCER_OFFSET(0)));
	read_unlock(&adapter->adapter_lock);
	temp = msg.Count;

	//temp1 = adapter->ctxDesc->CMD_CONSUMER_OFFSET;
	temp1 = *(adapter->cmdConsumer);

	if (DIDX_DIF(temp, temp1) > CIS_WATERMARK)
		ret = -CIS_WMARK;
#endif
	return (ret);
}

static int unm_cr_test(unm_adapter *adapter)
{

	unm_crbword_t temp;
	int ret = CR_TEST_OK;

	UNM_CRB_READ_CHECK_ADAPTER(UNM_CRB_PCIE, &temp, adapter);

	/* at least one bit of bits 0-2 must be set */
	if ((temp & 0xFFFF) != PCI_VENDOR_ID_NX) {
		// Vendor ID is itself wrong. Report definite error
		ret = -CR_ERROR;
	}
	return (ret);
}

/*
 * unm_nic_ioctl ()    We provide the tcl/phanmon support through these
 * ioctls.
 */
static int unm_nic_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	int err = 0;
	unsigned long nr_bytes = 0;
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	char dev_name[UNM_NIC_NAME_LEN];
	int count;

        nx_nic_print7(adapter, "doing ioctl\n");

	if ((cmd != UNM_NIC_NAME) && !capable(CAP_NET_ADMIN))
		return -EPERM;

        switch (cmd) {
        case UNM_NIC_CMD:
                err = unm_nic_do_ioctl(adapter, (void *) ifr->ifr_data);
                break;

        case UNM_NIC_NAME:
                nx_nic_print7(adapter, "ioctl cmd for UNM\n");
                if (ifr->ifr_data) {
                        sprintf(dev_name, "%s-%d", UNM_NIC_NAME_RSP,
                                adapter->portnum);
                        nr_bytes = copy_to_user((char *)ifr->ifr_data,
						dev_name, UNM_NIC_NAME_LEN);
                        if (nr_bytes)
				err = -EIO;
                }
                break;

        case UNM_NIC_SEND_TEST:
                err = unm_send_test(netdev, (void *)ifr->ifr_data,
				    &adapter->testCtx);
                break;

#if defined(UNM_IP_FILTER)
        case UNM_NIC_IP_FILTER:
                if (adapter->is_up == ADAPTER_UP_MAGIC) {
			err = unm_ip_filter(adapter, (void *)ifr->ifr_data);
		} else {
			nx_nic_print5(adapter, "Adapter resources are not "
				      "initialized\n");
			err = -ENOSYS;
                }
                break;
#endif /* UNM_IP_FILTER */

        case UNM_NIC_IRQ_TEST:
                err = unm_irq_test(adapter);
                break;

        case UNM_NIC_ILB_TEST:
                if (adapter->is_up == ADAPTER_UP_MAGIC) {
			if (!(ifr->ifr_data))
				err = -LB_UCOPY_PARAM_ERR;
			else
				err = unm_loopback_test(netdev, 1,
							(void *)ifr->ifr_data,
							&adapter->testCtx);
		} else {
			nx_nic_print5(adapter, "Adapter resources not "
				      "initialized\n");
			err = -ENOSYS;
                }
                break;

	case UNM_NIC_ELB_TEST:
		if (adapter->is_up == ADAPTER_UP_MAGIC) {
			if (!(ifr->ifr_data))
				err = -LB_UCOPY_PARAM_ERR;
			else
				err = unm_loopback_test(netdev, 0,
							(void *)ifr->ifr_data,
							&adapter->testCtx);
		} else {
			nx_nic_print3(adapter,
				      "Adapter resources not initialized\n");
			err = -ENOSYS;
		}
		break;

	case UNM_NIC_LINK_TEST:
		err = unm_link_test(adapter);
		break;

	case UNM_NIC_HW_TEST:
		err = unm_hw_test(adapter);
		break;

	case UNM_NIC_CIS_TEST:
		err = unm_cis_test(adapter);
		break;

	case UNM_NIC_CR_TEST:
		err = unm_cr_test(adapter);
		break;

	case UNM_NIC_LED_TEST:
		(adapter->ahw).LEDTestLast = 0;
		for (count = 0; count < 8; count++) {
			//Need to restore LED on last test
			if (count == 7)
				(adapter->ahw).LEDTestLast = 1;
			err = unm_led_test(adapter);
			mdelay(100);
		}
		break;

	default:
                nx_nic_print7(adapter, "ioctl cmd %x not supported\n", cmd);
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

#ifndef ESX
static int unm_nic_suspend(struct pci_dev *pdev, PM_MESSAGE_T state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct unm_adapter_s *adapter = netdev_priv(netdev);

	netif_device_detach(netdev);

	if (netif_running(netdev)) {
		netif_carrier_off(netdev);
		netif_stop_queue(netdev);
		adapter->state = PORT_SUSPEND;
		unm_nic_down(adapter);
	}

/*        pci_save_state(pdev, port->pci_state);        */
/*        pci_disable_device(pdev);        */

	return 0;
}

static int unm_nic_resume(struct pci_dev *pdev)
{
	uint32_t ret;
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	ret = pci_enable_device(pdev);
	adapter->state = PORT_UP;
	netif_device_attach(netdev);
	return ret;
}
#endif /* ESX*/

int unm_nic_fill_statistics_128M(struct unm_adapter_s *adapter,
			    struct unm_statistics *unm_stats)
{
	void *addr;
	if (adapter->ahw.board_type == UNM_NIC_XGBE) {
		UNM_WRITE_LOCK(&adapter->adapter_lock);
		unm_nic_pci_change_crbwindow_128M(adapter, 0);
		UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_TX_BYTE_CNT,
					&(unm_stats->tx_bytes));
		UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_TX_FRAME_CNT,
					&(unm_stats->tx_packets));
		UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_RX_BYTE_CNT,
					&(unm_stats->rx_bytes));
		UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_RX_FRAME_CNT,
					&(unm_stats->rx_packets));
		UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_AGGR_ERROR_CNT,
					&(unm_stats->rx_errors));
		UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_CRC_ERROR_CNT,
					&(unm_stats->rx_CRC_errors));
		UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_OVERSIZE_FRAME_ERR,
					&(unm_stats->rx_long_length_error));
		UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_UNDERSIZE_FRAME_ERR,
					&(unm_stats->rx_short_length_error));

		/* For reading rx_MAC_error bit different procedure */
/*		UNM_NIC_LOCKED_WRITE_REG(UNM_NIU_TEST_MUX_CTL, 0x15);
		UNM_NIC_LOCKED_READ_REG((UNM_CRB_NIU + 0xC0), &temp);
		unm_stats->rx_MAC_errors = temp & 0xff; */

		unm_nic_pci_change_crbwindow_128M(adapter, 1);
		UNM_WRITE_UNLOCK(&adapter->adapter_lock);
	} else {
		spin_lock_bh(&adapter->tx_lock);
		unm_stats->tx_bytes = adapter->stats.txbytes;
		unm_stats->tx_packets = adapter->stats.xmitedframes +
		    adapter->stats.xmitfinished;
		unm_stats->rx_bytes = adapter->stats.rxbytes;
		unm_stats->rx_packets = adapter->stats.no_rcv;
		unm_stats->rx_errors = adapter->stats.rcvdbadskb;
		unm_stats->tx_errors = adapter->stats.nocmddescriptor;
		unm_stats->rx_short_length_error = adapter->stats.uplcong;
		unm_stats->rx_long_length_error = adapter->stats.uphcong;
		unm_stats->rx_CRC_errors = 0;
		unm_stats->rx_MAC_errors = 0;
		spin_unlock_bh(&adapter->tx_lock);
	}
	return 0;
}

int unm_nic_led_config(struct unm_adapter_s *adapter, nx_nic_led_config_t *param)
{
	int rv;
	nic_request_t   req;
	nx_nic_led_config_t *pinfo;

	req.opcode = NX_NIC_HOST_REQUEST;
	pinfo = (nx_nic_led_config_t  *) &req.body;
	memcpy(pinfo, param, sizeof (nx_nic_led_config_t));

	pinfo->hdr.opcode = NX_NIC_H2C_OPCODE_CONFIG_LED;
	pinfo->hdr.comp_id = 1;
	pinfo->hdr.ctxid = adapter->portnum;
	pinfo->hdr.need_completion = 0;

	rv = nx_nic_send_cmd_descs(adapter->netdev, (cmdDescType0_t *)&req, 1);
	if (rv) {
		nx_nic_print4(adapter, "Changing LED configuration failed \n");
	}
	return rv;
}

int unm_nic_fill_statistics_2M(struct unm_adapter_s *adapter,
                        struct unm_statistics *unm_stats)
{
	if(adapter->ahw.board_type == UNM_NIC_XGBE) {
		unm_nic_hw_read_wx_2M(adapter, UNM_NIU_XGE_TX_BYTE_CNT,
			&(unm_stats->tx_bytes), 4);
		unm_nic_hw_read_wx_2M(adapter, UNM_NIU_XGE_TX_FRAME_CNT,
			&(unm_stats->tx_packets), 4);
		unm_nic_hw_read_wx_2M(adapter, UNM_NIU_XGE_RX_BYTE_CNT,
			&(unm_stats->rx_bytes), 4);
		unm_nic_hw_read_wx_2M(adapter, UNM_NIU_XGE_RX_FRAME_CNT,
			&(unm_stats->rx_packets), 4);
		unm_nic_hw_read_wx_2M(adapter, UNM_NIU_XGE_AGGR_ERROR_CNT,
			&(unm_stats->rx_errors), 4);
		unm_nic_hw_read_wx_2M(adapter, UNM_NIU_XGE_CRC_ERROR_CNT,
			&(unm_stats->rx_CRC_errors), 4);
		unm_nic_hw_read_wx_2M(adapter, UNM_NIU_XGE_OVERSIZE_FRAME_ERR,
			&(unm_stats->rx_long_length_error), 4);
		unm_nic_hw_read_wx_2M(adapter, UNM_NIU_XGE_UNDERSIZE_FRAME_ERR,
			&(unm_stats->rx_short_length_error), 4);
	} else {
		spin_lock_bh(&adapter->tx_lock);
		unm_stats->tx_bytes = adapter->stats.txbytes;
		unm_stats->tx_packets = adapter->stats.xmitedframes +
                                       adapter->stats.xmitfinished;
		unm_stats->rx_bytes = adapter->stats.rxbytes;
		unm_stats->rx_packets = adapter->stats.no_rcv;
		unm_stats->rx_errors = adapter->stats.rcvdbadskb;
		unm_stats->tx_errors = adapter->stats.nocmddescriptor;
		unm_stats->rx_short_length_error = adapter->stats.uplcong;
		unm_stats->rx_long_length_error = adapter->stats.uphcong;
		unm_stats->rx_CRC_errors = 0;
		unm_stats->rx_MAC_errors = 0;
		spin_unlock_bh(&adapter->tx_lock);
	}
	return 0;
}

int unm_nic_clear_statistics_128M(struct unm_adapter_s *adapter)
{
	void *addr;
	int data = 0;

	UNM_WRITE_LOCK(&adapter->adapter_lock);
	unm_nic_pci_change_crbwindow_128M(adapter, 0);

	UNM_NIC_LOCKED_WRITE_REG(UNM_NIU_XGE_TX_BYTE_CNT, &data);
	UNM_NIC_LOCKED_WRITE_REG(UNM_NIU_XGE_TX_FRAME_CNT, &data);
	UNM_NIC_LOCKED_WRITE_REG(UNM_NIU_XGE_RX_BYTE_CNT, &data);
	UNM_NIC_LOCKED_WRITE_REG(UNM_NIU_XGE_RX_FRAME_CNT, &data);
	UNM_NIC_LOCKED_WRITE_REG(UNM_NIU_XGE_AGGR_ERROR_CNT, &data);
	UNM_NIC_LOCKED_WRITE_REG(UNM_NIU_XGE_CRC_ERROR_CNT, &data);
	UNM_NIC_LOCKED_WRITE_REG(UNM_NIU_XGE_OVERSIZE_FRAME_ERR, &data);
	UNM_NIC_LOCKED_WRITE_REG(UNM_NIU_XGE_UNDERSIZE_FRAME_ERR, &data);

	unm_nic_pci_change_crbwindow_128M(adapter, 1);
	UNM_WRITE_UNLOCK(&adapter->adapter_lock);
	unm_nic_clear_stats(adapter);
	return 0;
}

int unm_nic_clear_statistics_2M(struct unm_adapter_s *adapter)
{
	int data = 0;

	unm_nic_hw_write_wx_2M(adapter, UNM_NIU_XGE_TX_BYTE_CNT, &data, 4);
	unm_nic_hw_write_wx_2M(adapter, UNM_NIU_XGE_TX_FRAME_CNT, &data, 4);
	unm_nic_hw_write_wx_2M(adapter, UNM_NIU_XGE_RX_BYTE_CNT, &data, 4);
	unm_nic_hw_write_wx_2M(adapter, UNM_NIU_XGE_RX_FRAME_CNT, &data, 4);
	unm_nic_hw_write_wx_2M(adapter, UNM_NIU_XGE_AGGR_ERROR_CNT, &data, 4);
	unm_nic_hw_write_wx_2M(adapter, UNM_NIU_XGE_CRC_ERROR_CNT, &data, 4);
	unm_nic_hw_write_wx_2M(adapter, UNM_NIU_XGE_OVERSIZE_FRAME_ERR, &data, 4);
	unm_nic_hw_write_wx_2M(adapter, UNM_NIU_XGE_UNDERSIZE_FRAME_ERR, &data, 4);
	unm_nic_clear_stats(adapter);
	return 0;
}
static int unm_nic_do_ioctl(struct unm_adapter_s *adapter, void *u_data)
{
	unm_nic_ioctl_data_t data;
	unm_nic_ioctl_data_t *up_data = (unm_nic_ioctl_data_t *)u_data;
	int retval = 0;
	struct unm_statistics unm_stats;
	long phy_id = 0 ;
	uint64_t efuse_chip_id = 0;
	unsigned int ssys_id = 0;
	nx_finger_print_t nx_lic_finger_print;
	nx_license_capabilities_t nx_lic_capabilities;
	nx_finger_print_ioctl_t *snt_ptr;
	nx_license_capabilities_ioctl_t *snt_ptr1;
	nx_install_license_ioctl_t *snt_ptr2;

	memset(&unm_stats, 0, sizeof(unm_stats));
	nx_nic_print7(adapter, "doing ioctl for %p\n", adapter);
	if (copy_from_user(&data, up_data, sizeof(data))) {
		/* evil user tried to crash the kernel */
		nx_nic_print6(adapter, "bad copy from userland: %d\n",
				(int)sizeof(data));
		retval = -EFAULT;
		goto error_out;
	}

	/* Shouldn't access beyond legal limits of  "char u[64];" member */
	if (!data.ptr && (data.size > sizeof(data.u))) {
		/* evil user tried to crash the kernel */
		nx_nic_print6(adapter, "bad size: %d\n", data.size);
		retval = -EFAULT;
		goto error_out;
	}

	switch (data.cmd) {
		case unm_nic_cmd_pci_read:
			if ((retval = adapter->unm_nic_hw_read_ioctl(adapter, data.off,
							&(data.u), data.size)))
				goto error_out;
			if (copy_to_user((void *)&(up_data->u), &(data.u), data.size)) {
				nx_nic_print6(adapter, "bad copy to userland: %d\n",
						(int)sizeof(data));
				retval = -EFAULT;
				goto error_out;
			}
			data.rv = 0;
			break;

		case unm_nic_cmd_pci_write:
			data.rv = adapter->unm_nic_hw_write_ioctl(adapter, data.off, &(data.u),
					data.size);
			break;

		case unm_nic_cmd_pci_mem_read:
			nx_nic_print7(adapter, "doing unm_nic_cmd_pci_mm_rd\n");
			if ((adapter->unm_nic_pci_mem_read(adapter, data.off, &(data.u),
							data.size) != 0) ||
					(copy_to_user((void *)&(up_data->u), &(data.u),
						      data.size) != 0)) {
				nx_nic_print6(adapter, "bad copy to userland: %d\n",
						(int)sizeof(data));
				retval = -EFAULT;
				goto error_out;
			}
			data.rv = 0;
			nx_nic_print7(adapter, "read %lx\n", (unsigned long)data.u);
			break;

		case unm_nic_cmd_pci_mem_write:
			if ((data.rv =
						adapter->unm_nic_pci_mem_write(adapter, data.off, &(data.u),
							data.size)) != 0) {
				retval = -EFAULT;
				goto error_out;
			}
			break;

		case unm_nic_cmd_pci_config_read:
			switch (data.size) {
				case 1:
					data.rv = pci_read_config_byte(adapter->ahw.pdev,
							data.off,
							(char *)&(data.u));
					break;
				case 2:
					data.rv = pci_read_config_word(adapter->ahw.pdev,
							data.off,
							(short *)&(data.u));
					break;
				case 4:
					data.rv = pci_read_config_dword(adapter->ahw.pdev,
							data.off,
							(u32 *) & (data.u));
					break;
			}
			if (copy_to_user((void *)&up_data->u, &data.u, data.size)) {
				nx_nic_print6(adapter, "bad copy to userland: %d\n",
						(int)sizeof(data));
				retval = -EFAULT;
				goto error_out;
			}
			break;

		case unm_nic_cmd_pci_config_write:
			switch (data.size) {
				case 1:
					data.rv = pci_write_config_byte(adapter->ahw.pdev,
							data.off,
							*(char *)&(data.u));
					break;
				case 2:
					data.rv = pci_write_config_word(adapter->ahw.pdev,
							data.off,
							*(short *)&(data.u));
					break;
				case 4:
					data.rv = pci_write_config_dword(adapter->ahw.pdev,
							data.off,
							*(u32 *) & (data.u));
					break;
			}
			break;

		case unm_nic_cmd_get_version:
			if (copy_to_user((void *)&(up_data->u), UNM_NIC_LINUX_VERSIONID,
						sizeof(UNM_NIC_LINUX_VERSIONID))) {
				nx_nic_print6(adapter, "bad copy to userland: %d\n",
						(int)sizeof(data));
				retval = -EFAULT;
				goto error_out;
			}
			break;

		case unm_nic_cmd_get_stats:
			data.rv = adapter->unm_nic_fill_statistics(adapter, &unm_stats);
			if (copy_to_user((void *)(up_data->ptr), (void *)&unm_stats,
						sizeof(struct unm_statistics))) {
				nx_nic_print6(adapter, "bad copy to userland: %d\n",
						(int)sizeof(data));
				retval = -EFAULT;
				goto error_out;
			}
			break;

		case unm_nic_cmd_clear_stats:
			data.rv = adapter->unm_nic_clear_statistics(adapter);
			break;
		case unm_nic_cmd_get_phy_type:
			if (adapter->ahw.board_type == UNM_NIC_XGBE) {
				phy_id = unm_xge_mdio_rd_port(adapter, 0, DEV_PMA_PMD,
						PMD_IDENTIFIER);
				if (adapter->portnum == 1 &&  phy_id == PMD_ID_MYSTICOM) {
					phy_id = unm_xge_mdio_rd_port(adapter, 3,
							DEV_PMA_PMD, PMD_IDENTIFIER);
				}
			}

			if (adapter->ahw.board_type == UNM_NIC_GBE) {
				unm_niu_gbe_phy_read(adapter, PMD_IDENTIFIER, (__uint32_t *)&phy_id);
			}
			if(copy_to_user((void *)&up_data->u, &phy_id, sizeof(phy_id))) {
				nx_nic_print4(adapter, "bad copy to userland: %d\n",
						(int)sizeof(data));
				retval = -EFAULT;
				goto error_out;
			}
			data.rv = 0;
			break;
		case unm_nic_cmd_efuse_chip_id:

			efuse_chip_id =
				adapter->unm_nic_pci_read_normalize(adapter,
						UNM_EFUSE_CHIP_ID_HIGH);
			efuse_chip_id <<= 32;
			efuse_chip_id |=
				adapter->unm_nic_pci_read_normalize(adapter,
						UNM_EFUSE_CHIP_ID_LOW);
			if(copy_to_user((void *) &up_data->u, &efuse_chip_id,
						sizeof(uint64_t))) {
				nx_nic_print4(adapter, "bad copy to userland: %d\n",
						(int)sizeof(data));
				retval = -EFAULT;
				goto error_out;
			}
			data.rv = 0;
			break;
		case unm_nic_cmd_get_lic_fingerprint:
			data.rv = nx_get_lic_finger_print(adapter, &nx_lic_finger_print);
			snt_ptr = (nx_finger_print_ioctl_t *)(data.ptr);


			if(copy_to_user(&snt_ptr->req_len, &(nx_lic_finger_print.len),
						sizeof(nx_lic_finger_print.len))) {
				nx_nic_print4(adapter, "bad copy to userland:\n ");
				retval = -EFAULT;
				goto error_out;
			}

			if(copy_to_user((void *) snt_ptr->req_finger_print, &(nx_lic_finger_print.data),
						sizeof(nx_lic_finger_print.data))) {
				nx_nic_print4(adapter, "bad copy to userland:\n ");
				retval = -EFAULT;
				goto error_out;
			}

			if(copy_to_user((void *) &up_data->u, &(nx_lic_finger_print.len),
						sizeof(nx_lic_finger_print.len))) {
				nx_nic_print4(adapter, "bad copy to userland:\n");
				retval = -EFAULT;
				goto error_out;
			}

			break;
		case unm_nic_cmd_lic_install:
			memset(adapter->nx_lic_dma.addr, 0, sizeof(nx_install_license_t));
			snt_ptr2 = (nx_install_license_ioctl_t *)(data.ptr);

			if (copy_from_user((adapter->nx_lic_dma.addr), (void*)snt_ptr2->data, snt_ptr2->data_len)) {
				nx_nic_print4(adapter, "bad copy from userland:\n ");
				retval = -EFAULT;
				goto error_out;
			}
			data.rv = nx_install_license(adapter);
			break;
		case unm_nic_cmd_get_lic_features:
			data.rv = nx_get_capabilty_request(adapter, &nx_lic_capabilities);
			snt_ptr1 = (nx_license_capabilities_ioctl_t *)data.ptr;

			if(copy_to_user((void *) &(snt_ptr1->req_len), &(nx_lic_capabilities.len),
						sizeof(nx_lic_capabilities.len))) {
				nx_nic_print4(adapter, "bad copy to userland:\n ");
				retval = -EFAULT;
				goto error_out;
			}

			if(copy_to_user((void *) snt_ptr1->req_license_capabilities, &(nx_lic_capabilities.arr),
						sizeof(nx_lic_capabilities.arr))) {
				nx_nic_print4(adapter, "bad copy to userland:\n ");
				retval = -EFAULT;
				goto error_out;
			}
			if(copy_to_user((void *) &up_data->u, &(nx_lic_capabilities.len),
						sizeof(nx_lic_capabilities.len))) {
				nx_nic_print4(adapter, "bad copy to userland:\n");
				retval = -EFAULT;
				goto error_out;
			}
			break;
		case unm_nic_cmd_get_ssys_id:
			adapter->unm_nic_hw_read_wx(adapter, UNM_PCIE_REG(((adapter->pdev)->devfn) * 0x1000 + 0x2c), &ssys_id,
					sizeof(unsigned long));
			if(copy_to_user((void *) &up_data->u, &ssys_id,
						sizeof(ssys_id))) {
				nx_nic_print4(adapter, "bad copy to userland: %d\n",
						(int)sizeof(data));
				retval = -EFAULT;
				goto error_out;
			}
			data.rv = 0;
			break;
#ifdef UNM_NIC_SNMP_TRAP
		case unm_nic_cmd_set_pid_trap:
			/* It will save the pid, netlink temperature message
			 * will be sent to this process
			 */
			if (data.off < 1)
				goto error_out;
			set_temperature_user_pid(data.off);
			/*if success , w'll return same pid. */
			data.rv = data.off;
			break;
#endif

#if 0				// wait for the unmflash changes
		case unm_nic_cmd_flash_read:
			/* do_rom_fast_read() called instead of rom_fast_read() because
			   rom_lock/rom_unlock is done by separate ioctl */
			if ((retval = do_rom_fast_read(adapter, data.off,
							(int *)&(data.u))))
				goto error_out;
			if (copy_to_user((void *)&(up_data->u), &(data.u),
						UNM_FLASH_READ_SIZE)) {
				nx_nic_print6(adapter, "bad copy to userland: %d\n",
						(int)sizeof(data));
				retval = -EFAULT;
				goto error_out;
			}
			data.rv = 0;
			break;

		case unm_nic_cmd_flash_write:
			/*
			 * do_rom_fast_write() called instead of rom_fast_write()
			 * because rom_lock/rom_unlock is done by separate ioctl
			 */
			data.rv = do_rom_fast_write(adapter, data.off,
					*(u32 *)&data.u);
			break;

		case unm_nic_cmd_flash_se:
			data.rv = rom_se(adapter, data.off);
			break;
#endif

		default:
			nx_nic_print4(adapter, "bad command %d\n", data.cmd);
			retval = -EOPNOTSUPP;
			goto error_out;
	}
	put_user(data.rv, &(up_data->rv));
	nx_nic_print7(adapter, "done ioctl.\n");

error_out:
	return retval;

}

int unm_read_blink_state(char *buf, char **start, off_t offset, int count,
		                       int *eof, void *data) {
	struct net_device *netdev = (struct net_device *)data;
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;
	int len = 0;
	len = sprintf(buf,"%d\n",adapter->led_blink_state);
	*eof = 1;
	return len;
}
int unm_write_blink_state(struct file *file, const char *buffer,
		unsigned long count, void *data) {
	nx_nic_led_config_t param;
	struct net_device *netdev = (struct net_device *)data;
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;
	unsigned int testval;
	int ret = 0;
	if (!capable(CAP_NET_ADMIN)) {
		return -EACCES;
	}
	if (adapter->state != PORT_UP) {
		return -EINVAL;
	}
	if (copy_from_user(&testval, buffer, 1)){

		return -EFAULT;
	}
	testval -= 48;
	if (testval <= 1) {
		param.ctx_id = adapter->portnum;
		param.blink_state = testval;
		param.blink_rate = 0xf;
		adapter->led_blink_state = param.blink_state;
		ret = unm_nic_led_config(adapter, &param);
	}
	if (ret) {
		return ret;
	} else {
		return 1;
	}
}
int unm_read_blink_rate(char *buf, char **start, off_t offset, int count,
				int *eof, void *data) {

	int len = 0;
	struct net_device *netdev = (struct net_device *)data;
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;
	len = sprintf(buf,"%d\n", adapter->led_blink_rate);
	*eof = 1;
	return len ;
}
#define MAX_SUPPORTED_BLINK_RATES               3
int unm_write_blink_rate(struct file *file, const char *buffer,
		unsigned long count, void *data) {
	nx_nic_led_config_t param;
	struct net_device *netdev = (struct net_device *)data;
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;
	unsigned int testval;
	int ret = 0;
	if (!capable(CAP_NET_ADMIN)) {
		return -EACCES;
	}
	if (adapter->state != PORT_UP) {
		return -EINVAL;
	}
	if(copy_from_user(&testval, buffer, 1)) {
		return -EFAULT;
	}
	testval -= 48;
	if(testval <= MAX_SUPPORTED_BLINK_RATES) {
		param.ctx_id = adapter->portnum;
		param.blink_rate = testval;
		param.blink_state = 0xf;
		adapter->led_blink_rate = param.blink_rate;
		ret = unm_nic_led_config(adapter, &param);
	}
	if (ret) {
		return ret;
	} else {
		return 1;
	}
}

static struct pci_driver unm_driver = {
	.name = unm_nic_driver_name,
	.id_table = unm_pci_tbl,
	.probe = unm_nic_probe,
	.remove = __devexit_p(unm_nic_remove),
#ifndef ESX
	.suspend = unm_nic_suspend,
	.resume = unm_nic_resume
#endif
};


/* Driver Registration on UNM card    */
static int __init unm_init_module(void)
{
	int err;

	if (!VMK_SET_MODULE_VERSION(unm_nic_driver_string)) {
		return -ENODEV;
	}
	err = unm_init_proc_drv_dir();
	if (err) {
		return err;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	if ((unm_workq = create_singlethread_workqueue("unm")) == NULL) {
		unm_cleanup_proc_drv_entries();
		return (-ENOMEM);
	}
#endif

	nx_verify_module_params();

	err = PCI_MODULE_INIT(&unm_driver);

	return err;
}

static void __exit unm_exit_module(void)
{
        /*
         * Wait for some time to allow the dma to drain, if any.
         */
        mdelay(5);

        pci_unregister_driver(&unm_driver);
	unm_cleanup_proc_drv_entries();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	destroy_workqueue(unm_workq);
#endif
}

void nx_nic_get_lsa_version_number(struct net_device *netdev,
				    nic_version_t * version)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;

        adapter->unm_nic_hw_read_wx(adapter, UNM_TCP_FW_VERSION_MAJOR_ADDR,
                        &version->major, 4);

        adapter->unm_nic_hw_read_wx(adapter, UNM_TCP_FW_VERSION_MINOR_ADDR,
                        &version->minor, 4);

        adapter->unm_nic_hw_read_wx(adapter, UNM_TCP_FW_VERSION_SUB_ADDR,
                        &version->sub, 4);

}

void nx_nic_get_nic_version_number(struct net_device *netdev,
				   nic_version_t * version)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;

        adapter->unm_nic_hw_read_wx(adapter, UNM_FW_VERSION_MAJOR,
                        &version->major, 4);

        adapter->unm_nic_hw_read_wx(adapter, UNM_FW_VERSION_MINOR,
                        &version->minor, 4);

        adapter->unm_nic_hw_read_wx(adapter, UNM_FW_VERSION_SUB,
                        &version->sub, 4);

}
#ifdef UNM_NIC_HW_VLAN
static void unm_nic_vlan_rx_register(struct net_device *net_dev,
				     struct vlan_group *grp)
{
	struct unm_adapter_s   *adapter = (struct unm_adapter_s*)net_dev->priv;
	/* TODO: Which value to be supplied as
	 * second arg as previously this function had only adapter as arg
	 * for sake of valid call I am using adapter->nx_dev->rx_ctxs[0]
	 */
	unm_nic_disable_all_int(adapter, adapter->nx_dev->rx_ctxs[0]);
	if (grp) {
		adapter->vgrp = grp;
	}
	unm_nic_enable_all_int(adapter, adapter->nx_dev->rx_ctxs[0]);
}
/* Generally vlan devices do not inherit netdev->features of real device
 * hence it also do not inherits checksum offload feature of nx_nic as
 * well, so we need to set it before we start activities on vlan device
 * or else checksum offload will not be working with vlan interface
 * this function does that.
 */
static void unm_nic_vlan_rx_add_vid(struct net_device *net_dev, unsigned short vid)
{
	struct unm_adapter_s   *adapter = (struct unm_adapter_s*)net_dev->priv;
	struct net_device *vdev = NULL;
	/* TODO: I don't know which value to be supplied as
	 * second arg as previously this function had only adapter as arg
	 * for sake of valid call I am using adapter->nx_dev->rx_ctxs[0]
	 * same is case with enable_int() call
	 */
	unm_nic_disable_all_int(adapter, adapter->nx_dev->rx_ctxs[0]);
	if (adapter->vgrp) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
		vdev = adapter->vgrp->vlan_devices[vid];
#else
		vdev = vlan_group_get_device(adapter->vgrp, vid);
#endif
		if (vdev) {
			vdev->features |= net_dev->features;
		}
	}
	unm_nic_enable_all_int(adapter, adapter->nx_dev->rx_ctxs[0]);
	return;
}
static void unm_nic_vlan_rx_kill_vid(struct net_device *net_dev,
				     unsigned short vid)
{
	struct unm_adapter_s   *adapter = (struct unm_adapter_s*)net_dev->priv;
	/* TODO: Don't know which value to be supplied as
	 * second arg as previously this function had only adapter as arg
	 * for sake of valid call I am using adapter->nx_dev->rx_ctxs[0]
	 */
	unm_nic_disable_all_int(adapter, adapter->nx_dev->rx_ctxs[0]);
	if (adapter->vgrp) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
		adapter->vgrp->vlan_devices[vid] = NULL;
#else
		vlan_group_set_device(adapter->vgrp, vid, NULL);
#endif
	}
	unm_nic_enable_all_int(adapter, adapter->nx_dev->rx_ctxs[0]);
	return;
}

#endif

static int nx_nic_get_stats(struct net_device *dev,
			    netxen_pstats_t *card_stats)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)dev->priv;
	nx_rcode_t rv = NX_RCODE_SUCCESS;

	if (!unm_link_ok(adapter) ||
	    !netif_carrier_ok(dev) ||
	    !(dev->flags & IFF_UP)) {
		return 0;
	}

	rv = nx_os_get_net_stats(adapter->nx_dev, adapter->nx_dev->rx_ctxs[0],
				 adapter->portnum,
				 (U64)adapter->nic_net_stats.phys,
				 (U32)sizeof (netxen_pstats_t));
	
	if (rv == NX_RCODE_SUCCESS) {
		nx_os_copy_memory(card_stats, adapter->nic_net_stats.data,
				  sizeof (netxen_pstats_t));
	} 

	return rv;
}

/*
 * Return the NIC API structure.
 *
 */
nx_nic_api_t *nx_nic_get_api(void)
{
        return &nx_nic_api_struct; 
}


EXPORT_SYMBOL(nx_nic_get_api);

module_init(unm_init_module);
module_exit(unm_exit_module);
