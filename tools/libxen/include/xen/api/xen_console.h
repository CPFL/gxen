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

#ifndef XEN_CONSOLE_H
#define XEN_CONSOLE_H

#include <xen/api/xen_common.h>
#include <xen/api/xen_console_decl.h>
#include <xen/api/xen_console_protocol.h>
#include <xen/api/xen_string_string_map.h>
#include <xen/api/xen_vm_decl.h>


/*
 * The console class.
 * 
 * A console.
 */


/**
 * Free the given xen_console.  The given handle must have been
 * allocated by this library.
 */
extern void
xen_console_free(xen_console console);


typedef struct xen_console_set
{
    size_t size;
    xen_console *contents[];
} xen_console_set;

/**
 * Allocate a xen_console_set of the given size.
 */
extern xen_console_set *
xen_console_set_alloc(size_t size);

/**
 * Free the given xen_console_set.  The given set must have been
 * allocated by this library.
 */
extern void
xen_console_set_free(xen_console_set *set);


typedef struct xen_console_record
{
    xen_console handle;
    char *uuid;
    enum xen_console_protocol protocol;
    char *location;
    struct xen_vm_record_opt *vm;
    xen_string_string_map *other_config;
} xen_console_record;

/**
 * Allocate a xen_console_record.
 */
extern xen_console_record *
xen_console_record_alloc(void);

/**
 * Free the given xen_console_record, and all referenced values.  The
 * given record must have been allocated by this library.
 */
extern void
xen_console_record_free(xen_console_record *record);


typedef struct xen_console_record_opt
{
    bool is_record;
    union
    {
        xen_console handle;
        xen_console_record *record;
    } u;
} xen_console_record_opt;

/**
 * Allocate a xen_console_record_opt.
 */
extern xen_console_record_opt *
xen_console_record_opt_alloc(void);

/**
 * Free the given xen_console_record_opt, and all referenced values. 
 * The given record_opt must have been allocated by this library.
 */
extern void
xen_console_record_opt_free(xen_console_record_opt *record_opt);


typedef struct xen_console_record_set
{
    size_t size;
    xen_console_record *contents[];
} xen_console_record_set;

/**
 * Allocate a xen_console_record_set of the given size.
 */
extern xen_console_record_set *
xen_console_record_set_alloc(size_t size);

/**
 * Free the given xen_console_record_set, and all referenced values. 
 * The given set must have been allocated by this library.
 */
extern void
xen_console_record_set_free(xen_console_record_set *set);



typedef struct xen_console_record_opt_set
{
    size_t size;
    xen_console_record_opt *contents[];
} xen_console_record_opt_set;

/**
 * Allocate a xen_console_record_opt_set of the given size.
 */
extern xen_console_record_opt_set *
xen_console_record_opt_set_alloc(size_t size);

/**
 * Free the given xen_console_record_opt_set, and all referenced
 * values.  The given set must have been allocated by this library.
 */
extern void
xen_console_record_opt_set_free(xen_console_record_opt_set *set);


/**
 * Get a record containing the current state of the given console.
 */
extern bool
xen_console_get_record(xen_session *session, xen_console_record **result, xen_console console);


/**
 * Get a reference to the console instance with the specified UUID.
 */
extern bool
xen_console_get_by_uuid(xen_session *session, xen_console *result, char *uuid);


/**
 * Create a new console instance, and return its handle.
 */
extern bool
xen_console_create(xen_session *session, xen_console *result, xen_console_record *record);


/**
 * Destroy the specified console instance.
 */
extern bool
xen_console_destroy(xen_session *session, xen_console console);


/**
 * Get the uuid field of the given console.
 */
extern bool
xen_console_get_uuid(xen_session *session, char **result, xen_console console);


/**
 * Get the protocol field of the given console.
 */
extern bool
xen_console_get_protocol(xen_session *session, enum xen_console_protocol *result, xen_console console);


/**
 * Get the location field of the given console.
 */
extern bool
xen_console_get_location(xen_session *session, char **result, xen_console console);


/**
 * Get the VM field of the given console.
 */
extern bool
xen_console_get_vm(xen_session *session, xen_vm *result, xen_console console);


/**
 * Get the other_config field of the given console.
 */
extern bool
xen_console_get_other_config(xen_session *session, xen_string_string_map **result, xen_console console);


/**
 * Set the other_config field of the given console.
 */
extern bool
xen_console_set_other_config(xen_session *session, xen_console console, xen_string_string_map *other_config);


/**
 * Add the given key-value pair to the other_config field of the given
 * console.
 */
extern bool
xen_console_add_to_other_config(xen_session *session, xen_console console, char *key, char *value);


/**
 * Remove the given key and its corresponding value from the
 * other_config field of the given console.  If the key is not in that Map,
 * then do nothing.
 */
extern bool
xen_console_remove_from_other_config(xen_session *session, xen_console console, char *key);


/**
 * Return a list of all the consoles known to the system.
 */
extern bool
xen_console_get_all(xen_session *session, struct xen_console_set **result);


#endif
