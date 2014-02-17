/*
 * NVIDIA NVC0 API PV BAR5 model
 *
 * This is Control Area for Para-virtualized GPU
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
#include <cstdint>
#include <cinttypes>
#include "nvc0_vm.h"
#include "nvc0_mmio.h"
#include "nvc0_context.h"
#include "nvc0_api_bar5.h"

extern "C" uint32_t nvc0_api_mmio_bar5_readb(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[4].addr;
    NVC0_PRINTF("read 0x%"PRIx64"\n", (uint64_t)offset);
    return 0;
}

extern "C" uint32_t nvc0_api_mmio_bar5_readw(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[4].addr;
    NVC0_PRINTF("read 0x%"PRIx64"\n", (uint64_t)offset);
    return 0;
}

extern "C" uint32_t nvc0_api_mmio_bar5_readd(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[4].addr;
    return 0;
}

extern "C" void nvc0_api_mmio_bar5_writeb(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[4].addr;
    NVC0_PRINTF("write 0x%"PRIx64" <= 0x%"PRIx64"\n", (uint64_t)offset, (uint64_t)val);
}

extern "C" void nvc0_api_mmio_bar5_writew(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[4].addr;
    NVC0_PRINTF("write 0x%"PRIx64" <= 0x%"PRIx64"\n", (uint64_t)offset, (uint64_t)val);
}

extern "C" void nvc0_api_mmio_bar5_writed(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[4].addr;
}
