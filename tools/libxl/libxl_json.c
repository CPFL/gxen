/*
 * Copyright (C) 2011      Citrix Ltd.
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

#include "libxl_osdeps.h" /* must come before any other headers */

#include <math.h>

#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>

#include "libxl_internal.h"

/* #define DEBUG_ANSWER */

struct libxl__yajl_ctx {
    libxl__gc *gc;
    yajl_handle hand;
    libxl__json_object *head;
    libxl__json_object *current;
#ifdef DEBUG_ANSWER
    yajl_gen g;
#endif
};

#ifdef DEBUG_ANSWER
#  define DEBUG_GEN_ALLOC(ctx) \
    if ((ctx)->g == NULL) { \
        yajl_gen_config conf = { 1, "  " }; \
        (ctx)->g = yajl_gen_alloc(&conf, NULL); \
    }
#  define DEBUG_GEN_FREE(ctx) \
    if ((ctx)->g) yajl_gen_free((ctx)->g)
#  define DEBUG_GEN(ctx, type)              yajl_gen_##type(ctx->g)
#  define DEBUG_GEN_VALUE(ctx, type, value) yajl_gen_##type(ctx->g, value)
#  define DEBUG_GEN_STRING(ctx, str, n)     yajl_gen_string(ctx->g, str, n)
#  define DEBUG_GEN_NUMBER(ctx, str, n)     yajl_gen_number(ctx->g, str, n)
#  define DEBUG_GEN_REPORT(yajl_ctx) \
    do { \
        const unsigned char *buf = NULL; \
        unsigned int len = 0; \
        yajl_gen_get_buf((yajl_ctx)->g, &buf, &len); \
        LIBXL__LOG(libxl__gc_owner((yajl_ctx)->gc), \
                   LIBXL__LOG_DEBUG, "response:\n%s", buf); \
        yajl_gen_free((yajl_ctx)->g); \
        (yajl_ctx)->g = NULL; \
    } while (0)
#else
#  define DEBUG_GEN_ALLOC(ctx)                  ((void)0)
#  define DEBUG_GEN_FREE(ctx)                   ((void)0)
#  define DEBUG_GEN(ctx, type)                  ((void)0)
#  define DEBUG_GEN_VALUE(ctx, type, value)     ((void)0)
#  define DEBUG_GEN_STRING(ctx, value, lenght)  ((void)0)
#  define DEBUG_GEN_NUMBER(ctx, value, lenght)  ((void)0)
#  define DEBUG_GEN_REPORT(ctx)                 ((void)0)
#endif

/*
 * YAJL Helper
 */

yajl_gen_status libxl__yajl_gen_asciiz(yajl_gen hand, const char *str)
{
    return yajl_gen_string(hand, (const unsigned char *)str, strlen(str));
}

yajl_gen_status libxl__yajl_gen_enum(yajl_gen hand, const char *str)
{
    if (str)
        return libxl__yajl_gen_asciiz(hand, str);
    else
        return yajl_gen_null(hand);
}

/*
 * YAJL generators for builtin libxl types.
 */
yajl_gen_status libxl_defbool_gen_json(yajl_gen hand,
                                       libxl_defbool *db)
{
    return libxl__yajl_gen_asciiz(hand, libxl_defbool_to_string(*db));
}

yajl_gen_status libxl_uuid_gen_json(yajl_gen hand,
                                    libxl_uuid *uuid)
{
    char buf[LIBXL_UUID_FMTLEN+1];
    snprintf(buf, sizeof(buf), LIBXL_UUID_FMT, LIBXL_UUID_BYTES((*uuid)));
    return yajl_gen_string(hand, (const unsigned char *)buf, LIBXL_UUID_FMTLEN);
}

yajl_gen_status libxl_bitmap_gen_json(yajl_gen hand,
                                      libxl_bitmap *bitmap)
{
    yajl_gen_status s;
    int i;

    s = yajl_gen_array_open(hand);
    if (s != yajl_gen_status_ok) goto out;

    libxl_for_each_bit(i, *bitmap) {
        if (libxl_bitmap_test(bitmap, i)) {
            s = yajl_gen_integer(hand, i);
            if (s != yajl_gen_status_ok) goto out;
        }
    }
    s = yajl_gen_array_close(hand);
out:
    return s;
}

