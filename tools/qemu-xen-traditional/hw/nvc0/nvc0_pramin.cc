/*
 * NVIDIA NVC0 PRAMIN functions
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
#include "nvc0_pramin.h"
#include "nvc0_mmio.h"
#include "nvc0_context.h"
#include "nvc0_remapping.h"
#include "nvc0_bit_mask.h"
namespace nvc0 {

uint32_t pramin_read32(context* ctx, uint64_t addr) {
    pramin_accessor accessor(ctx);
    return accessor.read32(addr);
}

void pramin_write32(context* ctx, uint64_t addr, uint32_t val) {
    pramin_accessor accessor(ctx);
    accessor.write32(addr, val);
}

pramin_accessor::pramin_accessor(context* ctx)
    : ctx_(ctx)
    , old_(ctx->pramin()) {
}

pramin_accessor::~pramin_accessor() {
    if (ctx_->pramin() != old_) {
        ctx_->set_pramin(old_);
        nvc0_mmio_write32(ctx_->state()->bar[0].real, 0x1700, old_);
    }
}

uint32_t pramin_accessor::read32(uint64_t addr) {
    change_current(addr);
    remapping::page_entry entry;
    if (ctx_->remapping()->lookup(addr, &entry) && entry.read_only) {
        // NVC0_PRINTF("handling 0x%" PRIX64 " access\n", addr);
        // TODO(Yusuke Suzuki)
        // memory separation
        // currently do nothing
    }
    const uint32_t result = nvc0_mmio_read32(ctx_->state()->bar[0].real + 0x700000, bit_mask<16>(addr));
    return result;
}

void pramin_accessor::write32(uint64_t addr, uint32_t val) {
    change_current(addr);
    remapping::page_entry entry;
    if (ctx_->remapping()->lookup(addr, &entry) && entry.read_only) {
        // NVC0_PRINTF("handling 0x%" PRIX64 " access\n", addr);
        // TODO(Yusuke Suzuki)
        // reconstruct page table entry
    }
    nvc0_mmio_write32(ctx_->state()->bar[0].real + 0x700000, bit_mask<16>(addr), val);
}

void pramin_accessor::change_current(uint64_t addr) {
    const uint64_t shifted = (addr >> 16);
    if (ctx_->pramin() != shifted) {
        ctx_->set_pramin(shifted);
        nvc0_mmio_write32(ctx_->state()->bar[0].real, 0x1700, shifted);
    }
}

}  // namespace nvc0
/* vim: set sw=4 ts=4 et tw=80 : */
