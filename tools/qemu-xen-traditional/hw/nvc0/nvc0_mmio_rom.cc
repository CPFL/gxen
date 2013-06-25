/*
 * NVIDIA NVC0 MMIO ROM
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

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include "nvc0_inttypes.h"
#include "nvc0_mmio.h"
#include "nvc0_mmio_rom.h"
#include "nvc0_context.h"
#include "nvc0_vbios.inc"

extern "C" void nvc0_init_rom(nvc0_state_t* state) {
    void* ptr = malloc(512 * 1024);  // 512K
    state->bar[6].real = static_cast<uint8_t*>(ptr);
    memset(ptr, 0, 512 * 1024);
    // map vbios
    NVC0_PRINTF("BIOS size ... %lu\n", sizeof(nvc0_vbios));
    memcpy(state->bar[6].real, nvc0_vbios, sizeof(nvc0_vbios));
}

extern "C" uint32_t nvc0_mmio_rom_readb(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[6].addr;
    return nvc0_mmio_read8(state->bar[6].real, offset);
}

extern "C" uint32_t nvc0_mmio_rom_readw(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[6].addr;
    return nvc0_mmio_read16(state->bar[6].real, offset);
}

extern "C" uint32_t nvc0_mmio_rom_readd(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[6].addr;
    return nvc0_mmio_read32(state->bar[6].real, offset);
}

extern "C" void nvc0_mmio_rom_writeb(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[6].addr;
    nvc0_mmio_write8(state->bar[6].real, offset, val);
}

extern "C" void nvc0_mmio_rom_writew(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[6].addr;
    nvc0_mmio_write16(state->bar[6].real, offset, val);
}

extern "C" void nvc0_mmio_rom_writed(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[6].addr;
    nvc0_mmio_write32(state->bar[6].real, offset, val);
}

/* vim: set sw=4 ts=4 et tw=80 : */
