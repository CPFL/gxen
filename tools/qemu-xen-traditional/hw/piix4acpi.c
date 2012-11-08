 /*
 * PIIX4 ACPI controller emulation
 *
 * Winston liwen Wang, winston.l.wang@intel.com
 * Copyright (c) 2006 , Intel Corporation.
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
#include "pci.h"
#include "sysemu.h"
#include "qemu-xen.h"
#include "battery_mgmt.h"
#include "qemu-log.h"

#include <xen/hvm/ioreq.h>
#include <xen/hvm/params.h>

#define PIIX4ACPI_LOG_ERROR 0
#define PIIX4ACPI_LOG_INFO 1
#define PIIX4ACPI_LOG_DEBUG 2
#define PIIX4ACPI_LOGLEVEL PIIX4ACPI_LOG_INFO
#define PIIX4ACPI_LOG(level, fmt, ...) do { if (level <= PIIX4ACPI_LOGLEVEL) qemu_log(fmt, ## __VA_ARGS__); } while (0)

#ifdef CONFIG_PASSTHROUGH
#include <pci/header.h>
#endif

/* PM1a_CNT bits, as defined in the ACPI specification. */
#define SCI_EN            (1 <<  0)
#define GBL_RLS           (1 <<  2)
#define SLP_TYP_Sx        (7 << 10)
#define SLP_EN            (1 << 13)

/* Sleep state type codes as defined by the \_Sx objects in the DSDT. */
/* These must be kept in sync with the DSDT (hvmloader/acpi/dsdt.asl) */
#define SLP_TYP_S4_V0     (6 << 10)
#define SLP_TYP_S3_V0     (5 << 10)
#define SLP_TYP_S5_V0     (7 << 10)
#define SLP_TYP_S4_V1     (0 << 10)
#define SLP_TYP_S3_V1     (1 << 10)
#define SLP_TYP_S5_V1     (0 << 10)

#define ACPI_DBG_IO_ADDR  0xb044
#define ACPI_PHP_IO_ADDR  0x10c0

#define PHP_EVT_ADD     0x0
#define PHP_EVT_REMOVE  0x3

/* The bit in GPE0_STS/EN to notify the pci hotplug event */
#define ACPI_PHP_GPE_BIT 3

#define NR_PHP_SLOT_REG (NR_PCI_DEVFN/2)
#define DEVFN_TO_PHP_SLOT_REG(devfn) (devfn >> 1)
#define PHP_SLOT_REG_TO_DEVFN(reg, hilo) ((reg << 1) | hilo)

/* ioport to monitor cpu add/remove status */
#define PROC_BASE 0xaf00

typedef struct PCIAcpiState {
    PCIDevice dev;
    uint16_t pm1_control; /* pm1a_ECNT_BLK */

    uint32_t pm1a_evt_blk_address;
} PCIAcpiState;

typedef struct GPEState {
    /* GPE0 block */
    uint8_t gpe0_sts[ACPI_GPE0_BLK_LEN_V0 / 2];
    uint8_t gpe0_en[ACPI_GPE0_BLK_LEN_V0 / 2];

    /* CPU bitmap */
    uint8_t cpus_sts[32];

    /* SCI IRQ level */
    uint8_t sci_asserted;

    uint32_t gpe0_blk_address;
    uint32_t gpe0_blk_half_len;
} GPEState;

static GPEState gpe_state;

typedef struct PHPDevFn {
    uint8_t status[NR_PHP_SLOT_REG]; /* Apaptor n stats | Adaptor n+1 status */
    uint8_t plug_evt;                /* PHP_EVT_ADD or PHP_EVT_REMOVE
                                      * PSTA in ASL */
    uint8_t plug_devfn;              /* DevFn number
                                      * PSTB in ASL */
} PHPDevFn;

static void acpi_map(PCIDevice *pci_dev, int region_num,
                     uint32_t addr, uint32_t size, int type);

static PHPDevFn php_devfn;
int s3_shutdown_flag;
static qemu_irq sci_irq;

static void php_reg_set(PHPDevFn *hotplug_devfn, int devfn, uint8_t val)
{
    uint8_t *reg = &(hotplug_devfn->status[DEVFN_TO_PHP_SLOT_REG(devfn)]);

    /* Value may only use a nibble */
    val &= 0xf;

    if (devfn & 0x1)
        *reg = (*reg & 0x0f) | (val << 4);
    else
        *reg = (*reg & 0xf0) | val;
}


