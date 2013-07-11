#ifndef A3_BAND_SCHEDULER_H_
#define A3_BAND_SCHEDULER_H_
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

class band_scheduler : private boost::noncopyable {
 public:
    friend class device;

    band_scheduler(const boost::posix_time::time_duration& wait, const boost::posix_time::time_duration& designed);
    ~band_scheduler();
    void start();
    void stop();
    void register_context(context* ctx);
    void unregister_context(context* ctx);

 private:
    typedef std::pair<context*, command> fire_t;
    void run();
    void enqueue(context* ctx, const command& cmd);
    void suspend(context* ctx, const command& cmd);
    void dispatch(context* ctx, const command& cmd);
    void acquire(context* ctx);
    context* completion(boost::unique_lock<boost::mutex>& lock);
    context* current() const { return current_; }

    boost::posix_time::time_duration wait_;
    boost::posix_time::time_duration designed_;
    boost::scoped_ptr<boost::thread> thread_;
    boost::mutex mutex_;
    boost::condition_variable cond_;
    std::queue<context*> suspended_;
    std::vector<context*> contexts_;
    context* current_;
    context* actual_;
};

}  // namespace a3
#endif  // A3_BAND_SCHEDULER_H_
/* vim: set sw=4 ts=4 et tw=80 : */
