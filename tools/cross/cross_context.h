#ifndef CROSS_CONTEXT_H_
#define CROSS_CONTEXT_H_
#include "cross.h"
#include "cross_session.h"
namespace cross {

class context : public session<context> {
 public:
    context(boost::asio::io_service& io_service);
    virtual ~context();
    void accept();
    void handle(const command& command);
    void write_bar0(const command& command);
    void write_bar1(const command& command);
    void write_bar3(const command& command);
    void read_bar0(const command& command);
    void read_bar1(const command& command);
    void read_bar3(const command& command);

 private:
    bool accepted_;
    int domid_;
    uint32_t id_;  // virtualized GPU id
};

}  // namespace cross
#endif  // CROSS_CONTEXT_H_
/* vim: set sw=4 ts=4 et tw=80 : */
