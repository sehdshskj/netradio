#include<stdio.h>
#include<stdlib.h>
#include<errno.h>
#include<unistd.h>
#include<getopt.h>
#include "client.h"
//#include "proto.h"
#include "../include/proto.h"
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<netinet/ip.h>
#include<arpa/inet.h>
#include<net/if.h>
#include<sys/file.h>
#include<fcntl.h>
#include<linux/input.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<string.h>
#include<signal.h>
#include<sys/types.h>
#include<pthread.h>
#include<sys/wait.h>
#include<linux/input.h>
#include <sys/types.h>
#include<time.h>

/**
 * 需要向其他用户手动添加文件/dev/input/event0的读权限
 * 
 */


/**
 *mp3缓冲区定义 
*/
#define BUF_MP3             "/home/chh/temp/buf_mp3"
#define BUF_MP3_LEN        (1024 * 1024 * 16)//指定缓冲区的大小
#define PIPEBLOCK          512//每次向管道内发送的数据
/**
 * 
 * KEY_UP       上一个频道
 * KEY_DOWN     下一个频道
 * KEY_LEFT     上一首
 * KEY_RIGHT    下一首
 * KEY_SPACE    暂停
 * KEY_Q        退出
 * KEY_ENTER    播放
 */

/**
 * -M --multigroup 指定多播组
 * -P --port       指定接收端口
 * -p --player     指定播放器
 * -H --help       显示帮助
*/
static int pd[2];
static pid_t pid;
static pthread_t tid_read;//读文件缓冲线程
static int buf_fd;
static off_t pos_write = SEEK_SET;//缓冲文件的写位置
static off_t pos_read = SEEK_SET;//缓冲文件的读位置

static pthread_t tid_player_cntl;//控制信息发送线程
static int sdlocal_cntl;
static struct sockaddr_in addrlocal, addrserver;
static socklen_t addrlocal_len, addrserver_len;
static struct  msg_cntl_st msg_cntl;
static int chosenid;
static int total_list = 3;

//线程并发
static pthread_mutex_t bufmp3_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_readmp3buf = PTHREAD_COND_INITIALIZER, cond_writemp3buf = PTHREAD_COND_INITIALIZER;
static int buf_rdwr = 1;

//main函数中的常用测试变量
static int err;
static int val;
static int c;
static int sd;
static int recv_len;
void msg_cntl_send(struct  msg_cntl_st msg_cntl);
void sdlocal_cntl_init(struct sockaddr_in addrserver);
struct client_conf_st client_conf = {\
        .rcvport = DEFAULT_RCVPORT,\
        .mgroup = DEFAULT_MGROUP,\
        .cntlport = DEFAULT_CNTLPORT_LOCAL,\
        .player_cmd = DEFAULT_PLAYERCMD};



void print_help()
{
    printf("-M --multigroup 指定多播组 \n");
    printf("-P --port       指定接收端口\n");
    printf("-p --player     指定播放器\n");
    printf("-H --help       显示帮助\n");
    printf("KEY_UP          上一首\n");
    printf("KEY_DOWN        下一首\n");
    printf("KEY_LEFT        回退\n");
    printf("KEY_RIGHT       快进\n");
    printf("KEY_SPACE       暂停\n");
    printf("KEY_F           一个频道\n");
    printf("KEY_N           下一个频道\n");
    printf("KEY_H           打印帮助 \n");
    printf("KEY_Q           退出 \n");
    printf("KEY_ENTER       播放 \n");
}

