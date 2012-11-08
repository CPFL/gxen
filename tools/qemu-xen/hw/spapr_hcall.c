#include "sysemu.h"
#include "cpu.h"
#include "dyngen-exec.h"
#include "qemu-char.h"
#include "sysemu.h"
#include "qemu-char.h"
#include "helper_regs.h"
#include "hw/spapr.h"

#define HPTES_PER_GROUP 8

#define HPTE_V_SSIZE_SHIFT      62
#define HPTE_V_AVPN_SHIFT       7
#define HPTE_V_AVPN             0x3fffffffffffff80ULL
#define HPTE_V_AVPN_VAL(x)      (((x) & HPTE_V_AVPN) >> HPTE_V_AVPN_SHIFT)
#define HPTE_V_COMPARE(x, y)    (!(((x) ^ (y)) & 0xffffffffffffff80UL))
#define HPTE_V_BOLTED           0x0000000000000010ULL
#define HPTE_V_LOCK             0x0000000000000008ULL
#define HPTE_V_LARGE            0x0000000000000004ULL
#define HPTE_V_SECONDARY        0x0000000000000002ULL
#define HPTE_V_VALID            0x0000000000000001ULL

#define HPTE_R_PP0              0x8000000000000000ULL
#define HPTE_R_TS               0x4000000000000000ULL
#define HPTE_R_KEY_HI           0x3000000000000000ULL
#define HPTE_R_RPN_SHIFT        12
#define HPTE_R_RPN              0x3ffffffffffff000ULL
#define HPTE_R_FLAGS            0x00000000000003ffULL
#define HPTE_R_PP               0x0000000000000003ULL
#define HPTE_R_N                0x0000000000000004ULL
#define HPTE_R_G                0x0000000000000008ULL
#define HPTE_R_M                0x0000000000000010ULL
#define HPTE_R_I                0x0000000000000020ULL
#define HPTE_R_W                0x0000000000000040ULL
#define HPTE_R_WIMG             0x0000000000000078ULL
#define HPTE_R_C                0x0000000000000080ULL
#define HPTE_R_R                0x0000000000000100ULL
#define HPTE_R_KEY_LO           0x0000000000000e00ULL

#define HPTE_V_1TB_SEG          0x4000000000000000ULL
#define HPTE_V_VRMA_MASK        0x4001ffffff000000ULL

#define HPTE_V_HVLOCK           0x40ULL

static inline int lock_hpte(void *hpte, target_ulong bits)
{
    uint64_t pteh;

    pteh = ldq_p(hpte);

    /* We're protected by qemu's global lock here */
    if (pteh & bits) {
        return 0;
    }
    stq_p(hpte, pteh | HPTE_V_HVLOCK);
    return 1;
}

static target_ulong compute_tlbie_rb(target_ulong v, target_ulong r,
                                     target_ulong pte_index)
{
    target_ulong rb, va_low;

    rb = (v & ~0x7fULL) << 16; /* AVA field */
    va_low = pte_index >> 3;
    if (v & HPTE_V_SECONDARY) {
        va_low = ~va_low;
    }
    /* xor vsid from AVA */
    if (!(v & HPTE_V_1TB_SEG)) {
        va_low ^= v >> 12;
    } else {
        va_low ^= v >> 24;
    }
    va_low &= 0x7ff;
    if (v & HPTE_V_LARGE) {
        rb |= 1;                         /* L field */
#if 0 /* Disable that P7 specific bit for now */
        if (r & 0xff000) {
            /* non-16MB large page, must be 64k */
            /* (masks depend on page size) */
            rb |= 0x1000;                /* page encoding in LP field */
            rb |= (va_low & 0x7f) << 16; /* 7b of VA in AVA/LP field */
            rb |= (va_low & 0xfe);       /* AVAL field */
        }
#endif
    } else {
        /* 4kB page */
        rb |= (va_low & 0x7ff) << 12;   /* remaining 11b of AVA */
    }
    rb |= (v >> 54) & 0x300;            /* B field */
    return rb;
}

