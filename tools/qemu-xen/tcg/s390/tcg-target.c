/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2009 Ulrich Hecht <uli@suse.de>
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 * Copyright (c) 2010 Richard Henderson <rth@twiddle.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* ??? The translation blocks produced by TCG are generally small enough to
   be entirely reachable with a 16-bit displacement.  Leaving the option for
   a 32-bit displacement here Just In Case.  */
#define USE_LONG_BRANCHES 0

#define TCG_CT_CONST_32    0x0100
#define TCG_CT_CONST_NEG   0x0200
#define TCG_CT_CONST_ADDI  0x0400
#define TCG_CT_CONST_MULI  0x0800
#define TCG_CT_CONST_ANDI  0x1000
#define TCG_CT_CONST_ORI   0x2000
#define TCG_CT_CONST_XORI  0x4000
#define TCG_CT_CONST_CMPI  0x8000

/* Several places within the instruction set 0 means "no register"
   rather than TCG_REG_R0.  */
#define TCG_REG_NONE    0

/* A scratch register that may be be used throughout the backend.  */
#define TCG_TMP0        TCG_REG_R14

#ifdef CONFIG_USE_GUEST_BASE
#define TCG_GUEST_BASE_REG TCG_REG_R13
#else
#define TCG_GUEST_BASE_REG TCG_REG_R0
#endif

#ifndef GUEST_BASE
#define GUEST_BASE 0
#endif


/* All of the following instructions are prefixed with their instruction
   format, and are defined as 8- or 16-bit quantities, even when the two
   halves of the 16-bit quantity may appear 32 bits apart in the insn.
   This makes it easy to copy the values from the tables in Appendix B.  */
typedef enum S390Opcode {
    RIL_AFI     = 0xc209,
    RIL_AGFI    = 0xc208,
    RIL_ALGFI   = 0xc20a,
    RIL_BRASL   = 0xc005,
    RIL_BRCL    = 0xc004,
    RIL_CFI     = 0xc20d,
    RIL_CGFI    = 0xc20c,
    RIL_CLFI    = 0xc20f,
    RIL_CLGFI   = 0xc20e,
    RIL_IIHF    = 0xc008,
    RIL_IILF    = 0xc009,
    RIL_LARL    = 0xc000,
    RIL_LGFI    = 0xc001,
    RIL_LGRL    = 0xc408,
    RIL_LLIHF   = 0xc00e,
    RIL_LLILF   = 0xc00f,
    RIL_LRL     = 0xc40d,
    RIL_MSFI    = 0xc201,
    RIL_MSGFI   = 0xc200,
    RIL_NIHF    = 0xc00a,
    RIL_NILF    = 0xc00b,
    RIL_OIHF    = 0xc00c,
    RIL_OILF    = 0xc00d,
    RIL_XIHF    = 0xc006,
    RIL_XILF    = 0xc007,

    RI_AGHI     = 0xa70b,
    RI_AHI      = 0xa70a,
    RI_BRC      = 0xa704,
    RI_IIHH     = 0xa500,
    RI_IIHL     = 0xa501,
    RI_IILH     = 0xa502,
    RI_IILL     = 0xa503,
    RI_LGHI     = 0xa709,
    RI_LLIHH    = 0xa50c,
    RI_LLIHL    = 0xa50d,
    RI_LLILH    = 0xa50e,
    RI_LLILL    = 0xa50f,
    RI_MGHI     = 0xa70d,
    RI_MHI      = 0xa70c,
    RI_NIHH     = 0xa504,
    RI_NIHL     = 0xa505,
    RI_NILH     = 0xa506,
    RI_NILL     = 0xa507,
    RI_OIHH     = 0xa508,
    RI_OIHL     = 0xa509,
    RI_OILH     = 0xa50a,
    RI_OILL     = 0xa50b,

    RIE_CGIJ    = 0xec7c,
    RIE_CGRJ    = 0xec64,
    RIE_CIJ     = 0xec7e,
    RIE_CLGRJ   = 0xec65,
    RIE_CLIJ    = 0xec7f,
    RIE_CLGIJ   = 0xec7d,
    RIE_CLRJ    = 0xec77,
    RIE_CRJ     = 0xec76,

    RRE_AGR     = 0xb908,
    RRE_CGR     = 0xb920,
    RRE_CLGR    = 0xb921,
    RRE_DLGR    = 0xb987,
    RRE_DLR     = 0xb997,
    RRE_DSGFR   = 0xb91d,
    RRE_DSGR    = 0xb90d,
    RRE_LGBR    = 0xb906,
    RRE_LCGR    = 0xb903,
    RRE_LGFR    = 0xb914,
    RRE_LGHR    = 0xb907,
    RRE_LGR     = 0xb904,
    RRE_LLGCR   = 0xb984,
    RRE_LLGFR   = 0xb916,
    RRE_LLGHR   = 0xb985,
    RRE_LRVR    = 0xb91f,
    RRE_LRVGR   = 0xb90f,
    RRE_LTGR    = 0xb902,
    RRE_MSGR    = 0xb90c,
    RRE_MSR     = 0xb252,
    RRE_NGR     = 0xb980,
    RRE_OGR     = 0xb981,
    RRE_SGR     = 0xb909,
    RRE_XGR     = 0xb982,

    RR_AR       = 0x1a,
    RR_BASR     = 0x0d,
    RR_BCR      = 0x07,
    RR_CLR      = 0x15,
    RR_CR       = 0x19,
    RR_DR       = 0x1d,
    RR_LCR      = 0x13,
    RR_LR       = 0x18,
    RR_LTR      = 0x12,
    RR_NR       = 0x14,
    RR_OR       = 0x16,
    RR_SR       = 0x1b,
    RR_XR       = 0x17,

    RSY_RLL     = 0xeb1d,
    RSY_RLLG    = 0xeb1c,
    RSY_SLLG    = 0xeb0d,
    RSY_SRAG    = 0xeb0a,
    RSY_SRLG    = 0xeb0c,

    RS_SLL      = 0x89,
    RS_SRA      = 0x8a,
    RS_SRL      = 0x88,

    RXY_AG      = 0xe308,
    RXY_AY      = 0xe35a,
    RXY_CG      = 0xe320,
    RXY_CY      = 0xe359,
    RXY_LB      = 0xe376,
    RXY_LG      = 0xe304,
    RXY_LGB     = 0xe377,
    RXY_LGF     = 0xe314,
    RXY_LGH     = 0xe315,
    RXY_LHY     = 0xe378,
    RXY_LLGC    = 0xe390,
    RXY_LLGF    = 0xe316,
    RXY_LLGH    = 0xe391,
    RXY_LMG     = 0xeb04,
    RXY_LRV     = 0xe31e,
    RXY_LRVG    = 0xe30f,
    RXY_LRVH    = 0xe31f,
    RXY_LY      = 0xe358,
    RXY_STCY    = 0xe372,
    RXY_STG     = 0xe324,
    RXY_STHY    = 0xe370,
    RXY_STMG    = 0xeb24,
    RXY_STRV    = 0xe33e,
    RXY_STRVG   = 0xe32f,
    RXY_STRVH   = 0xe33f,
    RXY_STY     = 0xe350,

    RX_A        = 0x5a,
    RX_C        = 0x59,
    RX_L        = 0x58,
    RX_LH       = 0x48,
    RX_ST       = 0x50,
    RX_STC      = 0x42,
    RX_STH      = 0x40,
} S390Opcode;

#define LD_SIGNED      0x04
#define LD_UINT8       0x00
#define LD_INT8        (LD_UINT8 | LD_SIGNED)
#define LD_UINT16      0x01
#define LD_INT16       (LD_UINT16 | LD_SIGNED)
#define LD_UINT32      0x02
#define LD_INT32       (LD_UINT32 | LD_SIGNED)
#define LD_UINT64      0x03
#define LD_INT64       (LD_UINT64 | LD_SIGNED)

#ifndef NDEBUG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "%r0", "%r1", "%r2", "%r3", "%r4", "%r5", "%r6", "%r7",
    "%r8", "%r9", "%r10" "%r11" "%r12" "%r13" "%r14" "%r15"
};
#endif

/* Since R6 is a potential argument register, choose it last of the
   call-saved registers.  Likewise prefer the call-clobbered registers
   in reverse order to maximize the chance of avoiding the arguments.  */
static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_R13,
    TCG_REG_R12,
    TCG_REG_R11,
    TCG_REG_R10,
    TCG_REG_R9,
    TCG_REG_R8,
    TCG_REG_R7,
    TCG_REG_R6,
    TCG_REG_R14,
    TCG_REG_R0,
    TCG_REG_R1,
    TCG_REG_R5,
    TCG_REG_R4,
    TCG_REG_R3,
    TCG_REG_R2,
};

static const int tcg_target_call_iarg_regs[] = {
    TCG_REG_R2,
    TCG_REG_R3,
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
};

static const int tcg_target_call_oarg_regs[] = {
    TCG_REG_R2,
#if TCG_TARGET_REG_BITS == 32
    TCG_REG_R3
#endif
};

#define S390_CC_EQ      8
#define S390_CC_LT      4
#define S390_CC_GT      2
#define S390_CC_OV      1
#define S390_CC_NE      (S390_CC_LT | S390_CC_GT)
#define S390_CC_LE      (S390_CC_LT | S390_CC_EQ)
#define S390_CC_GE      (S390_CC_GT | S390_CC_EQ)
#define S390_CC_NEVER   0
#define S390_CC_ALWAYS  15

/* Condition codes that result from a COMPARE and COMPARE LOGICAL.  */
static const uint8_t tcg_cond_to_s390_cond[10] = {
    [TCG_COND_EQ]  = S390_CC_EQ,
    [TCG_COND_NE]  = S390_CC_NE,
    [TCG_COND_LT]  = S390_CC_LT,
    [TCG_COND_LE]  = S390_CC_LE,
    [TCG_COND_GT]  = S390_CC_GT,
    [TCG_COND_GE]  = S390_CC_GE,
    [TCG_COND_LTU] = S390_CC_LT,
    [TCG_COND_LEU] = S390_CC_LE,
    [TCG_COND_GTU] = S390_CC_GT,
    [TCG_COND_GEU] = S390_CC_GE,
};

/* Condition codes that result from a LOAD AND TEST.  Here, we have no
   unsigned instruction variation, however since the test is vs zero we
   can re-map the outcomes appropriately.  */
