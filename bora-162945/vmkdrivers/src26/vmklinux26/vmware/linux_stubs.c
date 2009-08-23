/* ****************************************************************
 * Portions Copyright 1998, 2009 VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * ****************************************************************/

/******************************************************************
 *
 * linux_stubs.c
 *
 * From linux-2.4.31/kernel/panic.c:
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 *
 * From linux-2.6.18-8/kernel/resource.c:
 *
 * Copyright (C) 1999   Linus Torvalds
 * Copyright (C) 1999   Martin Mares <mj@ucw.cz>
 *
 * From linux-2.6.18-8/lib/cmdline.c:
 *
 * Code and copyrights come from init/main.c and arch/i386/kernel/setup.c
 *
 * From linux-2.6.18-8/init/main.c:
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 *
 * From linux-2.6.18-8/arch/i386/kernel/setup.c:
 *
 * Copyright (C) 1995  Linus Torvalds
 *
 * From linux-2.6.18-8/lib/iomap.c:
 *
 * (C) Copyright 2004 Linus Torvalds
 *
 * From linux-2.6.18-8/lib/vsprintf.c:
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 *
 * From linux-2.6.18.8/kernel/irq/manage.c:
 *
 * Copyright (C) 1992, 1998-2006 Linus Torvalds, Ingo Molnar
 * Copyright (C) 2005-2006 Thomas Gleixner
 *
 ******************************************************************/

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/utsname.h>
#include <linux/firmware.h>
#include <linux/net.h>
#include <linux/random.h>
#include <linux/usb.h>
#include <linux/dmi.h>
#include <linux/miscdevice.h>
#include <asm/dmi.h>

#include "vmkapi.h"
#include "vmklinux26_dist.h"
#include "linux_stubs.h"
#include "linux_pci.h"
#include "linux_task.h"
#include "linux_kthread.h"
#include "linux_net.h"
#include "vmklinux26_log.h"

VMK_VERSION_INFO(                                               \
   "Build: " VMK_REVISION_EXPANDSTR(BUILD_NUMBER_NUMERIC) ", "  \
   "Interface: "                                                \
   VMK_STRINGIFY(VMKLNX_API_VERSION_MAJOR_NUM) "."              \
   VMK_STRINGIFY(VMKLNX_API_VERSION_MINOR_NUM) ", "             \
   "Built on: " __DATE__);

vmk_Bool is_vmvisor;

/*
 * XXX: PR343324, PR322927
 * Gross hack to distinguish world private data from COS userspace data.  Under
 * bad circumstances we can mistake a COS userspace address for a world private
 * address and corrupt our data. Mark buffers allocated with
 * compat_alloc_user_space as VMKernel buffers using the highest bit.  It is
 * expected to be 0 in our current design (VMKernel and COS userspace addresses
 * are not sign extened). Going forward we will have to make the VMKernel and
 * COS address spaces distinct again to avoid these kinds of clashes.
 */
#define VMKBUF_MASK		((1UL)<<63)

static void * 
__MarkPtrAsVMKBuffer(void *ptr)
{
   VMK_ASSERT(((vmk_VirtAddr)ptr & VMKBUF_MASK) == 0);
   return (void *)((vmk_VirtAddr)ptr | VMKBUF_MASK);
}

static vmk_Bool
__IsPtrVMKBuffer(const void *ptr)
{
   return (((vmk_VirtAddr)ptr & VMKBUF_MASK) == VMKBUF_MASK);
}

static void *
__SanitizeVMKBufferPtr(const void *ptr)
{
   return (void *)((vmk_VirtAddr)ptr & ~VMKBUF_MASK);
}


/*
 * The number of PCPU is required by a few network drivers to optimize
 * multiqueue allocation.
 */
int smp_num_cpus;

vmk_LogComponentHandle  vmklinux26Log;
vmk_Semaphore     pci_bus_sem;

#ifndef HAVE_ARCH_PIO_SIZE
/*
 * We encode the physical PIO addresses (0-0xffff) into the
 * pointer by offsetting them with a constant (0x10000) and
 * assuming that all the low addresses are always PIO. That means
 * we can do some sanity checks on the low bits, and don't
 * need to just take things for granted.
 */
#define PIO_OFFSET      0x10000UL
#define PIO_MASK        0x0ffffUL
#define PIO_RESERVED    0x40000UL
#endif

#define VERIFY_PIO(port) BUG_ON((port & ~PIO_MASK) != PIO_OFFSET)

#define IO_COND(addr, is_pio, is_mmio) do {                \
   unsigned long port = (unsigned long __force)addr;       \
   if (port < PIO_RESERVED) {                              \
      VERIFY_PIO(port);                                    \
      port &= PIO_MASK;                                    \
      is_pio;                                              \
   } else {                                                \
      is_mmio;                                             \
   }                                                       \
} while (0)

/*
 * Used by the sprintf->snprintf wrappers.  Dare we go
 * lower than 1 MB?
 */
#define SPRINTF_MAX_BUFLEN (1*1024*1024)

struct resource ioport_resource; 
struct resource iomem_resource; 

unsigned securebits = 0;

static vmk_SpinlockIRQ irqMappingLock;
static vmk_SpinlockIRQ regionLock;
static vmk_SpinlockIRQ devMappingLock;

#define DEV_HASH_TABLE_SIZE	32
#define DEV_HASH_MASK		(DEV_HASH_TABLE_SIZE - 1)

vmk_ModuleID vmklinuxModID;

struct LinuxDevInfo;

typedef struct LinuxDevHashEntry {
   struct LinuxDevHashEntry *next;
   LinuxDevInfo *dev;
   void *key;
} LinuxDevHashEntry;

static LinuxDevHashEntry *devHashTable[DEV_HASH_TABLE_SIZE];
static LinuxDevInfo *Linux_FindDeviceInt(void *data, vmk_Bool addIt);
static void Linux_ReleaseDeviceInt(LinuxDevInfo *dev);

static vmk_BottomHalf linuxBHNum;

/* minimum time in jiffies between messages */
int printk_ratelimit_jiffies __read_mostly = 5 * HZ;

/* number of messages we send before ratelimiting */
int printk_ratelimit_burst __read_mostly = 10;

int net_msg_cost __read_mostly = 5*HZ;
int net_msg_burst __read_mostly = 10;
int net_msg_warn __read_mostly = 1;

#define IRQ_DONT_KNOW (-1)

typedef struct IRQInfo {
   struct list_head links;
   vmklnx_irq_handler_type_id handler_type;
   vmklnx_irq_handler_t handler;
   void *deviceID;
   uint32_t irq;
   LinuxDevInfo *deviceInfo;
   vmk_ModuleID moduleID;
} IRQInfo;

typedef struct ModuleIRQList {
   struct list_head links;
   vmk_ModuleID moduleID;
   struct list_head irqList;
} ModuleIRQList;

static LIST_HEAD(irqMappings);

// From linux/asm/irq.h
#ifndef NR_IRQS
#define NR_IRQS                 224
#endif
static unsigned int disableCount[NR_IRQS];

static LinuxDevInfo *deviceList;

#define MAX_SOFTIRQ	32 
static struct softirq_action softirq_vec[MAX_SOFTIRQ]
   VMK_ATTRIBUTE_L1_ALIGNED;

typedef struct LinuxBHData {
   struct LinuxBHData *next;
   void (*routine)(void *);
   void *data;
   vmk_ModuleID modID;
   vmk_Bool staticAlloc;
} LinuxBHData;

#define MAX_GET_RANDOM_BYTES_FUNCTIONS	4

typedef struct getRandomBytesInfo {
   vmk_GetEntropyFunction function;
   vmk_ModuleID modID;
} getRandomBytesInfo;

//Will be used in phase 2
#if 0
static getRandomBytesInfo getRandomBytesFct[MAX_GET_RANDOM_BYTES_FUNCTIONS];
#endif

// Heap min and max values are based on the approach of enabling allocations of upto 
// ~128MB at a time.
#define VMKLNX_VMALLOC_HEAP_MIN   (10*1024*1024)
#define VMKLNX_VMALLOC_HEAP_MAX   (160*1024*1024)
#define VMKLNX_VMALLOC_HEAP       "vmklnxVmallocHeap"
vmk_HeapID vmklnxVmallocHeap = VMK_INVALID_HEAP_ID;

// Define module load time paramaters for vmalloc's heap	
static int vmklnx_vmalloc_heap_min = VMKLNX_VMALLOC_HEAP_MIN;
module_param(vmklnx_vmalloc_heap_min, int, 0444);
MODULE_PARM_DESC(vmklnx_vmalloc_heap_min, "Initial heap size allocated for vmalloc.");
                                                                                                                                                                                                 
static int vmklnx_vmalloc_heap_max = VMKLNX_VMALLOC_HEAP_MAX;
module_param(vmklnx_vmalloc_heap_max, int, 0444);
MODULE_PARM_DESC(vmklnx_vmalloc_heap_max, "Maximum attainable heap size for vmalloc.");

/*
 * PER_PCPU_VMKLINUX_DATA_CACHE_LINES is the number of 128 byte cache-lines
 *   needed to hold non-pad content of vmkLinuxPCPU_t.
 */
#define PER_PCPU_VMKLINUX_DATA_CACHE_LINES 1

typedef struct vmkLinuxPCPU_t {
   union {
      struct {
         LinuxBHData *linuxBHList VMK_ATTRIBUTE_L1_ALIGNED;
         vmk_atomic64 softirq_pending;
         LinuxBHData softirqLinuxBHData;
      };
      char pad[PER_PCPU_VMKLINUX_DATA_CACHE_LINES*L1_CACHE_BYTES] 
           VMK_ATTRIBUTE_L1_ALIGNED;
   } VMK_ATTRIBUTE_L1_ALIGNED;
} vmkLinuxPCPU_t VMK_ATTRIBUTE_L1_ALIGNED;

static vmkLinuxPCPU_t vmkLinuxPCPU[NR_CPUS] VMK_ATTRIBUTE_L1_ALIGNED;

static vmk_GetEntropyFunction HWRNGget_random_bytes = NULL;
static unsigned int addRandomnessModuleID = 0;
static unsigned int getRandomBytesModuleID = 0;

struct new_utsname system_utsname = {
	.sysname	= "vmklinux26",
	.nodename       = "(none)",  
	.release	= "",
	.version	= "",
	.machine	= "",
	.domainname	= "",
};

inline struct new_utsname *init_utsname(void)
{
   return &system_utsname;
}

/* Config option handles for dump parameters */
static vmk_ConfigParamHandle dumpPollRetriesHandle;
static vmk_ConfigParamHandle dumpPollDelayHandle;

/* DMI related */
int dmi_alloc_index;
char dmi_alloc_data[DMI_MAX_DATA];
/*
 * The rtnl_mutex is the only portion of rtnetlink.c we retain.
 */
DEFINE_MUTEX(vmklnx_rtnl_mutex);

/*
 * Support for compat_alloc_user_space.
 */
struct umem {
   long len;

   /*
    * Must come last.  The actual size of the data field is
    * dynamically determined at run time.
    */
   char data[1];
};
static vmk_WorldPrivateInfoKey umem_key;
static int umem_initialized = 0;
#define MIN_UMEM_SIZE   128

/*
 * Forward declarations.
 */
static void umem_init(void);
static void umem_deinit(void);


/*
 *----------------------------------------------------------------------
 *
 * vmklnx_get_dump_poll_retries --
 *
 *      Get the number of device retries to use when doing a coredump 
 *
 *----------------------------------------------------------------------
 */
