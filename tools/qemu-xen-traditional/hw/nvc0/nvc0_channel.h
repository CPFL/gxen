#ifndef HW_NVC0_NVC0_CHANNEL_H_
#define HW_NVC0_NVC0_CHANNEL_H_
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct nvc0_channel {
    uint32_t status;
    uint32_t id;
} nvc0_channel_t;


#ifdef __cplusplus
}
#endif

#endif  // HW_NVC0_NVC0_CHANNEL_H_
/* vim: set sw=4 ts=4 et tw=80 : */
