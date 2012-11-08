#if !defined (__MIPS_CPU_H__)
#define __MIPS_CPU_H__

//#define DEBUG_OP

#define TARGET_HAS_ICE 1

#define ELF_MACHINE	EM_MIPS

#define CPUState struct CPUMIPSState

#include "config.h"
#include "qemu-common.h"
#include "mips-defs.h"
#include "cpu-defs.h"
#include "softfloat.h"

// uint_fast8_t and uint_fast16_t not in <sys/int_types.h>
// XXX: move that elsewhere
#if defined(CONFIG_SOLARIS) && CONFIG_SOLARIS_VERSION < 10
typedef unsigned char           uint_fast8_t;
typedef unsigned int            uint_fast16_t;
#endif

struct CPUMIPSState;

typedef struct r4k_tlb_t r4k_tlb_t;
struct r4k_tlb_t {
    target_ulong VPN;
    uint32_t PageMask;
    uint_fast8_t ASID;
    uint_fast16_t G:1;
    uint_fast16_t C0:3;
    uint_fast16_t C1:3;
    uint_fast16_t V0:1;
    uint_fast16_t V1:1;
    uint_fast16_t D0:1;
    uint_fast16_t D1:1;
    target_ulong PFN[2];
};

#if !defined(CONFIG_USER_ONLY)
typedef struct CPUMIPSTLBContext CPUMIPSTLBContext;
struct CPUMIPSTLBContext {
    uint32_t nb_tlb;
    uint32_t tlb_in_use;
    int (*map_address) (struct CPUMIPSState *env, target_phys_addr_t *physical, int *prot, target_ulong address, int rw, int access_type);
    void (*helper_tlbwi) (void);
    void (*helper_tlbwr) (void);
    void (*helper_tlbp) (void);
    void (*helper_tlbr) (void);
    union {
        struct {
            r4k_tlb_t tlb[MIPS_TLB_MAX];
        } r4k;
    } mmu;
};
#endif

typedef union fpr_t fpr_t;
union fpr_t {
    float64  fd;   /* ieee double precision */
    float32  fs[2];/* ieee single precision */
    uint64_t d;    /* binary double fixed-point */
    uint32_t w[2]; /* binary single fixed-point */
};
/* define FP_ENDIAN_IDX to access the same location
 * in the fpr_t union regardless of the host endianness
 */
#if defined(HOST_WORDS_BIGENDIAN)
#  define FP_ENDIAN_IDX 1
#else
#  define FP_ENDIAN_IDX 0
#endif

typedef struct CPUMIPSFPUContext CPUMIPSFPUContext;
struct CPUMIPSFPUContext {
    /* Floating point registers */
    fpr_t fpr[32];
    float_status fp_status;
    /* fpu implementation/revision register (fir) */
    uint32_t fcr0;
#define FCR0_F64 22
#define FCR0_L 21
#define FCR0_W 20
#define FCR0_3D 19
#define FCR0_PS 18
#define FCR0_D 17
#define FCR0_S 16
#define FCR0_PRID 8
#define FCR0_REV 0
    /* fcsr */
    uint32_t fcr31;
#define SET_FP_COND(num,env)     do { ((env).fcr31) |= ((num) ? (1 << ((num) + 24)) : (1 << 23)); } while(0)
#define CLEAR_FP_COND(num,env)   do { ((env).fcr31) &= ~((num) ? (1 << ((num) + 24)) : (1 << 23)); } while(0)
#define GET_FP_COND(env)         ((((env).fcr31 >> 24) & 0xfe) | (((env).fcr31 >> 23) & 0x1))
#define GET_FP_CAUSE(reg)        (((reg) >> 12) & 0x3f)
#define GET_FP_ENABLE(reg)       (((reg) >>  7) & 0x1f)
#define GET_FP_FLAGS(reg)        (((reg) >>  2) & 0x1f)
#define SET_FP_CAUSE(reg,v)      do { (reg) = ((reg) & ~(0x3f << 12)) | ((v & 0x3f) << 12); } while(0)
#define SET_FP_ENABLE(reg,v)     do { (reg) = ((reg) & ~(0x1f <<  7)) | ((v & 0x1f) << 7); } while(0)
#define SET_FP_FLAGS(reg,v)      do { (reg) = ((reg) & ~(0x1f <<  2)) | ((v & 0x1f) << 2); } while(0)
#define UPDATE_FP_FLAGS(reg,v)   do { (reg) |= ((v & 0x1f) << 2); } while(0)
#define FP_INEXACT        1
#define FP_UNDERFLOW      2
#define FP_OVERFLOW       4
#define FP_DIV0           8
#define FP_INVALID        16
#define FP_UNIMPLEMENTED  32
};

