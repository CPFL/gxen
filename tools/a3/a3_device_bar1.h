#ifndef A3_DEVICE_BAR1_H_
#define A3_DEVICE_BAR1_H_
#include <memory>
#include <boost/noncopyable.hpp>
#include "a3.h"
#include "a3_page.h"
#include "a3_page_table.h"
#include "a3_device.h"
#include "a3_size.h"
#include "a3_poll_area.h"
namespace a3 {

class context;

// TODO(Yusuke Suzuki):
// This is raw BAR1 value 128MB
static const uint64_t kBAR1_ARENA_SIZE = 128 * size::MB;

// Only considers first 0x1000 tables
class device_bar1 : private boost::noncopyable {
 public:
    device_bar1(device::bar_t bar);
    uint64_t address() const { return directory_.address(); }
    void refresh();
    void refresh_poll_area();
    void shadow(context* ctx);
    void flush();
    void write(context* ctx, const command& cmd);
    uint32_t read(context* ctx, const command& cmd);
    void pv_scan(context* ctx);
    void pv_reflect_entry(context* ctx, bool big, uint32_t index, uint64_t entry);
    poll_area_t* poll_area() { return poll_area_.get(); }

 private:
    void map(uint64_t virt, const struct page_entry& entry);

    page ramin_;
    page directory_;
    page entry_;
    std::unique_ptr<poll_area_t> poll_area_;
};

}  // namespace a3
#endif  // A3_DEVICE_BAR1_H_
/* vim: set sw=4 ts=4 et tw=80 : */
