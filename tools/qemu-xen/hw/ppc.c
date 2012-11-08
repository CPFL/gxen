/*
 * QEMU generic PowerPC hardware System Emulator
 *
 * Copyright (c) 2003-2007 Jocelyn Mayer
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
#include "hw.h"
#include "ppc.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "nvram.h"
#include "qemu-log.h"
#include "loader.h"
#include "kvm.h"
#include "kvm_ppc.h"

//#define PPC_DEBUG_IRQ
//#define PPC_DEBUG_TB

#ifdef PPC_DEBUG_IRQ
#  define LOG_IRQ(...) qemu_log_mask(CPU_LOG_INT, ## __VA_ARGS__)
#else
#  define LOG_IRQ(...) do { } while (0)
#endif


#ifdef PPC_DEBUG_TB
#  define LOG_TB(...) qemu_log(__VA_ARGS__)
#else
#  define LOG_TB(...) do { } while (0)
#endif

static void cpu_ppc_tb_stop (CPUState *env);
static void cpu_ppc_tb_start (CPUState *env);

void ppc_set_irq(CPUState *env, int n_IRQ, int level)
{
    unsigned int old_pending = env->pending_interrupts;

    if (level) {
        env->pending_interrupts |= 1 << n_IRQ;
        cpu_interrupt(env, CPU_INTERRUPT_HARD);
    } else {
        env->pending_interrupts &= ~(1 << n_IRQ);
        if (env->pending_interrupts == 0)
            cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
    }

    if (old_pending != env->pending_interrupts) {
#ifdef CONFIG_KVM
        kvmppc_set_interrupt(env, n_IRQ, level);
#endif
    }

    LOG_IRQ("%s: %p n_IRQ %d level %d => pending %08" PRIx32
                "req %08x\n", __func__, env, n_IRQ, level,
                env->pending_interrupts, env->interrupt_request);
}

/* PowerPC 6xx / 7xx internal IRQ controller */
static void ppc6xx_set_irq (void *opaque, int pin, int level)
{
    CPUState *env = opaque;
    int cur_level;

    LOG_IRQ("%s: env %p pin %d level %d\n", __func__,
                env, pin, level);
    cur_level = (env->irq_input_state >> pin) & 1;
    /* Don't generate spurious events */
    if ((cur_level == 1 && level == 0) || (cur_level == 0 && level != 0)) {
        switch (pin) {
        case PPC6xx_INPUT_TBEN:
            /* Level sensitive - active high */
            LOG_IRQ("%s: %s the time base\n",
                        __func__, level ? "start" : "stop");
            if (level) {
                cpu_ppc_tb_start(env);
            } else {
                cpu_ppc_tb_stop(env);
            }
        case PPC6xx_INPUT_INT:
            /* Level sensitive - active high */
            LOG_IRQ("%s: set the external IRQ state to %d\n",
                        __func__, level);
            ppc_set_irq(env, PPC_INTERRUPT_EXT, level);
            break;
        case PPC6xx_INPUT_SMI:
            /* Level sensitive - active high */
            LOG_IRQ("%s: set the SMI IRQ state to %d\n",
                        __func__, level);
            ppc_set_irq(env, PPC_INTERRUPT_SMI, level);
            break;
        case PPC6xx_INPUT_MCP:
            /* Negative edge sensitive */
            /* XXX: TODO: actual reaction may depends on HID0 status
             *            603/604/740/750: check HID0[EMCP]
             */
            if (cur_level == 1 && level == 0) {
                LOG_IRQ("%s: raise machine check state\n",
                            __func__);
                ppc_set_irq(env, PPC_INTERRUPT_MCK, 1);
            }
            break;
        case PPC6xx_INPUT_CKSTP_IN:
            /* Level sensitive - active low */
            /* XXX: TODO: relay the signal to CKSTP_OUT pin */
            /* XXX: Note that the only way to restart the CPU is to reset it */
            if (level) {
                LOG_IRQ("%s: stop the CPU\n", __func__);
                env->halted = 1;
            }
            break;
        case PPC6xx_INPUT_HRESET:
            /* Level sensitive - active low */
            if (level) {
                LOG_IRQ("%s: reset the CPU\n", __func__);
                env->interrupt_request |= CPU_INTERRUPT_EXITTB;
                /* XXX: TOFIX */
#if 0
                cpu_reset(env);
#else
                qemu_system_reset_request();
#endif
            }
            break;
        case PPC6xx_INPUT_SRESET:
            LOG_IRQ("%s: set the RESET IRQ state to %d\n",
                        __func__, level);
            ppc_set_irq(env, PPC_INTERRUPT_RESET, level);
            break;
        default:
            /* Unknown pin - do nothing */
            LOG_IRQ("%s: unknown IRQ pin %d\n", __func__, pin);
            return;
        }
        if (level)
            env->irq_input_state |= 1 << pin;
        else
            env->irq_input_state &= ~(1 << pin);
    }
}

void ppc6xx_irq_init (CPUState *env)
{
    env->irq_inputs = (void **)qemu_allocate_irqs(&ppc6xx_set_irq, env,
                                                  PPC6xx_INPUT_NB);
}

