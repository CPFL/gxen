/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef XC_PRIVATE_H
#define XC_PRIVATE_H

#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include "xenctrl.h"
#include "xenctrlosdep.h"

#include <xen/sys/privcmd.h>

/* valgrind cannot see when a hypercall has filled in some values.  For this
   reason, we must zero the privcmd_hypercall_t or domctl/sysctl instance
   before a call, if using valgrind.  */
#ifdef VALGRIND
#define DECLARE_HYPERCALL privcmd_hypercall_t hypercall = { 0 }
#define DECLARE_DOMCTL struct xen_domctl domctl = { 0 }
#define DECLARE_SYSCTL struct xen_sysctl sysctl = { 0 }
#define DECLARE_PHYSDEV_OP struct physdev_op physdev_op = { 0 }
#define DECLARE_FLASK_OP struct xen_flask_op op = { 0 }
#else
#define DECLARE_HYPERCALL privcmd_hypercall_t hypercall
#define DECLARE_DOMCTL struct xen_domctl domctl
#define DECLARE_SYSCTL struct xen_sysctl sysctl
#define DECLARE_PHYSDEV_OP struct physdev_op physdev_op
#define DECLARE_FLASK_OP struct xen_flask_op op
#endif

#undef PAGE_SHIFT
#undef PAGE_SIZE
#undef PAGE_MASK
#define PAGE_SHIFT              XC_PAGE_SHIFT
#define PAGE_SIZE               XC_PAGE_SIZE
#define PAGE_MASK               XC_PAGE_MASK

/* Force a compilation error if condition is true */
#define XC_BUILD_BUG_ON(p) ((void)sizeof(struct { int:-!!(p); }))

/*
** Define max dirty page cache to permit during save/restore -- need to balance 
** keeping cache usage down with CPU impact of invalidating too often.
** (Currently 16MB)
*/
#define MAX_PAGECACHE_USAGE (4*1024)

struct xc_interface_core {
    enum xc_osdep_type type;
    int flags;
    xentoollog_logger *error_handler,   *error_handler_tofree;
    xentoollog_logger *dombuild_logger, *dombuild_logger_tofree;
    struct xc_error last_error; /* for xc_get_last_error */
    FILE *dombuild_logger_file;
    const char *currently_progress_reporting;

    /*
     * A simple cache of unused, single page, hypercall buffers
     *
     * Protected by a global lock.
     */
#define HYPERCALL_BUFFER_CACHE_SIZE 4
    int hypercall_buffer_cache_nr;
    void *hypercall_buffer_cache[HYPERCALL_BUFFER_CACHE_SIZE];

    /*
     * Hypercall buffer statistics. All protected by the global
     * hypercall_buffer_cache lock.
     */
    int hypercall_buffer_total_allocations;
    int hypercall_buffer_total_releases;
    int hypercall_buffer_current_allocations;
    int hypercall_buffer_maximum_allocations;
    int hypercall_buffer_cache_hits;
    int hypercall_buffer_cache_misses;
    int hypercall_buffer_cache_toobig;

    /* Low lovel OS interface */
    xc_osdep_info_t  osdep;
    xc_osdep_ops    *ops; /* backend operations */
    xc_osdep_handle  ops_handle; /* opaque data for xc_osdep_ops */
};

void xc_report_error(xc_interface *xch, int code, const char *fmt, ...);
void xc_reportv(xc_interface *xch, xentoollog_logger *lg, xentoollog_level,
                int code, const char *fmt, va_list args)
     __attribute__((format(printf,5,0)));
void xc_report(xc_interface *xch, xentoollog_logger *lg, xentoollog_level,
               int code, const char *fmt, ...)
     __attribute__((format(printf,5,6)));

void xc_report_progress_start(xc_interface *xch, const char *doing,
                              unsigned long total);
void xc_report_progress_step(xc_interface *xch,
                             unsigned long done, unsigned long total);

/* anamorphic macros:  struct xc_interface *xch  must be in scope */

#define IPRINTF(_f, _a...) xc_report(xch, xch->error_handler, XTL_INFO,0, _f , ## _a)
#define DPRINTF(_f, _a...) xc_report(xch, xch->error_handler, XTL_DETAIL,0, _f , ## _a)
#define DBGPRINTF(_f, _a...) xc_report(xch, xch->error_handler, XTL_DEBUG,0, _f , ## _a)

