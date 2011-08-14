#ifndef OMPHALOS_ICMP
#define OMPHALOS_ICMP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#ifndef IPPROTO_ICMP6
#define IPPROTO_ICMP6	58
#endif

struct omphalos_iface;
struct omphalos_packet;

void handle_icmp_packet(const struct omphalos_iface *,struct omphalos_packet *,
					const void *,size_t);

void handle_icmp6_packet(const struct omphalos_iface *,struct omphalos_packet *,
					const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
