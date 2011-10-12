#ifndef OMPHALOS_DNS
#define OMPHALOS_DNS

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct routepath;
struct omphalos_iface;
struct omphalos_packet;

void handle_dns_packet(const struct omphalos_iface *,struct omphalos_packet *,
			const void *,size_t) __attribute__ ((nonnull (1,2,3)));

int tx_dns_ptr(const struct omphalos_iface *,int,const void *,const char *)
		__attribute__ ((nonnull (1,3,4)));

int setup_dns_ptr(const struct routepath *,int,size_t,const void *,
		void *,const char *) __attribute__ ((nonnull (1,4,5)));

// Generate reverse DNS lookup strings
char *rev_dns_a(const void *);		// Expects a 32-bit IPv4 address
char *rev_dns_aaaa(const void *);	// Expects a 128-bit IPv6 address

#ifdef __cplusplus
}
#endif

#endif
