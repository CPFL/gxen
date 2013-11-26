/*
 * A3 BAR3 fake Channel
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
#include <utility>
#include "a3.h"
#include "context.h"
#include "device.h"
#include "device_bar3.h"
#include "software_page_table.h"
#include "barrier.h"
#include "pmem.h"
#include "page.h"
#include "bit_mask.h"
#include "mmio.h"
#include "bar3_channel.h"
namespace a3 {

bar3_channel_t::bar3_channel_t(context* ctx)
    : id_(-3)
    , enabled_(false)
    , ramin_address_()
    , page_directory_address_()
{
}

void bar3_channel_t::detach(context* ctx, uint64_t addr) {
    A3_LOG("detach from 0x%" PRIX64 " to 0x%" PRIX64 "\n", ramin_address(), addr);
    ctx->barrier()->unmap(ramin_address());
}

void bar3_channel_t::shadow(context* ctx) {
    if (ctx->para_virtualized()) {
        return;
    }

    pmem::accessor pmem;
    // and adjust address
    // page directory
    uint64_t page_directory_virt = mmio::read64(&pmem, ramin_address() + 0x0200);
    uint64_t page_directory_phys = ctx->get_phys_address(page_directory_virt);
    refresh_table(ctx, page_directory_phys);
}

void bar3_channel_t::refresh_table(context* ctx, uint64_t addr) {
    page_directory_address_ = addr;
    A3_SYNCHRONIZED(device::instance()->mutex()) {
        device::instance()->bar3()->refresh_table(ctx, addr);
    }
}

void bar3_channel_t::attach(context* ctx, uint64_t addr) {
    A3_LOG("attach to 0x%" PRIX64 "\n", ramin_address());
    shadow(ctx);
    ctx->barrier()->map(ramin_address());
}

void bar3_channel_t::refresh(context* ctx, uint64_t addr) {
    if (enabled()) {
        if (addr == ramin_address()) {
            // same channel ramin
            return;
        }
        detach(ctx, addr);
    }
    enabled_ = true;
    ramin_address_ = addr;
    attach(ctx, addr);
    return;
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
