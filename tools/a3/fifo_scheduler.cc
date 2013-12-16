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
#include <cstdint>
#include "a3.h"
#include "fifo_scheduler.h"
#include "context.h"
#include "registers.h"
#include "device.h"
#include "device_bar1.h"
#include "ignore_unused_variable_warning.h"
namespace a3 {

fifo_scheduler_t::fifo_scheduler_t(const duration_t& wait, const duration_t& period, const duration_t& sample)
    : wait_(wait)
    , period_(period)
    , thread_()
    , sampler_(new sampler_t(this, sample))
    , cond_()
    , queue_()
{
}

fifo_scheduler_t::~fifo_scheduler_t() {
    stop();
}

void fifo_scheduler_t::start() {
    if (thread_) {
        stop();
    }
    sampler_->start();
    thread_.reset(new boost::thread(&fifo_scheduler_t::run, this));
    replenisher_.reset(new boost::thread(&fifo_scheduler_t::replenish, this));
}

void fifo_scheduler_t::stop() {
    if (thread_) {
        sampler_->stop();
        thread_->interrupt();
        thread_.reset();
        replenisher_->interrupt();
        replenisher_.reset();
    }
}

void fifo_scheduler_t::replenish() {
    // uint64_t count = 0;
    while (true) {
        // replenish
        A3_SYNCHRONIZED(sched_mutex()) {
            if (!contexts().empty()) {
                A3_SYNCHRONIZED(fire_mutex()) {
                    duration_t period = bandwidth_ + gpu_idle_;
                    duration_t defaults = period_ / contexts().size();
                    if (period != boost::posix_time::microseconds(0)) {
                        const auto budget = period / contexts().size();
                        for (context& ctx: contexts()) {
                            ctx.replenish(budget, period_, defaults, bandwidth_ == boost::posix_time::microseconds(0));
                        }
                        // ++count;
                    }
                    bandwidth_ = boost::posix_time::microseconds(0);
                    gpu_idle_ = boost::posix_time::microseconds(0);
                }
            }
        }
        boost::this_thread::sleep(period_);
        boost::this_thread::yield();
    }
}

void fifo_scheduler_t::enqueue(context* ctx, const command& cmd) {
    A3_SYNCHRONIZED(fire_mutex()) {
        queue_.push(fire_t(ctx, cmd));
        cond_.notify_one();
    }
}

void fifo_scheduler_t::run() {
    boost::condition_variable_any cond;
    boost::unique_lock<boost::mutex> lock(fire_mutex());
    while (true) {
        fire_t handle;
        while (queue_.empty()) {
            cond_.wait(lock);
        }
        handle = queue_.front();
        queue_.pop();

        lock.unlock();
        utilization_.start();

        A3_SYNCHRONIZED(device()->mutex()) {
            device()->bar1()->write(handle.first, handle.second);
        }

        lock.lock();

        while (device()->is_active(handle.first)) {
            cond.timed_wait(lock, wait_);
        }

        const auto duration = utilization_.elapsed();
        bandwidth_ += duration;
        sampler_->add(duration);
        handle.first->update_budget(duration);
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
