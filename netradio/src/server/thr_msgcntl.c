#include "thr_msgcntl.h"
#include <pthread.h>

int sdlocal_cntl;
struct sockaddr_in addrlocal, addrclient;
socklen_t addrlocal_len, addrclient_len;
int lisentsd;
pthread_mutex_t channel_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  channel_cond[CHNNR + 1],sock_cond[CHNNR + 1];
int issend[CHNNR + 1] = {1,0};//用来哪个线程发送
int oldsendchannel = 0;//原来正在发送的频道
int sock_send = 1;


void sdlocal_cntl_init()//控制端口初始化
{
    sdlocal_cntl = 0;
    lisentsd = socket(AF_INET, SOCK_STREAM, 0);
    if(lisentsd < 0)
    {
        perror("socket(AF_INET, SOCK_STREAM, 0):");
    }
    addrlocal.sin_family = AF_INET;
    inet_pton(AF_INET, "0.0.0.0", &addrlocal.sin_addr.s_addr);
    addrlocal.sin_port = htons(atoi(server_conf.cntlport));
    addrlocal_len = sizeof(addrlocal);
    if(bind(lisentsd, (struct sockaddr *)&addrlocal, addrlocal_len) < 0)
    {
        perror("bind(sd, (struct sockaddr *)&addrlocal, addrlocal_len):");
    }
    if(listen(lisentsd, SOMAXCONN) < 0)
    {
        perror("listen(sd, SOMAXCONN):");
    }
    //connect(sdlocal_cntl, (struct sockaddr *)&addrserver,sizeof(addrserver));
    /**if error*/
}

