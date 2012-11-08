/******************************************************************************
 * kexec.c - Achitecture independent kexec code for Xen
 *
 * Xen port written by:
 * - Simon 'Horms' Horman <horms@verge.net.au>
 * - Magnus Damm <magnus@valinux.co.jp>
 */

#include <xen/init.h>
#include <xen/lib.h>
#include <xen/acpi.h>
#include <xen/ctype.h>
#include <xen/errno.h>
#include <xen/guest_access.h>
#include <xen/sched.h>
#include <xen/types.h>
#include <xen/kexec.h>
#include <xen/keyhandler.h>
#include <public/kexec.h>
#include <xen/cpumask.h>
#include <asm/atomic.h>
#include <xen/spinlock.h>
#include <xen/version.h>
#include <xen/console.h>
#include <xen/kexec.h>
#include <public/elfnote.h>
#include <xsm/xsm.h>
#include <xen/cpu.h>
#ifdef CONFIG_COMPAT
#include <compat/kexec.h>
#endif

bool_t kexecing = FALSE;

/* Memory regions to store the per cpu register state etc. on a crash. */
typedef struct { Elf_Note * start; size_t size; } crash_note_range_t;
static crash_note_range_t * crash_notes;

/* Lock to prevent race conditions when allocating the crash note buffers.
 * It also serves to protect calls to alloc_from_crash_heap when allocating
 * crash note buffers in lower memory. */
static DEFINE_SPINLOCK(crash_notes_lock);

static Elf_Note *xen_crash_note;

static cpumask_t crash_saved_cpus;

static xen_kexec_image_t kexec_image[KEXEC_IMAGE_NR];

#define KEXEC_FLAG_DEFAULT_POS   (KEXEC_IMAGE_NR + 0)
#define KEXEC_FLAG_CRASH_POS     (KEXEC_IMAGE_NR + 1)
#define KEXEC_FLAG_IN_PROGRESS   (KEXEC_IMAGE_NR + 2)

static unsigned long kexec_flags = 0; /* the lowest bits are for KEXEC_IMAGE... */

static spinlock_t kexec_lock = SPIN_LOCK_UNLOCKED;

static unsigned char vmcoreinfo_data[VMCOREINFO_BYTES];
static size_t vmcoreinfo_size = 0;

xen_kexec_reserve_t kexec_crash_area;
static struct {
    u64 start, end;
    unsigned long size;
} ranges[16] __initdata;

/* Low crashinfo mode.  Start as INVALID so serveral codepaths can set up
 * defaults without needing to know the state of the others. */
enum low_crashinfo low_crashinfo_mode = LOW_CRASHINFO_INVALID;

/* This value is only considered if low_crash_mode is set to MIN or ALL, so
 * setting a default here is safe. Default to 4GB.  This is because the current
 * KEXEC_CMD_get_range compat hypercall trucates 64bit pointers to 32 bits. The
 * typical usecase for crashinfo_maxaddr will be for 64bit Xen with 32bit dom0
 * and 32bit crash kernel. */
static paddr_t __initdata crashinfo_maxaddr = 4ULL << 30;

/* = log base 2 of crashinfo_maxaddr after checking for sanity. Default to
 * larger than the entire physical address space. */
paddr_t crashinfo_maxaddr_bits = 64;

/* Pointers to keep track of the crash heap region. */
static void *crash_heap_current = NULL, *crash_heap_end = NULL;

/*
 * Parse command lines in the format
 *
 *   crashkernel=<ramsize-range>:<size>[,...][@<offset>]
 *
 * with <ramsize-range> being of form
 *
 *   <start>-[<end>]
 *
 * as well as the legacy ones in the format
 *
 *   crashkernel=<size>[@<offset>]
 */
