#ifndef PROTO_H__
#define PROTO_H__

#include "site_type.h"

#define DEFAULT_MGROUP    "224.2.2.2"
#define DEFAULT_RCVPORT   "1989"
#define DEFAULT_CNTLPORT_LOCAL   "2001"
#define DEFAULT_CNTLPORT_SERVER  "2022"

#define CHNNR 		   100

#define LISTCHNID	   0

#define MINCHNID 	   1
#define MAXCHNID 	   (MINCHNID+CHNNR-1)

#define MSG_CHANNEL_MAX 	(65536-20-8)
#define MAX_DATA 			(MSG_CHANNEL_MAX - sizeof(chnid_t))

#define MSG_LIST_MAX		(65536-20-8)
#define MAX_ENTRY			(MSG_LIST_MAX-sizeof(chnid_t))

struct msg_channel_st		//content of a channel
{
	chnid_t chnid;			//must between MINCHNID and MAXCHNID
	uint8_t data[1];
}__attribute__((packed));

struct msg_listentry_st		//description of a  channel
{
		chnid_t chnid;
		uint16_t len;
		char desc[1];
		//uint8_t desc[1];
}__attribute__((packed));

struct msg_list_st			//information of every channel
{
	chnid_t chnid;			//must be LISTCHNID
	struct msg_listentry_st entry[1];
}__attribute__((packed));

struct msg_cntl_st
{
	uint16_t type;
	chnid_t  chnid;

}__attribute__((packed));
#endif
