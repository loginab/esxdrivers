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
/* ethtool support for unm nic */

#include <linux/types.h>
#include <asm/uaccess.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/version.h>
#include <linux/delay.h>

#include "queue.h"
#include "unm_nic_hw.h"
#include "unm_nic.h"
#include "nic_phan_reg.h"
#include "unm_nic_ioctl.h"
#include "nic_cmn.h"
#include "unm_version.h"
#include "nxhal_nic_interface.h"
#include "nxhal_nic_api.h"
#include "nxhal.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,21)
extern char *unm_nic_driver_string;
extern void unm_change_ringparam(struct unm_adapter_s *adapter);
extern int rom_fast_read(struct unm_adapter_s *adapter, int addr, int *valp);
extern int unm_loopback_test(struct net_device *netdev, int fint, void *ptr,
			     unm_test_ctr_t * testCtx);
extern unsigned long crb_pci_to_internal(unsigned long addr);

extern int tx_test(struct net_device *netdev, unm_send_test_t * arg);
extern int unm_irq_test(unm_adapter * adapter);
extern int unm_link_test(unm_adapter * adapter);
extern int unm_led_test(unm_adapter * adapter);

#define ADAPTER_UP_MAGIC     777
//Maximum EEPROM exposed to User is 24K
#define MAX_ROM_SIZE    ((0x01 << 13)*3)

#define UNM_ROUNDUP(i, size)    ((i) = (((i) + (size) - 1) & ~((size) - 1)))

struct unm_nic_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define UNM_NIC_STAT(m) sizeof(((struct unm_adapter_s *)0)->m), \
                    offsetof(struct unm_adapter_s, m)

static const struct unm_nic_stats unm_nic_gstrings_stats[] = {
	{"rcvd bad skb", UNM_NIC_STAT(stats.rcvdbadskb)},
	{"xmit called", UNM_NIC_STAT(stats.xmitcalled)},
	{"xmited frames", UNM_NIC_STAT(stats.xmitedframes)},
	{"xmit finished", UNM_NIC_STAT(stats.xmitfinished)},
	{"bad skb len", UNM_NIC_STAT(stats.badskblen)},
	{"no cmd desc", UNM_NIC_STAT(stats.nocmddescriptor)},
	{"polled", UNM_NIC_STAT(stats.polled)},
	{"uphappy", UNM_NIC_STAT(stats.uphappy)},
	{"updropped", UNM_NIC_STAT(stats.updropped)},
	{"uplcong", UNM_NIC_STAT(stats.uplcong)},
	{"uphcong", UNM_NIC_STAT(stats.uphcong)},
	{"upmcong", UNM_NIC_STAT(stats.upmcong)},
	{"updunno", UNM_NIC_STAT(stats.updunno)},
	{"skb freed", UNM_NIC_STAT(stats.skbfreed)},
	{"tx dropped", UNM_NIC_STAT(stats.txdropped)},
	{"tx null skb", UNM_NIC_STAT(stats.txnullskb)},
	{"csummed", UNM_NIC_STAT(stats.csummed)},
	{"no rcv", UNM_NIC_STAT(stats.no_rcv)},
	{"rx bytes", UNM_NIC_STAT(stats.rxbytes)},
	{"tx bytes", UNM_NIC_STAT(stats.txbytes)},
};

#define UNM_NIC_STATS_LEN        \
        sizeof(unm_nic_gstrings_stats) / sizeof(struct unm_nic_stats)

static const char unm_nic_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register test  (on/offline)",
	"Link test   (on/offline)",
#if 0
	"Eeprom test    (offline)",
#endif
	"Interrupt test (offline)",
	"Loopback test  (offline)",
	"Led Test       (offline)"
};

#define UNM_NIC_TEST_LEN sizeof(unm_nic_gstrings_test) / ETH_GSTRING_LEN

#define UNM_NIC_REGS_COUNT 42
#define UNM_NIC_REGS_LEN (UNM_NIC_REGS_COUNT * sizeof(unm_crbword_t))

#define TRUE    1
#define FALSE   0

static long sw_lock_timeout= 100000000;

