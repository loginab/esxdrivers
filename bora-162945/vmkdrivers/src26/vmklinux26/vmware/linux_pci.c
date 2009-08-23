/* ****************************************************************
 * Portions Copyright 2003 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

#include <linux/pci.h>
#include <linux/proc_fs.h>

#include "vmkapi.h"
#include "vmklinux26_dist.h"
#include "linux_pci.h"
#include "linux_stubs.h"
#include "../linux/pci/pci.h"

#define VMKLNX_LOG_HANDLE LinPCI
#include "vmklinux26_log.h"

struct bus_type pci_bus_type = {
   .name = "pci",
};

static struct pci_bus linuxPCIBuses[VMK_PCI_NUM_BUSES];
extern void pci_announce_device_to_drivers(struct pci_dev *dev);

static inline void
LinuxPCIMSIIntrVectorSet(LinuxPCIDevExt *pciDevExt, vmk_uint32 vector)
{
   pciDevExt->linuxDev.irq = vector;
   pciDevExt->flags &= ~PCIDEVEXT_FLAG_DEFAULT_INTR_VECTOR;
}

static inline void
LinuxPCILegacyIntrVectorSet(LinuxPCIDevExt *pciDevExt)
{
   vmk_uint32 vector;
   VMK_ReturnStatus status;

   status = vmk_PCIAllocateIntVectors(pciDevExt->vmkDev, 
                                      VMK_PCI_INTERRUPT_TYPE_LEGACY, 
                                      1, 
                                      NULL,
                                      VMK_FALSE,
                                      &vector,
                                      NULL);
   if (status != VMK_OK) {
      VMKLNX_WARN("Could not allocate legacy PCI interrupt for device %s",
                  pciDevExt->linuxDev.dev.bus_id);
      return;
   }

   pciDevExt->linuxDev.irq = vector;
   pciDevExt->flags |= PCIDEVEXT_FLAG_DEFAULT_INTR_VECTOR;
}

static inline int
LinuxPCIIntrVectorIsLegacy(LinuxPCIDevExt *pciDevExt)
{
   return pciDevExt->flags & PCIDEVEXT_FLAG_DEFAULT_INTR_VECTOR;
}

static inline int
LinuxPCIIntrVectorIsNotLegacy(LinuxPCIDevExt *pciDevExt)
{
   return (pciDevExt->flags & PCIDEVEXT_FLAG_DEFAULT_INTR_VECTOR) == 0
          && pciDevExt->linuxDev.irq 
          && pciDevExt->linuxDev.irq != LINUXPCI_INVALID_INTR_VECTOR;
}

static inline int
LinuxPCIDeviceIsLegacyISA(LinuxPCIDevExt *pciDevExt)
{
   return pciDevExt->flags & PCIDEVEXT_FLAG_LEGACY_ISA;
}

inline static void
LinuxPCIIntrVectorFree(LinuxPCIDevExt *pciDevExt)
{
   if (LinuxPCIIntrVectorIsNotLegacy(pciDevExt)) {
      vmk_uint32 vector;
      /*
       * Remove the vector. 
       */
      vector = pciDevExt->linuxDev.irq;
      vmk_VectorDisable(vector);
      vmk_PCIFreeIntVectors(pciDevExt->vmkDev, 1, &vector);
   }
   pciDevExt->linuxDev.irq = LINUXPCI_INVALID_INTR_VECTOR;
   pciDevExt->flags &= ~PCIDEVEXT_FLAG_DEFAULT_INTR_VECTOR;
}

