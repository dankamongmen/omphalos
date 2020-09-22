#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <pciaccess.h>
#include <omphalos/pci.h>
#include <omphalos/interface.h>

static int
parse_pci_busid(const char *busid,unsigned long *domain,unsigned long *bus,
				unsigned long *dev,unsigned long *func){
	const char *cur;
	char *e;

	// FIXME clean this cut-and-paste crap up
	cur = busid;
	if(*cur == '-'){ // strtoul() admits leading negations
		return -1;
	}
	if((*domain = strtoul(cur,&e,16)) == ULONG_MAX){
		return -1;
	}
	if(*e != ':'){
		return -1;
	}
	cur = e + 1;
	if(*cur == '-'){ // strtoul() admits leading negations
		return -1;
	}
	if((*bus = strtoul(cur,&e,16)) == ULONG_MAX){
		return -1;
	}
	if(*e != ':'){
		return -1;
	}
	cur = e + 1;
	if(*cur == '-'){ // strtoul() admits leading negations
		return -1;
	}
	if((*dev = strtoul(cur,&e,16)) == ULONG_MAX){
		return -1;
	}
	if(*e != '.'){
		return -1;
	}
	cur = e + 1;
	if(*cur == '-'){ // strtoul() admits leading negations
		return -1;
	}
	if((*func = strtoul(cur,&e,16)) == ULONG_MAX){
		return -1;
	}
	if(*e){
		return -1;
	}
	return 0;
}

int init_pci_support(void){
	if(pci_system_init()){
		return -1;
	}
	return 0;
}

int stop_pci_support(void){
	pci_system_cleanup();
	return 0;
}

int find_pci_device(const char *busid,struct sysfs_device *sd __attribute__ ((unused)),
				topdev_info *tinf){
	unsigned long domain,bus,dev,func;
	const char *vend,*devname;
	struct pci_device *pci;
	size_t vendlen,devlen;
	mbstate_t mb;

	if(parse_pci_busid(busid,&domain,&bus,&dev,&func)){
		return -1;
	}
	if((pci = pci_device_find_by_slot(domain,bus,dev,func)) == NULL){
		return -1;
	}
	if((vend = pci_device_get_vendor_name(pci)) == NULL){
		vend = "Unknown vendor";
	}
	vendlen = strlen(vend);
	if((devname = pci_device_get_device_name(pci)) == NULL){
		devname = "Unknown device";
	}
	devlen = strlen(devname);
	if((tinf->devname = malloc(sizeof(wchar_t) * (vendlen + devlen + 2))) == NULL){
		return -1;
	}
	memset(&mb,0,sizeof(mb));
	assert(mbsrtowcs(tinf->devname,&vend,vendlen,&mb) == vendlen);
	tinf->devname[vendlen] = L' ';
	assert(mbsrtowcs(tinf->devname + vendlen + 1,&devname,devlen,&mb) == devlen);
	tinf->devname[vendlen + devlen + 1] = L'\0';
	return 0;
}
