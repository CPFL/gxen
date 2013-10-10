#ifndef A3_INSTRUMENTS_H_
#define A3_INSTRUMENTS_H_
#include <boost/noncopyable.hpp>
#include "a3.h"
#include "a3_lock.h"
#include "a3_context.h"
#include "a3_scheduler.h"
#include "a3_timer.h"
namespace a3 {

class context;

class instruments_t : private boost::noncopyable {
 public:
    instruments_t(context* ctx);

    uint64_t increment_flush_times() {
        return ++flush_times_;
    }

    uint64_t increment_shadowing_times() {
        return ++shadowing_times_;
    }

    boost::posix_time::time_duration increment_shadowing(const boost::posix_time::time_duration& time) {
        shadowing_ += time;
        return shadowing_;
    }

    void clear_shadowing_utilization() {
        flush_times_ = 0;
        shadowing_times_ = 0;
        shadowing_ = boost::posix_time::microseconds(0);
    }

 private:
    context* ctx_;

    // shadowing utilization
    uint64_t flush_times_;
    uint64_t shadowing_times_;
    boost::posix_time::time_duration shadowing_;
};

}  // namespace a3
#endif  // A3_INSTRUMENTS_H_
/* vim: set sw=4 ts=4 et tw=80 : */
