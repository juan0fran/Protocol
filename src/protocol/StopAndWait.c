#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include <assert.h>
#include <sys/time.h>

#include <math.h>

#include <protocol/StopAndWait.h>
#include <util/log.h>
#include <util/socket_utils.h>
#include <interfaces/packet.h>
#include <interfaces/phy.h>
#include <interfaces/net.h>

static unsigned long long millitime() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // caculate milliseconds
    return milliseconds;
}

static ErrorHandler Connect_Master(Control * c, Status * s);
static ErrorHandler Connect_Slave(Control * c, Status * s);

static ErrorHandler protocol_send_beacon_routine (BYTE * p, int len, Control * c, Status * s){
	s->type = 'B';
	if (write_packet_to_phy(c->phy_fd, p, len, c, s) != NO_ERROR){
		log_message(LOG_ERROR, "Error writing\n");
		return IO_ERROR;
	}
	return NO_ERROR;
}

static int protocol_recv_beacon_routine (BYTE * p, Control * c, Status * s){
	struct pollfd pfd;
	int len;	
	int rv;
	Status rs;
	if (len = read_packet_from_phy(c->phy_fd, p, 0, c, &rs), len > 0){
		if (rs.type == 'B'){
			printf("Beacon message Received\n");
			return len;
		}else{
			return 0;
		}
	}
	return -1;
}

ErrorHandler protocol_control_routine (ProtocolControlEvent event, Control * c, Status * s) {
	ErrorHandler ret;
	BYTE buffer[MTU_SIZE + MTU_OVERHEAD];
	int len;
	int i; 
	switch (event){
	/* Routine to send control frames to the other peer */
	/* We shall not be waiting an ACK before sending a control frame */
		case control_packet:
			if (c->waiting_ack == false){
				/* We can send the control frame */
				s->type = 'P'; /* i.e. P will be a control frame */
				/* put the control information inside */
				buffer[0] = 'A';
				buffer[1] = 'B';
				len = 2;
				if (write_packet_to_phy(c->phy_fd, buffer, len, c, s) != NO_ERROR){
					log_message(LOG_ERROR, "Error writing\n");
					return IO_ERROR;
				}
				s->stored_count = 0;
				s->stored_type = s->type;
				s->stored_len = len;
				memcpy(s->stored_packet, buffer, len);
				c->waiting_ack = true;
				/* We are waiting an ACK now!! */
				/* Start the timeout */
				c->timeout = millitime();
			}
			return NO_ERROR;
			break;
		case beacon_send:
			/* log_message(LOG_DEBUG, "Trying to get a beacon from up\n"); */
			if (input_timeout(c->beacon_fd, 0) > 0){
				len = read(c->beacon_fd, buffer, MTU_SIZE);
				if (len == 0){
					log_message(LOG_ERROR, "Error reading\n");
					return IO_ERROR;
				}
				/* First of all, receive a beacon packet from SOCKET FD */
				return (protocol_send_beacon_routine(buffer, len, c, s));
			}
			return NO_ERROR;
			break;

		case beacon_recv:
			/* log_message(LOG_DEBUG, "Trying to receive a beacon from down\n"); */
			if (input_timeout(c->phy_fd, 0) > 0){
				if (len = protocol_recv_beacon_routine(buffer, c , s), len > 0){
					/* just write as socat */
					write(c->beacon_fd, buffer, len);
					/* Send the BEACON packet to FD */
					return HAVE_BEACON;
				}else if (len == 0){
					return NO_ERROR;
				}else{
					return NO_LINK;
				}
			}else{
				return NO_LINK;
			}
			break;
		default:
		break;
	}
	return NO_ERROR;
}

