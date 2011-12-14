#ifndef OMPHALOS_UTIL
#define OMPHALOS_UTIL

#ifdef __cplusplus
extern "C" {
#endif

#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include <pthread.h>
#include <omphalos/diag.h>
#include <omphalos/omphalos.h>

static inline void *
MMalloc(size_t s,const char *fname){
	void *r;

	if((r = malloc(s)) == NULL){
		diagnostic("%s|couldn't allocate %zu bytes",fname,s);
	}
	return r;
}

#define Malloc(s) MMalloc(s,__func__)

static inline void
PPthread_mutex_lock(pthread_mutex_t *lock,const char *lname,const char *fname){
	int r;

	if( (r = pthread_mutex_lock(lock)) ){
		diagnostic("%s|coudln't lock %s (%s?)",fname,lname,strerror(r));
		assert(0);
	}
}

#define Pthread_mutex_lock(l) PPthread_mutex_lock(l,#l,__func__)

static inline void
PPthread_mutex_unlock(pthread_mutex_t *lock,const char *lname,const char *fname){
	int r;

	if( (r = pthread_mutex_unlock(lock)) ){
		diagnostic("%s|coudln't unlock %s (%s?)",fname,lname,strerror(r));
		assert(0);
	}
}

#define Pthread_mutex_unlock(l) PPthread_mutex_unlock(l,#l,__func__)

static inline void *
memdup(const void *,size_t) __attribute__ ((warn_unused_result))
	__attribute__ ((nonnull (1))) __attribute__ ((malloc));

static inline void *
memdup(const void *s,size_t l){
	void *r;

	if( (r = malloc(l)) ){
		memcpy(r,s,l);
	}
	return r;
}

static inline wchar_t *
btowdup(const char *) __attribute__ ((warn_unused_result))
	__attribute__ ((nonnull (1))) __attribute__ ((malloc));

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

char *fgetl(char **,int *,FILE *) __attribute__ ((nonnull (1,2,3)))
		__attribute__ ((warn_unused_result));

#ifdef __cplusplus
}
#endif

#endif