#define NB_MMU_MODES 3

typedef struct CPUMIPSMVPContext CPUMIPSMVPContext;
struct CPUMIPSMVPContext {
    int32_t CP0_MVPControl;
#define CP0MVPCo_CPA	3
#define CP0MVPCo_STLB	2
#define CP0MVPCo_VPC	1
#define CP0MVPCo_EVP	0
    int32_t CP0_MVPConf0;
#define CP0MVPC0_M	31
#define CP0MVPC0_TLBS	29
#define CP0MVPC0_GS	28
#define CP0MVPC0_PCP	27
#define CP0MVPC0_PTLBE	16
#define CP0MVPC0_TCA	15
#define CP0MVPC0_PVPE	10
#define CP0MVPC0_PTC	0
    int32_t CP0_MVPConf1;
#define CP0MVPC1_CIM	31
#define CP0MVPC1_CIF	30
#define CP0MVPC1_PCX	20
#define CP0MVPC1_PCP2	10
#define CP0MVPC1_PCP1	0
};

typedef struct mips_def_t mips_def_t;

#define MIPS_SHADOW_SET_MAX 16
#define MIPS_TC_MAX 5
#define MIPS_FPU_MAX 1
#define MIPS_DSP_ACC 4

typedef struct TCState TCState;
struct TCState {
    target_ulong gpr[32];
    target_ulong PC;
    target_ulong HI[MIPS_DSP_ACC];
    target_ulong LO[MIPS_DSP_ACC];
    target_ulong ACX[MIPS_DSP_ACC];
    target_ulong DSPControl;
    int32_t CP0_TCStatus;
#define CP0TCSt_TCU3	31
#define CP0TCSt_TCU2	30
#define CP0TCSt_TCU1	29
#define CP0TCSt_TCU0	28
#define CP0TCSt_TMX	27
#define CP0TCSt_RNST	23
#define CP0TCSt_TDS	21
#define CP0TCSt_DT	20
#define CP0TCSt_DA	15
#define CP0TCSt_A	13
#define CP0TCSt_TKSU	11
#define CP0TCSt_IXMT	10
#define CP0TCSt_TASID	0
    int32_t CP0_TCBind;
#define CP0TCBd_CurTC	21
#define CP0TCBd_TBE	17
#define CP0TCBd_CurVPE	0
    target_ulong CP0_TCHalt;
    target_ulong CP0_TCContext;
    target_ulong CP0_TCSchedule;
    target_ulong CP0_TCScheFBack;
    int32_t CP0_Debug_tcstatus;
};

typedef struct CPUMIPSState CPUMIPSState;
struct CPUMIPSState {
    TCState active_tc;
    CPUMIPSFPUContext active_fpu;

    uint32_t current_tc;
    uint32_t current_fpu;

    uint32_t SEGBITS;
    uint32_t PABITS;
    target_ulong SEGMask;
    target_ulong PAMask;

