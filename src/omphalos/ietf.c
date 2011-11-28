#include <assert.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <omphalos/128.h>
#include <asm/byteorder.h>
#include <omphalos/ietf.h>

// IPv4 Multicast is currently defined by RFC 5771 and RFC 2780

// The 224.0.0.0/4 multicast address space
#define RFC1112_BASE __constant_htonl(0xe0000000)
#define RFC1112_MASK __constant_htonl(0xe0000000)

// The 224.0.0.0/24 Local Network Control Block
#define RFC1112_LOCAL_BASE __constant_htonl(0xe0000000)
#define RFC1112_LOCAL_MASK __constant_htonl(0xffffff00)
#define RFC1112_LOCAL_ALL_HOSTS			__constant_htonl(0xe0000001)
#define RFC1112_LOCAL_ALL_ROUTERS		__constant_htonl(0xe0000002)
#define RFC1112_LOCAL_DVMRP			__constant_htonl(0xe0000004)
#define RFC1112_LOCAL_OSPFIGP_ROUTERS		__constant_htonl(0xe0000005)
#define RFC1112_LOCAL_OSPFIGP_DESIGNATED	__constant_htonl(0xe0000006)
#define RFC1112_LOCAL_ST_ROUTERS		__constant_htonl(0xe0000007)
#define RFC1112_LOCAL_ST_HOSTS			__constant_htonl(0xe0000008)
#define RFC1112_LOCAL_IGRP_ROUTERS		__constant_htonl(0xe000000a)
#define RFC1112_LOCAL_DHCP_REPLAY		__constant_htonl(0xe000000c)
#define RFC1112_LOCAL_IGMP3			__constant_htonl(0xe0000016)
#define RFC1112_LOCAL_MDNS			__constant_htonl(0xe00000fb)
#define RFC1112_LOCAL_LLMNR			__constant_htonl(0xe00000fc)

// The 224.0.1.0/24 Internetwork Control Block
#define IPV4_INTERNETWORK_BASE		__constant_htonl(0xe0000100)
#define IPV4_INTERNETWORK_MASK		__constant_htonl(0xffffff00)
#define IPV4_INTERNETWORK_NTP		__constant_htonl(0xe0000101)
#define IPV4_INTERNETWORK_MDHCPDISC	__constant_htonl(0xe0000144)

// The 239.255.255.0/24 Administratively Scoped Block
#define IPV4_ADMINSCOPED_BASE		__constant_htonl(0xefffff00)
#define IPV4_ADMINSCOPED_MASK		__constant_htonl(0xffffff00)
#define IPV4_ADMINSCOPED_SSDP		__constant_htonl(0xeffffffa)
#define IPV4_ADMINSCOPED_SLP		__constant_htonl(0xeffffffd)

#include <stdio.h>
static const wchar_t *
ietf_multicast_ipv4(const uint32_t *ip){
	if((*ip & RFC1112_MASK) != RFC1112_BASE){
		return NULL;
	}
	if((*ip & RFC1112_LOCAL_MASK) == RFC1112_LOCAL_BASE){
		if(*ip == RFC1112_LOCAL_ALL_HOSTS){
			return L"All segment hosts (RFC 1112)";
		}else if(*ip == RFC1112_LOCAL_ALL_ROUTERS){
			return L"All segment routers (RFC 1112)";
		}else if(*ip == RFC1112_LOCAL_DVMRP){
			return L"DVMRP (RFC 1075)";
		}else if(*ip == RFC1112_LOCAL_OSPFIGP_ROUTERS){
			return L"OSPFIGP all routers (RFC 1583)";
		}else if(*ip == RFC1112_LOCAL_OSPFIGP_DESIGNATED){
			return L"OSPFIGP designated routers (RFC 1583)";
		}else if(*ip == RFC1112_LOCAL_ST_ROUTERS){
			return L"ST2 routers (RFC 1190)";
		}else if(*ip == RFC1112_LOCAL_ST_HOSTS){
			return L"ST2 hosts (RFC 1190)";
		}else if(*ip == RFC1112_LOCAL_IGRP_ROUTERS){
			return L"[E]IGRP routers (Cisco)";
		}else if(*ip == RFC1112_LOCAL_DHCP_REPLAY){
			return L"DHCP relay (RFC 1884)";
		}else if(*ip == RFC1112_LOCAL_IGMP3){
			return L"IGMPv3 (RFC 1054)";
		}else if(*ip == RFC1112_LOCAL_MDNS){
			return L"mDNS (IANA)";
		}else if(*ip == RFC1112_LOCAL_LLMNR){
			return L"LLMNR (RFC 4795)";
		}
		return L"RFC 5771 local network multicast";
	}else if((*ip & IPV4_INTERNETWORK_MASK) == IPV4_INTERNETWORK_BASE){
		if(*ip == IPV4_INTERNETWORK_NTP){
			return L"SNTPv4 (RFC 4330)";
		}else if(*ip == IPV4_INTERNETWORK_MDHCPDISC){
			return L"mdhcpdiscover (RFC 2730)";
		}
		return L"RFC 5771 internetwork multicast";
	}else if((*ip & IPV4_ADMINSCOPED_MASK) == IPV4_ADMINSCOPED_BASE){
		if(*ip == IPV4_ADMINSCOPED_SSDP){
			return L"SSDP (UPnP 1.1)";
		}else if(*ip == IPV4_ADMINSCOPED_SLP){
			return L"Admin-scoped SLPv2 (RFC 2608)";
		}
		return L"RFC 5771 admin-scoped multicast";
	}
	return L"RFC 5771 multicast";
}

