/*
 * A3 scheduler
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
#include <map>
#include <utility>
#include "a3.h"
#include "a3_scheduler.h"
#include "a3_context.h"
#include "a3_registers.h"
namespace a3 {

void scheduler_t::show_utilization(contexts_t& contexts, const boost::posix_time::time_duration& sampling_bandwidth)
{
    std::map<int, std::pair<double, double>> results;
    for (context& ctx : contexts) {
        results.insert(std::make_pair(ctx.id(), std::make_pair(static_cast<double>(ctx.sampling_bandwidth_used().total_microseconds()) / sampling_bandwidth.total_microseconds(), static_cast<double>(ctx.budget().total_microseconds()) / 1000.0)));
        ctx.clear_sampling_bandwidth_used();
    }
    for (const auto& pair : results) {
        A3_FATAL(stdout, "UTIL: %d => %f (budget: %f)\n", pair.first, pair.second.first, pair.second.second);
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
