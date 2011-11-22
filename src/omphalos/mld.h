#ifndef OMPHALOS_MLD
#define OMPHALOS_MLD

#ifdef __cplusplus
extern "C" {
#endif

struct omphalos_packet;

void handle_mld_packet(struct omphalos_packet *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
