/*
 * This code is mostly taken from FreeBSD machine/atomic.h
 * Changes: Dietmar Hahn <dietmar.hahn@fujitsu-siemens.com>
 *
 ****************************************************************************
 * Copyright (c) 1998 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _MACHINE_ATOMIC_H_
#define _MACHINE_ATOMIC_H_

/*
 * Various simple arithmetic on memory which is atomic in the presence
 * of interrupts and SMP safe.
 */

#if !defined(__ASSEMBLY__)

#include <mini-os/types.h>


/*
 * Everything is built out of cmpxchg.
 */
#define IA64_CMPXCHG(sz, sem, p, cmpval, newval, ret)		\
	__asm __volatile (					\
		"mov ar.ccv=%2;;\n\t"				\
		"cmpxchg" #sz "." #sem " %0=%4,%3,ar.ccv\n\t"	\
		: "=r" (ret), "=m" (*p)				\
		: "r" (cmpval), "r" (newval), "m" (*p)		\
		: "memory")


/*
 * Some common forms of cmpxch.
 */

static __inline uint8_t
ia64_cmpxchg_acq_8(volatile uint8_t* p, uint8_t cmpval, uint8_t newval)
{
	uint8_t ret;

	IA64_CMPXCHG(1, acq, p, cmpval, newval, ret);
	return (ret);
}

static __inline uint16_t
ia64_cmpxchg_acq_16(volatile uint16_t* p, uint16_t cmpval, uint16_t newval)
{
	uint16_t ret;

	IA64_CMPXCHG(2, acq, p, cmpval, newval, ret);
	return (ret);
}

static __inline uint32_t
ia64_cmpxchg_acq_32(volatile uint32_t* p, uint32_t cmpval, uint32_t newval)
{
	uint32_t ret;

	IA64_CMPXCHG(4, acq, p, cmpval, newval, ret);
	return (ret);
}

static __inline uint32_t
ia64_cmpxchg_rel_32(volatile uint32_t* p, uint32_t cmpval, uint32_t newval)
{
	uint32_t ret;

	IA64_CMPXCHG(4, rel, p, cmpval, newval, ret);
	return (ret);
}

static __inline uint64_t
ia64_cmpxchg_acq_64(volatile uint64_t* p, uint64_t cmpval, uint64_t newval)
{
	uint64_t ret;

	IA64_CMPXCHG(8, acq, p, cmpval, newval, ret);
	return (ret);
}

static __inline uint64_t
ia64_cmpxchg_rel_64(volatile uint64_t* p, uint64_t cmpval, uint64_t newval)
{
	uint64_t ret;

	IA64_CMPXCHG(8, rel, p, cmpval, newval, ret);
	return (ret);
}

#define ATOMIC_STORE_LOAD(type, width, size)			\
static __inline uint##width##_t				\
ia64_ld_acq_##width(volatile uint##width##_t* p)		\
{								\
	uint##width##_t v;					\
								\
	__asm __volatile ("ld" size ".acq %0=%1"		\
			  : "=r" (v)				\
			  : "m" (*p)				\
			  : "memory");				\
	return (v);						\
}								\
								\
static __inline uint##width##_t				\
atomic_load_acq_##width(volatile uint##width##_t* p)		\
{								\
	uint##width##_t v;					\
								\
	__asm __volatile ("ld" size ".acq %0=%1"		\
			  : "=r" (v)				\
			  : "m" (*p)				\
			  : "memory");				\
	return (v);						\
}								\
								\
static __inline uint##width##_t				\
atomic_load_acq_##type(volatile uint##width##_t* p)		\
{								\
	uint##width##_t v;					\
								\
	__asm __volatile ("ld" size ".acq %0=%1"		\
			  : "=r" (v)				\
			  : "m" (*p)				\
			  : "memory");				\
	return (v);						\
}								\
							       	\
static __inline void						\
ia64_st_rel_##width(volatile uint##width##_t* p, uint##width##_t v)\
{								\
	__asm __volatile ("st" size ".rel %0=%1"		\
			  : "=m" (*p)				\
			  : "r" (v)				\
			  : "memory");				\
}								\
							       	\
