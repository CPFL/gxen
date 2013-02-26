#ifndef HW_NVC0_NVC0_BIT_MASK_H_
#define HW_NVC0_NVC0_BIT_MASK_H_
#include <cstddef>

namespace nvc0 {
namespace bit_mask_detail {

// bit mask template
// see http://d.hatena.ne.jp/tt_clown/20090616/p1
template<std::size_t LowerBits, class Type = uint32_t>  // NOLINT
struct values {
  static const Type full = ~(Type(0));
  static const Type upper = ~((Type(1) << LowerBits) - 1);
  static const Type lower = (Type(1) << LowerBits) - 1;
};

template<std::size_t x, std::size_t y>
struct round_up {
    static const std::size_t value = (((x) + (y - 1)) & ~(y - 1));
};

template<std::size_t x, std::size_t y>
struct max_value {
    static const std::size_t value = (x < y) ? y : x;
};

template<std::size_t Width>
struct char_type;

template<> struct char_type<1> {
    typedef uint8_t type;
};

template<> struct char_type<2> {
    typedef uint16_t type;
};

template<> struct char_type<4> {
    typedef uint32_t type;
};

template<> struct char_type<8> {
    typedef uint64_t type;
};

template<std::size_t N, typename CharT>
struct result_type {
    typedef typename char_type<
	  max_value<sizeof(CharT), (round_up<N, 8>::value / 8)>::value
      >::type type;
};

}  // namespace bit_mask_detail

template<std::size_t N, typename CharT>
inline typename bit_mask_detail::result_type<N, CharT>::type bit_mask(CharT ch) {
    typedef typename bit_mask_detail::result_type<N, CharT>::type result_type;
    return bit_mask_detail::values<N, result_type>::lower & ch;
}


}  // namespace nvc0
#endif  // HW_NVC0_NVC0_BIT_MASK_H_
/* vim: set sw=4 ts=4 et tw=80 : */