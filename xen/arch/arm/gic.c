/*
 * xen/arch/arm/gic.c
 *
 * ARM Generic Interrupt Controller support
 *
 * Tim Deegan <tim@xen.org>
 * Copyright (c) 2011 Citrix Systems.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <xen/config.h>
#include <xen/lib.h>
#include <xen/init.h>
#include <xen/mm.h>
#include <xen/irq.h>
#include <xen/sched.h>
#include <xen/errno.h>
#include <xen/softirq.h>
#include <xen/list.h>
#include <asm/p2m.h>
#include <asm/domain.h>

#include "gic.h"

/* Access to the GIC Distributor registers through the fixmap */
#define GICD ((volatile uint32_t *) FIXMAP_ADDR(FIXMAP_GICD))
#define GICC ((volatile uint32_t *) (FIXMAP_ADDR(FIXMAP_GICC1)  \
                                     + (GIC_CR_OFFSET & 0xfff)))
#define GICH ((volatile uint32_t *) (FIXMAP_ADDR(FIXMAP_GICH)  \
                                     + (GIC_HR_OFFSET & 0xfff)))
static void gic_restore_pending_irqs(struct vcpu *v);

/* Global state */
static struct {
    paddr_t dbase;       /* Address of distributor registers */
    paddr_t cbase;       /* Address of CPU interface registers */
    paddr_t hbase;       /* Address of virtual interface registers */
    unsigned int lines;
    unsigned int cpus;
    spinlock_t lock;
    uint64_t lr_mask;
} gic;

irq_desc_t irq_desc[NR_IRQS];
unsigned nr_lrs;

void gic_save_state(struct vcpu *v)
{
    int i;

    for ( i=0; i<nr_lrs; i++)
        v->arch.gic_lr[i] = GICH[GICH_LR + i];
    v->arch.lr_mask = gic.lr_mask;
    /* Disable until next VCPU scheduled */
    GICH[GICH_HCR] = 0;
    isb();
}

void gic_restore_state(struct vcpu *v)
{
    int i;

    if ( is_idle_vcpu(v) )
        return;

    gic.lr_mask = v->arch.lr_mask;
    for ( i=0; i<nr_lrs; i++)
        GICH[GICH_LR + i] = v->arch.gic_lr[i];
    GICH[GICH_HCR] = GICH_HCR_EN;
    isb();

    gic_restore_pending_irqs(v);
}

static unsigned int gic_irq_startup(struct irq_desc *desc)
{
    uint32_t enabler;
    int irq = desc->irq;

    /* Enable routing */
    enabler = GICD[GICD_ISENABLER + irq / 32];
    GICD[GICD_ISENABLER + irq / 32] = enabler | (1u << (irq % 32));

    return 0;
}

static void gic_irq_shutdown(struct irq_desc *desc)
{
    uint32_t enabler;
    int irq = desc->irq;

    /* Disable routing */
    enabler = GICD[GICD_ICENABLER + irq / 32];
    GICD[GICD_ICENABLER + irq / 32] = enabler | (1u << (irq % 32));
}

static void gic_irq_enable(struct irq_desc *desc)
{

}

static void gic_irq_disable(struct irq_desc *desc)
{

}

static void gic_irq_ack(struct irq_desc *desc)
{
    /* No ACK -- reading IAR has done this for us */
}

static void gic_host_irq_end(struct irq_desc *desc)
{
    int irq = desc->irq;
    /* Lower the priority */
    GICC[GICC_EOIR] = irq;
    /* Deactivate */
    GICC[GICC_DIR] = irq;
}

static void gic_guest_irq_end(struct irq_desc *desc)
{
    int irq = desc->irq;
    /* Lower the priority of the IRQ */
    GICC[GICC_EOIR] = irq;
    /* Deactivation happens in maintenance interrupt / via GICV */
}

static void gic_irq_set_affinity(struct irq_desc *desc, const cpumask_t *mask)
{
    BUG();
}

