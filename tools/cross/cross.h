#ifndef CROSS_H_
#define CROSS_H_
namespace cross {

#define CROSS_ENDPOINT "/tmp/cross_endpoint"

class command {
 public:
    enum type_t {
        TYPE_INIT,
        TYPE_WRITE,
        TYPE_READ
    };

    uint32_t type;
    uint32_t value;
    uint32_t offset;
    uint32_t payload;
};


}  // namespace cross
#endif  // CROSS_H_
/* vim: set sw=4 ts=4 et tw=80 : */
