/*
 * A3 Context sched
 *
 * Copyright (c) 2013 Yusuke Suzuki
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
#include "a3.h"
#include "lock.h"
#include "context.h"
namespace a3 {

bool context::enqueue(const command& cmd) {
    A3_SYNCHRONIZED(band_mutex()) {
        const bool ret = suspended_.empty();
        suspended_.push(cmd);
        return ret;
    }
    return false;
}

bool context::dequeue(command* cmd) {
    A3_SYNCHRONIZED(band_mutex()) {
        if (suspended_.empty()) {
            return false;
        }
        *cmd = suspended_.front();
        suspended_.pop();
        return true;
    }
    return false;
}

bool context::is_suspended() {
    A3_SYNCHRONIZED(band_mutex()) {
        return !suspended_.empty();
    }
    return false;
}


void context::update_budget(const duration_t& credit) {
    budget_ -= credit;
    bandwidth_used_ += credit;
    sampling_bandwidth_used_ += credit;
    sampling_bandwidth_used_100_ += credit;
}

void context::replenish(const duration_t& credit, const duration_t& threshold, const duration_t& bandwidth, bool idle) {
    A3_SYNCHRONIZED(band_mutex()) {
        budget_ += credit;

        if (idle && budget_ >= bandwidth) {
            budget_ = bandwidth;
        } else {
            if (budget_ > threshold) {
                budget_ = bandwidth; // boost::posix_time::microseconds(0);
            }

            if (budget_ < (-threshold)) {
                budget_ =  boost::posix_time::microseconds(0);
            }
        }
        bandwidth_used_ = boost::posix_time::microseconds(0);
    }
}

void context::clear_sampling_bandwidth_used(uint64_t point) {
    if (point % 5 == 4) {
        sampling_bandwidth_used_ = boost::posix_time::microseconds(0);
    }
    sampling_bandwidth_used_100_ = boost::posix_time::microseconds(0);
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