int sw_lock(struct unm_adapter_s *adapter)
{
    int i;
    int done = 0, timeout = 0;

    while (!done) {
        /* acquire semaphore3 from PCI HW block */
        adapter->unm_nic_hw_read_wx(adapter, UNM_PCIE_REG(PCIE_SEM6_LOCK), &done, 4);
        if (done == 1)
            break;
        if (timeout >= sw_lock_timeout) {
            return -1;
        }
        timeout++;
        /*
         * Yield CPU
         */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
        if(!in_atomic())
                schedule();
        else {
#endif
                for(i = 0; i < 20; i++)
                        cpu_relax();    /*This a nop instr on i386*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
        }
#endif
    }
    return 0;
}

void sw_unlock(struct unm_adapter_s *adapter)
{
    int val;
    /* release semaphore3 */
    adapter->unm_nic_hw_read_wx(adapter, UNM_PCIE_REG(PCIE_SEM6_UNLOCK), &val, 4);
}
static int unm_nic_get_eeprom_len(struct net_device *netdev)
{

	return MAX_ROM_SIZE;
#if 0
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	int n;
	if ((rom_fast_read(adapter, 0, &n) == 0) && (n & 0x80000000)) {	// verify the rom address
		n &= ~0x80000000;
		if (n < 1024)
			return (n);
	}
	return 0;
#endif
}
static uint32_t
unm_nic_get_tx_csum(struct net_device *netdev)
{
	return (netdev->features & NETIF_F_HW_CSUM) != 0;
}
static int
unm_nic_set_tx_csum(struct net_device *netdev, uint32_t data)
{
	if (data)
		netdev->features |= NETIF_F_HW_CSUM;
	else
		netdev->features &= ~NETIF_F_HW_CSUM;
	return 0;
}

static void
unm_nic_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	uint32_t fw_major = 0;
	uint32_t fw_minor = 0;
	uint32_t fw_build = 0;

	strncpy(drvinfo->driver, DRIVER_NAME, 32);
	strncpy(drvinfo->version, UNM_NIC_LINUX_VERSIONID, 32);

	read_lock(&adapter->adapter_lock);
    fw_major = adapter->unm_nic_pci_read_normalize(adapter, UNM_FW_VERSION_MAJOR);
    fw_minor = adapter->unm_nic_pci_read_normalize(adapter, UNM_FW_VERSION_MINOR);
    fw_build = adapter->unm_nic_pci_read_normalize(adapter, UNM_FW_VERSION_SUB);
	read_unlock(&adapter->adapter_lock);
	sprintf(drvinfo->fw_version, "%d.%d.%d", fw_major, fw_minor, fw_build);

	strncpy(drvinfo->bus_info, pci_name(adapter->pdev), 32);
	drvinfo->n_stats = UNM_NIC_STATS_LEN;
	drvinfo->testinfo_len = UNM_NIC_TEST_LEN;
	drvinfo->regdump_len = UNM_NIC_REGS_LEN;
	drvinfo->eedump_len = unm_nic_get_eeprom_len(netdev);
}

static int
unm_nic_get_settings(struct net_device *netdev, struct ethtool_cmd *ecmd)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	unm_board_info_t *boardinfo;
	u32 port_mode = 0;

	boardinfo = &adapter->ahw.boardcfg;

	// read which mode
	if (adapter->ahw.board_type == UNM_NIC_GBE) {
		ecmd->supported = (SUPPORTED_10baseT_Half |
				   SUPPORTED_10baseT_Full |
				   SUPPORTED_100baseT_Half |
				   SUPPORTED_100baseT_Full |
				   SUPPORTED_1000baseT_Half |
				   SUPPORTED_1000baseT_Full);

		ecmd->advertising = (ADVERTISED_100baseT_Half |
				     ADVERTISED_100baseT_Full |
				     ADVERTISED_1000baseT_Half |
				     ADVERTISED_1000baseT_Full);

//        if(netif_carrier_ok(netdev)) { // cant use this method to determine
//        link status as driver has not taken care of carrier state (ON/OFF).
//        Instead I'll use cached value in unm_port to find link status !!
		if (adapter->state) {
			ecmd->speed = adapter->link_speed;
			ecmd->duplex = adapter->link_duplex;
		} else
			return -EIO;	// link absent

	} else if (adapter->ahw.board_type == UNM_NIC_XGBE) {
		adapter->unm_nic_hw_read_wx(adapter, UNM_PORT_MODE_ADDR,
						 &port_mode, 4);
		if (port_mode == UNM_PORT_MODE_802_3_AP) {
			ecmd->supported = SUPPORTED_1000baseT_Full;
			ecmd->advertising = ADVERTISED_1000baseT_Full;
			ecmd->speed = SPEED_1000;
		} else {
			ecmd->supported = SUPPORTED_10000baseT_Full;
			ecmd->advertising = ADVERTISED_10000baseT_Full;
			ecmd->speed = SPEED_10000;
		}
		ecmd->duplex = DUPLEX_FULL;

		if((adapter->ahw.revision_id >= NX_P3_B0)) {
			u32 val;

			/* Use the per-function link speed value */
			adapter->unm_nic_hw_read_wx(adapter,
			     PF_LINK_SPEED_REG(adapter->ahw.pci_func), &val, 4);

			/* we have per-function link speed */
			ecmd->speed = PF_LINK_SPEED_VAL(adapter->ahw.pci_func, val)
					* PF_LINK_SPEED_MHZ;
		}
	} else {
		printk(KERN_ERR "%s: ERROR: Unsupported board model %d\n",
		       unm_nic_driver_name,
		       (unm_brdtype_t) boardinfo->board_type);
		return -EIO;
	}

	ecmd->phy_address = adapter->portnum;
	ecmd->transceiver = XCVR_EXTERNAL;

	switch ((unm_brdtype_t) boardinfo->board_type) {
	case UNM_BRDTYPE_P2_SB35_4G:
	case UNM_BRDTYPE_P2_SB31_2G:
	case UNM_BRDTYPE_P3_REF_QG:
	case UNM_BRDTYPE_P3_4_GB:
	case UNM_BRDTYPE_P3_4_GB_MM:
	case UNM_BRDTYPE_P3_10000_BASE_T:
		ecmd->supported |= SUPPORTED_Autoneg;
		ecmd->advertising |= ADVERTISED_Autoneg;
	case UNM_BRDTYPE_P3_10G_CX4:
	case UNM_BRDTYPE_P3_10G_CX4_LP:
	case UNM_BRDTYPE_P2_SB31_10G_CX4:
		ecmd->supported |= SUPPORTED_TP;
		ecmd->advertising |= ADVERTISED_TP;
		ecmd->port = PORT_TP;
		ecmd->autoneg = (boardinfo->board_type ==
				 UNM_BRDTYPE_P2_SB31_10G_CX4) ?
		    (AUTONEG_DISABLE) : (adapter->link_autoneg);
		break;
	case UNM_BRDTYPE_P3_XG_LOM:
	case UNM_BRDTYPE_P3_HMEZ:
		ecmd->supported = (SUPPORTED_1000baseT_Full  |
				SUPPORTED_10000baseT_Full |
				SUPPORTED_Autoneg);
		ecmd->advertising = (ADVERTISED_1000baseT_Full  |
				ADVERTISED_10000baseT_Full |
				ADVERTISED_Autoneg);
		ecmd->port = PORT_MII;
		ecmd->autoneg = AUTONEG_ENABLE;
		break;
	case UNM_BRDTYPE_P2_SB31_10G_HMEZ:
	case UNM_BRDTYPE_P2_SB31_10G_IMEZ:
	case UNM_BRDTYPE_P3_IMEZ:
		ecmd->supported |= SUPPORTED_MII;
		ecmd->advertising |= ADVERTISED_MII;
		ecmd->port = PORT_MII;
		ecmd->autoneg = AUTONEG_DISABLE;
		break;

	case UNM_BRDTYPE_P2_SB31_10G:
	case UNM_BRDTYPE_P3_10G_SFP_PLUS:
	case UNM_BRDTYPE_P3_10G_XFP:
		ecmd->supported |= SUPPORTED_FIBRE;
		ecmd->advertising |= ADVERTISED_FIBRE;
		ecmd->port = PORT_FIBRE;
		ecmd->autoneg = AUTONEG_DISABLE;
		break;
	case UNM_BRDTYPE_P3_10G_TROOPER:
		if (adapter->ahw.board_type == UNM_NIC_XGBE) {
			ecmd->autoneg = AUTONEG_DISABLE;
			ecmd->supported |= SUPPORTED_FIBRE;
			ecmd->advertising |= ADVERTISED_FIBRE;
			ecmd->port = PORT_FIBRE;
		}else {
			ecmd->autoneg = AUTONEG_ENABLE;
			ecmd->supported |= SUPPORTED_TP;
			ecmd->advertising |= ADVERTISED_TP;
			ecmd->port = PORT_TP;
		}
		break;

	default:
		printk(KERN_ERR "%s: ERROR: Unsupported board model %d\n",
		       unm_nic_driver_name,
		       (unm_brdtype_t) boardinfo->board_type);
		return -EIO;
		break;
	}

	return 0;
}

