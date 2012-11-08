
#ifndef __SCHED_H__
#define __SCHED_H__

#include <xen/types.h>
#include <xen/spinlock.h>
#include <xen/shared.h>
#include <xen/timer.h>
#include <xen/rangeset.h>
#include <xen/domain.h>
#include <xen/rcupdate.h>
#include <xen/cpumask.h>
#include <xen/nodemask.h>
#include <xen/radix-tree.h>
#include <xen/multicall.h>
#include <xen/tasklet.h>
#include <xen/mm.h>
#include <xen/smp.h>
#include <asm/atomic.h>
#include <xen/wait.h>
#include <public/xen.h>
#include <public/domctl.h>
#include <public/sysctl.h>
#include <public/vcpu.h>
#include <public/mem_event.h>

#ifdef CONFIG_COMPAT
#include <compat/vcpu.h>
DEFINE_XEN_GUEST_HANDLE(vcpu_runstate_info_compat_t);
#endif

/* A global pointer to the initial domain (DOM0). */
extern struct domain *dom0;

#ifndef CONFIG_COMPAT
#define BITS_PER_EVTCHN_WORD(d) BITS_PER_LONG
#else
#define BITS_PER_EVTCHN_WORD(d) (has_32bit_shinfo(d) ? 32 : BITS_PER_LONG)
#endif
#define MAX_EVTCHNS(d) (BITS_PER_EVTCHN_WORD(d) * BITS_PER_EVTCHN_WORD(d))
#define EVTCHNS_PER_BUCKET 128
#define NR_EVTCHN_BUCKETS  (NR_EVENT_CHANNELS / EVTCHNS_PER_BUCKET)

struct evtchn
{
#define ECS_FREE         0 /* Channel is available for use.                  */
#define ECS_RESERVED     1 /* Channel is reserved.                           */
#define ECS_UNBOUND      2 /* Channel is waiting to bind to a remote domain. */
#define ECS_INTERDOMAIN  3 /* Channel is bound to another domain.            */
#define ECS_PIRQ         4 /* Channel is bound to a physical IRQ line.       */
#define ECS_VIRQ         5 /* Channel is bound to a virtual IRQ line.        */
#define ECS_IPI          6 /* Channel is bound to a virtual IPI line.        */
    u8  state;             /* ECS_* */
    u8  xen_consumer;      /* Consumer in Xen, if any? (0 = send to guest) */
    u16 notify_vcpu_id;    /* VCPU for local delivery notification */
    union {
        struct {
            domid_t remote_domid;
        } unbound;     /* state == ECS_UNBOUND */
        struct {
            u16            remote_port;
            struct domain *remote_dom;
        } interdomain; /* state == ECS_INTERDOMAIN */
        struct {
            u16            irq;
            u16            next_port;
            u16            prev_port;
        } pirq;        /* state == ECS_PIRQ */
        u16 virq;      /* state == ECS_VIRQ */
    } u;
#ifdef FLASK_ENABLE
    void *ssid;
#endif
};

int  evtchn_init(struct domain *d); /* from domain_create */
void evtchn_destroy(struct domain *d); /* from domain_kill */
void evtchn_destroy_final(struct domain *d); /* from complete_domain_destroy */

struct waitqueue_vcpu;

struct vcpu 
{
    int              vcpu_id;

    int              processor;

    vcpu_info_t     *vcpu_info;

    struct domain   *domain;

    struct vcpu     *next_in_list;

    s_time_t         periodic_period;
    s_time_t         periodic_last_event;
    struct timer     periodic_timer;
    struct timer     singleshot_timer;

    struct timer     poll_timer;    /* timeout for SCHEDOP_poll */

    void            *sched_priv;    /* scheduler-specific data */

