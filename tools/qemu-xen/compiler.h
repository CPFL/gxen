/* public domain */

#ifndef COMPILER_H
#define COMPILER_H

#include "config-host.h"

/*----------------------------------------------------------------------------
| The macro QEMU_GNUC_PREREQ tests for minimum version of the GNU C compiler.
| The code is a copy of SOFTFLOAT_GNUC_PREREQ, see softfloat-macros.h.
*----------------------------------------------------------------------------*/
#if defined(__GNUC__) && defined(__GNUC_MINOR__)
# define QEMU_GNUC_PREREQ(maj, min) \
         ((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
# define QEMU_GNUC_PREREQ(maj, min) 0
#endif

#define QEMU_NORETURN __attribute__ ((__noreturn__))

#if QEMU_GNUC_PREREQ(3, 4)
#define QEMU_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define QEMU_WARN_UNUSED_RESULT
#endif

#if defined(_WIN32)
# define QEMU_PACKED __attribute__((gcc_struct, packed))
#else
# define QEMU_PACKED __attribute__((packed))
#endif

#define QEMU_BUILD_BUG_ON(x) \
    typedef char qemu_build_bug_on__##__LINE__[(x)?-1:1];

#if defined __GNUC__
# if !QEMU_GNUC_PREREQ(4, 4)
   /* gcc versions before 4.4.x don't support gnu_printf, so use printf. */
#  define GCC_ATTR __attribute__((__unused__, format(printf, 1, 2)))
#  define GCC_FMT_ATTR(n, m) __attribute__((format(printf, n, m)))
# else
   /* Use gnu_printf when supported (qemu uses standard format strings). */
#  define GCC_ATTR __attribute__((__unused__, format(gnu_printf, 1, 2)))
#  define GCC_FMT_ATTR(n, m) __attribute__((format(gnu_printf, n, m)))
# endif
#else
#define GCC_ATTR /**/
#define GCC_FMT_ATTR(n, m)
#endif

#endif /* COMPILER_H */
