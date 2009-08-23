/*
 *    Disk Array driver for HP SA 5xxx and 6xxx Controllers
 *    Copyright 2000, 2008 Hewlett-Packard Development Company, L.P.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *    Questions/Comments/Bugfixes to iss_storagedev@hp.com
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/blkpg.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/hdreg.h>
#include <linux/spinlock.h>
#include <linux/compat.h>
#include <linux/blktrace_api.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/dma-mapping.h>
#include <linux/genhd.h>
#include <linux/completion.h>
#include <linux/kthread.h>

#define HPSA_DRIVER_VERSION(maj,min,submin) ((maj<<16)|(min<<8)|(submin))
#define DRIVER_NAME "HP HPSA Driver (v 3.6.14.24vmw)"
#define DRIVER_VERSION HPSA_DRIVER_VERSION(3,6,14)

/* Embedded module documentation macros - see modules.h */
MODULE_AUTHOR("Hewlett-Packard Company");
MODULE_DESCRIPTION("Driver for HP Smart Array Controllers version 3.6.14.24vmw");
MODULE_SUPPORTED_DEVICE("HP P700M and Smart Array G2 Series");
MODULE_VERSION("3.6.14.24vmw");
MODULE_LICENSE("GPL");

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <linux/cciss_ioctl.h>
#if defined(__VMKLNX__)
#include <scsi/scsi_transport_sas.h>
#endif
#include "hpsa_cmd.h"
#include "hpsa.h"


/* define the PCI info for the cards we can control */
/* remove when PCI_DEVICE_ID_COMPAQ_HPSAC is in pci_ids.h */
#ifndef PCI_DEVICE_ID_COMPAQ_HPSAC
#define PCI_DEVICE_ID_COMPAQ_HPSAC 0x46
#endif
#ifndef PCI_DEVICE_ID_HP_HPSA
#define PCI_DEVICE_ID_HP_HPSA 0x3210
#endif
#ifndef PCI_DEVICE_ID_HP_HPSAA
#define PCI_DEVICE_ID_HP_HPSAA 0x3220
#endif
#ifndef PCI_DEVICE_ID_HP_HPSAC
#define PCI_DEVICE_ID_HP_HPSAC 0x3230
#endif
#ifndef PCI_DEVICE_ID_HP_HPSAD
#define PCI_DEVICE_ID_HP_HPSAD 0x3238
#endif
#ifndef PCI_DEVICE_ID_HP_HPSAE
#define PCI_DEVICE_ID_HP_HPSAE 0x323A
#endif

/* define the PCI info for the cards we can control */
static const struct pci_device_id hpsa_pci_device_id[] = {
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_HPSAC,     0x103C, 0x323D},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_HPSAE,     0x103C, 0x3241},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_HPSAE,     0x103C, 0x3243},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_HPSAE,     0x103C, 0x3245},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_HPSAE,     0x103C, 0x3247},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_HPSAE,     0x103C, 0x3249},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_HPSAE,     0x103C, 0x324a},
	{PCI_VENDOR_ID_HP,     PCI_DEVICE_ID_HP_HPSAE,     0x103C, 0x324b},
	{0,}
};

MODULE_DEVICE_TABLE(pci, hpsa_pci_device_id);

/*  board_id = Subsystem Device ID & Vendor ID
 *  product = Marketing Name for the board
 *  access = Address of the struct of function pointers
 */
static struct board_type products[] = {
	{0x323d103c, "Smart Array P700M", &SA5_access},
	{0x3241103C, "Smart Array P212", &SA5_access},
	{0x3243103C, "Smart Array P410", &SA5_access},
	{0x3245103C, "Smart Array P410i", &SA5_access},
	{0x3247103C, "Smart Array P411", &SA5_access},
	{0x3249103C, "Smart Array P812", &SA5_access},
	{0x324a103C, "Smart Array P712m", &SA5_access},
	{0x324b103C, "Smart Array P711m", &SA5_access},
};

/* How long to wait (in milliseconds) for board to go into simple mode */
#define MAX_CONFIG_WAIT 30000
#define MAX_IOCTL_CONFIG_WAIT 1000

/*define how many times we will try a command because of bus resets */
#define MAX_CMD_RETRIES 3

#define READ_AHEAD 	 1024
#ifdef __VMKLNX__
#define MAX_CTLR	8	// Only support 8 controllers in Vmware
#else
#define MAX_CTLR	32
#endif
#define MAX_TARGETS_PER_CTLR 16

/* Originally cciss driver only supports 8 major numbers */
#define MAX_CTLR_ORIG 	8

static ctlr_info_t *hba[MAX_CTLR];
#if defined(__VMKLNX__)
struct scsi_transport_template *hpsa_transport_template = NULL;
#endif
static irqreturn_t do_hpsa_intr(int irq, void *dev_id, struct pt_regs *regs);
static int hpsa_ioctl(struct scsi_device *dev, int cmd, void *arg);
static void start_io(ctlr_info_t *h);
static int sendcmd(__u8 cmd, int ctlr, void *buff, size_t size,
		   unsigned int use_unit_num, unsigned int log_unit,
		   __u8 page_code, unsigned char *scsi3addr, int cmd_type);

#ifdef CONFIG_PROC_FS
static int hpsa_proc_get_info(char *buffer, char **start, off_t offset,
			       int length, int *eof, void *data);
static void hpsa_procinit(int i);
#else
static void hpsa_procinit(int i)
{
}
#endif				/* CONFIG_PROC_FS */

#ifdef CONFIG_COMPAT
static int hpsa_compat_ioctl(struct scsi_device *dev, int cmd, void *arg);
int hpsa_char_compat_ioctl(struct file *f, unsigned cmd, unsigned long arg);
#endif

static int hpsa_do_rescan(void *data); 

#if defined(__VMKLNX__)
#define MAX_PATHS 8     // Maximum number of P700M SAS paths
#define CONTROLLER_DEVICE 7     // Physical device type of controller
#ifndef TYPE_RAID_CONTROLLER
#define TYPE_RAID_CONTROLLER 0x0c // Device type 
#endif
u64 target_sas_id[MAX_CTLR][MAX_PATHS];
u64 cntl_sas_id[MAX_CTLR];

static int
hpsa_get_linkerrors(struct sas_phy *phy)
{
    /* dummy function - placeholder */
    return 0;
}

static int
hpsa_get_enclosure_identifier(struct sas_rphy *rphy, u64 *identifier)
{
   /* dummy function - placeholder */
   return 0;
}

static int
hpsa_get_bay_identifier(struct sas_rphy *rphy)
{
   /* dummy function - placeholder */
   return 0;
}

static int
hpsa_phy_reset(struct sas_phy *phy, int hard_reset)
{
   /* dummy function - placeholder */
   return 0;
}

static int
hpsa_get_initiator_sas_identifier(struct Scsi_Host *sh, u64 *sas_id)
{
        ctlr_info_t *ci;
        int ctlr;
        ci = (ctlr_info_t *) sh->hostdata[0];
        if (ci == NULL) {
        	printk(KERN_INFO "hpsa: hpsa_get_sas_initiator_identifier:"
			" hostdata is NULL for host_no %d\n", sh->host_no);
        	return -EINVAL;
        }
        ctlr = ci->ctlr;
        if (ctlr > MAX_CTLR || ctlr < 0) {
                printk(KERN_INFO "hpsa: hpsa_get_sas_initiator_identifier:"
			" ctlr value (%d) not between 0 and 7\n", ctlr);
                return -EINVAL;
        }
        *sas_id = cntl_sas_id[ctlr];

#ifdef DEBUG
        printk(KERN_INFO "hpsa: hpsa_get_sas_initiator_identifier:"
		" sas_id returned is 0x%llx\n", *sas_id);
#endif

   return SUCCESS;
}

static int
hpsa_get_target_sas_identifier(struct scsi_target *starget, u64 *sas_id)
{
        ctlr_info_t *ci;
        int ctlr;
        struct Scsi_Host *host = dev_to_shost(&starget->dev);

        ci = (ctlr_info_t *) host->hostdata[0];
        if (ci == NULL) {
        	printk(KERN_INFO "hpsa: hpsa_get_sas_target_identifier:"
			" hostdata is NULL\n");
	        return -EINVAL;
        }
        ctlr = ci->ctlr;
        if (ctlr > MAX_CTLR || ctlr < 0) {
                printk(KERN_INFO "hpsa: hpsa_get_sas_target_identifier: ci=%p"
			" ctlr value (%d) not between 0 and 7\n", ci, ctlr);
                return -EINVAL;
        }
        *sas_id = target_sas_id[ctlr][starget->id];

#ifdef DEBUG
        printk(KERN_INFO "hpsa: C%d:T%d hpsa_get_sas_target_identifier: sas_id"
		" returned is 0x%llx\n", starget->channel, starget->id, *sas_id);
#endif

   return SUCCESS;
}

#endif

/*
 * Enqueuing and dequeuing functions for cmdlists.
 */
static inline void addQ(CommandList_struct **Qptr, CommandList_struct *c)
{
	if (*Qptr == NULL) {
		*Qptr = c;
		c->next = c->prev = c;
	} else {
		c->prev = (*Qptr)->prev;
		c->next = (*Qptr);
		(*Qptr)->prev->next = c;
		(*Qptr)->prev = c;
	}
}

static inline CommandList_struct *removeQ(CommandList_struct **Qptr,
					  CommandList_struct *c)
{
	if (c && c->next != c) {
		if (*Qptr == c)
			*Qptr = c->next;
		c->prev->next = c->next;
		c->next->prev = c->prev;
	} else {
		*Qptr = NULL;
	}
	return c;
}

/*
 * The File Operations structure for the serial/ioctl interface of the driver
 * For consistency, this is the same way the character ioctl interface was
 * done for the cciss driver
 */
int hpsa_char_ioctl(struct inode *inode, struct file *f,
                                    unsigned cmd, unsigned long arg);
int hpsa_char_open(struct inode *inode, struct file *filep);

static const struct file_operations hpsa_char_fops = {
        .owner          = THIS_MODULE,
        .open           = hpsa_char_open,
        .ioctl          = hpsa_char_ioctl,
#ifdef CONFIG_COMPAT
        .compat_ioctl   = hpsa_char_compat_ioctl,
#endif
};
int char_major[MAX_CTLR];    // Holds chrdev major number by controller,

#include "hpsa_scsi.c"		/* For SCSI support */

/**
 * hpsa_char_open()
 * @inode - unused
 * @filep - unused
 *
 * Routines for the character/ioctl interface to the driver. Find out if this
 * is a valid open.
 */
int hpsa_char_open (struct inode *inode, struct file *filep)
{
        /*
         * Only allow superuser to access private ioctl interface
         */
        if( !capable(CAP_SYS_ADMIN) ) return -EACCES;

        return 0;
}