#if defined(TARGET_PPC64)
/* PowerPC 970 internal IRQ controller */
static void ppc970_set_irq (void *opaque, int pin, int level)
{
    CPUState *env = opaque;
    int cur_level;

    LOG_IRQ("%s: env %p pin %d level %d\n", __func__,
                env, pin, level);
    cur_level = (env->irq_input_state >> pin) & 1;
    /* Don't generate spurious events */
    if ((cur_level == 1 && level == 0) || (cur_level == 0 && level != 0)) {
        switch (pin) {
        case PPC970_INPUT_INT:
            /* Level sensitive - active high */
            LOG_IRQ("%s: set the external IRQ state to %d\n",
                        __func__, level);
            ppc_set_irq(env, PPC_INTERRUPT_EXT, level);
            break;
        case PPC970_INPUT_THINT:
            /* Level sensitive - active high */
            LOG_IRQ("%s: set the SMI IRQ state to %d\n", __func__,
                        level);
            ppc_set_irq(env, PPC_INTERRUPT_THERM, level);
            break;
        case PPC970_INPUT_MCP:
            /* Negative edge sensitive */
            /* XXX: TODO: actual reaction may depends on HID0 status
             *            603/604/740/750: check HID0[EMCP]
             */
            if (cur_level == 1 && level == 0) {
                LOG_IRQ("%s: raise machine check state\n",
                            __func__);
                ppc_set_irq(env, PPC_INTERRUPT_MCK, 1);
            }
            break;
        case PPC970_INPUT_CKSTP:
            /* Level sensitive - active low */
            /* XXX: TODO: relay the signal to CKSTP_OUT pin */
            if (level) {
                LOG_IRQ("%s: stop the CPU\n", __func__);
                env->halted = 1;
            } else {
                LOG_IRQ("%s: restart the CPU\n", __func__);
                env->halted = 0;
                qemu_cpu_kick(env);
            }
            break;
        case PPC970_INPUT_HRESET:
            /* Level sensitive - active low */
            if (level) {
#if 0 // XXX: TOFIX
                LOG_IRQ("%s: reset the CPU\n", __func__);
                cpu_reset(env);
#endif
            }
            break;
        case PPC970_INPUT_SRESET:
            LOG_IRQ("%s: set the RESET IRQ state to %d\n",
                        __func__, level);
            ppc_set_irq(env, PPC_INTERRUPT_RESET, level);
            break;
        case PPC970_INPUT_TBEN:
            LOG_IRQ("%s: set the TBEN state to %d\n", __func__,
                        level);
            /* XXX: TODO */
            break;
        default:
            /* Unknown pin - do nothing */
            LOG_IRQ("%s: unknown IRQ pin %d\n", __func__, pin);
            return;
        }
        if (level)
            env->irq_input_state |= 1 << pin;
        else
            env->irq_input_state &= ~(1 << pin);
    }
}

void ppc970_irq_init (CPUState *env)
{
    env->irq_inputs = (void **)qemu_allocate_irqs(&ppc970_set_irq, env,
                                                  PPC970_INPUT_NB);
}

/* POWER7 internal IRQ controller */
static void power7_set_irq (void *opaque, int pin, int level)
{
    CPUState *env = opaque;

    LOG_IRQ("%s: env %p pin %d level %d\n", __func__,
                env, pin, level);

    switch (pin) {
    case POWER7_INPUT_INT:
        /* Level sensitive - active high */
        LOG_IRQ("%s: set the external IRQ state to %d\n",
                __func__, level);
        ppc_set_irq(env, PPC_INTERRUPT_EXT, level);
        break;
    default:
        /* Unknown pin - do nothing */
        LOG_IRQ("%s: unknown IRQ pin %d\n", __func__, pin);
        return;
    }
    if (level) {
        env->irq_input_state |= 1 << pin;
    } else {
        env->irq_input_state &= ~(1 << pin);
    }
}

void ppcPOWER7_irq_init (CPUState *env)
{
    env->irq_inputs = (void **)qemu_allocate_irqs(&power7_set_irq, env,
                                                  POWER7_INPUT_NB);
}
#endif /* defined(TARGET_PPC64) */

/* PowerPC 40x internal IRQ controller */
static void ppc40x_set_irq (void *opaque, int pin, int level)
{
    CPUState *env = opaque;
    int cur_level;

    LOG_IRQ("%s: env %p pin %d level %d\n", __func__,
                env, pin, level);
    cur_level = (env->irq_input_state >> pin) & 1;
    /* Don't generate spurious events */
    if ((cur_level == 1 && level == 0) || (cur_level == 0 && level != 0)) {
        switch (pin) {
        case PPC40x_INPUT_RESET_SYS:
            if (level) {
                LOG_IRQ("%s: reset the PowerPC system\n",
                            __func__);
                ppc40x_system_reset(env);
            }
            break;
        case PPC40x_INPUT_RESET_CHIP:
            if (level) {
                LOG_IRQ("%s: reset the PowerPC chip\n", __func__);
                ppc40x_chip_reset(env);
            }
            break;
        case PPC40x_INPUT_RESET_CORE:
            /* XXX: TODO: update DBSR[MRR] */
            if (level) {
                LOG_IRQ("%s: reset the PowerPC core\n", __func__);
                ppc40x_core_reset(env);
            }
            break;
        case PPC40x_INPUT_CINT:
            /* Level sensitive - active high */
            LOG_IRQ("%s: set the critical IRQ state to %d\n",
                        __func__, level);
            ppc_set_irq(env, PPC_INTERRUPT_CEXT, level);
            break;
        case PPC40x_INPUT_INT:
            /* Level sensitive - active high */
            LOG_IRQ("%s: set the external IRQ state to %d\n",
                        __func__, level);
            ppc_set_irq(env, PPC_INTERRUPT_EXT, level);
            break;
        case PPC40x_INPUT_HALT:
            /* Level sensitive - active low */
            if (level) {
                LOG_IRQ("%s: stop the CPU\n", __func__);
                env->halted = 1;
            } else {
                LOG_IRQ("%s: restart the CPU\n", __func__);
                env->halted = 0;
                qemu_cpu_kick(env);
            }
            break;
        case PPC40x_INPUT_DEBUG:
            /* Level sensitive - active high */
            LOG_IRQ("%s: set the debug pin state to %d\n",
                        __func__, level);
            ppc_set_irq(env, PPC_INTERRUPT_DEBUG, level);
            break;
        default:
            /* Unknown pin - do nothing */
            LOG_IRQ("%s: unknown IRQ pin %d\n", __func__, pin);
            return;
        }
        if (level)
            env->irq_input_state |= 1 << pin;
        else
            env->irq_input_state &= ~(1 << pin);
    }
}

