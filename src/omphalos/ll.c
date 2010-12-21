#include <omphalos/ll.h>

typedef struct l2node {
	void *hwaddr;
} l2node;

l2node *acquire_l2_node(const void *hwaddr,size_t hwlen){
	// FIXME
	if(!hwaddr || !hwlen){ return NULL; }
	return NULL;
}
