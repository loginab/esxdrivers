/* **********************************************************
 * Copyright 2004 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * SCSI Constants                                                 */ /**
 * \addtogroup SCSI
 * @{
 *
 * \defgroup SCSIconst SCSI Constants
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_SCSI_CONST_H_
#define _VMKAPI_SCSI_CONST_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"
/** \endcond never */

/*
 * Non-exhaustive list of SCSI operation codes.  Note that
 * some codes are defined differently according to the target
 * device.  Also, codes may have slightly different meanings
 * and/or names based on the version of the SCSI spec.
 *
 * Note: Command descriptions come from the "SCSI Book" and not
 *       from the SCSI specifications (YMMV).
 */
/** \brief Test if LUN ready to accept a command. */
#define VMK_SCSI_CMD_TEST_UNIT_READY       0x00
/** \brief Seek to track 0. */
#define VMK_SCSI_CMD_REZERO_UNIT           0x01
/** \brief Return detailed error information. */
#define VMK_SCSI_CMD_REQUEST_SENSE         0x03
#define VMK_SCSI_CMD_FORMAT_UNIT           0x04
#define VMK_SCSI_CMD_READ_BLOCKLIMITS      0x05
#define VMK_SCSI_CMD_REASSIGN_BLOCKS       0x07
/** \brief Media changer. */
#define VMK_SCSI_CMD_INIT_ELEMENT_STATUS   0x07
/** \brief Read w/ limited addressing. */
#define VMK_SCSI_CMD_READ6                 0x08
/** \brief Write w/ limited addressing. */
#define VMK_SCSI_CMD_WRITE6                0x0a
/** \brief Print data. */
#define VMK_SCSI_CMD_PRINT                 0x0a
/** \brief Seek to LBN. */
#define VMK_SCSI_CMD_SEEK6                 0x0b
/** \brief Advance and print. */
#define VMK_SCSI_CMD_SLEW_AND_PRINT        0x0b
/** \brief Read backwards. */
#define VMK_SCSI_CMD_READ_REVERSE          0x0f
#define VMK_SCSI_CMD_WRITE_FILEMARKS       0x10
/** \brief Print contents of buffer. */
#define VMK_SCSI_CMD_SYNC_BUFFER           0x10
#define VMK_SCSI_CMD_SPACE                 0x11
/** \brief Return LUN-specific information. */
#define VMK_SCSI_CMD_INQUIRY               0x12
/** \brief Recover buffered data. */
#define VMK_SCSI_CMD_RECOVER_BUFFERED      0x14
/** \brief Set device parameters. */
#define VMK_SCSI_CMD_MODE_SELECT           0x15
/** \brief Make LUN accessible only to certain initiators. */
#define VMK_SCSI_CMD_RESERVE_UNIT          0x16
/** \brief Make LUN accessible to other initiators. */
#define VMK_SCSI_CMD_RELEASE_UNIT          0x17
/** \brief Autonomous copy from/to another device. */
#define VMK_SCSI_CMD_COPY                  0x18
#define VMK_SCSI_CMD_ERASE                 0x19
/** \brief Read device parameters. */
#define VMK_SCSI_CMD_MODE_SENSE            0x1a
/** \brief Load/unload medium. */
#define VMK_SCSI_CMD_START_UNIT            0x1b
/** \brief Perform scan. */
#define VMK_SCSI_CMD_SCAN                  0x1b
/** \brief Interrupt printing. */
#define VMK_SCSI_CMD_STOP_PRINT            0x1b
/** \brief Read self-test results. */
#define VMK_SCSI_CMD_RECV_DIAGNOSTIC       0x1c
/** \brief Initiate self-test. */
#define VMK_SCSI_CMD_SEND_DIAGNOSTIC       0x1d
/** \brief Lock/unlock door. */
#define VMK_SCSI_CMD_MEDIUM_REMOVAL        0x1e
/** \brief Read format capacities. */
#define VMK_SCSI_CMD_READ_FORMAT_CAPACITIES 0x23
/** \brief Set scanning window. */
#define VMK_SCSI_CMD_SET_WINDOW            0x24
/** \brief Get scanning window. */
#define VMK_SCSI_CMD_GET_WINDOW            0x25
/** \brief Read number of logical blocks. */
#define VMK_SCSI_CMD_READ_CAPACITY         0x25
/** \brief Read. */
#define VMK_SCSI_CMD_READ10                0x28
/** \brief Read max generation address of LBN. */
#define VMK_SCSI_CMD_READ_GENERATION       0x29
/** \brief Write. */
#define VMK_SCSI_CMD_WRITE10               0x2a
/** \brief Seek LBN. */
#define VMK_SCSI_CMD_SEEK10                0x2b
/** \brief Media changer. */
#define VMK_SCSI_CMD_POSITION_TO_ELEMENT   0x2b
/** \brief Read specific version of changed block. */
#define VMK_SCSI_CMD_READ_UPDATED_BLOCK    0x2d
/** \brief Write w/ verify of success. */
#define VMK_SCSI_CMD_WRITE_VERIFY          0x2e
/** \brief Verify success. */
#define VMK_SCSI_CMD_VERIFY                0x2f
/** \brief Search for data pattern. */
#define VMK_SCSI_CMD_SEARCH_DATA_HIGH      0x30
/** \brief Search for data pattern. */
#define VMK_SCSI_CMD_SEARCH_DATA_EQUAL     0x31
/** \brief Search for data pattern. */
#define VMK_SCSI_CMD_SEARCH_DATA_LOW       0x32
/** \brief Define logical block boundaries. */
#define VMK_SCSI_CMD_SET_LIMITS            0x33
/** \brief Read data into buffer. */
#define VMK_SCSI_CMD_PREFETCH              0x34
/** \brief Read current tape position. */
#define VMK_SCSI_CMD_READ_POSITION         0x34
/** \brief Re-read data into buffer. */
#define VMK_SCSI_CMD_SYNC_CACHE            0x35
/** \brief Lock/unlock data in cache. */
#define VMK_SCSI_CMD_LOCKUNLOCK_CACHE      0x36
#define VMK_SCSI_CMD_READ_DEFECT_DATA      0x37
/** \brief Search for free area. */
#define VMK_SCSI_CMD_MEDIUM_SCAN           0x38
/** \brief Compare data. */
#define VMK_SCSI_CMD_COMPARE               0x39
/** \brief Autonomous copy w/ verify. */
#define VMK_SCSI_CMD_COPY_VERIFY           0x3a
/** \brief Write data buffer. */
#define VMK_SCSI_CMD_WRITE_BUFFER          0x3b
/** \brief Read data buffer. */
#define VMK_SCSI_CMD_READ_BUFFER           0x3c
/** \brief Substitute block with an updated one. */
#define VMK_SCSI_CMD_UPDATE_BLOCK          0x3d
/** \brief Read data and ECC. */
#define VMK_SCSI_CMD_READ_LONG             0x3e
/** \brief Write data and ECC. */
#define VMK_SCSI_CMD_WRITE_LONG            0x3f
/** \brief Set SCSI version. */
#define VMK_SCSI_CMD_CHANGE_DEF            0x40
#define VMK_SCSI_CMD_WRITE_SAME            0x41
/** \brief Read subchannel data and status. */
#define VMK_SCSI_CMD_READ_SUBCHANNEL       0x42
/** \brief Read contents table. */
#define VMK_SCSI_CMD_READ_TOC              0x43
/** \brief Read LBN header. */
#define VMK_SCSI_CMD_READ_HEADER           0x44
/** \brief Audio playback. */
#define VMK_SCSI_CMD_PLAY_AUDIO10          0x45
/** \brief Get configuration (SCSI-3). */
#define VMK_SCSI_CMD_GET_CONFIGURATION     0x46
/** \brief Audio playback starting at MSF address. */
#define VMK_SCSI_CMD_PLAY_AUDIO_MSF        0x47
/** \brief Audio playback starting at track/index. */
#define VMK_SCSI_CMD_PLAY_AUDIO_TRACK      0x48
/** \brief Audio playback starting at relative track. */
#define VMK_SCSI_CMD_PLAY_AUDIO_RELATIVE   0x49
#define VMK_SCSI_CMD_GET_EVENT_STATUS_NOTIFICATION 0x4a
/** \brief Audio playback pause/resume. */
#define VMK_SCSI_CMD_PAUSE                 0x4b
/** \brief Select statistics. */
#define VMK_SCSI_CMD_LOG_SELECT            0x4c
/** \brief Read statistics. */
#define VMK_SCSI_CMD_LOG_SENSE             0x4d
/** \brief Audio playback stop. */
#define VMK_SCSI_CMD_STOP_PLAY             0x4e
/** \brief Info on CDRs. */
#define VMK_SCSI_CMD_READ_DISC_INFO        0x51
/** \brief Track info on CDRs. */
#define VMK_SCSI_CMD_READ_TRACK_INFO       0x52
/** \brief Leave space for data on CDRs. */
#define VMK_SCSI_CMD_RESERVE_TRACK         0x53
/** \brief Optimum Power Calibration. */
#define VMK_SCSI_CMD_SEND_OPC_INFORMATION  0x54
/** \brief Set device parameters. */
#define VMK_SCSI_CMD_MODE_SELECT10         0x55
#define VMK_SCSI_CMD_RESERVE_UNIT10        0x56
#define VMK_SCSI_CMD_RELEASE_UNIT10        0x57
/** \brief Read device parameters. */
#define VMK_SCSI_CMD_MODE_SENSE10          0x5a
/** \brief Close area/sesssion (recordable). */
#define VMK_SCSI_CMD_CLOSE_SESSION         0x5b
/** \brief CDR burning info.. */
#define VMK_SCSI_CMD_READ_BUFFER_CAPACITY  0x5c
/** \brief (CDR Related?). */
#define VMK_SCSI_CMD_SEND_CUE_SHEET        0x5d
#define VMK_SCSI_CMD_PERSISTENT_RESERVE_IN 0x5e
#define VMK_SCSI_CMD_PERSISTENT_RESERVE_OUT 0x5f
/** \brief Read data. */
#define VMK_SCSI_CMD_READ16                0x88
/** \brief Write data. */
#define VMK_SCSI_CMD_WRITE16               0x8a
/** \brief Read number of logical blocks. */
#define VMK_SCSI_CMD_READ_CAPACITY16       0x9e
#define VMK_SCSI_CMD_REPORT_LUNS           0xa0
/** \brief Erase RW media. */
#define VMK_SCSI_CMD_BLANK                 0xa1
/** \brief Service actions define reports. */
#define VMK_SCSI_CMD_MAINTENANCE_IN        0xa3
/** \brief Service actions define changes. */
#define VMK_SCSI_CMD_MAINTENANCE_OUT       0xa4
#define VMK_SCSI_CMD_SEND_KEY              0xa3
/** \brief Report key (SCSI-3). */
#define VMK_SCSI_CMD_REPORT_KEY            0xa4
/** \brief Report target port group. */
#define VMK_SCSI_CMD_RTPGC                 0xa3
/** \brief Set target port group. */
#define VMK_SCSI_CMD_STPGC                 0xa4
#define VMK_SCSI_CMD_MOVE_MEDIUM           0xa5
/** \brief Audio playback. */
#define VMK_SCSI_CMD_PLAY_AUDIO12          0xa5
#define VMK_SCSI_CMD_EXCHANGE_MEDIUM       0xa6
#define VMK_SCSI_CMD_LOADCD                0xa6
/** \brief Read (SCSI-3). */
#define VMK_SCSI_CMD_READ12                0xa8
/** \brief Audio playback starting at relative track. */
#define VMK_SCSI_CMD_PLAY_TRACK_RELATIVE   0xa9
/** \brief Write data. */
#define VMK_SCSI_CMD_WRITE12               0xaa
/** \brief Erase logical block. */
#define VMK_SCSI_CMD_ERASE12               0xac
#define VMK_SCSI_CMD_GET_PERFORMANCE       0xac
/** \brief Read DVD structure (SCSI-3). */
#define VMK_SCSI_CMD_READ_DVD_STRUCTURE    0xad
/** \brief Write logical block, verify success. */
#define VMK_SCSI_CMD_WRITE_VERIFY12        0xae
/** \brief Verify data. */
#define VMK_SCSI_CMD_VERIFY12              0xaf
/** \brief Search data pattern. */
#define VMK_SCSI_CMD_SEARCH_DATA_HIGH12    0xb0
/** \brief Search data pattern. */
#define VMK_SCSI_CMD_SEARCH_DATA_EQUAL12   0xb1
/** \brief Search data pattern. */
#define VMK_SCSI_CMD_SEARCH_DATA_LOW12     0xb2
/** \brief Set block limits. */
#define VMK_SCSI_CMD_SET_LIMITS12          0xb3
#define VMK_SCSI_CMD_REQUEST_VOLUME_ELEMENT_ADDR 0xb5
#define VMK_SCSI_CMD_SEND_VOLUME_TAG       0xb6
/** \brief For avoiding over/underrun. */
#define VMK_SCSI_CMD_SET_STREAMING         0xb6
/** \brief Read defect data information. */
#define VMK_SCSI_CMD_READ_DEFECT_DATA12    0xb7
/** \brief Read element status. */
#define VMK_SCSI_CMD_READ_ELEMENT_STATUS   0xb8
/** \brief Set data rate. */
#define VMK_SCSI_CMD_SELECT_CDROM_SPEED    0xb8
/** \brief Read CD information (all formats, MSF addresses). */
#define VMK_SCSI_CMD_READ_CD_MSF           0xb9
/** \brief Fast audio playback. */
#define VMK_SCSI_CMD_AUDIO_SCAN            0xba
/** \brief (proposed). */
#define VMK_SCSI_CMD_SET_CDROM_SPEED       0xbb
#define VMK_SCSI_CMD_SEND_CDROM_XA_DATA    0xbc
#define VMK_SCSI_CMD_PLAY_CD               0xbc
#define VMK_SCSI_CMD_MECH_STATUS           0xbd
/** \brief Read CD information (all formats, MSF addresses). */
#define VMK_SCSI_CMD_READ_CD               0xbe
/** \brief Burning DVDs?. */
#define VMK_SCSI_CMD_SEND_DVD_STRUCTURE    0xbf
/**
 * A workaround for a specific scanner (NIKON LS-2000).
 * Can be removed once Linux backend uses 2.4.x interface
 */
