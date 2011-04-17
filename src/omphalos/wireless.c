#include <sys/socket.h>
#include <linux/wireless.h>
#include <omphalos/wireless.h>
#include <omphalos/interface.h>

int handle_wireless_event(interface *i,const struct iw_event *iw,size_t len){
	if(len < IW_EV_LCP_LEN){
		fprintf(stderr,"Wireless msg too short on %s (%zu)\n",i->name,len);
		return -1;
	}
	printf("Wireless event on %s: %x\n",i->name,iw->cmd);
	return 0;
}
