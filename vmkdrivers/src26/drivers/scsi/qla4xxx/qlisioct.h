/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

/*
 * File Name: qlisioct.h
 *
 */
#ifndef _QLISIOCT_H
#define _QLISIOCT_H

/*
 * NOTE: the following version defines must be updated each time the
 *	 changes made may affect the backward compatibility of the
 *	 input/output relations of the IOCTL functions.
 */
#define EXT_VERSION				6

/*
 * OS independent General definitions
 */
#define EXT_DEF_SIGNATURE_SIZE			8
#define EXT_DEF_SERIAL_NUM_SIZE			4
#define EXT_DEF_MAX_STR_SIZE			128

#define EXT_DEF_ADDR_MODE_32			1
#define EXT_DEF_ADDR_MODE_64			2

/*
 * ****************************************************************************
 * OS type definitions
 * ****************************************************************************
 */
#ifdef _MSC_VER					/* NT */

#include "qlisiont.h"

#elif defined(linux)				/* Linux */

#include "qlisioln.h"

#elif defined(sun) || defined(__sun)		/* Solaris */

#include "qlisioso.h"

#endif

/*
 * ****************************************************************************
 * OS dependent General configuration defines
 * ****************************************************************************
 */
#define EXT_DEF_MAX_HBA				EXT_DEF_MAX_HBA_OS
#define EXT_DEF_MAX_BUS				EXT_DEF_MAX_BUS_OS
#define EXT_DEF_MAX_TARGET			EXT_DEF_MAX_TARGET_OS
#define EXT_DEF_MAX_LUN				EXT_DEF_MAX_LUN_OS

/*
 * Addressing mode used by the user application
 */
#define EXT_ADDR_MODE				EXT_ADDR_MODE_OS

/*
 * Command Codes definitions
 */
#define EXT_CC_QUERY				EXT_CC_QUERY_OS
#define EXT_CC_REG_AEN				EXT_CC_REG_AEN_OS
#define EXT_CC_GET_AEN				EXT_CC_GET_AEN_OS
#define EXT_CC_GET_DATA				EXT_CC_GET_DATA_OS
#define EXT_CC_SET_DATA				EXT_CC_SET_DATA_OS
#define EXT_CC_SEND_SCSI_PASSTHRU		EXT_CC_SEND_SCSI_PASSTHRU_OS
#define EXT_CC_SEND_ISCSI_PASSTHRU		EXT_CC_SEND_ISCSI_PASSTHRU_OS
#define EXT_CC_DISABLE_ACB			EXT_CC_DISABLE_ACB_OS
#define EXT_CC_SEND_ROUTER_SOL			EXT_CC_SEND_ROUTER_SOL_OS

/*
 * ****************************************************************************
 * EXT_IOCTL_ISCSI
 * ****************************************************************************
 */
/*
 * EXT_IOCTL_ISCSI SubCode definition.
 * These macros are being used for setting SubCode field in EXT_IOCTL_ISCSI
 * structure.
 */

/*
 * Sub codes for Query.
 * Uses in combination with EXT_QUERY as the ioctl code.
 */
#define EXT_SC_QUERY_HBA_ISCSI_NODE		1
#define EXT_SC_QUERY_HBA_ISCSI_PORTAL		2
#define EXT_SC_QUERY_DISC_ISCSI_NODE		3
#define EXT_SC_QUERY_DISC_ISCSI_PORTAL		4
#define EXT_SC_QUERY_DISC_LUN                   5
#define EXT_SC_QUERY_DRIVER			6
#define EXT_SC_QUERY_FW				7
#define EXT_SC_QUERY_CHIP			8
#define EXT_SC_QUERY_IP_STATE			9
#define EXT_SC_QUERY_DEVICE_CURRENT_IP		10

/*
 * Sub codes for Get Data.
 * Use in combination with EXT_GET_DATA as the ioctl code
 */
#define EXT_SC_GET_STATISTICS_GEN		1
#define EXT_SC_GET_STATISTICS_ISCSI		2
#define EXT_SC_GET_DEVICE_ENTRY_ISCSI		3
#define EXT_SC_GET_INIT_FW_ISCSI		4
#define EXT_SC_GET_INIT_FW_DEFAULTS_ISCSI	5
#define EXT_SC_GET_DEVICE_ENTRY_DEFAULTS_ISCSI	6
#define EXT_SC_GET_ISNS_SERVER			7
#define EXT_SC_GET_ISNS_DISCOVERED_TARGETS	8
#define EXT_SC_GET_ACB				9
#define EXT_SC_GET_NEIGHBOR_CACHE		10
#define EXT_SC_GET_DESTINATION_CACHE		11
#define EXT_SC_GET_DEFAULT_ROUTER_LIST		12
#define EXT_SC_GET_LOCAL_PREFIX_LIST		13
#define EXT_SC_GET_STATISTICS_ISCSI_BLOCK	14

/*
 * Sub codes for Set Data.
 * Use in combination with EXT_SET_DATA as the ioctl code
 */
#define EXT_SC_RST_STATISTICS_GEN		1
#define EXT_SC_RST_STATISTICS_ISCSI		2
#define EXT_SC_SET_DEVICE_ENTRY_ISCSI		3
#define EXT_SC_SET_INIT_FW_ISCSI		4
#define EXT_SC_SET_ISNS_SERVER			5
#define EXT_SC_SET_ACB				6