static target_ulong h_enter(CPUState *env, sPAPREnvironment *spapr,
                            target_ulong opcode, target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong pte_index = args[1];
    target_ulong pteh = args[2];
    target_ulong ptel = args[3];
    target_ulong page_shift = 12;
    target_ulong raddr;
    target_ulong i;
    uint8_t *hpte;

    /* only handle 4k and 16M pages for now */
    if (pteh & HPTE_V_LARGE) {
#if 0 /* We don't support 64k pages yet */
        if ((ptel & 0xf000) == 0x1000) {
            /* 64k page */
        } else
#endif
        if ((ptel & 0xff000) == 0) {
            /* 16M page */
            page_shift = 24;
            /* lowest AVA bit must be 0 for 16M pages */
            if (pteh & 0x80) {
                return H_PARAMETER;
            }
        } else {
            return H_PARAMETER;
        }
    }

    raddr = (ptel & HPTE_R_RPN) & ~((1ULL << page_shift) - 1);

    if (raddr < spapr->ram_limit) {
        /* Regular RAM - should have WIMG=0010 */
        if ((ptel & HPTE_R_WIMG) != HPTE_R_M) {
            return H_PARAMETER;
        }
    } else {
        /* Looks like an IO address */
        /* FIXME: What WIMG combinations could be sensible for IO?
         * For now we allow WIMG=010x, but are there others? */
        /* FIXME: Should we check against registered IO addresses? */
        if ((ptel & (HPTE_R_W | HPTE_R_I | HPTE_R_M)) != HPTE_R_I) {
            return H_PARAMETER;
        }
    }

    pteh &= ~0x60ULL;

    if ((pte_index * HASH_PTE_SIZE_64) & ~env->htab_mask) {
        return H_PARAMETER;
    }
    if (likely((flags & H_EXACT) == 0)) {
        pte_index &= ~7ULL;
        hpte = env->external_htab + (pte_index * HASH_PTE_SIZE_64);
        for (i = 0; ; ++i) {
            if (i == 8) {
                return H_PTEG_FULL;
            }
            if (((ldq_p(hpte) & HPTE_V_VALID) == 0) &&
                lock_hpte(hpte, HPTE_V_HVLOCK | HPTE_V_VALID)) {
                break;
            }
            hpte += HASH_PTE_SIZE_64;
        }
    } else {
        i = 0;
        hpte = env->external_htab + (pte_index * HASH_PTE_SIZE_64);
        if (!lock_hpte(hpte, HPTE_V_HVLOCK | HPTE_V_VALID)) {
            return H_PTEG_FULL;
        }
    }
    stq_p(hpte + (HASH_PTE_SIZE_64/2), ptel);
    /* eieio();  FIXME: need some sort of barrier for smp? */
    stq_p(hpte, pteh);

    assert(!(ldq_p(hpte) & HPTE_V_HVLOCK));
    args[0] = pte_index + i;
    return H_SUCCESS;
}

enum {
    REMOVE_SUCCESS = 0,
    REMOVE_NOT_FOUND = 1,
    REMOVE_PARM = 2,
    REMOVE_HW = 3,
};

static target_ulong remove_hpte(CPUState *env, target_ulong ptex,
                                target_ulong avpn,
                                target_ulong flags,
                                target_ulong *vp, target_ulong *rp)
{
    uint8_t *hpte;
    target_ulong v, r, rb;

    if ((ptex * HASH_PTE_SIZE_64) & ~env->htab_mask) {
        return REMOVE_PARM;
    }

    hpte = env->external_htab + (ptex * HASH_PTE_SIZE_64);
    while (!lock_hpte(hpte, HPTE_V_HVLOCK)) {
        /* We have no real concurrency in qemu soft-emulation, so we
         * will never actually have a contested lock */
        assert(0);
    }

    v = ldq_p(hpte);
    r = ldq_p(hpte + (HASH_PTE_SIZE_64/2));

    if ((v & HPTE_V_VALID) == 0 ||
        ((flags & H_AVPN) && (v & ~0x7fULL) != avpn) ||
        ((flags & H_ANDCOND) && (v & avpn) != 0)) {
        stq_p(hpte, v & ~HPTE_V_HVLOCK);
        assert(!(ldq_p(hpte) & HPTE_V_HVLOCK));
        return REMOVE_NOT_FOUND;
    }
    *vp = v & ~HPTE_V_HVLOCK;
    *rp = r;
    stq_p(hpte, 0);
    rb = compute_tlbie_rb(v, r, ptex);
    ppc_tlb_invalidate_one(env, rb);
    assert(!(ldq_p(hpte) & HPTE_V_HVLOCK));
    return REMOVE_SUCCESS;
}

