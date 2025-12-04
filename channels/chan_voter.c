/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2011, Digium, Inc.
 *
 * Copyright (C) 2011-2013
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
 * \brief Radio Voter channel driver for Asterisk
 *
 * \author Jim Dixon, WB6NIL <jim@lambdatel.com>
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

/*  Basic Information On How This Works
Each node has a number of potential "clients" associated with it. In the voter.conf file, each stanza (category)
is named by the node number that the clients specified within the stanza are to be associated with. Each entry
consists of an arbitrary (relatively meaningless, just included for easy identification purposes within this
channel driver, and has nothing to do with its operation) identifier equated to a unique password. This password
is programmed into the client. All clients must have unique passwords, as that is what is used by this channel
driver to identify them.

Each channel instance (as opened by app_rpt as a main radio channel, e.g. rxchannel=Voter/1999 in rpt.conf) and
is directly associated with the node that opened it.

Each client has a pair of circular buffers, one for mulaw audio data, and one for RSSI value. The allocated buffer
length in all clients is determined by the 'buflen' parameter, which is specified in the "global" stanza in the
voter.conf file in milliseconds, and represented in the channel driver as number of samples (actual buffer length,
which is 8 * milliseconds).

Every channel instance has a index ("drainindex"), indicating the next position within the physical buffer(s) where
the audio will be taken from the buffers and presented to the Asterisk channel stream as VOICE frames.

Therefore, there is an abstraction of a "buffer" that exists starting at drainindex and ending (modulo) at
drainindex - 1, with length of buflen.

Buflen is selected so that there is enough time (delay) for any straggling packets to arrive before it is time
to present the data to the Asterisk channel.

The idea is that the current audio being presented to Asterisk is from some time shortly in the past. Therefore,
"Now" is the position in the abstracted buffer of 'bufdelay' (generally buflen - 160) (you gotta at least leave room for
an entire frame) and the data is being presented from the start of the abstracted buffer. As the physical buffer
moves along, what was once "now" will eventually become far enough in the "past" to be presented to Asterisk (gosh,
doesn't this sound like a scene from "Spaceballs"??.. I too always drink coffee while watching "Mr. Radar").

During the processing of an audio frame to be presented to Asterisk, all client's buffers that are associated with
a channel instance (node) are examined by taking an average of the RSSI value for each sample in the associated
time period (the first 160 samples of the abstracted buffer (which is the physical buffer from drainindex to
drainindex + 159) and whichever one, if any that has the largest RSSI average greater then zero is selected
as the audio source for that frame. The corresponding audio buffer's contents (in the corresponding offsets)
are presented to Asterisk, then ALL the clients corresponding RSSI data is set to 0, ALL the clients corresponding
audio is set to quiet (0x7f). The overwriting of the buffers after their use/examination is done so that the
next time those positions in the physical buffer are examined, they will not contain any data that was not actually
put there, since all client's buffers are significant regardless of whether they were populated or not. This
allows for the true 'connectionless-ness' of this protocol implementation.

Voter Channel test modes:

0 - Normal voting operation
1 - Randomly pick which client of all that
	are receiving at the max RSSI value to use.
> 1 - Cycle thru all the clients that are receiving
	at the max RSSI value with a cycle time of (test mode - 1)
	frames. In other words, if you set it to 2, it will
	change every single time. If you set it to 11, it will
	change every 10 times. This is serious torture test.

Note on ADPCM functionality:
The original intent was to change this driver to use signed linear internally (slin),
but after some thought, it was determined that it was prudent to continue using
mulaw as the "standard" internal audio format (with the understanding of the slight
degradation in dynamic range when using ADPCM resulting in doing so).  This was
done because existing external entities (such as the recording files and the streaming
stuff) use mulaw as their transport, and changing all of that to signed linear would
be cumbersome, inefficient and undesirable.

Redundant "Proxy" Mode:

A "Redundant" (backup) server may be set up, so that if the "primary" server fails,
clients can detect this failure, and connect to the designated "backup" (or "secondary")
server.

Needless to say, since Internet connectivity is not by any means guaranteed to be consistent,
it is possible for some clients to have working connectivity to the "primary" server and not
others, even though the "primary" server is functional.

If this was to occur, actual voting and/or simulcast clients would have a "broken" system
(being that all the clients need to be on the same server for any sort of functional operation).

To eliminate this possibility, functionality has been added so that a "secondary" server
will "proxy" (forward) all of its VOTER packets to the "primary" (if the "primary" is
on line), and the "primary" will generate all of the outbound VOTER packets, which (for clients
"connected" to the "secondary" server) get sent to the "secondary" server to distribution to
its clients.

This allows for a "unity" of all of the clients on a network, even though they may be connected
to different servers.

In addition, it is assumed that "permanent linking" (at least of some sort) will be provided between
the channel side of the chan_voter instances (presumably through a "perma-link" provided by app_rpt).
When the "secondary" is "proxying" (to the "primary") it does not provide direct connectivity to/from
its locally-connected clients, thus allowing them to "connect" via the "primary" server instead. In
"normal" mode, it works "normally".

The operation is performed by more-or-less "encapsulating" the VOTER packets received by the "secondary"
server, and forwarding them on to the "primary" server, where they are "un-encapsulated" and appear to
that serer to be coming from clients connected directly to it (and keeps track of which ones are connected
in this manner, etc). When it needs to send VOTER packets to a client connected through the "secondary",
it "encapsulates" them, and sends them to the "secondary", where they get "un-encapsulated" and sent
to their associated connected clients, based upon information in the "encapsulation".

If the "secondary" server loses (or does not make) connection to the "primary", it operates as normal, until
such time as it can make the connection.

The server redundancy feature is local to each chan_voter instance.

For each chan_voter instance served by both the "primary" and "secondary" servers, the client
list (parameters, etc) *MUST* be identical.

In addition, the following things must be added uniquely on each server:

In the "primary" server, there needs to be a "primary connectivity" client specified for each
"secondary" server for which it is "primary". Basically, this is a client that does NOTHING other
then providing a means by which the "secondary" can determine whether the "primary" is on line.
It is a standard chan_voter client, with nothing else specified other then its password. Again,
although it is a "legitimate" client (technically), its only purpose *MUST* be to allow the secondary
server to connect to it.

The "primary" server also needs to have the following in all of its instances that require redundancy:

isprimary = y

The "secondary" server needs to have the following in all of its instances that require redundancy:

primary = 12.34.56.78:1667,mypswd

(where 12.34.56.78:1667 is the IPADDDR:PORT of the "primary" server, and mypswd is the password of the
"primary connectivity" client)

Note: Master timing sources *MUST* be local to their associated server, and therefore, can not be operated
in a redundant configuration. If a radio needs server redundancy, it CAN NOT be connected to a master timing
source. Also, the master timing source MUST be associated with a chan_voter instance that DOES NOT have
redundancy configured for it, even if a separate instance needs to be created just for this purpose.

Also, if Non-GPS-based operation is all that is needed, just the use of redundancy within the clients is
sufficient, and does not require any use of the server redundancy features.

"hostdeemp" (app_rpt duplex=3) mode:

As of Voter board firmware 1.19 (7/19/2013), there is a set of options in both the firmware ("Offline Menu item
#12, "DUPLEX3 support"), and the "hostdeemp" option (instance-wide) in the voter.conf file on the host.

Duplex mode 3 in app_rpt allows for "in-cabinet" repeat audio (where the actual radio hardware supplies the repeated
audio directly itself, and app_rpt simply "adds" all of the other audio as appropriate.

The Voter board (RTCM) now has an option to do the same functionality itself, for a case where local repeat audio
is desired without the "network audio delay" normally associated with Voter board (RTCM) operation, and for a radio
that doesn't have the option of providing "in cabinet" repeat audio (along with externally provided audio) itself.

Because of limitations with the Voter board (RTCM) hardware (being that there is only 1 audio path into the processor,
and it either has de-emphasis in its "hardware path" of not), it is necessary if you:

1) Wish to have the "duplex=3" functionality in app_rpt
2) Have the "DUPLEX3" support enabled in the Voter (RTCM) board
3) Have a transmitter that you are "modulating directly" (with flat audio)

If all of the above is true, then you need to use the "hostdeemp" option in chan voter, which basically "forces" the
RTCM *NOT* to do de-emphasis in hardware (it will send the non-de-emphasized audio to the host), and have the host
"do" the de-emphasis (in software) instead.

This will allow the Voter (RTCM) board to be able to "pass" the non-de-emphasized audio back into the "direct modulation
audio" stream, since that is what will be "presented" to the processor in the Voter (RTCM) board, as the hardware de-emphasis
is disabled in this mode.

If you have a transmitter that you are "feeding" line-level (mic) audio, then this mode is not necessary, as the Voter (RTCM)
board is fully capable of providing the functionality all by itself.

Obviously, it is not valid to use *ANY* of the duplex=3 modes in a voted and/or simulcasted system.

*/

#include "asterisk.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/timex.h>
#include <ctype.h>
#include <search.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
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
#include "asterisk/translate.h"
#include "asterisk/cli.h"
#include "asterisk/ulaw.h"
#include "asterisk/dsp.h"
#include "asterisk/manager.h"
#include "asterisk/format.h"
#include "asterisk/format_cache.h"
#include "asterisk/format_compatibility.h"
#include "asterisk/timing.h"
#include "asterisk/rpt_chan_shared.h"

#include "../apps/app_rpt/pocsag.c"

/* Un-comment this if you wish Digital milliwatt output rather then real audio
   when transmitting (for debugging only) */
/* #define	DMWDIAG */

struct ast_flags zeroflag = { 0 };

#define	XPMR_VOTER
#include "xpmr/xpmr.h"

#define	VOTER_CHALLENGE_LEN 10
#define	VOTER_PASSWORD_LEN 30
#define	VOTER_NAME_LEN 50

#define	RX_TIMEOUT_MS 200
#define	CLIENT_TIMEOUT_MS 3000
#define	MASTER_TIMEOUT_MS 100
#define	TX_KEEPALIVE_MS 1000
#define	PING_TIME_MS 250
#define	PING_TIMEOUT_MS 3000

#define DEFAULT_LINGER 6
#define DEFAULT_GTXGAIN "0.0"

#define MAX_MASTER_COUNT 3
#define N_FMT(duf) "%30" #duf /* Maximum sscanf conversion to numeric strings */
#define CLIENT_WARN_SECS 60

#define DELIMCHR ','
#define QUOTECHR 34

#define MAXSTREAMS 50
#define MAXTHRESHOLDS 20

#define	GPS_WORK_FILE "/tmp/gps%s.tmp"
#define	GPS_DATA_FILE "/tmp/gps%s.dat"

#define	NTAPS_PL 6
#define	NTAPS_4K 6

#ifdef DMWDIAG
unsigned char ulaw_digital_milliwatt[8] = { 0x1e, 0x0b, 0x0b, 0x1e, 0x9e, 0x8b, 0x8b, 0x9e };

unsigned char mwp;
#endif

#define	IS_CLIENT_PROXY(x) (x->proxy_sin.sin_family == AF_INET)
#define	SEND_PRIMARY(x) (x->primary.sin_family == AF_INET)

/*! Defines for constructing POSAG paging packets */
#define	PAGER_SRC "PAGER"
#define	ENDPAGE_STR "ENDPAGE"
#define AMPVAL 30000
#define	SAMPRATE 8000			// (Sample Rate)
#define	DIVLCM 192000			// (Least Common Mult of 512,1200,2400,8000)
#define	PREAMBLE_BITS 576
#define	MESSAGE_BITS 544		// (17 * 32), 1 longword SYNC plus 16 longwords data
/* We have to send "inverted".. probably because of inverting AMP in Voter board */
#define	ONEVAL AMPVAL
#define ZEROVAL -AMPVAL
#define	DIVSAMP (DIVLCM / SAMPRATE)

static const char vdesc[] = "radio Voter channel driver";
static char type[] = "voter";

int run_forever = 1;
static int nullfd = -1;

AST_MUTEX_DEFINE_STATIC(voter_lock);

int16_t listen_port = 1667; /* port to listen to UDP packets on */
int udp_socket = -1;

struct ast_timer *voter_thread_timer = NULL;

int voter_timing_count = 0;
int last_master_count = 0;

int check_client_sanity = 1;

char challenge[VOTER_CHALLENGE_LEN];
char password[VOTER_PASSWORD_LEN];
char context[100];

double dnsec;

static pthread_t voter_reader_thread = 0;
static pthread_t voter_timer_thread = 0;

int maxpvtorder = 0;

#define	FRAME_SIZE 160
#define	ADPCM_FRAME_SIZE 163

#define	DEFAULT_BUFLEN 480		/* 480ms default buffer len */

#define BUFDELAY(p) (p->buflen - (FRAME_SIZE * 2))

#pragma pack(push)
#pragma pack(1)
typedef struct {
	uint32_t vtime_sec;
	uint32_t vtime_nsec;
} VTIME;

typedef struct {
	VTIME curtime;
	uint8_t challenge[10];
	uint32_t digest;
	uint16_t payload_type;
} VOTER_PACKET_HEADER;

typedef struct {
	char lat[9];
	char lon[10];
	char elev[7];
} VOTER_GPS;

typedef struct {
	char name[32];
	uint8_t audio[FRAME_SIZE];
	uint8_t rssi;
} VOTER_REC;

typedef struct {
	VTIME curtime;
	uint8_t audio[FRAME_SIZE];
	char str[152];
} VOTER_STREAM;

typedef struct {
	uint32_t ipaddr;
	uint16_t port;
	uint16_t payload_type;
	uint8_t flags;
	char challenge[VOTER_CHALLENGE_LEN];
} VOTER_PROXY_HEADER;

#pragma pack(pop)

/*! Defines voter payload types */
#define VOTER_PAYLOAD_NONE	0
#define VOTER_PAYLOAD_ULAW	1
#define	VOTER_PAYLOAD_GPS	2
#define VOTER_PAYLOAD_ADPCM	3
#define VOTER_PAYLOAD_NULAW	4
#define VOTER_PAYLOAD_PING	5
#define	VOTER_PAYLOAD_PROXY	0xf000

/*!
 * \brief Structure for one of our clients.
 */
struct voter_client {
	uint32_t nodenum;
	uint32_t digest;
	char name[VOTER_NAME_LEN];
	char pswd[VOTER_NAME_LEN];
	uint8_t *audio;
	uint8_t *rssi;
	uint32_t respdigest;
	struct sockaddr_in sin;
	int drainindex;
	int drainindex_40ms;
	int buflen;
	/* bit fields */
	unsigned int heardfrom:1;
	unsigned int totransmit:1;
	unsigned int ismaster:1;
	unsigned int curmaster:1;
	unsigned int doadpcm:1;
	unsigned int donulaw:1;
	unsigned int mix:1;
	unsigned int nodeemp:1;
	unsigned int noplfilter:1;
	unsigned int txlockout:1;
	unsigned int reload:1;
	unsigned int rxseq40ms:1;
	unsigned int drain40ms:1;
	unsigned int ping_abort:1;
	
	struct voter_client *next;
	uint8_t lastrssi;
	int txseqno;
	int txseqno_rxkeyed;
	int rxseqno;
	int rxseqno_40ms;
	int old_buflen;
	time_t warntime;
	char *gpsid;
	int prio;
	int prio_override;
	struct timeval lastheardtime;
	struct timeval lastsenttime;
	VTIME lastgpstime;
	VTIME lastmastergpstime;
	struct sockaddr_in proxy_sin;
	char saved_challenge[VOTER_CHALLENGE_LEN];
	short lastaudio[FRAME_SIZE];
	struct timeval ping_txtime;
	struct timeval ping_last_rxtime;
	unsigned int ping_last_seqno;
	int pings_requested;
	int pings_sent;
	int pings_received;
	int pings_oos;
	int pings_worst;
	int pings_best;
	unsigned int ping_seqno;
	int pings_total_ms;
};

/*!
 * \brief Structure for one of our channels.
 */
struct voter_pvt {
	struct ast_channel *owner;
	unsigned int nodenum;		/* node # associated with this instance */
	struct voter_pvt *next;
	struct ast_frame fr;
	char buf[FRAME_SIZE + AST_FRIENDLY_OFFSET];
	struct ast_module_user *u;
	struct timeval lastrxtime;
	/* bit fields */
	unsigned int txkey:1;
	unsigned int rxkey:1;
	unsigned int drained_once:1;
	unsigned int plfilter:1;
	unsigned int hostdeemp:1;
	unsigned int duplex:1;
	unsigned int usedtmf:1;
	unsigned int isprimary:1;
	unsigned int priconn:1;
	unsigned int mixminus:1;
	unsigned int waspager:1;
	unsigned int kill_xmit_thread:1;
	unsigned int kill_primary_thread:1;

	int testcycle;
	int testindex;
	struct voter_client *lastwon;
	char *streams[MAXSTREAMS];
	int nstreams;
	float hpx[NTAPS_PL + 1];
	float hpy[NTAPS_PL + 1];
	float rlpx[NTAPS_4K + 1];
	float rlpy[NTAPS_4K + 1];
	float tlpx[NTAPS_4K + 1];
	float tlpy[NTAPS_4K + 1];
	int32_t hdx;
	int linger;
	uint8_t rssi_thresh[MAXTHRESHOLDS];
	uint16_t count_thresh[MAXTHRESHOLDS];
	uint16_t linger_thresh[MAXTHRESHOLDS];
	int nthresholds;
	int threshold;
	struct voter_client *winner;
	uint16_t threshcount;
	uint16_t lingercount;
	int voter_test;
	struct ast_dsp *dsp;
	struct ast_trans_pvt *adpcmin;
	struct ast_trans_pvt *adpcmout;
	struct ast_trans_pvt *nuin;
	struct ast_trans_pvt *nuout;
	struct ast_trans_pvt *toast;
	struct ast_trans_pvt *toast1;
	struct ast_trans_pvt *fromast;
	t_pmr_chan *pmrChan;
	char txctcssfreq[32];
	int txctcsslevel;
	int txctcsslevelset;
	enum usbradio_carrier_type txtoctype;
	int order;
	struct ast_frame *adpcmf1;
	struct ast_frame *nulawf1;
	ast_mutex_t xmit_lock;
	ast_cond_t xmit_cond;
	pthread_t xmit_thread;
	pthread_t primary_thread;
	struct sockaddr_in primary;
	char primary_pswd[VOTER_NAME_LEN];
	char primary_challenge[VOTER_CHALLENGE_LEN];
	float gtxgain;
	FILE *recfp;
	short lastaudio[FRAME_SIZE];

	AST_LIST_HEAD_NOLOCK(, ast_frame) txq;
	AST_LIST_HEAD_NOLOCK(, ast_frame) pagerq;
	ast_mutex_t txqlock;
	ast_mutex_t pagerqlock;
};

static int reload(void);

int hasmaster = 0;

/*! This is just a horrendous KLUDGE!! Some Garmin LVC-18 GPS "pucks"
 sometimes get exactly 1 second off!! Some don't do it at all,
 while others do it constantly. Others do it once in a while. In an attempt
 to be at least somewhat tolerant of this operation, the "puckit"
 configuration flag may be set. We attempt to deal with  this problem by 
 keeping a "time differential" for each client (compared with the "master") 
 and applying it to time information within the protocol.
 Obviously, this SHOULD NEVER HAVE TO BE DONE. 
 */
int puckit = 0;

static char *config = "voter.conf";			/* default config file */

struct voter_pvt *pvts = NULL;

struct voter_client *clients = NULL;

FILE *fp;

VTIME master_time = { 0, 0 };
VTIME mastergps_time = { 0, 0 };

static struct ast_channel *voter_request(const char *type, struct ast_format_cap *cap,
										 const struct ast_assigned_ids *assignedids,
										 const struct ast_channel *requestor, const char *data, int *cause);
static int voter_call(struct ast_channel *ast, const char *dest, int timeout);
static int voter_hangup(struct ast_channel *ast);
static struct ast_frame *voter_read(struct ast_channel *ast);
static int voter_write(struct ast_channel *ast, struct ast_frame *frame);
static int voter_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen);
static int voter_digit_begin(struct ast_channel *c, char digit);
static int voter_digit_end(struct ast_channel *c, char digit, unsigned int duratiion);
static int voter_text(struct ast_channel *c, const char *text);
static int voter_setoption(struct ast_channel *chan, int option, void *data, int datalen);

/*!
 * \brief Asterisk channel technology struct.
 * This tells Asterisk the functions to call when
 * it needs to interact with our module.
 */
static struct ast_channel_tech voter_tech = {
	.type = type,
	.description = vdesc,
	.requester = voter_request,
	.call = voter_call,
	.hangup = voter_hangup,
	.read = voter_read,
	.write = voter_write,
	.indicate = voter_indicate,
	.send_text = voter_text,
	.send_digit_begin = voter_digit_begin,
	.send_digit_end = voter_digit_end,
	.setoption = voter_setoption,
};

/*
* CLI extensions
*/

/* Test */
static int voter_do_test(int fd, int argc, const char *const *argv);

/* Prio */
static int voter_do_prio(int fd, int argc, const char *const *argv);

/* Record */
static int voter_do_record(int fd, int argc, const char *const *argv);

/* Tone */
static int voter_do_tone(int fd, int argc, const char *const *argv);

/* Display */
static int voter_do_display(int fd, int argc, const char *const *argv);

/* Txlockout */
static int voter_do_txlockout(int fd, int argc, const char *const *argv);

/* Ping client */
static int voter_do_ping(int fd, int argc, const char *const *argv);

