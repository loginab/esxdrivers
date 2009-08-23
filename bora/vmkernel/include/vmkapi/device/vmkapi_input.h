/* **********************************************************
 * Copyright 2008 - 2009 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * @VMKAPIMOD_LICENSE@
 */

/*
 ***********************************************************************
 * Input                                                          */ /**
 * \addtogroup Device
 * @{
 * \defgroup Input Human Input Device Interfaces
 *
 * Interfaces that allow to enqueue keyboard character(s) to vmkernel 
 * and forward input events to the host.
 * @{ 
 *
 ***********************************************************************
 */

#ifndef _VMKAPI_INPUT_H_
#define _VMKAPI_INPUT_H_

/** \cond never */
#define INCLUDE_ALLOW_VMKERNEL
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMKDRIVERS
#include "includeCheck.h"
/** \endcond never */

#include "base/vmkapi_types.h"
#include "base/vmkapi_status.h"


/**
 * \brief Values for special keys (beyond normal ASCII codes).
 */
enum {
   VMK_INPUT_KEY_F1 = (vmk_int8) 0x81,
   VMK_INPUT_KEY_F2,
   VMK_INPUT_KEY_F3,
   VMK_INPUT_KEY_F4,
   VMK_INPUT_KEY_F5,
   VMK_INPUT_KEY_F6,
   VMK_INPUT_KEY_F7,
   VMK_INPUT_KEY_F8,
   VMK_INPUT_KEY_F9,
   VMK_INPUT_KEY_F10,
   VMK_INPUT_KEY_F11,
   VMK_INPUT_KEY_F12,
   VMK_INPUT_KEY_SHIFT_F1,
   VMK_INPUT_KEY_SHIFT_F2,
   VMK_INPUT_KEY_SHIFT_F3,
   VMK_INPUT_KEY_SHIFT_F4,
   VMK_INPUT_KEY_SHIFT_F5,
   VMK_INPUT_KEY_SHIFT_F6,
   VMK_INPUT_KEY_SHIFT_F7,
   VMK_INPUT_KEY_SHIFT_F8,
   VMK_INPUT_KEY_SHIFT_F9,
   VMK_INPUT_KEY_SHIFT_F10,
   VMK_INPUT_KEY_SHIFT_F11,
   VMK_INPUT_KEY_SHIFT_F12,
   VMK_INPUT_KEY_CTRL_F1,
   VMK_INPUT_KEY_CTRL_F2,
   VMK_INPUT_KEY_CTRL_F3,
   VMK_INPUT_KEY_CTRL_F4,
   VMK_INPUT_KEY_CTRL_F5,
   VMK_INPUT_KEY_CTRL_F6,
   VMK_INPUT_KEY_CTRL_F7,
   VMK_INPUT_KEY_CTRL_F8,
   VMK_INPUT_KEY_CTRL_F9,
   VMK_INPUT_KEY_CTRL_F10,
   VMK_INPUT_KEY_CTRL_F11,
   VMK_INPUT_KEY_CTRL_F12,
   VMK_INPUT_KEY_CTRLSHIFT_F1,
   VMK_INPUT_KEY_CTRLSHIFT_F2,
   VMK_INPUT_KEY_CTRLSHIFT_F3,
   VMK_INPUT_KEY_CTRLSHIFT_F4,
   VMK_INPUT_KEY_CTRLSHIFT_F5,
   VMK_INPUT_KEY_CTRLSHIFT_F6,
   VMK_INPUT_KEY_CTRLSHIFT_F7,
   VMK_INPUT_KEY_CTRLSHIFT_F8,
   VMK_INPUT_KEY_CTRLSHIFT_F9,
   VMK_INPUT_KEY_CTRLSHIFT_F10,
   VMK_INPUT_KEY_CTRLSHIFT_F11,
   VMK_INPUT_KEY_CTRLSHIFT_F12,
   VMK_INPUT_KEY_HOME,
   VMK_INPUT_KEY_UP,
   VMK_INPUT_KEY_PAGEUP,
   VMK_INPUT_KEY_NUMMINUS,
   VMK_INPUT_KEY_LEFT,
   VMK_INPUT_KEY_CENTER,
   VMK_INPUT_KEY_RIGHT,
   VMK_INPUT_KEY_NUMPLUS,
   VMK_INPUT_KEY_END,
   VMK_INPUT_KEY_DOWN,
   VMK_INPUT_KEY_PAGEDOWN,
   VMK_INPUT_KEY_INSERT,
   VMK_INPUT_KEY_DELETE,
   VMK_INPUT_KEY_UNUSED1,
   VMK_INPUT_KEY_UNUSED2,
   VMK_INPUT_KEY_UNUSED3,
   VMK_INPUT_KEY_ALT_F1,
   VMK_INPUT_KEY_ALT_F2,
   VMK_INPUT_KEY_ALT_F3,
   VMK_INPUT_KEY_ALT_F4,
   VMK_INPUT_KEY_ALT_F5,
   VMK_INPUT_KEY_ALT_F6,
   VMK_INPUT_KEY_ALT_F7,
   VMK_INPUT_KEY_ALT_F8,
   VMK_INPUT_KEY_ALT_F9,
   VMK_INPUT_KEY_ALT_F10,
   VMK_INPUT_KEY_ALT_F11,
   VMK_INPUT_KEY_ALT_F12,
};