static target_ulong h_remove(CPUState *env, sPAPREnvironment *spapr,
                             target_ulong opcode, target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong pte_index = args[1];
    target_ulong avpn = args[2];
    int ret;

    ret = remove_hpte(env, pte_index, avpn, flags,
                      &args[0], &args[1]);

    switch (ret) {
    case REMOVE_SUCCESS:
        return H_SUCCESS;

    case REMOVE_NOT_FOUND:
        return H_NOT_FOUND;

    case REMOVE_PARM:
        return H_PARAMETER;

    case REMOVE_HW:
        return H_HARDWARE;
    }

    assert(0);
}

#define H_BULK_REMOVE_TYPE             0xc000000000000000ULL
#define   H_BULK_REMOVE_REQUEST        0x4000000000000000ULL
#define   H_BULK_REMOVE_RESPONSE       0x8000000000000000ULL
#define   H_BULK_REMOVE_END            0xc000000000000000ULL
#define H_BULK_REMOVE_CODE             0x3000000000000000ULL
#define   H_BULK_REMOVE_SUCCESS        0x0000000000000000ULL
#define   H_BULK_REMOVE_NOT_FOUND      0x1000000000000000ULL
#define   H_BULK_REMOVE_PARM           0x2000000000000000ULL
#define   H_BULK_REMOVE_HW             0x3000000000000000ULL
#define H_BULK_REMOVE_RC               0x0c00000000000000ULL
#define H_BULK_REMOVE_FLAGS            0x0300000000000000ULL
#define   H_BULK_REMOVE_ABSOLUTE       0x0000000000000000ULL
#define   H_BULK_REMOVE_ANDCOND        0x0100000000000000ULL
#define   H_BULK_REMOVE_AVPN           0x0200000000000000ULL
#define H_BULK_REMOVE_PTEX             0x00ffffffffffffffULL

#define H_BULK_REMOVE_MAX_BATCH        4

static target_ulong h_bulk_remove(CPUState *env, sPAPREnvironment *spapr,
                                  target_ulong opcode, target_ulong *args)
{
    int i;

    for (i = 0; i < H_BULK_REMOVE_MAX_BATCH; i++) {
        target_ulong *tsh = &args[i*2];
        target_ulong tsl = args[i*2 + 1];
        target_ulong v, r, ret;

        if ((*tsh & H_BULK_REMOVE_TYPE) == H_BULK_REMOVE_END) {
            break;
        } else if ((*tsh & H_BULK_REMOVE_TYPE) != H_BULK_REMOVE_REQUEST) {
            return H_PARAMETER;
        }

        *tsh &= H_BULK_REMOVE_PTEX | H_BULK_REMOVE_FLAGS;
        *tsh |= H_BULK_REMOVE_RESPONSE;

        if ((*tsh & H_BULK_REMOVE_ANDCOND) && (*tsh & H_BULK_REMOVE_AVPN)) {
            *tsh |= H_BULK_REMOVE_PARM;
            return H_PARAMETER;
        }

        ret = remove_hpte(env, *tsh & H_BULK_REMOVE_PTEX, tsl,
                          (*tsh & H_BULK_REMOVE_FLAGS) >> 26,
                          &v, &r);

        *tsh |= ret << 60;

        switch (ret) {
        case REMOVE_SUCCESS:
            *tsh |= (r & (HPTE_R_C | HPTE_R_R)) << 43;
            break;

        case REMOVE_PARM:
            return H_PARAMETER;

        case REMOVE_HW:
            return H_HARDWARE;
        }
    }

    return H_SUCCESS;
}

