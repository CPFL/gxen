#ifndef HW_NVC0_NVC0_NONCOPYABLE_H_
#define HW_NVC0_NVC0_NONCOPYABLE_H_

namespace nvc0 {

// guard from ADL
// see http://ml.tietew.jp/cppll/cppll_novice/thread_articles/1652
namespace noncopyable_ {

template<class T = void>
class noncopyable {
 protected:
  noncopyable() {}
  ~noncopyable() {}

 private:
  noncopyable(const noncopyable&);
  const T& operator=(const T&);
};

template<>
class noncopyable<void> {
 protected:
  noncopyable() {}
  ~noncopyable() {}

 private:
  noncopyable(const noncopyable&);
  void operator=(const noncopyable&);
};



}  // namespace noncopyable_

using noncopyable_::noncopyable;

} // namespace nvc0
#endif  // HW_NVC0_NVC0_NONCOPYABLE_H_
