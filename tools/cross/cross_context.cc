#include <cstdlib>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <unistd.h>
#include "cross.h"
#include "cross_session.h"
#include "cross_context.h"
namespace cross {


context::context(boost::asio::io_service& io_service)
    : session(io_service) {
}

void context::handle(const command& cmd) {
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
