/*
 * A3 shadow page table
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
#include <cinttypes>
#include <boost/make_shared.hpp>
#include "bit_mask.h"
#include "shadow_page_table.h"
#include "pmem.h"
#include "page.h"
#include "context.h"
namespace a3 {

shadow_page_table::shadow_page_table(uint32_t channel_id)
    : size_(0)
    , channel_id_(channel_id)
    , phys_()
    , large_pages_pool_()
    , small_pages_pool_()
    , large_pages_pool_cursor_()
    , small_pages_pool_cursor_()
    , directory_()
{
}

void shadow_page_table::allocate_shadow_address() {
    if (!phys()) {
        phys_.reset(new page(0x10));
        phys_->clear();
    }
}

bool shadow_page_table::refresh(context* ctx, uint64_t page_directory_address, uint64_t page_limit) {
    // allocate directories
    allocate_shadow_address();

    page_directory_address_ = page_directory_address;

    const uint64_t vspace_size = page_limit + 1;
    size_ = vspace_size;

    refresh_page_directories(ctx, page_directory_address);
    return true;
}

void shadow_page_table::refresh_page_directories(context* ctx, uint64_t address) {
    pmem::accessor pmem;
    page_directory_address_ = address;
    large_pages_pool_cursor_ = 0;
    small_pages_pool_cursor_ = 0;

    // 0 check
    if (!ctx->get_virt_address(address)) {
        // FIXME(Yusuke Suzuki): Disable page table
        return;
    }

    if (!ctx->in_memory_range(address) || !ctx->in_memory_size(size())) {
        A3_LOG("page data out of range 0x%" PRIx64 " with 0x%" PRIx64 "\n", address, size());
        return;
    }

    for (uint64_t offset = 0, index = 0; offset < 0x10000; offset += 0x8, ++index) {
        const struct page_directory result = refresh_directory(ctx, &pmem, index);
        const struct page_directory old = directory_.entries[index].value;
        if (result != old) {
            if (old.word0 != result.word0) {
                phys()->write32(offset, result.word0);
            }
            if (old.word1 != result.word1) {
                phys()->write32(offset + 0x4, result.word1);
            }
            directory_.entries[index].value = result;
        }
    }
    A3_LOG("scan page table of channel id 0x%" PRIi32 " : pd 0x%" PRIX64 "\n", channel_id(), page_directory_address());
}

struct page_directory shadow_page_table::refresh_directory(context* ctx, pmem::accessor* pmem, uint32_t index) {
    const struct page_directory virt = page_directory::create(pmem, page_directory_address() + index * 0x8);
    struct page_directory result(virt);
    shadow_page_entry_t& shadow = directory_.entries[index];
    if (virt.large_page_table_present) {
        shadow.ensure_large_page_table();
        page* large_page = allocate_large_page();
        const uint64_t address = ctx->get_phys_address(static_cast<uint64_t>(virt.large_page_table_address) << 12);
        for (uint64_t i = 0, iz = page_directory::large_size_count(virt); i < iz; ++i) {
            const uint64_t item = 0x8 * i;
            struct page_entry entry;
            page_entry::create(pmem, address + item, &entry);
            const struct page_entry res = ctx->guest_to_host(entry);
            const struct page_entry old = (*shadow.large)[i];
            if (res != old) {
                if (res.word0 != old.word0) {
                    large_page->write32(item, res.word0);
                }
                if (res.word1 != old.word1) {
                    large_page->write32(item + 0x4, res.word1);
                }
                (*shadow.large)[i] = res;
            }
        }
        const uint64_t result_address = (large_page->address() >> 12);
        result.large_page_table_address = result_address;
    } else {
        result.word0 = 0;
    }

    if (virt.small_page_table_present) {
        shadow.ensure_small_page_table();
        page* small_page = allocate_small_page();
        const uint64_t address = ctx->get_phys_address(static_cast<uint64_t>(virt.small_page_table_address) << 12);
        for (uint64_t i = 0, iz = kSMALL_PAGE_COUNT; i < iz; ++i) {
            const uint64_t item = 0x8 * i;
            struct page_entry entry;
            page_entry::create(pmem, address + item, &entry);
            struct page_entry res = ctx->guest_to_host(entry);
            const struct page_entry old = (*shadow.small)[i];
            if (res != old) {
                if (res.word0 != old.word0) {
                    small_page->write32(item, res.word0);
                }
                if (res.word1 != old.word1) {
                    small_page->write32(item + 0x4, res.word1);
                }
                (*shadow.small)[i] = res;
            }
        }
        const uint64_t result_address = (small_page->address() >> 12);
        result.small_page_table_address = result_address;
    } else {
        result.word1 = 0;
    }

    return result;
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