    struct vcpu_runstate_info runstate;
#ifndef CONFIG_COMPAT
# define runstate_guest(v) ((v)->runstate_guest)
    XEN_GUEST_HANDLE(vcpu_runstate_info_t) runstate_guest; /* guest address */
#else
# define runstate_guest(v) ((v)->runstate_guest.native)
    union {
        XEN_GUEST_HANDLE(vcpu_runstate_info_t) native;
        XEN_GUEST_HANDLE(vcpu_runstate_info_compat_t) compat;
    } runstate_guest; /* guest address */
#endif

    /* last time when vCPU is scheduled out */
    uint64_t last_run_time;

    /* Has the FPU been initialised? */
    bool_t           fpu_initialised;
    /* Has the FPU been used since it was last saved? */
    bool_t           fpu_dirtied;
    /* Initialization completed for this VCPU? */
    bool_t           is_initialised;
    /* Currently running on a CPU? */
    bool_t           is_running;
    /* VCPU should wake fast (do not deep sleep the CPU). */
    bool_t           is_urgent;

#ifdef VCPU_TRAP_LAST
#define VCPU_TRAP_NONE    0
    struct {
        bool_t           pending;
        uint8_t          old_mask;
    }                async_exception_state[VCPU_TRAP_LAST];
#define async_exception_state(t) async_exception_state[(t)-1]
    uint8_t          async_exception_mask;
#endif

    /* Require shutdown to be deferred for some asynchronous operation? */
    bool_t           defer_shutdown;
    /* VCPU is paused following shutdown request (d->is_shutting_down)? */
    bool_t           paused_for_shutdown;

    /*
     * > 0: a single port is being polled;
     * = 0: nothing is being polled (vcpu should be clear in d->poll_mask);
     * < 0: multiple ports may be being polled.
     */
    int              poll_evtchn;

    /* (over-)protected by ->domain->event_lock */
    int              pirq_evtchn_head;

    unsigned long    pause_flags;
    atomic_t         pause_count;

    /* IRQ-safe virq_lock protects against delivering VIRQ to stale evtchn. */
    u16              virq_to_evtchn[NR_VIRQS];
    spinlock_t       virq_lock;

    /* Bitmask of CPUs on which this VCPU may run. */
    cpumask_var_t    cpu_affinity;
    /* Used to change affinity temporarily. */
    cpumask_var_t    cpu_affinity_tmp;

    /* Bitmask of CPUs which are holding onto this VCPU's state. */
    cpumask_var_t    vcpu_dirty_cpumask;

    /* Tasklet for continue_hypercall_on_cpu(). */
    struct tasklet   continue_hypercall_tasklet;

    /* Multicall information. */
    struct mc_state  mc_state;

    struct waitqueue_vcpu *waitqueue_vcpu;

    struct arch_vcpu arch;
};

/* Per-domain lock can be recursively acquired in fault handlers. */
#define domain_lock(d) spin_lock_recursive(&(d)->domain_lock)
#define domain_unlock(d) spin_unlock_recursive(&(d)->domain_lock)
#define domain_is_locked(d) spin_is_locked(&(d)->domain_lock)

/* Memory event */
struct mem_event_domain
{
    /* ring lock */
    spinlock_t ring_lock;
    /* The ring has 64 entries */
    unsigned char foreign_producers;
    unsigned char target_producers;
    /* shared ring page */
    void *ring_page;
    struct page_info *ring_pg_struct;
    /* front-end ring */
    mem_event_front_ring_t front_ring;
    /* event channel port (vcpu0 only) */
    int xen_port;
    /* mem_event bit for vcpu->pause_flags */
    int pause_flag;
    /* list of vcpus waiting for room in the ring */
    struct waitqueue_head wq;
    /* the number of vCPUs blocked */
    unsigned int blocked;
    /* The last vcpu woken up */
    unsigned int last_vcpu_wake_up;
};

struct mem_event_per_domain
{
    /* Memory sharing support */
    struct mem_event_domain share;
    /* Memory paging support */
    struct mem_event_domain paging;
    /* Memory access support */
    struct mem_event_domain access;
};