/* CRC polynomial 0xedb88320 */
static uint32_t crc_32_tab[] = {	
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
	0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
	0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
	0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
	0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
	0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
	0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
	0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
	0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
	0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
	0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
	0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
	0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
	0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
	0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
	0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
	0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
	0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
	0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
	0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

/*!
 * \brief Calculate the crc for two buffers.
 * \param buf			First buffer.
 * \param buf1			Seconds buffer.
 * \return				Computed CRC.
 */
static int32_t crc32_bufs(char * restrict buf, char * restrict buf1)
{
	register int32_t oldcrc32;

	oldcrc32 = 0xFFFFFFFF;
	while (buf && *buf) {
		oldcrc32 = crc_32_tab[(oldcrc32 ^ *buf++) & 0xff] ^ ((uint32_t) oldcrc32 >> 8);
	}
	while (buf1 && *buf1) {
		oldcrc32 = crc_32_tab[(oldcrc32 ^ *buf1++) & 0xff] ^ ((uint32_t) oldcrc32 >> 8);
	}
	return ~oldcrc32;
}

#define GAIN1   1.745882764e+00
/*!
 * \brief IIR High pass filter.
 * IIR 6 pole High pass filter, 300 Hz corner with 0.5 db ripple
 * \param input		Audio value to filter.
 * \param xv		Delay line.
 * \param yv		Delay line.
 * \return 			Filtered value.
 * \todo	This filter needs more documentation.
 */
static int16_t hpass6(int16_t input, float * restrict xv, float * restrict yv)
{
	xv[0] = xv[1];
	xv[1] = xv[2];
	xv[2] = xv[3];
	xv[3] = xv[4];
	xv[4] = xv[5];
	xv[5] = xv[6];
	xv[6] = ((float) input) / GAIN1;
	yv[0] = yv[1];
	yv[1] = yv[2];
	yv[2] = yv[3];
	yv[3] = yv[4];
	yv[4] = yv[5];
	yv[5] = yv[6];
	yv[6] = (xv[0] + xv[6]) - 6 * (xv[1] + xv[5]) + 15 * (xv[2] + xv[4])
		- 20 * xv[3]
		+ (-0.3491861578 * yv[0]) + (2.3932556573 * yv[1])
		+ (-6.9905126572 * yv[2]) + (11.0685981760 * yv[3])
		+ (-9.9896695552 * yv[4]) + (4.8664511065 * yv[5]);
	return ((int) yv[6]);
}

#define GAIN2   1.080715413e+02
/*!
 * \brief IIR Low pass filter.
 * IIR 6 pole Low pass filter, 1900 Hz corner with 0.5 db ripple
 * \param input		Audio value to filter.
 * \param xv		Delay line.
 * \param yv		Delay line.
 * \return 			Filtered value.
 * \todo	This filter needs more documentation.
 */
static int16_t lpass4(int16_t input, float * restrict xv, float * restrict yv)
{
	xv[0] = xv[1];
	xv[1] = xv[2];
	xv[2] = xv[3];
	xv[3] = xv[4];
	xv[4] = xv[5];
	xv[5] = xv[6];
	xv[6] = ((float) input) / GAIN2;
	yv[0] = yv[1];
	yv[1] = yv[2];
	yv[2] = yv[3];
	yv[3] = yv[4];
	yv[4] = yv[5];
	yv[5] = yv[6];
	yv[6] = (xv[0] + xv[6]) + 6 * (xv[1] + xv[5]) + 15 * (xv[2] + xv[4])
		+ 20 * xv[3]
		+ (-0.1802140297 * yv[0]) + (0.7084527003 * yv[1])
		+ (-1.5847014566 * yv[2]) + (2.3188475168 * yv[3])
		+ (-2.5392334760 * yv[4]) + (1.6846484378 * yv[5]);
	return ((int) yv[6]);
}

/*!
 * \brief Deemphasis filter.
 * Perform standard 6db/octave de-emphasis.
 * FIR integrator at 8000 samples/second.
 * \param input		Audio value to filter.
 * \param state		State variable.
 * \return 			Filtered value.
 * \todo	This filter needs more documentation.
 */
static int16_t deemp1(int16_t input, int32_t * restrict state0)
{

	register int32_t accum;
	register int16_t output;

	accum = input;
	*state0 = accum + (*state0 * 25889) / M_Q15;
	accum = (*state0 * 6878) / (M_Q15 / 4);
	output = accum;
	return output;
}

/*!
 * \brief Split a string into tokens using DELIMCHR and QUOTECHR and store pointers to each token.
 *
 * The function modifies the input string in-place: delimiter characters and closing
 * quote characters are replaced with NUL ('\0') bytes. Text enclosed by QUOTECHR is
 * treated as a single token and the surrounding quote characters are removed.
 *
 * \param str   Pointer to the NUL-terminated string to tokenize; the buffer is modified.
 * \param strp  Array to receive pointers to token start positions and a terminating NULL.
 *              Must have space for at least `limit` pointers.
 * \param limit Maximum number of pointers available in `strp`. The function will stop
 *              scanning when it has filled `limit - 1` token entries and will always
 *              write a terminating NULL at `strp[index]`.
 * \return      The number of tokens placed into `strp` (zero if none).
 */
static int finddelim(char *str, char *strp[], size_t limit)
{
	int i, inquo;

	inquo = 0;
	i = 0;
	strp[i++] = str;
	if (!*str) {
		strp[0] = 0;
		return 0;
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
		if ((*str == DELIMCHR) && !inquo) {
			*str = 0;
			strp[i++] = str + 1;
		}
	}
	strp[i] = 0;
	return i;

}

/*!
 * \brief Determine difference in two timevals in milliseconds.
 * \param x		First timeval.
 * \param y		Second timeval.
 * \return      Difference in milliseconds.
 */
static unsigned int voter_tvdiff_ms(const struct timeval x, const struct timeval y)
{
	int i;

	i = ast_tvdiff_ms(x, y);
	if (i < 0) {
		i = INT32_MAX;
	}
	return i;
}

/*!
 * \brief Determine the puck (GPS) offset time.
 *	Calculates the difference in time between the master GPS 
 *	time and the client's last GPS time.
 * \note This is only used for Garmin LVC-18 when the
 *	puckit configuration value is true.
 * \param client		Pointer to voter_client struct.
 * \return				Time difference in nanoseconds.
 */
static long long puckoffset(const struct voter_client *client)
{
	long long btime, ptime, difftime;

	if (!puckit) {
		return 0;
	}
	btime = ((long long) client->lastmastergpstime.vtime_sec * 1000000000LL) + client->lastmastergpstime.vtime_nsec;
	ptime = ((long long) client->lastgpstime.vtime_sec * 1000000000LL) + client->lastgpstime.vtime_nsec;
	difftime = ptime - btime;
	
	return difftime;
}

/*!
 * \brief Adjust the puck (GPS) time.
 * \param client		Pointer to voter_client struct.
 * \param dst			Pointer to VTIME struct.
 */
static void mkpucked(const struct voter_client *client, VTIME * dst)
{
	long long btime;

	btime = ((long long) master_time.vtime_sec * 1000000000LL) + master_time.vtime_nsec;
	btime += puckoffset(client);
	dst->vtime_nsec = htonl((long) (btime % 1000000000LL));
	dst->vtime_sec = htonl((long) (btime / 1000000000LL));
}

/*!
 * \brief Increment the drain index for the specified instance.
 * Call this routine with voter_locked locked.
 * \param p			Pointer to voter_pvt struct.
 */
static void incr_drainindex(const struct voter_pvt *p)
{
	struct voter_client *client;

	if (p == NULL) {
		return;
	}
	for (client = clients; client; client = client->next) {
		if (client->nodenum != p->nodenum) {
			continue;
		}
		if (!client->drain40ms) {
			client->drainindex_40ms = client->drainindex;
			client->rxseqno_40ms = client->rxseqno;
		}
		client->drainindex += FRAME_SIZE;
		if (client->drainindex >= client->buflen) {
			client->drainindex -= client->buflen;
		}
		client->drain40ms = !client->drain40ms;
	}
}

/*!
 * \brief Voter call.
 * \param c				Asterisk channel.
 * \param dest			Destination.
 * \param timeout		Timeout.
 * \retval -1 			if not successful.
 * \retval 0 			if successful.
 */
static int voter_call(struct ast_channel *ast, const char *dest, int timeout)
{
	if ((ast_channel_state(ast) != AST_STATE_DOWN) && (ast_channel_state(ast) != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "voter_call called on %s, neither down nor reserved\n", ast_channel_name(ast));
		return -1;
	}
	/* When we call, it just works, really, there's no destination...  Just
	   ring the phone and wait for someone to answer */
	ast_debug(1, "Channel %s: Calling %s\n",ast_channel_name(ast), dest);
	ast_setstate(ast, AST_STATE_UP);
	return 0;
}

/*!
 * \brief Asterisk hangup function.
 * \param c Asterisk channel.
 * \return  Always returns 0.
 */
static int voter_hangup(struct ast_channel *ast)
{
	struct voter_pvt *q, *p = ast_channel_tech_pvt(ast);

	ast_debug(1, "Channel %s: Hangup\n", ast_channel_name(ast));
	if (!p) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	/* free our resources */
	if (p->dsp) {
		ast_dsp_free(p->dsp);
	}
	if (p->adpcmin) {
		ast_translator_free_path(p->adpcmin);
	}
	if (p->adpcmout) {
		ast_translator_free_path(p->adpcmout);
	}
	if (p->toast) {
		ast_translator_free_path(p->toast);
	}
	if (p->toast1) {
		ast_translator_free_path(p->toast1);
	}
	if (p->fromast) {
		ast_translator_free_path(p->fromast);
	}
	if (p->nuin) {
		ast_translator_free_path(p->nuin);
	}
	if (p->nuout) {
		ast_translator_free_path(p->nuout);
	}
	ast_mutex_lock(&voter_lock);
	for (q = pvts; q->next; q = q->next) {
		if (q->next == p) {
			break;
		}
	}
	if (q->next) {
		q->next = p->next;
	}
	if (pvts == p) {
		pvts = p->next;
	}
	if (p->xmit_thread) {
		p->kill_xmit_thread = 1;
		ast_mutex_lock(&p->xmit_lock);
		ast_cond_signal(&p->xmit_cond);
		ast_mutex_unlock(&p->xmit_lock);
		pthread_join(p->xmit_thread, NULL);
	}
	if (p->primary_thread) {
		p->kill_primary_thread = 1;
		pthread_join(p->primary_thread, NULL);
	}
	ast_mutex_unlock(&voter_lock);
	ast_free(p);
	ast_channel_tech_pvt_set(ast, NULL);
	ast_setstate(ast, AST_STATE_DOWN);
	return 0;
}

/*!
 * \brief Asterisk indicate function.
 * This is used to indicate tx key / unkey.
 * \param c				Asterisk channel.
 * \param cond			Condition.
 * \param data			Data.
 * \param datalen		Data length.
 * \retval 0			If successful.
 * \retval -1			For hangup.
 */
static int voter_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen)
{
	struct voter_pvt *p = ast_channel_tech_pvt(ast);

	switch (cond) {
	case AST_CONTROL_RADIO_KEY:
		p->txkey = 1;
		ast_debug(1, "Channel %s: TX On\n", ast_channel_name(ast));
		break;
	case AST_CONTROL_RADIO_UNKEY:
		p->txkey = 0;
		ast_debug(1, "Channel %s: TX Off\n", ast_channel_name(ast));
		break;
	case AST_CONTROL_HANGUP:
		return -1;
	default:
		ast_log(LOG_WARNING, "Channel %s: Don't know how to display condition %d.\n", ast_channel_name(ast), cond);
		return 0;
	}

	return 0;
}

/*!
 * \brief Asterisk digit begin function.
 * \param ast			Asterisk channel.
 * \param digit			Digit processed.
 * \retval 0			
 */
static int voter_digit_begin(struct ast_channel *ast, char digit)
{
	return 0;
}

/*!
 * \brief Asterisk digit end function.
 * \param ast			Asterisk channel.
 * \param digit			Digit processed.
 * \param duration		Duration of the digit.
 * \retval 0			
 */
static int voter_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	/* no better use for received digits than print them */
	ast_debug(3, " << Console Received digit %c of duration %u ms >> \n", digit, duration);
	return 0;
}

/*!
 * \brief Asterisk setoption function.
 * \param chan			Asterisk channel.
 * \param option		Option.
 * \param data			Data.
 * \param datalen		Data length.
 * \retval 0			If successful.
 * \retval -1			If failed.
 */
static int voter_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
	char *cp;
	struct voter_pvt *o = ast_channel_tech_pvt(chan);

	/* all supported options require data */
	if (!data || (datalen < 1)) {
		errno = EINVAL;
		return -1;
	}

	switch (option) {
	case AST_OPTION_TONE_VERIFY:
		cp = (char *) data;
		switch (*cp) {
		case 1:
			ast_debug(1, "Channel %s: Set option TONE VERIFY, mode: OFF(0)\n", ast_channel_name(chan));
			o->usedtmf = 1;
			break;
		case 2:
			ast_debug(1, "Channel %s: Set option TONE VERIFY, mode: MUTECONF/MAX(2)\n", ast_channel_name(chan));
			o->usedtmf = 1;
			break;
		case 3:
			ast_debug(1, "Channel %s: Set option TONE VERIFY, mode: DISABLE DETECT(3)\n", ast_channel_name(chan));
			o->usedtmf = 0;
			break;
		default:
			ast_debug(1, "Channel %s: Set option TONE VERIFY, mode: OFF(0)\n", ast_channel_name(chan));
			o->usedtmf = 1;
			break;
		}
		break;
	}
	errno = 0;
	return 0;
}

/*!
 * \brief Make paging audio samples.
 * \param audio			Audio buffer.
 * \param data			Data to encode into audio.
 * \param audio_ptr		Audio buffer pointer.
 * \param divcnt		The running count of the number of samples encoded per bit.
 *						This tracks our samples as we create the wave form.
 * \param divdiv		The number of samples to encode per bit.
 */
static void mkpsamples(short* restrict audio, uint32_t x, int* restrict audio_ptr, int* restrict divcnt, int divdiv)
{
	register int i;

	for (i = 31; i >= 0; i--) {
		while (*divcnt < divdiv) {
			audio[(*audio_ptr)++] = (x & (1 << i)) ? ONEVAL : ZEROVAL;
			*divcnt += DIVSAMP;
		}
		if (*divcnt >= divdiv)
			*divcnt -= divdiv;
	}
}

/*!
 * \brief Asterisk text function.
 * \param c				Asterisk channel.
 * \param text			Text message to process.
 * \retval 0			If successful.
 * \retval -1			If unsuccessful.
 */
static int voter_text(struct ast_channel *ast, const char *text)
{
	struct voter_pvt *o = ast_channel_tech_pvt(ast);
	int cnt, i, j, audio_samples, divcnt, divdiv, audio_ptr, baud;
	struct pocsag_batch *batch, *b;
	short *audio;
	char *cmd, audio1[AST_FRIENDLY_OFFSET + (FRAME_SIZE * sizeof(short))];
	struct ast_frame wf, *f1;

	cmd = ast_alloca(strlen(text) + 10);

	/* print received messages */
	ast_debug(3, "Channel %s: Console Received voter text %s >> \n", ast_channel_name(ast), text);

	if (!strncmp(text, "PAGE", 4)) {
		if (!o->pmrChan) {
			ast_log(LOG_WARNING, "Channel %s: Attempt to page on a non-flat-audio VOTER config\n", ast_channel_name(ast));
			return 0;
		}
		cnt = sscanf(text, "%s " N_FMT(d) " " N_FMT(d) " %n", cmd, &baud, &i, &j);
		if (cnt < 3) {
			return 0;
		}
		if (strlen(text + j) < 1) {
			return 0;
		}
		switch (text[j]) {
		case 'T':				/* Tone only */
			ast_verb(3, "Channel %s: POCSAG page (%d baud, capcode=%d) TONE ONLY\n", ast_channel_name(ast), baud, i);
			batch = make_pocsag_batch(i, NULL, 0, TONE, 0);
			break;
		case 'N':				/* Numeric */
			if (!text[j + 1]) {
				return 0;
			}
			ast_verb(3, "Channel %s: POCSAG page (%d baud, capcode=%d) NUMERIC (%s)\n", ast_channel_name(ast), baud, i, text + j + 1);
			batch = make_pocsag_batch(i, (char *) text + j + 1, strlen(text + j + 1), NUMERIC, 0);
			break;
		case 'A':				/* Alpha */
			if (!text[j + 1]) {
				return 0;
			}
			ast_verb(3, "Channel %s: POCSAG page (%d baud, capcode=%d) ALPHA (%s)\n", ast_channel_name(ast), baud, i, text + j + 1);
			batch = make_pocsag_batch(i, (char *) text + j + 1, strlen(text + j + 1), ALPHA, 0);
			break;
		case '?':				/* Query Page Status */
			i = 0;
			ast_mutex_lock(&o->txqlock);
			AST_LIST_TRAVERSE(&o->txq, f1, frame_list) if (f1->src && (!strcmp(f1->src, PAGER_SRC)))
				i++;
			ast_mutex_unlock(&o->txqlock);
			cmd = (i) ? "PAGES" : "NOPAGES";
			memset(&wf, 0, sizeof(wf));
			wf.frametype = AST_FRAME_TEXT;
			wf.src = __PRETTY_FUNCTION__;
			wf.datalen = strlen(cmd);
			wf.data.ptr = cmd;
			ast_queue_frame(o->owner, &wf);
			return 0;
		default:
			return 0;
		}
		if (!batch) {
			ast_log(LOG_ERROR, "Channel %s: Error creating POCSAG page.\n", ast_channel_name(ast));
			return 0;
		}
		b = batch;
		for (i = 0; b; b = b->next) {
			i++;
		}
		/* get number of samples to alloc for audio */
		audio_samples = (SAMPRATE * (PREAMBLE_BITS + (MESSAGE_BITS * i))) / baud;
		/* pad end with 250ms of silence on each side */
		audio_samples += SAMPRATE / 2;
		/* also pad up to FRAME_SIZE */
		audio_samples += audio_samples % FRAME_SIZE;
		audio = ast_calloc(1, (audio_samples * sizeof(short)) + 10);
		if (!audio) {
			free_batch(batch);
			return 0;
		}
		divdiv = DIVLCM / baud;
		divcnt = 0;
		audio_ptr = SAMPRATE / 4;
		for (i = 0; i < (PREAMBLE_BITS / 32); i++) {
			mkpsamples(audio, 0xaaaaaaaa, &audio_ptr, &divcnt, divdiv);
		}
		b = batch;
		while (b) {
			mkpsamples(audio, b->sc, &audio_ptr, &divcnt, divdiv);
			for (j = 0; j < 8; j++) {
				for (i = 0; i < 2; i++) {
					mkpsamples(audio, b->frame[j][i], &audio_ptr, &divcnt, divdiv);
				}
			}
			b = b->next;
		}
		free_batch(batch);
		memset(audio1, 0, sizeof(audio1));
		for (i = 0; i < audio_samples; i += FRAME_SIZE) {
			memset(&wf, 0, sizeof(wf));
			wf.frametype = AST_FRAME_VOICE;
			wf.subclass.format = ast_format_slin;
			wf.samples = FRAME_SIZE;
			wf.datalen = FRAME_SIZE * 2;
			wf.offset = AST_FRIENDLY_OFFSET;
			wf.data.ptr = audio1 + AST_FRIENDLY_OFFSET;
			wf.src = PAGER_SRC;
			memcpy(wf.data.ptr, (char *) (audio + i), FRAME_SIZE * 2);
			f1 = ast_frdup(&wf);
			if (!f1) {
				ast_free(audio);
				return 0;
			}
			memset(&f1->frame_list, 0, sizeof(f1->frame_list));
			ast_mutex_lock(&o->pagerqlock);
			AST_LIST_INSERT_TAIL(&o->pagerq, f1, frame_list);
			ast_mutex_unlock(&o->pagerqlock);
		}
		ast_free(audio);
		return 0;
	}
	ast_log(LOG_ERROR, "Channel %s: Cannot parse VOTER cmd: %s\n", ast_channel_name(ast), text);
	return 0;
}

/*!
 * \brief Asterisk read function.
 * \param ast			Asterisk channel.
 * \retval 				Asterisk frame.
 */
static struct ast_frame *voter_read(struct ast_channel *ast)
{
	struct voter_pvt *p = ast_channel_tech_pvt(ast);

	memset(&p->fr, 0, sizeof(struct ast_frame));
	p->fr.frametype = AST_FRAME_NULL;
	return &p->fr;
}

/*!
 * \brief Asterisk write function.
 * This routine handles asterisk to radio frames.
 * \param ast			Asterisk channel.
 * \param frame			Asterisk frame to process.
 * \retval 0			Successful.
 */
static int voter_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct voter_pvt *p = ast_channel_tech_pvt(ast);
	struct ast_frame *f1;

	if (frame->frametype != AST_FRAME_VOICE) {
		return 0;
	}

	if (!p->txkey) {
		return 0;
	}

	if (hasmaster && (!master_time.vtime_sec)) {
		ast_mutex_lock(&p->txqlock);
		while ((f1 = AST_LIST_REMOVE_HEAD(&p->txq, frame_list)) != NULL) {
			ast_frfree(f1);
		}
		ast_mutex_unlock(&p->txqlock);
		return 0;
	}

	if (fp != NULL) {
		fwrite(frame->data.ptr, 1, frame->datalen, fp);
	}
	f1 = ast_frdup(frame);
	if (!f1) {
		return 0;
	}
	if (p->gtxgain != 1.0) {
		register int x1;
		short* restrict sp;
		register float fsamp;

		sp = (short *) f1->data.ptr;
		for (x1 = 0; x1 < f1->datalen / 2; x1++) {
			fsamp = (float) sp[x1] * p->gtxgain;
			if (fsamp > 32765.0) {
				fsamp = 32765.0;
			} else if (fsamp < -32765.0) {
				fsamp = -32765.0;
			}
			sp[x1] = (int) fsamp;
		}
	}
	memset(&f1->frame_list, 0, sizeof(f1->frame_list));
	ast_mutex_lock(&p->txqlock);
	AST_LIST_INSERT_TAIL(&p->txq, f1, frame_list);
	ast_mutex_unlock(&p->txqlock);

	return 0;
}
/*!
 * \brief Concatenates two asterisk frames.
 * \param f1 Pointer to asterisk frame.
 * \param f2 Pointer to asterisk frame.
 * \return   Concatenated frame, or NULL if there is an error.
 */

static struct ast_frame *ast_frcat(const struct ast_frame * restrict f1, const struct ast_frame * restrict f2)
{
	struct ast_frame *f;
	char* restrict cp;
	int len;

	/* The two frames must be of the same type */
	if ((f1->subclass.integer != f2->subclass.integer) || (f1->frametype != f2->frametype)) {
		ast_log(LOG_ERROR, "ast_frcat() called with non-matching frame types!!\n");
		return NULL;
	}
	f = ast_calloc(1, sizeof(struct ast_frame));
	if (!f) {
		return NULL;
	}
	/* Allocate memory for the two data elements */
	len = f1->datalen + f2->datalen + AST_FRIENDLY_OFFSET;
	cp = ast_malloc(len);
	if (!cp) {
		return NULL;
	}
	memcpy(cp + AST_FRIENDLY_OFFSET, f1->data.ptr, f1->datalen);
	memcpy(cp + AST_FRIENDLY_OFFSET + f1->datalen, f2->data.ptr, f2->datalen);
	f->frametype = f1->frametype;
	f->subclass.integer = f1->subclass.integer;
	f->datalen = f1->datalen + f2->datalen;
	f->samples = f1->samples + f2->samples;
	f->data.ptr = cp + AST_FRIENDLY_OFFSET;;
	f->mallocd = AST_MALLOCD_HDR | AST_MALLOCD_DATA;
	f->src = "ast_frcat";
	f->offset = AST_FRIENDLY_OFFSET;
	return f;
}

/*!
 * \brief Mix and send audio packet.
 * This routine must be called with voter_locked locked.
 * \param p				Pointer to voter_pvt struct.
 * \param maxclient		Pointer to voter_client struct.
 * \param maxrssi		Maximum RSSI value.
 * \retval 0			Successful.
 * \retval 1			Successful.
 */
