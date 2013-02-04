/*
 * NVIDIA NVC0 MMIO BAR1 model
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

#include "nvc0_mmio.h"
#include "nvc0_mmio_bar1.h"

// BAR 1:
//   VRAM. On pre-NV50, corresponds directly to the available VRAM on card.
//   On NV50, gets remapped through VM engine.
void nvc0_init_bar1(nvc0_state_t* state) {
    if (!(state->bar[1].space = qemu_mallocz(0x8000000))) {
        NVC0_PRINTF("BAR1 Initialization failed\n");
    }
}

uint32_t nvc0_mmio_bar1_readb(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    target_phys_addr_t offset = addr - state->bar[1].addr;
    if (state->log) {
        NVC0_PRINTF("read 0x%X\n", offset);
    }
    return nvc0_mmio_read8(state->bar[1].real, offset);
}

uint32_t nvc0_mmio_bar1_readw(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    target_phys_addr_t offset = addr - state->bar[1].addr;
    if (state->log) {
        NVC0_PRINTF("read 0x%X\n", offset);
    }
    return nvc0_mmio_read16(state->bar[1].real, offset);
}

uint32_t nvc0_mmio_bar1_readd(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    target_phys_addr_t offset = addr - state->bar[1].addr;
    // tracking user_vma
    if (state->pfifo.user_vma_enabled) {
        const nvc0_vm_addr_t vm_addr = offset;
        NVC0_PRINTF("user_vma enabled!... 0x%x and 0x%x\n", state->pfifo.user_vma, offset);
        if (state->pfifo.user_vma <= vm_addr &&
                vm_addr < (NVC0_USER_VMA_CHANNEL * NVC0_CHANNELS + state->pfifo.user_vma)) {
            NVC0_PRINTF("offset shift ... 0x%X to 0x%X\n", vm_addr, vm_addr + ((state->guest * NVC0_CHANNELS_SHIFT) << 12));
            // TODO(Yusuke Suzuki) check window overflow
            offset += ((state->guest * NVC0_CHANNELS_SHIFT) << 12);
        }
    }
    const uint32_t result = nvc0_mmio_read32(state->bar[1].real, offset);
    //if (state->log) {
        NVC0_PRINTF("read addr 0x%X => 0x%X\n", offset, result);
    //}
    return result;
}

void nvc0_mmio_bar1_writeb(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    target_phys_addr_t offset = addr - state->bar[1].addr;
    if (state->log) {
        NVC0_PRINTF("write 0x%X <= 0x%X\n", offset, val);
    }
    nvc0_mmio_write8(state->bar[1].real, offset, val);
}

void nvc0_mmio_bar1_writew(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    target_phys_addr_t offset = addr - state->bar[1].addr;
    if (state->log) {
        NVC0_PRINTF("write 0x%X <= 0x%X\n", offset, val);
    }
    nvc0_mmio_write16(state->bar[1].real, offset, val);
}

void nvc0_mmio_bar1_writed(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    target_phys_addr_t offset = addr - state->bar[1].addr;
    // tracking user_vma
    if (state->pfifo.user_vma_enabled) {
        const nvc0_vm_addr_t vm_addr = offset;
        NVC0_PRINTF("user_vma enabled!... 0x%x and 0x%x\n", state->pfifo.user_vma, offset);
        if (state->pfifo.user_vma <= vm_addr &&
                vm_addr < (NVC0_USER_VMA_CHANNEL * NVC0_CHANNELS + state->pfifo.user_vma)) {
            NVC0_PRINTF("offset shift ... 0x%X to 0x%X\n", vm_addr, vm_addr + ((state->guest * NVC0_CHANNELS_SHIFT) << 12));
            // TODO(Yusuke Suzuki) check window overflow
            offset += ((state->guest * NVC0_CHANNELS_SHIFT) << 12);
        }
    }
    //if (state->log) {
        NVC0_PRINTF("write addr 0x%X => 0x%X\n", offset, val);
    //}
    nvc0_mmio_write32(state->bar[1].real, offset, val);
    const uint32_t result = nvc0_mmio_read32(state->bar[1].real, offset);
    NVC0_PRINTF("checking 0x%X\n", result);
}
/* vim: set sw=4 ts=4 et tw=80 : */
