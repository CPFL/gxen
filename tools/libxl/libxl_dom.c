/*
 * Copyright (C) 2009      Citrix Ltd.
 * Author Vincent Hanquez <vincent.hanquez@eu.citrix.com>
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

#include <glob.h>

#include "libxl_internal.h"

#include <xc_dom.h>
#include <xen/hvm/hvm_info_table.h>

libxl_domain_type libxl__domain_type(libxl__gc *gc, uint32_t domid)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    xc_domaininfo_t info;
    int ret;

    ret = xc_domain_getinfolist(ctx->xch, domid, 1, &info);
    if (ret != 1 || info.domain != domid) {
        LIBXL__LOG(CTX, LIBXL__LOG_ERROR,
                   "unable to get domain type for domid=%"PRIu32, domid);
        return LIBXL_DOMAIN_TYPE_INVALID;
    }
    if (info.flags & XEN_DOMINF_hvm_guest)
        return LIBXL_DOMAIN_TYPE_HVM;
    else
        return LIBXL_DOMAIN_TYPE_PV;
}

int libxl__domain_shutdown_reason(libxl__gc *gc, uint32_t domid)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    xc_domaininfo_t info;
    int ret;

    ret = xc_domain_getinfolist(ctx->xch, domid, 1, &info);
    if (ret != 1)
        return -1;
    if (info.domain != domid)
        return -1;
    if (!(info.flags & XEN_DOMINF_shutdown))
        return -1;

    return (info.flags >> XEN_DOMINF_shutdownshift) & XEN_DOMINF_shutdownmask;
}

int libxl__domain_cpupool(libxl__gc *gc, uint32_t domid)
{
    xc_domaininfo_t info;
    int ret;

    ret = xc_domain_getinfolist(CTX->xch, domid, 1, &info);
    if (ret != 1)
    {
        LOGE(ERROR, "getinfolist failed %d\n", ret);
        return ERROR_FAIL;
    }
    if (info.domain != domid)
    {
        LOGE(ERROR, "got info for dom%d, wanted dom%d\n", info.domain, domid);
        return ERROR_FAIL;
    }
    return info.cpupool;
}

libxl_scheduler libxl__domain_scheduler(libxl__gc *gc, uint32_t domid)
{
    int cpupool = libxl__domain_cpupool(gc, domid);
    libxl_cpupoolinfo poolinfo;
    libxl_scheduler sched = LIBXL_SCHEDULER_UNKNOWN;
    int rc;

    if (cpupool < 0)
        return sched;

    libxl_cpupoolinfo_init(&poolinfo);
    rc = libxl_cpupool_info(CTX, &poolinfo, cpupool);
    if (rc < 0)
        goto out;

    sched = poolinfo.sched;

out:
    libxl_cpupoolinfo_dispose(&poolinfo);
    return sched;
}

/*
 * Two NUMA placement candidates are compared by means of the following
 * heuristics:

 *  - the number of vcpus runnable on the candidates is considered, and
 *    candidates with fewer of them are preferred. If two candidate have
 *    the same number of runnable vcpus,
 *  - the amount of free memory in the candidates is considered, and the
 *    candidate with greater amount of it is preferred.
 *
 * In fact, leaving larger memory holes, maximizes the probability of being
 * able to put other domains on the node. That hopefully means many domains
 * will benefit from local memory accesses, but also introduces the risk of
 * overloading large (from a memory POV) nodes. That's right the effect
 * that counting the vcpus able to run on the nodes tries to prevent.
 *
 * Note that this completely ignore the number of nodes each candidate span,
 * as the fact that fewer nodes is better is already accounted for in the
 * algorithm.
 */
static int numa_cmpf(const libxl__numa_candidate *c1,
                     const libxl__numa_candidate *c2)
{
    if (c1->nr_vcpus != c2->nr_vcpus)
        return c1->nr_vcpus - c2->nr_vcpus;

    return c2->free_memkb - c1->free_memkb;
}

/* The actual automatic NUMA placement routine */
static int numa_place_domain(libxl__gc *gc, uint32_t domid,
                             libxl_domain_build_info *info)
{
    int found;
    libxl__numa_candidate candidate;
    libxl_bitmap candidate_nodemap;
    libxl_cpupoolinfo cpupool_info;
    int i, cpupool, rc = 0;
    uint32_t memkb;

    libxl__numa_candidate_init(&candidate);
    libxl_bitmap_init(&candidate_nodemap);

    /*
     * Extract the cpumap from the cpupool the domain belong to. In fact,
     * it only makes sense to consider the cpus/nodes that are in there
     * for placement.
     */
    rc = cpupool = libxl__domain_cpupool(gc, domid);
    if (rc < 0)
        return rc;
    rc = libxl_cpupool_info(CTX, &cpupool_info, cpupool);
    if (rc)
        return rc;

    rc = libxl_domain_need_memory(CTX, info, &memkb);
    if (rc)
        goto out;
    if (libxl_node_bitmap_alloc(CTX, &candidate_nodemap, 0)) {
        rc = ERROR_FAIL;
        goto out;
    }

    /* Find the best candidate with enough free memory and at least
     * as much pcpus as the domain has vcpus.  */
    rc = libxl__get_numa_candidate(gc, memkb, info->max_vcpus,
                                   0, 0, &cpupool_info.cpumap,
                                   numa_cmpf, &candidate, &found);
    if (rc)
        goto out;

    /* Not even a suitable placement candidate! Let's just don't touch the
     * domain's info->cpumap. It will have affinity with all nodes/cpus. */
    if (found == 0)
        goto out;

    /* Map the candidate's node map to the domain's info->cpumap */
    libxl__numa_candidate_get_nodemap(gc, &candidate, &candidate_nodemap);
    rc = libxl_nodemap_to_cpumap(CTX, &candidate_nodemap, &info->cpumap);
    if (rc)
        goto out;

    /* Avoid trying to set the affinity to cpus that might be in the
     * nodemap but not in our cpupool. */
    libxl_for_each_set_bit(i, info->cpumap) {
        if (!libxl_bitmap_test(&cpupool_info.cpumap, i))
            libxl_bitmap_reset(&info->cpumap, i);
    }

    LOG(DETAIL, "NUMA placement candidate with %d nodes, %d cpus and "
                "%"PRIu32" KB free selected", candidate.nr_nodes,
                candidate.nr_cpus, candidate.free_memkb / 1024);

 out:
    libxl__numa_candidate_dispose(&candidate);
    libxl_bitmap_dispose(&candidate_nodemap);
    libxl_cpupoolinfo_dispose(&cpupool_info);
    return rc;
}

