#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include "server_conf.h"
#include "medialib.h"
#include "../include/proto.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>

//每个线程负责一个频道
struct thr_channel_ent_st
{
    chnid_t chnid;
    pthread_t tid;
};

struct thr_channel_ent_st thr_channel[CHNNR];

static int tid_nextpos = 0;

//对应频道的线程的处理函数
static void *thr_channel_snder(void *ptr)
{
    struct  msg_channel_st *sbufp;
    int len;
    ssize_t ret;
    struct mlib_listentry_st *ent = ptr;
    sbufp = malloc(MSG_CHANNEL_MAX);
    if(sbufp == NULL)
    {
        syslog(LOG_ERR, "malloc() : %s", strerror(errno));
        exit(1);
    }
    sbufp->chnid = ent->chnid;
    while(1)
    {   
        pthread_mutex_lock(&channel_lock);
        while(issend[sbufp->chnid] == 0)
        {
            printf("thr)channel[%d] sleep\n", sbufp->chnid);
            pthread_cond_wait(channel_cond +  sbufp->chnid, &channel_lock);
        }
        pthread_mutex_unlock(&channel_lock);
        //perror("pthread_mutex_unlock(&channel_lock):");
        //printf("thr_channel[%d] wakeup\n", sbufp->chnid);
        len = mlib_readchn(sbufp->chnid, sbufp->data, MAX_DATA);
        pthread_mutex_lock(&sd_lock);     
        while(sock_send == 0)
        {
            printf("channel[%d]socke locked\n", sbufp->chnid);
            pthread_cond_wait(sock_cond +  sbufp->chnid, &sd_lock);
            //len = 0;
        }
        // if(len == 0)
        // {
        //     continue;
        // }
        if(len < 0)//如果所有内容已经读完
        {
            break;
        }
        if((ret = sendto(serversd, sbufp, len + sizeof(chnid_t), 0, (void *)&sndaddr, sizeof(sndaddr))) < 0)
        {
            pthread_mutex_unlock(&sd_lock);
            syslog(LOG_ERR, "thr_channel(%d): sendto():%s", sbufp->chnid, strerror(errno));
            break;
        }
        else
        {
            pthread_mutex_unlock(&sd_lock);
            syslog(LOG_DEBUG, "thr_channel(%d) sendto() to succeed %ld", sbufp->chnid, ret);
        }
        //让出调度器
        //sched_yield();
    }
    syslog(LOG_DEBUG, "thr_channel(%d) has no more content and exit", sbufp->chnid);
    pthread_exit(NULL);
}

int thr_channel_create(struct mlib_listentry_st *ptr)//创建每个频道的发送线程
{
    int err;
    if(tid_nextpos >= CHNNR)
    {
            return -ENOSPC;
    }
    err = pthread_create(&thr_channel[tid_nextpos].tid, NULL, thr_channel_snder, ptr);
    if(err)
    {
        syslog(LOG_WARNING, "pthread_create(): %s", strerror(errno));
        return -err;
    }
    thr_channel[tid_nextpos].chnid = ptr->chnid;
    tid_nextpos ++;
    return 0;
}   
int thr_channel_destroy(struct mlib_listentry_st *ptr)//销毁
{
    int i;
    for(i = 0; i < CHNNR; i ++)
    {
        if(thr_channel[i].chnid == ptr->chnid)
        {
            if(pthread_cancel(thr_channel[i].tid) < 0)
            {
                syslog(LOG_ERR, "pthread_cancel():thread of channel %d ", ptr->chnid);
                return -ESRCH;
            }
            pthread_join(thr_channel[i].tid, NULL);
            thr_channel[i].chnid = -1;
            return 0;
        }
    }
    syslog(LOG_ERR,  "channel %d dosen't exist", ptr->chnid);
    return -ESRCH;

}
int thr_channel_destroyall(void)
{
    int i;
    for(i = 0; i < CHNNR; i++)
    {
        if(thr_channel[i].chnid > 0)
        {
            if(thr_channel[i].tid < 0)
            {
                syslog(LOG_ERR, "pthread_cancel():the thread of channel : %d", thr_channel[i].chnid);
                return -ESRCH;
            }
            pthread_join(thr_channel[i].tid, NULL);
            thr_channel[i].chnid = -1;
        }
    }
    return 0;
}
