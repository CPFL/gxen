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
#include "cross_inttypes.h"
#include "cross_bit_mask.h"
#include "cross_shadow_page_table.h"
#include "cross_pramin.h"
namespace cross {

shadow_page_table::shadow_page_table(uint32_t channel_id)
    : directories_()
    , size_(0)
    , channel_address_(0)
    , channel_id_(channel_id) {
}

bool shadow_page_table::refresh(context* ctx, uint32_t value) {
    // construct shadow page table from real data
    pramin_accessor pramin;

    channel_address_ = value;
    const uint64_t ramin = static_cast<uint64_t>(bit_mask<30>(value)) << 12;

    page_descriptor descriptor;
    descriptor.page_directory_address_low = pramin.read32(ramin + 0x200);
    descriptor.page_directory_address_high = pramin.read32(ramin + 0x204);
    descriptor.page_limit_low = pramin.read32(ramin + 0x208);
    descriptor.page_limit_high = pramin.read32(ramin + 0x20c);

    // exactly one page
    // ctx->remapping()->map(ramin, 0, true);

    printf("ramin 0x%" PRIX64 " page directory address 0x%" PRIX64 " and size %" PRIu64 "\n",
           ramin, descriptor.page_directory_address, descriptor.page_limit);

    const uint64_t vspace_size = descriptor.page_limit + 1;

    size_ = vspace_size;
    if (page_directory_size() > kMAX_PAGE_DIRECTORIES) {
        return false;
    }

    return refresh_page_directories(ctx, descriptor.page_directory_address);
}

bool shadow_page_table::refresh_page_directories(context* ctx, uint64_t address) {
    pramin_accessor pramin;
    page_directory_address_ = address;

    directories_.resize(page_directory_size());
    std::size_t i = 0;
    for (shadow_page_directories::iterator it = directories_.begin(),
         last = directories_.end(); it != last; ++it, ++i) {
        it->refresh(ctx, channel_id(), &pramin, page_directory_address() + 0x8 * i);
    }

    // max page directories size is page
    // ctx->remapping()->map(page_directory_address(), 0, true);

    dump();
    printf("scan page table done 0x%" PRIX64 "\n", page_directory_address());
    return false;
}

void shadow_page_table::set_low_size(uint32_t value) {
    low_size_ = value;
}

void shadow_page_table::set_high_size(uint32_t value) {
    high_size_ = value;
}

uint64_t shadow_page_table::resolve(uint64_t virtual_address) {
    const uint32_t index = virtual_address / kPAGE_DIRECTORY_COVERED_SIZE;
    if (directories_.size() <= index) {
        return UINT64_MAX;
    }
    return directories_[index].resolve(virtual_address - index * kPAGE_DIRECTORY_COVERED_SIZE);
}

void shadow_page_table::dump() const {
    std::size_t i = 0;
    for (shadow_page_directories::const_iterator it = directories_.begin(),
         iz = directories_.end(); it != iz; ++it, ++i) {
        const struct shadow_page_directory& dir = *it;
        printf("PDE 0x%" PRIX64 " : large %d / small %d\n",
               page_directory_address_ + 0x8 * i,
               dir.virt().large_page_table_present,
               dir.virt().small_page_table_present);

        if (dir.virt().large_page_table_present) {
            std::size_t j = 0;
            for (shadow_page_directory::shadow_page_entries::const_iterator jt = dir.large_entries().begin(),
                 jz = dir.large_entries().end(); jt != jz; ++jt, ++j) {
                if (jt->present()) {
                    const uint64_t address = jt->virt().address;
                    printf("  PTE 0x%" PRIX64 " - 0x%" PRIX64 " => 0x%" PRIX64 " - 0x%" PRIX64 " [%s] type [%d]\n",
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
                    printf("  PTE 0x%" PRIX64 " - 0x%" PRIX64 " => 0x%" PRIX64 " - 0x%" PRIX64 " [%s] type [%d]\n",
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

void shadow_page_directory::refresh(context* ctx, uint32_t channel_id, pramin_accessor* pramin, uint64_t page_directory_address) {
    struct page_directory virt = { { } };
    virt.word0 = pramin->read32(page_directory_address);
    virt.word1 = pramin->read32(page_directory_address + 0x4);
    virt_ = virt;

    if (virt.large_page_table_present) {
        const uint64_t address = static_cast<uint64_t>(virt.large_page_table_address) << 12;
        const uint64_t count = kPAGE_DIRECTORY_COVERED_SIZE >> kLARGE_PAGE_SHIFT;
        large_entries_.resize(count);
        std::size_t i = 0;
        for (shadow_page_entries::iterator it = large_entries_.begin(),
             last = large_entries_.end(); it != last; ++it, ++i) {
            it->refresh(pramin, address + 0x8 * i);
        }
        for (uint64_t it = address, end = address + 0x8 * count; it < end; it += 0x1000) {
            // ctx->remapping()->map(it, 0, true);
        }
    } else {
        large_entries_.clear();
    }

    if (virt.small_page_table_present) {
        const uint64_t address = static_cast<uint64_t>(virt.small_page_table_address) << 12;
        const uint64_t count = kPAGE_DIRECTORY_COVERED_SIZE >> kSMALL_PAGE_SHIFT;
        // const uint64_t size = count * sizeof(page_entry);
        small_entries_.resize(count);
        std::size_t i = 0;
        for (shadow_page_entries::iterator it = small_entries_.begin(),
             last = small_entries_.end(); it != last; ++it, ++i) {
            it->refresh(pramin, address + 0x8 * i);
        }
        for (uint64_t it = address, end = address + 0x8 * count; it < end; it += 0x1000) {
            // ctx->remapping()->map(it, 0, true);
        }
    } else {
        small_entries_.clear();
    }

    // TODO(Yusuke Suzuki)
    // Calculate physical page address
    phys_ = virt;
}

uint64_t shadow_page_directory::resolve(uint64_t offset) {
    if (virt().large_page_table_present) {
        const uint64_t index = offset / kLARGE_PAGE_SIZE;
        const uint64_t rest = offset % kLARGE_PAGE_SIZE;
        if (large_entries_.size() > index) {
            const struct shadow_page_entry& entry = large_entries_[index];
            if (entry.present()) {
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
                const uint64_t address = entry.virt().address;
                return (address << 12) + rest;
            }
        }
    }

    return UINT64_MAX;
}

bool shadow_page_entry::refresh(pramin_accessor* pramin, uint64_t page_entry_address) {
    struct page_entry virt = { };
    virt.word0 = pramin->read32(page_entry_address);
    virt.word1 = pramin->read32(page_entry_address + 0x4);
    virt_ = virt;

    // TODO(Yusuke Suzuki)
    // Calculate physical page address
    phys_ = virt;
    return true;
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */