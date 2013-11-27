#ifndef A3_VRAM_H_
#define A3_VRAM_H_
#include <cstddef>
#include <cstdlib>
#include <boost/pool/pool.hpp>
#include <boost/noncopyable.hpp>
#include <boost/intrusive/list.hpp>
#include "a3.h"
#include "page_table.h"
namespace a3 {

class vram_manager_t;

class vram_t
    : public boost::intrusive::list_base_hook<>
    , public boost::noncopyable
    {
 public:
    friend class vram_manager_t;
    uint64_t address() const { return address_; }
    std::size_t n() const { return units_; }

 private:
    vram_t(uint64_t address, std::size_t n)
        : address_(address)
        , units_(n)
    { }
    uint64_t address_;
    std::size_t units_;
};

class vram_manager_t : private boost::noncopyable {
 public:
    typedef boost::intrusive::list<vram_t> free_list_t;
    vram_manager_t(uint64_t mem, uint64_t size);

    vram_t* malloc(std::size_t n = 1);  // n is the number of pages
    void free(vram_t* mem);
    std::size_t max_pages() const { return size_ / kPAGE_SIZE; }

 private:
    bool more(std::size_t n);

    uint64_t mem_;
    uint64_t size_;
    uint64_t cursor_;
    free_list_t free_list_;
};

}  // namespace a3
#endif  // A3_VRAM_H_
/* vim: set sw=4 ts=4 et tw=80 : */
