#ifndef OMPHALOS_SERVICE
#define OMPHALOS_SERVICE

#ifdef __cplusplus
extern "C" {
#endif

struct l4srv;
struct l3host;

void observe_service(struct l3host *,const struct l4srv *);

#ifdef __cplusplus
}
#endif

#endif
