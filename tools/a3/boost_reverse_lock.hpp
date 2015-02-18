// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
// (C) Copyright 2012 Vicente J. Botet Escriba

#ifndef BOOST_THREAD_REVERSE_LOCK_HPP
#define BOOST_THREAD_REVERSE_LOCK_HPP
#include <boost/noncopyable.hpp>
#include <boost/thread/detail/config.hpp>
#include <boost/thread/locks.hpp>

namespace boost
{

    template<typename Lock>
    class reverse_lock : boost::noncopyable
    {

    public:
        explicit reverse_lock(Lock& m_)
        : m(m_)
        {
            if (m.owns_lock())
            {
              m.unlock();
            }
        }
        ~reverse_lock()
        {
          m.lock();
        }

    private:
      Lock& m;
    };

}

#endif // header
