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


/* ethtool support for ixgbe */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/vmalloc.h>
#ifdef SIOCETHTOOL
#include <asm/uaccess.h>

#include "ixgbe.h"

#ifndef ETH_GSTRING_LEN
#define ETH_GSTRING_LEN 32
#endif

#define IXGBE_ALL_RAR_ENTRIES 16

#ifdef ETHTOOL_OPS_COMPAT
#include "kcompat_ethtool.c"
#endif

#ifdef ETHTOOL_GSTATS
struct ixgbe_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define IXGBE_STAT(m) sizeof(((struct ixgbe_adapter *)0)->m), \
		      offsetof(struct ixgbe_adapter, m)
static struct ixgbe_stats ixgbe_gstrings_stats[] = {
	{"rx_packets", IXGBE_STAT(net_stats.rx_packets)},
	{"tx_packets", IXGBE_STAT(net_stats.tx_packets)},
	{"rx_bytes", IXGBE_STAT(net_stats.rx_bytes)},
	{"tx_bytes", IXGBE_STAT(net_stats.tx_bytes)},
	{"lsc_int", IXGBE_STAT(lsc_int)},
	{"tx_busy", IXGBE_STAT(tx_busy)},
	{"non_eop_descs", IXGBE_STAT(non_eop_descs)},
	{"rx_errors", IXGBE_STAT(net_stats.rx_errors)},
	{"tx_errors", IXGBE_STAT(net_stats.tx_errors)},
	{"rx_dropped", IXGBE_STAT(net_stats.rx_dropped)},
#ifndef CONFIG_IXGBE_NAPI
	{"rx_dropped_backlog", IXGBE_STAT(rx_dropped_backlog)},
#endif
	{"tx_dropped", IXGBE_STAT(net_stats.tx_dropped)},
	{"multicast", IXGBE_STAT(net_stats.multicast)},
	{"broadcast", IXGBE_STAT(stats.bprc)},
	{"rx_no_buffer_count", IXGBE_STAT(stats.rnbc[0]) },
	{"collisions", IXGBE_STAT(net_stats.collisions)},
	{"rx_over_errors", IXGBE_STAT(net_stats.rx_over_errors)},
	{"rx_crc_errors", IXGBE_STAT(net_stats.rx_crc_errors)},
	{"rx_frame_errors", IXGBE_STAT(net_stats.rx_frame_errors)},
	{"rx_fifo_errors", IXGBE_STAT(net_stats.rx_fifo_errors)},
	{"rx_missed_errors", IXGBE_STAT(net_stats.rx_missed_errors)},
	{"tx_aborted_errors", IXGBE_STAT(net_stats.tx_aborted_errors)},
	{"tx_carrier_errors", IXGBE_STAT(net_stats.tx_carrier_errors)},
	{"tx_fifo_errors", IXGBE_STAT(net_stats.tx_fifo_errors)},
	{"tx_heartbeat_errors", IXGBE_STAT(net_stats.tx_heartbeat_errors)},
	{"tx_timeout_count", IXGBE_STAT(tx_timeout_count)},
	{"tx_restart_queue", IXGBE_STAT(restart_queue)},
	{"rx_long_length_errors", IXGBE_STAT(stats.roc)},
	{"rx_short_length_errors", IXGBE_STAT(stats.ruc)},
#ifdef NETIF_F_TSO
	{"tx_tcp4_seg_ctxt", IXGBE_STAT(hw_tso_ctxt)},
#ifdef NETIF_F_TSO6
	{"tx_tcp6_seg_ctxt", IXGBE_STAT(hw_tso6_ctxt)},
#endif
#endif
	{"tx_flow_control_xon", IXGBE_STAT(stats.lxontxc)},
	{"rx_flow_control_xon", IXGBE_STAT(stats.lxonrxc)},
	{"tx_flow_control_xoff", IXGBE_STAT(stats.lxofftxc)},
	{"rx_flow_control_xoff", IXGBE_STAT(stats.lxoffrxc)},
	{"rx_csum_offload_good", IXGBE_STAT(hw_csum_rx_good)},
	{"rx_csum_offload_errors", IXGBE_STAT(hw_csum_rx_error)},
	{"tx_csum_offload_ctxt", IXGBE_STAT(hw_csum_tx_good)},
	{"rx_header_split", IXGBE_STAT(rx_hdr_split)},
#ifndef IXGBE_NO_LLI
	{"low_latency_interrupt", IXGBE_STAT(lli_int)},
#endif
	{"alloc_rx_page_failed", IXGBE_STAT(alloc_rx_page_failed)},
	{"alloc_rx_buff_failed", IXGBE_STAT(alloc_rx_buff_failed)},
#ifndef IXGBE_NO_LRO
	{"lro_flushed", IXGBE_STAT(lro_data.stats.flushed)},
	{"lro_coal", IXGBE_STAT(lro_data.stats.coal)},
#endif /* IXGBE_NO_LRO */

};

#define IXGBE_QUEUE_STATS_LEN ( \
                (((struct ixgbe_adapter *)netdev->priv)->num_tx_queues + \
                 ((struct ixgbe_adapter *)netdev->priv)->num_rx_queues) * \
                 (sizeof(struct ixgbe_queue_stats) / sizeof(u64)) \
	)
#define IXGBE_PB_STATS_LEN 0
#define IXGBE_GLOBAL_STATS_LEN	\
	sizeof(ixgbe_gstrings_stats) / sizeof(struct ixgbe_stats)
#define IXGBE_STATS_LEN ( \
		IXGBE_GLOBAL_STATS_LEN + \
		IXGBE_PB_STATS_LEN + \
		IXGBE_QUEUE_STATS_LEN)
#endif /* ETHTOOL_GSTATS */
#ifdef ETHTOOL_TEST
static const char ixgbe_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register test  (offline)", "Eeprom test    (offline)",
	"Interrupt test (offline)", "Loopback test  (offline)",
	"Link test   (on/offline)"
};
#define IXGBE_TEST_LEN sizeof(ixgbe_gstrings_test) / ETH_GSTRING_LEN
#endif /* ETHTOOL_TEST */

static int ixgbe_get_settings(struct net_device *netdev,
                              struct ethtool_cmd *ecmd)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u32 link_speed = 0;
	bool link_up;

	ecmd->supported = SUPPORTED_10000baseT_Full;
	ecmd->autoneg = AUTONEG_ENABLE;
	ecmd->transceiver = XCVR_EXTERNAL;
	if (hw->phy.media_type == ixgbe_media_type_copper) {
		ecmd->supported |= (SUPPORTED_1000baseT_Full |
		                    SUPPORTED_TP | SUPPORTED_Autoneg);

		ecmd->advertising = (ADVERTISED_TP | ADVERTISED_Autoneg);
		if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_10GB_FULL)
			ecmd->advertising |= ADVERTISED_10000baseT_Full;
		if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_1GB_FULL)
			ecmd->advertising |= ADVERTISED_1000baseT_Full;

		ecmd->port = PORT_TP;
	} else {
		ecmd->supported |= SUPPORTED_FIBRE;
		ecmd->advertising = (ADVERTISED_10000baseT_Full |
		                     ADVERTISED_FIBRE);
		ecmd->port = PORT_FIBRE;
		ecmd->autoneg = AUTONEG_DISABLE;
	}

	ixgbe_check_link(hw, &link_speed, &link_up, false);
	if (link_up) {
		ecmd->speed = (link_speed == IXGBE_LINK_SPEED_10GB_FULL) ?
		               SPEED_10000 : SPEED_1000;
		ecmd->duplex = DUPLEX_FULL;
	} else {
		ecmd->speed = -1;
		ecmd->duplex = -1;
	}

	return 0;
}

static int ixgbe_set_settings(struct net_device *netdev,
                              struct ethtool_cmd *ecmd)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u32 advertised, old;
	s32 err;

	switch (hw->phy.media_type) {
	case ixgbe_media_type_fiber:
		if ((ecmd->autoneg == AUTONEG_ENABLE) ||
		    (ecmd->speed + ecmd->duplex != SPEED_10000 + DUPLEX_FULL))
			return -EINVAL;
		/* in this case we currently only support 10Gb/FULL */
		break;
	case ixgbe_media_type_copper:
		/* 10000/copper and 1000/copper must autoneg
		 * this function does not support any duplex forcing, but can
		 * limit the advertising of the adapter to only 10000 or 1000 */
		if (ecmd->autoneg == AUTONEG_DISABLE)
			return -EINVAL;

		old = hw->phy.autoneg_advertised;
		advertised = 0;
		if (ecmd->advertising & ADVERTISED_10000baseT_Full)
			advertised |= IXGBE_LINK_SPEED_10GB_FULL;

		if (ecmd->advertising & ADVERTISED_1000baseT_Full)
			advertised |= IXGBE_LINK_SPEED_1GB_FULL;

		if (old == advertised)
			break;
		/* this sets the link speed and restarts auto-neg */
		err = ixgbe_setup_link_speed(hw, advertised, true, true);
		if (err) {
			DPRINTK(PROBE, INFO,
			        "setup link failed with code %d\n", err);
			ixgbe_setup_link_speed(hw, old, true, true);
		}
		break;
	default:
		break;
	}

	return 0;
}

static void ixgbe_get_pauseparam(struct net_device *netdev,
                                 struct ethtool_pauseparam *pause)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;

	pause->autoneg = (hw->fc.type == ixgbe_fc_full ? 1 : 0);

	if (hw->fc.type == ixgbe_fc_rx_pause) {
		pause->rx_pause = 1;
	} else if (hw->fc.type == ixgbe_fc_tx_pause) {
		pause->tx_pause = 1;
	} else if (hw->fc.type == ixgbe_fc_full) {
		pause->rx_pause = 1;
		pause->tx_pause = 1;
	}
}

