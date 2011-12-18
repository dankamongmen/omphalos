#ifndef OMPHALOS_ETHTOOL
#define OMPHALOS_ETHTOOL

#ifdef __cplusplus
extern "C" {
#endif

#include <omphalos/interface.h>

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

struct ethtool_cmd;
struct ethtool_drvinfo;

int iface_driver_info(const char *,struct ethtool_drvinfo *);
int iface_ethtool_info(const char *,struct ethtool_cmd *);
int iface_offload_info(const char *,unsigned *,unsigned *);
int iface_offloaded_p(const interface *,unsigned);

// Check for LRO/GRO/GSO use on the interface -- if they're active, we want to
// use RX frames larger than the MTU.
static inline int
iface_uses_offloading(const interface *i){
	return iface_offloaded_p(i,GENRX_OFFLOAD) > 0 ||
		iface_offloaded_p(i,GEN_SEG_OFFLOAD) > 0 ||
		iface_offloaded_p(i,TCP_SEG_OFFLOAD) > 0 ||
		iface_offloaded_p(i,LARGERX_OFFLOAD) > 0;
}

#ifdef __cplusplus
}
#endif

#endif