static struct pci_dev *
LinuxPCIFindDeviceByHandle(vmk_PCIDevice vmkDev)
{
   // search pci_devices and pci_hidden_devices lists
   struct pci_dev *linuxDev;

   down_read(&pci_bus_sem);
   linuxDev = NULL;
   list_for_each_entry(linuxDev, &pci_devices, global_list) {
      if (container_of(linuxDev, LinuxPCIDevExt, linuxDev)->vmkDev == vmkDev) {
         up_read(&pci_bus_sem);
         return linuxDev;
      }
   }
   linuxDev = NULL;
   list_for_each_entry(linuxDev, &pci_hidden_devices, global_list) {
      if (container_of(linuxDev, LinuxPCIDevExt, linuxDev)->vmkDev == vmkDev) {
         up_read(&pci_bus_sem);
         return linuxDev;
      }
   }
   up_read(&pci_bus_sem);

   return NULL;
}

inline static vmk_Bool
LinuxPCIDeviceIsClaimed(LinuxPCIDevExt *pciDevExt)
{
   return pciDevExt->moduleID != VMK_INVALID_MODULE_ID ? VMK_TRUE : VMK_FALSE;
}

static void
LinuxPCIDeviceUnclaimed(LinuxPCIDevExt *pciDevExt)
{  
   pciDevExt->moduleID = VMK_INVALID_MODULE_ID;

   VMKLNX_INFO("Device %x:%x unclaim.\n", pciDevExt->linuxDev.bus->number,
      pciDevExt->linuxDev.devfn);
}

/*
 * This function will be called whenever a device is newly visible for
 * vmklinux. It is modeled after pci_insert_device, the function which would
 * be called in a linux system.
 */