static int ixgbe_set_pauseparam(struct net_device *netdev,
                                struct ethtool_pauseparam *pause)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;

	if ((pause->autoneg == AUTONEG_ENABLE) ||
	    (pause->rx_pause && pause->tx_pause))
		hw->fc.type = ixgbe_fc_full;
	else if (pause->rx_pause && !pause->tx_pause)
		hw->fc.type = ixgbe_fc_rx_pause;
	else if (!pause->rx_pause && pause->tx_pause)
		hw->fc.type = ixgbe_fc_tx_pause;
	else if (!pause->rx_pause && !pause->tx_pause)
		hw->fc.type = ixgbe_fc_none;
	else
		return -EINVAL;

	hw->fc.original_type = hw->fc.type;

	if (netif_running(netdev))
		ixgbe_reinit_locked(adapter);
	else
		ixgbe_reset(adapter);

	return 0;
}

static u32 ixgbe_get_rx_csum(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	return (adapter->flags & IXGBE_FLAG_RX_CSUM_ENABLED);
}

static int ixgbe_set_rx_csum(struct net_device *netdev, u32 data)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if (data)
		adapter->flags |= IXGBE_FLAG_RX_CSUM_ENABLED;
	else
		adapter->flags &= ~IXGBE_FLAG_RX_CSUM_ENABLED;

	if (netif_running(netdev))
		ixgbe_reinit_locked(adapter);
	else
		ixgbe_reset(adapter);

	return 0;
}

static u32 ixgbe_get_tx_csum(struct net_device *netdev)
{
	return (netdev->features & NETIF_F_HW_CSUM) != 0;
}

static int ixgbe_set_tx_csum(struct net_device *netdev, u32 data)
{
	if (data)
		netdev->features |= NETIF_F_HW_CSUM;
	else
		netdev->features &= ~NETIF_F_HW_CSUM;

	return 0;
}

#ifdef NETIF_F_TSO
static int ixgbe_set_tso(struct net_device *netdev, u32 data)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	if (data) {
		netdev->features |= NETIF_F_TSO;
#ifdef NETIF_F_TSO6
		netdev->features |= NETIF_F_TSO6;
#endif
	} else {
#ifdef CONFIG_NETDEVICES_MULTIQUEUE
		int i;
#endif
		netif_stop_queue(netdev);
#ifdef CONFIG_NETDEVICES_MULTIQUEUE
		for (i = 0; i < adapter->num_tx_queues; i++)
			netif_stop_subqueue(netdev, i);
#endif
		netdev->features &= ~NETIF_F_TSO;
#ifdef NETIF_F_TSO6
		netdev->features &= ~NETIF_F_TSO6;
#endif
#ifdef NETIF_F_HW_VLAN_TX
		/* disable TSO on all VLANs if they're present */
		if (adapter->vlgrp) {
			int i;
			struct net_device *v_netdev;
			for (i = 0; i < VLAN_GROUP_ARRAY_LEN; i++) {
				v_netdev =
				       vlan_group_get_device(adapter->vlgrp, i);
				if (v_netdev) {
					v_netdev->features &= ~NETIF_F_TSO;
#ifdef NETIF_F_TSO6
					v_netdev->features &= ~NETIF_F_TSO6;
#endif
					vlan_group_set_device(adapter->vlgrp, i,
							      v_netdev);
				}
			}
		}
#endif
#ifdef CONFIG_NETDEVICES_MULTIQUEUE
		for (i = 0; i < adapter->num_tx_queues; i++)
			netif_start_subqueue(netdev, i);
#endif
		netif_start_queue(netdev);
	}
	return 0;
}
#endif /* NETIF_F_TSO */

static u32 ixgbe_get_msglevel(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	return adapter->msg_enable;
}

static void ixgbe_set_msglevel(struct net_device *netdev, u32 data)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	adapter->msg_enable = data;
}

static int ixgbe_get_regs_len(struct net_device *netdev)
{
#define IXGBE_REGS_LEN  1128
	return IXGBE_REGS_LEN * sizeof(u32);
}

#define IXGBE_GET_STAT(_A_, _R_) _A_->stats._R_

