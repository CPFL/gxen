/*
 * Qemu PowerPC MPC8544DS board emualtion
 *
 * Copyright (C) 2009 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Yu Liu,     <yu.liu@freescale.com>
 *
 * This file is derived from hw/ppc440_bamboo.c,
 * the copyright for that material belongs to the original owners.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of  the GNU General  Public License as published by
 * the Free Software Foundation;  either version 2 of the  License, or
 * (at your option) any later version.
 */

#include "config.h"
#include "qemu-common.h"
#include "net.h"
#include "hw.h"
#include "pc.h"
#include "pci.h"
#include "boards.h"
#include "sysemu.h"
#include "kvm.h"
#include "kvm_ppc.h"
#include "device_tree.h"
#include "openpic.h"
#include "ppc.h"
#include "loader.h"
#include "elf.h"
#include "sysbus.h"
#include "exec-memory.h"

#define BINARY_DEVICE_TREE_FILE    "mpc8544ds.dtb"
#define UIMAGE_LOAD_BASE           0
#define DTC_LOAD_PAD               0x500000
#define DTC_PAD_MASK               0xFFFFF
#define INITRD_LOAD_PAD            0x2000000
#define INITRD_PAD_MASK            0xFFFFFF

#define RAM_SIZES_ALIGN            (64UL << 20)

#define MPC8544_CCSRBAR_BASE       0xE0000000
#define MPC8544_MPIC_REGS_BASE     (MPC8544_CCSRBAR_BASE + 0x40000)
#define MPC8544_SERIAL0_REGS_BASE  (MPC8544_CCSRBAR_BASE + 0x4500)
#define MPC8544_SERIAL1_REGS_BASE  (MPC8544_CCSRBAR_BASE + 0x4600)
#define MPC8544_PCI_REGS_BASE      (MPC8544_CCSRBAR_BASE + 0x8000)
#define MPC8544_PCI_REGS_SIZE      0x1000
#define MPC8544_PCI_IO             0xE1000000
#define MPC8544_PCI_IOLEN          0x10000
#define MPC8544_UTIL_BASE          (MPC8544_CCSRBAR_BASE + 0xe0000)
#define MPC8544_SPIN_BASE          0xEF000000

struct boot_info
{
    uint32_t dt_base;
    uint32_t entry;
};

static int mpc8544_load_device_tree(CPUState *env,
                                    target_phys_addr_t addr,
                                    uint32_t ramsize,
                                    target_phys_addr_t initrd_base,
                                    target_phys_addr_t initrd_size,
                                    const char *kernel_cmdline)
{
    int ret = -1;
#ifdef CONFIG_FDT
    uint32_t mem_reg_property[] = {0, cpu_to_be32(ramsize)};
    char *filename;
    int fdt_size;
    void *fdt;
    uint8_t hypercall[16];
    uint32_t clock_freq = 400000000;
    uint32_t tb_freq = 400000000;
    int i;

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, BINARY_DEVICE_TREE_FILE);
    if (!filename) {
        goto out;
    }
    fdt = load_device_tree(filename, &fdt_size);
    g_free(filename);
    if (fdt == NULL) {
        goto out;
    }

    /* Manipulate device tree in memory. */
    ret = qemu_devtree_setprop(fdt, "/memory", "reg", mem_reg_property,
                               sizeof(mem_reg_property));
    if (ret < 0)
        fprintf(stderr, "couldn't set /memory/reg\n");

    if (initrd_size) {
        ret = qemu_devtree_setprop_cell(fdt, "/chosen", "linux,initrd-start",
                                        initrd_base);
        if (ret < 0) {
            fprintf(stderr, "couldn't set /chosen/linux,initrd-start\n");
        }

        ret = qemu_devtree_setprop_cell(fdt, "/chosen", "linux,initrd-end",
                                        (initrd_base + initrd_size));
        if (ret < 0) {
            fprintf(stderr, "couldn't set /chosen/linux,initrd-end\n");
        }
    }

    ret = qemu_devtree_setprop_string(fdt, "/chosen", "bootargs",
                                      kernel_cmdline);
    if (ret < 0)
        fprintf(stderr, "couldn't set /chosen/bootargs\n");

    if (kvm_enabled()) {
        /* Read out host's frequencies */
        clock_freq = kvmppc_get_clockfreq();
        tb_freq = kvmppc_get_tbfreq();

        /* indicate KVM hypercall interface */
        qemu_devtree_setprop_string(fdt, "/hypervisor", "compatible",
                                    "linux,kvm");
        kvmppc_get_hypercall(env, hypercall, sizeof(hypercall));
        qemu_devtree_setprop(fdt, "/hypervisor", "hcall-instructions",
                             hypercall, sizeof(hypercall));
    }

    /* We need to generate the cpu nodes in reverse order, so Linux can pick
       the first node as boot node and be happy */
    for (i = smp_cpus - 1; i >= 0; i--) {
        char cpu_name[128];
        uint64_t cpu_release_addr = cpu_to_be64(MPC8544_SPIN_BASE + (i * 0x20));

        for (env = first_cpu; env != NULL; env = env->next_cpu) {
            if (env->cpu_index == i) {
                break;
            }
        }

        if (!env) {
            continue;
        }

        snprintf(cpu_name, sizeof(cpu_name), "/cpus/PowerPC,8544@%x", env->cpu_index);
        qemu_devtree_add_subnode(fdt, cpu_name);
        qemu_devtree_setprop_cell(fdt, cpu_name, "clock-frequency", clock_freq);
        qemu_devtree_setprop_cell(fdt, cpu_name, "timebase-frequency", tb_freq);
        qemu_devtree_setprop_string(fdt, cpu_name, "device_type", "cpu");
        qemu_devtree_setprop_cell(fdt, cpu_name, "reg", env->cpu_index);
        qemu_devtree_setprop_cell(fdt, cpu_name, "d-cache-line-size",
                                  env->dcache_line_size);
        qemu_devtree_setprop_cell(fdt, cpu_name, "i-cache-line-size",
                                  env->icache_line_size);
        qemu_devtree_setprop_cell(fdt, cpu_name, "d-cache-size", 0x8000);
        qemu_devtree_setprop_cell(fdt, cpu_name, "i-cache-size", 0x8000);
        qemu_devtree_setprop_cell(fdt, cpu_name, "bus-frequency", 0);
        if (env->cpu_index) {
            qemu_devtree_setprop_string(fdt, cpu_name, "status", "disabled");
            qemu_devtree_setprop_string(fdt, cpu_name, "enable-method", "spin-table");
            qemu_devtree_setprop(fdt, cpu_name, "cpu-release-addr",
                                 &cpu_release_addr, sizeof(cpu_release_addr));
        } else {
            qemu_devtree_setprop_string(fdt, cpu_name, "status", "okay");
        }
    }

    ret = rom_add_blob_fixed(BINARY_DEVICE_TREE_FILE, fdt, fdt_size, addr);
    g_free(fdt);

