#include <xen/init.h>
#include <xen/types.h>
#include <xen/irq.h>
#include <xen/event.h>
#include <xen/kernel.h>
#include <xen/delay.h>
#include <xen/smp.h>
#include <xen/mm.h>
#include <xen/cpu.h>
#include <asm/processor.h> 
#include <public/sysctl.h>
#include <asm/system.h>
#include <asm/msr.h>
#include <asm/p2m.h>
#include <asm/mce.h>
#include <asm/apic.h>
#include "mce.h"
#include "x86_mca.h"

DEFINE_PER_CPU(struct mca_banks *, mce_banks_owned);
DEFINE_PER_CPU(struct mca_banks *, no_cmci_banks);
DEFINE_PER_CPU(struct mca_banks *, mce_clear_banks);
bool_t __read_mostly cmci_support = 0;
static bool_t __read_mostly ser_support = 0;
static bool_t __read_mostly mce_force_broadcast;
boolean_param("mce_fb", mce_force_broadcast);

static int __read_mostly nr_intel_ext_msrs;

/* Intel SDM define bit15~bit0 of IA32_MCi_STATUS as the MC error code */
#define INTEL_MCCOD_MASK 0xFFFF

/*
 * Currently Intel SDM define 2 kinds of srao errors:
 * 1). Memory scrubbing error, error code = 0xC0 ~ 0xCF
 * 2). L3 explicit writeback error, error code = 0x17A
 */
#define INTEL_SRAO_MEM_SCRUB 0xC0 ... 0xCF
#define INTEL_SRAO_L3_EWB    0x17A

/*
 * Currently Intel SDM define 2 kinds of srar errors:
 * 1). Data Load error, error code = 0x134
 * 2). Instruction Fetch error, error code = 0x150
 */
#define INTEL_SRAR_DATA_LOAD	0x134
#define INTEL_SRAR_INSTR_FETCH	0x150

#ifdef CONFIG_X86_MCE_THERMAL
static void intel_thermal_interrupt(struct cpu_user_regs *regs)
{
    uint64_t msr_content;
    unsigned int cpu = smp_processor_id();
    static DEFINE_PER_CPU(s_time_t, next);

    ack_APIC_irq();

    if (NOW() < per_cpu(next, cpu))
        return;

    per_cpu(next, cpu) = NOW() + MILLISECS(5000);
    rdmsrl(MSR_IA32_THERM_STATUS, msr_content);
    if (msr_content & 0x1) {
        printk(KERN_EMERG "CPU%d: Temperature above threshold\n", cpu);
        printk(KERN_EMERG "CPU%d: Running in modulated clock mode\n",
                cpu);
        add_taint(TAINT_MACHINE_CHECK);
    } else {
        printk(KERN_INFO "CPU%d: Temperature/speed normal\n", cpu);
    }
}

/* Thermal monitoring depends on APIC, ACPI and clock modulation */
static int intel_thermal_supported(struct cpuinfo_x86 *c)
{
    if (!cpu_has_apic)
        return 0;
    if (!cpu_has(c, X86_FEATURE_ACPI) || !cpu_has(c, X86_FEATURE_ACC))
        return 0;
    return 1;
}

static u32 __read_mostly lvtthmr_init;

static void __init mcheck_intel_therm_init(void)
{
    /*
     * This function is only called on boot CPU. Save the init thermal
     * LVT value on BSP and use that value to restore APs' thermal LVT
     * entry BIOS programmed later
     */
    if (intel_thermal_supported(&boot_cpu_data))
        lvtthmr_init = apic_read(APIC_LVTTHMR);
}

/* P4/Xeon Thermal regulation detect and init */
static void intel_init_thermal(struct cpuinfo_x86 *c)
{
    uint64_t msr_content;
    uint32_t val;
    int tm2 = 0;
    unsigned int cpu = smp_processor_id();
    static uint8_t thermal_apic_vector;

    if (!intel_thermal_supported(c))
        return; /* -ENODEV */

    /* first check if its enabled already, in which case there might
     * be some SMM goo which handles it, so we can't even put a handler
     * since it might be delivered via SMI already -zwanem.
     */
    rdmsrl(MSR_IA32_MISC_ENABLE, msr_content);
    val = lvtthmr_init;
    /*
     * The initial value of thermal LVT entries on all APs always reads
     * 0x10000 because APs are woken up by BSP issuing INIT-SIPI-SIPI
     * sequence to them and LVT registers are reset to 0s except for
     * the mask bits which are set to 1s when APs receive INIT IPI.
     * If BIOS takes over the thermal interrupt and sets its interrupt
     * delivery mode to SMI (not fixed), it restores the value that the
     * BIOS has programmed on AP based on BSP's info we saved (since BIOS
     * is required to set the same value for all threads/cores).
     */
    if ((val & APIC_MODE_MASK) != APIC_DM_FIXED
        || (val & APIC_VECTOR_MASK) > 0xf)
        apic_write(APIC_LVTTHMR, val);

    if ((msr_content & (1ULL<<3))
        && (val & APIC_MODE_MASK) == APIC_DM_SMI) {
        if (c == &boot_cpu_data)
            printk(KERN_DEBUG "Thermal monitoring handled by SMI\n");
        return; /* -EBUSY */
    }

    if (cpu_has(c, X86_FEATURE_TM2) && (msr_content & (1ULL << 13)))
        tm2 = 1;

    /* check whether a vector already exists, temporarily masked? */
    if (val & APIC_VECTOR_MASK) {
        if (c == &boot_cpu_data)
            printk(KERN_DEBUG "Thermal LVT vector (%#x) already installed\n",
                   val & APIC_VECTOR_MASK);
        return; /* -EBUSY */
    }

    alloc_direct_apic_vector(&thermal_apic_vector, intel_thermal_interrupt);

    /* The temperature transition interrupt handler setup */
    val = thermal_apic_vector;    /* our delivery vector */
    val |= (APIC_DM_FIXED | APIC_LVT_MASKED);  /* we'll mask till we're ready */
    apic_write_around(APIC_LVTTHMR, val);

    rdmsrl(MSR_IA32_THERM_INTERRUPT, msr_content);
    wrmsrl(MSR_IA32_THERM_INTERRUPT, msr_content | 0x03);

    rdmsrl(MSR_IA32_MISC_ENABLE, msr_content);
    wrmsrl(MSR_IA32_MISC_ENABLE, msr_content | (1ULL<<3));

    apic_write_around(APIC_LVTTHMR, val & ~APIC_LVT_MASKED);
    if (opt_cpu_info)
        printk(KERN_INFO "CPU%u: Thermal monitoring enabled (%s)\n",
                cpu, tm2 ? "TM2" : "TM1");
    return;
}
#endif /* CONFIG_X86_MCE_THERMAL */

