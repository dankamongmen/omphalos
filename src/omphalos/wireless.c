#include <sys/socket.h>
#include <linux/wireless.h>
#include <omphalos/wireless.h>
#include <omphalos/interface.h>

int handle_wireless_event(interface *i,const struct iw_event *iw,size_t len){
	if(len < IW_EV_LCP_LEN){
		fprintf(stderr,"Wireless msg too short on %s (%zu)\n",i->name,len);
		return -1;
	}
	switch(iw->cmd){
	case SIOCGIWSCAN:{
		// FIXME handle scan results
		fprintf(stderr,"\t   Scan results on %s\n",i->name);
		break;
	}default:{
		fprintf(stderr,"\t   Unknown wireless event on %s: %x\n",i->name,iw->cmd);
		break;
	} }
	return 0;
}
