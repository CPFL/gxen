#ifndef A3_DEVICE_BAR3_H_
#define A3_DEVICE_BAR3_H_
#include <boost/noncopyable.hpp>
#include "a3.h"
#include "a3_page.h"
#include "a3_device.h"
namespace a3 {

class context;

class device_bar3 : private boost::noncopyable {
 public:
    device_bar3(device::bar_t bar);
 private:
};

}  // namespace a3
#endif  // A3_DEVICE_BAR3_H_
/* vim: set sw=4 ts=4 et tw=80 : */
