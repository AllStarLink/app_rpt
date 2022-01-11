/* #define	OLD_ASTERISK */
/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Copyright (C) 2008, Scott Lawson/KI4LKF
 * ScottLawson/KI4LKF <ham44865@yahoo.com>
 * Jim Dixon, WB6NIL <jim@lambdatel.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Echolink channel driver for Asterisk
 * 
 * \author Scott Lawson/KI4LKF <ham44865@yahoo.com>
 * \author Jim Dixon, WB6NIL <jim@lambdatel.com>
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<depend>zlib</depend>
 ***/

/*

Echolink channel driver for Asterisk/app_rpt.

I wish to thank the following people for the immeasurable amount of
very high-quality assistance they have provided me, without which this
project would have been impossible:

Scott, KI4LKF
Skip, WB6YMH
Randy, KC6HUR
Steve, N4IRS

A lot more has to be added,
Here is what comes to mind first:

---> does not send its station info.
---> does not process chat text.
---> recognizes a few remote text commands.
---> no busy, deaf or mute.
---> no capacity limits.
---> no banned or privare station list.
---> no admin list, only local 127.0.0.1 access.
---> no welcome text message.
---> no login or connect timeouts.
---> no max TX time limit.
---> no activity reporting.
---> no event notififications.
---> no stats.
---> no callsign prefix restrictions.
---> no announcements on connects/disconnects.
---> no loop detection.
---> allows "doubles"

Default ports are 5198,5199.

Remote text commands thru netcat:
o.conip <IPaddress>    (request a connect)
o.dconip <IPaddress>   (request a disconnect)
o.rec                  (turn on/off recording)

It is invoked as echolink/identifier (like el0 for example)
Example: 
Under a node stanza in rpt.conf, 
rxchannel=echolink/el0

The el0 or whatever you put there must match the stanza in the
echolink.conf file.

If the linux box is protected by a NAT router,
leave the IP address as 0.0.0.0,
do not use 127.0.0.1

*/

#include "asterisk.h"

/*
 * Please change this revision number when you make a edit
 * use the simple format YYMMDD
*/

ASTERISK_FILE_VERSION(__FILE__,"$Revision$")
// ASTERISK_FILE_VERSION(__FILE__,"$Revision$")

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <search.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <zlib.h>
#include <pthread.h>
#include <signal.h>
#include <fnmatch.h>
#include <math.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/translate.h"
#include "asterisk/astdb.h"
#include "asterisk/cli.h"

#ifdef	OLD_ASTERISK
#define	AST_MODULE_LOAD_DECLINE -1
#endif

#define	MAX_RXKEY_TIME 4

/* 50 * 10 * 20ms iax2 = 10,000ms = 10 seconds heartbeat */
#define	KEEPALIVE_TIME 50 * 10
#define	AUTH_RETRY_MS 5000
#define	AUTH_ABANDONED_MS 15000
#define	BLOCKING_FACTOR 4
#define	GSM_FRAME_SIZE 33
#define QUEUE_OVERLOAD_THRESHOLD_AST 75
#define QUEUE_OVERLOAD_THRESHOLD_EL 30
#define	MAXPENDING 20

#define EL_IP_SIZE 16
#define EL_CALL_SIZE 16
#define EL_NAME_SIZE 32
#define EL_APRS_SIZE 200
#define EL_PWD_SIZE 16
#define EL_EMAIL_SIZE 32
#define EL_QTH_SIZE 32
#define EL_MAX_SERVERS 3
#define EL_SERVERNAME_SIZE 63
#define	EL_MAX_INSTANCES 100
#define	EL_MAX_CALL_LIST 60
#define	EL_APRS_SERVER "aprs.echolink.org"
#define	EL_APRS_INTERVAL 600
#define	EL_APRS_START_DELAY 10
#define	GPSFILE "/tmp/gps.dat"
#define	GPS_VALID_SECS 60


#define	ELDB_NODENUMLEN 8
#define	ELDB_CALLSIGNLEN 20
#define	ELDB_IPADDRLEN 18

#define	DELIMCHR ','
#define	QUOTECHR 34

/* 
   If you want to compile/link this code
   on "BIG-ENDIAN" platforms, then
   use this: #define RTP_BIG_ENDIAN
   Have only tested this code on "little-endian"
   platforms running Linux.
*/

static const char tdesc[] = "Echolink channel driver";
static int prefformat = AST_FORMAT_GSM;
static char type[] = "echolink";
static char snapshot_id[50] = {'0',0};
static int el_net_get_index = 0;
static int el_net_get_nread = 0;
static int nodeoutfd = -1;

struct sockaddr_in sin_aprs;

/* Echolink audio packet heafer */
struct gsmVoice_t {
#ifdef RTP_BIG_ENDIAN
  uint8_t version:2;
  uint8_t pad:1;
  uint8_t ext:1;
  uint8_t csrc:4;
  uint8_t marker:1;
  uint8_t payt:7;
#else
  uint8_t csrc:4;
  uint8_t ext:1;
  uint8_t pad:1;
  uint8_t version:2;
  uint8_t payt:7;
  uint8_t marker:1;
#endif
  uint16_t seqnum;
  uint32_t time;
  uint32_t ssrc;
  unsigned char data[BLOCKING_FACTOR * GSM_FRAME_SIZE];
};

struct el_instance;
struct el_pvt;

/* Echolink node details */
/* Also each node in binary tree in memory */
struct el_node {
   char ip[EL_IP_SIZE + 1]; 
   char call[EL_CALL_SIZE + 1];
   char name[EL_NAME_SIZE + 1];
   unsigned int nodenum; /* not used yet */
   short countdown;
   uint16_t seqnum;
   struct el_instance *instp;
   struct el_pvt *p;
   struct ast_channel *chan;
   char outbound;
};

struct el_pending {
	char fromip[EL_IP_SIZE + 1];
	struct timeval reqtime;
} ;		

struct el_instance
{
	ast_mutex_t lock;
	char name[EL_NAME_SIZE + 1];
	char mycall[EL_CALL_SIZE + 1];
	char myname[EL_NAME_SIZE + 1];
	char mypwd[EL_PWD_SIZE + 1];
	char myemail[EL_EMAIL_SIZE + 1];
	char myqth[EL_QTH_SIZE + 1];
	char elservers[EL_MAX_SERVERS][EL_SERVERNAME_SIZE + 1];
	char ipaddr[EL_IP_SIZE + 1];
	char port[EL_IP_SIZE + 1];
	char astnode[EL_NAME_SIZE + 1];
	char context[EL_NAME_SIZE + 1];
	float lat;
	float lon;
	float freq;
	float tone;
	char power;
	char height;
	char gain;
	char dir;
	int maxstns;
	char *denylist[EL_MAX_CALL_LIST];
	int ndenylist;
	char *permitlist[EL_MAX_CALL_LIST];
	int npermitlist;
	/* missed 10 heartbeats, you're out */
	short rtcptimeout;
	unsigned int mynode;
	char fdr_file[FILENAME_MAX];
	int audio_sock;
	int ctrl_sock;
	uint16_t audio_port;
	uint16_t ctrl_port;
	int fdr;
	unsigned long seqno;
	int useless_flag_1;
	struct el_pvt *confp;
	struct gsmVoice_t audio_all_but_one;
	struct gsmVoice_t audio_all;
	struct el_node el_node_test;
	struct el_pending pending[MAXPENDING];
	time_t aprstime;
	time_t starttime;
	char lastcall[EL_CALL_SIZE + 1];
	time_t lasttime;
	char login_display[EL_NAME_SIZE + EL_CALL_SIZE + 1];
	char aprs_display[EL_APRS_SIZE + 1];
	pthread_t el_reader_thread;
} ;

struct el_rxqast {
	struct el_rxqast *qe_forw;
	struct el_rxqast *qe_back;
	char buf[GSM_FRAME_SIZE];
};

struct el_rxqel {
        struct el_rxqel *qe_forw;
        struct el_rxqel *qe_back;
        char buf[BLOCKING_FACTOR * GSM_FRAME_SIZE];
        char fromip[EL_IP_SIZE + 1];
};

struct el_pvt {
	struct ast_channel *owner;
	struct el_instance *instp;
	char app[16];		
	char stream[80];
	char ip[EL_IP_SIZE + 1]; 
	char txkey;
	int rxkey;
	int keepalive;
	struct ast_frame fr;	
	int txindex;
	struct el_rxqast rxqast;
        struct el_rxqel rxqel;
	char firstsent;
	char firstheard;
	struct ast_dsp *dsp;
	struct ast_module_user *u;
	struct ast_trans_pvt *xpath;
	unsigned int nodenum;
	char *linkstr;
};

struct rtcp_sdes_request_item {
    unsigned char r_item;
    char *r_text;
};

struct rtcp_sdes_request {
   int nitems;
   unsigned char ssrc[4];
   struct rtcp_sdes_request_item item[10];
};

struct rtcp_common_t {
#ifdef RTP_BIG_ENDIAN
  uint8_t version:2;
  uint8_t p:1;
  uint8_t count:5;
#else
  uint8_t count:5;
  uint8_t p:1;
  uint8_t version:2;
#endif
  uint8_t pt:8;
  uint16_t length;
};

struct rtcp_sdes_item_t {
  uint8_t type;
  uint8_t length;
  char data[1];
};

struct rtcp_t {
  struct rtcp_common_t common;
  union {
    struct {
      uint32_t src[1];
    } bye;

    struct rtcp_sdes_t {
      uint32_t src;
      struct rtcp_sdes_item_t item[1];
    } sdes;
  } r;
};

struct eldb {
	char nodenum[ELDB_NODENUMLEN];
	char callsign[ELDB_CALLSIGNLEN];
	char ipaddr[ELDB_IPADDRLEN];
} ;

AST_MUTEX_DEFINE_STATIC(el_db_lock);
AST_MUTEX_DEFINE_STATIC(el_count_lock);

#ifdef	OLD_ASTERISK
static int usecnt;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);
#endif

int debug = 0;
struct el_instance *instances[EL_MAX_INSTANCES];
int ninstances = 0;

int count_n = 0;
int count_outbound_n = 0;
struct el_instance *count_instp;

/* binary search tree in memory, root node */
static void *el_node_list = NULL;
static void *el_db_callsign = NULL;
static void *el_db_nodenum = NULL;
static void *el_db_ipaddr = NULL;

/* Echolink registration thread */
static  pthread_t el_register_thread = 0;
static  pthread_t el_directory_thread = 0;
static int run_forever = 1;
static int killing = 0;
static int nullfd = -1;
static int el_sleeptime = 0;
static int el_login_sleeptime = 0;

static char *config = "echolink.conf";

#ifdef OLD_ASTERISK
#define ast_free free
#define ast_malloc malloc
#endif

static struct ast_channel *el_request(const char *type, int format, void *data, int *cause);
static int el_call(struct ast_channel *ast, char *dest, int timeout);
static int el_hangup(struct ast_channel *ast);
static struct ast_frame *el_xread(struct ast_channel *ast);
static int el_xwrite(struct ast_channel *ast, struct ast_frame *frame);
#ifdef	OLD_ASTERISK
static int el_indicate(struct ast_channel *ast, int cond);
static int el_digit_end(struct ast_channel *c, char digit);
#else
static int el_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen);
static int el_digit_begin(struct ast_channel *c, char digit);
static int el_digit_end(struct ast_channel *c, char digit, unsigned int duratiion);
#endif
static int el_text(struct ast_channel *c, const char *text);

static int rtcp_make_sdes(unsigned char *pkt, int pktLen, char *call, char *name, char *astnode);
static int rtcp_make_el_sdes(unsigned char *pkt, int pktLen, char *cname, char *loc);
static int rtcp_make_bye(unsigned char *p, char *reason);
static void parse_sdes(unsigned char *packet, struct rtcp_sdes_request *r);
static void copy_sdes_item(char *source, char *dest, int destlen);
static int is_rtcp_bye(unsigned char *p, int len);
static int is_rtcp_sdes(unsigned char *p, int len);
 /* remove binary tree functions if Asterisk has similar functionality */
static int compare_ip(const void *pa, const void *pb);
static void send_audio_all_but_one(const void *nodep, const VISIT which, const int depth);
static void send_audio_all(const void *nodep, const VISIT which, const int depth);
static void send_heartbeat(const void *nodep, const VISIT which, const int depth);
static void send_info(const void *nodep, const VISIT which, const int depth);
static void print_users(const void *nodep, const VISIT which, const int depth);
static void count_users(const void *nodep, const VISIT which, const int depth);
static void free_node(void *nodep);
static void process_cmd(char *buf,char *fromip,struct el_instance *instp);
static int find_delete(struct el_node *key);
static int sendcmd(char *server,struct el_instance *instp);
static int do_new_call(struct el_instance *instp, struct el_pvt *p, char *call, char *name);

/* remove writen if Asterisk has similar functionality */
static int writen(int fd, char *ptr, int nbytes);

static const struct ast_channel_tech el_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = AST_FORMAT_GSM,
	.requester = el_request,
	.call = el_call,
	.hangup = el_hangup,
	.read = el_xread,
	.write = el_xwrite,
	.indicate = el_indicate,
	.send_text = el_text,
#ifdef	OLD_ASTERISK
	.send_digit = el_digit_end,
#else
	.send_digit_begin = el_digit_begin,
	.send_digit_end = el_digit_end,
#endif
};

/*
* CLI extensions
*/

/* Debug mode */
static int el_do_debug(int fd, int argc, char *argv[]);
static int el_do_dbdump(int fd, int argc, char *argv[]);
static int el_do_dbget(int fd, int argc, char *argv[]);

static char debug_usage[] =
"Usage: echolink debug level {0-7}\n"
"       Enables debug messages in app_rpt\n";

static char dbdump_usage[] =
"Usage: echolink dbdump [nodename|callsign|ipaddr]\n"
"       Dumps entire echolink db\n";

static char dbget_usage[] =
"Usage: echolink dbget <nodename|callsign|ipaddr> <lookup-data>\n"
"       Looks up echolink db entry\n";

