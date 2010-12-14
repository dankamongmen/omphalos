#ifndef OMPHALOS_PSOCKET
#define OMPHALOS_PSOCKET

#ifdef __cplusplus
extern "C" {
#endif

int packet_socket(unsigned protocol);
void *mmap_rx_psocket(int fd);
void *mmap_tx_psocket(int fd);

#ifdef __cplusplus
}
#endif

#endif