yajl_gen_status libxl_key_value_list_gen_json(yajl_gen hand,
                                              libxl_key_value_list *pkvl)
{
    libxl_key_value_list kvl = *pkvl;
    yajl_gen_status s;
    int i;

    s = yajl_gen_map_open(hand);
    if (s != yajl_gen_status_ok) goto out;

    if (!kvl) goto empty;

    for (i = 0; kvl[i] != NULL; i += 2) {
        s = libxl__yajl_gen_asciiz(hand, kvl[i]);
        if (s != yajl_gen_status_ok) goto out;
        if (kvl[i + 1])
            s = libxl__yajl_gen_asciiz(hand, kvl[i+1]);
        else
            s = yajl_gen_null(hand);
        if (s != yajl_gen_status_ok) goto out;
    }
empty:
    s = yajl_gen_map_close(hand);
out:
    return s;
}

yajl_gen_status libxl_string_list_gen_json(yajl_gen hand, libxl_string_list *pl)
{
    libxl_string_list l = *pl;
    yajl_gen_status s;
    int i;

    s = yajl_gen_array_open(hand);
    if (s != yajl_gen_status_ok) goto out;

    if (!l) goto empty;

    for (i = 0; l[i] != NULL; i++) {
        s = libxl__yajl_gen_asciiz(hand, l[i]);
        if (s != yajl_gen_status_ok) goto out;
    }
empty:
    s = yajl_gen_array_close(hand);
out:
    return s;
}

yajl_gen_status libxl_mac_gen_json(yajl_gen hand, libxl_mac *mac)
{
    char buf[LIBXL_MAC_FMTLEN+1];
    snprintf(buf, sizeof(buf), LIBXL_MAC_FMT, LIBXL_MAC_BYTES((*mac)));
    return yajl_gen_string(hand, (const unsigned char *)buf, LIBXL_MAC_FMTLEN);
}

yajl_gen_status libxl_hwcap_gen_json(yajl_gen hand,
                                     libxl_hwcap *p)
{
    yajl_gen_status s;
    int i;

    s = yajl_gen_array_open(hand);
    if (s != yajl_gen_status_ok) goto out;

    for(i=0; i<4; i++) {
        s = yajl_gen_integer(hand, (*p)[i]);
        if (s != yajl_gen_status_ok) goto out;
    }
    s = yajl_gen_array_close(hand);
out:
    return s;
}

yajl_gen_status libxl__string_gen_json(yajl_gen hand,
                                       const char *p)
{
    if (p)
        return libxl__yajl_gen_asciiz(hand, p);
    else
        return yajl_gen_null(hand);
}

/*
 * libxl__json_object helper functions
 */

static libxl__json_object *json_object_alloc(libxl__gc *gc,
                                             libxl__json_node_type type)
{
    libxl__json_object *obj;

    obj = calloc(1, sizeof (libxl__json_object));
    if (obj == NULL) {
        LIBXL__LOG_ERRNO(libxl__gc_owner(gc), LIBXL__LOG_ERROR,
                         "Failed to allocate a libxl__json_object");
        return NULL;
    }

    obj->type = type;

    if (type == JSON_MAP || type == JSON_ARRAY) {
        flexarray_t *array = flexarray_make(1, 1);
        if (array == NULL) {
            LIBXL__LOG_ERRNO(libxl__gc_owner(gc), LIBXL__LOG_ERROR,
                             "Failed to allocate a flexarray");
            free(obj);
            return NULL;
        }
        if (type == JSON_MAP)
            obj->u.map = array;
        else
            obj->u.array = array;
    }

    return obj;
}

