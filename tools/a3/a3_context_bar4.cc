/*
 * A3 Context BAR4
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
#include "a3.h"
#include "a3_context.h"
#include "a3_pmem.h"
#include "a3_software_page_table.h"
#include "a3_channel.h"
#include "a3_barrier.h"
namespace a3 {

void context::write_bar4(const command& cmd) {
    switch (cmd.offset) {
    case 0x000000:
        break;

    case 0x000004:
        pv32(cmd.offset) = cmd.value;
        break;

    case 0x000008:
        pv32(cmd.offset) = cmd.value;
        break;
    }
}

void context::read_bar4(const command& cmd) {
    switch (cmd.offset) {
    case 0x000000: {
            const uint64_t lower = pv32(0x4);
            const uint64_t upper = pv32(0x8);
            const uint64_t gp = lower | (upper << 32);
            A3_LOG("Guest physical call data address %" PRIx64 "\n", gp);
            if (guest_) {
                munmap(guest_, A3_GUEST_DATA_SIZE);
                guest_ = NULL;
            }
            A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
                guest_ = reinterpret_cast<uint8_t*>(a3_xen_map_foreign_range(device::instance()->xl_ctx(), domid(), A3_GUEST_DATA_SIZE, PROT_READ | PROT_WRITE, gp >> 12));
            }
            // A3_LOG("Guest physical call data cookie %" PRIx32 "\n", reinterpret_cast<uint32_t*>(guest_)[0]);
        }
        break;
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