/**
 * \brief Opaque handle for a keyboard interrupt handler.
 */
typedef void *vmk_KeyboardInterruptHandle;

/*
 ***********************************************************************
 * vmk_InputPutQueue         --                                   */ /**
 *
 * \ingroup Input
 * \brief Enqueue a keyboard character to vmkernel.
 *
 *        Does nothing if vmkernel is not the audience.
 *
 * \param[in]  ch    Input character (ASCII or special).
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_InputPutQueue(int ch);

/*
 ***********************************************************************
 * vmk_InputPutsQueue --                                          */ /**
 *
 * \ingroup Input
 * \brief Enqueue multiple keyboard characters to vmkernel.
 *
 *        Does nothing if vmkernel is not the audience.
 *
 * \param[in]  cp    Input characters (ASCII or special).
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_InputPutsQueue(char *cp);

/*
 ***********************************************************************
 * vmk_InputEvent --                                              */ /**
 *
 * \ingroup Input
 * \brief Forward input event to host.
 *
 * input_event() in COS will  be called with corresponding parameters.
 * Does nothing if COS is not the audience.
 *
 * \param[in]  type     Event type.
 * \param[in]  code     Event code.
 * \param[in]  value    Event value.
 *
 ***********************************************************************
 */

VMK_ReturnStatus vmk_InputEvent(vmk_uint32 type,
   vmk_uint32 code, vmk_uint32 value);

/*
 ***********************************************************************
 * vmk_InterruptHandler --                                        */ /**
 *
 * \ingroup Input
 * \brief Interrupt handler pointer type provided to VMkernel
 *
 * \param[in] irq          Source specific IRQ info.
 * \param[in] context      Source specific context info.
 * \param[in] registers    Source specific register state.
 *
 ***********************************************************************
 */
typedef void (*vmk_InputInterruptHandler)(int irq,
                                          void *context,
                                          void *registers);

/*
 ***********************************************************************
 * vmk_RegisterInputKeyboardInterruptHandler --                   */ /**
 *
 * \ingroup Input
 * \brief Register an interrupt handler for polling an external
 *        keyboard.
 *
 * \note This function \em must be called at module load time.
 *
 * \param[in]  handler     Interrupt handler.
 * \param[in]  irq         Interrupt vector.
 * \param[in]  context     Context info.
 * \param[in]  registers   Register or other state.
 * \param[out] handle      Handle for registered keyboard interrupt
 *                         handler.
 *
 ***********************************************************************
 */
VMK_ReturnStatus
vmk_RegisterInputKeyboardInterruptHandler(vmk_InputInterruptHandler *handler,
                                          vmk_uint32 irq,
                                          void *context,
                                          void *registers,
                                          vmk_KeyboardInterruptHandle *handle);

/*
 ***********************************************************************
 * vmk_UnregisterInputKeyboardInterruptHandler --                 */ /**
 *
 * \ingroup Input
 * \brief Unregister a keyboard interrupt handler.
 *
 * \param[in] handle    Handle for registered keyboard interrupt
 *                      handler.
 *
 ***********************************************************************
 */
VMK_ReturnStatus
vmk_UnregisterInputKeyboardInterruptHandler(vmk_KeyboardInterruptHandle handle);

#endif
/** @} */
/** @} */
