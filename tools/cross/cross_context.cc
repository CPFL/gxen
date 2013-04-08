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
#include "cross_device_bar1.h"
#include "cross_shadow_page_table.h"
namespace cross {


context::context(boost::asio::io_service& io_service)
    : session(io_service)
    , accepted_(false)
    , id_()
    , bar1_channel_(new shadow_bar1(this))
    , bar3_channel_(new channel(-3))
    , channels_()
    , barrier_()
    , poll_area_(0)
    , reg_(new uint32_t[32ULL * 1024 * 1024])
    , fifo_playlist_() {
    for (std::size_t i = 0, iz = channels_.size(); i < iz; ++i) {
        channels_[i].reset(new channel(i));
    }
}

context::~context() {
    if (accepted_) {
        device::instance()->release_virt(id_);
        CROSS_LOG("END and release GPU id %u\n", id_);
    }
}

void context::accept() {
    accepted_ = true;
    id_ = device::instance()->acquire_virt();
    // id_ = 1;  // FIXME debug id
    barrier_.reset(new barrier::table(get_address_shift(), vram_size()));
    fifo_playlist_.reset(new page());
}

void context::handle(const command& cmd) {
    switch (cmd.type) {
    case command::TYPE_INIT:
        domid_ = cmd.value;
        CROSS_LOG("INIT domid %d & GPU id %u\n", domid(), id());

        // domid is not passed
        device::instance()->try_acquire_gpu(this);
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

void context::fifo_playlist_update(uint32_t reg_addr, uint32_t reg_count) {
    const uint64_t address = get_phys_address(bit_mask<28, uint64_t>(reg_addr) << 12);
    const uint32_t count = bit_mask<8, uint32_t>(reg_count);

    // scan fifo and update values
    {
        // TODO(Yusuke Suzuki): Virtualized playlist area
        pramin::accessor pramin;
        uint32_t i;
        CROSS_LOG("FIFO playlist update %u\n", count);
        for (i = 0; i < count; ++i) {
            const uint32_t cid = pramin.read32(address + i * 0x8);
            CROSS_LOG("FIFO playlist cid %u => %u\n", cid, get_phys_channel_id(cid));
            fifo_playlist_->write32(i * 0x8 + 0x0, get_phys_channel_id(cid));
            fifo_playlist_->write32(i * 0x8 + 0x4, 0x4);
#if 0
            pramin.write32(address + i * 0x8, get_phys_channel_id(cid));
            pramin.write32(address + i * 0x8 + 0x4, 0x4);
#endif
        }
    }

    registers::write32(0x70000, 1);
    // FIXME(Yusuke Suzuki): BAR flush wait code is needed?
    // usleep(1000);

    // registers::write32(0x2270, address >> 12);
    registers::write32(0x2270, fifo_playlist_->address() >> 12);
    registers::write32(0x2274, reg_count);
}

void context::flush_tlb(uint32_t vspace, uint32_t trigger) {
    const uint64_t page_directory = get_phys_address(bit_mask<28, uint64_t>(vspace >> 4) << 12);
    const uint64_t vspace_phys = bit_clear<4, uint32_t>(vspace) | static_cast<uint32_t>(page_directory >> 8);

    bool bar1 = false;
    bool bar1_only = true;

    // rescan page tables
    if (bar1_channel()->table()->page_directory_address() == page_directory) {
        // BAR1
        bar1 = true;
        bar1_channel()->table()->refresh_page_directories(this, page_directory);
        CROSS_SYNCHRONIZED(device::instance()->mutex_handle()) {
            device::instance()->bar1()->shadow(this);
        }
    }

    if (bar3_channel()->table()->page_directory_address() == page_directory) {
        // BAR3
        bar1_only = false;
        bar3_channel()->table()->refresh_page_directories(this, page_directory);
    }
    for (std::size_t i = 0, iz = channels_.size(); i < iz; ++i) {
        if (channels(i)->enabled()) {
            if (channels(i)->table()->page_directory_address() == page_directory) {
                bar1_only = false;
                channels(i)->table()->refresh_page_directories(this, page_directory);
            }
        }
    }

    if (bar1) {
        device::instance()->bar1()->flush();
        if (bar1_only) {
            return;
        }
    }

    registers::accessor registers;
    registers.write32(0x100cb8, vspace_phys);
    registers.write32(0x100cbc, trigger);
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
