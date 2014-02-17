/*
 * NVIDIA NVC0 MMIO model
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

#include "nvc0.h"
#include "nvc0_mmio.h"
#include "nvc0_mmio_bar0.h"
#include "nvc0_mmio_bar1.h"
#include "nvc0_mmio_bar3.h"
#include "nvc0_mmio_bar4.h"
#include "nvc0_mmio_rom.h"
#include "nvc0_para_virt.h"
#include "pass-through.h"
#include "a3/config.h"
#include "nvc0_api_bar4.h"
#include "nvc0_api_bar5.h"

// function to access byte (index 0), word (index 1) and dword (index 2)
typedef CPUReadMemoryFunc* CPUReadMemoryFuncBlock[3];
static CPUReadMemoryFuncBlock mmio_read_table[7] = {
    {
        // bar0
        nvc0_mmio_bar0_readb,
        nvc0_mmio_bar0_readw,
        nvc0_mmio_bar0_readd,
    },
    {
        // bar1
        nvc0_mmio_bar1_readb,
        nvc0_mmio_bar1_readw,
        nvc0_mmio_bar1_readd,
    },
    {
        // bar2
    },
    {
        // bar3
        nvc0_mmio_bar3_readb,
        nvc0_mmio_bar3_readw,
        nvc0_mmio_bar3_readd,
    },
    {
        // bar4
        nvc0_mmio_bar4_readb,
        nvc0_mmio_bar4_readw,
        nvc0_mmio_bar4_readd,
    },
    {
        // bar5
    },
    {
        // bar6
        nvc0_mmio_rom_readb,
        nvc0_mmio_rom_readw,
        nvc0_mmio_rom_readd,
    }
};

typedef CPUWriteMemoryFunc* CPUWriteMemoryFuncBlock[3];
static CPUWriteMemoryFuncBlock mmio_write_table[7] = {
    {
        // bar0
        nvc0_mmio_bar0_writeb,
        nvc0_mmio_bar0_writew,
        nvc0_mmio_bar0_writed,
    },
    {
        // bar1
        nvc0_mmio_bar1_writeb,
        nvc0_mmio_bar1_writew,
        nvc0_mmio_bar1_writed,
    },
    {
        // bar2
    },
    {
        // bar3
        nvc0_mmio_bar3_writeb,
        nvc0_mmio_bar3_writew,
        nvc0_mmio_bar3_writed,
    },
    {
        // bar4
        nvc0_mmio_bar4_writeb,
        nvc0_mmio_bar4_writew,
        nvc0_mmio_bar4_writed,
    },
    {
        // bar5
    },
    {
        // bar6
        nvc0_mmio_rom_writeb,
        nvc0_mmio_rom_writew,
        nvc0_mmio_rom_writed,
    }
};

static CPUReadMemoryFuncBlock api_mmio_read_table[7] = {
    {
        // bar0
    },
    {
        // bar1
    },
    {
        // bar2
    },
    {
        // bar3
    },
    {
        // bar4
        nvc0_api_mmio_bar4_readb,
        nvc0_api_mmio_bar4_readw,
        nvc0_api_mmio_bar4_readd
    },
    {
        // bar5
        nvc0_api_mmio_bar5_readb,
        nvc0_api_mmio_bar5_readw,
        nvc0_api_mmio_bar5_readd
    },
    {
        // bar6
    }
};

static CPUWriteMemoryFuncBlock api_mmio_write_table[7] = {
    {
        // bar0
    },
    {
        // bar1
    },
    {
        // bar2
    },
    {
        // bar3
    },
    {
        // bar4
        nvc0_api_mmio_bar4_writeb,
        nvc0_api_mmio_bar4_writew,
        nvc0_api_mmio_bar4_writed
    },
    {
        // bar5
        nvc0_api_mmio_bar5_writeb,
        nvc0_api_mmio_bar5_writew,
        nvc0_api_mmio_bar5_writed
    },
    {
        // bar6
    }
};

static void nvc0_mmio_map(PCIDevice *dev, int region_num, uint32_t addr, uint32_t size, int type) {
    int ret;
    nvc0_state_t* state = nvc0_state(dev);
    int io_index;

    if (nvc0_guest_id == 42) {
        io_index =  cpu_register_io_memory(0, api_mmio_read_table[region_num], api_mmio_write_table[region_num], dev);
    } else {
        io_index =  cpu_register_io_memory(0, mmio_read_table[region_num], mmio_write_table[region_num], dev);
    }

    nvc0_bar_t* bar = &(state)->bar[region_num];
    bar->io_index = io_index;
    bar->addr = addr;
    bar->size = size;
    bar->type = type;

    // get MMIO virtual address to real devices
#if 0
    if (!bar->real) {
        ret = pci_device_map_range(
                state->access,
                state->access->regions[region_num].base_addr,
                state->access->regions[region_num].size,
                PCI_DEV_MAP_FLAG_WRITABLE,
                (void**)&bar->real);
        if (ret) {
            NVC0_PRINTF("failed to map virt addr %d\n", ret);
        }
    }
#endif

    cpu_register_physical_memory(addr, size, io_index);

    // notify BAR3 to A3
    if (region_num == 3) {
        nvc0_mmio_bar3_notify(state);
    }

    NVC0_PRINTF("BAR%d MMIO 0x%X - 0x%X, size %d, io index 0x%X\n", region_num, addr, addr + size, size, io_index);
}

void nvc0_mmio_init(nvc0_state_t* state) {
    // Region 0: Memory at d8000000 (32-bit, non-prefetchable) [disabled] [size=32M]
    pci_register_io_region(&state->device->dev, 0, 0x2000000, PCI_ADDRESS_SPACE_MEM, nvc0_mmio_map);
    nvc0_init_bar0(state);

    // Region 1: Memory at c0000000 (64-bit, prefetchable) [disabled] [size=128M]
    pci_register_io_region(&state->device->dev, 1, 0x8000000, PCI_ADDRESS_SPACE_MEM_PREFETCH, nvc0_mmio_map);
    nvc0_init_bar1(state);

    // Region 3: Memory at cc000000 (64-bit, prefetchable) [disabled] [size=64M]
    // pci_register_io_region(&state->device->dev, 3, 0x4000000, PCI_ADDRESS_SPACE_MEM_PREFETCH, nvc0_mmio_map);
    // nvc0_init_bar3(state);

    // MODIFIED Region 3
    // Region 3: Memory at cc000000 (64-bit, prefetchable) [disabled] [size=16M]
    // BAR3 effective area is limited to 16MB (24bits)
    // So we should split this area. hard coded 8MB
    // pci_register_io_region(&state->device->dev, 3, 0x4000000 / 4, PCI_ADDRESS_SPACE_MEM_PREFETCH, nvc0_mmio_map);
    // TODO(Yusuke Suzuki)
    // This is hard coded value
    pci_register_io_region(&state->device->dev, 3, A3_BAR3_ARENA_SIZE, PCI_ADDRESS_SPACE_MEM_PREFETCH, nvc0_mmio_map);
    nvc0_init_bar3(state);

    // Region ROM : Meomory
    // pci_register_io_region(&state->device->dev, PCI_ROM_SLOT, 512 * 1024, PCI_ADDRESS_SPACE_MEM_PREFETCH, nvc0_mmio_map);
    // nvc0_init_rom(state);

    if (nvc0_guest_id > 0) {
        // para virtualized
        // BAR4 4KB (1 page)
        pci_register_io_region(&state->device->dev, 4, NOUVEAU_PV_SLOT_SIZE, PCI_ADDRESS_SPACE_MEM, nvc0_mmio_map);
    }
}

void nvc0_api_paravirt_mmio_init(nvc0_state_t* state) {
    // para virtualized
    // BAR4 4KB (1 page)
    pci_register_io_region(&state->device->dev, 4, NOUVEAU_PV_SLOT_SIZE, PCI_ADDRESS_SPACE_MEM, nvc0_mmio_map);

    // para virtualized
    // BAR5 128MB
    pci_register_io_region(&state->device->dev, 5, 0x8000000, PCI_ADDRESS_SPACE_MEM_PREFETCH, nvc0_mmio_map);
}

/* vim: set sw=4 ts=4 et tw=80 : */
