/* **********************************************************
 * Copyright 2006 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Uplink                                                         */ /**
 * \addtogroup Network
 *@{
 * \defgroup Uplink Uplink 
 *@{ 
 *
 * \par Uplink:
 *
 * A module may have many different functional direction and one of
 * them is to be a gateway to external network.
 * Thereby vmkernel could rely on this module to Tx/Rx packets.
 *
 * So one can imagine an uplink as a vmkernel bundle containing
 * all the handle required to interact with a module's internal network 
 * object.
 *
 * Something important to understand is that an uplink need to reflect
 * the harware services provided by the network interface is linked to.
 * Thereby if your network interface  do vlan tagging offloading a capability 
 * should be passed to vmkernel to express this service and it will be able
 * to use this capability to optimize its internal path when the got
 * corresponding uplink is going to be used.
 *
 ***********************************************************************
 */

#ifndef _VMKAPI_NET_UPLINK_H_
#define _VMKAPI_NET_UPLINK_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
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
#include "base/vmkapi_module.h"

#include "net/vmkapi_net.h"

/**
 * \brief MAC address of the device associated to an uplink.
 */

typedef vmk_uint8 vmk_UplinkMACAddress[6];

/**
 * \brief Capabilities provided by the device associated to an uplink.
 */

typedef vmk_uint64 vmk_UplinkCapabilities;

/**
 * \brief Possible capabilities for the device associated to an uplink.
  */

typedef enum {

   /** VLAN tagging offloading on tx path */
   VMK_UPLINK_CAP_HW_TX_VLAN,
   
   /** VLAN tagging offloading on rx path */
   VMK_UPLINK_CAP_HW_RX_VLAN,

   /** Checksum offloading */
   VMK_UPLINK_CAP_IP4_CSUM,

   /** Scatter-gather */
   VMK_UPLINK_CAP_SG,

   /** Scatter-gather span multiple pages */
   VMK_UPLINK_CAP_SG_SPAN_PAGES,

   /** High dma */
   VMK_UPLINK_CAP_HIGH_DMA,

   /** TCP Segmentation Offload */
   VMK_UPLINK_CAP_TSO,   

   /** checksum IPv6 **/
   VMK_UPLINK_CAP_IP6_CSUM,

   /** TSO for IPv6 **/
   VMK_UPLINK_CAP_TSO6,

   /** TSO size up to 256kB **/
   VMK_UPLINK_CAP_TSO256k

} vmk_UplinkCapability;

/**
 * \brief State of the device's link associated to an uplink.
 */

typedef enum {

   /** The device's link state is down */
   VMK_UPLINK_LINK_DOWN,

   /** The device's link state is up */
   VMK_UPLINK_LINK_UP
} vmk_UplinkLinkState;

/**
 * \brief Structure containing link status information related to the device associated to an uplink.
 */

typedef struct {
   
   /** Device link state */
   vmk_UplinkLinkState linkState;
 
   /** Device link speed in Mbps */
   vmk_int32 linkSpeed;

   /** Device full duplex activated */
   vmk_Bool  fullDuplex;
} vmk_UplinkLinkInfo;

/**
 * \brief Structure containing PCI information of the device associated to an uplink.
 */

typedef struct {

   /** PCI bus number */
   vmk_uint32  bus;

   /** PCI slot number */
   vmk_uint32  slot;

   /** PCI function number */
   vmk_uint32  func;

   /** Device vendor identifier */
   vmk_uint16  vendor;

   /** Device identifier */
   vmk_uint16  device;

   /** Allow pci hot plug notifications */
   vmk_Bool    hotPlug;
} vmk_UplinkPCIInfo;

/**
 * \brief Structure containing Panic-time polling information of the device associated to an uplink.
 */

typedef struct {

   /** Interrupt vector */
   vmk_uint32     vector;

   /** Polling data to be passed to the polling function */
   void          *clientData;   
} vmk_UplinkPanicInfo;

/**
 * \brief State of the device associated to an uplink.
 */

typedef vmk_uint64 vmk_UplinkStates;

typedef enum {

   /** The device associated to an uplink is present */
   VMK_UPLINK_STATE_PRESENT     = 0x1,

   /** The device associated to an uplink is ready */
   VMK_UPLINK_STATE_READY       = 0x2,

   /** The device associated to an uplink is running */
   VMK_UPLINK_STATE_RUNNING     = 0x4,

   /** The device's queue associated to an uplink is operational */
   VMK_UPLINK_STATE_QUEUE_OK    = 0x8,

   /** The device associated to an uplink is linked */
   VMK_UPLINK_STATE_LINK_OK     = 0x10,

   /** The device associated to an uplink is in promiscious mode */
   VMK_UPLINK_STATE_PROMISC     = 0x20,

   /** The device associated to an uplink accepts broadcast packets */
   VMK_UPLINK_STATE_BROADCAST   = 0x40,
   
   /** The device associated to an uplink supports multicast packets */
   VMK_UPLINK_STATE_MULTICAST   = 0x80
} vmk_UplinkState;

/**
 * \brief Structure containing memory resources information related to the device associated to an uplink.
 */

typedef struct {
      
   /** Uplink I/O address */
   void                *baseAddr;
   
   /** Shared mem start */
   void                *memStart;

   /** Shared mem end */
   void                *memEnd;

   /** DMA channel */
   vmk_uint8            dma;
} vmk_UplinkMemResources;

