/*
 * Linux host USB redirector
 *
 * Copyright (c) 2005 Fabrice Bellard
 *
 * Copyright (c) 2008 Max Krasnyansky
 *      Support for host device auto connect & disconnect
 *      Major rewrite to support fully async operation
 *
 * Copyright 2008 TJ <linux@tjworld.net>
 *      Added flexible support for /dev/bus/usb /sys/bus/usb/devices in addition
 *      to the legacy /proc/bus/usb USB device discovery and handling
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

#include "qemu-common.h"
#include "qemu-timer.h"
#include "monitor.h"
#include "sysemu.h"
#include "trace.h"

#include <dirent.h>
#include <sys/ioctl.h>

#include <linux/usbdevice_fs.h>
#include <linux/version.h>
#include "hw/usb.h"

/* We redefine it to avoid version problems */
struct usb_ctrltransfer {
    uint8_t  bRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
    uint32_t timeout;
    void *data;
};

typedef int USBScanFunc(void *opaque, int bus_num, int addr, const char *port,
                        int class_id, int vendor_id, int product_id,
                        const char *product_name, int speed);

//#define DEBUG

#ifdef DEBUG
#define DPRINTF printf
#else
#define DPRINTF(...)
#endif

#define USBDBG_DEVOPENED "husb: opened %s/devices\n"

#define USBPROCBUS_PATH "/proc/bus/usb"
#define PRODUCT_NAME_SZ 32
#define MAX_ENDPOINTS 15
#define MAX_PORTLEN 16
#define USBDEVBUS_PATH "/dev/bus/usb"
#define USBSYSBUS_PATH "/sys/bus/usb"

static char *usb_host_device_path;

#define USB_FS_NONE 0
#define USB_FS_PROC 1
#define USB_FS_DEV 2
#define USB_FS_SYS 3

static int usb_fs_type;

/* endpoint association data */
#define ISO_FRAME_DESC_PER_URB 32
#define INVALID_EP_TYPE 255

/* devio.c limits single requests to 16k */
#define MAX_USBFS_BUFFER_SIZE 16384

typedef struct AsyncURB AsyncURB;

struct endp_data {
    uint8_t type;
    uint8_t halted;
    uint8_t iso_started;
    AsyncURB *iso_urb;
    int iso_urb_idx;
    int iso_buffer_used;
    int max_packet_size;
    int inflight;
};

struct USBAutoFilter {
    uint32_t bus_num;
    uint32_t addr;
    char     *port;
    uint32_t vendor_id;
    uint32_t product_id;
};

typedef struct USBHostDevice {
    USBDevice dev;
    int       fd;
    int       hub_fd;
    int       hub_port;

    uint8_t   descr[8192];
    int       descr_len;
    int       configuration;
    int       ninterfaces;
    int       closing;
    uint32_t  iso_urb_count;
    Notifier  exit;

    struct endp_data ep_in[MAX_ENDPOINTS];
    struct endp_data ep_out[MAX_ENDPOINTS];
    QLIST_HEAD(, AsyncURB) aurbs;

    /* Host side address */
    int bus_num;
    int addr;
    char port[MAX_PORTLEN];
    struct USBAutoFilter match;
    int seen, errcount;

    QTAILQ_ENTRY(USBHostDevice) next;
} USBHostDevice;

static QTAILQ_HEAD(, USBHostDevice) hostdevs = QTAILQ_HEAD_INITIALIZER(hostdevs);

static int usb_host_close(USBHostDevice *dev);
static int parse_filter(const char *spec, struct USBAutoFilter *f);
static void usb_host_auto_check(void *unused);
static int usb_host_read_file(char *line, size_t line_size,
                            const char *device_file, const char *device_name);
static int usb_linux_update_endp_table(USBHostDevice *s);

static int usb_host_do_reset(USBHostDevice *dev)
{
    struct timeval s, e;
    uint32_t usecs;
    int ret;

    gettimeofday(&s, NULL);
    ret = ioctl(dev->fd, USBDEVFS_RESET);
    gettimeofday(&e, NULL);
    usecs = (e.tv_sec  - s.tv_sec) * 1000000;
    usecs += e.tv_usec - s.tv_usec;
    if (usecs > 1000000) {
        /* more than a second, something is fishy, broken usb device? */
        fprintf(stderr, "husb: device %d:%d reset took %d.%06d seconds\n",
                dev->bus_num, dev->addr, usecs / 1000000, usecs % 1000000);
    }
    return ret;
}

static struct endp_data *get_endp(USBHostDevice *s, int pid, int ep)
{
    struct endp_data *eps = pid == USB_TOKEN_IN ? s->ep_in : s->ep_out;
    assert(pid == USB_TOKEN_IN || pid == USB_TOKEN_OUT);
    assert(ep > 0 && ep <= MAX_ENDPOINTS);
    return eps + ep - 1;
}

static int is_isoc(USBHostDevice *s, int pid, int ep)
{
    return get_endp(s, pid, ep)->type == USBDEVFS_URB_TYPE_ISO;
}

static int is_valid(USBHostDevice *s, int pid, int ep)
{
    return get_endp(s, pid, ep)->type != INVALID_EP_TYPE;
}

static int is_halted(USBHostDevice *s, int pid, int ep)
{
    return get_endp(s, pid, ep)->halted;
}

static void clear_halt(USBHostDevice *s, int pid, int ep)
{
    trace_usb_host_ep_clear_halt(s->bus_num, s->addr, ep);
    get_endp(s, pid, ep)->halted = 0;
}

static void set_halt(USBHostDevice *s, int pid, int ep)
{
    if (ep != 0) {
        trace_usb_host_ep_set_halt(s->bus_num, s->addr, ep);
        get_endp(s, pid, ep)->halted = 1;
    }
}

static int is_iso_started(USBHostDevice *s, int pid, int ep)
{
    return get_endp(s, pid, ep)->iso_started;
}

static void clear_iso_started(USBHostDevice *s, int pid, int ep)
{
    trace_usb_host_ep_stop_iso(s->bus_num, s->addr, ep);
    get_endp(s, pid, ep)->iso_started = 0;
}

static void set_iso_started(USBHostDevice *s, int pid, int ep)
{
    struct endp_data *e = get_endp(s, pid, ep);

    trace_usb_host_ep_start_iso(s->bus_num, s->addr, ep);
    if (!e->iso_started) {
        e->iso_started = 1;
        e->inflight = 0;
    }
}

static int change_iso_inflight(USBHostDevice *s, int pid, int ep, int value)
{
    struct endp_data *e = get_endp(s, pid, ep);

    e->inflight += value;
    return e->inflight;
}

static void set_iso_urb(USBHostDevice *s, int pid, int ep, AsyncURB *iso_urb)
{
    get_endp(s, pid, ep)->iso_urb = iso_urb;
}

static AsyncURB *get_iso_urb(USBHostDevice *s, int pid, int ep)
{
    return get_endp(s, pid, ep)->iso_urb;
}

static void set_iso_urb_idx(USBHostDevice *s, int pid, int ep, int i)
{
    get_endp(s, pid, ep)->iso_urb_idx = i;
}

static int get_iso_urb_idx(USBHostDevice *s, int pid, int ep)
{
    return get_endp(s, pid, ep)->iso_urb_idx;
}

static void set_iso_buffer_used(USBHostDevice *s, int pid, int ep, int i)
{
    get_endp(s, pid, ep)->iso_buffer_used = i;
}

static int get_iso_buffer_used(USBHostDevice *s, int pid, int ep)
{
    return get_endp(s, pid, ep)->iso_buffer_used;
}

static void set_max_packet_size(USBHostDevice *s, int pid, int ep,
                                uint8_t *descriptor)
{
    int raw = descriptor[4] + (descriptor[5] << 8);
    int size, microframes;

    size = raw & 0x7ff;
    switch ((raw >> 11) & 3) {
    case 1:  microframes = 2; break;
    case 2:  microframes = 3; break;
    default: microframes = 1; break;
    }
    get_endp(s, pid, ep)->max_packet_size = size * microframes;
}

static int get_max_packet_size(USBHostDevice *s, int pid, int ep)
{
    return get_endp(s, pid, ep)->max_packet_size;
}

/*
 * Async URB state.
 * We always allocate iso packet descriptors even for bulk transfers
 * to simplify allocation and casts.
 */
