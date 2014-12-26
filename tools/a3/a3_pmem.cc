/*
 * A3 NVC0 PMEM
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
#include "a3_pmem.h"
#include "a3_device.h"
#include "a3_bit_mask.h"
namespace a3 {
namespace pmem {

uint32_t accessor::read(uint64_t addr, std::size_t size) {
    return device::instance()->read_pmem_locked(addr, size);
}

void accessor::write(uint64_t addr, uint32_t val, std::size_t size) {
    device::instance()->write_pmem_locked(addr, val, size);
}

void accessor::read_pages(void* ptr, uint64_t addr, size_t n) {
    device::instance()->read_pages_pmem_locked(ptr, addr, n);
}

void accessor::write_pages(const void* ptr, uint64_t addr, size_t n) {
    device::instance()->write_pages_pmem_locked(ptr, addr, n);
}

} }  // namespace a3::pmem
/* vim: set sw=4 ts=4 et tw=80 : */