out:
#endif

    return ret;
}

/* Create -kernel TLB entries for BookE, linearly spanning 256MB.  */
static inline target_phys_addr_t booke206_page_size_to_tlb(uint64_t size)
{
    return ffs(size >> 10) - 1;
}

static void mmubooke_create_initial_mapping(CPUState *env,
                                     target_ulong va,
                                     target_phys_addr_t pa)
{
    ppcmas_tlb_t *tlb = booke206_get_tlbm(env, 1, 0, 0);
    target_phys_addr_t size;

    size = (booke206_page_size_to_tlb(256 * 1024 * 1024) << MAS1_TSIZE_SHIFT);
    tlb->mas1 = MAS1_VALID | size;
    tlb->mas2 = va & TARGET_PAGE_MASK;
    tlb->mas7_3 = pa & TARGET_PAGE_MASK;
    tlb->mas7_3 |= MAS3_UR | MAS3_UW | MAS3_UX | MAS3_SR | MAS3_SW | MAS3_SX;

    env->tlb_dirty = true;
}

static void mpc8544ds_cpu_reset_sec(void *opaque)
{
    CPUState *env = opaque;

    cpu_reset(env);

    /* Secondary CPU starts in halted state for now. Needs to change when
       implementing non-kernel boot. */
    env->halted = 1;
    env->exception_index = EXCP_HLT;
}

static void mpc8544ds_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    struct boot_info *bi = env->load_info;

    cpu_reset(env);

    /* Set initial guest state. */
    env->halted = 0;
    env->gpr[1] = (16<<20) - 8;
    env->gpr[3] = bi->dt_base;
    env->nip = bi->entry;
    mmubooke_create_initial_mapping(env, 0, 0);
}

