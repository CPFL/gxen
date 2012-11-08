/*
 * QEMU ISA VGA Emulator.
 *
 * Copyright (c) 2003 Fabrice Bellard
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
#include "hw.h"
#include "console.h"
#include "pc.h"
#include "vga_int.h"
#include "pixel_ops.h"
#include "qemu-timer.h"
#include "loader.h"

typedef struct ISAVGAState {
    ISADevice dev;
    struct VGACommonState state;
} ISAVGAState;

static void vga_reset_isa(DeviceState *dev)
{
    ISAVGAState *d = container_of(dev, ISAVGAState, dev.qdev);
    VGACommonState *s = &d->state;

    vga_common_reset(s);
}

static int vga_initfn(ISADevice *dev)
{
    ISAVGAState *d = DO_UPCAST(ISAVGAState, dev, dev);
    VGACommonState *s = &d->state;
    MemoryRegion *vga_io_memory;
    const MemoryRegionPortio *vga_ports, *vbe_ports;

    vga_common_init(s, VGA_RAM_SIZE);
    s->legacy_address_space = isa_address_space(dev);
    vga_io_memory = vga_init_io(s, &vga_ports, &vbe_ports);
    isa_register_portio_list(dev, 0x3b0, vga_ports, s, "vga");
    if (vbe_ports) {
        isa_register_portio_list(dev, 0x1ce, vbe_ports, s, "vbe");
    }
    memory_region_add_subregion_overlap(isa_address_space(dev),
                                        isa_mem_base + 0x000a0000,
                                        vga_io_memory, 1);
    memory_region_set_coalescing(vga_io_memory);
    s->ds = graphic_console_init(s->update, s->invalidate,
                                 s->screen_dump, s->text_update, s);

    vga_init_vbe(s, isa_address_space(dev));
    /* ROM BIOS */
    rom_add_vga(VGABIOS_FILENAME);
    return 0;
}

static ISADeviceInfo vga_info = {
    .qdev.name     = "isa-vga",
    .qdev.size     = sizeof(ISAVGAState),
    .qdev.vmsd     = &vmstate_vga_common,
    .qdev.reset     = vga_reset_isa,
    .init          = vga_initfn,
};

static void vga_register(void)
{
    isa_qdev_register(&vga_info);
}
device_init(vga_register)
