#include <stdio.h>
#include <stdlib.h>
#include "medialib.h"
#include <glob.h>
#include "mytbf.h"
#include "server_conf.h"
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define PATHSIZE        1024
#define LINEBUFSIZE     1024


struct channel_context_st channel[MAXCHNID + 1];
static chnid_t curr_id = MINCHNID;

int open_next(chnid_t chnid)//打开某一频道下的下一首歌
{
    fprintf(stdout, "open_next()\n");
    while(1)
    {
        channel[chnid].pos ++;
        printf("open_next() : chnid : %d pos: %d pathc: %ld\n", chnid,channel[chnid].pos,channel[chnid].mp3glob.gl_pathc);
        //如果所有歌曲都没有打开
        if(channel[chnid].pos == channel[chnid].mp3glob.gl_pathc)
        {
            channel[chnid].pos = 0;//再来一次
        }
        close(channel[chnid].fd);
        channel[chnid].fd = open(channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], O_RDONLY);
        if(channel[chnid].fd < 0)
        {
            syslog(LOG_WARNING, "open(%s):%s",channel[chnid].mp3glob.gl_pathv[channel[chnid].pos], strerror(errno));
        }
        else
        {
            channel[chnid].offset = 0;
            return 0;
        }
    }
    syslog(LOG_ERR | LOG_DEBUG, "None of mp3 in channel %d is available, channel[chnid].mp3glob.gl_pathc:%ld", chnid, channel[chnid].mp3glob.gl_pathc);
    return -1;
}

static struct channel_context_st *path2entry(const char *path)//分析各个频道下的信息
{
    char pathstr[PATHSIZE] = {'\0'};//用来记录desc.txt的路径
    char linebuf[LINEBUFSIZE];
    FILE *fp;
    struct channel_context_st *me;
    strncpy(pathstr, path, PATHSIZE);
    strncat(pathstr, DESC_FNAME, PATHSIZE -1);

    fp = fopen(pathstr, "r");
    if(fp == NULL)
    {
        syslog(LOG_INFO, "%s is not a channel dir(can't find desc.txt)", path);
        return NULL;
    }

    if(fgets(linebuf, LINEBUFSIZE, fp) == NULL)
    {
        syslog(LOG_INFO, "%s is not a channel dir(can't get the desc.txt)", path);
        return NULL;
    }

    me = malloc(sizeof(*me));
    if(me == NULL)
    {
        syslog(LOG_ERR, "malloc() : %s", strerror(errno));
        return NULL;
    }

    me->tbf = mytbf_init(curr_id, MP3_BITRATE / 8 , MP3_BITRATE / 8 * 10 );
    if(me->tbf == NULL)
    {
        syslog(LOG_ERR, "mytbf_init(): %s", strerror(errno));
        free(me);
        return NULL;
    }

    me->desc = strdup(linebuf);
    strncpy(pathstr, path, PATHSIZE);
    strncat(pathstr, MP3_PARTERN, PATHSIZE - 1);

    if(glob(pathstr, 0, NULL, &me->mp3glob) != 0)
    {
        syslog(LOG_ERR, "%s is not a channel dir(can't find mp3 file)", path);
        mytbf_destroy(me->tbf);
        free(me);
        return NULL;
    }
    // //
    // for(int i = 0; i < me->mp3glob.gl_pathc; i ++)
    // {
    //     printf("%s:%d %s\n", path, i, me->mp3glob.gl_pathv[i]);
    // }
    me->pos = 0;
    me->offset = 0;
    me->fd = open(me->mp3glob.gl_pathv[me->pos], O_RDONLY);
    if(me->fd < 0)
    {
        syslog(LOG_WARNING, "%s open failed", me->mp3glob.gl_pathv[me->pos]);
        mytbf_destroy(me->tbf);
        free(me);
        return NULL;
    }
    me->chnid = curr_id;
    curr_id ++;
    return me;
}

int mlib_getchnlist(struct mlib_listentry_st **result, int * resnum)//初始化媒体库的信息
{
    int i;
    int num = 0;
    char path[PATHSIZE];
    glob_t globres;
    struct mlib_listentry_st *ptr;
    struct channel_context_st *res;


    for(i = 0; i < MAXCHNID + 1; i ++)
    {
        channel[i].chnid = -1;
    }
    snprintf(path, PATHSIZE, "%s/*", server_conf.media_dir);
    if(glob(path, 0, NULL, &globres) != 0)
    {
        return -1;
    }

    ptr = malloc(sizeof(struct mlib_listentry_st) * globres.gl_pathc);
    if(ptr == NULL)
    {
        syslog(LOG_ERR, "malloc failed.");
        exit(1);
    }

    for(i = 0; i < globres.gl_pathc; i ++)
    {        //globres.gl_pathv[i] -> /var/media/chi
        res = path2entry(globres.gl_pathv[i]);
        if(res != NULL)
        {
            syslog(LOG_DEBUG, "channel : %d desc : %s", res->chnid, res->desc);
            memcpy(channel + res->chnid, res, sizeof(*res));
            //printf("channel[%d]:%d %s\n", res->chnid, channel[res->chnid].mp3glob.gl_pathc, channel[res->chnid].mp3glob.gl_pathv[channel[res->chnid].mp3glob.gl_pathc -1]);
            ptr[num].chnid = res->chnid;
            ptr[num].desc = strdup(res->desc);
            num ++; 
        }
    }

    *result = realloc(ptr, sizeof(struct mlib_listentry_st) * num);
    if(result == NULL)
    {
        syslog(LOG_ERR, "realloc failed,");
    }
    *resnum = num;
    return 0;
}

int mlib_freechnlist(struct mlib_listentry_st *ptr)
{
    free(ptr);
    return 0;
}

size_t mlib_readchn(chnid_t chnid, void * buf, size_t size)//从某一频道读出流媒体数据
{
    //printf("thr_channel[%d] mlib_readchn_tbfsize: %d\n", chnid, 11);
    int tbfsize;
    int len;

    tbfsize = mytbf_fetchtoken(channel[chnid].tbf, size);
    //printf("thr_channel[%d] mlib_readchn_tbfsize: %d\n", chnid, tbfsize);
    while(1)
    {
        len = pread(channel[chnid].fd, buf, tbfsize, channel[chnid].offset);
        //当读到文件末尾时不算出错，pread()返回值0
        if(len <= 0)
        {
            //当这首歌有问题时，读取下一首歌
            fprintf(stderr, "读取这首歌歌失败\n");
            syslog(LOG_WARNING | LOG_DEBUG, "media  file %s pread() bytes: %d: %s", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos],len, strerror(errno));
            if((len = open_next(chnid)) < 0)
            {
                fprintf(stderr, "读取下一首歌失败\n");
                break;
            }
            syslog(LOG_WARNING | LOG_DEBUG, "media  file %s pread() bytes: %d: %s", channel[chnid].mp3glob.gl_pathv[channel[chnid].pos],len, strerror(errno));
        }
        else
        {
            channel[chnid].offset += len;
            break;
        }
        //printf("thr_channel[%d] mlib_readchn\n", len);
    }

    mytbf_returntoken(channel[chnid].tbf, tbfsize -len);
    return len;
}