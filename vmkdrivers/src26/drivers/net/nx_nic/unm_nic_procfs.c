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
#include <linux/netdevice.h>
#include <linux/proc_fs.h>

#include "nx_errorcode.h"
#include "nxplat.h"
#include "nxhal_nic_interface.h"
#include "nxhal_nic_api.h"

#include "unm_inc.h"
#include "unm_brdcfg.h"
#include "unm_nic.h"

#include "nx_nic_linux_tnic_api.h"
#include "nxhal.h"

#define SNMP_ETH_FILENAME       "snmp_eth"
#ifdef UNM_NIC_SNMP
int 
unm_nic_snmp_ether_read_proc(char *buf, char **start, off_t offset, 
					int count, int *eof, void *data);
#endif
static int unm_nic_port_read_proc(char *buf, char **start, off_t offset,
					int count, int *eof, void *data);
int unm_init_proc_drv_dir(void);
void unm_cleanup_proc_drv_entries(void);
void unm_init_proc_entries(struct unm_adapter_s *adapter);
void unm_cleanup_proc_entries(struct unm_adapter_s *adapter);
extern int unm_read_blink_state(char *buf, char **start, off_t offset,
					int count, int *eof, void *data);
extern int unm_write_blink_state(struct file *file, const char *buffer,
					unsigned long count, void *data);
extern int unm_read_blink_rate(char *buf, char **start, off_t offset,
					int count, int *eof, void *data);
extern int unm_write_blink_rate(struct file *file, const char *buffer,
					unsigned long count, void *data);
int nx_read_lro_state(char *buf, char **start, off_t offset, int count,
				int *eof, void *data); 
int nx_write_lro_state(struct file *file, const char *buffer,
		unsigned long count, void *data); 
int nx_read_lro_stats(char *buf, char **start, off_t offset, int count,
		      int *eof, void *data);
int nx_write_lro_stats(struct file *file, const char *buffer,
		       unsigned long count, void *data);


/*Contains all the procfs related fucntions here */
static struct proc_dir_entry *unm_proc_dir_entry;

/*
 * Gets the proc file directory where the procfs files are created.
 *
 * Parameters:
 *	None
 *
 * Returns:
 *	NULL - If the file system is not created.
 *	The directory that was created.
 */