static ssize_t writen(int fd, const uint8_t *buf, size_t len)//写线程从循环缓冲文件中读出数据
{
    int ret;
    int pos = 0;
    int the_more;
    the_more =len + pos_write -BUF_MP3_LEN;
    //printf("是否写越界？len : %ld the_more:%d pos_write : %ld\n", len, the_more, pos_write);
    if(the_more > 0)
    {
        if(writen(fd, buf + pos, len - the_more ) < 0)
        {
            perror("writen()");
            printf("循环写失败\n");
        }
        pos += len - the_more;
        pos_write = SEEK_SET;
        len = the_more;
    }
    while(len > 0)
    {
        ret = pwrite(fd, buf + pos, len, pos_write);
        //printf("是否写成功？len : %ld ret:%d pos_write : %ld\n", len, ret, pos_write);
        if(ret < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            fprintf(stderr, "pwrite(): %s\n", strerror(errno));
            exit(1);
        }
        len -= ret;
        pos += ret;
        pos_write += ret;
    }
    return pos;
}
static ssize_t readn(int buf_fd , uint8_t *buf, size_t len)
{
    int pos = 0;
    int ret = 0;
    int the_more;
    the_more = len + pos_read - BUF_MP3_LEN;
    if(the_more > 0)
    {
        ret = readn(buf_fd, buf + pos, len - the_more);
        if(ret < 0)
        {
            perror("readn()");
            printf("循环读失败\n");            
        }
        pos += len - the_more;
        pos_read = SEEK_SET;
        len = the_more;
    }
    while(len > 0)
    {
        ret = pread(buf_fd , buf + pos, len, pos_read);
        //printf("是否读成功？len : %ld ret:%d pos_read : %ld\n", len, ret, pos_read);
        if(ret < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            perror("bufmp3_read()");
            exit(1);
        }
        len -= ret;
        pos += ret;
    }    
    return pos;
}
static void *bufmp3_read(void *ptr)//子线程从循环缓冲文件中读出数据
{
    int pos = 0;
    int ret = 0;
    size_t len;
    uint8_t buf[PIPEBLOCK + 1];
    while(1)
    {
        pos = 0;
        ret = 0;
        len = PIPEBLOCK;
        pthread_mutex_lock(&bufmp3_lock);
        while(buf_rdwr == 0)
        {
            pthread_cond_wait(&cond_readmp3buf, &bufmp3_lock);
        }    
        buf_rdwr = 0;    
        if((pos_write >= pos_read?pos_write - pos_read : pos_write + BUF_MP3_LEN - pos_read) > PIPEBLOCK)
        {
            if(readn(buf_fd, buf, len) < 0)
            {
                fprintf(stderr, "readn():%s\n", strerror(errno));
            }
            //printf("是否读成功？len : %ld ret:%d pos_read : %ld\n", len, ret, pos_read);
            pos_read += PIPEBLOCK;  
            pos = 0;
            ret = 0;
            len = PIPEBLOCK;
            while(len > 0)
            {
                ret = write(pd[1], buf + pos, len);
                if(ret < 0)
                {
                    if(errno == EINTR)
                    {
                        continue;
                    }
                    perror("writen()");
                    exit(1);
                }
                //printf("成功向管道写入：%d\n", ret); 
                len -= ret;
                pos += ret;
            }        
        }
        // else 
        // {
        //     sleep(1);
        //     msg_cntl.type = KEY_ENTER;
        //     msg_cntl.chnid = chosenid;
        //     msg_cntl_send(msg_cntl);
        // }
        buf_rdwr = 1;
        pthread_cond_signal(&cond_writemp3buf);
        pthread_mutex_unlock(&bufmp3_lock);        
        
    }
    pthread_exit(NULL);
}

void sdlocal_cntl_init(struct sockaddr_in addrserver)
{
    sdlocal_cntl = socket(AF_INET, SOCK_STREAM, 0);
    if(sdlocal_cntl < 0)
    {
        perror("socket(AF_INET, SOCK_STREAM, 0):");
    }
    addrlocal.sin_family = AF_INET;
    inet_pton(AF_INET, "0.0.0.0", &addrlocal.sin_addr.s_addr);
    addrlocal.sin_port = htons(atoi(client_conf.cntlport));
    addrlocal_len = sizeof(addrlocal);
    if(bind(sdlocal_cntl, (struct sockaddr *)&addrlocal, addrlocal_len) < 0)
    {
        perror("bind(sdlocal_cntl, (struct sockaddr *)&addrlocal, addrlocal_len):");
    }
    //端口需要设置成已知的端口
    addrserver.sin_port = htons(atoi(DEFAULT_CNTLPORT_SERVER));
    //
    //connect(sdlocal_cntl, (struct sockaddr *)&addrserver,sizeof(addrserver));
    if(connect(sdlocal_cntl, (struct sockaddr *)&addrserver,sizeof(addrserver)) < 0)
    {
        perror("connect(sdlocal_cntl, (struct sockaddr *)&addrserver,sizeof(addrserver)):");
    }
    /**if error*/
}

