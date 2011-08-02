#include <omphalos/128.h>

int equal128(uint128_t v1,uint128_t v2){
	uint128_t v = v1 - v2;

	// FIXME
	return !(v[0] | v[1] | v[2] | v[3]);
}