static uint8_t php_reg_get(PHPDevFn *hotplug_devfn, int devfn)
{
    uint8_t reg = hotplug_devfn->status[DEVFN_TO_PHP_SLOT_REG(devfn)];
    uint8_t val;

    if (devfn & 0x1)
        val = (reg & 0xf0) >> 4;
    else
        val = reg & 0x0f;

    return val;
}

typedef struct AcpiDeviceState AcpiDeviceState;
AcpiDeviceState *acpi_device_table;
static void piix4acpi_save(QEMUFile *f, void *opaque)
{
    PCIAcpiState *s = opaque;
    pci_device_save(&s->dev, f);
    qemu_put_be16s(f, &s->pm1_control);
    qemu_put_be32s(f, &s->pm1a_evt_blk_address);
}

static int piix4acpi_load(QEMUFile *f, void *opaque, int version_id)
{
    PCIAcpiState *s = opaque;
    int ret;
    uint32_t pm1a_evt_address_assigned;

    if (version_id > 2)
        return -EINVAL;
    ret = pci_device_load(&s->dev, f);
    if (ret < 0)
        return ret;
    qemu_get_be16s(f, &s->pm1_control);

    pm1a_evt_address_assigned = s->pm1a_evt_blk_address;
    if (version_id <= 1) {
        /* map to old ioport instead of the new one */
        s->pm1a_evt_blk_address = ACPI_PM1A_EVT_BLK_ADDRESS_V0;
    } else {
        qemu_get_be32s(f, &s->pm1a_evt_blk_address);
    }

    if (s->pm1a_evt_blk_address != pm1a_evt_address_assigned) {
        PIIX4ACPI_LOG(PIIX4ACPI_LOG_DEBUG, "ACPI: Change firmware IOPorts mapping.\n");
        /* unmap new ioport to use old ioport */
        isa_unassign_ioport(pm1a_evt_address_assigned + 4, 2);
        acpi_map((PCIDevice *)s, 0, s->pm1a_evt_blk_address, 0x10, PCI_ADDRESS_SPACE_IO);
    }
    return 0;
}

static void acpiPm1Control_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    PCIAcpiState *s = opaque;
    s->pm1_control = (s->pm1_control & 0xff00) | (val & 0xff);
}

static uint32_t acpiPm1Control_readb(void *opaque, uint32_t addr)
{
    PCIAcpiState *s = opaque;
    /* Mask out the write-only bits */
    return (uint8_t)(s->pm1_control & ~(GBL_RLS|SLP_EN));
}

static void acpi_shutdown(uint32_t val)
{
    if (!(val & SLP_EN))
        return;

    switch (val & SLP_TYP_Sx) {
    case SLP_TYP_S3_V0:
    case SLP_TYP_S3_V1:
        s3_shutdown_flag = 1;
        qemu_system_reset();
        s3_shutdown_flag = 0;
        cmos_set_s3_resume();
        xc_set_hvm_param(xc_handle, domid, HVM_PARAM_ACPI_S_STATE, 3);
        break;
    case SLP_TYP_S4_V0:
    case SLP_TYP_S5_V0:
    case SLP_TYP_S5_V1:
        qemu_system_shutdown_request();
        break;
    default:
        break;
    }
}

static void acpiPm1ControlP1_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    PCIAcpiState *s = opaque;

    val <<= 8;
    s->pm1_control = ((s->pm1_control & 0xff) | val) & ~SLP_EN;

    acpi_shutdown(val);
}

static uint32_t acpiPm1ControlP1_readb(void *opaque, uint32_t addr)
{
    PCIAcpiState *s = opaque;
    /* Mask out the write-only bits */
    return (uint8_t)((s->pm1_control & ~(GBL_RLS|SLP_EN)) >> 8);
}

static void acpiPm1Control_writew(void *opaque, uint32_t addr, uint32_t val)
{
    PCIAcpiState *s = opaque;

    s->pm1_control = val & ~SLP_EN;

    acpi_shutdown(val);
}

static uint32_t acpiPm1Control_readw(void *opaque, uint32_t addr)
{
    PCIAcpiState *s = opaque;
    /* Mask out the write-only bits */
    return (s->pm1_control & ~(GBL_RLS|SLP_EN));
}

