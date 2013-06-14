/*
 * A3 Fake Channel
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
#include <utility>
#include "a3.h"
#include "a3_context.h"
#include "a3_fake_channel.h"
#include "a3_software_page_table.h"
#include "a3_barrier.h"
#include "a3_pmem.h"
#include "a3_inttypes.h"
#include "a3_page.h"
#include "a3_bit_mask.h"
namespace a3 {

template<typename T>
static uint64_t read64(T* pmem, uint64_t addr) {
    const uint32_t lower = pmem->read32(addr);
    const uint32_t upper = pmem->read32(addr + 0x4);
    return lower | (static_cast<uint64_t>(upper) << 32);
}

template<typename T>
static void write64(T* pmem, uint64_t addr, uint64_t value) {
    pmem->write32(addr, bit_mask<32>(value));
    pmem->write32(addr + 0x4, value >> 32);
}

fake_channel::fake_channel(int id)
    : id_(id)
    , enabled_(false)
    , ramin_address_()
    , table_(new software_page_table(id)) {
}

fake_channel::~fake_channel() {
}

void fake_channel::detach(context* ctx, uint64_t addr) {
    A3_LOG("detach from 0x%" PRIX64 " to 0x%" PRIX64 "\n", ramin_address(), addr);
    ctx->barrier()->unmap(ramin_address());
}

void fake_channel::shadow(context* ctx) {
    uint64_t page_directory_virt = 0;
    uint64_t page_directory_phys = 0;
    uint64_t page_directory_size = 0;
    pmem::accessor pmem;
    // and adjust address
    // page directory
    page_directory_virt = read64(&pmem, ramin_address() + 0x0200);
    page_directory_phys = ctx->get_phys_address(page_directory_virt);
    page_directory_size = read64(&pmem, ramin_address() + 0x0208);
    table()->refresh(ctx, page_directory_phys, page_directory_size);
}

void fake_channel::attach(context* ctx, uint64_t addr) {
    shadow(ctx);
    ctx->barrier()->map(ramin_address());
}

void fake_channel::refresh(context* ctx, uint64_t addr) {
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
