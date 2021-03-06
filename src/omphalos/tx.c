#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <sys/socket.h>
#include <net/if_arp.h>
#include <netinet/ip6.h>
#include <omphalos/tx.h>
#include <omphalos/diag.h>
#include <omphalos/pcap.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/psocket.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

#define TP_STATUS_PREPARING (~0u)

// Transmission is complicated due to idiosyncracies of various emission
// mechanisms, and also portability issues -- both across interfaces, and
// protocols, as we shall see. Transmission is built around the tpacket_hdr
// structure, necessary for use with PACKET_TX_MMAP-enabled packet sockets. In
// terms of performance and power, we can order mechanisms from most desirable
// to least desirable:
//
//  - PACKET_TX_MMAP-enabled PF_PACKET, SOCK_RAW sockets. Only available on
//     properly-configured late 2.6-series Linux kernels. Require CAP_NET_ADMIN.
//  - PF_PACKET, SOCK_RAW sockets. Require CAP_NET_ADMIN.
//  - PF_PACKET, SOCK_DGRAM sockets. Physical layer is inserted based on
//     sockaddr_ll information, which doesn't allow e.g. specification of
//     source hardware address. Require CAP_NET_ADMIN.
//  - PF_INET[6], SOCK_RAW, IPPROTO_RAW sockets. Physical layer is wholly
//     generated by the kernel. IP only. Require CAP_NET_ADMIN.
//  - IP_HDRINCL-enabled PF_INET[6], SOCK_RAW sockets of protocol other than
//     IPPROTO_RAW. Physical layer is wholly generated by the kernel.
//     Individual IP protocols only. Require CAP_NET_ADMIN.
//  - PF_INET[6], SOCK_RAW sockets of protocol other than IPPROTO_RAW. Layers
//     2 through 3 are wholly generated by the kernel. Individual IP protocols
//     only. Require CAP_NET_ADMIN.
//  - PF_INET[6] sockets of type other than SOCK_RAW. Layers 2 through 4 are
//     generated by the kernel. Only certain IP protocols are supported.
//     Subject to iptables actions.
//
// Ideally, transmission would need know nothing about the packet save its
// length, outgoing interface, and location in memory. This is most easily
// effected via PF_PACKET, SOCK_RAW sockets, which are also, happily, the
// highest-performing option. A complication arises, however: PF_PACKET packets
// are not injected into the local IP stack (it is for this reason that they're
// likewise not subject to iptables rules). They *are* copied to other
// PF_PACKET sockets on the host. This causes problems for three cases:
//
//  - self-directed unicast, including all loopback traffic
//  - multicast (independent of IP_MULTICAST_LOOP use), and
//  - broadcast.
//
// In these cases, we must fall back to the next most powerful mechanism,
// PF_INET sockets of SOCK_RAW type and IPPROTO_RAW protocol. These only
// support IPv4 and IPv6, and we need one socket for each. Thankfully, they are
// Layer 2-independent, and thus can be used on any type of interface.
// Unfortunately, we must still known enough of each interface's Layer 2
// protocol to recognize multicast and broadcast (where these concepts exist),
// and self-directed unicast.
//
// So, when given a packet to transmit, we must determine the transmission
// type based on that device's semantics. If the packet ought be visible to our
// local IP stack, we either transmit it solely there (unicast), or duplicate
// it via unicast (multi/broadcast). Note that other PF_PACKET listeners will
// thus see two packets for outgoing multicast and broadcasts of ours.

