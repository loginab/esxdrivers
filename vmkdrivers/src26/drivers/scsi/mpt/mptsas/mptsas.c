/*
 *  linux/drivers/message/fusion/mptsas.c
 *      For use with LSI PCI chip/adapter(s)
 *      running LSI Fusion MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 1999-2007 LSI Corporation
 *  (mailto:DL-MPTFusionLinux@lsi.com)
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
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#include "linux_compat.h"       /* linux-2.6 tweaks */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/delay.h>	/* for mdelay */
#include <linux/pci.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_sas.h>
#include <scsi/scsi_dbg.h>

#include "linux_compat.h"	/* linux-2.6 tweaks */
#include "mptbase.h"
#include "mptscsih.h"

/* The glue to get a single driver working in both
 * SLES10 and RHEL5 environments
 */
#if (defined(CONFIG_SUSE_KERNEL) && defined(scsi_is_sas_phy_local)) || LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
#define MPT_WIDE_PORT_API	1
#define MPT_WIDE_PORT_API_PLUS	1
#endif

#include "mptsas.h"

#define my_NAME		"Fusion MPT SAS Host driver"
#define my_VERSION	MPT_LINUX_VERSION_COMMON
#define MYNAM		"mptsas"

/*
 * Reserved channel for integrated raid
 */
#if defined(MPT_WIDE_PORT_API)
#define MPTSAS_RAID_CHANNEL	1
#else
#define MPTSAS_RAID_CHANNEL	8
#endif

#define SAS_CONFIG_PAGE_TIMEOUT		30

MODULE_AUTHOR(MODULEAUTHOR);
MODULE_DESCRIPTION(my_NAME);
MODULE_LICENSE("GPL");
MODULE_VERSION(my_VERSION);

static int mpt_pt_clear;
module_param(mpt_pt_clear, int, 0);
MODULE_PARM_DESC(mpt_pt_clear,
		" Clear persistency table: enable=1  "
		"(default=MPTSCSIH_PT_CLEAR=0)");

static int mpt_cmd_retry_count = 144;
module_param(mpt_cmd_retry_count, int, 0);
MODULE_PARM_DESC(mpt_cmd_retry_count,
		" Device discovery TUR command retry count: default=144");

static int mpt_disable_hotplug_remove = 0;
module_param(mpt_disable_hotplug_remove, int, 0);
MODULE_PARM_DESC(mpt_disable_hotplug_remove,
		" Disable hotpug remove events: default=0");

static int mpt_sdev_queue_depth = MPT_SCSI_CMD_PER_DEV_HIGH;
#if defined(__VMKLNX__)
/* if it is a static function, gcc complains "defined but not used */
int mptsas_set_sdev_queue_depth(const char *val,
    struct kernel_param *kp);
static struct mptsas_phyinfo *
mptsas_find_phyinfo_by_sas_address_NL(MPT_ADAPTER *ioc, u64 sas_address);
static void
mptsas_del_end_device(MPT_ADAPTER *ioc, struct mptsas_phyinfo *phy_info);
#else /* !defined(__VMKLNX__) */
static int mptsas_set_sdev_queue_depth(const char *val,
    struct kernel_param *kp);
#endif /* defined(__VMKLNX__) */
module_param_call(mpt_sdev_queue_depth, mptsas_set_sdev_queue_depth,
    param_get_int, &mpt_sdev_queue_depth, 0600);
MODULE_PARM_DESC(mpt_sdev_queue_depth,
    " Max Device Queue Depth (default="
    __MODULE_STRING(MPT_SCSI_CMD_PER_DEV_HIGH) ")");

/* scsi-mid layer global parmeter is max_report_luns, which is 511 */
#define MPTSAS_MAX_LUN (16895)
static int max_lun = MPTSAS_MAX_LUN;
module_param(max_lun, int, 0);
MODULE_PARM_DESC(max_lun, " max lun, default=16895 ");

static u8	mptsasDoneCtx = MPT_MAX_PROTOCOL_DRIVERS;
static u8	mptsasTaskCtx = MPT_MAX_PROTOCOL_DRIVERS;
static u8	mptsasInternalCtx = MPT_MAX_PROTOCOL_DRIVERS; /* Used only for internal commands */
static u8	mptsasMgmtCtx = MPT_MAX_PROTOCOL_DRIVERS;
static u8	mptsasDeviceResetCtx = MPT_MAX_PROTOCOL_DRIVERS;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
static void mptsas_hotplug_work(struct work_struct *work);
#else
static void mptsas_hotplug_work(void *arg);
#endif

#if defined(CPQ_CIM)
static struct mptsas_phyinfo * mptsas_find_phyinfo_by_sas_address(MPT_ADAPTER *ioc,
	u64 sas_address);
static int mptsas_sas_device_pg0(MPT_ADAPTER *ioc, struct mptsas_devinfo *device_info,
	u32 form, u32 form_specific);
static int mptsas_sas_enclosure_pg0(MPT_ADAPTER *ioc, struct mptsas_enclosure *enclosure,
		u32 form, u32 form_specific);
#endif

static int mptsas_add_end_device(MPT_ADAPTER *ioc, struct mptsas_phyinfo *phy_info);

#if defined(__VMKLNX__)
int mptsas_get_vpd83_info(MPT_ADAPTER *ioc, u8 channel, u8 id,
        SCSI_DEVICE_IDENTIFICATION_PAGE *buf)
{
   int             rc = -1;
   u32             vpd83_data_len;
   dma_addr_t      vpd83_data_dma;
   MPT_SCSI_HOST   *hd;
   INTERNAL_CMD    *iocmd = NULL;
   SCSI_DEVICE_IDENTIFICATION_PAGE *vpd83_data = NULL;

   if (!ioc || !buf) {
      return (-EINVAL);
   }

   hd = shost_priv(ioc->sh);
   vpd83_data_len = sizeof(SCSI_DEVICE_IDENTIFICATION_PAGE);
   vpd83_data = pci_alloc_consistent(ioc->pcidev, vpd83_data_len,
                                     &vpd83_data_dma);
   if (!vpd83_data) {
      printk(MYIOC_s_ERR_FMT "%s: pci_alloc_consistent(%d) FAILED!\n",
             ioc->name, __FUNCTION__, vpd83_data_len);
      rc = -ENOMEM;
      goto out;
   }

   iocmd = kzalloc(sizeof(INTERNAL_CMD), GFP_KERNEL);
   if (!iocmd) {
      printk(MYIOC_s_ERR_FMT "%s: kzalloc(%zd) FAILED!\n",
             ioc->name, __FUNCTION__, sizeof(INTERNAL_CMD));
      rc = -ENOMEM;
      goto out;
   }

   /* VPD83 INQUIRY */
   iocmd->cmd = INQUIRY;
   iocmd->data_dma = vpd83_data_dma;
   iocmd->data = (char *)vpd83_data;
   iocmd->size = vpd83_data_len;
   iocmd->channel = channel;
   iocmd->id = id;
   iocmd->flags = 0x83; /* Page Code 0x83 */
   if ((rc = mptscsih_do_cmd(hd, iocmd))) {
      printk(MYIOC_s_ERR_FMT "%s: fw_channel=%d fw_id=%d: "
             "VPD83 inquiry failed due to rc=0x%x\n", ioc->name,
             __FUNCTION__, channel, id, rc);
      goto out;
   }
   memcpy((void *)buf, (const void *)vpd83_data, sizeof(*buf));
   buf->PageHeader.PageLength = __be16_to_cpu(vpd83_data->PageHeader.PageLength);

 out:
   if (iocmd) {
      kfree(iocmd);
   }
   if (vpd83_data) {
      pci_free_consistent(ioc->pcidev, vpd83_data_len, vpd83_data,
                          vpd83_data_dma);
   }
   return rc;
}

/**
 *      mptsas_get_target_sas_identifier -
 *      @starget: a pointer to the scsi target
 *      @sas_addr: a pointer to the sas_address to be filled in by the function
 *
 *      Desc:
 *          This is a function for upper layer to retrieve the target SAS
 *          identifier. vmkctl requires a non-zero sas target identifier for
 *          each sas target to do state persistence; see PR328334 for details.
 *          For a sas end device target, the sas rphy address is reported.
 *          For a SAS RAID volume, driver obtains the SAS RAID uid from firmware
 *          during mptsas_target_alloc and caches it in the virtual target. 
 *          This function gets SAS RAID uid from virtual target.
 *
 **/
static int
mptsas_get_target_sas_identifier(struct scsi_target *starget, u64 *sas_id)
{
   if (!starget || !sas_id || !starget->hostdata) {
      return FAILED;
   }
   if (MPTSAS_RAID_CHANNEL == starget->channel) {
      /* SAS RAID */
      VirtTarget *vtarget = (VirtTarget *)starget->hostdata;
      if (!vtarget->sas_uid) {
         return FAILED;
      }
      *sas_id = vtarget->sas_uid;

   } else {
      /* SAS End Device */
      struct sas_rphy    *rphy = (struct sas_rphy *)starget->dev.parent;
      if (!rphy) {
         return FAILED;
      }
      *sas_id = rphy->identify.sas_address;

   }
   return SUCCESS;
}
#endif

/**
 *	mptsas_set_sdev_queue_depth - global setting of the mpt_sdev_queue_depth
 *	found via /sys/module/mptsas/parameters/mpt_sdev_queue_depth
 *	@val:
 *	@kp:
 *
 *	Returns
 **/
#if defined(__VMKLNX__)
int
#else /* !defined(__VMKLNX__) */
static int
#endif /* defined(__VMKLNX__) */
mptsas_set_sdev_queue_depth(const char *val, struct kernel_param *kp)
{
#if defined(__VMKLNX__)
        /* param_set_int is not defined; see PR 258107 */
	int ret = 0;
#else /* !defined(__VMKLNX__) */
	int ret = param_set_int(val, kp);
#endif /* defined(__VMKLNX__) */
	MPT_ADAPTER *ioc;
	struct scsi_device 	*sdev;

	if (ret)
		return ret;

	list_for_each_entry(ioc, &ioc_list, list) {
		if (ioc->bus_type != SAS)
			continue;
		shost_for_each_device(sdev, ioc->sh)
			mptscsih_change_queue_depth(sdev, mpt_sdev_queue_depth);
		ioc->sdev_queue_depth = mpt_sdev_queue_depth;
	}
	return 0;
}

static void mptsas_print_phy_data(MPT_ADAPTER *ioc,
					MPI_SAS_IO_UNIT0_PHY_DATA *phy_data)
{
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "---- IO UNIT PAGE 0 ------------\n", ioc->name));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Handle=0x%X\n",
	    ioc->name, le16_to_cpu(phy_data->AttachedDeviceHandle)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Controller Handle=0x%X\n",
	    ioc->name, le16_to_cpu(phy_data->ControllerDevHandle)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Port=0x%X\n",
	    ioc->name, phy_data->Port));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Port Flags=0x%X\n",
	    ioc->name, phy_data->PortFlags));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "PHY Flags=0x%X\n",
	    ioc->name, phy_data->PhyFlags));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Negotiated Link Rate=0x%X\n",
	    ioc->name, phy_data->NegotiatedLinkRate));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "Controller PHY Device Info=0x%X\n", ioc->name,
	    le32_to_cpu(phy_data->ControllerPhyDeviceInfo)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "DiscoveryStatus=0x%X\n\n",
	    ioc->name, le32_to_cpu(phy_data->DiscoveryStatus)));
}

static void mptsas_print_phy_pg0(MPT_ADAPTER *ioc, SasPhyPage0_t *pg0)
{
	__le64 sas_address;

	memcpy(&sas_address, &pg0->SASAddress, sizeof(__le64));

	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "---- SAS PHY PAGE 0 ------------\n", ioc->name));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "Attached Device Handle=0x%X\n", ioc->name,
	    le16_to_cpu(pg0->AttachedDevHandle)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "SAS Address=0x%llX\n",
	    ioc->name, (unsigned long long)le64_to_cpu(sas_address)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "Attached PHY Identifier=0x%X\n", ioc->name,
	    pg0->AttachedPhyIdentifier));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Attached Device Info=0x%X\n",
	    ioc->name, le32_to_cpu(pg0->AttachedDeviceInfo)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Programmed Link Rate=0x%X\n",
	    ioc->name,  pg0->ProgrammedLinkRate));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Change Count=0x%X\n",
	    ioc->name, pg0->ChangeCount));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "PHY Info=0x%X\n\n",
	    ioc->name, le32_to_cpu(pg0->PhyInfo)));
}

static void mptsas_print_phy_pg1(MPT_ADAPTER *ioc, SasPhyPage1_t *pg1)
{
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "---- SAS PHY PAGE 1 ------------\n", ioc->name));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Invalid Dword Count=0x%x\n",
	    ioc->name,  pg1->InvalidDwordCount));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "Running Disparity Error Count=0x%x\n", ioc->name,
	    pg1->RunningDisparityErrorCount));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "Loss Dword Synch Count=0x%x\n", ioc->name,
	    pg1->LossDwordSynchCount));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "PHY Reset Problem Count=0x%x\n\n", ioc->name,
	    pg1->PhyResetProblemCount));
}

static void mptsas_print_device_pg0(MPT_ADAPTER *ioc, SasDevicePage0_t *pg0)
{
	__le64 sas_address;

	memcpy(&sas_address, &pg0->SASAddress, sizeof(__le64));

	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "---- SAS DEVICE PAGE 0 ---------\n", ioc->name));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Handle=0x%X\n",
	    ioc->name, le16_to_cpu(pg0->DevHandle)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Parent Handle=0x%X\n",
	    ioc->name, le16_to_cpu(pg0->ParentDevHandle)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Enclosure Handle=0x%X\n",
	    ioc->name, le16_to_cpu(pg0->EnclosureHandle)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Slot=0x%X\n",
	    ioc->name, le16_to_cpu(pg0->Slot)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "SAS Address=0x%llX\n",
	    ioc->name, (unsigned long long)le64_to_cpu(sas_address)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Target ID=0x%X\n",
	    ioc->name, pg0->TargetID));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Bus=0x%X\n",
	    ioc->name, pg0->Bus));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Parent Phy Num=0x%X\n",
	    ioc->name, pg0->PhyNum));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Access Status=0x%X\n",
	    ioc->name, le16_to_cpu(pg0->AccessStatus)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Device Info=0x%X\n",
	    ioc->name, le32_to_cpu(pg0->DeviceInfo)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Flags=0x%X\n",
	    ioc->name, le16_to_cpu(pg0->Flags)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Physical Port=0x%X\n\n",
	    ioc->name, pg0->PhysicalPort));
}

static void mptsas_print_expander_pg1(MPT_ADAPTER *ioc, SasExpanderPage1_t *pg1)
{
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "---- SAS EXPANDER PAGE 1 ------------\n", ioc->name));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Physical Port=0x%X\n",
	    ioc->name, pg1->PhysicalPort));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "PHY Identifier=0x%X\n",
	    ioc->name, pg1->PhyIdentifier));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Negotiated Link Rate=0x%X\n",
	    ioc->name, pg1->NegotiatedLinkRate));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Programmed Link Rate=0x%X\n",
	    ioc->name, pg1->ProgrammedLinkRate));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Hardware Link Rate=0x%X\n",
	    ioc->name, pg1->HwLinkRate));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Owner Device Handle=0x%X\n",
	    ioc->name, le16_to_cpu(pg1->OwnerDevHandle)));
	dsasprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "Attached Device Handle=0x%X\n\n", ioc->name,
	    le16_to_cpu(pg1->AttachedDevHandle)));
}

/**
 *	phy_to_ioc -
 *	@phy:
 *
 *
 **/
static inline MPT_ADAPTER *
phy_to_ioc(struct sas_phy *phy)
{
	struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
	return ((MPT_SCSI_HOST *)shost->hostdata)->ioc;
}

/**
 *	rphy_to_ioc -
 *	@rphy:
 *
 *
 **/
static inline MPT_ADAPTER *
rphy_to_ioc(struct sas_rphy *rphy)
{
	struct Scsi_Host *shost = dev_to_shost(rphy->dev.parent->parent);
	return ((MPT_SCSI_HOST *)shost->hostdata)->ioc;
}

/**
 *	mptsas_find_portinfo_by_handle -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@handle:
 *
 *	This function should be called with the sas_topology_mutex already held
 *
 **/
static struct mptsas_portinfo *
mptsas_find_portinfo_by_handle(MPT_ADAPTER *ioc, u16 handle)
{
	struct mptsas_portinfo *port_info, *rc=NULL;
	int i;

	list_for_each_entry(port_info, &ioc->sas_topology, list)
		for (i = 0; i < port_info->num_phys; i++)
			if (port_info->phy_info[i].identify.handle == handle) {
				rc = port_info;
				goto out;
			}
 out:
	return rc;
}

/**
 *	mptsas_find_portinfo_by_sas_address -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@handle:
 *
 *	This function should be called with the sas_topology_mutex already held
 *
 **/
static struct mptsas_portinfo *
mptsas_find_portinfo_by_sas_address(MPT_ADAPTER *ioc, u64 sas_address)
{
	struct mptsas_portinfo *port_info, *rc=NULL;
	int i;

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(port_info, &ioc->sas_topology, list)
		for (i = 0; i < port_info->num_phys; i++)
			if (port_info->phy_info[i].identify.sas_address ==
			    sas_address) {
				rc = port_info;
				goto out;
			}
 out:
	mutex_unlock(&ioc->sas_topology_mutex);
	return rc;
}

/**
 *	mptsas_is_end_device -
 *	@attached:
 *
 *	Returns true if there is a scsi end device
 **/
static inline int
mptsas_is_end_device(struct mptsas_devinfo * attached)
{
	if ((attached->sas_address) &&
	    (attached->device_info &
	    MPI_SAS_DEVICE_INFO_END_DEVICE) &&
	    ((attached->device_info &
	    MPI_SAS_DEVICE_INFO_SSP_TARGET) |
	    (attached->device_info &
	    MPI_SAS_DEVICE_INFO_STP_TARGET) |
	    (attached->device_info &
	    MPI_SAS_DEVICE_INFO_SATA_DEVICE)))
		return 1;
	else
		return 0;
}

/**
 *	mptsas_get_rphy -
 *	@phy_info:
 *
 **/
static inline struct sas_rphy *
mptsas_get_rphy(struct mptsas_phyinfo *phy_info)
{
	if (phy_info->port_details)
		return phy_info->port_details->rphy;
	else
		return NULL;
}

/**
 *	mptsas_set_rphy -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@phy_info:
 *	@rphy:
 *
 **/
static inline void
mptsas_set_rphy(MPT_ADAPTER *ioc, struct mptsas_phyinfo *phy_info, struct sas_rphy *rphy)
{
	if (phy_info->port_details) {
		phy_info->port_details->rphy = rphy;
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "sas_rphy_add: rphy=%p\n", ioc->name, rphy));
	}

	if (rphy) {
		dsaswideprintk(ioc, dev_printk(KERN_DEBUG,
		    &rphy->dev, MYIOC_s_FMT "add:", ioc->name));
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT "rphy=%p release=%p\n",
		    ioc->name, rphy, rphy->dev.release));
	}
}

/**
 *	mptsas_port_delete -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@port_details:
 *
 *	(no mutex)
 *
 **/
static void
mptsas_port_delete(MPT_ADAPTER *ioc, struct mptsas_portinfo_details * port_details)
{
	struct mptsas_portinfo *port_info;
	struct mptsas_phyinfo *phy_info;
	u8	i;

	if (!port_details)
		return;

	port_info = port_details->port_info;
	phy_info = port_info->phy_info;

#if defined(MPT_WIDE_PORT_API)
	dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: [%p]: num_phys=%02d "
	    "bitmask=0x%016llX\n", ioc->name, __FUNCTION__, port_details,
	    port_details->num_phys, (unsigned long long)
	    port_details->phy_bitmask));
#else
	dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: [%p]: port=%02d num_phys=%02d "
	    "rphy=%02d bitmask=0x%016llX\n", ioc->name, __FUNCTION__, port_details,
	    port_details->port_id,  port_details->num_phys,
	    port_details->rphy_id, (unsigned long long)
	    port_details->phy_bitmask));
#endif

	for (i = 0; i < port_info->num_phys; i++, phy_info++) {
		if(phy_info->port_details != port_details)
			continue;
		memset(&phy_info->attached, 0, sizeof(struct mptsas_devinfo));
		mptsas_set_rphy(ioc, phy_info, NULL);
		phy_info->port_details = NULL;
	}
	kfree(port_details);
}

#if !defined(MPT_WIDE_PORT_API)
/**
 *	mptsas_get_rphy_id -
 *	@phy_info:
 *
 **/
static inline u8
mptsas_get_rphy_id(struct mptsas_phyinfo *phy_info)
{
	if (phy_info->port_details)
		return phy_info->port_details->rphy_id;
	else
		return 0xFF;
}
#endif

#if defined(MPT_WIDE_PORT_API)
/**
 *	mptsas_get_port -
 *	@phy_info:
 *
 **/
static inline struct sas_port *
mptsas_get_port(struct mptsas_phyinfo *phy_info)
{
	if (phy_info->port_details)
		return phy_info->port_details->port;
	else
		return NULL;
}

/**
 *	mptsas_set_port -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@phy_info:
 *	@port:
 *
 **/
static inline void
mptsas_set_port(MPT_ADAPTER *ioc, struct mptsas_phyinfo *phy_info, struct sas_port *port)
{
	if (phy_info->port_details)
		phy_info->port_details->port = port;

	if (port) {
		dsaswideprintk(ioc, dev_printk(KERN_DEBUG,
		    &port->dev, MYIOC_s_FMT "add:", ioc->name));
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT "port=%p release=%p\n",
		    ioc->name, port, port->dev.release));
	}
}
#endif

/**
 *	mptsas_get_starget -
 *	@phy_info:
 *
 **/
static inline struct scsi_target *
mptsas_get_starget(struct mptsas_phyinfo *phy_info)
{
	if (phy_info->port_details)
		return phy_info->port_details->starget;
	else
		return NULL;
}

/**
 *	mptsas_set_starget -
 *	@phy_info:
 *	@starget:
 *
 **/
static inline void
mptsas_set_starget(struct mptsas_phyinfo *phy_info, struct scsi_target *
starget)
{
	if (phy_info->port_details)
		phy_info->port_details->starget = starget;
}

#if defined(CPQ_CIM)
/**
 *	mptsas_add_device_component -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@channel: fw mapped id's
 *	@id:
 *	@sas_address:
 *	@device_info:
 *
 **/
