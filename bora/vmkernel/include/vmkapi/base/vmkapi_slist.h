/* **********************************************************
 * Copyright 2008 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * SList                                                          */ /**
 * \defgroup SList Singly-linked List Management
 *
 * Singly-linked lists.
 * 
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_SLIST_H_
#define _VMKAPI_SLIST_H_

/** \cond never */
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMMEXT
#define INCLUDE_ALLOW_VMNIXMOD
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_const.h"
#include "base/vmkapi_types.h"
#include "base/vmkapi_assert.h"

/**
 * \brief Singly-linked list link.
 *
 * This link can be embedded within other data structures to allow them
 * to be added to a list.
 */
typedef struct vmk_SList_Links {
   struct vmk_SList_Links *next;
} vmk_SList_Links;

/**
 * \brief A singly-linked list.
 *
 * This structure represents the entire list.
 */
typedef struct vmk_SList {
   vmk_SList_Links *head;
   vmk_SList_Links *tail;
} vmk_SList;

/*
 ***********************************************************************
 * VMK_SLIST_ENTRY --                                             */ /**
 *
 * \ingroup SList
 * \brief Get a pointer to the structure containing a given list element.
 *
 * \param itemPtr  List element that is contained by another structure
 * \param STRUCT   C type of the container
 * \param MEMBER   Name of the structure field in the container
 *                 that itemPtr is pointing to.
 *
 ***********************************************************************
 */

#define VMK_SLIST_ENTRY(itemPtr, STRUCT, MEMBER) \
  ((STRUCT *) ((vmk_uint8 *) (itemPtr) - vmk_offsetof (STRUCT, MEMBER)))

/*
 ***********************************************************************
 * VMK_SLIST_FORALL --                                            */ /**
 *
 * \ingroup SList
 * \brief for-loop replacement macro to scan through a list from
 *        the first to the last list member
 *
 * \note Expressions that contain side effects aren't valid as 
 *       parameters (example: removal).
 *
 * \param list     The list to scan
 * \param current  Loop pointer that is updated with the current
 *                 list member each time through the loop
 *
 ***********************************************************************
 */

#define VMK_SLIST_FORALL(list, current) \
        for ((current) = (list)->head; \
             (current) != NULL; \
             (current) = (current)->next)

/*
 ***********************************************************************
 * VMK_SLIST_FORALL_SAFE --                                       */ /**
 *
 * \ingroup SList
 * \brief for-loop replacement macro to scan through a list from
 *        the first to the last list member
 *
 * \note This macro can be used when removal of element is needed while
 *       looping.
 *
 * \note This macro is pretty inefficient as it maintains 3 pointers
 *       per iteration and has additional comparisons to do so. In
 *       case of performance critical loops it is advisable not to use
 *       it and do the necessary bookkeeping (a subset of this)
 *       manually.
 *
 * \param list     The list to scan
 * \param prevPtr  Loop pointer that is updated each time through the
 *                 loop with the previous list member
 * \param current  Loop pointer that is updated each time through the
 *                 loop with the current list member 
 * \param nextPtr  Loop pointer that is updated each time through the
 *                 loop with the next list member
 *
 ***********************************************************************
 */

#define VMK_SLIST_FORALL_SAFE(list, prevPtr, current, nextPtr) \
        for ((prevPtr) = NULL, (current) = (list)->head, \
               (nextPtr) = (current) ? (current)->next : NULL; \
               (current) != NULL; \
               (prevPtr) = (!(prevPtr) && ((list)->head != (current)))? NULL : \
               ((prevPtr) && ((prevPtr)->next == (nextPtr)) ? (prevPtr) : (current)), \
                (current) = (nextPtr), (nextPtr) = (current) ? (current)->next : NULL)

/*
 ***********************************************************************
 * VMK_SLIST_FORALL_AFTER --                                      */ /**
 *
 * \ingroup SList
 * \brief for-loop replacement macro to scan through a list from
 *        the current to the last list member
 *
 * \note Expressions that contain side effects aren't valid as 
 *       parameters (example: removal).
 *
 * \param list     The list to scan
 * \param current  Loop pointer that indicate the element where to start
 *                 and is updated with the current list member each time
 *                 through the loop
 *
 ***********************************************************************
 */

#define VMK_SLIST_FORALL_AFTER(list, current) \
        for (; \
             (current) != NULL; \
             (current) = (current)->next)

/*
 ***********************************************************************
 * VMK_SLIST_FORN --                                              */ /**
 *
 * \ingroup SList
 * \brief for-loop replacement macro to scan through a list from
 *        the first to the nth list member
 *
 * \note Expressions that contain side effects aren't valid as 
 *       parameters (example: removal).
 *
 * \note On exit, current points to the (n+1)th member or NULL if the
 *       list has a number of members lower than or equal to n.
 *
 * \param list     The list to scan
 * \param current  Loop pointer that is updated with the current
 *                 list member each time through the loop
 * \param n        Index of the element where to stop
 *
 ***********************************************************************
 */