/*
 * Status.  These macros are being used for setting Status field in
 * EXT_IOCTL_ISCSI structure.
 */
#define EXT_STATUS_OK				0
#define EXT_STATUS_ERR				1
#define EXT_STATUS_BUSY				2
#define EXT_STATUS_PENDING			3
#define EXT_STATUS_SUSPENDED			4
#define EXT_STATUS_RETRY_PENDING		5
#define EXT_STATUS_INVALID_PARAM		6
#define EXT_STATUS_DATA_OVERRUN			7
#define EXT_STATUS_DATA_UNDERRUN		8
#define EXT_STATUS_DEV_NOT_FOUND		9
#define EXT_STATUS_COPY_ERR			10
#define EXT_STATUS_MAILBOX			11
#define EXT_STATUS_UNSUPPORTED_SUBCODE		12
#define EXT_STATUS_UNSUPPORTED_VERSION		13
#define EXT_STATUS_MS_NO_RESPONSE		14
#define EXT_STATUS_SCSI_STATUS			15
#define EXT_STATUS_BUFFER_TOO_SMALL		16
#define EXT_STATUS_NO_MEMORY			17
#define EXT_STATUS_UNKNOWN			18
#define EXT_STATUS_UNKNOWN_DSTATUS		19
#define EXT_STATUS_INVALID_REQUEST		20
#define EXT_STATUS_DEVICE_NOT_READY		21
#define EXT_STATUS_DEVICE_OFFLINE		22
#define EXT_STATUS_HBA_NOT_READY		23
#define EXT_STATUS_HBA_QUEUE_FULL		24

/*
 * Detail Status contains the SCSI bus status codes.
 */
#define EXT_DSTATUS_GOOD			0x00
#define EXT_DSTATUS_CHECK_CONDITION		0x02
#define EXT_DSTATUS_CONDITION_MET		0x04
#define EXT_DSTATUS_BUSY			0x08
#define EXT_DSTATUS_INTERMEDIATE		0x10
#define EXT_DSTATUS_INTERMEDIATE_COND_MET	0x14
#define EXT_DSTATUS_RESERVATION_CONFLICT	0x18
#define EXT_DSTATUS_COMMAND_TERMINATED		0x22
#define EXT_DSTATUS_QUEUE_FULL			0x28

/*
 * Detail Status contains one of the following codes
 * when Status = EXT_STATUS_INVALID_PARAM or
 *	       = EXT_STATUS_DEV_NOT_FOUND
 */
#define EXT_DSTATUS_NOADNL_INFO			0x00
#define EXT_DSTATUS_HBA_INST			0x01
#define EXT_DSTATUS_TARGET			0x02
#define EXT_DSTATUS_LUN				0x03
#define EXT_DSTATUS_REQUEST_LEN			0x04
#define EXT_DSTATUS_PATH_INDEX			0x05

/*
 * FLASH error status
*/
#define EXT_FLASH_NO_INFO			0x00
#define EXT_FLASH_NO_MEMORY			0x0a
#define EXT_FLASH_FW_IMAGE_INVALID		0x0b
#define EXT_FLASH_NO_BKUP_FW_IMAGE		0x0c
#define EXT_FLASH_ERROR_ACCESSING_FLASH		0x0d

/*
 * Defines for VendorSpecificStatus
 */
#define VENDOR_SPECIFIC_STATUS_MB_STATUS_INDEX		0 /* [0-4]  mbSts */
#define VENDOR_SPECIFIC_STATUS_MB_COMMAND_INDEX		5 /* [5-10] mbCmd */
#define VENDOR_SPECIFIC_STATUS_IOSB_COMPLETION_INDEX	0
#define VENDOR_SPECIFIC_STATUS_SCSI_STATUS_INDEX	1


typedef struct _EXT_IOCTL_ISCSI {
	UINT8	Signature[EXT_DEF_SIGNATURE_SIZE];	/* 8   */
	UINT16	AddrMode;				/* 2   */
	UINT16	Version;				/* 2   */
	UINT16	SubCode;				/* 2   */
	UINT16	Instance;				/* 2   */
	UINT32	Status;					/* 4   */
	UINT32	DetailStatus;				/* 4   */
	UINT32	Reserved1;				/* 4   */
	UINT32	RequestLen;				/* 4   */
	UINT32	ResponseLen;				/* 4   */
	UINT64	RequestAdr;				/* 8   */
	UINT64	ResponseAdr;				/* 8   */
	UINT16	HbaSelect;				/* 2   */
	UINT32	VendorSpecificStatus[11];		/* 44  */
	UINT64	Signature2;				/* 8   */
} __attribute__((packed)) EXT_IOCTL_ISCSI, *PEXT_IOCTL_ISCSI;	/* 106 */

/*
 * ****************************************************************************
 * EXT_ISCSI_DEVICE
 * ****************************************************************************
 */
/* Device Type */
#define EXT_DEF_ISCSI_REMOTE			0x02
#define EXT_DEF_ISCSI_LOCAL			0x01

#define EXT_ISCSI_ENABLE_DHCP			0x01

