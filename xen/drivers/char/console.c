/******************************************************************************
 * console.c
 * 
 * Emergency console I/O for Xen and the domain-0 guest OS.
 * 
 * Copyright (c) 2002-2004, K A Fraser.
 *
 * Added printf_ratelimit
 *     Taken from Linux - Author: Andi Kleen (net_ratelimit)
 *     Ported to Xen - Steven Rostedt - Red Hat
 */

#include <xen/version.h>
#include <xen/lib.h>
#include <xen/init.h>
#include <xen/event.h>
#include <xen/console.h>
#include <xen/serial.h>
#include <xen/softirq.h>
#include <xen/keyhandler.h>
#include <xen/delay.h>
#include <xen/guest_access.h>
#include <xen/shutdown.h>
#include <xen/vga.h>
#include <xen/kexec.h>
#include <asm/debugger.h>
#include <asm/div64.h>
#include <xen/hypercall.h> /* for do_console_io */

/* console: comma-separated list of console outputs. */
static char __initdata opt_console[30] = OPT_CONSOLE_STR;
string_param("console", opt_console);

/* conswitch: a character pair controlling console switching. */
/* Char 1: CTRL+<char1> is used to switch console input between Xen and DOM0 */
/* Char 2: If this character is 'x', then do not auto-switch to DOM0 when it */
/*         boots. Any other value, or omitting the char, enables auto-switch */
static unsigned char __read_mostly opt_conswitch[3] = "a";
string_param("conswitch", opt_conswitch);

/* sync_console: force synchronous console output (useful for debugging). */
static bool_t __initdata opt_sync_console;
boolean_param("sync_console", opt_sync_console);

/* console_to_ring: send guest (incl. dom 0) console data to console ring. */
static bool_t __read_mostly opt_console_to_ring;
boolean_param("console_to_ring", opt_console_to_ring);

/* console_timestamps: include a timestamp prefix on every Xen console line. */
static bool_t __read_mostly opt_console_timestamps;
boolean_param("console_timestamps", opt_console_timestamps);

/* conring_size: allows a large console ring than default (16kB). */
static uint32_t __initdata opt_conring_size;
size_param("conring_size", opt_conring_size);

#define _CONRING_SIZE 16384
#define CONRING_IDX_MASK(i) ((i)&(conring_size-1))
static char __initdata _conring[_CONRING_SIZE];
static char *__read_mostly conring = _conring;
static uint32_t __read_mostly conring_size = _CONRING_SIZE;
static uint32_t conringc, conringp;

static int __read_mostly sercon_handle = -1;

static DEFINE_SPINLOCK(console_lock);

/*
 * To control the amount of printing, thresholds are added.
 * These thresholds correspond to the XENLOG logging levels.
 * There's an upper and lower threshold for non-guest messages and for
 * guest-provoked messages.  This works as follows, for a given log level L:
 *
 * L < lower_threshold                     : always logged
 * lower_threshold <= L < upper_threshold  : rate-limited logging
 * upper_threshold <= L                    : never logged
 *
 * Note, in the above algorithm, to disable rate limiting simply make
 * the lower threshold equal to the upper.
 */
#ifdef NDEBUG
#define XENLOG_UPPER_THRESHOLD       2 /* Do not print INFO and DEBUG  */
#define XENLOG_LOWER_THRESHOLD       2 /* Always print ERR and WARNING */
#define XENLOG_GUEST_UPPER_THRESHOLD 2 /* Do not print INFO and DEBUG  */
#define XENLOG_GUEST_LOWER_THRESHOLD 0 /* Rate-limit ERR and WARNING   */
#else
#define XENLOG_UPPER_THRESHOLD       4 /* Do not discard anything      */
#define XENLOG_LOWER_THRESHOLD       4 /* Print everything             */
#define XENLOG_GUEST_UPPER_THRESHOLD 4 /* Do not discard anything      */
#define XENLOG_GUEST_LOWER_THRESHOLD 4 /* Print everything             */
#endif
/*
 * The XENLOG_DEFAULT is the default given to printks that
 * do not have any print level associated with them.
 */
#define XENLOG_DEFAULT       1 /* XENLOG_WARNING */
#define XENLOG_GUEST_DEFAULT 1 /* XENLOG_WARNING */

