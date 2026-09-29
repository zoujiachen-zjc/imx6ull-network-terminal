#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8888
#define LED_BRIGHTNESS  "/sys/class/leds/red/brightness"
#define LED_TRIGGER     "/sys/class/leds/red/trigger"

/*定义全局锁*/
static pthread_mutex_t led_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t cpu_mutex=PTHREAD_MUTEX_INITIALIZER;

/*cpu占用率*/
static double g_cpu_usage=0.0;

/*后台线程*/
static void *cpu_monitor(void *arg){
    unsigned long long t1=0,t2=0,i1=0,i2=0;
    unsigned long long dt=0,di=0;
    double usage;
    (void) arg;
    while(1){
        if(read_cpu_snapshot(&t1,&i1)<0){
            sleep(1);
            continue;
        }
        usleep(1000000);
        if(read_cpu_snapshot(&t2,&i2)<0){
            sleep(1);
            continue;
        }
        dt=t2-t1;
        di=i2-i1;
        if(dt==0){
            sleep(1);
            continue;
        }
        usage=(double)(dt-di)*100.0/(double)dt;
        pthread_mutex_lock(&cpu_mutex);
        g_cpu_usage=usage;
        pthread_mutex_unlock(&cpu_mutex);
    }
    return NULL;
}


//设置led
int set_led(int on){    
    FILE *fp;
    int ret=-1;
    pthread_mutex_lock(&led_mutex);
    
    fp=fopen(LED_BRIGHTNESS,"w");
    if(fp!=NULL){
        fprintf(fp,"%d",on?1:0);
        fclose(fp);
        ret=0;
    }
    else{
        perror("led open");
        ret=-1;
    }
    pthread_mutex_unlock(&led_mutex);
    return -1;
}
//获取执行时间
void get_uptime(char * buf,int size){

    FILE* fp;
    double uptime;
    fp=fopen("/proc/uptime","r");
    if(fp==NULL){
        snprintf(buf,size,"GET_UPTIME ERROR");
        return ;
    }
    fscanf(fp,"%lf",&uptime);
    fclose(fp);
    snprintf(buf,size,"%.2f s",uptime);
}
//获取内存使用情况
void get_mem(char * buf,int size){
    FILE *fp;
    char line[256];
    unsigned long total_kb=0;
    unsigned long aviliable_kb=0;
    fp=fopen("/proc/meminfo","r");
    if(fp==NULL){
        snprintf(buf,size,"GET_MEM ERROR");
        return ;
    }
    while(fgets(line,sizeof(line),fp)!=NULL){
        if(strncmp(line,"MemTotal",8)==0){
            sscanf(line+9,"%lu",&total_kb);
        }
        else if(strncmp(line,"MemAvailable:",12)==0){
            sscanf(line+13,"%lu",&aviliable_kb);
        }
    }
    fclose(fp);
    unsigned long total_mb=total_kb/1024;
    unsigned long aviliable_mb=aviliable_kb/1024;
    unsigned long used = (total_kb-aviliable_kb) * 100 / total_kb;
    snprintf(buf, size, "MEM: total=%luMB\nfree=%luMB\nused=%lu%%",
         total_mb, aviliable_mb, used);
}
//读取cpu快照
int read_cpu_snapshot(unsigned long long *total,unsigned long long *idle){
    unsigned long long user=0,nice=0,sys=0,idle_v=0,
    iowait=0,irq=0,softirq=0,steal=0;
    FILE *fp;
    char line[256];
    fp=fopen("/proc/stat","r");
    if(fp==NULL){
        return -1;
    }
    if(fgets(line,sizeof(line),fp)==NULL){
        fclose(fp);
        return -1;
    }
    fclose(fp);
    if(sscanf(line,"cpu %llu %llu %llu %llu %llu %llu %llu %llu",
    &user,&nice,&sys,&idle_v,&iowait,&irq,&softirq,&steal)<4){
        return -1;
    }
    *total=user+nice+sys+idle_v+iowait+irq+softirq+steal;
    *idle=idle_v+iowait;
    return 0;
}
//获取cpu占用率
void get_cpu(char *buf,int size){
    double usage;
    pthread_mutex_lock(&cpu_mutex);
    usage=g_cpu_usage;
    pthread_mutex_unlock(&cpu_mutex);
    snprintf(buf,size,"CPU: %.1f%%", usage);
}

