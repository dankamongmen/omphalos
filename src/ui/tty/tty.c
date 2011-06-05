#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <omphalos/pcap.h>
#include <linux/wireless.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netaddrs.h>
#include <omphalos/wireless.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// FIXME this ought return a string rather than printing it
#define IFF_FLAG(flags,f) ((flags) & (IFF_##f) ? #f" " : "")
static int
print_iface(FILE *fp,const interface *iface){
	const char *at;
	int n = 0;

	if((at = lookup_arptype(iface->arptype,NULL)) == NULL){
		fprintf(stderr,"Unknown dev type %u\n",iface->arptype);
		return -1;
	}
	n = fprintf(fp,"[%8s][%s] %d %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
		iface->name,at,iface->mtu,
		IFF_FLAG(iface->flags,UP),
		IFF_FLAG(iface->flags,BROADCAST),
		IFF_FLAG(iface->flags,DEBUG),
		IFF_FLAG(iface->flags,LOOPBACK),
		IFF_FLAG(iface->flags,POINTOPOINT),
		IFF_FLAG(iface->flags,NOTRAILERS),
		IFF_FLAG(iface->flags,RUNNING),
		IFF_FLAG(iface->flags,PROMISC),
		IFF_FLAG(iface->flags,ALLMULTI),
		IFF_FLAG(iface->flags,MASTER),
		IFF_FLAG(iface->flags,SLAVE),
		IFF_FLAG(iface->flags,MULTICAST),
		IFF_FLAG(iface->flags,PORTSEL),
		IFF_FLAG(iface->flags,AUTOMEDIA),
		IFF_FLAG(iface->flags,DYNAMIC),
		IFF_FLAG(iface->flags,LOWER_UP),
		IFF_FLAG(iface->flags,DORMANT),
		IFF_FLAG(iface->flags,ECHO)
		);
	if(n < 0){
		return -1;
	}
	if(!(iface->flags & IFF_LOOPBACK)){
		int nn;

		nn = fprintf(fp,"\t   driver: %s %s @ %s\n",iface->drv.driver,
				iface->drv.version,iface->drv.bus_info);
		if(nn < 0){
			return -1;
		}
		n += nn;
	}
	return n;
}
#undef IFF_FLAG

static int
print_stats(FILE *fp){
	interface total;

	memset(&total,0,sizeof(total));
	if(printf("<stats>") < 0){
		return -1;
	}
	if(print_all_iface_stats(fp,&total) < 0){
		return -1;
	}
	if(print_pcap_stats(fp,&total) < 0){
		return -1;
	}
	if(print_iface_stats(fp,&total,NULL,"total") < 0){
		return -1;
	}
	if(printf("</stats>") < 0){
		return -1;
	}
	return 0;
}

static int
dump_output(FILE *fp){
	if(fprintf(fp,"<omphalos>") < 0){
		return -1;
	}
	if(print_stats(fp)){
		return -1;
	}
	if(print_l3hosts(fp)){
		return -1;
	}
	if(fprintf(fp,"</omphalos>\n") < 0 || fflush(fp)){
		return -1;
	}
	return 0;
}

static void *
iface_event(const interface *i,int inum __attribute__ ((unused)),
			void *unsafe __attribute__ ((unused))){
	print_iface(stdout,i);
	return NULL;
}

static void *
neigh_event(const struct interface *i,const struct l2host *l2,
		void *unsafe __attribute__ ((unused))){
	print_neigh(i,l2);
	return NULL;
}

static void
iface_removed(const interface *i,void *unsafe __attribute__ ((unused))){
	printf("[%s] removed\n",i->name);
}

static int
print_wireless_event(FILE *fp,const interface *i,unsigned cmd){
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

static void *
wireless_event(const interface *i,unsigned cmd,void *unsafe __attribute__ ((unused))){
	print_wireless_event(stdout,i,cmd);
	return NULL;
}

int main(int argc,char * const *argv){
	omphalos_ctx pctx;

	if(omphalos_setup(argc,argv,&pctx)){
		return EXIT_FAILURE;
	}
	pctx.iface.iface_event = iface_event;
	pctx.iface.iface_removed = iface_removed;
	pctx.iface.neigh_event = neigh_event;
	pctx.iface.wireless_event = wireless_event;
	if(omphalos_init(&pctx)){
		return EXIT_FAILURE;
	}
	if(dump_output(stdout) < 0){
		if(errno != ENOMEM){
			fprintf(stderr,"Couldn't write output (%s?)\n",strerror(errno));
		}
		return EXIT_FAILURE;
	}
	omphalos_cleanup(&pctx);
	return EXIT_SUCCESS;
}
