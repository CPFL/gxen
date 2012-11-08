/*
 * Arm PrimeCell PL110 Color LCD Controller
 *
 * Copyright (c) 2005-2009 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU LGPL
 */

#include "sysbus.h"
#include "console.h"
#include "framebuffer.h"

#define PL110_CR_EN   0x001
#define PL110_CR_BGR  0x100
#define PL110_CR_BEBO 0x200
#define PL110_CR_BEPO 0x400
#define PL110_CR_PWR  0x800

enum pl110_bppmode
{
    BPP_1,
    BPP_2,
    BPP_4,
    BPP_8,
    BPP_16,
    BPP_32,
    BPP_16_565, /* PL111 only */
    BPP_12      /* PL111 only */
};


/* The Versatile/PB uses a slightly modified PL110 controller.  */
enum pl110_version
{
    PL110,
    PL110_VERSATILE,
    PL111
};

typedef struct {
    SysBusDevice busdev;
    DisplayState *ds;

    int version;
    uint32_t timing[4];
    uint32_t cr;
    uint32_t upbase;
    uint32_t lpbase;
    uint32_t int_status;
    uint32_t int_mask;
    int cols;
    int rows;
    enum pl110_bppmode bpp;
    int invalidate;
    uint32_t mux_ctrl;
    uint32_t pallette[256];
    uint32_t raw_pallette[128];
    qemu_irq irq;
} pl110_state;

static const VMStateDescription vmstate_pl110 = {
    .name = "pl110",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(version, pl110_state),
        VMSTATE_UINT32_ARRAY(timing, pl110_state, 4),
        VMSTATE_UINT32(cr, pl110_state),
        VMSTATE_UINT32(upbase, pl110_state),
        VMSTATE_UINT32(lpbase, pl110_state),
        VMSTATE_UINT32(int_status, pl110_state),
        VMSTATE_UINT32(int_mask, pl110_state),
        VMSTATE_INT32(cols, pl110_state),
        VMSTATE_INT32(rows, pl110_state),
        VMSTATE_UINT32(bpp, pl110_state),
        VMSTATE_INT32(invalidate, pl110_state),
        VMSTATE_UINT32_ARRAY(pallette, pl110_state, 256),
        VMSTATE_UINT32_ARRAY(raw_pallette, pl110_state, 128),
        VMSTATE_UINT32_V(mux_ctrl, pl110_state, 2),
        VMSTATE_END_OF_LIST()
    }
};

static const unsigned char pl110_id[] =
{ 0x10, 0x11, 0x04, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };

/* The Arm documentation (DDI0224C) says the CLDC on the Versatile board
   has a different ID.  However Linux only looks for the normal ID.  */
#if 0
static const unsigned char pl110_versatile_id[] =
{ 0x93, 0x10, 0x04, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };
#else
#define pl110_versatile_id pl110_id
#endif

static const unsigned char pl111_id[] = {
    0x11, 0x11, 0x24, 0x00, 0x0d, 0xf0, 0x05, 0xb1
};

/* Indexed by pl110_version */
static const unsigned char *idregs[] = {
    pl110_id,
    pl110_versatile_id,
    pl111_id
};

#include "pixel_ops.h"

#define BITS 8
#include "pl110_template.h"
#define BITS 15
#include "pl110_template.h"
#define BITS 16
#include "pl110_template.h"
#define BITS 24
#include "pl110_template.h"
#define BITS 32
#include "pl110_template.h"

static int pl110_enabled(pl110_state *s)
{
  return (s->cr & PL110_CR_EN) && (s->cr & PL110_CR_PWR);
}

