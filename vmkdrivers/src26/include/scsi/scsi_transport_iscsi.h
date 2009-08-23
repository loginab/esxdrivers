/************************************************************
 * Portions Copyright 2007-2008 VMware, Inc.  All rights reserved.
 ************************************************************/

/*
 * scsi_transport_iscsi.h --
 *
 *  Annex iSCSI Transport Module definitions
 */

#ifndef _ISCSI_TRANSPORT_H_
#define _ISCSI_TRANSPORT_H_

struct scsi_transport_template;
struct iscsi_hdr;
struct Scsi_Host;

/*
*   Local control of build settings
*/
#ifndef TEST_SIGNATURES
#define TEST_SIGNATURES
#else
//#undefine TEST_SIGNATURES
#endif


#define vmk_iscsi_register_transport(transport) \
   vmk_iscsi_register_transport_vm(transport, sizeof(struct \
    scsi_transport_template))


/**********************************************************************
 *
 * User Kernel API Definitions
 *
 **********************************************************************/



/*
 *  iSCSI Parameters are passed from user space code and then resent to the
 *  Media Driver.  The Media Drivers use these by name.   The data for the
 *  parameter is always passed in as an ASCII string.  For instance numeric
 *  parameter will be passed in as "1234\0"
 */
enum iscsi_param {
   ISCSI_PARAM_MAX_RECV_DLENGTH   =   0,
   ISCSI_PARAM_MAX_XMIT_DLENGTH   =   1,
   ISCSI_PARAM_HDRDGST_EN         =   2,
   ISCSI_PARAM_DATADGST_EN        =   3,
   ISCSI_PARAM_INITIAL_R2T_EN     =   4,
   ISCSI_PARAM_MAX_R2T            =   5,
   ISCSI_PARAM_IMM_DATA_EN        =   6,
   ISCSI_PARAM_FIRST_BURST        =   7,
   ISCSI_PARAM_MAX_BURST          =   8,
   ISCSI_PARAM_PDU_INORDER_EN     =   9,
   ISCSI_PARAM_DATASEQ_INORDER_EN =  10,
   ISCSI_PARAM_ERL                =  11,
   ISCSI_PARAM_IFMARKER_EN        =  12,
   ISCSI_PARAM_OFMARKER_EN        =  13,
   ISCSI_PARAM_EXP_STATSN         =  14,
   ISCSI_PARAM_TARGET_NAME        =  15,
   ISCSI_PARAM_TPGT               =  16,
   ISCSI_PARAM_PERSISTENT_ADDRESS =  17,
   ISCSI_PARAM_PERSISTENT_PORT    =  18,
   ISCSI_PARAM_SESS_RECOVERY_TMO  =  19,
   ISCSI_PARAM_CONN_PORT          =  20,
   ISCSI_PARAM_CONN_ADDRESS       =  21,
   ISCSI_PARAM_USERNAME           =  22,
   ISCSI_PARAM_USERNAME_IN        =  23,
   ISCSI_PARAM_PASSWORD           =  24,
   ISCSI_PARAM_PASSWORD_IN        =  25,
   ISCSI_PARAM_FAST_ABORT         =  26,
   ISCSI_PARAM_ISID               =  27,
   ISCSI_PARAM_SSID               =  28,
};

#define ISCSI_CONN_PORT          ( 1 << ISCSI_PARAM_CONN_PORT         )
#define ISCSI_MAX_RECV_DLENGTH   ( 1 << ISCSI_PARAM_MAX_RECV_DLENGTH  )
#define ISCSI_MAX_XMIT_DLENGTH   ( 1 << ISCSI_PARAM_MAX_XMIT_DLENGTH  )
#define ISCSI_HDRDGST_EN         ( 1 << ISCSI_PARAM_HDRDGST_EN        )
#define ISCSI_DATADGST_EN        ( 1 << ISCSI_PARAM_DATADGST_EN       )
#define ISCSI_INITIAL_R2T_EN     ( 1 << ISCSI_PARAM_INITIAL_R2T_EN    )
#define ISCSI_MAX_R2T            ( 1 << ISCSI_PARAM_MAX_R2T           )
#define ISCSI_IMM_DATA_EN        ( 1 << ISCSI_PARAM_IMM_DATA_EN       )
#define ISCSI_FIRST_BURST        ( 1 << ISCSI_PARAM_FIRST_BURST       )
#define ISCSI_MAX_BURST          ( 1 << ISCSI_PARAM_MAX_BURST         )
#define ISCSI_PDU_INORDER_EN     ( 1 << ISCSI_PARAM_PDU_INORDER_EN    )
#define ISCSI_DATASEQ_INORDER_EN ( 1 << ISCSI_PARAM_DATASEQ_INORDER_EN)
#define ISCSI_ERL                ( 1 << ISCSI_PARAM_ERL               )
#define ISCSI_IFMARKER_EN        ( 1 << ISCSI_PARAM_IFMARKER_EN       )
#define ISCSI_OFMARKER_EN        ( 1 << ISCSI_PARAM_OFMARKER_EN       )
#define ISCSI_EXP_STATSN         ( 1 << ISCSI_PARAM_EXP_STATSN        )
#define ISCSI_TARGET_NAME        ( 1 << ISCSI_PARAM_TARGET_NAME       )
#define ISCSI_TPGT               ( 1 << ISCSI_PARAM_TPGT              )
#define ISCSI_CONN_ADDRESS       ( 1 << ISCSI_PARAM_CONN_ADDRESS      )
#define ISCSI_TARGET_NAME        ( 1 << ISCSI_PARAM_TARGET_NAME       )
#define ISCSI_TPGT               ( 1 << ISCSI_PARAM_TPGT              )
#define ISCSI_PERSISTENT_ADDRESS ( 1 << ISCSI_PARAM_PERSISTENT_ADDRESS)
#define ISCSI_PERSISTENT_PORT    ( 1 << ISCSI_PARAM_PERSISTENT_PORT   )
#define ISCSI_SESS_RECOVERY_TMO  ( 1 << ISCSI_PARAM_SESS_RECOVERY_TMO )
#define ISCSI_CONN_PORT          ( 1 << ISCSI_PARAM_CONN_PORT         )
#define ISCSI_CONN_ADDRESS       ( 1 << ISCSI_PARAM_CONN_ADDRESS      )
#define ISCSI_USERNAME           ( 1 << ISCSI_PARAM_USERNAME          )
#define ISCSI_USERNAME_IN        ( 1 << ISCSI_PARAM_USERNAME_IN       )
#define ISCSI_PASSWORD           ( 1 << ISCSI_PARAM_PASSWORD          )
#define ISCSI_PASSWORD_IN        ( 1 << ISCSI_PARAM_PASSWORD_IN       )
#define ISCSI_FAST_ABORT         ( 1 << ISCSI_PARAM_FAST_ABORT        )
#define ISCSI_ISID               ( 1 << ISCSI_PARAM_ISID              )
#define ISCSI_SSID               ( 1 << ISCSI_PARAM_SSID              )



