#ifndef HW_NVC0_NVC0_CHANNEL_H_
#define HW_NVC0_NVC0_CHANNEL_H_
#include <stddef.h>

#ifdef __cpp
extern "C" {
#endif

#define NVC0_CHANNELS 128
#define NVC0_CHANNELS_SHIFT 64

typedef struct nvc0_channel {
    uint32_t status;
    uint32_t id;
} nvc0_channel_t;

#define NVC0_USER_VMA_CHANNEL 0x1000

#ifdef __cpp
}
#endif

#endif  // HW_NVC0_NVC0_CHANNEL_H_
/* vim: set sw=4 ts=4 et tw=80 : */
