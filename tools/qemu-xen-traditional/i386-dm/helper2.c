/*
 *  i386 helpers (without register variable usage)
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Main cpu loop for handling I/O requests coming from a virtual machine
 * Copyright © 2004, Intel Corporation.
 * Copyright © 2005, International Business Machines Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 USA.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>
#include <assert.h>
#include <sys/select.h>

#include <limits.h>
#include <fcntl.h>

#include <xenctrl.h>
#include <xen/hvm/ioreq.h>
#include <xen/hvm/hvm_info_table.h>

#include "cpu.h"
#include "exec-all.h"
#include "hw.h"
#include "pci.h"
#include "console.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "qemu-xen.h"

//#define DEBUG_MMU

#ifdef USE_CODE_COPY
#include <asm/ldt.h>
#include <linux/unistd.h>
#include <linux/version.h>

_syscall3(int, modify_ldt, int, func, void *, ptr, unsigned long, bytecount)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 66)
#define modify_ldt_ldt_s user_desc
#endif
#endif /* USE_CODE_COPY */

#include "hw.h"

int domid = -1;
int vcpus = 1;
/* use 32b array to record whatever vcpu number bitmap */
/* do not use 64b array to avoid underflow/overflow when strtol */
uint32_t vcpu_avail[(HVM_MAX_VCPUS + 31)/32] = {0};

xc_interface *xc_handle;

char domain_name[64] = "Xen-no-name";

int domid_backend = 0;
  /* 0 for now.  If we ever have non-dom0 backend domains, this
   * will have to be the domid of the real backend domain.
   * For stubdom, this is the domain of the _real_ backend
   * not of the stubdom.  So still 0 unless we're in a driver
   * domain case.
   */

long time_offset = 0;

shared_iopage_t *shared_page = NULL;

#define BUFFER_IO_MAX_DELAY  100
buffered_iopage_t *buffered_io_page = NULL;
QEMUTimer *buffered_io_timer;

/* the evtchn fd for polling */
xc_interface *xce_handle = NULL;

/* which vcpu we are serving */
int send_vcpu = 0;

//the evtchn port for polling the notification,
evtchn_port_t *ioreq_local_port;
/* evtchn local port for buffered io */
evtchn_port_t bufioreq_local_port;

CPUX86State *cpu_x86_init(const char *cpu_model)
{
    CPUX86State *env;
    static int inited;
    int i, rc;
    unsigned long bufioreq_evtchn;

    env = qemu_mallocz(sizeof(CPUX86State));
    if (!env)
        return NULL;
    cpu_exec_init(env);

    /* There is no shared_page for PV, we're done now */
    if (shared_page == NULL)
        return env;

    ioreq_local_port = 
        (evtchn_port_t *)qemu_mallocz(vcpus * sizeof(evtchn_port_t));
    if (!ioreq_local_port)
        return NULL;

    /* init various static tables */
    if (!inited) {
        inited = 1;

        cpu_single_env = env;

        xce_handle = xc_evtchn_open(NULL, 0);
        if (xce_handle == NULL) {
            perror("open");
            return NULL;
        }

        /* FIXME: how about if we overflow the page here? */
        for (i = 0; i < vcpus; i++) {
            rc = xc_evtchn_bind_interdomain(
                xce_handle, domid, shared_page->vcpu_ioreq[i].vp_eport);
            if (rc == -1) {
                fprintf(logfile, "bind interdomain ioctl error %d\n", errno);
                return NULL;
            }
            ioreq_local_port[i] = rc;
        }
        rc = xc_get_hvm_param(xc_handle, domid, HVM_PARAM_BUFIOREQ_EVTCHN,
                &bufioreq_evtchn);
        if (rc < 0) {
            fprintf(logfile, "failed to get HVM_PARAM_BUFIOREQ_EVTCHN error=%d\n",
                    errno);
            return NULL;
        }
        rc = xc_evtchn_bind_interdomain(xce_handle, domid, (uint32_t)bufioreq_evtchn);
        if (rc == -1) {
            fprintf(logfile, "bind interdomain ioctl error %d\n", errno);
            return NULL;
        }
        bufioreq_local_port = rc;
    }

    return env;
}

/* called from main_cpu_reset */
void cpu_reset(CPUX86State *env)
{
    extern int s3_shutdown_flag;
    xc_interface *xcHandle;
    int sts;
 
    if (s3_shutdown_flag)
        return;

    xcHandle = xc_interface_open(0,0,0);
    if (!xcHandle)
        fprintf(logfile, "Cannot acquire xenctrl handle\n");
    else {
        xc_domain_shutdown_hook(xcHandle, domid);
        sts = xc_domain_shutdown(xcHandle, domid, SHUTDOWN_reboot);
        if (sts != 0)
            fprintf(logfile,
                    "? xc_domain_shutdown failed to issue reboot, sts %d\n",
                    sts);
        else
            fprintf(logfile, "Issued domain %d reboot\n", domid);
        xc_interface_close(xcHandle);
    }
}

