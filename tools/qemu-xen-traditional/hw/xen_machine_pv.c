/*
 * QEMU Xen PV Machine
 *
 * Copyright (c) 2007 Red Hat
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
#include "sysemu.h"
#include "boards.h"
#include "xen_backend.h"
#include "qemu-xen.h"

#ifndef CONFIG_STUBDOM
#include <hw/xen_blktap.h>
#endif

uint32_t xen_domid;
enum xen_mode xen_mode = XEN_EMULATE;

static void xen_init_pv(ram_addr_t ram_size, int vga_ram_size,
			const char *boot_device,
			const char *kernel_filename,
			const char *kernel_cmdline,
			const char *initrd_filename,
			const char *cpu_model,
			const char *direct_pci)
{
    CPUState *env;
    uint32_t domid_target;

#if !defined(CONFIG_STUBDOM) && !defined(__NetBSD__)
    /* Initialize tapdisk client */
    init_blktap();
#endif

    /* Initialize a dummy CPU */
    if (cpu_model == NULL) {
#ifdef TARGET_X86_64
        cpu_model = "qemu64";
#else
        cpu_model = "qemu32";
#endif
    }
    env = cpu_init(cpu_model);
    env->halted = 1;

    /* Initialize backend core & drivers */
    if (xen_be_init() != 0) {
        fprintf(stderr, "%s: xen backend core setup failed\n", __FUNCTION__);
        exit(1);
    }
    xen_be_register("console", &xen_console_ops);
    xen_be_register("vkbd", &xen_kbdmouse_ops);
    xen_be_register("vfb", &xen_framebuffer_ops);
#ifndef CONFIG_STUBDOM
    xen_be_register("qdisk", &xen_blkdev_ops);
    domid_target = xenstore_read_target();
    if (domid_target)
        xenstore_scan("qdisk", domid_target, &xen_blkdev_ops);
#endif

    /* setup framebuffer */
    xen_init_display(xen_domid);
}

QEMUMachine xenpv_machine = {
    .name = "xenpv",
    .desc = "Xen Para-virtualized PC",
    .init = xen_init_pv,
    .ram_require = BIOS_SIZE | RAMSIZE_FIXED,
    .max_cpus = 1,
    .nodisk_ok = 1,
};
