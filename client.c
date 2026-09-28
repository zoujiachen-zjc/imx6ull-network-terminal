#include <stdio.h>
#include <sys/types.h>          
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>

#define SERVER_IP "192.168.137.125"
#define SERVER_PORT 8888

int main()
{
    int client_fd;
    char buf[1024];
    //1. 创建socket
    client_fd=socket(AF_INET, SOCK_STREAM, 0);
    if(client_fd<0){
        perror("socket");
        return -1;
    }
    //设置服务器ip地址
    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(SERVER_PORT);
    //将字符串ip转换成网络地址
    if(inet_pton(AF_INET,SERVER_IP,&server_addr.sin_addr)<=0){
        perror("inet_pton");
        close(client_fd);
        return -1;
    }
    //3.连接服务器
    if(connect(client_fd,(struct sockaddr *)&server_addr,sizeof(server_addr))<0){
        perror("connect");
        close(client_fd);
        return -1;
    }
    printf("connect server success!\n");
    while(1){
        memset(buf,0,sizeof(buf));
        printf("请输入发送内容:");
        fgets(buf,sizeof(buf),stdin);
        buf[strcspn(buf,"\n")]='\0';
        //输入quit 退出客户端
        if(strcmp(buf,"quit")==0){
            break;
        }
        //4.发送数据
        if(send(client_fd,buf,strlen(buf),0)<0){
            perror("send");
            break;
        }
            memset(buf,0,sizeof(buf));
            //5. 接收服务器的数据
            int ret=recv(client_fd,buf,sizeof(buf)-1,0);
            if(ret>0){
                buf[ret]='\0';
                printf("server recv:%s\n",buf);
            }
            else if(ret==0){
                printf("server closed\n");
                break;
            }
            else{
                perror("recv");
                break;
            }
     
    }
    //6.关闭socket
    close(client_fd);
    return 0;
}
