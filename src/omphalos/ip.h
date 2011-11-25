#ifndef OMPHALOS_IP
#define OMPHALOS_IP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <omphalos/128.h>

struct omphalos_packet;

// Ethernet protocols 0x8000 and 0x86DD
void handle_ipv4_packet(struct omphalos_packet *,const void *,size_t);
void handle_ipv6_packet(struct omphalos_packet *,const void *,size_t);

int prep_ipv4_header(void *,size_t,uint32_t,uint32_t,unsigned);
int prep_ipv6_header(void *,size_t,uint128_t,uint128_t,unsigned);

#ifdef __cplusplus
}
#endif

#endif