ErrorHandler protocol_establishment_routine (ProtocolEstablishmentEvent event, Control * c, Status * s){
	ErrorHandler ret;
	switch (event){
		case initialise_link:
			/* Three way handshake, if it is done -> set initialised */
			if (c->master_slave_flag == SLAVE){ 
				/* we are slaves */
				/* Something has been received -> process */
				/* Do Connection */
				ret = Connect_Slave(c, s);
				if (ret == NO_ERROR){
					c->byte_round_trip_time = c->packet_timeout_time;
					c->round_trip_time = c->packet_timeout_time;
					c->initialised = 1;
					c->waiting_ack = false;
					c->last_link = millitime();
				}else if (ret == IO_ERROR){
					return IO_ERROR;
				}else{
					c->initialised = 0;
				}
			}else if (c->master_slave_flag == MASTER){ /* we are masters */
				ret = Connect_Master(c, s);
				if (ret == NO_ERROR){
					c->byte_round_trip_time = c->packet_timeout_time;
					c->round_trip_time = c->packet_timeout_time;					
					c->initialised = 1;
					c->waiting_ack = false;
					c->last_link = millitime();
				}else if (ret == IO_ERROR){
					return IO_ERROR;
				}else{
					c->initialised = 0;
				}
			}
		break;
		case check_link_availability:
			/* Make a connection test */
			if (c->last_link == 0){
				c->initialised = 0;
			/* now we can implement the ping... */
			}else if ( ((int) (millitime() - c->last_link )) < c->death_link_time){
				if (((int) (millitime() - c->last_link )) >= c->ping_link_time){
					/* Still not death but in ping time */
					/* Make a control frame send */
					ret = protocol_control_routine(control_packet, c, s);
					if (ret == IO_ERROR){
						log_message(LOG_ERROR, "Error at protocol control: %d\n", ret);
						return ret;
					}
				}
			}else if ( ((int) (millitime() - c->last_link )) >= c->death_link_time){
				log_message(LOG_ERROR, "Last link was: %llu. Now we are: %llu\n", c->last_link, millitime());
				log_message(LOG_ERROR, "link is dead -> set control.initialised to 0, return -1\n");
				c->initialised = 0;
			}else{
				c->initialised = 1;
			}
			/* If death link counter is not out, then proceed */
		break;
		case desconnect_link:
			/* Three way handshake, if it is done -> unset initialised */
		break;
		default:
		break;
	}
	return NO_ERROR;
}

/* The master saids Hanshake HELLO */
static ErrorHandler Connect_Master(Control * c, Status * s){
	BYTE connect_packet[] = "Some Useless Information";
	BYTE recv_buffer[MTU_SIZE + MTU_OVERHEAD];
	Status rs;
	struct pollfd pfd;
	int connect_done = 0;
	int ret;
	int len;
	int retry = 0;
	int limit = 1 + (int)(c->death_link_time/c->packet_timeout_time);
	pfd.fd = c->phy_fd;
	pfd.events = POLLIN;
	log_message(LOG_DEBUG, "Trying to connect as master\n");
	while(!connect_done){
		s->type = 'C';
		s->sn = 0;
		s->rn = 0;
		/* The master needs -> Send for a Connect packet, wait for a Connect packet, then send ACK */
		if (write_packet_to_phy(c->phy_fd, connect_packet, sizeof(connect_packet), c, s) != 0){
			printf("Error writing\n");
			return IO_ERROR;
		}
		/* a read packet has to go with a poll or select */
		if (poll(&pfd, 1, c->packet_timeout_time) > 0){
			/* Try again xD */
			if (len = read_packet_from_phy(c->phy_fd, recv_buffer, c->packet_timeout_time, c, &rs), len > 0){
				/* The slave answers S */
				if (rs.type == 'S'){
					s->sn = rs.rn;
					s->rn = (s->rn + 1)%2;
					c->last_link = millitime();
					write_ack_to_phy(c->phy_fd, c, s);
					connect_done = 1;
				}
				if (rs.type == 'C'){
					/* Both are trying to connect as MASTER */
					/* He is trying to connect us as master, equal as us -> send an ACK and end */
					log_message(LOG_WARN, "Both are trying to connect as MASTER. Copy the SN and RN\n");
					s->sn = rs.sn;
					s->rn = rs.rn;
					c->last_link = millitime();
					write_ack_to_phy(c->phy_fd, c, s);
					connect_done = 1;
				}
			}else{
				retry++;
				if (retry > limit){
					return NO_LINK;
				}
			}
		}else{
			retry++;
			if (retry > limit){
				return NO_LINK;
			}
		}
		/* Then is OK, a packet has been exchanged */
	}
	return NO_ERROR;
}