/**
 * \brief String containing naming information of the device associated to an uplink. 
 */

typedef vmk_String vmk_UplinkDeviceName;

/**
 * \brief Structure containing the information of the driver controlling the
 *        the device associated to an uplink.
 */

typedef struct {

   /** \brief String used to store the name of the driver */
   vmk_String driver;

   /** \brief String used to store the version of the driver */
   vmk_String version;

   /** \brief String used to store the firmware version of the driver */
   vmk_String firmwareVersion;

   /** \brief String used to store the name of the module managing this driver */
   vmk_String moduleInterface;
} vmk_UplinkDriverInfo;

/**
 * \brief Capabilities of wake-on-lan (wol)
 */
typedef enum {
   /** \brief wake on directed frames */
   VMK_UPLINK_WAKE_ON_PHY         =       0x01,

   /** \brief wake on unicast frame */
   VMK_UPLINK_WAKE_ON_UCAST       =       0x02,

   /** \brief wake on multicat frame */
   VMK_UPLINK_WAKE_ON_MCAST       =       0x04,

   /** \brief wake on broadcast frame */
   VMK_UPLINK_WAKE_ON_BCAST       =       0x08,

   /** \brief wake on arp */
   VMK_UPLINK_WAKE_ON_ARP         =       0x10,

   /** \brief wake up magic frame */
   VMK_UPLINK_WAKE_ON_MAGIC       =       0x20,

   /** \brief wake on magic frame */
   VMK_UPLINK_WAKE_ON_MAGICSECURE =       0x40

} vmk_UplinkWolCaps;

/**
 * \brief Structure describing the wake-on-lan features and state of an uplink
 */

typedef struct {

   /** \brief bit-flags, describing uplink supported wake-on-lan features */
   vmk_UplinkWolCaps supported;

   /** \brief bit-flags, describing uplink enabled wake-on-lan features */
   vmk_UplinkWolCaps enabled;

   /** \brief wake-on-lan secure on password */
   vmk_String secureONPassword;

} vmk_UplinkWolState;

/**
 * \brief Structure containing statistics related to the device associated to an uplink.
 */

typedef struct {

   /** \brief The number of rx packets received by the driver */
   vmk_uint32 rxpkt;

   /** \brief The number of tx packets sent by the driver */
   vmk_uint32 txpkt;

   /** \brief The number of rx bytes by the driver */
   vmk_uint32 rxbytes;

   /** \brief The number of tx bytes by the driver */
   vmk_uint32 txbytes;

   /** \brief The number of rx packets with errors */
   vmk_uint32 rxerr;

   /** \brief The number of tx packets with errors */
   vmk_uint32 txerr;

   /** \brief The number of rx packets dropped */
   vmk_uint32 rxdrp;

   /** \brief The number of tx packets dropped */
   vmk_uint32 txdrp;

   /** \brief The number of rx multicast packets */
   vmk_uint32 mltcast;

   /** \brief The number of collisions */
   vmk_uint32 col;

   /** \brief The number of rx length errors */
   vmk_uint32 rxlgterr;

   /** \brief The number of rx ring buffer overflow */
   vmk_uint32 rxoverr;

   /** \brief The number of rx packets with crc errors */
   vmk_uint32 rxcrcerr;

   /** \brief The number of rx packets with frame alignment error */
   vmk_uint32 rxfrmerr;

   /** \brief The number of rx fifo overrun */
   vmk_uint32 rxfifoerr;

   /** \brief The number of rx packets missed */
   vmk_uint32 rxmisserr;

   /** \brief The number of tx aborted errors */
   vmk_uint32 txaborterr;

   /** \brief The number of tx carriers errors */
   vmk_uint32 txcarerr;

   /** \brief The number of tx fifo errors */
   vmk_uint32 txfifoerr;
   
   /** \brief The number of tx heartbeat errors */
   vmk_uint32 txhearterr;

   /** \brief The number of tx windows errors */
   vmk_uint32 txwinerr;

   /** \brief The number of rx packets received by the module interface hosting the driver */
   vmk_uint32 intrxpkt;

   /** \brief The number of tx packets sent by the module interface hosting the driver */
   vmk_uint32 inttxpkt;

   /** \brief The number of rx packets dropped by the module interface hosting the driver */
   vmk_uint32 intrxdrp;

   /** \brief The number of tx packets dropped by the module interface hosting the driver  */
   vmk_uint32 inttxdrp;

   /** \brief The number of packets completed by the module interface hosting the driver  */
   vmk_uint32 intpktcompl;

   /** \brief String used to store the information specific the device associated to an uplink */
   vmk_String privateStats;
} vmk_UplinkStats;

