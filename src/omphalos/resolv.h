#ifndef OMPHALOS_RESOLV
#define OMPHALOS_RESOLV

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct l3host;
struct omphalos_iface;

void queue_for_naming(struct l3host *) __attribute__ ((nonnull (1)));

void cleanup_naming(const struct omphalos_iface *) __attribute__ ((nonnull (1)));

#ifdef __cplusplus
}
#endif

#endif
