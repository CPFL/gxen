/*
 * NVIDIA NVC0 MMIO BAR0 model
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
#include "nvc0_mmio_bar0.h"
#include "nvc0_context.h"
#include "nvc0_vbios.inc"
#include "nvc0_vm.h"
#include "nvc0_bit_mask.h"

// crystal freq is 27000KHz
#define GPU_CLOCKS_PER_NANO_SEC 27
#define GPU_CLOCKS_PER_SEC (GPU_CLOCKS_PER_NANO_SEC * 1000 * 1000)
static uint32_t timer_numerator = 0;
static uint32_t timer_denominator = 0;
static uint64_t timer_nano_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000000ULL) + ts.tv_nsec;
}
static uint64_t timer_now(void) {
    const uint64_t nano = timer_nano_sec();
    return nano * GPU_CLOCKS_PER_NANO_SEC * (timer_numerator + 1) / (timer_denominator + 1);
}

// http://nouveau.freedesktop.org/wiki/HwIntroduction
// BAR 0:
//   control registers. 16MB in size. Is divided into several areas for
//   each of the functional blocks of the card.
extern "C" void nvc0_init_bar0(nvc0_state_t* state) {
    void* ptr = malloc(0x2000000);
    memset(ptr, 0, 0x2000000);
    state->bar[0].space = static_cast<uint8_t*>(ptr);
    nvc0_mmio_write32(ptr, NV03_PMC_BOOT_0, NVC0_REG0);

    // map vbios
    NVC0_PRINTF("BIOS size ... %lu\n", sizeof(nvc0_vbios));
    memcpy(state->bar[0].space + NV_PROM_OFFSET, nvc0_vbios, sizeof(nvc0_vbios));

    // and initialization information from BIOS
    #include "nvc0_init.inc"
}

extern "C" uint32_t nvc0_mmio_bar0_readb(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;
    // return nvc0_mmio_read8(state->bar[0].space, offset);
    return nvc0_mmio_read8(state->bar[0].real, offset);
}

extern "C" uint32_t nvc0_mmio_bar0_readw(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;
    // return nvc0_mmio_read16(state->bar[0].space, offset);
    return nvc0_mmio_read16(state->bar[0].real, offset);
}

extern "C" void nvc0_mmio_bar0_writeb(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;
    // nvc0_mmio_write8(state->bar[0].space, offset, val);
    nvc0_mmio_write8(state->bar[0].real, offset, val);
}

extern "C" void nvc0_mmio_bar0_writew(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = nvc0_state(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;
    // nvc0_mmio_write16(state->bar[0].space, offset, val);
    nvc0_mmio_write16(state->bar[0].real, offset, val);
}

extern "C" uint32_t nvc0_mmio_bar0_readd(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = nvc0_state(opaque);
    nvc0::context* ctx = nvc0::context::extract(state);
    uint32_t ret = 0;
    const target_phys_addr_t offset = addr - state->bar[0].addr;

    switch (offset) {
    // PTIMER
    // Crystal freq is 27000KHz
    // We use CPU clock value instead of crystal of NVIDIA
    case NV04_PTIMER_TIME_0:  // 0x9400
        // low
        ret = (uint32_t)timer_now();
        goto end;
    case NV04_PTIMER_TIME_1:  // 0x9410
        // high
        ret = timer_now() >> 32;
        goto end;
    case NV04_PTIMER_NUMERATOR:  // 0x9200
        ret = timer_numerator;
        goto end;
    case NV04_PTIMER_DENOMINATOR:  // 0x9210
        ret = timer_denominator;
        goto end;

    // peephole
    // these are write port
    case 0x00155c:  // PEEPHOLE_W_CTRL
        break;
    case 0x060000:  // PEEPHOLE_W_ADDR
        break;
    case 0x060004:  // PEEPHOLE_W_DATA
        break;
    case 0x06000c:  // PEEPHOLE_RW_ADDR_HIGH
        break;
    }

    {
        const cross::command cmd = {
            cross::command::TYPE_READ,
            0xdeadface,
            offset,
            cross::command::BAR0
        };
        ret = ctx->send(cmd).value;
    }

end:
    NVC0_LOG(state, "read 0x%"PRIx64" => 0x%"PRIx64"\n", (uint64_t)offset, (uint64_t)ret);

    return ret;
}

extern "C" void nvc0_mmio_bar0_writed(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = nvc0_state(opaque);
    nvc0::context* ctx = nvc0::context::extract(state);
    const target_phys_addr_t offset = addr - state->bar[0].addr;

    NVC0_LOG(state, "write 0x%"PRIx64" <= 0x%"PRIx64"\n", (uint64_t)offset, (uint64_t)val);

    switch (offset) {
    case 0x00000000:
        switch (val) {
            case 0xDEADBEEF:
                state->log = 1;
                return;
            case 0xDEAFBEEF:
                state->log = 0;
                return;
            case 0xDEADFACE:
                NVC0_LOG(state, "DEADFACE\n");
                return;
        }
        break;

    case NV04_PTIMER_NUMERATOR:
        timer_numerator = val;
        NVC0_PRINTF("numerator set\n");
        return;

    case NV04_PTIMER_DENOMINATOR:
        timer_denominator = val;
        NVC0_PRINTF("denominator set\n");
        return;

    case 0x001704:
        NVC0_PRINTF("BAR1 ramin 0x%"PRIx64"\n", nvc0::bit_mask<30, uint64_t>(val) << 12);
        break;

    case 0x001714:
        NVC0_PRINTF("BAR3 ramin 0x%"PRIx64"\n", nvc0::bit_mask<30, uint64_t>(val) << 12);
        break;
    }

    const cross::command cmd = {
        cross::command::TYPE_WRITE,
        val,
        offset,
        cross::command::BAR0
    };
    ctx->send(cmd);
    return;
}
/* vim: set sw=4 ts=4 et tw=80 : */
