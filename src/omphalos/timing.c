#include <string.h>
#include <stdlib.h>
#include <omphalos/timing.h>

int timestat_prep(timestat *ts,unsigned persec,unsigned total){
	if((ts->counts = malloc(sizeof(*ts->counts) * total)) == NULL){
		return -1;
	}
	memset(ts->counts,0,sizeof(*ts->counts) * total);
	gettimeofday(&ts->firstsamp,NULL);
	ts->firstidx = 0;
	ts->persec = persec;
	ts->total = total;
	return 0;
}

void timestat_inc(timestat *ts,const struct timeval *tv){
	struct timeval diff;
	unsigned long usec;

	timersub(tv,&ts->firstsamp,&diff);
       	usec = timerusec(&diff);
}

void timestat_destroy(timestat *ts){
	free(ts->counts);
}
