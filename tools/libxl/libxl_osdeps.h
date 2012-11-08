/*
 * Copyright (C) 2009      Citrix Ltd.
 * Author Stefano Stabellini <stefano.stabellini@eu.citrix.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; version 2.1 only. with the special
 * exception on linking described in file LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 */

/*
 * This header must be included first, before any system headers,
 * so that _GNU_SOURCE takes effect properly.
 */

#ifndef LIBXL_OSDEP
#define LIBXL_OSDEP

#define _GNU_SOURCE

#if defined(__NetBSD__) || defined(__OpenBSD__)
#include <util.h>
#elif defined(__linux__)
#include <pty.h>
#elif defined(__sun__)
#include <stropts.h>
#endif

#ifdef NEED_OWN_ASPRINTF
#include <stdarg.h>

int asprintf(char **buffer, char *fmt, ...);
int vasprintf(char **buffer, const char *fmt, va_list ap);
#endif /*NEED_OWN_ASPRINTF*/

#endif

/*
 * Local variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
