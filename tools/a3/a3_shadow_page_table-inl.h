#ifndef A3_SHADOW_PAGE_TABLE_INL_H_
#define A3_SHADOW_PAGE_TABLE_INL_H_
#include "a3_context.h"
namespace a3 {

inline struct page_entry shadow_page_table::refresh_entry(context* ctx, pmem::accessor* pmem, const struct page_entry& entry) {
    struct page_entry result(entry);
    if (entry.present) {
        if (entry.target == page_entry::TARGET_TYPE_VRAM) {
            // rewrite address
            const uint64_t g_field = result.address;
            const uint64_t g_address = g_field << 12;
            const uint64_t h_address = ctx->get_phys_address(g_address);
            const uint64_t h_field = h_address >> 12;
            result.address = h_field;
            // A3_LOG("Rewriting address 0x%" PRIx64 " to 0x%" PRIx64 "\n", g_address, h_address);
        } else if (entry.target == page_entry::TARGET_TYPE_SYSRAM || entry.target == page_entry::TARGET_TYPE_SYSRAM_NO_SNOOP) {
            // rewrite address
            const uint64_t g_field = result.address;
            const uint64_t g_address = g_field << 12;  // Guest VM System Address
            uint64_t h_address = 0;
            A3_SYNCHRONIZED(->mutex()) {
                h_address = device::instance()->xl_ctx()
            }
            // const uint64_t h_address = ctx->get_phys_address(g_address);
            const uint64_t h_field = h_address >> 12;
            result.address = h_field;
        }
    }
    return result;
}

inline page* shadow_page_table::allocate_large_page() {
    if (large_pages_pool_cursor_ == large_pages_pool_.size()) {
        page* ptr(new page(kLARGE_PAGE_COUNT * 0x8 / kPAGE_SIZE));
        large_pages_pool_.push_back(ptr);
        return ptr;
    }
    return &large_pages_pool_[large_pages_pool_cursor_++];
}

inline page* shadow_page_table::allocate_small_page() {
    if (small_pages_pool_cursor_ == small_pages_pool_.size()) {
        page* ptr = new page(kSMALL_PAGE_COUNT * 0x8 / kPAGE_SIZE);
        small_pages_pool_.push_back(ptr);
        return ptr;
    }
    return &small_pages_pool_[small_pages_pool_cursor_++];
}



}  // namespace a3
#endif  // A3_SHADOW_PAGE_TABLE_INL_H_
/* vim: set sw=4 ts=4 et tw=80 : */