static void pl110_update_display(void *opaque)
{
    pl110_state *s = (pl110_state *)opaque;
    drawfn* fntable;
    drawfn fn;
    int dest_width;
    int src_width;
    int bpp_offset;
    int first;
    int last;

    if (!pl110_enabled(s))
        return;

    switch (ds_get_bits_per_pixel(s->ds)) {
    case 0:
        return;
    case 8:
        fntable = pl110_draw_fn_8;
        dest_width = 1;
        break;
    case 15:
        fntable = pl110_draw_fn_15;
        dest_width = 2;
        break;
    case 16:
        fntable = pl110_draw_fn_16;
        dest_width = 2;
        break;
    case 24:
        fntable = pl110_draw_fn_24;
        dest_width = 3;
        break;
    case 32:
        fntable = pl110_draw_fn_32;
        dest_width = 4;
        break;
    default:
        fprintf(stderr, "pl110: Bad color depth\n");
        exit(1);
    }
    if (s->cr & PL110_CR_BGR)
        bpp_offset = 0;
    else
        bpp_offset = 24;

    if ((s->version != PL111) && (s->bpp == BPP_16)) {
        /* The PL110's native 16 bit mode is 5551; however
         * most boards with a PL110 implement an external
         * mux which allows bits to be reshuffled to give
         * 565 format. The mux is typically controlled by
         * an external system register.
         * This is controlled by a GPIO input pin
         * so boards can wire it up to their register.
         *
         * The PL111 straightforwardly implements both
         * 5551 and 565 under control of the bpp field
         * in the LCDControl register.
         */
        switch (s->mux_ctrl) {
        case 3: /* 565 BGR */
            bpp_offset = (BPP_16_565 - BPP_16);
            break;
        case 1: /* 5551 */
            break;
        case 0: /* 888; also if we have loaded vmstate from an old version */
        case 2: /* 565 RGB */
        default:
            /* treat as 565 but honour BGR bit */
            bpp_offset += (BPP_16_565 - BPP_16);
            break;
        }
    }

    if (s->cr & PL110_CR_BEBO)
        fn = fntable[s->bpp + 8 + bpp_offset];
    else if (s->cr & PL110_CR_BEPO)
        fn = fntable[s->bpp + 16 + bpp_offset];
    else
        fn = fntable[s->bpp + bpp_offset];

    src_width = s->cols;
    switch (s->bpp) {
    case BPP_1:
        src_width >>= 3;
        break;
    case BPP_2:
        src_width >>= 2;
        break;
    case BPP_4:
        src_width >>= 1;
        break;
    case BPP_8:
        break;
    case BPP_16:
    case BPP_16_565:
    case BPP_12:
        src_width <<= 1;
        break;
    case BPP_32:
        src_width <<= 2;
        break;
    }
    dest_width *= s->cols;
    first = 0;
    framebuffer_update_display(s->ds,
                               s->upbase, s->cols, s->rows,
                               src_width, dest_width, 0,
                               s->invalidate,
                               fn, s->pallette,
                               &first, &last);
    if (first >= 0) {
        dpy_update(s->ds, 0, first, s->cols, last - first + 1);
    }
    s->invalidate = 0;
}

static void pl110_invalidate_display(void * opaque)
{
    pl110_state *s = (pl110_state *)opaque;
    s->invalidate = 1;
    if (pl110_enabled(s)) {
        qemu_console_resize(s->ds, s->cols, s->rows);
    }
}

static void pl110_update_pallette(pl110_state *s, int n)
{
    int i;
    uint32_t raw;
    unsigned int r, g, b;

    raw = s->raw_pallette[n];
    n <<= 1;
    for (i = 0; i < 2; i++) {
        r = (raw & 0x1f) << 3;
        raw >>= 5;
        g = (raw & 0x1f) << 3;
        raw >>= 5;
        b = (raw & 0x1f) << 3;
        /* The I bit is ignored.  */
        raw >>= 6;
        switch (ds_get_bits_per_pixel(s->ds)) {
        case 8:
            s->pallette[n] = rgb_to_pixel8(r, g, b);
            break;
        case 15:
            s->pallette[n] = rgb_to_pixel15(r, g, b);
            break;
        case 16:
            s->pallette[n] = rgb_to_pixel16(r, g, b);
            break;
        case 24:
        case 32:
            s->pallette[n] = rgb_to_pixel32(r, g, b);
            break;
        }
        n++;
    }
}

static void pl110_resize(pl110_state *s, int width, int height)
{
    if (width != s->cols || height != s->rows) {
        if (pl110_enabled(s)) {
            qemu_console_resize(s->ds, width, height);
        }
    }
    s->cols = width;
    s->rows = height;
}

/* Update interrupts.  */
static void pl110_update(pl110_state *s)
{
  /* TODO: Implement interrupts.  */
}

