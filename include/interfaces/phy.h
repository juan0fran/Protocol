#ifndef __PHY_H__
#define __PHY_H__

#include <interfaces/packet.h>

int input_timeout (int filedes, unsigned int milliseconds);
int read_packet_from_phy(int fd, BYTE * p, int timeout, Control * c, Status * s);
int write_packet_to_phy(int fd, BYTE * p, int len, Control * c, Status * s);
int write_ack_to_phy(int fd, Control * c, Status * s);
void init_radio(int _physical_size);
void flush_phy(int fd);
void printf_packet (BYTE * buff, int len);

#endif
