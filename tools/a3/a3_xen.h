#ifndef A3_XEN_H_
#define A3_XEN_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <libxl.h>

int a3_xen_add_memory_mapping(libxl_ctx* ctx, int domid, unsigned long first_gfn, unsigned long first_mfn, unsigned long nr_mfns);
int a3_xen_remove_memory_mapping(libxl_ctx* ctx, int domid, unsigned long first_gfn, unsigned long first_mfn, unsigned long nr_mfns);
void* a3_xen_map_foreign_range(libxl_ctx* ctx, int domid, int size, int prot, unsigned long mfn);
unsigned long a3_xen_gfn_to_mfn(libxl_ctx* ctx, int domid, unsigned long gfn);

#ifdef __cplusplus
}
#endif

#ifndef __cplusplus
#include <xenctrl.h>
#endif

#endif  // A3_XEN_H_
