/* **********************************************************
 * Copyright 2006 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * PktList                                                        */ /**
 * \addtogroup Network
 *@{
 * \defgroup PktList Packet List Management
 *@{ 
 *
 * \par Packet Lists:
 *
 * Packet list are an important entity in vmkernel as any set of packets
 * are represented through this data structure.
 * Every module will need to deal with it as vmkernel expects it to
 * be able to.
 *
 * For example if a module is intended to manage device driver and want
 * vmkernel to use it in order to communicate with the external world, 
 * it will receive packet lists for Tx process.
 * 
 ***********************************************************************
 */

#ifndef _VMKAPI_NET_PKTLIST_H_
#define _VMKAPI_NET_PKTLIST_H_

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
#include "base/vmkapi_assert.h"
#include "base/vmkapi_cslist.h"

#include "net/vmkapi_net_types.h"

/*
 * Structure representing the packet list.
 */

typedef struct vmk_PktList {
   vmk_Bool         mayModify;
   vmk_CSList       csList;
} vmk_PktList;

/*
 ***********************************************************************
 * vmk_PktListInit --                                             */ /**
 *
 * \ingroup PktList
 * \brief Initialize a packet list.
 *
 * \param[in]  pktList Target packet list
 *
 ***********************************************************************
 */

static inline void
vmk_PktListInit(vmk_PktList *pktList)
{
   VMK_ASSERT(pktList);
   pktList->mayModify = VMK_FALSE;
   vmk_CSListInit(&pktList->csList);
}

/*
 ***********************************************************************
 * vmk_PktListCount --                                            */ /**
 *
 * \ingroup PktList
 * \brief Retrieve the number of a packet in a specified packet list.
 *
 * \param[in]  pktList Target packet list
 *
 * \return Number of packets in the list
 *
 ***********************************************************************
 */

static inline vmk_uint32
vmk_PktListCount(vmk_PktList *pktList)
{
   VMK_ASSERT(pktList);
   return vmk_CSListCount(&pktList->csList);
}

/*
 ***********************************************************************
 * vmk_PktListIsConsistent --                                     */ /**
 *
 * \ingroup PktList
 * \brief Check if a packet list is consistent or not.
 *
 * \param[in]  list  Target packet list.
 *
 * \retval VMK_TRUE     List is correct.
 * \retval VMK_FALSE    List is corrupted.
 *
 ***********************************************************************
 */

static inline vmk_Bool
vmk_PktListIsConsistent(vmk_PktList *list)
{
   if (!list) {
      return VMK_FALSE;
   }

   if ((vmk_SListFirst(&list->csList.slist) == NULL) && 
       (vmk_SListLast(&list->csList.slist) == NULL) && 
       (vmk_PktListCount(list) == 0)) {
      return VMK_TRUE; /* empty list. */
   } else if (!((vmk_SListFirst(&list->csList.slist) != NULL) &&
                (vmk_SListLast(&list->csList.slist) != NULL) &&
                (vmk_PktListCount(list) != 0))) {
      return VMK_FALSE; /* inconsistent non-empty list. */
   }

   { /* counting packets */
      vmk_uint32 i = 0;
      vmk_SList_Links *cur, *prev = NULL;

      VMK_CSLIST_FORALL(&list->csList, cur) {
         prev = cur;
         if (++i > vmk_PktListCount(list)) { /* too many elements? */
            return VMK_FALSE;
         }
      }

      if (i != vmk_PktListCount(list)) { /* list count is correct? */
         return VMK_FALSE;
      }

      if (prev != vmk_SListLast(&list->csList.slist)) { /* tail is correct? */
         return VMK_FALSE;
      }

      if (prev->next != NULL) { /* list NULL terminated? */
         return VMK_FALSE;
      }
   }

   return VMK_TRUE;
}

/*
 ***********************************************************************
 * vmk_PktListIsEmpty --                                          */ /**
 *
 * \ingroup PktList
 * \brief Check if a specified packet list is empty.
 *
 * \param[in] pktList     Target packet list.
 *
 * \retval VMK_TRUE     List empty.
 * \retval VMK_FALSE    List not empty.
 *
 ***********************************************************************
 */

