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
#include "nvc0_context.h"
namespace nvc0 {

void fifo_playlist_update(context* ctx, uint64_t address, uint32_t count) {
    // scan fifo and update values
    pramin_accessor pramin(ctx);
    uint32_t i;
    NVC0_LOG(ctx->state(), "FIFO playlist update %u\n", count);
    for (i = 0; i < count; ++i) {
        const uint32_t cid = pramin.read32(address + i * 0x8);
        NVC0_LOG(ctx->state(), "FIFO playlist cid %u => %u\n", cid, nvc0_channel_get_phys_id(ctx->state(), cid));
        pramin.write32(address + i * 0x8, nvc0_channel_get_phys_id(ctx->state(), cid));
        pramin.write32(address + i * 0x8 + 0x4, 0x4);
    }

    // FIXME(Yusuke Suzuki): BAR flush wait code is needed?
    nvc0_mmio_write32(ctx->state()->bar[0].real, 0x70000, 1);
    usleep(1000);
}

}
/* vim: set sw=4 ts=4 et tw=80 : */