static int __read_mostly xenlog_upper_thresh = XENLOG_UPPER_THRESHOLD;
static int __read_mostly xenlog_lower_thresh = XENLOG_LOWER_THRESHOLD;
static int __read_mostly xenlog_guest_upper_thresh = XENLOG_GUEST_UPPER_THRESHOLD;
static int __read_mostly xenlog_guest_lower_thresh = XENLOG_GUEST_LOWER_THRESHOLD;

static void parse_loglvl(char *s);
static void parse_guest_loglvl(char *s);

/*
 * <lvl> := none|error|warning|info|debug|all
 * loglvl=<lvl_print_always>[/<lvl_print_ratelimit>]
 *  <lvl_print_always>: log level which is always printed
 *  <lvl_print_rlimit>: log level which is rate-limit printed
 * Similar definitions for guest_loglvl, but applies to guest tracing.
 * Defaults: loglvl=warning ; guest_loglvl=none/warning
 */
custom_param("loglvl", parse_loglvl);
custom_param("guest_loglvl", parse_guest_loglvl);

static atomic_t print_everything = ATOMIC_INIT(0);

#define ___parse_loglvl(s, ps, lvlstr, lvlnum)          \
    if ( !strncmp((s), (lvlstr), strlen(lvlstr)) ) {    \
        *(ps) = (s) + strlen(lvlstr);                   \
        return (lvlnum);                                \
    }

static int __init __parse_loglvl(char *s, char **ps)
{
    ___parse_loglvl(s, ps, "none",    0);
    ___parse_loglvl(s, ps, "error",   1);
    ___parse_loglvl(s, ps, "warning", 2);
    ___parse_loglvl(s, ps, "info",    3);
    ___parse_loglvl(s, ps, "debug",   4);
    ___parse_loglvl(s, ps, "all",     4);
    return 2; /* sane fallback */
}

static void __init _parse_loglvl(char *s, int *lower, int *upper)
{
    *lower = *upper = __parse_loglvl(s, &s);
    if ( *s == '/' )
        *upper = __parse_loglvl(s+1, &s);
    if ( *upper < *lower )
        *upper = *lower;
}

static void __init parse_loglvl(char *s)
{
    _parse_loglvl(s, &xenlog_lower_thresh, &xenlog_upper_thresh);
}

static void __init parse_guest_loglvl(char *s)
{
    _parse_loglvl(s, &xenlog_guest_lower_thresh, &xenlog_guest_upper_thresh);
}

static char * __init loglvl_str(int lvl)
{
    switch ( lvl )
    {
    case 0: return "Nothing";
    case 1: return "Errors";
    case 2: return "Errors and warnings";
    case 3: return "Errors, warnings and info";
    case 4: return "All";
    }
    return "???";
}

/*
 * ********************************************************
 * *************** ACCESS TO CONSOLE RING *****************
 * ********************************************************
 */

static void putchar_console_ring(int c)
{
    ASSERT(spin_is_locked(&console_lock));
    conring[CONRING_IDX_MASK(conringp++)] = c;
    if ( (uint32_t)(conringp - conringc) > conring_size )
        conringc = conringp - conring_size;
}

long read_console_ring(struct xen_sysctl_readconsole *op)
{
    XEN_GUEST_HANDLE(char) str;
    uint32_t idx, len, max, sofar, c;

    str   = guest_handle_cast(op->buffer, char),
    max   = op->count;
    sofar = 0;

    c = conringc;
    if ( op->incremental && ((int32_t)(op->index - c) > 0) )
        c = op->index;

    while ( (c != conringp) && (sofar < max) )
    {
        idx = CONRING_IDX_MASK(c);
        len = conringp - c;
        if ( (idx + len) > conring_size )
            len = conring_size - idx;
        if ( (sofar + len) > max )
            len = max - sofar;
        if ( copy_to_guest_offset(str, sofar, &conring[idx], len) )
            return -EFAULT;
        sofar += len;
        c += len;
    }

    if ( op->clear )
    {
        spin_lock_irq(&console_lock);
        if ( (uint32_t)(conringp - c) > conring_size )
            conringc = conringp - conring_size;
        else
            conringc = c;
        spin_unlock_irq(&console_lock);
    }

    op->count = sofar;
    op->index = c;

    return 0;
}


/*
 * *******************************************************
 * *************** ACCESS TO SERIAL LINE *****************
 * *******************************************************
 */

