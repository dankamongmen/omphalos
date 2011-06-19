#include <string.h>
#include <stdlib.h>
#include <omphalos/timing.h>

int timestat_prep(timestat *ts,unsigned usec,unsigned total){
	if((ts->counts = malloc(sizeof(*ts->counts) * total)) == NULL){
		return -1;
	}
	memset(ts->counts,0,sizeof(*ts->counts) * total);
	gettimeofday(&ts->firstsamp,NULL);
	ts->firstidx = 0;
	ts->total = total;
	ts->usec = usec;
	return 0;
}

static inline unsigned
ringinc(unsigned idx,unsigned move,unsigned s){
	return idx + move % s;
}

void timestat_inc(timestat *ts,const struct timeval *tv,unsigned val){
	struct timeval diff;
	unsigned long usec;
	unsigned distance;

	timersub(tv,&ts->firstsamp,&diff);
       	usec = timerusec(&diff);
	// Get the number of samples between us and the first sample
	distance = usec / ts->usec;
	// This is equivalent to moving forward a slot, since we can't move
	// forward without expiring some count once we've filled the ringbuf
	// once. Before we've filled the ring once, we can move forward without
	// expiry, but at that time the ring is initialized to 0's anyway, so
	// this is the only time (other than init) that we need to zero counts.
	// We zero out min(expired,ts->total), which is always at least 1, so
	// we always zero our own new slot. There's thus no need to track a
	// last sample time; the first tracked sample time is sufficient.
	if(distance >= ts->total){
		struct timeval adv;
		unsigned expired;

		// Some counts have expired (if the distance is greater than or
		// equal to twice the total, all of them have expired). First,
		// determine how many to keep...
		expired = distance - ts->total + 1; // this many expired
		if(expired < ts->total){ // keep some
			unsigned idx; // new slot's idx

			idx = ringinc(ts->firstidx,distance,ts->total);
			ts->firstidx = ringinc(ts->firstidx,expired,ts->total);
			distance -= expired;
			// zero out our new slot and any that we've skipped
			// over (expired in total). might be disjoint.
			if(idx + 1 < expired){
				unsigned rexpir = expired - (idx + 1);

				memset(ts->counts,0,
					sizeof(*ts->counts) * (idx + 1));
				memset(ts->counts + (ts->total - rexpir),0,
						sizeof(*ts->counts) * rexpir);
			}else{ // we can get all expired counts with one memset
				memset(ts->counts + ((idx + 1) - expired),0,
					sizeof(*ts->counts) * expired);
			}
		}else{ // lose all; start over at head of ring, zero out all
			ts->firstidx = 0;
			distance = 0;
			memset(ts->counts,0,sizeof(*ts->counts) * ts->total);
		}
		// Base the time off distance * ts->usec + firstsamp,
		// normalizing time of the sample within the period.
		adv.tv_sec = ((distance - ts->total) * ts->usec) / 1000000;
		adv.tv_usec = ((distance - ts->total) * ts->usec) % 1000000;
		timeradd(&ts->firstsamp,&adv,&ts->firstsamp);
	}
	ts->counts[ringinc(ts->firstidx,distance,ts->total)] += val;
}

void timestat_destroy(timestat *ts){
	free(ts->counts);
}
