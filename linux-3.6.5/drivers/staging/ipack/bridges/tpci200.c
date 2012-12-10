/**
 * tpci200.c
 *
 * driver for the TEWS TPCI-200 device
 * Copyright (c) 2009 Nicolas Serafini, EIC2 SA
 * Copyright (c) 2010,2011 Samuel Iglesias Gonsalvez <siglesia@cern.ch>, CERN
 * Copyright (c) 2012 Samuel Iglesias Gonsalvez <siglesias@igalia.com>, Igalia
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#include <linux/module.h>
#include "tpci200.h"

static struct ipack_bus_ops tpci200_bus_ops;

/* TPCI200 controls registers */
static int control_reg[] = {
	TPCI200_CONTROL_A_REG,
	TPCI200_CONTROL_B_REG,
	TPCI200_CONTROL_C_REG,
	TPCI200_CONTROL_D_REG
};

/* Linked list to save the registered devices */
static LIST_HEAD(tpci200_list);

static int tpci200_slot_unregister(struct ipack_device *dev);

static struct tpci200_board *check_slot(struct ipack_device *dev)
{
	struct tpci200_board *tpci200;
	int found = 0;

	if (dev == NULL)
		return NULL;

	list_for_each_entry(tpci200, &tpci200_list, list) {
		if (tpci200->number == dev->bus_nr) {
			found = 1;
			break;
		}
	}

	if (!found) {
		dev_err(&dev->dev, "Carrier not found\n");
		return NULL;
	}

	if (dev->slot >= TPCI200_NB_SLOT) {
		dev_info(&dev->dev,
			 "Slot [%d:%d] doesn't exist! Last tpci200 slot is %d.\n",
			 dev->bus_nr, dev->slot, TPCI200_NB_SLOT-1);
		return NULL;
	}

	return tpci200;
}

static inline unsigned char __tpci200_read8(void __iomem *address,
					    unsigned long offset)
{
	return ioread8(address + (offset^1));
}

static inline unsigned short __tpci200_read16(void __iomem *address,
					      unsigned long offset)
{
	return ioread16(address + offset);
}

static inline unsigned int __tpci200_read32(void __iomem *address,
					    unsigned long offset)
{
	return swahw32(ioread32(address + offset));
}

static inline void __tpci200_write8(unsigned char value,
				    void __iomem *address, unsigned long offset)
{
	iowrite8(value, address+(offset^1));
}

static inline void __tpci200_write16(unsigned short value,
				     void __iomem *address,
				     unsigned long offset)
{
	iowrite16(value, address+offset);
}

static inline void __tpci200_write32(unsigned int value,
				     void __iomem *address,
				     unsigned long offset)
{
	iowrite32(swahw32(value), address+offset);
}

static struct ipack_addr_space *get_slot_address_space(struct ipack_device *dev,
						       int space)
{
	struct ipack_addr_space *addr;

	switch (space) {
	case IPACK_IO_SPACE:
		addr = &dev->io_space;
		break;
	case IPACK_ID_SPACE:
		addr = &dev->id_space;
		break;
	case IPACK_MEM_SPACE:
		addr = &dev->mem_space;
		break;
	default:
		dev_err(&dev->dev,
			"Slot [%d:%d] space number %d doesn't exist !\n",
			dev->bus_nr, dev->slot, space);
		return NULL;
		break;
	}

	if ((addr->size == 0) || (addr->address == NULL)) {
		dev_err(&dev->dev, "Error, slot space not mapped !\n");
		return NULL;
	}

	return addr;
}

static int tpci200_read8(struct ipack_device *dev, int space,
			 unsigned long offset, unsigned char *value)
{
	struct ipack_addr_space *addr;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	addr = get_slot_address_space(dev, space);
	if (addr == NULL)
		return -EINVAL;

	if (offset >= addr->size) {
		dev_err(&dev->dev, "Error, slot space offset error !\n");
		return -EFAULT;
	}

	*value = __tpci200_read8(addr->address, offset);

	return 0;
}