void get_ip(char *buf, int size){

    int fd=socket(AF_INET, SOCK_DGRAM, 0);
    if(fd<0){
        snprintf(buf,size,"GET_IP ERROR");
        return;
    }
    struct ifreq ifr;
    memset(&ifr,0,sizeof(ifr));
    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1);
    //获取ip地址
    if(ioctl(fd,SIOCGIFADDR,&ifr)<0){
        perror("ioctl");
        close(fd);
        snprintf(buf,size,"GET_IP ERROR");
        return ;
    }
    struct sockaddr_in *addr=(struct sockaddr_in *)&ifr.ifr_addr;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET,&addr->sin_addr,ip,sizeof(ip));

      //获取mac地址
    if(ioctl(fd,SIOCGIFHWADDR,&ifr)<0){
        perror("ioctl hwaddr");
        close(fd);
        snprintf(buf,size,"GET_MAC ERROR");
        return ;
    }
    unsigned char *m= (unsigned char*)ifr.ifr_hwaddr.sa_data;
    char mac[18];
    snprintf(mac,sizeof(mac),"%02x:%02x:%02x:%02x:%02x:%02x",
    m[0],m[1],m[2],m[3],m[4],m[5]);
    snprintf(buf,size,"IP:%s MAC:%s",ip,mac);
    close(fd);
}

//获取芯片温度
void get_temp(char *buf,int size){

    FILE *fp;
    int temp_mc;
    fp=fopen("/sys/class/thermal/thermal_zone0/temp","r");
    if(fp==NULL){
        snprintf(buf,size,"GET_TEMP ERROR");
        return ;
    }
    if(fscanf(fp,"%d",&temp_mc)!=1){
        snprintf(buf,size,"GET_TEMP ERROR");
        return ;
    }
    fclose(fp);
    snprintf(buf,size,"TEMP:%lf C",temp_mc/1000.0);
}
/*线程函数*/
void *client_handler(void *arg) {

  int client_fd = *(int *)arg;
  /*用完立刻释放，避免内存泄漏*/
  free(arg);
  char buf[1024];
  char send_buf[1024];
  while (1) {
    memset(buf, 0, sizeof(buf));
    memset(send_buf, 0, sizeof(send_buf));
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n == 0) {
      printf("客户端已断开连接\n");
      break;
    } else if (n < 0) {
      perror("recv");
      break;
    } else {
      printf("recv:%s\n", buf);
      if (strcmp(buf, "PING") == 0) {
        snprintf(send_buf, sizeof(send_buf), "PONG");
      } else if (strcmp(buf, "GET_UPTIME") == 0) {
        get_uptime(send_buf, sizeof(send_buf));
      } else if (strcmp(buf, "GET_MEM") == 0) {
        get_mem(send_buf, sizeof(send_buf));
      } else if (strcmp(buf, "GET_CPU") == 0) {
        get_cpu(send_buf, sizeof(send_buf));
      } else if (strcmp(buf, "GET_IP") == 0) {
        get_ip(send_buf, sizeof(send_buf));
      } else if (strcmp(buf, "GET_TEMP") == 0) {
        get_temp(send_buf, sizeof(send_buf));
      } else if (strcmp(buf, "LED_ON") == 0) {
        if (set_led(1) == 0) {
          snprintf(send_buf, sizeof(send_buf), "LED_ON");
        } else {
          snprintf(send_buf, sizeof(send_buf), "LED_ERROR");
        }
      } else if (strcmp(buf, "LED_OFF") == 0) {
        if (set_led(0) == 0) {
          snprintf(send_buf, sizeof(send_buf), "LED_OFF");
        } else {
          snprintf(send_buf, sizeof(send_buf), "LED_ERROR");
        }
      } else {
        snprintf(send_buf, sizeof(send_buf), "Unknown Command", buf);
      }
      send(client_fd, send_buf, strlen(send_buf), 0);
    }
  }
  close(client_fd);
  return NULL;
}
int main(){

    //1.创建socket
    int server_fd;
    int client_fd;
    char buf[1024];
    char send_buf[1024];
    server_fd=socket(AF_INET,SOCK_STREAM,0);
    if(server_fd<0){
        perror("socket");
        return -1;
    }
    //2.设置服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(PORT);
    server_addr.sin_addr.s_addr=INADDR_ANY;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    //3.绑定
    if(bind(server_fd,(struct sockaddr *)&server_addr,sizeof(server_addr))<0){
        perror("bind");
        close(server_fd);
        return -1;
    }
    //4.监听
    listen(server_fd,5);
    pthread_t cpu_tid;
    if(pthread_create(&cpu_tid,NULL,cpu_monitor,NULL)!=0){
        perror("cpu thread create");
        close(server_fd);
        return -1;
    }
    pthread_detach(cpu_tid);
    printf("server wait ....\n");
    while(1){
        int *pfd=malloc(sizeof(int));
        *pfd=accept(server_fd,NULL,NULL);
        if(*pfd<0){
            perror("accept");
            free(pfd);
            continue;
        }
        printf("客户端已连接\n");
        pthread_t tid;
        if(pthread_create(&tid,NULL,client_handler,pfd)!=0){
            perror("pthread_create");
            close(*pfd);
            free(pfd);
            continue;
        }
        pthread_detach(tid);
        /*线程结束自动回收资源*/
    }
    return 0;
}