#define VMK_SLIST_FORN(list, current, n) \
        for ((current) = (list)->head; \
             ((current) != NULL) && (n != 0); \
             (current) = (current)->next, n--)

/*
 ***********************************************************************
 * vmk_SListInitElement --                                        */ /**
 *
 * \ingroup SList
 * \brief Initialize a list link.
 *
 * \param[in]  element     Element to initialize
 *
 ***********************************************************************
 */
static inline void
vmk_SListInitElement(vmk_SList_Links *element)
{
   VMK_ASSERT(element);
   element->next = NULL;
}

/*
 ***********************************************************************
 * vmk_SListIsEmpty --                                            */ /**
 *
 * \ingroup SList
 * \brief Checks whether a list is empty or not.
 *
 * \param[in]  list        Target list
 *
 * \retval     VMK_TRUE    The list is empty
 * \retval     VMK_FALSE   The list is not empty
 *
 ***********************************************************************
 */
static inline vmk_Bool
vmk_SListIsEmpty(vmk_SList *list)
{
   VMK_ASSERT(list);
   return list->head == NULL ? VMK_TRUE : VMK_FALSE;
}

/*
 ***********************************************************************
 * vmk_SListFirst --                                              */ /**
 *
 * \ingroup SList
 * \brief Returns the first element (head) of a list.
 *
 * \param[in]  list        Target list
 *
 * \retval     NULL        The list is empty
 * \return                 A pointer to the head of the list
 *
 ***********************************************************************
 */
static inline vmk_SList_Links *
vmk_SListFirst(vmk_SList *list)
{
   VMK_ASSERT(list);
   return list->head;
}

/*
 ***********************************************************************
 * vmk_SListLast --                                               */ /**
 *
 * \ingroup SList
 * \brief Returns the last element (tail) of a list.
 *
 * \param[in]  list        Target list
 *
 * \retval     NULL        The list is empty
 * \return                 A pointer to the tail of the list
 *
 ***********************************************************************
 */
static inline vmk_SList_Links *
vmk_SListLast(vmk_SList *list)
{
   VMK_ASSERT(list);
   return list->tail;
}

/*
 ***********************************************************************
 * vmk_SListNext --                                               */ /**
 *
 * \ingroup SList
 * \brief Returns the following link in a list.
 *
 * \param[in]  element     Target list
 *
 * \retval     NULL        We are at the end of the list
 * \return                 A pointer to the next element
 *
 ***********************************************************************
 */
static inline vmk_SList_Links *
vmk_SListNext(vmk_SList_Links *element)
{
   VMK_ASSERT(element);
   return element->next;
}

/*
 ***********************************************************************
 * vmk_SListInit --                                               */ /**
 *
 * \ingroup SList
 * \brief Initializes a list to be an empty list.
 *
 * \param[in]  list        Target list
 *
 ***********************************************************************
 */
static inline void
vmk_SListInit(vmk_SList *list)
{
   VMK_ASSERT(list);
   list->head = NULL;
   list->tail = NULL;
}

/*
 ***********************************************************************
 * vmk_SListPrev --                                               */ /**
 *
 * \ingroup SList
 * \brief Returns the previous element in a list. Runs in O(n).
 *
 * \param[in]  list        Target list
 * \param[in]  element     Element whose previous element is asked for
 *
 * \retval     NULL        We are at the beginning of the list
 * \return                 A pointer to the previous element
 *
 ***********************************************************************
 */
static inline vmk_SList_Links *
vmk_SListPrev(vmk_SList *list, vmk_SList_Links *element)
{
   vmk_SList_Links *cur;
   VMK_ASSERT(list);
   VMK_ASSERT(element);

   if (element == list->head) {
      return NULL;
   }

   VMK_SLIST_FORALL(list, cur) {
      if (cur->next == element) {
         return cur;
      }
   }

   VMK_ASSERT(0); /* Element not on the list. */

   return NULL;
}

/*
 ***********************************************************************
 * vmk_SListPop --                                                */ /**
 *
 * \ingroup SList
 * \brief Returns the first element (head) of a list and remove it
 * from the list.
 *
 * \param[in]  list        Target list
 *
 * \return                 A pointer to the head of the list
 *
 ***********************************************************************
 */
static inline vmk_SList_Links *
vmk_SListPop(vmk_SList *list)
{
   vmk_SList_Links *oldhead;
   VMK_ASSERT(list);

   oldhead = list->head;
   VMK_ASSERT(oldhead);

   list->head = oldhead->next;

   if (list->head == NULL) {
      list->tail = NULL;
   }

   oldhead->next = NULL;

   return oldhead;
}

