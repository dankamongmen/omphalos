#ifndef OMPHALOS_PIM
#define OMPHALOS_PIM

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct omphalos_iface;
struct omphalos_packet;

void handle_pim_packet(const struct omphalos_iface *,struct omphalos_packet *,
			const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
