#ifndef OMPHALOS_USB
#define OMPHALOS_USB

#ifdef __cplusplus
extern "C" {
#endif

struct topdev_info;
struct sysfs_device;

// This manages use of a usb.ids file to map vendor/device id's to strings.
// These are generally much better names than those available through sysfs.
// Manufacturer and Product id's from sysfs will be used as a fallback for
// entries absent from the USB ID database.
int init_usb_support(const char *fn);
int stop_usb_support(void);

// Will first consult the USB ID database, then sysfs.
int find_usb_device(const char *,struct sysfs_device *,struct topdev_info *);

#ifdef __cplusplus
}
#endif

#endif
