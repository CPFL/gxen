#ifndef CROSS_XEN_H_
#define CROSS_XEN_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <libxl.h>

int cross_assign_device(libxl_ctx* ctx, int domid, unsigned int encoded_bdf);
int cross_deassign_device(libxl_ctx* ctx, int domid, unsigned int encoded_bdf);

#ifdef __cplusplus
}
#endif

#ifndef __cplusplus
#include <xenctrl.h>
#endif

#endif  // CROSS_XEN_H_
