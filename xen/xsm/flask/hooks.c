/*
 *  This file contains the Flask hook function implementations for Xen.
 *
 *  Author:  George Coker, <gscoker@alpha.ncsc.mil>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License version 2,
 *      as published by the Free Software Foundation.
 */

#include <xen/init.h>
#include <xen/lib.h>
#include <xen/sched.h>
#include <xen/paging.h>
#include <xen/xmalloc.h>
#include <xsm/xsm.h>
#include <xen/spinlock.h>
#include <xen/cpumask.h>
#include <xen/errno.h>
#include <xen/guest_access.h>
#include <xen/xenoprof.h>
#include <asm/msi.h>
#include <public/xen.h>
#include <public/physdev.h>
#include <public/platform.h>

#include <public/xsm/flask_op.h>

#include <avc.h>
#include <avc_ss.h>
#include <objsec.h>
#include <conditional.h>

struct xsm_operations *original_ops = NULL;

static int domain_has_perm(struct domain *dom1, struct domain *dom2, 
                           u16 class, u32 perms)
{
    struct domain_security_struct *dsec1, *dsec2;
    struct avc_audit_data ad;
    AVC_AUDIT_DATA_INIT(&ad, NONE);
    ad.sdom = dom1;
    ad.tdom = dom2;

    dsec1 = dom1->ssid;
    dsec2 = dom2->ssid;

    return avc_has_perm(dsec1->sid, dsec2->sid, class, perms, &ad);
}

static int domain_has_evtchn(struct domain *d, struct evtchn *chn, u32 perms)
{
    struct domain_security_struct *dsec;
    struct evtchn_security_struct *esec;

    dsec = d->ssid;
    esec = chn->ssid;

    return avc_has_perm(dsec->sid, esec->sid, SECCLASS_EVENT, perms, NULL);
}

static int domain_has_xen(struct domain *d, u32 perms)
{
    struct domain_security_struct *dsec;
    dsec = d->ssid;

    return avc_has_perm(dsec->sid, SECINITSID_XEN, SECCLASS_XEN, perms, NULL);
}

static int get_irq_sid(int irq, u32 *sid, struct avc_audit_data *ad)
{
    struct irq_desc *desc = irq_to_desc(irq);
    if ( irq >= nr_irqs || irq < 0 )
        return -EINVAL;
    if ( irq < nr_irqs_gsi ) {
        if (ad) {
            AVC_AUDIT_DATA_INIT(ad, IRQ);
            ad->irq = irq;
        }
        return security_irq_sid(irq, sid);
    }
    if ( desc->msi_desc ) {
        struct pci_dev *dev = desc->msi_desc->dev;
        u32 sbdf = (dev->seg << 16) | (dev->bus << 8) | dev->devfn;
        if (ad) {
            AVC_AUDIT_DATA_INIT(ad, DEV);
            ad->device = sbdf;
        }
        return security_device_sid(sbdf, sid);
    }
    if (ad) {
        AVC_AUDIT_DATA_INIT(ad, IRQ);
        ad->irq = irq;
    }
    /* HPET or IOMMU IRQ, should not be seen by domains */
    *sid = SECINITSID_UNLABELED;
    return 0;
}

static int flask_domain_alloc_security(struct domain *d)
{
    struct domain_security_struct *dsec;

    dsec = xmalloc(struct domain_security_struct);

    if ( !dsec )
        return -ENOMEM;

    memset(dsec, 0, sizeof(struct domain_security_struct));

    dsec->create_sid = SECSID_NULL;
    switch ( d->domain_id )
    {
    case DOMID_IDLE:
        dsec->sid = SECINITSID_XEN;
        dsec->create_sid = SECINITSID_DOM0;
        break;
    case DOMID_XEN:
        dsec->sid = SECINITSID_DOMXEN;
        break;
    case DOMID_IO:
        dsec->sid = SECINITSID_DOMIO;
        break;
    default:
        dsec->sid = SECINITSID_UNLABELED;
    }

    d->ssid = dsec;

    return 0;
}

static void flask_domain_free_security(struct domain *d)
{
    struct domain_security_struct *dsec = d->ssid;

    if ( !dsec )
        return;

    d->ssid = NULL;
    xfree(dsec);
}

static int flask_evtchn_unbound(struct domain *d1, struct evtchn *chn, 
                                domid_t id2)
{
    u32 newsid;
    int rc;
    domid_t id;
    struct domain *d2;
    struct domain_security_struct *dsec, *dsec1, *dsec2;
    struct evtchn_security_struct *esec;

    dsec = current->domain->ssid;
    dsec1 = d1->ssid;
    esec = chn->ssid;

    if ( id2 == DOMID_SELF )
        id = current->domain->domain_id;
    else
        id = id2;

    d2 = get_domain_by_id(id);
    if ( d2 == NULL )
        return -EPERM;

    dsec2 = d2->ssid;
    rc = security_transition_sid(dsec1->sid, dsec2->sid, SECCLASS_EVENT, 
                                 &newsid);
    if ( rc )
        goto out;

    rc = avc_has_perm(dsec->sid, newsid, SECCLASS_EVENT, EVENT__CREATE, NULL);
    if ( rc )
        goto out;

    rc = avc_has_perm(newsid, dsec2->sid, SECCLASS_EVENT, EVENT__BIND, NULL);
    if ( rc )
        goto out;
    else
        esec->sid = newsid;

 out:
    put_domain(d2);
    return rc;
}

static int flask_evtchn_interdomain(struct domain *d1, struct evtchn *chn1, 
                                    struct domain *d2, struct evtchn *chn2)
{
    u32 newsid;
    int rc;
    struct domain_security_struct *dsec, *dsec1, *dsec2;
    struct evtchn_security_struct *esec1, *esec2;
    struct avc_audit_data ad;
    AVC_AUDIT_DATA_INIT(&ad, NONE);
    ad.sdom = d1;
    ad.tdom = d2;

    dsec = current->domain->ssid;
    dsec1 = d1->ssid;
    dsec2 = d2->ssid;

