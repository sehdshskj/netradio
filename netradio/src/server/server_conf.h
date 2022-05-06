#ifndef SERVER_CONF_H_
#define SERVER_CONF_H_

#define DEFAULT_MEDIADIR            "/home/chh/media"
#define DEFAULT_IF                  "eth0"
#include <unistd.h>
#include "thr_msgcntl.h"
enum
{
    RUN_DAEMON = 1,
    RUN_FOREGROUD = 2
};

struct server_conf_st
{
    char *rcvport;
    char *mgruop;
    char *media_dir;
    char runmode;
    char *ifname;
    char *cntlport;
};

extern struct server_conf_st server_conf;
extern int serversd;
extern struct sockaddr_in sndaddr;
extern int sdlocal_cntl;
extern struct sockaddr_in addrlocal, addrclient;
extern socklen_t addrlocal_len, addrclient_len;
#endif