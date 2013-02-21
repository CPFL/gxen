/*
 * NVIDIA NVC0 PRAMIN functions
 *
 * Copyright (c) 2012-2013 Yusuke Suzuki
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
#include "nvc0_pramin.h"
#include "nvc0_vm.h"
#include "nvc0_mmio.h"

uint32_t nvc0_pramin_read32(nvc0_state_t* state, uint64_t addr) {
    uint32_t res = 0;
    // spin_lock(&state->pramin_lock);
    {
        const uint64_t old = state->vm_engine.pramin;
        const int matched = ((addr >> 16) == old);
        if (!matched) {
            state->vm_engine.pramin = addr >> 16;
            nvc0_mmio_write32(state->bar[0].real, 0x1700, addr >> 16);
            res = nvc0_vm_pramin_read(state, (addr & 0xFFFF));
            state->vm_engine.pramin = old;
            nvc0_mmio_write32(state->bar[0].real, 0x1700, old);
        } else {
            res = nvc0_vm_pramin_read(state, (addr & 0xFFFF));
        }
    }
    // spin_unlock(&state->pramin_lock);
    return res;
}

void nvc0_pramin_write32(nvc0_state_t* state, uint64_t addr, uint32_t val) {
    // spin_lock(&state->pramin_lock);
    {
        const uint64_t old = state->vm_engine.pramin;
        const int matched = ((addr >> 16) == old);
        if (!matched) {
            state->vm_engine.pramin = addr >> 16;
            nvc0_mmio_write32(state->bar[0].real, 0x1700, addr >> 16);
            nvc0_vm_pramin_write(state, (addr & 0xFFFF), val);
            state->vm_engine.pramin = old;
            nvc0_mmio_write32(state->bar[0].real, 0x1700, old);
        } else {
            nvc0_vm_pramin_write(state, (addr & 0xFFFF), val);
        }
    }
    // spin_unlock(&state->pramin_lock);
}

/* vim: set sw=4 ts=4 et tw=80 : */
