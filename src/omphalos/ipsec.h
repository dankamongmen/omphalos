#ifndef OMPHALOS_IPSEC
#define OMPHALOS_IPSEC

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct omphalos_packet;

// Encapsulated Security Payload
void handle_esp_packet(struct omphalos_packet *,const void *,size_t);

// Authentication Header
void handle_ah_packet(struct omphalos_packet *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
