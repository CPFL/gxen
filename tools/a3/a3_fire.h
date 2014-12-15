#ifndef A3_FIRE_H_
#define A3_FIRE_H_
#include "a3.h"
namespace a3 {

class context;

class fire_t {
 public:
    fire_t()
        : offset_()
        , cmd_()
    { }

    fire_t(context* ctx, const command& cmd)
        : offset_(calculate_offset(ctx, cmd))
        , cmd_(cmd)
    { }

    static size_t calculate_offset(context* ctx, const command& cmd);

    inline size_t offset() const { return offset_; }
    inline const command& cmd() const { return cmd_; }

 private:
    size_t offset_;
    command cmd_;
};

}  // namespace a3
#endif  // A3_FIRE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
