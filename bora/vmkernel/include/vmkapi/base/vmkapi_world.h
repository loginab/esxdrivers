/* **********************************************************
 * Copyright 2007 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Worlds                                                         */ /**
 * \defgroup Worlds Worlds
 *
 * Worlds are the smallest schedulable entity in the VMkernel. A world
 * can be either a kernel thread or a virtual machine.
 *
 * Once a kernel world is running on a CPU it will not be preempted
 * except by hardare interrupts, otherwise it will only yield the CPU
 * voluntarily.
 *
 *@{
 ***********************************************************************
 */

#ifndef _VMKAPI_WORLD_H_
#define _VMKAPI_WORLD_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_VMKDRIVERS
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_status.h"
#include "base/vmkapi_assert.h"
#include "base/vmkapi_lock.h"
#include "base/vmkapi_platform.h"

/**
 * \brief General category of reason for blocking.
 */
typedef enum {
   VMK_WORLD_WAIT_SCSI_PLUGIN,
   VMK_WORLD_WAIT_WORKQ,
} vmk_WorldWaitReason;

/**
 * \brief Even to block on.
 *
 * Events to block on are, by convention, always kernel virtual addresses.
 */
typedef vmk_VirtAddr vmk_WorldEventId;

/** \brief The body function of a world. */
typedef VMK_ReturnStatus (*vmk_WorldStartFunc)(void *data);

/** \brief Opaque handle for a world */
typedef vmk_int32 vmk_WorldID;

/** \brief Key to access private data. */
typedef vmk_int64 vmk_WorldPrivateInfoKey;

/** \brief Destructor for private data. */
typedef void (*vmk_WorldPrivateKeyDestructor)(vmk_AddrCookie,
                                              vmk_WorldPrivateInfoKey,
                                              vmk_WorldID);
                        
/** \brief An opaque handle for held private data. */
typedef void *vmk_WorldPrivateDataHandle;

#define VMK_INVALID_WORLD_ID ((vmk_WorldID)0)

/**
 * \brief Indication of unlimited CPU allocation (max)
 */
#define VMK_CPU_ALLOC_UNLIMITED ((vmk_uint32) -1)

/*
 ***********************************************************************
 * vmk_WorldCreate --                                             */ /**
 *
 * \ingroup Worlds
 * \brief Create and start a new World
 *
 * \warning Since this World will run in kernel mode, it cannot be
 *          pre-empted by the scheduler. It is the World's
 *          responsibility to ensure a fair usage of the CPU.
 *
 * \param[in] moduleID        Module on whose behalf the world is running.
 * \param[in] name            A string that describes the world. The name
 *                            will show up as debug information.
 * \param[in] startFunction   Function that the world begins executing
 *                            on creation.
 * \param[in] data            Argument to passed to startFunction.
 * \param[out] worldId        World ID associated with the newly
 *                            created world. May be set to NULL if
 *                            the caller does not need the World ID.
 *
 * \retval VMK_OK             World created.
 * \retval VMK_NO_MEMORY      Ran out of memory.
 * \retval VMK_DEATH_PENDING  World is in the process of dying.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldCreate(
   vmk_ModuleID moduleID,
   const char *name,
   vmk_WorldStartFunc startFunction,
   void *data,
   vmk_WorldID *worldId);

/*
 ***********************************************************************
 * vmk_WorldExit --                                               */ /**
 *
 * \ingroup Worlds
 * \brief End execution of the calling world.
 *
 * \param[in] status   Status of the world on exit.
 *
 ***********************************************************************
 */
void vmk_WorldExit(VMK_ReturnStatus status);

/*
 ***********************************************************************
 * vmk_WorldGetID --                                              */ /**
 *
 * \ingroup Worlds
 * \brief Get the ID of the current world.
 *
 * \return  WorldID of the currently running world that this
 *          call was invoked from or VMK_INVALID_WORLD_ID if
 *          this call was not invoked from a world context.
 *
 ***********************************************************************
 */