#ifndef	NEW_ASTERISK

static struct ast_cli_entry  cli_debug =
        { { "echolink", "debug", "level" }, el_do_debug, 
		"Enable echolink debugging", debug_usage };
static struct ast_cli_entry  cli_dbdump =
        { { "echolink", "dbdump" }, el_do_dbdump,
		"Dump entire echolink db", dbdump_usage };

static struct ast_cli_entry  cli_dbget =
        { { "echolink", "dbget" }, el_do_dbget,
		"Look up echolink db entry", dbget_usage };

#endif

static void mythread_exit(void *nothing)
{
int	i;

	if (killing) pthread_exit(NULL);
	killing = 1;
	run_forever = 0;
	for(i = 0; i < ninstances; i++)
	{
		if (instances[i]->el_reader_thread) 
			pthread_kill(instances[i]->el_reader_thread,SIGTERM);
	}
	if (el_register_thread) pthread_kill(el_register_thread,SIGTERM);
	if (el_directory_thread) pthread_kill(el_directory_thread,SIGTERM);
	ast_log(LOG_ERROR,"Exiting chan_echolink, FATAL ERROR!!\n");
	ast_cli_command(nullfd,"rpt restart");
	pthread_exit(NULL);
}

/*
* Break up a delimited string into a table of substrings
*
* str - delimited string ( will be modified )
* strp- list of pointers to substrings (this is built by this function), NULL will be placed at end of list
* limit- maximum number of substrings to process
*/
	


static int finddelim(char *str, char *strp[], int limit)
{
int     i,l,inquo;

        inquo = 0;
        i = 0;
        strp[i++] = str;
        if (!*str)
           {
                strp[0] = 0;
                return(0);
           }
        for(l = 0; *str && (l < limit) ; str++)
           {
                if (*str == QUOTECHR)
                   {
                        if (inquo)
                           {
                                *str = 0;
                                inquo = 0;
                           }
                        else
                           {
                                strp[i - 1] = str + 1;
                                inquo = 1;
                           }
		}
                if ((*str == DELIMCHR) && (!inquo))
                {
                        *str = 0;
			l++;
                        strp[i++] = str + 1;
                }
           }
        strp[i] = 0;
        return(i);

}

static void print_nodes(const void *nodep, const VISIT which, const int depth)
{
   if ((which == leaf) || (which == postorder)) {
      ast_cli(nodeoutfd,"%s|%s|%s\n",
             (*(struct eldb **)nodep)->nodenum,
             (*(struct eldb **)nodep)->callsign,
             (*(struct eldb **)nodep)->ipaddr);
   }
}

static int compare_eldb_nodenum(const void *pa, const void *pb)
{
   return strcmp(((struct eldb *)pa)->nodenum,((struct eldb *)pb)->nodenum);
}

static int compare_eldb_ipaddr(const void *pa, const void *pb)
{
   return strcmp(((struct eldb *)pa)->ipaddr,((struct eldb *)pb)->ipaddr);
}

static int compare_eldb_callsign(const void *pa, const void *pb)
{
   return strcmp(((struct eldb *)pa)->callsign,((struct eldb *)pb)->callsign);
}

static struct eldb *el_db_find_nodenum(char *nodenum)
{
struct eldb **found_key = NULL,key;

	memset(&key,0,sizeof(key));
	strncpy(key.nodenum,strndup(nodenum,ELDB_NODENUMLEN),sizeof(key.nodenum) - 1);
	found_key = (struct eldb **)tfind(&key,&el_db_nodenum,compare_eldb_nodenum);
	if (found_key) return(*found_key);
	return NULL;
}

static struct eldb *el_db_find_callsign(char *callsign)
{
struct eldb **found_key = NULL,key;

	memset(&key,0,sizeof(key));
	strncpy(key.callsign,strndup(callsign,ELDB_CALLSIGNLEN),sizeof(key.callsign) - 1);
	found_key = (struct eldb **)tfind(&key,&el_db_callsign,compare_eldb_callsign);
	if (found_key) return(*found_key);
	return NULL;
}

static struct eldb *el_db_find_ipaddr(char *ipaddr)
{
struct eldb **found_key = NULL,key;

	memset(&key,0,sizeof(key));
	strncpy(key.ipaddr,strndup(ipaddr,ELDB_IPADDRLEN),sizeof(key.ipaddr) - 1);
	found_key = (struct eldb **)tfind(&key,&el_db_ipaddr,compare_eldb_ipaddr);
	if (found_key) return(*found_key);
	return NULL;
}

static void el_db_delete_indexes(struct eldb *node)
{
struct eldb *mynode;

	if (!node) return;
	mynode = el_db_find_nodenum(node->nodenum);
	if (mynode) tdelete(mynode, &el_db_nodenum, compare_eldb_nodenum);
	mynode = el_db_find_ipaddr(node->ipaddr);
	if (mynode) tdelete(mynode, &el_db_ipaddr, compare_eldb_ipaddr);
	mynode = el_db_find_callsign(node->callsign);
	if (mynode) tdelete(mynode, &el_db_callsign, compare_eldb_callsign);
	return;
}

static void el_db_delete(struct eldb *node)
{
	if (!node) return;
	el_db_delete_indexes(node);
	ast_free(node);
	return;
}

static struct eldb *el_db_put(char *nodenum,char *ipaddr, char *callsign)
{
struct eldb *node,*mynode;

	node = (struct eldb *)ast_malloc(sizeof(struct eldb));
	if (!node)
	{
		ast_log(LOG_NOTICE,"Caannot malloc!!\n");
		return NULL;
	}
	memset(node,0,sizeof(struct eldb));
	strncpy(node->nodenum,strndup(nodenum, ELDB_NODENUMLEN),ELDB_NODENUMLEN - 1);
	strncpy(node->ipaddr,strndup(ipaddr, ELDB_IPADDRLEN),ELDB_IPADDRLEN - 1);
	strncpy(node->callsign,strndup(callsign, ELDB_CALLSIGNLEN),ELDB_CALLSIGNLEN - 1);
	mynode = el_db_find_nodenum(node->nodenum);
	if (mynode) el_db_delete(mynode);
	mynode = el_db_find_ipaddr(node->ipaddr);
	if (mynode) el_db_delete(mynode);
	mynode = el_db_find_callsign(node->callsign);
	if (mynode) el_db_delete(mynode);
	tsearch(node,&el_db_nodenum,compare_eldb_nodenum);
	tsearch(node,&el_db_ipaddr,compare_eldb_ipaddr);
	tsearch(node,&el_db_callsign,compare_eldb_callsign);
	if (debug > 1)
		ast_log(LOG_DEBUG,"eldb put: Node=%s, Call=%s, IP=%s\n",nodenum,callsign,ipaddr);
	return(node);
}


static int rtcp_make_sdes(unsigned char *pkt, int pktLen, char *call, char *name, char *astnode)
{
    unsigned char zp[1500];
    unsigned char *p = zp;
    struct rtcp_t *rp;
    unsigned char *ap;
    char line[EL_CALL_SIZE + EL_NAME_SIZE + 1];
    int l, hl, pl;

    hl = 0;
    *p++ = 3 << 6;
    *p++ = 201;
    *p++ = 0;
    *p++ = 1;
    *((long *) p) = htonl(0);
    p += 4;
    hl = 8;

    rp = (struct rtcp_t *) p;
    *((short *) p) = htons((3 << 14) | 202 | (1 << 8));
    rp->r.sdes.src = htonl(0);
    ap = (unsigned char *) rp->r.sdes.item;

    strncpy(line,"CALLSIGN",EL_CALL_SIZE + EL_NAME_SIZE);
    *ap++ = 1;
    *ap++ = l = strlen(line);
    memcpy(ap,line,l);
    ap += l;

    snprintf(line,EL_CALL_SIZE + EL_NAME_SIZE,"%s %s",call,name);
    *ap++ = 2;
    *ap++ = l = strlen(line);
    memcpy(ap,line,l); 
    ap += l;

    if (astnode)
    {
	    snprintf(line,EL_CALL_SIZE + EL_NAME_SIZE,"Allstar %s",astnode);
	    *ap++ = 6;
	    *ap++ = l = strlen(line);
	    memcpy(ap,line,l); 
	    ap += l;
    }
    /* enable DTMF keypad */
    *ap++ = 8;
    *ap++ = 3;
    *ap++ = 1;
    *ap++ = 'D';
    *ap++ = '1';

    *ap++ = 0;
    *ap++ = 0;
    l = ap - p;

    rp->common.length = htons(((l + 3) / 4) - 1);
    l = hl + ((ntohs(rp->common.length) + 1) * 4);

    pl = (l & 4) ? l : l + 4;

    if (pl > l) {
       int pad = pl - l;
       memset(zp + l, '\0', pad);  
       zp[pl - 1] = pad;
       p[0] |= 0x20;
       rp->common.length = htons(ntohs(rp->common.length) + ((pad) / 4));
       l = pl;
    }

    if (l > pktLen)
       return 0;
    memcpy(pkt,zp,l);
    return l;
}

static int rtcp_make_el_sdes(unsigned char *pkt, int pktLen, char *cname, char *loc)
{
    unsigned char zp[1500];
    unsigned char *p = zp;
    struct rtcp_t *rp;
    unsigned char *ap;
    int l, hl, pl;

    hl = 0;
    *p++ = 2 << 6;
    *p++ = 201;
    *p++ = 0;
    *p++ = 1;
    *((long *) p) = htonl(0);
    p += 4;
    hl = 8;

    rp = (struct rtcp_t *) p;
    *((short *) p) = htons((2 << 14) | 202 | (1 << 8));
    rp->r.sdes.src = htonl(0);
    ap = (unsigned char *) rp->r.sdes.item;

    *ap++ = 1;
    *ap++ = l = strlen(cname);
    memcpy(ap,cname,l);
    ap += l;

    *ap++ = 5;
    *ap++ = l = strlen(loc);
    memcpy(ap,loc,l); 
    ap += l;

    *ap++ = 0;
    *ap++ = 0;
    l = ap - p;

    rp->common.length = htons(((l + 3) / 4) - 1);
    l = hl + ((ntohs(rp->common.length) + 1) * 4);

    pl = (l & 4) ? l : l + 4;

    if (pl > l) {
       int pad = pl - l;
       memset(zp + l, '\0', pad);  
       zp[pl - 1] = pad;
       p[0] |= 0x20;
       rp->common.length = htons(ntohs(rp->common.length) + ((pad) / 4));
       l = pl;
    }

    if (l > pktLen)
       return 0;
    memcpy(pkt,zp,l);
    return l;
}

static int rtcp_make_bye(unsigned char *p, char *reason)
{
    struct rtcp_t *rp;
    unsigned char *ap, *zp;
    int l, hl, pl;

    zp = p;
    hl = 0;

    *p++ = 3 << 6;
    *p++ = 201;
    *p++ = 0;
    *p++ = 1;
    *((long *) p) = htonl(0);
    p += 4;
    hl = 8;

    rp = (struct rtcp_t *)p;
    *((short *) p) = htons((3 << 14) | 203 | (1 << 8));
    rp->r.bye.src[0] = htonl(0);
    ap = (unsigned char *) rp->r.sdes.item;
    l = 0; 
    if (reason != NULL) {
        l = strlen(reason);
        if (l > 0) {
            *ap++ = l;
             memcpy(ap,reason,l);
            ap += l;
        }
    }
    while ((ap - p) & 3)
        *ap++ = 0;
    l = ap - p;
    rp->common.length = htons((l / 4) - 1);
    l = hl + ((ntohs(rp->common.length) + 1) * 4);

    pl = (l & 4) ? l : l + 4;
    if (pl > l) {
       int pad = pl - l;
       memset(zp + l, '\0', pad);
       zp[pl - 1] = pad;
       p[0] |= 0x20;
       rp->common.length = htons(ntohs(rp->common.length) + ((pad) / 4));
       l = pl;    
    }
    return l;
}

static void parse_sdes(unsigned char *packet, struct rtcp_sdes_request *r)
{
    int i;
    unsigned char *p = packet;

    for (i = 0; i < r->nitems; i++)
        r->item[i].r_text = NULL;

    while ((p[0] >> 6 & 3) == 3 || (p[0] >> 6 & 3) == 1) {
        if ((p[1] == 202) && ((p[0] & 0x1F) > 0)) {
            unsigned char *cp = p + 8,
                          *lp = cp + (ntohs(*((short *) (p + 2))) + 1) * 4;
            memcpy(r->ssrc, p + 4, 4); 
            while (cp < lp) {
                unsigned char itype = *cp;
                if (itype == 0)
                    break;

                for (i = 0; i < r->nitems; i++) {
                    if (r->item[i].r_item == itype &&
                        r->item[i].r_text == NULL) {
                        r->item[i].r_text = (char *) cp;
                        break;
                    }
                }
                cp += cp[1] + 2;
            }
            break;
        }
        p += (ntohs(*((short *) (p + 2))) + 1) * 4;
    }
    return;
}

static void copy_sdes_item(char *source, char *dest, int destlen)
{
    int len = source[1] & 0xFF;
    if (len > destlen)
       len = destlen;
    memcpy(dest,source + 2, len);
    dest[len] = 0;
    return;
}

static int is_rtcp_bye(unsigned char *p, int len)
{
    unsigned char *end;
    int sawbye = 0;


    if ((((p[0] >> 6) & 3) != 3 && ((p[0] >> 6) & 3) != 1) ||
        ((p[0] & 0x20) != 0) ||
        ((p[1] != 200) && (p[1] != 201)))
      return 0;

    end = p + len;

    do {
        if (p[1] == 203)
            sawbye = 1;

        p += (ntohs(*((short *) (p + 2))) + 1) * 4;
    } while (p < end && (((p[0] >> 6) & 3) == 3));

    return sawbye;
}

