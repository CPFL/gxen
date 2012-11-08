/******************************************************************************
 * cpupool.c
 * 
 * Generic cpupool-handling functions.
 *
 * Cpupools are a feature to have configurable scheduling domains. Each
 * cpupool runs an own scheduler on a dedicated set of physical cpus.
 * A domain is bound to one cpupool at any time, but it can be moved to
 * another cpupool.
 *
 * (C) 2009, Juergen Gross, Fujitsu Technology Solutions
 */

#include <xen/lib.h>
#include <xen/init.h>
#include <xen/cpumask.h>
#include <xen/percpu.h>
#include <xen/sched.h>
#include <xen/sched-if.h>
#include <xen/cpu.h>

#define for_each_cpupool(ptr)    \
    for ((ptr) = &cpupool_list; *(ptr) != NULL; (ptr) = &((*(ptr))->next))

struct cpupool *cpupool0;                /* Initial cpupool with Dom0 */
cpumask_t cpupool_free_cpus;             /* cpus not in any cpupool */

static struct cpupool *cpupool_list;     /* linked list, sorted by poolid */

static int cpupool_moving_cpu = -1;
static struct cpupool *cpupool_cpu_moving = NULL;
static cpumask_t cpupool_locked_cpus;

static DEFINE_SPINLOCK(cpupool_lock);

DEFINE_PER_CPU(struct cpupool *, cpupool);

#define cpupool_dprintk(x...) ((void)0)

static struct cpupool *alloc_cpupool_struct(void)
{
    struct cpupool *c = xzalloc(struct cpupool);

    if ( c && zalloc_cpumask_var(&c->cpu_valid) )
        return c;
    xfree(c);
    return NULL;
}

static void free_cpupool_struct(struct cpupool *c)
{
    if ( c )
        free_cpumask_var(c->cpu_valid);
    xfree(c);
}

/*
 * find a cpupool by it's id. to be called with cpupool lock held
 * if exact is not specified, the first cpupool with an id larger or equal to
 * the searched id is returned
 * returns NULL if not found.
 */
static struct cpupool *__cpupool_find_by_id(int id, int exact)
{
    struct cpupool **q;

    ASSERT(spin_is_locked(&cpupool_lock));

    for_each_cpupool(q)
        if ( (*q)->cpupool_id >= id )
            break;

    return (!exact || (*q == NULL) || ((*q)->cpupool_id == id)) ? *q : NULL;
}

static struct cpupool *cpupool_find_by_id(int poolid)
{
    return __cpupool_find_by_id(poolid, 1);
}

static struct cpupool *__cpupool_get_by_id(int poolid, int exact)
{
    struct cpupool *c;
    spin_lock(&cpupool_lock);
    c = __cpupool_find_by_id(poolid, exact);
    if ( c != NULL )
        atomic_inc(&c->refcnt);
    spin_unlock(&cpupool_lock);
    return c;
}

struct cpupool *cpupool_get_by_id(int poolid)
{
    return __cpupool_get_by_id(poolid, 1);
}

static struct cpupool *cpupool_get_next_by_id(int poolid)
{
    return __cpupool_get_by_id(poolid, 0);
}

void cpupool_put(struct cpupool *pool)
{
    if ( !atomic_dec_and_test(&pool->refcnt) )
        return;
    scheduler_free(pool->sched);
    free_cpupool_struct(pool);
}

/*
 * create a new cpupool with specified poolid and scheduler
 * returns pointer to new cpupool structure if okay, NULL else
 * possible failures:
 * - no memory
 * - poolid already used
 * - unknown scheduler
 */
