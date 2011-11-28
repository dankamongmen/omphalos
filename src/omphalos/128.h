#ifndef OMPHALOS_128
#define OMPHALOS_128

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdint.h>
#include <string.h>

// Use GCC vector extensions to drive SIMD
/*typedef uint32_t uint128_t __attribute__ ((vector_size(16)))
			__attribute__ ((aligned (16)));*/
 
#define ZERO128 { 0, 0, 0, 0 }

typedef uint32_t uint128_t[4];

static inline void
andequals128(uint128_t result,const uint128_t mask){
	result[0] &= mask[0];
	result[1] &= mask[1];
	result[2] &= mask[2];
	result[3] &= mask[3];
}

static inline int
equal128(const uint128_t v1,const uint128_t v2){
	return !memcmp(v1,v2,16);
}

static inline int
equal128masked(const uint128_t v1,const uint128_t v2,unsigned octetsmasked){
	uint128_t i1,i2;

	memset(i1,0,sizeof(i1));
	memset(i2,0,sizeof(i2));
	memcpy(i1,v1,octetsmasked);
	memcpy(i2,v2,octetsmasked);
	return !memcmp(i1,i2,16);
}

static inline void
assign128(uint128_t to,const uint128_t from){
	memcpy(to,from,16);
}

static inline void
cast128(void *to,const uint128_t from){
	memcpy(to,from,16);
}

static inline void
set128(uint128_t to,const unsigned from){
	memset(to,from,16);
}

#ifdef __cplusplus
}
#endif

#endif