struct AsyncURB
{
    struct usbdevfs_urb urb;
    struct usbdevfs_iso_packet_desc isocpd[ISO_FRAME_DESC_PER_URB];
    USBHostDevice *hdev;
    QLIST_ENTRY(AsyncURB) next;

    /* For regular async urbs */
    USBPacket     *packet;
    int more; /* large transfer, more urbs follow */

    /* For buffered iso handling */
    int iso_frame_idx; /* -1 means in flight */
};

static AsyncURB *async_alloc(USBHostDevice *s)
{
    AsyncURB *aurb = g_malloc0(sizeof(AsyncURB));
    aurb->hdev = s;
    QLIST_INSERT_HEAD(&s->aurbs, aurb, next);
    return aurb;
}

static void async_free(AsyncURB *aurb)
{
    QLIST_REMOVE(aurb, next);
    g_free(aurb);
}

static void do_disconnect(USBHostDevice *s)
{
    usb_host_close(s);
    usb_host_auto_check(NULL);
}

static void async_complete(void *opaque)
{
    USBHostDevice *s = opaque;
    AsyncURB *aurb;
    int urbs = 0;

    while (1) {
        USBPacket *p;

        int r = ioctl(s->fd, USBDEVFS_REAPURBNDELAY, &aurb);
        if (r < 0) {
            if (errno == EAGAIN) {
                if (urbs > 2) {
                    fprintf(stderr, "husb: %d iso urbs finished at once\n", urbs);
                }
                return;
            }
            if (errno == ENODEV) {
                if (!s->closing) {
                    trace_usb_host_disconnect(s->bus_num, s->addr);
                    do_disconnect(s);
                }
                return;
            }

            perror("USBDEVFS_REAPURBNDELAY");
            return;
        }

        DPRINTF("husb: async completed. aurb %p status %d alen %d\n",
                aurb, aurb->urb.status, aurb->urb.actual_length);

        /* If this is a buffered iso urb mark it as complete and don't do
           anything else (it is handled further in usb_host_handle_iso_data) */
        if (aurb->iso_frame_idx == -1) {
            int inflight;
            int pid = (aurb->urb.endpoint & USB_DIR_IN) ?
                USB_TOKEN_IN : USB_TOKEN_OUT;
            int ep = aurb->urb.endpoint & 0xf;
            if (aurb->urb.status == -EPIPE) {
                set_halt(s, pid, ep);
            }
            aurb->iso_frame_idx = 0;
            urbs++;
            inflight = change_iso_inflight(s, pid, ep, -1);
            if (inflight == 0 && is_iso_started(s, pid, ep)) {
                fprintf(stderr, "husb: out of buffers for iso stream\n");
            }
            continue;
        }

        p = aurb->packet;
        trace_usb_host_urb_complete(s->bus_num, s->addr, aurb, aurb->urb.status,
                                    aurb->urb.actual_length, aurb->more);

        if (p) {
            switch (aurb->urb.status) {
            case 0:
                p->result += aurb->urb.actual_length;
                break;

            case -EPIPE:
                set_halt(s, p->pid, p->devep);
                p->result = USB_RET_STALL;
                break;

            default:
                p->result = USB_RET_NAK;
                break;
            }

            if (aurb->urb.type == USBDEVFS_URB_TYPE_CONTROL) {
                trace_usb_host_req_complete(s->bus_num, s->addr, p->result);
                usb_generic_async_ctrl_complete(&s->dev, p);
            } else if (!aurb->more) {
                trace_usb_host_req_complete(s->bus_num, s->addr, p->result);
                usb_packet_complete(&s->dev, p);
            }
        }

        async_free(aurb);
    }
}

static void usb_host_async_cancel(USBDevice *dev, USBPacket *p)
{
    USBHostDevice *s = DO_UPCAST(USBHostDevice, dev, dev);
    AsyncURB *aurb;

    QLIST_FOREACH(aurb, &s->aurbs, next) {
        if (p != aurb->packet) {
            continue;
        }

        DPRINTF("husb: async cancel: packet %p, aurb %p\n", p, aurb);

        /* Mark it as dead (see async_complete above) */
        aurb->packet = NULL;

        int r = ioctl(s->fd, USBDEVFS_DISCARDURB, aurb);
        if (r < 0) {
            DPRINTF("husb: async. discard urb failed errno %d\n", errno);
        }
    }
}

static int usb_host_claim_port(USBHostDevice *s)
{
#ifdef USBDEVFS_CLAIM_PORT
    char *h, hub_name[64], line[1024];
    int hub_addr, ret;

    snprintf(hub_name, sizeof(hub_name), "%d-%s",
             s->match.bus_num, s->match.port);

    /* try strip off last ".$portnr" to get hub */
    h = strrchr(hub_name, '.');
    if (h != NULL) {
        s->hub_port = atoi(h+1);
        *h = '\0';
    } else {
        /* no dot in there -> it is the root hub */
        snprintf(hub_name, sizeof(hub_name), "usb%d",
                 s->match.bus_num);
        s->hub_port = atoi(s->match.port);
    }

    if (!usb_host_read_file(line, sizeof(line), "devnum",
                            hub_name)) {
        return -1;
    }
    if (sscanf(line, "%d", &hub_addr) != 1) {
        return -1;
    }

    if (!usb_host_device_path) {
        return -1;
    }
    snprintf(line, sizeof(line), "%s/%03d/%03d",
             usb_host_device_path, s->match.bus_num, hub_addr);
    s->hub_fd = open(line, O_RDWR | O_NONBLOCK);
    if (s->hub_fd < 0) {
        return -1;
    }

    ret = ioctl(s->hub_fd, USBDEVFS_CLAIM_PORT, &s->hub_port);
    if (ret < 0) {
        close(s->hub_fd);
        s->hub_fd = -1;
        return -1;
    }

    trace_usb_host_claim_port(s->match.bus_num, hub_addr, s->hub_port);
    return 0;
#else
    return -1;
#endif
}

static void usb_host_release_port(USBHostDevice *s)
{
    if (s->hub_fd == -1) {
        return;
    }
#ifdef USBDEVFS_RELEASE_PORT
    ioctl(s->hub_fd, USBDEVFS_RELEASE_PORT, &s->hub_port);
#endif
    close(s->hub_fd);
    s->hub_fd = -1;
}

static int usb_host_disconnect_ifaces(USBHostDevice *dev, int nb_interfaces)
{
    /* earlier Linux 2.4 do not support that */
#ifdef USBDEVFS_DISCONNECT
    struct usbdevfs_ioctl ctrl;
    int ret, interface;

    for (interface = 0; interface < nb_interfaces; interface++) {
        ctrl.ioctl_code = USBDEVFS_DISCONNECT;
        ctrl.ifno = interface;
        ctrl.data = 0;
        ret = ioctl(dev->fd, USBDEVFS_IOCTL, &ctrl);
        if (ret < 0 && errno != ENODATA) {
            perror("USBDEVFS_DISCONNECT");
            return -1;
        }
    }
#endif
    return 0;
}

static int usb_linux_get_num_interfaces(USBHostDevice *s)
{
    char device_name[64], line[1024];
    int num_interfaces = 0;

    if (usb_fs_type != USB_FS_SYS) {
        return -1;
    }

    sprintf(device_name, "%d-%s", s->bus_num, s->port);
    if (!usb_host_read_file(line, sizeof(line), "bNumInterfaces",
                            device_name)) {
        return -1;
    }
    if (sscanf(line, "%d", &num_interfaces) != 1) {
        return -1;
    }
    return num_interfaces;
}