static void acpi_map(PCIDevice *pci_dev, int region_num,
                     uint32_t addr, uint32_t size, int type)
{
    PCIAcpiState *d = (PCIAcpiState *)pci_dev;

    /* Byte access */
    register_ioport_write(addr + 4, 1, 1, acpiPm1Control_writeb, d);
    register_ioport_read(addr + 4, 1, 1, acpiPm1Control_readb, d);
    register_ioport_write(addr + 4 + 1, 1, 1, acpiPm1ControlP1_writeb, d);
    register_ioport_read(addr + 4 +1, 1, 1, acpiPm1ControlP1_readb, d);

    /* Word access */
    register_ioport_write(addr + 4, 2, 2, acpiPm1Control_writew, d);
    register_ioport_read(addr + 4, 2, 2, acpiPm1Control_readw, d);

    battery_mgmt_init(pci_dev);
}

static inline int test_bit(uint8_t *map, int bit)
{
    return ( map[bit / 8] & (1 << (bit % 8)) );
}

static inline void set_bit(uint8_t *map, int bit)
{
    map[bit / 8] |= (1 << (bit % 8));
}

static inline void clear_bit(uint8_t *map, int bit)
{
    map[bit / 8] &= ~(1 << (bit % 8));
}

static void acpi_dbg_writel(void *opaque, uint32_t addr, uint32_t val)
{
    PIIX4ACPI_LOG(PIIX4ACPI_LOG_DEBUG, "ACPI: DBG: 0x%08x\n", val);
    PIIX4ACPI_LOG(PIIX4ACPI_LOG_INFO, "ACPI:debug: write addr=0x%x, val=0x%x.\n", addr, val);
}

#ifdef CONFIG_PASSTHROUGH

/*
 * simple PCI hotplug controller IO
 * ACPI_PHP_IO_ADDR + :
 * 0 - the hotplug event
 * 1 - the devfn that has a hotplug event
 * 2 - 1st php devfn ctr/sts reg|2nd php devfn ctr/sts reg
 * 3 - 3rd php devfn ctr/sts reg|4th php devfn ctr/sts reg
 * ...
 */
static uint32_t acpi_php_readb(void *opaque, uint32_t addr)
{
    PHPDevFn *hotplug_devfn = opaque;
    int num;
    uint32_t val; 

    switch (addr)
    {
    case ACPI_PHP_IO_ADDR:
        val = hotplug_devfn->plug_evt;
        break;
    case ACPI_PHP_IO_ADDR + 1:
        val = hotplug_devfn->plug_devfn;
        break;
    default:
        num = addr - ACPI_PHP_IO_ADDR - 2;
        val = hotplug_devfn->status[num];
    }

    PIIX4ACPI_LOG(PIIX4ACPI_LOG_DEBUG,
		     "ACPI PCI hotplug: read addr=0x%x, val=0x%02x.\n",
            addr, val);

    return val;
}

static void acpi_php_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    PHPDevFn *hotplug_devfn = opaque;
    int devfn, num, func, i;

    PIIX4ACPI_LOG(PIIX4ACPI_LOG_DEBUG,
		     "ACPI PCI hotplug: write addr=0x%x, val=0x%02x.\n",
            addr, val);

    switch (addr)
    {
    case ACPI_PHP_IO_ADDR:
    case ACPI_PHP_IO_ADDR + 1:
        break;
    default:
        num = addr - ACPI_PHP_IO_ADDR - 2;
        if ( val == 0x01 || val == 0x10 ) { /* Eject command */
            if ( val == 0x01 )
                devfn = PHP_SLOT_REG_TO_DEVFN(num, 0);
            else
                devfn = PHP_SLOT_REG_TO_DEVFN(num, 1);

            fprintf(logfile, "ACPI PCI hotplug: write devfn=0x%02x.\n",
                    devfn);

            if (hotplug_devfn->plug_evt != PHP_EVT_REMOVE ||
                hotplug_devfn->plug_devfn!= devfn )
            {
                fprintf(logfile, "ACPI PCI hotplug: not expecting "
                        "devfn 0x%02x to be removed. Expected event 0x%x "
                        "for devfn 0x%02x\n", devfn,
                        hotplug_devfn->plug_evt, hotplug_devfn->plug_devfn);
                return;
            }

            /* clear the hotplug event */
            hotplug_devfn->plug_evt = 0;

            for ( func = NR_PCI_FUNC - 1; func >= 0; func-- )
            {
                i = PCI_DEVFN(PCI_SLOT(devfn), func);

                /* make _STA of the devfn 0 */
                php_reg_set(hotplug_devfn, i, 0);

                /* power off the slot */
                power_off_php_devfn(i);
            }

            /* signal the CP ACPI hot remove done. */
            xenstore_record_dm_state("pci-removed");
        }
    }
}