    int32_t CP0_Index;
    /* CP0_MVP* are per MVP registers. */
    int32_t CP0_Random;
    int32_t CP0_VPEControl;
#define CP0VPECo_YSI	21
#define CP0VPECo_GSI	20
#define CP0VPECo_EXCPT	16
#define CP0VPECo_TE	15
#define CP0VPECo_TargTC	0
    int32_t CP0_VPEConf0;
#define CP0VPEC0_M	31
#define CP0VPEC0_XTC	21
#define CP0VPEC0_TCS	19
#define CP0VPEC0_SCS	18
#define CP0VPEC0_DSC	17
#define CP0VPEC0_ICS	16
#define CP0VPEC0_MVP	1
#define CP0VPEC0_VPA	0
    int32_t CP0_VPEConf1;
#define CP0VPEC1_NCX	20
#define CP0VPEC1_NCP2	10
#define CP0VPEC1_NCP1	0
    target_ulong CP0_YQMask;
    target_ulong CP0_VPESchedule;
    target_ulong CP0_VPEScheFBack;
    int32_t CP0_VPEOpt;
#define CP0VPEOpt_IWX7	15
#define CP0VPEOpt_IWX6	14
#define CP0VPEOpt_IWX5	13
#define CP0VPEOpt_IWX4	12
#define CP0VPEOpt_IWX3	11
#define CP0VPEOpt_IWX2	10
#define CP0VPEOpt_IWX1	9
#define CP0VPEOpt_IWX0	8
#define CP0VPEOpt_DWX7	7
#define CP0VPEOpt_DWX6	6
#define CP0VPEOpt_DWX5	5
#define CP0VPEOpt_DWX4	4
#define CP0VPEOpt_DWX3	3
#define CP0VPEOpt_DWX2	2
#define CP0VPEOpt_DWX1	1
#define CP0VPEOpt_DWX0	0
    target_ulong CP0_EntryLo0;
    target_ulong CP0_EntryLo1;
    target_ulong CP0_Context;
    int32_t CP0_PageMask;
    int32_t CP0_PageGrain;
    int32_t CP0_Wired;
    int32_t CP0_SRSConf0_rw_bitmask;
    int32_t CP0_SRSConf0;
#define CP0SRSC0_M	31
#define CP0SRSC0_SRS3	20
#define CP0SRSC0_SRS2	10
#define CP0SRSC0_SRS1	0
    int32_t CP0_SRSConf1_rw_bitmask;
    int32_t CP0_SRSConf1;
#define CP0SRSC1_M	31
#define CP0SRSC1_SRS6	20
#define CP0SRSC1_SRS5	10
#define CP0SRSC1_SRS4	0
    int32_t CP0_SRSConf2_rw_bitmask;
    int32_t CP0_SRSConf2;
#define CP0SRSC2_M	31
#define CP0SRSC2_SRS9	20
#define CP0SRSC2_SRS8	10
#define CP0SRSC2_SRS7	0
    int32_t CP0_SRSConf3_rw_bitmask;
    int32_t CP0_SRSConf3;
#define CP0SRSC3_M	31
#define CP0SRSC3_SRS12	20
#define CP0SRSC3_SRS11	10
#define CP0SRSC3_SRS10	0
    int32_t CP0_SRSConf4_rw_bitmask;
    int32_t CP0_SRSConf4;
#define CP0SRSC4_SRS15	20
#define CP0SRSC4_SRS14	10
#define CP0SRSC4_SRS13	0
    int32_t CP0_HWREna;
    target_ulong CP0_BadVAddr;
    int32_t CP0_Count;
    target_ulong CP0_EntryHi;
    int32_t CP0_Compare;
    int32_t CP0_Status;
#define CP0St_CU3   31
#define CP0St_CU2   30
#define CP0St_CU1   29
#define CP0St_CU0   28
#define CP0St_RP    27
#define CP0St_FR    26
#define CP0St_RE    25
#define CP0St_MX    24
#define CP0St_PX    23
#define CP0St_BEV   22
#define CP0St_TS    21
#define CP0St_SR    20
#define CP0St_NMI   19
#define CP0St_IM    8
#define CP0St_KX    7
#define CP0St_SX    6
#define CP0St_UX    5
#define CP0St_KSU   3
#define CP0St_ERL   2
#define CP0St_EXL   1
#define CP0St_IE    0
    int32_t CP0_IntCtl;
#define CP0IntCtl_IPTI 29
#define CP0IntCtl_IPPC1 26
#define CP0IntCtl_VS 5
    int32_t CP0_SRSCtl;
#define CP0SRSCtl_HSS 26
#define CP0SRSCtl_EICSS 18
#define CP0SRSCtl_ESS 12
#define CP0SRSCtl_PSS 6
#define CP0SRSCtl_CSS 0
    int32_t CP0_SRSMap;
#define CP0SRSMap_SSV7 28
#define CP0SRSMap_SSV6 24
#define CP0SRSMap_SSV5 20
#define CP0SRSMap_SSV4 16
#define CP0SRSMap_SSV3 12
#define CP0SRSMap_SSV2 8
#define CP0SRSMap_SSV1 4
#define CP0SRSMap_SSV0 0
    int32_t CP0_Cause;
#define CP0Ca_BD   31
#define CP0Ca_TI   30
#define CP0Ca_CE   28
#define CP0Ca_DC   27
#define CP0Ca_PCI  26
#define CP0Ca_IV   23
#define CP0Ca_WP   22
#define CP0Ca_IP    8
#define CP0Ca_IP_mask 0x0000FF00
#define CP0Ca_EC    2
    target_ulong CP0_EPC;
    int32_t CP0_PRid;
    int32_t CP0_EBase;
    int32_t CP0_Config0;
#define CP0C0_M    31
#define CP0C0_K23  28
#define CP0C0_KU   25
#define CP0C0_MDU  20
#define CP0C0_MM   17
#define CP0C0_BM   16
#define CP0C0_BE   15
#define CP0C0_AT   13
#define CP0C0_AR   10
#define CP0C0_MT   7
#define CP0C0_VI   3
#define CP0C0_K0   0
    int32_t CP0_Config1;
#define CP0C1_M    31
#define CP0C1_MMU  25
#define CP0C1_IS   22
#define CP0C1_IL   19
#define CP0C1_IA   16
#define CP0C1_DS   13
#define CP0C1_DL   10
#define CP0C1_DA   7
#define CP0C1_C2   6
#define CP0C1_MD   5
#define CP0C1_PC   4
#define CP0C1_WR   3
#define CP0C1_CA   2
#define CP0C1_EP   1
#define CP0C1_FP   0
    int32_t CP0_Config2;
#define CP0C2_M    31
#define CP0C2_TU   28
#define CP0C2_TS   24
#define CP0C2_TL   20
#define CP0C2_TA   16
#define CP0C2_SU   12
#define CP0C2_SS   8
#define CP0C2_SL   4
#define CP0C2_SA   0
    int32_t CP0_Config3;
#define CP0C3_M    31
#define CP0C3_ISA_ON_EXC 16
#define CP0C3_DSPP 10
#define CP0C3_LPA  7
#define CP0C3_VEIC 6
#define CP0C3_VInt 5
#define CP0C3_SP   4
#define CP0C3_MT   2
#define CP0C3_SM   1
#define CP0C3_TL   0
    int32_t CP0_Config6;
    int32_t CP0_Config7;
    /* XXX: Maybe make LLAddr per-TC? */
    target_ulong lladdr;
    target_ulong llval;
    target_ulong llnewval;
    target_ulong llreg;
    target_ulong CP0_LLAddr_rw_bitmask;
    int CP0_LLAddr_shift;
    target_ulong CP0_WatchLo[8];
    int32_t CP0_WatchHi[8];
    target_ulong CP0_XContext;
    int32_t CP0_Framemask;
    int32_t CP0_Debug;
#define CP0DB_DBD  31
#define CP0DB_DM   30
#define CP0DB_LSNM 28
#define CP0DB_Doze 27
#define CP0DB_Halt 26
#define CP0DB_CNT  25
#define CP0DB_IBEP 24
#define CP0DB_DBEP 21
#define CP0DB_IEXI 20
#define CP0DB_VER  15
#define CP0DB_DEC  10
#define CP0DB_SSt  8
#define CP0DB_DINT 5
#define CP0DB_DIB  4
#define CP0DB_DDBS 3
#define CP0DB_DDBL 2
#define CP0DB_DBp  1
#define CP0DB_DSS  0
    target_ulong CP0_DEPC;
    int32_t CP0_Performance0;
    int32_t CP0_TagLo;
    int32_t CP0_DataLo;
    int32_t CP0_TagHi;
    int32_t CP0_DataHi;
    target_ulong CP0_ErrorEPC;
    int32_t CP0_DESAVE;
    /* We waste some space so we can handle shadow registers like TCs. */
    TCState tcs[MIPS_SHADOW_SET_MAX];
    CPUMIPSFPUContext fpus[MIPS_FPU_MAX];
    /* Qemu */
    int error_code;
    uint32_t hflags;    /* CPU State */
    /* TMASK defines different execution modes */
#define MIPS_HFLAG_TMASK  0x007FF
#define MIPS_HFLAG_MODE   0x00007 /* execution modes                    */
    /* The KSU flags must be the lowest bits in hflags. The flag order
       must be the same as defined for CP0 Status. This allows to use
       the bits as the value of mmu_idx. */
#define MIPS_HFLAG_KSU    0x00003 /* kernel/supervisor/user mode mask   */
#define MIPS_HFLAG_UM     0x00002 /* user mode flag                     */
#define MIPS_HFLAG_SM     0x00001 /* supervisor mode flag               */
#define MIPS_HFLAG_KM     0x00000 /* kernel mode flag                   */
#define MIPS_HFLAG_DM     0x00004 /* Debug mode                         */
#define MIPS_HFLAG_64     0x00008 /* 64-bit instructions enabled        */
#define MIPS_HFLAG_CP0    0x00010 /* CP0 enabled                        */
#define MIPS_HFLAG_FPU    0x00020 /* FPU enabled                        */
#define MIPS_HFLAG_F64    0x00040 /* 64-bit FPU enabled                 */
    /* True if the MIPS IV COP1X instructions can be used.  This also
       controls the non-COP1X instructions RECIP.S, RECIP.D, RSQRT.S
       and RSQRT.D.  */
#define MIPS_HFLAG_COP1X  0x00080 /* COP1X instructions enabled         */
#define MIPS_HFLAG_RE     0x00100 /* Reversed endianness                */
#define MIPS_HFLAG_UX     0x00200 /* 64-bit user mode                   */
#define MIPS_HFLAG_M16    0x00400 /* MIPS16 mode flag                   */
#define MIPS_HFLAG_M16_SHIFT 10
    /* If translation is interrupted between the branch instruction and
     * the delay slot, record what type of branch it is so that we can
     * resume translation properly.  It might be possible to reduce
     * this from three bits to two.  */
#define MIPS_HFLAG_BMASK_BASE  0x03800
#define MIPS_HFLAG_B      0x00800 /* Unconditional branch               */
#define MIPS_HFLAG_BC     0x01000 /* Conditional branch                 */
#define MIPS_HFLAG_BL     0x01800 /* Likely branch                      */
#define MIPS_HFLAG_BR     0x02000 /* branch to register (can't link TB) */
    /* Extra flags about the current pending branch.  */
#define MIPS_HFLAG_BMASK_EXT 0x3C000
#define MIPS_HFLAG_B16    0x04000 /* branch instruction was 16 bits     */
#define MIPS_HFLAG_BDS16  0x08000 /* branch requires 16-bit delay slot  */
#define MIPS_HFLAG_BDS32  0x10000 /* branch requires 32-bit delay slot  */
#define MIPS_HFLAG_BX     0x20000 /* branch exchanges execution mode    */
#define MIPS_HFLAG_BMASK  (MIPS_HFLAG_BMASK_BASE | MIPS_HFLAG_BMASK_EXT)
    target_ulong btarget;        /* Jump / branch target               */
    target_ulong bcond;          /* Branch condition (if needed)       */

