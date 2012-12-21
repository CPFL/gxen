#ifndef HW_NVC0_NVC0_CHANNEL_H_
#define HW_NVC0_NVC0_CHANNEL_H_
#include <stddef.h>

#define NVC0_CHANNELS 128
#define NVC0_CHANNELS_SHIFT 64

typedef struct nvc0_channel {
    uint32_t status;
    uint32_t id;
} nvc0_channel_t;

typedef struct nvc0_pfifo {
    size_t size;
    nvc0_channel_t channels[NVC0_CHANNELS];
} nvc0_pfifo_t;

#endif  // HW_NVC0_NVC0_CHANNEL_H_
/* vim: set sw=4 ts=4 et tw=80 : */
