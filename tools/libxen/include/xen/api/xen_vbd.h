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

#ifndef XEN_VBD_H
#define XEN_VBD_H

#include <xen/api/xen_common.h>
#include <xen/api/xen_string_set.h>
#include <xen/api/xen_string_string_map.h>
#include <xen/api/xen_vbd_decl.h>
#include <xen/api/xen_vbd_metrics_decl.h>
#include <xen/api/xen_vbd_mode.h>
#include <xen/api/xen_vbd_type.h>
#include <xen/api/xen_vdi_decl.h>
#include <xen/api/xen_vm_decl.h>


/*
 * The VBD class.
 * 
 * A virtual block device.
 */


/**
 * Free the given xen_vbd.  The given handle must have been allocated
 * by this library.
 */
extern void
xen_vbd_free(xen_vbd vbd);


typedef struct xen_vbd_set
{
    size_t size;
    xen_vbd *contents[];
} xen_vbd_set;

/**
 * Allocate a xen_vbd_set of the given size.
 */
extern xen_vbd_set *
xen_vbd_set_alloc(size_t size);

/**
 * Free the given xen_vbd_set.  The given set must have been allocated
 * by this library.
 */
extern void
xen_vbd_set_free(xen_vbd_set *set);


typedef struct xen_vbd_record
{
    xen_vbd handle;
    char *uuid;
    struct xen_vm_record_opt *vm;
    struct xen_vdi_record_opt *vdi;
    char *device;
    bool bootable;
    enum xen_vbd_mode mode;
    enum xen_vbd_type type;
    bool currently_attached;
    int64_t status_code;
    char *status_detail;
    xen_string_string_map *runtime_properties;
    char *qos_algorithm_type;
    xen_string_string_map *qos_algorithm_params;
    struct xen_string_set *qos_supported_algorithms;
    struct xen_vbd_metrics_record_opt *metrics;
} xen_vbd_record;

/**
 * Allocate a xen_vbd_record.
 */
extern xen_vbd_record *
xen_vbd_record_alloc(void);

/**
 * Free the given xen_vbd_record, and all referenced values.  The given
 * record must have been allocated by this library.
 */
extern void
xen_vbd_record_free(xen_vbd_record *record);


typedef struct xen_vbd_record_opt
{
    bool is_record;
    union
    {
        xen_vbd handle;
        xen_vbd_record *record;
    } u;
} xen_vbd_record_opt;

/**
 * Allocate a xen_vbd_record_opt.
 */
extern xen_vbd_record_opt *
xen_vbd_record_opt_alloc(void);

/**
 * Free the given xen_vbd_record_opt, and all referenced values.  The
 * given record_opt must have been allocated by this library.
 */
extern void
xen_vbd_record_opt_free(xen_vbd_record_opt *record_opt);


typedef struct xen_vbd_record_set
{
    size_t size;
    xen_vbd_record *contents[];
} xen_vbd_record_set;

/**
 * Allocate a xen_vbd_record_set of the given size.
 */
extern xen_vbd_record_set *
xen_vbd_record_set_alloc(size_t size);

/**
 * Free the given xen_vbd_record_set, and all referenced values.  The
 * given set must have been allocated by this library.
 */
extern void
xen_vbd_record_set_free(xen_vbd_record_set *set);



typedef struct xen_vbd_record_opt_set
{
    size_t size;
    xen_vbd_record_opt *contents[];
} xen_vbd_record_opt_set;

/**
 * Allocate a xen_vbd_record_opt_set of the given size.
 */
extern xen_vbd_record_opt_set *
xen_vbd_record_opt_set_alloc(size_t size);

/**
 * Free the given xen_vbd_record_opt_set, and all referenced values. 
 * The given set must have been allocated by this library.
 */
extern void
xen_vbd_record_opt_set_free(xen_vbd_record_opt_set *set);


/**
 * Get a record containing the current state of the given VBD.
 */
extern bool
xen_vbd_get_record(xen_session *session, xen_vbd_record **result, xen_vbd vbd);


/**
 * Get a reference to the VBD instance with the specified UUID.
 */
extern bool
xen_vbd_get_by_uuid(xen_session *session, xen_vbd *result, char *uuid);


/**
 * Create a new VBD instance, and return its handle.
 */
extern bool
xen_vbd_create(xen_session *session, xen_vbd *result, xen_vbd_record *record);


/**
 * Destroy the specified VBD instance.
 */
extern bool
xen_vbd_destroy(xen_session *session, xen_vbd vbd);


/**
 * Get the uuid field of the given VBD.
 */
extern bool
xen_vbd_get_uuid(xen_session *session, char **result, xen_vbd vbd);


/**
 * Get the VM field of the given VBD.
 */