static int json_object_append_to(libxl__gc *gc,
                                 libxl__json_object *obj,
                                 libxl__json_object *dst)
{
    assert(dst != NULL);

    switch (dst->type) {
    case JSON_MAP: {
        libxl__json_map_node *last;

        if (dst->u.map->count == 0) {
            LIBXL__LOG(libxl__gc_owner(gc), LIBXL__LOG_ERROR,
                       "Try to add a value to an empty map (with no key)");
            return -1;
        }
        flexarray_get(dst->u.map, dst->u.map->count - 1, (void**)&last);
        last->obj = obj;
        break;
    }
    case JSON_ARRAY:
        if (flexarray_append(dst->u.array, obj) == 2) {
            LIBXL__LOG_ERRNO(libxl__gc_owner(gc), LIBXL__LOG_ERROR,
                             "Failed to grow a flexarray");
            return -1;
        }
        break;
    default:
        LIBXL__LOG(libxl__gc_owner(gc), LIBXL__LOG_ERROR,
                   "Try append an object is not a map/array (%i)\n",
                   dst->type);
        return -1;
    }

    obj->parent = dst;
    return 0;
}

void libxl__json_object_free(libxl__gc *gc, libxl__json_object *obj)
{
    int index = 0;

    if (obj == NULL)
        return;
    switch (obj->type) {
    case JSON_STRING:
    case JSON_NUMBER:
        free(obj->u.string);
        break;
    case JSON_MAP: {
        libxl__json_map_node *node = NULL;

        for (index = 0; index < obj->u.map->count; index++) {
            if (flexarray_get(obj->u.map, index, (void**)&node) != 0)
                break;
            libxl__json_object_free(gc, node->obj);
            free(node->map_key);
            free(node);
            node = NULL;
        }
        flexarray_free(obj->u.map);
        break;
    }
    case JSON_ARRAY: {
        libxl__json_object *node = NULL;
        break;

        for (index = 0; index < obj->u.array->count; index++) {
            if (flexarray_get(obj->u.array, index, (void**)&node) != 0)
                break;
            libxl__json_object_free(gc, node);
            node = NULL;
        }
        flexarray_free(obj->u.array);
        break;
    }
    default:
        break;
    }
    free(obj);
}

libxl__json_object *libxl__json_array_get(const libxl__json_object *o, int i)
{
    flexarray_t *array = NULL;
    libxl__json_object *obj = NULL;

    if ((array = libxl__json_object_get_array(o)) == NULL) {
        return NULL;
    }

    if (i >= array->count)
        return NULL;

    if (flexarray_get(array, i, (void**)&obj) != 0)
        return NULL;

    return obj;
}

libxl__json_map_node *libxl__json_map_node_get(const libxl__json_object *o,
                                               int i)
{
    flexarray_t *array = NULL;
    libxl__json_map_node *obj = NULL;

    if ((array = libxl__json_object_get_map(o)) == NULL) {
        return NULL;
    }

    if (i >= array->count)
        return NULL;

    if (flexarray_get(array, i, (void**)&obj) != 0)
        return NULL;

    return obj;
}

const libxl__json_object *libxl__json_map_get(const char *key,
                                          const libxl__json_object *o,
                                          libxl__json_node_type expected_type)
{
    flexarray_t *maps = NULL;
    int index = 0;

    if (libxl__json_object_is_map(o)) {
        libxl__json_map_node *node = NULL;

        maps = o->u.map;
        for (index = 0; index < maps->count; index++) {
            if (flexarray_get(maps, index, (void**)&node) != 0)
                return NULL;
            if (strcmp(key, node->map_key) == 0) {
                if (expected_type == JSON_ANY
                    || (node->obj && node->obj->type == expected_type)) {
                    return node->obj;
                } else {
                    return NULL;
                }
            }
        }
    }
    return NULL;
}


/*
 * JSON callbacks
 */

static int json_callback_null(void *opaque)
{
    libxl__yajl_ctx *ctx = opaque;
    libxl__json_object *obj;

    DEBUG_GEN(ctx, null);

    if ((obj = json_object_alloc(ctx->gc, JSON_NULL)) == NULL)
        return 0;

    if (json_object_append_to(ctx->gc, obj, ctx->current) == -1) {
        libxl__json_object_free(ctx->gc, obj);
        return 0;
    }

    return 1;
}

