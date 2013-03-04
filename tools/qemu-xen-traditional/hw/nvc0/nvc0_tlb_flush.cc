/*
 * NVIDIA NVC0 TLB flush
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

#include <stdint.h>
#include "nvc0_inttypes.h"
#include "nvc0_mmio.h"
#include "nvc0_pramin.h"
#include "nvc0_bit_mask.h"
#include "nvc0_context.h"
#include "nvc0_tlb_flush.h"
namespace nvc0 {

void tlb_flush::trigger(context* ctx, uint32_t val) {
    trigger_ = val;
    nvc0_mmio_write32(ctx->state()->bar[0].real, 0x100cb8, vspace());
    nvc0_mmio_write32(ctx->state()->bar[0].real, 0x100cbc, trigger());
    const uint64_t page_directory = bit_mask<28, uint64_t>(vspace() >> 4) << 12;
    NVC0_PRINTF("page directory 0x%" PRIX64 " is flushed\n", page_directory);

    // rescan page tables
    if (ctx->bar1_table()->page_directory_address() == page_directory) {
        // BAR1
        ctx->bar1_table()->refresh_page_directories(ctx, page_directory);
    }

    if (ctx->bar3_table()->page_directory_address() == page_directory) {
        // BAR3
        ctx->bar3_table()->refresh_page_directories(ctx, page_directory);
    }
}

}  // namespace nvc0
/* vim: set sw=4 ts=4 et tw=80 : */
