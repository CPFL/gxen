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
#include "session.h"
#include "context.h"
#include "device.h"
#include "bit_mask.h"
#include "registers.h"
#include "barrier.h"
#include "pmem.h"
#include "device_bar1.h"
#include "device_bar3.h"
#include "shadow_page_table.h"
#include "software_page_table.h"
#include "page_table.h"
#include "pv_page.h"
#include "utility.h"
#include "ignore_unused_variable_warning.h"
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
    , poll_area_()
    , reg32_()
    , ramin_channel_map_()
    , bar3_address_()
    , pfifo_()
    , instruments_(new instruments_t(this))
    , para_virtualized_(false)
    , pv32_()
    , guest_()
    , pgds_()
    , pv_bar1_pgd_()
    , pv_bar1_large_pgt_()
    , pv_bar1_small_pgt_()
    , pv_bar3_pgd_()
    , pv_bar3_pgt_()
    , band_mutex_()
    , budget_()
    , bandwidth_()
    , bandwidth_used_()
    , sampling_bandwidth_used_()
    , sampling_bandwidth_used_100_()
    , suspended_()
{
}

context::~context() {
    if (initialized_) {
        device()->release_virt(id_, this);
        A3_LOG("END and release GPU id %u\n", id_);
    }
}

void context::initialize(int dom, bool para) {
    id_ = device()->acquire_virt(this);
    domid_ = dom;
    para_virtualized_ = para;
    if (para_virtualized()) {
        pv32_.reset(new uint32_t[A3_BAR4_SIZE / sizeof(uint32_t)]);
    }
    bar1_channel_.reset(new bar1_channel_t(this));
    bar3_channel_.reset(new bar3_channel_t(this));
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
                    ignore_unused_variable_warning(status);
                    A3_LOG("chan%0u => %" PRIx32 "\n", pid, status);
                }
            }
            break;

        case command::UTILITY_CLEAR_SHADOWING_UTILIZATION: {
                A3_SYNCHRONIZED(device()->mutex()) {
                    for (context* ctx : device()->contexts()) {
                        if (ctx) {
                            ctx->instruments()->clear_shadowing_utilization();
                        }
                    }
                }
                A3_LOG("clear context shadowing utilizations\n");
            }
            break;
        }
        return false;
    }

    if (through()) {
        A3_SYNCHRONIZED(device()->mutex()) {
            // through mode. direct access
            const uint32_t bar = cmd.bar();
            if (cmd.type == command::TYPE_WRITE) {
                device()->write(bar, cmd.offset, cmd.value, cmd.size());
                return false;
            } else if (cmd.type == command::TYPE_READ) {
                buffer()->value = device()->read(bar, cmd.offset, cmd.size());
                return true;
            }
        }
        // inspect(cmd, buffer()->value);
        return false;
    }

    bool wait = false;
    if (cmd.type == command::TYPE_WRITE) {
        switch (cmd.bar()) {
        case command::BAR0:
            write_bar0(cmd);
            A3_LOG("BAR0 write 0x%" PRIx32 " 0x%" PRIx32 "\n", cmd.offset, cmd.value);
            break;
        case command::BAR1:
            write_bar1(cmd);
            A3_LOG("BAR1 write 0x%" PRIx32 " 0x%" PRIx32 "\n", cmd.offset, cmd.value);
            break;
        case command::BAR3:
            write_bar3(cmd);
            A3_LOG("BAR3 write 0x%" PRIx32 " 0x%" PRIx32 "\n", cmd.offset, cmd.value);
            break;
        case command::BAR4:
            write_bar4(cmd);
            wait = true; // speicialized
            break;
        }
    }

    if (cmd.type == command::TYPE_READ) {
        wait = true;
        switch (cmd.bar()) {
        case command::BAR0:
            read_bar0(cmd);
            A3_LOG("BAR0 read  0x%" PRIx32 " 0x%" PRIx32 "\n", cmd.offset, buffer()->value);
            break;
        case command::BAR1:
            read_bar1(cmd);
            A3_LOG("BAR1 read  0x%" PRIx32 " 0x%" PRIx32 "\n", cmd.offset, buffer()->value);
            break;
        case command::BAR3:
            read_bar3(cmd);
            A3_LOG("BAR3 read  0x%" PRIx32 " 0x%" PRIx32 "\n", cmd.offset, buffer()->value);
            break;
        case command::BAR4:
            read_bar4(cmd);
            break;
        }
    }
    inspect(cmd, buffer()->value);

    return wait;
}