static void mpc8544ds_init(ram_addr_t ram_size,
                         const char *boot_device,
                         const char *kernel_filename,
                         const char *kernel_cmdline,
                         const char *initrd_filename,
                         const char *cpu_model)
{
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    PCIBus *pci_bus;
    CPUState *env = NULL;
    uint64_t elf_entry;
    uint64_t elf_lowaddr;
    target_phys_addr_t entry=0;
    target_phys_addr_t loadaddr=UIMAGE_LOAD_BASE;
    target_long kernel_size=0;
    target_ulong dt_base = 0;
    target_ulong initrd_base = 0;
    target_long initrd_size=0;
    int i=0;
    unsigned int pci_irq_nrs[4] = {1, 2, 3, 4};
    qemu_irq **irqs, *mpic;
    DeviceState *dev;
    CPUState *firstenv = NULL;

    /* Setup CPUs */
    if (cpu_model == NULL) {
        cpu_model = "e500v2_v30";
    }

    irqs = g_malloc0(smp_cpus * sizeof(qemu_irq *));
    irqs[0] = g_malloc0(smp_cpus * sizeof(qemu_irq) * OPENPIC_OUTPUT_NB);
    for (i = 0; i < smp_cpus; i++) {
        qemu_irq *input;
        env = cpu_ppc_init(cpu_model);
        if (!env) {
            fprintf(stderr, "Unable to initialize CPU!\n");
            exit(1);
        }

        if (!firstenv) {
            firstenv = env;
        }

        irqs[i] = irqs[0] + (i * OPENPIC_OUTPUT_NB);
        input = (qemu_irq *)env->irq_inputs;
        irqs[i][OPENPIC_OUTPUT_INT] = input[PPCE500_INPUT_INT];
        irqs[i][OPENPIC_OUTPUT_CINT] = input[PPCE500_INPUT_CINT];
        env->spr[SPR_BOOKE_PIR] = env->cpu_index = i;

        ppc_booke_timers_init(env, 400000000, PPC_TIMER_E500);

        /* Register reset handler */
        if (!i) {
            /* Primary CPU */
            struct boot_info *boot_info;
            boot_info = g_malloc0(sizeof(struct boot_info));
            qemu_register_reset(mpc8544ds_cpu_reset, env);
            env->load_info = boot_info;
        } else {
            /* Secondary CPUs */
            qemu_register_reset(mpc8544ds_cpu_reset_sec, env);
        }
    }

    env = firstenv;

    /* Fixup Memory size on a alignment boundary */
    ram_size &= ~(RAM_SIZES_ALIGN - 1);

    /* Register Memory */
    memory_region_init_ram(ram, NULL, "mpc8544ds.ram", ram_size);
    memory_region_add_subregion(address_space_mem, 0, ram);

    /* MPIC */
    mpic = mpic_init(address_space_mem, MPC8544_MPIC_REGS_BASE,
                     smp_cpus, irqs, NULL);

    if (!mpic) {
        cpu_abort(env, "MPIC failed to initialize\n");
    }

    /* Serial */
    if (serial_hds[0]) {
        serial_mm_init(address_space_mem, MPC8544_SERIAL0_REGS_BASE,
                       0, mpic[12+26], 399193,
                       serial_hds[0], DEVICE_BIG_ENDIAN);
    }

    if (serial_hds[1]) {
        serial_mm_init(address_space_mem, MPC8544_SERIAL1_REGS_BASE,
                       0, mpic[12+26], 399193,
                       serial_hds[0], DEVICE_BIG_ENDIAN);
    }

    /* General Utility device */
    sysbus_create_simple("mpc8544-guts", MPC8544_UTIL_BASE, NULL);

    /* PCI */
    dev = sysbus_create_varargs("e500-pcihost", MPC8544_PCI_REGS_BASE,
                                mpic[pci_irq_nrs[0]], mpic[pci_irq_nrs[1]],
                                mpic[pci_irq_nrs[2]], mpic[pci_irq_nrs[3]],
                                NULL);
    pci_bus = (PCIBus *)qdev_get_child_bus(dev, "pci.0");
    if (!pci_bus)
        printf("couldn't create PCI controller!\n");

    isa_mmio_init(MPC8544_PCI_IO, MPC8544_PCI_IOLEN);

    if (pci_bus) {
        /* Register network interfaces. */
        for (i = 0; i < nb_nics; i++) {
            pci_nic_init_nofail(&nd_table[i], "virtio", NULL);
        }
    }

    /* Register spinning region */
    sysbus_create_simple("e500-spin", MPC8544_SPIN_BASE, NULL);

    /* Load kernel. */
    if (kernel_filename) {
        kernel_size = load_uimage(kernel_filename, &entry, &loadaddr, NULL);
        if (kernel_size < 0) {
            kernel_size = load_elf(kernel_filename, NULL, NULL, &elf_entry,
                                   &elf_lowaddr, NULL, 1, ELF_MACHINE, 0);
            entry = elf_entry;
            loadaddr = elf_lowaddr;
        }
        /* XXX try again as binary */
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
    }

    /* Load initrd. */
    if (initrd_filename) {
        initrd_base = (kernel_size + INITRD_LOAD_PAD) & ~INITRD_PAD_MASK;
        initrd_size = load_image_targphys(initrd_filename, initrd_base,
                                          ram_size - initrd_base);

        if (initrd_size < 0) {
            fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                    initrd_filename);
            exit(1);
        }
    }

    /* If we're loading a kernel directly, we must load the device tree too. */
    if (kernel_filename) {
        struct boot_info *boot_info;

#ifndef CONFIG_FDT
        cpu_abort(env, "Compiled without FDT support - can't load kernel\n");
#endif
        dt_base = (kernel_size + DTC_LOAD_PAD) & ~DTC_PAD_MASK;
        if (mpc8544_load_device_tree(env, dt_base, ram_size,
                    initrd_base, initrd_size, kernel_cmdline) < 0) {
            fprintf(stderr, "couldn't load device tree\n");
            exit(1);
        }

        boot_info = env->load_info;
        boot_info->entry = entry;
        boot_info->dt_base = dt_base;
    }

    if (kvm_enabled()) {
        kvmppc_init();
    }
}

static QEMUMachine mpc8544ds_machine = {
    .name = "mpc8544ds",
    .desc = "mpc8544ds",
    .init = mpc8544ds_init,
    .max_cpus = 15,
};

static void mpc8544ds_machine_init(void)
{
    qemu_register_machine(&mpc8544ds_machine);
}

machine_init(mpc8544ds_machine_init);
