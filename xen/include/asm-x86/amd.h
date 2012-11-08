/*
 * amd.h - AMD processor specific definitions
 */

#ifndef __AMD_H__
#define __AMD_H__

#include <asm/cpufeature.h>

/* CPUID masked for use by AMD-V Extended Migration */

#define X86_FEATURE_BITPOS(_feature_) ((_feature_) % 32)
#define __bit(_x_) (1U << X86_FEATURE_BITPOS(_x_))

/* Family 0Fh, Revision C */
#define AMD_FEATURES_K8_REV_C_ECX  0
#define AMD_FEATURES_K8_REV_C_EDX (					\
	__bit(X86_FEATURE_FPU)      | __bit(X86_FEATURE_VME)   |	\
	__bit(X86_FEATURE_DE)       | __bit(X86_FEATURE_PSE)   |	\
	__bit(X86_FEATURE_TSC)      | __bit(X86_FEATURE_MSR)   |	\
	__bit(X86_FEATURE_PAE)      | __bit(X86_FEATURE_MCE)   |	\
	__bit(X86_FEATURE_CX8)      | __bit(X86_FEATURE_APIC)  |	\
	__bit(X86_FEATURE_SEP)      | __bit(X86_FEATURE_MTRR)  |	\
	__bit(X86_FEATURE_PGE)      | __bit(X86_FEATURE_MCA)   | 	\
	__bit(X86_FEATURE_CMOV)     | __bit(X86_FEATURE_PAT)   |	\
	__bit(X86_FEATURE_PSE36)    | __bit(X86_FEATURE_CLFLSH)|	\
	__bit(X86_FEATURE_MMX)      | __bit(X86_FEATURE_FXSR)  | 	\
	__bit(X86_FEATURE_XMM)      | __bit(X86_FEATURE_XMM2))
#define AMD_EXTFEATURES_K8_REV_C_ECX  0 
#define AMD_EXTFEATURES_K8_REV_C_EDX  (					\
	__bit(X86_FEATURE_FPU)      | __bit(X86_FEATURE_VME)   |	\
	__bit(X86_FEATURE_DE)       | __bit(X86_FEATURE_PSE)   |	\
	__bit(X86_FEATURE_TSC)      | __bit(X86_FEATURE_MSR)   |	\
	__bit(X86_FEATURE_PAE)      | __bit(X86_FEATURE_MCE)   |	\
	__bit(X86_FEATURE_CX8)      | __bit(X86_FEATURE_APIC)  |	\
	__bit(X86_FEATURE_SYSCALL)  | __bit(X86_FEATURE_MTRR)  |	\
	__bit(X86_FEATURE_PGE)      | __bit(X86_FEATURE_MCA)   |	\
	__bit(X86_FEATURE_CMOV)     | __bit(X86_FEATURE_PAT)   |	\
	__bit(X86_FEATURE_PSE36)    | __bit(X86_FEATURE_NX)    |	\
	__bit(X86_FEATURE_MMXEXT)   | __bit(X86_FEATURE_MMX)   |	\
	__bit(X86_FEATURE_FXSR)     | __bit(X86_FEATURE_LM)    |	\
	__bit(X86_FEATURE_3DNOWEXT) | __bit(X86_FEATURE_3DNOW))

/* Family 0Fh, Revision D */
#define AMD_FEATURES_K8_REV_D_ECX         AMD_FEATURES_K8_REV_C_ECX
#define AMD_FEATURES_K8_REV_D_EDX         AMD_FEATURES_K8_REV_C_EDX
#define AMD_EXTFEATURES_K8_REV_D_ECX     (AMD_EXTFEATURES_K8_REV_C_ECX |\
	__bit(X86_FEATURE_LAHF_LM))
#define AMD_EXTFEATURES_K8_REV_D_EDX     (AMD_EXTFEATURES_K8_REV_C_EDX |\
	__bit(X86_FEATURE_FFXSR))

