/*
 *	Definitions for the 'struct sk_buff' memory handlers.
 *
 *	Authors:
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Florian La Roche, <rzsfl@rz.uni-sb.de>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_SKBUFF_H
#define _LINUX_SKBUFF_H

#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/time.h>
#include <linux/cache.h>

#include <asm/atomic.h>
#include <asm/types.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/poll.h>
#include <linux/net.h>
#include <linux/textsearch.h>
#include <net/checksum.h>
#include <linux/dmaengine.h>

#if defined(__VMKLNX__)
#include <vmklinux26/vmknetddinetq.h>
#endif /* if defined(__VMKLNX__) */

#include <asm/io.h> /* phys_to_virt */

#define HAVE_ALLOC_SKB		/* For the drivers to know */
#define HAVE_ALIGNABLE_SKB	/* Ditto 8)		   */

#define CHECKSUM_NONE 0
#define CHECKSUM_HW 1
#define CHECKSUM_UNNECESSARY 2
#if defined(__COMPAT_LAYER_2_6_18_PLUS__)
#define CHECKSUM_PARTIAL CHECKSUM_HW
#define CHECKSUM_COMPLETE CHECKSUM_HW
#endif /* defined(__COMPAT_LAYER_2_6_18_PLUS__) */


#define SKB_DATA_ALIGN(X)	(((X) + (SMP_CACHE_BYTES - 1)) & \
				 ~(SMP_CACHE_BYTES - 1))
#define SKB_MAX_ORDER(X, ORDER)	(((PAGE_SIZE << (ORDER)) - (X) - \
				  sizeof(struct skb_shared_info)) & \
				  ~(SMP_CACHE_BYTES - 1))
#define SKB_MAX_HEAD(X)		(SKB_MAX_ORDER((X), 0))
#define SKB_MAX_ALLOC		(SKB_MAX_ORDER(0, 2))

/* A. Checksumming of received packets by device.
 *
 *	NONE: device failed to checksum this packet.
 *		skb->csum is undefined.
 *
 *	UNNECESSARY: device parsed packet and wouldbe verified checksum.
 *		skb->csum is undefined.
 *	      It is bad option, but, unfortunately, many of vendors do this.
 *	      Apparently with secret goal to sell you new device, when you
 *	      will add new protocol to your host. F.e. IPv6. 8)
 *
 *	HW: the most generic way. Device supplied checksum of _all_
 *	    the packet as seen by netif_rx in skb->csum.
 *	    NOTE: Even if device supports only some protocols, but
 *	    is able to produce some skb->csum, it MUST use HW,
 *	    not UNNECESSARY.
 *
 * B. Checksumming on output.
 *
 *	NONE: skb is checksummed by protocol or csum is not required.
 *
 *	HW: device is required to csum packet as seen by hard_start_xmit
 *	from skb->h.raw to the end and to record the checksum
 *	at skb->h.raw+skb->csum.
 *
 *	Device must show its capabilities in dev->features, set
 *	at device setup time.
 *	NETIF_F_HW_CSUM	- it is clever device, it is able to checksum
 *			  everything.
 *	NETIF_F_NO_CSUM - loopback or reliable single hop media.
 *	NETIF_F_IP_CSUM - device is dumb. It is able to csum only
 *			  TCP/UDP over IPv4. Sigh. Vendors like this
 *			  way by an unknown reason. Though, see comment above
 *			  about CHECKSUM_UNNECESSARY. 8)
 *
 *	Any questions? No questions, good. 		--ANK
 */

#if !defined(__VMKLNX__)
struct net_device;

#ifdef CONFIG_NETFILTER
struct nf_conntrack {
	atomic_t use;
	void (*destroy)(struct nf_conntrack *);
};

#ifdef CONFIG_BRIDGE_NETFILTER
struct nf_bridge_info {
	atomic_t use;
	struct net_device *physindev;
	struct net_device *physoutdev;
#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
	struct net_device *netoutdev;
#endif
	unsigned int mask;
	unsigned long data[32 / sizeof(unsigned long)];
};
#endif

#endif
#endif /* !defined(__VMKLNX__) */

struct sk_buff_head {
	/* These two members must be first. */
	struct sk_buff	*next;
	struct sk_buff	*prev;

	__u32		qlen;
	spinlock_t	lock;
};

struct sk_buff;

#if defined(__VMKLNX__)
#define MAX_SKB_FRAGS 24
#else
/* To allow 64K frame to be packed as single skb without frag_list */
#define MAX_SKB_FRAGS (65536/PAGE_SIZE + 2)
#endif

typedef struct skb_frag_struct skb_frag_t;

struct skb_frag_struct {
	struct page *page;
	__u16 page_offset;
	__u16 size;
};

/* This data is invariant across clones and lives at
 * the end of the header data, ie. at skb->end.
 */
struct skb_shared_info {
	atomic_t	dataref;
#if defined (__VMKLNX__)
        /*
         * It may happens we call skb_release_data() but we don't
         * want to release the associated packet handle.
         * This is the case in pskb_expand_head() for example.
         */
        atomic_t        fragsref;
#endif
	unsigned short	nr_frags;
	unsigned short	gso_size;
	/* Warning: this field is not always filled in (UFO)! */
	unsigned short	gso_segs;
	unsigned short  gso_type;
	unsigned int    ip6_frag_id;
	struct sk_buff	*frag_list;
	skb_frag_t	frags[MAX_SKB_FRAGS];
};

/* We divide dataref into two halves.  The higher 16 bits hold references
 * to the payload part of skb->data.  The lower 16 bits hold references to
 * the entire skb->data.  It is up to the users of the skb to agree on
 * where the payload starts.
 *
 * All users must obey the rule that the skb->data reference count must be
 * greater than or equal to the payload reference count.
 *
 * Holding a reference to the payload part means that the user does not
 * care about modifications to the header part of skb->data.
 */
#define SKB_DATAREF_SHIFT 16
#define SKB_DATAREF_MASK ((1 << SKB_DATAREF_SHIFT) - 1)

#if !defined(__VMKLNX__)
struct skb_timeval {
	u32	off_sec;
	u32	off_usec;
};


enum {
	SKB_FCLONE_UNAVAILABLE,
	SKB_FCLONE_ORIG,
	SKB_FCLONE_CLONE,
};
#endif /* !defined(__VMKLNX__) */

enum {
	SKB_GSO_TCPV4 = 1 << 0,
	SKB_GSO_UDP = 1 << 1,

	/* This indicates the skb is from an untrusted source. */
	SKB_GSO_DODGY = 1 << 2,

	/* This indicates the tcp segment has CWR set. */
	SKB_GSO_TCP_ECN = 1 << 3,

	SKB_GSO_TCPV6 = 1 << 4,
};

#if defined(__VMKLNX__)
/** 
 *	struct sk_buff - socket buffer
 *	@next: Next buffer in list
 *	@prev: Previous buffer in list
 *	@dev: Device we arrived on/are leaving by
 *      @pkt : Back pointer to the encapsulating struct
 *      @qid : the channel the packet was received on 
 *      @cache : Cache the skb comes from
 *      @napi : NAPI context the skb comes from
 *	@h: Transport layer header
 *	@nh: Network layer header
 *	@mac: Link layer header
 *	@cb: Control buffer. Free for use by every layer. Put private vars here
 *	@len: Length of actual data
 *	@data_len: Data length
 *	@csum: Checksum
 *	@priority: Packet queueing priority
 *	@ip_summed: Driver fed us an IP checksum
 *      @mhead : Packet flat buffer has been reallocated
 *      @lro_ready : Has the skb already been through lro ?
 *	@protocol: Packet protocol from driver
 *	@truesize: Buffer size 
 *	@users: User count - see {datagram,tcp}.c
 *	@head: Head of buffer
 *	@data: Data head pointer
 *	@tail: Tail pointer
 *	@end: End pointer
 */

/* Current size of this structure is 192 bytes. Increasing it beyond that may impact
 * performance. While changing this structure, make sure that its layout such that
 * compiler does minimal padding and the total size of the structure is minimal. 
 */

struct sk_buff {
	/* These two members must be first. */
	struct sk_buff              *next;
	struct sk_buff              *prev;
	struct net_device           *dev;
        vmk_PktHandle               *pkt;
        vmknetddi_queueops_queueid_t qid;
        u16                          queue_mapping;
        kmem_cache_t                *cache;
        struct napi_struct          *napi;

        union {
		struct tcphdr       *th;
		struct udphdr       *uh;
		struct icmphdr      *icmph;
		struct igmphdr      *igmph;
		struct iphdr        *ipiph;
		struct ipv6hdr      *ipv6h;
		unsigned char       *raw;
	} h;

	union {
		struct iphdr        *iph;
		struct ipv6hdr      *ipv6h;
		struct arphdr       *arph;
		unsigned char       *raw;
	} nh;
        
	union {
	  	unsigned char       *raw;
	} mac;
                
	/*
	 * This is the control buffer. It is free to use for every
	 * layer. Please put your private variables there. If you
	 * want to keep them across layers you have to do a skb_clone()
	 * first. This is owned by whoever has the skb queued ATM.
	 */
	char			     cb[48];

	unsigned int                 len,
				     data_len,
				     csum;
	__u32			     priority;
	__u8                         ip_summed;
        __u8                         mhead;
        __u8                         lro_ready;
        __be16                       protocol;

	/* These elements must be at the end, see alloc_skb() for details.  */
	unsigned int                 truesize;
        atomic_t                     users;
	unsigned char               *head,
				    *data,
				    *tail,
				    *end;
};

#else /* !defined(__VMKLNX__) */

/** 
 *	struct sk_buff - socket buffer
 *	@next: Next buffer in list
 *	@prev: Previous buffer in list
 *	@sk: Socket we are owned by
 *	@tstamp: Time we arrived
 *	@dev: Device we arrived on/are leaving by
 *	@input_dev: Device we arrived on
 *	@h: Transport layer header
 *	@nh: Network layer header
 *	@mac: Link layer header
 *	@dst: destination entry
 *	@sp: the security path, used for xfrm
 *	@cb: Control buffer. Free for use by every layer. Put private vars here
 *	@len: Length of actual data
 *	@data_len: Data length
 *	@mac_len: Length of link layer header
 *	@csum: Checksum
 *	@local_df: allow local fragmentation
 *	@cloned: Head may be cloned (check refcnt to be sure)
 *	@nohdr: Payload reference only, must not modify header
 *	@proto_data_valid: Protocol data validated since arriving at localhost
 *	@proto_csum_blank: Protocol csum must be added before leaving localhost
 *	@pkt_type: Packet class
 *	@fclone: skbuff clone status
 *	@ip_summed: Driver fed us an IP checksum
 *	@priority: Packet queueing priority
 *	@users: User count - see {datagram,tcp}.c
 *	@protocol: Packet protocol from driver
 *	@truesize: Buffer size 
 *	@head: Head of buffer
 *	@data: Data head pointer
 *	@tail: Tail pointer
 *	@end: End pointer
 *	@destructor: Destruct function
 *	@nfmark: Can be used for communication between hooks
 *	@nfct: Associated connection, if any
 *	@ipvs_property: skbuff is owned by ipvs
 *	@nfctinfo: Relationship of this skb to the connection
 *	@nfct_reasm: netfilter conntrack re-assembly pointer
 *	@nf_bridge: Saved data about a bridged frame - see br_netfilter.c
 *	@tc_index: Traffic control index
 *	@tc_verd: traffic control verdict
 *	@dma_cookie: a cookie to one of several possible DMA operations
 *		done by skb DMA functions
 *	@secmark: security marking
 */

/* Current size of this structure is 256 bytes. Increasing it beyond that may impact
 * performance. While changing this structure, make sure that its layout such that
 * compiler does minimal padding and the total size of the structure is minimal. 
 */
struct sk_buff {
	/* These two members must be first. */
	struct sk_buff		*next;
	struct sk_buff		*prev;

	struct sock		*sk;
	struct skb_timeval	tstamp;
	struct net_device	*dev;
	struct net_device	*input_dev;

	union {
		struct tcphdr	*th;
		struct udphdr	*uh;
		struct icmphdr	*icmph;
		struct igmphdr	*igmph;
		struct iphdr	*ipiph;
		struct ipv6hdr	*ipv6h;
		unsigned char	*raw;
	} h;

	union {
		struct iphdr	*iph;
		struct ipv6hdr	*ipv6h;
		struct arphdr	*arph;
		unsigned char	*raw;
	} nh;

	union {
	  	unsigned char 	*raw;
	} mac;

	struct  dst_entry	*dst;
	struct	sec_path	*sp;

	/*
	 * This is the control buffer. It is free to use for every
	 * layer. Please put your private variables there. If you
	 * want to keep them across layers you have to do a skb_clone()
	 * first. This is owned by whoever has the skb queued ATM.
	 */
	char			cb[48];

	unsigned int		len,
				data_len,
				mac_len,
				csum;
	__u32			priority;
	__u8			local_df:1,
				cloned:1,
				ip_summed:2,
				nohdr:1,
				nfctinfo:3;
	__u8			pkt_type:3,
				fclone:2,
#ifndef CONFIG_XEN
				ipvs_property:1;
#else
				ipvs_property:1,
				proto_data_valid:1,
				proto_csum_blank:1;
#endif
	__be16			protocol;

	void			(*destructor)(struct sk_buff *skb);
#ifdef CONFIG_NETFILTER
	struct nf_conntrack	*nfct;
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	struct sk_buff		*nfct_reasm;
#endif
#ifdef CONFIG_BRIDGE_NETFILTER
	struct nf_bridge_info	*nf_bridge;
#endif
	__u32			nfmark;
#endif /* CONFIG_NETFILTER */
#ifdef CONFIG_NET_SCHED
	__u16			tc_index;	/* traffic control index */
#ifdef CONFIG_NET_CLS_ACT
	__u16			tc_verd;	/* traffic control verdict */
#endif
#endif
#ifdef CONFIG_NET_DMA
	dma_cookie_t		dma_cookie;
#endif
#ifdef CONFIG_NETWORK_SECMARK
	__u32			secmark;
#endif


	/* These elements must be at the end, see alloc_skb() for details.  */
	unsigned int		truesize;
	atomic_t		users;
	unsigned char		*head,
				*data,
				*tail,
				*end;
};

#endif /* defined(__VMKLNX__) */

#ifdef __KERNEL__
/*
 *	Handling routines are only of interest to the kernel
 */
#include <linux/slab.h>

#include <asm/system.h>

#if defined(__VMKLNX__)
extern void skb_release_data(struct sk_buff *skb);
#endif /* defined(__VMKLNX__) */

extern void	       __kfree_skb(struct sk_buff *skb);
#if defined(__VMKLNX__)
#define kfree_skb(skb) __kfree_skb(skb)

/**
 *  dev_kfree_skb_irq - frees the given skbuff
 *  @skb: skbuff to free
 *
 *  Frees the given skbuff.
 *
 *  ESX Deviation Notes:
 *  dev_kfree_skb_irq does nothing with regard to interrupt context.
 *  kfree_skb is always called.
 *
 *  SYNOPSIS:
 *  #define dev_kfree_skb_irq(skb)
 *
 *  SEE ALSO:
 *  kfree_skb
 *
 *  RETURN VALUE:
 *  None
 */
/* _VMKLNX_CODECHECK_: dev_kfree_skb_irq */
#define dev_kfree_skb_irq(skb) kfree_skb(skb)

/**
 *  dev_kfree_skb_any - frees the given skbuff
 *  @skb: skbuff to free
 *
 *  Frees the given skbuff.
 *
 *  ESX Deviation Notes:
 *  dev_kfree_skb_any does nothing with regard to interrupt context.
 *  kfree_skb is always called.
 *
 *  SYNOPSIS:
 *  #define dev_kfree_skb_any(skb)
 *
 *  SEE ALSO:
 *  kfree_skb
 *
 *  RETURN VALUE:
 *  None
 */
/* _VMKLNX_CODECHECK_: dev_kfree_skb_any */
#define dev_kfree_skb_any(skb) kfree_skb(skb)
#else
extern void kfree_skb(struct sk_buff *skb);
#endif

#if defined(__VMKLNX__)
struct sk_buff *vmklnx_net_alloc_skb(kmem_cache_t *cache, unsigned int size);
static inline struct sk_buff *__alloc_skb(unsigned int size,
                                          gfp_t priority, int fclone)
{
        return vmklnx_net_alloc_skb(NULL, size);
}
#else /* !defined(__VMKLNX__) */
extern struct sk_buff *__alloc_skb(unsigned int size,
				   gfp_t priority, int fclone);
