/*
 * A3 Context BAR3
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
#include <cstdint>
#include "a3.h"
#include "context.h"
#include "pmem.h"
#include "software_page_table.h"
#include "channel.h"
#include "barrier.h"
#include "device_bar3.h"
namespace a3 {

void context::write_bar3(const command& cmd) {
    const uint64_t gphys = device()->bar3()->resolve(this, cmd.offset, nullptr);
    if (gphys != UINT64_MAX) {
        pmem::accessor pmem;
        pmem.write(gphys, cmd.value, cmd.size());
        barrier::page_entry* entry = nullptr;
        if (barrier()->lookup(gphys, &entry, false)) {
            // found
            write_barrier(gphys, cmd);
        }
        return;
    }
    A3_LOG("VM BAR3 invalid write 0x%" PRIX32 " access\n", cmd.offset);
}

void context::read_bar3(const command& cmd) {
    const uint64_t gphys = device()->bar3()->resolve(this, cmd.offset, nullptr);
    if (gphys != UINT64_MAX) {
        pmem::accessor pmem;
        const uint32_t ret = pmem.read(gphys, cmd.size());
        buffer()->value = ret;
        barrier::page_entry* entry = nullptr;
        if (barrier()->lookup(gphys, &entry, false)) {
            // found
            read_barrier(gphys, cmd);
        }
        return;
    }

    A3_LOG("VM BAR3 invalid read 0x%" PRIX32 " access\n", cmd.offset);
    buffer()->value = 0xFFFFFFFF;
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
