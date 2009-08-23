/***************************************************************************
 * Copyright 2004 - 2009 VMware, Inc.  All rights reserved.
 ***************************************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * SCSI Device management types and constants                     */ /**
 * \addtogroup SCSI
 * @{
 *
 * \defgroup SCSImgmt SCSI Device Management Interfaces
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_SCSI_MGMT_TYPES_H_
#define _VMKAPI_SCSI_MGMT_TYPES_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_const.h"

#define VMKDRIVER_DEV_NAME_LENGTH	64
#define VMK_MAX_IP_STRING_LENGTH 	64
#define VMK_MAX_PORT_STRING_LENGTH 	16
#define VMK_MAX_ISCSI_IQN_LENGTH        224
#define VMK_MAX_ISCSI_ISID_STRING_LENGTH      13
#define VMK_MAX_ISCSI_TPGT_STRING_LENGTH      16
#define VMK_MAX_ETHERNET_MAC_LENGTH     6
#define VMK_MAX_ISCSI_PARAM_LENGTH      256
#define VMK_MAX_ISCSI_UID_LENGTH        256

/* Session related Parameters */
#define  VMK_ISCSI_ADAPTER_IQN          0x00000001
#define  VMK_ISCSI_PARAM_DATAPDU_ORDER  0x00000002
#define  VMK_ISCSI_PARAM_DATASEQ_ORDER 	0x00000004
#define  VMK_ISCSI_PARAM_R2T 		0x00000008
#define  VMK_ISCSI_PARAM_IMMDATA 	0x00000010
#define  VMK_ISCSI_PARAM_ERL 		0x00000020
#define  VMK_ISCSI_PARAM_TIME2WAIT 	0x00000040
#define  VMK_ISCSI_PARAM_TIME2RETAIN 	0x00000080
#define  VMK_ISCSI_PARAM_MAXCONNECTIONS	0x00000100
#define  VMK_ISCSI_PARAM_MAXR2T 	0x00000200
#define  VMK_ISCSI_PARAM_FBL 		0x00000400
#define  VMK_ISCSI_PARAM_MBL 		0x00000800
#define  VMK_ISCSI_PARAM_HDRDIGEST 	0x00001000	
#define  VMK_ISCSI_PARAM_DATADIGEST 	0x00002000
#define  VMK_ISCSI_PARAM_RCVDATASEGMENT	0x00004000
#define  VMK_ISCSI_PARAM_ICHAPNAME 	0x00008000
#define  VMK_ISCSI_PARAM_TCHAPNAME 	0x00010000
#define  VMK_ISCSI_PARAM_ICHAPSECRET	0x00020000
#define  VMK_ISCSI_PARAM_TCHAPSECRET 	0x00040000
#define  VMK_ISCSI_PARAM_ISID           0x00080000

/* Target Information */
#define  VMK_ISCSI_IQN_TARGET_NAME 	0x00100000
#define  VMK_ISCSI_TARGET_IP 		0x00200000
#define  VMK_ISCSI_TARGET_PORT 		0x00400000
#define  VMK_ISCSI_TARGET_TPGT          0x00800000

/* Adapter TCP/IP Properties */
#define  VMK_ISCSI_ADAPTER_IP		0x01000000
#define  VMK_ISCSI_ADAPTER_SUBNET	0x02000000
#define  VMK_ISCSI_ADAPTER_GATEWAY	0x04000000
#define  VMK_ISCSI_ADAPTER_PRIMARY_DNS	0x08000000
#define  VMK_ISCSI_ADAPTER_SECONDARY_DNS 0x10000000

/* iscsi_trans max channels, target  mask  0-15: channel, 16-31: target */
#define ISCSI_MAX_CHANNEL_TARGET_MASK      0x0000FFFF

/* max iscsi parm string length */
#define ISCSI_MAX_PARM_STRING_LENGTH     4096

/* iscsi parm types */
typedef enum vmk_IscsiParmType {
   VMK_ISCSI_SESSION_PARM,
   VMK_ISCSI_CONN_PARM,
} vmk_IscsiParmType;