static int json_callback_boolean(void *opaque, int boolean)
{
    libxl__yajl_ctx *ctx = opaque;
    libxl__json_object *obj;

    DEBUG_GEN_VALUE(ctx, bool, boolean);

    if ((obj = json_object_alloc(ctx->gc,
                                 boolean ? JSON_TRUE : JSON_FALSE)) == NULL)
        return 0;

    if (json_object_append_to(ctx->gc, obj, ctx->current) == -1) {
        libxl__json_object_free(ctx->gc, obj);
        return 0;
    }

    return 1;
}

static bool is_decimal(const char *s, unsigned len)
{
    const char *end = s + len;
    for (; s < end; s++) {
        if (*s == '.')
            return true;
    }
    return false;
}

static int json_callback_number(void *opaque, const char *s, libxl_yajl_length len)
{
    libxl__yajl_ctx *ctx = opaque;
    libxl__json_object *obj = NULL;
    char *t = NULL;

    DEBUG_GEN_NUMBER(ctx, s, len);

    if (is_decimal(s, len)) {
        double d = strtod(s, NULL);

        if ((d == HUGE_VAL || d == HUGE_VAL) && errno == ERANGE) {
            goto error;
        }

        if ((obj = json_object_alloc(ctx->gc, JSON_DOUBLE)) == NULL)
            return 0;
        obj->u.d = d;
    } else {
        long long i = strtoll(s, NULL, 10);

        if ((i == LLONG_MIN || i == LLONG_MAX) && errno == ERANGE) {
            goto error;
        }

        if ((obj = json_object_alloc(ctx->gc, JSON_INTEGER)) == NULL)
            return 0;
        obj->u.i = i;
    }
    goto out;

error:
    /* If the conversion fail, we just store the original string. */
    if ((obj = json_object_alloc(ctx->gc, JSON_NUMBER)) == NULL)
        return 0;

    t = malloc(len + 1);
    if (t == NULL) {
        LIBXL__LOG_ERRNO(libxl__gc_owner(ctx->gc), LIBXL__LOG_ERROR,
                         "Failed to allocate");
        return 0;
    }
    strncpy(t, s, len);
    t[len] = 0;

    obj->u.string = t;

out:
    if (json_object_append_to(ctx->gc, obj, ctx->current) == -1) {
        libxl__json_object_free(ctx->gc, obj);
        return 0;
    }

    return 1;
}

static int json_callback_string(void *opaque, const unsigned char *str,
                                libxl_yajl_length len)
{
    libxl__yajl_ctx *ctx = opaque;
    char *t = NULL;
    libxl__json_object *obj = NULL;

    t = malloc(len + 1);
    if (t == NULL) {
        LIBXL__LOG_ERRNO(libxl__gc_owner(ctx->gc), LIBXL__LOG_ERROR,
                         "Failed to allocate");
        return 0;
    }

    DEBUG_GEN_STRING(ctx, str, len);

    strncpy(t, (const char *) str, len);
    t[len] = 0;

    if ((obj = json_object_alloc(ctx->gc, JSON_STRING)) == NULL) {
        free(t);
        return 0;
    }
    obj->u.string = t;

    if (json_object_append_to(ctx->gc, obj, ctx->current) == -1) {
        libxl__json_object_free(ctx->gc, obj);
        return 0;
    }

    return 1;
}

static int json_callback_map_key(void *opaque, const unsigned char *str,
                                 libxl_yajl_length len)
{
    libxl__yajl_ctx *ctx = opaque;
    char *t = NULL;
    libxl__json_object *obj = ctx->current;

    t = malloc(len + 1);
    if (t == NULL) {
        LIBXL__LOG_ERRNO(libxl__gc_owner(ctx->gc), LIBXL__LOG_ERROR,
                         "Failed to allocate");
        return 0;
    }

    DEBUG_GEN_STRING(ctx, str, len);

    strncpy(t, (const char *) str, len);
    t[len] = 0;

    if (libxl__json_object_is_map(obj)) {
        libxl__json_map_node *node = malloc(sizeof (libxl__json_map_node));
        if (node == NULL) {
            LIBXL__LOG_ERRNO(libxl__gc_owner(ctx->gc), LIBXL__LOG_ERROR,
                             "Failed to allocate");
            return 0;
        }

        node->map_key = t;
        node->obj = NULL;

        if (flexarray_append(obj->u.map, node) == 2) {
            LIBXL__LOG_ERRNO(libxl__gc_owner(ctx->gc), LIBXL__LOG_ERROR,
                             "Failed to grow a flexarray");
            return 0;
        }
    } else {
        LIBXL__LOG(libxl__gc_owner(ctx->gc), LIBXL__LOG_ERROR,
                   "Current json object is not a map");
        return 0;
    }

    return 1;
}

