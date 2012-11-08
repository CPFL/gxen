#ifndef __ARM_PERCPU_H__
#define __ARM_PERCPU_H__

#ifndef __ASSEMBLY__
extern char __per_cpu_start[], __per_cpu_data_end[];
extern unsigned long __per_cpu_offset[NR_CPUS];
void percpu_init_areas(void);

/* Separate out the type, so (int[3], foo) works. */
#define __DEFINE_PER_CPU(type, name, suffix)                    \
    __section(".bss.percpu" #suffix)                            \
    __typeof__(type) per_cpu_##name


#define per_cpu(var, cpu)  \
    (*RELOC_HIDE(&per_cpu__##var, __per_cpu_offset[cpu]))
#define __get_cpu_var(var) \
    (*RELOC_HIDE(&per_cpu__##var, READ_CP32(HTPIDR)))

#define DECLARE_PER_CPU(type, name) extern __typeof__(type) per_cpu__##name

DECLARE_PER_CPU(unsigned int, cpu_id);
#define get_processor_id()    (this_cpu(cpu_id))
#define set_processor_id(id)  do {                      \
    WRITE_CP32(__per_cpu_offset[id], HTPIDR);           \
    this_cpu(cpu_id) = (id);                            \
} while(0)
#endif

#endif /* __ARM_PERCPU_H__ */
/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