/* FC Port State */
typedef enum vmk_FcPortState {
   VMK_FC_PORTSTATE_UNKNOWN = 0x0,
   VMK_FC_PORTSTATE_NOTPRESENT,
   VMK_FC_PORTSTATE_ONLINE,
   VMK_FC_PORTSTATE_OFFLINE,		
   VMK_FC_PORTSTATE_BLOCKED,
   VMK_FC_PORTSTATE_BYPASSED,
   VMK_FC_PORTSTATE_DIAGNOSTICS,
   VMK_FC_PORTSTATE_LINKDOWN,
   VMK_FC_PORTSTATE_ERROR,
   VMK_FC_PORTSTATE_LOOPBACK,
   VMK_FC_PORTSTATE_DELETED,
} vmk_FcPortState;

/* FC Port Speed */
typedef enum  vmk_FcLinkSpeed {
   VMK_FC_SPEED_UNKNOWN = 0x0,
   VMK_FC_SPEED_1GBIT,
   VMK_FC_SPEED_2GBIT,
   VMK_FC_SPEED_4GBIT,
   VMK_FC_SPEED_8GBIT,
   VMK_FC_SPEED_10GBIT,
   VMK_FC_SPEED_16GBIT,
} vmk_FcLinkSpeed;

/* FC Port TYPE */
typedef enum vmk_FcPortType {
   VMK_FC_PORTTYPE_UNKNOWN = 0x0,
   VMK_FC_PORTTYPE_OTHER,
   VMK_FC_PORTTYPE_NOTPRESENT,
   VMK_FC_PORTTYPE_NPORT,		
   VMK_FC_PORTTYPE_NLPORT,	
   VMK_FC_PORTTYPE_LPORT,
   VMK_FC_PORTTYPE_PTP,
   VMK_FC_PORTTYPE_NPIV,
} vmk_FcPortType;

/* Adapter Status */
typedef enum vmk_AdapterStatus  {
   VMK_ADAPTER_STATUS_UNKNOWN = 0x0,
   VMK_ADAPTER_STATUS_ONLINE,
   VMK_ADAPTER_STATUS_OFFLINE,
} vmk_AdapterStatus;

/* Link Rescan Status */
typedef enum vmk_RescanLinkStatus  {
   VMK_RESCAN_LINK_UNSUPPORTED = 0x0,
   VMK_RESCAN_LINK_SUCCEEDED,
   VMK_RESCAN_LINK_FAILED,
} vmk_RescanLinkStatus;

/**
 * \brief fc target attributes
 */
typedef struct vmk_FcTargetAttrs {
   /** \brief target node WWN */
   vmk_uint64 nodeName;
   /** \brief target port WWN */
   vmk_uint64 portName;
   /** \brief target port id */
   vmk_uint32 portId;
} vmk_FcTargetAttrs;

/**
 * \brief fc adapter
 */
typedef struct vmk_FcAdapter
{
   /** \brief Get FC Node Name */
   vmk_uint64 (*getFcNodeName) (
      void *clientData);
   /** \brief Get FC Port Name */
   vmk_uint64 (*getFcPortName) (
      void *clientData);
   /** \brief Get FC Port Id */
   vmk_uint32 (*getFcPortId) (
      void *clientData);
   /** \brief Get FC Link Speed */
   vmk_FcLinkSpeed (*getFcLinkSpeed) (
      void *clientData);
   /** \brief Get FC Port Type */
   vmk_FcPortType (*getFcPortType) (
      void *clientData);
   /** \brief Get FC Port State */
   vmk_FcPortState (*getFcPortState) (
      void *clientData);
   /** \brief Get FC Target Attributes */
   VMK_ReturnStatus (*getFcTargetAttributes) (
      void *pSCSI_Adapter, vmk_FcTargetAttrs *fcAttrib,
      vmk_uint32 channelNo, vmk_uint32 targetNo);
   /** \brief Get FC Adapter Status */
   vmk_AdapterStatus (*getFcAdapterStatus) (
      void *clientData);
   /** \brief rescan FC Link Status */
   vmk_RescanLinkStatus (*rescanFcLink) (
      void *clientData);

   /**
    * \brief link timeout
    * This is the user provided Link Time out value.
    * If this is not set, default timeout set by
    * the underlying layer(vmklinux/driver) is used
    */
   vmk_uint64           linkTimeout;

   /**
    * \brief i/o timeout
    * This is the user provided I/O Time out value.
    * If this is not set, default timeout set by
    * the underlying layer(vmklinux/driver) is used
    */
   vmk_uint64           ioTimeout;
   /** \brief reserved */
   vmk_uint32           reserved1[4];
   /** \brief reserved */
   vmk_VirtAddr         reserved2[4];
} vmk_FcAdapter;