static inline vmk_Bool
vmk_PktListIsEmpty(vmk_PktList *pktList)
{
   VMK_ASSERT(pktList);
   return vmk_CSListIsEmpty(&pktList->csList);
}

/*
 ***********************************************************************
 * vmk_PktListAddToHead --                                        */ /**
 *
 * \ingroup PktList
 * \brief Add a specified packet at the front of a specified packet list.
 *
 * \param[in] pktList Target packet list.
 * \param[in] pkt     Packet to be added.
 *
 ***********************************************************************
 */

static inline void
vmk_PktListAddToHead(vmk_PktList *pktList,
                     vmk_PktHandle *pkt)
{
   VMK_ASSERT(pktList);
   VMK_ASSERT(pkt);
   vmk_CSListInsertAtHead(&pktList->csList, &pkt->pktLinks);
}

/*
 ***********************************************************************
 * vmk_PktListAddToTail --                                        */ /**
 *
 * \ingroup PktList
 * \brief Add a specifed packet at the end of a specified packet list.
 *
 * \param[in] pktList Target packet list.
 * \param[in] pkt     Packet to be added.
 *
 ***********************************************************************
 */

static inline void
vmk_PktListAddToTail(vmk_PktList *pktList,
                     vmk_PktHandle *pkt)
{
   VMK_ASSERT(pktList);
   VMK_ASSERT(pkt);
   vmk_CSListInsertAtTail(&pktList->csList, &pkt->pktLinks);
}

/*
 ***********************************************************************
 * vmk_PktListInsertAfter --                                      */ /**
 *
 * \ingroup PktList
 * \brief Insert a packet after the specified element in the packet list.
 *
 * \param[in] pktList Target packet list.
 * \param[in] prev    Packet to insert after.
 * \param[in] pkt     Packet to insert.
 *
 ***********************************************************************
 */

static inline void
vmk_PktListInsertAfter(vmk_PktList *pktList, 
                       vmk_PktHandle *prev, 
                       vmk_PktHandle *pkt)
{
   VMK_ASSERT(pktList);
   VMK_ASSERT(prev);
   VMK_ASSERT(pkt);
   vmk_CSListInsertAfter(&pktList->csList, &pkt->pktLinks, &prev->pktLinks);
}

/*
 ***********************************************************************
 * vmk_PktListGetHead --                                          */ /**
 *
 * \ingroup PktList
 * \brief Retrieve the packet in front of a specified packet list.
 *
 * \param[in]  pktList Target packet list
 * 
 * \retval     NULL        The list is empty.
 * \return                 A pointer to the head of the list.
 *
 ***********************************************************************
 */

static inline vmk_PktHandle *
vmk_PktListGetHead(vmk_PktList *pktList)
{
   VMK_ASSERT(pktList);
   return (vmk_PktHandle *) vmk_CSListFirst(&pktList->csList);
}

/*
 ***********************************************************************
 * vmk_PktListGetNext --                                          */ /**
 *
 * \ingroup PktList
 * \brief Retrieve the packet following a specified packet in a
 *        specified packet list.
 *
 * \param[in]  pktList Target packet list.
 * \param[in]  pkt     Target packet.
 *
 * \retval     NULL        End of the list.
 * \return                 A pointer to the successor in the list.
 *
 ***********************************************************************
 */

static inline vmk_PktHandle *
vmk_PktListGetNext(vmk_PktList *pktList,
                   vmk_PktHandle *pkt)
{
   VMK_ASSERT(pktList && pkt);
   return (vmk_PktHandle *) vmk_CSListNext(&pkt->pktLinks);
}

/*
 ***********************************************************************
 * vmk_PktListGetPrev --                                          */ /**
 *
 * \ingroup PktList
 * \brief Retrieve the packet preceding a specified packet in a
 *        specified packet list.
 *
 * \param[in]  pktList Target packet list.
 * \param[in]  pkt     Target packet.
 *
 * \retval     NULL        Beginning of the list.
 * \return                 A pointer to the predecessor in the list.
 *
 ***********************************************************************
 */

