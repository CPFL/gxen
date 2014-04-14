/*
 * A3 chipset
 *
 * Copyright (c) 2012-2014 Yusuke Suzuki
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
#include "a3.h"
#include "chipset.h"
#include "registers.h"
namespace a3 {

chipset_t::chipset_t(uint32_t boot0)
    : value_()
    , type_()
{
    value_ = (boot0 & 0xff00000) >> 20;
    switch (value_ & 0xf0) {
    case 0xc0:
        type_ = card::NVC0;
        break;
    case 0xe0:
    case 0xf0:
        type_ = card::NVE0;
        break;
    default:
        break;
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
