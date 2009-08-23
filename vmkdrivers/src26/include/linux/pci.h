/*
 *	pci.h
 *
 *	PCI defines and function prototypes
 *	Copyright 1994, Drew Eckhardt
 *	Copyright 1997--1999 Martin Mares <mj@ucw.cz>
 *
 *	For more information, please consult the following manuals (look at
 *	http://www.pcisig.com/ for how to get them):
 *
 *	PCI BIOS Specification
 *	PCI Local Bus Specification
 *	PCI to PCI Bridge Specification
 *	PCI System Design Guide
 */

#ifndef LINUX_PCI_H
#define LINUX_PCI_H

/* Include the pci register defines */
#include <linux/pci_regs.h>

/* Include the ID list */
#include <linux/pci_ids.h>

/**
 *  PCI_DEVFN - Form the PCI device function number
 *  @slot: Slot # of the pci device
 *  @func: function # of the pci device
 *
 *  The PCI interface treats multi-function devices as independent
 *  devices.  The slot/function address of each device is encoded
 *  in a single byte as follows
 *
 *  Bits 7-3 = slot      Bits 2-0 = function
 *
 *  This macro will generate the device function number as described above.
 *
 *  SYNOPSIS:
 *  #define PCI_DEVFN(slot,func)
 *
 *  RETURN VALUE:
 *  NONE
 *
 */
 /* _VMKLNX_CODECHECK_: PCI_DEVFN */
#define PCI_DEVFN(slot,func)	((((slot) & 0x1f) << 3) | ((func) & 0x07))

/**
 *  PCI_SLOT - Get the slot # from a given PCI device function number
 *  @devfn: the PCI device function number
 *
 *  The PCI interface treats multi-function devices as independent
 *  devices.  The slot/function address of each device is encoded
 *  in a single byte as follows
 *
 *  Bits 7-3 = slot      Bits 2-0 = function
 *
 *  This macro will extract the slot # from the given device function number
 *
 *  SYNOPSIS:
 *  #define PCI_SLOT(devfn)
 *
 *  RETURN VALUE:
 *  NONE
 *
 */
 /* _VMKLNX_CODECHECK_: PCI_SLOT */
#define PCI_SLOT(devfn)		(((devfn) >> 3) & 0x1f)

/**
 *  PCI_FUNC - Get the function # from the given PCI device function number
 *  @devfn: the PCI device function number
 *
 *  The PCI interface treats multi-function devices as independent
 *  devices.  The slot/function address of each device is encoded
 *  in a single byte as follows
 *
 *  Bits 7-3 = slot      Bits 2-0 = function
 *
 *  This macro will extract the function # from the given device function number
 *
 *  SYNOPSIS:
 *  #define PCI_FUNC(devfn)
 *
 *  RETURN VALUE:
 *  NONE
 *
 */
 /* _VMKLNX_CODECHECK_: PCI_FUNC */
#define PCI_FUNC(devfn)		((devfn) & 0x07)

/* Ioctls for /proc/bus/pci/X/Y nodes. */
#define PCIIOC_BASE		('P' << 24 | 'C' << 16 | 'I' << 8)
#define PCIIOC_CONTROLLER	(PCIIOC_BASE | 0x00)	/* Get controller for PCI device. */
#define PCIIOC_MMAP_IS_IO	(PCIIOC_BASE | 0x01)	/* Set mmap state to I/O space. */
#define PCIIOC_MMAP_IS_MEM	(PCIIOC_BASE | 0x02)	/* Set mmap state to MEM space. */
#define PCIIOC_WRITE_COMBINE	(PCIIOC_BASE | 0x03)	/* Enable/disable write-combining. */

#ifdef __KERNEL__

#include <linux/mod_devicetable.h>

#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/device.h>

/* File state for mmap()s on /proc/bus/pci/X/Y */
enum pci_mmap_state {
	pci_mmap_io,
	pci_mmap_mem
};

/* This defines the direction arg to the DMA mapping routines. */
#define PCI_DMA_BIDIRECTIONAL	0
#define PCI_DMA_TODEVICE	1
#define PCI_DMA_FROMDEVICE	2
#define PCI_DMA_NONE		3

#define DEVICE_COUNT_COMPATIBLE	4
#define DEVICE_COUNT_RESOURCE	12

typedef int __bitwise pci_power_t;

#define PCI_D0		((pci_power_t __force) 0)
#define PCI_D1		((pci_power_t __force) 1)
#define PCI_D2		((pci_power_t __force) 2)
#define PCI_D3hot	((pci_power_t __force) 3)
#define PCI_D3cold	((pci_power_t __force) 4)
#define PCI_UNKNOWN	((pci_power_t __force) 5)
#define PCI_POWER_ERROR	((pci_power_t __force) -1)

/** The pci_channel state describes connectivity between the CPU and
 *  the pci device.  If some PCI bus between here and the pci device
 *  has crashed or locked up, this info is reflected here.
 */
typedef unsigned int __bitwise pci_channel_state_t;

enum pci_channel_state {
	/* I/O channel is in normal state */
	pci_channel_io_normal = (__force pci_channel_state_t) 1,

	/* I/O to channel is blocked */
	pci_channel_io_frozen = (__force pci_channel_state_t) 2,

	/* PCI card is dead */
	pci_channel_io_perm_failure = (__force pci_channel_state_t) 3,
};

typedef unsigned short __bitwise pci_bus_flags_t;
enum pci_bus_flags {
	PCI_BUS_FLAGS_NO_MSI = (__force pci_bus_flags_t) 1,
};

struct pci_cap_saved_state {
	struct hlist_node next;
	char cap_nr;
	u32 data[0];
};

/*
 * The pci_dev structure is used to describe PCI devices.
 */
struct pci_dev {
	struct list_head global_list;	/* node in list of all PCI devices */
	struct list_head bus_list;	/* node in per-bus list */
	struct pci_bus	*bus;		/* bus this device is on */
	struct pci_bus	*subordinate;	/* bus this device bridges to */

	void		*sysdata;	/* hook for sys-specific extension */
	struct proc_dir_entry *procent;	/* device entry in /proc/bus/pci */

