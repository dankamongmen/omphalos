#ifndef OMPHALOS_DNS
#define OMPHALOS_DNS

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

struct routepath;
struct omphalos_packet;

#define DNS_CLASS_IN	__constant_ntohs(0x0001u)
#define DNS_CLASS_FLUSH	__constant_ntohs(0x8000u)
#define DNS_TYPE_A	__constant_htons(1u)
#define DNS_TYPE_CNAME	__constant_htons(5u)
#define DNS_TYPE_PTR	__constant_htons(12u)
#define DNS_TYPE_HINFO	__constant_htons(13u)
#define DNS_TYPE_MX	__constant_htons(15u)
#define DNS_TYPE_TXT	__constant_htons(0x10u)
#define DNS_TYPE_AAAA	__constant_htons(0x1cu)
#define DNS_TYPE_SRV	__constant_htons(0x21u)
#define DNS_TYPE_SPF	__constant_htons(0x63u)

struct dnshdr {
	uint16_t id;
	uint16_t flags;
	uint16_t qdcount,ancount,nscount,arcount;
	// question, answer, authority, and additional sections follow
} __attribute__ ((packed));

// Return value is 0 if packet was processed entirely, without error, as DNS.
// Takes the frame where DNS is expected to begin (UDP/TCP payload).
int handle_dns_packet(struct omphalos_packet *,const void *,size_t)
			__attribute__ ((nonnull (1,2)));

int tx_dns_ptr(int,const void *,const char *) __attribute__ ((nonnull (2,3)));

int setup_dns_ptr(const struct routepath *,int,const void *,unsigned,size_t,
			void *,const char *,unsigned)
			__attribute__ ((nonnull (1,3,6,7)));

// Generate reverse DNS lookup strings
char *rev_dns_a(const void *);		// Expects a 32-bit IPv4 address
char *rev_dns_aaaa(const void *);	// Expects a 128-bit IPv6 address

#ifdef __cplusplus
}
#endif

#endif
