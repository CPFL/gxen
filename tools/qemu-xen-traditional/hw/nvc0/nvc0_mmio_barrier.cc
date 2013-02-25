/*
 * NVIDIA NVC0 MMIO barrier
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

#include <utility>
#include "nvc0_mmio_barrier.h"

namespace nvc0 {

bool mmio_barrier::handle(uint64_t address) {
    barrier_map::const_iterator it = barriers_.upper_bound(interval(address, address));

    if (it == barriers_.begin()) {
        return false;
    }
    --it;

    if (it->first.second <= address) {
        return false;
    }

    NVC0_PRINTF("handling 0x%" PRIX64 " access\n", address);
    return true;
}

void mmio_barrier::clear(uint32_t type) {
    const std::pair<item_map::iterator, item_map::iterator> pair = items_.equal_range(type);
    for (item_map::const_iterator it = pair.first, last = pair.second; it != last; ++it) {
        barriers_.erase(it->second);
    }
    items_.erase(pair.first, pair.second);
}

void mmio_barrier::register_barrier(uint32_t type, interval i) {
    barriers_.insert(std::make_pair(i, type));
    items_.insert(std::make_pair(type, i));
}

}  // namespace nvc0
/* vim: set sw=4 ts=4 et tw=80 : */
