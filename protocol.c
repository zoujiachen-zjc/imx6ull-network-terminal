#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
/*校验和函数*/
unsigned char calc_sum(const unsigned char *data,int len){
    unsigned char sum=0;
    int i=0;
    for(i=0;i<len;i++){
        sum+=*(data+i);
    }
    return sum;
}
/*打包函数*/
int pack_frame(unsigned char *out,const char *playload){

    out[0]=0XAA;
    out[1]=0X55;
    
    int len=strlen(playload);
    if(len>MAX_PLAYLOAD)    return -1;
    out[2]=(len>>8)&0xff;
    out[3]=len&0xff;

    memcpy(out+4,playload,len);
    out[4+len]=calc_sum(out,len+4);
    return 4+len+1;
}

int recv_all(int fd,void *buf,int len){
    /*接收到的总字节数*/
    int total=0;
    while(total<len){
        ssize_t n=recv(fd,(char *)buf+total,len-total,0);
        if(n<0){
            perror("recv");
            return -1;
        }
        if(n==0){
            return -1;
        }
        total+=n;
    }
    return 0;
}

int recv_frame(int fd,char *playload,int maxlen){

    unsigned char hdr[4];
    if(recv_all(fd,hdr,4)<0) return -1;
    if(hdr[0]!=0XAA||hdr[1]!=0X55)  return -1;
    int len=(hdr[2]<<8)|hdr[3];
    if(len<0||len>=maxlen) return -1;
    unsigned char sum;
    if(recv_all(fd,playload,len)<0) return -1;
    if(recv_all(fd,&sum,1)<0) return -1;
    unsigned char s=calc_sum(hdr,4);
    s+=calc_sum((const unsigned char *)playload,len);
    if(s!=sum)return -1;
    playload[len]='\0';
    return len;
}