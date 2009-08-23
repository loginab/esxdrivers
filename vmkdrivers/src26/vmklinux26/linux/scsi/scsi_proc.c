/*
 * linux/drivers/scsi/scsi_proc.c
 *
 * The functions in this file provide an interface between
 * the PROC file system and the SCSI device drivers
 * It is mainly used for debugging, statistics and to pass 
 * information directly to the lowlevel driver.
 *
 * (c) 1995 Michael Neuffer neuffer@goofy.zdv.uni-mainz.de 
 * Version: 0.99.8   last change: 95/09/13
 * 
 * generic command parser provided by: 
 * Andreas Heilwagen <crashcar@informatik.uni-koblenz.de>
 *
 * generic_proc_info() support of xxxx_info() by:
 * Michael A. Griffith <grif@acm.org>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/blkdev.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>

#if !defined(__VMKLNX__)
#include "scsi_priv.h"
#include "scsi_logging.h"
#else
#include "vmklinux26/vmklinux26_scsi.h"
#include <scsi/scsi_cmnd.h>
#include "linux_scsi.h"
#endif


/* 4K page size, but our output routines, use some slack for overruns */
#define PROC_BLOCK_SIZE (3*1024)

#if !defined(__VMKLNX__)
static struct proc_dir_entry *proc_scsi;
#else
/*
 * proc_scsi is initialized in linux_proc.c
 */
extern struct proc_dir_entry *proc_scsi;
#endif

/* Protect sht->present and sht->proc_dir */
static DEFINE_MUTEX(global_host_template_mutex);

static int proc_scsi_read(char *buffer, char **start, off_t offset,
			  int length, int *eof, void *data)
{
	struct Scsi_Host *shost = data;
	int n;

#if defined(__VMKLNX__)
        VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(shost), n, 
                           shost->hostt->proc_info, shost, buffer, start,
                           offset, length, 0);       
#else
        n = shost->hostt->proc_info(shost, buffer, start, offset, length, 0);
#endif

#if defined(__VMKLNX__)
        if (n < length) {
           /*
            * If the buffer doesn't have any space for the vmkadapter
            * name, make space for it instead of overflowing. We are
            * interested in only the first few bytes(which return the
            * address from the driver proc_info handler) and the
            * vmkadapter name we print out. So I think it is fine
            * to overwrite the rest of the proc info dump.
            */

           /*
            * 50 Characters are reserved for vmhba names
 	    */
           if (length > 50 && n > length - 50) {
              printk(KERN_INFO "Handler returned large buffer: %d\n", n);
              n = length - 50;
           }

           n += snprintf(buffer + n, length - n, "\nvmkadapter: %s\n",
                         shost->adapter? vmklnx_get_vmhba_name(shost) : "None");

        }
#endif
	*eof = (n < length);

	return n;
}

static int proc_scsi_write_proc(struct file *file, const char __user *buf,
                           unsigned long count, void *data)
{
	struct Scsi_Host *shost = data;
	ssize_t ret = -ENOMEM;
	char *page;
	char *start;
    
	if (count > PROC_BLOCK_SIZE)
		return -EOVERFLOW;

	page = (char *)__get_free_page(GFP_KERNEL);
	if (page) {
		ret = -EFAULT;
		if (copy_from_user(page, buf, count))
			goto out;
#if defined(__VMKLNX__)
                VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(shost), ret, 
                                   shost->hostt->proc_info, shost, page, 
                                   &start, 0, count, 1);
#else
                ret = shost->hostt->proc_info(shost, page, &start, 0, count, 1);
#endif
	}
out:
	free_page((unsigned long)page);
	return ret;
}

void scsi_proc_hostdir_add(struct scsi_host_template *sht)
{
	if (!sht->proc_info)
		return;

	mutex_lock(&global_host_template_mutex);
	if (!sht->present++) {
		sht->proc_dir = proc_mkdir(sht->proc_name, proc_scsi);
        	if (!sht->proc_dir) 
			printk(KERN_ERR "%s: proc_mkdir failed for %s\n",
			       __FUNCTION__, sht->proc_name);
		else 
			sht->proc_dir->owner = sht->module;
	}
	mutex_unlock(&global_host_template_mutex);
}

