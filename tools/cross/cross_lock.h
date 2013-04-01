#ifndef CROSS_LOCK_H_
#define CROSS_LOCK_H_
#include <boost/thread.hpp>
namespace cross {

typedef boost::recursive_mutex mutex;

#define CROSS_SYNCHRONIZED(m) \
    if (mutex::scoped_lock __LOCK__ = mutex::scoped_lock(m))

}  // namespace cross
#endif  // CROSS_H_
/* vim: set sw=4 ts=4 et tw=80 : */
