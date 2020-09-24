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

// Consults udev's database, followed by USB/PCI IDs. Returns NULL,
// "usb", or "pci".
const char *lookup_bus(int netdevid, struct topdev_info *tinf);

#ifdef __cplusplus
}
#endif

#endif