void ppc40x_irq_init (CPUState *env)
{
    env->irq_inputs = (void **)qemu_allocate_irqs(&ppc40x_set_irq,
                                                  env, PPC40x_INPUT_NB);
}

/* PowerPC E500 internal IRQ controller */
static void ppce500_set_irq (void *opaque, int pin, int level)
{
    CPUState *env = opaque;
    int cur_level;

    LOG_IRQ("%s: env %p pin %d level %d\n", __func__,
                env, pin, level);
    cur_level = (env->irq_input_state >> pin) & 1;
    /* Don't generate spurious events */
    if ((cur_level == 1 && level == 0) || (cur_level == 0 && level != 0)) {
        switch (pin) {
        case PPCE500_INPUT_MCK:
            if (level) {
                LOG_IRQ("%s: reset the PowerPC system\n",
                            __func__);
                qemu_system_reset_request();
            }
            break;
        case PPCE500_INPUT_RESET_CORE:
            if (level) {
                LOG_IRQ("%s: reset the PowerPC core\n", __func__);
                ppc_set_irq(env, PPC_INTERRUPT_MCK, level);
            }
            break;
        case PPCE500_INPUT_CINT:
            /* Level sensitive - active high */
            LOG_IRQ("%s: set the critical IRQ state to %d\n",
                        __func__, level);
            ppc_set_irq(env, PPC_INTERRUPT_CEXT, level);
            break;
        case PPCE500_INPUT_INT:
            /* Level sensitive - active high */
            LOG_IRQ("%s: set the core IRQ state to %d\n",
                        __func__, level);
            ppc_set_irq(env, PPC_INTERRUPT_EXT, level);
            break;
        case PPCE500_INPUT_DEBUG:
            /* Level sensitive - active high */
            LOG_IRQ("%s: set the debug pin state to %d\n",
                        __func__, level);
            ppc_set_irq(env, PPC_INTERRUPT_DEBUG, level);
            break;
        default:
            /* Unknown pin - do nothing */
            LOG_IRQ("%s: unknown IRQ pin %d\n", __func__, pin);
            return;
        }
        if (level)
            env->irq_input_state |= 1 << pin;
        else
            env->irq_input_state &= ~(1 << pin);
    }
}

void ppce500_irq_init (CPUState *env)
{
    env->irq_inputs = (void **)qemu_allocate_irqs(&ppce500_set_irq,
                                        env, PPCE500_INPUT_NB);
}
/*****************************************************************************/
/* PowerPC time base and decrementer emulation */

uint64_t cpu_ppc_get_tb(ppc_tb_t *tb_env, uint64_t vmclk, int64_t tb_offset)
{
    /* TB time in tb periods */
    return muldiv64(vmclk, tb_env->tb_freq, get_ticks_per_sec()) + tb_offset;
}

uint64_t cpu_ppc_load_tbl (CPUState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    if (kvm_enabled()) {
        return env->spr[SPR_TBL];
    }

    tb = cpu_ppc_get_tb(tb_env, qemu_get_clock_ns(vm_clock), tb_env->tb_offset);
    LOG_TB("%s: tb %016" PRIx64 "\n", __func__, tb);

    return tb;
}

static inline uint32_t _cpu_ppc_load_tbu(CPUState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env, qemu_get_clock_ns(vm_clock), tb_env->tb_offset);
    LOG_TB("%s: tb %016" PRIx64 "\n", __func__, tb);

    return tb >> 32;
}

uint32_t cpu_ppc_load_tbu (CPUState *env)
{
    if (kvm_enabled()) {
        return env->spr[SPR_TBU];
    }

    return _cpu_ppc_load_tbu(env);
}

static inline void cpu_ppc_store_tb(ppc_tb_t *tb_env, uint64_t vmclk,
                                    int64_t *tb_offsetp, uint64_t value)
{
    *tb_offsetp = value - muldiv64(vmclk, tb_env->tb_freq, get_ticks_per_sec());
    LOG_TB("%s: tb %016" PRIx64 " offset %08" PRIx64 "\n",
                __func__, value, *tb_offsetp);
}

void cpu_ppc_store_tbl (CPUState *env, uint32_t value)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env, qemu_get_clock_ns(vm_clock), tb_env->tb_offset);
    tb &= 0xFFFFFFFF00000000ULL;
    cpu_ppc_store_tb(tb_env, qemu_get_clock_ns(vm_clock),
                     &tb_env->tb_offset, tb | (uint64_t)value);
}

static inline void _cpu_ppc_store_tbu(CPUState *env, uint32_t value)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env, qemu_get_clock_ns(vm_clock), tb_env->tb_offset);
    tb &= 0x00000000FFFFFFFFULL;
    cpu_ppc_store_tb(tb_env, qemu_get_clock_ns(vm_clock),
                     &tb_env->tb_offset, ((uint64_t)value << 32) | tb);
}

void cpu_ppc_store_tbu (CPUState *env, uint32_t value)
{
    _cpu_ppc_store_tbu(env, value);
}

uint64_t cpu_ppc_load_atbl (CPUState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env, qemu_get_clock_ns(vm_clock), tb_env->atb_offset);
    LOG_TB("%s: tb %016" PRIx64 "\n", __func__, tb);

    return tb;
}

