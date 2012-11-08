#include "config-host.h"
#include "trace.h"
#include "ui/qemu-spice.h"
#include <spice.h>
#include <spice-experimental.h>

#include "osdep.h"

#define dprintf(_scd, _level, _fmt, ...)                                \
    do {                                                                \
        static unsigned __dprintf_counter = 0;                          \
        if (_scd->debug >= _level) {                                    \
            fprintf(stderr, "scd: %3d: " _fmt, ++__dprintf_counter, ## __VA_ARGS__);\
        }                                                               \
    } while (0)

#define VMC_MAX_HOST_WRITE    2048

typedef struct SpiceCharDriver {
    CharDriverState*      chr;
    SpiceCharDeviceInstance     sin;
    char                  *subtype;
    bool                  active;
    uint8_t               *buffer;
    uint8_t               *datapos;
    ssize_t               bufsize, datalen;
    uint32_t              debug;
} SpiceCharDriver;

static int vmc_write(SpiceCharDeviceInstance *sin, const uint8_t *buf, int len)
{
    SpiceCharDriver *scd = container_of(sin, SpiceCharDriver, sin);
    ssize_t out = 0;
    ssize_t last_out;
    uint8_t* p = (uint8_t*)buf;

    while (len > 0) {
        last_out = MIN(len, VMC_MAX_HOST_WRITE);
        if (qemu_chr_be_can_write(scd->chr) < last_out) {
            break;
        }
        qemu_chr_be_write(scd->chr, p, last_out);
        out += last_out;
        len -= last_out;
        p += last_out;
    }

    dprintf(scd, 3, "%s: %zu/%zd\n", __func__, out, len + out);
    trace_spice_vmc_write(out, len + out);
    return out;
}

static int vmc_read(SpiceCharDeviceInstance *sin, uint8_t *buf, int len)
{
    SpiceCharDriver *scd = container_of(sin, SpiceCharDriver, sin);
    int bytes = MIN(len, scd->datalen);

    dprintf(scd, 2, "%s: %p %d/%d/%zd\n", __func__, scd->datapos, len, bytes, scd->datalen);
    if (bytes > 0) {
        memcpy(buf, scd->datapos, bytes);
        scd->datapos += bytes;
        scd->datalen -= bytes;
        assert(scd->datalen >= 0);
        if (scd->datalen == 0) {
            scd->datapos = 0;
        }
    }
    trace_spice_vmc_read(bytes, len);
    return bytes;
}

static void vmc_state(SpiceCharDeviceInstance *sin, int connected)
{
    SpiceCharDriver *scd = container_of(sin, SpiceCharDriver, sin);

#if SPICE_SERVER_VERSION < 0x000901
    /*
     * spice-server calls the state callback for the agent channel when the
     * spice client connects / disconnects. Given that not the client but
     * the server is doing the parsing of the messages this is wrong as the
     * server is still listening. Worse, this causes the parser in the server
     * to go out of sync, so we ignore state calls for subtype vdagent
     * spicevmc chardevs. For the full story see:
     * http://lists.freedesktop.org/archives/spice-devel/2011-July/004837.html
     */
    if (strcmp(sin->subtype, "vdagent") == 0) {
        return;
    }
#endif

    if ((scd->chr->opened && connected) ||
        (!scd->chr->opened && !connected)) {
        return;
    }

    qemu_chr_be_event(scd->chr,
                      connected ? CHR_EVENT_OPENED : CHR_EVENT_CLOSED);
}

static SpiceCharDeviceInterface vmc_interface = {
    .base.type          = SPICE_INTERFACE_CHAR_DEVICE,
    .base.description   = "spice virtual channel char device",
    .base.major_version = SPICE_INTERFACE_CHAR_DEVICE_MAJOR,
    .base.minor_version = SPICE_INTERFACE_CHAR_DEVICE_MINOR,
    .state              = vmc_state,
    .write              = vmc_write,
    .read               = vmc_read,
};


static void vmc_register_interface(SpiceCharDriver *scd)
{
    if (scd->active) {
        return;
    }
    dprintf(scd, 1, "%s\n", __func__);
    scd->sin.base.sif = &vmc_interface.base;
    qemu_spice_add_interface(&scd->sin.base);
    scd->active = true;
    trace_spice_vmc_register_interface(scd);
}

static void vmc_unregister_interface(SpiceCharDriver *scd)
{
    if (!scd->active) {
        return;
    }
    dprintf(scd, 1, "%s\n", __func__);
    spice_server_remove_interface(&scd->sin.base);
    scd->active = false;
    trace_spice_vmc_unregister_interface(scd);
}


static int spice_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    SpiceCharDriver *s = chr->opaque;

    dprintf(s, 2, "%s: %d\n", __func__, len);
    vmc_register_interface(s);
    assert(s->datalen == 0);
    if (s->bufsize < len) {
        s->bufsize = len;
        s->buffer = g_realloc(s->buffer, s->bufsize);
    }
    memcpy(s->buffer, buf, len);
    s->datapos = s->buffer;
    s->datalen = len;
    spice_server_char_device_wakeup(&s->sin);
    return len;
}

