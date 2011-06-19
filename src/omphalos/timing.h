#ifndef OMPHALOS_TIMING
#define OMPHALOS_TIMING

#ifdef __cplusplus
extern "C" {
#endif

// We want to support finite time-sliced statistics, to for instance show the
// bitrate on an interface for the last 5s at 50Hz sampling. Sampling at a
// higher rate than the video sync isn't useful for UI's, but might be
// desirable for headless drivers.

#include <stdint.h>
#include <sys/time.h>

typedef struct timestat {
	uint32_t *counts;		// (count = rate * time)-entry ringbuf
	unsigned persec,total;		// counts per sec, number of counts
	unsigned firstidx;		// index of first sample in ringbuffer
	struct timeval firstsamp;	// time associated with first sample
} timestat;

int timestat_prep(timestat *,unsigned,unsigned);
void timestat_inc(timestat *,const struct timeval *tv);
void timestat_destroy(timestat *);

static inline unsigned long
timerusec(const struct timeval *tv){
	return tv->tv_sec * 1000000 + tv->tv_usec;
}

#ifdef __cplusplus
}
#endif

#endif
