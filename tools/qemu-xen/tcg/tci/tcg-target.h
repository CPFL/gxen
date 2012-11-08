/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2009, 2011 Stefan Weil
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

/*
 * This code implements a TCG which does not generate machine code for some
 * real target machine but which generates virtual machine code for an
 * interpreter. Interpreted pseudo code is slow, but it works on any host.
 *
 * Some remarks might help in understanding the code:
 *
 * "target" or "TCG target" is the machine which runs the generated code.
 * This is different to the usual meaning in QEMU where "target" is the
 * emulated machine. So normally QEMU host is identical to TCG target.
 * Here the TCG target is a virtual machine, but this virtual machine must
 * use the same word size like the real machine.
 * Therefore, we need both 32 and 64 bit virtual machines (interpreter).
 */

#if !defined(TCG_TARGET_H)
#define TCG_TARGET_H

#include "config-host.h"

#define TCG_TARGET_INTERPRETER 1

#ifdef CONFIG_DEBUG_TCG
/* Enable debug output. */
#define CONFIG_DEBUG_TCG_INTERPRETER
#endif

#if 0 /* TCI tries to emulate a little endian host. */
#if defined(HOST_WORDS_BIGENDIAN)
# define TCG_TARGET_WORDS_BIGENDIAN
#endif
#endif

/* Optional instructions. */

#define TCG_TARGET_HAS_bswap16_i32      1
#define TCG_TARGET_HAS_bswap32_i32      1
/* Not more than one of the next two defines must be 1. */
#define TCG_TARGET_HAS_div_i32          1
#define TCG_TARGET_HAS_div2_i32         0
#define TCG_TARGET_HAS_ext8s_i32        1
#define TCG_TARGET_HAS_ext16s_i32       1
#define TCG_TARGET_HAS_ext8u_i32        1
#define TCG_TARGET_HAS_ext16u_i32       1
#define TCG_TARGET_HAS_andc_i32         0
#define TCG_TARGET_HAS_deposit_i32      0
#define TCG_TARGET_HAS_eqv_i32          0
#define TCG_TARGET_HAS_nand_i32         0
#define TCG_TARGET_HAS_nor_i32          0
#define TCG_TARGET_HAS_neg_i32          1
#define TCG_TARGET_HAS_not_i32          1
#define TCG_TARGET_HAS_orc_i32          0
#define TCG_TARGET_HAS_rot_i32          1

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_HAS_bswap16_i64      1
#define TCG_TARGET_HAS_bswap32_i64      1
#define TCG_TARGET_HAS_bswap64_i64      1
#define TCG_TARGET_HAS_deposit_i64      0
/* Not more than one of the next two defines must be 1. */
#define TCG_TARGET_HAS_div_i64          0
#define TCG_TARGET_HAS_div2_i64         0
#define TCG_TARGET_HAS_ext8s_i64        1
#define TCG_TARGET_HAS_ext16s_i64       1
#define TCG_TARGET_HAS_ext32s_i64       1
#define TCG_TARGET_HAS_ext8u_i64        1
#define TCG_TARGET_HAS_ext16u_i64       1
#define TCG_TARGET_HAS_ext32u_i64       1
#define TCG_TARGET_HAS_andc_i64         0
#define TCG_TARGET_HAS_eqv_i64          0
#define TCG_TARGET_HAS_nand_i64         0
#define TCG_TARGET_HAS_nor_i64          0
#define TCG_TARGET_HAS_neg_i64          1
#define TCG_TARGET_HAS_not_i64          1
#define TCG_TARGET_HAS_orc_i64          0
#define TCG_TARGET_HAS_rot_i64          1
#endif /* TCG_TARGET_REG_BITS == 64 */

/* Offset to user memory in user mode. */
#define TCG_TARGET_HAS_GUEST_BASE

/* Number of registers available.
   For 32 bit hosts, we need more than 8 registers (call arguments). */
/* #define TCG_TARGET_NB_REGS 8 */
#define TCG_TARGET_NB_REGS 16
/* #define TCG_TARGET_NB_REGS 32 */

/* List of registers which are used by TCG. */
typedef enum {
    TCG_REG_R0 = 0,
    TCG_REG_R1,
    TCG_REG_R2,
    TCG_REG_R3,
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
    TCG_AREG0 = TCG_REG_R7,
#if TCG_TARGET_NB_REGS >= 16
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11,
    TCG_REG_R12,
    TCG_REG_R13,
    TCG_REG_R14,
    TCG_REG_R15,
#if TCG_TARGET_NB_REGS >= 32
    TCG_REG_R16,
    TCG_REG_R17,
    TCG_REG_R18,
    TCG_REG_R19,
    TCG_REG_R20,
    TCG_REG_R21,
    TCG_REG_R22,
    TCG_REG_R23,
    TCG_REG_R24,
    TCG_REG_R25,
    TCG_REG_R26,
    TCG_REG_R27,
    TCG_REG_R28,
    TCG_REG_R29,
    TCG_REG_R30,
    TCG_REG_R31,
#endif
#endif
    /* Special value UINT8_MAX is used by TCI to encode constant values. */
    TCG_CONST = UINT8_MAX
} TCGReg;

void tci_disas(uint8_t opc);

unsigned long tcg_qemu_tb_exec(CPUState *env, uint8_t *tb_ptr);
#define tcg_qemu_tb_exec tcg_qemu_tb_exec

static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
}

#endif /* TCG_TARGET_H */
