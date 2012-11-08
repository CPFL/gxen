/*
 * QEMU PC System Emulator
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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
#include "fdc.h"
#include "pci.h"
#include "block.h"
#include "sysemu.h"
#include "audio/audio.h"
#include "net.h"
#include "smbus.h"
#include "boards.h"
#include "console.h"
#include "exec-all.h"
#include "qemu-xen.h"

#include "xen_platform.h"
#include "fw_cfg.h"
#include "virtio-blk.h"
#include "virtio-balloon.h"
#include "virtio-console.h"
#include "hpet_emul.h"

#ifdef CONFIG_PASSTHROUGH
#include "pass-through.h"
#endif

/* output Bochs bios info messages */
//#define DEBUG_BIOS

#define BIOS_FILENAME "bios.bin"
#define VGABIOS_FILENAME "vgabios.bin"
#define VGABIOS_CIRRUS_FILENAME "vgabios-cirrus.bin"

#define PC_MAX_BIOS_SIZE (4 * 1024 * 1024)

/* Leave a chunk of memory at the top of RAM for the BIOS ACPI tables.  */
#define ACPI_DATA_SIZE       0x10000
#define BIOS_CFG_IOPORT 0x510
#define FW_CFG_ACPI_TABLES (FW_CFG_ARCH_LOCAL + 0)

#define MAX_IDE_BUS 2

#ifdef HAS_TPM
void tpm_tis_init(SetIRQFunc *set_irq, void *opaque, int irq);
#endif

extern uint8_t *acpi_tables;
extern size_t acpi_tables_len;

static fdctrl_t *floppy_controller;
static RTCState *rtc_state;
static PITState *pit;
static IOAPICState *ioapic;
static PCIDevice *i440fx_state;

static void xen_relocator_hook(target_phys_addr_t *prot_addr_upd,
                               uint16_t protocol,
			       const uint8_t header[], int kernel_size,
			       target_phys_addr_t real_addr, int real_size);
#define smbus_eeprom_device_init (void)

static void ioport80_write(void *opaque, uint32_t addr, uint32_t data)
{
}

/* MSDOS compatibility mode FPU exception support */
static qemu_irq ferr_irq;
/* XXX: add IGNNE support */
void cpu_set_ferr(CPUX86State *s)
{
    qemu_irq_raise(ferr_irq);
}

static void ioportF0_write(void *opaque, uint32_t addr, uint32_t data)
{
    qemu_irq_lower(ferr_irq);
}

/* TSC handling */
uint64_t cpu_get_tsc(CPUX86State *env)
{
    /* Note: when using kqemu, it is more logical to return the host TSC
       because kqemu does not trap the RDTSC instruction for
       performance reasons */
#ifdef USE_KQEMU
    if (env->kqemu_enabled) {
        return cpu_get_real_ticks();
    } else
#endif
    {
        return cpu_get_ticks();
    }
}

#if 0
/* SMM support */
void cpu_smm_update(CPUState *env)
{
    if (i440fx_state && env == first_cpu)
        i440fx_set_smm(i440fx_state, (env->hflags >> HF_SMM_SHIFT) & 1);
}
#endif

#ifndef CONFIG_DM
/* IRQ handling */
int cpu_get_pic_interrupt(CPUState *env)
{
    int intno;

    intno = apic_get_interrupt(env);
    if (intno >= 0) {
        /* set irq request if a PIC irq is still pending */
        /* XXX: improve that */
        pic_update_irq(isa_pic);
        return intno;
    }
    /* read the irq from the PIC */
    if (!apic_accept_pic_intr(env))
        return -1;

    intno = pic_read_irq(isa_pic);
    return intno;
}

static void pic_irq_request(void *opaque, int irq, int level)
{
    CPUState *env = first_cpu;

    if (env->apic_state) {
        while (env) {
            if (apic_accept_pic_intr(env))
                apic_deliver_pic_intr(env, level);
            env = env->next_cpu;
        }
    } else {
        if (level)
            cpu_interrupt(env, CPU_INTERRUPT_HARD);
        else
            cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
    }
}
#else
#define pic_irq_request 0  /* see i386-dm/i8259.c:i8259_init */
#endif /* !CONFIG_DM */

/* PC cmos mappings */

#define REG_EQUIPMENT_BYTE          0x14

