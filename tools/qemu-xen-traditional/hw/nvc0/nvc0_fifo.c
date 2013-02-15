/*
 * NVIDIA NVC0 FIFO functions
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

#include <unistd.h>
#include "nvc0.h"
#include "nvc0_fifo.h"
#include "nvc0_pramin.h"
#include "nvc0_mmio.h"

void nvc0_fifo_playlist_update(nvc0_state_t* state, uint64_t vm_addr, uint32_t count) {
    // scan fifo and update values
    uint32_t i;
    NVC0_LOG("FIFO playlist update %u\n", count);
    for (i = 0; i < count; ++i) {
        const uint32_t cid = nvc0_pramin_read32(state, vm_addr + i * 0x8);
        NVC0_LOG("FIFO playlist cid %u => %u\n", cid, nvc0_channel_get_phys_id(state, cid));
        nvc0_pramin_write32(state, vm_addr + i * 0x8, nvc0_channel_get_phys_id(state, cid));
        nvc0_pramin_write32(state, vm_addr + i * 0x8 + 0x4, 0x4);
    }

    // FIXME(Yusuke Suzuki): BAR flush wait code is needed?
    nvc0_mmio_write32(state->bar[0].real, 0x70000, 1);
    usleep(1000);
}

/* vim: set sw=4 ts=4 et tw=80 : */
