/*
 * A3 Xen API
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
#include <libxl.h>
#include "xen.h"

// This depends on libxl_internal.h
static xc_interface* libxl_ctx_xch(libxl_ctx* ctx) {
    struct temp {
        xentoollog_logger *lg;
        xc_interface *xch;
    };
    return ((struct temp*)ctx)->xch;
}

int a3_xen_add_memory_mapping(libxl_ctx* ctx, int domid, unsigned long first_gfn, unsigned long first_mfn, unsigned long nr_mfns) {
    return xc_domain_memory_mapping(libxl_ctx_xch(ctx), domid, first_gfn, first_mfn, nr_mfns, DPCI_ADD_MAPPING);
}

int a3_xen_remove_memory_mapping(libxl_ctx* ctx, int domid, unsigned long first_gfn, unsigned long first_mfn, unsigned long nr_mfns) {
    return xc_domain_memory_mapping(libxl_ctx_xch(ctx), domid, first_gfn, first_mfn, nr_mfns, DPCI_REMOVE_MAPPING);
}

void* a3_xen_map_foreign_range(libxl_ctx* ctx, int domid, int size, int prot, unsigned long mfn) {
    return xc_map_foreign_range(libxl_ctx_xch(ctx), domid, size, prot, mfn);
}

unsigned long a3_xen_gfn_to_mfn(libxl_ctx* ctx, int domid, unsigned long gfn) {
    unsigned long mfn;
    const int ret = xc_domain_gfn_to_mfn(libxl_ctx_xch(ctx), domid, gfn, &mfn);
    if (ret != 0) {
        return 0;
    }
    return mfn;
}

/* vim: set sw=4 ts=4 et tw=80 : */
