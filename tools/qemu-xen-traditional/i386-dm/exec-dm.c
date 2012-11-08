/*
 *  virtual page mapping and translated block handling
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
#include "config.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/mman.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <inttypes.h>

#include "cpu.h"
#include "cpu-all.h"
#include "exec-all.h"
#include "hw.h"
#include "pc.h"
#include "disas.h"
#include "qemu-xen.h"

//#define DEBUG_TB_INVALIDATE
//#define DEBUG_FLUSH
//#define DEBUG_TLB

/* make various TB consistency checks */
//#define DEBUG_TB_CHECK 
//#define DEBUG_TLB_CHECK 

#ifndef CONFIG_DM
/* threshold to flush the translated code buffer */
#define CODE_GEN_BUFFER_MAX_SIZE (CODE_GEN_BUFFER_SIZE - CODE_GEN_MAX_SIZE)

#define SMC_BITMAP_USE_THRESHOLD 10

#define MMAP_AREA_START        0x00000000
#define MMAP_AREA_END          0xa8000000

TranslationBlock tbs[CODE_GEN_MAX_BLOCKS];
TranslationBlock *tb_hash[CODE_GEN_HASH_SIZE];
TranslationBlock *tb_phys_hash[CODE_GEN_PHYS_HASH_SIZE];
int nb_tbs;
/* any access to the tbs or the page table must use this lock */
spinlock_t tb_lock = SPIN_LOCK_UNLOCKED;

uint8_t code_gen_buffer[CODE_GEN_BUFFER_SIZE];
uint8_t *code_gen_ptr;
#endif /* !CONFIG_DM */

int use_icount = 0;
int64_t qemu_icount;
ram_addr_t phys_ram_size;
int phys_ram_fd;
uint8_t *phys_ram_base;
uint8_t *phys_ram_dirty;

CPUState *first_cpu;
/* current CPU in the current thread. It is only valid inside
   cpu_exec() */
CPUState *cpu_single_env; 

typedef struct PageDesc {
    /* list of TBs intersecting this ram page */
    TranslationBlock *first_tb;
    /* in order to optimize self modifying code, we count the number
       of lookups we do to a given page to use a bitmap */
    unsigned int code_write_count;
    uint8_t *code_bitmap;
#if defined(CONFIG_USER_ONLY)
    unsigned long flags;
#endif
} PageDesc;

typedef struct PhysPageDesc {
    /* offset in host memory of the page + io_index in the low 12 bits */
    unsigned long phys_offset;
} PhysPageDesc;

typedef struct VirtPageDesc {
    /* physical address of code page. It is valid only if 'valid_tag'
       matches 'virt_valid_tag' */ 
    target_ulong phys_addr; 
    unsigned int valid_tag;
#if !defined(CONFIG_SOFTMMU)
    /* original page access rights. It is valid only if 'valid_tag'
       matches 'virt_valid_tag' */
    unsigned int prot;
#endif
} VirtPageDesc;

#define L2_BITS 10
#define L1_BITS (32 - L2_BITS - TARGET_PAGE_BITS)

#define L1_SIZE (1 << L1_BITS)
#define L2_SIZE (1 << L2_BITS)

unsigned long qemu_real_host_page_size;
unsigned long qemu_host_page_bits;
unsigned long qemu_host_page_size;
unsigned long qemu_host_page_mask;

/* io memory support */
CPUWriteMemoryFunc *io_mem_write[IO_MEM_NB_ENTRIES][4];
CPUReadMemoryFunc *io_mem_read[IO_MEM_NB_ENTRIES][4];
void *io_mem_opaque[IO_MEM_NB_ENTRIES];
char io_mem_used[IO_MEM_NB_ENTRIES];

/* log support */
FILE *logfile;
int loglevel;

void cpu_exec_init_all(unsigned long tb_size)
{
}

void cpu_exec_init(CPUState *env)
{
    CPUState **penv;
    int cpu_index;

    env->next_cpu = NULL;
    penv = &first_cpu;
    cpu_index = 0;
    while (*penv != NULL) {
        penv = (CPUState **)&(*penv)->next_cpu;
        cpu_index++;
    }
    env->cpu_index = cpu_index;
    *penv = env;

    /* alloc dirty bits array */
    phys_ram_dirty = qemu_malloc(phys_ram_size >> TARGET_PAGE_BITS);
}

