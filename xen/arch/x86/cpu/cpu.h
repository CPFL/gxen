
struct cpu_model_info {
	int vendor;
	int family;
	char *model_names[16];
};

/* attempt to consolidate cpu attributes */
struct cpu_dev {
	char	* c_vendor;

	/* some have two possibilities for cpuid string */
	char	* c_ident[2];	

	struct		cpu_model_info c_models[4];

	void		(*c_init)(struct cpuinfo_x86 * c);
	void		(*c_identify)(struct cpuinfo_x86 * c);
	unsigned int	(*c_size_cache)(struct cpuinfo_x86 * c, unsigned int size);
};

extern struct cpu_dev * cpu_devs [X86_VENDOR_NUM];

extern unsigned int opt_cpuid_mask_ecx, opt_cpuid_mask_edx;
extern unsigned int opt_cpuid_mask_xsave_eax;
extern unsigned int opt_cpuid_mask_ext_ecx, opt_cpuid_mask_ext_edx;

extern int get_model_name(struct cpuinfo_x86 *c);
extern void display_cacheinfo(struct cpuinfo_x86 *c);

extern void early_intel_workaround(struct cpuinfo_x86 *c);