#endif

/**                                          
 *  alloc_skb - allocate a network buffer       
 *  @size: size to allocate    
 *  @priority: allocation priority 
 *                                           
 *  Allocate a new &sk_buff. The returned buffer has no headroom and a tail 
 *  room of size bytes. The object has a reference count of one. The return 
 *  is the buffer. On a failure the return is NULL. Buffers may only be 
 *  allocated from interrupts using a gfp_mask of GFP_ATOMIC                       
 *  
 *  RETURN VALUE:
 *  The pointer to the newly allocated sk_buff structure.
 *
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: alloc_skb */
static inline struct sk_buff *alloc_skb(unsigned int size,
					gfp_t priority)
{
	return __alloc_skb(size, priority, 0);
}

static inline struct sk_buff *alloc_skb_fclone(unsigned int size,
					       gfp_t priority)
{
	return __alloc_skb(size, priority, 1);
}

extern struct sk_buff *alloc_skb_from_cache(kmem_cache_t *cp,
					    unsigned int size,
					    gfp_t priority,
					    int fclone);
extern void	       kfree_skbmem(struct sk_buff *skb);
extern struct sk_buff *skb_clone(struct sk_buff *skb,
				 gfp_t priority);
extern struct sk_buff *skb_copy(const struct sk_buff *skb,
				gfp_t priority);
extern struct sk_buff *pskb_copy(struct sk_buff *skb,
				 gfp_t gfp_mask);
extern int	       pskb_expand_head(struct sk_buff *skb,
					int nhead, int ntail,
					gfp_t gfp_mask);
extern struct sk_buff *skb_realloc_headroom(struct sk_buff *skb,
					    unsigned int headroom);
extern struct sk_buff *skb_copy_expand(const struct sk_buff *skb,
				       int newheadroom, int newtailroom,
				       gfp_t priority);
extern int	       skb_pad(struct sk_buff *skb, int pad);

/**
 *  dev_kfree_skb - frees the given skbuff
 *  @a: skbuff to free
 *
 *  Frees the given skbuff.
 *
 *  SYNOPSIS:
 *  #define dev_kfree_skb(a)
 *
 *  SEE ALSO:
 *  kfree_skb
 *
 *  RETURN VALUE:
 *  None
 */
/* _VMKLNX_CODECHECK_: dev_kfree_skb */
#define dev_kfree_skb(a)	kfree_skb(a)
extern void	      skb_over_panic(struct sk_buff *skb, int len,
				     void *here);
extern void	      skb_under_panic(struct sk_buff *skb, int len,
				      void *here);
extern void	      skb_truesize_bug(struct sk_buff *skb);

static inline void skb_truesize_check(struct sk_buff *skb)
{
	if (unlikely((int)skb->truesize < sizeof(struct sk_buff) + skb->len))
		skb_truesize_bug(skb);
}

#if !defined(__VMKLNX__)
extern int skb_append_datato_frags(struct sock *sk, struct sk_buff *skb,
			int getfrag(void *from, char *to, int offset,
			int len,int odd, struct sk_buff *skb),
			void *from, int length);

struct skb_seq_state
{
	__u32		lower_offset;
	__u32		upper_offset;
	__u32		frag_idx;
	__u32		stepped_offset;
	struct sk_buff	*root_skb;
	struct sk_buff	*cur_skb;
	__u8		*frag_data;
};

extern void	      skb_prepare_seq_read(struct sk_buff *skb,
					   unsigned int from, unsigned int to,
					   struct skb_seq_state *st);
extern unsigned int   skb_seq_read(unsigned int consumed, const u8 **data,
				   struct skb_seq_state *st);
extern void	      skb_abort_seq_read(struct skb_seq_state *st);

extern unsigned int   skb_find_text(struct sk_buff *skb, unsigned int from,
				    unsigned int to, struct ts_config *config,
				    struct ts_state *state);
#endif /* !defined(__VMKLNX__) */

/* Internal */
#if defined(__VMKLNX__)
/**
 *  skb_shinfo - Return the skb_shared_info of the sk_buff
 *
 *  Return the skb_shared_info of the sk_buff.
 *
 *  ARGUMENTS:
 *  SKB -  sk_buff
 *
 *  SYNOPSIS:
 *     #define skb_shinfo(SKB)
 *
 *  RETURN VALUE:
 *   pointer to skb_shared_info
 *
 */
 /* _VMKLNX_CODECHECK_: skb_shinfo */

#define skb_shinfo(SKB)    ((struct skb_shared_info *)((SKB) + 1))
#else /* !defined(__VMKLNX__) */
#define skb_shinfo(SKB)    ((struct skb_shared_info *)((SKB)->end))
#endif /* defined(__VMKLNX__) */

#if defined(__VMKLNX__)
/**                                          
 *  vmknetddi_queueops_set_skb_queueid - set the qid for the socket buffer 
 *  @skb: the socket buffer to be set
 *  @qid: a channel packet was received on
 *                                           
 *  Set the qid of the given socket buffer to the given qid. 
 *                                           
 *  RETURN VALUE:                     
 *  This function does not return a value. 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: vmknetddi_queueops_set_skb_queueid */
static inline void
vmknetddi_queueops_set_skb_queueid(struct sk_buff *skb, 
                                   vmknetddi_queueops_queueid_t qid)
{
	skb->qid = qid;
}
#endif /* defined(__VMKLNX__) */

/**
 *	skb_queue_empty - check if a queue is empty
 *	@list: queue head
 *
 *	Returns true if the queue is empty, false otherwise.
 */
static inline int skb_queue_empty(const struct sk_buff_head *list)
{
	return list->next == (struct sk_buff *)list;
}

/**
 *	skb_get - reference buffer
 *	@skb: buffer to reference
 *
 *	Makes another reference to a socket buffer and returns a pointer
 *	to the buffer.
 */
static inline struct sk_buff *skb_get(struct sk_buff *skb)
{
	atomic_inc(&skb->users);
	return skb;
}

/*
 * If users == 1, we are the only owner and are can avoid redundant
 * atomic change.
 */

#if defined(__VMKLNX__)

/*
 * vmklinux26 doesn't support properly skb cloning and data reference counter.
 * Indeed our skb_shinfo() implementation is not based on skb->end thereby
 * after a clone the 2 skbs cannot share the same shinfo structure.
 * Don't know about the need of supporting skb_clone() but we may need to revisit
 * that.
 */

#define skb_cloned(skb) 0
/**
 *  skb_header_cloned - non-operational macro
 *  @skb: Ignored
 *
 *  A non-operational macro provided to help reduce
 *  kernel ifdefs. It is not supported in this release of ESX.
 *
 *  SYNOPSIS:
 *    #define skb_header_cloned(skb)
 *
 *  ESX Deviation Notes:
 *  sk_buff cloning is not supported in ESX.
 *
 *  RETURN VALUE:
 *  Does not return any value
 *
 */
/* _VMKLNX_CODECHECK_: skb_header_cloned */
#define skb_header_cloned(skb) 0
#else /* !defined(__VMKLNX__) */
/**
 *	skb_cloned - is the buffer a clone
 *	@skb: buffer to check
 *
 *	Returns true if the buffer was generated with skb_clone() and is
 *	one of multiple shared copies of the buffer. Cloned buffers are
 *	shared data so must not be written to under normal circumstances.
 */
static inline int skb_cloned(const struct sk_buff *skb)
{
	return skb->cloned &&
	       (atomic_read(&skb_shinfo(skb)->dataref) & SKB_DATAREF_MASK) != 1;
}

/**
 *	skb_header_cloned - is the header a clone
 *	@skb: buffer to check
 *
 *	Returns true if modifying the header part of the buffer requires
 *	the data to be copied.
 */
static inline int skb_header_cloned(const struct sk_buff *skb)
{
	int dataref;

	if (!skb->cloned)
		return 0;

	dataref = atomic_read(&skb_shinfo(skb)->dataref);
	dataref = (dataref & SKB_DATAREF_MASK) - (dataref >> SKB_DATAREF_SHIFT);
	return dataref != 1;
}

/**
 *	skb_header_release - release reference to header
 *	@skb: buffer to operate on
 *
 *	Drop a reference to the header part of the buffer.  This is done
 *	by acquiring a payload reference.  You must not read from the header
 *	part of skb->data after this.
 */