uint32_t cpu_ppc_load_atbu (CPUState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env, qemu_get_clock_ns(vm_clock), tb_env->atb_offset);
    LOG_TB("%s: tb %016" PRIx64 "\n", __func__, tb);

    return tb >> 32;
}

void cpu_ppc_store_atbl (CPUState *env, uint32_t value)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env, qemu_get_clock_ns(vm_clock), tb_env->atb_offset);
    tb &= 0xFFFFFFFF00000000ULL;
    cpu_ppc_store_tb(tb_env, qemu_get_clock_ns(vm_clock),
                     &tb_env->atb_offset, tb | (uint64_t)value);
}

void cpu_ppc_store_atbu (CPUState *env, uint32_t value)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env, qemu_get_clock_ns(vm_clock), tb_env->atb_offset);
    tb &= 0x00000000FFFFFFFFULL;
    cpu_ppc_store_tb(tb_env, qemu_get_clock_ns(vm_clock),
                     &tb_env->atb_offset, ((uint64_t)value << 32) | tb);
}

static void cpu_ppc_tb_stop (CPUState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb, atb, vmclk;

    /* If the time base is already frozen, do nothing */
    if (tb_env->tb_freq != 0) {
        vmclk = qemu_get_clock_ns(vm_clock);
        /* Get the time base */
        tb = cpu_ppc_get_tb(tb_env, vmclk, tb_env->tb_offset);
        /* Get the alternate time base */
        atb = cpu_ppc_get_tb(tb_env, vmclk, tb_env->atb_offset);
        /* Store the time base value (ie compute the current offset) */
        cpu_ppc_store_tb(tb_env, vmclk, &tb_env->tb_offset, tb);
        /* Store the alternate time base value (compute the current offset) */
        cpu_ppc_store_tb(tb_env, vmclk, &tb_env->atb_offset, atb);
        /* Set the time base frequency to zero */
        tb_env->tb_freq = 0;
        /* Now, the time bases are frozen to tb_offset / atb_offset value */
    }
}

static void cpu_ppc_tb_start (CPUState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb, atb, vmclk;

    /* If the time base is not frozen, do nothing */
    if (tb_env->tb_freq == 0) {
        vmclk = qemu_get_clock_ns(vm_clock);
        /* Get the time base from tb_offset */
        tb = tb_env->tb_offset;
        /* Get the alternate time base from atb_offset */
        atb = tb_env->atb_offset;
        /* Restore the tb frequency from the decrementer frequency */
        tb_env->tb_freq = tb_env->decr_freq;
        /* Store the time base value */
        cpu_ppc_store_tb(tb_env, vmclk, &tb_env->tb_offset, tb);
        /* Store the alternate time base value */
        cpu_ppc_store_tb(tb_env, vmclk, &tb_env->atb_offset, atb);
    }
}

static inline uint32_t _cpu_ppc_load_decr(CPUState *env, uint64_t next)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint32_t decr;
    int64_t diff;

    diff = next - qemu_get_clock_ns(vm_clock);
    if (diff >= 0) {
        decr = muldiv64(diff, tb_env->decr_freq, get_ticks_per_sec());
    } else if (tb_env->flags & PPC_TIMER_BOOKE) {
        decr = 0;
    }  else {
        decr = -muldiv64(-diff, tb_env->decr_freq, get_ticks_per_sec());
    }
    LOG_TB("%s: %08" PRIx32 "\n", __func__, decr);

    return decr;
}

uint32_t cpu_ppc_load_decr (CPUState *env)
{
    ppc_tb_t *tb_env = env->tb_env;

    if (kvm_enabled()) {
        return env->spr[SPR_DECR];
    }

    return _cpu_ppc_load_decr(env, tb_env->decr_next);
}

uint32_t cpu_ppc_load_hdecr (CPUState *env)
{
    ppc_tb_t *tb_env = env->tb_env;

    return _cpu_ppc_load_decr(env, tb_env->hdecr_next);
}

uint64_t cpu_ppc_load_purr (CPUState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t diff;

    diff = qemu_get_clock_ns(vm_clock) - tb_env->purr_start;

    return tb_env->purr_load + muldiv64(diff, tb_env->tb_freq, get_ticks_per_sec());
}

/* When decrementer expires,
 * all we need to do is generate or queue a CPU exception
 */
static inline void cpu_ppc_decr_excp(CPUState *env)
{
    /* Raise it */
    LOG_TB("raise decrementer exception\n");
    ppc_set_irq(env, PPC_INTERRUPT_DECR, 1);
}

static inline void cpu_ppc_hdecr_excp(CPUState *env)
{
    /* Raise it */
    LOG_TB("raise decrementer exception\n");
    ppc_set_irq(env, PPC_INTERRUPT_HDECR, 1);
}

static void __cpu_ppc_store_decr (CPUState *env, uint64_t *nextp,
                                  struct QEMUTimer *timer,
                                  void (*raise_excp)(CPUState *),
                                  uint32_t decr, uint32_t value,
                                  int is_excp)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t now, next;

    LOG_TB("%s: %08" PRIx32 " => %08" PRIx32 "\n", __func__,
                decr, value);

    if (kvm_enabled()) {
        /* KVM handles decrementer exceptions, we don't need our own timer */
        return;
    }

    now = qemu_get_clock_ns(vm_clock);
    next = now + muldiv64(value, get_ticks_per_sec(), tb_env->decr_freq);
    if (is_excp) {
        next += *nextp - now;
    }
    if (next == now) {
        next++;
    }
    *nextp = next;
    /* Adjust timer */
    qemu_mod_timer(timer, next);

    /* If we set a negative value and the decrementer was positive, raise an
     * exception.
     */
    if ((tb_env->flags & PPC_DECR_UNDERFLOW_TRIGGERED)
        && (value & 0x80000000)
        && !(decr & 0x80000000)) {
        (*raise_excp)(env);
    }
}