static void 
LinuxPCIDeviceInserted(vmk_PCIDevice vmkDev, vmk_PCIDeviceCallbackReason cbReason)
{
   LinuxPCIDevExt *pciDevExt;
   struct pci_dev *linuxDev;
   vmk_PCIDeviceInfo deviceInfo;
   uint8_t bus, slot, func;
   VMK_ReturnStatus status;
   vmk_Bool hide = VMK_FALSE;
   vmk_Bool bridge;
   vmk_Bool newDevice = VMK_TRUE;
 
   status = vmk_PCIGetInfo(vmkDev, &deviceInfo);
   VMK_ASSERT(status == VMK_OK);

   status = vmk_PCIGetBusDevFunc(vmkDev, &bus, &slot, &func);
   VMK_ASSERT(status == VMK_OK);

   if (deviceInfo.owner != VMK_PCI_DEVICE_OWNER_MODULE) {
      hide = VMK_TRUE;
   }

   bridge = ((deviceInfo.hdrType & 0x7f) == PCI_HEADER_TYPE_BRIDGE);

   linuxDev = LinuxPCIFindDeviceByHandle(vmkDev);

   if (unlikely(linuxDev != NULL)) {
      if (cbReason == VMK_PCI_DEVICE_INSERTED) {
         VMKLNX_WARN("Already received device " PCI_DEVICE_BUS_ADDRESS 
                     " at event device-inserted.", 
                     bus, slot, func);
      } else {
         VMKLNX_INFO("Already received device " PCI_DEVICE_BUS_ADDRESS 
                     " at event ownership-changed.", 
                     bus, slot, func );
      }

      VMK_ASSERT(cbReason != VMK_PCI_DEVICE_INSERTED);
      newDevice = VMK_FALSE;
      pciDevExt = container_of(linuxDev, LinuxPCIDevExt, linuxDev);
      VMK_ASSERT(pciDevExt->vmkDev == vmkDev);
      goto existing_device;
   }

   pciDevExt = vmk_HeapAlloc(VMK_MODULE_HEAP_ID, sizeof(LinuxPCIDevExt));
   if (unlikely(pciDevExt == NULL)) {
      VMKLNX_ALERT("Out of memory");
      return;
   }

   memset(pciDevExt, 0, sizeof(LinuxPCIDevExt));

   linuxDev = &pciDevExt->linuxDev;
   linuxDev->dev.dev_type = PCI_DEVICE_TYPE;
   linuxDev->bus = &linuxPCIBuses[bus];
   linuxDev->devfn = PCI_DEVFN(slot, func);
   linuxDev->vendor = deviceInfo.vendorID;
   linuxDev->device = deviceInfo.deviceID;
   linuxDev->class = (deviceInfo.classCode << 8) 
                      | (deviceInfo.progIFRevID >> 8);
   linuxDev->revision = deviceInfo.progIFRevID;
   /*
    * Linux expects only the header type(not the multi-functionness)
    */
   linuxDev->hdr_type = deviceInfo.hdrType & 0x7f; 
   linuxDev->subsystem_vendor = deviceInfo.subVendorID;
   linuxDev->subsystem_device = deviceInfo.subDeviceID;
   /*
    * vmkernel uses vector for irq
    */
   linuxDev->dev.dma_mask = &linuxDev->dma_mask;
   linuxDev->dma_mask = DMA_32BIT_MASK;
   device_initialize(&linuxDev->dev);

   snprintf(linuxDev->dev.bus_id, sizeof(linuxDev->dev.bus_id),
            PCI_DEVICE_BUS_ADDRESS, bus, slot, func);

   pciDevExt->vmkDev = vmkDev;

   /*
    * The device is not claimed yet/may not claimed at all within vmklinux. 
    */ 
   pciDevExt->moduleID = VMK_INVALID_MODULE_ID;

   if (bridge) {
      pci_read_bases(linuxDev, 2, PCI_ROM_ADDRESS1); 
   } else {
      pci_read_bases(linuxDev, 6, PCI_ROM_ADDRESS); 
   }

existing_device:

   down_write(&pci_bus_sem);
   if (newDevice) {
      list_add_tail(&linuxDev->global_list, hide ? &pci_hidden_devices :
                                                   &pci_devices);
      if (is_vmvisor) {
         pci_proc_attach_device(linuxDev);
      }
   } else {
      list_del(&linuxDev->global_list);
      list_add_tail(&linuxDev->global_list, hide ? &pci_hidden_devices :
                                                   &pci_devices);
   }
   up_write(&pci_bus_sem);

   if (hide) {
      VMKLNX_INFO("Received hidden %s device " PCI_DEVICE_BUS_ADDRESS
                  " at event %s.", bridge ? "bridge" : "",
                  bus, slot, func,
                  cbReason == VMK_PCI_DEVICE_INSERTED ? "device-inserted"
                                                      : "ownership-changed");
   } else { 

      LinuxPCILegacyIntrVectorSet(pciDevExt);

      VMKLNX_INFO("Received %s device " PCI_DEVICE_BUS_ADDRESS " using vector %d"
                  " at event %s.", bridge ? "bridge" : "",
                  bus, slot, func, (unsigned int)linuxDev->irq,
                  cbReason == VMK_PCI_DEVICE_INSERTED ? "device-inserted"
                                                      : "ownership-changed");
      pci_announce_device_to_drivers(linuxDev);
   }
}

/*
 * This function will be called whenever a device is newly invisible for
 * vmklinux. It is modelled after pci_remove_device, the function which would
 * be called in a linux system.
 */
