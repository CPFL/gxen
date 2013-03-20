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
#include "cross_barrier.h"
#include "cross_bit_mask.h"
#include "cross_pramin.h"
#include "cross_shadow_page_table.h"
namespace cross {

void context::write_bar0(const command& cmd) {
    switch (cmd.offset) {
    case 0x001700: {
            // PRAMIN
            reg_[cmd.offset] = cmd.value;
            return;
        }
    case 0x001704: {
            // BAR1 channel
            reg_[cmd.offset] = cmd.value;
            const uint64_t virt = (bit_mask<28, uint64_t>(cmd.value) << 12);
            const uint64_t phys = get_phys_address(virt);
            const uint32_t value = bit_clear<28>(cmd.value) | (phys >> 12);
            printf("0x1704 => 0x%" PRIX32 "\n", value);
            bar1_channel()->refresh(this, phys);
            // TODO(Yusuke Suzuki)
            // This value should not be set by device models
            registers::write32(0x1704, value);
            return;
        }
    case 0x001714: {
            // BAR3 channel
            reg_[cmd.offset] = cmd.value;
            const uint64_t virt = (bit_mask<28, uint64_t>(cmd.value) << 12);
            const uint64_t phys = get_phys_address(virt);
            const uint32_t value = bit_clear<28>(cmd.value) | (phys >> 12);
            printf("0x1714 => 0x%" PRIX32 "\n", value);
            bar3_channel()->refresh(this, phys);
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
            reg_[cmd.offset] = cmd.value;
            return;
        }
    case 0x002274: {
            // playlist update
            reg_[cmd.offset] = cmd.value;
            const uint32_t reg_playlist = reg_[0x2270];
            const uint32_t reg_playlist_update = cmd.value;
            const uint64_t addr = get_phys_address(bit_mask<28, uint64_t>(reg_playlist) << 12);
            const uint32_t count = bit_mask<8, uint32_t>(reg_playlist_update);
            fifo_playlist_update(addr, count);
            registers::write32(0x2270, addr >> 12);
            registers::write32(0x2274, reg_playlist_update);
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

    case 0x104050:
        // PCOPY 0x104000 + 0x050
        // TODO(Yusuke Suzuki) needs to limit engine
        break;

    case 0x104054:
        // PCOPY 0x104000 + 0x054
        // TODO(Yusuke Suzuki) needs to limit engine
        break;

    case 0x105050:
        // PCOPY 0x105000 + 0x050
        // TODO(Yusuke Suzuki) needs to limit engine
        break;

    case 0x105054:
        // PCOPY 0x105000 + 0x054
        // TODO(Yusuke Suzuki) needs to limit engine
        break;

    case 0x121c75:
        // 2GB
        return;

    case 0x409500:
        // WRCMD_DATA
        reg_[cmd.offset] = cmd.value;
        if (bit_check<31>(cmd.value)) {
            // VRAM address
            const uint64_t virt = (bit_mask<28, uint64_t>(cmd.value) << 12);
            const uint64_t phys = get_phys_address(virt);
            const uint32_t value = bit_clear<28>(cmd.value) | (phys >> 12);
            registers::write32(cmd.offset, value);
            return;
        }
        break;

    case 0x409b00:
        // graph IRQ channel instance
        return;

    case 0x4188b4: {
            // GPC_BCAST(0x08b4)
            reg_[cmd.offset] = cmd.value;
            const uint64_t virt = (static_cast<uint64_t>(cmd.value) << 8);
            const uint64_t phys = get_phys_address(virt);
            const uint32_t value = phys >> 8;
            registers::write32(cmd.offset, value);
            return;
        }

    case 0x4188b8: {
            // GPC_BCAST(0x08b8)
            reg_[cmd.offset] = cmd.value;
            const uint64_t virt = (static_cast<uint64_t>(cmd.value) << 8);
            const uint64_t phys = get_phys_address(virt);
            const uint32_t value = phys >> 8;
            registers::write32(cmd.offset, value);
            return;
        }

    case 0x610010: {
            // NV50 PDISPLAY OBJECTS
            reg_[cmd.offset] = cmd.value;
            const uint32_t value = cmd.value + (get_address_shift() >> 8);
            registers::write32(cmd.offset, value);
            return;
        }
    }

    // PRAMIN / PMEM
    if (0x700000 <= cmd.offset && cmd.offset <= 0x7fffff) {
        const uint64_t base = get_phys_address(static_cast<uint64_t>(reg_[0x1700]) << 16);
        const uint64_t addr = base + bit_mask<16>(cmd.offset - 0x700000);
        barrier::page_entry* entry = NULL;
        if (barrier()->lookup(addr, &entry, false)) {
            // found
            write_barrier(addr, cmd.value);
        }
        pramin::write32(addr, cmd.value);
        return;
    }

    // PFIFO
    if (0x002000 <= cmd.offset && cmd.offset <= 0x004000) {
        // see pscnv/nvc0_fifo.c
        // 0x003000 + id * 8
        if ((cmd.offset - 0x003000) <= CROSS_CHANNELS * 8) {
            // channel status access
            // we should shift access target by guest VM
            const bool ramin_area = ((cmd.offset - 0x003000) % 0x8) == 0;
            const uint32_t virt_channel_id = (cmd.offset - 0x003000) / 0x8;
            if (virt_channel_id >= CROSS_DOMAIN_CHANNELS) {
                // these channels cannot be used

                if (ramin_area) {
                    // channel ramin
                } else {
                    // status
                }

                // FIXME(Yusuke Suzuki)
                // write better value
                return;
            }

            const uint32_t phys_channel_id = get_phys_channel_id(virt_channel_id);
            const uint32_t adjusted_offset = (cmd.offset - virt_channel_id * 8) + (phys_channel_id * 8);
            printf("channel shift from 0x%"PRIx64" to 0x%"PRIx64"\n", (uint64_t)virt_channel_id, (uint64_t)phys_channel_id);

            if (ramin_area) {
                // channel ramin
                // VRAM shift
                reg_[cmd.offset] = cmd.value;
                const uint64_t virt = (bit_mask<28, uint64_t>(cmd.value) << 12);
                const uint64_t phys = get_phys_address(virt);
                const uint32_t value = bit_clear<28>(cmd.value) | (phys >> 12);
                channels(virt_channel_id)->refresh(this, phys);
                registers::write32(adjusted_offset, value);
            } else {
                // status
                registers::write32(adjusted_offset, cmd.value);
            }
            return;
        }
    }

    registers::write32(cmd.offset, cmd.value);
}

void context::read_bar0(const command& cmd) {
    switch (cmd.offset) {
    case 0x001700:
        buffer()->value = reg_[cmd.offset];
        return;

    case 0x001704:
        buffer()->value = reg_[cmd.offset];
        return;

    case 0x001714:
        buffer()->value = reg_[cmd.offset];
        return;

    case 0x002254:
        buffer()->value = reg_poll_;
        return;

    case 0x002270:
        buffer()->value = reg_[cmd.offset];
        return;

    case 0x002274:
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

    case 0x104050: {
            // PCOPY 0x104000 + 0x050
            // TODO(Yusuke Suzuki) needs to limit engine
            const uint32_t value = registers::read32(cmd.offset);
            buffer()->value = value - (get_address_shift() >> 12);
            return;
        }

    case 0x104054: {
            // PCOPY 0x104000 + 0x054
            // TODO(Yusuke Suzuki) needs to limit engine
            const uint32_t value = registers::read32(cmd.offset);
            buffer()->value = value - (get_address_shift() >> 12);
            return;
        }

    case 0x105050: {
            // PCOPY 0x105000 + 0x050
            // TODO(Yusuke Suzuki) needs to limit engine
            const uint32_t value = registers::read32(cmd.offset);
            buffer()->value = value - (get_address_shift() >> 12);
            return;
        }

    case 0x105054: {
            // PCOPY 0x105000 + 0x050
            // TODO(Yusuke Suzuki) needs to limit engine
            const uint32_t value = registers::read32(cmd.offset);
            buffer()->value = value - (get_address_shift() >> 12);
            return;
        }

    case 0x121c75:
        // VRAM 2GB (mem ctrl num)
        buffer()->value = 0x2;
        return;

    case 0x409500:
        // WRCMD_DATA
        buffer()->value = reg_[cmd.offset];
        return;

    case 0x409b00: {
            // graph IRQ channel instance
            const uint32_t value = registers::read32(cmd.offset);
            buffer()->value = value - (get_address_shift() >> 12);
            return;
        }

    case 0x4188b4:
        // GPC_BCAST(0x08b4)
        buffer()->value = reg_[cmd.offset];
        return;

    case 0x4188b8:
        // GPC_BCAST(0x08b8)
        buffer()->value = reg_[cmd.offset];
        return;

    case 0x610010:
        // NV50 PDISPLAY OBJECTS
        buffer()->value = reg_[cmd.offset];
        return;
    }

    // PRAMIN / PMEM
    if (0x700000 <= cmd.offset && cmd.offset <= 0x7fffff) {
        const uint64_t base = get_phys_address(static_cast<uint64_t>(reg_[0x1700]) << 16);
        const uint64_t addr = base + bit_mask<16>(cmd.offset - 0x700000);
        barrier::page_entry* entry = NULL;
        std::printf("read from PMEM 0x%" PRIX64 " 0x%" PRIX32 " 0x%" PRIX64 "\n", base, cmd.offset - 0x700000, addr);
        std::fflush(stdout);
        if (barrier()->lookup(addr, &entry, false)) {
            // found
            read_barrier(addr);
        }
        buffer()->value = pramin::read32(addr);
        return;
    }

    // PFIFO
    if (0x002000 <= cmd.offset && cmd.offset <= 0x004000) {
        // see pscnv/nvc0_fifo.c
        // 0x003000 + id * 8
        if ((cmd.offset - 0x003000) <= CROSS_CHANNELS * 8) {
            // channel status access
            // we should shift access target by guest VM
            const bool ramin_area = ((cmd.offset - 0x003000) % 0x8) == 0;
            const uint32_t virt_channel_id = (cmd.offset - 0x003000) / 0x8;
            if (virt_channel_id >= CROSS_DOMAIN_CHANNELS) {
                // these channels cannot be used

                if (ramin_area) {
                    // channel ramin
                } else {
                    // status
                }

                // FIXME(Yusuke Suzuki)
                // write better value
                return;
            }

            const uint32_t phys_channel_id = get_phys_channel_id(virt_channel_id);
            const uint32_t adjusted_offset = (cmd.offset - virt_channel_id * 8) + (phys_channel_id * 8);
            printf("channel shift from 0x%"PRIx64" to 0x%"PRIx64"\n", (uint64_t)virt_channel_id, (uint64_t)phys_channel_id);

            if (ramin_area) {
                // channel ramin
                buffer()->value = reg_[cmd.offset];
            } else {
                // status
                buffer()->value = registers::read32(adjusted_offset);
            }

            return;
        }
    }

    buffer()->value = registers::read32(cmd.offset);
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
