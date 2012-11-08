/*
 *  QEMU model of the Milkymist High Performance Dynamic Memory Controller.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Specification available at:
 *   http://www.milkymist.org/socdoc/hpdmc.pdf
 */

#include "hw.h"
#include "sysbus.h"
#include "trace.h"
#include "qemu-error.h"

enum {
    R_SYSTEM = 0,
    R_BYPASS,
    R_TIMING,
    R_IODELAY,
    R_MAX
};

enum {
    IODELAY_DQSDELAY_RDY = (1<<5),
    IODELAY_PLL1_LOCKED  = (1<<6),
    IODELAY_PLL2_LOCKED  = (1<<7),
};

struct MilkymistHpdmcState {
    SysBusDevice busdev;
    MemoryRegion regs_region;

    uint32_t regs[R_MAX];
};
typedef struct MilkymistHpdmcState MilkymistHpdmcState;

static uint64_t hpdmc_read(void *opaque, target_phys_addr_t addr,
                           unsigned size)
{
    MilkymistHpdmcState *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_SYSTEM:
    case R_BYPASS:
    case R_TIMING:
    case R_IODELAY:
        r = s->regs[addr];
        break;

    default:
        error_report("milkymist_hpdmc: read access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }

    trace_milkymist_hpdmc_memory_read(addr << 2, r);

    return r;
}

static void hpdmc_write(void *opaque, target_phys_addr_t addr, uint64_t value,
                        unsigned size)
{
    MilkymistHpdmcState *s = opaque;

    trace_milkymist_hpdmc_memory_write(addr, value);

    addr >>= 2;
    switch (addr) {
    case R_SYSTEM:
    case R_BYPASS:
    case R_TIMING:
        s->regs[addr] = value;
        break;
    case R_IODELAY:
        /* ignore writes */
        break;

    default:
        error_report("milkymist_hpdmc: write access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }
}

static const MemoryRegionOps hpdmc_mmio_ops = {
    .read = hpdmc_read,
    .write = hpdmc_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void milkymist_hpdmc_reset(DeviceState *d)
{
    MilkymistHpdmcState *s = container_of(d, MilkymistHpdmcState, busdev.qdev);
    int i;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }

    /* defaults */
    s->regs[R_IODELAY] = IODELAY_DQSDELAY_RDY | IODELAY_PLL1_LOCKED
                         | IODELAY_PLL2_LOCKED;
}

static int milkymist_hpdmc_init(SysBusDevice *dev)
{
    MilkymistHpdmcState *s = FROM_SYSBUS(typeof(*s), dev);

    memory_region_init_io(&s->regs_region, &hpdmc_mmio_ops, s,
            "milkymist-hpdmc", R_MAX * 4);
    sysbus_init_mmio_region(dev, &s->regs_region);

    return 0;
}

static const VMStateDescription vmstate_milkymist_hpdmc = {
    .name = "milkymist-hpdmc",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, MilkymistHpdmcState, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo milkymist_hpdmc_info = {
    .init = milkymist_hpdmc_init,
    .qdev.name  = "milkymist-hpdmc",
    .qdev.size  = sizeof(MilkymistHpdmcState),
    .qdev.vmsd  = &vmstate_milkymist_hpdmc,
    .qdev.reset = milkymist_hpdmc_reset,
};

static void milkymist_hpdmc_register(void)
{
    sysbus_register_withprop(&milkymist_hpdmc_info);
}

device_init(milkymist_hpdmc_register)
