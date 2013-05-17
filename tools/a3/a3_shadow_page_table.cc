/*
 * Cross shadow page table
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
#include "a3_shadow_page_table.h"
#include "a3_pramin.h"
#include "a3_page.h"
namespace a3 {

shadow_page_table::shadow_page_table(uint32_t channel_id)
    : directories_()
    , size_(0)
    , channel_id_(channel_id)
    , phys_(NULL) {
}

bool shadow_page_table::refresh(context* ctx, uint64_t page_directory_address, uint64_t page_limit) {
    // directories size change
    const uint64_t vspace_size = page_limit + 1;
    size_ = vspace_size;
    if (page_directory_size() > kMAX_PAGE_DIRECTORIES) {
        return false;
    }

    const uint64_t page_directory_page_size =
        round_up(page_directory_size() * sizeof(struct page_directory), kPAGE_SIZE) / kPAGE_SIZE;
    if (!page_directory_page_size) {
        // page_directory_page_size becomes 0
        return true;
    }

    if (!phys() || phys()->size() < page_directory_page_size) {
        phys_.reset(new page(page_directory_page_size));
    }

    return refresh_page_directories(ctx, page_directory_address);
}

bool shadow_page_table::refresh_page_directories(context* ctx, uint64_t address) {
    pramin::accessor pramin;
    page_directory_address_ = address;

    directories_.resize(page_directory_size());
    std::size_t i = 0;
    for (shadow_page_directories::iterator it = directories_.begin(),
         last = directories_.end(); it != last; ++it, ++i) {
        const uint64_t item = 0x8 * i;
        const struct page_directory result =
            it->refresh(ctx, &pramin, page_directory::create(&pramin, page_directory_address() + item));
        // TODO(Yusuke Suzuki): shift shadow page table value
        phys()->write32(item, result.word0);
        phys()->write32(item + 0x4, result.word1);
    }

    A3_LOG("scan page table of channel id 0x%" PRIX32 " : pd 0x%" PRIX64 "\n", channel_id(), page_directory_address());
    // dump();
    return false;
}

void shadow_page_table::set_low_size(uint32_t value) {
    low_size_ = value;
}

void shadow_page_table::set_high_size(uint32_t value) {
    high_size_ = value;
}

uint64_t shadow_page_table::resolve(uint64_t virtual_address, struct shadow_page_entry* result) {
    const uint32_t index = virtual_address / kPAGE_DIRECTORY_COVERED_SIZE;
    if (directories_.size() <= index) {
        return UINT64_MAX;
    }
    return directories_[index].resolve(virtual_address - index * kPAGE_DIRECTORY_COVERED_SIZE, result);
}

void shadow_page_table::dump() const {
    std::size_t i = 0;
    for (shadow_page_directories::const_iterator it = directories_.begin(),
         iz = directories_.end(); it != iz; ++it, ++i) {
        const struct shadow_page_directory& dir = *it;
        A3_LOG("PDE 0x%" PRIX64 " : large %d / small %d\n",
                  page_directory_address_ + 0x8 * i,
                  dir.virt().large_page_table_present,
                  dir.virt().small_page_table_present);

        if (dir.virt().large_page_table_present) {
            std::size_t j = 0;
            for (shadow_page_directory::shadow_page_entries::const_iterator jt = dir.large_entries().begin(),
                 jz = dir.large_entries().end(); jt != jz; ++jt, ++j) {
                if (jt->present()) {
                    const uint64_t address = jt->virt().address;
                    A3_LOG("  PTE 0x%" PRIX64 " - 0x%" PRIX64 " => 0x%" PRIX64 " - 0x%" PRIX64 " [%s] type [%d]\n",
                              kPAGE_DIRECTORY_COVERED_SIZE * i + kLARGE_PAGE_SIZE * j,
                              kPAGE_DIRECTORY_COVERED_SIZE * i + kLARGE_PAGE_SIZE * (j + 1) - 1,
                              (address << 12),
                              (address << 12) + kLARGE_PAGE_SIZE - 1,
                              jt->virt().read_only ? "RO" : "RW",
                              jt->virt().target);
                }
            }
        }

        if (dir.virt().small_page_table_present) {
            std::size_t j = 0;
            for (shadow_page_directory::shadow_page_entries::const_iterator jt = dir.small_entries().begin(),
                 jz = dir.small_entries().end(); jt != jz; ++jt, ++j) {
                if (jt->present()) {
                    const uint64_t address = jt->virt().address;
                    A3_LOG("  PTE 0x%" PRIX64 " - 0x%" PRIX64 " => 0x%" PRIX64 " - 0x%" PRIX64 " [%s] type [%d]\n",
                              kPAGE_DIRECTORY_COVERED_SIZE * i + kSMALL_PAGE_SIZE * j,
                              kPAGE_DIRECTORY_COVERED_SIZE * i + kSMALL_PAGE_SIZE * (j + 1) - 1,
                              (address << 12),
                              (address << 12) + kSMALL_PAGE_SIZE - 1,
                              jt->virt().read_only ? "RO" : "RW",
                              jt->virt().target);
                }
            }
        }
    }
}

struct page_directory shadow_page_directory::refresh(context* ctx, pramin::accessor* pramin, const struct page_directory& dir) {
    virt_ = dir;

    if (dir.large_page_table_present) {
        const uint64_t address = static_cast<uint64_t>(dir.large_page_table_address) << 12;
        const uint64_t count = kPAGE_DIRECTORY_COVERED_SIZE >> kLARGE_PAGE_SHIFT;
        if (!large_page()) {
            large_page_.reset(new page(count * 0x8 / kPAGE_SIZE));
        }
        large_entries_.resize(count);
        std::size_t i = 0;
        for (shadow_page_entries::iterator it = large_entries_.begin(),
             last = large_entries_.end(); it != last; ++it, ++i) {
            const uint64_t item = 0x8 * i;
            const struct page_entry result =
                it->refresh(pramin, page_entry::create(pramin, address + item));
            large_page_->write32(item, result.word0);
            large_page_->write32(item + 0x4, result.word1);
        }
    } else {
        large_entries_.clear();
    }

    if (dir.small_page_table_present) {
        const uint64_t address = static_cast<uint64_t>(dir.small_page_table_address) << 12;
        const uint64_t count = kPAGE_DIRECTORY_COVERED_SIZE >> kSMALL_PAGE_SHIFT;
        if (!small_page()) {
            small_page_.reset(new page(count * 0x8 / kPAGE_SIZE));
        }
        small_entries_.resize(count);
        std::size_t i = 0;
        for (shadow_page_entries::iterator it = small_entries_.begin(),
             last = small_entries_.end(); it != last; ++it, ++i) {
            const uint64_t item = 0x8 * i;
            const struct page_entry result =
                it->refresh(pramin, page_entry::create(pramin, address + item));
            small_page_->write32(item, result.word0);
            small_page_->write32(item + 0x4, result.word1);
        }
    } else {
        small_entries_.clear();
    }

    return dir;
}

uint64_t shadow_page_directory::resolve(uint64_t offset, struct shadow_page_entry* result) {
    if (virt().large_page_table_present) {
        const uint64_t index = offset / kLARGE_PAGE_SIZE;
        const uint64_t rest = offset % kLARGE_PAGE_SIZE;
        if (large_entries_.size() > index) {
            const struct shadow_page_entry& entry = large_entries_[index];
            if (entry.present()) {
                if (result) {
                    *result = entry;
                }
                const uint64_t address = entry.virt().address;
                return (address << 12) + rest;
            }
        }
    }

    if (virt().small_page_table_present) {
        const uint64_t index = offset / kSMALL_PAGE_SIZE;
        const uint64_t rest = offset % kSMALL_PAGE_SIZE;
        if (small_entries_.size() > index) {
            const struct shadow_page_entry& entry = small_entries_[index];
            if (entry.present()) {
                if (result) {
                    *result = entry;
                }
                const uint64_t address = entry.virt().address;
                return (address << 12) + rest;
            }
        }
    }

    return UINT64_MAX;
}

struct page_entry shadow_page_entry::refresh(pramin::accessor* pramin, const struct page_entry& entry) {
    virt_ = entry;
    return entry;
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