int hpsa_char_ioctl(struct inode *inode, struct file *f,
                                    unsigned cmd, unsigned long arg)
{
        int i;
        int found = 0;
        int chrmajor = 0;
        struct scsi_device dev;
        struct scsi_device *p_dev = &dev;
        //struct Scsi_Host *sh;

        chrmajor = MAJOR(inode->i_rdev);
        if (!chrmajor) {
                printk(KERN_WARNING "hpsa: Invalid Char Device major: %d\n",
                        MAJOR(inode->i_rdev));
                return -ENODEV;
        }
        for (i = 0; i < MAX_CTLR; i++) {
                if (char_major[i] == chrmajor) {
                        found = 1;
                        break;
                }
        }
        if (!found)
        {                 printk(KERN_WARNING "hpsa: No matching char dev found"
				" for major number %d\n", chrmajor);
                return -ENODEV;
        }
        // i is now the index into hba struct

        p_dev->host = ((struct hpsa_scsi_adapter_data_t *)hba[i]->scsi_ctlr)->scsi_host;

        return hpsa_ioctl(p_dev, cmd, (void *)arg);

}

#ifdef CONFIG_COMPAT
int hpsa_char_compat_ioctl(struct file *f,
                   unsigned cmd, unsigned long arg)
{
        int i;
        int found = 0;
        int chrmajor = 0;
        struct scsi_device dev;
        struct scsi_device *p_dev = &dev;
	struct inode *inode = f->f_dentry->d_inode;


        chrmajor = MAJOR(inode->i_rdev);
        if (!chrmajor) {
                printk(KERN_WARNING "hpsa: Invalid Char Device major: %d\n",
                        MAJOR(inode->i_rdev));
                return -ENODEV;
        }
        for (i = 0; i < MAX_CTLR; i++) {
                if (char_major[i] == chrmajor) {
                        found = 1;
                        break;
                }
        }
        if (!found)
        {                 printk(KERN_WARNING "hpsa: No matching char dev found"
				" for major number %d\n", chrmajor);
                return -ENODEV;
        }
        // i is now the index into hba struct

        p_dev->host = ((struct hpsa_scsi_adapter_data_t *)hba[i]->scsi_ctlr)->scsi_host;

        return hpsa_compat_ioctl(p_dev, cmd, (void *)arg);
}
#endif

#ifdef CONFIG_PROC_FS

/*
 * Report information about this controller.
 */
#define ENG_GIG 1000000000
#define ENG_GIG_FACTOR (ENG_GIG/512)
#define RAID_UNKNOWN 6
static const char *raid_label[] = { "0", "4", "1(1+0)", "5", "5+1", "ADG",
	"UNKNOWN"
};

static struct proc_dir_entry *proc_hpsa;

static int hpsa_proc_get_info(char *buffer, char **start, off_t offset,
			       int length, int *eof, void *data)
{
	off_t pos = 0;
	off_t len = 0;
	int size, i, ctlr;
	ctlr_info_t *h = (ctlr_info_t *) data;
	drive_info_struct *drv;
	unsigned long flags;
	sector_t vol_sz, vol_sz_frac;

	ctlr = h->ctlr;

	/* prevent displaying bogus info during configuration
	 * or deconfiguration of a logical volume
	 */
	spin_lock_irqsave(HPSA_LOCK(ctlr), flags);
	if (h->busy_configuring) {
		spin_unlock_irqrestore(HPSA_LOCK(ctlr), flags);
		return -EBUSY;
	}
	h->busy_configuring = 1;
	spin_unlock_irqrestore(HPSA_LOCK(ctlr), flags);

	size = sprintf(buffer, "%s: HP %s Controller\n"
		       "Board ID: 0x%08lx\n"
		       "Firmware Version: %c%c%c%c\n"
		       "IRQ: %d\n"
		       "Logical drives: %d\n"
		       "Current Q depth: %d\n"
		       "Current # commands on controller: %d\n"
		       "Max Q depth since init: %d\n"
		       "Max # commands on controller since init: %d\n"
		       "Max SG entries since init: %d\n\n",
		       h->devname,
		       h->product_name,
		       (unsigned long)h->board_id,
		       h->firm_ver[0], h->firm_ver[1], h->firm_ver[2],
		       h->firm_ver[3], (unsigned int)h->intr[SIMPLE_MODE_INT],
		       h->num_luns, h->Qdepth, h->commands_outstanding,
		       h->maxQsinceinit, h->max_outstanding, h->maxSG);

	pos += size;
	len += size;
	for (i = 0; i <= h->highest_lun; i++) {

		drv = &h->drv[i];
		if (drv->heads == 0)
			continue;

		vol_sz = drv->nr_blocks;
		vol_sz_frac = sector_div(vol_sz, ENG_GIG_FACTOR);
		vol_sz_frac *= 100;
		sector_div(vol_sz_frac, ENG_GIG_FACTOR);

		if (drv->raid_level > 5)
			drv->raid_level = RAID_UNKNOWN;
		size = sprintf(buffer + len, "hpsa/c%dd%d:"
			       "\t%4u.%02uGB\tRAID %s\n",
			       ctlr, i, (int)vol_sz, (int)vol_sz_frac,
			       raid_label[drv->raid_level]);
		pos += size;
		len += size;
	}

	*eof = 1;
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	h->busy_configuring = 0;
	return len;
}

static int
hpsa_proc_write(struct file *file, const char __user *buffer,
		 unsigned long count, void *data)
{
	ctlr_info_t *h = (ctlr_info_t *) data;
	int rc;

		rc = hpsa_engage_scsi(h->ctlr);
		if (rc != 0)
			return -rc;
		return count;
}

/*
 * Get us a file in /proc/hpsa that says something about each controller.
 * Create /proc/hpsa if it doesn't exist yet.
 */
static void __devinit hpsa_procinit(int i)
{
	struct proc_dir_entry *pde;

	if (proc_hpsa == NULL) {
		proc_hpsa = proc_mkdir("hpsa", proc_root_driver);
		if (!proc_hpsa)
			return;
	}

	pde = create_proc_read_entry(hba[i]->devname,
				     S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH,
				     proc_hpsa, hpsa_proc_get_info, hba[i]);
	pde->write_proc = hpsa_proc_write;
}
#endif				/* CONFIG_PROC_FS */

/*
 * For operations that cannot sleep, a command block is allocated at init,
 * and managed by cmd_alloc() and cmd_free() using a simple bitmap to track
 * which ones are free or in use.  For operations that can wait for kmalloc
 * to possible sleep, this routine can be called with get_from_pool set to 0.
 * cmd_free() MUST be called with a got_from_pool set to 0 if cmd_alloc was.
 */
static CommandList_struct *cmd_alloc(ctlr_info_t *h, int get_from_pool)
{
	CommandList_struct *c;
	int i;
	u64bit temp64;
	dma_addr_t cmd_dma_handle, err_dma_handle;

	if (!get_from_pool) {
		c = (CommandList_struct *) pci_alloc_consistent(h->pdev,
			sizeof(CommandList_struct), &cmd_dma_handle);
		if (c == NULL)
			return NULL;
		memset(c, 0, sizeof(CommandList_struct));

		c->cmdindex = -1;

		c->err_info = (ErrorInfo_struct *)
		    pci_alloc_consistent(h->pdev, sizeof(ErrorInfo_struct),
			    &err_dma_handle);

		if (c->err_info == NULL) {
			pci_free_consistent(h->pdev,
				sizeof(CommandList_struct), c, cmd_dma_handle);
			return NULL;
		}
		memset(c->err_info, 0, sizeof(ErrorInfo_struct));
	} else {		/* get it out of the controllers pool */

		do {
			i = find_first_zero_bit(h->cmd_pool_bits, h->nr_cmds);
			if (i == h->nr_cmds)
				return NULL;
		} while (test_and_set_bit
			 (i & (BITS_PER_LONG - 1),
			  h->cmd_pool_bits + (i / BITS_PER_LONG)) != 0);
#ifdef HPSA_DEBUG
		printk(KERN_DEBUG "hpsa: using command buffer %d\n", i);
#endif
		c = h->cmd_pool + i;
		memset(c, 0, sizeof(CommandList_struct));
		cmd_dma_handle = h->cmd_pool_dhandle
		    + i * sizeof(CommandList_struct);
		c->err_info = h->errinfo_pool + i;
		memset(c->err_info, 0, sizeof(ErrorInfo_struct));
		err_dma_handle = h->errinfo_pool_dhandle
		    + i * sizeof(ErrorInfo_struct);
		h->nr_allocs++;

		c->cmdindex = i;
	}

	c->busaddr = (__u32) cmd_dma_handle;
	temp64.val = (__u64) err_dma_handle;
	c->ErrDesc.Addr.lower = temp64.val32.lower;
	c->ErrDesc.Addr.upper = temp64.val32.upper;
	c->ErrDesc.Len = sizeof(ErrorInfo_struct);

	c->ctlr = h->ctlr;
	return c;
}

/*
 * Frees a command block that was previously allocated with cmd_alloc().
 */
static void cmd_free(ctlr_info_t *h, CommandList_struct *c, int got_from_pool)
{
	int i;
	u64bit temp64;

	if (!got_from_pool) {
		temp64.val32.lower = c->ErrDesc.Addr.lower;
		temp64.val32.upper = c->ErrDesc.Addr.upper;
		pci_free_consistent(h->pdev, sizeof(ErrorInfo_struct),
				    c->err_info, (dma_addr_t) temp64.val);
		pci_free_consistent(h->pdev, sizeof(CommandList_struct),
				    c, (dma_addr_t) c->busaddr);
	} else {
		i = c - h->cmd_pool;
		clear_bit(i & (BITS_PER_LONG - 1),
			  h->cmd_pool_bits + (i / BITS_PER_LONG));
		h->nr_frees++;
	}
}

#ifdef CONFIG_COMPAT

static int do_ioctl(struct scsi_device *dev, int cmd, void *arg)
{
	int ret;
	lock_kernel();
	ret = hpsa_ioctl(dev, cmd, arg);
	unlock_kernel();
	return ret;
}

static int hpsa_ioctl32_passthru(struct scsi_device *dev, int cmd, void *arg);
static int hpsa_ioctl32_big_passthru(struct scsi_device *dev, int cmd, void *arg);

static int hpsa_compat_ioctl(struct scsi_device *dev, int cmd, void *arg)
{
	switch (cmd) {
	case CCISS_DEREGDISK:
	case CCISS_REGNEWDISK:
	case CCISS_REGNEWD:
	case CCISS_RESCANDISK:
	case CCISS_GETPCIINFO:
	case CCISS_GETINTINFO:
	case CCISS_SETINTINFO:
	case CCISS_GETNODENAME:
	case CCISS_SETNODENAME:
	case CCISS_GETHEARTBEAT:
	case CCISS_GETBUSTYPES:
	case CCISS_GETFIRMVER:
	case CCISS_GETDRIVVER:
	case CCISS_REVALIDVOLS:
	case CCISS_GETLUNINFO:
		return do_ioctl(dev, cmd, arg);
	case CCISS_PASSTHRU32:
		return hpsa_ioctl32_passthru(dev, cmd, arg);
	case CCISS_BIG_PASSTHRU32:
		return hpsa_ioctl32_big_passthru(dev, cmd, arg);
	default:
		return -ENOIOCTLCMD;
	}
}