static struct cpupool *cpupool_create(
    int poolid, unsigned int sched_id, int *perr)
{
    struct cpupool *c;
    struct cpupool **q;
    int last = 0;

    *perr = -ENOMEM;
    if ( (c = alloc_cpupool_struct()) == NULL )
        return NULL;

    /* One reference for caller, one reference for cpupool_destroy(). */
    atomic_set(&c->refcnt, 2);

    cpupool_dprintk("cpupool_create(pool=%d,sched=%u)\n", poolid, sched_id);

    spin_lock(&cpupool_lock);

    for_each_cpupool(q)
    {
        last = (*q)->cpupool_id;
        if ( (poolid != CPUPOOLID_NONE) && (last >= poolid) )
            break;
    }
    if ( *q != NULL )
    {
        if ( (*q)->cpupool_id == poolid )
        {
            spin_unlock(&cpupool_lock);
            free_cpupool_struct(c);
            *perr = -EEXIST;
            return NULL;
        }
        c->next = *q;
    }

    c->cpupool_id = (poolid == CPUPOOLID_NONE) ? (last + 1) : poolid;
    if ( poolid == 0 )
    {
        c->sched = scheduler_get_default();
    }
    else
    {
        c->sched = scheduler_alloc(sched_id, perr);
        if ( c->sched == NULL )
        {
            spin_unlock(&cpupool_lock);
            free_cpupool_struct(c);
            return NULL;
        }
    }

    *q = c;

    spin_unlock(&cpupool_lock);

    cpupool_dprintk("Created cpupool %d with scheduler %s (%s)\n",
                    c->cpupool_id, c->sched->name, c->sched->opt_name);

    *perr = 0;
    return c;
}
/*
 * destroys the given cpupool
 * returns 0 on success, 1 else
 * possible failures:
 * - pool still in use
 * - cpus still assigned to pool
 * - pool not in list
 */
static int cpupool_destroy(struct cpupool *c)
{
    struct cpupool **q;

    spin_lock(&cpupool_lock);
    for_each_cpupool(q)
        if ( *q == c )
            break;
    if ( *q != c )
    {
        spin_unlock(&cpupool_lock);
        return -ENOENT;
    }
    if ( (c->n_dom != 0) || cpumask_weight(c->cpu_valid) )
    {
        spin_unlock(&cpupool_lock);
        return -EBUSY;
    }
    *q = c->next;
    spin_unlock(&cpupool_lock);

    cpupool_put(c);

    cpupool_dprintk("cpupool_destroy(pool=%d)\n", c->cpupool_id);
    return 0;
}

/*
 * assign a specific cpu to a cpupool
 * cpupool_lock must be held
 */
static int cpupool_assign_cpu_locked(struct cpupool *c, unsigned int cpu)
{
    int ret;
    struct cpupool *old;
    struct domain *d;

    if ( (cpupool_moving_cpu == cpu) && (c != cpupool_cpu_moving) )
        return -EBUSY;
    old = per_cpu(cpupool, cpu);
    per_cpu(cpupool, cpu) = c;
    ret = schedule_cpu_switch(cpu, c);
    if ( ret )
    {
        per_cpu(cpupool, cpu) = old;
        return ret;
    }

    cpumask_clear_cpu(cpu, &cpupool_free_cpus);
    if (cpupool_moving_cpu == cpu)
    {
        cpupool_moving_cpu = -1;
        cpupool_put(cpupool_cpu_moving);
        cpupool_cpu_moving = NULL;
    }
    cpumask_set_cpu(cpu, c->cpu_valid);

    rcu_read_lock(&domlist_read_lock);
    for_each_domain_in_cpupool(d, c)
    {
        domain_update_node_affinity(d);
    }
    rcu_read_unlock(&domlist_read_lock);

    return 0;
}

static long cpupool_unassign_cpu_helper(void *info)
{
    int cpu = cpupool_moving_cpu;
    long ret;

    cpupool_dprintk("cpupool_unassign_cpu(pool=%d,cpu=%d)\n",
                    cpupool_cpu_moving->cpupool_id, cpu);

    spin_lock(&cpupool_lock);
    ret = cpu_disable_scheduler(cpu);
    cpumask_set_cpu(cpu, &cpupool_free_cpus);
    if ( !ret )
    {
        ret = schedule_cpu_switch(cpu, NULL);
        if ( ret )
        {
            cpumask_clear_cpu(cpu, &cpupool_free_cpus);
            goto out;
        }
        per_cpu(cpupool, cpu) = NULL;
        cpupool_moving_cpu = -1;
        cpupool_put(cpupool_cpu_moving);
        cpupool_cpu_moving = NULL;
    }

out:
    spin_unlock(&cpupool_lock);
    cpupool_dprintk("cpupool_unassign_cpu ret=%ld\n", ret);
    return ret;
}