struct domain
{
    domid_t          domain_id;

    shared_info_t   *shared_info;     /* shared data area */

    spinlock_t       domain_lock;

    spinlock_t       page_alloc_lock; /* protects all the following fields  */
    struct page_list_head page_list;  /* linked list */
    struct page_list_head xenpage_list; /* linked list (size xenheap_pages) */
    unsigned int     tot_pages;       /* number of pages currently possesed */
    unsigned int     max_pages;       /* maximum value for tot_pages        */
    atomic_t         shr_pages;       /* number of shared pages             */
    atomic_t         paged_pages;     /* number of paged-out pages          */
    unsigned int     xenheap_pages;   /* # pages allocated from Xen heap    */

    unsigned int     max_vcpus;

    /* Scheduling. */
    void            *sched_priv;    /* scheduler-specific data */
    struct cpupool  *cpupool;

    struct domain   *next_in_list;
    struct domain   *next_in_hashbucket;

    struct list_head rangesets;
    spinlock_t       rangesets_lock;

    /* Event channel information. */
    struct evtchn   *evtchn[NR_EVTCHN_BUCKETS];
    spinlock_t       event_lock;

    struct grant_table *grant_table;

    /*
     * Interrupt to event-channel mappings and other per-guest-pirq data.
     * Protected by the domain's event-channel spinlock.
     */
    unsigned int     nr_pirqs;
    struct radix_tree_root pirq_tree;

    /* I/O capabilities (access to IRQs and memory-mapped I/O). */
    struct rangeset *iomem_caps;
    struct rangeset *irq_caps;

    /* Is this an HVM guest? */
    bool_t           is_hvm;
#ifdef HAS_PASSTHROUGH
    /* Does this guest need iommu mappings? */
    bool_t           need_iommu;
#endif
    /* Is this guest fully privileged (aka dom0)? */
    bool_t           is_privileged;
    /* Which guest this guest has privileges on */
    struct domain   *target;
    /* Is this guest being debugged by dom0? */
    bool_t           debugger_attached;
    /* Is this guest dying (i.e., a zombie)? */
    enum { DOMDYING_alive, DOMDYING_dying, DOMDYING_dead } is_dying;
    /* Domain is paused by controller software? */
    bool_t           is_paused_by_controller;
    /* Domain's VCPUs are pinned 1:1 to physical CPUs? */
    bool_t           is_pinned;

    /* Are any VCPUs polling event channels (SCHEDOP_poll)? */
#if MAX_VIRT_CPUS <= BITS_PER_LONG
    DECLARE_BITMAP(poll_mask, MAX_VIRT_CPUS);
#else
    unsigned long   *poll_mask;
#endif

    /* Guest has shut down (inc. reason code)? */
    spinlock_t       shutdown_lock;
    bool_t           is_shutting_down; /* in process of shutting down? */
    bool_t           is_shut_down;     /* fully shut down? */
    int              shutdown_code;

    /* If this is not 0, send suspend notification here instead of
     * raising DOM_EXC */
    int              suspend_evtchn;

    atomic_t         pause_count;

    unsigned long    vm_assist;

    atomic_t         refcnt;

    struct vcpu    **vcpu;

    /* Bitmask of CPUs which are holding onto this domain's state. */
    cpumask_var_t    domain_dirty_cpumask;

    struct arch_domain arch;

    void *ssid; /* sHype security subject identifier */

    /* Control-plane tools handle for this domain. */
    xen_domain_handle_t handle;

    /* OProfile support. */
    struct xenoprof *xenoprof;
    int32_t time_offset_seconds;

    /* Domain watchdog. */
#define NR_DOMAIN_WATCHDOG_TIMERS 2
    spinlock_t watchdog_lock;
    uint32_t watchdog_inuse_map;
    struct timer watchdog_timer[NR_DOMAIN_WATCHDOG_TIMERS];