static void ixgbe_get_regs(struct net_device *netdev, struct ethtool_regs *regs,
                           void *p)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u32 *regs_buff = p;
	u8 i;

	memset(p, 0, IXGBE_REGS_LEN * sizeof(u32));

	regs->version = (1 << 24) | hw->revision_id << 16 | hw->device_id;

	/* General Registers */
	regs_buff[0] = IXGBE_READ_REG(hw, IXGBE_CTRL);
	regs_buff[1] = IXGBE_READ_REG(hw, IXGBE_STATUS);
	regs_buff[2] = IXGBE_READ_REG(hw, IXGBE_CTRL_EXT);
	regs_buff[3] = IXGBE_READ_REG(hw, IXGBE_ESDP);
	regs_buff[4] = IXGBE_READ_REG(hw, IXGBE_EODSDP);
	regs_buff[5] = IXGBE_READ_REG(hw, IXGBE_LEDCTL);
	regs_buff[6] = IXGBE_READ_REG(hw, IXGBE_FRTIMER);
	regs_buff[7] = IXGBE_READ_REG(hw, IXGBE_TCPTIMER);

	/* NVM Register */
	regs_buff[8] = IXGBE_READ_REG(hw, IXGBE_EEC);
	regs_buff[9] = IXGBE_READ_REG(hw, IXGBE_EERD);
	regs_buff[10] = IXGBE_READ_REG(hw, IXGBE_FLA);
	regs_buff[11] = IXGBE_READ_REG(hw, IXGBE_EEMNGCTL);
	regs_buff[12] = IXGBE_READ_REG(hw, IXGBE_EEMNGDATA);
	regs_buff[13] = IXGBE_READ_REG(hw, IXGBE_FLMNGCTL);
	regs_buff[14] = IXGBE_READ_REG(hw, IXGBE_FLMNGDATA);
	regs_buff[15] = IXGBE_READ_REG(hw, IXGBE_FLMNGCNT);
	regs_buff[16] = IXGBE_READ_REG(hw, IXGBE_FLOP);
	regs_buff[17] = IXGBE_READ_REG(hw, IXGBE_GRC);

	/* Interrupt */
	regs_buff[18] = IXGBE_READ_REG(hw, IXGBE_EICR);
	regs_buff[19] = IXGBE_READ_REG(hw, IXGBE_EICS);
	regs_buff[20] = IXGBE_READ_REG(hw, IXGBE_EIMS);
	regs_buff[21] = IXGBE_READ_REG(hw, IXGBE_EIMC);
	regs_buff[22] = IXGBE_READ_REG(hw, IXGBE_EIAC);
	regs_buff[23] = IXGBE_READ_REG(hw, IXGBE_EIAM);
	regs_buff[24] = IXGBE_READ_REG(hw, IXGBE_EITR(0));
	regs_buff[25] = IXGBE_READ_REG(hw, IXGBE_IVAR(0));
	regs_buff[26] = IXGBE_READ_REG(hw, IXGBE_MSIXT);
	regs_buff[27] = IXGBE_READ_REG(hw, IXGBE_MSIXPBA);
	regs_buff[28] = IXGBE_READ_REG(hw, IXGBE_PBACL(0));
	regs_buff[29] = IXGBE_READ_REG(hw, IXGBE_GPIE);

	/* Flow Control */
	regs_buff[30] = IXGBE_READ_REG(hw, IXGBE_PFCTOP);
	regs_buff[31] = IXGBE_READ_REG(hw, IXGBE_FCTTV(0));
	regs_buff[32] = IXGBE_READ_REG(hw, IXGBE_FCTTV(1));
	regs_buff[33] = IXGBE_READ_REG(hw, IXGBE_FCTTV(2));
	regs_buff[34] = IXGBE_READ_REG(hw, IXGBE_FCTTV(3));
	for (i = 0; i < 8; i++)
		regs_buff[35 + i] = IXGBE_READ_REG(hw, IXGBE_FCRTL(i));
	for (i = 0; i < 8; i++)
		regs_buff[43 + i] = IXGBE_READ_REG(hw, IXGBE_FCRTH(i));
	regs_buff[51] = IXGBE_READ_REG(hw, IXGBE_FCRTV);
	regs_buff[52] = IXGBE_READ_REG(hw, IXGBE_TFCS);

	/* Receive DMA */
	for (i = 0; i < 64; i++)
		regs_buff[53 + i] = IXGBE_READ_REG(hw, IXGBE_RDBAL(i));
	for (i = 0; i < 64; i++)
		regs_buff[117 + i] = IXGBE_READ_REG(hw, IXGBE_RDBAH(i));
	for (i = 0; i < 64; i++)
		regs_buff[181 + i] = IXGBE_READ_REG(hw, IXGBE_RDLEN(i));
	for (i = 0; i < 64; i++)
		regs_buff[245 + i] = IXGBE_READ_REG(hw, IXGBE_RDH(i));
	for (i = 0; i < 64; i++)
		regs_buff[309 + i] = IXGBE_READ_REG(hw, IXGBE_RDT(i));
	for (i = 0; i < 64; i++)
		regs_buff[373 + i] = IXGBE_READ_REG(hw, IXGBE_RXDCTL(i));
	for (i = 0; i < 16; i++)
		regs_buff[437 + i] = IXGBE_READ_REG(hw, IXGBE_SRRCTL(i));
	for (i = 0; i < 16; i++)
		regs_buff[453 + i] = IXGBE_READ_REG(hw, IXGBE_DCA_RXCTRL(i));
	regs_buff[469] = IXGBE_READ_REG(hw, IXGBE_RDRXCTL);
	for (i = 0; i < 8; i++)
		regs_buff[470 + i] = IXGBE_READ_REG(hw, IXGBE_RXPBSIZE(i));
	regs_buff[478] = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	regs_buff[479] = IXGBE_READ_REG(hw, IXGBE_DROPEN);

	/* Receive */
	regs_buff[480] = IXGBE_READ_REG(hw, IXGBE_RXCSUM);
	regs_buff[481] = IXGBE_READ_REG(hw, IXGBE_RFCTL);
	for (i = 0; i < 16; i++)
		regs_buff[482 + i] = IXGBE_READ_REG(hw, IXGBE_RAL(i));
	for (i = 0; i < 16; i++)
		regs_buff[498 + i] = IXGBE_READ_REG(hw, IXGBE_RAH(i));
	regs_buff[514] = IXGBE_READ_REG(hw, IXGBE_PSRTYPE(0));
	regs_buff[515] = IXGBE_READ_REG(hw, IXGBE_FCTRL);
	regs_buff[516] = IXGBE_READ_REG(hw, IXGBE_VLNCTRL);
	regs_buff[517] = IXGBE_READ_REG(hw, IXGBE_MCSTCTRL);
	regs_buff[518] = IXGBE_READ_REG(hw, IXGBE_MRQC);
	regs_buff[519] = IXGBE_READ_REG(hw, IXGBE_VMD_CTL);
	for (i = 0; i < 8; i++)
		regs_buff[520 + i] = IXGBE_READ_REG(hw, IXGBE_IMIR(i));
	for (i = 0; i < 8; i++)
		regs_buff[528 + i] = IXGBE_READ_REG(hw, IXGBE_IMIREXT(i));
	regs_buff[536] = IXGBE_READ_REG(hw, IXGBE_IMIRVP);

	/* Transmit */
	for (i = 0; i < 32; i++)
		regs_buff[537 + i] = IXGBE_READ_REG(hw, IXGBE_TDBAL(i));
	for (i = 0; i < 32; i++)
		regs_buff[569 + i] = IXGBE_READ_REG(hw, IXGBE_TDBAH(i));
	for (i = 0; i < 32; i++)
		regs_buff[601 + i] = IXGBE_READ_REG(hw, IXGBE_TDLEN(i));
	for (i = 0; i < 32; i++)
		regs_buff[633 + i] = IXGBE_READ_REG(hw, IXGBE_TDH(i));
	for (i = 0; i < 32; i++)
		regs_buff[665 + i] = IXGBE_READ_REG(hw, IXGBE_TDT(i));
	for (i = 0; i < 32; i++)
		regs_buff[697 + i] = IXGBE_READ_REG(hw, IXGBE_TXDCTL(i));
	for (i = 0; i < 32; i++)
		regs_buff[729 + i] = IXGBE_READ_REG(hw, IXGBE_TDWBAL(i));
	for (i = 0; i < 32; i++)
		regs_buff[761 + i] = IXGBE_READ_REG(hw, IXGBE_TDWBAH(i));
	regs_buff[793] = IXGBE_READ_REG(hw, IXGBE_DTXCTL);
	for (i = 0; i < 16; i++)
		regs_buff[794 + i] = IXGBE_READ_REG(hw, IXGBE_DCA_TXCTRL(i));
	regs_buff[810] = IXGBE_READ_REG(hw, IXGBE_TIPG);
	for (i = 0; i < 8; i++)
		regs_buff[811 + i] = IXGBE_READ_REG(hw, IXGBE_TXPBSIZE(i));
	regs_buff[819] = IXGBE_READ_REG(hw, IXGBE_MNGTXMAP);

	/* Wake Up */
	regs_buff[820] = IXGBE_READ_REG(hw, IXGBE_WUC);
	regs_buff[821] = IXGBE_READ_REG(hw, IXGBE_WUFC);
	regs_buff[822] = IXGBE_READ_REG(hw, IXGBE_WUS);
	regs_buff[823] = IXGBE_READ_REG(hw, IXGBE_IPAV);
	regs_buff[824] = IXGBE_READ_REG(hw, IXGBE_IP4AT);
	regs_buff[825] = IXGBE_READ_REG(hw, IXGBE_IP6AT);
	regs_buff[826] = IXGBE_READ_REG(hw, IXGBE_WUPL);
	regs_buff[827] = IXGBE_READ_REG(hw, IXGBE_WUPM);
	regs_buff[828] = IXGBE_READ_REG(hw, IXGBE_FHFT);

	regs_buff[829] = IXGBE_READ_REG(hw, IXGBE_RMCS);
	regs_buff[830] = IXGBE_READ_REG(hw, IXGBE_DPMCS);
	regs_buff[831] = IXGBE_READ_REG(hw, IXGBE_PDPMCS);
	regs_buff[832] = IXGBE_READ_REG(hw, IXGBE_RUPPBMR);
	for (i = 0; i < 8; i++)
		regs_buff[833 + i] = IXGBE_READ_REG(hw, IXGBE_RT2CR(i));
	for (i = 0; i < 8; i++)
		regs_buff[841 + i] = IXGBE_READ_REG(hw, IXGBE_RT2SR(i));
	for (i = 0; i < 8; i++)
		regs_buff[849 + i] = IXGBE_READ_REG(hw, IXGBE_TDTQ2TCCR(i));
	for (i = 0; i < 8; i++)
		regs_buff[857 + i] = IXGBE_READ_REG(hw, IXGBE_TDTQ2TCSR(i));
	for (i = 0; i < 8; i++)
		regs_buff[865 + i] = IXGBE_READ_REG(hw, IXGBE_TDPT2TCCR(i));
	for (i = 0; i < 8; i++)
		regs_buff[873 + i] = IXGBE_READ_REG(hw, IXGBE_TDPT2TCSR(i));

	/* Statistics */
	regs_buff[881] = IXGBE_GET_STAT(adapter, crcerrs);
	regs_buff[882] = IXGBE_GET_STAT(adapter, illerrc);
	regs_buff[883] = IXGBE_GET_STAT(adapter, errbc);
	regs_buff[884] = IXGBE_GET_STAT(adapter, mspdc);
	for (i = 0; i < 8; i++)
		regs_buff[885 + i] = IXGBE_GET_STAT(adapter, mpc[i]);
	regs_buff[893] = IXGBE_GET_STAT(adapter, mlfc);
	regs_buff[894] = IXGBE_GET_STAT(adapter, mrfc);
	regs_buff[895] = IXGBE_GET_STAT(adapter, rlec);
	regs_buff[896] = IXGBE_GET_STAT(adapter, lxontxc);
	regs_buff[897] = IXGBE_GET_STAT(adapter, lxonrxc);
	regs_buff[898] = IXGBE_GET_STAT(adapter, lxofftxc);
	regs_buff[899] = IXGBE_GET_STAT(adapter, lxoffrxc);
	for (i = 0; i < 8; i++)
		regs_buff[900 + i] = IXGBE_GET_STAT(adapter, pxontxc[i]);
	for (i = 0; i < 8; i++)
		regs_buff[908 + i] = IXGBE_GET_STAT(adapter, pxonrxc[i]);
	for (i = 0; i < 8; i++)
		regs_buff[916 + i] = IXGBE_GET_STAT(adapter, pxofftxc[i]);
	for (i = 0; i < 8; i++)
		regs_buff[924 + i] = IXGBE_GET_STAT(adapter, pxoffrxc[i]);
	regs_buff[932] = IXGBE_GET_STAT(adapter, prc64);
	regs_buff[933] = IXGBE_GET_STAT(adapter, prc127);
	regs_buff[934] = IXGBE_GET_STAT(adapter, prc255);
	regs_buff[935] = IXGBE_GET_STAT(adapter, prc511);
	regs_buff[936] = IXGBE_GET_STAT(adapter, prc1023);
	regs_buff[937] = IXGBE_GET_STAT(adapter, prc1522);
	regs_buff[938] = IXGBE_GET_STAT(adapter, gprc);
	regs_buff[939] = IXGBE_GET_STAT(adapter, bprc);
	regs_buff[940] = IXGBE_GET_STAT(adapter, mprc);
	regs_buff[941] = IXGBE_GET_STAT(adapter, gptc);
	regs_buff[942] = IXGBE_GET_STAT(adapter, gorc);
	regs_buff[944] = IXGBE_GET_STAT(adapter, gotc);
	for (i = 0; i < 8; i++)
		regs_buff[946 + i] = IXGBE_GET_STAT(adapter, rnbc[i]);
	regs_buff[954] = IXGBE_GET_STAT(adapter, ruc);
	regs_buff[955] = IXGBE_GET_STAT(adapter, rfc);
	regs_buff[956] = IXGBE_GET_STAT(adapter, roc);
	regs_buff[957] = IXGBE_GET_STAT(adapter, rjc);
	regs_buff[958] = IXGBE_GET_STAT(adapter, mngprc);
	regs_buff[959] = IXGBE_GET_STAT(adapter, mngpdc);
	regs_buff[960] = IXGBE_GET_STAT(adapter, mngptc);
	regs_buff[961] = IXGBE_GET_STAT(adapter, tor);
	regs_buff[963] = IXGBE_GET_STAT(adapter, tpr);
	regs_buff[964] = IXGBE_GET_STAT(adapter, tpt);
	regs_buff[965] = IXGBE_GET_STAT(adapter, ptc64);
	regs_buff[966] = IXGBE_GET_STAT(adapter, ptc127);
	regs_buff[967] = IXGBE_GET_STAT(adapter, ptc255);
	regs_buff[968] = IXGBE_GET_STAT(adapter, ptc511);
	regs_buff[969] = IXGBE_GET_STAT(adapter, ptc1023);
	regs_buff[970] = IXGBE_GET_STAT(adapter, ptc1522);
	regs_buff[971] = IXGBE_GET_STAT(adapter, mptc);
	regs_buff[972] = IXGBE_GET_STAT(adapter, bptc);
	regs_buff[973] = IXGBE_GET_STAT(adapter, xec);
	for (i = 0; i < 16; i++)
		regs_buff[974 + i] = IXGBE_GET_STAT(adapter, qprc[i]);
	for (i = 0; i < 16; i++)
		regs_buff[990 + i] = IXGBE_GET_STAT(adapter, qptc[i]);
	for (i = 0; i < 16; i++)
		regs_buff[1006 + i] = IXGBE_GET_STAT(adapter, qbrc[i]);
	for (i = 0; i < 16; i++)
		regs_buff[1022 + i] = IXGBE_GET_STAT(adapter, qbtc[i]);

	/* MAC */
	regs_buff[1038] = IXGBE_READ_REG(hw, IXGBE_PCS1GCFIG);
	regs_buff[1039] = IXGBE_READ_REG(hw, IXGBE_PCS1GLCTL);
	regs_buff[1040] = IXGBE_READ_REG(hw, IXGBE_PCS1GLSTA);
	regs_buff[1041] = IXGBE_READ_REG(hw, IXGBE_PCS1GDBG0);
	regs_buff[1042] = IXGBE_READ_REG(hw, IXGBE_PCS1GDBG1);
	regs_buff[1043] = IXGBE_READ_REG(hw, IXGBE_PCS1GANA);
	regs_buff[1044] = IXGBE_READ_REG(hw, IXGBE_PCS1GANLP);
	regs_buff[1045] = IXGBE_READ_REG(hw, IXGBE_PCS1GANNP);
	regs_buff[1046] = IXGBE_READ_REG(hw, IXGBE_PCS1GANLPNP);
	regs_buff[1047] = IXGBE_READ_REG(hw, IXGBE_HLREG0);
	regs_buff[1048] = IXGBE_READ_REG(hw, IXGBE_HLREG1);
	regs_buff[1049] = IXGBE_READ_REG(hw, IXGBE_PAP);
	regs_buff[1050] = IXGBE_READ_REG(hw, IXGBE_MACA);
	regs_buff[1051] = IXGBE_READ_REG(hw, IXGBE_APAE);
	regs_buff[1052] = IXGBE_READ_REG(hw, IXGBE_ARD);
	regs_buff[1053] = IXGBE_READ_REG(hw, IXGBE_AIS);
	regs_buff[1054] = IXGBE_READ_REG(hw, IXGBE_MSCA);
	regs_buff[1055] = IXGBE_READ_REG(hw, IXGBE_MSRWD);
	regs_buff[1056] = IXGBE_READ_REG(hw, IXGBE_MLADD);
	regs_buff[1057] = IXGBE_READ_REG(hw, IXGBE_MHADD);
	regs_buff[1058] = IXGBE_READ_REG(hw, IXGBE_TREG);
	regs_buff[1059] = IXGBE_READ_REG(hw, IXGBE_PCSS1);
	regs_buff[1060] = IXGBE_READ_REG(hw, IXGBE_PCSS2);
	regs_buff[1061] = IXGBE_READ_REG(hw, IXGBE_XPCSS);
	regs_buff[1062] = IXGBE_READ_REG(hw, IXGBE_SERDESC);
	regs_buff[1063] = IXGBE_READ_REG(hw, IXGBE_MACS);
	regs_buff[1064] = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	regs_buff[1065] = IXGBE_READ_REG(hw, IXGBE_LINKS);
	regs_buff[1066] = IXGBE_READ_REG(hw, IXGBE_AUTOC2);
	regs_buff[1067] = IXGBE_READ_REG(hw, IXGBE_AUTOC3);
	regs_buff[1068] = IXGBE_READ_REG(hw, IXGBE_ANLP1);
	regs_buff[1069] = IXGBE_READ_REG(hw, IXGBE_ANLP2);
	regs_buff[1070] = IXGBE_READ_REG(hw, IXGBE_ATLASCTL);

	/* Diagnostic */
	regs_buff[1071] = IXGBE_READ_REG(hw, IXGBE_RDSTATCTL);
	for (i = 0; i < 8; i++)
		regs_buff[1072 + i] = IXGBE_READ_REG(hw, IXGBE_RDSTAT(i));
	regs_buff[1080] = IXGBE_READ_REG(hw, IXGBE_RDHMPN);
	for (i = 0; i < 4; i++)
		regs_buff[1081 + i] = IXGBE_READ_REG(hw, IXGBE_RIC_DW(i));
	regs_buff[1085] = IXGBE_READ_REG(hw, IXGBE_RDPROBE);
	regs_buff[1086] = IXGBE_READ_REG(hw, IXGBE_TDSTATCTL);
	for (i = 0; i < 8; i++)
		regs_buff[1087 + i] = IXGBE_READ_REG(hw, IXGBE_TDSTAT(i));
	regs_buff[1095] = IXGBE_READ_REG(hw, IXGBE_TDHMPN);
	for (i = 0; i < 4; i++)
		regs_buff[1096 + i] = IXGBE_READ_REG(hw, IXGBE_TIC_DW(i));
	regs_buff[1100] = IXGBE_READ_REG(hw, IXGBE_TDPROBE);
	regs_buff[1101] = IXGBE_READ_REG(hw, IXGBE_TXBUFCTRL);
	regs_buff[1102] = IXGBE_READ_REG(hw, IXGBE_TXBUFDATA0);
	regs_buff[1103] = IXGBE_READ_REG(hw, IXGBE_TXBUFDATA1);
	regs_buff[1104] = IXGBE_READ_REG(hw, IXGBE_TXBUFDATA2);
	regs_buff[1105] = IXGBE_READ_REG(hw, IXGBE_TXBUFDATA3);
	regs_buff[1106] = IXGBE_READ_REG(hw, IXGBE_RXBUFCTRL);
	regs_buff[1107] = IXGBE_READ_REG(hw, IXGBE_RXBUFDATA0);
	regs_buff[1108] = IXGBE_READ_REG(hw, IXGBE_RXBUFDATA1);
	regs_buff[1109] = IXGBE_READ_REG(hw, IXGBE_RXBUFDATA2);
	regs_buff[1110] = IXGBE_READ_REG(hw, IXGBE_RXBUFDATA3);
	for (i = 0; i < 8; i++)
		regs_buff[1111 + i] = IXGBE_READ_REG(hw, IXGBE_PCIE_DIAG(i));
	regs_buff[1119] = IXGBE_READ_REG(hw, IXGBE_RFVAL);
	regs_buff[1120] = IXGBE_READ_REG(hw, IXGBE_MDFTC1);
	regs_buff[1121] = IXGBE_READ_REG(hw, IXGBE_MDFTC2);
	regs_buff[1122] = IXGBE_READ_REG(hw, IXGBE_MDFTFIFO1);
	regs_buff[1123] = IXGBE_READ_REG(hw, IXGBE_MDFTFIFO2);
	regs_buff[1124] = IXGBE_READ_REG(hw, IXGBE_MDFTS);
	regs_buff[1125] = IXGBE_READ_REG(hw, IXGBE_PCIEECCCTL);
	regs_buff[1126] = IXGBE_READ_REG(hw, IXGBE_PBTXECC);
	regs_buff[1127] = IXGBE_READ_REG(hw, IXGBE_PBRXECC);
}