/* MCE handling */
struct mce_softirq_barrier {
	atomic_t val;
	atomic_t ingen;
	atomic_t outgen;
};

static struct mce_softirq_barrier mce_inside_bar, mce_severity_bar;
static struct mce_softirq_barrier mce_trap_bar;

/*
 * mce_logout_lock should only be used in the trap handler,
 * while MCIP has not been cleared yet in the global status
 * register. Other use is not safe, since an MCE trap can
 * happen at any moment, which would cause lock recursion.
 */
static DEFINE_SPINLOCK(mce_logout_lock);

static atomic_t severity_cpu = ATOMIC_INIT(-1);
static atomic_t found_error = ATOMIC_INIT(0);
static cpumask_t mce_fatal_cpus;

static void mce_barrier_enter(struct mce_softirq_barrier *);
static void mce_barrier_exit(struct mce_softirq_barrier *);

static const struct mca_error_handler *__read_mostly mce_dhandlers;
static const struct mca_error_handler *__read_mostly mce_uhandlers;
static unsigned int __read_mostly mce_dhandler_num;
static unsigned int __read_mostly mce_uhandler_num;

/* Maybe called in MCE context, no lock, no printk */
static enum mce_result mce_action(struct cpu_user_regs *regs,
                      mctelem_cookie_t mctc)
{
    struct mc_info *local_mi;
    enum mce_result bank_result = MCER_NOERROR;
    enum mce_result worst_result = MCER_NOERROR;
    struct mcinfo_common *mic = NULL;
    struct mca_binfo binfo;
    const struct mca_error_handler *handlers = mce_dhandlers;
    unsigned int i, handler_num = mce_dhandler_num;

    /* When in mce context, regs is valid */
    if (regs)
    {
        handler_num = mce_uhandler_num;
        handlers = mce_uhandlers;
    }

    /* At least a default handler should be registerd */
    ASSERT(handler_num);

    local_mi = (struct mc_info*)mctelem_dataptr(mctc);
    x86_mcinfo_lookup(mic, local_mi, MC_TYPE_GLOBAL);
    if (mic == NULL) {
        printk(KERN_ERR "MCE: get local buffer entry failed\n ");
        return MCER_CONTINUE;
    }

    memset(&binfo, 0, sizeof(binfo));
    binfo.mig = (struct mcinfo_global *)mic;
    binfo.mi = local_mi;

    /* Processing bank information */
    x86_mcinfo_lookup(mic, local_mi, MC_TYPE_BANK);

    for ( ; bank_result != MCER_RESET && mic && mic->size;
          mic = x86_mcinfo_next(mic) )
    {
        if (mic->type != MC_TYPE_BANK) {
            continue;
        }
        binfo.mib = (struct mcinfo_bank*)mic;
        binfo.bank = binfo.mib->mc_bank;
        bank_result = MCER_NOERROR;
        for ( i = 0; i < handler_num; i++ ) {
            if (handlers[i].owned_error(binfo.mib->mc_status))
            {
                handlers[i].recovery_handler(&binfo, &bank_result, regs);
                if (worst_result < bank_result)
                    worst_result = bank_result;
                break;
            }
        }
        ASSERT(i != handler_num);
    }

    return worst_result;
}

/*
 * Called from mctelem_process_deferred. Return 1 if the telemetry
 * should be committed for dom0 consumption, 0 if it should be
 * dismissed.
 */
static int mce_delayed_action(mctelem_cookie_t mctc)
{
    enum mce_result result;
    int ret = 0;

    result = mce_action(NULL, mctc);

    switch (result)
    {
    case MCER_RESET:
        dprintk(XENLOG_ERR, "MCE delayed action failed\n");
        is_mc_panic = 1;
        x86_mcinfo_dump(mctelem_dataptr(mctc));
        panic("MCE: Software recovery failed for the UCR\n");
        break;
    case MCER_RECOVERED:
        dprintk(XENLOG_INFO, "MCE: Error is successfully recovered\n");
        ret  = 1;
        break;
    case MCER_CONTINUE:
        dprintk(XENLOG_INFO, "MCE: Error can't be recovered, "
            "system is tainted\n");
        x86_mcinfo_dump(mctelem_dataptr(mctc));
        ret = 1;
        break;
    default:
        ret = 0;
        break;
    }
    return ret;
}

/* Softirq Handler for this MCE# processing */
static void mce_softirq(void)
{
    int cpu = smp_processor_id();
    unsigned int workcpu;

    mce_printk(MCE_VERBOSE, "CPU%d enter softirq\n", cpu);

    mce_barrier_enter(&mce_inside_bar);

    /*
     * Everybody is here. Now let's see who gets to do the
     * recovery work. Right now we just see if there's a CPU
     * that did not have any problems, and pick that one.
     *
     * First, just set a default value: the last CPU who reaches this
     * will overwrite the value and become the default.
     */

    atomic_set(&severity_cpu, cpu);

    mce_barrier_enter(&mce_severity_bar);
    if (!mctelem_has_deferred(cpu))
        atomic_set(&severity_cpu, cpu);
    mce_barrier_exit(&mce_severity_bar);

    /* We choose severity_cpu for further processing */
    if (atomic_read(&severity_cpu) == cpu) {

        mce_printk(MCE_VERBOSE, "CPU%d handling errors\n", cpu);

        /* Step1: Fill DOM0 LOG buffer, vMCE injection buffer and
         * vMCE MSRs virtualization buffer
         */
        for_each_online_cpu(workcpu) {
            mctelem_process_deferred(workcpu, mce_delayed_action);
        }

        /* Step2: Send Log to DOM0 through vIRQ */
        if (dom0_vmce_enabled()) {
            mce_printk(MCE_VERBOSE, "MCE: send MCE# to DOM0 through virq\n");
            send_global_virq(VIRQ_MCA);
        }
    }

    mce_barrier_exit(&mce_inside_bar);
}

