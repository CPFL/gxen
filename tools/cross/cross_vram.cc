/*
 * Cross NVC0 VRAM pool
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
#include "cross_vram.h"
namespace cross {

char* gpu_memory_allocator::base = NULL;

vram::vram(uint64_t mem, uint64_t size)
    : mem_(mem)
    , size_(size)
    , pool_(sizeof(void*), size / 0x1000, size / 0x1000) {
}

uint64_t vram::encode(void* ptr) {
    const uint64_t data = reinterpret_cast<uint64_t>(ptr);
    const uint64_t offset = (data - reinterpret_cast<uint64_t>(gpu_memory_allocator::base)) / sizeof(void*) * 0x1000;
    return mem_ + offset;
}

void* vram::decode(uint64_t address) {
    const uint64_t offset = address - mem_;
    const uint64_t data = (offset / 0x1000 * sizeof(void*)) + reinterpret_cast<uint64_t>(gpu_memory_allocator::base);
    return reinterpret_cast<void*>(data);
}

vram_memory* vram::malloc(std::size_t n) {
    void* ptr;
    if (n == 1) {
        ptr = pool_.malloc();
    } else {
        ptr = pool_.ordered_malloc(n);
    }

    return new vram_memory(encode(ptr), n);
}

void vram::free(vram_memory* mem) {
    if (!mem) {
        return;
    }
    void* ptr = decode(mem->address());
    if (mem->n() == 1) {
        pool_.free(ptr);
    } else {
        pool_.ordered_free(ptr, mem->n());
    }
    delete mem;
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