static const uint8_t tcg_cond_to_ltr_cond[10] = {
    [TCG_COND_EQ]  = S390_CC_EQ,
    [TCG_COND_NE]  = S390_CC_NE,
    [TCG_COND_LT]  = S390_CC_LT,
    [TCG_COND_LE]  = S390_CC_LE,
    [TCG_COND_GT]  = S390_CC_GT,
    [TCG_COND_GE]  = S390_CC_GE,
    [TCG_COND_LTU] = S390_CC_NEVER,
    [TCG_COND_LEU] = S390_CC_EQ,
    [TCG_COND_GTU] = S390_CC_NE,
    [TCG_COND_GEU] = S390_CC_ALWAYS,
};

#ifdef CONFIG_SOFTMMU

#include "../../softmmu_defs.h"

static void *qemu_ld_helpers[4] = {
    __ldb_mmu,
    __ldw_mmu,
    __ldl_mmu,
    __ldq_mmu,
};

static void *qemu_st_helpers[4] = {
    __stb_mmu,
    __stw_mmu,
    __stl_mmu,
    __stq_mmu,
};
#endif

static uint8_t *tb_ret_addr;

/* A list of relevant facilities used by this translator.  Some of these
   are required for proper operation, and these are checked at startup.  */

#define FACILITY_ZARCH_ACTIVE	(1ULL << (63 - 2))
#define FACILITY_LONG_DISP	(1ULL << (63 - 18))
#define FACILITY_EXT_IMM	(1ULL << (63 - 21))
#define FACILITY_GEN_INST_EXT	(1ULL << (63 - 34))

static uint64_t facilities;

static void patch_reloc(uint8_t *code_ptr, int type,
                        tcg_target_long value, tcg_target_long addend)
{
    tcg_target_long code_ptr_tl = (tcg_target_long)code_ptr;
    tcg_target_long pcrel2;

    /* ??? Not the usual definition of "addend".  */
    pcrel2 = (value - (code_ptr_tl + addend)) >> 1;

    switch (type) {
    case R_390_PC16DBL:
        assert(pcrel2 == (int16_t)pcrel2);
        *(int16_t *)code_ptr = pcrel2;
        break;
    case R_390_PC32DBL:
        assert(pcrel2 == (int32_t)pcrel2);
        *(int32_t *)code_ptr = pcrel2;
        break;
    default:
        tcg_abort();
        break;
    }
}

static int tcg_target_get_call_iarg_regs_count(int flags)
{
    return sizeof(tcg_target_call_iarg_regs) / sizeof(int);
}

/* parse target specific constraints */
static int target_parse_constraint(TCGArgConstraint *ct, const char **pct_str)
{
    const char *ct_str = *pct_str;

    switch (ct_str[0]) {
    case 'r':                  /* all registers */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffff);
        break;
    case 'R':                  /* not R0 */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffff);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R0);
        break;
    case 'L':                  /* qemu_ld/st constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffff);
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R2);
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R3);
        break;
    case 'a':                  /* force R2 for division */
        ct->ct |= TCG_CT_REG;
        tcg_regset_clear(ct->u.regs);
        tcg_regset_set_reg(ct->u.regs, TCG_REG_R2);
        break;
    case 'b':                  /* force R3 for division */
        ct->ct |= TCG_CT_REG;
        tcg_regset_clear(ct->u.regs);
        tcg_regset_set_reg(ct->u.regs, TCG_REG_R3);
        break;
    case 'N':                  /* force immediate negate */
        ct->ct |= TCG_CT_CONST_NEG;
        break;
    case 'W':                  /* force 32-bit ("word") immediate */
        ct->ct |= TCG_CT_CONST_32;
        break;
    case 'I':
        ct->ct |= TCG_CT_CONST_ADDI;
        break;
    case 'K':
        ct->ct |= TCG_CT_CONST_MULI;
        break;
    case 'A':
        ct->ct |= TCG_CT_CONST_ANDI;
        break;
    case 'O':
        ct->ct |= TCG_CT_CONST_ORI;
        break;
    case 'X':
        ct->ct |= TCG_CT_CONST_XORI;
        break;
    case 'C':
        ct->ct |= TCG_CT_CONST_CMPI;
        break;
    default:
        return -1;
    }
    ct_str++;
    *pct_str = ct_str;

    return 0;
}

/* Immediates to be used with logical AND.  This is an optimization only,
   since a full 64-bit immediate AND can always be performed with 4 sequential
   NI[LH][LH] instructions.  What we're looking for is immediates that we
   can load efficiently, and the immediate load plus the reg-reg AND is
   smaller than the sequential NI's.  */

static int tcg_match_andi(int ct, tcg_target_ulong val)
{
    int i;

    if (facilities & FACILITY_EXT_IMM) {
        if (ct & TCG_CT_CONST_32) {
            /* All 32-bit ANDs can be performed with 1 48-bit insn.  */
            return 1;
        }

        /* Zero-extensions.  */
        if (val == 0xff || val == 0xffff || val == 0xffffffff) {
            return 1;
        }
    } else {
        if (ct & TCG_CT_CONST_32) {
            val = (uint32_t)val;
        } else if (val == 0xffffffff) {
            return 1;
        }
    }

    /* Try all 32-bit insns that can perform it in one go.  */
    for (i = 0; i < 4; i++) {
        tcg_target_ulong mask = ~(0xffffull << i*16);
        if ((val & mask) == mask) {
            return 1;
        }
    }

    /* Look for 16-bit values performing the mask.  These are better
       to load with LLI[LH][LH].  */
    for (i = 0; i < 4; i++) {
        tcg_target_ulong mask = 0xffffull << i*16;
        if ((val & mask) == val) {
            return 0;
        }
    }

    /* Look for 32-bit values performing the 64-bit mask.  These
       are better to load with LLI[LH]F, or if extended immediates
       not available, with a pair of LLI insns.  */
    if ((ct & TCG_CT_CONST_32) == 0) {
        if (val <= 0xffffffff || (val & 0xffffffff) == 0) {
            return 0;
        }
    }

    return 1;
}

/* Immediates to be used with logical OR.  This is an optimization only,
   since a full 64-bit immediate OR can always be performed with 4 sequential
   OI[LH][LH] instructions.  What we're looking for is immediates that we
   can load efficiently, and the immediate load plus the reg-reg OR is
   smaller than the sequential OI's.  */

static int tcg_match_ori(int ct, tcg_target_long val)
{
    if (facilities & FACILITY_EXT_IMM) {
        if (ct & TCG_CT_CONST_32) {
            /* All 32-bit ORs can be performed with 1 48-bit insn.  */
            return 1;
        }
    }

    /* Look for negative values.  These are best to load with LGHI.  */
    if (val < 0) {
        if (val == (int16_t)val) {
            return 0;
        }
        if (facilities & FACILITY_EXT_IMM) {
            if (val == (int32_t)val) {
                return 0;
            }
        }
    }

    return 1;
}

/* Immediates to be used with logical XOR.  This is almost, but not quite,
   only an optimization.  XOR with immediate is only supported with the
   extended-immediate facility.  That said, there are a few patterns for
   which it is better to load the value into a register first.  */

static int tcg_match_xori(int ct, tcg_target_long val)
{
    if ((facilities & FACILITY_EXT_IMM) == 0) {
        return 0;
    }

    if (ct & TCG_CT_CONST_32) {
        /* All 32-bit XORs can be performed with 1 48-bit insn.  */
        return 1;
    }

    /* Look for negative values.  These are best to load with LGHI.  */
    if (val < 0 && val == (int32_t)val) {
        return 0;
    }

    return 1;
}

/* Imediates to be used with comparisons.  */

static int tcg_match_cmpi(int ct, tcg_target_long val)
{
    if (facilities & FACILITY_EXT_IMM) {
        /* The COMPARE IMMEDIATE instruction is available.  */
        if (ct & TCG_CT_CONST_32) {
            /* We have a 32-bit immediate and can compare against anything.  */
            return 1;
        } else {
            /* ??? We have no insight here into whether the comparison is
               signed or unsigned.  The COMPARE IMMEDIATE insn uses a 32-bit
               signed immediate, and the COMPARE LOGICAL IMMEDIATE insn uses
               a 32-bit unsigned immediate.  If we were to use the (semi)
               obvious "val == (int32_t)val" we would be enabling unsigned
               comparisons vs very large numbers.  The only solution is to
               take the intersection of the ranges.  */
            /* ??? Another possible solution is to simply lie and allow all
               constants here and force the out-of-range values into a temp
               register in tgen_cmp when we have knowledge of the actual
               comparison code in use.  */
            return val >= 0 && val <= 0x7fffffff;
        }
    } else {
        /* Only the LOAD AND TEST instruction is available.  */
        return val == 0;
    }
}

/* Test if a constant matches the constraint. */
static int tcg_target_const_match(tcg_target_long val,
                                  const TCGArgConstraint *arg_ct)
{
    int ct = arg_ct->ct;

    if (ct & TCG_CT_CONST) {
        return 1;
    }

    /* Handle the modifiers.  */
    if (ct & TCG_CT_CONST_NEG) {
        val = -val;
    }
    if (ct & TCG_CT_CONST_32) {
        val = (int32_t)val;
    }

    /* The following are mutually exclusive.  */
    if (ct & TCG_CT_CONST_ADDI) {
        /* Immediates that may be used with add.  If we have the
           extended-immediates facility then we have ADD IMMEDIATE
           with signed and unsigned 32-bit, otherwise we have only
           ADD HALFWORD IMMEDIATE with a signed 16-bit.  */
        if (facilities & FACILITY_EXT_IMM) {
            return val == (int32_t)val || val == (uint32_t)val;
        } else {
            return val == (int16_t)val;
        }
    } else if (ct & TCG_CT_CONST_MULI) {
        /* Immediates that may be used with multiply.  If we have the
           general-instruction-extensions, then we have MULTIPLY SINGLE
           IMMEDIATE with a signed 32-bit, otherwise we have only
           MULTIPLY HALFWORD IMMEDIATE, with a signed 16-bit.  */
        if (facilities & FACILITY_GEN_INST_EXT) {
            return val == (int32_t)val;
        } else {
            return val == (int16_t)val;
        }
    } else if (ct & TCG_CT_CONST_ANDI) {
        return tcg_match_andi(ct, val);
    } else if (ct & TCG_CT_CONST_ORI) {
        return tcg_match_ori(ct, val);
    } else if (ct & TCG_CT_CONST_XORI) {
        return tcg_match_xori(ct, val);
    } else if (ct & TCG_CT_CONST_CMPI) {
        return tcg_match_cmpi(ct, val);
    }

    return 0;
}

/* Emit instructions according to the given instruction format.  */

