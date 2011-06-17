#ifndef OMPHALOS_ETHTOOL
#define OMPHALOS_ETHTOOL

#ifdef __cplusplus
extern "C" {
#endif

#define RX_CSUM_OFFLOAD		0x0001
#define TX_CSUM_OFFLOAD		0x0002
#define ETH_SCATTER_GATHER	0x0004
#define TCP_SEG_OFFLOAD		0x0008
#define UDP_FRAG_OFFLOAD	0x0010
#define GEN_SEG_OFFLOAD		0x0020
#define GENRX_OFFLOAD		0x0040
#define LARGERX_OFFLOAD		0x0080
#define TXVLAN_OFFLOAD		0x0100
#define RXVLAN_OFFLOAD		0x0200
#define NTUPLE_FILTERS		0x0400
#define RXPATH_HASH		0x0800

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
