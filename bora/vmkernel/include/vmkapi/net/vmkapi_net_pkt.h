/* **********************************************************
 * Copyright 2006 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Pkt                                                            */ /**
 * \addtogroup Network
 *@{
 * \defgroup Pkt Packet Management
 *@{ 
 *
 * \par The packet representation:
 *
 * - Frags:\n
 *     A packet is a set of 1 to n frags not contiguous.\n
 *\n
 *     There isn't a main buffer and some frags if needed, here a\n
 *     complete packet  is just a set of memory frags.\n
 *\n
 *     Note that a packet cannot carry around more than\n
 *     VMK_PKT_FRAGS_MAX_LENGTH frags.\n
 *\n
 * - Mapped Data:\n
 *     A packet has a mapped area which corresponds to the\n
 *     (beginning of the) first frag. In order to access the frags\n
 *     the frag's machine addresses need to be converted to virtual\n
 *     addresses first.\n
 *\n
 *     Be aware that the mapped area may be larger than the \n
 *     pkt's length.  When processing data within the mapped\n
 *     data area, use MIN(frameLen, frameMappedLen)\n
 * \n
 *     \code
 *     int i;
 *     unsigned int len;
 *     vmk_uint8 *frameVA;
 *
 *     len = MIN(vmk_PktFrameMappedLenGet(pkt), vmk_PktFrameLenGet(pkt));
 *     frameVA = (vmk_uint8 *) vmk_PktFrameMappedPointerGet(pkt);
 *
 *     printk("frame mapped len = 0x%x\n", len);
 *     for (i = 0; i < len; i++) {
 *        printk("0x%x ", frameVA[i]);
 *     }
 *     printk("\n");
 *     \endcode
 *\n
 * - Frame:\n
 *     This represents the actual network frame nested in the packet frags.\n
 *\n
 * - Headroom:\n
 *     This represents the padding space available in front of the packet\n
 *     contents.\n
 *\n
 *     There is no pre-allocated headroom for a fresh allocated packet and\n
 *     the users need to define it manually with\n
 *     vmk_PushHeadroom()/vmk_PullHeadroom(). Note that increasing the\n
 *     headroom length doesn't increase the packet frame length as well\n
 *     and that it is the responsibility of the caller to do that when all\n
 *     the modifications on the frame are done.\n
 *
 * \par Packet Rx/Tx process:
 *
 * The Rx process is pretty simple and consists in pushing a packet in the
 * vmkernel. To do so you just need to use vmk_PktQueueForRxProcess() 
 * precising the packet you would like to push and the uplink it is coming
 * from.
 *
 * The Tx process is a little bit different as vmkernel want through your
 * module to send a packet. But the question is what are you supposed to do
 * with this packet when you are done?
 *
 * The answer to that is to push the packet for Tx completion in order to
 * release it properly, notifying some interested part of vmkernel at the
 * same time. To do so you need to use vmk_PktQueueForTxComp().
 * 
 ***********************************************************************
 */

#ifndef _VMKAPI_NET_PKT_H_
#define _VMKAPI_NET_PKT_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMNIXMOD
#define INCLUDE_ALLOW_VMMEXT
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_MODULE
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_const.h"
#include "base/vmkapi_types.h"
#include "base/vmkapi_cslist.h"
#include "base/vmkapi_memory.h"
#include "base/vmkapi_scatter_gather.h"

#include "net/vmkapi_net_types.h"

/** Maximum number of frags a packet can carry around */
#define VMK_PKT_FRAGS_MAX_LENGTH 24

/** Maximum packet heap size */
#define VMK_PKT_HEAP_MAX_SIZE    96

/**
 * \ingroup Pkt
 * \struct vmk_NetSGArray
 * \brief Network packet scatter-gather array
 *
 * Scatter-gather array definition for vmkernel networking code. Uses
 * the generic vmkapi version of SgElem.
 */
typedef struct vmk_NetSGArray {
   vmk_uint32        length;
   vmk_uint32        maxLength;
   vmk_SgElem        sg[VMK_PKT_FRAGS_MAX_LENGTH];
} vmk_NetSGArray;

/**
 * \ingroup Pkt
 * \brief PktBufDescriptor flags
 */
typedef enum {

   /** Packet checksum needs to be computed */
   VMK_PKTBUF_FLAG_MUST_CSUM     = 0x00020000,

   /** Packet checksum has been verified */
   VMK_PKTBUF_FLAG_CSUM_VFD      = 0x00040000,

   /** TCP Segmentation Offload */
   VMK_PKTBUF_FLAG_MUST_TSO      = 0x00080000,

   /** Switch generated */
   VMK_PKTBUF_FLAG_SWITCH_GEN    = 0x00100000,

   /** SG span multiple pages */
   VMK_PKTBUF_FLAG_SG_SPAN_PAGES = 0x00200000,

   /** Every PktBuf flags allowed */
   VMK_PKTBUF_VALID_FLAGS        = 0x003E0000
} vmk_PktBufDescFlags;

/**
 * \ingroup Pkt
 * \struct vmk_PktBufDescriptor
 * \brief Packet buffer descriptor
 */
typedef struct vmk_PktBufDescriptor {
   /** Total length of the buffer(s) described */
   vmk_small_size_t    bufLen;

   /** Length of the data */
   vmk_small_size_t    frameLen;

   /** TSO MSS */
   vmk_uint16          tsoMss;

   /** Length of headroom */
   vmk_uint16          headroomLen;

   /** DVFilter pkt tags */
   vmk_uint32          dvfilterTagBits;
   /** Flags private to this bufDesc */
   vmk_PktBufDescFlags flags;

   /** Pointer to packet metadata. Used in DVFilter */
   vmk_VirtAddr        dvfilterMetadata;

   /** Length of DVFilter packet metadata */
   vmk_uint32          dvfilterMetadataLen;   

   /** List of machine addresses of the buffer */
   vmk_NetSGArray      sgMA;

   /*
    * don't put anything else here, sgMA *must* be the last field so
    * that we can alloc larger blocks in order to have more than
    * NET_PKT_SG_DEFAULT_SIZE scatter gather elements, which is
    * required in some cases, such as when we get elements from a
    * client that cross page boundarys (which may be contiguous
    * in PA space, but not in MA space)
    */
} vmk_PktBufDescriptor;