static int ixgbe_get_eeprom_len(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	return adapter->hw.eeprom.word_size * 2;
}

static int ixgbe_get_eeprom(struct net_device *netdev,
			    struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u16 *eeprom_buff;
	int first_word, last_word, eeprom_len;
	int ret_val = 0;
	u16 i;

	if (eeprom->len == 0)
		return -EINVAL;

	eeprom->magic = hw->vendor_id | (hw->device_id << 16);

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;
	eeprom_len = last_word - first_word + 1;

	eeprom_buff = kmalloc(sizeof(u16) * eeprom_len, GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	for (i = 0; i < eeprom_len; i++) {
		if ((ret_val = ixgbe_read_eeprom(hw, first_word + i,
						 &eeprom_buff[i])))
			break;
	}

	/* Device's eeprom is always little-endian, word addressable */
	for (i = 0; i < eeprom_len; i++)
		le16_to_cpus(&eeprom_buff[i]);

	memcpy(bytes, (u8 *)eeprom_buff + (eeprom->offset & 1), eeprom->len);
	kfree(eeprom_buff);

	return ret_val;
}

static int ixgbe_set_eeprom(struct net_device *netdev,
                            struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u16 *eeprom_buff;
	void *ptr;
	int max_len, first_word, last_word, ret_val = 0;
	u16 i;

	if (eeprom->len == 0)
		return -EOPNOTSUPP;

	if (eeprom->magic != (hw->vendor_id | (hw->device_id << 16)))
		return -EFAULT;

	max_len = hw->eeprom.word_size * 2;

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;
	eeprom_buff = kmalloc(max_len, GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	ptr = (void *)eeprom_buff;

	if (eeprom->offset & 1) {
		/* need read/modify/write of first changed EEPROM word */
		/* only the second byte of the word is being modified */
		ret_val = ixgbe_read_eeprom(hw, first_word, &eeprom_buff[0]);
		ptr++;
	}
	if (((eeprom->offset + eeprom->len) & 1) && (ret_val == 0)) {
		/* need read/modify/write of last changed EEPROM word */
		/* only the first byte of the word is being modified */
		ret_val = ixgbe_read_eeprom(hw, last_word,
		                  &eeprom_buff[last_word - first_word]);
	}

	/* Device's eeprom is always little-endian, word addressable */
	for (i = 0; i < last_word - first_word + 1; i++)
		le16_to_cpus(&eeprom_buff[i]);

	memcpy(ptr, bytes, eeprom->len);

	for (i = 0; i <= (last_word - first_word); i++)
		ret_val |= ixgbe_write_eeprom(hw, first_word + i, eeprom_buff[i]);

	/* Update the checksum */
	ixgbe_update_eeprom_checksum(hw);

	kfree(eeprom_buff);
	return ret_val;
}

static void ixgbe_get_drvinfo(struct net_device *netdev,
                              struct ethtool_drvinfo *drvinfo)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	strncpy(drvinfo->driver, ixgbe_driver_name, 32);
	strncpy(drvinfo->version, ixgbe_driver_version, 32);
	strncpy(drvinfo->fw_version, "N/A", 32);
	strncpy(drvinfo->bus_info, pci_name(adapter->pdev), 32);
	drvinfo->n_stats = IXGBE_STATS_LEN;
	drvinfo->testinfo_len = IXGBE_TEST_LEN;
	drvinfo->regdump_len = ixgbe_get_regs_len(netdev);
}

static void ixgbe_get_ringparam(struct net_device *netdev,
                                struct ethtool_ringparam *ring)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_ring *tx_ring = adapter->tx_ring;
	struct ixgbe_ring *rx_ring = adapter->rx_ring;

	ring->rx_max_pending = IXGBE_MAX_RXD;
#if defined(__VMKLNX__)
	/* rx ring size is restricted to avoid heap issues in Jumbo and MQ case */
        if ((adapter->num_rx_queues > 1) && (adapter->netdev->mtu > ETH_DATA_LEN))
		ring->rx_max_pending = min(IXGBE_MAX_RXD / adapter->num_rx_queues,
			       		IXGBE_JUMBO_FRAME_DEFAULT_RXD);
#endif /* defined(__VMKLNX__) */
	ring->tx_max_pending = IXGBE_MAX_TXD;
	ring->rx_mini_max_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->rx_pending = rx_ring->count;
	ring->tx_pending = tx_ring->count;
	ring->rx_mini_pending = 0;
	ring->rx_jumbo_pending = 0;
}