static int usb_host_claim_interfaces(USBHostDevice *dev, int configuration)
{
    const char *op = NULL;
    int dev_descr_len, config_descr_len;
    int interface, nb_interfaces;
    int ret, i;

    if (configuration == 0) { /* address state - ignore */
        dev->ninterfaces   = 0;
        dev->configuration = 0;
        return 1;
    }

    DPRINTF("husb: claiming interfaces. config %d\n", configuration);

    i = 0;
    dev_descr_len = dev->descr[0];
    if (dev_descr_len > dev->descr_len) {
        fprintf(stderr, "husb: update iface failed. descr too short\n");
        return 0;
    }

    i += dev_descr_len;
    while (i < dev->descr_len) {
        DPRINTF("husb: i is %d, descr_len is %d, dl %d, dt %d\n",
                i, dev->descr_len,
               dev->descr[i], dev->descr[i+1]);

        if (dev->descr[i+1] != USB_DT_CONFIG) {
            i += dev->descr[i];
            continue;
        }
        config_descr_len = dev->descr[i];

        DPRINTF("husb: config #%d need %d\n", dev->descr[i + 5], configuration);

        if (configuration == dev->descr[i + 5]) {
            configuration = dev->descr[i + 5];
            break;
        }

        i += config_descr_len;
    }

    if (i >= dev->descr_len) {
        fprintf(stderr,
                "husb: update iface failed. no matching configuration\n");
        return 0;
    }
    nb_interfaces = dev->descr[i + 4];

    if (usb_host_disconnect_ifaces(dev, nb_interfaces) < 0) {
        goto fail;
    }

    /* XXX: only grab if all interfaces are free */
    for (interface = 0; interface < nb_interfaces; interface++) {
        op = "USBDEVFS_CLAIMINTERFACE";
        ret = ioctl(dev->fd, USBDEVFS_CLAIMINTERFACE, &interface);
        if (ret < 0) {
            goto fail;
        }
    }

    trace_usb_host_claim_interfaces(dev->bus_num, dev->addr,
                                    nb_interfaces, configuration);

    dev->ninterfaces   = nb_interfaces;
    dev->configuration = configuration;
    return 1;

fail:
    if (errno == ENODEV) {
        do_disconnect(dev);
    }
    perror(op);
    return 0;
}

static int usb_host_release_interfaces(USBHostDevice *s)
{
    int ret, i;

    trace_usb_host_release_interfaces(s->bus_num, s->addr);

    for (i = 0; i < s->ninterfaces; i++) {
        ret = ioctl(s->fd, USBDEVFS_RELEASEINTERFACE, &i);
        if (ret < 0) {
            perror("USBDEVFS_RELEASEINTERFACE");
            return 0;
        }
    }
    return 1;
}

static void usb_host_handle_reset(USBDevice *dev)
{
    USBHostDevice *s = DO_UPCAST(USBHostDevice, dev, dev);

    trace_usb_host_reset(s->bus_num, s->addr);

    usb_host_do_reset(s);;

    usb_host_claim_interfaces(s, 0);
    usb_linux_update_endp_table(s);
}

static void usb_host_handle_destroy(USBDevice *dev)
{
    USBHostDevice *s = (USBHostDevice *)dev;

    usb_host_release_port(s);
    usb_host_close(s);
    QTAILQ_REMOVE(&hostdevs, s, next);
    qemu_remove_exit_notifier(&s->exit);
}

/* iso data is special, we need to keep enough urbs in flight to make sure
   that the controller never runs out of them, otherwise the device will
   likely suffer a buffer underrun / overrun. */
static AsyncURB *usb_host_alloc_iso(USBHostDevice *s, int pid, uint8_t ep)
{
    AsyncURB *aurb;
    int i, j, len = get_max_packet_size(s, pid, ep);

    aurb = g_malloc0(s->iso_urb_count * sizeof(*aurb));
    for (i = 0; i < s->iso_urb_count; i++) {
        aurb[i].urb.endpoint      = ep;
        aurb[i].urb.buffer_length = ISO_FRAME_DESC_PER_URB * len;
        aurb[i].urb.buffer        = g_malloc(aurb[i].urb.buffer_length);
        aurb[i].urb.type          = USBDEVFS_URB_TYPE_ISO;
        aurb[i].urb.flags         = USBDEVFS_URB_ISO_ASAP;
        aurb[i].urb.number_of_packets = ISO_FRAME_DESC_PER_URB;
        for (j = 0 ; j < ISO_FRAME_DESC_PER_URB; j++)
            aurb[i].urb.iso_frame_desc[j].length = len;
        if (pid == USB_TOKEN_IN) {
            aurb[i].urb.endpoint |= 0x80;
            /* Mark as fully consumed (idle) */
            aurb[i].iso_frame_idx = ISO_FRAME_DESC_PER_URB;
        }
    }
    set_iso_urb(s, pid, ep, aurb);

    return aurb;
}

static void usb_host_stop_n_free_iso(USBHostDevice *s, int pid, uint8_t ep)
{
    AsyncURB *aurb;
    int i, ret, killed = 0, free = 1;

    aurb = get_iso_urb(s, pid, ep);
    if (!aurb) {
        return;
    }

    for (i = 0; i < s->iso_urb_count; i++) {
        /* in flight? */
        if (aurb[i].iso_frame_idx == -1) {
            ret = ioctl(s->fd, USBDEVFS_DISCARDURB, &aurb[i]);
            if (ret < 0) {
                perror("USBDEVFS_DISCARDURB");
                free = 0;
                continue;
            }
            killed++;
        }
    }

    /* Make sure any urbs we've killed are reaped before we free them */
    if (killed) {
        async_complete(s);
    }

    for (i = 0; i < s->iso_urb_count; i++) {
        g_free(aurb[i].urb.buffer);
    }

    if (free)
        g_free(aurb);
    else
        printf("husb: leaking iso urbs because of discard failure\n");
    set_iso_urb(s, pid, ep, NULL);
    set_iso_urb_idx(s, pid, ep, 0);
    clear_iso_started(s, pid, ep);
}

static int urb_status_to_usb_ret(int status)
{
    switch (status) {
    case -EPIPE:
        return USB_RET_STALL;
    default:
        return USB_RET_NAK;
    }
}

static int usb_host_handle_iso_data(USBHostDevice *s, USBPacket *p, int in)
{
    AsyncURB *aurb;
    int i, j, ret, max_packet_size, offset, len = 0;
    uint8_t *buf;

    max_packet_size = get_max_packet_size(s, p->pid, p->devep);
    if (max_packet_size == 0)
        return USB_RET_NAK;

    aurb = get_iso_urb(s, p->pid, p->devep);
    if (!aurb) {
        aurb = usb_host_alloc_iso(s, p->pid, p->devep);
    }

    i = get_iso_urb_idx(s, p->pid, p->devep);
    j = aurb[i].iso_frame_idx;
    if (j >= 0 && j < ISO_FRAME_DESC_PER_URB) {
        if (in) {
            /* Check urb status  */
            if (aurb[i].urb.status) {
                len = urb_status_to_usb_ret(aurb[i].urb.status);
                /* Move to the next urb */
                aurb[i].iso_frame_idx = ISO_FRAME_DESC_PER_URB - 1;
            /* Check frame status */
            } else if (aurb[i].urb.iso_frame_desc[j].status) {
                len = urb_status_to_usb_ret(
                                        aurb[i].urb.iso_frame_desc[j].status);
            /* Check the frame fits */
            } else if (aurb[i].urb.iso_frame_desc[j].actual_length
                       > p->iov.size) {
                printf("husb: received iso data is larger then packet\n");
                len = USB_RET_NAK;
            /* All good copy data over */
            } else {
                len = aurb[i].urb.iso_frame_desc[j].actual_length;
                buf  = aurb[i].urb.buffer +
                    j * aurb[i].urb.iso_frame_desc[0].length;
                usb_packet_copy(p, buf, len);
            }
        } else {
            len = p->iov.size;
            offset = (j == 0) ? 0 : get_iso_buffer_used(s, p->pid, p->devep);

            /* Check the frame fits */
            if (len > max_packet_size) {
                printf("husb: send iso data is larger then max packet size\n");
                return USB_RET_NAK;
            }

            /* All good copy data over */
            usb_packet_copy(p, aurb[i].urb.buffer + offset, len);
            aurb[i].urb.iso_frame_desc[j].length = len;
            offset += len;
            set_iso_buffer_used(s, p->pid, p->devep, offset);

            /* Start the stream once we have buffered enough data */
            if (!is_iso_started(s, p->pid, p->devep) && i == 1 && j == 8) {
                set_iso_started(s, p->pid, p->devep);
            }
        }
        aurb[i].iso_frame_idx++;
        if (aurb[i].iso_frame_idx == ISO_FRAME_DESC_PER_URB) {
            i = (i + 1) % s->iso_urb_count;
            set_iso_urb_idx(s, p->pid, p->devep, i);
        }
    } else {
        if (in) {
            set_iso_started(s, p->pid, p->devep);
        } else {
            DPRINTF("hubs: iso out error no free buffer, dropping packet\n");
        }
    }

    if (is_iso_started(s, p->pid, p->devep)) {
        /* (Re)-submit all fully consumed / filled urbs */
        for (i = 0; i < s->iso_urb_count; i++) {
            if (aurb[i].iso_frame_idx == ISO_FRAME_DESC_PER_URB) {
                ret = ioctl(s->fd, USBDEVFS_SUBMITURB, &aurb[i]);
                if (ret < 0) {
                    perror("USBDEVFS_SUBMITURB");
                    if (!in || len == 0) {
                        switch(errno) {
                        case ETIMEDOUT:
                            len = USB_RET_NAK;
                            break;
                        case EPIPE:
                        default:
                            len = USB_RET_STALL;
                        }
                    }
                    break;
                }
                aurb[i].iso_frame_idx = -1;
                change_iso_inflight(s, p->pid, p->devep, 1);
            }
        }
    }

    return len;
}