static __inline void						\
atomic_store_rel_##width(volatile uint##width##_t* p, uint##width##_t v)\
{								\
	__asm __volatile ("st" size ".rel %0=%1"		\
			  : "=m" (*p)				\
			  : "r" (v)				\
			  : "memory");				\
}								\
							       	\
static __inline void						\
atomic_store_rel_##type(volatile uint##width##_t* p, uint##width##_t v)\
{								\
	__asm __volatile ("st" size ".rel %0=%1"		\
			  : "=m" (*p)				\
			  : "r" (v)				\
			  : "memory");				\
}

ATOMIC_STORE_LOAD(char, 8, "1")
ATOMIC_STORE_LOAD(short, 16, "2")
ATOMIC_STORE_LOAD(int, 32, "4")
ATOMIC_STORE_LOAD(long, 64, "8")

#undef ATOMIC_STORE_LOAD

#define IA64_ATOMIC(sz, type, name, width, op)			\
									\
static __inline type							\
atomic_##name##_acq_##width(volatile type *p, type v)		\
{									\
	type old, ret;							\
	do {								\
		old = *p;						\
		IA64_CMPXCHG(sz, acq, p, old, old op v, ret);	\
	} while (ret != old);						\
	return(ret);							\
}									\
									\
static __inline type							\
atomic_##name##_rel_##width(volatile type *p, type v)		\
{									\
	type old, ret;							\
	do {								\
		old = *p;						\
		IA64_CMPXCHG(sz, rel, p, old, old op v, ret);	\
	} while (ret != old);						\
	return(ret);							\
}

IA64_ATOMIC(1, uint8_t,  set,	8,	|)
IA64_ATOMIC(2, uint16_t, set,	16,	|)
IA64_ATOMIC(4, uint32_t, set,	32,	|)
IA64_ATOMIC(8, uint64_t, set,	64,	|)

IA64_ATOMIC(1, uint8_t,  clear,	8,	&~)
IA64_ATOMIC(2, uint16_t, clear,	16,	&~)
IA64_ATOMIC(4, uint32_t, clear,	32,	&~)
IA64_ATOMIC(8, uint64_t, clear,	64,	&~)

IA64_ATOMIC(1, uint8_t,  add,	8,	+)
IA64_ATOMIC(2, uint16_t, add,	16,	+)
IA64_ATOMIC(4, uint32_t, add,	32,	+)
IA64_ATOMIC(8, uint64_t, add,	64,	+)

IA64_ATOMIC(1, uint8_t,  subtract,	8,	-)
IA64_ATOMIC(2, uint16_t, subtract,	16,	-)
IA64_ATOMIC(4, uint32_t, subtract,	32,	-)
IA64_ATOMIC(8, uint64_t, subtract,	64,	-)

#undef IA64_ATOMIC
#undef IA64_CMPXCHG

#define atomic_set_8			atomic_set_acq_8
#define	atomic_clear_8			atomic_clear_acq_8
#define atomic_add_8			atomic_add_acq_8
#define	atomic_subtract_8		atomic_subtract_acq_8

#define atomic_set_16			atomic_set_acq_16
#define	atomic_clear_16			atomic_clear_acq_16
#define atomic_add_16			atomic_add_acq_16
#define	atomic_subtract_16		atomic_subtract_acq_16

#define atomic_set_32			atomic_set_acq_32
#define	atomic_clear_32			atomic_clear_acq_32
#define atomic_add_32			atomic_add_acq_32
#define	atomic_subtract_32		atomic_subtract_acq_32

#define atomic_set_64			atomic_set_acq_64
#define	atomic_clear_64			atomic_clear_acq_64
#define atomic_add_64			atomic_add_acq_64
#define	atomic_subtract_64		atomic_subtract_acq_64

#define atomic_set_char			atomic_set_8
#define atomic_clear_char		atomic_clear_8
#define atomic_add_char			atomic_add_8
#define atomic_subtract_char		atomic_subtract_8
#define atomic_set_acq_char		atomic_set_acq_8
#define atomic_clear_acq_char		atomic_clear_acq_8
#define atomic_add_acq_char		atomic_add_acq_8
#define atomic_subtract_acq_char	atomic_subtract_acq_8
#define atomic_set_rel_char		atomic_set_rel_8
#define atomic_clear_rel_char		atomic_clear_rel_8
#define atomic_add_rel_char		atomic_add_rel_8
#define atomic_subtract_rel_char	atomic_subtract_rel_8

