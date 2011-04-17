#include <sys/socket.h>
#include <linux/wireless.h>
#include <omphalos/wireless.h>
#include <omphalos/interface.h>

int handle_wireless_event(interface *i,const struct iw_event *iw){
	printf("Wireless event on %s: %u\n",i->name,iw->cmd);
	return 0;
}