    esec1 = chn1->ssid;
    esec2 = chn2->ssid;

    rc = security_transition_sid(dsec1->sid, dsec2->sid, 
                                 SECCLASS_EVENT, &newsid);
    if ( rc )
    {
        printk("%s: security_transition_sid failed, rc=%d (domain=%d)\n",
               __FUNCTION__, -rc, d2->domain_id);
        return rc;
    }

    rc = avc_has_perm(dsec->sid, newsid, SECCLASS_EVENT, EVENT__CREATE, &ad);
    if ( rc )
        return rc;

    rc = avc_has_perm(newsid, dsec2->sid, SECCLASS_EVENT, EVENT__BIND, &ad);
    if ( rc )
        return rc;

    rc = avc_has_perm(esec2->sid, dsec1->sid, SECCLASS_EVENT, EVENT__BIND, &ad);
    if ( rc )
        return rc;

    esec1->sid = newsid;

    return rc;
}

static void flask_evtchn_close_post(struct evtchn *chn)
{
    struct evtchn_security_struct *esec;
    esec = chn->ssid;

    esec->sid = SECINITSID_UNLABELED;
}

static int flask_evtchn_send(struct domain *d, struct evtchn *chn)
{
    int rc;

    switch ( chn->state )
    {
    case ECS_INTERDOMAIN:
        rc = domain_has_evtchn(d, chn, EVENT__SEND);
        break;
    case ECS_IPI:
    case ECS_UNBOUND:
        rc = 0;
        break;
    default:
        rc = -EPERM;
    }

    return rc;
}

static int flask_evtchn_status(struct domain *d, struct evtchn *chn)
{
    return domain_has_evtchn(d, chn, EVENT__STATUS);
}

static int flask_evtchn_reset(struct domain *d1, struct domain *d2)
{
    return domain_has_perm(d1, d2, SECCLASS_EVENT, EVENT__RESET);
}

static int flask_alloc_security_evtchn(struct evtchn *chn)
{
    struct evtchn_security_struct *esec;

    esec = xmalloc(struct evtchn_security_struct);

    if ( !esec )
        return -ENOMEM;

    memset(esec, 0, sizeof(struct evtchn_security_struct));

    esec->sid = SECINITSID_UNLABELED;

    chn->ssid = esec;

    return 0;    
}

static void flask_free_security_evtchn(struct evtchn *chn)
{
    struct evtchn_security_struct *esec;

    if ( !chn )
        return;

    esec = chn->ssid;

    if ( !esec )
        return;

    chn->ssid = NULL;
    xfree(esec);
}

static char *flask_show_security_evtchn(struct domain *d, const struct evtchn *chn)
{
    struct evtchn_security_struct *esec;
    int irq;
    u32 sid = 0;
    char *ctx;
    u32 ctx_len;

    switch ( chn->state )
    {
    case ECS_UNBOUND:
    case ECS_INTERDOMAIN:
        esec = chn->ssid;
        if ( esec )
            sid = esec->sid;
        break;
    case ECS_PIRQ:
        irq = domain_pirq_to_irq(d, chn->u.pirq.irq);
        if (irq && get_irq_sid(irq, &sid, NULL))
            return NULL;
        break;
    }
    if ( !sid )
        return NULL;
    if (security_sid_to_context(sid, &ctx, &ctx_len))
        return NULL;
    return ctx;
}

static int flask_grant_mapref(struct domain *d1, struct domain *d2, 
                              uint32_t flags)
{
    u32 perms = GRANT__MAP_READ;

    if ( !(flags & GNTMAP_readonly) )
        perms |= GRANT__MAP_WRITE;

    return domain_has_perm(d1, d2, SECCLASS_GRANT, perms);
}

static int flask_grant_unmapref(struct domain *d1, struct domain *d2)
{
    return domain_has_perm(d1, d2, SECCLASS_GRANT, GRANT__UNMAP);
}

static int flask_grant_setup(struct domain *d1, struct domain *d2)
{
    return domain_has_perm(d1, d2, SECCLASS_GRANT, GRANT__SETUP);
}

static int flask_grant_transfer(struct domain *d1, struct domain *d2)
{
    return domain_has_perm(d1, d2, SECCLASS_GRANT, GRANT__TRANSFER);
}

static int flask_grant_copy(struct domain *d1, struct domain *d2)
{
    return domain_has_perm(d1, d2, SECCLASS_GRANT, GRANT__COPY);
}

static int flask_grant_query_size(struct domain *d1, struct domain *d2)
{
    return domain_has_perm(d1, d2, SECCLASS_GRANT, GRANT__QUERY);
}

static int flask_get_pod_target(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, DOMAIN__GETPODTARGET);
}

static int flask_set_pod_target(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, DOMAIN__SETPODTARGET);
}

static int flask_memory_adjust_reservation(struct domain *d1, struct domain *d2)
{
    return domain_has_perm(d1, d2, SECCLASS_MMU, MMU__ADJUST);
}

static int flask_memory_stat_reservation(struct domain *d1, struct domain *d2)
{
    return domain_has_perm(d1, d2, SECCLASS_MMU, MMU__STAT);
}

static int flask_memory_pin_page(struct domain *d1, struct domain *d2,
                                 struct page_info *page)
{
    return domain_has_perm(d1, d2, SECCLASS_MMU, MMU__PINPAGE);
}

static int flask_console_io(struct domain *d, int cmd)
{
    u32 perm;

    switch ( cmd )
    {
    case CONSOLEIO_read:
        perm = XEN__READCONSOLE;
        break;
    case CONSOLEIO_write:
        perm = XEN__WRITECONSOLE;
        break;
    default:
        return -EPERM;
    }

    return domain_has_xen(d, perm);
}