static int is_rtcp_sdes(unsigned char *p, int len)
{
    unsigned char *end;
    int sawsdes = 0;

    if ((((p[0] >> 6) & 3) != 3 && ((p[0] >> 6) & 3) != 1) ||
        ((p[0] & 0x20) != 0) ||
        ((p[1] != 200) && (p[1] != 201)))
      return 0;

    end = p + len;
    do {
       if (p[1] == 202)
          sawsdes = 1;

        p += (ntohs(*((short *) (p + 2))) + 1) * 4;
    } while (p < end && (((p[0] >> 6) & 3) == 3));

    return sawsdes;
}

static int el_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct el_pvt *p = ast->tech_pvt;
	struct el_instance *instp = p->instp;
	char buf[100];


	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "el_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	/* When we call, it just works, really, there's no destination...  Just
	   ring the phone and wait for someone to answer */
	if (option_debug) ast_log(LOG_DEBUG, "Calling %s on %s\n", dest, ast->name);
	if (*dest)  /* if number specified */
	{
		char *str,*cp;

		str = ast_strdup(dest);
		cp = strchr(str,'/');
		if (cp) *cp++ = 0; else cp = str;
		snprintf(buf,sizeof(buf) - 1,"o.conip %s",cp);
		ast_free(str);
		ast_mutex_lock(&instp->lock);
		strcpy(instp->el_node_test.ip,cp);
		do_new_call(instp,p,"OUTBOUND","OUTBOUND");
		process_cmd(buf,"127.0.0.1",instp);
		ast_mutex_unlock(&instp->lock);
	}
	ast_setstate(ast,AST_STATE_RINGING);
	return 0;
}

static void el_destroy(struct el_pvt *p)
{
	if (p->dsp) ast_dsp_free(p->dsp);
	if (p->xpath) ast_translator_free_path(p->xpath);
	if (p->linkstr) ast_free(p->linkstr);
	p->linkstr = NULL;
        twalk(el_node_list, send_info); 
#ifdef	OLD_ASTERISK
	ast_mutex_lock(&usecnt_lock);
	usecnt--;
	ast_mutex_unlock(&usecnt_lock);
#else
	ast_module_user_remove(p->u);
#endif
	ast_free(p);
}

static struct el_pvt *el_alloc(void *data)
{
	struct el_pvt *p;
	int n;
	/* int flags = 0; */
	char stream[256];
        
	if (ast_strlen_zero(data)) return NULL;

	for(n = 0; n < ninstances; n++)
	{
		if (!strcmp(instances[n]->name,(char *)data)) break;
	}
	if (n >= ninstances)
	{
		ast_log(LOG_ERROR,"Cannot find echolink channel %s\n",(char *)data);
		return NULL;
	}

	p = ast_malloc(sizeof(struct el_pvt));
	if (p) {
		memset(p, 0, sizeof(struct el_pvt));
		
		sprintf(stream,"%s-%lu",(char *)data,instances[n]->seqno++);
		strcpy(p->stream,stream);
		p->rxqast.qe_forw = &p->rxqast;
		p->rxqast.qe_back = &p->rxqast;

                p->rxqel.qe_forw = &p->rxqel;
                p->rxqel.qe_back = &p->rxqel;
                
		p->keepalive = KEEPALIVE_TIME;
		p->instp = instances[n];
		p->instp->confp = p;  /* save for conference mode */
		if (!p->instp->useless_flag_1)
		{
			p->dsp = ast_dsp_new();
			if (!p->dsp)
			{
				ast_log(LOG_ERROR,"Cannot get DSP!!\n");
				return NULL;
			}
#ifdef  NEW_ASTERISK
	                ast_dsp_set_features(p->dsp,DSP_FEATURE_DIGIT_DETECT);
	                ast_dsp_set_digitmode(p->dsp,DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
#else
	                ast_dsp_set_features(p->dsp,DSP_FEATURE_DTMF_DETECT);
	                ast_dsp_digitmode(p->dsp,DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
#endif
			p->xpath = ast_translator_build_path(AST_FORMAT_SLINEAR,AST_FORMAT_GSM);
			if (!p->xpath)
			{
				ast_log(LOG_ERROR,"Cannot get translator!!\n");
				return NULL;
			}
		}
	}
	return p;
}

static int el_hangup(struct ast_channel *ast)
{
	struct el_pvt *p = ast->tech_pvt;
	struct el_instance *instp = p->instp;
	int i,n;
        unsigned char bye[50];
	struct sockaddr_in sin;
	time_t now;

	if (!instp->useless_flag_1)
	{
		if (debug) ast_log(LOG_DEBUG,"Sent bye to IP address %s\n",p->ip);
		ast_mutex_lock(&instp->lock);
		strcpy(instp->el_node_test.ip,p->ip);
		find_delete(&instp->el_node_test);
		ast_mutex_unlock(&instp->lock);
		n = rtcp_make_bye(bye,"disconnected");
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = inet_addr(p->ip);
		sin.sin_port = htons(instp->ctrl_port);
		for (i = 0; i < 20; i++)
		{
			sendto(instp->ctrl_sock, bye, n,
				0,(struct sockaddr *)&sin,sizeof(sin));
		}
		time(&now);
		if (instp->starttime < (now - EL_APRS_START_DELAY))
			instp->aprstime = now;
	}		
	if (option_debug)
		ast_log(LOG_DEBUG, "el_hangup(%s)\n", ast->name);
	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	el_destroy(p);
	ast->tech_pvt = NULL;
	ast_setstate(ast, AST_STATE_DOWN);
	return 0;
}

#ifdef	OLD_ASTERISK
static int el_indicate(struct ast_channel *ast, int cond)
#else
static int el_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen)
#endif
{
	struct el_pvt *p = ast->tech_pvt;

	switch (cond) {
		case AST_CONTROL_RADIO_KEY:
			p->txkey = 1;
			break;
		case AST_CONTROL_RADIO_UNKEY:
			p->txkey = 0;
			break;
		case AST_CONTROL_HANGUP:
			return -1;
		default:
			return 0;
	}

	return 0;
}

#ifndef	OLD_ASTERISK

static int el_digit_begin(struct ast_channel *ast, char digit)
{
	return -1;
}

#endif

#ifdef	OLD_ASTERISK
static int el_digit_end(struct ast_channel *ast, char digit)
#else
static int el_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
#endif
{
	return -1;
}

static int mycompar(const void *a, const void *b)
{
char	**x = (char **) a;
char	**y = (char **) b;
int	xoff,yoff;

	if ((**x < '0') || (**x > '9')) xoff = 1; else xoff = 0;
	if ((**y < '0') || (**y > '9')) yoff = 1; else yoff = 0;
	return(strcmp((*x) + xoff,(*y) + yoff));
}

static int el_text(struct ast_channel *ast, const char *text)
{
#define	MAXLINKSTRS 200

	struct el_pvt *p = ast->tech_pvt;
	char *cmd = NULL,*arg1 = NULL;
	char delim = ' ',*saveptr,*cp,*pkt;
	char buf[200],*ptr,str[200],*arg4 = NULL,*strs[MAXLINKSTRS];
	int i,j,k,x;

	strncpy(buf,text,sizeof(buf) - 1);
	ptr = strchr(buf, (int)'\r'); 
	if (ptr) *ptr = '\0';
	ptr = strchr(buf, (int)'\n');    
	if (ptr) *ptr = '\0';

	if (p->instp && (!p->instp->useless_flag_1) && (text[0] == 'L'))
	{
		if (strlen(text) < 3)
		{
			if (p->linkstr)
			{
				ast_free(p->linkstr);
				p->linkstr = NULL;
			        twalk(el_node_list, send_info); 
			}
			return 0;
		}
		if (p->linkstr)
		{
			ast_free(p->linkstr);
			p->linkstr = NULL;
		}
		cp = ast_strdup(text + 2);
		if (!cp)
		{
			ast_log(LOG_ERROR,"Couldnt alloc");
			return -1;
		}
		i = finddelim(cp,strs,MAXLINKSTRS);
		if (i) 
		{
			qsort((void *)strs,i,sizeof(char *),mycompar);
			pkt = ast_malloc((i * 10) + 50);
			if (!pkt)
			{
				ast_log(LOG_ERROR,"Couldnt alloc");
				return -1;
			}
			memset(pkt,0,(i * 10) + 50);
			j = 0;
			k = 0;
			for(x = 0; x < i; x++)
			{
			    if ((*(strs[x] + 1) < '3') ||
			        (*(strs[x] + 1) > '4'))

			    {
				    if (strlen(pkt + k) >= 32)
				    {
					k = strlen(pkt);
					strcat(pkt,"\r    ");
				    }
				    if (!j++) strcat(pkt,"Allstar:");
				    if (*strs[x] == 'T')
					    sprintf(pkt + strlen(pkt)," %s",strs[x] + 1);
				    else
					    sprintf(pkt + strlen(pkt)," %s(M)",strs[x] + 1);
			    }
			}
			strcat(pkt,"\r");
			j = 0;
			k = strlen(pkt);
			for(x = 0; x < i; x++)
			{
			    if (*(strs[x] + 1) == '3')
			    {
				    if (strlen(pkt + k) >= 32)
				    {
					k = strlen(pkt);
					strcat(pkt,"\r    ");
				    }
				    if (!j++) strcat(pkt,"Echolink: ");
				    if (*strs[x] == 'T')
					    sprintf(pkt + strlen(pkt)," %d",atoi(strs[x] + 2));
				    else
					    sprintf(pkt + strlen(pkt)," %d(M)",atoi(strs[x] + 2));
			    }
			}
			strcat(pkt,"\r");
			if (p->linkstr && pkt && (!strcmp(p->linkstr,pkt))) ast_free(pkt);
			else p->linkstr = pkt;
		}
		ast_free(cp);
	        twalk(el_node_list, send_info); 
		return 0;
	}

	cmd = strtok_r(buf, &delim, &saveptr);
	if (!cmd)
	{
		return 0;
	}

	arg1 = strtok_r(NULL, &delim, &saveptr);
	strtok_r(NULL, &delim, &saveptr);
	strtok_r(NULL, &delim, &saveptr);
	arg4 = strtok_r(NULL, &delim, &saveptr);

	if (!strcasecmp(cmd,"D"))
	{
		sprintf(str,"3%06u",p->nodenum);
		/* if not for this one, we cant go any farther */
		if (strcmp(arg1,str)) return 0;
		ast_senddigit(ast,*arg4);
		return 0;
	}
	return 0;
}

static int compare_ip(const void *pa, const void *pb)
{
   return strncmp(((struct el_node *)pa)->ip,((struct el_node *)pb)->ip,EL_IP_SIZE); 
}

/* Echolink ---> Echolink */
void send_audio_all_but_one(const void *nodep, const VISIT which, const int depth) 
{
   struct sockaddr_in sin;
   struct el_instance *instp = (*(struct el_node **)nodep)->instp;

   if ((which == leaf) || (which == postorder)) {
      if (strncmp((*(struct el_node **)nodep)->ip, instp->el_node_test.ip,EL_IP_SIZE) != 0) {
         sin.sin_family = AF_INET;
         sin.sin_port = htons(instp->audio_port);
         sin.sin_addr.s_addr = inet_addr((*(struct el_node **)nodep)->ip);

         instp->audio_all_but_one.version = 3;
         instp->audio_all_but_one.pad = 0;
         instp->audio_all_but_one.ext = 0;
         instp->audio_all_but_one.csrc = 0;
         instp->audio_all_but_one.marker = 0;
         instp->audio_all_but_one.payt = 3;
         instp->audio_all_but_one.seqnum = htons((*(struct el_node **)nodep)->seqnum++); 
         instp->audio_all_but_one.time = htonl(0);
         instp->audio_all_but_one.ssrc = htonl(instp->mynode);

         sendto(instp->audio_sock, (char *)&instp->audio_all_but_one, sizeof(instp->audio_all_but_one),
                0,(struct sockaddr *)&sin,sizeof(sin));
      }
   }
}

static void send_audio_only_one(const void *nodep, const VISIT which, const int depth) 
{
   struct sockaddr_in sin;
   struct el_instance *instp = (*(struct el_node **)nodep)->instp;

   if ((which == leaf) || (which == postorder)) {
      if (strncmp((*(struct el_node **)nodep)->ip, instp->el_node_test.ip,EL_IP_SIZE) == 0) {
         sin.sin_family = AF_INET;
         sin.sin_port = htons(instp->audio_port);
         sin.sin_addr.s_addr = inet_addr((*(struct el_node **)nodep)->ip);

      instp->audio_all.version = 3;
      instp->audio_all.pad = 0;
      instp->audio_all.ext = 0;
      instp->audio_all.csrc = 0;
      instp->audio_all.marker = 0;
      instp->audio_all.payt = 3;
      instp->audio_all.seqnum = htons((*(struct el_node **)nodep)->seqnum++);
      instp->audio_all.time = htonl(0);
      instp->audio_all.ssrc = htonl(instp->mynode);

      sendto(instp->audio_sock, (char *)&instp->audio_all, sizeof(instp->audio_all), 
             0,(struct sockaddr *)&sin,sizeof(sin));
      }
   }
}

/* Asterisk ---> Echolink */
void send_audio_all(const void *nodep, const VISIT which, const int depth)
{
   struct sockaddr_in sin;
   struct el_instance *instp = (*(struct el_node **)nodep)->instp;

   if ((which == leaf) || (which == postorder)) {
      sin.sin_family = AF_INET;
      sin.sin_port = htons(instp->audio_port);
      sin.sin_addr.s_addr = inet_addr((*(struct el_node **)nodep)->ip);

      instp->audio_all.version = 3;
      instp->audio_all.pad = 0;
      instp->audio_all.ext = 0;
      instp->audio_all.csrc = 0;
      instp->audio_all.marker = 0;
      instp->audio_all.payt = 3;
      instp->audio_all.seqnum = htons((*(struct el_node **)nodep)->seqnum++);
      instp->audio_all.time = htonl(0);
      instp->audio_all.ssrc = htonl(instp->mynode);

      sendto(instp->audio_sock, (char *)&instp->audio_all, sizeof(instp->audio_all), 
             0,(struct sockaddr *)&sin,sizeof(sin));
   }
}

static void print_users(const void *nodep, const VISIT which, const int depth)
{
   if ((which == leaf) || (which == postorder)) {
      ast_verbose("Echolink user: call=%s,ip=%s,name=%s\n",
             (*(struct el_node **)nodep)->call,
             (*(struct el_node **)nodep)->ip,
             (*(struct el_node **)nodep)->name);
   }
}

static void count_users(const void *nodep, const VISIT which, const int depth)
{
   if ((which == leaf) || (which == postorder)) {
	if ((*(struct el_node **)nodep)->instp == count_instp) {
		count_n++;
		if ((*(struct el_node **)nodep)->outbound) count_outbound_n++;
	}
   }
}

static void send_info(const void *nodep, const VISIT which, const int depth)
{
	struct sockaddr_in sin;
	char pkt[2500],*cp;
	struct el_instance *instp = (*(struct el_node **)nodep)->instp;
	int i;

	if ((which == leaf) || (which == postorder)) {

		sin.sin_family = AF_INET;
		sin.sin_port = htons(instp->audio_port);
		sin.sin_addr.s_addr = inet_addr((*(struct el_node **)nodep)->ip);
		snprintf(pkt,sizeof(pkt) - 1,
			"oNDATA\rWelcome to Allstar Node %s\r",instp->astnode);
		i = strlen(pkt);
		snprintf(pkt + i,sizeof(pkt) - (i + 1),
			"Echolink Node %s\rNumber %u\r \r",
				instp->mycall,instp->mynode);
		if ((*(struct el_node **)nodep)->p &&
		    (*(struct el_node **)nodep)->p->linkstr)
		{
			i = strlen(pkt);
			strncat(pkt + i,"Systems Linked:\r",
				sizeof(pkt) - (i + 1));
			cp = ast_strdup((*(struct el_node **)nodep)->p->linkstr);
			i = strlen(pkt);
			strncat(pkt + i,cp,sizeof(pkt) - (i + 1));
			ast_free(cp);
		}
		sendto(instp->audio_sock, pkt, strlen(pkt),
			0,(struct sockaddr *)&sin,sizeof(sin));
	}
	return;
}

static void send_heartbeat(const void *nodep, const VISIT which, const int depth)
{
   struct sockaddr_in sin;
   unsigned char  sdes_packet[256];
   int sdes_length;
   struct el_instance *instp = (*(struct el_node **)nodep)->instp;

   if ((which == leaf) || (which == postorder)) {

      if ((*(struct el_node **)nodep)->countdown >= 0)
         (*(struct el_node **)nodep)->countdown --;
  
      if ((*(struct el_node **)nodep)->countdown < 0) {
         strncpy(instp->el_node_test.ip,(*(struct el_node **)nodep)->ip,EL_IP_SIZE+1);
         strncpy(instp->el_node_test.call,(*(struct el_node **)nodep)->call,EL_CALL_SIZE+1);
         ast_log(LOG_WARNING,"countdown for %s(%s) negative\n",instp->el_node_test.call,instp->el_node_test.ip);
      }
      memset(sdes_packet,0,sizeof(sdes_packet));
      sdes_length = rtcp_make_sdes(sdes_packet,sizeof(sdes_packet),
	instp->mycall,instp->myname,instp->astnode);

      sin.sin_family = AF_INET;
      sin.sin_port = htons(instp->ctrl_port);
      sin.sin_addr.s_addr = inet_addr((*(struct el_node **)nodep)->ip);
      sendto(instp->ctrl_sock, sdes_packet, sdes_length, 
             0,(struct sockaddr *)&sin,sizeof(sin));
   }
}

static void free_node(void *nodep)
{
}

static int find_delete(struct el_node *key)
{
   int found = 0;
   struct el_node **found_key = NULL;

   found_key = (struct el_node **)tfind(key, &el_node_list, compare_ip);
   if (found_key) {
       if (debug) ast_log(LOG_DEBUG,"...removing %s(%s)\n", (*found_key)->call, (*found_key)->ip); 
       found = 1;
       if (!(*found_key)->instp->useless_flag_1) 
		ast_softhangup((*found_key)->chan,AST_SOFTHANGUP_DEV);
       tdelete(key, &el_node_list, compare_ip);
   }
   return found;
}

static void process_cmd(char *buf, char *fromip,struct el_instance *instp)
{
   char *cmd = NULL;
   char *arg1 = NULL;

   char delim = ' ';
   char *saveptr;
   char *ptr;
   struct sockaddr_in sin;
   unsigned char  pack[256];
   int pack_length;
   unsigned short i,n;
   struct el_node key;

   if (strncmp(fromip,"127.0.0.1",EL_IP_SIZE) != 0)
      return;
   ptr = strchr(buf, (int)'\r'); 
   if (ptr)
      *ptr = '\0';
   ptr = strchr(buf, (int)'\n');    
   if (ptr)
      *ptr = '\0';

   /* all commands with no arguments go first */

   if (strcmp(buf,"o.users") == 0) {
      twalk(el_node_list, print_users);
      return;
   }

   if (strcmp(buf, "o.rec") == 0) {
      if (instp->fdr >= 0) {
         close(instp->fdr);
         instp->fdr = -1;
         if (debug) ast_log(LOG_DEBUG, "rec stopped\n");
      }
      else {
         instp->fdr = open(instp->fdr_file, O_CREAT | O_WRONLY | O_APPEND | O_TRUNC, 
                    S_IRUSR | S_IWUSR);
         if (debug && instp->fdr >= 0)
            ast_log(LOG_DEBUG, "rec into %s started\n", instp->fdr_file);
      }
      return;
   }

   cmd = strtok_r(buf, &delim, &saveptr);
   if (!cmd)
   {
      return;
   }

   /* This version:  up to 3 parameters */
   arg1 = strtok_r(NULL, &delim, &saveptr);
   strtok_r(NULL, &delim, &saveptr);
   strtok_r(NULL, &delim, &saveptr);

   if ((strcmp(cmd, "o.conip") == 0) ||
       (strcmp(cmd, "o.dconip") == 0)) {
      if (!arg1)
      {
         return;
      }

      if (strcmp(cmd, "o.conip") == 0)
      {
	 n = 1;
         pack_length = rtcp_make_sdes(pack,sizeof(pack),instp->mycall,instp->myname,instp->astnode);
      }
      else
      {
         pack_length = rtcp_make_bye(pack,"bye");
	 n = 20;
      }      
      sin.sin_family = AF_INET;
      sin.sin_port = htons(instp->ctrl_port);
      sin.sin_addr.s_addr = inet_addr(arg1);

      if (strcmp(cmd, "o.dconip") == 0) {
           strncpy(key.ip,arg1,EL_IP_SIZE);
           if (find_delete(&key)) {
              for (i = 0; i < 20; i++)
                 sendto(instp->ctrl_sock, pack, pack_length,
                        0,(struct sockaddr *)&sin,sizeof(sin));
              if (debug) ast_log(LOG_DEBUG,"disconnect request sent to %s\n",key.ip);
           }
           else
              if (debug) ast_log(LOG_DEBUG, "Did not find ip=%s to request disconnect\n",key.ip);
      }
      else {
         for (i = 0; i < n; i++)
	 {
            sendto(instp->ctrl_sock, pack, pack_length,
                   0,(struct sockaddr *)&sin,sizeof(sin));
	 }
         if (debug) ast_log(LOG_DEBUG,"connect request sent to %s\n", arg1);
      }
      return;
   }
   return;
}

static struct ast_frame  *el_xread(struct ast_channel *ast)
{
	struct el_pvt *p = ast->tech_pvt;
  
