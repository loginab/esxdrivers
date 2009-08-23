/* **********************************************************
 * Copyright 1998 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Character Devices                                              */ /**
 * \defgroup CharDev Character Devices
 *
 * Interfaces that allow management of vmkernel's UNIX-like character
 * device nodes.
 *
 * @{
 ***********************************************************************
 */

#ifndef _VMKAPI_CHAR_H_
#define _VMKAPI_CHAR_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_const.h"
#include "base/vmkapi_module.h"

/**
 * \brief Semaphore rank that may be safely used by character devices.
 */
#define VMK_SEMA_RANK_CHAR VMK_SEMA_RANK_LEAF

/*
 * UNIX-style open flags supported for character devices
 */
/** \brief Read-only. */
#define VMK_CHARDEV_OFLAG_RDONLY       0x00000000

/** \brief Write-only. */
#define VMK_CHARDEV_OFLAG_WRONLY       0x00000001

/** \brief Read-write. */
#define VMK_CHARDEV_OFLAG_RDWR         0x00000002

/** \brief Mask for read/write flags. */
#define VMK_CHARDEV_OFLAG_RDWR_MASK    0x00000003

/** \brief Exclusive access. */
#define VMK_CHARDEV_OFLAG_EXCLUSIVE    0x00000080

/** \brief Append to end of file.  Always set for writes */
#define VMK_CHARDEV_OFLAG_APPEND       0x00000400

/** \brief Don't block for file operations. */
#define VMK_CHARDEV_OFLAG_NONBLOCK     0x00000800

/** \brief Synchronous file operations. */
#define VMK_CHARDEV_OFLAG_SYNC         0x00001000

/** \brief Use direct I/O. */
#define VMK_CHARDEV_OFLAG_DIRECT       0x00004000

/**
 * \ingroup CharDev
 * \brief Character device's file descriptor's attibutes.
 */
typedef struct vmk_CharDevFdAttr {
   /** \brief Character device's major number. */
   vmk_uint16	major;
   
   /** \brief Character device's minor number. */
   vmk_uint16	minor;
   
   /**
    * \brief UNIX-style file flags used when opening the device
    * from the host.
    */
   vmk_uint32	openFlags;
   
   /**
    * \brief Client data associated with the file descriptor.
    *
    * May be used by the character driver to store information
    * persistent across syscalls
    *
    * The field can be updated by the driver at any time during
    * a syscall.
    */
   void		*clientData; /* For use by the character device driver */
                             /* across open/ioctl/close calls */
} vmk_CharDevFdAttr;

/**
 * \brief Opaque poll token handle.
 */
typedef void *vmk_PollToken;

/**
 * \brief Opaque poll context handle.
 */
typedef void *vmk_PollContext;

/**
 * \ingroup CharDev
 * \brief Character device driver's entry points
 */
typedef struct vmk_CharDevOps {
   VMK_ReturnStatus (*open)(vmk_CharDevFdAttr *attr);
   VMK_ReturnStatus (*close)(vmk_CharDevFdAttr *attr);
   VMK_ReturnStatus (*ioctl)(vmk_CharDevFdAttr *attr, unsigned int cmd,
                             vmk_uintptr_t userData,
                             vmk_IoctlCallerSize callerSize,
                             vmk_int32 *result);
   VMK_ReturnStatus (*fasync)(vmk_CharDevFdAttr *attr);
   VMK_ReturnStatus (*poll)(vmk_CharDevFdAttr *attr, void *pollCtx,
                            unsigned *pollMask);
   VMK_ReturnStatus (*read)(vmk_CharDevFdAttr *attr, char *buffer,
                            vmk_size_t nbytes, vmk_loff_t *ppos,
                            vmk_ssize_t *nread);
   VMK_ReturnStatus (*write)(vmk_CharDevFdAttr *attr, char *buffer,
                             vmk_size_t nbytes, vmk_loff_t *ppos,
                             vmk_ssize_t *nwritten);
} vmk_CharDevOps;

/*
 ***********************************************************************
 * vmk_CharDevRegister --                                         */ /**
 *
 * \ingroup CharDev
 * \brief Register the specified routine to be invoked wherever an ioctl
 *        is issued in the COS to a device with the given major number.
 *
 * \param[in]  module         Module that owns the character device.
 * \param[in]  major          Major number of the device. 0 if system
 *                            will allocate the major number.
 * \param[in]  minor          Minor number of the device.
 * \param[in]  name           The name of the device.
 * \param[in]  ops            Table of the driver operations (open,
 *                            close, ioctl, fasync).
 * \param[out] assignedMajor  Returns the major assigned to the driver
 *
 * \retval VMK_BUSY           The major number is already registered
 * \retval VMK_NO_RESOURCES   No free major number
 * \retval VMK_BAD_PARAM      Module ID was invalid, or one or more
 *                            specified driver ops are NULL.
 * \retval VMK_NOT_SUPPORTED  Minor number is not valid for major number.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_CharDevRegister(
   vmk_ModuleID module,
   vmk_uint16 major,
   vmk_uint16 minor,
   const char *name,
   vmk_CharDevOps *ops,
   vmk_int32 *assignedMajor);

/*
 ***********************************************************************
 * vmk_CharDevUnregister --                                       */ /**
 *
 * \ingroup CharDev
 * \brief Unregister the ioctl callback routine from the specified major
 *        and minor number.
 *
 * \param[in] major  Major number of the device. 
 * \param[in] minor  Minor number of the device. 
 * \param[in] name   The name of the device.  
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_CharDevUnregister(
   vmk_uint16 major,
   vmk_uint16 minor,
   const char *name);

/*
 ***********************************************************************
 * vmk_CharDevWakePollers --                                      */ /**
 *
 * \ingroup CharDev
 * \brief Wake up all users waiting on a poll call with the specified
 *        token.
 *
 * \param[in] token  Context on which worlds are waiting.
 *
 ***********************************************************************
 */
void vmk_CharDevWakePollers(void *token);

/*
 ***********************************************************************
 * vmk_CharDevNotifyFasyncComplete --                             */ /**
 *
 * \ingroup CharDev
 * \brief Send a signal to processes that have requested notification.
 *        The device's minor number is assumed to be zero.
 *
 * \param[in] major    Major number of the device.
 *
 ***********************************************************************
 */
VMK_ReturnStatus
vmk_CharDevNotifyFasyncComplete(vmk_uint16 major);

/*
 ***********************************************************************
 * vmk_CharDevNotifyFasyncCompleteWithMinor --                    */ /**
 *
 * \ingroup CharDev
 * \brief Send a signal to processes that have requested notification,
 *        specifying a nonzero device minor.
 *
 * \param[in] major    Major number of the device.
 * \param[in] minor    Minor number of the device.
 *
 ***********************************************************************
 */
VMK_ReturnStatus
vmk_CharDevNotifyFasyncCompleteWithMinor(vmk_uint16 major, 
                                         vmk_uint16 minor);

/*
 ***********************************************************************
 * vmk_CharDevSetPollCtx  --                                      */ /**
 *
 * \ingroup CharDev
 * \brief Set the poll context of the calling world to the specified
 *        context.
 *
 * \param[in]  pollCtx  The poll context of the calling thread.
 * \param[out] token    The token to set in the poll context.
 *
 ***********************************************************************
 */
void vmk_CharDevSetPollContext(vmk_PollContext *pollCtx, vmk_PollToken *token);

#endif /* _VMKAPI_CHAR_H_ */
/** @} */
