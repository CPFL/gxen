/*
 *  This file contains the XSM hook definitions for Xen.
 *
 *  This work is based on the LSM implementation in Linux 2.6.13.4.
 *
 *  Author:  George Coker, <gscoker@alpha.ncsc.mil>
 *
 *  Contributors: Michael LeMay, <mdlemay@epoch.ncsc.mil>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2,
 *  as published by the Free Software Foundation.
 */

#ifndef __XSM_H__
#define __XSM_H__

#include <xen/sched.h>
#include <xen/multiboot.h>

typedef void xsm_op_t;
DEFINE_XEN_GUEST_HANDLE(xsm_op_t);

#ifdef XSM_ENABLE
    #define xsm_call(fn) xsm_ops->fn
#else
    #define xsm_call(fn) 0
#endif

/* policy magic number (defined by XSM_MAGIC) */
typedef u32 xsm_magic_t;
#ifndef XSM_MAGIC
#define XSM_MAGIC 0x00000000
#endif

#ifdef XSM_ENABLE

extern char *policy_buffer;
extern u32 policy_size;

typedef int (*xsm_initcall_t)(void);

extern xsm_initcall_t __xsm_initcall_start[], __xsm_initcall_end[];

#define xsm_initcall(fn) \
    static xsm_initcall_t __initcall_##fn \
    __used_section(".xsm_initcall.init") = fn

struct xsm_operations {
    void (*security_domaininfo) (struct domain *d,
                                        struct xen_domctl_getdomaininfo *info);
    int (*setvcpucontext) (struct domain *d);
    int (*pausedomain) (struct domain *d);
    int (*unpausedomain) (struct domain *d);
    int (*resumedomain) (struct domain *d);
    int (*domain_create) (struct domain *d, u32 ssidref);
    int (*max_vcpus) (struct domain *d);
    int (*destroydomain) (struct domain *d);
    int (*vcpuaffinity) (int cmd, struct domain *d);
    int (*scheduler) (struct domain *d);
    int (*getdomaininfo) (struct domain *d);
    int (*getvcpucontext) (struct domain *d);
    int (*getvcpuinfo) (struct domain *d);
    int (*domain_settime) (struct domain *d);
    int (*set_target) (struct domain *d, struct domain *e);
    int (*domctl) (struct domain *d, int cmd);
    int (*set_virq_handler) (struct domain *d, uint32_t virq);
    int (*tbufcontrol) (void);
    int (*readconsole) (uint32_t clear);
    int (*sched_id) (void);
    int (*setdomainmaxmem) (struct domain *d);
    int (*setdomainhandle) (struct domain *d);
    int (*setdebugging) (struct domain *d);
    int (*perfcontrol) (void);
    int (*debug_keys) (void);
    int (*getcpuinfo) (void);
    int (*availheap) (void);
    int (*get_pmstat) (void);
    int (*setpminfo) (void);
    int (*pm_op) (void);
    int (*do_mca) (void);

    int (*evtchn_unbound) (struct domain *d, struct evtchn *chn, domid_t id2);
    int (*evtchn_interdomain) (struct domain *d1, struct evtchn *chn1,
                                        struct domain *d2, struct evtchn *chn2);
    void (*evtchn_close_post) (struct evtchn *chn);
    int (*evtchn_send) (struct domain *d, struct evtchn *chn);
    int (*evtchn_status) (struct domain *d, struct evtchn *chn);
    int (*evtchn_reset) (struct domain *d1, struct domain *d2);

    int (*grant_mapref) (struct domain *d1, struct domain *d2, uint32_t flags);
    int (*grant_unmapref) (struct domain *d1, struct domain *d2);
    int (*grant_setup) (struct domain *d1, struct domain *d2);
    int (*grant_transfer) (struct domain *d1, struct domain *d2);
    int (*grant_copy) (struct domain *d1, struct domain *d2);
    int (*grant_query_size) (struct domain *d1, struct domain *d2);

    int (*alloc_security_domain) (struct domain *d);
    void (*free_security_domain) (struct domain *d);
    int (*alloc_security_evtchn) (struct evtchn *chn);
    void (*free_security_evtchn) (struct evtchn *chn);
    char *(*show_security_evtchn) (struct domain *d, const struct evtchn *chn);

