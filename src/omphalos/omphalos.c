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

int main(int argc,char * const *argv){
	int opt;
	omphalos_ctx pctx = {
		.pcapfn = NULL,
		.count = 0,
		.user = NULL,
	};
	
	opterr = 0; // suppress getopt() diagnostic to stderr while((opt = getopt(argc,argv,":c:f:")) >= 0){ switch(opt){ case 'c':{
	while((opt = getopt(argc,argv,":c:f:u:")) >= 0){
		switch(opt){
		case 'c':{
			char *ep;

			if(pctx.count){
				fprintf(stderr,"Provided %c twice\n",opt);
				usage(argv[0],EXIT_FAILURE);
			}
			if((pctx.count = strtoul(optarg,&ep,0)) == ULONG_MAX && errno == ERANGE){
				fprintf(stderr,"Bad value for %c: %s\n",opt,optarg);
				usage(argv[0],EXIT_FAILURE);
			}
			if(pctx.count == 0){
				fprintf(stderr,"Bad value for %c: %s\n",opt,optarg);
				usage(argv[0],EXIT_FAILURE);
			}
			if(ep == optarg || *ep){
				fprintf(stderr,"Bad value for %c: %s\n",opt,optarg);
				usage(argv[0],EXIT_FAILURE);
			}
			break;
		}case 'f':{
			if(pctx.pcapfn){
				fprintf(stderr,"Provided %c twice\n",opt);
				usage(argv[0],EXIT_FAILURE);
			}
			pctx.pcapfn = optarg;
			break;
		}case 'u':{
			if(pctx.user){
				fprintf(stderr,"Provided %c twice\n",opt);
				usage(argv[0],EXIT_FAILURE);
			}
			pctx.user = optarg;
			break;
		}case ':':{
			fprintf(stderr,"Option requires argument: '%c'\n",optopt);
			usage(argv[0],EXIT_FAILURE);
		}default:
			fprintf(stderr,"Unknown option: '%c'\n",optopt);
			usage(argv[0],EXIT_FAILURE);
		}
	}
	if(argv[optind]){ // don't allow trailing arguments
		fprintf(stderr,"Trailing argument: %s\n",argv[optind]);
		usage(argv[0],EXIT_FAILURE);
	}
	if(pctx.user == NULL){
		pctx.user = DEFAULT_USERNAME;
	}
	if(pctx.pcapfn){
		if(handle_priv_drop(pctx.user)){
			fprintf(stderr,"Couldn't become user %s (%s?)\n",pctx.user,strerror(errno));
			usage(argv[0],EXIT_FAILURE);
		}
		if(handle_pcap_file(&pctx)){
			return EXIT_FAILURE;
		}
	}else{
		if(handle_packet_socket(&pctx)){
			return EXIT_FAILURE;
		}
	}
	if(dump_output(stdout) < 0){
		if(errno != ENOMEM){
			fprintf(stderr,"Couldn't write output (%s?)\n",strerror(errno));
		}
		return EXIT_FAILURE;
	}
	cleanup_interfaces();
	cleanup_pcap();
	cleanup_l2hosts();
	cleanup_l3hosts();
	return EXIT_SUCCESS;
}
