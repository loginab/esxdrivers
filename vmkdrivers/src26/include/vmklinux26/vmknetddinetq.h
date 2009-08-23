/*
 * Netqueue implementation for vmklinux
 * 
 *    Multi-queue NICs have multiple receive and/or transmit queues that 
 *    can be programmed individually. Netqueue API provides a mechanism 
 *    to discover and program these queues. 
 */

#ifndef _LINUX_NETQUEUE_H
#define _LINUX_NETQUEUE_H
#ifdef __VMKLNX__

#define __VMKNETDDI_QUEUEOPS__ 

/*
 *	IEEE 802.3 Ethernet magic constants.  The frame sizes omit the preamble
 *	and FCS/CRC (frame check sequence). 
 */

#define ETH_ALEN	6		/* Octets in one ethernet addr	 */

/*
 * WARNING: Binary compatiability tripping point.
 *
 * Version here must matchup with that on the kenel side for *a* 
 * given release.
 */
#define VMKNETDDI_QUEUEOPS_MAJOR_VER (2)
#define VMKNETDDI_QUEUEOPS_MINOR_VER (0)

#define VMKNETDDI_QUEUEOPS_OK  (0 )
#define VMKNETDDI_QUEUEOPS_ERR (-1)

struct sk_buff;
struct net_device;
struct net_device_stats;

/*
 * WARNING: Be careful changing constants, binary comtiability may be broken.
 */

#define VMKNETDDI_QUEUEOPS_FEATURE_NONE     \
				((vmknetddi_queueops_features_t)0x0)
#define VMKNETDDI_QUEUEOPS_FEATURE_RXQUEUES \
				((vmknetddi_queueops_features_t)0x1)
#define VMKNETDDI_QUEUEOPS_FEATURE_TXQUEUES \
				((vmknetddi_queueops_features_t)0x2)

typedef enum vmknetddi_queueops_filter_class {
	VMKNETDDI_QUEUEOPS_FILTER_NONE = 0x0,       /* Invalid filter */
	VMKNETDDI_QUEUEOPS_FILTER_MACADDR = 0x1,    /* Mac address filter */
	VMKNETDDI_QUEUEOPS_FILTER_VLAN = 0x2,       /* Vlan tag filter */
	VMKNETDDI_QUEUEOPS_FILTER_VLANMACADDR = 0x3,/* Vlan tag + 
                                                           Mac addr filter */
} vmknetddi_queueops_filter_class_t;

typedef enum vmknetddi_queueops_queue_type {
	VMKNETDDI_QUEUEOPS_QUEUE_TYPE_INVALID = 0x0, /* Invalid queue type */
	VMKNETDDI_QUEUEOPS_QUEUE_TYPE_RX = 0x1,      /* Rx queue */
	VMKNETDDI_QUEUEOPS_QUEUE_TYPE_TX = 0x2,      /* Tx queue */
} vmknetddi_queueops_queue_t;

typedef u32 vmknetddi_queueops_queueid_t;
typedef u32 vmknetddi_queueops_filterid_t;
typedef u8  vmknetddi_queueops_tx_priority_t;

#define VMKNETDDI_QUEUEOPS_INVALID_QUEUEID ((vmknetddi_queueops_queueid_t)0)
#define VMKNETDDI_QUEUEOPS_MK_TX_QUEUEID(n) ((vmknetddi_queueops_queueid_t) \
				((VMKNETDDI_QUEUEOPS_QUEUE_TYPE_TX<<16) | \
				 (n & 0xffff)))
#define VMKNETDDI_QUEUEOPS_MK_RX_QUEUEID(n) ((vmknetddi_queueops_queueid_t) \
				((VMKNETDDI_QUEUEOPS_QUEUE_TYPE_RX<<16) | \
				 (n & 0xffff)))

#define VMKNETDDI_QUEUEOPS_QUEUEID_VAL(cid) ((u16)((cid) & 0xffff))

#define VMKNETDDI_QUEUEOPS_MK_FILTERID(n) \
				((vmknetddi_queueops_filterid_t)(n))