    int (*get_pod_target) (struct domain *d);
    int (*set_pod_target) (struct domain *d);
    int (*memory_adjust_reservation) (struct domain *d1, struct domain *d2);
    int (*memory_stat_reservation) (struct domain *d1, struct domain *d2);
    int (*memory_pin_page) (struct domain *d1, struct domain *d2, struct page_info *page);
    int (*remove_from_physmap) (struct domain *d1, struct domain *d2);

    int (*console_io) (struct domain *d, int cmd);

    int (*profile) (struct domain *d, int op);

    int (*kexec) (void);
    int (*schedop_shutdown) (struct domain *d1, struct domain *d2);

    char *(*show_irq_sid) (int irq);
    int (*map_domain_pirq) (struct domain *d, int irq, void *data);
    int (*irq_permission) (struct domain *d, int pirq, uint8_t allow);
    int (*iomem_permission) (struct domain *d, uint64_t s, uint64_t e, uint8_t allow);
    int (*pci_config_permission) (struct domain *d, uint32_t machine_bdf, uint16_t start, uint16_t end, uint8_t access);

    int (*get_device_group) (uint32_t machine_bdf);
    int (*test_assign_device) (uint32_t machine_bdf);
    int (*assign_device) (struct domain *d, uint32_t machine_bdf);
    int (*deassign_device) (struct domain *d, uint32_t machine_bdf);

    int (*resource_plug_core) (void);
    int (*resource_unplug_core) (void);
    int (*resource_plug_pci) (uint32_t machine_bdf);
    int (*resource_unplug_pci) (uint32_t machine_bdf);
    int (*resource_setup_pci) (uint32_t machine_bdf);
    int (*resource_setup_gsi) (int gsi);
    int (*resource_setup_misc) (void);

    int (*page_offline)(uint32_t cmd);
    int (*lockprof)(void);
    int (*cpupool_op)(void);
    int (*sched_op)(void);

    long (*__do_xsm_op) (XEN_GUEST_HANDLE(xsm_op_t) op);

#ifdef CONFIG_X86
    int (*shadow_control) (struct domain *d, uint32_t op);
    int (*getpageframeinfo) (struct domain *d);
    int (*getmemlist) (struct domain *d);
    int (*hypercall_init) (struct domain *d);
    int (*hvmcontext) (struct domain *d, uint32_t op);
    int (*address_size) (struct domain *d, uint32_t op);
    int (*machine_address_size) (struct domain *d, uint32_t op);
    int (*hvm_param) (struct domain *d, unsigned long op);
    int (*hvm_set_pci_intx_level) (struct domain *d);
    int (*hvm_set_isa_irq_level) (struct domain *d);
    int (*hvm_set_pci_link_route) (struct domain *d);
    int (*hvm_inject_msi) (struct domain *d);
    int (*mem_event) (struct domain *d);
    int (*mem_sharing) (struct domain *d);
    int (*apic) (struct domain *d, int cmd);
    int (*xen_settime) (void);
    int (*memtype) (uint32_t access);
    int (*microcode) (void);
    int (*physinfo) (void);
    int (*platform_quirk) (uint32_t);
    int (*firmware_info) (void);
    int (*efi_call) (void);
    int (*acpi_sleep) (void);
    int (*change_freq) (void);
    int (*getidletime) (void);
    int (*machine_memory_map) (void);
    int (*domain_memory_map) (struct domain *d);
    int (*mmu_normal_update) (struct domain *d, struct domain *t,
                              struct domain *f, intpte_t fpte);
    int (*mmu_machphys_update) (struct domain *d1, struct domain *d2, unsigned long mfn);
    int (*update_va_mapping) (struct domain *d, struct domain *f, l1_pgentry_t pte);
    int (*add_to_physmap) (struct domain *d1, struct domain *d2);
    int (*sendtrigger) (struct domain *d);
    int (*bind_pt_irq) (struct domain *d, struct xen_domctl_bind_pt_irq *bind);
    int (*unbind_pt_irq) (struct domain *d);
    int (*pin_mem_cacheattr) (struct domain *d);
    int (*ext_vcpucontext) (struct domain *d, uint32_t cmd);
    int (*vcpuextstate) (struct domain *d, uint32_t cmd);
    int (*ioport_permission) (struct domain *d, uint32_t s, uint32_t e, uint8_t allow);
#endif
};

