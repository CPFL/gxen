/*
 * A3 shadow page table
 *
 * Copyright (c) 2012-2013 Yusuke Suzuki
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdint.h>
#include <array>
#include <tuple>
#include <boost/make_shared.hpp>
#include "a3_inttypes.h"
#include "a3_bit_mask.h"
#include "a3_shadow_page_table.h"
#include "a3_pmem.h"
#include "a3_mmio.h"
#include "a3_page.h"
#include "a3_context.h"
#include "a3_bench.h"
#include "a3_device_bar3.h"
namespace a3 {

shadow_page_table::shadow_page_table(uint32_t channel_id)
    : size_(0)
    , channel_id_(channel_id)
    , phys_()
    , large_pages_pool_()
    , small_pages_pool_()
    , large_pages_pool_cursor_()
    , small_pages_pool_cursor_()
    , top_(8192)
    , spare_(8192)
{
    top_.clear();
    spare_.clear();
}

void shadow_page_table::set_low_size(uint32_t value) {
    low_size_ = value;
}

void shadow_page_table::set_high_size(uint32_t value) {
    high_size_ = value;
}

void shadow_page_table::allocate_shadow_address() {
    if (!phys()) {
        phys_.reset(new page(0x10));
        phys_->clear();
    }
}

bool shadow_page_table::refresh(context* ctx, uint64_t page_directory_address, uint64_t page_limit) {
    // allocate directories
    allocate_shadow_address();

    page_directory_address_ = page_directory_address;

    const uint64_t vspace_size = page_limit + 1;
    size_ = vspace_size;

    bool result = false;
//     if (page_directory_size() > kMAX_PAGE_DIRECTORIES) {
//         return result;
//     }

//     const uint64_t page_directory_page_size =
//         round_up(page_directory_size() * sizeof(struct page_directory), kPAGE_SIZE) / kPAGE_SIZE;
//     if (page_directory_page_size) {
//         if (!phys() || phys()->size() < page_directory_page_size) {
//             result = true;
//             phys_.reset(new page(page_directory_page_size));
//         }
//     }

    refresh_page_directories(ctx, page_directory_address);
    return result;
}

template<typename T>
static inline T round_up(T value, T unit) {
    auto res = value % unit;
    if (res == 0) {
        return value;
    }
    return value - res + unit;
}

typedef std::vector<std::tuple<page*, uint64_t, struct page_entry>> deferred_t;

class shadow_page_table_refresher {
 public:
    shadow_page_table_refresher(pmem::accessor* pmem)
        : pmem_()
        , entries_(kSMALL_PAGE_COUNT)
        , deferred_()
    {
    }

    pmem::accessor* pmem() const { return pmem_; }
    std::vector<struct page_entry>& entries() { return entries_; }
    const std::vector<struct page_entry>& entries() const { return entries_; }
    deferred_t* deferred() { return &deferred_; }

    void process_deferred(context* ctx)
    {
        const size_t kMAX_PER = 128;
        if (!deferred_.empty()) {
            const auto larger = round_up<size_t>(deferred_.size(), kMAX_PER);
            for (size_t i = 0; i < larger; i += kMAX_PER) {
                const size_t start = i;
                const size_t end = std::min<size_t>(deferred_.size(), start + kMAX_PER);
                const size_t nr = end - start;

                std::array<uint32_t, kMAX_PER> entries = { };

                // rewrite address
                for (size_t j = 0; j < nr; ++j) {
                    const auto result = std::get<2>(deferred_[start + j]);
                    const uint32_t gfn = (uint32_t)(result.address);
                    entries[j] = gfn;
                }
                A3_SYNCHRONIZED(device::instance()->mutex()) {
                    a3_xen_gfn_to_mfn(device::instance()->xl_ctx(), ctx->domid(), nr, entries.data());
                }
                // const uint64_t h_address = ctx->get_phys_address(g_address);
                // TODO(Yusuke Suzuki): Validate host physical address in Xen side
                A3_LOG("  changing to sys addr %u\n", (unsigned)nr);
                for (size_t j = 0; j < nr; ++j) {
                    page* page = std::get<0>(deferred_[start + j]);
                    const uint64_t offset = std::get<1>(deferred_[start + j]);
                    auto result = std::get<2>(deferred_[start + j]);

                    result.address = (uint32_t)(entries[j]);
                    page->write32(pmem_, offset, result.word0);
                    page->write32(pmem_, offset + 0x4, result.word1);
                }
            }
        }
        deferred_.clear();
    }

    pmem::accessor* pmem_;
    std::vector<struct page_entry> entries_;
    deferred_t deferred_;
};

static void read_pages(context* ctx, pmem::accessor* pmem, void* dst, uint64_t src, std::size_t pages)
{
    pmem->read_pages(dst, src, pages);
#if 0
    for (std::size_t i = 0; i < pages; ++i) {
        const uint64_t target = src + i * 0x1000;
        void* to = reinterpret_cast<uint8_t*>(dst) + i * 0x1000;
        void* bar3 = device::instance()->bar3()->find_bar3_address(ctx, target);
        if (bar3) {
            /* CAUTION: Using memcpy, not mmio::memcpy. bar3 is iomem,
             * but it's not register window.
             */
            memcpy(to, bar3, 0x1000);
        } else {
            pmem->read_pages(to, target, 1);
        }
    }