void scsi_proc_hostdir_rm(struct scsi_host_template *sht)
{
	if (!sht->proc_info)
		return;

	mutex_lock(&global_host_template_mutex);
	if (!--sht->present && sht->proc_dir) {
		remove_proc_entry(sht->proc_name, proc_scsi);
		sht->proc_dir = NULL;
	}
	mutex_unlock(&global_host_template_mutex);
}

void scsi_proc_host_add(struct Scsi_Host *shost)
{
	struct scsi_host_template *sht = shost->hostt;
	struct proc_dir_entry *p;
	char name[10];

	if (!sht->proc_dir)
		return;

	sprintf(name,"%d", shost->host_no);
	p = create_proc_read_entry(name, S_IFREG | S_IRUGO | S_IWUSR,
			sht->proc_dir, proc_scsi_read, shost);
	if (!p) {
		printk(KERN_ERR "%s: Failed to register host %d in"
		       "%s\n", __FUNCTION__, shost->host_no,
		       sht->proc_name);
		return;
	} 

	p->write_proc = proc_scsi_write_proc;
	p->owner = sht->module;
}

void scsi_proc_host_rm(struct Scsi_Host *shost)
{
	char name[10];

	if (!shost->hostt->proc_dir)
		return;

	sprintf(name,"%d", shost->host_no);
	remove_proc_entry(name, shost->hostt->proc_dir);
}

#if !defined(__VMKLNX__)
static int proc_scsi_show(struct seq_file *s, void *data)
{
	struct klist_iter *iter = data;
	struct klist_node *node = iter->i_cur;
	struct device *dev = container_of(node, struct device, knode_bus);
	struct scsi_device *sdev = to_scsi_device(dev);
	int i;

	seq_printf(s,
		"Host: scsi%d Channel: %02d Id: %02d Lun: %02d\n  Vendor: ",
		sdev->host->host_no, sdev->channel, sdev->id, sdev->lun);
	for (i = 0; i < 8; i++) {
		if (sdev->vendor[i] >= 0x20)
			seq_printf(s, "%c", sdev->vendor[i]);
		else
			seq_printf(s, " ");
	}

	seq_printf(s, " Model: ");
	for (i = 0; i < 16; i++) {
		if (sdev->model[i] >= 0x20)
			seq_printf(s, "%c", sdev->model[i]);
		else
			seq_printf(s, " ");
	}

	seq_printf(s, " Rev: ");
	for (i = 0; i < 4; i++) {
		if (sdev->rev[i] >= 0x20)
			seq_printf(s, "%c", sdev->rev[i]);
		else
			seq_printf(s, " ");
	}

	seq_printf(s, "\n");

	seq_printf(s, "  Type:   %s ",
		     sdev->type < MAX_SCSI_DEVICE_CODE ?
	       scsi_device_types[(int) sdev->type] : "Unknown          ");
	seq_printf(s, "               ANSI"
		     " SCSI revision: %02x", (sdev->scsi_level - 1) ?
		     sdev->scsi_level - 1 : 1);
	if (sdev->scsi_level == 2)
		seq_printf(s, " CCS\n");
	else
		seq_printf(s, "\n");

	return 0;
}

static int scsi_add_single_device(uint host, uint channel, uint id, uint lun)
{
	struct Scsi_Host *shost;
	int error = -ENXIO;

	shost = scsi_host_lookup(host);
	if (IS_ERR(shost))
		return PTR_ERR(shost);

	if (shost->transportt->user_scan) {
#if defined(__VMKLNX__)
                VMKAPI_MODULE_CALL(SCSI_GET_MODULE_ID(shost), error, 
                                   shost->transportt->user_scan, shost, 
                                   channel, id, lun);
#else
                error = shost->transportt->user_scan(shost, channel, id, lun);
#endif
	} else {
		error = scsi_scan_host_selected(shost, channel, id, lun, 1);
        }
	scsi_host_put(shost);
	return error;
}

static int scsi_remove_single_device(uint host, uint channel, uint id, uint lun)
{
	struct scsi_device *sdev;
	struct Scsi_Host *shost;
	int error = -ENXIO;

	shost = scsi_host_lookup(host);
	if (IS_ERR(shost))
		return PTR_ERR(shost);
	sdev = scsi_device_lookup(shost, channel, id, lun);
	if (sdev) {
		scsi_remove_device(sdev);
		scsi_device_put(sdev);
		error = 0;
	}

	scsi_host_put(shost);
	return error;
}

