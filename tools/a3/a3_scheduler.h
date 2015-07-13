#ifndef A3_SCHEDULER_H_
#define A3_SCHEDULER_H_
#include <queue>
#include <boost/noncopyable.hpp>
#include <boost/intrusive/list.hpp>
#include "a3.h"
namespace a3 {

class context;

enum class scheduler_type_t {
    FIFO,
    CREDIT,
    BAND,
    DIRECT
};

class scheduler_t : private boost::noncopyable {
 public:
    typedef boost::intrusive::list<context> contexts_t;

    virtual ~scheduler_t() { }
    virtual void start() { }
    virtual void stop() { }
    virtual void enqueue(context* ctx, const command& cmd) = 0;
    virtual void register_context(context* ctx) { }
    virtual void unregister_context(context* ctx) { }

    static void show_utilization(contexts_t& contexts, const boost::posix_time::time_duration& sampling_bandwidth);
};

}  // namespace a3
#endif  // A3_SCHEDULER_H_
/* vim: set sw=4 ts=4 et tw=80 : */