/*
 ***********************************************************************
 * vmk_SListInsertAtHead --                                       */ /**
 *
 * \ingroup SList
 * \brief Inserts a given element at the beginning of the list.
 *
 * \param[in]  list        Target list
 * \param[in]  element     Element to insert
 *
 ***********************************************************************
 */
static inline void
vmk_SListInsertAtHead(vmk_SList *list, vmk_SList_Links *element)
{
   VMK_ASSERT(list);
   VMK_ASSERT(element);

   element->next = list->head;

   if (list->tail == NULL) {
      VMK_ASSERT(list->head == NULL);
      list->tail = element;
   }

   list->head = element;
}

/*
 ***********************************************************************
 * vmk_SListInsertAtTail --                                       */ /**
 *
 * \ingroup SList
 * \brief Inserts a given element at the end of the list.
 *
 * \param[in]  list        Target list
 * \param[in]  element     Element to insert
 *
 ***********************************************************************
 */
static inline void
vmk_SListInsertAtTail(vmk_SList *list, vmk_SList_Links *element)
{
   VMK_ASSERT(list);
   VMK_ASSERT(element);

   if (list->tail == NULL) {
      list->head = element;
      list->tail = element;
   } else {
      list->tail->next = element;
      list->tail = element;
   }

   element->next = NULL;
}

/*
 ***********************************************************************
 * vmk_SListInsertAfter --                                        */ /**
 *
 * \ingroup SList
 * \brief Inserts an element after a given element.
 *
 * \param[in]  list        Target list
 * \param[in]  element     Element to insert
 * \param[in]  other       Element to insert the new element after
 *
 ***********************************************************************
 */
static inline void
vmk_SListInsertAfter(vmk_SList *list, vmk_SList_Links *element, vmk_SList_Links *other)
{
   VMK_ASSERT(list);
   VMK_ASSERT(element);
   VMK_ASSERT(other);
   VMK_ASSERT(list->head != NULL);

   element->next = other->next;
   other->next = element;

   if (list->tail == other) {
      list->tail = element;
   }
}

/*
 ***********************************************************************
 * vmk_SListRemove --                                             */ /**
 *
 * \ingroup SList
 * \brief Removes a given element from the list knowing its predecessor
 *
 * \param[in]  list        Target list
 * \param[in]  element     Element to remove
 * \param[in]  prev        Element preceding the element to remove
 *
 ***********************************************************************
 */
static inline void
vmk_SListRemove(vmk_SList *list, vmk_SList_Links *element,
                vmk_SList_Links *prev)
{
   VMK_ASSERT(list);
   VMK_ASSERT(element);

   if (prev) {
      VMK_ASSERT(prev->next == element);
      prev->next = element->next;
      if (list->tail == element) {
         list->tail = prev;
      }
   } else {
      VMK_ASSERT(list->head == element);
      list->head = element->next;
      if (list->tail == element) {
         list->tail = list->head;
      }
   }

#if defined(VMX86_DEBUG)
   /* don't reinitialize the removed element in release builds */
   vmk_SListInitElement(element);
#endif
}

/*
 ***********************************************************************
 * vmk_SListRemoveSlow --                                         */ /**
 *
 * \ingroup SList
 * \brief Removes a given element from the list. Runs O(n).
 *
 * \param[in]  list        Target list
 * \param[in]  element     Element to remove
 *
 ***********************************************************************
 */
static inline void
vmk_SListRemoveSlow(vmk_SList *list, vmk_SList_Links *element)
{
   vmk_SList_Links *prev = vmk_SListPrev(list, element);
   vmk_SListRemove(list, element, prev);
}

/*
 ***********************************************************************
 * vmk_SListAppend --                                             */ /**
 *
 * \ingroup SList
 * \brief Appends all elements of a list at the end of another.
 *
 * \note The list appended is initialized to an empty list.
 *
 * \param[in]  list1       Target list
 * \param[in]  list2       List to append
 *
 ***********************************************************************
 */
static inline void
vmk_SListAppend(vmk_SList *list1, vmk_SList *list2)
{
   VMK_ASSERT(list1);
   VMK_ASSERT(list2);

   if (!list2->head) {
      return; /* second list empty, nothing to append. */
   }

   if (!list1->head) {
      list1->head = list2->head;
      list1->tail = list2->tail;
   } else {
      list1->tail->next = list2->head;
      list1->tail = list2->tail;
   }

   vmk_SListInit(list2);
}

/*
 ***********************************************************************
 * vmk_SListAppendN --                                            */ /**
 *
 * \ingroup SList
 * \brief Appends N elements of a list at the end of another.
 *
 * \note The list appended is initialized to an empty list.
 *
 * \param[in]  listDest    Target list
 * \param[in]  listSrc     List to append
 * \param[in]  num         Number of elements to append
 *
 ***********************************************************************
 */