#define EXT_DEF_ISCSI_TADDR_SIZE		32

typedef struct _EXT_ISCSI_DEVICE {
	UINT16	DeviceType;				/* 2   */
	UINT16	ExeThrottle;				/* 2   */
	UINT16	InitMarkerlessInt;			/* 2   */
	UINT8	RetryCount;				/* 1   */
	UINT8	RetryDelay;				/* 1   */
	UINT16	iSCSIOptions;				/* 2   */
	UINT16	TCPOptions;				/* 2   */
	UINT16	IPOptions;				/* 2   */
	UINT16	MaxPDUSize;				/* 2   */
	UINT16	FirstBurstSize;				/* 2   */
	UINT16	LogoutMinTime;				/* 2   */
	UINT16	LogoutMaxTime;				/* 2   */
	UINT16	MaxOutstandingR2T;			/* 2   */
	UINT16	KeepAliveTimeout;			/* 2   */
	UINT16	PortNumber;				/* 2   */
	UINT16	MaxBurstSize;				/* 2   */
	UINT16	TaskMgmtTimeout;			/* 2   */
	UINT8	TargetAddr[EXT_DEF_ISCSI_TADDR_SIZE];	/* 32  */
} EXT_ISCSI_DEVICE, *PEXT_ISCSI_DEVICE;		/* 64  */

/*
 * ****************************************************************************
 * EXT_ISCSI_IP_ADDR
 * ****************************************************************************
 */
#define EXT_DEF_IP_ADDR_SIZE			16
#define EXT_DEF_TYPE_ISCSI_IP			0
#define EXT_DEF_TYPE_ISCSI_IPV6			1

typedef struct _EXT_ISCSI_IP_ADDR {
	UINT8	IPAddress[EXT_DEF_IP_ADDR_SIZE];	/* 16  */
	UINT16	Type;					/* 2   */
	UINT16	Reserved;				/* 2   */
} EXT_ISCSI_IP_ADDR, *PEXT_ISCSI_IP_ADDR;		/* 20  */

/*
 * ****************************************************************************
 * EXT_NODE_INFO_ISCSI
 * ****************************************************************************
 */
#define EXT_DEF_ISCSI_NAME_LEN			256
#define EXT_DEF_ISCSI_ALIAS_LEN			32

typedef struct _EXT_NODE_INFO_ISCSI {
	EXT_ISCSI_IP_ADDR IPAddr;			/* 20  */
	UINT8	iSCSIName[EXT_DEF_ISCSI_NAME_LEN];	/* 256 */
	UINT8	Alias[EXT_DEF_ISCSI_ALIAS_LEN];		/* 32  */
	UINT16	PortalCount;				/* 2   */
	UINT8	Reserved[10];				/* 10  */
} EXT_NODE_INFO_ISCSI, *PEXT_NODE_INFO_ISCSI;		/* 320 */

/*
 * ****************************************************************************
 * EXT_SCSI_ADDR_ISCSI
 * ****************************************************************************
 */
typedef struct _EXT_SCSI_ADDR_ISCSI {
	UINT16	Bus;					/* 2   */
	UINT16	Target;					/* 2   */
	UINT16	Lun;					/* 2   */
	UINT16	Padding[5];				/* 10  */
} EXT_SCSI_ADDR_ISCSI, *PEXT_SCSI_ADDR_ISCSI;		/* 16  */

/*
 * ****************************************************************************
 * EXT_REG_AEN_ISCSI
 * ****************************************************************************
 */
#define EXT_DEF_ENABLE_AENS		0x00000000
#define EXT_DEF_ENABLE_NO_AENS		0xFFFFFFFF

typedef struct _EXT_REG_AEN_ISCSI {
	UINT32	Enable;					/* 4   */
	UINT32	Reserved[3];				/* 12  */
} EXT_REG_AEN_ISCSI, *PEXT_REG_AEN_ISCSI;		/* 16  */

/*
 * ****************************************************************************
 * EXT_ASYNC_EVENT
 * ****************************************************************************
 */

/* Required # of entries in the queue buffer allocated. */
#define EXT_DEF_MAX_AEN_QUEUE			EXT_DEF_MAX_AEN_QUEUE_OS
#define EXT_DEF_MAX_AEN_PAYLOAD			7

typedef struct _EXT_ASYNC_EVENT {
	UINT32	AsyncEventCode;				/* 4   */
	UINT32	Payload[EXT_DEF_MAX_AEN_PAYLOAD];	/* 28  */
} EXT_ASYNC_EVENT, *PEXT_ASYNC_EVENT;			/* 32  */

/*
 * ****************************************************************************
 * EXT_CHIP_INFO
 * ****************************************************************************
 */
typedef struct _EXT_CHIP_INFO {
	UINT16	VendorId;				/* 2   */
	UINT16	DeviceId;				/* 2   */
	UINT16	SubVendorId;				/* 2   */
	UINT16	SubSystemId;				/* 2   */
	UINT16	BoardID;				/* 2   */
	UINT16	Reserved[35];				/* 70  */
} EXT_CHIP_INFO, *PEXT_CHIP_INFO;			/* 80  */

/*
 * ****************************************************************************
 * EXT_DEVICE_ENTRY_ISCSI
 * ****************************************************************************
 */
