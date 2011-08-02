#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <omphalos/nl80211.h>

int open_nl80211_socket(void){
	return socket(PF_NETLINK,SOCK_RAW,NETLINK_GENERIC);
}