/**
 * \brief SAS target attributes
 */
typedef struct vmk_SasTargetAttrs {
   /** \brief SAS address */
   vmk_uint64 sasAddress;
} vmk_SasTargetAttrs;

/**
 * \brief SAS adapter
 */
typedef struct vmk_SasAdapter
{
   /** \brief get the adapter's SAS address */
   vmk_uint64 (*getInitiatorSasAddress) (
      void *clientData);
   /** \brief get the target's attributes */
   VMK_ReturnStatus (*getSasTargetAttributes) (
      void *pSCSI_Adapter, vmk_SasTargetAttrs *sasAttrib,
      vmk_uint32 channelNo, vmk_uint32 targetNo);
   /** \brief reserved */
   vmk_uint32           reserved1[4];
   /** \brief reserved */
   vmk_VirtAddr         reserved2[4];
} vmk_SasAdapter;

/**
 * \brief Block adapter
 */
typedef struct vmk_BlockAdapter {
   /** \brief device name */
   char         devName[VMKDRIVER_DEV_NAME_LENGTH];
   /** \brief controller instance number  */
   vmk_uint32   controllerInstance;
   /** \brief reserved */
   vmk_uint32   reserved1[4];
   /** \brief reserved */
   vmk_VirtAddr reserved2[4];
} vmk_BlockAdapter;

/**
 * \brief Identifier for generic SAN initiator/target
 */
typedef struct vmk_XsanID {
   /** \brief generic SAN ID lower 64bit */
   vmk_uint64 L;
   /** \brief generic SAN ID higher 64bit */
   vmk_uint64 H;
} vmk_XsanID;

/**
 * \brief generic SAN target attributes
 */
typedef struct vmk_XsanTargetAttrs {
   /** \brief generic SAN target ID */
   vmk_XsanID id;
} vmk_XsanTargetAttrs;

/**
 * \brief generic SAN adapter
 */
typedef struct vmk_XsanAdapter {
   /** \brief get the generic SAN initiator's ID */
   VMK_ReturnStatus (*getXsanInitiatorID)(void *clientData, vmk_XsanID *xsanID);
   /** \brief get the target's attributes on generic SAN */
   VMK_ReturnStatus (*getXsanTargetAttributes) (
      void *pSCSI_Adapter, vmk_XsanTargetAttrs *xsanAttrib,
      vmk_uint32 channelNo, vmk_uint32 targetNo);
} vmk_XsanAdapter;

/* Return types for iSCSI network param settings */
typedef enum vmk_IscsiAdapterParamStatus  {
   VMK_ISCSI_ADAPTER_PARAM_UNSUPPORTED = 0x0,
   VMK_ISCSI_ADAPTER_PARAM_CONFIG_SUCCEEDED,
   VMK_ISCSI_ADAPTER_PARAM_CONFIG_FAILED,
} vmk_IscsiAdapterParamStatus;

/* Return types for iSCSI Ethernet param settings */
typedef enum vmk_IscsiEthernetParamStatus  {
   VMK_ISCSI_ETHERNET_PARAM_UNSUPPORTED = 0x0,
   VMK_ISCSI_GET_ETHERNET_PARAM_SUCCESS,
   VMK_ISCSI_GET_ETHERNET_PARAM_FAILED,
} vmk_IscsiEthernetParamStatus;