/* Options */
#define EXT_DEF_ISCSI_GRANT_ACCESS		0x04
#define EXT_DEF_ISCSI_TARGET_DEVICE		0x02
#define EXT_DEF_ISCSI_INITIATOR_DEVICE		0x01

/* Control */
#define EXT_DEF_SESS_RECVRY_IN_PROCESS		0x10
#define EXT_DEF_ISCSI_TRANSMITTING		0x08
#define EXT_DEF_ISCSI_TX_LINKED			0x04
#define EXT_DEF_ISCSI_QUEUE_ABORTED		0x02
#define EXT_DEF_ISCSI_TX_LOGGED_IN		0x01

/* DeviceState */
#define EXT_DEF_DEV_STATE_UNASSIGNED		0x00
#define EXT_DEF_DEV_STATE_NO_CONNECTION_ACTIVE	0x01
#define EXT_DEF_DEV_STATE_DISCOVERY		0x02
#define EXT_DEF_DEV_STATE_NO_SESSION_ACTIVE	0x03
#define EXT_DEF_DEV_STATE_SESSION_ACTIVE	0x04
#define EXT_DEF_DEV_STATE_LOGGING_OUT		0x05
#define EXT_DEF_DEV_STATE_SESSION_FAILED	0x06
#define EXT_DEF_DEV_STATE_OPENING		0x07

#define EXT_DEF_ISCSI_ISID_SIZE			6
#define EXT_DEF_ISCSI_USER_ID_SIZE		32
#define EXT_DEF_ISCSI_PASSWORD_SIZE		32

typedef struct _EXT_DEVICE_ENTRY_ISCSI {
	UINT8	Options;				/* 1   */
	UINT8	Control;				/* 1   */
	UINT8	InitiatorSessID[EXT_DEF_ISCSI_ISID_SIZE];	/* 6   */
	UINT16	TargetSessID;				/* 2   */
	UINT32	ReservedFlags;				/* 4   */
	UINT8	UserID[EXT_DEF_ISCSI_USER_ID_SIZE];	/* 32  */
	UINT8	Password[EXT_DEF_ISCSI_PASSWORD_SIZE];	/* 32  */
	EXT_ISCSI_DEVICE	DeviceInfo;		/* 64  */
	EXT_NODE_INFO_ISCSI	EntryInfo;		/* 320 */
	UINT16	ExeCount;				/* 2   */
	UINT32	NumValid;				/* 4   */
	UINT32	NextValid;				/* 4   */
	UINT32	DeviceState;				/* 4   */
	UINT16	DDBLink;				/* 2   */
	UINT16	Reserved[17];				/* 34  */
} EXT_DEVICE_ENTRY_ISCSI, *PEXT_DEVICE_ENTRY_ISCSI;	/* 512 */

/*
 * ****************************************************************************
 * EXT_DEST_ADDR_ISCSI
 * ****************************************************************************
 */
typedef struct _EXT_DEST_ADDR_ISCSI {
	UINT8	iSCSINameStr[EXT_DEF_ISCSI_NAME_LEN];	/* 256 */
	UINT16	SessionID;				/* 2   */
	UINT16	ConnectionID;				/* 2   */
	UINT16	PortNumber;				/* 2   */
	UINT16	Reserved[3];				/* 6   */
} EXT_DEST_ADDR_ISCSI, *PEXT_DEST_ADDR_ISCSI;		/* 268 */

/*
 * ****************************************************************************
 * EXT_DISC_ISCSI_PORTAL
 * ****************************************************************************
 */
typedef struct _EXT_DISC_ISCSI_PORTAL {
	EXT_ISCSI_IP_ADDR	IPAddr;			/* 20  */
	UINT16	NodeCount;				/* 2   */
	UINT8	HostName[EXT_DEF_MAX_STR_SIZE];		/* 128 */
	UINT16	PortNumber;				/* 2   */
	UINT16	Reserved;				/* 2   */
} EXT_DISC_ISCSI_PORTAL, *PEXT_DISC_ISCSI_PORTAL;	/* 154 */

/*
 * ****************************************************************************
 * EXT_DISC_ISCSI_NODE
 * ****************************************************************************
 */
typedef struct _EXT_DISC_ISCSI_NODE {
	UINT16	SessionID;				/* 2   */
	UINT16	ConnectionID;				/* 2   */
	UINT16	PortalGroupID;				/* 2   */
	EXT_NODE_INFO_ISCSI	NodeInfo;		/* 320 */
	EXT_SCSI_ADDR_ISCSI	ScsiAddr;		/* 16  */
	UINT16	Reserved;				/* 2   */
} EXT_DISC_ISCSI_NODE, *PEXT_DISC_ISCSI_NODE;		/* 344 */

/*
 * ****************************************************************************
 * EXT_DNS
 * ****************************************************************************
 */
typedef struct _EXT_DNS {
	EXT_ISCSI_IP_ADDR	IPAddr;			/* 20  */
	UINT8	Reserved[132];				/* 132 */
} EXT_DNS, *PEXT_DNS;					/* 152 */

/*
 * ****************************************************************************
 * EXT_DRIVER_INFO
 * ****************************************************************************
 */