int libxl__build_pre(libxl__gc *gc, uint32_t domid,
              libxl_domain_build_info *info, libxl__domain_build_state *state)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    int tsc_mode;
    char *xs_domid, *con_domid;
    uint32_t rtc_timeoffset;

    xc_domain_max_vcpus(ctx->xch, domid, info->max_vcpus);

    /*
     * Check if the domain has any CPU affinity. If not, try to build
     * up one. In case numa_place_domain() find at least a suitable
     * candidate, it will affect info->cpumap accordingly; if it
     * does not, it just leaves it as it is. This means (unless
     * some weird error manifests) the subsequent call to
     * libxl_set_vcpuaffinity_all() will do the actual placement,
     * whatever that turns out to be.
     */
    if (libxl_defbool_val(info->numa_placement)) {
        int rc;

        if (!libxl_bitmap_is_full(&info->cpumap)) {
            LOG(ERROR, "Can run NUMA placement only if no vcpu "
                       "affinity is specified");
            return ERROR_INVAL;
        }

        rc = numa_place_domain(gc, domid, info);
        if (rc)
            return rc;
    }
    libxl_set_vcpuaffinity_all(ctx, domid, info->max_vcpus, &info->cpumap);

    xc_domain_setmaxmem(ctx->xch, domid, info->target_memkb + LIBXL_MAXMEM_CONSTANT);
    if (info->type == LIBXL_DOMAIN_TYPE_PV)
        xc_domain_set_memmap_limit(ctx->xch, domid,
                (info->max_memkb + info->u.pv.slack_memkb));
    switch (info->tsc_mode) {
    case LIBXL_TSC_MODE_DEFAULT:
        tsc_mode = 0;
        break;
    case LIBXL_TSC_MODE_ALWAYS_EMULATE:
        tsc_mode = 1;
        break;
    case LIBXL_TSC_MODE_NATIVE:
        tsc_mode = 2;
        break;
    case LIBXL_TSC_MODE_NATIVE_PARAVIRT:
        tsc_mode = 3;
        break;
    default:
        abort();
    }
    xc_domain_set_tsc_info(ctx->xch, domid, tsc_mode, 0, 0, 0);
    if (libxl_defbool_val(info->disable_migrate))
        xc_domain_disable_migrate(ctx->xch, domid);

    rtc_timeoffset = info->rtc_timeoffset;
    if (libxl_defbool_val(info->localtime)) {
        time_t t;
        struct tm *tm;

        t = time(NULL);
        tm = localtime(&t);

        rtc_timeoffset += tm->tm_gmtoff;
    }

    if (rtc_timeoffset)
        xc_domain_set_time_offset(ctx->xch, domid, rtc_timeoffset);

    if (info->type == LIBXL_DOMAIN_TYPE_HVM) {
        unsigned long shadow;
        shadow = (info->shadow_memkb + 1023) / 1024;
        xc_shadow_control(ctx->xch, domid, XEN_DOMCTL_SHADOW_OP_SET_ALLOCATION, NULL, 0, &shadow, 0, NULL);
    }

    xs_domid = xs_read(ctx->xsh, XBT_NULL, "/tool/xenstored/domid", NULL);
    state->store_domid = xs_domid ? atoi(xs_domid) : 0;
    free(xs_domid);

    con_domid = xs_read(ctx->xsh, XBT_NULL, "/tool/xenconsoled/domid", NULL);
    state->console_domid = con_domid ? atoi(con_domid) : 0;
    free(con_domid);

    state->store_port = xc_evtchn_alloc_unbound(ctx->xch, domid, state->store_domid);
    state->console_port = xc_evtchn_alloc_unbound(ctx->xch, domid, state->console_domid);
    state->vm_generationid_addr = 0;

    return 0;
}

int libxl__build_post(libxl__gc *gc, uint32_t domid,
                      libxl_domain_build_info *info,
                      libxl__domain_build_state *state,
                      char **vms_ents, char **local_ents)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    char *dom_path, *vm_path;
    xs_transaction_t t;
    char **ents, **hvm_ents;
    int i, rc;

    rc = libxl_domain_sched_params_set(CTX, domid, &info->sched_params);
    if (rc)
        return rc;

    libxl_cpuid_apply_policy(ctx, domid);
    if (info->cpuid != NULL)
        libxl_cpuid_set(ctx, domid, info->cpuid);

    ents = libxl__calloc(gc, 12 + (info->max_vcpus * 2) + 2, sizeof(char *));
    ents[0] = "memory/static-max";
    ents[1] = libxl__sprintf(gc, "%"PRId64, info->max_memkb);
    ents[2] = "memory/target";
    ents[3] = libxl__sprintf(gc, "%"PRId64,
                             info->target_memkb - info->video_memkb);
    ents[4] = "memory/videoram";
    ents[5] = libxl__sprintf(gc, "%"PRId64, info->video_memkb);
    ents[6] = "domid";
    ents[7] = libxl__sprintf(gc, "%d", domid);
    ents[8] = "store/port";
    ents[9] = libxl__sprintf(gc, "%"PRIu32, state->store_port);
    ents[10] = "store/ring-ref";
    ents[11] = libxl__sprintf(gc, "%lu", state->store_mfn);
    for (i = 0; i < info->max_vcpus; i++) {
        ents[12+(i*2)]   = libxl__sprintf(gc, "cpu/%d/availability", i);
        ents[12+(i*2)+1] = libxl_bitmap_test(&info->avail_vcpus, i)
                            ? "online" : "offline";
    }

    hvm_ents = NULL;
    if (info->type == LIBXL_DOMAIN_TYPE_HVM) {
        hvm_ents = libxl__calloc(gc, 3, sizeof(char *));
        hvm_ents[0] = "hvmloader/generation-id-address";
        hvm_ents[1] = libxl__sprintf(gc, "0x%lx", state->vm_generationid_addr);
    }

    dom_path = libxl__xs_get_dompath(gc, domid);
    if (!dom_path) {
        return ERROR_FAIL;
    }

    vm_path = xs_read(ctx->xsh, XBT_NULL, libxl__sprintf(gc, "%s/vm", dom_path), NULL);
retry_transaction:
    t = xs_transaction_start(ctx->xsh);

    libxl__xs_writev(gc, t, dom_path, ents);
    if (info->type == LIBXL_DOMAIN_TYPE_HVM)
        libxl__xs_writev(gc, t, dom_path, hvm_ents);

    libxl__xs_writev(gc, t, dom_path, local_ents);
    libxl__xs_writev(gc, t, vm_path, vms_ents);

    if (!xs_transaction_end(ctx->xsh, t, 0))
        if (errno == EAGAIN)
            goto retry_transaction;
    xs_introduce_domain(ctx->xsh, domid, state->store_mfn, state->store_port);
    free(vm_path);
    return 0;
}