static void
mptsas_add_device_component(MPT_ADAPTER *ioc, u8 channel, u8 id,
	u64 sas_address, u32 device_info, u16 slot, u64 enclosure_logical_id)
{
	struct sas_device_info	*sas_info, *next;
	struct scsi_device 	*sdev;
	struct scsi_target	*starget;
	struct sas_rphy		*rphy;

	down(&ioc->sas_device_info_mutex);

	/*
	 * Delete all matching sas_address's out of tree
	 */
	list_for_each_entry_safe(sas_info, next, &ioc->sas_device_info_list, list) {
		if (sas_info->sas_address != sas_address)
			continue;
		list_del(&sas_info->list);
		kfree(sas_info);
	}

	/*
	 * If there is a matching channel/id, then swap out with new target info
	 */
	list_for_each_entry(sas_info, &ioc->sas_device_info_list, list) {
		if (sas_info->fw.channel == channel && sas_info->fw.id == id)
			goto initialize_data;
	}

	if (!(sas_info = kzalloc(sizeof(*sas_info), GFP_KERNEL)))
		goto out;

	/*
	 * Set Firmware mapping
	 */
	sas_info->fw.id = id;
	sas_info->fw.channel = channel;
	list_add_tail(&sas_info->list, &ioc->sas_device_info_list);

 initialize_data:

	sas_info->sas_address = sas_address;
	sas_info->device_info = device_info;
	sas_info->slot = slot;
	sas_info->enclosure_logical_id = enclosure_logical_id;
	sas_info->is_cached = 0;
	sas_info->is_logical_volume = 0;

	/*
	 * Set OS mapping
	 */
	shost_for_each_device(sdev, ioc->sh) {
		starget = scsi_target(sdev);
		rphy = dev_to_rphy(starget->dev.parent);
		if (rphy->identify.sas_address == sas_address) {
			sas_info->os.id = starget->id;
			sas_info->os.channel = starget->channel;
		}
	}

 out:
	up(&ioc->sas_device_info_mutex);
	return;
}

/**
 *	mptsas_add_device_component_by_fw -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@channel:  fw mapped id's
 *	@id:
 *
 **/
static void
mptsas_add_device_component_by_fw(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	struct mptsas_devinfo sas_device;
	struct mptsas_enclosure enclosure_info;
	int rc;

	rc = mptsas_sas_device_pg0(ioc, &sas_device,
	    (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
	     MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
	    (channel << 8) + id);
	if (rc)
		return;

	memset(&enclosure_info, 0, sizeof(struct mptsas_enclosure));
	mptsas_sas_enclosure_pg0(ioc, &enclosure_info,
	    (MPI_SAS_ENCLOS_PGAD_FORM_HANDLE <<
	     MPI_SAS_ENCLOS_PGAD_FORM_SHIFT),
	     sas_device.handle_enclosure);

	mptsas_add_device_component(ioc, sas_device.channel,
	    sas_device.id, sas_device.sas_address, sas_device.device_info,
	    sas_device.slot, enclosure_info.enclosure_logical_id);
}

/**
 *	mptsas_add_device_component_starget_ir - Handle Integrated RAID, adding
 *	each individual device to list
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@channel: fw mapped id's
 *	@id:
 *
 **/
static void
mptsas_add_device_component_starget_ir(MPT_ADAPTER *ioc, struct scsi_target *starget)
{
	CONFIGPARMS			cfg;
	ConfigPageHeader_t		hdr;
	dma_addr_t			dma_handle;
	pRaidVolumePage0_t		buffer = NULL;
	int				i;
	RaidPhysDiskPage0_t 		phys_disk;
	struct sas_device_info		*sas_info;

	memset(&cfg, 0 , sizeof(CONFIGPARMS));
	memset(&hdr, 0 , sizeof(ConfigPageHeader_t));
	hdr.PageType = MPI_CONFIG_PAGETYPE_RAID_VOLUME;
	cfg.pageAddr = starget->id; /* assumption that all volumes on channel = 0 */
	cfg.cfghdr.hdr = &hdr;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	if (mpt_config(ioc, &cfg) != 0)
		goto out;

	if (!hdr.PageLength)
		goto out;

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.PageLength * 4,
	    &dma_handle);

	if (!buffer)
		goto out;

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if (mpt_config(ioc, &cfg) != 0)
		goto out;

	if (!buffer->NumPhysDisks)
		goto out;

	/*
	 * Adding entry for hidden components
	 */
	for (i = 0; i < buffer->NumPhysDisks; i++) {

		if(mpt_raid_phys_disk_pg0(ioc,
		    buffer->PhysDisk[i].PhysDiskNum, &phys_disk) != 0)
			continue;

		mptsas_add_device_component_by_fw(ioc, phys_disk.PhysDiskBus,
		    phys_disk.PhysDiskID);
	}

	/*
	 * Adding entry for logical volume in list
	 */
	list_for_each_entry(sas_info, &ioc->sas_device_info_list, list) {
		if (sas_info->fw.channel == 0 && sas_info->fw.id ==  starget->id)
			goto initialize_data;
	}

	if (!(sas_info = kzalloc(sizeof(*sas_info), GFP_KERNEL)))
		goto out;

	sas_info->fw.id = starget->id;
	sas_info->fw.channel = 0; /* channel zero */
	down(&ioc->sas_device_info_mutex);
	list_add_tail(&sas_info->list, &ioc->sas_device_info_list);
	up(&ioc->sas_device_info_mutex);

 initialize_data:

	sas_info->os.id = starget->id;
	sas_info->os.channel = starget->channel;
	sas_info->sas_address = 0;
	sas_info->device_info = 0;
	sas_info->slot = 0;
	sas_info->enclosure_logical_id = 0;
	sas_info->is_logical_volume = 1;
	sas_info->is_cached = 0;

 out:
	if (buffer)
		pci_free_consistent(ioc->pcidev, hdr.PageLength * 4, buffer,
		    dma_handle);
}

/**
 *	mptsas_add_device_component_starget -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@starget:
 *
 **/
static void
mptsas_add_device_component_starget(MPT_ADAPTER *ioc, struct scsi_target *starget)
{
	VirtTarget		*vtarget;
	struct sas_rphy		*rphy;
	struct mptsas_phyinfo *phy_info = NULL;
	struct mptsas_enclosure enclosure_info;

	rphy = dev_to_rphy(starget->dev.parent);
	vtarget = starget->hostdata;
	phy_info = mptsas_find_phyinfo_by_sas_address(ioc,
		    rphy->identify.sas_address);
	if (!phy_info)
		return;

	memset(&enclosure_info, 0, sizeof(struct mptsas_enclosure));
	mptsas_sas_enclosure_pg0(ioc, &enclosure_info,
	    (MPI_SAS_ENCLOS_PGAD_FORM_HANDLE <<
	     MPI_SAS_ENCLOS_PGAD_FORM_SHIFT),
	     phy_info->attached.handle_enclosure);

	mptsas_add_device_component(ioc, phy_info->attached.channel,
	    phy_info->attached.id, phy_info->attached.sas_address,
	    phy_info->attached.device_info,
	    phy_info->attached.slot, enclosure_info.enclosure_logical_id);
}

/**
 *	mptsas_del_device_component_by_os - Once a device has been removed, we
 *	mark the entry in the list as being cached
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@channel: os mapped id's
 *	@id:
 *
 **/
static void
mptsas_del_device_component_by_os(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	struct sas_device_info	*sas_info, *next;

	/*
	 * Set is_cached flag
	 */
	list_for_each_entry_safe(sas_info, next, &ioc->sas_device_info_list, list) {
		if (sas_info->os.channel == channel && sas_info->os.id == id)
			sas_info->is_cached = 1;
	}
}

/**
 *	mptsas_del_device_components - Cleaning the list
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 **/
static void
mptsas_del_device_components(MPT_ADAPTER *ioc)
{
	struct sas_device_info	*sas_info, *next;

	down(&ioc->sas_device_info_mutex);
	list_for_each_entry_safe(sas_info, next, &ioc->sas_device_info_list, list) {
		list_del(&sas_info->list);
		kfree(sas_info);
	}
	up(&ioc->sas_device_info_mutex);
}
#endif

/**
 *	mptsas_setup_wide_ports - Updates for new and existing narrow/wide port
 *	configuration
 *	in the sas_topology
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@port_info:
 *
 */
static void
mptsas_setup_wide_ports(MPT_ADAPTER *ioc, struct mptsas_portinfo *port_info)
{
	struct mptsas_portinfo_details * port_details;
	struct mptsas_phyinfo *phy_info, *phy_info_cmp;
	u64	sas_address;
#if !defined(MPT_WIDE_PORT_API)
	u8	found_wide_port;
#endif
	int	i, j;

	mutex_lock(&ioc->sas_topology_mutex);

	phy_info = port_info->phy_info;
	for (i = 0 ; i < port_info->num_phys ; i++, phy_info++) {
		if (phy_info->attached.handle)
			continue;
		port_details = phy_info->port_details;
		if (!port_details)
			continue;
		if (port_details->num_phys < 2)
			continue;

		/*
		 * Removing a phy from a port, letting the last
		 * phy be removed by firmware events.
		 */
#if defined(MPT_WIDE_PORT_API)
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			"%s: [%p]: deleting phy = %d\n",
			ioc->name, __FUNCTION__, port_details, i));
#else
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			"%s: [%p]: port=%d deleting phy = %d\n",
			ioc->name, __FUNCTION__, port_details,
			port_details->port_id, i));
#endif
		port_details->num_phys--;
		port_details->phy_bitmask &= ~ (1 << phy_info->phy_id);
		memset(&phy_info->attached, 0, sizeof(struct mptsas_devinfo));
#if defined(MPT_WIDE_PORT_API)
		devtprintk(ioc, dev_printk(KERN_DEBUG, &phy_info->phy->dev,
		    MYIOC_s_FMT "delete phy %d, phy-obj (0x%p)\n", ioc->name,
		     phy_info->phy_id, phy_info->phy));
		sas_port_delete_phy(port_details->port, phy_info->phy);
		phy_info->phy = NULL;
#endif
		phy_info->port_details = NULL;
	}

	/*
	 * Populate and refresh the tree
	 */
	phy_info = port_info->phy_info;
	for (i = 0 ; i < port_info->num_phys ; i++, phy_info++) {
		sas_address = phy_info->attached.sas_address;
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT "phy_id=%d sas_address=0x%018llX\n",
		    ioc->name, i, (unsigned long long)sas_address));
		if (!sas_address)
			continue;
		port_details = phy_info->port_details;
		/*
		 * Forming a port
		 */
		if (!port_details) {
			port_details = kzalloc(sizeof(*port_details),
				GFP_KERNEL);
			if (!port_details)
				goto out;
			port_details->num_phys = 1;
			port_details->port_info = port_info;
#if !defined(MPT_WIDE_PORT_API)
			port_details->port_id = phy_info->port_id;
			port_details->rphy_id = i;
			port_details->device_info = phy_info->attached.device_info;
#endif
			if (phy_info->phy_id < 64 )
				port_details->phy_bitmask |=
				    (1 << phy_info->phy_id);
#if defined(MPT_WIDE_PORT_API)
			phy_info->sas_port_add_phy=1;
#endif
			dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT "\t\tForming port\n\t\t"
			    "phy_id=%d sas_address=0x%018llX\n", ioc->name, i,
			    (unsigned long long) sas_address));
			phy_info->port_details = port_details;
		}

		if (i == port_info->num_phys - 1)
			continue;
		phy_info_cmp = &port_info->phy_info[i + 1];
#if !defined(MPT_WIDE_PORT_API)
		found_wide_port = 0;
#endif
		for (j = i + 1 ; j < port_info->num_phys ; j++,
		    phy_info_cmp++) {
			if (!phy_info_cmp->attached.sas_address)
				continue;
			if (sas_address != phy_info_cmp->attached.sas_address)
				continue;
#if !defined(MPT_WIDE_PORT_API)
			found_wide_port = 1;
#endif
			if (phy_info_cmp->port_details == port_details )
				continue;
			dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			    "\t\tphy_id=%d sas_address=0x%018llX\n",
			    ioc->name, j, (unsigned long long)
			    phy_info_cmp->attached.sas_address));
			if (phy_info_cmp->port_details) {
				port_details->rphy =
				    mptsas_get_rphy(phy_info_cmp);
#if defined(MPT_WIDE_PORT_API)
				port_details->port =
				    mptsas_get_port(phy_info_cmp);
#endif
				port_details->starget =
				    mptsas_get_starget(phy_info_cmp);
#if !defined(MPT_WIDE_PORT_API)
				port_details->port_id =
					phy_info_cmp->port_details->port_id;
				port_details->rphy_id =
					phy_info_cmp->port_details->rphy_id;
#endif
				port_details->num_phys =
					phy_info_cmp->port_details->num_phys;
				if (!phy_info_cmp->port_details->num_phys)
					kfree(phy_info_cmp->port_details);
#if defined(MPT_WIDE_PORT_API)
			} else
				phy_info_cmp->sas_port_add_phy=1;
#else
			}
#endif
			/*
			 * Adding a phy to a port
			 */
			phy_info_cmp->port_details = port_details;
			if (phy_info_cmp->phy_id < 64 )
				port_details->phy_bitmask |=
				(1 << phy_info_cmp->phy_id);
			port_details->num_phys++;
#if !defined(MPT_WIDE_PORT_API)
			phy_info_cmp->attached.wide_port_enable = 1;
#endif
		}
#if !defined(MPT_WIDE_PORT_API)
		phy_info->attached.wide_port_enable = (found_wide_port) ? 1:0;
#endif
	}

 out:

	for (i = 0; i < port_info->num_phys; i++) {
		port_details = port_info->phy_info[i].port_details;
		if (!port_details)
			continue;
#if defined(MPT_WIDE_PORT_API)
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: [%p]: phy_id=%02d num_phys=%02d "
		    "bitmask=0x%016llX\n", ioc->name, __FUNCTION__,
		    port_details, i, port_details->num_phys,
		    (unsigned long long)port_details->phy_bitmask));
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT "\t\tport = %p rphy=%p\n",
		    ioc->name, port_details->port, port_details->rphy));
#else
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			"%s: [%p]: phy=%02d port=%02d num_phys=%02d "
			"rphy=%02d bitmask=0x%016llX\n",
			ioc->name, __FUNCTION__,
			port_details, i, port_details->port_id,
			port_details->num_phys, port_details->rphy_id,
			(unsigned long long)port_details->phy_bitmask));
		dsaswideprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "\t\trphy=%p\n", ioc->name, port_details->rphy));
#endif
	}
	dsaswideprintk(ioc, printk("\n"));
	mutex_unlock(&ioc->sas_topology_mutex);
}

/**
 *	csmisas_find_vtarget -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@volume_id:
 *	@volume_bus:
 *
 **/
static VirtTarget *
mptsas_find_vtarget(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	struct scsi_device 		*sdev;
	VirtDevice			*vdevice;
	VirtTarget 			*vtarget = NULL;

	shost_for_each_device(sdev, ioc->sh) {
		if ((vdevice = sdev->hostdata) == NULL ||
			(vdevice->vtarget == NULL))
			continue;
		if (vdevice->vtarget->id == id &&
		    vdevice->vtarget->channel == channel)
			vtarget = vdevice->vtarget;
	}
	return vtarget;
}

#if defined(__VMKLNX__)
/**
 *	mptsas_connect_devices - mark vdevice is nexus-connected when vtarget
 *                               comes back. 
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@volume_id:
 *	@volume_bus:
 *      
 **/
void
mptsas_connect_devices(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	struct scsi_device	*sdev;
	VirtDevice		*vdevice;

	shost_for_each_device(sdev, ioc->sh) {
		if ((vdevice = sdev->hostdata) == NULL ||
		     (vdevice->vtarget == NULL))
			continue;
		if (vdevice->vtarget->id == id &&
		    vdevice->vtarget->channel == channel) {
		    if (vdevice->vtarget->deleted) {
			dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Connect vtarget VC%d:VT%d:VL%d SC%d:ST%d:SL%d vtarget=%p vdevice=%p sdev=%p vtarget->deleted=%d will be set to 0\n",
                        ioc->name, vdevice->vtarget->channel,
			vdevice->vtarget->id, vdevice->lun, sdev->channel,
			sdev->id, sdev->lun, vdevice->vtarget, vdevice, sdev,
			vdevice->vtarget->deleted));
			vdevice->vtarget->deleted = 0;
		    }
		    if (vdevice->nexus_loss) {
			dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT "Connect vdevice VC%d:VT%d:VL%d SC%d:ST%d:SL%d vtarget=%p vdevice=%p sdev=%p vdevice->nexus_loss=%d will be set 0\n",
                        ioc->name, vdevice->vtarget->channel,
			vdevice->vtarget->id, vdevice->lun, sdev->channel,
			sdev->id, sdev->lun, vdevice->vtarget, vdevice, sdev,
			vdevice->nexus_loss));
			vdevice->nexus_loss = 0;
		    }
		}
	}
}
#endif

/**
 *	mptsas_target_reset - Issues TARGET_RESET to end device using
 *	handshaking method
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@channel:
 *	@id:
 *
 *	Returns (1) success
 *      	(0) failure
 *
 **/
static int
mptsas_target_reset(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	MPT_FRAME_HDR	*mf;
	SCSITaskMgmt_t	*pScsiTm;

	if (mpt_set_taskmgmt_in_progress_flag(ioc) != 0)
		return 0;

	if ((mf = mpt_get_msg_frame(mptsasDeviceResetCtx, ioc)) == NULL) {
		dfailprintk(ioc, printk(MYIOC_s_WARN_FMT "%s, no msg frames @%d!!\n",
		    ioc->name,__FUNCTION__, __LINE__));
		goto out_fail;
	}

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT "TaskMgmt request (mf=%p)\n",
		ioc->name, mf));

	/* Format the Request
	 */
	pScsiTm = (SCSITaskMgmt_t *) mf;
	memset (pScsiTm, 0, sizeof(SCSITaskMgmt_t));
	pScsiTm->TargetID = id;
	pScsiTm->Bus = channel;
	pScsiTm->Function = MPI_FUNCTION_SCSI_TASK_MGMT;
	pScsiTm->TaskType = MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	pScsiTm->MsgFlags = MPI_SCSITASKMGMT_MSGFLAGS_LIPRESET_RESET_OPTION;

	DBG_DUMP_TM_REQUEST_FRAME(ioc, (u32 *)mf);

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT "TaskMgmt type=%d (sas device delete) fw_channel = %d fw_id = %d)\n",
	    ioc->name, MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET, channel, id));

	mpt_put_msg_frame_hi_pri(mptsasDeviceResetCtx, ioc, mf);

	return 1;

 out_fail:

	mpt_clear_taskmgmt_in_progress_flag(ioc);
	return 0;
}

/**
 *	mptsas_target_reset_queue - Receive request for TARGET_RESET after
 *	recieving an firmware event NOT_RESPONDING_EVENT, then put command in
 *	link list and queue if task_queue already in use.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@sas_event_data:
 *
 **/
static void
mptsas_target_reset_queue(MPT_ADAPTER *ioc,
    EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *sas_event_data)
{
	MPT_SCSI_HOST	*hd = shost_priv(ioc->sh);
	VirtTarget *vtarget = NULL;
	struct mptsas_target_reset_event	*target_reset_list;
	u8		id, channel;

	id = sas_event_data->TargetID;
	channel = sas_event_data->Bus;

	if (!(vtarget = mptsas_find_vtarget(ioc, channel, id)))
		return;

	if (!ioc->disable_hotplug_remove)
		vtarget->deleted = 1; /* block IO */

	target_reset_list = kzalloc(sizeof(*target_reset_list),
	    GFP_ATOMIC);
	if (!target_reset_list) {
		dfailprintk(ioc, printk(MYIOC_s_WARN_FMT "%s, failed to allocate mem @%d..!!\n",
		    ioc->name,__FUNCTION__, __LINE__));
		return;
	}

	memcpy(&target_reset_list->sas_event_data, sas_event_data,
		sizeof(*sas_event_data));
	list_add_tail(&target_reset_list->list, &hd->target_reset_list);

	target_reset_list->time_count = jiffies;

	if (mptsas_target_reset(ioc, channel, id))
		target_reset_list->target_reset_issued = 1;
}

/**
 *	mptsas_taskmgmt_complete - Completion for TARGET_RESET after
 *	NOT_RESPONDING_EVENT, enable work queue to finish off removing device
 *	from upper layers. then send next TARGET_RESET in the queue.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 **/
static int
mptsas_taskmgmt_complete(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, MPT_FRAME_HDR *mr)
{
	MPT_SCSI_HOST	*hd = shost_priv(ioc->sh);
        struct list_head *head = &hd->target_reset_list;
	struct mptsas_target_reset_event	*target_reset_list;
	struct mptsas_hotplug_event *ev;
	EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *sas_event_data;
	u8		id, channel;
	__le64		sas_address;
	SCSITaskMgmtReply_t *pScsiTmReply;

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT "TaskMgmt completed: "
	    "(mf = %p, mr = %p)\n", ioc->name, mf, mr));

	pScsiTmReply = (SCSITaskMgmtReply_t *)mr;
	if (pScsiTmReply) {
		dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "\tTaskMgmt completed: fw_channel = %d, fw_id = %d,\n"
		    "\ttask_type = 0x%02X, iocstatus = 0x%04X "
		    "loginfo = 0x%08X,\n\tresponse_code = 0x%02X, "
		    "term_cmnds = %d\n", ioc->name,
		    pScsiTmReply->Bus, pScsiTmReply->TargetID,
		    pScsiTmReply->TaskType,
		    le16_to_cpu(pScsiTmReply->IOCStatus),
		    le32_to_cpu(pScsiTmReply->IOCLogInfo),
		    pScsiTmReply->ResponseCode,
		    le32_to_cpu(pScsiTmReply->TerminationCount)));

		if (pScsiTmReply->ResponseCode)
			mptscsih_taskmgmt_response_code(ioc,
			pScsiTmReply->ResponseCode);
	}

	if (pScsiTmReply && (pScsiTmReply->TaskType ==
	    MPI_SCSITASKMGMT_TASKTYPE_QUERY_TASK || pScsiTmReply->TaskType ==
	     MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET)) {
		ioc->taskmgmt_cmds.status |= MPT_MGMT_STATUS_COMMAND_GOOD;
		ioc->taskmgmt_cmds.status |= MPT_MGMT_STATUS_RF_VALID;
		memcpy(ioc->taskmgmt_cmds.reply, mr,
		    min(MPT_DEFAULT_FRAME_SIZE, 4 * mr->u.reply.MsgLength));
		if (ioc->taskmgmt_cmds.status & MPT_MGMT_STATUS_PENDING) {
			ioc->taskmgmt_cmds.status &= ~MPT_MGMT_STATUS_PENDING;
			complete(&ioc->taskmgmt_cmds.done);
			return 1;
		}
		return 0;
	}

	mpt_clear_taskmgmt_in_progress_flag(ioc);

	if (list_empty(head))
		return 1;

	target_reset_list = list_entry(head->next,
	    struct mptsas_target_reset_event, list);

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "TaskMgmt: completed (%d seconds)\n",
	    ioc->name, jiffies_to_msecs(jiffies -
	    target_reset_list->time_count)/1000));

	sas_event_data = &target_reset_list->sas_event_data;
	id = sas_event_data->TargetID;
	channel = sas_event_data->Bus;

	target_reset_list->time_count = jiffies;

	/*
	 * retry target reset
	 */
	if (!target_reset_list->target_reset_issued) {
		if (mptsas_target_reset(ioc, channel, id))
			target_reset_list->target_reset_issued = 1;
		return 1;
	}

	/*
	 * enable work queue to remove device from upper layers
	 */
	list_del(&target_reset_list->list);

	ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev) {
		dfailprintk(ioc, printk(MYIOC_s_WARN_FMT "%s: failed to "
		    "allocate mem @%d..!!\n", ioc->name,__FUNCTION__,
		    __LINE__));
		return 1;
	}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	INIT_DELAYED_WORK(&ev->hotplug_work, mptsas_hotplug_work);
#else
	INIT_WORK(&ev->hotplug_work, mptsas_hotplug_work, ev);