	unsigned int	devfn;		/* encoded device & function index */
	unsigned short	vendor;
	unsigned short	device;
	unsigned short	subsystem_vendor;
	unsigned short	subsystem_device;
	unsigned int	class;		/* 3 bytes: (base,sub,prog-if) */
#if defined(__COMPAT_LAYER_2_6_18_PLUS__)
        u8              revision;       /* PCI revision, low byte of class word */
#endif /* defined(__COMPAT_LAYER_2_6_18_PLUS__) */
        u8		hdr_type;	/* PCI header type (`multi' flag masked out) */
	u8		rom_base_reg;	/* which config register controls the ROM */
	u8		pin;  		/* which interrupt pin this device uses */

	struct pci_driver *driver;	/* which driver has allocated this device */
	u64		dma_mask;	/* Mask of the bits of bus address this
					   device implements.  Normally this is
					   0xffffffff.  You only need to change
					   this if your device has broken DMA
					   or supports 64-bit transfers.  */

	pci_power_t     current_state;  /* Current operating state. In ACPI-speak,
					   this is D0-D3, D0 being fully functional,
					   and D3 being off. */

	pci_channel_state_t error_state;	/* current connectivity state */
	struct	device	dev;		/* Generic device interface */

	/* device is compatible with these IDs */
	unsigned short vendor_compatible[DEVICE_COUNT_COMPATIBLE];
	unsigned short device_compatible[DEVICE_COUNT_COMPATIBLE];

	int		cfg_size;	/* Size of configuration space */

	/*
	 * Instead of touching interrupt line and base address registers
	 * directly, use the values stored here. They might be different!
	 */
	unsigned int	irq;
	struct resource resource[DEVICE_COUNT_RESOURCE]; /* I/O and memory regions + expansion ROMs */

	/* These fields are used by common fixups */
	unsigned int	transparent:1;	/* Transparent PCI bridge */
	unsigned int	multifunction:1;/* Part of multi-function device */
	/* keep track of device state */
	unsigned int	is_enabled:1;	/* pci_enable_device has been called */
	unsigned int	is_busmaster:1; /* device is busmaster */
	unsigned int	no_msi:1;	/* device may not use msi */
	unsigned int	no_d1d2:1;   /* only allow d0 or d3 */
	unsigned int	block_ucfg_access:1;	/* userspace config space access is blocked */
	unsigned int	broken_parity_status:1;	/* Device generates false positive parity */
	unsigned int 	msi_enabled:1;
	unsigned int	msix_enabled:1;

#if defined(__COMPAT_LAYER_2_6_18_PLUS__)
        atomic_t        enable_cnt;     /* pci_enable_device has been called */
#endif /* defined(__COMPAT_LAYER_2_6_18_PLUS__) */

	u32		saved_config_space[16]; /* config space saved at suspend time */
	struct hlist_head saved_cap_space;
	struct bin_attribute *rom_attr; /* attribute descriptor for sysfs ROM entry */
	int rom_attr_enabled;		/* has display of the rom attribute been enabled? */
	struct bin_attribute *res_attr[DEVICE_COUNT_RESOURCE]; /* sysfs file for resources */
#if defined(__VMKLNX__)
   void *netdev; /* This is used for network devices and ignored for the rest */
   char     name[90];   /* device name */
#endif /* defined(__VMKLNX__) */
};

#define pci_dev_g(n) list_entry(n, struct pci_dev, global_list)
#define pci_dev_b(n) list_entry(n, struct pci_dev, bus_list)
/**
 * to_pci_dev - Returns the pci_device structure associated with the specific device structure
 * @n: Pointer to the struct device member in struct pci_dev
 *
 * Returns the pci_device structure associated with specified 
 * device structure.
 *
 * SYNOPSIS:
 * #define to_pci_dev(n)
 *
 * RETURN VALUE:
 * Pointer to the struct pci device
 */
 /* _VMKLNX_CODECHECK_: to_pci_dev */
#define to_pci_dev(n) container_of(n, struct pci_dev, dev)

/**
 *  for_each_pci_dev - Iterate over all pci devices in the system
 *  @d: pointer to struct pci_dev
 *
 *  Iterates through the list of known PCI devices. If a device is returned, 
 *  the reference count to the device is incremented and a pointer to its device structure 
 *  is returned. Otherwise, %NULL is returned.  A new iteration is initiated by passing %NULL
 *  in @d argument.  Otherwise if @d is not equal to %NULL, iteration continues
 *  from next device on the global list.  The reference count for @d is
 *  always decremented if it is not %NULL.
 * 
 *  SYNOPSIS:
 *  #define for_all_pci_dev(d)
 *
 *  RETURN VALUE:
 *  NONE
 */
 /* _VMKLNX_CODECHECK_: for_each_pci_dev */
#define for_each_pci_dev(d) while ((d = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, d)) != NULL)

#if defined(__VMKLNX__)
struct pci_dev *pci_get_device_all(unsigned int vendor, unsigned int device, struct pci_dev *from);
#define for_all_pci_dev(d) while ((d = pci_get_device_all(PCI_ANY_ID, PCI_ANY_ID, d)) != NULL)
#endif /* defined(__VMKLNX__) */

static inline struct pci_cap_saved_state *pci_find_saved_cap(
	struct pci_dev *pci_dev,char cap)
{
	struct pci_cap_saved_state *tmp;
	struct hlist_node *pos;

	hlist_for_each_entry(tmp, pos, &pci_dev->saved_cap_space, next) {
		if (tmp->cap_nr == cap)
			return tmp;
	}
	return NULL;
}

static inline void pci_add_saved_cap(struct pci_dev *pci_dev,
	struct pci_cap_saved_state *new_cap)
{
	hlist_add_head(&new_cap->next, &pci_dev->saved_cap_space);
}

static inline void pci_remove_saved_cap(struct pci_cap_saved_state *cap)
{
	hlist_del(&cap->next);
}

/*
 *  For PCI devices, the region numbers are assigned this way:
 *
 *	0-5	standard PCI regions
 *	6	expansion ROM
 *	7-10	bridges: address space assigned to buses behind the bridge
 */

#define PCI_ROM_RESOURCE	6
#define PCI_BRIDGE_RESOURCES	7
#define PCI_NUM_RESOURCES	11

