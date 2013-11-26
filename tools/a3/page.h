#ifndef A3_PAGE_H_
#define A3_PAGE_H_
#include <boost/noncopyable.hpp>
#include "a3.h"
#include "vram.h"
namespace a3 {

class page : private boost::noncopyable {
 public:
    page(std::size_t n = 1);
    ~page();
    void clear();
    uint64_t address() const { return vram_->address(); }
    void write32(uint64_t offset, uint32_t value);
    uint32_t read32(uint64_t offset);
    void write(uint64_t offset, uint32_t value, std::size_t s);
    uint32_t read(uint64_t offset, std::size_t s);
    std::size_t page_size() const;
    uint64_t size() const;

 private:
    vram_t* vram_;
};

}  // namespace a3
#endif  // A3_PAGE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
