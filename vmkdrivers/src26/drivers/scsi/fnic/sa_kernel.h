/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2006 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: sa_kernel.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _LIBSA_SA_KERNEL_H_
#define _LIBSA_SA_KERNEL_H_


#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 15)
#error  libsa requires Linux version 2.6.15 or newer
#endif /* VERSION */

#define	sa_malloc(size)		kmalloc(size, GFP_ATOMIC)
#define	sa_free(addr)		kfree(addr)

static inline void sa_spin_lock_debug_set_hier(spinlock_t * sp, u_char hier)
{
}


#if defined(__VMKLNX__)
#undef for_each_present_cpu
#undef for_each_online_cpu
#define for_each_present_cpu(_x)  for (_x = 0; _x < NR_CPUS; _x++) 
#define for_each_online_cpu  for_each_present_cpu
#endif /* __VMKLNX__ */

#if defined(__VMKLNX__)
#define wait_for_completion_interruptible(_x) \
	wait_for_completion_interruptible_timeout(_x, MAX_SCHEDULE_TIMEOUT)
#endif /* __VMKLNX__ */

#if defined(__VMKLNX__) && !defined(__memcpy)
#define __memcpy memcpy
#endif /* __VMKLNX__ */

#ifndef unlikely
#define unlikely(expr) (expr)
#endif /* unlikely */
#ifndef likely
#define likely(expr) (expr)
#endif /* likely */

#endif /* _LIBSA_SA_KERNEL_H_ */