static int ixgbe_set_ringparam(struct net_device *netdev,
                               struct ethtool_ringparam *ring)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_tx_buffer *old_buf;
	struct ixgbe_rx_buffer *old_rx_buf;
	void *old_desc;
	int i, err;
	u32 new_rx_count, new_tx_count, old_size;
	dma_addr_t old_dma;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	new_rx_count = max(ring->rx_pending, (u32)IXGBE_MIN_RXD);
	new_rx_count = min(new_rx_count, (u32)IXGBE_MAX_RXD);
	new_rx_count = ALIGN(new_rx_count, IXGBE_REQ_RX_DESCRIPTOR_MULTIPLE);

#if defined(__VMKLNX__)
	/* Adjust the rx ring size to avoid heap issues in Jumbo case */
	int count;
        if(adapter->rx_ring){
                if (adapter->num_rx_queues > 1) {
                        count = ixgbe_calculate_rx_ring_size(adapter);
			if (new_rx_count > count)
				new_rx_count = count;
                }
        }
#endif /* defined(__VMKLNX__) */

	new_tx_count = max(ring->tx_pending, (u32)IXGBE_MIN_TXD);
	new_tx_count = min(new_tx_count, (u32)IXGBE_MAX_TXD);
	new_tx_count = ALIGN(new_tx_count, IXGBE_REQ_TX_DESCRIPTOR_MULTIPLE);

	if ((new_tx_count == adapter->tx_ring->count) &&
	    (new_rx_count == adapter->rx_ring->count)) {
		/* nothing to do */
		return 0;
	}

	while (test_and_set_bit(__IXGBE_RESETTING, &adapter->state))
		msleep(1);

	if (netif_running(netdev))
		ixgbe_down(adapter);

	/*
	 * We can't just free everything and then setup again,
	 * because the ISRs in MSI-X mode get passed pointers
	 * to the tx and rx ring structs.
	 */
	if (new_tx_count != adapter->tx_ring->count) {
		for (i = 0; i < adapter->num_tx_queues; i++) {
			/* Save existing descriptor ring */
			old_buf = adapter->tx_ring[i].tx_buffer_info;
			old_desc = adapter->tx_ring[i].desc;
			old_size = adapter->tx_ring[i].size;
			old_dma = adapter->tx_ring[i].dma;
			/* Try to allocate a new one */
			adapter->tx_ring[i].tx_buffer_info = NULL;
			adapter->tx_ring[i].desc = NULL;
			adapter->tx_ring[i].count = new_tx_count;
			err = ixgbe_setup_tx_resources(adapter,
						       &adapter->tx_ring[i]);
			if (err) {
				/* Restore the old one so at least
				   the adapter still works, even if
				   we failed the request */
				adapter->tx_ring[i].tx_buffer_info = old_buf;
				adapter->tx_ring[i].desc = old_desc;
				adapter->tx_ring[i].size = old_size;
				adapter->tx_ring[i].dma = old_dma;
				goto err_setup;
			}
			/* Free the old buffer manually */
			vfree(old_buf);
			pci_free_consistent(adapter->pdev, old_size,
					    old_desc, old_dma);
		}
	}

	if (new_rx_count != adapter->rx_ring->count) {
		for (i = 0; i < adapter->num_rx_queues; i++) {

			old_rx_buf = adapter->rx_ring[i].rx_buffer_info;
			old_desc = adapter->rx_ring[i].desc;
			old_size = adapter->rx_ring[i].size;
			old_dma = adapter->rx_ring[i].dma;

			adapter->rx_ring[i].rx_buffer_info = NULL;
			adapter->rx_ring[i].desc = NULL;
			adapter->rx_ring[i].dma = 0;
			adapter->rx_ring[i].count = new_rx_count;
			err = ixgbe_setup_rx_resources(adapter,
						       &adapter->rx_ring[i]);
			if (err) {
				adapter->rx_ring[i].rx_buffer_info = old_rx_buf;
				adapter->rx_ring[i].desc = old_desc;
				adapter->rx_ring[i].size = old_size;
				adapter->rx_ring[i].dma = old_dma;
				goto err_setup;
			}

			vfree(old_rx_buf);
			pci_free_consistent(adapter->pdev, old_size, old_desc,
					    old_dma);
		}
	}

	/* success! */
	err = 0;
err_setup:
	if (netif_running(netdev))
		ixgbe_up(adapter);

	clear_bit(__IXGBE_RESETTING, &adapter->state);
	return err;
}

static int ixgbe_get_stats_count(struct net_device *netdev)
{
	return IXGBE_STATS_LEN;
}

static void ixgbe_get_ethtool_stats(struct net_device *netdev,
                                    struct ethtool_stats *stats, u64 *data)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	u64 *queue_stat;
	int stat_count = sizeof(struct ixgbe_queue_stats) / sizeof(u64);
	int j, k;
	int i;

	ixgbe_update_stats(adapter);
	for (i = 0; i < IXGBE_GLOBAL_STATS_LEN; i++) {
		char *p = (char *)adapter + ixgbe_gstrings_stats[i].stat_offset;
		data[i] = (ixgbe_gstrings_stats[i].sizeof_stat ==
		           sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}
	for (j = 0; j < adapter->num_tx_queues; j++) {
		queue_stat = (u64 *)&adapter->tx_ring[j].q_stats;
		for (k = 0; k < stat_count; k++)
			data[i + k] = queue_stat[k];
		i += k;
	}
	for (j = 0; j < adapter->num_rx_queues; j++) {
		queue_stat = (u64 *)&adapter->rx_ring[j].q_stats;
		for (k = 0; k < stat_count; k++)
			data[i + k] = queue_stat[k];
		i += k;
	}
}

static void ixgbe_get_strings(struct net_device *netdev, u32 stringset,
                              u8 *data)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, *ixgbe_gstrings_test,
		       IXGBE_TEST_LEN * ETH_GSTRING_LEN);
		break;
	case ETH_SS_STATS:
		for (i = 0; i < IXGBE_GLOBAL_STATS_LEN; i++) {
			memcpy(p, ixgbe_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < adapter->num_tx_queues; i++) {
			sprintf(p, "tx_queue_%u_packets", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "tx_queue_%u_bytes", i);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < adapter->num_rx_queues; i++) {
			sprintf(p, "rx_queue_%u_packets", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "rx_queue_%u_bytes", i);
			p += ETH_GSTRING_LEN;
		}
/*		BUG_ON(p - data != IXGBE_STATS_LEN * ETH_GSTRING_LEN); */
		break;
	}
}

static int ixgbe_link_test(struct ixgbe_adapter *adapter, u64 *data)
{
	bool link_up;
	u32 link_speed = 0;
	*data = 0;

	ixgbe_check_link(&adapter->hw, &link_speed, &link_up, true);
	if (link_up)
		return *data;
	else
		*data = 1;
	return *data;
}

/* ethtool register test data */
struct ixgbe_reg_test {
	u16 reg;
	u8  array_len;
	u8  test_type;
	u32 mask;
	u32 write;
};

/* In the hardware, registers are laid out either singly, in arrays
 * spaced 0x40 bytes apart, or in contiguous tables.  We assume
 * most tests take place on arrays or single registers (handled
 * as a single-element array) and special-case the tables.
 * Table tests are always pattern tests.
 *
 * We also make provision for some required setup steps by specifying
 * registers to be written without any read-back testing.
 */

#define PATTERN_TEST	1
#define SET_READ_TEST	2
#define WRITE_NO_TEST	3
#define TABLE32_TEST	4
#define TABLE64_TEST_LO	5
#define TABLE64_TEST_HI	6

/* default register test */
static struct ixgbe_reg_test reg_test_82598[] = {
	{ IXGBE_FCRTL(0),	1, PATTERN_TEST, 0x8007FFF0, 0x8007FFF0 },
	{ IXGBE_FCRTH(0),	1, PATTERN_TEST, 0x8007FFF0, 0x8007FFF0 },
	{ IXGBE_PFCTOP,		1, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_VLNCTRL,	1, PATTERN_TEST, 0x00000000, 0x00000000 },
	{ IXGBE_RDBAL(0),	4, PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ IXGBE_RDBAH(0),	4, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_RDLEN(0),	4, PATTERN_TEST, 0x000FFF80, 0x000FFFFF },
	/* Enable all four RX queues before testing. */
	{ IXGBE_RXDCTL(0),	4, WRITE_NO_TEST, 0, IXGBE_RXDCTL_ENABLE },
	/* RDH is read-only for 82598, only test RDT. */
	{ IXGBE_RDT(0),		4, PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ IXGBE_RXDCTL(0),	4, WRITE_NO_TEST, 0, 0 },
	{ IXGBE_FCRTH(0),	1, PATTERN_TEST, 0x8007FFF0, 0x8007FFF0 },
	{ IXGBE_FCTTV(0),	1, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_TIPG,		1, PATTERN_TEST, 0x000000FF, 0x000000FF },
	{ IXGBE_TDBAL(0),	4, PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ IXGBE_TDBAH(0),	4, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_TDLEN(0),	4, PATTERN_TEST, 0x000FFF80, 0x000FFFFF },
	{ IXGBE_RXCTRL,		1, SET_READ_TEST, 0x00000003, 0x00000003 },
	{ IXGBE_DTXCTL,		1, SET_READ_TEST, 0x00000005, 0x00000005 },
	{ IXGBE_RAL(0),		16, TABLE64_TEST_LO, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_RAL(0),		16, TABLE64_TEST_HI, 0x800CFFFF, 0x800CFFFF },
	{ IXGBE_MTA(0), 	128, TABLE32_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ 0, 0, 0, 0 }
};

