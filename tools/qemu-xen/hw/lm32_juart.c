/*
 *  LatticeMico32 JTAG UART model.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw.h"
#include "sysbus.h"
#include "trace.h"
#include "qemu-char.h"

#include "lm32_juart.h"

enum {
    LM32_JUART_MIN_SAVE_VERSION = 0,
    LM32_JUART_CURRENT_SAVE_VERSION = 0,
    LM32_JUART_MAX_SAVE_VERSION = 0,
};

enum {
    JTX_FULL = (1<<8),
};

enum {
    JRX_FULL = (1<<8),
};

struct LM32JuartState {
    SysBusDevice busdev;
    CharDriverState *chr;

    uint32_t jtx;
    uint32_t jrx;
};
typedef struct LM32JuartState LM32JuartState;

uint32_t lm32_juart_get_jtx(DeviceState *d)
{
    LM32JuartState *s = container_of(d, LM32JuartState, busdev.qdev);

    trace_lm32_juart_get_jtx(s->jtx);
    return s->jtx;
}

uint32_t lm32_juart_get_jrx(DeviceState *d)
{
    LM32JuartState *s = container_of(d, LM32JuartState, busdev.qdev);

    trace_lm32_juart_get_jrx(s->jrx);
    return s->jrx;
}

void lm32_juart_set_jtx(DeviceState *d, uint32_t jtx)
{
    LM32JuartState *s = container_of(d, LM32JuartState, busdev.qdev);
    unsigned char ch = jtx & 0xff;

    trace_lm32_juart_set_jtx(s->jtx);

    s->jtx = jtx;
    if (s->chr) {
        qemu_chr_fe_write(s->chr, &ch, 1);
    }
}

void lm32_juart_set_jrx(DeviceState *d, uint32_t jtx)
{
    LM32JuartState *s = container_of(d, LM32JuartState, busdev.qdev);

    trace_lm32_juart_set_jrx(s->jrx);
    s->jrx &= ~JRX_FULL;
}

static void juart_rx(void *opaque, const uint8_t *buf, int size)
{
    LM32JuartState *s = opaque;

    s->jrx = *buf | JRX_FULL;
}

static int juart_can_rx(void *opaque)
{
    LM32JuartState *s = opaque;

    return !(s->jrx & JRX_FULL);
}

static void juart_event(void *opaque, int event)
{
}

static void juart_reset(DeviceState *d)
{
    LM32JuartState *s = container_of(d, LM32JuartState, busdev.qdev);

    s->jtx = 0;
    s->jrx = 0;
}

static int lm32_juart_init(SysBusDevice *dev)
{
    LM32JuartState *s = FROM_SYSBUS(typeof(*s), dev);

    s->chr = qdev_init_chardev(&dev->qdev);
    if (s->chr) {
        qemu_chr_add_handlers(s->chr, juart_can_rx, juart_rx, juart_event, s);
    }

    return 0;
}

static const VMStateDescription vmstate_lm32_juart = {
    .name = "lm32-juart",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(jtx, LM32JuartState),
        VMSTATE_UINT32(jrx, LM32JuartState),
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo lm32_juart_info = {
    .init = lm32_juart_init,
    .qdev.name  = "lm32-juart",
    .qdev.size  = sizeof(LM32JuartState),
    .qdev.vmsd  = &vmstate_lm32_juart,
    .qdev.reset = juart_reset,
};

static void lm32_juart_register(void)
{
    sysbus_register_withprop(&lm32_juart_info);
}

device_init(lm32_juart_register)
