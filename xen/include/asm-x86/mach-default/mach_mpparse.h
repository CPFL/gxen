#ifndef __ASM_MACH_MPPARSE_H
#define __ASM_MACH_MPPARSE_H

static inline int __init mps_oem_check(struct mp_config_table *mpc, char *oem,
				       char *productid)
{
	return 0;
}

/* Hook from generic ACPI tables.c */
static inline int __init acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return 0;
}


#endif /* __ASM_MACH_MPPARSE_H */