/*
 ***********************************************************************
 * vmk_UplinkStartTx --                                           */ /**
 *
 * \brief Handler used by vmkernel to send packet through the device
 *        associated to an uplink.
 *
 * \param[in] clientData         Internal module structure for the device
 *                               associated to the uplink. This structure
 *                               is the one passed during uplink connection
 * \param[in] pktList            The set of packet needed to be sent
 *
 * \retval    VMK_OK             All the packets are being processed
 * \retval    VMK_FAILURE        If the module detects any error during
 *                               Tx process
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkStartTx)(void *clientData, 
					      vmk_PktList *pktList);

/*
 ***********************************************************************
 * vmk_UplinkOpenDev --                                           */ /**
 *
 * \brief Handler used by vmkernel to open the device associated to an uplink .
 *
 * \param[in] clientData         Internal module structure for the device
 *                               associated to the uplink. This structure
 *                               is the one passed during uplink connection
 *
 * \retval    VMK_OK             Open succeeded
 * \retval    VMK_FAILURE        Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus        (*vmk_UplinkOpenDev)(void *clientData);

/*
 ***********************************************************************
 * vmk_UplinkCloseDev --                                           */ /**
 *
 * \brief Handler used by vmkernel to close the device associated to an uplink
 *
 * \param[in] clientData         Internal module structure for the device
 *                               associated to the uplink. This structure
 *                               is the one passed during uplink connection
 *
 * \retval    VMK_OK             Close succeeded
 * \retval    VMK_FAILURE        Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus        (*vmk_UplinkCloseDev)(void *clientData);

/*
 ***********************************************************************
 * vmk_UplinkPanicPoll --                                      */ /**
 *
 * \brief Handler used by vmkernel to poll for packets received by 
 *        the device associated to an uplink. Might be ignored.
 *
 * \param[in]  clientData        Internal module structure for the device
 *                               associated to the uplink. This structure
 *                               is the one passed during uplink connection
 * \param[in]  budget            Maximum work to do in the poll function.
 * \param[out] workDone          The amount of work done by the poll handler
 *
 * \retval    VMK_OK             Always
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkPanicPoll)(void *clientData,
                                                vmk_uint32 budget,
                                                vmk_int32* workDone);

/*
 ***********************************************************************
 * vmk_UplinkFlushBuffers --                                      */ /**
 *
 * \brief Handler used by vmkernel to flush the Tx/Rx buffer of 
 *        the device associated to an uplink. Might be ignored.
 *
 * \param[in] clientData         Internal module structure for the device
 *                               associated to the uplink. This structure
 *                               is the one passed during uplink connection
 *
 * \retval    VMK_OK             Always
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkFlushBuffers)(void *clientData);

/*
 ***********************************************************************
 * vmk_UplinkIoctl --                                             */ /**
 *
 * \brief  Handler used by vmkernel to do an ioctl call against the 
 *         device associated to an uplink.
 *         
 *
 * \param[in]  uplinkName         Name of the aimed device
 * \param[in]  cmd                Command ioctl to be issued
 * \param[in]  args               Arguments to be passed to the ioctl call
 * \param[out] result             Result value of the ioctl call
 *
 * \retval    VMK_OK              If ioctl call succeeded
 * \retval    VMK_FAILURE         Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkIoctl)(vmk_String *uplinkName,
					    vmk_uint32 cmd, 
					    void *args, 
					    vmk_uint32 *result);

/*
 ***********************************************************************
 * vmk_UplinkBlockDev --                                          */ /**
 *
 * \brief  Handler used by vmkernel to block a device. No more traffic
 *         should go through after this call.
 *
 * \param[in]  clientData         Internal module structure for the device
 *                                associated to the uplink. This structure
 *                                is the one passed during uplink connection
 *
 * \retval    VMK_OK              If device is blocked
 * \retval    VMK_FAILURE         Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkBlockDev)(void *clientData);

/*
 ***********************************************************************
 * vmk_UplinkUnblockDev --                                        */ /**
 *
 * \brief  Handler used by vmkernel to unblock a device. Traffic should
 *         go through after this call.
 *         
 *
 * \param[in]  clientData         Internal module structure for the device
 *                                associated to the uplink. This structure
 *                                is the one passed during uplink connection
 *
 * \retval    VMK_OK              If device is unblocked
 * \retval    VMK_FAILURE         Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkUnblockDev)(void *clientData);

/*
 ***********************************************************************
 * vmk_UplinkSetLinkStatus --                                     */ /**
 *
 * \brief  Handler used by vmkernel to set the speed/duplex of a device
 *         associated with an uplink.
 *
 * \param[in]  clientData         Internal module structure for the device
 *                                associated to the uplink. This structure
 *                                is the one passed during uplink connection
 * \param[in] linkInfo            Specifies speed and duplex
 *
 * \retval    VMK_OK              If operation was successful
 * \retval    VMK_FAILURE         Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkSetLinkStatus)(void *clientData,
                                                    vmk_UplinkLinkInfo *linkInfo);

/*
 ***********************************************************************
 * vmk_UplinkResetDev --                                          */ /**
 *
 * \brief  Handler used by vmkernel to reset a device.
 *
 * \param[in]  clientData         Internal module structure for the device
 *                                associated to the uplink. This structure
 *                                is the one passed during uplink connection
 *
 * \retval    VMK_OK              If device is reset
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkResetDev)(void *clientData);


/*
 ***********************************************************************
 * vmk_UplinkGetStates --                                         */ /**
 *
 * \brief  Handler used by vmkernel to get the state of a device
 *         associated to an uplink.
 *
 * \param[in]  clientData  Internal module structure for the device
 *                         associated to the uplink. This structure
 *                         is the one passed during uplink connection
 * \param[out] state       State of the device 
 *
 * \retval    VMK_OK       If the information is properly stored
 * \retval    VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkGetStates)(void *clientData, 
						vmk_UplinkStates *states);

/*
 ***********************************************************************
 * vmk_UplinkGetMemResources --                                   */ /**
 *
 * \brief  Handler used by vmkernel to get the memory resources of a device
 *         associated to an uplink.
 *
 * \param[in]  clientData  Internal module structure for the device
 *                         associated to the uplink. This structure
 *                         is the one passed during uplink connection
 * \param[out] resources   Memory resources of the device 
 *
 * \retval    VMK_OK       If the information is properly stored
 * \retval    VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkGetMemResources)(void *clientData,
						      vmk_UplinkMemResources *resources);

/*
 ***********************************************************************
 * vmk_UplinkGetPCIProperties --                                  */ /**
 *
 * \brief  Handler used by vmkernel to get pci properties of a device
 *         associated to an uplink.
 *
 * \param[in]  clientData  Internal module structure for the device
 *                         associated to the uplink. This structure
 *                         is the one passed during uplink connection
 * \param[out] pciInfo     PCI properties of the device
 *
 * \retval    VMK_OK       If the information is properly stored
 * \retval    VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkGetPCIProperties)(void *clientData,
						       vmk_UplinkPCIInfo *pciInfo);

/*
 ***********************************************************************
 * vmk_UplinkGetPanicInfo --                                      */ /**
 *
 * \brief  Handler used by vmkernel to get panic-time polling properties 
 *         of a device associated to an uplink.
 *
 * \param[in]  clientData  Internal module structure for the device
 *                         associated to the uplink. This structure
 *                         is the one passed during uplink connection
 * \param[out] panicInfo   Panic-time polling properties of the device
 *
 * \retval    VMK_OK       If the information is properly stored
 * \retval    VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkGetPanicInfo)(void *clientData,
                                                   vmk_UplinkPanicInfo *panicInfo);

/*
 ***********************************************************************
 * vmk_UplinkGetMACAddr --                                        */ /**
 *
 * \brief  Handler used by vmkernel to get the mac address of a device
 *         associated to an uplink.
 *
 * \param[in]  clientData Internal module structure for the device
 *                        associated to the uplink. This structure
 *                        is the one passed during uplink connection
 * \param[out] macAddr    Buffer used to store the mac address
 *
 * \retval    VMK_OK      If the mac address is properly stored
 * \retval    VMK_FAILURE Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkGetMACAddr)(void *clientData, 
						 vmk_UplinkMACAddress macAddr);

/*
 ***********************************************************************
 * vmk_UplinkGetDeviceName --                                     */ /**
 *
 * \brief  Handler used by vmkernel to get the name of the device
 *         associated to an uplink
 *
 * \param[in]  clientData Internal module structure for the device
 *                        associated to the uplink. This structure
 *                        is the one passed during uplink connection
 * \param[out] devName    Structure used to store all the requested
 *                        information.
 *
 * \retval    VMK_OK      If the name is properly stored
 * \retval    VMK_FAILURE Otherwise
 *
 ***********************************************************************
 */ 