/**
 * \ingroup Pkt
 * \brief PktDescriptor flags
 */
typedef enum {
   /** Is the packet in use? */
   VMK_PKTDESC_FLAG_ALLOCATED       = 0x00000001,

   /** Complement of PKT_ALLOCATED */
   VMK_PKTDESC_FLAG_FREE            = 0x00000002,

   /** Completion notification needed? */
   VMK_PKTDESC_FLAG_NOTIFY_COMPLETE = 0x00000004,

   /** Can be held longer before completion */
   VMK_PKTDESC_FLAG_SLOW_COMPLETION = 0x00000008,

   /** Every PktDesc flags allowed */
   VMK_PKTDESC_VALID_FLAGS          = 0x0000000f
} vmk_PktDescFlags;

/** Data used to complete a packet */
typedef void *vmk_PktCompletionData;

/**
 * \ingroup Pkt
 * \struct vmk_PktDescriptor
 * \brief Packet descriptor
 */
typedef struct vmk_PktDescriptor {
   /** Flags for this descriptor shared by all handles */
   vmk_PktDescFlags      flags;

   /** Kernel context for io-complete routine */
   vmk_PktCompletionData ioCompleteData;

   vmk_PktCompletionData auxCompleteData;
} vmk_PktDescriptor;

/**
 * \ingroup Pkt
 * \brief PktHandle flags
 */
typedef enum {
   /** Is frameVA a valid pointer? */
   VMK_PKT_FLAG_FRAME_HEADER_MAPPED   = 0x00000001,

   /** Is bufDesc private? */
   VMK_PKT_FLAG_PRIVATE_BUF_DESC      = 0x00000002,

   /** Is the packet in use? */
   VMK_PKT_FLAG_ALLOCATED             = 0x00000004,

   /** Complement of PKT_ALLOCATED */
   VMK_PKT_FLAG_FREE                  = 0x00000008,

   /** Allocated from slab or not */
   VMK_PKT_FLAG_SLAB_ALLOCATED        = 0x00000010,

   /** VLAN redirection */
   VMK_PKT_FLAG_VLAN_REDIRECT         = 0x00000020,

   /** Tag for redirection */
   VMK_PKT_FLAG_MUST_TAG_REDIRECT     = 0x00000040,

   /** Allocated from NetPktHeap directly */
   VMK_PKT_FLAG_NETPKT_HEAP_ALLOCATED = 0x00000080,

   /** Allocated from NetGPHeap directly */
   VMK_PKT_FLAG_NET_GP_HEAP_ALLOCATED = 0x00000100,

   /** DVFilter slave packet - needs special completion */
   VMK_PKT_FLAG_DVFILTER_SLAVE        = 0x00000200,

   /** Packet needs vlan tag in Eth hdr */
   VMK_PKT_FLAG_MUST_TAG      = 0x00000400,

   /** Every packet flags allowed */
   VMK_PKT_VALID_FLAGS                = 0x000007ff
} vmk_PktHandleFlags;

/**
 * \ingroup Pkt
 * \struct vmk_PktHandle
 * \brief Exposed part of PktHandle.
 */
typedef struct vmk_PktHandle {
   /** This packet handle is part of a list */
   vmk_SList_Links       pktLinks;

   /** Part of the frame mapped */
   vmk_VirtAddr          frameVA;

   /** Number of bytes mapped */
   vmk_small_size_t      frameMappedLen;

   /** Flags private to this handle */
   vmk_PktHandleFlags    flags;

   /** PktBufDescriptor this handle refers to */
   vmk_PktBufDescriptor  *bufDesc;

   /** PktDescriptor this handle refers to */
   vmk_PktDescriptor     *pktDesc;
} vmk_PktHandle;

/**
 * \ingroup Pkt
 * \struct vmk_PktFrag
 * \brief Structure representing a packet fragment.
 */
typedef struct {
   /** Machine address of the fragment. */
   vmk_MachAddr      addr;

   /** Length of the fragment. */
   vmk_small_size_t  length;
} vmk_PktFrag;

/*
 ***********************************************************************
 * vmk_PktAlloc --                                                */ /**
 *
 * \ingroup Pkt
 * \brief Allocate a fresh packet which is physically contiguous and
 *        fully mapped.
 *
 * \note The resulting packet will contain only one frag.
 *
 * \param[in]  len         Size of the frag allocated for the packet
 * \param[out] pkt         Address of the new packet
 *
 * \retval     VMK_OK           Allocation succeeded
 * \retval     VMK_NO_RESOURCES Otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_PktAlloc(vmk_uint32 len,
			      vmk_PktHandle **pkt);

/*
 ***********************************************************************
 * vmk_PktRelease --                                              */ /**
 *
 * \ingroup Pkt
 * \brief Release/Complete packet in a non-interrupt context.
 *
 * \param[in]  pkt    Packet to be released
 *
 ***********************************************************************
 */

void vmk_PktRelease(vmk_PktHandle *pkt);

/*
 ***********************************************************************
 * vmk_PktReleaseIRQ --                                          */ /**
 *
 * \ingroup Pkt
 * \brief Release/Complete packet in an interrupt context.
 *
 * \param[in] pkt    Target packet
 *
 ***********************************************************************
 */