static void __init parse_crashkernel(const char *str)
{
    const char *cur;

    if ( strchr(str, ':' ) )
    {
        unsigned int idx = 0;

        do {
            if ( idx >= ARRAY_SIZE(ranges) )
            {
                printk(XENLOG_WARNING "crashkernel: too many ranges\n");
                cur = NULL;
                str = strchr(str, '@');
                break;
            }

            ranges[idx].start = parse_size_and_unit(cur = str + !!idx, &str);
            if ( cur == str )
                break;

            if ( *str != '-' )
            {
                printk(XENLOG_WARNING "crashkernel: '-' expected\n");
                break;
            }

            if ( *++str != ':' )
            {
                ranges[idx].end = parse_size_and_unit(cur = str, &str);
                if ( cur == str )
                    break;
                if ( ranges[idx].end <= ranges[idx].start )
                {
                    printk(XENLOG_WARNING "crashkernel: end <= start\n");
                    break;
                }
            }
            else
                ranges[idx].end = -1;

            if ( *str != ':' )
            {
                printk(XENLOG_WARNING "crashkernel: ':' expected\n");
                break;
            }

            ranges[idx].size = parse_size_and_unit(cur = str + 1, &str);
            if ( cur == str )
                break;

            ++idx;
        } while ( *str == ',' );
        if ( idx < ARRAY_SIZE(ranges) )
            ranges[idx].size = 0;
    }
    else
        kexec_crash_area.size = parse_size_and_unit(cur = str, &str);
    if ( cur != str && *str == '@' )
        kexec_crash_area.start = parse_size_and_unit(cur = str + 1, &str);
    if ( cur == str )
        printk(XENLOG_WARNING "crashkernel: memory value expected\n");
}
custom_param("crashkernel", parse_crashkernel);

/* Parse command lines in the format:
 *
 *   low_crashinfo=[none,min,all]
 *
 * - none disables the low allocation of crash info.
 * - min will allocate enough low information for the crash kernel to be able
 *       to extract the hypervisor and dom0 message ring buffers.
 * - all will allocate additional structures such as domain and vcpu structs
 *       low so the crash kernel can perform an extended analysis of state.
 */
static void __init parse_low_crashinfo(const char * str)
{

    if ( !strlen(str) )
        /* default to min if user just specifies "low_crashinfo" */
        low_crashinfo_mode = LOW_CRASHINFO_MIN;
    else if ( !strcmp(str, "none" ) )
        low_crashinfo_mode = LOW_CRASHINFO_NONE;
    else if ( !strcmp(str, "min" ) )
        low_crashinfo_mode = LOW_CRASHINFO_MIN;
    else if ( !strcmp(str, "all" ) )
        low_crashinfo_mode = LOW_CRASHINFO_ALL;
    else
    {
        printk("Unknown low_crashinfo parameter '%s'.  Defaulting to min.\n", str);
        low_crashinfo_mode = LOW_CRASHINFO_MIN;
    }
}
custom_param("low_crashinfo", parse_low_crashinfo);

/* Parse command lines in the format:
 *
 *   crashinfo_maxaddr=<addr>
 *
 * <addr> will be rounded down to the nearest power of two.  Defaults to 64G
 */
static void __init parse_crashinfo_maxaddr(const char * str)
{
    u64 addr;

    /* if low_crashinfo_mode is unset, default to min. */
    if ( low_crashinfo_mode == LOW_CRASHINFO_INVALID )
        low_crashinfo_mode = LOW_CRASHINFO_MIN;

    if ( (addr = parse_size_and_unit(str, NULL)) )
        crashinfo_maxaddr = addr;
    else
        printk("Unable to parse crashinfo_maxaddr. Defaulting to %"PRIpaddr"\n",
               crashinfo_maxaddr);
}
custom_param("crashinfo_maxaddr", parse_crashinfo_maxaddr);

void __init set_kexec_crash_area_size(u64 system_ram)
{
    unsigned int idx;

    for ( idx = 0; idx < ARRAY_SIZE(ranges) && !kexec_crash_area.size; ++idx )
    {
        if ( !ranges[idx].size )
            break;

        if ( ranges[idx].size >= system_ram )
        {
            printk(XENLOG_WARNING "crashkernel: invalid size\n");
            continue;
        }

        if ( ranges[idx].start <= system_ram && ranges[idx].end > system_ram )
            kexec_crash_area.size = ranges[idx].size;
    }
}

static void one_cpu_only(void)
{
    /* Only allow the first cpu to continue - force other cpus to spin */
    if ( test_and_set_bit(KEXEC_FLAG_IN_PROGRESS, &kexec_flags) )
        for ( ; ; ) ;
}

