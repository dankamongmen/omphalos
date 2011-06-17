#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <omphalos/usb.h>
#include <libusb-1.0/libusb.h>
#include <omphalos/interface.h>

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

static struct libusb_device_handle *
lookup(libusb_context *ctx,const char *busid){
	libusb_device_handle *handle = NULL;
	unsigned long bus,dev,port;
	libusb_device **list;
	const char *cur;
	ssize_t s;

	cur = busid;
	if(get_ul_token(&cur,'-',&bus) || get_ul_token(&cur,'.',&port) ||
		get_ul_token(&cur,':',&dev)){
		return NULL;
	}
	// Error, or no USB devices connected to the system. Since we were
	// triggered off a discovered device, following a bus lookup via sysfs,
	// there certainly ought be a USB device connected.
	if((s = libusb_get_device_list(ctx,&list)) <= 0){
		return NULL;
	}
	//1-1.1:1.0
	libusb_free_device_list(list,1);
	return handle;
}

// feed it the bus id as provided by libsysfs
int find_usb_device(const char *busid,topdev_info *tinf){
	struct libusb_device_handle *handle;

	if((handle = lookup(usb,busid)) == NULL){
		return -1;
	}
	if(!busid || !tinf){
		return -1;
	} // FIXME filler
	return 0;
}