static inline void _cpu_ppc_store_decr(CPUState *env, uint32_t decr,
                                       uint32_t value, int is_excp)
{
    ppc_tb_t *tb_env = env->tb_env;

    __cpu_ppc_store_decr(env, &tb_env->decr_next, tb_env->decr_timer,
                         &cpu_ppc_decr_excp, decr, value, is_excp);
}

void cpu_ppc_store_decr (CPUState *env, uint32_t value)
{
    _cpu_ppc_store_decr(env, cpu_ppc_load_decr(env), value, 0);
}

static void cpu_ppc_decr_cb (void *opaque)
{
    _cpu_ppc_store_decr(opaque, 0x00000000, 0xFFFFFFFF, 1);
}

static inline void _cpu_ppc_store_hdecr(CPUState *env, uint32_t hdecr,
                                        uint32_t value, int is_excp)
{
    ppc_tb_t *tb_env = env->tb_env;

    if (tb_env->hdecr_timer != NULL) {
        __cpu_ppc_store_decr(env, &tb_env->hdecr_next, tb_env->hdecr_timer,
                             &cpu_ppc_hdecr_excp, hdecr, value, is_excp);
    }
}

void cpu_ppc_store_hdecr (CPUState *env, uint32_t value)
{
    _cpu_ppc_store_hdecr(env, cpu_ppc_load_hdecr(env), value, 0);
}

static void cpu_ppc_hdecr_cb (void *opaque)
{
    _cpu_ppc_store_hdecr(opaque, 0x00000000, 0xFFFFFFFF, 1);
}

void cpu_ppc_store_purr (CPUState *env, uint64_t value)
{
    ppc_tb_t *tb_env = env->tb_env;

    tb_env->purr_load = value;
    tb_env->purr_start = qemu_get_clock_ns(vm_clock);
}

static void cpu_ppc_set_tb_clk (void *opaque, uint32_t freq)
{
    CPUState *env = opaque;
    ppc_tb_t *tb_env = env->tb_env;

    tb_env->tb_freq = freq;
    tb_env->decr_freq = freq;
    /* There is a bug in Linux 2.4 kernels:
     * if a decrementer exception is pending when it enables msr_ee at startup,
     * it's not ready to handle it...
     */
    _cpu_ppc_store_decr(env, 0xFFFFFFFF, 0xFFFFFFFF, 0);
    _cpu_ppc_store_hdecr(env, 0xFFFFFFFF, 0xFFFFFFFF, 0);
    cpu_ppc_store_purr(env, 0x0000000000000000ULL);
}

/* Set up (once) timebase frequency (in Hz) */
clk_setup_cb cpu_ppc_tb_init (CPUState *env, uint32_t freq)
{
    ppc_tb_t *tb_env;

    tb_env = g_malloc0(sizeof(ppc_tb_t));
    env->tb_env = tb_env;
    tb_env->flags = PPC_DECR_UNDERFLOW_TRIGGERED;
    /* Create new timer */
    tb_env->decr_timer = qemu_new_timer_ns(vm_clock, &cpu_ppc_decr_cb, env);
    if (0) {
        /* XXX: find a suitable condition to enable the hypervisor decrementer
         */
        tb_env->hdecr_timer = qemu_new_timer_ns(vm_clock, &cpu_ppc_hdecr_cb, env);
    } else {
        tb_env->hdecr_timer = NULL;
    }
    cpu_ppc_set_tb_clk(env, freq);

    return &cpu_ppc_set_tb_clk;
}

/* Specific helpers for POWER & PowerPC 601 RTC */
#if 0
static clk_setup_cb cpu_ppc601_rtc_init (CPUState *env)
{
    return cpu_ppc_tb_init(env, 7812500);
}
#endif

void cpu_ppc601_store_rtcu (CPUState *env, uint32_t value)
{
    _cpu_ppc_store_tbu(env, value);
}

uint32_t cpu_ppc601_load_rtcu (CPUState *env)
{
    return _cpu_ppc_load_tbu(env);
}

void cpu_ppc601_store_rtcl (CPUState *env, uint32_t value)
{
    cpu_ppc_store_tbl(env, value & 0x3FFFFF80);
}

uint32_t cpu_ppc601_load_rtcl (CPUState *env)
{
    return cpu_ppc_load_tbl(env) & 0x3FFFFF80;
}

/*****************************************************************************/
/* PowerPC 40x timers */

/* PIT, FIT & WDT */
typedef struct ppc40x_timer_t ppc40x_timer_t;
struct ppc40x_timer_t {
    uint64_t pit_reload;  /* PIT auto-reload value        */
    uint64_t fit_next;    /* Tick for next FIT interrupt  */
    struct QEMUTimer *fit_timer;
    uint64_t wdt_next;    /* Tick for next WDT interrupt  */
    struct QEMUTimer *wdt_timer;

    /* 405 have the PIT, 440 have a DECR.  */
    unsigned int decr_excp;
};

