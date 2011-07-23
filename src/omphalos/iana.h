#ifndef OMPHALOS_IANA
#define OMPHALOS_IANA

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct omphalos_iface;

// Load IANA OUI descriptions from the specified file, and watch it for updates
int init_iana_naming(const struct omphalos_iface *,const char *);

// Look up the 24-bit OUI against IANA specifications.
const char *iana_lookup(const void *);

#ifdef __cplusplus
}
#endif

#endif