static int voter_mix_and_send(struct voter_pvt *p, struct voter_client *maxclient, int maxrssi)
{
	int i, j, k, x, maxprio, haslastaudio;
	struct ast_frame fr, *f1, *f2;
	struct voter_client *client;
	short silbuf[FRAME_SIZE];

	haslastaudio = 0;
	memset(&fr, 0, sizeof(fr));
	fr.frametype = AST_FRAME_VOICE;
	fr.subclass.format = ast_format_ulaw;
	fr.datalen = FRAME_SIZE;
	fr.samples = FRAME_SIZE;
	fr.data.ptr = p->buf + AST_FRIENDLY_OFFSET;
	fr.offset = AST_FRIENDLY_OFFSET;
	fr.src = __PRETTY_FUNCTION__;
	f1 = ast_translate(p->toast, &fr, 0);
	if (!f1) {
		ast_log(LOG_ERROR, "VOTER %i: Can not translate frame to send to Asterisk\n", p->nodenum);
		return 0;
	}
	maxprio = 0;
	for (client = clients; client; client = client->next) {
		if (client->nodenum != p->nodenum) {
			continue;
		}
		if (!client->mix) {
			continue;
		}
		if (client->prio_override == -1) {
			continue;
		}
		if (client->prio_override > -2) {
			i = client->prio_override;
		} else {
			i = client->prio;
		}
		if (i > maxprio) {
			maxprio = i;
		}
	}
	/* f1 now contains the voted-upon audio in slinear */
	for (client = clients; client; client = client->next) {
		short *sp1, *sp2;
		if (client->nodenum != p->nodenum) {
			continue;
		}
		if (!client->mix) {
			continue;
		}
		if (client->prio_override == -1) {
			continue;
		}
		if (maxprio) {
			if (client->prio_override > -2) {
				i = client->prio_override;
			} else {
				i = client->prio;
			}
			if (i < maxprio) {
				continue;
			}
		}
		i = (int) client->buflen - ((int) client->drainindex + FRAME_SIZE);
		if (i >= 0) {
			memcpy(p->buf + AST_FRIENDLY_OFFSET, client->audio + client->drainindex, FRAME_SIZE);
		} else {
			memcpy(p->buf + AST_FRIENDLY_OFFSET, client->audio + client->drainindex, FRAME_SIZE + i);
			memcpy(p->buf + AST_FRIENDLY_OFFSET + (client->buflen - i), client->audio, -i);
		}
		if (i >= 0) {
			memset(client->audio + client->drainindex, 0xff, FRAME_SIZE);
		} else {
			memset(client->audio + client->drainindex, 0xff, FRAME_SIZE + i);
			memset(client->audio, 0xff, -i);
		}
		k = 0;
		if (i >= 0) {
			for (j = client->drainindex; j < client->drainindex + FRAME_SIZE; j++) {
				k += client->rssi[j];
				client->rssi[j] = 0;
			}
		} else {
			for (j = client->drainindex; j < client->drainindex + (FRAME_SIZE + i); j++) {
				k += client->rssi[j];
				client->rssi[j] = 0;
			}
			for (j = 0; j < -i; j++) {
				k += client->rssi[j];
				client->rssi[j] = 0;
			}
		}
		client->lastrssi = k / FRAME_SIZE;
		if (client->lastrssi > maxrssi) {
			maxrssi = client->lastrssi;
			maxclient = client;
		}
		memset(&fr, 0, sizeof(fr));
		fr.frametype = AST_FRAME_VOICE;
		fr.subclass.format = ast_format_ulaw;
		fr.datalen = FRAME_SIZE;
		fr.samples = FRAME_SIZE;
		fr.data.ptr = p->buf + AST_FRIENDLY_OFFSET;
		fr.src = __PRETTY_FUNCTION__;
		fr.offset = AST_FRIENDLY_OFFSET;
		f2 = ast_translate(p->toast1, &fr, 0);
		if (!f2) {
			ast_log(LOG_ERROR, "VOTER %i: Can not translate frame to send to Asterisk\n", p->nodenum);
			ast_frfree(f1);
			return 0;
		}
		sp1 = f1->data.ptr;
		sp2 = f2->data.ptr;
		if (!haslastaudio) {
			memcpy(p->lastaudio, sp1, FRAME_SIZE * 2);
			haslastaudio = 1;
		}
		memcpy(client->lastaudio, sp2, FRAME_SIZE * 2);
		for (i = 0; i < FRAME_SIZE; i++) {
			if (maxprio && client->lastrssi) {
				j = sp2[i];
			} else {
				j = sp1[i] + sp2[i];
			}
			if (j > 32767) {
				j = 32767;
			} else if (j < -32767) {
				j = -32767;
			}
			sp1[i] = j;
		}
		ast_frfree(f2);
	}
	if (p->priconn) {
		maxclient = NULL;
	}
	if (!maxclient) {			/* if nothing there */
		/* XXX p->owner probably shouldn't be NULL, in which case this should be made an assertion, once this issue is fixed.
		 * For now, this prevents a crash from queuing a frame to a NULL channel. */
		if (!p->owner) {
			ast_log(LOG_WARNING, "Cannot queue frame, %p has no owner\n", p);
			ast_frfree(f1);
			return 0;
		}
		if (p->rxkey && p->dsp && p->usedtmf) {
			memset(silbuf, 0, sizeof(silbuf));
			memset(&fr, 0, sizeof(fr));
			fr.frametype = AST_FRAME_VOICE;
			fr.subclass.format = ast_format_slin;
			fr.datalen = FRAME_SIZE * 2;
			fr.samples = FRAME_SIZE;
			fr.data.ptr = silbuf;
			fr.src = __PRETTY_FUNCTION__;
			f2 = ast_dsp_process(NULL, p->dsp, &fr);
			if ((f2->frametype == AST_FRAME_DTMF_END) || (f2->frametype == AST_FRAME_DTMF_BEGIN)) {
				if ((f2->subclass.integer != 'm') && (f2->subclass.integer != 'u')) {
					if (f2->frametype == AST_FRAME_DTMF_END)
						ast_debug(1, "VOTER %d: Got DTMF char %c\n", p->nodenum, f2->subclass.integer);
				} else {
					f2->frametype = AST_FRAME_NULL;
					f2->subclass.integer = 0;
				}
				ast_queue_frame(p->owner, f2);
				gettimeofday(&p->lastrxtime, NULL);
			}
			ast_frfree(f2);
		}
		memset(silbuf, 0, sizeof(silbuf));
		memset(&fr, 0, sizeof(fr));
		fr.frametype = AST_FRAME_VOICE;
		fr.subclass.format = ast_format_slin;
		fr.datalen = FRAME_SIZE * 2;
		fr.samples = FRAME_SIZE;
		fr.data.ptr = silbuf;
		fr.src = __PRETTY_FUNCTION__;
		p->threshold = 0;
		p->threshcount = 0;
		p->lingercount = 0;
		p->winner = 0;
		incr_drainindex(p);
		ast_queue_frame(p->owner, &fr);
		ast_frfree(f1);
		return 0;
	}
	p->winner = maxclient;
	incr_drainindex(p);
	gettimeofday(&p->lastrxtime, NULL);
	if (!p->rxkey) {
		struct ast_frame fr = {
			.frametype = AST_FRAME_CONTROL,
			.subclass.integer = AST_CONTROL_RADIO_KEY,
			.src = __PRETTY_FUNCTION__,
		};

		ast_queue_frame(p->owner, &fr);
	}
	p->rxkey = 1;
	x = 0;
	if (p->dsp && p->usedtmf) {
		struct ast_frame *f3 = ast_frdup(f1); /* dsp_process frees frame, so dup f1 so we still have it later on */
		if (!f3) {
			ast_frfree(f1);
			return 0;
		}
		f2 = ast_dsp_process(NULL, p->dsp, f3);
		if ((f2->frametype == AST_FRAME_DTMF_END) || (f2->frametype == AST_FRAME_DTMF_BEGIN)) {
			if ((f2->subclass.integer != 'm') && (f2->subclass.integer != 'u')) {
				if (f2->frametype == AST_FRAME_DTMF_END)
					ast_debug(1, "VOTER %d: Got DTMF char %c\n", p->nodenum, f2->subclass.integer);
			} else {
				f2->frametype = AST_FRAME_NULL;
				f2->subclass.integer = 0;
			}
			ast_queue_frame(p->owner, f2);
			x = 1;
		}
		ast_frfree(f2);
	}
	if (!x) {
		ast_queue_frame(p->owner, f1);
	} else {
		memset(silbuf, 0, sizeof(silbuf));
		memset(&fr, 0, sizeof(fr));
		fr.frametype = AST_FRAME_VOICE;
		fr.subclass.format = ast_format_slin;
		fr.datalen = FRAME_SIZE * 2;
		fr.samples = FRAME_SIZE;
		fr.data.ptr = silbuf;
		fr.src = __PRETTY_FUNCTION__;
		ast_queue_frame(p->owner, &fr);
	}
	ast_frfree(f1);
	return 1;
}

/*!
 * \brief Manage the UDP-based primary-client keepalive and authentication for a node.
 *
 * Sends periodic authentication and GPS keepalive packets to the configured primary,
 * processes incoming primary responses to establish/maintain a primary session,
 * and updates per-client proxy state when the primary connection is lost.
 *
 * \param data Pointer to the per-node state struct (struct voter_pvt *).
 * \return     NULL when the thread exits.
 */
