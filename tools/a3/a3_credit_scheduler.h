#ifndef A3_CREDIT_SCHEDULER_H_
#define A3_CREDIT_SCHEDULER_H_
#include <queue>
#include <atomic>
#include <boost/noncopyable.hpp>
#include <boost/thread.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/intrusive/list.hpp>
#include "a3.h"
#include "a3_lock.h"
#include "a3_context.h"
#include "a3_scheduler.h"
#include "a3_timer.h"
namespace a3 {

class context;

class credit_scheduler_t : public scheduler_t {
 public:
    typedef boost::intrusive::list<context> contexts_t;

    credit_scheduler_t(const boost::posix_time::time_duration& wait, const boost::posix_time::time_duration& designed, const boost::posix_time::time_duration& period, const boost::posix_time::time_duration& sample);
    virtual ~credit_scheduler_t();
    virtual void start();
    virtual void stop();
    virtual void register_context(context* ctx);
    virtual void unregister_context(context* ctx);
    virtual void enqueue(context* ctx, const command& cmd);

 private:
    typedef std::pair<context*, command> fire_t;
    void run();
    void replenish();
    void sampling();
    context* current() const { return current_; }
    context* select_next_context(bool idle);
    void submit(context* ctx);

    boost::posix_time::time_duration wait_;
    boost::posix_time::time_duration designed_;
    boost::posix_time::time_duration period_;
    boost::posix_time::time_duration sample_;
    boost::posix_time::time_duration gpu_idle_;
    boost::scoped_ptr<boost::thread> thread_;
    boost::scoped_ptr<boost::thread> replenisher_;
    boost::scoped_ptr<boost::thread> sampler_;
    boost::mutex mutex_;
    boost::mutex contexts_mutex_;
    boost::mutex fire_mutex_;
    boost::mutex sched_mutex_;
    boost::mutex counter_mutex_;
    boost::condition_variable cond_;
    contexts_t active_;
    contexts_t inactive_;
    contexts_t contexts_;
    context* current_;
    timer_t utilization_;
    timer_t gpu_idle_timer_;
    boost::posix_time::time_duration bandwidth_;
    boost::posix_time::time_duration sampling_bandwidth_;
    boost::posix_time::time_duration previous_bandwidth_;
    uint64_t counter_;
};

}  // namespace a3
#endif  // A3_CREDIT_SCHEDULER_H_
/* vim: set sw=4 ts=4 et tw=80 : */
