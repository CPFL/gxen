#ifndef A3_CREDIT_SCHEDULER_H_
#define A3_CREDIT_SCHEDULER_H_
#include <atomic>
#include <memory>
#include <boost/thread.hpp>
#include "a3.h"
#include "context.h"
#include "sampler.h"
#include "scheduler.h"
#include "duration.h"
#include "timer.h"
namespace a3 {

class context;

class credit_scheduler_t : public scheduler_t {
 public:
    credit_scheduler_t(const duration_t& period, const duration_t& sample);
    virtual ~credit_scheduler_t();
    virtual void start();
    virtual void stop();
    virtual void enqueue(context* ctx, const command& cmd);

 private:
    typedef std::pair<context*, command> fire_t;
    void run();
    void replenish();
    void sampling();
    context* current() const { return current_; }
    context* select_next_context(bool idle);
    void submit(context* ctx);

    duration_t period_;
    duration_t gpu_idle_;
    std::unique_ptr<boost::thread> thread_;
    std::unique_ptr<boost::thread> replenisher_;
    std::unique_ptr<sampler_t> sampler_;
    boost::mutex counter_mutex_;
    boost::condition_variable cond_;
    contexts_t contexts_;
    context* current_;
    timer_t utilization_;
    timer_t gpu_idle_timer_;
    duration_t bandwidth_;
    duration_t previous_bandwidth_;
    uint64_t counter_;
};

}  // namespace a3
#endif  // A3_CREDIT_SCHEDULER_H_
/* vim: set sw=4 ts=4 et tw=80 : */
