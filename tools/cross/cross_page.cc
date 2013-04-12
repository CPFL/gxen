/*
 * Cross Page
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
#include <cstdio>
#include "cross.h"
#include "cross_page.h"
#include "cross_pramin.h"
namespace cross {

page::page(std::size_t n)
    : vram_() {
    CROSS_SYNCHRONIZED(device::instance()->mutex_handle()) {
        vram_ = device::instance()->malloc(n);
    }
    clear();
}

page::~page() {
    CROSS_SYNCHRONIZED(device::instance()->mutex_handle()) {
        device::instance()->free(vram_);
    }
}

void page::clear() {
    pramin::accessor pramin;
    for (std::size_t i = 0; i < (size() / sizeof(uint32_t)); i += sizeof(uint32_t)) {
        pramin.write32(address() + i, 0);
    }
}

void page::write32(uint64_t offset, uint32_t value) {
    assert(offset < size());
    pramin::write32(address() + offset, value);
}

uint32_t page::read32(uint64_t offset) {
    assert(offset < size());
    return pramin::read32(address() + offset);
}

std::size_t page::page_size() const {
    return vram_->n();
}

uint64_t page::size() const {
    return page_size() * 0x1000ULL;
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