void *msg_cntl_listen(void *ptr)//接受控制信息
{
    // struct timespec breaktime;
    // breaktime.tv_sec = 1;
    // breaktime.tv_nsec = 0;
    sdlocal_cntl = 0;
    sdlocal_cntl = accept(lisentsd, (struct sockaddr *)&addrclient, &addrclient_len);
    char address[30];
    inet_ntop(AF_INET, &addrclient.sin_addr.s_addr, address, 30);
    printf("addrclient : %s sdlocal_cntl : %d\n", address, sdlocal_cntl);
    if(sdlocal_cntl < 0)
    {
        perror("accept():");
    }    
    printf("addrclient : %d sdlocal_cntl : %d\n", ntohs(addrclient.sin_addr.s_addr), sdlocal_cntl);
    if(sdlocal_cntl)
    {
        printf("clent and client connect succeed\n");
    }
    int recv_len = 0;
    struct msg_cntl_st msg_cntl;
    while(1)
    {
        printf("recv_len in msg_cntl()\n");
        recv_len = recvfrom(sdlocal_cntl, &msg_cntl, sizeof(struct msg_cntl_st), MSG_WAITALL, NULL, NULL);
        printf("addrclient : %d sdlocal_cntl : %d\n", ntohs(addrclient.sin_addr.s_addr), sdlocal_cntl);
        if(recv_len < 0)
        {
            perror("recv_len in msg_cntl()");
            break;
        }
        if(recv_len == sizeof(struct msg_cntl_st))
        {
            printf("recieved message is : KEY_CODE %d CHNID %d\n", msg_cntl.type,msg_cntl.chnid);
            msg_cntl_handle(msg_cntl);
        }
		// while(nanosleep(&breaktime, &breaktime) != 0) 
        // {
		// 	if (errno != EINTR) 
        //     {
        //         fprintf(stderr, "nanosleep():%s\n", strerror(errno));
		// 		exit(1);
		// 	}
		// }
    }
    pthread_exit(NULL);
}
void msg_cntl_handle(struct msg_cntl_st msg_cntl)//处理控制信息
{
    switch(msg_cntl.type)
            {
                case KEY_LEFT:
                {
                    pthread_mutex_lock(&channel_lock);                    
                    printf("KEY_UP 播放上一首\n");
                    channel[msg_cntl.chnid].pos = (channel[msg_cntl.chnid].pos + channel[msg_cntl.chnid].mp3glob.gl_pathc - 2) % channel[msg_cntl.chnid].mp3glob.gl_pathc;
                    channel[msg_cntl.chnid].offset = 0; 
                    open_next(msg_cntl.chnid);                   
                    issend[oldsendchannel] = 0;
                    issend[msg_cntl.chnid] = 1;
                    oldsendchannel = msg_cntl.chnid;
                    pthread_cond_signal(channel_cond + msg_cntl.chnid);                    
                    perror("pthread_cond_signal():");
                    pthread_mutex_unlock(&channel_lock);    
                    //解放套接字
                    pthread_mutex_lock(&sd_lock);
                    sock_send = 1;
                    pthread_cond_signal(sock_cond + msg_cntl.chnid);  
                    pthread_mutex_unlock(&sd_lock);                                                    
                    break;
                }
                case KEY_RIGHT:
                {
                    pthread_mutex_lock(&channel_lock);
                    open_next(msg_cntl.chnid);
                    printf("KEY_UP 播放下一首\n");
                    issend[oldsendchannel] = 0;
                    issend[msg_cntl.chnid] = 1;
                    oldsendchannel = msg_cntl.chnid;
                    pthread_cond_signal(channel_cond + msg_cntl.chnid);                    
                    perror("pthread_cond_signal():");
                    pthread_mutex_unlock(&channel_lock);    
                    //解放套接字
                    pthread_mutex_lock(&sd_lock);
                    sock_send = 1;
                    pthread_cond_signal(sock_cond + msg_cntl.chnid);  
                    pthread_mutex_unlock(&sd_lock);                                                    
                    break;                    
                    break;
                }
                case KEY_SPACE:
                {
                    printf("KEY_SPACE 暂停播放\n");
                    //锁套接字
                    pthread_mutex_lock(&sd_lock);
                    sock_send = 0;
                    pthread_mutex_unlock(&sd_lock);
                    //锁线程
                    pthread_mutex_lock(&channel_lock);
                    issend[oldsendchannel] = 0;
                    issend[msg_cntl.chnid] = 0;
                    oldsendchannel = 0;
                    pthread_cond_signal(channel_cond + msg_cntl.chnid);
                    perror("pthread_cond_signal():");
                    pthread_mutex_unlock(&channel_lock);     
                    break;
                }
                case KEY_UP:
                {   
                    channel[msg_cntl.chnid].pos = 0;
                    channel[msg_cntl.chnid].offset = 0;
                    pthread_mutex_lock(&channel_lock);
                    printf("KEY_UP 播放上一频道\n");
                    issend[oldsendchannel] = 0;
                    issend[msg_cntl.chnid] = 1;
                    oldsendchannel = msg_cntl.chnid;
                    pthread_cond_signal(channel_cond + msg_cntl.chnid);                    
                    perror("pthread_cond_signal():");
                    pthread_mutex_unlock(&channel_lock);    
                    //解放套接字
                    pthread_mutex_lock(&sd_lock);
                    sock_send = 1;
                    pthread_cond_signal(sock_cond + msg_cntl.chnid);  
                    pthread_mutex_unlock(&sd_lock);                                   
                    break;
                }
                case KEY_DOWN:
                {
                    channel[msg_cntl.chnid].pos = 0;
                    channel[msg_cntl.chnid].offset = 0;
                    pthread_mutex_lock(&channel_lock);
                    printf("KEY_DOWN 播放下一频道\n");
                    issend[oldsendchannel] = 0;
                    issend[msg_cntl.chnid] = 1;
                    oldsendchannel = msg_cntl.chnid;
                    pthread_cond_signal(channel_cond + msg_cntl.chnid);                    
                    perror("pthread_cond_signal():");
                    pthread_mutex_unlock(&channel_lock);    
                    //解放套接字
                    pthread_mutex_lock(&sd_lock);
                    sock_send = 1;
                    pthread_cond_signal(sock_cond + msg_cntl.chnid);  
                    pthread_mutex_unlock(&sd_lock);                                   
                    break;
                }
                case KEY_H:
                {
                     break;
                }
                case KEY_ENTER:
                {
                    printf("KEY_ENTER 继续播放\n");                 
                    //锁线程
                    pthread_mutex_lock(&channel_lock);
                    //printf("KEY_ENTER 继续播放\n");
                    issend[oldsendchannel] = 0;
                    issend[msg_cntl.chnid] = 1;
                    oldsendchannel = msg_cntl.chnid;
                    pthread_cond_signal(channel_cond + msg_cntl.chnid);
                    perror("pthread_cond_signal():");
                    pthread_mutex_unlock(&channel_lock);
                    //解锁套接字
                    pthread_mutex_lock(&sd_lock);
                    sock_send = 1;
                    pthread_cond_signal(sock_cond + msg_cntl.chnid);
                    pthread_mutex_unlock(&sd_lock);                     
                    break;
                }                
                case KEY_Q:
                {
                    printf("客户端已经关闭\n");
                    close(sdlocal_cntl);
                    msg_cntl_listen(NULL);
                    break;
                }                
                default:
                {
                    break;
                }                                                                                                                                
            }
}