#endif
	ev->ioc = ioc;
	ev->handle = le16_to_cpu(sas_event_data->DevHandle);
	ev->channel = channel;
	ev->id = id;
	ev->phy_id = sas_event_data->PhyNum;
	memcpy(&sas_address, &sas_event_data->SASAddress,
	    sizeof(__le64));
	ev->sas_address = le64_to_cpu(sas_address);
	ev->device_info = le32_to_cpu(sas_event_data->DeviceInfo);
	ev->event_type = MPTSAS_DEL_DEVICE;
	schedule_delayed_work(&ev->hotplug_work, 0);
	kfree(target_reset_list);

	/*
	 * issue target reset to next device in the queue
	 */

	head = &hd->target_reset_list;
	if (list_empty(head))
		return 1;

	target_reset_list = list_entry(head->next, struct mptsas_target_reset_event,
	    list);

	sas_event_data = &target_reset_list->sas_event_data;
	id = sas_event_data->TargetID;
	channel = sas_event_data->Bus;

	target_reset_list->time_count = jiffies;

	if (mptsas_target_reset(ioc, channel, id))
		target_reset_list->target_reset_issued = 1;

	return 1;
}

/**
 *	mptsas_ioc_reset -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@reset_phase:
 *
 **/
static int
mptsas_ioc_reset(MPT_ADAPTER *ioc, int reset_phase)
{
	MPT_SCSI_HOST   *hd;
	struct mptsas_target_reset_event *target_reset_list, *n;
	int rc;

	rc = mptscsih_ioc_reset(ioc, reset_phase);
	if ((ioc->bus_type != SAS) || (!rc))
		return rc;

	switch(reset_phase) {
	case MPT_IOC_SETUP_RESET:
		dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: MPT_IOC_SETUP_RESET\n", ioc->name, __FUNCTION__));
		break;
	case MPT_IOC_PRE_RESET:
		dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: MPT_IOC_PRE_RESET\n", ioc->name, __FUNCTION__));
		break;
	case MPT_IOC_POST_RESET:
		dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: MPT_IOC_POST_RESET\n", ioc->name, __FUNCTION__));
		if (ioc->sas_mgmt.status & MPT_MGMT_STATUS_PENDING) {
			ioc->sas_mgmt.status |= MPT_MGMT_STATUS_DID_IOCRESET;
			complete(&ioc->sas_mgmt.done);
		}
		hd = shost_priv(ioc->sh);
		if (!hd->ioc)
			goto out;
		if (list_empty(&hd->target_reset_list))
			break;
		/* flush the target_reset_list */
		list_for_each_entry_safe(target_reset_list, n,
		    &hd->target_reset_list, list) {
			dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
			    "%s: removing target reset for id=%d\n",
			    ioc->name, __FUNCTION__,
			    target_reset_list->sas_event_data.TargetID));
			list_del(&target_reset_list->list);
			kfree(target_reset_list);
		}
		break;
	default:
		break;
	}

 out:
	return rc;
}

/**
 *	mptsas_sas_enclosure_pg0 -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@enclosure:
 *	@form:
 *	@form_specific:
 *
 **/
static int
mptsas_sas_enclosure_pg0(MPT_ADAPTER *ioc, struct mptsas_enclosure *enclosure,
		u32 form, u32 form_specific)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasEnclosurePage0_t *buffer;
	dma_addr_t dma_handle;
	int error;
	__le64 le_identifier;

	memset(&hdr, 0, sizeof(hdr));
	hdr.PageVersion = MPI_SASENCLOSURE0_PAGEVERSION;
	hdr.PageNumber = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_ENCLOSURE;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = form + form_specific;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;
	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			&dma_handle);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;

	/* save config data */
	memcpy(&le_identifier, &buffer->EnclosureLogicalID, sizeof(__le64));
	enclosure->enclosure_logical_id = le64_to_cpu(le_identifier);
	enclosure->enclosure_handle = le16_to_cpu(buffer->EnclosureHandle);
	enclosure->flags = le16_to_cpu(buffer->Flags);
	enclosure->num_slot = le16_to_cpu(buffer->NumSlots);
	enclosure->start_slot = le16_to_cpu(buffer->StartSlot);
	enclosure->start_id = buffer->StartTargetID;
	enclosure->start_channel = buffer->StartBus;
	enclosure->sep_id = buffer->SEPTargetID;
	enclosure->sep_channel = buffer->SEPBus;

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
 out:
	return error;
}

/**
 *	mptsas_get_lun_number - returns the first entry in report_luns table
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@channel:
 *	@id:
 *	@lun:
 *
 */
static int
mptsas_get_lun_number(MPT_ADAPTER *ioc, u8 channel, u8 id, int *lun)
{
	INTERNAL_CMD	*iocmd;
	struct scsi_lun *lun_data;
	dma_addr_t	lun_data_dma;
	u32		lun_data_len;
	u8 		*data;
	MPT_SCSI_HOST	*hd;
	int		rc;
	u32 		length, num_luns;

	iocmd = NULL;
	hd = shost_priv(ioc->sh);
	lun_data_len = (255 * sizeof(struct scsi_lun));
	lun_data = pci_alloc_consistent(ioc->pcidev, lun_data_len,
	    &lun_data_dma);
	if (!lun_data) {
		printk(MYIOC_s_ERR_FMT "%s: pci_alloc_consistent(%d) FAILED!\n",
		    ioc->name, __FUNCTION__, lun_data_len);
		rc = -ENOMEM;
		goto out;
	}

	iocmd = kzalloc(sizeof(INTERNAL_CMD), GFP_KERNEL);
	if (!iocmd) {
		printk(MYIOC_s_ERR_FMT "%s: kzalloc(%zd) FAILED!\n",
		    ioc->name, __FUNCTION__, sizeof(INTERNAL_CMD));
		rc = -ENOMEM;
		goto out;
	}

	/*
	 * Report Luns
	 */
	iocmd->cmd = REPORT_LUNS;
	iocmd->data_dma = lun_data_dma;
#if defined(__VMKLNX__)
	iocmd->data = (char *)lun_data;
#else /* !defined(__VMKLNX__) */
	iocmd->data = (u8 *)lun_data;
#endif /* defined(__VMKLNX__) */
	iocmd->size = lun_data_len;
	iocmd->channel = channel;
	iocmd->id = id;

	if ((rc = mptscsih_do_cmd(hd, iocmd)) < 0) {
		printk(MYIOC_s_ERR_FMT "%s: fw_channel=%d fw_id=%d: "
		    "report_luns failed due to rc=0x%x\n", ioc->name,
		    __FUNCTION__, channel, id, rc);
		goto out;
	}

	if (rc != MPT_SCANDV_GOOD) {
		printk(MYIOC_s_ERR_FMT "%s: fw_channel=%d fw_id=%d: "
		    "report_luns failed due to rc=0x%x\n", ioc->name,
		    __FUNCTION__, channel, id, rc);
		rc = -rc;
		goto out;
	}

	data = (u8 *)lun_data;
	length = ((data[0] << 24) | (data[1] << 16) |
	    (data[2] << 8) | (data[3] << 0));

	num_luns = (length / sizeof(struct scsi_lun));
	if (!num_luns)
		goto out;
	/* return 1st lun in the list */
	*lun = mpt_scsilun_to_int(&lun_data[1]);

#if 0
	/* some debugging, left commented out */
	{
		struct scsi_lun *lunp;
		for (lunp = &lun_data[1]; lunp <= &lun_data[num_luns]; lunp++)
			printk("%x\n", mpt_scsilun_to_int(lunp));
	}
#endif

 out:
	if (lun_data)
		pci_free_consistent(ioc->pcidev, lun_data_len, lun_data,
		    lun_data_dma);
	kfree(iocmd);
	return rc;
}

/**
 *	mptsas_test_unit_ready -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@channel:
 *	@id:
 *
 **/
static int
mptsas_test_unit_ready(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	INTERNAL_CMD	*iocmd;
	MPT_SCSI_HOST	*hd = shost_priv(ioc->sh);
	int		rc, retries;
	u8		skey, asc, ascq;

	iocmd = kzalloc(sizeof(INTERNAL_CMD), GFP_KERNEL);
	if (!iocmd) {
		printk(MYIOC_s_ERR_FMT "%s: kzalloc(%zd) FAILED!\n",
		ioc->name, __FUNCTION__, sizeof(INTERNAL_CMD));
		return -1;
	}

	iocmd->cmd = TEST_UNIT_READY;
	iocmd->data_dma = -1;
	iocmd->data = NULL;

	if (mptscsih_is_phys_disk(ioc, channel, id)) {
		iocmd->flags |= MPT_ICFLAG_PHYS_DISK;
		iocmd->physDiskNum = mptscsih_raid_id_to_num(ioc, channel, id);
		iocmd->id = id;
	}
	iocmd->channel = channel;
	iocmd->id = id;

	for (retries = 0, rc = -1 ; retries < mpt_cmd_retry_count; retries++) {
		devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: fw_channel=%d "
		    "fw_id=%d lun=%d count=%d\n", ioc->name, __FUNCTION__,
		    channel, id, iocmd->lun, retries));
		rc = mptscsih_do_cmd(hd, iocmd);
		devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: rc=0x%02x\n",
		    ioc->name, __FUNCTION__, rc));
		if (rc < 0) {
			printk(MYIOC_s_ERR_FMT "%s: fw_channel=%d fw_id=%d: "
			    "tur failed due to timeout\n", ioc->name,
			    __FUNCTION__, channel, id);
			goto tur_done;
		}
		switch(rc) {
		case MPT_SCANDV_GOOD:
			goto tur_done;
		case MPT_SCANDV_BUSY:
			devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: "
			    "fw_channel=%d fw_id=%d : device busy\n",
			    ioc->name, __FUNCTION__, channel, id));
			ssleep(1);
			break;
		case MPT_SCANDV_DID_RESET:
			devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: "
			    "fw_channel=%d fw_id=%d : did reset\n",
			    ioc->name, __FUNCTION__, channel, id));
			ssleep(1);
			break;
		case MPT_SCANDV_SENSE:
			skey = ioc->internal_cmds.sense[2] & 0x0F;
			asc = ioc->internal_cmds.sense[12];
			ascq = ioc->internal_cmds.sense[13];

			devtprintk(ioc, printk(MYIOC_s_DEBUG_FMT "%s: "
			    "fw_channel=%d fw_id=%d : [sense_key,asc,"
			    "ascq]: [0x%02x,0x%02x,0x%02x]\n", ioc->name,
			     __FUNCTION__, channel, id, skey, asc, ascq));

			if (skey == UNIT_ATTENTION)
				break;
			else if (skey == NOT_READY) {
				/*
				 * medium isn't present
				 */
				if (asc == 0x3a) {
					rc = MPT_SCANDV_GOOD;
					goto tur_done;
				}
				/*
				 * LU becoming ready, or
				 * LU hasn't self-configured yet
				 */
				if ((asc == 0x04 && ascq == 0x01) ||
				    (asc == 0x3e && ascq == 0x00)) {
					ssleep(1);
					break;
				}
			} else if (skey == ILLEGAL_REQUEST) {
				/* try sending a tur to a non-zero lun number */
				if (!iocmd->lun && !mptsas_get_lun_number(ioc,
				    channel, id, &iocmd->lun) && iocmd->lun)
					continue;
			}
			printk(MYIOC_s_ERR_FMT "%s: fw_channel=%d fw_id=%d : "
			    "tur failed due to [sense_key,asc,ascq]: "
			    "[0x%02x,0x%02x,0x%02x]\n", ioc->name,
			    __FUNCTION__, channel, id, skey, asc, ascq);
			goto tur_done;
		case MPT_SCANDV_SELECTION_TIMEOUT:
			printk(MYIOC_s_ERR_FMT "%s: fw_channel=%d fw_id=%d: "
			    "tur failed due to no device\n", ioc->name,
			    __FUNCTION__, channel,
			    id);
			goto tur_done;
		case MPT_SCANDV_SOME_ERROR:
			printk(MYIOC_s_ERR_FMT "%s: fw_channel=%d fw_id=%d: "
			    "tur failed due to some error\n", ioc->name,
			    __FUNCTION__,
			    channel, id);
			goto tur_done;
		default:
			printk(MYIOC_s_ERR_FMT
			    "%s: fw_channel=%d fw_id=%d: tur failed due to "
			    "unknown rc=0x%02x\n", ioc->name, __FUNCTION__,
			    channel, id, rc );
			goto tur_done;
		}
	}
 tur_done:
	kfree(iocmd);
	return rc;
}

/**
 *	mptsas_issue_tlr - Enabling Transport Layer Retries
 *	@hd:
 *	@sdev:
 *
 **/
static void
mptsas_issue_tlr(MPT_SCSI_HOST *hd, struct scsi_device *sdev)
{
	INTERNAL_CMD	*iocmd;
	VirtDevice	*vdevice = sdev->hostdata;
	u8		retries;
	u8		rc;
	MPT_ADAPTER *ioc = hd->ioc;

	if ( sdev->inquiry[8]  == 'H' &&
	     sdev->inquiry[9]  == 'P' &&
	     sdev->inquiry[10] == ' ' &&
	     sdev->inquiry[11] == ' ' &&
	     sdev->inquiry[12] == ' ' &&
	     sdev->inquiry[13] == ' ' &&
	     sdev->inquiry[14] == ' ' &&
	     sdev->inquiry[15] == ' ' ) {

		iocmd = kzalloc(sizeof(INTERNAL_CMD), GFP_KERNEL);
		if (!iocmd) {
			printk(MYIOC_s_ERR_FMT "%s: kzalloc(%zd) FAILED!\n",
			__FUNCTION__, ioc->name, sizeof(INTERNAL_CMD));
			return;
		}
		iocmd->id = vdevice->vtarget->id;
		iocmd->channel = vdevice->vtarget->channel;
		iocmd->lun = vdevice->lun;
		iocmd->physDiskNum = -1;
		iocmd->cmd = TRANSPORT_LAYER_RETRIES;
		iocmd->data_dma = -1;
		for (retries = 0, rc = -1; retries < 3; retries++) {
			rc = mptscsih_do_cmd(hd, iocmd);
			if (!rc)
				break;
		}
		if (rc != 0)
			printk(MYIOC_s_DEBUG_FMT "unable to enable TLR on"
			   " fw_channel %d, fw_id %d, lun=%d\n",
			   ioc->name, vdevice->vtarget->channel,
			   vdevice->vtarget->id, sdev->lun);
		kfree(iocmd);
	}
}

/**
 *	mptsas_slave_configure -
 *	@sdev:
 *
 **/
static int
mptsas_slave_configure(struct scsi_device *sdev)
{
	struct Scsi_Host	*host = sdev->host;
	MPT_SCSI_HOST		*hd = shost_priv(host);
	MPT_ADAPTER 		*ioc = hd->ioc;

	/*
	 * RAID volumes placed beyond the last expected port.
	 * Ignore sending sas mode pages in that case..
	 */
	if (sdev->channel == MPTSAS_RAID_CHANNEL) {
#if defined(CPQ_CIM)
		mptsas_add_device_component_starget_ir(ioc, scsi_target(sdev));
#endif
		goto out;
	}

	sas_read_port_mode_page(sdev);
#if defined(__VMKLNX__)
	if (sdev->no_uld_attach && sdev->sdev_target && sdev->sdev_target->hostdata) {
	        /* Mark a RAID-member target as configured. */ 
		VirtTarget *vtarget = (VirtTarget *) sdev->sdev_target->hostdata;
		vtarget->configured = 1;
	}
#endif

#if defined(CPQ_CIM)
	mptsas_add_device_component_starget(ioc, scsi_target(sdev));
#endif

	if (sdev->type == TYPE_TAPE &&
	    (ioc->facts.IOCCapabilities & MPI_IOCFACTS_CAPABILITY_TLR ))
		mptsas_issue_tlr(hd, sdev);
 out:

	return mptscsih_slave_configure(sdev);
}

/**
 *	mptsas_target_alloc -
 *	@starget:
 *
 **/
static int
mptsas_target_alloc(struct scsi_target *starget)
{
	struct Scsi_Host *host = dev_to_shost(&starget->dev);
	MPT_SCSI_HOST		*hd = shost_priv(host);
	VirtTarget		*vtarget;
	u8			id, channel;
	struct sas_rphy		*rphy;
	struct mptsas_portinfo	*p;
	int 			 i;
	MPT_ADAPTER		*ioc = hd->ioc;

	vtarget = kzalloc(sizeof(VirtTarget), GFP_KERNEL);
	if (!vtarget)
		return -ENOMEM;

	vtarget->starget = starget;
	vtarget->ioc_id = ioc->id;
	vtarget->tflags = MPT_TARGET_FLAGS_Q_YES;
	id = starget->id;
	channel = 0;

	/*
	 * RAID volumes placed beyond the last expected port.
	 */
	if (starget->channel == MPTSAS_RAID_CHANNEL) {
		if (!ioc->raid_data.pIocPg2) {
			kfree(vtarget);
			return -ENXIO;
		}
		for (i=0; i < ioc->raid_data.pIocPg2->NumActiveVolumes; i++)
			if (id == ioc->raid_data.pIocPg2->RaidVolume[i].VolumeID)
				channel = ioc->raid_data.pIocPg2->RaidVolume[i].VolumeBus;
		vtarget->raidVolume = 1;
#if defined(__VMKLNX__)
                if (!vtarget->sas_uid) {
                   int rc = -1;
                   SCSI_DEVICE_IDENTIFICATION_PAGE vpd83Response;
                   /* First time - get RAID SAS uid from FW */
                   memset(&vpd83Response, 0, sizeof(vpd83Response));
                   rc = mptsas_get_vpd83_info(ioc, channel, id, &vpd83Response);
                   if (0 == rc) {
                      /*
                       * LSI firmware erases the first nibble of the sas
                       * identifier so it will not collide with any real
                       * SAS address. A typical RAID uid looks like
                       * 0x0XXXXXXXXXXXXXXX.
                       */
                      memcpy(&vtarget->sas_uid,
                         vpd83Response.NAA_ID_Reg_Extended.VenSpecIdExtension,
                         sizeof(vtarget->sas_uid));
                   } else {
                      printk("mptsas_target_alloc failed to get the SAS RAID uid on %s C%d:T%d VC%d:VT%d\n", ioc->name, starget->channel, starget->id, channel, id);
                   }
                }
#endif
		goto out;
	}

	rphy = dev_to_rphy(starget->dev.parent);
	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(p, &ioc->sas_topology, list) {
		for (i = 0; i < p->num_phys; i++) {
			if (p->phy_info[i].attached.sas_address !=
			    rphy->identify.sas_address)
				continue;
			id = p->phy_info[i].attached.id;
			channel = p->phy_info[i].attached.channel;
			mptsas_set_starget(&p->phy_info[i], starget);

			starget_printk(KERN_INFO, starget, MYIOC_s_FMT
			"add device: fw_channel %d, fw_id %d, phy %d, sas_addr 0x%llx\n",
			ioc->name, p->phy_info[i].attached.channel,
			p->phy_info[i].attached.id, p->phy_info[i].attached.phy_id,
			(unsigned long long)p->phy_info[i].attached.sas_address);

			/*
			 * Exposing hidden raid components
			 */
			if (mptscsih_is_phys_disk(ioc, channel, id)) {
				id = mptscsih_raid_id_to_num(ioc,
				    channel, id);
				vtarget->tflags |=
				    MPT_TARGET_FLAGS_RAID_COMPONENT;
				p->phy_info[i].attached.phys_disk_num = id;
			}
			mutex_unlock(&ioc->sas_topology_mutex);
			goto out;
		}
	}
	mutex_unlock(&ioc->sas_topology_mutex);

	kfree(vtarget);
	return -ENXIO;

 out:
	vtarget->id = id;
	vtarget->channel = channel;
	starget->hostdata = vtarget;
	return 0;
}

/**
 *	mptsas_target_destroy -
 *	@starget:
 *
 **/
static void
mptsas_target_destroy(struct scsi_target *starget)
{
	struct Scsi_Host *host = dev_to_shost(&starget->dev);
	MPT_SCSI_HOST		*hd = shost_priv(host);
	struct sas_rphy		*rphy;
	struct mptsas_portinfo	*p;
	int 			 i;
	MPT_ADAPTER 		*ioc = hd->ioc;

	if (!starget->hostdata)
		return;

#if defined(CPQ_CIM)
	mptsas_del_device_component_by_os(ioc, starget->channel,
	    starget->id);
#endif

	if (starget->channel == MPTSAS_RAID_CHANNEL)
		goto out;

	rphy = dev_to_rphy(starget->dev.parent);
	list_for_each_entry(p, &ioc->sas_topology, list) {
		for (i = 0; i < p->num_phys; i++) {
			if (p->phy_info[i].attached.sas_address !=
					rphy->identify.sas_address)
				continue;

			starget_printk(KERN_INFO, starget, MYIOC_s_FMT
			"delete device: fw_channel %d, fw_id %d, phy %d, sas_addr 0x%llx\n",
			ioc->name, p->phy_info[i].attached.channel,
			p->phy_info[i].attached.id, p->phy_info[i].attached.phy_id,
			(unsigned long long)p->phy_info[i].attached.sas_address);

			goto out;
		}
	}

 out:
	kfree(starget->hostdata);
	starget->hostdata = NULL;
}

/**
 *	mptsas_slave_alloc -
 *	@sdev:
 *
 **/
static int
mptsas_slave_alloc(struct scsi_device *sdev)
{
	struct Scsi_Host	*host = sdev->host;
	MPT_SCSI_HOST		*hd = shost_priv(host);
	struct sas_rphy		*rphy;
	struct mptsas_portinfo	*p;
	VirtDevice		*vdevice;
	struct scsi_target 	*starget;
	int 			i;
	MPT_ADAPTER		*ioc = hd->ioc;

#if defined(__VMKLNX__)
	VirtTarget		*vtarget;
	starget = scsi_target(sdev);
	vtarget = starget->hostdata;
        /*
         * If a RAID-member target is alreayd configured, fail slave_alloc here
         * so the upper layer will not try to create/configure/claim a path for
         * a RAID-member disk target during periodic probe. 
         */
	if (vtarget && vtarget->configured) {
		return -ENXIO;
	}
#endif
	vdevice = kzalloc(sizeof(VirtDevice), GFP_KERNEL);
	if (!vdevice) {
		printk(MYIOC_s_ERR_FMT "slave_alloc kzalloc(%zd) FAILED!\n",
		    ioc->name, sizeof(VirtDevice));
		return -ENOMEM;
	}
#if !defined(__VMKLNX__)
	starget = scsi_target(sdev);
#endif
	vdevice->vtarget = starget->hostdata;
#if defined(__VMKLNX__)
        if (0 == vdevice->vtarget->last_lun) {
                vdevice->vtarget->last_lun = MPT_SAS_LAST_LUN;
        }
#endif /* defined(__VMKLNX__) */
	/*
	 * RAID volumes placed beyond the last expected port.
	 */
#if defined(__VMKLNX__)
	if (sdev->channel == MPTSAS_RAID_CHANNEL) {
		vdevice->lun = sdev->lun;
		goto out;
	}
#else /* !defined(__VMKLNX__) */
	if (sdev->channel == MPTSAS_RAID_CHANNEL)
		goto out;
#endif /* defined(__VMKLNX__) */

	rphy = dev_to_rphy(sdev->sdev_target->dev.parent);
	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(p, &ioc->sas_topology, list) {
		for (i = 0; i < p->num_phys; i++) {
			if (p->phy_info[i].attached.sas_address !=
					rphy->identify.sas_address)
				continue;
			vdevice->lun = sdev->lun;
#if defined(__VMKLNX__)
                        if (p->phy_info[i].attached.device_info & MPI_SAS_DEVICE_INFO_SATA_DEVICE) {
                           vdevice->vtarget->attached_device_type |= MPT_TARGET_DEVICE_TYPE_SATA;
                        }
#endif /* defined(__VMKLNX__) */

			/*
			 * Exposing hidden raid components
			 */
			if (mptscsih_is_phys_disk(ioc,
			    p->phy_info[i].attached.channel,
			    p->phy_info[i].attached.id))
				sdev->no_uld_attach = 1;
			mutex_unlock(&ioc->sas_topology_mutex);
			goto out;
		}
	}
	mutex_unlock(&ioc->sas_topology_mutex);

	kfree(vdevice);
	return -ENXIO;

 out:
	vdevice->vtarget->num_luns++;
	sdev->hostdata = vdevice;
	return 0;
}

