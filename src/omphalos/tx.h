#ifndef OMPHALOS_TX
#define OMPHALOS_TX

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>

struct interface;

// Acquire a frame from the ringbuffer. Interface lock must be held.
void *get_tx_frame(struct interface *,size_t *);

// Mark a frame as ready-to-send. Interface lock must be held.
int send_tx_frame(struct interface *,void *);

// Release a frame for reuse without transmitting it. Interface lock must be held.
void abort_tx_frame(struct interface *,void *);

#ifdef __cplusplus
}
#endif

#endif