	memset(&p->fr,0,sizeof(struct ast_frame));
        p->fr.frametype = 0;
        p->fr.subclass = 0;
        p->fr.datalen = 0;
        p->fr.samples = 0;
        p->fr.data =  NULL;
        p->fr.src = type;
        p->fr.offset = 0;
        p->fr.mallocd=0;
        p->fr.delivery.tv_sec = 0;
        p->fr.delivery.tv_usec = 0;
        return &p->fr;
}

static int el_xwrite(struct ast_channel *ast, struct ast_frame *frame)
{
        int bye_length;
        unsigned char bye[50];
        unsigned short i;
        struct sockaddr_in sin;
	struct el_pvt *p = ast->tech_pvt;
	struct el_instance *instp = p->instp;
	struct ast_frame fr,*f1, *f2;
	struct el_rxqast *qpast;
	int n,m,x;
        struct el_rxqel *qpel;
	char buf[GSM_FRAME_SIZE + AST_FRIENDLY_OFFSET];

	if (frame->frametype != AST_FRAME_VOICE) return 0;

	if (!p->firstsent)
	{
		struct sockaddr_in sin;
		unsigned char  sdes_packet[256];
		int sdes_length;

		p->firstsent = 1;
		memset(sdes_packet,0,sizeof(sdes_packet));
		sdes_length = rtcp_make_sdes(sdes_packet,sizeof(sdes_packet),
			instp->mycall,instp->myname,instp->astnode);

		sin.sin_family = AF_INET;
		sin.sin_port = htons(instp->ctrl_port);
		sin.sin_addr.s_addr = inet_addr(p->ip);
		sendto(instp->ctrl_sock, sdes_packet, sdes_length, 
			0,(struct sockaddr *)&sin,sizeof(sin));
	}

        /* Echolink to Asterisk */
	if (p->rxqast.qe_forw != &p->rxqast) {
		for(n = 0,qpast = p->rxqast.qe_forw; qpast != &p->rxqast; qpast = qpast->qe_forw) {
			n++;
		}
		if (n > QUEUE_OVERLOAD_THRESHOLD_AST) {
			while(p->rxqast.qe_forw != &p->rxqast) {
				qpast = p->rxqast.qe_forw;
				remque((struct qelem *)qpast);
				ast_free(qpast);
			}
			if (p->rxkey) p->rxkey = 1;
		} else {		
			if (!p->rxkey) {
				memset(&fr,0,sizeof(fr));
				fr.datalen = 0;
				fr.samples = 0;
				fr.frametype = AST_FRAME_CONTROL;
				fr.subclass = AST_CONTROL_RADIO_KEY;
				fr.data =  0;
				fr.src = type;
				fr.offset = 0;
				fr.mallocd=0;
				fr.delivery.tv_sec = 0;
				fr.delivery.tv_usec = 0;
				ast_queue_frame(ast,&fr);
			} 
			p->rxkey = MAX_RXKEY_TIME;
			qpast = p->rxqast.qe_forw;
			remque((struct qelem *)qpast);
			memcpy(buf + AST_FRIENDLY_OFFSET,qpast->buf,GSM_FRAME_SIZE);
			ast_free(qpast);

			memset(&fr,0,sizeof(fr));
			fr.datalen = GSM_FRAME_SIZE;
			fr.samples = 160;
			fr.frametype = AST_FRAME_VOICE;
			fr.subclass = AST_FORMAT_GSM;
			fr.data =  buf + AST_FRIENDLY_OFFSET;
			fr.src = type;
			fr.offset = AST_FRIENDLY_OFFSET;
			fr.mallocd=0;
			fr.delivery.tv_sec = 0;
			fr.delivery.tv_usec = 0;

			x = 0;
			if (p->dsp && (!instp->useless_flag_1))
			{
				f2 = ast_translate(p->xpath,&fr,0);
				f1 = ast_dsp_process(NULL,p->dsp,f2);
				ast_frfree(f2);
#ifdef	OLD_ASTERISK
				if (f1->frametype == AST_FRAME_DTMF)
#else
				if ((f1->frametype == AST_FRAME_DTMF_END) ||
					(f1->frametype == AST_FRAME_DTMF_BEGIN))
#endif
				{
					if ((f1->subclass != 'm') && (f1->subclass != 'u'))
					{
#ifndef	OLD_ASTERISK
						if (f1->frametype == AST_FRAME_DTMF_END)
#endif
							if (option_verbose > 3) ast_verbose(VERBOSE_PREFIX_3 "Echolink %s Got DTMF char %c from IP %s\n",p->stream,f1->subclass,p->ip);
						ast_queue_frame(ast,f1);
						x = 1;
					}
				}
			} 
			if (!x) ast_queue_frame(ast,&fr);
		}
	}
	if (p->rxkey == 1) {
		memset(&fr,0,sizeof(fr));
		fr.datalen = 0;
		fr.samples = 0;
		fr.frametype = AST_FRAME_CONTROL;
		fr.subclass = AST_CONTROL_RADIO_UNKEY;
		fr.data =  0;
		fr.src = type;
		fr.offset = 0;
		fr.mallocd=0;
		fr.delivery.tv_sec = 0;
		fr.delivery.tv_usec = 0;
		ast_queue_frame(ast,&fr);
	} 
	if (p->rxkey) p->rxkey--;

        if (instp->useless_flag_1 && (p->rxqel.qe_forw != &p->rxqel))
        {
           for(m = 0,qpel = p->rxqel.qe_forw; qpel != &p->rxqel; qpel = qpel->qe_forw) 
              m++;

           if (m > QUEUE_OVERLOAD_THRESHOLD_EL)
           {
              while(p->rxqel.qe_forw != &p->rxqel) 
              {
                 qpel = p->rxqel.qe_forw;
                 remque((struct qelem *)qpel);
                 ast_free(qpel);
              }
           } 
           else 
           {
              qpel = p->rxqel.qe_forw;
              remque((struct qelem *)qpel);

              memcpy(instp->audio_all_but_one.data,qpel->buf,BLOCKING_FACTOR * GSM_FRAME_SIZE);
              strncpy(instp->el_node_test.ip, qpel->fromip, EL_IP_SIZE+1);

              ast_free(qpel);
	      ast_mutex_lock(&instp->lock);
              twalk(el_node_list, send_audio_all_but_one);
	      ast_mutex_unlock(&instp->lock);

              if (instp->fdr >= 0)
                 write(instp->fdr, instp->audio_all_but_one.data, BLOCKING_FACTOR * GSM_FRAME_SIZE);
           }
        }
        else
        {
           /* Asterisk to Echolink */
           if (!(frame->subclass & (AST_FORMAT_GSM))) {
                ast_log(LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
		ast_mutex_unlock(&instp->lock);
                return 0;
           }
           if (p->txkey || p->txindex)  {
                memcpy(instp->audio_all.data + (GSM_FRAME_SIZE * p->txindex++), frame->data,GSM_FRAME_SIZE);
           }      
           if (p->txindex >= BLOCKING_FACTOR) {
		ast_mutex_lock(&instp->lock);
                if (instp->useless_flag_1)
		{
			twalk(el_node_list, send_audio_all);
		}
		else
		{
			strcpy(instp->el_node_test.ip,p->ip);
			twalk(el_node_list, send_audio_only_one);
		}
		ast_mutex_unlock(&instp->lock);
                p->txindex = 0;
           }
        }

	if (p->keepalive--) return 0;
	p->keepalive = KEEPALIVE_TIME;

        /* Echolink: send heartbeats and drop dead stations */
	ast_mutex_lock(&instp->lock);
        instp->el_node_test.ip[0] = '\0';
        twalk(el_node_list, send_heartbeat); 
        if (instp->el_node_test.ip[0] != '\0') {
           if (find_delete(&instp->el_node_test)) {
              bye_length = rtcp_make_bye(bye,"rtcp timeout");
              sin.sin_family = AF_INET;
              sin.sin_addr.s_addr = inet_addr(instp->el_node_test.ip);
              sin.sin_port = htons(instp->ctrl_port);
	      ast_mutex_lock(&instp->lock);
              for (i = 0; i < 20; i++)
                 sendto(instp->ctrl_sock, bye, bye_length,
                        0,(struct sockaddr *)&sin,sizeof(sin));
	      ast_mutex_unlock(&instp->lock);
              if (option_verbose > 3) ast_verbose(VERBOSE_PREFIX_3 "call=%s RTCP timeout, removing\n",instp->el_node_test.call);
           }
           instp->el_node_test.ip[0] = '\0';
        } 
	ast_mutex_unlock(&instp->lock);
	return 0;
}

static struct ast_channel *el_new(struct el_pvt *i, int state, unsigned int nodenum)
{
	struct ast_channel *tmp;
	struct el_instance *instp = i->instp;

#ifdef	OLD_ASTERISK
	tmp = ast_channel_alloc(1);
	if (tmp)
	{
		ast_setstate(tmp,state);
		ast_copy_string(tmp->context, instp->context, sizeof(tmp->context));
		ast_copy_string(tmp->exten, instp->astnode, sizeof(tmp->exten));
		snprintf(tmp->name, sizeof(tmp->name), "echolink/%s", i->stream);
#else
	tmp = ast_channel_alloc(1, state, 0, 0, "", instp->astnode, instp->context, 0, "echolink/%s", i->stream);
	if (tmp) {
#endif
		tmp->tech = &el_tech;
		tmp->nativeformats = prefformat;
		tmp->rawreadformat = prefformat;
		tmp->rawwriteformat = prefformat;
		tmp->writeformat = prefformat;
		tmp->readformat = prefformat;
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->tech_pvt = i;
		ast_copy_string(tmp->context, instp->context, sizeof(tmp->context));
		ast_copy_string(tmp->exten, instp->astnode,  sizeof(tmp->exten));
#ifdef	OLD_ASTERISK
		ast_copy_string(tmp->language, "", sizeof(tmp->language));
#else
		ast_string_field_set(tmp, language, "");
#endif
		if (nodenum > 0)
		{
			char tmpstr[30];

			sprintf(tmpstr,"3%06u",nodenum);
			ast_set_callerid(tmp,tmpstr,NULL,NULL);
		}
		i->owner = tmp;
#ifdef	OLD_ASTERISK
		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
#else
		i->u = ast_module_user_add(tmp);
#endif
		i->nodenum = nodenum;
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
			}
		}
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	return tmp;
}


static struct ast_channel *el_request(const char *type, int format, void *data, int *cause)
{
	int oldformat,nodenum;
	struct el_pvt *p;
	struct ast_channel *tmp = NULL;
	char *str,*cp;
	
	oldformat = format;
	format &= (AST_FORMAT_GSM);
	if (!format) {
		ast_log(LOG_ERROR, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		return NULL;
	}
	str = ast_strdup((char *)data);
	cp = strchr(str,'/');
	if (cp) *cp++ = 0;
	nodenum = 0;
	if (*cp && *++cp) nodenum = atoi(cp);
	p = el_alloc(str);
	ast_free(str);
	if (p) {
		tmp = el_new(p, AST_STATE_DOWN,nodenum);
		if (!tmp)
			el_destroy(p);
	}
	return tmp;
}

/*
* Enable or disable debug output at a given level at the console
*/
                                                                                                                                 
static int el_do_debug(int fd, int argc, char *argv[])
{
	int newlevel;

        if (argc != 4)
                return RESULT_SHOWUSAGE;
        newlevel = atoi(argv[3]);
        if((newlevel < 0) || (newlevel > 7))
                return RESULT_SHOWUSAGE;
        if(newlevel)
                ast_cli(fd, "echolink Debugging enabled, previous level: %d, new level: %d\n", debug, newlevel);
        else
                ast_cli(fd, "echolink Debugging disabled\n");

        debug = newlevel;                                                                                                                          
        return RESULT_SUCCESS;
}

/*
* Dump entire database
*/
                                                                                                                                 
static int el_do_dbdump(int fd, int argc, char *argv[])
{
	char c;
        if (argc < 2)
                return RESULT_SHOWUSAGE;

	c = 'n';
	if (argc > 2)
	{
		c = tolower(*argv[2]);
	}
	ast_mutex_lock(&el_db_lock);
	nodeoutfd = fd;
	if (c == 'i') twalk(el_db_ipaddr,print_nodes);
	else if (c == 'c') twalk(el_db_callsign,print_nodes);
	else twalk(el_db_nodenum,print_nodes);
	nodeoutfd = -1;
	ast_mutex_unlock(&el_db_lock);
	return RESULT_SUCCESS;
}


/*
* Get echolink db entry
*/
                                                                                                                                 
static int el_do_dbget(int fd, int argc, char *argv[])
{
	char c;
	struct eldb *mynode;

        if (argc != 4)
                return RESULT_SHOWUSAGE;

	c = tolower(*argv[2]);
	ast_mutex_lock(&el_db_lock);
	if (c == 'i') mynode = el_db_find_ipaddr(argv[3]);
	else if (c == 'c') mynode = el_db_find_callsign(argv[3]);
	else mynode = el_db_find_nodenum(argv[3]);
	ast_mutex_unlock(&el_db_lock);
	if (!mynode)
	{
		ast_cli(fd,"Error: Entry for %s not found!\n",argv[3]);
		return RESULT_FAILURE;
	}
	ast_cli(fd,"%s|%s|%s\n",mynode->nodenum,mynode->callsign,mynode->ipaddr);
	return RESULT_SUCCESS;
}

#ifdef	NEW_ASTERISK

static char *res2cli(int r)

{
	switch (r)
	{
	    case RESULT_SUCCESS:
		return(CLI_SUCCESS);
	    case RESULT_SHOWUSAGE:
		return(CLI_SHOWUSAGE);
	    default:
		return(CLI_FAILURE);
	}
}

static char *handle_cli_debug(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "echolink debug level";
                e->usage = debug_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(el_do_debug(a->fd,a->argc,a->argv));
}

static char *handle_cli_dbdump(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "echolink dbdump";
                e->usage = dbdump_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_dbdump(a->fd,a->argc,a->argv));
}


static char *handle_cli_dbget(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "echolink dbget";
                e->usage = dbget_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_dbget(a->fd,a->argc,a->argv));
}

static struct ast_cli_entry rpt_cli[] = {
	AST_CLI_DEFINE(handle_cli_debug,"Enable app_rpt debugging"),
	AST_CLI_DEFINE(handle_cli_dbdump,"Dump entire echolink db"),
	AST_CLI_DEFINE(handle_cli_dbget,"Look up echolink db entry")
} ;

#endif

#ifndef	OLD_ASTERISK
static
#endif
int unload_module(void)
{
int	n;

        run_forever = 0;
        tdestroy(el_node_list, free_node);
	for(n = 0; n < ninstances; n++)
	{
		if (instances[n]->audio_sock != -1)
		{
			close(instances[n]->audio_sock);
			instances[n]->audio_sock = -1;
		}
		if (instances[n]->ctrl_sock != -1)
		{
			close(instances[n]->ctrl_sock);
			instances[n]->ctrl_sock = -1;
		}
	}	
#ifdef	NEW_ASTERISK
	ast_cli_unregister_multiple(el_cli,sizeof(el_cli) / 
		sizeof(struct ast_cli_entry));
#else
	/* Unregister cli extensions */
	ast_cli_unregister(&cli_debug);
	ast_cli_unregister(&cli_dbdump);
	ast_cli_unregister(&cli_dbget);
#endif
	/* First, take us out of the channel loop */
	ast_channel_unregister(&el_tech);
	for(n = 0; n < ninstances; n++) ast_free(instances[n]);
	if (nullfd != -1) close(nullfd);
	return 0;
}

/* 
   If asterisk has a function that writes at least n bytes to a TCP socket,
   remove writen function and use the one provided by Asterisk
*/
static int writen(int fd, char *ptr, int nbytes)
{
   int nleft, nwritten;
   char *local_ptr;

   nleft = nbytes;
   local_ptr = ptr;

   while (nleft > 0)
   {
      nwritten = write(fd, local_ptr, nleft);
      if (nwritten < 0)
         return nwritten;
      nleft -= nwritten;
      local_ptr += nwritten;
   }
   return (nbytes - nleft);
}

/* Feel free to make this code smaller, I know it works, so I use it */
static int sendcmd(char *server, struct el_instance *instp)
{
  struct hostent *ahp;
  struct ast_hostent ah;
  struct in_addr ia;

  char ip[EL_IP_SIZE + 1];
  struct sockaddr_in el;
  int el_len;
  int sd;
  int  rc;
  time_t now;
  struct tm *p_tm;
  char *id = NULL;
  const size_t LOGINSIZE = 1023;
  char buf[LOGINSIZE + 1];
  size_t len;

  ahp = ast_gethostbyname(server,&ah);
  if (ahp) {
     memcpy(&ia,ahp->h_addr,sizeof(in_addr_t));
#ifdef  OLD_ASTERISK
     ast_inet_ntoa(ip,EL_IP_SIZE,ia);
#else
     strncpy(ip,ast_inet_ntoa(ia),EL_IP_SIZE);
#endif
  } else {
     ast_log(LOG_ERROR, "Failed to resolve Echolink server %s\n", server);
     return -1;
  }

  memset(&el,0,sizeof(struct sockaddr_in));
  el.sin_family = AF_INET;
  el.sin_port = htons(5200);
  el.sin_addr.s_addr =  inet_addr(ip);
  el_len = sizeof(el);

  sd = socket(AF_INET,SOCK_STREAM,0);
  if (sd < 0) {
     ast_log(LOG_ERROR, "failed to create socket to contact the Echolink server %s\n", server);
     return -1;
  }

  rc = connect(sd,(struct  sockaddr *)&el,el_len);
  if (rc < 0) {
     ast_log(LOG_ERROR, "connect() failed to connect to the Echolink server %s\n", server);
     close(sd);
     return -1;
  }

  (void) time(&now);
  p_tm = localtime(&now);

  /* our version */
  if (instp->mycall[0] != '*')
     id = "1.00R";
  else
     id = "1.00B";

  snprintf(buf, LOGINSIZE,
          "l%s%c%c%s\rONLINE%s(%d:%2d)\r%s\r%s\r",
          instp->mycall,
          0xac,
          0xac,
          instp->mypwd,
          id,
          p_tm->tm_hour,p_tm->tm_mday,
          (instp->login_display[0]) ? instp->login_display : instp->myqth,
          instp->myemail);

  len = strlen(buf);
  rc = writen(sd,buf,len);
  if (rc != len) {
    ast_log(LOG_ERROR, 
        "writen() failed to send Echolink credentials to Echolink server %s\n", server);
    close(sd);
    return -1;
  }

  buf[0] = '\0';
  while (1) {
     rc = read(sd,buf,LOGINSIZE);
     if (rc > 0) {
        buf[rc] = '\0';
        if (option_verbose > 3) ast_verbose(VERBOSE_PREFIX_3 "Received %s from Echolink server %s\n", buf, server);
     }
     else
       break;
  }
  close(sd);

  if (strncmp(buf, "OK", 2) != 0)
     return -1;

      
  return 0;
}

#define	EL_DIRECTORY_PORT 5200

static void my_stupid_free(void *ptr)
{
	ast_free(ptr);
	return;
}

static void el_zapem(void)
{
	ast_mutex_lock(&el_db_lock);
        tdestroy(el_node_list, my_stupid_free);
	ast_mutex_unlock(&el_db_lock);
}

static void el_zapcall(char *call)
{
struct eldb *mynode;

	if (debug > 1)
		ast_log(LOG_DEBUG,"zapcall eldb delete Attempt: Call=%s\n",call);
	ast_mutex_lock(&el_db_lock);
	mynode = el_db_find_callsign(call);
	if (mynode)
	{
		if (debug > 1)
			ast_log(LOG_DEBUG,"zapcall eldb delete: Node=%s, Call=%s, IP=%s\n",
				mynode->nodenum,mynode->callsign,mynode->ipaddr);
		el_db_delete(mynode);
	}
	ast_mutex_unlock(&el_db_lock);
}

static int el_net_read(int sock,unsigned char *buf1,int buf1len,
	int compressed,struct z_stream_s *z)
{
unsigned char buf[512];
int	n,r;

	for(;;)
	{
		if (!compressed)
		{
			n = recv(sock,buf1,buf1len - 1,0);
			if (n < 1) return(-1);
			return(n);
		}		
		memset(buf1,0,buf1len);
		memset(buf,0,sizeof(buf));
		n = recv(sock,buf,sizeof(buf) - 1,0);
		if (n < 0) return(-1);
		z->next_in = buf;
		z->avail_in = n;
		z->next_out = buf1;
		z->avail_out = buf1len;
		r = inflate(z,Z_NO_FLUSH);
		if ((r != Z_OK) && (r != Z_STREAM_END))
		{
			if (z->msg)
				ast_log(LOG_ERROR,"Unable to inflate (Zlib): %s\n",z->msg);
			else
				ast_log(LOG_ERROR,"Unable to inflate (Zlib)\n");
			return -1;
		}
		r = buf1len - z->avail_out;
		if ((!n) || r) break;
	}
	return(buf1len - z->avail_out);
}

static int el_net_get_line(int s,char *str, int max, int compressed,
	struct z_stream_s *z)
{
int nstr;
static unsigned char buf[2048];
unsigned char c;

