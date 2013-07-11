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

band_scheduler::band_scheduler(const boost::posix_time::time_duration& wait, const boost::posix_time::time_duration& designed)
    : wait_(wait)
    , designed_(designed)
    , thread_()
    , mutex_()
    , cond_()
    , suspended_()
    , contexts_()
{
}


void band_scheduler::register_context(context* ctx) {
    contexts_.push_back(ctx);
}

void band_scheduler::unregister_context(context* ctx) {
    contexts_.erase(std::find(contexts_.begin(), contexts_.end(), ctx));
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
    boost::unique_lock<boost::mutex> lock(mutex_);
    if (current() && current_ != ctx) {
        suspend(ctx, cmd);
        return;
    }
    acquire(ctx);
    dispatch(ctx, cmd);
}

void band_scheduler::acquire(context* ctx) {
    current_ = ctx;
    if (ctx != actual_) {
        A3_SYNCHRONIZED(device::instance()->mutex()) {
            device::instance()->try_acquire_gpu(ctx);
        }
        actual_ = ctx;
    }
}

void band_scheduler::suspend(context* ctx, const command& cmd) {
    if (!ctx->is_suspended()) {
        suspended_.push(ctx);
    }
    ctx->suspend(cmd);
}

void band_scheduler::dispatch(context* ctx, const command& cmd) {
    device::instance()->bar1()->write(ctx, cmd);
}

context* band_scheduler::completion(boost::unique_lock<boost::mutex>& lock) {
    if (!current()) {
        if (suspended_.empty()) {
            return NULL;
        }
        context* next = suspended_.front();
        suspended_.pop();
        return next;
    }

    assert(current());

    if (suspended_.empty()) {
        current_ = NULL;
        return NULL;
    }

    boost::condition_variable_any cond;
    context* next = suspended_.front();
    if (next != current() && next->utilization() > next->bandwidth()) {
        lock.unlock();
        boost::this_thread::sleep(designed_);
        lock.lock();
        if (device::instance()->is_active(current())) {
            return NULL;
        }
    }
    suspended_.pop();
    return next;
}

void band_scheduler::run() {
    boost::condition_variable_any cond;
    while (true) {
        boost::unique_lock<boost::mutex> lock(mutex_);
        while (device::instance()->is_active(current())) {
            cond.timed_wait(lock, wait_);
        }
        context* next = completion(lock);
        if (next) {
            current_ = next;
        }
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
