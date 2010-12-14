#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <linux/netlink.h>

int netlink_socket(void){
	int fd;

	if((fd = socket(AF_NETLINK,SOCK_RAW,NETLINK_ROUTE)) < 0){
		fprintf(stderr,"Couldn't open NETLINK_ROUTE socket (%s?)\n",strerror(errno));
		return -1;
	}
	return 0;
}