/* The slave waits for a Handshake HELLO */
static ErrorHandler Connect_Slave(Control * c, Status * s){
	int connect_done = 0;
	int connect_established = 0;
	int len;
	struct pollfd pfds[2];
	Status rs;
	BYTE recv_buffer[MTU_SIZE + MTU_OVERHEAD];
	BYTE connect_packet[] = "Some Useless Information";
	ErrorHandler ret;

	pfds[0].fd = c->phy_fd;
	pfds[0].events = POLLIN;

	pfds[1].fd = c->beacon_fd;
	pfds[1].events = POLLIN;

	log_message(LOG_DEBUG, "Trying to connect as slave\n");
	while (!connect_done){
		/* The slave needs -> Wait for a Packet, then Send a Packet back (connect packet) */
		/* a read packet has to go with a poll or select */
		if (poll(pfds, 2, c->ping_link_time) <= 0){
			/* Try again xD */
			return NO_LINK;
		}
		if (pfds[0].revents & POLLIN){
			if (len = read_packet_from_phy(c->phy_fd, recv_buffer, c->packet_timeout_time, c, &rs), len > 0){
				if (rs.type == 'B' && connect_established == 0){
					write(c->beacon_fd, recv_buffer, len);
					/* In case a beacon packet has been received -> Send the beacon up */
					/* And try to connect them */
					ret = Connect_Master(c, s);
					if (ret == NO_ERROR){
						c->initialised = 1;
						c->waiting_ack = false;
						c->last_link = millitime();
					}else if (ret == IO_ERROR){
						return IO_ERROR;
					}else{
						c->initialised = 0;
					}		
					connect_done = 1;	
				}else if (rs.type == 'B' && connect_established == 1){
					/* try again */
					continue;
				}
				if (rs.type == 'C'){
					/* meaning im the slave answering */
					log_message(LOG_INFO, "Received a Connection packet!\n");
					s->type = 'S';
					s->sn = rs.sn;
					s->rn = rs.rn;
					s->rn = (s->rn + 1)%2;
					c->last_link = millitime();
					if (write_packet_to_phy(c->phy_fd, connect_packet, sizeof(connect_packet), c, s) != 0){
						log_message(LOG_ERROR, "Error writing\n");
						return IO_ERROR;
					}
					connect_established = 1;
				}else{
					if (rs.rn != s->sn){
						s->sn = rs.rn;
						connect_done = 1;
						if (rs.type == 'D'){
							log_message(LOG_INFO, "Connection from slave (ACK) has been done frome piggybacking packet\n");
							write_to_net(c->net_fd, recv_buffer, len);
							s->rn = (s->rn + 1)%2;
							c->last_link = millitime();
							write_ack_to_phy(c->phy_fd, c, s);
						}
					}
				}
			}else if (connect_established == 1){
				if (write_packet_to_phy(c->phy_fd, connect_packet, sizeof(connect_packet), c, s) != 0){
					log_message(LOG_ERROR, "Error writing\n");
					return IO_ERROR;
				}	
			}else{
				return NO_LINK;
			}
		}
		if (pfds[1].revents & POLLIN){
			if (protocol_control_routine(beacon_send, c, s) == IO_ERROR){
				return IO_ERROR;
			}
		}
	}
	return NO_ERROR;
}

