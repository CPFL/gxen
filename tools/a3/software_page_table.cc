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

#include <cstdint>
#include <cinttypes>
#include "bit_mask.h"
#include "software_page_table.h"
#include "pmem.h"
#include "pv_page.h"
#include "context.h"
#include "ignore_unused_variable_warning.h"
#include "radix_tree.h"
namespace a3 {

software_page_table::software_page_table(uint32_t channel_id, bool para, uint64_t predefined_max)
    : directories_()
    , size_(0)
    , channel_id_(channel_id)
    , predefined_max_(predefined_max)
{
    if (predefined_max_) {
        size_ = predefined_max_;
    }
    if (para) {
        // initialize directory at this time
        directories_.resize(page_directory_size());
    }
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

    if (!predefined_max_) {
        const uint64_t vspace_size = page_limit + 1;
        size_ = vspace_size;
    }

    bool result = false;
    if (page_directory_size() > kMAX_PAGE_DIRECTORIES) {
        return result;
    }

    refresh_page_directories(ctx, page_directory_address);
    return result;
}

void software_page_table::refresh_page_directories(context* ctx, uint64_t address) {
    pmem::accessor pmem;
    page_directory_address_ = address;
    directories_.resize(page_directory_size());
    std::size_t i = 0;
    const std::size_t count = directories_.size();
    std::size_t remain = size() % kPAGE_DIRECTORY_COVERED_SIZE;
    if (!remain) {
        remain = kPAGE_DIRECTORY_COVERED_SIZE;
    }
    for (software_page_directories::iterator it = directories_.begin(), last = directories_.end(); it != last; ++it, ++i) {
        const uint64_t item = 0x8 * i;
        if (!predefined_max_) {
            it->refresh(ctx, &pmem, page_directory::create(&pmem, page_directory_address() + item), kPAGE_DIRECTORY_COVERED_SIZE);
        } else {
            if ((i + 1) == count) {
                it->refresh(ctx, &pmem, page_directory::create(&pmem, page_directory_address() + item), remain);
            } else {
                it->refresh(ctx, &pmem, page_directory::create(&pmem, page_directory_address() + item), kPAGE_DIRECTORY_COVERED_SIZE);
            }
        }
    }

    A3_LOG("scan page table of channel id 0x%" PRIi32 " : pd 0x%" PRIX64 " size %" PRIu64 "\n", channel_id(), page_directory_address(), directories_.size());
    // dump();
}

void software_page_table::software_page_directory::refresh(context* ctx, pmem::accessor* pmem, const struct page_directory& dir, std::size_t remain) {
    if (dir.large_page_table_present) {
        const uint64_t address = ctx->get_phys_address(static_cast<uint64_t>(dir.large_page_table_address) << 12);
        if (!large_entries()) {
            large_entries_.reset(new software_page_entries(kLARGE_PAGE_COUNT));
        }
        const std::size_t count = std::min(remain / kLARGE_PAGE_SIZE, page_directory::large_size_count(dir));
        ASSERT(count <= kLARGE_PAGE_COUNT);
        for (std::size_t i = 0; i < count; ++i) {
            const uint64_t item = 0x8 * i;
            struct page_entry entry;
            if (page_entry::create(pmem, address + item, &entry)) {
                (*large_entries_)[i].refresh(ctx, entry);
            } else {
                (*large_entries_)[i].clear();
            }
        }
    } else {
        large_entries_.reset();
    }

    if (dir.small_page_table_present) {
        const uint64_t address = ctx->get_phys_address(static_cast<uint64_t>(dir.small_page_table_address) << 12);
        if (!small_entries()) {
            small_entries_.reset(new software_page_entries(kSMALL_PAGE_COUNT));
        }
        const std::size_t count = remain / kSMALL_PAGE_SIZE;
        ASSERT(count <= kSMALL_PAGE_COUNT);
        for (std::size_t i = 0; i < count; ++i) {
            const uint64_t item = 0x8 * i;
            struct page_entry entry;
            if (page_entry::create(pmem, address + item, &entry)) {
                (*small_entries_)[i].refresh(ctx, entry);
            } else {
                (*small_entries_)[i].clear();
            }
        }
    } else {
        small_entries_.reset();
    }
}

uint64_t software_page_table::resolve(uint64_t virtual_address, struct software_page_entry* result) {
    const uint32_t index = virtual_address / kPAGE_DIRECTORY_COVERED_SIZE;
    if (directories_.size() <= index) {
        return UINT64_MAX;
    }
    return directories_[index].resolve(virtual_address - index * kPAGE_DIRECTORY_COVERED_SIZE, result);
}

uint64_t software_page_table::software_page_directory::resolve(uint64_t offset, struct software_page_entry* result) {
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
                    ignore_unused_variable_warning(address);
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
                    ignore_unused_variable_warning(address);
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

void software_page_table::software_page_directory::pv_reflect(context* ctx, bool big, uint32_t index, uint64_t guest) {
    struct page_entry entry;
    entry.raw = guest;
    if (big) {
        if (!large_entries()) {
            large_entries_.reset(new software_page_entries(kLARGE_PAGE_COUNT));
        }
        (*large_entries_)[index].refresh(ctx, entry);
    } else {
        if (!small_entries()) {
            small_entries_.reset(new software_page_entries(kSMALL_PAGE_COUNT));
        }
        (*small_entries_)[index].refresh(ctx, entry);
    }
}

void software_page_table::pv_reflect_entry(context* ctx, uint32_t d, bool big, uint32_t index, uint64_t entry) {
    struct software_page_directory& dir = directories_[d];
    dir.pv_reflect(ctx, big, index, entry);
}

void software_page_table::software_page_directory::pv_scan(context* ctx, bool big, pv_page* pgt, std::size_t remain) {
    if (big) {
        if (!large_entries()) {
            large_entries_.reset(new software_page_entries(kLARGE_PAGE_COUNT));
        }
        const std::size_t count = remain / kLARGE_PAGE_SIZE;
        ASSERT(count <= kLARGE_PAGE_COUNT);
        for (std::size_t i = 0; i < count; ++i) {
            const uint64_t item = 0x8 * i;
            struct page_entry entry;
            if (page_entry::create(pgt, item, &entry)) {
                (*large_entries_)[i].assign(entry);
            } else {
                (*large_entries_)[i].clear();
            }
        }
    } else {
        if (!small_entries()) {
            small_entries_.reset(new software_page_entries(kSMALL_PAGE_COUNT));
        }
        const std::size_t count = remain / kSMALL_PAGE_SIZE;
        ASSERT(count <= kSMALL_PAGE_COUNT);
        for (std::size_t i = 0; i < count; ++i) {
            const uint64_t item = 0x8 * i;
            struct page_entry entry;
            if (page_entry::create(pgt, item, &entry)) {
                (*small_entries_)[i].assign(entry);
            } else {
                (*small_entries_)[i].clear();
            }
        }
    }
}

void software_page_table::pv_scan(context* ctx, uint32_t d, bool big, pv_page* pgt) {
    struct software_page_directory& dir = directories_[d];
    dir.pv_scan(ctx, big, pgt, predefined_max_);
}

void software_page_entry::refresh(context* ctx, const struct page_entry& entry) {
    phys_ = ctx->guest_to_host(entry);
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