/* Save the registers in the per-cpu crash note buffer. */
void kexec_crash_save_cpu(void)
{
    int cpu = smp_processor_id();
    Elf_Note *note;
    ELF_Prstatus *prstatus;
    crash_xen_core_t *xencore;

    BUG_ON ( ! crash_notes );

    if ( cpumask_test_and_set_cpu(cpu, &crash_saved_cpus) )
        return;

    note = crash_notes[cpu].start;

    prstatus = (ELF_Prstatus *)ELFNOTE_DESC(note);

    note = ELFNOTE_NEXT(note);
    xencore = (crash_xen_core_t *)ELFNOTE_DESC(note);

    elf_core_save_regs(&prstatus->pr_reg, xencore);
}

/* Set up the single Xen-specific-info crash note. */
crash_xen_info_t *kexec_crash_save_info(void)
{
    int cpu = smp_processor_id();
    crash_xen_info_t info;
    crash_xen_info_t *out = (crash_xen_info_t *)ELFNOTE_DESC(xen_crash_note);

    BUG_ON(!cpumask_test_and_set_cpu(cpu, &crash_saved_cpus));

    memset(&info, 0, sizeof(info));
    info.xen_major_version = xen_major_version();
    info.xen_minor_version = xen_minor_version();
    info.xen_extra_version = __pa(xen_extra_version());
    info.xen_changeset = __pa(xen_changeset());
    info.xen_compiler = __pa(xen_compiler());
    info.xen_compile_date = __pa(xen_compile_date());
    info.xen_compile_time = __pa(xen_compile_time());
    info.tainted = tainted;

    /* Copy from guaranteed-aligned local copy to possibly-unaligned dest. */
    memcpy(out, &info, sizeof(info));

    return out;
}

static void kexec_common_shutdown(void)
{
    watchdog_disable();
    console_start_sync();
    spin_debug_disable();
    one_cpu_only();
    acpi_dmar_reinstate();
}

void kexec_crash(void)
{
    int pos;

    pos = (test_bit(KEXEC_FLAG_CRASH_POS, &kexec_flags) != 0);
    if ( !test_bit(KEXEC_IMAGE_CRASH_BASE + pos, &kexec_flags) )
        return;

    kexecing = TRUE;

    kexec_common_shutdown();
    kexec_crash_save_cpu();
    machine_crash_shutdown();
    machine_kexec(&kexec_image[KEXEC_IMAGE_CRASH_BASE + pos]);

    BUG();
}

static long kexec_reboot(void *_image)
{
    xen_kexec_image_t *image = _image;

    kexecing = TRUE;

    kexec_common_shutdown();
    machine_reboot_kexec(image);

    BUG();
    return 0;
}

static void do_crashdump_trigger(unsigned char key)
{
    printk("'%c' pressed -> triggering crashdump\n", key);
    kexec_crash();
    printk(" * no crash kernel loaded!\n");
}

static struct keyhandler crashdump_trigger_keyhandler = {
    .u.fn = do_crashdump_trigger,
    .desc = "trigger a crashdump"
};

static void setup_note(Elf_Note *n, const char *name, int type, int descsz)
{
    int l = strlen(name) + 1;
    strlcpy(ELFNOTE_NAME(n), name, l);
    n->namesz = l;
    n->descsz = descsz;
    n->type = type;
}

static size_t sizeof_note(const char *name, int descsz)
{
    return (sizeof(Elf_Note) +
            ELFNOTE_ALIGN(strlen(name)+1) +
            ELFNOTE_ALIGN(descsz));
}

static size_t sizeof_cpu_notes(const unsigned long cpu)
{
    /* All CPUs present a PRSTATUS and crash_xen_core note. */
    size_t bytes =
        + sizeof_note("CORE", sizeof(ELF_Prstatus)) +
        + sizeof_note("Xen", sizeof(crash_xen_core_t));

    /* CPU0 also presents the crash_xen_info note. */
    if ( ! cpu )
        bytes = bytes +
            sizeof_note("Xen", sizeof(crash_xen_info_t));

    return bytes;
}

/* Allocate size_t bytes of space from the previously allocated
 * crash heap if the user has requested that crash notes be allocated
 * in lower memory.  There is currently no case where the crash notes
 * should be free()'d. */
