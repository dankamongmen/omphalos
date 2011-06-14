#ifndef OMPHALOS_USB
#define OMPHALOS_USB

#ifdef __cplusplus
extern "C" {
#endif

struct topdev_info;

int init_usb_support(void);
int stop_usb_support(void);

// feed it the bus id as provided by libsysfs
int find_usb_device(const char *,struct topdev_info *);

#ifdef __cplusplus
}
#endif

#endif
