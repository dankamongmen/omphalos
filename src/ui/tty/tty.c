#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <omphalos/pcap.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netaddrs.h>
#include <omphalos/wireless.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

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
	if(print_l2hosts(fp)){
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

static void
iface_event(const interface *i){
	print_iface(stdout,i);
}

static void
neigh_event(const struct interface *i,const struct l2host *l2){
	print_neigh(i,l2);
}

static void
iface_removed(const interface *i){
	printf("[%s] removed\n",i->name);
}

static void
wireless_event(const interface *i,unsigned cmd){
	print_wireless_event(stdout,i,cmd);
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
	omphalos_cleanup();
	return EXIT_SUCCESS;
}
