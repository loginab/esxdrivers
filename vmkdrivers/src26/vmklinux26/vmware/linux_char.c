/* **********************************************************
 * Copyright 1998, 2007-2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * linux_char.c --
 *
 *      Linux character device emulation. Provides un/register_chrdev()
 *      functions and an ioctl dispatcher.
 */

#include <linux/module.h>
#include <linux/poll.h>
#include <linux/smp_lock.h>

#include "vmkapi.h"
#include "linux_stubs.h"
#include "linux/miscdevice.h"
#include "vmklinux26_dist.h"

#define VMKLNX_LOG_HANDLE LinChar
#include "vmklinux26_log.h"

/*
 * XXX: Should see if we can avoid having
 * a fixed size array for linuxCharDev since
 * ESX does not have that many char devices.
 */ 
#define MAX_CHRDEV 255

typedef struct LinuxCharDev {
   vmk_ModuleID modID;
   const struct file_operations *ops;
} LinuxCharDev;

// Protected from races by the vmk_CharDev framework
static LinuxCharDev linuxCharDev[MAX_CHRDEV];
static LinuxCharDev linuxMiscDev[MISC_DYNAMIC_MINOR];


/*
 *-----------------------------------------------------------------------------
 *
 * LinuxCharSelectDev --
 *
 *      Utility function for sececting which array a char dev is on.
 *      
 * Results:
 *      Pointer to device.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

static inline LinuxCharDev *
LinuxCharSelectDev(int major, int minor)
{
   return major == MISC_MAJOR ? &linuxMiscDev[minor] : &linuxCharDev[major];
}


/*
 *-----------------------------------------------------------------------------
 *
 * LinuxCharOpen --
 *
 *      Open a vmkernel driver char device. 
 *      
 * Results:
 *      Driver return value wrapped in VMK_ReturnStatus. 
 *
 * Side effects:
 *      Calls driver open().
 *
 *-----------------------------------------------------------------------------
 */

VMK_ReturnStatus
LinuxCharOpen(vmk_CharDevFdAttr *attr) 
{
   LinuxCharDev *cdev;
   struct inode *inode;
   struct file  *file;
   VMK_ReturnStatus status;
   int ret;

   //Check vmkapi chardev open and Linux open flags compatibility on compile
   VMK_ASSERT_ON_COMPILE(VMK_CHARDEV_OFLAG_RDONLY == O_RDONLY);
   VMK_ASSERT_ON_COMPILE(VMK_CHARDEV_OFLAG_WRONLY  == O_WRONLY);
   VMK_ASSERT_ON_COMPILE(VMK_CHARDEV_OFLAG_RDWR == O_RDWR);
   VMK_ASSERT_ON_COMPILE(VMK_CHARDEV_OFLAG_RDWR_MASK == O_ACCMODE);
   VMK_ASSERT_ON_COMPILE(VMK_CHARDEV_OFLAG_EXCLUSIVE  == O_EXCL);
   VMK_ASSERT_ON_COMPILE(VMK_CHARDEV_OFLAG_APPEND  == O_APPEND);
   VMK_ASSERT_ON_COMPILE(VMK_CHARDEV_OFLAG_NONBLOCK  == O_NONBLOCK);
   VMK_ASSERT_ON_COMPILE(VMK_CHARDEV_OFLAG_SYNC == O_SYNC);
   VMK_ASSERT_ON_COMPILE(VMK_CHARDEV_OFLAG_DIRECT  == O_DIRECT);

   VMKLNX_DEBUG(2, "M=%d m=%d flags=0x%x", 
                   attr->major, attr->minor, attr->openFlags);

   cdev = LinuxCharSelectDev(attr->major, attr->minor);

   if (cdev->ops->open == NULL) {
      return VMK_OK;
   }

   VMK_ASSERT(vmk_ModuleGetHeapID(cdev->modID) != VMK_INVALID_HEAP_ID);
   inode = vmk_HeapAlloc(vmk_ModuleGetHeapID(cdev->modID),
			 sizeof(*inode) + sizeof(*file));
   if (inode == NULL) {
      return VMK_NO_MEMORY;
   }

   file = (struct file *) (inode + 1);

   inode->i_rdev = MKDEV((attr->major & 0xff), (attr->minor & 0xff));
   file->f_flags = attr->openFlags;

   VMKAPI_MODULE_CALL(cdev->modID, ret, cdev->ops->open, inode, file);
   attr->clientData = file->private_data;

   status = vmklnx_errno_to_vmk_return_status(ret);

   if (inode != NULL) {
      vmk_HeapFree(vmk_ModuleGetHeapID(cdev->modID), inode);
   }

   return status;
}