/* Family 0Fh, Revision E */
#define AMD_FEATURES_K8_REV_E_ECX        (AMD_FEATURES_K8_REV_D_ECX |	\
	__bit(X86_FEATURE_XMM3))
#define AMD_FEATURES_K8_REV_E_EDX        (AMD_FEATURES_K8_REV_D_EDX | 	\
	__bit(X86_FEATURE_HT))
#define AMD_EXTFEATURES_K8_REV_E_ECX     (AMD_EXTFEATURES_K8_REV_D_ECX |\
	__bit(X86_FEATURE_CMP_LEGACY)) 
#define AMD_EXTFEATURES_K8_REV_E_EDX      AMD_EXTFEATURES_K8_REV_D_EDX

/* Family 0Fh, Revision F */
#define AMD_FEATURES_K8_REV_F_ECX        (AMD_FEATURES_K8_REV_E_ECX | 	\
	__bit(X86_FEATURE_CX16))
#define AMD_FEATURES_K8_REV_F_EDX         AMD_FEATURES_K8_REV_E_EDX
#define AMD_EXTFEATURES_K8_REV_F_ECX     (AMD_EXTFEATURES_K8_REV_E_ECX |\
	__bit(X86_FEATURE_SVM) | __bit(X86_FEATURE_EXTAPIC) |	        \
	__bit(X86_FEATURE_CR8_LEGACY))
#define AMD_EXTFEATURES_K8_REV_F_EDX     (AMD_EXTFEATURES_K8_REV_E_EDX |\
	__bit(X86_FEATURE_RDTSCP))

/* Family 0Fh, Revision G */
#define AMD_FEATURES_K8_REV_G_ECX         AMD_FEATURES_K8_REV_F_ECX
#define AMD_FEATURES_K8_REV_G_EDX         AMD_FEATURES_K8_REV_F_EDX
#define AMD_EXTFEATURES_K8_REV_G_ECX     (AMD_EXTFEATURES_K8_REV_F_ECX |\
	__bit(X86_FEATURE_3DNOWPREFETCH))
#define AMD_EXTFEATURES_K8_REV_G_EDX      AMD_EXTFEATURES_K8_REV_F_EDX

/* Family 10h, Revision B */
#define AMD_FEATURES_FAM10h_REV_B_ECX    (AMD_FEATURES_K8_REV_F_ECX | 	\
	__bit(X86_FEATURE_POPCNT) | __bit(X86_FEATURE_MWAIT))
#define AMD_FEATURES_FAM10h_REV_B_EDX     AMD_FEATURES_K8_REV_F_EDX
#define AMD_EXTFEATURES_FAM10h_REV_B_ECX (AMD_EXTFEATURES_K8_REV_F_ECX |\
	__bit(X86_FEATURE_ABM) | __bit(X86_FEATURE_SSE4A) | 		\
	__bit(X86_FEATURE_MISALIGNSSE) | __bit(X86_FEATURE_OSVW) | 	\
	__bit(X86_FEATURE_IBS))
#define AMD_EXTFEATURES_FAM10h_REV_B_EDX (AMD_EXTFEATURES_K8_REV_F_EDX |\
	__bit(X86_FEATURE_PAGE1GB))

/* Family 10h, Revision C */
#define AMD_FEATURES_FAM10h_REV_C_ECX     AMD_FEATURES_FAM10h_REV_B_ECX
#define AMD_FEATURES_FAM10h_REV_C_EDX     AMD_FEATURES_FAM10h_REV_B_EDX
#define AMD_EXTFEATURES_FAM10h_REV_C_ECX (AMD_EXTFEATURES_FAM10h_REV_B_ECX |\
	__bit(X86_FEATURE_SKINIT) | __bit(X86_FEATURE_WDT))
#define AMD_EXTFEATURES_FAM10h_REV_C_EDX  AMD_EXTFEATURES_FAM10h_REV_B_EDX