#define ISCSI_PARAM_MAX_RECV_DLENGTH_FMT   "%d"
#define ISCSI_PARAM_MAX_XMIT_DLENGTH_FMT   "%d"
#define ISCSI_PARAM_HDRDGST_EN_FMT         "%d"
#define ISCSI_PARAM_DATADGST_EN_FMT        "%d"
#define ISCSI_PARAM_INITIAL_R2T_EN_FMT     "%d"
#define ISCSI_PARAM_MAX_R2T_FMT            "%d"
#define ISCSI_PARAM_IMM_DATA_EN_FMT        "%d"
#define ISCSI_PARAM_FIRST_BURST_FMT        "%d"
#define ISCSI_PARAM_MAX_BURST_FMT          "%d"
#define ISCSI_PARAM_PDU_INORDER_EN_FMT     "%d"
#define ISCSI_PARAM_DATASEQ_INORDER_EN_FMT "%d"
#define ISCSI_PARAM_ERL_FMT                "%d"
#define ISCSI_PARAM_IFMARKER_EN_FMT        "%d"
#define ISCSI_PARAM_OFMARKER_EN_FMT        "%d"
#define ISCSI_PARAM_EXP_STATSN_FMT         "%u"
#define ISCSI_PARAM_TARGET_NAME_FMT        "%s"
#define ISCSI_PARAM_TPGT_FMT               "%d"
#define ISCSI_PARAM_PERSISTENT_ADDRESS_FMT "%s"
#define ISCSI_PARAM_PERSISTENT_PORT_FMT    "%d"
#define ISCSI_PARAM_SESS_RECOVERY_TMO_FMT  "%d"
#define ISCSI_PARAM_CONN_PORT_FMT          "%u"
#define ISCSI_PARAM_CONN_ADDRESS_FMT       "%s"
#define ISCSI_PARAM_USERNAME_FMT           "%s"
#define ISCSI_PARAM_USERNAME_IN_FMT        "%s"
#define ISCSI_PARAM_PASSWORD_FMT           "%s"
#define ISCSI_PARAM_PASSWORD_IN_FMT        "%s"
#define ISCSI_PARAM_FAST_ABORT_FMT         "%d"

enum iscsi_host_param {
   ISCSI_HOST_PARAM_HWADDRESS      = 0,
   ISCSI_HOST_PARAM_INITIATOR_NAME = 1,
   ISCSI_HOST_PARAM_NETDEV_NAME    = 2,
   ISCSI_HOST_PARAM_IPADDRESS      = 3
};

#define ISCSI_HOST_HWADDRESS        (1<<ISCSI_HOST_PARAM_HWADDRESS      )
#define ISCSI_HOST_IPADDRESS        (1<<ISCSI_HOST_PARAM_IPADDRESS      )
#define ISCSI_HOST_INITIATOR_NAME   (1<<ISCSI_HOST_PARAM_INITIATOR_NAME )
#define ISCSI_HOST_NETDEV_NAME      (1<<ISCSI_HOST_PARAM_NETDEV_NAME    )

typedef enum {
   ISCSI_OK                  =    0, //OK
   ISCSI_ERR_DATASN          = 1001, //Data packet sequence number incorrect
   ISCSI_ERR_DATA_OFFSET     = 1002, //Data packet offset number incorrect
   ISCSI_ERR_MAX_CMDSN       = 1003, //Exceeded Max cmd sequence number
   ISCSI_ERR_EXP_CMDSN       = 1004, //Command sequence number error
   ISCSI_ERR_BAD_OPCODE      = 1005, //Invalid iSCSI OP code
   ISCSI_ERR_DATALEN         = 1006, //Data length error
                                     // ( i.e.: exceeded max data len, etc )
   ISCSI_ERR_AHSLEN          = 1007, //AHS Length error
   ISCSI_ERR_PROTO           = 1008, //Protocol violation ( A bit generic? )
   ISCSI_ERR_LUN             = 1009, //Invalid LUN
   ISCSI_ERR_BAD_ITT         = 1010, //Invalid ITT
   ISCSI_ERR_CONN_FAILED     = 1011, //Connection Failure
   ISCSI_ERR_R2TSN           = 1012, //Ready to send sequence error
   ISCSI_ERR_SESSION_FAILED  = 1013, //Session Failed ( Logout from target,
                                     // all connections down )
   ISCSI_ERR_HDR_DGST        = 1014, //Header Digest invalid
   ISCSI_ERR_DATA_DGST       = 1015, //Data digest invalid
   ISCSI_ERR_PARAM_NOT_FOUND = 1016, //Invalid/Unsupported Parameter
   ISCSI_ERR_NO_SCSI_CMD     = 1017, //Invalid iSCSI command
} iSCSIErrCode_t;

