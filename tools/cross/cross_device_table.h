#ifndef CROSS_DEVICE_TABLE_H_
#define CROSS_DEVICE_TABLE_H_
#include <boost/noncopyable.hpp>
#include "cross.h"
#include "cross_allocator.h"
namespace cross {

class device_page_directory : private boost::noncopyable {
 public:
    device_page_directory();
 private:
    page page_;
};

class device_table : private boost::noncopyable {
 public:
    device_table();

 private:
    device_page_directory page_directory_;
};

}  // namespace cross
#endif  // CROSS_DEVICE_TABLE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