static int tpci200_read16(struct ipack_device *dev, int space,
			  unsigned long offset, unsigned short *value)
{
	struct ipack_addr_space *addr;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	addr = get_slot_address_space(dev, space);
	if (addr == NULL)
		return -EINVAL;

	if ((offset+2) >= addr->size) {
		dev_err(&dev->dev, "Error, slot space offset error !\n");
		return -EFAULT;
	}
	*value = __tpci200_read16(addr->address, offset);

	return 0;
}

static int tpci200_read32(struct ipack_device *dev, int space,
			  unsigned long offset, unsigned int *value)
{
	struct ipack_addr_space *addr;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	addr = get_slot_address_space(dev, space);
	if (addr == NULL)
		return -EINVAL;

	if ((offset+4) >= addr->size) {
		dev_err(&dev->dev, "Error, slot space offset error !\n");
		return -EFAULT;
	}

	*value = __tpci200_read32(addr->address, offset);

	return 0;
}

static int tpci200_write8(struct ipack_device *dev, int space,
			  unsigned long offset, unsigned char value)
{
	struct ipack_addr_space *addr;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	addr = get_slot_address_space(dev, space);
	if (addr == NULL)
		return -EINVAL;

	if (offset >= addr->size) {
		dev_err(&dev->dev, "Error, slot space offset error !\n");
		return -EFAULT;
	}

	__tpci200_write8(value, addr->address, offset);

	return 0;
}

static int tpci200_write16(struct ipack_device *dev, int space,
			   unsigned long offset, unsigned short value)
{
	struct ipack_addr_space *addr;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	addr = get_slot_address_space(dev, space);
	if (addr == NULL)
		return -EINVAL;

	if ((offset+2) >= addr->size) {
		dev_err(&dev->dev, "Error, slot space offset error !\n");
		return -EFAULT;
	}

	__tpci200_write16(value, addr->address, offset);

	return 0;
}

static int tpci200_write32(struct ipack_device *dev, int space,
			   unsigned long offset, unsigned int value)
{
	struct ipack_addr_space *addr;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	addr = get_slot_address_space(dev, space);
	if (addr == NULL)
		return -EINVAL;

	if ((offset+4) >= addr->size) {
		dev_err(&dev->dev, "Error, slot space offset error !\n");
		return -EFAULT;
	}

	__tpci200_write32(value, addr->address, offset);

	return 0;
}

static void tpci200_unregister(struct tpci200_board *tpci200)
{
	int i;

	free_irq(tpci200->info->pdev->irq, (void *) tpci200);

	pci_iounmap(tpci200->info->pdev, tpci200->info->interface_regs);
	pci_iounmap(tpci200->info->pdev, tpci200->info->ioidint_space);
	pci_iounmap(tpci200->info->pdev, tpci200->info->mem8_space);

	pci_release_region(tpci200->info->pdev, TPCI200_IP_INTERFACE_BAR);
	pci_release_region(tpci200->info->pdev, TPCI200_IO_ID_INT_SPACES_BAR);
	pci_release_region(tpci200->info->pdev, TPCI200_MEM8_SPACE_BAR);

	pci_disable_device(tpci200->info->pdev);
	pci_dev_put(tpci200->info->pdev);

	for (i = 0; i < TPCI200_NB_SLOT; i++) {
		tpci200->slots[i].io_phys.address = NULL;
		tpci200->slots[i].io_phys.size = 0;
		tpci200->slots[i].id_phys.address = NULL;
		tpci200->slots[i].id_phys.size = 0;
		tpci200->slots[i].mem_phys.address = NULL;
		tpci200->slots[i].mem_phys.size = 0;
	}
}