/* Family 11h, Revision B */
#define AMD_FEATURES_FAM11h_REV_B_ECX     AMD_FEATURES_K8_REV_G_ECX
#define AMD_FEATURES_FAM11h_REV_B_EDX     AMD_FEATURES_K8_REV_G_EDX
#define AMD_EXTFEATURES_FAM11h_REV_B_ECX (AMD_EXTFEATURES_K8_REV_G_ECX |\
	__bit(X86_FEATURE_SKINIT))
#define AMD_EXTFEATURES_FAM11h_REV_B_EDX  AMD_EXTFEATURES_K8_REV_G_EDX

/* AMD errata checking
 *
 * Errata are defined using the AMD_LEGACY_ERRATUM() or AMD_OSVW_ERRATUM()
 * macros. The latter is intended for newer errata that have an OSVW id
 * assigned, which it takes as first argument. Both take a variable number
 * of family-specific model-stepping ranges created by AMD_MODEL_RANGE().
 *
 * Example 1:
 * #define AMD_ERRATUM_319                                              \
 *   AMD_LEGACY_ERRATUM(AMD_MODEL_RANGE(0x10, 0x2, 0x1, 0x4, 0x2),      \
 *                      AMD_MODEL_RANGE(0x10, 0x8, 0x0, 0x8, 0x0),      \
 *                      AMD_MODEL_RANGE(0x10, 0x9, 0x0, 0x9, 0x0))
 * Example 2:
 * #define AMD_ERRATUM_400                                              \
 *   AMD_OSVW_ERRATUM(1, AMD_MODEL_RANGE(0xf, 0x41, 0x2, 0xff, 0xf),    \
 *                       AMD_MODEL_RANGE(0x10, 0x2, 0x1, 0xff, 0xf))
 *   
 */

#define AMD_LEGACY_ERRATUM(...)         -1 /* legacy */, __VA_ARGS__, 0
#define AMD_OSVW_ERRATUM(osvw_id, ...)  osvw_id, __VA_ARGS__, 0
#define AMD_MODEL_RANGE(f, m_start, s_start, m_end, s_end)              \
    ((f << 24) | (m_start << 16) | (s_start << 12) | (m_end << 4) | (s_end))
#define AMD_MODEL_RANGE_FAMILY(range)   (((range) >> 24) & 0xff)
#define AMD_MODEL_RANGE_START(range)    (((range) >> 12) & 0xfff)
#define AMD_MODEL_RANGE_END(range)      ((range) & 0xfff)

#define AMD_ERRATUM_121                                                 \
    AMD_LEGACY_ERRATUM(AMD_MODEL_RANGE(0x0f, 0x0, 0x0, 0x3f, 0xf))

#define AMD_ERRATUM_170                                                 \
    AMD_LEGACY_ERRATUM(AMD_MODEL_RANGE(0x0f, 0x0, 0x0, 0x67, 0xf))

#define AMD_ERRATUM_383                                                 \
    AMD_OSVW_ERRATUM(3, AMD_MODEL_RANGE(0x10, 0x2, 0x1, 0xff, 0xf),	\
		        AMD_MODEL_RANGE(0x12, 0x0, 0x0, 0x1, 0x0))

#define AMD_ERRATUM_573							\
    AMD_LEGACY_ERRATUM(AMD_MODEL_RANGE(0x0f, 0x0, 0x0, 0xff, 0xf),	\
                       AMD_MODEL_RANGE(0x10, 0x0, 0x0, 0xff, 0xf),	\
                       AMD_MODEL_RANGE(0x11, 0x0, 0x0, 0xff, 0xf),	\
                       AMD_MODEL_RANGE(0x12, 0x0, 0x0, 0xff, 0xf))

struct cpuinfo_x86;
int cpu_has_amd_erratum(const struct cpuinfo_x86 *, int, ...);

#ifdef __x86_64__
extern s8 opt_allow_unsafe;

void fam10h_check_enable_mmcfg(void);
void check_enable_amd_mmconf_dmi(void);
#endif

#endif /* __AMD_H__ */
