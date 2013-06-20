#ifndef A3_XEN_H_
#define A3_XEN_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <libxl.h>

int a3_xen_assign_device(libxl_ctx* ctx, int domid, unsigned int encoded_bdf);
int a3_xen_deassign_device(libxl_ctx* ctx, int domid, unsigned int encoded_bdf);
int a3_xen_add_memory_mapping(libxl_ctx* ctx, int domid, unsigned long first_gfn, unsigned long first_mfn, unsigned long nr_mfns);
int a3_xen_remove_memory_mapping(libxl_ctx* ctx, int domid, unsigned long first_gfn, unsigned long first_mfn, unsigned long nr_mfns);

#ifdef __cplusplus
}
#endif

#ifndef __cplusplus
#include <xenctrl.h>
#endif

#endif  // A3_XEN_H_