static void * alloc_from_crash_heap(const size_t bytes)
{
    void * ret;
    if ( crash_heap_current + bytes > crash_heap_end )
        return NULL;
    ret = (void*)crash_heap_current;
    crash_heap_current += bytes;
    return ret;
}

/* Allocate a crash note buffer for a newly onlined cpu. */
static int kexec_init_cpu_notes(const unsigned long cpu)
{
    Elf_Note * note = NULL;
    int ret = 0;
    int nr_bytes = 0;

    BUG_ON( cpu >= nr_cpu_ids || ! crash_notes );

    /* If already allocated, nothing to do. */
    if ( crash_notes[cpu].start )
        return ret;

    nr_bytes = sizeof_cpu_notes(cpu);

    /* If we dont care about the position of allocation, malloc. */
    if ( low_crashinfo_mode == LOW_CRASHINFO_NONE )
        note = xzalloc_bytes(nr_bytes);

    /* Protect the write into crash_notes[] with a spinlock, as this function
     * is on a hotplug path and a hypercall path. */
    spin_lock(&crash_notes_lock);

    /* If we are racing with another CPU and it has beaten us, give up
     * gracefully. */
    if ( crash_notes[cpu].start )
    {
        spin_unlock(&crash_notes_lock);
        /* Always return ok, because whether we successfully allocated or not,
         * another CPU has successfully allocated. */
        if ( note )
            xfree(note);
    }
    else
    {
        /* If we care about memory possition, alloc from the crash heap,
         * also protected by the crash_notes_lock. */
        if ( low_crashinfo_mode > LOW_CRASHINFO_NONE )
            note = alloc_from_crash_heap(nr_bytes);

        crash_notes[cpu].start = note;
        crash_notes[cpu].size = nr_bytes;
        spin_unlock(&crash_notes_lock);

        /* If the allocation failed, and another CPU did not beat us, give
         * up with ENOMEM. */
        if ( ! note )
            ret = -ENOMEM;
        /* else all is good so lets set up the notes. */
        else
        {
            /* Set up CORE note. */
            setup_note(note, "CORE", NT_PRSTATUS, sizeof(ELF_Prstatus));
            note = ELFNOTE_NEXT(note);

            /* Set up Xen CORE note. */
            setup_note(note, "Xen", XEN_ELFNOTE_CRASH_REGS,
                       sizeof(crash_xen_core_t));

            if ( ! cpu )
            {
                /* Set up Xen Crash Info note. */
                xen_crash_note = note = ELFNOTE_NEXT(note);
                setup_note(note, "Xen", XEN_ELFNOTE_CRASH_INFO,
                           sizeof(crash_xen_info_t));
            }
        }
    }

    return ret;
}

static int cpu_callback(
    struct notifier_block *nfb, unsigned long action, void *hcpu)
{
    unsigned long cpu = (unsigned long)hcpu;

    /* Only hook on CPU_UP_PREPARE because once a crash_note has been reported
     * to dom0, it must keep it around in case of a crash, as the crash kernel
     * will be hard coded to the original physical address reported. */
    switch ( action )
    {
    case CPU_UP_PREPARE:
        /* Ignore return value.  If this boot time, -ENOMEM will cause all
         * manner of problems elsewhere very soon, and if it is during runtime,
         * then failing to allocate crash notes is not a good enough reason to
         * fail the CPU_UP_PREPARE */
        kexec_init_cpu_notes(cpu);
        break;
    default:
        break;
    }
    return NOTIFY_DONE;
}

static struct notifier_block cpu_nfb = {
    .notifier_call = cpu_callback
};

void __init kexec_early_calculations(void)
{
    /* If low_crashinfo_mode is still INVALID, neither "low_crashinfo" nor
     * "crashinfo_maxaddr" have been specified on the command line, so
     * explicitly set to NONE. */
    if ( low_crashinfo_mode == LOW_CRASHINFO_INVALID )
        low_crashinfo_mode = LOW_CRASHINFO_NONE;

    if ( low_crashinfo_mode > LOW_CRASHINFO_NONE )
        crashinfo_maxaddr_bits = fls(crashinfo_maxaddr) - 1;
}

