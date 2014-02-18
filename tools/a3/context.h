#ifndef A3_CONTEXT_H_
#define A3_CONTEXT_H_
#include <array>
#include <memory>
#include <queue>
#include <boost/unordered_map.hpp>
#include <boost/noncopyable.hpp>
#include <boost/ptr_container/ptr_unordered_map.hpp>
#include <boost/intrusive/list_hook.hpp>
#include "a3.h"
#include "lock.h"
#include "channel.h"
#include "bar1_channel.h"
#include "bar3_channel.h"
#include "session.h"
#include "page_table.h"
#include "instruments.h"
#include "duration.h"
#include "pfifo.h"
#include "poll_area.h"
namespace a3 {
namespace barrier {
class table;
}  // namespace barrier

struct slot_t;
class pv_page;

class context : private boost::noncopyable, public boost::intrusive::list_base_hook<> {
 public:
    typedef boost::unordered_multimap<uint64_t, channel*> channel_map;

    context(session* session, bool through);
    virtual ~context();
    bool handle(const command& command);
    void write_bar0(const command& command);
    void write_bar1(const command& command);
    void write_bar3(const command& command);
    void write_bar4(const command& command);
    void read_bar0(const command& command);
    void read_bar1(const command& command);
    void read_bar3(const command& command);
    void read_bar4(const command& command);
    void read_barrier(uint64_t addr, const command& command);
    void write_barrier(uint64_t addr, const command& command);
    bool through() const { return through_; }
    bar1_channel_t* bar1_channel() { return bar1_channel_.get(); }
    const bar1_channel_t* bar1_channel() const { return bar1_channel_.get(); }
    bar3_channel_t* bar3_channel() { return bar3_channel_.get(); }
    const bar3_channel_t* bar3_channel() const { return bar3_channel_.get(); }
    channel* channels(int id) { return channels_[id].get(); }
    const channel* channels(int id) const { return channels_[id].get(); }
    barrier::table* barrier() { return barrier_.get(); }
    const barrier::table* barrier() const { return barrier_.get(); }
    channel_map* ramin_channel_map() { return &ramin_channel_map_; }
    const channel_map* ramin_channel_map() const { return &ramin_channel_map_; }
    uint64_t vram_size() const { return A3_MEMORY_SIZE; }
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
        return virt + id() * A3_DOMAIN_CHANNELS;
    }
    uint32_t get_virt_channel_id(uint32_t phys) const {
        return phys - id() * A3_DOMAIN_CHANNELS;
    }
    uint32_t id() const { return id_; }
    int domid() const { return domid_; }
    bool flush(uint64_t pd, bool bar = false);
    command* buffer() { return session_->buffer(); }
    uint64_t bar3_address() const { return bar3_address_; }
    bool in_memory_range(uint64_t phys) const {
        return get_virt_address(phys) < vram_size();
    }
    bool in_memory_size(uint64_t size) const {
        return size <= vram_size();
    }

    bool para_virtualized() const { return para_virtualized_; }
    pv_page* pgds(uint32_t id) {
        return pgds_[id];
    }
    struct page_entry guest_to_host(const struct page_entry& entry);

    instruments_t* instruments() const { return instruments_.get(); }

    // BAND
    bool enqueue(const command& cmd);
    bool dequeue(command* cmd);
    bool is_suspended();
    duration_t budget() const { return budget_; }
    duration_t bandwidth() const { return bandwidth_; }
    duration_t bandwidth_used() const { return bandwidth_used_; }
    duration_t sampling_bandwidth_used() const { return sampling_bandwidth_used_; }
    duration_t sampling_bandwidth_used_100() const { return sampling_bandwidth_used_100_; }
    void replenish(const duration_t& credit, const duration_t& threshold, const duration_t& bandwidth, bool idle);
    void clear_sampling_bandwidth_used(uint64_t point);
    mutex_t& band_mutex() { return band_mutex_; }
    void update_budget(const duration_t& credit);

    uint32_t& reg32(uint64_t offset) {
        return reg32_[offset / sizeof(uint32_t)];
    }
    pfifo_t* pfifo() { return &pfifo_; }
    const pfifo_t* pfifo() const { return &pfifo_; }
    const poll_area_t* poll_area() const { return &poll_area_; }

 private:
    void initialize(int domid, bool para);
    void playlist_update(uint32_t reg_addr, uint32_t cmd);
    void flush_tlb(uint32_t vspace, uint32_t trigger);
    uint32_t decode_to_virt_ramin(uint32_t value);
    uint32_t encode_to_shadow_ramin(uint32_t value);
    bool shadow_ramin_to_phys(uint64_t shadow, uint64_t* phys);
    int a3_call(const command& command, slot_t* slot);
    uint32_t& pv32(uint64_t offset) {
        return pv32_[offset / sizeof(uint32_t)];
    }
    pv_page* lookup_by_pv_id(uint32_t id) {
        auto it = allocated_.find(id);
        if (it == allocated_.end()) {
            return nullptr;
        }
        return it->second;
    }
    int pv_map(pv_page* pgt, uint32_t index, uint64_t guest, uint64_t host);

    session* session_;
    bool through_;
    bool initialized_;
    int domid_;
    uint32_t id_;  // virtualized GPU id
    std::unique_ptr<bar1_channel_t> bar1_channel_;
    std::unique_ptr<bar3_channel_t> bar3_channel_;
    std::array<std::unique_ptr<channel>, A3_DOMAIN_CHANNELS> channels_;
    std::unique_ptr<barrier::table> barrier_;
    poll_area_t poll_area_;
    std::unique_ptr<uint32_t[]> reg32_;
    channel_map ramin_channel_map_;
    uint64_t bar3_address_;
    pfifo_t pfifo_;

    // instruments_t
    std::unique_ptr<instruments_t> instruments_;

    // PV
    bool para_virtualized_;
    std::unique_ptr<uint32_t[]> pv32_;
    uint8_t* guest_;
    boost::ptr_unordered_map<const uint32_t, pv_page> allocated_;
    std::array<pv_page*, A3_DOMAIN_CHANNELS> pgds_;
    pv_page* pv_bar1_pgd_;
    pv_page* pv_bar1_large_pgt_;
    pv_page* pv_bar1_small_pgt_;
    pv_page* pv_bar3_pgd_;
    pv_page* pv_bar3_pgt_;

    // only touched by BAND scheduler
    mutex_t band_mutex_;
    duration_t budget_;
    duration_t bandwidth_;
    duration_t bandwidth_used_;
    duration_t sampling_bandwidth_used_;
    duration_t sampling_bandwidth_used_100_;
    std::queue<command> suspended_;
};

}  // namespace a3
#endif  // A3_CONTEXT_H_
/* vim: set sw=4 ts=4 et tw=80 : */
