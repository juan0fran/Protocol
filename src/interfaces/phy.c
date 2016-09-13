#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include <assert.h>
#include <sys/time.h>

#include <math.h>

#include <interfaces/phy.h>
#include <protocol/StopAndWait.h>
#include <util/socket_utils.h>
#include <interfaces/packet.h>

static int phy_size;

static unsigned long long millitime() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // caculate milliseconds
    return milliseconds;
}

void init_radio(int _physical_size){
	phy_size = _physical_size;
}

int input_timeout (int filedes, unsigned int milliseconds){
	fd_set set;
	struct timeval timeout;
	/* Initialize the file descriptor set. */
	FD_ZERO (&set);
	FD_SET (filedes, &set);

	/* Initialize the timeout data structure. */
	timeout.tv_sec = 0;
	timeout.tv_usec = milliseconds * 1000; /* microsec*1000

	/* select returns 0 if timeout, 1 if input available, -1 if error. */
	return (select (FD_SETSIZE, &set, NULL, NULL, &timeout));
}

void flush_phy(int fd){
	int len;
	BYTE rx_buf[phy_size];
	while(input_timeout(fd, 0) > 0){
		if (read(fd, &len, sizeof(int)) == 0)
		{
			exit(0);
		}
	    read(fd, rx_buf, len);
	}
}

static int radio_receive(int fd, BYTE * packet, int * block_total){
	BYTE block_countdown, block_size;
	BYTE rx_buf[phy_size];
	int len;

	if (read(fd, &len, sizeof(int)) == 0)
	{
		exit(0);
	}
    read(fd, rx_buf, len);

    block_countdown = (int) rx_buf[0];
    *block_total = (int) rx_buf[1];

    memcpy(packet + (phy_size - 2) * block_countdown, (BYTE *) &rx_buf[2], (int) (phy_size - 2));
    //int i; for (i = 2; i < phy_size; i++) printf("%02X ", rx_buf[i]); printf("\n");
    return block_countdown;
}

static int radio_receive_block(int fd, BYTE * packet, int timeout_val){
	int packet_size = 0;
	int block_countdown = 0;
	int block_total;
	int len;
	int timeout = timeout_val;
	int rx_start = millitime();
	int rv;
	struct pollfd pfd;
	BYTE block_count = 0;
	/* timeout val is -> you have up to that timeout to receive something */
	pfd.fd = fd;
	pfd.events = POLLIN;

	do{
        block_countdown = radio_receive(fd, packet, &block_total);
        if (block_count != block_countdown)
        {
            //printf("RADIO: block sequence error, aborting packet\n");
            flush_phy(fd);
            return 0;
        }
        printf("Block received: %d from %d\n", block_countdown, block_total);
        block_count++;
        // Wait for the next block to be received if any is expected
        if(block_count < block_total){
			rv = poll(&pfd, 1, timeout);
			if (rv == -1){
				perror ("Error with poll: ");
				return -1;
			}else if (rv == 0){
				//printf("RADIO: timeout waiting for the next block, aborting packet\n");
				flush_phy(fd);
				return 0;
			}else{
				/* Less timeout */
				timeout -= (millitime() - rx_start);
				rx_start = millitime();
			}
        }
    } while (block_count < block_total);
	return 1;
}

static void radio_send_block(int fd, BYTE * packet, uint16_t size){
    int  block_countdown = 0;
    int  block_total = (int) (ceil( (double)size / (double)(phy_size - 2)));
    BYTE *block_start = packet;
    BYTE block_length;
    BYTE tx_buf[phy_size];

    while (block_countdown < block_total)
    {
        block_length = (size > phy_size - 2 ? phy_size - 2 : size);
        memset((BYTE *) tx_buf, 0, phy_size);
        memcpy((BYTE *) &tx_buf[2], block_start, block_length);

    	/* attach 2 bytes of length */
        tx_buf[0] = (BYTE) block_countdown;
        tx_buf[1] = (BYTE) block_total;

        write(fd, &phy_size, sizeof(int));
        write(fd, tx_buf, phy_size);
        
        /* In fact we send the entire block (phy_size) always */
        block_start += block_length;
        size -= block_length;
        block_countdown++;
    }
}