typedef struct _EXT_DRIVER_INFO {
	UINT8	Version[EXT_DEF_MAX_STR_SIZE];		/* 128 */
	UINT16	NumOfBus;				/* 2   */
	UINT16	TargetsPerBus;				/* 2   */
	UINT16	LunPerTarget;				/* 2   */
	UINT16	LunPerTargetOS;				/* 2   */
	UINT32	MaxTransferLen;				/* 4   */
	UINT32	MaxDataSegments;			/* 4   */
	UINT16	DmaBitAddresses;			/* 2   */
	UINT16	IoMapType;				/* 2   */
	UINT32	Attrib;					/* 4   */
	UINT32	InternalFlags[4];			/* 16  */
	UINT32	Reserved[8];				/* 32  */
} EXT_DRIVER_INFO, *PEXT_DRIVER_INFO;			/* 200 */

/*
 * ****************************************************************************
 * EXT_FW_INFO
 * ****************************************************************************
 */
typedef struct _EXT_FW_INFO {
	UINT8	Version[EXT_DEF_MAX_STR_SIZE];		/* 128 */
	UINT32	Attrib;					/* 4   */
	UINT32	Reserved[8];				/* 32  */
} EXT_FW_INFO, *PEXT_FW_INFO;				/* 164 */

/*
 * ****************************************************************************
 * EXT_HBA_ISCSI_NODE
 * ****************************************************************************
 */
typedef struct _EXT_HBA_ISCSI_NODE {
	UINT8	DeviceName[EXT_DEF_MAX_STR_SIZE];	/* 128 */
	UINT16	PortNumber;				/* 2   */
	EXT_NODE_INFO_ISCSI	NodeInfo;		/* 320 */
	UINT16	Reserved;				/* 2   */
} EXT_HBA_ISCSI_NODE, *PEXT_HBA_ISCSI_NODE;		/* 452 */

/*
 * ****************************************************************************
 * EXT_HBA_ISCSI_PORTAL
 * ****************************************************************************
 */
#define EXT_DEF_MAC_ADDR_SIZE			6

/* State */
#define EXT_DEF_CARD_STATE_READY		1
#define EXT_DEF_CARD_STATE_CONFIG_WAIT		2
#define EXT_DEF_CARD_STATE_LOGIN		3
#define EXT_DEF_CARD_STATE_ERROR		4

/* Type */
#define EXT_DEF_TYPE_COPPER			1
#define EXT_DEF_TYPE_OPTICAL			2

#define EXT_DEF_SERIAL_NUM_SIZE			4

typedef struct _EXT_HBA_ISCSI_PORTAL {
	EXT_ISCSI_IP_ADDR IPAddr;			/* 20  */
	UINT8	MacAddr[EXT_DEF_MAC_ADDR_SIZE];		/* 6   */
	UINT8	Padding[2];				/* 2   */
	UINT32	SerialNum;				/* 4   */
	UINT8	Manufacturer[EXT_DEF_MAX_STR_SIZE];	/* 128 */
	UINT8	Model[EXT_DEF_MAX_STR_SIZE];		/* 128 */
	UINT8	DriverVersion[EXT_DEF_MAX_STR_SIZE];	/* 128 */
	UINT8	FWVersion[EXT_DEF_MAX_STR_SIZE];	/* 128 */
	UINT8	OptRomVersion[EXT_DEF_MAX_STR_SIZE];	/* 128 */
	UINT16	State;					/* 2   */
	UINT16	Type;					/* 2   */
	UINT32	DriverAttr;				/* 4   */
	UINT32	FWAttr;					/* 4   */
	UINT16	DiscTargetCount;			/* 2   */
	UINT32	Reserved;				/* 4   */
} EXT_HBA_ISCSI_PORTAL, *PEXT_HBA_ISCSI_PORTAL;	/* 686 */

/*
 * ****************************************************************************
 * EXT_HBA_PORT_STAT_GEN
 * ****************************************************************************
 */
typedef struct _EXT_HBA_PORT_STAT_GEN {
	UINT64	HBAPortErrorCount;			/* 8   */
	UINT64	DevicePortErrorCount;			/* 8   */
	UINT64	IoCount;				/* 8   */
	UINT64	MBytesCount;				/* 8   */
	UINT64	InterruptCount;				/* 8   */
	UINT64	LinkFailureCount;			/* 8   */
	UINT64	InvalidCrcCount;			/* 8   */
	UINT32	Reserved[2];				/* 8   */
} EXT_HBA_PORT_STAT_GEN, *PEXT_HBA_PORT_STAT_GEN;	/* 64  */

/*
 * ****************************************************************************
 * EXT_HBA_PORT_STAT_ISCSI
 * ****************************************************************************
 */