static int flask_profile(struct domain *d, int op)
{
    u32 perm;

    switch ( op )
    {
    case XENOPROF_init:
    case XENOPROF_enable_virq:
    case XENOPROF_disable_virq:
    case XENOPROF_get_buffer:
        perm = XEN__NONPRIVPROFILE;
        break;
    case XENOPROF_reset_active_list:
    case XENOPROF_reset_passive_list:
    case XENOPROF_set_active:
    case XENOPROF_set_passive:
    case XENOPROF_reserve_counters:
    case XENOPROF_counter:
    case XENOPROF_setup_events:
    case XENOPROF_start:
    case XENOPROF_stop:
    case XENOPROF_release_counters:
    case XENOPROF_shutdown:
        perm = XEN__PRIVPROFILE;
        break;
    default:
        return -EPERM;
    }

    return domain_has_xen(d, perm);
}

static int flask_kexec(void)
{
    return domain_has_xen(current->domain, XEN__KEXEC);
}

static int flask_schedop_shutdown(struct domain *d1, struct domain *d2)
{
    return domain_has_perm(d1, d2, SECCLASS_DOMAIN, DOMAIN__SHUTDOWN);
}

static void flask_security_domaininfo(struct domain *d, 
                                      struct xen_domctl_getdomaininfo *info)
{
    struct domain_security_struct *dsec;

    dsec = d->ssid;
    info->ssidref = dsec->sid;
}

static int flask_setvcpucontext(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, 
                           DOMAIN__SETVCPUCONTEXT);
}

static int flask_pausedomain(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, DOMAIN__PAUSE);
}

static int flask_unpausedomain(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, DOMAIN__UNPAUSE);
}

static int flask_resumedomain(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, DOMAIN__RESUME);
}

static int flask_domain_create(struct domain *d, u32 ssidref)
{
    int rc;
    struct domain_security_struct *dsec1;
    struct domain_security_struct *dsec2;

    dsec1 = current->domain->ssid;

    if ( dsec1->create_sid == SECSID_NULL ) 
        dsec1->create_sid = ssidref;

    rc = avc_has_perm(dsec1->sid, dsec1->create_sid, SECCLASS_DOMAIN, 
                      DOMAIN__CREATE, NULL);
    if ( rc )
    {
        dsec1->create_sid = SECSID_NULL;
        return rc;
    }

    dsec2 = d->ssid;
    dsec2->sid = dsec1->create_sid;

    dsec1->create_sid = SECSID_NULL;
    dsec2->create_sid = SECSID_NULL;

    return rc;
}

static int flask_max_vcpus(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, 
                           DOMAIN__MAX_VCPUS);
}

static int flask_destroydomain(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, 
                           DOMAIN__DESTROY);
}

static int flask_vcpuaffinity(int cmd, struct domain *d)
{
    u32 perm;

    switch ( cmd )
    {
    case XEN_DOMCTL_setvcpuaffinity:
        perm = DOMAIN__SETVCPUAFFINITY;
        break;
    case XEN_DOMCTL_getvcpuaffinity:
        perm = DOMAIN__GETVCPUAFFINITY;
        break;
    default:
        return -EPERM;
    }

    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, perm );
}

static int flask_scheduler(struct domain *d)
{
    int rc = 0;

    rc = domain_has_xen(current->domain, XEN__SCHEDULER);
    if ( rc )
        return rc;

    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, 
                           DOMAIN__SCHEDULER);
}

static int flask_getdomaininfo(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN,
                           DOMAIN__GETDOMAININFO);
}

static int flask_getvcpucontext(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, 
                           DOMAIN__GETVCPUCONTEXT);
}

static int flask_getvcpuinfo(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN,
                           DOMAIN__GETVCPUINFO);
}

static int flask_domain_settime(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, DOMAIN__SETTIME);
}

static int flask_set_target(struct domain *d, struct domain *e)
{
    return domain_has_perm(d, e, SECCLASS_DOMAIN, DOMAIN__SET_TARGET);
}

static int flask_domctl(struct domain *d, int cmd)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, DOMAIN__SET_MISC_INFO);
}

static int flask_set_virq_handler(struct domain *d, uint32_t virq)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, DOMAIN__SET_VIRQ_HANDLER);
}

static int flask_tbufcontrol(void)
{
    return domain_has_xen(current->domain, XEN__TBUFCONTROL);
}

static int flask_readconsole(uint32_t clear)
{
    u32 perms = XEN__READCONSOLE;

    if ( clear )
        perms |= XEN__CLEARCONSOLE;

    return domain_has_xen(current->domain, perms);
}

static int flask_sched_id(void)
{
    return domain_has_xen(current->domain, XEN__SCHEDULER);
}

static int flask_setdomainmaxmem(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN,
                           DOMAIN__SETDOMAINMAXMEM);
}

static int flask_setdomainhandle(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN,
                           DOMAIN__SETDOMAINHANDLE);
}

static int flask_setdebugging(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN,
                           DOMAIN__SETDEBUGGING);
}

static int flask_debug_keys(void)
{
    return domain_has_xen(current->domain, XEN__DEBUG);
}

static int flask_getcpuinfo(void)
{
    return domain_has_xen(current->domain, XEN__GETCPUINFO);
}

static int flask_availheap(void)
{
    return domain_has_xen(current->domain, XEN__HEAP);
}

static int flask_get_pmstat(void)
{
    return domain_has_xen(current->domain, XEN__PM_OP);
}

static int flask_setpminfo(void)
{
    return domain_has_xen(current->domain, XEN__PM_OP);
}

static int flask_pm_op(void)
{
    return domain_has_xen(current->domain, XEN__PM_OP);
}

static int flask_do_mca(void)
{
    return domain_has_xen(current->domain, XEN__MCA_OP);
}

static inline u32 resource_to_perm(uint8_t access)
{
    if ( access )
        return RESOURCE__ADD;
    else
        return RESOURCE__REMOVE;
}

static char *flask_show_irq_sid (int irq)
{
    u32 sid, ctx_len;
    char *ctx;
    int rc = get_irq_sid(irq, &sid, NULL);
    if ( rc )
        return NULL;

    if (security_sid_to_context(sid, &ctx, &ctx_len))
        return NULL;

    return ctx;
}