void msg_cntl_send(struct  msg_cntl_st msg_cntl)
{
    int send_len;
    while((send_len = send(sdlocal_cntl, &msg_cntl, sizeof(msg_cntl), MSG_WAITALL)) < sizeof(msg_cntl))
    {
        printf("msg_type : %d size: %d\n", msg_cntl.type, send_len);
        perror("send(sdlocal_cntl, &msg_cntl, MSG_WAITALL):");
    }
}

static pid_t childproc_restar()
{
    //printf("开始重启子进程\n");
    recv_len = 0;
    //clean_sock_pipe_msg(); 
    close(pd[0]);
    close(pd[1]);   
    if(pipe(pd) < 0)
    {
        perror("pipe()");
        exit(1);
    } 
    pid = fork();
    if(pid < 0)
    {
        perror("fork()");
        exit(1);
    }
    else if(pid == 0)
    {       
        //子进程：调用解码器
        close(pd[1]);
        close(sd);
        dup2(pd[0],STDIN_FILENO);
        if(pd[0] > STDIN_FILENO)
        {
            close(pd[0]);
        }
        printf("开始调用mpg123\n");
        execl("/bin/sh", "sh", "-c", client_conf.player_cmd, NULL);
        perror("execl()");
        exit(1);
    }  
    kill(pid, SIGCONT);
    return pid;  
}

