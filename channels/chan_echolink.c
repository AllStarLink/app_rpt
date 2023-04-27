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
 * Changes:
 * --------
 * 02/5/23 - Danny Lloyd, KB4MDD <kb4mdd@arrl.net>
 * corrected memory leaks
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
	<support_level>extended</support_level>
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

/*
 * Echolink protocol information
 * RTP voice data is passed on port 5198 UDP.
 * RTCP data is passed on port 5199 UDP.
 * The directory server information is downloaded on port 5200 TCP.
 *
 * The RTP channel contains voice and text messages.  Text messages begin with 0x6f.
 * We send a text message with our connections each time we get or release a
 * connection.  The format is:
 *	oNDATA0x0dMESSAGE0x0d
 *
 * The RTCP channel is used for connections requests to and from our module.
 * The packets are in RTCP format using SDES.
 * There are two types of packets: a user information packet, which is considered 
 * the connection request, and a bye packet, that is the disconnection request.
 *
 * The directory server requires the user login information / registration.
 * To login, send the string:
 * 	lCCC0xac0xacPPP0x0dONLINEVVV(HH:DD)0x0dLLL0x0dEEE0x0d
 *  where 'l' is a literal
 *  CCC is the callsign
 *  0xac is the character 0xac
 *  0xac is the character 0xac
 *  PPP is the password
 *  0x0d is the character 0x0d
 *  ONLINE is a literal
 *  VVV is the echolink version we are sending '1.00B'
 *  ( is a literal
 *  HH is the hours since midnight
 *	DD is the day of the month
 *	) is a literal
 *	0x0d is the character 0x0d
 *	LLL is the login name or QTH
 *	0x0d is the character 0x0d
 *	EEE is the Email address
 *	0x0d is the character 0x0d
 *
 * If the login was successful, the directory server will respond with 'ok' any other 
 * response should be considered failure or not authorized.  The connection can be closed.
 *
 * Registration with the directory server is required once every 360 seconds. 
 *
 * To request a directory list, register as described above and start a new TCP connection.
 * The server can return the directory list as a full list or differential.  The directory
 * data can be compressed or uncompressed.
 * To request a differential compressed directory send:
 *	FSSS0x0d
 *	Where F is a literal
 *	SSS is the last snap shot id received.  This will initally be a zero length string.
 *
 * The first 4 bytes of the returned data determine if the download is full or compressed.
 * @@@ indicates an uncompressed list, while DDD indicates an uncompressed differential.
 * Anything else will indicate that the stream is compressed and will need to be deflated.
 * 
 * The deflated stream will start with @@@ or DDD as described above.  Each line is terminated 
 * with 0x0a.
 * Here is an example:
 *  DDD
 *  482:687993635
 *  E25HL-L
 *  .                          [BUSY 02:18]
 *  541765
 *  137.226.114.63
 *  VE3ABZ-L
 *  .                          [ON 14:09]
 *  549404
 *  148.170.130.43
 *  K1JTV
 *  .       
 * The line following the DDD has two formats - for uncompressed files, there is a single number
 * that represents the number of lines.  For compressed formats, the first number is the number of
 * lines, a colon, followed by the snapshot id. 
 * The remaining data repeats for each registration entry. 
 * An entry is composed of:
 *	Callsign
 *	Location [Status xx:xx]
 *	Node number
 *	IP address
 *
 * A line that starts with +++ indicates the end of entries.  When first encountered, the 
 * entries following +++ are to be deleted.
*/

#include "asterisk.h"

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
#include "asterisk/format_cache.h"

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
#define EL_MAX_SERVERS 4
#define EL_SERVERNAME_SIZE 63
#define	EL_MAX_INSTANCES 100
#define	EL_MAX_CALL_LIST 60
#define	EL_APRS_SERVER "aprs.echolink.org"
#define	EL_APRS_INTERVAL 600
#define	EL_APRS_START_DELAY 10

#define EL_QUERY_IPADDR 1
#define EL_QUERY_CALLSIGN 2

#define	GPSFILE "/tmp/gps.dat"
#define	GPS_VALID_SECS 60

#define	ELDB_NODENUMLEN 8
#define	ELDB_CALLSIGNLEN 20
#define	ELDB_IPADDRLEN 18

#define	DELIMCHR ','
#define	QUOTECHR 34
/* 
 * If you want to compile/link this code
 * on "BIG-ENDIAN" platforms, then
 * use this: #define RTP_BIG_ENDIAN
 * Have only tested this code on "little-endian"
 * platforms running Linux.
*/
static const char tdesc[] = "Echolink channel driver";
static char type[] = "echolink";
static char snapshot_id[50] = { '0', 0 };

static int el_net_get_index = 0;
static int el_net_get_nread = 0;
static int nodeoutfd = -1;

struct sockaddr_in sin_aprs;

/*!
 * \brief Echolink audio packet header.
 * This is the standard RTP packet format.
 */
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

/* forward definitions */
struct el_instance;
struct el_pvt;

/*!
 * \brief Echolink connected node struct.
 * These are stored internally in a binary tree.
 */
struct el_node {
	char ip[EL_IP_SIZE + 1];
	char call[EL_CALL_SIZE + 1];
	char name[EL_NAME_SIZE + 1];
	unsigned int nodenum;		/* not used yet */
	short countdown;
	uint16_t seqnum;
	struct el_instance *instp;
	struct el_pvt *p;
	struct ast_channel *chan;
	char outbound;
};

/*!
 * \brief Pending connections struct.
 * This holds the incoming connection that is not authorized.
 */
struct el_pending {
	char fromip[EL_IP_SIZE + 1];
	struct timeval reqtime;
};

/*!
 * \brief Echolink instance struct.
 */
struct el_instance {
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
};

/*!
 * \brief Echolink receive queue struct from asterisk.
 */
struct el_rxqast {
	struct el_rxqast *qe_forw;
	struct el_rxqast *qe_back;
	char buf[GSM_FRAME_SIZE];
};

/*!
 * \brief Echolink receive queue struct from echolink.
 */
struct el_rxqel {
	struct el_rxqel *qe_forw;
	struct el_rxqel *qe_back;
	char buf[BLOCKING_FACTOR * GSM_FRAME_SIZE];
	char fromip[EL_IP_SIZE + 1];
};

/*!
 * \brief Echolink private information.
 * This is stored in the asterisk channel private technology for reference.
 */
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

/*!
 * \brief RTP Control Packet SDES request item.
 */
struct rtcp_sdes_request_item {
	unsigned char r_item;
	char *r_text;
};

/*!
 * \brief RTP Control Packet SDES request items.
 */
struct rtcp_sdes_request {
	int nitems;
	unsigned char ssrc[4];
	struct rtcp_sdes_request_item item[10];
};

/*!
 * \brief RTCP Control Packet common header word.
 */
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

/*!
 * \brief RTP Control Packet SDES item detail.
 */
struct rtcp_sdes_item_t {
	uint8_t type;
	uint8_t length;
	char data[1];
};

/*!
 * \brief RTP Control Packet for SDES.
 */
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

/*!
 * \brief Echolink internal directory database entry.
 */
struct eldb {
	char nodenum[ELDB_NODENUMLEN];
	char callsign[ELDB_CALLSIGNLEN];
	char ipaddr[ELDB_IPADDRLEN];
};

AST_MUTEX_DEFINE_STATIC(el_db_lock);
AST_MUTEX_DEFINE_STATIC(el_count_lock);

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
static pthread_t el_register_thread = 0;
static pthread_t el_directory_thread = 0;
static int run_forever = 1;
static int killing = 0;
static int nullfd = -1;
static int el_sleeptime = 0;
static int el_login_sleeptime = 0;

static char *config = "echolink.conf";

static struct ast_channel *el_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static int el_call(struct ast_channel *ast, const char *dest, int timeout);
static int el_hangup(struct ast_channel *ast);
static struct ast_frame *el_xread(struct ast_channel *ast);
static int el_xwrite(struct ast_channel *ast, struct ast_frame *frame);
static int el_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen);
static int el_digit_begin(struct ast_channel *c, char digit);
static int el_digit_end(struct ast_channel *c, char digit, unsigned int duratiion);
static int el_text(struct ast_channel *c, const char *text);
static int el_queryoption(struct ast_channel *chan, int option, void *data, int *datalen);

static int rtcp_make_sdes(unsigned char *pkt, int pktLen, char *call, char *name, char *astnode);
static int rtcp_make_el_sdes(unsigned char *pkt, int pktLen, char *cname, char *loc);
static int rtcp_make_bye(unsigned char *p, char *reason);
static void parse_sdes(unsigned char *packet, struct rtcp_sdes_request *r);
static void copy_sdes_item(char *source, char *dest, int destlen);
static int is_rtcp_bye(unsigned char *p, int len);
static int is_rtcp_sdes(unsigned char *p, int len);
 /* remove binary tree functions if Asterisk has similar functionality */
static int compare_ip(const void *pa, const void *pb);
static void send_heartbeat(const void *nodep, const VISIT which, const int depth);
static void send_info(const void *nodep, const VISIT which, const int depth);
static void print_users(const void *nodep, const VISIT which, const int depth);
static void count_users(const void *nodep, const VISIT which, const int depth);
static void free_node(void *nodep);
static void process_cmd(char *buf, char *fromip, struct el_instance *instp);
static int find_delete(struct el_node *key);
static int sendcmd(char *server, struct el_instance *instp);
static int do_new_call(struct el_instance *instp, struct el_pvt *p, char *call, char *name);

/* remove writen if Asterisk has similar functionality */
static int writen(int fd, char *ptr, int nbytes);

/*!
 * \brief Asterisk channel technology struct.
 * This tells Asterisk the functions to call when
 * it needs to interact with our module.
 */
static struct ast_channel_tech el_tech = {
	.type = type,
	.description = tdesc,
	.requester = el_request,
	.call = el_call,
	.hangup = el_hangup,
	.read = el_xread,
	.write = el_xwrite,
	.indicate = el_indicate,
	.send_text = el_text,
	.send_digit_begin = el_digit_begin,
	.send_digit_end = el_digit_end,
	.queryoption = el_queryoption,
};

/*
* CLI extensions
*/

static int el_do_dbdump(int fd, int argc, const char *const *argv);
static int el_do_dbget(int fd, int argc, const char *const *argv);

static char dbdump_usage[] = "Usage: echolink dbdump [nodename|callsign|ipaddr]\n" "       Dumps entire echolink db\n";

static char dbget_usage[] =
	"Usage: echolink dbget <nodename|callsign|ipaddr> <lookup-data>\n" "       Looks up echolink db entry\n";

#define mythread_exit(nothing) __mythread_exit(nothing, __LINE__)

/*!
 * \brief Cleans up the application when a serious internal error occurs.
 * It forces app_rpt to restart.
 * \param nothing	Pointer to NULL (NULL is passed from all routines)
 * \param line		Line where the exit originated
 */
static void __mythread_exit(void *nothing, int line)
{
	int i;

	if (killing)
		pthread_exit(NULL);
	killing = 1;
	run_forever = 0;
	for (i = 0; i < ninstances; i++) {
		if (instances[i]->el_reader_thread)
			pthread_kill(instances[i]->el_reader_thread, SIGTERM);
	}
	if (el_register_thread)
		pthread_kill(el_register_thread, SIGTERM);
	if (el_directory_thread)
		pthread_kill(el_directory_thread, SIGTERM);
	ast_log(LOG_ERROR, "Exiting chan_echolink, FATAL ERROR at line %d!!\n", line);
	ast_cli_command(nullfd, "rpt restart");
	pthread_exit(NULL);
}

/*!
 * \brief Break up a delimited string into a table of substrings.
 * Uses defines for the delimiters: QUOTECHR and DELIMCHR.
 * \param str		Pointer to string to process (it will be modified).
 * \param strp		Pointer to a list of substrings created, NULL will be placed at the end of the list.
 * \param limit		Maximum number of substrings to process.
 */

static int finddelim(char *str, char *strp[], int limit)
{
	int i, l, inquo;

	inquo = 0;
	i = 0;
	strp[i++] = str;
	if (!*str) {
		strp[0] = 0;
		return (0);
	}
	for (l = 0; *str && (l < limit); str++) {
		if (*str == QUOTECHR) {
			if (inquo) {
				*str = 0;
				inquo = 0;
			} else {
				strp[i - 1] = str + 1;
				inquo = 1;
			}
		}
		if ((*str == DELIMCHR) && (!inquo)) {
			*str = 0;
			l++;
			strp[i++] = str + 1;
		}
	}
	strp[i] = 0;
	return (i);
}

/*!
 * \brief Print the echolink internal user list to the cli.
 * \param nodep		Pointer to eldb struct.
 * \param which		Enum for VISIT used by twalk.
 * \param depth		Level of the node in the tree. 
 */
static void print_nodes(const void *nodep, const VISIT which, const int depth)
{
	if ((which == leaf) || (which == postorder)) {
		ast_cli(nodeoutfd, "%s|%s|%s\n",
				(*(struct eldb **) nodep)->nodenum,
				(*(struct eldb **) nodep)->callsign, (*(struct eldb **) nodep)->ipaddr);
	}
}

/*!
 * \brief Compare two eldb struct by nodenum.
 * \param pa		Pointer to first eldb struct.
 * \param pb		Pointer to second eldb struct.
 * \retval 0		If equal.
 * \retval -1		If less than.
 * \retval 1		If greater than.
 */
static int compare_eldb_nodenum(const void *pa, const void *pb)
{
	return strcmp(((struct eldb *) pa)->nodenum, ((struct eldb *) pb)->nodenum);
}

/*!
 * \brief Compare two eldb struct by IP address.
 * \param pa		Pointer to first eldb struct.
 * \param pb		Pointer to second eldb struct.
 * \retval 0		If equal.
 * \retval -1		If less than.
 * \retval 1		If greater than.
 */