ErrorHandler ResendFrame(Control * c, Status * s){
	if (++s->stored_count == c->packet_counter){
		log_message(LOG_WARN, "Timeout EXPIRED for packet: SEQ -> %d\n", atoi((char *) s->stored_packet));
		/* Last link updated, round trip time must be updated */
		/* Every packet sent c->timeout is set */
		//c->initialised = 0;
		c->byte_round_trip_time = c->packet_timeout_time;
		c->round_trip_time = lround(ceil((double) c->byte_round_trip_time * (s->stored_len/c->phy_size)));
		c->waiting_ack = false;
		flush_phy(c->phy_fd);
		return NO_ERROR;
	}
	s->type = s->stored_type;
	/* Care!, maybe is not type D */
	if (write_packet_to_phy(c->phy_fd, s->stored_packet, s->stored_len, c, s) != 0){
		log_message(LOG_ERROR, "Error writing\n");
		return IO_ERROR;
	}
	return NO_ERROR;
}

ErrorHandler SendNetFrame(Control * c, Status * s){
	BYTE buffer[MTU_SIZE + MTU_OVERHEAD];
	int len;
	/* Something in Network Layer -> this has priority when not waiting for ACK */
	/* Do stop and wait SEND */
	if (len = read_packet_from_net(c->net_fd, buffer, 0), len < 0){
		log_message(LOG_ERROR, "Error reading, NET Socket has been closed\n");
		return IO_ERROR;
	}
	if (len > 0){
		if (len > MTU_SIZE){
			log_message(LOG_ERROR, "Maximum MTU reached\n");
			return IO_ERROR;
		}
		/* in ms */
		s->type = 'D';
		if (write_packet_to_phy(c->phy_fd, buffer, len, c, s) != 0){
			log_message(LOG_ERROR, "Error writing\n");
			return IO_ERROR;
		}
		s->stored_count = 0;
		s->stored_type = s->type;
		s->stored_len = len;
		memcpy(s->stored_packet, buffer, len);
		c->waiting_ack = true;
		/* Start the timeout */
		/* This update MUST be done to adapt from the current configuration */
		/* byte rount trip time is not an exact measurement... */
		/* But is a nice approximation including the piggy time */
		c->round_trip_time = (c->byte_round_trip_time * lround(1.0 + floor((double) (len/c->phy_size)))) + (c->piggy_time * 2);
		log_message(LOG_WARN, "Timeout for this transmission is: %d\n", c->round_trip_time);
		c->timeout = millitime();
	}
	return NO_ERROR;
}