static int hpsa_ioctl32_passthru(struct scsi_device *dev, int cmd, void *arg)
{
	IOCTL32_Command_struct __user *arg32 =
	    (IOCTL32_Command_struct __user *) arg;
	IOCTL_Command_struct arg64;
	IOCTL_Command_struct __user *p = compat_alloc_user_space(sizeof(arg64));
	int err;
	u32 cp;

	err = 0;
	err |=
	    copy_from_user(&arg64.LUN_info, &arg32->LUN_info,
			   sizeof(arg64.LUN_info));
	err |=
	    copy_from_user(&arg64.Request, &arg32->Request,
			   sizeof(arg64.Request));
	err |=
	    copy_from_user(&arg64.error_info, &arg32->error_info,
			   sizeof(arg64.error_info));
	err |= get_user(arg64.buf_size, &arg32->buf_size);
	err |= get_user(cp, &arg32->buf);
	arg64.buf = compat_ptr(cp);
	err |= copy_to_user(p, &arg64, sizeof(arg64));

	if (err)
		return -EFAULT;

	err = do_ioctl(dev, CCISS_PASSTHRU, (void *)p);
	if (err)
		return err;
	err |=
	    copy_in_user(&arg32->error_info, &p->error_info,
			 sizeof(arg32->error_info));
	if (err)
		return -EFAULT;
	return err;
}

static int hpsa_ioctl32_big_passthru(struct scsi_device *dev, int cmd, void *arg)
{
	BIG_IOCTL32_Command_struct __user *arg32 =
	    (BIG_IOCTL32_Command_struct __user *) arg;
	BIG_IOCTL_Command_struct arg64;
	BIG_IOCTL_Command_struct __user *p =
	    compat_alloc_user_space(sizeof(arg64));
	int err;
	u32 cp;

	err = 0;
	err |=
	    copy_from_user(&arg64.LUN_info, &arg32->LUN_info,
			   sizeof(arg64.LUN_info));
	err |=
	    copy_from_user(&arg64.Request, &arg32->Request,
			   sizeof(arg64.Request));
	err |=
	    copy_from_user(&arg64.error_info, &arg32->error_info,
			   sizeof(arg64.error_info));
	err |= get_user(arg64.buf_size, &arg32->buf_size);
	err |= get_user(arg64.malloc_size, &arg32->malloc_size);
	err |= get_user(cp, &arg32->buf);
	arg64.buf = compat_ptr(cp);
	err |= copy_to_user(p, &arg64, sizeof(arg64));

	if (err)
		return -EFAULT;

	err = do_ioctl(dev, CCISS_BIG_PASSTHRU, (void *)p);
	if (err)
		return err;
	err |=
	    copy_in_user(&arg32->error_info, &p->error_info,
			 sizeof(arg32->error_info));
	if (err)
		return -EFAULT;
	return err;
}
#endif

/*
 * ioctl
 */
static int hpsa_ioctl(struct scsi_device *dev, int cmd, void *arg)
{

	int ctlr;
	ctlr_info_t *ci, *host;
	void __user *argp = (void __user *)arg;

	ci = (ctlr_info_t *) dev->host->hostdata[0];
	if (ci == NULL) {
		printk(KERN_INFO "hpsa_ioctl hostdata is NULL for host %d.\n", dev->host->host_no);
		return -EINVAL;
	}
	ctlr = ci->ctlr;	/* Get our index into the hba[] array */
 #ifdef HPSA_DEBUG
	printk(KERN_INFO "hpsa_ioctl for hba %d with hostno %d.\n", ctlr, dev->host->host_no);
#endif
	host = hba[ctlr];

	switch (cmd) {

	/* CCISS_BIG_PASSTHRU IOCTL 3 is generated during rescans, and was the only signal found
	 * to trigger so that we could recoznize newly added or removed drives.
	 */
	case 3:
	{
		printk(KERN_INFO "hpsa: controller %d: rescanning for configuration changes.\n", ctlr);
		hpsa_update_scsi_devices(ctlr, dev->host->host_no);
		return 0;
	}
	case CCISS_DEREGDISK:
	case CCISS_REGNEWDISK:
	case CCISS_REGNEWD:
	{
		printk(KERN_ERR "hpsa: controller %d: rescanning for configuration changes.\n", ctlr);
		hpsa_update_scsi_devices(ctlr, dev->host->host_no);
		return 0;
		
	}
	case CCISS_GETPCIINFO:
		{
			hpsa_pci_info_struct pciinfo;

			if (!arg)
				return -EINVAL;
			pciinfo.domain = pci_domain_nr(host->pdev->bus);
			pciinfo.bus = host->pdev->bus->number;
			pciinfo.dev_fn = host->pdev->devfn;
			pciinfo.board_id = host->board_id;
			if (copy_to_user
			    (argp, &pciinfo, sizeof(hpsa_pci_info_struct)))
				return -EFAULT;
			return 0;
		}
	case CCISS_GETDRIVVER:
		{
			DriverVer_type DriverVer = DRIVER_VERSION;

			if (!arg)
				return -EINVAL;

			if (copy_to_user
			    (argp, &DriverVer, sizeof(DriverVer_type)))
				return -EFAULT;
			return 0;
		}

	case CCISS_PASSTHRU:
		{
			IOCTL_Command_struct iocommand;
			CommandList_struct *c;
			char *buff = NULL;
			u64bit temp64;
			unsigned long flags;
			DECLARE_COMPLETION_ONSTACK(wait);

			if (!arg)
				return -EINVAL;

			if (!capable(CAP_SYS_RAWIO))
				return -EPERM;

			if (copy_from_user
			    (&iocommand, argp, sizeof(IOCTL_Command_struct)))
				return -EFAULT;
			if ((iocommand.buf_size < 1) &&
			    (iocommand.Request.Type.Direction != XFER_NONE)) {
				return -EINVAL;
			}
			if (iocommand.buf_size > 0) {
				buff = kmalloc(iocommand.buf_size, GFP_KERNEL);
				if (buff == NULL)
					return -EFAULT;
			}
			if (iocommand.Request.Type.Direction == XFER_WRITE) {
				/* Copy the data into the buffer we created */
				if (copy_from_user
				    (buff, iocommand.buf, iocommand.buf_size)) {
					kfree(buff);
					return -EFAULT;
				}
			} else {
				memset(buff, 0, iocommand.buf_size);
			}
			if ((c = cmd_alloc(host, 0)) == NULL) {
				kfree(buff);
				return -ENOMEM;
			}
			// Fill in the command type
			c->cmd_type = CMD_IOCTL_PEND;
			// Fill in Command Header
			c->Header.ReplyQueue = 0;	// unused in simple mode
			if (iocommand.buf_size > 0)	// buffer to fill
			{
				c->Header.SGList = 1;
				c->Header.SGTotal = 1;
			} else	// no buffers to fill
			{
				c->Header.SGList = 0;
				c->Header.SGTotal = 0;
			}
			c->Header.LUN = iocommand.LUN_info;
			c->Header.Tag.lower = c->busaddr;	// use the kernel address the cmd block for tag

			// Fill in Request block
			c->Request = iocommand.Request;

			// Fill in the scatter gather information
			if (iocommand.buf_size > 0) {
				temp64.val = pci_map_single(host->pdev, buff,
					iocommand.buf_size,
					PCI_DMA_BIDIRECTIONAL);
				c->SG[0].Addr.lower = temp64.val32.lower;
				c->SG[0].Addr.upper = temp64.val32.upper;
				c->SG[0].Len = iocommand.buf_size;
				c->SG[0].Ext = 0;	// we are not chaining
			}
			c->waiting = &wait;

			/* Put the request on the tail of the request queue */
			spin_lock_irqsave(HPSA_LOCK(ctlr), flags);
			addQ(&host->reqQ, c);
			host->Qdepth++;
			start_io(host);
			spin_unlock_irqrestore(HPSA_LOCK(ctlr), flags);

			wait_for_completion(&wait);

			/* unlock the buffers from DMA */
			temp64.val32.lower = c->SG[0].Addr.lower;
			temp64.val32.upper = c->SG[0].Addr.upper;
			pci_unmap_single(host->pdev, (dma_addr_t) temp64.val,
					 iocommand.buf_size,
					 PCI_DMA_BIDIRECTIONAL);

			/* Copy the error information out */
			iocommand.error_info = *(c->err_info);
			if (copy_to_user
			    (argp, &iocommand, sizeof(IOCTL_Command_struct))) {
				kfree(buff);
				cmd_free(host, c, 0);
				return -EFAULT;
			}

			if (iocommand.Request.Type.Direction == XFER_READ) {
				/* Copy the data out of the buffer we created */
				if (copy_to_user
				    (iocommand.buf, buff, iocommand.buf_size)) {
					kfree(buff);
					cmd_free(host, c, 0);
					return -EFAULT;
				}
			}
			kfree(buff);
			cmd_free(host, c, 0);
			return 0;
		}
	case CCISS_BIG_PASSTHRU:{
			BIG_IOCTL_Command_struct *ioc;
			CommandList_struct *c;
			unsigned char **buff = NULL;
			int *buff_size = NULL;
			u64bit temp64;
			unsigned long flags;
			BYTE sg_used = 0;
			int status = 0;
			int i;
			DECLARE_COMPLETION_ONSTACK(wait);
			__u32 left;
			__u32 sz;
			BYTE __user *data_ptr;

			if (!arg)
				return -EINVAL;
			if (!capable(CAP_SYS_RAWIO))
				return -EPERM;
			ioc = (BIG_IOCTL_Command_struct *)
			    kmalloc(sizeof(*ioc), GFP_KERNEL);
			if (!ioc) {
				status = -ENOMEM;
				goto cleanup1;
			}
			if (copy_from_user(ioc, argp, sizeof(*ioc))) {
				status = -EFAULT;
				goto cleanup1;
			}
			if ((ioc->buf_size < 1) &&
			    (ioc->Request.Type.Direction != XFER_NONE)) {
				status = -EINVAL;
				goto cleanup1;
			}
			/* Check kmalloc limits  using all SGs */
			if (ioc->malloc_size > MAX_KMALLOC_SIZE) {
				status = -EINVAL;
				goto cleanup1;
			}
			if (ioc->buf_size > ioc->malloc_size * MAXSGENTRIES) {
				status = -EINVAL;
				goto cleanup1;
			}
			buff =
			    kzalloc(MAXSGENTRIES * sizeof(char *), GFP_KERNEL);
			if (!buff) {
				status = -ENOMEM;
				goto cleanup1;
			}
			buff_size = (int *)kmalloc(MAXSGENTRIES * sizeof(int),
						   GFP_KERNEL);
			if (!buff_size) {
				status = -ENOMEM;
				goto cleanup1;
			}
			left = ioc->buf_size;
			data_ptr = ioc->buf;
			while (left) {
				sz = (left >
				      ioc->malloc_size) ? ioc->
				    malloc_size : left;
				buff_size[sg_used] = sz;
				buff[sg_used] = kmalloc(sz, GFP_KERNEL);
				if (buff[sg_used] == NULL) {
					status = -ENOMEM;
					goto cleanup1;
				}
				if (ioc->Request.Type.Direction == XFER_WRITE) {
					if (copy_from_user
					    (buff[sg_used], data_ptr, sz)) {
						status = -ENOMEM;
						goto cleanup1;
					}
				} else {
					memset(buff[sg_used], 0, sz);
				}
				left -= sz;
				data_ptr += sz;
				sg_used++;
			}
			if ((c = cmd_alloc(host, 0)) == NULL) {
				status = -ENOMEM;
				goto cleanup1;
			}
			c->cmd_type = CMD_IOCTL_PEND;
			c->Header.ReplyQueue = 0;

			if (ioc->buf_size > 0) {
				c->Header.SGList = sg_used;
				c->Header.SGTotal = sg_used;
			} else {
				c->Header.SGList = 0;
				c->Header.SGTotal = 0;
			}
			c->Header.LUN = ioc->LUN_info;
			c->Header.Tag.lower = c->busaddr;

			c->Request = ioc->Request;
			if (ioc->buf_size > 0) {
				int i;
				for (i = 0; i < sg_used; i++) {
					temp64.val =
					    pci_map_single(host->pdev, buff[i],
						    buff_size[i],
						    PCI_DMA_BIDIRECTIONAL);
					c->SG[i].Addr.lower =
					    temp64.val32.lower;
					c->SG[i].Addr.upper =
					    temp64.val32.upper;
					c->SG[i].Len = buff_size[i];
					c->SG[i].Ext = 0;	/* we are not chaining */
				}
			}
			c->waiting = &wait;
			/* Put the request on the tail of the request queue */
			spin_lock_irqsave(HPSA_LOCK(ctlr), flags);
			addQ(&host->reqQ, c);
			host->Qdepth++;
			start_io(host);
			spin_unlock_irqrestore(HPSA_LOCK(ctlr), flags);
			wait_for_completion(&wait);
			/* unlock the buffers from DMA */
			for (i = 0; i < sg_used; i++) {
				temp64.val32.lower = c->SG[i].Addr.lower;
				temp64.val32.upper = c->SG[i].Addr.upper;
				pci_unmap_single(host->pdev,
					(dma_addr_t) temp64.val, buff_size[i],
					PCI_DMA_BIDIRECTIONAL);
			}
			/* Copy the error information out */
			ioc->error_info = *(c->err_info);
			if (copy_to_user(argp, ioc, sizeof(*ioc))) {
				cmd_free(host, c, 0);
				status = -EFAULT;
				goto cleanup1;
			}
			if (ioc->Request.Type.Direction == XFER_READ) {
				/* Copy the data out of the buffer we created */
				BYTE __user *ptr = ioc->buf;
				for (i = 0; i < sg_used; i++) {
					if (copy_to_user
					    (ptr, buff[i], buff_size[i])) {
						cmd_free(host, c, 0);
						status = -EFAULT;
						goto cleanup1;
					}
					ptr += buff_size[i];
				}
			}
			cmd_free(host, c, 0);
			status = 0;
		      cleanup1:
			if (buff) {
				for (i = 0; i < sg_used; i++)
					kfree(buff[i]);
				kfree(buff);
			}
			kfree(buff_size);
			kfree(ioc);
			return status;
		}
	default:
		printk(KERN_WARNING "hpsa: Unknown ioctl of 0x%x received\n", cmd);
		return -ENOTTY;
	}
}


