/*
 * Cross Context barrier
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
#include "cross.h"
#include "cross_context.h"
#include "cross_barrier.h"
#include "cross_inttypes.h"
#include "cross_bit_mask.h"
#include <cstdio>
namespace cross {

void context::write_barrier(uint64_t addr, const command& cmd) {
    const uint64_t page = bit_clear<barrier::kPAGE_BITS>(addr);
    // const uint64_t offset = bit_mask<barrier::kPAGE_BITS>(addr);
    CROSS_LOG("write barrier 0x%" PRIX64 " : page 0x%" PRIX64 " <= 0x%" PRIX32 "\n", addr, page, cmd.value);
//    switch (offset) {
//    case 0x0200: {
//            // lower 32bit
//            pramin::accessor pramin;
//            pramin.write32(addr, value + bit_mask<32>(get_address_shift()));
//            break;
//        }
//    case 0x0204:  // upper 32bit
//        break;
//    }
}

void context::read_barrier(uint64_t addr, const command& cmd) {
    const uint64_t page = bit_clear<barrier::kPAGE_BITS>(addr);
    // const uint64_t offset = bit_mask<barrier::kPAGE_BITS>(addr);
    CROSS_LOG("read barrier 0x%" PRIX64 " : page 0x%" PRIX64 "\n", addr, page);
//    switch (offset) {
//    case 0x0200: {
//            // lower 32bit
//            pramin::accessor pramin;
//            pramin.read32(addr, value + bit_mask<32>(get_address_shift()));
//            break;
//        }
//    case 0x0204:  // upper 32bit
//        break;
//    }
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
