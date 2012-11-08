/*
 * XEN platform fake pci device, formerly known as the event channel device
 * 
 * Copyright (c) 2003-2004 Intel Corp.
 * Copyright (c) 2006 XenSource
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
#include "irq.h"
#include "qemu-xen.h"
#include "net.h"
#include "xen_platform.h"

#include <assert.h>
#include <xenguest.h>

static int drivers_blacklisted;
static uint16_t driver_product_version;
static int throttling_disabled;
extern FILE *logfile;
static char log_buffer[4096];
static int log_buffer_off;

static uint8_t platform_flags;

#define PFFLAG_ROM_LOCK 1 /* Sets whether ROM memory area is RW or RO */

typedef struct PCIXenPlatformState
{
  PCIDevice  pci_dev;
} PCIXenPlatformState;

static void log_throttling(const char *path, void *opaque)
{
    int len;
    char *throttling = xenstore_dom_read(domid, "log-throttling", &len);
    if (throttling != NULL) {
        throttling_disabled = !(throttling[0] - '0');
        free(throttling);
        fprintf(logfile, "log_throttling %s\n", throttling_disabled ? "disabled" : "enabled");
    }
}

/* We throttle access to dom0 syslog, to avoid DOS attacks.  This is
   modelled as a token bucket, with one token for every byte of log.
   The bucket size is 128KB (->1024 lines of 128 bytes each) and
   refills at 256B/s.  It starts full.  The guest is blocked if no
   tokens are available when it tries to generate a log message. */
#define BUCKET_MAX_SIZE (128*1024)
#define BUCKET_FILL_RATE 256

static void throttle(unsigned count)
{
    static unsigned available;
    static struct timespec last_refil;
    static int started;
    static int warned;

    struct timespec waiting_for, now;
    double delay;
    struct timespec ts;

    if (throttling_disabled)
        return;

    if (!started) {
        clock_gettime(CLOCK_MONOTONIC, &last_refil);
        available = BUCKET_MAX_SIZE;
        started = 1;
    }

    if (count > BUCKET_MAX_SIZE) {
        fprintf(logfile, "tried to get %d tokens, but bucket size is %d\n",
                BUCKET_MAX_SIZE, count);
        exit(1);
    }

    if (available < count) {
        /* The bucket is empty.  Refil it */

        /* When will it be full enough to handle this request? */
        delay = (double)(count - available) / BUCKET_FILL_RATE;
        waiting_for = last_refil;
        waiting_for.tv_sec += delay;
        waiting_for.tv_nsec += (delay - (int)delay) * 1e9;
        if (waiting_for.tv_nsec >= 1000000000) {
            waiting_for.tv_nsec -= 1000000000;
            waiting_for.tv_sec++;
        }

        /* How long do we have to wait? (might be negative) */
        clock_gettime(CLOCK_MONOTONIC, &now);
        ts.tv_sec = waiting_for.tv_sec - now.tv_sec;
        ts.tv_nsec = waiting_for.tv_nsec - now.tv_nsec;
        if (ts.tv_nsec < 0) {
            ts.tv_sec--;
            ts.tv_nsec += 1000000000;
        }

        /* Wait for it. */
        if (ts.tv_sec > 0 ||
            (ts.tv_sec == 0 && ts.tv_nsec > 0)) {
            if (!warned) {
                fprintf(logfile, "throttling guest access to syslog");
                warned = 1;
            }
            while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
                ;
        }

        /* Refil */
        clock_gettime(CLOCK_MONOTONIC, &now);
        delay = (now.tv_sec - last_refil.tv_sec) +
            (now.tv_nsec - last_refil.tv_nsec) * 1.0e-9;
        available += BUCKET_FILL_RATE * delay;
        if (available > BUCKET_MAX_SIZE)
            available = BUCKET_MAX_SIZE;
        last_refil = now;
    }

    assert(available >= count);

    available -= count;
}

#define UNPLUG_ALL_IDE_DISKS 1
#define UNPLUG_ALL_NICS 2
#define UNPLUG_AUX_IDE_DISKS 4

static void platform_fixed_ioport_write2(void *opaque, uint32_t addr, uint32_t val)
{
    switch (addr - 0x10) {
    case 0:
        /* Unplug devices.  Value is a bitmask of which devices to
           unplug, with bit 0 the IDE devices, bit 1 the network
           devices, and bit 2 the non-primary-master IDE devices. */
        if (val & UNPLUG_ALL_IDE_DISKS)
            ide_unplug_harddisks();
        if (val & UNPLUG_ALL_NICS) {
            pci_unplug_netifs();
            net_tap_shutdown_all();
        }
        if (val & UNPLUG_AUX_IDE_DISKS) {
            ide_unplug_aux_harddisks();
        }
        break;
    case 2:
        switch (val) {
        case 1:
            fprintf(logfile, "Citrix Windows PV drivers loaded in guest\n");
            break;
        case 0:
            fprintf(logfile, "Guest claimed to be running PV product 0?\n");
            break;
        default:
            fprintf(logfile, "Unknown PV product %d loaded in guest\n", val);
            break;
        }
        driver_product_version = val;
        break;
    }
}