static void pci_devfn_save(QEMUFile* f, void* opaque)
{
    PHPDevFn *hotplug_devfn = opaque;
    int i;
    for ( i = 0; i < NR_PHP_SLOT_REG; i++ ) {
        qemu_put_8s( f, &hotplug_devfn->status[i]);
    }
    qemu_put_8s(f, &hotplug_devfn->plug_evt);
    qemu_put_8s(f, &hotplug_devfn->plug_devfn);
}

static int pci_devfn_load(QEMUFile* f, void* opaque, int version_id)
{
    PHPDevFn *hotplug_devfn = opaque;
    int i;
    if (version_id != 1)
        return -EINVAL;
    for ( i = 0; i < NR_PHP_SLOT_REG; i++ ) {
        qemu_get_8s( f, &hotplug_devfn->status[i]);
    }
    qemu_get_8s(f, &hotplug_devfn->plug_evt);
    qemu_get_8s(f, &hotplug_devfn->plug_devfn);
    return 0;
}

static void php_devfn_init(void)
{
    int i;
    memset(&php_devfn, 0, sizeof(PHPDevFn));

    /* update the pci devfn status */
    for ( i = 0; i < NR_PCI_DEVFN; i++ ) {
        if ( test_pci_devfn(i) )
            php_reg_set(&php_devfn, i, 0xf);
    }

    /* ACPI PCI hotplug controller */
    register_ioport_read(ACPI_PHP_IO_ADDR, NR_PHP_SLOT_REG + 2, 1,
                         acpi_php_readb, &php_devfn);
    register_ioport_write(ACPI_PHP_IO_ADDR, NR_PHP_SLOT_REG + 2, 1,
                          acpi_php_writeb, &php_devfn);
    register_savevm("pci_devfn", 0, 1, pci_devfn_save, pci_devfn_load,
                    &php_devfn);
}
#endif /* CONFIG_PASSTHROUGH */

/* GPEx_STS occupy 1st half of the block, while GPEx_EN 2nd half */
static uint32_t gpe_sts_read(void *opaque, uint32_t addr)
{
    GPEState *s = opaque;

    return s->gpe0_sts[addr - s->gpe0_blk_address];
}

/* write 1 to clear specific GPE bits */
static void gpe_sts_write(void *opaque, uint32_t addr, uint32_t val)
{
    GPEState *s = opaque;
    int hotplugged = 0;

    PIIX4ACPI_LOG(PIIX4ACPI_LOG_DEBUG, "gpe_sts_write: addr=0x%x, val=0x%x.\n", addr, val);

    hotplugged = test_bit(&s->gpe0_sts[0], ACPI_PHP_GPE_BIT);
    s->gpe0_sts[addr - s->gpe0_blk_address] &= ~val;
    if ( s->sci_asserted &&
         hotplugged &&
         !test_bit(&s->gpe0_sts[0], ACPI_PHP_GPE_BIT)) {
        PIIX4ACPI_LOG(PIIX4ACPI_LOG_INFO, "Clear the GPE0_STS bit for ACPI hotplug & deassert the IRQ.\n");
        qemu_irq_lower(sci_irq);
    }

}

static uint32_t gpe_en_read(void *opaque, uint32_t addr)
{
    GPEState *s = opaque;

    return s->gpe0_en[addr - (s->gpe0_blk_address + s->gpe0_blk_half_len)];
}