#ifndef PCI_BUS_NUM_RESOURCES
#define PCI_BUS_NUM_RESOURCES	8
#endif

#define PCI_REGION_FLAG_MASK	0x0fU	/* These bits of resource flags tell us the PCI region flags */

struct pci_bus {
	struct list_head node;		/* node in list of buses */
	struct pci_bus	*parent;	/* parent bus this bridge is on */
	struct list_head children;	/* list of child buses */
	struct list_head devices;	/* list of devices on this bus */
	struct pci_dev	*self;		/* bridge device as seen by parent */
	struct resource	*resource[PCI_BUS_NUM_RESOURCES];
					/* address space routed to this bus */

	struct pci_ops	*ops;		/* configuration access functions */
	void		*sysdata;	/* hook for sys-specific extension */
	struct proc_dir_entry *procdir;	/* directory entry in /proc/bus/pci */

	unsigned char	number;		/* bus number */
	unsigned char	primary;	/* number of primary bridge */
	unsigned char	secondary;	/* number of secondary bridge */
	unsigned char	subordinate;	/* max number of subordinate buses */

	char		name[48];

	unsigned short  bridge_ctl;	/* manage NO_ISA/FBB/et al behaviors */
	pci_bus_flags_t bus_flags;	/* Inherited by child busses */
	struct device		*bridge;
	struct class_device	class_dev;
	struct bin_attribute	*legacy_io; /* legacy I/O for this bus */
	struct bin_attribute	*legacy_mem; /* legacy mem */
};

#define pci_bus_b(n)	list_entry(n, struct pci_bus, node)
#define to_pci_bus(n)	container_of(n, struct pci_bus, class_dev)

/*
 * Error values that may be returned by PCI functions.
 */
#define PCIBIOS_SUCCESSFUL		0x00
#define PCIBIOS_FUNC_NOT_SUPPORTED	0x81
#define PCIBIOS_BAD_VENDOR_ID		0x83
#define PCIBIOS_DEVICE_NOT_FOUND	0x86
#define PCIBIOS_BAD_REGISTER_NUMBER	0x87
#define PCIBIOS_SET_FAILED		0x88
#define PCIBIOS_BUFFER_TOO_SMALL	0x89

/* Low-level architecture-dependent routines */

struct pci_ops {
	int (*read)(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *val);
	int (*write)(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val);
};

struct pci_raw_ops {
	int (*read)(unsigned int domain, unsigned int bus, unsigned int devfn,
		    int reg, int len, u32 *val);
	int (*write)(unsigned int domain, unsigned int bus, unsigned int devfn,
		     int reg, int len, u32 val);
};

extern struct pci_raw_ops *raw_pci_ops;

struct pci_bus_region {
	unsigned long start;
	unsigned long end;
};

struct pci_dynids {
	spinlock_t lock;            /* protects list, index */
	struct list_head list;      /* for IDs added at runtime */
	unsigned int use_driver_data:1; /* pci_driver->driver_data is used */
};

/* ---------------------------------------------------------------- */
/** PCI Error Recovery System (PCI-ERS).  If a PCI device driver provides
 *  a set fof callbacks in struct pci_error_handlers, then that device driver
 *  will be notified of PCI bus errors, and will be driven to recovery
 *  when an error occurs.
 */

typedef unsigned int __bitwise pci_ers_result_t;

enum pci_ers_result {
	/* no result/none/not supported in device driver */
	PCI_ERS_RESULT_NONE = (__force pci_ers_result_t) 1,

	/* Device driver can recover without slot reset */
	PCI_ERS_RESULT_CAN_RECOVER = (__force pci_ers_result_t) 2,

	/* Device driver wants slot to be reset. */
	PCI_ERS_RESULT_NEED_RESET = (__force pci_ers_result_t) 3,

	/* Device has completely failed, is unrecoverable */
	PCI_ERS_RESULT_DISCONNECT = (__force pci_ers_result_t) 4,

	/* Device driver is fully recovered and operational */
	PCI_ERS_RESULT_RECOVERED = (__force pci_ers_result_t) 5,
};

/* PCI bus error event callbacks */
struct pci_error_handlers
{
	/* PCI bus error detected on this device */
	pci_ers_result_t (*error_detected)(struct pci_dev *dev,
	                      enum pci_channel_state error);

	/* MMIO has been re-enabled, but not DMA */
	pci_ers_result_t (*mmio_enabled)(struct pci_dev *dev);

	/* PCI Express link has been reset */
	pci_ers_result_t (*link_reset)(struct pci_dev *dev);

	/* PCI slot has been reset */
	pci_ers_result_t (*slot_reset)(struct pci_dev *dev);

	/* Device driver may resume normal operations */
	void (*resume)(struct pci_dev *dev);
};

/* ---------------------------------------------------------------- */

struct module;
struct pci_driver {
	struct list_head node;
	char *name;
	const struct pci_device_id *id_table;	/* must be non-NULL for probe to be called */
	int  (*probe)  (struct pci_dev *dev, const struct pci_device_id *id);	/* New device inserted */
	void (*remove) (struct pci_dev *dev);	/* Device removed (NULL if not a hot-plug capable driver) */
	int  (*suspend) (struct pci_dev *dev, pm_message_t state);	/* Device suspended */
	int  (*resume) (struct pci_dev *dev);	                /* Device woken up */
	int  (*enable_wake) (struct pci_dev *dev, pci_power_t state, int enable);   /* Enable wake event */
	void (*shutdown) (struct pci_dev *dev);

	struct pci_error_handlers *err_handler;
	struct device_driver	driver;
	struct pci_dynids dynids;
};

#define	to_pci_driver(drv) container_of(drv,struct pci_driver, driver)

/**
 *  PCI_DEVICE - Describe a specific pci device
 *  @vend: the 16 bit PCI Vendor ID
 *  @dev: the 16 bit PCI Device ID
 *
 *  This initializes a struct pci_device_id that matches a specific device.
 *  The subvendor and subdevice fields will be set to PCI_ANY_ID.
 *
 *  SYNOPSIS:
 *  #define PCI_DEVICE(vend, dev)
 *
 *  RETURN VALUE:
 *  NONE
 *
 */
 /* _VMKLNX_CODECHECK_: PCI_DEVICE */
