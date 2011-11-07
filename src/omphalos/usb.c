#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <omphalos/usb.h>
#include <omphalos/inotify.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

/* LibUSB 1.0 implementation. Unused -- LibUSB is unsuitable for inclusion.
#include <libusb-1.0/libusb.h>
static libusb_context *usb;

int init_usb_support(void){
	if(libusb_init(&usb)){
		return -1;
	}
	return 0;
}

int stop_usb_support(void){
	if(usb){
		libusb_exit(usb);
		usb = NULL;
	}
	return 0;
}

static int
get_ul_token(const char **str,char tok,unsigned long *ul){
	char *e;

	if(**str == '-'){ // guard against strotul() '-'-parsing behavior
		return -1;
	}
	if((*ul = strtoul(*str,&e,16)) == ULONG_MAX && errno == ERANGE){
		return -1;
	}
	if(*e != tok){
		return -1;
	}
	*str = e + 1;
	return 0;
}

// feed it the bus id as provided by libsysfs
static libusb_device_handle *
lookup(const char *busid,struct libusb_device_descriptor *desc){
	libusb_device_handle *handle;
	unsigned long bus,dev,port;
	libusb_device **list;
	const char *cur;
	ssize_t s;

	cur = busid; // sysfs form: bus-port.dev:x.y
	if(get_ul_token(&cur,'-',&bus) || get_ul_token(&cur,'.',&port) ||
			get_ul_token(&cur,':',&dev)){
		return NULL;
	}
	// Error, or no USB devices connected to the system. Since we were
	// triggered off a discovered device, following a bus lookup via sysfs,
	// there certainly ought be a USB device connected.
	if((s = libusb_get_device_list(usb,&list)) <= 0){
		return NULL;
	}
	handle = NULL;
	while(--s >= 0){
		if(libusb_get_bus_number(list[s]) != bus){
			continue;
		}
		if(libusb_get_device_address(list[s]) != dev){
			continue;
		}
		if(libusb_get_device_descriptor(list[s],desc)){
			break;
		}
		if(libusb_open(list[s],&handle)){
			break;
		}
		break;
	}
	libusb_free_device_list(list,1);
	return handle;
}

int find_usb_device(const char *busid,struct sysfs_device *sd __attribute__ ((unused)),
				topdev_info *tinf){
	struct libusb_device_descriptor desc;
	libusb_device_handle *handle;
	unsigned char buf[128]; // FIXME
	int l;

	if((handle = lookup(busid,&desc)) == NULL){
		return -1;
	}
	if((l = libusb_get_string_descriptor_ascii(handle,desc.iManufacturer,buf,sizeof(buf))) <= 0){
		libusb_close(handle);
		return -1;
	}
	buf[l] = ' ';
	if((l = libusb_get_string_descriptor_ascii(handle,desc.iProduct,buf + l,sizeof(buf) - l)) <= 0){
		libusb_close(handle);
		return -1;
	}
	libusb_close(handle);
	if((tinf->devname = strdup((const char *)buf)) == NULL){
		return -1;
	}
	return 0;
}
*/

static int
parse_usbids_file(const omphalos_iface *octx,const char *fn){
	FILE *fp;

	if((fp = fopen(fn,"r")) == NULL){
		octx->diagnostic(L"Couldn't open USB ID db at %s (%s?)",fn,strerror(errno));
		return -1;
	}
	if(fclose(fp)){
		octx->diagnostic(L"Couldn't close USB ID db at %s (%s?)",fn,strerror(errno));
		return -1;
	}
	return 0;
}

// USB ID database implementation
int init_usb_support(const omphalos_iface *octx,const char *fn){
	if(watch_file(octx,fn,parse_usbids_file)){
		return -1;
	}
	return 0;
}

int stop_usb_support(void){
	return 0;
}

// libsysfs implementation
#include <sysfs/libsysfs.h>

int find_usb_device(const char *busid __attribute__ ((unused)),
		struct sysfs_device *sd,topdev_info *tinf){
	struct sysfs_attribute *attr;
	struct sysfs_device *parent;
	char *tmp;

	if((parent = sysfs_get_device_parent(sd)) == NULL){
		return -1;
	}
	if((attr = sysfs_get_device_attr(parent,"manufacturer")) == NULL){
		return -1;
	}
	if((tinf->devname = strdup(attr->value)) == NULL){
		return -1;
	}
	if((attr = sysfs_get_device_attr(parent,"product")) == NULL){
		free(tinf->devname);
		tinf->devname = NULL;
		return -1;
	}
	if((tmp = realloc(tinf->devname,strlen(tinf->devname) + strlen(attr->value) + 2)) == NULL){
		free(tinf->devname);
		tinf->devname = NULL;
		return -1;
	}
	// They come with a newline at the end, argh!
	tmp[strlen(tmp) - 1] = ' ';
	strcat(tmp,attr->value);
	tmp[strlen(tmp) - 1] = '\0';
	tinf->devname = tmp;
	return 0;
}
