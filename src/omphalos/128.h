#ifndef OMPHALOS_128
#define OMPHALOS_128

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Use GCC vector extensions to drive SIMD
typedef uint32_t uint128_t __attribute__ ((vector_size(16)));

int equal128(uint128_t,uint128_t);

#ifdef __cplusplus
}
#endif

#endif