static inline void skb_header_release(struct sk_buff *skb)
{
	BUG_ON(skb->nohdr);
	skb->nohdr = 1;
	atomic_add(1 << SKB_DATAREF_SHIFT, &skb_shinfo(skb)->dataref);
}
#endif /* !defined(__VMKLNX__) */

/**
 *	skb_shared - is the buffer shared
 *	@skb: buffer to check
 *
 *	Returns true if more than one person has a reference to this
 *	buffer.
 */
static inline int skb_shared(const struct sk_buff *skb)
{
	return atomic_read(&skb->users) != 1;
}

#if !defined(__VMKLNX__)
/**
 *	skb_share_check - check if buffer is shared and if so clone it
 *	@skb: buffer to check
 *	@pri: priority for memory allocation
 *
 *	If the buffer is shared the buffer is cloned and the old copy
 *	drops a reference. A new clone with a single reference is returned.
 *	If the buffer is not shared the original buffer is returned. When
 *	being called from interrupt status or with spinlocks held pri must
 *	be GFP_ATOMIC.
 *
 *	NULL is returned on a memory allocation failure.
 */
static inline struct sk_buff *skb_share_check(struct sk_buff *skb,
					      gfp_t pri)
{
	might_sleep_if(pri & __GFP_WAIT);
	if (skb_shared(skb)) {
		struct sk_buff *nskb = skb_clone(skb, pri);
		kfree_skb(skb);
		skb = nskb;
	}
	return skb;
}

/*
 *	Copy shared buffers into a new sk_buff. We effectively do COW on
 *	packets to handle cases where we have a local reader and forward
 *	and a couple of other messy ones. The normal one is tcpdumping
 *	a packet thats being forwarded.
 */

/**
 *	skb_unshare - make a copy of a shared buffer
 *	@skb: buffer to check
 *	@pri: priority for memory allocation
 *
 *	If the socket buffer is a clone then this function creates a new
 *	copy of the data, drops a reference count on the old copy and returns
 *	the new copy with the reference count at 1. If the buffer is not a clone
 *	the original buffer is returned. When called with a spinlock held or
 *	from interrupt state @pri must be %GFP_ATOMIC
 *
 *	%NULL is returned on a memory allocation failure.
 */
static inline struct sk_buff *skb_unshare(struct sk_buff *skb,
					  gfp_t pri)
{
	might_sleep_if(pri & __GFP_WAIT);
	if (skb_cloned(skb)) {
		struct sk_buff *nskb = skb_copy(skb, pri);
		kfree_skb(skb);	/* Free our shared copy */
		skb = nskb;
	}
	return skb;
}
#endif /* !defined(__VMKLNX__) */

/**
 *	skb_peek
 *	@list_: list to peek at
 *
 *	Peek an &sk_buff. Unlike most other operations you _MUST_
 *	be careful with this one. A peek leaves the buffer on the
 *	list and someone else may run off with it. You must hold
 *	the appropriate locks or have a private queue to do this.
 *
 *	Returns %NULL for an empty list or a pointer to the head element.
 *	The reference count is not incremented and the reference is therefore
 *	volatile. Use with caution.
 */
static inline struct sk_buff *skb_peek(struct sk_buff_head *list_)
{
	struct sk_buff *list = ((struct sk_buff *)list_)->next;
	if (list == (struct sk_buff *)list_)
		list = NULL;
	return list;
}

/**
 *	skb_peek_tail
 *	@list_: list to peek at
 *
 *	Peek an &sk_buff. Unlike most other operations you _MUST_
 *	be careful with this one. A peek leaves the buffer on the
 *	list and someone else may run off with it. You must hold
 *	the appropriate locks or have a private queue to do this.
 *
 *	Returns %NULL for an empty list or a pointer to the tail element.
 *	The reference count is not incremented and the reference is therefore
 *	volatile. Use with caution.
 */
static inline struct sk_buff *skb_peek_tail(struct sk_buff_head *list_)
{
	struct sk_buff *list = ((struct sk_buff *)list_)->prev;
	if (list == (struct sk_buff *)list_)
		list = NULL;
	return list;
}

/**
 *	skb_queue_len	- get queue length
 *	@list_: list to measure
 *
 *	Return the length of an &sk_buff queue.
 */
static inline __u32 skb_queue_len(const struct sk_buff_head *list_)
{
	return list_->qlen;
}

/*
 * This function creates a split out lock class for each invocation;
 * this is needed for now since a whole lot of users of the skb-queue
 * infrastructure in drivers have different locking usage (in hardirq)
 * than the networking core (in softirq only). In the long run either the
 * network layer or drivers should need annotation to consolidate the
 * main types of usage into 3 classes.
 */
static inline void skb_queue_head_init(struct sk_buff_head *list)
{
	spin_lock_init(&list->lock);
	list->prev = list->next = (struct sk_buff *)list;
	list->qlen = 0;
}

/*
 *	Insert an sk_buff at the start of a list.
 *
 *	The "__skb_xxxx()" functions are the non-atomic ones that
 *	can only be called with interrupts disabled.
 */

/**
 *	__skb_queue_after - queue a buffer at the list head
 *	@list: list to use
 *	@prev: place after this buffer
 *	@newsk: buffer to queue
 *
 *	Queue a buffer int the middle of a list. This function takes no locks
 *	and you must therefore hold required locks before calling it.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */
static inline void __skb_queue_after(struct sk_buff_head *list,
				     struct sk_buff *prev,
				     struct sk_buff *newsk)
{
	struct sk_buff *next;
	list->qlen++;

	next = prev->next;
	newsk->next = next;
	newsk->prev = prev;
	next->prev  = prev->next = newsk;
}

/**
 *	__skb_queue_head - queue a buffer at the list head
 *	@list: list to use
 *	@newsk: buffer to queue
 *
 *	Queue a buffer at the start of a list. This function takes no locks
 *	and you must therefore hold required locks before calling it.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */
extern void skb_queue_head(struct sk_buff_head *list, struct sk_buff *newsk);
static inline void __skb_queue_head(struct sk_buff_head *list,
				    struct sk_buff *newsk)
{
	__skb_queue_after(list, (struct sk_buff *)list, newsk);
}

/**
 *	__skb_queue_tail - queue a buffer at the list tail
 *	@list: list to use
 *	@newsk: buffer to queue
 *
 *	Queue a buffer at the end of a list. This function takes no locks
 *	and you must therefore hold required locks before calling it.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */
extern void skb_queue_tail(struct sk_buff_head *list, struct sk_buff *newsk);
static inline void __skb_queue_tail(struct sk_buff_head *list,
				   struct sk_buff *newsk)
{
	struct sk_buff *prev, *next;

	list->qlen++;
	next = (struct sk_buff *)list;
	prev = next->prev;
	newsk->next = next;
	newsk->prev = prev;
	next->prev  = prev->next = newsk;
}


/**
 *	__skb_dequeue - remove from the head of the queue
 *	@list: list to dequeue from
 *
 *	Remove the head of the list. This function does not take any locks
 *	so must be used with appropriate locks held only.
 *
 *      RETURN VALUE:
 *	The head item is returned or %NULL if the list is empty.
 */
extern struct sk_buff *skb_dequeue(struct sk_buff_head *list);
/* _VMKLNX_CODECHECK_: __skb_dequeue */
static inline struct sk_buff *__skb_dequeue(struct sk_buff_head *list)
{
	struct sk_buff *next, *prev, *result;

	prev = (struct sk_buff *) list;
	next = prev->next;
	result = NULL;
	if (next != prev) {
		result	     = next;
		next	     = next->next;
		list->qlen--;
		next->prev   = prev;
		prev->next   = next;
		result->next = result->prev = NULL;
	}
	return result;
}


/*
 *	Insert a packet on a list.
 */
extern void        skb_insert(struct sk_buff *old, struct sk_buff *newsk, struct sk_buff_head *list);
static inline void __skb_insert(struct sk_buff *newsk,
				struct sk_buff *prev, struct sk_buff *next,
				struct sk_buff_head *list)
{
	newsk->next = next;
	newsk->prev = prev;
	next->prev  = prev->next = newsk;
	list->qlen++;
}

/*
 *	Place a packet after a given packet in a list.
 */
extern void	   skb_append(struct sk_buff *old, struct sk_buff *newsk, struct sk_buff_head *list);
static inline void __skb_append(struct sk_buff *old, struct sk_buff *newsk, struct sk_buff_head *list)
{
	__skb_insert(newsk, old, old->next, list);
}