/*
 * Return:
 * -1: if system can't be recoved
 * 0: Continoue to next step
 */
static int mce_urgent_action(struct cpu_user_regs *regs,
                              mctelem_cookie_t mctc)
{
    uint64_t gstatus;

    if ( mctc == NULL)
        return 0;

    gstatus = mca_rdmsr(MSR_IA32_MCG_STATUS);

    /*
     * FIXME: When RIPV = EIPV = 0, it's a little bit tricky. It may be an
     * asynchronic error, currently we have no way to precisely locate
     * whether the error occur at guest or hypervisor.
     * To avoid handling error in wrong way, we treat it as unrecovered.
     *
     * Another unrecovered case is RIPV = 0 while in hypervisor
     * since Xen is not pre-emptible.
     */
    if ( !(gstatus & MCG_STATUS_RIPV) &&
         (!(gstatus & MCG_STATUS_EIPV) || !guest_mode(regs)) )
        return -1;

    return mce_action(regs, mctc) == MCER_RESET ? -1 : 0;
}

/* Machine Check owner judge algorithm:
 * When error happens, all cpus serially read its msr banks.
 * The first CPU who fetches the error bank's info will clear
 * this bank. Later readers can't get any infor again.
 * The first CPU is the actual mce_owner
 *
 * For Fatal (pcc=1) error, it might cause machine crash
 * before we're able to log. For avoiding log missing, we adopt two
 * round scanning:
 * Round1: simply scan. If found pcc = 1 or ripv = 0, simply reset.
 * All MCE banks are sticky, when boot up, MCE polling mechanism
 * will help to collect and log those MCE errors.
 * Round2: Do all MCE processing logic as normal.
 */

static void mce_panic_check(void)
{
      if (is_mc_panic) {
              local_irq_enable();
              for ( ; ; )
                      halt();
      }
}

/*
 * Initialize a barrier. Just set it to 0.
 */
static void mce_barrier_init(struct mce_softirq_barrier *bar)
{
      atomic_set(&bar->val, 0);
      atomic_set(&bar->ingen, 0);
      atomic_set(&bar->outgen, 0);
}

static void mce_handler_init(void)
{
    if (smp_processor_id() != 0)
        return;

    /* callback register, do we really need so many callback? */
    /* mce handler data initialization */
    mce_barrier_init(&mce_inside_bar);
    mce_barrier_init(&mce_severity_bar);
    mce_barrier_init(&mce_trap_bar);
    spin_lock_init(&mce_logout_lock);
    open_softirq(MACHINE_CHECK_SOFTIRQ, mce_softirq);
}
#if 0
/*
 * This function will need to be used when offlining a CPU in the
 * recovery actions.
 *
 * Decrement a barrier only. Needed for cases where the CPU
 * in question can't do it itself (e.g. it is being offlined).
 */
static void mce_barrier_dec(struct mce_softirq_barrier *bar)
{
      atomic_inc(&bar->outgen);
      wmb();
      atomic_dec(&bar->val);
}
#endif

static void mce_spin_lock(spinlock_t *lk)
{
      while (!spin_trylock(lk)) {
              cpu_relax();
              mce_panic_check();
      }
}

static void mce_spin_unlock(spinlock_t *lk)
{
      spin_unlock(lk);
}

/*
 * Increment the generation number and the value. The generation number
 * is incremented when entering a barrier. This way, it can be checked
 * on exit if a CPU is trying to re-enter the barrier. This can happen
 * if the first CPU to make it out immediately exits or re-enters, while
 * another CPU that is still in the loop becomes otherwise occupied
 * (e.g. it needs to service an interrupt, etc), missing the value
 * it's waiting for.
 *
 * These barrier functions should always be paired, so that the
 * counter value will reach 0 again after all CPUs have exited.
 */
static void mce_barrier_enter(struct mce_softirq_barrier *bar)
{
      int gen;

      if (!mce_broadcast)
          return;
      atomic_inc(&bar->ingen);
      gen = atomic_read(&bar->outgen);
      mb();
      atomic_inc(&bar->val);
      while ( atomic_read(&bar->val) != num_online_cpus() &&
          atomic_read(&bar->outgen) == gen) {
              mb();
              mce_panic_check();
      }
}

static void mce_barrier_exit(struct mce_softirq_barrier *bar)
{
      int gen;

      if (!mce_broadcast)
          return;
      atomic_inc(&bar->outgen);
      gen = atomic_read(&bar->ingen);
      mb();
      atomic_dec(&bar->val);
      while ( atomic_read(&bar->val) != 0 &&
          atomic_read(&bar->ingen) == gen ) {
              mb();
              mce_panic_check();
      }
}

#if 0
static void mce_barrier(struct mce_softirq_barrier *bar)
{
      mce_barrier_enter(bar);
      mce_barrier_exit(bar);
}
#endif

/* Intel MCE handler */
static inline void intel_get_extended_msr(struct mcinfo_extended *ext, u32 msr)
{
    if ( ext->mc_msrs < ARRAY_SIZE(ext->mc_msr)
         && msr < MSR_IA32_MCG_EAX + nr_intel_ext_msrs ) {
        ext->mc_msr[ext->mc_msrs].reg = msr;
        rdmsrl(msr, ext->mc_msr[ext->mc_msrs].value);
        ++ext->mc_msrs;
    }
}