void cpu_x86_close(CPUX86State *env)
{
    free(env);
    free(ioreq_local_port);
}


void cpu_dump_state(CPUState *env, FILE *f,
                    int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags)
{
}

/***********************************************************/
/* x86 mmu */
/* XXX: add PGE support */

void cpu_x86_set_a20(CPUX86State *env, int a20_state)
{
    a20_state = (a20_state != 0);
    if (a20_state != ((env->a20_mask >> 20) & 1)) {
#if defined(DEBUG_MMU)
        printf("A20 update: a20=%d\n", a20_state);
#endif
        env->a20_mask = 0xffefffff | (a20_state << 20);
    }
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
        return addr;
}

//some functions to handle the io req packet
static void sp_info(void)
{
    ioreq_t *req;
    int i;

    for (i = 0; i < vcpus; i++) {
        req = &shared_page->vcpu_ioreq[i];
        term_printf("vcpu %d: event port %d\n", i, ioreq_local_port[i]);
        term_printf("  req state: %x, ptr: %x, addr: %"PRIx64", "
                    "data: %"PRIx64", count: %u, size: %u\n",
                    req->state, req->data_is_ptr, req->addr,
                    req->data, req->count, req->size);
    }
}

//get the ioreq packets from share mem
static ioreq_t *__cpu_get_ioreq(int vcpu)
{
    ioreq_t *req = &shared_page->vcpu_ioreq[vcpu];

    if (req->state != STATE_IOREQ_READY) {
        fprintf(logfile, "I/O request not ready: "
                "%x, ptr: %x, port: %"PRIx64", "
                "data: %"PRIx64", count: %u, size: %u\n",
                req->state, req->data_is_ptr, req->addr,
                req->data, req->count, req->size);
        return NULL;
    }

    xen_rmb(); /* see IOREQ_READY /then/ read contents of ioreq */

    req->state = STATE_IOREQ_INPROCESS;
    return req;
}

//use poll to get the port notification
//ioreq_vec--out,the
//retval--the number of ioreq packet
static ioreq_t *cpu_get_ioreq(void)
{
    int i;
    evtchn_port_t port;

    port = xc_evtchn_pending(xce_handle);
    if (port == bufioreq_local_port) {
        qemu_mod_timer(buffered_io_timer,
                BUFFER_IO_MAX_DELAY + qemu_get_clock(rt_clock));
        return NULL;
    }
 
    if (port != -1) {
        for ( i = 0; i < vcpus; i++ )
            if ( ioreq_local_port[i] == port )
                break;

        if ( i == vcpus ) {
            fprintf(logfile, "Fatal error while trying to get io event!\n");
            exit(1);
        }

        // unmask the wanted port again
        xc_evtchn_unmask(xce_handle, port);

        //get the io packet from shared memory
        send_vcpu = i;
        return __cpu_get_ioreq(i);
    }

    //read error or read nothing
    return NULL;
}

static unsigned long do_inp(CPUState *env, unsigned long addr,
                            unsigned long size)
{
    switch(size) {
    case 1:
        return cpu_inb(env, addr);
    case 2:
        return cpu_inw(env, addr);
    case 4:
        return cpu_inl(env, addr);
    default:
        fprintf(logfile, "inp: bad size: %lx %lx\n", addr, size);
        exit(-1);
    }
}

static void do_outp(CPUState *env, unsigned long addr,
                    unsigned long size, unsigned long val)
{
    switch(size) {
    case 1:
        return cpu_outb(env, addr, val);
    case 2:
        return cpu_outw(env, addr, val);
    case 4:
        return cpu_outl(env, addr, val);
    default:
        fprintf(logfile, "outp: bad size: %lx %lx\n", addr, size);
        exit(-1);
    }
}

static inline void read_physical(uint64_t addr, unsigned long size, void *val)
{
    return cpu_physical_memory_rw((target_phys_addr_t)addr, val, size, 0);
}

static inline void write_physical(uint64_t addr, unsigned long size, void *val)
{
    return cpu_physical_memory_rw((target_phys_addr_t)addr, val, size, 1);
}

