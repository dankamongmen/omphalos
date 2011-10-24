#ifndef OMPHALOS_128
#define OMPHALOS_128

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Use GCC vector extensions to drive SIMD
typedef uint32_t uint128_t __attribute__ ((vector_size(16)))
			__attribute__ ((aligned (16)));
 
#define ZERO128 { 0, 0, 0, 0 }

/*typedef __int128 uint128_t;

#define ZERO128 0*/

int equal128(uint128_t,uint128_t);

#ifdef __cplusplus
}
#endif

#endif
