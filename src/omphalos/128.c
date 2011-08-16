#include <string.h>
#include <omphalos/128.h>

int equal128(uint128_t v1,uint128_t v2){
	return !memcmp(&v1,&v2,sizeof(v1));
}