static int compare_eldb_ipaddr(const void *pa, const void *pb)
{
	return strcmp(((struct eldb *) pa)->ipaddr, ((struct eldb *) pb)->ipaddr);
}

/*!
 * \brief Compare two eldb struct by callsign.
 * \param pa		Pointer to first eldb struct.
 * \param pb		Pointer to second eldb struct.
 * \retval 0		If equal.
 * \retval -1		If less than.
 * \retval 1		If greater than.
 */
static int compare_eldb_callsign(const void *pa, const void *pb)
{
	return strcmp(((struct eldb *) pa)->callsign, ((struct eldb *) pb)->callsign);
}

/*!
 * \brief Find an echolink node from the internal user database by nodenum.
 * \param nodenum	Pointer to node number to find.
 * \retval NULL		If the IP address was not found.
 * \return If found returns an eldb struct.
 */
static struct eldb *el_db_find_nodenum(const char *nodenum)
{
	struct eldb **found_key = NULL, key;
	memset(&key, 0, sizeof(key));

	ast_copy_string(key.nodenum, nodenum, sizeof(key.nodenum));

	found_key = (struct eldb **) tfind(&key, &el_db_nodenum, compare_eldb_nodenum);
	if (found_key)
		return (*found_key);

	return NULL;
}

/*!
 * \brief Find an echolink node from the internal user database by callsign.
 * \param callsign	Pointer to callsign to find.
 * \retval NULL		If the callsign was not found.
 * \return If found returns an eldb struct.
 */
static struct eldb *el_db_find_callsign(const char *callsign)
{
	struct eldb **found_key = NULL, key;
	memset(&key, 0, sizeof(key));

	ast_copy_string(key.callsign, callsign, sizeof(key.callsign) - 1);

	found_key = (struct eldb **) tfind(&key, &el_db_callsign, compare_eldb_callsign);
	if (found_key)
		return (*found_key);

	return NULL;
}

/*!
 * \brief Find an echolink node from the internal user database by ip address.
 * \param ipaddr	IP address to find.
 * \retval NULL		If the IP address was not found.
 * \return If found returns an eldb struct.
 */
static struct eldb *el_db_find_ipaddr(const char *ipaddr)
{
	struct eldb **found_key = NULL, key;
	memset(&key, 0, sizeof(key));

	ast_copy_string(key.ipaddr, ipaddr, sizeof(key.ipaddr) - 1);

	found_key = (struct eldb **) tfind(&key, &el_db_ipaddr, compare_eldb_ipaddr);
	if (found_key)
		return (*found_key);

	return NULL;
}

/*!
 * \brief Delete a node from the internal echolink users database.
 * This removes the node from the three internal binary trees.
 * \param node		Pointer to node to delete.
 */
static void el_db_delete_indexes(struct eldb *node)
{
	struct eldb *mynode;

	if (!node)
		return;

	mynode = el_db_find_nodenum(node->nodenum);
	if (mynode)
		tdelete(mynode, &el_db_nodenum, compare_eldb_nodenum);

	mynode = el_db_find_ipaddr(node->ipaddr);
	if (mynode)
		tdelete(mynode, &el_db_ipaddr, compare_eldb_ipaddr);

	mynode = el_db_find_callsign(node->callsign);
	if (mynode)
		tdelete(mynode, &el_db_callsign, compare_eldb_callsign);

	return;
}

/*!
 * \brief Delete a node from the internal echolink users database.
 * \param nodenum		Pointer to node to delete.
 */
static void el_db_delete(struct eldb *node)
{
	if (!node)
		return;
	el_db_delete_indexes(node);
	ast_free(node);
	return;
}

/*!
 * \brief Add a node to the internal echolink users database.
 * The node is added to the three internal indexes.
 * \param nodenum		Buffer to node number.
 * \param ipaddr		Buffer to ip address.
 * \param callsign		Buffer to callsign.
 * \return Returns struct eldb for the entry created.
 */
static struct eldb *el_db_put(char *nodenum, char *ipaddr, char *callsign)
{
	struct eldb *node, *mynode;

	node = (struct eldb *) ast_calloc(1, sizeof(struct eldb));
	if (!node) {
		return NULL;
	}

	ast_copy_string(node->nodenum, nodenum, ELDB_NODENUMLEN);
	ast_copy_string(node->ipaddr, ipaddr, ELDB_IPADDRLEN);
	ast_copy_string(node->callsign, callsign, ELDB_CALLSIGNLEN);

	mynode = el_db_find_nodenum(node->nodenum);
	if (mynode)
		el_db_delete(mynode);

	mynode = el_db_find_ipaddr(node->ipaddr);
	if (mynode)
		el_db_delete(mynode);

	mynode = el_db_find_callsign(node->callsign);
	if (mynode)
		el_db_delete(mynode);

	tsearch(node, &el_db_nodenum, compare_eldb_nodenum);
	tsearch(node, &el_db_ipaddr, compare_eldb_ipaddr);
	tsearch(node, &el_db_callsign, compare_eldb_callsign);

	ast_debug(2, "eldb put: Node=%s, Call=%s, IP=%s\n", nodenum, callsign, ipaddr);

	return (node);
}

/*!
 * \brief Make a sdes packet with our nodes information.
 * The RTP version = 3, RTP packet type = 201.
 * The RTCP: version = 3, packet type = 202.
 * \param pkt			Pointer to buffer for sdes packet
 * \param pktLen		Length of packet buffer
 * \param call			Pointer to callsign
 * \param name			Pointer to node name
 * \param astnode		Pointer to AllstarLink node number
 * \retval 1 			Successful
 */
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

	ast_copy_string(line, "CALLSIGN", EL_CALL_SIZE + EL_NAME_SIZE);
	*ap++ = 1;
	*ap++ = l = strlen(line);
	memcpy(ap, line, l);
	ap += l;

	snprintf(line, EL_CALL_SIZE + EL_NAME_SIZE, "%s %s", call, name);
	*ap++ = 2;
	*ap++ = l = strlen(line);
	memcpy(ap, line, l);
	ap += l;

	if (astnode) {
		snprintf(line, EL_CALL_SIZE + EL_NAME_SIZE, "Allstar %s", astnode);
		*ap++ = 6;
		*ap++ = l = strlen(line);
		memcpy(ap, line, l);
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
	memcpy(pkt, zp, l);
	return l;
}

/*!
 * \brief Make a sdes packet for APRS
 * The RTP version = 2, RTP packet type = 201.
 * The RTCP: version = 2, packet type = 202.
 * \param pkt			Pointer to buffer for sdes packet
 * \param pktLen		Length of packet buffer
 * \param cname			Pointer to aprs name
 * \param loc			Pointer to aprs location
 * \retval 1 			Successful
 */
static int rtcp_make_el_sdes(unsigned char *pkt, int pktLen, char *cname, char *loc)
{
	unsigned char zp[1500];
	unsigned char *p = zp;
	struct rtcp_t *rp;
	unsigned char *ap;
	int l, hl, pl;

	memset(zp, 0, sizeof(zp)); /* Not really needed since pkt has been memset already by the caller, but prevents valgrind complaining about it */

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
	memcpy(ap, cname, l);
	ap += l;

	*ap++ = 5;
	*ap++ = l = strlen(loc);
	memcpy(ap, loc, l);
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
	memcpy(pkt, zp, l);
	return l;
}

