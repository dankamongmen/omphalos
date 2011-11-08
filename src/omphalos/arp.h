#ifndef OMPHALOS_ARP
#define OMPHALOS_ARP

#ifdef __cplusplus
extern "C" {
#endif

struct interface;
struct omphalos_iface;
struct omphalos_packet;

void handle_arp_packet(const struct omphalos_iface *,struct omphalos_packet *,
				const void *,size_t);

void send_arp_probe(struct interface *,const void *,const void *,size_t,const void *);

#ifdef __cplusplus
}
#endif

#endif