struct mcinfo_extended *
intel_get_extended_msrs(struct mcinfo_global *mig, struct mc_info *mi)
{
    struct mcinfo_extended *mc_ext;
    int i;

    /*
     * According to spec, processor _support_ 64 bit will always
     * have MSR beyond IA32_MCG_MISC
     */
    if (!mi|| !mig || nr_intel_ext_msrs == 0 ||
            !(mig->mc_gstatus & MCG_STATUS_EIPV))
        return NULL;

    mc_ext = x86_mcinfo_reserve(mi, sizeof(struct mcinfo_extended));
    if (!mc_ext)
    {
        mi->flags |= MCINFO_FLAGS_UNCOMPLETE;
        return NULL;
    }

    /* this function will called when CAP(9).MCG_EXT_P = 1 */
    memset(&mc_ext, 0, sizeof(struct mcinfo_extended));
    mc_ext->common.type = MC_TYPE_EXTENDED;
    mc_ext->common.size = sizeof(struct mcinfo_extended);

    for (i = MSR_IA32_MCG_EAX; i <= MSR_IA32_MCG_MISC; i++)
        intel_get_extended_msr(mc_ext, i);

#ifdef __x86_64__
    for (i = MSR_IA32_MCG_R8; i <= MSR_IA32_MCG_R15; i++)
        intel_get_extended_msr(mc_ext, i);
#endif

    return mc_ext;
}

enum intel_mce_type
{
    intel_mce_invalid,
    intel_mce_fatal,
    intel_mce_corrected,
    intel_mce_ucr_ucna,
    intel_mce_ucr_srao,
    intel_mce_ucr_srar,
};

static enum intel_mce_type intel_check_mce_type(uint64_t status)
{
    if (!(status & MCi_STATUS_VAL))
        return intel_mce_invalid;

    if (status & MCi_STATUS_PCC)
        return intel_mce_fatal;

    /* Corrected error? */
    if (!(status & MCi_STATUS_UC))
        return intel_mce_corrected;

    if (!ser_support)
        return intel_mce_fatal;

    if (status & MCi_STATUS_S)
    {
        if (status & MCi_STATUS_AR)
        {
            if (status & MCi_STATUS_OVER)
                return intel_mce_fatal;
            else
                return intel_mce_ucr_srar;
        } else
            return intel_mce_ucr_srao;
    }
    else
        return intel_mce_ucr_ucna;

    /* Any type not included abovoe ? */
    return intel_mce_fatal;
}

struct mcinfo_recovery *mci_add_pageoff_action(int bank, struct mc_info *mi,
                              uint64_t mfn, uint32_t status)
{
    struct mcinfo_recovery *rec;

    if (!mi)
        return NULL;

    rec = x86_mcinfo_reserve(mi, sizeof(struct mcinfo_recovery));
    if (!rec)
    {
        mi->flags |= MCINFO_FLAGS_UNCOMPLETE;
        return NULL;
    }

    memset(rec, 0, sizeof(struct mcinfo_recovery));

    rec->mc_bank = bank;
    rec->action_types = MC_ACTION_PAGE_OFFLINE;
    rec->action_info.page_retire.mfn = mfn;
    rec->action_info.page_retire.status = status;
    return rec;
}

static void intel_memerr_dhandler(
             struct mca_binfo *binfo,
             enum mce_result *result,
             struct cpu_user_regs *regs)
{
    struct mcinfo_bank *bank = binfo->mib;
    struct mcinfo_global *global = binfo->mig;
    struct domain *d;
    unsigned long mfn, gfn;
    uint32_t status;
    uint64_t mc_status, mc_misc;

    mce_printk(MCE_VERBOSE, "MCE: Enter UCR recovery action\n");

    mc_status = bank->mc_status;
    mc_misc = bank->mc_misc;
    if (!(mc_status &  MCi_STATUS_ADDRV) ||
        !(mc_status & MCi_STATUS_MISCV) ||
        ((mc_misc & MCi_MISC_ADDRMOD_MASK) != MCi_MISC_PHYSMOD) )
    {
        dprintk(XENLOG_WARNING,
            "No physical address provided for memory error\n");
        return;
    }

    mfn = bank->mc_addr >> PAGE_SHIFT;
    if (offline_page(mfn, 1, &status))
    {
        dprintk(XENLOG_WARNING,
                "Failed to offline page %lx for MCE error\n", mfn);
        return;
    }

    mci_add_pageoff_action(binfo->bank, binfo->mi, mfn, status);

    /* This is free page */
    if (status & PG_OFFLINE_OFFLINED)
        *result = MCER_RECOVERED;
    else if (status & PG_OFFLINE_AGAIN)
        *result = MCER_CONTINUE;
    else if (status & PG_OFFLINE_PENDING) {
        /* This page has owner */
        if (status & PG_OFFLINE_OWNED) {
            bank->mc_domid = status >> PG_OFFLINE_OWNER_SHIFT;
            mce_printk(MCE_QUIET, "MCE: This error page is ownded"
              " by DOM %d\n", bank->mc_domid);
            /* XXX: Cannot handle shared pages yet 
             * (this should identify all domains and gfn mapping to
             *  the mfn in question) */
            BUG_ON( bank->mc_domid == DOMID_COW );
            if ( bank->mc_domid != DOMID_XEN ) {
                d = get_domain_by_id(bank->mc_domid);
                ASSERT(d);
                gfn = get_gpfn_from_mfn((bank->mc_addr) >> PAGE_SHIFT);

                if ( !is_vmce_ready(bank, d) )
                {
                    printk("DOM%d not ready for vMCE\n", d->domain_id);
                    goto vmce_failed;
                }

                if ( unmmap_broken_page(d, _mfn(mfn), gfn) )
                {
                    printk("Unmap broken memory %lx for DOM%d failed\n",
                            mfn, d->domain_id);
                    goto vmce_failed;
                }

                bank->mc_addr =  gfn << PAGE_SHIFT |
                  (bank->mc_addr & (PAGE_SIZE -1 ));
                if ( fill_vmsr_data(bank, d,
                      global->mc_gstatus) == -1 )
                {
                    mce_printk(MCE_QUIET, "Fill vMCE# data for DOM%d "
                      "failed\n", bank->mc_domid);
                    goto vmce_failed;
                }

                /* We will inject vMCE to DOMU*/
                if ( inject_vmce(d) < 0 )
                {
                    mce_printk(MCE_QUIET, "inject vMCE to DOM%d"
                      " failed\n", d->domain_id);
                    goto vmce_failed;
                }
                /* Impacted domain go on with domain's recovery job
                 * if the domain has its own MCA handler.
                 * For xen, it has contained the error and finished
                 * its own recovery job.
                 */
                *result = MCER_RECOVERED;
                put_domain(d);

                return;
vmce_failed:
                put_domain(d);
                domain_crash(d);
            }
        }
    }
}