/* Characters received over the serial line are buffered for domain 0. */
#define SERIAL_RX_SIZE 128
#define SERIAL_RX_MASK(_i) ((_i)&(SERIAL_RX_SIZE-1))
static char serial_rx_ring[SERIAL_RX_SIZE];
static unsigned int serial_rx_cons, serial_rx_prod;

static void (*serial_steal_fn)(const char *);

int console_steal(int handle, void (*fn)(const char *))
{
    if ( (handle == -1) || (handle != sercon_handle) )
        return 0;

    if ( serial_steal_fn != NULL )
        return -EBUSY;

    serial_steal_fn = fn;
    return 1;
}

void console_giveback(int id)
{
    if ( id == 1 )
        serial_steal_fn = NULL;
}

static void sercon_puts(const char *s)
{
    if ( serial_steal_fn != NULL )
        (*serial_steal_fn)(s);
    else
        serial_puts(sercon_handle, s);
}

/* CTRL-<switch_char> switches input direction between Xen and DOM0. */
#define switch_code (opt_conswitch[0]-'a'+1)
static int __read_mostly xen_rx = 1; /* FALSE => serial input passed to domain 0. */

static void switch_serial_input(void)
{
    static char *input_str[2] = { "DOM0", "Xen" };
    xen_rx = !xen_rx;
    printk("*** Serial input -> %s", input_str[xen_rx]);
    if ( switch_code )
        printk(" (type 'CTRL-%c' three times to switch input to %s)",
               opt_conswitch[0], input_str[!xen_rx]);
    printk("\n");
}

static void __serial_rx(char c, struct cpu_user_regs *regs)
{
    if ( xen_rx )
        return handle_keypress(c, regs);

    /* Deliver input to guest buffer, unless it is already full. */
    if ( (serial_rx_prod-serial_rx_cons) != SERIAL_RX_SIZE )
        serial_rx_ring[SERIAL_RX_MASK(serial_rx_prod++)] = c;
    /* Always notify the guest: prevents receive path from getting stuck. */
    send_global_virq(VIRQ_CONSOLE);
}

static void serial_rx(char c, struct cpu_user_regs *regs)
{
    static int switch_code_count = 0;

    if ( switch_code && (c == switch_code) )
    {
        /* We eat CTRL-<switch_char> in groups of 3 to switch console input. */
        if ( ++switch_code_count == 3 )
        {
            switch_serial_input();
            switch_code_count = 0;
        }
        return;
    }

    for ( ; switch_code_count != 0; switch_code_count-- )
        __serial_rx(switch_code, regs);

    /* Finally process the just-received character. */
    __serial_rx(c, regs);
}

static void notify_dom0_con_ring(unsigned long unused)
{
    send_global_virq(VIRQ_CON_RING);
}
static DECLARE_SOFTIRQ_TASKLET(notify_dom0_con_ring_tasklet,
                               notify_dom0_con_ring, 0);

static long guest_console_write(XEN_GUEST_HANDLE(char) buffer, int count)
{
    char kbuf[128], *kptr;
    int kcount;

    while ( count > 0 )
    {
        if ( hypercall_preempt_check() )
            return hypercall_create_continuation(
                __HYPERVISOR_console_io, "iih",
                CONSOLEIO_write, count, buffer);

        kcount = min_t(int, count, sizeof(kbuf)-1);
        if ( copy_from_guest(kbuf, buffer, kcount) )
            return -EFAULT;
        kbuf[kcount] = '\0';

        spin_lock_irq(&console_lock);

        sercon_puts(kbuf);
        vga_puts(kbuf);

        if ( opt_console_to_ring )
        {
            for ( kptr = kbuf; *kptr != '\0'; kptr++ )
                putchar_console_ring(*kptr);
            tasklet_schedule(&notify_dom0_con_ring_tasklet);
        }

        spin_unlock_irq(&console_lock);

        guest_handle_add_offset(buffer, kcount);
        count -= kcount;
    }

    return 0;
}

