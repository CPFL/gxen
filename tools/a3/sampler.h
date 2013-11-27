#ifndef A3_SAMPLER_H_
#define A3_SAMPLER_H_
#include <memory>
#include <boost/thread.hpp>
#include <boost/noncopyable.hpp>
#include "duration.h"
namespace a3 {

class scheduler_t;

class sampler_t : private boost::noncopyable {
 public:
    sampler_t(scheduler_t* scheduler, duration_t sample);
    void add(const duration_t& time);
    void start();
    void stop();
    void run();

 private:
    scheduler_t* scheduler_;
    duration_t sample_;
    std::unique_ptr<boost::thread> thread_;
    duration_t bandwidth_100_;
    duration_t bandwidth_500_;
};

}  // namespace a3
#endif  // A3_SAMPLER_H_