/*
 * These flags describes reason of stop_conn() call
 */
#define STOP_CONN_TERM          0x1
#define STOP_CONN_RECOVER       0x3
#define STOP_CONN_CLEANUP_ONLY  0xff

/*
 * These flags presents iSCSI Data-Path capabilities.
 */
#define CAP_RECOVERY_L0         0x1
#define CAP_RECOVERY_L1         0x2
#define CAP_RECOVERY_L2         0x4
#define CAP_MULTI_R2T           0x8
#define CAP_HDRDGST             0x10
#define CAP_DATADGST            0x20
#define CAP_MULTI_CONN          0x40
#define CAP_TEXT_NEGO           0x80
#define CAP_MARKERS             0x100
#define CAP_FW_DB               0x200
#define CAP_SENDTARGETS_OFFLOAD 0x400
#define CAP_DATA_PATH_OFFLOAD   0x800
#define CAP_SESSION_PERSISTENT  0x1000
#define CAP_IPV6                0x2000
#define CAP_RDMA                0x4000
#define CAP_USER_POLL           0x8000
#define CAP_KERNEL_POLL         0x10000
#define CAP_CONN_CLEANUP        0x20000

/**********************************************************************
 *   Media Driver API Definitions
 **********************************************************************/
#define MAX_PARAM_BUFFER_SZ      4096
#define MAX_SSID_BUFFER_SZ       1024

typedef struct TransportLimitMinMax{
   vmk_uint32 min;
   vmk_uint32 max;
}TransportLimitMinMax;

typedef struct TransportLimitList{
   vmk_uint32 count;
   vmk_uint32 value[0];
}TransportLimitList;

#define TRANPORT_LIMIT_TYPE_UNSUPPORTED    0
#define TRANPORT_LIMIT_TYPE_MINMAX         1
#define TRANPORT_LIMIT_TYPE_LIST           2

typedef struct TransportLimit{
   vmk_uint32 type;
   vmk_uint32 hasPreferred;
   vmk_uint32 preferred;
   vmk_uint32 param;
   union{
      TransportLimitMinMax minMax;
      TransportLimitList list;
   }limit;
}TransportParamLimit;

struct iscsi_transportData;

struct iscsi_cls_conn
{
   void                       *dd_data;  //This is private use for the Media
                                         //  Driver.  It should contain all
                                         //  pertinent information for the
                                         //  Media Driver to be able to manage
                                         //  a connection. The ML does not
                                         //  inspect the contents of this field.
   vmk_uint32                 state;
   struct iscsi_transportData *transportData;

   /*
    * Implemenation Specific Fields
    */
   vmk_ListLinks              list; //linked list structure
   vmk_uint32                 connectionID;  // ID of this Connection
   struct iscsi_cls_session   *session;      // Session holding this conn
   #ifdef TEST_SIGNATURES
   vmk_int64                 connSignature;     // XXX must be at end of struct
   #endif

};


struct sessionFlags
{
   vmk_uint8 doDeallocate:1; // 1 => deallocate when destroy
};


struct iscsi_cls_session
{
   void                       *dd_data;    //This is private use for the Media
                                           // Driver.  It should contain all
                                           // pertinent information for the
                                           // Media Driver to be able to carry
                                           // the session. The ML does not
                                           // check the contents of this field.
   vmk_int32                  recovery_tmo;   // Timeout in seconds
   struct iscsi_transportData *transportData;

   /*
    * Implemenation Specific Fields
    */
   vmk_ListLinks              list; //linked list structure
   void                       *host;   //Host creating this session (opaque)
   vmk_uint32                 targetID;    //Target ID for this session
   vmk_uint32                 channelID;   //Channel ID for this session
   struct iscsi_cls_conn      connections; //This session's connections
   struct iscsi_transport     *transport;  //transport holding this session
   vmk_uint32                 sessionID;   //ID for this Session
   vmk_uint32                 hostNo;      //Host number for this session
   struct sessionFlags        flags;       //flags for this session
   char                       ssid[MAX_SSID_BUFFER_SZ];
   void                   *device;         // Device structure pointer (opaque)
   #ifdef TEST_SIGNATURES
   vmk_int64              sessSignature;   // XXX must be at end of struct
   #endif
};


#define ISCSI_STATS_CUSTOM_DESC_MAX  64

struct iscsi_stats_custom {
        vmk_int8 desc[ISCSI_STATS_CUSTOM_DESC_MAX];
        vmk_uint64 value;
};

struct iscsi_stats {
        /* octets */
        vmk_uint64 txdata_octets;
        vmk_uint64 rxdata_octets;

        /* xmit pdus */
        vmk_uint32 noptx_pdus;
        vmk_uint32 scsicmd_pdus;
        vmk_uint32 tmfcmd_pdus;
        vmk_uint32 login_pdus;
        vmk_uint32 text_pdus;
        vmk_uint32 dataout_pdus;
        vmk_uint32 logout_pdus;
        vmk_uint32 snack_pdus;

        /* recv pdus */
        vmk_uint32 noprx_pdus;
        vmk_uint32 scsirsp_pdus;
        vmk_uint32 tmfrsp_pdus;
        vmk_uint32 textrsp_pdus;
        vmk_uint32 datain_pdus;
        vmk_uint32 logoutrsp_pdus;
        vmk_uint32 r2t_pdus;
        vmk_uint32 async_pdus;
        vmk_uint32 rjt_pdus;

