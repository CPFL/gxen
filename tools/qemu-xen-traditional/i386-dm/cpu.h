/*
 * i386 virtual CPU header
 * 
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef CPU_I386_H
#define CPU_I386_H

#include "config.h"

struct CPUX86State;

#ifdef TARGET_X86_64
#define TARGET_LONG_BITS 64
#else
/* #define TARGET_LONG_BITS 32 */
#define TARGET_LONG_BITS 64 /* for Qemu map cache */
#endif

/* target supports implicit self modifying code */
#define TARGET_HAS_SMC
/* support for self modifying code even if the modified instruction is
   close to the modifying instruction */
#define TARGET_HAS_PRECISE_SMC

/* MMU modes definitions
 */
/* We aren't handling the MMU in Qemu; all the addresses we deal with
 * are guest physical.  So as far as qemu
 * is concerned there is only the one MMU mode.
 */
#define NB_MMU_MODES 1
#define MMU_MODE0_SUFFIX _xen
static inline int cpu_mmu_index(struct CPUX86State *env) { return 0; }

#include "cpu-defs.h"

#ifdef CONFIG_SOFTFLOAT
#include "softfloat.h"
#endif

#if defined(__i386__) && !defined(CONFIG_SOFTMMU)
#define USE_CODE_COPY
#endif

#ifdef CONFIG_SOFTFLOAT
#ifdef USE_X86LDOUBLE
typedef floatx80 CPU86_LDouble;
#else
typedef float64 CPU86_LDouble;
#endif
#endif

typedef struct SegmentCache {
    uint32_t selector;
    target_ulong base;
    uint32_t limit;
    uint32_t flags;
} SegmentCache;

/* Empty for now */
typedef struct CPUX86State {
    uint32_t a20_mask;

    CPU_COMMON
} CPUX86State;

CPUX86State *cpu_x86_init(const char *cpu_model);
int cpu_x86_exec(CPUX86State *s);
void cpu_x86_close(CPUX86State *s);
int cpu_get_pic_interrupt(CPUX86State *s);
/* MSDOS compatibility mode FPU exception support */
void cpu_set_ferr(CPUX86State *s);

void cpu_x86_set_a20(CPUX86State *env, int a20_state);
uint64_t cpu_get_tsc(CPUX86State *env);

/* used to debug */
#define X86_DUMP_FPU  0x0001 /* dump FPU state too */
#define X86_DUMP_CCOP 0x0002 /* dump qemu flag cache */

#ifndef IN_OP_I386
void cpu_x86_outb(CPUX86State *env, int addr, int val);
void cpu_x86_outw(CPUX86State *env, int addr, int val);
void cpu_x86_outl(CPUX86State *env, int addr, int val);
int cpu_x86_inb(CPUX86State *env, int addr);
int cpu_x86_inw(CPUX86State *env, int addr);
int cpu_x86_inl(CPUX86State *env, int addr);
#endif

/* helper2.c */
int main_loop(void);

#if defined(__i386__) || defined(__x86_64__)
#define TARGET_PAGE_BITS 12
#elif defined(__ia64__)
#define TARGET_PAGE_BITS 14
#endif 

#define CPUState CPUX86State
#define cpu_init cpu_x86_init
#define cpu_exec cpu_x86_exec
#define cpu_gen_code cpu_x86_gen_code
#define cpu_signal_handler cpu_x86_signal_handler

#include "cpu-all.h"

#endif /* CPU_I386_H */
