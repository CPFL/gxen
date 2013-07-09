/*
 * A3 Context BAR0
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
#include "a3_context.h"
#include "a3_registers.h"
#include "a3_barrier.h"
#include "a3_bit_mask.h"
#include "a3_pmem.h"
#include "a3_device_bar1.h"
#include "a3_device_bar3.h"
#include "a3_shadow_page_table.h"
#include "a3_ignore_unused_variable_warning.h"
namespace a3 {

void context::write_bar0(const command& cmd) {
    switch (cmd.offset) {
    case 0x001700: {
            // pmem
            reg32(cmd.offset) = cmd.value;
            return;
        }
    case 0x001704: {
            // BAR1 channel
            reg32(cmd.offset) = cmd.value;
            const uint64_t virt = (bit_mask<28, uint64_t>(cmd.value) << 12);
            const uint64_t phys = get_phys_address(virt);
            const uint32_t value = bit_clear<28>(cmd.value) | (phys >> 12);
            ignore_unused_variable_warning(value);
            A3_LOG("0x1704 => 0x%" PRIX32 "\n", value);
            bar1_channel()->refresh(this, phys);
            A3_SYNCHRONIZED(device::instance()->mutex()) {
                device::instance()->bar1()->refresh();
            }
            return;
        }
    case 0x001714: {
            // BAR3 channel
            reg32(cmd.offset) = cmd.value;
            const uint64_t virt = (bit_mask<28, uint64_t>(cmd.value) << 12);
            const uint64_t phys = get_phys_address(virt);
            const uint32_t value = bit_clear<28>(cmd.value) | (phys >> 12);
            ignore_unused_variable_warning(value);
            A3_LOG("0x1714 => 0x%" PRIX32 "\n", value);
            bar3_channel()->refresh(this, phys);
            A3_SYNCHRONIZED(device::instance()->mutex()) {
                device::instance()->bar3()->refresh();
            }
            return;
        }
    case 0x002254: {
            // POLL_AREA
            poll_area_ = bit_mask<28, uint64_t>(cmd.value) << 12;
            reg32(cmd.offset) = cmd.value;
            A3_SYNCHRONIZED(device::instance()->mutex()) {
                device::instance()->bar1()->refresh_poll_area();
            }
            A3_LOG("POLL_AREA %" PRIX32 "\n", cmd.value);
            return;
        }
    case 0x002270: {
            // playlist
            reg32(cmd.offset) = cmd.value;
            return;
        }
    case 0x002274: {
            // playlist update
            reg32(cmd.offset) = cmd.value;
            playlist_update(reg32(0x2270), cmd.value);
            return;
        }
    case 0x002634: {
            // channel kill
            reg32(cmd.offset) = cmd.value;
            if (cmd.value >= A3_DOMAIN_CHANNELS) {
                return;
            }
            const uint32_t phys = get_phys_channel_id(cmd.value);
            registers::write32(cmd.offset, phys);
            return;
        }

    case 0x022438:
        // memctrl size (2)
        return;

    case 0x100cb8:
        // TLB vspace
        reg32(cmd.offset) = cmd.value;
        return;

    case 0x100cbc:
        // TLB flush trigger
        reg32(cmd.offset) = cmd.value;
        flush_tlb(reg32(0x100cb8), cmd.value);
        return;

    case 0x104050:
    case 0x104054:
    case 0x105050:
    case 0x105054: {
            // PCOPY 0x104000 + 0x050
            //                + 0x054
            // PCOPY 0x105000 + 0x050
            //                + 0x050
            // TODO(Yusuke Suzuki) needs to limit engine
            const uint32_t value = encode_to_shadow_ramin(cmd.value);
            registers::write32(cmd.offset, value);
            return;
        }

    case 0x121c75:
        // memctrl size (2)
        return;

    case 0x409500:
        // WRCMD_DATA
        reg32(cmd.offset) = cmd.value;
        return;

    case 0x409504: {
            // WRCMD_CMD
            reg32(cmd.offset) = cmd.value;
            uint32_t data = reg32(0x409500);
            if (bit_check<31>(data)) {
                // VRAM address
                const uint64_t virt = (bit_mask<28, uint64_t>(data) << 12);
                const uint64_t phys = get_phys_address(virt);

                data = bit_clear<28>(data) | (phys >> 12);

                typedef context::channel_map::iterator iter_t;
                const std::pair<iter_t, iter_t> range = ramin_channel_map()->equal_range(phys);

                if (range.first == range.second) {
                    // no channel found
                    data = bit_clear<28>(data) | (phys >> 12);
                    A3_LOG("channel not found graph\n");
                } else {
                    // channel found
                    // channel ramin shift
                    // FIXME(Yusuke Suzuki): do FIRE like code
                    A3_LOG("WRCMD start cmd %" PRIX32 "\n", cmd.value);
                    A3_SYNCHRONIZED(device::instance()->mutex()) {
                        for (iter_t it = range.first; it != range.second; ++it) {
                            const uint32_t res = bit_clear<28>(data) | (it->second->shadow_ramin()->address() >> 12);
                            it->second->flush(this);
                            A3_LOG("    channel %d ramin graph with cmd %" PRIX32 " with addr %" PRIX64 " : %" PRIX32 " => %" PRIX32 "\n", it->second->id(), cmd.value, it->second->shadow_ramin()->address(), data, res);

                            // Because we doesn't recognize PCOPY engine initialization
                            // it->second->shadow(this);

                            registers::write32(0x409500, res);
                            registers::write32(0x409504, cmd.value);
                        }
                    }
                    A3_LOG("WRCMD end cmd %" PRIX32 "\n", cmd.value);
                    return;
                }
            }

            // fire cmd
            // TODO(Yusuke Suzuki): queued system needed
            A3_SYNCHRONIZED(device::instance()->mutex()) {
                registers::write32(0x409500, data);
                registers::write32(0x409504, cmd.value);
            }
            return;
        }

    case 0x409b00:
        // graph IRQ channel instance
        return;

    case 0x4188b4: {
            // GPC_BCAST(0x08b4)
            reg32(cmd.offset) = cmd.value;
            const uint64_t virt = (static_cast<uint64_t>(cmd.value) << 8);
            const uint64_t phys = get_phys_address(virt);
            const uint32_t value = phys >> 8;
            registers::write32(cmd.offset, value);
            return;
        }

    case 0x4188b8: {
            // GPC_BCAST(0x08b8)
            reg32(cmd.offset) = cmd.value;
            const uint64_t virt = (static_cast<uint64_t>(cmd.value) << 8);
            const uint64_t phys = get_phys_address(virt);
            const uint32_t value = phys >> 8;
            registers::write32(cmd.offset, value);
            return;
        }

    case 0x610010: {
            // NV50 PDISPLAY OBJECTS
            reg32(cmd.offset) = cmd.value;
            const uint32_t value = cmd.value + (get_address_shift() >> 8);
            registers::write32(cmd.offset, value);
            return;
        }
    }

    // pmem / PMEM
    if (0x700000 <= cmd.offset && cmd.offset <= 0x7fffff) {
        const uint64_t base = get_phys_address(static_cast<uint64_t>(reg32(0x1700)) << 16);
        const uint64_t addr = base + (cmd.offset - 0x700000);
        pmem::accessor pmem;
        pmem.write(addr, cmd.value, cmd.size());
        barrier::page_entry* entry = NULL;
        // A3_LOG("write to PMEM 0x%" PRIX64 " 0x%" PRIX32 " 0x%" PRIX64 " 0x%" PRIx32 "\n", base, cmd.offset - 0x700000, addr, cmd.value);
        if (barrier()->lookup(addr, &entry, false)) {
            // found
            write_barrier(addr, cmd);
        }
        return;
    }

    // PFIFO
    if (0x002000 <= cmd.offset && cmd.offset <= 0x004000) {
        // see pscnv/nvc0_fifo.c
        // 0x003000 + id * 8
        if (cmd.offset >= 0x003000 && (cmd.offset - 0x003000) <= A3_CHANNELS * 8) {
            // channel status access
            // we should shift access target by guest VM
            const bool ramin_area = ((cmd.offset - 0x003000) % 0x8) == 0;
            const uint32_t virt_channel_id = (cmd.offset - 0x003000) / 0x8;
            if (virt_channel_id >= A3_DOMAIN_CHANNELS) {
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

            if (ramin_area) {
                // channel ramin
                // VRAM shift
                reg32(cmd.offset) = cmd.value;
                const uint64_t virt = (bit_mask<28, uint64_t>(cmd.value) << 12);
                const uint64_t phys = get_phys_address(virt);
                const uint64_t shadow = channels(virt_channel_id)->refresh(this, phys);
                const uint32_t value = bit_clear<28>(cmd.value) | (shadow >> 12);
                A3_LOG("channel shift from 0x%" PRIX64 " to 0x%" PRIX64 " mem 0x%" PRIX64 " to 0x%" PRIX64 "\n", (uint64_t)virt_channel_id, (uint64_t)phys_channel_id, phys, shadow);
                registers::write32(adjusted_offset, value);
            } else {
                // status
                registers::write32(adjusted_offset, cmd.value);
            }
            return;
        }
    }

    registers::accessor regs;
    regs.write(cmd.offset, cmd.value, cmd.size());
}

void context::read_bar0(const command& cmd) {
    switch (cmd.offset) {
    case 0x001700:
        buffer()->value = reg32(cmd.offset);
        return;

    case 0x001704:
        buffer()->value = reg32(cmd.offset);
        return;

    case 0x001714:
        buffer()->value = reg32(cmd.offset);
        return;

    case 0x002254:
        buffer()->value = reg32(cmd.offset);
        return;

    case 0x002270:
        buffer()->value = reg32(cmd.offset);
        return;

    case 0x002274:
        break;

    case 0x002634:
        // channel kill
        buffer()->value = reg32(cmd.offset);
        return;

    case 0x022438:
        buffer()->value = vram_size() / A3_1G;
        return;

    case 0x100cb8:
        // TLB vspace
        buffer()->value = reg32(cmd.offset);
        return;

    case 0x100cbc:
        // TLB flush trigger
        buffer()->value = reg32(cmd.offset);
        return;

    case 0x104050:
    case 0x104054:
    case 0x105050:
    case 0x105054: {
            // PCOPY 0x104000 + 0x050
            //                + 0x054
            // PCOPY 0x105000 + 0x050
            //                + 0x050
            // TODO(Yusuke Suzuki) needs to limit engine
            const uint32_t value = decode_to_virt_ramin(registers::read32(cmd.offset));
            buffer()->value = value;
            return;
        }

    case 0x121c75:
        buffer()->value = vram_size() / A3_1G;
        return;

    case 0x409500:
        // WRCMD_DATA
        buffer()->value = reg32(cmd.offset);
        return;

    case 0x409504:
        // WRCMD_CMD
        buffer()->value = reg32(cmd.offset);
        return;

    case 0x409b00: {
            // graph IRQ channel instance
            const uint32_t value = registers::read32(cmd.offset);
            buffer()->value = value - (get_address_shift() >> 12);
            return;
        }

    case 0x4188b4:
        // GPC_BCAST(0x08b4)
        buffer()->value = reg32(cmd.offset);
        return;

    case 0x4188b8:
        // GPC_BCAST(0x08b8)
        buffer()->value = reg32(cmd.offset);
        return;

    case 0x610010:
        // NV50 PDISPLAY OBJECTS
        buffer()->value = reg32(cmd.offset);
        return;
    }

    // pmem / PMEM
    if (0x700000 <= cmd.offset && cmd.offset <= 0x7fffff) {
        const uint64_t base = get_phys_address(static_cast<uint64_t>(reg32(0x1700)) << 16);
        const uint64_t addr = base + (cmd.offset - 0x700000);
        pmem::accessor pmem;
        buffer()->value = pmem.read(addr, cmd.size());
        barrier::page_entry* entry = NULL;
        // A3_LOG("read from PMEM 0x%" PRIX64 " 0x%" PRIX32 " 0x%" PRIX64 "\n", base, cmd.offset - 0x700000, addr);
        if (barrier()->lookup(addr, &entry, false)) {
            // found
            read_barrier(addr, cmd);
        }
        return;
    }

    // PFIFO
    if (0x002000 <= cmd.offset && cmd.offset <= 0x004000) {
        // see pscnv/nvc0_fifo.c
        // 0x003000 + id * 8
        if (cmd.offset >= 0x003000 && (cmd.offset - 0x003000) <= A3_CHANNELS * 8) {
            // channel status access
            // we should shift access target by guest VM
            const bool ramin_area = ((cmd.offset - 0x003000) % 0x8) == 0;
            const uint32_t virt_channel_id = (cmd.offset - 0x003000) / 0x8;
            if (virt_channel_id >= A3_DOMAIN_CHANNELS) {
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

            if (ramin_area) {
                // channel ramin
                buffer()->value = reg32(cmd.offset);
            } else {
                // status
                buffer()->value = registers::read32(adjusted_offset);
            }

            return;
        }
    }

    registers::accessor regs;
    buffer()->value = regs.read(cmd.offset, cmd.size());
}

// PCOPY channel inst decode
uint32_t context::decode_to_virt_ramin(uint32_t value) {
    A3_LOG("decoding channel 0x%" PRIX32 "\n", value);
    if (!value) {
        return value;
    }
    // This is address of channel ramin (shadow)
    const uint64_t shadow = bit_mask<28, uint64_t>(value) << 12;
    uint64_t phys = 0;
    if (shadow_ramin_to_phys(shadow, &phys)) {
        A3_LOG("decode: ramin shadow %" PRIX64 " to virt %" PRIX64 "\n", shadow, get_virt_address(phys));
        value = bit_clear<28>(value) | (get_virt_address(phys) >> 12);
    } else {
        value = 0;
    }
    return value;
}

bool context::shadow_ramin_to_phys(uint64_t shadow, uint64_t* phys) {
    for (std::size_t i = 0, iz = channels_.size(); i < iz; ++i) {
        if (channels(i)->enabled()) {
            if (channels(i)->shadow_ramin()->address() == shadow) {
                *phys = channels(i)->ramin_address();
                return true;
            }
        }
    }
    return false;
}

// PCOPY channel inst encode
uint32_t context::encode_to_shadow_ramin(uint32_t value) {
    A3_LOG("encoding channel 0x%" PRIX32 "\n", value);
    // This is address of channel ramin (shadow)
    if (!value) {
        return value;
    }

    const uint64_t virt = bit_mask<28, uint64_t>(value) << 12;
    const uint64_t phys = get_phys_address(virt);
    typedef context::channel_map::iterator iter_t;
    const std::pair<iter_t, iter_t> range = ramin_channel_map()->equal_range(phys);
    if (range.first == range.second) {
        // no channel found
        A3_LOG("encoding channel not found graph\n");
        return value;
    } else {
        for (iter_t it = range.first; it != range.second; ++it) {
            A3_LOG("encode: virt %" PRIX64 " to shadow ramin %" PRIX64 "\n", virt, it->second->shadow_ramin()->address());
            it->second->flush(this);
            return bit_clear<28>(value) | (it->second->shadow_ramin()->address() >> 12);
        }
    }
    return value;
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
