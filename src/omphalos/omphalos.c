#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pcap/pcap.h>
#include <sys/socket.h>
#include <omphalos/usb.h>
#include <omphalos/pci.h>
#include <omphalos/iana.h>
#include <omphalos/pcap.h>
#include <sys/capability.h>
#include <omphalos/privs.h>
#include <omphalos/route.h>
#include <omphalos/resolv.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netlink.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

#define DEFAULT_USERNAME "nobody"
#define DEFAULT_IANA_FILENAME "ieee-oui.txt" // from arp-scan's 'get-oui'
#define DEFAULT_RESOLVCONF_FILENAME "/etc/resolv.conf"

static void
usage(const char *arg0,int ret){
	FILE *fp = ret == EXIT_SUCCESS ? stdout : stderr;

	fprintf(fp,"usage: %s [ options... ]\n",basename(arg0));
	fprintf(fp,"\noptions:\n");
	fprintf(fp,"-h print this help, and exit\n");
	fprintf(fp,"--version print version info, and exit\n");
	fprintf(fp,"-u username: user name to take after creating packet socket.\n");
	fprintf(fp,"\t'%s' by default. provide empty string to disable.\n",DEFAULT_USERNAME);
	fprintf(fp,"-f filename: libpcap-format save file for input.\n");
	fprintf(fp,"--ouis filename: IANA's OUI mapping in get-oui(1) format.\n");
	fprintf(fp,"\t'%s' by default. provide empty string to disable.\n",DEFAULT_IANA_FILENAME);
	fprintf(fp,"--resolv filename: resolv.conf-format nameserver list.\n");
	fprintf(fp,"\t'%s' by default. provide empty string to disable.\n",DEFAULT_RESOLVCONF_FILENAME);
	fprintf(fp,"--plog filename: Enable malformed packet logging to this file.\n");
	exit(ret);
}

static void
version(const char *arg0){
	fprintf(stdout,"%s %s\n",PROGNAME,VERSION);
	fprintf(stdout,"invoked as %s\n",arg0);
	exit(EXIT_SUCCESS);
}

static void
default_diagnostic(const char *fmt,...){
	va_list va;

	va_start(va,fmt);
	if(vfprintf(stderr,fmt,va) < 0){
		abort();
	}
	if(fputc('\n',stderr) < 0){
		abort();
	}
	va_end(va);
}

// If we add any other signals to this list, be sure to update the signal
// unmasking that goes on in the handling thread!
static int
mask_cancel_sigs(sigset_t *oldsigs){
	sigset_t cancelsigs;

	if(sigemptyset(&cancelsigs) || sigaddset(&cancelsigs,SIGINT)){
		fprintf(stderr,"Couldn't prep signals (%s?)\n",strerror(errno));
		return -1;
	}
	if(sigprocmask(SIG_BLOCK,&cancelsigs,oldsigs)){
		fprintf(stderr,"Couldn't mask signals (%s?)\n",strerror(errno));
		return -1;
	}
	return 0;
}

enum {
	OPT_OUIS = 'z' + 1,
	OPT_VERSION,
	OPT_PLOG,
	OPT_RESOLV,
};