typedef VMK_ReturnStatus (*vmk_UplinkGetName)(void *clientData, 
					      vmk_UplinkDeviceName *devName);
/*
 ***********************************************************************
 * vmk_UplinkGetStats --                                          */ /**
 *
 * \brief  Handler used by vmkernel to get statistics on a device associated
 *         to an uplink.
 *
 * \param[in]  clientData  Internal module structure for the device
 *                         associated to the uplink. This structure
 *                         is the one passed during uplink connection
 * \param[out] stats       Structure used to store all the requested
 *                         information.
 *
 * \retval    VMK_OK       If the statistics are properly stored
 * \retval    VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkGetStats)(void *clientData, 
					       vmk_UplinkStats *stats);
	
/*
 ***********************************************************************
 * vmk_UplinkGetDriverInfo --                                     */ /**
 *
 * \brief  Handler used by vmkernel to get driver information of the
 *         device associated with an uplink.
 *
 * \param[in]  clientData  Internal module structure for the device
 *                         associated to the uplink. This structure
 *                         is the one passed during uplink connection
 * \param[out] driverInfo  Structure used to store all the requested
 *                         information.
 *
 * \retval    VMK_OK       If the driver information is properly stored
 * \retval    VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */
				       
typedef VMK_ReturnStatus (*vmk_UplinkGetDriverInfo)(void *clientData, 
						    vmk_UplinkDriverInfo *driverInfo);

/*
 ***********************************************************************
 * vmk_UplinkGetWolState --                                       */ /**
 *
 * \brief  Handler used by vmkernel to get wake-on-lan state of
 *         device associated with an uplink.
 *
 * \param[in]  clientData  Internal module structure for the device
 *                         associated to the uplink. This structure
 *                         is the one passed during uplink connection
 * \param[out] wolState    Structure used to store all the requested
 *                         information.
 *
 * \retval    VMK_OK       If the driver information is properly stored
 * \retval    VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */
				       
typedef VMK_ReturnStatus (*vmk_UplinkGetWolState)(void *clientData, 
						  vmk_UplinkWolState *wolState);

/*
 ***********************************************************************
 * vmk_UplinkSetWolState --                                       */ /**
 *
 * \brief  Handler used by vmkernel to get wake-on-lan state of
 *         device associated with an uplink.
 *
 * \param[in]  clientData  Internal module structure for the device
 *                         associated to the uplink. This structure
 *                         is the one passed during uplink connection
 * \param[out] wolState    Structure used to store all the requested
 *                         information.
 *
 * \retval    VMK_OK       If the driver information is properly stored
 * \retval    VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */
				       