int libxl__build_pv(libxl__gc *gc, uint32_t domid,
             libxl_domain_build_info *info, libxl__domain_build_state *state)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    struct xc_dom_image *dom;
    int ret;
    int flags = 0;

    xc_dom_loginit(ctx->xch);

    dom = xc_dom_allocate(ctx->xch, state->pv_cmdline, info->u.pv.features);
    if (!dom) {
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "xc_dom_allocate failed");
        return ERROR_FAIL;
    }

    LOG(DEBUG, "pv kernel mapped %d path %s\n", state->pv_kernel.mapped, state->pv_kernel.path);
    if (state->pv_kernel.mapped) {
        ret = xc_dom_kernel_mem(dom,
                                state->pv_kernel.data,
                                state->pv_kernel.size);
        if ( ret != 0) {
            LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "xc_dom_kernel_mem failed");
            goto out;
        }
    } else {
        ret = xc_dom_kernel_file(dom, state->pv_kernel.path);
        if ( ret != 0) {
            LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "xc_dom_kernel_file failed");
            goto out;
        }
    }

    if ( state->pv_ramdisk.path && strlen(state->pv_ramdisk.path) ) {
        if (state->pv_ramdisk.mapped) {
            if ( (ret = xc_dom_ramdisk_mem(dom, state->pv_ramdisk.data, state->pv_ramdisk.size)) != 0 ) {
                LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "xc_dom_ramdisk_mem failed");
                goto out;
            }
        } else {
            if ( (ret = xc_dom_ramdisk_file(dom, state->pv_ramdisk.path)) != 0 ) {
                LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "xc_dom_ramdisk_file failed");
                goto out;
            }
        }
    }

    dom->flags = flags;
    dom->console_evtchn = state->console_port;
    dom->console_domid = state->console_domid;
    dom->xenstore_evtchn = state->store_port;
    dom->xenstore_domid = state->store_domid;

    if ( (ret = xc_dom_boot_xen_init(dom, ctx->xch, domid)) != 0 ) {
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "xc_dom_boot_xen_init failed");
        goto out;
    }
    if ( (ret = xc_dom_parse_image(dom)) != 0 ) {
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "xc_dom_parse_image failed");
        goto out;
    }
    if ( (ret = xc_dom_mem_init(dom, info->target_memkb / 1024)) != 0 ) {
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "xc_dom_mem_init failed");
        goto out;
    }
    if ( (ret = xc_dom_boot_mem_init(dom)) != 0 ) {
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "xc_dom_boot_mem_init failed");
        goto out;
    }
    if ( (ret = xc_dom_build_image(dom)) != 0 ) {
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "xc_dom_build_image failed");
        goto out;
    }
    if ( (ret = xc_dom_boot_image(dom)) != 0 ) {
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "xc_dom_boot_image failed");
        goto out;
    }
    if ( (ret = xc_dom_gnttab_init(dom)) != 0 ) {
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "xc_dom_gnttab_init failed");
        goto out;
    }

    state->console_mfn = xc_dom_p2m_host(dom, dom->console_pfn);
    state->store_mfn = xc_dom_p2m_host(dom, dom->xenstore_pfn);

    libxl__file_reference_unmap(&state->pv_kernel);
    libxl__file_reference_unmap(&state->pv_ramdisk);

    ret = 0;
out:
    xc_dom_release(dom);
    return ret == 0 ? 0 : ERROR_FAIL;
}

static unsigned long timer_mode(const libxl_domain_build_info *info)
{
    const libxl_timer_mode mode = info->u.hvm.timer_mode;
    assert(mode != LIBXL_TIMER_MODE_DELAY_FOR_MISSED_TICKS &&
           mode <= LIBXL_TIMER_MODE_ONE_MISSED_TICK_PENDING);
    return ((unsigned long)mode);
}
static int hvm_build_set_params(xc_interface *handle, uint32_t domid,
                                libxl_domain_build_info *info,
                                int store_evtchn, unsigned long *store_mfn,
                                int console_evtchn, unsigned long *console_mfn,
                                domid_t store_domid, domid_t console_domid)
{
    struct hvm_info_table *va_hvm;
    uint8_t *va_map, sum;
    int i;

    va_map = xc_map_foreign_range(handle, domid,
                                  XC_PAGE_SIZE, PROT_READ | PROT_WRITE,
                                  HVM_INFO_PFN);
    if (va_map == NULL)
        return -1;

    va_hvm = (struct hvm_info_table *)(va_map + HVM_INFO_OFFSET);
    va_hvm->apic_mode = libxl_defbool_val(info->u.hvm.apic);
    va_hvm->nr_vcpus = info->max_vcpus;
    memset(va_hvm->vcpu_online, 0, sizeof(va_hvm->vcpu_online));
    memcpy(va_hvm->vcpu_online, info->avail_vcpus.map, info->avail_vcpus.size);
    for (i = 0, sum = 0; i < va_hvm->length; i++)
        sum += ((uint8_t *) va_hvm)[i];
    va_hvm->checksum -= sum;
    munmap(va_map, XC_PAGE_SIZE);

    xc_get_hvm_param(handle, domid, HVM_PARAM_STORE_PFN, store_mfn);
    xc_get_hvm_param(handle, domid, HVM_PARAM_CONSOLE_PFN, console_mfn);
    xc_set_hvm_param(handle, domid, HVM_PARAM_PAE_ENABLED,
                     libxl_defbool_val(info->u.hvm.pae));
#if defined(__i386__) || defined(__x86_64__)
    xc_set_hvm_param(handle, domid, HVM_PARAM_VIRIDIAN,
                     libxl_defbool_val(info->u.hvm.viridian));
    xc_set_hvm_param(handle, domid, HVM_PARAM_HPET_ENABLED,
                     libxl_defbool_val(info->u.hvm.hpet));
#endif
    xc_set_hvm_param(handle, domid, HVM_PARAM_TIMER_MODE, timer_mode(info));
    xc_set_hvm_param(handle, domid, HVM_PARAM_VPT_ALIGN,
                     libxl_defbool_val(info->u.hvm.vpt_align));
    xc_set_hvm_param(handle, domid, HVM_PARAM_NESTEDHVM,
                     libxl_defbool_val(info->u.hvm.nested_hvm));
    xc_set_hvm_param(handle, domid, HVM_PARAM_STORE_EVTCHN, store_evtchn);
    xc_set_hvm_param(handle, domid, HVM_PARAM_CONSOLE_EVTCHN, console_evtchn);

    xc_dom_gnttab_hvm_seed(handle, domid, *console_mfn, *store_mfn, console_domid, store_domid);
    return 0;
}

static const char *libxl__domain_firmware(libxl__gc *gc,
                                          libxl_domain_build_info *info)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    const char *firmware;

    if (info->u.hvm.firmware)
        firmware = info->u.hvm.firmware;
    else {
        switch (info->device_model_version)
        {
        case LIBXL_DEVICE_MODEL_VERSION_QEMU_XEN_TRADITIONAL:
            firmware = "hvmloader";
            break;
        case LIBXL_DEVICE_MODEL_VERSION_QEMU_XEN:
            firmware = "hvmloader";
            break;
        default:
            LIBXL__LOG(ctx, LIBXL__LOG_ERROR, "invalid device model version %d",
                       info->device_model_version);
            return NULL;
            break;
        }
    }
    return libxl__abs_path(gc, firmware, libxl__xenfirmwaredir_path());
}