/*
 *-----------------------------------------------------------------------------
 *
 * LinuxCharClose --
 *
 *      Close a vmkernel driver char device. 
 *      
 * Results:
 *      Driver return value wrapped in VMK_ReturnStatus. 
 *
 * Side effects:
 *      Calls driver close(). 
 *
 *-----------------------------------------------------------------------------
 */

VMK_ReturnStatus
LinuxCharClose(vmk_CharDevFdAttr *attr)
{
   LinuxCharDev *cdev;
   struct inode *inode;
   struct file *file;
   int ret;
   VMK_ReturnStatus status;

   VMKLNX_DEBUG(2, "M=%d m=%d flags=0x%x", 
                   attr->major, attr->minor, attr->openFlags);

   cdev = LinuxCharSelectDev(attr->major, attr->minor);

   if (cdev->ops->release == NULL) {
      return VMK_OK;
   }

   VMK_ASSERT(vmk_ModuleGetHeapID(cdev->modID) != VMK_INVALID_HEAP_ID);
   inode = vmk_HeapAlloc(vmk_ModuleGetHeapID(cdev->modID),
			 sizeof(*inode) + sizeof(*file));
   if (inode == NULL) {
      return VMK_NO_MEMORY;
   }
   memset(inode, 0, sizeof(*inode) + sizeof(*file));

   file = (struct file *) (inode + 1);

   inode->i_rdev = MKDEV((attr->major & 0xff), (attr->minor & 0xff));
   file->f_flags = attr->openFlags;
   file->private_data = attr->clientData;

   VMKAPI_MODULE_CALL(cdev->modID, ret, cdev->ops->release, inode, file);

   status =  vmklnx_errno_to_vmk_return_status(ret);

   if (inode != NULL) {
      vmk_HeapFree(vmk_ModuleGetHeapID(cdev->modID), inode);
   }

   return status;
}

