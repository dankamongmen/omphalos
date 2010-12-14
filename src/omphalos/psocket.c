#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <omphalos/psocket.h>

static const unsigned MAX_FRAME_SIZE = 1500; // FIXME get from device
static const unsigned MMAP_BLOCK_COUNT = 32768; // FIXME do better

// See packet(7) and Documentation/networking/packet_mmap.txt
int packet_socket(unsigned protocol){
	int fd;

	if((fd = socket(AF_PACKET,SOCK_RAW,htons(protocol))) < 0){
		fprintf(stderr,"Couldn't open packet socket (%s?)\n",strerror(errno));
		return -1;
	}
	return fd;
}

static int
get_block_size(unsigned fsize,unsigned *bsize){
	int b;

	// Ought be a power of two for performance. Must be a multiple of
	// page size. Ought be a multiple of tp_frame_size for efficiency.
	b = getpagesize();
	if(b < 0){
		fprintf(stderr,"Couldn't get page size (%s?)\n",strerror(errno));
		return -1;
	}
	*bsize = b;
	while(*bsize < fsize){
		if((*bsize << 1u) < *bsize){
			fprintf(stderr,"No valid configurations found\n");
			return -1;
		}
		*bsize <<= 1u;
	}
	return 0;
}

// Returns 0 on failure, otherwise size of the ringbuffer. On a failure,
// contents of treq are unspecified.
static size_t
size_mmap_psocket(struct tpacket_req *treq){
	// Must be a multiple of TPACKET_ALIGNMENT, and the following must
	// hold: TPACKET_HDRLEN <= tp_frame_size <= tp_block_size.
	treq->tp_frame_size = TPACKET_HDRLEN + MAX_FRAME_SIZE;
	if(get_block_size(treq->tp_frame_size,&treq->tp_block_size) < 0){
		return 0;
	}
	// Array of pointers to blocks, allocated via slab -- cannot be
	// larger than largest slabbable allocation.
	treq->tp_block_nr = MMAP_BLOCK_COUNT;
	// tp_frame_nr is derived from the other three parameters.
	treq->tp_frame_nr = (treq->tp_block_size / treq->tp_frame_size)
		* treq->tp_block_nr;
	return treq->tp_block_nr * treq->tp_block_size;
}

size_t mmap_rx_psocket(int fd,void **map){
	struct tpacket_req treq;
	size_t size;

	*map = MAP_FAILED;
	if((size = size_mmap_psocket(&treq)) == 0){
		return 0;
	}
	if(setsockopt(fd,SOL_PACKET,PACKET_RX_RING,&treq,sizeof(treq)) < 0){
		fprintf(stderr,"Couldn't set socket option (%s?)\n",strerror(errno));
		return 0;
	}
	if((*map = mmap(0,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0)) == MAP_FAILED){
		fprintf(stderr,"Couldn't mmap %zub (%s?)\n",size,strerror(errno));
	}
	return size;
}

size_t mmap_tx_psocket(int fd,void **map){
	struct tpacket_req treq;
	size_t size;

	*map = MAP_FAILED;
	if((size = size_mmap_psocket(&treq)) == 0){
		return 0;
	}
	if(setsockopt(fd,SOL_PACKET,PACKET_TX_RING,&treq,sizeof(treq)) < 0){
		fprintf(stderr,"Couldn't set socket option (%s?)\n",strerror(errno));
		return 0;
	}
	if((*map = mmap(0,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0)) == MAP_FAILED){
		fprintf(stderr,"Couldn't mmap %zub (%s?)\n",size,strerror(errno));
	}
	return size;
}

int unmap_psocket(void *map,size_t size){
	if(munmap(map,size)){
		fprintf(stderr,"Couldn't unmap %zub ring buffer (%s?)\n",size,strerror(errno));
		return -1;
	}
	return 0;
}
