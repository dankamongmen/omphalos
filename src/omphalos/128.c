#include <string.h>
#include <omphalos/128.h>

int equal128(uint128_t v1,uint128_t v2){
	return !memcmp(&v1,&v2,sizeof(v1));
}

int equal128masked(uint128_t v1,uint128_t v2,unsigned octetsmasked){
	uint128_t i1,i2;

	memset(&i1,0,sizeof(i1));
	memset(&i2,0,sizeof(i2));
	memcpy(&i1,&v1,octetsmasked);
	memcpy(&i2,&v2,octetsmasked);
	return !memcmp(&i1,&i2,sizeof(i1));
}
