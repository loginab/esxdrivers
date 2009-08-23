/* **********************************************************
 * Copyright 2006 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Netqueue                                                       */ /**
 * \addtogroup Network
 *@{
 * \defgroup Netqueue Netqueue
 *@{ 
 *
 ***********************************************************************
 */

#ifndef _VMKAPI_NET_NETQUEUE_H_
#define _VMKAPI_NET_NETQUEUE_H_

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

typedef vmk_uint64                         vmk_NetqueueFeatures;
typedef vmk_uint8                          vmk_NetqueueQueueType;
typedef vmk_uint64                         vmk_NetqueueQueueId;
typedef vmk_uint32                         vmk_NetqueueFilterId;
typedef vmk_uint8                          vmk_NetqueueOp;
typedef vmk_VlanPriority                   vmk_NetqueuePriority;
typedef struct VMKNetqueueFilter           vmk_NetqueueFilter;
typedef struct NetqOP_GetVersionArgs       vmk_NetqueueOpGetVersionArgs;
typedef struct NetqOP_GetFeaturesArgs      vmk_NetqueueOpGetFeaturesArgs;
typedef struct NetqOP_GetQueueCountArgs    vmk_NetqueueOpGetQueueCountArgs;
typedef struct NetqOP_GetFilterCountArgs   vmk_NetqueueOpGetFilterCountArgs;
typedef struct NetqOP_AllocQueueArgs       vmk_NetqueueOpAllocQueueArgs;
typedef struct NetqOP_FreeQueueArgs        vmk_NetqueueOpFreeQueueArgs;
typedef struct NetqOP_GetQueueVectorArgs   vmk_NetqueueOpGetQueueVectorArgs;
typedef struct NetqOP_GetDefaultQueueArgs  vmk_NetqueueOpGetDefaultQueueArgs;
typedef struct NetqOP_ApplyRxFilterArgs    vmk_NetqueueOpApplyRxFilterArgs;
typedef struct NetqOP_RemoveRxFilterArgs   vmk_NetqueueOpRemoveRxFilterArgs;
typedef struct NetqOP_SetTxPriorityArgs    vmk_NetqueueOpSetTxPriorityArgs;
typedef struct NetqOP_GetSetQueueStateArgs vmk_NetqueueOpGetSetQueueStateArgs;


/*
 ***********************************************************************
 * vmk_NetqueueFeaturesRxQueueSet --                              */ /**
 *
 * \ingroup Netqueue
 * \brief Set Rx queue feature to "features"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueFeaturesRxQueuesSet(vmk_NetqueueFeatures *features);

/*
 ***********************************************************************
 * vmk_NetqueueFeaturesTxQueueSet --                              */ /**
 *
 * \ingroup Netqueue
 * \brief Set Tx queue feature to "features"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueFeaturesTxQueuesSet(vmk_NetqueueFeatures *features);

/*
 ***********************************************************************
 * vmk_NetqueueQueueTypeIsTx --                                   */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the queue type "qtype" is Tx
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueQueueTypeIsTx(vmk_NetqueueQueueType qtype);

/*
 ***********************************************************************
 * vmk_NetqueueQueueTypeIsRx --                                   */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the queue type "qtype" is Rx
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueQueueTypeIsRx(vmk_NetqueueQueueType qtype);

/*
 ***********************************************************************
 * vmk_NetqueueIsTxQueueId --                                     */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the queue corresponding to "qid" is of a type Tx
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueIsTxQueueId(vmk_NetqueueQueueId qid);

/*
 ***********************************************************************
 * vmk_NetqueueIsRxQueueId --                                     */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the queue corresponding to "qid" is of a type Rx
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueIsRxQueueId(vmk_NetqueueQueueId qid);

/*
 ***********************************************************************
 * vmk_NetqueueQueueMkTxQueueId --                                */ /**
 *
 * \ingroup Netqueue
 * \brief Make the queue "qid" of a Tx type with value "val"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueQueueMkTxQueueId(vmk_NetqueueQueueId *qid,
					      vmk_uint32 val);

/*
 ***********************************************************************
 * vmk_NetqueueQueueMkRxQueueId --                                */ /**
 *
 * \ingroup Netqueue
 * \brief Make the queue "qid" of a Rx type with value "val"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueQueueMkRxQueueId(vmk_NetqueueQueueId *qid,
					      vmk_uint32 val);

/*
 ***********************************************************************
 * vmk_NetqueueQueueIdVal --                                      */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve value of a queue id "qid"
 *
 ***********************************************************************
 */

vmk_uint32 vmk_NetqueueQueueIdVal(vmk_NetqueueQueueId qid);