static irqreturn_t tpci200_interrupt(int irq, void *dev_id)
{
	struct tpci200_board *tpci200 = (struct tpci200_board *) dev_id;
	int i;
	unsigned short status_reg, reg_value;
	unsigned short unhandled_ints = 0;
	irqreturn_t ret = IRQ_NONE;

	/* Read status register */
	status_reg = readw(tpci200->info->interface_regs +
			   TPCI200_STATUS_REG);

	if (status_reg & TPCI200_SLOT_INT_MASK) {
		unhandled_ints = status_reg & TPCI200_SLOT_INT_MASK;
		/* callback to the IRQ handler for the corresponding slot */
		for (i = 0; i < TPCI200_NB_SLOT; i++) {
			if ((tpci200->slots[i].irq != NULL) &&
			    (status_reg & ((TPCI200_A_INT0 | TPCI200_A_INT1) << (2*i)))) {

				ret = tpci200->slots[i].irq->handler(tpci200->slots[i].irq->arg);

				/* Dummy reads */
				readw(tpci200->slots[i].dev->io_space.address +
				      0xC0);
				readw(tpci200->slots[i].dev->io_space.address +
				      0xC2);

				unhandled_ints &= ~(((TPCI200_A_INT0 | TPCI200_A_INT1) << (2*i)));
			}
		}
	}
	/* Interrupt not handled are disabled */
	if (unhandled_ints) {
		for (i = 0; i < TPCI200_NB_SLOT; i++) {
			if (unhandled_ints & ((TPCI200_INT0_EN | TPCI200_INT1_EN) << (2*i))) {
				dev_info(&tpci200->slots[i].dev->dev,
					 "No registered ISR for slot [%d:%d]!. IRQ will be disabled.\n",
					 tpci200->number, i);
				reg_value = readw(
					tpci200->info->interface_regs +
					control_reg[i]);
				reg_value &=
					~(TPCI200_INT0_EN | TPCI200_INT1_EN);
				writew(reg_value,
				       (tpci200->info->interface_regs +
					control_reg[i]));
			}
		}
	}

	return ret;
}