void context::playlist_update(uint32_t reg_addr, uint32_t cmd) {
    const uint64_t address = get_phys_address(bit_mask<28, uint64_t>(reg_addr) << 12);
    device()->playlist_update(this, address, cmd);
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
    const uint64_t page_directory = get_phys_address(bit_mask<40, uint64_t>(static_cast<uint64_t>(vspace) << 8));

    uint64_t already = 0;
    channel::page_table_reuse_t* reuse;

    // A3_FATAL(stdout, "flush times %" PRIu64 "\n", increment_flush_times());
    A3_LOG("TLB flush 0x%" PRIX64 " pd\n", page_directory);

    // rescan page tables
    if (bar1_channel()->table()->page_directory_address() == page_directory) {
        // BAR1
        bar1_channel()->table()->refresh_page_directories(this, page_directory);
        A3_SYNCHRONIZED(device()->mutex()) {
            device()->bar1()->shadow(this);
            device()->bar1()->flush();
        }
    }

    if (bar3_channel()->page_directory_address() == page_directory) {
        // BAR3
        bar3_channel()->refresh_table(this, page_directory);
        A3_SYNCHRONIZED(device()->mutex()) {
            device()->bar3()->shadow(this, page_directory);
            device()->bar3()->flush();
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
                    if (!a3::flags::lazy_shadowing) {
                        channel->flush(this);
                    }
                }
            }
        }
    }

    if (already) {
        const uint32_t vsp = static_cast<uint32_t>(already >> 8);
        A3_LOG("flush %" PRIx64 "\n", already);
        registers::accessor regs;
        // regs.wait_ne(0x100c80, 0x00ff0000, 0x00000000);
        regs.write32(0x100cb8, vsp);
        regs.write32(0x100cbc, trigger);
        // regs.wait_eq(0x100c80, 0x00008000, 0x00008000);
    }
}

struct page_entry context::guest_to_host(const struct page_entry& entry) {
    struct page_entry result(entry);
    if (entry.present) {
        if (entry.target == page_entry::TARGET_TYPE_VRAM) {
            // rewrite address
            const uint64_t g_field = (uint32_t)(result.address);
            const uint64_t g_address = g_field << 12;
            const uint64_t h_address = get_phys_address(g_address);
            const uint64_t h_field = h_address >> 12;
            result.address = (uint32_t)(h_field);
            if (!(get_address_shift() <= h_address && h_address < (get_address_shift() + vram_size()))) {
                // invalid address
                A3_LOG("  invalid addr 0x%" PRIx64 " to 0x%" PRIx64 "\n", g_address, h_address);
                result.present = false;
            }
        } else if (entry.target == page_entry::TARGET_TYPE_SYSRAM || entry.target == page_entry::TARGET_TYPE_SYSRAM_NO_SNOOP) {
            // rewrite address
            const uint32_t gfn = (uint32_t)(result.address);
            uint32_t mfn = 0;
            A3_SYNCHRONIZED(device()->mutex()) {
                mfn = a3_xen_gfn_to_mfn(device()->xl_ctx(), domid(), gfn);
            }
            // const uint64_t h_address = ctx->get_phys_address(g_address);
            result.address = (uint32_t)(mfn);
            // TODO(Yusuke Suzuki): Validate host physical address in Xen side
            A3_LOG("  changing to sys addr 0x%" PRIx32 " to 0x%" PRIx32 "\n", gfn, mfn);
        }
    }
    return result;
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
