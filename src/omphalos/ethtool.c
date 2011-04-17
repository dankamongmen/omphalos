#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <linux/ethtool.h>
#include <omphalos/ethtool.h>

int iface_driver_info(const char *name,struct ethtool_drvinfo *drv){
	struct ifreq ifr;
	int fd;

	memset(&ifr,0,sizeof(&ifr));
	if(strlen(name) >= sizeof(ifr.ifr_name)){
		return -1;
	}
	strcpy(ifr.ifr_name,name);
	ifr.ifr_data = (caddr_t)drv;
	drv->cmd = ETHTOOL_GDRVINFO;
	if((fd = socket(AF_INET,SOCK_DGRAM,0)) < 0){
		fprintf(stderr,"Couldn't open ethtool fd (%s?)\n",strerror(errno));
		return -1;
	}
	if(ioctl(fd,SIOCETHTOOL,&ifr)){
		fprintf(stderr,"Couldn't get driver info for %s (%s?)\n",name,strerror(errno));
		close(fd);
		return -1;
	}
	if(close(fd)){
		fprintf(stderr,"Couldn't close ethtool fd %d (%s?)\n",fd,strerror(errno));
		return -1;
	}
	return 0;
}