    struct rcu_head rcu;

    /*
     * Hypercall deadlock avoidance lock. Used if a hypercall might
     * cause a deadlock. Acquirers don't spin waiting; they preempt.
     */
    spinlock_t hypercall_deadlock_mutex;

    /* transcendent memory, auto-allocated on first tmem op by each domain */
    void *tmem;

    struct lock_profile_qhead profile_head;

    /* Non-migratable and non-restoreable? */
    bool_t disable_migrate;

    /* Various mem_events */
    struct mem_event_per_domain *mem_event;

    /* Currently computed from union of all vcpu cpu-affinity masks. */
    nodemask_t node_affinity;
    unsigned int last_alloc_node;
    spinlock_t node_affinity_lock;
};

struct domain_setup_info
{
    /* Initialised by caller. */
    unsigned long image_addr;
    unsigned long image_len;
    /* Initialised by loader: Public. */
    unsigned long v_start;
    unsigned long v_end;
    unsigned long v_kernstart;
    unsigned long v_kernend;
    unsigned long v_kernentry;
#define PAEKERN_no           0
#define PAEKERN_yes          1
#define PAEKERN_extended_cr3 2
#define PAEKERN_bimodal      3
    unsigned int  pae_kernel;
    /* Initialised by loader: Private. */
    unsigned long elf_paddr_offset;
    unsigned int  load_symtab;
    unsigned long symtab_addr;
    unsigned long symtab_len;
};

/* Protect updates/reads (resp.) of domain_list and domain_hash. */
extern spinlock_t domlist_update_lock;
extern rcu_read_lock_t domlist_read_lock;

extern struct vcpu *idle_vcpu[NR_CPUS];
#define is_idle_domain(d) ((d)->domain_id == DOMID_IDLE)
#define is_idle_vcpu(v)   (is_idle_domain((v)->domain))

#define DOMAIN_DESTROYED (1<<31) /* assumes atomic_t is >= 32 bits */
#define put_domain(_d) \
  if ( atomic_dec_and_test(&(_d)->refcnt) ) domain_destroy(_d)

/*
 * Use this when you don't have an existing reference to @d. It returns
 * FALSE if @d is being destroyed.
 */
static always_inline int get_domain(struct domain *d)
{
    atomic_t old, new, seen = d->refcnt;
    do
    {
        old = seen;
        if ( unlikely(_atomic_read(old) & DOMAIN_DESTROYED) )
            return 0;
        _atomic_set(new, _atomic_read(old) + 1);
        seen = atomic_compareandswap(old, new, &d->refcnt);
    }
    while ( unlikely(_atomic_read(seen) != _atomic_read(old)) );
    return 1;
}

/*
 * Use this when you already have, or are borrowing, a reference to @d.
 * In this case we know that @d cannot be destroyed under our feet.
 */
static inline void get_knownalive_domain(struct domain *d)
{
    atomic_inc(&d->refcnt);
    ASSERT(!(atomic_read(&d->refcnt) & DOMAIN_DESTROYED));
}

void domain_update_node_affinity(struct domain *d);

struct domain *domain_create(
    domid_t domid, unsigned int domcr_flags, uint32_t ssidref);
 /* DOMCRF_hvm: Create an HVM domain, as opposed to a PV domain. */
#define _DOMCRF_hvm           0
#define DOMCRF_hvm            (1U<<_DOMCRF_hvm)
 /* DOMCRF_hap: Create a domain with hardware-assisted paging. */
#define _DOMCRF_hap           1
#define DOMCRF_hap            (1U<<_DOMCRF_hap)
 /* DOMCRF_s3_integrity: Create a domain with tboot memory integrity protection
                        by tboot */
#define _DOMCRF_s3_integrity  2
#define DOMCRF_s3_integrity   (1U<<_DOMCRF_s3_integrity)
 /* DOMCRF_dummy: Create a dummy domain (not scheduled; not on domain list) */
