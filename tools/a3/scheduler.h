#ifndef A3_SCHEDULER_H_
#define A3_SCHEDULER_H_
#include <queue>
#include <boost/noncopyable.hpp>
#include <boost/intrusive/list.hpp>
#include "a3.h"
#include "context.h"
namespace a3 {

class scheduler_t : private boost::noncopyable {
 public:
    typedef boost::intrusive::list<context> contexts_t;

    virtual ~scheduler_t() { }
    virtual void start() { }
    virtual void stop() { }
    virtual void enqueue(context* ctx, const command& cmd) = 0;

    void register_context(context* ctx);
    void unregister_context(context* ctx);
    contexts_t& contexts() { return contexts_; }
    const contexts_t& contexts() const { return contexts_; }
    boost::mutex& fire_mutex() { return fire_mutex_; }
    boost::mutex& sched_mutex() { return sched_mutex_; }

 protected:
    virtual void on_register_context(context* ctx) { }
    virtual void on_unregister_context(context* ctx) { }

 private:
    contexts_t contexts_;
    boost::mutex fire_mutex_;
    boost::mutex sched_mutex_;
};

}  // namespace a3
#endif  // A3_SCHEDULER_H_
/* vim: set sw=4 ts=4 et tw=80 : */