typedef struct _EXT_HBA_PORT_STAT_ISCSI {
	UINT64	MACTxFramesCount;			/* 8   */
	UINT64	MACTxBytesCount;			/* 8   */
	UINT64	MACRxFramesCount;			/* 8   */
	UINT64	MACRxBytesCount;			/* 8   */
	UINT64	MACCRCErrorCount;			/* 8   */
	UINT64	MACEncodingErrorCount;			/* 8   */
	UINT64	IPTxPacketsCount;			/* 8   */
	UINT64	IPTxBytesCount;				/* 8   */
	UINT64	IPTxFragmentsCount;			/* 8   */
	UINT64	IPRxPacketsCount;			/* 8   */
	UINT64	IPRxBytesCount;				/* 8   */
	UINT64	IPRxFragmentsCount;			/* 8   */
	UINT64	IPDatagramReassemblyCount;		/* 8   */
	UINT64	IPv6RxPacketsCount;			/* 8   */
	UINT64	IPRxPacketErrorCount;			/* 8   */
	UINT64	IPReassemblyErrorCount;			/* 8   */
	UINT64	TCPTxSegmentsCount;			/* 8   */
	UINT64	TCPTxBytesCount;			/* 8   */
	UINT64	TCPRxSegmentsCount;			/* 8   */
	UINT64	TCPRxBytesCount;			/* 8   */
	UINT64	TCPTimerExpiredCount;			/* 8   */
	UINT64	TCPRxACKCount;				/* 8   */
	UINT64	TCPTxACKCount;				/* 8   */
	UINT64	TCPRxErrorSegmentCount;			/* 8   */
	UINT64	TCPWindowProbeUpdateCount;		/* 8   */
	UINT64	iSCSITxPDUCount;			/* 8   */
	UINT64	iSCSITxBytesCount;			/* 8   */
	UINT64	iSCSIRxPDUCount;			/* 8   */
	UINT64	iSCSIRxBytesCount;			/* 8   */
	UINT64	iSCSICompleteIOsCount;			/* 8   */
	UINT64	iSCSIUnexpectedIORxCount;		/* 8   */
	UINT64	iSCSIFormatErrorCount;			/* 8   */
	UINT64	iSCSIHeaderDigestCount;			/* 8   */
	UINT64	iSCSIDataDigestErrorCount;		/* 8   */
	UINT64	iSCSISeqErrorCount;			/* 8   */
	UINT32	Reserved[2];				/* 8   */
} EXT_HBA_PORT_STAT_ISCSI, *PEXT_HBA_PORT_STAT_ISCSI;	/* 272 */

/*
 * ****************************************************************************
 * EXT_HBA_PORT_STAT_ISCSI_BLOCK
 * ****************************************************************************
 */
typedef struct _EXT_HBA_PORT_STAT_ISCSI_BLOCK {
	UINT8	DataBlock[4096];				
} EXT_HBA_PORT_STAT_ISCSI_BLOCK, *PEXT_HBA_PORT_STAT_ISCSI_BLOCK;  /* 4096 */

/*
 * ****************************************************************************
 * EXT_INIT_FW_ISCSI
 * ****************************************************************************
 */
#define EXT_DEF_FW_MARKER_DISABLE		0x0400
#define EXT_DEF_FW_ACCESS_CONTROL_ENABLE	0x0080
#define EXT_DEF_FW_SESSION_MODE_ENABLE		0x0040
#define EXT_DEF_FW_INITIATOR_MODE_ENABLE	0x0020
#define EXT_DEF_FW_TARGET_MODE_ENABLE		0x0010
#define EXT_DEF_FW_FAST_STATUS_ENABLE		0x0008
#define EXT_DEF_FW_DMA_INT_ENABLE		0x0004
#define EXT_DEF_FW_SENSE_BUFF_DESC_ENABLE	0x0002

typedef struct _EXT_INIT_FW_ISCSI {
	UINT8	Reserved1;				/* 1   */
	UINT8	Version;				/* 1   */
	UINT16	FWOptions;				/* 2   */
	UINT16	AddFWOptions;				/* 2   */
	UINT16	WakeupThreshold;			/* 2   */
	EXT_ISCSI_IP_ADDR	IPAddr;			/* 20  */
	EXT_ISCSI_IP_ADDR	SubnetMask;		/* 20  */
	EXT_ISCSI_IP_ADDR	Gateway;		/* 20  */
	EXT_DNS	DNSConfig;				/* 152 */
	UINT8	Alias[EXT_DEF_ISCSI_ALIAS_LEN];		/* 32  */
	UINT8	iSCSIName[EXT_DEF_ISCSI_NAME_LEN];	/* 256 */
	EXT_ISCSI_DEVICE	DeviceInfo;		/* 64  */
	UINT8	Reserved[4];				/* 4   */
} EXT_INIT_FW_ISCSI , *PEXT_INIT_FW_ISCSI;		/* 576 */

/*
 * ****************************************************************************
 * EXT_ISCSI_PASSTHRU
 * ****************************************************************************
 */
#define EXT_DEF_ISCSI_PASSTHRU_PDU_LENGTH	64

#define EXT_DEF_ISCSI_PASSTHRU_DATA_IN		1
#define EXT_DEF_ISCSI_PASSTHRU_DATA_OUT	2

typedef struct _EXT_ISCSI_PASSTHRU {
	EXT_DEST_ADDR_ISCSI Addr;			/* 268 */
	UINT16	Direction;				/* 2   */
	UINT32	PduInLength;				/* 4   */
	UINT8	PduIn[EXT_DEF_ISCSI_PASSTHRU_PDU_LENGTH];	/* 64  */
	UINT32	PduOutLength;				/* 4   */
	UINT8	PduOut[EXT_DEF_ISCSI_PASSTHRU_PDU_LENGTH];	/* 64  */
	UINT32	Flags;					/* 4   */
	UINT32	Reserved;				/* 4   */
} EXT_ISCSI_PASSTHRU, *PEXT_ISCSI_PASSTHRU;		/* 282 */

