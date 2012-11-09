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

// See http://www.pcidatabase.com/vendor_details.php?id=606
void pci_quadro6000_init(PCIBus* bus) {
    quadro6000_state_t* state;
    struct pci_config_header* pch;
    uint8_t *pci_conf;
    int instance;

    state = (quadro6000_state_t*)pci_register_device(bus, "quadro6000", sizeof(quadro6000_state_t), -1, NULL, NULL);
    pci_conf = state->dev.config;
    pch = (struct pci_config_header *)state->pci_dev.config;

    pci_config_set_vendor_id(pci_conf, QUADRO6000_VENDOR);
    pci_config_set_device_id(pci_conf, QUADRO6000_DEVICE);
    pch->command = QUADRO6000_COMMAND; /* IO, memory access and bus master */
    pci_config_set_class(pci_conf, PCI_CLASS_DISPLAY_VGA);
    pch->revision = QUADRO6000_REVISION;
    pch->header_type = 0;
    pci_conf[0x2c] = 0x53; /* subsystem vendor: XenSource */
    pci_conf[0x2d] = 0x58;
    pci_conf[0x2e] = 0x01; /* subsystem device */
    pci_conf[0x2f] = 0x00;
    pch->interrupt_pin = 1;

#if 0
    pch->subclass = 0x80; /* Other */
    pch->class = 0xff; /* Unclassified device class */
#endif

    instance = pci_bus_num(bus) << 8 | state->pci_dev.devfn;

    printf("Register Quadro6000 device model: %x\n", instance);
}
/* vim: set sw=4 ts=4 et tw=80 : */
