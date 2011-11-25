#include <string.h>
#include <omphalos/diag.h>
#include <omphalos/ssdp.h>
#include <omphalos/omphalos.h>

#define SSDP_METHOD_NOTIFY "NOTIFY "
#define SSDP_METHOD_SEARCH "SEARCH "

// Returns 1 for a valid SSDP response, -1 for a valid SSDP query, 0 otherwise
int handle_ssdp_packet(omphalos_packet *op,const void *frame,size_t len){
	if(len < __builtin_strlen(SSDP_METHOD_NOTIFY)){
		diagnostic("%s frame too short (%zu)",__func__,len);
		op->malformed = 1;
		return 0;
	}
	if(strncmp(frame,SSDP_METHOD_NOTIFY,__builtin_strlen(SSDP_METHOD_NOTIFY)) == 0){
		return 1;
	}else if(strncmp(frame,SSDP_METHOD_SEARCH,__builtin_strlen(SSDP_METHOD_SEARCH)) == 0){
		return -1;
	}
	return 0;
}
