#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/wireless.h>
#include <omphalos/wireless.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

int handle_wireless_event(const omphalos_iface *octx,interface *i,
				const struct iw_event *iw,size_t len){
	if(len < IW_EV_LCP_LEN){
		octx->diagnostic("Wireless msg too short on %s (%zu)",i->name,len);
		return -1;
	}
	switch(iw->cmd){
	case SIOCGIWSCAN:{
		// FIXME handle scan results
	break;}case SIOCGIWAP:{
		// FIXME handle AP results
	break;}case IWEVASSOCRESPIE:{
		// FIXME handle IE reassociation results
	break;}default:{
		octx->diagnostic("Unknown wireless event on %s: 0x%x",i->name,iw->cmd);
		return -1;
	} }
	if(octx->wireless_event){
		i->opaque = octx->wireless_event(i,iw->cmd,i->opaque);
	}
	return 0;
}

int print_wireless_event(FILE *fp,const interface *i,unsigned cmd){
	int n = 0;

	switch(cmd){
	case SIOCGIWSCAN:{
		// FIXME handle scan results
		n = fprintf(fp,"\t   Scan results on %s\n",i->name);
	break;}case SIOCGIWAP:{
		// FIXME handle AP results
		n = fprintf(fp,"\t   Access point on %s\n",i->name);
	break;}case IWEVASSOCRESPIE:{
		// FIXME handle IE reassociation results
		n = fprintf(fp,"\t   Reassociation on %s\n",i->name);
	break;}default:{
		n = fprintf(fp,"\t   Unknown wireless event on %s: 0x%x\n",i->name,cmd);
		break;
	} }
	return n;
}

static inline int
get_wireless_extension(const omphalos_iface *octx,const char *name,int cmd,struct iwreq *req){
	int fd;

	if(strlen(name) >= sizeof(req->ifr_name)){
		octx->diagnostic("Name too long: %s",name);
		return -1;
	}
	if((fd = socket(AF_INET,SOCK_DGRAM,0)) < 0){
		octx->diagnostic("Couldn't get a socket (%s?)",strerror(errno));
		return -1;
	}
	strcpy(req->ifr_name,name);
	if(ioctl(fd,cmd,req)){
		//octx->diagnostic("ioctl() failed (%s?)",strerror(errno));
		close(fd);
		return -1;
	}
	if(close(fd)){
		octx->diagnostic("Couldn't close socket (%s?)",strerror(errno));
		return -1;
	}
	return 0;
}

int iface_wireless_info(const omphalos_iface *octx,const char *name,wireless_info *wi){
	struct iwreq req;

	memset(wi,0,sizeof(*wi));
	memset(&req,0,sizeof(req));
	if(get_wireless_extension(octx,name,SIOCGIWNAME,&req)){
		return -1;
	}
	if(get_wireless_extension(octx,name,SIOCGIWRATE,&req)){
		return -1;
	}
	return 0;
}
