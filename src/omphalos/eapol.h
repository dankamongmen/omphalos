#ifndef OMPHALOS_EAPOL
#define OMPHALOS_EAPOL

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct interface;

void handle_eapol_packet(struct interface *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