static inline vmk_PktHandle *
vmk_PktListGetPrev(vmk_PktList *pktList,
                   vmk_PktHandle *pkt)
{
   VMK_ASSERT(pktList);
   VMK_ASSERT(pkt);
   return (vmk_PktHandle *) vmk_CSListPrev(&pktList->csList, &pkt->pktLinks);
}


/*
 ***********************************************************************
 * vmk_PktListGetTail --                                          */ /**
 *
 * \ingroup PktList
 * \brief Retrieve the packet at the end of a specified packet list.
 *
 * \param[in]  pktList Target packet list.
 * 
 * \retval     NULL        The list is empty.
 * \return                 A pointer to the tail of the list.
 *
 ***********************************************************************
 */

static inline vmk_PktHandle *
vmk_PktListGetTail(vmk_PktList *pktList)
{
   VMK_ASSERT(pktList);
   return (vmk_PktHandle *) vmk_CSListLast(&pktList->csList);
}

/*
 ***********************************************************************
 * vmk_PktListPopHead --                                          */ /**
 *
 * \ingroup PktList
 * \brief Pop the packet in front of a specified packet list.
 *
 * \param[in]  pktList Target packet list
 *
 * \return             The head of the list
 *
 ***********************************************************************
 */

static inline vmk_PktHandle *
vmk_PktListPopHead(vmk_PktList *pktList)
{
   VMK_ASSERT(pktList);
   return (vmk_PktHandle *) vmk_CSListPop(&pktList->csList);   
}

/*
 ***********************************************************************
 * vmk_PktListRemoveFast --                                       */ /**
 *
 * \ingroup PktList
 * \brief Fast method to remove a specified packet in a specified packet list.
 *
 * \note The caller needs to provide the previous packet of the removed
 *       one in order to use this optimization.
 *
 * \param[in] pktList Target packet list
 * \param[in] pkt     Packet to be removed
 * \param[in] ppkt    Packet preceding the removed one
 *
 ***********************************************************************
 */

static inline void
vmk_PktListRemoveFast(vmk_PktList *pktList,
                      vmk_PktHandle *pkt,
                      vmk_PktHandle *ppkt)
{
   VMK_ASSERT(pktList);
   VMK_ASSERT(pkt);

   /* This is required for the prev == NULL case to work */
#ifndef __cplusplus
   VMK_ASSERT_ON_COMPILE(VMK_SLIST_ENTRY(ppkt, vmk_PktHandle, pktLinks) == ppkt);
#endif

   vmk_CSListRemove(&pktList->csList, &pkt->pktLinks, &ppkt->pktLinks);
}

/*
 ***********************************************************************
 * vmk_PktListRemove --                                           */ /**
 *
 * \ingroup PktList
 * \brief Remove a specified packet in a specified packet list.
 *
 * \param[in] pktList Target packet list
 * \param[in] pkt     Packet to be removed
 *
 ***********************************************************************
 */

static inline void
vmk_PktListRemove(vmk_PktList *pktList,
                  vmk_PktHandle *pkt)
{
   vmk_PktHandle *prevEntry;
   VMK_ASSERT(pktList);
   VMK_ASSERT(pkt);
   VMK_ASSERT(vmk_PktListCount(pktList));

   prevEntry = vmk_PktListGetPrev(pktList, pkt);
   vmk_PktListRemoveFast(pktList, pkt, prevEntry);
}

/*
 ***********************************************************************
 * vmk_PktListJoin --                                             */ /**
 *
 * \ingroup PktList
 * \brief Remove and append all the packets of one sourcepacket list at
 *        the end of a destination packet list.
 *
 * \note  After this call the source packet list is empty.
 *
 * \param[in] pktListDest Packet list destination
 * \param[in] pktListSrc  Packet list source
 *
 *
 ***********************************************************************
 */

static inline void
vmk_PktListJoin(vmk_PktList *pktListDest,
                vmk_PktList *pktListSrc)
{
   VMK_ASSERT(pktListDest);
   VMK_ASSERT(pktListSrc);

   vmk_CSListAppend(&pktListDest->csList, &pktListSrc->csList);
}