        /* errors */
        vmk_uint32 digest_err;
        vmk_uint32 timeout_err;

        /*
         * iSCSI Custom Statistics support, i.e. Transport could
         * extend existing MIB statistics with its own specific statistics
         * up to ISCSI_STATS_CUSTOM_MAX
         */
        vmk_uint32 custom_length;
        struct iscsi_stats_custom custom[0]
                __attribute__ ((aligned (sizeof(vmk_uint64))));
};

struct iscsi_cmd_task
{
};

struct iscsi_conn
{
};

struct iscsi_mgmt_task
{
};

enum iscsi_tgt_dscvr
{
   blah3
};
//# warning "@@@] To Be defined"

struct iscsi_transport
{
   /* Pointer to module that owns this instance */
   struct module             *owner;
   /* Name of the module */
   char                      *name;
   /* Capabilities */
   vmk_uint32                 caps;
   /* Parameter mask. Controls the param types that are sent to
    *  get_session_param(),set_param(),get_conn_param()
    */
   vmk_uint64                 param_mask;
   /*
    * Host Parameter mask, this controls the param types that
    * will be set to the Media Driver in the get_host_param()
    * call
    */
   vmk_uint64                 host_param_mask;
   /*
    * Pointer to the scsi_host_template structure.  The template
    * must be filled in as IO requests will be sent to the Media
    * Drivers via the scsi templates queuecommand function
    */
   struct scsi_host_template *host_template;
   /* Connection data size */
   vmk_int32                  conndata_size;
   /* Session data size */
   vmk_int32                  sessiondata_size;
   /* Max Lun */
   vmk_int32                  max_lun;
   /* Max connections */
   vmk_int32                  max_conn;
   /* Maximum Command Length */
   vmk_int32                  max_cmd_len;
   /*
    * Create and initialize a session class object. Do not
    * start any connections.
    */
   struct iscsi_cls_session   *(*create_session)
                                (struct iscsi_transport *,
                                struct scsi_transport_template *,
                                vmk_uint16  maxCmds,
                                vmk_uint16  qDepth,
                                vmk_uint32  initialCmdSeqNum,
                                vmk_uint32* hostNumber);
   /*
    * Create and initialize a session class object. Do not
    * start any connections. ( persistent target version )
    */
   struct iscsi_cls_session   *(*create_session_persistent)
                                (struct iscsi_transport *,
                                void *,
                                vmk_uint16  maxCmds,
                                vmk_uint16  qDepth,
                                vmk_uint32  initialCmdSeqNum,
                                vmk_uint32  targetID,
                                vmk_uint32  channelID,
                                vmk_uint32* hostNumber);

   /* Destroy a session class object and tear down a session */
   void                       (*destroy_session)
                                (struct iscsi_cls_session *);
   /* Create a connection class object */
   struct iscsi_cls_conn     *(*create_conn)
                               (struct iscsi_cls_session *,
                               vmk_uint32);
   /* Bind a socket created in user space to a session. */
   vmk_int32                  (*bind_conn)
                                (struct iscsi_cls_session *,
                                 struct iscsi_cls_conn *,
                                 vmk_uint64, vmk_int32);
   /* Enable connection for IO ( Connect ) */
   vmk_int32                  (*start_conn)
                                (struct iscsi_cls_conn *);
   /* Stop IO on the connection ( Disconnect ) */
   vmk_int32                  (*stop_conn)
                                (struct iscsi_cls_conn *,
                                 vmk_int32);
   /* Destroy a session connection */
   void                       (*destroy_conn)
                                (struct iscsi_cls_conn *);
   /* Retrieve session parameters from Media Driver */
   vmk_int32                  (*get_session_param)
                                (struct iscsi_cls_session *,
                                 enum iscsi_param,
                                 vmk_int8 *);
   /* Set connection parameters from Media Driver */
   vmk_int32                  (*set_param)
                                (struct iscsi_cls_conn *,
                                 enum iscsi_param,
                                 vmk_int8 *,
                                vmk_int32);
   /* Retrieve connection parameters from Media Driver */
   vmk_int32                  (*get_conn_param)
                                (struct iscsi_cls_conn *,
                                 enum iscsi_param,
                                 vmk_int8 *);
   /* Retrieve host configuration parameters from Media Driver */
   vmk_int32                  (*get_host_param)
                                (struct Scsi_Host *,
                                 enum iscsi_host_param,
                                 vmk_int8 *);

