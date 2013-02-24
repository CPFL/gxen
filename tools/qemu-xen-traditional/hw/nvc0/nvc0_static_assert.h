#ifndef HW_NVC0_NVC0_STATIC_ASSERT_H_
#define HW_NVC0_NVC0_STATIC_ASSERT_H_

// http://stackoverflow.com/questions/174356/ways-to-assert-expressions-at-build-time-in-c
#if defined(__GNUC__) && !defined(__cplusplus)
    #define NVC0_STATIC_ASSERT_HELPER(expr, msg) \
        (!!sizeof \
            (struct { unsigned int NVC0_STATIC_ASSERTION__##msg: (expr) ? 1 : -1; }))

    #define NVC0_STATIC_ASSERT(expr, msg) \
        extern int (*assert_function__(void)) [NVC0_STATIC_ASSERT_HELPER(expr, msg)]

#else

    #define NVC0_STATIC_ASSERT(expr, msg) \
        extern char NVC0_STATIC_ASSERTION__##msg[1]; \
        extern char NVC0_STATIC_ASSERTION__##msg[(expr)?1:2]

#endif  /* #ifdef __GNUC__ */

#endif  // HW_NVC0_NVC0_STATIC_ASSERT_H_
/* vim: set sw=4 ts=4 et tw=80 : */