static int tpci200_register(struct tpci200_board *tpci200)
{
	int i;
	int res;
	unsigned long ioidint_base;
	unsigned long mem_base;
	unsigned short slot_ctrl;

	if (pci_enable_device(tpci200->info->pdev) < 0)
		return -ENODEV;

	/* Request IP interface register (Bar 2) */
	res = pci_request_region(tpci200->info->pdev, TPCI200_IP_INTERFACE_BAR,
				 "Carrier IP interface registers");
	if (res) {
		dev_err(&tpci200->info->pdev->dev,
			"(bn 0x%X, sn 0x%X) failed to allocate PCI resource for BAR 2 !",
			tpci200->info->pdev->bus->number,
			tpci200->info->pdev->devfn);
		goto out_disable_pci;
	}

	/* Request IO ID INT space (Bar 3) */
	res = pci_request_region(tpci200->info->pdev,
				 TPCI200_IO_ID_INT_SPACES_BAR,
				 "Carrier IO ID INT space");
	if (res) {
		dev_err(&tpci200->info->pdev->dev,
			"(bn 0x%X, sn 0x%X) failed to allocate PCI resource for BAR 3 !",
			tpci200->info->pdev->bus->number,
			tpci200->info->pdev->devfn);
		goto out_release_ip_space;
	}

	/* Request MEM space (Bar 4) */
	res = pci_request_region(tpci200->info->pdev, TPCI200_MEM8_SPACE_BAR,
				 "Carrier MEM space");
	if (res) {
		dev_err(&tpci200->info->pdev->dev,
			"(bn 0x%X, sn 0x%X) failed to allocate PCI resource for BAR 4!",
			tpci200->info->pdev->bus->number,
			tpci200->info->pdev->devfn);
		goto out_release_ioid_int_space;
	}

	/* Map internal tpci200 driver user space */
	tpci200->info->interface_regs =
		ioremap(pci_resource_start(tpci200->info->pdev,
					   TPCI200_IP_INTERFACE_BAR),
			TPCI200_IFACE_SIZE);
	tpci200->info->ioidint_space =
		ioremap(pci_resource_start(tpci200->info->pdev,
					   TPCI200_IO_ID_INT_SPACES_BAR),
			TPCI200_IOIDINT_SIZE);
	tpci200->info->mem8_space =
		ioremap(pci_resource_start(tpci200->info->pdev,
					   TPCI200_MEM8_SPACE_BAR),
			TPCI200_MEM8_SIZE);

	ioidint_base = pci_resource_start(tpci200->info->pdev,
					  TPCI200_IO_ID_INT_SPACES_BAR);
	mem_base = pci_resource_start(tpci200->info->pdev,
				      TPCI200_MEM8_SPACE_BAR);

	/* Set the default parameters of the slot
	 * INT0 disabled, level sensitive
	 * INT1 disabled, level sensitive
	 * error interrupt disabled
	 * timeout interrupt disabled
	 * recover time disabled
	 * clock rate 8 MHz
	 */
	slot_ctrl = 0;

	/* Set all slot physical address space */
	for (i = 0; i < TPCI200_NB_SLOT; i++) {
		tpci200->slots[i].io_phys.address =
			(void __iomem *)ioidint_base +
			TPCI200_IO_SPACE_OFF + TPCI200_IO_SPACE_GAP*i;
		tpci200->slots[i].io_phys.size = TPCI200_IO_SPACE_SIZE;

		tpci200->slots[i].id_phys.address =
			(void __iomem *)ioidint_base +
			TPCI200_ID_SPACE_OFF + TPCI200_ID_SPACE_GAP*i;
		tpci200->slots[i].id_phys.size = TPCI200_ID_SPACE_SIZE;

		tpci200->slots[i].mem_phys.address =
			(void __iomem *)mem_base + TPCI200_MEM8_GAP*i;
		tpci200->slots[i].mem_phys.size = TPCI200_MEM8_SIZE;

		writew(slot_ctrl, (tpci200->info->interface_regs +
				   control_reg[i]));
	}

	res = request_irq(tpci200->info->pdev->irq,
			  tpci200_interrupt, IRQF_SHARED,
			  KBUILD_MODNAME, (void *) tpci200);
	if (res) {
		dev_err(&tpci200->info->pdev->dev,
			"(bn 0x%X, sn 0x%X) unable to register IRQ !",
			tpci200->info->pdev->bus->number,
			tpci200->info->pdev->devfn);
		goto out_release_ioid_int_space;
	}

	return 0;

out_release_ioid_int_space:
	pci_release_region(tpci200->info->pdev, TPCI200_IO_ID_INT_SPACES_BAR);
out_release_ip_space:
	pci_release_region(tpci200->info->pdev, TPCI200_IP_INTERFACE_BAR);
out_disable_pci:
	pci_disable_device(tpci200->info->pdev);
	return res;
}

static int __tpci200_request_irq(struct tpci200_board *tpci200,
				 struct ipack_device *dev)
{
	unsigned short slot_ctrl;

	/* Set the default parameters of the slot
	 * INT0 enabled, level sensitive
	 * INT1 enabled, level sensitive
	 * error interrupt disabled
	 * timeout interrupt disabled
	 * recover time disabled
	 * clock rate 8 MHz
	 */
	slot_ctrl = TPCI200_INT0_EN | TPCI200_INT1_EN;
	writew(slot_ctrl, (tpci200->info->interface_regs +
			   control_reg[dev->slot]));

	return 0;
}

static void __tpci200_free_irq(struct tpci200_board *tpci200,
			       struct ipack_device *dev)
{
	unsigned short slot_ctrl;

	/* Set the default parameters of the slot
	 * INT0 disabled, level sensitive
	 * INT1 disabled, level sensitive
	 * error interrupt disabled
	 * timeout interrupt disabled
	 * recover time disabled
	 * clock rate 8 MHz
	 */
	slot_ctrl = 0;
	writew(slot_ctrl, (tpci200->info->interface_regs +
			   control_reg[dev->slot]));
}