/*
 *-----------------------------------------------------------------------------
 *
 * LinuxCharIoctl --
 *
 *      Ioctl dispatcher. 
 *
 * Results:
 *      VMK_OK - called driver ioctl successfully. (Driver error in "result.") 
 *      VMK_NOT_FOUND - no matching char device.
 *      VMK_NOT_SUPPORTED - no ioctl handler for this device.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

VMK_ReturnStatus
LinuxCharIoctl(vmk_CharDevFdAttr *attr, unsigned int cmd,
               vmk_uintptr_t userData,
               vmk_IoctlCallerSize callerSize, vmk_int32 *result)
{
   LinuxCharDev *cdev;
   struct inode *inode;
   struct file *file;
   struct dentry d_entry;
   int done;

   VMKLNX_DEBUG(2, "M=%d m=%d flags=0x%x cmd=0x%x uargs=0x%lx",
                   attr->major, attr->minor, attr->openFlags, 
                   cmd, (unsigned long)userData);

   cdev = LinuxCharSelectDev(attr->major, attr->minor);
   
   if (cdev->ops->compat_ioctl == NULL && 
       cdev->ops->unlocked_ioctl == NULL &&
       cdev->ops->ioctl == NULL) {
      return VMK_NOT_IMPLEMENTED;
   }

   /*
    * if we have a 64-bit caller, one of unlocked_ioctl() or ioctl()
    * should be defined.
    */

   if (cdev->ops->unlocked_ioctl == NULL &&
       cdev->ops->ioctl == NULL &&
       callerSize == VMK_IOCTL_CALLER_64) {
      return VMK_NOT_IMPLEMENTED;
   }

   VMK_ASSERT(vmk_ModuleGetHeapID(cdev->modID) != VMK_INVALID_HEAP_ID);
   inode = vmk_HeapAlloc(vmk_ModuleGetHeapID(cdev->modID),
                         sizeof(*inode) + sizeof(*file));
   if (inode == NULL) {
      return VMK_NO_MEMORY;
   }

   file = (struct file *) (inode + 1);

   memset(inode, 0, sizeof(struct inode));
   inode->i_rdev = MKDEV((attr->major & 0xff), (attr->minor & 0xff));
   memset(file, 0, sizeof(struct file));
   /* over-loading the pid field for major/minor device number. */
   LinuxChar_MajorMinorToPID(attr->major, attr->minor, &file->f_owner.pid);
   file->f_flags = attr->openFlags;
   file->private_data = attr->clientData;
   memset(&d_entry, 0, sizeof(struct dentry));
   d_entry.d_inode = (struct inode*) inode;
   file->f_dentry = &d_entry;

   done = 0;
   if (callerSize == VMK_IOCTL_CALLER_32 && cdev->ops->compat_ioctl) {
      VMKAPI_MODULE_CALL(cdev->modID, *result, cdev->ops->compat_ioctl,
                 file, cmd, userData);
      if (*result != -ENOIOCTLCMD) {
         done = 1;
      }
   }
   if (!done && cdev->ops->unlocked_ioctl) {
      VMKAPI_MODULE_CALL(cdev->modID, *result, cdev->ops->unlocked_ioctl,
                         file, cmd, userData);
      if (*result != -ENOIOCTLCMD) {
         done = 1;
      }
   }
   if (!done && cdev->ops->ioctl) {
      lock_kernel();
      VMKAPI_MODULE_CALL(cdev->modID, *result, cdev->ops->ioctl,
                         inode, file, cmd, userData);
      unlock_kernel();
   }

   attr->clientData = file->private_data;

   vmk_HeapFree(vmk_ModuleGetHeapID(cdev->modID), inode);

   return VMK_OK;
}

/*
 *-----------------------------------------------------------------------------
 *
 * LinuxChar_Fasync --
 *
 *      Call the faysnc handler of a vmkernel driver char device
 *      
 * Results:
 *      VMK_NOT_FOUND - no such device,
 *      VMK_OK - even if no fasync() handler,
 *      VMK_NO_MEMORY - cannot allocate memory to fake the file struct.
 *      Driver return value wrapped in VMK_ReturnStatus. 
 *
 * Side effects:
 *      Calls driver fasync(). 
 *
 *-----------------------------------------------------------------------------
 */

VMK_ReturnStatus 
LinuxCharFasync(vmk_CharDevFdAttr *attr)
{
   LinuxCharDev *cdev;
   struct file *file;
   int ret;

   VMKLNX_DEBUG(2, "M=%d m=%d flags=0x%x", 
                   attr->major, attr->minor, attr->openFlags);

   cdev = LinuxCharSelectDev(attr->major, attr->minor);

   if (cdev->ops->fasync == NULL) {
      return VMK_NOT_IMPLEMENTED;
   }

   VMK_ASSERT(vmk_ModuleGetHeapID(cdev->modID) != VMK_INVALID_HEAP_ID);
   file = vmk_HeapAlloc(vmk_ModuleGetHeapID(cdev->modID), sizeof(*file));
   if (file == NULL) {
      return VMK_NO_MEMORY;
   }

   /* over-loading the pid field for major/minor device number. */
   memset(file, 0, sizeof(struct file));
   LinuxChar_MajorMinorToPID(attr->major, attr->minor, &file->f_owner.pid);
   file->private_data = attr->clientData;
   file->f_flags = attr->openFlags; 

   VMKAPI_MODULE_CALL(cdev->modID, ret, cdev->ops->fasync, -1, file, -1);

   attr->clientData = file->private_data;

   vmk_HeapFree(vmk_ModuleGetHeapID(cdev->modID), file);

   return vmklnx_errno_to_vmk_return_status(ret);
}


