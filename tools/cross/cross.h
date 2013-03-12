#ifndef CROSS_H_
#define CROSS_H_
namespace cross {

#define CROSS_ENDPOINT "/tmp/cross_endpoint"

class command {
 public:
    struct container {
        uint32_t type;
        uint32_t value;
        uint32_t offset;
        uint32_t payload;
    };

    uint32_t type() const { return data_.type; }
    uint32_t value() const { return data_.value; }
    uint32_t offset() const { return data_.offset; }
    uint32_t payload() const { return data_.payload; }

 private:
    container data_;
};

}  // namespace cross
#endif  // CROSS_H_
/* vim: set sw=4 ts=4 et tw=80 : */