static inline void
vmk_SListAppendN(vmk_SList *listDest,
                 vmk_SList *listSrc,
                 vmk_uint32 num)
{
   vmk_SList_Links *list1head, *list2tail;
   vmk_uint32 i;

   VMK_ASSERT(listDest);
   VMK_ASSERT(listSrc);

   /* find the last element to be transferred to the destination */
   list1head = listSrc->head;
   list2tail = list1head;
   for (i=0; i<num-1; i++) {
      list2tail = list2tail->next;
      VMK_ASSERT(list2tail);
   }

   /* fix the source list */
   if (list2tail == listSrc->tail) {
      vmk_SListInit(listSrc);
   } else {
      listSrc->head = list2tail->next;
   }

   /* fix the destination list */
   if (listDest->tail != NULL) {
      listDest->tail->next = list1head;
   } else {
      listDest->head = list1head;
   }
   listDest->tail = list2tail;
   list2tail->next = NULL;
}


/*
 ***********************************************************************
 * vmk_SListPrepend --                                            */ /**
 *
 * \ingroup SList
 * \brief Insert all elements of a list at the beginning of another.
 *
 * \note The list prepended is initialized to an empty list.
 *
 * \param[in]  list1       Target list
 * \param[in]  list2       List to prepend
 *
 ***********************************************************************
 */
static inline void
vmk_SListPrepend(vmk_SList *list1, vmk_SList *list2)
{
   VMK_ASSERT(list1);
   VMK_ASSERT(list2);

   if (!list2->head) {
      return; /* second list empty, nothing to prepend. */
   }

   if (!list1->head) {
      list1->head = list2->head;
      list1->tail = list2->tail;
   } else {
      list2->tail->next = list1->head;
      list1->head = list2->head;
   }

   vmk_SListInit(list2);
}

/*
 ***********************************************************************
 * vmk_SListSplitHead --                                          */ /**
 *
 * \ingroup SList
 * \brief Split a list into two list at a given entry.
 *
 * \note The second list must be empty.
 *
 * \param[in]  list1       Target list, becomes left part of the list
 * \param[in]  list2       Right part of the list
 * \param[in]  element     Element where to split, this element is moved
 *                         into list2
 *
 ***********************************************************************
 */
static inline void
vmk_SListSplitHead(vmk_SList *list1, vmk_SList *list2, vmk_SList_Links *element)
{
   VMK_ASSERT(list1);
   VMK_ASSERT(list2);
   VMK_ASSERT(vmk_SListIsEmpty(list2));
   VMK_ASSERT(element);

   list2->head = list1->head;
   list2->tail = element;

   list1->head = element->next;
   if (list1->head == NULL) {
      list1->tail = NULL;
   }

   element->next = NULL;
}

/*
 ***********************************************************************
 * vmk_SListSplitNHead --                                         */ /**
 *
 * \ingroup SList
 * \brief Split a list into two list starting a given element.
 *
 * \note The second list must be empty.
 *
 * \param[in]  list1       Target list, becomes left part of the list
 * \param[in]  list2       Right part of the list
 * \param[in]  n           Index of the element where to start splitting
 *
 ***********************************************************************
 */
static inline void
vmk_SListSplitNHead(vmk_SList *list1, vmk_SList *list2, vmk_uint64 n)
{
   vmk_SList_Links *cur;

   VMK_SLIST_FORN(list1, cur, n);
   if (cur == NULL) {
      vmk_SListAppend(list2, list1);
   } else {
      vmk_SListSplitHead(list1, list2, cur);
   }
}

/*
 ***********************************************************************
 * vmk_SListReplace --                                            */ /**
 *
 * \ingroup SList
 * \brief Replace the given entry with a new entry. Runs O(1).
 *
 * \param[in] list     List destination
 * \param[in] targetEntry Entry to replace
 * \param[in] newEntry    New entry
 * \param[in] prevEntry   Predecessor of the entry to replace
 *
 ***********************************************************************
 */
static inline void
vmk_SListReplace(vmk_SList *list, 
                 vmk_SList_Links *targetEntry,
                 vmk_SList_Links *newEntry, 
                 vmk_SList_Links *prevEntry)
{
   VMK_ASSERT(list);
   VMK_ASSERT(targetEntry);

   if (!prevEntry) {
      VMK_ASSERT(list->head == targetEntry);
      list->head = newEntry;
   } else {
      VMK_ASSERT(prevEntry->next == targetEntry);
      prevEntry->next = newEntry;
   }

   if (list->tail == targetEntry) {
      list->tail = newEntry;
   }

   newEntry->next = targetEntry->next;
   targetEntry->next = NULL;
}

#endif /* _VMKAPI_SLIST_H_ */
/** @} */
