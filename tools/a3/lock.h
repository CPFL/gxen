#ifndef A3_LOCK_H_
#define A3_LOCK_H_
#include <type_traits>
#include <boost/thread.hpp>
namespace a3 {

typedef boost::recursive_mutex mutex_t;

#define A3_SYNCHRONIZED(m) \
    if (auto __LOCK__ = boost::unique_lock<std::decay<decltype(m)>::type>(m))

}  // namespace a3
#endif  // A3_LOCK_H_
/* vim: set sw=4 ts=4 et tw=80 : */
