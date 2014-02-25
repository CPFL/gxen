/*
 * A3 Page
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
#include "a3.h"
#include "page.h"
#include "pmem.h"
namespace a3 {

page::page(std::size_t n)
    : vram_() {
    A3_SYNCHRONIZED(device()->mutex()) {
        vram_ = device()->malloc(n);
    }
}

page::~page() {
    A3_SYNCHRONIZED(device()->mutex()) {
        device()->free(vram_);
    }
}

void page::clear() {
    pmem::accessor pmem;
    for (std::size_t i = 0; i < (size() / sizeof(uint32_t)); i += sizeof(uint32_t)) {
        pmem.write32(address() + i, 0);
    }
}

void page::write32(uint64_t offset, uint32_t value) {
    ASSERT(offset < size());
    pmem::write32(address() + offset, value);
}

uint32_t page::read32(uint64_t offset) {
    ASSERT(offset < size());
    return pmem::read32(address() + offset);
}

void page::write(uint64_t offset, uint32_t value, std::size_t s) {
    ASSERT(offset < size());
    pmem::accessor pmem;
    pmem.write(address() + offset, value, s);
}

uint32_t page::read(uint64_t offset, std::size_t s) {
    ASSERT(offset < size());
    pmem::accessor pmem;
    return pmem.read(address() + offset, s);
}

std::size_t page::page_size() const {
    return vram_->n();
}

uint64_t page::size() const {
    return page_size() * 0x1000ULL;
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
