/*
 * SA-1110-based Sharp Zaurus SL-5500 platform.
 *
 * Copyright (C) 2011 Dmitry Eremin-Solenikov
 *
 * This code is licensed under GNU GPL v2.
 */
#include "hw.h"
#include "sysbus.h"
#include "boards.h"
#include "devices.h"
#include "strongarm.h"
#include "arm-misc.h"
#include "flash.h"
#include "blockdev.h"
#include "exec-memory.h"

static struct arm_boot_info collie_binfo = {
    .loader_start = SA_SDCS0,
    .ram_size = 0x20000000,
};

static void collie_init(ram_addr_t ram_size,
                const char *boot_device,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    StrongARMState *s;
    DriveInfo *dinfo;
    MemoryRegion *sysmem = get_system_memory();

    if (!cpu_model) {
        cpu_model = "sa1110";
    }

    s = sa1110_init(sysmem, collie_binfo.ram_size, cpu_model);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    pflash_cfi01_register(SA_CS0, NULL, "collie.fl1", 0x02000000,
                    dinfo ? dinfo->bdrv : NULL, (64 * 1024),
                    512, 4, 0x00, 0x00, 0x00, 0x00, 0);

    dinfo = drive_get(IF_PFLASH, 0, 1);
    pflash_cfi01_register(SA_CS1, NULL, "collie.fl2", 0x02000000,
                    dinfo ? dinfo->bdrv : NULL, (64 * 1024),
                    512, 4, 0x00, 0x00, 0x00, 0x00, 0);

    sysbus_create_simple("scoop", 0x40800000, NULL);

    collie_binfo.kernel_filename = kernel_filename;
    collie_binfo.kernel_cmdline = kernel_cmdline;
    collie_binfo.initrd_filename = initrd_filename;
    collie_binfo.board_id = 0x208;
    arm_load_kernel(s->env, &collie_binfo);
}

static QEMUMachine collie_machine = {
    .name = "collie",
    .desc = "Collie PDA (SA-1110)",
    .init = collie_init,
};

static void collie_machine_init(void)
{
    qemu_register_machine(&collie_machine);
}

machine_init(collie_machine_init)