#define REG_PATTERN_TEST(R, M, W)                                             \
{                                                                             \
	u32 pat, val;                                                         \
	const u32 _test[] = {0x5A5A5A5A, 0xA5A5A5A5, 0x00000000, 0xFFFFFFFF}; \
	for (pat = 0; pat < ARRAY_SIZE(_test); pat++) {                       \
		writel((_test[pat] & W), (adapter->hw.hw_addr + R));          \
		val = readl(adapter->hw.hw_addr + R);                         \
		if (val != (_test[pat] & W & M)) {                            \
			DPRINTK(DRV, ERR, "pattern test reg %04X failed: got "\
					  "0x%08X expected 0x%08X\n",         \
				R, val, (_test[pat] & W & M));                \
			*data = R;                                            \
			return 1;                                             \
		}                                                             \
	}                                                                     \
}

#define REG_SET_AND_CHECK(R, M, W)                                            \
{                                                                             \
	u32 val;                                                              \
	writel((W & M), (adapter->hw.hw_addr + R));                           \
	val = readl(adapter->hw.hw_addr + R);                                 \
	if ((W & M) != (val & M)) {                                           \
		DPRINTK(DRV, ERR, "set/check reg %04X test failed: got 0x%08X "\
				 "expected 0x%08X\n", R, (val & M), (W & M)); \
		*data = R;                                                    \
		return 1;                                                     \
	}                                                                     \
}

static int ixgbe_reg_test(struct ixgbe_adapter *adapter, u64 *data)
{
	struct ixgbe_reg_test *test;
	u32 value, before, after;
	u32 i, toggle;

	toggle = 0x7FFFF3FF;
	test = reg_test_82598;

	/*
	 * Because the status register is such a special case,
	 * we handle it separately from the rest of the register
	 * tests.  Some bits are read-only, some toggle, and some
	 * are writeable on newer MACs.
	 */
	before = IXGBE_READ_REG(&adapter->hw, IXGBE_STATUS);
	value = (IXGBE_READ_REG(&adapter->hw, IXGBE_STATUS) & toggle);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_STATUS, toggle);
	after = IXGBE_READ_REG(&adapter->hw, IXGBE_STATUS) & toggle;
	if (value != after) {
		DPRINTK(DRV, ERR, "failed STATUS register test got: "
				"0x%08X expected: 0x%08X\n", after, value);
		*data = 1;
		return 1;
	}
	/* restore previous status */
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_STATUS, before);

	/*
	 * Perform the remainder of the register test, looping through
	 * the test table until we either fail or reach the null entry.
	 */
	while (test->reg) {
		for (i = 0; i < test->array_len; i++) {
			switch (test->test_type) {
			case PATTERN_TEST:
				REG_PATTERN_TEST(test->reg + (i * 0x100),
						test->mask,
						test->write);
				break;
			case SET_READ_TEST:
				REG_SET_AND_CHECK(test->reg + (i * 0x100),
						test->mask,
						test->write);
				break;
			case WRITE_NO_TEST:
				writel(test->write,
				       (adapter->hw.hw_addr + test->reg)
				       + (i * 0x100));
				break;
			case TABLE32_TEST:
				REG_PATTERN_TEST(test->reg + (i * 4),
						test->mask,
						test->write);
				break;
			case TABLE64_TEST_LO:
				REG_PATTERN_TEST(test->reg + (i * 8),
						test->mask,
						test->write);
				break;
			case TABLE64_TEST_HI:
				REG_PATTERN_TEST((test->reg + 4) + (i * 8),
						test->mask,
						test->write);
				break;
			}
		}
		test++;
	}

	*data = 0;
	return 0;
}

static int ixgbe_eeprom_test(struct ixgbe_adapter *adapter, u64 *data)
{
	if (ixgbe_validate_eeprom_checksum(&adapter->hw, NULL))
		*data = 1;
	else
		*data = 0;
	return *data;
}

static irqreturn_t ixgbe_test_intr(int irq, void *data)
{
	struct net_device *netdev = (struct net_device *) data;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	adapter->test_icr |= IXGBE_READ_REG(&adapter->hw, IXGBE_EICR);

	return IRQ_HANDLED;
}

static int ixgbe_intr_test(struct ixgbe_adapter *adapter, u64 *data)
{
	struct net_device *netdev = adapter->netdev;
	u32 mask, i = 0, shared_int = true;
	u32 irq = adapter->pdev->irq;

	*data = 0;

	/* Hook up test interrupt handler just for this test */
	if (adapter->msix_entries) {
		/* NOTE: we don't test MSI-X interrupts here, yet */
		return 0;
	} else if (adapter->flags & IXGBE_FLAG_MSI_ENABLED) {
		shared_int = false;
		if (request_irq(irq, &ixgbe_test_intr, 0, netdev->name,
				netdev)) {
			*data = 1;
			return -1;
		}
	} else if (!request_irq(irq, &ixgbe_test_intr, IRQF_PROBE_SHARED,
	                        netdev->name, netdev)) {
		shared_int = false;
	} else if (request_irq(irq, &ixgbe_test_intr, IRQF_SHARED,
	                       netdev->name, netdev)) {
		*data = 1;
		return -1;
	}
	DPRINTK(HW, INFO, "testing %s interrupt\n",
		(shared_int ? "shared" : "unshared"));

	/* Disable all the interrupts */
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC, 0xFFFFFFFF);
	msleep(10);

	/* Test each interrupt */
	for (; i < 10; i++) {
		/* Interrupt to test */
		mask = 1 << i;

		if (!shared_int) {
			/*
			 * Disable the interrupts to be reported in
			 * the cause register and then force the same
			 * interrupt and see if one gets posted.  If
			 * an interrupt was posted to the bus, the
			 * test failed.
			 */
			adapter->test_icr = 0;
			IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC,
					~mask & 0x00007FFF);
			IXGBE_WRITE_REG(&adapter->hw, IXGBE_EICS,
					~mask & 0x00007FFF);
			msleep(10);

			if (adapter->test_icr & mask) {
				*data = 3;
				break;
			}
		}

		/*
		 * Enable the interrupt to be reported in the cause
		 * register and then force the same interrupt and see
		 * if one gets posted.  If an interrupt was not posted
		 * to the bus, the test failed.
		 */
		adapter->test_icr = 0;
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMS, mask);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EICS, mask);
		msleep(10);

		if (!(adapter->test_icr &mask)) {
			*data = 4;
			break;
		}

		if (!shared_int) {
			/*
			 * Disable the other interrupts to be reported in
			 * the cause register and then force the other
			 * interrupts and see if any get posted.  If
			 * an interrupt was posted to the bus, the
			 * test failed.
			 */
			adapter->test_icr = 0;
			IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC,
					~mask & 0x00007FFF);
			IXGBE_WRITE_REG(&adapter->hw, IXGBE_EICS,
					~mask & 0x00007FFF);
			msleep(10);

			if (adapter->test_icr) {
				*data = 5;
				break;
			}
		}
	}

	/* Disable all the interrupts */
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC, 0xFFFFFFFF);
	msleep(10);

	/* Unhook test interrupt handler */
	free_irq(irq, netdev);

	return *data;
}

static void ixgbe_free_desc_rings(struct ixgbe_adapter *adapter)
{
	struct ixgbe_ring *tx_ring = &adapter->test_tx_ring;
	struct ixgbe_ring *rx_ring = &adapter->test_rx_ring;
	struct pci_dev *pdev = adapter->pdev;
	int i;

	if (tx_ring->desc && tx_ring->tx_buffer_info) {
		for (i = 0; i < tx_ring->count; i++) {
			struct ixgbe_tx_buffer *buf =
					&(tx_ring->tx_buffer_info[i]);
			if (buf->dma)
				pci_unmap_single(pdev, buf->dma, buf->length,
						PCI_DMA_TODEVICE);
			if (buf->skb)
				dev_kfree_skb(buf->skb);
		}
	}

	if (rx_ring->desc && rx_ring->rx_buffer_info) {
		for (i = 0; i < rx_ring->count; i++) {
			struct ixgbe_rx_buffer *buf =
					&(rx_ring->rx_buffer_info[i]);
			if (buf->dma)
				pci_unmap_single(pdev, buf->dma,
						 IXGBE_RXBUFFER_2048,
						 PCI_DMA_FROMDEVICE);
			if (buf->skb)
				dev_kfree_skb(buf->skb);
		}
	}

	if (tx_ring->desc) {
		pci_free_consistent(pdev, tx_ring->size, tx_ring->desc,
				    tx_ring->dma);
		tx_ring->desc = NULL;
	}
	if (rx_ring->desc) {
		pci_free_consistent(pdev, rx_ring->size, rx_ring->desc,
				    rx_ring->dma);
		rx_ring->desc = NULL;
	}

	kfree(tx_ring->tx_buffer_info);
	tx_ring->tx_buffer_info = NULL;
	kfree(rx_ring->rx_buffer_info);
	rx_ring->rx_buffer_info = NULL;

	return;
}