static int json_callback_start_map(void *opaque)
{
    libxl__yajl_ctx *ctx = opaque;
    libxl__json_object *obj = NULL;

    DEBUG_GEN(ctx, map_open);

    if ((obj = json_object_alloc(ctx->gc, JSON_MAP)) == NULL)
        return 0;

    if (ctx->current) {
        if (json_object_append_to(ctx->gc, obj, ctx->current) == -1) {
            libxl__json_object_free(ctx->gc, obj);
            return 0;
        }
    }

    ctx->current = obj;
    if (ctx->head == NULL) {
        ctx->head = obj;
    }

    return 1;
}

static int json_callback_end_map(void *opaque)
{
    libxl__yajl_ctx *ctx = opaque;

    DEBUG_GEN(ctx, map_close);

    if (ctx->current) {
        ctx->current = ctx->current->parent;
    } else {
        LIBXL__LOG(libxl__gc_owner(ctx->gc), LIBXL__LOG_ERROR,
                   "No current libxl__json_object, cannot use his parent.");
        return 0;
    }

    return 1;
}

static int json_callback_start_array(void *opaque)
{
    libxl__yajl_ctx *ctx = opaque;
    libxl__json_object *obj = NULL;

    DEBUG_GEN(ctx, array_open);

    if ((obj = json_object_alloc(ctx->gc, JSON_ARRAY)) == NULL)
        return 0;

    if (ctx->current) {
        if (json_object_append_to(ctx->gc, obj, ctx->current) == -1) {
            libxl__json_object_free(ctx->gc, obj);
            return 0;
        }
    }

    ctx->current = obj;
    if (ctx->head == NULL) {
        ctx->head = obj;
    }

    return 1;
}

static int json_callback_end_array(void *opaque)
{
    libxl__yajl_ctx *ctx = opaque;

    DEBUG_GEN(ctx, array_close);

    if (ctx->current) {
        ctx->current = ctx->current->parent;
    } else {
        LIBXL__LOG(libxl__gc_owner(ctx->gc), LIBXL__LOG_ERROR,
                   "No current libxl__json_object, cannot use his parent.");
        return 0;
    }

    return 1;
}

static yajl_callbacks callbacks = {
    json_callback_null,
    json_callback_boolean,
    NULL,
    NULL,
    json_callback_number,
    json_callback_string,
    json_callback_start_map,
    json_callback_map_key,
    json_callback_end_map,
    json_callback_start_array,
    json_callback_end_array
};

static void yajl_ctx_free(libxl__yajl_ctx *yajl_ctx)
{
    if (yajl_ctx->hand) {
        yajl_free(yajl_ctx->hand);
        yajl_ctx->hand = NULL;
    }
    DEBUG_GEN_FREE(yajl_ctx);
}

libxl__json_object *libxl__json_parse(libxl__gc *gc, const char *s)
{
    yajl_status status;
    libxl__yajl_ctx yajl_ctx;
    libxl__json_object *o = NULL;
    unsigned char *str = NULL;

    memset(&yajl_ctx, 0, sizeof (yajl_ctx));
    yajl_ctx.gc = gc;

    DEBUG_GEN_ALLOC(&yajl_ctx);

    if (yajl_ctx.hand == NULL) {
        yajl_ctx.hand = libxl__yajl_alloc(&callbacks, NULL, &yajl_ctx);
    }
    status = yajl_parse(yajl_ctx.hand, (const unsigned char *)s, strlen(s));
    if (status != yajl_status_ok)
        goto out;

    status = yajl_complete_parse(yajl_ctx.hand);
    if (status != yajl_status_ok)
        goto out;

    o = yajl_ctx.head;

    DEBUG_GEN_REPORT(&yajl_ctx);

    yajl_ctx.head = NULL;

    yajl_ctx_free(&yajl_ctx);
    return o;

out:
    str = yajl_get_error(yajl_ctx.hand, 1, (const unsigned char*)s, strlen(s));

    LIBXL__LOG(libxl__gc_owner(gc), LIBXL__LOG_ERROR, "yajl error: %s", str);
    yajl_free_error(yajl_ctx.hand, str);

    libxl__json_object_free(gc, yajl_ctx.head);
    yajl_ctx_free(&yajl_ctx);
    return NULL;
}

