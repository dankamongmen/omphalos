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

typedef struct timestat {
	uint32_t *counts;		// (count = rate * time)-entry ringbuf
	unsigned usec,total;		// usec per count, number of counts
	unsigned firstidx;		// index of first sample in ringbuffer
	struct timeval firstsamp;	// time associated with first sample
} timestat;

// For 5s at 50Hz, provide 20000 and 250 -- 50Hz means 20ms per counter.
int timestat_prep(timestat *,unsigned,unsigned);
void timestat_inc(timestat *,const struct timeval *tv,unsigned);
void timestat_destroy(timestat *);

static inline unsigned long
timerusec(const struct timeval *tv){
	return tv->tv_sec * 1000000 + tv->tv_usec;
}

#ifdef __cplusplus
}
#endif

#endif