typedef VMK_ReturnStatus (*vmk_UplinkSetWolState)(void *clientData, 
						  vmk_UplinkWolState *wolState);

/**
 * \brief Default value for timeout handling before panic.
 */ 

#define VMK_UPLINK_WATCHDOG_HIT_CNT_DEFAULT 5

/*
 ***********************************************************************
 * vmk_UplinkGetWatchdogHitCnt --                                 */ /**
 *
 * \brief  Used only if the module provides a timeout mechanism to
 *         recover from a wedged device.
 *         Handler used by vmkernel to get the timeout hit counter needed 
 *         before hitting a panic.
 *         If no panic mode is implemented you could ignore this handler.
 *
 * \param[in]  clientData  Internal module structure for the device
 *                         associated to the uplink. This structure
 *                         is the one passed during uplink connection
 * \param[out] counter     Used to store the timeout hit counter
 *
 * \retval    VMK_OK       If the information is properly stored
 * \retval    VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkGetWatchdogHitCnt)(void *clientData, 
							vmk_int16 *counter);

/*
 ***********************************************************************
 * vmk_UplinkSetWatchdogHitCnt --                                 */ /**
 *
 * \brief  Used only if the module provides a timeout mechanism to
 *         recover from a wedged device.
 *         Handler used by vmkernel to set the timeout hit counter 
 *         needed before hitting a panic.
 *         If no panic mode is implemented you could ignore this handler.
 *
 * \param[in] clientData  Internal module structure for the device
 *                        associated to the uplink. This structure
 *                        is the one passed during uplink connection
 * \param[in] counter     The timeout hit counter
 *
 * \retval    VMK_OK       If the new setting is effective
 * \retval    VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkSetWatchdogHitCnt)(void *clientData, 
							vmk_int16 counter);

/*
 ***********************************************************************
 * vmk_UplinkGetWatchdogStats --                                  */ /**
 *
 * \brief  Used only if the module provides a timeout mechanism to
 *         recover from a wedged device.
 *         Handler used by vmkernel to know the number of times the recover
 *         process (usually a reset) has been run on the device associated
 *         to an uplink. Roughly the number of times the device got wedged.
 *
 * \param[in]  clientData  Internal module structure for the device
 *                         associated to the uplink. This structure
 *                         is the one passed during uplink connection
 * \param[out] counter     The number of times the device got wedged
 *
 * \retval    VMK_OK       If the information is properly stored
 * \retval    VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkGetWatchdogStats)(void *clientData,
						       vmk_int16 *counter);

/**
 * \brief Define the different status of uplink watchdog panic mod
 */

typedef enum {
   
   /** \brief The device's watchdog panic mod is disabled */
   VMK_UPLINK_WATCHDOG_PANIC_MOD_DISABLE,

   /** \brief The device's watchdog panic mod is enabled */
   VMK_UPLINK_WATCHDOG_PANIC_MOD_ENABLE
} vmk_UplinkWatchdogPanicModState;

/*
 ***********************************************************************
 * vmk_UplinkGetWatchdogPanicModState --                          */ /**
 *
 * \brief  Used only if the module provides a timeout mechanism to
 *         recover from a wedged device.
 *         Handler used by vmkernel to know if the timeout panic mod
 *         is enabled or not.
 *
 * \param[in]  clientData  Internal module structure for the device
 *                         associated to the uplink. This structure
 *                         is the one passed during uplink connection
 * \param[out] status      Status of the watchdog panic mod
 *
 * \retval    VMK_OK       If the information is properly stored
 * \retval    VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkGetWatchdogPanicModState)(void *clientData, 
							       vmk_UplinkWatchdogPanicModState *state);

/*
 ***********************************************************************
 * vmk_UplinkSetWatchdogPanicModState --                          */ /**
 *
 * \brief  Used only if the module provides a timeout mechanism to
 *         recover from a wedged device.
 *         Handler used by vmkernel to enable or disable the timeout 
 *         panic mod. Set panic mod could be useful for debugging as it
 *         is possible to get a coredump at this point.
 *
 * \param[in] clientData  Internal module structure for the device
 *                        associated to the uplink. This structure
 *                        is the one passed during uplink connection
 * \param[in] enable      Tne status of the watchdog panic mod
 *
 * \retval    VMK_OK       If the new panic mod is effective
 * \retval    VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkSetWatchdogPanicModState)(void *clientData, 
							       vmk_UplinkWatchdogPanicModState state);

/*
 ***********************************************************************
 * vmk_UplinkSetMTU --                                            */ /**
 *
 * \brief  Handler used by vmkernel to set up the mtu of the
 *         device associated with an uplink.
 *
 * \param[in] clientData  Internal module structure for the device
 *                        associated to the uplink. This structure
 *                        is the one passed during uplink connection
 * \param[in] mtu         The mtu to be set up
 *
 * \retval    VMK_OK       If the mtu setting is effective
 * \retval    VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkSetMTU) (void *clientData, 
					      vmk_uint32 mtu);

/*
 ***********************************************************************
 * vmk_UplinkGetMTU --                                            */ /**
 *
 * \brief  Handler used by vmkernel to retrieve the mtu of the
 *         device associated with an uplink.
 *
 * \param[in]  clientData  Internal module structure for the device
 *                         associated to the uplink. This structure
 *                         is the one passed during uplink connection
 * \param[out] mtu         Used to stored the current mtu
 *
 * \retval    VMK_OK       If the information is properly stored
 * \retval    VMK_FAILURE  Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkGetMTU) (void *clientData,
					      vmk_uint32 *mtu);

/*
 ***********************************************************************
 * vmk_UplinkVlanSetupHw --                                       */ /**
 *
 * \brief Handler used by vmkernel to activate vlan and add vid for the
 *        device associated to an uplink.
 *
 * \param[in] clientData         Internal module structure for the device
 *                               associated to the uplink. This structure
 *                               is the one passed during uplink connection
 * \param[in] enable             Initialize hw vlan functionality
 * \param[in] bitmap             A bitmap of permitted vlan id's.
 *
 * \retval    VMK_OK             If vlan (de)activation succeeded
 * \retval    VMK_FAILURE        Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkVlanSetupHw)(void *clientData, 
                                                  vmk_Bool enable,
						  void *bitmap);

/*
 ***********************************************************************
 * vmk_UplinkVlanRemoveHw --                                      */ /**
 *
 * \brief  Handler used by vmkernel to delete vlan ids and deactivate
 *         hw vlan for the device associated to an uplink.
 *
 * \param[in] clientData         Internal module structure for the device
 *                               associated to the uplink. This structure
 *                               is the one passed during uplink connection
 * \param[in] disable            Deactivate hw vlan completely
 * \param[in] bitmap             A bitmap of permitted vlan id's.
 *
 * \retval    VMK_OK             If vlan update succeeded
 * \retval    VMK_FAILURE        Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkVlanRemoveHw)(void *clientData, 
						   vmk_Bool disable,
                                                   void *bitmap);

/* FIXME : Gagan */

