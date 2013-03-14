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
    switch (cmd.type) {
        case command::TYPE_INIT:
            break;
        case command::TYPE_WRITE:
            break;
        case command::TYPE_READ:
            break;
    }
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
