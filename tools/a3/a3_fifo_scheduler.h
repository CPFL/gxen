#ifndef A3_FIFO_SCHEDULER_H_
#define A3_FIFO_SCHEDULER_H_
#include <queue>
#include <boost/noncopyable.hpp>
#include <boost/thread.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include "a3.h"
#include "a3_lock.h"
namespace a3 {

class context;
class device;

class fifo_scheduler : private boost::noncopyable {
 public:
    friend class device;

    fifo_scheduler(const boost::posix_time::time_duration& wait);
    ~fifo_scheduler();
    void start();
    void stop();

 private:
    typedef std::pair<context*, command> fire_t;
    void run();
    void enqueue(context* ctx, const command& cmd);

    boost::posix_time::time_duration wait_;
    boost::scoped_ptr<boost::thread> thread_;
    boost::mutex mutex_;
    boost::condition_variable cond_;
    std::queue<fire_t> queue_;
};

}  // namespace a3
#endif  // A3_FIFO_SCHEDULER_H_
/* vim: set sw=4 ts=4 et tw=80 : */