static int tpci200_free_irq(struct ipack_device *dev)
{
	struct slot_irq *slot_irq;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	if (mutex_lock_interruptible(&tpci200->mutex))
		return -ERESTARTSYS;

	if (tpci200->slots[dev->slot].irq == NULL) {
		mutex_unlock(&tpci200->mutex);
		return -EINVAL;
	}

	__tpci200_free_irq(tpci200, dev);
	slot_irq = tpci200->slots[dev->slot].irq;
	tpci200->slots[dev->slot].irq = NULL;
	kfree(slot_irq);

	mutex_unlock(&tpci200->mutex);
	return 0;
}

static int tpci200_slot_unmap_space(struct ipack_device *dev, int space)
{
	struct ipack_addr_space *virt_addr_space;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	if (mutex_lock_interruptible(&tpci200->mutex))
		return -ERESTARTSYS;

	switch (space) {
	case IPACK_IO_SPACE:
		if (dev->io_space.address == NULL) {
			dev_info(&dev->dev,
				 "Slot [%d:%d] IO space not mapped !\n",
				 dev->bus_nr, dev->slot);
			goto out_unlock;
		}
		virt_addr_space = &dev->io_space;
		break;
	case IPACK_ID_SPACE:
		if (dev->id_space.address == NULL) {
			dev_info(&dev->dev,
				 "Slot [%d:%d] ID space not mapped !\n",
				 dev->bus_nr, dev->slot);
			goto out_unlock;
		}
		virt_addr_space = &dev->id_space;
		break;
	case IPACK_MEM_SPACE:
		if (dev->mem_space.address == NULL) {
			dev_info(&dev->dev,
				 "Slot [%d:%d] MEM space not mapped !\n",
				 dev->bus_nr, dev->slot);
			goto out_unlock;
		}
		virt_addr_space = &dev->mem_space;
		break;
	default:
		dev_err(&dev->dev,
			"Slot [%d:%d] space number %d doesn't exist !\n",
			dev->bus_nr, dev->slot, space);
		mutex_unlock(&tpci200->mutex);
		return -EINVAL;
	}

	iounmap(virt_addr_space->address);

	virt_addr_space->address = NULL;
	virt_addr_space->size = 0;
out_unlock:
	mutex_unlock(&tpci200->mutex);
	return 0;
}

static int tpci200_slot_unregister(struct ipack_device *dev)
{
	struct tpci200_board *tpci200;

	if (dev == NULL)
		return -ENODEV;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	tpci200_free_irq(dev);

	if (mutex_lock_interruptible(&tpci200->mutex))
		return -ERESTARTSYS;

	ipack_device_unregister(dev);
	tpci200->slots[dev->slot].dev = NULL;
	mutex_unlock(&tpci200->mutex);

	return 0;
}

static int tpci200_slot_map_space(struct ipack_device *dev,
				  unsigned int memory_size, int space)
{
	int res = 0;
	unsigned int size_to_map;
	void __iomem *phys_address;
	struct ipack_addr_space *virt_addr_space;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	if (mutex_lock_interruptible(&tpci200->mutex))
		return -ERESTARTSYS;

	switch (space) {
	case IPACK_IO_SPACE:
		if (dev->io_space.address != NULL) {
			dev_err(&dev->dev,
				"Slot [%d:%d] IO space already mapped !\n",
				tpci200->number, dev->slot);
			res = -EINVAL;
			goto out_unlock;
		}
		virt_addr_space = &dev->io_space;

		phys_address = tpci200->slots[dev->slot].io_phys.address;
		size_to_map = tpci200->slots[dev->slot].io_phys.size;
		break;
	case IPACK_ID_SPACE:
		if (dev->id_space.address != NULL) {
			dev_err(&dev->dev,
				"Slot [%d:%d] ID space already mapped !\n",
				tpci200->number, dev->slot);
			res = -EINVAL;
			goto out_unlock;
		}
		virt_addr_space = &dev->id_space;

		phys_address = tpci200->slots[dev->slot].id_phys.address;
		size_to_map = tpci200->slots[dev->slot].id_phys.size;
		break;
	case IPACK_MEM_SPACE:
		if (dev->mem_space.address != NULL) {
			dev_err(&dev->dev,
				"Slot [%d:%d] MEM space already mapped !\n",
				tpci200->number, dev->slot);
			res = -EINVAL;
			goto out_unlock;
		}
		virt_addr_space = &dev->mem_space;

		if (memory_size > tpci200->slots[dev->slot].mem_phys.size) {
			dev_err(&dev->dev,
				"Slot [%d:%d] request is 0x%X memory, only 0x%X available !\n",
				dev->bus_nr, dev->slot, memory_size,
				tpci200->slots[dev->slot].mem_phys.size);
			res = -EINVAL;
			goto out_unlock;
		}

		phys_address = tpci200->slots[dev->slot].mem_phys.address;
		size_to_map = memory_size;
		break;
	default:
		dev_err(&dev->dev, "Slot [%d:%d] space %d doesn't exist !\n",
			tpci200->number, dev->slot, space);
		res = -EINVAL;
		goto out_unlock;
	}

	virt_addr_space->size = size_to_map;
	virt_addr_space->address =
		ioremap((unsigned long)phys_address, size_to_map);

out_unlock:
	mutex_unlock(&tpci200->mutex);
	return res;
}