typedef VMK_ReturnStatus (*vmk_UplinkNetqueueOpFunc)(void *, 
						     vmk_NetqueueOp, 
						     void *);

typedef VMK_ReturnStatus (*vmk_UplinkNetqueueXmit)(void *, 
						   vmk_NetqueueQueueId, 
						   vmk_PktList *);
/*
 ***********************************************************************
 * vmk_UplinkUPTOpFunc --                                      */ /**
 *
 * \brief  The routine to dispatch UPT management operations to 
 *         driver exported callbacks
 *
 * \param[in] clientData         Used to identify a VF 
 * \param[in] op                 The operation
 * \param[in] args               The optional arguments for the operation
 *
 * \retval    VMK_OK             If the operation succeeds
 * \retval    VMK_FAILURE        Otherwise
 *
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkUPTOpFunc)(void *clientData, 
						vmk_uint32 uptOp,
                                                void *args);

/*
 * The following function pointer type will be used later for eSwitch
 * setup.
 */

typedef VMK_ReturnStatus (*vmk_UplinkESwitchOpFunc)(void *);

/**
 * \brief Structure used to have access to the properties of the
 *        device associated to an uplink. 
 */

typedef struct {

   /** Handler used to retrieve the state of the device */
   vmk_UplinkGetStates          getStates;

   /** Handler used to retrieve memory resources of the device */
   vmk_UplinkGetMemResources    getMemResources;

   /** Handler used to retrieve pci properties of the device */
   vmk_UplinkGetPCIProperties   getPCIProperties;

   /** Handler used to retrieve panic-time polling properties of the device */
   vmk_UplinkGetPanicInfo       getPanicInfo;

   /** Handler used to retrieve the MAC address of the device */
   vmk_UplinkGetMACAddr         getMACAddr;

   /** Handler used to retrieve the name of the device */
   vmk_UplinkGetName            getName;

   /** Handler used to retrieve the statistics of the device */
   vmk_UplinkGetStats           getStats;

   /** Handler used to retrieve the driver information of the device */
   vmk_UplinkGetDriverInfo      getDriverInfo;

   /** Handler used to retrieve the wake-on-lan state of the device */
   vmk_UplinkGetWolState        getWolState;

   /** Handler used to set the wake-on-lan state of the device */
   vmk_UplinkGetWolState        setWolState;
} vmk_UplinkPropFunctions;

/** 
 * \brief Structure used to have access to the timeout properties of the
 *        device associated to an uplink.
 *        If the module does not provide a timeout mechanism, this information
 *        can be ignored.
 */

typedef struct {

   /** Handler used to retrieve the number of times the device handles timeout before hitting a panic */
   vmk_UplinkGetWatchdogHitCnt         getHitCnt;

   /** Handler used to set the number of times the device handles timeout before hitting a panic */
   vmk_UplinkSetWatchdogHitCnt         setHitCnt;

   /** Handler used to retrieve the global number of times the device hit a timeout */
   vmk_UplinkGetWatchdogStats          getStats;

   /** Handler used to retrieve the timeout panic mod's status for the device */
   vmk_UplinkGetWatchdogPanicModState  getPanicMod;

   /** Handler used to set the timeout panic mod's status for the device */
   vmk_UplinkSetWatchdogPanicModState  setPanicMod;
} vmk_UplinkWatchdogFunctions;

typedef struct {

   /* FIXME : Gagan */

   vmk_UplinkNetqueueOpFunc               netqOpFunc;
   vmk_UplinkNetqueueXmit                 netqXmit;
} vmk_UplinkNetqueueFunctions;

