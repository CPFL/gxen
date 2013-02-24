/*
 * NVIDIA NVC0 shadow page table
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

#include "nvc0_shadow_page_table.h"
#include "nvc0_pramin.h"
namespace nvc0 {

void shadow_page_table::refresh(nvc0_state_t* state, uint64_t ramin) {
    // construct shadow page table from real data
    pramin_accessor pramin(state);

    page_descriptor descriptor;
    descriptor.page_directory_address_low = pramin.read32(ramin + 0x200);
    descriptor.page_directory_address_high = pramin.read32(ramin + 0x204);
    descriptor.page_limit_low = pramin.read32(ramin + 0x208);
    descriptor.page_limit_high = pramin.read32(ramin + 0x20c);

    NVC0_PRINTF("page directory address 0x%" PRIx64 " and size %" PRIu64 "\n", descriptor.page_directory_address, descriptor.page_limit);

}

void shadow_page_table::set_low_size(uint32_t value) {
    low_size_ = value;
}

void shadow_page_table::set_high_size(uint32_t value) {
    high_size_ = value;
}

}  // namespace nvc
/* vim: set sw=4 ts=4 et tw=80 : */