void vmk_PktReleaseIRQ(vmk_PktHandle *pkt);

/*
 ***********************************************************************
 * vmk_PktReleaseAfterComplete --                                 */ /**
 *
 * \ingroup Pkt
 * \brief Release a packet after completion.
 *
 * \param[in] pkt    Target packet
 *
 ***********************************************************************
 */

void vmk_PktReleaseAfterComplete(vmk_PktHandle *pkt);

/*
 ***********************************************************************
 * vmk_PktIsDescWritable --                                       */ /**
 *
 * \ingroup Pkt
 * \brief Indicate if the descriptor of a packet is writable.
 *
 * \param[in]  pkt        Target packet
 *
 * \retval     VMK_TRUE   Descriptor is writable
 * \retval     VMK_FALSE  Descriptor is not writable
 *
 ***********************************************************************
 */

vmk_Bool vmk_PktIsDescWritable(vmk_PktHandle *pkt);

/*
 ***********************************************************************
 * vmk_PktIsBufDescWritable --                                    */ /**
 *
 * \ingroup Pkt
 * \brief Indicate if the buffer descriptor of a packet is writable.
 *
 * \param[in]  pkt        Target packet
 *
 * \retval     VMK_TRUE   Descriptor is writable
 * \retval     VMK_FALSE  Descriptor is not writable
 *
 ***********************************************************************
 */

vmk_Bool vmk_PktIsBufDescWritable(const vmk_PktHandle *pkt);

/*
 ***********************************************************************
 * vmk_PktFrameLenGet --                                          */ /**
 *
 * \ingroup Pkt
 * \brief Retrieve the frame length of a specified packet.
 *
 * \param[in]  pkt      Target packet
 *
 * \return              Frame length
 *
 ***********************************************************************
 */

static inline vmk_small_size_t
vmk_PktFrameLenGet(vmk_PktHandle *pkt)
{
   return pkt->bufDesc->frameLen;
}

/*
 ***********************************************************************
 * vmk_PktFrameLenSet --                                          */ /**
 *
 * \ingroup Pkt
 * \brief Set the frame length of a specified packet.
 *
 * \param[in]  pkt    Target packet
 * \param[in]  len    Size of the frame described by the packet
 *
 * \pre vmk_PktIsBufDescWritable() must be TRUE on this packet.
 *
 * \retval     VMK_OK        If the frame length is set
 * \retval     VMK_BAD_PARAM Otherwise
 *
 ***********************************************************************
 */

static inline VMK_ReturnStatus
vmk_PktFrameLenSet(vmk_PktHandle *pkt,
                   vmk_small_size_t len)
{
   VMK_ASSERT(vmk_PktIsBufDescWritable(pkt));

   if (VMK_UNLIKELY(len > pkt->bufDesc->bufLen)) {
      return VMK_BAD_PARAM;
   }
   pkt->bufDesc->frameLen = len;

   return VMK_OK;
}

/*
 ***********************************************************************
 * vmk_PktFragGet --                                              */ /**
 *
 * \ingroup Pkt
 * \brief Retrieve fragment information for a specified packet.
 *
 * \param[in]  pkt           Target packet
 * \param[out] frag          Structure used to store fragment information
 * \param[in]  entry         Entry number indexing the desired fragment
 *
 * \retval     VMK_OK        If the fragment is found
 * \retval     VMK_NOT_FOUND Otherwise
 *
 ***********************************************************************
 */

static inline VMK_ReturnStatus
vmk_PktFragGet(vmk_PktHandle *pkt,
               vmk_PktFrag *frag,
               vmk_uint16 entry)
{
   VMK_ASSERT(entry < pkt->bufDesc->sgMA.length);

   if (VMK_UNLIKELY(entry >= pkt->bufDesc->sgMA.length)) {
      return VMK_NOT_FOUND;
   }

   frag->addr = pkt->bufDesc->sgMA.sg[entry].addr;
   frag->length = pkt->bufDesc->sgMA.sg[entry].length;

   return VMK_OK;
}

/*
 ***********************************************************************
 * vmk_PktFragsNb --                                              */ /**
 *
 * \ingroup Pkt
 * \brief Return the number of fragment used by a specified packet.
 *
 * \param[in]  pkt      Target packet
 *
 * \return              Number of fragments
 *
 ***********************************************************************
 */

static inline vmk_uint16
vmk_PktFragsNb(vmk_PktHandle *pkt)
{
   return pkt->bufDesc->sgMA.length;
}

/*
 ***********************************************************************
 * vmk_PktIsFlatBuffer --                                         */ /**
 *
 * \ingroup Pkt
 * \brief Check if a packet is located in a full flat buffer 
 *        meaning if basically it is made of 1 fragment.
 *
 * \param[in]  pkt        Target packet
 *
 * \retval     VMK_TRUE   Flat
 * \retval     VMK_FALSE  Non-flat
 *
 ***********************************************************************
 */

static inline vmk_Bool
vmk_PktIsFlatBuffer(vmk_PktHandle *pkt)
{
   if (pkt->bufDesc->sgMA.length <= 1) {
      VMK_DEBUG_ONLY({
         if (pkt->bufDesc->sgMA.length != 0) {
            VMK_ASSERT(pkt->bufDesc->sgMA.length == 1);
            VMK_ASSERT(pkt->bufDesc->sgMA.sg[0].length == pkt->bufDesc->bufLen);
         }
      })
      return VMK_TRUE;
   }
   return VMK_FALSE;
}

/*
 ***********************************************************************
 * vmk_PktFrameMappedLenGet --                                    */ /**
 *
 * \ingroup Pkt
 * \brief Retrieve the length of the frame mapped area of a specified
 *        packet.
 *
 * \note The mapped area may be larger than the
 *       pkt's length.  When processing data within the mapped
 *       data area, use MIN(frameLen, frameMappedLen)
 *
 * \param[in]  pkt            Target packet
 *
 * \return                    Length of the frame mapped area
 *
 ***********************************************************************
 */

static inline vmk_small_size_t
vmk_PktFrameMappedLenGet(const vmk_PktHandle *pkt)
{
   return pkt->frameMappedLen;
}

/*
 ***********************************************************************
 * vmk_PktIsFullyMapped --                                        */ /**
 *
 * \ingroup Pkt
 * \brief Check if a packet is fully mapped.
 *
 * \param[in]  pkt           Target packet

 * \retval     VMK_TRUE      Fully mapped
 * \retval     VMK_FALSE     non-fully mappep
 *
 ***********************************************************************
 */

static inline vmk_Bool
vmk_PktIsFullyMapped(vmk_PktHandle *pkt)
{
   return pkt->bufDesc->bufLen == vmk_PktFrameMappedLenGet(pkt) ? VMK_TRUE : VMK_FALSE;
}

/*
 ***********************************************************************
 * vmk_PktFrameMappedPointerGet --                                */ /**
 *
 * \ingroup Pkt
 * \brief Retrieve the pointer on the frame mapped area of a specified
 *        packet.
 *
 * \param[in]  pkt         Target packet
 *
 * \return                 A pointer to the frame mapped area
 *
 ***********************************************************************
 */

static inline vmk_VirtAddr
vmk_PktFrameMappedPointerGet(const vmk_PktHandle *pkt)
{
   return pkt->frameVA;
}

/*
 ***********************************************************************
 * vmk_PktQueueForRxProcess --                                    */ /**
 *
 * \ingroup Pkt
 * \brief Queue a specified packet coming from an uplink for Rx process.
 *
 * \param[in] pkt       Target packet
 * \param[in] uplink    Uplink where the packet came from
 *
 ***********************************************************************
 */

void vmk_PktQueueForRxProcess(vmk_PktHandle *pkt,
                              vmk_Uplink *uplink);

/*
 ***********************************************************************
 * vmk_PktAdjust --                                               */ /**
 *
 * \ingroup Pkt
 * \brief Adjust the packet for a specified amount of byte.
 *
 * \note As a result of this adjustment the mapped frame pointer, the
 *       length of the mapped area and the set of frags will be modified.
 *       The packet to be adjusted need to be physically contiguous and fully mapped.
 *
 * \pre vmk_PktIsBufDescWritable() must be TRUE on this packet.
 *
 * \param[in] pkt       Target packet
 * \param[in] pullLen   Amount of bytes to pull from the beginning of the packet frags
 * \param[in] adjustLen Amount of bytes to keep from the packet frags after pullLen bytes
 *                      The rest of the bytes are simply discarded
 *
 * \retval     VMK_OK             The packet adjustement succeeded
 * \retval     VMK_FAILURE        The packet to adjust is not contiguous or not fully mapped
 * \retval     VMK_LIMIT_EXCEEDED The packet destination cannot be adjusted
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_PktAdjust(vmk_PktHandle *pkt,
                               vmk_uint32 pullLen,
                               vmk_uint32 adjustLen);

/*
 ***********************************************************************
 * vmk_PktAppend --                                               */ /**
 *
 * \ingroup Pkt
 * \brief Append all the frags from a packet source to a packet destination.
 *        Note that the packet source won't be released automatically
 *        with the packet destination which means that it will be the
 *        caller responsibility to take care of it.
 *        In order to release the packet source the caller absolutely 
 *        need to make sure that the packet destination is not used 
 *        anymore.
 *        Indeed releasing the packet source will release some fragments 
 *        the packet destination may expects to use later on.
 *
 * \note The packet to append needs to be physically contiguous and fully mapped.
 *       Also, this function invalidates all previous calls to vmk_PktFragGet() on
 *       the target packet as it tries to merge fragments if addresses are physically
 *       contiguous.
 *
 * \pre vmk_PktIsBufDescWritable() must be TRUE on this packet.
 *
 * \param[in] pkt         Target packet
 * \param[in] pktToAppend Packet to append
 * \param[in] pullLen     Amount of bytes from the beginning of
 *                        the packet to append
 * \param[in] appendLen   Amount of bytes to be appended
 *
 * \retval    VMK_OK             If the packet has been appended
 * \retval    VMK_FAILURE        The packet to append is not contiguous or not fully mapped
 * \retval    VMK_LIMIT_EXCEEDED The packet destination cannot accept new fragments anymore
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_PktAppend(vmk_PktHandle *pkt,
                               vmk_PktHandle *pktToAppend,
                               vmk_uint32 pullLen,
                               vmk_uint32 appendLen);

/*
 ***********************************************************************
 * vmk_PktSetCompletionData --                                    */ /**
 *
 * \ingroup Pkt
 * \brief Set a packet for completion which will cause vmkernel to call
 *        the completion handler registered by the source port of this
 *        packet (see vmk_UplinkRegisterCompletionFn()).
 *        Also the caller needs to specify if it is acceptable for the
 *        receiver to hold the packet a little bit longer such as in socket 
 *        buffers of the tcpip module until the peer acknowledges the data 
 *        in the packet, before completion.
 *        If it is not then the receiver will do a local copy and will
 *        release the packet immediately.
 *
 * \pre vmk_PktIsDescWritable() must be TRUE on this packet.
 *
 * \param[in] pkt                 Target packet
 * \param[in] ioData              Data embedded in the packet for io-completion
 * \param[in] auxData             Data embedded in the packet for aux-completion
 * \param[in] allowSlowCompletion Packet can be held longer before completion
 *
 ***********************************************************************
 */

