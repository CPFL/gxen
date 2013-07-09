/*
 * A3 BAND scheduler
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
#include <stdint.h>
#include "a3.h"
#include "a3_band_scheduler.h"
#include "a3_context.h"
#include "a3_registers.h"
#include "a3_device.h"
#include "a3_device_bar1.h"
#include "a3_ignore_unused_variable_warning.h"
namespace a3 {

band_scheduler::band_scheduler(const boost::posix_time::time_duration& wait)
    : wait_(wait)
    , thread_()
    , mutex_()
    , cond_()
    , queue_()
{
}

band_scheduler::~band_scheduler() {
    stop();
}

void band_scheduler::start() {
    if (thread_) {
        stop();
    }
    thread_.reset(new boost::thread(&band_scheduler::run, this));
}

void band_scheduler::stop() {
    if (thread_) {
        thread_->interrupt();
        thread_.reset();
    }
}

void band_scheduler::enqueue(context* ctx, const command& cmd) {
    // on arrival
    {
        boost::unique_lock<boost::mutex> lock(mutex_);
        if (!current_) {
            acquire(ctx);
        }
        if (current_ != ctx) {
            suspend(ctx, cmd);
        } else {
            dispatch(ctx, cmd);
        }
    }
}

void band_scheduler::acquire(context* ctx) {
    current_ = ctx;
    A3_SYNCHRONIZED(device::instance()->mutex()) {
        device::instance()->try_acquire_gpu(ctx);
    }
}

void band_scheduler::suspend(context* ctx, const command& cmd) {
    queue_.push(fire_t(ctx, cmd));
}

void band_scheduler::dispatch(context* ctx, const command& cmd) {
    device::instance()->bar1()->write(ctx, cmd);
}

void band_scheduler::run() {
//     context* current = NULL;
//     fire_t handle;
//     boost::condition_variable_any cond2;
//     while (true) {
//         {
//             boost::unique_lock<boost::mutex> lock(mutex_);
//             while (queue_.empty()) {
//                 cond_.wait(lock);
//             }
//             handle = queue_.front();
//             queue_.pop();
//         }
//
//         {
//             boost::unique_lock<mutex> lock(device::instance()->mutex_handle());
//             while (current != handle.first && device::instance()->is_active(current)) {
//                 cond2.timed_wait(lock, wait_);
//             }
//
//             if (current != handle.first) {
//                 // acquire GPU
//                 // context change.
//                 // To ensure all previous fires should be submitted, we flush BAR1.
//                 current = handle.first;
//                 const bool res = device::instance()->try_acquire_gpu(current);
//                 ignore_unused_variable_warning(res);
//                 A3_LOG("Acquire GPU [%s]\n", res ? "OK" : "NG");
//             }
//             device::instance()->bar1()->write(current, handle.second);
//             A3_LOG("timer thread fires FIRE %" PRIu32 " [%s]\n", current->id(), device::instance()->is_active(current) ? "ACTIVE" : "STOP");
//         }
//     }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
