/*
 * A3 FIFO scheduler
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
#include "a3_fifo_scheduler.h"
#include "a3_context.h"
#include "a3_registers.h"
#include "a3_device.h"
#include "a3_device_bar1.h"
#include "a3_ignore_unused_variable_warning.h"
namespace a3 {

fifo_scheduler::fifo_scheduler(const boost::posix_time::time_duration& wait)
    : wait_(wait)
    , thread_()
    , mutex_()
    , cond_()
    , queue_()
{
}

fifo_scheduler::~fifo_scheduler() {
    stop();
}

void fifo_scheduler::start() {
    if (thread_) {
        stop();
    }
    thread_.reset(new boost::thread(&fifo_scheduler::run, this));
}

void fifo_scheduler::stop() {
    if (thread_) {
        thread_->interrupt();
        thread_.reset();
    }
}

void fifo_scheduler::enqueue(context* ctx, const command& cmd) {
    {
        boost::unique_lock<boost::mutex> lock(mutex_);
        queue_.push(fire_t(ctx, cmd));
    }
    cond_.notify_one();
}

void fifo_scheduler::run() {
    context* current = NULL;
    fire_t handle;
    boost::condition_variable_any cond2;
    while (true) {
        {
            boost::unique_lock<boost::mutex> lock(mutex_);
            while (queue_.empty()) {
                cond_.wait(lock);
            }
            handle = queue_.front();
            queue_.pop();
        }

        {
            boost::unique_lock<mutex_t> lock(device::instance()->mutex());
            if (current != handle.first) {
                uint64_t counter = 0;
                boost::this_thread::sleep(wait_);
                while (device::instance()->is_active(current)) {
                    ++counter;
                    cond2.timed_wait(lock, wait_);
                    if ((counter % 1000) == 0) {
                        printf("Too long stop %" PRIu64 "\n", counter);
                        std::fflush(stdout);
                    }
                }
                // acquire GPU
                current = handle.first;
                const bool res = device::instance()->try_acquire_gpu(current);
                ignore_unused_variable_warning(res);
                A3_LOG("Acquire GPU [%s]\n", res ? "OK" : "NG");
            }
            device::instance()->bar1()->write(current, handle.second);
            registers::accessor regs;
            regs.write32(0x070000, 0x00000001);
            if (!regs.wait_eq(0x070000, 0x00000002, 0x00000000)) {
                A3_LOG("PRAMIN flush out\n");
            }
            A3_LOG("timer thread fires FIRE %" PRIu32 " [%s]\n", current->id(), device::instance()->is_active(current) ? "ACTIVE" : "STOP");
        }
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