static int fill_cmd(CommandList_struct *c, __u8 cmd, int ctlr, void *buff, size_t size, unsigned int use_unit_num,	/* 0: address the controller,
															   1: address logical volume log_unit,
															   2: periph device address is scsi3addr */
		    unsigned int log_unit, __u8 page_code,
		    unsigned char *scsi3addr, int cmd_type)
{
	ctlr_info_t *h = hba[ctlr];
	u64bit buff_dma_handle;
	int status = IO_OK;

	c->cmd_type = CMD_IOCTL_PEND;
	c->Header.ReplyQueue = 0;
	if (buff != NULL && size > 0) {
		c->Header.SGList = 1;
		c->Header.SGTotal = 1;
	} else {
		c->Header.SGList = 0;
		c->Header.SGTotal = 0;
	}
	c->Header.Tag.lower = c->busaddr;

	c->Request.Type.Type = cmd_type;
	if (cmd_type == TYPE_CMD) {
		switch (cmd) {
		case HPSA_INQUIRY:
			/* If the logical unit number is 0 then, this is going
			   to controller so It's a physical command
			   mode = 0 target = 0.  So we have nothing to write.
			   otherwise, if use_unit_num == 1,
			   mode = 1(volume set addressing) target = LUNID
			   otherwise, if use_unit_num == 2,
			   mode = 0(periph dev addr) target = scsi3addr */
			if (use_unit_num == 1) {
				c->Header.LUN.LogDev.VolId =
				    h->drv[log_unit].LunID;
				c->Header.LUN.LogDev.Mode = 1;
			} else if (use_unit_num == 2) {
				memcpy(c->Header.LUN.LunAddrBytes, scsi3addr,
				       8);
				c->Header.LUN.LogDev.Mode = 0;
			}
			/* are we trying to read a vital product page */
			if (page_code != 0) {
				c->Request.CDB[1] = 0x01;
				c->Request.CDB[2] = page_code;
			}
			c->Request.CDBLen = 6;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_READ;
			c->Request.Timeout = 0;
			c->Request.CDB[0] = HPSA_INQUIRY;
			c->Request.CDB[4] = size & 0xFF;
			break;
		case HPSA_REPORT_LOG:
		case HPSA_REPORT_PHYS:
			/* Talking to controller so It's a physical command
			   mode = 00 target = 0.  Nothing to write.
			 */
			c->Request.CDBLen = 12;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_READ;
			c->Request.Timeout = 0;
			c->Request.CDB[0] = cmd;
			c->Request.CDB[6] = (size >> 24) & 0xFF;	//MSB
			c->Request.CDB[7] = (size >> 16) & 0xFF;
			c->Request.CDB[8] = (size >> 8) & 0xFF;
			c->Request.CDB[9] = size & 0xFF;
			break;

		case HPSA_READ_CAPACITY:
			c->Header.LUN.LogDev.VolId = h->drv[log_unit].LunID;
			c->Header.LUN.LogDev.Mode = 1;
			c->Request.CDBLen = 10;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_READ;
			c->Request.Timeout = 0;
			c->Request.CDB[0] = cmd;
			break;
		case HPSA_CACHE_FLUSH:
			c->Request.CDBLen = 12;
			c->Request.Type.Attribute = ATTR_SIMPLE;
			c->Request.Type.Direction = XFER_WRITE;
			c->Request.Timeout = 0;
			c->Request.CDB[0] = BMIC_WRITE;
			c->Request.CDB[6] = BMIC_CACHE_FLUSH;
			break;
		default:
			printk(KERN_WARNING
			       "hpsa%d:  Unknown Command 0x%c\n", ctlr, cmd);
			return IO_ERROR;
		}
	} else if (cmd_type == TYPE_MSG) {
		switch (cmd) {

 case  HPSA_DEVICE_RESET_MSG:
                        /* If the logical unit number is 0 then, this is going
                                to controller so It's a physical command
                                mode = 0 target = 0.
                                So we have nothing to write.
                                otherwise, if use_unit_num == 1,
                                mode = 1(volume set addressing) target = LUNID
                                otherwise, if use_unit_num == 2,
                                mode = 0(periph dev addr) target = scsi3addr
                        */
                        if (use_unit_num == 1) {
                                c->Header.LUN.LogDev.VolId=
                                        hba[ctlr]->drv[log_unit].LunID;
                                c->Header.LUN.LogDev.Mode = 1;
                        }
                        else if (use_unit_num == 2) {
                                memcpy(c->Header.LUN.LunAddrBytes,scsi3addr,8);
                                c->Header.LUN.LogDev.Mode = 0;
                                                        /* phys dev addr */
                        }

                        c->Request.CDBLen = 12;
                        c->Request.Type.Type =  1; /* It is a MSG as opposed to a CMD */
                        c->Request.Type.Attribute = ATTR_SIMPLE;
                        c->Request.Type.Direction = XFER_WRITE; /* Write */
                        c->Request.Timeout = 0; /* Don't time out */
                        c->Request.CDB[0] =  0x01; // RESET_MSG is 0x01
                        c->Request.CDB[1] = 0x04;  // Reset LunID above
                        c->Request.CDB[4] = 0x00;  // If bytes 4-7 are zero, it means reset the LunID device
                        c->Request.CDB[5] = 0x00;
                        c->Request.CDB[6] = 0x00;
                        c->Request.CDB[7] = 0x00;
			printk(KERN_WARNING "hpsa: fillcmd(): cmd 0x%x, log_unit 0x%x, c->Header.LUN.LogDev.VolId 0x%x\n", cmd, log_unit, c->Header.LUN.LogDev.VolId); 
                break;

		case  HPSA_ABORT_MSG:
			printk(KERN_WARNING "hpsa: Processing abort command\n");

		case  HPSA_BUS_RESET_MSG:
			/* If the logical unit number is 0 then, this is going
				to controller so It's a physical command
				mode = 0 target = 0.
				So we have nothing to write. 
				otherwise, if use_unit_num == 1,
				mode = 1(volume set addressing) target = LUNID
				otherwise, if use_unit_num == 2,
				mode = 0(periph dev addr) target = scsi3addr
			*/
			if (use_unit_num == 1) {
				c->Header.LUN.LogDev.VolId=
                                	hba[ctlr]->drv[log_unit].LunID;
                        	c->Header.LUN.LogDev.Mode = 1;
			}
			else if (use_unit_num == 2) {
				memcpy(c->Header.LUN.LunAddrBytes,scsi3addr,8);
				c->Header.LUN.LogDev.Mode = 0; 
							/* phys dev addr */
			}

			c->Request.CDBLen = 12;
			c->Request.Type.Type =  1; /* It is a MSG as opposed to a CMD */
			c->Request.Type.Attribute = ATTR_SIMPLE;  
			c->Request.Type.Direction = XFER_WRITE; /* Write */
			c->Request.Timeout = 0; /* Don't time out */
			c->Request.CDB[0] =  0x01; // RESET_MSG is 0x01
			c->Request.CDB[1] = 0x04;  // Reset Bus of LunID above
			c->Request.CDB[7] = 0xff;  // If bytes 4-7 are non-zero it means reset the bus associated with the specified LunID
		break;
		case  HPSA_HOST_RESET_MSG:
			/* If the logical unit number is 0 then, this is going
				to controller so It's a physical command
				mode = 0 target = 0.
				So we have nothing to write. 
				otherwise, if use_unit_num == 1,
				mode = 1(volume set addressing) target = LUNID
				otherwise, if use_unit_num == 2,
				mode = 0(periph dev addr) target = scsi3addr
			*/
			if (use_unit_num == 1) {
				c->Header.LUN.LogDev.VolId=
                                	hba[ctlr]->drv[log_unit].LunID;
                        	c->Header.LUN.LogDev.Mode = 1;
			}
			else if (use_unit_num == 2) {
				memcpy(c->Header.LUN.LunAddrBytes,scsi3addr,8);
				c->Header.LUN.LogDev.Mode = 0; 
							/* phys dev addr */
			}

			c->Request.CDBLen = 12;
			c->Request.Type.Type =  1; /* It is a MSG as opposed to a CMD */
			c->Request.Type.Attribute = ATTR_SIMPLE;  
			c->Request.Type.Direction = XFER_WRITE; /* Write */
			c->Request.Timeout = 0; /* Don't time out */
			c->Request.CDB[0] =  0x01; // RESET_MSG is 0x01
			c->Request.CDB[1] = 0x00;  // Reset Host
		break;

		default:
			printk(KERN_WARNING
			       "hpsa%d: unknown message type %d\n", ctlr, cmd);
			return IO_ERROR;
		}
	} else {
		printk(KERN_WARNING
		       "hpsa%d: unknown command type %d\n", ctlr, cmd_type);
		return IO_ERROR;
	}
	/* Fill in the scatter gather information */
	if (size > 0) {
		buff_dma_handle.val = (__u64) pci_map_single(h->pdev,
							     buff, size,
							     PCI_DMA_BIDIRECTIONAL);
		c->SG[0].Addr.lower = buff_dma_handle.val32.lower;
		c->SG[0].Addr.upper = buff_dma_handle.val32.upper;
		c->SG[0].Len = size;
		c->SG[0].Ext = 0;	/* we are not chaining */
	}
	return status;
}