struct proc_dir_entry *nx_nic_get_base_procfs_dir(void)
{
	return (unm_proc_dir_entry);
}
int unm_init_proc_drv_dir(void) {

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
	unm_proc_dir_entry = proc_mkdir(DRIVER_NAME, init_net.proc_net);
#else
	unm_proc_dir_entry = proc_mkdir(DRIVER_NAME, proc_net);
#endif
	if(!unm_proc_dir_entry) {
		printk(KERN_WARNING "%s: Unable to create /proc/net/%s",
		       DRIVER_NAME, DRIVER_NAME);
		return -ENOMEM;
	}
	unm_proc_dir_entry->owner = THIS_MODULE;
	return 0;
}
void unm_cleanup_proc_drv_entries(void) {

	if (unm_proc_dir_entry != NULL) {

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
		remove_proc_entry(unm_proc_dir_entry->name, init_net.proc_net);
#else
		remove_proc_entry(unm_proc_dir_entry->name, proc_net);
#endif
		unm_proc_dir_entry = NULL;
	}
}
void unm_init_proc_entries(struct unm_adapter_s *adapter) {
	struct net_device *netdev = adapter->netdev;
	struct proc_dir_entry *stats_file, *state_file, *rate_file = NULL;
	struct proc_dir_entry *lro_file = NULL;
	struct proc_dir_entry *lro_stats_file = NULL;

	adapter->dev_dir = proc_mkdir(adapter->procname, unm_proc_dir_entry);
	stats_file = create_proc_entry("stats", S_IRUGO, adapter->dev_dir);
       	state_file = create_proc_entry("led_blink_state", S_IRUGO|S_IWUSR, adapter->dev_dir);
	rate_file = create_proc_entry("led_blink_rate", S_IRUGO|S_IWUSR, adapter->dev_dir);	
	lro_file = create_proc_entry("lro_enabled", S_IRUGO|S_IWUSR, adapter->dev_dir);	
	lro_stats_file = create_proc_entry("lro_stats", S_IRUGO|S_IWUSR, adapter->dev_dir);

	if (stats_file) {
		stats_file->data = netdev;
		stats_file->owner = THIS_MODULE;
		stats_file->read_proc = unm_nic_port_read_proc;
	}
	if (state_file) {
		state_file->data = netdev;
		state_file->owner = THIS_MODULE;
		state_file->read_proc = unm_read_blink_state;
		state_file->write_proc = unm_write_blink_state;
	}
	if (rate_file) {
		rate_file->data = netdev;
		rate_file->owner = THIS_MODULE;
		rate_file->read_proc = unm_read_blink_rate;
		rate_file->write_proc = unm_write_blink_rate;
	}
	if (lro_file) {
		lro_file->data = netdev;
		lro_file->owner = THIS_MODULE;
		lro_file->read_proc = nx_read_lro_state;
		lro_file->write_proc = nx_write_lro_state;
	}
	if (lro_stats_file) {
		lro_stats_file->data = netdev;
		lro_stats_file->owner = THIS_MODULE;
		lro_stats_file->read_proc = nx_read_lro_stats;
		lro_stats_file->write_proc = nx_write_lro_stats;
	}

#ifdef UNM_NIC_SNMP
	{
		struct proc_dir_entry *snmp_proc = NULL;
		snmp_proc = create_proc_entry(SNMP_ETH_FILENAME, S_IRUGO, adapter->dev_dir);

		if (snmp_proc) {
			snmp_proc->data = netdev;
			snmp_proc->owner = THIS_MODULE;
			snmp_proc->read_proc = unm_nic_snmp_ether_read_proc;
		}
	}
#endif
}
void unm_cleanup_proc_entries(struct unm_adapter_s *adapter) {

	if (strlen(adapter->procname) > 0) {
#ifdef UNM_NIC_SNMP
		remove_proc_entry(SNMP_ETH_FILENAME, adapter->dev_dir);
#endif
		remove_proc_entry("stats", adapter->dev_dir);
		remove_proc_entry("led_blink_state", adapter->dev_dir);
		remove_proc_entry("led_blink_rate", adapter->dev_dir);
		remove_proc_entry("lro_enabled", adapter->dev_dir);
		remove_proc_entry("lro_stats", adapter->dev_dir);
		remove_proc_entry(adapter->procname, unm_proc_dir_entry);
	}

}

