#ifndef A3_XEN_H_
#define A3_XEN_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <libxl.h>

int a3_assign_device(libxl_ctx* ctx, int domid, unsigned int encoded_bdf);
int a3_deassign_device(libxl_ctx* ctx, int domid, unsigned int encoded_bdf);

#ifdef __cplusplus
}
#endif

#ifndef __cplusplus
#include <xenctrl.h>
#endif

#endif  // A3_XEN_H_
