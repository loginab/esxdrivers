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
 * $Id: sa_log.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _LIBSA_SA_LOG_H_
#define _LIBSA_SA_LOG_H_


void sa_log(const char *format, ...);
void sa_log_func(const char *func, const char *format, ...);
void sa_log_err(int, const char *func, const char *format, ...);

/*
 * These functions can be provided outside of libsa for those environments
 * that want to redirect them.
 */
void sa_log_output(const char *);	/* log message */
void sa_log_abort(const char *);	/* log message and abort */
void sa_log_output_exit(const char *);	/* log message and exit */

#define __SA_STRING(x)  #x

/*
 * Log message.
 */
#define SA_LOG(...) \
            do {                                                        \
                sa_log_func(__FUNCTION__, __VA_ARGS__);                 \
            } while (0)

#define SA_LOG_ERR(error, ...) \
            do {                                                        \
                sa_log_err(error, NULL, __VA_ARGS__);			\
            } while (0)

/*
 * Logging exits.
 */
#define SA_LOG_EXIT(...) \
            do {                                                        \
                sa_log_func(__FUNCTION__, __VA_ARGS__);                 \
                sa_log_func(__FUNCTION__, "exiting at %s:%d",           \
                  __FILE__, __LINE__);                                  \
                sa_log_output_exit(__FUNCTION__);           		\
            } while (0)

#define SA_LOG_ERR_EXIT(error, ...) \
            do {                                                        \
                sa_log_func(__FUNCTION__, __VA_ARGS__);                 \
                sa_log_err(error, __FUNCTION__, "exiting at %s:%d", 	\
                  __FILE__, __LINE__);                                  \
                sa_log_output_exit(__FUNCTION__);           		\
            } while (0)

/*
 * Logging options.
 */
#define SA_LOGF_TIME    0x0001      /* include timestamp in message */
#define SA_LOGF_DELTA   0x0002      /* include time since last message */

extern int sa_log_flags;            /* timestamp and other option flags */
extern int sa_log_time_delta_min;   /* minimum diff to print in millisec */
extern char *sa_log_prefix;         /* string to print before any message */

#endif /* _LIBSA_SA_LOG_H_ */