static void 
LinuxPCIDeviceRemoved(vmk_PCIDevice vmkDev, vmk_PCIDeviceCallbackReason cbReason)
{
   LinuxPCIDevExt *pciDevExt;
   struct pci_dev *linuxDev;
   char vmkDevName[VMK_PCI_DEVICE_NAME_LENGTH];

   linuxDev = LinuxPCIFindDeviceByHandle(vmkDev);

   if (unlikely(linuxDev == NULL)) {
      uint8_t bus = 0, slot = 0, func = 0;

      if (vmk_PCIGetBusDevFunc(vmkDev, &bus, &slot, &func) == VMK_OK) {
         VMKLNX_WARN("Device " PCI_DEVICE_BUS_ADDRESS " not found at event %s.", 
                     bus, slot, func, 
                     cbReason == VMK_PCI_DEVICE_REMOVED ? "device-removed" : "ownership-changed");
      } else {
         VMKLNX_WARN("Device not found at event %s.", 
                     cbReason == VMK_PCI_DEVICE_REMOVED ? "device-removed" : "ownership-changed");
      }
      return;
   }

   if (unlikely(
         vmk_PCIGetDeviceName(vmkDev, vmkDevName, sizeof(vmkDevName)-1) != VMK_OK)) {
      vmkDevName[0] = 0;
   }
   VMKLNX_INFO("Remove %s %s\n", linuxDev->dev.bus_id, vmkDevName);

   pciDevExt = container_of(linuxDev, LinuxPCIDevExt, linuxDev);
   VMK_ASSERT(pciDevExt->vmkDev == vmkDev);

   if (unlikely(LinuxPCIDeviceIsClaimed(pciDevExt) == VMK_FALSE)) {
      VMKLNX_INFO("Device %s %s is not claimed by vmklinux drivers\n",
                  linuxDev->dev.bus_id, vmkDevName);
      goto quit;
   }

   if (unlikely(linuxDev->driver == NULL)) {
      VMKLNX_WARN("no driver (or not hotplug compatible)");
      goto quit;
   }

   if (unlikely(linuxDev->driver->remove == NULL)) {
      VMKLNX_INFO("no remove function");
      goto quit;
   }

   vmk_PCIDoPreRemove(pciDevExt->moduleID, vmkDev);
   VMKAPI_MODULE_CALL_VOID(pciDevExt->moduleID, 
                      linuxDev->driver->remove, 
                      linuxDev);
   LinuxPCIDeviceUnclaimed(pciDevExt);

quit:

   /*
    * If device is physically removed, free up the structures. Otherwise,
    * just move it to pci_hidden_devices.
    */
   down_write(&pci_bus_sem);
   list_del(&linuxDev->global_list);
   if (cbReason != VMK_PCI_DEVICE_REMOVED) {
      list_add_tail(&linuxDev->global_list, &pci_hidden_devices);
   }
   up_write(&pci_bus_sem);

   VMKLNX_INFO("Removed device %s at event %s.", 
               linuxDev->dev.bus_id,
               cbReason == VMK_PCI_DEVICE_REMOVED ? "device-removed" : "ownership-changed");

   if (cbReason == VMK_PCI_DEVICE_REMOVED) {
      if (is_vmvisor) {
         pci_proc_detach_device(linuxDev);
      }
      vmk_HeapFree(VMK_MODULE_HEAP_ID, pciDevExt);
   }
}

void
LinuxPCIDeviceNotification(vmk_PCIDevice device, 
                           vmk_PCIDeviceCallbackArg *cbArg,
                           void *private)
{
   struct pci_dev *linuxDev;

   if (cbArg->reason == VMK_PCI_DEVICE_INSERTED ||
       (cbArg->reason == VMK_PCI_DEVICE_CHANGED_OWNER &&
        cbArg->data.changedOwner.new == VMK_PCI_DEVICE_OWNER_MODULE)) {

      LinuxPCIDeviceInserted(device, cbArg->reason);
      return;
   }

   if (cbArg->reason == VMK_PCI_DEVICE_REMOVED ||
       (cbArg->reason == VMK_PCI_DEVICE_CHANGED_OWNER &&
        cbArg->data.changedOwner.old == VMK_PCI_DEVICE_OWNER_MODULE)) {
   
      LinuxPCIDeviceRemoved(device, cbArg->reason);
      return;
   }

   linuxDev = LinuxPCIFindDeviceByHandle(device);

   if (unlikely(linuxDev == NULL)) {
      VMKLNX_INFO("Notification: event %d but no action taken", cbArg->reason);
   } else {
      VMKLNX_INFO("Notification for device %s: event %d but no action taken",
                  linuxDev->dev.bus_id, cbArg->reason);
   }
}

int proc_initialized = 0;
struct proc_dir_entry *proc_bus_pci_dir;
struct proc_dir_entry *proc_bus_pci_devices;
extern int proc_bus_pci_devices_read(char *page, char **start, off_t off, int count, int *eof, void *data);

