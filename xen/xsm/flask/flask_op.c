/*
 *  This file contains the flask_op hypercall and associated functions.
 *
 *  Author:  George Coker, <gscoker@alpha.ncsc.mil>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2,
 *  as published by the Free Software Foundation.
 */

#include <xen/errno.h>
#include <xen/event.h>
#include <xsm/xsm.h>
#include <xen/guest_access.h>

#include <public/xsm/flask_op.h>

#include <avc.h>
#include <avc_ss.h>
#include <objsec.h>
#include <conditional.h>

#ifdef FLASK_DEVELOP
int flask_enforcing = 0;
integer_param("flask_enforcing", flask_enforcing);
#endif

#ifdef FLASK_BOOTPARAM
int flask_enabled = 1;
integer_param("flask_enabled", flask_enabled);
#endif

#define MAX_POLICY_SIZE 0x4000000

#define FLASK_COPY_OUT \
    ( \
        1UL<<FLASK_CONTEXT_TO_SID | \
        1UL<<FLASK_SID_TO_CONTEXT | \
        1UL<<FLASK_ACCESS | \
        1UL<<FLASK_CREATE | \
        1UL<<FLASK_RELABEL | \
        1UL<<FLASK_USER | \
        1UL<<FLASK_GETBOOL | \
        1UL<<FLASK_SETBOOL | \
        1UL<<FLASK_AVC_HASHSTATS | \
        1UL<<FLASK_AVC_CACHESTATS | \
        1UL<<FLASK_MEMBER | \
        1UL<<FLASK_GET_PEER_SID | \
   0)

static DEFINE_SPINLOCK(sel_sem);

/* global data for booleans */
static int bool_num = 0;
static int *bool_pending_values = NULL;
static int flask_security_make_bools(void);

extern int ss_initialized;

extern struct xsm_operations *original_ops;

static int domain_has_security(struct domain *d, u32 perms)
{
    struct domain_security_struct *dsec;
    
    dsec = d->ssid;
    if ( !dsec )
        return -EACCES;
        
    return avc_has_perm(dsec->sid, SECINITSID_SECURITY, SECCLASS_SECURITY, 
                        perms, NULL);
}

static int flask_copyin_string(XEN_GUEST_HANDLE(char) u_buf, char **buf, uint32_t size)
{
    char *tmp = xmalloc_bytes(size + 1);
    if ( !tmp )
        return -ENOMEM;

    if ( copy_from_guest(tmp, u_buf, size) )
    {
        xfree(tmp);
        return -EFAULT;
    }
    tmp[size] = 0;

    *buf = tmp;
    return 0;
}

static int flask_security_user(struct xen_flask_userlist *arg)
{
    char *user;
    u32 *sids;
    u32 nsids;
    int rv;

    rv = domain_has_security(current->domain, SECURITY__COMPUTE_USER);
    if ( rv )
        return rv;

    rv = flask_copyin_string(arg->u.user, &user, arg->size);
    if ( rv )
        return rv;

    rv = security_get_user_sids(arg->start_sid, user, &sids, &nsids);
    if ( rv < 0 )
        goto out;

    if ( nsids * sizeof(sids[0]) > arg->size )
        nsids = arg->size / sizeof(sids[0]);

    arg->size = nsids;

    if ( copy_to_guest(arg->u.sids, sids, nsids) )
        rv = -EFAULT;

    xfree(sids);
 out:
    xfree(user);
    return rv;
}

static int flask_security_relabel(struct xen_flask_transition *arg)
{
    int rv;

    rv = domain_has_security(current->domain, SECURITY__COMPUTE_RELABEL);
    if ( rv )
        return rv;

    rv = security_change_sid(arg->ssid, arg->tsid, arg->tclass, &arg->newsid);

    return rv;
}

static int flask_security_create(struct xen_flask_transition *arg)
{
    int rv;

    rv = domain_has_security(current->domain, SECURITY__COMPUTE_CREATE);
    if ( rv )
        return rv;

    rv = security_transition_sid(arg->ssid, arg->tsid, arg->tclass, &arg->newsid);

    return rv;
}

