#ifndef _THR_MSGCNTL_H_
#define _THR_MSGCNTL_H_
#include "../include/proto.h"
#include "server_conf.h"
#include "medialib.h"
#include "thr_list.h"
#include "thr_channel.h"
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
#include <time.h>
#include <pthread.h>
#include <linux/input.h>
void sdlocal_cntl_init();
void *msg_cntl_listen(void *ptr);
void msg_cntl_handle(struct msg_cntl_st msg_cntl);
extern pthread_mutex_t channel_lock;
extern pthread_cond_t  channel_cond[CHNNR + 1],sock_cond[CHNNR + 1];
extern int issend[CHNNR + 1];//用来决定是否发送
extern  int oldsendchannel;
extern int sock_send;
#endif