static int usb_host_handle_data(USBDevice *dev, USBPacket *p)
{
    USBHostDevice *s = DO_UPCAST(USBHostDevice, dev, dev);
    struct usbdevfs_urb *urb;
    AsyncURB *aurb;
    int ret, rem, prem, v;
    uint8_t *pbuf;
    uint8_t ep;

    trace_usb_host_req_data(s->bus_num, s->addr,
                            p->pid == USB_TOKEN_IN,
                            p->devep, p->iov.size);

    if (!is_valid(s, p->pid, p->devep)) {
        trace_usb_host_req_complete(s->bus_num, s->addr, USB_RET_NAK);
        return USB_RET_NAK;
    }

    if (p->pid == USB_TOKEN_IN) {
        ep = p->devep | 0x80;
    } else {
        ep = p->devep;
    }

    if (is_halted(s, p->pid, p->devep)) {
        unsigned int arg = ep;
        ret = ioctl(s->fd, USBDEVFS_CLEAR_HALT, &arg);
        if (ret < 0) {
            perror("USBDEVFS_CLEAR_HALT");
            trace_usb_host_req_complete(s->bus_num, s->addr, USB_RET_NAK);
            return USB_RET_NAK;
        }
        clear_halt(s, p->pid, p->devep);
    }

    if (is_isoc(s, p->pid, p->devep)) {
        return usb_host_handle_iso_data(s, p, p->pid == USB_TOKEN_IN);
    }

    v = 0;
    prem = p->iov.iov[v].iov_len;
    pbuf = p->iov.iov[v].iov_base;
    rem = p->iov.size;
    while (rem) {
        if (prem == 0) {
            v++;
            assert(v < p->iov.niov);
            prem = p->iov.iov[v].iov_len;
            pbuf = p->iov.iov[v].iov_base;
            assert(prem <= rem);
        }
        aurb = async_alloc(s);
        aurb->packet = p;

        urb = &aurb->urb;
        urb->endpoint      = ep;
        urb->type          = USBDEVFS_URB_TYPE_BULK;
        urb->usercontext   = s;
        urb->buffer        = pbuf;
        urb->buffer_length = prem;

        if (urb->buffer_length > MAX_USBFS_BUFFER_SIZE) {
            urb->buffer_length = MAX_USBFS_BUFFER_SIZE;
        }
        pbuf += urb->buffer_length;
        prem -= urb->buffer_length;
        rem  -= urb->buffer_length;
        if (rem) {
            aurb->more         = 1;
        }

        trace_usb_host_urb_submit(s->bus_num, s->addr, aurb,
                                  urb->buffer_length, aurb->more);
        ret = ioctl(s->fd, USBDEVFS_SUBMITURB, urb);

        DPRINTF("husb: data submit: ep 0x%x, len %u, more %d, packet %p, aurb %p\n",
                urb->endpoint, urb->buffer_length, aurb->more, p, aurb);

        if (ret < 0) {
            perror("USBDEVFS_SUBMITURB");
            async_free(aurb);

            switch(errno) {
            case ETIMEDOUT:
                trace_usb_host_req_complete(s->bus_num, s->addr, USB_RET_NAK);
                return USB_RET_NAK;
            case EPIPE:
            default:
                trace_usb_host_req_complete(s->bus_num, s->addr, USB_RET_STALL);
                return USB_RET_STALL;
            }
        }
    }

    return USB_RET_ASYNC;
}

static int ctrl_error(void)
{
    if (errno == ETIMEDOUT) {
        return USB_RET_NAK;
    } else {
        return USB_RET_STALL;
    }
}

static int usb_host_set_address(USBHostDevice *s, int addr)
{
    trace_usb_host_set_address(s->bus_num, s->addr, addr);
    s->dev.addr = addr;
    return 0;
}

static int usb_host_set_config(USBHostDevice *s, int config)
{
    int ret, first = 1;

    trace_usb_host_set_config(s->bus_num, s->addr, config);

    usb_host_release_interfaces(s);

again:
    ret = ioctl(s->fd, USBDEVFS_SETCONFIGURATION, &config);

    DPRINTF("husb: ctrl set config %d ret %d errno %d\n", config, ret, errno);

    if (ret < 0 && errno == EBUSY && first) {
        /* happens if usb device is in use by host drivers */
        int count = usb_linux_get_num_interfaces(s);
        if (count > 0) {
            DPRINTF("husb: busy -> disconnecting %d interfaces\n", count);
            usb_host_disconnect_ifaces(s, count);
            first = 0;
            goto again;
        }
    }

    if (ret < 0) {
        return ctrl_error();
    }
    usb_host_claim_interfaces(s, config);
    usb_linux_update_endp_table(s);
    return 0;
}

static int usb_host_set_interface(USBHostDevice *s, int iface, int alt)
{
    struct usbdevfs_setinterface si;
    int i, ret;

    trace_usb_host_set_interface(s->bus_num, s->addr, iface, alt);

    for (i = 1; i <= MAX_ENDPOINTS; i++) {
        if (is_isoc(s, USB_TOKEN_IN, i)) {
            usb_host_stop_n_free_iso(s, USB_TOKEN_IN, i);
        }
        if (is_isoc(s, USB_TOKEN_OUT, i)) {
            usb_host_stop_n_free_iso(s, USB_TOKEN_OUT, i);
        }
    }

    si.interface  = iface;
    si.altsetting = alt;
    ret = ioctl(s->fd, USBDEVFS_SETINTERFACE, &si);

    DPRINTF("husb: ctrl set iface %d altset %d ret %d errno %d\n",
            iface, alt, ret, errno);

    if (ret < 0) {
        return ctrl_error();
    }
    usb_linux_update_endp_table(s);
    return 0;
}

static int usb_host_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    USBHostDevice *s = DO_UPCAST(USBHostDevice, dev, dev);
    struct usbdevfs_urb *urb;
    AsyncURB *aurb;
    int ret;

    /*
     * Process certain standard device requests.
     * These are infrequent and are processed synchronously.
     */

    /* Note request is (bRequestType << 8) | bRequest */
    trace_usb_host_req_control(s->bus_num, s->addr, request, value, index);

    switch (request) {
    case DeviceOutRequest | USB_REQ_SET_ADDRESS:
        return usb_host_set_address(s, value);

    case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
        return usb_host_set_config(s, value & 0xff);

    case InterfaceOutRequest | USB_REQ_SET_INTERFACE:
        return usb_host_set_interface(s, index, value);
    }

    /* The rest are asynchronous */

    if (length > sizeof(dev->data_buf)) {
        fprintf(stderr, "husb: ctrl buffer too small (%d > %zu)\n",
                length, sizeof(dev->data_buf));
        return USB_RET_STALL;
    }

    aurb = async_alloc(s);
    aurb->packet = p;

    /*
     * Setup ctrl transfer.
     *
     * s->ctrl is laid out such that data buffer immediately follows
     * 'req' struct which is exactly what usbdevfs expects.
     */
    urb = &aurb->urb;

    urb->type     = USBDEVFS_URB_TYPE_CONTROL;
    urb->endpoint = p->devep;

    urb->buffer        = &dev->setup_buf;
    urb->buffer_length = length + 8;

    urb->usercontext = s;

    trace_usb_host_urb_submit(s->bus_num, s->addr, aurb,
                              urb->buffer_length, aurb->more);
    ret = ioctl(s->fd, USBDEVFS_SUBMITURB, urb);

    DPRINTF("husb: submit ctrl. len %u aurb %p\n", urb->buffer_length, aurb);

    if (ret < 0) {
        DPRINTF("husb: submit failed. errno %d\n", errno);
        async_free(aurb);

        switch(errno) {
        case ETIMEDOUT:
            return USB_RET_NAK;
        case EPIPE:
        default:
            return USB_RET_STALL;
        }
    }

    return USB_RET_ASYNC;
}

