/* ****************************************************************
 * Portions Copyright 2003 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

/*
 * linux_pci.h --
 *
 *      Linux PCI compatibility.
 */

#ifndef _LINUX_PCI_H_
#define _LINUX_PCI_H_

#include "vmkapi.h"

#define PCI_DEVICE_BUS_ADDRESS      "0000:%02x:%02x.%01d" /* dom,bus,slot,func*/
#define PCI_DEVICE_VENDOR_SIGNATURE "%04x:%04x %04x:%04x" /* ven,dev,subV,subD*/

typedef struct LinuxPCIDev {
   struct pci_dev linuxDev;
   vmk_PCIDevice  vmkDev;
   vmk_uint32     *intrVectors;
   vmk_uint32     numIntrVectors;
   vmk_ModuleID   moduleID;
   vmk_uint32     flags;
} LinuxPCIDevExt;

/*
 * Usage of the bits in LinuxPCIDev "flags"
 */
#define PCIDEVEXT_FLAG_LEGACY_ISA	       0x01
#define PCIDEVEXT_FLAG_DEFAULT_INTR_VECTOR     0x02
#define PCIDEVEXT_FLAG_MSIX_INTR_VECTORS       0x04

#define LINUXPCI_INVALID_INTR_VECTOR           (-1)

static inline int 
LinuxPCIConfigSpaceRead(struct pci_dev *dev, int size, int where, void *buf)
{
   VMK_ReturnStatus status;
   vmk_PCIDevice vmkDev = container_of(dev, LinuxPCIDevExt, linuxDev)->vmkDev;

   switch (size) {
      case  8:
         status = vmk_PCIReadConfigSpace(vmkDev, VMK_ACCESS_8, (vmk_uint16) where, buf);
         break;
      case 16:
         status = vmk_PCIReadConfigSpace(vmkDev, VMK_ACCESS_16, (vmk_uint16) where, buf);
         break;
      case 32:
         status = vmk_PCIReadConfigSpace(vmkDev, VMK_ACCESS_32, (vmk_uint16) where, buf);
         break;
   }

   return status == VMK_OK ? 0 : -EINVAL;
}

static inline int 
LinuxPCIConfigSpaceWrite(struct pci_dev *dev, int size, int where, vmk_uint32 data)
{
   VMK_ReturnStatus status;
   vmk_PCIDevice vmkDev = container_of(dev, LinuxPCIDevExt, linuxDev)->vmkDev;

   switch (size) {
      case  8:
         status = vmk_PCIWriteConfigSpace(vmkDev, VMK_ACCESS_8, (vmk_uint16) where, data);
         break;
      case 16:
         status = vmk_PCIWriteConfigSpace(vmkDev, VMK_ACCESS_16, (vmk_uint16) where, data);
         break;
      case 32:
         status = vmk_PCIWriteConfigSpace(vmkDev, VMK_ACCESS_32, (vmk_uint16) where, data);
         break;
   }

   return status == VMK_OK ? 0 : -EINVAL;
}

extern void pci_read_bases(struct pci_dev *dev, unsigned int howmany, int rom);

extern void LinuxPCI_Init(void);
extern void LinuxPCI_Shutdown(void);
extern void LinuxPCI_Cleanup(void);
extern void LinuxDMA_Init(void);
extern void LinuxDMA_Cleanup(void);
extern vmk_Bool LinuxPCI_DeviceIsPAECapable(struct pci_dev *dev);
extern void LinuxPCI_DeviceClaimed(vmk_ModuleID moduleID,
                                   LinuxPCIDevExt *pciDevExt);
int LinuxPCI_FindCapability(uint16_t bus, uint16_t devfn, uint8_t cap);

struct pci_dev *LinuxPCI_FindDevByPortBase(unsigned long base, unsigned long io_port);
struct pci_dev *LinuxPCI_FindDevByBusSlot(uint16_t bus, uint16_t devfn);
vmk_Bool LinuxPCI_IsValidPCIBusDev(struct pci_dev *pdev);

#endif