static inline void
vmk_PktSetCompletionData(vmk_PktHandle *pkt,
                         vmk_PktCompletionData ioData,
                         vmk_PktCompletionData auxData,
                         vmk_Bool allowSlowCompletion)
{
   VMK_ASSERT(vmk_PktIsDescWritable(pkt));

   if (allowSlowCompletion) {
      pkt->pktDesc->flags |= VMK_PKTDESC_FLAG_SLOW_COMPLETION;
   }
   pkt->pktDesc->flags |= VMK_PKTDESC_FLAG_NOTIFY_COMPLETE;
   pkt->pktDesc->ioCompleteData = ioData;
   pkt->pktDesc->auxCompleteData = auxData;
}

/*
 ***********************************************************************
 * vmk_PktGetCompletionData --                                    */ /**
 *
 * \ingroup Pkt
 * \brief Get packet's embedded completion data. Each NULL parameter
 *        will be simply ignored.
 *
 * \param[in]  pkt     Target packet
 * \param[out] ioData  Data embedded in the packet for io-completion
 * \param[out] auxData Data embedded in the packet for aux-completion
 *
 ***********************************************************************
 */

static inline void
vmk_PktGetCompletionData(vmk_PktHandle *pkt,
                         vmk_PktCompletionData *ioData,
                         vmk_PktCompletionData *auxData)
{
   if (ioData) {
      *ioData = pkt->pktDesc->ioCompleteData;
   }

   if (auxData) {
      *auxData = pkt->pktDesc->auxCompleteData;
   }
}

/*
 ***********************************************************************
 * vmk_PktClearCompletionData -                                   */ /**
 *
 * \ingroup Pkt
 * \brief Clear completion requirement for a packet.
 *
 * \pre vmk_PktIsDescWritable() must be TRUE on this packet.
 *
 * \param[in]  pkt    Target packet
 *
 ***********************************************************************
 */

static inline void
vmk_PktClearCompletionData(vmk_PktHandle *pkt)
{
   VMK_ASSERT(vmk_PktIsDescWritable(pkt));

   pkt->pktDesc->auxCompleteData = NULL;
   pkt->pktDesc->ioCompleteData = NULL;
   pkt->pktDesc->flags &= ~(VMK_PKTDESC_FLAG_NOTIFY_COMPLETE | VMK_PKTDESC_FLAG_SLOW_COMPLETION);
}

/*
 ***********************************************************************
 * vmk_PktNeedCompletion -                                        */ /**
 *
 * \ingroup Pkt
 * \brief Check if the packet needs completion.
 *
 * \param[in]  pkt                 Target packet
 *
 * \retval     VMK_TRUE            Packet needs completion
 * \retval     VMK_FALSE           Packet doesn't need completion
 *
 ***********************************************************************
 */

static inline vmk_Bool
vmk_PktNeedCompletion(vmk_PktHandle *pkt)
{
   return (pkt->pktDesc-> flags & VMK_PKTDESC_FLAG_NOTIFY_COMPLETE) != 0 ? VMK_TRUE : VMK_FALSE;
}

/*
 ***********************************************************************
 * vmk_PktAllowSlowCompletion -                                   */ /**
 *
 * \ingroup Pkt
 * \brief Check if the packet needing completion, allows slow completion.
 *
 * \param[in]  pkt                 Target packet
 *
 * \retval     VMK_TRUE            Packet can be held longer before completion
 * \retval     VMK_FALSE           Packet can't be held longer before completion
 *
 ***********************************************************************
 */

static inline vmk_Bool
vmk_PktAllowSlowCompletion(vmk_PktHandle *pkt)
{
   VMK_ASSERT((pkt->pktDesc-> flags & VMK_PKTDESC_FLAG_NOTIFY_COMPLETE) != 0);
   return (pkt->pktDesc->flags & VMK_PKTDESC_FLAG_SLOW_COMPLETION) != 0 ? VMK_TRUE : VMK_FALSE;
}

/*
 ***********************************************************************
 * vmk_PktPriorityGet --                                            */ /**
 *
 * \ingroup Pkt
 * \brief Retrieve 802.1p priority of a specified packet.
 *
 * \param[in]  pkt        Packet of interest
 *
 * \return                Packet priority
 *
 ***********************************************************************
 */

vmk_VlanPriority vmk_PktPriorityGet(vmk_PktHandle *pkt);

/*
 ***********************************************************************
 * vmk_PktQueueIdGet --                                           */ /**
 *
 * \ingroup Pkt
 * \brief Retrieve netqueue queueid of a specified packet.
 *
 * \param[in]  pkt        Packet of interest
 *
 * \return                Netqueue queue id.
 *
 ***********************************************************************
 */

vmk_NetqueueQueueId vmk_PktQueueIdGet(vmk_PktHandle *pkt);

/*
 ***********************************************************************
 * vmk_PktPrioritySet --                                            */ /**
 *
 * \ingroup Pkt
 * \brief Modify 802.1p priority of the specified packet.
 *
 * \param[in] pkt        Packet of interest
 * \param[in] priority   New priority
 *
 ***********************************************************************
 */

void vmk_PktPrioritySet(vmk_PktHandle *pkt,
                        vmk_VlanPriority priority);


/*
 ***********************************************************************
 * vmk_PktPriorityClear --                                            */ /**
 *
 * \ingroup Pkt
 * \brief Clear 802.1p priority of the specified packet.
 *
 * \param[in] pkt        Packet of interest
 *
 ***********************************************************************
 */

void vmk_PktPriorityClear(vmk_PktHandle *pkt);

/*
 ***********************************************************************
 * vmk_PktVlanIDGet --                                            */ /**
 *
 * \ingroup Pkt
 * \brief Retrieve vlan id of a specified packet.
 *
 * \param[in]  pkt    Target packet
 *
 * \return            Packet vlan id
 *
 ***********************************************************************
 */

