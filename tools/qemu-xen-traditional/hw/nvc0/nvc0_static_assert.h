#ifndef HW_NVC0_NVC0_STATIC_ASSERT_H_
#define HW_NVC0_NVC0_STATIC_ASSERT_H_

#define NVC0_JOIN2(x, y) x##y
#define NVC0_JOIN(x, y) NVC0_JOIN2(x, y)

// http://stackoverflow.com/questions/174356/ways-to-assert-expressions-at-build-time-in-c
#if defined(__GNUC__) && !defined(__cplusplus)
    #define NVC0_STATIC_ASSERT_HELPER(expr, msg) \
        (!!sizeof \
            (struct { unsigned int NVC0_JOIN(NVC0_STATIC_ASSERTION__##msg##__, __LINE__) : (expr) ? 1 : -1; }))

    #define NVC0_STATIC_ASSERT(expr, msg) \
        extern int (*assert_function__(void)) [NVC0_STATIC_ASSERT_HELPER(expr, msg)]

#else

    #define NVC0_STATIC_ASSERT(expr, msg) \
        typedef char NVC0_JOIN(NVC0_STATIC_ASSERTION__##msg##__, __LINE__)[(!!(expr))*2-1]

#endif  /* #ifdef __GNUC__ */

#endif  // HW_NVC0_NVC0_STATIC_ASSERT_H_
/* vim: set sw=4 ts=4 et tw=80 : */
