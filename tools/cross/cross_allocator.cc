/*
 * Cross Context
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
#include <cstdlib>
#include <iostream>
#include <boost/asio.hpp>
#include <unistd.h>
#include "cross.h"
#include "cross_session.h"
#include "cross_context.h"
#include "cross_device.h"
#include "cross_allocator.h"
#include "cross_pramin.h"
namespace cross {

// very naive way
allocator::allocator(uint64_t start, uint64_t end)
    : start_(start)
    , size_(end - start)
    , vector_(size_ / kPageSize, -1) {
}

uint64_t allocator::allocate() {
    const boost::dynamic_bitset<>::size_type pos = vector_.find_first();
    if (pos == vector_.npos) {
        assert(0);
        return 0;  // invaid
    }
    vector_.reset(pos);
    return pos * kPageSize + start_;
}

void allocator::deallocate(uint64_t addr) {
    vector_.set(((addr - start_) / kPageSize));
}

page::page()
    : address_() {
    CROSS_SYNCHRONIZED(device::instance()->mutex_handle()) {
        address_ = device::instance()->memory()->allocate();
    }
    clear();
}

page::~page() {
    CROSS_SYNCHRONIZED(device::instance()->mutex_handle()) {
        device::instance()->memory()->deallocate(address());
    }
}

void page::clear() {
    pramin::accessor pramin;
    for (std::size_t i = 0; i < (0x1000 / sizeof(uint32_t)); i += sizeof(uint32_t)) {
        pramin.write32(address() + i, 0);
    }
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