static int ixgbe_setup_desc_rings(struct ixgbe_adapter *adapter)
{
	struct ixgbe_ring *tx_ring = &adapter->test_tx_ring;
	struct ixgbe_ring *rx_ring = &adapter->test_rx_ring;
	struct pci_dev *pdev = adapter->pdev;
	u32 rctl, reg_data;
	int i, ret_val;

	/* Setup Tx descriptor ring and Tx buffers */

	if (!tx_ring->count)
		tx_ring->count = IXGBE_DEFAULT_TXD;

	tx_ring->tx_buffer_info = kcalloc(tx_ring->count,
	                                  sizeof(struct ixgbe_tx_buffer),
	                                  GFP_KERNEL);
	if (!(tx_ring->tx_buffer_info)) {
		ret_val = 1;
		goto err_nomem;
	}

	tx_ring->size = tx_ring->count * sizeof(struct ixgbe_legacy_tx_desc);
	tx_ring->size = ALIGN(tx_ring->size, 4096);
	if (!(tx_ring->desc = pci_alloc_consistent(pdev, tx_ring->size,
						   &tx_ring->dma))) {
		ret_val = 2;
		goto err_nomem;
	}
	tx_ring->next_to_use = tx_ring->next_to_clean = 0;

	IXGBE_WRITE_REG(&adapter->hw, IXGBE_TDBAL(0),
			((u64) tx_ring->dma & 0x00000000FFFFFFFF));
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_TDBAH(0),
			((u64) tx_ring->dma >> 32));
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_TDLEN(0),
			tx_ring->count * sizeof(struct ixgbe_legacy_tx_desc));
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_TDH(0), 0);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_TDT(0), 0);

	reg_data = IXGBE_READ_REG(&adapter->hw, IXGBE_HLREG0);
	reg_data |= IXGBE_HLREG0_TXPADEN;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_HLREG0, reg_data);

	reg_data = IXGBE_READ_REG(&adapter->hw, IXGBE_TXDCTL(0));
	reg_data |= IXGBE_TXDCTL_ENABLE;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_TXDCTL(0), reg_data);

	for (i = 0; i < tx_ring->count; i++) {
		struct ixgbe_legacy_tx_desc *tx_desc = IXGBE_TX_DESC(*tx_ring,
								     i);
		struct sk_buff *skb;
		unsigned int size = 1024;

		skb = alloc_skb(size, GFP_KERNEL);
		if (!skb) {
			ret_val = 3;
			goto err_nomem;
		}
		skb_put(skb, size);
		tx_ring->tx_buffer_info[i].skb = skb;
		tx_ring->tx_buffer_info[i].length = skb->len;
		tx_ring->tx_buffer_info[i].dma =
			pci_map_single(pdev, skb->data, skb->len,
					PCI_DMA_TODEVICE);
		tx_desc->buffer_addr =
				cpu_to_le64(tx_ring->tx_buffer_info[i].dma);
		tx_desc->lower.data = cpu_to_le32(skb->len);
		tx_desc->lower.data |= cpu_to_le32(IXGBE_TXD_CMD_EOP |
						   IXGBE_TXD_CMD_IFCS |
						   IXGBE_TXD_CMD_RS);
		tx_desc->upper.data = 0;
	}

	/* Setup Rx Descriptor ring and Rx buffers */

	if (!rx_ring->count)
		rx_ring->count = IXGBE_DEFAULT_RXD;

	rx_ring->rx_buffer_info = kcalloc(rx_ring->count,
	                                  sizeof(struct ixgbe_rx_buffer),
	                                  GFP_KERNEL);
	if (!(rx_ring->rx_buffer_info)) {
		ret_val = 4;
		goto err_nomem;
	}

	rx_ring->size = rx_ring->count * sizeof(struct ixgbe_legacy_rx_desc);
	if (!(rx_ring->desc = pci_alloc_consistent(pdev, rx_ring->size,
						   &rx_ring->dma))) {
		ret_val = 5;
		goto err_nomem;
	}
	rx_ring->next_to_use = rx_ring->next_to_clean = 0;

	rctl = IXGBE_READ_REG(&adapter->hw, IXGBE_RXCTRL);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_RXCTRL, rctl & ~IXGBE_RXCTRL_RXEN);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_RDBAL(0),
			((u64)rx_ring->dma & 0xFFFFFFFF));
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_RDBAH(0),
			((u64) rx_ring->dma >> 32));
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_RDLEN(0), rx_ring->size);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_RDH(0), 0);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_RDT(0), 0);

	reg_data = IXGBE_READ_REG(&adapter->hw, IXGBE_FCTRL);
	reg_data |= IXGBE_FCTRL_BAM | IXGBE_FCTRL_SBP | IXGBE_FCTRL_MPE;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_FCTRL, reg_data);

	reg_data = IXGBE_READ_REG(&adapter->hw, IXGBE_HLREG0);
	reg_data &= ~IXGBE_HLREG0_LPBK;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_HLREG0, reg_data);

	reg_data = IXGBE_READ_REG(&adapter->hw, IXGBE_RDRXCTL);
#define IXGBE_RDRXCTL_RDMTS_MASK    0x00000003 /* Receive Descriptor Minimum
                                                  Threshold Size mask */
	reg_data &= ~IXGBE_RDRXCTL_RDMTS_MASK;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_RDRXCTL, reg_data);

	reg_data = IXGBE_READ_REG(&adapter->hw, IXGBE_MCSTCTRL);
#define IXGBE_MCSTCTRL_MO_MASK      0x00000003 /* Multicast Offset mask */
	reg_data &= ~IXGBE_MCSTCTRL_MO_MASK;
	reg_data |= adapter->hw.mac.mc_filter_type;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_MCSTCTRL, reg_data);

	reg_data = IXGBE_READ_REG(&adapter->hw, IXGBE_RXDCTL(0));
	reg_data |= IXGBE_RXDCTL_ENABLE;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_RXDCTL(0), reg_data);

	rctl |= IXGBE_RXCTRL_RXEN | IXGBE_RXCTRL_DMBYPS;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_RXCTRL, rctl);

	for (i = 0; i < rx_ring->count; i++) {
		struct ixgbe_legacy_rx_desc *rx_desc =
					IXGBE_RX_DESC(*rx_ring, i);
		struct sk_buff *skb;

		skb = alloc_skb(IXGBE_RXBUFFER_2048 + NET_IP_ALIGN, GFP_KERNEL);
		if (!skb) {
			ret_val = 6;
			goto err_nomem;
		}
		skb_reserve(skb, NET_IP_ALIGN);
		rx_ring->rx_buffer_info[i].skb = skb;
		rx_ring->rx_buffer_info[i].dma =
			pci_map_single(pdev, skb->data, IXGBE_RXBUFFER_2048,
				       PCI_DMA_FROMDEVICE);
		rx_desc->buffer_addr =
				cpu_to_le64(rx_ring->rx_buffer_info[i].dma);
		memset(skb->data, 0x00, skb->len);
	}

	return 0;

err_nomem:
	ixgbe_free_desc_rings(adapter);
	return ret_val;
}

static int ixgbe_setup_loopback_test(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 reg_data;

	/* right now we only support MAC loopback in the driver */

	/* Setup MAC loopback */
	reg_data = IXGBE_READ_REG(&adapter->hw, IXGBE_HLREG0);
	reg_data |= IXGBE_HLREG0_LPBK;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_HLREG0, reg_data);

	reg_data = IXGBE_READ_REG(&adapter->hw, IXGBE_AUTOC);
	reg_data &= ~IXGBE_AUTOC_LMS_MASK;
	reg_data |= IXGBE_AUTOC_LMS_10G_LINK_NO_AN | IXGBE_AUTOC_FLU;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_AUTOC, reg_data);

	/* Disable Atlas Tx lanes; re-enabled in reset path */
	if (hw->mac.type == ixgbe_mac_82598EB) {
		u8 atlas;

		ixgbe_read_analog_reg8(&adapter->hw,
				       IXGBE_ATLAS_PDN_LPBK, &atlas);
		atlas |= IXGBE_ATLAS_PDN_TX_REG_EN;
		ixgbe_write_analog_reg8(&adapter->hw,
					IXGBE_ATLAS_PDN_LPBK, atlas);

		ixgbe_read_analog_reg8(&adapter->hw,
				       IXGBE_ATLAS_PDN_10G, &atlas);
		atlas |= IXGBE_ATLAS_PDN_TX_10G_QL_ALL;
		ixgbe_write_analog_reg8(&adapter->hw,
					IXGBE_ATLAS_PDN_10G, atlas);

		ixgbe_read_analog_reg8(&adapter->hw,
				       IXGBE_ATLAS_PDN_1G, &atlas);
		atlas |= IXGBE_ATLAS_PDN_TX_1G_QL_ALL;
		ixgbe_write_analog_reg8(&adapter->hw,
					IXGBE_ATLAS_PDN_1G, atlas);

		ixgbe_read_analog_reg8(&adapter->hw,
				       IXGBE_ATLAS_PDN_AN, &atlas);
		atlas |= IXGBE_ATLAS_PDN_TX_AN_QL_ALL;
		ixgbe_write_analog_reg8(&adapter->hw,
					IXGBE_ATLAS_PDN_AN, atlas);
	}

	return 0;
}

static void ixgbe_loopback_cleanup(struct ixgbe_adapter *adapter)
{
	u32 reg_data;

	reg_data = IXGBE_READ_REG(&adapter->hw, IXGBE_HLREG0);
	reg_data &= ~IXGBE_HLREG0_LPBK;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_HLREG0, reg_data);
}

static void ixgbe_create_lbtest_frame(struct sk_buff *skb,
				      unsigned int frame_size)
{
	memset(skb->data, 0xFF, frame_size);
	frame_size &= ~1;
	memset(&skb->data[frame_size / 2], 0xAA, frame_size / 2 - 1);
	memset(&skb->data[frame_size / 2 + 10], 0xBE, 1);
	memset(&skb->data[frame_size / 2 + 12], 0xAF, 1);
}

