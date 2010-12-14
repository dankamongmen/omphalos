#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

int netlink_socket(void){
	struct sockaddr_nl sa;
	int fd;

	if((fd = socket(AF_NETLINK,SOCK_RAW,NETLINK_ROUTE)) < 0){
		fprintf(stderr,"Couldn't open NETLINK_ROUTE socket (%s?)\n",strerror(errno));
		return -1;
	}
	memset(&sa,0,sizeof(sa));
	sa.nl_family = AF_NETLINK;
	sa.nl_groups = RTNLGRP_MAX;
	if(bind(fd,(const struct sockaddr *)&sa,sizeof(sa))){
		fprintf(stderr,"Couldn't bind NETLINK_ROUTE socket %d (%s?)\n",fd,strerror(errno));
		close(fd);
		return -1;
	}
	return 0;
}