/*!
 * \brief Make a rtcp bye packet
 * The RTP version = 3, RTP packet type = 201.
 * The RTCP: version = 3, packet type = 203.
 * \param p				Pointer to buffer for bye packet
 * \param reason		Pointer to reason for the bye packet
 * \retval 1 			Successful
 */
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

	rp = (struct rtcp_t *) p;
	*((short *) p) = htons((3 << 14) | 203 | (1 << 8));
	rp->r.bye.src[0] = htonl(0);
	ap = (unsigned char *) rp->r.sdes.item;
	l = 0;
	if (reason != NULL) {
		l = strlen(reason);
		if (l > 0) {
			*ap++ = l;
			memcpy(ap, reason, l);
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

/*!
 * \brief Parse a sdes packet
 * \param packet		Pointer to packet to parse
 * \param r				Pointer to sdes structure to receive parsed data
 */
static void parse_sdes(unsigned char *packet, struct rtcp_sdes_request *r)
{
	int i;
	unsigned char *p = packet;

	for (i = 0; i < r->nitems; i++)
		r->item[i].r_text = NULL;

	/* 	the RTP version must be 3 or 1 
	 *	the payload type must be 202
	 *	the CSRC must be greater than zero
	*/
	while ((p[0] >> 6 & 3) == 3 || (p[0] >> 6 & 3) == 1) {
		if ((p[1] == 202) && ((p[0] & 0x1F) > 0)) {
			unsigned char *cp = p + 8, *lp = cp + (ntohs(*((short *) (p + 2))) + 1) * 4;
			memcpy(r->ssrc, p + 4, 4);
			while (cp < lp) {
				unsigned char itype = *cp;
				if (itype == 0)
					break;

				for (i = 0; i < r->nitems; i++) {
					if (r->item[i].r_item == itype && r->item[i].r_text == NULL) {
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

/*!
 * \brief Copy a sdes item to a buffer
 * \param source		Pointer to source buffer to copy
 * \param dest			Pointer to destination buffer
 * \param destlen		Length of the destination buffer
 */
static void copy_sdes_item(char *source, char *dest, int destlen)
{
	int len = source[1] & 0xFF;
	if (len > destlen)
		len = destlen;
	memcpy(dest, source + 2, len);
	dest[len] = 0;
	return;
}

/*!
 * \brief Determine if the packet is of type rtcp bye.
 * The RTP packet type must be 200 or 201.
 * The RTCP packet type must be 203.
 * \param p				Pointer to buffer of packet to test.
 * \param len			Buffer length.
 * \retval 1 			Is a bye packet.
 * \retval 0 			Not bye packet.
 */
static int is_rtcp_bye(unsigned char *p, int len)
{
	unsigned char *end;
	int sawbye = 0;

	/* 	the RTP version must be 3 or 1 
	 *	the padding bit must not be set
	 *	the payload type must be 200 or 201
	*/
	if ((((p[0] >> 6) & 3) != 3 && ((p[0] >> 6) & 3) != 1) || ((p[0] & 0x20) != 0) || ((p[1] != 200) && (p[1] != 201)))
		return 0;

	end = p + len;

	/* 	see if this packet contains a RTCP packet type 203 */
	do {
		if (p[1] == 203)
			sawbye = 1;

		p += (ntohs(*((short *) (p + 2))) + 1) * 4;
	} while (p < end && (((p[0] >> 6) & 3) == 3));

	return sawbye;
}

/*!
 * \brief Determine if the packet is of type sdes.
 * The RTP packet type must be 200 or 201.
 * The RTCP packet type must be 202.
 * \param p				Buffer of packet to test.
 * \param len			Buffer length.
 * \retval 1 			Is a sdes packet.
 * \retval 0 			Not sdes packet.
 */
static int is_rtcp_sdes(unsigned char *p, int len)
{
	unsigned char *end;
	int sawsdes = 0;
	
	/* 	the RTP version must be 3 or 1 
	 *	the padding bit must not be set
	 *	the payload type must be 200 or 201
	*/
	if ((((p[0] >> 6) & 3) != 3 && ((p[0] >> 6) & 3) != 1) || ((p[0] & 0x20) != 0) || ((p[1] != 200) && (p[1] != 201)))
		return 0;

	end = p + len;
	
	/* 	see if this packet contains RTCP packet type 202 */
	do {
		if (p[1] == 202)
			sawsdes = 1;

		p += (ntohs(*((short *) (p + 2))) + 1) * 4;
	} while (p < end && (((p[0] >> 6) & 3) == 3));

	return sawsdes;
}

/*!
 * \brief Echolink call.
 * \param ast			Pointer to Asterisk channel.
 * \param dest			Pointer to Destination (echolink node number).
 * \param timeout		Timeout.
 * \retval -1 			if not successful.
 * \retval 0 			if successful.
 */
static int el_call(struct ast_channel *ast, const char *dest, int timeout)
{
	struct el_pvt *p = ast_channel_tech_pvt(ast);
	struct el_instance *instp = p->instp;
	struct eldb *foundnode;
	char ipaddr[EL_IP_SIZE];
	char buf[100];
	char *str, *cp;

	if ((ast_channel_state(ast) != AST_STATE_DOWN) && (ast_channel_state(ast) != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "el_call called on %s, neither down nor reserved\n", ast_channel_name(ast));
		return -1;
	}
	
	/* Make sure we have a destination */
	if(!*dest) {
		ast_log(LOG_WARNING, "Call on %s failed - no destination.\n", ast_channel_name(ast));
		return -1;
	}
	/* When we call, it just works, really, there's no destination...  Just
	 * ring the phone and wait for someone to answer 
	*/

	/* get the node number in cp */
	str = ast_strdup(dest);
	cp = strchr(str, '/');
	if (cp) {
		*cp++ = 0;
	} else {
		cp = str;
	}
	
	/* get the ip address for the node */
	ast_mutex_lock(&el_db_lock);
	foundnode = el_db_find_nodenum(cp);
	if (foundnode) {
		ast_copy_string(ipaddr, foundnode->ipaddr, sizeof(ipaddr));
	}
	ast_mutex_unlock(&el_db_lock);
	ast_free(str);

	if (!foundnode) {
		ast_verb(3, "Call for node %s on %s, failed. Node not found in database.\n", dest, ast_channel_name(ast));
		return -1;
	}
			
	snprintf(buf, sizeof(buf) - 1, "o.conip %s", ipaddr);
		
	ast_debug(1, "Calling %s/%s on %s\n", dest, ipaddr, ast_channel_name(ast));
		
	/* make the call */
	ast_mutex_lock(&instp->lock);
	strcpy(instp->el_node_test.ip, ipaddr);
	do_new_call(instp, p, "OUTBOUND", "OUTBOUND");
	process_cmd(buf, "127.0.0.1", instp);
	ast_mutex_unlock(&instp->lock);
	
	ast_setstate(ast, AST_STATE_RINGING);
	
	return 0;
}

/*!
 * \brief Destroy and free an echolink instance.
 * \param p			Pointer to el_pvt struct to release.
 */
static void el_destroy(struct el_pvt *p)
{
	if (p->dsp)
		ast_dsp_free(p->dsp);
	if (p->xpath)
		ast_translator_free_path(p->xpath);
	if (p->linkstr)
		ast_free(p->linkstr);
	p->linkstr = NULL;
	twalk(el_node_list, send_info);
	ast_module_user_remove(p->u);
	ast_free(p);
}

/*!
 * \brief Allocate and initialize an echolink private structure.
 * \param data			Pointer to echolink instance name to initialize.
 * \retval 				el_pvt structure.		
 */
static struct el_pvt *el_alloc(void *data)
{
	struct el_pvt *p;
	int n;
	/* int flags = 0; */
	char stream[256];

	if (ast_strlen_zero(data))
		return NULL;

	for (n = 0; n < ninstances; n++) {
		if (!strcmp(instances[n]->name, (char *) data))
			break;
	}
	if (n >= ninstances) {
		ast_log(LOG_ERROR, "Cannot find echolink channel %s\n", (char *) data);
		return NULL;
	}

	p = ast_calloc(1, sizeof(struct el_pvt));
	if (p) {
		sprintf(stream, "%s-%lu", (char *) data, instances[n]->seqno++);
		strcpy(p->stream, stream);
		p->rxqast.qe_forw = &p->rxqast;
		p->rxqast.qe_back = &p->rxqast;

		p->rxqel.qe_forw = &p->rxqel;
		p->rxqel.qe_back = &p->rxqel;

		p->keepalive = KEEPALIVE_TIME;
		p->instp = instances[n];
		p->dsp = ast_dsp_new();
		if (!p->dsp) {
			ast_log(LOG_ERROR, "Cannot get DSP!!\n");
			return NULL;
		}
		ast_dsp_set_features(p->dsp, DSP_FEATURE_DIGIT_DETECT);
		ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
		p->xpath = ast_translator_build_path(ast_format_slin, ast_format_gsm);
		if (!p->xpath) {
			ast_log(LOG_ERROR, "Cannot get translator!!\n");
			return NULL;
		}
	}
	return p;
}

/*!
 * \brief Asterisk hangup function.
 * \param ast			Asterisk channel.
 * \retval 0			Always returns 0.			
 */
static int el_hangup(struct ast_channel *ast)
{
	struct el_pvt *p = ast_channel_tech_pvt(ast);
	struct el_instance *instp = p->instp;
	int i, n;
	unsigned char bye[50];
	struct sockaddr_in sin;
	time_t now;

	ast_debug(1, "Sent bye to IP address %s\n", p->ip);
	ast_mutex_lock(&instp->lock);
	strcpy(instp->el_node_test.ip, p->ip);
	find_delete(&instp->el_node_test);
	ast_mutex_unlock(&instp->lock);
	n = rtcp_make_bye(bye, "disconnected");
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = inet_addr(p->ip);
	sin.sin_port = htons(instp->ctrl_port);
	/* send 20 bye packets to insure that they receive this disconnect */
	for (i = 0; i < 20; i++) {
		sendto(instp->ctrl_sock, bye, n, 0, (struct sockaddr *) &sin, sizeof(sin));
	}
	time(&now);
	if (instp->starttime < (now - EL_APRS_START_DELAY)) {
		instp->aprstime = now;
	}
	ast_debug(1, "el_hangup(%s)\n", ast_channel_name(ast));
	if (!ast_channel_tech_pvt(ast)) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	el_destroy(p);
	ast_channel_tech_pvt_set(ast, NULL);
	ast_setstate(ast, AST_STATE_DOWN);
	return 0;
}

/*!
 * \brief Asterisk indicate function.
 * This is used to indicate tx key / unkey.
 * \param ast			Pointer to Asterisk channel.
 * \param cond			Condition.
 * \param data			Pointer to data.
 * \param datalen		Pointer to data length.
 * \retval 0			If successful.
 * \retval -1			For hangup.
 */
static int el_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen)
{
	struct el_pvt *p = ast_channel_tech_pvt(ast);

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

/*!
 * \brief Asterisk digit begin function.
 * \param ast			Pointer to Asterisk channel.
 * \param digit			Digit processed.
 * \retval -1			
 */
static int el_digit_begin(struct ast_channel *ast, char digit)
{
	return -1;
}

/*!
 * \brief Asterisk digit end function.
 * \param ast			Pointer to Asterisk channel.
 * \param digit			Digit processed.
 * \param duration		Duration of the digit.
 * \retval -1			
 */
static int el_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	return -1;
}

/*!
 * \brief Asterisk queryoption function.
 * The calling application should populate the data buffer with the node number
 * to query information. 
 * \param chan			Pointer to Asterisk channel.
 * \param option		Query option to be performed.
 *						1 = query ipaddress, 2 = query callsign
 * \param data			Point to buffer to exchange data.
 * \param datalen		Length of the data buffer.
 * \retval 0			If successful.
 * \retval -1			Query failed.
 */
static int el_queryoption(struct ast_channel *chan, int option, void *data, int *datalen)
{
	struct eldb *foundnode = NULL;
	int res = -1;
	char *node = data;
	
	/* Make sure that we got a node number to query */
	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "Node number not supplied.");
		return res;
	}

	ast_mutex_lock(&el_db_lock);

	/* Process the requested query option */
	switch (option) {
		case EL_QUERY_IPADDR:
			foundnode = el_db_find_nodenum(node);
			if (foundnode) {
				ast_copy_string(data, foundnode->ipaddr, *datalen);
				res = 0;
			}
			break;
		case EL_QUERY_CALLSIGN:
			foundnode = el_db_find_nodenum(node);
			if (foundnode) {
				ast_copy_string(data, foundnode->callsign, *datalen);
				res = 0;
			}
			break;
		default:
			ast_log(LOG_ERROR, "Option %i is not valid.", option);
			break;
	}

	ast_mutex_unlock(&el_db_lock);
	
	if (res) {
		memset(data, '\0', *datalen);
		ast_debug(2, "Node %s was not found, query failed.", node);
	}

	return res;
}

/*!
 * \brief Compare for qsort - sorts nodes.
 * \param a			Pointer to first string.
 * \param b			Pointer to second string.
 * \retval 0		If equal.
 * \retval -1		If less than.
 * \retval 1		If greater than.
 */
static int mycompar(const void *a, const void *b)
{
	char **x = (char **) a;
	char **y = (char **) b;
	int xoff, yoff;

	if ((**x < '0') || (**x > '9'))
		xoff = 1;
	else
		xoff = 0;
	if ((**y < '0') || (**y > '9'))
		yoff = 1;
	else
		yoff = 0;
	return (strcmp((*x) + xoff, (*y) + yoff));
}

/*!
 * \brief Asterisk text function.
 * \param ast			Pointer to Asterisk channel.
 * \param text			Pointer to text message to process.
 * \retval 0			If successful.
 * \retval -1			If unsuccessful.
 */
static int el_text(struct ast_channel *ast, const char *text)
{
#define	MAXLINKSTRS 200

	struct el_pvt *p = ast_channel_tech_pvt(ast);
	char *cmd = NULL, *arg1 = NULL;
	const char *delim = " ";
	char *saveptr, *cp, *pkt;
	char buf[200], *ptr, str[200], *arg4 = NULL, *strs[MAXLINKSTRS];
	int i, j, k, x;

	ast_copy_string(buf, text, sizeof(buf));
	ptr = strchr(buf, (int) '\r');
	if (ptr)
		*ptr = '\0';
	ptr = strchr(buf, (int) '\n');
	if (ptr)
		*ptr = '\0';

	if (p->instp && (text[0] == 'L')) {
		if (strlen(text) < 3) {
			if (p->linkstr) {
				ast_free(p->linkstr);
				p->linkstr = NULL;
				twalk(el_node_list, send_info);
			}
			return 0;
		}
		if (p->linkstr) {
			ast_free(p->linkstr);
			p->linkstr = NULL;
		}
		cp = ast_strdup(text + 2);
		if (!cp) {
			ast_log(LOG_ERROR, "Couldnt alloc");
			return -1;
		}
		i = finddelim(cp, strs, MAXLINKSTRS);
		if (i) {
			qsort((void *) strs, i, sizeof(char *), mycompar);
			pkt = ast_calloc(1, (i * 10) + 50);
			if (!pkt) {
				return -1;
			}
			j = 0;
			k = 0;
			for (x = 0; x < i; x++) {
				if ((*(strs[x] + 1) < '3') || (*(strs[x] + 1) > '4')) {
					if (strlen(pkt + k) >= 32) {
						k = strlen(pkt);
						strcat(pkt, "\r    ");
					}
					if (!j++)
						strcat(pkt, "Allstar:");
					if (*strs[x] == 'T')
						sprintf(pkt + strlen(pkt), " %s", strs[x] + 1);
					else
						sprintf(pkt + strlen(pkt), " %s(M)", strs[x] + 1);
				}
			}
			strcat(pkt, "\r");
			j = 0;
			k = strlen(pkt);
			for (x = 0; x < i; x++) {
				if (*(strs[x] + 1) == '3') {
					if (strlen(pkt + k) >= 32) {
						k = strlen(pkt);
						strcat(pkt, "\r    ");
					}
					if (!j++)
						strcat(pkt, "Echolink: ");
					if (*strs[x] == 'T')
						sprintf(pkt + strlen(pkt), " %d", atoi(strs[x] + 2));
					else
						sprintf(pkt + strlen(pkt), " %d(M)", atoi(strs[x] + 2));
				}
			}
			strcat(pkt, "\r");
			if (p->linkstr && pkt && (!strcmp(p->linkstr, pkt)))
				ast_free(pkt);
			else
				p->linkstr = pkt;
		}
		ast_free(cp);
		twalk(el_node_list, send_info);
		return 0;
	}

	cmd = strtok_r(buf, delim, &saveptr);
	if (!cmd) {
		return 0;
	}

	arg1 = strtok_r(NULL, delim, &saveptr);
	strtok_r(NULL, delim, &saveptr);
	strtok_r(NULL, delim, &saveptr);
	arg4 = strtok_r(NULL, delim, &saveptr);

	if (!strcasecmp(cmd, "D")) {
		sprintf(str, "3%06u", p->nodenum);
		/* if not for this one, we cant go any farther */
		if (strcmp(arg1, str)) {
			return 0;
		}
		ast_senddigit(ast, *arg4, 0);
		return 0;
	}
	return 0;
}

/*!
 * \brief Compare two el_node struct by ip address.
 * \param pa		Pointer to first el_node struct.
 * \param pb		Pointer to second el_node struct.
 * \retval 0		If equal.
 * \retval -1		If less than.
 * \retval 1		If greater than.
 */
static int compare_ip(const void *pa, const void *pb)
{
	return strncmp(((struct el_node *) pa)->ip, ((struct el_node *) pb)->ip, EL_IP_SIZE);
}

/*!
 * \brief Send audio from asterisk to echolink to one node (currently connecting node).
 * \param nodep		Pointer to el_node struct.
 * \param which		Enum for VISIT used by twalk.
 * \param depth		Level of the node in the tree.
 */
static void send_audio_only_one(const void *nodep, const VISIT which, const int depth)
{
	struct sockaddr_in sin;
	struct el_instance *instp = (*(struct el_node **) nodep)->instp;

	if ((which == leaf) || (which == postorder)) {
		if (strncmp((*(struct el_node **) nodep)->ip, instp->el_node_test.ip, EL_IP_SIZE) == 0) {
			sin.sin_family = AF_INET;
			sin.sin_port = htons(instp->audio_port);
			sin.sin_addr.s_addr = inet_addr((*(struct el_node **) nodep)->ip);

			instp->audio_all.version = 3;
			instp->audio_all.pad = 0;
			instp->audio_all.ext = 0;
			instp->audio_all.csrc = 0;
			instp->audio_all.marker = 0;
			instp->audio_all.payt = 3;
			instp->audio_all.seqnum = htons((*(struct el_node **) nodep)->seqnum++);
			instp->audio_all.time = htonl(0);
			instp->audio_all.ssrc = htonl(instp->mynode);

			sendto(instp->audio_sock, (char *) &instp->audio_all, sizeof(instp->audio_all),
				   0, (struct sockaddr *) &sin, sizeof(sin));
		}
	}
}

/*!
 * \brief Print connected users using ast_verbose.
 * \param nodep		Pointer to el_node struct.
 * \param which		Enum for VISIT used by twalk.
 * \param depth		Level of the node in the tree.
 */
static void print_users(const void *nodep, const VISIT which, const int depth)
{
	if ((which == leaf) || (which == postorder)) {
		ast_verbose("Echolink user: call=%s,ip=%s,name=%s\n",
					(*(struct el_node **) nodep)->call,
					(*(struct el_node **) nodep)->ip, (*(struct el_node **) nodep)->name);
	}
}

/*!
 * \brief Count connected nodes.
 * \param nodep		Pointer to el_node struct.
 * \param which		Enum for VISIT used by twalk.
 * \param depth		Level of the node in the tree.
 */
static void count_users(const void *nodep, const VISIT which, const int depth)
{
	if ((which == leaf) || (which == postorder)) {
		if ((*(struct el_node **) nodep)->instp == count_instp) {
			count_n++;
			if ((*(struct el_node **) nodep)->outbound)
				count_outbound_n++;
		}
	}
}

/*!
 * \brief Send connection information to connected nodes.
 * \param nodep		Pointer to el_node struct.
 * \param which		Enum for VISIT used by twalk.
 * \param depth		Level of the node in the tree.
 */
static void send_info(const void *nodep, const VISIT which, const int depth)
{
	struct sockaddr_in sin;
	char pkt[2500], *cp;
	struct el_instance *instp = (*(struct el_node **) nodep)->instp;
	int i;

	if ((which == leaf) || (which == postorder)) {

		sin.sin_family = AF_INET;
		sin.sin_port = htons(instp->audio_port);
		sin.sin_addr.s_addr = inet_addr((*(struct el_node **) nodep)->ip);
		snprintf(pkt, sizeof(pkt) - 1, "oNDATA\rWelcome to Allstar Node %s\r", instp->astnode);
		i = strlen(pkt);
		snprintf(pkt + i, sizeof(pkt) - (i + 1), "Echolink Node %s\rNumber %u\r \r", instp->mycall, instp->mynode);
		if ((*(struct el_node **) nodep)->p && (*(struct el_node **) nodep)->p->linkstr) {
			i = strlen(pkt);
			strncat(pkt + i, "Systems Linked:\r", sizeof(pkt) - (i + 1));
			cp = ast_strdup((*(struct el_node **) nodep)->p->linkstr);
			i = strlen(pkt);
			strncat(pkt + i, cp, sizeof(pkt) - (i + 1));
			ast_free(cp);
		}
		sendto(instp->audio_sock, pkt, strlen(pkt), 0, (struct sockaddr *) &sin, sizeof(sin));
	}
	return;
}

/*!
 * \brief Send a heartbeat packet to connected nodes.
 * \param nodep		Pointer to eldb struct.
 * \param which		Enum for VISIT used by twalk.
 * \param depth		Level of the node in the tree.
 */
static void send_heartbeat(const void *nodep, const VISIT which, const int depth)
{
	struct sockaddr_in sin;
	unsigned char sdes_packet[256];
	int sdes_length;
	struct el_instance *instp = (*(struct el_node **) nodep)->instp;

	if ((which == leaf) || (which == postorder)) {

		if ((*(struct el_node **) nodep)->countdown >= 0)
			(*(struct el_node **) nodep)->countdown--;

		if ((*(struct el_node **) nodep)->countdown < 0) {
			ast_copy_string(instp->el_node_test.ip, (*(struct el_node **) nodep)->ip, EL_IP_SIZE);
			ast_copy_string(instp->el_node_test.call, (*(struct el_node **) nodep)->call, EL_CALL_SIZE);
			ast_log(LOG_WARNING, "countdown for %s(%s) negative\n", instp->el_node_test.call, instp->el_node_test.ip);
		}
		memset(sdes_packet, 0, sizeof(sdes_packet));
		sdes_length = rtcp_make_sdes(sdes_packet, sizeof(sdes_packet), instp->mycall, instp->myname, instp->astnode);

		sin.sin_family = AF_INET;
		sin.sin_port = htons(instp->ctrl_port);
		sin.sin_addr.s_addr = inet_addr((*(struct el_node **) nodep)->ip);
		sendto(instp->ctrl_sock, sdes_packet, sdes_length, 0, (struct sockaddr *) &sin, sizeof(sin));
	}
}

/*!
 * \brief Free node.  Empty routine.
 */
static void free_node(void *nodep)
{
}

/*!
 * \brief Find and delete a node from our internal node list.
 * \param key			Poitner to Echolink node struct to delete.
 * \retval 0			If node not found.
 * \retval 1			If node found.
 */
static int find_delete(struct el_node *key)
{
	int found = 0;
	struct el_node **found_key = NULL;

	found_key = (struct el_node **) tfind(key, &el_node_list, compare_ip);
	if (found_key) {
		ast_debug(5, "...removing %s(%s)\n", (*found_key)->call, (*found_key)->ip);
		found = 1;
		ast_softhangup((*found_key)->chan, AST_SOFTHANGUP_DEV);
		tdelete(key, &el_node_list, compare_ip);
	}
	return found;
}

/*!
 * \brief Process commands received from our local machine.
 * Commands: 
 *	o.conip <IPaddress>    (request a connect)
 *	o.dconip <IPaddress>   (request a disconnect)
 *	o.rec                  (turn on/off recording)
 * \param buf			Pointer to buffer with command data.
 * \param fromip		Pointer to ip address that sent the command.
 * \param instp			Poiner to Echolink instance.
 */
static void process_cmd(char *buf, char *fromip, struct el_instance *instp)
{
	char *cmd = NULL;
	char *arg1 = NULL;

	char delim = ' ';
	char *saveptr;
	char *ptr;
	struct sockaddr_in sin;
	unsigned char pack[256];
	int pack_length;
	unsigned short i, n;
	struct el_node key;

	if (strncmp(fromip, "127.0.0.1", EL_IP_SIZE) != 0)
		return;
	ptr = strchr(buf, (int) '\r');
	if (ptr)
		*ptr = '\0';
	ptr = strchr(buf, (int) '\n');
	if (ptr)
		*ptr = '\0';

	/* all commands with no arguments go first */

	if (strcmp(buf, "o.users") == 0) {
		twalk(el_node_list, print_users);
		return;
	}

	if (strcmp(buf, "o.rec") == 0) {
		if (instp->fdr >= 0) {
			close(instp->fdr);
			instp->fdr = -1;
			ast_debug(3, "rec stopped\n");
		} else {
			instp->fdr = open(instp->fdr_file, O_CREAT | O_WRONLY | O_APPEND | O_TRUNC, S_IRUSR | S_IWUSR);
			if (instp->fdr >= 0)
				ast_debug(3, "rec into %s started\n", instp->fdr_file);
		}
		return;
	}

	cmd = strtok_r(buf, &delim, &saveptr);
	if (!cmd) {
		return;
	}

	/* This version:  up to 3 parameters */
	arg1 = strtok_r(NULL, &delim, &saveptr);
	strtok_r(NULL, &delim, &saveptr);
	strtok_r(NULL, &delim, &saveptr);

	if ((strcmp(cmd, "o.conip") == 0) || (strcmp(cmd, "o.dconip") == 0)) {
		if (!arg1) {
			return;
		}

		if (strcmp(cmd, "o.conip") == 0) {
			n = 1;
			pack_length = rtcp_make_sdes(pack, sizeof(pack), instp->mycall, instp->myname, instp->astnode);
		} else {
			pack_length = rtcp_make_bye(pack, "bye");
			n = 20;
		}
		sin.sin_family = AF_INET;
		sin.sin_port = htons(instp->ctrl_port);
		sin.sin_addr.s_addr = inet_addr(arg1);

		if (strcmp(cmd, "o.dconip") == 0) {
			ast_copy_string(key.ip, arg1, EL_IP_SIZE);
			if (find_delete(&key)) {
				for (i = 0; i < 20; i++)
					sendto(instp->ctrl_sock, pack, pack_length, 0, (struct sockaddr *) &sin, sizeof(sin));
				ast_debug(3, "disconnect request sent to %s\n", key.ip);
			} else {
				ast_debug(1, "Did not find ip=%s to request disconnect\n", key.ip);
			}
		} else {
			for (i = 0; i < n; i++) {
				sendto(instp->ctrl_sock, pack, pack_length, 0, (struct sockaddr *) &sin, sizeof(sin));
			}
			ast_debug(3, "connect request sent to %s\n", arg1);
		}
		return;
	}
	return;
}

/*!
 * \brief Asterisk read function.
 * \param ast			Pointer to Asterisk channel.
 * \retval 				Asterisk frame.
 */
static struct ast_frame *el_xread(struct ast_channel *ast)
{
	struct el_pvt *p = ast_channel_tech_pvt(ast);

	memset(&p->fr, 0, sizeof(struct ast_frame));
	p->fr.frametype = 0;
	p->fr.subclass.integer = 0;
	p->fr.datalen = 0;
	p->fr.samples = 0;
	p->fr.data.ptr = NULL;
	p->fr.src = type;
	p->fr.offset = 0;
	p->fr.mallocd = 0;
	p->fr.delivery.tv_sec = 0;
	p->fr.delivery.tv_usec = 0;
	return &p->fr;
}

/*!
 * \brief Asterisk write function.
 * This routine handles echolink to asterisk and asterisk to echolink.
 * \param ast			Pointer to Asterisk channel.
 * \param frame			Pointer to Asterisk frame to process.
 * \retval 0			Successful.
 */
static int el_xwrite(struct ast_channel *ast, struct ast_frame *frame)
{
	int bye_length;
	unsigned char bye[50];
	unsigned short i;
	struct sockaddr_in sin;
	struct el_pvt *p = ast_channel_tech_pvt(ast);
	struct el_instance *instp = p->instp;
	struct ast_frame fr, *f1, *f2;
	struct el_rxqast *qpast;
	int n, x;
	char buf[GSM_FRAME_SIZE + AST_FRIENDLY_OFFSET];

	if (frame->frametype != AST_FRAME_VOICE)
		return 0;

	if (!p->firstsent) {
		struct sockaddr_in sin;
		unsigned char sdes_packet[256];
		int sdes_length;

		p->firstsent = 1;
		memset(sdes_packet, 0, sizeof(sdes_packet));
		sdes_length = rtcp_make_sdes(sdes_packet, sizeof(sdes_packet), instp->mycall, instp->myname, instp->astnode);

		sin.sin_family = AF_INET;
		sin.sin_port = htons(instp->ctrl_port);
		sin.sin_addr.s_addr = inet_addr(p->ip);
		sendto(instp->ctrl_sock, sdes_packet, sdes_length, 0, (struct sockaddr *) &sin, sizeof(sin));
	}

	/* Echolink to Asterisk */
	if (p->rxqast.qe_forw != &p->rxqast) {
		for (n = 0, qpast = p->rxqast.qe_forw; qpast != &p->rxqast; qpast = qpast->qe_forw) {
			n++;
		}
		if (n > QUEUE_OVERLOAD_THRESHOLD_AST) {
			while (p->rxqast.qe_forw != &p->rxqast) {
				qpast = p->rxqast.qe_forw;
				remque((struct qelem *) qpast);
				ast_free(qpast);
			}
			if (p->rxkey)
				p->rxkey = 1;
		} else {
			if (!p->rxkey) {
				memset(&fr, 0, sizeof(fr));
				fr.datalen = 0;
				fr.samples = 0;
				fr.frametype = AST_FRAME_CONTROL;
				fr.subclass.integer = AST_CONTROL_RADIO_KEY;
				fr.data.ptr = 0;
				fr.src = type;
				fr.offset = 0;
				fr.mallocd = 0;
				fr.delivery.tv_sec = 0;
				fr.delivery.tv_usec = 0;
				ast_queue_frame(ast, &fr);
			}
			p->rxkey = MAX_RXKEY_TIME;
			qpast = p->rxqast.qe_forw;
			remque((struct qelem *) qpast);
			memcpy(buf + AST_FRIENDLY_OFFSET, qpast->buf, GSM_FRAME_SIZE);
			ast_free(qpast);

			memset(&fr, 0, sizeof(fr));
			fr.datalen = GSM_FRAME_SIZE;
			fr.samples = 160;
			fr.frametype = AST_FRAME_VOICE;
			fr.subclass.format = ast_format_gsm;
			fr.data.ptr = buf + AST_FRIENDLY_OFFSET;
			fr.src = type;
			fr.offset = AST_FRIENDLY_OFFSET;
			fr.mallocd = 0;
			fr.delivery.tv_sec = 0;
			fr.delivery.tv_usec = 0;

			x = 0;
			if (p->dsp) {
				f2 = ast_translate(p->xpath, &fr, 0);
				f1 = ast_dsp_process(NULL, p->dsp, f2);
				if ((f1->frametype == AST_FRAME_DTMF_END) || (f1->frametype == AST_FRAME_DTMF_BEGIN))
				{
					if ((f1->subclass.integer != 'm') && (f1->subclass.integer != 'u')) {
						if (f1->frametype == AST_FRAME_DTMF_END)
							ast_verb(4, "Echolink %s Got DTMF char %c from IP %s\n", p->stream, f1->subclass.integer, p->ip);
						ast_queue_frame(ast, f1);
						x = 1;
					}
				}
			}
			if (!x)
				ast_queue_frame(ast, &fr);
		}
	}
	if (p->rxkey == 1) {
		memset(&fr, 0, sizeof(fr));
		fr.datalen = 0;
		fr.samples = 0;
		fr.frametype = AST_FRAME_CONTROL;
		fr.subclass.integer = AST_CONTROL_RADIO_UNKEY;
		fr.data.ptr = 0;
		fr.src = type;
		fr.offset = 0;
		fr.mallocd = 0;
		fr.delivery.tv_sec = 0;
		fr.delivery.tv_usec = 0;
		ast_queue_frame(ast, &fr);
	}
	if (p->rxkey) {
		p->rxkey--;
	}

	/* Asterisk to Echolink */
	if (ast_format_cap_iscompatible_format(ast_channel_nativeformats(ast), frame->subclass.format) == AST_FORMAT_CMP_NOT_EQUAL) {
		struct ast_str *cap_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		ast_log(LOG_WARNING,
				"Asked to transmit frame type %s, while native formats is %s (read/write = (%s/%s))\n",
				ast_format_get_name(frame->subclass.format),
				ast_format_cap_get_names(ast_channel_nativeformats(ast), &cap_buf),
				ast_format_get_name(ast_channel_readformat(ast)),
				ast_format_get_name(ast_channel_writeformat(ast)));
		return 0;
		}
	if (p->txkey || p->txindex) {
		memcpy(instp->audio_all.data + (GSM_FRAME_SIZE * p->txindex++), frame->data.ptr, GSM_FRAME_SIZE);
	}
	if (p->txindex >= BLOCKING_FACTOR) {
		ast_mutex_lock(&instp->lock);
		strcpy(instp->el_node_test.ip, p->ip);
		twalk(el_node_list, send_audio_only_one);
		ast_mutex_unlock(&instp->lock);
		p->txindex = 0;
	}

	if (p->keepalive--) {
		return 0;
	}
	p->keepalive = KEEPALIVE_TIME;

	/* Echolink: send heartbeats and drop dead stations */
	ast_mutex_lock(&instp->lock);
	instp->el_node_test.ip[0] = '\0';
	twalk(el_node_list, send_heartbeat);
	if (instp->el_node_test.ip[0] != '\0') {
		if (find_delete(&instp->el_node_test)) {
			bye_length = rtcp_make_bye(bye, "rtcp timeout");
			sin.sin_family = AF_INET;
			sin.sin_addr.s_addr = inet_addr(instp->el_node_test.ip);
			sin.sin_port = htons(instp->ctrl_port);
			ast_mutex_lock(&instp->lock);
			for (i = 0; i < 20; i++)
				sendto(instp->ctrl_sock, bye, bye_length, 0, (struct sockaddr *) &sin, sizeof(sin));
			ast_mutex_unlock(&instp->lock);
			ast_verb(4, "call=%s RTCP timeout, removing\n", instp->el_node_test.call);
		}
		instp->el_node_test.ip[0] = '\0';
	}
	ast_mutex_unlock(&instp->lock);
	return 0;
}

/*!
 * \brief Start a new Echolink call.
 * \param i				Pointer to echolink private.
 * \param state			State.
 * \param nodenum		Node number to call.
 * \param assignedids	Pointer to unique ID string assigned to the channel.
 * \param requestor		Pointer to Asterisk channel.
 * \return 				Asterisk channel.
 */
static struct ast_channel *el_new(struct el_pvt *i, int state, unsigned int nodenum, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor)
{
	struct ast_channel *tmp;
	struct el_instance *instp = i->instp;

	tmp = ast_channel_alloc(1, state, 0, 0, "", instp->astnode, instp->context, assignedids, requestor, 0, "echolink/%s", i->stream);
	if (!tmp) {
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
		return NULL;
	}

	ast_channel_tech_set(tmp, &el_tech);
	ast_channel_nativeformats_set(tmp, el_tech.capabilities);
	ast_channel_set_rawreadformat(tmp, ast_format_gsm);
	ast_channel_set_rawwriteformat(tmp, ast_format_gsm);
	ast_channel_set_writeformat(tmp, ast_format_gsm);
	ast_channel_set_readformat(tmp, ast_format_gsm);
	if (state == AST_STATE_RING)
		ast_channel_rings_set(tmp, 1);
	ast_channel_tech_pvt_set(tmp, i);
	ast_channel_context_set(tmp, instp->context);
	ast_channel_exten_set(tmp, instp->astnode);
	ast_channel_language_set(tmp, "");
	ast_channel_unlock(tmp);

	if (nodenum > 0) {
		char tmpstr[30];

		sprintf(tmpstr, "3%06u", nodenum);
		ast_set_callerid(tmp, tmpstr, NULL, NULL);
	}
	i->owner = tmp;
	i->u = ast_module_user_add(tmp);
	i->nodenum = nodenum;
	if (state != AST_STATE_DOWN) {
		if (ast_pbx_start(tmp)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ast_channel_name(tmp));
			ast_hangup(tmp);
		}
	}
	return tmp;
}

/*!
 * \brief Echolink request from Asterisk.
 * This is a standard Asterisk function - requester.
 * Asterisk calls this function to to setup private data structures.
 * \param type			Pointer to type of channel to request.
 * \param cap			Pointer to format capabilities for the channel.
 * \param assignedids	Pointer to unique ID string to assign to the channel.
 * \param requestor		Pointer to channel asking for data. 
 * \param data			Pointer to destination of the call.
 * \param cause			Pointer to cause of failure.
 * \retval NULL			Failure
 * \return				ast_channel if successful
 */
static struct ast_channel *el_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	int nodenum;
	struct el_pvt *p;
	struct ast_channel *tmp = NULL;
	char *str, *cp;

	if (!(ast_format_cap_iscompatible(cap, el_tech.capabilities))) {
		struct ast_str *cap_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		ast_log(LOG_NOTICE, "Channel requested with unsupported format(s): '%s'\n", ast_format_cap_get_names(cap, &cap_buf));
		return NULL;
	}

	str = ast_strdup((char *) data);
	cp = strchr(str, '/');
	if (cp)
		*cp++ = 0;
	nodenum = 0;
	if (*cp && *++cp)
		nodenum = atoi(cp);
	p = el_alloc(str);
	ast_free(str);
	if (p) {
		tmp = el_new(p, AST_STATE_DOWN, nodenum, assignedids, requestor);
		if (!tmp)
			el_destroy(p);
	}
	return tmp;
}

/*!
 * \brief Process asterisk cli request to dump internal database entries.
 * \param fd			Asterisk cli fd
 * \param argc			Number of arguments
 * \param argv			Pointer to arguments
 * \return	Cli success, showusage, or failure.
 */
static int el_do_dbdump(int fd, int argc, const char *const *argv)
{
	char c;
	if (argc < 2)
		return RESULT_SHOWUSAGE;

	c = 'n';
	if (argc > 2) {
		c = tolower(*argv[2]);
	}
	ast_mutex_lock(&el_db_lock);
	nodeoutfd = fd;
	if (c == 'i')
		twalk(el_db_ipaddr, print_nodes);
	else if (c == 'c')
		twalk(el_db_callsign, print_nodes);
	else
		twalk(el_db_nodenum, print_nodes);
	nodeoutfd = -1;
	ast_mutex_unlock(&el_db_lock);
	return RESULT_SUCCESS;
}

/*!
 * \brief Process asterisk cli request for internal user database entry.
 * Lookup can be for ip address, callsign, or nodenumber
 * \param fd			Asterisk cli file descriptor.
 * \param argc			Number of arguments.
 * \param argv			Pointer to asterisk cli arguments.
 * \return 	Cli success, showusage, or failure.
 */
static int el_do_dbget(int fd, int argc, const char *const *argv)
{
	char c;
	struct eldb *mynode;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	c = tolower(*argv[2]);
	ast_mutex_lock(&el_db_lock);
	if (c == 'i')
		mynode = el_db_find_ipaddr(argv[3]);
	else if (c == 'c')
		mynode = el_db_find_callsign(argv[3]);
	else
		mynode = el_db_find_nodenum(argv[3]);
	ast_mutex_unlock(&el_db_lock);
	if (!mynode) {
		ast_cli(fd, "Error: Entry for %s not found!\n", argv[3]);
		return RESULT_FAILURE;
	}
	ast_cli(fd, "%s|%s|%s\n", mynode->nodenum, mynode->callsign, mynode->ipaddr);
	return RESULT_SUCCESS;
}

/*!
 * \brief Turns integer response to char cli response
 * \param r				Response.
 * \return	Cli success, showusage, or failure.
 */
static char *res2cli(int r)
{
	switch (r) {
	case RESULT_SUCCESS:
		return (CLI_SUCCESS);
	case RESULT_SHOWUSAGE:
		return (CLI_SHOWUSAGE);
	default:
		return (CLI_FAILURE);
	}
}

/*!
 * \brief Handle asterisk cli request for dbdump
 * \param e				Asterisk cli entry.
 * \param cmd			Cli command type.
 * \param a				Pointer to asterisk cli arguments.
 * \return	Cli success or failure.
 */
static char *handle_cli_dbdump(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "echolink dbdump";
		e->usage = dbdump_usage;
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(el_do_dbdump(a->fd, a->argc, a->argv));
}

/*!
 * \brief Handle asterisk cli request for dbget
 * \param e				Asterisk cli entry.
 * \param cmd			Cli command type.
 * \param a				Pointer to asterisk cli arguments.
 * \retval 				Cli success or failure.
 */
static char *handle_cli_dbget(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "echolink dbget";
		e->usage = dbget_usage;
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(el_do_dbget(a->fd, a->argc, a->argv));
}

/*!
 * \brief Define cli entries for this module
 */
static struct ast_cli_entry el_cli[] = {
	AST_CLI_DEFINE(handle_cli_dbdump, "Dump entire echolink db"),
	AST_CLI_DEFINE(handle_cli_dbget, "Look up echolink db entry")
};

/* 
   If asterisk has a function that writes at least n bytes to a TCP socket,
   remove writen function and use the one provided by Asterisk
*/
/*!
 * \brief Write a buffer to a socket.  Ensures that all bytes are written.
 * This routine will send the number of bytes specified in nbytes,
 * unless an error occurs.
 * \param fd			Socket to write data.
 * \param ptr			Pointer to the data to be written.
 * \param nbytes		Number of bytes to write.
 * \return	Number of bytes written or -1 if the write fails.
 */
static int writen(int fd, char *ptr, int nbytes)
{
	int nleft, nwritten;
	char *local_ptr;

	nleft = nbytes;
	local_ptr = ptr;

	while (nleft > 0) {
		nwritten = write(fd, local_ptr, nleft);
		if (nwritten < 0)
			return nwritten;
		nleft -= nwritten;
		local_ptr += nwritten;
	}
	return (nbytes - nleft);
}

/* Feel free to make this code smaller, I know it works, so I use it */
/*!
 * \brief Send echolink registration command for this instance.
 * Each instance could have a different user name, password, or
 * other parameters.
 * \param server		Pointer to echolink server name.
 * \param instp			The interna echo link instance.
 * \retval -1			If registration failed.
 * \retval 0			If registration was successful.
 */
static int sendcmd(char *server, struct el_instance *instp)
{
	struct hostent *ahp;
	struct ast_hostent ah;
	struct in_addr ia;

	char ip[EL_IP_SIZE + 1];
	struct sockaddr_in el;
	int el_len;
	int sd;
	int rc;
	time_t now;
	struct tm *p_tm;
	char *id = NULL;
	const size_t LOGINSIZE = 1023;
	char buf[LOGINSIZE + 1];
	size_t len;

	ahp = ast_gethostbyname(server, &ah);
	if (ahp) {
		memcpy(&ia, ahp->h_addr, sizeof(in_addr_t));
		ast_copy_string(ip, ast_inet_ntoa(ia), EL_IP_SIZE);
	} else {
		ast_log(LOG_ERROR, "Failed to resolve Echolink server %s\n", server);
		return -1;
	}

	memset(&el, 0, sizeof(struct sockaddr_in));
	el.sin_family = AF_INET;
	el.sin_port = htons(5200);
	el.sin_addr.s_addr = inet_addr(ip);
	el_len = sizeof(el);

	sd = socket(AF_INET, SOCK_STREAM, 0);
	if (sd < 0) {
		ast_log(LOG_ERROR, "failed to create socket to contact the Echolink server %s\n", server);
		return -1;
	}

	rc = connect(sd, (struct sockaddr *) &el, el_len);
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
			 p_tm->tm_hour, p_tm->tm_mday,
			 (instp->login_display[0]) ? instp->login_display : instp->myqth, instp->myemail);

	len = strlen(buf);
	rc = writen(sd, buf, len);
	if (rc != len) {
		ast_log(LOG_ERROR, "writen() failed to send Echolink credentials to Echolink server %s\n", server);
		close(sd);
		return -1;
	}

	buf[0] = '\0';
	while (1) {
		rc = read(sd, buf, LOGINSIZE);
		if (rc > 0) {
			buf[rc] = '\0';
			ast_verb(4, "Received %s from Echolink server %s\n", buf, server);
		} else
			break;
	}
	close(sd);

	if (strncmp(buf, "OK", 2) != 0)
		return -1;

	return 0;
}

/*! \brief Echolink directory server port number */
#define	EL_DIRECTORY_PORT 5200

/*!
 * \brief Frees pointer from interal list.
 * \param ptr			Pointer to free.	
 */
static void my_stupid_free(void *ptr)
{
	ast_free(ptr);
	return;
}

/*!
 * \brief Delete entire echolink node list.
 */
static void el_zapem(void)
{
	ast_mutex_lock(&el_db_lock);
	tdestroy(el_node_list, my_stupid_free);
	ast_mutex_unlock(&el_db_lock);
}

/*!
 * \brief Delete callsign from internal directory.
 * \param call			Pointer to callsign to delete.	
 */
static void el_zapcall(char *call)
{
	struct eldb *mynode;

	ast_debug(2, "zapcall eldb delete Attempt: Call=%s\n", call);
	ast_mutex_lock(&el_db_lock);
	mynode = el_db_find_callsign(call);
	if (mynode) {
		ast_debug(2, "zapcall eldb delete: Node=%s, Call=%s, IP=%s\n", mynode->nodenum, mynode->callsign, mynode->ipaddr);
		el_db_delete(mynode);
	}
	ast_mutex_unlock(&el_db_lock);
}

/*!
 * \brief Reads a line from the passed socket and uncompresses if needed.
 * The routine will continue to read data until it fills the passed buffer.
 * \param sock			Socket to read data.	
 * \param buf1			Pointer to buffer to hold the received data.
 * \param bufllen		Buffer length
 * \param compressed	Compressed data indicator - 1=compressed
 * \param z				Pointer to struct z_stream_s 
 * \retval -1			If read is not successful
 * \retval 				Number of bytes read.
 */
static int el_net_read(int sock, unsigned char *buf1, int buf1len, int compressed, struct z_stream_s *z)
{
	unsigned char buf[512];
	int n, r;

	for (;;) {
		if (!compressed) {
			n = recv(sock, buf1, buf1len - 1, 0);
			if (n < 1)
				return (-1);
			return (n);
		}
		memset(buf1, 0, buf1len);
		memset(buf, 0, sizeof(buf));
		n = recv(sock, buf, sizeof(buf) - 1, 0);
		if (n < 0)
			return (-1);
		z->next_in = buf;
		z->avail_in = n;
		z->next_out = buf1;
		z->avail_out = buf1len;
		r = inflate(z, Z_NO_FLUSH);
		if ((r != Z_OK) && (r != Z_STREAM_END)) {
			if (z->msg)
				ast_log(LOG_ERROR, "Unable to inflate (Zlib): %s\n", z->msg);
			else
				ast_log(LOG_ERROR, "Unable to inflate (Zlib)\n");
			return -1;
		}
		r = buf1len - z->avail_out;
		if ((!n) || r)
			break;
	}
	return (buf1len - z->avail_out);
}

/*!
 * \brief Read and process a line from the echolink directory server.
 * \param s				Socket to read data.	
 * \param str			Pointer to buffer to hold the received line.
 * \param max			Maximum number of characters to read
 * \param compressed	Compressed data indicator - 1=compressed
 * \param z				Pointer to struct z_stream_s 
 * \return	Number of bytes read.
 */
static int el_net_get_line(int s, char *str, int max, int compressed, struct z_stream_s *z)
{
	int nstr;
	static unsigned char buf[2048];
	unsigned char c;

	nstr = 0;
	for (;;) {
		if (el_net_get_index >= el_net_get_nread) {
			el_net_get_index = 0;
			el_net_get_nread = el_net_read(s, buf, sizeof(buf), compressed, z);
			if ((el_net_get_nread) < 1)
				return (el_net_get_nread);
		}
		if (buf[el_net_get_index] > 126)
			buf[el_net_get_index] = ' ';
		c = buf[el_net_get_index++];
		str[nstr++] = c & 0x7f;
		str[nstr] = 0;
		if (c < ' ')
			break;
		if (nstr >= max)
			break;
	}
	return (nstr);
}

/*!
 * \brief Echolink directory retriever.
 * \param hostname		Pointer to echolink server name to process.
 * \retval -1			Download failed.
 * \retval 0			Download was successful - received directory not compressed.
 * \retval 1			Download was successful - received directory was compressed.
 */
static int do_el_directory(char *hostname)
{
	struct ast_hostent ah;
	struct hostent *host;
	struct sockaddr_in dirserver;
	char str[200], ipaddr[200], nodenum[200];
	char call[200], *pp, *cc;
	int n = 0, rep_lines, delmode;
	int dir_compressed, dir_partial;
	struct z_stream_s z;
	int sock;

	sendcmd(hostname, instances[0]);
	el_net_get_index = 0;
	el_net_get_nread = 0;
	memset(&z, 0, sizeof(z));
	if (inflateInit(&z) != Z_OK) {
		if (z.msg)
			ast_log(LOG_ERROR, "Unable to init Zlib: %s\n", z.msg);
		else
			ast_log(LOG_ERROR, "Unable to init Zlib\n");
		return -1;
	}
	host = ast_gethostbyname(hostname, &ah);
	if (!host) {
		ast_log(LOG_ERROR, "Unable to resolve name for directory server %s\n", hostname);
		inflateEnd(&z);
		return -1;
	}
	memset(&dirserver, 0, sizeof(dirserver));	/* Clear struct */
	dirserver.sin_family = AF_INET;	/* Internet/IP */
	dirserver.sin_addr.s_addr = *(unsigned long *) host->h_addr_list[0];
	dirserver.sin_port = htons(EL_DIRECTORY_PORT);	/* server port */
	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		ast_log(LOG_ERROR, "Unable to obtain a socket for directory server %s\n", hostname);
		inflateEnd(&z);
		return -1;
	}
	/* Establish connection */
	if (connect(sock, (struct sockaddr *) &dirserver, sizeof(dirserver)) < 0) {
		ast_log(LOG_ERROR, "Unable to connect to directory server %s\n", hostname);
		inflateEnd(&z);
		return -1;
	}
	sprintf(str, "F%s\r", snapshot_id);
	if (send(sock, str, strlen(str), 0) < 0) {
		ast_log(LOG_ERROR, "Unable to send to directory server %s\n", hostname);
		close(sock);
		inflateEnd(&z);
		return -1;
	}
	str[strlen(str) - 1] = 0;
	ast_debug(5, "Sending: %s to %s\n", str, hostname);
	if (recv(sock, str, 4, 0) != 4) {
		ast_log(LOG_ERROR, "Error in directory download (header) on %s\n", hostname);
		close(sock);
		inflateEnd(&z);
		return -1;
	}
	dir_compressed = 1;
	dir_partial = 0;
	/* Determine if the response is full, partial or the stream is compressed.
	 * @@@ indicates a full directory, DDD indicates a partial directory.
	 * If we don't find one of these indicates, the stream is compressed.
	 * We will decompress the stream and test again.
	*/
	if (!strncmp(str, "@@@", 3)) {
		dir_partial = 0;
		dir_compressed = 0;
	} else if (!strncmp(str, "DDD", 3)) {
		dir_partial = 1;
		dir_compressed = 0;
	}
	if (dir_compressed) {
		if (el_net_get_line(sock, str, sizeof(str) - 1, dir_compressed, &z) < 1) {
			ast_log(LOG_ERROR, "Error in directory download (header) on %s\n", hostname);
			close(sock);
			inflateEnd(&z);
			return -1;
		}
		if (!strncmp(str, "@@@", 3)) {
			dir_partial = 0;
		} else if (!strncmp(str, "DDD", 3)) {
			dir_partial = 1;
		} else {
			ast_log(LOG_ERROR, "Error in header on %s\n", hostname);
			close(sock);
			inflateEnd(&z);
			return -1;
		}
	}
	/* read the header line with the line count and possibly the snapshot id */
	if (el_net_get_line(sock, str, sizeof(str) - 1, dir_compressed, &z) < 1) {
		ast_log(LOG_ERROR, "Error in directory download (header) on %s\n", hostname);
		close(sock);
		inflateEnd(&z);
		return -1;
	}
	if (dir_compressed) {
		if (sscanf(str, "%d:%s", &rep_lines, snapshot_id) < 2) {
			ast_log(LOG_ERROR, "Error in parsing header on %s\n", hostname);
			close(sock);
			inflateEnd(&z);
			return -1;
		}
	} else {
		if (sscanf(str, "%d", &rep_lines) < 1) {
			ast_log(LOG_ERROR, "Error in parsing header on %s\n", hostname);
			close(sock);
			inflateEnd(&z);
			return -1;
		}
	}
	delmode = 0;
	/* if the returned directory is not partial - we should
	 * delete all existing directory messages
	*/
	if (!dir_partial)
		el_zapem();
	/* 
	 *	process the directory entries 
	*/
	for (;;) {
		/* read the callsign line 
		 * this line could also contain the end of list identicator
		*/
		if (el_net_get_line(sock, str, sizeof(str) - 1, dir_compressed, &z) < 1)
			break;
		if (*str <= ' ')
			break;
		/* see if we are at the end of the current list */
		if (!strncmp(str, "+++", 3)) {
			if (delmode)
				break;
			if (!dir_partial)
				break;
			delmode = 1;
			continue;
		}
		if (str[strlen(str) - 1] == '\n')
			str[strlen(str) - 1] = 0;
		ast_copy_string(call, str, sizeof(call));
		if (dir_partial) {
			el_zapcall(call);
			if (delmode)
				continue;
		}
		/* read the location / status line (we will not use this line) */
		if (el_net_get_line(sock, str, sizeof(str) - 1, dir_compressed, &z) < 1) {
			ast_log(LOG_ERROR, "Error in directory download on %s\n", hostname);
			el_zapem();
			close(sock);
			inflateEnd(&z);
			return -1;
		}
		/* read the node number line */
		if (el_net_get_line(sock, str, sizeof(str) - 1, dir_compressed, &z) < 1) {
			ast_log(LOG_ERROR, "Error in directory download on %s\n", hostname);
			el_zapem();
			close(sock);
			inflateEnd(&z);
			return -1;
		}
		if (str[strlen(str) - 1] == '\n')
			str[strlen(str) - 1] = 0;
		ast_copy_string(nodenum, str, sizeof(nodenum));
		/* read the ip address line */
		if (el_net_get_line(sock, str, sizeof(str) - 1, dir_compressed, &z) < 1) {
			ast_log(LOG_ERROR, "Error in directory download on %s\n", hostname);
			el_zapem();
			close(sock);
			inflateEnd(&z);
			return -1;
		}
		if (str[strlen(str) - 1] == '\n')
			str[strlen(str) - 1] = 0;
		ast_copy_string(ipaddr, str, sizeof(ipaddr));
		/* every 10 records, sleep for a short time */
		if (!(n % 10))
			usleep(2000);		/* To get to dry land */
		/* add this entry to our table */
		ast_mutex_lock(&el_db_lock);
		el_db_put(nodenum, ipaddr, call);
		ast_mutex_unlock(&el_db_lock);
		n++;
	}
	close(sock);
	inflateEnd(&z);
	pp = (dir_partial) ? "partial" : "full";
	cc = (dir_compressed) ? "compressed" : "un-compressed";
	ast_verb(4, "Directory pgm done downloading(%s,%s), %d records\n", pp, cc, n);
	if (dir_compressed)
		ast_debug(2, "Got snapshot_id: %s\n", snapshot_id);
	return (dir_compressed);
}

/*!
 * \brief Echolink directory retriever thread.
 * This thread is responsible for retreiving a directory of user registrations from
 * the echolink servers.  This is necessary to validate connecting users and
 * have the ip address available for outbound connections.
 *
 * It sequentially processes the echolink server list with each request cycle.
 * If the directory download fails, the routine waits 20 seconds and
 * moves to the next server.
 *
 * If a compressed directory was received, the routine will wait 240 seconds
 * before retrieving the directory again.  If the received directory
 * was not compressed, the routine will wait 1800 seconds before retrieving
 * the directory again.
 * \param data		Data passed to this thread.  (NULL is passed for all calls).
 */
static void *el_directory(void *data)
{
	int rc = 0, curdir;
	time_t then, now;
	curdir = 0;
	time(&then);
	while (run_forever) {
		time(&now);
		el_sleeptime -= (now - then);
		then = now;
		if (el_sleeptime < 0)
			el_sleeptime = 0;
		if (el_sleeptime) {
			usleep(200000);
			continue;
		}
		if (!instances[0]->elservers[curdir][0]) {
			if (++curdir >= EL_MAX_SERVERS)
				curdir = 0;
			continue;
		}
		ast_debug(2, "Trying to do directory download Echolink server %s\n", instances[0]->elservers[curdir]);
		rc = do_el_directory(instances[0]->elservers[curdir]);
		if (rc < 0) {
			if (++curdir >= EL_MAX_SERVERS)
				curdir = 0;
			el_sleeptime = 20;
			continue;
		}
		if (rc == 1)
			el_sleeptime = 240;
		else if (rc == 0)
			el_sleeptime = 1800;
	}
	ast_debug(1, "Echolink directory thread exited.\n");
	return NULL;
}

/*!
 * \brief Echolink registration thread.
 * This thread is responsible for registering an instance with
 * the echolink servers.
 * This routine generally runs every 360 seconds.
 * \param data		Pointer to struct el_instance data passsed to this thread.
 */
static void *el_register(void *data)
{
	short i = 0;
	int rc = 0;
	struct el_instance *instp = (struct el_instance *) data;
	time_t then, now;

	time(&then);
	ast_debug(1, "Echolink registration thread started on %s.\n", instp->name);
	while (run_forever) {
		time(&now);
		el_login_sleeptime -= (now - then);
		then = now;
		if (el_login_sleeptime < 0)
			el_login_sleeptime = 0;
		if (el_login_sleeptime) {
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
			ast_debug(2, "Trying to register with Echolink server %s\n", instp->elservers[i]);
			rc = sendcmd(instp->elservers[i++], instp);
		}
		if (rc == 0)
			el_login_sleeptime = 360;
		else
			el_login_sleeptime = 20;
	}
	/* Send a de-register message, but what is the point, Echolink deactivates this node within 6 minutes */
	ast_debug(1, "Echolink registration thread exited.\n");
	return NULL;
}

/*!
 * \brief Process a new echolink call.
 * \param instp			Pointer to echolink instance.
 * \param p				Pointer to echolink private data.
 * \param call			Pointer to callsign.
 * \param name			Pointer to name associated with the callsign.
 * \retval 1 			if not successful.
 * \retval 0 			if successful.
 * \retval -1			if memory allocation error.
 */
static int do_new_call(struct el_instance *instp, struct el_pvt *p, char *call, char *name)
{
	struct el_node *el_node_key = NULL;
	struct eldb *mynode;
	char nodestr[30];
	time_t now;

	el_node_key = (struct el_node *) ast_calloc(1, sizeof(struct el_node));
	if (el_node_key) {
		ast_copy_string(el_node_key->call, call, EL_CALL_SIZE);
		ast_copy_string(el_node_key->ip, instp->el_node_test.ip, EL_IP_SIZE);
		ast_copy_string(el_node_key->name, name, EL_NAME_SIZE);

		mynode = el_db_find_ipaddr(el_node_key->ip);
		if (!mynode) {
			ast_log(LOG_ERROR, "Cannot find DB entry for IP addr %s\n", el_node_key->ip);
			ast_free(el_node_key);
			return 1;
		}
		ast_copy_string(nodestr, mynode->nodenum, sizeof(nodestr));
		el_node_key->nodenum = atoi(nodestr);
		el_node_key->countdown = instp->rtcptimeout;
		el_node_key->seqnum = 1;
		el_node_key->instp = instp;
		if (tsearch(el_node_key, &el_node_list, compare_ip)) {
			ast_debug(1, "new CALL=%s,ip=%s,name=%s\n", el_node_key->call, el_node_key->ip, el_node_key->name);
			if (p == NULL) {	/* if a new inbound call */
				p = el_alloc((void *) instp->name);
				if (!p) {
					ast_log(LOG_ERROR, "Cannot alloc el channel\n");
					return -1;
				}
				el_node_key->p = p;
				ast_copy_string(el_node_key->p->ip, instp->el_node_test.ip, EL_IP_SIZE);
				el_node_key->chan = el_new(el_node_key->p, AST_STATE_RINGING, el_node_key->nodenum, NULL, NULL);
				if (!el_node_key->chan) {
					el_destroy(el_node_key->p);
					return -1;
				}
				ast_mutex_lock(&instp->lock);
				time(&now);
				if (instp->starttime < (now - EL_APRS_START_DELAY))
					instp->aprstime = now;
				ast_mutex_unlock(&instp->lock);
			} else {
				el_node_key->p = p;
				ast_copy_string(el_node_key->p->ip, instp->el_node_test.ip, EL_IP_SIZE);
				el_node_key->chan = p->owner;
				el_node_key->outbound = 1;
				ast_mutex_lock(&instp->lock);
				strcpy(instp->lastcall, mynode->callsign);
				time(&instp->lasttime);
				time(&now);
				instp->lasttime = now;
				if (instp->starttime < (now - EL_APRS_START_DELAY)) {
					instp->aprstime = now;
				}
				ast_mutex_unlock(&instp->lock);
			}
		} else {
			ast_log(LOG_ERROR, "tsearch() failed to add CALL=%s,ip=%s,name=%s\n",
					el_node_key->call, el_node_key->ip, el_node_key->name);
			ast_free(el_node_key);
			return -1;
		}
	} else {
		ast_log(LOG_ERROR, "calloc() failed for new CALL=%s, ip=%s\n", call, instp->el_node_test.ip);
		return -1;
	}
	return 0;
}

/*!
 * \brief This routine watches the UDP ports for activity.
 * It runs in its own thread and processes RTP / RTCP packets as they arrive.
 * One thread is required for each echolink instance.
 * It receives data from the audio socket and control socket.
 * Connection requests arrive over the control socket.
 * \param data		Pointer to struct el_instance data passed this this thread
 */
static void *el_reader(void *data)
{
	struct el_instance *instp = (struct el_instance *) data;
	char buf[1024];
	unsigned char bye[40];
	struct sockaddr_in sin, sin1;
	int i, j, x;
	struct el_rxqast *qpast;
	struct ast_frame fr;
	socklen_t fromlen;
	ssize_t recvlen;
	time_t now, was;
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
	ast_debug(1, "Echolink reader thread started on %s.\n", instp->name);
	ast_mutex_lock(&instp->lock);
	while (run_forever) {

		/* Send APRS information every EL_APRS_INTERVAL */
		time(&now);
		if (instp->aprstime <= now) {
			char aprsstr[512], aprscall[256], latc, lonc;
			unsigned char sdes_packet[256];
			unsigned int u;
			float lata, lona, latb, lonb, latd, lond, lat, lon, mylat, mylon;
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
			if (!j) {			/* if no outbound users */
				snprintf(instp->login_display, EL_NAME_SIZE + EL_CALL_SIZE,
						 "%s [%d/%d]", instp->myqth, i, instp->maxstns);
				snprintf(instp->aprs_display, EL_APRS_SIZE,
						 " On @ %02d%02d [%d/%d]", tm->tm_hour, tm->tm_min, i, instp->maxstns);
			} else {
				snprintf(instp->login_display, EL_NAME_SIZE + EL_CALL_SIZE, "In Conference %s", instp->lastcall);
				snprintf(instp->aprs_display, EL_APRS_SIZE,
						 "=N%s @ %02d%02d", instp->lastcall, tm->tm_hour, tm->tm_min);
			}
			mylat = instp->lat;
			mylon = instp->lon;
			fp = fopen(GPSFILE, "r");
			if (fp && (fstat(fileno(fp), &mystat) != -1) && (mystat.st_size < 100)) {
				if (fscanf(fp, "%u %f%c %f%c", &u, &lat, &latc, &lon, &lonc) == 5) {
					was = (time_t) u;
					if ((was + GPS_VALID_SECS) >= now) {
						mylat = floor(lat / 100.0);
						mylat += (lat - (mylat * 100)) / 60.0;
						mylon = floor(lon / 100.0);
						mylon += (lon - (mylon * 100)) / 60.0;
						if (latc == 'S')
							mylat = -mylat;
						if (lonc == 'W')
							mylon = -mylon;
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
			sprintf(aprsstr, ")EL-%-6.6s!%02d%02d.%02d%cE%03d%02d.%02d%c0PHG%d%d%d%d/%06d/%03d%s", instp->mycall,
					(int) lata, (int) latb, (int) latd, latc,
					(int) lona, (int) lonb, (int) lond, lonc,
					instp->power, instp->height, instp->gain, instp->dir,
					(int) ((instp->freq * 1000) + 0.5), (int) (instp->tone + 0.05), instp->aprs_display);

			ast_debug(5, "aprs out: %s\n", aprsstr);
			sprintf(aprscall, "%s/%s", instp->mycall, instp->mycall);
			memset(sdes_packet, 0, sizeof(sdes_packet));
			sdes_length = rtcp_make_el_sdes(sdes_packet, sizeof(sdes_packet), aprscall, aprsstr);
			sendto(instp->ctrl_sock, sdes_packet, sdes_length, 0, (struct sockaddr *) &sin_aprs, sizeof(sin_aprs));
			el_sleeptime = 0;
			el_login_sleeptime = 0;
		}
		ast_mutex_unlock(&instp->lock);
#if 0
		/*! \todo This causes gcc to complain:
		 * cc1: error: '__builtin_memset' writing 4096 bytes into a region of size 256 overflows the destination [-Werror=stringop-overflow=]
		 * In practice, we should probably be using poll instead of select anyways, not least because of this...
		 */
		FD_ZERO(fds);
#else
		memset(&fds, 0, sizeof(fds));
#endif
		FD_SET(instp->audio_sock, fds);
		FD_SET(instp->ctrl_sock, fds);
		x = instp->audio_sock;
		if (instp->ctrl_sock > x)
			x = instp->ctrl_sock;
		tmout.tv_sec = 0;
		tmout.tv_usec = 50000;
		i = select(x + 1, fds, NULL, NULL, &tmout);
		if (i == 0) {
			ast_mutex_lock(&instp->lock);
			continue;
		}
		if (i < 0) {
			ast_log(LOG_ERROR, "Error in select()\n");
			mythread_exit(NULL);
		}
		ast_mutex_lock(&instp->lock);
		if (FD_ISSET(instp->ctrl_sock, fds)) {	/* if a ctrl packet */
			fromlen = sizeof(struct sockaddr_in);
			recvlen = recvfrom(instp->ctrl_sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *) &sin, &fromlen);
			if (recvlen > 0) {
				buf[recvlen] = '\0';
				ast_copy_string(instp->el_node_test.ip, ast_inet_ntoa(sin.sin_addr), EL_IP_SIZE);
				if (is_rtcp_sdes((unsigned char *) buf, recvlen)) {
					call_name[0] = '\0';
					items.nitems = 1;
					items.item[0].r_item = 2;
					items.item[0].r_text = NULL;
					parse_sdes((unsigned char *) buf, &items);
					if (items.item[0].r_text != NULL)
						copy_sdes_item(items.item[0].r_text, call_name, 127);
					if (call_name[0] != '\0') {
						call = call_name;
						ptr = strchr(call_name, (int) ' ');
						name = "UNKNOWN";
						if (ptr) {
							*ptr = '\0';
							name = ptr + 1;
						}
						found_key = (struct el_node **) tfind(&instp->el_node_test, &el_node_list, compare_ip);
						if (found_key) {
							if (!(*found_key)->p->firstheard) {
								(*found_key)->p->firstheard = 1;
								memset(&fr, 0, sizeof(fr));
								fr.datalen = 0;
								fr.samples = 0;
								fr.frametype = AST_FRAME_CONTROL;
								fr.subclass.integer = AST_CONTROL_ANSWER;
								fr.data.ptr = 0;
								fr.src = type;
								fr.offset = 0;
								fr.mallocd = 0;
								fr.delivery.tv_sec = 0;
								fr.delivery.tv_usec = 0;
								ast_queue_frame((*found_key)->chan, &fr);
								ast_debug(1, "Channel %s answering\n", ast_channel_name((*found_key)->chan));
							}
							(*found_key)->countdown = instp->rtcptimeout;
							/* different callsigns behind a NAT router, running -L, -R, ... */
							if (strncmp((*found_key)->call, call, EL_CALL_SIZE) != 0) {
								ast_verb(4, "Call changed from %s to %s\n", (*found_key)->call, call);
								ast_copy_string((*found_key)->call, call, EL_CALL_SIZE);
							}
							if (strncmp((*found_key)->name, name, EL_NAME_SIZE) != 0) {
								ast_verb(4, "Name changed from %s to %s\n", (*found_key)->name, name);
								ast_copy_string((*found_key)->name, name, EL_NAME_SIZE);
							}
						} else {	/* otherwise its a new request */
							i = 0;	/* default authorized */
							if (instp->ndenylist) {
								for (x = 0; x < instp->ndenylist; x++) {
									if (!fnmatch(instp->denylist[x], call, FNM_CASEFOLD)) {
										i = 1;
										break;
									}
								}
							} else {
								/* if permit list specified, default is not to authorize */
								if (instp->npermitlist)
									i = 1;
							}
							if (instp->npermitlist) {
								for (x = 0; x < instp->npermitlist; x++) {
									if (!fnmatch(instp->permitlist[x], call, FNM_CASEFOLD)) {
										i = 0;
										break;
									}
								}
							}
							if (!i) {	/* if authorized */
								i = do_new_call(instp, NULL, call, name);
								if (i < 0) {
									ast_mutex_unlock(&instp->lock);
									mythread_exit(NULL);
								}
							}
							if (i) {	/* if not authorized or do_new_call failed*/
								/* first, see if we have one that is ours and not abandoned */
								for (x = 0; x < MAXPENDING; x++) {
									if (strcmp(instp->pending[x].fromip, instp->el_node_test.ip))
										continue;
									if (ast_tvdiff_ms(ast_tvnow(), instp->pending[x].reqtime) < AUTH_ABANDONED_MS)
										break;
								}
								if (x < MAXPENDING) {
									/* if its time, send un-auth */
									if (ast_tvdiff_ms(ast_tvnow(), instp->pending[x].reqtime) >= AUTH_RETRY_MS) {
										ast_debug(1, "Sent bye to IP address %s\n", instp->el_node_test.ip);
										j = rtcp_make_bye(bye, "UN-AUTHORIZED");
										sin1.sin_family = AF_INET;
										sin1.sin_addr.s_addr = inet_addr(instp->el_node_test.ip);
										sin1.sin_port = htons(instp->ctrl_port);
										for (i = 0; i < 20; i++) {
											sendto(instp->ctrl_sock, bye, j,
												   0, (struct sockaddr *) &sin1, sizeof(sin1));
										}
										instp->pending[x].fromip[0] = 0;
									}
									time(&now);
									if (instp->starttime < (now - EL_APRS_START_DELAY))
										instp->aprstime = now;
								} else {	/* find empty one */
									for (x = 0; x < MAXPENDING; x++) {
										if (!instp->pending[x].fromip[0])
											break;
										if (ast_tvdiff_ms(ast_tvnow(), instp->pending[x].reqtime) >= AUTH_ABANDONED_MS)
											break;
									}
									if (x < MAXPENDING) {	/* we found one */
										strcpy(instp->pending[x].fromip, instp->el_node_test.ip);
										instp->pending[x].reqtime = ast_tvnow();
										time(&now);
										if (instp->starttime < (now - EL_APRS_START_DELAY))
											instp->aprstime = now;
										else {
											el_sleeptime = 0;
											el_login_sleeptime = 0;
										}
									} else {
										ast_log(LOG_ERROR, "Cannot find open pending echolink request slot for IP %s\n",
												instp->el_node_test.ip);
									}
								}
							}
							twalk(el_node_list, send_info);
						}
					}
				} else {
					if (is_rtcp_bye((unsigned char *) buf, recvlen)) {
						if (find_delete(&instp->el_node_test))
							ast_verb(4, "disconnect from ip=%s\n", instp->el_node_test.ip);
					}
				}
			}
		}
		if (FD_ISSET(instp->audio_sock, fds)) {	/* if an audio packet */
			fromlen = sizeof(struct sockaddr_in);
			recvlen = recvfrom(instp->audio_sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *) &sin, &fromlen);
			if (recvlen > 0) {
				buf[recvlen] = '\0';
				ast_copy_string(instp->el_node_test.ip, ast_inet_ntoa(sin.sin_addr), EL_IP_SIZE);
				/* packets that start with 0x6f are text packets */
				if (buf[0] == 0x6f) {
					process_cmd(buf, instp->el_node_test.ip, instp);
				} else {
					found_key = (struct el_node **) tfind(&instp->el_node_test, &el_node_list, compare_ip);
					if (found_key) {
						struct el_pvt *p = (*found_key)->p;

						if (!(*found_key)->p->firstheard) {
							(*found_key)->p->firstheard = 1;
							memset(&fr, 0, sizeof(fr));
							fr.datalen = 0;
							fr.samples = 0;
							fr.frametype = AST_FRAME_CONTROL;
							fr.subclass.integer = AST_CONTROL_ANSWER;
							fr.data.ptr = 0;
							fr.src = type;
							fr.offset = 0;
							fr.mallocd = 0;
							fr.delivery.tv_sec = 0;
							fr.delivery.tv_usec = 0;
							ast_queue_frame((*found_key)->chan, &fr);
							ast_verb(3, "Channel %s answering\n", ast_channel_name((*found_key)->chan));
						}
						(*found_key)->countdown = instp->rtcptimeout;
						if (recvlen == sizeof(struct gsmVoice_t)) {
							if ((((struct gsmVoice_t *) buf)->version == 3) && (((struct gsmVoice_t *) buf)->payt == 3)) {
								/* break them up for Asterisk */
								for (i = 0; i < BLOCKING_FACTOR; i++) {
									qpast = ast_malloc(sizeof(struct el_rxqast));
									if (!qpast) {
										ast_mutex_unlock(&instp->lock);
										mythread_exit(NULL);
									}
									memcpy(qpast->buf, ((struct gsmVoice_t *) buf)->data +
										   (GSM_FRAME_SIZE * i), GSM_FRAME_SIZE);
									insque((struct qelem *) qpast, (struct qelem *)
										   p->rxqast.qe_back);
								}
							}
						}
					}
				}
			}
		}
	}
	ast_mutex_unlock(&instp->lock);
	ast_debug(1, "Echolink read thread exited.\n");
	return NULL;
}

/*!
 * \brief Stores the information from the configuration file to memory.
 * It setup up udp sockets for the echolink ports and starts background 
 * threads for processing connections for this instance and registration.
 * \param cfg		Pointer to struct ast_config.
 * \param ctg		Pointer to category to load.
 * \retval 0		If configuration load is successful.
 * \retval -1		If configuration load fails.
 */
static int store_config(struct ast_config *cfg, char *ctg)
{
	char *val;
	struct hostent *ahp;
	struct ast_hostent ah;
	struct el_instance *instp;
	struct sockaddr_in si_me;
	int serverindex;
	char servername[9];

	if (ninstances >= EL_MAX_INSTANCES) {
		ast_log(LOG_ERROR, "Too many instances specified\n");
		return -1;
	}

	instp = ast_calloc(1, sizeof(struct el_instance));
	if (!instp) {
		return -1;
	}

	ast_mutex_init(&instp->lock);
	instp->audio_sock = -1;
	instp->ctrl_sock = -1;
	instp->fdr = -1;

	val = (char *) ast_variable_retrieve(cfg, ctg, "ipaddr");
	if (val) {
		ast_copy_string(instp->ipaddr, val, EL_IP_SIZE);
	} else {
		strcpy(instp->ipaddr, "0.0.0.0");
	}

	val = (char *) ast_variable_retrieve(cfg, ctg, "port");
	if (val) {
		ast_copy_string(instp->port, val, EL_IP_SIZE);
	} else {
		strcpy(instp->port, "5198");
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "maxstns");
	if (!val)
		instp->maxstns = 50;
	else
		instp->maxstns = atoi(val);

	val = (char *) ast_variable_retrieve(cfg, ctg, "rtcptimeout");
	if (!val)
		instp->rtcptimeout = 15;
	else
		instp->rtcptimeout = atoi(val);

	val = (char *) ast_variable_retrieve(cfg, ctg, "node");
	if (!val)
		instp->mynode = 0;
	else
		instp->mynode = atol(val);

	val = (char *) ast_variable_retrieve(cfg, ctg, "astnode");
	if (val) {
		ast_copy_string(instp->astnode, val, EL_NAME_SIZE);
	} else {
		strcpy(instp->astnode, "1999");
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "context");
	if (val) {
		ast_copy_string(instp->context, val, EL_NAME_SIZE);
	} else {
		strcpy(instp->context, "echolink-in");
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "call");
	if (!val)
		ast_copy_string(instp->mycall, "INVALID", EL_CALL_SIZE);
	else
		ast_copy_string(instp->mycall, val, EL_CALL_SIZE);

	if (strcmp(instp->mycall, "INVALID") == 0) {
		ast_log(LOG_ERROR, "INVALID Echolink call");
		return -1;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "name");
	if (!val)
		ast_copy_string(instp->myname, instp->mycall, EL_NAME_SIZE);
	else
		ast_copy_string(instp->myname, val, EL_NAME_SIZE);

	val = (char *) ast_variable_retrieve(cfg, ctg, "recfile");
	if (!val)
		ast_copy_string(instp->fdr_file, "/tmp/echolink_recorded.gsm", FILENAME_MAX - 1);
	else
		ast_copy_string(instp->fdr_file, val, FILENAME_MAX);

	val = (char *) ast_variable_retrieve(cfg, ctg, "pwd");
	if (!val)
		ast_copy_string(instp->mypwd, "INVALID", EL_PWD_SIZE);
	else
		ast_copy_string(instp->mypwd, val, EL_PWD_SIZE);

	val = (char *) ast_variable_retrieve(cfg, ctg, "qth");
	if (!val)
		ast_copy_string(instp->myqth, "INVALID", EL_QTH_SIZE);
	else
		ast_copy_string(instp->myqth, val, EL_QTH_SIZE);

	val = (char *) ast_variable_retrieve(cfg, ctg, "email");
	if (!val)
		ast_copy_string(instp->myemail, "INVALID", EL_EMAIL_SIZE);
	else
		ast_copy_string(instp->myemail, val, EL_EMAIL_SIZE);

	for (serverindex = 0; serverindex < EL_MAX_SERVERS; serverindex++) {
		snprintf(servername, sizeof(servername), "server%i", serverindex + 1);
		val = (char *) ast_variable_retrieve(cfg, ctg, servername);
		if (!val) {
			instp->elservers[serverindex][0] = '\0';
		} else {
			ast_copy_string(instp->elservers[serverindex], val, EL_SERVERNAME_SIZE);
		}
	}

	val = (char *) ast_variable_retrieve(cfg, ctg, "deny");
	if (val)
		instp->ndenylist = finddelim(ast_strdup(val), instp->denylist, EL_MAX_CALL_LIST);

	val = (char *) ast_variable_retrieve(cfg, ctg, "permit");
	if (val)
		instp->npermitlist = finddelim(ast_strdup(val), instp->permitlist, EL_MAX_CALL_LIST);

	val = (char *) ast_variable_retrieve(cfg, ctg, "lat");
	if (val)
		instp->lat = strtof(val, NULL);
	else
		instp->lat = 0.0;

	val = (char *) ast_variable_retrieve(cfg, ctg, "lon");
	if (val)
		instp->lon = strtof(val, NULL);
	else
		instp->lon = 0.0;

	val = (char *) ast_variable_retrieve(cfg, ctg, "freq");
	if (val)
		instp->freq = strtof(val, NULL);
	else
		instp->freq = 0.0;

	val = (char *) ast_variable_retrieve(cfg, ctg, "tone");
	if (val)
		instp->tone = strtof(val, NULL);
	else
		instp->tone = 0.0;

	val = (char *) ast_variable_retrieve(cfg, ctg, "power");
	if (val)
		instp->power = (char) strtol(val, NULL, 0);
	else
		instp->power = 0;

	val = (char *) ast_variable_retrieve(cfg, ctg, "height");
	if (val)
		instp->height = (char) strtol(val, NULL, 0);
	else
		instp->height = 0;

	val = (char *) ast_variable_retrieve(cfg, ctg, "gain");
	if (val)
		instp->gain = (char) strtol(val, NULL, 0);
	else
		instp->gain = 0;

	val = (char *) ast_variable_retrieve(cfg, ctg, "dir");
	if (val)
		instp->dir = (char) strtol(val, NULL, 0);
	else
		instp->dir = 0;

	instp->audio_sock = -1;
	instp->ctrl_sock = -1;

	if ((strncmp(instp->mypwd, "INVALID", EL_PWD_SIZE) == 0) || (strncmp(instp->mycall, "INVALID", EL_CALL_SIZE) == 0)) {
		ast_log(LOG_ERROR, "Your Echolink call or password is not right\n");
		return -1;
	}
	if ((instp->elservers[0][0] == '\0') || (instp->elservers[1][0] == '\0') || (instp->elservers[2][0] == '\0')) {
		ast_log(LOG_ERROR, "One of the Echolink servers missing\n");
		return -1;
	}
	if ((instp->audio_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		ast_log(LOG_WARNING, "Unable to create new socket for echolink audio connection\n");
		return -1;
	}
	if ((instp->ctrl_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		ast_log(LOG_WARNING, "Unable to create new socket for echolink control connection\n");
		close(instp->audio_sock);
		instp->audio_sock = -1;
		return -1;
	}
	memset((char *) &si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
	if (strcmp(instp->ipaddr, "0.0.0.0") == 0)
		si_me.sin_addr.s_addr = htonl(INADDR_ANY);
	else
		si_me.sin_addr.s_addr = inet_addr(instp->ipaddr);
	instp->audio_port = atoi(instp->port);
	si_me.sin_port = htons(instp->audio_port);
	if (bind(instp->audio_sock, &si_me, sizeof(si_me)) == -1) {
		ast_log(LOG_WARNING, "Unable to bind port for echolink audio connection\n");
		close(instp->ctrl_sock);
		instp->ctrl_sock = -1;
		close(instp->audio_sock);
		instp->audio_sock = -1;
		return -1;
	}
	instp->ctrl_port = instp->audio_port + 1;
	si_me.sin_port = htons(instp->ctrl_port);
	if (bind(instp->ctrl_sock, &si_me, sizeof(si_me)) == -1) {
		ast_log(LOG_WARNING, "Unable to bind port for echolink control connection\n");
		close(instp->ctrl_sock);
		instp->ctrl_sock = -1;
		close(instp->audio_sock);
		instp->audio_sock = -1;
		return -1;
	}
	fcntl(instp->audio_sock, F_SETFL, O_NONBLOCK);
	fcntl(instp->ctrl_sock, F_SETFL, O_NONBLOCK);
	ast_copy_string(instp->name, ctg, EL_NAME_SIZE);
	sin_aprs.sin_family = AF_INET;
	sin_aprs.sin_port = htons(5199);
	ahp = ast_gethostbyname(EL_APRS_SERVER, &ah);
	if (!ahp) {
		ast_log(LOG_ERROR, "Unable to resolve echolink APRS server IP address\n");
		close(instp->ctrl_sock);
		instp->ctrl_sock = -1;
		close(instp->audio_sock);
		instp->audio_sock = -1;
		return -1;
	}
	memcpy(&sin_aprs.sin_addr.s_addr, ahp->h_addr, sizeof(in_addr_t));
	ast_pthread_create(&el_register_thread, NULL, el_register, (void *) instp);
	ast_pthread_create_detached(&instp->el_reader_thread, NULL, el_reader, (void *) instp);
	instances[ninstances++] = instp;

	ast_debug(1, "Echolink/%s listening on %s port %s\n", instp->name, instp->ipaddr, instp->port);
	ast_debug(1, "Echolink/%s node capacity set to %d node(s)\n", instp->name, instp->maxstns);
	ast_debug(1, "Echolink/%s heartbeat timeout set to %d heartbeats\n", instp->name, instp->rtcptimeout);
	ast_debug(1, "Echolink/%s node set to %u\n", instp->name, instp->mynode);
	ast_debug(1, "Echolink/%s call set to %s\n", instp->name, instp->mycall);
	ast_debug(1, "Echolink/%s name set to %s\n", instp->name, instp->myname);
	ast_debug(1, "Echolink/%s file for recording set to %s\n", instp->name, instp->fdr_file);
	ast_debug(1, "Echolink/%s  qth set to %s\n", instp->name, instp->myqth);
	ast_debug(1, "Echolink/%s emailID set to %s\n", instp->name, instp->myemail);
	return 0;
}

static int unload_module(void)
{
	int n;

	run_forever = 0;
	if (el_node_list) {
		tdestroy(el_node_list, free_node);
	}

	ast_debug(1, "We have %d Echolink instance%s\n", ninstances, ESS(ninstances));
	for (n = 0; n < ninstances; n++) {
		ast_debug(2, "Closing Echolink instance %d\n", n);
		if (instances[n]->audio_sock != -1) {
			close(instances[n]->audio_sock);
			instances[n]->audio_sock = -1;
		}
		if (instances[n]->ctrl_sock != -1) {
			close(instances[n]->ctrl_sock);
			instances[n]->ctrl_sock = -1;
		}
		if (instances[n]->el_reader_thread) {
			pthread_join(instances[n]->el_reader_thread, NULL);
		}
	}

	/* Wait for all threads to exit */
	pthread_join(el_directory_thread, NULL);
	pthread_join(el_register_thread, NULL);

	ast_cli_unregister_multiple(el_cli, sizeof(el_cli) / sizeof(struct ast_cli_entry));
	/* First, take us out of the channel loop */
	ast_channel_unregister(&el_tech);
	ao2_cleanup(el_tech.capabilities);
	el_tech.capabilities = NULL;

	for (n = 0; n < ninstances; n++)
		ast_free(instances[n]);
	if (nullfd != -1)
		close(nullfd);
	return 0;
}

 static int load_module(void)
{
	struct ast_config *cfg = NULL;
	char *ctg = NULL;
	struct ast_flags zeroflag = { 0 };

	if (!(cfg = ast_config_load(config, zeroflag))) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!(el_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append(el_tech.capabilities, ast_format_gsm, 0);

	while ((ctg = ast_category_browse(cfg, ctg)) != NULL) {
		if (ctg == NULL)
			continue;
		if (store_config(cfg, ctg) < 0)
			return AST_MODULE_LOAD_DECLINE;
	}
	ast_config_destroy(cfg);
	cfg = NULL;
	ast_verb(4, "Total of %d Echolink instances found\n", ninstances);
	if (ninstances < 1) {
		ast_log(LOG_ERROR, "Cannot run echolink with no instances\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_pthread_create(&el_directory_thread, NULL, el_directory, NULL);
	ast_cli_register_multiple(el_cli, sizeof(el_cli) / sizeof(struct ast_cli_entry));
	/* Make sure we can register our channel type */
	if (ast_channel_register(&el_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		return AST_MODULE_LOAD_DECLINE;
	}
	nullfd = open("/dev/null", O_RDWR);
	return 0;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Echolink Channel Driver");
