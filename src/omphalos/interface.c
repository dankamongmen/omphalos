#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <linux/if_arp.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/interface.h>

#define MAXINTERFACES (1u << 16) // lame FIXME

static interface interfaces[MAXINTERFACES];

int init_interfaces(void){
	unsigned i;

	for(i = 0 ; i < sizeof(interfaces) / sizeof(*interfaces) ; ++i){
		interface *iface = &interfaces[i];

		iface->fd = -1;
	}
	return 0;
}

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

// we wouldn't naturally want to use signed integers, but that's the api...
interface *iface_by_idx(int idx){
	if(idx < 0 || (unsigned)idx >= sizeof(interfaces) / sizeof(*interfaces)){
		return NULL;
	}
	return &interfaces[idx];
}

char *hwaddrstr(const interface *i){
	return l2addrstr(i->addr,i->addrlen);
}

void free_iface(interface *i){
	free(i->name);
	free(i->addr);
}

void cleanup_interfaces(void){
	unsigned i;

	for(i = 0 ; i < sizeof(interfaces) / sizeof(*interfaces) ; ++i){
		free_iface(&interfaces[i]);
	}
}

int print_all_iface_stats(FILE *fp,interface *agg){
	unsigned i;

	for(i = 0 ; i < sizeof(interfaces) / sizeof(*interfaces) ; ++i){
		const interface *iface = &interfaces[i];

		if(iface->pkts){
			if(print_iface_stats(fp,iface,agg,"iface") < 0){
				return -1;
			}
		}
	}
	return 0;
}
