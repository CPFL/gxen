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
#include <cstdio>
#include <cinttypes>
#include "a3.h"
#include "context.h"
#include "barrier.h"
#include "bit_mask.h"
#include "device.h"
#include "pmem.h"
#include "page.h"
#include "ignore_unused_variable_warning.h"
namespace a3 {

void context::write_barrier(uint64_t addr, const command& cmd) {
    const uint64_t page = bit_clear<barrier::kPAGE_BITS>(addr);
    const uint64_t rest = addr - page;
    A3_LOG("write barrier 0x%" PRIX64 " : page 0x%" PRIX64 " <= 0x%" PRIX32 "\n", addr, page, cmd.value);

    // TODO(Yusuke Suzuki): check values
    // TODO(Yusuke Suzuki): BAR1 & BAR3 shadow sync
    typedef context::channel_map::iterator iter_t;
    const std::pair<iter_t, iter_t> range = ramin_channel_map()->equal_range(page);
    A3_SYNCHRONIZED(device()->mutex()) {
        for (iter_t it = range.first; it != range.second; ++it) {
            A3_LOG("write reflect shadow 0x%" PRIX64 " : rest 0x%" PRIX64 "\n", it->second->shadow_ramin()->address(), rest);
            if (cmd.value) {
                if (a3::flags::lazy_shadowing) {
                    it->second->flush(this);
                }
            }
            it->second->shadow_ramin()->write(rest, cmd.value, cmd.size());
        }
    }

    // BAR3
    if (page == bar3_channel()->ramin_address()) {
        A3_LOG("write reflect shadow BAR3 : rest 0x%" PRIX64 "\n", rest);
        bar3_channel()->shadow(this);
    }

    // BAR1
    if (page == bar1_channel()->ramin_address()) {
        A3_LOG("write reflect shadow BAR1 : rest 0x%" PRIX64 "\n", rest);
        bar1_channel()->shadow(this);
    }

//    switch (offset) {
//    case 0x0200: {
//            // lower 32bit
//            pmem::accessor pmem;
//            pmem.write32(addr, value + bit_mask<32>(get_address_shift()));
//            break;
//        }
//    case 0x0204:  // upper 32bit
//        break;
//    }
}

void context::read_barrier(uint64_t addr, const command& cmd) {
    const uint64_t page = bit_clear<barrier::kPAGE_BITS>(addr);
    ignore_unused_variable_warning(page);
    // const uint64_t offset = bit_mask<barrier::kPAGE_BITS>(addr);
    A3_LOG("read barrier 0x%" PRIX64 " : page 0x%" PRIX64 "\n", addr, page);
//    switch (offset) {
//    case 0x0200: {
//            // lower 32bit
//            pmem::accessor pmem;
//            pmem.read32(addr, value + bit_mask<32>(get_address_shift()));
//            break;
//        }
//    case 0x0204:  // upper 32bit
//        break;
//    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