    int SYNCI_Step; /* Address step size for SYNCI */
    int CCRes; /* Cycle count resolution/divisor */
    uint32_t CP0_Status_rw_bitmask; /* Read/write bits in CP0_Status */
    uint32_t CP0_TCStatus_rw_bitmask; /* Read/write bits in CP0_TCStatus */
    int insn_flags; /* Supported instruction set */

    target_ulong tls_value; /* For usermode emulation */

    CPU_COMMON

    CPUMIPSMVPContext *mvp;
#if !defined(CONFIG_USER_ONLY)
    CPUMIPSTLBContext *tlb;
#endif

    const mips_def_t *cpu_model;
    void *irq[8];
    struct QEMUTimer *timer; /* Internal timer */
};

#if !defined(CONFIG_USER_ONLY)
int no_mmu_map_address (CPUMIPSState *env, target_phys_addr_t *physical, int *prot,
                        target_ulong address, int rw, int access_type);
int fixed_mmu_map_address (CPUMIPSState *env, target_phys_addr_t *physical, int *prot,
                           target_ulong address, int rw, int access_type);
int r4k_map_address (CPUMIPSState *env, target_phys_addr_t *physical, int *prot,
                     target_ulong address, int rw, int access_type);
void r4k_helper_tlbwi (void);
void r4k_helper_tlbwr (void);
void r4k_helper_tlbp (void);
void r4k_helper_tlbr (void);