int libxl__build_hvm(libxl__gc *gc, uint32_t domid,
              libxl_domain_build_info *info,
              libxl__domain_build_state *state)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    int ret, rc = ERROR_FAIL;
    const char *firmware = libxl__domain_firmware(gc, info);

    if (!firmware)
        goto out;
    ret = xc_hvm_build_target_mem(
        ctx->xch,
        domid,
        (info->max_memkb - info->video_memkb) / 1024,
        (info->target_memkb - info->video_memkb) / 1024,
        firmware);
    if (ret) {
        LIBXL__LOG_ERRNOVAL(ctx, LIBXL__LOG_ERROR, ret, "hvm building failed");
        goto out;
    }
    ret = hvm_build_set_params(ctx->xch, domid, info, state->store_port,
                               &state->store_mfn, state->console_port,
                               &state->console_mfn, state->store_domid,
                               state->console_domid);
    if (ret) {
        LIBXL__LOG_ERRNOVAL(ctx, LIBXL__LOG_ERROR, ret, "hvm build set params failed");
        goto out;
    }
    rc = 0;
out:
    return rc;
}

int libxl__qemu_traditional_cmd(libxl__gc *gc, uint32_t domid,
                                const char *cmd)
{
    char *path = NULL;
    path = libxl__sprintf(gc, "/local/domain/0/device-model/%d/command",
                          domid);
    return libxl__xs_write(gc, XBT_NULL, path, "%s", cmd);
}

struct libxl__physmap_info {
    uint64_t phys_offset;
    uint64_t start_addr;
    uint64_t size;
    uint32_t namelen;
    char name[];
};

#define TOOLSTACK_SAVE_VERSION 1

static inline char *restore_helper(libxl__gc *gc, uint32_t domid,
        uint64_t phys_offset, char *node)
{
    return libxl__sprintf(gc,
            "/local/domain/0/device-model/%d/physmap/%"PRIx64"/%s",
            domid, phys_offset, node);
}

int libxl__toolstack_restore(uint32_t domid, const uint8_t *buf,
                             uint32_t size, void *user)
{
    libxl__save_helper_state *shs = user;
    libxl__domain_create_state *dcs = CONTAINER_OF(shs, *dcs, shs);
    STATE_AO_GC(dcs->ao);
    libxl_ctx *ctx = CTX;
    int i, ret;
    const uint8_t *ptr = buf;
    uint32_t count = 0, version = 0;
    struct libxl__physmap_info* pi;
    char *xs_path;

    LOG(DEBUG,"domain=%"PRIu32" toolstack data size=%"PRIu32, domid, size);

    if (size < sizeof(version) + sizeof(count)) {
        LIBXL__LOG(ctx, LIBXL__LOG_ERROR, "wrong size");
        return -1;
    }

    memcpy(&version, ptr, sizeof(version));
    ptr += sizeof(version);

    if (version != TOOLSTACK_SAVE_VERSION) {
        LIBXL__LOG(ctx, LIBXL__LOG_ERROR, "wrong version");
        return -1;
    }

    memcpy(&count, ptr, sizeof(count));
    ptr += sizeof(count);
 
    if (size < sizeof(version) + sizeof(count) +
            count * (sizeof(struct libxl__physmap_info))) {
        LIBXL__LOG(ctx, LIBXL__LOG_ERROR, "wrong size");
        return -1;
    }

    for (i = 0; i < count; i++) {
        pi = (struct libxl__physmap_info*) ptr;
        ptr += sizeof(struct libxl__physmap_info) + pi->namelen;

        xs_path = restore_helper(gc, domid, pi->phys_offset, "start_addr");
        ret = libxl__xs_write(gc, 0, xs_path, "%"PRIx64, pi->start_addr);
        if (ret)
            return -1;
        xs_path = restore_helper(gc, domid, pi->phys_offset, "size");
        ret = libxl__xs_write(gc, 0, xs_path, "%"PRIx64, pi->size);
        if (ret)
            return -1;
        if (pi->namelen > 0) {
            xs_path = restore_helper(gc, domid, pi->phys_offset, "name");
            ret = libxl__xs_write(gc, 0, xs_path, "%s", pi->name);
            if (ret)
                return -1;
        }
    }
    return 0;
}

/*==================== Domain suspend (save) ====================*/

static void domain_suspend_done(libxl__egc *egc,
                        libxl__domain_suspend_state *dss, int rc);

/*----- complicated callback, called by xc_domain_save -----*/

/*
 * We implement the other end of protocol for controlling qemu-dm's
 * logdirty.  There is no documentation for this protocol, but our
 * counterparty's implementation is in
 * qemu-xen-traditional.git:xenstore.c in the function
 * xenstore_process_logdirty_event
 */

static void switch_logdirty_timeout(libxl__egc *egc, libxl__ev_time *ev,
                                    const struct timeval *requested_abs);
static void switch_logdirty_xswatch(libxl__egc *egc, libxl__ev_xswatch*,
                            const char *watch_path, const char *event_path);
static void switch_logdirty_done(libxl__egc *egc,
                                 libxl__domain_suspend_state *dss, int ok);

static void logdirty_init(libxl__logdirty_switch *lds)
{
    lds->cmd_path = 0;
    libxl__ev_xswatch_init(&lds->watch);
    libxl__ev_time_init(&lds->timeout);
}

void libxl__domain_suspend_common_switch_qemu_logdirty
                               (int domid, unsigned enable, void *user)
{
    libxl__save_helper_state *shs = user;
    libxl__egc *egc = shs->egc;
    libxl__domain_suspend_state *dss = CONTAINER_OF(shs, *dss, shs);
    libxl__logdirty_switch *lds = &dss->logdirty;
    STATE_AO_GC(dss->ao);
    int rc;
    xs_transaction_t t = 0;
    const char *got;

    if (!lds->cmd_path) {
        lds->cmd_path = GCSPRINTF(
                   "/local/domain/0/device-model/%u/logdirty/cmd", domid);
        lds->ret_path = GCSPRINTF(
                   "/local/domain/0/device-model/%u/logdirty/ret", domid);
    }
    lds->cmd = enable ? "enable" : "disable";

    rc = libxl__ev_xswatch_register(gc, &lds->watch,
                                switch_logdirty_xswatch, lds->ret_path);
    if (rc) goto out;

    rc = libxl__ev_time_register_rel(gc, &lds->timeout,
                                switch_logdirty_timeout, 10*1000);
    if (rc) goto out;

    for (;;) {
        rc = libxl__xs_transaction_start(gc, &t);
        if (rc) goto out;

        rc = libxl__xs_read_checked(gc, t, lds->cmd_path, &got);
        if (rc) goto out;

        if (got) {
            const char *got_ret;
            rc = libxl__xs_read_checked(gc, t, lds->ret_path, &got_ret);
            if (rc) goto out;

            if (strcmp(got, got_ret)) {
                LOG(ERROR,"controlling logdirty: qemu was already sent"
                    " command `%s' (xenstore path `%s') but result is `%s'",
                    got, lds->cmd_path, got_ret ? got_ret : "<none>");
                rc = ERROR_FAIL;
                goto out;
            }
            rc = libxl__xs_rm_checked(gc, t, lds->cmd_path);
            if (rc) goto out;
        }

        rc = libxl__xs_rm_checked(gc, t, lds->ret_path);
        if (rc) goto out;

        rc = libxl__xs_write_checked(gc, t, lds->cmd_path, lds->cmd);
        if (rc) goto out;

        rc = libxl__xs_transaction_commit(gc, &t);
        if (!rc) break;
        if (rc<0) goto out;
    }

    /* OK, wait for some callback */
    return;

 out:
    LOG(ERROR,"logdirty switch failed (rc=%d), aborting suspend",rc);
    libxl__xs_transaction_abort(gc, &t);
    switch_logdirty_done(egc,dss,-1);
}

