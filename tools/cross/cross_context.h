#ifndef CROSS_CONTEXT_H_
#define CROSS_CONTEXT_H_
#include <boost/scoped_ptr.hpp>
#include "cross.h"
#include "cross_session.h"
namespace cross {

class shadow_page_table;

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
    shadow_page_table* bar1_table() { return bar1_table_.get(); }
    const shadow_page_table* bar1_table() const { return bar1_table_.get(); }
    shadow_page_table* bar3_table() { return bar3_table_.get(); }
    const shadow_page_table* bar3_table() const { return bar3_table_.get(); }

 private:
    void fifo_playlist_update(uint64_t address, uint32_t count);
    uint32_t get_phys_channel_id(uint32_t virt) {
        return id_ * 64 + virt;
    }

    bool in_poll_area(uint64_t offset) {
        return poll_area_ <= offset && offset < poll_area_ + (128 * 0x1000);
    }

    bool accepted_;
    int domid_;
    uint32_t id_;  // virtualized GPU id
    boost::scoped_ptr<shadow_page_table> bar1_table_;
    boost::scoped_ptr<shadow_page_table> bar3_table_;

    uint64_t poll_area_;

    // register value stores
    uint32_t reg_pramin_;
    uint32_t reg_poll_;
    uint32_t reg_channel_kill_;
    uint32_t reg_playlist_;
    uint32_t reg_playlist_update_;
};

}  // namespace cross
#endif  // CROSS_CONTEXT_H_
/* vim: set sw=4 ts=4 et tw=80 : */