static int flask_map_domain_pirq (struct domain *d, int irq, void *data)
{
    u32 sid;
    int rc = -EPERM;
    struct msi_info *msi = data;

    struct domain_security_struct *ssec, *tsec;
    struct avc_audit_data ad;

    rc = domain_has_perm(current->domain, d, SECCLASS_RESOURCE, RESOURCE__ADD);

    if ( rc )
        return rc;

    if ( irq >= nr_irqs_gsi && msi ) {
        u32 machine_bdf = (msi->seg << 16) | (msi->bus << 8) | msi->devfn;
        AVC_AUDIT_DATA_INIT(&ad, DEV);
        ad.device = machine_bdf;
        rc = security_device_sid(machine_bdf, &sid);
    } else {
        rc = get_irq_sid(irq, &sid, &ad);
    }
    if ( rc )
        return rc;

    ssec = current->domain->ssid;
    tsec = d->ssid;

    rc = avc_has_perm(ssec->sid, sid, SECCLASS_RESOURCE, RESOURCE__ADD_IRQ, &ad);
    if ( rc )
        return rc;

    rc = avc_has_perm(tsec->sid, sid, SECCLASS_RESOURCE, RESOURCE__USE, &ad);
    return rc;
}

static int flask_irq_permission (struct domain *d, int irq, uint8_t access)
{
    u32 perm;
    u32 rsid;
    int rc = -EPERM;

    struct domain_security_struct *ssec, *tsec;
    struct avc_audit_data ad;

    rc = domain_has_perm(current->domain, d, SECCLASS_RESOURCE,
                         resource_to_perm(access));

    if ( rc )
        return rc;

    if ( access )
        perm = RESOURCE__ADD_IRQ;
    else
        perm = RESOURCE__REMOVE_IRQ;

    ssec = current->domain->ssid;
    tsec = d->ssid;

    rc = get_irq_sid(irq, &rsid, &ad);
    if ( rc )
        return rc;

    rc = avc_has_perm(ssec->sid, rsid, SECCLASS_RESOURCE, perm, &ad);
    if ( rc )
        return rc;

    if ( access )
        rc = avc_has_perm(tsec->sid, rsid, SECCLASS_RESOURCE, 
                            RESOURCE__USE, &ad);
    return rc;
}

struct iomem_has_perm_data {
    struct domain_security_struct *ssec, *tsec;
    u32 perm;
};

static int _iomem_has_perm(void *v, u32 sid, unsigned long start, unsigned long end)
{
    struct iomem_has_perm_data *data = v;
    struct avc_audit_data ad;
    int rc = -EPERM;

    AVC_AUDIT_DATA_INIT(&ad, RANGE);
    ad.range.start = start;
    ad.range.end = end;

    rc = avc_has_perm(data->ssec->sid, sid, SECCLASS_RESOURCE, data->perm, &ad);

    if ( rc )
        return rc;

    return avc_has_perm(data->tsec->sid, sid, SECCLASS_RESOURCE, RESOURCE__USE, &ad);
}

static int flask_iomem_permission(struct domain *d, uint64_t start, uint64_t end, uint8_t access)
{
    struct iomem_has_perm_data data;
    int rc;

    rc = domain_has_perm(current->domain, d, SECCLASS_RESOURCE,
                         resource_to_perm(access));
    if ( rc )
        return rc;

    if ( access )
        data.perm = RESOURCE__ADD_IOMEM;
    else
        data.perm = RESOURCE__REMOVE_IOMEM;

    data.ssec = current->domain->ssid;
    data.tsec = d->ssid;

    return security_iterate_iomem_sids(start, end, _iomem_has_perm, &data);
}

static int flask_pci_config_permission(struct domain *d, uint32_t machine_bdf, uint16_t start, uint16_t end, uint8_t access)
{
    u32 rsid;
    int rc = -EPERM;
    struct avc_audit_data ad;
    struct domain_security_struct *ssec;
    u32 perm = RESOURCE__USE;

    rc = security_device_sid(machine_bdf, &rsid);
    if ( rc )
        return rc;

    /* Writes to the BARs count as setup */
    if ( access && (end >= 0x10 && start < 0x28) )
        perm = RESOURCE__SETUP;

    AVC_AUDIT_DATA_INIT(&ad, DEV);
    ad.device = (unsigned long) machine_bdf;
    ssec = d->ssid;
    return avc_has_perm(ssec->sid, rsid, SECCLASS_RESOURCE, perm, &ad);

}

static int flask_resource_plug_core(void)
{
    struct domain_security_struct *ssec;

    ssec = current->domain->ssid;
    return avc_has_perm(ssec->sid, SECINITSID_DOMXEN, SECCLASS_RESOURCE, RESOURCE__PLUG, NULL);
}

static int flask_resource_unplug_core(void)
{
    struct domain_security_struct *ssec;

    ssec = current->domain->ssid;
    return avc_has_perm(ssec->sid, SECINITSID_DOMXEN, SECCLASS_RESOURCE, RESOURCE__UNPLUG, NULL);
}

static int flask_resource_use_core(void)
{
    struct domain_security_struct *ssec;

    ssec = current->domain->ssid;
    return avc_has_perm(ssec->sid, SECINITSID_DOMXEN, SECCLASS_RESOURCE, RESOURCE__USE, NULL);
}

static int flask_resource_plug_pci(uint32_t machine_bdf)
{
    u32 rsid;
    int rc = -EPERM;
    struct avc_audit_data ad;
    struct domain_security_struct *ssec;

    rc = security_device_sid(machine_bdf, &rsid);
    if ( rc )
        return rc;

    AVC_AUDIT_DATA_INIT(&ad, DEV);
    ad.device = (unsigned long) machine_bdf;
    ssec = current->domain->ssid;
    return avc_has_perm(ssec->sid, rsid, SECCLASS_RESOURCE, RESOURCE__PLUG, &ad);
}