/**
 *	mptsas_qcmd -
 *	@SCpnt:
 *	@done:
 *
 **/
static int
mptsas_qcmd(struct scsi_cmnd *SCpnt, void (*done)(struct scsi_cmnd *))
{
	VirtDevice	*vdevice = SCpnt->device->hostdata;

#if defined(__VMKLNX__)
	if (!vdevice || !vdevice->vtarget || vdevice->vtarget->deleted ||
	    vdevice->nexus_loss) {
#else
	if (!vdevice || !vdevice->vtarget || vdevice->vtarget->deleted) {
#endif
		SCpnt->result = DID_NO_CONNECT << 16;
		done(SCpnt);
		return 0;
	}
#if defined(__VMKLNX__)
       /*
        * Return success for SCSI Reserve/Release commands  for SATA disks
        */
        if (((SCpnt->cmnd[0] == RESERVE) || (SCpnt->cmnd[0] == RELEASE)) &&
            (vdevice->vtarget->attached_device_type & MPT_TARGET_DEVICE_TYPE_SATA)) {
                SCpnt->result =  DID_OK << 16;
                done(SCpnt);
                return 0;
        }
#endif /* defined(__VMKLNX__) */

// 	scsi_print_command(SCpnt);
	return mptscsih_qcmd(SCpnt,done);
}


static struct scsi_host_template mptsas_driver_template = {
	.module				= THIS_MODULE,
	.proc_name			= "mptsas",
	.proc_info			= mptscsih_proc_info,
#if defined(__VMKLNX__)
	.name				= "MPT SAS Host",
#else /* !defined(__VMKLNX__) */
	.name				= "MPT SPI Host",
#endif /* defined(__VMKLNX__) */
	.info				= mptscsih_info,
	.queuecommand			= mptsas_qcmd,
	.target_alloc			= mptsas_target_alloc,
	.slave_alloc			= mptsas_slave_alloc,
	.slave_configure		= mptsas_slave_configure,
	.target_destroy			= mptsas_target_destroy,
	.slave_destroy			= mptscsih_slave_destroy,
	.change_queue_depth 		= mptscsih_change_queue_depth,
	.eh_abort_handler		= mptscsih_abort,
	.eh_device_reset_handler	= mptscsih_dev_reset,
	.eh_bus_reset_handler		= mptscsih_bus_reset,
	.eh_host_reset_handler		= mptscsih_host_reset,
	.bios_param			= mptscsih_bios_param,
	.can_queue			= MPT_FC_CAN_QUEUE,
	.this_id			= -1,
	.sg_tablesize			= MPT_SCSI_SG_DEPTH,
	.max_sectors			= 8192,
	.cmd_per_lun			= 7,
	.use_clustering			= ENABLE_CLUSTERING,
	.shost_attrs			= mptscsih_host_attrs,
};

/**
 *	mptsas_get_linkerrors -
 *	@phy:
 *
 **/
static int mptsas_get_linkerrors(struct sas_phy *phy)
{
	MPT_ADAPTER *ioc = phy_to_ioc(phy);
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasPhyPage1_t *buffer;
	dma_addr_t dma_handle;
	int error;

#if defined(MPT_WIDE_PORT_API_PLUS)
	/* FIXME: only have link errors on local phys */
	if (!scsi_is_sas_phy_local(phy))
		return -EINVAL;
#endif

	hdr.PageVersion = MPI_SASPHY1_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 1 /* page number 1*/;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_PHY;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = phy->identify.phy_identifier;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;    /* read */
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	error = mpt_config(ioc, &cfg);
	if (error)
		return error;
	if (!hdr.ExtPageLength)
		return -ENXIO;

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
				      &dma_handle);
	if (!buffer)
		return -ENOMEM;

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;

	mptsas_print_phy_pg1(ioc, buffer);

	phy->invalid_dword_count = le32_to_cpu(buffer->InvalidDwordCount);
	phy->running_disparity_error_count =
		le32_to_cpu(buffer->RunningDisparityErrorCount);
	phy->loss_of_dword_sync_count =
		le32_to_cpu(buffer->LossDwordSynchCount);
	phy->phy_reset_problem_count =
		le32_to_cpu(buffer->PhyResetProblemCount);

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
	return error;
}

/**
 *	mptsas_mgmt_done -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@req:
 *	@reply:
 *
 **/
static int mptsas_mgmt_done(MPT_ADAPTER *ioc, MPT_FRAME_HDR *req,
		MPT_FRAME_HDR *reply)
{
	ioc->sas_mgmt.status |= MPT_MGMT_STATUS_COMMAND_GOOD;
	if (reply != NULL) {
		ioc->sas_mgmt.status |= MPT_MGMT_STATUS_RF_VALID;
		memcpy(ioc->sas_mgmt.reply, reply,
		    min(ioc->reply_sz, 4 * reply->u.reply.MsgLength));
	}

	if (ioc->sas_mgmt.status & MPT_MGMT_STATUS_PENDING) {
		ioc->sas_mgmt.status &= ~MPT_MGMT_STATUS_PENDING;
		complete(&ioc->sas_mgmt.done);
		return 1;
	}
	return 0;
}

/**
 *	mptsas_phy_reset -
 *	@phy:
 *	@hard_reset:
 *
 **/
static int mptsas_phy_reset(struct sas_phy *phy, int hard_reset)
{
	MPT_ADAPTER *ioc = phy_to_ioc(phy);
	SasIoUnitControlRequest_t *req;
	SasIoUnitControlReply_t *reply;
	MPT_FRAME_HDR *mf;
	MPIHeader_t *hdr;
	unsigned long timeleft;
	int error = -ERESTARTSYS;

#if defined(MPT_WIDE_PORT_API_PLUS)
	/* FIXME: fusion doesn't allow non-local phy reset */
	if (!scsi_is_sas_phy_local(phy))
		return -EINVAL;
#endif

	/* not implemented for expanders */
	if (phy->identify.target_port_protocols & SAS_PROTOCOL_SMP)
		return -ENXIO;

	if (mutex_lock_interruptible(&ioc->sas_mgmt.mutex))
		goto out;

	mf = mpt_get_msg_frame(mptsasMgmtCtx, ioc);
	if (!mf) {
		error = -ENOMEM;
		goto out_unlock;
	}

	hdr = (MPIHeader_t *) mf;
	req = (SasIoUnitControlRequest_t *)mf;
	memset(req, 0, sizeof(SasIoUnitControlRequest_t));
	req->Function = MPI_FUNCTION_SAS_IO_UNIT_CONTROL;
	req->MsgContext = hdr->MsgContext;
	req->Operation = hard_reset ?
		MPI_SAS_OP_PHY_HARD_RESET : MPI_SAS_OP_PHY_LINK_RESET;
	req->PhyNum = phy->identify.phy_identifier;

	INITIALIZE_MGMT_STATUS(ioc->sas_mgmt.status)
	mpt_put_msg_frame(mptsasMgmtCtx, ioc, mf);
	timeleft = wait_for_completion_timeout(&ioc->sas_mgmt.done, 10*HZ);
	if (!(ioc->sas_mgmt.status & MPT_MGMT_STATUS_COMMAND_GOOD)) {
		error = -ETIME;
		mpt_free_msg_frame(ioc, mf);
		if (ioc->sas_mgmt.status & MPT_MGMT_STATUS_DID_IOCRESET)
			goto out;
		if (!timeleft) {
			if (mpt_SoftResetHandler(ioc, CAN_SLEEP) != 0)
				mpt_HardResetHandler(ioc, CAN_SLEEP);
		}
		goto out_unlock;
	}

	/* a reply frame is expected */
	if ((ioc->sas_mgmt.status &
	    MPT_MGMT_STATUS_RF_VALID) == 0) {
		error = -ENXIO;
		goto out_unlock;
	}

	/* process the completed Reply Message Frame */
	reply = (SasIoUnitControlReply_t *)ioc->sas_mgmt.reply;
	if (reply->IOCStatus != MPI_IOCSTATUS_SUCCESS) {
		printk(MYIOC_s_INFO_FMT "%s: IOCStatus=0x%X IOCLogInfo=0x%X\n",
		    ioc->name, __FUNCTION__, reply->IOCStatus, reply->IOCLogInfo);
		error = -ENXIO;
		goto out_unlock;
	}

	error = 0;

 out_unlock:
	CLEAR_MGMT_STATUS(ioc->sas_mgmt.status)
	mutex_unlock(&ioc->sas_mgmt.mutex);
 out:
	return error;
}

/**
 *	mptsas_get_enclosure_identifier -
 *	@rphy:
 *	@identifier:
 *
 **/
static int
mptsas_get_enclosure_identifier(struct sas_rphy *rphy, u64 *identifier)
{
	MPT_ADAPTER *ioc = rphy_to_ioc(rphy);
	int i, error;
	struct mptsas_portinfo *p;
	struct mptsas_enclosure enclosure_info;
	u64 enclosure_handle;

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(p, &ioc->sas_topology, list) {
		for (i = 0; i < p->num_phys; i++) {
			if (p->phy_info[i].attached.sas_address ==
			    rphy->identify.sas_address) {
				enclosure_handle = p->phy_info[i].
					attached.handle_enclosure;
				goto found_info;
			}
		}
	}
	mutex_unlock(&ioc->sas_topology_mutex);
	return -ENXIO;

 found_info:
	mutex_unlock(&ioc->sas_topology_mutex);
	memset(&enclosure_info, 0, sizeof(struct mptsas_enclosure));
	error = mptsas_sas_enclosure_pg0(ioc, &enclosure_info,
			(MPI_SAS_ENCLOS_PGAD_FORM_HANDLE <<
			 MPI_SAS_ENCLOS_PGAD_FORM_SHIFT),
			 enclosure_handle);
	if (!error)
		*identifier = enclosure_info.enclosure_logical_id;
	return error;
}

/**
 *	mptsas_get_bay_identifier -
 *	@rphy:
 *
 **/
static int
mptsas_get_bay_identifier(struct sas_rphy *rphy)
{
	MPT_ADAPTER *ioc = rphy_to_ioc(rphy);
	struct mptsas_portinfo *p;
	int i, rc;

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(p, &ioc->sas_topology, list) {
		for (i = 0; i < p->num_phys; i++) {
			if (p->phy_info[i].attached.sas_address ==
			    rphy->identify.sas_address) {
				rc = p->phy_info[i].attached.slot;
				goto out;
			}
		}
	}
	rc = -ENXIO;
 out:
	mutex_unlock(&ioc->sas_topology_mutex);
	return rc;
}

static struct sas_function_template mptsas_transport_functions = {
	.get_linkerrors		= mptsas_get_linkerrors,
	.get_enclosure_identifier = mptsas_get_enclosure_identifier,
	.get_bay_identifier	= mptsas_get_bay_identifier,
	.phy_reset		= mptsas_phy_reset,
#if defined(__VMKLNX__)
        .get_target_sas_identifier= mptsas_get_target_sas_identifier,
#endif
};

static struct scsi_transport_template *mptsas_transport_template;

/**
 *	mptsas_sas_io_unit_pg0 -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@port_info:
 *
 **/
static int
mptsas_sas_io_unit_pg0(MPT_ADAPTER *ioc, struct mptsas_portinfo *port_info)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasIOUnitPage0_t *buffer;
	dma_addr_t dma_handle;
	int error, i;

	hdr.PageVersion = MPI_SASIOUNITPAGE0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = 0;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;
	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
					    &dma_handle);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;

	port_info->num_phys = buffer->NumPhys;
	port_info->phy_info = kcalloc(port_info->num_phys,
		sizeof(*port_info->phy_info),GFP_KERNEL);
	if (!port_info->phy_info) {
		error = -ENOMEM;
		goto out_free_consistent;
	}

	ioc->nvdata_version_persistent =
	    le16_to_cpu(buffer->NvdataVersionPersistent);
	ioc->nvdata_version_default =
	    le16_to_cpu(buffer->NvdataVersionDefault);

	for (i = 0; i < port_info->num_phys; i++) {
		mptsas_print_phy_data(ioc, &buffer->PhyData[i]);
		port_info->phy_info[i].phy_id = i;
		port_info->phy_info[i].port_id =
		    buffer->PhyData[i].Port;
		port_info->phy_info[i].negotiated_link_rate =
		    buffer->PhyData[i].NegotiatedLinkRate;
		port_info->phy_info[i].portinfo = port_info;
		port_info->phy_info[i].handle =
		    le16_to_cpu(buffer->PhyData[i].ControllerDevHandle);
#if defined(CPQ_CIM)
		port_info->phy_info[i].port_flags =
		    buffer->PhyData[i].PortFlags;
#endif
	}

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
 out:
	return error;
}

/**
 *	mptsas_sas_io_unit_pg1 -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 **/
static int
mptsas_sas_io_unit_pg1(MPT_ADAPTER *ioc)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasIOUnitPage1_t *buffer;
	dma_addr_t dma_handle;
	int error;
	u16 device_missing_delay;

	memset(&hdr, 0, sizeof(ConfigExtendedPageHeader_t));
	memset(&cfg, 0, sizeof(CONFIGPARMS));

	cfg.cfghdr.ehdr = &hdr;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.cfghdr.ehdr->PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	cfg.cfghdr.ehdr->ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;
	cfg.cfghdr.ehdr->PageVersion = MPI_SASIOUNITPAGE1_PAGEVERSION;
	cfg.cfghdr.ehdr->PageNumber = 1;
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;
	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
					    &dma_handle);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;

	ioc->io_missing_delay  =
	    le16_to_cpu(buffer->IODeviceMissingDelay);
	device_missing_delay = le16_to_cpu(buffer->ReportDeviceMissingDelay);
	ioc->device_missing_delay = (device_missing_delay & MPI_SAS_IOUNIT1_REPORT_MISSING_UNIT_16) ?
	    (device_missing_delay & MPI_SAS_IOUNIT1_REPORT_MISSING_TIMEOUT_MASK) * 16 :
	    device_missing_delay & MPI_SAS_IOUNIT1_REPORT_MISSING_TIMEOUT_MASK;

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
 out:
	return error;
}

/**
 *	mptsas_sas_phy_pg0 -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@phy_info:
 *	@form:
 *	@form_specific:
 *
 **/
static int
mptsas_sas_phy_pg0(MPT_ADAPTER *ioc, struct mptsas_phyinfo *phy_info,
		u32 form, u32 form_specific)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasPhyPage0_t *buffer;
	dma_addr_t dma_handle;
	int error;

	hdr.PageVersion = MPI_SASPHY0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_PHY;

	cfg.cfghdr.ehdr = &hdr;
	cfg.dir = 0;	/* read */

	/* Get Phy Pg 0 for each Phy. */
	cfg.physAddr = -1;
	cfg.pageAddr = form + form_specific;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;

	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
				      &dma_handle);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;

	mptsas_print_phy_pg0(ioc, buffer);

	phy_info->hw_link_rate = buffer->HwLinkRate;
	phy_info->programmed_link_rate = buffer->ProgrammedLinkRate;
	phy_info->identify.handle = le16_to_cpu(buffer->OwnerDevHandle);
	phy_info->attached.handle = le16_to_cpu(buffer->AttachedDevHandle);
#if defined(CPQ_CIM)
	phy_info->change_count = buffer->ChangeCount;
#endif
	phy_info->phy_info = le32_to_cpu(buffer->PhyInfo);

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
 out:
	return error;
}

/**
 *	mptsas_sas_device_pg0 -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@device_info:
 *	@form:
 *	@form_specific:
 *
 **/
static int
mptsas_sas_device_pg0(MPT_ADAPTER *ioc, struct mptsas_devinfo *device_info,
		u32 form, u32 form_specific)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasDevicePage0_t *buffer;
	dma_addr_t dma_handle;
	__le64 sas_address;
	int error=0;

	if (ioc->sas_discovery_runtime &&
		mptsas_is_end_device(device_info))
			goto out;

	hdr.PageVersion = MPI_SASDEVICE0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE;

	cfg.cfghdr.ehdr = &hdr;
	cfg.pageAddr = form + form_specific;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	memset(device_info, 0, sizeof(struct mptsas_devinfo));
	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;
	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
				      &dma_handle);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;

	mptsas_print_device_pg0(ioc, buffer);

	device_info->handle = le16_to_cpu(buffer->DevHandle);
	device_info->handle_parent = le16_to_cpu(buffer->ParentDevHandle);
	device_info->handle_enclosure =
	    le16_to_cpu(buffer->EnclosureHandle);
	device_info->slot = le16_to_cpu(buffer->Slot);
	device_info->phy_id = buffer->PhyNum;
	device_info->port_id = buffer->PhysicalPort;
	device_info->id = buffer->TargetID;
	device_info->phys_disk_num = ~0;
	device_info->channel = buffer->Bus;
	memcpy(&sas_address, &buffer->SASAddress, sizeof(__le64));
	device_info->sas_address = le64_to_cpu(sas_address);
	device_info->device_info =
	    le32_to_cpu(buffer->DeviceInfo);
#if defined(__VMKLNX__)
       /*
        * Set the flag if we find SATA devices connected to the SAS controller.
        */
        if (device_info->device_info & MPI_SAS_DEVICE_INFO_SATA_DEVICE) {
           ioc->attached_device_type |= MPT_TARGET_DEVICE_TYPE_SATA;
        }
#endif /* defined(__VMKLNX__) */
 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
 out:
	return error;
}

/**
 *	mptsas_sas_expander_pg0 -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@port_info:
 *	@form:
 *	@form_specific:
 *
 **/
static int
mptsas_sas_expander_pg0(MPT_ADAPTER *ioc, struct mptsas_portinfo *port_info,
		u32 form, u32 form_specific)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasExpanderPage0_t *buffer;
	dma_addr_t dma_handle;
	__le64 sas_address;
	int i, error;

	hdr.PageVersion = MPI_SASEXPANDER0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_EXPANDER;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = form + form_specific;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	memset(port_info, 0, sizeof(struct mptsas_portinfo));
	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;

	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
				      &dma_handle);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;

	/* save config data */
	if (!buffer->NumPhys) {
		error = -ENXIO;
		goto out;
	}

	port_info->num_phys = buffer->NumPhys;
	port_info->phy_info = kcalloc(port_info->num_phys,
		sizeof(*port_info->phy_info),GFP_KERNEL);
	if (!port_info->phy_info) {
		error = -ENOMEM;
		goto out_free_consistent;
	}

	memcpy(&sas_address, &buffer->SASAddress, sizeof(__le64));
	for (i = 0; i < port_info->num_phys; i++) {
		port_info->phy_info[i].portinfo = port_info;
		port_info->phy_info[i].handle =
		    le16_to_cpu(buffer->DevHandle);
		port_info->phy_info[i].identify.sas_address =
		    le64_to_cpu(sas_address);
		port_info->phy_info[i].identify.handle_parent =
		    le16_to_cpu(buffer->ParentDevHandle);
	}

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
 out:
	return error;
}

/**
 *	mptsas_sas_expander_pg1 -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@phy_info:
 *	@form:
 *	@form_specific:
 *
 **/
static int
mptsas_sas_expander_pg1(MPT_ADAPTER *ioc, struct mptsas_phyinfo *phy_info,
		u32 form, u32 form_specific)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasExpanderPage1_t *buffer;
	dma_addr_t dma_handle;
	int error=0;

	if (ioc->sas_discovery_runtime &&
		mptsas_is_end_device(&phy_info->attached))
			goto out;

	hdr.PageVersion = MPI_SASEXPANDER1_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 1;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_EXPANDER;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = form + form_specific;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out;

	if (!hdr.ExtPageLength) {
		error = -ENXIO;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
				      &dma_handle);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	error = mpt_config(ioc, &cfg);
	if (error)
		goto out_free_consistent;


	mptsas_print_expander_pg1(ioc, buffer);

	/* save config data */
	phy_info->phy_id = buffer->PhyIdentifier;
	phy_info->port_id = buffer->PhysicalPort;
	phy_info->negotiated_link_rate = buffer->NegotiatedLinkRate;
	phy_info->programmed_link_rate = buffer->ProgrammedLinkRate;
	phy_info->hw_link_rate = buffer->HwLinkRate;
	phy_info->identify.handle = le16_to_cpu(buffer->OwnerDevHandle);
	phy_info->attached.handle = le16_to_cpu(buffer->AttachedDevHandle);
#if defined(CPQ_CIM)
	phy_info->change_count = buffer->ChangeCount;
#endif
	phy_info->phy_info = le32_to_cpu(buffer->PhyInfo);

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
 out:
	return error;
}

/**
 *	mptsas_parse_device_info -
 *	@identify:
 *	@device_info:
 *
 **/
static void
mptsas_parse_device_info(struct sas_identify *identify,
		struct mptsas_devinfo *device_info)
{
	u16 protocols;

	identify->sas_address = device_info->sas_address;
	identify->phy_identifier = device_info->phy_id;

	/*
	 * Fill in Phy Initiator Port Protocol.
	 * Bits 6:3, more than one bit can be set, fall through cases.
	 */
	protocols = device_info->device_info & 0x78;
	identify->initiator_port_protocols = 0;
	if (protocols & MPI_SAS_DEVICE_INFO_SSP_INITIATOR)
		identify->initiator_port_protocols |= SAS_PROTOCOL_SSP;
	if (protocols & MPI_SAS_DEVICE_INFO_STP_INITIATOR)
		identify->initiator_port_protocols |= SAS_PROTOCOL_STP;
	if (protocols & MPI_SAS_DEVICE_INFO_SMP_INITIATOR)
		identify->initiator_port_protocols |= SAS_PROTOCOL_SMP;
	if (protocols & MPI_SAS_DEVICE_INFO_SATA_HOST)
		identify->initiator_port_protocols |= SAS_PROTOCOL_SATA;

	/*
	 * Fill in Phy Target Port Protocol.
	 * Bits 10:7, more than one bit can be set, fall through cases.
	 */
	protocols = device_info->device_info & 0x780;
	identify->target_port_protocols = 0;
	if (protocols & MPI_SAS_DEVICE_INFO_SSP_TARGET)
		identify->target_port_protocols |= SAS_PROTOCOL_SSP;
	if (protocols & MPI_SAS_DEVICE_INFO_STP_TARGET)
		identify->target_port_protocols |= SAS_PROTOCOL_STP;
	if (protocols & MPI_SAS_DEVICE_INFO_SMP_TARGET)
		identify->target_port_protocols |= SAS_PROTOCOL_SMP;
	if (protocols & MPI_SAS_DEVICE_INFO_SATA_DEVICE)
		identify->target_port_protocols |= SAS_PROTOCOL_SATA;