#define VMKNETDDI_QUEUEOPS_FILTERID_VAL(fid)  ((u16)(fid))

#define VMKNETDDI_QUEUEOPS_IS_TX_QUEUEID(n) (((n) & 0x00ff0000) == \
				(VMKNETDDI_QUEUEOPS_QUEUE_TYPE_TX << 16))

#define VMKNETDDI_QUEUEOPS_IS_RX_QUEUEID(n) (((n) & 0x00ff0000) == \
				(VMKNETDDI_QUEUEOPS_QUEUE_TYPE_RX << 16))

#define VMKNETDDI_QUEUEOPS_TX_QUEUEID_SET_QIDX(qid, idx) (qid | (idx << 24))
#define VMKNETDDI_QUEUEOPS_TX_QUEUEID_GET_QIDX(qid)      ((qid & 0xff000000) >> 24)

typedef struct vmknetddi_queueops_filter {
	vmknetddi_queueops_filter_class_t class; /* Filter class */
	u8 active;                                /* Is active? */

	union {
		u8 macaddr[ETH_ALEN];             /* MAC address */
		u16 vlan_id;                      /* VLAN Id */
		struct {
			u8 macaddr[ETH_ALEN];     /* MAC address */
			u16 vlan_id;              /* VLAN id */
		} vlanmac;
	} u;
} vmknetddi_queueops_filter_t;

typedef unsigned long long vmknetddi_queueops_features_t;


/* WARNING: Order below should not be changed  */
typedef enum _vmknetddi_queueops_op {
	/* Get supported netqueue api version */	
	VMKNETDDI_QUEUEOPS_OP_GET_VERSION       = 0x1,
	/* Get features supported by implementation */
	VMKNETDDI_QUEUEOPS_OP_GET_FEATURES      = 0x2, 
	/* Get supported (tx or rx) queue count */
	VMKNETDDI_QUEUEOPS_OP_GET_QUEUE_COUNT   = 0x3, 
	/* Get supported (tx or rx) filters count */
	VMKNETDDI_QUEUEOPS_OP_GET_FILTER_COUNT  = 0x4,
	/* Allocate a queue */
	VMKNETDDI_QUEUEOPS_OP_ALLOC_QUEUE       = 0x5,
	/* Free allocated queue */
	VMKNETDDI_QUEUEOPS_OP_FREE_QUEUE        = 0x6,
	/* Get vector assigned to the queue */
	VMKNETDDI_QUEUEOPS_OP_GET_QUEUE_VECTOR  = 0x7,
	/* Get default queue */
	VMKNETDDI_QUEUEOPS_OP_GET_DEFAULT_QUEUE = 0x8,
	/* Apply rx filter on a queue */
	VMKNETDDI_QUEUEOPS_OP_APPLY_RX_FILTER   = 0x9,
	/* Remove appled rx filter */
	VMKNETDDI_QUEUEOPS_OP_REMOVE_RX_FILTER  = 0xa,
	/* Get queue stats */
	VMKNETDDI_QUEUEOPS_OP_GET_STATS         = 0xb,
        /* Set tx queue priority */
        VMKNETDDI_QUEUEOPS_OP_SET_TX_PRIORITY   = 0xc,
} vmknetddi_queueops_op_t;

typedef struct _vmknetddi_queueop_get_version_args_t {
	u16                     major;       /* OUT: Major version */
	u16                     minor;       /* OUT: Minor version */
} vmknetddi_queueop_get_version_args_t ;

typedef struct _vmknetddi_queueop_get_features_args_t {
	struct net_device		*netdev;   /* IN:  Netdev */
	vmknetddi_queueops_features_t	features;  /* OUT: Features supported */
} vmknetddi_queueop_get_features_args_t ;

typedef struct _vmknetddi_queueop_get_queue_count_args_t {
	struct net_device		*netdev; /* IN:  Netdev */
	vmknetddi_queueops_queue_t	type;    /* IN:  Type of queue */
	u16				count;   /* OUT: Queues supported */
} vmknetddi_queueop_get_queue_count_args_t ;

