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
#ifndef __LIBELF_PRIVATE_H__
#define __LIBELF_PRIVATE_H_

#ifdef __XEN__

#include <xen/config.h>
#include <xen/types.h>
#include <xen/string.h>
#include <xen/lib.h>
#include <xen/libelf.h>
#include <asm/byteorder.h>
#include <public/elfnote.h>

/* we would like to use elf->log_callback but we can't because
 * there is no vprintk in Xen */
#define elf_msg(elf, fmt, args ... ) \
   if (elf->verbose) printk(fmt, ## args )
#define elf_err(elf, fmt, args ... ) \
   printk(fmt, ## args )

#define strtoull(str, end, base) simple_strtoull(str, end, base)
#define bswap_16(x) swab16(x)
#define bswap_32(x) swab32(x)
#define bswap_64(x) swab64(x)

#else /* !__XEN__ */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <inttypes.h>
#ifdef __sun__
#include <sys/byteorder.h>
#define bswap_16(x) BSWAP_16(x)
#define bswap_32(x) BSWAP_32(x)
#define bswap_64(x) BSWAP_64(x)
#elif defined(__NetBSD__)
#include <sys/bswap.h>
#define bswap_16(x) bswap16(x)
#define bswap_32(x) bswap32(x)
#define bswap_64(x) bswap64(x)
#elif defined(__OpenBSD__)
#include <machine/endian.h>
#define bswap_16(x) swap16(x)
#define bswap_32(x) swap32(x)
#define bswap_64(x) swap64(x)
#elif defined(__linux__) || defined(__Linux__) || defined(__MINIOS__)
#include <byteswap.h>
#else
#error Unsupported OS
#endif
#include <xen/elfnote.h>
#include <xen/libelf/libelf.h>

#include "xenctrl.h"
#include "xc_private.h"

#define elf_msg(elf, fmt, args ... )                    \
    elf_call_log_callback(elf, 0, fmt , ## args );
#define elf_err(elf, fmt, args ... )                    \
    elf_call_log_callback(elf, 1, fmt , ## args );

void elf_call_log_callback(struct elf_binary*, int iserr, const char *fmt,...);

#define safe_strcpy(d,s)                        \
do { strncpy((d),(s),sizeof((d))-1);            \
     (d)[sizeof((d))-1] = '\0';                 \
} while (0)

#endif

#endif /* __LIBELF_PRIVATE_H_ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
