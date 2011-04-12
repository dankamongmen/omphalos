#ifndef OMPHALOS_PSOCKET
#define OMPHALOS_PSOCKET

#ifdef __cplusplus
extern "C" {
#endif

struct tpacket_req;
struct omphalos_ctx;

// Open a packet socket. Requires superuser or network admin capabilities.
int packet_socket(unsigned protocol);

// Returns the size of the map, or 0 if the operation fails (in this case,
// map will be set to MAP_FAILED).
size_t mmap_rx_psocket(int fd,void **map,struct tpacket_req *treq);
size_t mmap_tx_psocket(int fd,void **map,struct tpacket_req *treq);

// map and size ought have been returned by mmap_*_psocket().
int unmap_psocket(void *map,size_t size);

// Loop on a packet socket according to provided program parameters
int handle_packet_socket(const struct omphalos_ctx *);

#ifdef __cplusplus
}
#endif

#endif
