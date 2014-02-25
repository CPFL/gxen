#ifndef A3_RADIX_TREE_H_
#define A3_RADIX_TREE_H_
#include <cstdint>
#include <array>
#include <memory>
namespace a3 {

class radix_tree_t {
 public:
  static const uint64_t STAGE_BIT = 10;
  static const uint64_t MASK = (1 << STAGE_BIT) - 1;

  struct next_t { virtual ~next_t() { } };
  struct entries_t : public next_t {
    std::array<uint64_t, (1 << STAGE_BIT)> array;
  };
  struct stage_t : public next_t {
    std::array<std::shared_ptr<next_t>, (1 << STAGE_BIT)> array;
  };

  radix_tree_t()
      : first_()
  {
  }

  uint64_t lookup(uint64_t address) {
    if (entries_t* stage = lookup_entries(address, false)) {
      const uint32_t index = address & MASK;
      return static_cast<entries_t*>(stage)->array[index];
    }
    return 0;
  }

  void insert(uint64_t address, uint64_t value) {
    entries_t* stage = lookup_entries(address, true);
    ASSERT(stage);
    const uint32_t index = address & MASK;
    static_cast<entries_t*>(stage)->array[index] = value;
  }

 private:
  entries_t* lookup_entries(uint64_t address, bool create) {
    next_t* stage = &first_;
    for (uint32_t i = 0; i < 3; ++i) {
      const uint32_t index = (address >> (STAGE_BIT * (3 - i))) & MASK;
      stage_t* current = static_cast<stage_t*>(stage);
      if (!current->array[index]) {
        if (!create) {
          return nullptr;
        }
        if (i == 3) {
          current->array[index] = std::shared_ptr<next_t>(new entries_t());
        } else {
          current->array[index] = std::shared_ptr<next_t>(new stage_t());
        }
      }
      stage = current->array[index].get();
    }
    return static_cast<entries_t*>(stage);
  }

  stage_t first_;
};

}  // namespace a3
#endif  // A3_RADIX_TREE_H_
