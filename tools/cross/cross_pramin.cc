/*
 * Cross NVC0 PRAMIN
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
#include "cross_pramin.h"
#include "cross_context.h"
#include "cross_device.h"
#include "cross_bit_mask.h"
// #include "nvc0_remapping.h"
namespace cross {

pramin_accessor::pramin_accessor()
    : lock_(cross::device::instance()->mutex_handle()) {
}

uint32_t pramin_accessor::read32(uint64_t addr) {
    change_current(addr);
//    remapping::page_entry entry;
//    if (ctx_->remapping()->lookup(addr, &entry) && entry.read_only) {
        // NVC0_PRINTF("handling 0x%" PRIX64 " access\n", addr);
        // TODO(Yusuke Suzuki)
        // memory separation
        // currently do nothing
//    }
    const uint32_t result = cross::device::instance()->read(0, 0x700000 + bit_mask<16>(addr));
    return result;
}

void pramin_accessor::write32(uint64_t addr, uint32_t val) {
    change_current(addr);
//    remapping::page_entry entry;
//    if (ctx_->remapping()->lookup(addr, &entry) && entry.read_only) {
        // NVC0_PRINTF("handling 0x%" PRIX64 " access\n", addr);
        // TODO(Yusuke Suzuki)
        // reconstruct page table entry
//        if (ctx_->bar1_table()->handle(addr)) {
//        }
//        if (ctx_->bar3_table()->handle(addr)) {
//        }
//    }
    cross::device::instance()->write(0, 0x700000 + bit_mask<16>(addr), val);
}

void pramin_accessor::change_current(uint64_t addr) {
    const uint64_t shifted = (addr >> 16);
    if (cross::device::instance()->pramin() != shifted) {
        cross::device::instance()->set_pramin(shifted);
        cross::device::instance()->write(0, 0x1700, shifted);
    }
}

}  // namespace nvc0
/* vim: set sw=4 ts=4 et tw=80 : */
