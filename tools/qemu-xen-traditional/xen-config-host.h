#ifndef XEN_CONFIG_HOST_H
#define XEN_CONFIG_HOST_H

#ifdef CONFIG_STUBDOM
#undef CONFIG_AIO
#define NO_UNIX_SOCKETS 1
#define NO_BLUETOOTH_PASSTHROUGH 1
#endif

#define CONFIG_DM
#define CONFIG_XEN

extern char domain_name[64];
extern int domid, domid_backend;

#include <errno.h>
#include <stdbool.h>

#include "xenctrl.h"
#include "xenstore.h"
#if !defined(CONFIG_STUBDOM) && !defined(__NetBSD__)
#include "blktaplib.h"
#endif

#define BIOS_SIZE ((256 + 64) * 1024)

#undef CONFIG_GDBSTUB

void main_loop_prepare(void);

extern xc_interface *xc_handle;
extern int xen_pause_requested;
extern int vcpus;
extern uint32_t vcpu_avail[];

#ifdef CONFIG_STUBDOM
#define bdrv_host_device bdrv_raw
#endif
struct CharDriverState;
void xenstore_store_serial_port_info(int i, struct CharDriverState *chr,
				     const char *devname);
void xenstore_store_pv_console_info(int i, struct CharDriverState *chr,
		             const char *devname);

extern unsigned int xen_logdirty_enable;

#ifdef CONFIG_STUBDOM
#undef HAVE_IOVEC
#endif

#endif /*XEN_CONFIG_HOST_H*/
