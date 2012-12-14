#ifndef HW_QUADRO6000_IOPORT_H_
#define HW_QUADRO6000_IOPORT_H_

void quadro6000_ioport_map(PCIDevice *dev, int region_num, uint32_t addr, uint32_t size, int type);

#endif  // HW_QUADRO6000_IOPORT_H_