/*
 ***********************************************************************
 * vmk_PktListReleasePkts --                                      */ /**
 *
 * \ingroup PktList
 * \brief Release all the packets of a specified packet list.
 *
 * \note This function take care of the packet Tx completion.
 *
 * \param[in] pktList Target packet list
 *
 ***********************************************************************
 */

extern void
vmk_PktListReleasePkts(vmk_PktList *pktList);

/*
 ***********************************************************************
 * vmk_PktListAppendN --                                          */ /**
 *
 * \ingroup PktList
 * \brief Remove and append the first n packets of a source packet 
 *        list at the end of a destination packet list.
 *
 * \param[in] pktListDest Packet list destination
 * \param[in] pktListSrc  Packet list source
 * \param[in] numPkts     Number of packets at the front to appended
 *
 ***********************************************************************
 */

static inline void
vmk_PktListAppendN(vmk_PktList *pktListDest,
                   vmk_PktList *pktListSrc,
                   vmk_uint32 numPkts)
{
   VMK_ASSERT(pktListDest);
   VMK_ASSERT(pktListSrc);

   vmk_CSListAppendN(&pktListDest->csList, &pktListSrc->csList, numPkts);
}

/*
 ***********************************************************************
 * vmk_PktListPrepend --                                          */ /**
 *
 * \ingroup PktList
 * \brief Join two lists by prepending the source list before the
 *        destination.
 *
 * \param[in] pktListDest Packet list destination
 * \param[in] pktListSrc  Packet list source
 *
 ***********************************************************************
 */

static inline void
vmk_PktListPrepend(vmk_PktList *pktListDest, vmk_PktList *pktListSrc)
{
   VMK_ASSERT(pktListDest);
   VMK_ASSERT(pktListSrc);

   vmk_CSListPrepend(&pktListDest->csList, &pktListSrc->csList);
}

/*
 ***********************************************************************
 * vmk_PktListReplace --                                          */ /**
 *
 * \ingroup PktList
 * \brief Replace the given entry with a new entry. Runs O(1).
 *
 * \param[in] pktList     Packet list destination
 * \param[in] targetEntry Entry to replace
 * \param[in] newEntry    New entry
 * \param[in] prevEntry   Predecessor of the entry to replace
 *
 ***********************************************************************
 */

static inline void
vmk_PktListReplace(vmk_PktList *pktList, 
                   vmk_PktHandle *targetEntry,
                   vmk_PktHandle *newEntry, 
                   vmk_PktHandle *prevEntry)
{
   VMK_ASSERT(pktList);
   VMK_ASSERT(vmk_PktListCount(pktList));
   VMK_ASSERT(targetEntry);

   vmk_CSListReplace(&pktList->csList, &targetEntry->pktLinks,
                     &newEntry->pktLinks, &prevEntry->pktLinks);
}

/*
 ***********************************************************************
 * vmk_PktListRxProcess --                                        */ /**
 *
 * \ingroup PktList
 * \brief Process a list of packets from an uplink.
 *
 * \param[in] pktList   Set of packets to process.
 * \param[in] uplink    Uplink from where the packets came from.
 *
 ***********************************************************************
 */

extern void
vmk_PktListRxProcess(vmk_PktList *pktList,
                     vmk_Uplink *uplink);

/*
 ***********************************************************************
 * vmk_PktTcpSegmentation --                                      */ /**
 *
 * \ingroup PktList
 *
 * \brief Software TSO. The TSO friendly TCP/IP stack will make sure
 *        the next IP frame won't have a conflicting IP id #. ipId
 *        are incremented starting with the ident of the original
 *        (non-segmented) packet.
 *
 * \note It is the responsibility of the caller to ensure the entire
 *       ip/tcp header is contained within frameVA/frameMappedLen.
 *
 * \param[in]   pkt         Packet to segment
 * \param[out]  pktList     List of segment packets
 *
 * \retval      VMK_OK      If the segmentation was successful
 * \retval      VMK_FAILURE Otherwise
 *
 ***********************************************************************
 */

extern VMK_ReturnStatus
vmk_PktTcpSegmentation(vmk_PktHandle *pkt,
                       vmk_PktList *pktList);

#endif /* _VMKAPI_NET_PKTLIST_H_ */
/** @} */
/** @} */
