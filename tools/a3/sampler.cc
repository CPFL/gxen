/*
 * A3 sampler
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
#include "context.h"
#include "scheduler.h"
#include "sampler.h"
namespace a3 {

sampler_t::sampler_t(scheduler_t* scheduler, duration_t sample)
    : scheduler_(scheduler)
    , sample_(sample)
    , thread_(nullptr)
    , bandwidth_100_()
    , bandwidth_500_()
{
}

void sampler_t::start() {
    if (thread_) {
        stop();
    }
    thread_.reset(new boost::thread(&sampler_t::run, this));
}

void sampler_t::stop() {
    if (thread_) {
        thread_->interrupt();
        thread_.reset();
    }
}

void sampler_t::add(const duration_t& time) {
    bandwidth_100_ += time;
    bandwidth_500_ += time;
}

void sampler_t::run() {
    bandwidth_100_ = boost::posix_time::microseconds(0);
    bandwidth_500_ = boost::posix_time::microseconds(0);
    uint64_t count = 0;
    uint64_t points = 0;
    while (true) {
        // sampling
        A3_SYNCHRONIZED(scheduler_->sched_mutex()) {
            if (!scheduler_->contexts().empty()) {
                A3_SYNCHRONIZED(scheduler_->fire_mutex()) {
                    if (bandwidth_500_ != boost::posix_time::microseconds(0)) {
                        // A3_FATAL(stdout, "UTIL: LOG %" PRIu64 "\n", count);
                        for (context& ctx : scheduler_->contexts()) {
                            // A3_FATAL(stdout, "UTIL[100]: %d => %f\n", ctx.id(), (static_cast<double>(ctx.sampling_bandwidth_used_100().total_microseconds()) / sampling_bandwidth_100_.total_microseconds()));
                            if (points % 5 == 4) {
                                // A3_FATAL(stdout, "UTIL[500]: %d => %f\n", ctx.id(), (static_cast<double>(ctx.sampling_bandwidth_used().total_microseconds()) / sampling_bandwidth_.total_microseconds()));
                            }
                            ctx.clear_sampling_bandwidth_used(points);
                        }
                        ++count;
                        points = (points + 1) % 5;
                    }
                    bandwidth_100_ = boost::posix_time::microseconds(0);
                    if (points % 5 == 4) {
                        bandwidth_500_ = boost::posix_time::microseconds(0);
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