vmk_VlanID vmk_PktVlanIDGet(vmk_PktHandle *pkt);

/*
 ***********************************************************************
 * vmk_PktVlanIDSet --                                            */ /**
 *
 * \ingroup Pkt
 * \brief Modify the vlan id of a specified packet.
 *
 * \param[in] pkt    Target packet
 * \param[in] vid    New vlan id
 *
 ***********************************************************************
 */

void vmk_PktVlanIDSet(vmk_PktHandle *pkt,
                      vmk_VlanID vid);

/*
 ***********************************************************************
 * vmk_PktVlanIDClear --                                            */ /**
 *
 * \ingroup Pkt
 * \brief Clear the vlan id of a specified packet.
 *
 * \param[in] pkt    Target packet
 *
 ***********************************************************************
 */

void vmk_PktVlanIDClear(vmk_PktHandle *pkt);

/*
 ***********************************************************************
 * vmk_PktSetCsumVfd --                                           */ /**
 *
 * \ingroup Pkt
 * \brief If the packet's checksum has been already verified, flag it
 *        with this information before queuing for Rx process.
 *
 * \param[in] pkt    Target packet
 *
 ***********************************************************************
 */

static inline void
vmk_PktSetCsumVfd(vmk_PktHandle *pkt)
{
   pkt->bufDesc->flags |= VMK_PKTBUF_FLAG_CSUM_VFD;
}

/*
 ***********************************************************************
 * vmk_PktClearCsumVfd --                                         */ /**
 *
 * \ingroup Pkt
 * \brief Clear CSUM_VFD flag
 *
 * \param[in]  pkt        Target packet
 *
 ***********************************************************************
 */

static inline void
vmk_PktClearCsumVfd(vmk_PktHandle *pkt)
{
   pkt->bufDesc->flags &= ~VMK_PKTBUF_FLAG_CSUM_VFD;
}

/*
 ***********************************************************************
 * vmk_PktIsMustCsum --                                           */ /**
 *
 * \ingroup Pkt
 * \brief Check if checksum is required for a specified packet.
 *
 * \param[in]  pkt        Target packet
 *
 * \retval     VMK_TRUE   Checksum needed
 * \retval     VMK_FALSE  Checksum not needed
 *
 ***********************************************************************
 */

static inline vmk_Bool
vmk_PktIsMustCsum(const vmk_PktHandle *pkt)
{
   return (pkt->bufDesc->flags & VMK_PKTBUF_FLAG_MUST_CSUM) != 0 ? VMK_TRUE : VMK_FALSE;
}

/*
 ***********************************************************************
 * vmk_PktClearMustCsum --                                        */ /**
 *
 * \ingroup Pkt
 * \brief Indicate checksum is not required for a specified packet.
 *
 * \pre vmk_PktIsBufDescWritable() must be TRUE on this packet.
 *
 * \param[in]  pkt        Target packet
 *
 ***********************************************************************
 */

static inline void
vmk_PktClearMustCsum(const vmk_PktHandle *pkt)
{
   VMK_ASSERT(vmk_PktIsBufDescWritable(pkt));
   pkt->bufDesc->flags &= ~VMK_PKTBUF_FLAG_MUST_CSUM;
}

/*
 ***********************************************************************
 * vmk_PktSetMustCsum --                                          */ /**
 *
 * \ingroup Pkt
 * \brief Indicate checksum is required for a specified packet.
 *
 * \pre vmk_PktIsBufDescWritable() must be TRUE on this packet.
 *
 * \param[in]  pkt        Target packet
 *
 ***********************************************************************
 */

static inline void
vmk_PktSetMustCsum(const vmk_PktHandle *pkt)
{
   VMK_ASSERT(vmk_PktIsBufDescWritable(pkt));
   pkt->bufDesc->flags |= VMK_PKTBUF_FLAG_MUST_CSUM;
}

/*
 ***********************************************************************
 * vmk_PktMustVlanTag --                                          */ /**
 *
 * \ingroup Pkt
 * \brief Check if vlan tagging is required for a specified packet.
 *
 * \param[in]  pkt        Target packet
 *
 * \retval     VMK_TRUE   Vlan tag needed
 * \retval     VMK_FALSE  Vlan tag not needed
 *
 ***********************************************************************
 */

static inline vmk_Bool
vmk_PktMustVlanTag(vmk_PktHandle *pkt)
{
   return ((pkt->flags & VMK_PKT_FLAG_MUST_TAG) ||
           (pkt->flags & VMK_PKT_FLAG_MUST_TAG_REDIRECT)) ? VMK_TRUE : VMK_FALSE;
}

/*
 ***********************************************************************
 * vmk_PktIsLargeTcpPacket --                                     */ /**
 *
 * \ingroup Pkt
 * \brief Check if a tcp segmentation offload is required for a specified packet.
 *
 * \param[in]  pkt        Target packet.
 *
 * \retval     VMK_TRUE   TSO needed.
 * \retval     VMK_FALSE  TSO not needed.
 *
 ***********************************************************************
 */

static inline vmk_Bool
vmk_PktIsLargeTcpPacket(const vmk_PktHandle *pkt)
{
   return (pkt->bufDesc->flags & VMK_PKTBUF_FLAG_MUST_TSO) != 0 ? VMK_TRUE : VMK_FALSE;
}

/*
 ***********************************************************************
 * vmk_PktSetLargeTcpPacket --                                    */ /**
 *
 * \ingroup Pkt
 * \brief Set the packet as a large tcp packet which will require tcp
 *        segmentation offload.
 *
 * \pre vmk_PktIsBufDescWritable() must be TRUE on this packet.
 *
 * \param[in]  pkt    Target packet.
 * \param[in]  mss    Maximum segment size of the tcp connection
 *
 ***********************************************************************
 */

