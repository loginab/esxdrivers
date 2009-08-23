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

/*
 *
 * VMware ESX compatibility layer
 *
 */


/* disable features that VMware ESX does not support */
//#undef CONFIG_PM
//#undef NETIF_F_TSO
#undef NETIF_F_TSO6
#undef CONFIG_IXGBE_PACKET_SPLIT

#undef netdev_alloc_skb
#define netdev_alloc_skb _esx_netdev_alloc_skb
static inline struct sk_buff *_esx_netdev_alloc_skb(struct net_device *dev,
                                                    unsigned int length)
{
	struct sk_buff *skb = alloc_skb(length, GFP_ATOMIC);
	if (skb)
		/* don't reserve NET_SKB_PAD */
		skb->dev = dev;
	return skb;
}

#define skb_trim _kc_skb_trim
static inline void _kc_skb_trim(struct sk_buff *skb, unsigned int len)
{
	if (skb->len > len) {
		if (unlikely(skb->data_len)) {
			WARN_ON(1);
			return;
		}
		skb->len = len;
		skb->tail = skb->data + len;
	}
}
/* disable pskb_trim usage for now - should break lots of stuff */
#define pskb_trim(a,b)

