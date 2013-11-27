#ifndef A3_DIRECT_SCHEDULER_H_
#define A3_DIRECT_SCHEDULER_H_
#include "a3.h"
#include "scheduler.h"
namespace a3 {

class context;

class direct_scheduler_t : public scheduler_t {
 public:
    virtual void enqueue(context* ctx, const command& cmd);
};

}  // namespace a3
#endif  // A3_DIRECT_SCHEDULER_H_
/* vim: set sw=4 ts=4 et tw=80 : */
