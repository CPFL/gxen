/*
 * NVIDIA NVC0 poll area controller
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
#include "nvc0.h"
#include "nvc0_inttypes.h"
#include "nvc0_context.h"
#include "nvc0_poll_area.h"
#include "nvc0_bit_mask.h"
#include "nvc0_mmio.h"
namespace nvc0 {

poll_area::poll_area()
    : offset_(0) {
}


void poll_area::set_offset(context* ctx, uint64_t offset) {
    offset_ = offset;

    NVC0_PRINTF("POLL_AREA 0x%" PRIX64 "\n", offset);

    // TODO(Yusuke Suzuki)
    // virtualized BAR1 is required
    nvc0_mmio_write32(ctx->state()->bar[0].real, 0x2254, 0x10000000 | bit_mask<28>(offset >> 12));
}

}  // namespace nvc0
/* vim: set sw=4 ts=4 et tw=80 : */