void cpu_unassigned_access(CPUState *env, target_phys_addr_t addr,
                           int is_write, int is_exec, int unused, int size);
#endif

void mips_cpu_list (FILE *f, fprintf_function cpu_fprintf);

#define cpu_init cpu_mips_init
#define cpu_exec cpu_mips_exec
#define cpu_gen_code cpu_mips_gen_code
#define cpu_signal_handler cpu_mips_signal_handler
#define cpu_list mips_cpu_list

#define CPU_SAVE_VERSION 3

/* MMU modes definitions. We carefully match the indices with our
   hflags layout. */
#define MMU_MODE0_SUFFIX _kernel
#define MMU_MODE1_SUFFIX _super
#define MMU_MODE2_SUFFIX _user
#define MMU_USER_IDX 2
static inline int cpu_mmu_index (CPUState *env)
{
    return env->hflags & MIPS_HFLAG_KSU;
}

static inline void cpu_clone_regs(CPUState *env, target_ulong newsp)
{
    if (newsp)
        env->active_tc.gpr[29] = newsp;
    env->active_tc.gpr[7] = 0;
    env->active_tc.gpr[2] = 0;
}

static inline int cpu_mips_hw_interrupts_pending(CPUState *env)
{
    int32_t pending;
    int32_t status;
    int r;

    if (!(env->CP0_Status & (1 << CP0St_IE)) ||
        (env->CP0_Status & (1 << CP0St_EXL)) ||
        (env->CP0_Status & (1 << CP0St_ERL)) ||
        /* Note that the TCStatus IXMT field is initialized to zero,
           and only MT capable cores can set it to one. So we don't
           need to check for MT capabilities here.  */
        (env->active_tc.CP0_TCStatus & (1 << CP0TCSt_IXMT)) ||
        (env->hflags & MIPS_HFLAG_DM)) {
        /* Interrupts are disabled */
        return 0;
    }

    pending = env->CP0_Cause & CP0Ca_IP_mask;
    status = env->CP0_Status & CP0Ca_IP_mask;

    if (env->CP0_Config3 & (1 << CP0C3_VEIC)) {
        /* A MIPS configured with a vectorizing external interrupt controller
           will feed a vector into the Cause pending lines. The core treats
           the status lines as a vector level, not as indiviual masks.  */
        r = pending > status;
    } else {
        /* A MIPS configured with compatibility or VInt (Vectored Interrupts)
           treats the pending lines as individual interrupt lines, the status
           lines are individual masks.  */
        r = pending & status;
    }
    return r;
}