static void tcg_out_insn_RR(TCGContext *s, S390Opcode op, TCGReg r1, TCGReg r2)
{
    tcg_out16(s, (op << 8) | (r1 << 4) | r2);
}

static void tcg_out_insn_RRE(TCGContext *s, S390Opcode op,
                             TCGReg r1, TCGReg r2)
{
    tcg_out32(s, (op << 16) | (r1 << 4) | r2);
}

static void tcg_out_insn_RI(TCGContext *s, S390Opcode op, TCGReg r1, int i2)
{
    tcg_out32(s, (op << 16) | (r1 << 20) | (i2 & 0xffff));
}

static void tcg_out_insn_RIL(TCGContext *s, S390Opcode op, TCGReg r1, int i2)
{
    tcg_out16(s, op | (r1 << 4));
    tcg_out32(s, i2);
}

static void tcg_out_insn_RS(TCGContext *s, S390Opcode op, TCGReg r1,
                            TCGReg b2, TCGReg r3, int disp)
{
    tcg_out32(s, (op << 24) | (r1 << 20) | (r3 << 16) | (b2 << 12)
              | (disp & 0xfff));
}

static void tcg_out_insn_RSY(TCGContext *s, S390Opcode op, TCGReg r1,
                             TCGReg b2, TCGReg r3, int disp)
{
    tcg_out16(s, (op & 0xff00) | (r1 << 4) | r3);
    tcg_out32(s, (op & 0xff) | (b2 << 28)
              | ((disp & 0xfff) << 16) | ((disp & 0xff000) >> 4));
}

#define tcg_out_insn_RX   tcg_out_insn_RS
#define tcg_out_insn_RXY  tcg_out_insn_RSY

/* Emit an opcode with "type-checking" of the format.  */
#define tcg_out_insn(S, FMT, OP, ...) \
    glue(tcg_out_insn_,FMT)(S, glue(glue(FMT,_),OP), ## __VA_ARGS__)


/* emit 64-bit shifts */
static void tcg_out_sh64(TCGContext* s, S390Opcode op, TCGReg dest,
                         TCGReg src, TCGReg sh_reg, int sh_imm)
{
    tcg_out_insn_RSY(s, op, dest, sh_reg, src, sh_imm);
}

/* emit 32-bit shifts */
static void tcg_out_sh32(TCGContext* s, S390Opcode op, TCGReg dest,
                         TCGReg sh_reg, int sh_imm)
{
    tcg_out_insn_RS(s, op, dest, sh_reg, 0, sh_imm);
}

static void tcg_out_mov(TCGContext *s, TCGType type, TCGReg dst, TCGReg src)
{
    if (src != dst) {
        if (type == TCG_TYPE_I32) {
            tcg_out_insn(s, RR, LR, dst, src);
        } else {
            tcg_out_insn(s, RRE, LGR, dst, src);
        }
    }
}

/* load a register with an immediate value */
static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long sval)
{
    static const S390Opcode lli_insns[4] = {
        RI_LLILL, RI_LLILH, RI_LLIHL, RI_LLIHH
    };

    tcg_target_ulong uval = sval;
    int i;

    if (type == TCG_TYPE_I32) {
        uval = (uint32_t)sval;
        sval = (int32_t)sval;
    }

    /* Try all 32-bit insns that can load it in one go.  */
    if (sval >= -0x8000 && sval < 0x8000) {
        tcg_out_insn(s, RI, LGHI, ret, sval);
        return;
    }

    for (i = 0; i < 4; i++) {
        tcg_target_long mask = 0xffffull << i*16;
        if ((uval & mask) == uval) {
            tcg_out_insn_RI(s, lli_insns[i], ret, uval >> i*16);
            return;
        }
    }

    /* Try all 48-bit insns that can load it in one go.  */
    if (facilities & FACILITY_EXT_IMM) {
        if (sval == (int32_t)sval) {
            tcg_out_insn(s, RIL, LGFI, ret, sval);
            return;
        }
        if (uval <= 0xffffffff) {
            tcg_out_insn(s, RIL, LLILF, ret, uval);
            return;
        }
        if ((uval & 0xffffffff) == 0) {
            tcg_out_insn(s, RIL, LLIHF, ret, uval >> 31 >> 1);
            return;
        }
    }

    /* Try for PC-relative address load.  */
    if ((sval & 1) == 0) {
        intptr_t off = (sval - (intptr_t)s->code_ptr) >> 1;
        if (off == (int32_t)off) {
            tcg_out_insn(s, RIL, LARL, ret, off);
            return;
        }
    }

    /* If extended immediates are not present, then we may have to issue
       several instructions to load the low 32 bits.  */
    if (!(facilities & FACILITY_EXT_IMM)) {
        /* A 32-bit unsigned value can be loaded in 2 insns.  And given
           that the lli_insns loop above did not succeed, we know that
           both insns are required.  */
        if (uval <= 0xffffffff) {
            tcg_out_insn(s, RI, LLILL, ret, uval);
            tcg_out_insn(s, RI, IILH, ret, uval >> 16);
            return;
        }

        /* If all high bits are set, the value can be loaded in 2 or 3 insns.
           We first want to make sure that all the high bits get set.  With
           luck the low 16-bits can be considered negative to perform that for
           free, otherwise we load an explicit -1.  */
        if (sval >> 31 >> 1 == -1) {
            if (uval & 0x8000) {
                tcg_out_insn(s, RI, LGHI, ret, uval);
            } else {
                tcg_out_insn(s, RI, LGHI, ret, -1);
                tcg_out_insn(s, RI, IILL, ret, uval);
            }
            tcg_out_insn(s, RI, IILH, ret, uval >> 16);
            return;
        }
    }

    /* If we get here, both the high and low parts have non-zero bits.  */

    /* Recurse to load the lower 32-bits.  */
    tcg_out_movi(s, TCG_TYPE_I32, ret, sval);

    /* Insert data into the high 32-bits.  */
    uval = uval >> 31 >> 1;
    if (facilities & FACILITY_EXT_IMM) {
        if (uval < 0x10000) {
            tcg_out_insn(s, RI, IIHL, ret, uval);
        } else if ((uval & 0xffff) == 0) {
            tcg_out_insn(s, RI, IIHH, ret, uval >> 16);
        } else {
            tcg_out_insn(s, RIL, IIHF, ret, uval);
        }
    } else {
        if (uval & 0xffff) {
            tcg_out_insn(s, RI, IIHL, ret, uval);
        }
        if (uval & 0xffff0000) {
            tcg_out_insn(s, RI, IIHH, ret, uval >> 16);
        }
    }
}


/* Emit a load/store type instruction.  Inputs are:
   DATA:     The register to be loaded or stored.
   BASE+OFS: The effective address.
   OPC_RX:   If the operation has an RX format opcode (e.g. STC), otherwise 0.
   OPC_RXY:  The RXY format opcode for the operation (e.g. STCY).  */

static void tcg_out_mem(TCGContext *s, S390Opcode opc_rx, S390Opcode opc_rxy,
                        TCGReg data, TCGReg base, TCGReg index,
                        tcg_target_long ofs)
{
    if (ofs < -0x80000 || ofs >= 0x80000) {
        /* Combine the low 16 bits of the offset with the actual load insn;
           the high 48 bits must come from an immediate load.  */
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_TMP0, ofs & ~0xffff);
        ofs &= 0xffff;

        /* If we were already given an index register, add it in.  */
        if (index != TCG_REG_NONE) {
            tcg_out_insn(s, RRE, AGR, TCG_TMP0, index);
        }
        index = TCG_TMP0;
    }

    if (opc_rx && ofs >= 0 && ofs < 0x1000) {
        tcg_out_insn_RX(s, opc_rx, data, base, index, ofs);
    } else {
        tcg_out_insn_RXY(s, opc_rxy, data, base, index, ofs);
    }
}


/* load data without address translation or endianness conversion */
static inline void tcg_out_ld(TCGContext *s, TCGType type, TCGReg data,
                              TCGReg base, tcg_target_long ofs)
{
    if (type == TCG_TYPE_I32) {
        tcg_out_mem(s, RX_L, RXY_LY, data, base, TCG_REG_NONE, ofs);
    } else {
        tcg_out_mem(s, 0, RXY_LG, data, base, TCG_REG_NONE, ofs);
    }
}

static inline void tcg_out_st(TCGContext *s, TCGType type, TCGReg data,
                              TCGReg base, tcg_target_long ofs)
{
    if (type == TCG_TYPE_I32) {
        tcg_out_mem(s, RX_ST, RXY_STY, data, base, TCG_REG_NONE, ofs);
    } else {
        tcg_out_mem(s, 0, RXY_STG, data, base, TCG_REG_NONE, ofs);
    }
}

/* load data from an absolute host address */
static void tcg_out_ld_abs(TCGContext *s, TCGType type, TCGReg dest, void *abs)
{
    tcg_target_long addr = (tcg_target_long)abs;

    if (facilities & FACILITY_GEN_INST_EXT) {
        tcg_target_long disp = (addr - (tcg_target_long)s->code_ptr) >> 1;
        if (disp == (int32_t)disp) {
            if (type == TCG_TYPE_I32) {
                tcg_out_insn(s, RIL, LRL, dest, disp);
            } else {
                tcg_out_insn(s, RIL, LGRL, dest, disp);
            }
            return;
        }
    }

    tcg_out_movi(s, TCG_TYPE_PTR, dest, addr & ~0xffff);
    tcg_out_ld(s, type, dest, dest, addr & 0xffff);
}

static void tgen_ext8s(TCGContext *s, TCGType type, TCGReg dest, TCGReg src)
{
    if (facilities & FACILITY_EXT_IMM) {
        tcg_out_insn(s, RRE, LGBR, dest, src);
        return;
    }

    if (type == TCG_TYPE_I32) {
        if (dest == src) {
            tcg_out_sh32(s, RS_SLL, dest, TCG_REG_NONE, 24);
        } else {
            tcg_out_sh64(s, RSY_SLLG, dest, src, TCG_REG_NONE, 24);
        }
        tcg_out_sh32(s, RS_SRA, dest, TCG_REG_NONE, 24);
    } else {
        tcg_out_sh64(s, RSY_SLLG, dest, src, TCG_REG_NONE, 56);
        tcg_out_sh64(s, RSY_SRAG, dest, dest, TCG_REG_NONE, 56);
    }
}