/*
 * unassign a specific cpu from a cpupool
 * we must be sure not to run on the cpu to be unassigned! to achieve this
 * the main functionality is performed via continue_hypercall_on_cpu on a
 * specific cpu.
 * if the cpu to be removed is the last one of the cpupool no active domain
 * must be bound to the cpupool. dying domains are moved to cpupool0 as they
 * might be zombies.
 * possible failures:
 * - last cpu and still active domains in cpupool
 * - cpu just being unplugged
 */
int cpupool_unassign_cpu(struct cpupool *c, unsigned int cpu)
{
    int work_cpu;
    int ret;
    struct domain *d;

    cpupool_dprintk("cpupool_unassign_cpu(pool=%d,cpu=%d)\n",
                    c->cpupool_id, cpu);

    spin_lock(&cpupool_lock);
    ret = -EBUSY;
    if ( (cpupool_moving_cpu != -1) && (cpu != cpupool_moving_cpu) )
        goto out;
    if ( cpumask_test_cpu(cpu, &cpupool_locked_cpus) )
        goto out;

    ret = 0;
    if ( !cpumask_test_cpu(cpu, c->cpu_valid) && (cpu != cpupool_moving_cpu) )
        goto out;

    if ( (c->n_dom > 0) && (cpumask_weight(c->cpu_valid) == 1) &&
         (cpu != cpupool_moving_cpu) )
    {
        rcu_read_lock(&domlist_read_lock);
        for_each_domain_in_cpupool(d, c)
        {
            if ( !d->is_dying )
            {
                ret = -EBUSY;
                break;
            }
            c->n_dom--;
            ret = sched_move_domain(d, cpupool0);
            if ( ret )
            {
                c->n_dom++;
                break;
            }
            cpupool0->n_dom++;
        }
        rcu_read_unlock(&domlist_read_lock);
        if ( ret )
            goto out;
    }
    cpupool_moving_cpu = cpu;
    atomic_inc(&c->refcnt);
    cpupool_cpu_moving = c;
    cpumask_clear_cpu(cpu, c->cpu_valid);
    spin_unlock(&cpupool_lock);

    work_cpu = smp_processor_id();
    if ( work_cpu == cpu )
    {
        work_cpu = cpumask_first(cpupool0->cpu_valid);
        if ( work_cpu == cpu )
            work_cpu = cpumask_next(cpu, cpupool0->cpu_valid);
    }
    return continue_hypercall_on_cpu(work_cpu, cpupool_unassign_cpu_helper, c);

out:
    spin_unlock(&cpupool_lock);
    cpupool_dprintk("cpupool_unassign_cpu(pool=%d,cpu=%d) ret %d\n",
                    c->cpupool_id, cpu, ret);
    return ret;
}

/*
 * add a new domain to a cpupool
 * possible failures:
 * - pool does not exist
 * - no cpu assigned to pool
 */
int cpupool_add_domain(struct domain *d, int poolid)
{
    struct cpupool *c;
    int rc;
    int n_dom = 0;

    if ( poolid == CPUPOOLID_NONE )
        return 0;
    spin_lock(&cpupool_lock);
    c = cpupool_find_by_id(poolid);
    if ( c == NULL )
        rc = -ESRCH;
    else if ( !cpumask_weight(c->cpu_valid) )
        rc = -ENODEV;
    else
    {
        c->n_dom++;
        n_dom = c->n_dom;
        d->cpupool = c;
        rc = 0;
    }
    spin_unlock(&cpupool_lock);
    cpupool_dprintk("cpupool_add_domain(dom=%d,pool=%d) n_dom %d rc %d\n",
                    d->domain_id, poolid, n_dom, rc);
    return rc;
}

/*
 * remove a domain from a cpupool
 */
void cpupool_rm_domain(struct domain *d)
{
    int cpupool_id;
    int n_dom;

    if ( d->cpupool == NULL )
        return;
    spin_lock(&cpupool_lock);
    cpupool_id = d->cpupool->cpupool_id;
    d->cpupool->n_dom--;
    n_dom = d->cpupool->n_dom;
    d->cpupool = NULL;
    spin_unlock(&cpupool_lock);
    cpupool_dprintk("cpupool_rm_domain(dom=%d,pool=%d) n_dom %d\n",
                    d->domain_id, cpupool_id, n_dom);
    return;
}