static void cpu_ioreq_pio(CPUState *env, ioreq_t *req)
{
    int i, sign;

    sign = req->df ? -1 : 1;

    if (req->dir == IOREQ_READ) {
        if (!req->data_is_ptr) {
            req->data = do_inp(env, req->addr, req->size);
        } else {
            unsigned long tmp;

            for (i = 0; i < req->count; i++) {
                tmp = do_inp(env, req->addr, req->size);
                write_physical((target_phys_addr_t) req->data
                  + (sign * i * req->size),
                  req->size, &tmp);
            }
        }
    } else if (req->dir == IOREQ_WRITE) {
        if (!req->data_is_ptr) {
            do_outp(env, req->addr, req->size, req->data);
        } else {
            for (i = 0; i < req->count; i++) {
                unsigned long tmp = 0;

                read_physical((target_phys_addr_t) req->data
                  + (sign * i * req->size),
                  req->size, &tmp);
                do_outp(env, req->addr, req->size, tmp);
            }
        }
    }
}

static void cpu_ioreq_move(CPUState *env, ioreq_t *req)
{
    int i, sign;

    sign = req->df ? -1 : 1;

    if (!req->data_is_ptr) {
        if (req->dir == IOREQ_READ) {
            for (i = 0; i < req->count; i++) {
                read_physical(req->addr
                  + (sign * i * req->size),
                  req->size, &req->data);
            }
        } else if (req->dir == IOREQ_WRITE) {
            for (i = 0; i < req->count; i++) {
                write_physical(req->addr
                  + (sign * i * req->size),
                  req->size, &req->data);
            }
        }
    } else {
        target_ulong tmp;

        if (req->dir == IOREQ_READ) {
            for (i = 0; i < req->count; i++) {
                read_physical(req->addr
                  + (sign * i * req->size),
                  req->size, &tmp);
                write_physical((target_phys_addr_t )req->data
                  + (sign * i * req->size),
                  req->size, &tmp);
            }
        } else if (req->dir == IOREQ_WRITE) {
            for (i = 0; i < req->count; i++) {
                read_physical((target_phys_addr_t) req->data
                  + (sign * i * req->size),
                  req->size, &tmp);
                write_physical(req->addr
                  + (sign * i * req->size),
                  req->size, &tmp);
            }
        }
    }
}

void timeoffset_get(void)
{
    char *p;

    p = xenstore_vm_read(domid, "rtc/timeoffset", NULL);
    if (!p)
	return;

    if (sscanf(p, "%ld", &time_offset) == 1)
	fprintf(logfile, "Time offset set %ld\n", time_offset);
    else
	time_offset = 0;

    free(p);
}

static void cpu_ioreq_timeoffset(CPUState *env, ioreq_t *req)
{
    char b[64];

    time_offset += (unsigned long)req->data;

    fprintf(logfile, "Time offset set %ld, added offset %"PRId64"\n",
        time_offset, req->data);
    sprintf(b, "%ld", time_offset);
    xenstore_vm_write(domid, "rtc/timeoffset", b);
}

static void __handle_ioreq(CPUState *env, ioreq_t *req)
{
    if (!req->data_is_ptr && (req->dir == IOREQ_WRITE) &&
        (req->size < sizeof(target_ulong)))
        req->data &= ((target_ulong)1 << (8 * req->size)) - 1;

    switch (req->type) {
    case IOREQ_TYPE_PIO:
        cpu_ioreq_pio(env, req);
        break;
    case IOREQ_TYPE_COPY:
        cpu_ioreq_move(env, req);
        break;
    case IOREQ_TYPE_TIMEOFFSET:
        cpu_ioreq_timeoffset(env, req);
        break;
    case IOREQ_TYPE_INVALIDATE:
        qemu_invalidate_map_cache();
        break;
    default:
        hw_error("Invalid ioreq type 0x%x\n", req->type);
    }
}

static int __handle_buffered_iopage(CPUState *env)
{
    buf_ioreq_t *buf_req = NULL;
    ioreq_t req;
    int qw;

    if (!buffered_io_page)
        return 0;

    memset(&req, 0x00, sizeof(req));

    while (buffered_io_page->read_pointer !=
           buffered_io_page->write_pointer) {
        buf_req = &buffered_io_page->buf_ioreq[
            buffered_io_page->read_pointer % IOREQ_BUFFER_SLOT_NUM];
        req.size = 1UL << buf_req->size;
        req.count = 1;
        req.addr = buf_req->addr;
        req.data = buf_req->data;
        req.state = STATE_IOREQ_READY;
        req.dir = buf_req->dir;
        req.df = 1;
        req.type = buf_req->type;
        req.data_is_ptr = 0;
        qw = (req.size == 8);
        if (qw) {
            buf_req = &buffered_io_page->buf_ioreq[
                (buffered_io_page->read_pointer+1) % IOREQ_BUFFER_SLOT_NUM];
            req.data |= ((uint64_t)buf_req->data) << 32;
        }

        __handle_ioreq(env, &req);

        xen_mb();
        buffered_io_page->read_pointer += qw ? 2 : 1;
    }

    return req.count;
}

