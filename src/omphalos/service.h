#ifndef OMPHALOS_SERVICE
#define OMPHALOS_SERVICE

#ifdef __cplusplus
extern "C" {
#endif

struct l4srv;
struct l3host;

// Call upon observing a service being provided, aka an advertisement or
// (preferably) an actual reply. Provide the:
//  - transport protocol
//  - transport address (port in UDP/TCP)
//  - service name
//  - server version (may be NULL)
void observe_service(struct l3host *,unsigned,unsigned,const char *,const char *);

#ifdef __cplusplus
}
#endif

#endif