#endif

extern struct xsm_operations *xsm_ops;

static inline void xsm_security_domaininfo (struct domain *d,
                                        struct xen_domctl_getdomaininfo *info)
{
    (void)xsm_call(security_domaininfo(d, info));
}

static inline int xsm_setvcpucontext(struct domain *d)
{
    return xsm_call(setvcpucontext(d));
}

static inline int xsm_pausedomain (struct domain *d)
{
    return xsm_call(pausedomain(d));
}

static inline int xsm_unpausedomain (struct domain *d)
{
    return xsm_call(unpausedomain(d));
}

static inline int xsm_resumedomain (struct domain *d)
{
    return xsm_call(resumedomain(d));
}

static inline int xsm_domain_create (struct domain *d, u32 ssidref)
{
    return xsm_call(domain_create(d, ssidref));
}

static inline int xsm_max_vcpus(struct domain *d)
{
    return xsm_call(max_vcpus(d));
}

static inline int xsm_destroydomain (struct domain *d)
{
    return xsm_call(destroydomain(d));
}

static inline int xsm_vcpuaffinity (int cmd, struct domain *d)
{
    return xsm_call(vcpuaffinity(cmd, d));
}

static inline int xsm_scheduler (struct domain *d)
{
    return xsm_call(scheduler(d));
}

static inline int xsm_getdomaininfo (struct domain *d)
{
    return xsm_call(getdomaininfo(d));
}

static inline int xsm_getvcpucontext (struct domain *d)
{
    return xsm_call(getvcpucontext(d));
}

static inline int xsm_getvcpuinfo (struct domain *d)
{
    return xsm_call(getvcpuinfo(d));
}

static inline int xsm_domain_settime (struct domain *d)
{
    return xsm_call(domain_settime(d));
}

static inline int xsm_set_target (struct domain *d, struct domain *e)
{
    return xsm_call(set_target(d, e));
}

static inline int xsm_domctl (struct domain *d, int cmd)
{
    return xsm_call(domctl(d, cmd));
}

static inline int xsm_set_virq_handler (struct domain *d, uint32_t virq)
{
    return xsm_call(set_virq_handler(d, virq));
}

static inline int xsm_tbufcontrol (void)
{
    return xsm_call(tbufcontrol());
}

static inline int xsm_readconsole (uint32_t clear)
{
    return xsm_call(readconsole(clear));
}

static inline int xsm_sched_id (void)
{
    return xsm_call(sched_id());
}

static inline int xsm_setdomainmaxmem (struct domain *d)
{
    return xsm_call(setdomainmaxmem(d));
}

static inline int xsm_setdomainhandle (struct domain *d)
{
    return xsm_call(setdomainhandle(d));
}

static inline int xsm_setdebugging (struct domain *d)
{
    return xsm_call(setdebugging(d));
}

static inline int xsm_perfcontrol (void)
{
    return xsm_call(perfcontrol());
}

static inline int xsm_debug_keys (void)
{
    return xsm_call(debug_keys());
}

static inline int xsm_availheap (void)
{
    return xsm_call(availheap());
}

static inline int xsm_getcpuinfo (void)
{
    return xsm_call(getcpuinfo());
}

static inline int xsm_get_pmstat(void)
{
    return xsm_call(get_pmstat());
}

static inline int xsm_setpminfo(void)
{
	return xsm_call(setpminfo());
}

static inline int xsm_pm_op(void)
{
    return xsm_call(pm_op());
}

static inline int xsm_do_mca(void)
{
    return xsm_call(do_mca());
}

static inline int xsm_evtchn_unbound (struct domain *d1, struct evtchn *chn,
                                                                    domid_t id2)
{
    return xsm_call(evtchn_unbound(d1, chn, id2));
}