void printf_packet (BYTE * buff, int len){
	printf("Length: %d ", len);
	printf("type: %c ", buff[0]);
	printf("sn: 0x%02X ", buff[1]);
	printf("rn :0x%02X ", buff[2]);
	if (len > 5){
		printf("seq: %d\n", atoi((char *) &buff[5]));
	}else{
		printf("\n");
	}
	//int i; for (i = 0; i < len; i++) printf("0x%02X%s", buff[i], (i % 16 == 15) ? "\n" : " "); printf("\n");
}

/* Architecture/system dependant */

/* 	This physical layer is waiting for: 	
		- 255 bytes FIXED SIZE packet (performs FEC with fixed length)
	What we need back is:
		- 1500 bytes MTU + Headers (at this moment 3 bytes): 1503 bytes
	Transmitter -> 
		- Encode 239 bytes payload into a 255 byte RS encoded packet
		- On top of that, add a RS erasure code at packet level 
		- Send N 255 bytes packets towards the sender 
	Receiver ->
		- Receive a 255 bytes packet from the receiver
		- Decode 255 bytes RS packet into a 239 bytes payload
		- Put the decoded packet into RS erasure code receiver
		- When RS decoder returns OK -> Pass the packet to upper layer
*/

int write_ack_to_phy(int fd, Control * c, Status * s){
	BYTE ack [1];
	ack[0] = 0x55;
	s->type = 'A';
	write_packet_to_phy(fd, ack, 1, c, s);
	/* first is the packet type */
	return 0;
}

int write_packet_to_phy(int fd, BYTE * p, int len, Control * c, Status * s){
	/* This creates a packet */
	BYTE tosend [len + 5];
	uint16_t packet_length;
	int ret;	
	packet_length = (uint16_t) len + 5;
	/* first is the packet type */
	tosend[0] = s->type;
	/* Then the actual sequence number */
	tosend[1] = s->sn;
	/* Finally the actual request number */
	tosend[2] = s->rn;
	/* The size must be passed now */
	memcpy(tosend + 3, &packet_length, sizeof(uint16_t));
	/* size of packet copied */
	memcpy(tosend + 5, p, (int) len);
	/* copied ;) */
	/* Do a radio send block */
	radio_send_block(fd, tosend, (uint16_t) packet_length);
	/* Radio send block splits the packet */
	//printf("Packet sent: ");
	//printf_packet(tosend, packet_length);
	return 0;
}

/* If timeout is 0, will be read automatically */
/* Control is passed just for if we need to use it in the future */
/* Status is passed to pump out the received status from their part */
/* This function returns -1 when no packet (timeout/error). Returns 0 when control packet arrived and returns > 0 when data packet */
int read_packet_from_phy(int fd, BYTE * p, int timeout, Control * c, Status * s){
	struct pollfd pfd;
	int len;
	int rv;
	int ret;
	int timeout_count = 0;
	uint16_t packet_length;
	pfd.fd = fd;
	pfd.events = POLLIN;
	BYTE buffer[MTU_SIZE + MTU_OVERHEAD];

	/* Directly call radio_receive_block */
	if (radio_receive_block(fd, buffer, timeout) > 0){
		/* We have a packet */
		/* Extract the size from it and then pass it */
		//int i; for (i = 0; i < 10; i++) printf("0x%02X ", buffer[i]); printf("\n");
		s->type = buffer[0];
		s->sn = buffer[1];
		s->rn = buffer[2];
		memcpy(&packet_length, buffer + 3, sizeof(uint16_t));
		//printf("Packet received: ");
		//printf_packet(buffer, packet_length);
		//printf("Copying memory from packet length: %d\n", packet_length);
		/* all parameters copied */
		len = packet_length - 5;
		memcpy(p, buffer + 5, len);
		/* CRC */
		return len;
	}else{
		//printf("Timeout or wrong sequence\n");
		return 0;
	}
}