static int flask_resource_unplug_pci(uint32_t machine_bdf)
{
    u32 rsid;
    int rc = -EPERM;
    struct avc_audit_data ad;
    struct domain_security_struct *ssec;

    rc = security_device_sid(machine_bdf, &rsid);
    if ( rc )
        return rc;

    AVC_AUDIT_DATA_INIT(&ad, DEV);
    ad.device = (unsigned long) machine_bdf;
    ssec = current->domain->ssid;
    return avc_has_perm(ssec->sid, rsid, SECCLASS_RESOURCE, RESOURCE__UNPLUG, &ad);
}

static int flask_resource_setup_pci(uint32_t machine_bdf)
{
    u32 rsid;
    int rc = -EPERM;
    struct avc_audit_data ad;
    struct domain_security_struct *ssec;

    rc = security_device_sid(machine_bdf, &rsid);
    if ( rc )
        return rc;

    AVC_AUDIT_DATA_INIT(&ad, DEV);
    ad.device = (unsigned long) machine_bdf;
    ssec = current->domain->ssid;
    return avc_has_perm(ssec->sid, rsid, SECCLASS_RESOURCE, RESOURCE__SETUP, &ad);
}

static int flask_resource_setup_gsi(int gsi)
{
    u32 rsid;
    int rc = -EPERM;
    struct avc_audit_data ad;
    struct domain_security_struct *ssec;

    rc = get_irq_sid(gsi, &rsid, &ad);
    if ( rc )
        return rc;

    ssec = current->domain->ssid;
    return avc_has_perm(ssec->sid, rsid, SECCLASS_RESOURCE, RESOURCE__SETUP, &ad);
}

static int flask_resource_setup_misc(void)
{
    struct domain_security_struct *ssec;

    ssec = current->domain->ssid;
    return avc_has_perm(ssec->sid, SECINITSID_XEN, SECCLASS_RESOURCE, RESOURCE__SETUP, NULL);
}

static inline int flask_page_offline(uint32_t cmd)
{
    switch (cmd) {
    case sysctl_page_offline:
        return flask_resource_unplug_core();
    case sysctl_page_online:
        return flask_resource_plug_core();
    case sysctl_query_page_offline:
        return flask_resource_use_core();
    default:
        return -EPERM;
    }
}

static inline int flask_lockprof(void)
{
    return domain_has_xen(current->domain, XEN__LOCKPROF);
}

static inline int flask_cpupool_op(void)
{
    return domain_has_xen(current->domain, XEN__CPUPOOL_OP);
}

static inline int flask_sched_op(void)
{
    return domain_has_xen(current->domain, XEN__SCHED_OP);
}

static int flask_perfcontrol(void)
{
    return domain_has_xen(current->domain, XEN__PERFCONTROL);
}

#ifdef CONFIG_X86
static int flask_shadow_control(struct domain *d, uint32_t op)
{
    u32 perm;

    switch ( op )
    {
    case XEN_DOMCTL_SHADOW_OP_OFF:
        perm = SHADOW__DISABLE;
        break;
    case XEN_DOMCTL_SHADOW_OP_ENABLE:
    case XEN_DOMCTL_SHADOW_OP_ENABLE_TEST:
    case XEN_DOMCTL_SHADOW_OP_ENABLE_TRANSLATE:
    case XEN_DOMCTL_SHADOW_OP_GET_ALLOCATION:
    case XEN_DOMCTL_SHADOW_OP_SET_ALLOCATION:
        perm = SHADOW__ENABLE;
        break;
    case XEN_DOMCTL_SHADOW_OP_ENABLE_LOGDIRTY:
    case XEN_DOMCTL_SHADOW_OP_PEEK:
    case XEN_DOMCTL_SHADOW_OP_CLEAN:
        perm = SHADOW__LOGDIRTY;
        break;
    default:
        return -EPERM;
    }

    return domain_has_perm(current->domain, d, SECCLASS_SHADOW, perm);
}

struct ioport_has_perm_data {
    struct domain_security_struct *ssec, *tsec;
    u32 perm;
};

static int _ioport_has_perm(void *v, u32 sid, unsigned long start, unsigned long end)
{
    struct ioport_has_perm_data *data = v;
    struct avc_audit_data ad;
    int rc;

    AVC_AUDIT_DATA_INIT(&ad, RANGE);
    ad.range.start = start;
    ad.range.end = end;

    rc = avc_has_perm(data->ssec->sid, sid, SECCLASS_RESOURCE, data->perm, &ad);

    if ( rc )
        return rc;

    return avc_has_perm(data->tsec->sid, sid, SECCLASS_RESOURCE, RESOURCE__USE, &ad);
}


static int flask_ioport_permission(struct domain *d, uint32_t start, uint32_t end, uint8_t access)
{
    int rc;
    struct ioport_has_perm_data data;

    rc = domain_has_perm(current->domain, d, SECCLASS_RESOURCE,
                         resource_to_perm(access));

    if ( rc )
        return rc;

    if ( access )
        data.perm = RESOURCE__ADD_IOPORT;
    else
        data.perm = RESOURCE__REMOVE_IOPORT;

    data.ssec = current->domain->ssid;
    data.tsec = d->ssid;

    return security_iterate_ioport_sids(start, end, _ioport_has_perm, &data);
}

static int flask_getpageframeinfo(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_MMU, MMU__PAGEINFO);
}

static int flask_getmemlist(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_MMU, MMU__PAGELIST);
}

static int flask_hypercall_init(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN,
                           DOMAIN__HYPERCALL);
}

static int flask_hvmcontext(struct domain *d, uint32_t cmd)
{
    u32 perm;

    switch ( cmd )
    {
    case XEN_DOMCTL_sethvmcontext:
        perm = HVM__SETHVMC;
        break;
    case XEN_DOMCTL_gethvmcontext:
    case XEN_DOMCTL_gethvmcontext_partial:
        perm = HVM__GETHVMC;
        break;
    case HVMOP_track_dirty_vram:
        perm = HVM__TRACKDIRTYVRAM;
        break;
    default:
        return -EPERM;
    }

    return domain_has_perm(current->domain, d, SECCLASS_HVM, perm);
}

