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

#ifndef XEN_USER_H
#define XEN_USER_H

#include <xen/api/xen_common.h>
#include <xen/api/xen_user_decl.h>


/*
 * The user class.
 * 
 * A user of the system.
 */


/**
 * Free the given xen_user.  The given handle must have been allocated
 * by this library.
 */
extern void
xen_user_free(xen_user user);


typedef struct xen_user_set
{
    size_t size;
    xen_user *contents[];
} xen_user_set;

/**
 * Allocate a xen_user_set of the given size.
 */
extern xen_user_set *
xen_user_set_alloc(size_t size);

/**
 * Free the given xen_user_set.  The given set must have been allocated
 * by this library.
 */
extern void
xen_user_set_free(xen_user_set *set);


typedef struct xen_user_record
{
    xen_user handle;
    char *uuid;
    char *short_name;
    char *fullname;
} xen_user_record;

/**
 * Allocate a xen_user_record.
 */
extern xen_user_record *
xen_user_record_alloc(void);

/**
 * Free the given xen_user_record, and all referenced values.  The
 * given record must have been allocated by this library.
 */
extern void
xen_user_record_free(xen_user_record *record);


typedef struct xen_user_record_opt
{
    bool is_record;
    union
    {
        xen_user handle;
        xen_user_record *record;
    } u;
} xen_user_record_opt;

/**
 * Allocate a xen_user_record_opt.
 */
extern xen_user_record_opt *
xen_user_record_opt_alloc(void);

/**
 * Free the given xen_user_record_opt, and all referenced values.  The
 * given record_opt must have been allocated by this library.
 */
extern void
xen_user_record_opt_free(xen_user_record_opt *record_opt);


typedef struct xen_user_record_set
{
    size_t size;
    xen_user_record *contents[];
} xen_user_record_set;

/**
 * Allocate a xen_user_record_set of the given size.
 */
extern xen_user_record_set *
xen_user_record_set_alloc(size_t size);

/**
 * Free the given xen_user_record_set, and all referenced values.  The
 * given set must have been allocated by this library.
 */
extern void
xen_user_record_set_free(xen_user_record_set *set);



typedef struct xen_user_record_opt_set
{
    size_t size;
    xen_user_record_opt *contents[];
} xen_user_record_opt_set;

/**
 * Allocate a xen_user_record_opt_set of the given size.
 */
extern xen_user_record_opt_set *
xen_user_record_opt_set_alloc(size_t size);

/**
 * Free the given xen_user_record_opt_set, and all referenced values. 
 * The given set must have been allocated by this library.
 */
extern void
xen_user_record_opt_set_free(xen_user_record_opt_set *set);


/**
 * Get a record containing the current state of the given user.
 */
extern bool
xen_user_get_record(xen_session *session, xen_user_record **result, xen_user user);


/**
 * Get a reference to the user instance with the specified UUID.
 */
extern bool
xen_user_get_by_uuid(xen_session *session, xen_user *result, char *uuid);


/**
 * Create a new user instance, and return its handle.
 */
extern bool
xen_user_create(xen_session *session, xen_user *result, xen_user_record *record);


/**
 * Destroy the specified user instance.
 */
extern bool
xen_user_destroy(xen_session *session, xen_user user);


/**
 * Get the uuid field of the given user.
 */
extern bool
xen_user_get_uuid(xen_session *session, char **result, xen_user user);


/**
 * Get the short_name field of the given user.
 */
extern bool
xen_user_get_short_name(xen_session *session, char **result, xen_user user);


/**
 * Get the fullname field of the given user.
 */
extern bool
xen_user_get_fullname(xen_session *session, char **result, xen_user user);


/**
 * Set the fullname field of the given user.
 */
extern bool
xen_user_set_fullname(xen_session *session, xen_user user, char *fullname);


#endif
