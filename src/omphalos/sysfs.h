#ifndef OMPHALOS_SYSFS
#define OMPHALOS_SYSFS

#ifdef __cplusplus
extern "C" {
#endif

struct topdev_info;

const char *lookup_bus(int netdevid, struct topdev_info *);

#ifdef __cplusplus
}
#endif

#endif
