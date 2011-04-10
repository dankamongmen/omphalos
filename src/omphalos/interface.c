#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <linux/if_arp.h>
#include <omphalos/interface.h>

#define STAT(fp,i,x) if((i)->x) { if(fprintf((fp),"<"#x">%ju</"#x">",(i)->x) < 0){ return -1; } }
int print_iface_stats(FILE *fp,const interface *i,interface *agg,const char *decorator){
	if(i->name == NULL){
		if(fprintf(fp,"<%s>",decorator) < 0){
			return -1;
		}
	}else{
		if(fprintf(fp,"<%s name=\"%s\">",decorator,i->name) < 0){
			return -1;
		}
	}
	STAT(fp,i,pkts);
	STAT(fp,i,truncated);
	STAT(fp,i,noprotocol);
	STAT(fp,i,malformed);
	if(fprintf(fp,"</%s>",decorator) < 0){
		return -1;
	}
	if(agg){
		agg->pkts += i->pkts;
	}
	return 0;
}
#undef STAT

char *hwaddrstr(const interface *i){
	unsigned idx;
	size_t s;
	char *r;

	// Each byte becomes two ASCII characters and either a separator or a nul
	s = i->addrlen * 3;
	if( (r = malloc(s)) ){
		for(idx = 0 ; idx < i->addrlen ; ++idx){
			snprintf(r + idx * 3,s - idx * 3,"%02x:",((unsigned char *)i->addr)[idx]);
		}
	}
	return r;
}

void free_iface(interface *i){
	free(i->name);
	free(i->addr);
}
