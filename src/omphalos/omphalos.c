#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <omphalos/pcap.h>
#include <omphalos/privs.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/psocket.h>
#include <omphalos/omphalos.h>
#include <omphalos/netaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

#define DEFAULT_USERNAME "nobody"

static void
usage(const char *arg0,int ret){
	fprintf(stderr,"usage: %s [ -u username ] [ -f filename ] [ -c count ]\n",
			basename(arg0));
	fprintf(stderr,"options:\n");
	fprintf(stderr,"-u username: user name to take after creating packet socket.\n");
	fprintf(stderr,"\t'%s' by default. provide empty string to disable.\n",DEFAULT_USERNAME);
	fprintf(stderr,"-f filename: libpcap-format save file for input.\n");
	fprintf(stderr,"-c count: exit after reading this many packets.\n");
	exit(ret);
}


int omphalos_setup(int argc,char * const *argv,omphalos_ctx *pctx){
	int opt;
	
	memset(pctx,0,sizeof(*pctx));
	opterr = 0; // suppress getopt() diagnostic to stderr while((opt = getopt(argc,argv,":c:f:")) >= 0){ switch(opt){ case 'c':{
	while((opt = getopt(argc,argv,":c:f:u:")) >= 0){
		switch(opt){
		case 'c':{
			char *ep;

			if(pctx->count){
				fprintf(stderr,"Provided %c twice\n",opt);
				usage(argv[0],-1);
			}
			if((pctx->count = strtoul(optarg,&ep,0)) == ULONG_MAX && errno == ERANGE){
				fprintf(stderr,"Bad value for %c: %s\n",opt,optarg);
				usage(argv[0],-1);
			}
			if(pctx->count == 0){
				fprintf(stderr,"Bad value for %c: %s\n",opt,optarg);
				usage(argv[0],-1);
			}
			if(ep == optarg || *ep){
				fprintf(stderr,"Bad value for %c: %s\n",opt,optarg);
				usage(argv[0],-1);
			}
			break;
		}case 'f':{
			if(pctx->pcapfn){
				fprintf(stderr,"Provided %c twice\n",opt);
				usage(argv[0],-1);
			}
			pctx->pcapfn = optarg;
			break;
		}case 'u':{
			if(pctx->user){
				fprintf(stderr,"Provided %c twice\n",opt);
				usage(argv[0],-1);
			}
			pctx->user = optarg;
			break;
		}case ':':{
			fprintf(stderr,"Option requires argument: '%c'\n",optopt);
			usage(argv[0],-1);
			break;
		}default:
			fprintf(stderr,"Unknown option: '%c'\n",optopt);
			usage(argv[0],-1);
			break;
		}
	}
	if(argv[optind]){ // don't allow trailing arguments
		fprintf(stderr,"Trailing argument: %s\n",argv[optind]);
		usage(argv[0],-1);
	}
	if(init_interfaces()){
		return -1;
	}
	if(pctx->user == NULL){
		pctx->user = DEFAULT_USERNAME;
	}
	return 0;
}

int omphalos_init(const omphalos_ctx *pctx){
	if(pctx->pcapfn){
		if(handle_priv_drop(pctx->user)){
			fprintf(stderr,"Couldn't become user %s (%s?)\n",pctx->user,strerror(errno));
			return -1;
		}
		if(handle_pcap_file(pctx)){
			return -1;
		}
	}else{
		if(handle_packet_socket(pctx)){
			return -1;
		}
	}
	return 0;
}

void omphalos_cleanup(void){
	cleanup_interfaces();
	cleanup_pcap();
	cleanup_l2hosts();
	cleanup_l3hosts();
}