static int flask_address_size(struct domain *d, uint32_t cmd)
{
    u32 perm;

    switch ( cmd )
    {
    case XEN_DOMCTL_set_address_size:
        perm = DOMAIN__SETADDRSIZE;
        break;
    case XEN_DOMCTL_get_address_size:
        perm = DOMAIN__GETADDRSIZE;
        break;
    default:
        return -EPERM;
    }

    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, perm);
}

static int flask_hvm_param(struct domain *d, unsigned long op)
{
    u32 perm;

    switch ( op )
    {
    case HVMOP_set_param:
        perm = HVM__SETPARAM;
        break;
    case HVMOP_get_param:
        perm = HVM__GETPARAM;
        break;
    case HVMOP_track_dirty_vram:
        perm = HVM__TRACKDIRTYVRAM;
        break;
    default:
        perm = HVM__HVMCTL;
    }

    return domain_has_perm(current->domain, d, SECCLASS_HVM, perm);
}

static int flask_hvm_set_pci_intx_level(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_HVM, HVM__PCILEVEL);
}

static int flask_hvm_set_isa_irq_level(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_HVM, HVM__IRQLEVEL);
}

static int flask_hvm_set_pci_link_route(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_HVM, HVM__PCIROUTE);
}

static int flask_mem_event(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_HVM, HVM__MEM_EVENT);
}

static int flask_mem_sharing(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_HVM, HVM__MEM_SHARING);
}

static int flask_apic(struct domain *d, int cmd)
{
    u32 perm;

    switch ( cmd )
    {
    case PHYSDEVOP_APIC_READ:
        perm = XEN__READAPIC;
        break;
    case PHYSDEVOP_APIC_WRITE:
        perm = XEN__WRITEAPIC;
        break;
    default:
        return -EPERM;
    }

    return domain_has_xen(d, perm);
}

static int flask_xen_settime(void)
{
    return domain_has_xen(current->domain, XEN__SETTIME);
}

static int flask_memtype(uint32_t access)
{
    u32 perm;

    switch ( access )
    {
    case XENPF_add_memtype:
        perm = XEN__MTRR_ADD;
        break;
    case XENPF_del_memtype:
        perm = XEN__MTRR_DEL;
        break;
    case XENPF_read_memtype:
        perm = XEN__MTRR_READ;
        break;
    default:
        return -EPERM;
    }

    return domain_has_xen(current->domain, perm);
}

static int flask_microcode(void)
{
    return domain_has_xen(current->domain, XEN__MICROCODE);
}

static int flask_physinfo(void)
{
    return domain_has_xen(current->domain, XEN__PHYSINFO);
}

static int flask_platform_quirk(uint32_t quirk)
{
    struct domain_security_struct *dsec;
    dsec = current->domain->ssid;

    return avc_has_perm(dsec->sid, SECINITSID_XEN, SECCLASS_XEN, 
                        XEN__QUIRK, NULL);
}

static int flask_firmware_info(void)
{
    return domain_has_xen(current->domain, XEN__FIRMWARE);
}

static int flask_efi_call(void)
{
    return domain_has_xen(current->domain, XEN__FIRMWARE);
}

static int flask_acpi_sleep(void)
{
    return domain_has_xen(current->domain, XEN__SLEEP);
}

static int flask_change_freq(void)
{
    return domain_has_xen(current->domain, XEN__FREQUENCY);
}

static int flask_getidletime(void)
{
    return domain_has_xen(current->domain, XEN__GETIDLE);
}

static int flask_machine_memory_map(void)
{
    struct domain_security_struct *dsec;
    dsec = current->domain->ssid;

    return avc_has_perm(dsec->sid, SECINITSID_XEN, SECCLASS_MMU, 
                        MMU__MEMORYMAP, NULL);
}

static int flask_domain_memory_map(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_MMU, MMU__MEMORYMAP);
}

static int domain_memory_perm(struct domain *d, struct domain *f, l1_pgentry_t pte)
{
    int rc = 0;
    u32 map_perms = MMU__MAP_READ;
    unsigned long fgfn, fmfn;
    p2m_type_t p2mt;

    if ( !(l1e_get_flags(pte) & _PAGE_PRESENT) )
        return 0;

    if ( l1e_get_flags(pte) & _PAGE_RW )
        map_perms |= MMU__MAP_WRITE;

    fgfn = l1e_get_pfn(pte);
    fmfn = mfn_x(get_gfn_query(f, fgfn, &p2mt));
    put_gfn(f, fgfn);

    if ( f->domain_id == DOMID_IO || !mfn_valid(fmfn) )
    {
        struct avc_audit_data ad;
        struct domain_security_struct *dsec = d->ssid;
        u32 fsid;
        AVC_AUDIT_DATA_INIT(&ad, MEMORY);
        ad.sdom = d;
        ad.tdom = f;
        ad.memory.pte = pte.l1;
        ad.memory.mfn = fmfn;
        rc = security_iomem_sid(fmfn, &fsid);
        if ( rc )
            return rc;
        return avc_has_perm(dsec->sid, fsid, SECCLASS_MMU, map_perms, &ad);
    }

    return domain_has_perm(d, f, SECCLASS_MMU, map_perms);
}

static int flask_mmu_normal_update(struct domain *d, struct domain *t,
                                   struct domain *f, intpte_t fpte)
{
    int rc = 0;

    if (d != t)
        rc = domain_has_perm(d, t, SECCLASS_MMU, MMU__REMOTE_REMAP);
    if ( rc )
        return rc;

    return domain_memory_perm(d, f, l1e_from_intpte(fpte));
}

static int flask_mmu_machphys_update(struct domain *d1, struct domain *d2,
                                     unsigned long mfn)
{
    return domain_has_perm(d1, d2, SECCLASS_MMU, MMU__UPDATEMP);
}

static int flask_update_va_mapping(struct domain *d, struct domain *f,
                                   l1_pgentry_t pte)
{
    return domain_memory_perm(d, f, pte);
}