/* write 0 to clear en bit */
static void gpe_en_write(void *opaque, uint32_t addr, uint32_t val)
{
    GPEState *s = opaque;
    int reg_count;

    PIIX4ACPI_LOG(PIIX4ACPI_LOG_DEBUG, "gpe_en_write: addr=0x%x, val=0x%x.\n", addr, val);
    reg_count = addr - (s->gpe0_blk_address + s->gpe0_blk_half_len);
    s->gpe0_en[reg_count] = val;
    /* If disable GPE bit right after generating SCI on it, 
     * need deassert the intr to avoid redundant intrs
     */
    if ( s->sci_asserted &&
         reg_count == (ACPI_PHP_GPE_BIT / 8) &&
         !(val & (1 << (ACPI_PHP_GPE_BIT % 8))) ) {
        PIIX4ACPI_LOG(PIIX4ACPI_LOG_INFO, "deassert due to disable GPE bit.\n");
        s->sci_asserted = 0;
        qemu_irq_lower(sci_irq);
    }

}

static void gpe_save(QEMUFile* f, void* opaque)
{
    GPEState *s = (GPEState*)opaque;
    int i;

    for ( i = 0; i < ACPI_GPE0_BLK_LEN_V0 / 2; i++ ) {
        qemu_put_8s(f, &s->gpe0_sts[i]);
        qemu_put_8s(f, &s->gpe0_en[i]);
    }

    qemu_put_8s(f, &s->sci_asserted);
    if ( s->sci_asserted ) {
        PIIX4ACPI_LOG(PIIX4ACPI_LOG_INFO, "gpe_save with sci asserted!\n");
    }

    qemu_put_be32s(f, &s->gpe0_blk_address);
    qemu_put_be32s(f, &s->gpe0_blk_half_len);
}

static int gpe_load(QEMUFile* f, void* opaque, int version_id)
{
    GPEState *s = (GPEState*)opaque;
    int i;
    uint32_t gpe0_addr_assigned;
    uint32_t gpe0_half_len_assigned;

    if (version_id > 2)
        return -EINVAL;

    for ( i = 0; i < ACPI_GPE0_BLK_LEN_V0 / 2; i++ ) {
        qemu_get_8s(f, &s->gpe0_sts[i]);
        qemu_get_8s(f, &s->gpe0_en[i]);
    }

    qemu_get_8s(f, &s->sci_asserted);

    gpe0_addr_assigned = s->gpe0_blk_address;
    gpe0_half_len_assigned = s->gpe0_blk_half_len;

    if (version_id <= 1) {
        s->gpe0_blk_address = ACPI_GPE0_BLK_ADDRESS_V0;
        s->gpe0_blk_half_len = ACPI_GPE0_BLK_LEN_V0 / 2;
    } else {
        qemu_get_be32s(f, &s->gpe0_blk_address);
        qemu_get_be32s(f, &s->gpe0_blk_half_len);
    }

    if (gpe0_addr_assigned != s->gpe0_blk_address ||
        gpe0_half_len_assigned != s->gpe0_blk_half_len) {
        isa_unassign_ioport(gpe0_addr_assigned, gpe0_half_len_assigned * 2);

        register_ioport_read(s->gpe0_blk_address, s->gpe0_blk_half_len,
                             1, gpe_sts_read, s);
        register_ioport_read(s->gpe0_blk_address + s->gpe0_blk_half_len,
                             s->gpe0_blk_half_len, 1, gpe_en_read, s);

        register_ioport_write(s->gpe0_blk_address, s->gpe0_blk_half_len,
                              1, gpe_sts_write, s);
        register_ioport_write(s->gpe0_blk_address + s->gpe0_blk_half_len,
                              s->gpe0_blk_half_len, 1, gpe_en_write, s);
    }

    return 0;
}

static uint32_t gpe_cpus_readb(void *opaque, uint32_t addr)
{
    uint32_t val = 0;
    GPEState *g = opaque;

    switch (addr) {
        case PROC_BASE ... PROC_BASE+31:
            val = g->cpus_sts[addr - PROC_BASE];
        default:
            break;
    }

    return val;
}

static void gpe_cpus_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    GPEState *g = opaque;
    switch (addr) {
        case PROC_BASE ... PROC_BASE + 31:
            /* don't allow to change cpus_sts from inside a guest */
            break;
        default:
            break;
    }
}