/*
 * remove sk_buff from list. _Must_ be called atomically, and with
 * the list known..
 */
extern void	   skb_unlink(struct sk_buff *skb, struct sk_buff_head *list);
static inline void __skb_unlink(struct sk_buff *skb, struct sk_buff_head *list)
{
	struct sk_buff *next, *prev;

	list->qlen--;
	next	   = skb->next;
	prev	   = skb->prev;
	skb->next  = skb->prev = NULL;
	next->prev = prev;
	prev->next = next;
}


/* XXX: more streamlined implementation */

/**
 *	__skb_dequeue_tail - remove from the tail of the queue
 *	@list: list to dequeue from
 *
 *	Remove the tail of the list. This function does not take any locks
 *	so must be used with appropriate locks held only. The tail item is
 *	returned or %NULL if the list is empty.
 */
extern struct sk_buff *skb_dequeue_tail(struct sk_buff_head *list);
static inline struct sk_buff *__skb_dequeue_tail(struct sk_buff_head *list)
{
	struct sk_buff *skb = skb_peek_tail(list);
	if (skb)
		__skb_unlink(skb, list);
	return skb;
}


static inline int skb_is_nonlinear(const struct sk_buff *skb)
{
	return skb->data_len;
}

/**                                          
 *  skb_headlen - Get the size of @skb's header
 *  @skb: Pointer to socket buffer
 *                                           
 *  Compute the skbhead room size and return it
 *
 *  RETURN VALUE:
 *  The skbhead room size
 */                                          
/* _VMKLNX_CODECHECK_: skb_headlen */
static inline unsigned int skb_headlen(const struct sk_buff *skb)
{
	return skb->len - skb->data_len;
}

static inline int skb_pagelen(const struct sk_buff *skb)
{
	int i, len = 0;

	for (i = (int)skb_shinfo(skb)->nr_frags - 1; i >= 0; i--)
		len += skb_shinfo(skb)->frags[i].size;
	return len + skb_headlen(skb);
}

/* TODO: dilpreet do we need this function? */
/**                                          
 *  skb_fill_page_desc - Populate frags in shared info's frag
 *  @skb: Pointer to a socket buffer
 *  @i: Value to put in nr_frags
 *  @page: Value for frag->page
 *  @off: Value for frag->page_offset
 *  @size: Value for frag->size
 *                                           
 *  Utility function to populate the frags in the shared_info
 */                                          
/* _VMKLNX_CODECHECK_: skb_fill_page_desc */
static inline void skb_fill_page_desc(struct sk_buff *skb, int i,
				      struct page *page, int off, int size)
{
	skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

	frag->page		  = page;
	frag->page_offset	  = off;
	frag->size		  = size;
	skb_shinfo(skb)->nr_frags = i + 1;
}

#define SKB_PAGE_ASSERT(skb) 	BUG_ON(skb_shinfo(skb)->nr_frags)
#define SKB_FRAG_ASSERT(skb) 	BUG_ON(skb_shinfo(skb)->frag_list)
#define SKB_LINEAR_ASSERT(skb)  BUG_ON(skb_is_nonlinear(skb))

/*
 *	Add data to an sk_buff
 */
static inline unsigned char *__skb_put(struct sk_buff *skb, unsigned int len)
{
	unsigned char *tmp = skb->tail;
	SKB_LINEAR_ASSERT(skb);
	skb->tail += len;
	skb->len  += len;
	return tmp;
}

/**
 *	skb_put - add data to a buffer
 *	@skb: buffer to use
 *	@len: amount of data to add
 *
 *	This function extends the used data area of the buffer. If this would
 *	exceed the total buffer size the kernel will panic. A pointer to the
 *	first byte of the extra data is returned.
 *
 *	RETURN VALUE:
 *	  Pointer to the first byte of the extra data
 */
/* _VMKLNX_CODECHECK_: skb_put */
static inline unsigned char *skb_put(struct sk_buff *skb, unsigned int len)
{
	unsigned char *tmp = skb->tail;
	SKB_LINEAR_ASSERT(skb);
	skb->tail += len;
	skb->len  += len;
	if (unlikely(skb->tail>skb->end))
		skb_over_panic(skb, len, current_text_addr());
	return tmp;
}

static inline unsigned char *__skb_push(struct sk_buff *skb, unsigned int len)
{
	skb->data -= len;
	skb->len  += len;
	return skb->data;
}

/**
 *	skb_push - add data to the start of a buffer
 *	@skb: buffer to use
 *	@len: amount of data to add
 *
 *	This function extends the used data area of the buffer at the buffer
 *	start. If this would exceed the total buffer headroom the kernel will
 *	panic.
 *
 *	RETURN VALUE:
 *	pointer to the first byte of the extra data
 */
/* _VMKLNX_CODECHECK_: skb_push */

static inline unsigned char *skb_push(struct sk_buff *skb, unsigned int len)
{
	skb->data -= len;
	skb->len  += len;
	if (unlikely(skb->data<skb->head))
		skb_under_panic(skb, len, current_text_addr());
	return skb->data;
}

static inline unsigned char *__skb_pull(struct sk_buff *skb, unsigned int len)
{
	skb->len -= len;
	BUG_ON(skb->len < skb->data_len);
	return skb->data += len;
}

/**
 *	skb_pull - remove data from the start of a buffer
 *	@skb: buffer to use
 *	@len: amount of data to remove
 *
 *	This function removes data from the start of a buffer, returning
 *	the memory to the headroom. A pointer to the next data in the buffer
 *	is returned. Once the data has been pulled future pushes will overwrite
 *	the old data.
 *
 *	RETURN VALUE:
 *	  non-zero : Pointer to the next data in the buffer
 *	  zero : @len > socket buffer len
 */
/* _VMKLNX_CODECHECK_: skb_pull */
static inline unsigned char *skb_pull(struct sk_buff *skb, unsigned int len)
{
	return unlikely(len > skb->len) ? NULL : __skb_pull(skb, len);
}

extern unsigned char *__pskb_pull_tail(struct sk_buff *skb, int delta);

static inline unsigned char *__pskb_pull(struct sk_buff *skb, unsigned int len)
{
	if (len > skb_headlen(skb) &&
	    !__pskb_pull_tail(skb, len-skb_headlen(skb)))
		return NULL;
	skb->len -= len;
	return skb->data += len;
}

static inline unsigned char *pskb_pull(struct sk_buff *skb, unsigned int len)
{
	return unlikely(len > skb->len) ? NULL : __pskb_pull(skb, len);
}

/**                                          
 *  pskb_may_pull - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: pskb_may_pull */
static inline int pskb_may_pull(struct sk_buff *skb, unsigned int len)
{
	if (likely(len <= skb_headlen(skb)))
		return 1;
	if (unlikely(len > skb->len))
		return 0;
	return __pskb_pull_tail(skb, len-skb_headlen(skb)) != NULL;
}

/**
 *	skb_headroom - bytes at buffer head
 *	@skb: buffer to check
 *
 *	Return the number of bytes of free space at the head of an &sk_buff.
 */
static inline int skb_headroom(const struct sk_buff *skb)
{
	return skb->data - skb->head;
}

/**
 *	skb_tailroom - bytes at buffer end
 *	@skb: buffer to check
 *
 *	Return the number of bytes of free space at the tail of an sk_buff
 */
/* _VMKLNX_CODECHECK_: skb_tailroom */
static inline int skb_tailroom(const struct sk_buff *skb)
{
	return skb_is_nonlinear(skb) ? 0 : skb->end - skb->tail;
}

/**
 *	skb_reserve - adjust headroom
 *	@skb: buffer to alter
 *	@len: bytes to move
 *
 *	Increase the headroom of an empty &sk_buff by reducing the tail
 *	room. This is only allowed for an empty buffer.
 */
/* _VMKLNX_CODECHECK_: skb_reserve */
static inline void skb_reserve(struct sk_buff *skb, int len)
{
	skb->data += len;
	skb->tail += len;
}

#if defined(__COMPAT_LAYER_2_6_18_PLUS__)
static inline unsigned char *skb_transport_header(const struct sk_buff *skb)
{
        return skb->h.raw;
}

static inline void skb_reset_transport_header(struct sk_buff *skb)
{
        skb->h.raw = skb->data;
}

static inline void skb_set_transport_header(struct sk_buff *skb,
                                            const int offset)
{
        skb->h.raw = skb->data + offset;
}