/*
 *   Wait polling for a command to complete.
 *   The memory mapped FIFO is polled for the completion.
 *   Used only at init time, interrupts from the HBA are disabled.
 */
static unsigned long pollcomplete(int ctlr)
{
	unsigned long done;
	int i;

	/* Wait (up to 20 seconds) for a command to complete */

	for (i = 20 * HZ; i > 0; i--) {
		done = hba[ctlr]->access.command_completed(hba[ctlr]);
		if (done == FIFO_EMPTY)
			schedule_timeout_uninterruptible(1);
		else
		{
			return done;
		}
	}
	/* Invalid address to tell caller we ran out of time */
	printk(KERN_WARNING "hpsa: pollcomplete(): returning 1\n");
	return 1;
}

static int add_sendcmd_reject(__u8 cmd, int ctlr, unsigned long complete)
{
	/* We get in here if sendcmd() is polling for completions
	   and gets some command back that it wasn't expecting --
	   something other than that which it just sent down.
	   Ordinarily, that shouldn't happen, but it can happen when
	   the scsi tape stuff gets into error handling mode, and
	   starts using sendcmd() to try to abort commands and
	   reset tape drives.  In that case, sendcmd may pick up
	   completions of commands that were sent to logical drives
	   through the block i/o system, or hpsa ioctls completing, etc.
	   In that case, we need to save those completions for later
	   processing by the interrupt handler.
	 */

	struct sendcmd_reject_list *srl = &hba[ctlr]->scsi_rejects;

	/* If it's not the scsi stuff doing error handling, (abort */
	/* or reset) then we don't expect anything weird. */
	if (	cmd != HPSA_DEVICE_RESET_MSG && 
		cmd != HPSA_BUS_RESET_MSG && 
		cmd != HPSA_HOST_RESET_MSG && 
		cmd != HPSA_ABORT_MSG && 
		cmd != HPSA_REPORT_PHYS && 
		cmd != HPSA_INQUIRY && 
		cmd != HPSA_CACHE_FLUSH) {
		printk(KERN_WARNING "hpsa hpsa%d: SendCmd "
		       "Invalid command type, invalid list address returned! (%lx)\n",
		       ctlr, complete);
		/* not much we can do. */
		return 1;
	}

	/* We've sent down an abort or reset, but something else
	   has completed */
	if (srl->ncompletions >= (hba[ctlr]->nr_cmds + 2)) {
		/* Uh oh.  No room to save it for later... */
		printk(KERN_WARNING "hpsa%d: Sendcmd: Invalid command addr, "
		       "reject list overflow, command lost!\n", ctlr);
		return 1;
	}
	/* Save it for later */
	srl->complete[srl->ncompletions] = complete;
	srl->ncompletions++;
	return 0;
}

/*
 * Send a command to the controller, and wait for it to complete.
 * Only used at init time or to send abort/reset messages
 */
// FIXME - last parameter of sendcmd is now cmd_type, change on things that call sendcmd() in hpsa_scsi.c
static int sendcmd(__u8 cmd, int ctlr, void *buff, size_t size, unsigned int use_unit_num,	/* 0: address the controller,
												   1: address logical volume log_unit,
												   2: periph device address is scsi3addr */
		   unsigned int log_unit,
		   __u8 page_code, unsigned char *scsi3addr, int cmd_type)
{
	CommandList_struct *c;
	int i;
	unsigned long complete;
	ctlr_info_t *info_p = hba[ctlr];
	u64bit buff_dma_handle;
	int status, done = 0;

	if ((c = cmd_alloc(info_p, 1)) == NULL) {
		printk(KERN_WARNING "hpsa: unable to get memory");
		return IO_ERROR;
	}
	status = fill_cmd(c, cmd, ctlr, buff, size, use_unit_num,
			  log_unit, page_code, scsi3addr, cmd_type);
	if (status != IO_OK) {
		cmd_free(info_p, c, 1);
		return status;
	}
      resend_cmd1:
	/*
	 * Disable interrupt
	 */
#ifdef HPSA_DEBUG
	printk(KERN_DEBUG "hpsa: turning intr off\n");
#endif				/* HPSA_DEBUG */
	info_p->access.set_intr_mask(info_p, HPSA_INTR_OFF);

	/* Make sure there is room in the command FIFO */
	/* Actually it should be completely empty at this time */
	/* unless we are in here doing error handling for the scsi */
	/* tape side of the driver. */
	for (i = 200000; i > 0; i--) {
		/* if fifo isn't full go */
		if (!(info_p->access.fifo_full(info_p))) {

			break;
		}
		udelay(10);
#ifdef HPSA_DEBUG
		printk(KERN_WARNING "hpsa hpsa%d: SendCmd FIFO full,"
		       " waiting!\n", ctlr);
#endif
	}
	/*
	 * Send the cmd
	 */
	info_p->access.submit_command(info_p, c);
	done = 0;
	do {
		complete = pollcomplete(ctlr);

#ifdef HPSA_DEBUG
		printk(KERN_DEBUG "hpsa: command completed\n");
#endif				/* HPSA_DEBUG */

		if (complete == 1) {
			printk(KERN_WARNING
			       "hpsa hpsa%d: SendCmd Timeout out, "
			       "No command list address returned!\n", ctlr);
			status = IO_ERROR;
			done = 1;
			break;
		}

		/* This will need to change for direct lookup completions */
		if ((complete & HPSA_ERROR_BIT)
		    && (complete & ~HPSA_ERROR_BIT) == c->busaddr) {
			/* if data overrun or underun on Report command
			   ignore it
			 */
			if (((c->Request.CDB[0] == HPSA_REPORT_LOG) ||
			     (c->Request.CDB[0] == HPSA_REPORT_PHYS) ||
			     (c->Request.CDB[0] == HPSA_INQUIRY)) &&
			    ((c->err_info->CommandStatus ==
			      CMD_DATA_OVERRUN) ||
			     (c->err_info->CommandStatus == CMD_DATA_UNDERRUN)
			    )) {
				complete = c->busaddr;
			} else {
				if (c->err_info->CommandStatus ==
				    CMD_UNSOLICITED_ABORT) {
					printk(KERN_WARNING "hpsa%d: "
					       "unsolicited abort %p\n",
					       ctlr, c);
					if (c->retry_count < MAX_CMD_RETRIES) {
						printk(KERN_WARNING
						       "hpsa%d: retrying %p\n",
						       ctlr, c);
						c->retry_count++;
						/* erase the old error */
						/* information */
						memset(c->err_info, 0,
						       sizeof
						       (ErrorInfo_struct));
						goto resend_cmd1;
					} else {
						printk(KERN_WARNING
						       "hpsa%d: retried %p too "
						       "many times\n", ctlr, c);
						status = IO_ERROR;
						goto cleanup1;
					}
				} else if (c->err_info->CommandStatus ==
					   CMD_UNABORTABLE) {
					printk(KERN_WARNING
					       "hpsa%d: command could not be aborted.\n",
					       ctlr);
					status = IO_ERROR;
					goto cleanup1;
				}
				printk(KERN_WARNING "hpsa hpsa%d: sendcmd"
				       " Error %x \n", ctlr,
				       c->err_info->CommandStatus);
				printk(KERN_WARNING "hpsa hpsa%d: sendcmd"
				       " offensive info\n"
				       "  size %x\n   num %x   value %x\n",
				       ctlr,
				       c->err_info->MoreErrInfo.Invalid_Cmd.
				       offense_size,
				       c->err_info->MoreErrInfo.Invalid_Cmd.
				       offense_num,
				       c->err_info->MoreErrInfo.Invalid_Cmd.
				       offense_value);
				status = IO_ERROR;
				goto cleanup1;
			}
		}
		/* This will need changing for direct lookup completions */
		if (complete != c->busaddr) {
			if (add_sendcmd_reject(cmd, ctlr, complete) != 0) {
				BUG();	/* we are pretty much hosed if we get here. */
			}
			continue;
		} else
			done = 1;
	} while (!done);

      cleanup1:
	/* unlock the data buffer from DMA */
	buff_dma_handle.val32.lower = c->SG[0].Addr.lower;
	buff_dma_handle.val32.upper = c->SG[0].Addr.upper;
	pci_unmap_single(info_p->pdev, (dma_addr_t) buff_dma_handle.val,
			 c->SG[0].Len, PCI_DMA_BIDIRECTIONAL);
	/* if we saved some commands for later, process them now. */
	if (info_p->scsi_rejects.ncompletions > 0)
		do_hpsa_intr(0, info_p, NULL);
	cmd_free(info_p, c, 1);
	return status;
}

/*
 * Map (physical) PCI mem into (virtual) kernel space
 */
static void __iomem *remap_pci_mem(ulong base, ulong size)
{
	ulong page_base = ((ulong) base) & PAGE_MASK;
	ulong page_offs = ((ulong) base) - page_base;
	void __iomem *page_remapped = ioremap(page_base, page_offs + size);

	return page_remapped ? (page_remapped + page_offs) : NULL;
}

/*
 * Takes jobs of the Q and sends them to the hardware, then puts it on
 * the Q to wait for completion.
 */