long do_console_io(int cmd, int count, XEN_GUEST_HANDLE(char) buffer)
{
    long rc;
    unsigned int idx, len;

#ifndef VERBOSE
    /* Only domain 0 may access the emergency console. */
    if ( current->domain->domain_id != 0 )
        return -EPERM;
#endif

    rc = xsm_console_io(current->domain, cmd);
    if ( rc )
        return rc;

    switch ( cmd )
    {
    case CONSOLEIO_write:
        rc = guest_console_write(buffer, count);
        break;
    case CONSOLEIO_read:
        rc = 0;
        while ( (serial_rx_cons != serial_rx_prod) && (rc < count) )
        {
            idx = SERIAL_RX_MASK(serial_rx_cons);
            len = serial_rx_prod - serial_rx_cons;
            if ( (idx + len) > SERIAL_RX_SIZE )
                len = SERIAL_RX_SIZE - idx;
            if ( (rc + len) > count )
                len = count - rc;
            if ( copy_to_guest_offset(buffer, rc, &serial_rx_ring[idx], len) )
            {
                rc = -EFAULT;
                break;
            }
            rc += len;
            serial_rx_cons += len;
        }
        break;
    default:
        rc = -ENOSYS;
        break;
    }

    return rc;
}


/*
 * *****************************************************
 * *************** GENERIC CONSOLE I/O *****************
 * *****************************************************
 */

static bool_t console_locks_busted;

static void __putstr(const char *str)
{
    int c;

    ASSERT(spin_is_locked(&console_lock));

    sercon_puts(str);
    vga_puts(str);

    if ( !console_locks_busted )
    {
        while ( (c = *str++) != '\0' )
            putchar_console_ring(c);
        tasklet_schedule(&notify_dom0_con_ring_tasklet);
    }
}

static int printk_prefix_check(char *p, char **pp)
{
    int loglvl = -1;
    int upper_thresh = xenlog_upper_thresh;
    int lower_thresh = xenlog_lower_thresh;

    while ( (p[0] == '<') && (p[1] != '\0') && (p[2] == '>') )
    {
        switch ( p[1] )
        {
        case 'G':
            upper_thresh = xenlog_guest_upper_thresh;
            lower_thresh = xenlog_guest_lower_thresh;
            if ( loglvl == -1 )
                loglvl = XENLOG_GUEST_DEFAULT;
            break;
        case '0' ... '3':
            loglvl = p[1] - '0';
            break;
        }
        p += 3;
    }

    if ( loglvl == -1 )
        loglvl = XENLOG_DEFAULT;

    *pp = p;

    return ((atomic_read(&print_everything) != 0) ||
            (loglvl < lower_thresh) ||
            ((loglvl < upper_thresh) && printk_ratelimit()));
} 

