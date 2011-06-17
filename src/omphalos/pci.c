#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pci/pci.h>
#include <omphalos/pci.h>
#include <omphalos/interface.h>

static struct pci_access *pci; // FIXME

int init_pci_support(void){
	if((pci = pci_alloc()) == NULL){
		return -1;
	}
	pci_init(pci);
	pci_scan_bus(pci);
	return 0;
}

int stop_pci_support(void){
	if(pci){
		pci_cleanup(pci);
		pci = NULL;
	}
	return 0;
}

// feed it the bus id as provided by libsysfs
int find_pci_device(const char *busid,topdev_info *tinf){
	unsigned long domain,bus,dev,func;
	struct pci_dev *d;
	const char *cur;
	char *e;

	// FIXME clean this cut-and-paste crap up
	cur = busid;
	if(*cur == '-'){ // strtoul() admits leading negations
		return -1;
	}
	if((domain = strtoul(cur,&e,16)) == ULONG_MAX){
		return -1;
	}
	if(*e != ':'){
		return -1;
	}
	cur = e + 1;
	if(*cur == '-'){ // strtoul() admits leading negations
		return -1;
	}
	if((bus = strtoul(cur,&e,16)) == ULONG_MAX){
		return -1;
	}
	if(*e != ':'){
		return -1;
	}
	cur = e + 1;
	if(*cur == '-'){ // strtoul() admits leading negations
		return -1;
	}
	if((dev = strtoul(cur,&e,16)) == ULONG_MAX){
		return -1;
	}
	if(*e != '.'){
		return -1;
	}
	cur = e + 1;
	if(*cur == '-'){ // strtoul() admits leading negations
		return -1;
	}
	if((func = strtoul(cur,&e,16)) == ULONG_MAX){
		return -1;
	}
	if(*e){
		return -1;
	}
	for(d = pci->devices ; d ; d = d->next){
		if(d->domain == domain && d->bus == bus && d->dev == dev && d->func == func){
			char buf[80]; // FIXME
			if(pci_lookup_name(pci,buf,sizeof(buf),PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
						d->vendor_id,d->device_id)){
				if( (tinf->devname = strdup(buf)) ){
					return 0;
				}
			}
		}
	}
	return -1;
}
