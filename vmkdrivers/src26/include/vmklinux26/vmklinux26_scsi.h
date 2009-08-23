/* ****************************************************************
 * Portions Copyright 1998 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/
#ifndef _VMKLNX26_SCSI_H
#define _VMKLNX26_SCSI_H

#include <scsi/scsi_host.h>

/*
 * vmklinux specific per instance adapter information
 */
struct vmklnx_ScsiAdapter {
   struct list_head entry;
   struct Scsi_Host *shost;
   vmk_ScsiAdapter  *vmkAdapter;
   struct vmklnx_ScsiModule *vmkModule;
   atomic_t tmfFlag;
   unsigned vmkSgArray; /* Does the driver understand vmk_SgArray types? */
   struct mutex     ioctl_mutex;
   struct work_struct adapter_destroy_work;
};

/*
 * SCSI transports that vmklinux supports today
 */
#define	VMKLNX_SCSI_TRANSPORT_TYPE_UNKNOWN	0x0000000000000000UL
#define	VMKLNX_SCSI_TRANSPORT_TYPE_PSCSI	0x0000000000000001UL
#define	VMKLNX_SCSI_TRANSPORT_TYPE_SAS		0x0000000000000002UL
#define	VMKLNX_SCSI_TRANSPORT_TYPE_FC		0x0000000000000004UL
#define	VMKLNX_SCSI_TRANSPORT_TYPE_ISCSI	0x0000000000000008UL
#define	VMKLNX_SCSI_TRANSPORT_TYPE_USB		0x0000000000000010UL
#define	VMKLNX_SCSI_TRANSPORT_TYPE_SATA		0x0000000000000020UL
#define	VMKLNX_SCSI_TRANSPORT_TYPE_IDE		0x0000000000000040UL
/*
 * VMKLNK_SCSI_TRANSPORT_TYPE_XSAN generically represents any type of SAN transport not
 * explicitly encoded elsewhere in this list; it is intended mainly for
 * Community Source partners whose VM * Kernel drivers support novel SAN
 * transports that don't (yet) have VMware product-level support; these mostly
 * masquerade as locally-attached discs, except that:
 *
 * 1. They respond to SCSI "Inquiry" commands, yielding unique target IDs.
 * 2. RDMs are allowed.
 * 3. Periodic rescanning for new LUNs is performed.
 */
#define	VMKLNX_SCSI_TRANSPORT_TYPE_XSAN		0x0000000000000080UL

typedef	uint64_t	vmklnx_ScsiTransportType;	// For the above

enum VMKLinux_Flags {
   /* 
    * Use LUN reset rather than bus reset, if possible 
    */
   VMK_FLAGS_USE_LUNRESET       = 0x00000001,
   /* 
    * Indicate that a cmd has been dropped for testing purposes. 
    */
   VMK_FLAGS_DROP_CMD           = 0x00000002,
   /* 
    * Use target reset rather than bus reset, if possible 
    */
   VMK_FLAGS_USE_TARGETRESET    = 0x00000004,
   /* 
    * Indicate that the cmd still needs to execute SCSILinuxCmdDone 
    */
   VMK_FLAGS_NEED_CMDDONE       = 0x00000008,
   /* 
    * Indicate to SCSILinuxCmdDone that it's actions will be performed later 
    */
   VMK_FLAGS_DELAY_CMDDONE      = 0x00000010,
   /*
    * Indicate that SCSILinuxCmdDone has been attempted for this cmd.
    * if VMK_FLAGS_NEED_CMDDONE is still set then SCSILinuxCmdDone needs to be
    * called a second time ... used in combination with DELAY_CMDDONE
    */
   VMK_FLAGS_CMDDONE_ATTEMPTED  = 0x00000020,
   /*
    * This flag is used on scsi_cmnd requests that are associated with TMF 
    * like bus reset where no scmd is involved
    */
   VMK_FLAGS_TMF_REQUEST        = 0x00000040,
   /*
    * The command is used to identify the dump requests
    */
   VMK_FLAGS_DUMP_REQUEST       = 0x00000080,
   /*
    * The command is used to identify the requests with IO timeout
    */
   VMK_FLAGS_IO_TIMEOUT         = 0x00000100,
   /*
    * This flag identifies internal commands
    */
   VMK_FLAGS_INTERNAL_COMMAND   = 0x00000200
};

static __inline__ void
VMKLinux_Flags_Test(enum VMKLinux_Flags flags) {
   switch (flags) {
   case VMK_FLAGS_USE_LUNRESET:
   case VMK_FLAGS_DROP_CMD:
   case VMK_FLAGS_USE_TARGETRESET:
   case VMK_FLAGS_NEED_CMDDONE:
   case VMK_FLAGS_DELAY_CMDDONE:
   case VMK_FLAGS_CMDDONE_ATTEMPTED:
   case VMK_FLAGS_TMF_REQUEST:
   case VMK_FLAGS_DUMP_REQUEST:
   case VMK_FLAGS_IO_TIMEOUT:
   case VMK_FLAGS_INTERNAL_COMMAND:
      return;
   }
}

/* generic san object identifier */
struct xsan_id {
   u64 L;  /* lower part of identifire */
   u64 H;  /* higher part of identifire */
};

/* The functions by which the generic san transport and the driver communicate */
struct xsan_function_template {
   int (*setup_transport_attributes)(struct Scsi_Host *, struct scsi_target *);
   int (*setup_host_attributes)(struct Scsi_Host *);
   int (*get_initiator_xsan_identifier)(struct Scsi_Host *, struct xsan_id *);
   int (*get_target_xsan_identifier)(struct scsi_target *, struct xsan_id *);
};

char * vmklnx_get_vmhba_name(struct Scsi_Host *);
struct scsi_transport_template *
vmklnx_generic_san_attach_transport(struct xsan_function_template *t,
                                    size_t target_size, size_t host_size);
void vmklnx_generic_san_release_transport(struct scsi_transport_template *t);
void vmklnx_scsi_set_path_maxsectors(struct scsi_device *sdev,
                                     unsigned int max_sectors);

#endif /* _VMKLNX26_SCSI_H */
