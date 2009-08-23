/* drivers/message/fusion/linux_compat.h */
#ifndef FUSION_LINUX_COMPAT_H
#define FUSION_LINUX_COMPAT_H

#include <linux/version.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi.h>

#ifndef PCI_VENDOR_ID_ATTO
#define PCI_VENDOR_ID_ATTO	0x117c
#endif

#ifndef PCI_VENDOR_ID_BROCADE
#define PCI_VENDOR_ID_BROCADE	0x1657
#endif

#if !defined(__VMKLNX__)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
static inline void *shost_priv(struct Scsi_Host *shost)
{
        return (void *)shost->hostdata;
}
#endif
#endif /* !defined(__VMKLNX__) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,6))
static int inline scsi_device_online(struct scsi_device *sdev)
{
	return sdev->online;
}
#endif

#ifndef spi_dv_pending
#define spi_dv_pending(x) (((struct spi_transport_attrs *)&(x)->starget_data)->dv_pending)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
/**
 * mpt_scsilun_to_int: convert a scsi_lun to an int
 * @scsilun:    struct scsi_lun to be converted.
 *
 * Description:
 *     Convert @scsilun from a struct scsi_lun to a four byte host byte-ordered
 *     integer, and return the result. The caller must check for
 *     truncation before using this function.
 *
 * Notes:
 *     The struct scsi_lun is assumed to be four levels, with each level
 *     effectively containing a SCSI byte-ordered (big endian) short; the
 *     addressing bits of each level are ignored (the highest two bits).
 *     For a description of the LUN format, post SCSI-3 see the SCSI
 *     Architecture Model, for SCSI-3 see the SCSI Controller Commands.
 *
 *     Given a struct scsi_lun of: 0a 04 0b 03 00 00 00 00, this function returns
 *     the integer: 0x0b030a04
 **/
static inline int mpt_scsilun_to_int(struct scsi_lun *scsilun)
{
        int i;
        unsigned int lun;

        lun = 0;
        for (i = 0; i < sizeof(lun); i += 2)
                lun = lun | (((scsilun->scsi_lun[i] << 8) |
                              scsilun->scsi_lun[i + 1]) << (i * 8));
        return lun;
}
#endif
/*}-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#endif /* _LINUX_COMPAT_H */
