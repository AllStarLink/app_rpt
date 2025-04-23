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

---> recognizes a few remote text commands.
---> no busy, deaf or mute.
---> no admin list, only local 127.0.0.1 access.
---> no event notififications.
---> no loop detection.

Default ports are 5198,5199.

Remote text commands thru netcat:
o.conip <IPaddress>    (request a connect)
o.dconip <IPaddress>   (request a disconnect)
o.rec                  (turn on/off recording)

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
 *	SSS is the last snap shot id received.  This will initially be a zero length string.
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
 *
 * Note:  The relay mode used in the Android and iOS clients causes a problem with the directory
 * database.  The relay server uses one ip address for multiple callsigns. Because we only allow
 * one entry per IP address, some entries are deleted that otherwise would be valid.  Clients
 * associated with a relay server cannot accept connections.  In addition, the node number used
 * by the relay server is generated by the relay server.  It does not match the actual station's
 * assigned node number. In some cases, it matches valid node numbers.  We address this issue
 * by looking up dbget requests in our connected nodes list first.
 *
*/

#include "asterisk.h"

#include <search.h>
#include <zlib.h>
#include <fnmatch.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/network.h"	/* socket routines */
#include "asterisk/pbx.h"
#include "asterisk/dsp.h"
#include "asterisk/translate.h"
#include "asterisk/cli.h"
#include "asterisk/format_cache.h"

#define MAX_RXKEY_TIME 4 /* 50 * 10 * 20ms iax2 = 10,000ms = 10 seconds heartbeat */
#define KEEPALIVE_TIME 50 * 10
#define AUTH_RETRY_MS 5000
#define AUTH_ABANDONED_MS 15000
#define BLOCKING_FACTOR 4
#define GSM_FRAME_SIZE 33
#define QUEUE_OVERLOAD_THRESHOLD_AST 75
#define QUEUE_OVERLOAD_THRESHOLD_EL 30
#define MAXPENDING 20
#define AUDIO_TIMEOUT 800 /*Audio timeout in milliseconds */
#define RPT_TO_STRING(x) #x
#define S_FMT(x) "%" RPT_TO_STRING(x) "s "
#define N_FMT(duf) "%30" #duf /* Maximum sscanf conversion to numeric strings */
#define EL_IP_SIZE 16
#define EL_CALL_SIZE 12
#define EL_NAME_SIZE 32
#define EL_MESSAGE_SIZE 256
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

/*! \brief Echolink directory server port number */
#define	EL_DIRECTORY_PORT 5200

#define	GPS_VALID_SECS 60

#define	ELDB_NODENUMLEN 8
#define	ELDB_CALLSIGNLEN 12
#define	ELDB_IPADDRLEN 16

#define	DELIMCHR ','
#define	QUOTECHR 34

#define EL_WELCOME_MESSAGE "oNDATA\rWelcome to AllStar Node %s\rEcholink Node %s\rNumber %u\r \r"
#define EL_INIT_BUFFER sizeof(EL_WELCOME_MESSAGE) \
		+ 16  		/* ASL & EL nodes #'s */  \
		+ 80  		/* room for "a" message */ \
		+ (50 * 8) 	/* room for linked ASL and EL nodes */
/* 
 * If you want to compile/link this code
 * on "BIG-ENDIAN" platforms, then
 * use this: #define RTP_BIG_ENDIAN
 * Have only tested this code on "little-endian"
 * platforms running Linux.
*/
static const char tdesc[] = "Echolink channel driver";
static char type[] = "echolink";
#define SNAPSHOT_SZ 49
static char snapshot_id[SNAPSHOT_SZ + 1] = { '0', 0 };

static int el_net_get_index = 0;
static int el_net_get_nread = 0;

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

/* forward definitions */
struct el_instance;
struct el_pvt;

/*!
 * \brief Echolink connected node struct.
 * These are stored internally in a binary tree.
 */
struct el_node {
	char ip[EL_IP_SIZE];
	char call[EL_CALL_SIZE];
	char name[EL_NAME_SIZE];
	char outbound;
	unsigned int nodenum;
	short countdown;
	uint16_t seqnum;
	float jitter;
	struct el_instance *instp;
	struct el_pvt *pvt;
	struct timeval last_packet_time;
	uint32_t rx_audio_packets;
	uint32_t tx_audio_packets;
	uint32_t rx_ctrl_packets;
	uint32_t tx_ctrl_packets;
	char istimedout;
	char isdoubling;
};

/*!
 * \brief Pending connections struct.
 * This holds the incoming connection that is not authorized.
 */
struct el_pending {
	char fromip[EL_IP_SIZE];
	struct timeval reqtime;
};

/*!
 * \brief Count of connections struct.
 * This is used by twalk_r to count the connections.
 */
 struct el_node_count {
	struct el_instance *instp;
	int inbound;
	int outbound;
 };

 /*!
 * \brief Lookup for el_node callsign.
 * This is used by twalk_r to look up a node.
 */
 struct el_node_lookup_callsign {
	int nodenum;
	char callsign[EL_CALL_SIZE];
 	char ipaddr[EL_IP_SIZE];
};

/*!
 * \brief Echolink instance struct.
 */
struct el_instance {
	ast_mutex_t lock;
	char name[EL_NAME_SIZE];
	char mycall[EL_CALL_SIZE];
	char myname[EL_NAME_SIZE];
	char mymessage[EL_MESSAGE_SIZE];
	char mypwd[EL_PWD_SIZE];
	char myemail[EL_EMAIL_SIZE];
	char myqth[EL_QTH_SIZE];
	char elservers[EL_MAX_SERVERS][EL_SERVERNAME_SIZE];
	char ipaddr[EL_IP_SIZE];
	char port[EL_IP_SIZE];
	char astnode[EL_NAME_SIZE];
	char context[EL_NAME_SIZE];
	/* missed 10 heartbeats, you're out */
	short rtcptimeout;
	float lat;
	float lon;
	float freq;
	float tone;
	char power;
	char height;
	char gain;
	char dir;
	int maxstns;
	int ndenylist;
	char *denylist[EL_MAX_CALL_LIST];
	char *permitlist[EL_MAX_CALL_LIST];
	int npermitlist;
	int fdr;
	unsigned int mynode;
	char fdr_file[FILENAME_MAX];
	int audio_sock;
	int ctrl_sock;
	uint16_t audio_port;
	uint16_t ctrl_port;
	unsigned long seqno;
	struct gsmVoice_t audio_all;
	struct el_node el_node_test;
	struct el_pending pending[MAXPENDING];
	time_t aprstime;
	time_t starttime;
	char lastcall[EL_CALL_SIZE];
	int text_packet_len;
	char *text_packet;
	char login_display[EL_NAME_SIZE + EL_CALL_SIZE];
	char aprs_display[EL_APRS_SIZE];
	uint32_t rx_audio_packets;
	uint32_t tx_audio_packets;
	uint32_t rx_ctrl_packets;
	uint32_t tx_ctrl_packets;
	uint32_t rx_bad_packets;
	int timeout_time;
	struct el_node *current_talker;
	struct timeval current_talker_start_time;
	struct timeval current_talker_last_time;
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
	char fromip[EL_IP_SIZE];
};

/*!
 * \brief Echolink private information.
 * This is stored in the asterisk channel private technology for reference.
 */
struct el_pvt {
	struct el_instance *instp;
	char app[16];
	char stream[80];
	char ip[EL_IP_SIZE];
	unsigned int firstsent:1; /* First packet seen from echolink */
	unsigned int txkey:1;	  /* Transmit keyed */
	int rxkey;				  /* Receive keyed timer */
	int keepalive;
	int txindex;
	unsigned int hangup:1; /* indicate the channel should hang up */
	struct ast_frame fr;
	struct el_rxqast rxqast;
	struct el_rxqel rxqel;
	struct ast_dsp *dsp;
	struct ast_module_user *u;
	struct ast_trans_pvt *xpath;
	unsigned int nodenum;
	char *linkstr;
	ast_mutex_t lock;
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
AST_MUTEX_DEFINE_STATIC(el_nodelist_lock);

struct el_instance *instances[EL_MAX_INSTANCES];
int ninstances = 0;

/* binary search tree in memory, root node */
static void *el_node_list = NULL;
static void *el_db_callsign = NULL;
static void *el_db_nodenum = NULL;
static void *el_db_ipaddr = NULL;

/* Echolink registration thread */
static pthread_t el_register_thread = 0;
static pthread_t el_directory_thread = 0;
static int run_forever = 1;
static int el_sleeptime = 0;
static int el_login_sleeptime = 0;

static char *el_config = "echolink.conf";
static char *rpt_config = "rpt.conf";

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

static void send_info(const void *nodep, const VISIT which, const int depth);
static void process_cmd(char *buf, int buf_len, const char *fromip, struct el_instance *instp);
static int find_delete(const struct el_node *key);
static int do_new_call(struct el_instance *instp, struct el_pvt *p, const char *call, const char *name);
static void lookup_node_nodenum(const void *nodep, const VISIT which, void *closure);
static void lookup_node_callsign(const void *nodep, const VISIT which, void *closure);

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
static int el_do_show_nodes(int fd, int argc, const char *const *argv);
static int el_do_show_stats(int fd, int argc, const char *const *argv);

static char dbdump_usage[] = "Usage: echolink dbdump [nodename|callsign|ipaddr]\n" "       Dumps entire echolink db\n";

static char dbget_usage[] = "Usage: echolink dbget <nodename|callsign|ipaddr> <lookup-data>\n" "       Looks up echolink db entry\n";

static char show_nodes_usage[] = "Usage: echolink show nodes\n";

static char show_stats_usage[] = "Usage: echolink show stats\n";

/*!
 * \brief Get system monotonic 
 * This returns the CLOCK_MONOTONIC time
 * \retval		Monotonic seconds.
 */
static time_t time_monotonic(void)
{
	struct timespec ts;
	
	clock_gettime(CLOCK_MONOTONIC, &ts);
	
	return ts.tv_sec;
}

/*!
 * \brief Break up a delimited string into a table of substrings.
 * Uses defines for the delimiters: QUOTECHR and DELIMCHR.
 * \param str		Pointer to string to process (it will be modified).
 * \param strp		Pointer to a list of substrings created, NULL will be placed at the end of the list.
 * \param limit		Maximum number of substrings to process.
 */

static int finddelim(char *str, char *strp[], size_t limit)
{
	int i, inquo;

	inquo = 0;
	i = 0;
	strp[i++] = str;
	if (!*str) {
		strp[0] = 0;
		return (0);
	}
	for (; *str && (i < (limit - 1)); str++) {
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
			strp[i++] = str + 1;
		}
	}
	strp[i] = 0;
	return (i);
}

/*!
 * \brief Print the echolink internal user database list to the cli.
 * \param nodep		Pointer to eldb struct.
 * \param which		Enum for VISIT used by twalk.
 * \param closure	Pointer to int 'fd' for the ast_cli
 */
static void print_nodes(const void *nodep, const VISIT which, void *closure)
{
	const struct eldb *node = *(const struct eldb **) nodep;
	int *fd;

	if ((which == leaf) || (which == postorder)) {
		fd = closure;
		ast_cli(*fd, "%s|%s|%s\n",
				node->nodenum,
				node->callsign,
				node->ipaddr);
	}
}

/*!
 * \brief Print the echolink internal connected node list to the cli.
 * \param nodep		Pointer to el_node struct.
 * \param which		Enum for VISIT used by twalk.
 * \param closure	Pointer to int 'fd' for the ast_cli
 */
static void print_connected_nodes(const void *nodep, const VISIT which, void *closure)
{
	const struct el_node *node = *(const struct el_node **) nodep;
	int *fd;

	if ((which == leaf) || (which == postorder)) {
		fd = closure;
		ast_cli(*fd, "%7i  %-10s %-15s   %-32s\n",
				node->nodenum,
				node->call,
				node->ip,
				node->name);
	}
}

/*!
 * \brief Print the echolink internal connected node list stats to the cli.
 * \param nodep		Pointer to el_node struct.
 * \param which		Enum for VISIT used by twalk.
 * \param closure	Pointer to int 'fd' for the ast_cli
 */
