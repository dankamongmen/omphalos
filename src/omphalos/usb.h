#ifndef OMPHALOS_USB
#define OMPHALOS_USB

#ifdef __cplusplus
extern "C" {
#endif

struct topdev_info;
struct sysfs_device;

int init_usb_support(void);
int stop_usb_support(void);

int find_usb_device(const char *,struct sysfs_device *,struct topdev_info *);

#ifdef __cplusplus
}
#endif

#endif
