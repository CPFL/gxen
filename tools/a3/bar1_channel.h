#ifndef A3_BAR1_CHANNEL_H_
#define A3_BAR1_CHANNEL_H_
#include <memory>
#include <boost/noncopyable.hpp>
#include "a3.h"
namespace a3 {
class software_page_table;
class context;
class page;

class bar1_channel_t : private boost::noncopyable {
 public:
    bar1_channel_t(context* ctx);
    void refresh(context* ctx, uint64_t addr);
    int id() const { return id_; }
    bool enabled() const { return enabled_; }
    uint64_t ramin_address() const { return ramin_address_; }
    void shadow(context* ctx);
    software_page_table* table() const { return table_.get(); }

 private:
    void detach(context* ctx, uint64_t addr);
    void attach(context* ctx, uint64_t addr);
    int id_;
    bool enabled_;
    uint64_t ramin_address_;
    std::unique_ptr<software_page_table> table_;
};

}  // namespace a3
#endif  // A3_BAR1_CHANNEL_H_
/* vim: set sw=4 ts=4 et tw=80 : */