   /* Set a host configuration parameters for a Media Driver */
   vmk_int32                  (*set_host_param)
                                (struct Scsi_Host *,
                                 enum iscsi_host_param,
                                 vmk_int8 *valueToSet,
                                 vmk_int32 nBytesInBuffer);
   /*
    * Send a data PDU to a target. This is a redirect from an
    * external source, like the iscsid daemon.
    */
   vmk_int32                  (*send_pdu)
                                 (struct iscsi_cls_conn *,
                                  struct iscsi_hdr *,
                                  vmk_int8 *, vmk_uint32);
   /* Retrieve per connection statistics from the Media Driver */
   void                       (*get_stats)
                                 (struct iscsi_cls_conn *,
                                 struct iscsi_stats *);
   /*
    * Perform a Media Driver specific task initialization for
    * cmd task when using libiscsi.c
    */
   void                       (*init_cmd_task)
                                 (struct iscsi_cmd_task *);
   /*
    * Perform a Media Driver specific task initialization for
    * cmd task when using libiscsi.c
    */
   void                       (*init_mgmt_task)
                                 (struct iscsi_conn *,
                                  struct iscsi_mgmt_task *);
   /*
    * Perform a Media Driver specific task initialization for
    * cmd task when using libiscsi.c
    */
   vmk_int32                  (*xmit_mgmt_task)
                                 (struct iscsi_conn *,
                                  struct iscsi_cmd_task *);
   /* Cleanup/Delete a command */
   void                       (*cleanup_cmd_task)
                                 (struct iscsi_conn *conn,
                             struct iscsi_cmd_task *ctask);
   /*
    * This will be called by the ML when the session recovery
    * timer has expired and the driver will be opened back up
    * for IO.
    */
   void                       (*session_recovery_timedout)
                                 (struct iscsi_cls_session *session);
   /*
    * Connect to the specified destination through a Media Driver.
    * This can be used to get a handle on a socket like handle of
    * the Media Driver to allow user space to drive recovery
    * through iscsid
    */
   vmk_int32                  (*ep_connect)
                                 (struct sockaddr *dst_addr,
                                  vmk_int32 non_blocking,
                                  vmk_uint64 *ep_handle);

   /*
    * Connect to the specified destination through a Media Driver.
    * This can be used to get a handle on a socket like handle of
    * the Media Driver to allow user space to drive recovery
    * through iscsid. This extended version provides a handle
    * to the vmknic to use.
    */
   vmk_int32                  (*ep_connect_extended)
                                 (struct sockaddr *dst_addr,
                                  vmk_int32 non_blocking,
                                  vmk_uint64 *ep_handle,
                                  char *vmkNicName);

   /*
    * Poll for an event from the Media Driver
    *   Not currently used by any Media Driver
    */
   vmk_int32                  (*ep_poll)
                                 (vmk_uint64 , vmk_int32);
   /*
    * Disconnect the socket channel though the Media Driver
    *   Not currently used by any Media Driver
    */
   void                       (*ep_disconnect)
                                 (vmk_int64);
   /*
    * Perform a target discovery on a specific ip address
    *   Not currently used by any Media Driver
    */
   vmk_int32                  (*tgt_dscvr)
                                 (struct Scsi_Host *,
                                  enum iscsi_tgt_dscvr,
                             vmk_uint32,
                                  struct sockaddr *);

   vmk_int32                  (*get_transport_limit)
                                 (enum iscsi_param,
                                 TransportParamLimit *,
                                 vmk_int32 listMaxLen);

   /*
    * Implemenation Specific Fields
    */
   vmk_ListLinks                   list; //linked list structure
   void                  *scsi_template; //template for scsi interface
   #ifdef TEST_SIGNATURES
   vmk_int64              tprtSignature;         // XXX must be at end of struct
   #endif
};

/*  End Media Driver API Definitions */

/**********************************************************************
 *  Middle Layer Support Functions Prototypes
 **********************************************************************/

/*
 *
 * iscsi_alloc_session --
 *   Description:  Initialize the ML specific class object that represents a
 *                 session. Note that the size of iscsi_cls_session will vary
 *                 depending on the ML implementation. (IE: Open iSCSI version
 *                 vs VMWare proprietary version) The new session is not added
 *                 to the ML list of sessions for this Media Driver. Media
 *                 Drivers should use iscsi_create_session.
 *   arguments:    struct Scsi_Host        *host        Pointer to the
 *                                                      scsi_host that will
 *                                                      own this target
 *                 struct iscsi_transport  *transport   The transport template
 *                                                      used passed in as part
 *                                                      of
 *                                                      iscsi_register_transport
 *   returns:      pointer to the newly allocated session.
 *   side effects: XXX TBD
 */
extern struct iscsi_cls_session * iscsi_alloc_session(
   struct Scsi_Host       *host,
   struct iscsi_transport *transport
);

/*
 *
 * iscsi_add_session --
 *   Description:  Adds the session object to the ML thus exposing the target
 *                 to the Operating System as a SCSI target.
 *   arguments:    struct iscsi_cls_session  *session    A session previously
 *                                                       created with
 *                                                       iscsi_alloc_session
 *                 unsigned int              target_id   The target ID for
 *                                                       this session
 *   returns:      0 on success, otherwise an OS error.
 *   side effects: XXX TBD
 */
extern int iscsi_add_session(
   struct iscsi_cls_session *session,
   unsigned int             target_id,
   unsigned int             channel
);

/*
 *
 * iscsi_if_create_session_done --
 *   Description:  This is a callback from a Media Driver when it has
 *                 completed creating a session or an existing session has
 *                 gone into the "LOGGED" in state.  It may be used by a
 *                 Hardware Media Driver (such as qlogic) that brings it's
 *                 sessions up on boot. (Currently Not Used)
 *   arguments:    struct iscsi_cls_conn  *connection   The connection that is
 *                                                      carrying the session
 *   returns:      0 on success, otherwise an OS error.
 *   side effects: XXX TBD
 */
extern int iscsi_if_create_session_done(
   struct iscsi_cls_conn *connection
);

/*
 *
 * iscsi_if_destroy_session_done --
 *   Description:  This is a callback from the Media Driver to inform the ML
 *                 that a particular session was removed by the Media Driver.
 *                 (Currently Not Used)
 *   arguments:    struct iscsi_cls_conn  *connection   The connection that is
 *                                                      carrying the session
 *   returns:      0 on success, otherwise an OS error.
 *   side effects: XXX TBD
 */
extern int iscsi_if_destroy_session_done(
   struct iscsi_cls_conn *connection
);