static int
unm_nic_set_settings(struct net_device *netdev, struct ethtool_cmd *ecmd)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	unm_niu_phy_status_t status;

	// read which mode
	if (adapter->ahw.board_type == UNM_NIC_GBE) {
		// autonegotiation
		if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
			if (nx_fw_cmd_set_phy(adapter, adapter->portnum,
			    UNM_NIU_GB_MII_MGMT_ADDR_AUTONEG,
			    (unm_crbword_t) ecmd->autoneg) != 0)
				return -EIO;
		} else {
			if (unm_nic_phy_write(adapter,
			    UNM_NIU_GB_MII_MGMT_ADDR_AUTONEG,
			    (unm_crbword_t) ecmd->autoneg) != 0)
				return -EIO;
		}
		
		adapter->link_autoneg = ecmd->autoneg;

		if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
			if (nx_fw_cmd_query_phy(adapter, adapter->portnum,
			    UNM_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
			    (unm_crbword_t *)&status) != 0)
				return -EIO;

		} else {
			if (unm_nic_phy_read
			    (adapter, UNM_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
			     (unm_crbword_t *) &status) != 0)
				return -EIO;
		}
		// speed
		switch (ecmd->speed) {
		case SPEED_10:
			status.speed = 0;
			break;
		case SPEED_100:
			status.speed = 1;
			break;
		case SPEED_1000:
			status.speed = 2;
			break;
		}
		// set duplex mode
		if (ecmd->duplex == DUPLEX_HALF)
			status.duplex = 0;
		if (ecmd->duplex == DUPLEX_FULL)
			status.duplex = 1;

		if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
			if (nx_fw_cmd_set_phy(adapter, adapter->portnum,
			    UNM_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
			    *((int *)&status)) != 0)
				return -EIO;
		} else {
			if (unm_nic_phy_write(adapter,
			    UNM_NIU_GB_MII_MGMT_ADDR_PHY_STATUS,
			    *((int *)&status)) != 0)
				return -EIO;
		}

		adapter->link_speed = ecmd->speed;
		adapter->link_duplex = ecmd->duplex;
	} else if (adapter->ahw.board_type == UNM_NIC_XGBE) {
		// no changes supported for XGBE
		return -EIO;
	}
	if (netif_running(netdev)) {
		netdev->stop(netdev);
		netdev->open(netdev);
	}
	return 0;
}

static int unm_nic_get_regs_len(struct net_device *netdev)
{
	return UNM_NIC_REGS_LEN;
}

