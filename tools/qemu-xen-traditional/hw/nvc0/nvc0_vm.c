/*
 * NVIDIA NVC0 VM(Virtual Memory) model
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

#include "nvc0.h"
#include "nvc0_vm.h"
#include "nvc0_mmio.h"

static inline nvc0_vm_addr_t nvc0_get_bar1_addr(nvc0_state_t* state, target_phys_addr_t offset) {
    return state->vm_engine.bar1 + offset;
}

static inline nvc0_vm_addr_t nvc0_get_pramin_addr(nvc0_state_t* state, target_phys_addr_t offset) {
    return state->vm_engine.pramin + offset;
}

static uint32_t nvc0_vm_read(nvc0_state_t* state, void* real, target_phys_addr_t offset, nvc0_vm_addr_t vm_addr) {
    // tracking user_vma
    if (state->pfifo.user_vma_enabled) {
        NVC0_PRINTF("user_vma enabled!... 0x%x and 0x%x\n", state->pfifo.user_vma, offset);
        if (state->pfifo.user_vma <= vm_addr &&
                vm_addr < (NVC0_USER_VMA_CHANNEL * NVC0_CHANNELS + state->pfifo.user_vma)) {
            NVC0_PRINTF("offset shift ... 0x%X to 0x%X\n", vm_addr, vm_addr + ((state->guest * NVC0_CHANNELS_SHIFT) << 12));
            // TODO(Yusuke Suzuki) check window overflow
            offset += ((state->guest * NVC0_CHANNELS_SHIFT) << 12);
        }
    }
    const uint32_t result = nvc0_mmio_read32(real, offset);
    //if (state->log) {
        NVC0_PRINTF("read addr 0x%X => 0x%X\n", offset, result);
    //}
    return result;
}

static void nvc0_vm_write(nvc0_state_t* state, void* real, target_phys_addr_t offset, nvc0_vm_addr_t vm_addr, uint32_t value) {
    // tracking user_vma
    if (state->pfifo.user_vma_enabled) {
        NVC0_PRINTF("user_vma enabled!... 0x%x and 0x%x\n", state->pfifo.user_vma, offset);
        if (state->pfifo.user_vma <= vm_addr &&
                vm_addr < (NVC0_USER_VMA_CHANNEL * NVC0_CHANNELS + state->pfifo.user_vma)) {
            NVC0_PRINTF("offset shift ... 0x%X to 0x%X\n", vm_addr, vm_addr + ((state->guest * NVC0_CHANNELS_SHIFT) << 12));
            // TODO(Yusuke Suzuki) check window overflow
            offset += ((state->guest * NVC0_CHANNELS_SHIFT) << 12);
        }
    }
    //if (state->log) {
        NVC0_PRINTF("write addr 0x%X => 0x%X\n", offset, value);
    //}
    nvc0_mmio_write32(real, offset, value);
}

uint32_t nvc0_vm_bar1_read(nvc0_state_t* state, target_phys_addr_t offset) {
    return nvc0_vm_read(state, state->bar[1].real, offset, nvc0_get_bar1_addr(state, offset));
}

void nvc0_vm_bar1_write(nvc0_state_t* state, target_phys_addr_t offset, uint32_t value) {
    nvc0_vm_write(state, state->bar[1].real, offset, nvc0_get_bar1_addr(state, offset), value);
}

uint32_t nvc0_vm_pramin_read(nvc0_state_t* state, target_phys_addr_t offset) {
    return nvc0_vm_read(state, state->bar[0].real + 0x700000, offset, nvc0_get_pramin_addr(state, offset));
}

void nvc0_vm_pramin_write(nvc0_state_t* state, target_phys_addr_t offset, uint32_t value) {
    nvc0_vm_write(state, state->bar[0].real + 0x700000, offset, nvc0_get_pramin_addr(state, offset), value);
}

void nvc0_vm_init(nvc0_state_t* state) {
}
/* vim: set sw=4 ts=4 et tw=80 : */