void
LinuxPCI_Init(void)
{
   int i;
   static vmk_Bool initialized = VMK_FALSE;

   if (unlikely(initialized)) {
      return;
   }
   initialized = VMK_TRUE;
   proc_initialized = 1;

   VMKLNX_CREATE_LOG();

   /*
    * Each device has a pointer to the pci_bus structure of the bus it
    * resides on, but it seems to only use the number field of that
    * structure. So we simply create an array of such structures for
    * all possible values of the number field so we always have a
    * structure handy with the right number.
    */
   for (i = 0; i < VMK_PCI_NUM_BUSES; i++) {
      linuxPCIBuses[i].number = i;
      INIT_LIST_HEAD(&linuxPCIBuses[i].devices);
   }

   if (is_vmvisor) {
      proc_bus_pci_dir = proc_mkdir("pci", proc_bus);
      proc_bus_pci_devices = create_proc_entry("devices", 0, proc_bus_pci_dir);
      VMK_ASSERT(proc_bus_pci_devices != NULL);
      proc_bus_pci_devices->read_proc = proc_bus_pci_devices_read;
   }

   vmk_PCIRegisterCallback(vmklinuxModID, LinuxPCIDeviceNotification,
                           NULL);
}

void
LinuxPCI_Cleanup(void)
{
   /*
    * May need to unregister the callback function.
    */
   VMKLNX_DESTROY_LOG();
}