static void printk_start_of_line(void)
{
    struct tm tm;
    char tstr[32];

    __putstr("(XEN) ");

    if ( !opt_console_timestamps )
        return;

    tm = wallclock_time();
    if ( tm.tm_mday == 0 )
        return;

    snprintf(tstr, sizeof(tstr), "[%04u-%02u-%02u %02u:%02u:%02u] ",
             1900 + tm.tm_year, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    __putstr(tstr);
}

void printk(const char *fmt, ...)
{
    static char   buf[1024];
    static int    start_of_line = 1, do_print;

    va_list       args;
    char         *p, *q;
    unsigned long flags;

    /* console_lock can be acquired recursively from __printk_ratelimit(). */
    local_irq_save(flags);
    spin_lock_recursive(&console_lock);

    va_start(args, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);        

    p = buf;

    while ( (q = strchr(p, '\n')) != NULL )
    {
        *q = '\0';
        if ( start_of_line )
            do_print = printk_prefix_check(p, &p);
        if ( do_print )
        {
            if ( start_of_line )
                printk_start_of_line();
            __putstr(p);
            __putstr("\n");
        }
        start_of_line = 1;
        p = q + 1;
    }

    if ( *p != '\0' )
    {
        if ( start_of_line )
            do_print = printk_prefix_check(p, &p);
        if ( do_print )
        {
            if ( start_of_line )
                printk_start_of_line();
            __putstr(p);
        }
        start_of_line = 0;
    }

    spin_unlock_recursive(&console_lock);
    local_irq_restore(flags);
}

void __init console_init_preirq(void)
{
    char *p;

    serial_init_preirq();

    /* Where should console output go? */
    for ( p = opt_console; p != NULL; p = strchr(p, ',') )
    {
        if ( *p == ',' )
            p++;
        if ( !strncmp(p, "vga", 3) )
            vga_init();
        else if ( !strncmp(p, "none", 4) )
            continue;
        else if ( strncmp(p, "com", 3) ||
                  (sercon_handle = serial_parse_handle(p)) == -1 )
        {
            char *q = strchr(p, ',');
            if ( q != NULL )
                *q = '\0';
            printk("Bad console= option '%s'\n", p);
            if ( q != NULL )
                *q = ',';
        }
    }

    serial_set_rx_handler(sercon_handle, serial_rx);

    /* HELLO WORLD --- start-of-day banner text. */
    spin_lock(&console_lock);
    __putstr(xen_banner());
    spin_unlock(&console_lock);
    printk("Xen version %d.%d%s (%s@%s) (%s) %s\n",
           xen_major_version(), xen_minor_version(), xen_extra_version(),
           xen_compile_by(), xen_compile_domain(),
           xen_compiler(), xen_compile_date());
    printk("Latest ChangeSet: %s\n", xen_changeset());

    if ( opt_sync_console )
    {
        serial_start_sync(sercon_handle);
        add_taint(TAINT_SYNC_CONSOLE);
        printk("Console output is synchronous.\n");
    }
}

void __init console_init_postirq(void)
{
    char *ring;
    unsigned int i, order;

    serial_init_postirq();

    if ( !opt_conring_size )
        opt_conring_size = num_present_cpus() << (9 + xenlog_lower_thresh);

    order = get_order_from_bytes(max(opt_conring_size, conring_size));
    while ( (ring = alloc_xenheap_pages(order, MEMF_bits(crashinfo_maxaddr_bits))) == NULL )
    {
        BUG_ON(order == 0);
        order--;
    }
    opt_conring_size = PAGE_SIZE << order;

    spin_lock_irq(&console_lock);
    for ( i = conringc ; i != conringp; i++ )
        ring[i & (opt_conring_size - 1)] = conring[i & (conring_size - 1)];
    conring = ring;
    wmb(); /* Allow users of console_force_unlock() to see larger buffer. */
    conring_size = opt_conring_size;
    spin_unlock_irq(&console_lock);

    printk("Allocated console ring of %u KiB.\n", opt_conring_size >> 10);
}

void __init console_endboot(void)
{
    int i, j;

    printk("Std. Loglevel: %s", loglvl_str(xenlog_lower_thresh));
    if ( xenlog_upper_thresh != xenlog_lower_thresh )
        printk(" (Rate-limited: %s)", loglvl_str(xenlog_upper_thresh));
    printk("\nGuest Loglevel: %s", loglvl_str(xenlog_guest_lower_thresh));
    if ( xenlog_guest_upper_thresh != xenlog_guest_lower_thresh )
        printk(" (Rate-limited: %s)", loglvl_str(xenlog_guest_upper_thresh));
    printk("\n");

    if ( opt_sync_console )
    {
        printk("**********************************************\n");
        printk("******* WARNING: CONSOLE OUTPUT IS SYNCHRONOUS\n");
        printk("******* This option is intended to aid debugging "
               "of Xen by ensuring\n");
        printk("******* that all output is synchronously delivered "
               "on the serial line.\n");
        printk("******* However it can introduce SIGNIFICANT latencies "
               "and affect\n");
        printk("******* timekeeping. It is NOT recommended for "
               "production use!\n");
        printk("**********************************************\n");
        for ( i = 0; i < 3; i++ )
        {
            printk("%d... ", 3-i);
            for ( j = 0; j < 100; j++ )
            {
                process_pending_softirqs();
                mdelay(10);
            }
        }
        printk("\n");
    }

    vga_endboot();

    /*
     * If user specifies so, we fool the switch routine to redirect input
     * straight back to Xen. I use this convoluted method so we still print
     * a useful 'how to switch' message.
     */
    if ( opt_conswitch[1] == 'x' )
        xen_rx = !xen_rx;

    /* Serial input is directed to DOM0 by default. */
    switch_serial_input();
}

int __init console_has(const char *device)
{
    char *p;

    for ( p = opt_console; p != NULL; p = strchr(p, ',') )
    {
        if ( *p == ',' )
            p++;
        if ( strncmp(p, device, strlen(device)) == 0 )
            return 1;
    }

    return 0;
}

void console_start_log_everything(void)
{
    serial_start_log_everything(sercon_handle);
    atomic_inc(&print_everything);
}

void console_end_log_everything(void)
{
    serial_end_log_everything(sercon_handle);
    atomic_dec(&print_everything);
}

void console_force_unlock(void)
{
    spin_lock_init(&console_lock);
    serial_force_unlock(sercon_handle);
    console_locks_busted = 1;
    console_start_sync();
}

void console_start_sync(void)
{
    atomic_inc(&print_everything);
    serial_start_sync(sercon_handle);
}

void console_end_sync(void)
{
    serial_end_sync(sercon_handle);
    atomic_dec(&print_everything);
}

/*
 * printk rate limiting, lifted from Linux.
 *
 * This enforces a rate limit: not more than one kernel message
 * every printk_ratelimit_ms (millisecs).
 */
int __printk_ratelimit(int ratelimit_ms, int ratelimit_burst)
{
    static DEFINE_SPINLOCK(ratelimit_lock);
    static unsigned long toks = 10 * 5 * 1000;
    static unsigned long last_msg;
    static int missed;
    unsigned long flags;
    unsigned long long now = NOW(); /* ns */
    unsigned long ms;

    do_div(now, 1000000);
    ms = (unsigned long)now;

    spin_lock_irqsave(&ratelimit_lock, flags);
    toks += ms - last_msg;
    last_msg = ms;
    if ( toks > (ratelimit_burst * ratelimit_ms))
        toks = ratelimit_burst * ratelimit_ms;
    if ( toks >= ratelimit_ms )
    {
        int lost = missed;
        missed = 0;
        toks -= ratelimit_ms;
        spin_unlock(&ratelimit_lock);
        if ( lost )
        {
            char lost_str[8];
            snprintf(lost_str, sizeof(lost_str), "%d", lost);
            /* console_lock may already be acquired by printk(). */
            spin_lock_recursive(&console_lock);
            printk_start_of_line();
            __putstr("printk: ");
            __putstr(lost_str);
            __putstr(" messages suppressed.\n");
            spin_unlock_recursive(&console_lock);
        }
        local_irq_restore(flags);
        return 1;
    }
    missed++;
    spin_unlock_irqrestore(&ratelimit_lock, flags);
    return 0;
}

/* minimum time in ms between messages */
static int __read_mostly printk_ratelimit_ms = 5 * 1000;

/* number of messages we send before ratelimiting */
static int __read_mostly printk_ratelimit_burst = 10;

int printk_ratelimit(void)
{
    return __printk_ratelimit(printk_ratelimit_ms, printk_ratelimit_burst);
}

/*
 * **************************************************************
 * *************** Serial console ring buffer *******************
 * **************************************************************
 */

#ifdef DEBUG_TRACE_DUMP

/* Send output direct to console, or buffer it? */
static volatile int debugtrace_send_to_console;

static char        *debugtrace_buf; /* Debug-trace buffer */
static unsigned int debugtrace_prd; /* Producer index     */
static unsigned int debugtrace_kilobytes = 128, debugtrace_bytes;
static unsigned int debugtrace_used;
static DEFINE_SPINLOCK(debugtrace_lock);
integer_param("debugtrace", debugtrace_kilobytes);

static void debugtrace_dump_worker(void)
{
    if ( (debugtrace_bytes == 0) || !debugtrace_used )
        return;

    printk("debugtrace_dump() starting\n");

    /* Print oldest portion of the ring. */
    ASSERT(debugtrace_buf[debugtrace_bytes - 1] == 0);
    sercon_puts(&debugtrace_buf[debugtrace_prd]);

    /* Print youngest portion of the ring. */
    debugtrace_buf[debugtrace_prd] = '\0';
    sercon_puts(&debugtrace_buf[0]);

    memset(debugtrace_buf, '\0', debugtrace_bytes);

    printk("debugtrace_dump() finished\n");
}

static void debugtrace_toggle(void)
{
    unsigned long flags;

    watchdog_disable();
    spin_lock_irqsave(&debugtrace_lock, flags);

    /*
     * Dump the buffer *before* toggling, in case the act of dumping the
     * buffer itself causes more printk() invocations.
     */
    printk("debugtrace_printk now writing to %s.\n",
           !debugtrace_send_to_console ? "console": "buffer");
    if ( !debugtrace_send_to_console )
        debugtrace_dump_worker();

    debugtrace_send_to_console = !debugtrace_send_to_console;

    spin_unlock_irqrestore(&debugtrace_lock, flags);
    watchdog_enable();

}

void debugtrace_dump(void)
{
    unsigned long flags;

    watchdog_disable();
    spin_lock_irqsave(&debugtrace_lock, flags);

    debugtrace_dump_worker();

    spin_unlock_irqrestore(&debugtrace_lock, flags);
    watchdog_enable();
}

void debugtrace_printk(const char *fmt, ...)
{
    static char    buf[1024];
    static u32 count;

    va_list       args;
    char         *p;
    unsigned long flags;

    if ( debugtrace_bytes == 0 )
        return;

    debugtrace_used = 1;

    spin_lock_irqsave(&debugtrace_lock, flags);

    ASSERT(debugtrace_buf[debugtrace_bytes - 1] == 0);

    snprintf(buf, sizeof(buf), "%u ", ++count);

    va_start(args, fmt);
    (void)vsnprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), fmt, args);
    va_end(args);

    if ( debugtrace_send_to_console )
    {
        serial_puts(sercon_handle, buf);
    }
    else
    {
        for ( p = buf; *p != '\0'; p++ )
        {
            debugtrace_buf[debugtrace_prd++] = *p;            
            /* Always leave a nul byte at the end of the buffer. */
            if ( debugtrace_prd == (debugtrace_bytes - 1) )
                debugtrace_prd = 0;
        }
    }

    spin_unlock_irqrestore(&debugtrace_lock, flags);
}