#include "cpu-all.h"

/* Memory access type :
 * may be needed for precise access rights control and precise exceptions.
 */
enum {
    /* 1 bit to define user level / supervisor access */
    ACCESS_USER  = 0x00,
    ACCESS_SUPER = 0x01,
    /* 1 bit to indicate direction */
    ACCESS_STORE = 0x02,
    /* Type of instruction that generated the access */
    ACCESS_CODE  = 0x10, /* Code fetch access                */
    ACCESS_INT   = 0x20, /* Integer load/store access        */
    ACCESS_FLOAT = 0x30, /* floating point load/store access */
};

/* Exceptions */
enum {
    EXCP_NONE          = -1,
    EXCP_RESET         = 0,
    EXCP_SRESET,
    EXCP_DSS,
    EXCP_DINT,
    EXCP_DDBL,
    EXCP_DDBS,
    EXCP_NMI,
    EXCP_MCHECK,
    EXCP_EXT_INTERRUPT, /* 8 */
    EXCP_DFWATCH,
    EXCP_DIB,
    EXCP_IWATCH,
    EXCP_AdEL,
    EXCP_AdES,
    EXCP_TLBF,
    EXCP_IBE,
    EXCP_DBp, /* 16 */
    EXCP_SYSCALL,
    EXCP_BREAK,
    EXCP_CpU,
    EXCP_RI,
    EXCP_OVERFLOW,
    EXCP_TRAP,
    EXCP_FPE,
    EXCP_DWATCH, /* 24 */
    EXCP_LTLBL,
    EXCP_TLBL,
    EXCP_TLBS,
    EXCP_DBE,
    EXCP_THREAD,
    EXCP_MDMX,
    EXCP_C2E,
    EXCP_CACHE, /* 32 */