static int intel_srar_check(uint64_t status)
{
    return ( intel_check_mce_type(status) == intel_mce_ucr_srar );
}

static void intel_srar_dhandler(
             struct mca_binfo *binfo,
             enum mce_result *result,
             struct cpu_user_regs *regs)
{
    uint64_t status = binfo->mib->mc_status;

    /* For unknown srar error code, reset system */
    *result = MCER_RESET;

    switch ( status & INTEL_MCCOD_MASK )
    {
    case INTEL_SRAR_DATA_LOAD:
    case INTEL_SRAR_INSTR_FETCH:
        intel_memerr_dhandler(binfo, result, regs);
        break;
    default:
        break;
    }
}

static int intel_srao_check(uint64_t status)
{
    return ( intel_check_mce_type(status) == intel_mce_ucr_srao );
}

static void intel_srao_dhandler(
             struct mca_binfo *binfo,
             enum mce_result *result,
             struct cpu_user_regs *regs)
{
    uint64_t status = binfo->mib->mc_status;

    /* For unknown srao error code, no action required */
    *result = MCER_CONTINUE;

    if ( status & MCi_STATUS_VAL )
    {
        switch ( status & INTEL_MCCOD_MASK )
        {
        case INTEL_SRAO_MEM_SCRUB:
        case INTEL_SRAO_L3_EWB:
            intel_memerr_dhandler(binfo, result, regs);
            break;
        default:
            break;
        }
    }
}

static int intel_default_check(uint64_t status)
{
    return 1;
}

static void intel_default_mce_dhandler(
             struct mca_binfo *binfo,
             enum mce_result *result,
             struct cpu_user_regs * regs)
{
    uint64_t status = binfo->mib->mc_status;
    enum intel_mce_type type;

    type = intel_check_mce_type(status);

    if (type == intel_mce_fatal)
        *result = MCER_RESET;
    else
        *result = MCER_CONTINUE;
}

static const struct mca_error_handler intel_mce_dhandlers[] = {
    {intel_srao_check, intel_srao_dhandler},
    {intel_srar_check, intel_srar_dhandler},
    {intel_default_check, intel_default_mce_dhandler}
};

static void intel_default_mce_uhandler(
             struct mca_binfo *binfo,
             enum mce_result *result,
             struct cpu_user_regs *regs)
{
    uint64_t status = binfo->mib->mc_status;
    enum intel_mce_type type;

    type = intel_check_mce_type(status);

    switch (type)
    {
    case intel_mce_fatal:
        *result = MCER_RESET;
        break;
    default:
        *result = MCER_CONTINUE;
        break;
    }
}

static const struct mca_error_handler intel_mce_uhandlers[] = {
    {intel_default_check, intel_default_mce_uhandler}
};

static void intel_machine_check(struct cpu_user_regs * regs, long error_code)
{
    uint64_t gstatus;
    mctelem_cookie_t mctc = NULL;
    struct mca_summary bs;
    struct mca_banks *clear_bank;

    mce_spin_lock(&mce_logout_lock);

    clear_bank = __get_cpu_var(mce_clear_banks);
    memset( clear_bank->bank_map, 0x0,
        sizeof(long) * BITS_TO_LONGS(clear_bank->num));
    mctc = mcheck_mca_logout(MCA_MCE_SCAN, mca_allbanks, &bs, clear_bank);

    if (bs.errcnt) {
        /*
         * Uncorrected errors must be dealth with in softirq context.
         */
        if (bs.uc || bs.pcc) {
            add_taint(TAINT_MACHINE_CHECK);
            if (mctc != NULL)
                mctelem_defer(mctc);
            /*
             * For PCC=1 and can't be recovered, context is lost, so reboot now without
             * clearing  the banks, and deal with the telemetry after reboot
             * (the MSRs are sticky)
             */
            if (bs.pcc || !bs.recoverable)
                cpumask_set_cpu(smp_processor_id(), &mce_fatal_cpus);
        } else {
            if (mctc != NULL)
                mctelem_commit(mctc);
        }
        atomic_set(&found_error, 1);

        /* The last CPU will be take check/clean-up etc */
        atomic_set(&severity_cpu, smp_processor_id());

        mce_printk(MCE_CRITICAL, "MCE: clear_bank map %lx on CPU%d\n",
                *((unsigned long*)clear_bank), smp_processor_id());
        mcheck_mca_clearbanks(clear_bank);
    } else {
        if (mctc != NULL)
            mctelem_dismiss(mctc);
    }
    mce_spin_unlock(&mce_logout_lock);

    mce_barrier_enter(&mce_trap_bar);
    if ( mctc != NULL && mce_urgent_action(regs, mctc))
        cpumask_set_cpu(smp_processor_id(), &mce_fatal_cpus);
    mce_barrier_exit(&mce_trap_bar);
    /*
     * Wait until everybody has processed the trap.
     */
    mce_barrier_enter(&mce_trap_bar);
    if (atomic_read(&severity_cpu) == smp_processor_id())
    {
        /* According to SDM, if no error bank found on any cpus,
         * something unexpected happening, we can't do any
         * recovery job but to reset the system.
         */
        if (atomic_read(&found_error) == 0)
            mc_panic("MCE: No CPU found valid MCE, need reset\n");
        if (!cpumask_empty(&mce_fatal_cpus))
        {
            char *ebufp, ebuf[96] = "MCE: Fatal error happened on CPUs ";
            ebufp = ebuf + strlen(ebuf);
            cpumask_scnprintf(ebufp, 95 - strlen(ebuf), &mce_fatal_cpus);
            mc_panic(ebuf);
        }
        atomic_set(&found_error, 0);
    }
    mce_barrier_exit(&mce_trap_bar);

    /* Clear flags after above fatal check */
    mce_barrier_enter(&mce_trap_bar);
    gstatus = mca_rdmsr(MSR_IA32_MCG_STATUS);
    if ((gstatus & MCG_STATUS_MCIP) != 0) {
        mce_printk(MCE_CRITICAL, "MCE: Clear MCIP@ last step");
        mca_wrmsr(MSR_IA32_MCG_STATUS, gstatus & ~MCG_STATUS_MCIP);
    }
    mce_barrier_exit(&mce_trap_bar);

    raise_softirq(MACHINE_CHECK_SOFTIRQ);
}

