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

void pci_quadro6000_init(PCIBus* bus) {
    quadro6000_state_t* state;
    struct pci_config_header* pch;
    int instance;

    state = (quadro6000_state_t*)pci_register_device(bus, "quadro6000", sizeof(quadro6000_state_t), -1, NULL, NULL);
    pch = (struct pci_config_header *)state->pci_dev.config;
    pch->vendor_id = 0x5853;
    pch->device_id = 0x0003;
    pch->command = 3; /* IO and memory access */
    pch->revision = 1;
    pch->api = 0;
    pch->subclass = 0x80; /* Other */
    pch->class = 0xff; /* Unclassified device class */
    pch->header_type = 0;
    pch->interrupt_pin = 1;

    instance = pci_bus_num(bus) << 8 | state->pci_dev.devfn;

    printf("Register Quadro6000 device model: %x\n", instance);
}