#define atomic_set_short		atomic_set_16
#define atomic_clear_short		atomic_clear_16
#define atomic_add_short		atomic_add_16
#define atomic_subtract_short		atomic_subtract_16
#define atomic_set_acq_short		atomic_set_acq_16
#define atomic_clear_acq_short		atomic_clear_acq_16
#define atomic_add_acq_short		atomic_add_acq_16
#define atomic_subtract_acq_short	atomic_subtract_acq_16
#define atomic_set_rel_short		atomic_set_rel_16
#define atomic_clear_rel_short		atomic_clear_rel_16
#define atomic_add_rel_short		atomic_add_rel_16
#define atomic_subtract_rel_short	atomic_subtract_rel_16

#define atomic_set_int			atomic_set_32
#define atomic_clear_int		atomic_clear_32
#define atomic_add_int			atomic_add_32
#define atomic_subtract_int		atomic_subtract_32
#define atomic_set_acq_int		atomic_set_acq_32
#define atomic_clear_acq_int		atomic_clear_acq_32
#define atomic_add_acq_int		atomic_add_acq_32
#define atomic_subtract_acq_int		atomic_subtract_acq_32
#define atomic_set_rel_int		atomic_set_rel_32
#define atomic_clear_rel_int		atomic_clear_rel_32
#define atomic_add_rel_int		atomic_add_rel_32
#define atomic_subtract_rel_int		atomic_subtract_rel_32

#define atomic_set_long			atomic_set_64
#define atomic_clear_long		atomic_clear_64
#define atomic_add_long			atomic_add_64
#define atomic_subtract_long		atomic_subtract_64
#define atomic_set_acq_long		atomic_set_acq_64
#define atomic_clear_acq_long		atomic_clear_acq_64
#define atomic_add_acq_long		atomic_add_acq_64
#define atomic_subtract_acq_long	atomic_subtract_acq_64
#define atomic_set_rel_long		atomic_set_rel_64
#define atomic_clear_rel_long		atomic_clear_rel_64
#define atomic_add_rel_long		atomic_add_rel_64
#define atomic_subtract_rel_long	atomic_subtract_rel_64

/*
 * Atomically compare the value stored at *p with cmpval and if the
 * two values are equal, update the value of *p with newval. Returns
 * zero if the compare failed, nonzero otherwise.
 */
static __inline int
atomic_cmpset_acq_32(volatile uint32_t* p, uint32_t cmpval, uint32_t newval)
{
	return ia64_cmpxchg_acq_32(p, cmpval, newval) == cmpval;
}

static __inline int
atomic_cmpset_rel_32(volatile uint32_t* p, uint32_t cmpval, uint32_t newval)
{
	return ia64_cmpxchg_rel_32(p, cmpval, newval) == cmpval;
}

/*
 * Atomically compare the value stored at *p with cmpval and if the
 * two values are equal, update the value of *p with newval. Returns
 * zero if the compare failed, nonzero otherwise.
 */
static __inline int
atomic_cmpset_acq_64(volatile uint64_t* p, uint64_t cmpval, uint64_t newval)
{
	return ia64_cmpxchg_acq_64(p, cmpval, newval) == cmpval;
}

static __inline int
atomic_cmpset_rel_64(volatile uint64_t* p, uint64_t cmpval, uint64_t newval)
{
	return ia64_cmpxchg_rel_64(p, cmpval, newval) == cmpval;
}

#define atomic_cmpset_32		atomic_cmpset_acq_32
#define atomic_cmpset_64		atomic_cmpset_acq_64
#define	atomic_cmpset_int		atomic_cmpset_32
#define	atomic_cmpset_long		atomic_cmpset_64
#define atomic_cmpset_acq_int		atomic_cmpset_acq_32
#define atomic_cmpset_rel_int		atomic_cmpset_rel_32
#define atomic_cmpset_acq_long		atomic_cmpset_acq_64
#define atomic_cmpset_rel_long		atomic_cmpset_rel_64

static __inline int
atomic_cmpset_acq_ptr(volatile void *dst, void *exp, void *src)
{
        return atomic_cmpset_acq_long((volatile u_long *)dst,
				      (u_long)exp, (u_long)src);
}