#define PCI_DEVICE(vend,dev) \
	.vendor = (vend), .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

/**
 *  PCI_DEVICE_CLASS - Describe a specific pci device class
 *  @dev_class: the class, subclass, prog-if triple for this device
 *  @dev_class_mask: the class mask for this device
 *
 *  This initializes a struct pci_device_id that matches a
 *  specific PCI class.  The vendor, device, subvendor, and subdevice
 *  fields will be set to PCI_ANY_ID.
 *
 *  SYNOPSIS:
 *  #define PCI_DEVICE_CLASS(dev_class,dev_class_mask)
 *
 *  RETURN VALUE:
 *  NONE
 *
 */
 /* _VMKLNX_CODECHECK_: PCI_DEVICE_CLASS */
#define PCI_DEVICE_CLASS(dev_class,dev_class_mask) \
	.class = (dev_class), .class_mask = (dev_class_mask), \
	.vendor = PCI_ANY_ID, .device = PCI_ANY_ID, \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

/*
 * pci_module_init is obsolete, this stays here till we fix up all usages of it
 * in the tree.
 */
#define pci_module_init	pci_register_driver

/* these external functions are only available when PCI support is enabled */
#ifdef CONFIG_PCI

extern struct bus_type pci_bus_type;

/* Do NOT directly access these two variables, unless you are arch specific pci
 * code, or pci core code. */
extern struct list_head pci_root_buses;	/* list of all known PCI buses */
extern struct list_head pci_devices;	/* list of all devices */
#if defined(__VMKLNX__)
// pci_devices contains all shown devices only
extern struct list_head pci_hidden_devices; /* list of all hidden devices */
#endif /* defined(__VMKLNX__) */

void pcibios_fixup_bus(struct pci_bus *);
int pcibios_enable_device(struct pci_dev *, int mask);
char *pcibios_setup (char *str);

/* Used only when drivers/pci/setup.c is used */
void pcibios_align_resource(void *, struct resource *, resource_size_t,
				resource_size_t);
void pcibios_update_irq(struct pci_dev *, int irq);

/* Generic PCI functions used internally */

extern struct pci_bus *pci_find_bus(int domain, int busnr);
void pci_bus_add_devices(struct pci_bus *bus);
struct pci_bus *pci_scan_bus_parented(struct device *parent, int bus, struct pci_ops *ops, void *sysdata);
static inline struct pci_bus *pci_scan_bus(int bus, struct pci_ops *ops, void *sysdata)
{
	struct pci_bus *root_bus;
	root_bus = pci_scan_bus_parented(NULL, bus, ops, sysdata);
	if (root_bus)
		pci_bus_add_devices(root_bus);
	return root_bus;
}
struct pci_bus *pci_create_bus(struct device *parent, int bus, struct pci_ops *ops, void *sysdata);
struct pci_bus * pci_add_new_bus(struct pci_bus *parent, struct pci_dev *dev, int busnr);
int pci_scan_slot(struct pci_bus *bus, int devfn);
struct pci_dev * pci_scan_single_device(struct pci_bus *bus, int devfn);
void pci_device_add(struct pci_dev *dev, struct pci_bus *bus);
unsigned int pci_scan_child_bus(struct pci_bus *bus);
void pci_bus_add_device(struct pci_dev *dev);
void pci_read_bridge_bases(struct pci_bus *child);
struct resource *pci_find_parent_resource(const struct pci_dev *dev, struct resource *res);
int pci_get_interrupt_pin(struct pci_dev *dev, struct pci_dev **bridge);
extern struct pci_dev *pci_dev_get(struct pci_dev *dev);
extern void pci_dev_put(struct pci_dev *dev);
extern void pci_remove_bus(struct pci_bus *b);
extern void pci_remove_bus_device(struct pci_dev *dev);
extern void pci_stop_bus_device(struct pci_dev *dev);
void pci_setup_cardbus(struct pci_bus *bus);
extern void pci_sort_breadthfirst(void);

/* Generic PCI functions exported to card drivers */

struct pci_dev *pci_find_device (unsigned int vendor, unsigned int device, const struct pci_dev *from);
struct pci_dev *pci_find_device_reverse (unsigned int vendor, unsigned int device, const struct pci_dev *from);
struct pci_dev *pci_find_slot (unsigned int bus, unsigned int devfn);
int pci_find_capability (struct pci_dev *dev, int cap);
int pci_find_next_capability (struct pci_dev *dev, u8 pos, int cap);
int pci_find_ext_capability (struct pci_dev *dev, int cap);
struct pci_bus * pci_find_next_bus(const struct pci_bus *from);

struct pci_dev *pci_get_device (unsigned int vendor, unsigned int device, struct pci_dev *from);
struct pci_dev *pci_get_subsys (unsigned int vendor, unsigned int device,
				unsigned int ss_vendor, unsigned int ss_device,
				struct pci_dev *from);
struct pci_dev *pci_get_slot (struct pci_bus *bus, unsigned int devfn);
struct pci_dev *pci_get_class (unsigned int class, struct pci_dev *from);
int pci_dev_present(const struct pci_device_id *ids);

#if !defined(__VMKLNX__)
int pci_bus_read_config_byte (struct pci_bus *bus, unsigned int devfn, int where, u8 *val);
int pci_bus_read_config_word (struct pci_bus *bus, unsigned int devfn, int where, u16 *val);
int pci_bus_read_config_dword (struct pci_bus *bus, unsigned int devfn, int where, u32 *val);
int pci_bus_write_config_byte (struct pci_bus *bus, unsigned int devfn, int where, u8 val);
int pci_bus_write_config_word (struct pci_bus *bus, unsigned int devfn, int where, u16 val);
int pci_bus_write_config_dword (struct pci_bus *bus, unsigned int devfn, int where, u32 val);

static inline int pci_read_config_byte(struct pci_dev *dev, int where, u8 *val)
{
	return pci_bus_read_config_byte (dev->bus, dev->devfn, where, val);
}

static inline int pci_read_config_word(struct pci_dev *dev, int where, u16 *val)
{
	return pci_bus_read_config_word (dev->bus, dev->devfn, where, val);
}

