#ifndef A3_PLAYLIST_H_
#define A3_PLAYLIST_H_
#include <boost/noncopyable.hpp>
#include <boost/scoped_array.hpp>
#include "a3.h"
namespace a3 {

class page;
class context;

class playlist : private boost::noncopyable {
 public:
    playlist();
    uint64_t update(context* ctx, uint64_t address, uint32_t count);

 private:
    page* toggle();

    boost::scoped_array<page> pages_;
    int cursor_;
};

}  // namespace a3
#endif  // A3_PLAYLIST_H_
/* vim: set sw=4 ts=4 et tw=80 : */