static void print_node_stats(const void *nodep, const VISIT which, void *closure)
{
	const struct el_node *node = *(const struct el_node **) nodep;
	int *fd;

	if ((which == leaf) || (which == postorder)) {
		fd = closure;
		ast_cli(*fd, "%7i  %10i %10i %10i %10i %7.2f\n",
				node->nodenum,
				node->rx_ctrl_packets,
				node->tx_ctrl_packets,
				node->rx_audio_packets,
				node->tx_audio_packets,
				node->jitter);
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
 * \note Must be called locked.
 * \param nodenum	Pointer to node number to find.
 * \retval NULL		If the IP address was not found.
 * \return If found returns an eldb struct.
 */
static struct eldb *el_db_find_nodenum(const char *nodenum)
{
	struct eldb **found_key, key;
	memset(&key, 0, sizeof(key));

	ast_copy_string(key.nodenum, nodenum, sizeof(key.nodenum));

	found_key = (struct eldb **) tfind(&key, &el_db_nodenum, compare_eldb_nodenum);
	if (found_key) {
		return (*found_key);
	}

	return NULL;
}

/*!
 * \brief Find an echolink node from the internal user database by callsign.
 * \note Must be called locked.
 * \param callsign	Pointer to callsign to find.
 * \retval NULL		If the callsign was not found.
 * \return If found returns an eldb struct.
 */
static struct eldb *el_db_find_callsign(const char *callsign)
{
	struct eldb **found_key, key;
	memset(&key, 0, sizeof(key));

	ast_copy_string(key.callsign, callsign, sizeof(key.callsign));

	found_key = (struct eldb **) tfind(&key, &el_db_callsign, compare_eldb_callsign);
	if (found_key) {
		return (*found_key);
	}

	return NULL;
}

/*!
 * \brief Find an echolink node from the internal user database by ip address.
 * \note Must be called locked.
 * \param ipaddr	IP address to find.
 * \retval NULL		If the IP address was not found.
 * \return If found returns an eldb struct.
 */
static struct eldb *el_db_find_ipaddr(const char *ipaddr)
{
	struct eldb **found_key, key;
	memset(&key, 0, sizeof(key));

	ast_copy_string(key.ipaddr, ipaddr, sizeof(key.ipaddr));

	found_key = (struct eldb **) tfind(&key, &el_db_ipaddr, compare_eldb_ipaddr);
	if (found_key) {
		return (*found_key);
	}

	return NULL;
}

/*!
 * \brief Delete a node from the internal echolink users database.
 * \note Must be called locked.
 * \param nodenum		Pointer to node to delete.
 */
static void el_db_delete_entries(struct eldb *node)
{
	const struct eldb *mynode_num, *mynode_ip, *mynode_call;

	if (!node) {
		return;
	}

	mynode_num = el_db_find_nodenum(node->nodenum);
	if (mynode_num) {
		tdelete(mynode_num, &el_db_nodenum, compare_eldb_nodenum);
	}

	mynode_ip = el_db_find_ipaddr(node->ipaddr);
	if (mynode_ip) {
		tdelete(mynode_ip, &el_db_ipaddr, compare_eldb_ipaddr);
	}

	mynode_call = el_db_find_callsign(node->callsign);
	if (mynode_call) {
		tdelete(mynode_call, &el_db_callsign, compare_eldb_callsign);
	}

	if (!(mynode_num == mynode_ip && mynode_ip == mynode_call)) {
		ast_log(LOG_ERROR, "Echolink internal database corruption removing callsign %s node number=%p node ip=%p node call=%p", node->callsign, mynode_num, mynode_ip, mynode_call);
	}

	ast_free(node);
}

/*!
 * \brief Add a node to the internal echolink users database.
 * The node is added to the three internal indexes.
 * \note Must be called locked.
 * \param nodenum		Buffer to node number.
 * \param ipaddr		Buffer to ip address.
 * \param callsign		Buffer to callsign.
 * \return Returns struct eldb for the entry created.
 */
static struct eldb *el_db_put(const char *nodenum, const char *ipaddr, const char *callsign)
{
	struct eldb *node, *mynode;

	node = ast_calloc(1, sizeof(struct eldb));
	if (!node) {
		return NULL;
	}

	ast_copy_string(node->nodenum, nodenum, sizeof(node->nodenum));
	ast_copy_string(node->ipaddr, ipaddr, sizeof(node->ipaddr));
	ast_copy_string(node->callsign, callsign, sizeof(node->callsign));

	mynode = el_db_find_nodenum(node->nodenum);
	if (mynode) {
		el_db_delete_entries(mynode);
	}

	mynode = el_db_find_ipaddr(node->ipaddr);
	if (mynode) {
		el_db_delete_entries(mynode);
	}

	mynode = el_db_find_callsign(node->callsign);
	if (mynode) {
		el_db_delete_entries(mynode);
	}

	tsearch(node, &el_db_nodenum, compare_eldb_nodenum);
	tsearch(node, &el_db_ipaddr, compare_eldb_ipaddr);
	tsearch(node, &el_db_callsign, compare_eldb_callsign);

	ast_debug(5, "Directory - Added: Node=%s, Callsign=%s, IP address=%s.\n", nodenum, callsign, ipaddr);

	return (node);
}

/*!
 * \brief Lookup node by callsign
 * This looks up a node by callsign first in the connected entries and
 * if not found in the echolink database.
 * \param callsign		Callsign.
 * \param result		eldb struct to hold the results.
 * \return 				Returns 1 for success or 0 for failure.
 */
static int lookup_node_by_callsign(const char *callsign, struct eldb *result)
{
	struct el_node_lookup_callsign node_lookup = {0};

	memset(result, 0, sizeof(*result));
	ast_copy_string(node_lookup.callsign, callsign, sizeof(node_lookup.callsign));

	ast_mutex_lock(&el_nodelist_lock);
	twalk_r(el_node_list, lookup_node_callsign, &node_lookup);
	ast_mutex_unlock(&el_nodelist_lock);

	if (node_lookup.nodenum) {
		snprintf(result->nodenum, sizeof(result->nodenum), "%d", node_lookup.nodenum);
		ast_copy_string(result->callsign, node_lookup.callsign, sizeof(result->callsign));
		ast_copy_string(result->ipaddr, node_lookup.ipaddr, sizeof(result->ipaddr));
		return 1;
	} else {
		struct eldb *found_node;
		ast_mutex_lock(&el_db_lock);
		found_node = el_db_find_callsign(callsign);
		ast_mutex_unlock(&el_db_lock);
		if (found_node) {
			memcpy(result, found_node, sizeof(*result));
			return 1;
		}
    }

	return 0;
}

/*!
 * \brief Lookup node by nodenum
 * This looks up a node by node number first in the connected entries and
 * if not found in the echolink database.
 * \param nodenum		Node number.
 * \param result		eldb struct to hold the results.
 * \return 				Returns 1 for success or 0 for failure.
 */
static int lookup_node_by_nodenum(const char *nodenum, struct eldb *result)
{
	struct el_node_lookup_callsign node_lookup = {0};

	memset(result, 0, sizeof(*result));
	node_lookup.nodenum = atoi(nodenum);

	ast_mutex_lock(&el_nodelist_lock);
	twalk_r(el_node_list, lookup_node_nodenum, &node_lookup);
	ast_mutex_unlock(&el_nodelist_lock);

	if (node_lookup.callsign[0]) {
		ast_copy_string(result->nodenum, nodenum, sizeof(result->nodenum));
		ast_copy_string(result->callsign, node_lookup.callsign, sizeof(result->callsign));
		ast_copy_string(result->ipaddr, node_lookup.ipaddr, sizeof(result->ipaddr));
		return 1;
	} else {
		struct eldb *found_node;
		ast_mutex_lock(&el_db_lock);
		found_node = el_db_find_nodenum(nodenum);
		ast_mutex_unlock(&el_db_lock);
		if (found_node) {
			memcpy(result, found_node, sizeof(*result));
			return 1;
		}
	}

	return 0;
}

/*!
 * \brief Make a sdes packet with our nodes information.
 * The RTP version = 3, RTP packet type = 201.
 * The RTCP: version = 3, packet type = 202.
 * \param pkt			Pointer to buffer for sdes packet
 * \param pkt_len		Length of packet buffer
 * \param call			Pointer to callsign
 * \param name			Pointer to node name
 * \param astnode		Pointer to AllStarLink node number
 * \retval 0			Unsuccessful - pkt to small
 * \retval  			Successful length
 */
static int rtcp_make_sdes(unsigned char *pkt, int pkt_len, const char *call, const char *name, const char *astnode)
{
	unsigned char zp[1500];
	unsigned char *p = zp;
	struct rtcp_t *rp;
	unsigned char *ap;
	char line[EL_CALL_SIZE + EL_NAME_SIZE];
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
		l = snprintf(line, EL_CALL_SIZE + EL_NAME_SIZE, "AllStar %s", astnode);
		*ap++ = 6;
		*ap++ = l;
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

	if (l > pkt_len) {
		return 0;
	}
	memcpy(pkt, zp, l);
	return l;
}

/*!
 * \brief Make a sdes packet for APRS
 * The RTP version = 2, RTP packet type = 201.
 * The RTCP: version = 2, packet type = 202.
 * \param pkt			Pointer to buffer for sdes packet
 * \param pkt_len		Length of packet buffer
 * \param cname			Pointer to aprs name
 * \param loc			Pointer to aprs location
 * \retval 0			Unsuccessful - pkt to small
 * \retval  			Successful length
 */
static int rtcp_make_el_sdes(unsigned char *pkt, int pkt_len, const char *cname, const char *loc)
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

	if (l > pkt_len) {
		return 0;
	}
	memcpy(pkt, zp, l);
	return l;
}

/*!
 * \brief Make a rtcp bye packet
 * The RTP version = 3, RTP packet type = 201.
 * The RTCP: version = 3, packet type = 203.
 * \param pkt			Pointer to buffer for bye packet
 * \param reason		Pointer to reason for the bye packet
 * \retval  			Successful length
 */
static int rtcp_make_bye(unsigned char *pkt, const char *reason)
{
	struct rtcp_t *rp;
	unsigned char *ap, *zp;
	int l, hl, pl;

	zp = pkt;
	hl = 0;

	*pkt++ = 3 << 6;
	*pkt++ = 201;
	*pkt++ = 0;
	*pkt++ = 1;
	*((long *) pkt) = htonl(0);
	pkt += 4;
	hl = 8;

	rp = (struct rtcp_t *) pkt;
	*((short *) pkt) = htons((3 << 14) | 203 | (1 << 8));
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
	while ((ap - pkt) & 3) {
		*ap++ = 0;
	}
	l = ap - pkt;
	rp->common.length = htons((l / 4) - 1);
	l = hl + ((ntohs(rp->common.length) + 1) * 4);

	pl = (l & 4) ? l : l + 4;
	if (pl > l) {
		int pad = pl - l;
		memset(zp + l, '\0', pad);
		zp[pl - 1] = pad;
		pkt[0] |= 0x20;
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

	for (i = 0; i < r->nitems; i++) {
		r->item[i].r_text = NULL;
	}

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
				if (itype == 0) {
					break;
				}

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
}

/*!
 * \brief Copy a sdes item to a buffer
 * \param source		Pointer to source buffer to copy
 * \param dest			Pointer to destination buffer
 * \param destlen		Length of the destination buffer
 */
static void copy_sdes_item(const char *source, char *dest, int destlen)
{
	int len = source[1] & 0xFF;
	if (len > destlen) {
		len = destlen;
	}
	memcpy(dest, source + 2, len);
	dest[len] = 0;
}

/*!
 * \brief Determine if the packet is of type rtcp bye.
 * The RTP packet type must be 200 or 201.
 * The RTCP packet type must be 203.
 * \param pkt			Pointer to buffer of packet to test.
 * \param len			Buffer length.
 * \retval 1 			Is a bye packet.
 * \retval 0 			Not bye packet.
 */
static int is_rtcp_bye(const unsigned char *pkt, int len)
{
	const unsigned char *end;
	int sawbye = 0;

	/* 	the RTP version must be 3 or 1
	 *	the padding bit must not be set
	 *	the payload type must be 200 or 201
	*/
	if ((((pkt[0] >> 6) & 3) != 3 && ((pkt[0] >> 6) & 3) != 1) ||
		((pkt[0] & 0x20) != 0) || ((pkt[1] != 200) && (pkt[1] != 201))) {
		return 0;
	}

	end = pkt + len;

	/* 	see if this packet contains a RTCP packet type 203 */
	do {
		if (pkt[1] == 203) {
			sawbye = 1;
			break;
		}

		pkt += (ntohs(*((short *) (pkt + 2))) + 1) * 4;
	} while (pkt < end && (((pkt[0] >> 6) & 3) == 3));

	return sawbye;
}

/*!
 * \brief Determine if the packet is of type sdes.
 * The RTP packet type must be 200 or 201.
 * The RTCP packet type must be 202.
 * \param pkt			Buffer of packet to test.
 * \param len			Buffer length.
 * \retval 1 			Is a sdes packet.
 * \retval 0 			Not sdes packet.
 */
static int is_rtcp_sdes(const unsigned char *pkt, int len)
{
	const unsigned char *end;
	int sawsdes = 0;

	/* 	the RTP version must be 3 or 1
	 *	the padding bit must not be set
	 *	the payload type must be 200 or 201
	*/
	if ((((pkt[0] >> 6) & 3) != 3 && ((pkt[0] >> 6) & 3) != 1) ||
		((pkt[0] & 0x20) != 0) || ((pkt[1] != 200) && (pkt[1] != 201))) {
		return 0;
	}

	end = pkt + len;

	/* 	see if this packet contains RTCP packet type 202 */
	do {
		if (pkt[1] == 202) {
			sawsdes = 1;
			break;
		}

		pkt += (ntohs(*((short *) (pkt + 2))) + 1) * 4;
	} while (pkt < end && (((pkt[0] >> 6) & 3) == 3));

	return sawsdes;
}

/*!
 * \brief Echolink call.
 * \param ast			Pointer to Asterisk channel.
 * \param dest			Pointer to Destination (echolink node number - format 'el0/009999').
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
		ast_log(LOG_WARNING, "el_call called on %s, neither down nor reserved.\n", ast_channel_name(ast));
		return -1;
	}
	/* Make sure we have a destination */
	if (!*dest) {
		ast_log(LOG_WARNING, "Call on %s failed - no destination.\n", ast_channel_name(ast));
		return -1;
	}
	/* When we call, it just works, really, there's no destination...  Just
	 * ring the phone and wait for someone to answer
	*/

	/* get the node number in cp */
	str = ast_strdup(dest);
	if (!str) {
		return -1;
	}
	cp = strchr(str, '/');
	if (cp) {
		*cp++ = 0;
	} else {
		cp = str;
	}
	/* Advance the pointer past the leading zeros */
	while (*cp) {
		if(*cp != '0') {
			break;
		}
		cp++;
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

	ast_debug(1, "Calling %s/%s on %s.\n", dest, ipaddr, ast_channel_name(ast));

	/* make the call */
	ast_mutex_lock(&instp->lock);
	strcpy(instp->el_node_test.ip, ipaddr);
	do_new_call(instp, p, "OUTBOUND", "OUTBOUND");
	process_cmd(buf, sizeof(buf), "127.0.0.1", instp);
	ast_mutex_unlock(&instp->lock);

	ast_setstate(ast, AST_STATE_RINGING);

	return 0;
}

/*!
 * \brief Destroy and free an echolink instance.
 * \param pvt		Pointer to el_pvt struct to release.
 */
static void el_destroy(struct el_pvt *pvt)
{
	struct qelem *qpast;

	ast_mutex_lock(&pvt->lock);
	while (pvt->rxqast.qe_forw != &pvt->rxqast) {
		qpast = (struct qelem *) pvt->rxqast.qe_forw;
		remque(qpast);
		ast_free(qpast);
	}

	while (pvt->rxqel.qe_forw != &pvt->rxqel) {
		qpast = (struct qelem *) pvt->rxqel.qe_forw;
		remque(qpast);
		ast_free(qpast);
	}
	ast_mutex_unlock(&pvt->lock);

	if (pvt->dsp) {
		ast_dsp_free(pvt->dsp);
	}
	if (pvt->xpath) {
		ast_translator_free_path(pvt->xpath);
	}
	if (pvt->linkstr) {
		ast_free(pvt->linkstr);
	}
	pvt->linkstr = NULL;
	ast_mutex_lock(&el_nodelist_lock);
	twalk(el_node_list, send_info);
	ast_mutex_unlock(&el_nodelist_lock);
	ast_module_user_remove(pvt->u);
	ast_free(pvt);
}

/*!
 * \brief Allocate and initialize an echolink private structure.
 * \param data			Pointer to echolink instance name to initialize.
 * \retval 				el_pvt structure.
 */
static struct el_pvt *el_alloc(const char *data)
{
	struct el_pvt *pvt;
	int n;

	if (ast_strlen_zero(data)) {
		return NULL;
	}

	for (n = 0; n < ninstances; n++) {
		if (!strcmp(instances[n]->name, data)) {
			break;
		}
	}
	if (n >= ninstances) {
		ast_log(LOG_ERROR, "Cannot find echolink channel %s.\n", data);
		return NULL;
	}

	pvt = ast_calloc(1, sizeof(struct el_pvt));
	if (pvt) {
		snprintf(pvt->stream, sizeof(pvt->stream), "%s-%lu", data, instances[n]->seqno++);
		pvt->rxqast.qe_forw = &pvt->rxqast;
		pvt->rxqast.qe_back = &pvt->rxqast;

		pvt->rxqel.qe_forw = &pvt->rxqel;
		pvt->rxqel.qe_back = &pvt->rxqel;

		pvt->keepalive = KEEPALIVE_TIME;
		pvt->instp = instances[n];
		pvt->dsp = ast_dsp_new();
		if (!pvt->dsp) {
			ast_log(LOG_ERROR, "Cannot get DSP!\n");
			return NULL;
		}
		pvt->hangup = 0;
		ast_dsp_set_features(pvt->dsp, DSP_FEATURE_DIGIT_DETECT);
		ast_dsp_set_digitmode(pvt->dsp, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
		pvt->xpath = ast_translator_build_path(ast_format_slin, ast_format_gsm);
		if (!pvt->xpath) {
			ast_log(LOG_ERROR, "Cannot get translator!\n");
			return NULL;
		}
	}
	return pvt;
}

/*!
 * \brief Asterisk hangup function.
 * \param ast			Asterisk channel.
 * \retval 0			Always returns 0.
 */
static int el_hangup(struct ast_channel *ast)
{
	struct el_pvt *pvt = ast_channel_tech_pvt(ast);
	struct el_instance *instp = pvt->instp;
	int i, n;
	unsigned char bye[50];
	struct sockaddr_in sin;
	time_t now;

	ast_debug(1, "Sent bye to IP address %s.\n", pvt->ip);
	ast_mutex_lock(&instp->lock);
	strcpy(instp->el_node_test.ip, pvt->ip);
	find_delete(&instp->el_node_test);
	ast_softhangup(ast, AST_SOFTHANGUP_DEV);
	pvt->hangup = 0;
	ast_mutex_unlock(&instp->lock);
	n = rtcp_make_bye(bye, "disconnected");

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = inet_addr(pvt->ip);
	sin.sin_port = htons(instp->ctrl_port);
	/* send 20 bye packets to insure that they receive this disconnect */
	for (i = 0; i < 20; i++) {
		sendto(instp->ctrl_sock, bye, n, 0, (struct sockaddr *) &sin, sizeof(sin));
		instp->tx_ctrl_packets++;
	}
	time(&now);
	if (instp->starttime < (now - EL_APRS_START_DELAY)) {
		instp->aprstime = now;
	}
	ast_debug(1, "Hanging up (%s).\n", ast_channel_name(ast));
	if (!ast_channel_tech_pvt(ast)) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected.\n");
		return 0;
	}
	el_destroy(pvt);
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
	struct el_pvt *pvt = ast_channel_tech_pvt(ast);

	switch (cond) {
	case AST_CONTROL_RADIO_KEY:
		pvt->txkey = 1;
		break;
	case AST_CONTROL_RADIO_UNKEY:
		pvt->txkey = 0;
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
	const struct eldb *foundnode = NULL;
	struct eldb node_result;
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
			ast_mutex_lock(&el_db_lock);
			foundnode = el_db_find_nodenum(node);
			ast_mutex_unlock(&el_db_lock);
			if (foundnode) {
				ast_copy_string(data, foundnode->ipaddr, *datalen);
				res = 0;
			}
			break;
		case EL_QUERY_CALLSIGN:
			/* lookup first in connected table, then echolink database */
			ast_mutex_lock(&el_db_lock);
			if (lookup_node_by_nodenum(node, &node_result)) {
				ast_copy_string(data, node_result.callsign, *datalen);
				res = 0;
			}
			ast_mutex_unlock(&el_db_lock);
			break;
		default:
			ast_log(LOG_ERROR, "Option %i is not valid.", option);
			break;
	}

	ast_mutex_unlock(&el_db_lock);

	if (res) {
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
	const char **x = (const char **) a;
	const char **y = (const char **) b;
	int xoff, yoff;

	if ((**x < '0') || (**x > '9')) {
		xoff = 1;
	} else {
		xoff = 0;
	}
	if ((**y < '0') || (**y > '9')) {
		yoff = 1;
	} else {
		yoff = 0;
	}
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
#define	MAXLINKSTRS 250

	struct el_pvt *pvt = ast_channel_tech_pvt(ast);
	const char *delim = " ";
	char *cmd, *arg1, *arg4;
	char *ptr,*saveptr, *cp, *pkt;
	char buf[5120], str[200], *strs[MAXLINKSTRS];
	int i, j, k, x, pkt_len, pkt_actual_len;

	/* see if we are receiving a link text message */
	if (pvt->instp && (text[0] == 'L')) {
		if (strlen(text) < 3) {
			if (pvt->linkstr) {
				ast_free(pvt->linkstr);
				pvt->linkstr = NULL;
				ast_mutex_lock(&el_nodelist_lock);
				twalk(el_node_list, send_info);
				ast_mutex_unlock(&el_nodelist_lock);
			}
			return 0;
		}
		if (pvt->linkstr) {
			ast_free(pvt->linkstr);
			pvt->linkstr = NULL;
		}
		cp = ast_strdup(text + 2);
		if (!cp) {
			return -1;
		}
		i = finddelim(cp, strs, ARRAY_LEN(strs));
		if (i) {
			qsort(strs, i, sizeof(char *), mycompar);
			/* get size of largest string (node number) */
			j = 0;
			for (x =0; x < i; x++) {
			    j = MAX(strlen(strs[x]), j);
			}
			/* allocate a string for all of the links */
			pkt_len = (i * (j + 3)) + 50;
			pkt = ast_calloc(1, pkt_len);
			if (!pkt) {
				ast_free(cp);
				return -1;
			}
			j = 0;
			k = 0;
			for (x = 0; x < i; x++) {
				/* Process AllStar node numbers - skip over those that begin with '3' which are echolink */
				if ((*(strs[x] + 1) != '3')) {
					if (strlen(pkt + k) >= 32) {
						strncat(pkt, "\r    ", pkt_len);
					}
					if (!j++) {
						strncat(pkt, "AllStar:", pkt_len);
					}
					pkt_actual_len = strlen(pkt);
					if (*strs[x] == 'T') {
						snprintf(pkt + pkt_actual_len, pkt_len - pkt_actual_len, " %s", strs[x] + 1);
					} else {
						snprintf(pkt + pkt_actual_len, pkt_len - pkt_actual_len, " %s(M)", strs[x] + 1);
					}
				}
			}
			strncat(pkt, "\r", pkt_len);
			j = 0;
			k = strlen(pkt);
			for (x = 0; x < i; x++) {
				/* Process echolink node numbers - they start with 3 */
				if (*(strs[x] + 1) == '3') {
					if (strlen(pkt + k) >= 32) {
						strncat(pkt, "\r    ", pkt_len);
					}
					if (!j++) {
						strncat(pkt, "Echolink: ", pkt_len);
					}
					pkt_actual_len = strlen(pkt);
					if (*strs[x] == 'T') {
						snprintf(pkt + pkt_actual_len, pkt_len - pkt_actual_len, " %d", atoi(strs[x] + 2));
					} else {
						snprintf(pkt + pkt_actual_len, pkt_len - pkt_actual_len, " %d(M)", atoi(strs[x] + 2));
					}
				}
			}
			strncat(pkt, "\r", pkt_len);
			pvt->linkstr = pkt;
		}
		ast_free(cp);
		ast_mutex_lock(&el_nodelist_lock);
		twalk(el_node_list, send_info);
		ast_mutex_unlock(&el_nodelist_lock);
		return 0;
	}

	ast_copy_string(buf, text, sizeof(buf));
	ptr = strchr(buf, '\r');
	if (ptr) {
		*ptr = '\0';
	}
	ptr = strchr(buf, '\n');
	if (ptr) {
		*ptr = '\0';
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
		snprintf(str, sizeof(str), "3%06u", pvt->nodenum);
		/* if not for this one, we can't go any farther */
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
	return strncmp(((const struct el_node *) pa)->ip, ((const struct el_node *) pb)->ip, EL_IP_SIZE);
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
			memset(&sin, 0, sizeof(sin));
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

			instp->tx_audio_packets++;
			(*(struct el_node **) nodep)->tx_audio_packets++;
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
	const struct el_node *node;

	if ((which == leaf) || (which == postorder)) {
		node = *(struct el_node **) nodep;
		ast_verbose("Echolink user: call=%s,ip=%s,name=%s\n",
					node->call,
					node->ip,
					node->name);
	}
}

/*!
 * \brief Count connected nodes.
 * \param nodep		Pointer to el_node struct.
 * \param which		Enum for VISIT used by twalk.
 * \param closure	Points to el_node_count struct used to count the nodes.
 */
static void count_users(const void *nodep, const VISIT which, void *closure)
{
	const struct el_node *node;
	struct el_node_count *count;

	if ((which == leaf) || (which == postorder)) {
		node = *(struct el_node **) nodep;
		count = closure;
		if (node->instp == count->instp) {
			count->inbound++;
			if (node->outbound) {
				count->outbound++;
			}
		}
	}
}

/*!
 * \brief Lookup callsign by node number for a connected node.
 * \param nodep		Pointer to el_node struct.
 * \param which		Enum for VISIT used by twalk.
 * \param closure	Points to el_node_lookup_callsign struct used to search for the node.
 */
static void lookup_node_nodenum(const void *nodep, const VISIT which, void *closure)
{
	const struct el_node *node;
	struct el_node_lookup_callsign *lookup;

	if ((which == leaf) || (which == postorder)) {
		node = *(struct el_node **) nodep;
		lookup = closure;
		if (node->nodenum == lookup->nodenum) {
			ast_copy_string(lookup->callsign, node->call, sizeof(lookup->callsign));
			ast_copy_string(lookup->ipaddr, node->ip, sizeof(lookup->ipaddr));
		}
	}
}

/*!
 * \brief Lookup node number by callsign for a connected node.
 * \param nodep		Pointer to el_node struct.
 * \param which		Enum for VISIT used by twalk.
 * \param closure	Points to el_node_lookup_callsign struct used to search for the node.
 */
static void lookup_node_callsign(const void *nodep, const VISIT which, void *closure)
{
	const struct el_node *node;
	struct el_node_lookup_callsign *lookup;

	if ((which == leaf) || (which == postorder)) {
		node = *(struct el_node **) nodep;
		lookup = closure;
		if (!strcmp(node->call, lookup->callsign)) {
			lookup->nodenum = node->nodenum;
			ast_copy_string(lookup->ipaddr, node->ip, sizeof(lookup->ipaddr));
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
	struct ast_str *pkt = NULL;
	char *cp;
	struct el_instance *instp = (*(struct el_node **) nodep)->instp;

	pkt = ast_str_create(EL_INIT_BUFFER);
	if (!pkt) {
		return;
	}

	if ((which == leaf) || (which == postorder)) {
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(instp->audio_port);
		sin.sin_addr.s_addr = inet_addr((*(struct el_node **) nodep)->ip);

		ast_str_set(&pkt, 0, EL_WELCOME_MESSAGE, instp->astnode, instp->mycall, instp->mynode);

		if (instp->mymessage[0] != '\0') {
			ast_str_append(&pkt, 0, "%s\n\n", instp->mymessage);
		}

		if ((cp = (*(struct el_node **) nodep)->pvt ? (*(struct el_node **) nodep)->pvt->linkstr : NULL)) {
			ast_str_append(&pkt, 0, "Systems Linked:\r%s", cp);
		}

		sendto(instp->audio_sock, ast_str_buffer(pkt), ast_str_strlen(pkt), 0, (struct sockaddr *) &sin, sizeof(sin));
		ast_free(pkt);

		instp->tx_ctrl_packets++;
		(*(struct el_node **) nodep)->tx_ctrl_packets++;
	}
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

		if ((*(struct el_node **) nodep)->countdown >= 0) {
			(*(struct el_node **) nodep)->countdown--;
		}

		if ((*(struct el_node **) nodep)->countdown < 0) {
			ast_copy_string(instp->el_node_test.ip, (*(struct el_node **) nodep)->ip, EL_IP_SIZE);
			ast_copy_string(instp->el_node_test.call, (*(struct el_node **) nodep)->call, EL_CALL_SIZE);
			ast_log(LOG_WARNING, "Countdown for Callsign %s, IP Address %s is negative.\n", instp->el_node_test.call, instp->el_node_test.ip);
		}
		memset(sdes_packet, 0, sizeof(sdes_packet));
		sdes_length = rtcp_make_sdes(sdes_packet, sizeof(sdes_packet), instp->mycall, instp->myname, instp->astnode);

		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(instp->ctrl_port);
		sin.sin_addr.s_addr = inet_addr((*(struct el_node **) nodep)->ip);
		sendto(instp->ctrl_sock, sdes_packet, sdes_length, 0, (struct sockaddr *) &sin, sizeof(sin));

		instp->tx_ctrl_packets++;
		(*(struct el_node **) nodep)->tx_ctrl_packets++;
	}
}

/*!
 * \brief Send text message to connected nodes except sender.
 * \param nodep		Pointer to el_node struct.
 * \param which		Enum for VISIT used by twalk.
 * \param depth		Level of the node in the tree.
 */
static void send_text(const void *nodep, const VISIT which, const int depth)
{
	struct sockaddr_in sin;
	struct el_node *node = *(struct el_node **) nodep;

	if ((which == leaf) || (which == postorder)) {
		if (strncmp(node->ip, node->instp->el_node_test.ip, sizeof(node->ip))) {

			memset(&sin, 0, sizeof(sin));
			sin.sin_family = AF_INET;
			sin.sin_port = htons(node->instp->audio_port);
			sin.sin_addr.s_addr = inet_addr(node->ip);

			sendto(node->instp->audio_sock, node->instp->text_packet, node->instp->text_packet_len, 0, (struct sockaddr *) &sin, sizeof(sin));

			node->instp->tx_audio_packets++;
			node->tx_audio_packets++;
		}
	}
}

/*!
 * \brief Send text message to one node
 * \param node		Pointer to el_node struct.
 * \param message	Pointer to message to send.
 */
static void send_text_one(struct el_node *node, const char *message)
{
	struct sockaddr_in sin;
	char text[1024];

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(node->instp->audio_port);
	sin.sin_addr.s_addr = inet_addr(node->ip);

	snprintf(text, sizeof(text), "oNDATA%s>%s\r\n", node->instp->mycall, message);

	sendto(node->instp->audio_sock, text, strlen(text), 0, (struct sockaddr *) &sin, sizeof(sin));

	node->instp->tx_audio_packets++;
	node->tx_audio_packets++;

}

/*!
 * \brief Free node.  Empty routine.
 */
static void free_node(void *nodep)
{
}

/*!
 * \brief Find and delete a node from our internal node list.
 * \param key			Pointer to Echolink node struct to delete.
 * \retval 0			If node not found.
 * \retval 1			If node found.
 */
static int find_delete(const struct el_node *key)
{
	int found = 0;
	struct el_node **found_key;
	struct el_node *node ;

	found_key = (struct el_node **) tfind(key, &el_node_list, compare_ip);
	if (found_key) {
		node = *found_key;
		ast_debug(3, "Removing from current node list Callsign %s, IP Address %s.\n", node->call, node->ip);
		found = 1;
		node->pvt->hangup = 1;
		tdelete(node, &el_node_list, compare_ip);
		ast_free(node);
	}
	return found;
}

/*!
 * \brief Process commands and text messages.
 * Commands:
 *	o.conip <IPaddress>    (request a connect)
 *	o.dconip <IPaddress>   (request a disconnect)
 *	o.rec                  (turn on/off recording)
 * \param buf			Pointer to buffer with command data.
 * \param fromip		Pointer to ip address that sent the command.
 * \param instp			Pointer to Echolink instance.
 */
static void process_cmd(char *buf, int buf_len, const char *fromip, struct el_instance *instp)
{
	char *cmd, *arg1;
	char delim = ' ';
	char *ptr, *saveptr, *textptr;
	struct sockaddr_in sin;
	unsigned char pack[256];
	int pack_length;
	unsigned short i, n;
	struct el_node key;

	/*
	* see if this is a text packet
	* text and data messages start with the preamble oNDATA
	* it should be in the format "callsign>text message\r"
	*/
	ptr = buf + 6;
	if (!memcmp(buf, "oNDATA", 6) && *ptr != '\r' && (textptr = strchr(ptr, '>')) != NULL) {
		if ((textptr - ptr) < EL_CALL_SIZE) {

			instp->text_packet = buf;
			instp->text_packet_len = buf_len;
			ast_mutex_lock(&el_nodelist_lock);
			twalk(el_node_list, send_text);
			ast_mutex_unlock(&el_nodelist_lock);
			instp->text_packet = NULL;
			instp->text_packet_len = 0;

			textptr = strchr(textptr, '\r');
			if (textptr) {
				*textptr = '\0';
				ast_debug(3, "Sent text: %s\n", ptr);
			}
			return;
		}
	}

	/*
	* process commands received from the loopback
	*/
	if (strncmp(fromip, "127.0.0.1", EL_IP_SIZE) != 0) {
		return;
	}
	ptr = strchr(buf, (int) '\r');
	if (ptr) {
		*ptr = '\0';
	}
	ptr = strchr(buf, (int) '\n');
	if (ptr) {
		*ptr = '\0';
	}

	/* all commands with no arguments go first */

	if (strcmp(buf, "o.users") == 0) {
		ast_mutex_lock(&el_nodelist_lock);
		twalk(el_node_list, print_users);
		ast_mutex_unlock(&el_nodelist_lock);
		return;
	}

	if (strcmp(buf, "o.rec") == 0) {
		if (instp->fdr >= 0) {
			close(instp->fdr);
			instp->fdr = -1;
			ast_debug(3, "Recording stopped.\n");
		} else {
			instp->fdr = open(instp->fdr_file, O_CREAT | O_WRONLY | O_APPEND | O_TRUNC, S_IRUSR | S_IWUSR);
			if (instp->fdr >= 0) {
				ast_debug(3, "Recording into %s started.\n", instp->fdr_file);
			}
		}
		return;
	}

	cmd = strtok_r(buf, &delim, &saveptr);
	if (!cmd) {
		instp->rx_bad_packets++;
		return;
	}

	/* This version:  up to 3 parameters */
	arg1 = strtok_r(NULL, &delim, &saveptr);
	strtok_r(NULL, &delim, &saveptr);
	strtok_r(NULL, &delim, &saveptr);

	if ((strcmp(cmd, "o.conip") == 0) || (strcmp(cmd, "o.dconip") == 0)) {
		if (!arg1) {
			instp->rx_bad_packets++;
			return;
		}

		if (strcmp(cmd, "o.conip") == 0) {
			n = 1;
			pack_length = rtcp_make_sdes(pack, sizeof(pack), instp->mycall, instp->myname, instp->astnode);
		} else {
			pack_length = rtcp_make_bye(pack, "bye");
			n = 20;
		}
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(instp->ctrl_port);
		sin.sin_addr.s_addr = inet_addr(arg1);

		if (strcmp(cmd, "o.dconip") == 0) {
			ast_copy_string(key.ip, arg1, sizeof(key.ip));
			if (find_delete(&key)) {
				for (i = 0; i < 20; i++) {
					sendto(instp->ctrl_sock, pack, pack_length, 0, (struct sockaddr *) &sin, sizeof(sin));
				}
				ast_debug(1, "Disconnect request sent to %s.\n", key.ip);
			} else {
				ast_debug(1, "Did not find IP Address %s to request disconnect.\n", key.ip);
			}
		} else {
			for (i = 0; i < n; i++) {
				sendto(instp->ctrl_sock, pack, pack_length, 0, (struct sockaddr *) &sin, sizeof(sin));
			}
			ast_debug(3, "Connect request sent to %s.\n", arg1);
		}
		return;
	}
	instp->rx_bad_packets++;
}

/*!
 * \brief Asterisk read function.
 * \param ast			Pointer to Asterisk channel.
 * \retval 				Asterisk frame.
 */
static struct ast_frame *el_xread(struct ast_channel *ast)
{
	struct el_pvt *p = ast_channel_tech_pvt(ast);

	if (p->hangup) {
		ast_softhangup(ast, AST_SOFTHANGUP_DEV);
		p->hangup = 0;
	}

	memset(&p->fr, 0, sizeof(struct ast_frame));
	p->fr.src = type;
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

	if (frame->frametype != AST_FRAME_VOICE) {
		return 0;
	}
	if (p->hangup) {
		ast_softhangup(ast, AST_SOFTHANGUP_DEV);
		p->hangup = 0;
	}

	if (!p->firstsent) {
		unsigned char sdes_packet[256];
		int sdes_length;

		p->firstsent = 1;
		memset(sdes_packet, 0, sizeof(sdes_packet));
		sdes_length = rtcp_make_sdes(sdes_packet, sizeof(sdes_packet), instp->mycall, instp->myname, instp->astnode);

		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(instp->ctrl_port);
		sin.sin_addr.s_addr = inet_addr(p->ip);
		sendto(instp->ctrl_sock, sdes_packet, sdes_length, 0, (struct sockaddr *) &sin, sizeof(sin));

		instp->tx_ctrl_packets++;
	}

	/* Echolink to Asterisk */
	if (p->rxqast.qe_forw != &p->rxqast) {
		for (n = 0, qpast = p->rxqast.qe_forw; qpast != &p->rxqast; qpast = qpast->qe_forw) {
			n++;
		}
		if (n > QUEUE_OVERLOAD_THRESHOLD_AST) {
			ast_mutex_lock(&p->lock);
			while (p->rxqast.qe_forw != &p->rxqast) {
				qpast = p->rxqast.qe_forw;
				remque((struct qelem *) qpast);
				ast_free(qpast);
			}
			ast_mutex_unlock(&p->lock);
			if (p->rxkey) {
				p->rxkey = 1;
			}
		} else {
			if (!p->rxkey) {
				memset(&fr, 0, sizeof(fr));
				fr.frametype = AST_FRAME_CONTROL;
				fr.subclass.integer = AST_CONTROL_RADIO_KEY;
				fr.src = type;
				ast_queue_frame(ast, &fr);
			}
			p->rxkey = MAX_RXKEY_TIME;
			ast_mutex_lock(&p->lock);
			qpast = p->rxqast.qe_forw;
			remque((struct qelem *) qpast);
			ast_mutex_unlock(&p->lock);
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

			x = 0;
			if (p->dsp) {
				f2 = ast_translate(p->xpath, &fr, 0);
				f1 = ast_dsp_process(NULL, p->dsp, f2);
				if ((f1->frametype == AST_FRAME_DTMF_END) || (f1->frametype == AST_FRAME_DTMF_BEGIN)) {
					if ((f1->subclass.integer != 'm') && (f1->subclass.integer != 'u')) {
						if (f1->frametype == AST_FRAME_DTMF_END)
							ast_verb(4, "Echolink %s Got DTMF character %c from IP address %s.\n", p->stream, f1->subclass.integer, p->ip);
						ast_queue_frame(ast, f1);
						x = 1;
					}
				}
				ast_frfree(f1);
			}
			if (!x) {
				ast_queue_frame(ast, &fr);
			}
		}
	}
	if (p->rxkey == 1) {
		memset(&fr, 0, sizeof(fr));
		fr.frametype = AST_FRAME_CONTROL;
		fr.subclass.integer = AST_CONTROL_RADIO_UNKEY;
		fr.src = type;
		ast_queue_frame(ast, &fr);
	}
	if (p->rxkey) {
		p->rxkey--;
	}

	/* Asterisk to Echolink */
	if (ast_format_cap_iscompatible_format(ast_channel_nativeformats(ast), frame->subclass.format) == AST_FORMAT_CMP_NOT_EQUAL) {
		struct ast_str *cap_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		ast_log(LOG_WARNING, "Asked to transmit frame type %s, while native formats is %s (read/write = (%s/%s)).\n",
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
		ast_mutex_lock(&el_nodelist_lock);
		twalk(el_node_list, send_audio_only_one);
		ast_mutex_unlock(&el_nodelist_lock);
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
	ast_mutex_lock(&el_nodelist_lock);
	twalk(el_node_list, send_heartbeat);
	ast_mutex_unlock(&el_nodelist_lock);
	if (instp->el_node_test.ip[0] != '\0') {
		if (find_delete(&instp->el_node_test)) {
			bye_length = rtcp_make_bye(bye, "rtcp timeout");
			memset(&sin, 0, sizeof(sin));
			sin.sin_family = AF_INET;
			sin.sin_addr.s_addr = inet_addr(instp->el_node_test.ip);
			sin.sin_port = htons(instp->ctrl_port);
			ast_mutex_lock(&instp->lock);
			/* send 20 bye packets to insure that they receive this disconnect */
			for (i = 0; i < 20; i++) {
				sendto(instp->ctrl_sock, bye, bye_length, 0, (struct sockaddr *) &sin, sizeof(sin));
				instp->tx_ctrl_packets++;
			}
			ast_mutex_unlock(&instp->lock);
			ast_verb(4, "Callsign %s RTCP timeout, removing connection.\n", instp->el_node_test.call);
		}
		instp->el_node_test.ip[0] = '\0';
	}
	ast_mutex_unlock(&instp->lock);
	return 0;
}

/*!
 * \brief Start a new Echolink call.
 * \param pvt			Pointer to echolink private.
 * \param state			State.
 * \param nodenum		Node number to call.
 * \param assignedids	Pointer to unique ID string assigned to the channel.
 * \param requestor		Pointer to Asterisk channel.
 * \return 				Asterisk channel.
 */
static struct ast_channel *el_new(struct el_pvt *pvt, int state, unsigned int nodenum, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor)
{
	struct ast_channel *tmp;

	tmp = ast_channel_alloc(1, state, 0, 0, "", pvt->instp->astnode, pvt->instp->context, assignedids, requestor, 0, "echolink/%s", pvt->stream);
	if (!tmp) {
		ast_log(LOG_WARNING, "Unable to allocate channel structure.\n");
		return NULL;
	}

	ast_channel_tech_set(tmp, &el_tech);
	ast_channel_nativeformats_set(tmp, el_tech.capabilities);
	ast_channel_set_rawreadformat(tmp, ast_format_gsm);
	ast_channel_set_rawwriteformat(tmp, ast_format_gsm);
	ast_channel_set_writeformat(tmp, ast_format_gsm);
	ast_channel_set_readformat(tmp, ast_format_gsm);
	if (state == AST_STATE_RING) {
		ast_channel_rings_set(tmp, 1);
	}
	ast_channel_tech_pvt_set(tmp, pvt);
	ast_channel_context_set(tmp, pvt->instp->context);
	ast_channel_exten_set(tmp, pvt->instp->astnode);
	ast_channel_language_set(tmp, "");
	ast_channel_unlock(tmp);

	if (nodenum > 0) {
		char tmpstr[30];

		snprintf(tmpstr, sizeof(tmpstr), "3%06u", nodenum);
		ast_set_callerid(tmp, tmpstr, NULL, NULL);
	}
	pvt->u = ast_module_user_add(tmp);
	pvt->nodenum = nodenum;
	if (state != AST_STATE_DOWN) {
		if (ast_pbx_start(tmp)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s.\n", ast_channel_name(tmp));
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
		ast_log(LOG_NOTICE, "Channel requested with unsupported format(s): '%s'.\n", ast_format_cap_get_names(cap, &cap_buf));
		return NULL;
	}

	str = ast_strdupa((char *) data);
	if (!str) {
		return NULL;
	}
	cp = strchr(str, '/');
	if (cp) {
		*cp++ = 0;
	}
	nodenum = 0;
	if (*cp && *++cp) {
		nodenum = atoi(cp);
	}
	p = el_alloc(str);
	if (p) {
		tmp = el_new(p, AST_STATE_DOWN, nodenum, assignedids, requestor);
		if (!tmp) {
			el_destroy(p);
		}
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
	if (argc < 2) {
		return RESULT_SHOWUSAGE;
	}

	c = 'n';
	if (argc > 2) {
		c = tolower(*argv[2]);
	}
	ast_mutex_lock(&el_db_lock);
	if (c == 'i') {
		twalk_r(el_db_ipaddr, print_nodes, &fd);
	} else if (c == 'c') {
		twalk_r(el_db_callsign, print_nodes, &fd);
	} else {
		twalk_r(el_db_nodenum, print_nodes, &fd);
	}
	ast_mutex_unlock(&el_db_lock);
	return RESULT_SUCCESS;
}

/*!
 * \brief Process asterisk cli request for internal user database entry.
 * Lookup can be for ip address, callsign, or nodenumber.
 * \param fd			Asterisk cli file descriptor.
 * \param argc			Number of arguments.
 * \param argv			Pointer to asterisk cli arguments.
 * \return 	Cli success, showusage, or failure.
 */
static int el_do_dbget(int fd, int argc, const char *const *argv)
{
	char c;
	const struct eldb *mynode = NULL;
	struct eldb found_node;

	if (argc != 4) {
		return RESULT_SHOWUSAGE;
	}

	c = tolower(*argv[2]);
	ast_mutex_lock(&el_db_lock);
	if (c == 'i') {
		/* Lookup node data by IP address */
		mynode = el_db_find_ipaddr(argv[3]);
	} else if (c == 'c') {
		/* Lookup node data by callsign */
		if (lookup_node_by_callsign(argv[3], &found_node)) {
			mynode = &found_node;
		}
	} else {
		/* Lookup node data by node number */
		if (lookup_node_by_nodenum(argv[3], &found_node)) {
			mynode = &found_node;
		}
	}
	/* Report failure to find node */
	if (!mynode) {
		ast_cli(fd, "Error: Entry for %s not found!\n", argv[3]);
		ast_mutex_unlock(&el_db_lock);
		return RESULT_FAILURE;
	}

	ast_cli(fd, "%s|%s|%s\n", mynode->nodenum, mynode->callsign, mynode->ipaddr);
	ast_mutex_unlock(&el_db_lock);
	return RESULT_SUCCESS;
}

/*!
 * \brief Process asterisk cli request to show connected nodes.
 * \param fd			Asterisk cli fd
 * \param argc			Number of arguments
 * \param argv			Pointer to arguments
 * \return	Cli success, showusage, or failure.
 */
static int el_do_show_nodes(int fd, int argc, const char *const *argv)
{
	ast_cli(fd, "   Node  Call Sign  IP Address        Name\n");
	ast_mutex_lock(&el_nodelist_lock);
	twalk_r(el_node_list, print_connected_nodes, &fd);
	ast_mutex_unlock(&el_nodelist_lock);
	return RESULT_SUCCESS;
}

/*!
 * \brief Process asterisk cli request to show node statistics.
 * \param fd			Asterisk cli fd
 * \param argc			Number of arguments
 * \param argv			Pointer to arguments
 * \return	Cli success, showusage, or failure.
 */
static int el_do_show_stats(int fd, int argc, const char *const *argv)
{
	struct el_node_count count;

	/* make sure we have an instance */
	if (!instances[0]) {
		ast_cli(fd, "No echolink instances found.\n");
		return RESULT_SUCCESS;
	}

	/* count the inbound and outbound nodes */
	memset(&count, 0, sizeof(count));
	count.instp = instances[0];
	ast_mutex_lock(&el_nodelist_lock);
	twalk_r(el_node_list, count_users, &count);
	ast_mutex_unlock(&el_nodelist_lock);

	ast_cli(fd, "Inbound connections  %i\n", count.inbound);
	ast_cli(fd, "Outbound connections %i\n\n", count.outbound);

	ast_cli(fd, "   Node     Rx Ctrl    Tx Ctrl   Rx Audio   Tx Audio Jitter(ms) Bad Packets\n");
	ast_cli(fd, " System  %10i %10i %10i %10i           %10i\n",
		instances[0]->rx_ctrl_packets, instances[0]->tx_ctrl_packets,
		instances[0]->rx_audio_packets, instances[0]->tx_audio_packets,
		instances[0]->rx_bad_packets);

	ast_mutex_lock(&el_nodelist_lock);
	twalk_r(el_node_list, print_node_stats, &fd);
	ast_mutex_unlock(&el_nodelist_lock);

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
 * \brief Handle asterisk cli request for show nodes
 * \param e				Asterisk cli entry.
 * \param cmd			Cli command type.
 * \param a				Pointer to asterisk cli arguments.
 * \retval 				Cli success or failure.
 */
static char *handle_cli_show_nodes(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "echolink show nodes";
		e->usage = show_nodes_usage;
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(el_do_show_nodes(a->fd, a->argc, a->argv));
}

/*!
 * \brief Handle asterisk cli request for show stats
 * \param e				Asterisk cli entry.
 * \param cmd			Cli command type.
 * \param a				Pointer to asterisk cli arguments.
 * \retval 				Cli success or failure.
 */
static char *handle_cli_show_stats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "echolink show stats";
		e->usage = show_stats_usage;
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(el_do_show_stats(a->fd, a->argc, a->argv));
}

/*!
 * \brief Define cli entries for this module
 */
static struct ast_cli_entry el_cli[] = {
	AST_CLI_DEFINE(handle_cli_dbdump, "Dump entire echolink db"),
	AST_CLI_DEFINE(handle_cli_dbget, "Look up echolink db entry"),
	AST_CLI_DEFINE(handle_cli_show_nodes, "Show connected nodes"),
	AST_CLI_DEFINE(handle_cli_show_stats, "Show node statistics")
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
 * \param buffer		Pointer to the data to be written.
 * \param nbytes		Number of bytes to write.
 * \return	Number of bytes written or -1 if the write fails.
 */
static int writen(int fd, const char *buffer, int nbytes)
{
	int nleft, nwritten;
	const char *local_ptr = buffer;

	nleft = nbytes;

	while (nleft > 0) {
		nwritten = write(fd, local_ptr, nleft);
		if (nwritten < 0) {
			return nwritten;
		}
		nleft -= nwritten;
		local_ptr += nwritten;
	}
	return (nbytes - nleft);
}

/*!
 * \brief Send echolink registration command for this instance.
 * Each instance could have a different user name, password, or
 * other parameters.
 * \param server		Pointer to echolink server name.
 * \param instp			The interna echo link instance.
 * \retval -1			If registration failed.
 * \retval 0			If registration was successful.
 */
static int sendcmd(const char *server, const struct el_instance *instp)
{
	struct hostent *ahp;
	struct ast_hostent ah;
	struct in_addr ia;

	char ip[EL_IP_SIZE];
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
		ast_copy_string(ip, ast_inet_ntoa(ia), sizeof(ip));
	} else {
		ast_log(LOG_ERROR, "Failed to resolve Echolink server %s.\n", server);
		return -1;
	}

	memset(&el, 0, sizeof(el));
	el.sin_family = AF_INET;
	el.sin_port = htons(5200);
	el.sin_addr.s_addr = inet_addr(ip);
	el_len = sizeof(el);

	sd = socket(AF_INET, SOCK_STREAM, 0);
	if (sd < 0) {
		ast_log(LOG_ERROR, "Failed to create socket to contact the Echolink server %s.\n", server);
		return -1;
	}

	rc = connect(sd, (struct sockaddr *) &el, el_len);
	if (rc < 0) {
		ast_log(LOG_ERROR, "Connect failed to connect to the Echolink server %s.\n", server);
		close(sd);
		return -1;
	}

	time(&now);
	p_tm = localtime(&now);

	/* our version */
	if (instp->mycall[0] != '*') {
		id = "1.00R";
	} else {
		id = "1.00B";
	}

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
		ast_log(LOG_ERROR, "Failed to send Echolink credentials to Echolink server %s.\n", server);
		close(sd);
		return -1;
	}

	buf[0] = '\0';
	while (1) {
		rc = read(sd, buf, LOGINSIZE);
		if (rc > 0) {
			buf[rc] = '\0';
			ast_verb(4, "Received %s from Echolink server %s.\n", buf, server);
		} else {
			break;
		}
	}
	close(sd);

	if (strncmp(buf, "OK", 2)) {
		return -1;
	}

	return 0;
}

/*!
 * \brief Delete entire echolink directory node list.
 * Delete all nodes from the three binary trees that
 * index the eldb structure.
 */
static void el_db_delete_all_nodes(void)
{
	struct eldb *node;

	ast_mutex_lock(&el_db_lock);
	while (el_db_callsign) {
		node = *(struct eldb **) el_db_callsign;
		el_db_delete_entries(node);
	}
	ast_mutex_unlock(&el_db_lock);

	ast_assert((!el_db_nodenum && !el_db_callsign && !el_db_ipaddr));
}

/*!
 * \brief Delete callsign from internal directory.
 * \param call			Pointer to callsign to delete.
 */
static void el_db_delete_call(const char *call)
{
	struct eldb *mynode;

	ast_debug(5, "Directory - Attempt to delete: Call=%s.\n", call);
	ast_mutex_lock(&el_db_lock);
	mynode = el_db_find_callsign(call);
	if (mynode) {
		ast_debug(5, "Directory - Deleted: Node=%s, Call=%s, IP=%s.\n", mynode->nodenum, mynode->callsign, mynode->ipaddr);
		el_db_delete_entries(mynode);
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
			if (n < 1) {
				return (-1);
			}
			return (n);
		}
		memset(buf1, 0, buf1len);
		memset(buf, 0, sizeof(buf));
		n = recv(sock, buf, sizeof(buf) - 1, 0);
		if (n < 0) {
			return (-1);
		}
		z->next_in = buf;
		z->avail_in = n;
		z->next_out = buf1;
		z->avail_out = buf1len;
		r = inflate(z, Z_NO_FLUSH);
		if ((r != Z_OK) && (r != Z_STREAM_END)) {
			if (z->msg) {
				ast_log(LOG_ERROR, "Unable to inflate (Zlib): %s.\n", z->msg);
			} else {
				ast_log(LOG_ERROR, "Unable to inflate (Zlib).\n");
			}
			return -1;
		}
		r = buf1len - z->avail_out;
		if ((!n) || r) {
			break;
		}
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
			if ((el_net_get_nread) < 1) {
				return (el_net_get_nread);
			}
		}
		if (buf[el_net_get_index] > 126) {
			buf[el_net_get_index] = ' ';
		}
		c = buf[el_net_get_index++];
		str[nstr++] = c & 0x7f;
		str[nstr] = 0;
		if (c < ' ') {
			break;
		}
		if (nstr >= max) {
			break;
		}
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
static int do_el_directory(const char *hostname)
{
	struct ast_hostent ah;
	struct hostent *host;
	struct sockaddr_in dirserver;
	char str[200], ipaddr[200], nodenum[200], call[200];
	char *pp, *cc;
	int n = 0, rep_lines, delmode, str_len;
	int dir_compressed, dir_partial;
	struct z_stream_s z;
	int sock;

	sendcmd(hostname, instances[0]);
	el_net_get_index = 0;
	el_net_get_nread = 0;
	memset(&z, 0, sizeof(z));
	if (inflateInit(&z) != Z_OK) {
		if (z.msg) {
			ast_log(LOG_ERROR, "Unable to initialize Zlib: %s.\n", z.msg);
		} else {
			ast_log(LOG_ERROR, "Unable to initialize Zlib.\n");
		}
		return -1;
	}
	host = ast_gethostbyname(hostname, &ah);
	if (!host) {
		ast_log(LOG_ERROR, "Unable to resolve name for directory server %s.\n", hostname);
		inflateEnd(&z);
		return -1;
	}
	memset(&dirserver, 0, sizeof(dirserver));	/* Clear struct */
	dirserver.sin_family = AF_INET;	/* Internet/IP */
	dirserver.sin_addr.s_addr = *(unsigned long *) host->h_addr_list[0];
	dirserver.sin_port = htons(EL_DIRECTORY_PORT);	/* server port */
	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		ast_log(LOG_ERROR, "Unable to obtain a socket for directory server %s.\n", hostname);
		inflateEnd(&z);
		return -1;
	}
	/* Establish connection */
	if (connect(sock, (struct sockaddr *) &dirserver, sizeof(dirserver)) < 0) {
		ast_log(LOG_ERROR, "Unable to connect to directory server %s.\n", hostname);
		goto cleanup;
	}
	snprintf(str, sizeof(str), "F%s\r", snapshot_id);
	if (send(sock, str, strlen(str), 0) < 0) {
		ast_log(LOG_ERROR, "Unable to send to directory server %s.\n", hostname);
		goto cleanup;
	}
	str[strlen(str) - 1] = 0;
	ast_debug(4, "Sending: %s to %s.\n", str, hostname);
	if (recv(sock, str, 4, 0) != 4) {
		ast_log(LOG_ERROR, "Error in directory download (header) on %s.\n", hostname);
		goto cleanup;
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
			ast_log(LOG_ERROR, "Error in directory download (header) on %s.\n", hostname);
			goto cleanup;
		}
		if (!strncmp(str, "@@@", 3)) {
			dir_partial = 0;
		} else if (!strncmp(str, "DDD", 3)) {
			dir_partial = 1;
		} else {
			ast_log(LOG_ERROR, "Error in header on %s\n", hostname);
			goto cleanup;
		}
	}
	/* read the header line with the line count and possibly the snapshot id */
	if (el_net_get_line(sock, str, sizeof(str) - 1, dir_compressed, &z) < 1) {
		ast_log(LOG_ERROR, "Error in directory download (header) on %s.\n", hostname);
		goto cleanup;
	}
	if (dir_compressed) {
		if (sscanf(str, N_FMT(d) ":" S_FMT(SNAPSHOT_SZ), &rep_lines, snapshot_id) < 2) {
			ast_log(LOG_ERROR, "Error in parsing header on %s.\n", hostname);
			goto cleanup;
		}
	} else {
		if (sscanf(str, N_FMT(d), &rep_lines) < 1) {
			ast_log(LOG_ERROR, "Error in parsing header on %s.\n", hostname);
			goto cleanup;
		}
	}
	delmode = 0;
	/* if the returned directory is not partial - we should
	 * delete all existing directory messages
	*/
	if (!dir_partial) {
		ast_debug(4, "Full directory received, deleting all nodes.\n");
		el_db_delete_all_nodes();
	}
	/*
	 *	process the directory entries
	*/
	for (;;) {
		/* read the callsign line
		 * this line could also contain the end of list identicator
		*/
		if (el_net_get_line(sock, str, sizeof(str) - 1, dir_compressed, &z) < 1) {
			break;
		}
		if (*str <= ' ') {
			break;
		}
		/* see if we are at the end of the current list */
		if (!strncmp(str, "+++", 3)) {
			if (delmode) {
				break;
			}
			if (!dir_partial) {
				break;
			}
			delmode = 1;
			continue;
		}
		str_len = strlen(str);
		if (str[str_len - 1] == '\n') {
			str[str_len - 1] = 0;
		}
		ast_copy_string(call, str, sizeof(call));
		if (dir_partial) {
			el_db_delete_call(call);
			if (delmode) {
				continue;
			}
		}
		/* read the location / status line (we will not use this line) */
		if (el_net_get_line(sock, str, sizeof(str) - 1, dir_compressed, &z) < 1) {
			ast_log(LOG_ERROR, "Error in directory download on %s.\n", hostname);
			el_db_delete_all_nodes();
			goto cleanup;
		}
		/* read the node number line */
		if (el_net_get_line(sock, str, sizeof(str) - 1, dir_compressed, &z) < 1) {
			ast_log(LOG_ERROR, "Error in directory download on %s.\n", hostname);
			el_db_delete_all_nodes();
			goto cleanup;
		}
		str_len = strlen(str);
		if (str[str_len - 1] == '\n') {
			str[str_len - 1] = 0;
		}
		ast_copy_string(nodenum, str, sizeof(nodenum));
		/* read the ip address line */
		if (el_net_get_line(sock, str, sizeof(str) - 1, dir_compressed, &z) < 1) {
			ast_log(LOG_ERROR, "Error in directory download on %s.\n", hostname);
			el_db_delete_all_nodes();
			goto cleanup;
		}
		str_len = strlen(str);
		if (str[str_len - 1] == '\n') {
			str[str_len - 1] = 0;
		}
		ast_copy_string(ipaddr, str, sizeof(ipaddr));
		/* every 10 records, sleep for a short time */
		if (!(n % 10)) {
			usleep(2000);		/* To get to dry land */
		}
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
	ast_verb(4, "Directory completed downloading(%s,%s), %d records.\n", pp, cc, n);
	if (dir_compressed) {
		ast_debug(4, "Got snapshot_id: %s\n", snapshot_id);
	}
	return (dir_compressed);

cleanup:
	close(sock);
	inflateEnd(&z);
	return -1;
}

/*!
 * \brief Echolink directory retriever thread.
 * This thread is responsible for retrieving a directory of user registrations from
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
		if (el_sleeptime < 0) {
			el_sleeptime = 0;
		}
		if (el_sleeptime) {
			usleep(200000);
			continue;
		}
		if (!instances[0]->elservers[curdir][0]) {
			if (++curdir >= EL_MAX_SERVERS) {
				curdir = 0;
			}
			continue;
		}
		ast_debug(2, "Trying to do directory download Echolink server %s.\n", instances[0]->elservers[curdir]);
		rc = do_el_directory(instances[0]->elservers[curdir]);
		if (rc < 0) {
			if (++curdir >= EL_MAX_SERVERS) {
				curdir = 0;
			}
			el_sleeptime = 20;
			continue;
		}
		if (rc == 1) {
			el_sleeptime = 240;
		} else if (rc == 0) {
			el_sleeptime = 1800;
		}
	}
	ast_debug(1, "Echolink directory thread exited.\n");
	return NULL;
}

/*!
 * \brief Echolink registration thread.
 * This thread is responsible for registering an instance with
 * the echolink servers.
 * This routine generally runs every 360 seconds.
 * \param data		Pointer to struct el_instance data passed to this thread.
 */
static void *el_register(void *data)
{
	int i = 0;
	int rc = 0;
	const struct el_instance *instp = (struct el_instance *) data;
	time_t then, now;

	time(&then);
	ast_debug(1, "Echolink registration thread started on %s.\n", instp->name);
	while (run_forever) {
		time(&now);
		el_login_sleeptime -= (now - then);
		then = now;
		if (el_login_sleeptime < 0) {
			el_login_sleeptime = 0;
		}
		if (el_login_sleeptime) {
			usleep(200000);
			continue;
		}
		if (i >= EL_MAX_SERVERS) {
			i = 0;
		}

		do {
			if (instp->elservers[i][0] != '\0') {
				break;
			}
			i++;
		} while (i < EL_MAX_SERVERS);

		if (i < EL_MAX_SERVERS) {
			ast_debug(2, "Trying to register with Echolink server %s.\n", instp->elservers[i]);
			rc = sendcmd(instp->elservers[i++], instp);
		}
		if (rc == 0) {
			el_login_sleeptime = 360;
		} else {
			el_login_sleeptime = 20;
		}
	}
	/* Send a de-register message, but what is the point, Echolink deactivates this node within 6 minutes */
	ast_debug(1, "Echolink registration thread exited.\n");
	return NULL;
}

/*!
 * \brief Process a new echolink call.
 * \param instp			Pointer to echolink instance.
 * \param pvt			Pointer to echolink private data.
 * \param call			Pointer to callsign.
 * \param name			Pointer to name associated with the callsign.
 * \retval 1 			if not successful.
 * \retval 0 			if successful.
 * \retval -1			if memory allocation error.
 */
static int do_new_call(struct el_instance *instp, struct el_pvt *pvt, const char *call, const char *name)
{
	struct el_node *el_node_key;
	struct ast_channel *chan;
	struct ast_frame fr = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = AST_CONTROL_ANSWER,
	};
	const struct eldb *mynode;
	char nodestr[30];
	time_t now;

	el_node_key = ast_calloc(1, sizeof(struct el_node));
	if (!el_node_key) {
		return -1;
	}
	ast_copy_string(el_node_key->call, call, EL_CALL_SIZE);
	ast_copy_string(el_node_key->ip, instp->el_node_test.ip, EL_IP_SIZE);
	ast_copy_string(el_node_key->name, name, EL_NAME_SIZE);

	ast_mutex_lock(&el_db_lock);
	mynode = el_db_find_ipaddr(el_node_key->ip);
	if (!mynode) {
		ast_log(LOG_ERROR, "Cannot find database entry for IP address %s, Callsign %s.\n", el_node_key->ip, call);
		ast_free(el_node_key);
		ast_mutex_unlock(&el_db_lock);
		return 1;
	}
	ast_copy_string(nodestr, mynode->nodenum, sizeof(nodestr));
	el_node_key->nodenum = atoi(nodestr);
	el_node_key->countdown = instp->rtcptimeout;
	el_node_key->seqnum = 1;
	el_node_key->instp = instp;
	if (tsearch(el_node_key, &el_node_list, compare_ip)) {
		ast_debug(1, "New Call - Callsign %s, IP Address %s, Node %i, Name %s.\n", el_node_key->call, el_node_key->ip, el_node_key->nodenum, el_node_key->name);
		if (pvt == NULL) {	/* if a new inbound call */
			pvt = el_alloc(instp->name);
			if (!pvt) {
				ast_log(LOG_ERROR, "Cannot alloc el channel %s.\n", instp->name);
				ast_free(el_node_key);
				ast_mutex_unlock(&el_db_lock);
				return -1;
			}
			el_node_key->pvt = pvt;
			ast_copy_string(el_node_key->pvt->ip, instp->el_node_test.ip, EL_IP_SIZE);
			chan = el_new(el_node_key->pvt, AST_STATE_RINGING, el_node_key->nodenum, NULL, NULL);
			if (!chan) {
				el_destroy(el_node_key->pvt);
				ast_free(el_node_key);
				ast_mutex_unlock(&el_db_lock);
				return -1;
			}
			fr.src = type;
			ast_queue_frame(chan, &fr);
			el_node_key->rx_ctrl_packets++;
			ast_mutex_lock(&instp->lock);
			time(&now);
			if (instp->starttime < (now - EL_APRS_START_DELAY)) {
				instp->aprstime = now;
			}
			ast_mutex_unlock(&instp->lock);
		} else {
			el_node_key->pvt = pvt;
			ast_copy_string(el_node_key->pvt->ip, instp->el_node_test.ip, EL_IP_SIZE);
			el_node_key->outbound = 1;
			el_node_key->rx_ctrl_packets++;
			ast_mutex_lock(&instp->lock);
			strcpy(instp->lastcall, mynode->callsign);
			time(&now);
			if (instp->starttime < (now - EL_APRS_START_DELAY)) {
				instp->aprstime = now;
			}
			ast_mutex_unlock(&instp->lock);
		}
	} else {
		ast_log(LOG_ERROR, "Failed to add new call, Callsign %s, IP Address %s, Name %s.\n",
				el_node_key->call, el_node_key->ip, el_node_key->name);
		ast_free(el_node_key);
		ast_mutex_unlock(&el_db_lock);
		return -1;
	}
	ast_mutex_unlock(&el_db_lock);
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
	socklen_t fromlen;
	ssize_t recvlen;
	time_t now;
	struct tm *tm;
	struct el_node **found_key;
	struct el_node *node;
	struct rtcp_sdes_request items;
	char call_name[128];
	char *call;
	char *name;
	char *nameptr;
	struct pollfd fds[2];
	struct timeval current_packet_time;
	struct gsmVoice_t *gsmPacket;
	uint32_t time_difference;

	time(&instp->starttime);
	instp->aprstime = instp->starttime + EL_APRS_START_DELAY;
	ast_debug(1, "Echolink reader thread started on %s.\n", instp->name);
	ast_mutex_lock(&instp->lock);

	memset(&fds, 0, sizeof(fds));
	fds[0].fd = instp->ctrl_sock;
	fds[0].events = POLLIN;
	fds[1].fd = instp->audio_sock;
	fds[1].events = POLLIN;

	while (run_forever) {

		/* Send APRS information every EL_APRS_INTERVAL */
		time(&now);
		if (instp->aprstime <= now) {
			instp->aprstime = now + EL_APRS_INTERVAL;
			if (sin_aprs.sin_port) {	/* a zero port indicates that we never resolved the host name */
				char aprsstr[512], aprscall[256], gps_data[100], latc, lonc;
				unsigned char sdes_packet[256];
				unsigned long long u_mono;
				float lat, lon, mylat, mylon;
				double latmin, lonmin;
				int sdes_length, from_GPS = 0, latdeg, londeg;
				struct el_node_count count;
				time_t now_mono, was_mono;

				memset(&count, 0, sizeof(count));
				count.instp = instp;
				ast_mutex_lock(&el_nodelist_lock);
				twalk_r(el_node_list, count_users, &count);
				ast_mutex_unlock(&el_nodelist_lock);
				tm = gmtime(&now);
				if (!count.outbound) {			/* if no outbound users */
					snprintf(instp->login_display, EL_NAME_SIZE + EL_CALL_SIZE,
						"%s [%d/%d]", instp->myqth, count.inbound, instp->maxstns);
					snprintf(instp->aprs_display, EL_APRS_SIZE,
						" On @ %02d%02d [%d/%d]", tm->tm_hour, tm->tm_min, count.inbound, instp->maxstns);
				} else {
					snprintf(instp->login_display, EL_NAME_SIZE + EL_CALL_SIZE, "In Conference %s", instp->lastcall);
					snprintf(instp->aprs_display, EL_APRS_SIZE,
						"=N%s @ %02d%02d", instp->lastcall, tm->tm_hour, tm->tm_min);
				}
				mylat = instp->lat;
				mylon = instp->lon;
				if (ast_custom_function_find("GPS_READ") && !ast_func_read(NULL, "GPS_READ()", gps_data, sizeof(gps_data))) {
					/* gps_data format monotonic time, epoch, latitude, longitude, elevation */
					if (sscanf(gps_data, N_FMT(llu) " %*u " N_FMT(f) N_FMT(c) N_FMT(f) N_FMT(c), &u_mono, &lat, &latc, &lon, &lonc) == 5) {
						now_mono = time_monotonic();
						was_mono = (time_t) u_mono;
						if ((was_mono + GPS_VALID_SECS) >= now_mono) {
							mylat = floor(lat / 100.0);
							mylat += (lat - (mylat * 100)) / 60.0;
							mylon = floor(lon / 100.0);
							mylon += (lon - (mylon * 100)) / 60.0;
							if (latc == 'S') {
								mylat = -mylat;
							}
							if (lonc == 'W') {
								mylon = -mylon;
							}
							from_GPS = 1;
						}
					}
				}

				/* APRS location format is ddmm.hh (lat) or dddmm.hh (long)
				 * followed by the cardinal compass direction as N,S,E,W.
				 * hh hundredths of minutes not thousandths  as is the standard format.
				 */
				/* lat conversion */
				latc = mylat >= 0 ? 'N' : 'S';
				latdeg = (int) fabs(mylat);
				latmin = (fabs(mylat) - latdeg) * 60.0;

				/* lon conversion */
				lonc = mylon >= 0 ? 'E' : 'W';
				londeg = (int) fabs(mylon);
				lonmin = (fabs(mylon) - londeg) * 60.0;

				snprintf(aprsstr, sizeof(aprsstr), ")EL-%-6.6s!%02d%05.2f%cE%03d%05.2f%c0PHG%d%d%d%d/%06d/%03d%s", instp->mycall,
					latdeg, latmin, latc, londeg, lonmin, lonc,
					instp->power, instp->height, instp->gain, instp->dir,
					(int) ((instp->freq * 1000) + 0.5), (int) (instp->tone + 0.05), instp->aprs_display);

				ast_debug(4, "APRS out%s: %s.\n", from_GPS ? " (GPS)" : "", aprsstr);
				snprintf(aprscall, sizeof(aprscall), "%s/%s", instp->mycall, instp->mycall);
				memset(sdes_packet, 0, sizeof(sdes_packet));
				sdes_length = rtcp_make_el_sdes(sdes_packet, sizeof(sdes_packet), aprscall, aprsstr);
				sendto(instp->ctrl_sock, sdes_packet, sdes_length, 0, (struct sockaddr *) &sin_aprs, sizeof(sin_aprs));
				instp->tx_ctrl_packets++;
			} else {		/* we were unable to resolve the APRS host name - attempt to resolve it now */
				struct hostent *ahp;
				struct ast_hostent ah;
				sin_aprs.sin_family = AF_INET;
				sin_aprs.sin_port = htons(5199);
				ahp = ast_gethostbyname(EL_APRS_SERVER, &ah);
				if (!ahp) {
					ast_log(LOG_WARNING, "Unable to resolve echolink APRS server IP address %s.\n", EL_APRS_SERVER);
					memset(&sin_aprs, 0, sizeof(sin_aprs));
				} else {
					memcpy(&sin_aprs.sin_addr.s_addr, ahp->h_addr, sizeof(in_addr_t));
				}
			}
			el_sleeptime = 0;
			el_login_sleeptime = 0;
		}
		ast_mutex_unlock(&instp->lock);
		/*
		* poll for activity
		*/
		i = ast_poll(fds, 2, 50);
		if (i == 0) {
			ast_mutex_lock(&instp->lock);
			continue;
		}
		if (i < 0) {
			ast_log(LOG_ERROR, "Fatal error, poll returned %d: %s\n", i, strerror(errno));
			run_forever = 0;
			break;
		}
		ast_mutex_lock(&instp->lock);
		/*
		* process the control socket
		*/
		if (fds[0].revents) {	/* if a ctrl packet */
			fds[0].revents = 0;
			instp->rx_ctrl_packets++;
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
					if (items.item[0].r_text != NULL) {
						copy_sdes_item(items.item[0].r_text, call_name, 127);
					}
					if (call_name[0] != '\0') {
						call = call_name;
						nameptr = strchr(call_name, ' ');
						name = "UNKNOWN";
						if (nameptr) {
							*nameptr = '\0';
							name = nameptr + 1;
							name = ast_strip(name);
						}
						ast_mutex_lock(&el_nodelist_lock);
						found_key = (struct el_node **) tfind(&instp->el_node_test, &el_node_list, compare_ip);
						ast_mutex_unlock(&el_nodelist_lock);
						if (found_key) {
							node = *found_key;
							node->countdown = instp->rtcptimeout;
							/* different callsigns behind a NAT router, running -L, -R, ... */
							if (strncmp((*found_key)->call, call, EL_CALL_SIZE) != 0) {
								ast_verb(4, "Call changed from %s to %s.\n", node->call, call);
								ast_copy_string(node->call, call, EL_CALL_SIZE);
							}
							if (strncmp(node->name, name, EL_NAME_SIZE) != 0) {
								ast_verb(4, "Name changed from %s to %s.\n", (*found_key)->name, name);
								ast_copy_string(node->name, name, EL_NAME_SIZE);
							}
							node->rx_ctrl_packets++;
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
								if (instp->npermitlist) {
									i = 1;
								}
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
									/* we failed to create a new call - error reported by do_new_call */
									i = 0;
								}
							}
							if (i) {	/* if not authorized or do_new_call failed*/
								/* first, see if we have one that is ours and not abandoned */
								for (x = 0; x < MAXPENDING; x++) {
									if (strcmp(instp->pending[x].fromip, instp->el_node_test.ip)) {
										continue;
									}
									if (ast_tvdiff_ms(ast_tvnow(), instp->pending[x].reqtime) < AUTH_ABANDONED_MS) {
										break;
									}
								}
								if (x < MAXPENDING) {
									/* if its time, send un-auth */
									if (ast_tvdiff_ms(ast_tvnow(), instp->pending[x].reqtime) >= AUTH_RETRY_MS) {
										ast_debug(1, "Sent bye to IP address %s.\n", instp->el_node_test.ip);
										j = rtcp_make_bye(bye, "UN-AUTHORIZED");
										memset(&sin1, 0, sizeof(sin1));
										sin1.sin_family = AF_INET;
										sin1.sin_addr.s_addr = inet_addr(instp->el_node_test.ip);
										sin1.sin_port = htons(instp->ctrl_port);
										for (i = 0; i < 20; i++) {
											sendto(instp->ctrl_sock, bye, j,
												   0, (struct sockaddr *) &sin1, sizeof(sin1));
											instp->tx_ctrl_packets++;
										}
										instp->pending[x].fromip[0] = 0;
									}
									time(&now);
									if (instp->starttime < (now - EL_APRS_START_DELAY)) {
										instp->aprstime = now;
									}
								} else {	/* find empty one */
									for (x = 0; x < MAXPENDING; x++) {
										if (!instp->pending[x].fromip[0]) {
											break;
										}
										if (ast_tvdiff_ms(ast_tvnow(), instp->pending[x].reqtime) >= AUTH_ABANDONED_MS) {
											break;
										}
									}
									if (x < MAXPENDING) {	/* we found one */
										strcpy(instp->pending[x].fromip, instp->el_node_test.ip);
										instp->pending[x].reqtime = ast_tvnow();
										time(&now);
										if (instp->starttime < (now - EL_APRS_START_DELAY)) {
											instp->aprstime = now;
										} else {
											el_sleeptime = 0;
											el_login_sleeptime = 0;
										}
									} else {
										ast_log(LOG_ERROR, "Cannot find open pending echolink request slot for IP Address %s.\n",
												instp->el_node_test.ip);
									}
								}
							}
							ast_mutex_lock(&el_nodelist_lock);
							twalk(el_node_list, send_info);
							ast_mutex_unlock(&el_nodelist_lock);
						}
					} else {
						instp->rx_bad_packets++;
					}
				} else {
					if (is_rtcp_bye((unsigned char *) buf, recvlen)) {
						if (find_delete(&instp->el_node_test)) {
							ast_verb(4, "Disconnect from IP address %s, Callsign %s.\n", instp->el_node_test.ip, instp->el_node_test.call);
						}
					} else {
						instp->rx_bad_packets++;
					}
				}
			}
		}
		/*
		*process the audio socket
		*/
		if (fds[1].revents) {	/* if an audio packet */
			fds[1].revents = 0;
			instp->rx_audio_packets++;
			current_packet_time = ast_tvnow();
			fromlen = sizeof(struct sockaddr_in);
			recvlen = recvfrom(instp->audio_sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *) &sin, &fromlen);
			if (recvlen > 0) {
				buf[recvlen] = '\0';
				ast_copy_string(instp->el_node_test.ip, ast_inet_ntoa(sin.sin_addr), EL_IP_SIZE);
				gsmPacket = (struct gsmVoice_t *)&buf;
				/* packets that start with 0x6f are text packets */
				if (buf[0] == 0x6f) {
					process_cmd(buf, recvlen, instp->el_node_test.ip, instp);
				} else {
					ast_mutex_lock(&el_nodelist_lock);
					found_key = (struct el_node **) tfind(&instp->el_node_test, &el_node_list, compare_ip);
					ast_mutex_unlock(&el_nodelist_lock);
					if (found_key) {
						node = *found_key;

						node->countdown = instp->rtcptimeout;
						node->rx_audio_packets++;
						/* compute inter-arrival jitter */
						time_difference = ast_tvdiff_ms(current_packet_time, node->last_packet_time);
						if (time_difference < 2000) {
							node->jitter = (time_difference + node->jitter) / 2;
						}
						node->last_packet_time = current_packet_time;
						/*
						* see if we have a new talker
						*/
						if (!instp->current_talker) {
							instp->current_talker = node;
							instp->current_talker_start_time = current_packet_time;
							node->istimedout = 0;
							node->isdoubling = 0;
							ast_debug(3, "Station %s started talking.\n", node->call);
						} else {
							/* see if this is a double - two stations talking at the same time */
							if (node->nodenum != instp->current_talker->nodenum) {
								if (!node->isdoubling) {
									ast_debug(3, "Station %s is doubling with %s.\n", node->call, instp->current_talker->call);
									send_text_one(node, "You are doubling.");
								}
								node->isdoubling = 1;
								continue;
							}
						}
						instp->current_talker_last_time = current_packet_time;
						/* see if they have timed out */
						if (ast_tvdiff_ms(current_packet_time, instp->current_talker_start_time) > instp->timeout_time) {
							if (!node->istimedout) {
								ast_debug(1, "Station %s timed out.\n", node->call);
								send_text_one(node, "You have timed out.");
							}
							node->istimedout = 1;
							continue;
						}
						/* queue the gsm packets */
						if (recvlen == sizeof(struct gsmVoice_t)) {
							if (gsmPacket->version == 3 && gsmPacket->payt == 3) {
								/* break them up for Asterisk */
								for (i = 0; i < BLOCKING_FACTOR; i++) {
									qpast = ast_malloc(sizeof(struct el_rxqast));
									if (qpast) {
										memcpy(qpast->buf, gsmPacket->data + (GSM_FRAME_SIZE * i), GSM_FRAME_SIZE);
										ast_mutex_lock(&node->pvt->lock);
										insque((struct qelem *) qpast, (struct qelem *) node->pvt->rxqast.qe_back);
										ast_mutex_unlock(&node->pvt->lock);
									}
								}
							} else {
								instp->rx_bad_packets++;
							}
						} else {
							instp->rx_bad_packets++;
						}
					} else {
						instp->rx_bad_packets++;
					}
				}
			}
		}
		/* check current talker (see if they have stopped talking) */
		if (instp->current_talker) {
			if (ast_tvdiff_ms(ast_tvnow(), instp->current_talker_last_time) > AUDIO_TIMEOUT) {
				ast_debug(3, "Station %s stopped talking.\n", instp->current_talker->call);
				instp->current_talker->istimedout = 0;
				instp->current_talker->isdoubling = 0;
				instp->current_talker = NULL;
				instp->current_talker_start_time = (struct timeval) {0};
				instp->current_talker_last_time = (struct timeval) {0};
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
	struct ast_config *rpt_cfg;
	struct ast_flags zeroflag = { 0 };
	const char *val;
	char *str;
	struct hostent *ahp;
	struct ast_hostent ah;
	struct el_instance *instp;
	struct sockaddr_in si_me;
	int serverindex;
	char servername[9];

	if (ninstances >= EL_MAX_INSTANCES) {
		ast_log(LOG_ERROR, "Too many instances specified.\n");
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

	ast_copy_string(instp->name, ctg, sizeof(instp->name));

	val = ast_variable_retrieve(cfg, ctg, "ipaddr");
	if (!val) {
		strcpy(instp->ipaddr, "0.0.0.0");
	} else {
		ast_copy_string(instp->ipaddr, val, sizeof(instp->ipaddr));
	}

	val = ast_variable_retrieve(cfg, ctg, "port");
	if (!val) {
		strcpy(instp->port, "5198");
	} else {
		ast_copy_string(instp->port, val, sizeof(instp->port));
	}

	val = ast_variable_retrieve(cfg, ctg, "maxstns");
	if (!val) {
		instp->maxstns = 50;
	} else {
		instp->maxstns = atoi(val);
	}

	val = ast_variable_retrieve(cfg, ctg, "rtcptimeout");
	if (!val) {
		instp->rtcptimeout = 15;
	} else {
		instp->rtcptimeout = atoi(val);
	}

	val = ast_variable_retrieve(cfg, ctg, "node");
	if (!val) {
		instp->mynode = 0;
	} else {
		instp->mynode = atol(val);
	}

	val = ast_variable_retrieve(cfg, ctg, "astnode");
	if (!val) {
		strcpy(instp->astnode, "1999");
	} else {
		ast_copy_string(instp->astnode, val, sizeof(instp->astnode));
	}

	val = ast_variable_retrieve(cfg, ctg, "context");
	if (!val) {
		strcpy(instp->context, "radio-secure");
	} else {
		ast_copy_string(instp->context, val, sizeof(instp->context));
	}

	val = ast_variable_retrieve(cfg, ctg, "call");
	if (!val) {
		ast_copy_string(instp->mycall, "INVALID", sizeof(instp->mycall));
	} else {
		ast_copy_string(instp->mycall, val, sizeof(instp->mycall));
	}

	if (!strcmp(instp->mycall, "INVALID")) {
		ast_log(LOG_ERROR, "Invalid Echolink callsign.\n");
		return -1;
	}

	val = ast_variable_retrieve(cfg, ctg, "name");
	if (!val) {
		ast_copy_string(instp->myname, instp->mycall, sizeof(instp->myname));
	} else {
		ast_copy_string(instp->myname, val, sizeof(instp->myname));
	}

	val = ast_variable_retrieve(cfg, ctg, "message");
	ast_copy_string(instp->mymessage, S_OR(val, ""), sizeof(instp->mymessage));
	if (instp->mymessage[0] != '\0') {
		char *p = instp->mymessage;
		while ((p = strstr(p, "\\n")) != NULL) {
			*p = '\n'; /* Replace the literal \n with a newline character */
			memmove(p + 1, p + 2, strlen(p + 2) + 1); /* Shift the string to remove the \n */
			p++; 
		}
	}

	val = ast_variable_retrieve(cfg, ctg, "recfile");
	if (!val) {
		ast_copy_string(instp->fdr_file, "/tmp/echolink_recorded.gsm", FILENAME_MAX - 1);
	} else {
		ast_copy_string(instp->fdr_file, val, FILENAME_MAX);
	}

	val = ast_variable_retrieve(cfg, ctg, "pwd");
	if (!val) {
		ast_copy_string(instp->mypwd, "INVALID", sizeof(instp->mypwd));
	} else {
		ast_copy_string(instp->mypwd, val, sizeof(instp->mypwd));
	}

	val = ast_variable_retrieve(cfg, ctg, "qth");
	if (!val) {
		ast_copy_string(instp->myqth, "INVALID", sizeof(instp->myqth));
	} else {
		ast_copy_string(instp->myqth, val, sizeof(instp->myqth));
	}

	val = ast_variable_retrieve(cfg, ctg, "email");
	if (!val) {
		ast_copy_string(instp->myemail, "INVALID", sizeof(instp->myemail));
	} else {
		ast_copy_string(instp->myemail, val, sizeof(instp->myemail));
	}

	for (serverindex = 0; serverindex < EL_MAX_SERVERS; serverindex++) {
		snprintf(servername, sizeof(servername), "server%i", serverindex + 1);
		val = ast_variable_retrieve(cfg, ctg, servername);
		if (!val) {
			instp->elservers[serverindex][0] = '\0';
		} else {
			ast_copy_string(instp->elservers[serverindex], val, EL_SERVERNAME_SIZE);
		}
	}

	val = ast_variable_retrieve(cfg, ctg, "deny");
	if (val) {
		str = ast_strdup(val);
		if (str) {
			instp->ndenylist = finddelim(str, instp->denylist, ARRAY_LEN(instp->denylist));
		}
	}

	val = ast_variable_retrieve(cfg, ctg, "permit");
	if (val) {
		str = ast_strdup(val);
		if (str) {
			instp->npermitlist = finddelim(str, instp->permitlist, ARRAY_LEN(instp->permitlist));
		}
	}

	val = ast_variable_retrieve(cfg, ctg, "lat");
	if (!val) {
		instp->lat = 0.0;
	} else {
		instp->lat = strtof(val, NULL);
	}

	val = ast_variable_retrieve(cfg, ctg, "lon");
	if (!val) {
		instp->lon = 0.0;
	} else {
		instp->lon = strtof(val, NULL);
	}

	val = ast_variable_retrieve(cfg, ctg, "freq");
	if (!val) {
		instp->freq = 0.0;
	} else {
		instp->freq = strtof(val, NULL);
	}

	val = ast_variable_retrieve(cfg, ctg, "tone");
	if (!val) {
		instp->tone = 0.0;
	} else {
		instp->tone = strtof(val, NULL);
	}

	val = ast_variable_retrieve(cfg, ctg, "power");
	if (!val) {
		instp->power = 0;
	} else {
		instp->power = (char) strtol(val, NULL, 0);
	}

	val = ast_variable_retrieve(cfg, ctg, "height");
	if (!val) {
		instp->height = 0;
	} else {
		instp->height = (char) strtol(val, NULL, 0);
	}

	val = ast_variable_retrieve(cfg, ctg, "gain");
	if (!val) {
		instp->gain = 0;
	} else {
		instp->gain = (char) strtol(val, NULL, 0);
	}

	val = ast_variable_retrieve(cfg, ctg, "dir");
	if (!val) {
		instp->dir = 0;
	} else {
		instp->dir = (char) strtol(val, NULL, 0);
	}

	/* load settings from app_rpt rpt.conf */
	if (!(rpt_cfg = ast_config_load(rpt_config, zeroflag))) {
		ast_log(LOG_ERROR, "Unable to load config %s.\n", rpt_config);
		return -1;
	}
	val = ast_variable_retrieve(rpt_cfg, instp->astnode, "totime");
	if (!val) {
		instp->timeout_time = 180000;
	} else {
		instp->timeout_time = atoi(val);
	}

	ast_config_destroy(rpt_cfg);

	/* validate settings */

	if ((!strncmp(instp->mypwd, "INVALID", sizeof(instp->mypwd))) || (!strncmp(instp->mycall, "INVALID", sizeof(instp->mycall)))) {
		ast_log(LOG_ERROR, "Your Echolink call or password is not correct.\n");
		return -1;
	}
	if ((instp->elservers[0][0] == '\0') || (instp->elservers[1][0] == '\0') || (instp->elservers[2][0] == '\0')) {
		ast_log(LOG_ERROR, "One of the Echolink servers missing.\n");
		return -1;
	}
	/* start up the socket listeners */
	if ((instp->audio_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		ast_log(LOG_WARNING, "Unable to create new socket for echolink audio connection.\n");
		return -1;
	}
	if ((instp->ctrl_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		ast_log(LOG_WARNING, "Unable to create new socket for echolink control connection.\n");
		close(instp->audio_sock);
		instp->audio_sock = -1;
		return -1;
	}
	/* audio channel */
	memset(&si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
	if (strcmp(instp->ipaddr, "0.0.0.0") == 0) {
		si_me.sin_addr.s_addr = htonl(INADDR_ANY);
	} else {
		si_me.sin_addr.s_addr = inet_addr(instp->ipaddr);
	}
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
	/* control channel */
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
	/* APRS channel */
	memset(&sin_aprs, 0, sizeof(sin_aprs));
	sin_aprs.sin_family = AF_INET;
	sin_aprs.sin_port = htons(5199);
	ahp = ast_gethostbyname(EL_APRS_SERVER, &ah);
	if (!ahp) {
		/* We could not resolve the host name.  This will be attempted again
		 * when we need to send an APRS packet
		 */
		ast_log(LOG_WARNING, "Unable to resolve echolink APRS server IP address %s.\n", EL_APRS_SERVER);
		memset(&sin_aprs, 0, sizeof(sin_aprs));
	} else {
		memcpy(&sin_aprs.sin_addr.s_addr, ahp->h_addr, sizeof(in_addr_t));
	}
	/* start the registration thread */
	ast_pthread_create(&el_register_thread, NULL, el_register, instp);
	ast_pthread_create_detached(&instp->el_reader_thread, NULL, el_reader, instp);
	instances[ninstances++] = instp;

	ast_debug(1, "Echolink/%s listening on %s port %s.\n", instp->name, instp->ipaddr, instp->port);
	ast_debug(1, "Echolink/%s node capacity set to %d node(s).\n", instp->name, instp->maxstns);
	ast_debug(1, "Echolink/%s heartbeat timeout set to %d heartbeats.\n", instp->name, instp->rtcptimeout);
	ast_debug(1, "Echolink/%s node set to %u.\n", instp->name, instp->mynode);
	ast_debug(1, "Echolink/%s call set to %s.\n", instp->name, instp->mycall);
	ast_debug(1, "Echolink/%s name set to %s.\n", instp->name, instp->myname);
	ast_debug(1, "Echolink/%s file for recording set to %s.\n", instp->name, instp->fdr_file);
	ast_debug(1, "Echolink/%s  qth set to %s.\n", instp->name, instp->myqth);
	ast_debug(1, "Echolink/%s emailID set to %s.\n", instp->name, instp->myemail);

	return 0;
}

static int unload_module(void)
{
	int n;

	run_forever = 0;
	if (el_node_list) {
		ast_mutex_lock(&el_nodelist_lock);
		tdestroy(el_node_list, free_node);
		ast_mutex_unlock(&el_nodelist_lock);
	}
	if (el_db_callsign) {
		el_db_delete_all_nodes();
	}

	ast_debug(1, "We have %d Echolink instance%s.\n", ninstances, ESS(ninstances));
	for (n = 0; n < ninstances; n++) {
		ast_debug(2, "Closing Echolink instance %d.\n", n);
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
		if (instances[n]->denylist[0]) {
			ast_free(instances[n]->denylist[0]);
		}
		if (instances[n]->permitlist[0]) {
			ast_free(instances[n]->permitlist[0]);
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

	for (n = 0; n < ninstances; n++) {
		ast_free(instances[n]);
	}
	return 0;
}

 static int load_module(void)
{
	struct ast_config *cfg;
	char *ctg = NULL;
	struct ast_flags zeroflag = { 0 };

	if (!(cfg = ast_config_load(el_config, zeroflag))) {
		ast_log(LOG_ERROR, "Unable to load config %s.\n", el_config);
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!(el_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append(el_tech.capabilities, ast_format_gsm, 0);

	while ((ctg = ast_category_browse(cfg, ctg)) != NULL) {
		if (ctg == NULL) {
			continue;
		}
		if (store_config(cfg, ctg) < 0) {
			return AST_MODULE_LOAD_DECLINE;
		}
	}
	ast_config_destroy(cfg);
	cfg = NULL;
	ast_verb(4, "Total of %d Echolink instances found.\n", ninstances);
	if (ninstances < 1) {
		ast_log(LOG_ERROR, "Cannot run echolink with no instances.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	/* start the directory thread */
	ast_pthread_create(&el_directory_thread, NULL, el_directory, NULL);

	ast_cli_register_multiple(el_cli, sizeof(el_cli) / sizeof(struct ast_cli_entry));
	/* Make sure we can register our channel type */
	if (ast_channel_register(&el_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s.\n", type);
		return AST_MODULE_LOAD_DECLINE;
	}
	return 0;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Echolink Channel Driver");