static inline int pci_read_config_dword(struct pci_dev *dev, int where, u32 *val)
{
	return pci_bus_read_config_dword (dev->bus, dev->devfn, where, val);
}

static inline int pci_write_config_byte(struct pci_dev *dev, int where, u8 val)
{
	return pci_bus_write_config_byte (dev->bus, dev->devfn, where, val);
}

static inline int pci_write_config_word(struct pci_dev *dev, int where, u16 val)
{
	return pci_bus_write_config_word (dev->bus, dev->devfn, where, val);
}

static inline int pci_write_config_dword(struct pci_dev *dev, int where, u32 val)
{
	return pci_bus_write_config_dword (dev->bus, dev->devfn, where, val);
}
#else /* defined(__VMKLNX__) */
int pci_read_config_byte(struct pci_dev *dev, int where, u8 *val);
int pci_read_config_word(struct pci_dev *dev, int where, u16 *val);
int pci_read_config_dword(struct pci_dev *dev, int where, u32 *val);
int pci_write_config_byte(struct pci_dev *dev, int where, u8 val);
int pci_write_config_word(struct pci_dev *dev, int where, u16 val);
int pci_write_config_dword(struct pci_dev *dev, int where, u32 val);
#endif /* !defined(__VMKLNX__) */

int pci_enable_device(struct pci_dev *dev);
int pci_enable_device_bars(struct pci_dev *dev, int mask);
void pci_disable_device(struct pci_dev *dev);
void pci_set_master(struct pci_dev *dev);
#define HAVE_PCI_SET_MWI
int pci_set_mwi(struct pci_dev *dev);
#if defined(__COMPAT_LAYER_2_6_18_PLUS__) && defined(__USE_COMPAT_LAYER_2_6_18_PLUS__)
int pci_try_set_mwi(struct pci_dev *dev);
int pcie_set_readrq(struct pci_dev *dev, int rq);
#endif
void pci_clear_mwi(struct pci_dev *dev);
void pci_intx(struct pci_dev *dev, int enable);
#if defined(__VMKLNX__)
struct vmklnx_codma;
extern struct vmklnx_codma vmklnx_codma;
extern int vmklnx_pci_set_dma_mask(struct vmklnx_codma *codma,
                                   struct pci_dev *dev, u64 mask);
extern int vmklnx_pci_set_consistent_dma_mask(struct vmklnx_codma *codma,
                                              struct pci_dev *dev, u64 mask);

/**                                          
 *  pci_set_dma_mask - Set the dma mask for a PCI device.
 *  @dev: PCI device implementing direct memory access.
 *  @mask: DMA mask for the above PCI device.
 *                                           
 *  Allows a driver to specify the dma mask for a PCI device.
 *  Returns 0 on success.
 *                                           
 *  ESX Deviation Notes:                     
 *  A dma requirement below 30 bits (DMA_30BIT_MASK) is not supported.
 */                                          
/* _VMKLNX_CODECHECK_: pci_set_dma_mask */
static inline int pci_set_dma_mask(struct pci_dev *dev, u64 mask)
{
   return vmklnx_pci_set_dma_mask(&vmklnx_codma, dev, mask);
}

/**                                          
 *  pci_set_consistent_dma_mask - Set the coherent dma mask for a PCI device.
 *  @dev: PCI device implementing direct memory access.
 *  @mask: Coherent DMA mask for the above PCI device.
 *                                           
 *  Allows a driver to specify the coherent dma mask for a PCI device.
 *  Returns 0 on success.
 *
 *  ESX Deviation Notes:                     
 *  A dma requirement below 30 bits (DMA_30BIT_MASK) is not supported.
 */                                          
/* _VMKLNX_CODECHECK_: pci_set_consistent_dma_mask */
static inline int pci_set_consistent_dma_mask(struct pci_dev *dev, u64 mask)
{
   return vmklnx_pci_set_consistent_dma_mask(&vmklnx_codma, dev, mask);
}
#else /* !defined(__VMKLNX__) */
int pci_set_dma_mask(struct pci_dev *dev, u64 mask);
int pci_set_consistent_dma_mask(struct pci_dev *dev, u64 mask);
#endif /* !defined(__VMKLNX__) */
void pci_update_resource(struct pci_dev *dev, struct resource *res, int resno);
int pci_assign_resource(struct pci_dev *dev, int i);
int pci_assign_resource_fixed(struct pci_dev *dev, int i);
void pci_restore_bars(struct pci_dev *dev);

/* ROM control related routines */
void __iomem __must_check *pci_map_rom(struct pci_dev *pdev, size_t *size);
void __iomem __must_check *pci_map_rom_copy(struct pci_dev *pdev, size_t *size);
void pci_unmap_rom(struct pci_dev *pdev, void __iomem *rom);
void pci_remove_rom(struct pci_dev *pdev);

/* Power management related routines */
int pci_save_state(struct pci_dev *dev);
int pci_restore_state(struct pci_dev *dev);
int pci_set_power_state(struct pci_dev *dev, pci_power_t state);
pci_power_t pci_choose_state(struct pci_dev *dev, pm_message_t state);
int pci_enable_wake(struct pci_dev *dev, pci_power_t state, int enable);

/* Helper functions for low-level code (drivers/pci/setup-[bus,res].c) */
void pci_bus_assign_resources(struct pci_bus *bus);
void pci_bus_size_bridges(struct pci_bus *bus);
int pci_claim_resource(struct pci_dev *, int);
void pci_assign_unassigned_resources(void);
void pdev_enable_device(struct pci_dev *);
void pdev_sort_resources(struct pci_dev *, struct resource_list *);
void pci_fixup_irqs(u8 (*)(struct pci_dev *, u8 *),
		    int (*)(struct pci_dev *, u8, u8));
#define HAVE_PCI_REQ_REGIONS	2
int pci_request_regions(struct pci_dev *, const char *);
void pci_release_regions(struct pci_dev *);
int pci_request_region(struct pci_dev *, int, const char *);
void pci_release_region(struct pci_dev *, int);

