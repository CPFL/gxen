#ifndef HW_NVC0_NVC0_IF_H_
#define HW_NVC0_NVC0_IF_H_
namespace nvc0 {

template<bool Cond, class Then, class Else>
struct if_c {
    typedef Then type;
};

template<class Then, class Else>
struct if_c<false, Then, Else> {
    typedef Else type;
};

template<class Cond, class Then, class Else>
struct if_ : public if_c<Cond::value, Then, Else> { };

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_IF_H_
/* vim: set sw=4 ts=4 et tw=80 : */