	nstr = 0;
	for(;;)
	{
		if (el_net_get_index >= el_net_get_nread)
		{
			el_net_get_index = 0;
			el_net_get_nread = el_net_read(s,buf,sizeof(buf),compressed,z);
			if ((el_net_get_nread) < 1) return(el_net_get_nread);
		}
		if (buf[el_net_get_index] > 126) buf[el_net_get_index] = ' ';
		c = buf[el_net_get_index++];
		str[nstr++] = c & 0x7f;
		str[nstr] = 0;
		if (c < ' ') break;
		if (nstr >= max) break;
	}	
	return(nstr);
}

static int do_el_directory(char *hostname)
{
struct ast_hostent ah;
struct hostent *host;
struct sockaddr_in dirserver;
char	str[200],ipaddr[200],nodenum[200];
char	call[200],*pp,*cc;
int	n = 0,rep_lines,delmode;
int	dir_compressed,dir_partial;
struct	z_stream_s z;
int	sock;

	sendcmd(hostname,instances[0]);
	el_net_get_index = 0;
	el_net_get_nread = 0;
	memset(&z,0,sizeof(z));
	if (inflateInit(&z) != Z_OK)
	{
		if (z.msg)
			ast_log(LOG_ERROR,"Unable to init Zlib: %s\n",z.msg);
		else
			ast_log(LOG_ERROR,"Unable to init Zlib\n");
		return -1;
	}
	host = ast_gethostbyname(hostname,&ah);
	if (!host)
	{
		ast_log(LOG_ERROR,"Unable to resolve name for directory server %s\n",hostname);
		inflateEnd(&z);
		return -1;
	}
	memset(&dirserver, 0, sizeof(dirserver));       /* Clear struct */
	dirserver.sin_family = AF_INET;                  /* Internet/IP */
	dirserver.sin_addr.s_addr = 
		*(unsigned long *) host->h_addr_list[0];
	dirserver.sin_port = htons(EL_DIRECTORY_PORT);       /* server port */
	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0)
	{
		ast_log(LOG_ERROR,"Unable to obtain a socket for directory server %s\n",hostname);
		inflateEnd(&z);
		return -1;
	}
        /* Establish connection */
	if (connect(sock,(struct sockaddr *) &dirserver,
		sizeof(dirserver)) < 0)
	{
		ast_log(LOG_ERROR,"Unable to connect to directory server %s\n",hostname);
		inflateEnd(&z);
		return -1;
	}
	sprintf(str,"F%s\r",snapshot_id);
	if (send(sock,str,strlen(str),0) < 0)
	{
		ast_log(LOG_ERROR,"Unable to send to directory server %s\n",hostname);
		close(sock);
		inflateEnd(&z);
		return -1;
	}
	str[strlen(str) - 1] = 0;
	if (debug) printf("Sending: %s to %s\n",str,hostname);
	if (recv(sock,str,4,0) != 4)
	{
		ast_log(LOG_ERROR,"Error in directory download (header) on %s\n",hostname);
		close(sock);
		inflateEnd(&z);
		return -1;
	} 
	dir_compressed = 1;
	dir_partial = 0;
	if (!strncmp(str,"@@@",3))
	{
		dir_partial = 0;
		dir_compressed = 0;
	}
	else if (!strncmp(str,"DDD",3))
	{
		dir_partial = 1;
		dir_compressed = 0;
	}
	if (dir_compressed)
	{
		if (el_net_get_line(sock,str,sizeof(str) - 1,dir_compressed,&z) < 1)
		{
			ast_log(LOG_ERROR,"Error in directory download (header) on %s\n",hostname);
			close(sock);
			inflateEnd(&z);
			return -1;
		}
		if (!strncmp(str,"@@@",3))
		{
			dir_partial = 0;
		}
		else if (!strncmp(str,"DDD",3))
		{
			dir_partial = 1;
		}
		else
		{
			ast_log(LOG_ERROR,"Error in header on %s\n",hostname);
			close(sock);
			inflateEnd(&z);
			return -1;
		}
	}
	if (el_net_get_line(sock,str,sizeof(str) - 1,dir_compressed,&z) < 1)
	{
		ast_log(LOG_ERROR,"Error in directory download (header) on %s\n",hostname);
		close(sock);
		inflateEnd(&z);
		return -1;
	}
	if (dir_compressed)
	{
		if(sscanf(str,"%d:%s",&rep_lines,snapshot_id) < 2)
		{
			ast_log(LOG_ERROR,"Error in parsing header on %s\n",hostname);
			close(sock);
			inflateEnd(&z);
			return -1;
		}	
	}
	else
	{
		if(sscanf(str,"%d",&rep_lines) < 1)
		{
			ast_log(LOG_ERROR,"Error in parsing header on %s\n",hostname);
			close(sock);
			inflateEnd(&z);
			return -1;
		}	
	}
	delmode = 0;
	if (!dir_partial) el_zapem();
	for(;;)
	{
		if (el_net_get_line(sock,str,sizeof(str) - 1,dir_compressed,&z) < 1) break;
		if (*str <= ' ') break;
		if (!strncmp(str,"+++",3))
		{
			if (delmode) break;
			if (!dir_partial) break;
			delmode = 1;
			continue;
		}			
		if (str[strlen(str) - 1] == '\n')
			str[strlen(str) - 1] = 0;
		strncpy(call,str,sizeof(call));
		if (dir_partial)
		{
			el_zapcall(call);
			if (delmode) continue;
		}
		if (el_net_get_line(sock,str,sizeof(str) - 1,dir_compressed,&z) < 1)
		{
			ast_log(LOG_ERROR,"Error in directory download on %s\n",hostname);
			el_zapem();
			close(sock);
			inflateEnd(&z);
			return -1;
		}
		if (el_net_get_line(sock,str,sizeof(str) - 1,dir_compressed,&z) < 1)
		{
			ast_log(LOG_ERROR,"Error in directory download on %s\n",hostname);
			el_zapem();
			close(sock);
			inflateEnd(&z);
			return -1;
		}
		if (str[strlen(str) - 1] == '\n')
			str[strlen(str) - 1] = 0;
		strncpy(nodenum,str,sizeof(nodenum));
		if (el_net_get_line(sock,str,sizeof(str) - 1,dir_compressed,&z) < 1)
		{
			ast_log(LOG_ERROR,"Error in directory download on %s\n",hostname);
			el_zapem();
			close(sock);
			inflateEnd(&z);
			return -1;
		}
		if (str[strlen(str) - 1] == '\n')
			str[strlen(str) - 1] = 0;
		strncpy(ipaddr,str,sizeof(ipaddr));
		if (!(n % 10)) usleep(2000); /* To get to dry land */
		ast_mutex_lock(&el_db_lock);
		el_db_put(nodenum,ipaddr,call);
		ast_mutex_unlock(&el_db_lock);
		n++;
	}
	close(sock);
	inflateEnd(&z);
	pp = (dir_partial) ? "partial" : "full";
	cc = (dir_compressed) ? "compressed" : "un-compressed";
	if (option_verbose > 3) ast_verbose(VERBOSE_PREFIX_3 "Directory pgm done downloading(%s,%s), %d records\n",pp,cc,n);
	if (debug && dir_compressed) ast_log(LOG_DEBUG,"Got snapshot_id: %s\n",snapshot_id);
	return(dir_compressed);
}