static void* player_cntl(void *ptr)//播放器的控制函数
{
    /**
     * @brief 
     * 
     */
    int event_fd;
    int ret = 0;
    struct input_event key_event;
    if((event_fd = open("/dev/input/event0", O_RDONLY)) < 0)
    {
        perror("open():");
        exit(1);
    }
    while(1)
    {
        ret = 0;
        key_event.type = 0;
        ret = read(event_fd, &key_event, sizeof(key_event));
        if(ret == sizeof(key_event) && key_event.type == EV_KEY && key_event.value == 1)
        {  
            printf("recieved message is : %d\n", key_event.code);
            switch(key_event.code)
            {
                case KEY_LEFT:
                {
                    printf("KEY_UP 播放上一首\n");        
                    msg_cntl.type = KEY_SPACE;
                    msg_cntl.chnid = chosenid;
                    msg_cntl_send(msg_cntl); 
                    kill(pid , SIGKILL);   
                    while(wait(NULL) != pid); 
                    sleep(2);
                    printf("子进程已经杀死\n");  
                    pthread_mutex_lock(&bufmp3_lock);
                    buf_rdwr = 0; 
                    while(childproc_restar() == 0);         
                    pos_write = 0;
                    pos_read = pos_write;
                    buf_rdwr = 1; 
                    pthread_cond_signal(&cond_writemp3buf);                     
                    pthread_mutex_unlock(&bufmp3_lock);                    
                    break;
                }
                case KEY_RIGHT:
                {
                    printf("KEY_UP 播放下一首\n");        
                    msg_cntl.type = KEY_SPACE;
                    msg_cntl.chnid = chosenid;
                    msg_cntl_send(msg_cntl); 
                    kill(pid , SIGKILL);   
                    while(wait(NULL) != pid); 
                    sleep(2);
                    printf("子进程已经杀死\n");  
                    pthread_mutex_lock(&bufmp3_lock);
                    buf_rdwr = 0; 
                    while(childproc_restar() == 0);         
                    pos_write = 0;
                    pos_read = pos_write;
                    buf_rdwr = 1; 
                    pthread_cond_signal(&cond_writemp3buf);                     
                    pthread_mutex_unlock(&bufmp3_lock);                        
                    break;
                }
                case KEY_SPACE:
                {
                    printf("KEY_SPACE 暂停播放\n"); 
                    pthread_mutex_lock(&bufmp3_lock); 
                    buf_rdwr = 0;    
                    pthread_mutex_unlock(&bufmp3_lock);             
                    break;
                }
                case KEY_UP:
                {
                    printf("KEY_UP 播放上一频道\n");        
                    msg_cntl.type = KEY_SPACE;
                    msg_cntl.chnid = chosenid;
                    msg_cntl_send(msg_cntl); 
                    kill(pid , SIGKILL);   
                    while(wait(NULL) != pid); 
                    sleep(2);
                    printf("子进程已经杀死\n");  
                    pthread_mutex_lock(&bufmp3_lock);
                    buf_rdwr = 0; 
                    while(childproc_restar() == 0);         
                    pos_write = 0;
                    pos_read = pos_write;
                    buf_rdwr = 1; 
                    pthread_cond_signal(&cond_writemp3buf);                     
                    pthread_mutex_unlock(&bufmp3_lock);
                    if(chosenid == 1)
                    {
                        chosenid = total_list;
                    }
                    else
                    {
                        chosenid = (chosenid - 1);
                    }
                    break;
                }
                case KEY_DOWN:
                {   
                    printf("KEY_DOWN 播放下一频道\n");        
                    msg_cntl.type = KEY_SPACE;
                    msg_cntl.chnid = chosenid;
                    msg_cntl_send(msg_cntl); 
                    kill(pid , SIGKILL);   
                    while(wait(NULL) != pid); 
                    sleep(2);
                    printf("子进程已经杀死\n");                        
                    pthread_mutex_lock(&bufmp3_lock);
                    buf_rdwr = 0; 
                    while(childproc_restar() == 0);         
                    pos_write = 0;
                    pos_read = pos_write;
                    buf_rdwr = 1; 
                    pthread_cond_signal(&cond_writemp3buf);                     
                    pthread_mutex_unlock(&bufmp3_lock);
                    if(chosenid == total_list)
                    {
                        chosenid = 1;
                    }
                    else
                    {
                        chosenid = (chosenid + 1);
                    }
                    break;
                }
                case KEY_H:
                {
                     print_help();
                     break;
                }
                case KEY_ENTER:
                {
                    //kill(pid , SIGCONT);
                    pthread_mutex_lock(&bufmp3_lock); 
                    printf("KEY_ENTER 继续播放\n"); 
                    buf_rdwr = 1;   
                    pthread_mutex_unlock(&bufmp3_lock); 
                    pthread_cond_signal(&cond_writemp3buf); 
                    break;
                }                
                case KEY_Q:
                {
                    close(event_fd);
                    close(buf_fd);
                    close(sdlocal_cntl);
                    if(remove(BUF_MP3) < 0)
                    {
                        perror("remove():");
                    }  
                    pthread_mutex_lock(&bufmp3_lock);
                    pthread_cancel(tid_read);
                    pthread_join(tid_read, NULL);
                    pthread_mutex_unlock(&bufmp3_lock);
                    pthread_mutex_destroy(&bufmp3_lock);
                    pthread_cond_destroy(&cond_readmp3buf);
                    pthread_cond_destroy(&cond_writemp3buf);
                    kill(pid, SIGKILL);
                    wait(NULL);
                    printf("已经杀死了子进程\n");                                                    
                    exit(0);
                    break;
                }                
                default:
                {
                    break;
                }                                                                                                                                
            }
            msg_cntl.type = key_event.code;
            msg_cntl.chnid = chosenid;
            msg_cntl_send(msg_cntl);
        }
    }
    pthread_exit(NULL);
}