/* XXX different for level vs edge */
static hw_irq_controller gic_host_irq_type = {
    .typename = "gic",
    .startup = gic_irq_startup,
    .shutdown = gic_irq_shutdown,
    .enable = gic_irq_enable,
    .disable = gic_irq_disable,
    .ack = gic_irq_ack,
    .end = gic_host_irq_end,
    .set_affinity = gic_irq_set_affinity,
};
static hw_irq_controller gic_guest_irq_type = {
    .typename = "gic",
    .startup = gic_irq_startup,
    .shutdown = gic_irq_shutdown,
    .enable = gic_irq_enable,
    .disable = gic_irq_disable,
    .ack = gic_irq_ack,
    .end = gic_guest_irq_end,
    .set_affinity = gic_irq_set_affinity,
};

/* Program the GIC to route an interrupt */
static int gic_route_irq(unsigned int irq, bool_t level,
                         unsigned int cpu_mask, unsigned int priority)
{
    volatile unsigned char *bytereg;
    uint32_t cfg, edgebit;
    struct irq_desc *desc = irq_to_desc(irq);
    unsigned long flags;

    ASSERT(!(cpu_mask & ~0xff));  /* Targets bitmap only supports 8 CPUs */
    ASSERT(priority <= 0xff);     /* Only 8 bits of priority */
    ASSERT(irq < gic.lines + 32); /* Can't route interrupts that don't exist */

    spin_lock_irqsave(&desc->lock, flags);
    spin_lock(&gic.lock);

    if ( desc->action != NULL )
    {
        spin_unlock(&gic.lock);
        spin_unlock(&desc->lock);
        return -EBUSY;
    }

    desc->handler = &gic_host_irq_type;

    /* Disable interrupt */
    desc->handler->shutdown(desc);

    /* Set edge / level */
    cfg = GICD[GICD_ICFGR + irq / 16];
    edgebit = 2u << (2 * (irq % 16));
    if ( level )
        cfg &= ~edgebit;
    else
        cfg |= edgebit;
    GICD[GICD_ICFGR + irq / 16] = cfg;

    /* Set target CPU mask (RAZ/WI on uniprocessor) */
    bytereg = (unsigned char *) (GICD + GICD_ITARGETSR);
    bytereg[irq] = cpu_mask;

    /* Set priority */
    bytereg = (unsigned char *) (GICD + GICD_IPRIORITYR);
    bytereg[irq] = priority;

    spin_unlock(&gic.lock);
    spin_unlock_irqrestore(&desc->lock, flags);
    return 0;
}

static void __init gic_dist_init(void)
{
    uint32_t type;
    uint32_t cpumask = 1 << smp_processor_id();
    int i;

    cpumask |= cpumask << 8;
    cpumask |= cpumask << 16;

    /* Disable the distributor */
    GICD[GICD_CTLR] = 0;

    type = GICD[GICD_TYPER];
    gic.lines = 32 * (type & GICD_TYPE_LINES);
    gic.cpus = 1 + ((type & GICD_TYPE_CPUS) >> 5);
    printk("GIC: %d lines, %d cpu%s%s (IID %8.8x).\n",
           gic.lines, gic.cpus, (gic.cpus == 1) ? "" : "s",
           (type & GICD_TYPE_SEC) ? ", secure" : "",
           GICD[GICD_IIDR]);

    /* Default all global IRQs to level, active low */
    for ( i = 32; i < gic.lines; i += 16 )
        GICD[GICD_ICFGR + i / 16] = 0x0;

    /* Route all global IRQs to this CPU */
    for ( i = 32; i < gic.lines; i += 4 )
        GICD[GICD_ICFGR + i / 4] = cpumask;

    /* Default priority for global interrupts */
    for ( i = 32; i < gic.lines; i += 4 )
        GICD[GICD_IPRIORITYR + i / 4] = 0xa0a0a0a0;

    /* Disable all global interrupts */
    for ( i = 32; i < gic.lines; i += 32 )
        GICD[GICD_ICENABLER + i / 32] = ~0ul;

    /* Turn on the distributor */
    GICD[GICD_CTLR] = GICD_CTL_ENABLE;
}

