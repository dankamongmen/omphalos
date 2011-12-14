#ifndef OMPHALOS_OSPF
#define OMPHALOS_OSPF

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#ifndef IPPROTO_OSPF
#define IPPROTO_OSPF	89
#endif

struct omphalos_packet;

void handle_ospf_packet(struct omphalos_packet *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