/* drivers/pci/bus.c */
int pci_bus_alloc_resource(struct pci_bus *bus, struct resource *res,
			   resource_size_t size, resource_size_t align,
			   resource_size_t min, unsigned int type_mask,
			   void (*alignf)(void *, struct resource *,
					  resource_size_t, resource_size_t),
			   void *alignf_data);
void pci_enable_bridges(struct pci_bus *bus);

/* Proper probing supporting hot-pluggable devices */
int __pci_register_driver(struct pci_driver *, struct module *);

/**
 *  pci_register driver - Register a new pci driver
 *  @driver: pointer to the pci_driver structure which has to be registered
 *
 *  This registers a given new pci driver and adds the driver structure to the
 *  list of registered drivers.
 *
 *  RETURN VALUE:
 *  0 on success and a negative value on failure.
 *
 */
/* _VMKLNX_CODECHECK_: pci_register_driver */
static inline int pci_register_driver(struct pci_driver *driver)
{
	return __pci_register_driver(driver, THIS_MODULE);
}

void pci_unregister_driver(struct pci_driver *);
void pci_remove_behind_bridge(struct pci_dev *);
struct pci_driver *pci_dev_driver(const struct pci_dev *);

#if defined(__VMKLNX__)
struct pci_dev * alloc_pci_dev(void);
#endif /* defined(__VMKLNX__) */

/* TODO: dilpreet remove hack later */
#if defined(__VMKLNX__)
const struct pci_device_id *pci_match_device(const struct pci_device_id *ids, const struct pci_dev *dev);
#else /* !defined(__VMKLNX__) */
const struct pci_device_id *pci_match_device(struct pci_driver *drv, struct pci_dev *dev);
#endif /* defined(__VMKLNX__) */
const struct pci_device_id *pci_match_id(const struct pci_device_id *ids, struct pci_dev *dev);
int pci_scan_bridge(struct pci_bus *bus, struct pci_dev * dev, int max, int pass);

void pci_walk_bus(struct pci_bus *top, void (*cb)(struct pci_dev *, void *),
		  void *userdata);
int pci_cfg_space_size(struct pci_dev *dev);
unsigned char pci_bus_max_busnr(struct pci_bus* bus);

/* kmem_cache style wrapper around pci_alloc_consistent() */

#include <linux/dmapool.h>

#define	pci_pool dma_pool

/**
 *  pci_pool_create - Create a DMA memory pool for use with the given device
 *  @name: descriptive name for the pool
 *  @pdev: Pointer to the struct pci_dev of the pci device
 *  @size: size of memory blocks for this pool, in bytes
 *  @align: alignment specification for blocks in this pool. Must be power of 2.
 *  @allocation: boundary constraints for blocks in this pool.  Blocks 
 *             in this pool will not cross this boundary.  Must be a 
 *             power of 2.
 *
 *  This is functionally the same as dma_pool_create. It uses the device
 *  structure in the passed in pdev to call into dma_pool_create.
 *
 *  SYNOPSIS:
 *  struct dma_pool *pci_pool_create(const char *name, struct device *pdev,
 *                               size_t size, size_t align, size_t allocation);
 *
 *  RETURN VALUE:
 *  Pointer to a DMA pool on success and NULL on failure
 *
 *  SEE ALSO:
 *  dma_pool_create
 *
 */
/* _VMKLNX_CODECHECK_: pci_pool_create */
#define pci_pool_create(name, pdev, size, align, allocation) \
		dma_pool_create(name, &pdev->dev, size, align, allocation)

/**
 *  pci_pool_destroy - Destroy a DMA pool
 *  @pool: pool to be destroyed
 *
 *  This is the same as dma_pool_destroy. See dma_pool_destroy for full details.
 *
 *  SYNOPSIS:
 *  void pci_pool_destroy(struct dma_pool *pool);
 *
 *  RETURN VALUE:
 *  NONE
 *
 *  SEE ALSO:
 *  dma_pool_destroy
 *
 */
/* _VMKLNX_CODECHECK_: pci_pool_destroy */
#define	pci_pool_destroy(pool) dma_pool_destroy(pool)

/**
 *  pci_pool_alloc - Allocate a block of memory from the dma pool
 *  @pool: pool object from which to allocate
 *  @flags: flags to be used in allocation
 *  @handle: output bus address for the allocated memory
 *
 *  This is the same as dma_pool_alloc. See dma_pool_alloc for full details
 *
 *  SYNOPSIS:
 *  void *pci_pool_alloc(struct dma_pool *pool, gfp_t mem_flags,
 *                       dma_addr_t *handle)
 *
 *  RETURN VALUE:
 *  Virtual address of the allocated memory on success, NULL on failure
 *
 *  SEE ALSO:
 *  dma_pool_alloc
 */
/* _VMKLNX_CODECHECK_: pci_pool_alloc */
#define	pci_pool_alloc(pool, flags, handle) dma_pool_alloc(pool, flags, handle)

/**
 *  pci_pool_free - Give back memory to the DMA pool
 *  @pool: DMA pool structure    
 *  @vaddr: virtual address of the memory being returned
 *  @addr: machine address of the memory being returned
 *
 *  Essentially the same as dma_pool_free. See dma_pool_free for details.
 *
 *  SYNOPSIS:
 *  void pci_pool_free(struct dma_pool *pool, void *vaddr, dma_addr_t addr);
 *
 *  RETURN VALUE:
 *  NONE
 *
 *  SEE ALSO:
 *  dma_pool_free
 *
 */
/* _VMKLNX_CODECHECK_: pci_pool_free */
#define	pci_pool_free(pool, vaddr, addr) dma_pool_free(pool, vaddr, addr)

enum pci_dma_burst_strategy {
	PCI_DMA_BURST_INFINITY,	/* make bursts as large as possible,
				   strategy_parameter is N/A */
	PCI_DMA_BURST_BOUNDARY, /* disconnect at every strategy_parameter
				   byte boundaries */
	PCI_DMA_BURST_MULTIPLE, /* disconnect at some multiple of
				   strategy_parameter byte boundaries */
};

#if defined(CONFIG_ISA) || defined(CONFIG_EISA)
extern struct pci_dev *isa_bridge;
#endif

struct msix_entry {
	u16 	vector;	/* kernel uses to write allocated vector */
	u16	entry;	/* driver uses to specify entry, OS writes */
};