static void __cpuinit gic_cpu_init(void)
{
    int i;

    /* The first 32 interrupts (PPI and SGI) are banked per-cpu, so 
     * even though they are controlled with GICD registers, they must 
     * be set up here with the other per-cpu state. */
    GICD[GICD_ICENABLER] = 0xffff0000; /* Disable all PPI */
    GICD[GICD_ISENABLER] = 0x0000ffff; /* Enable all SGI */
    /* Set PPI and SGI priorities */
    for (i = 0; i < 32; i += 4)
        GICD[GICD_IPRIORITYR + i / 4] = 0xa0a0a0a0;

    /* Local settings: interface controller */
    GICC[GICC_PMR] = 0xff;                /* Don't mask by priority */
    GICC[GICC_BPR] = 0;                   /* Finest granularity of priority */
    GICC[GICC_CTLR] = GICC_CTL_ENABLE|GICC_CTL_EOI;    /* Turn on delivery */
}

static void gic_cpu_disable(void)
{
    GICC[GICC_CTLR] = 0;
}

static void __cpuinit gic_hyp_init(void)
{
    uint32_t vtr;

    vtr = GICH[GICH_VTR];
    nr_lrs  = (vtr & GICH_VTR_NRLRGS) + 1;

    GICH[GICH_MISR] = GICH_MISR_EOI;
}

static void __cpuinit gic_hyp_disable(void)
{
    GICH[GICH_HCR] = 0;
}

/* Set up the GIC */
int __init gic_init(void)
{
    /* XXX FIXME get this from devicetree */
    gic.dbase = GIC_BASE_ADDRESS + GIC_DR_OFFSET;
    gic.cbase = GIC_BASE_ADDRESS + GIC_CR_OFFSET;
    gic.hbase = GIC_BASE_ADDRESS + GIC_HR_OFFSET;
    set_fixmap(FIXMAP_GICD, gic.dbase >> PAGE_SHIFT, DEV_SHARED);
    BUILD_BUG_ON(FIXMAP_ADDR(FIXMAP_GICC1) !=
                 FIXMAP_ADDR(FIXMAP_GICC2)-PAGE_SIZE);
    set_fixmap(FIXMAP_GICC1, gic.cbase >> PAGE_SHIFT, DEV_SHARED);
    set_fixmap(FIXMAP_GICC2, (gic.cbase >> PAGE_SHIFT) + 1, DEV_SHARED);
    set_fixmap(FIXMAP_GICH, gic.hbase >> PAGE_SHIFT, DEV_SHARED);

    /* Global settings: interrupt distributor */
    spin_lock_init(&gic.lock);
    spin_lock(&gic.lock);

    gic_dist_init();
    gic_cpu_init();
    gic_hyp_init();

    gic.lr_mask = 0ULL;

    spin_unlock(&gic.lock);

    return gic.cpus;
}

/* Set up the per-CPU parts of the GIC for a secondary CPU */
void __cpuinit gic_init_secondary_cpu(void)
{
    spin_lock(&gic.lock);
    gic_cpu_init();
    gic_hyp_init();
    spin_unlock(&gic.lock);
}

/* Shut down the per-CPU GIC interface */
void gic_disable_cpu(void)
{
    spin_lock_irq(&gic.lock);
    gic_cpu_disable();
    gic_hyp_disable();
    spin_unlock_irq(&gic.lock);
}

