#ifndef HW_NVC0_NVC0_VM_H_
#define HW_NVC0_NVC0_VM_H_
#include <stdint.h>
#include "nvc0.h"
#include "nvc0_inttypes.h"
#include "nvc0_vm.h"
#include "nvc0_mmio.h"
#include "nvc0_context.h"
namespace nvc0 {

template<std::size_t N>
uint32_t vm_bar1_read(nvc0_state_t* state, target_phys_addr_t offset) {
    context* ctx = context::extract(state);
    const cross::command cmd = {
        cross::command::TYPE_READ,
        0,
        offset,
        { cross::command::BAR1, N }
    };
    return ctx->send(cmd).value;
}

template<std::size_t N>
void vm_bar1_write(nvc0_state_t* state, target_phys_addr_t offset, uint32_t value) {
    context* ctx = context::extract(state);
    const cross::command cmd = {
        cross::command::TYPE_WRITE,
        value,
        offset,
        { cross::command::BAR1, N }
    };
    ctx->send(cmd);
}

template<std::size_t N>
uint32_t vm_bar3_read(nvc0_state_t* state, target_phys_addr_t offset) {
    context* ctx = context::extract(state);
    const cross::command cmd = {
        cross::command::TYPE_READ,
        0,
        offset,
        { cross::command::BAR3, N }
    };
    return ctx->send(cmd).value;
}

template<std::size_t N>
void vm_bar3_write(nvc0_state_t* state, target_phys_addr_t offset, uint32_t value) {
    context* ctx = context::extract(state);
    const cross::command cmd = {
        cross::command::TYPE_WRITE,
        value,
        offset,
        { cross::command::BAR3, N }
    };
    ctx->send(cmd);
}

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_VM_H_
/* vim: set sw=4 ts=4 et tw=80 : */