vmk_Bool
LinuxPCI_DeviceIsPAECapable(struct pci_dev *dev)
{
   if ((dev->dma_mask & 0xfffffffffULL) == 0xfffffffffULL) {
      VMKLNX_INFO("PAE capable device at %s", dev->dev.bus_id);
      return VMK_TRUE;
   } else {
      /* the following assert is not necessary.  It is simply here to warn
       * us if we ever run into a device that can dma to more than 4GB, but
       * less than 64GB */
      VMK_ASSERT((dev->dma_mask & 0x7ffffffffULL) 
              == (dev->dma_mask & 0xffffffffULL));
      return VMK_FALSE;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * LinuxPCI_DeviceClaimed --
 *
 *      Old-style (i.e. no pci_driver) drivers automatically claim devices
 *      through this during device registration with the I/O subsystems.  
 *
 * Results:
 *      None. 
 *
 * Side effects:
 *      Current module is associated with device. 
 *
 *-----------------------------------------------------------------------------
 */

void
LinuxPCI_DeviceClaimed(vmk_ModuleID moduleID, LinuxPCIDevExt *pciDevExt)
{
   vmk_PCIDevice vmkDev;

   VMKLNX_INFO("Device %x:%x claimed.\n", pciDevExt->linuxDev.bus->number,
      pciDevExt->linuxDev.devfn);

   pciDevExt->moduleID = moduleID;
   vmkDev = pciDevExt->vmkDev;
   vmk_PCIDoPostInsert(moduleID, vmkDev);
}


/*
 *----------------------------------------------------------------------------
 *
 *  LinuxPCI_FindDevByBusSlot --
 *
 *    Get struct pci_dev from (bus, slot, fn)
 *
 *  Results:
 *    Pointer to struct pci_dev. NULL if no device matches the key.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

struct pci_dev *
LinuxPCI_FindDevByBusSlot(uint16_t bus, uint16_t devfn)
{
   struct pci_dev *linuxDev;

   linuxDev = NULL;
   for_each_pci_dev(linuxDev) {
      if (linuxDev->bus->number == bus && linuxDev->devfn == devfn) {
         return linuxDev;
      }
   }

   return NULL;
}



/*
 *----------------------------------------------------------------------------
 *
 *  LinuxPCI_FindCapability --
 *
 *    Return the index to the specified capability in the config space of
 *    the given device.
 *
 *  Results:
 *    Index to the capability. 0, if the device doesn't have the specified
 *    capability.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

int
LinuxPCI_FindCapability(uint16_t bus, uint16_t devfn, uint8_t cap)
{
   vmk_PCIDevice vmkDev;
   vmk_uint16 capIdx;

   if (unlikely(vmk_PCIGetPCIDevice(bus, 
                                    PCI_SLOT(devfn), 
                                    PCI_FUNC(devfn), 
                                    &vmkDev) != VMK_OK)) {
      return 0;
   }

   if (unlikely(vmk_PCIGetCapabilityIndex(vmkDev, cap, &capIdx) != VMK_OK)) {
      return 0;
   }

   return capIdx;
}



/*
 *----------------------------------------------------------------------------
 *
 *  LinuxPCI_EnableMSI --
 *
 *    Allocate vectors and enable MSI on the specified device.
 *
 *  Results:
 *    VMK_ReturnStatus indicating the outcome.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

VMK_ReturnStatus
LinuxPCI_EnableMSI(struct pci_dev* dev)
{
   vmk_uint32 vector;
   LinuxPCIDevExt *pciDevExt;
   vmk_PCIDevice vmkDev;

   pciDevExt = container_of(dev, LinuxPCIDevExt, linuxDev);
   vmkDev = pciDevExt->vmkDev;

   if (unlikely(vmk_PCIAllocateIntVectors(vmkDev, 
                                          VMK_PCI_INTERRUPT_TYPE_MSI,
                                          1,
                                          NULL,
                                          VMK_FALSE,
                                          &vector,
                                          NULL) != VMK_OK)) {
      VMKLNX_WARN("vmk_PCIAllocateIntVectors failed on handle %p\n", 
                  (void *)vmkDev);
      return VMK_FAILURE;
   }

   /*
    * Remove the previous vector. 
    */
   LinuxPCIIntrVectorFree(pciDevExt);

   /*
    * trickle MSI vector down to driver land
    */
   LinuxPCIMSIIntrVectorSet(pciDevExt, vector);

   return VMK_OK;
}


void
LinuxPCI_DisableMSI(struct pci_dev* dev)
{
   LinuxPCIDevExt *pciDevExt;
   vmk_PCIDevice vmkDev;
   VMK_ReturnStatus status;
   vmk_uint32 vector;

   pciDevExt = container_of(dev, LinuxPCIDevExt, linuxDev);
   vmkDev = pciDevExt->vmkDev;

   vector = pciDevExt->linuxDev.irq;
   status = vmk_PCIFreeIntVectors(vmkDev, 1, &vector);
   VMK_ASSERT(status == VMK_OK);

   /*
    * Restore the legacy default PCI interrupt vector
    */
   LinuxPCILegacyIntrVectorSet(pciDevExt);
}



/*
 *----------------------------------------------------------------------------
 *
 *  LinuxPCI_EnableMSIX --
 *
 *    Allocate vectors and enable MSI-X on the specified device.
 *
 *  Results:
 *    VMK_ReturnStatus indicating the outcome. Allocated vectors are
 *    returned in 'entries'.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

VMK_ReturnStatus
LinuxPCI_EnableMSIX(struct pci_dev* dev, struct msix_entry *entries,
                    int nvecs, vmk_Bool bestEffortAllocation,
                    int *nvecs_alloced)
{
   int i;
   LinuxPCIDevExt *pciDevExt;
   vmk_PCIDevice vmkDev;
   vmk_uint32 *vectors;
   vmk_uint16 *indexes;
   VMK_ReturnStatus status;

   pciDevExt = container_of(dev, LinuxPCIDevExt, linuxDev);
   vmkDev = pciDevExt->vmkDev;

   vectors = vmk_HeapAlloc(VMK_MODULE_HEAP_ID, nvecs * sizeof(*vectors));
   if (unlikely(vectors == NULL)) {
      return VMK_NO_RESOURCES;
   }

   indexes = vmk_HeapAlloc(VMK_MODULE_HEAP_ID, nvecs * sizeof(*indexes));
   if (unlikely(indexes == NULL)) {
      vmk_HeapFree(VMK_MODULE_HEAP_ID, vectors);
      return VMK_NO_RESOURCES;
   }

   for (i = 0; i < nvecs; i++) {
      indexes[i] = entries[i].entry;
   }

   status = vmk_PCIAllocateIntVectors(vmkDev, 
                                      VMK_PCI_INTERRUPT_TYPE_MSIX, 
                                      nvecs, 
                                      indexes,
                                      bestEffortAllocation,
                                      vectors,
                                      nvecs_alloced);

   vmk_HeapFree(VMK_MODULE_HEAP_ID, indexes);

   if (status != VMK_OK) {
      VMKLNX_WARN("vmk_PCIAllocateIntVectors "
                  "failed on handle %p for %d vectors\n", 
                  (void *)vmkDev,
                  nvecs);
      vmk_HeapFree(VMK_MODULE_HEAP_ID, vectors);
      return status;
   }

   for (i = 0; i < *nvecs_alloced; i++) {
      entries[i].vector = vectors[i];
   }

   pciDevExt->intrVectors = vectors;
   pciDevExt->numIntrVectors = *nvecs_alloced;

   /*
    * Remove the previous vector. 
    */
   LinuxPCIIntrVectorFree(pciDevExt);

   return VMK_OK;
}