static uint8_t usb_linux_get_alt_setting(USBHostDevice *s,
    uint8_t configuration, uint8_t interface)
{
    uint8_t alt_setting;
    struct usb_ctrltransfer ct;
    int ret;

    if (usb_fs_type == USB_FS_SYS) {
        char device_name[64], line[1024];
        int alt_setting;

        sprintf(device_name, "%d-%s:%d.%d", s->bus_num, s->port,
                (int)configuration, (int)interface);

        if (!usb_host_read_file(line, sizeof(line), "bAlternateSetting",
                                device_name)) {
            goto usbdevfs;
        }
        if (sscanf(line, "%d", &alt_setting) != 1) {
            goto usbdevfs;
        }
        return alt_setting;
    }

usbdevfs:
    ct.bRequestType = USB_DIR_IN | USB_RECIP_INTERFACE;
    ct.bRequest = USB_REQ_GET_INTERFACE;
    ct.wValue = 0;
    ct.wIndex = interface;
    ct.wLength = 1;
    ct.data = &alt_setting;
    ct.timeout = 50;
    ret = ioctl(s->fd, USBDEVFS_CONTROL, &ct);
    if (ret < 0) {
        /* Assume alt 0 on error */
        return 0;
    }

    return alt_setting;
}

/* returns 1 on problem encountered or 0 for success */
static int usb_linux_update_endp_table(USBHostDevice *s)
{
    uint8_t *descriptors;
    uint8_t devep, type, alt_interface;
    int interface, length, i, ep, pid;
    struct endp_data *epd;

    for (i = 0; i < MAX_ENDPOINTS; i++) {
        s->ep_in[i].type = INVALID_EP_TYPE;
        s->ep_out[i].type = INVALID_EP_TYPE;
    }

    if (s->configuration == 0) {
        /* not configured yet -- leave all endpoints disabled */
        return 0;
    }

    /* get the desired configuration, interface, and endpoint descriptors
     * from device description */
    descriptors = &s->descr[18];
    length = s->descr_len - 18;
    i = 0;

    while (i < length) {
        if (descriptors[i + 1] != USB_DT_CONFIG) {
            fprintf(stderr, "invalid descriptor data\n");
            return 1;
        } else if (descriptors[i + 5] != s->configuration) {
            DPRINTF("not requested configuration %d\n", s->configuration);
            i += (descriptors[i + 3] << 8) + descriptors[i + 2];
            continue;
        }

        i += descriptors[i];

        if (descriptors[i + 1] != USB_DT_INTERFACE ||
            (descriptors[i + 1] == USB_DT_INTERFACE &&
             descriptors[i + 4] == 0)) {
            i += descriptors[i];
            continue;
        }

        interface = descriptors[i + 2];
        alt_interface = usb_linux_get_alt_setting(s, s->configuration,
                                                  interface);

        /* the current interface descriptor is the active interface
         * and has endpoints */
        if (descriptors[i + 3] != alt_interface) {
            i += descriptors[i];
            continue;
        }

        /* advance to the endpoints */
        while (i < length && descriptors[i +1] != USB_DT_ENDPOINT) {
            i += descriptors[i];
        }

        if (i >= length)
            break;

        while (i < length) {
            if (descriptors[i + 1] != USB_DT_ENDPOINT) {
                break;
            }

            devep = descriptors[i + 2];
            pid = (devep & USB_DIR_IN) ? USB_TOKEN_IN : USB_TOKEN_OUT;
            ep = devep & 0xf;
            if (ep == 0) {
                fprintf(stderr, "usb-linux: invalid ep descriptor, ep == 0\n");
                return 1;
            }

            switch (descriptors[i + 3] & 0x3) {
            case 0x00:
                type = USBDEVFS_URB_TYPE_CONTROL;
                break;
            case 0x01:
                type = USBDEVFS_URB_TYPE_ISO;
                set_max_packet_size(s, pid, ep, descriptors + i);
                break;
            case 0x02:
                type = USBDEVFS_URB_TYPE_BULK;
                break;
            case 0x03:
                type = USBDEVFS_URB_TYPE_INTERRUPT;
                break;
            default:
                DPRINTF("usb_host: malformed endpoint type\n");
                type = USBDEVFS_URB_TYPE_BULK;
            }
            epd = get_endp(s, pid, ep);
            assert(epd->type == INVALID_EP_TYPE);
            epd->type = type;
            epd->halted = 0;

            i += descriptors[i];
        }
    }
    return 0;
}

/*
 * Check if we can safely redirect a usb2 device to a usb1 virtual controller,
 * this function assumes this is safe, if:
 * 1) There are no isoc endpoints
 * 2) There are no interrupt endpoints with a max_packet_size > 64
 * Note bulk endpoints with a max_packet_size > 64 in theory also are not
 * usb1 compatible, but in practice this seems to work fine.
 */
static int usb_linux_full_speed_compat(USBHostDevice *dev)
{
    int i, packet_size;

    /*
     * usb_linux_update_endp_table only registers info about ep in the current
     * interface altsettings, so we need to parse the descriptors again.
     */
    for (i = 0; (i + 5) < dev->descr_len; i += dev->descr[i]) {
        if (dev->descr[i + 1] == USB_DT_ENDPOINT) {
            switch (dev->descr[i + 3] & 0x3) {
            case 0x00: /* CONTROL */
                break;
            case 0x01: /* ISO */
                return 0;
            case 0x02: /* BULK */
                break;
            case 0x03: /* INTERRUPT */
                packet_size = dev->descr[i + 4] + (dev->descr[i + 5] << 8);
                if (packet_size > 64)
                    return 0;
                break;
            }
        }
    }
    return 1;
}

static int usb_host_open(USBHostDevice *dev, int bus_num,
                         int addr, const char *port,
                         const char *prod_name, int speed)
{
    int fd = -1, ret;
    char buf[1024];

    trace_usb_host_open_started(bus_num, addr);

    if (dev->fd != -1) {
        goto fail;
    }

    if (!usb_host_device_path) {
        perror("husb: USB Host Device Path not set");
        goto fail;
    }
    snprintf(buf, sizeof(buf), "%s/%03d/%03d", usb_host_device_path,
             bus_num, addr);
    fd = open(buf, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror(buf);
        goto fail;
    }
    DPRINTF("husb: opened %s\n", buf);

    dev->bus_num = bus_num;
    dev->addr = addr;
    strcpy(dev->port, port);
    dev->fd = fd;

    /* read the device description */
    dev->descr_len = read(fd, dev->descr, sizeof(dev->descr));
    if (dev->descr_len <= 0) {
        perror("husb: reading device data failed");
        goto fail;
    }

#ifdef DEBUG
    {
        int x;
        printf("=== begin dumping device descriptor data ===\n");
        for (x = 0; x < dev->descr_len; x++) {
            printf("%02x ", dev->descr[x]);
        }
        printf("\n=== end dumping device descriptor data ===\n");
    }
#endif


    /* start unconfigured -- we'll wait for the guest to set a configuration */
    if (!usb_host_claim_interfaces(dev, 0)) {
        goto fail;
    }

    ret = usb_linux_update_endp_table(dev);
    if (ret) {
        goto fail;
    }

    if (speed == -1) {
        struct usbdevfs_connectinfo ci;

        ret = ioctl(fd, USBDEVFS_CONNECTINFO, &ci);
        if (ret < 0) {
            perror("usb_host_device_open: USBDEVFS_CONNECTINFO");
            goto fail;
        }

        if (ci.slow) {
            speed = USB_SPEED_LOW;
        } else {
            speed = USB_SPEED_HIGH;
        }
    }
    dev->dev.speed = speed;
    dev->dev.speedmask = (1 << speed);
    if (dev->dev.speed == USB_SPEED_HIGH && usb_linux_full_speed_compat(dev)) {
        dev->dev.speedmask |= USB_SPEED_MASK_FULL;
    }

    trace_usb_host_open_success(bus_num, addr);

    if (!prod_name || prod_name[0] == '\0') {
        snprintf(dev->dev.product_desc, sizeof(dev->dev.product_desc),
                 "host:%d.%d", bus_num, addr);
    } else {
        pstrcpy(dev->dev.product_desc, sizeof(dev->dev.product_desc),
                prod_name);
    }

    ret = usb_device_attach(&dev->dev);
    if (ret) {
        goto fail;
    }

    /* USB devio uses 'write' flag to check for async completions */
    qemu_set_fd_handler(dev->fd, NULL, async_complete, dev);

    return 0;

fail:
    trace_usb_host_open_failure(bus_num, addr);
    if (dev->fd != -1) {
        close(dev->fd);
        dev->fd = -1;
    }
    return -1;
}