int omphalos_setup(int argc,char * const *argv,omphalos_ctx *pctx){
	static const struct option ops[] = {
		{
			.name = "ouis",
			.has_arg = 1,
			.flag = NULL,
			.val = OPT_OUIS,
		},{
			.name = "version",
			.has_arg = 0,
			.flag = NULL,
			.val = OPT_VERSION,
		},{
			.name = "plog",
			.has_arg = 1,
			.flag = NULL,
			.val = OPT_PLOG,
		},{
			.name = "resolv",
			.has_arg = 1,
			.flag = NULL,
			.val = OPT_RESOLV,
		},
		{
			.name = NULL,
			.has_arg = 0,
			.flag = NULL,
			.val = 0,
		}
	};
	// FIXME maybe CAP_SETPCAP as well?
	const cap_value_t caparray[] = { CAP_NET_RAW, };
	const char *user = NULL;
	int opt,longidx;
	
	memset(pctx,0,sizeof(*pctx));
	opterr = 0; // suppress getopt() diagnostic to stderr
	while((opt = getopt_long(argc,argv,":hf:u:",ops,&longidx)) >= 0){
		switch(opt){ // FIXME need --plog
		case 'h':{
			usage(argv[0],EXIT_SUCCESS);
			break;
		}case OPT_VERSION:{
			version(argv[0]);
			break;
		}case OPT_OUIS:{
			if(pctx->ianafn){
				fprintf(stderr,"Provided --ouis twice\n");
				usage(argv[0],EXIT_FAILURE);
			}
			pctx->ianafn = optarg;
			break;
		}case OPT_RESOLV:{
			if(pctx->resolvconf){
				fprintf(stderr,"Provided --resolv twice\n");
				usage(argv[0],EXIT_FAILURE);
			}
			pctx->resolvconf = optarg;
			break;
		}case OPT_PLOG:{
			if(pctx->plog){
				fprintf(stderr,"Provided --plog twice\n");
				usage(argv[0],EXIT_FAILURE);
			}
			if((pctx->plogp = pcap_open_dead(DLT_EN10MB,0)) == NULL){
				fprintf(stderr,"Couldn't open pcap output file\n");
				usage(argv[0],EXIT_FAILURE);
			}
			if((pctx->plog = pcap_dump_open(pctx->plogp,optarg)) == NULL){
				// pcap_geterr() sticks a friendly ": " in front of itself argh
				fprintf(stderr,"Couldn't write to %s%s?\n",optarg,pcap_geterr(pctx->plogp));
				usage(argv[0],EXIT_FAILURE);
			}
			fprintf(stdout,"Logging malformed packets to %s\n",optarg);
			break;
		}case 'f':{
			if(pctx->pcapfn){
				fprintf(stderr,"Provided %c twice\n",opt);
				usage(argv[0],EXIT_FAILURE);
			}
			pctx->pcapfn = optarg;
			break;
		}case 'u':{
			if(user){
				fprintf(stderr,"Provided %c twice\n",opt);
				usage(argv[0],EXIT_FAILURE);
			}
			user = optarg;
			break;
		}case ':':{
			// FIXME need handle long options here
			fprintf(stderr,"Option requires argument: '%c'\n",optopt);
			usage(argv[0],EXIT_FAILURE);
			break;
		}case '?':{
			fprintf(stderr,"Unknown option: '%c'\n",optopt);
			usage(argv[0],EXIT_FAILURE);
			break;
		}default:{
			fprintf(stderr,"Getopt returned %d (%c)\n",opt,opt);
			usage(argv[0],EXIT_FAILURE);
			break;
		} }
	}
	if(argv[optind]){ // don't allow trailing arguments
		fprintf(stderr,"Trailing argument: %s\n",argv[optind]);
		usage(argv[0],-1);
	}
	if(user == NULL){
		user = DEFAULT_USERNAME;
	}
	if(pctx->ianafn == NULL){
		pctx->ianafn = DEFAULT_IANA_FILENAME;
	}
	if(pctx->resolvconf == NULL){
		pctx->resolvconf = DEFAULT_RESOLVCONF_FILENAME;
	}
	// Drop privileges (possibly requiring a setuid()), and mask
	// cancellation signals, before creating other threads.
	if(pctx->pcapfn){
		if(handle_priv_drop(user,NULL,0)){
			return -1;
		}
	}else{
		if(handle_priv_drop(user,caparray,sizeof(caparray) / sizeof(*caparray))){
			return -1;
		}
	}
	if(init_pcap(pctx)){
		return -1;
	}
	if(init_interfaces()){
		return -1;
	}
	// We unmask the cancellation signals in the packet socket thread
	if(mask_cancel_sigs(NULL)){
		return -1;
	}
	pctx->iface.diagnostic = default_diagnostic;
	if(pctx->ianafn && strcmp(pctx->ianafn,"")){
		if(init_iana_naming(&pctx->iface,pctx->ianafn)){
			return -1;
		}
	}
	if(pctx->resolvconf && strcmp(pctx->resolvconf,"")){
		if(init_naming(&pctx->iface,pctx->resolvconf)){
			return -1;
		}
	}
	return 0;
}

int omphalos_init(const omphalos_ctx *pctx){
	if(pctx->iface.diagnostic == NULL){
		fprintf(stderr,"No diagnostic callback function defined, exiting\n");
		return -1;
	}
	if(pctx->pcapfn){
		if(handle_pcap_file(pctx)){
			return -1;
		}
	}else{
		if(init_pci_support()){
			pctx->iface.diagnostic("Warning: no PCI support available");
		}
		if(init_usb_support()){
			return -1;
		}
		if(handle_netlink_socket(pctx)){
			return -1;
		}
	}
	return 0;
}

void omphalos_cleanup(const omphalos_ctx *pctx){
	cleanup_interfaces(&pctx->iface);
	free_routes();
	cleanup_pcap(pctx);
	cleanup_iana_naming();
	stop_pci_support();
	stop_usb_support();
	cleanup_naming(&pctx->iface);
}
