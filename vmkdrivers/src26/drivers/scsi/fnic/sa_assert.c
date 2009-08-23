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
 * $Id: sa_assert.c 18557 2008-09-14 22:36:38Z jre $
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "sa_kernel.h"


#include "sa_log.h"
#include "sa_assert.h"

/*
 * Size of on-stack line buffers.
 * These shouldn't be to large for a kernel stack frame.
 */
#define SA_LOG_BUF_LEN  200	/* on-stack line buffer size */

/*
 * Assert failures.
 */
void assert_failed(const char *format, ...)
{
	va_list arg;
	char buf[SA_LOG_BUF_LEN];

	va_start(arg, format);
	vsnprintf(buf, sizeof(buf), format, arg);
	va_end(arg);
	sa_log_abort(buf);
}

EXPORT_SYMBOL(assert_failed);
