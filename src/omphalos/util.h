#ifndef OMPHALOS_UTIL
#define OMPHALOS_UTIL

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>
#include <endian.h>

static inline void *
memdup(const void *s,size_t l){
	void *r;

	if( (r = malloc(l)) ){
		memcpy(r,s,l);
	}
	return r;
}

#ifdef __cplusplus
}
#endif

#endif
