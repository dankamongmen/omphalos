#ifndef OMPHALOS_CSUM
#define OMPHALOS_CSUM

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

uint16_t ipv4_csum(const void *) __attribute__ ((nonnull (1)));

uint16_t udp4_csum(const void *hdr) __attribute__ ((nonnull (1)));
uint16_t udp6_csum(const void *hdr) __attribute__ ((nonnull (1)));

uint16_t icmp6_csum(const void *hdr) __attribute__ ((nonnull (1)));

#ifdef __cplusplus
}
#endif

#endif