// Acquire a frame from the ringbuffer. Start writing, given return value
// 'frame', at: (char *)frame + ((struct tpacket_hdr *)frame)->tp_mac.
void *get_tx_frame(interface *i,size_t *fsize){
	struct tpacket_hdr *thdr;
	void *ret;

	pthread_mutex_lock(&i->lock);
	thdr = i->curtxm;
	if(thdr == NULL){
		pthread_mutex_unlock(&i->lock);
		diagnostic("[%s] can't transmit (fd %d)", i->name, i->fd);
		return NULL;
	}
	if(thdr->tp_status != TP_STATUS_AVAILABLE){
		if(thdr->tp_status != TP_STATUS_WRONG_FORMAT){
			pthread_mutex_unlock(&i->lock);
			diagnostic("[%s] no available TX frames", i->name);
			return NULL;
		}
		thdr->tp_status = TP_STATUS_AVAILABLE;
	}
	// Need indicate that this one is in use, but don't want to
	// indicate that it should be sent yet
	thdr->tp_status = TP_STATUS_PREPARING;
	// FIXME we ought be able to set this once for each packet, and be done
	thdr->tp_net = thdr->tp_mac = TPACKET_ALIGN(sizeof(struct tpacket_hdr));
	ret = i->curtxm;
	i->curtxm += inclen(&i->txidx,&i->ttpr);
	pthread_mutex_unlock(&i->lock);
	*fsize = i->ttpr.tp_frame_size;
	return ret;
}

// Loopback devices don't play nicely with PF_PACKET sockets; transmitting on
// them results in packets visible to device-level sniffers (tcpdump, wireshark,
// ourselves) but never injected into the machine's IP stack. These results are
// not portable across systems, either; FreeBSD and OpenBSD do things
// differently, to the point of a distinct loopback header type.
//
// So, we take the packet as prepared, and sendto() over the (unbound) TX
// sockets (we need one per protocol family -- effectively, AF_INET and
// AF_INET6). This suffers a copy, of course.
//
// UDP and ICMP are the only transport protocols supported now. We cannot
// currently use ARPHRD_NONE interfaces (e.g. TUN), because it doesn't carry the
// L3 protocol in the header FIXME.
static ssize_t
send_to_self(interface *i, void *frame){
	struct tpacket_hdr *thdr = frame;
	struct sockaddr_in6 sina6;
	struct sockaddr_in sina;
	unsigned short l2proto;
	struct sockaddr *ss;
	const void *payload;
	socklen_t slen;
	const char *l2;
	unsigned plen;
	size_t l2len;
	int fd,r;

	l2 = ((const char *)frame + thdr->tp_mac);
	switch(i->arptype){
		case ARPHRD_ETHER:
			l2len = sizeof(struct ethhdr);
			l2proto = ((const struct ethhdr *)l2)->h_proto;
			break;
		case ARPHRD_LOOPBACK: // emulates loopback
			l2len = sizeof(struct ethhdr);
			l2proto = ((const struct ethhdr *)l2)->h_proto;
			break;
		case ARPHRD_NONE:
			return -1;
		default:
			diagnostic("Unknown ARPHRD type %u", i->arptype);
			return -1;
	}
	if(l2proto == ntohs(ETH_P_IP)){
		const struct iphdr *ip;

		fd = i->fd4;
		ss = (struct sockaddr *)&sina;
		slen = sizeof(sina);
		memset(ss,0,slen);
		sina.sin_family = AF_INET;
		ip = (const struct iphdr *)(l2 + l2len);
		if(ip->protocol == IPPROTO_UDP){
			const struct udphdr *udp;

			sina.sin_addr.s_addr = ip->daddr;
			udp = (const struct udphdr *)((const char *)ip + ip->ihl * 4u);
			sina.sin_port = udp->dest;
			plen = ntohs(ip->tot_len);
		}else if(ip->protocol == IPPROTO_ICMP){
			sina.sin_addr.s_addr = ip->daddr;
			sina.sin_port = 0;
			plen = ntohs(ip->tot_len);
		}else{
			diagnostic("Unknown IPv4 protocol %u", ip->protocol);
			return 0;
		}
		payload = ip;
	}else if(l2proto == ntohs(ETH_P_IPV6)){
		const struct ip6_hdr *ip;

		ss = (struct sockaddr *)&sina6;
		slen = sizeof(sina6);
		memset(ss,0,slen);
		sina6.sin6_family = AF_INET6;
		ip = (const struct ip6_hdr *)(l2 + l2len);
		// IPv6 doesn't support IP_HDRINCL.
		if(ip->ip6_ctlun.ip6_un1.ip6_un1_nxt == IPPROTO_UDP){
			const struct udphdr *udp;

			fd = i->fd6udp;
			udp = (const struct udphdr *)((const char *)ip + sizeof(*ip));
			payload = udp;
			sina6.sin6_port = htons(IPPROTO_UDP);
		}else if(ip->ip6_ctlun.ip6_un1.ip6_un1_nxt == IPPROTO_ICMPV6){
			const struct icmp6_hdr *icmp;

			fd = i->fd6icmp;
			icmp = (const struct icmp6_hdr *)((const char *)ip + sizeof(*ip));
			payload = icmp;
			sina6.sin6_port = htons(IPPROTO_ICMPV6);
		}else{
			diagnostic("Unknown IPv6 protocol %u", ip->ip6_ctlun.ip6_un1.ip6_un1_nxt);
			return 0;
		}
		plen = ntohs(ip->ip6_ctlun.ip6_un1.ip6_un1_plen);
		memcpy(&sina6.sin6_addr, &ip->ip6_dst, sizeof(ip->ip6_dst));
	}else{
		return -1;
	}
	if((r = sendto(fd, payload, plen, 0, ss, slen)) < 0){
		diagnostic("Error self-TXing %d on %s:%d (%s)", plen, i->name, fd, strerror(errno));
	}
	return r;
}