static void *el_directory(void *data)
{
	int rc = 0,curdir;
	time_t	then,now;
	curdir = 0;
	time(&then);
	while (run_forever)
	{
		time(&now);
		el_sleeptime -= (now - then);		
		then = now;
		if (el_sleeptime < 0) el_sleeptime = 0;
		if (el_sleeptime)
		{
			usleep(200000);
			continue;
		}
		if (!instances[0]->elservers[curdir][0])
		{
			if (++curdir >= EL_MAX_SERVERS) curdir = 0;
			continue;
		}
		if (debug) ast_log(LOG_DEBUG, "Trying to do directory download Echolink server %s\n", instances[0]->elservers[curdir]);
		rc = do_el_directory(instances[0]->elservers[curdir]);
		if (rc < 0)
		{
			if (++curdir >= EL_MAX_SERVERS) curdir = 0;
			el_sleeptime = 20;
			continue;
		}
		if (rc == 1) el_sleeptime = 240;
		else if (rc == 0) el_sleeptime = 1800;
	}
	ast_log(LOG_WARNING, "Echolink directory thread exited.\n");
	mythread_exit(NULL);
	return NULL;
}

static void *el_register(void *data)
{
   short i = 0;
   int rc = 0;
   struct el_instance *instp = (struct el_instance *)data;
   time_t then,now;

   time(&then);
   if (debug) ast_log(LOG_DEBUG, "Echolink registration thread started on %s.\n",instp->name);
   while (run_forever)
   {
	time(&now);
	el_login_sleeptime -= (now - then);		
	then = now;
	if (el_login_sleeptime < 0) el_login_sleeptime = 0;
	if (el_login_sleeptime)
	{
		usleep(200000);
		continue;
	}
      if (i >= EL_MAX_SERVERS)
         i = 0;
 
      do {
         if (instp->elservers[i][0] != '\0')
            break;
         i++;
      } while (i < EL_MAX_SERVERS);

      if (i < EL_MAX_SERVERS) {
         if (debug) ast_log(LOG_DEBUG, "Trying to register with Echolink server %s\n", instp->elservers[i]);
         rc = sendcmd(instp->elservers[i++],instp); 
      }
      if (rc == 0)
         el_login_sleeptime = 360;
      else
         el_login_sleeptime = 20;
   }
   /* 
      Send a de-register message, but what is the point,
      Echolink deactivates this node within 6 minutes
   */
   ast_log(LOG_WARNING, "Echolink registration thread exited.\n");
   mythread_exit(NULL);
   return NULL;
}

static int do_new_call(struct el_instance *instp, struct el_pvt *p, char *call, char *name)
{
        struct el_node *el_node_key = NULL;
	struct eldb *mynode;
	char nodestr[30];
	time_t now;

	el_node_key = (struct el_node *)ast_malloc(sizeof(struct el_node));
	if (el_node_key)
	{
		memset(el_node_key,0,sizeof(struct el_node));
		strncpy(el_node_key->call,call,EL_CALL_SIZE);
		strncpy(el_node_key->ip, instp->el_node_test.ip, EL_IP_SIZE+1);
		strncpy(el_node_key->name,name,EL_NAME_SIZE); 
		
		mynode = el_db_find_ipaddr(el_node_key->ip);
		if (!mynode)
		{
			ast_log(LOG_ERROR, "Cannot find DB entry for IP addr %s\n",el_node_key->ip);
			ast_free(el_node_key); 
			return 1;
		}
		strncpy(nodestr,mynode->nodenum,sizeof(nodestr) - 1);
		el_node_key->nodenum = atoi(nodestr);
		el_node_key->countdown = instp->rtcptimeout;
		el_node_key->seqnum = 1;
		el_node_key->instp = instp;
		if (tsearch(el_node_key, &el_node_list, compare_ip))
		{
			if (option_verbose > 3) ast_verbose(VERBOSE_PREFIX_3 "new CALL=%s,ip=%s,name=%s\n",
				el_node_key->call,el_node_key->ip,
					el_node_key->name);
			if (instp->useless_flag_1)
			{
				el_node_key->p = instp->confp;
			}
			else
			{
				if (p == NULL) /* if a new inbound call */
				{
					p = el_alloc((void *)instp->name);
					if (!p)
					{
						ast_log(LOG_ERROR,"Cannot alloc el channel\n");
						return -1;
					}	
					el_node_key->p = p;
					strncpy(el_node_key->p->ip, instp->el_node_test.ip,EL_IP_SIZE+1);
					el_node_key->chan = el_new(el_node_key->p,
						AST_STATE_RINGING,el_node_key->nodenum);
					if (!el_node_key->chan)
					{
						el_destroy(el_node_key->p);
						return -1;
					}
					ast_mutex_lock(&instp->lock);
					time(&now);
					if (instp->starttime < (now - EL_APRS_START_DELAY))
						instp->aprstime = now;
					ast_mutex_unlock(&instp->lock);
				}
				else
				{
					el_node_key->p = p;
					strncpy(el_node_key->p->ip, instp->el_node_test.ip,EL_IP_SIZE+1);
					el_node_key->chan = p->owner;
					el_node_key->outbound = 1;
					ast_mutex_lock(&instp->lock);
					strcpy(instp->lastcall,mynode->callsign);
					time(&instp->lasttime);
					ast_mutex_unlock(&instp->lock);
					time(&now);
					instp->lasttime = now;
					if (instp->starttime < (now - EL_APRS_START_DELAY))
						instp->aprstime = now;
					ast_mutex_unlock(&instp->lock);
				}
			}
		}
		else
		{
			ast_log(LOG_ERROR, "tsearch() failed to add CALL=%s,ip=%s,name=%s\n",
				el_node_key->call,el_node_key->ip,el_node_key->name);
			ast_free(el_node_key); 
			return -1;
		}
	}
	else
	{
		ast_log(LOG_ERROR,"malloc() failed for new CALL=%s, ip=%s\n",
			call,instp->el_node_test.ip);
		return -1;
	}
	return 0;
}

