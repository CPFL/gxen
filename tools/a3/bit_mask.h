#ifndef A3_BIT_MASK_H_
#define A3_BIT_MASK_H_
#include <cstddef>

namespace a3 {
namespace bit_mask_detail {

template<std::size_t LowerBits>  // NOLINT
struct values {
  static const uint64_t full = ~(uint64_t(0));
  static const uint64_t upper = ~((uint64_t(1) << LowerBits) - 1);
  static const uint64_t lower = (uint64_t(1) << LowerBits) - 1;
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
    return bit_mask_detail::values<N>::lower & static_cast<uint64_t>(ch);
}

template<std::size_t N, typename CharT>
inline typename bit_mask_detail::result_type<N, CharT>::type bit_clear(CharT ch) {
    typedef typename bit_mask_detail::result_type<N, CharT>::type result_type;
    return bit_mask_detail::values<N>::upper & static_cast<uint64_t>(ch);
}

template<std::size_t N, typename CharT>
inline bool bit_check(CharT ch) {
    typedef typename bit_mask_detail::result_type<N, CharT>::type result_type;
    uint64_t one = 1;
    return (one << N) & static_cast<uint64_t>(ch);
}

inline uint32_t lower32(uint64_t data) {
    return static_cast<uint32_t>(data);
}

inline uint32_t upper32(uint64_t data) {
    return static_cast<uint32_t>((data >> 16) >> 16);
}

}  // namespace a3
#endif  // A3_BIT_MASK_H_
/* vim: set sw=4 ts=4 et tw=80 : */
