#ifndef OMPHALOS_HWADDRS
#define OMPHALOS_HWADDRS

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct l2host;
struct interface;
struct omphalos_iface;

// We don't handle any hardware addresses longer than 64 bits...yet...
typedef uint64_t hwaddrint;

struct l2host *lookup_l2host(const struct omphalos_iface *,struct interface *,
				const void *) __attribute__ ((nonnull (1,2,3)));

void cleanup_l2hosts(struct l2host **) __attribute__ ((nonnull (1)));

// Each byte becomes two ASCII characters + separator or nul
#define HWADDRSTRLEN(len) ((len) * 3)
void l2ntop(const struct l2host *,size_t,void *) __attribute__ ((nonnull (1,3)));
char *l2addrstr(const struct l2host *,size_t) __attribute__ ((nonnull (1)));

void *l2host_get_opaque(struct l2host *) __attribute__ ((nonnull (1)));

// Accessors
hwaddrint get_hwaddr(const struct l2host *) __attribute__ ((nonnull (1)));
const char *get_devname(const struct l2host *) __attribute__ ((nonnull (1)));

// Predicates and comparators
int l2hostcmp(const struct l2host *,const struct l2host *,size_t)
				__attribute__ ((nonnull (1,2)));

int l2categorize(const struct interface *,const struct l2host *)
				__attribute__ ((nonnull (1,2)));

#ifdef __cplusplus
}
#endif

#endif
