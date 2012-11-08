#include <stdint.h>
#include <xen/io/fbif.h>
#include <xen/io/kbdif.h>
#include <semaphore.h>
#include <sched.h>
#include <fbfront.h>
#include <hw/hw.h>
#include <hw/pc.h>
#include <console.h>

#include <hw/xenfb.h>

typedef struct XenFBState {
    struct semaphore kbd_sem;
    struct kbdfront_dev *kbd_dev;
    struct fbfront_dev *fb_dev;
    void *nonshared_vram;
    DisplayState *ds;
} XenFBState;

XenFBState *xs;

static char *kbd_path, *fb_path;
static void *vga_vram;

static unsigned char linux2scancode[KEY_MAX + 1];
static DisplayChangeListener *dcl;

extern uint32_t vga_ram_size;

int xenfb_connect_vkbd(const char *path)
{
    kbd_path = strdup(path);
    return 0;
}

int xenfb_connect_vfb(const char *path)
{
    fb_path = strdup(path);
    return 0;
}

static void xenfb_pv_update(DisplayState *ds, int x, int y, int w, int h)
{
    XenFBState *xs = ds->opaque;
    struct fbfront_dev *fb_dev = xs->fb_dev;
    if (!fb_dev)
        return;
    fbfront_update(fb_dev, x, y, w, h);
}

static void xenfb_pv_resize(DisplayState *ds)
{
    XenFBState *xs = ds->opaque;
    struct fbfront_dev *fb_dev = xs->fb_dev;
    int offset;

    fprintf(stderr,"resize to %dx%d@%d, %d required\n", ds_get_width(ds), ds_get_height(ds), ds_get_bits_per_pixel(ds), ds_get_linesize(ds));
    if (!fb_dev)
        return;
    if (!(ds->surface->flags & QEMU_ALLOCATED_FLAG))
        offset = ((void *) ds_get_data(ds)) - vga_vram;
    else
        offset = vga_ram_size;
    fbfront_resize(fb_dev, ds_get_width(ds), ds_get_height(ds), ds_get_linesize(ds), ds_get_bits_per_pixel(ds), offset);
}

static void xenfb_pv_setdata(DisplayState *ds)
{
    XenFBState *xs = ds->opaque;
    struct fbfront_dev *fb_dev = xs->fb_dev;
    int offset = ((void *) ds_get_data(ds)) - vga_vram;
    if (!fb_dev)
        return;
    fbfront_resize(fb_dev, ds_get_width(ds), ds_get_height(ds), ds_get_linesize(ds), ds_get_bits_per_pixel(ds), offset);
}

static void xenfb_pv_refresh(DisplayState *ds)
{
    vga_hw_update();
}

static void xenfb_fb_handler(void *opaque)
{
#define FB_NUM_BATCH 4
    union xenfb_in_event buf[FB_NUM_BATCH];
    int n, i;
    XenFBState *xs = opaque;
    DisplayState *ds = xs->ds;

    n = fbfront_receive(xs->fb_dev, buf, FB_NUM_BATCH);
    for (i = 0; i < n; i++) {
        switch (buf[i].type) {
        case XENFB_TYPE_REFRESH_PERIOD:
            if (buf[i].refresh_period.period == XENFB_NO_REFRESH) {
                /* Sleeping interval */
                dcl->idle = 1;
                dcl->gui_timer_interval = 500;
            } else {
                /* Set interval */
                dcl->idle = 0;
                dcl->gui_timer_interval = buf[i].refresh_period.period;
            }
        default:
            /* ignore unknown events */
            break;
        }
    }
}

