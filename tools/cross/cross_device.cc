/*
 * Cross Device
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
#include <cassert>
#include <iostream>
#include <unistd.h>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/pool/detail/singleton.hpp>
#include <pciaccess.h>
#include "cross.h"
#include "cross_xen.h"
#include "cross_device.h"
#include "cross_vram.h"
#include "cross_mmio.h"
#include "cross_context.h"
#include "cross_device_bar1.h"

#define NVC0_VENDOR 0x10DE
#define NVC0_DEVICE 0x6D8
#define NVC0_COMMAND 0x07
#define NVC0_REVISION 0xA3
#define PCI_COMMAND 0x04

namespace cross {

static unsigned int pcidev_encode_bdf(libxl_device_pci *pcidev) {
    unsigned int value;

    value = pcidev->domain << 16;
    value |= (pcidev->bus & 0xff) << 8;
    value |= (pcidev->dev & 0x1f) << 3;
    value |= (pcidev->func & 0x7);

    return value;
}

device::device()
    : device_()
    , virts_(2, -1)
    , mutex_handle_()
    , pramin_()
    , bars_()
    , bar1_()
    , vram_(new vram(0x4ULL << 30, 0x2ULL << 30))  // FIXME(Yusuke Suzuki): pre-defined area, 4GB - 6GB
    , xl_ctx_()
    , xl_logger_()
    , xl_device_pci_()
    , domid_(-1)
    {
    if (!(xl_logger_ = xtl_createlogger_stdiostream(stderr, XTL_PROGRESS,  0))) {
        std::exit(1);
    }

    if (libxl_ctx_alloc(&xl_ctx_, LIBXL_VERSION, 0, (xentoollog_logger*)xl_logger_)) {
        fprintf(stderr, "cannot init xl context\n");
        exit(1);
    }
}

device::~device() {
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
void device::initialize(const bdf& bdf) {
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
    assert(!ret);

    struct pci_device_iterator* it = pci_id_match_iterator_create(&nvc0_match);
    assert(it);

    struct pci_device* dev;
    while ((dev = pci_device_next(it)) != NULL) {
        // search by BDF
        if (dev->bus == bdf.bus && dev->dev == bdf.dev && dev->func == bdf.func) {
            break;
        }
    }
    pci_iterator_destroy(it);

    assert(dev);
    pci_device_enable(dev);
    ret = pci_device_probe(dev);
    assert(!ret);

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
    bars_[0].size = dev->regions[0].size;
    ret = pci_device_map_range(dev, dev->regions[1].base_addr, dev->regions[1].size, PCI_DEV_MAP_FLAG_WRITABLE, &addr);
    bars_[1].addr = addr;
    bars_[1].size = dev->regions[1].size;
    ret = pci_device_map_range(dev, dev->regions[3].base_addr, dev->regions[3].size, PCI_DEV_MAP_FLAG_WRITABLE, &addr);
    bars_[3].addr = addr;
    bars_[3].size = dev->regions[3].size;

    if (!initialized()) {
        pci_system_cleanup();
        return;
    }

    // init bar1 device
    bar1_.reset(new device_bar1());

    // list assignable devices
    int num = 0;
    if (libxl_device_pci* pcidevs = libxl_device_pci_assignable_list(xl_ctx_, &num)) {
        for (int i = 0; i < num; ++i) {
            libxl_device_pci* pci = (pcidevs + i);
            CROSS_LOG("PCI device: %02x:%02x.%02x => %d\n", pci->bus, pci->dev, pci->func, pci->domain);
            if (pci->bus == bdf.bus && pci->dev == bdf.dev && pci->func == bdf.func) {
                xl_device_pci_ = *pci;
            }
        }
        std::free(pcidevs);
    }

    CROSS_LOG("device initialized\n");
}

uint32_t device::acquire_virt() {
    mutex::scoped_lock lock(mutex_handle_);
    const boost::dynamic_bitset<>::size_type pos = virts_.find_first();
    if (pos != virts_.npos) {
        virts_.set(pos, 0);
    }
    return pos;
}

void device::release_virt(uint32_t virt) {
    mutex::scoped_lock lock(mutex_handle_);
    virts_.set(virt, 1);
}

uint32_t device::read(int bar, uint32_t offset) {
    return mmio::read32(bars_[bar].addr, offset);
}

void device::write(int bar, uint32_t offset, uint32_t val) {
    mmio::write32(bars_[bar].addr, offset, val);
}

device* device::instance() {
    return &boost::details::pool::singleton_default<device>::instance();
}

vram_memory* device::malloc(std::size_t n) {
    return vram_->malloc(n);
}

void device::free(vram_memory* mem) {
    vram_->free(mem);
}

bool device::try_acquire_gpu(context* ctx) {
    CROSS_SYNCHRONIZED(mutex_handle()) {
        // TODO(Yusuke Suzuki): check GPU doesn't work now
        if (domid_ >= 0) {
            const int rc = cross_deassign_device(xl_ctx_, domid(), pcidev_encode_bdf(&xl_device_pci_));
            if (rc < 0) {
                CROSS_FPRINTF(stderr, "xc_deassign_device failed\n");
                return false;
            }
        }
        domid_ = ctx->domid();
        const int rc = cross_assign_device(xl_ctx_, domid(), pcidev_encode_bdf(&xl_device_pci_));
        if (rc < 0) {
            CROSS_FPRINTF(stderr, "xc_assign_device failed\n");
            return false;
        }
    }
    return true;
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