#define VMK_SCSI_CMD_VENDOR_NIKON_UNKNOWN  0xe1

/*
 * Sense key values.
 */

/** \brief There is no sense information. */
#define VMK_SCSI_SENSE_KEY_NONE            0x0
/** \brief The last command completed succesfully but used error correction in the process. */
#define VMK_SCSI_SENSE_KEY_RECOVERED_ERROR 0x1
/** \brief The addressed LUN is not ready to be accessed. */
#define VMK_SCSI_SENSE_KEY_NOT_READY       0x2
/** \brief The target detected a data error on the medium. */
#define VMK_SCSI_SENSE_KEY_MEDIUM_ERROR    0x3
/** \brief The target detected a hardware error during a command or self-test. */
#define VMK_SCSI_SENSE_KEY_HARDWARE_ERROR  0x4
/** \brief Either the command or the parameter list contains an error. */
#define VMK_SCSI_SENSE_KEY_ILLEGAL_REQUEST 0x5
/** \brief The LUN has been reset (bus reset of medium change). */
#define VMK_SCSI_SENSE_KEY_UNIT_ATTENTION  0x6
/** \brief Access to the data is blocked. */
#define VMK_SCSI_SENSE_KEY_DATA_PROTECT    0x7
/** \brief Reached an unexpected written or unwritten region of the medium. */
#define VMK_SCSI_SENSE_KEY_BLANK_CHECK     0x8
/** \brief COPY, COMPARE, or COPY AND VERIFY was aborted. */
#define VMK_SCSI_SENSE_KEY_COPY_ABORTED    0xa
/** \brief The target aborted the command. */
#define VMK_SCSI_SENSE_KEY_ABORTED_CMD     0xb
/** \brief Comparison for SEARCH DATA was unsuccessful. */
#define VMK_SCSI_SENSE_KEY_EQUAL           0xc
/** \brief The medium is full. */
#define VMK_SCSI_SENSE_KEY_VOLUME_OVERFLOW 0xd
/** \brief Source and data on the medium do not agree. */
#define VMK_SCSI_SENSE_KEY_MISCOMPARE      0xe

