/*
 * A3 NVC0 poll area
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
#include <cstdint>
#include "a3.h"
#include "poll_area.h"
#include "context.h"
#include "device_bar1.h"
namespace a3 {

bool poll_area_t::in_poll_area(context* ctx, uint64_t offset) {
    const uint64_t area = ctx->poll_area();
    return area <= offset && offset < area + (A3_DOMAIN_CHANNELS * 0x1000);
}

poll_area_t::channel_and_offset_t poll_area_t::extract_channel_and_offset(context* ctx, uint64_t offset) {
    channel_and_offset_t result = {};
    const uint64_t area = ctx->poll_area();
    const uint64_t sub = offset - area;
    result.channel = sub / 0x1000;
    result.offset = sub % 0x1000;
    return result;
}

void poll_area_t::write(context* ctx, const command& cmd) {
    A3_SYNCHRONIZED(device::instance()->mutex()) {
        device::instance()->bar1()->write(ctx, cmd);
    }
}

uint32_t poll_area_t::read(context* ctx, const command& cmd) {
    A3_SYNCHRONIZED(device::instance()->mutex()) {
        return device::instance()->bar1()->read(ctx, cmd);
    }
    return 0;
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
