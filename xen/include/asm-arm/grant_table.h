#ifndef __ASM_GRANT_TABLE_H__
#define __ASM_GRANT_TABLE_H__

#include <xen/grant_table.h>

#define INVALID_GFN (-1UL)
#define INITIAL_NR_GRANT_FRAMES 1

void gnttab_clear_flag(unsigned long nr, uint16_t *addr);
int create_grant_host_mapping(unsigned long gpaddr,
        unsigned long mfn, unsigned int flags, unsigned int
        cache_flags);
#define gnttab_host_mapping_get_page_type(op, d, rd) (0)
int replace_grant_host_mapping(unsigned long gpaddr, unsigned long mfn,
        unsigned long new_gpaddr, unsigned int flags);
void gnttab_mark_dirty(struct domain *d, unsigned long l);
#define gnttab_create_status_page(d, t, i) do {} while (0)
#define gnttab_create_shared_page(d, t, i) do {} while (0)
#define gnttab_shared_gmfn(d, t, i) (0)
#define gnttab_status_gmfn(d, t, i) (0)
#define gnttab_release_host_mappings(domain) 1
static inline int replace_grant_supported(void)
{
    return 1;
}

#endif /* __ASM_GRANT_TABLE_H__ */
/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