/*
 ***********************************************************************
 * vmk_NetqueueFilterClassIsMacAddr --                            */ /**
 *
 * \ingroup Netqueue
 * \brief Check if filter "filter" belongs to mac addr class
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueFilterClassIsMacAddr(vmk_NetqueueFilter *filter);

/*
 ***********************************************************************
 * vmk_NetqueueFilterClassIsVlanMacAddr --                        */ /**
 *
 * \ingroup Netqueue
 * \brief Check if filter "filter" belongs to vlan mac addr class
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueFilterClassIsVlanMacAddr(vmk_NetqueueFilter *filter);

/*
 ***********************************************************************
 * vmk_NetqueueFilterClassIsVlan --                               */ /**
 *
 * \ingroup Netqueue
 * \brief Check if filter "filter" belongs to vlan class
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueFilterClassIsVlan(vmk_NetqueueFilter *filter);

/*
 ***********************************************************************
 * vmk_NetqueueFilterMacAddrGet --                                */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve mac addr of a filter "filter"
 *
 ***********************************************************************
 */

vmk_uint8 *vmk_NetqueueFilterMacAddrGet(vmk_NetqueueFilter *filter);

/*
 ***********************************************************************
 * vmk_NetqueueMkFilterId --                                      */ /**
 *
 * \ingroup Netqueue
 * \brief Set filter id "fid" with value "val"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueMkFilterId(vmk_NetqueueFilterId *fid,
					vmk_uint16 val);

/*
 ***********************************************************************
 * vmk_NetqueueFilterIdVal --                                     */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve value of a filter id "fid"
 *
 ***********************************************************************
 */

vmk_uint16 vmk_NetqueueFilterIdVal(vmk_NetqueueFilterId fid);

/*
 ***********************************************************************
 * vmk_NetqueueOpGetVersionArgsMajorSet --                        */ /**
 *
 * \ingroup Netqueue
 * \brief Set the major of "args" with "major"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueOpGetVersionArgsMajorSet(vmk_NetqueueOpGetVersionArgs *args,
						      vmk_uint16 major);

/*
 ***********************************************************************
 * vmk_NetqueueOpGetVersionArgsMinorSet --                        */ /**
 *
 * \ingroup Netqueue
 * \brief Set the minor of "args" with "minor"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueOpGetVersionArgsMinorSet(vmk_NetqueueOpGetVersionArgs *args,
						      vmk_uint16 minor);

/*
 ***********************************************************************
 * vmk_NetqueueOpGetFeaturesArgsFeaturesSet --                    */ /**
 *
 * \ingroup Netqueue
 * \brief Set the features of "args" with "features"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueOpGetFeaturesArgsFeatureSet(vmk_NetqueueOpGetFeaturesArgs *args,
							 vmk_NetqueueFeatures features);

/*
 ***********************************************************************
 * vmk_NetqueueOpGetQueueCountArgsCountSet --                     */ /**
 *
 * \ingroup Netqueue
 * \brief Set the count of "args" with "count"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueOpGetQueueCountArgsCountSet(vmk_NetqueueOpGetQueueCountArgs *args,
							 vmk_uint16 count);

/*
 ***********************************************************************
 * vmk_NetqueueOpGetQueueCountArgsQueueTypeGet --                 */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve the queue type of "args"
 *
 ***********************************************************************
 */

vmk_NetqueueQueueType vmk_NetqueueOpGetQueueCountArgsQueueTypeGet(vmk_NetqueueOpGetQueueCountArgs *args);

/*
 ***********************************************************************
 * vmk_NetqueueOpGetFilterCountArgsCountSet --                    */ /**
 *
 * \ingroup Netqueue
 * \brief Set the count of "args" with "count"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueOpGetFilterCountArgsCountSet(vmk_NetqueueOpGetFilterCountArgs *args,
							  vmk_uint16 count);

/*
 ***********************************************************************
 * vmk_NetqueueOpGetFilterCountArgsQueueTypeGet --                    */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve the queue type of "args"
 *
 ***********************************************************************
 */

vmk_NetqueueQueueType vmk_NetqueueOpGetFilterCountArgsQueueTypeGet(vmk_NetqueueOpGetFilterCountArgs *args);
	
/*
 ***********************************************************************
 * vmk_NetqueueOpAllocQueueArgsQueueTypeGet --                        */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve the queue type of "args"
 *
 ***********************************************************************
 */
 
vmk_NetqueueQueueType vmk_NetqueueOpAllocQueueArgsQueueTypeGet(vmk_NetqueueOpAllocQueueArgs *args);