static target_ulong h_protect(CPUState *env, sPAPREnvironment *spapr,
                              target_ulong opcode, target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong pte_index = args[1];
    target_ulong avpn = args[2];
    uint8_t *hpte;
    target_ulong v, r, rb;

    if ((pte_index * HASH_PTE_SIZE_64) & ~env->htab_mask) {
        return H_PARAMETER;
    }

    hpte = env->external_htab + (pte_index * HASH_PTE_SIZE_64);
    while (!lock_hpte(hpte, HPTE_V_HVLOCK)) {
        /* We have no real concurrency in qemu soft-emulation, so we
         * will never actually have a contested lock */
        assert(0);
    }

    v = ldq_p(hpte);
    r = ldq_p(hpte + (HASH_PTE_SIZE_64/2));

    if ((v & HPTE_V_VALID) == 0 ||
        ((flags & H_AVPN) && (v & ~0x7fULL) != avpn)) {
        stq_p(hpte, v & ~HPTE_V_HVLOCK);
        assert(!(ldq_p(hpte) & HPTE_V_HVLOCK));
        return H_NOT_FOUND;
    }

    r &= ~(HPTE_R_PP0 | HPTE_R_PP | HPTE_R_N |
           HPTE_R_KEY_HI | HPTE_R_KEY_LO);
    r |= (flags << 55) & HPTE_R_PP0;
    r |= (flags << 48) & HPTE_R_KEY_HI;
    r |= flags & (HPTE_R_PP | HPTE_R_N | HPTE_R_KEY_LO);
    rb = compute_tlbie_rb(v, r, pte_index);
    stq_p(hpte, v & ~HPTE_V_VALID);
    ppc_tlb_invalidate_one(env, rb);
    stq_p(hpte + (HASH_PTE_SIZE_64/2), r);
    /* Don't need a memory barrier, due to qemu's global lock */
    stq_p(hpte, v & ~HPTE_V_HVLOCK);
    assert(!(ldq_p(hpte) & HPTE_V_HVLOCK));
    return H_SUCCESS;
}

static target_ulong h_set_dabr(CPUState *env, sPAPREnvironment *spapr,
                               target_ulong opcode, target_ulong *args)
{
    /* FIXME: actually implement this */
    return H_HARDWARE;
}

#define FLAGS_REGISTER_VPA         0x0000200000000000ULL
#define FLAGS_REGISTER_DTL         0x0000400000000000ULL
#define FLAGS_REGISTER_SLBSHADOW   0x0000600000000000ULL
#define FLAGS_DEREGISTER_VPA       0x0000a00000000000ULL
#define FLAGS_DEREGISTER_DTL       0x0000c00000000000ULL
#define FLAGS_DEREGISTER_SLBSHADOW 0x0000e00000000000ULL

#define VPA_MIN_SIZE           640
#define VPA_SIZE_OFFSET        0x4
#define VPA_SHARED_PROC_OFFSET 0x9
#define VPA_SHARED_PROC_VAL    0x2

static target_ulong register_vpa(CPUState *env, target_ulong vpa)
{
    uint16_t size;
    uint8_t tmp;

    if (vpa == 0) {
        hcall_dprintf("Can't cope with registering a VPA at logical 0\n");
        return H_HARDWARE;
    }

    if (vpa % env->dcache_line_size) {
        return H_PARAMETER;
    }
    /* FIXME: bounds check the address */

    size = lduw_be_phys(vpa + 0x4);

    if (size < VPA_MIN_SIZE) {
        return H_PARAMETER;
    }

    /* VPA is not allowed to cross a page boundary */
    if ((vpa / 4096) != ((vpa + size - 1) / 4096)) {
        return H_PARAMETER;
    }

    env->vpa = vpa;

    tmp = ldub_phys(env->vpa + VPA_SHARED_PROC_OFFSET);
    tmp |= VPA_SHARED_PROC_VAL;
    stb_phys(env->vpa + VPA_SHARED_PROC_OFFSET, tmp);

    return H_SUCCESS;
}

static target_ulong deregister_vpa(CPUState *env, target_ulong vpa)
{
    if (env->slb_shadow) {
        return H_RESOURCE;
    }

    if (env->dispatch_trace_log) {
        return H_RESOURCE;
    }

    env->vpa = 0;
    return H_SUCCESS;
}

static target_ulong register_slb_shadow(CPUState *env, target_ulong addr)
{
    uint32_t size;

    if (addr == 0) {
        hcall_dprintf("Can't cope with SLB shadow at logical 0\n");
        return H_HARDWARE;
    }

    size = ldl_be_phys(addr + 0x4);
    if (size < 0x8) {
        return H_PARAMETER;
    }

    if ((addr / 4096) != ((addr + size - 1) / 4096)) {
        return H_PARAMETER;
    }

    if (!env->vpa) {
        return H_RESOURCE;
    }

    env->slb_shadow = addr;

    return H_SUCCESS;
}