/*
 *----------------------------------------------------------------------------
 *
 *  LinuxPCI_DisableMSIX --
 *
 *    Disable MSI-X on the specified device. Allocated interrupts
 *    are released.
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

void
LinuxPCI_DisableMSIX(struct pci_dev* dev)
{
   LinuxPCIDevExt *pciDevExt;
   vmk_PCIDevice vmkDev;
   VMK_ReturnStatus status;

   pciDevExt = container_of(dev, LinuxPCIDevExt, linuxDev);
   vmkDev = pciDevExt->vmkDev;

   status = vmk_PCIFreeIntVectors(vmkDev, 
                                  pciDevExt->numIntrVectors, 
                                  pciDevExt->intrVectors);
   VMK_ASSERT(status == VMK_OK);

   pciDevExt->numIntrVectors = 0;
   vmk_HeapFree(VMK_MODULE_HEAP_ID, pciDevExt->intrVectors);
   pciDevExt->intrVectors = NULL;

   /*
    * Restore the legacy default PCI interrupt vector
    */
   LinuxPCILegacyIntrVectorSet(pciDevExt);
}


/*
 *----------------------------------------------------------------------------
 *
 *  LinuxPCI_FindDevByPortBase --
 *
 *    Get struct pci_dev from io_port or base address
 *
 *  Results:
 *    Pointer to struct pci_dev. NULL if no device matches the keys.
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

struct pci_dev *
LinuxPCI_FindDevByPortBase(unsigned long base, unsigned long io_port)
{
   struct pci_dev *linuxDev;
   int i;

   linuxDev = NULL;
   for_each_pci_dev(linuxDev) {
      for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
         unsigned long start = pci_resource_start(linuxDev, i);
         unsigned long flags = pci_resource_flags(linuxDev, i);
         if ((start != 0) &&
             (((flags == PCI_BASE_ADDRESS_SPACE_MEMORY) && (start == base)) ||
              ((flags == PCI_BASE_ADDRESS_SPACE_IO) && (start == io_port)))) {
            return linuxDev;
         }
      }
   }

   return NULL;
}



/*
 *----------------------------------------------------------------------------
 *
 * LinuxPCI_Shutdown --
 *
 *      Call shutdown methods of PCI network device drivers
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Shuts down devices for poweroff.  Some net devices need this to
 *      arm them for wake-on-LAN.
 *
 * Note:
 *      XXX Native Linux calls shutdown methods for *all* PCI devices
 *      (and PCI buses too).  Maybe we should do that eventually, but
 *      (1) currently we only have code to call the close/stop methods
 *      for network devices, and those should really be called before
 *      the shutdown methods, (2) usb_uhci's shutdown method caused a
 *      PSOD when I had this routine calling all device shutdown
 *      methods, (3) we've been getting along fine without calling
 *      shutdown on these devices up to now.  So for now we shutdown
 *      only the network devices.  --mann
 *
 *----------------------------------------------------------------------------
 */

