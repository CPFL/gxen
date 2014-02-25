/*
 * A3 barrier table
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

#include "barrier.h"
#include "bit_mask.h"
#include "assertion.h"
namespace a3 {
namespace barrier {

table::table(uint64_t base, uint64_t memory_size)
    : table_()
    , base_(base)
    , size_(bit_mask<kADDRESS_BITS>(memory_size)) {
    if (memory_size == 0) {
        return;
    }
    const uint32_t directories_size = bit_mask<kPAGE_DIRECTORY_BITS>((size_ - 1) >> (kPAGE_BITS + kPAGE_DIRECTORY_BITS)) + 1;
    table_.resize(directories_size);
}

bool table::in_range(uint64_t address) const {
    return base() <= address && address < (base() + size());
}

bool table::map(uint64_t page_start_address) {
    page_entry* entry = nullptr;
    const bool result = lookup(page_start_address, &entry, true);
    if (entry) {
        entry->retain();
    }
    return result;
}

bool table::unmap(uint64_t page_start_address) {
    page_entry* entry = nullptr;
    lookup(page_start_address, &entry, false);
    if (entry) {
        entry->release();
        return entry->present();
    }
    return false;
}

bool table::lookup(uint64_t address, page_entry** entry, bool force_create) {
    // out of range
    if (!in_range(address)) {
        return false;
    }

    // to virtual address
    address -= base();

    const uint32_t index = bit_mask<kPAGE_DIRECTORY_BITS>(address >> (kPAGE_BITS + kPAGE_DIRECTORY_BITS));
    directory& dir = table_[index];
    if (!dir) {
        if (!force_create) {
            return false;
        }

        dir.reset(new page_directory);
    }
    ASSERT(dir);
    return dir->lookup(address, entry);
}

bool page_directory::map(uint64_t page_start_address) {
    page_entry* entry = nullptr;
    const bool result = lookup(page_start_address, &entry);
    entry->retain();
    return result;
}

void page_directory::unmap(uint64_t page_start_address) {
    page_entry* entry = nullptr;
    if (lookup(page_start_address, &entry)) {
        entry->release();
    }
}

bool page_directory::lookup(uint64_t address, page_entry** entry) {
    const uint32_t index = bit_mask<kPAGE_ENTRY_BITS>(address >> kPAGE_BITS);
    *entry = &entries_[index];
    return (*entry)->present();
}

} }  // namespace a3::barrier
/* vim: set sw=4 ts=4 et tw=80 : */
