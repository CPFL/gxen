/*
 * A3 Instruments
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
#include <cstdio>
#include <cstdlib>
#include "a3.h"
#include "context.h"
#include "instruments.h"
namespace a3 {

instruments_t::instruments_t(context* ctx)
    : ctx_(ctx)
    , flush_times_()
    , shadowing_times_()
    , shadowing_(boost::posix_time::microseconds(0))
    , hypercalls_()
{
}

void instruments_t::hypercall(const command& cmd, slot_t* slot) {
    ++hypercalls_;
    // A3_FATAL(stdout, "[hypercalls] %" PRIu64 "\n", hypercalls_);
    A3_LOG("A3 call from [%" PRIu32 "] %d : %s\n", ctx_->id(), static_cast<int>(slot->u8[0]), kPV_OPS_STRING[slot->u8[0]]);
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
