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

#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include "nvc0_mmio.h"
#include "nvc0_mmio_bar0.h"
#include "nvc0_channel.h"
#include "nvc0_vbios.inc"
#include "nvc0_vm.h"

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
void nvc0_init_bar0(nvc0_state_t* state) {
    void* ptr = qemu_mallocz(0x2000000);
    state->bar[0].space = ptr;
    nvc0_mmio_write32(ptr, NV03_PMC_BOOT_0, NVC0_REG0);

    // map vbios
    NVC0_PRINTF("BIOS size ... %lu\n", sizeof(nvc0_vbios));
    memcpy(state->bar[0].space + NV_PROM_OFFSET, nvc0_vbios, sizeof(nvc0_vbios));

    // and initialization information from BIOS
    #include "nvc0_init.inc"
}

uint32_t nvc0_mmio_bar0_readb(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;
    // return nvc0_mmio_read8(state->bar[0].space, offset);
    return nvc0_mmio_read8(state->bar[0].real, offset);
}

uint32_t nvc0_mmio_bar0_readw(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;
    // return nvc0_mmio_read16(state->bar[0].space, offset);
    return nvc0_mmio_read16(state->bar[0].real, offset);
}

void nvc0_mmio_bar0_writeb(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;
    // nvc0_mmio_write8(state->bar[0].space, offset, val);
    nvc0_mmio_write8(state->bar[0].real, offset, val);
}

void nvc0_mmio_bar0_writew(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;
    // nvc0_mmio_write16(state->bar[0].space, offset, val);
    nvc0_mmio_write16(state->bar[0].real, offset, val);
}

uint32_t nvc0_mmio_bar0_readd(void *opaque, target_phys_addr_t addr) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;

    if (state->log) {
        NVC0_PRINTF("read 0x%X\n", offset);
    }

    switch (offset) {
    case NV50_PMC_BOOT_0:  // 0x00000000
        // return QUADRO6000_REG0;
        // for debugging, we access to real device
        break;

    // see nvc0_vram.c
    // Part struct starts with 0x11020c (PBFBs)
    case 0x022438:  // parts
        // Quadro6000 has 6 parts
        // return 0x00000006;
        // NVC0_PRINTF("MMIO bar0 parts 0x%X : 0x%X\n", addr, 1);
        // return 0x00000001;  // Expose only 1 parts by Quadro6000 device model
        break;

    case 0x022554:  // pmask
        // NVC0_PRINTF("MMIO bar0 pmask 0x%X : 0x%X\n", addr, 0);
        // return 0x00000000;
        break;

    case 0x10f20c:  // bsize
        // Quadro6000 has 0x00000400 size vram per part
        // Actually, because of << 20, VRAM part size is 1GB
        // NVC0_PRINTF("MMIO bar0 bsize 0x%X : 0x%X\n", addr, 0x400);
        // return 0x00000400;
        break;

    case 0x100800:  //  ?
        // return 0x00000006;
        break;

    case (0x11020c + 0 * 0x1000):  // part 0 size
        // return 0x00000400;
        break;

    case 0x409604:
        // gpc_nr TODO(Yusuke Suzuki) fix upper bits
        // gpc_nr(0x4) & rop_nr(0x6)
        // return 0x00060004;
        break;

    // PTIMER
    // Crystal freq is 27000KHz
    // We use CPU clock value instead of crystal of NVIDIA
    case NV04_PTIMER_TIME_0:  // 0x9400
        // low
        return (uint32_t)timer_now();
    case NV04_PTIMER_TIME_1:  // 0x9410
        // high
        return timer_now() >> 32;
    case NV04_PTIMER_NUMERATOR:  // 0x9200
        return timer_numerator;
    case NV04_PTIMER_DENOMINATOR:  // 0x9210
        return timer_denominator;

    // nvc0_graph.c
    case 0x40910c:  // write 0
        break;
    case 0x409100:  // write 2
        break;
    case 0x409800:  // nv_wait
        // used in nvc0_graph.c nvc0_graph_init_ctxctl
        // HUB init
        //
        // FIXME(Yusuke Suzuki) we should store valid context value...
        // return 0x80000001;
        break;

    // nvc0_vm.c
    case 0x100cb8:
        break;
    case 0x100cbc:
        break;
    case 0x100c80:
        // used in nvc0_vm.c nvc0_vm_flush
        // return 0x00ff8000;
        break;

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

    // tp
    case GPC_UNIT(0, 0x2608):
        // return 0x3;
        break;
    case GPC_UNIT(1, 0x2608):
        // return 0x4;
        break;
    case GPC_UNIT(2, 0x2608):
        // return 0x3;
        break;
    case GPC_UNIT(3, 0x2608):
        // return 0x4;
        break;

    // VRAM base address
    case 0x001700:
        return nvc0_mmio_read32(state->bar[0].real, offset);

    // PFIFO
    // we should shift channel id
    case 0x002634:  // channel kill
        return nvc0_channel_get_virt_id(state, nvc0_mmio_read32(state->bar[0].real, offset));
    }

    if (0x700000 <= offset && offset <= 0x7fffff) {
        return nvc0_vm_pramin_read(state, offset - 0x700000);
    }

    // PFIFO
    if (0x002000 <= offset && offset <= 0x004000) {
        // 0x003000 + id * 8
        // see pscnv/nvc0_fifo.c
        if ((offset - 0x003000) <= NVC0_CHANNELS * 8) {
            // channel status access
            // we should shift access target by guest VM
            const uint32_t virt = (offset - 0x003000) >> 3;
            if (virt >= NVC0_CHANNELS_SHIFT) {
                // these channels cannot be used
                if (virt & 0x4) {
                    // status
                } else {
                    // others
                }
                // FIXME(Yusuke Suzuki)
                // return better value
                return 0;
            } else {
                const uint32_t phys = nvc0_channel_get_phys_id(state, virt);
                const uint32_t adjusted = (offset - virt * 8) + (phys * 8);
                if (state->log) {
                    NVC0_PRINTF("0x%X adjusted to => 0x%X\n", offset, adjusted);
                }
                return nvc0_mmio_read32(state->bar[0].real, adjusted);
            }
        } else if (offset == 0x002254) {
            // see nvc0_fifo.c
        }
    } else if (0x800000 <= offset) {
        // PFIFO channel table
        if (state->log) {
            NVC0_PRINTF("channel table access 0x%X => 0x%X\n", offset, nvc0_mmio_read32(state->bar[0].real, offset));
        }
    }

    // return nvc0_mmio_read32(state->bar[0].space, offset);
    return nvc0_mmio_read32(state->bar[0].real, offset);
}