void gic_route_irqs(void)
{
    /* XXX should get these from DT */
    /* GIC maintenance */
    gic_route_irq(25, 1, 1u << smp_processor_id(), 0xa0);
    /* Hypervisor Timer */
    gic_route_irq(26, 1, 1u << smp_processor_id(), 0xa0);
    /* Timer */
    gic_route_irq(30, 1, 1u << smp_processor_id(), 0xa0);
    /* UART */
    gic_route_irq(37, 0, 1u << smp_processor_id(), 0xa0);
}

void __init release_irq(unsigned int irq)
{
    struct irq_desc *desc;
    unsigned long flags;
   struct irqaction *action;

    desc = irq_to_desc(irq);

    spin_lock_irqsave(&desc->lock,flags);
    action = desc->action;
    desc->action  = NULL;
    desc->status |= IRQ_DISABLED;

    spin_lock(&gic.lock);
    desc->handler->shutdown(desc);
    spin_unlock(&gic.lock);

    spin_unlock_irqrestore(&desc->lock,flags);

    /* Wait to make sure it's not being used on another CPU */
    do { smp_mb(); } while ( desc->status & IRQ_INPROGRESS );

    if (action && action->free_on_release)
        xfree(action);
}

static int __setup_irq(struct irq_desc *desc, unsigned int irq,
                       struct irqaction *new)
{
    if ( desc->action != NULL )
        return -EBUSY;

    desc->action  = new;
    desc->status &= ~IRQ_DISABLED;
    dsb();

    desc->handler->startup(desc);

    return 0;
}

int __init setup_irq(unsigned int irq, struct irqaction *new)
{
    int rc;
    unsigned long flags;
    struct irq_desc *desc;

    desc = irq_to_desc(irq);

    spin_lock_irqsave(&desc->lock, flags);

    rc = __setup_irq(desc, irq, new);

    spin_unlock_irqrestore(&desc->lock,flags);

    return rc;
}

static inline void gic_set_lr(int lr, unsigned int virtual_irq,
        unsigned int state, unsigned int priority)
{
    int maintenance_int = GICH_LR_MAINTENANCE_IRQ;

    BUG_ON(lr > nr_lrs);

    GICH[GICH_LR + lr] = state |
        maintenance_int |
        ((priority >> 3) << GICH_LR_PRIORITY_SHIFT) |
        ((virtual_irq & GICH_LR_VIRTUAL_MASK) << GICH_LR_VIRTUAL_SHIFT);
}

void gic_set_guest_irq(struct vcpu *v, unsigned int virtual_irq,
        unsigned int state, unsigned int priority)
{
    int i;
    struct pending_irq *iter, *n;

    spin_lock_irq(&gic.lock);

    if ( v->is_running && list_empty(&v->arch.vgic.lr_pending) )
    {
        i = find_first_zero_bit(&gic.lr_mask, nr_lrs);
        if (i < nr_lrs) {
            set_bit(i, &gic.lr_mask);
            gic_set_lr(i, virtual_irq, state, priority);
            goto out;
        }
    }

    n = irq_to_pending(v, virtual_irq);
    if ( !list_empty(&n->lr_queue) )
        goto out;

    list_for_each_entry ( iter, &v->arch.vgic.lr_pending, lr_queue )
    {
        if ( iter->priority > priority )
        {
            list_add_tail(&n->lr_queue, &iter->lr_queue);
            goto out;
        }
    }
    list_add_tail(&n->lr_queue, &v->arch.vgic.lr_pending);

out:
    spin_unlock_irq(&gic.lock);
    return;
}

static void gic_restore_pending_irqs(struct vcpu *v)
{
    int i;
    struct pending_irq *p, *t;

    list_for_each_entry_safe ( p, t, &v->arch.vgic.lr_pending, lr_queue )
    {
        i = find_first_zero_bit(&gic.lr_mask, nr_lrs);
        if ( i >= nr_lrs ) return;

        gic_set_lr(i, p->irq, GICH_LR_PENDING, p->priority);
        list_del_init(&p->lr_queue);
        set_bit(i, &gic.lr_mask);
    }

}