static int cmos_get_fd_drive_type(int fd0)
{
    int val;

    switch (fd0) {
    case 0:
        /* 1.44 Mb 3"5 drive */
        val = 4;
        break;
    case 1:
        /* 2.88 Mb 3"5 drive */
        val = 5;
        break;
    case 2:
        /* 1.2 Mb 5"5 drive */
        val = 2;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void cmos_init_hd(int type_ofs, int info_ofs, BlockDriverState *hd)
{
    RTCState *s = rtc_state;
    int cylinders, heads, sectors;
    bdrv_get_geometry_hint(hd, &cylinders, &heads, &sectors);
    rtc_set_memory(s, type_ofs, 47);
    rtc_set_memory(s, info_ofs, cylinders);
    rtc_set_memory(s, info_ofs + 1, cylinders >> 8);
    rtc_set_memory(s, info_ofs + 2, heads);
    rtc_set_memory(s, info_ofs + 3, 0xff);
    rtc_set_memory(s, info_ofs + 4, 0xff);
    rtc_set_memory(s, info_ofs + 5, 0xc0 | ((heads > 8) << 3));
    rtc_set_memory(s, info_ofs + 6, cylinders);
    rtc_set_memory(s, info_ofs + 7, cylinders >> 8);
    rtc_set_memory(s, info_ofs + 8, sectors);
}

/* convert boot_device letter to something recognizable by the bios */
static int boot_device2nibble(char boot_device)
{
    switch(boot_device) {
    case 'a':
    case 'b':
        return 0x01; /* floppy boot */
    case 'c':
        return 0x02; /* hard drive boot */
    case 'd':
        return 0x03; /* CD-ROM boot */
    case 'n':
        return 0x04; /* Network boot */
    }
    return 0;
}

/* copy/pasted from cmos_init, should be made a general function
 and used there as well */
static int pc_boot_set(void *opaque, const char *boot_device)
{
#define PC_MAX_BOOT_DEVICES 3
    RTCState *s = (RTCState *)opaque;
    int nbds, bds[3] = { 0, };
    int i;

    nbds = strlen(boot_device);
    if (nbds > PC_MAX_BOOT_DEVICES) {
        term_printf("Too many boot devices for PC\n");
        return(1);
    }
    for (i = 0; i < nbds; i++) {
        bds[i] = boot_device2nibble(boot_device[i]);
        if (bds[i] == 0) {
            term_printf("Invalid boot device for PC: '%c'\n",
                    boot_device[i]);
            return(1);
        }
    }
    rtc_set_memory(s, 0x3d, (bds[1] << 4) | bds[0]);
    rtc_set_memory(s, 0x38, (bds[2] << 4));
    return(0);
}

/* hd_table must contain 4 block drivers */
static void cmos_init(ram_addr_t ram_size, ram_addr_t above_4g_mem_size,
                      const char *boot_device, BlockDriverState **hd_table)
{
    RTCState *s = rtc_state;
    int nbds, bds[3] = { 0, };
    int val;
    int fd0, fd1, nb;
    int i;

    /* various important CMOS locations needed by PC/Bochs bios */

    /* memory size */
    val = 640; /* base memory in K */
    rtc_set_memory(s, 0x15, val);
    rtc_set_memory(s, 0x16, val >> 8);

    val = (ram_size / 1024) - 1024;
    if (val > 65535)
        val = 65535;
    rtc_set_memory(s, 0x17, val);
    rtc_set_memory(s, 0x18, val >> 8);
    rtc_set_memory(s, 0x30, val);
    rtc_set_memory(s, 0x31, val >> 8);

    if (above_4g_mem_size) {
        rtc_set_memory(s, 0x5b, (unsigned int)above_4g_mem_size >> 16);
        rtc_set_memory(s, 0x5c, (unsigned int)above_4g_mem_size >> 24);
        rtc_set_memory(s, 0x5d, (uint64_t)above_4g_mem_size >> 32);
    }

    if (ram_size > (16 * 1024 * 1024))
        val = (ram_size / 65536) - ((16 * 1024 * 1024) / 65536);
    else
        val = 0;
    if (val > 65535)
        val = 65535;
    rtc_set_memory(s, 0x34, val);
    rtc_set_memory(s, 0x35, val >> 8);

    /* set the number of CPU */
    rtc_set_memory(s, 0x5f, smp_cpus - 1);

    /* set boot devices, and disable floppy signature check if requested */
#define PC_MAX_BOOT_DEVICES 3
    nbds = strlen(boot_device);
    if (nbds > PC_MAX_BOOT_DEVICES) {
        fprintf(stderr, "Too many boot devices for PC\n");
        exit(1);
    }
    for (i = 0; i < nbds; i++) {
        bds[i] = boot_device2nibble(boot_device[i]);
        if (bds[i] == 0) {
            fprintf(stderr, "Invalid boot device for PC: '%c'\n",
                    boot_device[i]);
            exit(1);
        }
    }
    rtc_set_memory(s, 0x3d, (bds[1] << 4) | bds[0]);
    rtc_set_memory(s, 0x38, (bds[2] << 4) | (fd_bootchk ?  0x0 : 0x1));

    /* floppy type */

    fd0 = fdctrl_get_drive_type(floppy_controller, 0);
    fd1 = fdctrl_get_drive_type(floppy_controller, 1);

    val = (cmos_get_fd_drive_type(fd0) << 4) | cmos_get_fd_drive_type(fd1);
    rtc_set_memory(s, 0x10, val);

    val = 0;
    nb = 0;
    if (fd0 < 3)
        nb++;
    if (fd1 < 3)
        nb++;
    switch (nb) {
    case 0:
        break;
    case 1:
        val |= 0x01; /* 1 drive, ready for boot */
        break;
    case 2:
        val |= 0x41; /* 2 drives, ready for boot */
        break;
    }
    val |= 0x02; /* FPU is there */
    val |= 0x04; /* PS/2 mouse installed */
    rtc_set_memory(s, REG_EQUIPMENT_BYTE, val);

    /* hard drives */

    rtc_set_memory(s, 0x12, (hd_table[0] ? 0xf0 : 0) | (hd_table[1] ? 0x0f : 0));
    if (hd_table[0])
        cmos_init_hd(0x19, 0x1b, hd_table[0]);
    if (hd_table[1])
        cmos_init_hd(0x1a, 0x24, hd_table[1]);

    val = 0;
    for (i = 0; i < 4; i++) {
        if (hd_table[i]) {
            int cylinders, heads, sectors, translation;
            /* NOTE: bdrv_get_geometry_hint() returns the physical
                geometry.  It is always such that: 1 <= sects <= 63, 1
                <= heads <= 16, 1 <= cylinders <= 16383. The BIOS
                geometry can be different if a translation is done. */
            translation = bdrv_get_translation_hint(hd_table[i]);
            if (translation == BIOS_ATA_TRANSLATION_AUTO) {
                bdrv_get_geometry_hint(hd_table[i], &cylinders, &heads, &sectors);
                if (cylinders <= 1024 && heads <= 16 && sectors <= 63) {
                    /* No translation. */
                    translation = 0;
                } else {
                    /* LBA translation. */
                    translation = 1;
                }
            } else {
                translation--;
            }
            val |= translation << (i * 2);
        }
    }
    rtc_set_memory(s, 0x39, val);
}

void ioport_set_a20(int enable)
{
    /* XXX: send to all CPUs ? */
    cpu_x86_set_a20(first_cpu, enable);
}

int ioport_get_a20(void)
{
    return ((first_cpu->a20_mask >> 20) & 1);
}

static void ioport92_write(void *opaque, uint32_t addr, uint32_t val)
{
    ioport_set_a20((val >> 1) & 1);
    /* XXX: bit 0 is fast reset */
}

static uint32_t ioport92_read(void *opaque, uint32_t addr)
{
    return ioport_get_a20() << 1;
}

/***********************************************************/
/* Bochs BIOS debug ports */

static void bochs_bios_write(void *opaque, uint32_t addr, uint32_t val)
{
    static const char shutdown_str[8] = "Shutdown";
    static int shutdown_index = 0;

    switch(addr) {
        /* Bochs BIOS messages */
    case 0x400:
    case 0x401:
        fprintf(stderr, "BIOS panic at rombios.c, line %d\n", val);
        exit(1);
    case 0x402:
    case 0x403:
#ifdef DEBUG_BIOS
        fprintf(stderr, "%c", val);
#endif
        break;
    case 0x8900:
        /* same as Bochs power off */
        if (val == shutdown_str[shutdown_index]) {
            shutdown_index++;
            if (shutdown_index == 8) {
                shutdown_index = 0;
                qemu_system_shutdown_request();
            }
        } else {
            shutdown_index = 0;
        }
        break;

        /* LGPL'ed VGA BIOS messages */
    case 0x501:
    case 0x502:
        fprintf(stderr, "VGA BIOS panic, line %d\n", val);
        exit(1);
    case 0x500:
    case 0x503:
#ifdef DEBUG_BIOS
        fprintf(stderr, "%c", val);
#endif
        break;
    }
}

static void bochs_bios_init(void)
{
    void *fw_cfg;

    register_ioport_write(0x400, 1, 2, bochs_bios_write, NULL);
    register_ioport_write(0x401, 1, 2, bochs_bios_write, NULL);
    register_ioport_write(0x402, 1, 1, bochs_bios_write, NULL);
    register_ioport_write(0x403, 1, 1, bochs_bios_write, NULL);
    register_ioport_write(0x8900, 1, 1, bochs_bios_write, NULL);

    register_ioport_write(0x501, 1, 2, bochs_bios_write, NULL);
    register_ioport_write(0x502, 1, 2, bochs_bios_write, NULL);
    register_ioport_write(0x500, 1, 1, bochs_bios_write, NULL);
    register_ioport_write(0x503, 1, 1, bochs_bios_write, NULL);

    fw_cfg = fw_cfg_init(BIOS_CFG_IOPORT, BIOS_CFG_IOPORT + 1, 0, 0);
    fw_cfg_add_i32(fw_cfg, FW_CFG_ID, 1);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
#ifndef CONFIG_DM
    fw_cfg_add_bytes(fw_cfg, FW_CFG_ACPI_TABLES, acpi_tables, acpi_tables_len);
#endif /*CONFIG_DM*/
}

/* Generate an initial boot sector which sets state and jump to
   a specified vector */
static void generate_bootsect(uint8_t *option_rom,
                              uint32_t gpr[8], uint16_t segs[6], uint16_t ip)
{
    uint8_t rom[512], *p, *reloc;
    uint8_t sum;
    int i;

    memset(rom, 0, sizeof(rom));

    p = rom;
    /* Make sure we have an option rom signature */
    *p++ = 0x55;
    *p++ = 0xaa;

    /* ROM size in sectors*/
    *p++ = 1;

    /* Hook int19 */

    *p++ = 0x50;		/* push ax */
    *p++ = 0x1e;		/* push ds */
    *p++ = 0x31; *p++ = 0xc0;	/* xor ax, ax */
    *p++ = 0x8e; *p++ = 0xd8;	/* mov ax, ds */

    *p++ = 0xc7; *p++ = 0x06;   /* movvw _start,0x64 */
    *p++ = 0x64; *p++ = 0x00;
    reloc = p;
    *p++ = 0x00; *p++ = 0x00;

    *p++ = 0x8c; *p++ = 0x0e;   /* mov cs,0x66 */
    *p++ = 0x66; *p++ = 0x00;

    *p++ = 0x1f;		/* pop ds */
    *p++ = 0x58;		/* pop ax */
    *p++ = 0xcb;		/* lret */
    
    /* Actual code */
    *reloc = (p - rom);

    *p++ = 0xfa;		/* CLI */
    *p++ = 0xfc;		/* CLD */

    for (i = 0; i < 6; i++) {
	if (i == 1)		/* Skip CS */
	    continue;

	*p++ = 0xb8;		/* MOV AX,imm16 */
	*p++ = segs[i];
	*p++ = segs[i] >> 8;
	*p++ = 0x8e;		/* MOV <seg>,AX */
	*p++ = 0xc0 + (i << 3);
    }

    for (i = 0; i < 8; i++) {
	*p++ = 0x66;		/* 32-bit operand size */
	*p++ = 0xb8 + i;	/* MOV <reg>,imm32 */
	*p++ = gpr[i];
	*p++ = gpr[i] >> 8;
	*p++ = gpr[i] >> 16;
	*p++ = gpr[i] >> 24;
    }

    *p++ = 0xea;		/* JMP FAR */
    *p++ = ip;			/* IP */
    *p++ = ip >> 8;
    *p++ = segs[1];		/* CS */
    *p++ = segs[1] >> 8;

    /* sign rom */
    sum = 0;
    for (i = 0; i < (sizeof(rom) - 1); i++)
        sum += rom[i];
    rom[sizeof(rom) - 1] = -sum;

    memcpy(option_rom, rom, sizeof(rom));
}

static long get_file_size(FILE *f)
{
    long where, size;

    /* XXX: on Unix systems, using fstat() probably makes more sense */

    where = ftell(f);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, where, SEEK_SET);

    return size;
}

static void load_linux(uint8_t *option_rom,
                       const char *kernel_filename,
		       const char *initrd_filename,
		       const char *kernel_cmdline)
{
#ifndef __ia64__
    uint16_t protocol;
    uint32_t gpr[8];
    uint16_t seg[6];
    uint16_t real_seg;
    int setup_size, kernel_size, initrd_size, cmdline_size;
    unsigned long end_low_ram;
    uint32_t initrd_max;
    uint8_t header[1024];
    target_phys_addr_t real_addr, prot_addr, cmdline_addr, initrd_addr;
    FILE *f, *fi;

    /* Align to 16 bytes as a paranoia measure */
    cmdline_size = (strlen(kernel_cmdline)+16) & ~15;

    /* load the kernel header */
    f = fopen(kernel_filename, "rb");
    if (!f || !(kernel_size = get_file_size(f)) ||
	fread(header, 1, 1024, f) != 1024) {
	fprintf(stderr, "qemu: could not load kernel '%s'\n",
		kernel_filename);
	exit(1);
    }

    /* kernel protocol version */
#if 0
    fprintf(stderr, "header magic: %#x\n", ldl_p(header+0x202));
#endif
    if (ldl_p(header+0x202) == 0x53726448)
	protocol = lduw_p(header+0x206);
    else
	protocol = 0;

    if (protocol < 0x200 || !(header[0x211] & 0x01)) {
	/* Low kernel */
	real_addr    = 0x90000;
	cmdline_addr = 0x9a000 - cmdline_size;
	prot_addr    = 0x10000;
    } else if (protocol < 0x202) {
	/* High but ancient kernel */
	real_addr    = 0x90000;
	cmdline_addr = 0x9a000 - cmdline_size;
	prot_addr    = 0x100000;
    } else {
	/* High and recent kernel */
	real_addr    = 0x10000;
	cmdline_addr = 0x20000;
	prot_addr    = 0x100000;
    }

#if 1
    fprintf(stderr,
	    "qemu: real_addr     = 0x" TARGET_FMT_plx "\n"
	    "qemu: cmdline_addr  = 0x" TARGET_FMT_plx "\n"
	    "qemu: prot_addr     = 0x" TARGET_FMT_plx "\n",
	    real_addr,
	    cmdline_addr,
	    prot_addr);
#endif

    /* Special pages are placed at end of low RAM: pick an arbitrary one and
     * subtract a suitably large amount of padding (64kB) to skip BIOS data. */
    xc_get_hvm_param(xc_handle, domid, HVM_PARAM_BUFIOREQ_PFN, &end_low_ram);
    end_low_ram = (end_low_ram << 12) - (64*1024);

    /* highest address for loading the initrd */
    initrd_max = (protocol >= 0x203) ? ldl_p(header+0x22c) : 0x37ffffff;
    initrd_max = MIN(initrd_max, (uint32_t)end_low_ram);

    /* kernel command line */
    pstrcpy_targphys(cmdline_addr, 4096, kernel_cmdline);

    if (protocol >= 0x202) {
	stl_p(header+0x228, cmdline_addr);
    } else {
	stw_p(header+0x20, 0xA33F);
	stw_p(header+0x22, cmdline_addr-real_addr);
    }

    /* loader type */
    /* High nybble = B reserved for Qemu; low nybble is revision number.
       If this code is substantially changed, you may want to consider
       incrementing the revision. */
    if (protocol >= 0x200)
	header[0x210] = 0xB0;

    /* heap */
    if (protocol >= 0x201) {
	header[0x211] |= 0x80;	/* CAN_USE_HEAP */
	stw_p(header+0x224, cmdline_addr-real_addr-0x200);
    }

    /* load initrd */
    if (initrd_filename) {
	if (protocol < 0x200) {
	    fprintf(stderr, "qemu: linux kernel too old to load a ram disk\n");
	    exit(1);
	}

	fi = fopen(initrd_filename, "rb");
	if (!fi) {
	    fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
		    initrd_filename);
	    exit(1);
	}

	initrd_size = get_file_size(fi);
	initrd_addr = (initrd_max-initrd_size) & ~4095;

        fprintf(stderr, "qemu: loading initrd (%#x bytes) at 0x" TARGET_FMT_plx
                "\n", initrd_size, initrd_addr);

	if (!fread_targphys_ok(initrd_addr, initrd_size, fi)) {
	    fprintf(stderr, "qemu: read error on initial ram disk '%s'\n",
		    initrd_filename);
	    exit(1);
	}
	fclose(fi);

	stl_p(header+0x218, initrd_addr);
	stl_p(header+0x21c, initrd_size);
    }

    /* store the finalized header and load the rest of the kernel */
    cpu_physical_memory_write(real_addr, header, 1024);

    setup_size = header[0x1f1];
    if (setup_size == 0)
	setup_size = 4;

    setup_size = (setup_size+1)*512;
    kernel_size -= setup_size;	/* Size of protected-mode code */

    xen_relocator_hook(&prot_addr, protocol, header, kernel_size,
		       real_addr, setup_size-1024);

    if (!fread_targphys_ok(real_addr+1024, setup_size-1024, f) ||
	!fread_targphys_ok(prot_addr, kernel_size, f)) {
	fprintf(stderr, "qemu: read error on kernel '%s'\n",
		kernel_filename);
	exit(1);
    }
    fclose(f);

    /* generate bootsector to set up the initial register state */
    real_seg = real_addr >> 4;
    seg[0] = seg[2] = seg[3] = seg[4] = real_seg;
    seg[1] = real_seg+0x20;	/* CS */
    memset(gpr, 0, sizeof gpr);
    gpr[4] = cmdline_addr-real_addr-16;	/* SP (-16 is paranoia) */

    generate_bootsect(option_rom, gpr, seg, 0);
#endif
}

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);
}