	/*
	 * Fill in Attached device type.
	 */
	switch (device_info->device_info &
			MPI_SAS_DEVICE_INFO_MASK_DEVICE_TYPE) {
	case MPI_SAS_DEVICE_INFO_NO_DEVICE:
		identify->device_type = SAS_PHY_UNUSED;
		break;
	case MPI_SAS_DEVICE_INFO_END_DEVICE:
		identify->device_type = SAS_END_DEVICE;
		break;
	case MPI_SAS_DEVICE_INFO_EDGE_EXPANDER:
		identify->device_type = SAS_EDGE_EXPANDER_DEVICE;
		break;
	case MPI_SAS_DEVICE_INFO_FANOUT_EXPANDER:
		identify->device_type = SAS_FANOUT_EXPANDER_DEVICE;
		break;
	}
}

/**
 *	mptsas_probe_one_phy -
 *	@dev:
 *	@phy_info:
 *	@local:
 *
 **/
static int mptsas_probe_one_phy(struct device *dev,
		struct mptsas_phyinfo *phy_info, int index, int local)
{
	MPT_ADAPTER *ioc;
	struct sas_phy *phy;
#if defined(MPT_WIDE_PORT_API)
	struct sas_port *port;
#endif
	int error = 0;

	if (!dev) {
		error = -ENODEV;
		goto out;
	}

	if (!phy_info->phy) {
		phy = sas_phy_alloc(dev, index);
		if (!phy) {
			error = -ENOMEM;
			goto out;
		}
	} else
		phy = phy_info->phy;

#if !defined(MPT_WIDE_PORT_API)
	phy->port_identifier = phy_info->port_id;
#endif
	mptsas_parse_device_info(&phy->identify, &phy_info->identify);

	/*
	 * Set Negotiated link rate.
	 */
	switch (phy_info->negotiated_link_rate) {
	case MPI_SAS_IOUNIT0_RATE_PHY_DISABLED:
		phy->negotiated_linkrate = SAS_PHY_DISABLED;
		break;
	case MPI_SAS_IOUNIT0_RATE_FAILED_SPEED_NEGOTIATION:
		phy->negotiated_linkrate = SAS_LINK_RATE_FAILED;
		break;
	case MPI_SAS_IOUNIT0_RATE_1_5:
		phy->negotiated_linkrate = SAS_LINK_RATE_1_5_GBPS;
		break;
	case MPI_SAS_IOUNIT0_RATE_3_0:
		phy->negotiated_linkrate = SAS_LINK_RATE_3_0_GBPS;
		break;
	case MPI_SAS_IOUNIT0_RATE_SATA_OOB_COMPLETE:
	case MPI_SAS_IOUNIT0_RATE_UNKNOWN:
	default:
		phy->negotiated_linkrate = SAS_LINK_RATE_UNKNOWN;
		break;
	}

	/*
	 * Set Max hardware link rate.
	 */
	switch (phy_info->hw_link_rate & MPI_SAS_PHY0_PRATE_MAX_RATE_MASK) {
	case MPI_SAS_PHY0_HWRATE_MAX_RATE_1_5:
		phy->maximum_linkrate_hw = SAS_LINK_RATE_1_5_GBPS;
		break;
	case MPI_SAS_PHY0_PRATE_MAX_RATE_3_0:
		phy->maximum_linkrate_hw = SAS_LINK_RATE_3_0_GBPS;
		break;
	default:
		break;
	}

	/*
	 * Set Max programmed link rate.
	 */
	switch (phy_info->programmed_link_rate &
			MPI_SAS_PHY0_PRATE_MAX_RATE_MASK) {
	case MPI_SAS_PHY0_PRATE_MAX_RATE_1_5:
		phy->maximum_linkrate = SAS_LINK_RATE_1_5_GBPS;
		break;
	case MPI_SAS_PHY0_PRATE_MAX_RATE_3_0:
		phy->maximum_linkrate = SAS_LINK_RATE_3_0_GBPS;
		break;
	default:
		break;
	}

	/*
	 * Set Min hardware link rate.
	 */
	switch (phy_info->hw_link_rate & MPI_SAS_PHY0_HWRATE_MIN_RATE_MASK) {
	case MPI_SAS_PHY0_HWRATE_MIN_RATE_1_5:
		phy->minimum_linkrate_hw = SAS_LINK_RATE_1_5_GBPS;
		break;
	case MPI_SAS_PHY0_PRATE_MIN_RATE_3_0:
		phy->minimum_linkrate_hw = SAS_LINK_RATE_3_0_GBPS;
		break;
	default:
		break;
	}

	/*
	 * Set Min programmed link rate.
	 */
	switch (phy_info->programmed_link_rate &
			MPI_SAS_PHY0_PRATE_MIN_RATE_MASK) {
	case MPI_SAS_PHY0_PRATE_MIN_RATE_1_5:
		phy->minimum_linkrate = SAS_LINK_RATE_1_5_GBPS;
		break;
	case MPI_SAS_PHY0_PRATE_MIN_RATE_3_0:
		phy->minimum_linkrate = SAS_LINK_RATE_3_0_GBPS;
		break;
	default:
		break;
	}

	if (!phy_info->phy) {

#if !defined(MPT_WIDE_PORT_API_PLUS)
		if (local)
			phy->local_attached = 1;
#endif
		error = sas_phy_add(phy);
		if (error) {
			sas_phy_free(phy);
			goto out;
		}
		phy_info->phy = phy;
	}

	if (!phy_info->attached.handle ||
			!phy_info->port_details)
		goto out;

#if defined(MPT_WIDE_PORT_API)
	port = mptsas_get_port(phy_info);
#endif
	ioc = phy_to_ioc(phy_info->phy);

#if defined(MPT_WIDE_PORT_API)
	if (phy_info->sas_port_add_phy) {
		if (!port) {
			port = sas_port_alloc_num(dev);
			if (!port) {
				error = -ENOMEM;
				goto out;
			}
			error = sas_port_add(port);
			if (error) {
				dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
					"%s: exit at line=%d\n", ioc->name,
					__FUNCTION__, __LINE__));
				goto out;
			}
			mptsas_set_port(ioc, phy_info, port);
			devtprintk(ioc, dev_printk(KERN_DEBUG, &port->dev,
			    MYIOC_s_FMT "add port %d, sas_addr (0x%llx)\n",
			    ioc->name, port->port_identifier,
			    (unsigned long long)phy_info->attached.sas_address));
		}
		sas_port_add_phy(port, phy_info->phy);
		phy_info->sas_port_add_phy = 0;
		devtprintk(ioc, dev_printk(KERN_DEBUG, &phy_info->phy->dev,
		    MYIOC_s_FMT "add phy %d, phy-obj (0x%p)\n", ioc->name,
		     phy_info->phy_id, phy_info->phy));
	}
#else
	/*
	 * wide port suport
	 * only report the expander or end device once
	 */
	if (phy_info->attached.wide_port_enable &&
		(phy_info->phy_id != mptsas_get_rphy_id(phy_info)))
			goto out;

#endif

#if defined(MPT_WIDE_PORT_API)
	if (!mptsas_get_rphy(phy_info) && port && !port->rphy) {
#else
	if (!mptsas_get_rphy(phy_info)) {
#endif

		struct sas_rphy *rphy;
		struct device *parent;
		struct sas_identify identify;

		parent = dev->parent->parent;

		if (mptsas_is_end_device(&phy_info->attached) &&
		    phy_info->attached.handle_parent) {
			if (!ioc->sas_discovery_runtime)
				mptsas_add_end_device(ioc, phy_info);
			goto out;
		}

		mptsas_parse_device_info(&identify, &phy_info->attached);
		if (scsi_is_host_device(parent)) {
			struct mptsas_portinfo *port_info;
			int i;

			port_info = ioc->hba_port_info;
			for (i = 0; i < port_info->num_phys; i++)
				if (port_info->phy_info[i].identify.sas_address ==
				    identify.sas_address) {
#if defined(MPT_WIDE_PORT_API_PLUS)
					sas_port_mark_backlink(port);
#endif
					goto out;
			}

		} else if (scsi_is_sas_rphy(parent)) {
			struct sas_rphy *parent_rphy = dev_to_rphy(parent);
			if (identify.sas_address ==
			    parent_rphy->identify.sas_address) {
#if defined(MPT_WIDE_PORT_API_PLUS)
				sas_port_mark_backlink(port);
#endif
				goto out;
			}
		}

		switch (identify.device_type) {
		case SAS_END_DEVICE:
#if defined(MPT_WIDE_PORT_API)
			rphy = sas_end_device_alloc(port);
#else
			rphy = sas_end_device_alloc(phy);
#endif
			break;
		case SAS_EDGE_EXPANDER_DEVICE:
		case SAS_FANOUT_EXPANDER_DEVICE:
#if defined(MPT_WIDE_PORT_API)
			rphy = sas_expander_alloc(port, identify.device_type);
#else
			rphy = sas_expander_alloc(phy, identify.device_type);
#endif
			break;
		default:
			rphy = NULL;
			break;
		}
		if (!rphy) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
				__FUNCTION__, __LINE__));
			goto out;
		}

		rphy->identify = identify;
		error = sas_rphy_add(rphy);
		if (error) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
				__FUNCTION__, __LINE__));
			sas_rphy_free(rphy);
			goto out;
		}
		mptsas_set_rphy(ioc, phy_info, rphy);
	}

 out:
	return error;
}

/**
 *	mptsas_probe_hba_phys -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@handle:
 *
 **/