static inline void
vmk_PktSetLargeTcpPacket(vmk_PktHandle *pkt,
                         vmk_uint32 mss)
{
   VMK_ASSERT(vmk_PktIsBufDescWritable(pkt));

   pkt->bufDesc->flags |= VMK_PKTBUF_FLAG_MUST_TSO;
   pkt->bufDesc->tsoMss = mss;
}

/*
 ***********************************************************************
 * vmk_PktGetLargeTcpPacketMss --                                 */ /**
 *
 * \ingroup Pkt
 * \brief Retrieve mss of the tcp connection a specified packet
 *        belongs to.
 *
 * \param[in]  pkt    Target packet
 *
 * \return            Maximum segment size
 *
 ***********************************************************************
 */

static inline vmk_uint32
vmk_PktGetLargeTcpPacketMss(vmk_PktHandle *pkt)
{
   VMK_ASSERT(vmk_PktIsLargeTcpPacket(pkt));

   return pkt->bufDesc->tsoMss;
}

/*
 ***********************************************************************
 * vmk_PktCopyMetaData --                                         */ /**
 *
 * \ingroup Pkt
 * \brief Copy packet metadata from one packet to another
 *
 * \param[in] srcPkt Source of the metadata
 * \param[in] dstPkt Packet where to copy the metadata into
 *
 ***********************************************************************
 */

void vmk_PktCopyMetaData(vmk_PktHandle *srcPkt,
                         vmk_PktHandle *dstPkt);

/*
 ***********************************************************************
 * vmk_PktCheckConsistency --                                     */ /**
 *
 * \ingroup Pkt
 * \brief Check for obvious inconsistency in the internal representation
 *        of a specified packet.
 *
 * \param[in]  pkt        Target packet
 *
 * \retval     VMK_TRUE   Packet is consistent
 * \retval     VMK_FALSE  Packet is not consistent
 *
 ***********************************************************************
 */

vmk_Bool vmk_PktCheckInternalConsistency(vmk_PktHandle *pkt);

/*
 ***********************************************************************
 * vmk_PktSetIsCorrupted --                                       */ /**
 *
 * \ingroup Pkt
 * \brief Mark a packet as having been corrupted.
 *
 * \param[in]  pkt        Target packet
 *
 ***********************************************************************
 */

void vmk_PktSetIsCorrupted(vmk_PktHandle *pkt);

/*
 ***********************************************************************
 * vmk_PktPartialCopy --                                          */ /**
 *
 * \ingroup Pkt
 * \brief Create a partial copy of a packet source with a private header
 *        of the specified amount of bytes.
 *        If the source packet has a headroom, this latter won't be retained.
 *
 * \note  This function needs to be used for module which intend to modify
 *        a packet. Every packets pushed into a module are considered as 
 *        read-only and this function is a way to change this policy.
 *        Also the resulting packet won't be neither fully mapped nor
 *        physically contiguous besides if the partial copy turns out to
 *        be a full copy.
 *
 * \param[in]   pkt        Target packet
 * \param[in]   numBytes   Number of bytes in the private header
 * \param[out]  copyPkt    Partial copied packet
 *
 * \retval     VMK_OK      If the partial copy was successful
 * \retval     VMK_FAILURE Otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_PktPartialCopy(vmk_PktHandle *pkt,
                                    vmk_uint32 numBytes,
                                    vmk_PktHandle **copyPkt);

/*
 ***********************************************************************
 * vmk_PktPartialCopyWithHeadroom --                              */ /**
 *
 * \ingroup Pkt
 * \brief Create a partial copy of a packet source with a private header
 *        of the specified amount of bytes. The resulting packet 
 *        will have a headroom of the specified size in front of the 
 *        contents. 
 *        If the source packet already has a headroom, this latter won't be retained.
 *
 * \note  This function needs to be used for module which intend to modify
 *        a packet. Every packets pushed into a module are considered as 
 *        read-only and this function is a way to change this policy.
 *        Also the resulting packet won't be neither fully mapped nor
 *        physically contiguous besides if the partial copy turns out to
 *        be a full copy.
 *
 * \param[in]   pkt         Target packet
 * \param[in]   numBytes    Number of bytes in the private header
 * \param[in]   headroomLen Number of bytes before the actual frame
 * \param[out]  copyPkt     Partial copied packet
 *
 * \retval     VMK_OK       If the partial copy was successful
 * \retval     VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_PktPartialCopyWithHeadroom(vmk_PktHandle *pkt,
                                                vmk_uint32 numBytes,
                                                vmk_uint16 headroomLen,
                                                vmk_PktHandle **copyPkt);

/*
 ***********************************************************************
 * vmk_PktCopy --                                                 */ /**
 *
 * \ingroup Pkt
 * \brief Create a full copy of a packet source.
 *        If the source packet has a headroom, this latter won't be retained.
 *
 * \note  This function needs to be used for module which intend to modify
 *        a packet. Every packets pushed into a module are considered as 
 *        read-only and this function is a way  to change this policy.
 *        Also the resulting packet will be fully mapped and physically 
 *        contiguous.
 *
 * \param[in]   pkt        Target packet
 * \param[out]  copyPkt    Partial copied packet
 *
 * \retval     VMK_OK      If the partial copy was successful
 * \retval     VMK_FAILURE Otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_PktCopy(vmk_PktHandle *pkt,
                             vmk_PktHandle **copyPkt);

/*
 ***********************************************************************
 * vmk_PktCopyWithHeadroom --                                     */ /**
 *
 * \ingroup Pkt
 * \brief Create a full copy of a packet source. The resulting packet 
 *        will have a headroom of the specified size in front of the 
 *        contents.
 *        If the source packet already has a headroom, this latter won't be retained.
 *
 * \note  This function needs to be used for module which intend to modify
 *        a packet. Every packets pushed into a module are considered as 
 *        read-only and this function is a way to change this policy.
 *        Also the resulting packet will be fully mapped and physically 
 *        contiguous.
 *
 * \param[in]   pkt         Target packet
 * \param[in]   headroomLen Number of bytes before the actual frame
 * \param[out]  copyPkt     Partial copied packet
 *
 * \retval     VMK_OK       If the partial copy was successful
 * \retval     VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_PktCopyWithHeadroom(vmk_PktHandle *pkt,
                                         vmk_uint16 headroomLen,
                                         vmk_PktHandle **copyPkt);

/*
 ***********************************************************************
 * vmk_PktClone --                                                */ /**
 *
 * \ingroup Pkt
 * \brief Create a clone of a packet source.
 *        If the source packet has a headroom, this latter won't be retained.
 *
 * \note  The resulting packet contents will still be read-only thereby 
 *        this function cannot be used to modify the packet contents.
 *
 * \param[in]   pkt         Target packet
 * \param[out]  clonePkt    Partial copied packet
 *
 * \retval     VMK_OK       If the partial copy was successful
 * \retval     VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_PktClone(vmk_PktHandle *pkt,
                              vmk_PktHandle **clonePkt);

/*
 ***********************************************************************
 * vmk_PktGetHeadroomLen --                                       */ /**
 *
 * \ingroup Pkt
 * \brief Return the length of the headroom in front of the packet contents.
 *
 * \param[in]   pkt         Target packet
 *
 * \retval      >=0         The length of the headroom
 *
 ***********************************************************************
 */