static void start_io(ctlr_info_t *h)
{
	CommandList_struct *c;

	while ((c = h->reqQ) != NULL) {
		/* can't do anything if fifo is full */
		if ((h->access.fifo_full(h))) {
			printk(KERN_WARNING "hpsa: fifo full\n");
			break;
		}

		/* Get the first entry from the Request Q */
		removeQ(&(h->reqQ), c);
		h->Qdepth--;

		/* Tell the controller execute command */
		h->access.submit_command(h, c);

		/* Put job onto the completed Q */
		addQ(&(h->cmpQ), c);
	}
}

/* Assumes that HPSA_LOCK(h->ctlr) is held. */
/* Zeros out the error record and then resends the command back */
/* to the controller */
static inline void resend_hpsa_cmd(ctlr_info_t *h, CommandList_struct *c)
{
	/* erase the old error information */
	memset(c->err_info, 0, sizeof(ErrorInfo_struct));

	/* add it to software queue and then send it to the controller */
	addQ(&(h->reqQ), c);
	h->Qdepth++;
	if (h->Qdepth > h->maxQsinceinit)
		h->maxQsinceinit = h->Qdepth;

	start_io(h);
}

/* checks the status of the job and calls complete buffers to mark all
 * buffers for the completed job. Note that this function does not need
 * to hold the hba/queue lock.
 */
static inline void complete_command(ctlr_info_t *h, CommandList_struct *cmd,
				    int timeout)
{
	int status = 1;
	int retry_cmd = 0;

	if (timeout)
		status = 0;

	if (cmd->err_info->CommandStatus != 0) {	/* an error has occurred */
		switch (cmd->err_info->CommandStatus) {
			unsigned char sense_key;
		case CMD_TARGET_STATUS:
			status = 0;

			if (cmd->err_info->ScsiStatus == 0x02) {
				printk(KERN_WARNING "hpsa: cmd %p "
				       "has CHECK CONDITION "
				       " byte 2 = 0x%x\n", cmd,
				       cmd->err_info->SenseInfo[2]
				    );
				/* check the sense key */
				sense_key = 0xf & cmd->err_info->SenseInfo[2];
				/* no status or recovered error */
				if ((sense_key == 0x0) || (sense_key == 0x1)) {
					status = 1;
				}
			} else {
				printk(KERN_WARNING "hpsa: cmd %p "
				       "has SCSI Status 0x%x\n",
				       cmd, cmd->err_info->ScsiStatus);
			}
			break;
		case CMD_DATA_UNDERRUN:
			printk(KERN_WARNING "hpsa: cmd %p has"
			       " completed with data underrun "
			       "reported\n", cmd);
			break;
		case CMD_DATA_OVERRUN:
			printk(KERN_WARNING "hpsa: cmd %p has"
			       " completed with data overrun "
			       "reported\n", cmd);
			break;
		case CMD_INVALID:
			printk(KERN_WARNING "hpsa: cmd %p is "
			       "reported invalid\n", cmd);
			status = 0;
			break;
		case CMD_PROTOCOL_ERR:
			printk(KERN_WARNING "hpsa: cmd %p has "
			       "protocol error \n", cmd);
			status = 0;
			break;
		case CMD_HARDWARE_ERR:
			printk(KERN_WARNING "hpsa: cmd %p had "
			       " hardware error\n", cmd);
			status = 0;
			break;
		case CMD_CONNECTION_LOST:
			printk(KERN_WARNING "hpsa: cmd %p had "
			       "connection lost\n", cmd);
			status = 0;
			break;
		case CMD_ABORTED:
			printk(KERN_WARNING "hpsa: cmd %p was "
			       "aborted\n", cmd);
			status = 0;
			break;
		case CMD_ABORT_FAILED:
			printk(KERN_WARNING "hpsa: cmd %p reports "
			       "abort failed\n", cmd);
			status = 0;
			break;
		case CMD_UNSOLICITED_ABORT:
			printk(KERN_WARNING "hpsa%d: unsolicited "
			       "abort %p\n", h->ctlr, cmd);
			if (cmd->retry_count < MAX_CMD_RETRIES) {
				retry_cmd = 1;
				printk(KERN_WARNING
				       "hpsa%d: retrying %p\n", h->ctlr, cmd);
				cmd->retry_count++;
			} else
				printk(KERN_WARNING
				       "hpsa%d: %p retried too "
				       "many times\n", h->ctlr, cmd);
			status = 0;
			break;
		case CMD_TIMEOUT:
			printk(KERN_WARNING "hpsa: cmd %p timedout\n", cmd);
			status = 0;
			break;
		default:
			printk(KERN_WARNING "hpsa: cmd %p returned "
			       "unknown status %x\n", cmd,
			       cmd->err_info->CommandStatus);
			status = 0;
		}
	}
	/* We need to return this command */
	if (retry_cmd) {
		resend_hpsa_cmd(h, cmd);
		return;
	}

	cmd->rq->completion_data = cmd;
	cmd->rq->errors = status;
}

static inline unsigned long get_next_completion(ctlr_info_t *h)
{
	/* Any rejects from sendcmd() lying around? Process them first */
	if (h->scsi_rejects.ncompletions == 0)
		return h->access.command_completed(h);
	else {
		struct sendcmd_reject_list *srl;
		int n;
		srl = &h->scsi_rejects;
		n = --srl->ncompletions;
		/* printk("hpsa%d: processing saved reject\n", h->ctlr); */
		printk("p");
		return srl->complete[n];
	}
}

static inline int interrupt_pending(ctlr_info_t *h)
{
	return (h->access.intr_pending(h)
		|| (h->scsi_rejects.ncompletions > 0));
}

static inline long interrupt_not_for_us(ctlr_info_t *h)
{
	return (((h->access.intr_pending(h) == 0) ||
		 (h->interrupts_enabled == 0))
		&& (h->scsi_rejects.ncompletions == 0));
}

static irqreturn_t do_hpsa_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	ctlr_info_t *h = dev_id;
	CommandList_struct *c;
	unsigned long flags;
	__u32 a, a1, a2;

	if (interrupt_not_for_us(h))
		return IRQ_NONE;
	/*
	 * If there are completed commands in the completion queue,
	 * we had better do something about it.
	 */
	spin_lock_irqsave(HPSA_LOCK(h->ctlr), flags);	
	while (interrupt_pending(h)) {
		while ((a = get_next_completion(h)) != FIFO_EMPTY) {
			a1 = a;
			if ((a & 0x04)) {
				a2 = (a >> 3);
				if (a2 >= h->nr_cmds) {
					printk(KERN_WARNING
					       "hpsa: controller hpsa%d failed, stopping.\n",
					       h->ctlr);
					return IRQ_HANDLED;
				}

				c = h->cmd_pool + a2;
				a = c->busaddr;

			} else {
				a &= ~3;
				if ((c = h->cmpQ) == NULL) {
					printk(KERN_WARNING
					       "hpsa: Completion of %08x ignored\n",
					       a1);
					continue;
				}
				while (c->busaddr != a) {
					c = c->next;
					if (c == h->cmpQ)
						break;
				}
			}
			/*
			 * If we've found the command, take it off the
			 * completion Q and free it
			 */
			if (c->busaddr == a) {
				removeQ(&h->cmpQ, c);
				if (c->cmd_type == CMD_RWREQ) {
					complete_command(h, c, 0);
				} else if (c->cmd_type == CMD_IOCTL_PEND) {
					complete(c->waiting);
				}
				else if (c->cmd_type == CMD_SCSI)
					complete_scsi_command(c, 0, a1);
				continue;
			}
		}
	}

	spin_unlock_irqrestore(HPSA_LOCK(h->ctlr), flags);
	return IRQ_HANDLED;
}

/*
 *  We cannot read the structure directly, for portability we must use
 *   the io functions.
 *   This is for debug only.
 */
#ifdef HPSA_DEBUG
static void print_cfg_table(CfgTable_struct *tb)
{
	int i;
	char temp_name[17];

	printk("Controller Configuration information\n");
	printk("------------------------------------\n");
	for (i = 0; i < 4; i++)
		temp_name[i] = readb(&(tb->Signature[i]));
	temp_name[4] = '\0';
	printk("   Signature = %s\n", temp_name);
	printk("   Spec Number = %d\n", readl(&(tb->SpecValence)));
	printk("   Transport methods supported = 0x%x\n",
	       readl(&(tb->TransportSupport)));
	printk("   Transport methods active = 0x%x\n",
	       readl(&(tb->TransportActive)));
	printk("   Requested transport Method = 0x%x\n",
	       readl(&(tb->HostWrite.TransportRequest)));
	printk("   Coalesce Interrupt Delay = 0x%x\n",
	       readl(&(tb->HostWrite.CoalIntDelay)));
	printk("   Coalesce Interrupt Count = 0x%x\n",
	       readl(&(tb->HostWrite.CoalIntCount)));
	printk("   Max outstanding commands = 0x%d\n",
	       readl(&(tb->CmdsOutMax)));
	printk("   Bus Types = 0x%x\n", readl(&(tb->BusTypes)));
	for (i = 0; i < 16; i++)
		temp_name[i] = readb(&(tb->ServerName[i]));
	temp_name[16] = '\0';
	printk("   Server Name = %s\n", temp_name);
	printk("   Heartbeat Counter = 0x%x\n\n\n", readl(&(tb->HeartBeat)));
}
#endif				/* HPSA_DEBUG */

static int find_PCI_BAR_index(struct pci_dev *pdev, unsigned long pci_bar_addr)
{
	int i, offset, mem_type, bar_type;
	if (pci_bar_addr == PCI_BASE_ADDRESS_0)	/* looking for BAR zero? */
		return 0;
	offset = 0;
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		bar_type = pci_resource_flags(pdev, i) & PCI_BASE_ADDRESS_SPACE;
		if (bar_type == PCI_BASE_ADDRESS_SPACE_IO)
			offset += 4;
		else {
			mem_type = pci_resource_flags(pdev, i) &
			    PCI_BASE_ADDRESS_MEM_TYPE_MASK;
			switch (mem_type) {
			case PCI_BASE_ADDRESS_MEM_TYPE_32:
			case PCI_BASE_ADDRESS_MEM_TYPE_1M:
				offset += 4;	/* 32 bit */
				break;
			case PCI_BASE_ADDRESS_MEM_TYPE_64:
				offset += 8;
				break;
			default:	/* reserved in PCI 2.2 */
				printk(KERN_WARNING
				       "Base address is invalid\n");
				return -1;
				break;
			}
		}
		if (offset == pci_bar_addr - PCI_BASE_ADDRESS_0)
			return i + 1;
	}
	return -1;
}

/* If MSI/MSI-X is supported by the kernel we will try to enable it on
 * controllers that are capable. If not, we use IO-APIC mode.
 */

static void __devinit hpsa_interrupt_mode(ctlr_info_t *c,
					   struct pci_dev *pdev, __u32 board_id)
{
#ifdef CONFIG_PCI_MSI
	int err;
	struct msix_entry hpsa_msix_entries[4] = { {0, 0}, {0, 1},
	{0, 2}, {0, 3}
	};

