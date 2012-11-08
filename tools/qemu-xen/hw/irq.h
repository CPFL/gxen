#ifndef QEMU_IRQ_H
#define QEMU_IRQ_H

/* Generic IRQ/GPIO pin infrastructure.  */

typedef void (*qemu_irq_handler)(void *opaque, int n, int level);

void qemu_set_irq(qemu_irq irq, int level);

static inline void qemu_irq_raise(qemu_irq irq)
{
    qemu_set_irq(irq, 1);
}

static inline void qemu_irq_lower(qemu_irq irq)
{
    qemu_set_irq(irq, 0);
}

static inline void qemu_irq_pulse(qemu_irq irq)
{
    qemu_set_irq(irq, 1);
    qemu_set_irq(irq, 0);
}

/* Returns an array of N IRQs.  */
qemu_irq *qemu_allocate_irqs(qemu_irq_handler handler, void *opaque, int n);
void qemu_free_irqs(qemu_irq *s);

/* Returns a new IRQ with opposite polarity.  */
qemu_irq qemu_irq_invert(qemu_irq irq);

/* Returns a new IRQ which feeds into both the passed IRQs */
qemu_irq qemu_irq_split(qemu_irq irq1, qemu_irq irq2);

/* Returns a new IRQ set which connects 1:1 to another IRQ set, which
 * may be set later.
 */
qemu_irq *qemu_irq_proxy(qemu_irq **target, int n);

#endif