static int
unm_nic_port_read_proc(char *buf, char **start, off_t offset, int count,
		       int *eof, void *data)
{
	struct net_device *netdev = (struct net_device *)data;
	int j;
	int len = 0;
	struct unm_adapter_s *adapter = (struct unm_adapter_s *)netdev->priv;
	 rds_host_ring_t *host_rds_ring = NULL;
	 sds_host_ring_t         *host_sds_ring = NULL;
	 nx_host_sds_ring_t      *nxhal_sds_ring = NULL;

	if (netdev == NULL) {
		len = sprintf(buf, "No Statistics available now. Device is"
			      " NULL\n");
		*eof = 1;
		return len;
	}
	len = sprintf(buf + len, "%s NIC port statistics\n",
		      unm_nic_driver_name);
	len += sprintf(buf + len, "\n");
	len += sprintf(buf + len, "Interface Name           : %s\n",
		       netdev->name);
	len += sprintf(buf + len, "Port Number              : %d\n",
		       adapter->portnum);
	len += sprintf(buf + len, "Bad SKB                  : %lld\n",
		       adapter->stats.rcvdbadskb);
	len += sprintf(buf + len, "Xmit called              : %lld\n",
		       adapter->stats.xmitcalled);
	len += sprintf(buf + len, "Xmited Frames            : %lld\n",
		       adapter->stats.xmitedframes);
	len += sprintf(buf + len, "Bad SKB length           : %lld\n",
		       adapter->stats.badskblen);
	len += sprintf(buf + len, "Cmd Desc Error           : %lld\n",
		       adapter->stats.nocmddescriptor);
	len += sprintf(buf + len, "Polled for Rcv           : %lld\n",
		       adapter->stats.polled);
	len += sprintf(buf + len, "Received Desc            : %lld\n",
		       adapter->stats.no_rcv);
	len += sprintf(buf + len, "Rcv to stack             : %lld\n",
		       adapter->stats.uphappy);
	len += sprintf(buf + len, "Stack dropped            : %lld\n",
		       adapter->stats.updropped);
	len += sprintf(buf + len, "Low congestion           : %lld\n",
		       adapter->stats.uplcong);
	len += sprintf(buf + len, "High congestion          : %lld\n",
		       adapter->stats.uphcong);
	len += sprintf(buf + len, "Medium congestion        : %lld\n",
		       adapter->stats.upmcong);
	len += sprintf(buf + len, "Rcv bad return           : %lld\n",
		       adapter->stats.updunno);
	len += sprintf(buf + len, "SKBs Freed               : %lld\n",
		       adapter->stats.skbfreed);
	len += sprintf(buf + len, "Xmit finished            : %lld\n",
		       adapter->stats.xmitfinished);
	len += sprintf(buf + len, "Tx dropped SKBs          : %lld\n",
		       adapter->stats.txdropped);
	len += sprintf(buf + len, "Tx got NULL SKBs         : %lld\n",
		       adapter->stats.txnullskb);
	len += sprintf(buf + len, "Rcv of CSUMed SKB        : %lld\n",
		       adapter->stats.csummed);
	len += sprintf(buf + len, "\n");
	len += sprintf(buf + len, "Ring Statistics\n");
	len += sprintf(buf + len, "Command Producer    : %d\n",
		       adapter->cmdProducer);
	len += sprintf(buf + len, "LastCommand Consumer: %d\n",
		       adapter->lastCmdConsumer);
	if(adapter->is_up == ADAPTER_UP_MAGIC) {
		for (j = 0; j < NUM_RCV_DESC_RINGS; j++) {
			host_rds_ring =
			    (rds_host_ring_t *) adapter->nx_dev->rx_ctxs[0]->
		    				rds_rings[j].os_data;
			len += sprintf(buf + len, "Rcv Ring %d\n", j);
			len += sprintf(buf + len, "\tReceive Producer [%d]:"
					" %d\n", j,host_rds_ring->producer);
		}
		for (j = 0; j < adapter->nx_dev->rx_ctxs[0]->
						num_sds_rings; j++) {
                	nxhal_sds_ring  = &adapter->nx_dev->rx_ctxs[0]->
							sds_rings[j];
	                host_sds_ring   = (sds_host_ring_t *)
						nxhal_sds_ring->os_data;
        	        len += sprintf(buf+len, "Rx Status Producer[%d]: %d\n",
                	               j, host_sds_ring->producer);
	                len += sprintf(buf+len, "Rx Status Consumer[%d]: %d\n",
        	                       j, host_sds_ring->consumer);
                	len += sprintf(buf+len, "Rx Status Polled[%d]: %llu\n",
                        	       j, host_sds_ring->polled);
		}
	} else {
		for (j = 0; j < NUM_RCV_DESC_RINGS; j++) {
			len += sprintf(buf + len, "Rcv Ring %d\n", j);	
			len += sprintf(buf + len, "\tReceive Producer [%d]: "
					"0\n", j);
		}
	}
	len += sprintf(buf + len, "\n");
	if (adapter->link_width < 8) {
		len += sprintf(buf + len, "PCIE Negotiated Link width : x%d\n",
			       adapter->link_width);
	} else {
		len += sprintf(buf + len, "PCIE Negotiated Link width : x%d\n",
			       adapter->link_width);
	}

	*eof = 1;
	return len;
}
EXPORT_SYMBOL(nx_nic_get_base_procfs_dir);