/* enable or disable low levels log */
void cpu_set_log(int log_flags)
{
    loglevel = log_flags;
    if (!logfile)
      logfile = stderr;
}

void cpu_set_log_filename(const char *filename)
{
    logfile = fopen(filename, "w");
    if (!logfile) {
        perror(filename);
	_exit(1);
    }
#if !defined(CONFIG_SOFTMMU)
    /* must avoid mmap() usage of glibc by setting a buffer "by hand" */
    {
        static uint8_t logfile_buf[4096];
	setvbuf(logfile, logfile_buf, _IOLBF, sizeof(logfile_buf));
    }
#else
    setvbuf(logfile, NULL, _IOLBF, 0);
#endif
    dup2(fileno(logfile), 1);
    dup2(fileno(logfile), 2);
}

/* mask must never be zero, except for A20 change call */
void cpu_interrupt(CPUState *env, int mask)
{
    env->interrupt_request |= mask;
}

void cpu_reset_interrupt(CPUState *env, int mask)
{
    env->interrupt_request &= ~mask;
}

const CPULogItem cpu_log_items[] = {
    { CPU_LOG_TB_OUT_ASM, "out_asm", 
      "show generated host assembly code for each compiled TB" },
    { CPU_LOG_TB_IN_ASM, "in_asm",
      "show target assembly code for each compiled TB" },
    { CPU_LOG_TB_OP, "op", 
      "show micro ops for each compiled TB (only usable if 'in_asm' used)" },
#ifdef TARGET_I386
    { CPU_LOG_TB_OP_OPT, "op_opt",
      "show micro ops after optimization for each compiled TB" },
#endif
    { CPU_LOG_INT, "int",
      "show interrupts/exceptions in short format" },
    { CPU_LOG_EXEC, "exec",
      "show trace before each executed TB (lots of logs)" },
    { CPU_LOG_TB_CPU, "cpu",
      "show CPU state before bloc translation" },
#ifdef TARGET_I386
    { CPU_LOG_PCALL, "pcall",
      "show protected mode far calls/returns/exceptions" },
#endif
#ifdef DEBUG_IOPORT
    { CPU_LOG_IOPORT, "ioport",
      "show all i/o ports accesses" },
#endif
    { 0, NULL, NULL },
};

static int cmp1(const char *s1, int n, const char *s2)
{
    if (strlen(s2) != n)
        return 0;
    return memcmp(s1, s2, n) == 0;
}
      
/* takes a comma separated list of log masks. Return 0 if error. */
int cpu_str_to_log_mask(const char *str)
{
    const CPULogItem *item;
    int mask;
    const char *p, *p1;

    p = str;
    mask = 0;
    for(;;) {
        p1 = strchr(p, ',');
        if (!p1)
            p1 = p + strlen(p);
	if(cmp1(p,p1-p,"all")) {
		for(item = cpu_log_items; item->mask != 0; item++) {
			mask |= item->mask;
		}
	} else {
        for(item = cpu_log_items; item->mask != 0; item++) {
            if (cmp1(p, p1 - p, item->name))
                goto found;
        }
        return 0;
	}
    found:
        mask |= item->mask;
        if (*p1 != ',')
            break;
        p = p1 + 1;
    }
    return mask;
}

void cpu_abort(CPUState *env, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "qemu: fatal: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    abort();
}


/* XXX: Simple implementation. Fix later */
#define MAX_MMIO 1024
static struct mmio_space {
        target_phys_addr_t start;
        unsigned long size;
        unsigned long io_index;
} mmio[MAX_MMIO];
static unsigned long mmio_cnt;

/* register physical memory. 'size' must be a multiple of the target
   page size. If (phys_offset & ~TARGET_PAGE_MASK) != 0, then it is an
   io memory page */
void cpu_register_physical_memory(target_phys_addr_t start_addr, 
				  ram_addr_t size,
				  ram_addr_t phys_offset)
{
    int i;

    for (i = 0; i < mmio_cnt; i++) { 
        if(mmio[i].start == start_addr) {
            mmio[i].io_index = phys_offset;
            mmio[i].size = size;
            return;
        }
    }

    if (mmio_cnt == MAX_MMIO) {
        fprintf(logfile, "too many mmio regions\n");
        exit(-1);
    }

    mmio[mmio_cnt].io_index = phys_offset;
    mmio[mmio_cnt].start = start_addr;
    mmio[mmio_cnt++].size = size;
}