unsigned int
vmklnx_get_dump_poll_retries(void)
{
   unsigned int value;

   vmk_ConfigParamGetUint(dumpPollRetriesHandle, &value);
   return value;
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_get_dump_poll_delay --
 *
 *      Get the number of microseconds to delay between poll attempts
 *      when doing a coredump 
 *
 *----------------------------------------------------------------------
 */
unsigned int
vmklnx_get_dump_poll_delay(void)
{
   unsigned int value;

   vmk_ConfigParamGetUint(dumpPollDelayHandle, &value);
   return value;
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_errno_to_vmk_return_status --
 *
 *      Convert a linux errno to a VMK_ReturnStatus
 *
 *----------------------------------------------------------------------
 */
VMK_ReturnStatus
vmklnx_errno_to_vmk_return_status(int error)
{
    return ((error) == 0 ? VMK_OK :
            (error) <  0 ? VMK_GENERIC_LINUX_ERROR - (error) :
                           VMK_GENERIC_LINUX_ERROR + (error));
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_phys_to_kmap --
 *
 *      Stub routine that maps a machine address to a kernel virtual
 *      address. Need to use this where the machine address could have
 *      come from the host
 *      Note: length cannot be more than one page
 *
 * Results: 
 *	Virtual address pointing to the machine address
 *
 *----------------------------------------------------------------------
 */
void *
vmklnx_phys_to_kmap(vmk_uint64 maddr, vmk_uint32 length)
{
   VMK_ReturnStatus status;
   void *vaddr;
   vmk_MachPageRange range;
   vmk_MachPage firstMPN, lastMPN;

   VMK_ASSERT(length > 0 && length <= PAGE_SIZE);

   firstMPN = (vmk_MachPage)vmk_MachAddrToMachPage(maddr);
   lastMPN = (vmk_MachPage)vmk_MachAddrToMachPage(maddr + length - 1);

   range.startPage = firstMPN;
   range.numPages = lastMPN - firstMPN + 1;
   status = vmk_MapVA(&range, 1, (vmk_VirtAddr *)&vaddr);

   VMK_ASSERT(vaddr);
   
   return (void *)vaddr;
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_phys_to_kmap_free --
 *
 *      Stub routine that frees a virtual to machine mapping 
 *
 * Results: 
 *	none
 *
 * Side effects:
 *	
 *
 *----------------------------------------------------------------------
 */
void
vmklnx_phys_to_kmap_free(void *vaddr)
{
   vmk_UnmapVA((vmk_VirtAddr)vaddr);
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_p2v_memcpy --
 * 
 *    This routine will copy data from physical address 'src' to virtual
 *    address 'dst', in VMK_PAGE_SIZE chunks.  This is necessary because
 *    vmklnx_phys_to_kmap is limited to mapping PAGE_SIZE bytes of memory for 
 *    unmapped physical addresses.  
 *
 * Results:
 *    'dst' contains data copied from 'src' of length 'length'.
 *
 * Side effects:
 *    Briefly allocates a mapping in kseg.  
 *
 *----------------------------------------------------------------------
 */
inline void 
vmklnx_p2v_memcpy(void *dst, vmk_uint64 src, vmk_uint32 length)
{
   void *src_virt;
   uint32_t left = length;

   while(left) {
      uint32_t xfer_sz = min(left, (uint32_t)VMK_PAGE_SIZE);
      uint32_t offset = length - left;

      src_virt = vmklnx_phys_to_kmap(src + offset, xfer_sz);
      memcpy(dst + offset, src_virt, xfer_sz);
      vmklnx_phys_to_kmap_free(src_virt);
      left -= xfer_sz;
   }
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_v2p_memcpy --
 * 
 *    This routine will copy data from virtual address 'src' to physical
 *    address 'dst', in VMK_PAGE_SIZE chunks.  This is necessary because
 *    vmklnx_phys_to_kmap is limited to mapping PAGE_SIZE bytes of memory for 
 *    unmapped physical addresses.  
 *
 * Results:
 *    'dst' contains data copied from 'src' of length 'length'.
 *
 * Side effects:
 *    Briefly allocates a mapping in kseg.  
 *
 *----------------------------------------------------------------------
 */
inline void 
vmklnx_v2p_memcpy(vmk_uint64 dst, void *src, vmk_uint32 length)
{
   void *dst_virt;
   uint32_t left = length;

   while(left) {
      uint32_t xfer_sz = min(left, (uint32_t)VMK_PAGE_SIZE);
      uint32_t offset = length - left;

      dst_virt = vmklnx_phys_to_kmap(dst + offset, xfer_sz);
      memcpy(dst_virt, src + offset, xfer_sz);
      vmklnx_phys_to_kmap_free(dst_virt);
      left -= xfer_sz;
   }
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_mem_map
 *
 *      TODO: kalyanc; Stubbed version to support mem_map as of now
 *
 * Results: 
 *      NULL
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */

struct page*
vmklnx_mem_map(char* msg, ...)
{
   
   char* filename;
   int linenum; 
   va_list args;

   va_start(args, msg);
   filename = va_arg(args, char*);
   linenum = va_arg(args, int);
   va_end(args);

   vmk_Panic(msg, filename, linenum);
   return NULL;
}

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 */
void __iomem *
__ioremap(unsigned long phys_addr, unsigned long size, unsigned long flags)
{
   VMK_ReturnStatus status;
   void *addr;
   vmk_MachPageRange range;
   vmk_MachPage firstMPN, lastMPN;

   VMKLNX_DEBUG(4, "phys_addr 0x%lx size 0x%lx flags 0x%lx", phys_addr, size, flags);

   firstMPN = (vmk_MachPage)vmk_MachAddrToMachPage(phys_addr);
   lastMPN = (vmk_MachPage)vmk_MachAddrToMachPage(phys_addr + size - 1);
   range.startPage = firstMPN;
   range.numPages = lastMPN - firstMPN + 1;
   
   if (flags & _PAGE_PCD) {
      status = vmk_MapVAUncached(&range, 1, (vmk_VirtAddr *)&addr);
   } else {
      status = vmk_MapVA(&range, 1, (vmk_VirtAddr *)&addr);
   }

   VMK_ASSERT(status == VMK_OK);

   /*
    * Remember, linux's PAGE_MASK is (~(PAGE_SIZE - 1)) instead
    * of just PAGE_SIZE like vmkernel.
    */
   addr += phys_addr & ~PAGE_MASK;

   VMKLNX_DEBUG(4, "returning %p", addr);

   return addr;
}

/**                                          
 *  ioremap_nocache - perform an uncacheable mapping of physical memory
 *  @offset: physical address to map   
 *  @size: number of bytes to map
 *                                           
 *  Map in a physically contiguous range into kernel virtual memory and
 *  get a pointer to the mapped region. The region is mapped uncacheable.      
 *                                           
 *  RETURN VALUE:
 *     None.
 *                                           
 */                                         
/* _VMKLNX_CODECHECK_: ioremap_nocache */
void __iomem * 
ioremap_nocache (unsigned long offset, unsigned long size)
{
   return __ioremap(offset, size, _PAGE_PCD);
}


/**                                          
 *  iounmap - unmap a previously mapped physically contiguous region       
 *  @addr: virtual address of the mapping to unmap   
 *                                           
 *  Unmap a physically contiguous region mapped into the kernel by
 *  ioremap or ioremap_nocache.
 *                                           
 *  RETURN VALUE:
 *     None.
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: iounmap */
void
iounmap(volatile void __iomem *addr)
{
   VMKLNX_DEBUG(4, "Unmapping %p", addr);
   vmk_UnmapVA((vmk_VirtAddr)addr);
}

/* Create a virtual mapping cookie for an IO port range */
void __iomem *
ioport_map(unsigned long port, unsigned int nr)
{
   if (port > PIO_MASK)
      return NULL;
   return (void __iomem *) (unsigned long) (port + PIO_OFFSET);
}

void 
ioport_unmap(void __iomem *addr)
{
   /* Nothing to do */
}

/* Create a virtual mapping cookie for a PCI BAR (memory or IO) */
/**                                          
 *  pci_iomap -  Perform a virtual mapping for a PCI BAR (memory or IO)
 *  @dev:  pointer to the PCI device data structure
 *  @bar:  the BAR (base address register) whose access is sought
 *  @maxlen: length of the memory requested
 *                                           
 *  Map in a contiguous physical memory region corresponding to the address in 
 *  BAR @bar of PCI device @dev with size @maxlen to a virtual address.
 *                                           
 *  Return Value:
 *  address if successful
 *  NULL if failed
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: pci_iomap */
void __iomem *
pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
   unsigned long start = pci_resource_start(dev, bar);
   unsigned long len = pci_resource_len(dev, bar);
   unsigned long flags = pci_resource_flags(dev, bar);

   if (!len || !start)
      return NULL;
   if (maxlen && len > maxlen)
      len = maxlen;
   if (flags & IORESOURCE_IO)
      return ioport_map(start, len);
   if (flags & IORESOURCE_MEM) {
      if (flags & IORESOURCE_CACHEABLE)
         return ioremap(start, len);
      return ioremap_nocache(start, len);
   }
   /* What? */
   return NULL;
}

/**                                          
 *  pci_iounmap - unmap a previously mapped physically contiguous PCI region
 *  @dev: pointer to the PCI device data structure
 *  @addr: virtual address of the mapping to unmap
 *                                           
 *  Unmap a physically contiguous region mapped into the kernel by pci_iomap
 *                                           
 *  Return Value:
 *  Does not return any value                                           
 */                                          
/* _VMKLNX_CODECHECK_: pci_iounmap */
void 
pci_iounmap(struct pci_dev *dev, void __iomem * addr)
{
   IO_COND(addr, /* nothing */, iounmap(addr));
}

/* from lib/iomap.c */
/**                                          
 *  ioread8 - Read 8-bits from an IO memory address     
 *  @addr: IO address to read     
 *                                           
 *  Reads 8-bits from the specified IO memory address and returns the
 *  value as an integer.                      
 *                                           
 *  RETURN VALUE:
 *    8-bits from the specified IO memory address
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: ioread8 */
unsigned int fastcall
ioread8(void __iomem *addr)
{
   IO_COND(addr, return inb(port), return readb(addr));
}

/**                                          
 *  ioread16 - Read 16-bits from an IO memory address     
 *  @addr: IO address to read     
 *                                           
 *  Reads 16-bits from the specified IO memory address and returns the
 *  value as an integer.                      
 *                                           
 *  RETURN VALUE:
 *    16-bits from the specified IO memory address
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: ioread16 */
unsigned int fastcall
ioread16(void __iomem *addr)
{
   IO_COND(addr, return inw(port), return readw(addr));
}

/**                                          
 *  ioread32 - Read 32-bits from an IO memory address     
 *  @addr: IO address to read     
 *                                           
 *  Reads 32-bits from the specified IO memory address and returns the
 *  value as an integer.                      
 *                                           
 *  RETURN VALUE:
 *    32-bits from the specified IO memory address
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: ioread32 */
unsigned int fastcall 
ioread32(void __iomem *addr)
{
   IO_COND(addr, return inl(port), return readl(addr));
}

/**                                          
 *  iowrite8 - Write 8-bits to an IO memory address
 *  @val: Value to to write.
 *  @addr: IO address to write to.
 *                                           
 *  Writes 8-bits to the specified IO memory address.                     
 *                                           
 *  RETURN VALUE:
 *    None.
 *                                           
 */                                         
/* _VMKLNX_CODECHECK_: iowrite8 */
void fastcall
iowrite8(u8 val, void __iomem *addr)
{
   IO_COND(addr, outb(val,port), writeb(val, addr));
}

/**                                          
 *  iowrite16 - Write 16-bits to an IO memory address
 *  @val: Value to to write.
 *  @addr: IO address to write to.
 *                                           
 *  Writes 16-bits to the specified IO memory address.                     
 *                                           
 *  RETURN VALUE:
 *    None.
 *                                           
 */                                         
/* _VMKLNX_CODECHECK_: iowrite16 */
void fastcall
iowrite16(u16 val, void __iomem *addr)
{
   IO_COND(addr, outw(val,port), writew(val, addr));
}

/**                                          
 *  iowrite32 - Write 32-bits to an IO memory address
 *  @val: Value to to write.
 *  @addr: IO address to write to.
 *                                           
 *  Writes 32-bits to the specified IO memory address.                     
 *                                           
 *  RETURN VALUE:
 *    None.
 *                                           
 */                                         
/* _VMKLNX_CODECHECK_: iowrite32 */
void fastcall 
iowrite32(u32 val, void __iomem *addr)
{
   IO_COND(addr, outl(val,port), writel(val, addr));
}

vmk_HeapID 
vmklnx_get_module_heap_id(void)
{
   vmk_HeapID heapID;
   vmk_ModuleID moduleID;

   moduleID = vmk_ModuleStackTop();
   VMKLNX_ASSERT_NOT_IMPLEMENTED(moduleID != VMK_INVALID_MODULE_ID);

   if (moduleID == VMK_VMKERNEL_MODULE_ID) {
      /*
       * This is allocation from vmkernel thread. For now use vmklinux26 heap
       */
      heapID = vmklinux_HeapID;
   } else {
      // XXX This is a temporary workaround, see PR 65541
      if ((heapID = vmk_ModuleGetHeapID(moduleID)) == VMK_INVALID_HEAP_ID) {
         VMKLNX_WARN("Using vmklinuxHeap for module %"VMK_FMT64"x",
                     vmk_ModuleGetDebugID(moduleID));
         heapID = vmklinux_HeapID;
         vmk_ModuleSetHeapID(moduleID, heapID);
      }
   }

   return heapID;
}

/* Internal structure/routines used by alloc_pages() and __free_pages() to 
 * manage physical page number to virtual address mapping.
 */
struct pagevirt {
   struct list_head pagevirts;
   unsigned long pfn;
#if defined(VMX86_DEBUG)
   unsigned int order;
#endif
   void *virtual;
};

/* List of active pagevirt_map structs */
static LIST_HEAD(pagevirt_list);
static DEFINE_SPINLOCK(pagevirt_lock);
kmem_cache_t* pagevirt_cache;
#if defined(VMX86_DEBUG)
#define INIT_PAGEVIRT(pv, page, order, vaddr)		\
	({ (pv)->pfn = page_to_pfn(page);		\
	   (pv)->order = (order);			\
	   (pv)->virtual = (vaddr);			\
	   INIT_LIST_HEAD(&((pv)->pagevirts));		\
	})
#else
#define INIT_PAGEVIRT(pv, page, order, vaddr)           \
        ({ (pv)->pfn = page_to_pfn(page);               \
           (pv)->virtual = (vaddr);                     \
           INIT_LIST_HEAD(&((pv)->pagevirts));          \
        })
#endif

static inline int set_pagevirt(struct page *page, unsigned int order, void *vaddr)
{
   unsigned long flags;
   struct pagevirt *pv = kmem_cache_alloc(pagevirt_cache, GFP_KERNEL);
   if (!pv) {
      VMKLNX_WARN("Out of memory");
      return -ENOMEM;
   }
   /* setup mapping */
   INIT_PAGEVIRT(pv, page, order, vaddr);

   /* add to pagevirt_list */
   spin_lock_irqsave(&pagevirt_lock, flags);
   list_add(&(pv->pagevirts), &pagevirt_list);
   spin_unlock_irqrestore(&pagevirt_lock, flags);
   return 0;
}

static inline void* get_pagevirt(struct page *page, unsigned int order)
{
   unsigned long flags;
   struct pagevirt *pv, *next;
   void *vaddr = NULL;
   unsigned long pfn = page_to_pfn(page);
   spin_lock_irqsave(&pagevirt_lock, flags);
   list_for_each_entry_safe(pv, next, &pagevirt_list, pagevirts) {
      if (pv->pfn == pfn) {
         VMK_ASSERT(pv->order == order);
         vaddr = pv->virtual;
         list_del(&(pv->pagevirts));
         kmem_cache_free(pagevirt_cache, (void *)pv);
         break;
      }
   }
   spin_unlock_irqrestore(&pagevirt_lock, flags);
   return vaddr;
}

static int init_pagevirt_cache(void)
{
   pagevirt_cache = kmem_cache_create("pagevirt_cache",
                  sizeof(struct pagevirt),
                  0, SLAB_HWCACHE_ALIGN, NULL, NULL);

   if (!pagevirt_cache) {
      VMKLNX_WARN("pagevirt cache creation failed!");
      return -ENOMEM;
   }
   return 0;
}

/**                                          
 *  free_pages - free memory pages
 *  @p: virtual address of memory pages
 *  @order: Ignored
 *                                           
 *  Free the pages which were allocated using __get_free_pages.
 *                                           
 *  RETURN VALUE:
 *  NONE
 */                                          
/* _VMKLNX_CODECHECK_: free_pages */
fastcall void 
free_pages(unsigned long p, unsigned int order)
{
   vmk_HeapFree(Linux_GetModuleHeapID(), (void *)p);
}

/**                                          
 *  __free_pages - free memory pages
 *  @p: page descriptor
 *  @order: logarithm of (number of contiguous pages requested) to base 2
 *                                           
 *  Free the pages which were allocated using alloc_pages.
 *                                           
 *  RETURN VALUE:
 *  NONE
 */                                          
/* _VMKLNX_CODECHECK_: __free_pages */
fastcall void
__free_pages(struct page *p, unsigned int order)
{
   void *vaddr = get_pagevirt(p, order);
   if (vaddr) {
      vmk_HeapFree(Linux_GetModuleHeapID(), vaddr);
   } else {
      /* We come here only when:
       * 1. put_page() was called on a page that's not owned by caller (or)
       * 2. tried to free page(s) not allocated via alloc_pages() (or)
       * 3. passed NULL ptr
       */
      VMKLNX_DEBUG(2, "Tried to free a page not owned by you or put_page() was called");
   }
}

/**
  *  alloc_pages - allocate pages and return the page descriptor
  *  @gfp_mask: a bit mask of flags indicating the required memory properties
  *  @order: size in the power of 2
  *
  *  Allocates 2^@order memory in contiguous pages and returns the page descriptor for it.
  *
  * ESX Deviation notes:
  * gfp_mask is ignored.
  *
  * The resulting pointer to the page descriptor should not be referenced nor
  * used in any form of pointer arithmetic to obtain the page descriptor to
  * any adjacent page. The pointer should be treated as an opaque handle and
  * should only be used as argument to other functions.
  *
  * SEE ALSO:
  * __free_pages
  * virt_to_page
  *
  * RETURN VALUE
  * a pointer to struct page if successful, NULL otherwise.
  */
/* _VMKLNX_CODECHECK_: alloc_pages */
fastcall struct page*
alloc_pages(unsigned int gfp_mask, unsigned int order)
{
   void *vaddr;
   struct page *page;
   unsigned long size = (VMK_PAGE_SIZE << order);
   vmk_HeapID heapID = Linux_GetModuleHeapID();

   vaddr = vmk_HeapAlign(heapID, size, PAGE_SIZE);
   if (vaddr == NULL) {
      VMKLNX_WARN("Out of memory");
      return NULL;
   }

   page = virt_to_page(vaddr);

   /* setup page-virt mappping for later use by __free_pages() */
   if (set_pagevirt(page, order, vaddr)) {
      vmk_HeapFree(heapID, vaddr);
      VMK_ASSERT(0);
      return NULL;
   }

   return page;
}

/**                                          
 *  __get_free_pages - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: __get_free_pages */
fastcall unsigned long
__get_free_pages(unsigned int gfp_mask, unsigned int order)
{
   void *vaddr;
   unsigned long size = (VMK_PAGE_SIZE << order);
   vaddr = vmk_HeapAlign(Linux_GetModuleHeapID(), size, PAGE_SIZE);
   if (vaddr == NULL) {
      VMKLNX_WARN("Out of memory");
      return 0;
   }
   return (unsigned long) vaddr;
}

typedef struct Region {
   struct Region	*next;
   resource_size_t	base;
   resource_size_t	length;
   char			*name;
} Region;

static Region *regions;

static void
AddRegion(Region *prev, unsigned long from, unsigned long extent, const char *name)
{
   Region *r = (Region *)vmk_HeapAlloc(VMK_MODULE_HEAP_ID, sizeof(Region));
   if (r == NULL) {
      VMKLNX_WARN("Out of memory");
      return;
   }
   r->base = from;
   r->length = extent;
   r->name = vmk_HeapAlloc(VMK_MODULE_HEAP_ID, strlen(name) + 1);
   if (r->name == NULL) {
      VMKLNX_WARN("Out of memory");
      vmk_HeapFree(VMK_MODULE_HEAP_ID, r);
      return;
   }
   memcpy(r->name, name, strlen(name) + 1);

   if (prev == NULL) {
      r->next = regions;
      regions = r;
   } else {
      r->next = prev->next;
      prev->next = r;
   }
}

/**                                          
 *  __check_region - check if a resource region is busy or free
 *  @parent: parent resource descriptor
 *  @from: resource start address
 *  @extent: resource region size
 *
 *  Checks if a resource region is busy or free
 *                                           
 *  NOTES:
 *  This function is deprecated because its use may result in race condition.
 *  Even if it returns 0, a subsequent call to request_region may fail because
 *  another driver etc. just allocated the region. 
 *  Do NOT use it. It will be removed from the kernel. 
 *
 *  RETURN VALUE:
 *  Returns 0 if the region is free at the moment it is checked, non-zero otherwise.
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: __check_region */
int __check_region(struct resource *parent, resource_size_t from, resource_size_t extent)
{
   Region *r;
   uint64_t prevIRQL;
   int status = 0;

   VMKLNX_DEBUG(2, "0x%x for 0x%x", from, extent);

   prevIRQL = vmk_SPLockIRQ(&regionLock);

   for (r = regions; r != NULL; r = r->next) {
      if ((from >= r->base && from < r->base + r->length) ||
            (from < r->base && from + extent > r->base)) {
         VMKLNX_DEBUG(2, "Region conflict");
         status = 1;
         break;
      } else if (from < r->base) {
         break;
      }
   }

   vmk_SPUnlockIRQ(&regionLock, prevIRQL);

   return status;
}

/**                                          
 *  __request_region - Reserve a region within a resource
 *  @parent: parent resource descriptor
 *  @from: resource start address
 *  @extent: resource region size
 *  @name: reserving caller's ID string
 * 
 *                                           
 *  ESX Deviation Notes:                     
 *  Always returns (struct resource *) 1
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: __request_region */
struct resource * __request_region(struct resource *parent, resource_size_t from, resource_size_t extent, const char *name)
{
   Region *r, *prev;
   uint64_t prevIRQL;

   VMKLNX_DEBUG(2, "0x%x for 0x%x named %s", from, extent, name);

   prevIRQL = vmk_SPLockIRQ(&regionLock);

   for (prev = NULL, r = regions;;  prev = r, r = r->next) {
      if (r == NULL) {
         AddRegion(prev, from, extent, name);
         vmk_SPUnlockIRQ(&regionLock, prevIRQL);
         break;
      } else if ((from >= r->base && from < r->base + r->length) ||
            (from < r->base && from + extent > r->base)) { 
         static int throttle = 0;

         vmk_SPUnlockIRQ(&regionLock, prevIRQL);
         VMKLNX_THROTTLED_WARN(throttle,
                               "Region conflict @ 0x%x => 0x%x",
                               from,
                               from + extent - 1);
         break;
      } else if (from < r->base) {
         AddRegion(prev, from, extent, name);
         vmk_SPUnlockIRQ(&regionLock, prevIRQL);
         break;
      }
   }

   // all three loop exits drop the regionLock
   VMK_ASSERT_SPLOCKIRQ_UNLOCKED_IRQ(&regionLock);

   /* Drivers check for the return value to not be NULL, but they currently 
    * don't use it.  If they ever do, returning a 1 here, should allow 
    * us to track down the problem quickly.
    */
   return (struct resource *)1;
}

/**                                          
 *  __release_region - release a previously reserved resource region     
 *  @parent: parent resource descriptor
 *  @from: resource start address
 *  @extent: resource region size
 *                                           
 *  The described resource region must match a currently busy region
 *                                           
 *  ESX Deviation Notes:
 *  No warning is printed when region is not found. 
 */                                          
/* _VMKLNX_CODECHECK_: __release_region */
void __release_region(struct resource *parent, resource_size_t from, resource_size_t extent)
{
   Region *r, *prev;
   uint64_t prevIRQL;

   VMKLNX_DEBUG(2, "0x%x for 0x%x", from, extent);

   prevIRQL = vmk_SPLockIRQ(&regionLock);

   for (prev = NULL, r = regions; r != NULL; prev = r, r = r->next) {
      if (from == r->base && extent == r->length) {
         if (prev == NULL) {
            regions = r->next;
         } else {
            prev->next = r->next;
         }
         vmk_HeapFree(VMK_MODULE_HEAP_ID, r->name);
         vmk_HeapFree(VMK_MODULE_HEAP_ID, r);
         break;
      }
   }

   if (r == NULL) {
      // Don't print message, since release_region() could have
      // already been called by the driver.
      //VMKLNX_DEBUG(0, "Couldn't find region 0x%x for %d", from, extent);
   }

   vmk_SPUnlockIRQ(&regionLock, prevIRQL);
}

/*
 *-----------------------------------------------------------------------------
 * 
 * Linux_IRQHandler --
 *
 *      A generic interrupt handler that dispatch interrupt to driver.
 *      This handler is registered using vmk_AddInterruptHandler().
 *
 *-----------------------------------------------------------------------------
 */

void 
Linux_IRQHandler(void *clientData, vmk_uint32 vector)
{
   IRQInfo *irqInfo = (IRQInfo *)clientData;

   VMKLNX_ASSERT_BUG(48431, irqInfo->handler.handler_type1);
   
   VMK_ASSERT(irqInfo->moduleID != VMK_INVALID_MODULE_ID);
   if (irqInfo->handler_type == VMKLNX_IRQHANDLER_TYPE1) {
      irqreturn_t (*handler)(int, void *, struct pt_regs *);

      handler = irqInfo->handler.handler_type1;
      VMKAPI_MODULE_CALL_VOID(irqInfo->moduleID,
                              handler,
                              irqInfo->irq,
                              irqInfo->deviceID,
                              NULL);
   } else {
      irqreturn_t (*handler)(int, void *);

      handler = irqInfo->handler.handler_type2;
      VMKAPI_MODULE_CALL_VOID(irqInfo->moduleID,
                              handler,
                              irqInfo->irq,
                              irqInfo->deviceID);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 *  Linux_PollIRQ --
 *
 *      Call all interrupt handlers that match the given vector and moduleID
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None. 
 *
 *-----------------------------------------------------------------------------
 */

void
Linux_PollIRQ(void *clientData, 
              uint32_t vector)
{
   vmk_ModuleID moduleID = (vmk_ModuleID)(vmk_VirtAddr)clientData;
   ModuleIRQList *ml;
   IRQInfo *irqInfo;

   VMK_ASSERT(vector != 0);

   /*
    * Not grabbing irqMappings lock because this is only called from panic
    * and net debugger, which is currently non functional.  Once that
    * works, we should expand the assert to allow debugger as well.
    */
   VMK_ASSERT(vmk_SystemCheckState(VMK_SYSTEM_STATE_PANIC));

   list_for_each_entry(ml, &irqMappings, links) {
      if (moduleID == ml->moduleID) {
         break;
      }
   }
   list_for_each_entry(irqInfo, &ml->irqList, links) {
      if (vector == irqInfo->irq) {
         Linux_IRQHandler((void *)irqInfo, vector);
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 *  FindIRQInfo --
 *      Find the IRQInfo of a device must using the irq and device id as keys.
 *      The irqMappingLock must be already acquired by the caller.
 *
 * Pre-condition:
 *	irqMappingLock must be acquired by the caller.
 *
 * Results:
 *      Pointer to the IRQInfo if success; otherwise NULL.
 *
 * Side effects:
 *      If an IRQInfo is returned, the head of the link list in which
 *      the IRQInfo is found is saved in the argument listHead.
 *
 *-----------------------------------------------------------------------------
 */

static IRQInfo *
FindIRQInfo(vmk_ModuleID moduleID, 
            unsigned int irq, 
            void *devId, 
            ModuleIRQList **listHead)
{
   vmk_Bool found;
   IRQInfo *irqInfo = NULL;
   IRQInfo *info;
   ModuleIRQList *ml;

   VMK_ASSERT_SPLOCK_LOCKED_IRQ(&irqMappingLock);

   found = VMK_FALSE;

   list_for_each_entry(ml, &irqMappings, links) {
      if (moduleID == ml->moduleID) {
         found = VMK_TRUE;
         break;
      }
   }

   if (!found) {
      goto out;
   }

   list_for_each_entry(info, &ml->irqList, links) {
      if (irq != IRQ_DONT_KNOW && irq != info->irq) {
         continue;
      }

      if (irq == IRQ_DONT_KNOW && info->deviceInfo != NULL) {
         /*
          * If irq is IRQ_DONT_KNOW, that means we are in the
          * context of Linux_RegisterDevice() and irq is not 
          * available at that point. In this case, the IRQInfo 
          * that we want must have a null 'deviceInfo'. 
          */
         continue;
      }

      if (devId == info->deviceID) {
         irqInfo = info;
         break;
      }

      /*
       * TODO: reddys - hack for libata IDE/PATA drivers
       * We come here if irq is IRQ_DONT_KNOW and called in context of
       * Linux_RegisterDevice() for IDE drivers where its devId doesn't match with
       * the devId of request_irq(), so in IDE case we pass NULL for devId & catch
       * it here and the IRQInfo we want must have a NULL deviceInfo
       */
       if (devId == NULL && info->deviceInfo == NULL) {
          irqInfo = info;
          break;
       }
   }

   if (irqInfo != NULL && listHead != NULL) {
      *listHead = ml;
   }

out:
   return irqInfo;
}


/*
 *-----------------------------------------------------------------------------
 *
 *  FindUnregisterDeviceIRQInfo --
 *      Find the IRQInfo for the device that is not yet registered with
 *      Linux_RegisterDevice(), use devId as key.
 *
 * Pre-condition:
 *	irqMappingLock must be acquired by the caller.
 *
 * Results:
 *      Pointer to the IRQInfo if success; otherwise NULL.
 *
 * Side effects:
 *      If an IRQInfo is returned, the head of the link list in which
 *      the IRQInfo is found is saved in the argument listHead.
 *
 *-----------------------------------------------------------------------------
 */

static inline
IRQInfo *
FindUnregisterDeviceIRQInfo(vmk_ModuleID moduleID, 
                            void *devId, 
                            ModuleIRQList **listHead)
{
   return FindIRQInfo(moduleID, IRQ_DONT_KNOW, devId, listHead);
}

/*
 *-----------------------------------------------------------------------------
 *
 *  AddIRQInfo --
 *      Create a new IRQInfo node and add it to the irq info list of the 
 *      module specified.
 *
 * Results:
 *      The IRQInfo node of the newly added one.
 *
 * Side effects:
 *      None. 
 *
 *-----------------------------------------------------------------------------
 */

static IRQInfo *
AddIRQInfo(vmk_ModuleID moduleID,
           unsigned int irq,
           vmklnx_irq_handler_type_id handler_type,
           vmklnx_irq_handler_t handler,
	   void *devId,
           LinuxDevInfo *devInfo)
{
   IRQInfo *irqInfo;
   ModuleIRQList *ml;
   vmk_Bool found;
   uint64_t prevIRQL;

#if defined(VMX86_DEBUG)
   prevIRQL = vmk_SPLockIRQ(&irqMappingLock);
   VMK_ASSERT(FindIRQInfo(moduleID, irq, devId, NULL) == NULL);
   vmk_SPUnlockIRQ(&irqMappingLock, prevIRQL);
#endif /* defined(VMX86_DEBUG) */

   irqInfo = vmk_HeapAlloc(VMK_MODULE_HEAP_ID, sizeof(*irqInfo));
   if (irqInfo == NULL) {
      VMKLNX_WARN("Couldn't allocate a IRQInfo node for irq 0x%x", irq);
      return NULL;
   }

   irqInfo->deviceInfo   = devInfo;
   irqInfo->handler_type = handler_type;
   irqInfo->handler      = handler;
   irqInfo->deviceID     = devId;
   irqInfo->irq          = irq;      
   irqInfo->moduleID     = moduleID;
   INIT_LIST_HEAD(&irqInfo->links);

   prevIRQL = vmk_SPLockIRQ(&irqMappingLock);

   found = VMK_FALSE;
   list_for_each_entry(ml, &irqMappings, links) {
      if (ml->moduleID == moduleID) {
         found = VMK_TRUE;
         break;
      }
   }

   if (!found) {
      ml = vmk_HeapAlloc(VMK_MODULE_HEAP_ID, sizeof(*ml));
      if (ml == NULL) {
         VMKLNX_WARN("Couldn't allocate a ModuleIRQList node for irq 0x%x", irq);
         vmk_HeapFree(VMK_MODULE_HEAP_ID, irqInfo);
         irqInfo = NULL;
         goto out;
      }
      ml->moduleID = moduleID;
      INIT_LIST_HEAD(&ml->irqList);
      INIT_LIST_HEAD(&ml->links);
      list_add_tail(&ml->links, &irqMappings);
   }

   list_add_tail(&irqInfo->links, &ml->irqList);

out:
   vmk_SPUnlockIRQ(&irqMappingLock, prevIRQL);
   return irqInfo;
}

/*
 *-----------------------------------------------------------------------------
 *
 *  RemoveIRQInfo --
 *      Remove the IRQInfo of the one identified by module id, irq, and device
 *      id.
 *
 * Results:
 *      Return the IRQInfo removed from the per module irq info list.
 *
 * Side effects:
 *      If the module irqList is empty, the head of the irqList will
 *      be deleted.
 *
 *-----------------------------------------------------------------------------
 */

static IRQInfo *
RemoveIRQInfo(vmk_ModuleID moduleID, unsigned int irq, void *devId)
{
   IRQInfo *irqInfo;
   ModuleIRQList *ml;
   uint64_t prevIRQL;

   prevIRQL = vmk_SPLockIRQ(&irqMappingLock);

   irqInfo = FindIRQInfo(moduleID, irq, devId, &ml);
   if (irqInfo == NULL) {
      goto out;
   }

   list_del(&irqInfo->links);

   if (list_empty(&ml->irqList)) {
      list_del(&ml->links);
      vmk_HeapFree(VMK_MODULE_HEAP_ID, ml);
   }

out:
   vmk_SPUnlockIRQ(&irqMappingLock, prevIRQL);
   return irqInfo;
}

/*
 *-----------------------------------------------------------------------------
 *
 *  vmklnx_convert_isa_irq --
 *
 *    Helper function to convert isa irqs to vmkernel vectors
 *
 * Results:
 *    returns vmkernel vector on success. 0 on failure.
 *
 * Side effects:
 *      None. 
 *
 *-----------------------------------------------------------------------------
 */
/**                                          
 *  vmklnx_convert_isa_irq - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: vmklnx_convert_isa_irq */
unsigned int
vmklnx_convert_isa_irq(unsigned int irq)
{
   vmk_uint32 vector;

   if (irq >= 16) {
      VMKLNX_WARN("non ISA irq %d", irq);
      VMKLNX_ASSERT_NOT_IMPLEMENTED(0);
   }

   if (unlikely((irq != 14) && (irq != 15))) {
      /*
       * Not supported for now
       */
      VMKLNX_WARN("unsupported ISA irq %d", irq);
      VMKLNX_ASSERT_NOT_IMPLEMENTED(0);
   }

   /*
    * irq == vector in our scheme. For PCI devices, it's fine because we
    * directly store the vector in the irq field of the pci device structure.
    * For ISA devices, there is no equivalent, so the driver has to explicitly
    * call this function to get the vector we want it to use as irq.
    */
   if (vmk_ISAMapIRQToVector(irq, &vector) != VMK_OK) {
      VMKLNX_WARN("no vector for ISA irq %d", irq);
      return 0;
   }

   VMKLNX_DEBUG(0, "Converted ISA IRQ %d to vector %d\n", irq, vector);
   return vector;
}

int 
vmklnx_request_irq(unsigned int irq,
	           vmklnx_irq_handler_type_id handler_type,
                   vmklnx_irq_handler_t handler,
	           unsigned long flags, 
	           const char *device,
	           void *dev_id)
{
   LinuxDevInfo *devInfo;
   uint32_t vector; 
   vmk_VectorAddHandlerOptions addOptions;
   vmk_ModuleID moduleID;
   IRQInfo *irqInfo;

   VMKLNX_DEBUG(1, "called for: %d", irq);

   devInfo = Linux_FindDevice(dev_id, VMK_FALSE);
   if (devInfo != NULL) {
      VMKLNX_DEBUG(0, "Found device %s", devInfo->name);
      moduleID = devInfo->moduleID;
   } else {
      moduleID = vmk_ModuleStackTop();
   }

   irqInfo = AddIRQInfo(moduleID, irq, handler_type, handler, dev_id, devInfo);
   if (irqInfo == NULL) {
      goto err;
   }

   vector = irq;

   if (flags & IRQF_SAMPLE_RANDOM) {
      VMKLNX_DEBUG(2, "Enabling entropy sampling for device %s with moduleID %"VMK_FMT64"x",
                   devInfo != NULL ? devInfo->name : "unknown",
                   vmk_ModuleGetDebugID(moduleID));
   } else {
      VMKLNX_DEBUG(1, "Overriding driver, enabling entropy sampling for "
                   "device %s with moduleID %"VMK_FMT64"x",
                   devInfo != NULL ? devInfo->name : "unknown",
                   vmk_ModuleGetDebugID(moduleID));
   }

   addOptions.sharedVector = (flags & SA_SHIRQ) != 0;
   addOptions.entropySource = VMK_TRUE; /* entropy source for random device*/

   if (vmk_AddInterruptHandler(vector,
                               device,
                               Linux_IRQHandler,
                               irqInfo,
                               &addOptions
                               ) != VMK_OK) {
      VMKLNX_WARN("Couldn't register vector 0x%x", vector);
      goto err;
   }

   if (vmk_VectorEnable(vector) != VMK_OK) {
      VMKLNX_WARN("Couldn't enable vector 0x%x", vector);
      vmk_RemoveInterruptHandler(vector, irqInfo);
      goto err;
   }

   if (devInfo != NULL) {
      devInfo->vector = vector;	 
      if (devInfo->irqRegisterFunc != NULL) {
         devInfo->irqRegisterFunc(devInfo->device, vector, Linux_IRQHandler, irqInfo);
      }
      Linux_ReleaseDevice(devInfo);
   }

   return 0;

err:
   if (devInfo != NULL) {
      Linux_ReleaseDevice(devInfo);
   }

   if (irqInfo != NULL) {
      RemoveIRQInfo(moduleID, irq, dev_id);
      vmk_HeapFree(VMK_MODULE_HEAP_ID, irqInfo);
   }
   return -1;
}

/**                                          
 *  free_irq - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: free_irq */
void 
free_irq(unsigned int irq, void *dev_id)
{
   IRQInfo *irqInfo;
   LinuxDevInfo *devInfo;
   vmk_ModuleID moduleID;

   VMKLNX_DEBUG(1, "called for %d", irq);

   devInfo = Linux_FindDevice(dev_id, VMK_FALSE);
   if (devInfo != NULL) {
      VMKLNX_DEBUG(0, "Found device %s", devInfo->name);
      moduleID = devInfo->moduleID;
      Linux_ReleaseDevice(devInfo);
   } else {
      moduleID = vmk_ModuleStackTop();
   }

   irqInfo = RemoveIRQInfo(moduleID, irq, dev_id);
   VMK_ASSERT(irqInfo != NULL);
   if (irqInfo) {
      vmk_RemoveInterruptHandler((vmk_uint32) irq, irqInfo);
      vmk_HeapFree(VMK_MODULE_HEAP_ID, irqInfo);
   }
}

/**                                          
 *  enable_irq - enable interrupt handling on an IRQ
 *  @irq: interrupt to enable
 *
 *  Enables interrupts for the given IRQ
 *
 *  RETURN VALUE:
 *  This function does not return a value
 */                                          
/* _VMKLNX_CODECHECK_: enable_irq */
void 
enable_irq(unsigned int irq)
{
   uint32_t vector = irq;
   uint64_t prevIRQL;

   VMKLNX_DEBUG(1, "called for: %d", irq);

   prevIRQL = vmk_SPLockIRQ(&irqMappingLock);

   switch (disableCount[irq]) {
   case 1:
      if (vmk_VectorEnable(vector) != VMK_OK) {
         VMKLNX_WARN("Cannot enable vector %d", vector);
      }
      // falls through
   default:
      disableCount[irq]--;
      break;
   case 0:
      VMKLNX_WARN("enable_irq(%u) unbalanced from %p", irq,
		      __builtin_return_address(0));
      break;
   }

   vmk_SPUnlockIRQ(&irqMappingLock, prevIRQL);
}

void
disable_irq_nosync(unsigned int irq)
{
   uint32_t vector = irq;
   uint64_t prevIRQL;

   VMKLNX_DEBUG(1, "called for: %d", irq);

   prevIRQL = vmk_SPLockIRQ(&irqMappingLock);

   if (disableCount[irq]++ == 0) {
      if (vmk_VectorDisable(vector) != VMK_OK) {
         VMKLNX_WARN("Cannot disable vector %d", irq);
      }
   }

   vmk_SPUnlockIRQ(&irqMappingLock, prevIRQL);
}

/**                                          
 *  disable_irq - disable an irq and synchronously wait for completion
 *  @irq: The interrupt request line to be disabled.
 *                                           
 *  Disable the selected interrupt line.  Then wait for any in progress
 *  interrupts on this irq (possibly executing on other CPUs) to complete.  
 *
 *  Enables and Disables nest, in the sense that n disables are needed to
 *  counteract n enables.
 * 
 *  If you use this function while holding any resource the IRQ handler
 *  may need, you will deadlock.
 *                                           
 *  ESX Deviation Notes:                     
 *  This function should not fail in ordinary circumstances.  But if failure
 *  does occur, the errors are reported to the vmkernel log.
 *
 * RETURN VALUE:
 * None.
 */                                          
/* _VMKLNX_CODECHECK_: disable_irq */
void
disable_irq(unsigned int irq)
{
   uint32_t vector = irq;

   VMKLNX_DEBUG(1, "called for: %d", irq);
   disable_irq_nosync(irq);
   if (vmk_VectorSync(vector) != VMK_OK) {
      VMKLNX_WARN("Cannot sync vector %d", vector);
   }
}

/*
 * drivers want to only use the good version of synchronize_irq()
 * which takes a vector if the kernel is < KERNEL_VERSION(2,5,28)
 * but ours isn't, so we manually fix up the couple of places 
 * where we need to do this.
 */
/**                                          
 *  synchronize_irq - wait for pending IRQ handlers (on other CPUs) 
 *  @irq: irq number to be synchronized
 *                                           
 *  This function waits for any pending IRQ handlers for this interrupt to complete before returning. 
 *  If you use this function while holding a resource the IRQ handler may need you will deadlock. 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: synchronize_irq */
void
synchronize_irq(unsigned int irq)
{
   uint32_t vector = irq;

   /* 
    * Linux seems to bail out silently if the vector is zero or too high.
    */
   if (vector && vector < NR_IRQS) {
      if (vmk_VectorSync(vector) != VMK_OK) {
         VMKLNX_WARN("Cannot sync vector %d", vector);
      }
   }
}

/*
 *----------------------------------------------------------------------------
 *
 *  Linux_PollBH --
 *
 *    Bottom half to handle poll notification requests.
 *
 *  Results:
 *    None.
 *
 *  Side effects:
 *    A bunch of worlds may be woken up. 
 *    COS will get a notification.
 *
 *----------------------------------------------------------------------------
 */

void
Linux_PollBH(void *data)
{
   wait_queue_head_t *q = (wait_queue_head_t *) data;

   vmk_CharDevWakePollers(data);
   vmk_CharDevNotifyFasyncCompleteWithMinor(q->chardev.major,
                                            q->chardev.minor);
}

/*
 *----------------------------------------------------------------------
 *
 * copy_ {to,from,in} _user --
 *
 *      Copy to/from host from/to vmkernel/host.
 *
 *      For VMvisor, there is no COS.  Any user-level data is located in
 *      a user-world, whose memory is scoped in a different segment and
 *      which, therefore, requires a segment-overridden 'movs'.
 *
 *      user heap of the kernel. So, a memcpy will suffice.
 *
 * Results: 
 *      0 on success, 1 on failure.
 *
 * Side effects:
 *      Blocking call. 
 *
 *----------------------------------------------------------------------
 */

static inline unsigned long
__CopyOut(void *d, const void *s, unsigned long l)
{
   struct umem *umem;
   VMK_ReturnStatus status;

   if (__IsPtrVMKBuffer(d)) {
      void *vmkBuf = __SanitizeVMKBufferPtr(d);
      if (vmk_ContextGetCurrentType() == VMK_CONTEXT_TYPE_WORLD) {
	 status = vmk_WorldGetPrivateInfo(umem_key, (vmk_AddrCookie *)&umem);
	 if (status == VMK_OK &&
	     (vmk_VirtAddr) vmkBuf >= (vmk_VirtAddr) &umem->data[0] &&
	     (vmk_VirtAddr) vmkBuf + l <= (vmk_VirtAddr) &umem->data[umem->len]) {
	    memcpy(vmkBuf, s, l);
	    return 0;
	 }
      }
   }

   return (vmk_CopyToUser((vmk_VirtAddr)d, (vmk_VirtAddr)s, l) == VMK_OK)? 0 : 1;
}

static inline unsigned long
__CopyIn(void *d, const void *s, unsigned long l)
{
   struct umem *umem;
   VMK_ReturnStatus status;

   if (__IsPtrVMKBuffer(s)) {
      void *vmkBuf = __SanitizeVMKBufferPtr(s);
      if (vmk_ContextGetCurrentType() == VMK_CONTEXT_TYPE_WORLD) {
	 status = vmk_WorldGetPrivateInfo(umem_key, (vmk_AddrCookie *)&umem);
	 if (status == VMK_OK &&
	     (vmk_VirtAddr) vmkBuf >= (vmk_VirtAddr) &umem->data[0] &&
	     (vmk_VirtAddr) vmkBuf + l <= (vmk_VirtAddr) &umem->data[umem->len]) {
	    memcpy(d, vmkBuf, l);
	    return 0;
	 }
      }
   }

   return (vmk_CopyFromUser((vmk_VirtAddr)d, (vmk_VirtAddr)s, l) == VMK_OK)? 0 : 1;
}

/**                                          
 *  copy_to_user - copy a block of data into user space.
 *  @d: destination address, in kernel space.
 *  @s: source address, in user space.
 *  @l: number of bytes to copy.
 *                                           
 *  Copy data from kernel space to user space.
 *  User context only. This function may sleep.             
 *                                           
 *  RETURN VALUE:
 *  Returns number of bytes that could not be copied. On success, this will be zero.
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: copy_to_user */
unsigned long
copy_to_user(void *d, const void *s, unsigned long l)
{
   VMKLNX_DEBUG(5, "world=%d from=%p to=%p bytes=%lu", vmk_WorldGetID(),
                s, d, l); 

   return __CopyOut(d, s, l);
}

/**                                          
 *  copy_from_user - copy a block of data from user space
 *  @d: destination address, in kernel space.
 *  @s: source address, in user space.
 *  @l: number of bytes to copy.
 *                                           
 *  Copy data from user space to kernel space.
 *  If some data could not be copied, this function will pad the copied data
 *  to the requested size using zero bytes.
 *  User context only. This function may sleep.
 *                                           
 *  RETURN VALUE:     
 *  Returns number of bytes that could not be copied. On success, this will be zero.
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: copy_from_user */
unsigned long 
copy_from_user(void *d, const void *s, unsigned long l)
{
   VMKLNX_DEBUG(5, "world=%d from=%p to=%p bytes=%lu", vmk_WorldGetID(),
               s, d, l);

   return __CopyIn(d, s, l);
}

/**                                          
 *  copy_in_user - Copy a block from one user location to another.
 *  @d: Pointer to destination address
 *  @s: Pointer to source address
 *  @l: Number of bytes to copy
 *                                           
 *  Copy the data from user space location s to
 *  to user location d.
 *
 *  ESX Deviation Notes:
 *  This function can fail due to lack of kernel resources.
 *                                           
 *  RETURN VALUE:
 *  Returns 1 on error and 0 otherwise
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: copy_in_user */
unsigned long 
copy_in_user(void *d, const void *s, unsigned long l)
{
   unsigned long ret;
   void *ks;

   VMKLNX_DEBUG(5, "world=%d from=%p to=%p bytes=%lu", vmk_WorldGetID(),
                s, d, l);

   ks = vmk_HeapAlloc(VMK_MODULE_HEAP_ID, l);
   if (ks == NULL) {
      VMKLNX_DEBUG(3, "worldId = %d vmk_HeapAlloc failed", vmk_WorldGetID());
      return 1;
   }
   if (__CopyIn(ks, s, l) == 0 && __CopyOut(d, ks, l) == 0) {
      ret = 0;
   } else {
      ret = 1;
   }
   vmk_HeapFree(VMK_MODULE_HEAP_ID, ks);
   return ret;
}

/**
 * clear_user - write 0 into user space.
 * @mem: pointer of __user
 * @l: number of bytes to be zeroed
 *
 * Write 0 into user space.
 *
 * RETURN VALUE:
 * 0 on success, 1 on failure.
 *
 */
/* _VMKLNX_CODECHECK_: clear_user */
unsigned long
clear_user(void __user *mem, unsigned long l)
{
   char *cmem = mem;

   /*
    * 64 bytes of nulls is large enough as a source
    * of zeros in a __CopyOut() call to clear most
    * user buffers that are used as argument to ioctl
    * commands, but also small enough as a local array
    * on a kernel stack.
    */
   char nulls[64];

   VMKLNX_DEBUG(5, "world=%d mem=%p bytes=%lu", vmk_WorldGetID(), mem, l);

   memset(nulls, 0, sizeof(nulls));

   while (l) {
      unsigned long nbytes = min_t(unsigned long, l, sizeof(nulls));

      if (__CopyOut(cmem, nulls, nbytes) != 0) {
         return 1;
      }
      cmem += nbytes;
      l -= nbytes;
   }

  return 0;
}

void console_print(const char *str)
{
   VMKLNX_INFO("%s", (char *)str);
}

static vmk_Spinlock nbLock;
static struct notifier_block *nb_head;
static vmk_Bool nbNotifiersRunning = VMK_FALSE;

/*
 *----------------------------------------------------------------------
 *
 * register_reboot_notifier --
 *
 *      Register a reboot notifier
 *
 * Results: 
 *      0 on success, -1 on failure.
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */
/**                                          
 *  register_reboot_notifier - Register function to be called at reboot time
 *  @nb: Info about notifier function to be called
 *
 *  Registers a function with the list of functions to be called at reboot time
 *  Always returns 0.
 *
 */
/* _VMKLNX_CODECHECK_: register_reboot_notifier */
int register_reboot_notifier(struct notifier_block *nb)
{
   VMKLNX_DEBUG(0, "register reboot notifier %p", nb->notifier_call);
   VMK_ASSERT(nb->notifier_call != NULL);
   nb->modID = vmk_ModuleStackTop();
   vmk_SPLock(&nbLock);
   if (nbNotifiersRunning) {
      vmk_SPUnlock(&nbLock);
      return 0;
   }
   nb->next = nb_head;
   nb_head = nb;
   vmk_SPUnlock(&nbLock);
   return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * unregister_reboot_notifier --
 *
 *      Unregister a reboot notifier
 *
 * Results: 
 *      0 on success, -1 on failure.
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */
/**                                          
 *  unregister_reboot_notifier - Unregisters a previously registered reboot notifier function. 
 *  @nb: pointer to notifier_block
 *                                           
 *  Return value: 
 *   0 on success, -1 on failure.
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: unregister_reboot_notifier */
int unregister_reboot_notifier(struct notifier_block *nb)
{
   struct notifier_block *n, *np;

   VMKLNX_DEBUG(0, "unregister reboot notifier %p", nb->notifier_call);
   vmk_SPLock(&nbLock);
   if (nbNotifiersRunning) {
      vmk_SPUnlock(&nbLock);
      return 0;
   }
   n = nb_head;
   for (np = NULL; n; n = n->next) {
      if (n == nb) {
         if (np) {
            np->next = nb->next;
         } else {
            nb_head = nb->next;
         }
         vmk_SPUnlock(&nbLock);
         return 0;
      }
      np = n;
   }
   vmk_SPUnlock(&nbLock);
   return -1;
}

/*
 *----------------------------------------------------------------------
 *
 * RebootHandler --
 *
 *      Run registered reboot notification handlers.  Also call into
 *      LinuxPCI to run device shutdown methods; on native Linux that
 *      occurs right after the reboot notifiers are run.
 *
 * Results: 
 *      None.
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */
static void
RebootHandler(void *data)
{
   struct notifier_block *nb, *nbn;

   data = data;
   vmk_SPLock(&nbLock);
   /* once we're in here we don't let register/unregister do any work */
   VMK_ASSERT(nbNotifiersRunning == VMK_FALSE);
   nbNotifiersRunning = VMK_TRUE;
   nb = nb_head;
   vmk_SPUnlock(&nbLock);
   for (; nb; nb = nbn) {
      nbn = nb->next;
      VMKLNX_DEBUG(0, "running reboot notifier %p", nb->notifier_call);
      VMKAPI_MODULE_CALL_VOID(nb->modID, nb->notifier_call, nb, SYS_POWER_OFF, NULL);
   }
   LinuxPCI_Shutdown();
}

void __brelse(struct buffer_head *bh)
{
   VMKLNX_WARN("__brelse");
   VMKLNX_ASSERT_NOT_IMPLEMENTED(0);
}

void free_dma(unsigned int dmanr)
{
   VMKLNX_WARN("free_dma");
   VMKLNX_ASSERT_NOT_IMPLEMENTED(0);
}

int request_dma(unsigned int dmanr, const char * device_id)
{
   VMKLNX_WARN("request_dma");
   VMKLNX_ASSERT_NOT_IMPLEMENTED(0);
   return 1;
}

static int
LinuxPrintk(const char * fmt, va_list args)
{
   va_list argsCopy;
   int printedLen;

   va_copy(argsCopy, args);

   vmk_vLogNoLevel(VMK_LOG_URGENCY_NORMAL, fmt, args);
   printedLen = vmk_Vsnprintf(NULL, 0, fmt, argsCopy);

   va_end(argsCopy);
   va_end(args);

   return printedLen;
}

/**
 *  printk - print messages to the vmkernel log
 *  @fmt: the format string
 *
 *  printk is used to print a formatted message to the vmkernel logs from inside
 *  the drivers. 
 *
 *  ESX Deviation Notes:
 *  The priorities for Linux priority code string like KERN_DEBUG etc might be
 *  ignored and the messages will always have the same priority.
 *
 *  RETURN VALUE:
 *  Returns the number of characters written to the log.
 *
 *  SEE ALSO:
 *  printf
 *
 */
/* _VMKLNX_CODECHECK_: printk */
asmlinkage int printk(const char * fmt, ...)
{
   va_list args;
   va_start(args, fmt);
   return LinuxPrintk(fmt, args);
}

/**
 *  printf - print messages to the vmkernel log
 *  @fmt: the format string
 *
 *  Prints a formatted message to the vmkernel logs. This function is an alias to
 *  printk().
 *
 *  ESX Deviation Notes:
 *  The priorities for Linux priority code string like KERN_DEBUG etc might be
 *  ignored and the messages will always have the same priority.
 *
 *  RETURN VALUE:
 *  Returns the number of characters written to the log.
 *
 *  SEE ALSO:
 *  printk
 *
 */
/* _VMKLNX_CODECHECK_: printf */
asmlinkage int printf(const char * fmt, ...) __attribute__ ((alias ("printk") ));

/**
 *  scnprintf - format a string and place it in a buffer       
 *  @buf: the buffer to place the result into    
 *  @size: the size of the buffer, including the trailing null space
 *  @fmt: the format string to use
 *  @...: arguments for the format string
 *
 *  Format a string and place it in a buffer
 *
 *  RETURN VALUE:
 *  The return value is the number of characters written into @buf not including
 *  the trailing '\0'. If @size is <= 0 the function returns 0. If the return is
 *  greater than or equal to @size, the resulting string is truncated.
 */
/* _VMKLNX_CODECHECK_: scnprintf */
int scnprintf(char * buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vmk_Vsnprintf(buf, size, fmt, args);
	va_end(args);
	return (i >= size) ? (size - 1) : i;
}

void * best_memcpy(void * to, const void * from, size_t n)
{
   return memcpy(to, from, n);
}

void * best_memset(void * s, char c, size_t count)
{
   return memset(s, c, count);
}

static vmk_Bool
Linux_AddInfoInt(LinuxDevInfo *devInfo, void *data)
{
   LinuxDevHashEntry *he = (LinuxDevHashEntry *)vmk_HeapAlloc(VMK_MODULE_HEAP_ID, sizeof(LinuxDevHashEntry));
   if (he == NULL) {
      VMKLNX_WARN("No memory");
      return VMK_FALSE;
   } else {
      devInfo->refCount++;
      he->key = data;
      he->dev = devInfo;
      he->next = devHashTable[(vmk_VirtAddr)data & DEV_HASH_MASK];
      devHashTable[(vmk_VirtAddr)data & DEV_HASH_MASK] = he;
      return VMK_TRUE;      
   }

}

/* 
 * TODO: reddys - hack for libata IDE/PATA drivers
 * See Linux_RegisterDevice() for its purpose.
 */
VMK_ReturnStatus Linux_IdeRegisterIRQ(void *vmkAdapter,
                                      vmk_uint32 intrVector,
                                      vmk_InterruptHandler intrHandler,
                                      void *intrHandlerData)
{
   /* dummy function */
   return VMK_OK;
}

vmk_Bool
Linux_RegisterDevice(void *data, uint32_t dataLength, void *device, 
		     const char *name, LinuxRegisterIRQFunc irqRegisterFunc,
                     vmk_ModuleID moduleID)
{
   vmk_Bool status;
   LinuxDevInfo *devInfo;
   uint64_t prevIRQL, prevIMLIRQL;

   if (vmklinuxModID != 0 && vmk_ModuleIncUseCount(vmklinuxModID) != 0) {
      VMKLNX_WARN("Mod_IncUseCount failed!!!");
      return VMK_FALSE;
   }
   
   // Must grab the locks in this order.
   prevIMLIRQL = vmk_SPLockIRQ(&irqMappingLock);
   prevIRQL = vmk_SPLockIRQ(&devMappingLock);

   devInfo = Linux_FindDeviceInt(data, VMK_FALSE);
   if (devInfo != NULL) {
      Linux_ReleaseDeviceInt(devInfo);
      status = VMK_FALSE;
      VMKLNX_WARN("Device already registered @ %p => %p",
	      data, data + dataLength - 1);
   } else {
      devInfo = (LinuxDevInfo *)vmk_HeapAlloc(VMK_MODULE_HEAP_ID, sizeof(LinuxDevInfo));
      if (devInfo == NULL) {
	 status = VMK_FALSE;
	 VMKLNX_WARN("No memory");
      } else {
         IRQInfo *irqInfo;
         void *dev_id = data;

	 devInfo->device = device;
	 devInfo->dataStart = data;
	 devInfo->dataEnd = data + dataLength;
	 devInfo->dataLength = dataLength;
	 devInfo->name = name;
	 devInfo->moduleID = moduleID;
	 devInfo->refCount = 1;
	 devInfo->irqRegisterFunc = irqRegisterFunc;
         devInfo->vector = -1;

         /* TODO: reddys - remove post KL */
         if (irqRegisterFunc && irqRegisterFunc == Linux_IdeRegisterIRQ) {
            dev_id = NULL;
            devInfo->irqRegisterFunc = vmk_ScsiRegisterIRQ;
         }

	 VMKLNX_DEBUG(0, "Registering %s @ %p to %p", 
	     name, devInfo->dataStart, devInfo->dataEnd);

	 devInfo->next = deviceList;
	 deviceList = devInfo;

         // Was an irq requested before registering the device? 
         irqInfo = FindUnregisterDeviceIRQInfo(devInfo->moduleID, 
                                               dev_id, 
                                               NULL);
         if (irqInfo) {
            VMK_ASSERT(irqInfo->deviceInfo == NULL);
            irqInfo->deviceInfo = devInfo;
            devInfo->vector = irqInfo->irq;
               if (devInfo->irqRegisterFunc != NULL) {
                   devInfo->irqRegisterFunc(devInfo->device,
                                            devInfo->vector, 
                                            Linux_IRQHandler,
                                            irqInfo);
               }
         }

	 status = Linux_AddInfoInt(devInfo, data);
	 if (!status) {
	    vmk_HeapFree(VMK_MODULE_HEAP_ID, devInfo);
	 }
      }
   }

   vmk_SPUnlockIRQ(&devMappingLock, prevIRQL);
   vmk_SPUnlockIRQ(&irqMappingLock, prevIMLIRQL);
      

   if (!status) {
      vmk_ModuleDecUseCount(vmklinuxModID);
   }

   return status;
}

vmk_Bool
Linux_UnregisterDevice(void *data, void *device)
{
   vmk_Bool status = VMK_TRUE;
   LinuxDevInfo *dev;
   uint64_t prevIRQL = vmk_SPLockIRQ(&devMappingLock);

   dev = Linux_FindDeviceInt(data, VMK_FALSE);
   if (dev == NULL) {
      VMKLNX_DEBUG(0, "Couldn't find device with data %p", data);
      status = VMK_FALSE;
   } else {
      LinuxDevInfo *curDev, *prev;
      int i;

      for (i = 0; i < DEV_HASH_TABLE_SIZE; i++) {
	 while (1) {
	    LinuxDevHashEntry *he, *prev;	 
	    for (prev = NULL, he = devHashTable[i]; 
	         he != NULL && he->dev != dev;
		 prev = he, he = he->next) {
	    }
	    if (he == NULL) {
	       break;
	    } else {
	       if (prev == NULL) {
		  devHashTable[i] = he->next;
	       } else {
		  prev->next = he->next;
	       }
	       VMK_ASSERT(dev->refCount >= 1);
	       dev->refCount--;
	       vmk_HeapFree(VMK_MODULE_HEAP_ID, he);
	    }
	 }
      }

      /*
       * There is one reference since the device was registered and 
       * one reference from Linux_FindDeviceInt above.
       */
      if (dev->refCount != 2) {
	 VMKLNX_WARN("Device reference count is %d", dev->refCount);
	 status = VMK_FALSE;
      } else {
	 for (prev = NULL, curDev = deviceList; 
	      curDev != NULL && curDev != dev; 
	      prev = curDev, curDev = curDev->next) {
	 }
	 if (curDev == NULL) {
	    VMKLNX_WARN("Couldn't find device in list");
	 } else {
	    if (prev == NULL) {
	       deviceList = dev->next;
	    } else {
	       prev->next = dev->next;
	    }
	 }

	 dev->refCount = 1;

	 status = VMK_TRUE;
      }

      Linux_ReleaseDeviceInt(dev);      
   }

   vmk_SPUnlockIRQ(&devMappingLock, prevIRQL);

   return status;
}

static LinuxDevInfo *
Linux_FindDeviceFast(void *data)
{
   LinuxDevHashEntry *he;

   VMK_ASSERT(vmk_SPIsLockedIRQ(&devMappingLock));

   for (he = devHashTable[(vmk_VirtAddr)data & DEV_HASH_MASK];
        he != NULL && data != he->key;
	he = he->next) {
   }

   if (he != NULL) {
      he->dev->refCount++;
      VMKLNX_DEBUG(2, "dev=%p refCount=%d", he->dev, he->dev->refCount);
      return he->dev;
   } else {
      VMKLNX_DEBUG(2, "Couldn't find device=%p", data);
      return NULL;
   }
}

static LinuxDevInfo *
Linux_FindDeviceInt(void *data, vmk_Bool addIt)
{
   LinuxDevInfo *dev = Linux_FindDeviceFast(data);
   VMK_ASSERT(vmk_SPIsLockedIRQ(&devMappingLock));
   if (dev == NULL) {
      for (dev = deviceList; dev != NULL; dev = dev->next) {
	 if (data >= dev->dataStart && data < dev->dataEnd) {
	    break;
	 }
      }
      if (dev != NULL) {
	 if (addIt) {
	    Linux_AddInfoInt(dev, data);
	 }
	 dev->refCount++;
	 VMKLNX_DEBUG(2, "dev=%p refCount=%d", dev, dev->refCount);
      }
   }

   return dev;
}

LinuxDevInfo *
Linux_FindDevice(void *data, vmk_Bool addIt)
{
   LinuxDevInfo *dev;

   uint64_t prevIRQL = vmk_SPLockIRQ(&devMappingLock);

   dev = Linux_FindDeviceInt(data, addIt);

   vmk_SPUnlockIRQ(&devMappingLock, prevIRQL);

   return dev;
}

void
Linux_ReleaseDevice(LinuxDevInfo *dev)
{
   uint64_t prevIRQL = vmk_SPLockIRQ(&devMappingLock);

   VMKLNX_DEBUG(2, "from %p", __builtin_return_address(0));
   Linux_ReleaseDeviceInt(dev);

   vmk_SPUnlockIRQ(&devMappingLock, prevIRQL);
}

static void
Linux_ReleaseDeviceInt(LinuxDevInfo *dev)
{
   VMK_ASSERT(dev->refCount >= 1);
   VMK_ASSERT(vmk_SPIsLockedIRQ(&devMappingLock));

   dev->refCount--;
   VMKLNX_DEBUG(2, "dev=%p refCount=%d", dev, dev->refCount);
   if (dev->refCount == 0) {
      vmk_HeapFree(VMK_MODULE_HEAP_ID, dev);
      vmk_ModuleDecUseCount(vmklinuxModID);
   }
}

void
Linux_BH(void (*routine)(void *), void *data) 
{
   vmk_ModuleID modID;

   modID = vmk_ModuleStackTop();
   VMK_ASSERT(modID != VMK_INVALID_MODULE_ID);
   Linux_BHInternal(routine, data, modID);
}

LinuxBHData *
Linux_GetBHList(void)
{
   return vmkLinuxPCPU[vmk_GetPCPUNum()].linuxBHList;
}

void
Linux_SetBHList(LinuxBHData *list)
{
   vmkLinuxPCPU[vmk_GetPCPUNum()].linuxBHList = list;
}

void
Linux_BHInternal(void (*routine)(void *), void *data, vmk_ModuleID modID) 
{
   LinuxBHData *d = (LinuxBHData *)vmk_HeapAlloc(VMK_MODULE_HEAP_ID, sizeof(LinuxBHData));

/*   ASSERT_NO_INTERRUPTS(); */

   if (d == NULL) {
      VMKLNX_WARN("Couldn't allocate memory");
   } else {
      vmk_CPUFlags eflags;

      eflags = vmk_CPUGetFlags();
      vmk_CPUDisableInterrupts();
      d->routine = routine;
      d->data = data;
      d->next = Linux_GetBHList();
      d->modID = modID;
      d->staticAlloc = VMK_FALSE;
      Linux_SetBHList(d);
      vmk_BottomHalfSchedulePCPU(linuxBHNum, vmk_GetPCPUNum()); /* schedule Linux_BHHandler for this PCPU */
      vmk_CPUSetFlags(eflags);
   }
}

void
Linux_BHInternal_static(LinuxBHData *d, vmk_ModuleID modID)
{
   vmk_CPUFlags eflags;

   VMK_ASSERT(d);
/*   ASSERT_NO_INTERRUPTS(); */
   VMK_ASSERT(d->routine);
   VMK_ASSERT(d->staticAlloc);

   eflags = vmk_CPUGetFlags();
   vmk_CPUDisableInterrupts();
   d->next = Linux_GetBHList();
   d->modID = modID;
   Linux_SetBHList(d);
   vmk_BottomHalfSchedulePCPU(linuxBHNum, vmk_GetPCPUNum()); /* schedule LinuxBHHandler for this PCPU */
   vmk_CPUSetFlags(eflags);
}

void
Linux_BH_static(void (*routine)(void *), void *linuxBHdata) 
{
   vmk_ModuleID modID;

   modID = vmk_ModuleStackTop();
   VMK_ASSERT(modID != VMK_INVALID_MODULE_ID);
   Linux_BHInternal_static((LinuxBHData *)linuxBHdata, modID);
}

void
Linux_BHHandler(void *clientData)
{
   LinuxBHData *d;

   if (clientData != LINUX_BHHANDLER_NO_IRQS) {
      VMK_ASSERT_CPU_HAS_INTS_ENABLED();
      vmk_CPUDisableInterrupts();
   }
   d = Linux_GetBHList();
   Linux_SetBHList(NULL);
   if (clientData != LINUX_BHHANDLER_NO_IRQS) {
      vmk_CPUEnableInterrupts();
   }
   while (d != NULL) {
      LinuxBHData *next = d->next;
      VMKAPI_MODULE_CALL_VOID(d->modID, d->routine, d->data);
      if (!d->staticAlloc) {
         vmk_HeapFree(VMK_MODULE_HEAP_ID, d);
      }
      d = next;
   }
}


/**
 * do_gettimeofday - Gets the time in seconds since 1970
 * @tv: pointer to struct timeval where the time is returned
 *
 * Fills in the timeval struct with time in seconds since 1970.
 *
 * RETURN VALUE:
 * NONE
 *
 */
/* _VMKLNX_CODECHECK_: do_gettimeofday */
void
do_gettimeofday(struct timeval *tv)
{
   vmk_TimeVal vmktv;
   vmk_GetTimeOfDay(&vmktv);
   tv->tv_sec = vmktv.sec;
   tv->tv_usec = vmktv.usec;
}

// return value is only used as argument to probe_irq_off
unsigned long probe_irq_on(void)
{
   return 0x12345678;
}

/* Should return the irq that received ints between irq_on and irq_off,
 * but we only return 0, and ide-probe manually sets the IRQ value in
 * try_to_identify(line 243) */
int probe_irq_off(unsigned long unused)
{
   return 0;
}

/* These functions are supposed to enable and disable hard_idle:hlt,
 * noop for vmkernel */
void disable_hlt(void)
{
}
void enable_hlt(void)
{
}

/**
 *	get_option - Parse integer from an option string
 *	@str: option string
 *	@pint: (output) integer value parsed from @str
 *
 *	Read an int from an option string; if available accept a subsequent
 *	comma as well.
 *
 *	Return values:
 *	0 : no int in string
 *	1 : int found, no subsequent comma
 *	2 : int found including a subsequent comma
 */

int get_option (char **str, int *pint)
{
	char *cur = *str;

	if (!cur || !(*cur))
		return 0;
	*pint = simple_strtol (cur, str, 0);
	if (cur == *str)
		return 0;
	if (**str == ',') {
		(*str)++;
		return 2;
	}

	return 1;
}

/**
 *	get_options - Parse a string into a list of integers
 *	@str: String to be parsed
 *	@nints: size of integer array
 *	@ints: integer array
 *
 *	This function parses a string containing a comma-separated
 *	list of integers.  The parse halts when the array is
 *	full, or when no more numbers can be retrieved from the
 *	string.
 *
 *	Return value is the character in the string which caused
 *	the parse to end (typically a null terminator, if @str is
 *	completely parseable).
 */
 
char *get_options (const char *str, int nints, int *ints)
{
	int res, i = 1;
	char *_str = (char *)str;

	while (i < nints) {
		res = get_option (&_str, ints + i);
		if (res == 0)
			break;
		i++;
		if (res == 1)
			break;
	}
	ints[0] = i - 1;
	return (_str);
}


void do_BUG(const char* file, int line)
{
   vmk_Panic("do_BUG: %s:%d", file, line);         
}


/*
 * Wrapper to conform to the function prototype expected by Linux_BH.
 */
static void
Linux_Softirq(void *unused)
{
   do_softirq();
}

/*
 *----------------------------------------------------------------------
 *
 * init_vmkLinuxPCPU --
 *
 *     initialize the structures in vmkLinuxPCPU for PCPUs that are 
 *     present.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */
static void
init_vmkLinuxPCPU(uint32_t numPCPUs)
{
   int i;

   // if the following ASSERT fires, adjust PER_PCPU_VMKLINUX_DATA_CACHE_LINES
   VMK_ASSERT_ON_COMPILE(sizeof(vmkLinuxPCPU_t) 
                         == (PER_PCPU_VMKLINUX_DATA_CACHE_LINES*L1_CACHE_BYTES));
   VMK_ASSERT(numPCPUs <= NR_CPUS);

   VMKLNX_DEBUG(0, "vmk_NumPCPUs %d &vmkLinuxPCPU[0]: %p; &vmkLinuxPCPU[1]: %p", 
       numPCPUs, &vmkLinuxPCPU[0], &vmkLinuxPCPU[1]);
       
   // double check cache-line alignment
   VMK_ASSERT((((vmk_VirtAddr)&vmkLinuxPCPU[0]) % L1_CACHE_BYTES) == 0);
   VMK_ASSERT((((vmk_VirtAddr)&vmkLinuxPCPU[1]) % L1_CACHE_BYTES) == 0);

   if ((((vmk_VirtAddr)&vmkLinuxPCPU[0]) % L1_CACHE_BYTES) != 0) {
      VMKLNX_WARN("vmkLinuxPCPU[0] %p not cache-line boundary", &vmkLinuxPCPU[0]);
   }
   if ((((vmk_VirtAddr)&vmkLinuxPCPU[1]) % L1_CACHE_BYTES) != 0) {
      VMKLNX_WARN("vmkLinuxPCPU[1] %p not cache-line boundary", &vmkLinuxPCPU[1]);
   }

   // now initialize the softirqLinuxBHData data structure.
   for (i = 0; i < numPCPUs; i++) {
      vmkLinuxPCPU[i].softirqLinuxBHData.routine = Linux_Softirq;
      vmkLinuxPCPU[i].softirqLinuxBHData.data = NULL;
      // modID is updated dynamically.
      vmkLinuxPCPU[i].softirqLinuxBHData.modID = VMK_INVALID_MODULE_ID;
      vmkLinuxPCPU[i].softirqLinuxBHData.staticAlloc = VMK_TRUE;
   }

   // NULL out the remainder that should not be used.
   for ( ; i < NR_CPUS; i++) {
      vmkLinuxPCPU[i].softirqLinuxBHData.routine = NULL;
      vmkLinuxPCPU[i].softirqLinuxBHData.data = NULL;
      // modID is updated dynamically.
      vmkLinuxPCPU[i].softirqLinuxBHData.modID = VMK_INVALID_MODULE_ID;
      vmkLinuxPCPU[i].softirqLinuxBHData.staticAlloc = VMK_TRUE;
   }
}

// Keep these at the end 
int
init_module(void)
{
   VMK_ReturnStatus status;
   vmklinuxModID = vmklnx_this_module_id;

   /* 
    * Register our Log details
    */
   status = vmk_LogRegister(VMKLINUX26_NAME, vmklinuxModID, 0, NULL,
                            &vmklinux26Log);
                                                                                
   if (status != VMK_OK) {
      VMKLNX_WARN("vmklinux26: init_module: vmk_LogRegister failed: %s", 
         vmk_StatusToString(status));
      goto log_register_error;
   }
   vmk_LogSetCurrentLogLevel(vmklinux26Log, 1);
   VMKLNX_INFO("vmklinux26 module load starting...");

   VMK_ASSERT_ON_COMPILE(HZ == VMK_JIFFIES_PER_SECOND);

   is_vmvisor = (vmk_SystemGetHostType() == VMK_SYSTEM_HOST_TYPE_VISOR);

   smp_num_cpus = vmk_NumPCPUs();
   init_vmkLinuxPCPU(vmk_NumPCPUs());

   status = vmk_SPCreateIRQ(&regionLock, vmklinuxModID, "regionLck", NULL,
                            VMK_SP_RANK_IRQ_MEMTIMER);
   VMK_ASSERT(status == VMK_OK);

   status = vmk_SPCreateIRQ(&devMappingLock, vmklinuxModID, "deviceMapLck",
                            NULL, VMK_SP_RANK_IRQ_MEMTIMER-1);
   VMK_ASSERT(status == VMK_OK);

   status = vmk_SPCreateIRQ(&irqMappingLock, vmklinuxModID, "irqMapLck", NULL,
                            VMK_SP_RANK_IRQ_MEMTIMER-2);//depends on 
                                                        //devmappinglock
   VMK_ASSERT(status == VMK_OK);

   status = vmk_SPCreate(&nbLock, vmklinuxModID, "nbLock", NULL,
                         VMK_SP_RANK_LEAF);
   VMK_ASSERT(status == VMK_OK);

   
   status = vmk_SemaCreate(&pci_bus_sem, vmklinuxModID, "PCI_BUS_SEMA", 1);
   if (status != VMK_OK) {
      VMKLNX_WARN("vmklinux26: init_module: mk_SemaCreate for pci_bus_sem failed: %s", 
                  vmk_StatusToString(status));
      goto undo_lock_log_init;
   }

   status = vmk_ConfigParamOpen("Disk", "DumpPollMaxRetries",
                                &dumpPollRetriesHandle);
   VMK_ASSERT(status == VMK_OK);
   status = vmk_ConfigParamOpen("Disk", "DumpPollDelay",
                                &dumpPollDelayHandle);
   VMK_ASSERT(status == VMK_OK);

   if (LinuxWorkQueue_Init() < 0) {
      VMKLNX_WARN("vmklinux26: init_module: LinuxWorkQueue_Init failed");
      goto undo_lock_log_init;
   }

   driver_init();

   LinuxTask_Init();
   LinuxKthread_Init();
   umem_init();
   LinuxProc_Init();
   LinuxPCI_Init();
   LinuxDMA_Init();
   LinNet_Init();
   IoatLinux_Init();
   SCSILinux_Init();
   LinuxUSB_Init();
   BlockLinux_Init();
   LinuxChar_Init();
   softirq_init();

   dmi_scan_machine();

   if (vmk_RegisterRebootHandler(RebootHandler, NULL) != VMK_OK) {
      goto undo_bunch_init;
   }

   status = vmk_BottomHalfRegister(Linux_BHHandler, NULL, &linuxBHNum, "linuxbh");
   if (status != VMK_OK) {
      goto undo_reboot_register;
   }

   if (input_init())
      goto undo_bh_register;

   if (hid_init())
      goto undo_input_init;

   if (lnx_kbd_init())
      goto undo_hid_init;

   if (mousedev_init())
      goto undo_hid_init;

   status = vmk_HeapCreate(VMKLNX_VMALLOC_HEAP,
                           vmklnx_vmalloc_heap_min,
                           vmklnx_vmalloc_heap_max,
                           VMK_HEAP_PHYS_ANY_CONTIGUITY,
                           VMK_HEAP_ANY_MEM,
                           &vmklnxVmallocHeap);
   if (status != VMK_OK) {
      VMKLNX_WARN("vmk_HeapCreate for __vmalloc failed with error: %s",
                  vmk_StatusToString(status));
      goto undo_hid_init;
   }

   if (init_pagevirt_cache()) {
      goto undo_hid_init;
   }

   VMKLNX_INFO("vmklinux26 module load successful.");
   return 0;

undo_hid_init:
   hid_exit();
undo_input_init:
   input_exit();
undo_bh_register:
   vmk_BottomHalfUnregister(linuxBHNum);
undo_reboot_register:
   vmk_UnregisterRebootHandler(RebootHandler);
undo_bunch_init:
   LinuxPCI_Cleanup();
   LinNet_Cleanup();
   SCSILinux_Cleanup();
   LinuxUSB_Cleanup();
   BlockLinux_Cleanup();
   LinuxChar_Cleanup();
   LinuxProc_Cleanup();
   umem_deinit();
   LinuxKthread_Cleanup();
   LinuxTask_Cleanup();
   LinuxWorkQueue_Cleanup();

   vmk_ConfigParamClose(dumpPollDelayHandle);
   vmk_ConfigParamClose(dumpPollRetriesHandle);

   vmk_SemaDestroy(&pci_bus_sem);

undo_lock_log_init:
   vmk_SPDestroyIRQ(&irqMappingLock);
   vmk_SPDestroyIRQ(&regionLock);
   vmk_SPDestroyIRQ(&devMappingLock);
   vmk_SPDestroy(&nbLock);
   vmk_LogUnregister(&vmklinux26Log);

log_register_error:
   return -1;
}

void
cleanup_module(void)
{
   kmem_cache_destroy(pagevirt_cache);
   mousedev_exit();
   hid_exit();
   input_exit();
   // kill kernel thread
   vmk_UnregisterRebootHandler(RebootHandler);

   LinuxPCI_Cleanup();
#if defined(__x86_64__)
   LinuxDMA_Cleanup();
#endif /* defined(__x86_64__) */
   LinNet_Cleanup();
   SCSILinux_Cleanup();
   BlockLinux_Cleanup();
   LinuxChar_Cleanup();
   LinuxProc_Cleanup();
   umem_deinit();
   LinuxKthread_Cleanup();
   LinuxTask_Cleanup();
   LinuxWorkQueue_Cleanup();

   vmk_SPDestroyIRQ(&irqMappingLock);
   vmk_SPDestroyIRQ(&regionLock);
   vmk_SPDestroyIRQ(&devMappingLock);
   vmk_SPDestroy(&nbLock);
   vmk_SemaDestroy(&pci_bus_sem);
   vmk_LogUnregister(&vmklinux26Log);
}


/**
 *  virt_to_phys - returns the physical address mapped to the virtual                                          
 *  @address: the virtual address
 *                                           
 *  Gets the machine address mapped to the virtual address.
 *
 *  RETURN VALUE:
 *  Returns the physical(machine) address
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: virt_to_phys */
unsigned long
virt_to_phys(void *address)
{
   vmk_MachAddr maddr;

   (void) vmk_VirtAddrToMachAddr((vmk_VirtAddr)address, &maddr);
   return maddr;
}

/**
 *  phys_to_virt - map physical address to virtual
 *  @address: the machine address
 *
 *  Maps the machine address @address to its corresponding virtual address.
 *
 *  ESX Deviation Notes:
 *  This function returns "a" virtual address which can be used by the caller to
 *  access the physical memory. If the physical address is already mapped to a
 *  virtual address prior to the call by a different mechanism e.g., module
 *  loader, this function may not return the same virtual address. In other
 *  words:
 *  
 *  phys_to_virt(virt_tophys(&buf)) != &buf;
 *
 *  Do not try to free the memory using the virtual address returned by this
 *  function.
 *
 *  RETURN VALUE:
 *  Returns "a" virtual address which can be used by the caller to access the
 *  physical memory.
 *
 */
/* _VMKLNX_CODECHECK_: phys_to_virt */
void *phys_to_virt(unsigned long address)
{
   vmk_VirtAddr vaddr;

   (void) vmk_MachAddrToVirtAddr((vmk_MachAddr)address, &vaddr);
   return (void*) vaddr;
}

/*
 *----------------------------------------------------------------------
 *
 * vmklnx_CheckModuleVersion --
 *
 *      Check that the calling driver and vmkernel are compatible.
 *      Version number in vmklinux_dist.h.
 *
 * Results: 
 *      VMKLNX_VERSION_OK if version checks ok, 
 *      VMKLNX_VERSION_NOT_SUPPORTED if version major numbers differ (fatal error).
 *      VMKLNX_VERSION_MINOR_NOT_MATCHED if version minor numbers differ (non-fatal).	
 *
 * Side effects:
 *      None. 
 *
 *----------------------------------------------------------------------
 */

int
vmklnx_CheckModuleVersion(uint32_t drvAPIVersion, char *modName) // IN: driver version
{
   int module_major    = VMKLNX_API_VERSION_MAJOR(drvAPIVersion);
   int module_minor    = VMKLNX_API_VERSION_MINOR(drvAPIVersion);
   int vmklinux_major  = VMKLNX_API_VERSION_MAJOR(VMKLNX_API_VERSION);
   int vmklinux_minor  = VMKLNX_API_VERSION_MINOR(VMKLNX_API_VERSION);
   int majorDelta      = vmklinux_major - module_major;
   int minorDelta      = vmklinux_minor - module_minor;
   char *error_message = "Attempt to load %s driver (%s): "
                         "driver API version: %d.%d, "
                         "vmklinux API version: %d.%d.";

   if (majorDelta > 0) {
      vmk_WarningMessage(error_message,
                         "an obsolete",
                         modName, 
                         module_major, module_minor,
                         vmklinux_major, vmklinux_minor);
      return VMKLNX_VERSION_NOT_SUPPORTED;
   }

   if (majorDelta < 0 || (majorDelta == 0 && minorDelta < 0)) {
      vmk_WarningMessage(error_message,
                         "a too recent",
                         modName, 
                         module_major, module_minor,
                         vmklinux_major, vmklinux_minor);
      return VMKLNX_VERSION_NOT_SUPPORTED;
   }

   if (minorDelta > 0) {
      return VMKLNX_VERSION_MINOR_NOT_MATCHED;
   }
   
   return VMKLNX_VERSION_OK;
}

/*
 * A BUG() call in an inline function in a header should be avoided,
 * because it can seriously bloat the kernel.  So here we have
 * helper functions.
 * We lose the BUG()-time file-and-line info this way, but it's
 * usually not very useful from an inline anyway.  The backtrace
 * tells us what we want to know.
 */

void __out_of_line_bug(int line)
{
	printk("kernel BUG in header file at line %d\n", line);

	BUG();

	/* Satisfy __attribute__((noreturn)) */
	for ( ; ; )
		;
}

/*
 *  get_random_bytes is hooked by the random driver but ESX also makes
 *  get_random_bytes_int available to VMkernel as a source of purely
 *  software entropy
 */
static VMK_ReturnStatus
get_random_bytes_int(void *buf, int nbytes, int *bytesReturned)
{
   static uint32_t seed;
   uint32_t rand=0;

   if (seed == 0) {
      seed = vmk_GetRandSeed();
   }

   while (nbytes >= sizeof(uint32_t)) {
      rand = vmk_Rand(seed);
      seed = rand;
      *(uint32_t *)buf = rand;
      nbytes -= sizeof(uint32_t);
      buf += sizeof(uint32_t);
   }

   if (nbytes > 0) {
      VMK_ASSERT(nbytes <= 3);
      rand = vmk_Rand(seed);
      seed = rand;
      memcpy (buf, (unsigned char*)&rand, nbytes);
   }

   //XXX This return value is tossed by vmklinux26 but is visible to VMkernel
   return VMK_OK;
}
 

/**                                          
 *  get_random_bytes - Gets the requested number of random bytes
 *  @buf: Specifies the address of the buffer in which the requested bytes are stored
 *  @nbytes: Specifies the number of random bytes
 *                                           
 *  Returns the requested number of random bytes in the supplied buffer.
 *                                           
 *  RETURN VALUE:
 *  NONE 
 */                                          
/* _VMKLNX_CODECHECK_: get_random_bytes */
void 
get_random_bytes(void *buf, int nbytes)
{
   int  bytesReturned;

   if (HWRNGget_random_bytes != NULL) {
      HWRNGget_random_bytes(buf, nbytes, &bytesReturned);
      return;
   }

   get_random_bytes_int(buf, nbytes, &bytesReturned);
}


void (*vmklnx_add_keyboard_randomness)(int) = NULL;
void (*vmklnx_add_mouse_randomness)(int) = NULL;
void (*vmklnx_add_storage_randomness)(int) = NULL;

unsigned int
vmklnx_register_add_randomness_functions(
   void (*add_interrupt_randomness)(int),
   void (*add_hardware_rng_device_randomness)(int),
   void (*add_keyboard_randomness)(int),
   void (*add_mouse_randomness)(int),
   void (*add_hid_other_randomness)(int),
   void (*add_storage_randomness)(int))
{
   vmk_ModuleID modID = vmk_ModuleStackTop();
   unsigned int status = 0;

#define VMKLNX_REGISTER_ADD_ENTROPY_FCT(function, source)		       \
   if ((function) &&							       \
       (vmk_RegisterAddEntropyFunction(modID, (function), (source)) != VMK_OK))\
      status |= 1 << (source)

#define VMKLNX_REGISTER_ADD_ENTROPY_FCT_W_VMKLNX_COPY(function, source)	\
   if (function) {							\
      vmklnx_##function = function;					\
      VMKLNX_REGISTER_ADD_ENTROPY_FCT(function, source);		\
   }


   VMKLNX_REGISTER_ADD_ENTROPY_FCT(add_interrupt_randomness,
				   VMK_ENTROPY_HARDWARE_INTERRUPT);
   VMKLNX_REGISTER_ADD_ENTROPY_FCT(add_hardware_rng_device_randomness,
				   VMK_ENTROPY_HARDWARE_RNG);
   VMKLNX_REGISTER_ADD_ENTROPY_FCT_W_VMKLNX_COPY(add_keyboard_randomness,
						 VMK_ENTROPY_KEYBOARD);
   VMKLNX_REGISTER_ADD_ENTROPY_FCT_W_VMKLNX_COPY(add_mouse_randomness,
						 VMK_ENTROPY_MOUSE);
   VMKLNX_REGISTER_ADD_ENTROPY_FCT(add_hid_other_randomness,
				   VMK_ENTROPY_OTHER_HID);
   VMKLNX_REGISTER_ADD_ENTROPY_FCT_W_VMKLNX_COPY(add_storage_randomness,
						 VMK_ENTROPY_STORAGE);

#undef VMKLNX_REGISTER_ADD_ENTROPY_FCT
#undef VMKLNX_REGISTER_ADD_ENTROPY_FCT_W_VMKLNX_COPY

   addRandomnessModuleID = modID;

   return modID;
}


unsigned int
vmklnx_unregister_add_randomness_functions(
   void (*add_interrupt_randomness)(int),
   void (*add_hardware_rng_device_randomness)(int),
   void (*add_keyboard_randomness)(int),
   void (*add_mouse_randomness)(int),
   void (*add_hid_other_randomness)(int),
   void (*add_storage_randomness)(int),
   int modID)
{
   unsigned int status = 0;

   if (modID != addRandomnessModuleID) {
      return VMK_BAD_PARAM;
   }

#define VMKLNX_UNREGISTER_ADD_ENTROPY_FCT(function)			\
   if ((function) &&							\
       (vmk_UnregisterAddEntropyFunction(modID, (function)) != VMK_OK))	\
      status = 1

#define VMKLNX_UNREGISTER_ADD_ENTROPY_FCT_W_VMKLNX_COPY(function)	\
   if (function) {							\
      vmklnx_##function = NULL;						\
      VMKLNX_UNREGISTER_ADD_ENTROPY_FCT(function);			\
   }

   VMKLNX_UNREGISTER_ADD_ENTROPY_FCT(add_interrupt_randomness);
   VMKLNX_UNREGISTER_ADD_ENTROPY_FCT(add_hardware_rng_device_randomness);
   VMKLNX_UNREGISTER_ADD_ENTROPY_FCT_W_VMKLNX_COPY(add_keyboard_randomness);
   VMKLNX_UNREGISTER_ADD_ENTROPY_FCT_W_VMKLNX_COPY(add_mouse_randomness);
   VMKLNX_UNREGISTER_ADD_ENTROPY_FCT(add_hid_other_randomness);
   VMKLNX_UNREGISTER_ADD_ENTROPY_FCT_W_VMKLNX_COPY(add_storage_randomness);

#undef VMKLNX_UNREGISTER_ADD_ENTROPY_FCT
#undef VMKLNX_UNREGISTER_ADD_ENTROPY_FCT_W_VMKLNX_COPY

   addRandomnessModuleID = 0;

   return status;
}


unsigned int
vmklnx_register_get_random_byte_functions(
   vmk_GetEntropyFunction get_hardware_random_bytes,
   vmk_GetEntropyFunction get_hardware_non_blocking_random_bytes,
   vmk_GetEntropyFunction get_software_random_bytes,
   vmk_GetEntropyFunction get_software_only_random_bytes)
{
   vmk_ModuleID modID = vmk_ModuleStackTop();
   unsigned int status = 0;

   if (addRandomnessModuleID) {
      modID = addRandomnessModuleID;
   }

#define VMKLNX_REGISTER_GET_ENTROPY_FUNCTION(function, type)		      \
   if ((function) &&							      \
       (vmk_RegisterGetEntropyFunction(modID, (function), (type)) != VMK_OK)) \
      status |= 1 << (type)

   VMKLNX_REGISTER_GET_ENTROPY_FUNCTION(get_hardware_random_bytes,
					VMK_ENTROPY_HARDWARE);
   VMKLNX_REGISTER_GET_ENTROPY_FUNCTION(get_hardware_non_blocking_random_bytes,
					VMK_ENTROPY_HARDWARE_NON_BLOCKING);
   VMKLNX_REGISTER_GET_ENTROPY_FUNCTION(get_software_random_bytes,
					VMK_ENTROPY_SOFTWARE);
   if (get_software_only_random_bytes) {
      VMKLNX_REGISTER_GET_ENTROPY_FUNCTION(get_software_only_random_bytes,
					   VMK_ENTROPY_SOFTWARE_ONLY);
   } else {
      if (vmk_RegisterGetEntropyFunction(modID, get_random_bytes_int,
                                         VMK_ENTROPY_SOFTWARE_ONLY)) {
         status |= 1 << VMK_ENTROPY_SOFTWARE_ONLY;
      }
   }

#undef VMKLNX_REGISTER_GET_ENTROPY_FUNCTION

   getRandomBytesModuleID = modID;

   return modID;
}


unsigned int
vmklnx_unregister_get_random_byte_functions(
   vmk_GetEntropyFunction get_hardware_random_bytes,
   vmk_GetEntropyFunction get_hardware_non_blocking_random_bytes,
   vmk_GetEntropyFunction get_software_random_bytes,
   vmk_GetEntropyFunction get_software_only_random_bytes,
   int modID)
{
   unsigned int status = 0;

   if (modID != getRandomBytesModuleID) {
      return VMK_BAD_PARAM;
   }

#define VMKLNX_UNREGISTER_GET_ENTROPY_FUNCTION(function)		\
   if ((function) &&							\
       (vmk_UnregisterGetEntropyFunction(modID, (function)) != VMK_OK))	\
      status = 1

   VMKLNX_UNREGISTER_GET_ENTROPY_FUNCTION(get_hardware_random_bytes);
   VMKLNX_UNREGISTER_GET_ENTROPY_FUNCTION(get_hardware_non_blocking_random_bytes);
   VMKLNX_UNREGISTER_GET_ENTROPY_FUNCTION(get_software_random_bytes);
   if (get_software_only_random_bytes) {
      VMKLNX_UNREGISTER_GET_ENTROPY_FUNCTION(get_software_only_random_bytes);
   } else {
      if (vmk_UnregisterGetEntropyFunction(modID, get_random_bytes_int)) {
         status = 1;
      }
   }

#undef VMKLNX_UNREGISTER_GET_ENTROPY_FUNCTION

   getRandomBytesModuleID = 0;

   return status;
}


/*
 *----------------------------------------------------------------------
 *
 * vmklnx_get_driver_module_id --
 *
 *      Returns the module id corresponding to the driver if one
 *      exists else returns vmk_ModuleStackTop().
 *
 * Results:
 *      Returns the module id 
 *
 * Side Effects:
 *      None.
 *----------------------------------------------------------------------
 */

vmk_ModuleID
vmklnx_get_driver_module_id(const struct device_driver *drv)
{
   vmk_ModuleID moduleID = VMK_INVALID_MODULE_ID;

   if (likely(drv != NULL)) {
      VMK_ASSERT(drv->owner != NULL);
      if (likely(drv->owner != NULL)) {
         moduleID = drv->owner->moduleID;
      }
      VMK_ASSERT(moduleID != VMK_INVALID_MODULE_ID);
   }

   if (unlikely(moduleID == VMK_INVALID_MODULE_ID)) {
      moduleID = vmk_ModuleStackTop();
   }

   return moduleID;
}

/*
 * This is a simple implementation of vmalloc using a physically discontiguous heap.
 * vmalloc is only supposed to be used for
 * allocations that are large so the overhead shouldn't be that large of a
 * percentage of the overall allocation.  We could save overhead by allocating
 * smaller vmalloc requests off of the heap.
 */
void *
__vmalloc(unsigned long size, gfp_t flags,  pgprot_t prot)
{
   void *addr;

   if (size == 0) {
      VMKLNX_WARN("Invalid size argument");
      return NULL;
   }
   addr = vmk_HeapAlloc(vmklnxVmallocHeap, size);
   if (addr == NULL){
      VMKLNX_WARN("Couldn't allocate virtual memory");
      return NULL;
   }
   return addr;
}

/**                                          
 *  vmap - Create a virtually contiguous mapping for a given array of pages       
 *  @pages: Array of pages    
 *  @count: Number of elements in the page array
 *  @flags: Flags for the underlying vm_area created by Linux 
 *  @prot: Page protection for each page of the new mapping
 *
 *  Returns the new virtual address upon success, NULL upon failure.
 *                                           
 *  vmap() takes an array of page structures that may or may not be physically
 *  contiguous and creates a single, new virtually contiguous mapping covering
 *  the pages.  This mapping is valid in the vmklinux kernel-virtual address
 *  space and is in addition to any existing mapping(s) for the pages.
 *  page_to_virt() will return the default virtual mapping for the component 
 *  pages after calling vmap(), *not* the new mapping that was created with 
 *  vmap().  You must keep track of the original mapped address if you want to 
 *  later unmap it.
 *                                           
 *  ESX Deviation Notes:                     
 *  flags and prot are ignored.  This function always implements the behavior
 *  of flags = VM_MAP and prot = PAGE_KERNEL 
 *                                           
 */
/* _VMKLNX_CODECHECK_: vmap */
void *vmap(struct page **pages, unsigned int count, 
		   unsigned long flags, pgprot_t prot)
{
   vmk_MachPageRange *ranges;
   void *vaddr = NULL;
   VMK_ReturnStatus status;
   unsigned int i;
   
   ranges = vmalloc(sizeof(vmk_MachPageRange) * count);
   if(ranges == NULL) {
      return NULL;
   }
   
   for(i = 0; i < count; i++) {
      ranges[i].numPages = 1;
      ranges[i].startPage = (vmk_MachPage) page_to_pfn(pages[i]);		 
   }
   
   status = vmk_MapVA(ranges, (vmk_uint32) count, (vmk_VirtAddr *) &vaddr);
   if(status != VMK_OK) {
      vaddr = NULL;
   }

   vfree(ranges);
   return vaddr;
}

/**                                          
 *  vunmap - Unmap a virtually contiguous mapping that was created with vmap
 *  @addr: Virtual address created with vmap    
 *                                           
 *  vunmap() tears down the virtual mapping that was created with vmap().
 *  This function should only be used on virtual mappings that were created
 *  with vmap().
 * 
 */
/* _VMKLNX_CODECHECK_: vunmap */
void vunmap(void *addr)
{
   vmk_UnmapVA((vmk_VirtAddr) addr);
}


/**                                          
 *  vfree - Free the memory
 *  @addr: pointer to memory area to be freed
 *                                           
 *  Frees the memory allocated by the vmalloc().
 *                                           
 *  RETURN VALUE:
 *  None.
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: vfree */
void 
vfree(void * addr)
{
   // To comply with Linux implementation.
   if (addr == NULL) {
      return;
   }
   vmk_HeapFree(vmklnxVmallocHeap, addr);
}

void
Linux_OpenSoftirq(int nr, void (*action)(struct softirq_action *), void *data)
{
   VMK_ASSERT(nr < MAX_SOFTIRQ);

   softirq_vec[nr].action = action;
   softirq_vec[nr].data = data;
}

/**                                          
 *  raw_smp_processor_id - Returns the ID of the current cpu   
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: raw_smp_processor_id */
uint32_t
raw_smp_processor_id(void)
{
   return vmk_GetPCPUNum();
}

static uint32_t
LinuxAtomicReadWriteSoftirqFlag(uint32_t val)
{
   return (uint32_t)vmk_AtomicReadWrite64(&vmkLinuxPCPU[vmk_GetPCPUNum()].
                                          softirq_pending, (vmk_uint64)val);
}

static uint32_t
LinuxAtomicFetchAndOrSoftirqFlag(uint32_t val)
{
   return (uint32_t)vmk_AtomicReadOr64(&vmkLinuxPCPU[vmk_GetPCPUNum()].
                                       softirq_pending, (vmk_uint64)val);
}

unsigned int
softirq_pending(int cpu)
{
   VMK_ASSERT(cpu < vmk_NumPCPUs());
   return (unsigned int)vmk_AtomicRead64(&vmkLinuxPCPU[cpu].softirq_pending);
}

void
__cpu_raise_softirq(int cpu, int nr)
{
   uint32_t pending;
   uint32_t this_cpu = smp_processor_id();

   VMK_ASSERT(nr < 32);
   pending = LinuxAtomicFetchAndOrSoftirqFlag((1 << nr));

   // see if need to schedule a softirq on this cpu.
   if (!pending) {
      Linux_BH_static(Linux_Softirq, &vmkLinuxPCPU[this_cpu].softirqLinuxBHData);
   }
}

void __attribute__((regparm(3)))
cpu_raise_softirq(unsigned int cpu, unsigned int nr)
{
   __cpu_raise_softirq(cpu, nr);

   /*
    * XXX we should look into creating some kernel thread like
    * linux does with softirqd (see their impl of this fn)
    */
}

/* TODO: dilpreet Needs to be revisited */
void
__raise_softirq_irqoff(unsigned int nr)
{
   uint32_t pending;
   uint32_t this_cpu = smp_processor_id();

   VMK_ASSERT(nr < 32);
   pending = LinuxAtomicFetchAndOrSoftirqFlag((1 << nr));

   // see if need to schedule a softirq on this cpu.
   if (!pending) {
      Linux_BH_static(Linux_Softirq, &vmkLinuxPCPU[this_cpu].softirqLinuxBHData);
   }
}

void fastcall
raise_softirq_irqoff(unsigned int nr)
{
   __raise_softirq_irqoff(nr);
}

void fastcall
raise_softirq(unsigned int nr)
{
   raise_softirq_irqoff(nr);
}

asmlinkage void do_softirq(void)
{
   uint32_t pending;
   int i;

   pending = LinuxAtomicReadWriteSoftirqFlag(0);
   for (i = 0; i < MAX_SOFTIRQ; i++) {
      if (pending & (1 << i)) {
         VMK_ASSERT(softirq_vec[i].action);
         softirq_vec[i].action(softirq_vec[i].data);
      }
   }
}


void
vmklnx_pollwait(struct file *filp,
           wait_queue_head_t *q,
           poll_table *token)
{
   unsigned long flags;
   uint16_t major, minor;
   vmk_CharDevSetPollContext((vmk_PollContext *)token, (vmk_PollToken *)q);

   spin_lock_irqsave(&q->lock, flags);
   q->wakeupType = WAIT_QUEUE_POLL;
   /* 
    * XXX  - PR 364177 
    * We're packing these values into 8-byte datatypes packed into 
    * a structure in order to keep binary compatibility with KL RC1,
    * which used a single 16-bit value for the major only.  MN and
    * later should revisit the way we're carrying majors & minors 
    * through vmklinux for character/misc devices and unpack the
    * structure */
   VMK_ASSERT_ON_COMPILE(MAX_CHRDEV <= 256);
   VMK_ASSERT_ON_COMPILE(MISC_DYNAMIC_MINOR <= 256);
   VMK_ASSERT_ON_COMPILE(sizeof(q->chardev) == sizeof(uint16_t));
   LinuxChar_PIDToMajorMinor(filp->f_owner.pid, &major, &minor);
   q->chardev.major = (uint8_t) major;
   q->chardev.minor = (uint8_t) minor;
   spin_unlock_irqrestore(&q->lock, flags);
}

void si_meminfo(struct sysinfo *si)
{
   memset(si, 0, sizeof(*si));
   si->mem_unit = PAGE_SIZE;

   // just return max supported (or likely supported soon) values
#define PAGES_PER_GB (1024*1024*1024/PAGE_SIZE)
   si->totalram = 512*PAGES_PER_GB;
   si->totalhigh = 512*PAGES_PER_GB;

   // just make up numbers.
   si->freeram = si->totalram/2;
   si->freehigh = si->totalhigh/2;
}

#ifdef VM_X86_64
/*
 * XXX64: This needs to be investigated further. Seems like its going to be
 * deprecated in future.
 */
int
register_ioctl32_conversion(unsigned int cmd,
			    void *handler)
{
   return 0;
}

int
unregister_ioctl32_conversion(unsigned int cmd)
{
   return 0;
}

#endif

int request_firmware(const struct firmware **firmware_p, const char *name, 
		     struct device *device)
{
   VMKLNX_WARN("request_firmware");
   VMKLNX_ASSERT_NOT_IMPLEMENTED(0);
   return 0;
}

/**                                          
 *  release_firmware - release the resource associated with a firmware image
 *  @fw: firmware resource to release
 *                                           
 *  Release the resource associated with a firmware image. This call is
 *  unimplemented in ESX and raises an ASSERT in developer builds.
 *
 *  ESX Deviation Notes:                     
 *  NOT IMPLEMENTED.
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: release_firmware */
void release_firmware(const struct firmware *fw)
{
   VMKLNX_WARN("release_firmware");
   VMKLNX_ASSERT_NOT_IMPLEMENTED(0);
}

/*
 * printk rate limiting, lifted from the networking subsystem.
 *
 * This enforces a rate limit: not more than one kernel message
 * every printk_ratelimit_jiffies to make a denial-of-service
 * attack impossible.
 */
int __printk_ratelimit(int ratelimit_jiffies, int ratelimit_burst)
{
	static DEFINE_SPINLOCK(ratelimit_lock);
	static unsigned long toks = 10 * 5 * HZ;
	static unsigned long last_msg;
	static int missed;
	unsigned long flags;
	unsigned long now = jiffies;

	spin_lock_irqsave(&ratelimit_lock, flags);
	toks += now - last_msg;
	last_msg = now;
	if (toks > (ratelimit_burst * ratelimit_jiffies))
		toks = ratelimit_burst * ratelimit_jiffies;
	if (toks >= ratelimit_jiffies) {
		int lost = missed;

		missed = 0;
		toks -= ratelimit_jiffies;
		spin_unlock_irqrestore(&ratelimit_lock, flags);
		if (lost)
			printk(KERN_WARNING "printk: %d messages suppressed.\n", lost);
		return 1;
	}
	missed++;
	spin_unlock_irqrestore(&ratelimit_lock, flags);
	return 0;
}

/**
 * printk_ratelimit - guard against excessive message logging
 *
 * Restricts message logging rate not to exceed one every 5 seconds.
 * Here is a typical usage
 *
 * 	if (printk_ratelimit()) printk(...);
 *
 * RETURN VALUE:
 * 1 if rate limit has not been exceeded; otherwise 0.
 *
 */
/* _VMKLNX_CODECHECK_: printk_ratelimit */
int printk_ratelimit(void)
{
	return __printk_ratelimit(printk_ratelimit_jiffies,
				printk_ratelimit_burst);
}

int net_ratelimit(void)
{
	return __printk_ratelimit(net_msg_cost, net_msg_burst);
}

/* from kernel/resource.c for platform.c */
#ifdef read_lock_irqsave
#undef read_lock_irqsave
#endif

#ifdef read_unlock_irqrestore
#undef read_unlock_irqrestore
#endif

#ifdef write_lock_irqsave
#undef write_lock_irqsave
#endif

#ifdef write_unlock_irqrestore
#undef write_unlock_irqrestore
#endif

#define read_lock_irqsave(_lck, _flags) spin_lock_irqsave(_lck, _flags)
#define read_unlock_irqrestore(_lck, _flags) spin_unlock_irqrestore(_lck, _flags)
#define write_lock_irqsave(_lck, _flags) spin_lock_irqsave(_lck, _flags)
#define write_unlock_irqrestore(_lck, _flags) spin_unlock_irqrestore(_lck, _flags)

#define rwlock_t spinlock_t
#ifdef rwlock_init
#undef rwlock_init
#endif
#define rwlock_init(_lck) spin_lock_init(_lck)

static DEFINE_RWLOCK(resource_lock);

static int __release_resource(struct resource *old)
{
        struct resource *tmp, **p;

        p = &old->parent->child;
        for (;;) {
                tmp = *p;
                if (!tmp)
                        break;
                if (tmp == old) {
                        *p = tmp->sibling;
                        old->parent = NULL;
                        return 0;
                }
                p = &tmp->sibling;
        }
        return -EINVAL;
}

/**
 * release_resource - release a previously reserved resource
 * @old: resource pointer
 */
int release_resource(struct resource *old)
{
        int retval;
#if defined(__VMKLNX__)
        unsigned long flags;

        write_lock_irqsave(&resource_lock, flags);
#else
        write_lock(&resource_lock);
#endif
        retval = __release_resource(old);
#if defined(__VMKLNX__)
        write_unlock_irqrestore(&resource_lock, flags);
#else
        write_unlock(&resource_lock);
#endif
        return retval;
}

/* Return the conflict entry if you can't request it */
static struct resource * __request_resource(struct resource *root, struct resource *new)
{
        resource_size_t start = new->start;
        resource_size_t end = new->end;
        struct resource *tmp, **p;

        if (end < start)
                return root;
        if (start < root->start)
                return root;
        if (end > root->end)
                return root;
        p = &root->child;
        for (;;) {
                tmp = *p;
                if (!tmp || tmp->start > end) {
                        new->sibling = tmp;
                        *p = new;
                        new->parent = root;
                        return NULL;
                }
                p = &tmp->sibling;
                if (tmp->end < start)
                        continue;
                return tmp;
        }
}

/**
 * insert_resource - Inserts a resource in the resource tree
 * @parent: parent of the new resource
 * @new: new resource to insert
 *
 * Returns 0 on success, -EBUSY if the resource can't be inserted.
 *
 * This function is equivalent to request_resource when no conflict
 * happens. If a conflict happens, and the conflicting resources
 * entirely fit within the range of the new resource, then the new
 * resource is inserted and the conflicting resources become children of
 * the new resource.
 */
int insert_resource(struct resource *parent, struct resource *new)
{
        int result;
        struct resource *first, *next;
#if defined(__VMKLNX__)
        unsigned long flags;

        write_lock_irqsave(&resource_lock, flags);
#else
        write_lock(&resource_lock);
#endif

        for (;; parent = first) {
                result = 0;
                first = __request_resource(parent, new);
                if (!first)
                        goto out;

                result = -EBUSY;
                if (first == parent)
                        goto out;

                if ((first->start > new->start) || (first->end < new->end))
                        break;
                if ((first->start == new->start) && (first->end == new->end))
                        break;
        }

        for (next = first; ; next = next->sibling) {
                /* Partial overlap? Bad, and unfixable */
                if (next->start < new->start || next->end > new->end)
                        goto out;
                if (!next->sibling)
                        break;
                if (next->sibling->start > new->end)
                        break;
        }

        result = 0;

        new->parent = parent;
        new->sibling = next->sibling;
        new->child = first;

        next->sibling = NULL;
        for (next = first; next; next = next->sibling)
                next->parent = new;

        if (parent->child == first) {
                parent->child = new;
        } else {
                next = parent->child;
                while (next->sibling != first)
                        next = next->sibling;
                next->sibling = new;
        }

 out:
#if defined(__VMKLNX__)
        write_unlock_irqrestore(&resource_lock, flags);
#else
        write_unlock(&resource_lock);
#endif
        return result;
}

vmklnx_kbd_handle
vmklnx_register_usb_kbd_int_handler(vmklnx_usb_interrupt_info *int_info)
{
   vmk_KeyboardInterruptHandle handle;
   VMK_ReturnStatus ret;

   VMK_ASSERT_ON_COMPILE(sizeof(vmk_KeyboardInterruptHandle) ==
                         sizeof(vmklnx_kbd_handle));

   ret = vmk_RegisterInputKeyboardInterruptHandler(
                        (vmk_InputInterruptHandler *)int_info->handler,
                        int_info->irq,
                        int_info->context,
                        int_info->regs,
                        &handle);
   if (ret != VMK_OK) {
      handle = NULL;
   }
   return (vmklnx_kbd_handle)handle;
}

int
vmklnx_unregister_usb_kbd_int_handler(vmklnx_kbd_handle handle)
{
   if (vmk_UnregisterInputKeyboardInterruptHandler((vmk_KeyboardInterruptHandle)handle)
        == VMK_OK) {
      return 0;
   }
   return -1;
}

/*
 * Support for compat_alloc_user_space follows.
 */

/**
 * compat_alloc_user_space - allocate memory from user space
 * @len: len of the memory required
 *
 * Returns memory from the user space of the calling process.
 *
 * ESX Deviation Notes:
 * In ESX, compat_alloc_user_space() actually returns specially marked 
 * kernel memory. You can't directly access the pointer.
 * However, the memory can be passed to copy_to_user(),
 * copy_from_user(), and clear_user() as if it was user memory, so
 * that the implementation is functionally equivalent.
 */

/* _VMKLNX_CODECHECK_: compat_alloc_user_space */
void *compat_alloc_user_space(long len)
{
   struct umem *umem;
   VMK_ReturnStatus status;

   /*
    * First check to see if the space has already been allocated.
    */
   status = vmk_WorldGetPrivateInfo(umem_key, (vmk_AddrCookie *)&umem);
   if (status == VMK_OK && umem->len >= len) {
      return __MarkPtrAsVMKBuffer(&umem->data[0]);
   }

   /*
    * The space is either not there, or its too small.  So allocate some
    * now and set it into our world private info area.
    */
   if (len < MIN_UMEM_SIZE) {
      len = MIN_UMEM_SIZE;
   }
   umem = kmalloc(offsetof(struct umem, data) + len, GFP_KERNEL);
   if (umem == NULL) {
      return NULL;
   }
   umem->len = len;
   status = vmk_WorldSetPrivateInfo(umem_key, (vmk_AddrCookie *)umem);
   if (status != VMK_OK) {
      return NULL;
   }

   return __MarkPtrAsVMKBuffer(&umem->data[0]);
}

static void
umem_free(vmk_AddrCookie cookie, vmk_WorldPrivateInfoKey key, vmk_WorldID id)
{
   VMK_ASSERT(key == umem_key);
   kfree(cookie.ptr);
}

static void
umem_init(void)
{
   VMK_ReturnStatus status;

   status = vmk_WorldPrivateKeyCreate(&umem_key, umem_free);
   umem_initialized = 1;
   if (status != VMK_OK) {
      vmk_Panic("vmklinux: umem_init: vmk_WorldPrivateKeyCreate failed %s\n",
                vmk_StatusToString(status));
   }
}

static void
umem_deinit(void)
{
   if (umem_initialized) {
      (void) vmk_WorldPrivateKeyDestroy(umem_key);
      umem_initialized = 0;
   }
}
