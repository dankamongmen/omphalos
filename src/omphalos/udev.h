#ifndef OMPHALOS_UDEV
#define OMPHALOS_UDEV

#ifdef __cplusplus
extern "C" {
#endif

struct topdev_info;

// This manages use of a usb.ids file to map vendor/device IDs to strings.
// These are generally much better names than those available through sysfs.
// Manufacturer and Product id's from sysfs will be used as a fallback for
// entries absent from the USB ID database. We talk to sysfs via libudev.
int init_udev_support(const char *fn);
int stop_udev_support(void);

// Will first consult the USB ID database, then sysfs.
int find_net_device(int netdevid, struct topdev_info* tinf);

#ifdef __cplusplus
}
#endif

#endif