typedef struct {

   /** Handler to dispatch all eSwitch operations */
   vmk_UplinkESwitchOpFunc                eSwitchOpFunc;
} vmk_UplinkESwitchFunctions;

typedef struct {
   /** dispatch routine for UPT management operations */
   vmk_UplinkUPTOpFunc                    uptOpFunc;
} vmk_UplinkUPTFunctions;

typedef struct {

   /** Handler used to setup vlan hardware context and add vid */
   vmk_UplinkVlanSetupHw            setupVlan;

   /** Handler used to delete vlan id and deactivate hw for the device */
   vmk_UplinkVlanRemoveHw           removeVlan;
} vmk_UplinkVlanFunctions;

typedef struct {

   /** Handler used to retrieve the MTU of the device */
   vmk_UplinkGetMTU                 getMTU;

   /** Handler used to set the MTU of the device */
   vmk_UplinkSetMTU                 setMTU;
} vmk_UplinkMtuFunctions;

typedef struct {

   /** Handler used to Tx a packet immediately through the device */
   vmk_UplinkStartTx                startTxImmediate;

   /** Handler used to set up the resources of the device */
   vmk_UplinkOpenDev                open;

   /** Handler used to release the resources of the device */
   vmk_UplinkCloseDev               close;

   /** Handler used to poll device for a Rx packet */
   vmk_UplinkPanicPoll              panicPoll;

   /** Handler used to flush the Rx/Tx buffers of the device */
   vmk_UplinkFlushBuffers           flushRxBuffers;

   /** Handler used to issue an ioctl command to the device */
   vmk_UplinkIoctl                  ioctl;

   /** Handler used to set the device as blocked */
   vmk_UplinkBlockDev               block;

   /** Handler used to set the device as unblocked */
   vmk_UplinkUnblockDev             unblock;

   /** Handler used to change link speed and duplex */
   vmk_UplinkSetLinkStatus          setLinkStatus;

   /** Handler used to reset a device */
   vmk_UplinkResetDev               reset;
} vmk_UplinkCoreFunctions;

/**
 * \brief Structure passed to vmkernel in order to interact with the device
 *        associated to an uplink.
 */

typedef struct vmk_UplinkFunctions {

   /** Set of functions giving access to the core services of the device */
   vmk_UplinkCoreFunctions          coreFns;

   /** Set of functions giving access to the vlan services of the device */
   vmk_UplinkVlanFunctions          vlanFns;

   /** Set of functions giving access to the MTU services of the device*/
   vmk_UplinkMtuFunctions           mtuFns;

   /** Set of functions giving access to the properties/statistics of the device */
   vmk_UplinkPropFunctions          propFns;

   /** Set of functions giving access to the watchdog management of the device */
   vmk_UplinkWatchdogFunctions      watchdogFns;

   /** Set of functions giving access to the netqueue services of the device */
   vmk_UplinkNetqueueFunctions      netqueueFns;

   /** Set of functions giving access to the UPT services of the device */
   vmk_UplinkUPTFunctions           uptFns;

   /** Set of functions giving access to the eSwitch services of the device */
   vmk_UplinkESwitchFunctions       eSwitchFns;
} vmk_UplinkFunctions;

/** \brief Data passed to an uplink completion handler */
typedef void *vmk_UplinkCompletionData;

/*
 ***********************************************************************
 * vmk_UplinkCompletionFn --                                        */ /**
 *
 * \brief Handler called by vmkernel in order to do packet completion.
 *
 * \param[in] reserved           Reserved for private module
 * \param[in] data               Data passed to vmk_UplinkRegisterCompletionFn()
 * \param[in] pktList            List of packets to be completed
 *
 * \retval    VMK_OK             All the packets are being completed
 * \retval    VMK_FAILURE        If the module detects any error during
 *                               completion
 ***********************************************************************
 */

typedef VMK_ReturnStatus (*vmk_UplinkCompletionFn)(void *reserved,
                                                   vmk_UplinkCompletionData data,
                                                   vmk_PktList *pktList);

/**
 * \brief Uplink flags for misc. info.
 */
typedef enum {
   /** \brief hidden from management apps */
   VMK_UPLINK_FLAG_HIDDEN         =       0x01,

} vmk_UplinkFlags;

/**
 * \brief Structure containing all the required information to bind a 
 *        device to an uplink.
 */

typedef struct {

   /** Name of the freshly connected device */
   vmk_String            devName;

   /** Internal module structure for this network device */
   void                  *clientData;

   /** Module identifier of the caller module */
   vmk_ModuleID          moduleID;

   /** Functions used by vmkernel to interact with module network services */
   vmk_UplinkFunctions   *functions;

   /** Maximum packet fragments the caller module can handle */
   vmk_size_t             maxSGLength;  

   /** Capabilities populated to vmkernel level for this particular uplink */
   vmk_UplinkCapabilities cap;

   /** Completion handler for the uplink */
   vmk_UplinkCompletionFn   complHandler;

   /** Data passed to the completion handler of the uplink */
   vmk_UplinkCompletionData complData;

   /** Data misc. flags for the uplink */
   vmk_UplinkFlags          flags;
} vmk_UplinkConnectInfo;