ErrorHandler RecvPhyFrame(Control * c, Status * s, int timeout){
	Status rs;
	int rv;
	int len;
	struct pollfd netfd;
	BYTE buffer[MTU_SIZE + MTU_OVERHEAD];
	double packet_time;

	netfd.fd = c->net_fd;
	netfd.fd = POLLIN;

	/* This is the most important, read packet from phy */
	/* This means, try to receive from the medium during c->round_trip_time time */
	/* read_packet_from_phy will call a method to extract the packets... */
	if (len = read_packet_from_phy(c->phy_fd, buffer, c->round_trip_time, c, &rs), len < 0){
		log_message(LOG_ERROR, "Error reading\n");
		return IO_ERROR;
	}
	if (len == 0){
		/* Timeout */
		return NO_ERROR;
	}
	/* Now is time to check wheter is that */
	if (rs.type == 'C'){
		log_message(LOG_WARN, "We are in troubles, master asks for reconnect\n");
		log_message(LOG_WARN, "Waiting flag was: %d\n", c->waiting_ack);
		c->last_link = 0;
		/* The packet is lost */
		return NO_ERROR;
	}

	if (rs.type == 'B'){
		log_message(LOG_INFO, "Beacon received!");
		write(c->beacon_fd, buffer, len);
		return NO_ERROR;
	}

	if (rs.rn == s->sn && c->waiting_ack == true){
		/* The packet that is being received is a new one, but I want to send a previous one!! */
		/* Resend the packet and forget about the received here */
		ResendFrame(c, s);
	}
	/* This means, a packet ACKing the last sent packet has been received (we have to update the sequence number) */
	if (rs.rn != s->sn && c->waiting_ack == true){
		log_message(LOG_INFO, "Good packet while waiting for ACK. s->sn updated\n");
		s->sn = rs.rn;
		c->waiting_ack = false;
		/* Last link updated, round trip time must be updated */
		/* Every packet sent c->timeout is set */
		/* Filtering the round trip time */
		/* Round trip time will be different if is an ACK or a piggybacking frame */
		/* pending to fix round trip time */
		log_message(LOG_WARN, "The transmission took: %llu milliseconds\n", millitime() - c->timeout);
		log_message(LOG_WARN, "Plen: %d, phy size: %d\n", len, c->phy_size);
		packet_time = floor((double)((double)len/(double)c->phy_size)) + 1.0;
		log_message(LOG_WARN, "Packet Amount: %f\n", packet_time);
		packet_time = (double) (millitime() - c->timeout) / packet_time;
		c->byte_round_trip_time = (int) (double) (c->byte_round_trip_time * 0.2 + 0.8 * packet_time);
		//c->byte_round_trip_time = (int) lround ((double) ((double) c->round_trip_time * 0.2 + 0.8 * (double) (millitime() - c->timeout)));
		log_message(LOG_WARN, "Byte round trip time updated to: %d\n", c->byte_round_trip_time);
		c->last_link = millitime();
		/* A new packet (sent from other station) has been received while witing for ACK */
		if (rs.sn == s->rn){
			/* Data or Control */
			if (rs.type == 'D' || rs.type == 'P'){
				if (rs.type == 'D'){
					log_message(LOG_INFO, "Sending packet towards the network. Received a piggybacking ACK\n");
					check_headers_net(buffer, &len);
					write_to_net(c->net_fd, buffer, len);
				}else if (rs.type == 'P'){
					log_message(LOG_DEBUG, "Control Packet arrived-> ");
					log_message(LOG_DEBUG, "0x%02X 0x%02X\n", buffer[0], buffer[1]);
				}
				/* If we are waiting a packet from network, update s->rn and do not send ACK, send a new packet directly */
				rv = poll(&netfd, 1, c->piggy_time);
				if (rv == -1){
					log_message(LOG_ERROR, "poll inside function: ");
					return IO_ERROR;
				}else if (rv == 0){
					s->rn = (s->rn + 1)%2;
					c->last_link = millitime();
					write_ack_to_phy(c->phy_fd, c, s);
					return NO_ERROR;
				}else{
					s->rn = (s->rn + 1)%2;
					c->last_link = millitime();
					return NO_ERROR;
				}
			}
			return NO_ERROR;
		}
		return NO_ERROR;
	}
	/* A new packet (sent from other station) has been received, we were not waiting for ACK or nothing */
	if (rs.sn == s->rn){
		if (c->waiting_ack == false){
			/* Aquí no entro nunca broh */
			log_message(LOG_INFO, "Received sn == rn and waiting_ack == false\n");
			if (rs.type == 'D' || rs.type == 'P'){
				if (rs.type == 'D'){
					log_message(LOG_INFO, "Sending packet towards the network\n");
					check_headers_net(buffer, &len);
					write_to_net(c->net_fd, buffer, len);
				}else if (rs.type == 'P'){
					log_message(LOG_DEBUG, "Control Packet arrived-> ");
					log_message(LOG_DEBUG, "0x%02X 0x%02X\n", buffer[0], buffer[1]);
				}
				/* If we are waiting a packet from network, update s->rn and do not send ACK, send a new packet directly */
				rv = poll(&netfd, 1, c->piggy_time);
				if (rv == -1){
					log_message(LOG_ERROR, "poll inside function: ");
					return IO_ERROR;
				}else if (rv == 0){
					s->rn = (s->rn + 1)%2;
					c->last_link = millitime();
					write_ack_to_phy(c->phy_fd, c, s);
					return NO_ERROR;
				}else{
					s->rn = (s->rn + 1)%2;
					c->last_link = millitime();
					return NO_ERROR;
				}
			}
			if (rs.type == 'C'){
				s->rn = (s->rn + 1)%2;
				c->last_link = millitime();
				write_ack_to_phy(c->phy_fd, c, s);
				return NO_ERROR;
			}
		}else{
			/* A packet received not ACKing my last packet, but I was waiting a packet */
			log_message(LOG_INFO, "Received sn == rn and waiting_ack == true\n");
			if (rs.type == 'D' || rs.type == 'P'){
				if (rs.type == 'D'){
					log_message(LOG_INFO, "Sending packet towards the network while waiting for ACK\n");
					check_headers_net(buffer, &len);
					write_to_net(c->net_fd, buffer, len);
				}else if (rs.type == 'P'){
					log_message(LOG_DEBUG, "Control Packet arrived-> ");
					log_message(LOG_DEBUG, "0x%02X 0x%02X\n", buffer[0], buffer[1]);
				}
				s->rn = (s->rn + 1)%2;
				c->last_link = millitime();	
				write_ack_to_phy(c->phy_fd, c, s);
				return NO_ERROR;
			}
		}
	}else{
		/* In case of received packet that amis to get an ACK */
		if (rs.type != 'A'){
			log_message(LOG_DEBUG, "ACKing old packet\n");
			write_ack_to_phy(c->phy_fd, c, s);
		}
		return NO_ERROR;
	}
	return NO_ERROR;
}