static void tgen_ext8u(TCGContext *s, TCGType type, TCGReg dest, TCGReg src)
{
    if (facilities & FACILITY_EXT_IMM) {
        tcg_out_insn(s, RRE, LLGCR, dest, src);
        return;
    }

    if (dest == src) {
        tcg_out_movi(s, type, TCG_TMP0, 0xff);
        src = TCG_TMP0;
    } else {
        tcg_out_movi(s, type, dest, 0xff);
    }
    if (type == TCG_TYPE_I32) {
        tcg_out_insn(s, RR, NR, dest, src);
    } else {
        tcg_out_insn(s, RRE, NGR, dest, src);
    }
}

static void tgen_ext16s(TCGContext *s, TCGType type, TCGReg dest, TCGReg src)
{
    if (facilities & FACILITY_EXT_IMM) {
        tcg_out_insn(s, RRE, LGHR, dest, src);
        return;
    }

    if (type == TCG_TYPE_I32) {
        if (dest == src) {
            tcg_out_sh32(s, RS_SLL, dest, TCG_REG_NONE, 16);
        } else {
            tcg_out_sh64(s, RSY_SLLG, dest, src, TCG_REG_NONE, 16);
        }
        tcg_out_sh32(s, RS_SRA, dest, TCG_REG_NONE, 16);
    } else {
        tcg_out_sh64(s, RSY_SLLG, dest, src, TCG_REG_NONE, 48);
        tcg_out_sh64(s, RSY_SRAG, dest, dest, TCG_REG_NONE, 48);
    }
}

static void tgen_ext16u(TCGContext *s, TCGType type, TCGReg dest, TCGReg src)
{
    if (facilities & FACILITY_EXT_IMM) {
        tcg_out_insn(s, RRE, LLGHR, dest, src);
        return;
    }

    if (dest == src) {
        tcg_out_movi(s, type, TCG_TMP0, 0xffff);
        src = TCG_TMP0;
    } else {
        tcg_out_movi(s, type, dest, 0xffff);
    }
    if (type == TCG_TYPE_I32) {
        tcg_out_insn(s, RR, NR, dest, src);
    } else {
        tcg_out_insn(s, RRE, NGR, dest, src);
    }
}

static inline void tgen_ext32s(TCGContext *s, TCGReg dest, TCGReg src)
{
    tcg_out_insn(s, RRE, LGFR, dest, src);
}

static inline void tgen_ext32u(TCGContext *s, TCGReg dest, TCGReg src)
{
    tcg_out_insn(s, RRE, LLGFR, dest, src);
}

static inline void tgen32_addi(TCGContext *s, TCGReg dest, int32_t val)
{
    if (val == (int16_t)val) {
        tcg_out_insn(s, RI, AHI, dest, val);
    } else {
        tcg_out_insn(s, RIL, AFI, dest, val);
    }
}

static inline void tgen64_addi(TCGContext *s, TCGReg dest, int64_t val)
{
    if (val == (int16_t)val) {
        tcg_out_insn(s, RI, AGHI, dest, val);
    } else if (val == (int32_t)val) {
        tcg_out_insn(s, RIL, AGFI, dest, val);
    } else if (val == (uint32_t)val) {
        tcg_out_insn(s, RIL, ALGFI, dest, val);
    } else {
        tcg_abort();
    }

}

static void tgen64_andi(TCGContext *s, TCGReg dest, tcg_target_ulong val)
{
    static const S390Opcode ni_insns[4] = {
        RI_NILL, RI_NILH, RI_NIHL, RI_NIHH
    };
    static const S390Opcode nif_insns[2] = {
        RIL_NILF, RIL_NIHF
    };

    int i;

    /* Look for no-op.  */
    if (val == -1) {
        return;
    }

    /* Look for the zero-extensions.  */
    if (val == 0xffffffff) {
        tgen_ext32u(s, dest, dest);
        return;
    }

    if (facilities & FACILITY_EXT_IMM) {
        if (val == 0xff) {
            tgen_ext8u(s, TCG_TYPE_I64, dest, dest);
            return;
        }
        if (val == 0xffff) {
            tgen_ext16u(s, TCG_TYPE_I64, dest, dest);
            return;
        }

        /* Try all 32-bit insns that can perform it in one go.  */
        for (i = 0; i < 4; i++) {
            tcg_target_ulong mask = ~(0xffffull << i*16);
            if ((val & mask) == mask) {
                tcg_out_insn_RI(s, ni_insns[i], dest, val >> i*16);
                return;
            }
        }

        /* Try all 48-bit insns that can perform it in one go.  */
        if (facilities & FACILITY_EXT_IMM) {
            for (i = 0; i < 2; i++) {
                tcg_target_ulong mask = ~(0xffffffffull << i*32);
                if ((val & mask) == mask) {
                    tcg_out_insn_RIL(s, nif_insns[i], dest, val >> i*32);
                    return;
                }
            }
        }

        /* Perform the AND via sequential modifications to the high and low
           parts.  Do this via recursion to handle 16-bit vs 32-bit masks in
           each half.  */
        tgen64_andi(s, dest, val | 0xffffffff00000000ull);
        tgen64_andi(s, dest, val | 0x00000000ffffffffull);
    } else {
        /* With no extended-immediate facility, just emit the sequence.  */
        for (i = 0; i < 4; i++) {
            tcg_target_ulong mask = 0xffffull << i*16;
            if ((val & mask) != mask) {
                tcg_out_insn_RI(s, ni_insns[i], dest, val >> i*16);
            }
        }
    }
}

static void tgen64_ori(TCGContext *s, TCGReg dest, tcg_target_ulong val)
{
    static const S390Opcode oi_insns[4] = {
        RI_OILL, RI_OILH, RI_OIHL, RI_OIHH
    };
    static const S390Opcode nif_insns[2] = {
        RIL_OILF, RIL_OIHF
    };

    int i;

    /* Look for no-op.  */
    if (val == 0) {
        return;
    }

    if (facilities & FACILITY_EXT_IMM) {
        /* Try all 32-bit insns that can perform it in one go.  */
        for (i = 0; i < 4; i++) {
            tcg_target_ulong mask = (0xffffull << i*16);
            if ((val & mask) != 0 && (val & ~mask) == 0) {
                tcg_out_insn_RI(s, oi_insns[i], dest, val >> i*16);
                return;
            }
        }

        /* Try all 48-bit insns that can perform it in one go.  */
        for (i = 0; i < 2; i++) {
            tcg_target_ulong mask = (0xffffffffull << i*32);
            if ((val & mask) != 0 && (val & ~mask) == 0) {
                tcg_out_insn_RIL(s, nif_insns[i], dest, val >> i*32);
                return;
            }
        }

        /* Perform the OR via sequential modifications to the high and
           low parts.  Do this via recursion to handle 16-bit vs 32-bit
           masks in each half.  */
        tgen64_ori(s, dest, val & 0x00000000ffffffffull);
        tgen64_ori(s, dest, val & 0xffffffff00000000ull);
    } else {
        /* With no extended-immediate facility, we don't need to be so
           clever.  Just iterate over the insns and mask in the constant.  */
        for (i = 0; i < 4; i++) {
            tcg_target_ulong mask = (0xffffull << i*16);
            if ((val & mask) != 0) {
                tcg_out_insn_RI(s, oi_insns[i], dest, val >> i*16);
            }
        }
    }
}

static void tgen64_xori(TCGContext *s, TCGReg dest, tcg_target_ulong val)
{
    /* Perform the xor by parts.  */
    if (val & 0xffffffff) {
        tcg_out_insn(s, RIL, XILF, dest, val);
    }
    if (val > 0xffffffff) {
        tcg_out_insn(s, RIL, XIHF, dest, val >> 31 >> 1);
    }
}

static int tgen_cmp(TCGContext *s, TCGType type, TCGCond c, TCGReg r1,
                    TCGArg c2, int c2const)
{
    bool is_unsigned = (c > TCG_COND_GT);
    if (c2const) {
        if (c2 == 0) {
            if (type == TCG_TYPE_I32) {
                tcg_out_insn(s, RR, LTR, r1, r1);
            } else {
                tcg_out_insn(s, RRE, LTGR, r1, r1);
            }
            return tcg_cond_to_ltr_cond[c];
        } else {
            if (is_unsigned) {
                if (type == TCG_TYPE_I32) {
                    tcg_out_insn(s, RIL, CLFI, r1, c2);
                } else {
                    tcg_out_insn(s, RIL, CLGFI, r1, c2);
                }
            } else {
                if (type == TCG_TYPE_I32) {
                    tcg_out_insn(s, RIL, CFI, r1, c2);
                } else {
                    tcg_out_insn(s, RIL, CGFI, r1, c2);
                }
            }
        }
    } else {
        if (is_unsigned) {
            if (type == TCG_TYPE_I32) {
                tcg_out_insn(s, RR, CLR, r1, c2);
            } else {
                tcg_out_insn(s, RRE, CLGR, r1, c2);
            }
        } else {
            if (type == TCG_TYPE_I32) {
                tcg_out_insn(s, RR, CR, r1, c2);
            } else {
                tcg_out_insn(s, RRE, CGR, r1, c2);
            }
        }
    }
    return tcg_cond_to_s390_cond[c];
}

static void tgen_setcond(TCGContext *s, TCGType type, TCGCond c,
                         TCGReg dest, TCGReg r1, TCGArg c2, int c2const)
{
    int cc = tgen_cmp(s, type, c, r1, c2, c2const);

    /* Emit: r1 = 1; if (cc) goto over; r1 = 0; over:  */
    tcg_out_movi(s, type, dest, 1);
    tcg_out_insn(s, RI, BRC, cc, (4 + 4) >> 1);
    tcg_out_movi(s, type, dest, 0);
}

static void tgen_gotoi(TCGContext *s, int cc, tcg_target_long dest)
{
    tcg_target_long off = (dest - (tcg_target_long)s->code_ptr) >> 1;
    if (off > -0x8000 && off < 0x7fff) {
        tcg_out_insn(s, RI, BRC, cc, off);
    } else if (off == (int32_t)off) {
        tcg_out_insn(s, RIL, BRCL, cc, off);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_TMP0, dest);
        tcg_out_insn(s, RR, BCR, cc, TCG_TMP0);
    }
}

static void tgen_branch(TCGContext *s, int cc, int labelno)
{
    TCGLabel* l = &s->labels[labelno];
    if (l->has_value) {
        tgen_gotoi(s, cc, l->u.value);
    } else if (USE_LONG_BRANCHES) {
        tcg_out16(s, RIL_BRCL | (cc << 4));
        tcg_out_reloc(s, s->code_ptr, R_390_PC32DBL, labelno, -2);
        s->code_ptr += 4;
    } else {
        tcg_out16(s, RI_BRC | (cc << 4));
        tcg_out_reloc(s, s->code_ptr, R_390_PC16DBL, labelno, -2);
        s->code_ptr += 2;
    }
}