/* Fixed interval timer */
static void cpu_4xx_fit_cb (void *opaque)
{
    CPUState *env;
    ppc_tb_t *tb_env;
    ppc40x_timer_t *ppc40x_timer;
    uint64_t now, next;

    env = opaque;
    tb_env = env->tb_env;
    ppc40x_timer = tb_env->opaque;
    now = qemu_get_clock_ns(vm_clock);
    switch ((env->spr[SPR_40x_TCR] >> 24) & 0x3) {
    case 0:
        next = 1 << 9;
        break;
    case 1:
        next = 1 << 13;
        break;
    case 2:
        next = 1 << 17;
        break;
    case 3:
        next = 1 << 21;
        break;
    default:
        /* Cannot occur, but makes gcc happy */
        return;
    }
    next = now + muldiv64(next, get_ticks_per_sec(), tb_env->tb_freq);
    if (next == now)
        next++;
    qemu_mod_timer(ppc40x_timer->fit_timer, next);
    env->spr[SPR_40x_TSR] |= 1 << 26;
    if ((env->spr[SPR_40x_TCR] >> 23) & 0x1)
        ppc_set_irq(env, PPC_INTERRUPT_FIT, 1);
    LOG_TB("%s: ir %d TCR " TARGET_FMT_lx " TSR " TARGET_FMT_lx "\n", __func__,
           (int)((env->spr[SPR_40x_TCR] >> 23) & 0x1),
           env->spr[SPR_40x_TCR], env->spr[SPR_40x_TSR]);
}

/* Programmable interval timer */
static void start_stop_pit (CPUState *env, ppc_tb_t *tb_env, int is_excp)
{
    ppc40x_timer_t *ppc40x_timer;
    uint64_t now, next;

    ppc40x_timer = tb_env->opaque;
    if (ppc40x_timer->pit_reload <= 1 ||
        !((env->spr[SPR_40x_TCR] >> 26) & 0x1) ||
        (is_excp && !((env->spr[SPR_40x_TCR] >> 22) & 0x1))) {
        /* Stop PIT */
        LOG_TB("%s: stop PIT\n", __func__);
        qemu_del_timer(tb_env->decr_timer);
    } else {
        LOG_TB("%s: start PIT %016" PRIx64 "\n",
                    __func__, ppc40x_timer->pit_reload);
        now = qemu_get_clock_ns(vm_clock);
        next = now + muldiv64(ppc40x_timer->pit_reload,
                              get_ticks_per_sec(), tb_env->decr_freq);
        if (is_excp)
            next += tb_env->decr_next - now;
        if (next == now)
            next++;
        qemu_mod_timer(tb_env->decr_timer, next);
        tb_env->decr_next = next;
    }
}

static void cpu_4xx_pit_cb (void *opaque)
{
    CPUState *env;
    ppc_tb_t *tb_env;
    ppc40x_timer_t *ppc40x_timer;

    env = opaque;
    tb_env = env->tb_env;
    ppc40x_timer = tb_env->opaque;
    env->spr[SPR_40x_TSR] |= 1 << 27;
    if ((env->spr[SPR_40x_TCR] >> 26) & 0x1)
        ppc_set_irq(env, ppc40x_timer->decr_excp, 1);
    start_stop_pit(env, tb_env, 1);
    LOG_TB("%s: ar %d ir %d TCR " TARGET_FMT_lx " TSR " TARGET_FMT_lx " "
           "%016" PRIx64 "\n", __func__,
           (int)((env->spr[SPR_40x_TCR] >> 22) & 0x1),
           (int)((env->spr[SPR_40x_TCR] >> 26) & 0x1),
           env->spr[SPR_40x_TCR], env->spr[SPR_40x_TSR],
           ppc40x_timer->pit_reload);
}

/* Watchdog timer */
static void cpu_4xx_wdt_cb (void *opaque)
{
    CPUState *env;
    ppc_tb_t *tb_env;
    ppc40x_timer_t *ppc40x_timer;
    uint64_t now, next;

    env = opaque;
    tb_env = env->tb_env;
    ppc40x_timer = tb_env->opaque;
    now = qemu_get_clock_ns(vm_clock);
    switch ((env->spr[SPR_40x_TCR] >> 30) & 0x3) {
    case 0:
        next = 1 << 17;
        break;
    case 1:
        next = 1 << 21;
        break;
    case 2:
        next = 1 << 25;
        break;
    case 3:
        next = 1 << 29;
        break;
    default:
        /* Cannot occur, but makes gcc happy */
        return;
    }
    next = now + muldiv64(next, get_ticks_per_sec(), tb_env->decr_freq);
    if (next == now)
        next++;
    LOG_TB("%s: TCR " TARGET_FMT_lx " TSR " TARGET_FMT_lx "\n", __func__,
           env->spr[SPR_40x_TCR], env->spr[SPR_40x_TSR]);
    switch ((env->spr[SPR_40x_TSR] >> 30) & 0x3) {
    case 0x0:
    case 0x1:
        qemu_mod_timer(ppc40x_timer->wdt_timer, next);
        ppc40x_timer->wdt_next = next;
        env->spr[SPR_40x_TSR] |= 1 << 31;
        break;
    case 0x2:
        qemu_mod_timer(ppc40x_timer->wdt_timer, next);
        ppc40x_timer->wdt_next = next;
        env->spr[SPR_40x_TSR] |= 1 << 30;
        if ((env->spr[SPR_40x_TCR] >> 27) & 0x1)
            ppc_set_irq(env, PPC_INTERRUPT_WDT, 1);
        break;
    case 0x3:
        env->spr[SPR_40x_TSR] &= ~0x30000000;
        env->spr[SPR_40x_TSR] |= env->spr[SPR_40x_TCR] & 0x30000000;
        switch ((env->spr[SPR_40x_TCR] >> 28) & 0x3) {
        case 0x0:
            /* No reset */
            break;
        case 0x1: /* Core reset */
            ppc40x_core_reset(env);
            break;
        case 0x2: /* Chip reset */
            ppc40x_chip_reset(env);
            break;
        case 0x3: /* System reset */
            ppc40x_system_reset(env);
            break;
        }
    }
}

