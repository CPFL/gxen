#ifndef CROSS_CONTEXT_H_
#define CROSS_CONTEXT_H_
#include <boost/scoped_array.hpp>
#include <boost/checked_delete.hpp>
#include <boost/interprocess/smart_ptr/unique_ptr.hpp>
#include "cross.h"
#include "cross_session.h"
#include "cross_channel.h"
namespace cross {
namespace barrier {
class table;
}  // namespace barrier
class poll_area;

template<class T>
struct unique_ptr {
  typedef boost::interprocess::unique_ptr< T, boost::checked_deleter<T> > type;
};

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
    void read_barrier(uint64_t addr);
    void write_barrier(uint64_t addr, uint32_t value);
    channel* bar1_channel() { return bar1_channel_.get(); }
    const channel* bar1_channel() const { return bar1_channel_.get(); }
    channel* bar3_channel() { return bar3_channel_.get(); }
    const channel* bar3_channel() const { return bar3_channel_.get(); }
    channel* channels(int id) { return channels_[id].get(); }
    const channel* channels(int id) const { return channels_[id].get(); }
    barrier::table* barrier() { return barrier_.get(); }
    const barrier::table* barrier() const { return barrier_.get(); }

 private:
    void fifo_playlist_update(uint64_t address, uint32_t count);
    void flush_tlb(uint32_t vspace, uint32_t trigger);
    // TODO(Yusuke Suzuki)
    // channel separation
    uint32_t get_phys_channel_id(uint32_t virt) const {
        return virt;
        // return virt + id_ * 64;
    }
    uint32_t get_virt_channel_id(uint32_t phys) const {
        return phys;
        // return phys - id_ * 64;
    }
    bool in_poll_area(uint64_t offset) const {
        return poll_area_ <= offset && offset < poll_area_ + (128 * 0x1000);
    }
    uint64_t get_phys_address(uint64_t virt) const {
        return virt + get_address_shift();
    }
    uint64_t get_virt_address(uint64_t phys) const {
        return phys - get_address_shift();
    }
    uint64_t get_address_shift() const {
        return id_ * CROSS_2G;
    }

    bool accepted_;
    int domid_;
    uint32_t id_;  // virtualized GPU id
    unique_ptr<channel>::type bar1_channel_;
    unique_ptr<channel>::type bar3_channel_;
    boost::array<unique_ptr<channel>::type, CROSS_DOMAIN_CHANNELS> channels_;
    unique_ptr<barrier::table>::type barrier_;

    uint64_t poll_area_;

    boost::scoped_array<uint32_t> reg_;

    // register value stores
    uint32_t reg_poll_;
    uint32_t reg_channel_kill_;
    uint32_t reg_tlb_vspace_;
    uint32_t reg_tlb_trigger_;
};

}  // namespace cross
#endif  // CROSS_CONTEXT_H_
/* vim: set sw=4 ts=4 et tw=80 : */
