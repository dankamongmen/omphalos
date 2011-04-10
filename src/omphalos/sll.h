#ifndef OMPHALOS_SLL
#define OMPHALOS_SLL

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct interface;

void handle_cooked_packet(struct interface *,const unsigned char *,size_t);

#ifdef __cplusplus
}
#endif

#endif