static void platform_fixed_ioport_write4(void *opaque, uint32_t addr,
                                         uint32_t val)
{
    switch (addr - 0x10) {
    case 0:
        /* PV driver version */
        if (driver_product_version == 0) {
            fprintf(logfile,
                    "Drivers tried to set their version number (%d) before setting the product number?\n",
                    val);
            return;
        }
        fprintf(logfile, "PV driver build %d\n", val);
        if (xenstore_pv_driver_build_blacklisted(driver_product_version,
                                                 val)) {
            fprintf(logfile, "Drivers are blacklisted!\n");
            drivers_blacklisted = 1;
        }
        break;
    }
}

static void platform_fixed_ioport_write1(void *opaque, uint32_t addr, uint32_t val)
{
    switch (addr - 0x10) {
    case 0: /* Platform flags */ {
        hvmmem_type_t mem_type = (val & PFFLAG_ROM_LOCK) ?
            HVMMEM_ram_ro : HVMMEM_ram_rw;
        if (xc_hvm_set_mem_type(xc_handle, domid, mem_type, 0xc0, 0x40))
            fprintf(logfile,"platform_fixed_ioport: unable to change ro/rw "
                    "state of ROM memory area!\n");
        else {
            platform_flags = val & PFFLAG_ROM_LOCK;
            fprintf(logfile,"platform_fixed_ioport: changed ro/rw "
                    "state of ROM memory area. now is %s state.\n",
                    (mem_type == HVMMEM_ram_ro ? "ro":"rw"));
        }
        break;
    }
    case 2:
        /* Send bytes to syslog */
        if (val == '\n' || log_buffer_off == sizeof(log_buffer) - 1) {
            /* Flush buffer */
            log_buffer[log_buffer_off] = 0;
            throttle(log_buffer_off);
            fprintf(logfile, "%s\n", log_buffer);
            log_buffer_off = 0;
            break;
        }
        log_buffer[log_buffer_off++] = val;
        break;
    }
}

static uint32_t platform_fixed_ioport_read2(void *opaque, uint32_t addr)
{
    switch (addr - 0x10) {
    case 0:
        if (drivers_blacklisted) {
            /* The drivers will recognise this magic number and refuse
             * to do anything. */
            return 0xd249;
        } else {
            /* Magic value so that you can identify the interface. */
            return 0x49d2;
        }
    default:
        return 0xffff;
    }
}

static uint32_t platform_fixed_ioport_read1(void *opaque, uint32_t addr)
{
    switch (addr - 0x10) {
    case 0:
        /* Platform flags */
        return platform_flags;
    case 2:
        /* Version number */
        return 1;
    default:
        return 0xff;
    }
}

static void platform_fixed_ioport_save(QEMUFile *f, void *opaque)
{
    qemu_put_8s(f, &platform_flags);
}

static int platform_fixed_ioport_load(QEMUFile *f, void *opaque, int version_id)
{
    uint8_t flags;

    if (version_id > 1)
        return -EINVAL;

    qemu_get_8s(f, &flags);
    platform_fixed_ioport_write1(NULL, 0x10, flags);

    return 0;
}

void platform_fixed_ioport_init(void)
{
    struct stat stbuf;
    int len = 1;

    register_savevm("platform_fixed_ioport", 0, 1, platform_fixed_ioport_save,
                    platform_fixed_ioport_load, NULL);

    register_ioport_write(0x10, 16, 4, platform_fixed_ioport_write4, NULL);
    register_ioport_write(0x10, 16, 2, platform_fixed_ioport_write2, NULL);
    register_ioport_write(0x10, 16, 1, platform_fixed_ioport_write1, NULL);
    register_ioport_read(0x10, 16, 2, platform_fixed_ioport_read2, NULL);
    register_ioport_read(0x10, 16, 1, platform_fixed_ioport_read1, NULL);

    platform_fixed_ioport_write1(NULL, 0x10, 0);
}

static uint32_t xen_platform_ioport_readb(void *opaque, uint32_t addr)
{
    addr &= 0xff;

    return (addr == 0) ? platform_fixed_ioport_read1(NULL, 0x10) : ~0u;
}

static void xen_platform_ioport_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    addr &= 0xff;
    val  &= 0xff;

    switch (addr) {
    case 0: /* Platform flags */
        platform_fixed_ioport_write1(NULL, 0x10, val);
        break;
    case 8:
        {
            if (val == '\n' || log_buffer_off == sizeof(log_buffer) - 1) {
                /* Flush buffer */
                log_buffer[log_buffer_off] = 0;
                throttle(log_buffer_off);
                fprintf(logfile, "%s\n", log_buffer);
                log_buffer_off = 0;
                break;
            }
            log_buffer[log_buffer_off++] = val;
        }
        break;
    default:
        break;
    }
}

