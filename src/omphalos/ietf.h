#ifndef OMPHALOS_IETF
#define OMPHALOS_IETF

#ifdef __cplusplus
extern "C" {
#endif

// Look up the 24-bit OUI against IANA specifications.
const char *ietf_multicast_lookup(int,const void *);

#ifdef __cplusplus
}
#endif

#endif
