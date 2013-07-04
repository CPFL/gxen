/*
 * A3 Context BAR4
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
#include "a3.h"
#include "a3_context.h"
#include "a3_pmem.h"
#include "a3_software_page_table.h"
#include "a3_channel.h"
#include "a3_bit_mask.h"
#include "a3_barrier.h"
#include "a3_pv_slot.h"
#include "a3_page.h"
#include "a3_pv_page.h"
#include "a3_device_bar1.h"
#include "a3_device_bar3.h"
namespace a3 {


static uint64_t round_up(uint64_t x, uint64_t y) {
    return (((x) + (y - 1)) & ~(y - 1));
}

static inline uint64_t guest_to_host_pte(context* ctx, uint64_t guest) {
    struct page_entry result;
    result.raw = guest;
    if (result.target == page_entry::TARGET_TYPE_VRAM) {
        // rewrite address
        const uint64_t g_field = result.address;
        const uint64_t g_address = g_field << 12;
        const uint64_t h_address = ctx->get_phys_address(g_address);
        const uint64_t h_field = h_address >> 12;
        result.address = h_field;
    }
    return result.raw;
}

int context::a3_call(const command& cmd, slot_t* slot) {
    A3_LOG("A3 call %d\n", static_cast<int>(slot->u8[0]));
    switch (slot->u8[0]) {
    case NOUVEAU_PV_OP_SET_PGD: {
            pv_page* pgd = lookup_by_pv_id(slot->u32[1]);
            if (!pgd) {
                A3_LOG("INVALID...\n");
                return -EINVAL;
            }
            const int cid = static_cast<int32_t>(slot->u32[2]);
            A3_LOG("cid %d\n", cid);
            if (cid < 0) {
                // BAR1 OR BAR3
                if (cid == -3) {
                    pv_bar3_pgd_ = pgd;
                } else {
                    pv_bar1_pgd_ = pgd;
                }
            } else {
                pgds_[cid] = pgd;
            }
        }
        return 0;

    case NOUVEAU_PV_OP_MAP_PGT: {
            pv_page* pgd = lookup_by_pv_id(slot->u32[1]);
            if (!pgd) {
                A3_LOG("INVALID...\n");
                return -EINVAL;
            }

            pv_page* pgt0 = NULL;
            if (slot->u32[2]) {
                pgt0 = lookup_by_pv_id(slot->u32[2]);
                if (!pgt0) {
                    A3_LOG("INVALID...\n");
                    return -EINVAL;
                }
            }

            pv_page* pgt1 = NULL;
            if (slot->u32[3]) {
                pgt1 = lookup_by_pv_id(slot->u32[3]);
                if (!pgt1) {
                    A3_LOG("INVALID...\n");
                    return -EINVAL;
                }
            }

            if (pgd == pv_bar1_pgd_) {
                // CAUTION: inverted (pgt1 & pgt0)
                if (pv_bar1_large_pgt_ != pgt1) {
                    pv_bar1_large_pgt_ = pgt1;
                    // TODO(Yusuke Suzuki)
                    // set xen shadow for PV
                    bar1_channel()->table()->pv_scan(this, 0, true, pgt1);
                    A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
                        device::instance()->bar1()->pv_scan(this);
                    }
                }
                if (pv_bar1_small_pgt_ != pgt0) {
                    pv_bar1_small_pgt_ = pgt0;
                    bar1_channel()->table()->pv_scan(this, 0, false, pgt0);
                    A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
                        device::instance()->bar1()->pv_scan(this);
                    }
                }
                return 0;
            } else if (pgd == pv_bar3_pgd_) {
                return 0;
            }

            const uint64_t index = slot->u32[4];
            if ((0x8 * (index + 1)) > pgd->size()) {
                A3_LOG("INVALID...\n");
                return -ERANGE;
            }
            // CAUTION: inverted (pgt1 & pgt0)
            pgd->write32(0x8 * index + 0x0, pgt1 ? (0x00000001 | (pgt1->address() >> 8)) : 0);
            pgd->write32(0x8 * index + 0x4, pgt0 ? (0x00000001 | (pgt0->address() >> 8)) : 0);
        }
        return 0;

    case NOUVEAU_PV_OP_MAP: {
            pv_page* pgt = lookup_by_pv_id(slot->u32[1]);
            if (!pgt) {
                A3_LOG("INVALID...\n");
                return -EINVAL;
            }

            // TODO(Yusuke Suzuki): validation
            const uint64_t index = slot->u32[2];
            const uint64_t host = guest_to_host_pte(this, slot->u64[2]);
            if (pgt == pv_bar3_pgt_) {
                bar3_channel()->table()->pv_reflect_entry(this, 0, false, index, slot->u64[2]);
                A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
                    device::instance()->bar3()->pv_reflect(this, index, host);
                }
                return 0;
            } else if (pgt == pv_bar1_large_pgt_) {
                bar1_channel()->table()->pv_reflect_entry(this, 0, true, index, slot->u64[2]);
                // TODO(Yusuke Suzuki) sync
//                 A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
//                     device::instance()->bar1()->pv_reflect_entry(this, true, index, slot->u64[2]);
//                 }
                return 0;
            } else if (pgt == pv_bar1_small_pgt_) {
                bar1_channel()->table()->pv_reflect_entry(this, 0, false, index, slot->u64[2]);
                A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
                    device::instance()->bar1()->pv_reflect_entry(this, false, index, host);
                }
                return 0;
            }

            if ((0x8 * (index + 1)) > pgt->size()) {
                A3_LOG("INVALID...\n");
                return -ERANGE;
            }
            pgt->write32(0x8 * index + 0x0, lower32(host));
            pgt->write32(0x8 * index + 0x4, upper32(host));
        }
        return 0;

    case NOUVEAU_PV_OP_VM_FLUSH: {
            page* pgd = lookup_by_pv_id(slot->u32[1]);
            if (!pgd) {
                A3_LOG("INVALID...\n");
                return -EINVAL;
            }

            if (pgd == pv_bar1_pgd_) {
                A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
                    device::instance()->bar1()->flush();
                }
                return 0;
            }

            if (pgd == pv_bar3_pgd_) {
                A3_LOG("BAR3 flush\n");
                A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
                    device::instance()->bar3()->flush();
                    // pgd = device::instance()->bar3()->directory();
                }
                return 0;
            }

            const uint32_t engine = slot->u32[2];
            A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
                registers::accessor regs;
                if (!regs.wait_ne(0x100c80, 0x00ff0000, 0x00000000)) {
                    A3_LOG("INVALID...\n");
                    return -EINVAL;
                }
                regs.write32(0x100cb8, pgd->address() >> 8);
                regs.write32(0x100cbc, 0x80000000 | engine);
                if (!regs.wait_eq(0x100c80, 0x00008000, 0x00008000)) {
                    A3_LOG("INVALID...\n");
                    return -EINVAL;
                }
            }
        }
        return 0;

    case NOUVEAU_PV_OP_MEM_ALLOC: {
            const uint32_t size = slot->u32[1];
            pv_page* p(new pv_page(round_up(size, kPAGE_SIZE) / kPAGE_SIZE));
            p->clear();
            // address is 40bits => shift 12 & get 28bit page frame number
            uint32_t id = p->address() >> 12;
            slot->u32[1] = id;
            allocated_.insert(id, p);
        }
        return 0;

    case NOUVEAU_PV_OP_MEM_FREE: {
            allocated_.erase(slot->u32[1]);
        }
        return 0;

    case NOUVEAU_PV_OP_BAR3_PGT: {
            pv_page* pgt = lookup_by_pv_id(slot->u32[1]);
            if (!pgt) {
                A3_LOG("INVALID...\n");
                return -EINVAL;
            }
            pv_bar3_pgt_ = pgt;
        }
        return 0;

    default:
        return -EINVAL;
    }
    return 0;
}

void context::write_bar4(const command& cmd) {
    switch (cmd.offset) {
    case 0x000000:
        break;

    case 0x000004:
        pv32(cmd.offset) = cmd.value;
        break;

    case 0x000008:
        pv32(cmd.offset) = cmd.value;
        break;

    case 0x00000c: {  // A3 call
            uint32_t pos = cmd.value;

            // validate slot
            if (pos >= NOUVEAU_PV_SLOT_NUM) {
                buffer()->value = -EINVAL;
                break;
            }

            if (!guest_) {
                buffer()->value = -EINVAL;
                break;
            }

            // lookup slot
            slot_t* slot = reinterpret_cast<slot_t*>(guest_ + NOUVEAU_PV_SLOT_SIZE * pos);
            // result code
            slot->u32[0] = a3_call(cmd, slot);
        }
        break;
    }
}

void context::read_bar4(const command& cmd) {
    switch (cmd.offset) {
    case 0x000000: {
            const uint64_t lower = pv32(0x4);
            const uint64_t upper = pv32(0x8);
            const uint64_t gp = lower | (upper << 32);
            A3_LOG("Guest physical call data address %" PRIx64 "\n", gp);
            if (guest_) {
                munmap(guest_, A3_GUEST_DATA_SIZE);
                guest_ = NULL;
            }
            A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
                guest_ = reinterpret_cast<uint8_t*>(a3_xen_map_foreign_range(device::instance()->xl_ctx(), domid(), A3_GUEST_DATA_SIZE, PROT_READ | PROT_WRITE, gp >> 12));
            }

            if (!guest_) {
                buffer()->value = -EINVAL;
                break;
            }

            A3_LOG("Guest physical call data cookie %" PRIx32 "\n", reinterpret_cast<uint32_t*>(guest_)[0]);
            buffer()->value = 0x0;
        }
        break;
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