static void debugtrace_key(unsigned char key)
{
    debugtrace_toggle();
}

static struct keyhandler debugtrace_keyhandler = {
    .u.fn = debugtrace_key,
    .desc = "toggle debugtrace to console/buffer"
};

static int __init debugtrace_init(void)
{
    int order;
    unsigned int kbytes, bytes;

    /* Round size down to next power of two. */
    while ( (kbytes = (debugtrace_kilobytes & (debugtrace_kilobytes-1))) != 0 )
        debugtrace_kilobytes = kbytes;

    bytes = debugtrace_kilobytes << 10;
    if ( bytes == 0 )
        return 0;

    order = get_order_from_bytes(bytes);
    debugtrace_buf = alloc_xenheap_pages(order, 0);
    ASSERT(debugtrace_buf != NULL);

    memset(debugtrace_buf, '\0', bytes);

    debugtrace_bytes = bytes;

    register_keyhandler('T', &debugtrace_keyhandler);

    return 0;
}
__initcall(debugtrace_init);

#endif /* !NDEBUG */


/*
 * **************************************************************
 * *************** Debugging/tracing/error-report ***************
 * **************************************************************
 */

void panic(const char *fmt, ...)
{
    va_list args;
    unsigned long flags;
    static DEFINE_SPINLOCK(lock);
    static char buf[128];
    
    debugtrace_dump();

    /* Protects buf[] and ensure multi-line message prints atomically. */
    spin_lock_irqsave(&lock, flags);

    va_start(args, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    console_start_sync();
    printk("\n****************************************\n");
    printk("Panic on CPU %d:\n", smp_processor_id());
    printk("%s", buf);
    printk("****************************************\n\n");
    if ( opt_noreboot )
        printk("Manual reset required ('noreboot' specified)\n");
    else
        printk("Reboot in five seconds...\n");

    spin_unlock_irqrestore(&lock, flags);

    debugger_trap_immediate();

#ifdef CONFIG_KEXEC
    kexec_crash();
#endif

    if ( opt_noreboot )
    {
        machine_halt();
    }
    else
    {
        watchdog_disable();
        machine_restart(5000);
    }
}

void __bug(char *file, int line)
{
    console_start_sync();
    printk("Xen BUG at %s:%d\n", file, line);
    dump_execution_state();
    panic("Xen BUG at %s:%d\n", file, line);
    for ( ; ; ) ;
}

void __warn(char *file, int line)
{
    printk("Xen WARN at %s:%d\n", file, line);
    dump_execution_state();
}


/*
 * **************************************************************
 * ****************** Console suspend/resume ********************
 * **************************************************************
 */

static void suspend_steal_fn(const char *str) { }
static int suspend_steal_id;

int console_suspend(void)
{
    suspend_steal_id = console_steal(sercon_handle, suspend_steal_fn);
    serial_suspend();
    return 0;
}

int console_resume(void)
{
    serial_resume();
    console_giveback(suspend_steal_id);
    return 0;
}

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