int main(int argc, char** argv)
{
    struct ip_mreqn mreq;
    int index = 0;
    mode_t oldmask;
    struct sockaddr_in laddr, remoteaddr;
    socklen_t  remoteaddr_len;
    struct option argarr[] = {{"port", 1, NULL,'P'},{"mgroup", 1, NULL, 'M'},\
                              {"player", 1, NULL, 'p'},{"help", 0, NULL, 'H'},\
                              {NULL, 0, NULL, 0}};
    /**
     * 初始化级别：默认值，配置文件，环境变量，命令行参数
     *
     * */
    while(1)
    {
        c = getopt_long(argc, argv,"P:M:p:H",argarr,&index);
        if(c < 0) break;
        switch(c)
        {
            case 'P':
                client_conf.rcvport = optarg;
                break;
            case 'M':
                client_conf.mgroup = optarg;
                break;
            case 'p':
                client_conf.player_cmd = optarg;
                break;
            case 'H':
                print_help();         
                exit(0);
                break;
            default:
                abort();
                break;
        }
    }
    //数据收发套接字
    sd = socket(AF_INET,SOCK_DGRAM,0);
    if(sd < 0)
    {
        perror("socket()");
        exit(1);
    }
    inet_pton(AF_INET, client_conf.mgroup, &mreq.imr_multiaddr);
    /*if error*/

    inet_pton(AF_INET, "0.0.0.0", &mreq.imr_address);
    /*if error*/
    mreq.imr_ifindex = if_nametoindex("eth0");    //网卡设备名称和索引之间的映射
    /*if error*/
    //添加多播地址
    if(setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
    {
        perror("setsockopt()");
        exit(1);
    }

    val = 1;
    if(setsockopt(sd, IPPROTO_IP, IP_MULTICAST_LOOP, &val, sizeof(val)))
    {
        perror("setsockopt()");
        exit(0);
    }
    //绑定本地地址
    laddr.sin_family = AF_INET;
    laddr.sin_port = htons(atoi(client_conf.rcvport));
    inet_pton(AF_INET, "0.0.0.0",&laddr.sin_addr.s_addr);
    if(bind(sd, (void*)&laddr, sizeof(laddr)) < 0 )
    {
        perror("bind()");
        exit(1);
    }
    //开辟一个管道
    if(pipe(pd) < 0)
    {
        perror("pipe()");
        exit(1);
    }
    //创建缓冲文件
    oldmask = umask(0);
    buf_fd = open(BUF_MP3, O_RDWR | O_CREAT, 0777);
    umask(oldmask);
    //创建子进程
    //signal(SIGCHLD, childproc_restar);
    pid = fork();
    if(pid < 0)
    {
        perror("fork()");
        exit(1);
    }
    else if(pid == 0)
    {
        //子进程：调用解码器
        close(sd);
        close(pd[1]);
        dup2(pd[0],STDIN_FILENO);
        if(pd[0] > STDIN_FILENO)
        {
            close(pd[0]);
        }
        printf("开始调用mpg123\n");
        execl("/bin/sh", "sh", "-c", client_conf.player_cmd, NULL);
        perror("execl()");
        exit(1);
    }
    //父进程：从网络上收包，发送给子进程
    //收节目单
    struct msg_list_st *msg_list; 
    
    msg_list = malloc(MSG_LIST_MAX);
    if(msg_list == NULL)
    {
        perror("malloc()");
        exit(1);
    }
    addrserver_len = sizeof(struct sockaddr_in);
    while(1)
    {
        recv_len = recvfrom(sd, &total_list, sizeof(total_list), 0, (void *)&addrserver, &addrserver_len);
        if(recv_len < sizeof(int))
        {
            fprintf(stderr, "message is too small.\n");
            continue;
        }
        recv_len = recvfrom(sd, msg_list, MSG_LIST_MAX, 0, (void *)&addrserver, &addrserver_len);
        if(recv_len < sizeof(struct msg_list_st))
        {
            fprintf(stderr, "message is too small.\n");
            continue;
        }
        if(msg_list->chnid != LISTCHNID)
        {
            fprintf(stderr,"chnid is not match.\n");
            continue;
        }
        break;
    }
    //初始化控制端套接字
    sdlocal_cntl_init(addrserver);
    //打印节目单并选择频道
    struct msg_listentry_st *pos;
    printf("the number of channel is %d\n",total_list);
    for(pos = msg_list->entry; (char *)pos < ((char *)msg_list) + recv_len; pos = (void *)(((char *)pos) + ntohs(pos->len)))
    {
        printf("channel %d: %s\n",pos->chnid, pos->desc);
    }
    free(msg_list);
    while(scanf("%d",&chosenid) != 1)
    {
        exit(1);
    }
    msg_cntl.type = KEY_ENTER;
    msg_cntl.chnid = chosenid;
    msg_cntl_send(msg_cntl);
    //打开键盘监控
    if((err = pthread_create(&tid_player_cntl, NULL, player_cntl, NULL)) < 0)
    {
        fprintf( stderr,"thread of  player_cntl create failed\n");
    }
    //收频道包，发送给子进程
    struct msg_channel_st *msg_channel;
    msg_channel = malloc(MSG_CHANNEL_MAX);
    if(msg_channel == NULL)
    {
        perror("malloc()");
        exit(1);
    }
    remoteaddr_len = sizeof(struct sockaddr_in);
    if((err = pthread_create(&tid_read, NULL, bufmp3_read, pd)) < 0)
    {
        fprintf(stderr, "pthread_create():%s.\n", strerror(err));
        exit(1);
    }
    while(1)
    {
        recv_len = recvfrom(sd, msg_channel, MSG_CHANNEL_MAX, 0, (void *)&remoteaddr, &remoteaddr_len);
        if(recv_len == 0 || remoteaddr.sin_addr.s_addr != addrserver.sin_addr.s_addr || remoteaddr.sin_port != addrserver.sin_port)
        {   //当读到不符合的数据包时recvfrom返回0；msg_channel不发生改变
            fprintf(stderr, "IGnore:address or data not match.\n");
            continue;
        }  
        pthread_mutex_lock(&bufmp3_lock);   
        while(buf_rdwr == 0)
        {
            pthread_cond_wait(&cond_writemp3buf, &bufmp3_lock);
        }
        buf_rdwr = 0;            
        if(recv_len < sizeof(struct msg_channel_st))
        {            
            if(recv_len < 0)
            {
                fprintf(stderr,"read from socket failed\n");
            }
            fprintf(stderr,"Ignore: message is too small.\n");
            pthread_cond_signal(&cond_readmp3buf);
            pthread_mutex_unlock(&bufmp3_lock);             
            continue;
        }                
        if(msg_channel->chnid == chosenid && recv_len - sizeof(chnid_t) > 0)
        {
            //printf("msg_channel->chnid %d chosenid%d recv_len%d\n",msg_channel->chnid, chosenid, recv_len);
            //fprintf(stdout, "accepted message: %d recieved.\n",msg_channel->chnid);                    
            if((pos_write >= pos_read?pos_write - pos_read : pos_write + BUF_MP3_LEN - pos_read) < BUF_MP3_LEN / 16 * 15)
            {        
                printf("msg_channel->chnid %d chosenid%d recv_len%d\n",msg_channel->chnid, chosenid, recv_len);
                if(writen(buf_fd, msg_channel->data, recv_len - sizeof(chnid_t)) < 0)
                {
                    exit(1);
                }
                //printf("成功写入了包\n");
            }
            else
            {
                msg_cntl.type = KEY_SPACE;
                msg_cntl.chnid = chosenid;
                msg_cntl_send(msg_cntl);
            }          
        }
        buf_rdwr = 1;  
        pthread_cond_signal(&cond_readmp3buf);
        pthread_mutex_unlock(&bufmp3_lock);         
    }
    free(msg_channel);
    free(msg_list);
    close(buf_fd);
    close(sd);
    close(sdlocal_cntl);
    pthread_mutex_destroy(&bufmp3_lock);
    pthread_cond_destroy(&cond_readmp3buf);
    pthread_cond_destroy(&cond_writemp3buf);
    if(-remove(BUF_MP3))
    {
        perror("remove():");
    }
    kill(pid, SIGKILL);
    wait(NULL);
    printf("已经杀死了子进程\n");
    exit(0);
}