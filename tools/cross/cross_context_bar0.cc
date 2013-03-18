/*
 * Cross Context BAR0
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
#include "cross_context.h"
#include "cross_registers.h"
#include "cross_shadow_page_table.h"
namespace cross {

void context::write_bar0(const command& cmd) {
    switch (cmd.offset) {
    case 0x001700: {
            // PRAMIN
            registers_accessor regs;
            regs.write32(0x1700, cmd.value);
            break;
        }
    case 0x001704: {
            // BAR1 VM
            bar1_table_->refresh(this, cmd.value);
            break;
        }
    case 0x001714: {
            // BAR3 VM
            bar3_table_->refresh(this, cmd.value);
            break;
        }
    }
}

void context::read_bar0(const command& cmd) {
    switch (cmd.offset) {
    }
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
