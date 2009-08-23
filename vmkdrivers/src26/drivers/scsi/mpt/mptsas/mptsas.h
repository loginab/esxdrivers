/*
 *  linux/drivers/message/fusion/mptsas.h
 *      High performance SCSI + LAN / Fibre Channel device drivers.
 *      For use with PCI chip/adapter(s):
 *          LSIFC9xx/LSI409xx Fibre Channel
 *      running LSI MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 1999-2007 LSI Corporation
 *  (mailto:DL-MPTFusionLinux@lsi.com)
 *
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    NO WARRANTY
    THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
    LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
    solely responsible for determining the appropriateness of using and
    distributing the Program and assumes all risks associated with its
    exercise of rights under this Agreement, including but not limited to
    the risks and costs of program errors, damage to or loss of data,
    programs or equipment, and unavailability or interruption of operations.

    DISCLAIMER OF LIABILITY
    NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
    TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
    USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
    HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef MPTSAS_H_INCLUDED
#define MPTSAS_H_INCLUDED
/*{-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

struct mptsas_target_reset_event {
	struct list_head 	list;
	EVENT_DATA_SAS_DEVICE_STATUS_CHANGE sas_event_data;
	u8	target_reset_issued;
	unsigned long	 time_count;
};

enum mptsas_hotplug_action {
	MPTSAS_ADD_DEVICE,
	MPTSAS_DEL_DEVICE,
	MPTSAS_ADD_RAID,
	MPTSAS_DEL_RAID,
	MPTSAS_ADD_INACTIVE_VOLUME,
	MPTSAS_ADD_PHYSDISK,
	MPTSAS_ADD_PHYSDISK_REPROBE,
	MPTSAS_DEL_PHYSDISK,
	MPTSAS_DEL_PHYSDISK_REPROBE,
	MPTSAS_IGNORE_EVENT,
};

#if defined(CPQ_CIM)
struct sas_mapping{
	u8			id;
	u8			channel;
};

struct sas_device_info {
	struct list_head 	list;
	struct sas_mapping	os;	/* operating system mapping*/
	struct sas_mapping	fw;	/* firmware mapping */
	u64			sas_address; 
	u32			device_info; /* specific bits for devices */
	u16			slot;		/* enclosure slot id */
	u64			enclosure_logical_id; /*enclosure address */
	u8			is_logical_volume; /* is this logical volume */
	u8			is_cached;	/* cached data for a removed device */
};
#endif

struct mptsas_hotplug_event {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	struct delayed_work	 hotplug_work;
#else
	struct work_struct	 hotplug_work;
#endif
	MPT_ADAPTER		*ioc;
	enum mptsas_hotplug_action event_type;
	u64			sas_address;
	u8			channel;
	u8			id;
	u32			device_info;
	u16			handle;
	u8			phy_id;
	u8			phys_disk_num;		/* hrc - unique index*/
	u8			retries;
	struct scsi_device	*sdev;
};

struct mptsas_discovery_event {
	struct work_struct	work;
	MPT_ADAPTER		*ioc;
};

struct mptsas_link_status_event {
	struct work_struct	work;
	MpiEventDataSasPhyLinkStatus_t link_data;
	MPT_ADAPTER		*ioc;
};

/*
 * SAS topology structures
 *
 * The MPT Fusion firmware interface spreads information about the
 * SAS topology over many manufacture pages, thus we need some data
 * structure to collect it and process it for the SAS transport class.
 */

struct mptsas_devinfo {
	u16	handle;		/* unique id to address this device */
	u16	handle_parent;	/* unique id to address parent device */
	u16	handle_enclosure; /* enclosure identifier of the enclosure */
	u16	slot;		/* physical slot in enclosure */
	u8	phy_id;		/* phy number of parent device */
	u8	port_id;	/* sas physical port this device
				   is assoc'd with */
	u8	id;		/* logical target id of this device */
	u32	phys_disk_num;	/* phys disk id, for csmi-ioctls */
	u8	channel;	/* logical bus number of this device */
	u64	sas_address;    /* WWN of this device,
				   SATA is assigned by HBA,expander */
	u32	device_info;	/* bitfield detailed info about this device */
#if !defined(MPT_WIDE_PORT_API)
	u8	wide_port_enable;	/* when set, this is part of wide port*/
#endif
};

/*
 * Specific details on ports, wide/narrow
 */
struct mptsas_portinfo_details{
#if !defined(MPT_WIDE_PORT_API)
	u8	port_id; 	/* port number provided to transport */
	u8	rphy_id; 	/* phy index used for reporting end device*/
	u32	device_info;	/* bitfield detailed info about this device */
#endif
	u16	num_phys;	/* number of phys beloing to this port */
	u64	phy_bitmask; 	/* this needs extending to support 128 phys */
	struct sas_rphy *rphy; /* rphy for end devices */
#if defined(MPT_WIDE_PORT_API)
	struct sas_port *port;	/* transport layer port object */
#endif
	struct scsi_target *starget;
	struct mptsas_portinfo *port_info;
};

struct mptsas_phyinfo {
	u16	handle;			/* handle for this phy */
	u8	phy_id; 		/* phy index */
	u8	port_id; 		/* port number this phy is part of */
	u8	negotiated_link_rate;	/* nego'd link rate for this phy */
	u8	hw_link_rate; 		/* hardware max/min phys link rate */
	u8	programmed_link_rate;	/* programmed max/min phy link rate */
#if defined(MPT_WIDE_PORT_API)
	u8	sas_port_add_phy;	/* flag to request sas_port_add_phy*/
#endif
#if defined(CPQ_CIM)
	u8	change_count;		/* change count of the phy */
	u8	port_flags;		/* info wrt host sas ports */
#endif
	u32	phy_info;		/* various info wrt the phy */
	struct mptsas_devinfo identify;	/* point to phy device info */
	struct mptsas_devinfo attached;	/* point to attached device info */
	struct sas_phy *phy;
	struct mptsas_portinfo *portinfo;
	struct mptsas_portinfo_details * port_details;
};

struct mptsas_portinfo {
	struct list_head list;
	u16	num_phys;		/* number of phys */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	struct delayed_work	del_work;	/* work for deleting expanders */
	struct delayed_work	add_work;	/* work for adding expanders */
#else
	struct work_struct	del_work;
	struct work_struct	add_work;
#endif
	void	*ioc;			/* ioc ptr */
	u8	del_work_scheduled;
	struct mptsas_phyinfo *phy_info;
};

struct mptsas_enclosure {
	u64	enclosure_logical_id;	/* The WWN for the enclosure */
	u16	enclosure_handle;	/* unique id to address this */
	u16	flags;			/* details enclosure management */
	u16	num_slot;		/* num slots */
	u16	start_slot;		/* first slot */
	u8	start_id;		/* starting logical target id */
	u8	start_channel;		/* starting logical channel id */
	u8	sep_id;			/* SEP device logical target id */
	u8	sep_channel;		/* SEP channel logical channel id */
};

struct mptsas_broadcast_primative_event {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	struct delayed_work	aen_work;
#else
	struct work_struct	aen_work;
#endif
	MPT_ADAPTER		*ioc;
};

/*}-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#endif