/*
 *
 * iscsi_if_connection_start_done
 *   Description:  This is a callback from a Media Driver when it has
 *                 completed the connection start operation
 *
 *   arguments:    struct iscsi_cls_conn  *connection   The connection that is
 *                                                      carrying the session
 *                 int error                             Error code for the operation
 *   returns:      0 on success, otherwise an OS error.
 *   side effects: XXX TBD
 */
extern int iscsi_if_connection_start_done(
   struct iscsi_cls_conn *connection, int error
);

/*
 *
 * iscsi_if_connection_stop_done
 *   Description:  This is a callback from a Media Driver when it has
 *                 completed the connection stop operation
 *
 *   arguments:    struct iscsi_cls_conn  *connection   The connection that is
 *                                                      carrying the session
 *                 int error                            Error code for the 
 *                                                      operation
 *   returns:      0 on success, otherwise an OS error.
 *   side effects: XXX TBD
 */
extern int iscsi_if_connection_stop_done(
   struct iscsi_cls_conn *connection, int error
);

/*
 *
 * iscsi_create_session --
 *   Description:  This function allocates a new iscsi_cls_session object and
 *                 adds it to the Media Drivers list of sessions. This is done
 *                 by calling iscsi_alloc_session followed by
 *                 iscsi_add_session.  Most Media Drivers should use this
 *                 interface.
 *   arguments:    struct Scsi_Host        *host        The host that will be
 *                                                      used to create the
 *                                                      session
 *                 struct iscsi_transport  *transport   Pointer to the
 *                                                      iscsi_transport
 *                                                      template that was
 *                                                      previously registered
 *                                                      for this host (Media
 *                                                      Driver)
 *                 vmk_uint32              targetID     Target ID for the
 *                                                      session
 *   returns:
 *   side effects: XXX TBD
 */
extern struct iscsi_cls_session * iscsi_create_session(
   void                   *host,
   struct iscsi_transport *transport,
   vmk_uint32             target_id,
   vmk_uint32             channel
);

/*
 *
 * iscsi_offline_session --
 *   Description:  Prevent additional I/O from being sent to the Media Driver
 *               through queue command.  In addition update scsi_device
 *               states to mark this device SDEV_OFFLINE.  Then notify
 *               the upper layer to rescan the path.
 *   arguments:    struct iscsi_cls_session  *session   The session to offline
 *                                                      from the ML
 *   returns:      NONE
 *   side effects: XXX TBD
 */
extern  void iscsi_offline_session(
   struct iscsi_cls_session *session
);

/*
 *
 * iscsi_remove_session --
 *   Description:  Remove the specified session from the Mid Layer. The Mid
 *                 Layer is responsible for removing the scsi target from the
 *                 SCSI layer as well as ensuring any recovery work is cleaned
 *                 up.
 *   arguments:    struct iscsi_cls_session  *session   The session to remove
 *                                                      from the ML
 *   returns:      NONE
 *   side effects: XXX TBD
 */
extern  void iscsi_remove_session(
   struct iscsi_cls_session *session
);

/*
 *
 * iscsi_free_session --
 *   Description:   Release the scsi device associated with this session, this
 *                 should have the effect of cleaning up the session object if
 *                 this is the last reference to the session.
 *   arguments:    struct iscsi_cls_session  *session   Delete this session's
 *                                                      class object freeing
 *                                                      it's memory
 *   returns:      NONE
 *   side effects: XXX TBD
 */
extern  void iscsi_free_session(
   struct iscsi_cls_session *session
);

/*
 *
 * iscsi_destroy_session --
 *   Description:  Reverse of iscsi_create_session.  Removes the session from
 *                 the ML and dereferences any scsi devices. (Needs more
 *                 information)
 *   arguments:    struct iscsi_cls_session  *session   Session to
 *                                                      shutdown/destroy.
 *                                                      Frees memory??
 *   returns:      XXX TBD
 *   side effects: XXX TBD
 */
extern int iscsi_destroy_session(
   struct iscsi_cls_session *session
);

/*
 *
 * iscsi_create_conn --
 *   Description:  Create a new connection to the target on the specified
 *                 session.
 *   arguments:    struct iscsi_cls_session  *session       The session that
 *                                                          we are requesting
 *                                                          a new connection
 *                                                          on
 *                 vmk_uint32                connectionID   The connection
 *                                                          ID/number
 *   returns:      pointer to the newly created connection.
 *   side effects: XXX TBD
 */
extern struct iscsi_cls_conn * iscsi_create_conn(
   struct iscsi_cls_session *session,
   vmk_uint32               connectionID
);

/*
 *
 * iscsi_destroy_conn --
 *   Description:  Close the connection at conn and remove it from the session
 *                 list. Leave the session in place.
 *   arguments:    struct iscsi_cls_conn  *connection   The connection to
 *                                                      shutdown
 *   returns:      0 on success otherwise an OS error
 *   side effects: XXX TBD
 */
extern int iscsi_destroy_conn(
   struct iscsi_cls_conn *connection
);

/*
 *
 * iscsi_unblock_session --
 *   Description:  Allow additional IO work to be sent to the driver through
 *                 it's queuecommand function. It may be necessary to block
 *                 the Operating System IO queue.
 *   arguments:    struct iscsi_cls_session  *session   The session to unblock
 *   returns:      None, function must always succeed.
 *   side effects: XXX TBD
 */
extern  void iscsi_unblock_session(
   struct iscsi_cls_session *session
);

/*
 *
 * iscsi_block_session --
 *   Description:  Prevent additional io from being sent to the Media Driver
 *                 through queuecommand. Any session recovery work should
 *                 still continue.
 *   arguments:    struct iscsi_cls_session  *session   The session to block
 *   returns:      None, function must always succeed.
 *   side effects: XXX TBD
 */