/*
 *----------------------------------------------------------------------------
 *
 *  LinuxCharPoll --
 *
 *    Invoke the device's poll handler if it has been declared.
 *
 *  Results:
 *    VMK_ReturnStatus
 *
 *  Side effects:
 *    None
 *
 *----------------------------------------------------------------------------
 */

VMK_ReturnStatus
LinuxCharPoll(vmk_CharDevFdAttr *attr, void *pollCtx, unsigned *pollMask)
{
   LinuxCharDev *cdev;
   struct file *file;

   VMKLNX_DEBUG(2, "M=%d m=%d flags=0x%x", 
                   attr->major, attr->minor, attr->openFlags);

   cdev = LinuxCharSelectDev(attr->major, attr->minor);

   if (cdev->ops->poll == NULL) {
      /*
       * From O'Reilly's "Linux Device Drivers", Chapter 3:
       * "If a driver leaves its poll method NULL, the device is assumed to
       * be both readable and writable without blocking."
       */
      *pollMask = (POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM);
      return VMK_OK;
   }

   VMK_ASSERT(vmk_ModuleGetHeapID(cdev->modID) != VMK_INVALID_HEAP_ID);
   file = vmk_HeapAlloc(vmk_ModuleGetHeapID(cdev->modID), sizeof(*file));
   if (file == NULL) {
      return VMK_NO_MEMORY;
   }


   /* over-loading the pid field for major/minor device number. */
   memset(file, 0, sizeof(struct file));
   LinuxChar_MajorMinorToPID(attr->major, attr->minor, &file->f_owner.pid);
   file->private_data = attr->clientData;
   file->f_flags = attr->openFlags; 

   VMKAPI_MODULE_CALL(cdev->modID, *pollMask, cdev->ops->poll, file, pollCtx);

   attr->clientData = file->private_data;

   vmk_HeapFree(vmk_ModuleGetHeapID(cdev->modID), file);

   return VMK_OK;
}

/*
 *-----------------------------------------------------------------------------
 *
 * LinuxCharRead --
 *
 *      Char device read dispatcher.  Reads "len" bytes into the buffer.
 *      XXX The buffer is assumed to be large enough
 *
 * Results:
 *      VMK_OK - called driver read successfully. (Driver error in TBD) 
 *      VMK_NOT_FOUND - no matching char device.
 *      VMK_NOT_SUPPORTED - no read handler for this device.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

VMK_ReturnStatus
LinuxCharRead(vmk_CharDevFdAttr *attr, char *buffer,
              vmk_size_t nbytes, vmk_loff_t *ppos, vmk_ssize_t *nread)
{
   LinuxCharDev *cdev;
   struct file *file;
   VMK_ReturnStatus status = VMK_OK;

   VMKLNX_DEBUG(1, "M=%d m=%d flags=0x%x read %lu bytes at offset 0x%llx",
                   attr->major, attr->minor, attr->openFlags, nbytes, *ppos);

   cdev = LinuxCharSelectDev(attr->major, attr->minor);

   if (cdev->ops->read == NULL) {
      return VMK_NOT_IMPLEMENTED;
   }

   VMK_ASSERT(vmk_ModuleGetHeapID(cdev->modID) != VMK_INVALID_HEAP_ID);
   file = vmk_HeapAlloc(vmk_ModuleGetHeapID(cdev->modID), sizeof(*file));
   if (file == NULL) {
      return VMK_NO_MEMORY;
   }


   /* over-loading the pid field for major/minor device number. */
   memset(file, 0, sizeof(struct file));
   LinuxChar_MajorMinorToPID(attr->major, attr->minor, &file->f_owner.pid);
   file->private_data = attr->clientData;
   file->f_flags = attr->openFlags; 

   VMKAPI_MODULE_CALL(cdev->modID, *nread, cdev->ops->read, file,
                           buffer, nbytes, ppos);

   if (*nread < 0) {
      if (!(attr->openFlags & VMK_CHARDEV_OFLAG_NONBLOCK) 
          && *nread != -EAGAIN) {
         VMKLNX_WARN("M=%d m=%d flags=%#x read %lu bytes at offset %#llx failed (%ld)",
              attr->major, attr->minor, attr->openFlags, nbytes, *ppos, *nread);
      } else {
         VMKLNX_DEBUG(2, "M=%d m=%d flags=%#x non-blocking read %lu bytes at offset %#llx failed (%ld)",
              attr->major, attr->minor, attr->openFlags, nbytes, *ppos, *nread);
      }
      status =  vmklnx_errno_to_vmk_return_status(*nread);
   } else {
      VMK_ASSERT(*nread <= nbytes);
   }

   attr->clientData = file->private_data;

   vmk_HeapFree(vmk_ModuleGetHeapID(cdev->modID), file);

   return status;
}

