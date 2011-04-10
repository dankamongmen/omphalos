#include <stdio.h>
#include <omphalos/interface.h>

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
	if(i->pkts){
		if(fprintf(fp,"<frames>%ju</frames>",i->pkts) < 0){
			return -1;
		}
	}
	if(fprintf(fp,"</%s>",decorator) < 0){
		return -1;
	}
	if(agg){
		agg->pkts += i->pkts;
	}
	return 0;
}