static const int ide_iobase[2] = { 0x1f0, 0x170 };
static const int ide_iobase2[2] = { 0x3f6, 0x376 };
static const int ide_irq[2] = { 14, 15 };

#define NE2000_NB_MAX 6

static int ne2000_io[NE2000_NB_MAX] = { 0x300, 0x320, 0x340, 0x360, 0x280, 0x380 };
static int ne2000_irq[NE2000_NB_MAX] = { 9, 10, 11, 3, 4, 5 };

static int serial_io[MAX_SERIAL_PORTS] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
static int serial_irq[MAX_SERIAL_PORTS] = { 4, 3, 4, 3 };

static int parallel_io[MAX_PARALLEL_PORTS] = { 0x378, 0x278, 0x3bc };
static int parallel_irq[MAX_PARALLEL_PORTS] = { 7, 7, 7 };

#ifdef HAS_AUDIO
static void audio_init (PCIBus *pci_bus, qemu_irq *pic)
{
    struct soundhw *c;
    int audio_enabled = 0;

    for (c = soundhw; !audio_enabled && c->name; ++c) {
        audio_enabled = c->enabled;
    }

    if (audio_enabled) {
        AudioState *s;

        s = AUD_init ();
        if (s) {
            for (c = soundhw; c->name; ++c) {
                if (c->enabled) {
                    if (c->isa) {
                        c->init.init_isa (s, pic);
                    }
                    else {
                        if (pci_bus) {
                            c->init.init_pci (pci_bus, s);
                        }
                    }
                }
            }
        }
    }
}
#endif

