#ifndef A3_FIFO_SCHEDULER_H_
#define A3_FIFO_SCHEDULER_H_
#include <queue>
#include <boost/noncopyable.hpp>
#include <boost/thread.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/intrusive/list.hpp>
#include "a3.h"
#include "a3_lock.h"
#include "a3_scheduler.h"
#include "a3_timer.h"
#include "a3_context.h"
namespace a3 {

class fifo_scheduler_t : public scheduler_t {
 public:
    typedef boost::intrusive::list<context> contexts_t;

    fifo_scheduler_t(const boost::posix_time::time_duration& wait, const boost::posix_time::time_duration& period);
    virtual ~fifo_scheduler_t();
    virtual void start();
    virtual void stop();
    virtual void enqueue(context* ctx, const command& cmd);
    virtual void register_context(context* ctx);
    virtual void unregister_context(context* ctx);

 private:
    typedef std::pair<context*, command> fire_t;
    void run();
    void replenish();

    boost::posix_time::time_duration wait_;
    boost::posix_time::time_duration period_;
    boost::scoped_ptr<boost::thread> thread_;
    boost::scoped_ptr<boost::thread> replenisher_;
    boost::mutex mutex_;
    boost::condition_variable cond_;
    std::queue<fire_t> queue_;
    timer_t utilization_;
    contexts_t contexts_;
    boost::posix_time::time_duration bandwidth_;
};

}  // namespace a3
#endif  // A3_FIFO_SCHEDULER_H_
/* vim: set sw=4 ts=4 et tw=80 : */
