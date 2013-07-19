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
{
}


void band_scheduler_t::register_context(context* ctx) {
    boost::unique_lock<boost::mutex> lock(mutex_);
    inactive_.push_back(*ctx);
}

void band_scheduler_t::unregister_context(context* ctx) {
    boost::unique_lock<boost::mutex> lock(mutex_);
    inactive_.remove_if([ctx](const context& target) {
        return &target == ctx;
    });
    active_.remove_if([ctx](const context& target) {
        return &target == ctx;
    });
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

void band_scheduler_t::replenish() {
    uint64_t count = 0;
    while (true) {
        // replenish
        {
            boost::unique_lock<boost::mutex> lock(mutex_);
            if (active_.size() + inactive_.size()) {
                if (bandwidth_ != boost::posix_time::microseconds(0)) {
                    A3_LOG("UTIL: LOG %" PRIu64 "\n", count);
                    const auto budget = period_ / (inactive_.size() + active_.size());
                    for (context& ctx: active_) {
                        A3_LOG("UTIL: %d => %f\n", ctx.id(), (static_cast<double>(ctx.utilization().total_microseconds()) / bandwidth_.total_microseconds()));
                        ctx.replenish(budget);
                    }
                    for (context& ctx: inactive_) {
                        A3_LOG("UTIL: %d => %f\n", ctx.id(), (static_cast<double>(ctx.utilization().total_microseconds()) / bandwidth_.total_microseconds()));
                        ctx.replenish(budget);
                    }
                    ++count;
                }
                bandwidth_ = boost::posix_time::microseconds(0);
            }
        }
        boost::this_thread::sleep(period_);
        boost::this_thread::yield();
    }
}

void band_scheduler_t::enqueue(context* ctx, const command& cmd) {
    // on arrival
    {
        boost::unique_lock<boost::mutex> lock(mutex_);
        A3_LOG("ENQUEUE command\n");
        if (ctx->enqueue(cmd)) {
            inactive_.erase(contexts_t::s_iterator_to(*ctx));
            active_.push_back(*ctx);
        }
    }
    cond_.notify_one();
}

bool band_scheduler_t::utilization_over_bandwidth(context* ctx) const {
    if (bandwidth_ == boost::posix_time::microseconds(0)) {
        return false;
    }
    return (ctx->utilization().total_microseconds() / static_cast<double>(bandwidth_.total_microseconds())) > (1.0 / (inactive_.size() + active_.size()));
}

void band_scheduler_t::run() {
    boost::unique_lock<boost::mutex> lock(mutex_);
    boost::condition_variable_any cond;
    while (true) {
        // check queued fires
        while (active_.empty()) {
            A3_LOG("Q empty\n");
            cond_.wait(lock);
        }
        context* next = &active_.front();
        assert(next->is_suspended());

        A3_LOG("context comes %" PRIu32 "\n", next->id());

        if (current() && next != current() && utilization_over_bandwidth(next)) {
            // wait short time
            bool continue_current = false;
            if (current()->is_suspended()) {
                continue_current = true;
            } else {
                cond.timed_wait(lock, designed_);
                if (current()->is_suspended()) {
                    continue_current = true;
                }
            }

            if (continue_current) {
                next = current();
            }
            assert(next->is_suspended());
        }

        current_ = next;
        assert(current()->is_suspended());
        const command target = current()->dequeue();
        const bool inactive = !current()->is_suspended();
        if (inactive) {
            active_.erase(contexts_t::s_iterator_to(*current()));
            inactive_.push_back(*current());
        }
        A3_LOG("DEQUEUE command [will be %s]\n", inactive ? "inactive" : "active");

        utilization_.start();
        lock.unlock();

        A3_SYNCHRONIZED(device::instance()->mutex()) {
            device::instance()->bar1()->write(current(), target);
        }

        lock.lock();

        while (device::instance()->is_active(current())) {
            cond.timed_wait(lock, wait_);
        }

        // on completion

        const auto duration = utilization_.elapsed();
        bandwidth_ += duration;

        current()->update_utilization(duration);
        if (current()->is_suspended()) {
            // ulitization over budget
            if (current()->budget() < current()->utilization()) {
                // utilization ratio over bandwidth ratio
                if (utilization_over_bandwidth(current())) {
                    // lowering priority
                    active_.erase(contexts_t::s_iterator_to(*current()));
                    active_.push_back(*current());
                }
            }
        }
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
