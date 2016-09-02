#ifndef __NET__H
#define __NET__H

int write_to_net(int fd, BYTE * p, int len);
int read_packet_from_net(int fd, BYTE * p, int timeout);
int check_headers_net(BYTE * p, int * len);

#endif