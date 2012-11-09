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

#include "hw.h"
#include "pc.h"
#include "pci.h"
#include "irq.h"
#include "quadro6000.h"

typedef struct quadro6000_state {
    PCIDevice  pci_dev;
    int quadro6000_mmio_addr;
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

static uint32_t quadro6000_mmio_readb(void *opaque, target_phys_addr_t addr) {
    return 0;
}

static uint32_t quadro6000_mmio_readw(void *opaque, target_phys_addr_t addr) {
    return 0;
}

static uint32_t quadro6000_mmio_readd(void *opaque, target_phys_addr_t addr) {
    return 0;
}

static void quadro6000_mmio_writeb(void *opaque, target_phys_addr_t addr, uint32_t val) {
}

static void quadro6000_mmio_writew(void *opaque, target_phys_addr_t addr, uint32_t val) {
}

static void quadro6000_mmio_writed(void *opaque, target_phys_addr_t addr, uint32_t val) {
}

// function to access byte (index 0), word (index 1) and dword (index 2)
static CPUReadMemoryFunc *platform_mmio_read_funcs[3] = {
    quadro6000_mmio_readb,
    quadro6000_mmio_readw,
    quadro6000_mmio_readd,
};

static CPUWriteMemoryFunc *platform_mmio_write_funcs[3] = {
    quadro6000_mmio_writeb,
    quadro6000_mmio_writew,
    quadro6000_mmio_writed,
};

static void quadro6000_mmio_map(PCIDevice *dev, int region_num, uint32_t addr, uint32_t size, int type) {
    const int mmio_io_addr = cpu_register_io_memory(region_num, platform_mmio_read_funcs, platform_mmio_write_funcs, dev);
    cpu_register_physical_memory(addr, size, mmio_io_addr);
}

static uint32_t xen_platform_ioport_readb(void *opaque, uint32_t addr) {
    return 0;
}

static void xen_platform_ioport_writeb(void *opaque, uint32_t addr, uint32_t val) {
}

static void quadro6000_ioport_map(PCIDevice *dev, int region_num, uint32_t addr, uint32_t size, int type) {
    register_ioport_write(addr, size, 1, xen_platform_ioport_writeb, dev);
    register_ioport_read(addr, size, 1, xen_platform_ioport_readb, dev);
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
void pci_quadro6000_init(PCIBus* bus) {
    quadro6000_state_t* state;
    struct pci_config_header* pch;
    uint8_t *pci_conf;
    int instance;

    state = (quadro6000_state_t*)pci_register_device(bus, "quadro6000", sizeof(quadro6000_state_t), -1, NULL, NULL);
    pci_conf = state->pci_dev.config;
    pch = (struct pci_config_header *)state->pci_dev.config;

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
    pci_register_io_region(&state->pci_dev, 0, 0x2000000, PCI_ADDRESS_SPACE_MEM, quadro6000_mmio_map);

    // Region 1: Memory at c0000000 (64-bit, prefetchable) [disabled] [size=128M]
    pci_register_io_region(&state->pci_dev, 1, 0x8000000, PCI_ADDRESS_SPACE_MEM_PREFETCH, quadro6000_mmio_map);

    // Region 3: Memory at cc000000 (64-bit, prefetchable) [disabled] [size=64M]
    pci_register_io_region(&state->pci_dev, 3, 0x4000000, PCI_ADDRESS_SPACE_MEM_PREFETCH, quadro6000_mmio_map);

    // Region 5: I/O ports at ec80 [disabled] [size=128]
    pci_register_io_region(&state->pci_dev, 5, 0x0000080, PCI_ADDRESS_SPACE_IO, quadro6000_mmio_map);

    instance = pci_bus_num(bus) << 8 | state->pci_dev.devfn;
    Q6_PRINTF("register device model: %x\n", instance);
}
/* vim: set sw=8 ts=8 et tw=80 : */