static int get_free_io_mem_idx(void)
{
    int i;

    /* Leave 1st element empty */
    for (i = 1; i<IO_MEM_NB_ENTRIES; i++)
        if (!io_mem_used[i]) {
            io_mem_used[i] = 1;
            return i;
        }

    return -1;
}

/* mem_read and mem_write are arrays of functions containing the
   function to access byte (index 0), word (index 1) and dword (index
   2). All functions must be supplied. If io_index is non zero, the
   corresponding io zone is modified. If it is zero, a new io zone is
   allocated. The return value can be used with
   cpu_register_physical_memory(). (-1) is returned if error. */
int cpu_register_io_memory(int io_index,
                           CPUReadMemoryFunc **mem_read,
                           CPUWriteMemoryFunc **mem_write,
                           void *opaque)
{
    int i;

    if (io_index <= 0) {
        io_index = get_free_io_mem_idx();
        if (io_index == -1)
            return io_index;
    } else {
        if (io_index >= IO_MEM_NB_ENTRIES)
            return -1;
    }
    
    for(i = 0;i < 3; i++) {
        io_mem_read[io_index][i] = mem_read[i];
        io_mem_write[io_index][i] = mem_write[i];
    }
    io_mem_opaque[io_index] = opaque;
    return io_index << IO_MEM_SHIFT;
}

void cpu_unregister_io_memory(int io_table_address)
{
    int i;
    int io_index = io_table_address >> IO_MEM_SHIFT;

    for (i = 0; i < mmio_cnt; i++) {
	if (mmio[i].io_index == io_index) {
	   mmio[i].start = mmio[i].size = 0;
	   break;
	}
    }

    for (i=0;i < 3; i++) {
        io_mem_read[io_index][i] = NULL;
        io_mem_write[io_index][i] = NULL;
    }
    io_mem_opaque[io_index] = NULL;
    io_mem_used[io_index] = 0;
}

void cpu_physical_memory_set_dirty(ram_addr_t addr)
{
    phys_ram_dirty[addr >> TARGET_PAGE_BITS] = 0xff;

    if (xen_logdirty_enable)
        xc_hvm_modified_memory(xc_handle, domid, addr >> TARGET_PAGE_BITS, 1);
}

CPUWriteMemoryFunc **cpu_get_io_memory_write(int io_index)
{
    return io_mem_write[io_index >> IO_MEM_SHIFT];
}

CPUReadMemoryFunc **cpu_get_io_memory_read(int io_index)
{
    return io_mem_read[io_index >> IO_MEM_SHIFT];
}

#ifdef __ia64__

#define __ia64_fc(addr)	asm volatile ("fc %0" :: "r"(addr) : "memory")
#define ia64_sync_i()	asm volatile (";; sync.i" ::: "memory")
#define ia64_srlz_i()	asm volatile (";; srlz.i ;;" ::: "memory")

/* IA64 has seperate I/D cache, with coherence maintained by DMA controller.
 * So to emulate right behavior that guest OS is assumed, we need to flush
 * I/D cache here.
 */
static void sync_icache(uint8_t *address, int len)
{
    unsigned long addr = (unsigned long)address;
    unsigned long end = addr + len;

    for (addr &= ~(32UL-1); addr < end; addr += 32UL)
        __ia64_fc(addr);

    ia64_sync_i();
    ia64_srlz_i();
}
#endif 

/* physical memory access (slow version, mainly for debug) */
#if defined(CONFIG_USER_ONLY)
void cpu_physical_memory_rw(target_phys_addr_t addr, uint8_t *buf, 
                            int len, int is_write)
{
    int l, flags;
    target_ulong page;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        flags = page_get_flags(page);
        if (!(flags & PAGE_VALID))
            return;
        if (is_write) {
            if (!(flags & PAGE_WRITE))
                return;
            memcpy((uint8_t *)addr, buf, len);
        } else {
            if (!(flags & PAGE_READ))
                return;
            memcpy(buf, (uint8_t *)addr, len);
        }
        len -= l;
        buf += l;
        addr += l;
    }
}
#else