static void tgen_compare_branch(TCGContext *s, S390Opcode opc, int cc,
                                TCGReg r1, TCGReg r2, int labelno)
{
    TCGLabel* l = &s->labels[labelno];
    tcg_target_long off;

    if (l->has_value) {
        off = (l->u.value - (tcg_target_long)s->code_ptr) >> 1;
    } else {
        /* We need to keep the offset unchanged for retranslation.  */
        off = ((int16_t *)s->code_ptr)[1];
        tcg_out_reloc(s, s->code_ptr + 2, R_390_PC16DBL, labelno, -2);
    }

    tcg_out16(s, (opc & 0xff00) | (r1 << 4) | r2);
    tcg_out16(s, off);
    tcg_out16(s, cc << 12 | (opc & 0xff));
}

static void tgen_compare_imm_branch(TCGContext *s, S390Opcode opc, int cc,
                                    TCGReg r1, int i2, int labelno)
{
    TCGLabel* l = &s->labels[labelno];
    tcg_target_long off;

    if (l->has_value) {
        off = (l->u.value - (tcg_target_long)s->code_ptr) >> 1;
    } else {
        /* We need to keep the offset unchanged for retranslation.  */
        off = ((int16_t *)s->code_ptr)[1];
        tcg_out_reloc(s, s->code_ptr + 2, R_390_PC16DBL, labelno, -2);
    }

    tcg_out16(s, (opc & 0xff00) | (r1 << 4) | cc);
    tcg_out16(s, off);
    tcg_out16(s, (i2 << 8) | (opc & 0xff));
}

static void tgen_brcond(TCGContext *s, TCGType type, TCGCond c,
                        TCGReg r1, TCGArg c2, int c2const, int labelno)
{
    int cc;

    if (facilities & FACILITY_GEN_INST_EXT) {
        bool is_unsigned = (c > TCG_COND_GT);
        bool in_range;
        S390Opcode opc;

        cc = tcg_cond_to_s390_cond[c];

        if (!c2const) {
            opc = (type == TCG_TYPE_I32
                   ? (is_unsigned ? RIE_CLRJ : RIE_CRJ)
                   : (is_unsigned ? RIE_CLGRJ : RIE_CGRJ));
            tgen_compare_branch(s, opc, cc, r1, c2, labelno);
            return;
        }

        /* COMPARE IMMEDIATE AND BRANCH RELATIVE has an 8-bit immediate field.
           If the immediate we've been given does not fit that range, we'll
           fall back to separate compare and branch instructions using the
           larger comparison range afforded by COMPARE IMMEDIATE.  */
        if (type == TCG_TYPE_I32) {
            if (is_unsigned) {
                opc = RIE_CLIJ;
                in_range = (uint32_t)c2 == (uint8_t)c2;
            } else {
                opc = RIE_CIJ;
                in_range = (int32_t)c2 == (int8_t)c2;
            }
        } else {
            if (is_unsigned) {
                opc = RIE_CLGIJ;
                in_range = (uint64_t)c2 == (uint8_t)c2;
            } else {
                opc = RIE_CGIJ;
                in_range = (int64_t)c2 == (int8_t)c2;
            }
        }
        if (in_range) {
            tgen_compare_imm_branch(s, opc, cc, r1, c2, labelno);
            return;
        }
    }

    cc = tgen_cmp(s, type, c, r1, c2, c2const);
    tgen_branch(s, cc, labelno);
}

static void tgen_calli(TCGContext *s, tcg_target_long dest)
{
    tcg_target_long off = (dest - (tcg_target_long)s->code_ptr) >> 1;
    if (off == (int32_t)off) {
        tcg_out_insn(s, RIL, BRASL, TCG_REG_R14, off);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_TMP0, dest);
        tcg_out_insn(s, RR, BASR, TCG_REG_R14, TCG_TMP0);
    }
}

static void tcg_out_qemu_ld_direct(TCGContext *s, int opc, TCGReg data,
                                   TCGReg base, TCGReg index, int disp)
{
#ifdef TARGET_WORDS_BIGENDIAN
    const int bswap = 0;
#else
    const int bswap = 1;
#endif
    switch (opc) {
    case LD_UINT8:
        tcg_out_insn(s, RXY, LLGC, data, base, index, disp);
        break;
    case LD_INT8:
        tcg_out_insn(s, RXY, LGB, data, base, index, disp);
        break;
    case LD_UINT16:
        if (bswap) {
            /* swapped unsigned halfword load with upper bits zeroed */
            tcg_out_insn(s, RXY, LRVH, data, base, index, disp);
            tgen_ext16u(s, TCG_TYPE_I64, data, data);
        } else {
            tcg_out_insn(s, RXY, LLGH, data, base, index, disp);
        }
        break;
    case LD_INT16:
        if (bswap) {
            /* swapped sign-extended halfword load */
            tcg_out_insn(s, RXY, LRVH, data, base, index, disp);
            tgen_ext16s(s, TCG_TYPE_I64, data, data);
        } else {
            tcg_out_insn(s, RXY, LGH, data, base, index, disp);
        }
        break;
    case LD_UINT32:
        if (bswap) {
            /* swapped unsigned int load with upper bits zeroed */
            tcg_out_insn(s, RXY, LRV, data, base, index, disp);
            tgen_ext32u(s, data, data);
        } else {
            tcg_out_insn(s, RXY, LLGF, data, base, index, disp);
        }
        break;
    case LD_INT32:
        if (bswap) {
            /* swapped sign-extended int load */
            tcg_out_insn(s, RXY, LRV, data, base, index, disp);
            tgen_ext32s(s, data, data);
        } else {
            tcg_out_insn(s, RXY, LGF, data, base, index, disp);
        }
        break;
    case LD_UINT64:
        if (bswap) {
            tcg_out_insn(s, RXY, LRVG, data, base, index, disp);
        } else {
            tcg_out_insn(s, RXY, LG, data, base, index, disp);
        }
        break;
    default:
        tcg_abort();
    }
}

static void tcg_out_qemu_st_direct(TCGContext *s, int opc, TCGReg data,
                                   TCGReg base, TCGReg index, int disp)
{
#ifdef TARGET_WORDS_BIGENDIAN
    const int bswap = 0;
#else
    const int bswap = 1;
#endif
    switch (opc) {
    case LD_UINT8:
        if (disp >= 0 && disp < 0x1000) {
            tcg_out_insn(s, RX, STC, data, base, index, disp);
        } else {
            tcg_out_insn(s, RXY, STCY, data, base, index, disp);
        }
        break;
    case LD_UINT16:
        if (bswap) {
            tcg_out_insn(s, RXY, STRVH, data, base, index, disp);
        } else if (disp >= 0 && disp < 0x1000) {
            tcg_out_insn(s, RX, STH, data, base, index, disp);
        } else {
            tcg_out_insn(s, RXY, STHY, data, base, index, disp);
        }
        break;
    case LD_UINT32:
        if (bswap) {
            tcg_out_insn(s, RXY, STRV, data, base, index, disp);
        } else if (disp >= 0 && disp < 0x1000) {
            tcg_out_insn(s, RX, ST, data, base, index, disp);
        } else {
            tcg_out_insn(s, RXY, STY, data, base, index, disp);
        }
        break;
    case LD_UINT64:
        if (bswap) {
            tcg_out_insn(s, RXY, STRVG, data, base, index, disp);
        } else {
            tcg_out_insn(s, RXY, STG, data, base, index, disp);
        }
        break;
    default:
        tcg_abort();
    }
}

#if defined(CONFIG_SOFTMMU)
static void tgen64_andi_tmp(TCGContext *s, TCGReg dest, tcg_target_ulong val)
{
    if (tcg_match_andi(0, val)) {
        tcg_out_movi(s, TCG_TYPE_I64, TCG_TMP0, val);
        tcg_out_insn(s, RRE, NGR, dest, TCG_TMP0);
    } else {
        tgen64_andi(s, dest, val);
    }
}

static void tcg_prepare_qemu_ldst(TCGContext* s, TCGReg data_reg,
                                  TCGReg addr_reg, int mem_index, int opc,
                                  uint16_t **label2_ptr_p, int is_store)
{
    const TCGReg arg0 = TCG_REG_R2;
    const TCGReg arg1 = TCG_REG_R3;
    int s_bits = opc & 3;
    uint16_t *label1_ptr;
    tcg_target_long ofs;

    if (TARGET_LONG_BITS == 32) {
        tgen_ext32u(s, arg0, addr_reg);
    } else {
        tcg_out_mov(s, TCG_TYPE_I64, arg0, addr_reg);
    }

    tcg_out_sh64(s, RSY_SRLG, arg1, addr_reg, TCG_REG_NONE,
                 TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);

    tgen64_andi_tmp(s, arg0, TARGET_PAGE_MASK | ((1 << s_bits) - 1));
    tgen64_andi_tmp(s, arg1, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS);

    if (is_store) {
        ofs = offsetof(CPUState, tlb_table[mem_index][0].addr_write);
    } else {
        ofs = offsetof(CPUState, tlb_table[mem_index][0].addr_read);
    }
    assert(ofs < 0x80000);

    if (TARGET_LONG_BITS == 32) {
        tcg_out_mem(s, RX_C, RXY_CY, arg0, arg1, TCG_AREG0, ofs);
    } else {
        tcg_out_mem(s, 0, RXY_CG, arg0, arg1, TCG_AREG0, ofs);
    }

    if (TARGET_LONG_BITS == 32) {
        tgen_ext32u(s, arg0, addr_reg);
    } else {
        tcg_out_mov(s, TCG_TYPE_I64, arg0, addr_reg);
    }

    label1_ptr = (uint16_t*)s->code_ptr;

    /* je label1 (offset will be patched in later) */
    tcg_out_insn(s, RI, BRC, S390_CC_EQ, 0);

    /* call load/store helper */
    if (is_store) {
        /* Make sure to zero-extend the value to the full register
           for the calling convention.  */
        switch (opc) {
        case LD_UINT8:
            tgen_ext8u(s, TCG_TYPE_I64, arg1, data_reg);
            break;
        case LD_UINT16:
            tgen_ext16u(s, TCG_TYPE_I64, arg1, data_reg);
            break;
        case LD_UINT32:
            tgen_ext32u(s, arg1, data_reg);
            break;
        case LD_UINT64:
            tcg_out_mov(s, TCG_TYPE_I64, arg1, data_reg);
            break;
        default:
            tcg_abort();
        }
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_R4, mem_index);
        tgen_calli(s, (tcg_target_ulong)qemu_st_helpers[s_bits]);
    } else {
        tcg_out_movi(s, TCG_TYPE_I32, arg1, mem_index);
        tgen_calli(s, (tcg_target_ulong)qemu_ld_helpers[s_bits]);

        /* sign extension */
        switch (opc) {
        case LD_INT8:
            tgen_ext8s(s, TCG_TYPE_I64, data_reg, arg0);
            break;
        case LD_INT16:
            tgen_ext16s(s, TCG_TYPE_I64, data_reg, arg0);
            break;
        case LD_INT32:
            tgen_ext32s(s, data_reg, arg0);
            break;
        default:
            /* unsigned -> just copy */
            tcg_out_mov(s, TCG_TYPE_I64, data_reg, arg0);
            break;
        }
    }

    /* jump to label2 (end) */
    *label2_ptr_p = (uint16_t*)s->code_ptr;

    tcg_out_insn(s, RI, BRC, S390_CC_ALWAYS, 0);

    /* this is label1, patch branch */
    *(label1_ptr + 1) = ((unsigned long)s->code_ptr -
                         (unsigned long)label1_ptr) >> 1;

    ofs = offsetof(CPUState, tlb_table[mem_index][0].addend);
    assert(ofs < 0x80000);

    tcg_out_mem(s, 0, RXY_AG, arg0, arg1, TCG_AREG0, ofs);
}

