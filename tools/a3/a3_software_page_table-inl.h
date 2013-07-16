#ifndef A3_SOFTWARE_PAGE_TABLE_INL_H_
#define A3_SOFTWARE_PAGE_TABLE_INL_H_
#include "a3_context.h"
namespace a3 {

inline void software_page_entry::refresh(context* ctx, const struct page_entry& entry) {
    phys_ = ctx->guest_to_host(entry);
}

inline void software_page_entry::assign(const struct page_entry& entry) {
    phys_ = entry;
}

}  // namespace a3
#endif  // A3_SOFTWARE_PAGE_TABLE_INL_H_
/* vim: set sw=4 ts=4 et tw=80 : */