/* Return types for iSCSI Session/Connection Param settings */
typedef enum vmk_IscsiParamStatus  {
   VMK_ISCSI_PARAM_CONFIG_UNSUPPORTED = 0x0,
   VMK_ISCSI_PARAM_CONFIG_SUCCEEDED,
   VMK_ISCSI_PARAM_CONFIG_FAILED,
} vmk_IscsiParamStatus;

/* Return types for iSCSI Login */
typedef enum vmk_IscsiSessionStatus  {
   VMK_ISCSI_SESSION_STATUS_UNKNOWN = 0x0,
   VMK_ISCSI_INVALID_PARAM,
   VMK_ISCSI_LOGIN_FAILED,
   VMK_ISCSI_LOGOUT_FAILED,
   VMK_ISCSI_SESSION_ESTABLISHED,
   VMK_ISCSI_LOGGED_OUT,
} vmk_IscsiSessionStatus;

/* Iscsi/Ethernet Link Speed */
typedef enum  vmk_IscsiLinkSpeed {
   VMK_ISCSI_SPEED_UNKNOWN = 0x0,
   VMK_ISCSI_SPEED_10MBIT,
   VMK_ISCSI_SPEED_100MBIT,
   VMK_ISCSI_SPEED_1GBIT,
   VMK_ISCSI_SPEED_10GBIT,
} vmk_IscsiLinkSpeed;

/* Iscsi Link State */
typedef enum  vmk_IscsiLinkState {
   VMK_ISCSI_LINK_STATE_UNKNOWN = 0x0,
   VMK_ISCSI_LINK_STATE_DOWN,
   VMK_ISCSI_LINK_STATE_UP,
} vmk_IscsiLinkState;

/* Iscsi Duplex Nature */
typedef enum  vmk_IscsiDuplex {
   VMK_ISCSI_DUPLEX_UNKNOWN = 0x0,
   VMK_ISCSI_HALF_DUPLEX,
   VMK_ISCSI_FULL_DUPLEX,
} vmk_IscsiDuplex;

/* Iscsi Service State */
typedef enum  vmk_IscsiServiceStatus {
   VMK_ISCSI_SERVICE_STATE_UNKNOWN = 0x0,
   VMK_ISCSI_SERVICE_STATE_RUNNING,
   VMK_ISCSI_SERVICE_STATE_STOPPED,
} vmk_IscsiServiceStatus;

/* Iscsi Service Action */
typedef enum  vmk_IscsiServiceAction {
   VMK_ISCSI_SERVICE_DISABLE = 0x0,
   VMK_ISCSI_SERVICE_ENABLE,
   VMK_ISCSI_SERVICE_GETSTATE,
} vmk_IscsiServiceAction;

/* Iscsi Service Type */
typedef enum  vmk_IscsiServiceType {
   VMK_ISCSI_SERVICE_DHCP = 0x0,
   VMK_ISCSI_SERVICE_SENDTARGET,
   VMK_ISCSI_SERVICE_STATICTARGET,
   VMK_ISCSI_SERVICE_PERSISTENTTARGET,
} vmk_IscsiServiceType;

/* Ethernet Attributes */
typedef struct vmk_IscsiEthernetAttrs {	
   vmk_uint8		macAddress[VMK_MAX_ETHERNET_MAC_LENGTH];
   vmk_IscsiDuplex 	duplex;
   vmk_IscsiLinkState  	link;
   vmk_IscsiLinkSpeed 	speed;
} vmk_IscsiEthernetAttrs;

/* Send Target parameters */
typedef struct vmk_IscsiSendTargetAttrs {	
   char		sendTargetIp[VMK_MAX_IP_STRING_LENGTH];
   vmk_uint32	sendTargetPort;
} vmk_IscsiSendTargetAttrs;

/* Iscsi Target Information */
typedef struct vmk_IscsiTargetAttr {	
   char		targetName[VMK_MAX_ISCSI_IQN_LENGTH];
   char		targetIp[VMK_MAX_IP_STRING_LENGTH];
   vmk_uint32	targetPort;
} vmk_IscsiTargetAttr;