static inline unsigned char *skb_network_header(const struct sk_buff *skb)
{
        return skb->nh.raw;
}

static inline void skb_reset_network_header(struct sk_buff *skb)
{
        skb->nh.raw = skb->data;
}

static inline void skb_set_network_header(struct sk_buff *skb, const int offset)
{
        skb->nh.raw = skb->data + offset;
}

static inline unsigned char *skb_mac_header(const struct sk_buff *skb)
{
        return skb->mac.raw;
}

static inline int skb_mac_header_was_set(const struct sk_buff *skb)
{
        return skb_mac_header(skb) != NULL;
}

static inline void skb_reset_mac_header(struct sk_buff *skb)
{
        skb->mac.raw = skb->data;
}

static inline void skb_set_mac_header(struct sk_buff *skb, const int offset)
{
        skb->mac.raw = skb->data + offset;
}

static inline int skb_transport_offset(const struct sk_buff *skb)
{
        return skb_transport_header(skb) - skb->data;
}

static inline u32 skb_network_header_len(const struct sk_buff *skb)
{
        return skb->h.raw - skb->nh.raw;
}

static inline int skb_network_offset(const struct sk_buff *skb)
{
        return skb_network_header(skb) - skb->data;
}

/**
 *  skb_end_pointer - Return the pointer to the end of the sk_buff
 *  @skb: sk_buff
 *
 *  Returns the end element of the sk_buff
 *
 *  RETURN VALUE:
 *  pointer to the end element
 *
 */
 /* _VMKLNX_CODECHECK_: skb_end_pointer */

static inline unsigned char *skb_end_pointer(const struct sk_buff *skb)
{
        return skb->end;
}
#endif /* defined(__COMPAT_LAYER_2_6_18_PLUS__) */

/*
 * CPUs often take a performance hit when accessing unaligned memory
 * locations. The actual performance hit varies, it can be small if the
 * hardware handles it or large if we have to take an exception and fix it
 * in software.
 *
 * Since an ethernet header is 14 bytes network drivers often end up with
 * the IP header at an unaligned offset. The IP header can be aligned by
 * shifting the start of the packet by 2 bytes. Drivers should do this
 * with:
 *
 * skb_reserve(NET_IP_ALIGN);
 *
 * The downside to this alignment of the IP header is that the DMA is now
 * unaligned. On some architectures the cost of an unaligned DMA is high
 * and this cost outweighs the gains made by aligning the IP header.
 * 
 * Since this trade off varies between architectures, we allow NET_IP_ALIGN
 * to be overridden.
 */
#ifndef NET_IP_ALIGN
#define NET_IP_ALIGN	2
#endif

/*
 * The networking layer reserves some headroom in skb data (via
 * dev_alloc_skb). This is used to avoid having to reallocate skb data when
 * the header has to grow. In the default case, if the header has to grow
 * 16 bytes or less we avoid the reallocation.
 *
 * Unfortunately this headroom changes the DMA alignment of the resulting
 * network packet. As for NET_IP_ALIGN, this unaligned DMA is expensive
 * on some architectures. An architecture can override this value,
 * perhaps setting it to a cacheline in size (since that will maintain
 * cacheline alignment of the DMA). It must be a power of 2.
 *
 * Various parts of the networking layer expect at least 16 bytes of
 * headroom, you should not reduce this.
 */
#ifndef NET_SKB_PAD
#define NET_SKB_PAD	16
#endif

extern int ___pskb_trim(struct sk_buff *skb, unsigned int len);

static inline void __skb_trim(struct sk_buff *skb, unsigned int len)
{
	if (unlikely(skb->data_len)) {
		WARN_ON(1);
		return;
	}
	skb->len  = len;
	skb->tail = skb->data + len;
}

/**
 *	skb_trim - remove end from a buffer
 *	@skb: buffer to alter
 *	@len: new length
 *
 *	Cut the length of a buffer down by removing data from the tail. If
 *	the buffer is already under the length specified it is not modified.
 *	The skb must be linear.
 */
/* _VMKLNX_CODECHECK_: skb_trim */
static inline void skb_trim(struct sk_buff *skb, unsigned int len)
{
	if (skb->len > len)
		__skb_trim(skb, len);
}

static inline int __pskb_trim(struct sk_buff *skb, unsigned int len)
{
	if (skb->data_len)
		return ___pskb_trim(skb, len);
	__skb_trim(skb, len);
	return 0;
}

/**                                          
 *  pskb_trim - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: pskb_trim */
static inline int pskb_trim(struct sk_buff *skb, unsigned int len)
{
	return (len < skb->len) ? __pskb_trim(skb, len) : 0;
}

/**
 *	pskb_trim_unique - remove end from a paged unique (not cloned) buffer
 *	@skb: buffer to alter
 *	@len: new length
 *
 *	This is identical to pskb_trim except that the caller knows that
 *	the skb is not cloned so we should never get an error due to out-
 *	of-memory.
 */
static inline void pskb_trim_unique(struct sk_buff *skb, unsigned int len)
{
	int err = pskb_trim(skb, len);
	BUG_ON(err);
}

#if !defined(__VMKLNX__)
/**
 *	skb_orphan - orphan a buffer
 *	@skb: buffer to orphan
 *
 *	If a buffer currently has an owner then we call the owner's
 *	destructor function and make the @skb unowned. The buffer continues
 *	to exist but is no longer charged to its former owner.
 */
static inline void skb_orphan(struct sk_buff *skb)
{
	if (skb->destructor)
		skb->destructor(skb);
	skb->destructor = NULL;
	skb->sk		= NULL;
}

/**
 *	__skb_queue_purge - empty a list
 *	@list: list to empty
 *
 *	Delete all buffers on an &sk_buff list. Each buffer is removed from
 *	the list and one reference dropped. This function does not take the
 *	list lock and the caller must hold the relevant locks to use it.
 */
extern void skb_queue_purge(struct sk_buff_head *list);
static inline void __skb_queue_purge(struct sk_buff_head *list)
{
	struct sk_buff *skb;
	while ((skb = __skb_dequeue(list)) != NULL)
		kfree_skb(skb);
}
#endif /* !defined(__VMKLNX__) */

#ifndef CONFIG_HAVE_ARCH_DEV_ALLOC_SKB
/**
 *	__dev_alloc_skb - allocate an skbuff for receiving
 *	@length: length to allocate
 *	@gfp_mask: get_free_pages mask, passed to alloc_skb
 *
 *	Allocate a new &sk_buff and assign it a usage count of one. The
 *	buffer has unspecified headroom built in. Users should allocate
 *	the headroom they think they need without accounting for the
 *	built in space. The built in space is used for optimisations.
 *
 *	%NULL is returned if there is no free memory.
 */
static inline struct sk_buff *__dev_alloc_skb(unsigned int length,
					      gfp_t gfp_mask)
{
	struct sk_buff *skb = alloc_skb(length + NET_SKB_PAD, gfp_mask);
	if (likely(skb))
		skb_reserve(skb, NET_SKB_PAD);
	return skb;
}
#else
extern struct sk_buff *__dev_alloc_skb(unsigned int length, gfp_t gfp_mask);
#endif

/**
 *	dev_alloc_skb - allocate an skbuff for receiving
 *	@length: length to allocate
 *
 *	Allocate a new &sk_buff and assign it a usage count of one. The
 *	buffer has unspecified headroom built in. Users should allocate
 *	the headroom they think they need without accounting for the
 *	built in space. The built in space is used for optimisations.
 *
 *	%NULL is returned if there is no free memory. Although this function
 *	allocates memory it can be called from an interrupt.
 */
/* _VMKLNX_CODECHECK_: dev_alloc_skb */
static inline struct sk_buff *dev_alloc_skb(unsigned int length)
{
	return __dev_alloc_skb(length, GFP_ATOMIC);
}

extern struct sk_buff *__netdev_alloc_skb(struct net_device *dev,
		unsigned int length, gfp_t gfp_mask);

/**
 *	netdev_alloc_skb - allocate an skbuff for rx on a specific device
 *	@dev: network device to receive on
 *	@length: length to allocate
 *
 *	Allocate a new &sk_buff and assign it a usage count of one. The
 *	buffer has unspecified headroom built in. Users should allocate
 *	the headroom they think they need without accounting for the
 *	built in space. The built in space is used for optimisations.
 *
 *      Return Value:
 *	%NULL is returned if there is no free memory. Although this function
 *	allocates memory it can be called from an interrupt.
 */