/*
 *-----------------------------------------------------------------------------
 *
 * LinuxCharWrite --
 *
 *      Char device read dispatcher.  Write "len" bytes from the buffer.
 *      XXX The buffer is assumed to be large enough
 *
 * Results:
 *      VMK_OK - called driver read successfully. (Driver error in TBD) 
 *      VMK_NOT_FOUND - no matching char device.
 *      VMK_NOT_SUPPORTED - no read handler for this device.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

VMK_ReturnStatus
LinuxCharWrite(vmk_CharDevFdAttr *attr, char *buffer,
               vmk_size_t nbytes, vmk_loff_t *ppos, vmk_ssize_t *nwritten)
{
   LinuxCharDev *cdev;
   struct file *file;
   VMK_ReturnStatus status = VMK_OK;

   VMKLNX_DEBUG(1, "M=%d m=%d flags=0x%x write %lu bytes at offset 0x%llx",
                   attr->major, attr->minor, attr->openFlags, nbytes, *ppos);

   cdev = LinuxCharSelectDev(attr->major, attr->minor);

   if (cdev->ops->write == NULL) {
      return VMK_NOT_IMPLEMENTED;
   }

   VMK_ASSERT(vmk_ModuleGetHeapID(cdev->modID) != VMK_INVALID_HEAP_ID);
   file = vmk_HeapAlloc(vmk_ModuleGetHeapID(cdev->modID), sizeof(*file));
   if (file == NULL) {
      return VMK_NO_MEMORY;
   }


   /* over-loading the pid field for major/minor device number. */
   memset(file, 0, sizeof(struct file));
   LinuxChar_MajorMinorToPID(attr->major, attr->minor, &file->f_owner.pid);
   file->private_data = attr->clientData;
   file->f_flags = attr->openFlags; 

   VMKAPI_MODULE_CALL(cdev->modID, *nwritten, cdev->ops->write, file,
                           buffer, nbytes, ppos);

   VMK_ASSERT(*nwritten <= (vmk_ssize_t)nbytes);
 
   if (*nwritten < 0) {
      if (!(attr->openFlags & VMK_CHARDEV_OFLAG_NONBLOCK) 
          && *nwritten != -EAGAIN) {
         VMKLNX_WARN("M=%d m=%d flags=%#x write %lu bytes at offset %#llx failed (%ld)",
              attr->major, attr->minor, attr->openFlags, nbytes, *ppos, *nwritten);
      } else {
         VMKLNX_DEBUG(2, "M=%d m=%d flags=%#x non-blocking write %lu bytes at offset %#llx failed (%ld)",
              attr->major, attr->minor, attr->openFlags, nbytes, *ppos, *nwritten);
      }
      status =  vmklnx_errno_to_vmk_return_status(*nwritten);
   } else {
      VMK_ASSERT(*nwritten <= nbytes);
   }

   attr->clientData = file->private_data;

   vmk_HeapFree(vmk_ModuleGetHeapID(cdev->modID), file);

   return status;
}

/*
 *-----------------------------------------------------------------------------
 *
 * register_chrdev --
 *
 *      Register a char device with the VMkernel & the COS
 *
 * Results:
 *      0 on success for static major request, new major number if 
 *      dynamic major request, errno on failure.
 *      
 * Side effects:
 *      Call into vmkernel and COS to register chrdev. 
 *
 *-----------------------------------------------------------------------------
 */
