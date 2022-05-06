#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include "mytbf.h"
#include <errno.h>
struct mytbf_st
{
    int cps;
    int burst;
    int token;
    int pos;
    pthread_mutex_t mut;
    pthread_cond_t cond;
};
static pthread_t tid;
static struct mytbf_st *job[CHNNR + 1];
static pthread_mutex_t mut_job = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t init_once = PTHREAD_ONCE_INIT;
/**
 *  
 * 
 * 一个可以控制流量的令牌桶
 * 
 */
static void *thr_alrm(void *p)//令牌定时增加
{
    int i;
    struct timespec t;
    while(1)
    {
        pthread_mutex_lock(&mut_job);
        //for(i = 0; i < MYTBF_MAX; i ++)
        //pthread_mutex_lock(&channel_lock);
        for(i = oldsendchannel; i < oldsendchannel + 1; i ++)
        {
            if(job[i] != NULL)
            {
                pthread_mutex_lock(&job[i]->mut);
                job[i]->token += job[i]->cps;
                if(job[i]->token > job[i]->burst)
                {
                    job[i]->token = job[i]->burst;
                }
                pthread_cond_signal(&job[i]->cond);
                pthread_mutex_unlock(&job[i]->mut);
                // perror("pthread_cond_signal(&job[i]->cond):");
                // printf("thr_channel[%d] thr_alrm\n", i);
            }
        }
        //pthread_mutex_unlock(&channel_lock);
        pthread_mutex_unlock(&mut_job);
        t.tv_sec = 1;
		t.tv_nsec = 0;
		while(nanosleep(&t, &t) != 0) 
        {
			if (errno != EINTR) 
            {
                fprintf(stderr, "nanosleep():%s\n", strerror(errno));
				exit(1);
			}
		}
    }
}
static void module_unload()//模块卸载
{
    int i;
    pthread_cancel(tid);
    pthread_join(tid, NULL);
    for(i = 0; i < CHNNR + 1; i ++)
    {
        free(job[i]);
    }
}
static void module_load(void)//模块初始化
{
    int err;
    pthread_t tid;
    err = pthread_create(&tid, NULL, thr_alrm, NULL);
    if(err)
    {
        fprintf(stderr, "pthread_create(): %s\n", strerror(errno));
        exit(1);
    }
    atexit(module_unload);
}

static int get_free_pos_unlocked()//寻找未使用的桶
{
    int i;
    for (i = 0; i < CHNNR + 1; i++)
    {
        if (job[i] == NULL)
        {
            return i;
        }
    }
    return -1;
}

mytbf_t *mytbf_init(chnid_t chnid, int cps, int burst)//令牌桶初始化
{

    pthread_once(&init_once, module_load);


    int pos;
    struct mytbf_st *me;
    me = (struct mytbf_st *)malloc(sizeof(struct mytbf_st));
    if (me == NULL)
        return NULL;
    me->cps = cps;
    me->burst = burst;
    me->token = cps;
    pthread_mutex_init(&me->mut, NULL);
    pthread_cond_init(&me->cond, NULL);

    pthread_mutex_lock(&mut_job);
    // pos = get_free_pos_unlocked();
    // if (pos < 0)
    // {
    //     pthread_mutex_unlock(&mut_job);
    //     free(me);
    //     return NULL;
    // }

    me->pos = chnid;
    job[me->pos] = me;
    pthread_mutex_unlock(&mut_job);

    return me;
}

int mytbf_fetchtoken(mytbf_t *ptr, int size)//申请size个令牌
{
    int n;
    struct mytbf_st *me = ptr;
    pthread_mutex_lock(&me->mut);
    while (me->token <= 0)
    {
        printf("mytbf_fetchtoken sleep\n");
        pthread_cond_wait(&me->cond, &me->mut);
    }
    n = me->token < size ? me->token : size; // min(me->token, size);
    me->token -= n;

    pthread_mutex_unlock(&me->mut);
    return n;
}

int mytbf_returntoken(mytbf_t *ptr, int size)//返还未使用完的令牌
{
    struct mytbf_st *me = ptr;

    pthread_mutex_lock(&me->mut);
    me->token += size;
    if (me->token > me->burst)
    {
        me->token = me->burst;
    }
    pthread_cond_broadcast(&me->cond);
    pthread_mutex_unlock(&me->mut);
    return 0;
}

int mytbf_destroy(mytbf_t *ptr)//令牌桶销毁
{
    struct mytbf_st *me = ptr;

    pthread_mutex_lock(&mut_job);
    job[me->pos] = NULL;
    pthread_mutex_unlock(&mut_job);
    pthread_mutex_destroy(&me->mut);
    pthread_cond_destroy(&me->cond);
    free(ptr);
    return 0;
}