static void
unm_nic_get_regs(struct net_device *netdev, struct ethtool_regs *regs, void *p)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	unm_crbword_t mode, *regs_buff = p;
	void *addr;

	memset(p, 0, UNM_NIC_REGS_LEN);
	regs->version = (1 << 24) | (adapter->ahw.revision_id << 16) |
	    adapter->ahw.device_id;


	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {

		read_lock(&adapter->adapter_lock);
		// which mode
		UNM_NIC_LOCKED_READ_REG(UNM_NIU_MODE, &regs_buff[0]);
		mode = regs_buff[0];

		// Common registers to all the modes
		UNM_NIC_LOCKED_READ_REG(UNM_NIU_STRAP_VALUE_SAVE_HIGHER, &regs_buff[2]);
		switch (mode) {
		case 4:{		//XGB Mode
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XG_SINGLE_TERM,
						&regs_buff[3]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XG_DRIVE_HI,
						&regs_buff[4]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XG_DRIVE_LO,
						&regs_buff[5]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XG_DTX, &regs_buff[6]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XG_DEQ, &regs_buff[7]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XG_WORD_ALIGN,
						&regs_buff[8]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XG_RESET,
						&regs_buff[9]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XG_POWER_DOWN,
						&regs_buff[10]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XG_RESET_PLL,
						&regs_buff[11]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XG_SERDES_LOOPBACK,
						&regs_buff[12]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XG_DO_BYTE_ALIGN,
						&regs_buff[13]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XG_TX_ENABLE,
						&regs_buff[14]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XG_RX_ENABLE,
						&regs_buff[15]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XG_STATUS,
						&regs_buff[16]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XG_PAUSE_THRESHOLD,
						&regs_buff[17]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_CONFIG_0,
						&regs_buff[18]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_CONFIG_1,
						&regs_buff[19]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_IPG,
						&regs_buff[20]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_STATION_ADDR_0_HI,
						&regs_buff[21]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_STATION_ADDR_0_1,
						&regs_buff[22]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_STATION_ADDR_1_LO,
						&regs_buff[23]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_STATUS,
						&regs_buff[24]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_MAX_FRAME_SIZE,
						&regs_buff[25]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_PAUSE_FRAME_VALUE,
						&regs_buff[26]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_TX_BYTE_CNT,
						&regs_buff[27]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_TX_FRAME_CNT,
						&regs_buff[28]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_RX_BYTE_CNT,
						&regs_buff[29]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_RX_FRAME_CNT,
						&regs_buff[30]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_AGGR_ERROR_CNT,
						&regs_buff[31]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_MULTICAST_FRAME_CNT,
						&regs_buff[32]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_UNICAST_FRAME_CNT,
						&regs_buff[33]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_CRC_ERROR_CNT,
						&regs_buff[34]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_OVERSIZE_FRAME_ERR,
						&regs_buff[35]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_UNDERSIZE_FRAME_ERR,
						&regs_buff[36]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_LOCAL_ERROR_CNT,
						&regs_buff[37]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_REMOTE_ERROR_CNT,
						&regs_buff[38]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_CONTROL_CHAR_CNT,
						&regs_buff[39]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_XGE_PAUSE_FRAME_CNT,
						&regs_buff[40]);
			break;
			}

		case 2:{		// GB Mode
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_SERDES_RESET,
						&regs_buff[3]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB0_MII_MODE,
						&regs_buff[4]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB1_MII_MODE,
						&regs_buff[5]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB2_MII_MODE,
						&regs_buff[6]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB3_MII_MODE,
						&regs_buff[7]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB0_GMII_MODE,
						&regs_buff[8]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB1_GMII_MODE,
						&regs_buff[9]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB2_GMII_MODE,
						&regs_buff[10]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB3_GMII_MODE,
						&regs_buff[11]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_REMOTE_LOOPBACK,
						&regs_buff[12]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB0_HALF_DUPLEX,
						&regs_buff[13]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB1_HALF_DUPLEX,
						&regs_buff[14]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_RESET_SYS_FIFOS,
						&regs_buff[15]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_CRC_DROP,
						&regs_buff[16]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_DROP_WRONGADDR,
						&regs_buff[17]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_TEST_MUX_CTL,
						&regs_buff[18]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_MAC_CONFIG_0
						(adapter->physical_port),
						&regs_buff[19]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_MAC_CONFIG_1
						(adapter->physical_port),
						&regs_buff[20]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_HALF_DUPLEX_CTRL
						(adapter->physical_port),
						&regs_buff[21]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_MAX_FRAME_SIZE
						(adapter->physical_port),
						&regs_buff[22]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_TEST_REG
						(adapter->physical_port),
						&regs_buff[23]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_MII_MGMT_CONFIG
						(adapter->physical_port),
						&regs_buff[24]);
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_MII_MGMT_COMMAND
						(adapter->physical_port),
						&regs_buff[25]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_MII_MGMT_ADDR
						(adapter->physical_port),
						&regs_buff[26]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_MII_MGMT_CTRL
						(adapter->physical_port),
						&regs_buff[27]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_MII_MGMT_STATUS
						(adapter->physical_port),
						&regs_buff[28]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_MII_MGMT_INDICATE
						(adapter->physical_port),
						&regs_buff[29]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_INTERFACE_CTRL
						(adapter->physical_port),
						&regs_buff[30]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_INTERFACE_STATUS
						(adapter->physical_port),
						&regs_buff[31]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_STATION_ADDR_0
						(adapter->physical_port),
						&regs_buff[32]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_GB_STATION_ADDR_1
						(adapter->physical_port),
						&regs_buff[33]);
			break;
			}

		case 1:{		// FC Mode
			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_RX_STATUS
						(adapter->physical_port),
						&regs_buff[3]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_RX_COMMA_DETECT
						(adapter->physical_port),
						&regs_buff[4]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_LASER_UNSAFE
						(adapter->physical_port),
						&regs_buff[5]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_TX_CONTROL
						(adapter->physical_port),
						&regs_buff[6]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_ON_OFFLINE_CTL
						(adapter->physical_port),
						&regs_buff[7]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_PORT_ACTIVE_STAT
						(adapter->physical_port),
						&regs_buff[8]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_PORT_INACTIVE_STAT
						(adapter->physical_port),
						&regs_buff[9]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_LINK_FAILURE_CNT
						(adapter->physical_port),
						&regs_buff[10]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_LOSS_SYNC_CNT
						(adapter->physical_port),
						&regs_buff[11]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_LOSS_SIGNAL_CNT
						(adapter->physical_port),
						&regs_buff[12]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_PRIM_SEQ_ERR_CNT
						(adapter->physical_port),
						&regs_buff[13]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_INVLD_TX_WORD_CNT
						(adapter->physical_port),
						&regs_buff[14]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_INVLD_CRC_CNT
						(adapter->physical_port),
						&regs_buff[15]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_RX_CELL_CNT
						(adapter->physical_port),
						&regs_buff[16]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_TX_CELL_CNT
						(adapter->physical_port),
						&regs_buff[17]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_B2B_CREDIT
						(adapter->physical_port),
						&regs_buff[18]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_LOGIN_DONE
						(adapter->physical_port),
						&regs_buff[19]);

			UNM_NIC_LOCKED_READ_REG(UNM_NIU_FC_OPERATING_SPEED
						(adapter->physical_port),
						&regs_buff[20]);
			break;
			}
		}
		read_unlock(&adapter->adapter_lock);
	} else {
		/* P3 */
		// which mode
		nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
			crb_pci_to_internal(UNM_NIU_MODE), 0, &regs_buff[0]);
		mode = regs_buff[0];

		// Common registers to all the modes
		nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
			crb_pci_to_internal(UNM_NIU_STRAP_VALUE_SAVE_HIGHER),
			0, &regs_buff[2]);
		switch (mode) {
		case 4:{		//XGB Mode
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XG_SINGLE_TERM),
				0, &regs_buff[3]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XG_DRIVE_HI),
				0, &regs_buff[4]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XG_DRIVE_LO),
				0, &regs_buff[5]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XG_DTX),
				0, &regs_buff[6]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XG_DEQ),
				0, &regs_buff[7]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XG_WORD_ALIGN),
				0, &regs_buff[8]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XG_RESET),
				0, &regs_buff[9]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XG_POWER_DOWN),
				0, &regs_buff[10]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XG_RESET_PLL),
				0, &regs_buff[11]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XG_SERDES_LOOPBACK),
				0, &regs_buff[12]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XG_DO_BYTE_ALIGN),
				0, &regs_buff[13]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XG_TX_ENABLE),
				0, &regs_buff[14]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XG_RX_ENABLE),
				0, &regs_buff[15]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XG_STATUS),
				0, &regs_buff[16]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XG_PAUSE_THRESHOLD),
				0, &regs_buff[17]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_CONFIG_0),
				0, &regs_buff[18]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_CONFIG_1),
				0, &regs_buff[19]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_IPG),
				0, &regs_buff[20]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_STATION_ADDR_0_HI),
				0, &regs_buff[21]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_STATION_ADDR_0_1),
				0, &regs_buff[22]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_STATION_ADDR_1_LO),
				0, &regs_buff[23]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_STATUS),
				0, &regs_buff[24]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_MAX_FRAME_SIZE),
				0, &regs_buff[25]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_PAUSE_FRAME_VALUE),
				0, &regs_buff[26]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_TX_BYTE_CNT),
				0, &regs_buff[27]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_TX_FRAME_CNT),
				0, &regs_buff[28]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_RX_BYTE_CNT),
				0, &regs_buff[29]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_RX_FRAME_CNT),
				0, &regs_buff[30]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_AGGR_ERROR_CNT),
				0, &regs_buff[31]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_MULTICAST_FRAME_CNT),
				0, &regs_buff[32]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_UNICAST_FRAME_CNT),
				0, &regs_buff[33]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_CRC_ERROR_CNT),
				0, &regs_buff[34]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_OVERSIZE_FRAME_ERR),
				0, &regs_buff[35]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_UNDERSIZE_FRAME_ERR),
				0, &regs_buff[36]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_LOCAL_ERROR_CNT),
				0, &regs_buff[37]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_REMOTE_ERROR_CNT),
				0, &regs_buff[38]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_CONTROL_CHAR_CNT),
				0, &regs_buff[39]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_XGE_PAUSE_FRAME_CNT),
				0, &regs_buff[40]);
			break;
			}

		case 2:{		// GB Mode
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_SERDES_RESET),
				0, &regs_buff[3]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB0_MII_MODE),
				0, &regs_buff[4]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB1_MII_MODE),
				0, &regs_buff[5]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB2_MII_MODE),
				0, &regs_buff[6]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB3_MII_MODE),
				0, &regs_buff[7]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB0_GMII_MODE),
				0, &regs_buff[8]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB1_GMII_MODE),
				0, &regs_buff[9]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB2_GMII_MODE),
				0, &regs_buff[10]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB3_GMII_MODE),
				0, &regs_buff[11]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_REMOTE_LOOPBACK),
				0, &regs_buff[12]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB0_HALF_DUPLEX),
				0, &regs_buff[13]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB1_HALF_DUPLEX),
				0, &regs_buff[14]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_RESET_SYS_FIFOS),
				0, &regs_buff[15]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_CRC_DROP),
				0, &regs_buff[16]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_DROP_WRONGADDR),
				0, &regs_buff[17]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_TEST_MUX_CTL),
				0, &regs_buff[18]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_MAC_CONFIG_0(0)),
				0x10000, &regs_buff[19]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_MAC_CONFIG_1(0)),
				0x10000, &regs_buff[20]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_HALF_DUPLEX_CTRL(0)),
				0x10000, &regs_buff[21]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_MAX_FRAME_SIZE(0)),
				0x10000, &regs_buff[22]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_TEST_REG(0)),
				0x10000, &regs_buff[23]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_MII_MGMT_CONFIG(0)),
				0x10000, &regs_buff[24]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_MII_MGMT_COMMAND(0)),
				0x10000, &regs_buff[25]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_MII_MGMT_ADDR(0)),
				0x10000, &regs_buff[26]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_MII_MGMT_CTRL(0)),
				0x10000, &regs_buff[27]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_MII_MGMT_STATUS(0)),
				0x10000, &regs_buff[28]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_MII_MGMT_INDICATE(0)),
				0x10000, &regs_buff[29]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_INTERFACE_CTRL(0)),
				0x10000, &regs_buff[30]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_INTERFACE_STATUS(0)),
				0x10000, &regs_buff[31]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_STATION_ADDR_0(0)),
				0x10000, &regs_buff[32]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_GB_STATION_ADDR_1(0)),
				0x10000, &regs_buff[33]);
			break;
			}

		case 1:{		// FC Mode
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_RX_STATUS(0)),
				0x10000, &regs_buff[3]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_RX_COMMA_DETECT(0)),
				0x10000, &regs_buff[4]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_LASER_UNSAFE(0)),
				0x10000, &regs_buff[5]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_TX_CONTROL(0)),
				0x10000, &regs_buff[6]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_ON_OFFLINE_CTL(0)),
				0x10000, &regs_buff[7]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_PORT_ACTIVE_STAT(0)),
				0x10000, &regs_buff[8]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_PORT_INACTIVE_STAT(0)),
				0x10000, &regs_buff[9]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_LINK_FAILURE_CNT(0)),
				0x10000, &regs_buff[10]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_LOSS_SYNC_CNT(0)),
				0x10000, &regs_buff[11]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_LOSS_SIGNAL_CNT(0)),
				0x10000, &regs_buff[12]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_PRIM_SEQ_ERR_CNT(0)),
				0x10000, &regs_buff[13]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_INVLD_TX_WORD_CNT(0)),
				0x10000, &regs_buff[14]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_INVLD_CRC_CNT(0)),
				0x10000, &regs_buff[15]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_RX_CELL_CNT(0)),
				0x10000, &regs_buff[16]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_TX_CELL_CNT(0)),
				0x10000, &regs_buff[17]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_B2B_CREDIT(0)),
				0x10000, &regs_buff[18]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_LOGIN_DONE(0)),
				0x10000, &regs_buff[19]);
			nx_fw_cmd_query_hw_reg(adapter, adapter->portnum,
				crb_pci_to_internal(UNM_NIU_FC_OPERATING_SPEED(0)),
				0x10000, &regs_buff[20]);
			break;
			}
		}
	}
}