#define _DOMCRF_dummy         3
#define DOMCRF_dummy          (1U<<_DOMCRF_dummy)
 /* DOMCRF_oos_off: dont use out-of-sync optimization for shadow page tables */
#define _DOMCRF_oos_off         4
#define DOMCRF_oos_off          (1U<<_DOMCRF_oos_off)

/*
 * rcu_lock_domain_by_id() is more efficient than get_domain_by_id().
 * This is the preferred function if the returned domain reference
 * is short lived,  but it cannot be used if the domain reference needs 
 * to be kept beyond the current scope (e.g., across a softirq).
 * The returned domain reference must be discarded using rcu_unlock_domain().
 */
struct domain *rcu_lock_domain_by_id(domid_t dom);

/*
 * As above function, but accounts for current domain context:
 *  - Translates target DOMID_SELF into caller's domain id; and
 *  - Checks that caller has permission to act on the target domain.
 */
int rcu_lock_target_domain_by_id(domid_t dom, struct domain **d);

/*
 * As rcu_lock_target_domain_by_id(), but will fail EPERM rather than resolve
 * to local domain. Successful return always resolves to a remote domain that
 * the local domain is privileged to control.
 */
int rcu_lock_remote_target_domain_by_id(domid_t dom, struct domain **d);

/* Finish a RCU critical region started by rcu_lock_domain_by_id(). */
static inline void rcu_unlock_domain(struct domain *d)
{
    if ( d != current->domain )
        rcu_read_unlock(&domlist_read_lock);
}

static inline struct domain *rcu_lock_domain(struct domain *d)
{
    if ( d != current->domain )
        rcu_read_lock(d);
    return d;
}

static inline struct domain *rcu_lock_current_domain(void)
{
    return /*rcu_lock_domain*/(current->domain);
}

struct domain *get_domain_by_id(domid_t dom);
void domain_destroy(struct domain *d);
int domain_kill(struct domain *d);
void domain_shutdown(struct domain *d, u8 reason);
void domain_resume(struct domain *d);
void domain_pause_for_debugger(void);

int vcpu_start_shutdown_deferral(struct vcpu *v);
void vcpu_end_shutdown_deferral(struct vcpu *v);

/*
 * Mark specified domain as crashed. This function always returns, even if the
 * caller is the specified domain. The domain is not synchronously descheduled
 * from any processor.
 */
void __domain_crash(struct domain *d);
#define domain_crash(d) do {                                              \
    printk("domain_crash called from %s:%d\n", __FILE__, __LINE__);       \
    __domain_crash(d);                                                    \
} while (0)

/*
 * Mark current domain as crashed and synchronously deschedule from the local
 * processor. This function never returns.
 */
void __domain_crash_synchronous(void) __attribute__((noreturn));
#define domain_crash_synchronous() do {                                   \
    printk("domain_crash_sync called from %s:%d\n", __FILE__, __LINE__);  \
    __domain_crash_synchronous();                                         \
} while (0)

#define set_current_state(_s) do { current->state = (_s); } while (0)
void scheduler_init(void);
int  sched_init_vcpu(struct vcpu *v, unsigned int processor);
void sched_destroy_vcpu(struct vcpu *v);
int  sched_init_domain(struct domain *d);
void sched_destroy_domain(struct domain *d);
int sched_move_domain(struct domain *d, struct cpupool *c);
long sched_adjust(struct domain *, struct xen_domctl_scheduler_op *);
long sched_adjust_global(struct xen_sysctl_scheduler_op *);
int  sched_id(void);
void sched_tick_suspend(void);
void sched_tick_resume(void);
void vcpu_wake(struct vcpu *d);
void vcpu_sleep_nosync(struct vcpu *d);
void vcpu_sleep_sync(struct vcpu *d);