static void switch_logdirty_timeout(libxl__egc *egc, libxl__ev_time *ev,
                                    const struct timeval *requested_abs)
{
    libxl__domain_suspend_state *dss = CONTAINER_OF(ev, *dss, logdirty.timeout);
    STATE_AO_GC(dss->ao);
    LOG(ERROR,"logdirty switch: wait for device model timed out");
    switch_logdirty_done(egc,dss,-1);
}

static void switch_logdirty_xswatch(libxl__egc *egc, libxl__ev_xswatch *watch,
                            const char *watch_path, const char *event_path)
{
    libxl__domain_suspend_state *dss =
        CONTAINER_OF(watch, *dss, logdirty.watch);
    libxl__logdirty_switch *lds = &dss->logdirty;
    STATE_AO_GC(dss->ao);
    const char *got;
    xs_transaction_t t = 0;
    int rc;

    for (;;) {
        rc = libxl__xs_transaction_start(gc, &t);
        if (rc) goto out;

        rc = libxl__xs_read_checked(gc, t, lds->ret_path, &got);
        if (rc) goto out;

        if (!got) {
            rc = +1;
            goto out;
        }

        if (strcmp(got, lds->cmd)) {
            LOG(ERROR,"logdirty switch: sent command `%s' but got reply `%s'"
                " (xenstore paths `%s' / `%s')", lds->cmd, got,
                lds->cmd_path, lds->ret_path);
            rc = ERROR_FAIL;
            goto out;
        }

        rc = libxl__xs_rm_checked(gc, t, lds->cmd_path);
        if (rc) goto out;

        rc = libxl__xs_rm_checked(gc, t, lds->ret_path);
        if (rc) goto out;

        rc = libxl__xs_transaction_commit(gc, &t); 
        if (!rc) break;
        if (rc<0) goto out;
    }

 out:
    /* rc < 0: error
     * rc == 0: ok, we are done
     * rc == +1: need to keep waiting
     */
    libxl__xs_transaction_abort(gc, &t);

    if (!rc) {
        switch_logdirty_done(egc,dss,0);
    } else if (rc < 0) {
        LOG(ERROR,"logdirty switch: failed (rc=%d)",rc);
        switch_logdirty_done(egc,dss,-1);
    }
}

static void switch_logdirty_done(libxl__egc *egc,
                                 libxl__domain_suspend_state *dss,
                                 int broke)
{
    STATE_AO_GC(dss->ao);
    libxl__logdirty_switch *lds = &dss->logdirty;

    libxl__ev_xswatch_deregister(gc, &lds->watch);
    libxl__ev_time_deregister(gc, &lds->timeout);

    libxl__xc_domain_saverestore_async_callback_done(egc, &dss->shs, broke);
}

/*----- callbacks, called by xc_domain_save -----*/

int libxl__domain_suspend_device_model(libxl__gc *gc,
                                       libxl__domain_suspend_state *dss)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    int ret = 0;
    uint32_t const domid = dss->domid;
    const char *const filename = dss->dm_savefile;

    switch (libxl__device_model_version_running(gc, domid)) {
    case LIBXL_DEVICE_MODEL_VERSION_QEMU_XEN_TRADITIONAL: {
        LIBXL__LOG(ctx, LIBXL__LOG_DEBUG,
                   "Saving device model state to %s", filename);
        libxl__qemu_traditional_cmd(gc, domid, "save");
        libxl__wait_for_device_model(gc, domid, "paused", NULL, NULL, NULL);
        break;
    }
    case LIBXL_DEVICE_MODEL_VERSION_QEMU_XEN:
        if (libxl__qmp_stop(gc, domid))
            return ERROR_FAIL;
        /* Save DM state into filename */
        ret = libxl__qmp_save(gc, domid, filename);
        if (ret)
            unlink(filename);
        break;
    default:
        return ERROR_INVAL;
    }

    return ret;
}

int libxl__domain_resume_device_model(libxl__gc *gc, uint32_t domid)
{

    switch (libxl__device_model_version_running(gc, domid)) {
    case LIBXL_DEVICE_MODEL_VERSION_QEMU_XEN_TRADITIONAL: {
        libxl__qemu_traditional_cmd(gc, domid, "continue");
        libxl__wait_for_device_model(gc, domid, "running", NULL, NULL, NULL);
        break;
    }
    case LIBXL_DEVICE_MODEL_VERSION_QEMU_XEN:
        if (libxl__qmp_resume(gc, domid))
            return ERROR_FAIL;
    default:
        return ERROR_INVAL;
    }

    return 0;
}