static int usb_host_close(USBHostDevice *dev)
{
    int i;

    if (dev->fd == -1) {
        return -1;
    }

    trace_usb_host_close(dev->bus_num, dev->addr);

    qemu_set_fd_handler(dev->fd, NULL, NULL, NULL);
    dev->closing = 1;
    for (i = 1; i <= MAX_ENDPOINTS; i++) {
        if (is_isoc(dev, USB_TOKEN_IN, i)) {
            usb_host_stop_n_free_iso(dev, USB_TOKEN_IN, i);
        }
        if (is_isoc(dev, USB_TOKEN_OUT, i)) {
            usb_host_stop_n_free_iso(dev, USB_TOKEN_OUT, i);
        }
    }
    async_complete(dev);
    dev->closing = 0;
    if (dev->dev.attached) {
        usb_device_detach(&dev->dev);
    }
    usb_host_do_reset(dev);
    close(dev->fd);
    dev->fd = -1;
    return 0;
}

static void usb_host_exit_notifier(struct Notifier *n, void *data)
{
    USBHostDevice *s = container_of(n, USBHostDevice, exit);

    usb_host_release_port(s);
    if (s->fd != -1) {
        usb_host_do_reset(s);;
    }
}

static int usb_host_initfn(USBDevice *dev)
{
    USBHostDevice *s = DO_UPCAST(USBHostDevice, dev, dev);

    dev->auto_attach = 0;
    s->fd = -1;
    s->hub_fd = -1;

    QTAILQ_INSERT_TAIL(&hostdevs, s, next);
    s->exit.notify = usb_host_exit_notifier;
    qemu_add_exit_notifier(&s->exit);
    usb_host_auto_check(NULL);

    if (s->match.bus_num != 0 && s->match.port != NULL) {
        usb_host_claim_port(s);
    }
    return 0;
}

static const VMStateDescription vmstate_usb_host = {
    .name = "usb-host",
    .unmigratable = 1,
};

