#ifndef CLIENT_H_
#define CLIENT_H_

#define DEFAULT_PLAYERCMD       "/usr/bin/mpg123 - > /dev/null "
//#define DEFAULT_PLAYERCMD       "/usr/bin/mpg123 /home/chh/temp/buf_mp3 > /dev/null "
struct client_conf_st
{
    char *rcvport;
    char *mgroup;
    char *player_cmd;
    char *cntlport;
};

extern struct client_conf_st client_conf;
#endif