/* Iscsi Session Information */
typedef struct vmk_IscsiSessionParams {	
   char		adapterIQN[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		dataPDUInOrder[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		dataSequenceInOrder[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		initialR2T[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		immediateData[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		erl[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		time2Wait[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		time2Retain[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		maxConnections[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		maxR2T[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		fbl[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		mbl[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		hdrDigest[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		dataDigest[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		receiveDataSegment[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		iChapname[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		tChapname[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		iChapSecret[VMK_MAX_ISCSI_PARAM_LENGTH];
   char		tChapSecret[VMK_MAX_ISCSI_PARAM_LENGTH];
} vmk_IscsiSessionParams;

/* Iscsi Session Information */
typedef struct vmk_IscsiSession {
   struct vmk_IscsiSession       *next;

   vmk_uint32                 sessionId;
   vmk_uint32                 channelId;
   vmk_uint32                 targetId;

   vmk_IscsiSessionStatus status;
   vmk_IscsiTargetAttr    targetInfo;

   /* Per Session Param's are not enabled now, So a future place holder */
   vmk_IscsiSessionParams   *param;
} vmk_IscsiSession;

/* Send Target - has list of sendTarget structures */
typedef struct vmk_SendTarget
{
   struct vmk_SendTarget     *next;
   vmk_IscsiSendTargetAttrs  attrs;
} vmk_SendTarget;

/* Send Target - has list of sendTarget structures */
typedef struct vmk_StaticTarget
{
   struct vmk_StaticTarget        *next;
   struct vmk_IscsiTargetAttr     target;
} vmk_StaticTarget;

/* iSCSI Adapter Structure */
typedef struct vmk_IscsiAdapter
{
   /* Device Capability Information based on vmodl */ 	

   /* Authentication types */	
   vmk_Bool 		    chapAuthSettable;
   vmk_Bool 		    chapAuthEnabled;

   /* Discovery types */	
   vmk_Bool 		    sendTargetDiscoverySettable;
   vmk_Bool 		    sendTargetsDiscoveryEnabled;

   vmk_Bool 		    staticTargetDiscoverySettable;
   vmk_Bool 		    staticTargetDiscoveryEnabled;

   /* Network Settings */	
   vmk_Bool 		    dhcpSettable;
   vmk_Bool 		    dhcpEnabled;

   vmk_Bool 		    ipAddressSettable;
   vmk_Bool 		    primaryDnsServerSettable;
   vmk_Bool 		    secondaryDnsServerSettable;
   vmk_Bool 		    defaultGatewaySettable;
   vmk_Bool 		    subnetMaskSettable;

   /* Indicate Software/Hardware initiator */
   vmk_Bool 		    softwareInitiator;
   vmk_Bool 		    usingToe;

   vmk_uint8		    totalSendTargets; /* Number of Send Target Nodes */
   vmk_SendTarget	    *SendTargets; /* Queue of Send Targets */

   vmk_uint8		    totalStaticTargets; /* Number of Static Target Nodes */
   vmk_StaticTarget	    *StaticTargets; /* Queue of Static Targets */

   vmk_uint8		    totalSessions; /* Number of Sessions */
   vmk_IscsiSession	    *Sessions; /* Queue of iscsi sessions */

   vmk_IscsiSessionParams   defaultParams;
   vmk_uint32		    enabledParams; /* Flag to indicate the param's enabled */

   vmk_uint32           reserved1[4];
   vmk_VirtAddr         reserved2[4];
   void                 *transport; /* For use by Annex iSCSI */
   vmk_RescanLinkStatus (*rescanIscsiLink)
                                (void *clientData);

   /* Configure Adapter Parameters */
   vmk_IscsiAdapterParamStatus  (*getAdapterParams)
        (void *clientData, vmk_uint32 param, char *pBuf, vmk_uint32 bufLength);
   vmk_IscsiAdapterParamStatus  (*setAdapterParams)
        (void *clientData, vmk_uint32 param, char *pBuf, vmk_uint32 bufLength);

   /* Get Ethernet Parameters */
   vmk_IscsiEthernetParamStatus  (*getIscsiEthernetSetting)
        (void *clientData, struct vmk_IscsiEthernetAttrs *ethernetAttrib);

   /* Configure Iscsi Session/Connection Parameters */
   vmk_IscsiParamStatus  (*getIscsiAttributes)
        (void *clientData, vmk_uint32 sessionId, vmk_uint32 param, char *pBuf, vmk_uint32 bufLength);
   vmk_IscsiParamStatus  (*setIscsiAttributes)
        (void *clientData, vmk_uint32 sessionId, vmk_uint32 param, char *pBuf, vmk_uint32 bufLength);

   /* Supported Discovery Methods */
   vmk_uint32  (*doSendTargetDiscovery)
        (void *clientData, struct vmk_IscsiSendTargetAttrs *pSendTargets );
   vmk_uint32  (*doStaticTargetDiscovery)
        (void *clientData, struct vmk_IscsiTargetAttr *pStaticTargets );

   /* Start/Stop Session */
   vmk_IscsiSessionStatus   (*createIscsiSession)
        (void *clientData, vmk_IscsiSession *pIscsiSession);
   vmk_IscsiSessionStatus   (*destroyIscsiSession)
        (void *clientData, vmk_IscsiSession *pIscsiSession);

   /* Get Service Status */
   vmk_IscsiServiceStatus    (*getServiceStatus)
        (void *clientData, vmk_IscsiServiceType type, vmk_IscsiServiceAction action);

   /* Get iSCSI Target Uid */
   VMK_ReturnStatus         (*getIscsiTargetUid)
        (void *pSCSI_Adapter, char *pTargetUid, vmk_uint32 uidLength,
         vmk_uint32 channelNo, vmk_uint32 targetNo);

} vmk_IscsiAdapter;

/**
 * \brief Managed storage adapter types.
 */
typedef enum vmk_StorageTransport {
   VMK_STORAGE_ADAPTER_TRANSPORT_UNKNOWN = 0x0,
   VMK_STORAGE_ADAPTER_BLOCK,
   VMK_STORAGE_ADAPTER_FC,
   VMK_STORAGE_ADAPTER_ISCSI,
   VMK_STORAGE_ADAPTER_IDE,
   VMK_STORAGE_ADAPTER_ISCSI_VENDOR_SUPPLIED_IMA,
   VMK_STORAGE_ADAPTER_SAS,
   VMK_STORAGE_ADAPTER_SATA,
   VMK_STORAGE_ADAPTER_USB,
   VMK_STORAGE_ADAPTER_PSCSI,
   /**
    * VMK_STORAGE_ADAPTER_XSAN generically represents any type of SAN
    * transport not explicitly encoded elsewhere in this list. It is
    * intended mainly to support novel SAN transports that aren't yet
    * fully integrated and supported in the way other transports are.
    * This type of SAN adapters generally masquerade as
    * locally-attached storage, except that:
    * - They respond to SCSI "Inquiry" commands, yielding unique target IDs.
    * - RDMs are allowed.
    * - Periodic rescanning for new LUNs is performed.
    */
   VMK_STORAGE_ADAPTER_XSAN,
} vmk_StorageTransport;

/**
 * \brief Transport specific info required for management.
 */
typedef struct vmk_SCSITransportMgmt {
   /** \brief Storage transport type. */
   vmk_StorageTransport transport;
   /** \brief Reserved */
   vmk_uint8    reserved[4];
   /** \brief Driver transport specific data */
   void         *transportData;
   /** \brief Transport-specifics */
   union {
      /** \brief Block transport specific data. */
      vmk_BlockAdapter *block;
      /** \brief Fibre Channel transport specific data. */
      vmk_FcAdapter    *fc;
      /** \brief ISCSI transport specific data. */
      vmk_IscsiAdapter *iscsi;
      /** \brief SAS transport specific data. */
      vmk_SasAdapter   *sas;
      /** \brief Generic SAN transport specific data. */
      vmk_XsanAdapter  *xsan;
   } t;
} vmk_SCSITransportMgmt;

#endif /* _VMKAPI_SCSI_MGMT_TYPES_H_ */
/** @} */
/** @} */
