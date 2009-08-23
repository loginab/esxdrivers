/* **********************************************************
 * Copyright 2006, 2008 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * linux_ioat.h --
 *
 *    Contains ioat structures and functions
 */

#ifndef _VMKIOAT_H_
#define _VMKIOAT_H_

#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMKDRIVERS
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#include "includeCheck.h"

#include "vmkapi.h"

typedef vmk_uintptr_t vmklnx_IoatXfer;

typedef enum {
   VMKLNX_IOAT_SUCCESS,
   VMKLNX_IOAT_IN_PROGRESS,
   VMKLNX_IOAT_ERROR,
} vmklnx_IoatStatus;

typedef struct vmklnx_IoatStats {
   vmk_uint64 memcpyCount;
   vmk_uint64 memcpyBytes;
   vmk_uint64 flushCount;
   vmk_uint64 completeCount;
} vmklnx_IoatStats;

typedef struct vmklnx_IoatContext {
   vmk_Spinlock lock;
   
   /* Is this context free for use? */
   vmk_Bool free;
   
   /* Implementation specific data for the channel */
   void  *channel;
   vmklnx_IoatStats stats;
} vmklnx_IoatContext;

vmklnx_IoatContext* vmklnx_Ioat_RequestChannel(void);
void vmklnx_Ioat_ReleaseChannel(vmklnx_IoatContext *ctx);
int vmklnx_Ioat_Memcpy(vmklnx_IoatContext *ctx, vmk_MachAddr dest,
                       vmk_MachAddr src, vmk_size_t len);
enum vmklnx_IoatStatus vmklnx_Ioat_Complete(vmklnx_IoatContext *ctx,
                                            vmklnx_IoatXfer xfer);
void vmklnx_Ioat_Flush(vmklnx_IoatContext *ctx);

#endif
