/* Copyright 2003 Andi Kleen, SuSE Labs. 
 * Subject to the GNU Public License, v.2 
 * 
 * Generic x86 APIC driver probe layer.
 */  
#include <xen/config.h>
#include <xen/cpumask.h>
#include <xen/string.h>
#include <xen/kernel.h>
#include <xen/ctype.h>
#include <xen/init.h>
#include <asm/cache.h>
#include <asm/fixmap.h>
#include <asm/mpspec.h>
#include <asm/apicdef.h>
#include <asm/mach-generic/mach_apic.h>
#include <asm/setup.h>

extern const struct genapic apic_summit;
extern const struct genapic apic_bigsmp;

const struct genapic *__read_mostly genapic;

const struct genapic *apic_probe[] __initdata = {
	&apic_summit,
	&apic_bigsmp, 
	&apic_default,	/* must be last */
	NULL,
};

static bool_t __initdata cmdline_apic;

void __init generic_bigsmp_probe(void)
{
	/*
	 * This routine is used to switch to bigsmp mode when
	 * - There is no apic= option specified by the user
	 * - generic_apic_probe() has choosen apic_default as the sub_arch
	 * - we find more than 8 CPUs in acpi LAPIC listing with xAPIC support
	 */

	if (!cmdline_apic && genapic == &apic_default)
		if (apic_bigsmp.probe()) {
			genapic = &apic_bigsmp;
			printk(KERN_INFO "Overriding APIC driver with %s\n",
			       genapic->name);
		}
}

static void __init genapic_apic_force(char *str)
{
	int i;
	for (i = 0; apic_probe[i]; i++)
		if (!strcmp(apic_probe[i]->name, str))
			genapic = apic_probe[i];
}
custom_param("apic", genapic_apic_force);

void __init generic_apic_probe(void) 
{ 
	int i, changed;

	record_boot_APIC_mode();

	check_x2apic_preenabled();
	cmdline_apic = changed = (genapic != NULL);

	for (i = 0; !changed && apic_probe[i]; i++) { 
		if (apic_probe[i]->probe()) {
			changed = 1;
			genapic = apic_probe[i];
		} 
	}
	if (!changed) 
		genapic = &apic_default;

	printk(KERN_INFO "Using APIC driver %s\n", genapic->name);
} 

/* These functions can switch the APIC even after the initial ->probe() */

int __init mps_oem_check(struct mp_config_table *mpc, char *oem, char *productid)
{ 
	int i;
	for (i = 0; apic_probe[i]; ++i) { 
		if (apic_probe[i]->mps_oem_check(mpc,oem,productid)) { 
			if (!cmdline_apic) {
				genapic = apic_probe[i];
				printk(KERN_INFO "Switched to APIC driver `%s'.\n", 
				       genapic->name);
			}
			return 1;
		} 
	} 
	return 0;
} 

int __init acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	int i;
	for (i = 0; apic_probe[i]; ++i) { 
		if (apic_probe[i]->acpi_madt_oem_check(oem_id, oem_table_id)) { 
			if (!cmdline_apic) {
				genapic = apic_probe[i];
				printk(KERN_INFO "Switched to APIC driver `%s'.\n", 
				       genapic->name);
			}
			return 1;
		} 
	} 
	return 0;	
}