static int tpci200_request_irq(struct ipack_device *dev, int vector,
			       int (*handler)(void *), void *arg)
{
	int res;
	struct slot_irq *slot_irq;
	struct tpci200_board *tpci200;

	tpci200 = check_slot(dev);
	if (tpci200 == NULL)
		return -EINVAL;

	if (mutex_lock_interruptible(&tpci200->mutex))
		return -ERESTARTSYS;

	if (tpci200->slots[dev->slot].irq != NULL) {
		dev_err(&dev->dev,
			"Slot [%d:%d] IRQ already registered !\n", dev->bus_nr,
			dev->slot);
		res = -EINVAL;
		goto out_unlock;
	}

	slot_irq = kzalloc(sizeof(struct slot_irq), GFP_KERNEL);
	if (slot_irq == NULL) {
		dev_err(&dev->dev,
			"Slot [%d:%d] unable to allocate memory for IRQ !\n",
			dev->bus_nr, dev->slot);
		res = -ENOMEM;
		goto out_unlock;
	}

	/*
	 * WARNING: Setup Interrupt Vector in the IndustryPack device
	 * before an IRQ request.
	 * Read the User Manual of your IndustryPack device to know
	 * where to write the vector in memory.
	 */
	slot_irq->vector = vector;
	slot_irq->handler = handler;
	slot_irq->arg = arg;

	tpci200->slots[dev->slot].irq = slot_irq;
	res = __tpci200_request_irq(tpci200, dev);

out_unlock:
	mutex_unlock(&tpci200->mutex);
	return res;
}

static void tpci200_uninstall(struct tpci200_board *tpci200)
{
	int i;

	for (i = 0; i < TPCI200_NB_SLOT; i++)
		tpci200_slot_unregister(tpci200->slots[i].dev);

	tpci200_unregister(tpci200);
	kfree(tpci200->slots);
}

static struct ipack_bus_ops tpci200_bus_ops = {
	.map_space = tpci200_slot_map_space,
	.unmap_space = tpci200_slot_unmap_space,
	.request_irq = tpci200_request_irq,
	.free_irq = tpci200_free_irq,
	.read8 = tpci200_read8,
	.read16 = tpci200_read16,
	.read32 = tpci200_read32,
	.write8 = tpci200_write8,
	.write16 = tpci200_write16,
	.write32 = tpci200_write32,
	.remove_device = tpci200_slot_unregister,
};

static int tpci200_install(struct tpci200_board *tpci200)
{
	int res;

	tpci200->slots = kzalloc(
		TPCI200_NB_SLOT * sizeof(struct tpci200_slot), GFP_KERNEL);
	if (tpci200->slots == NULL)
		return -ENOMEM;

	res = tpci200_register(tpci200);
	if (res) {
		kfree(tpci200->slots);
		tpci200->slots = NULL;
		return res;
	}

	mutex_init(&tpci200->mutex);
	return 0;
}

