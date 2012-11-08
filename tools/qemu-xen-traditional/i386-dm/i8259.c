/* Xen 8259 stub for interrupt controller emulation
 * 
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2005      Intel corperation
 * Copyright (c) 2008      Citrix / Xensource
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

#include "hw.h"
#include "pc.h"

#include <xen/hvm/ioreq.h>
#include <stdio.h>

static void i8259_set_irq(void *opaque, int irq, int level) {
    xc_hvm_set_isa_irq_level(xc_handle, domid, irq, level);
}

qemu_irq *i8259_init(qemu_irq parent_irq)
     /* We ignore the parent irq entirely.  The parent irq is attached to
      * hw/pc.c:pic_irq_request
      */
{
    return qemu_allocate_irqs(i8259_set_irq, 0, 16);
}

#if 0
void irq_info(void)
{
    term_printf("irq statistics not supported with Xen.\n");
}

void pic_info(void)
{
    term_printf("pic_info not supported with Xen .\n");
}
#endif
