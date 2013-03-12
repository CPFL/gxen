#ifndef CROSS_H_
#define CROSS_H_
namespace cross {

#define CROSS_ENDPOINT "/tmp/cross_endpoint"

class command {
 public:
    struct container {
        uint32_t command;
        uint32_t value;
        uint32_t offset;
    };

 private:
    container data_;
};

}  // namespace cross
#endif  // CROSS_H_
/* vim: set sw=4 ts=4 et tw=80 : */
