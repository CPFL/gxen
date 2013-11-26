#ifndef A3_TIMER_H_
#define A3_TIMER_H_
#include <boost/noncopyable.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include "a3.h"
#include "lock.h"
namespace a3 {

class timer_t : private boost::noncopyable {
 public:
    void start() {
        start_ = boost::posix_time::microsec_clock::local_time();
    }

    boost::posix_time::time_duration elapsed() const {
        auto now = boost::posix_time::microsec_clock::local_time();
        return now - start_;
    }

 private:
    boost::posix_time::ptime start_;
};

}  // namespace a3
#endif  // A3_TIMER_H_
/* vim: set sw=4 ts=4 et tw=80 : */