static int flask_security_access(struct xen_flask_access *arg)
{
    struct av_decision avd;
    int rv;

    rv = domain_has_security(current->domain, SECURITY__COMPUTE_AV);
    if ( rv )
        return rv;

    rv = security_compute_av(arg->ssid, arg->tsid, arg->tclass, arg->req, &avd);
    if ( rv < 0 )
        return rv;

    arg->allowed = avd.allowed;
    arg->audit_allow = avd.auditallow;
    arg->audit_deny = avd.auditdeny;
    arg->seqno = avd.seqno;
                
    return rv;
}

static int flask_security_member(struct xen_flask_transition *arg)
{
    int rv;

    rv = domain_has_security(current->domain, SECURITY__COMPUTE_MEMBER);
    if ( rv )
        return rv;

    rv = security_member_sid(arg->ssid, arg->tsid, arg->tclass, &arg->newsid);

    return rv;
}

static int flask_security_setenforce(struct xen_flask_setenforce *arg)
{
    int enforce = !!(arg->enforcing);
    int rv;

    if ( enforce == flask_enforcing )
        return 0;

    rv = domain_has_security(current->domain, SECURITY__SETENFORCE);
    if ( rv )
        return rv;

    flask_enforcing = enforce;

    if ( flask_enforcing )
        avc_ss_reset(0);

    return 0;
}

static int flask_security_context(struct xen_flask_sid_context *arg)
{
    int rv;
    char *buf;

    rv = domain_has_security(current->domain, SECURITY__CHECK_CONTEXT);
    if ( rv )
        return rv;

    rv = flask_copyin_string(arg->context, &buf, arg->size);
    if ( rv )
        return rv;

    rv = security_context_to_sid(buf, arg->size, &arg->sid);
    if ( rv < 0 )
        goto out;

 out:
    xfree(buf);

    return rv;
}

static int flask_security_sid(struct xen_flask_sid_context *arg)
{
    int rv;
    char *context;
    u32 len;

    rv = domain_has_security(current->domain, SECURITY__CHECK_CONTEXT);
    if ( rv )
        return rv;

    rv = security_sid_to_context(arg->sid, &context, &len);
    if ( rv < 0 )
        return rv;

    rv = 0;

    if ( len > arg->size )
        rv = -ERANGE;

    arg->size = len;

    if ( !rv && copy_to_guest(arg->context, context, len) )
        rv = -EFAULT;

    xfree(context);

    return rv;
}

int flask_disable(void)
{
    static int flask_disabled = 0;

    if ( ss_initialized )
    {
        /* Not permitted after initial policy load. */
        return -EINVAL;
    }

    if ( flask_disabled )
    {
        /* Only do this once. */
        return -EINVAL;
    }

    printk("Flask:  Disabled at runtime.\n");

    flask_disabled = 1;

    /* Reset xsm_ops to the original module. */
    xsm_ops = original_ops;

    return 0;
}

static int flask_security_setavc_threshold(struct xen_flask_setavc_threshold *arg)
{
    int rv = 0;

    if ( arg->threshold != avc_cache_threshold )
    {
        rv = domain_has_security(current->domain, SECURITY__SETSECPARAM);
        if ( rv )
            goto out;
        avc_cache_threshold = arg->threshold;
    }

 out:
    return rv;
}

static int flask_security_resolve_bool(struct xen_flask_boolean *arg)
{
    char *name;
    int rv;

    if ( arg->bool_id != -1 )
        return 0;

    rv = flask_copyin_string(arg->name, &name, arg->size);
    if ( rv )
        return rv;

    arg->bool_id = security_find_bool(name);
    arg->size = 0;

    xfree(name);

    return 0;
}

static int flask_security_set_bool(struct xen_flask_boolean *arg)
{
    int rv;

    rv = flask_security_resolve_bool(arg);
    if ( rv )
        return rv;

    rv = domain_has_security(current->domain, SECURITY__SETBOOL);
    if ( rv )
        return rv;

    spin_lock(&sel_sem);

    if ( arg->commit )
    {
        int num;
        int *values;

        rv = security_get_bools(&num, NULL, &values);
        if ( rv != 0 )
            goto out;

        if ( arg->bool_id >= num )
        {
            rv = -ENOENT;
            goto out;
        }
        values[arg->bool_id] = !!(arg->new_value);

        arg->enforcing = arg->pending = !!(arg->new_value);

        if ( bool_pending_values )
            bool_pending_values[arg->bool_id] = !!(arg->new_value);

        rv = security_set_bools(num, values);
        xfree(values);
    }
    else
    {
        if ( !bool_pending_values )
            flask_security_make_bools();

        if ( arg->bool_id >= bool_num )
            goto out;

        bool_pending_values[arg->bool_id] = !!(arg->new_value);
        arg->pending = !!(arg->new_value);
        arg->enforcing = security_get_bool_value(arg->bool_id);

        rv = 0;
    }

 out:
    spin_unlock(&sel_sem);
    return rv;
}

