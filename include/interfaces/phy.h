#ifndef __PHY__H
#define __PHY__H

#include <interfaces/packet.h>

int read_packet_from_phy(int fd, BYTE * p, int timeout, Control * c, Status * s);
int write_packet_to_phy(int fd, BYTE * p, int len, Control * c, Status * s);
int write_ack_to_phy(int fd, Control * c, Status * s);

#endif