static target_ulong deregister_slb_shadow(CPUState *env, target_ulong addr)
{
    env->slb_shadow = 0;
    return H_SUCCESS;
}

static target_ulong register_dtl(CPUState *env, target_ulong addr)
{
    uint32_t size;

    if (addr == 0) {
        hcall_dprintf("Can't cope with DTL at logical 0\n");
        return H_HARDWARE;
    }

    size = ldl_be_phys(addr + 0x4);

    if (size < 48) {
        return H_PARAMETER;
    }

    if (!env->vpa) {
        return H_RESOURCE;
    }

    env->dispatch_trace_log = addr;
    env->dtl_size = size;

    return H_SUCCESS;
}

static target_ulong deregister_dtl(CPUState *emv, target_ulong addr)
{
    env->dispatch_trace_log = 0;
    env->dtl_size = 0;

    return H_SUCCESS;
}

static target_ulong h_register_vpa(CPUState *env, sPAPREnvironment *spapr,
                                   target_ulong opcode, target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong procno = args[1];
    target_ulong vpa = args[2];
    target_ulong ret = H_PARAMETER;
    CPUState *tenv;

    for (tenv = first_cpu; tenv; tenv = tenv->next_cpu) {
        if (tenv->cpu_index == procno) {
            break;
        }
    }

    if (!tenv) {
        return H_PARAMETER;
    }

    switch (flags) {
    case FLAGS_REGISTER_VPA:
        ret = register_vpa(tenv, vpa);
        break;

    case FLAGS_DEREGISTER_VPA:
        ret = deregister_vpa(tenv, vpa);
        break;

    case FLAGS_REGISTER_SLBSHADOW:
        ret = register_slb_shadow(tenv, vpa);
        break;

    case FLAGS_DEREGISTER_SLBSHADOW:
        ret = deregister_slb_shadow(tenv, vpa);
        break;

    case FLAGS_REGISTER_DTL:
        ret = register_dtl(tenv, vpa);
        break;

    case FLAGS_DEREGISTER_DTL:
        ret = deregister_dtl(tenv, vpa);
        break;
    }

    return ret;
}

static target_ulong h_cede(CPUState *env, sPAPREnvironment *spapr,
                           target_ulong opcode, target_ulong *args)
{
    env->msr |= (1ULL << MSR_EE);
    hreg_compute_hflags(env);
    if (!cpu_has_work(env)) {
        env->halted = 1;
    }
    return H_SUCCESS;
}

static target_ulong h_rtas(CPUState *env, sPAPREnvironment *spapr,
                           target_ulong opcode, target_ulong *args)
{
    target_ulong rtas_r3 = args[0];
    uint32_t token = ldl_be_phys(rtas_r3);
    uint32_t nargs = ldl_be_phys(rtas_r3 + 4);
    uint32_t nret = ldl_be_phys(rtas_r3 + 8);

    return spapr_rtas_call(spapr, token, nargs, rtas_r3 + 12,
                           nret, rtas_r3 + 12 + 4*nargs);
}

static target_ulong h_logical_load(CPUState *env, sPAPREnvironment *spapr,
                                   target_ulong opcode, target_ulong *args)
{
    target_ulong size = args[0];
    target_ulong addr = args[1];

    switch (size) {
    case 1:
        args[0] = ldub_phys(addr);
        return H_SUCCESS;
    case 2:
        args[0] = lduw_phys(addr);
        return H_SUCCESS;
    case 4:
        args[0] = ldl_phys(addr);
        return H_SUCCESS;
    case 8:
        args[0] = ldq_phys(addr);
        return H_SUCCESS;
    }
    return H_PARAMETER;
}

static target_ulong h_logical_store(CPUState *env, sPAPREnvironment *spapr,
                                    target_ulong opcode, target_ulong *args)
{
    target_ulong size = args[0];
    target_ulong addr = args[1];
    target_ulong val  = args[2];

    switch (size) {
    case 1:
        stb_phys(addr, val);
        return H_SUCCESS;
    case 2:
        stw_phys(addr, val);
        return H_SUCCESS;
    case 4:
        stl_phys(addr, val);
        return H_SUCCESS;
    case 8:
        stq_phys(addr, val);
        return H_SUCCESS;
    }
    return H_PARAMETER;
}

