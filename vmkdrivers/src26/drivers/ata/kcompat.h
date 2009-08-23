#ifndef _KCOMPAT_H_
#define _KCOMPAT_H_

#include <linux/version.h>

#include "vmklinux26/vmklinux26_scsi.h"

extern int ata_scsi_abort(struct scsi_cmnd *cmd);
extern int ata_scsi_device_reset(struct scsi_cmnd *cmd);
extern int ata_scsi_bus_reset(struct scsi_cmnd *cmd);
extern int ata_scsi_host_reset(struct scsi_cmnd *cmd);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19))
typedef irqreturn_t (*old_irq_handler_t)(int, void*, struct pt_regs *);
typedef irqreturn_t (*new_irq_handler_t)(int, void*);

static inline int __request_irq(unsigned int irq,
				new_irq_handler_t handler,
				unsigned long flags,
				const char *devname,
				void *dev_id)
{
	old_irq_handler_t old_handler = (old_irq_handler_t) handler;
	return request_irq(irq, old_handler, flags, devname, dev_id);
}

#define request_irq(irq, handler, flags, devname, dev_id) \
__request_irq((irq), (handler), (flags), (devname), (dev_id))
#endif

#endif