extern bool
xen_vbd_get_vm(xen_session *session, xen_vm *result, xen_vbd vbd);


/**
 * Get the VDI field of the given VBD.
 */
extern bool
xen_vbd_get_vdi(xen_session *session, xen_vdi *result, xen_vbd vbd);


/**
 * Get the device field of the given VBD.
 */
extern bool
xen_vbd_get_device(xen_session *session, char **result, xen_vbd vbd);


/**
 * Get the bootable field of the given VBD.
 */
extern bool
xen_vbd_get_bootable(xen_session *session, bool *result, xen_vbd vbd);


/**
 * Get the mode field of the given VBD.
 */
extern bool
xen_vbd_get_mode(xen_session *session, enum xen_vbd_mode *result, xen_vbd vbd);


/**
 * Get the type field of the given VBD.
 */
extern bool
xen_vbd_get_type(xen_session *session, enum xen_vbd_type *result, xen_vbd vbd);


/**
 * Get the currently_attached field of the given VBD.
 */
extern bool
xen_vbd_get_currently_attached(xen_session *session, bool *result, xen_vbd vbd);


/**
 * Get the status_code field of the given VBD.
 */
extern bool
xen_vbd_get_status_code(xen_session *session, int64_t *result, xen_vbd vbd);


/**
 * Get the status_detail field of the given VBD.
 */
extern bool
xen_vbd_get_status_detail(xen_session *session, char **result, xen_vbd vbd);


/**
 * Get the runtime_properties field of the given VBD.
 */
extern bool
xen_vbd_get_runtime_properties(xen_session *session, xen_string_string_map **result, xen_vbd vbd);


/**
 * Get the qos/algorithm_type field of the given VBD.
 */
extern bool
xen_vbd_get_qos_algorithm_type(xen_session *session, char **result, xen_vbd vbd);


/**
 * Get the qos/algorithm_params field of the given VBD.
 */
extern bool
xen_vbd_get_qos_algorithm_params(xen_session *session, xen_string_string_map **result, xen_vbd vbd);


/**
 * Get the qos/supported_algorithms field of the given VBD.
 */
extern bool
xen_vbd_get_qos_supported_algorithms(xen_session *session, struct xen_string_set **result, xen_vbd vbd);


/**
 * Get the metrics field of the given VBD.
 */
extern bool
xen_vbd_get_metrics(xen_session *session, xen_vbd_metrics *result, xen_vbd vbd);


/**
 * Set the device field of the given VBD.
 */
extern bool
xen_vbd_set_device(xen_session *session, xen_vbd vbd, char *device);


/**
 * Set the bootable field of the given VBD.
 */
extern bool
xen_vbd_set_bootable(xen_session *session, xen_vbd vbd, bool bootable);


/**
 * Set the mode field of the given VBD.
 */
extern bool
xen_vbd_set_mode(xen_session *session, xen_vbd vbd, enum xen_vbd_mode mode);


/**
 * Set the type field of the given VBD.
 */
extern bool
xen_vbd_set_type(xen_session *session, xen_vbd vbd, enum xen_vbd_type type);


/**
 * Set the qos/algorithm_type field of the given VBD.
 */
extern bool
xen_vbd_set_qos_algorithm_type(xen_session *session, xen_vbd vbd, char *algorithm_type);


/**
 * Set the qos/algorithm_params field of the given VBD.
 */
extern bool
xen_vbd_set_qos_algorithm_params(xen_session *session, xen_vbd vbd, xen_string_string_map *algorithm_params);


/**
 * Add the given key-value pair to the qos/algorithm_params field of
 * the given VBD.
 */
extern bool
xen_vbd_add_to_qos_algorithm_params(xen_session *session, xen_vbd vbd, char *key, char *value);


/**
 * Remove the given key and its corresponding value from the
 * qos/algorithm_params field of the given VBD.  If the key is not in that
 * Map, then do nothing.
 */
extern bool
xen_vbd_remove_from_qos_algorithm_params(xen_session *session, xen_vbd vbd, char *key);


/**
 * Change the media in the device for CDROM-like devices only. For
 * other devices, detach the VBD and attach a new one.
 */
extern bool
xen_vbd_media_change(xen_session *session, xen_vbd vbd, xen_vdi vdi);


/**
 * Hotplug the specified VBD, dynamically attaching it to the running
 * VM.
 */
extern bool
xen_vbd_plug(xen_session *session, xen_vbd self);


/**
 * Hot-unplug the specified VBD, dynamically unattaching it from the
 * running VM.
 */
extern bool
xen_vbd_unplug(xen_session *session, xen_vbd self);


/**
 * Return a list of all the VBDs known to the system.
 */
extern bool
xen_vbd_get_all(xen_session *session, struct xen_vbd_set **result);


#endif
