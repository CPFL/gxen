/*
 * A3 PFIFO
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
#include "pfifo.h"
#include "device.h"
#include "context.h"
#include "registers.h"
#include "bit_mask.h"
namespace a3 {

pfifo_t::pfifo_t()
    : total_channels_(A3_CHANNELS)
    , channels_(A3_DOMAIN_CHANNELS)
    , range_(device()->chipset()->type() == card::NVC0 ? 0x003000 : 0x800000)
{
}

bool pfifo_t::in_range(uint32_t offset) const {
    return offset >= range() && (offset - range()) <= total_channels() * 8;
}

void pfifo_t::write(context* ctx, command cmd) {
    // channel status access
    // we should shift access target by guest VM
    const bool ramin_area = ((cmd.offset - range()) % 0x8) == 0;
    const uint32_t virt_channel_id = (cmd.offset - range()) / 0x8;
    if (virt_channel_id >= channels()) {
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

    const uint32_t phys_channel_id = ctx->get_phys_channel_id(virt_channel_id);
    const uint32_t adjusted_offset =
        (cmd.offset - virt_channel_id * 8) + (phys_channel_id * 8);
    A3_LOG("adjusted offset 0x%" PRIX32 "\n", adjusted_offset);

    if (ramin_area) {
        // channel ramin
        // VRAM shift
        ctx->reg32(cmd.offset) = cmd.value;
        const uint64_t virt = (bit_mask<28, uint64_t>(cmd.value) << 12);
        const uint64_t phys = ctx->get_phys_address(virt);
        const uint64_t shadow = ctx->channels(virt_channel_id)->refresh(ctx, phys);
        const uint32_t value = bit_clear<28>(cmd.value) | (shadow >> 12);
        A3_LOG("channel shift from 0x%" PRIX64 " to 0x%" PRIX64 " mem 0x%" PRIX64 " to 0x%" PRIX64 "\n", (uint64_t)virt_channel_id, (uint64_t)phys_channel_id, phys, shadow);
        registers::write32(adjusted_offset, value);
    } else {
        // status
        registers::write32(adjusted_offset, cmd.value);
    }
    return;
}

uint32_t pfifo_t::read(context* ctx, command cmd) {
    ASSERT(in_range(cmd.offset));
    // channel status access
    // we should shift access target by guest VM
    const bool ramin_area = ((cmd.offset - range()) % 0x8) == 0;
    const uint32_t virt_channel_id = (cmd.offset - range()) / 0x8;
    if (virt_channel_id >= channels()) {
        // these channels cannot be used

        if (ramin_area) {
            // channel ramin
        } else {
            // status
        }

        // FIXME(Yusuke Suzuki)
        // write better value
        return 0;
    }

    const uint32_t phys_channel_id = ctx->get_phys_channel_id(virt_channel_id);
    const uint32_t adjusted_offset =
        (cmd.offset - virt_channel_id * 8) + (phys_channel_id * 8);

    if (ramin_area) {
        // channel ramin
        return ctx->reg32(cmd.offset);
    } else {
        // status
        return registers::read32(adjusted_offset);
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