static void gpe_acpi_init(void)
{
    GPEState *s = &gpe_state;
    memset(s, 0, sizeof(GPEState));
    int i = 0, cpus = vcpus;
    char *vcpumap = (char *)vcpu_avail;

    while (cpus > 0) {
        s->cpus_sts[i] = vcpumap[i];
        i++;
        cpus -= 8;
    }

    register_ioport_read(PROC_BASE, 32, 1,  gpe_cpus_readb, s);
    register_ioport_write(PROC_BASE, 32, 1, gpe_cpus_writeb, s);

    s->gpe0_blk_address = ACPI_GPE0_BLK_ADDRESS_V1;
    s->gpe0_blk_half_len = ACPI_GPE0_BLK_LEN_V1 / 2;

    register_ioport_read(s->gpe0_blk_address,
                         s->gpe0_blk_half_len,
                         1,
                         gpe_sts_read,
                         s);
    register_ioport_read(s->gpe0_blk_address + s->gpe0_blk_half_len,
                         s->gpe0_blk_half_len,
                         1,
                         gpe_en_read,
                         s);

    register_ioport_write(s->gpe0_blk_address,
                          s->gpe0_blk_half_len,
                          1,
                          gpe_sts_write,
                          s);
    register_ioport_write(s->gpe0_blk_address + s->gpe0_blk_half_len,
                          s->gpe0_blk_half_len,
                          1,
                          gpe_en_write,
                          s);

    register_savevm("gpe", 0, 2, gpe_save, gpe_load, s);
}

#ifdef CONFIG_PASSTHROUGH

static void acpi_sci_intr(GPEState *s)
{
    if ( !test_bit(&s->gpe0_sts[0], ACPI_PHP_GPE_BIT) &&
         test_bit(&s->gpe0_en[0], ACPI_PHP_GPE_BIT) ) {

        set_bit(&s->gpe0_sts[0], ACPI_PHP_GPE_BIT);
        s->sci_asserted = 1;
        qemu_irq_raise(sci_irq);
        PIIX4ACPI_LOG(PIIX4ACPI_LOG_INFO, "generate a sci for PHP.\n");
    }
}

void acpi_php_del(int devfn)
{
    GPEState *s = &gpe_state;
    int slot, func;

    slot = PCI_SLOT(devfn);
    func = PCI_FUNC(devfn);

    if ( test_pci_devfn(devfn) < 0 ) {
        PIIX4ACPI_LOG(PIIX4ACPI_LOG_ERROR,
		"hot remove: pci slot 0x%02x, function 0x%x "
                "is not used by a hotplug device.\n", slot, func);
        return;
    }

    /* ACPI PHP can only work on slots
     * So only remove zero-functions -
     * which will remove all other fucntions of the same device in the
     * guest.
     */
    if ( func ) {
        fprintf(logfile, "hot remove: Attempt to remove non-zero function "
                "slot=0x%02x func=0x%0x.\n", slot, func);
        return;
    }

    /* update the php controller status */
    php_devfn.plug_evt = PHP_EVT_REMOVE;
    php_devfn.plug_devfn = devfn;

    /* generate a SCI interrupt */
    acpi_sci_intr(s);
}

void acpi_php_add(int devfn)
{
    GPEState *s = &gpe_state;
    char ret_str[30];
    int slot, func;

    if ( devfn < 0 ) {
        PIIX4ACPI_LOG(PIIX4ACPI_LOG_ERROR,
		         "hot add pci devfn %d exceed.\n", devfn);

        if ( devfn == -1 )
            sprintf(ret_str, "no free hotplug devfn");
        else if ( devfn == -2 )
            sprintf(ret_str, "wrong bdf or vdevfn");

        if ( strlen(ret_str) > 0 )
            xenstore_record_dm("parameter", ret_str);

        return;
    }

    /* ACPI PHP can only work on slots
     * For function 0 we do a full hot-add.
     * For other functions we just register the device with the hypervisor.
     * Assuming that function 0 is added after non-zero functions,
     * its ACPI PHP event will cause all previously registered functions
     * to be added to the guest.
     */

    slot = PCI_SLOT(devfn);
    func = PCI_FUNC(devfn);

    if ( !func )
    {
        /* update the php controller status */
        php_devfn.plug_evt = PHP_EVT_ADD;
        php_devfn.plug_devfn = devfn;
    }

    /* update the devfn status as present */
    php_reg_set(&php_devfn, devfn, 0xf);

    /* power on the function */
    power_on_php_devfn(devfn);

    /* tell Control panel which devfn for the new pass-throgh dev */
    sprintf(ret_str, "0x%02x", devfn);
    xenstore_record_dm("parameter", ret_str);

    /* signal the CP ACPI hot insert done */
    xenstore_record_dm_state("pci-inserted");

    /* generate a SCI interrupt */
    if ( !func )
        acpi_sci_intr(s);
}