static int flask_add_to_physmap(struct domain *d1, struct domain *d2)
{
    return domain_has_perm(d1, d2, SECCLASS_MMU, MMU__PHYSMAP);
}

static int flask_remove_from_physmap(struct domain *d1, struct domain *d2)
{
    return domain_has_perm(d1, d2, SECCLASS_MMU, MMU__PHYSMAP);
}

static int flask_sendtrigger(struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, DOMAIN__TRIGGER);
}

static int flask_get_device_group(uint32_t machine_bdf)
{
    u32 rsid;
    int rc = -EPERM;
    struct domain_security_struct *ssec = current->domain->ssid;

    rc = security_device_sid(machine_bdf, &rsid);
    if ( rc )
        return rc;

    return avc_has_perm(ssec->sid, rsid, SECCLASS_RESOURCE, RESOURCE__STAT_DEVICE, NULL);
}

static int flask_test_assign_device(uint32_t machine_bdf)
{
    u32 rsid;
    int rc = -EPERM;
    struct domain_security_struct *ssec = current->domain->ssid;

    rc = security_device_sid(machine_bdf, &rsid);
    if ( rc )
        return rc;

    return rc = avc_has_perm(ssec->sid, rsid, SECCLASS_RESOURCE, RESOURCE__STAT_DEVICE, NULL);
}

static int flask_assign_device(struct domain *d, uint32_t machine_bdf)
{
    u32 rsid;
    int rc = -EPERM;
    struct domain_security_struct *ssec, *tsec;
    struct avc_audit_data ad;

    rc = domain_has_perm(current->domain, d, SECCLASS_RESOURCE, RESOURCE__ADD);
    if ( rc )
        return rc;

    rc = security_device_sid(machine_bdf, &rsid);
    if ( rc )
        return rc;

    AVC_AUDIT_DATA_INIT(&ad, DEV);
    ad.device = (unsigned long) machine_bdf;
    ssec = current->domain->ssid;
    rc = avc_has_perm(ssec->sid, rsid, SECCLASS_RESOURCE, RESOURCE__ADD_DEVICE, &ad);
    if ( rc )
        return rc;

    tsec = d->ssid;
    return avc_has_perm(tsec->sid, rsid, SECCLASS_RESOURCE, RESOURCE__USE, &ad);
}

static int flask_deassign_device(struct domain *d, uint32_t machine_bdf)
{
    u32 rsid;
    int rc = -EPERM;
    struct domain_security_struct *ssec = current->domain->ssid;

    rc = domain_has_perm(current->domain, d, SECCLASS_RESOURCE, RESOURCE__REMOVE);
    if ( rc )
        return rc;

    rc = security_device_sid(machine_bdf, &rsid);
    if ( rc )
        return rc;

    return rc = avc_has_perm(ssec->sid, rsid, SECCLASS_RESOURCE, RESOURCE__REMOVE_DEVICE, NULL);
}

static int flask_bind_pt_irq (struct domain *d, struct xen_domctl_bind_pt_irq *bind)
{
    u32 rsid;
    int rc = -EPERM;
    int irq;
    struct domain_security_struct *ssec, *tsec;
    struct avc_audit_data ad;

    rc = domain_has_perm(current->domain, d, SECCLASS_RESOURCE, RESOURCE__ADD);
    if ( rc )
        return rc;

    irq = domain_pirq_to_irq(d, bind->machine_irq);

    rc = get_irq_sid(irq, &rsid, &ad);
    if ( rc )
        return rc;

    ssec = current->domain->ssid;
    rc = avc_has_perm(ssec->sid, rsid, SECCLASS_HVM, HVM__BIND_IRQ, &ad);
    if ( rc )
        return rc;

    tsec = d->ssid;
    return avc_has_perm(tsec->sid, rsid, SECCLASS_RESOURCE, RESOURCE__USE, &ad);
}

static int flask_unbind_pt_irq (struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_RESOURCE, RESOURCE__REMOVE);
}

static int flask_pin_mem_cacheattr (struct domain *d)
{
    return domain_has_perm(current->domain, d, SECCLASS_HVM, HVM__CACHEATTR);
}

static int flask_ext_vcpucontext (struct domain *d, uint32_t cmd)
{
    u32 perm;

    switch ( cmd )
    {
    case XEN_DOMCTL_set_ext_vcpucontext:
        perm = DOMAIN__SETEXTVCPUCONTEXT;
        break;
    case XEN_DOMCTL_get_ext_vcpucontext:
        perm = DOMAIN__GETEXTVCPUCONTEXT;
        break;
    default:
        return -EPERM;
    }

    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, perm);
}

static int flask_vcpuextstate (struct domain *d, uint32_t cmd)
{
    u32 perm;

    switch ( cmd )
    {
        case XEN_DOMCTL_setvcpuextstate:
            perm = DOMAIN__SETVCPUEXTSTATE;
        break;
        case XEN_DOMCTL_getvcpuextstate:
            perm = DOMAIN__GETVCPUEXTSTATE;
        break;
        default:
            return -EPERM;
    }

    return domain_has_perm(current->domain, d, SECCLASS_DOMAIN, perm);
}
#endif

long do_flask_op(XEN_GUEST_HANDLE(xsm_op_t) u_flask_op);

