#ifndef OMPHALOS_DNS
#define OMPHALOS_DNS

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct routepath;
struct omphalos_packet;

// Return value is 0 if packet was processed entirely, without error, as DNS.
// Takes the frame where DNS is expected to begin (UDP/TCP payload).
int handle_dns_packet(struct omphalos_packet *,const void *,size_t)
			__attribute__ ((nonnull (1,2)));

int tx_dns_ptr(int,const void *,const char *) __attribute__ ((nonnull (2,3)));

int setup_dns_ptr(const struct routepath *,int,unsigned,size_t,void *,const char *)
			__attribute__ ((nonnull (1,5,6)));

// Generate reverse DNS lookup strings
char *rev_dns_a(const void *);		// Expects a 32-bit IPv4 address
char *rev_dns_aaaa(const void *);	// Expects a 128-bit IPv6 address

#ifdef __cplusplus
}
#endif

#endif