static void pc_init_ne2k_isa(NICInfo *nd, qemu_irq *pic)
{
    static int nb_ne2k = 0;

    if (nb_ne2k == NE2000_NB_MAX)
        return;
    isa_ne2000_init(ne2000_io[nb_ne2k], pic[ne2000_irq[nb_ne2k]], nd);
    nb_ne2k++;
}

/* PC hardware initialisation */
static void pc_init1(ram_addr_t ram_size, int vga_ram_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename,
                     int pci_enabled, const char *cpu_model)
{
    char buf[1024];
    int ret, linux_boot, i;
    int disable_pf;
    ram_addr_t ram_addr, vga_ram_addr, bios_offset, vga_bios_offset;
    ram_addr_t below_4g_mem_size, above_4g_mem_size = 0;
    int bios_size, isa_bios_size, vga_bios_size;
    PCIBus *pci_bus;
    int piix3_devfn = -1;
    CPUState *env;
    qemu_irq *cpu_irq;
    qemu_irq *i8259;
    int index;
    BlockDriverState *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    BlockDriverState *fd[MAX_FD];
    int rc;

    if (ram_size >= 0xe0000000 ) {
        above_4g_mem_size = ram_size - 0xe0000000;
        below_4g_mem_size = 0xe0000000;
    } else {
        below_4g_mem_size = ram_size;
    }

    linux_boot = (kernel_filename != NULL);

    /* init CPUs */
    if (cpu_model == NULL) {
#ifdef TARGET_X86_64
        cpu_model = "qemu64";
#else
        cpu_model = "qemu32";
#endif
    }
    
    for(i = 0; i < smp_cpus; i++) {
        env = cpu_init(cpu_model);
        if (!env) {
            fprintf(stderr, "Unable to find x86 CPU definition\n");
            exit(1);
        }
#ifndef CONFIG_DM
        if (i != 0)
            env->halted = 1;
        if (smp_cpus > 1) {
            /* XXX: enable it in all cases */
            env->cpuid_features |= CPUID_APIC;
        }
#endif /* !CONFIG_DM */
        qemu_register_reset(main_cpu_reset, env);
        if (pci_enabled) {
            apic_init(env);
        }
    }

    vmport_init();

#ifndef CONFIG_DM
    /* allocate RAM */
    ram_addr = qemu_ram_alloc(0xa0000);
    cpu_register_physical_memory(0, 0xa0000, ram_addr);

    /* Allocate, even though we won't register, so we don't break the
     * phys_ram_base + PA assumption. This range includes vga (0xa0000 - 0xc0000),
     * and some bios areas, which will be registered later
     */
    ram_addr = qemu_ram_alloc(0x100000 - 0xa0000);
    ram_addr = qemu_ram_alloc(below_4g_mem_size - 0x100000);
    cpu_register_physical_memory(0x100000,
                 below_4g_mem_size - 0x100000,
                 ram_addr);

    /* above 4giga memory allocation */
    if (above_4g_mem_size > 0) {
        ram_addr = qemu_ram_alloc(above_4g_mem_size);
        cpu_register_physical_memory(0x100000000ULL,
                                     above_4g_mem_size,
                                     ram_addr);
    }


    /* allocate VGA RAM */
    vga_ram_addr = qemu_ram_alloc(vga_ram_size);

    /* BIOS load */
    if (bios_name == NULL)
        bios_name = BIOS_FILENAME;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, bios_name);
    bios_size = get_image_size(buf);
    if (bios_size <= 0 ||
        (bios_size % 65536) != 0) {
        goto bios_error;
    }
    bios_offset = qemu_ram_alloc(bios_size);
    ret = load_image_targphys(buf, bios_offset, bios_size);
    if (ret != bios_size) {
    bios_error:
        fprintf(stderr, "qemu: could not load PC BIOS '%s'\n", buf);
        exit(1);
    }

    if (cirrus_vga_enabled || std_vga_enabled || vmsvga_enabled) {
        /* VGA BIOS load */
        if (cirrus_vga_enabled) {
            snprintf(buf, sizeof(buf), "%s/%s", bios_dir, VGABIOS_CIRRUS_FILENAME);
        } else {
            snprintf(buf, sizeof(buf), "%s/%s", bios_dir, VGABIOS_FILENAME);
        }
        vga_bios_size = get_image_size(buf);
        if (vga_bios_size <= 0 || vga_bios_size > 65536)
            goto vga_bios_error;
        vga_bios_offset = qemu_ram_alloc(65536);

        ret = load_image_targphys(buf, phys_ram_base + vga_bios_offset);
        if (ret != vga_bios_size) {
vga_bios_error:
            fprintf(stderr, "qemu: could not load VGA BIOS '%s'\n", buf);
            exit(1);
        }

        /* setup basic memory access */
        cpu_register_physical_memory(0xc0000, 0x10000,
                                     vga_bios_offset | IO_MEM_ROM);
    }

    /* map the last 128KB of the BIOS in ISA space */
    isa_bios_size = bios_size;
    if (isa_bios_size > (128 * 1024))
        isa_bios_size = 128 * 1024;
    cpu_register_physical_memory(0x100000 - isa_bios_size,
                                 isa_bios_size,
                                 (bios_offset + bios_size - isa_bios_size) | IO_MEM_ROM);

    {
        ram_addr_t option_rom_offset;
        int size, offset;

        offset = 0;
        if (linux_boot) {
            option_rom_offset = qemu_ram_alloc(TARGET_PAGE_SIZE);
            load_linux(phys_ram_base + option_rom_offset,
                       kernel_filename, initrd_filename, kernel_cmdline);
            cpu_register_physical_memory(0xd0000, TARGET_PAGE_SIZE,
                                         option_rom_offset | IO_MEM_ROM);
            offset = TARGET_PAGE_SIZE;
        }

        for (i = 0; i < nb_option_roms; i++) {
            size = get_image_size(option_rom[i]);
            if (size < 0) {
                fprintf(stderr, "Could not load option rom '%s'\n",
                        option_rom[i]);
                exit(1);
            }
            if (size > (0x10000 - offset))
                goto option_rom_error;
            option_rom_offset = qemu_ram_alloc(size);
            ret = load_image_targphys(option_rom[i], option_rom_offset, size);
            if (ret != size) {
            option_rom_error:
                fprintf(stderr, "Too many option ROMS\n");
                exit(1);
            }
            size = (size + 4095) & ~4095;
            cpu_register_physical_memory(0xd0000 + offset,
                                         size, option_rom_offset | IO_MEM_ROM);
            offset += size;
        }
    }

    /* map all the bios at the top of memory */
    cpu_register_physical_memory((uint32_t)(-bios_size),
                                 bios_size, bios_offset | IO_MEM_ROM);
