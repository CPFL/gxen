#ifndef CROSS_CONTEXT_H_
#define CROSS_CONTEXT_H_
#include <boost/scoped_array.hpp>
#include <boost/checked_delete.hpp>
#include <boost/interprocess/smart_ptr/unique_ptr.hpp>
#include <boost/unordered_map.hpp>
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

class playlist;

class context : public session<context> {
 public:
    typedef boost::unordered_multimap<uint64_t, channel*> channel_map;

    context(boost::asio::io_service& io_service, bool through);
    virtual ~context();
    void accept();
    void handle(const command& command);
    void write_bar0(const command& command);
    void write_bar1(const command& command);
    void write_bar3(const command& command);
    void read_bar0(const command& command);
    void read_bar1(const command& command);
    void read_bar3(const command& command);
    void read_barrier(uint64_t addr, const command& command);
    void write_barrier(uint64_t addr, const command& command);
    bool through() const { return through_; }
    channel* bar1_channel() { return bar1_channel_.get(); }
    const channel* bar1_channel() const { return bar1_channel_.get(); }
    channel* bar3_channel() { return bar3_channel_.get(); }
    const channel* bar3_channel() const { return bar3_channel_.get(); }
    channel* channels(int id) { return channels_[id].get(); }
    const channel* channels(int id) const { return channels_[id].get(); }
    barrier::table* barrier() { return barrier_.get(); }
    const barrier::table* barrier() const { return barrier_.get(); }
    channel_map* ramin_channel_map() { return &ramin_channel_map_; }
    const channel_map* ramin_channel_map() const { return &ramin_channel_map_; }
    playlist* fifo_playlist() { return fifo_playlist_.get(); }
    const playlist* fifo_playlist() const { return fifo_playlist_.get(); }
    uint64_t vram_size() const { return CROSS_2G; }
    uint64_t get_address_shift() const {
        return id() * vram_size();
    }
    uint64_t get_phys_address(uint64_t virt) const {
        return virt + get_address_shift();
    }
    uint64_t get_virt_address(uint64_t phys) const {
        return phys - get_address_shift();
    }
    uint32_t get_phys_channel_id(uint32_t virt) const {
        return virt + id() * CROSS_DOMAIN_CHANNELS;
    }
    uint32_t get_virt_channel_id(uint32_t phys) const {
        return phys - id() * CROSS_DOMAIN_CHANNELS;
    }
    uint32_t id() const { return id_; }
    int domid() const { return domid_; }
    uint64_t poll_area() const { return poll_area_; }

 private:
    void fifo_playlist_update(uint32_t reg_addr, uint32_t cmd);
    void flush_tlb(uint32_t vspace, uint32_t trigger);
    bool in_poll_area(uint64_t offset) const {
        return poll_area() <= offset && offset < poll_area() + (CROSS_DOMAIN_CHANNELS * 0x1000);
    }

    bool through_;
    bool accepted_;
    int domid_;
    uint32_t id_;  // virtualized GPU id
    unique_ptr<channel>::type bar1_channel_;
    unique_ptr<channel>::type bar3_channel_;
    boost::array<unique_ptr<channel>::type, CROSS_DOMAIN_CHANNELS> channels_;
    unique_ptr<barrier::table>::type barrier_;
    unique_ptr<playlist>::type fifo_playlist_;

    uint64_t poll_area_;

    boost::scoped_array<uint32_t> reg_;
    channel_map ramin_channel_map_;
};

}  // namespace cross
#endif  // CROSS_CONTEXT_H_
/* vim: set sw=4 ts=4 et tw=80 : */
