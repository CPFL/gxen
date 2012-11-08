/*
 * QEMU PPC PREP hardware System Emulator
 *
 * Copyright (c) 2003-2007 Jocelyn Mayer
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
#include "nvram.h"
#include "pc.h"
#include "fdc.h"
#include "net.h"
#include "sysemu.h"
#include "isa.h"
#include "pci.h"
#include "prep_pci.h"
#include "usb-ohci.h"
#include "ppc.h"
#include "boards.h"
#include "qemu-log.h"
#include "ide.h"
#include "loader.h"
#include "mc146818rtc.h"
#include "blockdev.h"
#include "exec-memory.h"

//#define HARD_DEBUG_PPC_IO
//#define DEBUG_PPC_IO

/* SMP is not enabled, for now */
#define MAX_CPUS 1

#define MAX_IDE_BUS 2

#define BIOS_SIZE (1024 * 1024)
#define BIOS_FILENAME "ppc_rom.bin"
#define KERNEL_LOAD_ADDR 0x01000000
#define INITRD_LOAD_ADDR 0x01800000

#if defined (HARD_DEBUG_PPC_IO) && !defined (DEBUG_PPC_IO)
#define DEBUG_PPC_IO
#endif

#if defined (HARD_DEBUG_PPC_IO)
#define PPC_IO_DPRINTF(fmt, ...)                         \
do {                                                     \
    if (qemu_loglevel_mask(CPU_LOG_IOPORT)) {            \
        qemu_log("%s: " fmt, __func__ , ## __VA_ARGS__); \
    } else {                                             \
        printf("%s : " fmt, __func__ , ## __VA_ARGS__);  \
    }                                                    \
} while (0)
#elif defined (DEBUG_PPC_IO)
#define PPC_IO_DPRINTF(fmt, ...) \
qemu_log_mask(CPU_LOG_IOPORT, fmt, ## __VA_ARGS__)
#else
#define PPC_IO_DPRINTF(fmt, ...) do { } while (0)
#endif

/* Constants for devices init */
static const int ide_iobase[2] = { 0x1f0, 0x170 };
static const int ide_iobase2[2] = { 0x3f6, 0x376 };
static const int ide_irq[2] = { 13, 13 };

#define NE2000_NB_MAX 6

static uint32_t ne2000_io[NE2000_NB_MAX] = { 0x300, 0x320, 0x340, 0x360, 0x280, 0x380 };
static int ne2000_irq[NE2000_NB_MAX] = { 9, 10, 11, 3, 4, 5 };

//static ISADevice *pit;

/* ISA IO ports bridge */
#define PPC_IO_BASE 0x80000000

#if 0
/* Speaker port 0x61 */
static int speaker_data_on;
static int dummy_refresh_clock;
#endif

static void speaker_ioport_write (void *opaque, uint32_t addr, uint32_t val)
{
#if 0
    speaker_data_on = (val >> 1) & 1;
    pit_set_gate(pit, 2, val & 1);
#endif
}

static uint32_t speaker_ioport_read (void *opaque, uint32_t addr)
{
#if 0
    int out;
    out = pit_get_out(pit, 2, qemu_get_clock_ns(vm_clock));
    dummy_refresh_clock ^= 1;
    return (speaker_data_on << 1) | pit_get_gate(pit, 2) | (out << 5) |
        (dummy_refresh_clock << 4);
#endif
    return 0;
}

/* PCI intack register */
/* Read-only register (?) */
static void PPC_intack_write (void *opaque, target_phys_addr_t addr,
                              uint64_t value, unsigned size)
{
#if 0
    printf("%s: 0x" TARGET_FMT_plx " => 0x%08" PRIx64 "\n", __func__, addr,
           value);
#endif
}

static uint64_t PPC_intack_read(void *opaque, target_phys_addr_t addr,
                                unsigned size)
{
    uint32_t retval = 0;

    if ((addr & 0xf) == 0)
        retval = pic_read_irq(isa_pic);
#if 0
    printf("%s: 0x" TARGET_FMT_plx " <= %08" PRIx32 "\n", __func__, addr,
           retval);
#endif

    return retval;
}

static const MemoryRegionOps PPC_intack_ops = {
    .read = PPC_intack_read,
    .write = PPC_intack_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* PowerPC control and status registers */
#if 0 // Not used
static struct {
    /* IDs */
    uint32_t veni_devi;
    uint32_t revi;
    /* Control and status */
    uint32_t gcsr;
    uint32_t xcfr;
    uint32_t ct32;
    uint32_t mcsr;
    /* General purpose registers */
    uint32_t gprg[6];
    /* Exceptions */
    uint32_t feen;
    uint32_t fest;
    uint32_t fema;
    uint32_t fecl;
    uint32_t eeen;
    uint32_t eest;
    uint32_t eecl;
    uint32_t eeint;
    uint32_t eemck0;
    uint32_t eemck1;
    /* Error diagnostic */
} XCSR;

static void PPC_XCSR_writeb (void *opaque,
                             target_phys_addr_t addr, uint32_t value)
{
    printf("%s: 0x" TARGET_FMT_plx " => 0x%08" PRIx32 "\n", __func__, addr,
           value);
}

static void PPC_XCSR_writew (void *opaque,
                             target_phys_addr_t addr, uint32_t value)
{
    printf("%s: 0x" TARGET_FMT_plx " => 0x%08" PRIx32 "\n", __func__, addr,
           value);
}

static void PPC_XCSR_writel (void *opaque,
                             target_phys_addr_t addr, uint32_t value)
{
    printf("%s: 0x" TARGET_FMT_plx " => 0x%08" PRIx32 "\n", __func__, addr,
           value);
}

static uint32_t PPC_XCSR_readb (void *opaque, target_phys_addr_t addr)
{
    uint32_t retval = 0;

    printf("%s: 0x" TARGET_FMT_plx " <= %08" PRIx32 "\n", __func__, addr,
           retval);

    return retval;
}

static uint32_t PPC_XCSR_readw (void *opaque, target_phys_addr_t addr)
{
    uint32_t retval = 0;

    printf("%s: 0x" TARGET_FMT_plx " <= %08" PRIx32 "\n", __func__, addr,
           retval);

    return retval;
}

static uint32_t PPC_XCSR_readl (void *opaque, target_phys_addr_t addr)
{
    uint32_t retval = 0;

    printf("%s: 0x" TARGET_FMT_plx " <= %08" PRIx32 "\n", __func__, addr,
           retval);

    return retval;
}

static const MemoryRegionOps PPC_XCSR_ops = {
    .old_mmio = {
        .read = { PPC_XCSR_readb, PPC_XCSR_readw, PPC_XCSR_readl, },
        .write = { PPC_XCSR_writeb, PPC_XCSR_writew, PPC_XCSR_writel, },
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

#endif

/* Fake super-io ports for PREP platform (Intel 82378ZB) */
typedef struct sysctrl_t {
    qemu_irq reset_irq;
    M48t59State *nvram;
    uint8_t state;
    uint8_t syscontrol;
    uint8_t fake_io[2];
    int contiguous_map;
    int endian;
} sysctrl_t;

enum {
    STATE_HARDFILE = 0x01,
};

static sysctrl_t *sysctrl;

static void PREP_io_write (void *opaque, uint32_t addr, uint32_t val)
{
    sysctrl_t *sysctrl = opaque;

    PPC_IO_DPRINTF("0x%08" PRIx32 " => 0x%02" PRIx32 "\n", addr - PPC_IO_BASE,
                   val);
    sysctrl->fake_io[addr - 0x0398] = val;
}

static uint32_t PREP_io_read (void *opaque, uint32_t addr)
{
    sysctrl_t *sysctrl = opaque;

    PPC_IO_DPRINTF("0x%08" PRIx32 " <= 0x%02" PRIx32 "\n", addr - PPC_IO_BASE,
                   sysctrl->fake_io[addr - 0x0398]);
    return sysctrl->fake_io[addr - 0x0398];
}

static void PREP_io_800_writeb (void *opaque, uint32_t addr, uint32_t val)
{
    sysctrl_t *sysctrl = opaque;

    PPC_IO_DPRINTF("0x%08" PRIx32 " => 0x%02" PRIx32 "\n",
                   addr - PPC_IO_BASE, val);
    switch (addr) {
    case 0x0092:
        /* Special port 92 */
        /* Check soft reset asked */
        if (val & 0x01) {
            qemu_irq_raise(sysctrl->reset_irq);
        } else {
            qemu_irq_lower(sysctrl->reset_irq);
        }
        /* Check LE mode */
        if (val & 0x02) {
            sysctrl->endian = 1;
        } else {
            sysctrl->endian = 0;
        }
        break;
    case 0x0800:
        /* Motorola CPU configuration register : read-only */
        break;
    case 0x0802:
        /* Motorola base module feature register : read-only */
        break;
    case 0x0803:
        /* Motorola base module status register : read-only */
        break;
    case 0x0808:
        /* Hardfile light register */
        if (val & 1)
            sysctrl->state |= STATE_HARDFILE;
        else
            sysctrl->state &= ~STATE_HARDFILE;
        break;
    case 0x0810:
        /* Password protect 1 register */
        if (sysctrl->nvram != NULL)
            m48t59_toggle_lock(sysctrl->nvram, 1);
        break;
    case 0x0812:
        /* Password protect 2 register */
        if (sysctrl->nvram != NULL)
            m48t59_toggle_lock(sysctrl->nvram, 2);
        break;
    case 0x0814:
        /* L2 invalidate register */
        //        tlb_flush(first_cpu, 1);
        break;
    case 0x081C:
        /* system control register */
        sysctrl->syscontrol = val & 0x0F;
        break;
    case 0x0850:
        /* I/O map type register */
        sysctrl->contiguous_map = val & 0x01;
        break;
    default:
        printf("ERROR: unaffected IO port write: %04" PRIx32
               " => %02" PRIx32"\n", addr, val);
        break;
    }
}

static uint32_t PREP_io_800_readb (void *opaque, uint32_t addr)
{
    sysctrl_t *sysctrl = opaque;
    uint32_t retval = 0xFF;

    switch (addr) {
    case 0x0092:
        /* Special port 92 */
        retval = 0x00;
        break;
    case 0x0800:
        /* Motorola CPU configuration register */
        retval = 0xEF; /* MPC750 */
        break;
    case 0x0802:
        /* Motorola Base module feature register */
        retval = 0xAD; /* No ESCC, PMC slot neither ethernet */
        break;
    case 0x0803:
        /* Motorola base module status register */
        retval = 0xE0; /* Standard MPC750 */
        break;
    case 0x080C:
        /* Equipment present register:
         *  no L2 cache
         *  no upgrade processor
         *  no cards in PCI slots
         *  SCSI fuse is bad
         */
        retval = 0x3C;
        break;
    case 0x0810:
        /* Motorola base module extended feature register */
        retval = 0x39; /* No USB, CF and PCI bridge. NVRAM present */
        break;
    case 0x0814:
        /* L2 invalidate: don't care */
        break;
    case 0x0818:
        /* Keylock */
        retval = 0x00;
        break;
    case 0x081C:
        /* system control register
         * 7 - 6 / 1 - 0: L2 cache enable
         */
        retval = sysctrl->syscontrol;
        break;
    case 0x0823:
        /* */
        retval = 0x03; /* no L2 cache */
        break;
    case 0x0850:
        /* I/O map type register */
        retval = sysctrl->contiguous_map;
        break;
    default:
        printf("ERROR: unaffected IO port: %04" PRIx32 " read\n", addr);
        break;
    }
    PPC_IO_DPRINTF("0x%08" PRIx32 " <= 0x%02" PRIx32 "\n",
                   addr - PPC_IO_BASE, retval);

    return retval;
}

static inline target_phys_addr_t prep_IO_address(sysctrl_t *sysctrl,
                                                 target_phys_addr_t addr)
{
    if (sysctrl->contiguous_map == 0) {
        /* 64 KB contiguous space for IOs */
        addr &= 0xFFFF;
    } else {
        /* 8 MB non-contiguous space for IOs */
        addr = (addr & 0x1F) | ((addr & 0x007FFF000) >> 7);
    }

    return addr;
}

static void PPC_prep_io_writeb (void *opaque, target_phys_addr_t addr,
                                uint32_t value)
{
    sysctrl_t *sysctrl = opaque;

    addr = prep_IO_address(sysctrl, addr);
    cpu_outb(addr, value);
}

static uint32_t PPC_prep_io_readb (void *opaque, target_phys_addr_t addr)
{
    sysctrl_t *sysctrl = opaque;
    uint32_t ret;

    addr = prep_IO_address(sysctrl, addr);
    ret = cpu_inb(addr);

    return ret;
}

static void PPC_prep_io_writew (void *opaque, target_phys_addr_t addr,
                                uint32_t value)
{
    sysctrl_t *sysctrl = opaque;

    addr = prep_IO_address(sysctrl, addr);
    PPC_IO_DPRINTF("0x" TARGET_FMT_plx " => 0x%08" PRIx32 "\n", addr, value);
    cpu_outw(addr, value);
}

static uint32_t PPC_prep_io_readw (void *opaque, target_phys_addr_t addr)
{
    sysctrl_t *sysctrl = opaque;
    uint32_t ret;

    addr = prep_IO_address(sysctrl, addr);
    ret = cpu_inw(addr);
    PPC_IO_DPRINTF("0x" TARGET_FMT_plx " <= 0x%08" PRIx32 "\n", addr, ret);

    return ret;
}

static void PPC_prep_io_writel (void *opaque, target_phys_addr_t addr,
                                uint32_t value)
{
    sysctrl_t *sysctrl = opaque;

    addr = prep_IO_address(sysctrl, addr);
    PPC_IO_DPRINTF("0x" TARGET_FMT_plx " => 0x%08" PRIx32 "\n", addr, value);
    cpu_outl(addr, value);
}

static uint32_t PPC_prep_io_readl (void *opaque, target_phys_addr_t addr)
{
    sysctrl_t *sysctrl = opaque;
    uint32_t ret;

    addr = prep_IO_address(sysctrl, addr);
    ret = cpu_inl(addr);
    PPC_IO_DPRINTF("0x" TARGET_FMT_plx " <= 0x%08" PRIx32 "\n", addr, ret);

    return ret;
}

static const MemoryRegionOps PPC_prep_io_ops = {
    .old_mmio = {
        .read = { PPC_prep_io_readb, PPC_prep_io_readw, PPC_prep_io_readl },
        .write = { PPC_prep_io_writeb, PPC_prep_io_writew, PPC_prep_io_writel },
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

#define NVRAM_SIZE        0x2000

static void cpu_request_exit(void *opaque, int irq, int level)
{
    CPUState *env = cpu_single_env;

    if (env && level) {
        cpu_exit(env);
    }
}

/* PowerPC PREP hardware initialisation */
static void ppc_prep_init (ram_addr_t ram_size,
                           const char *boot_device,
                           const char *kernel_filename,
                           const char *kernel_cmdline,
                           const char *initrd_filename,
                           const char *cpu_model)
{
    MemoryRegion *sysmem = get_system_memory();
    CPUState *env = NULL;
    char *filename;
    nvram_t nvram;
    M48t59State *m48t59;
    MemoryRegion *PPC_io_memory = g_new(MemoryRegion, 1);
    MemoryRegion *intack = g_new(MemoryRegion, 1);
#if 0
    MemoryRegion *xcsr = g_new(MemoryRegion, 1);
#endif
    int linux_boot, i, nb_nics1, bios_size;
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *bios = g_new(MemoryRegion, 1);
    uint32_t kernel_base, initrd_base;
    long kernel_size, initrd_size;
    PCIBus *pci_bus;
    qemu_irq *i8259;
    qemu_irq *cpu_exit_irq;
    int ppc_boot_device;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    DriveInfo *fd[MAX_FD];

    sysctrl = g_malloc0(sizeof(sysctrl_t));

    linux_boot = (kernel_filename != NULL);

    /* init CPUs */
    if (cpu_model == NULL)
        cpu_model = "602";
    for (i = 0; i < smp_cpus; i++) {
        env = cpu_init(cpu_model);
        if (!env) {
            fprintf(stderr, "Unable to find PowerPC CPU definition\n");
            exit(1);
        }
        if (env->flags & POWERPC_FLAG_RTC_CLK) {
            /* POWER / PowerPC 601 RTC clock frequency is 7.8125 MHz */
            cpu_ppc_tb_init(env, 7812500UL);
        } else {
            /* Set time-base frequency to 100 Mhz */
            cpu_ppc_tb_init(env, 100UL * 1000UL * 1000UL);
        }
        qemu_register_reset((QEMUResetHandler*)&cpu_reset, env);
    }

    /* allocate RAM */
    memory_region_init_ram(ram, NULL, "ppc_prep.ram", ram_size);
    memory_region_add_subregion(sysmem, 0, ram);

    /* allocate and load BIOS */
    memory_region_init_ram(bios, NULL, "ppc_prep.bios", BIOS_SIZE);
    if (bios_name == NULL)
        bios_name = BIOS_FILENAME;
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (filename) {
        bios_size = get_image_size(filename);
    } else {
        bios_size = -1;
    }
    if (bios_size > 0 && bios_size <= BIOS_SIZE) {
        target_phys_addr_t bios_addr;
        bios_size = (bios_size + 0xfff) & ~0xfff;
        bios_addr = (uint32_t)(-bios_size);
        memory_region_set_readonly(bios, true);
        memory_region_add_subregion(sysmem, bios_addr, bios);
        bios_size = load_image_targphys(filename, bios_addr, bios_size);
    }
    if (bios_size < 0 || bios_size > BIOS_SIZE) {
        hw_error("qemu: could not load PPC PREP bios '%s'\n", bios_name);
    }
    if (filename) {
        g_free(filename);
    }

    if (linux_boot) {
        kernel_base = KERNEL_LOAD_ADDR;
        /* now we can load the kernel */
        kernel_size = load_image_targphys(kernel_filename, kernel_base,
                                          ram_size - kernel_base);
        if (kernel_size < 0) {
            hw_error("qemu: could not load kernel '%s'\n", kernel_filename);
            exit(1);
        }
        /* load initrd */
        if (initrd_filename) {
            initrd_base = INITRD_LOAD_ADDR;
            initrd_size = load_image_targphys(initrd_filename, initrd_base,
                                              ram_size - initrd_base);
            if (initrd_size < 0) {
                hw_error("qemu: could not load initial ram disk '%s'\n",
                          initrd_filename);
            }
        } else {
            initrd_base = 0;
            initrd_size = 0;
        }
        ppc_boot_device = 'm';
    } else {
        kernel_base = 0;
        kernel_size = 0;
        initrd_base = 0;
        initrd_size = 0;
        ppc_boot_device = '\0';
        /* For now, OHW cannot boot from the network. */
        for (i = 0; boot_device[i] != '\0'; i++) {
            if (boot_device[i] >= 'a' && boot_device[i] <= 'f') {
                ppc_boot_device = boot_device[i];
                break;
            }
        }
        if (ppc_boot_device == '\0') {
            fprintf(stderr, "No valid boot device for Mac99 machine\n");
            exit(1);
        }
    }

    isa_mem_base = 0xc0000000;
    if (PPC_INPUT(env) != PPC_FLAGS_INPUT_6xx) {
        hw_error("Only 6xx bus is supported on PREP machine\n");
    }
    /* Hmm, prep has no pci-isa bridge ??? */
    isa_bus_new(NULL, get_system_io());
    i8259 = i8259_init(first_cpu->irq_inputs[PPC6xx_INPUT_INT]);
    pci_bus = pci_prep_init(i8259, get_system_memory(), get_system_io());
    isa_bus_irqs(i8259);
    //    pci_bus = i440fx_init();
    /* Register 8 MB of ISA IO space (needed for non-contiguous map) */
    memory_region_init_io(PPC_io_memory, &PPC_prep_io_ops, sysctrl,
                          "ppc-io", 0x00800000);
    memory_region_add_subregion(sysmem, 0x80000000, PPC_io_memory);

    /* init basic PC hardware */
    pci_vga_init(pci_bus);
    //    openpic = openpic_init(0x00000000, 0xF0000000, 1);
    //    pit = pit_init(0x40, 0);
    rtc_init(2000, NULL);

    if (serial_hds[0])
        serial_isa_init(0, serial_hds[0]);
    nb_nics1 = nb_nics;
    if (nb_nics1 > NE2000_NB_MAX)
        nb_nics1 = NE2000_NB_MAX;
    for(i = 0; i < nb_nics1; i++) {
        if (nd_table[i].model == NULL) {
	    nd_table[i].model = g_strdup("ne2k_isa");
        }
        if (strcmp(nd_table[i].model, "ne2k_isa") == 0) {
            isa_ne2000_init(ne2000_io[i], ne2000_irq[i], &nd_table[i]);
        } else {
            pci_nic_init_nofail(&nd_table[i], "ne2k_pci", NULL);
        }
    }

    ide_drive_get(hd, MAX_IDE_BUS);
    for(i = 0; i < MAX_IDE_BUS; i++) {
        isa_ide_init(ide_iobase[i], ide_iobase2[i], ide_irq[i],
                     hd[2 * i],
		     hd[2 * i + 1]);
    }
    isa_create_simple("i8042");

    cpu_exit_irq = qemu_allocate_irqs(cpu_request_exit, NULL, 1);
    DMA_init(1, cpu_exit_irq);

    //    SB16_init();

    for(i = 0; i < MAX_FD; i++) {
        fd[i] = drive_get(IF_FLOPPY, 0, i);
    }
    fdctrl_init_isa(fd);

    /* Register speaker port */
    register_ioport_read(0x61, 1, 1, speaker_ioport_read, NULL);
    register_ioport_write(0x61, 1, 1, speaker_ioport_write, NULL);
    /* Register fake IO ports for PREP */
    sysctrl->reset_irq = first_cpu->irq_inputs[PPC6xx_INPUT_HRESET];
    register_ioport_read(0x398, 2, 1, &PREP_io_read, sysctrl);
    register_ioport_write(0x398, 2, 1, &PREP_io_write, sysctrl);
    /* System control ports */
    register_ioport_read(0x0092, 0x01, 1, &PREP_io_800_readb, sysctrl);
    register_ioport_write(0x0092, 0x01, 1, &PREP_io_800_writeb, sysctrl);
    register_ioport_read(0x0800, 0x52, 1, &PREP_io_800_readb, sysctrl);
    register_ioport_write(0x0800, 0x52, 1, &PREP_io_800_writeb, sysctrl);
    /* PCI intack location */
    memory_region_init_io(intack, &PPC_intack_ops, NULL, "ppc-intack", 4);
    memory_region_add_subregion(sysmem, 0xBFFFFFF0, intack);
    /* PowerPC control and status register group */
#if 0
    memory_region_init_io(xcsr, &PPC_XCSR_ops, NULL, "ppc-xcsr", 0x1000);
    memory_region_add_subregion(sysmem, 0xFEFF0000, xcsr);
#endif

    if (usb_enabled) {
        usb_ohci_init_pci(pci_bus, -1);
    }

    m48t59 = m48t59_init(i8259[8], 0, 0x0074, NVRAM_SIZE, 59);
    if (m48t59 == NULL)
        return;
    sysctrl->nvram = m48t59;

    /* Initialise NVRAM */
    nvram.opaque = m48t59;
    nvram.read_fn = &m48t59_read;
    nvram.write_fn = &m48t59_write;
    PPC_NVRAM_set_params(&nvram, NVRAM_SIZE, "PREP", ram_size, ppc_boot_device,
                         kernel_base, kernel_size,
                         kernel_cmdline,
                         initrd_base, initrd_size,
                         /* XXX: need an option to load a NVRAM image */
                         0,
                         graphic_width, graphic_height, graphic_depth);

    /* Special port to get debug messages from Open-Firmware */
    register_ioport_write(0x0F00, 4, 1, &PPC_debug_write, NULL);
}

static QEMUMachine prep_machine = {
    .name = "prep",
    .desc = "PowerPC PREP platform",
    .init = ppc_prep_init,
    .max_cpus = MAX_CPUS,
};

static void prep_machine_init(void)
{
    qemu_register_machine(&prep_machine);
}

machine_init(prep_machine_init);