void nvc0_mmio_bar0_writed(void *opaque, target_phys_addr_t addr, uint32_t val) {
    nvc0_state_t* state = (nvc0_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;

    if (state->log) {
        NVC0_PRINTF("write 0x%X <= 0x%X\n", offset, val);
    }

    switch (offset) {
    case 0x00000000:
        NVC0_PRINTF("write call 0x%X\n", val);
        if (val == 0xDEADBEEF) {
            state->log = 1;
        } else if (val == 0xDEAFBEEF) {
            state->log = 0;
        }
        // state->log = val;
        break;

    case NV04_PTIMER_NUMERATOR:
        timer_numerator = val;
        NVC0_PRINTF("numerator set\n");
        return;

    case NV04_PTIMER_DENOMINATOR:
        timer_denominator = val;
        NVC0_PRINTF("denominator set\n");
        return;

    // VRAM base address
    case 0x001700:
        // 0x1700 (NV50) PMC_BAR0_PRAMIN
        //
        // Physical VRAM address of window that PRAMIN points to, shifted right by 16 bits.
        state->vm_engine.pramin = ((nvc0_vm_addr_t)val) << 16;
        nvc0_mmio_write32(state->bar[0].real, offset, val);
        NVC0_PRINTF("PRAMIN base addr set 0x%llX\n", (uint64_t)state->vm_engine.pramin);
        return;

    case 0x001704: {
            // BAR1 base
            const nvc0_vm_addr_t bar1_shifted = (val & (0x80000000 - 1));
            state->vm_engine.bar1 = bar1_shifted << 12;  // offset
            nvc0_mmio_write32(state->bar[0].real, offset, val);
            NVC0_PRINTF("BAR1 base addr set 0x%llX\n", (uint64_t)state->vm_engine.bar1);
            return;
        }
    }

    // PRAMIN
    if (0x700000 <= offset && offset <= 0x7fffff) {
        nvc0_vm_pramin_write(state, offset - 0x700000, val);
        return;
    }

    // PFIFO
    if (0x002000 <= offset && offset <= 0x004000) {
        // 0x003000 + id * 8
        // see pscnv/nvc0_fifo.c
        if ((offset - 0x003000) <= NVC0_CHANNELS * 8) {
            // channel status access
            // we should shift access target by guest VM
            const uint32_t virt = (offset - 0x003000) >> 3;
            if (virt >= NVC0_CHANNELS_SHIFT) {
                // these channels cannot be used
                if (virt & 0x4) {
                    // status
                } else {
                    // others
                }
                // FIXME(Yusuke Suzuki)
                // write better value
            } else {
                const uint32_t phys = nvc0_channel_get_phys_id(state, virt);
                const uint32_t adjusted = (offset - virt * 8) + (phys * 8);
                nvc0_mmio_write32(state->bar[0].real, adjusted, val);
            }
            return;
        } else if (offset == 0x002634) {
            // kill
            if (val >= NVC0_CHANNELS_SHIFT) {
                return;
            }
            const uint32_t id = nvc0_channel_get_phys_id(state, val);
            nvc0_mmio_write32(state->bar[0].real, offset, id);
            return;
        } else if (offset == 0x002254) {
            // see nvc0_fifo.c
            if (val >= 0x10000000) {
                // FIXME we should scan values...
                state->pfifo.user_vma = (val & (0x10000000 - 1));  // offset
                state->pfifo.user_vma_enabled = 1;
                NVC0_PRINTF("user_vma... 0x%X\n", state->pfifo.user_vma);
            }
        }
    } else if (0x800000 <= offset) {
        // PFIFO channel table
        if (state->log) {
            NVC0_PRINTF("channel table access 0x%X <= 0x%X\n", offset, val);
        }
    }

    // fallback
    // nvc0_mmio_write32(state->bar[0].space, offset, val);
    nvc0_mmio_write32(state->bar[0].real, offset, val);
}
/* vim: set sw=4 ts=4 et tw=80 : */
