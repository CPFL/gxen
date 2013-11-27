#ifndef A3_SCHEDULER_H_
#define A3_SCHEDULER_H_
#include <queue>
#include <boost/noncopyable.hpp>
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
    virtual void register_context(context* ctx) { }
    virtual void unregister_context(context* ctx) { }
};

}  // namespace a3
#endif  // A3_SCHEDULER_H_
/* vim: set sw=4 ts=4 et tw=80 : */