/* According to MCA OS writer guide, CMCI handler need to clear bank when
 * 1) CE (UC = 0)
 * 2) ser_support = 1, Superious error, OVER = 0, EN = 0, [UC = 1]
 * 3) ser_support = 1, UCNA, OVER = 0, S = 1, AR = 0, PCC = 0, [UC = 1, EN = 1]
 * MCA handler need to clear bank when
 * 1) ser_support = 1, Superious error, OVER = 0, EN = 0, UC = 1
 * 2) ser_support = 1, SRAR, UC = 1, OVER = 0, S = 1, AR = 1, [EN = 1]
 * 3) ser_support = 1, SRAO, UC = 1, S = 1, AR = 0, [EN = 1]
*/

static int intel_need_clearbank_scan(enum mca_source who, u64 status)
{
    if ( who == MCA_CMCI_HANDLER) {
        /* CMCI need clear bank */
        if ( !(status & MCi_STATUS_UC) )
            return 1;
        /* Spurious need clear bank */
        else if ( ser_support && !(status & MCi_STATUS_OVER)
                    && !(status & MCi_STATUS_EN) )
            return 1;
        /* UCNA OVER = 0 need clear bank */
        else if ( ser_support && !(status & MCi_STATUS_OVER) 
                    && !(status & MCi_STATUS_PCC) && !(status & MCi_STATUS_S) 
                    && !(status & MCi_STATUS_AR))
            return 1;
        /* Only Log, no clear */
        else return 0;
    }
    else if ( who == MCA_MCE_SCAN) {
        if ( !ser_support )
            return 0;
        /* 
         * For fatal error, it shouldn't be cleared so that sticky bank
         * have chance to be handled after reboot by polling
         */
        if ( (status & MCi_STATUS_UC) && (status & MCi_STATUS_PCC) )
            return 0;
        /* Spurious need clear bank */
        else if ( !(status & MCi_STATUS_OVER)
                    && (status & MCi_STATUS_UC) && !(status & MCi_STATUS_EN))
            return 1;
        /* SRAR OVER=0 clear bank. OVER = 1 have caused reset */
        else if ( (status & MCi_STATUS_UC)
                    && (status & MCi_STATUS_S) && (status & MCi_STATUS_AR )
                    && !(status & MCi_STATUS_OVER) )
            return 1;
        /* SRAO need clear bank */
        else if ( !(status & MCi_STATUS_AR) 
                    && (status & MCi_STATUS_S) && (status & MCi_STATUS_UC))
            return 1; 
        else
            return 0;
    }

    return 1;
}

/* MCE continues/is recoverable when 
 * 1) CE UC = 0
 * 2) Supious ser_support = 1, OVER = 0, En = 0 [UC = 1]
 * 3) SRAR ser_support = 1, OVER = 0, PCC = 0, S = 1, AR = 1 [UC =1, EN = 1]
 * 4) SRAO ser_support = 1, PCC = 0, S = 1, AR = 0, EN = 1 [UC = 1]
 * 5) UCNA ser_support = 1, OVER = 0, EN = 1, PCC = 0, S = 0, AR = 0, [UC = 1]
 */
static int intel_recoverable_scan(u64 status)
{

    if ( !(status & MCi_STATUS_UC ) )
        return 1;
    else if ( ser_support && !(status & MCi_STATUS_EN) 
                && !(status & MCi_STATUS_OVER) )
        return 1;
    /* SRAR error */
    else if ( ser_support && !(status & MCi_STATUS_OVER) 
                && !(status & MCi_STATUS_PCC) && (status & MCi_STATUS_S)
                && (status & MCi_STATUS_AR) && (status & MCi_STATUS_EN) )
        return 1;
    /* SRAO error */
    else if (ser_support && !(status & MCi_STATUS_PCC)
                && (status & MCi_STATUS_S) && !(status & MCi_STATUS_AR)
                && (status & MCi_STATUS_EN))
        return 1;
    /* UCNA error */
    else if (ser_support && !(status & MCi_STATUS_OVER)
                && (status & MCi_STATUS_EN) && !(status & MCi_STATUS_PCC)
                && !(status & MCi_STATUS_S) && !(status & MCi_STATUS_AR))
        return 1;
    return 0;
}

/* CMCI */
static DEFINE_SPINLOCK(cmci_discover_lock);

/*
 * Discover bank sharing using the algorithm recommended in the SDM.
 */
static int do_cmci_discover(int i)
{
    unsigned msr = MSR_IA32_MCx_CTL2(i);
    u64 val;

    rdmsrl(msr, val);
    /* Some other CPU already owns this bank. */
    if (val & CMCI_EN) {
        mcabanks_clear(i, __get_cpu_var(mce_banks_owned));
        goto out;
    }

    val &= ~CMCI_THRESHOLD_MASK;
    wrmsrl(msr, val | CMCI_EN | CMCI_THRESHOLD);
    rdmsrl(msr, val);

    if (!(val & CMCI_EN)) {
        /* This bank does not support CMCI. Polling timer has to handle it. */
        mcabanks_set(i, __get_cpu_var(no_cmci_banks));
        return 0;
    }
    mcabanks_set(i, __get_cpu_var(mce_banks_owned));
out:
    mcabanks_clear(i, __get_cpu_var(no_cmci_banks));
    return 1;
}

