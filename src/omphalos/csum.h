#ifndef OMPHALOS_CSUM
#define OMPHALOS_CSUM

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

uint16_t ipv4_csum(const void *) __attribute__ ((nonnull (1)));

#ifdef __cplusplus
}
#endif

#endif
