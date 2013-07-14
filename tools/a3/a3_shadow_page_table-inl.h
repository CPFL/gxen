#ifndef A3_SHADOW_PAGE_TABLE_INL_H_
#define A3_SHADOW_PAGE_TABLE_INL_H_
#include "a3_context.h"
namespace a3 {

inline struct page_entry shadow_page_table::refresh_entry(context* ctx, pmem::accessor* pmem, const struct page_entry& entry) {
    return ctx->guest_to_host(entry);
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