// Determine whether the packet is (a) self-directed and/or (b) out-directed.
// We should never return 0 for both, unless we don't know the L2 protocol.
static inline int
categorize_tx(const interface *i,const void *frame,int *self,int *out){
	int r = 0;

	switch(i->arptype){
		case ARPHRD_ETHER:{
			const struct ethhdr *eth = frame;

			r = categorize_l2addr(i,eth->h_dest);
			// LOCAL, MULTICAST, and BROADCAST all ought go to us,
			// unless they're ARP.
			*self = (!(r == RTN_UNICAST)) && (ntohs(eth->h_proto) != ETH_P_ARP);
			*out = !(r == RTN_LOCAL);
			break;
		}case ARPHRD_LOOPBACK:{
			*self = 1;
			*out = 0;
			break;
		}case ARPHRD_NONE:{
			*self = 1;
			*out = 1;
			break;
		}default:
			diagnostic("Need implement %s for %u",__func__,i->arptype);
			*self = 0;
			*out = 0;
			break;
	}
	return r;
}

// Mark a frame as ready-to-send. Must have come from get_tx_frame() using this
// same interface. Yes, we will see packets we generate on the RX ring.
int send_tx_frame(interface *i,void *frame){
	const omphalos_ctx *octx = get_octx();
	struct tpacket_hdr *thdr = frame;
	int ret = 0;

	assert(thdr->tp_status == TP_STATUS_PREPARING);
	if(octx->mode != OMPHALOS_MODE_SILENT){
		int self,out;

		categorize_tx(i,(const char *)frame + thdr->tp_mac,&self,&out);
		if(self){
			int r;

			r = send_to_self(i,frame);
			if(r < 0){
				++i->txerrors;
			}else{
				i->txbytes += r;
				++i->txframes;
			}
			ret |= r < 0 ? -1 : 0;
		}
		if(out){
			uint32_t tplen = thdr->tp_len;
			int r;

			//thdr->tp_status = TP_STATUS_SEND_REQUEST;
			//r = send(i->fd,NULL,0,0);
			r = send(i->fd,(const char *)frame + thdr->tp_mac,tplen,0);
			if(r == 0){
				r = tplen;
			}
			//diagnostic("Transmitted %d on %s",ret,i->name);
			if(r < 0){
				diagnostic("Error out-TXing %u on %s (%s)",tplen,i->name,strerror(errno));
				++i->txerrors;
			}else{
				i->txbytes += r;
				++i->txframes;
			}
			ret |= r < 0 ? -1 : 0;
		}
		thdr->tp_status = TP_STATUS_AVAILABLE;
	}else{
		abort_tx_frame(i,frame);
		ret = 0;
	}
	return ret;
}

void abort_tx_frame(interface *i,void *frame){
	const omphalos_ctx *octx = get_octx();
	struct tpacket_hdr *thdr = frame;

	++i->txaborts;
	thdr->tp_status = TP_STATUS_AVAILABLE;
	if(octx->mode != OMPHALOS_MODE_SILENT){
		diagnostic("Aborted TX %ju on %s",i->txaborts,i->name);
	}
}