/**                                          
 *  register_chrdev - Register a character device     
 *  @major: major number request or 0 to let VMkernel pick a major number.
 *  @name: name of this range of devices
 *  @fops: file operations associated with devices
 *
 *  If @major == 0, this function will dynamically allocate a major and return its number.
 * 
 *  If @major > 0 this function will attempt to reserve a device with the given major number
 *  and will return 0 on success.
 *
 *  This function registers a range of 256 minor numbers, the first being 0.
 *
 */                                          
/* _VMKLNX_CODECHECK_: register_chrdev */
int 
register_chrdev(unsigned int major,           // IN: driver requested major
                const char *name,             // IN: 
                const struct file_operations *fops) // IN: file ops
{
   static vmk_CharDevOps linuxCharDevOps = {
      LinuxCharOpen,
      LinuxCharClose,
      LinuxCharIoctl,
      LinuxCharFasync,
      LinuxCharPoll,
      LinuxCharRead,
      LinuxCharWrite,
   };

   vmk_int32 assignedMajor;
   VMK_ReturnStatus status;
   vmk_ModuleID module = vmk_ModuleStackTop();

   VMKLNX_DEBUG(2, "M=%d driver=%s open=%p, close=%p, ioctl=%p, "
                   "poll=%p, read=%p, write=%p ioctl_compat=%p,", 
                   major, name, fops->open, fops->release, fops->ioctl, 
                   fops->poll, fops->read, fops->write, fops->compat_ioctl);

   status = vmk_CharDevRegister(module, major, 0, name, &linuxCharDevOps,
                                &assignedMajor);
   if (status == VMK_OK) {
      VMK_ASSERT(linuxCharDev[assignedMajor].ops == NULL);
      linuxCharDev[assignedMajor].ops = fops;
      linuxCharDev[assignedMajor].modID = module;
      return (major == 0) ? assignedMajor : 0;
   } else if (status == VMK_BUSY) {
      return -EBUSY;
   } else {
      return -EINVAL;
   }
}

/*
 *-----------------------------------------------------------------------------
 *
 * unregister_chrdev --
 *
 *      Unregister a char device with the VMkernel & the COS
 *
 * Results:
 *      0 on success, errno on failure.
 *      
 * Side effects:
 *      Call into vmkernel and COS to unregister chrdev. 
 *
 *-----------------------------------------------------------------------------
 */
/**                                          
 *  unregister_chrdev - Unregister a char device
 *  @major: major device number of device to be unregistered
 *  @name: name of device
 *                                           
 *  ESX Deviation Notes:                     
 *  In addition to kernel unregisteration this device is also unregistered
 *  with COS.
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: unregister_chrdev */
int 
unregister_chrdev(unsigned int major, 
                  const char *name)
{
   VMKLNX_DEBUG(2, "M=%d driver=%s", major, name);

   VMK_ReturnStatus status = vmk_CharDevUnregister(major, 0, name);
   if (status == VMK_OK) {
      linuxCharDev[major].ops = NULL;
      linuxCharDev[major].modID = 0;
   } else {
      return -EINVAL;
   }

   return 0;
}

/**                                          
 *  misc_register - Register a misc device with the VMkernel and COS
 *  @misc: device to register
 *
 *  misc_register attempts to register a character device with the VMkernel
 *  and COS, using the device's supplied file operations and requested
 *  minor.
 *                                           
 *  ESX Deviation Notes:
 *  On ESX, misc_register returns the assigned character-device major
 *  associated with the device (which is always MISC_MAJOR) upon a 
 *  successful registration.  For failure, a negative error code is
 *  returned.
 *  ESX does not support dynamic minors.
 *  ESX Classic supports the fasync() file operation, but ESXi does not.
 *
 *  RETURN VALUE:
 *  A negative error code on an error, the assigned major on success
 *                
 */                                          