void store_40x_pit (CPUState *env, target_ulong val)
{
    ppc_tb_t *tb_env;
    ppc40x_timer_t *ppc40x_timer;

    tb_env = env->tb_env;
    ppc40x_timer = tb_env->opaque;
    LOG_TB("%s val" TARGET_FMT_lx "\n", __func__, val);
    ppc40x_timer->pit_reload = val;
    start_stop_pit(env, tb_env, 0);
}

target_ulong load_40x_pit (CPUState *env)
{
    return cpu_ppc_load_decr(env);
}

static void ppc_40x_set_tb_clk (void *opaque, uint32_t freq)
{
    CPUState *env = opaque;
    ppc_tb_t *tb_env = env->tb_env;

    LOG_TB("%s set new frequency to %" PRIu32 "\n", __func__,
                freq);
    tb_env->tb_freq = freq;
    tb_env->decr_freq = freq;
    /* XXX: we should also update all timers */
}

clk_setup_cb ppc_40x_timers_init (CPUState *env, uint32_t freq,
                                  unsigned int decr_excp)
{
    ppc_tb_t *tb_env;
    ppc40x_timer_t *ppc40x_timer;

    tb_env = g_malloc0(sizeof(ppc_tb_t));
    env->tb_env = tb_env;
    tb_env->flags = PPC_DECR_UNDERFLOW_TRIGGERED;
    ppc40x_timer = g_malloc0(sizeof(ppc40x_timer_t));
    tb_env->tb_freq = freq;
    tb_env->decr_freq = freq;
    tb_env->opaque = ppc40x_timer;
    LOG_TB("%s freq %" PRIu32 "\n", __func__, freq);
    if (ppc40x_timer != NULL) {
        /* We use decr timer for PIT */
        tb_env->decr_timer = qemu_new_timer_ns(vm_clock, &cpu_4xx_pit_cb, env);
        ppc40x_timer->fit_timer =
            qemu_new_timer_ns(vm_clock, &cpu_4xx_fit_cb, env);
        ppc40x_timer->wdt_timer =
            qemu_new_timer_ns(vm_clock, &cpu_4xx_wdt_cb, env);
        ppc40x_timer->decr_excp = decr_excp;
    }

    return &ppc_40x_set_tb_clk;
}

/*****************************************************************************/
/* Embedded PowerPC Device Control Registers */
typedef struct ppc_dcrn_t ppc_dcrn_t;
struct ppc_dcrn_t {
    dcr_read_cb dcr_read;
    dcr_write_cb dcr_write;
    void *opaque;
};

/* XXX: on 460, DCR addresses are 32 bits wide,
 *      using DCRIPR to get the 22 upper bits of the DCR address
 */
#define DCRN_NB 1024
struct ppc_dcr_t {
    ppc_dcrn_t dcrn[DCRN_NB];
    int (*read_error)(int dcrn);
    int (*write_error)(int dcrn);
};

int ppc_dcr_read (ppc_dcr_t *dcr_env, int dcrn, uint32_t *valp)
{
    ppc_dcrn_t *dcr;

    if (dcrn < 0 || dcrn >= DCRN_NB)
        goto error;
    dcr = &dcr_env->dcrn[dcrn];
    if (dcr->dcr_read == NULL)
        goto error;
    *valp = (*dcr->dcr_read)(dcr->opaque, dcrn);

    return 0;

 error:
    if (dcr_env->read_error != NULL)
        return (*dcr_env->read_error)(dcrn);

    return -1;
}

int ppc_dcr_write (ppc_dcr_t *dcr_env, int dcrn, uint32_t val)
{
    ppc_dcrn_t *dcr;

    if (dcrn < 0 || dcrn >= DCRN_NB)
        goto error;
    dcr = &dcr_env->dcrn[dcrn];
    if (dcr->dcr_write == NULL)
        goto error;
    (*dcr->dcr_write)(dcr->opaque, dcrn, val);

    return 0;

 error:
    if (dcr_env->write_error != NULL)
        return (*dcr_env->write_error)(dcrn);

    return -1;
}

int ppc_dcr_register (CPUState *env, int dcrn, void *opaque,
                      dcr_read_cb dcr_read, dcr_write_cb dcr_write)
{
    ppc_dcr_t *dcr_env;
    ppc_dcrn_t *dcr;

    dcr_env = env->dcr_env;
    if (dcr_env == NULL)
        return -1;
    if (dcrn < 0 || dcrn >= DCRN_NB)
        return -1;
    dcr = &dcr_env->dcrn[dcrn];
    if (dcr->opaque != NULL ||
        dcr->dcr_read != NULL ||
        dcr->dcr_write != NULL)
        return -1;
    dcr->opaque = opaque;
    dcr->dcr_read = dcr_read;
    dcr->dcr_write = dcr_write;

    return 0;
}

int ppc_dcr_init (CPUState *env, int (*read_error)(int dcrn),
                  int (*write_error)(int dcrn))
{
    ppc_dcr_t *dcr_env;

    dcr_env = g_malloc0(sizeof(ppc_dcr_t));
    dcr_env->read_error = read_error;
    dcr_env->write_error = write_error;
    env->dcr_env = dcr_env;

    return 0;
}

/*****************************************************************************/
/* Debug port */
void PPC_debug_write (void *opaque, uint32_t addr, uint32_t val)
{
    addr &= 0xF;
    switch (addr) {
    case 0:
        printf("%c", val);
        break;
    case 1:
        printf("\n");
        fflush(stdout);
        break;
    case 2:
        printf("Set loglevel to %04" PRIx32 "\n", val);
        cpu_set_log(val | 0x100);
        break;
    }
}

/*****************************************************************************/
/* NVRAM helpers */
static inline uint32_t nvram_read (nvram_t *nvram, uint32_t addr)
{
    return (*nvram->read_fn)(nvram->opaque, addr);;
}

