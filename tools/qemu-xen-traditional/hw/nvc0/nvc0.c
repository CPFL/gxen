/*
 * NVIDIA NVC0 device model
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

#include <assert.h>
#include "nvc0.h"
#include "nvc0_channel.h"
#include "nvc0_ioport.h"
#include "nvc0_mmio.h"

long nvc0_guest_id = -1;

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

// This code is ported from pass-through.c
static struct pci_dev* nvc0_find_real_device(uint8_t r_bus, uint8_t r_dev, uint8_t r_func, struct pci_access *pci_access) {
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
static void nvc0_init_real_device(nvc0_state_t* state, uint8_t r_bus, uint8_t r_dev, uint8_t r_func, struct pci_access *pci_access) {
    state->real = nvc0_find_real_device(r_bus, r_dev, r_func, pci_access);
    {
        struct pci_id_match nvc0_match = {
            NVC0_VENDOR,
            PCI_MATCH_ANY,
            PCI_MATCH_ANY,
            PCI_MATCH_ANY,
            0x30000,
            0xFFFF0000
        };
        struct pci_device_iterator* it;
        struct pci_device* dev;
        int ret;

        ret = pci_system_init();
        assert(!ret);

        it = pci_id_match_iterator_create(&nvc0_match);
        assert(it);
        while ((dev = pci_device_next(it)) != NULL) {
            // search by BDF
            if (dev->bus == r_bus && dev->dev == r_dev && dev->func == r_func) {
                break;
            }
        }
        pci_iterator_destroy(it);

        assert(dev);
        pci_device_enable(dev);
        ret = pci_device_probe(dev);
        assert(!ret);

        // And enable memory and io port.
        // FIXME(Yusuke Suzuki)
        // This is very ad-hoc code.
        // We should cleanup and set precise command code in the future.
        pci_device_cfg_write_u16(dev, NVC0_COMMAND, PCI_COMMAND);

        state->access = dev;
    }
    NVC0_PRINTF("PCI device enabled\n");
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
struct pt_dev * pci_nvc0_init(PCIBus *bus,
        const char *e_dev_name, int e_devfn, uint8_t r_bus, uint8_t r_dev,
        uint8_t r_func, uint32_t machine_irq, struct pci_access *pci_access,
        char *opt) {
    nvc0_state_t* state;
    struct pci_config_header* pch;
    uint8_t *pci_conf;
    int instance;

    state = (nvc0_state_t*)pci_register_device(bus, "nvc0", sizeof(nvc0_state_t), e_devfn, NULL, NULL);

    // FIXME(Yusuke Suzuki)
    // set correct guest id
    state->guest = nvc0_guest_id;

    nvc0_init_real_device(state, r_bus, r_dev, r_func, pci_access);

    pci_conf = state->pt_dev.dev.config;
    pch = (struct pci_config_header *)state->pt_dev.dev.config;

    pci_config_set_vendor_id(pci_conf, NVC0_VENDOR);
    pci_config_set_device_id(pci_conf, NVC0_DEVICE);
    pch->command = NVC0_COMMAND; /* IO, memory access and bus master */
    pci_config_set_class(pci_conf, PCI_CLASS_DISPLAY_VGA);
    pch->revision = NVC0_REVISION;
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

    // init MMIO
    nvc0_init_mmio(state);

    // init I/O ports
    nvc0_init_ioport(state);

    instance = pci_bus_num(bus) << 8 | state->pt_dev.dev.devfn;
    NVC0_PRINTF("register device model: %x\n", instance);
    return (struct pt_dev*)state;
}
/* vim: set sw=4 ts=4 et tw=80 : */
