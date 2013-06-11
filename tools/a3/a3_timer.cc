/*
 * A3 timer
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
#include "a3_timer.h"
#include "a3_context.h"
#include "a3_device.h"
#include "a3_device_bar1.h"
namespace a3 {

timer_t::timer_t(const boost::posix_time::time_duration& wait)
    : wait_(wait)
    , thread_()
    , mutex_()
    , queue_()
{
}

timer_t::~timer_t() {
    stop();
}

void timer_t::start() {
    if (thread_) {
        stop();
    }
    thread_.reset(new boost::thread(&timer_t::run, this));
}

void timer_t::stop() {
    if (thread_) {
        thread_->interrupt();
        thread_.reset();
    }
}

void timer_t::enqueue(context* ctx, const command& cmd) {
    A3_SYNCHRONIZED(mutex_) {
        queue_.push(fire_t(ctx, cmd));
    }
}

void timer_t::run() {
    context* current = NULL;
    bool wait = false;
    fire_t handle;
    while (true) {
        A3_SYNCHRONIZED(mutex_) {
            if (!wait && !queue_.empty()) {
                wait = true;
                handle = queue_.front();
                queue_.pop();
            }
        }

        bool will_be_sleep = true;
        if (wait) {
            A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
                if (current == handle.first || !device::instance()->is_active()) {
                    if (current != handle.first) {
                        // acquire GPU
                        current = handle.first;
                        device::instance()->try_acquire_gpu(current);
                        A3_LOG("Acquire GPU\n");
                    }
                    wait = false;
                    // FIXME(Yusuke Suzuki) thread unsafe
                    device::instance()->bar1()->write(handle.first, handle.second);
                    A3_LOG("timer thread fires FIRE [%s]\n", device::instance()->is_active() ? "OK" : "NG");
                    will_be_sleep = false;
                }
            }
        }

        if (will_be_sleep) {
            if (wait) {
                // A3_LOG("timer thread sleeps\n");
            }
            boost::this_thread::yield();
            boost::this_thread::sleep(wait_);
        }
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