/*
 ***********************************************************************
 * vmk_UplinkUpdateLinkState --                                   */ /**
 *
 * \ingroup Uplink
 * \brief Update link status information related to a specified uplink
 *        with a bundle containing the information.
 *
 * \param[in] uplink   Uplink aimed
 * \param[in] linkInfo Structure containing link information
 *
 * \retval    VMK_OK   Always
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_UplinkUpdateLinkState(vmk_Uplink *uplink,
					   vmk_UplinkLinkInfo *linkInfo);

/*
 ***********************************************************************
 * vmk_UplinkWatchdogTimeoutHit --                                */ /**
 *
 * \brief Notify vmkernel that a watchdog timeout has occurred.
 *
 * \note If an uplink driver has a watchdog for the transmit queue of the device,
 *       the driver should notify vmkernel when a timeout occurs. Vmkernel may use this
 *       information to determine the reliability of a particular uplink.
 *
 * \param[in]  uplink    Uplink aimed
 *
 * \retval     VMK_OK    Always
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_UplinkWatchdogTimeoutHit(vmk_Uplink *uplink);

/*
 ***********************************************************************
 * vmk_UplinkConnected --                                         */ /**
 *
 * \ingroup Uplink
 * \brief Notify vmkernel that an uplink has been connected.
 *
 * \note This function create the bond between vmkernel uplink and 
 *       a module internal structure.
 *       Through this connection vmkernel will be able to manage
 *       Rx/Tx and other operations on module network services.
 *
 * \param[in]  connectInfo    Information passed to vmkernel to bind an 
 *                            uplink to a module internal NIC representation
 * \param[out] uplink         Address of the new uplink
 *
 * \retval     VMK_OK        if succeed
 * \retval     VMK_FAILURE   otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_UplinkConnected(vmk_UplinkConnectInfo *connectInfo,
				     vmk_Uplink **uplink);

/*
 ***********************************************************************
 * vmk_UplinkOpen --                                              */ /**
 *
 * \ingroup Uplink
 * \brief Open the device associated with the uplink
 *
 * \note This function needs to be called if the device associated with
 *       the uplink is not a PCI device. For PCI device, 
 *       vmk_PCIDoPostInsert() should be called instead.
 *
 * \param[in]  uplinkName    Name of the uplink to be opened
 *
 * \retval     VMK_OK        if succeed
 * \retval     VMK_FAILURE   otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_UplinkOpen(vmk_String *uplinkName);

/*
 ***********************************************************************
 * vmk_UplinkClose --                                             */ /**
 *
 * \ingroup Uplink
 * \brief Close the device associated with the uplink and disconnect the
 *        uplink from the network services
 *
 * \note This function needs to be called if the device associated with
 *       the uplink is not a PCI device. For PCI device,
 *       vmk_PCIDoPreRemove() should be called instead.
 *
 * \param[in]  uplinkName    Name of the uplink to be closed
 *
 * \retval     VMK_OK        if succeed
 * \retval     VMK_FAILURE   otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_UplinkClose(vmk_String *uplinkName);

/*
 ***********************************************************************
 * vmk_UplinkIsNameAvailable --                                   */ /**
 *
 * \ingroup Uplink
 * \brief Check if a name is already used by an uplink in vmkernel.
 *
 * \param[in]  uplinkName     Name of the uplink
 *
 * \retval     VMK_TRUE       the uplink name is available
 * \retval     VMK_FALSE      otherwise
 *
 ***********************************************************************
 */

vmk_Bool vmk_UplinkIsNameAvailable(vmk_String *uplinkName);

/*
 ***********************************************************************
 * vmk_UplinkWorldletSet --                                       */ /**
 *
 * \ingroup Uplink
 * \brief Associate worldlet with an uplink.
 *
 * \param[in]  uplink   Uplink aimed
 * \param[in]  worldlet Worldlet to associate with this uplink
 *
 * \retval     VMK_OK        if succeed
 * \retval     VMK_FAILURE   otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_UplinkWorldletSet(vmk_Uplink *uplink,
                                       void *worldlet);

/*
 ***********************************************************************
 * vmk_UplinkCapabilitySet --                                     */ /**
 *
 * \ingroup Uplink
 * \brief Enable/Disable a capability for an uplink.
 *
 * \param[in] uplinkCaps    Capabilities to be modified
 * \param[in] cap           Capability to be enabled/disabled
 * \param[in] enable        true => enable, false => disable
 *
 * \retval    VMK_OK        If capability is valid
 * \retval    VMK_NOT_FOUND Otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_UplinkCapabilitySet(vmk_UplinkCapabilities *uplinkCaps, 
					 vmk_UplinkCapability cap, 
					 vmk_Bool enable);

/*
 ***********************************************************************
 * vmk_UplinkCapabilityGet --                                     */ /**
 *
 * \ingroup Uplink
 * \brief Retrieve status of a capability for an uplink.
 *
 * \param[in]  uplinkCaps    Capabilities to be modified
 * \param[in]  cap           Capability to be checked
 * \param[out] status        true => enabled, false => disabled
 *
 * \retval     VMK_OK        If capability is valid
 * \retval     VMK_NOT_FOUND Otherwise
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_UplinkCapabilityGet(vmk_UplinkCapabilities *uplinkCaps, 
					 vmk_UplinkCapability cap, 
					 vmk_Bool *status);

#endif /* _VMKAPI_NET_UPLINK_H_ */
/** @} */
/** @} */