extern  void iscsi_block_session(
   struct iscsi_cls_session *session
);

/*
 *
 * iscsi_register_transport --
 *   Description:  Register this Media Driver with the transport Mid-Layer.
 *                 This is the first step required in enabling the Media
 *                 Driver to function in the Open iSCSI framework.  The
 *                 Mid-Layer will then query the driver for various
 *                 information as well as signal when to start operation.
 *   arguments:    struct iscsi_transport  *transport   definition for this
 *                                                      Media Driver
 *   returns:      The transport template
 *   side effects: XXX TBD
 */
extern struct scsi_transport_template * iscsi_register_transport(
   struct iscsi_transport *transport
);

/*
 *
 * iscsi_unregister_transport --
 *   Description:  This function is called in the clean up path of the Media
 *                 Driver when we are trying to unload it from the kernel.
 *   arguments:    struct iscsi_transport  *transport   The iSCSI transport
 *                                                      template we previously
 *                                                      registered using the
 *                                                      iscsi_register_transport
 *                                                      call.
 *   returns:      Always zero. I suppose it should return an error if the
 *                 driver is still in use.
 *   side effects: XXX TBD
 */
extern int iscsi_unregister_transport(
   struct iscsi_transport *transport
);

/*
 *
 * iscsi_conn_error --
 *   Description:  This up call must be made to the ML when a connection
 *                 fails. IE: a target disconnect occurs, etc. This function
 *                 must then inform the user space process of a connection
 *                 error by sending a ISCSI_KEVENT_CONN_ERROR event packet.
 *   arguments:    struct iscsi_cls_conn  *connection   The connection that we
 *                                                      had an error on
 *                 iSCSIErrCode_t          error        The iSCSI error code
 *                                                      that was encountered:
 *                                                      ISCSI_ERR_CONN_FAILED
 *   returns:      NONE
 *   side effects: XXX TBD
 */
extern  void iscsi_conn_error(
   struct iscsi_cls_conn *connection,
   iSCSIErrCode_t         error
);

/*
 *
 * iscsi_recv_pdu --
 *   Description:  Called to have the ML management code handle a specific PDU
 *                 for us. The required PDU's that must be sent to the ML are:
 *                    ISCSI_OP_NOOP_IN,
 *                    ISCSI_OP_ASYNC_EVENT,
 *                    ISCSI_OP_TEXT_RSP
 *                 The mid layer is then responsible for sending this PDU up
 *                 to the user space daemon for processing using an
 *                 ISCSI_KEVENT_RECV_PDU event packet. Recv pdu will also need
 *                 to handle asynchronous events and not just replies to
 *                 previous send_pdu packets.
 *   arguments:    struct iscsi_cls_conn  *connection   The connection the PDU
 *                                                      arrived on
 *                 struct iscsi_hdr       *PDU          The iSCSI PDU Header
 *                 char                   *data         Any additional data
 *                                                      that arrived with this
 *                                                      PDU
 *                 vmk_uint32             nByteOfData   The size of the
 *                                                      additional data
 *   returns:      OS Error. ( ENOMEM, EINVA, etc )
 *   side effects: XXX TBD
 */
extern int iscsi_recv_pdu(
   struct iscsi_cls_conn *connection,
   void      *PDU,
   char                  *data,
   vmk_uint32            nByteOfData
);

/*
 * vmk_iscsi_register_adapter --
 *   Description: Register an adapter with the icssi_trans layer so that
 *                we can attach a Managemenbt adapter ontop of it
 *                The management adapter is used to expose VSI nodes
 *                and for path uuid information
 *   arguments:    vmk_ScsiAdapter        vmkAdapter    The addapter to attach our
 *                                                      management adapter to
 *                 struct iscsi_transport  *transport   The transport template
 *                                                      that will be used to call into
 *                                                      the media driver for this adapter.
 *
 *   returns:      success or vmk_ReturnStatus
 */
extern VMK_ReturnStatus vmk_iscsi_register_adapter(
   vmk_ScsiAdapter *vmkAdapter, 
   struct iscsi_transport *transport
);


/*
 * vmk_iscsi_unregister_adapter --
 *   Description: Release this adapter freeing it's management adapter object
 *                and removing it fromthe VSI tree.
 *   arguments:    vmk_ScsiAdapter        vmkAdapter    The adapter to release
 *   returns:      success or vmk_ReturnStatus
 */
extern VMK_ReturnStatus vmk_iscsi_unregister_adapter(
   vmk_ScsiAdapter *vmkAdapter
);


#define SIZE_OF_ISCSI_HDR 48


/*
 * VMK Specific interfaces
 */

/*
 * vmk_iscsi_alloc_session
 *
 * Description:  Initialize the ML specific class object that represents a
 *               session. Note that the size of iscsi_cls_session will vary
 *               depending on the ML implementation. (IE: Open iSCSI version
 *               vs VMWare proprietary version) The new session is not added
 *               to the ML list of sessions for this Media Driver. Media
 *               Drivers should use iscsi_create_session.
 * Arguments:    *host        Pointer to the scsi_host that willown this target
 *               *transport   The transport template used passed in as part
 *                            of iscsi_register_transport
 * Returns:      pointer to the newly allocated session.
 * Side effects: XXX TBD
 */
struct iscsi_cls_session *
vmk_iscsi_alloc_session(struct Scsi_Host *host, 
   struct iscsi_transport *transport);

/*
 * vmk_iscsi_add_session
 *
 * Description:  Adds the session object to the ML thus exposing the target
 *               to the Operating System as a SCSI target.
 * Arguments:    session    A session previously created with
 *                          vmk_iscsi_alloc_session
 *               target_id   The target ID for this session
 * Returns:      0 on success, otherwise an OS error.
 * Side effects: XXX TBD
 */
