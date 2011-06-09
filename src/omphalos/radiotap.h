#ifndef OMPHALOS_RADIOTAP
#define OMPHALOS_RADIOTAP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct interface;
struct omphalos_iface;

void handle_radiotap_packet(const struct omphalos_iface *,
		struct interface *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