static struct xsm_operations flask_ops = {
    .security_domaininfo = flask_security_domaininfo,
    .setvcpucontext = flask_setvcpucontext,
    .pausedomain = flask_pausedomain,
    .unpausedomain = flask_unpausedomain,    
    .resumedomain = flask_resumedomain,    
    .domain_create = flask_domain_create,
    .max_vcpus = flask_max_vcpus,
    .destroydomain = flask_destroydomain,
    .vcpuaffinity = flask_vcpuaffinity,
    .scheduler = flask_scheduler,
    .getdomaininfo = flask_getdomaininfo,
    .getvcpucontext = flask_getvcpucontext,
    .getvcpuinfo = flask_getvcpuinfo,
    .domain_settime = flask_domain_settime,
    .set_target = flask_set_target,
    .domctl = flask_domctl,
    .set_virq_handler = flask_set_virq_handler,
    .tbufcontrol = flask_tbufcontrol,
    .readconsole = flask_readconsole,
    .sched_id = flask_sched_id,
    .setdomainmaxmem = flask_setdomainmaxmem,
    .setdomainhandle = flask_setdomainhandle,
    .setdebugging = flask_setdebugging,
    .perfcontrol = flask_perfcontrol,
    .debug_keys = flask_debug_keys,
    .getcpuinfo = flask_getcpuinfo,
    .availheap = flask_availheap,
    .get_pmstat = flask_get_pmstat,
    .setpminfo = flask_setpminfo,
    .pm_op = flask_pm_op,
    .do_mca = flask_do_mca,

    .evtchn_unbound = flask_evtchn_unbound,
    .evtchn_interdomain = flask_evtchn_interdomain,
    .evtchn_close_post = flask_evtchn_close_post,
    .evtchn_send = flask_evtchn_send,
    .evtchn_status = flask_evtchn_status,
    .evtchn_reset = flask_evtchn_reset,

    .grant_mapref = flask_grant_mapref,
    .grant_unmapref = flask_grant_unmapref,
    .grant_setup = flask_grant_setup,
    .grant_transfer = flask_grant_transfer,
    .grant_copy = flask_grant_copy,
    .grant_query_size = flask_grant_query_size,

    .alloc_security_domain = flask_domain_alloc_security,
    .free_security_domain = flask_domain_free_security,
    .alloc_security_evtchn = flask_alloc_security_evtchn,
    .free_security_evtchn = flask_free_security_evtchn,
    .show_security_evtchn = flask_show_security_evtchn,

    .get_pod_target = flask_get_pod_target,
    .set_pod_target = flask_set_pod_target,
    .memory_adjust_reservation = flask_memory_adjust_reservation,
    .memory_stat_reservation = flask_memory_stat_reservation,
    .memory_pin_page = flask_memory_pin_page,

    .console_io = flask_console_io,

    .profile = flask_profile,

    .kexec = flask_kexec,
    .schedop_shutdown = flask_schedop_shutdown,

    .show_irq_sid = flask_show_irq_sid,

    .map_domain_pirq = flask_map_domain_pirq,
    .irq_permission = flask_irq_permission,
    .iomem_permission = flask_iomem_permission,
    .pci_config_permission = flask_pci_config_permission,

    .resource_plug_core = flask_resource_plug_core,
    .resource_unplug_core = flask_resource_unplug_core,
    .resource_plug_pci = flask_resource_plug_pci,
    .resource_unplug_pci = flask_resource_unplug_pci,
    .resource_setup_pci = flask_resource_setup_pci,
    .resource_setup_gsi = flask_resource_setup_gsi,
    .resource_setup_misc = flask_resource_setup_misc,

    .page_offline = flask_page_offline,
    .lockprof = flask_lockprof,
    .cpupool_op = flask_cpupool_op,
    .sched_op = flask_sched_op,

    .__do_xsm_op = do_flask_op,

#ifdef CONFIG_X86
    .shadow_control = flask_shadow_control,
    .getpageframeinfo = flask_getpageframeinfo,
    .getmemlist = flask_getmemlist,
    .hypercall_init = flask_hypercall_init,
    .hvmcontext = flask_hvmcontext,
    .address_size = flask_address_size,
    .hvm_param = flask_hvm_param,
    .hvm_set_pci_intx_level = flask_hvm_set_pci_intx_level,
    .hvm_set_isa_irq_level = flask_hvm_set_isa_irq_level,
    .hvm_set_pci_link_route = flask_hvm_set_pci_link_route,
    .mem_event = flask_mem_event,
    .mem_sharing = flask_mem_sharing,
    .apic = flask_apic,
    .xen_settime = flask_xen_settime,
    .memtype = flask_memtype,
    .microcode = flask_microcode,
    .physinfo = flask_physinfo,
    .platform_quirk = flask_platform_quirk,
    .firmware_info = flask_firmware_info,
    .efi_call = flask_efi_call,
    .acpi_sleep = flask_acpi_sleep,
    .change_freq = flask_change_freq,
    .getidletime = flask_getidletime,
    .machine_memory_map = flask_machine_memory_map,
    .domain_memory_map = flask_domain_memory_map,
    .mmu_normal_update = flask_mmu_normal_update,
    .mmu_machphys_update = flask_mmu_machphys_update,
    .update_va_mapping = flask_update_va_mapping,
    .add_to_physmap = flask_add_to_physmap,
    .remove_from_physmap = flask_remove_from_physmap,
    .sendtrigger = flask_sendtrigger,
    .get_device_group = flask_get_device_group,
    .test_assign_device = flask_test_assign_device,
    .assign_device = flask_assign_device,
    .deassign_device = flask_deassign_device,
    .bind_pt_irq = flask_bind_pt_irq,
    .unbind_pt_irq = flask_unbind_pt_irq,
    .pin_mem_cacheattr = flask_pin_mem_cacheattr,
    .ext_vcpucontext = flask_ext_vcpucontext,
    .vcpuextstate = flask_vcpuextstate,
    .ioport_permission = flask_ioport_permission,
#endif
};

static __init int flask_init(void)
{
    int ret = 0;

    if ( !flask_enabled )
    {
        printk("Flask:  Disabled at boot.\n");
        return 0;
    }

    printk("Flask:  Initializing.\n");

    avc_init();

    original_ops = xsm_ops;
    if ( register_xsm(&flask_ops) )
        panic("Flask: Unable to register with XSM.\n");

    ret = security_load_policy(policy_buffer, policy_size);

    if ( flask_enforcing )
        printk("Flask:  Starting in enforcing mode.\n");
    else
        printk("Flask:  Starting in permissive mode.\n");

    return ret;
}

xsm_initcall(flask_init);
