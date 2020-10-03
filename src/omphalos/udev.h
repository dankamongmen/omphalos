#ifndef OMPHALOS_UDEV
#define OMPHALOS_UDEV

#ifdef __cplusplus
extern "C" {
#endif

struct topdev_info;

// Manufacturer and Product id's from sysfs will be used as a fallback for
// entries absent from the udev ID database. We talk to sysfs via libudev.
int init_udev_support(void);
int stop_udev_support(void);

// Consults udev's database, followed by USB/PCI IDs. Returns NULL,
// "usb", or "pci".
char *lookup_bus(int netdevid, struct topdev_info *tinf);

#ifdef __cplusplus
}
#endif

#endif
