#ifndef HW_NVC0_NVC0_CONTEXT_H_
#define HW_NVC0_NVC0_CONTEXT_H_
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include "nvc0.h"
#include "nvc0_shadow_page_table.h"
#include "nvc0_mmio_barrier.h"
#include "nvc0_remapping.h"
#include "nvc0_tlb_flush.h"
#include "nvc0_poll_area.h"
#include "cross.h"
namespace nvc0 {

class context {
 public:
    explicit context(nvc0_state_t* state, uint64_t memory_size);
    nvc0_state_t* state() const { return state_; }
    mmio_barrier* barrier() { return &barrier_; }
    nvc0::remapping::table* remapping() { return &remapping_; }
    tlb_flush* tlb() { return &tlb_; }
    poll_area* poll() { return &poll_; }
    uint64_t pramin() const { return pramin_; }
    void set_pramin(uint64_t pramin) { pramin_ = pramin; }
    cross::command send(const cross::command& cmd);

    static context* extract(nvc0_state_t* state);

 private:
    nvc0_state_t* state_;
    mmio_barrier barrier_;
    nvc0::remapping::table remapping_;
    tlb_flush tlb_;
    poll_area poll_;
    uint64_t pramin_;  // 16bit shifted

    // ASIO
    boost::asio::io_service io_service_;
    boost::asio::local::stream_protocol::socket socket_;
    boost::mutex socket_mutex_;
};

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_CONTEXT_H_
/* vim: set sw=4 ts=4 et tw=80 : */
