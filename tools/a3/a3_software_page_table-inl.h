#ifndef A3_SOFTWARE_PAGE_TABLE_INL_H_
#define A3_SOFTWARE_PAGE_TABLE_INL_H_
#include "a3_context.h"
namespace a3 {

inline void software_page_entry::refresh(context* ctx, pmem::accessor* pmem, const struct page_entry& entry) {
    struct page_entry result(entry);
    assert(entry.present);
    if (entry.target == page_entry::TARGET_TYPE_VRAM) {
        // rewrite address
        const uint64_t g_field = result.address;
        const uint64_t g_address = g_field << 12;
        const uint64_t h_address = ctx->get_phys_address(g_address);
        const uint64_t h_field = h_address >> 12;
        result.address = h_field;
        // A3_LOG("Rewriting address 0x%" PRIx64 " to 0x%" PRIx64 "\n", g_address, h_address);
    }
    phys_ = result;
}

}  // namespace a3
#endif  // A3_SOFTWARE_PAGE_TABLE_INL_H_
/* vim: set sw=4 ts=4 et tw=80 : */