#else
    vga_ram_addr = 0; /* this is not supposed to be used */
#endif /* !CONFIG_DM */

    bochs_bios_init();

    cpu_irq = qemu_allocate_irqs(pic_irq_request, NULL, 1);
    i8259 = i8259_init(cpu_irq[0]);
    ferr_irq = i8259[13];

    if (pci_enabled) {
        pci_bus = i440fx_init(&i440fx_state, i8259);
        piix3_devfn = piix3_init(pci_bus, -1);
    } else {
        pci_bus = NULL;
    }

#ifdef CONFIG_PASSTHROUGH
    intel_pch_init(pci_bus);
#endif

    /* init basic PC hardware */
    register_ioport_write(0x80, 1, 1, ioport80_write, NULL);

    register_ioport_write(0xf0, 1, 1, ioportF0_write, NULL);

    if (cirrus_vga_enabled) {
        if (pci_enabled) {
            pci_cirrus_vga_init(pci_bus,
                                phys_ram_base + vga_ram_addr,
                                vga_ram_addr, vga_ram_size);
        } else {
            isa_cirrus_vga_init(phys_ram_base + vga_ram_addr,
                                vga_ram_addr, vga_ram_size);
        }
#ifndef CONFIG_DM
    } else if (vmsvga_enabled) {
        if (pci_enabled)
            pci_vmsvga_init(pci_bus, phys_ram_base + vga_ram_addr,
                            vga_ram_addr, vga_ram_size);
        else
            fprintf(stderr, "%s: vmware_vga: no PCI bus\n", __FUNCTION__);
#endif
    } else if (std_vga_enabled) {
        if (pci_enabled) {
            pci_vga_init(pci_bus, phys_ram_base + vga_ram_addr,
                         vga_ram_addr, vga_ram_size, 0, 0);
        } else {
            isa_vga_init(phys_ram_base + vga_ram_addr,
                         vga_ram_addr, vga_ram_size);
        }
    }

