/*
 * Cross Context BAR1
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
#include "cross.h"
#include "cross_context.h"
#include "cross_pramin.h"
#include "cross_shadow_page_table.h"
#include "cross_barrier.h"
namespace cross {

void context::write_bar1(const command& cmd) {
    if (in_poll_area(cmd.offset)) {
        CROSS_SYNCHRONIZED(device::instance()->mutex_handle()) {
            const uint64_t offset = cmd.offset - poll_area();
            device::instance()->write(1, offset, cmd.value);
        }
        return;
    }

    const uint64_t gphys = bar1_channel()->table()->resolve(cmd.offset, NULL);
    if (gphys != UINT64_MAX) {
        barrier::page_entry* entry = NULL;
        if (barrier()->lookup(gphys, &entry, false)) {
            // found
            read_barrier(gphys);
        }
        pramin::accessor pramin;
        pramin.write32(gphys, cmd.value);
        return;
    }
    CROSS_LOG("VM BAR1 invalid write 0x%" PRIX32 " access\n", cmd.offset);
}

void context::read_bar1(const command& cmd) {
    if (in_poll_area(cmd.offset)) {
        CROSS_SYNCHRONIZED(device::instance()->mutex_handle()) {
            const uint64_t offset = cmd.offset - poll_area();
            buffer()->value = device::instance()->read(1, offset);
        }
        return;
    }

    const uint64_t gphys = bar1_channel()->table()->resolve(cmd.offset, NULL);
    if (gphys != UINT64_MAX) {
        barrier::page_entry* entry = NULL;
        if (barrier()->lookup(gphys, &entry, false)) {
            // found
            read_barrier(gphys);
        }
        pramin::accessor pramin;
        const uint32_t ret = pramin.read32(gphys);
        buffer()->value = ret;
        return;
    }

    CROSS_LOG("VM BAR1 invalid read 0x%" PRIX32 " access\n", cmd.offset);
    buffer()->value = 0xFFFFFFFF;
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
