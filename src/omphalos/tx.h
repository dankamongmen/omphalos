#ifndef OMPHALOS_TX
#define OMPHALOS_TX

#ifdef __cplusplus
extern "C" {
#endif

struct interface;
struct omphalos_iface;

// Acquire a frame from the ringbuffer
void *get_tx_frame(const struct omphalos_iface *,struct interface *);

// Mark a frame as ready-to-send
void send_tx_frame(const struct omphalos_iface *,struct interface *,void *);

#ifdef __cplusplus
}
#endif

#endif