/*
 ***********************************************************************
 * vmk_NetqueueOpAllocQueueArgsWorldletSet --                     */ /**
 *
 * \ingroup Netqueue
 * \brief Set worldlet of args with "worldlet"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueOpAllocQueueArgsWorldletSet(vmk_NetqueueOpAllocQueueArgs *args,
                                                         void *worldlet);

/*
 ***********************************************************************
 * vmk_NetqueueOpAllocQueueArgsQueueIdSet --                      */ /**
 *
 * \ingroup Netqueue
 * \brief Set the queue id of "args" with "qid"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueOpAllocQueueArgsQueueIdSet(vmk_NetqueueOpAllocQueueArgs *args,
							vmk_NetqueueQueueId qid);

/*
 ***********************************************************************
 * vmk_NetqueueOpFreeQueueArgsQueueIdGet --                       */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve the queue id of "args"
 *
 ***********************************************************************
 */

vmk_NetqueueQueueId vmk_NetqueueOpFreeQueueArgsQueueIdGet(vmk_NetqueueOpFreeQueueArgs *args);

/*
 ***********************************************************************
 * vmk_NetqueueOpGetQueueVectorArgsQueueIdGet --                  */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve the queue id of "args"
 *
 ***********************************************************************
 */

vmk_NetqueueQueueId vmk_NetqueueOpGetQueueVectorArgsQueueIdGet(vmk_NetqueueOpGetQueueVectorArgs *args);

/*
 ***********************************************************************
 * vmk_NetqueueOpGetQueueVectorArgsVectorSet --                   */ /**
 *
 * \ingroup Netqueue
 * \brief Set the vector of "args" with "vector"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueOpGetQueueVectorArgsVectorSet(vmk_NetqueueOpGetQueueVectorArgs *args,
							   vmk_uint16 vector);

/*
 ***********************************************************************
 * vmk_NetqueueOpGetDefaultQueueArgsWorldletSet                   */ /**
 *
 * \ingroup Netqueue
 * \brief Set worldlet of args with "worldlet"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueOpGetDefaultQueueArgsWorldletSet(vmk_NetqueueOpGetDefaultQueueArgs *args,
                                                         void *worldlet);

/*
 ***********************************************************************
 * vmk_NetqueueOpGetDefaultQueueArgsQueueTypeGet --                   */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve the queue type of "args"
 *
 ***********************************************************************
 */

vmk_NetqueueQueueType vmk_NetqueueOpGetDefaultQueueArgsQueueTypeGet(vmk_NetqueueOpGetDefaultQueueArgs *args);	

/*
 ***********************************************************************
 * vmk_NetqueueOpGetDefaultQueueArgsQueueIdSet --                 */ /**
 *
 * \ingroup Netqueue
 * \brief Set the queue id of "args" with "qid"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueOpGetDefaultQueueArgsQueueIdSet(vmk_NetqueueOpGetDefaultQueueArgs *args,
							     vmk_NetqueueQueueId qid);

/*
 ***********************************************************************
 * vmk_NetqueueOpApplyRxFilterArgsQueueIdGet --                   */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve the queue id of "args"
 *
 ***********************************************************************
 */

vmk_NetqueueQueueId vmk_NetqueueOpApplyRxFilterArgsQueueIdGet(vmk_NetqueueOpApplyRxFilterArgs *args);	      

/*
 ***********************************************************************
 * vmk_NetqueueOpApplyRxFilterArgsFilterGet --                    */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve the filter of "args"
 *
 ***********************************************************************
 */

vmk_NetqueueFilter *vmk_NetqueueOpApplyRxFilterArgsFilterGet(vmk_NetqueueOpApplyRxFilterArgs *args);

/*
 ***********************************************************************
 * vmk_NetqueueOpApplyRxFilterArgsFilterIdGet --                  */ /**
 *
 * \ingroup Netqueue
 * \brief Set the filter id of "args" with "fid"
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_NetqueueOpApplyRxFilterArgsFilterIdSet(vmk_NetqueueOpApplyRxFilterArgs *args,
						vmk_NetqueueFilterId fid);

/*
 ***********************************************************************
 * vmk_NetqueueOpRemoveRxFilterArgsQueueIdGet --                   */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve the queue id of "args"
 *
 ***********************************************************************
 */

vmk_NetqueueQueueId vmk_NetqueueOpRemoveRxFilterArgsQueueIdGet(vmk_NetqueueOpRemoveRxFilterArgs *args);

/*
 ***********************************************************************
 * vmk_NetqueueOpRemoveRxFilterArgsFilterIdGet --                 */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve the filter id of "args"
 *
 ***********************************************************************
 */

vmk_NetqueueFilterId vmk_NetqueueOpRemoveRxFilterArgsFilterIdGet(vmk_NetqueueOpRemoveRxFilterArgs *args);

/*
 ***********************************************************************
 * vmk_NetqueueOpSetTxPriorityArgsQueueIdGet --                   */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve the queue id of "args"
 *
 ***********************************************************************
 */

vmk_NetqueueQueueId vmk_NetqueueOpSetTxPriorityArgsQueueIdGet(vmk_NetqueueOpSetTxPriorityArgs *args);

