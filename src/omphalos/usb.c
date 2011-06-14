#include <usb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omphalos/usb.h>
#include <omphalos/interface.h>

int init_usb_support(void){
	return 0;
}

int stop_usb_support(void){
	return 0;
}

// feed it the bus id as provided by libsysfs
int find_usb_device(const char *busid,topdev_info *tinf){
	if(!busid || !tinf){
		return -1;
	} // FIXME filler
	return 0;
}
