/*
 * NVIDIA cross remapping table
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

#include <assert.h>
#include "cross_remapping.h"
#include "cross_bit_mask.h"
namespace cross {
namespace remapping {

table::table(uint64_t memory_size)
    : table_()
    , size_(bit_mask<kADDRESS_BITS>(memory_size)) {
    if (memory_size == 0) {
        return;
    }
    const uint32_t directories_size = bit_mask<kPAGE_DIRECTORY_BITS>((size_ - 1) >> (kPAGE_BITS + kPAGE_DIRECTORY_BITS)) + 1;
    table_.resize(directories_size);
}

bool table::map(uint64_t page_start_address, uint64_t result_start_address, bool read_only) {
    // out of range
    if (page_start_address >= size_) {
        return false;
    }
    const uint32_t index = bit_mask<kPAGE_DIRECTORY_BITS>(page_start_address >> (kPAGE_BITS + kPAGE_DIRECTORY_BITS));
    directory& dir = table_[index];
    if (!dir) {
        dir.reset(new page_directory);
    }
    assert(dir);
    return dir->map(page_start_address, result_start_address, read_only);
}

void table::unmap(uint64_t page_start_address) {
    // out of range
    if (page_start_address >= size_) {
        return;
    }
    const uint32_t index = bit_mask<kPAGE_DIRECTORY_BITS>(page_start_address >> (kPAGE_BITS + kPAGE_DIRECTORY_BITS));
    directory& dir = table_[index];
    if (!dir) {
        return;
    }
    assert(dir);
    return dir->unmap(page_start_address);
}

bool table::lookup(uint64_t address, page_entry* entry) const {
    if (address >= size_) {
        return false;
    }
    const uint32_t index = bit_mask<kPAGE_DIRECTORY_BITS>(address >> (kPAGE_BITS + kPAGE_DIRECTORY_BITS));
    const directory& dir = table_[index];
    if (!dir) {
        return false;
    }
    assert(dir);
    return dir->lookup(address, entry);
}

bool page_directory::map(uint64_t page_start_address, uint64_t result_start_address, bool read_only) {
    const uint32_t index = bit_mask<kPAGE_ENTRY_BITS>(page_start_address >> kPAGE_BITS);
    page_entry& entry = entries_[index];
    const bool result = entry.present;
    entry.present = true;
    entry.read_only = read_only;
    entry.target = (result_start_address >> kPAGE_BITS);
    return result;
}

void page_directory::unmap(uint64_t page_start_address) {
    const uint32_t index = bit_mask<kPAGE_ENTRY_BITS>(page_start_address >> kPAGE_BITS);
    page_entry& entry = entries_[index];
    entry.present = false;
}

bool page_directory::lookup(uint64_t address, page_entry* entry) const {
    const uint32_t index = bit_mask<kPAGE_ENTRY_BITS>(address >> kPAGE_BITS);
    *entry = entries_[index];
    return entry->present;
}

} }  // namespace cross::remapping
/* vim: set sw=4 ts=4 et tw=80 : */