typedef struct _vmknetddi_queueop_get_filter_count_args_t {
	struct net_device		*netdev; /* IN:  Netdev */
	vmknetddi_queueops_queue_t	type;    /* IN:  Type of queue */
	u16				count;   /* OUT: Filters supported */
} vmknetddi_queueop_get_filter_count_args_t ;

typedef struct _vmknetddi_queueop_alloc_queue_args_t {
	struct net_device       	*netdev;  /* IN:  Netdev */
	vmknetddi_queueops_queue_t      type;     /* IN:  Type of queue */
        struct napi_struct              *napi;    /* OUT: Napi struct for this queue */
        u16                             queue_mapping; /* OUT: Linux tx queue mapping */
	vmknetddi_queueops_queueid_t    queueid;  /* OUT: New queue id */
} vmknetddi_queueop_alloc_queue_args_t ;

typedef struct _vmknetddi_queueop_free_queue_args_t {
	struct net_device       	*netdev; /* IN:  Netdev */
	vmknetddi_queueops_queueid_t    queueid; /* IN: Queue id */
} vmknetddi_queueop_free_queue_args_t ;

typedef struct _vmknetddi_queueop_get_queue_vector_args_t {
	struct net_device       	*netdev; /* IN:  Netdev */
	vmknetddi_queueops_queueid_t	queueid; /* IN: Queue id */
	u16                     	vector;  /* OUT: Assigned interrupt vector */
} vmknetddi_queueop_get_queue_vector_args_t ;

typedef struct _vmknetddi_queueop_get_default_queue_args_t {
	struct net_device       	*netdev;  /* IN:  Netdev */
	vmknetddi_queueops_queue_t	type;     /* IN:  Type of queue */
        struct napi_struct              *napi;    /* OUT: Napi struct for this queue */
        u16                             queue_mapping; /* OUT: Linux tx queue mapping */
	vmknetddi_queueops_queueid_t	queueid;  /* OUT: Default queue id */
} vmknetddi_queueop_get_default_queue_args_t ;

typedef struct _vmknetddi_queueop_apply_rx_filter_args_t {
	struct net_device       	*netdev;   /* IN: Netdev */
	vmknetddi_queueops_queueid_t	queueid;   /* IN: Queue id */
	vmknetddi_queueops_filter_t	filter;    /* IN: Filter */
	vmknetddi_queueops_filterid_t	filterid;  /* OUT: Filter id */
} vmknetddi_queueop_apply_rx_filter_args_t ;

typedef struct _vmknetddi_queueop_remove_rx_filter_args_t {
	struct net_device       	*netdev;   /* IN: Netdev */
	vmknetddi_queueops_queueid_t	queueid;   /* IN: Queue id */
	vmknetddi_queueops_filterid_t	filterid;  /* IN: Filter id */
} vmknetddi_queueop_remove_rx_filter_args_t ;

typedef struct _vmknetddi_queueop_get_stats_args_t {
	struct net_device        	*netdev;   /* IN: Netdev */
	vmknetddi_queueops_queueid_t    queueid;   /* IN: Queue id */
	struct net_device_stats  	*stats;    /* OUT: Queue statistics */
} vmknetddi_queueop_get_stats_args_t ;

typedef struct _vmknetddi_queueop_set_tx_priority_args_t {
        struct net_device               *netdev;   /* IN: Netdev */
	vmknetddi_queueops_queueid_t     queueid;  /* IN: Queue id */
	vmknetddi_queueops_tx_priority_t priority; /* IN: Queue priority */
} vmknetddi_queueop_set_tx_priority_args_t;


typedef int (*vmknetddi_queueops_f)(vmknetddi_queueops_op_t op, 
				     void *args);

static inline int
vmknetddi_queueops_version(vmknetddi_queueop_get_version_args_t *args)
{
	args->major = VMKNETDDI_QUEUEOPS_MAJOR_VER;
	args->minor = VMKNETDDI_QUEUEOPS_MINOR_VER;
	return 0;
}

static inline void
vmknetddi_queueops_set_filter_active(vmknetddi_queueops_filter_t *f)
{
	f->active = 1;
}

static inline void
vmknetddi_queueops_set_filter_inactive(vmknetddi_queueops_filter_t *f)
{
	f->active = 0;
}

