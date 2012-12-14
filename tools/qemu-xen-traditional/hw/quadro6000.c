/*
 * NVIDIA Quadro6000 device model
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

#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <pciaccess.h>
#include <assert.h>
#include "hw.h"
#include "pc.h"
#include "irq.h"
#include "pci.h"
#include "pci/header.h"
#include "pci/pci.h"
#include "pass-through.h"
#include "quadro6000.h"
#include "quadro6000_channel.h"
#include "quadro6000_vbios.inc"

typedef struct BAR {
    int io_index;     //  io_index in qemu
    uint32_t addr;    //  MMIO GPA
    uint32_t size;    //  MMIO memory size
    int type;
    uint8_t* space;   //  workspace memory
    uint8_t* real;    //  MMIO HVA
} bar_t;

typedef struct quadro6000_state {
    struct pt_dev pt_dev;
    bar_t bar[6];
    struct pci_dev* real;
    struct pci_device* access;
} quadro6000_state_t;

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

// Functional blocks
// http://nouveau.freedesktop.org/wiki/HwIntroduction#The_functional_blocks
#define LIST_FUNCTIONAL_BLOCK(V)\
    V(PMC)\
    V(PBUS)\
    V(PFIFO)\
    V(PFIFO_CACHE_I)\
    V(PVIDEO)\
    V(PTIMER)\
    V(PTV)\
    V(PCONNECTOR)\
    V(PRMVIO)\
    V(PFB)\
    V(PEXTDEV)\
    V(PROM)\
    V(PGRAPH)\
    V(PCRTC0)\
    V(PRMCIO)\
    V(PDISPLAY)\
    V(PDISPLAY_USER)\
    V(PRAMDAC)\
    V(PRMDIO)\
    V(PRAMIN)\
    V(FIFO)

enum functional_block_t {
#define V(NAME) NAME,
    LIST_FUNCTIONAL_BLOCK(V)
#undef V
};

static const char* functional_block_names[] = {
#define V(NAME) #NAME,
    LIST_FUNCTIONAL_BLOCK(V)
#undef V
};

// crystal freq is 27000KHz
#define GPU_CLOCKS_PER_NANO_SEC 27
#define GPU_CLOCKS_PER_SEC (GPU_CLOCKS_PER_NANO_SEC * 1000 * 1000)
static uint32_t timer_numerator = 0;
static uint32_t timer_denominator = 0;
static uint64_t timer_nano_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000000ULL) + ts.tv_nsec;
}
static uint64_t timer_now(void) {
    const uint64_t nano = timer_nano_sec();
    return nano * GPU_CLOCKS_PER_NANO_SEC * (timer_numerator + 1) / (timer_denominator + 1);
}

// Currently not considering alignment
static inline uint32_t read32(void* ptr, ptrdiff_t offset) {
    return *(uint32_t*)(((uint8_t*)ptr) + offset);
}

static inline uint16_t read16(void* ptr, ptrdiff_t offset) {
    return *(uint16_t*)(((uint8_t*)ptr) + offset);
}

static inline uint8_t read8(void* ptr, ptrdiff_t offset) {
    return *(uint8_t*)(((uint8_t*)ptr) + offset);
}

static inline void write32(void* ptr, ptrdiff_t offset, uint32_t data) {
    *(uint32_t*)(((uint8_t*)ptr) + offset) = data;
}

static inline void write16(void* ptr, ptrdiff_t offset, uint16_t data) {
    *(uint16_t*)(((uint8_t*)ptr) + offset) = data;
}

static inline void write8(void* ptr, ptrdiff_t offset, uint8_t data) {
    *(uint8_t*)(((uint8_t*)ptr) + offset) = data;
}

// http://nouveau.freedesktop.org/wiki/HwIntroduction
// BAR 0:
//   control registers. 16MB in size. Is divided into several areas for
//   each of the functional blocks of the card.
static void quadro6000_initialize_bar0(quadro6000_state_t* state) {
    void* ptr = qemu_mallocz(0x2000000);
    state->bar[0].space = ptr;
    write32(ptr, NV03_PMC_BOOT_0, QUADRO6000_REG0);

    // map vbios
    Q6_PRINTF("BIOS size ... %d\n", sizeof(quadro6000_vbios));
    memcpy(state->bar[0].space + NV_PROM_OFFSET, quadro6000_vbios, sizeof(quadro6000_vbios));

    // and initialization information from BIOS
    #include "quadro6000_init.inc"
}

static uint32_t quadro6000_mmio_bar0_readb(void *opaque, target_phys_addr_t addr) {
    // Q6_PRINTF("MMIO bar0 readb 0x%X\n", addr);
    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;
    return read8(state->bar[0].space, offset);
}

static uint32_t quadro6000_mmio_bar0_readw(void *opaque, target_phys_addr_t addr) {
    // Q6_PRINTF("MMIO bar0 readw 0x%X\n", addr);
    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;
    return read16(state->bar[0].space, offset);
}

static void quadro6000_mmio_bar0_writeb(void *opaque, target_phys_addr_t addr, uint32_t val) {
    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;
    write8(state->bar[0].space, offset, val);
}

static void quadro6000_mmio_bar0_writew(void *opaque, target_phys_addr_t addr, uint32_t val) {
    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;
    write16(state->bar[0].space, offset, val);
}

static uint32_t quadro6000_mmio_bar0_readd(void *opaque, target_phys_addr_t addr) {
    // Q6_PRINTF("MMIO bar0 readd 0x%X\n", addr);
    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;
    switch (offset) {
    case NV50_PMC_BOOT_0:  // 0x00000000
        // Q6_PRINTF("MMIO bar0 card 0x%X : 0x%X\n", addr, QUADRO6000_REG0);
        return QUADRO6000_REG0;

    // see nvc0_vram.c
    // Part struct starts with 0x11020c (PBFBs)
    case 0x022438:  // parts
        // Quadro6000 has 6 parts
        // return 0x00000006;
        // Q6_PRINTF("MMIO bar0 parts 0x%X : 0x%X\n", addr, 1);
        return 0x00000001;  // Expose only 1 parts by Quadro6000 device model

    case 0x022554:  // pmask
        // Q6_PRINTF("MMIO bar0 pmask 0x%X : 0x%X\n", addr, 0);
        return 0x00000000;

    case 0x10f20c:  // bsize
        // Quadro6000 has 0x00000400 size vram per part
        // Actually, because of << 20, VRAM part size is 1GB
        // Q6_PRINTF("MMIO bar0 bsize 0x%X : 0x%X\n", addr, 0x400);
        return 0x00000400;

    case 0x100800:  //  ?
        return 0x00000006;

    case (0x11020c + 0 * 0x1000):  // part 0 size
        return 0x00000400;

    case 0x409604:
        // gpc_nr TODO(Yusuke Suzuki) fix upper bits
        // gpc_nr(0x4) & rop_nr(0x6)
        return 0x00060004;

    case 0x070000:  // nv_wait
        // used in nv50_instmem.c, nv84_instmem_flush
        return 0;

    // PTIMER
    // Crystal freq is 27000KHz
    // We use CPU clock value instead of crystal of NVIDIA
    case NV04_PTIMER_TIME_0:  // 0x9400
        // low
        return (uint32_t)timer_now();
    case NV04_PTIMER_TIME_1:  // 0x9410
        // high
        return timer_now() >> 32;
    case NV04_PTIMER_NUMERATOR:  // 0x9200
        return timer_numerator;
    case NV04_PTIMER_DENOMINATOR:  // 0x9210
        return timer_denominator;

    // nvc0_graph.c
    case 0x40910c:  // write 0
        break;
    case 0x409100:  // write 2
        break;
    case 0x409800:  // nv_wait
        // used in nvc0_graph.c nvc0_graph_init_ctxctl
        // HUB init
        //
        // FIXME(Yusuke Suzuki) we should store valid context value...
        return 0x80000001;

    // nvc0_vm.c
    case 0x100cb8:
        break;
    case 0x100cbc:
        break;
    case 0x100c80:
        // used in nvc0_vm.c nvc0_vm_flush
        return 0x00ff8000;

    // tp
    case GPC_UNIT(0, 0x2608):
        return 0x3;
    case GPC_UNIT(1, 0x2608):
        return 0x4;
    case GPC_UNIT(2, 0x2608):
        return 0x3;
    case GPC_UNIT(3, 0x2608):
        return 0x4;
    }
    return read32(state->bar[0].space, offset);
}

static void quadro6000_mmio_bar0_writed(void *opaque, target_phys_addr_t addr, uint32_t val) {
    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[0].addr;
    switch (offset) {
    case 0x00000000:
        Q6_PRINTF("write call 0x%X\n", val);
        return;

    case 0x070000:  // nv_wait
        // used in nv50_instmem.c, nv84_instmem_flush
        // code
        //     nv_wr32(dev, 0x070000, 0x00000001);
        //     if (!nv_wait(dev, 0x070000, 0x00000002, 0x00000000))
        //           NV_ERROR(dev, "PRAMIN flush timeout\n");
        // flush instruction
        // nv84_instmem_flush();
        return;

    case NV04_PTIMER_NUMERATOR:
        timer_numerator = val;
        Q6_PRINTF("numerator set\n");
        return;

    case NV04_PTIMER_DENOMINATOR:
        timer_denominator = val;
        Q6_PRINTF("denominator set\n");
        return;
    }
    // fallback
    write32(state->bar[0].space, offset, val);
}

// BAR 1:
//   VRAM. On pre-NV50, corresponds directly to the available VRAM on card.
//   On NV50, gets remapped through VM engine.
static void quadro6000_initialize_bar1(quadro6000_state_t* state) {
    if (!(state->bar[1].space = qemu_mallocz(0x8000000))) {
        Q6_PRINTF("BAR1 Initialization failed\n");
    }
}

static uint32_t quadro6000_mmio_bar1_readb(void *opaque, target_phys_addr_t addr) {
    // Q6_PRINTF("MMIO bar1 readb 0x%X\n", addr);
    return 0;
//    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
//    const target_phys_addr_t offset = addr - state->bar[1].addr;
//    return read8(state->bar[1].space, offset);
}

static uint32_t quadro6000_mmio_bar1_readw(void *opaque, target_phys_addr_t addr) {
    // Q6_PRINTF("MMIO bar1 readw 0x%X\n", addr);
    return 0;
//    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
//    const target_phys_addr_t offset = addr - state->bar[1].addr;
//    return read16(state->bar[1].space, offset);
}

static uint32_t quadro6000_mmio_bar1_readd(void *opaque, target_phys_addr_t addr) {
    // Q6_PRINTF("MMIO bar1 readd 0x%X\n", addr);
    return 0;
//    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
//    const target_phys_addr_t offset = addr - state->bar[1].addr;
//    return read32(state->bar[1].space, offset);
}

static void quadro6000_mmio_bar1_writeb(void *opaque, target_phys_addr_t addr, uint32_t val) {
//    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
//    const target_phys_addr_t offset = addr - state->bar[1].addr;
//    write8(state->bar[1].space, offset, val);
}

static void quadro6000_mmio_bar1_writew(void *opaque, target_phys_addr_t addr, uint32_t val) {
//    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
//    const target_phys_addr_t offset = addr - state->bar[1].addr;
//    write16(state->bar[1].space, offset, val);
}

static void quadro6000_mmio_bar1_writed(void *opaque, target_phys_addr_t addr, uint32_t val) {
//    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
//    const target_phys_addr_t offset = addr - state->bar[1].addr;
//    Q6_PRINTF("writing BAR1 0x%X offset 0x%X\n", addr, offset);
//    write32(state->bar[1].space, offset, val);
}

// BAR3 ramin bar
static void quadro6000_initialize_bar3(quadro6000_state_t* state) {
    if (!(state->bar[3].space = qemu_mallocz(0x4000000))) {
        Q6_PRINTF("BAR3 Initialization failed\n");
    }
}

static uint32_t quadro6000_mmio_bar3_readb(void *opaque, target_phys_addr_t addr) {
    // Q6_PRINTF("MMIO bar3 readb 0x%X\n", addr);
    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[3].addr;
    return read8(state->bar[3].space, offset);
}

static uint32_t quadro6000_mmio_bar3_readw(void *opaque, target_phys_addr_t addr) {
    // Q6_PRINTF("MMIO bar3 readw 0x%X\n", addr);
    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[3].addr;
    return read16(state->bar[3].space, offset);
}

static uint32_t quadro6000_mmio_bar3_readd(void *opaque, target_phys_addr_t addr) {
    // Q6_PRINTF("MMIO bar3 readd 0x%X\n", addr);
    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[3].addr;
    return read32(state->bar[3].space, offset);
}

static void quadro6000_mmio_bar3_writeb(void *opaque, target_phys_addr_t addr, uint32_t val) {
    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[3].addr;
    write8(state->bar[3].space, offset, val);
}

static void quadro6000_mmio_bar3_writew(void *opaque, target_phys_addr_t addr, uint32_t val) {
    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[3].addr;
    write16(state->bar[3].space, offset, val);
}

static void quadro6000_mmio_bar3_writed(void *opaque, target_phys_addr_t addr, uint32_t val) {
    quadro6000_state_t* state = (quadro6000_state_t*)(opaque);
    const target_phys_addr_t offset = addr - state->bar[3].addr;
    write32(state->bar[3].space, offset, val);
}

// function to access byte (index 0), word (index 1) and dword (index 2)
typedef CPUReadMemoryFunc* CPUReadMemoryFuncBlock[3];
static CPUReadMemoryFuncBlock mmio_read_table[5] = {
    {
        quadro6000_mmio_bar0_readb,
        quadro6000_mmio_bar0_readw,
        quadro6000_mmio_bar0_readd,
    },
    {
        quadro6000_mmio_bar1_readb,
        quadro6000_mmio_bar1_readw,
        quadro6000_mmio_bar1_readd,
    },
    {},  // bar2
    {
        quadro6000_mmio_bar3_readb,
        quadro6000_mmio_bar3_readw,
        quadro6000_mmio_bar3_readd,
    }
};

typedef CPUWriteMemoryFunc* CPUWriteMemoryFuncBlock[3];
static CPUWriteMemoryFuncBlock mmio_write_table[5] = {
    {
        quadro6000_mmio_bar0_writeb,
        quadro6000_mmio_bar0_writew,
        quadro6000_mmio_bar0_writed,
    },
    {
        quadro6000_mmio_bar1_writeb,
        quadro6000_mmio_bar1_writew,
        quadro6000_mmio_bar1_writed,
    },
    {},  // bar2
    {
        quadro6000_mmio_bar3_writeb,
        quadro6000_mmio_bar3_writew,
        quadro6000_mmio_bar3_writed,
    }
};

static void quadro6000_mmio_map(PCIDevice *dev, int region_num, uint32_t addr, uint32_t size, int type) {
    int ret;
    quadro6000_state_t* state = (quadro6000_state_t*)dev;
    const int io_index = cpu_register_io_memory(0, mmio_read_table[region_num], mmio_write_table[region_num], dev);

    bar_t* bar = &(state)->bar[region_num];
    bar->io_index = io_index;
    bar->addr = addr;
    bar->size = size;
    bar->type = type;

    // get MMIO virtual address to real devices
    // by using libpciaccess
    ret = pci_device_map_range(
            state->access,
            state->access->regions[region_num].base_addr,
            state->access->regions[region_num].size,
            PCI_DEV_MAP_FLAG_WRITABLE,
            &bar->real);

    if (ret) {
        Q6_PRINTF("failed to map virt addr\n");
    }

    cpu_register_physical_memory(addr, size, io_index);

    Q6_PRINTF("BAR%d MMIO 0x%X - 0x%X, size %d, io index 0x%X\n", region_num, addr, addr + size, size, io_index);
}

static uint32_t quadro6000_ioport_readb(void *opaque, uint32_t addr) {
    return 0;
}

static void quadro6000_ioport_writeb(void *opaque, uint32_t addr, uint32_t val) {
}

static void quadro6000_ioport_map(PCIDevice *dev, int region_num, uint32_t addr, uint32_t size, int type) {
    register_ioport_write(addr, size, 1, quadro6000_ioport_writeb, dev);
    register_ioport_read(addr, size, 1, quadro6000_ioport_readb, dev);
}

// This code is ported from pass-through.c
static struct pci_dev* quadro6000_find_real_device(uint8_t r_bus, uint8_t r_dev, uint8_t r_func, struct pci_access *pci_access) {
    /* Find real device structure */
    struct pci_dev* pci_dev;
    for (pci_dev = pci_access->devices; pci_dev != NULL; pci_dev = pci_dev->next) {
        if ((r_bus == pci_dev->bus) && (r_dev == pci_dev->dev) && (r_func == pci_dev->func)) {
            return pci_dev;
        }
    }
    return NULL;
}