vmk_int32
vmk_iscsi_add_session(struct iscsi_cls_session *session, vmk_uint32 target_id,
   vmk_uint32 channel);

/*
 * vmk_iscsi_create_session
 *
 * Description:  This function allocates a new iscsi_cls_session object and
 *               adds it to the Media Drivers list of sessions. This is done
 *               by calling vmk_iscsi_alloc_session followed by
 *               iscsi_add_session.  Most Media Drivers should use this
 *               interface.
 * Arguments:    host        The host that will be used to create the
 *                           session
 *               transport   Pointer to the iscsi_transport template that was
 *                           previously registered for this host (Media Driver)
 *               targetID     Target ID for the session
 * Returns:      pointer to the session created
 * Side effects: XXX TBD
 */
struct iscsi_cls_session *
vmk_iscsi_create_session(void *host, struct iscsi_transport *transport,
                     vmk_uint32 target_id, vmk_uint32 channel);

/*
 * vmk_iscsi_remove_session
 *
 * Description:  Remove the specified session from the Mid Layer. The Mid
 *               Layer is responsible for removing the scsi target from the
 *               SCSI layer as well as ensuring any recovery work is cleaned
 *               up.
 * Arguments:    session   The session to remove from the ML
 * Returns:      nothing
 * Side effects: XXX TBD
 */
void
vmk_iscsi_remove_session(struct iscsi_cls_session *session);

/*
 * vmk_iscsi_free_session
 *
 * Description:  Release the scsi device associated with this session, this
 *               should have the effect of cleaning up the session object if
 *               this is the last reference to the session.
 * Arguments:    session   Delete session's class object freeing it's memory
 * Returns:      nothing
 * Side effects: XXX TBD
 */
void
vmk_iscsi_free_session(struct iscsi_cls_session *session);

/*
 * vmk_iscsi_destroy_session
 *
 * Description:  Reverse of iscsi_create_session.  Removes the session from
 *               the ML and dereferences any scsi devices. (Needs more
 *               information)
 * Arguments:    session   Session to shutdown/destroy.
 * Returns:      XXX TBD
 * Side effects: XXX TBD
 */
vmk_int32
vmk_iscsi_destroy_session(struct iscsi_cls_session *session);

/*
 * vmk_iscsi_create_conn
 *
 * Description:  Create a new connection to the target on the specified
 *               session.
 * Arguments:    session       session on which we are requesting a new
 *                             connection
 *               connectionID  The connection ID/number
 * Returns:      pointer to the newly created connection.
 * Side effects: XXX TBD
 */
struct iscsi_cls_conn *
vmk_iscsi_create_conn(struct iscsi_cls_session *session,
   vmk_uint32 connectionID);

/*
 * vmk_iscsi_destroy_conn
 *
 * Description:  Close the connection at conn and remove it from the session
 *               list. Leave the session in place.
 * Arguments:    connection   The connection to shutdown
 * Returns:      0 on success otherwise an OS error
 * Side effects: XXX TBD
 */
vmk_int32
vmk_iscsi_destroy_conn(struct iscsi_cls_conn *connection);

/*
 * vmk_iscsi_unblock_session
 *
 * Description:  Allow additional IO work to be sent to the driver through
 *               it's queuecommand function. It may be necessary to block
 *               the Operating System IO queue.
 * Arguments:    session   The session to unblock
 * Returns:      nothing, function must always succeed.
 * Side effects: XXX TBD
 */
void
vmk_iscsi_unblock_session(struct iscsi_cls_session *session);

/*
 * vmk_iscsi_block_session
 *
 * Description:  Prevent additional io from being sent to the Media Driver
 *               through queue command. Any session recovery work should
 *               still continue.
 * Arguments:    session   The session to block
 * Returns:      nothing, function must always succeed.
 * Side effects: XXX TBD
 */
void
vmk_iscsi_block_session(struct iscsi_cls_session *session);

/*
 * vmk_iscsi_register_transport_vm
 *
 * Description:  Register this Media Driver with the transport Mid-Layer.
 *               This is the first step required in enabling the Media
 *               Driver to function in the Open iSCSI framework.  The
 *               Mid-Layer will then query the driver for various
 *               information as well as signal when to start operation.
 * Arguments:    transport   definition for this Media Driver
 * Returns:      The transport template
 * Side effects: XXX TBD
 */
struct scsi_transport_template *
vmk_iscsi_register_transport_vm(struct iscsi_transport *transport, size_t size);

/*
 * vmk_iscsi_unregister_transport
 *
 * Description:  This function is called in the clean up path of the Media
 *               Driver when we are trying to unload it from the kernel.
 * Arguments:    transport   The iSCSI transport template we previously
 *                           registered using the iscsi_register_transport call.
 * Returns:      Always zero. I suppose it should return an error if the
 *               driver is still in use.
 * Side effects: XXX TBD
 */
vmk_int32
vmk_iscsi_unregister_transport(struct iscsi_transport *transport);

/*
 * vmk_iscsi_getSessionFromTarget
 *
 * Description:  return session associated with specified parameters
 * Arguments:    host_no   Device's Host number
 *               channel   Channel on which device is connected
 *               id        Target ID of the device
 * Returns:      pointer to the session, NULL => not found
 * Side effects: none
 */
extern struct iscsi_cls_session *
vmk_iscsi_getSessionFromTarget(unsigned short host_no
   , unsigned int channel, unsigned int id);

/*
 * End VMK Specific interfaces
 */


/* End Middle Layer Support Prototypes */

#endif/*_ISCSI_TRANSPORT_H_*/

