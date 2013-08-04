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

band_scheduler_t::band_scheduler_t(const boost::posix_time::time_duration& wait, const boost::posix_time::time_duration& designed, const boost::posix_time::time_duration& period)
    : wait_(wait)
    , designed_(designed)
    , period_(period)
    , thread_()
    , replenisher_()
    , mutex_()
    , cond_()
    , active_()
    , inactive_()
    , current_()
    , utilization_()
    , bandwidth_()
    , counter2_()
{
}


void band_scheduler_t::register_context(context* ctx) {
    boost::unique_lock<boost::mutex> lock(sched_mutex_);
    contexts_.push_back(*ctx);
}

void band_scheduler_t::unregister_context(context* ctx) {
    boost::unique_lock<boost::mutex> lock(sched_mutex_);
    contexts_.erase(contexts_t::s_iterator_to(*ctx));
}

band_scheduler_t::~band_scheduler_t() {
    stop();
}

void band_scheduler_t::start() {
    if (thread_ || replenisher_) {
        stop();
    }
    thread_.reset(new boost::thread(&band_scheduler_t::run, this));
    replenisher_.reset(new boost::thread(&band_scheduler_t::replenish, this));
}

void band_scheduler_t::stop() {
    if (thread_) {
        thread_->interrupt();
        thread_.reset();
        replenisher_->interrupt();
        replenisher_.reset();
    }
}

static void yield_chance(const boost::posix_time::time_duration& duration) {
    auto now = boost::posix_time::microsec_clock::local_time();
    const auto wait = now + duration;
    while (now < wait) {
        boost::this_thread::yield();
        now = boost::posix_time::microsec_clock::local_time();
    }
}

void band_scheduler_t::enqueue(context* ctx, const command& cmd) {
    // on arrival
    ctx->enqueue(cmd);
#if 0
    {
        if (ctx->budeget() < boost::posix_time::microseconds(0) && ctx->bandwidth_used() > ctx->bandwidth() && contexts_.size() != 1) {
            boost::unique_lock<boost::mutex> lock(sched_mutex_);
            if (current() == ctx) {
                current_ = NULL;
                lock.unlock();
                yield_chance();
                lock.lock();

                if (current_ == NULL) {
                    current_ = ctx;
                }
            }
        }
    }

    boost::unique_lock<boost::mutex> lock(sched_mutex_);
    if (current_ && current_ != ctx) {
    } else {
        current_ = ctx;
    }
#endif
    counter_.fetch_add(1);
    cond_.notify_one();
}

void band_scheduler_t::replenish() {
    uint64_t count = 0;
    while (true) {
        // replenish
        {
            boost::unique_lock<boost::mutex> lock(sched_mutex_);
            if (!contexts_.empty()) {
                boost::unique_lock<boost::mutex> lock(fire_mutex_);
                if (bandwidth_ != boost::posix_time::microseconds(0)) {
                    A3_FATAL(stdout, "UTIL: LOG %" PRIu64 "\n", count);
                    // const auto budget = period_ * 0.5 / contexts_.size();
                    const auto budget = period_ / 4 / contexts_.size();
                    for (context& ctx : contexts_) {
                        A3_FATAL(stdout, "UTIL: %d => %f\n", ctx.id(), (static_cast<double>(ctx.bandwidth_used().total_microseconds()) / bandwidth_.total_microseconds()));
                        ctx.replenish(budget, period_);
                    }
                    ++count;
                }
                bandwidth_ = boost::posix_time::microseconds(0);
            }
        }
        boost::this_thread::sleep(period_);
        // boost::this_thread::yield();
    }
}

bool band_scheduler_t::utilization_over_bandwidth(context* ctx) const {
    if (bandwidth_ == boost::posix_time::microseconds(0)) {
        return true;
    }
    return (ctx->bandwidth_used().total_microseconds() / static_cast<double>(bandwidth_.total_microseconds())) > (1.0 / contexts_.size());
}

context* band_scheduler_t::select_next_context() {
    boost::unique_lock<boost::mutex> lock(sched_mutex_);

    if (current()) {
        // lowering priority
        context* ctx = current();
        if (ctx->budget() < boost::posix_time::microseconds(0) && utilization_over_bandwidth(ctx)) {
            contexts_.erase(contexts_t::s_iterator_to(*ctx));
            contexts_.push_back(*ctx);
        }
    }

    context* over = NULL;
    context* next = NULL;
    for (context& ctx : contexts_) {
        if (ctx.is_suspended()) {
            if (ctx.budget() < boost::posix_time::microseconds(0) && utilization_over_bandwidth(&ctx)) {
                if (!over) {
                    over = &ctx;
                }
            } else {
                if (!next) {
                    next = &ctx;
                }
            }
            if (over && next) {
                break;
            }
        }
    }

    if (!next && over) {
        next = over;
    }

#if 0

    if (!current()) {
        return next;
    }


    if (next && next != current() && utilization_over_bandwidth(next)) {
        // yield_chance();
        if (current()->is_suspended()) {
            return current();
        }
    }

#endif
    return next;
}

void band_scheduler_t::submit(context* ctx) {
    boost::unique_lock<boost::mutex> lock(fire_mutex_);
    command cmd;

    counter_.fetch_sub(1);
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
    ctx->update_budget(duration);
}

void band_scheduler_t::run() {
    while (true) {
        {
            while (!counter_.load()) {
                boost::this_thread::yield();
            }
        }
        if ((current_ = select_next_context())) {
            submit(current());
        }
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */