#ifndef A3_PLAYLIST_H_
#define A3_PLAYLIST_H_
#include <bitset>
#include <boost/noncopyable.hpp>
#include "a3.h"
#include "make_unique.h"
#include "page.h"
namespace a3 {

class page;
class context;

template<size_t N>
struct engine_t {
 public:
    engine_t()
        : channels_()
        , pages_()
        , cursor_()
    {
    }

    page* toggle() {
        cursor_ ^= 1;
        const int index = cursor_ & 0x1;
        if (!pages_[index]) {
            pages_[index] = make_unique<page>(N);
        }
        return pages_[index].get();
    }

    void set(int index, bool val) {
        channels_.set(index, val);
    }

    bool get(int index) {
        return channels_[index];
    }
 private:
    std::bitset<A3_CHANNELS> channels_;
    std::array<std::unique_ptr<page>, 2> pages_;
    int cursor_;
};

class playlist_t : private boost::noncopyable {
 public:
    virtual void update(context* ctx, uint64_t address, uint32_t cmd) = 0;
};

class nvc0_playlist_t : public playlist_t {
 public:
    nvc0_playlist_t() : engine_() { }
    virtual void update(context* ctx, uint64_t address, uint32_t cmd);
 private:
    engine_t<1> engine_;
};

class nve0_playlist_t : public playlist_t {
 public:
    static const int NR_ENGINES = 7;

    nve0_playlist_t() : engines_() { }
    virtual void update(context* ctx, uint64_t address, uint32_t cmd);
 private:
    std::array<engine_t<8>, NR_ENGINES> engines_;
};

}  // namespace a3
#endif  // A3_PLAYLIST_H_
/* vim: set sw=4 ts=4 et tw=80 : */
