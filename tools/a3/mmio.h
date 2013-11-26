#ifndef A3_MMIO_H_
#define A3_MMIO_H_
#include <cstddef>
#include "bit_mask.h"
namespace a3 {
namespace mmio {

inline uint8_t read8(const volatile void *addr) {
    return *(const volatile uint8_t*) addr;
}

inline uint16_t read16(const volatile void *addr) {
    return *(const volatile uint16_t*) addr;
}

inline uint32_t read32(const volatile void *addr) {
    return *(const volatile uint32_t*) addr;
}

inline void write8(uint8_t b, volatile void *addr) {
    *(volatile uint8_t*) addr = b;
}

inline void write16(uint16_t b, volatile void *addr) {
    *(volatile uint16_t*) addr = b;
}

inline void write32(uint32_t b, volatile void *addr) {
    *(volatile uint32_t*) addr = b;
}

inline uint8_t read8(void* ptr, ptrdiff_t offset) {
    return read8(((uint8_t*)ptr) + offset);
}

inline uint16_t read16(void* ptr, ptrdiff_t offset) {
    return read16(((uint8_t*)ptr) + offset);
}

inline uint32_t read32(void* ptr, ptrdiff_t offset) {
    return read32(((uint8_t*)ptr) + offset);
}

inline void write8(void* ptr, ptrdiff_t offset, uint8_t data) {
    write8(data, ((uint8_t*)ptr) + offset);
}

inline void write16(void* ptr, ptrdiff_t offset, uint16_t data) {
    write16(data, ((uint8_t*)ptr) + offset);
}

inline void write32(void* ptr, ptrdiff_t offset, uint32_t data) {
    write32(data, ((uint8_t*)ptr) + offset);
}

template<typename T>
static uint64_t read64(T* pmem, uint64_t addr) {
    const uint64_t lower = pmem->read32(addr);
    const uint64_t upper = pmem->read32(addr + 0x4);
    return lower | (static_cast<uint64_t>(upper) << 32);
}

template<typename T>
static void write64(T* pmem, uint64_t addr, uint64_t value) {
    pmem->write32(addr, bit_mask<32>(value));
    pmem->write32(addr + 0x4, value >> 32);
}

} }  // namespace a3::mmio
#endif  // A3_MMIO_H_
/* vim: set sw=4 ts=4 et tw=80 : */