void
LinuxPCI_Shutdown(void)
{
   struct pci_dev *linuxDev;
   struct pci_dev *tmp;
   vmk_ModuleID moduleID;

   /*
    * Hold pci_bus_sem to iterate pci_devices safely
    */
   down_read(&pci_bus_sem);
   list_for_each_entry_safe_reverse(linuxDev, tmp, &pci_devices, global_list) {

      VMKLNX_DEBUG(1, "%02x.%02x:%x %s: netdev %p, shutdown %p",
                   linuxDev->bus->number,
                   PCI_SLOT(linuxDev->devfn),
                   PCI_FUNC(linuxDev->devfn),
                   linuxDev->driver ? linuxDev->driver->name : "<no name>",
                   linuxDev->netdev,
                   linuxDev->driver ? linuxDev->driver->shutdown : NULL);

      if (linuxDev->netdev != NULL &&
          linuxDev->driver && linuxDev->driver->shutdown) {

         VMK_ASSERT(linuxDev->driver->driver.owner != NULL);
         moduleID = linuxDev->driver->driver.owner->moduleID;
         VMK_ASSERT(moduleID != VMK_INVALID_MODULE_ID);

         VMKAPI_MODULE_CALL_VOID(moduleID, linuxDev->driver->shutdown, 
                                 linuxDev);
      }
   }
   up_read(&pci_bus_sem);
}

/**                                          
 *  pci_dev_get - non-operational function
 *  @dev: Ignored
 *                                           
 *  A non-operational function provided to help reduce
 *  kernel ifdefs. It is not supported in this release of ESX.
 * 
 *  ESX Deviation Notes:
 *  A non-operational function provided to help reduce
 *  kernel ifdefs. It is not supported in this release of ESX.
 *
 *  RETURN VALUE:
 *  Pointer to pci device structure
 */                                          
/* _VMKLNX_CODECHECK_: pci_dev_get */
struct pci_dev *pci_dev_get(struct pci_dev *dev)
{
	return dev;
}

/**                                          
 *  pci_dev_put - non-operational function
 *  @dev: Ignored
 *                                           
 *  A non-operational function provided to help reduce
 *  kernel ifdefs. It is not supported in this release of ESX.
 * 
 *  ESX Deviation Notes:
 *  A non-operational function provided to help reduce
 *  kernel ifdefs. It is not supported in this release of ESX.
 *
 *  RETURN VALUE:
 *  This function does not return a value
 */                                          
/* _VMKLNX_CODECHECK_: pci_dev_put */
void pci_dev_put(struct pci_dev *dev)
{
	return;
}



/*
 *----------------------------------------------------------------------------
 *
 *  LinuxPCI_IsValidPCIBusDev --
 *
 *    Check whether the device is valid PCI bus device 
 *
 *  Results:
 *    True if it is valid PCI bus device
 *
 *  Side effects:
 *    None.
 *
 *----------------------------------------------------------------------------
 */

vmk_Bool
LinuxPCI_IsValidPCIBusDev(struct pci_dev *pdev)
{
   if (pdev && pdev->bus) {
      if ((PCI_SLOT(pdev->devfn) >= 0 
           && PCI_SLOT(pdev->devfn) < VMK_PCI_NUM_SLOTS) 
           && (PCI_FUNC(pdev->devfn) >= 0 
           && PCI_FUNC(pdev->devfn) <= VMK_PCI_NUM_FUNCS)) {
         return VMK_TRUE;
      }
   }
   return VMK_FALSE;
}