static __inline int
atomic_cmpset_rel_ptr(volatile void *dst, void *exp, void *src)
{
        return atomic_cmpset_rel_long((volatile u_long *)dst,
				      (u_long)exp, (u_long)src);
}

#define	atomic_cmpset_ptr	atomic_cmpset_acq_ptr

static __inline void *
atomic_load_acq_ptr(volatile void *p)
{
	return (void *)atomic_load_acq_long((volatile u_long *)p);
}

static __inline void
atomic_store_rel_ptr(volatile void *p, void *v)
{
	atomic_store_rel_long((volatile u_long *)p, (u_long)v);
}

#define IA64_ATOMIC_PTR(NAME)				\
static __inline void					\
atomic_##NAME##_ptr(volatile void *p, uintptr_t v)	\
{							\
	atomic_##NAME##_long((volatile u_long *)p, v);	\
}							\
							\
static __inline void					\
atomic_##NAME##_acq_ptr(volatile void *p, uintptr_t v)	\
{							\
	atomic_##NAME##_acq_long((volatile u_long *)p, v);\
}							\
							\
static __inline void					\
atomic_##NAME##_rel_ptr(volatile void *p, uintptr_t v)	\
{							\
	atomic_##NAME##_rel_long((volatile u_long *)p, v);\
}

IA64_ATOMIC_PTR(set)
IA64_ATOMIC_PTR(clear)
IA64_ATOMIC_PTR(add)
IA64_ATOMIC_PTR(subtract)

#undef IA64_ATOMIC_PTR

static __inline uint32_t
atomic_readandclear_32(volatile uint32_t* p)
{
	uint32_t val;
	do {
		val = *p;
	} while (!atomic_cmpset_32(p, val, 0));
	return val;
}

static __inline uint64_t
atomic_readandclear_64(volatile uint64_t* p)
{
	uint64_t val;
	do {
		val = *p;
	} while (!atomic_cmpset_64(p, val, 0));
	return val;
}

#define atomic_readandclear_int	atomic_readandclear_32
#define atomic_readandclear_long	atomic_readandclear_64


/* Some bit operations */

static inline void
set_bit(int num, volatile void *addr)
{
	uint32_t bit, old, new;
	volatile uint32_t *p;
	p = (volatile uint32_t *) addr + (num >> 5);
	bit = 1 << (num & 31);
	do
	{
		old = *p;
		new = old | bit;
	} while(ia64_cmpxchg_acq_32(p, old, new) != old);
}

static __inline__ void
clear_bit(int num, volatile void *addr)
{
	uint32_t mask, old, new;
	volatile uint32_t *p;
	p = (volatile uint32_t *) addr + (num >> 5);
	mask = ~(1 << (num & 31));
	do {
		old = *p;
		new = old & mask;
	} while (ia64_cmpxchg_acq_32(p, old, new) != old);
}

static __inline__ int
test_bit(int num, const volatile void *addr)
{
	uint32_t val = 1;
        return val & (((const volatile uint32_t *) addr)[num >> 5] >> (num & 31));
}

/*
 * test_and_set_bit - Set a bit and return its old value
 * num: Bit to set
 * addr: Address to count from
 */
static inline int
test_and_set_bit (int num, volatile void *addr)
{
        uint32_t bit, old, new;
        volatile uint32_t *m;

        m = (volatile uint32_t *) addr + (num >> 5);
        bit = 1 << (num & 31);
        do {
                old = *m;
                new = old | bit;
        } while (ia64_cmpxchg_acq_32(m, old, new) != old);
        return (old & bit) != 0;
}

/*
 * test_and_clear_bit - Clear a bit and return its old value
 * num: Bit to set
 * addr: Address to count from
 */
static
inline int test_and_clear_bit(int num, volatile unsigned long * addr)
{
        uint32_t bit, old, new;
        volatile uint32_t* a;

        a = (volatile uint32_t *) addr + (num >> 5);
        bit = ~(1 << (num & 31));
        do {
                old = *a;
                new = old & bit;
        } while (ia64_cmpxchg_acq_32(a, old, new) != old);
        return (old & ~bit) != 0;
}


#endif /* !defined(__ASSEMBLY__) */

#endif /* ! _MACHINE_ATOMIC_H_ */
