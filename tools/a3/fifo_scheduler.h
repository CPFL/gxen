#ifndef A3_FIFO_SCHEDULER_H_
#define A3_FIFO_SCHEDULER_H_
#include <queue>
#include <memory>
#include <boost/thread.hpp>
#include "a3.h"
#include "context.h"
#include "sampler.h"
#include "scheduler.h"
#include "duration.h"
#include "timer.h"
namespace a3 {

class fifo_scheduler_t : public scheduler_t {
 public:
    fifo_scheduler_t(const duration_t& wait, const duration_t& period, const duration_t& sample);
    virtual ~fifo_scheduler_t();
    virtual void start();
    virtual void stop();
    virtual void enqueue(context* ctx, const command& cmd);

 private:
    typedef std::pair<context*, command> fire_t;
    void run();
    void replenish();
    void sampling();

    duration_t wait_;
    duration_t period_;
    duration_t gpu_idle_;
    std::unique_ptr<boost::thread> thread_;
    std::unique_ptr<boost::thread> replenisher_;
    std::unique_ptr<sampler_t> sampler_;
    boost::condition_variable cond_;
    std::queue<fire_t> queue_;
    timer_t utilization_;
    contexts_t contexts_;
    duration_t bandwidth_;
};

}  // namespace a3
#endif  // A3_FIFO_SCHEDULER_H_
/* vim: set sw=4 ts=4 et tw=80 : */