static void xenfb_kbd_handler(void *opaque)
{
#define KBD_NUM_BATCH 64
    union xenkbd_in_event buf[KBD_NUM_BATCH];
    int n, i;
    XenFBState *xs = opaque;
    DisplayState *s = xs->ds;
    static int buttons;
    static int x, y;

    n = kbdfront_receive(xs->kbd_dev, buf, KBD_NUM_BATCH);
    for (i = 0; i < n; i++) {
        switch (buf[i].type) {

            case XENKBD_TYPE_MOTION:
                fprintf(stderr, "FB backend sent us relative mouse motion event!\n");
                break;

            case XENKBD_TYPE_POS:
            {
                int new_x = buf[i].pos.abs_x;
                int new_y = buf[i].pos.abs_y;
                if (new_x >= ds_get_width(s))
                    new_x = ds_get_width(s) - 1;
                if (new_y >= ds_get_height(s))
                    new_y = ds_get_height(s) - 1;
                if (kbd_mouse_is_absolute()) {
                    kbd_mouse_event(
                            new_x * 0x7FFF / (ds_get_width(s) - 1),
                            new_y * 0x7FFF / (ds_get_height(s) - 1),
                            buf[i].pos.rel_z,
                            buttons);
                } else {
                    kbd_mouse_event(
                            new_x - x,
                            new_y - y,
                            buf[i].pos.rel_z,
                            buttons);
                }
                x = new_x;
                y = new_y;
                break;
            }

            case XENKBD_TYPE_KEY:
            {
                int keycode = buf[i].key.keycode;
                int button = 0;

                if (keycode == BTN_LEFT)
                    button = MOUSE_EVENT_LBUTTON;
                else if (keycode == BTN_RIGHT)
                    button = MOUSE_EVENT_RBUTTON;
                else if (keycode == BTN_MIDDLE)
                    button = MOUSE_EVENT_MBUTTON;

                if (button) {
                    if (buf[i].key.pressed)
                        buttons |=  button;
                    else
                        buttons &= ~button;
                    if (kbd_mouse_is_absolute())
                        kbd_mouse_event(
                                x * 0x7FFF / (ds_get_width(s) - 1),
                                y * 0x7FFF / (ds_get_height(s) - 1),
                                0,
                                buttons);
                    else
                        kbd_mouse_event(0, 0, 0, buttons);
                } else {
                    int scancode = linux2scancode[keycode];
                    if (!scancode) {
                        fprintf(stderr, "Can't convert keycode %x to scancode\n", keycode);
                        break;
                    }
                    if (scancode & 0x80) {
                        kbd_put_keycode(0xe0);
                        scancode &= 0x7f;
                    }
                    if (!buf[i].key.pressed)
                        scancode |= 0x80;
                    kbd_put_keycode(scancode);
                }
                break;
            }
        }
    }
}

static void kbdfront_thread(void *p)
{
    int scancode, keycode;
    XenFBState *xs = p;
    xs->kbd_dev = init_kbdfront(kbd_path, 1);
    if (!xs->kbd_dev) {
        fprintf(stderr,"can't open keyboard\n");
        exit(1);
    }
    up(&xs->kbd_sem);
    for (scancode = 0; scancode < 128; scancode++) {
        keycode = atkbd_set2_keycode[atkbd_unxlate_table[scancode]];
        linux2scancode[keycode] = scancode;
        keycode = atkbd_set2_keycode[atkbd_unxlate_table[scancode] | 0x80];
        linux2scancode[keycode] = scancode | 0x80;
    }
}


static DisplaySurface* xenfb_create_displaysurface(int width, int height)
{
    DisplaySurface *surface = (DisplaySurface*) qemu_mallocz(sizeof(DisplaySurface));
    if (surface == NULL) {
        fprintf(stderr, "xenfb_create_displaysurface: malloc failed\n");
        exit(1);
    }

    surface->width = width;
    surface->height = height;
    surface->linesize = width * 4;
    surface->pf = qemu_default_pixelformat(32);
#ifdef WORDS_BIGENDIAN
    surface->flags = QEMU_ALLOCATED_FLAG | QEMU_BIG_ENDIAN_FLAG;
#else
    surface->flags = QEMU_ALLOCATED_FLAG;
#endif
    surface->data = xs->nonshared_vram;

    return surface;
}

static DisplaySurface* xenfb_resize_displaysurface(DisplaySurface *surface,
                                          int width, int height)
{
    surface->width = width;
    surface->height = height;
    surface->linesize = width * 4;
    surface->pf = qemu_default_pixelformat(32);
#ifdef WORDS_BIGENDIAN
    surface->flags = QEMU_ALLOCATED_FLAG | QEMU_BIG_ENDIAN_FLAG;
#else
    surface->flags = QEMU_ALLOCATED_FLAG;
#endif
    surface->data = xs->nonshared_vram;

    return surface;
}