static int tpci200_pciprobe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	int ret, i;
	struct tpci200_board *tpci200;

	tpci200 = kzalloc(sizeof(struct tpci200_board), GFP_KERNEL);
	if (!tpci200)
		return -ENOMEM;

	tpci200->info = kzalloc(sizeof(struct tpci200_infos), GFP_KERNEL);
	if (!tpci200->info) {
		kfree(tpci200);
		return -ENOMEM;
	}

	/* Save struct pci_dev pointer */
	tpci200->info->pdev = pdev;
	tpci200->info->id_table = (struct pci_device_id *)id;

	/* register the device and initialize it */
	ret = tpci200_install(tpci200);
	if (ret) {
		dev_err(&pdev->dev, "Error during tpci200 install !\n");
		kfree(tpci200->info);
		kfree(tpci200);
		return -ENODEV;
	}

	/* Register the carrier in the industry pack bus driver */
	tpci200->info->ipack_bus = ipack_bus_register(&pdev->dev,
						      TPCI200_NB_SLOT,
						      &tpci200_bus_ops);
	if (!tpci200->info->ipack_bus) {
		dev_err(&pdev->dev,
			"error registering the carrier on ipack driver\n");
		tpci200_uninstall(tpci200);
		kfree(tpci200->info);
		kfree(tpci200);
		return -EFAULT;
	}

	/* save the bus number given by ipack to logging purpose */
	tpci200->number = tpci200->info->ipack_bus->bus_nr;
	dev_set_drvdata(&pdev->dev, tpci200);
	/* add the registered device in an internal linked list */
	list_add_tail(&tpci200->list, &tpci200_list);

	/*
	 * Give the same IRQ number as the slot number.
	 * The TPCI200 has assigned his own two IRQ by PCI bus driver
	 */
	for (i = 0; i < TPCI200_NB_SLOT; i++)
		tpci200->slots[i].dev =
			ipack_device_register(tpci200->info->ipack_bus, i, i);
	return ret;
}

static void __tpci200_pci_remove(struct tpci200_board *tpci200)
{
	tpci200_uninstall(tpci200);
	list_del(&tpci200->list);
	ipack_bus_unregister(tpci200->info->ipack_bus);
	kfree(tpci200->info);
	kfree(tpci200);
}

static void __devexit tpci200_pci_remove(struct pci_dev *dev)
{
	struct tpci200_board *tpci200, *next;

	/* Search the registered device to uninstall it */
	list_for_each_entry_safe(tpci200, next, &tpci200_list, list) {
		if (tpci200->info->pdev == dev) {
			__tpci200_pci_remove(tpci200);
			break;
		}
	}
}

static DEFINE_PCI_DEVICE_TABLE(tpci200_idtable) = {
	{ TPCI200_VENDOR_ID, TPCI200_DEVICE_ID, TPCI200_SUBVENDOR_ID,
	  TPCI200_SUBDEVICE_ID },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, tpci200_idtable);

static struct pci_driver tpci200_pci_drv = {
	.name = "tpci200",
	.id_table = tpci200_idtable,
	.probe = tpci200_pciprobe,
	.remove = __devexit_p(tpci200_pci_remove),
};

static int __init tpci200_drvr_init_module(void)
{
	return pci_register_driver(&tpci200_pci_drv);
}

static void __exit tpci200_drvr_exit_module(void)
{
	struct tpci200_board *tpci200, *next;

	list_for_each_entry_safe(tpci200, next, &tpci200_list, list)
		__tpci200_pci_remove(tpci200);

	pci_unregister_driver(&tpci200_pci_drv);
}

MODULE_DESCRIPTION("TEWS TPCI-200 device driver");
MODULE_LICENSE("GPL");
module_init(tpci200_drvr_init_module);
module_exit(tpci200_drvr_exit_module);
