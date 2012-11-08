/*
 * include/asm-x86/cache.h
 */
#ifndef __ARCH_X86_CACHE_H
#define __ARCH_X86_CACHE_H

#include <xen/config.h>

/* L1 cache line size */
#define L1_CACHE_SHIFT	(CONFIG_X86_L1_CACHE_SHIFT)
#define L1_CACHE_BYTES	(1 << L1_CACHE_SHIFT)

#define __read_mostly __section(".data.read_mostly")

#endif