static void spice_chr_close(struct CharDriverState *chr)
{
    SpiceCharDriver *s = chr->opaque;

    printf("%s\n", __func__);
    vmc_unregister_interface(s);
    g_free(s);
}

static void spice_chr_guest_open(struct CharDriverState *chr)
{
    SpiceCharDriver *s = chr->opaque;
    vmc_register_interface(s);
}

static void spice_chr_guest_close(struct CharDriverState *chr)
{
    SpiceCharDriver *s = chr->opaque;
    vmc_unregister_interface(s);
}

static void print_allowed_subtypes(void)
{
    const char** psubtype;
    int i;

    fprintf(stderr, "allowed names: ");
    for(i=0, psubtype = spice_server_char_device_recognized_subtypes();
        *psubtype != NULL; ++psubtype, ++i) {
        if (i == 0) {
            fprintf(stderr, "%s", *psubtype);
        } else {
            fprintf(stderr, ", %s", *psubtype);
        }
    }
    fprintf(stderr, "\n");
}

int qemu_chr_open_spice(QemuOpts *opts, CharDriverState **_chr)
{
    CharDriverState *chr;
    SpiceCharDriver *s;
    const char* name = qemu_opt_get(opts, "name");
    uint32_t debug = qemu_opt_get_number(opts, "debug", 0);
    const char** psubtype = spice_server_char_device_recognized_subtypes();
    const char *subtype = NULL;

    if (name == NULL) {
        fprintf(stderr, "spice-qemu-char: missing name parameter\n");
        print_allowed_subtypes();
        return -EINVAL;
    }
    for(;*psubtype != NULL; ++psubtype) {
        if (strcmp(name, *psubtype) == 0) {
            subtype = *psubtype;
            break;
        }
    }
    if (subtype == NULL) {
        fprintf(stderr, "spice-qemu-char: unsupported name\n");
        print_allowed_subtypes();
        return -EINVAL;
    }

    chr = g_malloc0(sizeof(CharDriverState));
    s = g_malloc0(sizeof(SpiceCharDriver));
    s->chr = chr;
    s->debug = debug;
    s->active = false;
    s->sin.subtype = subtype;
    chr->opaque = s;
    chr->chr_write = spice_chr_write;
    chr->chr_close = spice_chr_close;
    chr->chr_guest_open = spice_chr_guest_open;
    chr->chr_guest_close = spice_chr_guest_close;

#if SPICE_SERVER_VERSION < 0x000901
    /* See comment in vmc_state() */
    if (strcmp(subtype, "vdagent") == 0) {
        qemu_chr_generic_open(chr);
    }
#endif

    *_chr = chr;
    return 0;
}