static inline void nvram_write (nvram_t *nvram, uint32_t addr, uint32_t val)
{
    (*nvram->write_fn)(nvram->opaque, addr, val);
}

void NVRAM_set_byte (nvram_t *nvram, uint32_t addr, uint8_t value)
{
    nvram_write(nvram, addr, value);
}

uint8_t NVRAM_get_byte (nvram_t *nvram, uint32_t addr)
{
    return nvram_read(nvram, addr);
}

void NVRAM_set_word (nvram_t *nvram, uint32_t addr, uint16_t value)
{
    nvram_write(nvram, addr, value >> 8);
    nvram_write(nvram, addr + 1, value & 0xFF);
}

uint16_t NVRAM_get_word (nvram_t *nvram, uint32_t addr)
{
    uint16_t tmp;

    tmp = nvram_read(nvram, addr) << 8;
    tmp |= nvram_read(nvram, addr + 1);

    return tmp;
}

void NVRAM_set_lword (nvram_t *nvram, uint32_t addr, uint32_t value)
{
    nvram_write(nvram, addr, value >> 24);
    nvram_write(nvram, addr + 1, (value >> 16) & 0xFF);
    nvram_write(nvram, addr + 2, (value >> 8) & 0xFF);
    nvram_write(nvram, addr + 3, value & 0xFF);
}

uint32_t NVRAM_get_lword (nvram_t *nvram, uint32_t addr)
{
    uint32_t tmp;

    tmp = nvram_read(nvram, addr) << 24;
    tmp |= nvram_read(nvram, addr + 1) << 16;
    tmp |= nvram_read(nvram, addr + 2) << 8;
    tmp |= nvram_read(nvram, addr + 3);

    return tmp;
}

void NVRAM_set_string (nvram_t *nvram, uint32_t addr,
                       const char *str, uint32_t max)
{
    int i;

    for (i = 0; i < max && str[i] != '\0'; i++) {
        nvram_write(nvram, addr + i, str[i]);
    }
    nvram_write(nvram, addr + i, str[i]);
    nvram_write(nvram, addr + max - 1, '\0');
}

int NVRAM_get_string (nvram_t *nvram, uint8_t *dst, uint16_t addr, int max)
{
    int i;

    memset(dst, 0, max);
    for (i = 0; i < max; i++) {
        dst[i] = NVRAM_get_byte(nvram, addr + i);
        if (dst[i] == '\0')
            break;
    }

    return i;
}

static uint16_t NVRAM_crc_update (uint16_t prev, uint16_t value)
{
    uint16_t tmp;
    uint16_t pd, pd1, pd2;

    tmp = prev >> 8;
    pd = prev ^ value;
    pd1 = pd & 0x000F;
    pd2 = ((pd >> 4) & 0x000F) ^ pd1;
    tmp ^= (pd1 << 3) | (pd1 << 8);
    tmp ^= pd2 | (pd2 << 7) | (pd2 << 12);

    return tmp;
}

static uint16_t NVRAM_compute_crc (nvram_t *nvram, uint32_t start, uint32_t count)
{
    uint32_t i;
    uint16_t crc = 0xFFFF;
    int odd;

    odd = count & 1;
    count &= ~1;
    for (i = 0; i != count; i++) {
        crc = NVRAM_crc_update(crc, NVRAM_get_word(nvram, start + i));
    }
    if (odd) {
        crc = NVRAM_crc_update(crc, NVRAM_get_byte(nvram, start + i) << 8);
    }

    return crc;
}

#define CMDLINE_ADDR 0x017ff000

int PPC_NVRAM_set_params (nvram_t *nvram, uint16_t NVRAM_size,
                          const char *arch,
                          uint32_t RAM_size, int boot_device,
                          uint32_t kernel_image, uint32_t kernel_size,
                          const char *cmdline,
                          uint32_t initrd_image, uint32_t initrd_size,
                          uint32_t NVRAM_image,
                          int width, int height, int depth)
{
    uint16_t crc;

    /* Set parameters for Open Hack'Ware BIOS */
    NVRAM_set_string(nvram, 0x00, "QEMU_BIOS", 16);
    NVRAM_set_lword(nvram,  0x10, 0x00000002); /* structure v2 */
    NVRAM_set_word(nvram,   0x14, NVRAM_size);
    NVRAM_set_string(nvram, 0x20, arch, 16);
    NVRAM_set_lword(nvram,  0x30, RAM_size);
    NVRAM_set_byte(nvram,   0x34, boot_device);
    NVRAM_set_lword(nvram,  0x38, kernel_image);
    NVRAM_set_lword(nvram,  0x3C, kernel_size);
    if (cmdline) {
        /* XXX: put the cmdline in NVRAM too ? */
        pstrcpy_targphys("cmdline", CMDLINE_ADDR, RAM_size - CMDLINE_ADDR, cmdline);
        NVRAM_set_lword(nvram,  0x40, CMDLINE_ADDR);
        NVRAM_set_lword(nvram,  0x44, strlen(cmdline));
    } else {
        NVRAM_set_lword(nvram,  0x40, 0);
        NVRAM_set_lword(nvram,  0x44, 0);
    }
    NVRAM_set_lword(nvram,  0x48, initrd_image);
    NVRAM_set_lword(nvram,  0x4C, initrd_size);
    NVRAM_set_lword(nvram,  0x50, NVRAM_image);

    NVRAM_set_word(nvram,   0x54, width);
    NVRAM_set_word(nvram,   0x56, height);
    NVRAM_set_word(nvram,   0x58, depth);
    crc = NVRAM_compute_crc(nvram, 0x00, 0xF8);
    NVRAM_set_word(nvram,   0xFC, crc);

    return 0;
}
