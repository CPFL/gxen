/*
 * NVIDIA NVC0 ioport model
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
#include "nvc0_ioport.h"
#include "pass-through.h"

static uint32_t nvc0_ioport_readb(void *opaque, uint32_t addr) {
    return 0;
}

static void nvc0_ioport_writeb(void *opaque, uint32_t addr, uint32_t val) {
}

static void nvc0_ioport_map(PCIDevice *dev, int region_num, uint32_t addr, uint32_t size, int type) {
    int ret;
    nvc0_state_t* state = nvc0_state(dev);

    nvc0_bar_t* bar = &(state)->bar[region_num];
    bar->io_index = -1;
    bar->addr = addr;
    bar->size = size;
    bar->type = type;

    register_ioport_write(addr, size, 1, nvc0_ioport_writeb, dev);
    register_ioport_read(addr, size, 1, nvc0_ioport_readb, dev);
}

void nvc0_ioport_init(nvc0_state_t* state) {
    // Region 5: I/O ports at ec80 [disabled] [size=128]
    pci_register_io_region(&state->device->dev, 5, 0x0000080, PCI_ADDRESS_SPACE_IO, nvc0_ioport_map);
}

/* vim: set sw=4 ts=4 et tw=80 : */
