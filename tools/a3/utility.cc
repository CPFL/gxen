/*
 * A3 utility
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
#include "utility.h"
#include <cstdio>
#include <string>
#include <boost/format.hpp>
namespace a3 {

std::string examine(command cmd, uint32_t value) {
    if (cmd.type != command::TYPE_READ && cmd.type != command::TYPE_WRITE) {
        return "";
    }
    const char RW = (cmd.type == command::TYPE_READ) ? 'R' : 'W';
    const uint32_t v = (cmd.type == command::TYPE_READ) ? value : cmd.value;
    return boost::str(boost::format("[%c] BAR%d 0x%08X 0x%08X") % RW % cmd.bar() % cmd.offset % v);
}

void inspect(command cmd, uint32_t value) {
    const std::string str = examine(cmd, value);
    if (str.empty()) {
        return;
    }
    A3_RAW_FPRINTF(stdout, "I %s\n", str.c_str());
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