/*
 * Force synchronisation of given VCPU's state. If it is currently descheduled,
 * this call will ensure that all its state is committed to memory and that
 * no CPU is using critical state (e.g., page tables) belonging to the VCPU.
 */
void sync_vcpu_execstate(struct vcpu *v);

/* As above, for any lazy state being held on the local CPU. */
void sync_local_execstate(void);

/*
 * Called by the scheduler to switch to another VCPU. This function must
 * call context_saved(@prev) when the local CPU is no longer running in
 * @prev's context, and that context is saved to memory. Alternatively, if
 * implementing lazy context switching, it suffices to ensure that invoking
 * sync_vcpu_execstate() will switch and commit @prev's state.
 */
void context_switch(
    struct vcpu *prev, 
    struct vcpu *next);

/*
 * As described above, context_switch() must call this function when the
 * local CPU is no longer running in @prev's context, and @prev's context is
 * saved to memory. Alternatively, if implementing lazy context switching,
 * ensure that invoking sync_vcpu_execstate() will switch and commit @prev.
 */
void context_saved(struct vcpu *prev);

/* Called by the scheduler to continue running the current VCPU. */
void continue_running(
    struct vcpu *same);

void startup_cpu_idle_loop(void);
extern void (*pm_idle) (void);
extern void (*dead_idle) (void);


/*
 * Creates a continuation to resume the current hypercall. The caller should
 * return immediately, propagating the value returned from this invocation.
 * The format string specifies the types and number of hypercall arguments.
 * It contains one character per argument as follows:
 *  'i' [unsigned] {char, int}
 *  'l' [unsigned] long
 *  'h' guest handle (XEN_GUEST_HANDLE(foo))
 */
unsigned long hypercall_create_continuation(
    unsigned int op, const char *format, ...);
void hypercall_cancel_continuation(void);

#define hypercall_preempt_check() (unlikely(    \
        softirq_pending(smp_processor_id()) |   \
        local_events_need_delivery()            \
    ))

extern struct domain *domain_list;

/* Caller must hold the domlist_read_lock or domlist_update_lock. */
static inline struct domain *first_domain_in_cpupool( struct cpupool *c)
{
    struct domain *d;
    for (d = rcu_dereference(domain_list); d && d->cpupool != c;
         d = rcu_dereference(d->next_in_list));
    return d;
}
static inline struct domain *next_domain_in_cpupool(
    struct domain *d, struct cpupool *c)
{
    for (d = rcu_dereference(d->next_in_list); d && d->cpupool != c;
         d = rcu_dereference(d->next_in_list));
    return d;
}

#define for_each_domain(_d)                     \
 for ( (_d) = rcu_dereference(domain_list);     \
       (_d) != NULL;                            \
       (_d) = rcu_dereference((_d)->next_in_list )) \

#define for_each_domain_in_cpupool(_d,_c)       \
 for ( (_d) = first_domain_in_cpupool(_c);      \
       (_d) != NULL;                            \
       (_d) = next_domain_in_cpupool((_d), (_c)))

#define for_each_vcpu(_d,_v)                    \
 for ( (_v) = (_d)->vcpu ? (_d)->vcpu[0] : NULL; \
       (_v) != NULL;                            \
       (_v) = (_v)->next_in_list )

/*
 * Per-VCPU pause flags.
 */
 /* Domain is blocked waiting for an event. */
#define _VPF_blocked         0
#define VPF_blocked          (1UL<<_VPF_blocked)
 /* VCPU is offline. */
#define _VPF_down            1
#define VPF_down             (1UL<<_VPF_down)
 /* VCPU is blocked awaiting an event to be consumed by Xen. */
#define _VPF_blocked_in_xen  2
#define VPF_blocked_in_xen   (1UL<<_VPF_blocked_in_xen)
 /* VCPU affinity has changed: migrating to a new CPU. */
#define _VPF_migrating       3
#define VPF_migrating        (1UL<<_VPF_migrating)
 /* VCPU is blocked due to missing mem_paging ring. */
