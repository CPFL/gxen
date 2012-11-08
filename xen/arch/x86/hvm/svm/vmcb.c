/*
 * vmcb.c: VMCB management
 * Copyright (c) 2005-2007, Advanced Micro Devices, Inc.
 * Copyright (c) 2004, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#include <xen/config.h>
#include <xen/init.h>
#include <xen/lib.h>
#include <xen/keyhandler.h>
#include <xen/mm.h>
#include <xen/rcupdate.h>
#include <xen/sched.h>
#include <asm/hvm/svm/vmcb.h>
#include <asm/msr-index.h>
#include <asm/p2m.h>
#include <asm/hvm/support.h>
#include <asm/hvm/svm/svm.h>
#include <asm/hvm/svm/svmdebug.h>

struct vmcb_struct *alloc_vmcb(void) 
{
    struct vmcb_struct *vmcb;

    vmcb = alloc_xenheap_page();
    if ( vmcb == NULL )
    {
        printk(XENLOG_WARNING "Warning: failed to allocate vmcb.\n");
        return NULL;
    }

    clear_page(vmcb);
    return vmcb;
}

void free_vmcb(struct vmcb_struct *vmcb)
{
    free_xenheap_page(vmcb);
}

struct host_save_area *alloc_host_save_area(void)
{
    struct host_save_area *hsa;

    hsa = alloc_xenheap_page();
    if ( hsa == NULL )
    {
        printk(XENLOG_WARNING "Warning: failed to allocate hsa.\n");
        return NULL;
    }

    clear_page(hsa);
    return hsa;
}

/* This function can directly access fields which are covered by clean bits. */
static int construct_vmcb(struct vcpu *v)
{
    struct arch_svm_struct *arch_svm = &v->arch.hvm_svm;
    struct vmcb_struct *vmcb = arch_svm->vmcb;

    vmcb->_general1_intercepts = 
        GENERAL1_INTERCEPT_INTR        | GENERAL1_INTERCEPT_NMI         |
        GENERAL1_INTERCEPT_SMI         | GENERAL1_INTERCEPT_INIT        |
        GENERAL1_INTERCEPT_CPUID       | GENERAL1_INTERCEPT_INVD        |
        GENERAL1_INTERCEPT_HLT         | GENERAL1_INTERCEPT_INVLPG      | 
        GENERAL1_INTERCEPT_INVLPGA     | GENERAL1_INTERCEPT_IOIO_PROT   |
        GENERAL1_INTERCEPT_MSR_PROT    | GENERAL1_INTERCEPT_SHUTDOWN_EVT|
        GENERAL1_INTERCEPT_TASK_SWITCH;
    vmcb->_general2_intercepts = 
        GENERAL2_INTERCEPT_VMRUN       | GENERAL2_INTERCEPT_VMMCALL     |
        GENERAL2_INTERCEPT_VMLOAD      | GENERAL2_INTERCEPT_VMSAVE      |
        GENERAL2_INTERCEPT_STGI        | GENERAL2_INTERCEPT_CLGI        |
        GENERAL2_INTERCEPT_SKINIT      | GENERAL2_INTERCEPT_MWAIT       |
        GENERAL2_INTERCEPT_WBINVD      | GENERAL2_INTERCEPT_MONITOR     |
        GENERAL2_INTERCEPT_XSETBV;

    /* Intercept all debug-register writes. */
    vmcb->_dr_intercepts = ~0u;

    /* Intercept all control-register accesses except for CR2 and CR8. */
    vmcb->_cr_intercepts = ~(CR_INTERCEPT_CR2_READ |
                             CR_INTERCEPT_CR2_WRITE |
                             CR_INTERCEPT_CR8_READ |
                             CR_INTERCEPT_CR8_WRITE);

    /* I/O and MSR permission bitmaps. */
    arch_svm->msrpm = alloc_xenheap_pages(get_order_from_bytes(MSRPM_SIZE), 0);
    if ( arch_svm->msrpm == NULL )
        return -ENOMEM;
    memset(arch_svm->msrpm, 0xff, MSRPM_SIZE);

    svm_disable_intercept_for_msr(v, MSR_FS_BASE);
    svm_disable_intercept_for_msr(v, MSR_GS_BASE);
    svm_disable_intercept_for_msr(v, MSR_SHADOW_GS_BASE);
    svm_disable_intercept_for_msr(v, MSR_CSTAR);
    svm_disable_intercept_for_msr(v, MSR_LSTAR);
    svm_disable_intercept_for_msr(v, MSR_STAR);
    svm_disable_intercept_for_msr(v, MSR_SYSCALL_MASK);

    /* LWP_CBADDR MSR is saved and restored by FPU code. So SVM doesn't need to
     * intercept it. */
    if ( cpu_has_lwp )
        svm_disable_intercept_for_msr(v, MSR_AMD64_LWP_CBADDR);

    vmcb->_msrpm_base_pa = (u64)virt_to_maddr(arch_svm->msrpm);
    vmcb->_iopm_base_pa  = (u64)virt_to_maddr(hvm_io_bitmap);

    /* Virtualise EFLAGS.IF and LAPIC TPR (CR8). */
    vmcb->_vintr.fields.intr_masking = 1;
  
    /* Initialise event injection to no-op. */
    vmcb->eventinj.bytes = 0;

    /* TSC. */
    vmcb->_tsc_offset = 0;

    /* Don't need to intercept RDTSC if CPU supports TSC rate scaling */
    if ( v->domain->arch.vtsc && !cpu_has_tsc_ratio )
    {
        vmcb->_general1_intercepts |= GENERAL1_INTERCEPT_RDTSC;
        vmcb->_general2_intercepts |= GENERAL2_INTERCEPT_RDTSCP;
    }

    /* Guest EFER. */
    v->arch.hvm_vcpu.guest_efer = 0;
    hvm_update_guest_efer(v);

    /* Guest segment limits. */
    vmcb->cs.limit = ~0u;
    vmcb->es.limit = ~0u;
    vmcb->ss.limit = ~0u;
    vmcb->ds.limit = ~0u;
    vmcb->fs.limit = ~0u;
    vmcb->gs.limit = ~0u;

    /* Guest segment bases. */
    vmcb->cs.base = 0;
    vmcb->es.base = 0;
    vmcb->ss.base = 0;
    vmcb->ds.base = 0;
    vmcb->fs.base = 0;
    vmcb->gs.base = 0;

    /* Guest segment AR bytes. */
    vmcb->es.attr.bytes = 0xc93; /* read/write, accessed */
    vmcb->ss.attr.bytes = 0xc93;
    vmcb->ds.attr.bytes = 0xc93;
    vmcb->fs.attr.bytes = 0xc93;
    vmcb->gs.attr.bytes = 0xc93;
    vmcb->cs.attr.bytes = 0xc9b; /* exec/read, accessed */

    /* Guest IDT. */
    vmcb->idtr.base = 0;
    vmcb->idtr.limit = 0;

    /* Guest GDT. */
    vmcb->gdtr.base = 0;
    vmcb->gdtr.limit = 0;

    /* Guest LDT. */
    vmcb->ldtr.sel = 0;
    vmcb->ldtr.base = 0;
    vmcb->ldtr.limit = 0;
    vmcb->ldtr.attr.bytes = 0;

    /* Guest TSS. */
    vmcb->tr.attr.bytes = 0x08b; /* 32-bit TSS (busy) */
    vmcb->tr.base = 0;
    vmcb->tr.limit = 0xff;

    v->arch.hvm_vcpu.guest_cr[0] = X86_CR0_PE | X86_CR0_ET;
    hvm_update_guest_cr(v, 0);

    v->arch.hvm_vcpu.guest_cr[4] = 0;
    hvm_update_guest_cr(v, 4);

    paging_update_paging_modes(v);

    vmcb->_exception_intercepts =
        HVM_TRAP_MASK
        | (1U << TRAP_no_device);

    if ( paging_mode_hap(v->domain) )
    {
        vmcb->_np_enable = 1; /* enable nested paging */
        vmcb->_g_pat = MSR_IA32_CR_PAT_RESET; /* guest PAT */
        vmcb->_h_cr3 = pagetable_get_paddr(
            p2m_get_pagetable(p2m_get_hostp2m(v->domain)));

        /* No point in intercepting CR3 reads/writes. */
        vmcb->_cr_intercepts &=
            ~(CR_INTERCEPT_CR3_READ|CR_INTERCEPT_CR3_WRITE);

        /*
         * No point in intercepting INVLPG if we don't have shadow pagetables
         * that need to be fixed up.
         */
        vmcb->_general1_intercepts &= ~GENERAL1_INTERCEPT_INVLPG;

        /* PAT is under complete control of SVM when using nested paging. */
        svm_disable_intercept_for_msr(v, MSR_IA32_CR_PAT);
    }
    else
    {
        vmcb->_exception_intercepts |= (1U << TRAP_page_fault);
    }

    if ( cpu_has_pause_filter )
    {
        vmcb->_pause_filter_count = SVM_PAUSEFILTER_INIT;
        vmcb->_general1_intercepts |= GENERAL1_INTERCEPT_PAUSE;
    }

    vmcb->cleanbits.bytes = 0;

    return 0;
}

