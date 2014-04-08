#ifndef A3_CHANNEL_H_
#define A3_CHANNEL_H_
#include <memory>
#include <boost/noncopyable.hpp>
#include <boost/dynamic_bitset.hpp>
#include "a3.h"
namespace a3 {
class shadow_page_table;
class context;
class page;

class channel : private boost::noncopyable {
 public:
    typedef boost::dynamic_bitset<> page_table_reuse_t;

    channel(int id);
    uint64_t refresh(context* ctx, uint64_t addr);
    shadow_page_table* table() { return table_.get(); }
    const shadow_page_table* table() const { return table_.get(); }
    int id() const { return id_; }
    bool enabled() const { return enabled_; }
    uint64_t ramin_address() const { return ramin_address_; }
    page* shadow_ramin() { return shadow_ramin_.get(); }
    const page* shadow_ramin() const { return shadow_ramin_.get(); }
    void shadow(context* ctx);

    void flush(context* ctx);
    void tlb_flush_needed();

    void write_shadow_page_table(context* ctx, uint64_t shadow);
    void override_shadow(context* ctx, uint64_t shadow, page_table_reuse_t* reuse);
    bool is_overridden_shadow();
    void remove_overridden_shadow(context* ctx);

    inline page_table_reuse_t* generate_original() {
        original_.reset();
        original_.set(id(), 1);
        return &original_;
    }

    void submit(context* ctx, const command& cmd) {
        submitted_ = cmd.value;
    }

    uint32_t submitted() const { return submitted_; }

 private:
    void clear_tlb_flush_needed() {
        tlb_flush_needed_ = false;
    }
    bool detach(context* ctx, uint64_t addr);
    void attach(context* ctx, uint64_t addr);
    int id_;
    bool enabled_;
    bool tlb_flush_needed_;
    uint64_t ramin_address_;
    uint64_t shared_address_;
    uint32_t submitted_;
    std::unique_ptr<shadow_page_table> table_;
    std::unique_ptr<page> shadow_ramin_;

    page_table_reuse_t original_;
    page_table_reuse_t* derived_;
};

}  // namespace a3
#endif  // A3_CHANNEL_H_
/* vim: set sw=4 ts=4 et tw=80 : */
