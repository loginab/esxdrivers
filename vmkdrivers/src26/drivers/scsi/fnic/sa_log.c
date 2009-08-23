/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2006, 2007 Nuova Systems, Inc.  All rights reserved.
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
 * $Id: sa_log.c 18557 2008-09-14 22:36:38Z jre $
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "sa_kernel.h"


#include "sa_log.h"


/*
 * Size of on-stack line buffers.
 * These shouldn't be to large for a kernel stack frame.
 */
#define SA_LOG_BUF_LEN  200	/* on-stack line buffer size */

/*
 * log with a variable argument list.
 */
static void sa_log_va(const char *func, const char *format, va_list arg)
{
	size_t len;
	size_t flen;
	int add_newline;
	char sa_buf[SA_LOG_BUF_LEN];
	char *bp;

	/*
	 * If the caller didn't provide a newline at the end, we will.
	 */
	len = strlen(format);
	add_newline = 0;
	if (!len || format[len - 1] != '\n')
		add_newline = 1;
	bp = sa_buf;
	len = sizeof(sa_buf);
	if (func) {
		flen = snprintf(bp, len, "%s: ", func);
		len -= flen;
		bp += flen;
	}
	flen = vsnprintf(bp, len, format, arg);
	if (add_newline && flen < len) {
		bp += flen;
		*bp++ = '\n';
		*bp = '\0';
	}
	sa_log_output(sa_buf);
}

/*
 * log
 */
void sa_log(const char *format, ...)
{
	va_list arg;

	va_start(arg, format);
	sa_log_va(NULL, format, arg);
	va_end(arg);
}

/*
 * log with function name.
 */
void sa_log_func(const char *func, const char *format, ...)
{
	va_list arg;

	va_start(arg, format);
	sa_log_va(func, format, arg);
	va_end(arg);
}
EXPORT_SYMBOL(sa_log_func);

#if defined(__KERNEL__) || !defined(linux)
static char *strerror_r(int err, char *buf, size_t len)
{
	return "";
}
#endif /* __KERNEL__ */

/*
 * log with error number.
 */
void sa_log_err(int error, const char *func, const char *format, ...)
{
	va_list arg;
	char buf[SA_LOG_BUF_LEN];
	char *errmsg;

	/*
	 * strerror_r() comes with two incompatible prototypes.
	 * One returns an integer, (0, or -1 on error), and 
	 * the other returns the string to use, and doesn't
	 * necessarily use the buffer.
	 * Since the program may link with either version,
	 * this kludge allows us to get the message even if the
	 * non-GNU version is provided by the library.
	 */
	buf[0] = '\0';
	errmsg = strerror_r(error, buf, sizeof(buf));
	if (errmsg == (char *)-1) {
		errmsg = "(unknown)";
	} else if (errmsg == NULL) {
		errmsg = buf;
	}
	if (func) {
		sa_log("%s: error %d: %s", func, error, errmsg);
	} else {
		sa_log("error %d: %s", error, errmsg);
	}
	va_start(arg, format);
	sa_log_va(func, format, arg);
	va_end(arg);
}
