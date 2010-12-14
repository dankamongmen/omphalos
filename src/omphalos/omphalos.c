#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/ethernet.h>

// See packet(7) and Documentation/networking/packet_mmap.txt
static int
packet_socket(unsigned protocol){
	return socket(AF_PACKET,SOCK_RAW,htons(protocol));
}

int main(void){
	int fd;

	if((fd = packet_socket(ETH_P_ALL)) < 0){
		return EXIT_FAILURE;
	}
	if(close(fd)){
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