static int flask_security_commit_bools(void)
{
    int rv;

    spin_lock(&sel_sem);

    rv = domain_has_security(current->domain, SECURITY__SETBOOL);
    if ( rv )
        goto out;

    if ( bool_pending_values )
        rv = security_set_bools(bool_num, bool_pending_values);
    
 out:
    spin_unlock(&sel_sem);
    return rv;
}

static int flask_security_get_bool(struct xen_flask_boolean *arg)
{
    int rv;

    rv = flask_security_resolve_bool(arg);
    if ( rv )
        return rv;

    spin_lock(&sel_sem);

    rv = security_get_bool_value(arg->bool_id);
    if ( rv < 0 )
        goto out;

    arg->enforcing = rv;

    if ( bool_pending_values )
        arg->pending = bool_pending_values[arg->bool_id];
    else
        arg->pending = rv;

    rv = 0;

    if ( arg->size )
    {
        char *nameout = security_get_bool_name(arg->bool_id);
        size_t nameout_len = strlen(nameout);
        if ( nameout_len > arg->size )
            rv = -ERANGE;
        arg->size = nameout_len;
 
        if ( !rv && copy_to_guest(arg->name, nameout, nameout_len) )
            rv = -EFAULT;
        xfree(nameout);
    }

 out:
    spin_unlock(&sel_sem);
    return rv;
}

static int flask_security_make_bools(void)
{
    int ret = 0;
    int num;
    int *values = NULL;
    
    xfree(bool_pending_values);
    
    ret = security_get_bools(&num, NULL, &values);
    if ( ret != 0 )
        goto out;

    bool_num = num;
    bool_pending_values = values;

 out:
    return ret;
}

#ifdef FLASK_AVC_STATS

static int flask_security_avc_cachestats(struct xen_flask_cache_stats *arg)
{
    struct avc_cache_stats *st;

    if ( arg->cpu > nr_cpu_ids )
        return -ENOENT;
    if ( !cpu_online(arg->cpu) )
        return -ENOENT;

    st = &per_cpu(avc_cache_stats, arg->cpu);

    arg->lookups = st->lookups;
    arg->hits = st->hits;
    arg->misses = st->misses;
    arg->allocations = st->allocations;
    arg->reclaims = st->reclaims;
    arg->frees = st->frees;

    return 0;
}

#endif

static int flask_security_load(struct xen_flask_load *load)
{
    int ret;
    void *buf = NULL;

    ret = domain_has_security(current->domain, SECURITY__LOAD_POLICY);
    if ( ret )
        return ret;

    if ( load->size > MAX_POLICY_SIZE )
        return -EINVAL;

    buf = xmalloc_bytes(load->size);
    if ( !buf )
        return -ENOMEM;

    if ( copy_from_guest(buf, load->buffer, load->size) )
    {
        ret = -EFAULT;
        goto out_free;
    }

    spin_lock(&sel_sem);

    ret = security_load_policy(buf, load->size);
    if ( ret )
        goto out;

    xfree(bool_pending_values);
    bool_pending_values = NULL;
    ret = 0;

 out:
    spin_unlock(&sel_sem);
 out_free:
    xfree(buf);
    return ret;
}

static int flask_ocontext_del(struct xen_flask_ocontext *arg)
{
    int rv;

    if ( arg->low > arg->high )
        return -EINVAL;

    rv = domain_has_security(current->domain, SECURITY__DEL_OCONTEXT);
    if ( rv )
        return rv;

    return security_ocontext_del(arg->ocon, arg->low, arg->high);
}