static struct USBDeviceInfo usb_host_dev_info = {
    .product_desc   = "USB Host Device",
    .qdev.name      = "usb-host",
    .qdev.size      = sizeof(USBHostDevice),
    .qdev.vmsd      = &vmstate_usb_host,
    .init           = usb_host_initfn,
    .handle_packet  = usb_generic_handle_packet,
    .cancel_packet  = usb_host_async_cancel,
    .handle_data    = usb_host_handle_data,
    .handle_control = usb_host_handle_control,
    .handle_reset   = usb_host_handle_reset,
    .handle_destroy = usb_host_handle_destroy,
    .usbdevice_name = "host",
    .usbdevice_init = usb_host_device_open,
    .qdev.props     = (Property[]) {
        DEFINE_PROP_UINT32("hostbus",  USBHostDevice, match.bus_num,    0),
        DEFINE_PROP_UINT32("hostaddr", USBHostDevice, match.addr,       0),
        DEFINE_PROP_STRING("hostport", USBHostDevice, match.port),
        DEFINE_PROP_HEX32("vendorid",  USBHostDevice, match.vendor_id,  0),
        DEFINE_PROP_HEX32("productid", USBHostDevice, match.product_id, 0),
        DEFINE_PROP_UINT32("isobufs",  USBHostDevice, iso_urb_count,    4),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void usb_host_register_devices(void)
{
    usb_qdev_register(&usb_host_dev_info);
}
device_init(usb_host_register_devices)

USBDevice *usb_host_device_open(const char *devname)
{
    struct USBAutoFilter filter;
    USBDevice *dev;
    char *p;

    dev = usb_create(NULL /* FIXME */, "usb-host");

    if (strstr(devname, "auto:")) {
        if (parse_filter(devname, &filter) < 0) {
            goto fail;
        }
    } else {
        if ((p = strchr(devname, '.'))) {
            filter.bus_num    = strtoul(devname, NULL, 0);
            filter.addr       = strtoul(p + 1, NULL, 0);
            filter.vendor_id  = 0;
            filter.product_id = 0;
        } else if ((p = strchr(devname, ':'))) {
            filter.bus_num    = 0;
            filter.addr       = 0;
            filter.vendor_id  = strtoul(devname, NULL, 16);
            filter.product_id = strtoul(p + 1, NULL, 16);
        } else {
            goto fail;
        }
    }

    qdev_prop_set_uint32(&dev->qdev, "hostbus",   filter.bus_num);
    qdev_prop_set_uint32(&dev->qdev, "hostaddr",  filter.addr);
    qdev_prop_set_uint32(&dev->qdev, "vendorid",  filter.vendor_id);
    qdev_prop_set_uint32(&dev->qdev, "productid", filter.product_id);
    qdev_init_nofail(&dev->qdev);
    return dev;

fail:
    qdev_free(&dev->qdev);
    return NULL;
}

int usb_host_device_close(const char *devname)
{
#if 0
    char product_name[PRODUCT_NAME_SZ];
    int bus_num, addr;
    USBHostDevice *s;

    if (strstr(devname, "auto:")) {
        return usb_host_auto_del(devname);
    }
    if (usb_host_find_device(&bus_num, &addr, product_name,
                                    sizeof(product_name), devname) < 0) {
        return -1;
    }
    s = hostdev_find(bus_num, addr);
    if (s) {
        usb_device_delete_addr(s->bus_num, s->dev.addr);
        return 0;
    }
#endif

    return -1;
}

static int get_tag_value(char *buf, int buf_size,
                         const char *str, const char *tag,
                         const char *stopchars)
{
    const char *p;
    char *q;
    p = strstr(str, tag);
    if (!p) {
        return -1;
    }
    p += strlen(tag);
    while (qemu_isspace(*p)) {
        p++;
    }
    q = buf;
    while (*p != '\0' && !strchr(stopchars, *p)) {
        if ((q - buf) < (buf_size - 1)) {
            *q++ = *p;
        }
        p++;
    }
    *q = '\0';
    return q - buf;
}

/*
 * Use /proc/bus/usb/devices or /dev/bus/usb/devices file to determine
 * host's USB devices. This is legacy support since many distributions
 * are moving to /sys/bus/usb
 */
static int usb_host_scan_dev(void *opaque, USBScanFunc *func)
{
    FILE *f = NULL;
    char line[1024];
    char buf[1024];
    int bus_num, addr, speed, device_count;
    int class_id, product_id, vendor_id, port;
    char product_name[512];
    int ret = 0;

    if (!usb_host_device_path) {
        perror("husb: USB Host Device Path not set");
        goto the_end;
    }
    snprintf(line, sizeof(line), "%s/devices", usb_host_device_path);
    f = fopen(line, "r");
    if (!f) {
        perror("husb: cannot open devices file");
        goto the_end;
    }

    device_count = 0;
    bus_num = addr = class_id = product_id = vendor_id = port = 0;
    speed = -1; /* Can't get the speed from /[proc|dev]/bus/usb/devices */
    for(;;) {
        if (fgets(line, sizeof(line), f) == NULL) {
            break;
        }
        if (strlen(line) > 0) {
            line[strlen(line) - 1] = '\0';
        }
        if (line[0] == 'T' && line[1] == ':') {
            if (device_count && (vendor_id || product_id)) {
                /* New device.  Add the previously discovered device.  */
                if (port > 0) {
                    snprintf(buf, sizeof(buf), "%d", port);
                } else {
                    snprintf(buf, sizeof(buf), "?");
                }
                ret = func(opaque, bus_num, addr, buf, class_id, vendor_id,
                           product_id, product_name, speed);
                if (ret) {
                    goto the_end;
                }
            }
            if (get_tag_value(buf, sizeof(buf), line, "Bus=", " ") < 0) {
                goto fail;
            }
            bus_num = atoi(buf);
            if (get_tag_value(buf, sizeof(buf), line, "Port=", " ") < 0) {
                goto fail;
            }
            port = atoi(buf);
            if (get_tag_value(buf, sizeof(buf), line, "Dev#=", " ") < 0) {
                goto fail;
            }
            addr = atoi(buf);
            if (get_tag_value(buf, sizeof(buf), line, "Spd=", " ") < 0) {
                goto fail;
            }
            if (!strcmp(buf, "5000")) {
                speed = USB_SPEED_SUPER;
            } else if (!strcmp(buf, "480")) {
                speed = USB_SPEED_HIGH;
            } else if (!strcmp(buf, "1.5")) {
                speed = USB_SPEED_LOW;
            } else {
                speed = USB_SPEED_FULL;
            }
            product_name[0] = '\0';
            class_id = 0xff;
            device_count++;
            product_id = 0;
            vendor_id = 0;
        } else if (line[0] == 'P' && line[1] == ':') {
            if (get_tag_value(buf, sizeof(buf), line, "Vendor=", " ") < 0) {
                goto fail;
            }
            vendor_id = strtoul(buf, NULL, 16);
            if (get_tag_value(buf, sizeof(buf), line, "ProdID=", " ") < 0) {
                goto fail;
            }
            product_id = strtoul(buf, NULL, 16);
        } else if (line[0] == 'S' && line[1] == ':') {
            if (get_tag_value(buf, sizeof(buf), line, "Product=", "") < 0) {
                goto fail;
            }
            pstrcpy(product_name, sizeof(product_name), buf);
        } else if (line[0] == 'D' && line[1] == ':') {
            if (get_tag_value(buf, sizeof(buf), line, "Cls=", " (") < 0) {
                goto fail;
            }
            class_id = strtoul(buf, NULL, 16);
        }
    fail: ;
    }
    if (device_count && (vendor_id || product_id)) {
        /* Add the last device.  */
        if (port > 0) {
            snprintf(buf, sizeof(buf), "%d", port);
        } else {
            snprintf(buf, sizeof(buf), "?");
        }
        ret = func(opaque, bus_num, addr, buf, class_id, vendor_id,
                   product_id, product_name, speed);
    }
 the_end:
    if (f) {
        fclose(f);
    }
    return ret;
}

/*
 * Read sys file-system device file
 *
 * @line address of buffer to put file contents in
 * @line_size size of line
 * @device_file path to device file (printf format string)
 * @device_name device being opened (inserted into device_file)
 *
 * @return 0 failed, 1 succeeded ('line' contains data)
 */
static int usb_host_read_file(char *line, size_t line_size,
                              const char *device_file, const char *device_name)
{
    FILE *f;
    int ret = 0;
    char filename[PATH_MAX];

    snprintf(filename, PATH_MAX, USBSYSBUS_PATH "/devices/%s/%s", device_name,
             device_file);
    f = fopen(filename, "r");
    if (f) {
        ret = fgets(line, line_size, f) != NULL;
        fclose(f);
    }

    return ret;
}

/*
 * Use /sys/bus/usb/devices/ directory to determine host's USB
 * devices.
 *
 * This code is based on Robert Schiele's original patches posted to
 * the Novell bug-tracker https://bugzilla.novell.com/show_bug.cgi?id=241950
 */
static int usb_host_scan_sys(void *opaque, USBScanFunc *func)
{
    DIR *dir = NULL;
    char line[1024];
    int bus_num, addr, speed, class_id, product_id, vendor_id;
    int ret = 0;
    char port[MAX_PORTLEN];
    char product_name[512];
    struct dirent *de;

    dir = opendir(USBSYSBUS_PATH "/devices");
    if (!dir) {
        perror("husb: cannot open devices directory");
        goto the_end;
    }

    while ((de = readdir(dir))) {
        if (de->d_name[0] != '.' && !strchr(de->d_name, ':')) {
            if (sscanf(de->d_name, "%d-%7[0-9.]", &bus_num, port) < 2) {
                continue;
            }

            if (!usb_host_read_file(line, sizeof(line), "devnum", de->d_name)) {
                goto the_end;
            }
            if (sscanf(line, "%d", &addr) != 1) {
                goto the_end;
            }
            if (!usb_host_read_file(line, sizeof(line), "bDeviceClass",
                                    de->d_name)) {
                goto the_end;
            }
            if (sscanf(line, "%x", &class_id) != 1) {
                goto the_end;
            }

            if (!usb_host_read_file(line, sizeof(line), "idVendor",
                                    de->d_name)) {
                goto the_end;
            }
            if (sscanf(line, "%x", &vendor_id) != 1) {
                goto the_end;
            }
            if (!usb_host_read_file(line, sizeof(line), "idProduct",
                                    de->d_name)) {
                goto the_end;
            }
            if (sscanf(line, "%x", &product_id) != 1) {
                goto the_end;
            }
            if (!usb_host_read_file(line, sizeof(line), "product",
                                    de->d_name)) {
                *product_name = 0;
            } else {
                if (strlen(line) > 0) {
                    line[strlen(line) - 1] = '\0';
                }
                pstrcpy(product_name, sizeof(product_name), line);
            }

            if (!usb_host_read_file(line, sizeof(line), "speed", de->d_name)) {
                goto the_end;
            }
            if (!strcmp(line, "5000\n")) {
                speed = USB_SPEED_SUPER;
            } else if (!strcmp(line, "480\n")) {
                speed = USB_SPEED_HIGH;
            } else if (!strcmp(line, "1.5\n")) {
                speed = USB_SPEED_LOW;
            } else {
                speed = USB_SPEED_FULL;
            }

            ret = func(opaque, bus_num, addr, port, class_id, vendor_id,
                       product_id, product_name, speed);
            if (ret) {
                goto the_end;
            }
        }
    }
 the_end:
    if (dir) {
        closedir(dir);
    }
    return ret;
}

/*
 * Determine how to access the host's USB devices and call the
 * specific support function.
 */
static int usb_host_scan(void *opaque, USBScanFunc *func)
{
    Monitor *mon = cur_mon;
    FILE *f = NULL;
    DIR *dir = NULL;
    int ret = 0;
    const char *fs_type[] = {"unknown", "proc", "dev", "sys"};
    char devpath[PATH_MAX];

    /* only check the host once */
    if (!usb_fs_type) {
        dir = opendir(USBSYSBUS_PATH "/devices");
        if (dir) {
            /* devices found in /dev/bus/usb/ (yes - not a mistake!) */
            strcpy(devpath, USBDEVBUS_PATH);
            usb_fs_type = USB_FS_SYS;
            closedir(dir);
            DPRINTF(USBDBG_DEVOPENED, USBSYSBUS_PATH);
            goto found_devices;
        }
        f = fopen(USBPROCBUS_PATH "/devices", "r");
        if (f) {
            /* devices found in /proc/bus/usb/ */
            strcpy(devpath, USBPROCBUS_PATH);
            usb_fs_type = USB_FS_PROC;
            fclose(f);
            DPRINTF(USBDBG_DEVOPENED, USBPROCBUS_PATH);
            goto found_devices;
        }
        /* try additional methods if an access method hasn't been found yet */
        f = fopen(USBDEVBUS_PATH "/devices", "r");
        if (f) {
            /* devices found in /dev/bus/usb/ */
            strcpy(devpath, USBDEVBUS_PATH);
            usb_fs_type = USB_FS_DEV;
            fclose(f);
            DPRINTF(USBDBG_DEVOPENED, USBDEVBUS_PATH);
            goto found_devices;
        }
    found_devices:
        if (!usb_fs_type) {
            if (mon) {
                monitor_printf(mon, "husb: unable to access USB devices\n");
            }
            return -ENOENT;
        }

        /* the module setting (used later for opening devices) */
        usb_host_device_path = g_malloc0(strlen(devpath)+1);
        strcpy(usb_host_device_path, devpath);
        if (mon) {
            monitor_printf(mon, "husb: using %s file-system with %s\n",
                           fs_type[usb_fs_type], usb_host_device_path);
        }
    }

    switch (usb_fs_type) {
    case USB_FS_PROC:
    case USB_FS_DEV:
        ret = usb_host_scan_dev(opaque, func);
        break;
    case USB_FS_SYS:
        ret = usb_host_scan_sys(opaque, func);
        break;
    default:
        ret = -EINVAL;
        break;
    }
    return ret;
}

static QEMUTimer *usb_auto_timer;

static int usb_host_auto_scan(void *opaque, int bus_num,
                              int addr, const char *port,
                              int class_id, int vendor_id, int product_id,
                              const char *product_name, int speed)
{
    struct USBAutoFilter *f;
    struct USBHostDevice *s;

    /* Ignore hubs */
    if (class_id == 9)
        return 0;

    QTAILQ_FOREACH(s, &hostdevs, next) {
        f = &s->match;

        if (f->bus_num > 0 && f->bus_num != bus_num) {
            continue;
        }
        if (f->addr > 0 && f->addr != addr) {
            continue;
        }
        if (f->port != NULL && (port == NULL || strcmp(f->port, port) != 0)) {
            continue;
        }

        if (f->vendor_id > 0 && f->vendor_id != vendor_id) {
            continue;
        }

        if (f->product_id > 0 && f->product_id != product_id) {
            continue;
        }
        /* We got a match */
        s->seen++;
        if (s->errcount >= 3) {
            return 0;
        }

        /* Already attached ? */
        if (s->fd != -1) {
            return 0;
        }
        DPRINTF("husb: auto open: bus_num %d addr %d\n", bus_num, addr);

        if (usb_host_open(s, bus_num, addr, port, product_name, speed) < 0) {
            s->errcount++;
        }
        break;
    }

    return 0;
}

static void usb_host_auto_check(void *unused)
{
    struct USBHostDevice *s;
    int unconnected = 0;

    usb_host_scan(NULL, usb_host_auto_scan);

    QTAILQ_FOREACH(s, &hostdevs, next) {
        if (s->fd == -1) {
            unconnected++;
        }
        if (s->seen == 0) {
            s->errcount = 0;
        }
        s->seen = 0;
    }

    if (unconnected == 0) {
        /* nothing to watch */
        if (usb_auto_timer) {
            qemu_del_timer(usb_auto_timer);
            trace_usb_host_auto_scan_disabled();
        }
        return;
    }

    if (!usb_auto_timer) {
        usb_auto_timer = qemu_new_timer_ms(rt_clock, usb_host_auto_check, NULL);
        if (!usb_auto_timer) {
            return;
        }
        trace_usb_host_auto_scan_enabled();
    }
    qemu_mod_timer(usb_auto_timer, qemu_get_clock_ms(rt_clock) + 2000);
}

/*
 * Autoconnect filter
 * Format:
 *    auto:bus:dev[:vid:pid]
 *    auto:bus.dev[:vid:pid]
 *
 *    bus  - bus number    (dec, * means any)
 *    dev  - device number (dec, * means any)
 *    vid  - vendor id     (hex, * means any)
 *    pid  - product id    (hex, * means any)
 *
 *    See 'lsusb' output.
 */
static int parse_filter(const char *spec, struct USBAutoFilter *f)
{
    enum { BUS, DEV, VID, PID, DONE };
    const char *p = spec;
    int i;

    f->bus_num    = 0;
    f->addr       = 0;
    f->vendor_id  = 0;
    f->product_id = 0;

    for (i = BUS; i < DONE; i++) {
        p = strpbrk(p, ":.");
        if (!p) {
            break;
        }
        p++;

        if (*p == '*') {
            continue;
        }
        switch(i) {
        case BUS: f->bus_num = strtol(p, NULL, 10);    break;
        case DEV: f->addr    = strtol(p, NULL, 10);    break;
        case VID: f->vendor_id  = strtol(p, NULL, 16); break;
        case PID: f->product_id = strtol(p, NULL, 16); break;
        }
    }

    if (i < DEV) {
        fprintf(stderr, "husb: invalid auto filter spec %s\n", spec);
        return -1;
    }

    return 0;
}

/**********************/
/* USB host device info */

struct usb_class_info {
    int class;
    const char *class_name;
};

static const struct usb_class_info usb_class_info[] = {
    { USB_CLASS_AUDIO, "Audio"},
    { USB_CLASS_COMM, "Communication"},
    { USB_CLASS_HID, "HID"},
    { USB_CLASS_HUB, "Hub" },
    { USB_CLASS_PHYSICAL, "Physical" },
    { USB_CLASS_PRINTER, "Printer" },
    { USB_CLASS_MASS_STORAGE, "Storage" },
    { USB_CLASS_CDC_DATA, "Data" },
    { USB_CLASS_APP_SPEC, "Application Specific" },
    { USB_CLASS_VENDOR_SPEC, "Vendor Specific" },
    { USB_CLASS_STILL_IMAGE, "Still Image" },
    { USB_CLASS_CSCID, "Smart Card" },
    { USB_CLASS_CONTENT_SEC, "Content Security" },
    { -1, NULL }
};

static const char *usb_class_str(uint8_t class)
{
    const struct usb_class_info *p;
    for(p = usb_class_info; p->class != -1; p++) {
        if (p->class == class) {
            break;
        }
    }
    return p->class_name;
}

static void usb_info_device(Monitor *mon, int bus_num,
                            int addr, const char *port,
                            int class_id, int vendor_id, int product_id,
                            const char *product_name,
                            int speed)
{
    const char *class_str, *speed_str;

    switch(speed) {
    case USB_SPEED_LOW:
        speed_str = "1.5";
        break;
    case USB_SPEED_FULL:
        speed_str = "12";
        break;
    case USB_SPEED_HIGH:
        speed_str = "480";
        break;
    case USB_SPEED_SUPER:
        speed_str = "5000";
        break;
    default:
        speed_str = "?";
        break;
    }

    monitor_printf(mon, "  Bus %d, Addr %d, Port %s, Speed %s Mb/s\n",
                   bus_num, addr, port, speed_str);
    class_str = usb_class_str(class_id);
    if (class_str) {
        monitor_printf(mon, "    %s:", class_str);
    } else {
        monitor_printf(mon, "    Class %02x:", class_id);
    }
    monitor_printf(mon, " USB device %04x:%04x", vendor_id, product_id);
    if (product_name[0] != '\0') {
        monitor_printf(mon, ", %s", product_name);
    }
    monitor_printf(mon, "\n");
}

static int usb_host_info_device(void *opaque, int bus_num, int addr,
                                const char *path, int class_id,
                                int vendor_id, int product_id,
                                const char *product_name,
                                int speed)
{
    Monitor *mon = opaque;

    usb_info_device(mon, bus_num, addr, path, class_id, vendor_id, product_id,
                    product_name, speed);
    return 0;
}

static void dec2str(int val, char *str, size_t size)
{
    if (val == 0) {
        snprintf(str, size, "*");
    } else {
        snprintf(str, size, "%d", val);
    }
}

static void hex2str(int val, char *str, size_t size)
{
    if (val == 0) {
        snprintf(str, size, "*");
    } else {
        snprintf(str, size, "%04x", val);
    }
}

void usb_host_info(Monitor *mon)
{
    struct USBAutoFilter *f;
    struct USBHostDevice *s;

    usb_host_scan(mon, usb_host_info_device);

    if (QTAILQ_EMPTY(&hostdevs)) {
        return;
    }

    monitor_printf(mon, "  Auto filters:\n");
    QTAILQ_FOREACH(s, &hostdevs, next) {
        char bus[10], addr[10], vid[10], pid[10];
        f = &s->match;
        dec2str(f->bus_num, bus, sizeof(bus));
        dec2str(f->addr, addr, sizeof(addr));
        hex2str(f->vendor_id, vid, sizeof(vid));
        hex2str(f->product_id, pid, sizeof(pid));
        monitor_printf(mon, "    Bus %s, Addr %s, Port %s, ID %s:%s\n",
                       bus, addr, f->port ? f->port : "*", vid, pid);
    }
}