static const struct {
	uint128_t ip;
	const wchar_t *name;
	unsigned octets;
} ip6mcasts[] = {
	{ // ff02::1
		.ip = { __constant_htonl(0xff020000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x00000001), },
		.name = L"All segment hosts (RFC 2375)",
		.octets = 16,
	},
	{ // ff02::1:ff:0
		.ip = { __constant_htonl(0xff020000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000001), __constant_htonl(0xff000000), },
		.name = L"LL neighbor solicitation (RFC 4861)",
		.octets = 13,
	},
	{ // ff02::2
		.ip = { __constant_htonl(0xff020000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x00000002), },
		.name = L"All segment routers (RFC 2375)",
		.octets = 16,
	},
	{ // ff02::c
		.ip = { __constant_htonl(0xff020000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x0000000c), },
		.name = L"Link-local SSDP (UPnP 1.1)",
		.octets = 16,
	},
	{ // ff02::16
		.ip = { __constant_htonl(0xff020000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x00000016), },
		.name = L"MLDv2 (RFC 4604)",
		.octets = 16,
	},
	{ // ff02::fb
		.ip = { __constant_htonl(0xff020000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x000000fb), },
		.name = L"mDNS (IANA)",
		.octets = 16,
	},
	{ // ff02::1:2
		.ip = { __constant_htonl(0xff020000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x00010003), },
		.name = L"DHCPv6 servers/relays (RFC 3315)",
		.octets = 16,
	},
	{ // ff02::1:3
		.ip = { __constant_htonl(0xff020000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x00010003), },
		.name = L"LLMNR (RFC 4795)",
		.octets = 16,
	},
	{ // ff05::1:3
		.ip = { __constant_htonl(0xff050000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x00010003), },
		.name = L"DHCPv6 servers (RFC 3315)",
		.octets = 16,
	},
	{ // ff05::2
		.ip = { __constant_htonl(0xff050000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x00000002), },
		.name = L"All site routers (RFC 2375)",
		.octets = 16,
	},
	{ // ff05::c
		.ip = { __constant_htonl(0xff050000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x0000000c), },
		.name = L"Site-local SSDP (UPnP 1.1)",
		.octets = 16,
	},
	{ // ff08::c
		.ip = { __constant_htonl(0xff080000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x0000000c), },
		.name = L"Organization-local SSDP (UPnP 1.1)",
		.octets = 16,
	},
	{ // ff0e::c
		.ip = { __constant_htonl(0xff0e0000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x0000000c), },
		.name = L"Global SSDP (UPnP 1.1)",
		.octets = 16,
	},
	{ .name = NULL, }
};

static const wchar_t *
ietf_multicast_ipv6(const uint32_t *ip){
	uint128_t alignedip = { ip[0], ip[1], ip[2], ip[3] };
	const typeof(*ip6mcasts) *mcast;

	if(!IN6_IS_ADDR_MULTICAST(&alignedip)){
		return NULL;
	}
	for(mcast = ip6mcasts ; (*mcast).name ; ++mcast){
		if(equal128masked(alignedip,(*mcast).ip,(*mcast).octets)){
			return (*mcast).name;
		}
	}
	return L"RFC 4291 multicast";
}

const wchar_t *ietf_multicast_lookup(int fam,const void *addr){
	if(fam == AF_INET){
		return ietf_multicast_ipv4(addr);
	}else if(fam == AF_INET6){
		return ietf_multicast_ipv6(addr);
	}
	return NULL;
}

static const wchar_t *
ietf_bcast_ipv4(const uint32_t *ip){
	const uint32_t localb = 0xffffffffu;

	if(memcmp(ip,&localb,sizeof(localb)) == 0){
		return L"Local IPv4 broadcast (RFC 919)";
	}
	return NULL;
}

const wchar_t *ietf_bcast_lookup(int fam,const void *addr){
	if(fam == AF_INET){
		return ietf_bcast_ipv4(addr);
	}
	return NULL;
}

const wchar_t *ietf_local_lookup(int fam,const void *addr){
	if(fam == AF_INET){
		const uint32_t loopback = htonl(INADDR_LOOPBACK);

		if(memcmp(addr,&loopback,sizeof(loopback)) == 0){
			return L"Internal IPv4 loopback (RFC 3330)";
		}
	}else if(fam == AF_INET6){
		if(IN6_IS_ADDR_LOOPBACK(addr)){
			return L"Internal IPv6 loopback (RFC 4291)";
		}
	}
	return NULL;
}
