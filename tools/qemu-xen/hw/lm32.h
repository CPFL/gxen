
#include "qemu-common.h"

static inline DeviceState *lm32_pic_init(qemu_irq cpu_irq)
{
    DeviceState *dev;
    SysBusDevice *d;

    dev = qdev_create(NULL, "lm32-pic");
    qdev_init_nofail(dev);
    d = sysbus_from_qdev(dev);
    sysbus_connect_irq(d, 0, cpu_irq);

    return dev;
}

static inline DeviceState *lm32_juart_init(void)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "lm32-juart");
    qdev_init_nofail(dev);

    return dev;
}