static uint32_t pl110_read(void *opaque, target_phys_addr_t offset)
{
    pl110_state *s = (pl110_state *)opaque;

    if (offset >= 0xfe0 && offset < 0x1000) {
        return idregs[s->version][(offset - 0xfe0) >> 2];
    }
    if (offset >= 0x200 && offset < 0x400) {
        return s->raw_pallette[(offset - 0x200) >> 2];
    }
    switch (offset >> 2) {
    case 0: /* LCDTiming0 */
        return s->timing[0];
    case 1: /* LCDTiming1 */
        return s->timing[1];
    case 2: /* LCDTiming2 */
        return s->timing[2];
    case 3: /* LCDTiming3 */
        return s->timing[3];
    case 4: /* LCDUPBASE */
        return s->upbase;
    case 5: /* LCDLPBASE */
        return s->lpbase;
    case 6: /* LCDIMSC */
        if (s->version != PL110) {
            return s->cr;
        }
        return s->int_mask;
    case 7: /* LCDControl */
        if (s->version != PL110) {
            return s->int_mask;
        }
        return s->cr;
    case 8: /* LCDRIS */
        return s->int_status;
    case 9: /* LCDMIS */
        return s->int_status & s->int_mask;
    case 11: /* LCDUPCURR */
        /* TODO: Implement vertical refresh.  */
        return s->upbase;
    case 12: /* LCDLPCURR */
        return s->lpbase;
    default:
        hw_error("pl110_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void pl110_write(void *opaque, target_phys_addr_t offset,
                        uint32_t val)
{
    pl110_state *s = (pl110_state *)opaque;
    int n;

    /* For simplicity invalidate the display whenever a control register
       is writen to.  */
    s->invalidate = 1;
    if (offset >= 0x200 && offset < 0x400) {
        /* Pallette.  */
        n = (offset - 0x200) >> 2;
        s->raw_pallette[(offset - 0x200) >> 2] = val;
        pl110_update_pallette(s, n);
        return;
    }
    switch (offset >> 2) {
    case 0: /* LCDTiming0 */
        s->timing[0] = val;
        n = ((val & 0xfc) + 4) * 4;
        pl110_resize(s, n, s->rows);
        break;
    case 1: /* LCDTiming1 */
        s->timing[1] = val;
        n = (val & 0x3ff) + 1;
        pl110_resize(s, s->cols, n);
        break;
    case 2: /* LCDTiming2 */
        s->timing[2] = val;
        break;
    case 3: /* LCDTiming3 */
        s->timing[3] = val;
        break;
    case 4: /* LCDUPBASE */
        s->upbase = val;
        break;
    case 5: /* LCDLPBASE */
        s->lpbase = val;
        break;
    case 6: /* LCDIMSC */
        if (s->version != PL110) {
            goto control;
        }
    imsc:
        s->int_mask = val;
        pl110_update(s);
        break;
    case 7: /* LCDControl */
        if (s->version != PL110) {
            goto imsc;
        }
    control:
        s->cr = val;
        s->bpp = (val >> 1) & 7;
        if (pl110_enabled(s)) {
            qemu_console_resize(s->ds, s->cols, s->rows);
        }
        break;
    case 10: /* LCDICR */
        s->int_status &= ~val;
        pl110_update(s);
        break;
    default:
        hw_error("pl110_write: Bad offset %x\n", (int)offset);
    }
}

static CPUReadMemoryFunc * const pl110_readfn[] = {
   pl110_read,
   pl110_read,
   pl110_read
};

static CPUWriteMemoryFunc * const pl110_writefn[] = {
   pl110_write,
   pl110_write,
   pl110_write
};

static void pl110_mux_ctrl_set(void *opaque, int line, int level)
{
    pl110_state *s = (pl110_state *)opaque;
    s->mux_ctrl = level;
}

static int pl110_init(SysBusDevice *dev)
{
    pl110_state *s = FROM_SYSBUS(pl110_state, dev);
    int iomemtype;

    iomemtype = cpu_register_io_memory(pl110_readfn,
                                       pl110_writefn, s,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, iomemtype);
    sysbus_init_irq(dev, &s->irq);
    qdev_init_gpio_in(&s->busdev.qdev, pl110_mux_ctrl_set, 1);
    s->ds = graphic_console_init(pl110_update_display,
                                 pl110_invalidate_display,
                                 NULL, NULL, s);
    return 0;
}

static int pl110_versatile_init(SysBusDevice *dev)
{
    pl110_state *s = FROM_SYSBUS(pl110_state, dev);
    s->version = PL110_VERSATILE;
    return pl110_init(dev);
}

static int pl111_init(SysBusDevice *dev)
{
    pl110_state *s = FROM_SYSBUS(pl110_state, dev);
    s->version = PL111;
    return pl110_init(dev);
}

static SysBusDeviceInfo pl110_info = {
    .init = pl110_init,
    .qdev.name = "pl110",
    .qdev.size = sizeof(pl110_state),
    .qdev.vmsd = &vmstate_pl110,
    .qdev.no_user = 1,
};

static SysBusDeviceInfo pl110_versatile_info = {
    .init = pl110_versatile_init,
    .qdev.name = "pl110_versatile",
    .qdev.size = sizeof(pl110_state),
    .qdev.vmsd = &vmstate_pl110,
    .qdev.no_user = 1,
};

static SysBusDeviceInfo pl111_info = {
    .init = pl111_init,
    .qdev.name = "pl111",
    .qdev.size = sizeof(pl110_state),
    .qdev.vmsd = &vmstate_pl110,
    .qdev.no_user = 1,
};

static void pl110_register_devices(void)
{
    sysbus_register_withprop(&pl110_info);
    sysbus_register_withprop(&pl110_versatile_info);
    sysbus_register_withprop(&pl111_info);
}

device_init(pl110_register_devices)
