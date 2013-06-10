#ifndef A3_TIMER_H_
#define A3_TIMER_H_
#include <boost/noncopyable.hpp>
#include <boost/thread.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include "a3.h"
namespace a3 {

class context;

class timer_t : private boost::noncopyable {
 public:
    timer_t(const boost::posix_time::time_duration& wait);
    ~timer_t();
    void start();
    void stop();

 private:
    void run();

    boost::posix_time::time_duration wait_;
    boost::scoped_ptr<boost::thread> thread_;
};

}  // namespace a3
#endif  // A3_TIMER_H_
/* vim: set sw=4 ts=4 et tw=80 : */