/*
 * The Additional Sense Code - ASC             and
 *     Additional Sense Code Qualifiers - ASCQ
 * always come in pairs.
 *
 * Note:
 *     These values are found at senseBuffer[12} and senseBuffer[13].
 *     You may see references to these in legacy code. New code should make an
 *     attempt to use the ASC/ASCQ syntax.
 */
/** \brief Logical unit not ready. */
#define VMK_SCSI_ASC_LU_NOT_READY                                            0x04
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_CAUSE_NOT_REPORTABLE                  0x00
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_CAUSE_NOT_REPORTABLE                  0x00
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_UNIT_BECOMING_READY                   0x01
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_UNIT_BECOMING_READY                   0x01
/** \brief Initializing command required. */
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_INIT_CMD_REQUIRED                     0x02
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_MANUAL_INTERVENTION_REQUIRED          0x03
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_MANUAL_INTERVENTION_REQUIRED          0x03
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_FORMAT_IN_PROGRESS                    0x04
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_FORMAT_IN_PROGRESS                    0x04
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_REBUILD_IN_PROGRESS                   0x05
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_REBUILD_IN_PROGRESS                   0x05
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_RECALCULATION_IN_PROGRESS             0x06
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_RECALCULATION_IN_PROGRESS             0x06
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_OPERATION_IN_PROGRESS                 0x07
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_OPERATION_IN_PROGRESS                 0x07
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_LONG_WRITE_IN_PROGRESS                0x08
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_LONG_WRITE_IN_PROGRESS                0x08
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_SELF_TEST_IN_PROGRESS                 0x09
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_SELF_TEST_IN_PROGRESS                 0x09
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_ASYMMETRIC_ACCESS_STATE_TRANSITION    0x0a
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_ASYMMETRIC_ACCESS_STATE_TRANSITION    0x0a
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_TARGET_PORT_IN_STANDBY_STATE          0x0b
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_TARGET_PORT_IN_STANDBY_STATE          0x0b
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_TARGET_PORT_IN_UNAVAILABLE_STATE      0x0c
#define VMK_SCSI_ASC_LU_NOT_READY_ASCQ_TARGET_PORT_IN_UNAVAILABLE_STATE      0x0c
/** \brief Logical unit doesn't respond to selection. */
#define VMK_SCSI_ASC_LU_NO_RESPONSE_TO_SELECTION                             0x05
/** \brief Write error. */
#define VMK_SCSI_ASC_WRITE_ERROR                                             0x0c
/** \brief Unrecovered read error. */
#define VMK_SCSI_ASC_UNRECOVERED_READ_ERROR                                  0x11
/** \brief Parameter list length error. */
#define VMK_SCSI_ASC_PARAM_LIST_LENGTH_ERROR                                 0x1a
/** \brief Invalid command operation code. */
#define VMK_SCSI_ASC_INVALID_COMMAND_OPERATION                               0x20
#define VMK_SCSI_ASC_INVALID_FIELD_IN_CDB                                    0x24
#define VMK_SCSI_ASC_INVALID_FIELD_IN_CDB                                    0x24
/** \brief LU has been removed. */
#define VMK_SCSI_ASC_LU_NOT_SUPPORTED                                        0x25
#define VMK_SCSI_ASC_INVALID_FIELD_IN_PARAMETER_LIST                         0x26
#define VMK_SCSI_ASC_INVALID_FIELD_IN_PARAMETER_LIST                         0x26
/** \brief Device is write protected. */
#define VMK_SCSI_ASC_WRITE_PROTECTED                                         0x27
/** \brief After changing medium. */
#define VMK_SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED                                 0x28
/** \brief Device power-on or SCSI reset. */
#define VMK_SCSI_ASC_POWER_ON_OR_RESET                                       0x29
#define VMK_SCSI_ASC_COMMANDS_CLEARED                                        0x2F
#define VMK_SCSI_ASC_COMMANDS_CLEARED                                        0x2F
#define VMK_SCSI_ASC_PARAMS_CHANGED                                          0x2a
#define VMK_SCSI_ASC_PARAMS_CHANGED                                          0x2a
#define VMK_SCSI_ASC_PARAMS_CHANGED_ASCQ_ASYMMETRIC_ACCESS_STATE_CHANGED     0x06
#define VMK_SCSI_ASC_PARAMS_CHANGED_ASCQ_ASYMMETRIC_ACCESS_STATE_CHANGED     0x06
/** \brief Saving parameters not supported. */
#define VMK_SCSI_ASC_SAVING_PARAMS_NOT_SUPPORTED                             0x39
/** \brief Changing medium. */
#define VMK_SCSI_ASC_MEDIUM_NOT_PRESENT                                      0x3a
/** \brief An ascq. */
#define VMK_SCSI_ASC_MEDIUM_NOT_PRESENT_ASCQ_TRAY_OPEN                       0x02
/** \brief Something changed in LU or Target. */
#define VMK_SCSI_ASC_CHANGED                                                 0x3f
/** \brief Internal target failure. */
#define VMK_SCSI_ASC_INTERNAL_TARGET_FAILURE                                 0x44
/** \brief Internal target failure. */
#define VMK_SCSI_ASCQ_INTERNAL_TARGET_FAILURE_ASCQ0                          0x00
/** \brief An ascq: REPORTED LUNS DATA HAS CHANGED. */
#define VMK_SCSI_ASC_CHANGED_ASCQ_REPORTED_LUNS_DATA_CHANGED                 0x0e
/** \brief During persistent reservations. */
#define VMK_SCSI_ASC_INSUFFICIENT_REGISTRATION_RESOURCES                     0x55

