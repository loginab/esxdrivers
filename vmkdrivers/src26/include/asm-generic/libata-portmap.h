#ifndef __ASM_GENERIC_LIBATA_PORTMAP_H
#define __ASM_GENERIC_LIBATA_PORTMAP_H

#define ATA_PRIMARY_CMD		0x1F0
#define ATA_PRIMARY_CTL		0x3F6
#if defined(__VMKLNX__)
#define ATA_PRIMARY_IRQ		vmklnx_convert_isa_irq(14)
#else
#define ATA_PRIMARY_IRQ		14
#endif /* defined(__VMKLNX__) */

#define ATA_SECONDARY_CMD	0x170
#define ATA_SECONDARY_CTL	0x376
#if defined(__VMKLNX__)
#define ATA_SECONDARY_IRQ	vmklnx_convert_isa_irq(15)
#else
#define ATA_SECONDARY_IRQ	15
#endif /* defined(__VMKLNX__) */

#endif