ErrorHandler StopAndWait(Control * c, Status * s){
	int rv;
	ErrorHandler err;
	struct pollfd ufds[3];
	unsigned long long timeout;

	log_message(LOG_DEBUG, "Print the states: SN: %d RN: %d\n", s->sn, s->rn);

	ufds[0].fd = c->net_fd;
	ufds[0].events = POLLIN; // check for normal data
	ufds[1].fd = c->phy_fd;
	ufds[1].events = POLLIN; // check for normal data
	ufds[2].fd = c->beacon_fd;
	ufds[2].events = POLLIN;

	if (c->waiting_ack == true){
		log_message(LOG_DEBUG, "Waiting for a packet from PHY -> do not accept from net\n");
		timeout = millitime();
		rv = poll(&ufds[1], 1, c->round_trip_time);
		timeout = millitime() - timeout;
	}else{
		log_message(LOG_INFO, "Waiting for some packet from NET or PHY\n");
		rv = poll(ufds, 3, c->ping_link_time);
	}
	/* Wait for EVENT */
	if (rv == -1){
		log_message(LOG_ERROR, "Error waiting for event");
		return IO_ERROR;
	}else if(rv == 0){
		if (c->waiting_ack == true){
			log_message(LOG_INFO, "Timeout waiting for ACK, resending a frame\n");
			return (ResendFrame(c, s));
		}
	}else{
		if ((ufds[0].revents & POLLIN) && c->waiting_ack == false){
			log_message(LOG_DEBUG, "Something at the NETWORK layer\n");
			err = SendNetFrame(c, s);
			if (err != NO_ERROR){
				return err;
			}
		}
		/* Check the timeout */
		/* Something arrived from the medium!! */
		if (ufds[1].revents & POLLIN){
			/* TODO: MAKE A FUNCTION FOR THAT */
			log_message(LOG_DEBUG, "Something at the PHYSICAL layer\n");
			err = RecvPhyFrame(c, s, (int) timeout);
			if (err != NO_ERROR){
				return err;
			}
		}
		if (ufds[2].revents & POLLIN){
			log_message(LOG_DEBUG, "Something at the BEACON layer\n");
			err = protocol_control_routine(beacon_send, c, s);
			if (err != NO_ERROR){
				return err;
			} 
		}
	}
	return NO_ERROR;
}