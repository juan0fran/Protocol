#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include <assert.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include <math.h>

#include <interfaces/packet.h>
#include <util/socket_utils.h>

static int plen;
static int timeout_val;

int radio_receive(int fd, BYTE * packet, int * block_total){
	BYTE block_countdown, block_size;
	BYTE rx_buf[MTU_SIZE + MTU_OVERHEAD];
	int len;

	if (read(fd, &len, sizeof(int)) == 0)
	{
		exit(0);
	}
    read(fd, rx_buf, len);

    block_countdown = (int) rx_buf[0];
    *block_total = (int) rx_buf[1];

    memcpy(packet + (plen - 2)* block_countdown, (BYTE *) &rx_buf[2], (int) plen - 2);
    //int i; for (i = 2; i < plen; i++) printf("%02X ", rx_buf[i]); printf("\n");
    return block_countdown;
}

int radio_receive_block(int fd, BYTE * packet){
	int packet_size = 0;
	int block_countdown = 0;
	int block_total;
	int len;
	int timeout = 1;
	int rv;
	struct pollfd pfd;
	BYTE block_count = 0;

	pfd.fd = fd;
	pfd.events = POLLIN;

	do{
        block_countdown = radio_receive(fd, packet, &block_total);

        if (block_count != block_countdown)
        {
            printf("RADIO: block sequence error, aborting packet\n");
            return 0;
        }
        block_count++;
        // Wait for the next block to be received if any is expected
        if(block_count < block_total){
			rv = poll(&pfd, 1, timeout_val);
			if (rv == -1){
				perror ("Error with poll: ");
				return -1;
			}else if (rv == 0){
				printf("RADIO: timeout waiting for the next block, aborting packet\n");
				return 0;
			}else{
				// nothing!!!
			}
        }

    } while (block_count < block_total);
	return 1;

}

void radio_send_block(int fd, BYTE * packet, uint16_t size){
    int  block_countdown = 0;
    int  block_total = (int) (ceil( (double)size / (double)(plen - 2)));
    BYTE *block_start = packet;
    BYTE block_length;
    BYTE tx_buf[MTU_SIZE + MTU_OVERHEAD];

    while (block_countdown < block_total)
    {
        block_length = (size > plen - 2 ? plen - 2 : size);
        memset((BYTE *) tx_buf, 0, plen);
        memcpy((BYTE *) &tx_buf[2], block_start, block_length);

    	/* attach 2 bytes of length */
        tx_buf[0] = (BYTE) block_countdown;
        tx_buf[1] = (BYTE) block_total;

        write(fd, &plen, sizeof(int));
        write(fd, tx_buf, plen);
        
        //int i; for (i = 2; i < plen; i++) printf("0x%02X ", tx_buf[i]); printf("\n");

        /* In fact we send the entire block (plen) always */
        block_start += block_length;
        size -= block_length;
        block_countdown++;
    }
}

int main (int argc, char ** argv){
	struct pollfd ufds[2];
	int rv;
	uint16_t phylen;
	int len;
	BYTE buffer[MTU_SIZE + MTU_OVERHEAD];
	if (argc != 5){
		exit(-1);
	}
	int from_up = initialise_client_socket(argv[1]);
	int to_down = initialise_server_socket(argv[2]);
	plen = atoi(argv[3]);
	timeout_val = atoi(argv[4]);
	
	ufds[0].fd = from_up;
	ufds[1].fd = to_down;
	ufds[0].events = POLLIN; // check for normal data
	ufds[1].events = POLLIN; // check for normal data

	while(1){
		rv = poll(ufds, 2, -1);
		if (rv == -1){
			printf("Poll failed\n");
			exit(0);
		}else if (rv > 0){
			if (ufds[0].revents & POLLIN){
				if (read(from_up, &len, sizeof(int)) == 0){
					exit(0);
				}
				memset(buffer, 0, sizeof(buffer));
				phylen = (uint16_t) len;
				memcpy(buffer, &phylen, sizeof(uint16_t));
				read(from_up, buffer + 2, len);
				radio_send_block(to_down, buffer, len + 2);
				printf("Sent %d bytes to phy layer: ", len + 2);
				printf("%s\n", buffer + 2 + 3);
			}
			if (ufds[1].revents & POLLIN){
				memset(buffer, 0, sizeof(buffer));
				len = radio_receive_block(to_down, buffer);
				if (len > 0){
					memcpy(&phylen, buffer, sizeof(uint16_t));
					len = (int) phylen;
					printf("Sending %d to upper layers: ", len);
					printf("%s\n", buffer + 2 + 3);
					write(from_up, &len, sizeof(int));
    				write(from_up, buffer + 2, len);
				}
			}
		}
	}
}























