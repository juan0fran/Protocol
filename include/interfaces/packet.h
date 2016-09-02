#ifndef __PACKET_H__
#define __PACKET_H__

#include <stdint.h>
#include <time.h>
#include <stdbool.h>

#define MTU_SIZE		1500
#define MTU_OVERHEAD	50
#define PHYSICAL_SIZE	255
#define PHY_OVERHEAD	4

typedef unsigned char BYTE;

typedef enum ProtocolDataEvent{
	new_packet_from_phy,
	new_packet_from_network,
}ProtocolDataEvent;

typedef enum ProtocolControlEvent{
	initialise_link,
	check_link_availability,
	desconnect_link,
}ProtocolControlEvent;

typedef enum MSFLAG{
	MASTER,
	SLAVE,
}MSFLAG;

typedef struct Control{
	int initialised;
	int death_link_time;
	int packet_timeout_time; /* in ms -> not used inside the structure, pending to be removed */
	MSFLAG master_slave_flag;
	int net_fd;
	int phy_fd;
	bool waiting_ack;
	unsigned long long timeout; /* timeout in ms */
	time_t 		last_link;
}Control;

typedef struct Status{
	int 		rx;
	uint8_t 	type;
	uint8_t 	sn;
	uint8_t 	rn;
	BYTE		stored_packet[MTU_SIZE + MTU_OVERHEAD];
	int 		stored_len;
}Status;

#endif