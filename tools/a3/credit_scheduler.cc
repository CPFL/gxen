/*
 * A3 Credit scheduler
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
#include <memory>
#include "a3.h"
#include "credit_scheduler.h"
#include "context.h"
#include "registers.h"
#include "device.h"
#include "device_bar1.h"
#include "ignore_unused_variable_warning.h"
namespace a3 {

credit_scheduler_t::credit_scheduler_t(const boost::posix_time::time_duration& wait, const boost::posix_time::time_duration& designed, const boost::posix_time::time_duration& period, const boost::posix_time::time_duration& sample)
    : wait_(wait)
    , designed_(designed)
    , period_(period)
    , sample_(sample)
    , gpu_idle_()
    , thread_()
    , replenisher_()
    , sampler_()
    , mutex_()
    , cond_()
    , active_()
    , inactive_()
    , current_()
    , utilization_()
    , bandwidth_()
    , sampling_bandwidth_()
    , sampling_bandwidth_100_()
    , counter_()
{
}


void credit_scheduler_t::register_context(context* ctx) {
    A3_SYNCHRONIZED(sched_mutex_) {
        contexts_.push_back(*ctx);
    }
}

void credit_scheduler_t::unregister_context(context* ctx) {
    A3_SYNCHRONIZED(sched_mutex_) {
        contexts_.erase(contexts_t::s_iterator_to(*ctx));
    }
}

credit_scheduler_t::~credit_scheduler_t() {
    stop();
}

void credit_scheduler_t::start() {
    if (thread_ || replenisher_ || sampler_) {
        stop();
    }
    thread_.reset(new boost::thread(&credit_scheduler_t::run, this));
    replenisher_.reset(new boost::thread(&credit_scheduler_t::replenish, this));
    sampler_.reset(new boost::thread(&credit_scheduler_t::sampling, this));
}

void credit_scheduler_t::stop() {
    if (thread_) {
        thread_->interrupt();
        thread_.reset();
        replenisher_->interrupt();
        replenisher_.reset();
        sampler_->interrupt();
        sampler_.reset();
    }
}

void credit_scheduler_t::enqueue(context* ctx, const command& cmd) {
    // on arrival
    ctx->enqueue(cmd);
    A3_SYNCHRONIZED(counter_mutex_) {
        counter_ += 1;
    }
    cond_.notify_one();
}

void credit_scheduler_t::replenish() {
    // uint64_t count = 0;
    while (true) {
        // replenish
        A3_SYNCHRONIZED(sched_mutex_) {
            if (!contexts_.empty()) {
                A3_SYNCHRONIZED(fire_mutex_) {
                    boost::posix_time::time_duration period = bandwidth_ + gpu_idle_;
                    boost::posix_time::time_duration defaults = period_ / contexts_.size();
                    previous_bandwidth_ = period;
                    // boost::posix_time::time_duration period = bandwidth_;
                    if (period != boost::posix_time::microseconds(0)) {
                        const auto budget = period / contexts_.size();
                        for (context& ctx : contexts_) {
                            ctx.replenish(budget, budget * 2, defaults, bandwidth_ == boost::posix_time::microseconds(0));
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

context* credit_scheduler_t::select_next_context(bool idle) {
    A3_SYNCHRONIZED(sched_mutex_) {
        if (idle) {
            gpu_idle_ += gpu_idle_timer_.elapsed();
        }

        if (current()) {
            // lowering priority
            context* ctx = current();
            if (ctx->budget() < boost::posix_time::microseconds(0)) {
                contexts_.erase(contexts_t::s_iterator_to(*ctx));
                contexts_.push_back(*ctx);
            }
        }

        for (context& ctx : contexts_) {
            if (ctx.is_suspended()) {
                return &ctx;
            }
        }
    }
    return nullptr;
}

void credit_scheduler_t::submit(context* ctx) {
    A3_SYNCHRONIZED(fire_mutex_) {
        command cmd;

        ctx->dequeue(&cmd);

        utilization_.start();
        A3_SYNCHRONIZED(device::instance()->mutex()) {
            device::instance()->bar1()->write(ctx, cmd);
        }

        while (device::instance()->is_active(ctx)) {
            boost::this_thread::yield();
        }

        const auto duration = utilization_.elapsed();
        bandwidth_ += duration;
        sampling_bandwidth_ += duration;
        sampling_bandwidth_100_ += duration;
        ctx->update_budget(duration);
    }
}

void credit_scheduler_t::run() {
    while (true) {
        bool idle = false;
        gpu_idle_timer_.start();
        {
            boost::unique_lock<boost::mutex> lock(counter_mutex_);
            while (!counter_) {
                boost::this_thread::yield();
                idle = true;
                cond_.wait(lock);
            }
        }
        if ((current_ = select_next_context(idle))) {
            A3_SYNCHRONIZED(counter_mutex_) {
                counter_ -= 1;
            }
            submit(current());
        }
    }
}

void credit_scheduler_t::sampling() {
    uint64_t count = 0;
    uint64_t points = 0;
    while (true) {
        // sampling
        A3_SYNCHRONIZED(sched_mutex_) {
            if (!contexts_.empty()) {
                A3_SYNCHRONIZED(fire_mutex_) {
                    uint64_t next_points = points;
                    const bool use100 = sampling_bandwidth_100_ != boost::posix_time::microseconds(0);
                    const bool use500 = sampling_bandwidth_ != boost::posix_time::microseconds(0);
                    if (use100 || use500) {
                        // A3_FATAL(stdout, "UTIL: LOG %" PRIu64 "\n", count);
                        for (context& ctx : contexts_) {
                            if (use100) {
                                // A3_FATAL(stdout, "UTIL[100]: %d => %f\n", ctx.id(), (static_cast<double>(ctx.sampling_bandwidth_used_100().total_microseconds()) / sampling_bandwidth_100_.total_microseconds()));
                            }
                            if (use500) {
                                if (points % 5 == 4) {
                                    // A3_FATAL(stdout, "UTIL[500]: %d => %f\n", ctx.id(), (static_cast<double>(ctx.sampling_bandwidth_used().total_microseconds()) / sampling_bandwidth_.total_microseconds()));
                                }
                            }
                            ctx.clear_sampling_bandwidth_used(points);
                        }
                        ++count;
                        next_points = (points + 1) % 5;
                    }
                    sampling_bandwidth_100_ = boost::posix_time::microseconds(0);
                    if (points % 5 == 4) {
                        sampling_bandwidth_ = boost::posix_time::microseconds(0);
                    }
                    points = next_points;
                }
            }
        }
        boost::this_thread::sleep(sample_);
        boost::this_thread::yield();
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