/*
 * Get the per adapter message level. Currently the global is not adjusted.
 */
static uint32_t unm_nic_get_msglevel(struct net_device *netdev)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);

	return adapter->msglvl;
}

/*
 * Set the per adapter message level. Currently the global is not adjusted.
 */
static void unm_nic_set_msglevel(struct net_device *netdev, uint32_t data)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);

	adapter->msglvl = data;
}

/* Restart Link Process */
static int unm_nic_nway_reset(struct net_device *netdev)
{
	if (netif_running(netdev)) {
		netdev->stop(netdev);	// verify
		netdev->open(netdev);
	}
	return 0;
}

static int
unm_nic_get_eeprom(struct net_device *netdev,
		   struct ethtool_eeprom *eeprom, uint8_t * bytes)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	int i, b_offset, b_end;
	uint8_t b_data[4], b_align;
	uint32_t data, b_rem;

	if ((eeprom->len <= 0) || (eeprom->offset >= MAX_ROM_SIZE))
		return -EINVAL;

	eeprom->magic = adapter->ahw.vendor_id | (adapter->ahw.device_id << 16);

	if ((eeprom->offset + eeprom->len) > MAX_ROM_SIZE)
		eeprom->len = MAX_ROM_SIZE - eeprom->offset;

	b_offset = eeprom->offset;
	b_end = eeprom->len + eeprom->offset;
	b_align = 4;
	b_rem = eeprom->offset % 4;
	/*Check for offset which is not 4 byte aligned */
	if ((b_rem) & 0x03) {

		b_offset -= b_rem;
		if (rom_fast_read(adapter, b_offset, &data) == -1)
			return -1;
		memcpy(b_data, &data, b_align);
		if (eeprom->len < (b_align - b_rem)) {
			memcpy(bytes, (b_data + b_rem), eeprom->len);
			return 0;
		} else
			memcpy(bytes, (b_data + b_rem), (b_align - b_rem));
		b_offset += b_align;
		bytes += b_align - b_rem;
	}

	for (i = 0; i <= (b_end - b_offset - b_align); i += b_align) {

		if (rom_fast_read(adapter, (b_offset + i), &data) == -1)
			return -1;
		memcpy((bytes + i), &data, b_align);
	}

	if ((b_end % b_align) & 0x03) {

		if (rom_fast_read(adapter, (b_offset + i), &data) == -1)
			return -1;
		memcpy((bytes + i), &data, (b_end % b_align));
	}

	return 0;
}