static void tcg_finish_qemu_ldst(TCGContext* s, uint16_t *label2_ptr)
{
    /* patch branch */
    *(label2_ptr + 1) = ((unsigned long)s->code_ptr -
                         (unsigned long)label2_ptr) >> 1;
}
#else
static void tcg_prepare_user_ldst(TCGContext *s, TCGReg *addr_reg,
                                  TCGReg *index_reg, tcg_target_long *disp)
{
    if (TARGET_LONG_BITS == 32) {
        tgen_ext32u(s, TCG_TMP0, *addr_reg);
        *addr_reg = TCG_TMP0;
    }
    if (GUEST_BASE < 0x80000) {
        *index_reg = TCG_REG_NONE;
        *disp = GUEST_BASE;
    } else {
        *index_reg = TCG_GUEST_BASE_REG;
        *disp = 0;
    }
}
#endif /* CONFIG_SOFTMMU */

/* load data with address translation (if applicable)
   and endianness conversion */
static void tcg_out_qemu_ld(TCGContext* s, const TCGArg* args, int opc)
{
    TCGReg addr_reg, data_reg;
#if defined(CONFIG_SOFTMMU)
    int mem_index;
    uint16_t *label2_ptr;
#else
    TCGReg index_reg;
    tcg_target_long disp;
#endif

    data_reg = *args++;
    addr_reg = *args++;

#if defined(CONFIG_SOFTMMU)
    mem_index = *args;

    tcg_prepare_qemu_ldst(s, data_reg, addr_reg, mem_index,
                          opc, &label2_ptr, 0);

    tcg_out_qemu_ld_direct(s, opc, data_reg, TCG_REG_R2, TCG_REG_NONE, 0);

    tcg_finish_qemu_ldst(s, label2_ptr);
#else
    tcg_prepare_user_ldst(s, &addr_reg, &index_reg, &disp);
    tcg_out_qemu_ld_direct(s, opc, data_reg, addr_reg, index_reg, disp);
#endif
}

static void tcg_out_qemu_st(TCGContext* s, const TCGArg* args, int opc)
{
    TCGReg addr_reg, data_reg;
#if defined(CONFIG_SOFTMMU)
    int mem_index;
    uint16_t *label2_ptr;
#else
    TCGReg index_reg;
    tcg_target_long disp;
#endif

    data_reg = *args++;
    addr_reg = *args++;

#if defined(CONFIG_SOFTMMU)
    mem_index = *args;

    tcg_prepare_qemu_ldst(s, data_reg, addr_reg, mem_index,
                          opc, &label2_ptr, 1);

    tcg_out_qemu_st_direct(s, opc, data_reg, TCG_REG_R2, TCG_REG_NONE, 0);

    tcg_finish_qemu_ldst(s, label2_ptr);
#else
    tcg_prepare_user_ldst(s, &addr_reg, &index_reg, &disp);
    tcg_out_qemu_st_direct(s, opc, data_reg, addr_reg, index_reg, disp);
#endif
}

#if TCG_TARGET_REG_BITS == 64
# define OP_32_64(x) \
        case glue(glue(INDEX_op_,x),_i32): \
        case glue(glue(INDEX_op_,x),_i64)
#else
# define OP_32_64(x) \
        case glue(glue(INDEX_op_,x),_i32)
#endif