static target_ulong h_logical_icbi(CPUState *env, sPAPREnvironment *spapr,
                                   target_ulong opcode, target_ulong *args)
{
    /* Nothing to do on emulation, KVM will trap this in the kernel */
    return H_SUCCESS;
}

static target_ulong h_logical_dcbf(CPUState *env, sPAPREnvironment *spapr,
                                   target_ulong opcode, target_ulong *args)
{
    /* Nothing to do on emulation, KVM will trap this in the kernel */
    return H_SUCCESS;
}

static spapr_hcall_fn papr_hypercall_table[(MAX_HCALL_OPCODE / 4) + 1];
static spapr_hcall_fn kvmppc_hypercall_table[KVMPPC_HCALL_MAX - KVMPPC_HCALL_BASE + 1];

void spapr_register_hypercall(target_ulong opcode, spapr_hcall_fn fn)
{
    spapr_hcall_fn *slot;

    if (opcode <= MAX_HCALL_OPCODE) {
        assert((opcode & 0x3) == 0);

        slot = &papr_hypercall_table[opcode / 4];
    } else {
        assert((opcode >= KVMPPC_HCALL_BASE) && (opcode <= KVMPPC_HCALL_MAX));


        slot = &kvmppc_hypercall_table[opcode - KVMPPC_HCALL_BASE];
    }

    assert(!(*slot) || (fn == *slot));
    *slot = fn;
}

target_ulong spapr_hypercall(CPUState *env, target_ulong opcode,
                             target_ulong *args)
{
    if (msr_pr) {
        hcall_dprintf("Hypercall made with MSR[PR]=1\n");
        return H_PRIVILEGE;
    }

    if ((opcode <= MAX_HCALL_OPCODE)
        && ((opcode & 0x3) == 0)) {
        spapr_hcall_fn fn = papr_hypercall_table[opcode / 4];

        if (fn) {
            return fn(env, spapr, opcode, args);
        }
    } else if ((opcode >= KVMPPC_HCALL_BASE) &&
               (opcode <= KVMPPC_HCALL_MAX)) {
        spapr_hcall_fn fn = kvmppc_hypercall_table[opcode - KVMPPC_HCALL_BASE];

        if (fn) {
            return fn(env, spapr, opcode, args);
        }
    }

    hcall_dprintf("Unimplemented hcall 0x" TARGET_FMT_lx "\n", opcode);
    return H_FUNCTION;
}

static void hypercall_init(void)
{
    /* hcall-pft */
    spapr_register_hypercall(H_ENTER, h_enter);
    spapr_register_hypercall(H_REMOVE, h_remove);
    spapr_register_hypercall(H_PROTECT, h_protect);

    /* hcall-bulk */
    spapr_register_hypercall(H_BULK_REMOVE, h_bulk_remove);

    /* hcall-dabr */
    spapr_register_hypercall(H_SET_DABR, h_set_dabr);

    /* hcall-splpar */
    spapr_register_hypercall(H_REGISTER_VPA, h_register_vpa);
    spapr_register_hypercall(H_CEDE, h_cede);

    /* "debugger" hcalls (also used by SLOF). Note: We do -not- differenciate
     * here between the "CI" and the "CACHE" variants, they will use whatever
     * mapping attributes qemu is using. When using KVM, the kernel will
     * enforce the attributes more strongly
     */
    spapr_register_hypercall(H_LOGICAL_CI_LOAD, h_logical_load);
    spapr_register_hypercall(H_LOGICAL_CI_STORE, h_logical_store);
    spapr_register_hypercall(H_LOGICAL_CACHE_LOAD, h_logical_load);
    spapr_register_hypercall(H_LOGICAL_CACHE_STORE, h_logical_store);
    spapr_register_hypercall(H_LOGICAL_ICBI, h_logical_icbi);
    spapr_register_hypercall(H_LOGICAL_DCBF, h_logical_dcbf);

    /* qemu/KVM-PPC specific hcalls */
    spapr_register_hypercall(KVMPPC_H_RTAS, h_rtas);
}
device_init(hypercall_init);
