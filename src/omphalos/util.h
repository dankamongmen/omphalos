#ifndef OMPHALOS_UTIL
#define OMPHALOS_UTIL

#ifdef __cplusplus
extern "C" {
#endif

#include <wchar.h>
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

static inline wchar_t *
btowdup(const char *s){
	wchar_t *w;
	size_t l;

	l = strlen(s) + 1;
	if( (w = malloc(sizeof(*w) * l)) ){
		while(l--){
			w[l] = btowc(s[l]);
		}
	}
	return w;
}

#ifdef __cplusplus
}
#endif

#endif