static int __init kexec_init(void)
{
    void *cpu = (void *)(unsigned long)smp_processor_id();

    /* If no crash area, no need to allocate space for notes. */
    if ( !kexec_crash_area.size )
        return 0;

    if ( low_crashinfo_mode > LOW_CRASHINFO_NONE )
    {
        size_t crash_heap_size;

        /* This calculation is safe even if the machine is booted in
         * uniprocessor mode. */
        crash_heap_size = sizeof_cpu_notes(0) +
            sizeof_cpu_notes(1) * (nr_cpu_ids - 1);
        crash_heap_size = PAGE_ALIGN(crash_heap_size);

        crash_heap_current = alloc_xenheap_pages(
            get_order_from_bytes(crash_heap_size),
            MEMF_bits(crashinfo_maxaddr_bits) );

        if ( ! crash_heap_current )
            return -ENOMEM;

        memset(crash_heap_current, 0, crash_heap_size);

        crash_heap_end = crash_heap_current + crash_heap_size;
    }

    /* crash_notes may be allocated anywhere Xen can reach in memory.
       Only the individual CPU crash notes themselves must be allocated
       in lower memory if requested. */
    crash_notes = xzalloc_array(crash_note_range_t, nr_cpu_ids);
    if ( ! crash_notes )
        return -ENOMEM;

    register_keyhandler('C', &crashdump_trigger_keyhandler);

    cpu_callback(&cpu_nfb, CPU_UP_PREPARE, cpu);
    register_cpu_notifier(&cpu_nfb);
    return 0;
}
/* The reason for this to be a presmp_initcall as opposed to a regular
 * __initcall is to allow the setup of the cpu hotplug handler before APs are
 * brought up. */
presmp_initcall(kexec_init);

static int kexec_get_reserve(xen_kexec_range_t *range)
{
    if ( kexec_crash_area.size > 0 && kexec_crash_area.start > 0) {
        range->start = kexec_crash_area.start;
        range->size = kexec_crash_area.size;
    }
    else
        range->start = range->size = 0;
    return 0;
}

static int kexec_get_cpu(xen_kexec_range_t *range)
{
    int nr = range->nr;

    if ( nr < 0 || nr >= nr_cpu_ids )
        return -ERANGE;

    if ( ! crash_notes )
        return -EINVAL;

    /* Try once again to allocate room for the crash notes.  It is just possible
     * that more space has become available since we last tried.  If space has
     * already been allocated, kexec_init_cpu_notes() will return early with 0.
     */
    kexec_init_cpu_notes(nr);

    /* In the case of still not having enough memory to allocate buffer room,
     * returning a range of 0,0 is still valid. */
    if ( crash_notes[nr].start )
    {
        range->start = __pa(crash_notes[nr].start);
        range->size = crash_notes[nr].size;
    }
    else
        range->start = range->size = 0;

    return 0;
}

static int kexec_get_vmcoreinfo(xen_kexec_range_t *range)
{
    range->start = __pa((unsigned long)vmcoreinfo_data);
    range->size = VMCOREINFO_BYTES;
    return 0;
}

static int kexec_get_range_internal(xen_kexec_range_t *range)
{
    int ret = -EINVAL;

    switch ( range->range )
    {
    case KEXEC_RANGE_MA_CRASH:
        ret = kexec_get_reserve(range);
        break;
    case KEXEC_RANGE_MA_CPU:
        ret = kexec_get_cpu(range);
        break;
    case KEXEC_RANGE_MA_VMCOREINFO:
        ret = kexec_get_vmcoreinfo(range);
        break;
    default:
        ret = machine_kexec_get(range);
        break;
    }

    return ret;
}

static int kexec_get_range(XEN_GUEST_HANDLE(void) uarg)
{
    xen_kexec_range_t range;
    int ret = -EINVAL;

    if ( unlikely(copy_from_guest(&range, uarg, 1)) )
        return -EFAULT;

    ret = kexec_get_range_internal(&range);

    if ( ret == 0 && unlikely(copy_to_guest(uarg, &range, 1)) )
        return -EFAULT;

    return ret;
}

