/*
 * NVIDIA NVC0 MMIO BAR3 model
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
#include "nvc0_mmio_bar3.h"

// BAR3 ramin bar
void nvc0_init_bar3(nvc0_state_t* state) {
    if (!(state->bar[3].space = qemu_mallocz(0x4000000))) {
        NVC0_PRINTF("BAR3 Initialization failed\n");
    }
}

uint32_t nvc0_mmio_bar3_readb(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[3].addr;
    NVC0_LOG("read 0x%llX\n", (uint64_t)offset);
    return nvc0_mmio_read8(state->bar[3].real, offset);
}

uint32_t nvc0_mmio_bar3_readw(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[3].addr;
    NVC0_LOG("read 0x%llX\n", (uint64_t)offset);
    return nvc0_mmio_read16(state->bar[3].real, offset);
}

uint32_t nvc0_mmio_bar3_readd(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[3].addr;
    return nvc0_vm_bar3_read(state, offset);
}

void nvc0_mmio_bar3_writeb(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[3].addr;
    NVC0_LOG("write 0x%llX <= 0x%llX\n", (uint64_t)offset, (uint64_t)val);
    nvc0_mmio_write8(state->bar[3].real, offset, val);
}

void nvc0_mmio_bar3_writew(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[3].addr;
    NVC0_LOG("write 0x%llX <= 0x%llX\n", (uint64_t)offset, (uint64_t)val);
    nvc0_mmio_write16(state->bar[3].real, offset, val);
}

void nvc0_mmio_bar3_writed(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[3].addr;
    nvc0_vm_bar3_write(state, offset, val);
}
/* vim: set sw=4 ts=4 et tw=80 : */
