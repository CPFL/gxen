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
#include "cross_bit_mask.h"
#include "cross_registers.h"
#include "cross_shadow_page_table.h"
namespace cross {


context::context(boost::asio::io_service& io_service)
    : session(io_service)
    , accepted_(false)
    , id_()
    , bar1_table_(new shadow_page_table(-1))
    , bar3_table_(new shadow_page_table(-3))
    , reg_pramin_(0) {
}

context::~context() {
    if (accepted_) {
        device::instance()->release_virt(id_);
        std::cout << "END and release GPU id " << id_ << std::endl;
    }
}

void context::accept() {
    accepted_ = true;
    id_ = device::instance()->acquire_virt();
}

void context::handle(const command& cmd) {
    switch (cmd.type) {
    case command::TYPE_INIT:
        domid_ = cmd.value;
        std::cout << "INIT domid " << domid_ << " & GPU id " << id_ << std::endl;
        break;

    case command::TYPE_WRITE:
        switch (cmd.payload) {
            case command::BAR0:
                write_bar0(cmd);
                break;
            case command::BAR1:
                write_bar1(cmd);
                break;
            case command::BAR3:
                write_bar3(cmd);
                break;
        }
        break;

    case command::TYPE_READ:
        switch (cmd.payload) {
            case command::BAR0:
                read_bar0(cmd);
                break;
            case command::BAR1:
                read_bar1(cmd);
                break;
            case command::BAR3:
                read_bar3(cmd);
                break;
        }
        break;

    case command::TYPE_TLB_FLUSH: {
            const uint64_t page_directory = bit_mask<28, uint64_t>(cmd.value >> 4) << 12;
            // rescan page tables
            if (bar1_table()->page_directory_address() == page_directory) {
                // BAR1
                bar1_table()->refresh_page_directories(this, page_directory);
            }
            if (bar3_table()->page_directory_address() == page_directory) {
                // BAR3
                bar3_table()->refresh_page_directories(this, page_directory);
            }

            registers_accessor registers;
            registers.write32(0x100cb8, cmd.value);   // vspace
            registers.write32(0x100cbc, cmd.offset);  // trigger
        }
        break;
    }
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
