#ifndef HW_NVC0_NVC0_ENABLE_IF_H_
#define HW_NVC0_NVC0_ENABLE_IF_H_
namespace nvc0 {

template<bool B, class T = void>
struct enable_if_c {
  typedef T type;
};
template<class T>
struct enable_if_c<false, T> {
    // NO TYPEDEF!
};

template<class Cond, class T = void>
struct enable_if : public enable_if_c<Cond::value, T> { };

template<bool B, class T = void>
struct disable_if_c {
    typedef T type;
};

template<typename T>
struct disable_if_c<true, T> {
    // NO TYPEDEF!
};

template<class Cond, class T = void>
struct disable_if : public disable_if_c<Cond::value, T> { };

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_ENABLE_IF_H_
/* vim: set sw=4 ts=4 et tw=80 : */