#define ERROR(_m, _a...)  xc_report_error(xch,XC_INTERNAL_ERROR,_m , ## _a )
#define PERROR(_m, _a...) xc_report_error(xch,XC_INTERNAL_ERROR,_m \
                  " (%d = %s)", ## _a , errno, xc_strerror(xch, errno))

/*
 * HYPERCALL ARGUMENT BUFFERS
 *
 * Augment the public hypercall buffer interface with the ability to
 * bounce between user provided buffers and hypercall safe memory.
 *
 * Use xc_hypercall_bounce_pre/post instead of
 * xc_hypercall_buffer_alloc/free(_pages).  The specified user
 * supplied buffer is automatically copied in/out of the hypercall
 * safe memory.
 */
enum {
    XC_HYPERCALL_BUFFER_BOUNCE_NONE = 0,
    XC_HYPERCALL_BUFFER_BOUNCE_IN   = 1,
    XC_HYPERCALL_BUFFER_BOUNCE_OUT  = 2,
    XC_HYPERCALL_BUFFER_BOUNCE_BOTH = 3
};

/*
 * Declare a named bounce buffer.
 *
 * Normally you should use DECLARE_HYPERCALL_BOUNCE (see below).
 *
 * This declaration should only be used when the user pointer is
 * non-trivial, e.g. when it is contained within an existing data
 * structure.
 */
#define DECLARE_NAMED_HYPERCALL_BOUNCE(_name, _ubuf, _sz, _dir) \
    xc_hypercall_buffer_t XC__HYPERCALL_BUFFER_NAME(_name) = {  \
        .hbuf = NULL,                                           \
        .param_shadow = NULL,                                   \
        .sz = _sz, .dir = _dir, .ubuf = _ubuf,                  \
    }

/*
 * Declare a bounce buffer shadowing the named user data pointer.
 */
#define DECLARE_HYPERCALL_BOUNCE(_ubuf, _sz, _dir) DECLARE_NAMED_HYPERCALL_BOUNCE(_ubuf, _ubuf, _sz, _dir)

/*
 * Set the size of data to bounce. Useful when the size is not known
 * when the bounce buffer is declared.
 */
#define HYPERCALL_BOUNCE_SET_SIZE(_buf, _sz) do { (HYPERCALL_BUFFER(_buf))->sz = _sz; } while (0)

/*
 * Initialise and free hypercall safe memory. Takes care of any required
 * copying.
 */
int xc__hypercall_bounce_pre(xc_interface *xch, xc_hypercall_buffer_t *bounce);
#define xc_hypercall_bounce_pre(_xch, _name) xc__hypercall_bounce_pre(_xch, HYPERCALL_BUFFER(_name))
void xc__hypercall_bounce_post(xc_interface *xch, xc_hypercall_buffer_t *bounce);
#define xc_hypercall_bounce_post(_xch, _name) xc__hypercall_bounce_post(_xch, HYPERCALL_BUFFER(_name))

/*
 * Release hypercall buffer cache
 */
void xc__hypercall_buffer_cache_release(xc_interface *xch);

/*
 * Hypercall interfaces.
 */

int do_xen_hypercall(xc_interface *xch, privcmd_hypercall_t *hypercall);

static inline int do_xen_version(xc_interface *xch, int cmd, xc_hypercall_buffer_t *dest)
{
    DECLARE_HYPERCALL;
    DECLARE_HYPERCALL_BUFFER_ARGUMENT(dest);

    hypercall.op     = __HYPERVISOR_xen_version;
    hypercall.arg[0] = (unsigned long) cmd;
    hypercall.arg[1] = HYPERCALL_BUFFER_AS_ARG(dest);

    return do_xen_hypercall(xch, &hypercall);
}

static inline int do_physdev_op(xc_interface *xch, int cmd, void *op, size_t len)
{
    int ret = -1;
    DECLARE_HYPERCALL;
    DECLARE_HYPERCALL_BOUNCE(op, len, XC_HYPERCALL_BUFFER_BOUNCE_BOTH);

    if ( xc_hypercall_bounce_pre(xch, op) )
    {
        PERROR("Could not bounce memory for physdev hypercall");
        goto out1;
    }

    hypercall.op = __HYPERVISOR_physdev_op;
    hypercall.arg[0] = (unsigned long) cmd;
    hypercall.arg[1] = HYPERCALL_BUFFER_AS_ARG(op);

    if ( (ret = do_xen_hypercall(xch, &hypercall)) < 0 )
    {
        if ( errno == EACCES )
            DPRINTF("physdev operation failed -- need to"
                    " rebuild the user-space tool set?\n");
    }

    xc_hypercall_bounce_post(xch, op);
out1:
    return ret;
}