#ifdef CONFIG_PASSTHROUGH
    /* Pass-through Initialization
     * init libpci even direct_pci is null, as can hotplug a dev runtime
     */
    if ( pci_enabled )
    {
        rc = pt_init(pci_bus);
        if ( rc < 0 )
        {
            fprintf(logfile, "Error: Initialization failed for pass-through devices\n");
            exit(1);
        }
    }
#endif

    rtc_state = rtc_init(0x70, i8259[8], 2000);

    qemu_register_boot_set(pc_boot_set, rtc_state);

    register_ioport_read(0x92, 1, 1, ioport92_read, NULL);
    register_ioport_write(0x92, 1, 1, ioport92_write, NULL);

#ifndef CONFIG_DM
    if (pci_enabled) {
        ioapic = ioapic_init();
    }
    pit = pit_init(0x40, i8259[0]);
    pcspk_init(pit);
    if (!no_hpet) {
        hpet_init(i8259);
    }
    if (pci_enabled) {
        pic_set_alt_irq_func(isa_pic, ioapic_set_irq, ioapic);
    }
#endif /* !CONFIG_DM */
    if (pci_enabled) {
        disable_pf = xenstore_parse_disable_pf_config();
        if (disable_pf != 1) {
            pci_xen_platform_init(pci_bus);
            platform_fixed_ioport_init();
        }
    }

    for(i = 0; i < MAX_SERIAL_PORTS; i++) {
        if (serial_hds[i]) {
            serial_init(serial_io[i], i8259[serial_irq[i]], 115200,
                        serial_hds[i]);
        }
    }

    for(i = 0; i < MAX_PARALLEL_PORTS; i++) {
        if (parallel_hds[i]) {
            parallel_init(parallel_io[i], i8259[parallel_irq[i]],
                          parallel_hds[i]);
        }
    }

    for(i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];

        if (!pci_enabled || (nd->model && strcmp(nd->model, "ne2k_isa") == 0))
            pc_init_ne2k_isa(nd, i8259);
        else
            pci_nic_init(pci_bus, nd, -1, "ne2k_pci");
    }

    qemu_system_hot_add_init();

    if (drive_get_max_bus(IF_IDE) >= MAX_IDE_BUS) {
        fprintf(stderr, "qemu: too many IDE bus\n");
        exit(1);
    }

    for(i = 0; i < MAX_IDE_BUS * MAX_IDE_DEVS; i++) {
        index = drive_get_index(IF_IDE, i / MAX_IDE_DEVS, i % MAX_IDE_DEVS);
	if (index != -1)
	    hd[i] = drives_table[index].bdrv;
	else
	    hd[i] = NULL;
    }

    if (pci_enabled) {
        pci_piix3_ide_init(pci_bus, hd, piix3_devfn + 1, i8259);
    } else {
        for(i = 0; i < MAX_IDE_BUS; i++) {
            isa_ide_init(ide_iobase[i], ide_iobase2[i], i8259[ide_irq[i]],
	                 hd[MAX_IDE_DEVS * i], hd[MAX_IDE_DEVS * i + 1]);
        }
    }