static void unm_nic_get_ringparam(struct net_device *netdev,
				  struct ethtool_ringparam *ring)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);

	ring->rx_mini_pending = 0;
	ring->rx_mini_max_pending = 0;

	ring->rx_pending = adapter->MaxRxDescCount;
	ring->rx_max_pending = NX_MAX_SUPPORTED_RDS_SIZE;

	ring->rx_jumbo_pending = adapter->MaxJumboRxDescCount;
	ring->rx_jumbo_max_pending = NX_MAX_SUPPORTED_JUMBO_RDS_SIZE;

	ring->tx_pending = adapter->MaxTxDescCount;
	ring->tx_max_pending = MAX_CMD_DESCRIPTORS;
}

// NO CHANGE of ringparams allowed !
static int unm_nic_set_ringparam(struct net_device *netdev,
				 struct ethtool_ringparam *ring)
{
	return (-EIO);
}

static void
unm_nic_get_pauseparam(struct net_device *netdev,
		       struct ethtool_pauseparam *pause)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	int temp;

	pause->autoneg = AUTONEG_DISABLE;

	if ((adapter->ahw.board_type != UNM_NIC_GBE) &&
	    (adapter->ahw.board_type != UNM_NIC_XGBE)) {
		printk(KERN_ERR "%s: Unknown board type: %x\n",
		       unm_nic_driver_name, adapter->ahw.board_type);
		return;
	}

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		if (nx_fw_cmd_get_flow_ctl(adapter, adapter->portnum, 0,
			&temp) == 0)
			pause->rx_pause = temp;
		if (nx_fw_cmd_get_flow_ctl(adapter, adapter->portnum, 1,
			&temp) == 0)
			pause->tx_pause = temp;
		return;
	}

	if (adapter->ahw.board_type == UNM_NIC_GBE) {
		if (unm_niu_gbe_get_rx_flow_ctl(adapter, &temp) == 0)
			pause->rx_pause = temp;
		if (unm_niu_gbe_get_tx_flow_ctl(adapter, &temp) == 0)
			pause->tx_pause = temp;

	} else {
		if (unm_niu_xg_get_tx_flow_ctl(adapter, &temp) == 0) {
			pause->tx_pause = temp;
		}
		pause->rx_pause = 1;	// always on for XG
	}
}

static int
unm_nic_set_pauseparam(struct net_device *netdev,
		       struct ethtool_pauseparam *pause)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	int ret = 0;

	if(pause->autoneg)
		return -EOPNOTSUPP;     

	if ((adapter->ahw.board_type != UNM_NIC_GBE) &&
	    (adapter->ahw.board_type != UNM_NIC_XGBE))
		return -EIO;

	if (NX_IS_REVISION_P3(adapter->ahw.revision_id)) {
		if (nx_fw_cmd_set_flow_ctl(adapter, adapter->portnum, 1,
			pause->tx_pause) != 0)
			ret = -EIO;

		if ((adapter->ahw.board_type == UNM_NIC_XGBE) &&
		    (!pause->rx_pause)) {
			/*
			 * Changing rx pause parameter is not 
			 * supported for now
			 */
			ret = -EOPNOTSUPP;
		} else {

			if (nx_fw_cmd_set_flow_ctl(adapter, adapter->portnum, 0,
				pause->rx_pause) != 0)
				ret = -EIO;
		}
		return ret;
	}

	if (adapter->ahw.board_type == UNM_NIC_GBE) {
		if (unm_niu_gbe_set_rx_flow_ctl(adapter, pause->rx_pause)) {
			ret = -EIO;
		}
		if (unm_niu_gbe_set_tx_flow_ctl(adapter, pause->tx_pause)) {
			ret = -EIO;
		}
	} else {
		if (unm_niu_xg_set_tx_flow_ctl(adapter, pause->tx_pause)) {
			ret = -EIO;
		}
		/* Changing rx pause parameter is not supported for now */
		if (!pause->rx_pause) {
			ret = -EOPNOTSUPP;
		}
	}
	return ret;
}

