#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <omphalos/psocket.h>

// See packet(7) and Documentation/networking/packet_mmap.txt
int packet_rx_socket(unsigned protocol){
	struct tpacket_req treq;
	int fd;

	if((fd = socket(AF_PACKET,SOCK_RAW,htons(protocol))) < 0){
		fprintf(stderr,"Couldn't open packet socket (%s?)\n",strerror(errno));
		return -1;
	}
	treq.tp_block_size = 0;
	treq.tp_block_nr = 0;
	treq.tp_frame_size = 0;
	treq.tp_frame_nr = 0;
	return 0;
}