/*
 * called to add a new cpu to pool admin
 * we add a hotplugged cpu to the cpupool0 to be able to add it to dom0
 */
static void cpupool_cpu_add(unsigned int cpu)
{
    spin_lock(&cpupool_lock);
    cpumask_clear_cpu(cpu, &cpupool_locked_cpus);
    cpumask_set_cpu(cpu, &cpupool_free_cpus);
    cpupool_assign_cpu_locked(cpupool0, cpu);
    spin_unlock(&cpupool_lock);
}

/*
 * called to remove a cpu from pool admin
 * the cpu to be removed is locked to avoid removing it from dom0
 * returns failure if not in pool0
 */
static int cpupool_cpu_remove(unsigned int cpu)
{
    int ret = 0;
	
    spin_lock(&cpupool_lock);
    if ( !cpumask_test_cpu(cpu, cpupool0->cpu_valid))
        ret = -EBUSY;
    else
        cpumask_set_cpu(cpu, &cpupool_locked_cpus);
    spin_unlock(&cpupool_lock);

    return ret;
}

/*
 * do cpupool related sysctl operations
 */
int cpupool_do_sysctl(struct xen_sysctl_cpupool_op *op)
{
    int ret;
    struct cpupool *c;

    switch ( op->op )
    {

    case XEN_SYSCTL_CPUPOOL_OP_CREATE:
    {
        int poolid;

        poolid = (op->cpupool_id == XEN_SYSCTL_CPUPOOL_PAR_ANY) ?
            CPUPOOLID_NONE: op->cpupool_id;
        c = cpupool_create(poolid, op->sched_id, &ret);
        if ( c != NULL )
        {
            op->cpupool_id = c->cpupool_id;
            cpupool_put(c);
        }
    }
    break;

    case XEN_SYSCTL_CPUPOOL_OP_DESTROY:
    {
        c = cpupool_get_by_id(op->cpupool_id);
        ret = -ENOENT;
        if ( c == NULL )
            break;
        ret = cpupool_destroy(c);
        cpupool_put(c);
    }
    break;

    case XEN_SYSCTL_CPUPOOL_OP_INFO:
    {
        c = cpupool_get_next_by_id(op->cpupool_id);
        ret = -ENOENT;
        if ( c == NULL )
            break;
        op->cpupool_id = c->cpupool_id;
        op->sched_id = c->sched->sched_id;
        op->n_dom = c->n_dom;
        ret = cpumask_to_xenctl_cpumap(&op->cpumap, c->cpu_valid);
        cpupool_put(c);
    }
    break;

    case XEN_SYSCTL_CPUPOOL_OP_ADDCPU:
    {
        unsigned cpu;

        cpu = op->cpu;
        cpupool_dprintk("cpupool_assign_cpu(pool=%d,cpu=%d)\n",
                        op->cpupool_id, cpu);
        spin_lock(&cpupool_lock);
        if ( cpu == XEN_SYSCTL_CPUPOOL_PAR_ANY )
            cpu = cpumask_first(&cpupool_free_cpus);
        ret = -EINVAL;
        if ( cpu >= nr_cpu_ids )
            goto addcpu_out;
        ret = -EBUSY;
        if ( !cpumask_test_cpu(cpu, &cpupool_free_cpus) )
            goto addcpu_out;
        c = cpupool_find_by_id(op->cpupool_id);
        ret = -ENOENT;
        if ( c == NULL )
            goto addcpu_out;
        ret = cpupool_assign_cpu_locked(c, cpu);
    addcpu_out:
        spin_unlock(&cpupool_lock);
        cpupool_dprintk("cpupool_assign_cpu(pool=%d,cpu=%d) ret %d\n",
                        op->cpupool_id, cpu, ret);
    }
    break;

    case XEN_SYSCTL_CPUPOOL_OP_RMCPU:
    {
        unsigned cpu;

        c = cpupool_get_by_id(op->cpupool_id);
        ret = -ENOENT;
        if ( c == NULL )
            break;
        cpu = op->cpu;
        if ( cpu == XEN_SYSCTL_CPUPOOL_PAR_ANY )
            cpu = cpumask_last(c->cpu_valid);
        ret = (cpu < nr_cpu_ids) ? cpupool_unassign_cpu(c, cpu) : -EINVAL;
        cpupool_put(c);
    }
    break;

    case XEN_SYSCTL_CPUPOOL_OP_MOVEDOMAIN:
    {
        struct domain *d;

        ret = -EINVAL;
        if ( op->domid == 0 )
            break;
        ret = -ESRCH;
        d = rcu_lock_domain_by_id(op->domid);
        if ( d == NULL )
            break;
        if ( d->cpupool == NULL )
        {
            ret = -EINVAL;
            rcu_unlock_domain(d);
            break;
        }
        if ( op->cpupool_id == d->cpupool->cpupool_id )
        {
            ret = 0;
            rcu_unlock_domain(d);
            break;
        }
        cpupool_dprintk("cpupool move_domain(dom=%d)->pool=%d\n",
                        d->domain_id, op->cpupool_id);
        ret = -ENOENT;
        spin_lock(&cpupool_lock);
        c = cpupool_find_by_id(op->cpupool_id);
        if ( (c != NULL) && cpumask_weight(c->cpu_valid) )
        {
            d->cpupool->n_dom--;
            ret = sched_move_domain(d, c);
            if ( ret )
                d->cpupool->n_dom++;
            else
                c->n_dom++;
        }
        spin_unlock(&cpupool_lock);
        cpupool_dprintk("cpupool move_domain(dom=%d)->pool=%d ret %d\n",
                        d->domain_id, op->cpupool_id, ret);
        rcu_unlock_domain(d);
    }
    break;

    case XEN_SYSCTL_CPUPOOL_OP_FREEINFO:
    {
        ret = cpumask_to_xenctl_cpumap(
            &op->cpumap, &cpupool_free_cpus);
    }
    break;

    default:
        ret = -ENOSYS;
        break;
    }

    return ret;
}

