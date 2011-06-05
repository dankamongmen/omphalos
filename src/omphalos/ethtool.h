#ifndef OMPHALOS_ETHTOOL
#define OMPHALOS_ETHTOOL

#ifdef __cplusplus
extern "C" {
#endif

struct ethtool_cmd;
struct ethtool_drvinfo;
struct omphalos_iface;

int iface_driver_info(const struct omphalos_iface *,const char *,struct ethtool_drvinfo *);
int iface_ethtool_info(const struct omphalos_iface *,const char *,struct ethtool_cmd *);
int iface_offload_info(const struct omphalos_iface *,const char *,unsigned *,unsigned *);

#ifdef __cplusplus
}
#endif

#endif
