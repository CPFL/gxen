/*
 * A3 NVC0 PRAMIN
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
#include "a3_pramin.h"
#include "a3_device.h"
#include "a3_bit_mask.h"
namespace a3 {
namespace pramin {

accessor::accessor()
    : regs_()
    , old_(regs_.read32(0x1700)) {
}

accessor::~accessor() {
    regs_.write32(0x1700, old_);
}

uint32_t accessor::read(uint64_t addr, std::size_t size) {
    change_current(addr);
    return regs_.read(0x700000 + bit_mask<16>(addr), size);
}

void accessor::write(uint64_t addr, uint32_t val, std::size_t size) {
    change_current(addr);
    regs_.write(0x700000 + bit_mask<16>(addr), val, size);
}

uint32_t accessor::read32(uint64_t addr) {
    return read(addr, sizeof(uint32_t));
}

void accessor::write32(uint64_t addr, uint32_t val) {
    write(addr, val, sizeof(uint32_t));
}

uint32_t accessor::read16(uint64_t addr) {
    return read(addr, sizeof(uint16_t));
}

void accessor::write16(uint64_t addr, uint16_t val) {
    write(addr, val, sizeof(uint16_t));
}

uint32_t accessor::read8(uint64_t addr) {
    return read(addr, sizeof(uint8_t));
}

void accessor::write8(uint64_t addr, uint8_t val) {
    write(addr, val, sizeof(uint8_t));
}

void accessor::change_current(uint64_t addr) {
    const uint64_t shifted = (addr >> 16);
//    if (a3::device::instance()->pramin() != shifted) {
//        a3::device::instance()->set_pramin(shifted);
        regs_.write32(0x1700, shifted);
//    }
}

} }  // namespace a3::pramin
/* vim: set sw=4 ts=4 et tw=80 : */