vmk_uint16 vmk_PktGetHeadroomLen(vmk_PktHandle *pkt);

/*
 ***********************************************************************
 * vmk_PktPushHeadroom --                                         */ /**
 *
 * \ingroup Pkt
 * \brief Increase the size of the headroom in front of the packets contents.
 *
 * \note As a result of increasing the headroom space, the
 *       length of the mapped area and the set of frags will be modified.
 *
 * \param[in]   pkt         Target packet
 * \param[in]   headroomLen Size to increase
 *
 * \retval      VMK_OK      The headroom has been enlarged properly
 * \retval      VMK_FAILURE Otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_PktPushHeadroom(vmk_PktHandle *pkt,
                                     vmk_uint16 headroomLen);

/*
 ***********************************************************************
 * vmk_PktPullHeadroom --                                         */ /**
 *
 * \ingroup Pkt
 * \brief Decrease the size of the headroom in front of the packets contents.
 *
 * \note As a result of increasing the headroom space, the
 *       length of the mapped area and the set of frags will be modified.
 *
 * \param[in]   pkt         Target packet
 * \param[in]   headroomLen Size to decrease
 *
 * \retval      VMK_OK      The headroom has been reduced properly
 * \retval      VMK_FAILURE Otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_PktPullHeadroom(vmk_PktHandle *pkt,
                                     vmk_uint16 headroomLen);

/*
 ***********************************************************************
 * vmk_PktDup --                                                  */ /**
 *
 * \ingroup Pkt
 * \brief Make a full, independent, copy of the pkt and meta data
 *
 * \param[in]   pkt         Target packet
 * \param[out]  dupPkt      Fully copied packet
 *
 * \retval      VMK_OK      If the duplication was successful
 * \retval      VMK_FAILURE Otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_PktDup(vmk_PktHandle *pkt,
                            vmk_PktHandle **dupPkt);

/*
 ***********************************************************************
 * vmk_PktDupAndCsum --                                           */ /**
 *
 * \ingroup Pkt
 * \brief Make a full, independent, checksummed copy of the pkt and meta data 
 *
 * \param[in]   pkt         Target packet
 * \param[out]  dupPkt      Fully copied packet
 *
 * \retval      VMK_OK      If the duplication was successful
 * \retval      VMK_FAILURE Otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_PktDupAndCsum(vmk_PktHandle *pkt,
                                   vmk_PktHandle **dupPkt);

/*
 ***********************************************************************
 * vmk_PktDupWithHeadroom --                                      */ /**
 *
 * \ingroup Pkt
 * \brief Make a full, independent, copy of the pkt and meta data.
 *        The resulting packet will have a headroom of the specified 
 *        size in front of the contents. 
 *        If the source packet already has a headroom, this latter 
 *        won't be retained. 

 *
 * \param[in]   p           Target packet
 * \param[in]   headroomLen Number of bytes before the actual frame
 * \param[out]  dupPkt      Fully copied packet
 *
 * \retval      VMK_OK      If the duplication was successful
 * \retval      VMK_FAILURE Otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_PktDupWithHeadroom(vmk_PktHandle *p,
                                        vmk_uint16 headroomLen,
                                        vmk_PktHandle **dupPkt);

/*
 ***********************************************************************
 * vmk_PktDupAndCsumWithHeadroom --                               */ /**
 *
 * \ingroup Pkt
 * \brief Make a full, independent, checksummed copy of the pkt and meta data.
 *        The resulting packet will have a headroom of the specified 
 *        size in front of the contents. 
 *        If the source packet already has a headroom, this latter 
 *        won't be retained. 

 *
 * \param[in]   pkt         Target packet
 * \param[in]   headroomLen Number of bytes before the actual frame
 * \param[out]  dupPkt      Fully copied packet
 *
 * \retval      VMK_OK      If the duplication was successful
 * \retval      VMK_FAILURE Otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_PktDupAndCsumWithHeadroom(vmk_PktHandle *pkt,
                                               vmk_uint16 headroomLen,
                                               vmk_PktHandle **dupPkt);

#endif
/** @} */
/** @} */