static void cmci_discover(void)
{
    unsigned long flags;
    int i;
    mctelem_cookie_t mctc;
    struct mca_summary bs;

    mce_printk(MCE_VERBOSE, "CMCI: find owner on CPU%d\n", smp_processor_id());

    spin_lock_irqsave(&cmci_discover_lock, flags);

    for (i = 0; i < nr_mce_banks; i++)
        if (!mcabanks_test(i, __get_cpu_var(mce_banks_owned)))
            do_cmci_discover(i);

    spin_unlock_irqrestore(&cmci_discover_lock, flags);

    /* In case CMCI happended when do owner change.
     * If CMCI happened yet not processed immediately,
     * MCi_status (error_count bit 38~52) is not cleared,
     * the CMCI interrupt will never be triggered again.
     */

    mctc = mcheck_mca_logout(
        MCA_CMCI_HANDLER, __get_cpu_var(mce_banks_owned), &bs, NULL);

    if (bs.errcnt && mctc != NULL) {
        if (dom0_vmce_enabled()) {
            mctelem_commit(mctc);
            send_global_virq(VIRQ_MCA);
        } else {
            x86_mcinfo_dump(mctelem_dataptr(mctc));
            mctelem_dismiss(mctc);
        }
    } else if (mctc != NULL)
        mctelem_dismiss(mctc);

    mce_printk(MCE_VERBOSE, "CMCI: CPU%d owner_map[%lx], no_cmci_map[%lx]\n",
           smp_processor_id(),
           *((unsigned long *)__get_cpu_var(mce_banks_owned)->bank_map),
           *((unsigned long *)__get_cpu_var(no_cmci_banks)->bank_map));
}

/*
 * Define an owner for each bank. Banks can be shared between CPUs
 * and to avoid reporting events multiple times always set up one
 * CPU as owner. 
 *
 * The assignment has to be redone when CPUs go offline and
 * any of the owners goes away. Also pollers run in parallel so we
 * have to be careful to update the banks in a way that doesn't
 * lose or duplicate events.
 */

static void mce_set_owner(void)
{
    if (!cmci_support || mce_disabled == 1)
        return;

    cmci_discover();
}

static void __cpu_mcheck_distribute_cmci(void *unused)
{
    cmci_discover();
}

static void cpu_mcheck_distribute_cmci(void)
{
    if (cmci_support && !mce_disabled)
        on_each_cpu(__cpu_mcheck_distribute_cmci, NULL, 0);
}

static void clear_cmci(void)
{
    int i;

    if (!cmci_support || mce_disabled == 1)
        return;

    mce_printk(MCE_VERBOSE, "CMCI: clear_cmci support on CPU%d\n",
            smp_processor_id());

    for (i = 0; i < nr_mce_banks; i++) {
        unsigned msr = MSR_IA32_MCx_CTL2(i);
        u64 val;
        if (!mcabanks_test(i, __get_cpu_var(mce_banks_owned)))
            continue;
        rdmsrl(msr, val);
        if (val & (CMCI_EN|CMCI_THRESHOLD_MASK))
            wrmsrl(msr, val & ~(CMCI_EN|CMCI_THRESHOLD_MASK));
        mcabanks_clear(i, __get_cpu_var(mce_banks_owned));
    }
}

static void cpu_mcheck_disable(void)
{
    clear_in_cr4(X86_CR4_MCE);

    if (cmci_support && !mce_disabled)
        clear_cmci();
}

static void cmci_interrupt(struct cpu_user_regs *regs)
{
    mctelem_cookie_t mctc;
    struct mca_summary bs;

    ack_APIC_irq();

    mctc = mcheck_mca_logout(
        MCA_CMCI_HANDLER, __get_cpu_var(mce_banks_owned), &bs, NULL);

    if (bs.errcnt && mctc != NULL) {
        if (dom0_vmce_enabled()) {
            mctelem_commit(mctc);
            mce_printk(MCE_VERBOSE, "CMCI: send CMCI to DOM0 through virq\n");
            send_global_virq(VIRQ_MCA);
        } else {
            x86_mcinfo_dump(mctelem_dataptr(mctc));
            mctelem_dismiss(mctc);
       }
    } else if (mctc != NULL)
        mctelem_dismiss(mctc);
}

static void intel_init_cmci(struct cpuinfo_x86 *c)
{
    u32 l, apic;
    int cpu = smp_processor_id();
    static uint8_t cmci_apic_vector;

    if (!mce_available(c) || !cmci_support) {
        if (opt_cpu_info)
            mce_printk(MCE_QUIET, "CMCI: CPU%d has no CMCI support\n", cpu);
        return;
    }

    apic = apic_read(APIC_CMCI);
    if ( apic & APIC_VECTOR_MASK )
    {
        mce_printk(MCE_QUIET, "CPU%d CMCI LVT vector (%#x) already installed\n",
            cpu, ( apic & APIC_VECTOR_MASK ));
        return;
    }

    alloc_direct_apic_vector(&cmci_apic_vector, cmci_interrupt);

    apic = cmci_apic_vector;
    apic |= (APIC_DM_FIXED | APIC_LVT_MASKED);
    apic_write_around(APIC_CMCI, apic);

    l = apic_read(APIC_CMCI);
    apic_write_around(APIC_CMCI, l & ~APIC_LVT_MASKED);

    mce_set_owner();
}

/* MCA */

static int mce_is_broadcast(struct cpuinfo_x86 *c)
{
    if (mce_force_broadcast)
        return 1;

    /* According to Intel SDM Dec, 2009, 15.10.4.1, For processors with
     * DisplayFamily_DisplayModel encoding of 06H_EH and above,
     * a MCA signal is broadcast to all logical processors in the system
     */
    if (c->x86_vendor == X86_VENDOR_INTEL && c->x86 == 6 &&
        c->x86_model >= 0xe)
            return 1;
    return 0;
}

/* Check and init MCA */
static void intel_init_mca(struct cpuinfo_x86 *c)
{
    bool_t broadcast, cmci = 0, ser = 0;
    int ext_num = 0, first;
    uint64_t msr_content;

    broadcast = mce_is_broadcast(c);

    rdmsrl(MSR_IA32_MCG_CAP, msr_content);

    if ((msr_content & MCG_CMCI_P) && cpu_has_apic)
        cmci = 1;

    /* Support Software Error Recovery */
    if (msr_content & MCG_SER_P)
        ser = 1;

    if (msr_content & MCG_EXT_P)
        ext_num = (msr_content >> MCG_EXT_CNT) & 0xff;

    first = mce_firstbank(c);

    if (smp_processor_id() == 0)
    {
        dprintk(XENLOG_INFO, "MCA Capability: BCAST %x SER %x"
                " CMCI %x firstbank %x extended MCE MSR %x\n",
                broadcast, ser, cmci, first, ext_num);

        mce_broadcast = broadcast;
        cmci_support = cmci;
        ser_support = ser;
        nr_intel_ext_msrs = ext_num;
        firstbank = first;
    }
    else if (cmci != cmci_support || ser != ser_support ||
             broadcast != mce_broadcast ||
             first != firstbank || ext_num != nr_intel_ext_msrs)
    {
        dprintk(XENLOG_WARNING,
                "CPU %u has different MCA capability (%x,%x,%x,%x,%x)"
                " than BSP, may cause undetermined result!!!\n",
                smp_processor_id(), broadcast, ser, cmci, first, ext_num);
    }
}

