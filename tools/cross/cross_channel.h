#ifndef CROSS_CHANNEL_H_
#define CROSS_CHANNEL_H_
#include <boost/scoped_ptr.hpp>
#include "cross.h"
#include "cross_session.h"
namespace cross {
class shadow_page_table;

class channel {
 public:
    channel(int id);
    ~channel();
    shadow_page_table* table() { return table_.get(); }
    const shadow_page_table* table() const { return table_.get(); }
    int id() const { return id_; }

 private:
    int id_;
    boost::scoped_ptr<shadow_page_table> table_;
};

}  // namespace cross
#endif  // CROSS_CHANNEL_H_
/* vim: set sw=4 ts=4 et tw=80 : */