vmk_WorldID vmk_WorldGetID(
   void);

/*
 ***********************************************************************
 * vmk_WorldAssertIsSafeToBlock --                                */ /**
 *
 * \ingroup Globals
 * \brief Assert that it is OK for the caller to block.
 *
 * \note Only works in debug builds.
 *
 ***********************************************************************
 */
#ifdef VMX86_DEBUG
void vmk_WorldAssertIsSafeToBlock(
   void);
#else
static inline void vmk_WorldAssertIsSafeToBlock(
   void){}
#endif

/*
 ***********************************************************************
 * vmk_WorldWait --                                               */ /**
 *
 * \ingroup Worlds
 * \brief Deschedule a World holding an Non-IRQ spinlock until awakened.
 *
 * \note Spurious wakeups are possible.
 *
 * \note The lock should be of rank VMK_SP_RANK_IRQ_BLOCK or lower
 *       otherwise a lock rank violation will occur.
 *
 * \param[in] eventId   System wide unique identifier of the event
 *                      to sleep on.
 * \param[in] lock      Non-IRQ spinlock to release before descheduling
 *                      the world.
 * \param[in] reason    Subsystem/reason for the descheduling.
 *
 * \retval VMK_OK                Woken up by a vmk_WorldWakeup on
 *                               eventId.
 * \retval VMK_DEATH_PENDING     Woken up because the world is dying
 *                               and being reaped by the scheduler.
 * \retval VMK_WAIT_INTERRUPTED  Woken for some other reason.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldWait(
   vmk_WorldEventId eventId,
   vmk_Spinlock *lock,
   vmk_WorldWaitReason reason);
   
/*
 ***********************************************************************
 * vmk_WorldWaitIRQ --                                            */ /**
 *
 * \ingroup Worlds
 * \brief Deschedule a World holding an IRQ spinlock until awakened.
 *
 * \note Spurious wakeups are possible.
 *
 * \param[in] eventId   System wide unique identifier of the event
 *                      to sleep on.
 * \param[in] lock      IRQ spinlock to release before descheduling
 *                      the world.
 * \param[in] reason    Subsystem/reason for the descheduling.
 * \param[in] flags     IRQ flags returned by IRQ spinlock function.
 *
 * \retval VMK_OK                Woken up by a vmk_WorldWakeup on
 *                               eventId.
 * \retval VMK_DEATH_PENDING     Woken up because the world is dying and
 *                               being reaped by the scheduler.
 * \retval VMK_WAIT_INTERRUPTED  Woken for some other reason.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldWaitIRQ(
   vmk_WorldEventId eventId,
   vmk_SpinlockIRQ *lock,
   vmk_WorldWaitReason reason,
   unsigned long flags);

/*
 ***********************************************************************
 * vmk_WorldTimedWait --                                          */ /**
 *
 * \ingroup Worlds
 * \brief Deschedule a World holding an Non-IRQ Spinlock until awakened
 *        or until the specified timeout expires.
 *
 * \note Spurious wakeups are possible.
 *
 * \param[in]  eventId     System wide unique identifier of the event
 *                         to sleep on.
 * \param[in]  lock        Non-IRQ spinlock to release before
 *                         descheduling the world.
 * \param[in]  reason      Subsystem/reason for the descheduling
 * \param[in]  timeoutUs   Number of microseconds before timeout.
 * \param[out] timedOut    If non-NULL, set to TRUE if wakeup was
 *                         due to timeout expiration, FALSE otherwise.
 *
 * \retval VMK_OK                Woken up by a vmk_WorldWakeup on
 *                               eventId or by timeout expiration.
 * \retval VMK_DEATH_PENDING     Woken up because the world is dying
 *                               and being reaped by the scheduler.
 * \retval VMK_WAIT_INTERRUPTED  Woken for some other reason.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldTimedWait(
   vmk_WorldEventId eventId,
   vmk_Spinlock *lock,
   vmk_WorldWaitReason reason,
   vmk_uint64 timeoutUs,
   vmk_Bool *timedOut);

/*
 ***********************************************************************
 * vmk_WorldTimedWaitIRQ --                                       */ /**
 *
 * \ingroup Worlds
 * \brief Deschedule a World holding an IRQ Spinlock until awakened
 *        or until the specified timeout expires.
 *
 * \note Spurious wakeups are possible.
 *
 * \param[in]  eventId     System wide unique identifier of the event
 *                         to sleep on.
 * \param[in]  lock        Non-IRQ spinlock to release before
 *                         descheduling the world.
 * \param[in]  reason      Subsystem/reason for the descheduling
 * \param[in]  timeoutUs   Number of microseconds before timeout.
 * \param[in]  flags       IRQ flags returned by IRQ spinlock function.
 * \param[out] timedOut    If non-NULL, set to TRUE if wakeup was
 *                         due to timeout expiration, FALSE otherwise.
 *
 * \retval VMK_OK                Woken up by a vmk_WorldWakeup on
 *                               eventId or by timeout expiration.
 * \retval VMK_DEATH_PENDING     Woken up because the world is dying
 *                               and being reaped by the scheduler.
 * \retval VMK_WAIT_INTERRUPTED  Woken for some other reason.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldTimedWaitIRQ(
   vmk_WorldEventId eventId,
   vmk_SpinlockIRQ *lock,
   vmk_WorldWaitReason reason,
   unsigned long flags,
   vmk_uint64 timeoutUs,
   vmk_Bool *timedOut);

/*
 ***********************************************************************
 * vmk_WorldWakeup --                                             */ /**
 *
 * \ingroup Worlds
 * \brief Wake up all the Worlds waiting on a event eventId
 *
 * \param[in] eventId      System wide unique identifier of the event.
 *
 * \retval VMK_OK          One or more worlds was woken up.
 * \retval VMK_NOT_FOUND   No worlds were found that wake up to eventId. 
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldWakeup(
   vmk_WorldEventId eventId);

/*
 ***********************************************************************
 * vmk_WorldSleep --                                              */ /**
 *
 * \ingroup Worlds
 * \brief Deschedule a World for the specified amount of time
 *
 * \param delayUs Duration of the snooze in microseconds
 *
 * \retval VMK_OK                Woken up by a vmk_WorldWakeup on
 *                               eventId.
 * \retval VMK_DEATH_PENDING     Woken up because the world is dying
 *                               and beeing reaped by the scheduler.
 * \retval VMK_WAIT_INTERRUPTED  Woken for some other reason.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldSleep(
   vmk_uint64 delayUs);
   
/*
 ***********************************************************************
 * vmk_WorldYield --                                              */ /**
 *
 * \ingroup Worlds
 * \brief Deschedule the calling World.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldYield(
   void);

/*
 ***********************************************************************
 * vmk_WorldPrivateKeyCreate --                                   */ /**
 *
 * \ingroup Worlds
 * \brief Creates a new key to represent world-private info.
 *
 * The destructor function will be called when a vmk_AddrCookie
 * (set up by vmk_WorldSetPrivateInfo) is destroyed.  It will be called
 * in an unspecified context, not necessarily the context in which
 * vmk_WorldSetPrivateInfo ran.  
 *
 * The destructor may not block.
 *
 * Any module using this interface must destroy the keys it creates by
 * call vmk_WorldPrivateKeyDestroy before completing its unload.
 *
 * \param[out] key         The key created.
 * \param[in]  destructor  A optional destructor function, or
 *                         NULL for no destructor.
 *
 * \retval VMK_OK The key was sucessfully returned.
 * \retval VMK_NO_MEMORY Insufficient memory to complete the request.
 * \retval VMK_NO_RESOURCES Insufficient resources to complete the request.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldPrivateKeyCreate(
    vmk_WorldPrivateInfoKey *key, vmk_WorldPrivateKeyDestructor destructor);

/*
 ************************************************************************
 * vmk_WorldPrivateKeyDestroy -                                   */ /**
 *
 * \ingroup Worlds
 * \brief Destroys a key created by vmk_WorldPrivateKeyCreate.
 *
 * Upon return, the caller is assured that the destructor (specified by
 * vmk_WorldPrivateKeyCreate) has been invoked for every data item
 * associated with the key, and that all such destructor invocations
 * have completed.
 *
 * vmk_WorldPrivateKeyDestroy may block.
 *
 * \param[in] key    The key to be destroyed.
 *
 * \retval VMK_OK          The key was sucessfully destroyed.
 * \retval VMK_NOT_FOUND   The key does not exist.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldPrivateKeyDestroy(
    vmk_WorldPrivateInfoKey key);

/*
 ***********************************************************************
 * vmk_WorldSetPrivateInfo --                                     */ /**
 *
 * \ingroup Worlds
 * \brief Associates a data pointer with the calling World and the
 *        specified key.
 *
 * This function will typically associate memory (allocated by the caller)
 * with the key, private to the currently executing world.
 *
 * This function can return VMK_NOT_FOUND if the key is concurrently
 * being destroyed by vmk_WorldPrivateKeyDestroy.
 *
 * \param[in] key    Key to access private data.
 * \param[in] info   Private data pointer to store.
 *
 * \retval VMK_OK             The pointer was sucessfully stored.
 * \retval VMK_NOT_FOUND      The given key does not exist.
 * \retval VMK_NO_MEMORY      Not enough memory to complete the operation.
 * \retval VMK_INVALID_WORLD  Caller is not running in world context.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldSetPrivateInfo(
    vmk_WorldPrivateInfoKey key,
    vmk_AddrCookie info);

/*
 ***********************************************************************
 * vmk_WorldGetPrivateInfo --                                     */ /**
 *
 * \ingroup Worlds
 * \brief Get a World-private data pointer associated with a key.
 *
 * This function can return VMK_NOT_FOUND if the key is concurrently
 * being destroyed by vmk_WorldPrivateKeyDestroy.
 *
 * \param[in]  key   The module the data is associated with.
 * \param[out] info  Retrieved private data pointer.
 *
 * \retval VMK_OK          The pointer was sucessfully retreived.
 * \retval VMK_NOT_FOUND   The specified key does not exist, or no
 *                         data is currently associated with the key
 *                         for the calling World.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldGetPrivateInfo(
    vmk_WorldPrivateInfoKey key,
    vmk_AddrCookie *info);

/*
 ***********************************************************************
 * vmk_WorldUnsetPrivateInfo --                                   */ /**
 *
 * \ingroup Worlds
 * \brief Disassociate data with a key for the currently executing
 *        world.
 *
 * This function can return VMK_NOT_FOUND if the key is concurrently
 * being destroyed by vmk_WorldPrivateKeyDestroy.
 *
 * Calling this function will provoke a callback to the destructor.
 *
 * \param[in] key    The module the data is associated with.
 *
 * \retval VMK_OK          The moduleID is now disassociated with
 *                         the World-private data pointer.
 * \retval VMK_NOT_FOUND   No data pointer is associated with the
 *                         moduleID on the calling world.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldUnsetPrivateInfo(
    vmk_WorldPrivateInfoKey key);

/*
 ***********************************************************************
 * vmk_WorldGetPrivateInfoWithHold --                             */ /**
 *
 * \ingroup Worlds
 * \brief Get a World-private data pointer associated with a
 *        key-world pair.
 *
 * This function can return VMK_NOT_FOUND if the key is concurrently
 * being destroyed by vmk_WorldPrivateKeyDestroy.
 *
 * The caller must continue to hold the handle to access the data at *info.
 *
 * The handle returned by this function must be released by
 * vmk_ReleasePrivateInfo.  As long as the caller holds this handle,
 * the private info destructor will not be called.
 *
 * \param[in]  key      World Private Data key.
 * \param[in]  id       World ID to identify the world
 * \param[out] info     A pointer to storage to hold the retreived
 *                      World-private data pointer.
 * \param[out] handle   A pointer to storage to hold a retrieved handle
 *                      on info.
 *
 * \retval VMK_OK             The pointer was sucessfully retreived.
 * \retval VMK_NOT_FOUND      No data is currently associated with the
 *                            key for the specified world.
 * \retval VMK_INVALID_WORLD  Specified world does not exist.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldGetPrivateInfoWithHold(
    vmk_WorldPrivateInfoKey key,
    vmk_WorldID id,
    vmk_AddrCookie *info,
    vmk_WorldPrivateDataHandle *handle);

/*
 ***********************************************************************
 * vmk_ReleasePrivateInfo --                                      */ /**
 *
 * \ingroup Worlds
 * \brief Release a private info datum previously acquired through
 *        vmk_WorldGetPrivateInfoWithHold.
 *
 * \param[in] handle    A handle to private info data, previously
 *                      acquired by vmk_WorldGetPrivateInfoWithHold.
 *
 *
 * \retval VMK_OK                The world was successfuly released.
 * \retval VMK_INVALID_HANDLE    Handle is invalid.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_ReleasePrivateInfo(
    vmk_WorldPrivateDataHandle handle);

/*
 ***********************************************************************
 * vmk_WorldSetAffinity --                                        */ /**
 *
 * \ingroup Worlds
 * \brief Set the PCPU affinity for the given non-running world
  
 * \note If the world is currently running and migration must take place
 *       for compliance with the given affinity, the change will not
 *       be immediate.
 *
 * \param[in] id              World ID to identify the world.
 * \param[in] affinityMask    Bitmask of physical CPUs on which the
 *                            world will be allowed to run.
 *
 * \retval VMK_NOT_FOUND   The given world ID does not exist.
 * \retval VMK_BAD_PARAM   The specified affinity is invalid for the
 *                         given world.
 *
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldSetAffinity(
    vmk_WorldID id,
    vmk_AffinityMask affinityMask);

/*
 ***********************************************************************
 * vmk_WorldGetCPUMinMax --                                       */ /**
 *
 * \ingroup Worlds
 * \brief Get the min/max CPU allocation in MHz for the running world.
  
 * \param[out] min   Minimum CPU allocation of the current running
 *                   world in MHz.
 * \param[out] max   Maximum CPU allocation of the current running
 *                   world in MHz.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldGetCPUMinMax(
    vmk_uint32 *min,
    vmk_uint32 *max);

/*
 ***********************************************************************
 * vmk_WorldSetCPUMinMax --                                       */ /**
 *
 * \ingroup Worlds
 * \brief Set the min/max CPU allocation in MHz for the running world
  
 * \note The scheduling of the current world will change based on the
 *       input parameters.
 *
 * \param[in] min    Minimum CPU allocation in MHz that will be
 *                   guaranteed.
 * \param[in] max    Maximum CPU allocation in MHz that the world can
 *                   use.
 *
 ***********************************************************************
 */
VMK_ReturnStatus vmk_WorldSetCPUMinMax(
    vmk_uint32 min,
    vmk_uint32 max);

/*
 ***********************************************************************
 * vmk_WorldsMax --                                               */ /**
 *
 * \ingroup Worlds
 * \brief Returns the maximum possible number of worlds the system
 *        will support.
  
 * \note This includes VMs and any other worlds that the system needs
 *       to execute.
 *
 ***********************************************************************
 */
vmk_uint32 vmk_WorldsMax(void);

#endif /* _VMKAPI_WORLD_H_ */
/** @} */
