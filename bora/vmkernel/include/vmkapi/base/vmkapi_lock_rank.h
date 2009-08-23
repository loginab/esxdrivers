/* **********************************************************
 * Copyright 2007 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 * These are internal spinlock ranks used to compute the
 * exported spinlock ranks. Calling modules should NOT
 * use these ranks directly but only those ranks exported
 * in vmkapi_lock.h.
 */

#ifndef _VMKAPI_SPLOCK_RANK_H_
#define _VMKAPI_SPLOCK_RANK_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMKDRIVERS
#include "includeCheck.h"
/** \endcond never */

/** \cond nodoc */

/*
 * Private definitions
 */
typedef int _vmk_SP_Rank;

#define _VMK_SP_RANK_RECURSIVE_FLAG  (0x8000)
#define _VMK_SP_RANK_UNRANKED        (0xffff)
/* Real mask */
#define _VMK_SP_RANK_NUMERIC_MASK    (_VMK_SP_RANK_RECURSIVE_FLAG - 1)
/* Special locks */
#define _VMK_SP_RANK_LOCK_STATS      (0x4000)
#define _VMK_SP_RANK_SYMBOL_LIST     (_VMK_SP_RANK_LOCK_STATS - 1)
#define _VMK_SP_RANK_LOG             (_VMK_SP_RANK_SYMBOL_LIST - 1)
#define _VMK_SP_RANK_LOG_EVENT       (_VMK_SP_RANK_LOG - 1)
#define _VMK_SP_RANK_VMKTAG          (_VMK_SP_RANK_LOG_EVENT - 1)
#define _VMK_SP_RANK_IRQ_LEAF        (_VMK_SP_RANK_VMKTAG - 1)
#define _VMK_SP_RANK_IRQ_WATCHPOINT  (_VMK_SP_RANK_IRQ_LEAF - 1)
/* To be used for IRQ locks that depend on memory/timer events */
#define _VMK_SP_RANK_IRQ_MEMTIMER    (0x3000)
/* To be used for IRQ locks that depend on worldlet services */
#define _VMK_SP_RANK_IRQ_WORLDLET    (_VMK_SP_RANK_IRQ_MEMTIMER - 1)
/* To be used for IRQ locks that depend on eventqueue/cpusched locks */
#define _VMK_SP_RANK_IRQ_BLOCK       (0x2000)
/* Lowest possible rank for IRQ locks */
#define _VMK_SP_RANK_IRQ_LOWEST      (0x1000)


/* Highest possible rank for non-IRQ locks */
/* To be used for non-IRQ locks that don't call any other non-IRQ locks */
#define _VMK_SP_RANK_HIGHEST         (_VMK_SP_RANK_IRQ_LOWEST - 1)
#define _VMK_SP_RANK_SEMAPHORE       (_VMK_SP_RANK_HIGHEST - 1)

#define _VMK_SP_RANK_LEAF            (_VMK_SP_RANK_SEMAPHORE - 1)
#define _VMK_SP_RANK_SCSI            (_VMK_SP_RANK_LEAF -  0x3)
#define _VMK_SP_RANK_SCSI_PLUGIN2    (_VMK_SP_RANK_LEAF -  0x4)
#define _VMK_SP_RANK_SCSI_PLUGIN1    (_VMK_SP_RANK_LEAF -  0x5)
#define _VMK_SP_RANK_SCSI_LOWEST     (_VMK_SP_RANK_LEAF - 0x20)
#define _VMK_SP_RANK_FDS_LOWEST      (_VMK_SP_RANK_SCSI_LOWEST)
#define _VMK_SP_RANK_FSDRIVER_LOWEST (_VMK_SP_RANK_FDS_LOWEST - 0x20)
#define _VMK_SP_RANK_NETWORK         (_VMK_SP_RANK_LEAF - 3)
#define _VMK_SP_RANK_NETWORK_LOWEST  (_VMK_SP_RANK_SCSI_LOWEST - 0x12)
#define _VMK_SP_RANK_NETWORK_HIGHEST (_VMK_SP_RANK_SCSI_LOWEST - 1)
#define _VMK_SP_RANK_LOWEST          (0x0001)
#define _VMK_SP_RANK_TCPIP_HIGHEST   (_VMK_SP_RANK_NETWORK_HIGHEST + 0xa)
/** \endcond nodoc */

#endif /* _VMKAPI_SPLOCK_RANK_H_ */
