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

fifo_scheduler_t::fifo_scheduler_t(const boost::posix_time::time_duration& wait, const boost::posix_time::time_duration& period, const boost::posix_time::time_duration& sample)
    : wait_(wait)
    , period_(period)
    , sample_(sample)
    , thread_()
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
    thread_.reset(new boost::thread(&fifo_scheduler_t::run, this));
    replenisher_.reset(new boost::thread(&fifo_scheduler_t::replenish, this));
    sampler_.reset(new boost::thread(&fifo_scheduler_t::sampling, this));
}

void fifo_scheduler_t::stop() {
    if (thread_) {
        thread_->interrupt();
        thread_.reset();
        replenisher_->interrupt();
        replenisher_.reset();
        sampler_->interrupt();
        sampler_.reset();
    }
}

void fifo_scheduler_t::replenish() {
    // uint64_t count = 0;
    while (true) {
        // replenish
        A3_SYNCHRONIZED(sched_mutex()) {
            if (!contexts().empty()) {
                A3_SYNCHRONIZED(fire_mutex()) {
                    boost::posix_time::time_duration period = bandwidth_ + gpu_idle_;
                    boost::posix_time::time_duration defaults = period_ / contexts().size();
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
    }
    cond_.notify_one();
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

        A3_SYNCHRONIZED(device::instance()->mutex()) {
            device::instance()->bar1()->write(handle.first, handle.second);
        }

        lock.lock();

        while (device::instance()->is_active(handle.first)) {
            cond.timed_wait(lock, wait_);
        }

        const auto duration = utilization_.elapsed();
        bandwidth_ += duration;
        sampling_bandwidth_ += duration;
        sampling_bandwidth_100_ += duration;
        handle.first->update_budget(duration);
    }
}

void fifo_scheduler_t::sampling() {
    uint64_t count = 0;
    uint64_t points = 0;
    while (true) {
        // sampling
        A3_SYNCHRONIZED(sched_mutex()) {
            if (!contexts().empty()) {
                A3_SYNCHRONIZED(fire_mutex()) {
                    if (sampling_bandwidth_ != boost::posix_time::microseconds(0)) {
                        // A3_FATAL(stdout, "UTIL: LOG %" PRIu64 "\n", count);
                        for (context& ctx : contexts()) {
                            // A3_FATAL(stdout, "UTIL[100]: %d => %f\n", ctx.id(), (static_cast<double>(ctx.sampling_bandwidth_used_100().total_microseconds()) / sampling_bandwidth_100_.total_microseconds()));
                            if (points % 5 == 4) {
                                // A3_FATAL(stdout, "UTIL[500]: %d => %f\n", ctx.id(), (static_cast<double>(ctx.sampling_bandwidth_used().total_microseconds()) / sampling_bandwidth_.total_microseconds()));
                            }
                            ctx.clear_sampling_bandwidth_used(points);
                        }
                        ++count;
                        points = (points + 1) % 5;
                    }
                    sampling_bandwidth_100_ = boost::posix_time::microseconds(0);
                    if (points % 5 == 4) {
                        sampling_bandwidth_ = boost::posix_time::microseconds(0);
                    }
                }
            }
        }
        boost::this_thread::sleep(sample_);
        boost::this_thread::yield();
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