static int
mptsas_probe_hba_phys(MPT_ADAPTER *ioc)
{
	struct mptsas_portinfo *port_info, *hba;
	int error = -ENOMEM, i;

	hba = kzalloc(sizeof(*port_info), GFP_KERNEL);
	if (! hba)
		goto out;

	error = mptsas_sas_io_unit_pg0(ioc, hba);
	if (error)
		goto out_free_port_info;

	mptsas_sas_io_unit_pg1(ioc);
	mutex_lock(&ioc->sas_topology_mutex);
	port_info = ioc->hba_port_info;
	if (!port_info) {
		ioc->hba_port_info = port_info = hba;
		port_info->ioc = ioc;
		list_add_tail(&port_info->list, &ioc->sas_topology);
	} else {
		for (i = 0; i < hba->num_phys; i++) {
			port_info->phy_info[i].negotiated_link_rate =
				hba->phy_info[i].negotiated_link_rate;
			port_info->phy_info[i].handle =
				hba->phy_info[i].handle;
			port_info->phy_info[i].port_id =
				hba->phy_info[i].port_id;
#if defined(CPQ_CIM)
			port_info->phy_info[i].port_flags =
			    hba->phy_info[i].port_flags;
#endif
		}
		kfree(hba->phy_info);
		kfree(hba);
		hba = NULL;
	}
	mutex_unlock(&ioc->sas_topology_mutex);
#if defined(CPQ_CIM)
	ioc->num_ports = port_info->num_phys;
#endif
	for (i = 0; i < port_info->num_phys; i++) {
		mptsas_sas_phy_pg0(ioc, &port_info->phy_info[i],
			(MPI_SAS_PHY_PGAD_FORM_PHY_NUMBER <<
			 MPI_SAS_PHY_PGAD_FORM_SHIFT), i);
		port_info->phy_info[i].identify.handle =
		    port_info->phy_info[i].handle;
		mptsas_sas_device_pg0(ioc, &port_info->phy_info[i].identify,
			(MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
			 MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
			 port_info->phy_info[i].identify.handle);
		port_info->phy_info[i].identify.phy_id =
		    port_info->phy_info[i].phy_id = i;
		if (port_info->phy_info[i].attached.handle)
			mptsas_sas_device_pg0(ioc,
				&port_info->phy_info[i].attached,
				(MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
				 MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
				port_info->phy_info[i].attached.handle);
	}

	mptsas_setup_wide_ports(ioc, port_info);

	for (i = 0; i < port_info->num_phys; i++, ioc->sas_index++)
		mptsas_probe_one_phy(&ioc->sh->shost_gendev,
		    &port_info->phy_info[i], ioc->sas_index, 1);

	return 0;

 out_free_port_info:
	kfree(hba);
 out:
	return error;
}

/**
 *	mptsas_add_expander - refresh or add new expander properties
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@port_info: expander port_info struct
 *	@is_new: is this a new expander being added (1:yes, 0:no)
 *
 **/
static void
mptsas_add_expander(MPT_ADAPTER *ioc, struct mptsas_portinfo *port_info, u8 is_new)
{
	struct mptsas_portinfo *p;
	struct device	*parent;
	struct sas_rphy	*rphy;
	int		i;
	u64		expander_sas_address;
	u32		handle;

	handle = port_info->phy_info[0].handle;
	expander_sas_address = port_info->phy_info[0].identify.sas_address;
	for (i = 0; i < port_info->num_phys; i++) {
		mptsas_sas_expander_pg1(ioc, &port_info->phy_info[i],
			(MPI_SAS_EXPAND_PGAD_FORM_HANDLE_PHY_NUM <<
			 MPI_SAS_EXPAND_PGAD_FORM_SHIFT), (i << 16) + handle);

		mptsas_sas_device_pg0(ioc,
			&port_info->phy_info[i].identify,
			(MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
			 MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
			port_info->phy_info[i].identify.handle);
		port_info->phy_info[i].identify.phy_id =
		    port_info->phy_info[i].phy_id;

		if (port_info->phy_info[i].attached.handle) {
			mptsas_sas_device_pg0(ioc,
				&port_info->phy_info[i].attached,
				(MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
				 MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
				port_info->phy_info[i].attached.handle);
			port_info->phy_info[i].attached.phy_id =
			    port_info->phy_info[i].phy_id;
		}
	}

	if (is_new)
		printk(MYIOC_s_INFO_FMT "add expander: num_phys %d, "
		    "sas_addr (0x%llx)\n", ioc->name, port_info->num_phys,
		    (unsigned long long)expander_sas_address);

	parent = NULL;
	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(p, &ioc->sas_topology, list) {
		for (i = 0; i < p->num_phys && parent == NULL; i++) {
			if (expander_sas_address !=
			    p->phy_info[i].attached.sas_address)
				continue;
			rphy = mptsas_get_rphy(&p->phy_info[i]);
			parent = &rphy->dev;
		}
	}
	mutex_unlock(&ioc->sas_topology_mutex);

	if (!parent)
		parent = &ioc->sh->shost_gendev;

	mptsas_setup_wide_ports(ioc, port_info);

	for (i = 0; i < port_info->num_phys; i++, ioc->sas_index++)
		mptsas_probe_one_phy(parent, &port_info->phy_info[i],
		    ioc->sas_index, 0);
}

/**
 *	mptsas_probe_expander_phys - probing for new expanders, and refreshing existing
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@handle:
 *
 **/
static void
mptsas_probe_expander_phys(MPT_ADAPTER *ioc)
{
	struct mptsas_portinfo buffer;
	struct mptsas_portinfo *port_info;
	u32 			handle;
	int			i;
	u8			add_new_expander;

	handle = 0xFFFF;
	while (!mptsas_sas_expander_pg0(ioc, &buffer,
	    (MPI_SAS_EXPAND_PGAD_FORM_GET_NEXT_HANDLE <<
	     MPI_SAS_EXPAND_PGAD_FORM_SHIFT), handle)) {

		handle = buffer.phy_info[0].handle;
		port_info = mptsas_find_portinfo_by_sas_address(ioc,
		    buffer.phy_info[0].identify.sas_address);
		if (!port_info) {
			port_info = kzalloc(sizeof(*port_info), GFP_KERNEL);
			if (!port_info) {
				dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
				"%s: exit at line=%d\n", ioc->name,
				__FUNCTION__, __LINE__));
				return;
			}
			port_info->num_phys = buffer.num_phys;
			port_info->phy_info = buffer.phy_info;
			for (i = 0; i < port_info->num_phys; i++)
				port_info->phy_info[i].portinfo = port_info;
			mutex_lock(&ioc->sas_topology_mutex);
			list_add_tail(&port_info->list, &ioc->sas_topology);
			mutex_unlock(&ioc->sas_topology_mutex);
			port_info->ioc = ioc;
			add_new_expander = 1;
		} else {
			add_new_expander = 0;
			for (i = 0; i < buffer.num_phys; i++) {
				port_info->phy_info[i].handle =
				    buffer.phy_info[i].handle;
				port_info->phy_info[i].port_id =
				    buffer.phy_info[i].port_id;
			}
			kfree(buffer.phy_info);
		}
		mptsas_add_expander(ioc, port_info, add_new_expander);
	}
}

/**
 *	mptsas_delete_expander - remove this expander
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@port_info: expander port_info struct
 *
 **/
static void
mptsas_delete_expander(MPT_ADAPTER *ioc, struct mptsas_portinfo *port_info)
{
	struct mptsas_portinfo *parent;
	int		i;
	u64		expander_sas_address;
	char		*ds = NULL;
	struct mptsas_phyinfo *phy_info;
#if defined(MPT_WIDE_PORT_API)
	struct sas_port * port;
	struct mptsas_portinfo_details * port_details;
#else
	struct sas_rphy * rphy;
#endif

	/*
	 * Obtain the port_info instance to the parent port
	 */
	expander_sas_address =
	    port_info->phy_info[0].identify.sas_address;
	parent = mptsas_find_portinfo_by_handle(ioc,
	    port_info->phy_info[0].identify.handle_parent);
	if (!parent)
		goto remove_end_devices;

	/*
	 * Delete rphys in the parent that point
	 * to this expander.  The transport layer will
	 * cleanup all the children.
	 */
#if defined(MPT_WIDE_PORT_API)
	phy_info = parent->phy_info;
	port_details = NULL;
	port = NULL;
	for (i = 0; i < parent->num_phys; i++, phy_info++) {
		if(!phy_info->phy)
			continue;
		if (phy_info->attached.sas_address !=
		    expander_sas_address)
			continue;
		if (!port) {
			port = mptsas_get_port(phy_info);
			port_details = phy_info->port_details;
		}
		devtprintk(ioc, dev_printk(KERN_DEBUG, &phy_info->phy->dev,
		    MYIOC_s_FMT "delete phy %d, phy-obj (0x%p)\n", ioc->name,
		    phy_info->phy_id, phy_info->phy));
		sas_port_delete_phy(port, phy_info->phy);
		phy_info->phy = NULL;
	}
	if (port && port_details) {
		devtprintk(ioc, dev_printk(KERN_DEBUG, &port->dev,
		    MYIOC_s_FMT "delete port %d, sas_addr (0x%llx)\n",
		    ioc->name, port->port_identifier,
		    (unsigned long long)expander_sas_address));
		sas_port_delete(port);
		mptsas_port_delete(ioc, port_details);
	}
#else
	phy_info = parent->phy_info;
	for (i = 0; i < parent->num_phys; i++, phy_info++) {
		rphy = mptsas_get_rphy(phy_info);
		if (!rphy)
			continue;
		if (phy_info->attached.sas_address !=
		    expander_sas_address)
			continue;
		dev_printk(KERN_DEBUG, &rphy->dev,
		    MYIOC_s_FMT "delete: sas_addr (0x%llx)\n",
		    ioc->name, (unsigned long long) expander_sas_address);
		sas_rphy_delete(rphy);
		mptsas_port_delete(ioc, phy_info->port_details);
	}
#endif
 remove_end_devices:

	printk(MYIOC_s_INFO_FMT "delete expander: num_phys %d, sas_addr (0x%llx)\n",
	    ioc->name, port_info->num_phys,
	    (unsigned long long)expander_sas_address);

	/*
	 * removing end devices
	 */
	phy_info = port_info->phy_info;
	for (i = 0; i < port_info->num_phys; i++, phy_info++) {
		if (!phy_info->port_details)
			continue;
		if (phy_info->attached.sas_address) {
			ds = NULL;
			if (phy_info->attached.device_info &
			    MPI_SAS_DEVICE_INFO_SSP_TARGET)
				ds = "ssp";
			if (phy_info->attached.device_info &
			    MPI_SAS_DEVICE_INFO_STP_TARGET)
				ds = "stp";
			if (phy_info->attached.device_info &
			    MPI_SAS_DEVICE_INFO_SATA_DEVICE)
				ds = "sata";
			if (ds)
				printk(MYIOC_s_INFO_FMT
				"removing %s device: fw_channel %d, fw_id %d,"
				" phy %d, sas_addr 0x%llx\n",
				ioc->name, ds, phy_info->attached.channel,
				phy_info->attached.id, phy_info->attached.phy_id,
				(unsigned long long)phy_info->attached.sas_address);
#if defined(__VMKLNX__)
			/* delete end devices prior to expander deletion */
			if ((!ioc->disable_hotplug_remove) && ds) {
				mptsas_del_end_device(ioc, phy_info);
			}
#endif
		}
		mptsas_port_delete(ioc, phy_info->port_details);
	}

	/*
	 * free link
	 */
	list_del(&port_info->list);
	kfree(port_info->phy_info);
	kfree(port_info);
}

/**
 *	mptsas_link_status_work -
 *	    delayed worktask handling link status change
 *	@work: Pointer to mptsas_link_status_event structure
 *
 **/
static void
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
mptsas_link_status_work(struct work_struct *work)
{
	struct mptsas_link_status_event *ev =
		container_of(work, struct mptsas_link_status_event, work);
#else
mptsas_link_status_work(void * arg)
{
	struct mptsas_link_status_event *ev = arg;
#endif
	struct mptsas_portinfo *port_info;
	struct mptsas_phyinfo *phy_info;
	__le64 sas_address;
	MPT_ADAPTER *ioc;
	u8 phy_num;
	u8 link_rate;

	ioc = ev->ioc;
	if (ioc->sas_discovery_ignore_events) {
		kfree(ev);
		return;
	}

	memcpy(&sas_address, &ev->link_data.SASAddress, sizeof(__le64));
	sas_address = le64_to_cpu(sas_address);
	link_rate = ev->link_data.LinkRates >> 4;
	phy_num = ev->link_data.PhyNum;

	mutex_lock(&ioc->sas_discovery_mutex);
	ioc->sas_discovery_runtime = 1;
	scsi_block_requests(ioc->sh);
	port_info = mptsas_find_portinfo_by_sas_address(ioc, sas_address);
	if (!port_info)
		goto out;
	phy_info = &port_info->phy_info[phy_num];
	if (!phy_info)
		goto out;
	phy_info->negotiated_link_rate = link_rate;

#if defined(__VMKLNX__)
        if (phy_info->port_details && phy_info->port_details->starget &&
            ((link_rate == MPI_SAS_IOUNIT0_RATE_1_5) || (link_rate == MPI_SAS_IOUNIT0_RATE_3_0))) {
                VirtTarget *vtarget = phy_info->port_details->starget->hostdata;                if (vtarget) {
                        dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT "link_status calling mptsas_connect_devices VC%d:VT%d HeaderVersion=0x%x\n", ioc->name, vtarget->channel, vtarget->id, (ioc->facts.HeaderVersion >> 8)));
                        mptsas_connect_devices(ioc, vtarget->channel, vtarget->id);
                }
        }
        /* Skip expander handling for older version of FW since it will walk
         * entire topology tree to realize changes
         */
        if (!((ioc->facts.HeaderVersion >> 8) >= 0xE))
                goto out;
#endif
	if (link_rate == MPI_SAS_IOUNIT0_RATE_1_5 ||
	    link_rate == MPI_SAS_IOUNIT0_RATE_3_0) {
		if (port_info == ioc->hba_port_info)
			mptsas_probe_hba_phys(ioc);
		else
			mptsas_add_expander(ioc, port_info, 0);
	} else if (phy_info->phy) {
		if (link_rate ==  MPI_SAS_IOUNIT0_RATE_PHY_DISABLED)
			phy_info->phy->negotiated_linkrate =
			    SAS_PHY_DISABLED;
		else if (link_rate ==
		    MPI_SAS_IOUNIT0_RATE_FAILED_SPEED_NEGOTIATION)
			phy_info->phy->negotiated_linkrate =
			    SAS_LINK_RATE_FAILED;
		else
			phy_info->phy->negotiated_linkrate =
			    SAS_LINK_RATE_UNKNOWN;
	}
 out:
	ioc->sas_discovery_runtime = 0;
	scsi_unblock_requests(ioc->sh);
	mutex_unlock(&ioc->sas_discovery_mutex);
	kfree(ev);
}

/**
 *	mptsas_expander_add_work -
 *	    delayed worktask handling adding expanders
 *	@work: Pointer to port_info structure
 *
 **/
static void
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
mptsas_expander_add_work(struct work_struct *work)
{
	struct mptsas_portinfo *buffer =
		container_of(work, struct mptsas_portinfo, add_work.work);
#else
mptsas_expander_add_work(void * arg)
{
	struct mptsas_portinfo *buffer = arg;
#endif
	MPT_ADAPTER *ioc;
	struct mptsas_portinfo *port_info;
	int i;

	ioc = buffer->ioc;
	mutex_lock(&ioc->sas_discovery_mutex);
	ioc->sas_discovery_runtime = 1;
	scsi_block_requests(ioc->sh);
	port_info = mptsas_find_portinfo_by_sas_address(ioc,
	    buffer->phy_info[0].identify.sas_address);

	if (port_info) {
		for (i = 0; i < port_info->num_phys; i++) {
			port_info->phy_info[i].portinfo = port_info;
			port_info->phy_info[i].handle =
			    buffer->phy_info[i].handle;
			port_info->phy_info[i].identify.sas_address =
			    buffer->phy_info[i].identify.sas_address;
			port_info->phy_info[i].identify.handle_parent =
			    buffer->phy_info[i].identify.handle_parent;
		}
		mptsas_add_expander(ioc, port_info, 0);
		kfree(buffer->phy_info);
		kfree(buffer);
	} else {
		port_info = buffer;
		mutex_lock(&ioc->sas_topology_mutex);
		list_add_tail(&port_info->list, &ioc->sas_topology);
		mutex_unlock(&ioc->sas_topology_mutex);
		mptsas_add_expander(ioc, port_info, 1);
	}
	ioc->sas_discovery_runtime = 0;
	scsi_unblock_requests(ioc->sh);
	mutex_unlock(&ioc->sas_discovery_mutex);
}

/**
 *	mptsas_expander_delete_work -
 *	    delayed worktask handling removal of expanders
 * 	that are no longer present
 *	@work: Pointer to port_info structure
 *
 **/
static void
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
mptsas_expander_delete_work(struct work_struct *work)
{
	struct mptsas_portinfo *buffer =
		container_of(work, struct mptsas_portinfo, del_work.work);
#else
mptsas_expander_delete_work(void * arg)
{
	struct mptsas_portinfo *buffer = arg;
#endif
	MPT_ADAPTER *ioc;
	int rc;
	struct mptsas_portinfo *port_info, tmp;

	ioc = buffer->ioc;
	mutex_lock(&ioc->sas_discovery_mutex);
	port_info = mptsas_find_portinfo_by_sas_address(ioc,
	    buffer->phy_info[0].identify.sas_address);

	/* In 1.05.12 firmware expander events were added, and we need to free
	 * this memory that was allocated in mptsas_send_expander_event */
	if ((ioc->facts.HeaderVersion >> 8) >= 0xE) {
		kfree(buffer->phy_info);
		kfree(buffer);
	}

	if (!port_info)
		goto out;

	rc = mptsas_sas_expander_pg0(ioc, &tmp,
	    (MPI_SAS_EXPAND_PGAD_FORM_HANDLE <<
	    MPI_SAS_EXPAND_PGAD_FORM_SHIFT),
	    port_info->phy_info[0].handle);
	kfree(tmp.phy_info);
	if (rc) {
		mutex_lock(&ioc->sas_topology_mutex);
		mptsas_delete_expander(ioc, port_info);
		mutex_unlock(&ioc->sas_topology_mutex);
	}
	port_info->del_work_scheduled=0;

 out:
	mutex_unlock(&ioc->sas_discovery_mutex);
}

/**
 *	mptsas_remove_expanders_not_responding -
 *	    this will traverse topology removing not responding expanders
 * 	that are no longer present
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 **/
static void
mptsas_remove_expanders_not_responding(MPT_ADAPTER *ioc)
{
	struct mptsas_portinfo buffer;
	struct mptsas_portinfo *port_info, *n;
	int			rc;

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry_safe(port_info, n, &ioc->sas_topology, list) {

		if (port_info->phy_info &&
		    (!(port_info->phy_info[0].identify.device_info &
		    MPI_SAS_DEVICE_INFO_SMP_TARGET)))
			continue;

		rc = mptsas_sas_expander_pg0(ioc, &buffer,
		    (MPI_SAS_EXPAND_PGAD_FORM_HANDLE <<
		    MPI_SAS_EXPAND_PGAD_FORM_SHIFT),
		    port_info->phy_info[0].handle);

		kfree(buffer.phy_info);
		if (!rc)
			continue;
		/* handle missing expanders */
		if (ioc->device_missing_delay) {
			if(port_info->del_work_scheduled) {
				cancel_delayed_work(&port_info->del_work);
				port_info->del_work_scheduled=0;
			}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
			INIT_DELAYED_WORK(&port_info->del_work,
			    mptsas_expander_delete_work);
#else
			INIT_WORK(&port_info->del_work,
			    mptsas_expander_delete_work, port_info);
#endif
			schedule_delayed_work(&port_info->del_work,
			    HZ * ioc->device_missing_delay);
			port_info->del_work_scheduled=1;
		} else
			mptsas_delete_expander(ioc, port_info);
	}
	mutex_unlock(&ioc->sas_topology_mutex);
}

/**
 *	mptsas_scan_sas_topology - Start of day discovery
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@sas_address:
 *
 **/
static void
mptsas_scan_sas_topology(MPT_ADAPTER *ioc)
{
	int i;

	mutex_lock(&ioc->sas_discovery_mutex);
	mptsas_probe_hba_phys(ioc);
	mptsas_probe_expander_phys(ioc);
	/*
	  Reporting RAID volumes.
	*/
	if (!ioc->ir_firmware)
		goto out;
	if (!ioc->raid_data.pIocPg2)
		goto out;
	if (!ioc->raid_data.pIocPg2->NumActiveVolumes)
		goto out;
	for (i=0; i < ioc->raid_data.pIocPg2->NumActiveVolumes; i++) {
		printk(MYIOC_s_INFO_FMT "attaching raid volume, channel %d, "
		    "id %d\n", ioc->name, MPTSAS_RAID_CHANNEL,
		    ioc->raid_data.pIocPg2->RaidVolume[i].VolumeID);
		scsi_add_device(ioc->sh, MPTSAS_RAID_CHANNEL,
		    ioc->raid_data.pIocPg2->RaidVolume[i].VolumeID, 0);
	}
 out:
	mutex_unlock(&ioc->sas_discovery_mutex);
}

/**
 *	mptsas_discovery_work - Work queue thread to handle Runtime discovery,
 *	with the mere purpose is the hot add/delete of expanders
 *	@work: work queue payload containing the event info
 *
 *	(Mutex LOCKED)
 **/
static void
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
mptsas_discovery_work(struct work_struct *work)
{
	struct mptsas_discovery_event *ev =
		container_of(work, struct mptsas_discovery_event, work);
#else
mptsas_discovery_work(void * arg)
{
	struct mptsas_discovery_event *ev = arg;
#endif
	MPT_ADAPTER *ioc = ev->ioc;

	mutex_lock(&ioc->sas_discovery_mutex);
	ioc->sas_discovery_runtime = 1;
	if (!ioc->disable_hotplug_remove)
		mptsas_remove_expanders_not_responding(ioc);
	scsi_block_requests(ioc->sh);
	mptsas_probe_hba_phys(ioc);
	mptsas_probe_expander_phys(ioc);
	ioc->sas_discovery_runtime = 0;
	scsi_unblock_requests(ioc->sh);
	mutex_unlock(&ioc->sas_discovery_mutex);
	kfree(ev);
}

/**
 *	mptsas_find_phyinfo_by_sas_address -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@sas_address:
 *
 **/
static struct mptsas_phyinfo *
mptsas_find_phyinfo_by_sas_address(MPT_ADAPTER *ioc, u64 sas_address)
{
	struct mptsas_portinfo *port_info;
	struct mptsas_phyinfo *phy_info = NULL;
	int i;

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(port_info, &ioc->sas_topology, list) {
		for (i = 0; i < port_info->num_phys; i++) {
			if (!mptsas_is_end_device(
				&port_info->phy_info[i].attached))
				continue;
			if (port_info->phy_info[i].attached.sas_address
			    != sas_address)
				continue;
			phy_info = &port_info->phy_info[i];
			break;
		}
	}
	mutex_unlock(&ioc->sas_topology_mutex);
	return phy_info;
}
#if defined(__VMKLNX__)
/*
 * Exact implementation of mptsas_find_phyinfo_by_sas_address except callers
 * needs to lock/unlock sas_topology_mutex.
 */
static struct mptsas_phyinfo *
mptsas_find_phyinfo_by_sas_address_NL(MPT_ADAPTER *ioc, u64 sas_address) {
	struct mptsas_portinfo *port_info;
	struct mptsas_phyinfo *phy_info = NULL;
	int i;

	list_for_each_entry(port_info, &ioc->sas_topology, list) {
		for (i = 0; i < port_info->num_phys; i++) {
			if (!mptsas_is_end_device(
				&port_info->phy_info[i].attached))
				continue;
			if (port_info->phy_info[i].attached.sas_address
			    != sas_address)
				continue;
			phy_info = &port_info->phy_info[i];
			break;
		}
	}
	return phy_info;
}
#endif

/**
 *	mptsas_find_phyinfo_by_phys_disk_num -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@phys_disk_num:
 *	@channel:
 *	@id:
 *
 **/
static struct mptsas_phyinfo *
mptsas_find_phyinfo_by_phys_disk_num(MPT_ADAPTER *ioc, u8 phys_disk_num, u8 channel, u8 id)
{
	struct mptsas_phyinfo *phy_info;
	struct mptsas_portinfo *port_info;
	RaidPhysDiskPage1_t *phys_disk = NULL;
	int num_paths;
	u64 sas_address = 0;
	int i;

	phy_info = NULL;
	if (!ioc->raid_data.pIocPg3)
		return NULL;
	/* dual port support */
	num_paths = mpt_raid_phys_disk_get_num_paths(ioc, phys_disk_num);
	if (!num_paths)
		goto out;
	phys_disk = kzalloc(offsetof(RaidPhysDiskPage1_t,Path) +
	   (num_paths * sizeof(RAID_PHYS_DISK1_PATH)), GFP_KERNEL);
	if (!phys_disk)
		goto out;
	mpt_raid_phys_disk_pg1(ioc, phys_disk_num, phys_disk);
	for (i = 0; i < num_paths; i++) {
		if ((phys_disk->Path[i].Flags & 1) != 0) /* entry no longer valid */
			continue;
		if ((id == phys_disk->Path[i].PhysDiskID) &&
		    (channel == phys_disk->Path[i].PhysDiskBus)) {
			memcpy(&sas_address, &phys_disk->Path[i].WWID, sizeof(u64));
			phy_info = mptsas_find_phyinfo_by_sas_address(ioc, sas_address);
			goto out;
		}
	}

 out:
	kfree(phys_disk);
	if (phy_info)
		return phy_info;

	/*
	 * Extra code to handle RAID0 case, where the sas_address is not updated
	 * in phys_disk_page_1 when hotswapped
	 */
	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry(port_info, &ioc->sas_topology, list) {
		for (i = 0; i < port_info->num_phys && !phy_info; i++) {
			if (!mptsas_is_end_device(
				&port_info->phy_info[i].attached))
				continue;
			if (port_info->phy_info[i].attached.phys_disk_num == ~0)
				continue;
			if (port_info->phy_info[i].attached.phys_disk_num == phys_disk_num &&
			    port_info->phy_info[i].attached.id == id &&
			    port_info->phy_info[i].attached.channel == channel)
				phy_info = &port_info->phy_info[i];
		}
	}
	mutex_unlock(&ioc->sas_topology_mutex);
	return phy_info;
}

/**
 *	mptsas_persist_clear_table - Work queue thread to clear the persitency table
 *	@work: work queue payload containing the MPT_ADAPTER structure
 *
 **/
static void
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
mptsas_persist_clear_table(struct work_struct *work)
{
	MPT_ADAPTER *ioc = container_of(work, MPT_ADAPTER, sas_persist_task);
#else
mptsas_persist_clear_table(void * arg)
{
	MPT_ADAPTER *ioc = (MPT_ADAPTER *)arg;
#endif

	mptbase_sas_persist_operation(ioc, MPI_SAS_OP_CLEAR_NOT_PRESENT);
}

/**
 *	mptsas_reprobe_lun -
 *	@sdev:
 *	@data:
 *
 **/
static void
mptsas_reprobe_lun(struct scsi_device *sdev, void *data)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
        int rc;
#endif
	sdev->no_uld_attach = data ? 1 : 0;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
	rc = scsi_device_reprobe(sdev);
#else
	scsi_device_reprobe(sdev);
#endif
}

/**
 *	mptsas_reprobe_target -
 *	@starget:
 *	@uld_attach:
 *
 **/
static void
mptsas_reprobe_target(struct scsi_target *starget, int uld_attach)
{
	starget_for_each_device(starget, uld_attach ? (void *)1 : NULL,
			mptsas_reprobe_lun);
}

/**
 *	mptsas_adding_inactive_raid_components -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@channel:
 *	@id:
 *
 *
 *	 TODO: check for hotspares
 **/
static void
mptsas_adding_inactive_raid_components(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	CONFIGPARMS			cfg;
	ConfigPageHeader_t		hdr;
	dma_addr_t			dma_handle;
	pRaidVolumePage0_t		buffer = NULL;
	RaidPhysDiskPage0_t 		phys_disk;
	int				i;
	struct mptsas_phyinfo		*phy_info;
	struct mptsas_devinfo		sas_device;

	memset(&cfg, 0 , sizeof(CONFIGPARMS));
	memset(&hdr, 0 , sizeof(ConfigPageHeader_t));
	hdr.PageType = MPI_CONFIG_PAGETYPE_RAID_VOLUME;
	cfg.pageAddr = (channel << 8) + id;
	cfg.cfghdr.hdr = &hdr;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.timeout = SAS_CONFIG_PAGE_TIMEOUT;

	if (mpt_config(ioc, &cfg) != 0)
		goto out;

	if (!hdr.PageLength)
		goto out;

	buffer = pci_alloc_consistent(ioc->pcidev, hdr.PageLength * 4,
	    &dma_handle);

	if (!buffer)
		goto out;

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if (mpt_config(ioc, &cfg) != 0)
		goto out;

	if (!(buffer->VolumeStatus.Flags &
	    MPI_RAIDVOL0_STATUS_FLAG_VOLUME_INACTIVE))
		goto out;

	if (!buffer->NumPhysDisks)
		goto out;

	for (i = 0; i < buffer->NumPhysDisks; i++) {

		if (mpt_raid_phys_disk_pg0(ioc,
		    buffer->PhysDisk[i].PhysDiskNum, &phys_disk) != 0)
			continue;

		if (mptsas_sas_device_pg0(ioc, &sas_device,
		    (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
		     MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
			(phys_disk.PhysDiskBus << 8) +
			phys_disk.PhysDiskID))
			continue;

		phy_info = mptsas_find_phyinfo_by_sas_address(ioc,
		    sas_device.sas_address);
		mptsas_add_end_device(ioc, phy_info);
	}

 out:
	if (buffer)
		pci_free_consistent(ioc->pcidev, hdr.PageLength * 4, buffer,
		    dma_handle);
}

/**
 *	mptsas_add_end_device - report a new end device to sas transport layer
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@phy_info: decribes attached device
 *
 *	return (0) success (1) failure
 *
 **/
static int
mptsas_add_end_device(MPT_ADAPTER *ioc, struct mptsas_phyinfo *phy_info)
{
	struct sas_rphy *rphy;
#if defined(MPT_WIDE_PORT_API)
	struct sas_port *port;
#endif
	struct sas_identify identify;
	char *ds = NULL;
	u8 fw_id;

	if (!phy_info){
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: exit at line=%d\n", ioc->name,
		       	__FUNCTION__, __LINE__));
		return 1;
	}

	fw_id = phy_info->attached.id;

	if (mptsas_get_rphy(phy_info)) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
		       	__FUNCTION__, fw_id, __LINE__));
		return 2;
	}

#if defined(MPT_WIDE_PORT_API)
	port = mptsas_get_port(phy_info);
	if (!port) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
		       	__FUNCTION__, fw_id, __LINE__));
		return 3;
	}
#endif

	if (mptsas_test_unit_ready(ioc, phy_info->attached.channel,
	    phy_info->attached.id) != 0) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
			__FUNCTION__, fw_id, __LINE__));
		return 4;
	}

	if (phy_info->attached.device_info &
	    MPI_SAS_DEVICE_INFO_SSP_TARGET)
		ds = "ssp";
	if (phy_info->attached.device_info &
	    MPI_SAS_DEVICE_INFO_STP_TARGET)
		ds = "stp";
	if (phy_info->attached.device_info &
	    MPI_SAS_DEVICE_INFO_SATA_DEVICE)
		ds = "sata";

	printk(MYIOC_s_INFO_FMT "attaching %s device: fw_channel %d, fw_id %d,"
	    " phy %d, sas_addr 0x%llx\n", ioc->name, ds,
	    phy_info->attached.channel, phy_info->attached.id,
	    phy_info->attached.phy_id, (unsigned long long)
	    phy_info->attached.sas_address);

	mptsas_parse_device_info(&identify, &phy_info->attached);
#if defined(MPT_WIDE_PORT_API)
	rphy = sas_end_device_alloc(port);
#else
	rphy = sas_end_device_alloc(phy_info->phy);
#endif
	if (!rphy) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
		       	__FUNCTION__, fw_id, __LINE__));
		return 5; /* non-fatal: an rphy can be added later */
	}

	rphy->identify = identify;
	if (sas_rphy_add(rphy)) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
		       	__FUNCTION__, fw_id, __LINE__));
		sas_rphy_free(rphy);
		return 6;
	}
	mptsas_set_rphy(ioc, phy_info, rphy);
	return 0;
}

/**
 *	mptsas_del_end_device - report a deleted end device to sas transport
 *	layer
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@phy_info: decribes attached device
 *
 **/
static void
mptsas_del_end_device(MPT_ADAPTER *ioc, struct mptsas_phyinfo *phy_info)
{
	struct sas_rphy *rphy;
#if defined(MPT_WIDE_PORT_API)
	struct sas_port *port;
	struct mptsas_portinfo *port_info;
	struct mptsas_phyinfo *phy_info_lookup;
	int i;
#endif
	struct scsi_target * starget;
	char *ds = NULL;
	u8 fw_id;
	u64 sas_address;

	if (!phy_info){
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: exit at line=%d\n", ioc->name,
		       	__FUNCTION__, __LINE__));
		return;
	}

	fw_id = phy_info->attached.id;
	sas_address = phy_info->attached.sas_address;

	if (!phy_info->port_details) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
		       	__FUNCTION__, fw_id, __LINE__));
		return;
	}
	rphy = mptsas_get_rphy(phy_info);
	if (!rphy) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
		       	__FUNCTION__, fw_id, __LINE__));
		return;
	}
#if defined(MPT_WIDE_PORT_API)
	port = mptsas_get_port(phy_info);
	if (!port) {
		dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
		       	__FUNCTION__, fw_id, __LINE__));
		return;
	}
	port_info = phy_info->portinfo;
	phy_info_lookup = port_info->phy_info;
	for (i = 0; i < port_info->num_phys; i++, phy_info_lookup++) {
		if(!phy_info_lookup->phy)
			continue;
		if (phy_info_lookup->attached.sas_address !=
		    sas_address)
			continue;
		devtprintk(ioc, dev_printk(KERN_DEBUG, &phy_info_lookup->phy->dev,
		    MYIOC_s_FMT "delete phy %d, phy-obj (0x%p)\n",
		    ioc->name, phy_info_lookup->phy_id,
		    phy_info_lookup->phy));
		sas_port_delete_phy(port, phy_info_lookup->phy);
		phy_info_lookup->phy = NULL;
	}
#endif
	if (phy_info->attached.device_info &
	    MPI_SAS_DEVICE_INFO_SSP_TARGET)
		ds = "ssp";
	if (phy_info->attached.device_info &
	    MPI_SAS_DEVICE_INFO_STP_TARGET)
		ds = "stp";
	if (phy_info->attached.device_info &
	    MPI_SAS_DEVICE_INFO_SATA_DEVICE)
		ds = "sata";

	starget = mptsas_get_starget(phy_info);
	if (starget)
		starget_printk(KERN_INFO, starget, MYIOC_s_FMT "removing %s device: fw_channel %d,"
		    " fw_id %d, phy %d, sas_addr 0x%llx\n", ioc->name, ds,
		    phy_info->attached.channel, phy_info->attached.id,
		    phy_info->attached.phy_id, (unsigned long long)
		    sas_address);
	else
		printk(MYIOC_s_INFO_FMT "removing %s device: fw_channel %d,"
		    " fw_id %d, phy %d, sas_addr 0x%llx\n", ioc->name, ds,
		    phy_info->attached.channel, phy_info->attached.id,
		    phy_info->attached.phy_id, (unsigned long long)
		    sas_address);

#if defined(MPT_WIDE_PORT_API)
	devtprintk(ioc, dev_printk(KERN_DEBUG, &port->dev, MYIOC_s_FMT
	    "delete port %d, sas_addr (0x%llx)\n", ioc->name,
	     port->port_identifier, (unsigned long long)sas_address));
	sas_port_delete(port);
#else
	sas_rphy_delete(rphy);
#endif
	mptsas_port_delete(ioc, phy_info->port_details);
}

/**
 *	mptsas_hotplug_work - Work queue thread to handle SAS hotplug events
 *	@work: work queue payload containing info describing the event
 *
 **/