/* _VMKLNX_CODECHECK_: netdev_alloc_skb */
static inline struct sk_buff *netdev_alloc_skb(struct net_device *dev,
		unsigned int length)
{
	return __netdev_alloc_skb(dev, length, GFP_ATOMIC);
}

/**
 *	skb_cow - copy header of skb when it is required
 *	@skb: buffer to cow
 *	@headroom: needed headroom
 *
 *	If the skb passed lacks sufficient headroom or its data part
 *	is shared, data is reallocated. If reallocation fails, an error
 *	is returned and original skb is not changed.
 *
 *	The result is skb with writable area skb->head...skb->tail
 *	and at least @headroom of space at head.
 */
static inline int skb_cow(struct sk_buff *skb, unsigned int headroom)
{
	int delta = (headroom > NET_SKB_PAD ? headroom : NET_SKB_PAD) -
			skb_headroom(skb);

	if (delta < 0)
		delta = 0;

	if (delta || skb_cloned(skb))
		return pskb_expand_head(skb, (delta + (NET_SKB_PAD-1)) &
				~(NET_SKB_PAD-1), 0, GFP_ATOMIC);
	return 0;
}

/**
 *	skb_padto	- pad an skbuff up to a minimal size
 *	@skb: buffer to pad
 *	@len: minimal length
 *
 *	Pads up a buffer to ensure the trailing bytes exist and are
 *	blanked. If the buffer already contains sufficient data it
 *	is untouched. Otherwise it is extended. Returns zero on
 *	success. The skb is freed on error.
 */
 
static inline int skb_padto(struct sk_buff *skb, unsigned int len)
{
	unsigned int size = skb->len;
	if (likely(size >= len))
		return 0;
	return skb_pad(skb, len-size);
}

static inline int skb_add_data(struct sk_buff *skb,
			       char __user *from, int copy)
{
	const int off = skb->len;

	if (skb->ip_summed == CHECKSUM_NONE) {
		int err = 0;
#if defined (__VMKLNX__)
		unsigned int csum = csum_and_copy_from_user(
					  (const unsigned char *)from,
					  skb_put(skb, copy),
					  copy, 0, &err);
#else
                unsigned int csum = csum_and_copy_from_user(from,
                                                            skb_put(skb, copy),
                                                            copy, 0, &err);
#endif
		if (!err) {
			skb->csum = csum_block_add(skb->csum, csum, off);
			return 0;
		}
	} else if (!copy_from_user(skb_put(skb, copy), from, copy))
		return 0;

	__skb_trim(skb, off);
	return -EFAULT;
}

static inline int skb_can_coalesce(struct sk_buff *skb, int i,
				   struct page *page, int off)
{
	if (i) {
		struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[i - 1];
		return page == frag->page &&
		       off == frag->page_offset + frag->size;
	}
	return 0;
}

static inline int __skb_linearize(struct sk_buff *skb)
{
	return __pskb_pull_tail(skb, skb->data_len) ? 0 : -ENOMEM;
}

/**
 *	skb_linearize - convert paged skb to linear one
 *	@skb: buffer to linearize
 *
 *	Convert the skb that is fragmented in multiple pages to a linear one
 *
 *	RETURN VALUE:
 *	If there is no free memory -ENOMEM is returned, otherwise zero
 *	is returned and the old skb data released.
 */
/* _VMKLNX_CODECHECK_: skb_linearize */
static inline int skb_linearize(struct sk_buff *skb)
{
	return skb_is_nonlinear(skb) ? __skb_linearize(skb) : 0;
}

/**
 *	skb_linearize_cow - make sure skb is linear and writable
 *	@skb: buffer to process
 *
 *	If there is no free memory -ENOMEM is returned, otherwise zero
 *	is returned and the old skb data released.
 */
static inline int skb_linearize_cow(struct sk_buff *skb)
{
	return skb_is_nonlinear(skb) || skb_cloned(skb) ?
	       __skb_linearize(skb) : 0;
}

/**
 *	skb_postpull_rcsum - update checksum for received skb after pull
 *	@skb: buffer to update
 *	@start: start of data before pull
 *	@len: length of data pulled
 *
 *	After doing a pull on a received packet, you need to call this to
 *	update the CHECKSUM_HW checksum, or set ip_summed to CHECKSUM_NONE
 *	so that it can be recomputed from scratch.
 */

static inline void skb_postpull_rcsum(struct sk_buff *skb,
				      const void *start, unsigned int len)
{
	if (skb->ip_summed == CHECKSUM_HW)
		skb->csum = csum_sub(skb->csum, csum_partial(start, len, 0));
}

unsigned char *skb_pull_rcsum(struct sk_buff *skb, unsigned int len);

/**
 *	pskb_trim_rcsum - trim received skb and update checksum
 *	@skb: buffer to trim
 *	@len: new length
 *
 *	This is exactly the same as pskb_trim except that it ensures the
 *	checksum of received packets are still valid after the operation.
 */

static inline int pskb_trim_rcsum(struct sk_buff *skb, unsigned int len)
{
	if (likely(len >= skb->len))
		return 0;
	if (skb->ip_summed == CHECKSUM_HW)
		skb->ip_summed = CHECKSUM_NONE;
	return __pskb_trim(skb, len);
}

static inline void *kmap_skb_frag(const skb_frag_t *frag)
{
#ifdef CONFIG_HIGHMEM
	BUG_ON(in_irq());

	local_bh_disable();
#endif
	return kmap_atomic(frag->page, KM_SKB_DATA_SOFTIRQ);
}

static inline void kunmap_skb_frag(void *vaddr)
{
	kunmap_atomic(vaddr, KM_SKB_DATA_SOFTIRQ);
#ifdef CONFIG_HIGHMEM
	local_bh_enable();
#endif
}

#define skb_queue_walk(queue, skb) \
		for (skb = (queue)->next;					\
		     prefetch(skb->next), (skb != (struct sk_buff *)(queue));	\
		     skb = skb->next)

#define skb_queue_reverse_walk(queue, skb) \
		for (skb = (queue)->prev;					\
		     prefetch(skb->prev), (skb != (struct sk_buff *)(queue));	\
		     skb = skb->prev)


#if !defined(__VMKLNX__)
extern struct sk_buff *skb_recv_datagram(struct sock *sk, unsigned flags,
					 int noblock, int *err);
extern unsigned int    datagram_poll(struct file *file, struct socket *sock,
				     struct poll_table_struct *wait);
extern int	       skb_copy_datagram_iovec(const struct sk_buff *from,
					       int offset, struct iovec *to,
					       int size);
extern int	       skb_copy_and_csum_datagram_iovec(struct sk_buff *skb,
							int hlen,
							struct iovec *iov);
extern void	       skb_free_datagram(struct sock *sk, struct sk_buff *skb);
extern void	       skb_kill_datagram(struct sock *sk, struct sk_buff *skb,
					 unsigned int flags);
extern unsigned int    skb_checksum(const struct sk_buff *skb, int offset,
				    int len, unsigned int csum);
#endif /* !defined(__VMKLNX__) */
extern int	       skb_copy_bits(const struct sk_buff *skb, int offset,
				     void *to, int len);
#if !defined(__VMKLNX__)
extern int	       skb_store_bits(const struct sk_buff *skb, int offset,
				      void *from, int len);
extern unsigned int    skb_copy_and_csum_bits(const struct sk_buff *skb,
					      int offset, u8 *to, int len,
					      unsigned int csum);
extern void	       skb_copy_and_csum_dev(const struct sk_buff *skb, u8 *to);
extern void	       skb_split(struct sk_buff *skb,
				 struct sk_buff *skb1, const u32 len);

extern struct sk_buff *skb_segment(struct sk_buff *skb, int features);
#endif /* !defined(__VMKLNX__) */

static inline void *skb_header_pointer(const struct sk_buff *skb, int offset,
				       int len, void *buffer)
{
	int hlen = skb_headlen(skb);

	if (hlen - offset >= len)
		return skb->data + offset;

	if (skb_copy_bits(skb, offset, buffer, len) < 0)
		return NULL;

	return buffer;
}