#ifndef CONFIG_PCI_MSI
static inline void pci_scan_msi_device(struct pci_dev *dev) {}
static inline int pci_enable_msi(struct pci_dev *dev) {return -1;}
static inline void pci_disable_msi(struct pci_dev *dev) {}
static inline int pci_enable_msix(struct pci_dev* dev,
	struct msix_entry *entries, int nvec) {return -1;}
static inline void pci_disable_msix(struct pci_dev *dev) {}
static inline void msi_remove_pci_irq_vectors(struct pci_dev *dev) {}
#else
extern void pci_scan_msi_device(struct pci_dev *dev);
extern int pci_enable_msi(struct pci_dev *dev);
extern void pci_disable_msi(struct pci_dev *dev);
extern int pci_enable_msix(struct pci_dev* dev,
	struct msix_entry *entries, int nvec);
extern void pci_disable_msix(struct pci_dev *dev);
extern void msi_remove_pci_irq_vectors(struct pci_dev *dev);
#endif

extern void pci_block_user_cfg_access(struct pci_dev *dev);
extern void pci_unblock_user_cfg_access(struct pci_dev *dev);

/*
 * PCI domain support.  Sometimes called PCI segment (eg by ACPI),
 * a PCI domain is defined to be a set of PCI busses which share
 * configuration space.
 */
#ifndef CONFIG_PCI_DOMAINS
/**
 *  pci_domain_nr - non-operational function
 *  @bus: Ignored
 *
 *  A non-operational function provided to help reduce
 *  kernel ifdefs. It is not supported in this release of ESX.
 *
 *  ESX Deviation Notes:
 *  A non-operational function provided to help reduce
 *  kernel ifdefs. It is not supported in this release of ESX.
 *
 *  RETURN VALUE:
 *  0 always
 *
 */
/* _VMKLNX_CODECHECK_: pci_domain_nr */
static inline int pci_domain_nr(struct pci_bus *bus) { return 0; }
static inline int pci_proc_domain(struct pci_bus *bus)
{
	return 0;
}
#endif

#else /* CONFIG_PCI is not enabled */

/*
 *  If the system does not have PCI, clearly these return errors.  Define
 *  these as simple inline functions to avoid hair in drivers.
 */

#define _PCI_NOP(o,s,t) \
	static inline int pci_##o##_config_##s (struct pci_dev *dev, int where, t val) \
		{ return PCIBIOS_FUNC_NOT_SUPPORTED; }
#define _PCI_NOP_ALL(o,x)	_PCI_NOP(o,byte,u8 x) \
				_PCI_NOP(o,word,u16 x) \
				_PCI_NOP(o,dword,u32 x)
_PCI_NOP_ALL(read, *)
_PCI_NOP_ALL(write,)

static inline struct pci_dev *pci_find_device(unsigned int vendor, unsigned int device, const struct pci_dev *from)
{ return NULL; }

static inline struct pci_dev *pci_find_slot(unsigned int bus, unsigned int devfn)
{ return NULL; }

static inline struct pci_dev *pci_get_device (unsigned int vendor, unsigned int device, struct pci_dev *from)
{ return NULL; }

static inline struct pci_dev *pci_get_subsys (unsigned int vendor, unsigned int device,
unsigned int ss_vendor, unsigned int ss_device, struct pci_dev *from)
{ return NULL; }

static inline struct pci_dev *pci_get_class(unsigned int class, struct pci_dev *from)
{ return NULL; }

#define pci_dev_present(ids)	(0)
#define pci_dev_put(dev)	do { } while (0)

static inline void pci_set_master(struct pci_dev *dev) { }
static inline int pci_enable_device(struct pci_dev *dev) { return -EIO; }
static inline void pci_disable_device(struct pci_dev *dev) { }
static inline int pci_set_dma_mask(struct pci_dev *dev, u64 mask) { return -EIO; }
static inline int pci_assign_resource(struct pci_dev *dev, int i) { return -EBUSY;}
static inline int __pci_register_driver(struct pci_driver *drv, struct module *owner) { return 0;}
static inline int pci_register_driver(struct pci_driver *drv) { return 0;}
static inline void pci_unregister_driver(struct pci_driver *drv) { }
static inline int pci_find_capability (struct pci_dev *dev, int cap) {return 0; }
static inline int pci_find_next_capability (struct pci_dev *dev, u8 post, int cap) { return 0; }
static inline int pci_find_ext_capability (struct pci_dev *dev, int cap) {return 0; }
static inline const struct pci_device_id *pci_match_device(const struct pci_device_id *ids, const struct pci_dev *dev) { return NULL; }

/* Power management related routines */
static inline int pci_save_state(struct pci_dev *dev) { return 0; }
static inline int pci_restore_state(struct pci_dev *dev) { return 0; }
static inline int pci_set_power_state(struct pci_dev *dev, pci_power_t state) { return 0; }
static inline pci_power_t pci_choose_state(struct pci_dev *dev, pm_message_t state) { return PCI_D0; }
static inline int pci_enable_wake(struct pci_dev *dev, pci_power_t state, int enable) { return 0; }

#define	isa_bridge	((struct pci_dev *)NULL)

#define pci_dma_burst_advice(pdev, strat, strategy_parameter) do { } while (0)

static inline void pci_block_user_cfg_access(struct pci_dev *dev) { }
static inline void pci_unblock_user_cfg_access(struct pci_dev *dev) { }

#endif /* CONFIG_PCI */

/* Include architecture-dependent settings and functions */

#include <asm/pci.h>

/* these helpers provide future and backwards compatibility
 * for accessing popular PCI BAR info */

/**
 *  pci_resource_start - Get start value for a given BAR of a pci device
 *  @dev: Pointer to the pci_dev structure
 *  @bar: Index of the Base Address Register (BAR)
 *
 *  This retrieves the start value of the specified BAR for the pci device
 *  passed in.
 *
 *  SYNOPSIS:
 *  #define pci_resource_start(dev,bar)
 *
 *  RETURN VALUE:
 *  Unsigned int value of the start of the BAR
 *
 */
/* _VMKLNX_CODECHECK_: pci_resource_start */
#define pci_resource_start(dev,bar)   ((dev)->resource[(bar)].start)

