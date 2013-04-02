#ifndef CROSS_DEVICE_BAR1_H_
#define CROSS_DEVICE_BAR1_H_
#include <boost/noncopyable.hpp>
#include "cross.h"
#include "cross_allocator.h"
namespace cross {

class context;

// Only considers first 0x1000 tables
class device_bar1 : private boost::noncopyable {
 public:
  device_bar1();
  uint64_t address() const { return directory_.address(); }
  void shadow(context* ctx);

 private:
  void map(uint64_t virt, uint64_t phys);

  page directory_;
  page entry_;
};

}  // namespace cross
#endif  // CROSS_DEVICE_BAR1_H_
/* vim: set sw=4 ts=4 et tw=80 : */