static int kexec_get_range_compat(XEN_GUEST_HANDLE(void) uarg)
{
#ifdef CONFIG_COMPAT
    xen_kexec_range_t range;
    compat_kexec_range_t compat_range;
    int ret = -EINVAL;

    if ( unlikely(copy_from_guest(&compat_range, uarg, 1)) )
        return -EFAULT;

    XLAT_kexec_range(&range, &compat_range);

    ret = kexec_get_range_internal(&range);

    /* Dont silently truncate physical addresses or sizes. */
    if ( (range.start | range.size) & ~(unsigned long)(~0u) )
        return -ERANGE;

    if ( ret == 0 ) {
        XLAT_kexec_range(&compat_range, &range);
        if ( unlikely(copy_to_guest(uarg, &compat_range, 1)) )
             return -EFAULT;
    }

    return ret;
#else /* CONFIG_COMPAT */
    return 0;
#endif /* CONFIG_COMPAT */
}

static int kexec_load_get_bits(int type, int *base, int *bit)
{
    switch ( type )
    {
    case KEXEC_TYPE_DEFAULT:
        *base = KEXEC_IMAGE_DEFAULT_BASE;
        *bit = KEXEC_FLAG_DEFAULT_POS;
        break;
    case KEXEC_TYPE_CRASH:
        *base = KEXEC_IMAGE_CRASH_BASE;
        *bit = KEXEC_FLAG_CRASH_POS;
        break;
    default:
        return -1;
    }
    return 0;
}

void vmcoreinfo_append_str(const char *fmt, ...)
{
    va_list args;
    char buf[0x50];
    int r;
    size_t note_size = sizeof(Elf_Note) + ELFNOTE_ALIGN(strlen(VMCOREINFO_NOTE_NAME) + 1);

    if (vmcoreinfo_size + note_size + sizeof(buf) > VMCOREINFO_BYTES)
        return;

    va_start(args, fmt);
    r = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    memcpy(&vmcoreinfo_data[note_size + vmcoreinfo_size], buf, r);

    vmcoreinfo_size += r;
}

static void crash_save_vmcoreinfo(void)
{
    size_t data_size;

    if (vmcoreinfo_size > 0)    /* already saved */
        return;

    data_size = VMCOREINFO_BYTES - (sizeof(Elf_Note) + ELFNOTE_ALIGN(strlen(VMCOREINFO_NOTE_NAME) + 1));
    setup_note((Elf_Note *)vmcoreinfo_data, VMCOREINFO_NOTE_NAME, 0, data_size);

    VMCOREINFO_PAGESIZE(PAGE_SIZE);

    VMCOREINFO_SYMBOL(domain_list);
#ifndef frame_table
    VMCOREINFO_SYMBOL(frame_table);
#else
    {
        static const void *const _frame_table = frame_table;
        VMCOREINFO_SYMBOL_ALIAS(frame_table, _frame_table);
    }
#endif
    VMCOREINFO_SYMBOL(max_page);

    VMCOREINFO_STRUCT_SIZE(page_info);
    VMCOREINFO_STRUCT_SIZE(domain);

    VMCOREINFO_OFFSET(page_info, count_info);
    VMCOREINFO_OFFSET_SUB(page_info, v.inuse, _domain);
    VMCOREINFO_OFFSET(domain, domain_id);
    VMCOREINFO_OFFSET(domain, next_in_list);

#ifdef ARCH_CRASH_SAVE_VMCOREINFO
    arch_crash_save_vmcoreinfo();
#endif
}

static int kexec_load_unload_internal(unsigned long op, xen_kexec_load_t *load)
{
    xen_kexec_image_t *image;
    int base, bit, pos;
    int ret = 0;

    if ( kexec_load_get_bits(load->type, &base, &bit) )
        return -EINVAL;

    pos = (test_bit(bit, &kexec_flags) != 0);

    /* Load the user data into an unused image */
    if ( op == KEXEC_CMD_kexec_load )
    {
        image = &kexec_image[base + !pos];

        BUG_ON(test_bit((base + !pos), &kexec_flags)); /* must be free */

        memcpy(image, &load->image, sizeof(*image));

        if ( !(ret = machine_kexec_load(load->type, base + !pos, image)) )
        {
            /* Set image present bit */
            set_bit((base + !pos), &kexec_flags);

            /* Make new image the active one */
            change_bit(bit, &kexec_flags);
        }

        crash_save_vmcoreinfo();
    }

    /* Unload the old image if present and load successful */
    if ( ret == 0 && !test_bit(KEXEC_FLAG_IN_PROGRESS, &kexec_flags) )
    {
        if ( test_and_clear_bit((base + pos), &kexec_flags) )
        {
            image = &kexec_image[base + pos];
            machine_kexec_unload(load->type, base + pos, image);
        }
    }

    return ret;
}