#endif
}

void shadow_page_table::refresh_page_directories(context* ctx, uint64_t address) {
    pmem::accessor pmem;
    page_directory_address_ = address;
    large_pages_pool_cursor_ = 0;
    small_pages_pool_cursor_ = 0;

    // 0 check
    if (!ctx->get_virt_address(address)) {
        // FIXME(Yusuke Suzuki): Disable page table
        return;
    }

    if (!ctx->in_memory_range(address) || !ctx->in_memory_size(size())) {
        A3_LOG("page data out of range 0x%" PRIx64 " with 0x%" PRIx64 "\n", address, size());
        return;
    }

#if 0
    A3_BENCH() {
        for (uint64_t offset = 0, index = 0; offset < 0x10000; offset += 0x8, ++index) {
                // A3_BENCH_THAN(20) {
                    const struct page_directory res = page_directory::create(&pmem, page_directory_address() + offset);
                    struct page_directory result = refresh_directory(ctx, &pmem, res);
                    phys()->write32(&pmem, offset, result.word0);
                    phys()->write32(&pmem, offset + 0x4, result.word1);
                // }
        }
    }
#endif

#if 1
    A3_BENCH() {
        using std::swap;
        shadow_page_table_refresher refresher(&pmem);
        read_pages(ctx, &pmem, spare_.data(), page_directory_address(), 16);
        for (uint64_t offset = 0, index = 0; offset < 0x10000; offset += 0x8, ++index) {
                // A3_BENCH_THAN(20) {
                    // const struct page_directory res = page_directory::create(&pmem, page_directory_address() + offset);
                    const struct page_directory res = spare_[index];
                    const struct page_directory result = refresh_directory(ctx, &refresher, res);
                    if (top_[index].raw != result.raw) {
                        phys()->write32(&pmem, offset, result.word0);
                        phys()->write32(&pmem, offset + 0x4, result.word1);
                    }
                // }
        }
        refresher.process_deferred(ctx);
        std::swap(top_, spare_);
    }
#endif

#if 0
    A3_BENCH() {
        std::vector<struct page_directory> vec(8192);
        pmem.read_pages(vec.data(), page_directory_address(), 16);
    }
    // IDEAL
    A3_BENCH() {
        for (uint64_t offset = 0, index = 0; offset < 0x10000; offset += 0x8, ++index) {
                A3_BENCH_THAN(20) {
                    const struct page_directory dir = page_directory::create(&pmem, page_directory_address() + offset);
                    struct page_directory result(dir);

                    // deferred_t deferred;

                    if (dir.large_page_table_present) {
                        const uint64_t address = ctx->get_phys_address(static_cast<uint64_t>(dir.large_page_table_address) << 12);
                        // page* large_page = allocate_large_page();
                        for (uint64_t i = 0, iz = page_directory::large_size_count(dir); i < iz; ++i) {
                            const uint64_t item = 0x8 * i;
                            struct page_entry entry;
                            if (page_entry::create(&pmem, address + item, &entry)) {
                                // guest_to_host(ctx, &pmem, &deferred, large_page, item, entry);
                            } else {
                                // large_page->write32(&pmem, item, 0);
                            }
                        }
                        // const uint64_t result_address = (large_page->address() >> 12);
                        // result.large_page_table_address = result_address;
                    } else {
                        result.word0 = 0;
                    }

                    if (dir.small_page_table_present) {
                        const uint64_t address = ctx->get_phys_address(static_cast<uint64_t>(dir.small_page_table_address) << 12);
                        // page* small_page = allocate_small_page();
                        for (uint64_t i = 0, iz = kSMALL_PAGE_COUNT; i < iz; ++i) {
                            const uint64_t item = 0x8 * i;
                            struct page_entry entry;
                            if (page_entry::create(&pmem, address + item, &entry)) {
                                // guest_to_host(ctx, &pmem, &deferred, small_page, item, entry);
                            } else {
                                // small_page->write32(&pmem, item, 0);
                            }
                        }
                        // const uint64_t result_address = (small_page->address() >> 12);
                        // result.small_page_table_address = result_address;
                    } else {
                        result.word1 = 0;
                    }

                    // process_deferred(ctx, &pmem, deferred);

                    // return result;
                }
        }
    }
#endif
    A3_LOG("scan page table of channel id 0x%" PRIi32 " : pd 0x%" PRIX64 "\n", channel_id(), page_directory_address());
}