/*
 * Inquiry data.
 */

/* \brief Standard INQUIRY data layout. */
#define VMK_SCSI_INQUIRY_DATA_LEN      255

/** \brief Byte offset of vendor name in SCSI inquiry. */
#define VMK_SCSI_INQUIRY_VENDOR_OFFSET 8
/** \brief Length of vendor name in SCSI inquiry (w/o terminating NUL). */
#define VMK_SCSI_INQUIRY_VENDOR_LENGTH 8
/** \brief Byte offset of model name in SCSI inquiry. */
#define VMK_SCSI_INQUIRY_MODEL_OFFSET 16
 /** \brief Length of model name in SCSI inquiry (w/o terminating NUL). */
#define VMK_SCSI_INQUIRY_MODEL_LENGTH 16
/** \brief Byte off of revision string in SCSI inquiry. */
#define VMK_SCSI_INQUIRY_REVISION_OFFSET 32
/** \brief Length of revision string in SCSI inquiry (w/o terminating NUL). */
#define VMK_SCSI_INQUIRY_REVISION_LENGTH 4

#define VMK_SCSI_PAGE0_INQUIRY_DATA_LEN   255
#define VMK_SCSI_PAGE80_INQUIRY_DATA_LEN  255

/* Inquiry page 0x83: Identifier Type. */
#define VMK_SCSI_IDENTIFIERTYPE_VENDOR_SPEC    0x0
#define VMK_SCSI_IDENTIFIERTYPE_T10            0x1
#define VMK_SCSI_IDENTIFIERTYPE_EUI            0x2
#define VMK_SCSI_IDENTIFIERTYPE_NAA            0x3
#define VMK_SCSI_IDENTIFIERTYPE_RTPI           0x4
#define VMK_SCSI_IDENTIFIERTYPE_TPG            0x5
#define VMK_SCSI_IDENTIFIERTYPE_LUG            0x6
#define VMK_SCSI_IDENTIFIERTYPE_MD5            0x7
#define VMK_SCSI_IDENTIFIERTYPE_SNS            0x8
#define VMK_SCSI_IDENTIFIERTYPE_RESERVED       0x9
#define VMK_SCSI_IDENTIFIERTYPE_MAX            VMK_SCSI_IDENTIFIERTYPE_RESERVED

