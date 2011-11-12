#ifndef OMPHALOS_IANA
#define OMPHALOS_IANA

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

// Load IANA OUI descriptions from the specified file, and watch it for updates
int init_iana_naming(const char *);

// Look up the 24-bit OUI against IANA specifications.
const wchar_t *iana_lookup(const void *,size_t);

void cleanup_iana_naming(void);

#ifdef __cplusplus
}
#endif

#endif