static void
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
mptsas_hotplug_work(struct work_struct *work)
{
	struct mptsas_hotplug_event *ev =
		container_of(work, struct mptsas_hotplug_event, hotplug_work.work);
#else
mptsas_hotplug_work(void *arg)
{
	struct mptsas_hotplug_event *ev = arg;
#endif

	MPT_ADAPTER *ioc = ev->ioc;
	struct mptsas_phyinfo *phy_info;
	struct scsi_target * starget;
	struct mptsas_devinfo sas_device;
	struct mptsas_portinfo * port_info;
	VirtTarget *vtarget;
	int i;

	switch (ev->event_type) {

	case MPTSAS_ADD_DEVICE:
		mutex_lock(&ioc->sas_discovery_mutex);
		phy_info = mptsas_find_phyinfo_by_sas_address(ioc,
		    ev->sas_address);
		if (phy_info){
			port_info = phy_info->portinfo;
			if (port_info) {
				for (i = 0; i < port_info->num_phys; i++)
				    if(port_info->phy_info[i].attached.sas_address
				        == ev->sas_address)
					port_info->phy_info[i].attached.id = ev->id;
			}
#if defined(__VMKLNX__)
			mptsas_connect_devices(ioc, ev->channel, ev->id);
			mptsas_add_end_device(ioc, phy_info);
			/*
			 * In case, a cable is inserted to a port where it is
                         * masked out by storage array manager and rphy
                         * information will still be NULL.
                         * Checking is necessary prior to target scan. 
			 */
			if (phy_info->port_details &&
			    phy_info->port_details->rphy) {
			    scsi_scan_target(&phy_info->port_details->rphy->dev,
				0, phy_info->port_details->rphy->scsi_target_id,
				~0, 1);
			}
#else
			mptsas_add_end_device(ioc, phy_info);
#endif /* defined(__VMKLNX__) */
			mutex_unlock(&ioc->sas_discovery_mutex);
			break;
		}

		mutex_unlock(&ioc->sas_discovery_mutex);
		/*
		 * retry later if the phy_info wasn't created
		 */
		if (ev->retries == 12)
			break;
		dfailprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: line=%d: fw_id=%d retry_add (%d)\n", ioc->name,
		    __FUNCTION__, __LINE__, ev->id, ev->retries));
		schedule_delayed_work(&ev->hotplug_work, 5*HZ);
		ev->retries++;
		return;

	case MPTSAS_DEL_DEVICE:

		mutex_lock(&ioc->sas_discovery_mutex);
		if (!ioc->disable_hotplug_remove) {
#if defined(__VMKLNX__)
                /*
                 * mptsas_delete_expander and hotplug_work end_device deletion
                 * can occur the same time. sas_topology_mutex is used to make
                 * sure end devices are either deleted here or through
                 * mptsas_del_end_device in mptsas_delete_expander;
                 * see PR283476 comment #77. The scope of sas_topology_mutex 
                 * needs to be locked through out mptsas_del_end_device.
                 * mptsas_find_phyinfo_by_sas_address_NL is introduced to avoid
                 * mutex locking in mptsas_find_phyinfo_by_sas_address. 
                 */
			mutex_lock(&ioc->sas_topology_mutex);
			phy_info = mptsas_find_phyinfo_by_sas_address_NL(ioc,
			    ev->sas_address);
#else
			phy_info = mptsas_find_phyinfo_by_sas_address(ioc,
			    ev->sas_address);
#endif
			mptsas_del_end_device(ioc, phy_info);
#if defined(__VMKLNX__)
			mutex_unlock(&ioc->sas_topology_mutex);
#endif
		}
		mutex_unlock(&ioc->sas_discovery_mutex);
		break;

	case MPTSAS_ADD_PHYSDISK:

		if (mptsas_sas_device_pg0(ioc, &sas_device,
		    (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
		     MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
			(ev->channel << 8) + ev->id)) {
				dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
				    "%s: fw_id=%d exit at line=%d\n",
				    ioc->name, __FUNCTION__, ev->id, __LINE__));
			break;
		}

		mutex_lock(&ioc->sas_discovery_mutex);
		mutex_lock(&ioc->sas_topology_mutex);
		port_info = mptsas_find_portinfo_by_handle(ioc,
		    sas_device.handle_parent);
		mutex_unlock(&ioc->sas_topology_mutex);
		if (port_info) {
			ioc->sas_discovery_runtime = 1;
			scsi_block_requests(ioc->sh);
			mpt_findImVolumes(ioc);
			if (port_info == ioc->hba_port_info)
				mptsas_probe_hba_phys(ioc);
			else
				mptsas_add_expander(ioc, port_info, 0);
			scsi_unblock_requests(ioc->sh);
			ioc->sas_discovery_runtime = 0;
			phy_info = mptsas_find_phyinfo_by_sas_address(ioc,
			    sas_device.sas_address);
			phy_info->attached.id = ev->id;
			mptsas_add_end_device(ioc, phy_info);
			mutex_unlock(&ioc->sas_discovery_mutex);
			break;

		}
		mutex_unlock(&ioc->sas_discovery_mutex);
		/*
		 * retry later if the phy_info wasn't created
		 */
		if (ev->retries == 12)
			break;
		dfailprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s: line=%d: fw_id=%d retry_add (%d)\n", ioc->name,
		    __FUNCTION__, __LINE__, ev->id, ev->retries));
		schedule_delayed_work(&ev->hotplug_work, 5*HZ);
		ev->retries++;
		return;

	case MPTSAS_DEL_PHYSDISK:

		mutex_lock(&ioc->sas_discovery_mutex);
		mpt_findImVolumes(ioc);
		phy_info = mptsas_find_phyinfo_by_phys_disk_num(
		    ioc, ev->phys_disk_num, ev->channel, ev->id);
#if defined(__VMKLNX__)
		if (phy_info && (starget = mptsas_get_starget(phy_info)) &&
                   (vtarget = starget->hostdata) && vtarget->configured) {
                   /*
                    * Clear vtarget->configured when a RAID-member disk is
                    * removed and it can be configured upon insertion.
                    */
                   vtarget->configured = 0;
		}
#endif
		mptsas_del_end_device(ioc, phy_info);
		mutex_unlock(&ioc->sas_discovery_mutex);
		break;

	case MPTSAS_ADD_PHYSDISK_REPROBE:

		if (mptsas_sas_device_pg0(ioc, &sas_device,
		    (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
		     MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
		    (ev->channel << 8) + ev->id)) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			"%s: fw_id=%d exit at line=%d\n", ioc->name,
				__FUNCTION__, ev->id, __LINE__));
			break;
		}

		mutex_lock(&ioc->sas_discovery_mutex);
		phy_info = mptsas_find_phyinfo_by_sas_address(
		    ioc, sas_device.sas_address);
		mutex_unlock(&ioc->sas_discovery_mutex);

		if (!phy_info){
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
				"%s: fw_id=%d exit at line=%d\n", ioc->name,
			       	__FUNCTION__, ev->id, __LINE__));
			break;
		}

		starget = mptsas_get_starget(phy_info);
		if (!starget) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
				"%s: fw_id=%d exit at line=%d\n", ioc->name,
				__FUNCTION__, ev->id, __LINE__));
			break;
		}

		vtarget = starget->hostdata;
		if (!vtarget) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
				"%s: fw_id=%d exit at line=%d\n", ioc->name,
				__FUNCTION__, ev->id, __LINE__));
			break;
		}

		mpt_findImVolumes(ioc);

		starget_printk(KERN_INFO, starget, MYIOC_s_FMT
		    "RAID Hidding: fw_channel=%d, fw_id=%d, physdsk %d, sas_addr 0x%llx\n",
		    ioc->name, ev->channel, ev->id, ev->phys_disk_num, (unsigned long long)
		    sas_device.sas_address);

		vtarget->id = ev->phys_disk_num;
		vtarget->tflags |= MPT_TARGET_FLAGS_RAID_COMPONENT;
		phy_info->attached.phys_disk_num = ev->phys_disk_num;
		mptsas_reprobe_target(starget, 1);
		break;

	case MPTSAS_DEL_PHYSDISK_REPROBE:

		if (mptsas_sas_device_pg0(ioc, &sas_device,
		    (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
		     MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
			(ev->channel << 8) + ev->id)) {
				dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
					"%s: fw_id=%d exit at line=%d\n", ioc->name,
					__FUNCTION__, ev->id, __LINE__));
			break;
		}

		mutex_lock(&ioc->sas_discovery_mutex);
		phy_info = mptsas_find_phyinfo_by_sas_address(ioc,
				sas_device.sas_address);
		mutex_unlock(&ioc->sas_discovery_mutex);
		if (!phy_info) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			    "%s: fw_id=%d exit at line=%d\n", ioc->name,
			    __FUNCTION__, ev->id, __LINE__));
			break;
		}

		starget = mptsas_get_starget(phy_info);
		if (!starget) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			    "%s: fw_id=%d exit at line=%d\n", ioc->name,
			    __FUNCTION__, ev->id, __LINE__));
			break;
		}

		vtarget = starget->hostdata;
		if (!vtarget) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			    "%s: fw_id=%d exit at line=%d\n", ioc->name,
			    __FUNCTION__, ev->id, __LINE__));
			break;
		}

		if (!(vtarget->tflags & MPT_TARGET_FLAGS_RAID_COMPONENT)) {
			dfailprintk(ioc, printk(MYIOC_s_ERR_FMT
			    "%s: fw_id=%d exit at line=%d\n", ioc->name,
			    __FUNCTION__, ev->id, __LINE__));
			break;
		}

		mpt_findImVolumes(ioc);

		starget_printk(KERN_INFO, starget, MYIOC_s_FMT
		    "RAID Exposing: fw_channel=%d, fw_id=%d, physdsk %d, sas_addr 0x%llx\n",
		    ioc->name, ev->channel, ev->id, ev->phys_disk_num, (unsigned long long)
		    sas_device.sas_address);

		vtarget->tflags &= ~MPT_TARGET_FLAGS_RAID_COMPONENT;
		vtarget->id = ev->id;
#if defined(__VMKLNX__)
               /*
                * After a RAID is torn down, clear vtarget->configured
                * for a RAID-member disk.
                */
		vtarget->configured = 0;
#endif
		phy_info->attached.phys_disk_num = ~0;
		mptsas_reprobe_target(starget, 0);
#if defined(CPQ_CIM)
		mptsas_add_device_component_by_fw(ioc,
		    ev->channel, ev->id);
#endif
		break;

	case MPTSAS_ADD_RAID:

#if defined(__VMKLNX__)
		mptsas_connect_devices(ioc, ev->channel, ev->id);
#endif
		if (mptsas_test_unit_ready(ioc, ev->channel, ev->id) != 0)
			break;
		mpt_findImVolumes(ioc);
		printk(MYIOC_s_INFO_FMT "attaching raid volume, channel %d, "
		    "id %d\n", ioc->name, MPTSAS_RAID_CHANNEL, ev->id);
		scsi_add_device(ioc->sh, MPTSAS_RAID_CHANNEL, ev->id, 0);
		break;

	case MPTSAS_DEL_RAID:

		mpt_findImVolumes(ioc);
		printk(MYIOC_s_INFO_FMT "removing raid volume, channel %d, "
		    "id %d\n", ioc->name, MPTSAS_RAID_CHANNEL, ev->id);
		scsi_remove_device(ev->sdev);
		scsi_device_put(ev->sdev);
		break;

	case MPTSAS_ADD_INACTIVE_VOLUME:

		mpt_findImVolumes(ioc);
		mutex_lock(&ioc->sas_discovery_mutex);
		mptsas_adding_inactive_raid_components(ioc, ev->channel,
		    ev->id);
		mutex_unlock(&ioc->sas_discovery_mutex);
		break;

	default:
		break;
	}
	kfree(ev);
}

/**
 *	mptsas_send_sas_event -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@sas_event_data:
 *
 **/
static void
mptsas_send_sas_event(MPT_ADAPTER *ioc,
		EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *sas_event_data)
{
	struct mptsas_hotplug_event *ev;
	u32 device_info = le32_to_cpu(sas_event_data->DeviceInfo);
	__le64 sas_address;

	if ((device_info &
	     (MPI_SAS_DEVICE_INFO_SSP_TARGET |
	      MPI_SAS_DEVICE_INFO_STP_TARGET |
	      MPI_SAS_DEVICE_INFO_SATA_DEVICE )) == 0)
		return;

	switch (sas_event_data->ReasonCode) {
	case MPI_EVENT_SAS_DEV_STAT_RC_NOT_RESPONDING:

		mptsas_target_reset_queue(ioc, sas_event_data);
		break;

	case MPI_EVENT_SAS_DEV_STAT_RC_ADDED:
		ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
		if (!ev) {
			printk(MYIOC_s_WARN_FMT
			    "mptsas: lost hotplug event\n", ioc->name);
			break;
		}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
		INIT_DELAYED_WORK(&ev->hotplug_work, mptsas_hotplug_work);
#else
		INIT_WORK(&ev->hotplug_work, mptsas_hotplug_work, ev);
#endif
		ev->ioc = ioc;
		ev->handle = le16_to_cpu(sas_event_data->DevHandle);
		ev->channel = sas_event_data->Bus;
		ev->id = sas_event_data->TargetID;
		ev->phy_id = sas_event_data->PhyNum;
		memcpy(&sas_address, &sas_event_data->SASAddress,
		    sizeof(__le64));
		ev->sas_address = le64_to_cpu(sas_address);
		ev->device_info = device_info;
		ev->event_type = MPTSAS_ADD_DEVICE;
		schedule_delayed_work(&ev->hotplug_work, 0);
		break;

	case MPI_EVENT_SAS_DEV_STAT_RC_NO_PERSIST_ADDED:
	/*
	 * Persistent table is full.
	 */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
		INIT_WORK(&ioc->sas_persist_task,
		    mptsas_persist_clear_table);
#else
		INIT_WORK(&ioc->sas_persist_task,
		    mptsas_persist_clear_table, (void *)ioc);
#endif

		schedule_work(&ioc->sas_persist_task);
		break;
	/*
	 * TODO, handle other events
	 */
	case MPI_EVENT_SAS_DEV_STAT_RC_SMART_DATA:
	case MPI_EVENT_SAS_DEV_STAT_RC_UNSUPPORTED:
	case MPI_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET:
	case MPI_EVENT_SAS_DEV_STAT_RC_TASK_ABORT_INTERNAL:
	case MPI_EVENT_SAS_DEV_STAT_RC_ABORT_TASK_SET_INTERNAL:
	case MPI_EVENT_SAS_DEV_STAT_RC_CLEAR_TASK_SET_INTERNAL:
	case MPI_EVENT_SAS_DEV_STAT_RC_QUERY_TASK_INTERNAL:
	case MPI_EVENT_SAS_DEV_STAT_RC_ASYNC_NOTIFICATION:
	default:
		break;
	}
}

/**
 *	mptsas_send_raid_event -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@raid_event_data:
 *
 **/
static void
mptsas_send_raid_event(MPT_ADAPTER *ioc,
		EVENT_DATA_RAID *raid_event_data)
{
	struct mptsas_hotplug_event *ev;
	int status = le32_to_cpu(raid_event_data->SettingsStatus);
	int state = (status >> 8) & 0xff;
	struct scsi_device *sdev = NULL;
	VirtDevice *vdevice = NULL;

	if (ioc->bus_type != SAS)
		return;

	ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev) {
		printk(MYIOC_s_WARN_FMT
		    "mptsas: lost hotplug event\n", ioc->name);
		return;
	}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	INIT_DELAYED_WORK(&ev->hotplug_work, mptsas_hotplug_work);
#else
	INIT_WORK(&ev->hotplug_work, mptsas_hotplug_work, ev);
#endif
	ev->ioc = ioc;
	ev->id = raid_event_data->VolumeID;
	ev->channel = raid_event_data->VolumeBus;
	ev->event_type = MPTSAS_IGNORE_EVENT;
	ev->phys_disk_num = raid_event_data->PhysDiskNum;

	if (raid_event_data->ReasonCode == MPI_EVENT_RAID_RC_VOLUME_DELETED ||
	    raid_event_data->ReasonCode == MPI_EVENT_RAID_RC_VOLUME_CREATED ||
	    raid_event_data->ReasonCode == MPI_EVENT_RAID_RC_VOLUME_STATUS_CHANGED) {
		sdev = scsi_device_lookup(ioc->sh, MPTSAS_RAID_CHANNEL, ev->id, 0);
		ev->sdev = sdev;
		if (sdev)
			vdevice = sdev->hostdata;
	}

	switch (raid_event_data->ReasonCode) {
	case MPI_EVENT_RAID_RC_PHYSDISK_DELETED:
		ev->event_type = MPTSAS_DEL_PHYSDISK_REPROBE;
		break;
	case MPI_EVENT_RAID_RC_PHYSDISK_CREATED:
		ev->event_type = MPTSAS_ADD_PHYSDISK_REPROBE;
		break;
	case MPI_EVENT_RAID_RC_PHYSDISK_STATUS_CHANGED:
		switch (state) {
		case MPI_PD_STATE_ONLINE:
		case MPI_PD_STATE_NOT_COMPATIBLE:
			ev->event_type = MPTSAS_ADD_PHYSDISK;
			break;
		case MPI_PD_STATE_FAILED:
		case MPI_PD_STATE_MISSING:
		case MPI_PD_STATE_OFFLINE_AT_HOST_REQUEST:
		case MPI_PD_STATE_FAILED_AT_HOST_REQUEST:
		case MPI_PD_STATE_OFFLINE_FOR_ANOTHER_REASON:
			ev->event_type = MPTSAS_DEL_PHYSDISK;
			break;
		default:
			break;
		}
		break;
	case MPI_EVENT_RAID_RC_VOLUME_DELETED:
		if (!sdev)
			break;
		vdevice->vtarget->deleted = 1; /* block IO */
		ev->event_type = MPTSAS_DEL_RAID;
		break;
	case MPI_EVENT_RAID_RC_VOLUME_CREATED:
		if (sdev) {
			scsi_device_put(sdev);
			break;
		}
		ev->event_type = MPTSAS_ADD_RAID;
		break;
	case MPI_EVENT_RAID_RC_VOLUME_STATUS_CHANGED:
		if (!(status & MPI_RAIDVOL0_STATUS_FLAG_ENABLED)) {
			if (!sdev)
				break;
			vdevice->vtarget->deleted = 1; /* block IO */
			ev->event_type = MPTSAS_DEL_RAID;
			break;
		}
		switch (state) {
		case MPI_RAIDVOL0_STATUS_STATE_FAILED:
		case MPI_RAIDVOL0_STATUS_STATE_MISSING:
			if (!sdev)
				break;
			vdevice->vtarget->deleted = 1; /* block IO */
			ev->event_type = MPTSAS_DEL_RAID;
			break;
		case MPI_RAIDVOL0_STATUS_STATE_OPTIMAL:
		case MPI_RAIDVOL0_STATUS_STATE_DEGRADED:
			if (sdev) {
				scsi_device_put(sdev);
				break;
			}
			ev->event_type = MPTSAS_ADD_RAID;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	if (ev->event_type == MPTSAS_IGNORE_EVENT)
		kfree(ev);
	else
		schedule_delayed_work(&ev->hotplug_work, 0);
}

/**
 *	mptsas_send_discovery_event -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@discovery_data:
 *
 **/
static void
mptsas_send_discovery_event(MPT_ADAPTER *ioc,
	EVENT_DATA_SAS_DISCOVERY *discovery_data)
{
	struct mptsas_discovery_event *ev;
	u32 discovery_status = le32_to_cpu(discovery_data->DiscoveryStatus);

	/*
	 * DiscoveryStatus
	 *
	 * This flag will be non-zero when firmware
	 * kicks off discovery, and return to zero
	 * once its completed.
	 */
	ioc->sas_discovery_quiesce_io = discovery_status ? 1 : 0;
	if (discovery_status)
		return;

	/* Older firmware look at discovery events to handle expanders */
	if ((ioc->facts.HeaderVersion >> 8) >= 0xE)
		return;

	ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev)
		return;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	INIT_WORK(&ev->work, mptsas_discovery_work);
#else
	INIT_WORK(&ev->work, mptsas_discovery_work, ev);
#endif

	ev->ioc = ioc;
	schedule_work(&ev->work);
};

/**
 *	mptsas_issue_tm - send mptsas internal tm request
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@type
 *	@channel
 *	@id
 *	@lun
 *	@task_context
 *	@timeout
 *
 *	return:
 *
 **/
static int
mptsas_issue_tm(MPT_ADAPTER *ioc, u8 type, u8 channel, u8 id, u64 lun, int task_context, ulong timeout,
	u8 *issue_reset)
{
	MPT_FRAME_HDR	*mf;
	SCSITaskMgmt_t	*pScsiTm;
	int		 retval;
	unsigned long	 timeleft;

	*issue_reset = 0;
	if ((mf = mpt_get_msg_frame(mptsasDeviceResetCtx, ioc)) == NULL) {
		retval = -1; /* return failure */
		dtmprintk(ioc, printk(MYIOC_s_WARN_FMT "TaskMgmt request: no "
		    "msg frames!!\n", ioc->name));
		goto out;
	}

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT "TaskMgmt request: mr = %p, "
	    "task_type = 0x%02X,\n\t timeout = %ld, fw_channel = %d, "
	    "fw_id = %d, lun = %lld,\n\t task_context = 0x%x\n", ioc->name, mf,
	     type, timeout, channel, id, (unsigned long long)lun,
	     task_context));

	pScsiTm = (SCSITaskMgmt_t *) mf;
	memset(pScsiTm, 0, sizeof(SCSITaskMgmt_t));
	pScsiTm->Function = MPI_FUNCTION_SCSI_TASK_MGMT;
	pScsiTm->TaskType = type;
	pScsiTm->MsgFlags = 0;
	pScsiTm->TargetID = id;
	pScsiTm->Bus = channel;
	pScsiTm->ChainOffset = 0;
	pScsiTm->Reserved = 0;
	pScsiTm->Reserved1 = 0;
	pScsiTm->TaskMsgContext = task_context;
	int_to_scsilun(lun, (struct scsi_lun *)pScsiTm->LUN);

	INITIALIZE_MGMT_STATUS(ioc->taskmgmt_cmds.status)
	CLEAR_MGMT_STATUS(ioc->internal_cmds.status)
	retval = 0;
	mpt_put_msg_frame_hi_pri(mptsasDeviceResetCtx, ioc, mf);

	/* Now wait for the command to complete */
	timeleft = wait_for_completion_timeout(&ioc->taskmgmt_cmds.done,
	    timeout*HZ);
	if (!(ioc->taskmgmt_cmds.status & MPT_MGMT_STATUS_COMMAND_GOOD)) {
		retval = -1; /* return failure */
		dtmprintk(ioc, printk(MYIOC_s_ERR_FMT
		    "TaskMgmt request: TIMED OUT!(mr=%p)\n", ioc->name, mf));
		mpt_free_msg_frame(ioc, mf);
		if (ioc->taskmgmt_cmds.status & MPT_MGMT_STATUS_DID_IOCRESET)
			goto out;
		*issue_reset = 1;
		goto out;
	}

	if (!(ioc->taskmgmt_cmds.status & MPT_MGMT_STATUS_RF_VALID)) {
		retval = -1; /* return failure */
		dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "TaskMgmt request: failed with no reply\n", ioc->name));
		goto out;
	}

 out:
	CLEAR_MGMT_STATUS(ioc->taskmgmt_cmds.status)
	return retval;
}


/**
 *	mptsas_broadcast_primative_work - Work queue thread to handle
 *	broadcast primitive events
 *	@work: work queue payload containing info describing the event
 *
 **/
