/*
 * Optimizations for Tiny Code Generator for QEMU
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include "qemu-common.h"
#include "tcg-op.h"

#define CASE_OP_32_64(x)                        \
        glue(glue(case INDEX_op_, x), _i32):    \
        glue(glue(case INDEX_op_, x), _i64)

typedef enum {
    TCG_TEMP_UNDEF = 0,
    TCG_TEMP_CONST,
    TCG_TEMP_COPY,
    TCG_TEMP_HAS_COPY,
    TCG_TEMP_ANY
} tcg_temp_state;

struct tcg_temp_info {
    tcg_temp_state state;
    uint16_t prev_copy;
    uint16_t next_copy;
    tcg_target_ulong val;
};

static struct tcg_temp_info temps[TCG_MAX_TEMPS];

/* Reset TEMP's state to TCG_TEMP_ANY.  If TEMP was a representative of some
   class of equivalent temp's, a new representative should be chosen in this
   class. */
static void reset_temp(TCGArg temp, int nb_temps, int nb_globals)
{
    int i;
    TCGArg new_base = (TCGArg)-1;
    if (temps[temp].state == TCG_TEMP_HAS_COPY) {
        for (i = temps[temp].next_copy; i != temp; i = temps[i].next_copy) {
            if (i >= nb_globals) {
                temps[i].state = TCG_TEMP_HAS_COPY;
                new_base = i;
                break;
            }
        }
        for (i = temps[temp].next_copy; i != temp; i = temps[i].next_copy) {
            if (new_base == (TCGArg)-1) {
                temps[i].state = TCG_TEMP_ANY;
            } else {
                temps[i].val = new_base;
            }
        }
        temps[temps[temp].next_copy].prev_copy = temps[temp].prev_copy;
        temps[temps[temp].prev_copy].next_copy = temps[temp].next_copy;
    } else if (temps[temp].state == TCG_TEMP_COPY) {
        temps[temps[temp].next_copy].prev_copy = temps[temp].prev_copy;
        temps[temps[temp].prev_copy].next_copy = temps[temp].next_copy;
        new_base = temps[temp].val;
    }
    temps[temp].state = TCG_TEMP_ANY;
    if (new_base != (TCGArg)-1 && temps[new_base].next_copy == new_base) {
        temps[new_base].state = TCG_TEMP_ANY;
    }
}

static int op_bits(TCGOpcode op)
{
    const TCGOpDef *def = &tcg_op_defs[op];
    return def->flags & TCG_OPF_64BIT ? 64 : 32;
}

static TCGOpcode op_to_movi(TCGOpcode op)
{
    switch (op_bits(op)) {
    case 32:
        return INDEX_op_movi_i32;
    case 64:
        return INDEX_op_movi_i64;
    default:
        fprintf(stderr, "op_to_movi: unexpected return value of "
                "function op_bits.\n");
        tcg_abort();
    }
}

static void tcg_opt_gen_mov(TCGContext *s, TCGArg *gen_args, TCGArg dst,
                            TCGArg src, int nb_temps, int nb_globals)
{
        reset_temp(dst, nb_temps, nb_globals);
        assert(temps[src].state != TCG_TEMP_COPY);
        /* Don't try to copy if one of temps is a global or either one
           is local and another is register */
        if (src >= nb_globals && dst >= nb_globals &&
            tcg_arg_is_local(s, src) == tcg_arg_is_local(s, dst)) {
            assert(temps[src].state != TCG_TEMP_CONST);
            if (temps[src].state != TCG_TEMP_HAS_COPY) {
                temps[src].state = TCG_TEMP_HAS_COPY;
                temps[src].next_copy = src;
                temps[src].prev_copy = src;
            }
            temps[dst].state = TCG_TEMP_COPY;
            temps[dst].val = src;
            temps[dst].next_copy = temps[src].next_copy;
            temps[dst].prev_copy = src;
            temps[temps[dst].next_copy].prev_copy = dst;
            temps[src].next_copy = dst;
        }
        gen_args[0] = dst;
        gen_args[1] = src;
}

