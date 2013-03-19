#ifndef CROSS_CHANNEL_H_
#define CROSS_CHANNEL_H_
#include <boost/scoped_ptr.hpp>
#include "cross.h"
#include "cross_session.h"
namespace cross {
class shadow_page_table;
class context;

class channel {
 public:
    channel(int id);
    ~channel();
    void refresh(context* ctx, uint64_t addr);
    shadow_page_table* table() { return table_.get(); }
    const shadow_page_table* table() const { return table_.get(); }
    int id() const { return id_; }
    bool enabled() const { return enabled_; }
    uint64_t ramin_address() const { return ramin_address_; }

 private:
    int id_;
    bool enabled_;
    uint64_t ramin_address_;
    boost::scoped_ptr<shadow_page_table> table_;
};

}  // namespace cross
#endif  // CROSS_CHANNEL_H_
/* vim: set sw=4 ts=4 et tw=80 : */
