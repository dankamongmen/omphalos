#include <stdio.h>
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
	libusb_exit(usb);
	usb = NULL;
	return 0;
}

// feed it the bus id as provided by libsysfs
int find_usb_device(const char *busid,topdev_info *tinf){
	if(!busid || !tinf){
		return -1;
	} // FIXME filler
	return 0;
}
