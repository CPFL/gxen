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
#include <cstdint>
#include "a3.h"
#include "lock.h"
#include "context.h"
#include "device.h"
#include "device_bar1.h"
#include "band_scheduler.h"
namespace a3 {

band_scheduler_t::band_scheduler_t(const duration_t& period, const duration_t& sample)
    : period_(period)
    , gpu_idle_()
    , thread_()
    , replenisher_()
    , sampler_(new sampler_t(this, sample))
    , cond_()
    , current_()
    , utilization_()
    , bandwidth_()
    , counter_()
{
}

band_scheduler_t::~band_scheduler_t() {
    stop();
}

void band_scheduler_t::start() {
    if (thread_) {
        stop();
    }
    sampler_->start();
    thread_.reset(new boost::thread(&band_scheduler_t::run, this));
    replenisher_.reset(new boost::thread(&band_scheduler_t::replenish, this));
}

void band_scheduler_t::stop() {
    if (thread_) {
        sampler_->stop();
        thread_->interrupt();
        thread_.reset();
        replenisher_->interrupt();
        replenisher_.reset();
    }
}

static void yield_chance(const duration_t& duration) {
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
    A3_SYNCHRONIZED(counter_mutex_) {
        counter_ += 1;
        cond_.notify_one();
    }
}

void band_scheduler_t::replenish() {
    // uint64_t count = 0;
    while (true) {
        // replenish
        A3_SYNCHRONIZED(sched_mutex()) {
            if (!contexts().empty()) {
                A3_SYNCHRONIZED(fire_mutex()) {
                    duration_t period = bandwidth_ + gpu_idle_;
                    duration_t defaults = period_ / contexts().size();
                    previous_bandwidth_ = period;
                    // duration_t period = bandwidth_;
                    if (period != boost::posix_time::microseconds(0)) {
                        const auto budget = period / contexts().size();
                        for (context& ctx : contexts()) {
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

bool band_scheduler_t::utilization_over_bandwidth(context* ctx) const {
    if (bandwidth_ == boost::posix_time::microseconds(0)) {
        return true;
    }
    if (ctx->bandwidth_used() > (previous_bandwidth_ / contexts().size())) {
        return true;
    }
    return (ctx->bandwidth_used().total_microseconds() / static_cast<double>(bandwidth_.total_microseconds())) > (1.0 / contexts().size());
}

context* band_scheduler_t::select_next_context(bool idle) {
    A3_SYNCHRONIZED(sched_mutex()) {
        if (idle) {
            gpu_idle_ += gpu_idle_timer_.elapsed();
        }

        if (current()) {
            // lowering priority
            context* ctx = current();
            if (ctx->budget() < boost::posix_time::microseconds(0) && utilization_over_bandwidth(ctx)) {
                contexts().erase(contexts_t::s_iterator_to(*ctx));
                contexts().push_back(*ctx);
            }
        }

        context* band = nullptr;
        context* under = nullptr;
        context* over = nullptr;
        for (context& ctx : contexts()) {
            if (ctx.is_suspended()) {
                if (ctx.budget() < boost::posix_time::microseconds(0)) {
                    if (!over) {
                        over = &ctx;
                    }
                } else if (utilization_over_bandwidth(&ctx)) {
                    if (!band) {
                        band = &ctx;
                    }
                } else {
                    if (!under) {
                        under = &ctx;
                    }
                }
                if (over && under && band) {
                    break;
                }
            }
        }

        context* next =
            (under) ? under :
            (band)  ? band  : over;

        if (!current()) {
            return next;
        }

        if (next && next != current() && utilization_over_bandwidth(next) && !utilization_over_bandwidth(current()) && next->bandwidth_used() > current()->bandwidth_used()) {
            yield_chance(boost::posix_time::microseconds(500));
            if (current()->is_suspended()) {
                return current();
            }
        }

        return next;
    }
    return nullptr;  // Makes compiler happy
}

void band_scheduler_t::submit(context* ctx) {
    A3_SYNCHRONIZED(fire_mutex()) {
        command cmd;

        ctx->dequeue(&cmd);

        utilization_.start();
        A3_SYNCHRONIZED(device()->mutex()) {
            device()->bar1()->write(ctx, cmd);
        }

        while (device()->is_active(ctx)) {
            boost::this_thread::yield();
        }

        const auto duration = utilization_.elapsed();
        bandwidth_ += duration;
        sampler_->add(duration);
        ctx->update_budget(duration);
    }
}

void band_scheduler_t::run() {
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

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