static void tcg_opt_gen_movi(TCGArg *gen_args, TCGArg dst, TCGArg val,
                             int nb_temps, int nb_globals)
{
        reset_temp(dst, nb_temps, nb_globals);
        temps[dst].state = TCG_TEMP_CONST;
        temps[dst].val = val;
        gen_args[0] = dst;
        gen_args[1] = val;
}

static TCGOpcode op_to_mov(TCGOpcode op)
{
    switch (op_bits(op)) {
    case 32:
        return INDEX_op_mov_i32;
    case 64:
        return INDEX_op_mov_i64;
    default:
        fprintf(stderr, "op_to_mov: unexpected return value of "
                "function op_bits.\n");
        tcg_abort();
    }
}

static TCGArg do_constant_folding_2(TCGOpcode op, TCGArg x, TCGArg y)
{
    switch (op) {
    CASE_OP_32_64(add):
        return x + y;

    CASE_OP_32_64(sub):
        return x - y;

    CASE_OP_32_64(mul):
        return x * y;

    CASE_OP_32_64(and):
        return x & y;

    CASE_OP_32_64(or):
        return x | y;

    CASE_OP_32_64(xor):
        return x ^ y;

    case INDEX_op_shl_i32:
        return (uint32_t)x << (uint32_t)y;

    case INDEX_op_shl_i64:
        return (uint64_t)x << (uint64_t)y;

    case INDEX_op_shr_i32:
        return (uint32_t)x >> (uint32_t)y;

    case INDEX_op_shr_i64:
        return (uint64_t)x >> (uint64_t)y;

    case INDEX_op_sar_i32:
        return (int32_t)x >> (int32_t)y;

    case INDEX_op_sar_i64:
        return (int64_t)x >> (int64_t)y;

    case INDEX_op_rotr_i32:
        x = ((uint32_t)x << (32 - y)) | ((uint32_t)x >> y);
        return x;

    case INDEX_op_rotr_i64:
        x = ((uint64_t)x << (64 - y)) | ((uint64_t)x >> y);
        return x;

    case INDEX_op_rotl_i32:
        x = ((uint32_t)x << y) | ((uint32_t)x >> (32 - y));
        return x;

    case INDEX_op_rotl_i64:
        x = ((uint64_t)x << y) | ((uint64_t)x >> (64 - y));
        return x;

    CASE_OP_32_64(not):
        return ~x;

    CASE_OP_32_64(neg):
        return -x;

    CASE_OP_32_64(andc):
        return x & ~y;

    CASE_OP_32_64(orc):
        return x | ~y;

    CASE_OP_32_64(eqv):
        return ~(x ^ y);

    CASE_OP_32_64(nand):
        return ~(x & y);

    CASE_OP_32_64(nor):
        return ~(x | y);

    CASE_OP_32_64(ext8s):
        return (int8_t)x;

    CASE_OP_32_64(ext16s):
        return (int16_t)x;

    CASE_OP_32_64(ext8u):
        return (uint8_t)x;

    CASE_OP_32_64(ext16u):
        return (uint16_t)x;

    case INDEX_op_ext32s_i64:
        return (int32_t)x;

    case INDEX_op_ext32u_i64:
        return (uint32_t)x;

    default:
        fprintf(stderr,
                "Unrecognized operation %d in do_constant_folding.\n", op);
        tcg_abort();
    }
}

static TCGArg do_constant_folding(TCGOpcode op, TCGArg x, TCGArg y)
{
    TCGArg res = do_constant_folding_2(op, x, y);
    if (op_bits(op) == 32) {
        res &= 0xffffffff;
    }
    return res;
}

/* Propagate constants and copies, fold constant expressions. */
static TCGArg *tcg_constant_folding(TCGContext *s, uint16_t *tcg_opc_ptr,
                                    TCGArg *args, TCGOpDef *tcg_op_defs)
{
    int i, nb_ops, op_index, nb_temps, nb_globals, nb_call_args;
    TCGOpcode op;
    const TCGOpDef *def;
    TCGArg *gen_args;
    TCGArg tmp;
    /* Array VALS has an element for each temp.
       If this temp holds a constant then its value is kept in VALS' element.
       If this temp is a copy of other ones then this equivalence class'
       representative is kept in VALS' element.
       If this temp is neither copy nor constant then corresponding VALS'
       element is unused. */

    nb_temps = s->nb_temps;
    nb_globals = s->nb_globals;
    memset(temps, 0, nb_temps * sizeof(struct tcg_temp_info));

    nb_ops = tcg_opc_ptr - gen_opc_buf;
    gen_args = args;
    for (op_index = 0; op_index < nb_ops; op_index++) {
        op = gen_opc_buf[op_index];
        def = &tcg_op_defs[op];
        /* Do copy propagation */
        if (!(def->flags & (TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS))) {
            assert(op != INDEX_op_call);
            for (i = def->nb_oargs; i < def->nb_oargs + def->nb_iargs; i++) {
                if (temps[args[i]].state == TCG_TEMP_COPY) {
                    args[i] = temps[args[i]].val;
                }
            }
        }

        /* For commutative operations make constant second argument */
        switch (op) {
        CASE_OP_32_64(add):
        CASE_OP_32_64(mul):
        CASE_OP_32_64(and):
        CASE_OP_32_64(or):
        CASE_OP_32_64(xor):
        CASE_OP_32_64(eqv):
        CASE_OP_32_64(nand):
        CASE_OP_32_64(nor):
            if (temps[args[1]].state == TCG_TEMP_CONST) {
                tmp = args[1];
                args[1] = args[2];
                args[2] = tmp;
            }
            break;
        default:
            break;
        }

        /* Simplify expression if possible. */
        switch (op) {
        CASE_OP_32_64(add):
        CASE_OP_32_64(sub):
        CASE_OP_32_64(shl):
        CASE_OP_32_64(shr):
        CASE_OP_32_64(sar):
        CASE_OP_32_64(rotl):
        CASE_OP_32_64(rotr):
            if (temps[args[1]].state == TCG_TEMP_CONST) {
                /* Proceed with possible constant folding. */
                break;
            }
            if (temps[args[2]].state == TCG_TEMP_CONST
                && temps[args[2]].val == 0) {
                if ((temps[args[0]].state == TCG_TEMP_COPY
                    && temps[args[0]].val == args[1])
                    || args[0] == args[1]) {
                    args += 3;
                    gen_opc_buf[op_index] = INDEX_op_nop;
                } else {
                    gen_opc_buf[op_index] = op_to_mov(op);
                    tcg_opt_gen_mov(s, gen_args, args[0], args[1],
                                    nb_temps, nb_globals);
                    gen_args += 2;
                    args += 3;
                }
                continue;
            }
            break;
        CASE_OP_32_64(mul):
            if ((temps[args[2]].state == TCG_TEMP_CONST
                && temps[args[2]].val == 0)) {
                gen_opc_buf[op_index] = op_to_movi(op);
                tcg_opt_gen_movi(gen_args, args[0], 0, nb_temps, nb_globals);
                args += 3;
                gen_args += 2;
                continue;
            }
            break;
        CASE_OP_32_64(or):
        CASE_OP_32_64(and):
            if (args[1] == args[2]) {
                if (args[1] == args[0]) {
                    args += 3;
                    gen_opc_buf[op_index] = INDEX_op_nop;
                } else {
                    gen_opc_buf[op_index] = op_to_mov(op);
                    tcg_opt_gen_mov(s, gen_args, args[0], args[1], nb_temps,
                                    nb_globals);
                    gen_args += 2;
                    args += 3;
                }
                continue;
            }
            break;
        default:
            break;
        }

        /* Propagate constants through copy operations and do constant
           folding.  Constants will be substituted to arguments by register
           allocator where needed and possible.  Also detect copies. */
        switch (op) {
        CASE_OP_32_64(mov):
            if ((temps[args[1]].state == TCG_TEMP_COPY
                && temps[args[1]].val == args[0])
                || args[0] == args[1]) {
                args += 2;
                gen_opc_buf[op_index] = INDEX_op_nop;
                break;
            }
            if (temps[args[1]].state != TCG_TEMP_CONST) {
                tcg_opt_gen_mov(s, gen_args, args[0], args[1],
                                nb_temps, nb_globals);
                gen_args += 2;
                args += 2;
                break;
            }
            /* Source argument is constant.  Rewrite the operation and
               let movi case handle it. */
            op = op_to_movi(op);
            gen_opc_buf[op_index] = op;
            args[1] = temps[args[1]].val;
            /* fallthrough */
        CASE_OP_32_64(movi):
            tcg_opt_gen_movi(gen_args, args[0], args[1], nb_temps, nb_globals);
            gen_args += 2;
            args += 2;
            break;
        CASE_OP_32_64(not):
        CASE_OP_32_64(neg):
        CASE_OP_32_64(ext8s):
        CASE_OP_32_64(ext8u):
        CASE_OP_32_64(ext16s):
        CASE_OP_32_64(ext16u):
        case INDEX_op_ext32s_i64:
        case INDEX_op_ext32u_i64:
            if (temps[args[1]].state == TCG_TEMP_CONST) {
                gen_opc_buf[op_index] = op_to_movi(op);
                tmp = do_constant_folding(op, temps[args[1]].val, 0);
                tcg_opt_gen_movi(gen_args, args[0], tmp, nb_temps, nb_globals);
                gen_args += 2;
                args += 2;
                break;
            } else {
                reset_temp(args[0], nb_temps, nb_globals);
                gen_args[0] = args[0];
                gen_args[1] = args[1];
                gen_args += 2;
                args += 2;
                break;
            }
        CASE_OP_32_64(add):
        CASE_OP_32_64(sub):
        CASE_OP_32_64(mul):
        CASE_OP_32_64(or):
        CASE_OP_32_64(and):
        CASE_OP_32_64(xor):
        CASE_OP_32_64(shl):
        CASE_OP_32_64(shr):
        CASE_OP_32_64(sar):
        CASE_OP_32_64(rotl):
        CASE_OP_32_64(rotr):
        CASE_OP_32_64(andc):
        CASE_OP_32_64(orc):
        CASE_OP_32_64(eqv):
        CASE_OP_32_64(nand):
        CASE_OP_32_64(nor):
            if (temps[args[1]].state == TCG_TEMP_CONST
                && temps[args[2]].state == TCG_TEMP_CONST) {
                gen_opc_buf[op_index] = op_to_movi(op);
                tmp = do_constant_folding(op, temps[args[1]].val,
                                          temps[args[2]].val);
                tcg_opt_gen_movi(gen_args, args[0], tmp, nb_temps, nb_globals);
                gen_args += 2;
                args += 3;
                break;
            } else {
                reset_temp(args[0], nb_temps, nb_globals);
                gen_args[0] = args[0];
                gen_args[1] = args[1];
                gen_args[2] = args[2];
                gen_args += 3;
                args += 3;
                break;
            }
        case INDEX_op_call:
            nb_call_args = (args[0] >> 16) + (args[0] & 0xffff);
            if (!(args[nb_call_args + 1] & (TCG_CALL_CONST | TCG_CALL_PURE))) {
                for (i = 0; i < nb_globals; i++) {
                    reset_temp(i, nb_temps, nb_globals);
                }
            }
            for (i = 0; i < (args[0] >> 16); i++) {
                reset_temp(args[i + 1], nb_temps, nb_globals);
            }
            i = nb_call_args + 3;
            while (i) {
                *gen_args = *args;
                args++;
                gen_args++;
                i--;
            }
            break;
        case INDEX_op_set_label:
        case INDEX_op_jmp:
        case INDEX_op_br:
        CASE_OP_32_64(brcond):
            memset(temps, 0, nb_temps * sizeof(struct tcg_temp_info));
            for (i = 0; i < def->nb_args; i++) {
                *gen_args = *args;
                args++;
                gen_args++;
            }
            break;
        default:
            /* Default case: we do know nothing about operation so no
               propagation is done.  We only trash output args.  */
            for (i = 0; i < def->nb_oargs; i++) {
                reset_temp(args[i], nb_temps, nb_globals);
            }
            for (i = 0; i < def->nb_args; i++) {
                gen_args[i] = args[i];
            }
            args += def->nb_args;
            gen_args += def->nb_args;
            break;
        }
    }

    return gen_args;
}

TCGArg *tcg_optimize(TCGContext *s, uint16_t *tcg_opc_ptr,
        TCGArg *args, TCGOpDef *tcg_op_defs)
{
    TCGArg *res;
    res = tcg_constant_folding(s, tcg_opc_ptr, args, tcg_op_defs);
    return res;
}