static int iomem_index(target_phys_addr_t addr)
{
        int i;

        for (i = 0; i < mmio_cnt; i++) {
                unsigned long start, end;

                start = mmio[i].start;
                end = mmio[i].start + mmio[i].size;

                if ((addr >= start) && (addr < end)){
                        return (mmio[i].io_index >> IO_MEM_SHIFT) & (IO_MEM_NB_ENTRIES - 1);
                }
        }
        return 0;
}

void unregister_iomem(target_phys_addr_t start)
{
    unsigned int index;

    for (index = 0; index < mmio_cnt; index++)
        if (start == mmio[index].start)
            break;
    if (index < mmio_cnt) {
        fprintf(logfile, "squash iomem [%lx, %lx).\n",
		(unsigned long)(mmio[index].start),
                (unsigned long)(mmio[index].start + mmio[index].size));
        mmio[index].size = 0;
    }
}


unsigned int xen_logdirty_enable = 0;

/*
 * Replace the standard byte memcpy with a word memcpy for appropriately sized
 * memory copy operations.  Some users (USB-UHCI) can not tolerate the possible
 * word tearing that can result from a guest concurrently writing a memory
 * structure while the qemu device model is modifying the same location.
 * Forcing a word-sized read/write prevents the guest from seeing a partially
 * written word-sized atom.
 */
#if defined(__x86_64__) || defined(__i386__)
static void memcpy_words(void *dst, void *src, size_t n)
{
    asm volatile (
        "   movl %%edx,%%ecx \n"
#ifdef __x86_64__
        "   shrl $3,%%ecx    \n"
        "   rep  movsq       \n"
        "   test $4,%%edx    \n"
        "   jz   1f          \n"
        "   movsl            \n"
#else /* __i386__ */
        "   shrl $2,%%ecx    \n"
        "   rep  movsl       \n"
#endif
        "1: test $2,%%edx    \n"
        "   jz   1f          \n"
        "   movsw            \n"
        "1: test $1,%%edx    \n"
        "   jz   1f          \n"
        "   movsb            \n"
        "1:                  \n"
        : "+S" (src), "+D" (dst) : "d" (n) : "ecx", "memory" );
}
#else
static void memcpy_words(void *dst, void *src, size_t n)
{
    /* Some architectures do not like unaligned accesses. */
    if (((unsigned long)dst | (unsigned long)src) & 3) {
        memcpy(dst, src, n);
        return;
    }

    while (n >= sizeof(uint32_t)) {
        *((uint32_t *)dst) = *((uint32_t *)src);
        dst = ((uint32_t *)dst) + 1;
        src = ((uint32_t *)src) + 1;
        n -= sizeof(uint32_t);
    }

    if (n & 2) {
        *((uint16_t *)dst) = *((uint16_t *)src);
        dst = ((uint16_t *)dst) + 1;
        src = ((uint16_t *)src) + 1;
    }

    if (n & 1) {
        *((uint8_t *)dst) = *((uint8_t *)src);
        dst = ((uint8_t *)dst) + 1;
        src = ((uint8_t *)src) + 1;
    }
}
#endif