static void gic_inject_irq_start(void)
{
    uint32_t hcr;
    hcr = READ_CP32(HCR);
    WRITE_CP32(hcr | HCR_VI, HCR);
    isb();
}

static void gic_inject_irq_stop(void)
{
    uint32_t hcr;
    hcr = READ_CP32(HCR);
    if (hcr & HCR_VI) {
        WRITE_CP32(hcr & ~HCR_VI, HCR);
        isb();
    }
}

void gic_inject(void)
{
    if (!gic.lr_mask)
        gic_inject_irq_stop();
    else
        gic_inject_irq_start();
}

int gic_route_irq_to_guest(struct domain *d, unsigned int irq,
                           const char * devname)
{
    struct irqaction *action;
    struct irq_desc *desc = irq_to_desc(irq);
    unsigned long flags;
    int retval;

    action = xmalloc(struct irqaction);
    if (!action)
        return -ENOMEM;

    action->dev_id = d;
    action->name = devname;

    spin_lock_irqsave(&desc->lock, flags);

    desc->handler = &gic_guest_irq_type;
    desc->status |= IRQ_GUEST;

    retval = __setup_irq(desc, irq, action);
    if (retval) {
        xfree(action);
        goto out;
    }

out:
    spin_unlock_irqrestore(&desc->lock, flags);
    return retval;
}

/* Accept an interrupt from the GIC and dispatch its handler */
void gic_interrupt(struct cpu_user_regs *regs, int is_fiq)
{
    uint32_t intack = GICC[GICC_IAR];
    unsigned int irq = intack & GICC_IA_IRQ;

    local_irq_enable();

    if ( irq == 1023 )
        /* Spurious interrupt */
        return;

    do_IRQ(regs, irq, is_fiq);
}

int gicv_setup(struct domain *d)
{
    /* map the gic virtual cpu interface in the gic cpu interface region of
     * the guest */
    return map_mmio_regions(d, GIC_BASE_ADDRESS + GIC_CR_OFFSET,
                        GIC_BASE_ADDRESS + GIC_CR_OFFSET + (2 * PAGE_SIZE) - 1,
                        GIC_BASE_ADDRESS + GIC_VR_OFFSET);
}

static void maintenance_interrupt(int irq, void *dev_id, struct cpu_user_regs *regs)
{
    int i = 0, virq;
    uint32_t lr;
    struct vcpu *v = current;
    uint64_t eisr = GICH[GICH_EISR0] | (((uint64_t) GICH[GICH_EISR1]) << 32);

    while ((i = find_next_bit((const long unsigned int *) &eisr,
                              64, i)) < 64) {
        struct pending_irq *p;

        spin_lock_irq(&gic.lock);
        lr = GICH[GICH_LR + i];
        virq = lr & GICH_LR_VIRTUAL_MASK;
        GICH[GICH_LR + i] = 0;
        clear_bit(i, &gic.lr_mask);

        if ( !list_empty(&v->arch.vgic.lr_pending) ) {
            p = list_entry(v->arch.vgic.lr_pending.next, typeof(*p), lr_queue);
            gic_set_lr(i, p->irq, GICH_LR_PENDING, p->priority);
            list_del_init(&p->lr_queue);
            set_bit(i, &gic.lr_mask);
        } else {
            gic_inject_irq_stop();
        }
        spin_unlock_irq(&gic.lock);

        spin_lock_irq(&v->arch.vgic.lock);
        p = irq_to_pending(v, virq);
        if ( p->desc != NULL ) {
            p->desc->status &= ~IRQ_INPROGRESS;
            GICC[GICC_DIR] = virq;
        }
        list_del_init(&p->inflight);
        spin_unlock_irq(&v->arch.vgic.lock);

        i++;
    }
}

void __cpuinit init_maintenance_interrupt(void)
{
    request_irq(25, maintenance_interrupt, 0, "irq-maintenance", NULL);
}

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