/*
 * ****************************************************************************
 * EXT_SCSI_PASSTHRU_ISCSI
 * ****************************************************************************
 */
#define EXT_DEF_SCSI_PASSTHRU_CDB_LENGTH	16

#define EXT_DEF_SCSI_PASSTHRU_DATA_IN		1
#define EXT_DEF_SCSI_PASSTHRU_DATA_OUT		2

#define EXT_DEF_SCSI_SENSE_DATA_SIZE		256

typedef struct _EXT_SCSI_PASSTHRU_ISCSI {
	EXT_SCSI_ADDR_ISCSI Addr;			/* 16  */
	UINT8	Direction;				/* 1   */
	UINT8	CdbLength;				/* 1   */
	UINT8	Cdb[EXT_DEF_SCSI_PASSTHRU_CDB_LENGTH];	/* 16  */
	UINT8	Reserved[16];				/* 16  */
	UINT8	SenseData[EXT_DEF_SCSI_SENSE_DATA_SIZE];/* 256 */
} EXT_SCSI_PASSTHRU_ISCSI, *PEXT_SCSI_PASSTHRU_ISCSI;	/* 306 */


/*
 * ****************************************************************************
 * EXT_ISNS_SERVER
 * ****************************************************************************
 */

#define EXT_DEF_ISNS_WELL_KNOWN_PORT		3205

typedef struct _EXT_ISNS_SERVER {
	UINT8	PerformiSNSDiscovery;			/* 1 */
	UINT8	AutomaticiSNSDiscovery;			/* 1 */	
	UINT8	iSNSNotSupported;			/* 1 */
	UINT8	Reserved1[1];				/* 1 */
	EXT_ISCSI_IP_ADDR	IPAddr;			/* 20 */
	UINT16	PortNumber;				/* 2 */
	UINT16	Reserved2;				/* 2 */
	UINT8	InitiatorName[EXT_DEF_ISCSI_NAME_LEN];	/* 256 */		
	UINT32	Reserved3;				/* 4   */
} EXT_ISNS_SERVER, *PEXT_ISNS_SERVER;			/* 288 */

/*
 * ****************************************************************************
 * EXT_ISNS_DISCOVERED_TARGET_PORTAL
 * ****************************************************************************
 */

typedef struct _EXT_ISNS_DISCOVERED_TARGET_PORTAL
{
	EXT_ISCSI_IP_ADDR	IPAddr;			/* 20 */
	UINT16	PortNumber;				/* 2 */
	UINT16	Reserved;				/* 2 */
} EXT_ISNS_DISCOVERED_TARGET_PORTAL, *PEXT_ISNS_DISCOVERED_TARGET_PORTAL;
							/* 24 */

/*
 * ****************************************************************************
 * EXT_ISNS_DISCOVERED_TARGET
 * ****************************************************************************
 */

#define EXT_DEF_ISNS_MAX_PORTALS		4

typedef struct _EXT_ISNS_DISCOVERED_TARGET
{
	UINT32	NumPortals;				/* 4 */
	EXT_ISNS_DISCOVERED_TARGET_PORTAL Portal[EXT_DEF_ISNS_MAX_PORTALS];	/* 96 */
	UINT32	DDID;					/* 4 */
	UINT8	NameString[EXT_DEF_ISCSI_NAME_LEN];	/* 256 */
	UINT8	Alias[EXT_DEF_ISCSI_ALIAS_LEN];		/* 32 */
} EXT_ISNS_DISCOVERED_TARGET, *PEXT_ISNS_DISCOVERED_TARGET;	/* 392 */

/*
 * ****************************************************************************
 * EXT_ISNS_DISCOVERED_TARGETS
 * ****************************************************************************
 */

#define EXT_DEF_NUM_ISNS_DISCOVERED_TARGETS	32

typedef struct _EXT_ISNS_DISCOVERED_TARGETS
{
	UINT32  iSNSDiscoveredTargetIndexStart;		/* 4 */
	UINT32	NumiSNSDiscoveredTargets;		/* 4 */
	EXT_ISNS_DISCOVERED_TARGET
		iSNSDiscoveredTargets[EXT_DEF_NUM_ISNS_DISCOVERED_TARGETS];
							/* 12544 */	
} EXT_ISNS_DISCOVERED_TARGETS, *PEXT_ISNS_DISCOVERED_TARGETS;
							/* 12548 */


/*
 * ****************************************************************************
 * ACB Defines
 * ****************************************************************************
 */

#define EXT_DEF_ACB_SIZE				0x300

typedef struct _EXT_ACB
{
	UINT8 Buffer[EXT_DEF_ACB_SIZE];			
} EXT_ACB, *PEXT_ACB;                                   /* 0x300 */
		
/* Specifies which ACB for all ACB IOCTLs*/
#define EXT_DEF_ACB_PRIMARY				0
#define EXT_DEF_ACB_SECONDARY	 			1

