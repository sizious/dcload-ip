#ifndef __DC_IO_H__
#define __DC_IO_H__

int recv_data(void *data, unsigned int dcaddr, unsigned int total, unsigned int quiet);
int send_data(unsigned char *addr, unsigned int dcaddr, unsigned int size);

int recv_response(unsigned char *buffer, int timeout);
int send_command(char *command, unsigned int addr, unsigned int size, unsigned char *data, unsigned int dsize);

/* Convinience macro */
#define send_cmd(v, w, x, y, z) if(send_command(v, w, x, y, z) == -1) return -1

#endif