static void guest_to_host(context* ctx, pmem::accessor* pmem, deferred_t* deferred, page* page, uint64_t offset, const struct page_entry& entry) {
    struct page_entry result(entry);
    if (!entry.present) {
        page->write32(pmem, offset, result.word0);
        page->write32(pmem, offset + 0x4, result.word1);
        return;
    }

    if (entry.target == page_entry::TARGET_TYPE_VRAM) {
        // rewrite address
        const uint64_t g_field = (uint32_t)(result.address);
        const uint64_t g_address = g_field << 12;
        const uint64_t h_address = ctx->get_phys_address(g_address);
        const uint64_t h_field = h_address >> 12;
        result.address = (uint32_t)(h_field);
        if (!(ctx->get_address_shift() <= h_address && h_address < (ctx->get_address_shift() + ctx->vram_size()))) {
            // invalid address
            A3_LOG("  invalid addr 0x%" PRIx64 " to 0x%" PRIx64 "\n", g_address, h_address);
            result.present = false;
        }
        page->write32(pmem, offset, result.word0);
        page->write32(pmem, offset + 0x4, result.word1);
        return;
    }

    if (entry.target == page_entry::TARGET_TYPE_SYSRAM || entry.target == page_entry::TARGET_TYPE_SYSRAM_NO_SNOOP) {
        deferred->push_back(std::make_tuple(page, offset, entry));
        return;
    }
}

struct page_directory shadow_page_table::refresh_directory(context* ctx, shadow_page_table_refresher* refresher, const struct page_directory& dir) {
    struct page_directory result(dir);

    if (dir.large_page_table_present) {
        const uint64_t address = ctx->get_phys_address(static_cast<uint64_t>(dir.large_page_table_address) << 12);
        page* large_page = allocate_large_page();

        read_pages(ctx, refresher->pmem(), refresher->entries().data(), address, (page_directory::large_size_count(dir) * 0x8) / 0x1000);

        for (uint64_t i = 0, iz = page_directory::large_size_count(dir); i < iz; ++i) {
            const uint64_t item = 0x8 * i;
            struct page_entry entry = refresher->entries()[i];
            if (entry.present) {
                guest_to_host(ctx, refresher->pmem(), refresher->deferred(), large_page, item, entry);
            } else {
                large_page->write32(refresher->pmem(), item, 0);
            }
            // struct page_entry entry;
            // if (page_entry::create(refresher->pmem(), address + item, &entry)) {
            //     guest_to_host(ctx, refresher->pmem(), &deferred, large_page, item, entry);
            // } else {
            //     large_page->write32(refresher->pmem(), item, 0);
            // }
        }
        const uint64_t result_address = (large_page->address() >> 12);
        result.large_page_table_address = result_address;
    } else {
        result.word0 = 0;
    }

    if (dir.small_page_table_present) {
        const uint64_t address = ctx->get_phys_address(static_cast<uint64_t>(dir.small_page_table_address) << 12);
        page* small_page = allocate_small_page();

        read_pages(ctx, refresher->pmem(), refresher->entries().data(), address, (kSMALL_PAGE_COUNT * 0x8) / 0x1000);

        for (uint64_t i = 0, iz = kSMALL_PAGE_COUNT; i < iz; ++i) {
            const uint64_t item = 0x8 * i;
            struct page_entry entry = refresher->entries()[i];
            if (entry.present) {
                guest_to_host(ctx, refresher->pmem(), refresher->deferred(), small_page, item, entry);
            } else {
                small_page->write32(refresher->pmem(), item, 0);
            }
            // struct page_entry entry;
            // if (page_entry::create(refresher->pmem(), address + item, &entry)) {
            //     guest_to_host(ctx, refresher->pmem(), &deferred, small_page, item, entry);
            // } else {
            //     small_page->write32(refresher->pmem(), item, 0);
            // }
        }
        const uint64_t result_address = (small_page->address() >> 12);
        result.small_page_table_address = result_address;
    } else {
        result.word1 = 0;
    }

    return result;
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
