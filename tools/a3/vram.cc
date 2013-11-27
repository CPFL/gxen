/*
 * A3 NVC0 VRAM pool
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
#include "vram.h"
namespace a3 {

vram_manager_t::vram_manager_t(uint64_t mem, uint64_t size)
    : mem_(mem)
    , size_(size)
    , cursor_(0) {
}

// K&R malloc

bool vram_manager_t::more(std::size_t n) {
    // round with 32
    const std::size_t pages = ((n + 32 - 1) / 32) * 32;
    const uint64_t address = mem_ + cursor_ * kPAGE_SIZE;
    cursor_ += pages;
    if (cursor_ > max_pages()) {
        return false;
    }

    free(new vram_t(address, pages));
    return true;
}

vram_t* vram_manager_t::malloc(std::size_t n) {
    do {
        for (auto& entry : free_list_) {
            if (entry.units_ >= n) {
                if (entry.units_ == n) {
                    free_list_.erase(free_list_t::s_iterator_to(entry));
                    return &entry;
                }
                entry.units_ -= n;
                return new vram_t(entry.address_ + entry.units_ * kPAGE_SIZE, n);
            }
        }
        if (!more(n)) {
            std::abort();
        }
    } while (true);
}

void vram_manager_t::free(vram_t* entry) {
    const auto range = std::equal_range(free_list_.begin(), free_list_.end(), *entry, [](const vram_t& i, const vram_t& j) {
        return i.address_ < j.address_;
    });
    auto& prev = range.first;
    auto& next = range.second;
    free_list_.insert(next, *entry);
    if (next != free_list_.end() && (entry->address_ + entry->units_ * kPAGE_SIZE) == next->address_) {
        // join current and next
        entry->units_ += next->units_;
        vram_t* del = &*next;
        free_list_.erase(free_list_t::s_iterator_to(*del));
        delete del;
    }
    if (prev != free_list_.end() && (prev->address_ + prev->units_ * kPAGE_SIZE) == entry->address_) {
        prev->units_ += entry->units_;
        free_list_.erase(free_list_t::s_iterator_to(*entry));
        delete entry;
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
