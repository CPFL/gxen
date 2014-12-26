/*
 * Copyright (C) Yusuke Suzuki
 *
 * Keio University
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef A3_BENCH_H__
#define A3_BENCH_H__
#include <ctime>
#include "a3.h"
namespace a3 {

#define USEC_1SEC	1000000
#define USEC_1MSEC	1000
#define MSEC_1SEC	1000

class bench_t {
 public:
    bench_t(bool immediately)
        : opened_()
        , time_()
    {
        if (immediately) {
            open();
        }
    }

    static struct timespec diff(struct timespec start, struct timespec end) {
        struct timespec temp;
        if ((end.tv_nsec-start.tv_nsec) < 0) {
            temp.tv_sec = end.tv_sec-start.tv_sec - 1;
            temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
        } else {
            temp.tv_sec = end.tv_sec-start.tv_sec;
            temp.tv_nsec = end.tv_nsec-start.tv_nsec;
        }
        return temp;
    }

    static inline unsigned long time_to_ms(const struct timespec p) {
        return (p.tv_sec * USEC_1SEC + (p.tv_nsec / 1000)) / USEC_1MSEC;
    }

    static inline unsigned long time_to_us(const struct timespec p) {
        return (p.tv_sec * USEC_1SEC + (p.tv_nsec / 1000));
    }

    void open() {
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time_);
        opened_ = true;
    }

    struct timespec close(const char* prefix, int line, uint64_t threshold = 0) {
        struct timespec finish;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &finish);
        struct timespec delta = diff(time_, finish);
        if (prefix) {
            const long long unsigned ms = time_to_ms(delta);
            const long long unsigned us = time_to_us(delta);
            if (!threshold || threshold <= us) {
                A3_FATAL(stderr, "[%s:%d] ms:(%llu),us:(%llu)\n", prefix, line, ms, us);
            }
        }
        time_ = delta;
        opened_ = false;
        return time_;
    }

    bool opened() const { return opened_; }

 private:
    bool opened_;
    struct timespec time_;
};

#define A3_BENCH() \
    for (a3::bench_t bench(true); bench.opened(); bench.close(__func__, __LINE__))

#define A3_BENCH_THAN(us) \
    for (a3::bench_t bench(true); bench.opened(); bench.close(__func__, __LINE__, (us)))



}  // namespace a3
#endif
/* vim: set sw=4 ts=4 et tw=80 : */
