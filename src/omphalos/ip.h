#ifndef OMPHALOS_IP
#define OMPHALOS_IP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

struct omphalos_iface;
struct omphalos_packet;

// Ethernet protocols 0x8000 and 0x86DD
void handle_ipv4_packet(const struct omphalos_iface *,struct omphalos_packet *,
					const void *,size_t);
void handle_ipv6_packet(const struct omphalos_iface *,struct omphalos_packet *,
					const void *,size_t);

int prep_ipv4_header(void *,size_t,uint32_t,uint32_t,uint16_t);
uint16_t ipv4_csum(const void *hdr) __attribute__ ((nonnull (1)));

#ifdef __cplusplus
}
#endif

#endif
