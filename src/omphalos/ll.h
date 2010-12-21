#ifndef OMPHALOS_LL
#define OMPHALOS_LL

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

struct l2node;

struct l2node *acquire_l2_node(const void *hwaddr,size_t hwlen);

#ifdef __cplusplus
}
#endif

#endif