/**
 *  pci_resource_end - Get the end value for a given BAR of a pci device
 *  @dev: Pointer to the pci_dev structure
 *  @bar: Index of the Base Address Register (BAR)
 *
 *  This retrieves the end value of the specified BAR for the pci device
 *  passed in.
 *
 *  SYNOPSIS:
 *  #define pci_resource_end(dev,bar)
 *
 *  RETURN VALUE:
 *  Unsigned int value of the start of the BAR
 *
 */
/* _VMKLNX_CODECHECK_: pci_resource_end */
#define pci_resource_end(dev,bar)     ((dev)->resource[(bar)].end)

/**
 *  pci_resource_flags - Get the flags for a given BAR of a pci device
 *  @dev: Pointer to the pci_dev structure
 *  @bar: Index of the Base Address Register (BAR)
 *
 *  This retrieves the flags for a given BAR for of a pci device (dev).
 *
 *  SYNOPSIS:
 *  #define pci_resource_flags(dev,bar)
 *
 *  RETURN VALUE:
 *  unsigned long value of flags
 *
 */
/* _VMKLNX_CODECHECK_: pci_resource_flags */
#define pci_resource_flags(dev,bar)   ((dev)->resource[(bar)].flags)

/**
 *  pci_resource_len - Determine the length of a given BAR of a pci device
 *  @dev: Pointer to the pci_dev structure
 *  @bar: Index of the Base Address Register (BAR)
 *
 *  This determines the length of a given BAR of a pci device (dev).
 *
 *  SYNOPSIS:
 *  #define pci_resource_len(dev,bar)
 *
 *  RETURN VALUE:
 *  Unsigned int bit value of the length
 *
 */
/* _VMKLNX_CODECHECK_: pci_resource_len */
#define pci_resource_len(dev,bar) \
	((pci_resource_start((dev),(bar)) == 0 &&	\
	  pci_resource_end((dev),(bar)) ==		\
	  pci_resource_start((dev),(bar))) ? 0 :	\
	  						\
	 (pci_resource_end((dev),(bar)) -		\
	  pci_resource_start((dev),(bar)) + 1))

/* Similar to the helpers above, these manipulate per-pci_dev
 * driver-specific data.  They are really just a wrapper around
 * the generic device structure functions of these calls.
 */
/**
 *  pci_get_drvdata - get a driver specific data
 *  @pdev: pci device data structure that the driver specific data has been associated
 *
 *  This function returns the driver specific data saved by pci_set_drvdata.
 *
 */
/* _VMKLNX_CODECHECK_: pci_get_drvdata */
static inline void *pci_get_drvdata (struct pci_dev *pdev)
{
	return dev_get_drvdata(&pdev->dev);
}

/**
 *  pci_set_drvdata - set a driver specific data
 *  @pdev: pci device data structure that the driver specific data is associated
 *  @data: driver specific data to be set
 *
 *  This function saves the driver specific data into the specified pci device
 *  data structure.
 *
 */
/* _VMKLNX_CODECHECK_: pci_set_drvdata */
static inline void pci_set_drvdata (struct pci_dev *pdev, void *data)
{
	dev_set_drvdata(&pdev->dev, data);
}

/* If you want to know what to call your pci_dev, ask this function.
 * Again, it's a wrapper around the generic device.
 */
/**                                          
 *  pci_name - Return the bus id (name) of the PCI device
 *  @pdev: the PCI device
 *                                           
 *  Returns the name of the PCI device.
 *                                           
 *  Return Value:
 *  The name of the PCI device                                           
 */                                          
/* _VMKLNX_CODECHECK_: pci_name */
static inline char *pci_name(struct pci_dev *pdev)
{
	return pdev->dev.bus_id;
}


/* Some archs don't want to expose struct resource to userland as-is
 * in sysfs and /proc
 */
#ifndef HAVE_ARCH_PCI_RESOURCE_TO_USER
static inline void pci_resource_to_user(const struct pci_dev *dev, int bar,
                const struct resource *rsrc, resource_size_t *start,
		resource_size_t *end)
{
	*start = rsrc->start;
	*end = rsrc->end;
}
#endif /* HAVE_ARCH_PCI_RESOURCE_TO_USER */


/*
 *  The world is not perfect and supplies us with broken PCI devices.
 *  For at least a part of these bugs we need a work-around, so both
 *  generic (drivers/pci/quirks.c) and per-architecture code can define
 *  fixup hooks to be called for particular buggy devices.
 */

struct pci_fixup {
	u16 vendor, device;	/* You can use PCI_ANY_ID here of course */
	void (*hook)(struct pci_dev *dev);
};

enum pci_fixup_pass {
	pci_fixup_early,	/* Before probing BARs */
	pci_fixup_header,	/* After reading configuration header */
	pci_fixup_final,	/* Final phase of device fixups */
	pci_fixup_enable,	/* pci_enable_device() time */
};

/* Anonymous variables would be nice... */
#define DECLARE_PCI_FIXUP_SECTION(section, name, vendor, device, hook)	\
	static const struct pci_fixup __pci_fixup_##name __attribute_used__ \
	__attribute__((__section__(#section))) = { vendor, device, hook };
#define DECLARE_PCI_FIXUP_EARLY(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_early,			\
			vendor##device##hook, vendor, device, hook)
#define DECLARE_PCI_FIXUP_HEADER(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_header,			\
			vendor##device##hook, vendor, device, hook)
#define DECLARE_PCI_FIXUP_FINAL(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_final,			\
			vendor##device##hook, vendor, device, hook)
#define DECLARE_PCI_FIXUP_ENABLE(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_enable,			\
			vendor##device##hook, vendor, device, hook)


void pci_fixup_device(enum pci_fixup_pass pass, struct pci_dev *dev);

extern int pci_pci_problems;
#define PCIPCI_FAIL		1
#define PCIPCI_TRITON		2
#define PCIPCI_NATOMA		4
#define PCIPCI_VIAETBF		8
#define PCIPCI_VSFX		16
#define PCIPCI_ALIMAGIK		32

#endif /* __KERNEL__ */
#endif /* LINUX_PCI_H */