#ifdef HAS_TPM
    if (has_tpm_device_danger())
        tpm_tis_init(&i8259[11]);
#endif

    i8042_init(i8259[1], i8259[12], 0x60);
    DMA_init(0);
#ifdef HAS_AUDIO
    audio_init(pci_enabled ? pci_bus : NULL, i8259);
#endif

    for(i = 0; i < MAX_FD; i++) {
        index = drive_get_index(IF_FLOPPY, 0, i);
	if (index != -1)
	    fd[i] = drives_table[index].bdrv;
	else
	    fd[i] = NULL;
    }
    floppy_controller = fdctrl_init(i8259[6], 2, 0, 0x3f0, fd);

    cmos_init(below_4g_mem_size, above_4g_mem_size, boot_device, hd);

    if (pci_enabled && usb_enabled) {
        usb_uhci_piix3_init(pci_bus, piix3_devfn + 2);
    }

    if (pci_enabled && acpi_enabled) {
        uint8_t *eeprom_buf = qemu_mallocz(8 * 256); /* XXX: make this persistent */
        i2c_bus *smbus;

        /* TODO: Populate SPD eeprom data.  */
        smbus = piix4_pm_init(pci_bus, piix3_devfn + 3, 0xb100, i8259[9]);

	if (smbus) {
	    for (i = 0; i < 8; i++) {
		smbus_eeprom_device_init(smbus, 0x50 + i, eeprom_buf + (i * 256));
	    }
        }
    }

    if (i440fx_state) {
        i440fx_init_memory_mappings(i440fx_state);
    }

    if (pci_enabled) {
	int max_bus;
        int bus, unit;
        void *scsi;

        max_bus = drive_get_max_bus(IF_SCSI);

	for (bus = 0; bus <= max_bus; bus++) {
            scsi = lsi_scsi_init(pci_bus, -1);
            for (unit = 0; unit < LSI_MAX_DEVS; unit++) {
	        index = drive_get_index(IF_SCSI, bus, unit);
		if (index == -1)
		    continue;
		lsi_scsi_attach(scsi, drives_table[index].bdrv, unit);
	    }
        }
    }

    if (pci_enabled) {
        PCI_EMULATION_INFO *p;
        for (p = PciEmulationInfoHead; p != NULL; p = p->next) {
            pci_emulation_init(pci_bus, p);
        }
    }

#ifndef CONFIG_DM
    /* Add virtio block devices */
    if (pci_enabled) {
        int index;
        int unit_id = 0;

        while ((index = drive_get_index(IF_VIRTIO, 0, unit_id)) != -1) {
            virtio_blk_init(pci_bus, drives_table[index].bdrv);
            unit_id++;
        }
    }

    /* Add virtio balloon device */
    if (pci_enabled)
        virtio_balloon_init(pci_bus);

    /* Add virtio console devices */
    if (pci_enabled) {
        for(i = 0; i < MAX_VIRTIO_CONSOLES; i++) {
            if (virtcon_hds[i])
                virtio_console_init(pci_bus, virtcon_hds[i]);
        }
    }
#endif
}

static void pc_init_pci(ram_addr_t ram_size, int vga_ram_size,
                        const char *boot_device,
                        const char *kernel_filename,
                        const char *kernel_cmdline,
                        const char *initrd_filename,
                        const char *cpu_model,
                        const char *direct_pci)
{
    pc_init1(ram_size, vga_ram_size, boot_device,
             kernel_filename, kernel_cmdline,
             initrd_filename, 1, cpu_model);
}

static void pc_init_isa(ram_addr_t ram_size, int vga_ram_size,
                        const char *boot_device,
                        const char *kernel_filename,
                        const char *kernel_cmdline,
                        const char *initrd_filename,
                        const char *cpu_model,
                        const char *direct_pci)
{
    pc_init1(ram_size, vga_ram_size, boot_device,
             kernel_filename, kernel_cmdline,
             initrd_filename, 0, cpu_model);
}

/* set CMOS shutdown status register (index 0xF) as S3_resume(0xFE)
   BIOS will read it and start S3 resume at POST Entry*/
void cmos_set_s3_resume(void)
{
    if (rtc_state)
        rtc_set_memory(rtc_state, 0xF, 0xFE);
}

QEMUMachine pc_machine = {
    .name = "pc",
    .desc = "Standard PC",
    .init = pc_init_pci,
    .ram_require = PC_MAX_BIOS_SIZE,
    .max_cpus = 255,
};

QEMUMachine isapc_machine = {
    .name = "isapc",
    .desc = "ISA-only PC",
    .init = pc_init_isa,
    .ram_require = PC_MAX_BIOS_SIZE,
    .max_cpus = 1,
};