void dump_runq(unsigned char key)
{
    unsigned long    flags;
    s_time_t         now = NOW();
    struct cpupool **c;

    spin_lock(&cpupool_lock);
    local_irq_save(flags);

    printk("sched_smt_power_savings: %s\n",
            sched_smt_power_savings? "enabled":"disabled");
    printk("NOW=0x%08X%08X\n",  (u32)(now>>32), (u32)now);

    printk("Idle cpupool:\n");
    schedule_dump(NULL);

    for_each_cpupool(c)
    {
        printk("Cpupool %d:\n", (*c)->cpupool_id);
        schedule_dump(*c);
    }

    local_irq_restore(flags);
    spin_unlock(&cpupool_lock);
}

static int cpu_callback(
    struct notifier_block *nfb, unsigned long action, void *hcpu)
{
    unsigned int cpu = (unsigned long)hcpu;
    int rc = 0;

    if ( (system_state == SYS_STATE_suspend) ||
         (system_state == SYS_STATE_resume) )
        goto out;

    switch ( action )
    {
    case CPU_DOWN_FAILED:
    case CPU_ONLINE:
        cpupool_cpu_add(cpu);
        break;
    case CPU_DOWN_PREPARE:
        rc = cpupool_cpu_remove(cpu);
        break;
    default:
        break;
    }

out:
    return !rc ? NOTIFY_DONE : notifier_from_errno(rc);
}

static struct notifier_block cpu_nfb = {
    .notifier_call = cpu_callback
};

static int __init cpupool_presmp_init(void)
{
    int err;
    void *cpu = (void *)(long)smp_processor_id();
    cpupool0 = cpupool_create(0, 0, &err);
    BUG_ON(cpupool0 == NULL);
    cpupool_put(cpupool0);
    cpu_callback(&cpu_nfb, CPU_ONLINE, cpu);
    register_cpu_notifier(&cpu_nfb);
    return 0;
}
presmp_initcall(cpupool_presmp_init);

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