static inline int xsm_evtchn_interdomain (struct domain *d1, 
                struct evtchn *chan1, struct domain *d2, struct evtchn *chan2)
{
    return xsm_call(evtchn_interdomain(d1, chan1, d2, chan2));
}

static inline void xsm_evtchn_close_post (struct evtchn *chn)
{
    (void)xsm_call(evtchn_close_post(chn));
}

static inline int xsm_evtchn_send (struct domain *d, struct evtchn *chn)
{
    return xsm_call(evtchn_send(d, chn));
}

static inline int xsm_evtchn_status (struct domain *d, struct evtchn *chn)
{
    return xsm_call(evtchn_status(d, chn));
}

static inline int xsm_evtchn_reset (struct domain *d1, struct domain *d2)
{
    return xsm_call(evtchn_reset(d1, d2));
}

static inline int xsm_grant_mapref (struct domain *d1, struct domain *d2,
                                                                uint32_t flags)
{
    return xsm_call(grant_mapref(d1, d2, flags));
}

static inline int xsm_grant_unmapref (struct domain *d1, struct domain *d2)
{
    return xsm_call(grant_unmapref(d1, d2));
}

static inline int xsm_grant_setup (struct domain *d1, struct domain *d2)
{
    return xsm_call(grant_setup(d1, d2));
}

static inline int xsm_grant_transfer (struct domain *d1, struct domain *d2)
{
    return xsm_call(grant_transfer(d1, d2));
}

static inline int xsm_grant_copy (struct domain *d1, struct domain *d2)
{
    return xsm_call(grant_copy(d1, d2));
}

static inline int xsm_grant_query_size (struct domain *d1, struct domain *d2)
{
    return xsm_call(grant_query_size(d1, d2));
}

static inline int xsm_alloc_security_domain (struct domain *d)
{
    return xsm_call(alloc_security_domain(d));
}

static inline void xsm_free_security_domain (struct domain *d)
{
    (void)xsm_call(free_security_domain(d));
}

static inline int xsm_alloc_security_evtchn (struct evtchn *chn)
{
    return xsm_call(alloc_security_evtchn(chn));
}

static inline void xsm_free_security_evtchn (struct evtchn *chn)
{
    (void)xsm_call(free_security_evtchn(chn));
}

static inline char *xsm_show_security_evtchn (struct domain *d, const struct evtchn *chn)
{
    return xsm_call(show_security_evtchn(d, chn));
}

static inline int xsm_get_pod_target (struct domain *d)
{
    return xsm_call(get_pod_target(d));
}

static inline int xsm_set_pod_target (struct domain *d)
{
    return xsm_call(set_pod_target(d));
}

static inline int xsm_memory_adjust_reservation (struct domain *d1, struct
                                                                    domain *d2)
{
    return xsm_call(memory_adjust_reservation(d1, d2));
}

static inline int xsm_memory_stat_reservation (struct domain *d1,
                                                            struct domain *d2)
{
    return xsm_call(memory_stat_reservation(d1, d2));
}

static inline int xsm_memory_pin_page(struct domain *d1, struct domain *d2,
                                      struct page_info *page)
{
    return xsm_call(memory_pin_page(d1, d2, page));
}

static inline int xsm_remove_from_physmap(struct domain *d1, struct domain *d2)
{
    return xsm_call(remove_from_physmap(d1, d2));
}

static inline int xsm_console_io (struct domain *d, int cmd)
{
    return xsm_call(console_io(d, cmd));
}

static inline int xsm_profile (struct domain *d, int op)
{
    return xsm_call(profile(d, op));
}

static inline int xsm_kexec (void)
{
    return xsm_call(kexec());
}

static inline int xsm_schedop_shutdown (struct domain *d1, struct domain *d2)
{
    return xsm_call(schedop_shutdown(d1, d2));
}

static inline char *xsm_show_irq_sid (int irq)
{
    return xsm_call(show_irq_sid(irq));
}

static inline int xsm_map_domain_pirq (struct domain *d, int irq, void *data)
{
    return xsm_call(map_domain_pirq(d, irq, data));
}

static inline int xsm_irq_permission (struct domain *d, int pirq, uint8_t allow)
{
    return xsm_call(irq_permission(d, pirq, allow));
}

