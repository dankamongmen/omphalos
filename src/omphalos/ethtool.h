#ifndef OMPHALOS_ETHTOOL
#define OMPHALOS_ETHTOOL

#ifdef __cplusplus
extern "C" {
#endif

#define RX_CSUM_OFFLOAD		0x01
#define TX_CSUM_OFFLOAD		0x02
#define ETH_SCATTER_GATHER	0x04
#define TCP_SEG_OFFLOAD		0x08
#define UDP_FRAG_OFFLOAD	0x10
#define GEN_SEG_OFFLOAD		0x20
#define GENRX_OFFLOAD		0x40
#define LARGERX_OFFLOAD		0x80

struct interface;
struct ethtool_cmd;
struct ethtool_drvinfo;
struct omphalos_iface;

int iface_driver_info(const struct omphalos_iface *,const char *,struct ethtool_drvinfo *);
int iface_ethtool_info(const struct omphalos_iface *,const char *,struct ethtool_cmd *);
int iface_offload_info(const struct omphalos_iface *,const char *,unsigned *,unsigned *);
int iface_offloaded_p(const struct interface *,unsigned);

#ifdef __cplusplus
}
#endif

#endif