/*
 ***********************************************************************
 * vmk_NetqueueOpSetTxPriorityArgsPriorityGet --                   */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve the priority of "args"
 *
 ***********************************************************************
 */

vmk_NetqueuePriority vmk_NetqueueOpSetTxPriorityArgsPriorityGet(vmk_NetqueueOpSetTxPriorityArgs *args);

/*
 ***********************************************************************
 * vmk_NetqueueOpGetSetQueueStateArgsSetOldState --                 */ /**
 *
 * \ingroup Netqueue
 * \brief Retrieve the old state of netqueue in the device
 *
 ***********************************************************************
 */

VMK_ReturnStatus
vmk_NetqueueOpGetSetQueueStateArgsSetOldState(vmk_NetqueueOpGetSetQueueStateArgs *args,
                                              vmk_Bool valid);

/*
 ***********************************************************************
 * vmk_NetqueueOpGetSetQueueStateArgsGetNewState --                 */ /**
 *
 * \ingroup Netqueue
 * \brief set the new state of netqueue in the device
 *
 ***********************************************************************
 */

vmk_Bool 
vmk_NetqueueOpGetSetQueueStateArgsGetNewState(vmk_NetqueueOpGetSetQueueStateArgs *args);


/*
 ***********************************************************************
 * vmk_NetqueueOpIsGetVersion --                                  */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the netqueue operation "op" belongs to 
 *        GET_VERSION class
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueOpIsGetVersion(vmk_NetqueueOp op);

/*
 ***********************************************************************
 * vmk_NetqueueOpIsGetFeatures --                                  */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the netqueue operation "op" belongs to 
 *        GET_FEATURES class
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueOpIsGetFeatures(vmk_NetqueueOp op);

/*
 ***********************************************************************
 * vmk_NetqueueOpIsQueueCount --                                  */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the netqueue operation "op" belongs to 
 *        QUEUE_COUNT class
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueOpIsQueueCount(vmk_NetqueueOp op);

/*
 ***********************************************************************
 * vmk_NetqueueOpIsFilterCount --                                 */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the netqueue operation "op" belongs to 
 *        FILTER_COUNT class
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueOpIsFilterCount(vmk_NetqueueOp op);

/*
 ***********************************************************************
 * vmk_NetqueueOpIsAllocQueue --                                  */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the netqueue operation "op" belongs to 
 *        ALLOC_QUEUE class
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueOpIsAllocQueue(vmk_NetqueueOp op);

/*
 ***********************************************************************
 * vmk_NetqueueOpIsFreeQueue --                                   */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the netqueue operation "op" belongs to 
 *        FREE_QUEUE class
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueOpIsFreeQueue(vmk_NetqueueOp op);

/*
 ***********************************************************************
 * vmk_NetqueueOpIsGetQueueVector --                              */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the netqueue operation "op" belongs to 
 *        GET_QUEUE_VECTOR class
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueOpIsGetQueueVector(vmk_NetqueueOp op);

/*
 ***********************************************************************
 * vmk_NetqueueOpIsGetDefaultQueue --                             */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the netqueue operation "op" belongs to 
 *        GET_DEFAULT_QUEUE class
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueOpIsGetDefaultQueue(vmk_NetqueueOp op);

/*
 ***********************************************************************
 * vmk_NetqueueOpIsApplyRxFilter --                               */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the netqueue operation "op" belongs to 
 *        APPLY_RX_FILTER class
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueOpIsApplyRxFilter(vmk_NetqueueOp op);

/*
 ***********************************************************************
 * vmk_NetqueueOpIsRemoveRxFilter --                              */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the netqueue operation "op" belongs to 
 *        REMOVE_RX_FILTER class
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueOpIsRemoveRxFilter(vmk_NetqueueOp op);

/*
 ***********************************************************************
 * vmk_NetqueueOpIsGetQueueStats --                               */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the netqueue operation "op" belongs to 
 *        GET_QUEUE_STATS class
 *
 ***********************************************************************
 */

vmk_Bool vmk_NetqueueOpIsGetQueueStats(vmk_NetqueueOp op);

/*
 ***********************************************************************
 * vmk_NetqueueOpIsSetTxPriority --                               */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the netqueue operation "op" belongs to 
 *        SET_TX_PRIORITY class
 *
 ***********************************************************************
 */
vmk_Bool vmk_NetqueueOpIsSetTxPriority(vmk_NetqueueOp op);

/*
 ***********************************************************************
 * vmk_NetqueueOpIsGetSetState --                                 */ /**
 *
 * \ingroup Netqueue
 * \brief Check if the netqueue operation "op" belongs to 
 *        GETSET_QUEUE_STATE class
 *
 ***********************************************************************
 */
vmk_Bool vmk_NetqueueOpIsGetSetState(vmk_NetqueueOp op);

#endif /* _VMKAPI_NET_PKTLIST_H_ */
/** @} */
/** @} */