static void intel_mce_post_reset(void)
{
    mctelem_cookie_t mctc;
    struct mca_summary bs;

    mctc = mcheck_mca_logout(MCA_RESET, mca_allbanks, &bs, NULL);

    /* in the boot up stage, print out and also log in DOM0 boot process */
    if (bs.errcnt && mctc != NULL) {
        x86_mcinfo_dump(mctelem_dataptr(mctc));
        mctelem_commit(mctc);
    }
    return;
}

static void intel_init_mce(void)
{
    uint64_t msr_content;
    int i;

    intel_mce_post_reset();

    /* clear all banks */
    for (i = firstbank; i < nr_mce_banks; i++)
    {
        /* Some banks are shared across cores, use MCi_CTRL to judge whether
         * this bank has been initialized by other cores already. */
        rdmsrl(MSR_IA32_MCx_CTL(i), msr_content);
        if (!msr_content)
        {
            /* if ctl is 0, this bank is never initialized */
            mce_printk(MCE_VERBOSE, "mce_init: init bank%d\n", i);
            wrmsrl(MSR_IA32_MCx_CTL(i), 0xffffffffffffffffULL);
            wrmsrl(MSR_IA32_MCx_STATUS(i), 0x0ULL);
        }
    }
    if (firstbank) /* if cmci enabled, firstbank = 0 */
        wrmsrl(MSR_IA32_MC0_STATUS, 0x0ULL);

    x86_mce_vector_register(intel_machine_check);
    mce_recoverable_register(intel_recoverable_scan);
    mce_need_clearbank_register(intel_need_clearbank_scan);

    mce_dhandlers = intel_mce_dhandlers;
    mce_dhandler_num = ARRAY_SIZE(intel_mce_dhandlers);
    mce_uhandlers = intel_mce_uhandlers;
    mce_uhandler_num = ARRAY_SIZE(intel_mce_uhandlers);
}

static void cpu_mcabank_free(unsigned int cpu)
{
    struct mca_banks *mb1, *mb2, *mb3;

    mb1 = per_cpu(mce_clear_banks, cpu);
    mb2 = per_cpu(no_cmci_banks, cpu);
    mb3 = per_cpu(mce_banks_owned, cpu);

    mcabanks_free(mb1);
    mcabanks_free(mb2);
    mcabanks_free(mb3);
}

static int cpu_mcabank_alloc(unsigned int cpu)
{
    struct mca_banks *mb1, *mb2, *mb3;

    mb1 = mcabanks_alloc();
    mb2 = mcabanks_alloc();
    mb3 = mcabanks_alloc();
    if (!mb1 || !mb2 || !mb3)
        goto out;

    per_cpu(mce_clear_banks, cpu) = mb1;
    per_cpu(no_cmci_banks, cpu) = mb2;
    per_cpu(mce_banks_owned, cpu) = mb3;

    return 0;
out:
    mcabanks_free(mb1);
    mcabanks_free(mb2);
    mcabanks_free(mb3);
    return -ENOMEM;
}

static int cpu_callback(
    struct notifier_block *nfb, unsigned long action, void *hcpu)
{
    unsigned int cpu = (unsigned long)hcpu;
    int rc = 0;

    switch ( action )
    {
    case CPU_UP_PREPARE:
        rc = cpu_mcabank_alloc(cpu);
        break;
    case CPU_DYING:
        cpu_mcheck_disable();
        break;
    case CPU_UP_CANCELED:
    case CPU_DEAD:
        cpu_mcheck_distribute_cmci();
        cpu_mcabank_free(cpu);
        break;
    default:
        break;
    }

    return !rc ? NOTIFY_DONE : notifier_from_errno(rc);
}

static struct notifier_block cpu_nfb = {
    .notifier_call = cpu_callback
};

/* p4/p6 family have similar MCA initialization process */
enum mcheck_type intel_mcheck_init(struct cpuinfo_x86 *c, bool_t bsp)
{
    if ( bsp )
    {
        /* Early MCE initialisation for BSP. */
        if ( cpu_mcabank_alloc(0) )
            BUG();
        register_cpu_notifier(&cpu_nfb);
        mcheck_intel_therm_init();
    }

    intel_init_mca(c);

    mce_handler_init();

    intel_init_mce();

    intel_init_cmci(c);
#ifdef CONFIG_X86_MCE_THERMAL
    intel_init_thermal(c);
#endif

    return mcheck_intel;
}

/* intel specific MCA MSR */
int intel_mce_wrmsr(struct vcpu *v, uint32_t msr, uint64_t val)
{
    int ret = 0;

    if ( msr >= MSR_IA32_MC0_CTL2 &&
         msr < MSR_IA32_MCx_CTL2(v->arch.mcg_cap & MCG_CAP_COUNT) )
    {
        mce_printk(MCE_QUIET, "We have disabled CMCI capability, "
                 "Guest should not write this MSR!\n");
         ret = 1;
    }

    return ret;
}

int intel_mce_rdmsr(const struct vcpu *v, uint32_t msr, uint64_t *val)
{
    int ret = 0;

    if ( msr >= MSR_IA32_MC0_CTL2 &&
         msr < MSR_IA32_MCx_CTL2(v->arch.mcg_cap & MCG_CAP_COUNT) )
    {
        mce_printk(MCE_QUIET, "We have disabled CMCI capability, "
                 "Guest should not read this MSR!\n");
        ret = 1;
    }

    return ret;
}

