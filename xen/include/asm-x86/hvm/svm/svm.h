/*
 * svm.h: SVM Architecture related definitions
 * Copyright (c) 2005, AMD Corporation.
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

#ifndef __ASM_X86_HVM_SVM_H__
#define __ASM_X86_HVM_SVM_H__

#include <xen/types.h>
#include <xen/bitmap.h>

#define SVM_REG_EAX (0) 
#define SVM_REG_ECX (1) 
#define SVM_REG_EDX (2) 
#define SVM_REG_EBX (3) 
#define SVM_REG_ESP (4) 
#define SVM_REG_EBP (5) 
#define SVM_REG_ESI (6) 
#define SVM_REG_EDI (7) 
#define SVM_REG_R8  (8)
#define SVM_REG_R9  (9)
#define SVM_REG_R10 (10)
#define SVM_REG_R11 (11)
#define SVM_REG_R12 (12)
#define SVM_REG_R13 (13)
#define SVM_REG_R14 (14)
#define SVM_REG_R15 (15)

static inline void svm_vmload(void *vmcb)
{
    asm volatile (
        ".byte 0x0f,0x01,0xda" /* vmload */
        : : "a" (__pa(vmcb)) : "memory" );
}

static inline void svm_vmsave(void *vmcb)
{
    asm volatile (
        ".byte 0x0f,0x01,0xdb" /* vmsave */
        : : "a" (__pa(vmcb)) : "memory" );
}

static inline void svm_invlpga(unsigned long vaddr, uint32_t asid)
{
    asm volatile (
        ".byte 0x0f,0x01,0xdf"
        : /* output */
        : /* input */
        "a" (vaddr), "c" (asid));
}

unsigned long *svm_msrbit(unsigned long *msr_bitmap, uint32_t msr);
void __update_guest_eip(struct cpu_user_regs *regs, unsigned int inst_len);

extern u32 svm_feature_flags;

#define SVM_FEATURE_NPT            0 /* Nested page table support */
#define SVM_FEATURE_LBRV           1 /* LBR virtualization support */
#define SVM_FEATURE_SVML           2 /* SVM locking MSR support */
#define SVM_FEATURE_NRIPS          3 /* Next RIP save on VMEXIT support */
#define SVM_FEATURE_TSCRATEMSR     4 /* TSC ratio MSR support */
#define SVM_FEATURE_VMCBCLEAN      5 /* VMCB clean bits support */
#define SVM_FEATURE_FLUSHBYASID    6 /* TLB flush by ASID support */
#define SVM_FEATURE_DECODEASSISTS  7 /* Decode assists support */
#define SVM_FEATURE_PAUSEFILTER   10 /* Pause intercept filter support */

#define cpu_has_svm_feature(f) test_bit(f, &svm_feature_flags)
#define cpu_has_svm_npt       cpu_has_svm_feature(SVM_FEATURE_NPT)
#define cpu_has_svm_lbrv      cpu_has_svm_feature(SVM_FEATURE_LBRV)
#define cpu_has_svm_svml      cpu_has_svm_feature(SVM_FEATURE_SVML)
#define cpu_has_svm_nrips     cpu_has_svm_feature(SVM_FEATURE_NRIPS)
#define cpu_has_svm_cleanbits cpu_has_svm_feature(SVM_FEATURE_VMCBCLEAN)
#define cpu_has_svm_decode    cpu_has_svm_feature(SVM_FEATURE_DECODEASSISTS)
#define cpu_has_pause_filter  cpu_has_svm_feature(SVM_FEATURE_PAUSEFILTER)
#define cpu_has_tsc_ratio     cpu_has_svm_feature(SVM_FEATURE_TSCRATEMSR)

#define SVM_PAUSEFILTER_INIT    3000

/* TSC rate */
#define DEFAULT_TSC_RATIO       0x0000000100000000ULL
#define TSC_RATIO_RSVD_BITS     0xffffff0000000000ULL
#define TSC_RATIO(g_khz, h_khz) ( (((u64)(g_khz)<<32)/(u64)(h_khz)) & \
                                  ~TSC_RATIO_RSVD_BITS )
#define vcpu_tsc_ratio(v)       TSC_RATIO((v)->domain->arch.tsc_khz, cpu_khz)

extern void svm_host_osvw_reset(void);
extern void svm_host_osvw_init(void);

#endif /* __ASM_X86_HVM_SVM_H__ */