static void *el_reader(void *data)
{
	struct el_instance *instp = (struct el_instance *)data;
	char buf[1024];
	unsigned char bye[40];
	struct sockaddr_in sin,sin1;
 	int i,j,x;
        struct el_rxqast *qpast;
        struct el_rxqel *qpel;
	struct ast_frame fr;
        socklen_t fromlen;
	ssize_t recvlen;
	time_t now,was;
	struct tm *tm;
        struct el_node **found_key = NULL;
        struct rtcp_sdes_request items;
        char call_name[128];
        char *call = NULL;
        char *name = NULL;
        char *ptr = NULL;
	fd_set fds[2];
	struct timeval tmout;
	FILE *fp;
	struct stat mystat;

	time(&instp->starttime);
	instp->aprstime = instp->starttime + EL_APRS_START_DELAY;
	if (option_verbose > 3) ast_verbose(VERBOSE_PREFIX_3 "Echolink reader thread started on %s.\n",instp->name);
	ast_mutex_lock(&instp->lock);
	while(run_forever)
	{

		time(&now);
		if (instp->aprstime <= now)
		{
			char aprsstr[512],aprscall[256],latc,lonc;
 			unsigned char sdes_packet[256];
			unsigned int u;
			float lata,lona,latb,lonb,latd,lond,lat,lon,mylat,mylon;
			int sdes_length;

			instp->aprstime = now + EL_APRS_INTERVAL;
			ast_mutex_lock(&el_count_lock);
			count_instp = instp;
			count_n = count_outbound_n = 0;
			twalk(el_node_list, count_users);
			i = count_n;
			j = count_outbound_n;
			ast_mutex_unlock(&el_count_lock);
			tm = gmtime(&now);
			if (!j) /* if no outbound users */
			{
				snprintf(instp->login_display,EL_NAME_SIZE + EL_CALL_SIZE,
					"%s [%d/%d]",instp->myqth,i,instp->maxstns);
				snprintf(instp->aprs_display,EL_APRS_SIZE,
					" On @ %02d%02d [%d/%d]",tm->tm_hour,tm->tm_min,i,instp->maxstns);
			}
			else
			{
				snprintf(instp->login_display,EL_NAME_SIZE + EL_CALL_SIZE,
					"In Conference %s",instp->lastcall);
				snprintf(instp->aprs_display,EL_APRS_SIZE,
					"=N%s @ %02d%02d",instp->lastcall,tm->tm_hour,tm->tm_min);
			}				
			mylat = instp->lat;
			mylon = instp->lon;
			fp = fopen(GPSFILE,"r");
			if (fp && (fstat(fileno(fp),&mystat) != -1) &&
				(mystat.st_size < 100))
			{
				if (fscanf(fp,"%u %f%c %f%c",&u,&lat,&latc,&lon,&lonc) == 5)
				{
					was = (time_t) u;
					if ((was + GPS_VALID_SECS) >= now)
					{
						mylat = floor(lat / 100.0);
						mylat += (lat - (mylat * 100)) / 60.0;
						mylon = floor(lon / 100.0);
						mylon += (lon - (mylon * 100)) / 60.0;
						if (latc == 'S') mylat = -mylat;
						if (lonc == 'W') mylon = -mylon;
					}						
				}
				fclose(fp);
			}
			latc = (mylat >= 0.0) ? 'N' : 'S';
			lonc = (mylon >= 0.0) ? 'E' : 'W';
			lata = fabs(mylat);
			lona = fabs(mylon);
			latb = (lata - floor(lata)) * 60;
                        latd = (latb - floor(latb)) * 100 + 0.5;
                        lonb = (lona - floor(lona)) * 60;
                        lond = (lonb - floor(lonb)) * 100 + 0.5;
			sprintf(aprsstr,")EL-%-6.6s!%02d%02d.%02d%cE%03d%02d.%02d%c0PHG%d%d%d%d/%06d/%03d%s",instp->mycall,
				(int)lata,(int)latb,(int)latd,latc,
				(int)lona,(int)lonb,(int)lond,lonc,
				instp->power,instp->height,instp->gain,instp->dir,
				(int)((instp->freq * 1000) + 0.5),(int)(instp->tone + 0.05),instp->aprs_display);

			if (debug) ast_log(LOG_DEBUG,"aprs out: %s\n",aprsstr);
			sprintf(aprscall,"%s/%s",instp->mycall,instp->mycall);
			memset(sdes_packet,0,sizeof(sdes_packet));
			sdes_length = rtcp_make_el_sdes(sdes_packet,sizeof(sdes_packet),aprscall,aprsstr);
			sendto(instp->ctrl_sock, sdes_packet, sdes_length, 
		             0,(struct sockaddr *)&sin_aprs,sizeof(sin_aprs));
			el_sleeptime = 0;
			el_login_sleeptime = 0;
		}		
		ast_mutex_unlock(&instp->lock);
		FD_ZERO(fds);
		FD_SET(instp->audio_sock,fds);
		FD_SET(instp->ctrl_sock,fds);
		x = instp->audio_sock;
		if (instp->ctrl_sock > x) x = instp->ctrl_sock;
		tmout.tv_sec = 0;
		tmout.tv_usec = 50000;
		i = select(x + 1,fds,NULL,NULL,&tmout);
		if (i == 0) 
		{
			ast_mutex_lock(&instp->lock);
			continue;
		}
		if (i < 0)
		{
			ast_log(LOG_ERROR,"Error in select()\n");
			mythread_exit(NULL);
		}
		ast_mutex_lock(&instp->lock);
		if (FD_ISSET(instp->ctrl_sock,fds)) /* if a ctrl packet */
		{
			fromlen = sizeof(struct sockaddr_in);
			recvlen = recvfrom(instp->ctrl_sock,buf,sizeof(buf) - 1,
				0,(struct sockaddr *)&sin,&fromlen);
			if (recvlen > 0) 
			{
			    buf[recvlen] = '\0';
#ifdef  OLD_ASTERISK
			    ast_inet_ntoa(instp->el_node_test.ip,EL_IP_SIZE,sin.sin_addr);
#else
			    strncpy(instp->el_node_test.ip,ast_inet_ntoa(sin.sin_addr),
				EL_IP_SIZE);
#endif
			    if (is_rtcp_sdes((unsigned char *)buf,recvlen))
			    {
				call_name[0] = '\0';
				items.nitems = 1;
				items.item[0].r_item = 2;
				items.item[0].r_text = NULL;
				parse_sdes((unsigned char *)buf,&items);
				if (items.item[0].r_text != NULL)
					copy_sdes_item(items.item[0].r_text,call_name, 127);
				if (call_name[0] != '\0')
				{
					call = call_name;
					ptr = strchr(call_name, (int)' ');
					name = "UNKNOWN";
					if (ptr)
					{
						*ptr = '\0';
						name = ptr + 1;      
					}
					found_key = (struct el_node **)tfind(&instp->el_node_test,
						&el_node_list, compare_ip); 
					if (found_key)
					{
						if (!(*found_key)->p->firstheard)
						{
							(*found_key)->p->firstheard = 1;
							memset(&fr,0,sizeof(fr));
							fr.datalen = 0;
							fr.samples = 0;
							fr.frametype = AST_FRAME_CONTROL;
							fr.subclass = AST_CONTROL_ANSWER;
							fr.data =  0;
							fr.src = type;
							fr.offset = 0;
							fr.mallocd=0;
							fr.delivery.tv_sec = 0;
							fr.delivery.tv_usec = 0;
							ast_queue_frame((*found_key)->chan,&fr);
							if (debug) ast_log(LOG_DEBUG,"Channel %s answering\n",
								(*found_key)->chan->name);
						}
						(*found_key)->countdown = instp->rtcptimeout;
						/* different callsigns behind a NAT router, running -L, -R, ... */
						if (strncmp((*found_key)->call,call,EL_CALL_SIZE) != 0)
						{
							if (option_verbose > 3) ast_verbose(VERBOSE_PREFIX_3 "Call changed from %s to %s\n",
								(*found_key)->call,call);
							strncpy((*found_key)->call,strndup(call, EL_CALL_SIZE),EL_CALL_SIZE);
						}
						if (strncmp((*found_key)->name, name, EL_NAME_SIZE) != 0) 
						{
							if (option_verbose > 3) ast_verbose(VERBOSE_PREFIX_3 "Name changed from %s to %s\n",
								(*found_key)->name,name);
							strncpy((*found_key)->name,name,EL_NAME_SIZE);
						}
					}
					else /* otherwise its a new request */
					{
						i = 0;  /* default authorized */
						if (instp->ndenylist)
						{
							for (x = 0; x < instp->ndenylist; x++)
							{
								if (!fnmatch(instp->denylist[x],call,FNM_CASEFOLD)) 
								{
									i = 1;
									break;
								}
							}
						}
						else
						{
							/* if permit list specified, default is not to authorize */
							if (instp->npermitlist) i = 1;
						}
						if (instp->npermitlist)
						{
							for (x = 0; x < instp->npermitlist; x++)
							{
								if (!fnmatch(instp->permitlist[x],call,FNM_CASEFOLD)) 
								{
									i = 0;
									break;
								}
							}
						}
						if (!i) /* if authorized */
						{
							i = do_new_call(instp,NULL,call,name);
							if (i < 0)
							{
								ast_mutex_unlock(&instp->lock);
								mythread_exit(NULL);
							}
						}
						if (i) /* if not authorized */
						{
							/* first, see if we have one that is ours and not abandoned */
							for(x = 0; x < MAXPENDING; x++)
							{
								if (strcmp(instp->pending[x].fromip,
									instp->el_node_test.ip)) continue;
								if (ast_tvdiff_ms(ast_tvnow(),
									instp->pending[x].reqtime) < AUTH_ABANDONED_MS) break;
							}
							if (x < MAXPENDING)
							{
								/* if its time, send un-auth */
								if (ast_tvdiff_ms(ast_tvnow(),
									instp->pending[x].reqtime) >= AUTH_RETRY_MS)
								{
									if (debug) ast_log(LOG_DEBUG,"Sent bye to IP address %s\n",
										instp->el_node_test.ip);
									j = rtcp_make_bye(bye,"UN-AUTHORIZED");
									sin1.sin_family = AF_INET;
									sin1.sin_addr.s_addr = inet_addr(instp->el_node_test.ip);
									sin1.sin_port = htons(instp->ctrl_port);
									for (i = 0; i < 20; i++)
									{
										sendto(instp->ctrl_sock, bye, j,
											0,(struct sockaddr *)&sin1,sizeof(sin1));
									}
									instp->pending[x].fromip[0] = 0;
								}
								time(&now);
								if (instp->starttime < (now - EL_APRS_START_DELAY))
									instp->aprstime = now;
							}
							else /* find empty one */
							{
								for(x = 0; x < MAXPENDING; x++)
								{
									if (!instp->pending[x].fromip[0]) break;
									if (ast_tvdiff_ms(ast_tvnow(),
										instp->pending[x].reqtime) >= AUTH_ABANDONED_MS) break;
								}
								if (x < MAXPENDING) /* we found one */
								{
									strcpy(instp->pending[x].fromip,
										instp->el_node_test.ip);
									instp->pending[x].reqtime = ast_tvnow();
									time(&now);
									if (instp->starttime < (now - EL_APRS_START_DELAY))
										instp->aprstime = now;
									else
									{
										el_sleeptime = 0;
										el_login_sleeptime = 0;
									}
								}
								else
								{
									ast_log(LOG_ERROR,"Cannot find open pending echolink request slot for IP %s\n",
										instp->el_node_test.ip);
								}
							}
						}
					        twalk(el_node_list, send_info); 
					}
				}
			    }
			    else
			    {
				if (is_rtcp_bye((unsigned char *)buf,recvlen))
				{
					if (find_delete(&instp->el_node_test))
						if (option_verbose > 3) ast_verbose(VERBOSE_PREFIX_3 "disconnect from ip=%s\n",instp->el_node_test.ip);
				} 
			    }
			}
		}
		if (FD_ISSET(instp->audio_sock,fds)) /* if an audio packet */
		{
			fromlen = sizeof(struct sockaddr_in);
			recvlen = recvfrom(instp->audio_sock,buf,sizeof(buf) - 1,0,
				(struct sockaddr *)&sin,&fromlen);
			if (recvlen > 0)
			{
				buf[recvlen] = '\0';
#ifdef  OLD_ASTERISK
				ast_inet_ntoa(instp->el_node_test.ip,EL_IP_SIZE,sin.sin_addr);
#else
				strncpy(instp->el_node_test.ip,ast_inet_ntoa(sin.sin_addr),EL_IP_SIZE);
#endif
				if (buf[0] == 0x6f)
				{
					process_cmd(buf,instp->el_node_test.ip,instp);
				}
				else
				{
					found_key = (struct el_node **)tfind(&instp->el_node_test, &el_node_list, compare_ip);
					if (found_key)
					{
						struct el_pvt *p = (*found_key)->p;

						if (!(*found_key)->p->firstheard)
						{
							(*found_key)->p->firstheard = 1;
							memset(&fr,0,sizeof(fr));
							fr.datalen = 0;
							fr.samples = 0;
							fr.frametype = AST_FRAME_CONTROL;
							fr.subclass = AST_CONTROL_ANSWER;
							fr.data =  0;
							fr.src = type;
							fr.offset = 0;
							fr.mallocd=0;
							fr.delivery.tv_sec = 0;
							fr.delivery.tv_usec = 0;
							ast_queue_frame((*found_key)->chan,&fr);
							if (option_verbose > 3) ast_verbose(VERBOSE_PREFIX_3 "Channel %s answering\n",
								(*found_key)->chan->name);
						}
						(*found_key)->countdown = instp->rtcptimeout;
						if (recvlen == sizeof(struct gsmVoice_t))
						{
							if ((((struct gsmVoice_t *)buf)->version == 3) &&
								(((struct gsmVoice_t *)buf)->payt == 3))
							{
								/* break them up for Asterisk */
								for (i = 0; i < BLOCKING_FACTOR; i++)
								{
									qpast = ast_malloc(sizeof(struct el_rxqast));
									if (!qpast)
									{
										ast_log(LOG_ERROR,"Cannot malloc for qpast\n");
										ast_mutex_unlock(&instp->lock);
										mythread_exit(NULL);
									}
									memcpy(qpast->buf,((struct gsmVoice_t *)buf)->data +
										(GSM_FRAME_SIZE * i),GSM_FRAME_SIZE);
									insque((struct qelem *)qpast,(struct qelem *)
										p->rxqast.qe_back);
								}
							}
							if (!instp->useless_flag_1) continue;
							/* need complete packet and IP address for Echolink */
							qpel = ast_malloc(sizeof(struct el_rxqel));
							if (!qpel)
							{
								ast_log(LOG_ERROR,"Cannot malloc for qpel\n");
							}
							else
							{
								memcpy(qpel->buf,((struct gsmVoice_t *)buf)->data,
									BLOCKING_FACTOR * GSM_FRAME_SIZE);
								strncpy(qpel->fromip,strndup(instp->el_node_test.ip, EL_IP_SIZE),EL_IP_SIZE);
								insque((struct qelem *)qpel,(struct qelem *)
									p->rxqel.qe_back);
							}
						}
					}   
				}
			}
		}
	}
	ast_mutex_unlock(&instp->lock);
	if (option_verbose > 3) ast_verbose(VERBOSE_PREFIX_3 "Echolink read thread exited.\n");
	mythread_exit(NULL);
	return NULL;
}

