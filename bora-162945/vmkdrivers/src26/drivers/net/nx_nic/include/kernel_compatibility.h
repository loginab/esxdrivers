/*
 * Copyright (C) 2003 - 2007 NetXen, Inc.
 * All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 * 
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.
 * 
 * Contact Information:
 *    licensing@netxen.com
 * NetXen, Inc.
 * 3965 Freedom Circle, Fourth floor,
 * Santa Clara, CA 95054
 */
#ifndef __KERNEL_COMPATIBILITY_H__
#define __KERNEL_COMPATIBILITY_H__

#include <linux/version.h>
#include <net/sock.h>

#ifdef NETIF_F_TSO6 
#define TSO_ENABLED(netdev) ((netdev)->features & (NETIF_F_TSO | NETIF_F_TSO6))
#else
#define TSO_ENABLED(netdev) ((netdev)->features & (NETIF_F_TSO))
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
#define DEV_BASE		dev_base
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22) && LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
#define DEV_BASE                dev_base_head
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
#define IP_HDR(skb)             (skb->nh.iph)
#define IPV6_HDR(skb)           (skb->nh.ipv6h)
#define IP_HDR_OFFSET(skb)      (skb->nh.raw - skb->data)
#define TCP_HDR(skb)            (skb->h.th)
#define TCP_HDR_OFFSET(skb)     (skb->h.raw - skb->data)
#define NW_HDR_SIZE             (skb->h.raw - skb->nh.raw)
#define MAC_HDR(skb)		(skb->mac.raw)		
#define SKB_TCP_HDR(skb)	(skb->h.th)
#define SKB_IP_HDR(skb)		(skb->nh.iph)
#define SKB_MAC_HDR(skb)	(skb->mac.raw)
#define TCP_HDR_TYPE		struct tcphdr
#define IP_HDR_TYPE		struct iphdr
#else
#define IP_HDR(skb)             ip_hdr(skb)
#define IPV6_HDR(skb)           ipv6_hdr(skb)
#define IP_HDR_OFFSET(skb)      skb_network_offset(skb)
#define TCP_HDR(skb)            tcp_hdr(skb)
#define TCP_HDR_OFFSET(skb)     skb_transport_offset(skb)
#define NW_HDR_SIZE             skb_transport_offset(skb) - skb_network_offset(skb)
#define MAC_HDR(skb)		skb_mac_header(skb)
#define TCP_HDR_TYPE		char
#define IP_HDR_TYPE		char

#ifdef NET_SKBUFF_DATA_USES_OFFSET
#define SKB_MAC_HDR(skb)	(skb->head + skb->mac_header)
#define SKB_TCP_HDR(skb) 	(skb->head + skb->transport_header)
#define SKB_IP_HDR(skb) 	(skb->head + skb->network_header)
#else
#define SKB_MAC_HDR(skb)	(skb->mac_header)
#define SKB_TCP_HDR(skb)	(skb->transport_header)
#define SKB_IP_HDR(skb) 	(skb->network_header)
#endif

#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)
#define PCI_MODULE_INIT(drv)    pci_module_init(drv)
#else
#define PCI_MODULE_INIT(drv)    pci_register_driver(drv)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,7)
#define	NX_KC_ROUTE_DEVICE(RT)	((RT)->u.dst.dev)
#else
#define	NX_KC_ROUTE_DEVICE(RT)	(((RT)->idev)->dev)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,9)
extern kmem_cache_t *nx_sk_cachep;
#endif

/* 2.4 - 2.6 compatibility macros*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)

#define SK_ERR 		sk_err
#define SK_ERR_SOFT     sk_err_soft
#define SK_STATE_CHANGE sk_state_change
#define SK_PROT		sk_prot
#define SK_PROTOCOL	sk_protocol
#define SK_PROTINFO	sk_protinfo
#define SK_DESTRUCT	sk_destruct
#define SK_FAMILY	sk_family
#define SK_FLAGS	sk_flags
#define SK_DATA_READY   sk_data_ready
#define SK_RCVTIMEO     sk_rcvtimeo
#define SK_SNDTIMEO     sk_sndtimeo
#define SK_STATE        sk_state
#define SK_SHUTDOWN	sk_shutdown
#define SK_SLEEP        sk_sleep
#define SPORT(s)	(inet_sk(s))->num
#define SK_LINGER_FLAG(SK)      sock_flag((SK), SOCK_LINGER)
#define SK_OOBINLINE_FLAG(SK)	sock_flag((SK), SOCK_URGINLINE)
#define SK_KEEPALIVE_FLAG(SK)	sock_flag((SK), SOCK_KEEPOPEN)
#define SK_LINGERTIME   sk_lingertime
#define SK_REUSE	sk_reuse
#define SK_SOCKET       sk_socket
#define SK_RCVBUF      sk_rcvbuf
#define SK_INET(s)      (inet_sk(s))
#define NX_SOCKOPT_TASK	nx_sockopt_thread

#else   /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0) */

#define SK_ERR 		err
#define SK_ERR_SOFT     err_soft
#define SK_STATE_CHANGE state_change
#define SK_PROT		prot
#define SK_PROTOCOL	protocol
#define SK_PROTINFO	user_data
#define SK_DESTRUCT	destruct
#define SK_FAMILY	family
#define SK_FLAGS	dead
#define SK_DATA_READY   data_ready
#define SK_RCVTIMEO     rcvtimeo
#define SK_SNDTIMEO     sndtimeo
#define SK_STATE        state
#define SK_SHUTDOWN	shutdown
#define SK_SLEEP        sleep
#define SPORT(s)	s->num
#define SK_LINGER_FLAG(SK)      ((SK)->linger)
#define SK_OOBINLINE_FLAG(SK)   ((SK)->urginline)
#define SK_KEEPALIVE_FLAG(SK)   ((SK)->keepopen)
#define SK_LINGERTIME   lingertime
#define SK_REUSE	reuse
#define SK_SOCKET       socket
#define SK_RCVBUF      rcvbuf
#define SK_INET(s)      s
#define NX_SOCKOPT_TASK	nx_sockopt_task

#endif  /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0) */

#define NOFC 0
#define FC1  1
#define FC2  2
#define FC3  3
#define FC4  4
#define FC5  5

/*
 * sk_alloc - varieties taken care here.
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,12)) || ((LINUX_VERSION_CODE == KERNEL_VERSION(2,6,11)) && (FEDORA == FC4)) || defined(RDMA_MODULE)

#define	SK_ALLOC(FAMILY, PRIORITY, PROTO, ZERO_IT)		\
	sk_alloc((FAMILY), (PRIORITY), (PROTO), (ZERO_IT))

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)

#define	SK_ALLOC(FAMILY, PRIORITY, PROTO, ZERO_IT)			\
	sk_alloc((FAMILY), (PRIORITY),					\
		 ((ZERO_IT) > 1) ? (PROTO)->slab_obj_size : (ZERO_IT),	\
		 (PROTO)->slab)

#else

#define	SK_ALLOC(FAMILY, PRIORITY, PROTO, ZERO_IT)	\
 	sk_alloc((FAMILY), (PRIORITY), sizeof(struct tcp_sock), nx_sk_cachep)

#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,10)

#define USER_MSS        user_mss
#define TCP_SOCK  	struct tcp_opt
#define PINET6(SK)	((struct tcp6_sock *)(SK))->pinet6
#else
#define USER_MSS        rx_opt.user_mss
#define TCP_SOCK  	struct tcp_sock
#define PINET6(SK)	SK_INET(SK)->pinet6
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,12)
#define MSS_CACHE	mss_cache_std
#else
#define MSS_CACHE       mss_cache
#endif


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)

#define BIND_HASH(SK)   tcp_sk(SK)->bind_hash
#define QUICKACK	ack.pingpong
#define DEFER_ACCEPT	defer_accept
#define SYN_RETRIES	syn_retries
#else
#define BIND_HASH(SK)   inet_csk(SK)->icsk_bind_hash
#define QUICKACK        inet_conn.icsk_ack.pingpong
#define DEFER_ACCEPT	inet_conn.icsk_accept_queue.rskq_defer_accept
#define SYN_RETRIES	inet_conn.icsk_syn_retries

#endif

#if defined(CONFIG_X86_64)
#define PAGE_KERNEL_FLAG  PAGE_KERNEL_EXEC
#else 
#define PAGE_KERNEL_FLAG  PAGE_KERNEL
#endif


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,12)) || ((LINUX_VERSION_CODE == KERNEL_VERSION(2,6,11)) && (FEDORA == FC4))
#define	NX_KC_DST_MTU(DST)	dst_mtu((DST))
#else
#define	NX_KC_DST_MTU(DST)	dst_pmtu((DST))
#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)

typedef struct work_struct	nx_kc_work_queue_t;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)) || (defined(RDMA_MODULE))
#define	NX_KC_INIT_WORK(TASK, CB_FUNC, OBJ)	INIT_WORK((TASK), (CB_FUNC))
#else
#define	NX_KC_INIT_WORK(TASK, CB_FUNC, OBJ)	\
	INIT_WORK((TASK), (CB_FUNC), (OBJ))
#endif
#define	NX_KC_SCHEDULE_WORK(TASK)		schedule_work((TASK))
#define	NX_KC_SCHEDULE_DELAYED_WORK(TASK, DELAY)	\
	schedule_delayed_work((TASK), (DELAY))

#else

typedef struct tq_struct	nx_kc_work_queue_t;
#define	NX_KC_INIT_WORK(TASK, CB_FUNC, OBJ)	\
	INIT_TQUEUE((TASK), (CB_FUNC), (OBJ))
#define	NX_KC_SCHEDULE_WORK(TASK)	schedule_task((TASK))

#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)
#define PM_MESSAGE_T pm_message_t
#else
#define PM_MESSAGE_T u32
#endif

#ifndef module_param
#define module_param(v,t,p) MODULE_PARM(v, "i");
#endif

#endif  /* __KERNEL_COMPATIBILITY_H__ */
