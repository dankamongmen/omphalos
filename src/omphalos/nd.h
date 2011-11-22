#ifndef OMPHALOS_ND
#define OMPHALOS_ND

#ifdef __cplusplus
extern "C" {
#endif

// IPv6 Neigbor Discovery (RFC 4861)

#include <stdint.h>
#include <omphalos/128.h>

struct omphalos_packet;

void handle_nd_routersol(struct omphalos_packet *,const void *,size_t);
void handle_nd_neighsol(struct omphalos_packet *,const void *,size_t);
void handle_nd_routerad(struct omphalos_packet *,const void *,size_t);
void handle_nd_neighad(struct omphalos_packet *,const void *,size_t);
void handle_nd_redirect(struct omphalos_packet *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
