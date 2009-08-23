/* ****************************************************************
 * Portions Copyright 1998, 2009 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

/*
 * linux_stubs.h --
 *
 *      Linux device driver compatibility.
 */

#ifndef _LINUX_STUBS_H_
#define _LINUX_STUBS_H_

#include "vmkapi.h"
#include "vmklinux26_dist.h"
#include "genhd.h"
#include "input.h"

#define VMKLINUX_SEMA_RANK_BLOCK (VMK_SEMA_RANK_LEAF)
#define VMKLINUX_SEMA_RANK_SCSI  (VMK_SEMA_RANK_LEAF)
#define VMKLINUX_SEMA_RANK_NET   (VMK_SEMA_RANK_LEAF)
#define VMKLINUX_SEMA_RANK_CHAR  (VMK_SEMA_RANK_LEAF)
#define VMKLINUX_DRIVERLOCK_RANK (VMK_SP_RANK_IRQ_LOWEST)
#define VMKLINUX_ABORTLOCK_RANK  (VMKLINUX_DRIVERLOCK_RANK+1)

#define VMKLINUX26_NAME           "vmklinux26"

#if VMK_SEMA_RANK_STORAGE >= VMK_SEMA_RANK_LEAF
#error "STORAGE rank should be lower than vmklinux ranks"
#endif

#define LINUX_BHHANDLER_NO_IRQS	(void *)0xdeadbeef

extern vmk_Bool is_vmvisor;

extern vmk_ModuleID vmklinuxModID;
extern vmk_SpinlockIRQ kthread_wait_lock;

#define VMKLinux26_Alloc(size)	vmklnx_kzmalloc(VMK_MODULE_HEAP_ID, size)
#define VMKLinux26_Free(ptr)	vmklnx_kfree(VMK_MODULE_HEAP_ID, ptr)

extern vmk_LogComponentHandle  vmklinux26Log;
extern vmk_Semaphore pci_bus_sem;

typedef VMK_ReturnStatus (*LinuxRegisterIRQFunc)(void *a, vmk_uint32 vector, 
                                                 vmk_InterruptHandler h,
                                                 void *handlerData);

typedef struct LinuxDevInfo {
   struct LinuxDevInfo *next;
   void *device;
   void *dataStart;
   void *dataEnd;
   uint32_t dataLength;
   const char *name;
   vmk_ModuleID moduleID;
   int vector;
   uint32_t refCount;
   LinuxRegisterIRQFunc irqRegisterFunc;
} LinuxDevInfo;

struct softirq_action;

extern void IoatLinux_Init(void);
extern void SCSILinux_Init(void);
extern void SCSILinux_Cleanup(void);
extern void LinuxUSB_Init(void);
extern void LinuxUSB_Cleanup(void);
extern void BlockLinux_Init(void);
extern void BlockLinux_Cleanup(void);
extern void LinuxChar_Init(void);
extern void LinuxChar_Cleanup(void);
extern void LinuxChar_MajorMinorToPID(uint16_t major, uint16_t minor, int *pid);
extern void LinuxChar_PIDToMajorMinor(int pid, uint16_t *major, uint16_t *minor);
extern uint16_t LinuxChar_GetVMKMajor(uint16_t major);
extern void LinuxProc_Init(void);
extern void LinuxProc_Cleanup(void);
extern struct proc_dir_entry* LinuxProc_AllocPDE(const char* name);
extern void LinuxProc_FreePDE(struct proc_dir_entry* pde);

/* TODO: reddys - remove post KL */
extern VMK_ReturnStatus Linux_IdeRegisterIRQ(void *vmkAdapter,
                                             vmk_uint32 intrVector,
                                             vmk_InterruptHandler intrHandler,
                                             void *intrHandlerData);

extern void Linux_IRQHandler(void *clientData, uint32_t vector);
vmk_Bool Linux_RegisterDevice(void *data, 
                              uint32_t dataLength, 
                              void *device, 
		              const char *name,
                              LinuxRegisterIRQFunc irqRegisterFunc,
                              vmk_ModuleID moduleID);

extern vmk_Bool Linux_UnregisterDevice(void *data, void *device);
extern LinuxDevInfo *Linux_FindDevice(void *data, vmk_Bool addIt);
extern void Linux_ReleaseDevice(LinuxDevInfo *dev);
extern void Linux_BHInternal(void (*routine)(void *), void *data,
                             vmk_ModuleID modID);
extern void Linux_BHHandler(void *clientData);
extern void Linux_BH(void (*routine)(void *), void *data); 
extern void Linux_PollBH(void *data);
extern void Linux_OpenSoftirq(int nr, void (*action)(struct softirq_action *), void *data);
void Linux_PollIRQ(void *clientData, uint32_t vector);
extern void driver_init(void);
extern int input_init(void);
extern void input_exit(void);
extern int hid_init(void);
extern void hid_exit(void);
extern int lnx_kbd_init(void);
extern int mousedev_init(void);
extern void mousedev_exit(void);
vmk_ModuleID vmklnx_get_driver_module_id(const struct device_driver *drv);

/*
 * Linux random function pointers, used by inlines below
 */
extern void (*vmklnx_add_keyboard_randomness)(int);
extern void (*vmklnx_add_mouse_randomness)(int);
extern void (*vmklnx_add_storage_randomness)(int);

/**                                          
 *  add_disk_randomness - supply data for the random data source device       
 *  @disk: the pointer to gendisk structure    
 *
 *  Supply data for the random data source device
 *
 *  ESX Deviation Notes:
 *  Implemented as an inline call to function registered by random driver.
 *  Call is protected against case where random driver has not been loaded.
 *  
 *  RETURN VALUE:
 *  NONE                                         
 */
/* _VMKLNX_CODECHECK_: add_disk_randomness */
static inline void
add_disk_randomness(struct gendisk *disk)
{
   if (likely(vmklnx_add_storage_randomness)) {
      vmklnx_add_storage_randomness(disk ? disk->major : 1);
   }
}

/**                                          
 *  add_input_randomness - supply data for the random data source device       
 *  @type: enum (EV_KEY for keyboard, EV_REL for mouse)
 *  @code: int (scancode for keyboard, position for mouse)
 *  @value: int (ignored, random driver fakes it)
 * 
 *  ESX Deviation Notes:
 *  Implemented as an inline call to functions registered by random driver.
 *  Calls are protected against case where random driver has not been loaded.
 *                                           
 */
/* _VMKLNX_CODECHECK_: add_input_randomness */
static inline void
add_input_randomness(unsigned int type, unsigned int code, unsigned int value)
{
   switch (type) {
      case EV_KEY:
         if (likely(vmklnx_add_keyboard_randomness)) {
            vmklnx_add_keyboard_randomness(code);
         }
         break;

      case EV_REL:
         if (likely(vmklnx_add_mouse_randomness)) {
            vmklnx_add_mouse_randomness(code);
         }
         break;

      default:
         // XXX should "never" happen
         break;
   }
}

#define Linux_GetModuleHeapID() vmklnx_get_module_heap_id()
#endif