	/* Some boards advertise MSI but don't really support it */
	if ((board_id == 0x40700E11) ||
	    (board_id == 0x40800E11) ||
	    (board_id == 0x40820E11) || (board_id == 0x40830E11))
		goto default_int_mode;
	if (pci_find_capability(pdev, PCI_CAP_ID_MSIX)) {
                printk(KERN_WARNING "hpsa: MSIX\n");
		err = pci_enable_msix(pdev, hpsa_msix_entries, 4);
		if (!err) {
			c->intr[0] = hpsa_msix_entries[0].vector;
			c->intr[1] = hpsa_msix_entries[1].vector;
			c->intr[2] = hpsa_msix_entries[2].vector;
			c->intr[3] = hpsa_msix_entries[3].vector;
			c->msix_vector = 1;
			return;
		}
		if (err > 0) {
			printk(KERN_WARNING "hpsa: only %d MSI-X vectors "
			       "available\n", err);
			goto default_int_mode;
		} else {
			printk(KERN_WARNING "hpsa: MSI-X init failed %d\n",
			       err);
			goto default_int_mode;
		}
	}
	if (pci_find_capability(pdev, PCI_CAP_ID_MSI)) {
                printk(KERN_WARNING "hpsa: MSI\n");
		if (!pci_enable_msi(pdev)) {
			c->msi_vector = 1;
		} else {
			printk(KERN_WARNING "hpsa: MSI init failed\n");
		}
	}
default_int_mode:
#endif				/* CONFIG_PCI_MSI */
	/* if we get here we're going to use the default interrupt mode */
	c->intr[SIMPLE_MODE_INT] = pdev->irq;
	return;
}

static int hpsa_pci_init(ctlr_info_t *c, struct pci_dev *pdev)
{
	ushort subsystem_vendor_id, subsystem_device_id, command;
	__u32 board_id, scratchpad = 0;
	__u64 cfg_offset;
	__u32 cfg_base_addr;
	__u64 cfg_base_addr_index;
	int i, err;

	/* check to see if controller has been disabled */
	/* BEFORE trying to enable it */
	(void)pci_read_config_word(pdev, PCI_COMMAND, &command);
	if (!(command & 0x02)) {
		printk(KERN_WARNING
		       "hpsa: controller appears to be disabled\n");
		return -ENODEV;
	}

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR "hpsa: Unable to Enable PCI device\n");
		return err;
	}

	err = pci_request_regions(pdev, "hpsa");
	if (err) {
		printk(KERN_ERR "hpsa: Cannot obtain PCI resources, "
		       "aborting\n");
		return err;
	}

	subsystem_vendor_id = pdev->subsystem_vendor;
	subsystem_device_id = pdev->subsystem_device;
	board_id = (((__u32) (subsystem_device_id << 16) & 0xffff0000) |
		    subsystem_vendor_id);

#ifdef HPSA_DEBUG
	printk("command = %x\n", command);
	printk("irq = %x\n", pdev->irq);
	printk("board_id = %x\n", board_id);
#endif				/* HPSA_DEBUG */

/* If the kernel supports MSI/MSI-X we will try to enable that functionality,
 * else we use the IO-APIC interrupt assigned to us by system ROM.
 */
	hpsa_interrupt_mode(c, pdev, board_id);

	/*
	 * Memory base addr is first addr , the second points to the config
	 *   table
	 */

	c->paddr = pci_resource_start(pdev, 0);	/* addressing mode bits already removed */
#ifdef HPSA_DEBUG
	printk("address 0 = %x\n", c->paddr);
#endif				/* HPSA_DEBUG */
	c->vaddr = remap_pci_mem(c->paddr, 0x250);

	/* Wait for the board to become ready.  (PCI hotplug needs this.)
	 * We poll for up to 120 secs, once per 100ms. */
	for (i = 0; i < 1200; i++) {
		scratchpad = readl(c->vaddr + SA5_SCRATCHPAD_OFFSET);
		if (scratchpad == HPSA_FIRMWARE_READY)
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ / 10);	/* wait 100ms */
	}
	if (scratchpad != HPSA_FIRMWARE_READY) {
		printk(KERN_WARNING "hpsa: Board not ready.  Timed out.\n");
		err = -ENODEV;
		goto err_out_free_res;
	}

	/* get the address index number */
	cfg_base_addr = readl(c->vaddr + SA5_CTCFG_OFFSET);
	cfg_base_addr &= (__u32) 0x0000ffff;
#ifdef HPSA_DEBUG
	printk("cfg base address = %x\n", cfg_base_addr);
#endif				/* HPSA_DEBUG */
	cfg_base_addr_index = find_PCI_BAR_index(pdev, cfg_base_addr);
#ifdef HPSA_DEBUG
	printk("cfg base address index = %x\n", cfg_base_addr_index);
#endif				/* HPSA_DEBUG */
	if (cfg_base_addr_index == -1) {
		printk(KERN_WARNING "hpsa: Cannot find cfg_base_addr_index\n");
		err = -ENODEV;
		goto err_out_free_res;
	}

	cfg_offset = readl(c->vaddr + SA5_CTMEM_OFFSET);
#ifdef HPSA_DEBUG
	printk("cfg offset = %x\n", cfg_offset);
#endif				/* HPSA_DEBUG */
	c->cfgtable = remap_pci_mem(pci_resource_start(pdev,
						       cfg_base_addr_index) +
				    cfg_offset, sizeof(CfgTable_struct));
	c->board_id = board_id;

#ifdef HPSA_DEBUG
	print_cfg_table(c->cfgtable);
#endif				/* HPSA_DEBUG */

	// Query controller for max supported commands:
	c->max_commands = readl(&(c->cfgtable->CmdsOutMax));

	for (i = 0; i < ARRAY_SIZE(products); i++) {
		if (board_id == products[i].board_id) {
			c->product_name = products[i].product_name;
			c->access = *(products[i].access);
			// Allow room for some ioclts
			c->nr_cmds = c->max_commands - 4;
			break;
		}
	}
	if (i == ARRAY_SIZE(products)) {
		printk(KERN_WARNING "hpsa: Sorry, I don't know how"
		       " to access the Smart Array controller %08lx\n",
		       (unsigned long)board_id);
		err = -ENODEV;
		goto err_out_free_res;
	}
	if ((readb(&c->cfgtable->Signature[0]) != 'C') ||
	    (readb(&c->cfgtable->Signature[1]) != 'I') ||
	    (readb(&c->cfgtable->Signature[2]) != 'S') ||
	    (readb(&c->cfgtable->Signature[3]) != 'S')) {
		printk("Does not appear to be a valid CISS config table\n");
		err = -ENODEV;
		goto err_out_free_res;
	}
#ifdef CONFIG_X86
	{
		/* Need to enable prefetch in the SCSI core for 6400 in x86 */
		__u32 prefetch;
		prefetch = readl(&(c->cfgtable->SCSI_Prefetch));
		prefetch |= 0x100;
		writel(prefetch, &(c->cfgtable->SCSI_Prefetch));
	}
#endif

	/* Disabling DMA prefetch for the P600
	 * An ASIC bug may result in a prefetch beyond
	 * physical memory.
	 */
	if(board_id == 0x3225103C) {
		__u32 dma_prefetch;
		dma_prefetch = readl(c->vaddr + I2O_DMA1_CFG);
		dma_prefetch |= 0x8000;
		writel(dma_prefetch, c->vaddr + I2O_DMA1_CFG);
	}

#ifdef HPSA_DEBUG
	printk("Trying to put board into Simple mode\n");
#endif				/* HPSA_DEBUG */
	c->max_commands = readl(&(c->cfgtable->CmdsOutMax));
	/* Update the field, and then ring the doorbell */
	writel(CFGTBL_Trans_Simple, &(c->cfgtable->HostWrite.TransportRequest));
	writel(CFGTBL_ChangeReq, c->vaddr + SA5_DOORBELL);

	/* under certain very rare conditions, this can take awhile.
	 * (e.g.: hot replace a failed 144GB drive in a RAID 5 set right
	 * as we enter this code.) */
	for (i = 0; i < MAX_CONFIG_WAIT; i++) {
		if (!(readl(c->vaddr + SA5_DOORBELL) & CFGTBL_ChangeReq))
			break;
		/* delay and try again */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(10);
	}

#ifdef HPSA_DEBUG
	printk(KERN_DEBUG "I counter got to %d %x\n", i,
	       readl(c->vaddr + SA5_DOORBELL));
#endif				/* HPSA_DEBUG */
#ifdef HPSA_DEBUG
	print_cfg_table(c->cfgtable);
#endif				/* HPSA_DEBUG */

	if (!(readl(&(c->cfgtable->TransportActive)) & CFGTBL_Trans_Simple)) {
		printk(KERN_WARNING "hpsa: unable to get board into"
		       " simple mode\n");
		err = -ENODEV;
		goto err_out_free_res;
	}
	return 0;

      err_out_free_res:
	/*
	 * Deliberately omit pci_disable_device(): it does something nasty to
	 * Smart Array controllers that pci_enable_device does not undo
	 */
	pci_release_regions(pdev);
	return err;
}


/* Function to find the first free pointer into our hba[] array */
/* Returns -1 if no free entries are left.  */
static int alloc_hpsa_hba(void)
{
	int i;

	for (i = 0; i < MAX_CTLR; i++) {
		if (!hba[i]) {
			ctlr_info_t *p;
			p = kzalloc(sizeof(ctlr_info_t), GFP_KERNEL);
			if (!p)
				goto Enomem;
			hba[i] = p;
			return i;
		}
	}
	printk(KERN_WARNING "hpsa: This driver supports a maximum"
	       " of %d controllers.\n", MAX_CTLR);
	goto out;
      Enomem:
	printk(KERN_ERR "hpsa: out of memory.\n");
      out:
	return -1;
}

static void free_hba(int i)
{
	ctlr_info_t *p = hba[i];

	hba[i] = NULL;
	kfree(p);
}

/*
 *  This is it.  Find all the controllers and register them.  I really hate
 *  stealing all these major device numbers.
 *  returns the number of block devices registered.
 */
