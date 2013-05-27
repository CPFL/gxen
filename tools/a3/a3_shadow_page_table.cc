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

#include <stdint.h>
#include <boost/make_shared.hpp>
#include "a3_inttypes.h"
#include "a3_bit_mask.h"
#include "a3_shadow_page_table.h"
#include "a3_pramin.h"
#include "a3_page.h"
#include "a3_context.h"
namespace a3 {

shadow_page_table::shadow_page_table(uint32_t channel_id)
    : size_(0)
    , channel_id_(channel_id)
    , phys_()
    , large_pages_pool_()
    , small_pages_pool_()
    , large_pages_pool_cursor_()
    , small_pages_pool_cursor_() {
}

void shadow_page_table::set_low_size(uint32_t value) {
    low_size_ = value;
}

void shadow_page_table::set_high_size(uint32_t value) {
    high_size_ = value;
}

bool shadow_page_table::refresh(context* ctx, uint64_t page_directory_address, uint64_t page_limit) {
    // allocate directories
    if (!phys()) {
        phys_.reset(new page(0x10));
        phys_->clear();
    }

    // directories size change
    page_directory_address_ = page_directory_address;

    const uint64_t vspace_size = page_limit + 1;
    size_ = vspace_size;

    bool result = false;
    if (page_directory_size() > kMAX_PAGE_DIRECTORIES) {
        return result;
    }

//     const uint64_t page_directory_page_size =
//         round_up(page_directory_size() * sizeof(struct page_directory), kPAGE_SIZE) / kPAGE_SIZE;
//     if (page_directory_page_size) {
//         if (!phys() || phys()->size() < page_directory_page_size) {
//             result = true;
//             phys_.reset(new page(page_directory_page_size));
//         }
//     }

    refresh_page_directories(ctx, page_directory_address);
    return result;
}

void shadow_page_table::refresh_page_directories(context* ctx, uint64_t address) {
    pramin::accessor pramin;
    page_directory_address_ = address;
    large_pages_pool_cursor_ = 0;
    small_pages_pool_cursor_ = 0;

    phys()->clear();
    for (uint64_t offset = 0, index = 0; offset < 0x10000; offset += 0x8, ++index) {
        const struct page_directory res = page_directory::create(&pramin, page_directory_address() + offset);
        struct page_directory result = { };
        if (res.large_page_table_present || res.small_page_table_present) {
            result = refresh_directory(ctx, &pramin, res);
        }
        phys()->write32(offset, result.word0);
        phys()->write32(offset + 0x4, result.word1);
    }
    A3_LOG("scan page table of channel id 0x%" PRIi32 " : pd 0x%" PRIX64 "\n", channel_id(), page_directory_address());
}

boost::shared_ptr<page> shadow_page_table::allocate_large_page() {
    if (large_pages_pool_cursor_ == large_pages_pool_.size()) {
        const boost::shared_ptr<page> p = boost::make_shared<page>(kLARGE_PAGE_COUNT * 0x8 / kPAGE_SIZE);
        large_pages_pool_.push_back(p);
        return p;
    }
    const boost::shared_ptr<page> p = large_pages_pool_[large_pages_pool_cursor_++];
    return p;
}

boost::shared_ptr<page> shadow_page_table::allocate_small_page() {
    if (small_pages_pool_cursor_ == small_pages_pool_.size()) {
        const boost::shared_ptr<page> p = boost::make_shared<page>(kSMALL_PAGE_COUNT * 0x8 / kPAGE_SIZE);
        small_pages_pool_.push_back(p);
        return p;
    }
    const boost::shared_ptr<page> p = small_pages_pool_[small_pages_pool_cursor_++];
    return p;
}

struct page_directory shadow_page_table::refresh_directory(context* ctx, pramin::accessor* pramin, const struct page_directory& dir) {
    struct page_directory result(dir);
    if (dir.large_page_table_present) {
        // TODO(Yusuke Suzuki): regression
        const uint64_t address = ctx->get_phys_address(static_cast<uint64_t>(dir.large_page_table_address) << 12);
//         const uint64_t address = (static_cast<uint64_t>(dir.large_page_table_address) << 12);
        boost::shared_ptr<page> large_page = allocate_large_page();
        for (uint64_t i = 0, iz = kLARGE_PAGE_COUNT; i < iz; ++i) {
            const uint64_t item = 0x8 * i;
            const struct page_entry res = refresh_entry(ctx, pramin, page_entry::create(pramin, address + item));
            large_page->write32(item, res.word0);
            large_page->write32(item + 0x4, res.word1);
        }
        const uint64_t result_address = (large_page->address() >> 12);
        result.large_page_table_address = result_address;
    } else {
        result.word0 = 0;
    }

    if (dir.small_page_table_present) {
        // TODO(Yusuke Suzuki): regression
        const uint64_t address = ctx->get_phys_address(static_cast<uint64_t>(dir.small_page_table_address) << 12);
//         const uint64_t address = (static_cast<uint64_t>(dir.small_page_table_address) << 12);
        A3_LOG("=> %" PRIX64 "\n", address);
        boost::shared_ptr<page> small_page = allocate_small_page();
        for (uint64_t i = 0, iz = kSMALL_PAGE_COUNT; i < iz; ++i) {
            const uint64_t item = 0x8 * i;
            const struct page_entry res = refresh_entry(ctx, pramin, page_entry::create(pramin, address + item));
            small_page->write32(item, res.word0);
            small_page->write32(item + 0x4, res.word1);
        }
        const uint64_t result_address = (small_page->address() >> 12);
        result.small_page_table_address = result_address;
    } else {
        result.word1 = 0;
    }

    return result;
}

struct page_entry shadow_page_table::refresh_entry(context* ctx, pramin::accessor* pramin, const struct page_entry& entry) {
    struct page_entry result(entry);
    if (entry.present && entry.target == page_entry::TARGET_TYPE_VRAM) {
        // rewrite address
        const uint64_t g_field = result.address;
        const uint64_t g_address = g_field << 12;
        const uint64_t h_address = ctx->get_phys_address(g_address);
        const uint64_t h_field = h_address >> 12;
        result.address = h_field;
        // A3_LOG("Rewriting address 0x%" PRIx64 " to 0x%" PRIx64 "\n", g_address, h_address);
    }
    return result;
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