/*
 * Evil helper for non-relocatable kernels
 *
 * So it works out like this:
 *
 *  0x100000  - Xen HVM firmware lives here. Kernel wants to boot here
 *
 * You can't both live there and HVM firmware is needed first, thus
 * our plan is
 *
 *  0x200000              - kernel is loaded here by QEMU
 *  0x200000+kernel_size  - helper code is put here by QEMU
 *
 * code32_switch in kernel header is set to point at out helper
 * code at 0x200000+kernel_size
 *
 * Our helper basically does memmove(0x100000,0x200000,kernel_size)
 * and then jmps to  0x1000000.
 *
 * So we've overwritten the HVM firmware (which was no longer
 * needed) and the non-relocatable kernel can happily boot
 * at its usual address.
 *
 * Simple, eh ?
 *
 * Well the assembler needed to do this is fairly short:
 *
 *  # Load segments
 *    cld                         
 *    cli                         
 *    movl $0x18,%eax
 *    mov %ax,%ds                 
 *    mov %ax,%es                 
 *    mov %ax,%fs                 
 *    mov %ax,%gs                 
 *    mov %ax,%ss                 
 *
 *  # Move the kernel into position
 *    xor    %edx,%edx            
 *_doloop:                        
 *    movzbl 0x600000(%edx),%eax  
 *    mov    %al,0x100000(%edx)   
 *    add    $0x1,%edx            
 *    cmp    $0x500000,%edx       
 *    jne    _doloop              
 *
 *  # start kernel
 *    xorl %ebx,%ebx              
 *    mov    $0x100000,%ecx       
 *    jmp    *%ecx                
 *
 */
static void setup_relocator(target_phys_addr_t addr, target_phys_addr_t src, target_phys_addr_t dst, size_t len)
{
  /* Now this assembler corresponds to follow machine code, with our args from QEMU spliced in :-) */
  unsigned char buf[] = {
    /* Load segments */
    0xfc,                         /* cld               */
    0xfa,                         /* cli               */ 
    0xb8, 0x18, 0x00, 0x00, 0x00, /* mov    $0x18,%eax */
    0x8e, 0xd8,                   /* mov    %eax,%ds   */
    0x8e, 0xc0,                   /* mov    %eax,%es   */
    0x8e, 0xe0,                   /* mov    %eax,%fs   */
    0x8e, 0xe8,                   /* mov    %eax,%gs   */
    0x8e, 0xd0,                   /* mov    %eax,%ss   */
    0x31, 0xd2,                   /* xor    %edx,%edx  */
  
    /* Move the kernel into position */
    0x0f, 0xb6, 0x82, (src&0xff), ((src>>8)&0xff), ((src>>16)&0xff), ((src>>24)&0xff), /*   movzbl $src(%edx),%eax */
    0x88, 0x82, (dst&0xff), ((dst>>8)&0xff), ((dst>>16)&0xff), ((dst>>24)&0xff),       /*   mov    %al,$dst(%edx)  */
    0x83, 0xc2, 0x01,                                                                  /*   add    $0x1,%edx       */
    0x81, 0xfa, (len&0xff), ((len>>8)&0xff), ((len>>16)&0xff), ((len>>24)&0xff),       /*   cmp    $len,%edx       */
    0x75, 0xe8,                                                                        /*   jne    13 <_doloop>    */

    /* Start kernel */
    0x31, 0xdb,                                                                        /*   xor    %ebx,%ebx       */
    0xb9, (dst&0xff), ((dst>>8)&0xff), ((dst>>16)&0xff), ((dst>>24)&0xff),             /*   mov    $dst,%ecx  */
    0xff, 0xe1,                                                                        /*   jmp    *%ecx           */
  };
  cpu_physical_memory_rw(addr, buf, sizeof(buf), 1);
  fprintf(stderr, "qemu: helper at 0x%x of size %d bytes, to move kernel of %d bytes from 0x%x to 0x%x\n",
	  (int)addr, (int)sizeof(buf), (int)len, (int)src, (int)dst);
}


static void xen_relocator_hook(target_phys_addr_t *prot_addr_upd,
                               uint16_t protocol,
			       const uint8_t header[], int kernel_size,
			       target_phys_addr_t real_addr, int real_size)
{
    target_phys_addr_t prot_addr = *prot_addr_upd;

    /* Urgh, Xen's HVM firmware lives at 0x100000, but that's also the
     * address Linux wants to start life at prior to relocatable support
     */
    fprintf(stderr, "checking need for relocation, header protocol: %x\n",
	    protocol);

    if (prot_addr != 0x10000) { /* old low kernels are OK */
	target_phys_addr_t reloc_prot_addr = 0x200000;
	
        if (protocol >= 0x205 && (header[0x234] & 1)) {
	    /* Relocatable automatically */
	    stl_phys(real_addr+0x214, reloc_prot_addr);
	    fprintf(stderr, "qemu: kernel is relocatable\n");
	} else {
	    /* Setup a helper which moves  kernel back to
	     * its expected addr after firmware has got out
	     * of the way. We put a helper at  reloc_prot_addr+kernel_size.
	     * It moves kernel from reloc_prot_addr to prot_addr and
	     * then jumps to prot_addr. Yes this is sick.
	     */
	    fprintf(stderr, "qemu: kernel is NOT relocatable\n");
	    stl_phys(real_addr+0x214, reloc_prot_addr + kernel_size);
	    setup_relocator(reloc_prot_addr + kernel_size, reloc_prot_addr, prot_addr, kernel_size);
	}
	fprintf(stderr, "qemu: loading kernel protected mode (%x bytes) at %#zx\n",
		kernel_size, (size_t)reloc_prot_addr);
	fprintf(stderr, "qemu: loading kernel real mode (%x bytes) at %#zx\n",
		real_size, (size_t)real_addr);
        *prot_addr_upd = reloc_prot_addr;
    }
}

#ifdef CONFIG_DM

void vmport_init(void) { }
int apic_init(CPUX86State *env) { return 0; }

#endif /* CONFIG_DM */