#define _VPF_mem_paging      4
#define VPF_mem_paging       (1UL<<_VPF_mem_paging)
 /* VCPU is blocked due to missing mem_access ring. */
#define _VPF_mem_access      5
#define VPF_mem_access       (1UL<<_VPF_mem_access)
 /* VCPU is blocked due to missing mem_sharing ring. */
#define _VPF_mem_sharing     6
#define VPF_mem_sharing      (1UL<<_VPF_mem_sharing)

static inline int vcpu_runnable(struct vcpu *v)
{
    return !(v->pause_flags |
             atomic_read(&v->pause_count) |
             atomic_read(&v->domain->pause_count));
}

void vcpu_unblock(struct vcpu *v);
void vcpu_pause(struct vcpu *v);
void vcpu_pause_nosync(struct vcpu *v);
void domain_pause(struct domain *d);
void vcpu_unpause(struct vcpu *v);
void domain_unpause(struct domain *d);
void domain_pause_by_systemcontroller(struct domain *d);
void domain_unpause_by_systemcontroller(struct domain *d);
void cpu_init(void);

struct scheduler;

struct scheduler *scheduler_get_default(void);
struct scheduler *scheduler_alloc(unsigned int sched_id, int *perr);
void scheduler_free(struct scheduler *sched);
int schedule_cpu_switch(unsigned int cpu, struct cpupool *c);
void vcpu_force_reschedule(struct vcpu *v);
int cpu_disable_scheduler(unsigned int cpu);
int vcpu_set_affinity(struct vcpu *v, const cpumask_t *affinity);

void vcpu_runstate_get(struct vcpu *v, struct vcpu_runstate_info *runstate);
uint64_t get_cpu_idle_time(unsigned int cpu);

/*
 * Used by idle loop to decide whether there is work to do:
 *  (1) Run softirqs; or (2) Play dead; or (3) Run tasklets.
 */
#define cpu_is_haltable(cpu)                    \
    (!softirq_pending(cpu) &&                   \
     cpu_online(cpu) &&                         \
     !per_cpu(tasklet_work_to_do, cpu))

void watchdog_domain_init(struct domain *d);
void watchdog_domain_destroy(struct domain *d);

#define IS_PRIV(_d) ((_d)->is_privileged)
#define IS_PRIV_FOR(_d, _t) (IS_PRIV(_d) || ((_d)->target && (_d)->target == (_t)))

#define VM_ASSIST(_d,_t) (test_bit((_t), &(_d)->vm_assist))

#define is_hvm_domain(d) ((d)->is_hvm)
#define is_hvm_vcpu(v)   (is_hvm_domain(v->domain))
#define is_pinned_vcpu(v) ((v)->domain->is_pinned || \
                           cpumask_weight((v)->cpu_affinity) == 1)
#ifdef HAS_PASSTHROUGH
#define need_iommu(d)    ((d)->need_iommu)
#else
#define need_iommu(d)    (0)
#endif

void set_vcpu_migration_delay(unsigned int delay);
unsigned int get_vcpu_migration_delay(void);

extern bool_t sched_smt_power_savings;

extern enum cpufreq_controller {
    FREQCTL_none, FREQCTL_dom0_kernel, FREQCTL_xen
} cpufreq_controller;

#define CPUPOOLID_NONE    -1

struct cpupool *cpupool_get_by_id(int poolid);
void cpupool_put(struct cpupool *pool);
int cpupool_add_domain(struct domain *d, int poolid);
void cpupool_rm_domain(struct domain *d);
int cpupool_do_sysctl(struct xen_sysctl_cpupool_op *op);
void schedule_dump(struct cpupool *c);
extern void dump_runq(unsigned char key);

#define num_cpupool_cpus(c) cpumask_weight((c)->cpu_valid)

#endif /* __SCHED_H__ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
