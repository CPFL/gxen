/*
 * Cross Xen API
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
#include "cross_xen.h"

// FIXME(Yusuke Suzuki): This depends on libxl_internal.h
static xc_interface* libxl_ctx_xch(libxl_ctx* ctx) {
    struct temp {
        xentoollog_logger *lg;
        xc_interface *xch;
    };
    return ((struct temp*)ctx)->xch;
}

int cross_assign_device(libxl_ctx* ctx, int domid, unsigned int encoded_bdf) {
    return xc_assign_device(libxl_ctx_xch(ctx), domid, encoded_bdf);
}

int cross_deassign_device(libxl_ctx* ctx, int domid, unsigned int encoded_bdf) {
    return xc_deassign_device(libxl_ctx_xch(ctx), domid, encoded_bdf);
}

/* vim: set sw=4 ts=4 et tw=80 : */