    EXCP_LAST = EXCP_CACHE,
};
/* Dummy exception for conditional stores.  */
#define EXCP_SC 0x100

/*
 * This is an interrnally generated WAKE request line.
 * It is driven by the CPU itself. Raised when the MT
 * block wants to wake a VPE from an inactive state and
 * cleared when VPE goes from active to inactive.
 */
#define CPU_INTERRUPT_WAKE CPU_INTERRUPT_TGT_INT_0

int cpu_mips_exec(CPUMIPSState *s);
CPUMIPSState *cpu_mips_init(const char *cpu_model);
//~ uint32_t cpu_mips_get_clock (void);
int cpu_mips_signal_handler(int host_signum, void *pinfo, void *puc);

/* mips_timer.c */
uint32_t cpu_mips_get_random (CPUState *env);
uint32_t cpu_mips_get_count (CPUState *env);
void cpu_mips_store_count (CPUState *env, uint32_t value);
void cpu_mips_store_compare (CPUState *env, uint32_t value);
void cpu_mips_start_count(CPUState *env);
void cpu_mips_stop_count(CPUState *env);

/* mips_int.c */
void cpu_mips_soft_irq(CPUState *env, int irq, int level);

/* helper.c */
int cpu_mips_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                               int mmu_idx);
#define cpu_handle_mmu_fault cpu_mips_handle_mmu_fault
void do_interrupt (CPUState *env);
#if !defined(CONFIG_USER_ONLY)
void r4k_invalidate_tlb (CPUState *env, int idx, int use_extra);
target_phys_addr_t cpu_mips_translate_address (CPUState *env, target_ulong address,
		                               int rw);
#endif

static inline void cpu_get_tb_cpu_state(CPUState *env, target_ulong *pc,
                                        target_ulong *cs_base, int *flags)
{
    *pc = env->active_tc.PC;
    *cs_base = 0;
    *flags = env->hflags & (MIPS_HFLAG_TMASK | MIPS_HFLAG_BMASK);
}

static inline void cpu_set_tls(CPUState *env, target_ulong newtls)
{
    env->tls_value = newtls;
}

static inline int mips_vpe_active(CPUState *env)
{
    int active = 1;

    /* Check that the VPE is enabled.  */
    if (!(env->mvp->CP0_MVPControl & (1 << CP0MVPCo_EVP))) {
        active = 0;
    }
    /* Check that the VPE is actived.  */
    if (!(env->CP0_VPEConf0 & (1 << CP0VPEC0_VPA))) {
        active = 0;
    }

    /* Now verify that there are active thread contexts in the VPE.

       This assumes the CPU model will internally reschedule threads
       if the active one goes to sleep. If there are no threads available
       the active one will be in a sleeping state, and we can turn off
       the entire VPE.  */
    if (!(env->active_tc.CP0_TCStatus & (1 << CP0TCSt_A))) {
        /* TC is not activated.  */
        active = 0;
    }
    if (env->active_tc.CP0_TCHalt & 1) {
        /* TC is in halt state.  */
        active = 0;
    }

    return active;
}

static inline int cpu_has_work(CPUState *env)
{
    int has_work = 0;

    /* It is implementation dependent if non-enabled interrupts
       wake-up the CPU, however most of the implementations only
       check for interrupts that can be taken. */
    if ((env->interrupt_request & CPU_INTERRUPT_HARD) &&
        cpu_mips_hw_interrupts_pending(env)) {
        has_work = 1;
    }

    /* MIPS-MT has the ability to halt the CPU.  */
    if (env->CP0_Config3 & (1 << CP0C3_MT)) {
        /* The QEMU model will issue an _WAKE request whenever the CPUs
           should be woken up.  */
        if (env->interrupt_request & CPU_INTERRUPT_WAKE) {
            has_work = 1;
        }

        if (!mips_vpe_active(env)) {
            has_work = 0;
        }
    }
    return has_work;
}

#include "exec-all.h"

static inline void cpu_pc_from_tb(CPUState *env, TranslationBlock *tb)
{
    env->active_tc.PC = tb->pc;
    env->hflags &= ~MIPS_HFLAG_BMASK;
    env->hflags |= tb->flags & MIPS_HFLAG_BMASK;
}

#endif /* !defined (__MIPS_CPU_H__) */