static uint32_t unm_nic_get_rx_csum(struct net_device *netdev)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)(netdev->priv);
	return adapter->rx_csum;
}

static int unm_nic_set_rx_csum(struct net_device *netdev, uint32_t data)
{
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)(netdev->priv);
	adapter->rx_csum = data;
	return 0;
}

static int unm_nic_reg_test(struct net_device *netdev)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	uint32_t data_read, data_written;

	// Read test
	unm_nic_read_w0(adapter, UNM_PCIX_PH_REG(0), &data_read);
	if ((data_read & 0xffff) != PHAN_VENDOR_ID) {
		return 1;
	}
	// write test
	data_written = (uint32_t) 0xa5a5a5a5;

	unm_nic_reg_write(adapter, CRB_SCRATCHPAD_TEST, data_written);
    adapter->unm_nic_hw_read_wx(adapter, CRB_SCRATCHPAD_TEST, &data_read, 4);
	if (data_written != data_read) {
		return 1;
	}

	return 0;
}
static int unm_nic_intr_test(struct net_device *netdev)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	if (!unm_irq_test(adapter))
		return 0;
	else
		return 1;
}

static int unm_nic_diag_test_count(struct net_device *netdev)
{
	return UNM_NIC_TEST_LEN;
}

static void
unm_nic_diag_test(struct net_device *netdev,
		  struct ethtool_test *eth_test, uint64_t * data)
{
	struct unm_adapter_s *adapter;
	int count;
	adapter = (struct unm_adapter_s *)netdev->priv;
	memset(data, 0, sizeof(uint64_t) * UNM_NIC_TEST_LEN);
	// online tests
	// register tests
	if ((data[0] = unm_nic_reg_test(netdev)))
		eth_test->flags |= ETH_TEST_FL_FAILED;
	// link test
	if ((data[1] = (uint64_t) unm_link_test(adapter)))
		eth_test->flags |= ETH_TEST_FL_FAILED;
	//LED test
	(adapter->ahw).LEDTestLast = 0;
	for (count = 0; count < 8; count++) {
		//Need to restore LED on last test
		if (count == 7)
			(adapter->ahw).LEDTestLast = 1;
		data[4] = (uint64_t) unm_led_test(adapter);
		mdelay(100);
	}
	if (data[4] == -LED_TEST_NOT_SUPPORTED || data[4] == 0)
		data[4] = 0;
	else
		eth_test->flags |= ETH_TEST_FL_FAILED;
	//End Led Test
	if ((eth_test->flags == ETH_TEST_FL_OFFLINE) && (adapter->is_up == ADAPTER_UP_MAGIC)) {	// offline tests
		if (netif_running(netdev))
			netif_stop_queue(netdev);

		// interrupt tests
		if ((data[2] = unm_nic_intr_test(netdev)))
			eth_test->flags |= ETH_TEST_FL_FAILED;
		if (netif_running(netdev))
			netif_wake_queue(netdev);
		// loopback tests
		data[3] = unm_loopback_test(netdev, 1, NULL, &adapter->testCtx);
		if (data[3] == -LB_NOT_SUPPORTED || data[3] == 0)
			data[3] = 0;
		else
			eth_test->flags |= ETH_TEST_FL_FAILED;
	} else {
		data[2] = 0;
		data[3] = 0;
	}
}

static void
unm_nic_get_strings(struct net_device *netdev, uint32_t stringset,
		    uint8_t * data)
{
	int i;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, *unm_nic_gstrings_test,
		       UNM_NIC_TEST_LEN * ETH_GSTRING_LEN);
		break;
	case ETH_SS_STATS:
		for (i = 0; i < UNM_NIC_STATS_LEN; i++) {
			memcpy(data + i * ETH_GSTRING_LEN,
			       unm_nic_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
		}
		break;
	}
}

static int unm_nic_get_stats_count(struct net_device *netdev)
{
	return UNM_NIC_STATS_LEN;
}

/*
 * NOTE: I have displayed only port's stats
 * TBD: unm_nic_stats(struct unm_port * port) doesn't update stats
 * as of now !! So this function may produce unexpected results !!
 */
static void
unm_nic_get_ethtool_stats(struct net_device *netdev,
			  struct ethtool_stats *stats, uint64_t * data)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	int i;

	for (i = 0; i < UNM_NIC_STATS_LEN; i++) {
		char *p =
		    (char *)adapter + unm_nic_gstrings_stats[i].stat_offset;
		data[i] =
		    (unm_nic_gstrings_stats[i].sizeof_stat ==
		     sizeof(uint64_t)) ? *(uint64_t *) p : *(uint32_t *) p;
	}

}

static void
unm_nic_get_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);
	u32 wol_cfg = 0;

	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		wol->supported = 0;
		wol->wolopts = 0;
		return;
	}

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	sw_lock(adapter);
    wol_cfg = adapter->unm_nic_pci_read_normalize(adapter, UNM_WOL_CONFIG);
	if (wol_cfg & (1 << adapter->portnum)) {
		wol->wolopts = WAKE_MAGIC;
	}
	sw_unlock(adapter);

	return;
}

static int
unm_nic_set_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct unm_adapter_s	*adapter = netdev_priv(netdev);
	u32 wol_cfg = 0;


	if (NX_IS_REVISION_P2(adapter->ahw.revision_id)) {
		return -EOPNOTSUPP;
	}

	if (wol->wolopts & ~WAKE_MAGIC)
		return -EOPNOTSUPP;

	sw_lock(adapter);
    wol_cfg = adapter->unm_nic_pci_read_normalize(adapter, UNM_WOL_CONFIG);
	if (wol->wolopts & WAKE_MAGIC) {
	        wol_cfg |= 1 << adapter->portnum;
	} else {
	        wol_cfg &= ~(1U << adapter->portnum);
	}
    adapter->unm_nic_pci_write_normalize(adapter, UNM_WOL_CONFIG, wol_cfg);
	sw_unlock(adapter);

	return 0;
}