static void *voter_primary_client(void *data)
{
	struct voter_pvt *p = (struct voter_pvt *) data;
	int i, pri_socket, ms;
	char buf[4096];
	struct sockaddr_in sin;
	socklen_t fromlen;
	ssize_t recvlen;
	struct voter_client *client;
	struct timeval tv, lasttx, lastrx;
	VOTER_PACKET_HEADER *vph;
	uint32_t resp_digest, digest, mydigest;
#pragma pack(push)
#pragma pack(1)
	struct {
		VOTER_PACKET_HEADER vp;
		char flags;
	} authpacket;
#pragma pack(pop)

	ast_debug(3, "VOTER %i: Primary client thread started\n", p->nodenum);
	if ((pri_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		ast_log(LOG_ERROR, "Unable to create new socket for VOTER primary connection for instance %d\n", p->nodenum);
		pthread_exit(NULL);
		return NULL;
	}
	resp_digest = 0;
	digest = 0;
	lasttx = (struct timeval) {0};
	lastrx = (struct timeval) {0};
	ast_mutex_lock(&voter_lock);
	p->primary_challenge[0] = 0;
	while (run_forever && !ast_shutting_down() && !p->kill_primary_thread) {
		ast_mutex_unlock(&voter_lock);
		ms = 100;
		i = ast_waitfor_n_fd(&pri_socket, 1, &ms, NULL);
		ast_mutex_lock(&voter_lock);
		if (i == -1) {
			ast_mutex_unlock(&voter_lock);
			ast_log(LOG_ERROR, "VOTER %i: Error in select()\n", p->nodenum);
			pthread_exit(NULL);
		}
		gettimeofday(&tv, NULL);
		memset(&authpacket, 0, sizeof(authpacket));
		if (!p->priconn && (ast_tvzero(lasttx) || (voter_tvdiff_ms(tv, lasttx) >= 500))) {
			authpacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
			authpacket.vp.curtime.vtime_nsec = htonl(voter_timing_count);
			strcpy((char *) authpacket.vp.challenge, challenge);
			authpacket.vp.digest = htonl(resp_digest);
			authpacket.flags = 32;
			ast_debug(3, "VOTER %i: Sent primary client auth to %s:%d\n", p->nodenum, ast_inet_ntoa(p->primary.sin_addr),
				ntohs(p->primary.sin_port));
			sendto(pri_socket, &authpacket, sizeof(authpacket), 0, (struct sockaddr *) &p->primary, sizeof(p->primary));
			lasttx = tv;
		}
		if (p->priconn && (ast_tvzero(lasttx) || (voter_tvdiff_ms(tv, lasttx) >= 1000))) {
			authpacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
			authpacket.vp.curtime.vtime_nsec = htonl(voter_timing_count);
			strcpy((char *) authpacket.vp.challenge, challenge);
			authpacket.vp.digest = htonl(resp_digest);
			authpacket.vp.payload_type = htons(VOTER_PAYLOAD_GPS);
			ast_debug(5, "VOTER %i: Sent primary client GPS Keepalive to %s:%d\n", p->nodenum, ast_inet_ntoa(p->primary.sin_addr),
				ntohs(p->primary.sin_port));
			sendto(pri_socket, &authpacket, sizeof(authpacket) - 1, 0, (struct sockaddr *) &p->primary,
				sizeof(p->primary));
			lasttx = tv;
		}
		if (p->priconn && (ast_tvzero(lastrx) || (voter_tvdiff_ms(tv, lastrx) >= 2000))) {
			p->priconn = 0;
			digest = 0;
			p->primary_challenge[0] = 0;
			ast_verb(3, "VOTER %i: Primary client for %d  Lost connection!!!\n", p->nodenum, p->nodenum);
			for (client = clients; client; client = client->next) {
				if (client->nodenum != p->nodenum) {
					continue;
				}
				if (!IS_CLIENT_PROXY(client)) {
					continue;
				}
				client->respdigest = 0;
				client->heardfrom = 0;
			}
		}
		if (i < 0) {
			continue;
		}
		if (i == pri_socket) {
			fromlen = sizeof(struct sockaddr_in);
			recvlen = recvfrom(pri_socket, buf, sizeof(buf) - 1, 0, (struct sockaddr *) &sin, &fromlen);

			if (recvlen >= sizeof(VOTER_PACKET_HEADER)) {	/* if set got something worthwhile */
				vph = (VOTER_PACKET_HEADER *) buf;
				ast_debug(3, "VOTER %i: Got primary client X packet, len %d payload %d challenge %s digest %08x\n", p->nodenum,
					(int) recvlen, ntohs(vph->payload_type), vph->challenge, ntohl(vph->digest));
				/* if this is a new session */
				if (strcmp((char *) vph->challenge, p->primary_challenge)) {
					resp_digest = crc32_bufs((char *) vph->challenge, p->primary_pswd);
					strcpy(p->primary_challenge, (char *) vph->challenge);
					p->priconn = 0;
				} else {
					if (!digest || !vph->digest || (digest != ntohl(vph->digest)) || (ntohs(vph->payload_type) == 0) ||
						(ntohs(vph->payload_type) == VOTER_PAYLOAD_GPS)) {
						mydigest = crc32_bufs(challenge, password);
						if (mydigest == ntohl(vph->digest)) {
							digest = mydigest;
							if (!p->priconn) {
								ast_verb(3, "VOTER %i: Primary client connected (with challenge=%s)\n", p->nodenum, p->primary_challenge);
							}
							p->priconn = 1;
							lastrx = tv;
						} else {
							p->priconn = 0;
							digest = 0;
						}
					}
				}
			}
		}
	}
	pthread_exit(NULL);
	return NULL;
}

/*!
 * \brief See if the ping request has completed.
 * \param client		Pointer to voter_client struct.
 */
static void check_ping_done(struct voter_client *client)
{
	float p, q;

	if (!client->pings_requested) {
		return;
	}
	if (!client->ping_abort) {
		if (client->pings_sent < client->pings_requested) {
			return;
		}
		if (voter_tvdiff_ms(ast_tvnow(),
							(ast_tvzero(client->ping_last_rxtime)) ? client->ping_txtime : client->ping_last_rxtime) >
			PING_TIMEOUT_MS) {
			ast_log(LOG_WARNING, "\nPING (%s): RESPONSE TIMEOUT!!\n", client->name);
		} else {
			if (client->pings_received < client->pings_requested) {
				return;
			}
		}
	} else {
		ast_log(LOG_WARNING, "\nPING (%s): ABORTED!!\n", client->name);
		client->ping_abort = 0;
	}
	if (client->pings_sent) {
		p = 100.0 * (float) (client->pings_received - client->pings_oos) / (float) client->pings_sent;
	} else {
		p = 0.0;
	}
	if (client->pings_received) {
		q = (float) client->pings_total_ms / (float) client->pings_received;
	} else {
		q = 0;
	}
	ast_verb(1, "\nPING (%s): Packets tx: %d, rx: %d, oos: %d, Avg.: %0.3f ms\n", client->name, client->pings_sent,
		client->pings_received, client->pings_oos, q);
	ast_verb(1, "PING (%s):  Worst: %d ms, Best: %d ms, %0.1f%% Packets successfully received (%0.1f%% loss)\n", client->name,
		client->pings_worst, client->pings_best, p, 100.0 - p);
	client->pings_requested = 0;
}

/*!
 * \brief Manage and dispatch transmit activity for a single Voter node instance.
 *
 * Runs the per-node transmit worker: consumes queued Asterisk frames and pager frames,
 * integrates PMR channel input, performs optional mix-minus and format conversions,
 * and sends TX audio, keepalive, ping, and proxy packets to connected clients.
 *
 * \param data Pointer to the per-node voter_pvt instance.
 */
static void *voter_xmit(void *data)
{

	struct voter_pvt *p = (struct voter_pvt *) data;
	int i, n, x, mx;
	i16 dummybuf1[FRAME_SIZE * 12], xmtbuf1[FRAME_SIZE * 12];
	i16 xmtbuf[FRAME_SIZE], dummybuf2[FRAME_SIZE], xmtbuf2[FRAME_SIZE];
	i32 l;
	struct ast_frame fr, *f1, *f2, *f3, wf1;
	struct voter_client *client, *client1;
	struct timeval tv;

#pragma pack(push)
#pragma pack(1)
	struct {
		VOTER_PACKET_HEADER vp;
		char rssi;
		char audio[FRAME_SIZE + 3];
	} audiopacket;
	struct {
		VOTER_PACKET_HEADER vp;
		VOTER_PROXY_HEADER vprox;
		char rssi;
		char audio[FRAME_SIZE + 3];
	} proxy_audiopacket;
	struct {
		VOTER_PACKET_HEADER vp;
		unsigned int seqno;
		struct timeval txtime;
		struct timeval starttime;
		char filler[128];
	} pingpacket;
#pragma pack(pop)

	while (run_forever && !ast_shutting_down() && !p->kill_xmit_thread) {
		ast_mutex_lock(&p->xmit_lock);
		ast_cond_wait(&p->xmit_cond, &p->xmit_lock);
		ast_mutex_unlock(&p->xmit_lock);
		if (!p->drained_once) {
			p->drained_once = 1;
			continue;
		}
		n = x = 0;
		f2 = NULL;
		ast_mutex_lock(&p->txqlock);
		AST_LIST_TRAVERSE(&p->txq, f1, frame_list) n++;
		ast_mutex_unlock(&p->txqlock);
		if (n && ((n > 3) || (!p->txkey))) {
			x = 1;
			ast_mutex_lock(&p->txqlock);
			f2 = AST_LIST_REMOVE_HEAD(&p->txq, frame_list);
			ast_mutex_unlock(&p->txqlock);
			if (p->pmrChan) {
				p->pmrChan->txPttIn = 1;
				PmrTx(p->pmrChan, (i16 *) f2->data.ptr);
				ast_frfree(f2);
			}
		}
		f1 = NULL;
		// x will be set here is there was actual transmit activity
		if (!x && p->pmrChan) {
			p->pmrChan->txPttIn = 0;
		}
		if (x && (!p->pmrChan)) {
			memcpy(xmtbuf, f2->data.ptr, sizeof(xmtbuf));
			f1 = ast_translate(p->fromast, f2, 1);
			if (!f1) {
				ast_log(LOG_ERROR, "VOTER %i: Can not translate frame to receive from Asterisk\n", p->nodenum);
				ast_frfree(f2);
				continue;
			}
		}
		if (p->pmrChan) {
			if (p->pmrChan->txPttOut && (!x)) {
				memset(xmtbuf, 0, sizeof(xmtbuf));
				if (p->pmrChan) {
					PmrTx(p->pmrChan, xmtbuf);
				}
			}
			PmrRx(p->pmrChan, dummybuf1, dummybuf2, xmtbuf1);
			n = 0;
			ast_mutex_lock(&p->pagerqlock);
			AST_LIST_TRAVERSE(&p->pagerq, f1, frame_list) n++;
			ast_mutex_unlock(&p->pagerqlock);
			if (p->waspager && (n < 1)) {
				memset(&wf1, 0, sizeof(wf1));
				wf1.frametype = AST_FRAME_TEXT;
				wf1.src = __PRETTY_FUNCTION__;
				wf1.datalen = strlen(ENDPAGE_STR) + 1;
				wf1.data.ptr = ENDPAGE_STR;
				ast_queue_frame(p->owner, &wf1);
				p->waspager = 0;
			}
			if (n) {
				ast_mutex_lock(&p->pagerqlock);
				f3 = AST_LIST_REMOVE_HEAD(&p->pagerq, frame_list);
				f1 = ast_translate(p->fromast, f3, 1);
				if (!f1) {
					ast_mutex_unlock(&p->pagerqlock);
					ast_log(LOG_ERROR, "VOTER %i: Can not translate frame to receive from Asterisk\n", p->nodenum);
					ast_frfree(f3);
					continue;
				}
				ast_mutex_unlock(&p->pagerqlock);
				x = 1;
				p->waspager = 1;
			} else {
				x = p->pmrChan->txPttOut;
				for (i = 0; i < FRAME_SIZE; i++) {
					xmtbuf[i] = xmtbuf1[i * 2];
					if (xmtbuf[i] > 28000) {
						xmtbuf[i] = 28000;
					} else if (xmtbuf[i] < -28000) {
						xmtbuf[i] = -28000;
					}
				}
				memset(&fr, 0, sizeof(fr));
				fr.frametype = AST_FRAME_VOICE;
				fr.subclass.format = ast_format_slin;
				fr.datalen = FRAME_SIZE;
				fr.samples = FRAME_SIZE;
				fr.data.ptr = xmtbuf;
				fr.src = __PRETTY_FUNCTION__;
				f1 = ast_translate(p->fromast, &fr, 0);
				if (!f1) {
					ast_log(LOG_ERROR, "VOTER %i: Can not translate frame to receive from Asterisk\n", p->nodenum);
					continue;
				}
			}
		}
		mx = 0;
		if (p->mixminus) {
			for (client = clients; client; client = client->next) {
				if (client->nodenum != p->nodenum) {
					continue;
				}
				if (!client->heardfrom) {
					continue;
				}
				if (!client->respdigest) {
					continue;
				}
				if (!client->mix) {
					continue;
				}
				if (client->doadpcm) {
					continue;
				}
				if (client->donulaw) {
					continue;
				}
				if (client->lastrssi) {
					mx = 1;
				}
			}
		}
		// x will now be set if we are to generate TX output
		if (x || mx) {
			memset(&audiopacket, 0, sizeof(audiopacket) - sizeof(audiopacket.audio));
			memset(&audiopacket.audio, 0xff, sizeof(audiopacket.audio));
			strcpy((char *) audiopacket.vp.challenge, challenge);
			audiopacket.vp.payload_type = htons(1);
			audiopacket.rssi = 0;
			if (f1) {
				memcpy(audiopacket.audio, f1->data.ptr, FRAME_SIZE);
			}
#ifdef	DMWDIAG
			for (i = 0; i < FRAME_SIZE; i++) {
				audiopacket.audio[i] = ulaw_digital_milliwatt[mwp++];
				if (mwp > 7)
					mwp = 0;
			}
#endif
			audiopacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
			audiopacket.vp.curtime.vtime_nsec = htonl(master_time.vtime_nsec);
			for (client = clients; client; client = client->next) {
				if (client->nodenum != p->nodenum) {
					continue;
				}
				if (p->priconn && !client->mix) {
					continue;
				}
				if (!client->respdigest && !IS_CLIENT_PROXY(client)) {
					continue;
				}
				if (!client->heardfrom) {
					continue;
				}
				if (client->doadpcm) {
					continue;
				}
				if (client->donulaw) {
					continue;
				}
				if (p->mixminus) {
					memcpy(xmtbuf2, xmtbuf, sizeof(xmtbuf2));
					i = 0;
					for (client1 = clients; client1; client1 = client1->next) {
						if (client1 == client) {
							continue;
						}
						if (client1->nodenum != p->nodenum) {
							continue;
						}
						if (!client1->heardfrom) {
							continue;
						}
						if (!client1->respdigest) {
							continue;
						}
						if (!client1->mix) {
							continue;
						}
						if (client1->doadpcm) {
							continue;
						}
						if (client1->donulaw) {
							continue;
						}
						if (!client1->lastrssi) {
							continue;
						}
						for (i = 0; i < FRAME_SIZE; i++) {
							l = xmtbuf2[i] + client1->lastaudio[i];
							if (l > 32767) {
								l = 32767;
							}
							if (l < -32767) {
								l = -32767;
							}
							xmtbuf2[i] = l;
						}
					}
					if (!x && !i) {
						continue;
					}
					memset(&fr, 0, sizeof(fr));
					fr.frametype = AST_FRAME_VOICE;
					fr.subclass.format = ast_format_slin;
					fr.datalen = FRAME_SIZE;
					fr.samples = FRAME_SIZE;
					fr.data.ptr = xmtbuf2;
					fr.src = __PRETTY_FUNCTION__;
					if (f1) {
						ast_frfree(f1);
					}
					f1 = ast_translate(p->fromast, &fr, 0);
					if (!f1) {
						ast_log(LOG_ERROR, "VOTER %i: Can not translate frame to receive from Asterisk\n", p->nodenum);
						continue;
					}
					memcpy(audiopacket.audio, f1->data.ptr, FRAME_SIZE);
				}
				mkpucked(client, &audiopacket.vp.curtime);
				audiopacket.vp.digest = htonl(client->respdigest);
				audiopacket.vp.curtime.vtime_nsec = client->mix ? htonl(client->txseqno) : htonl(master_time.vtime_nsec);
				if (client->totransmit && !client->txlockout) {
					if (IS_CLIENT_PROXY(client)) {
						memset(&proxy_audiopacket, 0, sizeof(proxy_audiopacket));
						proxy_audiopacket.vp = audiopacket.vp;
						proxy_audiopacket.rssi = audiopacket.rssi;
						memcpy(proxy_audiopacket.audio, audiopacket.audio, sizeof(audiopacket.audio));
						proxy_audiopacket.vprox.ipaddr = client->proxy_sin.sin_addr.s_addr;
						proxy_audiopacket.vprox.port = client->proxy_sin.sin_port;
						proxy_audiopacket.vprox.payload_type = proxy_audiopacket.vp.payload_type;
						proxy_audiopacket.vp.payload_type = htons(VOTER_PAYLOAD_PROXY);
						proxy_audiopacket.vp.digest = htonl(crc32_bufs(client->saved_challenge, client->pswd));
						proxy_audiopacket.vp.curtime.vtime_nsec = client->mix ? htonl(client->txseqno) : htonl(master_time.vtime_nsec);
						ast_debug(6, "VOTER %i: Sending (proxied) TX audio packet to client %s digest %08x\n", p->nodenum,
							client->name, proxy_audiopacket.vp.digest);
						sendto(udp_socket, &proxy_audiopacket, sizeof(proxy_audiopacket) - 3, 0, (struct sockaddr *) &client->sin,
							sizeof(client->sin));
					} else {
						ast_debug(6, "VOTER %i: Sending TX audio packet to client %s digest %08x\n", p->nodenum, client->name,
							client->respdigest);
						sendto(udp_socket, &audiopacket, sizeof(audiopacket) - 3, 0, (struct sockaddr *) &client->sin,
							sizeof(client->sin));
					}
					gettimeofday(&client->lastsenttime, NULL);
				}
			}
		}
		if (x || p->adpcmf1) {
			if (p->adpcmf1 == NULL) {
				p->adpcmf1 = ast_frdup(f1);
			} else {
				memset(xmtbuf, 0xff, sizeof(xmtbuf));
				memset(&fr, 0, sizeof(fr));
				fr.frametype = AST_FRAME_VOICE;
				fr.subclass.format = ast_format_ulaw;
				fr.datalen = FRAME_SIZE;
				fr.samples = FRAME_SIZE;
				fr.data.ptr = xmtbuf;
				fr.src = __PRETTY_FUNCTION__;
				if (x) {
					f3 = ast_frcat(p->adpcmf1, f1);
				} else {
					f3 = ast_frcat(p->adpcmf1, &fr);
				}
				ast_frfree(p->adpcmf1);
				p->adpcmf1 = NULL;
				f2 = ast_translate(p->adpcmout, f3, 1);
				memcpy(audiopacket.audio, f2->data.ptr, f2->datalen);
				audiopacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
				audiopacket.vp.payload_type = htons(3);
				for (client = clients; client; client = client->next) {
					if (client->nodenum != p->nodenum) {
						continue;
					}
					if (p->priconn && !client->mix) {
						continue;
					}
					if (!client->respdigest && !IS_CLIENT_PROXY(client)) {
						continue;
					}
					if (!client->heardfrom) {
						continue;
					}
					if (!client->doadpcm) {
						continue;
					}
					mkpucked(client, &audiopacket.vp.curtime);
					audiopacket.vp.digest = htonl(client->respdigest);
					audiopacket.vp.curtime.vtime_nsec = client->mix ? htonl(client->txseqno) : htonl(master_time.vtime_nsec);
#ifndef ADPCM_LOOPBACK
					if (client->totransmit && !client->txlockout) {
						if (IS_CLIENT_PROXY(client)) {
							memset(&proxy_audiopacket, 0, sizeof(proxy_audiopacket));
							proxy_audiopacket.vp = audiopacket.vp;
							proxy_audiopacket.rssi = audiopacket.rssi;
							memcpy(proxy_audiopacket.audio, audiopacket.audio, sizeof(audiopacket.audio));
							proxy_audiopacket.vprox.ipaddr = client->proxy_sin.sin_addr.s_addr;
							proxy_audiopacket.vprox.port = client->proxy_sin.sin_port;
							proxy_audiopacket.vprox.payload_type = proxy_audiopacket.vp.payload_type;
							proxy_audiopacket.vp.payload_type = htons(VOTER_PAYLOAD_PROXY);
							proxy_audiopacket.vp.digest = htonl(crc32_bufs(client->saved_challenge, client->pswd));
							proxy_audiopacket.vp.curtime.vtime_nsec = client->mix ? htonl(client->txseqno) : htonl(master_time.vtime_nsec);
							ast_debug(6, "VOTER %i: Sending (proxied) TX audio packet to client %s digest %08x\n", p->nodenum,
								client->name, proxy_audiopacket.vp.digest);
							sendto(udp_socket, &proxy_audiopacket, sizeof(proxy_audiopacket), 0, (struct sockaddr *) &client->sin,
								sizeof(client->sin));
						} else {
							ast_debug(6, "VOTER %i: Sending TX audio packet to client %s digest %08x\n", p->nodenum, client->name,
								client->respdigest);
							sendto(udp_socket, &audiopacket, sizeof(audiopacket), 0, (struct sockaddr *) &client->sin,
								sizeof(client->sin));
						}
						gettimeofday(&client->lastsenttime, NULL);
					}
#endif
				}
				ast_frfree(f2);
			}
		}
		if (x || p->nulawf1) {
			short *sap, s;
			unsigned char nubuf[FRAME_SIZE];

			if (p->nulawf1 == NULL) {
				p->nulawf1 = ast_frdup(f1);
			} else {
				memset(xmtbuf, 0xff, sizeof(xmtbuf));
				memset(&fr, 0, sizeof(fr));
				fr.frametype = AST_FRAME_VOICE;
				fr.subclass.format = ast_format_ulaw;
				fr.datalen = FRAME_SIZE;
				fr.samples = FRAME_SIZE;
				fr.data.ptr = xmtbuf;
				fr.src = __PRETTY_FUNCTION__;
				if (x) {
					f3 = ast_frcat(p->nulawf1, f1);
				} else {
					f3 = ast_frcat(p->nulawf1, &fr);
				}
				ast_frfree(p->nulawf1);
				p->nulawf1 = NULL;
				f2 = ast_translate(p->nuout, f3, 1);
				sap = (short *) f2->data.ptr;
				for (i = 0; i < f2->samples / 2; i++) {
					s = *sap++;
					if (s > 14000) {
						s = 14000;
					} else if (s < -14000) {
						s = -14000;
					}
					lpass4(s, p->tlpx, p->tlpy);
					s = *sap++;
					if (s > 14000) {
						s = 14000;
					} else if (s < -14000) {
						s = -14000;
					}
					nubuf[i] = AST_LIN2MU(lpass4(s, p->tlpx, p->tlpy));
				}
				memcpy(audiopacket.audio, nubuf, sizeof(nubuf));
				audiopacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
				audiopacket.vp.payload_type = htons(4);
				for (client = clients; client; client = client->next) {
					if (client->nodenum != p->nodenum) {
						continue;
					}
					if (p->priconn && !client->mix) {
						continue;
					}
					if (!client->respdigest && !IS_CLIENT_PROXY(client)) {
						continue;
					}
					if (!client->heardfrom) {
						continue;
					}
					if (!client->donulaw) {
						continue;
					}
					mkpucked(client, &audiopacket.vp.curtime);
					audiopacket.vp.digest = htonl(client->respdigest);
					audiopacket.vp.curtime.vtime_nsec = client->mix ? htonl(client->txseqno) : htonl(master_time.vtime_nsec);
#ifndef NULAW_LOOPBACK
					if (client->totransmit && !client->txlockout) {
						if (IS_CLIENT_PROXY(client)) {
							memset(&proxy_audiopacket, 0, sizeof(proxy_audiopacket));
							proxy_audiopacket.vp = audiopacket.vp;
							proxy_audiopacket.rssi = audiopacket.rssi;
							memcpy(proxy_audiopacket.audio, audiopacket.audio, sizeof(audiopacket.audio));
							proxy_audiopacket.vprox.ipaddr = client->proxy_sin.sin_addr.s_addr;
							proxy_audiopacket.vprox.port = client->proxy_sin.sin_port;
							proxy_audiopacket.vprox.payload_type = proxy_audiopacket.vp.payload_type;
							proxy_audiopacket.vp.payload_type = htons(VOTER_PAYLOAD_PROXY);
							proxy_audiopacket.vp.digest = htonl(crc32_bufs(client->saved_challenge, client->pswd));
							proxy_audiopacket.vp.curtime.vtime_nsec = client->mix ? htonl(client->txseqno) : htonl(master_time.vtime_nsec);
							ast_debug(6, "VOTER %i: Sending (proxied) TX audio packet to client %s digest %08x\n", p->nodenum,
								client->name, proxy_audiopacket.vp.digest);
							sendto(udp_socket, &proxy_audiopacket, sizeof(proxy_audiopacket) - 3, 0,
								(struct sockaddr *) &client->sin, sizeof(client->sin));
						} else {
							ast_debug(6, "VOTER %i: Sending TX audio packet to client %s digest %08x\n", p->nodenum, client->name,
								client->respdigest);
							sendto(udp_socket, &audiopacket, sizeof(audiopacket) - 3, 0,
								(struct sockaddr *) &client->sin, sizeof(client->sin));
						}
						gettimeofday(&client->lastsenttime, NULL);
					}
#endif
				}
				ast_frfree(f2);
			}
		}
		if (f1) {
			ast_frfree(f1);
		}
		gettimeofday(&tv, NULL);
		for (client = clients; client; client = client->next) {
			if (client->nodenum != p->nodenum) {
				continue;
			}
			if (!client->respdigest) {
				continue;
			}
			if (!client->heardfrom) {
				continue;
			}
			if (IS_CLIENT_PROXY(client)) {
				continue;
			}
			check_ping_done(client);
			if (!client->pings_requested) {
				continue;
			}
			if (client->pings_sent >= client->pings_requested) {
				continue;
			}
			if (voter_tvdiff_ms(tv, client->ping_txtime) >= (PING_TIME_MS * client->pings_sent)) {
				if (!client->pings_sent) {
					client->ping_txtime = ast_tvnow();
					memset(&client->ping_last_rxtime, 0, sizeof(client->ping_last_rxtime));
				}
				client->pings_sent++;
				memset(&pingpacket, 0, sizeof(pingpacket));
				pingpacket.seqno = ++client->ping_seqno;
				for (i = 0; i < sizeof(pingpacket.filler); i++) {
					pingpacket.filler[i] = (pingpacket.seqno & 0xff) + i;
				}
				pingpacket.txtime = tv;
				pingpacket.starttime = client->ping_txtime;
				strcpy((char *) pingpacket.vp.challenge, challenge);
				pingpacket.vp.payload_type = htons(VOTER_PAYLOAD_PING);
				pingpacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
				pingpacket.vp.curtime.vtime_nsec = htonl(master_time.vtime_nsec);
				mkpucked(client, &pingpacket.vp.curtime);
				pingpacket.vp.digest = htonl(client->respdigest);
				pingpacket.vp.curtime.vtime_nsec = client->mix ? htonl(client->txseqno) : htonl(master_time.vtime_nsec);
				ast_debug(2, "VOTER %i: Sending ping packet to client %s digest %08x\n", p->nodenum, client->name, client->respdigest);
				sendto(udp_socket, &pingpacket, sizeof(pingpacket), 0, (struct sockaddr *) &client->sin, sizeof(client->sin));
			}
		}
		for (client = clients; client; client = client->next) {
			if (client->nodenum != p->nodenum) {
				continue;
			}
			if (!client->respdigest && !IS_CLIENT_PROXY(client)) {
				continue;
			}
			if (p->priconn && !client->mix && !IS_CLIENT_PROXY(client)) {
				continue;
			}
			if (!client->heardfrom) {
				continue;
			}
			if (ast_tvzero(client->lastsenttime) || (voter_tvdiff_ms(tv, client->lastsenttime) >= TX_KEEPALIVE_MS)) {
				memset(&audiopacket, 0, sizeof(audiopacket));
				strcpy((char *) audiopacket.vp.challenge, challenge);
				audiopacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
				audiopacket.vp.payload_type = htons(2);
				audiopacket.vp.digest = htonl(client->respdigest);
				audiopacket.vp.curtime.vtime_nsec = client->mix ? htonl(client->txseqno) : htonl(master_time.vtime_nsec);
				if (IS_CLIENT_PROXY(client)) {
					memset(&proxy_audiopacket, 0, sizeof(proxy_audiopacket));
					proxy_audiopacket.vp = audiopacket.vp;
					proxy_audiopacket.rssi = audiopacket.rssi;
					memcpy(proxy_audiopacket.audio, audiopacket.audio, sizeof(audiopacket.audio));
					proxy_audiopacket.vprox.ipaddr = client->proxy_sin.sin_addr.s_addr;
					proxy_audiopacket.vprox.port = client->proxy_sin.sin_port;
					proxy_audiopacket.vprox.payload_type = proxy_audiopacket.vp.payload_type;
					proxy_audiopacket.vp.payload_type = htons(VOTER_PAYLOAD_PROXY);
					proxy_audiopacket.vp.digest = htonl(crc32_bufs(client->saved_challenge, client->pswd));
					proxy_audiopacket.vp.curtime.vtime_nsec = client->mix ? htonl(client->txseqno) : htonl(master_time.vtime_nsec);
					ast_debug(5, "VOTER %i: Sending (proxied) GPS/Keepalive packet to client %s digest %08x\n", p->nodenum,
						client->name, proxy_audiopacket.vp.digest);
					sendto(udp_socket, &proxy_audiopacket, sizeof(VOTER_PACKET_HEADER) + sizeof(VOTER_PROXY_HEADER), 0,
						(struct sockaddr *) &client->sin, sizeof(client->sin));
				} else {
					ast_debug(5, "VOTER %i: Sending KEEPALIVE (GPS) packet to client %s digest %08x\n", p->nodenum, client->name,
						client->respdigest);
					sendto(udp_socket, &audiopacket, sizeof(VOTER_PACKET_HEADER), 0, (struct sockaddr *) &client->sin,
						sizeof(client->sin));
				}
				gettimeofday(&client->lastsenttime, NULL);
			}
		}
	}
	pthread_exit(NULL);
}

/*!
 * \brief Create and initialize a Voter channel instance for the requested node from Asterisk.
 *
 * Allocates and initializes per-node private state, translators, DSP, channel
 * formats, and loads node configuration from voter.conf. The function registers
 * the new channel with Asterisk, links the private state to the channel, and
 * starts per-node worker threads (transmit and optional primary/keepalive).
 *
 * \param type         Requested channel type string.
 * \param cap          Format capabilities negotiated for the channel.
 * \param assignedids  Unique IDs to assign to the new channel.
 * \param requestor    Channel that requested this new channel (may be NULL).
 * \param data         Node identifier string (interpreted as the node number).
 * \param cause        Pointer to integer to receive a failure cause code on error.
 * \return             Pointer to the newly allocated ast_channel on success, `NULL` on failure.
 */
static struct ast_channel *voter_request(const char *type, struct ast_format_cap *cap,
										 const struct ast_assigned_ids *assignedids,
										 const struct ast_channel *requestor, const char *data, int *cause)
{
	int i, j;
	struct voter_pvt *p, *p1;
	struct ast_channel *tmp = NULL;
	char *val, *cp, *cp1, *cp2, *strs[MAXTHRESHOLDS], *ctg;
	struct ast_config *cfg = NULL;

	if (!ast_format_cap_iscompatible(cap, voter_tech.capabilities)) {
		struct ast_str *cap_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		ast_log(LOG_NOTICE, "Channel requested with unsupported format(s): '%s'\n",
				ast_format_cap_get_names(cap, &cap_buf));
		return NULL;
	}
	p = ast_calloc(1, sizeof(struct voter_pvt));
	if (!p) {
		return NULL;
	}
	p->nodenum = strtoul((char *) data, NULL, 0);
	ast_mutex_init(&p->txqlock);
	ast_mutex_init(&p->pagerqlock);
	ast_mutex_init(&p->xmit_lock);
	ast_cond_init(&p->xmit_cond, NULL);
	p->dsp = ast_dsp_new();
	if (!p->dsp) {
		ast_log(LOG_ERROR, "VOTER %i: Cannot get DSP!!\n", p->nodenum);
		ast_free(p);
		return NULL;
	}
	ast_dsp_set_features(p->dsp, DSP_FEATURE_DIGIT_DETECT);
	ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
	p->usedtmf = 1;
	p->adpcmin = ast_translator_build_path(ast_format_ulaw, ast_format_adpcm);
	if (!p->adpcmin) {
		ast_log(LOG_ERROR, "VOTER %i: Cannot get translator from adpcm to ulaw!!\n", p->nodenum);
		ast_dsp_free(p->dsp);
		ast_free(p);
		return NULL;
	}
	p->adpcmout = ast_translator_build_path(ast_format_adpcm, ast_format_ulaw);
	if (!p->adpcmout) {
		ast_log(LOG_ERROR, "VOTER %i: Cannot get translator from ulaw to adpcm!!\n", p->nodenum);
		ast_dsp_free(p->dsp);
		ast_free(p);
		return NULL;
	}
	p->toast = ast_translator_build_path(ast_format_slin, ast_format_ulaw);
	if (!p->toast) {
		ast_log(LOG_ERROR, "VOTER %i: Cannot get translator from ulaw to slinear!!\n", p->nodenum);
		ast_dsp_free(p->dsp);
		ast_free(p);
		return NULL;
	}
	p->toast1 = ast_translator_build_path(ast_format_slin, ast_format_ulaw);
	if (!p->toast1) {
		ast_log(LOG_ERROR, "VOTER %i: Cannot get translator from ulaw to slinear!!\n", p->nodenum);
		ast_dsp_free(p->dsp);
		ast_free(p);
		return NULL;
	}
	p->fromast = ast_translator_build_path(ast_format_ulaw, ast_format_slin);
	if (!p->fromast) {
		ast_log(LOG_ERROR, "VOTER %i: Cannot get translator from slinear to ulaw!!\n", p->nodenum);
		ast_dsp_free(p->dsp);
		ast_free(p);
		return NULL;
	}
	p->nuin = ast_translator_build_path(ast_format_ulaw, ast_format_slin);
	if (!p->nuin) {
		ast_log(LOG_ERROR, "VOTER %i: Cannot get translator from slinear to ulaw!!\n", p->nodenum);
		ast_dsp_free(p->dsp);
		ast_free(p);
		return NULL;
	}
	p->nuout = ast_translator_build_path(ast_format_slin, ast_format_ulaw);
	if (!p->nuout) {
		ast_log(LOG_ERROR, "VOTER %i: Cannot get translator from ulaw to slinear!!\n", p->nodenum);
		ast_dsp_free(p->dsp);
		ast_free(p);
		return NULL;
	}
	tmp = ast_channel_alloc(1, AST_STATE_DOWN, 0, 0, "", (char *) data, context, assignedids, requestor, 0, "voter/%s",
						  (char *) data);
	if (!tmp) {
		ast_log(LOG_ERROR, "VOTER %i: Cannot alloc new Asterisk channel\n", p->nodenum);
		ast_free(p);
		return NULL;
	}
	ast_mutex_lock(&voter_lock);
	if (pvts != NULL) {
		p->next = pvts;
	}
	pvts = p;
	ast_mutex_unlock(&voter_lock);
	ast_channel_tech_set(tmp, &voter_tech);
	ast_channel_set_rawwriteformat(tmp, ast_format_slin);
	ast_channel_set_writeformat(tmp, ast_format_slin);
	ast_channel_set_rawreadformat(tmp, ast_format_slin);
	ast_channel_set_readformat(tmp, ast_format_slin);
	ast_channel_nativeformats_set(tmp, voter_tech.capabilities);
//  if (state == AST_STATE_RING) tmp->rings = 1;
	ast_channel_tech_pvt_set(tmp, p);
	ast_channel_unlock(tmp);
	ast_channel_language_set(tmp, "");
	p->owner = tmp;
	p->u = ast_module_user_add(tmp);
	/* Load the configuration for this node */
	if (!(cfg = ast_config_load(config, zeroflag))) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config);
	} else {
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "linger");
		if (val) {
			p->linger = atoi(val);
		} else {
			p->linger = DEFAULT_LINGER;
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "plfilter");
		if (val) {
			p->plfilter = ast_true(val);
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "hostdeemp");
		if (val) {
			p->hostdeemp = ast_true(val);
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "duplex");
		if (val) {
			p->duplex = ast_true(val);
		} else {
			p->duplex = 1;
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "mixminus");
		if (val) {
			p->mixminus = ast_true(val);
		} else {
			p->mixminus = 0;
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "streams");
		if (val) {
			cp = ast_strdup(val);
			p->nstreams = finddelim(cp, p->streams, ARRAY_LEN(p->streams));
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "txctcss");
		if (val) {
			ast_copy_string(p->txctcssfreq, val, sizeof(p->txctcssfreq));
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "txctcsslevel");
		if (val) {
			p->txctcsslevel = atoi(val);
		} else {
			p->txctcsslevel = 62;
		}
		p->txctcsslevelset = p->txctcsslevel;
		p->txtoctype = TOC_NONE;
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "txtoctype");
		if (val) {
			if (!strcasecmp(val, "phase")) {
				p->txtoctype = TOC_PHASE;
			} else if (!strcasecmp(val, "notone")) {
				p->txtoctype = TOC_NOTONE;
			}
		}
		memset(&p->primary, 0, sizeof(p->primary));
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "primary");
		if (val) {
			cp = ast_strdup(val);
			if (!cp) {
				return NULL;
			}
			j = finddelim(cp, strs, ARRAY_LEN(strs));
			if (j < 2) {
				ast_log(LOG_ERROR, "Channel %s: primary not specified properly\n", ast_channel_name(tmp));
			} else {
				cp1 = strchr(strs[0], ':');
				if (cp1) {
					*cp1 = 0;
					j = atoi(cp1 + 1);
				} else {
					j = listen_port;
				}
				p->primary.sin_family = AF_INET;
				p->primary.sin_addr.s_addr = inet_addr(strs[0]);
				p->primary.sin_port = htons(j);
				ast_copy_string(p->primary_pswd, strs[1], sizeof(p->primary_pswd) - 1);
			}
			ast_free(cp);
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "isprimary");
		if (val) {
			p->isprimary = ast_true(val);
		} else {
			p->isprimary = 0;
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "thresholds");
		if (val) {
			cp = ast_strdup(val);
			p->nthresholds = finddelim(cp, strs, MIN(ARRAY_LEN(strs), ARRAY_LEN(p->linger_thresh)));
			for (i = 0; i < p->nthresholds; i++) {
				cp1 = strchr(strs[i], '=');
				p->linger_thresh[i] = p->linger;
				if (cp1) {
					*cp1 = 0;
					cp2 = strchr(cp1 + 1, ':');
					if (cp2) {
						*cp2 = 0;
						if (cp2[1]) {
							p->linger_thresh[i] = (uint16_t) atoi(cp2 + 1);
						}
					}
					if (cp1[1]){
						p->count_thresh[i] = (uint16_t) atoi(cp1 + 1);
					}
				}
				p->rssi_thresh[i] = (uint8_t) atoi(strs[i]);
			}
			ast_free(cp);
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "gtxgain");
		if (!val) {
			val = DEFAULT_GTXGAIN;
		}
		p->gtxgain = pow(10.0, atof(val) / 20.0);
	}

	if (p->txctcssfreq[0]) {
		t_pmr_chan tChan;

		memset(&tChan, 0, sizeof(t_pmr_chan));

		tChan.pTxCodeDefault = p->txctcssfreq;
		tChan.pTxCodeSrc = p->txctcssfreq;
		tChan.pRxCodeSrc = p->txctcssfreq;
		tChan.txMod = 2;
		tChan.txMixA = TX_OUT_COMPOSITE;
		tChan.b.txboost = 1;
		if (p->pmrChan) {
			destroyPmrChannel(p->pmrChan);
		}
		p->pmrChan = createPmrChannel(&tChan, FRAME_SIZE);
		p->pmrChan->radioDuplex = 1;	//o->radioduplex;
		p->pmrChan->b.loopback = 0;
		p->pmrChan->b.radioactive = 1;
		p->pmrChan->txrxblankingtime = 0;
		p->pmrChan->rxCpuSaver = 0;
		p->pmrChan->txCpuSaver = 0;
		*(p->pmrChan->prxSquelchAdjust) = 0;
		*(p->pmrChan->prxVoiceAdjust) = 0;
		*(p->pmrChan->prxCtcssAdjust) = 0;
		p->pmrChan->rxCtcss->relax = 0;
		p->pmrChan->txTocType = p->txtoctype;
		p->pmrChan->spsTxOutA->outputGain = 250;
		*p->pmrChan->ptxCtcssAdjust = p->txctcsslevel;
		p->pmrChan->pTxCodeDefault = p->txctcssfreq;
		p->pmrChan->pTxCodeSrc = p->txctcssfreq;

	}
	i = 0;
	ctg = NULL;
	while ((ctg = ast_category_browse(cfg, ctg)) != NULL) {
		if (!ctg[0]) {
			continue;
		}
		if (!isdigit(ctg[0])) {
			continue;
		}
		ast_mutex_lock(&voter_lock);
		for (p1 = pvts; p1; p1 = p1->next) {
			if (p1->nodenum == atoi(ctg)) {
				break;
			}
		}
		if (p1) {
			p1->order = ++i;
			if (i > maxpvtorder) {
				maxpvtorder = i;
			}
		}
		ast_mutex_unlock(&voter_lock);
	}
	ast_pthread_create(&p->xmit_thread, NULL, voter_xmit, p);
	if (SEND_PRIMARY(p)) {
		ast_pthread_create(&p->primary_thread, NULL, voter_primary_client, p);
	}
	return tmp;
}

/*!
 * \brief Process Asterisk CLI request to enable/disable client test mode.
 * \param fd			Asterisk CLI fd
 * \param argc			Number of arguments
 * \param argv			Arguments
 * \return	CLI success, showusage, or failure.
 */
