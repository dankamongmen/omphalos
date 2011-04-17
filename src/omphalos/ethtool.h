#ifndef OMPHALOS_ETHTOOL
#define OMPHALOS_ETHTOOL

#ifdef __cplusplus
extern "C" {
#endif

struct ethtool_drvinfo;

int iface_driver_info(const char *,struct ethtool_drvinfo *);

#ifdef __cplusplus
}
#endif

#endif
