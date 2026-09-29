#ifndef PROTOCOL_H
#define PROTOCOL_H

#define FRAME_SOF_HI 0XAA
#define FRAME_SOF_LO 0X55
#define FRAME_HDR_LEN 4
#define FRAME_SUM_LEN 1
#define MAX_PLAYLOAD 1024

unsigned char calc_sum(const unsigned char *data,int len);
int pack_frame(unsigned char *out,const char *playload);
int recv_all(int fd,void *buf,int len);
int recv_frame(int fd,char *playload,int maxlen);

#endif