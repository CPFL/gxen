#ifndef HW_NVC0_NVC0_CONTEXT_H_
#define HW_NVC0_NVC0_CONTEXT_H_
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/scoped_ptr.hpp>
#include "nvc0.h"
#include "a3/a3.h"
namespace nvc0 {

class context {
 public:
    explicit context(nvc0_state_t* state, uint64_t memory_size);
    nvc0_state_t* state() const { return state_; }
    uint64_t pramin() const { return pramin_; }
    void set_pramin(uint64_t pramin) { pramin_ = pramin; }
    uint32_t id() const { return id_; }
    // socket based
    a3::command send(const a3::command& cmd);
    // message passing
    a3::command message(const a3::command& cmd, bool read);
    void notify_bar3_change();

    static context* extract(nvc0_state_t* state);

 private:
    uint32_t id_;
    nvc0_state_t* state_;
    uint64_t pramin_;  // 16bit shifted

    // ASIO
    boost::asio::io_service io_service_;
    boost::asio::local::stream_protocol::socket socket_;
    boost::mutex socket_mutex_;
    boost::scoped_ptr<a3::interprocess::message_queue> req_queue_;
    boost::scoped_ptr<a3::interprocess::message_queue> res_queue_;
};

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_CONTEXT_H_
/* vim: set sw=4 ts=4 et tw=80 : */
