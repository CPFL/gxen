/*
 * A3 Context
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
#include "a3.h"
#include "a3_session.h"
#include "a3_context.h"
#include "a3_device.h"
#include "a3_bit_mask.h"
#include "a3_registers.h"
#include "a3_barrier.h"
#include "a3_pmem.h"
#include "a3_device_bar1.h"
#include "a3_device_bar3.h"
#include "a3_shadow_page_table.h"
#include "a3_software_page_table.h"
#include "a3_page_table.h"
#include "a3_pv_page.h"
namespace a3 {

context::context(session* s, bool through)
    : session_(s)
    , through_(through)
    , initialized_(false)
    , id_()
    , bar1_channel_()
    , bar3_channel_()
    , channels_()
    , barrier_()
    , poll_area_(0)
    , reg32_()
    , ramin_channel_map_()
    , bar3_address_()
    , para_virtualized_(false)
    , pv32_()
    , guest_()
    , pgds_()
    , pv_bar1_pgd_()
    , pv_bar1_large_pgt_()
    , pv_bar1_small_pgt_()
    , pv_bar3_pgd_()
    , pv_bar3_pgt_()
{
}

context::~context() {
    if (initialized_) {
        device::instance()->release_virt(id_);
        A3_LOG("END and release GPU id %u\n", id_);
    }
}

void context::initialize(int dom, bool para) {
    id_ = device::instance()->acquire_virt();
    domid_ = dom;
    para_virtualized_ = para;
    if (para_virtualized()) {
        pv32_.reset(new uint32_t[A3_BAR4_SIZE / sizeof(uint32_t)]);
    }
    bar1_channel_.reset(new fake_channel(this, -1, kBAR1_ARENA_SIZE));
    bar3_channel_.reset(new fake_channel(this, -3, kBAR3_ARENA_SIZE));
    barrier_.reset(new barrier::table(get_address_shift(), vram_size()));
    reg32_.reset(new uint32_t[A3_BAR0_SIZE / sizeof(uint32_t)]);
    for (std::size_t i = 0, iz = channels_.size(); i < iz; ++i) {
        channels_[i].reset(new channel(i));
    }
    initialized_ = true;
    A3_LOG("INIT domid %d & GPU id %u with %s\n", domid(), id(), para_virtualized() ? "Para-virt" : "Full-virt");
    buffer()->value = id();
    session_->initialize(id());
}

// main entry
bool context::handle(const command& cmd) {
    if (cmd.type == command::TYPE_INIT) {
        initialize(cmd.value, cmd.offset != 0);
        return false;
    }

    if (cmd.type == command::TYPE_BAR3) {
        uint64_t tmp = static_cast<uint64_t>(cmd.value) << 12;
        tmp += cmd.offset;
        bar3_address_ = tmp;
        A3_LOG("BAR3 address notification %" PRIx64 "\n", bar3_address());
        return false;
    }

    if (cmd.type == command::TYPE_UTILITY) {
        switch (cmd.value) {
        case command::UTILITY_REGISTER_READ: {
                const uint32_t status = registers::read32(cmd.offset);
                buffer()->value = status;
            }
            break;

        case command::UTILITY_PGRAPH_STATUS: {
                registers::accessor regs;
                const uint32_t status = regs.read32(0x400700);
                buffer()->value = status;
                A3_LOG("status %" PRIx32 "\n", status);
                for (uint32_t pid = 0; pid < 128; ++pid) {
                    const uint32_t offset = 0x3000 + 0x8 * pid + 0x4;
                    const uint32_t status = regs.read32(offset);
                    A3_LOG("chan%0u => %" PRIx32 "\n", pid, status);
                }
            }
            break;
        }
        return false;
    }

    if (through()) {
        A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
            // through mode. direct access
            const uint32_t bar = cmd.bar();
            if (cmd.type == command::TYPE_WRITE) {
                device::instance()->write(bar, cmd.offset, cmd.value, cmd.size());
                return false;
            } else if (cmd.type == command::TYPE_READ) {
                buffer()->value = device::instance()->read(bar, cmd.offset, cmd.size());
                return true;
            }
        }
        return false;
    }

    if (cmd.type == command::TYPE_WRITE) {
        switch (cmd.bar()) {
        case command::BAR0:
            write_bar0(cmd);
            // A3_LOG("BAR0 write 0x%" PRIx32 " 0x%" PRIx32 "\n", cmd.offset, cmd.value);
            break;
        case command::BAR1:
            write_bar1(cmd);
            // A3_LOG("BAR1 write 0x%" PRIx32 " 0x%" PRIx32 "\n", cmd.offset, cmd.value);
            break;
        case command::BAR3:
            write_bar3(cmd);
            // A3_LOG("BAR3 write 0x%" PRIx32 " 0x%" PRIx32 "\n", cmd.offset, cmd.value);
            break;
        case command::BAR4:
            write_bar4(cmd);
            return true; // speicialized
        }
        return false;
    }

    if (cmd.type == command::TYPE_READ) {
        switch (cmd.bar()) {
        case command::BAR0:
            read_bar0(cmd);
            // A3_LOG("BAR0 read  0x%" PRIx32 " 0x%" PRIx32 "\n", cmd.offset, buffer()->value);
            break;
        case command::BAR1:
            read_bar1(cmd);
            // A3_LOG("BAR1 read  0x%" PRIx32 " 0x%" PRIx32 "\n", cmd.offset, buffer()->value);
            break;
        case command::BAR3:
            read_bar3(cmd);
            A3_LOG("BAR3 read  0x%" PRIx32 " 0x%" PRIx32 "\n", cmd.offset, buffer()->value);
            break;
        case command::BAR4:
            read_bar4(cmd);
            return true;
        }
        return true;
    }

    return false;
}

void context::playlist_update(uint32_t reg_addr, uint32_t cmd) {
    const uint64_t address = get_phys_address(bit_mask<28, uint64_t>(reg_addr) << 12);
    device::instance()->playlist_update(this, address, cmd);
}

static bool flush_without_check(uint64_t pd, uint32_t engine) {
    registers::accessor registers;
    if (!registers.wait_ne(0x100c80, 0x00ff0000, 0x00000000)) {
        A3_LOG("error on wait flush 2\n");
        return false;
    }
    registers.write32(0x100cb8, pd >> 8);
    registers.write32(0x100cbc, 0x80000000 | engine);
    return true;
}

bool context::flush(uint64_t pd, bool bar) {
    registers::accessor registers;
    uint32_t engine = 1;
    if (bar) {
        engine |= 4;
    }
    if (!flush_without_check(pd, engine)) {
        return false;
    }
    if (!registers.wait_eq(0x100c80, 0x00008000, 0x00008000)) {
        A3_LOG("error on wait flush 1\n");
        return false;
    }
    return true;
}

void context::flush_tlb(uint32_t vspace, uint32_t trigger) {
    const uint64_t page_directory = get_phys_address(bit_mask<28, uint64_t>(vspace >> 4) << 12);

    uint64_t already = 0;
    channel::page_table_reuse_t* reuse;

    A3_LOG("TLB flush 0x%" PRIX64 " pd\n", page_directory);

    // rescan page tables
    if (bar1_channel()->table()->page_directory_address() == page_directory) {
        // BAR1
        bar1_channel()->table()->refresh_page_directories(this, page_directory);
        A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
            device::instance()->bar1()->shadow(this);
            device::instance()->bar1()->flush();
        }
    }

    if (bar3_channel()->table()->page_directory_address() == page_directory) {
        // BAR3
        bar3_channel()->table()->refresh_page_directories(this, page_directory);
        A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
            device::instance()->bar3()->shadow(this, page_directory);
            device::instance()->bar3()->flush();
        }
    }

    for (std::size_t i = 0, iz = channels_.size(); i < iz; ++i) {
        channel* channel = channels(i);
        if (channel->enabled()) {
            A3_LOG("channel id %" PRIu64 " => 0x%" PRIx64 "\n", i, channel->table()->page_directory_address());
            if (channel->table()->page_directory_address() == page_directory) {
                channel->tlb_flush_needed();
                if (already) {
                    channel->override_shadow(this, already, reuse);
                } else {
                    if (channel->is_overridden_shadow()) {
                        channel->remove_overridden_shadow(this);
                    }
                    // channel->table()->refresh_page_directories(this, page_directory);
                    channel->table()->allocate_shadow_address();
                    already = channel->table()->shadow_address();
                    reuse = channel->generate_original();
                    // channel->flush(this);
                }
            }
        }
    }

    if (already) {
        const uint32_t vsp = static_cast<uint32_t>(already >> 8);
        A3_LOG("flush %" PRIx64 "\n", already);
        registers::accessor regs;
        regs.write32(0x100cb8, vsp);
        regs.write32(0x100cbc, trigger);
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
