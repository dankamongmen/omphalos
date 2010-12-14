#include <pcap.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/ethernet.h>
#include <omphalos/psocket.h>

static void
usage(const char *arg0,int ret){
	fprintf(stderr,"usage: %s [ -f filename ]\n",arg0);
	exit(ret);
}

int main(int argc,char * const *argv){
	pcap_t *pcap = NULL;
	int opt;

	opterr = 0; // suppress getopt() diagnostic to stderr
	while((opt = getopt(argc,argv,":f:")) >= 0){
		switch(opt){
		case 'f':{
			char ebuf[PCAP_ERRBUF_SIZE];
			const char *fn = optarg;

			if(pcap){
				fprintf(stderr,"Provided -f twice\n");
				usage(argv[0],EXIT_FAILURE);
			}
			if((pcap = pcap_open_offline(fn,ebuf)) == NULL){
				fprintf(stderr,"Couldn't open %s (%s?)\n",fn,ebuf);
				return EXIT_FAILURE;
			}
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
	if(pcap){
		pcap_close(pcap);
	}else{
		void *txm,*rxm;
		size_t ts,rs;
		int tfd,rfd;

		if((rfd = packet_socket(ETH_P_ALL)) < 0){
			return EXIT_FAILURE;
		}
		if((tfd = packet_socket(ETH_P_ALL)) < 0){
			return EXIT_FAILURE;
		}
		if((rs = mmap_rx_psocket(rfd,&rxm)) == 0){
			close(rfd);
			close(tfd);
			return EXIT_FAILURE;
		}
		if((ts = mmap_tx_psocket(tfd,&txm)) == 0){
			unmap_psocket(rxm,rs);
			close(rfd);
			close(tfd);
			return EXIT_FAILURE;
		}
		if(unmap_psocket(txm,ts)){
			unmap_psocket(rxm,rs);
			close(rfd);
			close(tfd);
			return EXIT_FAILURE;
		}
		if(unmap_psocket(rxm,rs)){
			close(rfd);
			close(tfd);
			return EXIT_FAILURE;
		}
		if(close(rfd)){
			fprintf(stderr,"Couldn't close packet socket %d (%s?)\n",rfd,strerror(errno));
			close(tfd);
			return EXIT_FAILURE;
		}
		if(close(tfd)){
			fprintf(stderr,"Couldn't close packet socket %d (%s?)\n",tfd,strerror(errno));
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
