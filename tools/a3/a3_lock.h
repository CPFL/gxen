#ifndef A3_LOCK_H_
#define A3_LOCK_H_
#include <boost/thread.hpp>
namespace a3 {

typedef boost::recursive_mutex mutex_t;

#define A3_SYNCHRONIZED(m) \
    if (mutex_t::scoped_lock __LOCK__ = mutex_t::scoped_lock(m))

}  // namespace a3
#endif  // A3_H_
/* vim: set sw=4 ts=4 et tw=80 : */
