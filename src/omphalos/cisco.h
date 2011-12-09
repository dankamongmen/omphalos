#ifndef OMPHALOS_CISCO
#define OMPHALOS_CISCO

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct omphalos_packet;

// Various Cisco garbage protocols

// Unidirectional Link Detection Protocol
void handle_udld_packet(struct omphalos_packet *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