#if defined(__COMPAT_LAYER_2_6_18_PLUS__)
/**
 *  skb_copy_from_linear_data - copy the data of specified length from sk_buff
 *  to a buffer
 *  @skb: pointer to the sk_buff containing the data
 *  @to: buffer where the data should be copied to
 *  @len: number of bytes to be copied
 *
 *  Copy the data of specified length @len from sk_buff @skb to a buffer @to.
 *
 *  RETURN VALUE:
 *  None
 *
 */
 /* _VMKLNX_CODECHECK_: skb_copy_from_linear_data */

static inline void skb_copy_from_linear_data(const struct sk_buff *skb,
                                             void *to,
                                             const unsigned int len)
{
        memcpy(to, skb->data, len);
}

/**
 *  skb_copy_from_linear_data_offset - copy the data of specified length from a 
 *  specified offset in sk_buff to a buffer
 *  @skb: pointer to the sk_buff containing the data
 *  @offset: offset in the sk_buff data
 *  @to: buffer where the data should be copied to
 *  @len: number of bytes to be copied
 *
 *  Copy the data of specified length @len from offset @offset in sk_buff @skb
 *  to a buffer @to.
 *
 *  RETURN VALUE:
 *  None
 *
 */
 /* _VMKLNX_CODECHECK_: skb_copy_from_linear_data_offset */

static inline void skb_copy_from_linear_data_offset(const struct sk_buff *skb,
                                                    const int offset, void *to,
                                                    const unsigned int len)
{
        memcpy(to, skb->data + offset, len);
}

/**
 *  skb_copy_to_linear_data - copy the data of specified length from buffer
 *  to sk_buff
 *  @skb: pointer to the sk_buff
 *  @from: buffer where the data should be copied from
 *  @len: number of bytes to be copied
 *
 *  Copy the data of specified length @len from buffer @from to sk_buff @skb.
 *
 *  RETURN VALUE:
 *  None
 *
 */
 /* _VMKLNX_CODECHECK_: skb_copy_to_linear_data */

static inline void skb_copy_to_linear_data(struct sk_buff *skb,
                                           const void *from,
                                           const unsigned int len)
{
        memcpy(skb->data, from, len);
}

/**
 *  skb_copy_to_linear_data_offset - copy the data of specified length from 
 *  buffer to sk_buff at a specified offset
 *  @skb: pointer to the sk_buff
 *  @offset: offset in the sk_buff data
 *  @from: buffer where the data should be copied from
 *  @len: number of bytes to be copied
 *
 *  Copy the data of specified length @len from buffer @from to sk_buff @skb at a
 *  specified offset @offset.
 *
 *  RETURN VALUE:
 *  None
 *
 */
 /* _VMKLNX_CODECHECK_: skb_copy_to_linear_data_offset */

static inline void skb_copy_to_linear_data_offset(struct sk_buff *skb,
                                                  const int offset,
                                                  const void *from,
                                                  const unsigned int len)
{
        memcpy(skb->data + offset, from, len);
}
#endif /* defined(__COMPAT_LAYER_2_6_18_PLUS__) */

#if !defined(__VMKLNX__)

extern void skb_init(void);
extern void skb_add_mtu(int mtu);

/**
 *	skb_get_timestamp - get timestamp from a skb
 *	@skb: skb to get stamp from
 *	@stamp: pointer to struct timeval to store stamp in
 *
 *	Timestamps are stored in the skb as offsets to a base timestamp.
 *	This function converts the offset back to a struct timeval and stores
 *	it in stamp.
 */
static inline void skb_get_timestamp(const struct sk_buff *skb, struct timeval *stamp)
{
	stamp->tv_sec  = skb->tstamp.off_sec;
	stamp->tv_usec = skb->tstamp.off_usec;
}

/**
 * 	skb_set_timestamp - set timestamp of a skb
 *	@skb: skb to set stamp of
 *	@stamp: pointer to struct timeval to get stamp from
 *
 *	Timestamps are stored in the skb as offsets to a base timestamp.
 *	This function converts a struct timeval to an offset and stores
 *	it in the skb.
 */
static inline void skb_set_timestamp(struct sk_buff *skb, const struct timeval *stamp)
{
	skb->tstamp.off_sec  = stamp->tv_sec;
	skb->tstamp.off_usec = stamp->tv_usec;
}

extern void __net_timestamp(struct sk_buff *skb);

extern unsigned int __skb_checksum_complete(struct sk_buff *skb);

/**
 *	skb_checksum_complete - Calculate checksum of an entire packet
 *	@skb: packet to process
 *
 *	This function calculates the checksum over the entire packet plus
 *	the value of skb->csum.  The latter can be used to supply the
 *	checksum of a pseudo header as used by TCP/UDP.  It returns the
 *	checksum.
 *
 *	For protocols that contain complete checksums such as ICMP/TCP/UDP,
 *	this function can be used to verify that checksum on received
 *	packets.  In that case the function should return zero if the
 *	checksum is correct.  In particular, this function will return zero
 *	if skb->ip_summed is CHECKSUM_UNNECESSARY which indicates that the
 *	hardware has already verified the correctness of the checksum.
 */
static inline unsigned int skb_checksum_complete(struct sk_buff *skb)
{
	return skb->ip_summed != CHECKSUM_UNNECESSARY &&
		__skb_checksum_complete(skb);
}

struct tux_req_struct;

#ifdef CONFIG_NETFILTER
static inline void nf_conntrack_put(struct nf_conntrack *nfct)
{
	if (nfct && atomic_dec_and_test(&nfct->use))
		nfct->destroy(nfct);
}
static inline void nf_conntrack_get(struct nf_conntrack *nfct)
{
	if (nfct)
		atomic_inc(&nfct->use);
}
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
static inline void nf_conntrack_get_reasm(struct sk_buff *skb)
{
	if (skb)
		atomic_inc(&skb->users);
}
static inline void nf_conntrack_put_reasm(struct sk_buff *skb)
{
	if (skb)
		kfree_skb(skb);
}
#endif
#ifdef CONFIG_BRIDGE_NETFILTER
static inline void nf_bridge_put(struct nf_bridge_info *nf_bridge)
{
	if (nf_bridge && atomic_dec_and_test(&nf_bridge->use))
		kfree(nf_bridge);
}
static inline void nf_bridge_get(struct nf_bridge_info *nf_bridge)
{
	if (nf_bridge)
		atomic_inc(&nf_bridge->use);
}
#endif /* CONFIG_BRIDGE_NETFILTER */
static inline void nf_reset(struct sk_buff *skb)
{
	nf_conntrack_put(skb->nfct);
	skb->nfct = NULL;
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	nf_conntrack_put_reasm(skb->nfct_reasm);
	skb->nfct_reasm = NULL;
#endif
#ifdef CONFIG_BRIDGE_NETFILTER
	nf_bridge_put(skb->nf_bridge);
	skb->nf_bridge = NULL;
#endif
}

#else /* CONFIG_NETFILTER */
static inline void nf_reset(struct sk_buff *skb) {}
#endif /* CONFIG_NETFILTER */

#ifdef CONFIG_NETWORK_SECMARK
static inline void skb_copy_secmark(struct sk_buff *to, const struct sk_buff *from)
{
	to->secmark = from->secmark;
}

static inline void skb_init_secmark(struct sk_buff *skb)
{
	skb->secmark = 0;
}
#else
static inline void skb_copy_secmark(struct sk_buff *to, const struct sk_buff *from)
{ }

static inline void skb_init_secmark(struct sk_buff *skb)
{ }
#endif
#endif /* !defined(__VMKLNX__) */

static inline void skb_set_queue_mapping(struct sk_buff *skb, u16 queue_mapping)
{
	skb->queue_mapping = queue_mapping;
}

static inline u16 skb_get_queue_mapping(struct sk_buff *skb)
{
	return skb->queue_mapping;
}

static inline void skb_copy_queue_mapping(struct sk_buff *to, const struct sk_buff *from)
{
	to->queue_mapping = from->queue_mapping;
}

/**                                          
 *  skb_is_gso - Is Generic Segmentation Offload needed ?
 *  @skb: Pointer to socket buffer
 *                                           
 *  This function returns the size of the GSO. A non-zero size implies that
 *  segmentation is needed.
 *
 *  RETURN VALUE:
 *    non-zero - segmentation is needed
 *    zero - segmentation is not needed
 */                                          
/* _VMKLNX_CODECHECK_: skb_is_gso */
static inline int skb_is_gso(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->gso_size;
}

#endif	/* __KERNEL__ */
#endif	/* _LINUX_SKBUFF_H */