/*
 * Set the coalescing parameters. Currently only normal is supported.
 * If rx_coalesce_usecs == 0 or rx_max_coalesced_frames == 0 then set the
 * firmware coalescing to default.
 */
static int nx_ethtool_set_intr_coalesce(struct net_device *netdev,
					struct ethtool_coalesce *ethcoal)
{
	struct unm_adapter_s *adapter = netdev_priv(netdev);

	if (adapter->state != PORT_UP) {
		return -EINVAL;
	}

	/*
	 * Return Error if  unsupported values or unsupported parameters are
	 * set
	 */
	if (ethcoal->rx_coalesce_usecs > 0xffff ||
	    ethcoal->rx_max_coalesced_frames > 0xffff ||
	    ethcoal->tx_coalesce_usecs > 0xffff ||
	    ethcoal->tx_max_coalesced_frames > 0xffff ||
	    ethcoal->rx_coalesce_usecs_irq ||
	    ethcoal->rx_max_coalesced_frames_irq ||
	    ethcoal->tx_coalesce_usecs_irq ||
	    ethcoal->tx_max_coalesced_frames_irq ||
	    ethcoal->stats_block_coalesce_usecs ||
	    ethcoal->use_adaptive_rx_coalesce ||
	    ethcoal->use_adaptive_tx_coalesce ||
	    ethcoal->pkt_rate_low ||
	    ethcoal->rx_coalesce_usecs_low ||
	    ethcoal->rx_max_coalesced_frames_low ||
	    ethcoal->tx_coalesce_usecs_low ||
	    ethcoal->tx_max_coalesced_frames_low ||
	    ethcoal->pkt_rate_high ||
	    ethcoal->rx_coalesce_usecs_high ||
	    ethcoal->rx_max_coalesced_frames_high ||
	    ethcoal->tx_coalesce_usecs_high ||
	    ethcoal->tx_max_coalesced_frames_high) {
		return -EINVAL;
	}

	if (ethcoal->rx_coalesce_usecs == 0 ||
	    ethcoal->rx_max_coalesced_frames == 0) {
		adapter->coal.flags = NX_NIC_INTR_DEFAULT;
		adapter->coal.normal.data.rx_time_us =
			NX_DEFAULT_INTR_COALESCE_RX_TIME_US;
		adapter->coal.normal.data.rx_packets =
			NX_DEFAULT_INTR_COALESCE_RX_PACKETS;
	} else {
		adapter->coal.flags = 0;
		adapter->coal.normal.data.rx_time_us =
			ethcoal->rx_coalesce_usecs;
		adapter->coal.normal.data.rx_packets =
			ethcoal->rx_max_coalesced_frames;
	}
	adapter->coal.normal.data.tx_time_us = ethcoal->tx_coalesce_usecs;
	adapter->coal.normal.data.tx_packets =
		ethcoal->tx_max_coalesced_frames;

	nx_nic_config_intr_coalesce(adapter);

	return (0);
}

/*
 * Get the interrupt coalescing parameters.
 */
static int nx_ethtool_get_intr_coalesce(struct net_device *netdev,
					struct ethtool_coalesce *ethcoal)
{
	struct unm_adapter_s   *adapter = netdev_priv(netdev);

	if (adapter->state != PORT_UP) {
		return -EINVAL;
	}
	ethcoal->rx_coalesce_usecs = adapter->coal.normal.data.rx_time_us;
	ethcoal->tx_coalesce_usecs =  adapter->coal.normal.data.tx_time_us;
	ethcoal->rx_max_coalesced_frames =
		adapter->coal.normal.data.rx_packets;
	ethcoal->tx_max_coalesced_frames =
		adapter->coal.normal.data.tx_packets;	
	return 0;
}


static struct ethtool_ops unm_nic_ethtool_ops = {
	.get_settings		= unm_nic_get_settings,
	.set_settings		= unm_nic_set_settings,
	.get_drvinfo		= unm_nic_get_drvinfo,
	.get_regs_len		= unm_nic_get_regs_len,
	.get_regs		= unm_nic_get_regs,
	.get_wol		= unm_nic_get_wol,
	.set_wol		= unm_nic_set_wol,
	.get_msglevel		= unm_nic_get_msglevel,
	.set_msglevel		= unm_nic_set_msglevel,
	.nway_reset		= unm_nic_nway_reset,
	.get_link		= ethtool_op_get_link,

#ifndef __VMKERNEL_MODULE__
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	.get_eeprom_len		= unm_nic_get_eeprom_len,
#endif
#endif
	.get_eeprom		= unm_nic_get_eeprom,
	.get_ringparam		= unm_nic_get_ringparam,
	.set_ringparam		= unm_nic_set_ringparam,
	.get_pauseparam		= unm_nic_get_pauseparam,
	.set_pauseparam		= unm_nic_set_pauseparam,
	.get_rx_csum		= unm_nic_get_rx_csum,
	.set_rx_csum		= unm_nic_set_rx_csum,
	.get_tx_csum		= unm_nic_get_tx_csum,
	.set_tx_csum		= unm_nic_set_tx_csum,
	.get_sg			= ethtool_op_get_sg,
	.set_sg			= ethtool_op_set_sg,
#ifndef __VMKERNEL_MODULE__
#ifdef NETIF_F_TSO
	.get_tso		= ethtool_op_get_tso,
	.set_tso		= ethtool_op_set_tso,
#endif
#endif
	.self_test_count	= unm_nic_diag_test_count,
	.self_test		= unm_nic_diag_test,
	.get_strings		= unm_nic_get_strings,
	.get_stats_count	= unm_nic_get_stats_count,
	.get_ethtool_stats	= unm_nic_get_ethtool_stats,
	.get_coalesce		= nx_ethtool_get_intr_coalesce,
	.set_coalesce		= nx_ethtool_set_intr_coalesce,
};

void set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &unm_nic_ethtool_ops;
}
#endif /* LINUX_VERSION */