static inline int xsm_iomem_permission (struct domain *d, uint64_t s, uint64_t e, uint8_t allow)
{
    return xsm_call(iomem_permission(d, s, e, allow));
}

static inline int xsm_pci_config_permission (struct domain *d, uint32_t machine_bdf, uint16_t start, uint16_t end, uint8_t access)
{
    return xsm_call(pci_config_permission(d, machine_bdf, start, end, access));
}

static inline int xsm_get_device_group(uint32_t machine_bdf)
{
    return xsm_call(get_device_group(machine_bdf));
}

static inline int xsm_test_assign_device(uint32_t machine_bdf)
{
    return xsm_call(test_assign_device(machine_bdf));
}

static inline int xsm_assign_device(struct domain *d, uint32_t machine_bdf)
{
    return xsm_call(assign_device(d, machine_bdf));
}

static inline int xsm_deassign_device(struct domain *d, uint32_t machine_bdf)
{
    return xsm_call(deassign_device(d, machine_bdf));
}

static inline int xsm_resource_plug_pci (uint32_t machine_bdf)
{
    return xsm_call(resource_plug_pci(machine_bdf));
}

static inline int xsm_resource_unplug_pci (uint32_t machine_bdf)
{
    return xsm_call(resource_unplug_pci(machine_bdf));
}

static inline int xsm_resource_plug_core (void)
{
    return xsm_call(resource_plug_core());
}

static inline int xsm_resource_unplug_core (void)
{
    return xsm_call(resource_unplug_core());
}

static inline int xsm_resource_setup_pci (uint32_t machine_bdf)
{
    return xsm_call(resource_setup_pci(machine_bdf));
}

static inline int xsm_resource_setup_gsi (int gsi)
{
    return xsm_call(resource_setup_gsi(gsi));
}

static inline int xsm_resource_setup_misc (void)
{
    return xsm_call(resource_setup_misc());
}

static inline int xsm_page_offline(uint32_t cmd)
{
    return xsm_call(page_offline(cmd));
}

static inline int xsm_lockprof(void)
{
    return xsm_call(lockprof());
}

static inline int xsm_cpupool_op(void)
{
    return xsm_call(cpupool_op());
}

static inline int xsm_sched_op(void)
{
    return xsm_call(sched_op());
}

static inline long __do_xsm_op (XEN_GUEST_HANDLE(xsm_op_t) op)
{
#ifdef XSM_ENABLE
    return xsm_ops->__do_xsm_op(op);
#else
    return -ENOSYS;
#endif
}

#ifdef XSM_ENABLE
extern int xsm_init(unsigned long *module_map, const multiboot_info_t *mbi,
                    void *(*bootstrap_map)(const module_t *));
extern int xsm_policy_init(unsigned long *module_map,
                           const multiboot_info_t *mbi,
                           void *(*bootstrap_map)(const module_t *));
extern int register_xsm(struct xsm_operations *ops);
extern int unregister_xsm(struct xsm_operations *ops);
#else
static inline int xsm_init (unsigned long *module_map,
                            const multiboot_info_t *mbi,
                            void *(*bootstrap_map)(const module_t *))
{
    return 0;
}
#endif

#ifdef CONFIG_X86
static inline int xsm_shadow_control (struct domain *d, uint32_t op)
{
    return xsm_call(shadow_control(d, op));
}

static inline int xsm_getpageframeinfo (struct domain *d)
{
    return xsm_call(getpageframeinfo(d));
}

static inline int xsm_getmemlist (struct domain *d)
{
    return xsm_call(getmemlist(d));
}

static inline int xsm_hypercall_init (struct domain *d)
{
    return xsm_call(hypercall_init(d));
}

static inline int xsm_hvmcontext (struct domain *d, uint32_t cmd)
{
    return xsm_call(hvmcontext(d, cmd));
}

static inline int xsm_address_size (struct domain *d, uint32_t cmd)
{
    return xsm_call(address_size(d, cmd));
}

static inline int xsm_machine_address_size (struct domain *d, uint32_t cmd)
{
    return xsm_call(machine_address_size(d, cmd));
}

