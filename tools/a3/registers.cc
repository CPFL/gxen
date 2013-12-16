/*
 * A3 NVC0 registers accessor
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
#include <functional>
#include "registers.h"
#include "device.h"
namespace a3 {
namespace registers {

accessor::accessor()
    : lock_(a3::device()->mutex()) {
}

uint32_t accessor::read(uint32_t offset, std::size_t size) {
    return a3::device()->read(0, offset, size);
}

void accessor::write(uint32_t offset, uint32_t val, std::size_t size) {
    a3::device()->write(0, offset, val, size);
}

uint32_t accessor::read32(uint32_t offset) {
    return read(offset, sizeof(uint32_t));
}

void accessor::write32(uint32_t offset, uint32_t val) {
    write(offset, val, sizeof(uint32_t));
}

uint32_t accessor::read16(uint32_t offset) {
    return read(offset, sizeof(uint16_t));
}

void accessor::write16(uint32_t offset, uint16_t val) {
    write(offset, val, sizeof(uint16_t));
}

uint32_t accessor::read8(uint32_t offset) {
    return read(offset, sizeof(uint8_t));
}

void accessor::write8(uint32_t offset, uint8_t val) {
    write(offset, val, sizeof(uint8_t));
}

uint32_t accessor::mask32(uint32_t offset, uint32_t mask, uint32_t val) {
    const uint32_t tmp = read32(offset);
    write32(offset, (tmp & ~mask) | val);
    return tmp;
}

bool accessor::wait_eq(uint32_t offset, uint32_t mask, uint32_t val) {
    return wait_cb(offset, mask, val, std::equal_to<uint32_t>());
}

bool accessor::wait_ne(uint32_t offset, uint32_t mask, uint32_t val) {
    return wait_cb(offset, mask, val, std::not_equal_to<uint32_t>());
}

} }  // namespace a3::registers
/* vim: set sw=4 ts=4 et tw=80 : */
