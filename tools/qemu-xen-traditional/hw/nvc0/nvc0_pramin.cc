/*
 * NVIDIA NVC0 PRAMIN functions
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
#include "nvc0_pramin.h"
#include "nvc0_vm.h"
#include "nvc0_mmio.h"
namespace nvc0 {

uint32_t pramin_read32(nvc0_state_t* state, uint64_t addr) {
    pramin_accessor accessor(state);
    return accessor.read32(addr);
}

void pramin_write32(nvc0_state_t* state, uint64_t addr, uint32_t val) {
    pramin_accessor accessor(state);
    accessor.write32(addr, val);
}

pramin_accessor::pramin_accessor(nvc0_state_t* state)
    : state_(state)
    , old_(state->vm_engine.pramin) {
}

pramin_accessor::~pramin_accessor() {
    if (state_->vm_engine.pramin != old_) {
        state_->vm_engine.pramin = old_;
        nvc0_mmio_write32(state_->bar[0].real, 0x1700, old_);
    }
}

uint32_t pramin_accessor::read32(uint64_t addr) {
    change_current(addr);
    return nvc0_vm_pramin_read(state_, (addr & 0xFFFF));
}

void pramin_accessor::write32(uint64_t addr, uint32_t val) {
    change_current(addr);
    nvc0_vm_pramin_write(state_, (addr & 0xFFFF), val);
}

void pramin_accessor::change_current(uint64_t addr) {
    const uint64_t shifted = (addr >> 16);
    if (state_->vm_engine.pramin != shifted) {
        state_->vm_engine.pramin = shifted;
        nvc0_mmio_write32(state_->bar[0].real, 0x1700, shifted);
    }
}

}  // namespace nvc0
/* vim: set sw=4 ts=4 et tw=80 : */