// setup real device initialization code
// TODO(Yusuke Suzuki) See error code
void quadro6000_init_real_device(quadro6000_state_t* state, uint8_t r_bus, uint8_t r_dev, uint8_t r_func, struct pci_access *pci_access) {
    struct pci_device_iterator* it;
    struct pci_device* dev;
    int ret;

    ret = pci_system_init();
    assert(!ret);

    state->real = quadro6000_find_real_device(r_bus, r_dev, r_func, pci_access);

    {
        struct pci_id_match quadro6000_match = {
            QUADRO6000_VENDOR,
            PCI_MATCH_ANY,
            PCI_MATCH_ANY,
            PCI_MATCH_ANY,
            0x30000,
            0xFFFF0000
        };

        it = pci_id_match_iterator_create(&quadro6000_match);
        assert(it);
        while ((dev = pci_device_next(it)) != NULL) {
            // search by BDF
            if (dev->bus == r_bus && dev->dev == r_dev && dev->func == r_func) {
                break;
            }
        }
        pci_iterator_destroy(it);

        assert(dev);
        ret = pci_device_probe(dev);
        assert(!ret);

        pci_device_enable(dev);

        state->access = dev;
    }
}

// Real device information
// 0a:00.0 VGA compatible controller: NVIDIA Corporation GF100 [Quadro 6000] (rev a3) (prog-if 00 [VGA controller])
//         Subsystem: NVIDIA Corporation Device 076f
//         Control: I/O- Mem- BusMaster- SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx-
//         Status: Cap+ 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-
//         Interrupt: pin A routed to IRQ 48
//         Region 0: Memory at d8000000 (32-bit, non-prefetchable) [disabled] [size=32M]
//         Region 1: Memory at c0000000 (64-bit, prefetchable) [disabled] [size=128M]
//         Region 3: Memory at cc000000 (64-bit, prefetchable) [disabled] [size=64M]
//         Region 5: I/O ports at ec80 [disabled] [size=128]
//         Expansion ROM at db000000 [disabled] [size=512K]
//         Capabilities: [60] Power Management version 3
//                 Flags: PMEClk- DSI- D1- D2- AuxCurrent=0mA PME(D0-,D1-,D2-,D3hot-,D3cold-)
//                 Status: D0 NoSoftRst+ PME-Enable- DSel=0 DScale=0 PME-
//         Capabilities: [68] MSI: Enable- Count=1/1 Maskable- 64bit+
//                 Address: 0000000000000000  Data: 0000
//         Capabilities: [78] Express (v1) Endpoint, MSI 00
//                 DevCap: MaxPayload 128 bytes, PhantFunc 0, Latency L0s unlimited, L1 <64us
//                         ExtTag+ AttnBtn- AttnInd- PwrInd- RBE+ FLReset-
//                 DevCtl: Report errors: Correctable- Non-Fatal+ Fatal+ Unsupported+
//                         RlxdOrd+ ExtTag- PhantFunc- AuxPwr- NoSnoop+
//                         MaxPayload 128 bytes, MaxReadReq 512 bytes
//                 DevSta: CorrErr- UncorrErr- FatalErr- UnsuppReq- AuxPwr- TransPend-
//                 LnkCap: Port #0, Speed 2.5GT/s, Width x16, ASPM L0s L1, Latency L0 <256ns, L1 <4us
//                         ClockPM+ Surprise- LLActRep- BwNot-
//                 LnkCtl: ASPM Disabled; RCB 64 bytes Disabled- Retrain- CommClk+
//                         ExtSynch- ClockPM- AutWidDis- BWInt- AutBWInt-
//                 LnkSta: Speed 2.5GT/s, Width x16, TrErr- Train- SlotClk+ DLActive- BWMgmt- ABWMgmt-
//         Capabilities: [b4] Vendor Specific Information: Len=14 <?>
//         Capabilities: [100 v1] Virtual Channel
//                 Caps:   LPEVC=0 RefClk=100ns PATEntryBits=1
//                 Arb:    Fixed- WRR32- WRR64- WRR128-
//                 Ctrl:   ArbSelect=Fixed
//                 Status: InProgress-
//                 VC0:    Caps:   PATOffset=00 MaxTimeSlots=1 RejSnoopTrans-
//                         Arb:    Fixed- WRR32- WRR64- WRR128- TWRR128- WRR256-
//                         Ctrl:   Enable+ ID=0 ArbSelect=Fixed TC/VC=ff
//                         Status: NegoPending- InProgress-
//         Capabilities: [128 v1] Power Budgeting <?>
//         Capabilities: [600 v1] Vendor Specific Information: ID=0001 Rev=1 Len=024 <?>
//         Kernel driver in use: pciback
//         Kernel modules: nouveau, nvidiafb
struct pt_dev * pci_quadro6000_init(PCIBus *bus,
        const char *e_dev_name, int e_devfn, uint8_t r_bus, uint8_t r_dev,
        uint8_t r_func, uint32_t machine_irq, struct pci_access *pci_access,
        char *opt) {
    quadro6000_state_t* state;
    struct pci_config_header* pch;
    uint8_t *pci_conf;
    int instance;

    state = (quadro6000_state_t*)pci_register_device(bus, "quadro6000", sizeof(quadro6000_state_t), e_devfn, NULL, NULL);

    quadro6000_init_real_device(state, r_bus, r_dev, r_func, pci_access);

    pci_conf = state->pt_dev.dev.config;
    pch = (struct pci_config_header *)state->pt_dev.dev.config;

    pci_config_set_vendor_id(pci_conf, QUADRO6000_VENDOR);
    pci_config_set_device_id(pci_conf, QUADRO6000_DEVICE);
    pch->command = QUADRO6000_COMMAND; /* IO, memory access and bus master */
    pci_config_set_class(pci_conf, PCI_CLASS_DISPLAY_VGA);
    pch->revision = QUADRO6000_REVISION;
    pch->header_type = 0;
    pch->interrupt_pin = 1;
    pci_conf[0x2c] = 0x53; /* subsystem vendor: XenSource */
    pci_conf[0x2d] = 0x58;
    pci_conf[0x2e] = 0x01; /* subsystem device */
    pci_conf[0x2f] = 0x00;

#if 0
    pch->subclass = 0x80; /* Other */
    pch->class = 0xff; /* Unclassified device class */
#endif

    // Region 0: Memory at d8000000 (32-bit, non-prefetchable) [disabled] [size=32M]
    pci_register_io_region(&state->pt_dev.dev, 0, 0x2000000, PCI_ADDRESS_SPACE_MEM, quadro6000_mmio_map);
    quadro6000_initialize_bar0(state);

    // Region 1: Memory at c0000000 (64-bit, prefetchable) [disabled] [size=128M]
    pci_register_io_region(&state->pt_dev.dev, 1, 0x8000000, PCI_ADDRESS_SPACE_MEM_PREFETCH, quadro6000_mmio_map);
    quadro6000_initialize_bar1(state);

    // Region 3: Memory at cc000000 (64-bit, prefetchable) [disabled] [size=64M]
    pci_register_io_region(&state->pt_dev.dev, 3, 0x4000000, PCI_ADDRESS_SPACE_MEM_PREFETCH, quadro6000_mmio_map);
    quadro6000_initialize_bar3(state);

    // Region 5: I/O ports at ec80 [disabled] [size=128]
    pci_register_io_region(&state->pt_dev.dev, 5, 0x0000080, PCI_ADDRESS_SPACE_IO, quadro6000_mmio_map);

    instance = pci_bus_num(bus) << 8 | state->pt_dev.dev.devfn;
    Q6_PRINTF("register device model: %x\n", instance);
    return (struct pt_dev*)state;
}
/* vim: set sw=4 ts=4 et tw=80 : */
