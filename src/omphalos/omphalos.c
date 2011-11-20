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
#include <omphalos/procfs.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netlink.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

#define DEFAULT_USERNAME "nobody"
#define DEFAULT_PROCROOT "/proc/"
#define DEFAULT_MODESTRING "active"
#define DEFAULT_IANA_FILENAME "ieee-oui.txt"	// arp-scan's 'get-oui'
#define DEFAULT_USBIDS_FILENAME "usb.ids"	// usbutils' 'update-usbids'
#define DEFAULT_RESOLVCONF_FILENAME "/etc/resolv.conf"

pthread_key_t omphalos_ctx_key;

const omphalos_ctx *get_octx(void){
	return pthread_getspecific(omphalos_ctx_key);
}

static void
usage(const char *arg0,int ret){
	FILE *fp = ret == EXIT_SUCCESS ? stdout : stderr;

	fprintf(fp,"usage: %s [ options... ]\n",basename(arg0));
	fprintf(fp,"\noptions:\n");
	fprintf(fp,"-h print this help, and exit\n");
	fprintf(fp,"-p do not make newly-discovered devices promiscuous\n");
	fprintf(fp,"--version print version info, and exit\n");
	fprintf(fp,"-u username: user name to take after creating packet socket.\n");
	fprintf(fp,"\t'%s' by default. provide empty string to disable.\n",DEFAULT_USERNAME);
	fprintf(fp,"-f filename: libpcap-format save file for input.\n");
	fprintf(fp,"--mode silent|stealthy|active|aggressive|forceful|hostile\n");
	fprintf(fp,"\t'%s' by default. See documentation for details.\n",DEFAULT_MODESTRING);
	fprintf(fp,"--usbids filename: USB ID Repository (http://www.linux-usb.org/usb-ids.html).\n");
	fprintf(fp,"\t'%s' by default. provide empty string to disable.\n",DEFAULT_USBIDS_FILENAME);
	fprintf(fp,"--ouis filename: IANA's OUI mapping in get-oui(1) format.\n");
	fprintf(fp,"\t'%s' by default. provide empty string to disable.\n",DEFAULT_IANA_FILENAME);
	fprintf(fp,"--resolv filename: resolv.conf-format nameserver list.\n");
	fprintf(fp,"\t'%s' by default. provide empty string to disable.\n",DEFAULT_RESOLVCONF_FILENAME);
	fprintf(fp,"--plog filename: Enable malformed packet logging to this file.\n");
	exit(ret);
}

typedef enum {
	OMPHALOS_MODE_NONE,
	OMPHALOS_MODE_SILENT,
	OMPHALOS_MODE_STEALTHY,
	OMPHALOS_MODE_ACTIVE,
	OMPHALOS_MODE_AGGRESSIVE,
	OMPHALOS_MODE_FORCEFUL,
	OMPHALOS_MODE_HOSTILE
} omphalos_mode_enum;

static const struct omphalos_mode {
	omphalos_mode_enum level;
	const char *str;	
} omphalos_modes[] = {
	{ OMPHALOS_MODE_SILENT,		"silent",	},
	{ OMPHALOS_MODE_STEALTHY,	"stealthy",	},
	{ OMPHALOS_MODE_ACTIVE,		"active",	},
	{ OMPHALOS_MODE_AGGRESSIVE,	"aggressive",	},
	{ OMPHALOS_MODE_FORCEFUL,	"forceful",	},
	{ OMPHALOS_MODE_HOSTILE,	"hostile",	},
	{ OMPHALOS_MODE_NONE,		NULL,		}
};

static omphalos_mode_enum
lex_omphalos_mode(const char *str){
	const typeof(*omphalos_modes) *m;

	for(m = omphalos_modes ; m->str ; ++m){
		if(strcmp(str,m->str) == 0){
			return m->level;
		}
	}
	return OMPHALOS_MODE_NONE;
}

static void
version(const char *arg0){
	fprintf(stdout,"%s %s\n",PROGNAME,VERSION);
	fprintf(stdout,"invoked as %s\n",arg0);
	exit(EXIT_SUCCESS);
}

static inline void
default_vdiagnostic(const wchar_t *fmt,va_list v){
	assert(vfwprintf(stderr,fmt,v) >= 0);
	assert(fputwc(L'\n',stderr) != WEOF);
}

static void
default_diagnostic(const wchar_t *fmt,...){
	va_list va;

	va_start(va,fmt);
	default_vdiagnostic(fmt,va);
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
	OPT_USBIDS,
	OPT_VERSION,
	OPT_PLOG,
	OPT_RESOLV,
	OPT_MODE,
};

