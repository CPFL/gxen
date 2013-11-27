#ifndef A3_INSTRUMENTS_H_
#define A3_INSTRUMENTS_H_
#include <boost/noncopyable.hpp>
#include "a3.h"
#include "duration.h"
#include "pv_slot.h"
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

    duration_t increment_shadowing(const duration_t& time) {
        shadowing_ += time;
        return shadowing_;
    }

    void clear_shadowing_utilization() {
        flush_times_ = 0;
        shadowing_times_ = 0;
        shadowing_ = boost::posix_time::microseconds(0);
    }

    void hypercall(const command& cmd, slot_t* slot);

 private:
    context* ctx_;

    // shadowing utilization
    uint64_t flush_times_;
    uint64_t shadowing_times_;
    duration_t shadowing_;

    // hypercalls
    uint64_t hypercalls_;
};

}  // namespace a3
#endif  // A3_INSTRUMENTS_H_
/* vim: set sw=4 ts=4 et tw=80 : */
