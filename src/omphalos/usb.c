#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <omphalos/usb.h>
#include <omphalos/interface.h>

/* LibUSB 1.0 implementation
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

// libsysfs implementation
#include <sysfs/libsysfs.h>

int init_usb_support(void){
	return 0;
}

int stop_usb_support(void){
	return 0;
}

int find_usb_device(const char *busid __attribute__ ((unused)),
		struct sysfs_device *sd,topdev_info *tinf){
	struct dlist *dl;

	if(!tinf){ // FIXME kill
		return -1;
	}
	if((dl = sysfs_get_device_attributes(sd)) == NULL){
		return -1;
	}
	dlist_destroy(dl);
	return 0;
}