/* Specifies Command Option for EXT_CC_DISABLE IOCTL */
#define EXT_DEF_ACB_CMD_OPTION_NOT_FORCED		0x0000
#define EXT_DEF_ACB_CMD_OPTION_FORCED			0x0001

/* Specifies Parameter Error for EXT_CC_SET_DATA|EXT_SC_SET_ACB IOCTL */
#define EXT_DEF_ACB_PARAM_ERR_INVALID_VALUE		0x0001
#define EXT_DEF_ACB_PARAM_ERR_INVALID_SIZE		0x0002
#define EXT_DEF_ACB_PARAM_ERR_INVALID_ADDR		0x0003

/* Specifies the type of InitFW for the get defaults */
#define EXT_DEF_VERSION_1						0x0000
#define EXT_DEF_VERSION_2						0x0001


/*
 * ****************************************************************************
 * QUERY_IP_STATE Defines
 * ****************************************************************************
 */

typedef struct _EXT_QUERY_IP_STATE
{
	UINT8  	IP_ACBState[4];				/* 4 */
	UINT32 	ValidLifetime;                          /* 4 */
	UINT32 	PreferredLifetime;                      /* 4 */
	UINT8  	IPAddressInfo1[4];                      /* 4 */
	UINT8	IPAddressInfo2[4];                      /* 4 */
	UINT8	IPAddressInfo3[4];                      /* 4 */
	UINT8	IPAddressInfo4[4];                      /* 4 */
	UINT8	Reserved[4];                            /* 4 */
} EXT_QUERY_IP_STATE, *PEXT_QUERY_IP_STATE;


/*
 * ****************************************************************************
 * NEIGHBOR_CACHE Defines
 * ****************************************************************************
 */
typedef struct _EXT_NEIGHBOR_CACHE {
	UINT32	CacheBufferSize;                        /* 4 */
	UINT8	Reserved[4];                            /* 4 */
	UINT8	Buffer[0];
} EXT_NEIGHBOR_CACHE, *PEXT_NEIGHBOR_CACHE;

#define EXT_DEF_IPv6INFO_ALL_ENTRIES			0xFFFFFFFF
#define EXT_DEF_NEIGHBOR_CACHE_SIZE			0x28 /* 40 decimal */

/*
 * ****************************************************************************
 * DESTINATION_CACHE Defines
 * ****************************************************************************
 */
typedef struct _EXT_DESTINATION_CACHE {
	UINT32	CacheBufferSize;                        /* 4 */
	UINT8	Reserved[4];                            /* 4 */
	UINT8	Buffer[0];
} EXT_DESTINATION_CACHE, *PEXT_DESTINATION_CACHE;

#define EXT_DEF_DESTINATION_CACHE_SIZE			0x38 /* 56 decimal */

/*
 * ****************************************************************************
 * ROUTER_LIST Defines
 * ****************************************************************************
 */
typedef struct _EXT_ROUTER_LIST {
	UINT32	CacheBufferSize;                        /* 4 */
	UINT8	Reserved[4];                            /* 4 */
	UINT8	Buffer[0];
} EXT_ROUTER_LIST, *PEXT_ROUTER_LIST;

#define EXT_DEF_ROUTER_LISTE_SIZE			0x28 /* 40 decimal */

/*
 * ****************************************************************************
 * PREFIX_LIST Defines
 * ****************************************************************************
 */
typedef struct _EXT_PREFIX_LIST {
	UINT32	CacheBufferSize;                        /* 4 */
	UINT8	Reserved[4];                            /* 4 */
	UINT8	Buffer[0];
} EXT_PREFIX_LIST, *PEXT_PREFIX_LIST;

#define EXT_DEF_PREFIX_LIST_SIZE			0x20

/*
 * ****************************************************************************
 * SEND_ROUTER_SOL Defines
 * ****************************************************************************
 */
typedef struct _EXT_SEND_ROUTER_SOL {
	EXT_ISCSI_IP_ADDR	Addr;                   /* 20 */
	UINT32			Flags;                  /* 4  */
	UINT8			Reserved[8];            /* 8  */
} EXT_SEND_ROUTER_SOL, *PEXT_SEND_ROUTER_SOL;

#define EXT_DEF_SOURCE_NOT_AVAIL			0x0001
#define EXT_DEF_ADDRESS_NOT_RESOLVED			0x0002
#define EXT_DEF_NCB_FAIL				0x0003
#define EXT_DEF_TIMEOUT					0x0004
#define EXT_DEF_IPV6_DISABLED				0x0005
#define EXT_DEF_EVENT_ERROR				0x0006

/*
 * ****************************************************************************
 * QUERY_DEVICE_CURRENT_IP Defines
 * ****************************************************************************
 */

typedef struct _EXT_QUERY_DEVICE_CURRENT_IP {
	EXT_ISCSI_IP_ADDR	Addr;		/* 20 */
	UINT32			DeviceState;	/* 4  */
	UINT16			TCPPort; 	/* 2  */
	UINT8			Flags[2];	/* 2  */
	UINT8			Reserved[4];	/* 4  */
} EXT_QUERY_DEVICE_CURRENT_IP, *PEXT_QUERY_DEVICE_CURRENT_IP;	/* 32 */

#endif /* _QLISIOCT_H */