static inline void tcg_out_op(TCGContext *s, TCGOpcode opc,
                const TCGArg *args, const int *const_args)
{
    S390Opcode op;

    switch (opc) {
    case INDEX_op_exit_tb:
        /* return value */
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R2, args[0]);
        tgen_gotoi(s, S390_CC_ALWAYS, (unsigned long)tb_ret_addr);
        break;

    case INDEX_op_goto_tb:
        if (s->tb_jmp_offset) {
            tcg_abort();
        } else {
            /* load address stored at s->tb_next + args[0] */
            tcg_out_ld_abs(s, TCG_TYPE_PTR, TCG_TMP0, s->tb_next + args[0]);
            /* and go there */
            tcg_out_insn(s, RR, BCR, S390_CC_ALWAYS, TCG_TMP0);
        }
        s->tb_next_offset[args[0]] = s->code_ptr - s->code_buf;
        break;

    case INDEX_op_call:
        if (const_args[0]) {
            tgen_calli(s, args[0]);
        } else {
            tcg_out_insn(s, RR, BASR, TCG_REG_R14, args[0]);
        }
        break;

    case INDEX_op_mov_i32:
        tcg_out_mov(s, TCG_TYPE_I32, args[0], args[1]);
        break;
    case INDEX_op_movi_i32:
        tcg_out_movi(s, TCG_TYPE_I32, args[0], args[1]);
        break;

    OP_32_64(ld8u):
        /* ??? LLC (RXY format) is only present with the extended-immediate
           facility, whereas LLGC is always present.  */
        tcg_out_mem(s, 0, RXY_LLGC, args[0], args[1], TCG_REG_NONE, args[2]);
        break;

    OP_32_64(ld8s):
        /* ??? LB is no smaller than LGB, so no point to using it.  */
        tcg_out_mem(s, 0, RXY_LGB, args[0], args[1], TCG_REG_NONE, args[2]);
        break;

    OP_32_64(ld16u):
        /* ??? LLH (RXY format) is only present with the extended-immediate
           facility, whereas LLGH is always present.  */
        tcg_out_mem(s, 0, RXY_LLGH, args[0], args[1], TCG_REG_NONE, args[2]);
        break;

    case INDEX_op_ld16s_i32:
        tcg_out_mem(s, RX_LH, RXY_LHY, args[0], args[1], TCG_REG_NONE, args[2]);
        break;

    case INDEX_op_ld_i32:
        tcg_out_ld(s, TCG_TYPE_I32, args[0], args[1], args[2]);
        break;

    OP_32_64(st8):
        tcg_out_mem(s, RX_STC, RXY_STCY, args[0], args[1],
                    TCG_REG_NONE, args[2]);
        break;

    OP_32_64(st16):
        tcg_out_mem(s, RX_STH, RXY_STHY, args[0], args[1],
                    TCG_REG_NONE, args[2]);
        break;

    case INDEX_op_st_i32:
        tcg_out_st(s, TCG_TYPE_I32, args[0], args[1], args[2]);
        break;

    case INDEX_op_add_i32:
        if (const_args[2]) {
            tgen32_addi(s, args[0], args[2]);
        } else {
            tcg_out_insn(s, RR, AR, args[0], args[2]);
        }
        break;
    case INDEX_op_sub_i32:
        if (const_args[2]) {
            tgen32_addi(s, args[0], -args[2]);
        } else {
            tcg_out_insn(s, RR, SR, args[0], args[2]);
        }
        break;

    case INDEX_op_and_i32:
        if (const_args[2]) {
            tgen64_andi(s, args[0], args[2] | 0xffffffff00000000ull);
        } else {
            tcg_out_insn(s, RR, NR, args[0], args[2]);
        }
        break;
    case INDEX_op_or_i32:
        if (const_args[2]) {
            tgen64_ori(s, args[0], args[2] & 0xffffffff);
        } else {
            tcg_out_insn(s, RR, OR, args[0], args[2]);
        }
        break;
    case INDEX_op_xor_i32:
        if (const_args[2]) {
            tgen64_xori(s, args[0], args[2] & 0xffffffff);
        } else {
            tcg_out_insn(s, RR, XR, args[0], args[2]);
        }
        break;

    case INDEX_op_neg_i32:
        tcg_out_insn(s, RR, LCR, args[0], args[1]);
        break;

    case INDEX_op_mul_i32:
        if (const_args[2]) {
            if ((int32_t)args[2] == (int16_t)args[2]) {
                tcg_out_insn(s, RI, MHI, args[0], args[2]);
            } else {
                tcg_out_insn(s, RIL, MSFI, args[0], args[2]);
            }
        } else {
            tcg_out_insn(s, RRE, MSR, args[0], args[2]);
        }
        break;

    case INDEX_op_div2_i32:
        tcg_out_insn(s, RR, DR, TCG_REG_R2, args[4]);
        break;
    case INDEX_op_divu2_i32:
        tcg_out_insn(s, RRE, DLR, TCG_REG_R2, args[4]);
        break;

    case INDEX_op_shl_i32:
        op = RS_SLL;
    do_shift32:
        if (const_args[2]) {
            tcg_out_sh32(s, op, args[0], TCG_REG_NONE, args[2]);
        } else {
            tcg_out_sh32(s, op, args[0], args[2], 0);
        }
        break;
    case INDEX_op_shr_i32:
        op = RS_SRL;
        goto do_shift32;
    case INDEX_op_sar_i32:
        op = RS_SRA;
        goto do_shift32;

    case INDEX_op_rotl_i32:
        /* ??? Using tcg_out_sh64 here for the format; it is a 32-bit rol.  */
        if (const_args[2]) {
            tcg_out_sh64(s, RSY_RLL, args[0], args[1], TCG_REG_NONE, args[2]);
        } else {
            tcg_out_sh64(s, RSY_RLL, args[0], args[1], args[2], 0);
        }
        break;
    case INDEX_op_rotr_i32:
        if (const_args[2]) {
            tcg_out_sh64(s, RSY_RLL, args[0], args[1],
                         TCG_REG_NONE, (32 - args[2]) & 31);
        } else {
            tcg_out_insn(s, RR, LCR, TCG_TMP0, args[2]);
            tcg_out_sh64(s, RSY_RLL, args[0], args[1], TCG_TMP0, 0);
        }
        break;

    case INDEX_op_ext8s_i32:
        tgen_ext8s(s, TCG_TYPE_I32, args[0], args[1]);
        break;
    case INDEX_op_ext16s_i32:
        tgen_ext16s(s, TCG_TYPE_I32, args[0], args[1]);
        break;
    case INDEX_op_ext8u_i32:
        tgen_ext8u(s, TCG_TYPE_I32, args[0], args[1]);
        break;
    case INDEX_op_ext16u_i32:
        tgen_ext16u(s, TCG_TYPE_I32, args[0], args[1]);
        break;

    OP_32_64(bswap16):
        /* The TCG bswap definition requires bits 0-47 already be zero.
           Thus we don't need the G-type insns to implement bswap16_i64.  */
        tcg_out_insn(s, RRE, LRVR, args[0], args[1]);
        tcg_out_sh32(s, RS_SRL, args[0], TCG_REG_NONE, 16);
        break;
    OP_32_64(bswap32):
        tcg_out_insn(s, RRE, LRVR, args[0], args[1]);
        break;

    case INDEX_op_br:
        tgen_branch(s, S390_CC_ALWAYS, args[0]);
        break;

    case INDEX_op_brcond_i32:
        tgen_brcond(s, TCG_TYPE_I32, args[2], args[0],
                    args[1], const_args[1], args[3]);
        break;
    case INDEX_op_setcond_i32:
        tgen_setcond(s, TCG_TYPE_I32, args[3], args[0], args[1],
                     args[2], const_args[2]);
        break;

    case INDEX_op_qemu_ld8u:
        tcg_out_qemu_ld(s, args, LD_UINT8);
        break;
    case INDEX_op_qemu_ld8s:
        tcg_out_qemu_ld(s, args, LD_INT8);
        break;
    case INDEX_op_qemu_ld16u:
        tcg_out_qemu_ld(s, args, LD_UINT16);
        break;
    case INDEX_op_qemu_ld16s:
        tcg_out_qemu_ld(s, args, LD_INT16);
        break;
    case INDEX_op_qemu_ld32:
        /* ??? Technically we can use a non-extending instruction.  */
        tcg_out_qemu_ld(s, args, LD_UINT32);
        break;
    case INDEX_op_qemu_ld64:
        tcg_out_qemu_ld(s, args, LD_UINT64);
        break;

    case INDEX_op_qemu_st8:
        tcg_out_qemu_st(s, args, LD_UINT8);
        break;
    case INDEX_op_qemu_st16:
        tcg_out_qemu_st(s, args, LD_UINT16);
        break;
    case INDEX_op_qemu_st32:
        tcg_out_qemu_st(s, args, LD_UINT32);
        break;
    case INDEX_op_qemu_st64:
        tcg_out_qemu_st(s, args, LD_UINT64);
        break;

#if TCG_TARGET_REG_BITS == 64
    case INDEX_op_mov_i64:
        tcg_out_mov(s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_movi_i64:
        tcg_out_movi(s, TCG_TYPE_I64, args[0], args[1]);
        break;

    case INDEX_op_ld16s_i64:
        tcg_out_mem(s, 0, RXY_LGH, args[0], args[1], TCG_REG_NONE, args[2]);
        break;
    case INDEX_op_ld32u_i64:
        tcg_out_mem(s, 0, RXY_LLGF, args[0], args[1], TCG_REG_NONE, args[2]);
        break;
    case INDEX_op_ld32s_i64:
        tcg_out_mem(s, 0, RXY_LGF, args[0], args[1], TCG_REG_NONE, args[2]);
        break;
    case INDEX_op_ld_i64:
        tcg_out_ld(s, TCG_TYPE_I64, args[0], args[1], args[2]);
        break;

    case INDEX_op_st32_i64:
        tcg_out_st(s, TCG_TYPE_I32, args[0], args[1], args[2]);
        break;
    case INDEX_op_st_i64:
        tcg_out_st(s, TCG_TYPE_I64, args[0], args[1], args[2]);
        break;

    case INDEX_op_add_i64:
        if (const_args[2]) {
            tgen64_addi(s, args[0], args[2]);
        } else {
            tcg_out_insn(s, RRE, AGR, args[0], args[2]);
        }
        break;
    case INDEX_op_sub_i64:
        if (const_args[2]) {
            tgen64_addi(s, args[0], -args[2]);
        } else {
            tcg_out_insn(s, RRE, SGR, args[0], args[2]);
        }
        break;

    case INDEX_op_and_i64:
        if (const_args[2]) {
            tgen64_andi(s, args[0], args[2]);
        } else {
            tcg_out_insn(s, RRE, NGR, args[0], args[2]);
        }
        break;
    case INDEX_op_or_i64:
        if (const_args[2]) {
            tgen64_ori(s, args[0], args[2]);
        } else {
            tcg_out_insn(s, RRE, OGR, args[0], args[2]);
        }
        break;
    case INDEX_op_xor_i64:
        if (const_args[2]) {
            tgen64_xori(s, args[0], args[2]);
        } else {
            tcg_out_insn(s, RRE, XGR, args[0], args[2]);
        }
        break;

    case INDEX_op_neg_i64:
        tcg_out_insn(s, RRE, LCGR, args[0], args[1]);
        break;
    case INDEX_op_bswap64_i64:
        tcg_out_insn(s, RRE, LRVGR, args[0], args[1]);
        break;

    case INDEX_op_mul_i64:
        if (const_args[2]) {
            if (args[2] == (int16_t)args[2]) {
                tcg_out_insn(s, RI, MGHI, args[0], args[2]);
            } else {
                tcg_out_insn(s, RIL, MSGFI, args[0], args[2]);
            }
        } else {
            tcg_out_insn(s, RRE, MSGR, args[0], args[2]);
        }
        break;

    case INDEX_op_div2_i64:
        /* ??? We get an unnecessary sign-extension of the dividend
           into R3 with this definition, but as we do in fact always
           produce both quotient and remainder using INDEX_op_div_i64
           instead requires jumping through even more hoops.  */
        tcg_out_insn(s, RRE, DSGR, TCG_REG_R2, args[4]);
        break;
    case INDEX_op_divu2_i64:
        tcg_out_insn(s, RRE, DLGR, TCG_REG_R2, args[4]);
        break;

    case INDEX_op_shl_i64:
        op = RSY_SLLG;
    do_shift64:
        if (const_args[2]) {
            tcg_out_sh64(s, op, args[0], args[1], TCG_REG_NONE, args[2]);
        } else {
            tcg_out_sh64(s, op, args[0], args[1], args[2], 0);
        }
        break;
    case INDEX_op_shr_i64:
        op = RSY_SRLG;
        goto do_shift64;
    case INDEX_op_sar_i64:
        op = RSY_SRAG;
        goto do_shift64;

    case INDEX_op_rotl_i64:
        if (const_args[2]) {
            tcg_out_sh64(s, RSY_RLLG, args[0], args[1],
                         TCG_REG_NONE, args[2]);
        } else {
            tcg_out_sh64(s, RSY_RLLG, args[0], args[1], args[2], 0);
        }
        break;
    case INDEX_op_rotr_i64:
        if (const_args[2]) {
            tcg_out_sh64(s, RSY_RLLG, args[0], args[1],
                         TCG_REG_NONE, (64 - args[2]) & 63);
        } else {
            /* We can use the smaller 32-bit negate because only the
               low 6 bits are examined for the rotate.  */
            tcg_out_insn(s, RR, LCR, TCG_TMP0, args[2]);
            tcg_out_sh64(s, RSY_RLLG, args[0], args[1], TCG_TMP0, 0);
        }
        break;

    case INDEX_op_ext8s_i64:
        tgen_ext8s(s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_ext16s_i64:
        tgen_ext16s(s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_ext32s_i64:
        tgen_ext32s(s, args[0], args[1]);
        break;
    case INDEX_op_ext8u_i64:
        tgen_ext8u(s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_ext16u_i64:
        tgen_ext16u(s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_ext32u_i64:
        tgen_ext32u(s, args[0], args[1]);
        break;

    case INDEX_op_brcond_i64:
        tgen_brcond(s, TCG_TYPE_I64, args[2], args[0],
                    args[1], const_args[1], args[3]);
        break;
    case INDEX_op_setcond_i64:
        tgen_setcond(s, TCG_TYPE_I64, args[3], args[0], args[1],
                     args[2], const_args[2]);
        break;

    case INDEX_op_qemu_ld32u:
        tcg_out_qemu_ld(s, args, LD_UINT32);
        break;
    case INDEX_op_qemu_ld32s:
        tcg_out_qemu_ld(s, args, LD_INT32);
        break;
#endif /* TCG_TARGET_REG_BITS == 64 */

    case INDEX_op_jmp:
        /* This one is obsolete and never emitted.  */
        tcg_abort();
        break;

    default:
        fprintf(stderr,"unimplemented opc 0x%x\n",opc);
        tcg_abort();
    }
}

static const TCGTargetOpDef s390_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },
    { INDEX_op_call, { "ri" } },
    { INDEX_op_jmp, { "ri" } },
    { INDEX_op_br, { } },

    { INDEX_op_mov_i32, { "r", "r" } },
    { INDEX_op_movi_i32, { "r" } },

    { INDEX_op_ld8u_i32, { "r", "r" } },
    { INDEX_op_ld8s_i32, { "r", "r" } },
    { INDEX_op_ld16u_i32, { "r", "r" } },
    { INDEX_op_ld16s_i32, { "r", "r" } },
    { INDEX_op_ld_i32, { "r", "r" } },
    { INDEX_op_st8_i32, { "r", "r" } },
    { INDEX_op_st16_i32, { "r", "r" } },
    { INDEX_op_st_i32, { "r", "r" } },

    { INDEX_op_add_i32, { "r", "0", "rWI" } },
    { INDEX_op_sub_i32, { "r", "0", "rWNI" } },
    { INDEX_op_mul_i32, { "r", "0", "rK" } },

    { INDEX_op_div2_i32, { "b", "a", "0", "1", "r" } },
    { INDEX_op_divu2_i32, { "b", "a", "0", "1", "r" } },

    { INDEX_op_and_i32, { "r", "0", "rWA" } },
    { INDEX_op_or_i32, { "r", "0", "rWO" } },
    { INDEX_op_xor_i32, { "r", "0", "rWX" } },

    { INDEX_op_neg_i32, { "r", "r" } },

    { INDEX_op_shl_i32, { "r", "0", "Ri" } },
    { INDEX_op_shr_i32, { "r", "0", "Ri" } },
    { INDEX_op_sar_i32, { "r", "0", "Ri" } },

    { INDEX_op_rotl_i32, { "r", "r", "Ri" } },
    { INDEX_op_rotr_i32, { "r", "r", "Ri" } },

    { INDEX_op_ext8s_i32, { "r", "r" } },
    { INDEX_op_ext8u_i32, { "r", "r" } },
    { INDEX_op_ext16s_i32, { "r", "r" } },
    { INDEX_op_ext16u_i32, { "r", "r" } },

    { INDEX_op_bswap16_i32, { "r", "r" } },
    { INDEX_op_bswap32_i32, { "r", "r" } },

    { INDEX_op_brcond_i32, { "r", "rWC" } },
    { INDEX_op_setcond_i32, { "r", "r", "rWC" } },

    { INDEX_op_qemu_ld8u, { "r", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L" } },
    { INDEX_op_qemu_ld32, { "r", "L" } },
    { INDEX_op_qemu_ld64, { "r", "L" } },

    { INDEX_op_qemu_st8, { "L", "L" } },
    { INDEX_op_qemu_st16, { "L", "L" } },
    { INDEX_op_qemu_st32, { "L", "L" } },
    { INDEX_op_qemu_st64, { "L", "L" } },

#if defined(__s390x__)
    { INDEX_op_mov_i64, { "r", "r" } },
    { INDEX_op_movi_i64, { "r" } },

    { INDEX_op_ld8u_i64, { "r", "r" } },
    { INDEX_op_ld8s_i64, { "r", "r" } },
    { INDEX_op_ld16u_i64, { "r", "r" } },
    { INDEX_op_ld16s_i64, { "r", "r" } },
    { INDEX_op_ld32u_i64, { "r", "r" } },
    { INDEX_op_ld32s_i64, { "r", "r" } },
    { INDEX_op_ld_i64, { "r", "r" } },

    { INDEX_op_st8_i64, { "r", "r" } },
    { INDEX_op_st16_i64, { "r", "r" } },
    { INDEX_op_st32_i64, { "r", "r" } },
    { INDEX_op_st_i64, { "r", "r" } },

    { INDEX_op_add_i64, { "r", "0", "rI" } },
    { INDEX_op_sub_i64, { "r", "0", "rNI" } },
    { INDEX_op_mul_i64, { "r", "0", "rK" } },

    { INDEX_op_div2_i64, { "b", "a", "0", "1", "r" } },
    { INDEX_op_divu2_i64, { "b", "a", "0", "1", "r" } },

    { INDEX_op_and_i64, { "r", "0", "rA" } },
    { INDEX_op_or_i64, { "r", "0", "rO" } },
    { INDEX_op_xor_i64, { "r", "0", "rX" } },

    { INDEX_op_neg_i64, { "r", "r" } },

    { INDEX_op_shl_i64, { "r", "r", "Ri" } },
    { INDEX_op_shr_i64, { "r", "r", "Ri" } },
    { INDEX_op_sar_i64, { "r", "r", "Ri" } },

    { INDEX_op_rotl_i64, { "r", "r", "Ri" } },
    { INDEX_op_rotr_i64, { "r", "r", "Ri" } },

    { INDEX_op_ext8s_i64, { "r", "r" } },
    { INDEX_op_ext8u_i64, { "r", "r" } },
    { INDEX_op_ext16s_i64, { "r", "r" } },
    { INDEX_op_ext16u_i64, { "r", "r" } },
    { INDEX_op_ext32s_i64, { "r", "r" } },
    { INDEX_op_ext32u_i64, { "r", "r" } },

    { INDEX_op_bswap16_i64, { "r", "r" } },
    { INDEX_op_bswap32_i64, { "r", "r" } },
    { INDEX_op_bswap64_i64, { "r", "r" } },

    { INDEX_op_brcond_i64, { "r", "rC" } },
    { INDEX_op_setcond_i64, { "r", "r", "rC" } },

    { INDEX_op_qemu_ld32u, { "r", "L" } },
    { INDEX_op_qemu_ld32s, { "r", "L" } },
#endif

    { -1 },
};

/* ??? Linux kernels provide an AUXV entry AT_HWCAP that provides most of
   this information.  However, getting at that entry is not easy this far
   away from main.  Our options are: start searching from environ, but
   that fails as soon as someone does a setenv in between.  Read the data
   from /proc/self/auxv.  Or do the probing ourselves.  The only thing
   extra that AT_HWCAP gives us is HWCAP_S390_HIGH_GPRS, which indicates
   that the kernel saves all 64-bits of the registers around traps while
   in 31-bit mode.  But this is true of all "recent" kernels (ought to dig
   back and see from when this might not be true).  */

#include <signal.h>

static volatile sig_atomic_t got_sigill;

static void sigill_handler(int sig)
{
    got_sigill = 1;
}

static void query_facilities(void)
{
    struct sigaction sa_old, sa_new;
    register int r0 __asm__("0");
    register void *r1 __asm__("1");
    int fail;

    memset(&sa_new, 0, sizeof(sa_new));
    sa_new.sa_handler = sigill_handler;
    sigaction(SIGILL, &sa_new, &sa_old);

    /* First, try STORE FACILITY LIST EXTENDED.  If this is present, then
       we need not do any more probing.  Unfortunately, this itself is an
       extension and the original STORE FACILITY LIST instruction is
       kernel-only, storing its results at absolute address 200.  */
    /* stfle 0(%r1) */
    r1 = &facilities;
    asm volatile(".word 0xb2b0,0x1000"
                 : "=r"(r0) : "0"(0), "r"(r1) : "memory", "cc");

    if (got_sigill) {
        /* STORE FACILITY EXTENDED is not available.  Probe for one of each
           kind of instruction that we're interested in.  */
        /* ??? Possibly some of these are in practice never present unless
           the store-facility-extended facility is also present.  But since
           that isn't documented it's just better to probe for each.  */

        /* Test for z/Architecture.  Required even in 31-bit mode.  */
        got_sigill = 0;
        /* agr %r0,%r0 */
        asm volatile(".word 0xb908,0x0000" : "=r"(r0) : : "cc");
        if (!got_sigill) {
            facilities |= FACILITY_ZARCH_ACTIVE;
        }

        /* Test for long displacement.  */
        got_sigill = 0;
        /* ly %r0,0(%r1) */
        r1 = &facilities;
        asm volatile(".word 0xe300,0x1000,0x0058"
                     : "=r"(r0) : "r"(r1) : "cc");
        if (!got_sigill) {
            facilities |= FACILITY_LONG_DISP;
        }

        /* Test for extended immediates.  */
        got_sigill = 0;
        /* afi %r0,0 */
        asm volatile(".word 0xc209,0x0000,0x0000" : : : "cc");
        if (!got_sigill) {
            facilities |= FACILITY_EXT_IMM;
        }

        /* Test for general-instructions-extension.  */
        got_sigill = 0;
        /* msfi %r0,1 */
        asm volatile(".word 0xc201,0x0000,0x0001");
        if (!got_sigill) {
            facilities |= FACILITY_GEN_INST_EXT;
        }
    }

    sigaction(SIGILL, &sa_old, NULL);

    /* The translator currently uses these extensions unconditionally.
       Pruning this back to the base ESA/390 architecture doesn't seem
       worthwhile, since even the KVM target requires z/Arch.  */
    fail = 0;
    if ((facilities & FACILITY_ZARCH_ACTIVE) == 0) {
        fprintf(stderr, "TCG: z/Arch facility is required.\n");
        fprintf(stderr, "TCG: Boot with a 64-bit enabled kernel.\n");
        fail = 1;
    }
    if ((facilities & FACILITY_LONG_DISP) == 0) {
        fprintf(stderr, "TCG: long-displacement facility is required.\n");
        fail = 1;
    }

    /* So far there's just enough support for 31-bit mode to let the
       compile succeed.  This is good enough to run QEMU with KVM.  */
    if (sizeof(void *) != 8) {
        fprintf(stderr, "TCG: 31-bit mode is not supported.\n");
        fail = 1;
    }

    if (fail) {
        exit(-1);
    }
}

static void tcg_target_init(TCGContext *s)
{
#if !defined(CONFIG_USER_ONLY)
    /* fail safe */
    if ((1 << CPU_TLB_ENTRY_BITS) != sizeof(CPUTLBEntry)) {
        tcg_abort();
    }
#endif

    query_facilities();

    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffff);
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I64], 0, 0xffff);

    tcg_regset_clear(tcg_target_call_clobber_regs);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R0);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R1);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R2);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R3);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R4);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R5);
    /* The return register can be considered call-clobbered.  */
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R14);

    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_TMP0);
    /* XXX many insns can't be used with R0, so we better avoid it for now */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R0);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_CALL_STACK);

    tcg_add_target_add_op_defs(s390_op_defs);
    tcg_set_frame(s, TCG_AREG0, offsetof(CPUState, temp_buf),
                  CPU_TEMP_BUF_NLONGS * sizeof(long));
}

static void tcg_target_qemu_prologue(TCGContext *s)
{
    /* stmg %r6,%r15,48(%r15) (save registers) */
    tcg_out_insn(s, RXY, STMG, TCG_REG_R6, TCG_REG_R15, TCG_REG_R15, 48);

    /* aghi %r15,-160 (stack frame) */
    tcg_out_insn(s, RI, AGHI, TCG_REG_R15, -160);

    if (GUEST_BASE >= 0x80000) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_GUEST_BASE_REG, GUEST_BASE);
        tcg_regset_set_reg(s->reserved_regs, TCG_GUEST_BASE_REG);
    }

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);
    /* br %r3 (go to TB) */
    tcg_out_insn(s, RR, BCR, S390_CC_ALWAYS, tcg_target_call_iarg_regs[1]);

    tb_ret_addr = s->code_ptr;

    /* lmg %r6,%r15,208(%r15) (restore registers) */
    tcg_out_insn(s, RXY, LMG, TCG_REG_R6, TCG_REG_R15, TCG_REG_R15, 208);

    /* br %r14 (return) */
    tcg_out_insn(s, RR, BCR, S390_CC_ALWAYS, TCG_REG_R14);
}