static void platform_ioport_map(PCIDevice *pci_dev, int region_num, uint32_t addr, uint32_t size, int type)
{
    PCIXenPlatformState *d = (PCIXenPlatformState *)pci_dev;
    register_ioport_write(addr, size, 1, xen_platform_ioport_writeb, d);
    register_ioport_read(addr, size, 1, xen_platform_ioport_readb, d);
}

static uint32_t platform_mmio_read(void *opaque, target_phys_addr_t addr)
{
    static int warnings = 0;
    if (warnings < 5) {
        fprintf(logfile, "Warning: attempted read from physical address "
                "0x%"PRIx64" in xen platform mmio space\n", (uint64_t)addr);
        warnings++;
    }
    return 0;
}

static void platform_mmio_write(void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    static int warnings = 0;
    if (warnings < 5) {
        fprintf(logfile, "Warning: attempted write of 0x%x to physical "
                "address 0x%"PRIx64" in xen platform mmio space\n",
                val, (uint64_t)addr);
        warnings++;
    }
    return;
}

static CPUReadMemoryFunc *platform_mmio_read_funcs[3] = {
    platform_mmio_read,
    platform_mmio_read,
    platform_mmio_read,
};

static CPUWriteMemoryFunc *platform_mmio_write_funcs[3] = {
    platform_mmio_write,
    platform_mmio_write,
    platform_mmio_write,
};

static void platform_mmio_map(PCIDevice *d, int region_num,
                              uint32_t addr, uint32_t size, int type)
{
    int mmio_io_addr;

    mmio_io_addr = cpu_register_io_memory(0, platform_mmio_read_funcs,
                                          platform_mmio_write_funcs, NULL);

    cpu_register_physical_memory(addr, 0x1000000, mmio_io_addr);
}

struct pci_config_header {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t  revision;
    uint8_t  api;
    uint8_t  subclass;
    uint8_t  class;
    uint8_t  cache_line_size; /* Units of 32 bit words */
    uint8_t  latency_timer; /* In units of bus cycles */
    uint8_t  header_type; /* Should be 0 */
    uint8_t  bist; /* Built in self test */
    uint32_t base_address_regs[6];
    uint32_t reserved1;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
    uint32_t rom_addr;
    uint32_t reserved3;
    uint32_t reserved4;
    uint8_t  interrupt_line;
    uint8_t  interrupt_pin;
    uint8_t  min_gnt;
    uint8_t  max_lat;
};

static void xen_pci_save(QEMUFile *f, void *opaque)
{
    PCIXenPlatformState *d = opaque;
    uint64_t t = 0;

    pci_device_save(&d->pci_dev, f);
    qemu_put_be64s(f, &t);
}

static int xen_pci_load(QEMUFile *f, void *opaque, int version_id)
{
    PCIXenPlatformState *d = opaque;
    int ret;

    if (version_id > 3)
        return -EINVAL;

    ret = pci_device_load(&d->pci_dev, f);
    if (ret < 0)
        return ret;

    if (version_id >= 2) {
        if (version_id == 2) {
            uint8_t flags;
            qemu_get_8s(f, &flags);
            xen_platform_ioport_writeb(d, 0, flags);
        }
        qemu_get_be64(f);
    }

    return 0;
}

void pci_xen_platform_init(PCIBus *bus)
{
    PCIXenPlatformState *d;
    struct pci_config_header *pch;

    printf("Register xen platform.\n");
    d = (PCIXenPlatformState *)pci_register_device(
        bus, "xen-platform", sizeof(PCIXenPlatformState), -1, NULL, NULL);
    pch = (struct pci_config_header *)d->pci_dev.config;
    pch->vendor_id = 0x5853;
    pch->device_id = 0x0001;
    pch->command = 3; /* IO and memory access */
    pch->revision = 1;
    pch->api = 0;
    pch->subclass = 0x80; /* Other */
    pch->class = 0xff; /* Unclassified device class */
    pch->header_type = 0;
    pch->interrupt_pin = 1;

    /* Microsoft WHQL requires non-zero subsystem IDs. */
    /* http://www.pcisig.com/reflector/msg02205.html.  */
    pch->subsystem_vendor_id = pch->vendor_id; /* Duplicate vendor id.  */
    pch->subsystem_id        = 0x0001;         /* Hardcode sub-id as 1. */

    pci_register_io_region(&d->pci_dev, 0, 0x100,
                           PCI_ADDRESS_SPACE_IO, platform_ioport_map);

    /* reserve 16MB mmio address for share memory*/
    pci_register_io_region(&d->pci_dev, 1, 0x1000000,
                           PCI_ADDRESS_SPACE_MEM_PREFETCH, platform_mmio_map);

    register_savevm("platform", 0, 3, xen_pci_save, xen_pci_load, d);
    printf("Done register platform.\n");
    xenstore_dom_watch(domid, "log-throttling", log_throttling, NULL);
}

