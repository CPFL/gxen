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
#include "cross_barrier.h"
#include "cross_pramin.h"
#include "cross_shadow_page_table.h"
namespace cross {


context::context(boost::asio::io_service& io_service)
    : session(io_service)
    , accepted_(false)
    , id_()
    , bar1_table_(new shadow_page_table(-1))
    , bar3_table_(new shadow_page_table(-3))
    , barrier_(new barrier::table(CROSS_2G))
    , poll_area_(0)
    , reg_(new uint32_t[32ULL * 1024 * 1024])
    , reg_pramin_(0)
    , reg_poll_(0)
    , reg_channel_kill_(0)
    , reg_tlb_vspace_(0)
    , reg_tlb_trigger_(0) {
    barrier()->map(bar1_table_->channel_address());
    barrier()->map(bar3_table_->channel_address());
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
    id_ = 1;  // FIXME debug id
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
    }
}

void context::fifo_playlist_update(uint64_t address, uint32_t count) {
    // scan fifo and update values
    {
        pramin::accessor pramin;
        uint32_t i;
        printf("FIFO playlist update %u\n", count);
        for (i = 0; i < count; ++i) {
            const uint32_t cid = pramin.read32(address + i * 0x8);
            printf("FIFO playlist cid %u => %u\n", cid, get_phys_channel_id(cid));
            pramin.write32(address + i * 0x8, get_phys_channel_id(cid));
            pramin.write32(address + i * 0x8 + 0x4, 0x4);
        }
    }
    registers::write32(0x70000, 1);
    // FIXME(Yusuke Suzuki): BAR flush wait code is needed?
    // usleep(1000);
}

void context::flush_tlb(uint32_t vspace, uint32_t trigger) {
    const uint64_t page_directory = bit_mask<28, uint64_t>(vspace >> 4) << 12;
    // rescan page tables
    if (bar1_table()->page_directory_address() == page_directory) {
        // BAR1
        bar1_table()->refresh_page_directories(this, page_directory);
    }
    if (bar3_table()->page_directory_address() == page_directory) {
        // BAR3
        bar3_table()->refresh_page_directories(this, page_directory);
    }
    registers::accessor registers;
    registers.write32(0x100cb8, vspace);
    registers.write32(0x100cbc, trigger);
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
