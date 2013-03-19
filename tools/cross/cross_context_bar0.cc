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
#include "cross_bit_mask.h"
#include "cross_pramin.h"
#include "cross_shadow_page_table.h"
namespace cross {

void context::write_bar0(const command& cmd) {
    switch (cmd.offset) {
    case 0x001700: {
            // PRAMIN
            reg_pramin_ = cmd.value;
            return;
        }
    case 0x001704: {
            // BAR1 VM
            bar1_table_->refresh(this, cmd.value);
            // TODO(Yusuke Suzuki)
            // This value should not be set by device models
            registers::write32(0x1704, cmd.value);
            return;
        }
    case 0x001714: {
            // BAR3 VM
            bar3_table_->refresh(this, cmd.value);
            return;
        }
    case 0x002254: {
            // POLL_AREA
            poll_area_ = bit_mask<28, uint64_t>(cmd.value) << 12;
            reg_poll_ = cmd.value;
            registers::write32(0x2254, reg_poll_);
            return;
        }
    case 0x002270: {
            // playlist
            reg_playlist_ = cmd.value;
            return;
        }
    case 0x002274: {
            // playlist update
            reg_playlist_update_ = cmd.value;
            const uint64_t addr = bit_mask<28, uint64_t>(reg_playlist_) << 12;
            const uint32_t count = bit_mask<8, uint32_t>(reg_playlist_update_);
            fifo_playlist_update(addr, count);
            registers::write32(0x2270, reg_playlist_);
            registers::write32(0x2274, reg_playlist_update_);
            return;
        }
    case 0x002634: {
            // channel kill
            if (cmd.value >= CROSS_DOMAIN_CHANNELS) {
                return;
            }
            const uint32_t phys = get_phys_channel_id(cmd.value);
            registers::write32(cmd.offset, phys);
            reg_channel_kill_ = cmd.value;
            return;
        }

    case 0x022438:
        // 2GB
        return;

    case 0x100cb8:
        // TLB vspace
        reg_tlb_vspace_ = cmd.value;
        return;

    case 0x100cbc:
        // TLB flush trigger
        reg_tlb_trigger_ = cmd.value;
        flush_tlb(reg_tlb_vspace_, reg_tlb_trigger_);
        return;

    case 0x121c75:
        // 2GB
        return;
    }

    // PRAMIN / PMEM
    if (0x700000 <= cmd.offset && cmd.offset <= 0x7fffff) {
        pramin::write32((reg_pramin_ << 16) + bit_mask<16>(cmd.offset - 0x700000), cmd.value);
        return;
    }

    // PFIFO
    if (0x002000 <= cmd.offset && cmd.offset <= 0x004000) {
        // see pscnv/nvc0_fifo.c
        // 0x003000 + id * 8
        if ((cmd.offset - 0x003000) <= CROSS_CHANNELS * 8) {
            // channel status access
            // we should shift access target by guest VM
            const uint32_t virt = (cmd.offset - 0x003000) / 0x8;
            if (virt >= CROSS_DOMAIN_CHANNELS) {
                // these channels cannot be used
                if (virt & 0x4) {
                    // status
                } else {
                    // others
                }
                // FIXME(Yusuke Suzuki)
                // write better value
                return;
            }
            const uint32_t phys = get_phys_channel_id(virt);
            const uint32_t adjusted = (cmd.offset - virt * 8) + (phys * 8);
            printf("channel shift from 0x%"PRIx64" to 0x%"PRIx64"\n", (uint64_t)virt, (uint64_t)phys);
            registers::write32(adjusted, cmd.value);
            return;
        }
    }

    registers::write32(cmd.offset, cmd.value);
}

void context::read_bar0(const command& cmd) {
    switch (cmd.offset) {
    case 0x001700:
        buffer()->value = reg_pramin_;
        return;

    case 0x001704:
        buffer()->value = bar1_table_->channel_address();
        return;

    case 0x001714:
        buffer()->value = bar3_table_->channel_address();
        return;

    case 0x002254:
        buffer()->value = reg_poll_;
        return;

    case 0x002270:
        buffer()->value = reg_playlist_;
        return;

    case 0x002274:
        // buffer()->value = reg_playlist_update_;
        // return;
        break;

    case 0x002634:
        // channel kill
        buffer()->value = reg_channel_kill_;
        return;

    case 0x022438:
        // VRAM 2GB
        buffer()->value = 0x2;
        return;

    case 0x100cb8:
        // TLB vspace
        buffer()->value = reg_tlb_vspace_;
        return;

    case 0x100cbc:
        // TLB flush trigger
        buffer()->value = reg_tlb_trigger_;
        return;

    case 0x121c75:
        // VRAM 2GB (mem ctrl num)
        buffer()->value = 0x2;
        return;
    }

    // PRAMIN / PMEM
    if (0x700000 <= cmd.offset && cmd.offset <= 0x7fffff) {
        buffer()->value = pramin::read32((reg_pramin_ << 16) + bit_mask<16>(cmd.offset - 0x700000));
        return;
    }

    // PFIFO
    if (0x002000 <= cmd.offset && cmd.offset <= 0x004000) {
        // see pscnv/nvc0_fifo.c
        // 0x003000 + id * 8
        if ((cmd.offset - 0x003000) <= CROSS_CHANNELS * 8) {
            // channel status access
            // we should shift access target by guest VM
            const uint32_t virt = (cmd.offset - 0x003000) / 0x8;
            if (virt >= CROSS_DOMAIN_CHANNELS) {
                // these channels cannot be used
                if (virt & 0x4) {
                    // status
                } else {
                    // others
                }
                // FIXME(Yusuke Suzuki)
                // write better value
                return;
            }
            const uint32_t phys = get_phys_channel_id(virt);
            const uint32_t adjusted = (cmd.offset - virt * 8) + (phys * 8);
            printf("channel shift from 0x%"PRIx64" to 0x%"PRIx64"\n", (uint64_t)virt, (uint64_t)phys);
            buffer()->value = registers::read32(adjusted);
            return;
        }
    }

    buffer()->value = registers::read32(cmd.offset);
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