int libxl__domain_suspend_common_callback(void *user)
{
    libxl__save_helper_state *shs = user;
    libxl__domain_suspend_state *dss = CONTAINER_OF(shs, *dss, shs);
    STATE_AO_GC(dss->ao);
    unsigned long hvm_s_state = 0, hvm_pvdrv = 0;
    int ret;
    char *state = "suspend";
    int watchdog;
    xs_transaction_t t;

    /* Convenience aliases */
    const uint32_t domid = dss->domid;

    if (dss->hvm) {
        xc_get_hvm_param(CTX->xch, domid, HVM_PARAM_CALLBACK_IRQ, &hvm_pvdrv);
        xc_get_hvm_param(CTX->xch, domid, HVM_PARAM_ACPI_S_STATE, &hvm_s_state);
    }

    if ((hvm_s_state == 0) && (dss->suspend_eventchn >= 0)) {
        LOG(DEBUG, "issuing %s suspend request via event channel",
            dss->hvm ? "PVHVM" : "PV");
        ret = xc_evtchn_notify(dss->xce, dss->suspend_eventchn);
        if (ret < 0) {
            LOG(ERROR, "xc_evtchn_notify failed ret=%d", ret);
            return 0;
        }
        ret = xc_await_suspend(CTX->xch, dss->xce, dss->suspend_eventchn);
        if (ret < 0) {
            LOG(ERROR, "xc_await_suspend failed ret=%d", ret);
            return 0;
        }
        dss->guest_responded = 1;
        goto guest_suspended;
    }

    if (dss->hvm && (!hvm_pvdrv || hvm_s_state)) {
        LOG(DEBUG, "Calling xc_domain_shutdown on HVM domain");
        xc_domain_shutdown(CTX->xch, domid, SHUTDOWN_suspend);
        /* The guest does not (need to) respond to this sort of request. */
        dss->guest_responded = 1;
    } else {
        LOG(DEBUG, "issuing %s suspend request via XenBus control node",
            dss->hvm ? "PVHVM" : "PV");

        libxl__domain_pvcontrol_write(gc, XBT_NULL, domid, "suspend");

        LOG(DEBUG, "wait for the guest to acknowledge suspend request");
        watchdog = 60;
        while (!strcmp(state, "suspend") && watchdog > 0) {
            usleep(100000);

            state = libxl__domain_pvcontrol_read(gc, XBT_NULL, domid);
            if (!state) state = "";

            watchdog--;
        }

        /*
         * Guest appears to not be responding. Cancel the suspend
         * request.
         *
         * We re-read the suspend node and clear it within a
         * transaction in order to handle the case where we race
         * against the guest catching up and acknowledging the request
         * at the last minute.
         */
        if (!strcmp(state, "suspend")) {
            LOG(ERROR, "guest didn't acknowledge suspend, cancelling request");
        retry_transaction:
            t = xs_transaction_start(CTX->xsh);

            state = libxl__domain_pvcontrol_read(gc, t, domid);
            if (!state) state = "";

            if (!strcmp(state, "suspend"))
                libxl__domain_pvcontrol_write(gc, t, domid, "");

            if (!xs_transaction_end(CTX->xsh, t, 0))
                if (errno == EAGAIN)
                    goto retry_transaction;

        }

        /*
         * Final check for guest acknowledgement. The guest may have
         * acknowledged while we were cancelling the request in which
         * case we lost the race while cancelling and should continue.
         */
        if (!strcmp(state, "suspend")) {
            LOG(ERROR, "guest didn't acknowledge suspend, request cancelled");
            return 0;
        }

        LOG(DEBUG, "guest acknowledged suspend request");
        dss->guest_responded = 1;
    }

    LOG(DEBUG, "wait for the guest to suspend");
    watchdog = 60;
    while (watchdog > 0) {
        xc_domaininfo_t info;

        usleep(100000);
        ret = xc_domain_getinfolist(CTX->xch, domid, 1, &info);
        if (ret == 1 && info.domain == domid &&
            (info.flags & XEN_DOMINF_shutdown)) {
            int shutdown_reason;

            shutdown_reason = (info.flags >> XEN_DOMINF_shutdownshift)
                & XEN_DOMINF_shutdownmask;
            if (shutdown_reason == SHUTDOWN_suspend) {
                LOG(DEBUG, "guest has suspended");
                goto guest_suspended;
            }
        }

        watchdog--;
    }

    LOG(ERROR, "guest did not suspend");
    return 0;

 guest_suspended:
    if (dss->hvm) {
        ret = libxl__domain_suspend_device_model(gc, dss);
        if (ret) {
            LOG(ERROR, "libxl__domain_suspend_device_model failed ret=%d", ret);
            return 0;
        }
    }
    return 1;
}

static inline char *physmap_path(libxl__gc *gc, uint32_t domid,
        char *phys_offset, char *node)
{
    return libxl__sprintf(gc,
            "/local/domain/0/device-model/%d/physmap/%s/%s",
            domid, phys_offset, node);
}

int libxl__toolstack_save(uint32_t domid, uint8_t **buf,
        uint32_t *len, void *dss_void)
{
    libxl__domain_suspend_state *dss = dss_void;
    STATE_AO_GC(dss->ao);
    int i = 0;
    char *start_addr = NULL, *size = NULL, *phys_offset = NULL, *name = NULL;
    unsigned int num = 0;
    uint32_t count = 0, version = TOOLSTACK_SAVE_VERSION, namelen = 0;
    uint8_t *ptr = NULL;
    char **entries = NULL;
    struct libxl__physmap_info *pi;

    entries = libxl__xs_directory(gc, 0, libxl__sprintf(gc,
                "/local/domain/0/device-model/%d/physmap", domid), &num);
    count = num;

    *len = sizeof(version) + sizeof(count);
    *buf = calloc(1, *len);
    ptr = *buf;
    if (*buf == NULL)
        return -1;

    memcpy(ptr, &version, sizeof(version));
    ptr += sizeof(version);
    memcpy(ptr, &count, sizeof(count));
    ptr += sizeof(count);

    for (i = 0; i < count; i++) {
        unsigned long offset;
        char *xs_path;
        phys_offset = entries[i];
        if (phys_offset == NULL) {
            LOG(ERROR, "phys_offset %d is NULL", i);
            return -1;
        }

        xs_path = physmap_path(gc, domid, phys_offset, "start_addr");
        start_addr = libxl__xs_read(gc, 0, xs_path);
        if (start_addr == NULL) {
            LOG(ERROR, "%s is NULL", xs_path);
            return -1;
        }

        xs_path = physmap_path(gc, domid, phys_offset, "size");
        size = libxl__xs_read(gc, 0, xs_path);
        if (size == NULL) {
            LOG(ERROR, "%s is NULL", xs_path);
            return -1;
        }

        xs_path = physmap_path(gc, domid, phys_offset, "name");
        name = libxl__xs_read(gc, 0, xs_path);
        if (name == NULL)
            namelen = 0;
        else
            namelen = strlen(name) + 1;
        *len += namelen + sizeof(struct libxl__physmap_info);
        offset = ptr - (*buf);
        *buf = realloc(*buf, *len);
        if (*buf == NULL)
            return -1;
        ptr = (*buf) + offset;
        pi = (struct libxl__physmap_info *) ptr;
        pi->phys_offset = strtoll(phys_offset, NULL, 16);
        pi->start_addr = strtoll(start_addr, NULL, 16);
        pi->size = strtoll(size, NULL, 16);
        pi->namelen = namelen;
        memcpy(pi->name, name, namelen);
        ptr += sizeof(struct libxl__physmap_info) + namelen;
    }

    LOG(DEBUG,"domain=%"PRIu32" toolstack data size=%"PRIu32, domid, *len);

    return 0;
}

/*----- remus callbacks -----*/

static int libxl__remus_domain_suspend_callback(void *data)
{
    /* REMUS TODO: Issue disk and network checkpoint reqs. */
    return libxl__domain_suspend_common_callback(data);
}

static int libxl__remus_domain_resume_callback(void *data)
{
    libxl__save_helper_state *shs = data;
    libxl__domain_suspend_state *dss = CONTAINER_OF(shs, *dss, shs);
    STATE_AO_GC(dss->ao);

    /* Resumes the domain and the device model */
    if (libxl__domain_resume(gc, dss->domid, /* Fast Suspend */1))
        return 0;

    /* REMUS TODO: Deal with disk. Start a new network output buffer */
    return 1;
}