static void handle_buffered_io(void *opaque)
{
    CPUState *env = opaque;

    if (__handle_buffered_iopage(env)) {
        qemu_mod_timer(buffered_io_timer,
                BUFFER_IO_MAX_DELAY + qemu_get_clock(rt_clock));
    } else {
        qemu_del_timer(buffered_io_timer);
        xc_evtchn_unmask(xce_handle, bufioreq_local_port);
    }
}

static void cpu_handle_ioreq(void *opaque)
{
    extern int shutdown_requested;
    CPUState *env = opaque;
    ioreq_t *req = cpu_get_ioreq();

    __handle_buffered_iopage(env);
    if (req) {
        __handle_ioreq(env, req);

        if (req->state != STATE_IOREQ_INPROCESS) {
            fprintf(logfile, "Badness in I/O request ... not in service?!: "
                    "%x, ptr: %x, port: %"PRIx64", "
                    "data: %"PRIx64", count: %u, size: %u\n",
                    req->state, req->data_is_ptr, req->addr,
                    req->data, req->count, req->size);
            destroy_hvm_domain();
            return;
        }

        xen_wmb(); /* Update ioreq contents /then/ update state. */

	/*
         * We do this before we send the response so that the tools
         * have the opportunity to pick up on the reset before the
         * guest resumes and does a hlt with interrupts disabled which
         * causes Xen to powerdown the domain.
         */
        if (vm_running) {
            if (qemu_shutdown_requested()) {
		fprintf(logfile, "shutdown requested in cpu_handle_ioreq\n");
		destroy_hvm_domain();
	    }
	    if (qemu_reset_requested()) {
		fprintf(logfile, "reset requested in cpu_handle_ioreq.\n");
		qemu_system_reset();
	    }
	}

        req->state = STATE_IORESP_READY;
        xc_evtchn_notify(xce_handle, ioreq_local_port[send_vcpu]);
    }
}

int xen_pause_requested;

int main_loop(void)
{
    CPUState *env = cpu_single_env;
    int evtchn_fd = xce_handle == NULL ? -1 : xc_evtchn_fd(xce_handle);
    char *qemu_file;
    fd_set fds;

    main_loop_prepare();

    buffered_io_timer = qemu_new_timer(rt_clock, handle_buffered_io,
				       cpu_single_env);

    if (evtchn_fd != -1)
        qemu_set_fd_handler(evtchn_fd, cpu_handle_ioreq, NULL, env);

    xenstore_record_dm_state("running");

    qemu_set_fd_handler(xenstore_fd(), xenstore_process_event, NULL, NULL);

    while (1) {
        while (!(vm_running && xen_pause_requested))
#ifdef CONFIG_STUBDOM
            /* Wait up to 10 msec. */
            main_loop_wait(10);
#else
            /* Wait up to 1h. */
            main_loop_wait(1000*60*60);
#endif

        fprintf(logfile, "device model saving state\n");

        /* Pull all outstanding ioreqs through the system */
        handle_buffered_pio();
        handle_buffered_io(env);
        main_loop_wait(1); /* For the select() on events */

        /* Save the device state */
        asprintf(&qemu_file, "/var/lib/xen/qemu-save.%d", domid);
        do_savevm(qemu_file);
        free(qemu_file);

        xenstore_record_dm_state("paused");

        /* Wait to be allowed to continue */
        while (xen_pause_requested) {
            FD_ZERO(&fds);
            FD_SET(xenstore_fd(), &fds);
            if (select(xenstore_fd() + 1, &fds, NULL, NULL, NULL) > 0)
                xenstore_process_event(NULL);
        }

        xenstore_record_dm_state("running");
    }

    return 0;
}

void destroy_hvm_domain(void)
{
    xc_interface *xcHandle;
    int sts;
 
    xcHandle = xc_interface_open(0,0,0);
    if (!xcHandle)
        fprintf(logfile, "Cannot acquire xenctrl handle\n");
    else {
        sts = xc_domain_shutdown(xcHandle, domid, SHUTDOWN_poweroff);
        if (sts != 0)
            fprintf(logfile, "? xc_domain_shutdown failed to issue poweroff, "
                    "sts %d, errno %d\n", sts, errno);
        else
            fprintf(logfile, "Issued domain %d poweroff\n", domid);
        xc_interface_close(xcHandle);
    }
}