static int voter_do_test(int fd, int argc, const char *const *argv)
{
	int newlevel;
	struct voter_pvt *p;

	if (argc < 3) {
		return RESULT_SHOWUSAGE;
	}
	ast_mutex_lock(&voter_lock);
	for (p = pvts; p; p = p->next) {
		if (p->nodenum == atoi(argv[2])) {
			break;
		}
	}
	if (!p) {
		ast_cli(fd, "Voter instance %s not found\n", argv[2]);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (argc == 3) {
		if (p->voter_test) {
			ast_cli(fd, "Voter instance %d Test: currently set to %d\n", p->nodenum, p->voter_test);
		} else {
			ast_cli(fd, "Voter instance %d Test: currently disabled\n", p->nodenum);
		}
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (argc != 4) {
		ast_mutex_unlock(&voter_lock);
		return RESULT_SHOWUSAGE;
	}
	newlevel = atoi(argv[3]);
	if (newlevel < 0) {
		ast_cli(fd, "Error: Invalid test mode value specification!!\n");
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (newlevel) {
		ast_cli(fd, "Voter instance %d Test: previous level: %d, new level: %d\n", p->nodenum, p->voter_test, newlevel);
	} else {
		ast_cli(fd, "Voter instance %d Test disabled\n", p->nodenum);
	}

	p->voter_test = newlevel;
	ast_mutex_unlock(&voter_lock);
	return RESULT_SUCCESS;
}

/*!
 * \brief Update or display per-client priority settings for a Voter node from the Asterisk CLI.
 *
 * When invoked with just a node number, prints all clients and their effective/override
 * priority values for that node. When given a node number and a client name (or "all"),
 * prints that client's priority information. When additionally supplied with a priority
 * value (or "off"/"disable"), sets the client's priority override to that value (or
 * disables the override).
 *
 * \param fd Asterisk CLI file descriptor used for output.
 * \param argc Number of CLI arguments.
 * \param argv CLI arguments; expected forms:
 *             - argv[2]: node number
 *             - argv[3] (optional): client name or "all"
 *             - argv[4] (optional): priority value or "off"/"disable"
 * \return     RESULT_SHOWUSAGE when the argument count or format is incorrect,
 *             RESULT_SUCCESS otherwise.
 */
static int voter_do_prio(int fd, int argc, const char *const *argv)
{
	int newlevel, foundit;
	struct voter_pvt *p;
	struct voter_client *client;

	if (argc < 3) {
		return RESULT_SHOWUSAGE;
	}
	ast_mutex_lock(&voter_lock);
	for (p = pvts; p; p = p->next) {
		if (p->nodenum == atoi(argv[2])) {
			break;
		}
	}
	if (!p) {
		ast_cli(fd, "Voter instance %s not found\n", argv[2]);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (argc == 3) {
		ast_cli(fd, "Voter instance %d priority values:\n\n", p->nodenum);
		for (client = clients; client; client = client->next) {
			if (client->nodenum != p->nodenum) {
				continue;
			}
			if (client->prio_override > -2) {
				ast_cli(fd, "Client %s: eff_prio: %d, prio: %d, override_prio: %d\n",
						client->name, client->prio_override, client->prio, client->prio_override);
			} else {
				ast_cli(fd, "Client %s: prio: %d (not overridden)\n", client->name, client->prio);
			}
		}
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (argc == 4) {
		foundit = 0;
		for (client = clients; client; client = client->next) {
			if (client->nodenum != p->nodenum) {
				continue;
			}
			if (strcasecmp(argv[3], "all") && strcasecmp(argv[3], client->name)) {
				continue;
			}
			foundit = 1;
			if (client->prio_override > -2) {
				ast_cli(fd, "Voter instance %d, client %s: eff_prio: %d, prio: %d, override_prio: %d\n",
						p->nodenum, client->name, client->prio_override, client->prio, client->prio_override);
			} else {
				ast_cli(fd, "Voter instance %d, client %s: prio: %d (not overridden)\n",
						p->nodenum, client->name, client->prio);
			}
		}
		if (!foundit) {
			ast_cli(fd, "Voter client %s not found\n", argv[3]);
		}
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (argc != 5) {
		ast_mutex_unlock(&voter_lock);
		return RESULT_SHOWUSAGE;
	}
	foundit = 0;
	for (client = clients; client; client = client->next) {
		if (client->nodenum != p->nodenum) {
			continue;
		}
		if (strcasecmp(argv[3], "all") && strcasecmp(argv[3], client->name)) {
			continue;
		}
		if (!strcasecmp(argv[4], "off") || !strncasecmp(argv[4], "dis", 3)) {
			newlevel = -2;
		} else {
			if (sscanf(argv[4], N_FMT(d), &newlevel) < 1) {
				ast_cli(fd, "Error: Invalid priority value specification!!\n");
				ast_mutex_unlock(&voter_lock);
				return RESULT_SUCCESS;
			}
		}
		if (newlevel < -2) {
			ast_cli(fd, "Error: Invalid priority value specification!!\n");
			ast_mutex_unlock(&voter_lock);
			return RESULT_SUCCESS;
		}
		if (newlevel > -2) {
			if (client->prio_override > -2) {
				ast_cli(fd, "Voter instance %d client %s prio (override): previous level: %d, new level: %d\n",
						p->nodenum, client->name, client->prio_override, newlevel);
			} else {
				ast_cli(fd, "Voter instance %d client %s prio (override): previous level: <disabled>, new level: %d\n",
						p->nodenum, client->name, newlevel);
			}
		} else {
			ast_cli(fd, "Voter instance %d client %s prio (override) disabled\n", p->nodenum, client->name);
		}

		client->prio_override = newlevel;
		foundit = 1;
	}
	ast_mutex_unlock(&voter_lock);
	if (!foundit) {
		ast_cli(fd, "Voter client %s not found\n", argv[3]);
	}
	return RESULT_SUCCESS;
}

/*!
 * \brief Process Asterisk CLI request to enable/disable recording.
 * \param fd			Asterisk CLI fd
 * \param argc			Number of arguments
 * \param argv			Arguments
 * \return	CLI success, showusage, or failure.
 */
static int voter_do_record(int fd, int argc, const char *const *argv)
{
	struct voter_pvt *p;

	if (argc < 3) {
		return RESULT_SHOWUSAGE;
	}
	ast_mutex_lock(&voter_lock);
	for (p = pvts; p; p = p->next) {
		if (p->nodenum == atoi(argv[2])) {
			break;
		}
	}
	if (!p) {
		ast_cli(fd, "Voter instance %s not found\n", argv[2]);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (argc == 3) {
		if (p->recfp) {
			fclose(p->recfp);
		}
		p->recfp = NULL;
		ast_cli(fd, "Voter instance %s recording disabled\n", argv[2]);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (argc != 4) {
		ast_mutex_unlock(&voter_lock);
		return RESULT_SHOWUSAGE;
	}
	p->recfp = fopen(argv[3], "w");
	if (!p->recfp) {
		ast_cli(fd, "Voter instance %s Record: Could not open file %s\n", argv[2], argv[3]);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	ast_cli(fd, "Voter instance %s Record: Recording enabled info file %s\n", argv[2], argv[3]);
	ast_mutex_unlock(&voter_lock);
	return RESULT_SUCCESS;
}

/*!
 * \brief Process Asterisk CLI request to display or set the ctcss level.
 * \param fd			Asterisk CLI fd
 * \param argc			Number of arguments
 * \param argv			Arguments
 * \return	CLI success, showusage, or failure.
 */
static int voter_do_tone(int fd, int argc, const char *const *argv)
{
	int newlevel;
	struct voter_pvt *p;

	if (argc < 3) {
		return RESULT_SHOWUSAGE;
	}
	ast_mutex_lock(&voter_lock);
	for (p = pvts; p; p = p->next) {
		if (p->nodenum == atoi(argv[2])) {
			break;
		}
	}
	if (!p) {
		ast_cli(fd, "Voter instance %s not found\n", argv[2]);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (!p->pmrChan) {
		ast_cli(fd, "Voter instance %s does not have CTCSS enabled\n", argv[2]);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (argc == 3) {
		ast_cli(fd, "Voter instance %d CTCSS tone level is %d\n", p->nodenum, p->txctcsslevel);
		ast_mutex_unlock(&voter_lock);
		return RESULT_SUCCESS;
	}
	if (!strcasecmp(argv[3], "default")) {
		newlevel = p->txctcsslevelset;
	} else {
		newlevel = atoi(argv[3]);
		if ((newlevel < 0) || (newlevel > 250)) {
			ast_mutex_unlock(&voter_lock);
			return RESULT_SHOWUSAGE;
		}
	}
	ast_cli(fd, "Voter instance %d CTCSS tone level set to %d\n", p->nodenum, newlevel);
	p->txctcsslevel = newlevel;
	*p->pmrChan->ptxCtcssAdjust = newlevel;
	ast_mutex_unlock(&voter_lock);
	return RESULT_SUCCESS;
}

/*!
 * \brief Poll the specified fd for input for the specified milliseconds.
 * \param fd			File descriptor.
 * \param ms			Milliseconds to wait.
 * \return  -1, 1, 0    Needs to be defined.
 */
static int rad_rxwait(int fd, int ms)
{
	int myms = ms, x;

	x = ast_waitfor_n_fd(&fd, 1, &myms, NULL);
	if (x == -1) {
		return -1;
	}
	if (x == fd) {
		return 1;
	}
	return 0;
}

/*! \brief I think it's reasonable to assume everyone has a VT100 compatible console these days...
 * This must be enabled if the screen should clear automatically for remote consoles too
 * (and you probably want that to work!) */
#define ASSUME_CONSOLES_SUPPORT_CLEAR

#ifndef ASSUME_CONSOLES_SUPPORT_CLEAR
/*!
 * \brief Determine if the console supports clear.
 * \note This only works if Asterisk is running in foreground console (-c option when starting).
 * This is because this function is checking the TERM of the process, and if Asterisk forked and
 * is daemonized, then TERM is not the TERM type of the connecting remote console user.
 * Yet another reason that trying to check the TERM type is a big no-no and shouldn't be done. 
 * \retval 0	Does not support clear.
 * \retval 1	Supports clear.
 */

static int term_supports_clear(void)
{
	char *term;

	term = getenv("TERM");
	/* XXX We should probably query ncurses/termcap DB to be more complete, instead of checking just common TERM types */

	ast_debug(2, "Terminal type: %s\n", S_OR(term, ""));

	if (term) {
		if (!strcmp(term, "linux")) {
			return 1;
		} else if (!strcmp(term, "xterm")) {
			return 1;
		} else if (!strcmp(term, "xterm-color")) {
			return 1;
		} else if (!strcmp(term, "xterm-256color")) {
			return 1;
		} else if (!strncmp(term, "Eterm", 5)) {
			/* Both entries which start with Eterm support color */
			return 1;
		} else if (!strcmp(term, "vt100")) {
			return 1;
		} else if (!strncmp(term, "crt", 3)) {
			/* Both crt terminals support color */
			return 1;
		}
	}
	return 0;
}
#endif

/*!
 * \brief Display a live, updating CLI view of the specified voter instance.
 *
 * Presents per-client RSSI bars, marks the current winner, lists active clients
 * with their source addresses, and shows a warning when the master timing
 * source is unavailable. The display updates periodically and remains active
 * until the user cancels the console view.
 *
 * \param fd  Asterisk CLI file descriptor used to write the display.
 * \param p   Pointer to the voter_pvt instance to display.
 *
 * \note This function temporarily suppresses other verbose console output by
 *       modifying global verbose state and restores it before returning.
 */
static void voter_display(int fd, const struct voter_pvt *p)
{
	int j, rssi, thresh, ncols = 56, wasverbose, vt100compat;
	char str[256], c;
	struct voter_client *client;

#ifdef ASSUME_CONSOLES_SUPPORT_CLEAR
	vt100compat = 1;
#else
	vt100compat = term_supports_clear();
#endif

	for (j = 0; j < ncols; j++) {
		str[j] = ' ';
	}
	str[j] = 0;
	ast_cli(fd, " %s \r", str);

	/* Temporarily disable other verbose messages as long as we're running.
	 * XXX This should probably be done for just *THIS* console, not all of them. */
	wasverbose = option_verbose;
	option_verbose = 0;

	for (;;) {
		/* Repeat every 100ms */
		if (rad_rxwait(fd, 100)) {
			break;
		}
		if (vt100compat) {
			ast_cli(fd, "\033[2J\033[H"); /* Clear the screen */
		}
		ast_cli(fd, "Voter Instance %d Display:\n\n", p->nodenum);
		if (hasmaster && (!master_time.vtime_sec)) {
			ast_cli(fd, "*** WARNING -- LOSS OF MASTER TIMING SOURCE ***\n\n");
		}
		for (client = clients; client; client = client->next) {
			if (client->nodenum != p->nodenum) {
				continue;
			}
			if (p->priconn && !client->mix) {
				continue;
			}
			if (!client->respdigest && !IS_CLIENT_PROXY(client)) {
				continue;
			}
			if (!client->heardfrom) {
				continue;
			}
			rssi = client->lastrssi;
			thresh = (rssi * ncols) / 256;
			for (j = 0; j < ncols; j++) {
				if (client->prio_override == -1) {
					str[j] = 'X';
				} else if (j < thresh) {
					str[j] = '=';
				} else if (j == thresh) {
					str[j] = '>';
				} else {
					str[j] = ' ';
				}
			}
			str[j] = 0;
			c = ' ';
			if (p->winner == client) {
				c = '*';
			}
			ast_cli(fd, "%c%10.10s |%s| [%3d]\n", c, client->name, str, rssi);
		}
		ast_cli(fd, "\n\n");
		ast_cli(fd, "Active Clients:\n\n");
		for (client = clients; client; client = client->next) {
			if (client->nodenum != p->nodenum) {
				continue;
			}
			if (p->priconn && !client->mix) {
				continue;
			}
			if (!client->respdigest && !IS_CLIENT_PROXY(client)) {
				continue;
			}
			if (!client->heardfrom) {
				continue;
			}
			ast_cli(fd, "%10.10s -- %s:%d\n", client->name, ast_inet_ntoa(client->sin.sin_addr), ntohs(client->sin.sin_port));
		}
		ast_cli(fd, "\n\n");
	}
	option_verbose = wasverbose;
}

/*!
 * \brief Handle the CLI "voter display" request and print the voter instance information for a node.
 *
 * \param fd   Asterisk CLI file descriptor.
 * \param argc Number of arguments in argv.
 * \param argv Argument array; argv[2] is expected to contain the node number to display.
 * \return     RESULT_SUCCESS if the node was found or when a not-found message was printed,
 *             RESULT_SHOWUSAGE if insufficient arguments were provided.
 */
static int voter_do_display(int fd, int argc, const char *const *argv)
{
	struct voter_pvt *p;

	if (argc < 3) {
		return RESULT_SHOWUSAGE;
	}
	for (p = pvts; p; p = p->next) {
		if (p->nodenum == atoi(argv[2])) {
			break;
		}
	}
	if (!p) {
		ast_cli(fd, "Voter instance %s not found\n", argv[2]);
		return RESULT_SUCCESS;
	}
	voter_display(fd, p);
	return RESULT_SUCCESS;
}

/*!
 * \brief Handle the CLI "txlockout" command to set or display per-client transmit lockout
 * state for a specified voter instance.
 *
 * The command accepts a node number and an optional lockout specification:
 * - "all" to enable tx lockout for every client on the node,
 * - "none" to clear lockout for every client on the node,
 * - or a comma-separated list of client names, optionally prefixed with '-' to
 *   disable or '+' (or no sign) to enable lockout for each named client.
 *
 * \param fd   Asterisk CLI file descriptor to write output to.
 * \param argc Number of command arguments.
 * \param argv Argument vector; argv[2] is the node number and argv[3] (optional)
 *             is the lockout specification as described above.
 * \return     `RESULT_SHOWUSAGE` if arguments are insufficient, `RESULT_SUCCESS`
 *             on successful processing, or `RESULT_FAILURE` on error.
 */
static int voter_do_txlockout(int fd, int argc, const char *const *argv)
{
	int i, n, newval;
	char str[300], *strs[100];
	struct voter_pvt *p;
	struct voter_client *client;

	if (argc < 3) {
		return RESULT_SHOWUSAGE;
	}
	for (p = pvts; p; p = p->next) {
		if (p->nodenum == atoi(argv[2])) {
			break;
		}
	}
	if (!p) {
		ast_cli(fd, "Voter instance %s not found\n", argv[2]);
		return RESULT_SUCCESS;
	}
	if (argc > 3) {				/* specify list of lockouts */
		if (!strcasecmp(argv[3], "all")) {
			for (client = clients; client; client = client->next) {
				if (client->nodenum != p->nodenum) {
					continue;
				}
				client->txlockout = 1;
			}
		} else if (!strcasecmp(argv[3], "none")) {
			for (client = clients; client; client = client->next) {
				if (client->nodenum != p->nodenum) {
					continue;
				}
				client->txlockout = 0;
			}
		} else {				/* must be a comma-delimited list */
			ast_copy_string(str, argv[3], sizeof(str) - 1);
			n = finddelim((char *) argv[3], strs, ARRAY_LEN(strs));
			for (i = 0; i < n; i++) {
				if (!*strs[i]) {
					continue;
				}
				newval = 1;
				if (*strs[i] == '-') {
					strs[i]++;
					newval = 0;
				} else if (*strs[i] == '+') {
					strs[i]++;
				}
				for (client = clients; client; client = client->next) {
					if (client->nodenum != p->nodenum) {
						continue;
					}
					if (strcasecmp(strs[i], client->name)) {
						continue;
					}
					ast_cli(fd, "Client %s tx lockout %s\n", strs[i], (newval) ? "Enabled" : "Disabled");
					client->txlockout = newval;
					break;
				}
				if (!client) {
					ast_cli(fd, "Client %s not found!!\n", strs[i]);
				}
			}
		}
	}
	ast_cli(fd, "\nFull list of Tx Locked-out clients for voter instance %s:\n", argv[2]);
	for (n = 0, client = clients; client; client = client->next) {
		if (client->nodenum != p->nodenum) {
			continue;
		}
		if (client->txlockout) {
			ast_cli(fd, "Client %s tx is locked-out\n", client->name);
			n++;
		}
	}
	if (!n) {
		ast_cli(fd, "No clients are currently locked-out\n");
	}
	ast_cli(fd, "\nFull list of normally transmitting clients for voter instance %s:\n", argv[2]);
	for (n = 0, client = clients; client; client = client->next) {
		if (client->nodenum != p->nodenum) {
			continue;
		}
		if (!client->txlockout) {
			ast_cli(fd, "Client %s is able to transmit\n", client->name);
			n++;
		}
	}
	if (!n) {
		ast_cli(fd, "No clients are currently able to transmit\n");
	}
	ast_cli(fd, "\n");
	return RESULT_SUCCESS;
}

/*!
 * \brief Start or stop a ping sequence for a named Voter client from the Asterisk CLI.
 *
 * \param fd   Asterisk CLI file descriptor used for command output.
 * \param argc Number of CLI arguments.
 * \param argv CLI argument vector; argv[2] is the client name, argv[3] (optional) is ping count.
 * \return     RESULT_SUCCESS on normal handling, RESULT_SHOWUSAGE if arguments are insufficient, or RESULT_FAILURE on error.
 */
static int voter_do_ping(int fd, int argc, const char *const *argv)
{
	struct voter_client *client;
	int npings = 8;

	if (argc < 3) {
		return RESULT_SHOWUSAGE;
	}

	for (client = clients; client; client = client->next) {
		if (IS_CLIENT_PROXY(client)) {
			continue;
		}
		if (!client->heardfrom) {
			continue;
		}
		if (!client->respdigest) {
			continue;
		}
		if (!strcasecmp(client->name, argv[2])) {
			break;
		}
	}
	if (!client) {
		ast_cli(fd, "Voter client %s not found or not connected\n", argv[2]);
		return RESULT_SUCCESS;
	}
	if (argc > 3) {
		npings = atoi(argv[3]);
	}
	if (npings <= 0) {
		client->ping_abort = 1;
		return RESULT_SUCCESS;
	} else if ((client->pings_requested) && (client->pings_sent < client->pings_requested)) {
		ast_cli(fd, "Voter client %s already pinging!!\n", argv[2]);
		return RESULT_SUCCESS;
	}
	client->pings_sent = 0;
	client->pings_received = 0;
	client->pings_oos = 0;
	client->pings_total_ms = 0;
	client->pings_best = 0;
	client->pings_worst = 0;
	client->ping_last_seqno = 0;
	client->ping_seqno = 0;
	client->ping_abort = 0;
	client->pings_requested = npings;
	return RESULT_SUCCESS;
}
static char *voter_complete_static_client_list(const char *line, const char *word, int pos, int rpos)
{
	struct voter_client *client;
	size_t wordlen = strlen(word);

	if (pos != rpos) {
		return NULL;
	}
	ast_mutex_lock(&voter_lock);
	for (client = clients; client; client = client->next) {
		if (!strncmp(client->name, word, wordlen)) {
			ast_cli_completion_add(ast_strdup(client->name));
		}
	}
	ast_mutex_unlock(&voter_lock);
	return NULL;
}

/*!
 * \brief Populate CLI completions with names of currently connected, authenticated clients
 *
 * Scans the global client list and, for each non-proxy client that has been heard
 * from and has a valid response digest, adds the client's name as a completion
 * if it starts with the provided word prefix and the cursor is at the end of the line.
 *
 * \param line Full input line from the CLI.
 * \param word Current word to complete (prefix to match).
 * \param pos  Cursor position in the line.
 * \param rpos Cursor position relative to the completion engine (should equal pos to act).
 * \return     NULL
 */
static char *voter_complete_connected_client_list(const char *line, const char *word, int pos, int rpos)
{
	struct voter_client *client;
	size_t wordlen = strlen(word);

	if (pos != rpos) {
		return NULL;
	}
	ast_mutex_lock(&voter_lock);
	for (client = clients; client; client = client->next) {
		if (IS_CLIENT_PROXY(client)) {
			continue;
		}
		if (!client->heardfrom) {
			continue;
		}
		if (!client->respdigest) {
			continue;
		}
		if (!strncmp(client->name, word, wordlen)) {
			ast_cli_completion_add(ast_strdup(client->name));
		}
	}
	ast_mutex_unlock(&voter_lock);
	return NULL;
}

static char *voter_complete_node_list(const char *line, const char *word, int pos, int rpos)
{
	struct voter_pvt *p;
	char node[20];

	size_t wordlen = strlen(word);

	if (pos != rpos) {
		return NULL;
	}
	ast_mutex_lock(&voter_lock);
	for (p = pvts; p; p = p->next) {
		snprintf(node, sizeof(node), "%d", p->nodenum);
		if (!strncmp(node, word, wordlen)) {
			ast_cli_completion_add(ast_strdup(node));
		}
	}
	ast_mutex_unlock(&voter_lock);
	return NULL;
}

/*!
 * \brief Turns integer response to char CLI response
 * \param r				Response.
 * \return	CLI success, showusage, or failure.
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
 * \brief Handle Asterisk CLI request to report/set client test mode.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_cli_test(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "voter test";
		e->usage = "Usage: voter test instance_id [test value]\n" 
				   "       Specifies/Queries test mode for voter instance\n";
		return NULL;
	case CLI_GENERATE:
		return voter_complete_node_list(a->line, a->word, a->pos, 2);
	}
	return res2cli(voter_do_test(a->fd, a->argc, a->argv));
}

/*!
 * \brief Handle Asterisk CLI request to set client priority.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_cli_prio(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "voter prio";
		e->usage = 	"Usage: voter prio instance_id [client_id] [priority value]\n"
					"       Specifies/Queries priority value for voter client\n";

		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return voter_complete_node_list(a->line, a->word, a->pos, 2);
		} else if (a->pos == 3) {
			return voter_complete_static_client_list(a->line, a->word, a->pos, 3);
		}
		return NULL;
	}
	return res2cli(voter_do_prio(a->fd, a->argc, a->argv));
}

/*!
 * \brief Handle Asterisk CLI request to enable/disable recording.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_cli_record(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "voter record";
		e->usage = 	"Usage: voter record instance_id [record filename]\n"
					"       Enables/Specifies (or disables) recording file for chan_voter\n";

		return NULL;
	case CLI_GENERATE:
		return voter_complete_node_list(a->line, a->word, a->pos, 2);
	}
	return res2cli(voter_do_record(a->fd, a->argc, a->argv));
}

/*!
 * \brief Handle Asterisk CLI request report or set ctcss level.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_cli_tone(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "voter tone";
		e->usage = 	"Usage: voter tone instance_id [new_tone_level(0-250)]\n"
					"       Sets/Queries Tx CTCSS level for specified chan_voter instance\n";

		return NULL;
	case CLI_GENERATE:
		return voter_complete_node_list(a->line, a->word, a->pos, 2);
	}
	return res2cli(voter_do_tone(a->fd, a->argc, a->argv));
}

/*!
 * \brief Handle Asterisk CLI request to display voter information.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_cli_display(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "voter display";
		e->usage = "Usage: voter display [instance]\n" 
				   "       Display voter instance clients\n";
		return NULL;
	case CLI_GENERATE:
		return voter_complete_node_list(a->line, a->word, a->pos, 2);
	}
	return res2cli(voter_do_display(a->fd, a->argc, a->argv));
}

/*!
 * \brief Handle Asterisk CLI request for tx lockout.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_cli_txlockout(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "voter txlockout";
		e->usage = 	"Usage: voter txlockout [instance] <client_list>\n" 
					"       Set Tx Lockout for voter instance clients\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return voter_complete_node_list(a->line, a->word, a->pos, 2);
		} else if (a->pos == 3) {
			return voter_complete_static_client_list(a->line, a->word, a->pos, 3);
		}
		return NULL;
	}
	return res2cli(voter_do_txlockout(a->fd, a->argc, a->argv));
}

/*!
 * \brief Handle Asterisk CLI request to ping client.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_cli_ping(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "voter ping";
		e->usage = 	"Usage: voter ping [client] <# pings, 0 to abort>\n" 
					"       Ping (check connectivity) to client\n";
		return NULL;
	case CLI_GENERATE:
		return voter_complete_connected_client_list(a->line, a->word, a->pos, 2);
	}
	return res2cli(voter_do_ping(a->fd, a->argc, a->argv));
}

static struct ast_cli_entry voter_cli[] = {
	AST_CLI_DEFINE(handle_cli_test, "Specify/Query voter instance test mode"),
	AST_CLI_DEFINE(handle_cli_prio, "Specify/Query voter client priority value"),
	AST_CLI_DEFINE(handle_cli_record, "Enable/Specify (or disable) voter recording file"),
	AST_CLI_DEFINE(handle_cli_tone, "Sets/Queries Tx CTCSS level for specified chan_voter instance"),
	AST_CLI_DEFINE(handle_cli_display, "Displays voter (instance) clients"),
	AST_CLI_DEFINE(handle_cli_txlockout, "Set Tx Lockout for voter (instance) clients"),
	AST_CLI_DEFINE(handle_cli_ping, "Client ping"),
};

/*!
 * \brief Append Success and ActionID to asterisk manager response.
 * \param s					Pointer to mansession struct.
 * \param m					Pointer to message struct.
 */
static void rpt_manager_success(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");

	astman_append(s, "Response: Success\r\n");

	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}
}

/*!
 * \brief Send per-node and per-client voter status lines to the given Asterisk manager session.
 *
 * When a "Node" header is present in the manager message, only status for the listed node
 * numbers is included. For each reported node this emits node identifier, last voted client (if any),
 * and for each client that has been heard from emits client name, flags (Mix, Master, ActiveMaster),
 * IP/port information (proxied clients are marked), and last RSSI.
 *
 * \param ses Pointer to the manager session to which status lines will be written.
 * \param m   Pointer to the manager message; the optional "Node" header is used to filter output.
 * \return    RESULT_SUCCESS to indicate the manager response was sent.
 */
static int manager_voter_status(struct mansession *ses, const struct message *m)
{
	int i, j, n;
	struct voter_pvt *p;
	struct voter_client *client;
	const char *node = astman_get_header(m, "Node");
	char *str, *strs[100];

	ast_mutex_lock(&voter_lock);
	str = NULL;
	if (node) {
		str = ast_strdup(node);
	}
	n = 0;
	if (str) {
		n = finddelim(str, strs, ARRAY_LEN(strs));
	}
	for (j = 1; j <= maxpvtorder; j++) {
		for (p = pvts; p; p = p->next) {
			if (p->order == j) {
				break;
			}
		}
		if (!p) {
			continue;
		}
		if (node && *node && str && n) {
			for (i = 0; i < n; i++) {
				if (!*strs[i]) {
					continue;
				}
				if (atoi(strs[i]) == p->nodenum) {
					break;
				}
			}
			if (i >= n) {
				continue;
			}
		}
		rpt_manager_success(ses, m);
		astman_append(ses, "Node: %d\r\n", p->nodenum);
		if (p->lastwon) {
			astman_append(ses, "Voted: %s\r\n", p->lastwon->name);
		}
		for (client = clients; client; client = client->next) {
			if (client->nodenum != p->nodenum) {
				continue;
			}
			if (!client->heardfrom) {
				continue;
			}
			if (IS_CLIENT_PROXY(client)) {
				astman_append(ses, "Client: %s", client->name);
				if (client->mix) {
					astman_append(ses, " Mix");
				}
				if (client->ismaster) {
					astman_append(ses, " Master");
				}
				if (client->curmaster) {
					astman_append(ses, " ActiveMaster");
				}
				astman_append(ses, "\r\n");
				astman_append(ses, "IP: %s:%d (Proxied)\r\n",
							  ast_inet_ntoa(client->proxy_sin.sin_addr), ntohs(client->proxy_sin.sin_port));
			} else {
				if (!client->respdigest) {
					continue;
				}
				astman_append(ses, "Client: %s", client->name);
				if (client->mix) {
					astman_append(ses, " Mix");
				}
				if (client->ismaster) {
					astman_append(ses, " Master");
				}
				if (client->curmaster) {
					astman_append(ses, " ActiveMaster");
				}
				astman_append(ses, "\r\n");
				astman_append(ses, "IP: %s:%d\r\n", ast_inet_ntoa(client->sin.sin_addr), ntohs(client->sin.sin_port));
			}
			astman_append(ses, "RSSI: %d\r\n", client->lastrssi);
		}
	}
	ast_mutex_unlock(&voter_lock);
	astman_append(ses, "\r\n");	/* Properly terminate Manager output */
	if (str) {
		ast_free(str);
	}
	return RESULT_SUCCESS;
}

#include "xpmr/xpmr.c"

/*!
 * \brief Advance transmit and receive sequence numbers for mixed clients and wake per-node transmit workers.
 *
 * Increments the tx sequence number for each client that has completed authentication, has been heard
 * from recently, and is marked for mixing. Also advances each client's rx sequence number in a
 * format-aware manner. After updating clients, signals the xmit condition on every per-node voter
 * instance to wake transmit threads.
 */
static void voter_xmit_master(void)
{
	struct voter_client *client;
	struct voter_pvt *p;

	for (client = clients; client; client = client->next) {
		if (!client->respdigest) {
			continue;
		}
		if (!client->heardfrom) {
			continue;
		}
		if (!client->mix) {
			continue;
		}
		client->txseqno++;
		if (client->rxseqno) {
			if (!client->doadpcm && !client->donulaw) {
				client->rxseqno++;
			} else {
				if (client->rxseq40ms) {
					client->rxseqno += 2;
				}
				client->rxseq40ms = !client->rxseq40ms;
			}
		}
	}
	for (p = pvts; p; p = p->next) {
		ast_mutex_lock(&p->xmit_lock);
		ast_cond_signal(&p->xmit_cond);
		ast_mutex_unlock(&p->xmit_lock);
	}
}

/*!
 * \brief Voter timing thread that drives the driver's internal 20 ms timing tick.
 *
 * Maintains the driver's master-relative timebase that is not dependent on the system
 * time of day, advances per-node timing counters,
 * sends empty frames when no master is present, performs periodic client timeout and
 * sanity checks, and wakes transmit logic as required.
 *
 * \param data Always NULL; unused.
 * \return     NULL on thread exit.
 */
static void *voter_timer(void *data)
{
	time_t t;
	struct voter_pvt *p;
	struct voter_client *client, *client1;
	struct timeval tv;
	int timingfd = ast_timer_fd(voter_thread_timer);

	while (run_forever && !ast_shutting_down()) {
		int timeout = -1;
		ast_waitfor_n_fd(&timingfd, 1, &timeout, NULL);
		if (ast_timer_ack(voter_thread_timer, 1) < 0) {
			ast_log(LOG_ERROR, "Failed to acknowledge timer\n");
			break;
		}

		ast_mutex_lock(&voter_lock);
		time(&t);
		if (!hasmaster) {
			master_time.vtime_sec = (uint32_t) t;
		}
		voter_timing_count++;
		if (!hasmaster) {
			for (p = pvts; p; p = p->next) {
				memset(p->buf + AST_FRIENDLY_OFFSET, 0xff, FRAME_SIZE);
				voter_mix_and_send(p, NULL, 0);
			}
			voter_xmit_master();
			gettimeofday(&tv, NULL);
			for (client = clients; client; client = client->next) {
				if (!ast_tvzero(client->lastheardtime)
					&& (voter_tvdiff_ms(tv, client->lastheardtime) >
						((client->ismaster) ? MASTER_TIMEOUT_MS : CLIENT_TIMEOUT_MS))) {
					ast_log(LOG_NOTICE, "VOTER client %s disconnect (timeout)\n", client->name);
					client->heardfrom = 0;
					client->respdigest = 0;
					client->lastheardtime = ast_tv(0, 0);
				}
			}
			if (check_client_sanity) {
				for (client = clients; client; client = client->next) {
					if (!client->respdigest) {
						continue;
					}
					for (client1 = client->next; client1; client1 = client1->next) {
						if ((client1->sin.sin_addr.s_addr == client->sin.sin_addr.s_addr) &&
							(client1->sin.sin_port == client->sin.sin_port)) {
							if (!client1->respdigest) {
								continue;
							}
							client->respdigest = 0;
							client->heardfrom = 0;
							client1->respdigest = 0;
							client1->heardfrom = 0;
						}
					}
				}
			}
		}
		ast_mutex_unlock(&voter_lock);
	}
	return NULL;
}

/*!
 * \brief UDP reader thread that processes incoming Voter protocol packets and updates voter state.
 *
 * This thread receives Voter-format UDP packets, matches them to configured clients (including
 * dynamic binding), validates/authenticates clients, and handles payloads such as audio
 * (ULAW/ADPCM/NULAW), proxy-encapsulated packets, GPS, and PING.  It updates timing and master
 * synchronization state, writes received audio and RSSI into per-client circular buffers,
 * performs RSSI-based selection and threshold/linger logic per node, queues audio/text/control
 * frames to the associated Asterisk channel, and sends authentication/keepalive responses when
 * appropriate.
 *
 * \param data Always NULL; unused thread argument.
 * \return     NULL when the thread exits.
 */
static void *voter_reader(void *data)
{
	char buf[4096], timestr[100], hasmastered, *cp, *cp1;
	char gps1[300], gps2[300], isproxy;
	struct sockaddr_in sin, sin_stream, psin;
	struct voter_pvt *p;
	int i, j, k, ms, maxrssi, master_port, no_ast_channel;
	struct ast_frame *f1, fr;
	socklen_t fromlen;
	ssize_t recvlen;
	struct timeval tv, timetv;
	FILE *gpsfp;
	struct voter_client *client = NULL, *client1, *maxclient, *lastmaster;
	VOTER_PACKET_HEADER *vph;
	VOTER_PROXY_HEADER proxy;
	VOTER_GPS *vgp;
	VOTER_REC rec;
	VOTER_STREAM stream;
	time_t timestuff, t;
	short silbuf[FRAME_SIZE];
#pragma pack(push)
#pragma pack(1)
#ifdef	ADPCM_LOOPBACK
	struct {
		VOTER_PACKET_HEADER vp;
		char rssi;
		char audio[FRAME_SIZE + 3];
	} audiopacket;
#endif
	struct {
		VOTER_PACKET_HEADER vp;
		char flags;
	} authpacket;
	struct {
		VOTER_PACKET_HEADER vp;
		VOTER_PROXY_HEADER vprox;
		char flags;
	} proxy_authpacket;
	struct {
		VOTER_PACKET_HEADER vp;
		unsigned int seqno;
		struct timeval txtime;
		struct timeval starttime;
		char filler[128];
	} pingpacket;
#pragma pack(pop)

	ast_debug(1, "VOTER: Reader thread started.\n");
	ast_mutex_lock(&voter_lock);
	master_port = 0;

	while (run_forever && !ast_shutting_down()) {
		ast_mutex_unlock(&voter_lock);
		no_ast_channel = 0;
		ms = 50;
		i = ast_waitfor_n_fd(&udp_socket, 1, &ms, NULL);
		ast_mutex_lock(&voter_lock);
		if (i == -1) {
			ast_mutex_unlock(&voter_lock);
			ast_log(LOG_ERROR, "Error in select()\n");
			pthread_exit(NULL);
		}
		/* Check all of our nodes to see if any are receiving and have timed out */
		gettimeofday(&tv, NULL);
		for (p = pvts; p; p = p->next) {
			if (!p->rxkey) {
				continue;
			}
			if (voter_tvdiff_ms(tv, p->lastrxtime) > RX_TIMEOUT_MS) {
				struct ast_frame wf = {
					.frametype = AST_FRAME_CONTROL,
					.subclass.integer = AST_CONTROL_RADIO_UNKEY,
					.src = __PRETTY_FUNCTION__,
				};
				ast_debug(3, "A VOTER on %d was receiving but now has stopped (RX_TIMEOUT_MS)!\n", p->nodenum);
				ast_queue_frame(p->owner, &wf);
				p->rxkey = 0;
				p->lastwon = NULL;
			}
		}
		if (i < 0) {
			continue;
		}
		/* Is there activity on our UDP socket */
		if (i != udp_socket) {
			continue;
		}
		fromlen = sizeof(struct sockaddr_in);
		recvlen = recvfrom(udp_socket, buf, sizeof(buf) - 1, 0, (struct sockaddr *) &sin, &fromlen);
		/* if set got something worthwhile */
		if (recvlen < sizeof(VOTER_PACKET_HEADER)) {
			continue;
		}
		vph = (VOTER_PACKET_HEADER *) buf;
		ast_debug(7, "Got RX packet, len %d payload %d challenge %s digest %08x\n", (int) recvlen, ntohs(vph->payload_type),
			vph->challenge, ntohl(vph->digest));
		client = NULL;
		if (!check_client_sanity && master_port) {
			sin.sin_port = htons(master_port);
		}
		isproxy = 0;
		if (vph->digest) {
			gettimeofday(&tv, NULL);
			/* first see if client is found */
			for (client = clients; client; client = client->next) {
				if (client->digest == htonl(vph->digest)) {
					break;
				}
			}
			if (DEBUG_ATLEAST(4) && client && ((unsigned char) *(buf + sizeof(VOTER_PACKET_HEADER)) > 0) &&
				ntohs(vph->payload_type) == VOTER_PAYLOAD_ULAW) {
				timestuff = (time_t) ntohl(vph->curtime.vtime_sec);
				strftime(timestr, sizeof(timestr) - 1, "%Y %T", localtime((time_t *) &timestuff));
				ast_debug(4, "Time:      %s.%03d, (%s) RSSI: %d\n", timestr, ntohl(vph->curtime.vtime_nsec) / 1000000,
					client->name, (unsigned char) *(buf + sizeof(VOTER_PACKET_HEADER)));
			}
			if (client) {
				/* Search for connected Asterisk channel for this known client */
				for (p = pvts; p; p = p->next) {
					if (p->nodenum == client->nodenum) {
						break;
					}
				}
				if (!p) {
					/* We didn't find an asterisk channel,
					 * act like we don't know the client,
					 * do not respond via no_ast_channel flag
					 */
					ast_debug(2, "Request for voter client %s to node %d with no matching asterisk channel\n", client->name,
						client->nodenum);
					no_ast_channel = 1;
					client = NULL;
				}
			}
			if (client) {
				if (check_client_sanity && p && !p->priconn) {
					if ((client->sin.sin_addr.s_addr && (client->sin.sin_addr.s_addr != sin.sin_addr.s_addr)) ||
						(client->sin.sin_port && (client->sin.sin_port != sin.sin_port))) {
						client->heardfrom = 0;
					}
					if (IS_CLIENT_PROXY(client)) {
						client->heardfrom = 0;
						client->respdigest = 0;
					}
				}
				lastmaster = NULL;
				/* first, kill all the 'curmaster' flags */
				for (client1 = clients; client1; client1 = client1->next) {
					if (client1->curmaster) {
						lastmaster = client1;
						client1->curmaster = 0;
					}
				}
				client->lastheardtime = tv;
				/* if possible, set it to first 'active' one */
				for (client1 = clients; client1; client1 = client1->next) {
					if (!client1->ismaster) {
						continue;
					}
					if (ast_tvzero(client1->lastheardtime)) {
						continue;
					}
					if (voter_tvdiff_ms(tv, client1->lastheardtime) > MASTER_TIMEOUT_MS) {
						continue;
					}
					client1->curmaster = 1;
					if (client1 != lastmaster) {
						ast_log(LOG_NOTICE, "VOTER Master changed from client %s to %s\n",
							(lastmaster) ? lastmaster->name : "NONE", client1->name);
					}
					break;
				}
				/* if not, just set to to 'one of them' */
				if (!client1) {
					if (client->ismaster) {
						client->curmaster = 1;
					} else {
						for (client1 = clients; client1; client1 = client1->next) {
							if (!client1->ismaster) {
								continue;
							}
							client1->curmaster = 1;
							if (client1 != lastmaster) {
								ast_log(LOG_NOTICE, "VOTER Master changed from client %s to %s (inactive)\n",
									(lastmaster) ? lastmaster->name : "NONE", client1->name);
							}
							break;
						}
					}
				}
				if (!client || (ntohs(vph->payload_type) != VOTER_PAYLOAD_PROXY)) {
					client->respdigest = crc32_bufs((char *) vph->challenge, password);
				}
				client->sin = sin;
				memset(&client->proxy_sin, 0, sizeof(client->proxy_sin));
				if (!client->curmaster && hasmaster) {
					if (last_master_count && (voter_timing_count > (last_master_count + MAX_MASTER_COUNT))) {
						ast_log(LOG_NOTICE, "VOTER lost master timing source!!\n");
						last_master_count = 0;
						master_time.vtime_sec = 0;
						for (client1 = client->next; client1; client1 = client1->next) {
							memset(client1->audio, 0xff, client1->buflen);
							memset(client1->rssi, 0, client1->buflen);
						}
						for (p = pvts; p; p = p->next) {
							if (p->rxkey) {
								struct ast_frame wf = {
									.frametype = AST_FRAME_CONTROL,
									.subclass.integer = AST_CONTROL_RADIO_UNKEY,
									.src = __PRETTY_FUNCTION__,
								};

								ast_queue_frame(p->owner, &wf);
							}
							p->lastwon = NULL;
							p->rxkey = 0;
							ast_mutex_lock(&p->txqlock);
							while ((f1 = AST_LIST_REMOVE_HEAD(&p->txq, frame_list)) != NULL) {
								ast_frfree(f1);
							}
							ast_mutex_unlock(&p->txqlock);
						}
						continue;
					}
					if (!master_time.vtime_sec) {
						continue;
					}
				}
			}
			if (client && ntohs(vph->payload_type)) {
				client->heardfrom = 1;
			}
			/* if we know the client, find the connection that the audio belongs to and send it there */
			if (client && client->heardfrom &&
				(((ntohs(vph->payload_type) == VOTER_PAYLOAD_ULAW) && (recvlen == (sizeof(VOTER_PACKET_HEADER) + FRAME_SIZE + 1))) ||
					((ntohs(vph->payload_type) == VOTER_PAYLOAD_ADPCM) && (recvlen == (sizeof(VOTER_PACKET_HEADER) + FRAME_SIZE + 4))) ||
					(ntohs(vph->payload_type) == VOTER_PAYLOAD_PROXY) ||
					((ntohs(vph->payload_type) == VOTER_PAYLOAD_NULAW) && (recvlen == (sizeof(VOTER_PACKET_HEADER) + FRAME_SIZE + 1))))) {
				for (p = pvts; p; p = p->next) {
					if (p->nodenum == client->nodenum) {
						break;
					}
				}
				/* if we found the client */
				if (p) {
					long long btime, ptime, difftime;
					int index, flen;

					gettimeofday(&client->lastheardtime, NULL);
					if (client->curmaster) {
						if (!master_time.vtime_sec) {
							for (p = pvts; p; p = p->next) {
								ast_mutex_lock(&p->txqlock);
								while ((f1 = AST_LIST_REMOVE_HEAD(&p->txq, frame_list)) != NULL) {
									ast_frfree(f1);
								}
								ast_mutex_unlock(&p->txqlock);
							}
						}
						last_master_count = voter_timing_count;
						master_time.vtime_sec = ntohl(vph->curtime.vtime_sec);
						master_time.vtime_nsec = ntohl(vph->curtime.vtime_nsec);
						if (!master_port) {
							master_port = ntohs(sin.sin_port);
						}
					} else {
						if (!master_time.vtime_sec) {
							continue;
						}
						if (ntohs(vph->payload_type) == VOTER_PAYLOAD_PROXY) {
							memcpy(&proxy, buf + sizeof(VOTER_PACKET_HEADER), sizeof(proxy));
							memmove(buf + sizeof(VOTER_PACKET_HEADER), buf + sizeof(VOTER_PACKET_HEADER) + sizeof(VOTER_PROXY_HEADER),
								recvlen - (sizeof(VOTER_PACKET_HEADER) + sizeof(VOTER_PROXY_HEADER)));
							vph->payload_type = proxy.payload_type;
							psin.sin_family = AF_INET;
							psin.sin_addr.s_addr = proxy.ipaddr;
							psin.sin_port = proxy.port;
							isproxy = 1;
							if (!p->isprimary) {
								vph->digest = htonl(client->respdigest);
								strcpy((char *) vph->challenge, challenge);
								sendto(udp_socket, buf, recvlen - sizeof(proxy), 0, (struct sockaddr *) &psin, sizeof(psin));
								continue;
							}
							ast_copy_string(client->saved_challenge, proxy.challenge, sizeof(client->saved_challenge) - 1);
							client->proxy_sin = psin;
							if (proxy.flags & 32) {
								client->mix = 1;
							} else {
								client->mix = 0;
							}
							recvlen -= sizeof(proxy);
							ast_debug(6, "Now (proxy) Got RX packet, len %d payload %d challenge %s digest %08x\n", (int) recvlen,
								ntohs(vph->payload_type), vph->challenge, ntohl(vph->digest));
							if (ntohs(vph->payload_type) == VOTER_PAYLOAD_GPS) {
								goto process_gps;
							}
						} else if (p->priconn && !client->mix) {
							memcpy(&proxy, buf + sizeof(VOTER_PACKET_HEADER), sizeof(proxy));
							proxy.ipaddr = sin.sin_addr.s_addr;
							proxy.port = sin.sin_port;
							proxy.payload_type = vph->payload_type;
							ast_copy_string(proxy.challenge, challenge, sizeof(proxy.challenge) - 1);
							vph->payload_type = htons(VOTER_PAYLOAD_PROXY);
							proxy.flags = 0;
							if (client->ismaster) {
								proxy.flags |= 2 | 8;
							}
							if (client->doadpcm) {
								proxy.flags |= 16;
							}
							if (client->mix) {
								proxy.flags |= 32;
							}
							if (client->nodeemp || p->hostdeemp) {
								proxy.flags |= 1;
							}
							if (client->noplfilter) {
								proxy.flags |= 4;
							}
							vph->digest = htonl(crc32_bufs(p->primary_challenge, client->pswd));
							memmove(buf + sizeof(VOTER_PACKET_HEADER) + sizeof(VOTER_PROXY_HEADER),
								buf + sizeof(VOTER_PACKET_HEADER), recvlen - sizeof(VOTER_PACKET_HEADER));
							memcpy(buf + sizeof(VOTER_PACKET_HEADER), &proxy, sizeof(proxy));
							ast_debug(3, "Sent outproxy to %s:%d for %s payload %d digest %08x\n", ast_inet_ntoa(p->primary.sin_addr),
								ntohs(p->primary.sin_port), client->name, ntohs(proxy.payload_type), ntohl(vph->digest));
							sendto(udp_socket, buf, recvlen + sizeof(proxy), 0, (struct sockaddr *) &p->primary, sizeof(p->primary));
							continue;
						}
					}
					if (client->mix) {
						if (ntohl(vph->curtime.vtime_nsec) > client->rxseqno) {
							client->rxseqno = 0;
							client->rxseqno_40ms = 0;
							client->rxseq40ms = 0;
							client->drain40ms = 0;
						}
						if (client->txseqno > (client->txseqno_rxkeyed + 4)) {
							client->rxseqno = 0;
							client->rxseqno_40ms = 0;
							client->rxseq40ms = 0;
							client->drain40ms = 0;
						}
						client->txseqno_rxkeyed = client->txseqno;
						if (!client->rxseqno) {
							client->rxseqno_40ms = client->rxseqno = ntohl(vph->curtime.vtime_nsec);
						}
						if (!client->doadpcm && !client->donulaw) {
							index = ntohl(vph->curtime.vtime_nsec) - client->rxseqno;
						} else {
							index = ntohl(vph->curtime.vtime_nsec) - client->rxseqno_40ms;
						}
						index *= FRAME_SIZE;
						index += BUFDELAY(client);
						index -= (FRAME_SIZE * 4);
						if (DEBUG_ATLEAST(3)) {
							if (!client->doadpcm && !client->donulaw) {
								ast_debug(7, "Mix client (Mulaw) %s index: %d their seq: %d our seq: %d\n", client->name, index,
									ntohl(vph->curtime.vtime_nsec), client->rxseqno);
							} else {
								ast_debug(7, "Mix client (ADPCM/Nulaw) %s index: %d their seq: %d our seq: %d\n", client->name,
									index, ntohl(vph->curtime.vtime_nsec), client->rxseqno_40ms);
							}
						}
					} else {
						btime = ((long long) master_time.vtime_sec * 1000000000LL) + master_time.vtime_nsec;
						btime += 40000000;
						if (client->curmaster) {
							btime -= 20000000;
						}
						ptime = ((long long) ntohl(vph->curtime.vtime_sec) * 1000000000LL) + ntohl(vph->curtime.vtime_nsec);
						difftime = (ptime - btime) + (BUFDELAY(client) * 125000LL);
						difftime -= puckoffset(client);
						index = (int) ((long long) difftime / 125000LL);
						if (DEBUG_ATLEAST(5) && ((unsigned char) *(buf + sizeof(VOTER_PACKET_HEADER)) > 0)) {
							timestuff = (time_t) master_time.vtime_sec;
							strftime(timestr, sizeof(timestr) - 1, "%Y %T", localtime((time_t *) &timestuff));
							ast_debug(4, "DrainTime: %s.%03d\n", timestr, master_time.vtime_nsec / 1000000);
							gettimeofday(&timetv, NULL);
							timestuff = (time_t) timetv.tv_sec;
							strftime(timestr, sizeof(timestr) - 1, "%Y %T", localtime((time_t *) &timestuff));
							ast_debug(4, "SysTime:   %s.%03d, diff: %lld, index: %d\n", timestr, (int) timetv.tv_usec / 1000,
								btime - ptime, index);
						}
					}
					/* if in bounds */
					if ((index > 0) && (index < (client->buflen - (FRAME_SIZE * 2)))) {
						f1 = NULL;
						/* if no RSSI, just make it quiet */
						if (!buf[sizeof(VOTER_PACKET_HEADER)]) {
							for (i = 0; i < FRAME_SIZE; i++) {
								buf[sizeof(VOTER_PACKET_HEADER) + i + 1] = 0xff;
							}
						}
						/* if otherwise (RSSI > 0), if ADPCM, translate it */
						else if (ntohs(vph->payload_type) == VOTER_PAYLOAD_ADPCM) {
#ifdef ADPCM_LOOPBACK
							memset(&audiopacket, 0, sizeof(audiopacket));
							strcpy((char *) audiopacket.vp.challenge, challenge);
							audiopacket.vp.payload_type = htons(3);
							audiopacket.rssi = 0;
							memcpy(audiopacket.audio, buf + sizeof(VOTER_PACKET_HEADER) + 1, FRAME_SIZE + 3);
							audiopacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
							audiopacket.vp.curtime.vtime_nsec = htonl(master_time.vtime_nsec);
							audiopacket.vp.digest = htonl(client->respdigest);
							sendto(udp_socket, &audiopacket, sizeof(audiopacket), 0, (struct sockaddr *) &client->sin,
								sizeof(client->sin));
#endif

							memset(&fr, 0, sizeof(fr));
							fr.frametype = AST_FRAME_VOICE;
							fr.subclass.format = ast_format_adpcm;
							fr.datalen = ADPCM_FRAME_SIZE;
							fr.samples = FRAME_SIZE * 2;
							fr.data.ptr = buf + sizeof(VOTER_PACKET_HEADER) + 1;
							fr.src = __PRETTY_FUNCTION__;
							f1 = ast_translate(p->adpcmin, &fr, 0);
						}
						/* if otherwise (RSSI > 0), if NULAW, translate it */
						else if (ntohs(vph->payload_type) == VOTER_PAYLOAD_NULAW) {
							short s, xbuf[FRAME_SIZE * 2];
#ifdef NULAW_LOOPBACK
							memset(&audiopacket, 0, sizeof(audiopacket));
							strcpy((char *) audiopacket.vp.challenge, challenge);
							audiopacket.vp.payload_type = htons(4);
							audiopacket.rssi = 0;
							memcpy(audiopacket.audio, buf + sizeof(VOTER_PACKET_HEADER) + 1, FRAME_SIZE);
							audiopacket.vp.curtime.vtime_sec = htonl(master_time.vtime_sec);
							audiopacket.vp.curtime.vtime_nsec = htonl(master_time.vtime_nsec);
							audiopacket.vp.digest = htonl(client->respdigest);
							sendto(udp_socket, &audiopacket, sizeof(audiopacket), 0, (struct sockaddr *) &client->sin,
								sizeof(client->sin));
#endif

							for (i = 0; i < FRAME_SIZE * 2; i += 2) {
								s = (AST_MULAW((int) (unsigned char) buf[sizeof(VOTER_PACKET_HEADER) + 1 + (i >> 1)])) / 2;
								xbuf[i] = lpass4(s, p->rlpx, p->rlpy);
								xbuf[i + 1] = lpass4(s, p->rlpx, p->rlpy);
							}
							memset(&fr, 0, sizeof(fr));
							fr.frametype = AST_FRAME_VOICE;
							fr.subclass.format = ast_format_slin;
							fr.datalen = FRAME_SIZE * 4;
							fr.samples = FRAME_SIZE * 2;
							fr.data.ptr = xbuf;
							fr.src = __PRETTY_FUNCTION__;
							f1 = ast_translate(p->nuin, &fr, 0);
						}
						if (!client->doadpcm && !client->donulaw) {
							index = (index + client->drainindex) % client->buflen;
						} else {
							index = (index + client->drainindex_40ms) % client->buflen;
						}
						flen = (f1) ? f1->datalen : FRAME_SIZE;
						i = (int) client->buflen - (index + flen);
						if (i >= 0) {
							memcpy(client->audio + index, ((f1) ? f1->data.ptr : buf + sizeof(VOTER_PACKET_HEADER) + 1), flen);
							memset(client->rssi + index, buf[sizeof(VOTER_PACKET_HEADER)], flen);
						} else {
							memcpy(client->audio + index, ((f1) ? f1->data.ptr : buf + sizeof(VOTER_PACKET_HEADER) + 1), flen + i);
							memset(client->rssi + index, buf[sizeof(VOTER_PACKET_HEADER)], flen + i);
							memcpy(client->audio, ((f1) ? f1->data.ptr : buf + sizeof(VOTER_PACKET_HEADER) + 1) + (flen + i), -i);
							memset(client->rssi, buf[sizeof(VOTER_PACKET_HEADER)], -i);
						}
						if (f1) {
							ast_frfree(f1);
						}
					} else if (client->mix) {
						client->rxseqno = 0;
						client->rxseqno_40ms = 0;
						client->rxseq40ms = 0;
						client->drain40ms = 0;
						ast_debug(3, "Mix client %s index %i < bufflen %i out of bounds, resetting!!\n", client->name, index,
							(client->buflen - (FRAME_SIZE * 2)));
						ast_log(LOG_ERROR, "Mix client %s out of bounds! buflen must be >=160 in voter.conf with Mix clients!",
							client->name);
					}
					if (client->curmaster) {
						gettimeofday(&tv, NULL);
						for (client = clients; client; client = client->next) {
							if (!ast_tvzero(client->lastheardtime) &&
								(voter_tvdiff_ms(tv, client->lastheardtime) > ((client->ismaster) ? MASTER_TIMEOUT_MS : CLIENT_TIMEOUT_MS))) {
								ast_log(LOG_NOTICE, "VOTER client %s disconnect (timeout)\n", client->name);
								client->heardfrom = 0;
								client->respdigest = 0;
							}
							if (!client->heardfrom) {
								client->lastheardtime.tv_sec = client->lastheardtime.tv_usec = 0;
							}
						}
						if (check_client_sanity) {
							for (client = clients; client; client = client->next) {
								for (p = pvts; p; p = p->next) {
									if (p->nodenum == client->nodenum) {
										break;
									}
								}
								if (!p || p->priconn) {
									continue;
								}
								if (!client->respdigest) {
									continue;
								}
								for (client1 = client->next; client1; client1 = client1->next) {
									if (client1 == client) {
										continue;
									}
									if ((client1->sin.sin_addr.s_addr == client->sin.sin_addr.s_addr) &&
										(client1->sin.sin_port == client->sin.sin_port)) {
										if (!client1->respdigest) {
											continue;
										}
										client->respdigest = 0;
										client->heardfrom = 0;
										client1->respdigest = 0;
										client1->heardfrom = 0;
									}
								}
							}
						}
						hasmastered = 0;
						voter_xmit_master();
						for (p = pvts; p; p = p->next) {
							char startagain;

							startagain = 0;
							maxrssi = 0;
							maxclient = NULL;
							for (client = clients; client; client = (startagain) ? clients : client->next) {
								int maxprio, thisprio;

								startagain = 0;
								if (client->nodenum != p->nodenum) {
									continue;
								}
								if (client->mix) {
									continue;
								}
								if (client->prio_override == -1) {
									continue;
								}
								k = 0;
								i = (int) client->buflen - ((int) client->drainindex + FRAME_SIZE);
								if (i >= 0) {
									for (j = client->drainindex; j < client->drainindex + FRAME_SIZE; j++) {
										k += client->rssi[j];
									}
								} else {
									for (j = client->drainindex; j < client->drainindex + (FRAME_SIZE + i); j++) {
										k += client->rssi[j];
									}
									for (j = 0; j < -i; j++) {
										k += client->rssi[j];
									}
								}
								client->lastrssi = k / FRAME_SIZE;
								maxprio = thisprio = 0;
								if (maxclient) {
									if (maxclient->prio_override > -2) {
										maxprio = maxclient->prio_override;
									} else {
										maxprio = maxclient->prio;
									}
								}
								if (client->prio_override > -2) {
									thisprio = client->prio_override;
								} else {
									thisprio = client->prio;
								}
								if (((client->lastrssi > maxrssi) && (thisprio == maxprio)) || (client->lastrssi && (thisprio > maxprio))) {
									maxrssi = client->lastrssi;
									maxclient = client;
									if (thisprio > maxprio) {
										startagain = 1;
									}
								}
							}
							for (client = clients; client; client = client->next) {
								if (client->nodenum != p->nodenum) {
									continue;
								}
								if (client->mix) {
									continue;
								}
								if (client->prio_override == -1) {
									continue;
								}
								i = (int) client->buflen - ((int) client->drainindex + FRAME_SIZE);
								if (i >= 0) {
									for (j = client->drainindex; j < client->drainindex + FRAME_SIZE; j++) {
										client->rssi[j] = 0;
									}
								} else {
									for (j = client->drainindex; j < client->drainindex + (FRAME_SIZE + i); j++) {
										client->rssi[j] = 0;
									}
									for (j = 0; j < -i; j++) {
										client->rssi[j] = 0;
									}
								}
							}
							if (!maxclient) {
								maxrssi = 0;
							}
							memset(p->buf + AST_FRIENDLY_OFFSET, 0xff, FRAME_SIZE);
							if (maxclient) {
								int maxprio, lastprio;

								if (maxclient->prio_override > -2) {
									maxprio = maxclient->prio_override;
								} else {
									maxprio = maxclient->prio;
								}
								lastprio = 0;
								if (p->lastwon) {
									if (p->lastwon->prio_override > -2) {
										lastprio = p->lastwon->prio_override;
									} else {
										lastprio = p->lastwon->prio;
									}
								}
								/* if not on same client, and we have thresholds, and priority appropriate */
								if (p->lastwon && p->nthresholds && (maxprio <= lastprio)) {
									/* go thru all the thresholds */
									for (i = 0; i < p->nthresholds; i++) {
										/* if meets criteria */
										if (p->lastwon->lastrssi >= p->rssi_thresh[i]) {
											/* if not at same threshold, change to new one */
											if ((i + 1) != p->threshold) {
												p->threshold = i + 1;
												p->threshcount = 0;
												ast_debug(3, "New threshold %d, client %s, RSSI %d\n", p->threshold,
													p->lastwon->name, p->lastwon->lastrssi);
											}
											/* at the same threshold still, if count is enabled and is met */
											else if (p->count_thresh[i] && (p->threshcount++ >= p->count_thresh[i])) {
												ast_debug(3, "Threshold %d time (%d) exceeded, client %s, RSSI %d\n",
													p->threshold, p->count_thresh[i], p->lastwon->name, p->lastwon->lastrssi);
												p->threshold = 0;
												p->threshcount = 0;
												p->lingercount = 0;
												continue;
											}
											p->lingercount = 0;
											maxclient = p->lastwon;
											maxrssi = maxclient->lastrssi;
											break;
										}
										/* if doesn't match any criteria */
										if (i == (p->nthresholds - 1)) {
											if (DEBUG_ATLEAST(3) && p->threshold) {
												ast_debug(3, "Nothing matches criteria any more\n");
											}
											if (p->threshold) {
												p->lingercount = p->linger_thresh[p->threshold - 1];
											}
											p->threshold = 0;
											p->threshcount = 0;
										}
									}
								}
								if (p->lingercount) {
									ast_debug(3, "Lingering on client %s, RSSI %d, Maxclient is %s, RSSI %d\n", p->lastwon->name,
										p->lastwon->lastrssi, maxclient->name, maxrssi);
									p->lingercount--;
									maxclient = p->lastwon;
									maxrssi = maxclient->lastrssi;
								}
								if (p->voter_test > 0) { /* perform cyclic selection */
									/* see how many are eligible */
									for (i = 0, client = clients; client; client = client->next) {
										if (client->nodenum != p->nodenum) {
											continue;
										}
										if (client->mix) {
											continue;
										}
										if (client->lastrssi == maxrssi) {
											i++;
										}
									}
									if (p->voter_test == 1) {
										p->testindex = random() % i;
									} else {
										p->testcycle++;
										if (p->testcycle >= (p->voter_test - 1)) {
											p->testcycle = 0;
											p->testindex++;
											if (p->testindex >= i) {
												p->testindex = 0;
											}
										}
									}
									for (i = 0, client = clients; client; client = client->next) {
										if (client->nodenum != p->nodenum) {
											continue;
										}
										if (client->mix) {
											continue;
										}
										if (client->lastrssi != maxrssi) {
											continue;
										}
										if (i++ == p->testindex) {
											maxclient = client;
											maxrssi = client->lastrssi;
											break;
										}
									}
								} else {
									p->testcycle = 0;
									p->testindex = 0;
								}
								if (!maxclient) { /* if nothing there */
									memset(silbuf, 0, sizeof(silbuf));
									memset(&fr, 0, sizeof(fr));
									fr.frametype = AST_FRAME_VOICE;
									fr.subclass.format = ast_format_slin;
									fr.datalen = FRAME_SIZE * 2;
									fr.samples = FRAME_SIZE;
									fr.data.ptr = silbuf;
									fr.src = __PRETTY_FUNCTION__;
									p->threshold = 0;
									p->threshcount = 0;
									p->lingercount = 0;
									p->winner = 0;
									incr_drainindex(p);
									ast_queue_frame(p->owner, &fr);
									continue;
								}
								i = (int) maxclient->buflen - ((int) maxclient->drainindex + FRAME_SIZE);
								if (i >= 0) {
									memcpy(p->buf + AST_FRIENDLY_OFFSET, maxclient->audio + maxclient->drainindex, FRAME_SIZE);
								} else {
									memcpy(p->buf + AST_FRIENDLY_OFFSET, maxclient->audio + maxclient->drainindex, FRAME_SIZE + i);
									memcpy(p->buf + AST_FRIENDLY_OFFSET + (maxclient->buflen - i), maxclient->audio, -i);
								}
								for (client = clients; client; client = client->next) {
									if (client->nodenum != p->nodenum) {
										continue;
									}
									if (client->mix) {
										continue;
									}
									if (p->recfp) {
										if (!hasmastered) {
											hasmastered = 1;
											memset(&rec, 0, sizeof(rec));
											memcpy(rec.audio, &master_time, sizeof(master_time));
											fwrite(&rec, 1, sizeof(rec), p->recfp);
										}
										ast_copy_string(rec.name, client->name, sizeof(rec.name) - 1);
										rec.rssi = client->lastrssi;
										if (i >= 0) {
											memcpy(rec.audio, client->audio + client->drainindex, FRAME_SIZE);
										} else {
											memcpy(rec.audio, client->audio + client->drainindex, FRAME_SIZE + i);
											memset(client->audio + client->drainindex, 0xff, FRAME_SIZE + i);
											memcpy(rec.audio + FRAME_SIZE + i, client->audio, -i);
											memset(client->audio + client->drainindex, 0xff, FRAME_SIZE + i);
										}
										fwrite(&rec, 1, sizeof(rec), p->recfp);
									}
									if (i >= 0) {
										memset(client->audio + client->drainindex, 0xff, FRAME_SIZE);
									} else {
										memset(client->audio + client->drainindex, 0xff, FRAME_SIZE + i);
										memset(client->audio, 0xff, -i);
									}
								}
								if (!p->duplex && p->txkey) {
									p->rxkey = 0;
									p->lastwon = NULL;
									memset(silbuf, 0, sizeof(silbuf));
									memset(&fr, 0, sizeof(fr));
									fr.frametype = AST_FRAME_VOICE;
									fr.subclass.format = ast_format_slin;
									fr.datalen = FRAME_SIZE * 2;
									fr.samples = FRAME_SIZE;
									fr.data.ptr = silbuf;
									fr.src = __PRETTY_FUNCTION__;
									p->threshold = 0;
									p->threshcount = 0;
									p->lingercount = 0;
									p->winner = 0;
									incr_drainindex(p);
									ast_queue_frame(p->owner, &fr);
									continue;
								}
								if (p->plfilter || p->hostdeemp) {
									short ix;
									for (i = 0; i < FRAME_SIZE; i++) {
										j = p->buf[AST_FRIENDLY_OFFSET + i] & 0xff;
										ix = AST_MULAW(j);
										if (p->plfilter) {
											ix = hpass6(ix, p->hpx, p->hpy);
										}
										if (p->hostdeemp) {
											ix = deemp1(ix, &p->hdx);
										}
										p->buf[AST_FRIENDLY_OFFSET + i] = AST_LIN2MU(ix);
									}
								}
								stream.curtime = master_time;
								memcpy(stream.audio, p->buf + AST_FRIENDLY_OFFSET, FRAME_SIZE);
								sprintf(stream.str, "%s", maxclient->name);
								for (client = clients; client; client = client->next) {
									if (client->nodenum != p->nodenum) {
										continue;
									}
									sprintf(stream.str + strlen(stream.str), ",%s=%d", client->name, client->lastrssi);
								}
								for (i = 0; i < p->nstreams; i++) {
									cp = ast_strdup(p->streams[i]);
									if (!cp) {
										break;
									}
									cp1 = strchr(cp, ':');
									if (cp1) {
										*cp1 = 0;
										j = atoi(cp1 + 1);
									} else {
										j = listen_port;
									}
									sin_stream.sin_family = AF_INET;
									sin_stream.sin_addr.s_addr = inet_addr(cp);
									sin_stream.sin_port = htons(j);
									sendto(udp_socket, &stream, sizeof(stream), 0, (struct sockaddr *) &sin_stream, sizeof(sin_stream));
									ast_free(cp);
								}
								if (maxclient != p->lastwon) {
									p->lastwon = maxclient;
									ast_debug(1, "VOTER client %s selected for node %d\n", maxclient->name, p->nodenum);
									memset(&fr, 0, sizeof(fr));
									fr.datalen = strlen(maxclient->name) + 1;
									fr.frametype = AST_FRAME_TEXT;
									fr.data.ptr = maxclient->name;
									fr.src = __PRETTY_FUNCTION__;
									ast_queue_frame(p->owner, &fr);
								}
								ast_debug(4, "Receiving from client %s RSSI %d\n", maxclient->name, maxrssi);
							}
							if (!p->duplex && p->txkey) {
								p->rxkey = 0;
								p->lastwon = NULL;
								memset(silbuf, 0, sizeof(silbuf));
								memset(&fr, 0, sizeof(fr));
								fr.frametype = AST_FRAME_VOICE;
								fr.subclass.format = ast_format_slin;
								fr.datalen = FRAME_SIZE * 2;
								fr.samples = FRAME_SIZE;
								fr.data.ptr = silbuf;
								fr.src = __PRETTY_FUNCTION__;
								p->threshold = 0;
								p->threshcount = 0;
								p->lingercount = 0;
								p->winner = 0;
								incr_drainindex(p);
								ast_queue_frame(p->owner, &fr);
								continue;
							}
							if (!voter_mix_and_send(p, maxclient, maxrssi)) {
								continue;
							}
						}
					}
				}
				continue;
			}
			/* if we know the dude, and its ping, process it */
			if (client && client->heardfrom && (ntohs(vph->payload_type) == VOTER_PAYLOAD_PING) && (recvlen == sizeof(pingpacket))) {
				int timediff;

				memcpy(&pingpacket, buf, sizeof(pingpacket));
				gettimeofday(&client->ping_last_rxtime, NULL);
				/* if ping not for this session */
				if (voter_tvdiff_ms(client->ping_txtime, pingpacket.starttime)) {
					continue;
				}
				if (client->ping_last_seqno && (pingpacket.seqno < (client->ping_last_seqno + 1))) {
					ast_log(LOG_WARNING, "PING (%s): Packets out of sequence!!\n", client->name);
					client->pings_oos++;
				}
				timediff = ast_tvdiff_ms(client->ping_last_rxtime, pingpacket.txtime);
				if (timediff < 0) {
					ast_log(LOG_WARNING, "PING (%s): Packet has invalid time (diff=%d)!!\n", client->name, timediff);
					continue;
				}
				client->ping_last_seqno = pingpacket.seqno;
				client->pings_received++;
				client->pings_total_ms += timediff;
				if (!client->pings_worst) {
					client->pings_worst = timediff;
				}
				if (!client->pings_best) {
					client->pings_best = timediff;
				}
				if (timediff < client->pings_best) {
					client->pings_best = timediff;
				}
				if (timediff > client->pings_worst) {
					client->pings_worst = timediff;
				}
				ast_verb(1, "PING (%s) Response:   seqno: %u  diff: %d ms\n", client->name, pingpacket.seqno, timediff);

				timestuff = (time_t) ntohl(vph->curtime.vtime_sec);
				strftime(timestr, sizeof(timestr) - 1, "%Y %T", localtime((time_t *) &timestuff));
				/* ast_debug(3, "PING (%s):   seqno: %u %s.%09d\n",client->name,seqno,timestr,ntohl(vph->curtime.vtime_nsec)); */

				check_ping_done(client);
				continue;
			}
			/* if we know the dude, find the connection his audio belongs to and send it there */
			if (client && client->heardfrom && (ntohs(vph->payload_type) == VOTER_PAYLOAD_GPS) &&
				((recvlen == sizeof(VOTER_PACKET_HEADER)) || (recvlen == (sizeof(VOTER_PACKET_HEADER) + sizeof(VOTER_GPS))) ||
					(recvlen == ((sizeof(VOTER_PACKET_HEADER) + sizeof(VOTER_GPS)) - 1)))) {
				gettimeofday(&client->lastheardtime, NULL);
				client->lastgpstime.vtime_sec = ntohl(vph->curtime.vtime_sec);
				client->lastgpstime.vtime_nsec = ntohl(vph->curtime.vtime_nsec);
				for (p = pvts; p; p = p->next) {
					if (p->nodenum == client->nodenum) {
						break;
					}
				}
				if (client->curmaster) {
					mastergps_time.vtime_sec = ntohl(vph->curtime.vtime_sec);
					mastergps_time.vtime_nsec = ntohl(vph->curtime.vtime_nsec);
				} else if (p && p->priconn && !client->mix) {
					memcpy(&proxy, buf + sizeof(VOTER_PACKET_HEADER), sizeof(proxy));
					proxy.ipaddr = sin.sin_addr.s_addr;
					proxy.port = sin.sin_port;
					proxy.payload_type = vph->payload_type;
					ast_copy_string(proxy.challenge, challenge, sizeof(challenge));
					vph->payload_type = htons(VOTER_PAYLOAD_PROXY);
					proxy.flags = 0;
					if (client->ismaster) {
						proxy.flags |= 2 | 8;
					}
					if (client->doadpcm) {
						proxy.flags |= 16;
					}
					if (client->mix) {
						proxy.flags |= 32;
					}
					if (client->nodeemp || p->hostdeemp) {
						proxy.flags |= 1;
					}
					if (client->noplfilter) {
						proxy.flags |= 4;
					}
					vph->digest = htonl(crc32_bufs(p->primary_challenge, client->pswd));
					memmove(buf + sizeof(VOTER_PACKET_HEADER) + sizeof(VOTER_PROXY_HEADER), buf + sizeof(VOTER_PACKET_HEADER),
						recvlen - sizeof(VOTER_PACKET_HEADER));
					memcpy(buf + sizeof(VOTER_PACKET_HEADER), &proxy, sizeof(proxy));
					ast_debug(3, "Sent outproxy to %s:%d for %s payload %d digest %08x\n", ast_inet_ntoa(p->primary.sin_addr),
						ntohs(p->primary.sin_port), client->name, ntohs(proxy.payload_type), ntohl(vph->digest));
					sendto(udp_socket, buf, recvlen + sizeof(proxy), 0, (struct sockaddr *) &p->primary, sizeof(p->primary));
					continue;
				}
process_gps:
				client->lastmastergpstime.vtime_sec = mastergps_time.vtime_sec;
				client->lastmastergpstime.vtime_nsec = mastergps_time.vtime_nsec;
				if (DEBUG_ATLEAST(4)) {
					gettimeofday(&timetv, NULL);
					timestuff = (time_t) ntohl(vph->curtime.vtime_sec);
					strftime(timestr, sizeof(timestr) - 1, "%Y %T", localtime((time_t *) &timestuff));

					ast_debug(4, "GPSTime from %s:   %s.%09d\n", client->name, timestr, ntohl(vph->curtime.vtime_nsec));
					timetv.tv_usec = ((timetv.tv_usec + 10000) / 20000) * 20000;
					if (timetv.tv_usec >= 1000000) {
						timetv.tv_sec++;
						timetv.tv_usec -= 1000000;
					}
					timestuff = (time_t) timetv.tv_sec;
					strftime(timestr, sizeof(timestr) - 1, "%Y %T", localtime((time_t *) &timestuff));
					ast_debug(4, "SysTime:   %s.%06d\n", timestr, (int) timetv.tv_usec);
					timestuff = (time_t) master_time.vtime_sec;
					strftime(timestr, sizeof(timestr) - 1, "%Y %T", localtime((time_t *) &timestuff));
					ast_debug(4, "DrainTime: %s.%03d\n", timestr, master_time.vtime_nsec / 1000000);
				}
				if (recvlen == sizeof(VOTER_PACKET_HEADER)) {
					ast_debug(5, "Got GPS Keepalive from %s\n", client->name);
				} else {
					vgp = (VOTER_GPS *) (buf + sizeof(VOTER_PACKET_HEADER));
					if (client->gpsid) {
						snprintf(gps1, sizeof(gps1) - 1, GPS_WORK_FILE, client->gpsid);
						snprintf(gps2, sizeof(gps2) - 1, GPS_DATA_FILE, client->gpsid);
						gpsfp = fopen(gps1, "w");
						if (!gpsfp) {
							ast_log(LOG_ERROR, "Unable to open GPS work file %s!!\n", gps1);
							continue;
						}
						time(&t);
						fprintf(gpsfp, "%u %s %s %sM\n", (unsigned int) t, vgp->lat, vgp->lon, vgp->elev);
						fclose(gpsfp);
						rename(gps1, gps2);
					}
					ast_debug(5, "Got GPSLoc from %s: Lat: %s, Lon: %s, Elev: %s\n", client->name, vgp->lat, vgp->lon, vgp->elev);
				}
				continue;
			}
			if (client) {
				client->heardfrom = 1;
			}
		}

		if (no_ast_channel) {
			/* No Askterisk channel, do not respond to the client. */
			continue;
		}
		/* otherwise, we just need to send an empty packet to the dude */
		memset(&authpacket, 0, sizeof(authpacket));
		memset(&proxy_authpacket, 0, sizeof(proxy_authpacket));
		if (client) {
			client->txseqno = 0;
			client->txseqno_rxkeyed = 0;
			client->rxseqno = 0;
			client->rxseqno_40ms = 0;
			client->rxseq40ms = 0;
			client->drain40ms = 0;
		}
		strcpy((char *) authpacket.vp.challenge, challenge);
		gettimeofday(&tv, NULL);
		authpacket.vp.curtime.vtime_sec = htonl(tv.tv_sec);
		authpacket.vp.curtime.vtime_nsec = htonl(tv.tv_usec * 1000);
		/* make our digest based on their challenge */
		authpacket.vp.digest = htonl(crc32_bufs((char *) vph->challenge, password));
		authpacket.flags = 0;
		proxy_authpacket.vp.curtime.vtime_sec = htonl(tv.tv_sec);
		proxy_authpacket.vp.curtime.vtime_nsec = htonl(tv.tv_usec * 1000);
		/* make our digest based on their challenge */
		proxy_authpacket.vp.digest = htonl(crc32_bufs((char *) vph->challenge, password));
		proxy_authpacket.flags = 0;
		if (client && !vph->payload_type) {
			client->mix = 0;
			/* if client is sending options */
			if (recvlen > sizeof(VOTER_PACKET_HEADER)) {
				if (client->ismaster) {
					ast_log(LOG_WARNING,
						"VOTER client master timing source %s attempting to authenticate as a mix client!! (HUH\?\?)\n", client->name);
					authpacket.vp.digest = 0;
					client->heardfrom = 0;
					client->respdigest = 0;
					continue;
				}
				if (buf[sizeof(VOTER_PACKET_HEADER)] & 32) {
					client->mix = 1;
				}
			}
			if (!client->mix && !hasmaster) {
				time(&t);
				if (t >= (client->warntime + CLIENT_WARN_SECS)) {
					client->warntime = t;
					ast_log(LOG_WARNING, "VOTER client %s attempting to authenticate as GPS-timing-based with no master timing source defined!!\n",
						client->name);
				}
				authpacket.vp.digest = 0;
				client->heardfrom = 0;
				client->respdigest = 0;
			} else {
				if (client->ismaster) {
					authpacket.flags |= 2 | 8;
				}
				if (client->doadpcm) {
					authpacket.flags |= 16;
				}
				if (client->mix) {
					authpacket.flags |= 32;
				}
				if (client->nodeemp || (p && p->hostdeemp)) {
					authpacket.flags |= 1;
				}
				if (client->noplfilter) {
					authpacket.flags |= 4;
				}
			}
		}
		/* send them the empty packet to get things started */
		if (isproxy) {
			ast_debug(2, "Sending (proxied) packet challenge %s digest %08x password %s\n", authpacket.vp.challenge,
				ntohl(authpacket.vp.digest), password);
			proxy_authpacket.flags = authpacket.flags;
			proxy_authpacket.vprox.ipaddr = sin.sin_addr.s_addr;
			proxy_authpacket.vprox.port = sin.sin_port;
			proxy_authpacket.vp.payload_type = htons(VOTER_PAYLOAD_PROXY);
			sendto(udp_socket, &proxy_authpacket, sizeof(proxy_authpacket), 0, (struct sockaddr *) &psin, sizeof(psin));
		} else {
			ast_debug(2, "Sending packet challenge %s digest %08x password %s to client %s\n", authpacket.vp.challenge,
				ntohl(authpacket.vp.digest), password, ((client) ? client->name : "UNKNOWN"));
			sendto(udp_socket, &authpacket, sizeof(authpacket), 0, (struct sockaddr *) &sin, sizeof(sin));
		}
		continue;
	}
	ast_mutex_unlock(&voter_lock);
	ast_debug(1, "VOTER: Read thread exited.\n");
	return NULL;
}

/*!
 * \brief Reload Voter driver configuration from disk and apply changes.
 *
 * Parses the configured voter.conf, updates per-instance (pvts) and per-client
 * state, reallocates audio and RSSI buffers as needed, creates or tears down
 * per-instance PMR channels for CTCSS changes, and ensures client digests are
 * unique and valid. The global voter_lock is held while modifying shared state.
 *
 * \retval  0 Success — configuration loaded and applied.
 * \retval -1 Failure — configuration load or allocation error; existing state is left unchanged where possible.
 */
static int reload(void)
{
	struct ast_flags zeroflag = { 0 };
	int i, n, instance_buflen, buflen, oldtoctype, oldlevel;
	char *val, *ctg, *cp, *cp1, *cp2, *strs[40], newclient, data[100], oldctcss[100];
	struct voter_pvt *p;
	struct voter_client *client, *client1;
	struct ast_config *cfg = NULL;
	struct ast_variable *v;

	ast_mutex_lock(&voter_lock);
	for (client = clients; client; client = client->next) {
		client->reload = 0;
		client->old_buflen = client->buflen;
	}

	if (!(cfg = ast_config_load(config, zeroflag))) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		ast_mutex_unlock(&voter_lock);
		return -1;
	}

	val = (char *) ast_variable_retrieve(cfg, "general", "password");
	if (val) {
		ast_copy_string(password, val, sizeof(password) - 1);
	} else {
		password[0] = 0;
	}

	val = (char *) ast_variable_retrieve(cfg, "general", "context");
	if (val) {
		ast_copy_string(context, val, sizeof(context) - 1);
	} else {
		context[0] = 0;
	}

	val = (char *) ast_variable_retrieve(cfg, "general", "buflen");
	if (val) {
		buflen = strtoul(val, NULL, 0) * 8;
	} else {
		buflen = DEFAULT_BUFLEN * 8;
	}

	val = (char *) ast_variable_retrieve(cfg, "general", "sanity");
	if (val) {
		check_client_sanity = ast_true(val);
	} else {
		check_client_sanity = 1;
	}

	val = (char *) ast_variable_retrieve(cfg, "general", "puckit");
	if (val) {
		puckit = ast_true(val);
	} else {
		puckit = 0;
	}

	if (buflen < (FRAME_SIZE * 2)) {
		buflen = FRAME_SIZE * 2;
	}

	for (p = pvts; p; p = p->next) {
		oldctcss[0] = 0;
		strcpy(oldctcss, p->txctcssfreq);
		sprintf(data, "%d", p->nodenum);
		if (ast_variable_browse(cfg, data) == NULL) {
			continue;
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "linger");
		if (val) {
			p->linger = atoi(val);
		} else {
			p->linger = DEFAULT_LINGER;
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "plfilter");
		if (val) {
			p->plfilter = ast_true(val);
		} else {
			p->plfilter = 0;
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "hostdeemp");
		if (val) {
			p->hostdeemp = ast_true(val);
		} else {
			p->hostdeemp = 0;
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "duplex");
		if (val) {
			p->duplex = ast_true(val);
		} else {
			p->duplex = 1;
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "mixminus");
		if (val) {
			p->mixminus = ast_true(val);
		} else {
			p->mixminus = 0;
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "streams");
		if (p->nstreams && p->streams[0]) {
			ast_free(p->streams[0]);
		}
		p->nstreams = 0;
		if (val) {
			cp = ast_strdup(val);
			p->nstreams = finddelim(cp, p->streams, ARRAY_LEN(p->streams));
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "txctcss");
		if (val) {
			ast_copy_string(p->txctcssfreq, val, sizeof(p->txctcssfreq));
		} else {
			p->txctcssfreq[0] = 0;
		}
		oldlevel = p->txctcsslevel;
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "txctcsslevel");
		if (val) {
			p->txctcsslevel = atoi(val);
		} else {
			p->txctcsslevel = 62;
		}
		p->txctcsslevelset = p->txctcsslevel;
		oldtoctype = p->txtoctype;
		p->txtoctype = TOC_NONE;
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "txtoctype");
		if (val) {
			if (!strcasecmp(val, "phase")) {
				p->txtoctype = TOC_PHASE;
			} else if (!strcasecmp(val, "notone")) {
				p->txtoctype = TOC_NOTONE;
			}
		}
		p->nthresholds = 0;
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "thresholds");
		if (val) {
			cp = ast_strdup(val);
			p->nthresholds = finddelim(cp, strs, MIN(ARRAY_LEN(strs), ARRAY_LEN(p->linger_thresh)));
			for (i = 0; i < p->nthresholds; i++) {
				cp1 = strchr(strs[i], '=');
				p->linger_thresh[i] = p->linger;
				if (cp1) {
					*cp1 = 0;
					cp2 = strchr(cp1 + 1, ':');
					if (cp2) {
						*cp2 = 0;
						if (cp2[1]) {
							p->linger_thresh[i] = (uint16_t) atoi(cp2 + 1);
						}
					}
					if (cp1[1]) {
						p->count_thresh[i] = (uint16_t) atoi(cp1 + 1);
					}
				}
				p->rssi_thresh[i] = (uint8_t) atoi(strs[i]);
			}
			ast_free(cp);
		}
		val = (char *) ast_variable_retrieve(cfg, (char *) data, "gtxgain");
		if (!val) {
			val = DEFAULT_GTXGAIN;
		}
		p->gtxgain = pow(10.0, atof(val) / 20.0);
		/* if new CTCSS freq */
		if (strcmp(oldctcss, p->txctcssfreq) || (oldtoctype != p->txtoctype) || (oldlevel != p->txctcsslevel)) {

			t_pmr_chan tChan;

			if (p->pmrChan) {
				destroyPmrChannel(p->pmrChan);
			}
			p->pmrChan = 0;
			if (p->txctcssfreq[0]) {

				memset(&tChan, 0, sizeof(t_pmr_chan));

				tChan.pTxCodeDefault = p->txctcssfreq;
				tChan.pTxCodeSrc = p->txctcssfreq;
				tChan.pRxCodeSrc = p->txctcssfreq;
				tChan.txMod = 2;
				tChan.txMixA = TX_OUT_COMPOSITE;
				tChan.b.txboost = 1;
				p->pmrChan = createPmrChannel(&tChan, FRAME_SIZE);
				p->pmrChan->radioDuplex = 1;	//o->radioduplex;
				p->pmrChan->b.loopback = 0;
				p->pmrChan->b.radioactive = 1;
				p->pmrChan->txrxblankingtime = 0;
				p->pmrChan->rxCpuSaver = 0;
				p->pmrChan->txCpuSaver = 0;
				*(p->pmrChan->prxSquelchAdjust) = 0;
				*(p->pmrChan->prxVoiceAdjust) = 0;
				*(p->pmrChan->prxCtcssAdjust) = 0;
				p->pmrChan->rxCtcss->relax = 0;
				p->pmrChan->txTocType = p->txtoctype;
				p->pmrChan->spsTxOutA->outputGain = 250;
				*p->pmrChan->ptxCtcssAdjust = p->txctcsslevel;
				p->pmrChan->pTxCodeDefault = p->txctcssfreq;
				p->pmrChan->pTxCodeSrc = p->txctcssfreq;
			}
		}
	}
	hasmaster = 0;
	ctg = NULL;
	while ((ctg = ast_category_browse(cfg, ctg)) != NULL) {
		if (ctg == NULL) {
			continue;
		}
		if (!strcmp(ctg, "general")) {
			continue;
		}
		val = (char *) ast_variable_retrieve(cfg, ctg, "buflen");
		if (val) {
			instance_buflen = strtoul(val, NULL, 0) * 8;
		} else {
			instance_buflen = buflen;
		}
		if (instance_buflen < (FRAME_SIZE * 2)) {
			instance_buflen = FRAME_SIZE * 2;
		}
		for (v = ast_variable_browse(cfg, ctg); v; v = v->next) {
			if (!strcmp(v->name, "txctcsslevel")) {
				continue;
			}
			if (!strcmp(v->name, "txctcss")) {
				continue;
			}
			if (!strcmp(v->name, "txtoctype")) {
				continue;
			}
			if (!strcmp(v->name, "streams")) {
				continue;
			}
			if (!strcmp(v->name, "thresholds")) {
				continue;
			}
			if (!strcmp(v->name, "plfilter")) {
				continue;
			}
			if (!strcmp(v->name, "gtxgain")) {
				continue;
			}
			if (!strcmp(v->name, "hostdeemp")) {
				continue;
			}
			if (!strcmp(v->name, "duplex")) {
				continue;
			}
			if (!strcmp(v->name, "mixminus")) {
				continue;
			}
			if (!strcmp(v->name, "linger")) {
				continue;
			}
			if (!strcmp(v->name, "primary")) {
				continue;
			}
			if (!strcmp(v->name, "isprimary")) {
				continue;
			}
			if (!strncasecmp(v->name, "transmit", 8)) {
				continue;
			}
			if (!strncasecmp(v->name, "master", 6)) {
				continue;
			}
			if (!strncasecmp(v->name, "adpcm", 5)) {
				continue;
			}
			if (!strncasecmp(v->name, "nulaw", 5)) {
				continue;
			}
			if (!strncasecmp(v->name, "gpsid", 5)) {
				continue;
			}
			if (!strncasecmp(v->name, "buflen", 6)) {
				continue;
			}
			if (!strncasecmp(v->name, "nodeemp", 7)) {
				continue;
			}
			if (!strncasecmp(v->name, "hostdeemp", 9)) {
				continue;
			}
			if (!strncasecmp(v->name, "noplfilter", 10)) {
				continue;
			}
			if (!strncasecmp(v->name, "prio", 4)) {
				continue;
			}
			cp = ast_strdup(v->value);
			if (!cp) {
				close(udp_socket);
				ast_config_destroy(cfg);
				ast_mutex_unlock(&voter_lock);
				return -1;
			}
			n = finddelim(cp, strs, ARRAY_LEN(strs));
			if (n < 1) {
				continue;
			}
			/* see if we "know" this client already */
			for (client = clients; client; client = client->next) {
				/* if this is the one whose digest matches one currently being looked at */
				if (client->digest == crc32_bufs(challenge, strs[0])) {
					/* if has moved to another instance, free this one, and treat as new */
					if (client->nodenum != strtoul(ctg, NULL, 0)) {
						client->reload = 0;
						client = NULL;
					}
					break;
				}
			}
			newclient = 0;
			/* if a new one, alloc its space */
			if (!client) {
				client = ast_calloc(1, sizeof(struct voter_client));
				if (!client) {
					ast_free(cp);
					close(udp_socket);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&voter_lock);
					return -1;
				}
				client->prio_override = -2;
				ast_copy_string(client->name, v->name, VOTER_NAME_LEN - 1);
				newclient = 1;
			}
			client->reload = 1;
			client->buflen = instance_buflen;
			client->nodenum = strtoul(ctg, NULL, 0);
			client->totransmit = 0;
			client->doadpcm = 0;
			client->donulaw = 0;
			client->nodeemp = 0;
			client->mix = 0;
			client->noplfilter = 0;
			client->prio = 0;
			client->gpsid = 0;
			for (i = 1; i < n; i++) {
				if (!strcasecmp(strs[i], "transmit")) {
					client->totransmit = 1;
				} else if (!strcasecmp(strs[i], "master")) {
					client->ismaster = 1;
					hasmaster = 1;
				} else if (!strcasecmp(strs[i], "adpcm")) {
					client->doadpcm = 1;
				} else if (!strcasecmp(strs[i], "nulaw")) {
					client->donulaw = 1;
				} else if (!strcasecmp(strs[i], "nodeemp")) {
					client->nodeemp = 1;
				} else if (!strcasecmp(strs[i], "noplfilter")) {
					client->noplfilter = 1;
				} else if (!strncasecmp(strs[i], "gpsid", 5)) {
					cp1 = strchr(strs[i], '=');
					if (!cp1) {
						client->gpsid = ast_strdup("");
					} else {
						client->gpsid = ast_strdup(cp1);
						*client->gpsid = '_';
					}
				} else if (!strncasecmp(strs[i], "prio", 4)) {
					cp1 = strchr(strs[i], '=');
					if (cp1) {
						client->prio = strtoul(cp1 + 1, NULL, 0);
						if (client->prio < -1) {
							client->prio = 0;
						}
					}
				}
			}
			client->buflen -= client->buflen % (FRAME_SIZE * 2);
			client->digest = crc32_bufs(challenge, strs[0]);
			ast_copy_string(client->pswd, strs[0], sizeof(client->pswd) - 1);
			ast_free(cp);
			if (client->old_buflen && (client->buflen != client->old_buflen)) {
				client->drainindex = 0;
			}
			if (client->audio && client->old_buflen && (client->buflen != client->old_buflen)) {
				client->audio = ast_realloc(client->audio, client->buflen);
				if (!client->audio) {
					close(udp_socket);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&voter_lock);
					return -1;
				}
				memset(client->audio, 0xff, client->buflen);
			} else if (!client->audio) {
				client->audio = ast_malloc(client->buflen);
				if (!client->audio) {
					close(udp_socket);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&voter_lock);
					return -1;
				}
				memset(client->audio, 0xff, client->buflen);
			}
			if (client->rssi && client->old_buflen && (client->buflen != client->old_buflen)) {
				client->rssi = ast_realloc(client->rssi, client->buflen);
				if (!client->rssi) {
					close(udp_socket);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&voter_lock);
					return -1;
				}
				memset(client->rssi, 0, client->buflen);
			} else if (!client->rssi) {
				client->rssi = ast_calloc(1, client->buflen);
				if (!client->rssi) {
					close(udp_socket);
					ast_config_destroy(cfg);
					ast_mutex_unlock(&voter_lock);
					return -1;
				}
			}
			/* if a new client, add it into list */
			if (newclient) {
				if (clients == NULL) {
					clients = client;
				}
				else {
					for (client1 = clients; client1->next; client1 = client1->next);
					client1->next = client;
				}
			}
		}
	}
	ast_config_destroy(cfg);
	for (client = clients; client; client = client->next) {
		if (!client->reload) {
			continue;
		}
		if (client->digest == 0) {
			ast_log(LOG_ERROR, "Can not load chan_voter -- VOTER client %s has invalid authentication digest (can not be 0)!!!\n",
				client->name);
			ast_mutex_unlock(&voter_lock);
			return -1;
		}
		for (client1 = clients; client1; client1 = client1->next) {
			if (!client1->reload) {
				continue;
			}
			if (client == client1) {
				continue;
			}
			if (client->digest == client1->digest) {
				ast_log(LOG_ERROR, "Can not load chan_voter -- VOTER clients %s and %s have same authentication digest!!!\n",
					client->name, client1->name);
				ast_mutex_unlock(&voter_lock);
				return -1;
			}
		}
	}
	/* remove all the clients that are no longer in the config */
	for (client = clients; client; client = client->next) {
		if (client->reload) {
			continue;
		}
		if (client->audio) {
			ast_free(client->audio);
		}
		if (client->rssi) {
			ast_free(client->rssi);
		}
		if (client->gpsid) {
			ast_free(client->gpsid);
		}
		for (client1 = clients; client1; client1 = client1->next) {
			if (client1->next == client) {
				break;
			}
		}
		if (client1) {
			client1->next = client->next;
		} else {
			clients = NULL;
		}
		ast_free(client);
		client = clients;
	}
	ast_mutex_unlock(&voter_lock);
	return 0;
}

static int unload_module(void)
{
	run_forever = 0;
	ast_cli_unregister_multiple(voter_cli, ARRAY_LEN(voter_cli));
	ast_manager_unregister("VoterStatus");

	/* First, take us out of the channel loop */
	pthread_join(voter_timer_thread, NULL);
	pthread_join(voter_reader_thread, NULL);
	ast_channel_unregister(&voter_tech);

	if (nullfd != -1) {
		close(nullfd);
	}
	if (voter_thread_timer) {
		ast_timer_close(voter_thread_timer);
		voter_thread_timer = NULL;
	}

	ao2_ref(voter_tech.capabilities, -1);
	voter_tech.capabilities = NULL;
	return 0;
}

/*!
 * \brief Initialize and register the Voter channel module and its runtime resources.
 *
 * Sets up UDP socket and bind address/port from configuration, opens a periodic
 * timer, loads runtime configuration, registers CLI and manager hooks, creates
 * reader and timer threads, allocates channel format capabilities, and registers
 * the channel driver so the Voter channel becomes available to Asterisk.
 *
 * \return 0 on success; non-zero on failure (typically AST_MODULE_LOAD_DECLINE for
 *         module load errors, or 1 if the configuration could not be loaded).
 */
static int load_module(void)
{
	struct sockaddr_in sin;
	int i, utos;
	struct ast_config *cfg = NULL;
	const char *val;
	struct ast_flags zeroflag = { 0 };

	run_forever = 1;

	snprintf(challenge, sizeof(challenge), "%ld", ast_random());
	hasmaster = 0;

	if (!(cfg = ast_config_load(config, zeroflag))) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		return 1;
	}

	if ((udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		ast_log(LOG_ERROR, "Unable to create new socket for VOTER audio connection\n");
		ast_config_destroy(cfg);
		return AST_MODULE_LOAD_DECLINE;
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	val = ast_variable_retrieve(cfg, "general", "port");
	if (val) {
		listen_port = (uint16_t) strtoul(val, NULL, 0);
	}

	val = ast_variable_retrieve(cfg, "general", "utos");
	utos = val ? ast_true(val) : 0;
	val = ast_variable_retrieve(cfg, "general", "bindaddr");
	sin.sin_addr.s_addr = val ? inet_addr(val) : htonl(INADDR_ANY);
	sin.sin_port = htons(listen_port);

	ast_config_destroy(cfg);

	if (bind(udp_socket, &sin, sizeof(sin)) == -1) {
		ast_log(LOG_ERROR, "Unable to bind port for VOTER audio connection: %s\n", strerror(errno));
		close(udp_socket);
		return AST_MODULE_LOAD_DECLINE;
	}

	i = fcntl(udp_socket, F_GETFL, 0);	/* Get socket flags */
	fcntl(udp_socket, F_SETFL, i | O_NONBLOCK);	/* Add non-blocking flag */

	if (utos) {
		i = 0xc0;
		if (setsockopt(udp_socket, IPPROTO_IP, IP_TOS, &i, sizeof(i))) {
			ast_log(LOG_ERROR, "Can't setsockopt: IP_TOS: %s\n", strerror(errno));
			close(udp_socket);
			return AST_MODULE_LOAD_DECLINE;
		}
	}

	voter_thread_timer = ast_timer_open();
	if (!voter_thread_timer) {
		ast_log(LOG_ERROR, "Failed to open timer\n");
		close(udp_socket);
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_timer_set_rate(voter_thread_timer, 50); /* 50 ticks per second = every 20ms */

	if (reload()) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_cli_register_multiple(voter_cli, ARRAY_LEN(voter_cli));

	ast_manager_register("VoterStatus", 0, manager_voter_status, "Return Voter instance(s) status");
	ast_pthread_create(&voter_reader_thread, NULL, voter_reader, NULL);
	ast_pthread_create(&voter_timer_thread, NULL, voter_timer, NULL);

	if (!(voter_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		ast_timer_close(voter_thread_timer);
		close(udp_socket);
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append(voter_tech.capabilities, ast_format_slin, 0);

	/* Make sure we can register our channel type */
	if (ast_channel_register(&voter_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		ast_timer_close(voter_thread_timer);
		close(udp_socket);
		return AST_MODULE_LOAD_DECLINE;
	}
	nullfd = open("/dev/null", O_RDWR);
	if (nullfd < 0) {
		ast_log(LOG_ERROR, "Failed to open null fd: %s\n", strerror(errno));
	}
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Voter Radio Channel Driver", .support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module, .unload = unload_module, .reload = reload, );