static inline int do_domctl(xc_interface *xch, struct xen_domctl *domctl)
{
    int ret = -1;
    DECLARE_HYPERCALL;
    DECLARE_HYPERCALL_BOUNCE(domctl, sizeof(*domctl), XC_HYPERCALL_BUFFER_BOUNCE_BOTH);

    domctl->interface_version = XEN_DOMCTL_INTERFACE_VERSION;

    if ( xc_hypercall_bounce_pre(xch, domctl) )
    {
        PERROR("Could not bounce buffer for domctl hypercall");
        goto out1;
    }

    hypercall.op     = __HYPERVISOR_domctl;
    hypercall.arg[0] = HYPERCALL_BUFFER_AS_ARG(domctl);

    if ( (ret = do_xen_hypercall(xch, &hypercall)) < 0 )
    {
        if ( errno == EACCES )
            DPRINTF("domctl operation failed -- need to"
                    " rebuild the user-space tool set?\n");
    }

    xc_hypercall_bounce_post(xch, domctl);
 out1:
    return ret;
}

static inline int do_sysctl(xc_interface *xch, struct xen_sysctl *sysctl)
{
    int ret = -1;
    DECLARE_HYPERCALL;
    DECLARE_HYPERCALL_BOUNCE(sysctl, sizeof(*sysctl), XC_HYPERCALL_BUFFER_BOUNCE_BOTH);

    sysctl->interface_version = XEN_SYSCTL_INTERFACE_VERSION;

    if ( xc_hypercall_bounce_pre(xch, sysctl) )
    {
        PERROR("Could not bounce buffer for sysctl hypercall");
        goto out1;
    }

    hypercall.op     = __HYPERVISOR_sysctl;
    hypercall.arg[0] = HYPERCALL_BUFFER_AS_ARG(sysctl);
    if ( (ret = do_xen_hypercall(xch, &hypercall)) < 0 )
    {
        if ( errno == EACCES )
            DPRINTF("sysctl operation failed -- need to"
                    " rebuild the user-space tool set?\n");
    }

    xc_hypercall_bounce_post(xch, sysctl);
 out1:
    return ret;
}

int do_memory_op(xc_interface *xch, int cmd, void *arg, size_t len);

void *xc_map_foreign_ranges(xc_interface *xch, uint32_t dom,
                            size_t size, int prot, size_t chunksize,
                            privcmd_mmap_entry_t entries[], int nentries);

int xc_get_pfn_type_batch(xc_interface *xch, uint32_t dom,
                          unsigned int num, xen_pfn_t *);

void bitmap_64_to_byte(uint8_t *bp, const uint64_t *lp, int nbits);
void bitmap_byte_to_64(uint64_t *lp, const uint8_t *bp, int nbits);

/* Optionally flush file to disk and discard page cache */
void discard_file_cache(xc_interface *xch, int fd, int flush);

#define MAX_MMU_UPDATES 1024
struct xc_mmu {
    mmu_update_t updates[MAX_MMU_UPDATES];
    int          idx;
    domid_t      subject;
};
/* Structure returned by xc_alloc_mmu_updates must be free()'ed by caller. */
struct xc_mmu *xc_alloc_mmu_updates(xc_interface *xch, domid_t dom);
int xc_add_mmu_update(xc_interface *xch, struct xc_mmu *mmu,
                   unsigned long long ptr, unsigned long long val);
int xc_flush_mmu_updates(xc_interface *xch, struct xc_mmu *mmu);

/* Return 0 on success; -1 on error setting errno. */
int read_exact(int fd, void *data, size_t size); /* EOF => -1, errno=0 */
int write_exact(int fd, const void *data, size_t size);

int xc_ffs8(uint8_t x);
int xc_ffs16(uint16_t x);
int xc_ffs32(uint32_t x);
int xc_ffs64(uint64_t x);

#define DOMPRINTF(fmt, args...) xc_dom_printf(dom->xch, fmt, ## args)
#define DOMPRINTF_CALLED(xch) xc_dom_printf((xch), "%s: called", __FUNCTION__)

#endif /* __XC_PRIVATE_H__ */
