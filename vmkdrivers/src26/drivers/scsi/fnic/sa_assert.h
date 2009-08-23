/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2006 Nuova Systems, Inc. All Rights Reserved
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
 * $Id: sa_assert.h 18557 2008-09-14 22:36:38Z jre $
 */

#ifndef _LIBSA_ASSERT_H_
#define _LIBSA_ASSERT_H_

extern void assert_failed(const char *s, ...)
    __attribute__ ((format(printf, 1, 2)));

#ifndef UNLIKELY
#define UNLIKELY(_x) (_x)
#endif /* UNLIKELY */

/*
 * ASSERT macros
 *
 * ASSERT(expr) - this calls assert_failed() if expr is false.  This variant
 * is not present in production code or if DEBUG_ASSERTS is not defined.
 * Be careful not to rely on expr being evaluated.
 */
#if defined(DEBUG_ASSERTS) && !defined(__WINDOWS__)
#define ASSERT(_x) do {                                                 \
                if (UNLIKELY(!(_x))) {                                  \
                        assert_failed("ASSERT FAILED (%s) @ %s:%d\n",   \
                          "" #_x, __FILE__, __LINE__);                  \
                }                                                       \
        } while (0)
#else
#define ASSERT(_x)
#endif /* DEBUG_ASSERTS */

/*
 * ASSERT_NOTIMPL(expr) - this calls assert_failed() if expr is false.
 * The implication is that the condition is not handled by the current
 * implementation, and work should be done eventually to handle this.
 */
#define ASSERT_NOTIMPL(_x) do {                                         \
                if (UNLIKELY(!(_x))) {                                  \
                        assert_failed("ASSERT (NOT IMPL) (%s) @ %s:%d\n", \
                          "" #_x, __FILE__, __LINE__);                  \
                }                                                       \
        } while (0)

/*
 * ASSERT_NOTREACHED - this is the same as ASSERT_NOTIMPL(0).
 */
#define ASSERT_NOTREACHED do {                                          \
                assert_failed("ASSERT (NOT REACHED) @ %s:%d\n",         \
                    __FILE__, __LINE__);                                \
        } while (0)

/*
 * ASSERT_BUG(bugno, expr).  This variant is used when a bug number has
 * been assigned to any one of the other assertion failures.  It is always
 * present in code.  It gives the bug number which helps locate
 * documentation and helps prevent duplicate bug filings.
 */
#define ASSERT_BUG(_bugNr, _x) do {                                     \
                if (UNLIKELY(!(_x))) {                                  \
                        assert_failed("ASSERT (BUG %d) (%s) @ %s:%d\n", \
                            (_bugNr), #_x, __FILE__, __LINE__);         \
                }                                                       \
        } while (0)

#ifndef LIBSA_USE_DANGEROUS_ROUTINES
#define gets   DONT_USE_gets
#endif /* LIBSA_USE_DANGEROUS_ROUTINES */

#endif /* _LIBSA_ASSERT_H_ */
