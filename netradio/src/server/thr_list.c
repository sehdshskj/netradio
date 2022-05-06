#include <stdio.h>
#include <stdlib.h>
#include "thr_list.h"
#include <pthread.h>
#include <errno.h>
#include <syslog.h>
#include <string.h>
#include "../include/proto.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include "server_conf.h"
#include "medialib.h"

static pthread_t tid_list;

//节目单包含的节目数量
static int nr_list_ent;
//节目单信息数组，每一条存储一个节目频道信息
static struct  mlib_listentry_st *list_ent;

static void *thr_list(void *p)//发送节目单线程
{
    int i;
    int totalsize;
    struct msg_list_st *entlistp;
    struct msg_listentry_st *entryp;
    int size;
    int ret;
    int len;

    totalsize = sizeof(chnid_t);
    for(i = 0; i < nr_list_ent; i ++)
    {
        totalsize += sizeof(struct msg_listentry_st) + strlen(list_ent[i].desc);
    }

    entlistp = malloc(totalsize);
    if(entlistp == NULL)
    {
        syslog(LOG_ERR, "malloc():%s.", strerror(errno));
        exit(1);
    }
    entlistp->chnid = LISTCHNID;
    entryp = entlistp->entry;

    for(i = 0; i < nr_list_ent; i ++)
    {
        size = sizeof(struct msg_listentry_st) + strlen(list_ent[i].desc);
        entryp->chnid = list_ent[i].chnid;
        entryp->len = htons(size);
        strcpy(entryp->desc, list_ent[i].desc);
        entryp = (void *)(((char *)entryp) + size);
    }
    //在0号频道发送节目单
    while(1)
    {
        pthread_mutex_lock(&channel_lock);
        while(issend[0] == 0)
        {
            pthread_cond_wait(channel_cond + 0 , &channel_lock);
        }
        pthread_mutex_unlock(&channel_lock);
        sendto(serversd, &nr_list_ent, sizeof(nr_list_ent), 0, (void*)&sndaddr, sizeof(sndaddr));
        len = totalsize;
        ret = 0;
        while(len > 0)
        {
            pthread_mutex_lock(&sd_lock);
            ret = sendto(serversd, entlistp, len, 0, (void*)&sndaddr, sizeof(sndaddr));
            pthread_mutex_unlock(&sd_lock);
            syslog(LOG_DEBUG, "send content len :%d\n", entlistp->entry->len);
            if(ret < 0)
            {
                syslog(LOG_WARNING, "sendto():%s", strerror(errno));
            }
            else
            {
                syslog(LOG_DEBUG, "send to program list succeed.%d", ret);
            }
            len = len - ret;
            sleep(1);
        }
    }
    pthread_exit(NULL);
}



int thr_list_create(struct mlib_listentry_st *listp, int nr_ent)//节目单线程的创建
{
    int err;
    list_ent = listp;
    nr_list_ent = nr_ent;
    err = pthread_create(&tid_list, NULL, thr_list, NULL);
    if(err)
    {
        syslog(LOG_ERR, "pthread_create():%s ", strerror(errno));
        return -1;
    }
    return 0;

}
int thr_list_destroy(void)
{
    pthread_cancel(tid_list);
    pthread_join(tid_list, NULL);
    return 0;
}