/* _VMKLNX_CODECHECK_: misc_register */
int 
misc_register(struct miscdevice *misc)
{
   static vmk_CharDevOps linuxCharDevOps = {
      LinuxCharOpen,
      LinuxCharClose,
      LinuxCharIoctl,
      LinuxCharFasync,
      LinuxCharPoll,
   };

   vmk_int32 assignedMajor;
   VMK_ReturnStatus status;
   vmk_ModuleID module = vmk_ModuleStackTop();

   VMKLNX_DEBUG(2, "driver=%s open=%p, close=%p, ioctl=%p, compat_ioctl=%p", 
                   misc->name, misc->fops->open, misc->fops->release, 
                   misc->fops->ioctl, misc->fops->compat_ioctl);

   status = vmk_CharDevRegister(module, MISC_MAJOR, misc->minor, misc->name,
                                &linuxCharDevOps, &assignedMajor);
   if (status == VMK_OK) {
      VMK_ASSERT(linuxMiscDev[misc->minor].ops == NULL);
      linuxMiscDev[misc->minor].ops = misc->fops;
      linuxMiscDev[misc->minor].modID = module;
      return assignedMajor;
   } else if (status == VMK_BUSY) {
      return -EBUSY;
   } else {
      return -EINVAL;
   }

   return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * misc_deregister --
 *
 *      Unregister a misc device with the VMkernel & the COS
 *
 * Results:
 *      
 *      
 * Side effects:
 *      
 *
 *-----------------------------------------------------------------------------
 */
/**                                          
 *  misc_deregister - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: misc_deregister */
int 
misc_deregister(struct miscdevice *misc)
{
   VMKLNX_DEBUG(2, "m=%d driver=%s", misc->minor, misc->name);

   VMK_ReturnStatus status = vmk_CharDevUnregister(MISC_MAJOR, misc->minor,
                                                   misc->name);

   if (status == VMK_OK) {
      linuxMiscDev[misc->minor].ops = NULL;
      linuxMiscDev[misc->minor].modID = 0;
   } else {
      return -EINVAL;
   }

   return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * LinuxChar_Init --
 *
 *      Initialize the Linux emulation character driver subsystem
 *
 * Results:
 *      None
 *      
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */
void
LinuxChar_Init(void)
{
   VMKLNX_CREATE_LOG();
}

/*
 *-----------------------------------------------------------------------------
 *
 * LinuxChar_Cleanup --
 *
 *      Shutdown the Linux emulation character driver subsystem
 *
 * Results:
 *      None
 *      
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */
void
LinuxChar_Cleanup(void)
{
   VMKLNX_DESTROY_LOG();
}

/**                                          
 *  no_llseek - <1 Line Description>       
 *  @<arg1>: <first argument description>    
 *  @<arg2>: <second argument description>   
 *                                           
 *  <full description>                       
 *                                           
 *  ESX Deviation Notes:                     
 *  <Describes how ESX implementation is different from standard Linux.> 
 *                                           
 */                                          
/* _VMKLNX_CODECHECK_: no_llseek */
loff_t
no_llseek(struct file *file, loff_t offset, int origin)
{
    return -ESPIPE;
}

/*
 *----------------------------------------------------------------------------
 *
 *  LinuxChar_MajorMinorToPID --
 *
 *    Pack a character device's major and minor and store it into 
 *    the over-loaded pid field of a file structure.
 *
 *  Results:
 *    pid updated via passed parameter
 *
 *  Side effects:
 *    None
 *
 *----------------------------------------------------------------------------
 */
void LinuxChar_MajorMinorToPID(uint16_t major, uint16_t minor, int *pid)
{
   uint32_t major_minor = ((major << 16) & 0xFFFF0000) |
                          (minor & 0xFFFF);
   *pid = (int) major_minor;
}

/*
 *----------------------------------------------------------------------------
 *
 *  LinuxChar_PIDToMajorMinor --
 *
 *    Unpack a character device's major and minor from the 
 *    over-loaded pid value in a file structure.
 *
 *  Results:
 *    major and minor are updated via passed parameters
 *
 *  Side effects:
 *    None
 *
 *----------------------------------------------------------------------------
 */
void LinuxChar_PIDToMajorMinor(int pid, uint16_t *major, uint16_t 
*minor)
{
  uint32_t major_minor = pid;
  *major = (major_minor >> 16) & 0xFFFF;
  *minor = (major_minor)       & 0xFFFF;
}