void cpu_physical_memory_rw(target_phys_addr_t _addr, uint8_t *buf, 
                            int _len, int is_write)
{
    target_phys_addr_t addr = _addr;
    int len = _len;
    int l, io_index;
    uint8_t *ptr;
    uint32_t val;

    mapcache_lock();

    while (len > 0) {
        /* How much can we copy before the next page boundary? */
        l = TARGET_PAGE_SIZE - (addr & ~TARGET_PAGE_MASK); 
        if (l > len)
            l = len;

        io_index = iomem_index(addr);
        if (is_write) {
            if (io_index) {
                if (l >= 4 && ((addr & 3) == 0)) {
                    /* 32 bit read access */
                    val = ldl_raw(buf);
                    io_mem_write[io_index][2](io_mem_opaque[io_index], addr, val);
                    l = 4;
                } else if (l >= 2 && ((addr & 1) == 0)) {
                    /* 16 bit read access */
                    val = lduw_raw(buf);
                    io_mem_write[io_index][1](io_mem_opaque[io_index], addr, val);
                    l = 2;
                } else {
                    /* 8 bit access */
                    val = ldub_raw(buf);
                    io_mem_write[io_index][0](io_mem_opaque[io_index], addr, val);
                    l = 1;
                }
            } else if ((ptr = phys_ram_addr(addr)) != NULL) {
                /* Writing to RAM */
                memcpy_words(ptr, buf, l);

                if (xen_logdirty_enable)
                    xc_hvm_modified_memory(xc_handle,
                        domid,
                        addr >> TARGET_PAGE_BITS,
                        ((addr + l + TARGET_PAGE_SIZE - 1) >> TARGET_PAGE_BITS)
                        - (addr >> TARGET_PAGE_BITS));
#ifdef __ia64__
                sync_icache(ptr, l);
#endif 
            }
        } else {
            if (io_index) {
                if (l >= 4 && ((addr & 3) == 0)) {
                    /* 32 bit read access */
                    val = io_mem_read[io_index][2](io_mem_opaque[io_index], addr);
                    stl_raw(buf, val);
                    l = 4;
                } else if (l >= 2 && ((addr & 1) == 0)) {
                    /* 16 bit read access */
                    val = io_mem_read[io_index][1](io_mem_opaque[io_index], addr);
                    stw_raw(buf, val);
                    l = 2;
                } else {
                    /* 8 bit access */
                    val = io_mem_read[io_index][0](io_mem_opaque[io_index], addr);
                    stb_raw(buf, val);
                    l = 1;
                }
            } else if ((ptr = phys_ram_addr(addr)) != NULL) {
                /* Reading from RAM */
                memcpy_words(buf, ptr, l);
            } else {
                /* Neither RAM nor known MMIO space */
                memset(buf, 0xff, len); 
            }
        }
        len -= l;
        buf += l;
        addr += l;
    }

    mapcache_unlock();
}
#endif

/* virtual memory access for debug */
int cpu_memory_rw_debug(CPUState *env, target_ulong addr, 
                        uint8_t *buf, int len, int is_write)
{
    int l;
    target_ulong page, phys_addr;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        phys_addr = cpu_get_phys_page_debug(env, page);
        /* if no physical page mapped, return an error */
        if (phys_addr == -1)
            return -1;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        cpu_physical_memory_rw(phys_addr + (addr & ~TARGET_PAGE_MASK), 
                               buf, l, is_write);
        len -= l;
        buf += l;
        addr += l;
    }
    return 0;
}

void cpu_physical_memory_reset_dirty(ram_addr_t start, ram_addr_t end,
                                     int dirty_flags)
{
	unsigned long length;
	int i, mask, len;
	uint8_t *p;

	start &= TARGET_PAGE_MASK;
	end = TARGET_PAGE_ALIGN(end);

	length = end - start;
	if (length == 0)
		return;
	mask = ~dirty_flags;
	p = phys_ram_dirty + (start >> TARGET_PAGE_BITS);
	len = length >> TARGET_PAGE_BITS;
	for(i = 0; i < len; i++)
		p[i] &= mask;

	return;
}


/* Unoptimised in Xen DM, nicked from git
 *  aab33094073678d459ccaac5c60ea7533e8d1d8e */
uint32_t ldub_phys(target_phys_addr_t addr)
{
    uint8_t val;
    cpu_physical_memory_read(addr, &val, 1);
    return val;
}
uint32_t lduw_phys(target_phys_addr_t addr)
{
    uint16_t val;
    cpu_physical_memory_read(addr, (uint8_t *)&val, 2);
    return tswap16(val);
}
uint64_t ldq_phys(target_phys_addr_t addr)
{
    uint64_t val;
    cpu_physical_memory_read(addr, (uint8_t *)&val, 8);
    return tswap64(val);
}
void stb_phys(target_phys_addr_t addr, uint32_t val)
{
    uint8_t v = val;
    cpu_physical_memory_write(addr, &v, 1);
}
void stw_phys(target_phys_addr_t addr, uint32_t val)
{
    uint16_t v = tswap16(val);
    cpu_physical_memory_write(addr, (const uint8_t *)&v, 2);
}
void stq_phys(target_phys_addr_t addr, uint64_t val)
{
    val = tswap64(val);
    cpu_physical_memory_write(addr, (const uint8_t *)&val, 8);
}