static void
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
mptsas_broadcast_primative_work(struct work_struct *work)
{
	struct mptsas_broadcast_primative_event *ev =
		container_of(work, struct mptsas_broadcast_primative_event, aen_work.work);
#else
mptsas_broadcast_primative_work(void *arg)
{
	struct mptsas_broadcast_primative_event *ev = arg;
#endif
	MPT_ADAPTER		*ioc = ev->ioc;
	MPT_FRAME_HDR		*mf;
	VirtDevice		*vdevice;
	int			ii;
	struct scsi_cmnd	*sc;
	SCSITaskMgmtReply_t *	pScsiTmReply;
	u8			issue_reset;
	int			task_context;
	u8			channel, id;
	int			 lun;
	u32			 termination_count;
	u32			 query_count;

	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "%s - enter\n", ioc->name, __FUNCTION__));

	mutex_lock(&ioc->taskmgmt_cmds.mutex);
	if (mpt_set_taskmgmt_in_progress_flag(ioc) != 0) {
		mutex_unlock(&ioc->taskmgmt_cmds.mutex);
		dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		    "%s - busy, reschedule task\n", ioc->name, __FUNCTION__));
		schedule_delayed_work(&ev->aen_work, 1*HZ);
		return;
	}

	issue_reset = 0;
	termination_count = 0;
	query_count = 0;
	mpt_findImVolumes(ioc);
	pScsiTmReply = (SCSITaskMgmtReply_t *) ioc->taskmgmt_cmds.reply;

	for (ii = 0; ii < ioc->req_depth; ii++) {
		sc = mptscsih_get_scsi_lookup(ioc, ii);
		if (!sc)
			continue;
		mf = MPT_INDEX_2_MFPTR(ioc, ii);
		if (!mf)
			continue;
		task_context = mf->u.frame.hwhdr.msgctxu.MsgContext;
		vdevice = sc->device->hostdata;
		if (!vdevice || !vdevice->vtarget)
			continue;
		if (vdevice->vtarget->tflags & MPT_TARGET_FLAGS_RAID_COMPONENT)
			continue; /* skip hidden raid components */
		if (vdevice->vtarget->raidVolume)
			continue; /* skip hidden raid components */
		channel = vdevice->vtarget->channel;
		id = vdevice->vtarget->id;
		lun = vdevice->lun;
		if (mptsas_issue_tm(ioc, MPI_SCSITASKMGMT_TASKTYPE_QUERY_TASK,
		    channel, id, (u64)lun, task_context, 30, &issue_reset))
			goto out;
		query_count++;
		termination_count +=
		    le32_to_cpu(pScsiTmReply->TerminationCount);
		if ((pScsiTmReply->IOCStatus == MPI_IOCSTATUS_SUCCESS) &&
		    (pScsiTmReply->ResponseCode ==
		    MPI_SCSITASKMGMT_RSP_TM_SUCCEEDED ||
		    pScsiTmReply->ResponseCode ==
		    MPI_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC))
			continue;
		if (mptsas_issue_tm(ioc,
		    MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET,
		    channel, id, (u64)lun, 0, 30, &issue_reset))
			goto out;
		termination_count +=
		    le32_to_cpu(pScsiTmReply->TerminationCount);
	}

 out:
	dtmprintk(ioc, printk(MYIOC_s_DEBUG_FMT
	    "%s - exit, query_count = %d termination_count = %d\n",
	    ioc->name, __FUNCTION__, query_count, termination_count));

	ioc->broadcast_aen_busy = 0;
	mpt_clear_taskmgmt_in_progress_flag(ioc);
	mutex_unlock(&ioc->taskmgmt_cmds.mutex);
	kfree(ev);

	if (issue_reset) {
		printk(MYIOC_s_WARN_FMT "Issuing Reset from %s!!\n",
		    ioc->name, __FUNCTION__);
		if (mpt_SoftResetHandler(ioc, CAN_SLEEP))
			mpt_HardResetHandler(ioc, CAN_SLEEP);
	}
}

/**
 *	mptsas_send_broadcast_primative_event - processing of event data
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	broadcast_event_data: event data
 *
 **/
static void
mptsas_send_broadcast_primative_event(MPT_ADAPTER * ioc,
	EVENT_DATA_SAS_BROADCAST_PRIMITIVE *broadcast_event_data)
{
	struct mptsas_broadcast_primative_event *ev;

	if (ioc->broadcast_aen_busy)
		return;

	if (broadcast_event_data->Primitive !=
	    MPI_EVENT_PRIMITIVE_ASYNCHRONOUS_EVENT)
		return;

	ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev)
		return;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	INIT_DELAYED_WORK(&ev->aen_work, mptsas_broadcast_primative_work);
#else
	INIT_WORK(&ev->aen_work, mptsas_broadcast_primative_work, ev);
#endif

	ev->ioc = ioc;
	ioc->broadcast_aen_busy = 1;
	schedule_delayed_work(&ev->aen_work, 0);
}

/**
 *	mptsas_send_ir2_event - handle exposing hidden disk when an inactive raid volume is added
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@ir2_data:
 *
 **/
static void
mptsas_send_ir2_event(MPT_ADAPTER *ioc, PTR_MPI_EVENT_DATA_IR2 ir2_data)
{
	struct mptsas_hotplug_event *ev;

	ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev)
		return;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	INIT_DELAYED_WORK(&ev->hotplug_work, mptsas_hotplug_work);
#else
	INIT_WORK(&ev->hotplug_work, mptsas_hotplug_work, ev);
#endif
	ev->ioc = ioc;
	ev->id = ir2_data->TargetID;
	ev->channel = ir2_data->Bus;
	ev->event_type = MPTSAS_IGNORE_EVENT;
	ev->phys_disk_num = ir2_data->PhysDiskNum;

	switch (ir2_data->ReasonCode) {
	case MPI_EVENT_IR2_RC_FOREIGN_CFG_DETECTED:
		ev->event_type = MPTSAS_ADD_INACTIVE_VOLUME;
		break;
	case MPI_EVENT_IR2_RC_DUAL_PORT_REMOVED:
		ev->event_type = MPTSAS_DEL_PHYSDISK;
		break;
	case MPI_EVENT_IR2_RC_DUAL_PORT_ADDED:
		ev->event_type = MPTSAS_ADD_PHYSDISK;
		break;
	default:
		break;
	}

	if (ev->event_type == MPTSAS_IGNORE_EVENT)
		kfree(ev);
	else
		schedule_delayed_work(&ev->hotplug_work, 0);
};

/**
 *	mptsas_send_expander_event - handle expanders coming and going
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@expander_data: event data
 *
 **/
static void
mptsas_send_expander_event(MPT_ADAPTER *ioc, PTR_EVENT_DATA_SAS_EXPANDER_STATUS_CHANGE expander_data)
{
	struct mptsas_portinfo *port_info;
	__le64 sas_address;
	int i;

	port_info = kzalloc(sizeof(*port_info), GFP_ATOMIC);
	if (!port_info)
		return;
	port_info->ioc = ioc;
	port_info->num_phys = expander_data->NumPhys;
	port_info->phy_info = kcalloc(port_info->num_phys,
	    sizeof(*port_info->phy_info),GFP_ATOMIC);
	if (!port_info->phy_info) {
		kfree(port_info);
		return;
	}

	memcpy(&sas_address, &expander_data->SASAddress, sizeof(__le64));
	for (i = 0; i < port_info->num_phys; i++) {
		port_info->phy_info[i].portinfo = port_info;
		port_info->phy_info[i].handle =
		    le16_to_cpu(expander_data->DevHandle);
		port_info->phy_info[i].identify.sas_address =
		    le64_to_cpu(sas_address);
		port_info->phy_info[i].identify.handle_parent =
		    le16_to_cpu(expander_data->ParentDevHandle);
	}

	if (expander_data->ReasonCode == MPI_EVENT_SAS_EXP_RC_NOT_RESPONDING) {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
		INIT_DELAYED_WORK(&port_info->del_work, mptsas_expander_delete_work);
#else
		INIT_WORK(&port_info->del_work, mptsas_expander_delete_work, port_info);
#endif
		schedule_delayed_work(&port_info->del_work, HZ * ioc->device_missing_delay);
	} else {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
		INIT_DELAYED_WORK(&port_info->add_work, mptsas_expander_add_work);
#else
		INIT_WORK(&port_info->add_work, mptsas_expander_add_work, port_info);
#endif
		schedule_delayed_work(&port_info->add_work, 0);
	}
}

/**
 *	mptsas_send_link_status_event - handle link status change
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	link_data: event data
 *
 **/
static void
mptsas_send_link_status_event(MPT_ADAPTER *ioc, PTR_EVENT_DATA_SAS_PHY_LINK_STATUS link_data)
{
	struct mptsas_link_status_event *ev;

	ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev)
		return;

	ev->ioc = ioc;
	memcpy(&ev->link_data, link_data, sizeof(*link_data));
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	INIT_WORK(&ev->work, mptsas_link_status_work);
#else
	INIT_WORK(&ev->work, mptsas_link_status_work, ev);
#endif
	schedule_work(&ev->work);
}

/**
 *	mptsas_event_process -
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@reply:
 *
 **/
static int
mptsas_event_process(MPT_ADAPTER *ioc, EventNotificationReply_t *reply)
{
	int rc=1;
	u8 event = le32_to_cpu(reply->Event) & 0xFF;

	if (!ioc->sh)
		goto out;

	/*
	 * sas_discovery_ignore_events
	 *
	 * This flag is to prevent anymore processing of
	 * sas events once mptsas_remove function is called.
	 */
	if (ioc->sas_discovery_ignore_events) {
		rc = mptscsih_event_process(ioc, reply);
		goto out;
	}

	switch (event) {
	case MPI_EVENT_SAS_DEVICE_STATUS_CHANGE:
#if defined(CPQ_CIM)
	ioc->csmi_change_count++;
#endif
		mptsas_send_sas_event(ioc,
			(EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *)reply->Data);
		break;
	case MPI_EVENT_INTEGRATED_RAID:
#if defined(CPQ_CIM)
	ioc->csmi_change_count++;
#endif
		mptsas_send_raid_event(ioc,
			(EVENT_DATA_RAID *)reply->Data);
		break;
	case MPI_EVENT_PERSISTENT_TABLE_FULL:
#if defined(CPQ_CIM)
	ioc->csmi_change_count++;
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
		INIT_WORK(&ioc->sas_persist_task,
		    mptsas_persist_clear_table);
#else
		INIT_WORK(&ioc->sas_persist_task,
		    mptsas_persist_clear_table, (void *)ioc);
#endif
		schedule_work(&ioc->sas_persist_task);
		break;
	 case MPI_EVENT_SAS_DISCOVERY:
		mptsas_send_discovery_event(ioc,
		    (EVENT_DATA_SAS_DISCOVERY *)reply->Data);
		break;
	case MPI_EVENT_SAS_EXPANDER_STATUS_CHANGE:
		/* In 1.05.12 firmware expander events were added */
		if ((ioc->facts.HeaderVersion >> 8) >= 0xE)
			mptsas_send_expander_event(ioc,
			    (EVENT_DATA_SAS_EXPANDER_STATUS_CHANGE *)reply->Data);
		break;
	case MPI_EVENT_SAS_PHY_LINK_STATUS:
/*
 * Any command is issued to a device and it will be marked as nexus_loss
 * if device is not present. In case cable is removed and inserted back before
 * DMD timeout, nexus_loss will stay ON and that path will be "dead".
 * So, we need to clear nexus_loss at phy_link_event to cover this case.
 * Regardless fw header version, call link_status_work to clear
 * vdevice->nexus_loss. For older version, it take an early exit after clearing
 * nexus_loss flag. For new version, it should continue as usual.
 * Some vendor's solution still uses header version 0x0.
 * Others move to version 0xE.
 */
#if !defined(__VMKLNX__)
		if ((ioc->facts.HeaderVersion >> 8) >= 0xE)
#endif
			mptsas_send_link_status_event(ioc,
			    (EVENT_DATA_SAS_PHY_LINK_STATUS *)reply->Data);
		break;
	case MPI_EVENT_IR2:
#if defined(CPQ_CIM)
	ioc->csmi_change_count++;
#endif
		mptsas_send_ir2_event(ioc,
		    (PTR_MPI_EVENT_DATA_IR2)reply->Data);
		break;
	case MPI_EVENT_SAS_BROADCAST_PRIMITIVE:
		mptsas_send_broadcast_primative_event(ioc,
			(EVENT_DATA_SAS_BROADCAST_PRIMITIVE *)reply->Data);
		break;
	default:
		rc = mptscsih_event_process(ioc, reply);
		break;
	}
 out:

	return rc;
}

/**
 *	mptsas_probe -
 *	@pdev:
 *	@id:
 *
 **/
static int
mptsas_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct Scsi_Host	*sh;
	MPT_SCSI_HOST		*hd;
	MPT_ADAPTER 		*ioc;
	unsigned long		 flags;
	int			 ii;
	int			 numSGE = 0;
	int			 scale;
	int			 ioc_cap;
	int			error=0;
	int			r;

	r = mpt_attach(pdev,id);
	if (r)
		return r;

	ioc = pci_get_drvdata(pdev);
#if defined(__VMKLNX__)
	if (!ioc) {
		printk("Skipping because ioc is NULL due to HW issues!\n");
		return(-ENODEV);
	}
#endif
	ioc->DoneCtx = mptsasDoneCtx;
	ioc->TaskCtx = mptsasTaskCtx;
	ioc->InternalCtx = mptsasInternalCtx;

	/*  Added sanity check on readiness of the MPT adapter.
	 */
	if (ioc->last_state != MPI_IOC_STATE_OPERATIONAL) {
		printk(MYIOC_s_WARN_FMT
		  "Skipping because it's not operational!\n",
		  ioc->name);
		error = -ENODEV;
		goto out_mptsas_probe;
	}

	if (!ioc->active) {
		printk(MYIOC_s_WARN_FMT "Skipping because it's disabled!\n",
		  ioc->name);
		error = -ENODEV;
		goto out_mptsas_probe;
	}

	/*  Sanity check - ensure at least 1 port is INITIATOR capable
	 */
	ioc_cap = 0;
	for (ii = 0; ii < ioc->facts.NumberOfPorts; ii++) {
		if (ioc->pfacts[ii].ProtocolFlags &
				MPI_PORTFACTS_PROTOCOL_INITIATOR)
			ioc_cap++;
	}

	if (!ioc_cap) {
		printk(MYIOC_s_WARN_FMT
			"Skipping ioc=%p because SCSI Initiator mode "
			"is NOT enabled!\n", ioc->name, ioc);
		return 0;
	}

	sh = scsi_host_alloc(&mptsas_driver_template, sizeof(MPT_SCSI_HOST));
	if (!sh) {
		printk(MYIOC_s_WARN_FMT
			"Unable to register controller with SCSI subsystem\n",
			ioc->name);
		error = -1;
		goto out_mptsas_probe;
	}

	spin_lock_irqsave(&ioc->FreeQlock, flags);

	/* Attach the SCSI Host to the IOC structure
	 */
	ioc->sh = sh;

	sh->io_port = 0;
	sh->n_io_port = 0;
	sh->irq = 0;

	/* set 16 byte cdb's */
	sh->max_cmd_len = 16;
	sh->can_queue = min_t(int, ioc->req_depth - 10, sh->can_queue);
	sh->max_id = ioc->pfacts[0].PortSCSIID;
#if defined(__VMKLNX__)
       /*
        * If RAID Firmware Detected, setup virtual channel
        */
        if (ioc->ir_firmware) {
                sh->max_channel = MPTSAS_RAID_CHANNEL;
        }
        sh->max_lun = MPT_SAS_LAST_LUN + 1;
#else /* !defined(__VMKLNX__) */
	sh->max_lun = max_lun;
#endif /* defined(__VMKLNX__) */
	sh->transportt = mptsas_transport_template;

	/* Required entry.
	 */
	sh->unique_id = ioc->id;

	INIT_LIST_HEAD(&ioc->sas_topology);
	mutex_init(&ioc->sas_topology_mutex);
	mutex_init(&ioc->sas_discovery_mutex);
	mutex_init(&ioc->sas_mgmt.mutex);
	init_completion(&ioc->sas_mgmt.done);


	/* Verify that we won't exceed the maximum
	 * number of chain buffers
	 * We can optimize:  ZZ = req_sz/sizeof(SGE)
	 * For 32bit SGE's:
	 *  numSGE = 1 + (ZZ-1)*(maxChain -1) + ZZ
	 *               + (req_sz - 64)/sizeof(SGE)
	 * A slightly different algorithm is required for
	 * 64bit SGEs.
	 */
	scale = ioc->req_sz/(sizeof(dma_addr_t) + sizeof(u32));
	if (sizeof(dma_addr_t) == sizeof(u64)) {
		numSGE = (scale - 1) *
		  (ioc->facts.MaxChainDepth-1) + scale +
		  (ioc->req_sz - 60) / (sizeof(dma_addr_t) +
		  sizeof(u32));
	} else {
		numSGE = 1 + (scale - 1) *
		  (ioc->facts.MaxChainDepth-1) + scale +
		  (ioc->req_sz - 64) / (sizeof(dma_addr_t) +
		  sizeof(u32));
	}

	if (numSGE < sh->sg_tablesize) {
		/* Reset this value */
		dprintk(ioc, printk(MYIOC_s_DEBUG_FMT
		  "Resetting sg_tablesize to %d from %d\n",
		  ioc->name, numSGE, sh->sg_tablesize));
		sh->sg_tablesize = numSGE;
	}

	hd = shost_priv(sh);
	hd->ioc = ioc;

	/* SCSI needs scsi_cmnd lookup table!
	 * (with size equal to req_depth*PtrSz!)
	 */
	ioc->ScsiLookup = kcalloc(ioc->req_depth, sizeof(void *), GFP_ATOMIC);
	if (!ioc->ScsiLookup) {
		error = -ENOMEM;
		spin_unlock_irqrestore(&ioc->FreeQlock, flags);
		goto out_mptsas_probe;
	}
	spin_lock_init(&ioc->scsi_lookup_lock);

	dprintk(ioc, printk(MYIOC_s_DEBUG_FMT "ScsiLookup @ %p\n",
		 ioc->name, ioc->ScsiLookup));

	ioc->sdev_queue_depth = mpt_sdev_queue_depth;
	ioc->sas_data.ptClear = mpt_pt_clear;
	hd->last_queue_full = 0;
	ioc->disable_hotplug_remove = mpt_disable_hotplug_remove;
	if (ioc->disable_hotplug_remove)
		printk(MYIOC_s_INFO_FMT "disabling hotplug remove\n", ioc->name);

	INIT_LIST_HEAD(&hd->target_reset_list);
#if defined(CPQ_CIM)
	INIT_LIST_HEAD(&ioc->sas_device_info_list);
	init_MUTEX(&ioc->sas_device_info_mutex);
#endif

	spin_unlock_irqrestore(&ioc->FreeQlock, flags);

	if (ioc->sas_data.ptClear==1) {
		mptbase_sas_persist_operation(
		    ioc, MPI_SAS_OP_CLEAR_ALL_PERSISTENT);
	}

	error = scsi_add_host(sh, &ioc->pcidev->dev);
	if (error) {
		dprintk(ioc, printk(MYIOC_s_ERR_FMT
		  "scsi_add_host failed\n", ioc->name));
		goto out_mptsas_probe;
	}

	mptsas_scan_sas_topology(ioc);

#if defined(__VMKLNX__)
       /*
        * If a SATA disk is attached, set the max IO transfer size to 64K.
        */
        if (ioc->attached_device_type & MPT_TARGET_DEVICE_TYPE_SATA) {
           sh->max_sectors = 128;
        }
#endif /* defined(__VMKLNX__) */
	return 0;

 out_mptsas_probe:

	mptscsih_remove(pdev);
	return error;
}

/**
 *	mptsas_remove -
 *	@pdev:
 *
 **/
static void __devexit
mptsas_remove(struct pci_dev *pdev)
{
	MPT_ADAPTER *ioc = pci_get_drvdata(pdev);
	struct mptsas_portinfo *p, *n;
	int i;

#if defined(CPQ_CIM)
	mptsas_del_device_components(ioc);
#endif

	ioc->sas_discovery_ignore_events = 1;
	sas_remove_host(ioc->sh);

	mutex_lock(&ioc->sas_topology_mutex);
	list_for_each_entry_safe(p, n, &ioc->sas_topology, list) {
		list_del(&p->list);
		for (i = 0 ; i < p->num_phys ; i++)
			mptsas_port_delete(ioc, p->phy_info[i].port_details);

		if(p->del_work_scheduled) {
			cancel_delayed_work(&p->del_work);
			p->del_work_scheduled=0;
		}
		kfree(p->phy_info);
		kfree(p);
	}
	mutex_unlock(&ioc->sas_topology_mutex);
	ioc->hba_port_info = NULL;
	mptscsih_remove(pdev);
}

static struct pci_device_id mptsas_pci_table[] = {
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVID_SAS1064,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVID_SAS1068,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVID_SAS1064E,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVID_SAS1068E,
		PCI_ANY_ID, PCI_ANY_ID },
	{ PCI_VENDOR_ID_LSI_LOGIC, MPI_MANUFACTPAGE_DEVID_SAS1078,
		PCI_ANY_ID, PCI_ANY_ID },
	{0}	/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, mptsas_pci_table);


static struct pci_driver mptsas_driver = {
	.name		= "mptsas",
	.id_table	= mptsas_pci_table,
	.probe		= mptsas_probe,
	.remove		= __devexit_p(mptsas_remove),
	.shutdown	= mptscsih_shutdown,
#ifdef CONFIG_PM
	.suspend	= mptscsih_suspend,
	.resume		= mptscsih_resume,
#endif
};

/**
 *	mptsas_init -
 *
 **/
static int __init
mptsas_init(void)
{
	int error;

	show_mptmod_ver(my_NAME, my_VERSION);
#if defined(__VMKLNX__)
        fusion_init();
#endif /* defined(__VMKLNX__) */
	mptsas_transport_template =
	    sas_attach_transport(&mptsas_transport_functions);
	if (!mptsas_transport_template)
		return -ENODEV;

	mptsasDoneCtx = mpt_register(mptscsih_io_done, MPTSAS_DRIVER);
	mptsasTaskCtx = mpt_register(mptscsih_taskmgmt_complete, MPTSAS_DRIVER);
	mptsasInternalCtx =
		mpt_register(mptscsih_scandv_complete, MPTSAS_DRIVER);
	mptsasMgmtCtx = mpt_register(mptsas_mgmt_done, MPTSAS_DRIVER);
	mptsasDeviceResetCtx =
		mpt_register(mptsas_taskmgmt_complete, MPTSAS_DRIVER);

	mpt_event_register(mptsasDoneCtx, mptsas_event_process);
	mpt_reset_register(mptsasDoneCtx, mptsas_ioc_reset);

	error = pci_register_driver(&mptsas_driver);
	if (error)
		sas_release_transport(mptsas_transport_template);
#if defined(__VMKLNX__)
        if (!error) {
                mptctl_init(SAS);
        }
#endif /* defined(__VMKLNX__) */
	return error;
}

/**
 *	mptsas_exit -
 *
 **/
static void __exit
mptsas_exit(void)
{
#if defined(__VMKLNX__)
        mptctl_exit();
#endif /* defined(__VMKLNX__) */
	pci_unregister_driver(&mptsas_driver);
	sas_release_transport(mptsas_transport_template);

	mpt_reset_deregister(mptsasDoneCtx);
	mpt_event_deregister(mptsasDoneCtx);

	mpt_deregister(mptsasMgmtCtx);
	mpt_deregister(mptsasInternalCtx);
	mpt_deregister(mptsasTaskCtx);
	mpt_deregister(mptsasDoneCtx);
	mpt_deregister(mptsasDeviceResetCtx);
}

module_init(mptsas_init);
module_exit(mptsas_exit);
