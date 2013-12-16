/*
 * A3 Channel
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
#include "channel.h"
#include "shadow_page_table.h"
#include "barrier.h"
#include "pmem.h"
#include "registers.h"
#include "page.h"
#include "pv_page.h"
#include "bit_mask.h"
#include "device_bar3.h"
#include "mmio.h"
#include "timer.h"
#include "ignore_unused_variable_warning.h"
namespace a3 {

channel::channel(int id)
    : id_(id)
    , enabled_(false)
    , tlb_flush_needed_(false)
    , ramin_address_()
    , shared_address_()
    , table_(new shadow_page_table(id))
    , shadow_ramin_(new page(1))
    , original_(A3_DOMAIN_CHANNELS)
    , derived_(&original_)
{
}

bool channel::detach(context* ctx, uint64_t addr) {
    A3_LOG("detach from 0x%" PRIX64 " to 0x%" PRIX64 "\n", ramin_address(), addr);
    const bool old_exists = ctx->barrier()->unmap(ramin_address());

    typedef context::channel_map::iterator iter_t;
    const std::pair<iter_t, iter_t> range = ctx->ramin_channel_map()->equal_range(addr);
    for (iter_t it = range.first; it != range.second; ++it) {
        if (it->second == this) {
            ctx->ramin_channel_map()->erase(it);
            break;
        }
    }

    return old_exists;
}

void channel::shadow(context* ctx) {
    uint64_t page_directory_virt = 0;
    uint64_t page_directory_phys = 0;
    uint64_t page_directory_size = 0;

    pmem::accessor pmem;

    // shadow ramin
    for (uint64_t offset = 0; offset < 0x1000; offset += 0x4) {
        const uint32_t value = pmem.read32(ramin_address() + offset);
        shadow_ramin()->write32(offset, value);
    }

    // and adjust address
    // page directory

    if (!ctx->para_virtualized()) {
        page_directory_virt = mmio::read64(&pmem, ramin_address() + 0x0200);
        page_directory_phys = ctx->get_phys_address(page_directory_virt);
        page_directory_size = mmio::read64(&pmem, ramin_address() + 0x0208);
        mmio::write64(shadow_ramin(), 0x0200, page_directory_phys);
        mmio::write64(shadow_ramin(), 0x0208, page_directory_size);

        A3_LOG("id %d virt 0x%" PRIX64 " phys 0x%" PRIX64 " size %" PRIu64 "\n", id(), page_directory_virt, page_directory_phys, page_directory_size);
    }

    // fctx
    const uint64_t fctx_virt = mmio::read64(&pmem, ramin_address() + 0x08);
    const uint64_t fctx_phys = ctx->get_phys_address(fctx_virt);
    mmio::write64(shadow_ramin(), 0x08, fctx_phys);

    // mpeg ctx
    const uint64_t mpeg_ctx_limit_virt = pmem.read32(ramin_address() + 0x60 + 0x04);
    const uint64_t mpeg_ctx_limit_phys = ctx->get_phys_address(mpeg_ctx_limit_virt);
    shadow_ramin()->write32(0x60 + 0x04, mpeg_ctx_limit_phys);

    const uint64_t mpeg_ctx_virt = pmem.read32(ramin_address() + 0x60 + 0x08);
    const uint64_t mpeg_ctx_phys = ctx->get_phys_address(mpeg_ctx_virt);
    shadow_ramin()->write32(0x60 + 0x08, mpeg_ctx_phys);

    // TODO(Yusuke Suzuki):
    // optimize it. only mark it is OK or NG
    if (!ctx->para_virtualized()) {
        table()->refresh(ctx, page_directory_phys, page_directory_size);
        write_shadow_page_table(ctx, table()->shadow_address());
    } else {
        page* page = ctx->pgds(id());
        if (page) {
            A3_LOG("set pgd %d channel page %" PRIx64 "\n", id(), page->address());
            write_shadow_page_table(ctx, page->address());
        }
    }

    if (!ctx->para_virtualized()) {
        // FIXME(Yusuke Suzuki):
        // Fix this
//     clear_tlb_flush_needed();
//     remove_overridden_shadow(ctx);
        registers::accessor regs;
        regs.wait_ne(0x100c80, 0x00ff0000, 0x00000000);
        regs.write32(0x100cb8, table()->shadow_address() >> 8);
        regs.write32(0x100cbc, 0x80000000 | 0x1);
        regs.wait_eq(0x100c80, 0x00008000, 0x00008000);
    }
}

void channel::write_shadow_page_table(context* ctx, uint64_t shadow) {
    mmio::write64(shadow_ramin(), 0x0200, shadow);
}

void channel::attach(context* ctx, uint64_t addr) {
    shadow(ctx);
    ctx->ramin_channel_map()->insert(std::make_pair(ramin_address(), this));
    ctx->barrier()->map(ramin_address());
}

uint64_t channel::refresh(context* ctx, uint64_t addr) {
    A3_LOG("mapping 0x%" PRIX64 " with shadow 0x%" PRIX64 "\n", addr, shadow_ramin()->address());
    bool old_remap = false;
    if (enabled()) {
        if (addr == ramin_address()) {
            // same channel ramin
            return shadow_ramin()->address();
        }
        old_remap = !detach(ctx, addr);
    }
    enabled_ = true;
    const uint64_t old = ramin_address_;
    ramin_address_ = addr;
    attach(ctx, addr);
    A3_SYNCHRONIZED(device()->mutex()) {
        device()->bar3()->reset_barrier(ctx, old, addr, old_remap);
    }
    return shadow_ramin()->address();
}

void channel::override_shadow(context* ctx, uint64_t shadow, page_table_reuse_t* reuse) {
    derived_ = reuse;
    reuse->set(id(), true);
    write_shadow_page_table(ctx, shadow);
}

bool channel::is_overridden_shadow() {
    return derived_ != &original_;
}

void channel::remove_overridden_shadow(context* ctx) {
    derived_->set(id(), false);
    derived_ = &original_;
    write_shadow_page_table(ctx, table()->shadow_address());
}

void channel::tlb_flush_needed() {
    tlb_flush_needed_ = true;
}

void channel::flush(context* ctx) {
    if (!tlb_flush_needed_) {
        return;
    }

    // A3_FATAL(stdout, "shadowing times %" PRIu64 "\n", ctx->increment_shadowing_times());

    // clear dirty flags
    channel* origin = nullptr;
    for (page_table_reuse_t::size_type pos = derived_->find_first(); pos != derived_->npos; pos = derived_->find_next(pos)) {
        channel* channel = ctx->channels(pos);
        channel->clear_tlb_flush_needed();
        if (!channel->is_overridden_shadow()) {
            origin = channel;
        }
    }

    // shadowing...
    {
        timer_t timer;
        timer.start();
        origin->table()->refresh_page_directories(ctx, table()->page_directory_address());
        auto duration = ctx->instruments()->increment_shadowing(timer.elapsed());
        a3::ignore_unused_variable_warning(duration);
        // A3_FATAL(stdout, "shadowing duration %" PRIu64 "\n", static_cast<uint64_t>(duration.total_microseconds()));
    }

    registers::accessor regs;
    A3_LOG("flush %d %" PRIx64 "\n", origin->id(), origin->table()->shadow_address());
    regs.wait_ne(0x100c80, 0x00ff0000, 0x00000000);
    regs.write32(0x100cb8, origin->table()->shadow_address() >> 8);
    regs.write32(0x100cbc, 0x80000000 | 0x1);
    regs.wait_eq(0x100c80, 0x00008000, 0x00008000);
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
