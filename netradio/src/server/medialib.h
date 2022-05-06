#include "../include/proto.h"
#include <pthread.h>
#include <sys/wait.h>
#include "thr_msgcntl.h"
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>
#include "mytbf.h"
#ifndef MEDIALIB_H_
#define MEDIALIB_H_
#define DESC_FNAME      "/desc.txt"
#define MP3_BITRATE     (1024 * 350)
#define MP3_PARTERN     "/*.mp3"

struct mlib_listentry_st
{
    chnid_t chnid;
    char *desc;
};
struct channel_context_st
{
    chnid_t chnid;
    char *desc;
    glob_t mp3glob;
    int pos;
    int fd;
    off_t offset;
    mytbf_t *tbf;
};
extern pthread_mutex_t sd_lock;
extern struct channel_context_st channel[CHNNR + 1];
extern int open_next(chnid_t chnid);
int mlib_getchnlist(struct mlib_listentry_st **, int *);

int mlib_freechnlist(struct mlib_listentry_st *);

size_t mlib_readchn(chnid_t, void *, size_t);


#endif