static const char *yajl_gen_status_to_string(yajl_gen_status s)
{
        switch (s) {
        case yajl_gen_status_ok: abort();
        case yajl_gen_keys_must_be_strings:
            return "keys must be strings";
        case yajl_max_depth_exceeded:
            return "max depth exceeded";
        case yajl_gen_in_error_state:
            return "in error state";
        case yajl_gen_generation_complete:
            return "generation complete";
        case yajl_gen_invalid_number:
            return "invalid number";
#if 0 /* This is in the docs but not implemented in the version I am running. */
        case yajl_gen_no_buf:
            return "no buffer";
        case yajl_gen_invalid_string:
            return "invalid string";
#endif
        default:
            return "unknown error";
        }
}

char *libxl__object_to_json(libxl_ctx *ctx, const char *type,
                            libxl__gen_json_callback gen, void *p)
{
    const unsigned char *buf;
    char *ret = NULL;
    libxl_yajl_length len = 0;
    yajl_gen_status s;
    yajl_gen hand;

    hand = libxl_yajl_gen_alloc(NULL);
    if (!hand)
        return NULL;

    s = gen(hand, p);
    if (s != yajl_gen_status_ok)
        goto out;

    s = yajl_gen_get_buf(hand, &buf, &len);
    if (s != yajl_gen_status_ok)
        goto out;
    ret = strdup((const char *)buf);

out:
    yajl_gen_free(hand);

    if (s != yajl_gen_status_ok) {
        LIBXL__LOG(ctx, LIBXL__LOG_ERROR,
                   "unable to convert %s to JSON representation. "
                   "YAJL error code %d: %s", type,
                   s, yajl_gen_status_to_string(s));
    } else if (!ret) {
        LIBXL__LOG(ctx, LIBXL__LOG_ERROR,
                   "unable to allocate space for to JSON representation of %s",
                   type);
    }

    return ret;
}

