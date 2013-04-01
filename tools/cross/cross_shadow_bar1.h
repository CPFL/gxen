#ifndef CROSS_SHADOW_BAR1_H_
#define CROSS_SHADOW_BAR1_H_
#include "cross.h"
#include "cross_channel.h"
namespace cross {

class context;

class shadow_bar1 : public channel {
 public:
    shadow_bar1(context* ctx);

 private:
    context* ctx_;
};

}  // namespace cross
#endif  // CROSS_SHADOW_BAR1_H_
/* vim: set sw=4 ts=4 et tw=80 : */
