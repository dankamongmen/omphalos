#ifndef OMPHALOS_TIMING
#define OMPHALOS_TIMING

#ifdef __cplusplus
extern "C" {
#endif

// We want to support finite time-sliced statistics, to for instance show the
// bitrate on an interface for the last 5s at 50Hz sampling. Sampling at a
// higher rate than the video sync isn't useful for UI's, but might be
// desirable for headless drivers. We operate at microsecond resolution, though
// might not get that much from gettimeofday().

#include <stdint.h>
#include <sys/time.h>

// Total time domain in microseconds == total * usec
typedef struct timestat {
	uint32_t *counts;		// ringbuf of values
	unsigned usec,total;		// usec per count, number of counts
	unsigned firstidx;		// index of first sample in ringbuffer
	struct timeval firstsamp;	// time associated with first sample
	uintmax_t valtotal;		// sum of all counts
} timestat;

// For 5s at 50Hz, provide 20000 and 250 -- 50Hz means 20ms per counter.
int timestat_prep(timestat *,unsigned,unsigned);
void timestat_inc(timestat *,const struct timeval *tv,unsigned);
void timestat_destroy(timestat *);

static inline uintmax_t
timestat_val(const timestat *ts){
	return ts->valtotal;
}

static inline unsigned long
timerusec(const struct timeval *tv){
	return tv->tv_sec * 1000000 + tv->tv_usec;
}

// We don't expect to have very many timers active for an interface (certainly
// not as many as 10), so we use a trivial linked list implementation of our
// timing wheel. Should this ever get serious (eg, TCP implementation), use the
// methods of Varghese and Lauck.
//
// Functions will be invoked from the interface thread's context. They block
// progress of the interface thread. They are handed arbitrary opaque state and
// the most recently-sampled timestamp. If a non-zero value is returned, the
// value-result timestamp is taken to be a rescheduling time.

typedef int (*timerfxn)(const struct timeval *,struct timeval *,void *);

typedef struct twheel {
	struct timeval sched;	// earliest (absolute) time it can fire
	timerfxn fxn;
	struct twheel *next;
} twheel;

int schedule_timer(twheel **,const struct timeval *,timerfxn,void *);

#ifdef __cplusplus
}
#endif

#endif