static int __devinit hpsa_init_one(struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	int i;
	int dac;

	i = alloc_hpsa_hba();
	if (i < 0)
		return -1;

	hba[i]->busy_initializing = 1;

	if (hpsa_pci_init(hba[i], pdev) != 0)
		goto clean1;

	sprintf(hba[i]->devname, "hpsa%d", i);
	hba[i]->ctlr = i;
	hba[i]->pdev = pdev;

	/* configure PCI DMA stuff */
	if (!pci_set_dma_mask(pdev, DMA_64BIT_MASK))
		dac = 1;
	else if (!pci_set_dma_mask(pdev, DMA_32BIT_MASK))
		dac = 0;
	else {
		printk(KERN_ERR "hpsa: no suitable DMA available\n");
		goto clean1;
	}

	/*
	 * register with the major number, or get a dynamic major number
	 * by passing 0 as argument.  This is done for greater than
	 * 8 controller support.
	 */

	/* make sure the board interrupts are off */
	hba[i]->access.set_intr_mask(hba[i], HPSA_INTR_OFF);
	if (request_irq(hba[i]->intr[SIMPLE_MODE_INT], do_hpsa_intr,
			IRQF_DISABLED | IRQF_SHARED, hba[i]->devname, hba[i])) {
		printk(KERN_ERR "hpsa: Unable to get irq %d for %s\n",
		       hba[i]->intr[SIMPLE_MODE_INT], hba[i]->devname);
		goto clean2;
	}

#if defined(__VMKLNX__)
	/* 
	 * set pdev->irq, so that it contains the one we registered above.
	 * This hack is needed to fix coredump failure - see pr# 360662
	 */
	printk("%s: <0x%x> at PCI %s - pdev->irq: %d hba[i]->intr[SIMPLE_MODE_INT]: %d\n",
		hba[i]->devname, pdev->device, pci_name(pdev), pdev->irq, hba[i]->intr[SIMPLE_MODE_INT]);
	pdev->irq = hba[i]->intr[SIMPLE_MODE_INT];
#endif /* defined(__VMKLNX__) */

	printk(KERN_INFO "%s: <0x%x> at PCI %s IRQ %d%s using DAC\n",
	       hba[i]->devname, pdev->device, pci_name(pdev),
	       hba[i]->intr[SIMPLE_MODE_INT], dac ? "" : " not");

	hba[i]->cmd_pool_bits =
	    kmalloc(((hba[i]->nr_cmds + BITS_PER_LONG -
		      1) / BITS_PER_LONG) * sizeof(unsigned long), GFP_KERNEL);
	hba[i]->cmd_pool = (CommandList_struct *)
	    pci_alloc_consistent(hba[i]->pdev,
		    hba[i]->nr_cmds * sizeof(CommandList_struct),
		    &(hba[i]->cmd_pool_dhandle));
	hba[i]->errinfo_pool = (ErrorInfo_struct *)
	    pci_alloc_consistent(hba[i]->pdev,
		    hba[i]->nr_cmds * sizeof(ErrorInfo_struct),
		    &(hba[i]->errinfo_pool_dhandle));
	if ((hba[i]->cmd_pool_bits == NULL)
	    || (hba[i]->cmd_pool == NULL)
	    || (hba[i]->errinfo_pool == NULL)) {
		printk(KERN_ERR "hpsa: out of memory");
		goto clean4;
	}
	hba[i]->scsi_rejects.complete =
	    kmalloc(sizeof(hba[i]->scsi_rejects.complete[0]) *
		    (hba[i]->nr_cmds + 5), GFP_KERNEL);
	if (hba[i]->scsi_rejects.complete == NULL) {
		printk(KERN_ERR "hpsa: out of memory");
		goto clean4;
	}
	spin_lock_init(&hba[i]->lock);

	/* Initialize the pdev driver private data.
	   have it point to hba[i].  */
	pci_set_drvdata(pdev, hba[i]);
	/* command and error info recs zeroed out before
	   they are used */
	memset(hba[i]->cmd_pool_bits, 0,
	       ((hba[i]->nr_cmds + BITS_PER_LONG -
		 1) / BITS_PER_LONG) * sizeof(unsigned long));


	//Insert rescan thread
	hba[i]->rescan_thread = kthread_create(hpsa_do_rescan, ((ctlr_info_t *) hba[i]),
		"hpsa_rescan");
	if (IS_ERR(hba[i]->rescan_thread)) {
		printk(KERN_WARNING "hpsa: Unable to start hpsa%d rescan thread!\n", i);
		goto clean4;
	}
	//end rescan thread


#ifdef HPSA_DEBUG
	printk(KERN_DEBUG "Scanning for drives on controller hpsa%d\n", i);
#endif				/* HPSA_DEBUG */


	hpsa_scsi_setup(i);

	/* Turn the interrupts on so we can service requests */
	hba[i]->access.set_intr_mask(hba[i], HPSA_INTR_ON);

	hpsa_procinit(i);
	hpsa_register_scsi(i);	/* hook ourselves into SCSI subsystem */
	hba[i]->busy_initializing = 0;

	return 1;

      clean4:
	kfree(hba[i]->scsi_rejects.complete);
	kfree(hba[i]->cmd_pool_bits);
	if (hba[i]->cmd_pool)
		pci_free_consistent(hba[i]->pdev,
				    hba[i]->nr_cmds * sizeof(CommandList_struct),
				    hba[i]->cmd_pool, hba[i]->cmd_pool_dhandle);
	if (hba[i]->errinfo_pool)
		pci_free_consistent(hba[i]->pdev,
				    hba[i]->nr_cmds * sizeof(ErrorInfo_struct),
				    hba[i]->errinfo_pool,
				    hba[i]->errinfo_pool_dhandle);
	free_irq(hba[i]->intr[SIMPLE_MODE_INT], hba[i]);
      clean2:
      clean1:
	hba[i]->busy_initializing = 0;
	free_hba(i);
	return -1;
}

static int hpsa_do_rescan(void *data) 
{
	ctlr_info_t *ctlr;

	ctlr = (ctlr_info_t *)data;

	set_user_nice(current, -20);

	while (!kthread_should_stop()) {

		/* rescan thread sleeping */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		
		__set_current_state(TASK_RUNNING);

		/* rescan thread awake */
		printk(KERN_WARNING "hpsa%d: rescan thread updating scsi devices\n", ctlr->ctlr);
		hpsa_update_scsi_devices(ctlr->ctlr, ((struct hpsa_scsi_adapter_data_t *)ctlr->scsi_ctlr)->scsi_host->host_no);
	}
	
	printk(KERN_WARNING "hpsa%d: rescan thread stopped\n",
		((struct hpsa_scsi_adapter_data_t *)ctlr->scsi_ctlr)->scsi_host->host_no);

	return 0;
}

static void __devexit hpsa_remove_one(struct pci_dev *pdev)
{
	ctlr_info_t *tmp_ptr;
	int i; // , j;
	char flush_buf[4];
	int return_code;

	if (pci_get_drvdata(pdev) == NULL) {
		printk(KERN_ERR "hpsa: Unable to remove device \n");
		return;
	}
	tmp_ptr = pci_get_drvdata(pdev);
	i = tmp_ptr->ctlr;
	if (hba[i] == NULL) {
		printk(KERN_ERR "hpsa: device appears to "
		       "already be removed \n");
		return;
	}
	/* Turn board interrupts off  and send the flush cache command */
	/* sendcmd will turn off interrupt, and send the flush...
	 * To write all data in the battery backed cache to disks */
	memset(flush_buf, 0, 4);
	return_code = sendcmd(HPSA_CACHE_FLUSH, i, flush_buf, 4, 0, 0, 0, NULL,
			      TYPE_CMD);
	if (return_code != IO_OK) {
		printk(KERN_WARNING "Error Flushing cache on controller %d\n",
		       i);
	}
	free_irq(hba[i]->intr[2], hba[i]);
#ifdef CONFIG_PCI_MSI
	if (hba[i]->msix_vector)
		pci_disable_msix(hba[i]->pdev);
	else if (hba[i]->msi_vector)
		pci_disable_msi(hba[i]->pdev);
#endif				/* CONFIG_PCI_MSI */

        /* Kill the rescan thread for this host */
        if (hba[i]->rescan_thread) {
                struct task_struct *t = hba[i]->rescan_thread;
                kthread_stop(t);
        }

	iounmap(hba[i]->vaddr);
	hpsa_unregister_scsi(i);	/* unhook from SCSI subsystem */
	remove_proc_entry(hba[i]->devname, proc_hpsa);

	/* remove it from the disk list */

	pci_free_consistent(hba[i]->pdev, hba[i]->nr_cmds * sizeof(CommandList_struct),
			    hba[i]->cmd_pool, hba[i]->cmd_pool_dhandle);
	pci_free_consistent(hba[i]->pdev, hba[i]->nr_cmds * sizeof(ErrorInfo_struct),
			    hba[i]->errinfo_pool, hba[i]->errinfo_pool_dhandle);
	kfree(hba[i]->cmd_pool_bits);
	kfree(hba[i]->scsi_rejects.complete);
	/*
	 * Deliberately omit pci_disable_device(): it does something nasty to
	 * Smart Array controllers that pci_enable_device does not undo
	 */
	pci_release_regions(pdev);
	pci_set_drvdata(pdev, NULL);
	free_hba(i);
}

static struct pci_driver hpsa_pci_driver = {
	.name = "hpsa",
	.probe = hpsa_init_one,
	.remove = __devexit_p(hpsa_remove_one),
	.id_table = hpsa_pci_device_id,	/* id_table */
};

#if defined(__VMKLNX__)
static struct sas_function_template hpsa_transport_functions = {
        .get_linkerrors         = hpsa_get_linkerrors,
        .get_enclosure_identifier = hpsa_get_enclosure_identifier,
        .get_bay_identifier     =  hpsa_get_bay_identifier,
        .phy_reset              = hpsa_phy_reset,
        .get_initiator_sas_identifier= hpsa_get_initiator_sas_identifier,
        .get_target_sas_identifier= hpsa_get_target_sas_identifier,
};
#endif

/*
 *  This is it.  Register the PCI driver information for the cards we control
 *  the OS will call our registered routines when it finds one of our cards.
 */
static int __init hpsa_init(void)
{
	printk(KERN_INFO DRIVER_NAME "hpsa\n");
#if defined(__VMKLNX__)
	hpsa_transport_template = sas_attach_transport(&hpsa_transport_functions);
        if (!hpsa_transport_template) {
                printk("sas_attach_transport FAILED, hpsa_transport_template is NULL\n");
                return -ENODEV;
        }
#endif
	/* Register for our PCI devices */
	return pci_register_driver(&hpsa_pci_driver);
}

#if defined (__VMKLNX__)
static int __init init_hpsa_module(void)
{
        int status;

        int i;
        for (i = 0; i < MAX_CTLR; i++)
        {
                char_major[i] = -1;
        }
        printk(KERN_INFO DRIVER_NAME "VMKLNX\n");
        status = hpsa_init();
        return status;
}
#endif /*  defined(__VMKLNX__)  */

static void __exit hpsa_cleanup(void)
{
	int i;

	pci_unregister_driver(&hpsa_pci_driver);
	/* double check that all controller entrys have been removed */
	for (i = 0; i < MAX_CTLR; i++) {
		if (hba[i] != NULL) {
			printk(KERN_WARNING "hpsa: had to remove"
			       " controller %d\n", i);
			hpsa_remove_one(hba[i]->pdev);
		}
                if (char_major[i] != -1)
                        unregister_chrdev(char_major[i], hba[i]->devname);
	}
	remove_proc_entry("hpsa", proc_root_driver);
}

#if defined (__VMKLNX__)
module_init(init_hpsa_module);
#else /*  !defined(__VMKLNX__)  */
module_init(hpsa_init);
#endif /*  defined(__VMKLNX__)  */
module_exit(hpsa_cleanup);
