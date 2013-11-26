#ifndef A3_PV_PAGE_H_
#define A3_PV_PAGE_H_
#include <bitset>
#include "page.h"
namespace a3 {

class pv_page : public page {
 public:
    typedef std::bitset<A3_DOMAIN_CHANNELS + 2> bitset_t;

    static const int kBAR1 = A3_DOMAIN_CHANNELS;
    static const int kBAR3 = A3_DOMAIN_CHANNELS + 1;

    enum page_type_t {
	TYPE_NONE,
	TYPE_PGT,
	TYPE_PGD
    };

    pv_page(std::size_t n)
    : page(n)
    , page_type_(TYPE_NONE)
    , channel_bitset_()
    {}

    void set_page_type(page_type_t type) { page_type_ = type; }
    page_type_t page_type() const { return page_type_; }
    bitset_t* channel_bitset() { return &channel_bitset_; }

 private:
    page_type_t page_type_;
    std::bitset<A3_DOMAIN_CHANNELS + 2> channel_bitset_;
};

}  // namespace a3
#endif  // A3_PV_PAGE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