static int flask_ocontext_add(struct xen_flask_ocontext *arg)
{
    int rv;

    if ( arg->low > arg->high )
        return -EINVAL;

    rv = domain_has_security(current->domain, SECURITY__ADD_OCONTEXT);
    if ( rv )
        return rv;

    return security_ocontext_add(arg->ocon, arg->low, arg->high, arg->sid);
}

static int flask_get_peer_sid(struct xen_flask_peersid *arg)
{
    int rv = -EINVAL;
    struct domain *d = current->domain;
    struct domain *peer;
    struct evtchn *chn;
    struct domain_security_struct *dsec;

    spin_lock(&d->event_lock);

    if ( !port_is_valid(d, arg->evtchn) )
        goto out;

    chn = evtchn_from_port(d, arg->evtchn);
    if ( chn->state != ECS_INTERDOMAIN )
        goto out;

    peer = chn->u.interdomain.remote_dom;
    if ( !peer )
        goto out;

    dsec = peer->ssid;
    arg->sid = dsec->sid;
    rv = 0;

 out:
    spin_unlock(&d->event_lock);
    return rv;
}

long do_flask_op(XEN_GUEST_HANDLE(xsm_op_t) u_flask_op)
{
    xen_flask_op_t op;
    int rv;

    if ( copy_from_guest(&op, u_flask_op, 1) )
        return -EFAULT;

    if ( op.interface_version != XEN_FLASK_INTERFACE_VERSION )
        return -ENOSYS;

    switch ( op.cmd )
    {
    case FLASK_LOAD:
        rv = flask_security_load(&op.u.load);
        break;

    case FLASK_GETENFORCE:
        rv = flask_enforcing;
        break;

    case FLASK_SETENFORCE:
        rv = flask_security_setenforce(&op.u.enforce);
        break;

    case FLASK_CONTEXT_TO_SID:
        rv = flask_security_context(&op.u.sid_context);
        break;

    case FLASK_SID_TO_CONTEXT:
        rv = flask_security_sid(&op.u.sid_context);
        break;

    case FLASK_ACCESS:
        rv = flask_security_access(&op.u.access);
        break;

    case FLASK_CREATE:
        rv = flask_security_create(&op.u.transition);
        break;

    case FLASK_RELABEL:
        rv = flask_security_relabel(&op.u.transition);
        break;

    case FLASK_USER:
        rv = flask_security_user(&op.u.userlist);
        break;

    case FLASK_POLICYVERS:
        rv = POLICYDB_VERSION_MAX;
        break;

    case FLASK_GETBOOL:
        rv = flask_security_get_bool(&op.u.boolean);
        break;

    case FLASK_SETBOOL:
        rv = flask_security_set_bool(&op.u.boolean);
        break;

    case FLASK_COMMITBOOLS:
        rv = flask_security_commit_bools();
        break;

    case FLASK_MLS:
        rv = flask_mls_enabled;
        break;    

    case FLASK_DISABLE:
        rv = flask_disable();
        break;

    case FLASK_GETAVC_THRESHOLD:
        rv = avc_cache_threshold;
        break;

    case FLASK_SETAVC_THRESHOLD:
        rv = flask_security_setavc_threshold(&op.u.setavc_threshold);
        break;

    case FLASK_AVC_HASHSTATS:
        rv = avc_get_hash_stats(&op.u.hash_stats);
        break;

#ifdef FLASK_AVC_STATS
    case FLASK_AVC_CACHESTATS:
        rv = flask_security_avc_cachestats(&op.u.cache_stats);
        break;
#endif

    case FLASK_MEMBER:
        rv = flask_security_member(&op.u.transition);
        break;

    case FLASK_ADD_OCONTEXT:
        rv = flask_ocontext_add(&op.u.ocontext);
        break;

    case FLASK_DEL_OCONTEXT:
        rv = flask_ocontext_del(&op.u.ocontext);
        break;

    case FLASK_GET_PEER_SID:
        rv = flask_get_peer_sid(&op.u.peersid);
        break;

    default:
        rv = -ENOSYS;
    }

    if ( rv < 0 )
        goto out;

    if ( (FLASK_COPY_OUT&(1UL<<op.cmd)) )
    {
        if ( copy_to_guest(u_flask_op, &op, 1) )
            rv = -EFAULT;
    }

 out:
    return rv;
}
