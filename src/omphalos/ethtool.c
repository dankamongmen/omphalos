#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <linux/version.h>
#include <linux/ethtool.h>
#include <omphalos/ethtool.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

static inline int
ethtool_docmd(const omphalos_iface *octx,const char *name,void *unsafe){
	struct ifreq ifr;
	int fd;

	if(strlen(name) >= sizeof(ifr.ifr_name)){
		octx->diagnostic("Bad name: %s",name);
		return -1;
	}
	memset(&ifr,0,sizeof(&ifr));
	strcpy(ifr.ifr_name,name);
	ifr.ifr_data = (caddr_t)unsafe;
	if((fd = socket(AF_INET,SOCK_DGRAM,0)) < 0){
		octx->diagnostic("Couldn't open ethtool fd (%s?)",strerror(errno));
		return -1;
	}
	if(ioctl(fd,SIOCETHTOOL,&ifr)){ // no diagnostic here; specialize
		close(fd);
		return -1;
	}
	if(close(fd)){
		octx->diagnostic("Couldn't close ethtool fd %d (%s?)",fd,strerror(errno));
		return -1;
	}
	return 0;
}

int iface_driver_info(const omphalos_iface *octx,const char *name,struct ethtool_drvinfo *drv){
	drv->cmd = ETHTOOL_GDRVINFO;
	if(ethtool_docmd(octx,name,drv)){
		return -1;
	}
	// Some return the empty string for firmware / bus, others "N/A".
	// Normalize them here.
	if(strcmp(drv->fw_version,"N/A") == 0){
		drv->fw_version[0] = '\0';
	}
	if(strcmp(drv->bus_info,"N/A") == 0){
		drv->bus_info[0] = '\0';
	}
	return 0;
}

static const struct offload_info {
	const char *desc;
	unsigned mask;
	int op;
} offload_infos[] = {
	{
		.desc = "RX checksum offload",
		.mask = RX_CSUM_OFFLOAD,
		.op = ETHTOOL_GRXCSUM,
	},{
		.desc = "TX checksum offload",
		.mask = TX_CSUM_OFFLOAD,
		.op = ETHTOOL_GTXCSUM,
	},{
		.desc = "Scatter/gather I/O",
		.mask = ETH_SCATTER_GATHER,
		.op = ETHTOOL_GSG,
	},{
		.desc = "TCP segmentation offload",
		.mask = TCP_SEG_OFFLOAD,
		.op = ETHTOOL_GTSO,
	},{
		.desc = "UDP fragmentation offload",
		.mask = UDP_FRAG_OFFLOAD,
		.op = ETHTOOL_GUFO,
	},{
		.desc = "Generic segmentation offload",
		.mask = GEN_SEG_OFFLOAD,
		.op = ETHTOOL_GGSO,
	},{
		.desc = "Generic RX offload",
		.mask = GENRX_OFFLOAD,
		.op = ETHTOOL_GGRO,
	},
	{ .desc = NULL, .mask = 0, .op = -1, }
};

static const struct offload_flags_info {
	const char *desc;
	unsigned mask;
	int flag;
} offload_flags[] = {
	{
		.desc = "Large RX offload",
		.mask = LARGERX_OFFLOAD,
		.flag = ETH_FLAG_LRO,
	},{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,37)
		.desc = "TX VLAN offload",
		.mask = TXVLAN_OFFLOAD,
		.flag = ETH_FLAG_TXVLAN,
	},{
		.desc = "RX VLAN offload",
		.mask = RXVLAN_OFFLOAD,
		.flag = ETH_FLAG_RXVLAN,
	},{
#endif
		.desc = "N-tuple filters",
		.mask = NTUPLE_FILTERS,
		.flag = ETH_FLAG_NTUPLE,
	},{
		.desc = "RX path hashing",
		.mask = RXPATH_HASH,
		.flag = ETH_FLAG_RXHASH,
	},
	{ .desc = NULL, .mask = 0, .flag = 0, }
};

// Returns -1 for unknown, 0 for non-offloaded, 1 for offloaded
int iface_offloaded_p(const interface *i,unsigned otype){
	if(i->offloadmask & otype){
		if(i->offload & otype){
			return 1;
		}else{
			return 0;
		}
	}
	return -1;
}

int iface_offload_info(const omphalos_iface *octx,const char *name,
				unsigned *offload,unsigned *valid){
	const struct offload_info *oi;
	const struct offload_flags_info *of;
	struct ethtool_value ev;

	*valid = *offload = 0;
	for(oi = offload_infos ; oi->desc ; ++oi){
		if(oi->op >= 0){
			ev.cmd = oi->op;
			if(ethtool_docmd(octx,name,&ev) == 0){
				*valid |= oi->mask;
				*offload |= ev.data ? oi->mask : 0;
			}
		}
	}
	ev.cmd = ETHTOOL_GFLAGS;
	if(ethtool_docmd(octx,name,&ev) == 0){
		for(of = offload_flags ; of->desc ; ++of){
			*valid |= of->mask;
			if(ev.data & of->flag){
				*offload |= (ev.data & of->flag) ? of->mask : 0;
			}
		}
	}
	return 0;
}

int iface_ethtool_info(const omphalos_iface *octx,const char *name,struct ethtool_cmd *info){
	info->cmd = ETHTOOL_GSET;
	if(ethtool_docmd(octx,name,info)){
		return -1;
	}
	return 0;
}