static ssize_t proc_scsi_write(struct file *file, const char __user *buf,
			       size_t length, loff_t *ppos)
{
	int host, channel, id, lun;
	char *buffer, *p;
	int err;

	if (!buf || length > PAGE_SIZE)
		return -EINVAL;

	buffer = (char *)__get_free_page(GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	err = -EFAULT;
	if (copy_from_user(buffer, buf, length))
		goto out;

	err = -EINVAL;
	if (length < PAGE_SIZE)
		buffer[length] = '\0';
	else if (buffer[PAGE_SIZE-1])
		goto out;

	/*
	 * Usage: echo "scsi add-single-device 0 1 2 3" >/proc/scsi/scsi
	 * with  "0 1 2 3" replaced by your "Host Channel Id Lun".
	 */
	if (!strncmp("scsi add-single-device", buffer, 22)) {
		p = buffer + 23;

		host = simple_strtoul(p, &p, 0);
		channel = simple_strtoul(p + 1, &p, 0);
		id = simple_strtoul(p + 1, &p, 0);
		lun = simple_strtoul(p + 1, &p, 0);

		err = scsi_add_single_device(host, channel, id, lun);

	/*
	 * Usage: echo "scsi remove-single-device 0 1 2 3" >/proc/scsi/scsi
	 * with  "0 1 2 3" replaced by your "Host Channel Id Lun".
	 */
	} else if (!strncmp("scsi remove-single-device", buffer, 25)) {
		p = buffer + 26;

		host = simple_strtoul(p, &p, 0);
		channel = simple_strtoul(p + 1, &p, 0);
		id = simple_strtoul(p + 1, &p, 0);
		lun = simple_strtoul(p + 1, &p, 0);

		err = scsi_remove_single_device(host, channel, id, lun);
	}

	/*
	 * convert success returns so that we return the 
	 * number of bytes consumed.
	 */
	if (!err)
		err = length;

 out:
	free_page((unsigned long)buffer);
	return err;
}

static void *proc_scsi_start(struct seq_file *s, loff_t *pos)
{
	struct klist_iter *iter;
	loff_t n;

	iter = kmalloc(sizeof(*iter), GFP_KERNEL);
	if (iter == NULL)
		return ERR_PTR(-ENOMEM);
	klist_iter_init(&scsi_bus_type.klist_devices, iter);

	if (*pos == 0)
		seq_puts(s, "Attached devices:\n");

	n = *pos;
	do {
		struct klist_node *node = klist_next(iter);
		if (node == NULL) {
			klist_iter_exit(iter);
			kfree(iter);
			return NULL;
		}
	} while (n-- != 0);

	return iter;
}

static void *proc_scsi_next(struct seq_file *s, void *p, loff_t *pos)
{
	struct klist_iter *iter = p;
	struct klist_node *node = klist_next(iter);

	if (node == NULL) {
		klist_iter_exit(iter);
		kfree(iter);
		return NULL;
	}

	*pos += 1;
	return iter;
}

static void proc_scsi_stop(struct seq_file *s, void *p)
{
	struct klist_iter *iter = p;

	if (iter != NULL && !IS_ERR_VALUE((unsigned long)iter)) {
		klist_iter_exit(iter);
		kfree(iter);
	}
}


static struct seq_operations proc_scsi_op = {
	.start  = proc_scsi_start,
	.next   = proc_scsi_next,
	.stop   = proc_scsi_stop,
	.show   = proc_scsi_show,
};

static int proc_scsi_open(struct inode *inode, struct file *file)
{
	/*
	 * We don't really needs this for the write case but it doesn't
	 * harm either.
	 */
	return seq_open(file, &proc_scsi_op);
}

static struct file_operations proc_scsi_operations = {
	.open		= proc_scsi_open,
	.read		= seq_read,
	.write		= proc_scsi_write,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

int __init scsi_init_procfs(void)
{
	struct proc_dir_entry *pde;

	proc_scsi = proc_mkdir("scsi", NULL);
	if (!proc_scsi)
		goto err1;

	pde = create_proc_entry("scsi/scsi", 0, NULL);
	if (!pde)
		goto err2;
	pde->proc_fops = &proc_scsi_operations;

	return 0;

err2:
	remove_proc_entry("scsi", NULL);
err1:
	return -ENOMEM;
}

void scsi_exit_procfs(void)
{
	remove_proc_entry("scsi/scsi", NULL);
	remove_proc_entry("scsi", NULL);
}
#endif
