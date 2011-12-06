#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <omphalos/diag.h>
#include <netlink/socket.h>
#include <linux/rtnetlink.h>
#include <omphalos/nl80211.h>

static struct nl_sock *nl;

int open_nl80211(void){
	if((nl = nl_socket_alloc()) == NULL){
		diagnostic("Couldn't allocate generic netlink (%s?)",strerror(errno));
		return -1;
	}
	return 0;
	//return socket(PF_NETLINK,SOCK_RAW,NETLINK_GENERIC);
}

int close_nl80211(void){
	int ret = 0;

	nl_socket_free(nl);
	return ret;
}