int omphalos_setup(int argc,char * const *argv,omphalos_ctx *pctx){
	static const struct option ops[] = {
		{
			.name = "ouis",
			.has_arg = 1,
			.flag = NULL,
			.val = OPT_OUIS,
		},{
			.name = "usbids",
			.has_arg = 1,
			.flag = NULL,
			.val = OPT_USBIDS,
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
		},{
			.name = "mode",
			.has_arg = 1,
			.flag = NULL,
			.val = OPT_MODE,
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
	const char *user = NULL,*mode = NULL;
	int opt,longidx;
	
	memset(pctx,0,sizeof(*pctx));
	opterr = 0; // suppress getopt() diagnostic to stderr
	while((opt = getopt_long(argc,argv,":hf:u:p",ops,&longidx)) >= 0){
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
		}case OPT_USBIDS:{
			if(pctx->usbidsfn){
				fprintf(stderr,"Provided --usbids twice\n");
				usage(argv[0],EXIT_FAILURE);
			}
			pctx->usbidsfn = optarg;
			break;
		}case OPT_RESOLV:{
			if(pctx->resolvconf){
				fprintf(stderr,"Provided --resolv twice\n");
				usage(argv[0],EXIT_FAILURE);
			}
			pctx->resolvconf = optarg;
			break;
		}case OPT_MODE:{
			if(mode){
				fprintf(stderr,"Provided --mode twice\n");
				usage(argv[0],EXIT_FAILURE);
			}
			mode = optarg;
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
		}case 'p':{
			if(pctx->nopromiscuous){
				fprintf(stderr,"Provided %c twice\n",opt);
				usage(argv[0],EXIT_FAILURE);
			}
			pctx->nopromiscuous = 1;
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
		return -1;
	}
	if(user == NULL){
		user = DEFAULT_USERNAME;
	}
	if(pctx->ianafn == NULL){
		pctx->ianafn = DEFAULT_IANA_FILENAME;
	}
	if(pctx->usbidsfn == NULL){
		pctx->usbidsfn = DEFAULT_USBIDS_FILENAME;
	}
	if(pctx->resolvconf == NULL){
		pctx->resolvconf = DEFAULT_RESOLVCONF_FILENAME;
	}
	if(mode == NULL){
		mode = DEFAULT_MODESTRING;
	}
	if((pctx->mode = lex_omphalos_mode(mode)) == OMPHALOS_MODE_NONE){
		fprintf(stderr,"Invalid operating mode: %s\n",mode);
		usage(argv[0],-1);
		return -1;
	}
	printf("Operating mode: %s\n",mode);
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
	pctx->iface.vdiagnostic = default_vdiagnostic;
	if(pthread_key_create(&omphalos_ctx_key,NULL)){
		return -1;
	}
	if(pthread_setspecific(omphalos_ctx_key,pctx)){
		return -1;
	}
	if(init_procfs(DEFAULT_PROCROOT)){
		return -1;
	}
	if(strcmp(pctx->usbidsfn,"")){
		if(init_usb_support(pctx->usbidsfn)){
			return -1;
		}
	}
	if(strcmp(pctx->ianafn,"")){
		if(init_iana_naming(pctx->ianafn)){
			return -1;
		}
	}
	if(strcmp(pctx->resolvconf,"")){
		if(init_naming(pctx->resolvconf)){
			return -1;
		}
	}
	return 0;
}

int omphalos_init(const omphalos_ctx *pctx){
	if(pctx->iface.diagnostic == NULL){
		fwprintf(stderr,L"No diagnostic callback function defined, exiting\n");
		return -1;
	}
	if(pthread_setspecific(omphalos_ctx_key,pctx)){
		return -1;
	}
	if(pctx->pcapfn){
		if(handle_pcap_file(pctx)){
			return -1;
		}
	}else{
		if(init_pci_support()){
			diagnostic(L"Warning: no PCI support available");
		}
		if(handle_netlink_socket()){
			return -1;
		}
	}
	return 0;
}

void omphalos_cleanup(const omphalos_ctx *pctx){
	cleanup_naming();
	free_routes();
	cleanup_interfaces();
	cleanup_pcap(pctx);
	cleanup_iana_naming();
	stop_pci_support();
	stop_usb_support();
	cleanup_procfs();
	destroy_interfaces();
	pthread_key_delete(omphalos_ctx_key);
}