/*----- remus asynchronous checkpoint callback -----*/

static void remus_checkpoint_dm_saved(libxl__egc *egc,
                                      libxl__domain_suspend_state *dss, int rc);

static void libxl__remus_domain_checkpoint_callback(void *data)
{
    libxl__save_helper_state *shs = data;
    libxl__domain_suspend_state *dss = CONTAINER_OF(shs, *dss, shs);
    libxl__egc *egc = dss->shs.egc;
    STATE_AO_GC(dss->ao);

    /* This would go into tailbuf. */
    if (dss->hvm) {
        libxl__domain_save_device_model(egc, dss, remus_checkpoint_dm_saved);
    } else {
        remus_checkpoint_dm_saved(egc, dss, 0);
    }
}

static void remus_checkpoint_dm_saved(libxl__egc *egc,
                                      libxl__domain_suspend_state *dss, int rc)
{
    /* REMUS TODO: Wait for disk and memory ack, release network buffer */
    /* REMUS TODO: make this asynchronous */
    assert(!rc); /* REMUS TODO handle this error properly */
    usleep(dss->interval * 1000);
    libxl__xc_domain_saverestore_async_callback_done(egc, &dss->shs, 1);
}

/*----- main code for suspending, in order of execution -----*/

void libxl__domain_suspend(libxl__egc *egc, libxl__domain_suspend_state *dss)
{
    STATE_AO_GC(dss->ao);
    int port;
    int rc = ERROR_FAIL;
    unsigned long vm_generationid_addr;

    /* Convenience aliases */
    const uint32_t domid = dss->domid;
    const libxl_domain_type type = dss->type;
    const int live = dss->live;
    const int debug = dss->debug;
    const libxl_domain_remus_info *const r_info = dss->remus;
    libxl__srm_save_autogen_callbacks *const callbacks =
        &dss->shs.callbacks.save.a;

    logdirty_init(&dss->logdirty);

    switch (type) {
    case LIBXL_DOMAIN_TYPE_HVM: {
        char *path;
        char *addr;

        path = libxl__sprintf(gc, "%s/hvmloader/generation-id-address",
                              libxl__xs_get_dompath(gc, domid));
        addr = libxl__xs_read(gc, XBT_NULL, path);

        vm_generationid_addr = (addr) ? strtoul(addr, NULL, 0) : 0;
        dss->hvm = 1;
        break;
    }
    case LIBXL_DOMAIN_TYPE_PV:
        vm_generationid_addr = 0;
        dss->hvm = 0;
        break;
    default:
        abort();
    }

    dss->xcflags = (live) ? XCFLAGS_LIVE : 0
          | (debug) ? XCFLAGS_DEBUG : 0
          | (dss->hvm) ? XCFLAGS_HVM : 0;

    dss->suspend_eventchn = -1;
    dss->guest_responded = 0;
    dss->dm_savefile = libxl__device_model_savefile(gc, domid);

    if (r_info != NULL) {
        dss->interval = r_info->interval;
        if (r_info->compression)
            dss->xcflags |= XCFLAGS_CHECKPOINT_COMPRESS;
    }

    dss->xce = xc_evtchn_open(NULL, 0);
    if (dss->xce == NULL)
        goto out;
    else
    {
        port = xs_suspend_evtchn_port(dss->domid);

        if (port >= 0) {
            dss->suspend_eventchn =
                xc_suspend_evtchn_init(CTX->xch, dss->xce, dss->domid, port);

            if (dss->suspend_eventchn < 0)
                LOG(WARN, "Suspend event channel initialization failed");
        }
    }

    memset(callbacks, 0, sizeof(*callbacks));
    if (r_info != NULL) {
        callbacks->suspend = libxl__remus_domain_suspend_callback;
        callbacks->postcopy = libxl__remus_domain_resume_callback;
        callbacks->checkpoint = libxl__remus_domain_checkpoint_callback;
    } else
        callbacks->suspend = libxl__domain_suspend_common_callback;

    callbacks->switch_qemu_logdirty = libxl__domain_suspend_common_switch_qemu_logdirty;
    dss->shs.callbacks.save.toolstack_save = libxl__toolstack_save;

    libxl__xc_domain_save(egc, dss, vm_generationid_addr);
    return;

 out:
    domain_suspend_done(egc, dss, rc);
}

void libxl__xc_domain_save_done(libxl__egc *egc, void *dss_void,
                                int rc, int retval, int errnoval)
{
    libxl__domain_suspend_state *dss = dss_void;
    STATE_AO_GC(dss->ao);

    /* Convenience aliases */
    const libxl_domain_type type = dss->type;

    if (rc)
        goto out;

    if (retval) {
        LOGEV(ERROR, errnoval, "saving domain: %s",
                         dss->guest_responded ?
                         "domain responded to suspend request" :
                         "domain did not respond to suspend request");
        if ( !dss->guest_responded )
            rc = ERROR_GUEST_TIMEDOUT;
        else
            rc = ERROR_FAIL;
        goto out;
    }

    if (type == LIBXL_DOMAIN_TYPE_HVM) {
        rc = libxl__domain_suspend_device_model(gc, dss);
        if (rc) goto out;
        
        libxl__domain_save_device_model(egc, dss, domain_suspend_done);
        return;
    }

    rc = 0;

out:
    domain_suspend_done(egc, dss, rc);
}

static void save_device_model_datacopier_done(libxl__egc *egc,
     libxl__datacopier_state *dc, int onwrite, int errnoval);

void libxl__domain_save_device_model(libxl__egc *egc,
                                     libxl__domain_suspend_state *dss,
                                     libxl__save_device_model_cb *callback)
{
    STATE_AO_GC(dss->ao);
    struct stat st;
    uint32_t qemu_state_len;
    int rc;

    dss->save_dm_callback = callback;

    /* Convenience aliases */
    const char *const filename = dss->dm_savefile;
    const int fd = dss->fd;

    libxl__datacopier_state *dc = &dss->save_dm_datacopier;
    memset(dc, 0, sizeof(*dc));
    dc->readwhat = GCSPRINTF("qemu save file %s", filename);
    dc->ao = ao;
    dc->readfd = -1;
    dc->writefd = fd;
    dc->maxsz = INT_MAX;
    dc->copywhat = GCSPRINTF("qemu save file for domain %"PRIu32, dss->domid);
    dc->writewhat = "save/migration stream";
    dc->callback = save_device_model_datacopier_done;

    dc->readfd = open(filename, O_RDONLY);
    if (dc->readfd < 0) {
        LOGE(ERROR, "unable to open %s", dc->readwhat);
        goto out;
    }

    if (fstat(dc->readfd, &st))
    {
        LOGE(ERROR, "unable to fstat %s", dc->readwhat);
        goto out;
    }

    if (!S_ISREG(st.st_mode)) {
        LOG(ERROR, "%s is not a plain file!", dc->readwhat);
        goto out;
    }

    qemu_state_len = st.st_size;
    LOG(DEBUG, "%s is %d bytes", dc->readwhat, qemu_state_len);

    rc = libxl__datacopier_start(dc);
    if (rc) goto out;

    libxl__datacopier_prefixdata(egc, dc,
                                 QEMU_SIGNATURE, strlen(QEMU_SIGNATURE));

    libxl__datacopier_prefixdata(egc, dc,
                                 &qemu_state_len, sizeof(qemu_state_len));
    return;

 out:
    save_device_model_datacopier_done(egc, dc, -1, 0);
}

