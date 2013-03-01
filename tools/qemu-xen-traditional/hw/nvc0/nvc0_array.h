#ifndef HW_NVC0_NVC0_ARRAY_H_
#define HW_NVC0_NVC0_ARRAY_H_
#include "nvc0_platform.h"

#if defined(NVC0_COMPILER_MSVC) || defined(__GXX_EXPERIMENTAL_CXX0X__)
#include <array>

#if defined(NVC0_COMPILER_MSVC) && !defined(NVC0_COMPILER_MSVC_10)
namespace std { using namespace tr1; }
#endif

#else

// G++ 4.6 workaround. including <list> before using namespace tr1
#include <list>
#include <tr1/array>
namespace std { using namespace tr1; }

#endif

#endif // HW_NVC0_NVC0_ARRAY_H_
/* vim: set sw=4 ts=4 et tw=80 : */