static void xenfb_free_displaysurface(DisplaySurface *surface)
{
    if (surface == NULL)
        return;
    qemu_free(surface);
}

static void xenfb_pv_display_allocator(void)
{
    DisplaySurface *ds;
    DisplayAllocator *da = qemu_mallocz(sizeof(DisplayAllocator));
    da->create_displaysurface = xenfb_create_displaysurface;
    da->resize_displaysurface = xenfb_resize_displaysurface;
    da->free_displaysurface = xenfb_free_displaysurface;
    if (register_displayallocator(xs->ds, da) != da) {
        fprintf(stderr, "xenfb_pv_display_allocator: could not register DisplayAllocator\n");
        exit(1);
    }

    xs->nonshared_vram = qemu_memalign(XC_PAGE_SIZE, vga_ram_size);
    if (!xs->nonshared_vram) {
        fprintf(stderr, "xenfb_pv_display_allocator: could not allocate nonshared_vram\n");
        exit(1);
    }
    /* Touch the pages before sharing them */
    memset(xs->nonshared_vram, 0xff, vga_ram_size);

    ds = xenfb_create_displaysurface(ds_get_width(xs->ds), ds_get_height(xs->ds));
    defaultallocator_free_displaysurface(xs->ds->surface);
    xs->ds->surface = ds;
}

int xenfb_pv_display_init(DisplayState *ds)
{
    struct fbfront_dev *fb_dev;
    int kbd_fd, fb_fd;
    unsigned long *mfns;
    int offset = 0;
    int i;
    int n = vga_ram_size / XC_PAGE_SIZE;

    if (!fb_path || !kbd_path)
        return -1;

    xs = qemu_mallocz(sizeof(XenFBState));
    if (!xs)
        return -1;

    init_SEMAPHORE(&xs->kbd_sem, 0);
    xs->ds = ds;

    xenfb_pv_display_allocator();

    create_thread("kbdfront", kbdfront_thread, (void*) xs);

    dcl = qemu_mallocz(sizeof(DisplayChangeListener));
    if (!dcl)
        exit(1);
    ds->opaque = xs;
    dcl->dpy_update = xenfb_pv_update;
    dcl->dpy_resize = xenfb_pv_resize;
    dcl->dpy_setdata = xenfb_pv_setdata;
    dcl->dpy_refresh = xenfb_pv_refresh;
    register_displaychangelistener(ds, dcl);

    mfns = malloc(2 * n * sizeof(*mfns));
    for (i = 0; i < n; i++)
        mfns[i] = virtual_to_mfn(vga_vram + i * XC_PAGE_SIZE);
    for (i = 0; i < n; i++)
        mfns[n + i] = virtual_to_mfn(xs->nonshared_vram + i * XC_PAGE_SIZE);

    fb_dev = init_fbfront(fb_path, mfns, ds_get_width(ds), ds_get_height(ds), ds_get_bits_per_pixel(ds), ds_get_linesize(ds), 2 * n);
    free(mfns);
    if (!fb_dev) {
        fprintf(stderr,"can't open frame buffer\n");
        exit(1);
    }
    free(fb_path);

    if (!(ds->surface->flags & QEMU_ALLOCATED_FLAG)) {
        offset = (void*) ds_get_data(ds) - vga_vram;
    } else {
        offset = vga_ram_size;
    }
    if (offset)
        fbfront_resize(fb_dev, ds_get_width(ds), ds_get_height(ds), ds_get_linesize(ds), ds_get_bits_per_pixel(ds), offset);

    down(&xs->kbd_sem);
    free(kbd_path);

    kbd_fd = kbdfront_open(xs->kbd_dev);
    qemu_set_fd_handler(kbd_fd, xenfb_kbd_handler, NULL, xs);

    fb_fd = fbfront_open(fb_dev);
    qemu_set_fd_handler(fb_fd, xenfb_fb_handler, NULL, xs);

    xs->fb_dev = fb_dev;
    return 0;
}

void xenfb_pv_display_vram(void *data)
{
    vga_vram = data;
}