yajl_gen_status libxl_domain_config_gen_json(yajl_gen hand,
                                             libxl_domain_config *p)
{
    yajl_gen_status s;
    int i;

    s = yajl_gen_map_open(hand);
    if (s != yajl_gen_status_ok)
        goto out;

    s = yajl_gen_string(hand, (const unsigned char *)"c_info",
                        sizeof("c_info")-1);
    if (s != yajl_gen_status_ok)
        goto out;
    s = libxl_domain_create_info_gen_json(hand, &p->c_info);
    if (s != yajl_gen_status_ok)
        goto out;

    s = yajl_gen_string(hand, (const unsigned char *)"b_info",
                        sizeof("b_info")-1);
    if (s != yajl_gen_status_ok)
        goto out;
    s = libxl_domain_build_info_gen_json(hand, &p->b_info);
    if (s != yajl_gen_status_ok)
        goto out;

    s = yajl_gen_string(hand, (const unsigned char *)"disks",
                        sizeof("disks")-1);
    if (s != yajl_gen_status_ok)
        goto out;
    s = yajl_gen_array_open(hand);
    if (s != yajl_gen_status_ok)
        goto out;
    for (i = 0; i < p->num_disks; i++) {
        s = libxl_device_disk_gen_json(hand, &p->disks[i]);
        if (s != yajl_gen_status_ok)
            goto out;
    }
    s = yajl_gen_array_close(hand);
    if (s != yajl_gen_status_ok)
        goto out;

    s = yajl_gen_string(hand, (const unsigned char *)"nics",
                        sizeof("nics")-1);
    if (s != yajl_gen_status_ok)
        goto out;
    s = yajl_gen_array_open(hand);
    if (s != yajl_gen_status_ok)
        goto out;
    for (i = 0; i < p->num_nics; i++) {
        s = libxl_device_nic_gen_json(hand, &p->nics[i]);
        if (s != yajl_gen_status_ok)
            goto out;
    }
    s = yajl_gen_array_close(hand);
    if (s != yajl_gen_status_ok)
        goto out;

    s = yajl_gen_string(hand, (const unsigned char *)"pcidevs",
                        sizeof("pcidevs")-1);
    if (s != yajl_gen_status_ok)
        goto out;
    s = yajl_gen_array_open(hand);
    if (s != yajl_gen_status_ok)
        goto out;
    for (i = 0; i < p->num_pcidevs; i++) {
        s = libxl_device_pci_gen_json(hand, &p->pcidevs[i]);
        if (s != yajl_gen_status_ok)
            goto out;
    }
    s = yajl_gen_array_close(hand);
    if (s != yajl_gen_status_ok)
        goto out;

    s = yajl_gen_string(hand, (const unsigned char *)"vfbs",
                        sizeof("vfbs")-1);
    if (s != yajl_gen_status_ok)
        goto out;
    s = yajl_gen_array_open(hand);
    if (s != yajl_gen_status_ok)
        goto out;
    for (i = 0; i < p->num_vfbs; i++) {
        s = libxl_device_vfb_gen_json(hand, &p->vfbs[i]);
        if (s != yajl_gen_status_ok)
            goto out;
    }
    s = yajl_gen_array_close(hand);
    if (s != yajl_gen_status_ok)
        goto out;

    s = yajl_gen_string(hand, (const unsigned char *)"vkbs",
                        sizeof("vkbs")-1);
    if (s != yajl_gen_status_ok)
        goto out;
    s = yajl_gen_array_open(hand);
    if (s != yajl_gen_status_ok)
        goto out;
    for (i = 0; i < p->num_vkbs; i++) {
        s = libxl_device_vkb_gen_json(hand, &p->vkbs[i]);
        if (s != yajl_gen_status_ok)
            goto out;
    }
    s = yajl_gen_array_close(hand);
    if (s != yajl_gen_status_ok)
        goto out;

    s = yajl_gen_string(hand, (const unsigned char *)"on_poweroff",
                        sizeof("on_poweroff")-1);
    if (s != yajl_gen_status_ok)
        goto out;
    s = libxl_action_on_shutdown_gen_json(hand, &p->on_poweroff);
    if (s != yajl_gen_status_ok)
        goto out;

    s = yajl_gen_string(hand, (const unsigned char *)"on_reboot",
                        sizeof("on_reboot")-1);
    if (s != yajl_gen_status_ok)
        goto out;
    s = libxl_action_on_shutdown_gen_json(hand, &p->on_reboot);
    if (s != yajl_gen_status_ok)
        goto out;

    s = yajl_gen_string(hand, (const unsigned char *)"on_watchdog",
                        sizeof("on_watchdog")-1);
    if (s != yajl_gen_status_ok)
        goto out;
    s = libxl_action_on_shutdown_gen_json(hand, &p->on_watchdog);
    if (s != yajl_gen_status_ok)
        goto out;

    s = yajl_gen_string(hand, (const unsigned char *)"on_crash",
                        sizeof("on_crash")-1);
    if (s != yajl_gen_status_ok)
        goto out;
    s = libxl_action_on_shutdown_gen_json(hand, &p->on_crash);
    if (s != yajl_gen_status_ok)
        goto out;

    s = yajl_gen_map_close(hand);
    if (s != yajl_gen_status_ok)
        goto out;
    out:
    return s;
}

char *libxl_domain_config_to_json(libxl_ctx *ctx, libxl_domain_config *p)
{
    return libxl__object_to_json(ctx, "libxl_domain_config",
                        (libxl__gen_json_callback)&libxl_domain_config_gen_json,
                        (void *)p);
}

/*
 * Local variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