/* stubs which we hope (think!) are OK for Xen DM */
void stl_phys(target_phys_addr_t addr, uint32_t val)
{
    val = tswap32(val);
    cpu_physical_memory_write(addr, (const uint8_t *)&val, 4);
}
void stl_phys_notdirty(target_phys_addr_t addr, uint32_t val)
{
    stl_phys(addr, val);
}
uint32_t ldl_phys(target_phys_addr_t addr)
{
    uint32_t val;
    cpu_physical_memory_read(addr, (uint8_t *)&val, 4);
    return tswap32(val);
}

void cpu_physical_memory_write_rom(target_phys_addr_t addr, 
                                   const uint8_t *buf, int len) {
    return cpu_physical_memory_write(addr,buf,len);
}

void qemu_register_coalesced_mmio(target_phys_addr_t addr, ram_addr_t size) { }
void qemu_unregister_coalesced_mmio(target_phys_addr_t addr, ram_addr_t size) { }

/* stub out various functions for Xen DM */
void dump_exec_info(FILE *f,
                    int (*cpu_fprintf)(FILE *f, const char *fmt, ...)) {
}
void monitor_disas(CPUState *env,
                   target_ulong pc, int nb_insn, int is_physical, int flags) {
}
void irq_info(void) { }
void pic_info(void) { }




/*
 * This next section was clone-and-hacked from the version in exec.c
 * :-(.  But the exec.c version is full of tcg-specific stuff and
 * assumptions about phys_ram_base.
 */

typedef struct {
    void *buffer;
    target_phys_addr_t addr;
    target_phys_addr_t len;
} BounceBuffer;

static BounceBuffer bounce;

typedef struct MapClient {
    void *opaque;
    void (*callback)(void *opaque);
    LIST_ENTRY(MapClient) link;
} MapClient;

static LIST_HEAD(map_client_list, MapClient) map_client_list
    = LIST_HEAD_INITIALIZER(map_client_list);

void *cpu_register_map_client(void *opaque, void (*callback)(void *opaque))
{
    MapClient *client = qemu_malloc(sizeof(*client));

    client->opaque = opaque;
    client->callback = callback;
    LIST_INSERT_HEAD(&map_client_list, client, link);
    return client;
}

void cpu_unregister_map_client(void *_client)
{
    MapClient *client = (MapClient *)_client;

    LIST_REMOVE(client, link);
    qemu_free(client);
}

static void cpu_notify_map_clients(void)
{
    MapClient *client;

    while (!LIST_EMPTY(&map_client_list)) {
        client = LIST_FIRST(&map_client_list);
        client->callback(client->opaque);
        cpu_unregister_map_client(client);
    }
}

/* Map a physical memory region into a host virtual address.
 * May map a subset of the requested range, given by and returned in *plen.
 * May return NULL if resources needed to perform the mapping are exhausted.
 * Use only for reads OR writes - not for read-modify-write operations.
 * Use cpu_register_map_client() to know when retrying the map operation is
 * likely to succeed.
 */
void *cpu_physical_memory_map(target_phys_addr_t addr,
                              target_phys_addr_t *plen,
                              int is_write)
{
    unsigned long l = 0;
#ifdef MAPCACHE
    l = MCACHE_BUCKET_SIZE - (addr & (MCACHE_BUCKET_SIZE-1));
    if ((*plen) > l)
        *plen = l;
#endif
    if (xen_logdirty_enable)
        xc_hvm_modified_memory(xc_handle, domid, addr >> TARGET_PAGE_BITS,
                ((addr + l + TARGET_PAGE_SIZE - 1) >> TARGET_PAGE_BITS)
                    - (addr >> TARGET_PAGE_BITS));

    return qemu_map_cache(addr, 1);
}

/* Unmaps a memory region previously mapped by cpu_physical_memory_map().
 * Will also mark the memory as dirty if is_write == 1.  access_len gives
 * the amount of memory that was actually read or written by the caller.
 */
void cpu_physical_memory_unmap(void *buffer, target_phys_addr_t len,
                               int is_write, target_phys_addr_t access_len)
{
    qemu_invalidate_entry(buffer);
    cpu_notify_map_clients();
}
