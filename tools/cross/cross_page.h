#ifndef CROSS_PAGE_H_
#define CROSS_PAGE_H_
#include <boost/noncopyable.hpp>
#include "cross.h"
#include "cross_vram.h"
namespace cross {

class page : private boost::noncopyable {
 public:
    page(std::size_t n = 1);
    ~page();
    void clear();
    uint64_t address() const { return vram_->address(); }
    void write32(uint64_t offset, uint32_t value);
    uint32_t read32(uint64_t offset);
    std::size_t page_size() const;
    uint64_t size() const;

 private:
    vram_memory* vram_;
};

}  // namespace cross
#endif  // CROSS_PAGE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