#endif /* CONFIG_PASSTHROUGH */

/* PIIX4 acpi pci configuration space, func 2 */
i2c_bus *piix4_pm_init(PCIBus *bus, int devfn, uint32_t smb_io_base,
                       qemu_irq sci_irq_spec)
{
    PCIAcpiState *d;
    uint8_t *pci_conf;

    sci_irq = sci_irq_spec;

    /* we ignore smb_io_base as we don't give HVM guests an emulated smbus */

    /* register a function 2 of PIIX4 */
    d = (PCIAcpiState *)pci_register_device(
        bus, "PIIX4 ACPI", sizeof(PCIAcpiState),
        devfn, NULL, NULL);

    pci_conf = d->dev.config;
    pci_conf[0x00] = 0x86;  /* Intel */
    pci_conf[0x01] = 0x80;
    pci_conf[0x02] = 0x13;
    pci_conf[0x03] = 0x71;
    pci_conf[0x08] = 0x01;  /* B0 stepping */
    pci_conf[0x09] = 0x00;  /* base class */
    pci_conf[0x0a] = 0x80;  /* Sub class */
    pci_conf[0x0b] = 0x06;
    pci_conf[0x0e] = 0x00;
    pci_conf[0x3d] = 0x01;  /* Hardwired to PIRQA is used */


    /* PMBA POWER MANAGEMENT BASE ADDRESS, hardcoded to 0x1f40 
     * to make shutdown work for IPF, due to IPF Guest Firmware 
     * will enumerate pci devices. 
     *
     * TODO:  if Guest Firmware or Guest OS will change this PMBA,
     * More logic will be added.
     */
    pci_conf[0x40] = 0x41; /* Special device-specific BAR at 0x40 */
    pci_conf[0x41] = 0x1f;
    pci_conf[0x42] = 0x00;
    pci_conf[0x43] = 0x00;
    d->pm1_control = SCI_EN;

    d->pm1a_evt_blk_address = ACPI_PM1A_EVT_BLK_ADDRESS_V1;
    acpi_map((PCIDevice *)d, 0, d->pm1a_evt_blk_address, 0x10, PCI_ADDRESS_SPACE_IO);

    gpe_acpi_init();
#ifdef CONFIG_PASSTHROUGH
    php_devfn_init();
#endif
    register_ioport_write(ACPI_DBG_IO_ADDR, 4, 4, acpi_dbg_writel, d);

    register_savevm("piix4acpi", 0, 2, piix4acpi_save, piix4acpi_load, d);

    return NULL;
}

void qemu_system_hot_add_init() { }
void qemu_system_device_hot_add(int bus, int devfn, int state) {
    PIIX4ACPI_LOG(PIIX4ACPI_LOG_ERROR,
	  "qemu-upstream PCI hotplug not supported in qemu-dm\n");
    exit(-1);
}

void i440fx_init_memory_mappings(PCIDevice *d) {
    /* our implementation doesn't need this */
}

static int enable_processor(GPEState *g, int cpu)
{
    if (g->cpus_sts[cpu/8] & (1 << (cpu%8)))
        return 0;

    g->gpe0_sts[0] |= 4;
    g->cpus_sts[cpu/8] |= (1 << (cpu%8));
    return 1;
}

static int disable_processor(GPEState *g, int cpu)
{
    if (!(g->cpus_sts[cpu/8] & (1 << (cpu%8))))
        return 0;

    g->gpe0_sts[0] |= 4;
    g->cpus_sts[cpu/8] &= ~(1 << (cpu%8));
    return 1;
}

void qemu_cpu_add_remove(int cpu, int state)
{
    if ((cpu <0) || (cpu >= vcpus)) {
        fprintf(stderr, "vcpu out of range, should be [0~%d]\n", vcpus - 1);
        return;
    }

    if (state) {
        if (!enable_processor(&gpe_state, cpu))
            return;
    } else {
        if (!disable_processor(&gpe_state, cpu))
            return;
    }
    fprintf(logfile, "%s vcpu %d\n", state ? "Add" : "Remove", cpu);

    if (gpe_state.gpe0_en[0] & 4) {
        qemu_set_irq(sci_irq, 1);
        qemu_set_irq(sci_irq, 0);
    }
}