int svm_create_vmcb(struct vcpu *v)
{
    struct nestedvcpu *nv = &vcpu_nestedhvm(v);
    struct arch_svm_struct *arch_svm = &v->arch.hvm_svm;
    int rc;

    if ( (nv->nv_n1vmcx == NULL) &&
         (nv->nv_n1vmcx = alloc_vmcb()) == NULL )
    {
        printk("Failed to create a new VMCB\n");
        return -ENOMEM;
    }

    arch_svm->vmcb = nv->nv_n1vmcx;
    rc = construct_vmcb(v);
    if ( rc != 0 )
    {
        free_vmcb(nv->nv_n1vmcx);
        nv->nv_n1vmcx = NULL;
        arch_svm->vmcb = NULL;
        return rc;
    }

    arch_svm->vmcb_pa = nv->nv_n1vmcx_pa = virt_to_maddr(arch_svm->vmcb);
    return 0;
}

void svm_destroy_vmcb(struct vcpu *v)
{
    struct nestedvcpu *nv = &vcpu_nestedhvm(v);
    struct arch_svm_struct *arch_svm = &v->arch.hvm_svm;

    if ( nv->nv_n1vmcx != NULL )
        free_vmcb(nv->nv_n1vmcx);

    if ( arch_svm->msrpm != NULL )
    {
        free_xenheap_pages(
            arch_svm->msrpm, get_order_from_bytes(MSRPM_SIZE));
        arch_svm->msrpm = NULL;
    }

    nv->nv_n1vmcx = NULL;
    nv->nv_n1vmcx_pa = VMCX_EADDR;
    arch_svm->vmcb = NULL;
}

static void vmcb_dump(unsigned char ch)
{
    struct domain *d;
    struct vcpu *v;
    
    printk("*********** VMCB Areas **************\n");

    rcu_read_lock(&domlist_read_lock);

    for_each_domain ( d )
    {
        if ( !is_hvm_domain(d) )
            continue;
        printk("\n>>> Domain %d <<<\n", d->domain_id);
        for_each_vcpu ( d, v )
        {
            printk("\tVCPU %d\n", v->vcpu_id);
            svm_vmcb_dump("key_handler", v->arch.hvm_svm.vmcb);
        }
    }

    rcu_read_unlock(&domlist_read_lock);

    printk("**************************************\n");
}

static struct keyhandler vmcb_dump_keyhandler = {
    .diagnostic = 1,
    .u.fn = vmcb_dump,
    .desc = "dump AMD-V VMCBs"
};

void setup_vmcb_dump(void)
{
    register_keyhandler('v', &vmcb_dump_keyhandler);
}

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