static void save_device_model_datacopier_done(libxl__egc *egc,
     libxl__datacopier_state *dc, int onwrite, int errnoval)
{
    libxl__domain_suspend_state *dss =
        CONTAINER_OF(dc, *dss, save_dm_datacopier);
    STATE_AO_GC(dss->ao);

    /* Convenience aliases */
    const char *const filename = dss->dm_savefile;
    int our_rc = 0;
    int rc;

    libxl__datacopier_kill(dc);

    if (onwrite || errnoval)
        our_rc = ERROR_FAIL;

    if (dc->readfd >= 0) {
        close(dc->readfd);
        dc->readfd = -1;
    }

    rc = libxl__remove_file(gc, filename);
    if (!our_rc) our_rc = rc;

    dss->save_dm_callback(egc, dss, our_rc);
}

static void domain_suspend_done(libxl__egc *egc,
                        libxl__domain_suspend_state *dss, int rc)
{
    STATE_AO_GC(dss->ao);

    /* Convenience aliases */
    const uint32_t domid = dss->domid;

    if (dss->suspend_eventchn > 0)
        xc_suspend_evtchn_release(CTX->xch, dss->xce, domid,
                                  dss->suspend_eventchn);
    if (dss->xce != NULL)
        xc_evtchn_close(dss->xce);

    dss->callback(egc, dss, rc);
}

/*==================== Miscellaneous ====================*/

char *libxl__uuid2string(libxl__gc *gc, const libxl_uuid uuid)
{
    char *s = libxl__sprintf(gc, LIBXL_UUID_FMT, LIBXL_UUID_BYTES(uuid));
    if (!s)
        LIBXL__LOG(libxl__gc_owner(gc), LIBXL__LOG_ERROR, "cannot allocate for uuid");
    return s;
}

static const char *userdata_path(libxl__gc *gc, uint32_t domid,
                                      const char *userdata_userid,
                                      const char *wh)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    char *path, *uuid_string;
    libxl_dominfo info;
    int rc;

    rc = libxl_domain_info(ctx, &info, domid);
    if (rc) {
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "unable to find domain info"
                     " for domain %"PRIu32, domid);
        return NULL;
    }
    uuid_string = libxl__sprintf(gc, LIBXL_UUID_FMT, LIBXL_UUID_BYTES(info.uuid));

    path = libxl__sprintf(gc, "/var/lib/xen/"
                         "userdata-%s.%u.%s.%s",
                         wh, domid, uuid_string, userdata_userid);
    if (!path)
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "unable to allocate for"
                     " userdata path");
    return path;
}

static int userdata_delete(libxl__gc *gc, const char *path)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    int r;
    r = unlink(path);
    if (r) {
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "remove failed for %s", path);
        return errno;
    }
    return 0;
}

void libxl__userdata_destroyall(libxl__gc *gc, uint32_t domid)
{
    libxl_ctx *ctx = libxl__gc_owner(gc);
    const char *pattern;
    glob_t gl;
    int r, i;

    pattern = userdata_path(gc, domid, "*", "?");
    if (!pattern)
        goto out;

    gl.gl_pathc = 0;
    gl.gl_pathv = 0;
    gl.gl_offs = 0;
    r = glob(pattern, GLOB_ERR|GLOB_NOSORT|GLOB_MARK, 0, &gl);
    if (r == GLOB_NOMATCH)
        goto out;
    if (r)
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "glob failed for %s", pattern);

    for (i=0; i<gl.gl_pathc; i++) {
        userdata_delete(gc, gl.gl_pathv[i]);
    }
    globfree(&gl);
out:
    return;
}

int libxl_userdata_store(libxl_ctx *ctx, uint32_t domid,
                              const char *userdata_userid,
                              const uint8_t *data, int datalen)
{
    GC_INIT(ctx);
    const char *filename;
    const char *newfilename;
    int e, rc;
    int fd = -1;
    FILE *f = NULL;
    size_t rs;

    filename = userdata_path(gc, domid, userdata_userid, "d");
    if (!filename) {
        rc = ERROR_NOMEM;
        goto out;
    }

    if (!datalen) {
        rc = userdata_delete(gc, filename);
        goto out;
    }

    newfilename = userdata_path(gc, domid, userdata_userid, "n");
    if (!newfilename) {
        rc = ERROR_NOMEM;
        goto out;
    }

    rc = ERROR_FAIL;

    fd= open(newfilename, O_RDWR|O_CREAT|O_TRUNC, 0600);
    if (fd<0)
        goto err;

    f= fdopen(fd, "wb");
    if (!f)
        goto err;
    fd = -1;

    rs = fwrite(data, 1, datalen, f);
    if (rs != datalen) {
        assert(ferror(f));
        goto err;
    }

    if (fclose(f))
        goto err;
    f = 0;

    if (rename(newfilename,filename))
        goto err;

    rc = 0;

err:
    e = errno;
    if (f) fclose(f);
    if (fd>=0) close(fd);

    errno = e;
    if ( rc )
        LIBXL__LOG_ERRNO(ctx, LIBXL__LOG_ERROR, "cannot write %s for %s",
                 newfilename, filename);
out:
    GC_FREE;
    return rc;
}

int libxl_userdata_retrieve(libxl_ctx *ctx, uint32_t domid,
                                 const char *userdata_userid,
                                 uint8_t **data_r, int *datalen_r)
{
    GC_INIT(ctx);
    const char *filename;
    int e, rc;
    int datalen = 0;
    void *data = 0;

    filename = userdata_path(gc, domid, userdata_userid, "d");
    if (!filename) {
        rc = ERROR_NOMEM;
        goto out;
    }

    e = libxl_read_file_contents(ctx, filename, data_r ? &data : 0, &datalen);
    if (e && errno != ENOENT) {
        rc = ERROR_FAIL;
        goto out;
    }
    if (!e && !datalen) {
        LIBXL__LOG(ctx, LIBXL__LOG_ERROR, "userdata file %s is empty", filename);
        if (data_r) assert(!*data_r);
        rc = ERROR_FAIL;
        goto out;
    }

    if (data_r) *data_r = data;
    if (datalen_r) *datalen_r = datalen;
    rc = 0;
out:
    GC_FREE;
    return rc;
}

/*
 * Local variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