static int ixgbe_check_lbtest_frame(struct sk_buff *skb,
				    unsigned int frame_size)
{
	frame_size &= ~1;
	if (*(skb->data + 3) == 0xFF) {
		if ((*(skb->data + frame_size / 2 + 10) == 0xBE) &&
		    (*(skb->data + frame_size / 2 + 12) == 0xAF)) {
			return 0;
		}
	}
	return 13;
}

static int ixgbe_run_loopback_test(struct ixgbe_adapter *adapter)
{
	struct ixgbe_ring *tx_ring = &adapter->test_tx_ring;
	struct ixgbe_ring *rx_ring = &adapter->test_rx_ring;
	struct pci_dev *pdev = adapter->pdev;
	int i, j, k, l, lc, good_cnt, ret_val = 0;
	unsigned long time;

	IXGBE_WRITE_REG(&adapter->hw, IXGBE_RDT(0), rx_ring->count - 1);

	/*
	 * Calculate the loop count based on the largest descriptor ring
	 * The idea is to wrap the largest ring a number of times using 64
	 * send/receive pairs during each loop
	 */

	if (rx_ring->count <= tx_ring->count)
		lc = ((tx_ring->count / 64) * 2) + 1;
	else
		lc = ((rx_ring->count / 64) * 2) + 1;

	k = l = 0;
	for (j = 0; j <= lc; j++) {
		for (i = 0; i < 64; i++) {
			ixgbe_create_lbtest_frame(
					tx_ring->tx_buffer_info[k].skb,
					1024);
			pci_dma_sync_single_for_device(pdev,
				tx_ring->tx_buffer_info[k].dma,
				tx_ring->tx_buffer_info[k].length,
				PCI_DMA_TODEVICE);
			if (unlikely(++k == tx_ring->count))
				k = 0;
		}
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_TDT(0), k);
		msleep(200);
		/* set the start time for the receive */
		time = jiffies;
		good_cnt = 0;
		do {
			/* receive the sent packets */
			pci_dma_sync_single_for_cpu(pdev,
					rx_ring->rx_buffer_info[l].dma,
					IXGBE_RXBUFFER_2048,
					PCI_DMA_FROMDEVICE);
			ret_val = ixgbe_check_lbtest_frame(
					rx_ring->rx_buffer_info[l].skb, 1024);
			if (!ret_val)
				good_cnt++;
			if (++l == rx_ring->count)
				l = 0;
			/*
			 * time + 20 msecs (200 msecs on 2.4) is more than
			 * enough time to complete the receives, if it's
			 * exceeded, break and error off
			 */
		} while (good_cnt < 64 && jiffies < (time + 20));
		if (good_cnt != 64) {
			/* ret_val is the same as mis-compare */
			ret_val = 13;
			break;
		}
		if (jiffies >= (time + 20)) {
			/* Error code for time out error */
			ret_val = 14;
			break;
		}
	}

	return ret_val;
}

static int ixgbe_loopback_test(struct ixgbe_adapter *adapter, u64 *data)
{
	*data = ixgbe_setup_desc_rings(adapter);
	if (*data)
		goto out;
	*data = ixgbe_setup_loopback_test(adapter);
	if (*data)
		goto err_loopback;
	*data = ixgbe_run_loopback_test(adapter);
	ixgbe_loopback_cleanup(adapter);

err_loopback:
	ixgbe_free_desc_rings(adapter);
out:
	return *data;
}

static int ixgbe_diag_test_count(struct net_device *netdev)
{
	return IXGBE_TEST_LEN;
}

static void ixgbe_diag_test(struct net_device *netdev,
			    struct ethtool_test *eth_test, u64 *data)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	bool if_running = netif_running(netdev);

	set_bit(__IXGBE_TESTING, &adapter->state);
	if (eth_test->flags == ETH_TEST_FL_OFFLINE) {
		/* Offline tests */

		DPRINTK(HW, INFO, "offline testing starting\n");

		/* Link test performed before hardware reset so autoneg doesn't
		 * interfere with test result */
		if (ixgbe_link_test(adapter, &data[4]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		if (if_running)
			/* indicate we're in test mode */
			dev_close(netdev);
		else
			ixgbe_reset(adapter);

		if (ixgbe_reg_test(adapter, &data[0]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		ixgbe_reset(adapter);
		if (ixgbe_eeprom_test(adapter, &data[1]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		ixgbe_reset(adapter);
		if (ixgbe_intr_test(adapter, &data[2]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		ixgbe_reset(adapter);
		if (ixgbe_loopback_test(adapter, &data[3]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		ixgbe_reset(adapter);

		clear_bit(__IXGBE_TESTING, &adapter->state);
		if (if_running)
			dev_open(netdev);
	} else {
		DPRINTK(HW, INFO, "online testing starting\n");
		/* Online tests */
		if (ixgbe_link_test(adapter, &data[4]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* Online tests aren't run; pass by default */
		data[0] = 0;
		data[1] = 0;
		data[2] = 0;
		data[3] = 0;

		clear_bit(__IXGBE_TESTING, &adapter->state);
	}
	msleep_interruptible(4 * 1000);
}

static void ixgbe_get_wol(struct net_device *netdev,
			  struct ethtool_wolinfo *wol)
{
	wol->supported = 0;
	wol->wolopts = 0;

	return;
}

static int ixgbe_nway_reset(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if (netif_running(netdev))
		ixgbe_reinit_locked(adapter);

	return 0;
}

static int ixgbe_phys_id(struct net_device *netdev, u32 data)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	u32 led_reg = IXGBE_READ_REG(&adapter->hw, IXGBE_LEDCTL);
	u32 i;

	if (!data || data > 300)
		data = 300;

	for (i = 0; i < (data * 1000); i += 400) {
		ixgbe_led_on(&adapter->hw, IXGBE_LED_ON);
		msleep_interruptible(200);
		ixgbe_led_off(&adapter->hw, IXGBE_LED_ON);
		msleep_interruptible(200);
	}

	/* Restore LED settings */
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_LEDCTL, led_reg);

	return IXGBE_SUCCESS;
}

static int ixgbe_get_coalesce(struct net_device *netdev,
                              struct ethtool_coalesce *ec)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	ec->tx_max_coalesced_frames_irq = adapter->tx_ring[0].work_limit;
#ifndef CONFIG_IXGBE_NAPI
	ec->rx_max_coalesced_frames_irq = adapter->rx_ring[0].work_limit;
#endif

	/* only valid if in constant ITR mode */
	if (adapter->itr_setting == 0)
		ec->rx_coalesce_usecs = 1000000/adapter->eitr_param;

	return 0;
}

static int ixgbe_set_coalesce(struct net_device *netdev,
                              struct ethtool_coalesce *ec)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if (ec->tx_max_coalesced_frames_irq)
		adapter->tx_ring[0].work_limit = ec->tx_max_coalesced_frames_irq;

#ifndef CONFIG_IXGBE_NAPI
	if (ec->rx_max_coalesced_frames_irq)
		adapter->rx_ring[0].work_limit = ec->rx_max_coalesced_frames_irq;
#endif

	/* if in constant ITR mode, change the param, AN reset req'd */
	if (ec->rx_coalesce_usecs && (adapter->itr_setting == 0)) {
		struct ixgbe_hw *hw = &adapter->hw;
		/* store the value in ints/second */
		adapter->eitr_param = 1000000/ec->rx_coalesce_usecs;
		IXGBE_WRITE_REG(hw, IXGBE_EITR(0),
		            EITR_INTS_PER_SEC_TO_REG(adapter->eitr_param));
	}

	/* if some error return -EINVAL */
	return 0;
}


static struct ethtool_ops ixgbe_ethtool_ops = {
	.get_settings           = ixgbe_get_settings,
	.set_settings           = ixgbe_set_settings,
	.get_drvinfo            = ixgbe_get_drvinfo,
	.get_regs_len           = ixgbe_get_regs_len,
	.get_regs               = ixgbe_get_regs,
	.get_wol                = ixgbe_get_wol,
	.nway_reset             = ixgbe_nway_reset,
	.get_link               = ethtool_op_get_link,
	.get_eeprom_len         = ixgbe_get_eeprom_len,
	.get_eeprom             = ixgbe_get_eeprom,
	.set_eeprom             = ixgbe_set_eeprom,
	.get_ringparam          = ixgbe_get_ringparam,
	.set_ringparam          = ixgbe_set_ringparam,
	.get_pauseparam         = ixgbe_get_pauseparam,
	.set_pauseparam         = ixgbe_set_pauseparam,
	.get_rx_csum            = ixgbe_get_rx_csum,
	.set_rx_csum            = ixgbe_set_rx_csum,
	.get_tx_csum            = ixgbe_get_tx_csum,
	.set_tx_csum            = ixgbe_set_tx_csum,
	.get_sg                 = ethtool_op_get_sg,
	.set_sg                 = ethtool_op_set_sg,
	.get_msglevel           = ixgbe_get_msglevel,
	.set_msglevel           = ixgbe_set_msglevel,
#ifdef NETIF_F_TSO
	.get_tso                = ethtool_op_get_tso,
	.set_tso                = ixgbe_set_tso,
#endif
	.self_test_count        = ixgbe_diag_test_count,
	.self_test              = ixgbe_diag_test,
	.get_strings            = ixgbe_get_strings,
	.phys_id                = ixgbe_phys_id,
	.get_stats_count        = ixgbe_get_stats_count,
	.get_ethtool_stats      = ixgbe_get_ethtool_stats,
#ifdef ETHTOOL_GPERMADDR
	.get_perm_addr          = ethtool_op_get_perm_addr,
#endif
	.get_coalesce           = ixgbe_get_coalesce,
	.set_coalesce           = ixgbe_set_coalesce,
};

void ixgbe_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &ixgbe_ethtool_ops);
}
#endif /* SIOCETHTOOL */
