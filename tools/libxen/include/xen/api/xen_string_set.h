/*
 * Copyright (c) 2006-2007, XenSource Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */

#ifndef XEN_STRING_SET_H
#define XEN_STRING_SET_H


#include "xen_common.h"


typedef struct xen_string_set
{
    size_t size;
    char *contents[];
} xen_string_set;


/**
 * Allocate a xen_string_set of the given size.
 */
extern xen_string_set *
xen_string_set_alloc(size_t size);

/**
 * Free the given xen_string_set.  The given set must have been allocated
 * by this library.
 */
extern void
xen_string_set_free(xen_string_set *set);


#endif
