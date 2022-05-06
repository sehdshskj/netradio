#include <stdio.h>
#include <stdlib.h>
#include "server_conf.h"
#include "../include/proto.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>      
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <net/if.h>
#include "medialib.h"
#include "thr_list.h"
#include "thr_channel.h"
#include "thr_msgcntl.h"
/**
 * -M 指定多播组
 * -P 指定接收端口
 * -F 前台运行
 * -D 指定媒体库位置
 * -I 指定网络设备
 * -H 显示帮助
 */


struct server_conf_st server_conf = { .rcvport = DEFAULT_RCVPORT,\
                                      .mgruop = DEFAULT_MGROUP,\
                                      .media_dir = DEFAULT_MEDIADIR,\
                                      .runmode = RUN_DAEMON,\
                                      .cntlport = DEFAULT_CNTLPORT_SERVER,\
                                      .ifname = DEFAULT_IF};
void print_help()
{
    printf("-M 指定多播组\n");
    printf("-P 指定接收端口\n");
    printf("-F 前台运行\n");
    printf("-D 指定媒体库位置\n");
    printf("-I 指定网络设备\n");
    printf("-H 显示帮助\n");
}   
int serversd;
struct sockaddr_in sndaddr;
struct mlib_listentry_st *list;  
pthread_mutex_t sd_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t tid_msgcntl;


static  int daemonize(void)
{
    pid_t pid;
    pid = fork();
    if(pid < 0)
    {
//        perror("fock()");
        syslog(LOG_ERR,"fock():%s", strerror(errno));
        return -1;
    }

    if(pid > 0)
    {
        exit(0);
    }

    int fd = open("dev/null", O_RDWR);
    if(fd < 0)
    {
    //  perror("open()");
        syslog(LOG_WARNING,"open():%s", strerror(errno));
        return -2;
    }
    else
    {
        dup2(fd,STDERR_FILENO);
        dup2(fd,STDIN_FILENO);
        dup2(fd,STDOUT_FILENO);

        if(fd > STDOUT_FILENO)
        {
            close(fd);
        }
    }
    setsid();

    chdir("/");
    umask(0);
    return 0;
}

static void daemon_exit(int s)
{
    thr_list_destroy();
    thr_channel_destroyall();
    mlib_freechnlist(list);
    syslog(LOG_WARNING, "signal -%d caught, exit now", s);
    
    closelog();
    exit(0);
}

static void socket_init()
{
    struct ip_mreqn mreq;

    serversd = socket(AF_INET, SOCK_DGRAM, 0);
    if(serversd < 0)
    {
        syslog(LOG_ERR, "socket(): %s", strerror(errno));
        exit(1);
    }

    inet_pton(AF_INET, server_conf.mgruop, &mreq.imr_multiaddr);
    inet_pton(AF_INET, "0.0.0.0",&mreq.imr_address);
    mreq.imr_ifindex = if_nametoindex(server_conf.ifname);

    if(setsockopt(serversd, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq)) < 0)
    {
        syslog(LOG_ERR, "setsockopt(IP_MULTICAST_IF): %s",strerror(errno));
        exit(1);
    }
    sndaddr.sin_family = AF_INET;
    sndaddr.sin_port = htons(atoi(server_conf.rcvport));
    inet_pton(AF_INET, server_conf.mgruop, &sndaddr.sin_addr);
}
    //bind();

int main(int argc, char *argv[])
{
    int c;
    struct sigaction sa;
    int i;
    int list_size;
    int err;
    sa.sa_handler = daemon_exit;
    sigemptyset(&sa.sa_mask);

    // int fd;
    // fd = open("/home/chh/temp/debuglog.txt", O_RDWR | O_APPEND);
    // dup2(fd,STDERR_FILENO);
    // dup2(fd,STDIN_FILENO);
    // dup2(fd,STDOUT_FILENO);
    // close(fd);
    //sigaddset(&sa.sa_mask, SIGINT);
    //sigaddset(&sa.sa_mask, SIGQUIT);
    //sigaddset(&sa.sa_mask, SIGTERM);

    //sigaction(SIGTERM, &sa, NULL);
    //sigaction(SIGINT, &sa, NULL);
    //sigaction(SIGQUIT, &sa, NULL);
    for(i = 0; i <CHNNR + 1; i ++)
    {
        pthread_cond_init(channel_cond + i, NULL);
        pthread_cond_init(sock_cond + i, NULL);
    }
    openlog("netradio", LOG_PID | LOG_PERROR, LOG_DAEMON);
    /*命令行分析*/
    while(1)
    {
        c = getopt(argc, argv, "M:P:FD:I:H");
        if(c < 0) break;
        switch (c)
        {
        case 'M':
            server_conf.mgruop = optarg;
            break;
        case 'P':
            server_conf.rcvport = optarg;
            break;
        case 'F':
            server_conf.runmode = RUN_FOREGROUD;
            break;
        case 'D':
            server_conf.media_dir = optarg;
            break;
        case 'I':
            server_conf.ifname = optarg;
            break;
        case 'H':
            print_help();
            exit(1);
            break;
        default:
            abort();
            break;
        }

    }
    /*守护进程的实现*/
    if(server_conf.runmode == RUN_DAEMON)
    {
        if(daemonize() != 0)
        {
            exit(1);
        }
    }
    else if(server_conf.runmode == RUN_FOREGROUD)
    {
        //do nothing
    }
    else
    {
    //    fprintf(stderr, "EINVAL\n");
        syslog(LOG_ERR,"EINVAL server_conf.runmode");
        exit(1);
    }
    /*socket初始化*/
    socket_init();
    sdlocal_cntl_init();
    /*获取频道信息*/
    //struct mlib_listentry_st *list;
    err = mlib_getchnlist(&list, &list_size);
    if(err)
    {
        syslog(LOG_ERR, "mlib_getchnlist() : %s.", strerror(errno));
        exit(1);
    }
    syslog(LOG_DEBUG, "channel size = %d", list_size);
    /*创建节目单线程*/
    err = thr_list_create(list, list_size);
    if(err)
    {
        exit(1);
    }
    /*创建频道线程*/
    for(i = 0; i < list_size; i ++)
    {
        err = thr_channel_create(list + i);
        /*if error*/
        if(err)
        {
            syslog(LOG_ERR, "thr_channel_create():%s\n", strerror(errno));
            exit(1);
        }
    }

    syslog(LOG_DEBUG, "%d channel threads created.", i);
    //启动控制端口
    err = pthread_create(&tid_msgcntl, 0, msg_cntl_listen,NULL);
    if(err < 0)
    {
        printf("pthread_create(&tid_msgcntl, 0, msg_cntl_listen,NULL)");
        exit(1);
    }
    while(1)
        pause();
    /**/
    close(serversd);
    close(sdlocal_cntl);
    pthread_mutex_destroy(&sd_lock);
    pthread_mutex_destroy(&channel_lock);
    for(i = 0; i < CHNNR + 1; i ++)
    {
        pthread_cond_destroy(channel_cond + i);
        pthread_cond_destroy(sock_cond + i);
    }
    thr_list_destroy();
    thr_channel_destroyall();
    mlib_freechnlist(list);
    closelog();  
    exit(0);
}