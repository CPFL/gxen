/*
 * A3 software page table
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
#include "a3_inttypes.h"
#include "a3_bit_mask.h"
#include "a3_software_page_table.h"
#include "a3_pramin.h"
#include "a3_context.h"
namespace a3 {

software_page_table::software_page_table(uint32_t channel_id)
    : directories_()
    , size_(0)
    , channel_id_(channel_id) {
}

void software_page_table::set_low_size(uint32_t value) {
    low_size_ = value;
}

void software_page_table::set_high_size(uint32_t value) {
    high_size_ = value;
}

bool software_page_table::refresh(context* ctx, uint64_t page_directory_address, uint64_t page_limit) {
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

void software_page_table::refresh_page_directories(context* ctx, uint64_t address) {
    pramin::accessor pramin;
    page_directory_address_ = address;
    directories_.resize(page_directory_size());
    // directories_.resize(max);
    std::size_t i = 0;
    for (software_page_directories::iterator it = directories_.begin(),
         last = directories_.end(); it != last; ++it, ++i) {
        const uint64_t item = 0x8 * i;
        it->refresh(ctx, &pramin, page_directory::create(&pramin, page_directory_address() + item));
    }

    A3_LOG("scan page table of channel id 0x%" PRIi32 " : pd 0x%" PRIX64 " size %" PRIu64 "\n", channel_id(), page_directory_address(), directories_.size());
    // dump();
}

void software_page_table::software_page_directory::refresh(context* ctx, pramin::accessor* pramin, const struct page_directory& dir) {
    if (dir.large_page_table_present) {
        // TODO(Yusuke Suzuki): regression
        const uint64_t address = ctx->get_phys_address(static_cast<uint64_t>(dir.large_page_table_address) << 12);
//         const uint64_t address = (static_cast<uint64_t>(dir.large_page_table_address) << 12);
        if (!large_entries()) {
            large_entries_.reset(new software_page_entries(kLARGE_PAGE_COUNT));
        }
        std::size_t i = 0;
        for (software_page_entries::iterator it = large_entries_->begin(),
             last = large_entries_->end(); it != last; ++it, ++i) {
            const uint64_t item = 0x8 * i;
            it->refresh(ctx, pramin, page_entry::create(pramin, address + item));
        }
    } else {
        large_entries_.reset();
    }

    if (dir.small_page_table_present) {
        // TODO(Yusuke Suzuki): regression
        const uint64_t address = ctx->get_phys_address(static_cast<uint64_t>(dir.small_page_table_address) << 12);
//         const uint64_t address = (static_cast<uint64_t>(dir.small_page_table_address) << 12);
        if (!small_entries()) {
            small_entries_.reset(new software_page_entries(kSMALL_PAGE_COUNT));
        }
        A3_LOG("=> %" PRIX64 "\n", address);
        std::size_t i = 0;
        for (software_page_entries::iterator it = small_entries_->begin(),
             last = small_entries_->end(); it != last; ++it, ++i) {
            const uint64_t item = 0x8 * i;
            it->refresh(ctx, pramin, page_entry::create(pramin, address + item));
        }
    } else {
        small_entries_.reset();
    }
}

void software_page_entry::refresh(context* ctx, pramin::accessor* pramin, const struct page_entry& entry) {
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
    phys_ = result;
}

uint64_t software_page_table::resolve(uint64_t virtual_address, struct software_page_entry* result) {
    const uint32_t index = virtual_address / kPAGE_DIRECTORY_COVERED_SIZE;
    if (directories_.size() <= index) {
        return UINT64_MAX;
    }
    return directories_[index].resolve(virtual_address - index * kPAGE_DIRECTORY_COVERED_SIZE, result);
}

uint64_t software_page_table::software_page_directory::resolve(uint64_t offset, struct software_page_entry* result) {
    if (large_entries()) {
        const uint64_t index = offset / kLARGE_PAGE_SIZE;
        const uint64_t rest = offset % kLARGE_PAGE_SIZE;
        if (large_entries_->size() > index) {
            const struct software_page_entry& entry = (*large_entries_)[index];
            if (entry.present()) {
                if (result) {
                    *result = entry;
                }
                const uint64_t address = entry.phys().address;
                return (address << 12) + rest;
            }
        }
    }

    if (small_entries()) {
        const uint64_t index = offset / kSMALL_PAGE_SIZE;
        const uint64_t rest = offset % kSMALL_PAGE_SIZE;
        if (small_entries_->size() > index) {
            const struct software_page_entry& entry = (*small_entries_)[index];
            if (entry.present()) {
                if (result) {
                    *result = entry;
                }
                const uint64_t address = entry.phys().address;
                return (address << 12) + rest;
            }
        }
    }

    return UINT64_MAX;
}

void software_page_table::dump() const {
    std::size_t i = 0;
    for (software_page_directories::const_iterator it = directories_.begin(),
         iz = directories_.end(); it != iz; ++it, ++i) {
        const struct software_page_directory& dir = *it;
        if (dir.large_entries()) {
            std::size_t j = 0;
            for (software_page_directory::software_page_entries::const_iterator jt = dir.large_entries()->begin(),
                 jz = dir.large_entries()->end(); jt != jz; ++jt, ++j) {
                if (jt->present()) {
                    const uint64_t address = jt->phys().address;
                    A3_LOG("  PTE 0x%" PRIX64 " - 0x%" PRIX64 " => 0x%" PRIX64 " - 0x%" PRIX64 " [%s] type [%d]\n",
                              kPAGE_DIRECTORY_COVERED_SIZE * i + kLARGE_PAGE_SIZE * j,
                              kPAGE_DIRECTORY_COVERED_SIZE * i + kLARGE_PAGE_SIZE * (j + 1) - 1,
                              (address << 12),
                              (address << 12) + kLARGE_PAGE_SIZE - 1,
                              jt->phys().read_only ? "RO" : "RW",
                              jt->phys().target);
                }
            }
        }

        if (dir.small_entries()) {
            std::size_t j = 0;
            for (software_page_directory::software_page_entries::const_iterator jt = dir.small_entries()->begin(),
                 jz = dir.small_entries()->end(); jt != jz; ++jt, ++j) {
                if (jt->present()) {
                    const uint64_t address = jt->phys().address;
                    A3_LOG("  PTE 0x%" PRIX64 " - 0x%" PRIX64 " => 0x%" PRIX64 " - 0x%" PRIX64 " [%s] type [%d]\n",
                              kPAGE_DIRECTORY_COVERED_SIZE * i + kSMALL_PAGE_SIZE * j,
                              kPAGE_DIRECTORY_COVERED_SIZE * i + kSMALL_PAGE_SIZE * (j + 1) - 1,
                              (address << 12),
                              (address << 12) + kSMALL_PAGE_SIZE - 1,
                              jt->phys().read_only ? "RO" : "RW",
                              jt->phys().target);
                }
            }
        }
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
