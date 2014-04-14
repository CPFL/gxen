/*
 * A3 Device
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
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <sched.h>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/pool/detail/singleton.hpp>
#include <pciaccess.h>
#include "a3.h"
#include "xen.h"
#include "device.h"
#include "vram.h"
#include "mmio.h"
#include "context.h"
#include "playlist.h"
#include "registers.h"
#include "device_bar1.h"
#include "device_bar3.h"
#include "bit_mask.h"
#include "ignore_unused_variable_warning.h"
#include "fifo_scheduler.h"
#include "band_scheduler.h"
#include "credit_scheduler.h"
#include "direct_scheduler.h"
#include "assertion.h"

#define NVC0_VENDOR 0x10DE
#define NVC0_DEVICE 0x6D8
#define NVC0_COMMAND 0x07
#define NVC0_REVISION 0xA3
#define PCI_COMMAND 0x04

namespace a3 {

static inline unsigned int pcidev_encode_bdf(libxl_device_pci *pcidev) {
    unsigned int value;

    value = pcidev->domain << 16;
    value |= (pcidev->bus & 0xff) << 8;
    value |= (pcidev->dev & 0x1f) << 3;
    value |= (pcidev->func & 0x7);

    return value;
}

device_t::device_t()
    : device_()
    , virts_(A3_VM_NUM, -1)
    , contexts_(A3_VM_NUM, nullptr)
    , mutex_()
    , pmem_()
    , bars_()
    , bar1_()
    , bar3_()
    , vram_()
    , playlist_()
    , scheduler_()
    , chipset_()
    , domid_(-1)
    , xl_ctx_()
    , xl_logger_()
    , xl_device_pci_()
{
    if (!(xl_logger_ = xtl_createlogger_stdiostream(stderr, XTL_PROGRESS,  0))) {
        std::exit(1);
    }

    if (libxl_ctx_alloc(&xl_ctx_, LIBXL_VERSION, 0, (xentoollog_logger*)xl_logger_)) {
        fprintf(stderr, "cannot init xl context\n");
        std::exit(1);
    }

    A3_LOG("device environment setup\n");
}

device_t::~device_t() {
    if (xl_ctx_) {
        libxl_ctx_free(xl_ctx_);
    }
    if (xl_logger_) {
        xtl_logger_destroy((xentoollog_logger*)xl_logger_);
    }
    if (initialized()) {
        pci_system_cleanup();
    }
}

// not thread safe
void device_t::initialize(const bdf& bdf) {
    struct pci_id_match nvc0_match = {
        NVC0_VENDOR,
        PCI_MATCH_ANY,
        PCI_MATCH_ANY,
        PCI_MATCH_ANY,
        0x30000,
        0xFFFF0000,
        0
    };
    int ret;

    ret = pci_system_init();
    ASSERT(!ret);
    ignore_unused_variable_warning(ret);

    struct pci_device_iterator* it = pci_id_match_iterator_create(&nvc0_match);
    ASSERT(it);

    struct pci_device* dev;
    while ((dev = pci_device_next(it)) != nullptr) {
        // search by BDF
        if (dev->bus == bdf.bus && dev->dev == bdf.dev && dev->func == bdf.func) {
            break;
        }
    }
    pci_iterator_destroy(it);

    ASSERT(dev);
    pci_device_enable(dev);
    ret = pci_device_probe(dev);
    ASSERT(!ret);

    // And enable memory and io port.
    // FIXME(Yusuke Suzuki)
    // This is very ad-hoc code.
    // We should cleanup and set precise command code in the future.
    pci_device_cfg_write_u16(dev, NVC0_COMMAND, PCI_COMMAND);
    device_ = dev;

    // init BARs
    void* addr;
    ret = pci_device_map_range(dev, dev->regions[0].base_addr, dev->regions[0].size, PCI_DEV_MAP_FLAG_WRITABLE, &addr);
    bars_[0].addr = addr;
    bars_[0].base_addr = dev->regions[0].base_addr;
    bars_[0].size = dev->regions[0].size;
    ret = pci_device_map_range(dev, dev->regions[1].base_addr, dev->regions[1].size, PCI_DEV_MAP_FLAG_WRITABLE, &addr);
    bars_[1].addr = addr;
    bars_[1].base_addr = dev->regions[1].base_addr;
    bars_[1].size = dev->regions[1].size;
    ret = pci_device_map_range(dev, dev->regions[3].base_addr, dev->regions[3].size, PCI_DEV_MAP_FLAG_WRITABLE, &addr);
    bars_[3].addr = addr;
    bars_[3].base_addr = dev->regions[3].base_addr;
    bars_[3].size = dev->regions[3].size;

    if (!initialized()) {
        pci_system_cleanup();
        return;
    }

    A3_LOG("PCI device catch\n");

    // init chipset
    chipset_.reset(new chipset_t(read(0, 0x0000, sizeof(uint32_t))));

    // init vram
    vram_.reset(new vram_manager_t(A3_HYPERVISOR_DEVICE_MEM_BASE, A3_HYPERVISOR_DEVICE_MEM_SIZE));

    // init bar1 device
    bar1_.reset(new device_bar1(bars_[1]));

    // init bar3 device
    bar3_.reset(new device_bar3(bars_[3]));

    // list assignable devices
    int num = 0;
    if (libxl_device_pci* pcidevs = libxl_device_pci_assignable_list(xl_ctx_, &num)) {
        for (int i = 0; i < num; ++i) {
            libxl_device_pci* pci = (pcidevs + i);
            A3_LOG("PCI device: %02x:%02x.%02x => %d\n", pci->bus, pci->dev, pci->func, pci->domain);
            if (pci->bus == bdf.bus && pci->dev == bdf.dev && pci->func == bdf.func) {
                xl_device_pci_ = *pci;
            }
        }
        std::free(pcidevs);
    }

    // init playlist
    if (device()->chipset()->type() == card::NVC0) {
        playlist_.reset(new nvc0_playlist_t());
    } else {
        playlist_.reset(new nve0_playlist_t());
    }

    // init pmem
    pmem_ = read(0, 0x1700, sizeof(uint32_t));

    // init scheduler

    // Direct
    // scheduler_.reset(new direct_scheduler_t());
    // FIFO
    // scheduler_.reset(new fifo_scheduler_t(boost::posix_time::microseconds(50), boost::posix_time::milliseconds(500), boost::posix_time::milliseconds(100)));
    // scheduler_.reset(new fifo_scheduler_t(boost::posix_time::microseconds(50), boost::posix_time::milliseconds(500), boost::posix_time::milliseconds(500)));
    // BAND
    // scheduler_.reset(new band_scheduler_t(boost::posix_time::microseconds(50), boost::posix_time::milliseconds(500)));
    // scheduler_.reset(new band_scheduler_t(boost::posix_time::microseconds(50), boost::posix_time::milliseconds(100)));
    // Credit
    // scheduler_.reset(new credit_scheduler_t(boost::posix_time::microseconds(50), boost::posix_time::milliseconds(500)));
    scheduler_.reset(new credit_scheduler_t(boost::posix_time::microseconds(50), boost::posix_time::milliseconds(100)));
    scheduler_->start();

    A3_LOG("NV%02X device initialized\n", chipset()->detail());
}

uint32_t device_t::acquire_virt(context* ctx) {
    mutex_t::scoped_lock lock(mutex());
    const boost::dynamic_bitset<>::size_type pos = virts_.find_first();
    if (pos != virts_.npos) {
        virts_.set(pos, 0);
        contexts_[pos] = ctx;
    }
    scheduler_->register_context(ctx);
    return pos;
}

void device_t::release_virt(uint32_t virt, context* ctx) {
    mutex_t::scoped_lock lock(mutex());
    virts_.set(virt, 1);
    scheduler_->unregister_context(ctx);
    contexts_[virt] = nullptr;
}

uint32_t device_t::read(int bar, uint32_t offset, std::size_t size) {
    switch (size) {
    case sizeof(uint8_t):
        return mmio::read8(bars_[bar].addr, offset);
    case sizeof(uint16_t):
        return mmio::read16(bars_[bar].addr, offset);
    case sizeof(uint32_t):
        return mmio::read32(bars_[bar].addr, offset);
    }
    A3_LOG("%" PRIu64 " is invalid\n", size);
    A3_UNREACHABLE();
    return 0;
}

void device_t::write(int bar, uint32_t offset, uint32_t val, std::size_t size) {
    switch (size) {
    case sizeof(uint8_t):
        mmio::write8(bars_[bar].addr, offset, val);
        return;
    case sizeof(uint16_t):
        mmio::write16(bars_[bar].addr, offset, val);
        return;
    case sizeof(uint32_t):
        mmio::write32(bars_[bar].addr, offset, val);
        return;
    }
    A3_LOG("%" PRIu64 " is invalid\n", size);
    A3_UNREACHABLE();
    return;
}

vram_t* device_t::malloc(std::size_t n) {
    ASSERT(vram_);
    return vram_->malloc(n);
}

void device_t::free(vram_t* mem) {
    ASSERT(vram_);
    vram_->free(mem);
}

bool device_t::is_active(context* ctx) {
    return registers::read32(0x400700);
}

void device_t::fire(context* ctx, const command& cmd) {
    scheduler_->enqueue(ctx, cmd);
}

void device_t::playlist_update(context* ctx, uint32_t address, uint32_t cmd) {
    A3_SYNCHRONIZED(mutex()) {
        playlist_->update(ctx, address, cmd);
    }
}

uint32_t device_t::read_pmem(uint64_t addr, std::size_t size) {
    A3_SYNCHRONIZED(mutex()) {
        const uint64_t shifted = ((addr & 0xffffff00000ULL) >> 16);
        if (shifted != pmem_) {
            // change pmem
            pmem_ = shifted;
            write(0, 0x1700, shifted, sizeof(uint32_t));
        }
        return read(0, 0x700000 + (addr & 0x000000fffffULL), size);
    }
    return 0;  // make compiler happy
}

void device_t::write_pmem(uint64_t addr, uint32_t val, std::size_t size) {
    A3_SYNCHRONIZED(mutex()) {
        const uint64_t shifted = ((addr & 0xffffff00000ULL) >> 16);
        if (shifted != pmem_) {
            // change pmem
            pmem_ = shifted;
            write(0, 0x1700, shifted, sizeof(uint32_t));
        }
        write(0, 0x700000 + (addr & 0x000000fffffULL), val, size);
    }
}

device_t* device() {
    return &boost::details::pool::singleton_default<device_t>::instance();
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