static inline int xsm_hvm_param (struct domain *d, unsigned long op)
{
    return xsm_call(hvm_param(d, op));
}

static inline int xsm_hvm_set_pci_intx_level (struct domain *d)
{
    return xsm_call(hvm_set_pci_intx_level(d));
}

static inline int xsm_hvm_set_isa_irq_level (struct domain *d)
{
    return xsm_call(hvm_set_isa_irq_level(d));
}

static inline int xsm_hvm_set_pci_link_route (struct domain *d)
{
    return xsm_call(hvm_set_pci_link_route(d));
}

static inline int xsm_hvm_inject_msi (struct domain *d)
{
    return xsm_call(hvm_inject_msi(d));
}

static inline int xsm_mem_event (struct domain *d)
{
    return xsm_call(mem_event(d));
}

static inline int xsm_mem_sharing (struct domain *d)
{
    return xsm_call(mem_sharing(d));
}

static inline int xsm_apic (struct domain *d, int cmd)
{
    return xsm_call(apic(d, cmd));
}

static inline int xsm_xen_settime (void)
{
    return xsm_call(xen_settime());
}

static inline int xsm_memtype (uint32_t access)
{
    return xsm_call(memtype(access));
}

static inline int xsm_microcode (void)
{
    return xsm_call(microcode());
}

static inline int xsm_physinfo (void)
{
    return xsm_call(physinfo());
}

static inline int xsm_platform_quirk (uint32_t quirk)
{
    return xsm_call(platform_quirk(quirk));
}

static inline int xsm_firmware_info (void)
{
    return xsm_call(firmware_info());
}

static inline int xsm_efi_call (void)
{
    return xsm_call(efi_call());
}

static inline int xsm_acpi_sleep (void)
{
    return xsm_call(acpi_sleep());
}

static inline int xsm_change_freq (void)
{
    return xsm_call(change_freq());
}

static inline int xsm_getidletime (void)
{
    return xsm_call(getidletime());
}

static inline int xsm_machine_memory_map(void)
{
    return xsm_call(machine_memory_map());
}

static inline int xsm_domain_memory_map(struct domain *d)
{
    return xsm_call(domain_memory_map(d));
}

static inline int xsm_mmu_normal_update (struct domain *d, struct domain *t,
                                         struct domain *f, intpte_t fpte)
{
    return xsm_call(mmu_normal_update(d, t, f, fpte));
}

static inline int xsm_mmu_machphys_update (struct domain *d1, struct domain *d2,
                                           unsigned long mfn)
{
    return xsm_call(mmu_machphys_update(d1, d2, mfn));
}

static inline int xsm_update_va_mapping(struct domain *d, struct domain *f, 
                                                            l1_pgentry_t pte)
{
    return xsm_call(update_va_mapping(d, f, pte));
}

static inline int xsm_add_to_physmap(struct domain *d1, struct domain *d2)
{
    return xsm_call(add_to_physmap(d1, d2));
}

static inline int xsm_sendtrigger(struct domain *d)
{
    return xsm_call(sendtrigger(d));
}

static inline int xsm_bind_pt_irq(struct domain *d, 
                                                struct xen_domctl_bind_pt_irq *bind)
{
    return xsm_call(bind_pt_irq(d, bind));
}

static inline int xsm_unbind_pt_irq(struct domain *d)
{
    return xsm_call(unbind_pt_irq(d));
}

static inline int xsm_pin_mem_cacheattr(struct domain *d)
{
    return xsm_call(pin_mem_cacheattr(d));
}

static inline int xsm_ext_vcpucontext(struct domain *d, uint32_t cmd)
{
    return xsm_call(ext_vcpucontext(d, cmd));
}
static inline int xsm_vcpuextstate(struct domain *d, uint32_t cmd)
{
    return xsm_call(vcpuextstate(d, cmd));
}

static inline int xsm_ioport_permission (struct domain *d, uint32_t s, uint32_t e, uint8_t allow)
{
    return xsm_call(ioport_permission(d, s, e, allow));
}
#endif /* CONFIG_X86 */

extern struct xsm_operations dummy_xsm_ops;
extern void xsm_fixup_ops(struct xsm_operations *ops);

#endif /* __XSM_H */