static int store_config(struct ast_config *cfg,char *ctg)
{
char	*val;
struct hostent *ahp;
struct ast_hostent ah;
struct	el_instance *instp;
struct sockaddr_in si_me;
pthread_attr_t attr;

	if (ninstances >= EL_MAX_INSTANCES)
	{
		ast_log(LOG_ERROR,"Too many instances specified\n");
		return -1;
	}

	instp = ast_malloc(sizeof(struct el_instance));
	if (!instp)
	{
		ast_log(LOG_ERROR,"Cannot malloc\n");
		return -1;
	}
	memset(instp,0,sizeof(struct el_instance));

	ast_mutex_init(&instp->lock);
	instp->audio_sock = -1;
	instp->ctrl_sock = -1;
	instp->fdr = -1;

        val = (char *) ast_variable_retrieve(cfg,ctg,"ipaddr"); 
        if (val) 
	{
		strncpy(instp->ipaddr,val,EL_IP_SIZE);
	}
        else
	{
		strcpy(instp->ipaddr,"0.0.0.0");
	}
        
        val = (char *) ast_variable_retrieve(cfg,ctg,"port"); 
        if (val)
	{
	   strncpy(instp->port,val,EL_IP_SIZE);
	}
        else
	{
	   strcpy(instp->port,"5198");
	}
        val = (char *) ast_variable_retrieve(cfg,ctg,"maxstns"); 
        if (!val)
           instp->maxstns = 50;
        else
           instp->maxstns = atoi(val);
        
        val = (char *) ast_variable_retrieve(cfg,ctg,"rtcptimeout");
        if (!val)
           instp->rtcptimeout = 15;
        else
           instp->rtcptimeout = atoi(val);

        val = (char *) ast_variable_retrieve(cfg,ctg,"node");
        if (!val)
           instp->mynode = 0;
        else
           instp->mynode = atol(val);

        val = (char *) ast_variable_retrieve(cfg,ctg,"astnode"); 
        if (val)
	{
	   strncpy(instp->astnode,val,EL_NAME_SIZE);
	}
        else
	{
	   strcpy(instp->astnode,"1999");
	}
        val = (char *) ast_variable_retrieve(cfg,ctg,"context"); 
        if (val)
	{
	   strncpy(instp->context,val,EL_NAME_SIZE);
	}
        else
	{
	   strcpy(instp->context,"echolink-in");
	}
        val = (char *) ast_variable_retrieve(cfg,ctg,"call");
        if (!val)
           strncpy(instp->mycall,"INVALID",EL_CALL_SIZE);
        else
           strncpy(instp->mycall,val,EL_CALL_SIZE);

        if (strcmp(instp->mycall,"INVALID") == 0)
	{
		ast_log(LOG_ERROR,"INVALID Echolink call");
		return -1;
	}
        val = (char *) ast_variable_retrieve(cfg,ctg,"name");
        if (!val)
           strncpy(instp->myname,instp->mycall,EL_NAME_SIZE);
        else
           strncpy(instp->myname,val,EL_NAME_SIZE);

        val = (char *) ast_variable_retrieve(cfg,ctg,"recfile");
        if (!val)
           strncpy(instp->fdr_file, "/tmp/echolink_recorded.gsm", FILENAME_MAX - 1);
        else
           strncpy(instp->fdr_file,val,FILENAME_MAX - 1);

        val = (char *) ast_variable_retrieve(cfg,ctg,"pwd");
        if (!val)
           strncpy(instp->mypwd,"INVALID", EL_PWD_SIZE);
        else
           strncpy(instp->mypwd,val,EL_PWD_SIZE);

        val = (char *) ast_variable_retrieve(cfg,ctg,"qth");
        if (!val)
           strncpy(instp->myqth,"INVALID", EL_QTH_SIZE);
        else
           strncpy(instp->myqth,val,EL_QTH_SIZE);

        val = (char *) ast_variable_retrieve(cfg,ctg,"email");
        if (!val)
           strncpy(instp->myemail, "INVALID", EL_EMAIL_SIZE);
        else
           strncpy(instp->myemail,val,EL_EMAIL_SIZE);

	instp->useless_flag_1 = 0;

        val = (char *) ast_variable_retrieve(cfg,ctg,"server1");
        if (!val)
           instp->elservers[0][0] = '\0';
        else
           strncpy(instp->elservers[0],val,EL_SERVERNAME_SIZE); 

        val = (char *) ast_variable_retrieve(cfg,ctg,"server2");
        if (!val)
           instp->elservers[1][0] = '\0';
        else
           strncpy(instp->elservers[1],val,EL_SERVERNAME_SIZE);

        val = (char *) ast_variable_retrieve(cfg,ctg,"server3");
        if (!val)
           instp->elservers[2][0] = '\0';
        else
           strncpy(instp->elservers[2],val,EL_SERVERNAME_SIZE);

        val = (char *) ast_variable_retrieve(cfg,ctg,"deny"); 
	if (val) instp->ndenylist = finddelim(strdup(val),instp->denylist,EL_MAX_CALL_LIST);

        val = (char *) ast_variable_retrieve(cfg,ctg,"permit"); 
	if (val) instp->npermitlist = finddelim(strdup(val),instp->permitlist,EL_MAX_CALL_LIST);

        val = (char *) ast_variable_retrieve(cfg,ctg,"lat"); 
	if (val) instp->lat = strtof(val,NULL); else instp->lat = 0.0;
	
        val = (char *) ast_variable_retrieve(cfg,ctg,"lon"); 
	if (val) instp->lon = strtof(val,NULL); else instp->lon = 0.0;
	
        val = (char *) ast_variable_retrieve(cfg,ctg,"freq"); 
	if (val) instp->freq = strtof(val,NULL); else instp->freq = 0.0;
	
        val = (char *) ast_variable_retrieve(cfg,ctg,"tone"); 
	if (val) instp->tone = strtof(val,NULL); else instp->tone = 0.0;
	
        val = (char *) ast_variable_retrieve(cfg,ctg,"power"); 
	if (val) instp->power = (char)strtol(val,NULL,0); else instp->power = 0;

        val = (char *) ast_variable_retrieve(cfg,ctg,"height"); 
	if (val) instp->height = (char)strtol(val,NULL,0); else instp->height = 0;

        val = (char *) ast_variable_retrieve(cfg,ctg,"gain"); 
	if (val) instp->gain = (char)strtol(val,NULL,0); else instp->gain = 0;

        val = (char *) ast_variable_retrieve(cfg,ctg,"dir"); 
	if (val) instp->dir = (char)strtol(val,NULL,0); else instp->dir = 0;

	instp->audio_sock = -1;
	instp->ctrl_sock = -1;

        if ((strncmp(instp->mypwd,"INVALID",EL_PWD_SIZE) == 0) ||
            (strncmp(instp->mycall,"INVALID",EL_CALL_SIZE) == 0))
	{
           ast_log(LOG_ERROR,"Your Echolink call or password is not right\n");
	   return -1;
	}
        if ((instp->elservers[0][0] == '\0') || (instp->elservers[1][0] == '\0') || (instp->elservers[2][0] == '\0'))
	{
           ast_log(LOG_ERROR, "One of the Echolink servers missing\n");
	   return -1;
	}
	if ((instp->audio_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
	{
		ast_log(LOG_WARNING, 
                        "Unable to create new socket for echolink audio connection\n");
		return -1;
	}
	if ((instp->ctrl_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
	{
		ast_log(LOG_WARNING, 
                        "Unable to create new socket for echolink control connection\n");
                close(instp->audio_sock);
                instp->audio_sock = -1;
		return -1;
	}
	memset((char *) &si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
        if (strcmp(instp->ipaddr,"0.0.0.0") == 0)
		si_me.sin_addr.s_addr = htonl(INADDR_ANY);
        else
		si_me.sin_addr.s_addr = inet_addr(instp->ipaddr);
        instp->audio_port = atoi(instp->port);
	si_me.sin_port = htons(instp->audio_port);               
	if (bind(instp->audio_sock, &si_me, sizeof(si_me)) == -1) 
	{
		ast_log(LOG_WARNING, "Unable to bind port for echolink audio connection\n");
                close(instp->ctrl_sock); instp->ctrl_sock = -1;
		close(instp->audio_sock); instp->audio_sock = -1;
		return -1;
	}
        instp->ctrl_port = instp->audio_port + 1;
	si_me.sin_port = htons(instp->ctrl_port);
	if (bind(instp->ctrl_sock, &si_me, sizeof(si_me))==-1)
	{
		ast_log(LOG_WARNING, "Unable to bind port for echolink control connection\n");
                close(instp->ctrl_sock); instp->ctrl_sock = -1;
                close(instp->audio_sock); instp->audio_sock = -1;
		return -1;
	}
        fcntl(instp->audio_sock,F_SETFL,O_NONBLOCK);
        fcntl(instp->ctrl_sock,F_SETFL,O_NONBLOCK);
	strncpy(instp->name,ctg,EL_NAME_SIZE);
	sin_aprs.sin_family = AF_INET;
	sin_aprs.sin_port = htons(5199);
	ahp = ast_gethostbyname(EL_APRS_SERVER,&ah);
	if (!ahp) {
		ast_log(LOG_ERROR, "Unable to resolve echolink APRS server IP address\n");
                close(instp->ctrl_sock); instp->ctrl_sock = -1;
                close(instp->audio_sock); instp->audio_sock = -1;
		return -1;
	}
	memcpy(&sin_aprs.sin_addr.s_addr,ahp->h_addr,sizeof(in_addr_t));
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        ast_pthread_create(&el_register_thread,&attr,el_register,(void *)instp);
        ast_pthread_create(&instp->el_reader_thread,&attr,el_reader,(void *)instp);
	instances[ninstances++] = instp;


	if (option_verbose > 3)
	{
	        ast_verbose(VERBOSE_PREFIX_3  "Echolink/%s listening on %s port %s\n", instp->name, instp->ipaddr,instp->port);
	        ast_verbose(VERBOSE_PREFIX_3  "Echolink/%s node capacity set to %d node(s)\n", instp->name, instp->maxstns);
	        ast_verbose(VERBOSE_PREFIX_3  "Echolink/%s heartbeat timeout set to %d heartbeats\n", instp->name,instp->rtcptimeout);
	        ast_verbose(VERBOSE_PREFIX_3  "Echolink/%s node set to %u\n", instp->name,instp->mynode);
	        ast_verbose(VERBOSE_PREFIX_3  "Echolink/%s call set to %s\n",instp->name,instp->mycall);
	        ast_verbose(VERBOSE_PREFIX_3  "Echolink/%s name set to %s\n",instp->name,instp->myname);
	        ast_verbose(VERBOSE_PREFIX_3  "Echolink/%s file for recording set to %s\n",instp->name, instp->fdr_file);
	        ast_verbose(VERBOSE_PREFIX_3  "Echolink/%s  qth set to %s\n",instp->name, instp->myqth);
	        ast_verbose(VERBOSE_PREFIX_3  "Echolink/%s emailID set to %s\n", instp->name,instp->myemail);
	}
	return 0;
}
#ifndef	OLD_ASTERISK
static
#endif
int load_module(void)
{
	struct ast_config *cfg = NULL;
        char *ctg = NULL;
	pthread_attr_t attr;

#ifdef  NEW_ASTERISK
        struct ast_flags zeroflag = {0};
#endif

#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_ERROR, "Unable to load config %s\n", config);
                return AST_MODULE_LOAD_DECLINE;
        }

        while ( (ctg = ast_category_browse(cfg, ctg)) != NULL)
	{
		if (ctg == NULL) continue;
                if (store_config(cfg, ctg) < 0) return AST_MODULE_LOAD_DECLINE;
	}
        ast_config_destroy(cfg);
        cfg = NULL; 
	if (option_verbose > 3) ast_verbose(VERBOSE_PREFIX_3 "Total of %d Echolink instances found\n",ninstances);
	if (ninstances < 1)
	{
		ast_log(LOG_ERROR,"Cannot run echolink with no instances\n");
		return AST_MODULE_LOAD_DECLINE;
	}

        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        ast_pthread_create(&el_directory_thread,&attr,el_directory,NULL);
#ifdef	NEW_ASTERISK
	ast_cli_register_multiple(el_cli,sizeof(el_cli) / 
		sizeof(struct ast_cli_entry));
#else
	/* Register cli extensions */
	ast_cli_register(&cli_debug);
	ast_cli_register(&cli_dbdump);
	ast_cli_register(&cli_dbget);
#endif
	/* Make sure we can register our channel type */
	if (ast_channel_register(&el_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
                return AST_MODULE_LOAD_DECLINE;
	}
	nullfd = open("/dev/null",O_RDWR);
	return 0;
}

#ifdef	OLD_ASTERISK
char *description()
{
	return (char *)el_tech.description;
}

int usecount()
{
	return usecnt;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
#else
AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "echolink channel driver");
#endif