/* Inquiry page 0x83: UUID Entity. */
#define VMK_SCSI_ASSOCIATION_LUN            0x0
#define VMK_SCSI_ASSOCIATION_TARGET_PORT    0x1
#define VMK_SCSI_ASSOCIATION_TARGET_DEVICE  0x2
#define VMK_SCSI_ASSOCIATION_RESERVED       0x3

/* Persistent Reserve Out Service Actions. */
#define VMK_SCSI_REGISTER                         0x0
#define VMK_SCSI_PRESERVE                         0x1
#define VMK_SCSI_PRELEASE                         0x2
#define VMK_SCSI_CLEAR                            0x3
#define VMK_SCSI_PREEMPT                          0x4
#define VMK_SCSI_PREEMPT_AND_ABORT                0x5
#define VMK_SCSI_REGISTER_AND_IGNORE_EXISTING_KEY 0x6
#define VMK_SCSI_REGISTER_AND_MOVE                0x7

/* Persistent Reservation Type Codes. */
#define VMK_SCSI_WRITE_EXCL                       0x1
#define VMK_SCSI_EXCL_ACCESS                      0x3
#define VMK_SCSI_WRITE_EXCL_REG_ONLY              0x5
#define VMK_SCSI_EXCL_ACCESS_REG_ONLY             0x6
#define VMK_SCSI_WRITE_EXCL_ALL_REG               0x7
#define VMK_SCSI_EXCL_ACCESS_ALL_REG              0x8

/* Persistent Reservation In Service Actions. */
#define VMK_SCSI_READ_KEYS                        0x0
#define VMK_SCSI_READ_RESERVATION                 0x1
#define VMK_SCSI_REPORT_CAPABILITIES              0x2
#define VMK_SCSI_READ_FULL_STATUS                 0x3

#endif /*_VMKAPI_SCSI_CONST_H_ */
/** @} */
/** @} */
