#ifndef CROSS_CONTEXT_H_
#define CROSS_CONTEXT_H_
#include "cross.h"
#include "cross_session.h"
namespace cross {

class context : public session<context> {
 public:
    context(boost::asio::io_service& io_service);
    void handle(const command& command);
};

}  // namespace cross
#endif  // CROSS_CONTEXT_H_
/* vim: set sw=4 ts=4 et tw=80 : */