static inline int
vmknetddi_queueops_is_filter_active(vmknetddi_queueops_filter_t *f)
{
	return f->active;
}

/**                                          
 *  vmknetddi_queueops_filter_class - get filter class 
 *  @f: a given vmknetddi_queueops_filter_t    
 *                                           
 *  Get the class of a given filter. 
 *                                           
 *  RETURN VALUE:                     
 *  0x0 (Invalid filter class)
 *  0x1 (Mac address filter class)
 *  0x2 (Vlan tag filter class)
 *  0x3 (Vlan tag + Mac address filter class) 
 *                                           
 */
/* _VMKLNX_CODECHECK_: vmknetddi_queueops_get_filter_class */
static inline vmknetddi_queueops_filter_class_t
vmknetddi_queueops_get_filter_class(vmknetddi_queueops_filter_t *f)
{
	return f->class;
}

/**
 *  vmknetddi_queueops_set_filter_macaddr - set class and MAC address for the filter
 *  @f: a given vmknetddi_queueops_filter_t
 *  @macaddr: a given MAC address
 *
 *  Set the filter class to VMKNETDDI_QUEUEOPS_FILTER_MACADDR and
 *  the MAC address of the filter.
 *
 *  RETURN VALUE:
 *  This function does not return a value.
 *
 */
/* _VMKLNX_CODECHECK_: vmknetddi_queueops_set_filter_macaddr */
static inline void
vmknetddi_queueops_set_filter_macaddr(vmknetddi_queueops_filter_t *f,
			      	       u8 *macaddr)
{
	f->class = VMKNETDDI_QUEUEOPS_FILTER_MACADDR;
	memcpy(f->u.macaddr, macaddr, ETH_ALEN);
}

/**                                          
 *  vmknetddi_queueops_get_filter_macaddr - get the MAC address for a given filter 
 *  @f: a pointer to the given filter
 *                                           
 *  Get the Mac address of the filter if the filter class is Mac filter or
 *  Vlan tag + Mac filter. 
 *                                           
 *  RETURN VALUE:                     
 *  the pointer to the Mac address if the filter class is Mac filter or
 *                     Vlan tag + Mac address filter
 *  NULL otherwise
 *                                           
 */
/* _VMKLNX_CODECHECK_: vmknetddi_queueops_get_filter_macaddr */
static inline u8 *
vmknetddi_queueops_get_filter_macaddr(vmknetddi_queueops_filter_t *f)
{
	if (f->class == VMKNETDDI_QUEUEOPS_FILTER_MACADDR) {
		return f->u.macaddr;
	}
	else if (f->class == VMKNETDDI_QUEUEOPS_FILTER_VLANMACADDR) {
		return f->u.vlanmac.macaddr;
	}
	else {
		return NULL;
	}
}

static inline void
vmknetddi_queueops_set_filter_vlan(vmknetddi_queueops_filter_t *f,
			   	    u16 vlanid)
{
	f->class = VMKNETDDI_QUEUEOPS_FILTER_VLAN;
	f->u.vlan_id = vlanid;
}

static inline void
vmknetddi_queueops_set_filter_vlanmacaddr(vmknetddi_queueops_filter_t *f,
					   u8 *macaddr,
					   u16 vlanid)
{
	f->class = VMKNETDDI_QUEUEOPS_FILTER_VLANMACADDR;
	memcpy(f->u.vlanmac.macaddr, macaddr, ETH_ALEN);
	f->u.vlanmac.vlan_id = vlanid;
}

static inline u16 
vmknetddi_queueops_get_filter_vlanid(vmknetddi_queueops_filter_t *f)
{
	if (f->class == VMKNETDDI_QUEUEOPS_FILTER_VLAN) {
		return f->u.vlan_id;
	}
	else if (f->class == VMKNETDDI_QUEUEOPS_FILTER_VLANMACADDR) {
		return f->u.vlanmac.vlan_id;
	}
	else {
		return 0;
	}
}

#endif	/* _LINUX_NETQUEUE_H */
#endif	/* __VMKLNX__ */
