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
#include <omphalos/omphalos.h>

static inline int
ethtool_docmd(const omphalos_iface *octx,const char *name,void *unsafe){
	struct ifreq ifr;
	int fd;

	if(strlen(name) >= sizeof(ifr.ifr_name)){
		octx->diagnostic("Bad name: %s",name);
		return -1;
	}
	memset(&ifr,0,sizeof(&ifr));
	strcpy(ifr.ifr_name,name);
	ifr.ifr_data = unsafe;
	if((fd = socket(AF_INET,SOCK_DGRAM,0)) < 0){
		octx->diagnostic("Couldn't open ethtool fd (%s?)",strerror(errno));
		return -1;
	}
	if(ioctl(fd,SIOCETHTOOL,&ifr)){
		// octx->diagnostic("Couldn't get driver info for %s (%s?)",name,strerror(errno));
		close(fd);
		return -1;
	}
	if(close(fd)){
		octx->diagnostic("Couldn't close ethtool fd %d (%s?)",fd,strerror(errno));
		return -1;
	}
	return 0;
}

int iface_driver_info(const omphalos_iface *octx,const char *name,struct ethtool_drvinfo *drv){
	drv->cmd = ETHTOOL_GDRVINFO;
	if(ethtool_docmd(octx,name,drv)){
		return -1;
	}
	// Some return the empty string for firmware / bus, others "N/A".
	// Normalize them here.
	if(strcmp(drv->fw_version,"N/A") == 0){
		drv->fw_version[0] = '\0';
	}
	if(strcmp(drv->bus_info,"N/A") == 0){
		drv->bus_info[0] = '\0';
	}
	return 0;
}

int iface_ethtool_info(const omphalos_iface *octx,const char *name,struct ethtool_cmd *info){
	info->cmd = ETHTOOL_GSET;
	if(ethtool_docmd(octx,name,info)){
		return -1;
	}
	return 0;
}
