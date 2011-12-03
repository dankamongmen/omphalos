#ifndef OMPHALOS_HDLC
#define OMPHALOS_HDLC

#ifdef __cplusplus
extern "C" {
#endif

struct omphalos_packet;

void handle_hdlc_packet(struct omphalos_packet *,const void *,size_t)
			__attribute__ ((nonnull (1,2)));

#ifdef __cplusplus
}
#endif

#endif