static int kexec_load_unload(unsigned long op, XEN_GUEST_HANDLE(void) uarg)
{
    xen_kexec_load_t load;

    if ( unlikely(copy_from_guest(&load, uarg, 1)) )
        return -EFAULT;

    return kexec_load_unload_internal(op, &load);
}

static int kexec_load_unload_compat(unsigned long op,
                                    XEN_GUEST_HANDLE(void) uarg)
{
#ifdef CONFIG_COMPAT
    compat_kexec_load_t compat_load;
    xen_kexec_load_t load;

    if ( unlikely(copy_from_guest(&compat_load, uarg, 1)) )
        return -EFAULT;

    /* This is a bit dodgy, load.image is inside load,
     * but XLAT_kexec_load (which is automatically generated)
     * doesn't translate load.image (correctly)
     * Just copy load->type, the only other member, manually instead.
     *
     * XLAT_kexec_load(&load, &compat_load);
     */
    load.type = compat_load.type;
    XLAT_kexec_image(&load.image, &compat_load.image);

    return kexec_load_unload_internal(op, &load);
#else /* CONFIG_COMPAT */
    return 0;
#endif /* CONFIG_COMPAT */
}

static int kexec_exec(XEN_GUEST_HANDLE(void) uarg)
{
    xen_kexec_exec_t exec;
    xen_kexec_image_t *image;
    int base, bit, pos, ret = -EINVAL;

    if ( unlikely(copy_from_guest(&exec, uarg, 1)) )
        return -EFAULT;

    if ( kexec_load_get_bits(exec.type, &base, &bit) )
        return -EINVAL;

    pos = (test_bit(bit, &kexec_flags) != 0);

    /* Only allow kexec/kdump into loaded images */
    if ( !test_bit(base + pos, &kexec_flags) )
        return -ENOENT;

    switch (exec.type)
    {
    case KEXEC_TYPE_DEFAULT:
        image = &kexec_image[base + pos];
        ret = continue_hypercall_on_cpu(0, kexec_reboot, image);
        break;
    case KEXEC_TYPE_CRASH:
        kexec_crash(); /* Does not return */
        break;
    }

    return -EINVAL; /* never reached */
}

static int do_kexec_op_internal(unsigned long op, XEN_GUEST_HANDLE(void) uarg,
                                bool_t compat)
{
    unsigned long flags;
    int ret = -EINVAL;

    if ( !IS_PRIV(current->domain) )
        return -EPERM;

    ret = xsm_kexec();
    if ( ret )
        return ret;

    switch ( op )
    {
    case KEXEC_CMD_kexec_get_range:
        if (compat)
                ret = kexec_get_range_compat(uarg);
        else
                ret = kexec_get_range(uarg);
        break;
    case KEXEC_CMD_kexec_load:
    case KEXEC_CMD_kexec_unload:
        spin_lock_irqsave(&kexec_lock, flags);
        if (!test_bit(KEXEC_FLAG_IN_PROGRESS, &kexec_flags))
        {
                if (compat)
                        ret = kexec_load_unload_compat(op, uarg);
                else
                        ret = kexec_load_unload(op, uarg);
        }
        spin_unlock_irqrestore(&kexec_lock, flags);
        break;
    case KEXEC_CMD_kexec:
        ret = kexec_exec(uarg);
        break;
    }

    return ret;
}

long do_kexec_op(unsigned long op, XEN_GUEST_HANDLE(void) uarg)
{
    return do_kexec_op_internal(op, uarg, 0);
}

#ifdef CONFIG_COMPAT
int compat_kexec_op(unsigned long op, XEN_GUEST_HANDLE(void) uarg)
{
    return do_kexec_op_internal(op, uarg, 1);
}
#endif

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
