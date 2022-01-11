/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2002-2014, Jim Dixon, WB6NIL
 *
 * Jim Dixon, WB6NIL <jim@lambdatel.com>
 * Serious contributions by Steve RoDgers, WA6ZFT <hwstar@rodgers.sdcoxmail.com>
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
 *
 * -------------------------------------
 * Notes on app_rpt.c
 * -------------------------------------
 * By: Stacy Olivas, KG7QIN <kg7qin@arrl.net> - 20 March 2017
 * This application, the heart of the AllStar network and using asterisk as a repeater,
 * is largely undocumented code.  It uses a multi-threaded approach to fulfilling its functions
 * and can be quite a chore to follow for debugging.
 *
 * The entry point in the code , rpt_exec, is called by the main pbx call handing routine.
 * The code handles the initial setup and then passes the call/connection off to
 * the threaded routines, which do the actual work <behind the scenes> of keeping multiple
 * connections open, passing telemetry, etc.  rpt_master handles the management of the threaded
 * routines used (rpt_master_thread is the p_thread structure).
 *
 * Having gone through this code during an attempt at porting to this Asterisk 1.8, I recommend
 * that anyone who is serious about trying to understand this code, to liberally sprinkle
 * debugging statements throughout it and run it.  The program flow may surprise you.
 *
 * Note that due changes in later versions of asterisk, you cannot simply drop this module into
 * the build tree and expect it to work.  There has been some significant renaming of
 * key variables and structures between 1.4 and later versions of Asterisk.  Additionally,
 * the changes to how the pbx module passes calls off to applications has changed as well,
 * which causes app_rpt to fail without a modification of the base Asterisk code in these
 * later versions.
 * --------------------------------------
 */
/*! \file
 *
 * \brief Radio Repeater / Remote Base program
 *  version 0.332 11/30/2019
 *
 * \author Jim Dixon, WB6NIL <jim@lambdatel.com>
 *
 * \note Serious contributions by Steve RoDgers, WA6ZFT <hwstar@rodgers.sdcoxmail.com>
 * \note contributions by Steven Henke, W9SH, <w9sh@arrl.net>
 * \note contributions by Mike Zingman, N4IRR
 * \note contributions by Steve Zingman, N4IRS
 *
 * \note Allison ducking code by W9SH
 * \ported by Adam KC1KCC
 * \ported by Mike N4IRR
 *
 * See http://www.zapatatelephony.org/app_rpt.html
 *
 *
 * Repeater / Remote Functions:
 * "Simple" Mode:  * - autopatch access, # - autopatch hangup
 * Normal mode:
 * See the function list in rpt.conf (autopatchup, autopatchdn)
 * autopatchup can optionally take comma delimited setting=value pairs:
 *
 *
 * context=string		:	Override default context with "string"
 * dialtime=ms			:	Specify the max number of milliseconds between phone number digits (1000 milliseconds = 1 second)
 * farenddisconnect=1		:	Automatically disconnect when called party hangs up
 * noct=1			:	Don't send repeater courtesy tone during autopatch calls
 * quiet=1			:	Don't send dial tone, or connect messages. Do not send patch down message when called party hangs up
 *
 *
 * Example: 123=autopatchup,dialtime=20000,noct=1,farenddisconnect=1
 *
 *  To send an asterisk (*) while dialing or talking on phone,
 *  use the autopatch acess code.
 *
 *
 * status cmds:
 *
 *  1 - Force ID (global)
 *  2 - Give Time of Day (global)
 *  3 - Give software Version (global)
 *  4 - Give GPS location info
 *  5 - Last (dtmf) user
 *  11 - Force ID (local only)
 *  12 - Give Time of Day (local only)
 *
 * cop (control operator) cmds:
 *
 *  1 - System warm boot
 *  2 - System enable
 *  3 - System disable
 *  4 - Test Tone On/Off
 *  5 - Dump System Variables on Console (debug)
 *  6 - PTT (phone mode only)
 *  7 - Time out timer enable
 *  8 - Time out timer disable
 *  9 - Autopatch enable
 *  10 - Autopatch disable
 *  11 - Link enable
 *  12 - Link disable
 *  13 - Query System State
 *  14 - Change System State
 *  15 - Scheduler Enable
 *  16 - Scheduler Disable
 *  17 - User functions (time, id, etc) enable
 *  18 - User functions (time, id, etc) disable
 *  19 - Select alternate hang timer
 *  20 - Select standard hang timer 
 *  21 - Enable Parrot Mode
 *  22 - Disable Parrot Mode
 *  23 - Birdbath (Current Parrot Cleanup/Flush)
 *  24 - Flush all telemetry
 *  25 - Query last node un-keyed
 *  26 - Query all nodes keyed/unkeyed
 *  27 - Reset DAQ minimum on a pin
 *  28 - Reset DAQ maximum on a pin
 *  30 - Recall Memory Setting in Attached Xcvr
 *  31 - Channel Selector for Parallel Programmed Xcvr
 *  32 - Touchtone pad test: command + Digit string + # to playback all digits pressed
 *  33 - Local Telemetry Output Enable
 *  34 - Local Telemetry Output Disable
 *  35 - Local Telemetry Output on Demand
 *  36 - Foreign Link Local Output Path Enable
 *  37 - Foreign Link Local Output Path Disable
 *  38 - Foreign Link Local Output Path Follows Local Telemetry
 *  39 - Foreign Link Local Output Path on Demand
 *  42 - Echolink announce node # only
 *  43 - Echolink announce node Callsign only
 *  44 - Echolink announce node # & Callsign
 *  45 - Link Activity timer enable
 *  46 - Link Activity timer disable
 *  47 - Reset "Link Config Changed" Flag 
 *  48 - Send Page Tone (Tone specs separated by parenthesis)
 *  49 - Disable incoming connections (control state noice)
 *  50 - Enable incoming connections (control state noicd)
 *  51 - Enable sleep mode
 *  52 - Disable sleep mode
 *  53 - Wake up from sleep
 *  54 - Go to sleep
 *  55 - Parrot Once if parrot mode is disabled
 *  56 - Rx CTCSS Enable
 *  57 - Rx CTCSS Disable
 *  58 - Tx CTCSS On Input only Enable
 *  59 - Tx CTCSS On Input only Disable
 *  60 - Send MDC-1200 Burst (cop,60,type,UnitID[,DestID,SubCode])
 *     Type is 'I' for PttID, 'E' for Emergency, and 'C' for Call 
 *     (SelCall or Alert), or 'SX' for STS (ststus), where X is 0-F.
 *     DestID and subcode are only specified for  the 'C' type message.
 *     UnitID is the local systems UnitID. DestID is the MDC1200 ID of
 *     the radio being called, and the subcodes are as follows: 
 *          Subcode '8205' is Voice Selective Call for Spectra ('Call')
 *          Subcode '8015' is Voice Selective Call for Maxtrac ('SC') or
 *             Astro-Saber('Call')
 *          Subcode '810D' is Call Alert (like Maxtrac 'CA')
 *  61 - Send Message to USB to control GPIO pins (cop,61,GPIO1=0[,GPIO4=1].....)
 *  62 - Send Message to USB to control GPIO pins, quietly (cop,62,GPIO1=0[,GPIO4=1].....)
 *  63 - Send pre-configred APRSTT notification (cop,63,CALL[,OVERLAYCHR])
 *  64 - Send pre-configred APRSTT notification, quietly (cop,64,CALL[,OVERLAYCHR]) 
 *  65 - Send POCSAG page (equipped channel types only)
 *
 * ilink cmds:
 *
 *  1 - Disconnect specified link
 *  2 - Connect specified link -- monitor only
 *  3 - Connect specified link -- tranceive
 *  4 - Enter command mode on specified link
 *  5 - System status
 *  6 - Disconnect all links
 *  7 - Last Node to Key Up
 *  8 - Connect specified link -- local monitor only
 *  9 - Send Text Message (9,<destnodeno or 0 (for all)>,Message Text, etc.
 *  10 - Disconnect all RANGER links (except permalinks)
 *  11 - Disconnect a previously permanently connected link
 *  12 - Permanently connect specified link -- monitor only
 *  13 - Permanently connect specified link -- tranceive
 *  15 - Full system status (all nodes)
 *  16 - Reconnect links disconnected with "disconnect all links"
 *  17 - MDC test (for diag purposes)
 *  18 - Permanently Connect specified link -- local monitor only

 *  200 thru 215 - (Send DTMF 0-9,*,#,A-D) (200=0, 201=1, 210=*, etc)
 *
 * remote cmds:
 *
 *  1 - Recall Memory MM  (*000-*099) (Gets memory from rpt.conf)
 *  2 - Set VFO MMMMM*KKK*O   (Mhz digits, Khz digits, Offset)
 *  3 - Set Rx PL Tone HHH*D*
 *  4 - Set Tx PL Tone HHH*D* (Not currently implemented with DHE RBI-1)
 *  5 - Link Status (long)
 *  6 - Set operating mode M (FM, USB, LSB, AM, etc)
 *  100 - RX PL off (Default)
 *  101 - RX PL On
 *  102 - TX PL Off (Default)
 *  103 - TX PL On
 *  104 - Low Power
 *  105 - Med Power
 *  106 - Hi Power
 *  107 - Bump Down 20 Hz
 *  108 - Bump Down 100 Hz
 *  109 - Bump Down 500 Hz
 *  110 - Bump Up 20 Hz
 *  111 - Bump Up 100 Hz
 *  112 - Bump Up 500 Hz
 *  113 - Scan Down Slow
 *  114 - Scan Down Medium
 *  115 - Scan Down Fast
 *  116 - Scan Up Slow
 *  117 - Scan Up Medium
 *  118 - Scan Up Fast
 *  119 - Transmit allowing auto-tune
 *  140 - Link Status (brief)
 *  200 thru 215 - (Send DTMF 0-9,*,#,A-D) (200=0, 201=1, 210=*, etc)
 *
 * playback cmds:
 *  specify the name of the file to be played globally (for example, 25=rpt/foo)
 *
 * localplay cmds:
 * specify the name of the file to be played locally (for example, 25=rpt/foo)
 *
 * 'duplex' modes:  (defaults to duplex=2)
 *
 * 0 - Only remote links key Tx and no main repeat audio.
 * 1 - Everything other then main Rx keys Tx, no main repeat audio.
 * 2 - Normal mode
 * 3 - Normal except no main repeat audio.
 * 4 - Normal except no main repeat audio during autopatch only
 *
 *
 * "events" subsystem:
 *
 * in the "events" section of the rpt.conf file (if any), the user may 
 * specify actions to take place when ceratin events occur. 
 *
 * It is implemented as acripting, based heavily upon expression evaluation built
 * into Asterisk. Each line of the section contains an action, a type, and variable info.
 * Each line either sets a variable, or executes an action based on a transitional state
 * of a specified (already defined) variable (such as going true, going false, no change, 
 * or getting set initially).
 *
 * The syntax for each line is as follows:
 *
 * action-spec = action|type|var-spec
 *
 * if action is 'V' (for "setting variable"), then action-spec is the variable being set.
 * if action is 'G' (for "setting global variable"), then action-spec is the global variable being set.
 * if action is 'F' (for "function"), then action-spec is a DTMF function to be executed (if result is 1).
 * if action is 'C' (for "rpt command"), then action-spec is a raw rpt command to be executed (if result is 1).
 * if action is 'S' (for "shell command"), then action-spec is a shell command to be executed (if result is 1).
 *
 * if type is 'E' (for "evaluate statement" (or perhaps "equals") ) then the var-spec is a full statement containing
 *    expressions, variables and operators per the expression evaluation built into Asterisk.
 * if type is 'T' (for "going True"), var-spec is a single (already-defined) variable name, and the result will be 1
 *    if the varible has just gone from 0 to 1.
 * if type is 'F' (for "going False"), var-spec is a single (already-defined) variable name, and the result will be 1
 *    if the varible has just gone from 1 to 0.
 * if type is 'N' (for "no change"), var-spec is a single (already-defined) variable name, and the result will be 1
 *    if the varible has not changed.
 *
 * "RANGER" mode configuration:
 * in the node stanza in rpt.conf ONLY the following need be specified for a RANGER node:
 *
 * 
 *
 * [90101]
 *
 * rxchannel=Radio/usb90101
 * functions=rangerfunctions
 * litzcmd=*32008
 *
 * This example given would be for node "90101" (note ALL RANGER nodes MUST begin with '9'.
 * litzcmd specifes the function that LiTZ inititiates to cause a connection
 * "rangerfunctions" in this example, is a function stanza that AT LEAST has the *3 command
 * to connect to another node
 *
 *
*/

/*** MODULEINFO
	<depend>tonezone</depend>
	<depend>curl</depend>
	<defaultenabled>yes</defaultenabled>
 ***/

/* maximum digits in DTMF buffer, and seconds after * for DTMF command timeout */

#define	MAXDTMF 32
#define	MAXMACRO 2048
#define	MAXLINKLIST 5120
#define	LINKLISTTIME 10000
#define	LINKLISTSHORTTIME 200
#define	LINKPOSTTIME 30000
#define	LINKPOSTSHORTTIME 200
#define	KEYPOSTTIME 30000
#define	KEYPOSTSHORTTIME 200
#define	KEYTIMERTIME 250
#define	MACROTIME 100
#define	MACROPTIME 500
#define	DTMF_TIMEOUT 3
#define	KENWOOD_RETRIES 5
#define	TOPKEYN 32
#define	TOPKEYWAIT 3
#define	TOPKEYMAXSTR 30
#define	NEWKEYTIME 2000

#define	AUTHTELLTIME 7000
#define	AUTHTXTIME 1000
#define	AUTHLOGOUTTIME 25000

#define	DISC_TIME 10000  /* report disc after 10 seconds of no connect */
#define	MAX_RETRIES 5
#define	MAX_RETRIES_PERM 1000000000

#define	APRSTT_PIPE "/tmp/aprs_ttfifo"
#define	APRSTT_SUB_PIPE "/tmp/aprs_ttfifo_%s"

#define	REDUNDANT_TX_TIME 2000

#define	RETRY_TIMER_MS 5000

#define	PATCH_DIALPLAN_TIMEOUT 1500

#define	RPT_LOCKOUT_SECS 10

#define MAXPEERSTR 31
#define	MAXREMSTR 15

#define	DELIMCHR ','
#define	QUOTECHR 34

#define	MONITOR_DISK_BLOCKS_PER_MINUTE 38

#define	DEFAULT_MONITOR_MIN_DISK_BLOCKS 10000
#define	DEFAULT_REMOTE_INACT_TIMEOUT (15 * 60)
#define	DEFAULT_REMOTE_TIMEOUT (60 * 60)
#define	DEFAULT_REMOTE_TIMEOUT_WARNING (3 * 60)
#define	DEFAULT_REMOTE_TIMEOUT_WARNING_FREQ 30

#define	DEFAULT_ERXGAIN "-3.0"
#define	DEFAULT_ETXGAIN "3.0"
#define	DEFAULT_TRXGAIN "-3.0"
#define	DEFAULT_TTXGAIN "3.0"
#define	DEFAULT_LINKMONGAIN "0.0"

#define	DEFAULT_EANNMODE 1
#define	DEFAULT_TANNMODE 1

#define	DEFAULT_RXBURST_TIME 250 
#define	DEFAULT_RXBURST_THRESHOLD 16

#define	DEFAULT_SPLIT_2M 600
#define	DEFAULT_SPLIT_70CM 5000

#define TONE_SAMPLE_RATE 8000
#define TONE_SAMPLES_IN_FRAME 160

#define	MAX_TEXTMSG_SIZE 160

#define	MAX_EXTNODEFILES 50
#define	MAX_LOCALLINKNODES 50
#define	MAX_LSTUFF 20

#define ISRANGER(name) (name[0] == '9')

#define	NODES "nodes"
#define	EXTNODES "extnodes"
#define MEMORY "memory"
#define MACRO "macro"
#define	FUNCTIONS "functions"
#define TELEMETRY "telemetry"
#define MORSE "morse"
#define	TONEMACRO "tonemacro"
#define	MDCMACRO "mdcmacro"
#define	DTMFKEYS "dtmfkeys"
#define	FUNCCHAR '*'
#define	ENDCHAR '#'
#define	EXTNODEFILE "/var/lib/asterisk/rpt_extnodes"
#define	NODENAMES "rpt/nodenames"
#define	PARROTFILE "/tmp/parrot_%s_%u"
#define	GPSFILE "/tmp/gps.dat"

#define	GPS_VALID_SECS 60
#define	GPS_UPDATE_SECS 30

#define	PARROTTIME 1000

#define	TELEM_HANG_TIME 120000
#define	LINK_HANG_TIME 120000

#define	DEFAULT_IOBASE 0x378

#define	DEFAULT_CIV_ADDR 0x58

#define	MAXCONNECTTIME 5000

#define MAXNODESTR 300

#define MAXNODELEN 16

#define MAXIDENTLEN 32

#define MAXPATCHCONTEXT 100

#define ACTIONSIZE 32

#define TELEPARAMSIZE 400

#define REM_SCANTIME 100

#define	DTMF_LOCAL_TIME 250
#define	DTMF_LOCAL_STARTTIME 500

#define	IC706_PL_MEMORY_OFFSET 50

#define	VOX_ON_DEBOUNCE_COUNT 3
#define	VOX_OFF_DEBOUNCE_COUNT 20
#define	VOX_MAX_THRESHOLD 10000.0
#define	VOX_MIN_THRESHOLD 3000.0
#define	VOX_TIMEOUT_MS 10000
#define	VOX_RECOVER_MS 2000
#define	SIMPLEX_PATCH_DELAY 25
#define	SIMPLEX_PHONE_DELAY 25

#define	RX_LINGER_TIME 50
#define	RX_LINGER_TIME_IAXKEY 150

#define	REQUIRED_ZAPTEL_VERSION 'A'

#define	STATPOST_PROGRAM "/usr/bin/wget,-q,--output-document=/dev/null,--no-check-certificate"

#define	ALLOW_LOCAL_CHANNELS

#define EL_DB_ROOT "echolink"

#define	DEFAULT_LITZ_TIME 3000
#define	DEFAULT_LITZ_CHAR "0"

/*
 * DAQ subsystem
 */

#define MAX_DAQ_RANGES 16  /* Max number of entries for range() */
#define MAX_DAQ_ENTRIES 10 /* Max number of DAQ devices */
#define MAX_DAQ_NAME 32 /* Max length of a device name */
#define MAX_DAQ_DEV 64 /* Max length of a daq device path */
#define MAX_METER_FILES 10 /* Max number of sound files in a meter def. */
#define DAQ_RX_TIMEOUT 50 /* Receive time out for DAQ subsystem */ 
#define DAQ_ADC_ACQINT 10 /* Acquire interval in sec. for ADC channels */
#define ADC_HIST_TIME 300 /* Time  in sec. to calculate short term avg, high and low peaks from. */
#define ADC_HISTORY_DEPTH ADC_HIST_TIME/DAQ_ADC_ACQINT
 

enum {REM_OFF,REM_MONITOR,REM_TX};

enum {LINKMODE_OFF,LINKMODE_ON,LINKMODE_FOLLOW,LINKMODE_DEMAND,
	LINKMODE_GUI,LINKMODE_PHONE,LINKMODE_ECHOLINK,LINKMODE_TLB};

enum{ID,PROC,TERM,COMPLETE,UNKEY,REMDISC,REMALREADY,REMNOTFOUND,REMGO,
	CONNECTED,CONNFAIL,STATUS,TIMEOUT,ID1, STATS_TIME, PLAYBACK,
	LOCALPLAY, STATS_VERSION, IDTALKOVER, ARB_ALPHA, TEST_TONE, REV_PATCH,
	TAILMSG, MACRO_NOTFOUND, MACRO_BUSY, LASTNODEKEY, FULLSTATUS,
	MEMNOTFOUND, INVFREQ, REMMODE, REMLOGIN, REMXXX, REMSHORTSTATUS,
	REMLONGSTATUS, LOGINREQ, SCAN, SCANSTAT, TUNE, SETREMOTE, TOPKEY,
	TIMEOUT_WARNING, ACT_TIMEOUT_WARNING, LINKUNKEY, UNAUTHTX, PARROT,
	STATS_TIME_LOCAL, VARCMD, LOCUNKEY, METER, USEROUT, PAGE,
	STATS_GPS,STATS_GPS_LEGACY, MDC1200, LASTUSER, REMCOMPLETE, PFXTONE};


enum {REM_SIMPLEX,REM_MINUS,REM_PLUS};

enum {REM_LOWPWR,REM_MEDPWR,REM_HIPWR};

enum {DC_INDETERMINATE, DC_REQ_FLUSH, DC_ERROR, DC_COMPLETE, DC_COMPLETEQUIET, DC_DOKEY};

enum {SOURCE_RPT, SOURCE_LNK, SOURCE_RMT, SOURCE_PHONE, SOURCE_DPHONE, SOURCE_ALT};

enum {DLY_TELEM, DLY_ID, DLY_UNKEY, DLY_CALLTERM, DLY_COMP, DLY_LINKUNKEY, DLY_PARROT, DLY_MDC1200};

enum {REM_MODE_FM,REM_MODE_USB,REM_MODE_LSB,REM_MODE_AM};

enum {HF_SCAN_OFF,HF_SCAN_DOWN_SLOW,HF_SCAN_DOWN_QUICK,
      HF_SCAN_DOWN_FAST,HF_SCAN_UP_SLOW,HF_SCAN_UP_QUICK,HF_SCAN_UP_FAST};

/*
 * DAQ Subsystem
 */

enum{DAQ_PS_IDLE = 0, DAQ_PS_START, DAQ_PS_BUSY, DAQ_PS_IN_MONITOR};
enum{DAQ_CMD_IN, DAQ_CMD_ADC, DAQ_CMD_OUT, DAQ_CMD_PINSET, DAQ_CMD_MONITOR};
enum{DAQ_SUB_CUR = 0, DAQ_SUB_MIN, DAQ_SUB_MAX, DAQ_SUB_STMIN, DAQ_SUB_STMAX, DAQ_SUB_STAVG};
enum{DAQ_PT_INADC = 1, DAQ_PT_INP, DAQ_PT_IN, DAQ_PT_OUT};
enum{DAQ_TYPE_UCHAMELEON};


#define DEFAULT_TELEMDUCKDB "-9"
#define	DEFAULT_RPT_TELEMDEFAULT 1
#define	DEFAULT_RPT_TELEMDYNAMIC 1
#define	DEFAULT_GUI_LINK_MODE LINKMODE_ON
#define	DEFAULT_GUI_LINK_MODE_DYNAMIC 1
#define	DEFAULT_PHONE_LINK_MODE LINKMODE_ON
#define	DEFAULT_PHONE_LINK_MODE_DYNAMIC 1
#define	DEFAULT_ECHOLINK_LINK_MODE LINKMODE_DEMAND
#define	DEFAULT_ECHOLINK_LINK_MODE_DYNAMIC 1
#define	DEFAULT_TLB_LINK_MODE LINKMODE_DEMAND
#define	DEFAULT_TLB_LINK_MODE_DYNAMIC 1

#include "asterisk.h"
#include "../astver.h"

/*
 * Defines for the "old" way to manage module hooks into Asterisk
*/

#ifdef OLD_ASTERISK
#define ast_free free
#define ast_malloc malloc
#define ast_strdup strdup
#endif

#ifdef OLD_ASTERISK
#define	START_DELAY 10
#else
#define	START_DELAY 2
#endif

/*
 * Please change this revision number when you make a edit
 * use the simple format YYMMDD (better for sort)
*/

ASTERISK_FILE_VERSION(__FILE__,"$Revision$")
// ASTERISK_FILE_VERSION(__FILE__,"$Revision$")

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <search.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/io.h>
#include <sys/vfs.h>
#include <math.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fnmatch.h>
#include <curl/curl.h>

#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/callerid.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/features.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/config.h"
#include "asterisk/say.h"
#include "asterisk/localtime.h"
#include "asterisk/cdr.h"
#include "asterisk/options.h"
#include "asterisk/manager.h"
#include "asterisk/astdb.h"
#include "asterisk/app.h"
#include "asterisk/indications.h"
#include <termios.h>

#ifdef	NEW_ASTERISK
struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS };
#endif

/* Un-comment the following to include support decoding of MDC-1200 digital tone
   signalling protocol (using KA6SQG's GPL'ed implementation) */
#include "../allstar/mdc_decode.c"

/* Un-comment the following to include support encoding of MDC-1200 digital tone
   signalling protocol (using KA6SQG's GPL'ed implementation) */
#include "../allstar/mdc_encode.c"

/* Un-comment the following to include support for notch filters in the
   rx audio stream (using Tony Fisher's mknotch (mkfilter) implementation) */
/* #include "rpt_notch.c" */


#ifdef	__RPT_NOTCH
#define	MAXFILTERS 10
#endif

#ifdef	_MDC_ENCODE_H_

#define	MDCGEN_BUFSIZE 2000

struct mdcgen_pvt
{
	mdc_encoder_t *mdc;
	int origwfmt;
	struct ast_frame f;
	char buf[(MDCGEN_BUFSIZE * 2) + AST_FRIENDLY_OFFSET];
	unsigned char cbuf[MDCGEN_BUFSIZE];
} ;

struct mdcparams
{
	char	type[10];
	short	UnitID;
	short	DestID;
	short	subcode;
} ;

static int mdc1200gen(struct ast_channel *chan, char *type, short UnitID, short destID, short subcode);
static int mdc1200gen_start(struct ast_channel *chan, char *type, short UnitID, short destID, short subcode);

#endif


#ifdef	OLD_ASTERISK
int reload();
#else
static int reload(void);
#endif

AST_MUTEX_DEFINE_STATIC(rpt_master_lock);

/* Start a tone-list going */
int ast_playtones_start(struct ast_channel *chan, int vol, const char* tonelist, int interruptible);
/*! Stop the tones from playing */
void ast_playtones_stop(struct ast_channel *chan);

static  char *tdesc = "Radio Repeater / Remote Base  version 2.0.0-beta 03/24/2021";

static char *app = "Rpt";

static char *synopsis = "Radio Repeater/Remote Base Control System";

static char *descrip = 
"  Rpt(nodename[|options][|M][|*]):  \n"
"    Radio Remote Link or Remote Base Link Endpoint Process.\n"
"\n"
"    Not specifying an option puts it in normal endpoint mode (where source\n"
"    IP and nodename are verified).\n"
"\n"
"    Options are as follows:\n"
"\n"
"        X - Normal endpoint mode WITHOUT security check. Only specify\n"
"            this if you have checked security already (like with an IAX2\n"
"            user/password or something).\n"
"\n"
"        Rannounce-string[|timeout[|timeout-destination]] - Amateur Radio\n"
"            Reverse Autopatch. Caller is put on hold, and announcement (as\n"
"            specified by the 'announce-string') is played on radio system.\n"
"            Users of radio system can access autopatch, dial specified\n"
"            code, and pick up call. Announce-string is list of names of\n"
"            recordings, or \"PARKED\" to substitute code for un-parking,\n"
"            or \"NODE\" to substitute node number.\n"
"\n"
"        P - Phone Control mode. This allows a regular phone user to have\n"
"            full control and audio access to the radio system. For the\n"
"            user to have DTMF control, the 'phone_functions' parameter\n"
"            must be specified for the node in 'rpt.conf'. An additional\n"
"            function (cop,6) must be listed so that PTT control is available.\n"
"\n"
"        D - Dumb Phone Control mode. This allows a regular phone user to\n"
"            have full control and audio access to the radio system. In this\n"
"            mode, the PTT is activated for the entire length of the call.\n"
"            For the user to have DTMF control (not generally recomended in\n"
"            this mode), the 'dphone_functions' parameter must be specified\n"
"            for the node in 'rpt.conf'. Otherwise no DTMF control will be\n"
"            available to the phone user.\n"
"\n"
"        S - Simplex Dumb Phone Control mode. This allows a regular phone user\n"
"            audio-only access to the radio system. In this mode, the\n"
"            transmitter is toggled on and off when the phone user presses the\n"
"            funcchar (*) key on the telephone set. In addition, the transmitter\n"
"            will turn off if the endchar (#) key is pressed. When a user first\n"
"            calls in, the transmitter will be off, and the user can listen for\n"
"            radio traffic. When the user wants to transmit, they press the *\n" 
"            key, start talking, then press the * key again or the # key to turn\n"
"            the transmitter off.  No other functions can be executed by the\n"
"            user on the phone when this mode is selected. Note: If your\n"
"            radio system is full-duplex, we recommend using either P or D\n"
"            modes as they provide more flexibility.\n"
"\n"
"        V - Set Asterisk channel variable for specified node ( e.g. rpt(2000|V|foo=bar) ).\n"
"\n"
"        q - Query Status. Sets channel variables and returns + 101 in plan.\n"
"\n"
"        M - Memory Channel Steer as MXX where XX is the memory channel number.\n"
"\n"
"        * - Alt Macro to execute (e.g. *7 for status)\n"
"\n";
;

static int debug = 0;  /* Set this >0 for extra debug output */
static int nrpts = 0;

static char remdtmfstr[] = "0123456789*#ABCD";

enum {TOP_TOP,TOP_WON,WON_BEFREAD,BEFREAD_AFTERREAD};

int max_chan_stat [] = {22000,1000,22000,100,22000,2000,22000};

int nullfd = -1;

#define NRPTSTAT 7

struct rpt_chan_stat
{
	struct timeval last;
	long long total;
	unsigned long count;
	unsigned long largest;
	struct timeval largest_time;
};

char *discstr = "!!DISCONNECT!!";
char *newkeystr = "!NEWKEY!";
char *newkey1str = "!NEWKEY1!";
char *iaxkeystr = "!IAXKEY!";
static char *remote_rig_ft950="ft950";
static char *remote_rig_ft897="ft897";
static char *remote_rig_ft100="ft100";
static char *remote_rig_rbi="rbi";
static char *remote_rig_kenwood="kenwood";
static char *remote_rig_tm271="tm271";
static char *remote_rig_tmd700="tmd700";
static char *remote_rig_ic706="ic706";
static char *remote_rig_xcat="xcat";
static char *remote_rig_rtx150="rtx150";
static char *remote_rig_rtx450="rtx450";
static char *remote_rig_ppp16="ppp16";	  		// parallel port programmable 16 channels

/*
 * DTMF Tones - frequency pairs used to generate them along with the required timings
 */
 
static char* dtmf_tones[] = {
	"!941+1336/200,!0/200",	/* 0 */
	"!697+1209/200,!0/200",	/* 1 */
	"!697+1336/200,!0/200",	/* 2 */
	"!697+1477/200,!0/200",	/* 3 */
	"!770+1209/200,!0/200",	/* 4 */
	"!770+1336/200,!0/200",	/* 5 */
	"!770+1477/200,!0/200",	/* 6 */
	"!852+1209/200,!0/200",	/* 7 */
	"!852+1336/200,!0/200",	/* 8 */
	"!852+1477/200,!0/200",	/* 9 */
	"!697+1633/200,!0/200",	/* A */
	"!770+1633/200,!0/200",	/* B */
	"!852+1633/200,!0/200",	/* C */
	"!941+1633/200,!0/200",	/* D */
	"!941+1209/200,!0/200",	/* * */
	"!941+1477/200,!0/200" };	/* # */

#define ISRIG_RTX(x) ((!strcmp(x,remote_rig_rtx150)) || (!strcmp(x,remote_rig_rtx450)))
#define	IS_XPMR(x) (!strncasecmp(x->rxchanname,"rad",3))


#ifdef	OLD_ASTERISK
STANDARD_LOCAL_USER;
LOCAL_USER_DECL;
#endif

#define	MSWAIT 20
#define	HANGTIME 5000
#define SLEEPTIME 900		/* default # of seconds for of no activity before entering sleep mode */
#define	TOTIME 180000
#define	IDTIME 300000
#define	MAXRPTS 500
#define MAX_STAT_LINKS 32
#define POLITEID 30000
#define FUNCTDELAY 1500

#define	MAXXLAT 20
#define	MAXXLATTIME 3

#define MAX_SYSSTATES 10

#define FT897_SERIAL_DELAY 75000		/* # of usec to wait between some serial commands on FT-897 */
#define FT100_SERIAL_DELAY 75000		/* # of usec to wait between some serial commands on FT-897 */

struct vox {
	float	speech_energy;
	float	noise_energy;
	int	enacount;
	char	voxena;
	char	lastvox;
	int	offdebcnt;
	int	ondebcnt;
} ;

#define	mymax(x,y) ((x > y) ? x : y)
#define	mymin(x,y) ((x < y) ? x : y)

struct rpt_topkey
{
char	node[TOPKEYMAXSTR];
int	timesince;
int	keyed;
} ;

struct rpt_xlat
{
char	funccharseq[MAXXLAT];
char	endcharseq[MAXXLAT];
char	passchars[MAXXLAT];
int	funcindex;
int	endindex;
time_t	lastone;
} ;

static time_t	starttime = 0;

static  pthread_t rpt_master_thread;

/*
 * Structure that holds information regarding app_rpt operation
*/ 
struct rpt;

/*
 * Structure used to manage links 
*/
struct rpt_link
{
	struct rpt_link *next;
	struct rpt_link *prev;
	char	mode;			/* 1 if in tx mode */
	char	isremote;
	char	phonemode;
	char	phonevox;		/* vox the phone */
	char	phonemonitor;		/* no tx or funs for the phone */
	char	name[MAXNODESTR];	/* identifier (routing) string */
	char	lasttx;
	char	lasttx1;
	char	lastrx;
	char	lastrealrx;
	char	lastrx1;
	char	wouldtx;
	char	connected;
	char	hasconnected;
	char	perma;
	char	thisconnected;
	char	outbound;
	char	disced;
	char	killme;
	long	elaptime;
	long	disctime;
	long 	retrytimer;
	long	retxtimer;
	long	rerxtimer;
	long	rxlingertimer;
	int     rssi;
	int	retries;
	int	max_retries;
	int	reconnects;
	long long connecttime;
	struct ast_channel *chan;	
	struct ast_channel *pchan;	
	char	linklist[MAXLINKLIST];
	time_t	linklistreceived;
	long	linklisttimer;
	int	dtmfed;
	int linkunkeytocttimer;
	struct timeval lastlinktv;
	struct	ast_frame *lastf1,*lastf2;
	struct	rpt_chan_stat chan_stat[NRPTSTAT];
	struct vox vox;
	char wasvox;
	int voxtotimer;
	char voxtostate;
	char newkey;
	char iaxkey;
	int linkmode;
	int newkeytimer;
	char gott;
	int		voterlink;      /*!< \brief set if node is defined as a voter rx */
	int		votewinner;		/*!< \brief set if node won the rssi competition */
	time_t	lastkeytime;
	time_t	lastunkeytime;
#ifdef OLD_ASTERISK
        AST_LIST_HEAD(, ast_frame) rxq;
#else
	AST_LIST_HEAD_NOLOCK(, ast_frame) rxq;
#endif
#ifdef OLD_ASTERISK
        AST_LIST_HEAD(, ast_frame) textq;
#else
	AST_LIST_HEAD_NOLOCK(, ast_frame) textq;
#endif
} ;

/*
 * Structure used to manage link status
*/
struct rpt_lstat
{
	struct	rpt_lstat *next;
	struct	rpt_lstat *prev;
	char	peer[MAXPEERSTR];
	char	name[MAXNODESTR];
	char	mode;
	char	outbound;
	int	reconnects;
	char	thisconnected;
	long long	connecttime;
	struct	rpt_chan_stat chan_stat[NRPTSTAT];
} ;

struct rpt_tele
{
	struct rpt_tele *next;
	struct rpt_tele *prev;
	struct rpt *rpt;
	struct ast_channel *chan;
	int	mode;
	struct rpt_link mylink;
	char param[TELEPARAMSIZE];
	union {
		int i;
		void *p;
		char _filler[8];
	} submode;
	unsigned int parrot;
	char killed;
	pthread_t threadid;
} ;

struct function_table_tag
{
	char action[ACTIONSIZE];
	int (*function)(struct rpt *myrpt, char *param, char *digitbuf, 
		int command_source, struct rpt_link *mylink);
} ;

/*
 * Structs used in the DAQ code
 */


struct daq_tx_entry_tag{
	char txbuff[32];
	struct daq_tx_entry_tag *prev;
	struct daq_tx_entry_tag *next;
};

struct daq_pin_entry_tag{
	int num;
	int pintype;
	int command;
	int state;
	int value;
	int valuemax;
	int valuemin;
	int ignorefirstalarm;
	int alarmmask;
	int adcnextupdate;
	int adchistory[ADC_HISTORY_DEPTH];
	char alarmargs[64];
	void (*monexec)(struct daq_pin_entry_tag *);
	struct daq_pin_entry_tag *next;
};


struct daq_entry_tag{
	char name[MAX_DAQ_NAME];
	char dev[MAX_DAQ_DEV];
	int type;
	int fd;
	int active;
	time_t adcacqtime;
	pthread_t threadid;
	ast_mutex_t lock;
	struct daq_tx_entry_tag *txhead;
	struct daq_tx_entry_tag *txtail;
	struct daq_pin_entry_tag *pinhead;
	struct daq_entry_tag *next;
};

struct daq_tag{
	int ndaqs;
	struct daq_entry_tag *hw;
};


/* Used to store the morse code patterns */

struct morse_bits
{		  
	int len;
	int ddcomb;
} ;

struct telem_defaults
{
	char name[20];
	char value[200];
} ;


struct sysstate
{
	char txdisable;
	char totdisable;
	char linkfundisable;
	char autopatchdisable;
	char schedulerdisable;
	char userfundisable;
	char alternatetail;
	char noincomingconns;
	char sleepena;
};

/* rpt cmd support */
#define CMD_DEPTH 1
#define CMD_STATE_IDLE 0
#define CMD_STATE_BUSY 1
#define CMD_STATE_READY 2
#define CMD_STATE_EXECUTING 3

struct rpt_cmd_struct
{
    int state;
    int functionNumber;
    char param[MAXDTMF];
    char digits[MAXDTMF];
    int command_source;
};

typedef struct {
	int v2;
	int v3;
	int chunky;
	int fac;
	int samples;
} goertzel_state_t;

typedef struct {
	int value;
	int power;
} goertzel_result_t;

typedef struct
{
	int freq;
	int block_size;
	int squelch;		/* Remove (squelch) tone */
	goertzel_state_t tone;
	float energy;		/* Accumulated energy of the current block */
	int samples_pending;	/* Samples remain to complete the current block */
	int mute_samples;	/* How many additional samples needs to be muted to suppress already detected tone */

	int hits_required;	/* How many successive blocks with tone we are looking for */
	float threshold;	/* Energy of the tone relative to energy from all other signals to consider a hit */

	int hit_count;		/* How many successive blocks we consider tone present */
	int last_hit;		/* Indicates if the last processed block was a hit */

} tone_detect_state_t;

/*
 * Populate rpt structure with data
*/ 
static struct rpt
{
	ast_mutex_t lock;
	ast_mutex_t remlock;
	ast_mutex_t statpost_lock;
	struct ast_config *cfg;
	char reload;
	char reload1;
	char deleted;
	char xlink;		 							// cross link state of a share repeater/remote radio
	unsigned int statpost_seqno;

	char *name;
	char *rxchanname;
	char *txchanname;
	char remote;
	char *remoterig;
	struct	rpt_chan_stat chan_stat[NRPTSTAT];
	unsigned int scram;
#ifdef	_MDC_DECODE_H_
	mdc_decoder_t *mdc;
#endif

	struct {
		char *ourcontext;
		char *ourcallerid;
		char *acctcode;
		char *ident;
		char *tonezone;
		char simple;
		char *functions;
		char *link_functions;
		char *phone_functions;
		char *dphone_functions;
		char *alt_functions;
		char *nodes;
		char *extnodes;
		char *extnodefiles[MAX_EXTNODEFILES];
		int  extnodefilesn;
		char *patchconnect;
		char *lnkactmacro;
		char *lnkacttimerwarn;
		char *rptinactmacro;
		char *dtmfkeys;
		int hangtime;
		int althangtime;
		int totime;
		int idtime;
		int tailmessagetime;
		int tailsquashedtime;
		int sleeptime;
		int lnkacttime;
		int rptinacttime;
		int duplex;
		int politeid;
		char *tailmessages[500];
		int tailmessagemax;
		char	*memory;
		char	*macro;
		char	*tonemacro;
		char	*mdcmacro;
		char	*startupmacro;
		char	*morse;
		char	*telemetry;
		int iobase;
		char *ioport;
		int iospeed;
		char funcchar;
		char endchar;
		char nobusyout;
		char notelemtx;
		char propagate_dtmf;
		char propagate_phonedtmf;
		char linktolink;
		unsigned char civaddr;
		struct rpt_xlat inxlat;
		struct rpt_xlat outxlat;
		char *archivedir;
		int authlevel;
		char *csstanzaname;
		char *skedstanzaname;
		char *txlimitsstanzaname;
		long monminblocks;
		int remoteinacttimeout;
		int remotetimeout;
		int remotetimeoutwarning;
		int remotetimeoutwarningfreq;
		int sysstate_cur;
		struct sysstate s[MAX_SYSSTATES];
		char parrotmode;
		int parrottime;
		char *rptnode;
		char remote_mars;
		int voxtimeout_ms;
		int voxrecover_ms;
		int simplexpatchdelay;
		int simplexphonedelay;
		char telemdefault;
		char telemdynamic;		
		char lnkactenable;
		char *statpost_program;
		char *statpost_url;
		char linkmode[10];
		char linkmodedynamic[10];
		char *locallist[16];
		int nlocallist;
		char ctgroup[16];
		float telemnomgain;		/*!< \brief nominal gain adjust for telemetry */
		float telemduckgain;	/*!< \brief duck on busy gain adjust for telemetry */
		float erxgain;
		float etxgain;
		float linkmongain;
		char eannmode; /* {NONE,NODE,CALL,BOTH} */
		float trxgain;
		float ttxgain;
		char tannmode; /* {NONE,NODE,CALL,BOTH} */
		char *discpgm;
		char *connpgm;
		char *mdclog;
		char nolocallinkct;
		char nounkeyct;
		char holdofftelem;
		char beaconing;
		int rxburstfreq;
		int rxbursttime;
		int rxburstthreshold;
		int litztime;
		char *litzchar;
		char *litzcmd;
		int itxctcss;		
		int gpsfeet;
		int default_split_2m;
		int default_split_70cm;
                int votertype;                                  /*!< \brief 0 none, 1 repeater, 2 voter rx      */
                int votermode;                                  /*!< \brief 0 none, 1 one shot, 2 continuous    */
                int votermargin;                                /*!< \brief rssi margin to win a vote           */
		int dtmfkey;
		char dias;
		char dusbabek;
		char *outstreamcmd;
		char dopfxtone;
		char *events;
		char *locallinknodes[MAX_LOCALLINKNODES];
		int locallinknodesn;
		char *eloutbound;
		int elke;
		char *aprstt;
		char *lconn[MAX_LSTUFF];
		int nlconn;
		char *ldisc[MAX_LSTUFF];
		int nldisc;
		char *timezone;
	} p;
	struct rpt_link links;
	int unkeytocttimer;
	time_t lastkeyedtime;
	time_t lasttxkeyedtime;
	char keyed;
	char txkeyed;
	char rxchankeyed;					/*!< \brief Receiver RxChan Key State */
	char exttx;
	char localtx;
	char remrx;	
	char remoterx;
	char remotetx;
	char remoteon;
	char remtxfreqok;
	char tounkeyed;
	char tonotify;
	char dtmfbuf[MAXDTMF];
	char macrobuf[MAXMACRO];
	char rem_dtmfbuf[MAXDTMF];
	char lastdtmfcommand[MAXDTMF];
	char cmdnode[50];
	char nowchan;						// channel now
	char waschan;						// channel selected initially or by command
	char bargechan;						// barge in channel
	char macropatch;					// autopatch via tonemacro state
	char parrotstate;
	char parrotonce;
	char linkactivityflag;
	char rptinactwaskeyedflag;
	char lastitx;
	char remsetting;
	char tunetx;
	int  parrottimer;
	unsigned int parrotcnt;
	int telemmode;
	struct ast_channel *rxchannel,*txchannel, *monchannel, *parrotchannel;
	struct ast_channel *pchannel,*txpchannel, *zaprxchannel, *zaptxchannel;
	struct ast_channel *telechannel;  	/*!< \brief pseudo channel between telemetry conference and txconf */
	struct ast_channel *btelechannel;  	/*!< \brief pseudo channel buffer between telemetry conference and txconf */
	struct ast_channel *voxchannel;
	struct ast_frame *lastf1,*lastf2;
	struct rpt_tele tele;
	struct timeval lasttv,curtv;
	pthread_t rpt_call_thread,rpt_thread;
	time_t dtmf_time,rem_dtmf_time,dtmf_time_rem;
	int calldigittimer;
	int tailtimer,totimer,idtimer,txconf,conf,callmode,cidx,scantimer,tmsgtimer,skedtimer,linkactivitytimer,elketimer;
	int mustid,tailid;
	int rptinacttimer;
	int tailevent;
	int teleconf;								/*!< \brief telemetry conference id */
	int telemrefcount;
	int dtmfidx,rem_dtmfidx;
	int dailytxtime,dailykerchunks,totalkerchunks,dailykeyups,totalkeyups,timeouts;
	int totalexecdcommands, dailyexecdcommands;
	long	retxtimer;
	long	rerxtimer;
	long long totaltxtime;
	char mydtmf;
	char exten[AST_MAX_EXTENSION];
	char freq[MAXREMSTR],rxpl[MAXREMSTR],txpl[MAXREMSTR];
	int  splitkhz;
	char offset;
	char powerlevel;
	char txplon;
	char rxplon;
	char remmode;
	char tunerequest;
	char hfscanmode;
	int hfscanstatus;
	char hfscanstop;
	char lastlinknode[MAXNODESTR];
	char savednodes[MAXNODESTR];
	int stopgen;
	int remstopgen;
	char patchfarenddisconnect;
	char patchnoct;
	char patchquiet;
	char patchvoxalways;
	char patchcontext[MAXPATCHCONTEXT];
	char patchexten[AST_MAX_EXTENSION];
	int patchdialtime;
	int macro_longest;
	int phone_longestfunc;
	int alt_longestfunc;
	int dphone_longestfunc;
	int link_longestfunc;
	int longestfunc;
	int longestnode;
	int threadrestarts;		
	int tailmessagen;
	time_t disgorgetime;
	time_t lastthreadrestarttime;
	long	macrotimer;
	char	lastnodewhichkeyedusup[MAXNODESTR];
	int	dtmf_local_timer;
	char	dtmf_local_str[100];
	struct ast_filestream *monstream,*parrotstream;
	char	loginuser[50];
	char	loginlevel[10];
	long	authtelltimer;
	long	authtimer;
	int iofd;
	time_t start_time,last_activity_time;
	char	lasttone[32];
	struct rpt_tele *active_telem;
	struct 	rpt_topkey topkey[TOPKEYN];
	int topkeystate;
	time_t topkeytime;
	int topkeylong;
	struct vox vox;
	char wasvox;
	int voxtotimer;
	char voxtostate;
	int linkposttimer;			
	int keyposttimer;			
	int lastkeytimer;			
	char newkey;
	char iaxkey;
	char inpadtest;
	long rxlingertimer;
	char localoverride;
	char ready;
	char lastrxburst;
	char reallykeyed;
	char dtmfkeyed;
	char dtmfkeybuf[MAXDTMF];
	char localteleminhibit;		/*!< \brief local telemetry inhibit */
	char noduck;				/*!< \brief no ducking of telemetry  */
	char sleepreq;
	char sleep;
	struct rpt_link *voted_link; /*!< \brief last winning link or NULL */
	int  rxrssi;				/*!< \brief rx rssi from the rxchannel */
	int  voted_rssi;			/*!< \brief last winning rssi */
	int  vote_counter;			/*!< \brief count to frame used to vote the winner */
	int  voter_oneshot;
	int  votewinner;
	int  voteremrx;             /* 0 no voters are keyed, 1 at least one voter is keyed */
	char lastdtmfuser[MAXNODESTR];
	char curdtmfuser[MAXNODESTR];
	int  sleeptimer;
	time_t lastgpstime;
	int outstreampipe[2];
	int outstreampid;
	struct ast_channel *remote_webtransceiver;
#ifndef	OLD_ASTERISK
	struct timeval lastdtmftime;
#endif
	tone_detect_state_t burst_tone_state;
#ifdef OLD_ASTERISK
	AST_LIST_HEAD(, ast_frame) txq;
#else
	AST_LIST_HEAD_NOLOCK(, ast_frame) txq;
#endif
#ifdef OLD_ASTERISK
	AST_LIST_HEAD(, ast_frame) rxq;
#else
	AST_LIST_HEAD_NOLOCK(, ast_frame) rxq;
#endif
	char txrealkeyed;
#ifdef	__RPT_NOTCH
	struct rptfilter
	{
		char	desc[100];
		float	x0;
		float	x1;
		float	x2;
		float	y0;
		float	y1;
		float	y2;
		float	gain;
		float	const0;
		float	const1;
		float	const2;
	} filters[MAXFILTERS];
#endif
#ifdef	_MDC_DECODE_H_
	unsigned short lastunit;
	char lastmdc[32];
#endif
	struct rpt_cmd_struct cmdAction;
	struct timeval paging;
	char deferid;
	struct timeval lastlinktime;
} rpt_vars[MAXRPTS];

struct nodelog {
struct nodelog *next;
struct nodelog *prev;
time_t	timestamp;
char archivedir[MAXNODESTR];
char str[MAXNODESTR * 2];
} nodelog;

static int service_scan(struct rpt *myrpt);
static int set_mode_ft897(struct rpt *myrpt, char newmode);
static int set_mode_ft100(struct rpt *myrpt, char newmode);
static int set_mode_ic706(struct rpt *myrpt, char newmode);
static int simple_command_ft897(struct rpt *myrpt, char command);
static int simple_command_ft100(struct rpt *myrpt, unsigned char command, unsigned char p1);
static int setrem(struct rpt *myrpt);
static int setrtx_check(struct rpt *myrpt);
static int channel_revert(struct rpt *myrpt);
static int channel_steer(struct rpt *myrpt, char *data);
static void rpt_telemetry(struct rpt *myrpt,int mode, void *data);
static void rpt_manager_trigger(struct rpt *myrpt, char *event, char *value);

AST_MUTEX_DEFINE_STATIC(nodeloglock);

AST_MUTEX_DEFINE_STATIC(nodelookuplock);

#ifdef	APP_RPT_LOCK_DEBUG

#warning COMPILING WITH LOCK-DEBUGGING ENABLED!!

#define	MAXLOCKTHREAD 100

#define rpt_mutex_lock(x) _rpt_mutex_lock(x,myrpt,__LINE__)
#define rpt_mutex_unlock(x) _rpt_mutex_unlock(x,myrpt,__LINE__)

struct lockthread
{
	pthread_t id;
	int lockcount;
	int lastlock;
	int lastunlock;
} lockthreads[MAXLOCKTHREAD];


struct by_lightning
{
	int line;
	struct timeval tv;
	struct rpt *rpt;
	struct lockthread lockthread;
} lock_ring[32];

int lock_ring_index = 0;

AST_MUTEX_DEFINE_STATIC(locklock);

static struct lockthread *get_lockthread(pthread_t id)
{
int	i;

	for(i = 0; i < MAXLOCKTHREAD; i++)
	{
		if (lockthreads[i].id == id) return(&lockthreads[i]);
	}
	return(NULL);
}

static struct lockthread *put_lockthread(pthread_t id)
{
int	i;

	for(i = 0; i < MAXLOCKTHREAD; i++)
	{
		if (lockthreads[i].id == id)
			return(&lockthreads[i]);
	}
	for(i = 0; i < MAXLOCKTHREAD; i++)
	{
		if (!lockthreads[i].id)
		{
			lockthreads[i].lockcount = 0;
			lockthreads[i].lastlock = 0;
			lockthreads[i].lastunlock = 0;
			lockthreads[i].id = id;
			return(&lockthreads[i]);
		}
	}
	return(NULL);
}

/*
 * Functions related to the threading used in app_rpt dealing with locking
*/

static void rpt_mutex_spew(void)
{
	struct by_lightning lock_ring_copy[32];
	int lock_ring_index_copy;
	int i,j;
	long long diff;
	char a[100];
	struct timeval lasttv;

	ast_mutex_lock(&locklock);
	memcpy(&lock_ring_copy, &lock_ring, sizeof(lock_ring_copy));
	lock_ring_index_copy = lock_ring_index;
	ast_mutex_unlock(&locklock);

	lasttv.tv_sec = lasttv.tv_usec = 0;
	for(i = 0 ; i < 32 ; i++)
	{
		j = (i + lock_ring_index_copy) % 32;
		strftime(a,sizeof(a) - 1,"%m/%d/%Y %H:%M:%S",
			localtime(&lock_ring_copy[j].tv.tv_sec));
		diff = 0;
		if(lasttv.tv_sec)
		{
			diff = (lock_ring_copy[j].tv.tv_sec - lasttv.tv_sec)
				* 1000000;
			diff += (lock_ring_copy[j].tv.tv_usec - lasttv.tv_usec);
		}
		lasttv.tv_sec = lock_ring_copy[j].tv.tv_sec;
		lasttv.tv_usec = lock_ring_copy[j].tv.tv_usec;
		if (!lock_ring_copy[j].tv.tv_sec) continue;
		if (lock_ring_copy[j].line < 0)
		{
			ast_log(LOG_NOTICE,"LOCKDEBUG [#%d] UNLOCK app_rpt.c:%d node %s pid %x diff %lld us at %s.%06d\n",
				i - 31,-lock_ring_copy[j].line,lock_ring_copy[j].rpt->name,(int) lock_ring_copy[j].lockthread.id,diff,a,(int)lock_ring_copy[j].tv.tv_usec);
		}
		else
		{
			ast_log(LOG_NOTICE,"LOCKDEBUG [#%d] LOCK app_rpt.c:%d node %s pid %x diff %lld us at %s.%06d\n",
				i - 31,lock_ring_copy[j].line,lock_ring_copy[j].rpt->name,(int) lock_ring_copy[j].lockthread.id,diff,a,(int)lock_ring_copy[j].tv.tv_usec);
		}
	}
}


static void _rpt_mutex_lock(ast_mutex_t *lockp, struct rpt *myrpt, int line)
{
struct lockthread *t;
pthread_t id;

	id = pthread_self();
	ast_mutex_lock(&locklock);
	t = put_lockthread(id);
	if (!t)
	{
		ast_mutex_unlock(&locklock);
		return;
	}
	if (t->lockcount)
	{
		int lastline = t->lastlock;
		ast_mutex_unlock(&locklock);
		ast_log(LOG_NOTICE,"rpt_mutex_lock: Double lock request line %d node %s pid %x, last lock was line %d\n",line,myrpt->name,(int) t->id,lastline);
		rpt_mutex_spew();
		return;
	}
	t->lastlock = line;
	t->lockcount = 1;
	gettimeofday(&lock_ring[lock_ring_index].tv, NULL);
	lock_ring[lock_ring_index].rpt = myrpt;
	memcpy(&lock_ring[lock_ring_index].lockthread,t,sizeof(struct lockthread));
	lock_ring[lock_ring_index++].line = line;
	if(lock_ring_index == 32)
		lock_ring_index = 0;
	ast_mutex_unlock(&locklock);
	ast_mutex_lock(lockp);
}


static void _rpt_mutex_unlock(ast_mutex_t *lockp, struct rpt *myrpt, int line)
{
struct lockthread *t;
pthread_t id;

	id = pthread_self();
	ast_mutex_lock(&locklock);
	t = put_lockthread(id);
	if (!t)
	{
		ast_mutex_unlock(&locklock);
		return;
	}
	if (!t->lockcount)
	{
		int lastline = t->lastunlock;
		ast_mutex_unlock(&locklock);
		ast_log(LOG_NOTICE,"rpt_mutex_lock: Double un-lock request line %d node %s pid %x, last un-lock was line %d\n",line,myrpt->name,(int) t->id,lastline);
		rpt_mutex_spew();
		return;
	}
	t->lastunlock = line;
	t->lockcount = 0;
	gettimeofday(&lock_ring[lock_ring_index].tv, NULL);
	lock_ring[lock_ring_index].rpt = myrpt;
	memcpy(&lock_ring[lock_ring_index].lockthread,t,sizeof(struct lockthread));
	lock_ring[lock_ring_index++].line = -line;
	if(lock_ring_index == 32)
		lock_ring_index = 0;
	ast_mutex_unlock(&locklock);
	ast_mutex_unlock(lockp);
}

#else  /* APP_RPT_LOCK_DEBUG */

#define rpt_mutex_lock(x) ast_mutex_lock(x)
#define rpt_mutex_unlock(x) ast_mutex_unlock(x)

#endif  /* APP_RPT_LOCK_DEBUG */


#ifdef	_MDC_DECODE_H_
static const char *my_variable_match(const struct ast_config *config, const char *category, const char *variable)
{
	struct ast_variable *v;

	if (category)
	{
		for (v = ast_variable_browse(config, category); v; v = v->next)
		{
			if (!fnmatch(v->name,variable,FNM_CASEFOLD | FNM_NOESCAPE))
				return v->value;
		}

	}
	return NULL;
}
#endif

/* Return 1 if a web transceiver node */
static int iswebtransceiver(struct  rpt_link *l)
{
int	i;

	if (!l) return 0;
	for(i = 0; l->name[i]; i++)
	{
		if (!isdigit(l->name[i])) return 1;
	}
	return 0;
}

/*
* Return 1 if rig is multimode capable
*/

static int multimode_capable(struct rpt *myrpt)
{
	if(!strcmp(myrpt->remoterig, remote_rig_ft897))
		return 1;
	if(!strcmp(myrpt->remoterig, remote_rig_ft100))
		return 1;
	if(!strcmp(myrpt->remoterig, remote_rig_ft950))
		return 1;
	if(!strcmp(myrpt->remoterig, remote_rig_ic706))
		return 1;
	return 0;
}	
/*
* Return 1 if rig is narrow capable
*/

static int narrow_capable(struct rpt *myrpt)
{
	if(!strcmp(myrpt->remoterig, remote_rig_kenwood))
		return 1;
	if(!strcmp(myrpt->remoterig, remote_rig_tmd700))
		return 1;
	if(!strcmp(myrpt->remoterig, remote_rig_tm271))
		return 1;
	return 0;
}	

static char is_paging(struct rpt *myrpt)
{
char	rv = 0;

	if ((!ast_tvzero(myrpt->paging)) &&
		(ast_tvdiff_ms(ast_tvnow(),myrpt->paging) <= 300000)) rv = 1;
	return(rv);
}


static void voxinit_rpt(struct rpt *myrpt,char enable)
{

	myrpt->vox.speech_energy = 0.0;
	myrpt->vox.noise_energy = 0.0;
	myrpt->vox.enacount = 0;
	myrpt->vox.voxena = 0;
	if (!enable) myrpt->vox.voxena = -1;
	myrpt->vox.lastvox = 0;
	myrpt->vox.ondebcnt = VOX_ON_DEBOUNCE_COUNT;
	myrpt->vox.offdebcnt = VOX_OFF_DEBOUNCE_COUNT;
	myrpt->wasvox = 0;
	myrpt->voxtotimer = 0;
	myrpt->voxtostate = 0;
}

static void voxinit_link(struct rpt_link *mylink,char enable)
{

	mylink->vox.speech_energy = 0.0;
	mylink->vox.noise_energy = 0.0;
	mylink->vox.enacount = 0;
	mylink->vox.voxena = 0;
	if (!enable) mylink->vox.voxena = -1;
	mylink->vox.lastvox = 0;
	mylink->vox.ondebcnt = VOX_ON_DEBOUNCE_COUNT;
	mylink->vox.offdebcnt = VOX_OFF_DEBOUNCE_COUNT;
	mylink->wasvox = 0;
	mylink->voxtotimer = 0;
	mylink->voxtostate = 0;
}

static int dovox(struct vox *v,short *buf,int bs)
{

	int i;
	float	esquare = 0.0;
	float	energy = 0.0;
	float	threshold = 0.0;
	
	if (v->voxena < 0) return(v->lastvox);
	for(i = 0; i < bs; i++)
	{
		esquare += (float) buf[i] * (float) buf[i];
	}
	energy = sqrt(esquare);

	if (energy >= v->speech_energy)
		v->speech_energy += (energy - v->speech_energy) / 4;
	else
		v->speech_energy += (energy - v->speech_energy) / 64;

	if (energy >= v->noise_energy)
		v->noise_energy += (energy - v->noise_energy) / 64;
	else
		v->noise_energy += (energy - v->noise_energy) / 4;
	
	if (v->voxena) threshold = v->speech_energy / 8;
	else
	{
		threshold = mymax(v->speech_energy / 16,v->noise_energy * 2);
		threshold = mymin(threshold,VOX_MAX_THRESHOLD);
	}
	threshold = mymax(threshold,VOX_MIN_THRESHOLD);
	if (energy > threshold)
	{
		if (v->voxena) v->noise_energy *= 0.75;
		v->voxena = 1;
	} else 	v->voxena = 0;
	if (v->lastvox != v->voxena)
	{
		if (v->enacount++ >= ((v->lastvox) ? v->offdebcnt : v->ondebcnt))
		{
			v->lastvox = v->voxena;
			v->enacount = 0;
		}
	} else v->enacount = 0;
	return(v->lastvox);



}

/*
 * Multi-thread safe sleep routine
*/
static void rpt_safe_sleep(struct rpt *rpt,struct ast_channel *chan, int ms)
{
	struct ast_frame *f;
	struct ast_channel *cs[2],*w;

	cs[0] = rpt->rxchannel;
	cs[1] = chan;
	while (ms > 0) {
		w = ast_waitfor_n(cs,2,&ms);
		if (!w) break;
		f = ast_read(w);
		if (!f) break;
		if ((w == cs[0]) && (f->frametype != AST_FRAME_VOICE) && (f->frametype != AST_FRAME_NULL))
		{
			ast_queue_frame(rpt->rxchannel,f);
			ast_frfree(f);
			break;
		}
		ast_frfree(f);
	}
	return;
}

/*
 * Routine to forward a "call" from one channel to another
*/

static void rpt_forward(struct ast_channel *chan, char *dialstr, char *nodefrom)
{

struct ast_channel *dest,*w,*cs[2];
struct ast_frame *f;
int	ms;

	dest = ast_request("IAX2", AST_FORMAT_SLINEAR, dialstr ,NULL);
	if (!dest)
	{
		if (ast_safe_sleep(chan,150) == -1) return;
		dest = ast_request("IAX2", AST_FORMAT_SLINEAR, dialstr ,NULL);
		if (!dest)
		{
			ast_log(LOG_ERROR,"Can not create channel for rpt_forward to IAX2/%s\n",dialstr);
			return;
		}
	}
	ast_set_read_format(chan, AST_FORMAT_SLINEAR);
	ast_set_write_format(chan, AST_FORMAT_SLINEAR);
	ast_set_read_format(dest, AST_FORMAT_SLINEAR);
	ast_set_write_format(dest, AST_FORMAT_SLINEAR);
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "rpt forwarding call from %s to %s on %s\n",
			nodefrom,dialstr,dest->name);
	ast_set_callerid(dest,nodefrom,chan->cid.cid_name,nodefrom);
        ast_call(dest,dialstr,999); 
	cs[0] = chan;
	cs[1] = dest;
	for(;;)
	{
		if (ast_check_hangup(chan)) break;
		if (ast_check_hangup(dest)) break;
		ms = 100;
		w = cs[0];
		cs[0] = cs[1];
		cs[1] = w;
		w = ast_waitfor_n(cs,2,&ms);
		if (!w) continue;
		if (w == chan)
		{
			f = ast_read(chan);
			if (!f) break;
			if ((f->frametype == AST_FRAME_CONTROL) &&
			    (f->subclass == AST_CONTROL_HANGUP))
			{
				ast_frfree(f);
				break;
			}
			ast_write(dest,f);
			ast_frfree(f);
		}
		if (w == dest)
		{
			f = ast_read(dest);
			if (!f) break;
			if ((f->frametype == AST_FRAME_CONTROL) &&
			    (f->subclass == AST_CONTROL_HANGUP))
			{
				ast_frfree(f);
				break;
			}
			ast_write(chan,f);
			ast_frfree(f);
		}

	}
	ast_hangup(dest);
	return;
}


/*
* CLI extensions
*/

/* Debug mode */
static int rpt_do_debug(int fd, int argc, char *argv[]);
static int rpt_do_dump(int fd, int argc, char *argv[]);
static int rpt_do_stats(int fd, int argc, char *argv[]);
static int rpt_do_lstats(int fd, int argc, char *argv[]);
static int rpt_do_nodes(int fd, int argc, char *argv[]);
static int rpt_do_xnode(int fd, int argc, char *argv[]);
static int rpt_do_local_nodes(int fd, int argc, char *argv[]);
static int rpt_do_reload(int fd, int argc, char *argv[]);
static int rpt_do_restart(int fd, int argc, char *argv[]);
static int rpt_do_playback(int fd, int argc, char *argv[]);
static int rpt_do_localplay(int fd, int argc, char *argv[]);
static int rpt_do_sendall(int fd, int argc, char *argv[]);
static int rpt_do_sendtext(int fd, int argc, char *argv[]);
static int rpt_do_fun(int fd, int argc, char *argv[]);
static int rpt_do_fun1(int fd, int argc, char *argv[]);
static int rpt_do_cmd(int fd, int argc, char *argv[]);
static int rpt_do_setvar(int fd, int argc, char *argv[]);
static int rpt_do_showvars(int fd, int argc, char *argv[]);
static int rpt_do_page(int fd, int argc, char *argv[]);
static int rpt_do_lookup(int fd, int argc, char *argv[]);

static char debug_usage[] =
"Usage: rpt debug level {0-7}\n"
"       Enables debug messages in app_rpt\n";

static char dump_usage[] =
"Usage: rpt dump <nodename>\n"
"       Dumps struct debug info to log\n";

static char dump_stats[] =
"Usage: rpt stats <nodename>\n"
"       Dumps node statistics to console\n";

static char dump_lstats[] =
"Usage: rpt lstats <nodename>\n"
"       Dumps link statistics to console\n";

static char dump_nodes[] =
"Usage: rpt nodes <nodename>\n"
"       Dumps a list of directly and indirectly connected nodes to the console\n";

static char dump_xnode[] =
"Usage: rpt xnode <nodename>\n"
"       Dumps extended node info to the console\n";

static char usage_local_nodes[] =
"Usage: rpt localnodes\n"
"       Dumps a list of the locally configured node numbers to the console.\n";

static char reload_usage[] =
"Usage: rpt reload\n"
"       Reloads app_rpt running config parameters\n";

static char restart_usage[] =
"Usage: rpt restart\n"
"       Restarts app_rpt\n";

static char playback_usage[] =
"Usage: rpt playback <nodename> <sound_file_base_name>\n"
"       Send an Audio File to a node, send to all other connected nodes (global)\n";

static char localplay_usage[] =
"Usage: rpt localplay <nodename> <sound_file_base_name>\n"
"       Send an Audio File to a node, do not send to other connected nodes (local)\n";


static char sendtext_usage[] =
"Usage: rpt sendtext <nodename> <destnodename> <Text Message>\n"
"       Send a Text message to a specified node\n";

static char sendall_usage[] =
"Usage: rpt sendall <nodename> <Text Message>\n"
"       Send a Text message to all connected nodes\n";


static char fun_usage[] =
"Usage: rpt fun <nodename> <command>\n"
"       Send a DTMF function to a node\n";

static char cmd_usage[] =
"Usage: rpt cmd <nodename> <cmd-name> <cmd-index> <cmd-args>\n"
"       Send a command to a node.\n        i.e. rpt cmd 2000 ilink 3 2001\n";

static char setvar_usage[] =
"Usage: rpt setvar <nodename> <name=value> [<name=value>...]\n"
"       Set an Asterisk channel variable for a node.\nNote: variable names are case-sensitive.\n";

static char showvars_usage[] =
"Usage: rpt showvars <nodename>\n"
"       Display all the Asterisk channel variables for a node.\n";

static char page_usage[] =
"Usage: rpt page <nodename> <baud> <capcode> <[ANT]Text....>\n"
"       Send an page to a user on a node, specifying capcode and type/text\n";

static char lookup_usage[] = 
"Usage rpt lookup <nodename>\n"
"      Look up nodes on the allstar network\n";


#ifndef	NEW_ASTERISK

static struct ast_cli_entry  cli_debug =
        { { "rpt", "debug", "level" }, rpt_do_debug, 
		"Enable app_rpt debugging", debug_usage };

static struct ast_cli_entry  cli_dump =
        { { "rpt", "dump" }, rpt_do_dump,
		"Dump app_rpt structs for debugging", dump_usage };

static struct ast_cli_entry  cli_stats =
        { { "rpt", "stats" }, rpt_do_stats,
		"Dump node statistics", dump_stats };

static struct ast_cli_entry  cli_nodes =
        { { "rpt", "nodes" }, rpt_do_nodes,
		"Dump node list", dump_nodes };

static struct ast_cli_entry  cli_xnode =
        { { "rpt", "xnode" }, rpt_do_xnode,
		"Dump extended info", dump_xnode };

static struct ast_cli_entry  cli_local_nodes =
        { { "rpt", "localnodes" }, rpt_do_local_nodes,
		"Dump list of local node numbers", usage_local_nodes };

static struct ast_cli_entry  cli_lstats =
        { { "rpt", "lstats" }, rpt_do_lstats,
		"Dump link statistics", dump_lstats };

static struct ast_cli_entry  cli_reload =
        { { "rpt", "reload" }, rpt_do_reload,
		"Reload app_rpt config", reload_usage };

static struct ast_cli_entry  cli_restart =
        { { "rpt", "restart" }, rpt_do_restart,
		"Restart app_rpt", restart_usage };

static struct ast_cli_entry  cli_playback =
        { { "rpt", "playback" }, rpt_do_playback,
		"Play Back an Audio File Globally", playback_usage };

static struct ast_cli_entry  cli_localplay =
        { { "rpt", "localplay" }, rpt_do_localplay,
                "Play Back an Audio File Locally", localplay_usage };

static struct ast_cli_entry  cli_sendtext =
        { { "rpt", "sendtext" }, rpt_do_sendtext,
                "Send a Text message to specific node", sendtext_usage };


static struct ast_cli_entry  cli_sendall =
        { { "rpt", "sendall" }, rpt_do_sendall,
                "Send a Text message to all", sendall_usage };


static struct ast_cli_entry  cli_fun =
        { { "rpt", "fun" }, rpt_do_fun,
		"Execute a DTMF function", fun_usage };

static struct ast_cli_entry  cli_fun1 =
        { { "rpt", "fun1" }, rpt_do_fun1,
		"Execute a DTMF function", fun_usage };

static struct ast_cli_entry  cli_cmd =
        { { "rpt", "cmd" }, rpt_do_cmd,
		"Execute a DTMF function", cmd_usage };

static struct ast_cli_entry  cli_setvar =
        { { "rpt", "setvar" }, rpt_do_setvar,
		"Set an Asterisk channel variable", setvar_usage };

static struct ast_cli_entry  cli_showvars =
        { { "rpt", "showvars" }, rpt_do_showvars,
		"Display Asterisk channel variables", showvars_usage };

static struct ast_cli_entry  cli_page =
        { { "rpt", "page" }, rpt_do_page,
		"Page a user on a node", page_usage };

static struct ast_cli_entry cli_lookup =
	{ { "rpt", "lookup" }, rpt_do_lookup,
		"Look up allstar nodes", lookup_usage };

#endif

/*
* Telemetry defaults
*/


static struct telem_defaults tele_defs[] = {
	{"ct1","|t(350,0,100,3072)(500,0,100,3072)(660,0,100,3072)"},
	{"ct2","|t(660,880,150,3072)"},
	{"ct3","|t(440,0,150,3072)"},
	{"ct4","|t(550,0,150,3072)"},
	{"ct5","|t(660,0,150,3072)"},
	{"ct6","|t(880,0,150,3072)"},
	{"ct7","|t(660,440,150,3072)"},
	{"ct8","|t(700,1100,150,3072)"},
	{"ranger","|t(1800,0,60,3072)(0,0,50,0)(1800,0,60,3072)(0,0,50,0)(1800,0,60,3072)(0,0,50,0)(1800,0,60,3072)(0,0,50,0)(1800,0,60,3072)(0,0,50,0)(1800,0,60,3072)(0,0,150,0)"},
	{"remotemon","|t(1600,0,75,2048)"},
	{"remotetx","|t(2000,0,75,2048)(0,0,75,0)(1600,0,75,2048)"},
	{"cmdmode","|t(900,904,200,2048)"},
	{"functcomplete","|t(1000,0,100,2048)(0,0,100,0)(1000,0,100,2048)"},
	{"remcomplete","|t(650,0,100,2048)(0,0,100,0)(650,0,100,2048)(0,0,100,0)(650,0,100,2048)"},
	{"pfxtone","|t(350,440,30000,3072)"}
} ;

static inline void goertzel_sample(goertzel_state_t *s, short sample)
{
	int v1;
	
	v1 = s->v2;
	s->v2 = s->v3;
	
	s->v3 = (s->fac * s->v2) >> 15;
	s->v3 = s->v3 - v1 + (sample >> s->chunky);
	if (abs(s->v3) > 32768) {
		s->chunky++;
		s->v3 = s->v3 >> 1;
		s->v2 = s->v2 >> 1;
		v1 = v1 >> 1;
	}
}

static inline void goertzel_update(goertzel_state_t *s, short *samps, int count)
{
	int i;
	
	for (i = 0; i < count; i++) {
		goertzel_sample(s, samps[i]);
	}
}


static inline float goertzel_result(goertzel_state_t *s)
{
	goertzel_result_t r;
	r.value = (s->v3 * s->v3) + (s->v2 * s->v2);
	r.value -= ((s->v2 * s->v3) >> 15) * s->fac;
	r.power = s->chunky * 2;
	return (float)r.value * (float)(1 << r.power);
}

static inline void goertzel_init(goertzel_state_t *s, float freq, int samples)
{
	s->v2 = s->v3 = s->chunky = 0.0;
	s->fac = (int)(32768.0 * 2.0 * cos(2.0 * M_PI * freq / TONE_SAMPLE_RATE));
	s->samples = samples;
}

static inline void goertzel_reset(goertzel_state_t *s)
{
	s->v2 = s->v3 = s->chunky = 0.0;
}

/*
 * Code used to detect tones
*/


static void tone_detect_init(tone_detect_state_t *s, int freq, int duration, int amp)
{
	int duration_samples;
	float x;
	int periods_in_block;

	s->freq = freq;

	/* Desired tone duration in samples */
	duration_samples = duration * TONE_SAMPLE_RATE / 1000;
	/* We want to allow 10% deviation of tone duration */
	duration_samples = duration_samples * 9 / 10;

	/* If we want to remove tone, it is important to have block size not
	   to exceed frame size. Otherwise by the moment tone is detected it is too late
 	   to squelch it from previous frames */
	s->block_size = TONE_SAMPLES_IN_FRAME;

	periods_in_block = s->block_size * freq / TONE_SAMPLE_RATE;

	/* Make sure we will have at least 5 periods at target frequency for analisys.
	   This may make block larger than expected packet and will make squelching impossible
	   but at least we will be detecting the tone */
	if (periods_in_block < 5)
		periods_in_block = 5;

	/* Now calculate final block size. It will contain integer number of periods */
	s->block_size = periods_in_block * TONE_SAMPLE_RATE / freq;

	/* tone_detect is currently only used to detect fax tones and we
	   do not need suqlching the fax tones */
	s->squelch = 0;

	/* Account for the first and the last block to be incomplete
	   and thus no tone will be detected in them */
	s->hits_required = (duration_samples - (s->block_size - 1)) / s->block_size;

	goertzel_init(&s->tone, freq, s->block_size);

	s->samples_pending = s->block_size;
	s->hit_count = 0;
	s->last_hit = 0;
	s->energy = 0.0;

	/* We want tone energy to be amp decibels above the rest of the signal (the noise).
	   According to Parseval's theorem the energy computed in time domain equals to energy
	   computed in frequency domain. So subtracting energy in the frequency domain (Goertzel result)
	   from the energy in the time domain we will get energy of the remaining signal (without the tone
	   we are detecting). We will be checking that
		10*log(Ew / (Et - Ew)) > amp
	   Calculate threshold so that we will be actually checking
		Ew > Et * threshold
	*/

	x = pow(10.0, amp / 10.0);
	s->threshold = x / (x + 1);

	ast_log(LOG_DEBUG,"Setup tone %d Hz, %d ms, block_size=%d, hits_required=%d\n", freq, duration, s->block_size, s->hits_required);
}


static int tone_detect(tone_detect_state_t *s, int16_t *amp, int samples)
{
	float tone_energy;
	int i;
	int hit = 0;
	int limit;
	int res = 0;
	int16_t *ptr;
	int start, end;

	for (start = 0;  start < samples;  start = end) {
		/* Process in blocks. */
		limit = samples - start;
		if (limit > s->samples_pending) {
			limit = s->samples_pending;
		}
		end = start + limit;

		for (i = limit, ptr = amp ; i > 0; i--, ptr++) {
			/* signed 32 bit int should be enough to suqare any possible signed 16 bit value */
			s->energy += (int32_t) *ptr * (int32_t) *ptr;

			goertzel_sample(&s->tone, *ptr);
		}

		s->samples_pending -= limit;

		if (s->samples_pending) {
			/* Finished incomplete (last) block */
			break;
		}

		tone_energy = goertzel_result(&s->tone);

		/* Scale to make comparable */
		tone_energy *= 2.0;
		s->energy *= s->block_size;

		hit = 0;
		ast_log(LOG_DEBUG,"tone %d, Ew=%.2E, Et=%.2E, s/n=%10.2f\n", s->freq, tone_energy, s->energy, tone_energy / (s->energy - tone_energy));
		if (tone_energy > s->energy * s->threshold) {
			ast_log(LOG_DEBUG,"Hit! count=%d\n", s->hit_count);
			hit = 1;
		}

		if (s->hit_count) {
			s->hit_count++;
		}

		if (hit == s->last_hit) {
			if (!hit) {
				/* Two successive misses. Tone ended */
				s->hit_count = 0;
			} else if (!s->hit_count) {
				s->hit_count++;
			}

		}

		if (s->hit_count >= s->hits_required) {
			ast_log(LOG_DEBUG,"%d Hz tone detected\n", s->freq);
			res = 1;
		}

		s->last_hit = hit;

		/* Reinitialise the detector for the next block */
		/* Reset for the next block */
		goertzel_reset(&s->tone);

		/* Advance to the next block */
		s->energy = 0.0;
		s->samples_pending = s->block_size;

		amp += limit;
	}

	return res;
}


/*
 * DAQ variables
 */

static struct daq_tag daq;


/*
* Forward decl's - these suppress compiler warnings when funcs coded further down the file than thier invokation
*/

static int setrbi(struct rpt *myrpt);
static int set_ft897(struct rpt *myrpt);
static int set_ft100(struct rpt *myrpt);
static int set_ft950(struct rpt *myrpt);
static int set_ic706(struct rpt *myrpt);
static int set_xcat(struct rpt *myrpt);
static int setkenwood(struct rpt *myrpt);
static int set_tm271(struct rpt *myrpt);
static int set_tmd700(struct rpt *myrpt);
static int setrbi_check(struct rpt *myrpt);
static int setxpmr(struct rpt *myrpt, int dotx);



/*
* Define function protos for function table here
*/

static int function_ilink(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_autopatchup(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_autopatchdn(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_status(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_cop(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_remote(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_macro(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_playback(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_localplay(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_meter(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_userout(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_cmd(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*
* Function table
*/

static struct function_table_tag function_table[] = {
	{"cop", function_cop},
	{"autopatchup", function_autopatchup},
	{"autopatchdn", function_autopatchdn},
	{"ilink", function_ilink},
	{"status", function_status},
	{"remote", function_remote},
	{"macro", function_macro},
	{"playback", function_playback},
	{"localplay", function_localplay},
	{"meter", function_meter},
	{"userout", function_userout},
	{"cmd", function_cmd}


} ;


/*
 * *****************************************
 * Generic serial I/O routines             *
 * *****************************************
*/


/*
 * Generic serial port open command 
 */

static int serial_open(char *fname, int speed, int stop2)
{
	struct termios mode;
	int fd;

	fd = open(fname,O_RDWR);
	if (fd == -1)
	{
		if(debug >= 1)
			ast_log(LOG_WARNING,"Cannot open serial port %s\n",fname);
		return -1;
	}
	
	memset(&mode, 0, sizeof(mode));
	if (tcgetattr(fd, &mode)) {
		if(debug >= 1){
			ast_log(LOG_WARNING, "Unable to get serial parameters on %s: %s\n", fname, strerror(errno));
		}
		return -1;
	}
#ifndef	SOLARIS
	cfmakeraw(&mode);
#else
        mode.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                        |INLCR|IGNCR|ICRNL|IXON);
        mode.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
        mode.c_cflag &= ~(CSIZE|PARENB|CRTSCTS);
        mode.c_cflag |= CS8;
	if(stop2)
		mode.c_cflag |= CSTOPB;
	mode.c_cc[VTIME] = 3;
	mode.c_cc[VMIN] = 1; 
#endif

	cfsetispeed(&mode, speed);
	cfsetospeed(&mode, speed);
	if (tcsetattr(fd, TCSANOW, &mode)){
		if(debug >= 1) 
			ast_log(LOG_WARNING, "Unable to set serial parameters on %s: %s\n", fname, strerror(errno));
		return -1;
	}
	usleep(100000);
	if (debug >= 3)
		ast_log(LOG_NOTICE,"Opened serial port %s\n",fname);
	return(fd);	
}

/*
 * Return receiver ready status
 *
 * Return 1 if an Rx byte is avalable
 * Return 0 if none was avaialable after a time out period
 * Return -1 if error
 */


static int serial_rxready(int fd, int timeoutms)
{
int	myms = timeoutms;

	return(ast_waitfor_n_fd(&fd, 1, &myms,NULL));
}

/*
* Remove all RX characters in the receive buffer
*
* Return number of bytes flushed.
* or  return -1 if error
*
*/

static int serial_rxflush(int fd, int timeoutms)
{
	int res, flushed = 0;
	char c;
	
	while((res = serial_rxready(fd, timeoutms)) == 1){
		if(read(fd, &c, 1) == -1){
			res = -1;
			break;
		flushed++;
		}
	}		
	return (res == -1)? res : flushed;
}
/*
 * Receive a string from the serial device
 */

static int serial_rx(int fd, char *rxbuf, int rxmaxbytes, unsigned timeoutms, char termchr)
{
	char c;
	int i, j, res;

	if ((!rxmaxbytes) || (rxbuf == NULL)){ 
		return 0;
	}
	memset(rxbuf,0,rxmaxbytes);
	for(i = 0; i < rxmaxbytes; i++){
		if(timeoutms){
			res = serial_rxready(fd, timeoutms);
			if(res < 0)
				return -1;
			if(!res){
				break;
			}
		}
		j = read(fd,&c,1);
		if(j == -1){
			ast_log(LOG_WARNING,"read failed: %s\n", strerror(errno));
			return -1;
		}
		if (j == 0) 
			return i ;
		rxbuf[i] = c;
		if (termchr){
			rxbuf[i + 1] = 0;
			if (c == termchr) break;
		}
	}					
	if(i && debug >= 6) {
		printf("i = %d\n",i);
		printf("String returned was:\n");
		for(j = 0; j < i; j++)
			printf("%02X ", (unsigned char ) rxbuf[j]);
		printf("\n");
	}
	return i;
}

/*
 * Send a nul-terminated string to the serial device (without RX-flush)
 */

static int serial_txstring(int fd, char *txstring)
{
	int txbytes;

	txbytes = strlen(txstring);

	if(debug > 5)
		ast_log(LOG_NOTICE, "sending: %s\n", txstring);

	if(write(fd, txstring, txbytes) != txbytes){
		ast_log(LOG_WARNING,"write failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}
		
/*
 * Write some bytes to the serial port, then optionally expect a fixed response
 */

static int serial_io(int fd, char *txbuf, char *rxbuf, int txbytes, int rxmaxbytes, unsigned int timeoutms, char termchr)
{
	int i;

	if(debug >= 7)
		ast_log(LOG_NOTICE,"fd = %d\n",fd);

	if ((rxmaxbytes) && (rxbuf != NULL)){ 
		if((i = serial_rxflush(fd, 10)) == -1)
			return -1;
		if(debug >= 7)
			ast_log(LOG_NOTICE,"%d bytes flushed prior to write\n", i);
	}

	if(write(fd, txbuf, txbytes) != txbytes){
		ast_log(LOG_WARNING,"write failed: %s\n", strerror(errno));
		return -1;
	}

	return serial_rx(fd, rxbuf, rxmaxbytes, timeoutms, termchr);
}

/*
 * ***********************************
 * Uchameleon specific routines      *
 * ***********************************
 */

/* Forward Decl's */

static int uchameleon_do_long( struct daq_entry_tag *t, int pin,
int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2);
static int matchkeyword(char *string, char **param, char *keywords[]);
static int explode_string(char *str, char *strp[], int limit, char delim, char quote);
static void *uchameleon_monitor_thread(void *this);
static char *strupr(char *str);


/*
 * Start the Uchameleon monitor thread
 */




static int uchameleon_thread_start(struct daq_entry_tag *t)
{
	int res, tries = 50;
	pthread_attr_t attr;


	ast_mutex_init(&t->lock);


	/*
 	* Start up uchameleon monitor thread
 	*/

       	pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	res = ast_pthread_create(&t->threadid,&attr,uchameleon_monitor_thread,(void *) t);
	if(res){
		ast_log(LOG_WARNING, "Could not start uchameleon monitor thread\n");
		return -1;
	}

	ast_mutex_lock(&t->lock);
	while((!t->active)&&(tries)){
		ast_mutex_unlock(&t->lock);
		usleep(100*1000);
		ast_mutex_lock(&t->lock);
		tries--;
	}
	ast_mutex_unlock(&t->lock);

	if(!tries)
		return -1;


        return 0;
}

static int uchameleon_connect(struct daq_entry_tag *t)
{
	int count;
	static char *idbuf = "id\n";
	static char *ledbuf = "led on\n";
	static char *expect = "Chameleon";
	char rxbuf[20];

        if((t->fd = serial_open(t->dev, B115200, 0)) == -1){
               	ast_log(LOG_WARNING, "serial_open on %s failed!\n", t->name);
                return -1;
        }
        if((count = serial_io(t->fd, idbuf, rxbuf, strlen(idbuf), 14, DAQ_RX_TIMEOUT, 0x0a)) < 1){
              	ast_log(LOG_WARNING, "serial_io on %s failed\n", t->name);
		close(t->fd);
		t->fd = -1;
                return -1;
        }
	if(debug >= 3)
        	ast_log(LOG_NOTICE,"count = %d, rxbuf = %s\n",count,rxbuf);
	if((count != 13)||(strncmp(expect, rxbuf+4, sizeof(&expect)))){
		ast_log(LOG_WARNING, "%s is not a uchameleon device\n", t->name);
		close(t->fd);
		t->fd = -1;
		return -1;
	}
	/* uchameleon LED on solid once we communicate with it successfully */
	
	if(serial_io(t->fd, ledbuf, NULL, strlen(ledbuf), 0, DAQ_RX_TIMEOUT, 0) == -1){
		ast_log(LOG_WARNING, "Can't set LED on uchameleon device\n");
		close(t->fd);
		t->fd= -1;
		return -1;
	}
	return 0;
}

/*
 * Uchameleon alarm handler
 */


static void uchameleon_alarm_handler(struct daq_pin_entry_tag *p)
{
	char *valuecopy;
	int i, busy;
	char *s;
	char *argv[7];
	int argc;


	if(!(valuecopy = ast_strdup(p->alarmargs))){
		ast_log(LOG_ERROR,"Out of memory\n");
		return;
	}
	
	argc = explode_string(valuecopy, argv, 6, ',', 0);

	if(debug >= 3){
		ast_log(LOG_NOTICE, "Alarm event on device %s, pin %d, state = %d\n", argv[0], p->num, p->value);
	}

	/*
 	* Node: argv[3]
 	* low function: argv[4]
 	* high function: argv[5]
 	*
 	*/
	i = busy = 0;
	s = (p->value) ? argv[5]: argv[4];
	if((argc == 6)&&(s[0] != '-')){
		for(i = 0; i < nrpts; i++){
			if(!strcmp(argv[3], rpt_vars[i].name)){

				struct rpt *myrpt = &rpt_vars[i];
				rpt_mutex_lock(&myrpt->lock);
				if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(s)){
					rpt_mutex_unlock(&myrpt->lock);
					busy=1;
				}
				if(!busy){
					myrpt->macrotimer = MACROTIME;
					strncat(myrpt->macrobuf,s,MAXMACRO - 1);
				}
				rpt_mutex_unlock(&myrpt->lock);

			}
		}
	}
	if(argc != 6){
		ast_log(LOG_WARNING, "Not enough arguments to process alarm\n"); 
	}
	else if(busy){
		ast_log(LOG_WARNING, "Function decoder busy while processing alarm");
	}
	ast_free(valuecopy);
}




/*
 * Initialize pins
 */



static int uchameleon_pin_init(struct daq_entry_tag *t)
{
	int i;
	struct ast_config *ourcfg;
	struct ast_variable *var,*var2;

	/* Pin Initialization */

	#ifdef	NEW_ASTERISK
		ourcfg = ast_config_load("rpt.conf",config_flags);
	#else
		ourcfg = ast_config_load("rpt.conf");
	#endif

	if(!ourcfg)
		return -1;

	var2 = ast_variable_browse(ourcfg, t->name);
	while(var2){
		unsigned int pin;
		int x = 0;
		static char *pin_keywords[]={"inadc","inp","in","out",NULL};
		if((var2->name[0] < '0')||(var2->name[0] > '9')){
			var2 = var2->next;
			continue;
		}
		pin = (unsigned int) atoi(var2->name);
		i = matchkeyword((char *)var2->value, NULL, pin_keywords);
		if(debug >= 3)
			ast_log(LOG_NOTICE, "Pin = %d, Pintype = %d\n", pin, i);
		if(i && i < 5){
			uchameleon_do_long(t, pin, DAQ_CMD_PINSET, NULL, &i, NULL);	 /* Set pin type */
			uchameleon_do_long(t, pin, DAQ_CMD_MONITOR, NULL, &x, NULL); /* Monitor off */
			if(i == DAQ_PT_OUT){
				if(debug >= 3)
					ast_log(LOG_NOTICE,"Set output pin %d low\n", pin); /* Set output pins low */
				uchameleon_do_long(t, pin, DAQ_CMD_OUT, NULL, &x, NULL);
			}
		}
		else
			ast_log(LOG_WARNING,"Invalid pin type: %s\n", var2->value);
		var2 = var2->next;
	}

	/*
 	* Alarm initialization
 	*/

	var = ast_variable_browse(ourcfg,"alarms");
	while(var){
		int ignorefirst,pin;
		char s[64];
		char *argv[7];
		struct daq_pin_entry_tag *p;


		/* Parse alarm entry */

		strlcpy(s,var->value,sizeof(s)-1);

		if(explode_string(s, argv, 6, ',', 0) != 6){
			ast_log(LOG_WARNING,"Alarm arguments must be 6 for %s\n", var->name);
			var = var->next;
			continue;
		}

		ignorefirst = atoi(argv[2]);

		if(!(pin = atoi(argv[1]))){
			ast_log(LOG_WARNING,"Pin must be greater than 0 for %s\n",var->name);
			var = var->next;
			continue;
		}

		/* Find the pin entry */
		p = t->pinhead;
		while(p){
			if(p->num == pin)
				break;
			p = p->next;
		}
		if(!p){
			ast_log(LOG_WARNING,"Can't find pin %d for device %s\n", pin, argv[0]);
			var = var->next;
			continue;
		}

		if(!strcmp(argv[0], t->name)){
			strncpy(p->alarmargs, var->value, 64); /* Save the alarm arguments in the pin entry */
			p->alarmargs[63] = 0;
			ast_log(LOG_NOTICE,"Adding alarm %s on pin %d\n", var->name, pin);
			uchameleon_do_long(t, pin, DAQ_CMD_MONITOR, uchameleon_alarm_handler, &ignorefirst, NULL);
		}
		var = var->next;
	}

	ast_config_destroy(ourcfg);
	time(&t->adcacqtime); /* Start ADC Acquisition */ 
	return -0;
}


/*
 * Open the serial channel and test for the uchameleon device at the end of the link
 */

static int uchameleon_open(struct daq_entry_tag *t)
{
	int res;


	if(!t)
		return -1;

	if(uchameleon_connect(t)){
		ast_log(LOG_WARNING,"Cannot open device %s", t->name);
		return -1;
	}

	res = uchameleon_thread_start(t);

	if(!res)
		res = uchameleon_pin_init(t);

	return res;

}

/*
 * Close uchameleon
 */

static int uchameleon_close(struct daq_entry_tag *t)
{
	int res = 0;
	char *ledpat="led pattern 253\n";
	struct daq_pin_entry_tag *p,*pn;
	struct daq_tx_entry_tag *q,*qn;

	if(!t)
		return -1;

	ast_mutex_lock(&t->lock);

	if(t->active){
		res = pthread_kill(t->threadid, 0);
		if(res)
		ast_log(LOG_WARNING, "Can't kill monitor thread");
		ast_mutex_unlock(&t->lock);
		return -1;
	}

	if(t->fd > 0)
		serial_io(t->fd, ledpat, NULL, strlen(ledpat) ,0, 0, 0); /* LED back to flashing */

	/* Free linked lists */

	if(t->pinhead){
		p = t->pinhead;
		while(p){
			pn = p->next;
			ast_free(p);
			p = pn;
		}
		t->pinhead = NULL;
	}


	if(t->txhead){
		q = t->txhead;
		while(q){
			qn = q->next;
			ast_free(q);
			q = qn;
		}
		t->txhead = t->txtail = NULL;
	}
	
	if(t->fd > 0){	
		res = close(t->fd);
		if(res)
			ast_log(LOG_WARNING, "Error closing serial port");
		t->fd = -1;
	}
	ast_mutex_unlock(&t->lock);
	ast_mutex_destroy(&t->lock);
	return res;
}

/*
 * Uchameleon generic interface which supports monitor thread
 */

static int uchameleon_do_long( struct daq_entry_tag *t, int pin,
int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2)
{	
	int i,j,x;
	struct daq_pin_entry_tag *p, *listl, *listp;

	if(!t)
		return -1;

	ast_mutex_lock(&t->lock);

	if(!t->active){
		/* Try to restart thread and re-open device */
		ast_mutex_unlock(&t->lock);
		uchameleon_close(t);
		usleep(10*1000);
		if(uchameleon_open(t)){
			ast_log(LOG_WARNING,"Could not re-open Uchameleon\n");
			return -1;
		}
		ast_mutex_lock(&t->lock);
		/* We're back in business! */
	}


	/* Find our pin */

	listp = listl = t->pinhead;
	while(listp){
		listl = listp;
		if(listp->num == pin)
			break;
		listp = listp->next;
	}
	if(listp){
		if(cmd == DAQ_CMD_PINSET){
			if(arg1 && *arg1 && (*arg1 < 19)){
				while(listp->state){
					ast_mutex_unlock(&t->lock);
					usleep(10*1000); /* Wait */
					ast_mutex_lock(&t->lock);
				}
				listp->command = DAQ_CMD_PINSET;
				listp->pintype = *arg1; /* Pin redefinition */
				listp->valuemin = 255;
				listp->valuemax = 0;
				listp->state = DAQ_PS_START;
			}
			else{
				ast_log(LOG_WARNING,"Invalid pin number for pinset\n");
			}
		}
		else{
			/* Return ADC value */

			if(cmd == DAQ_CMD_ADC){
				if(arg2){
					switch(*((int *) arg2)){
						case DAQ_SUB_CUR:
							if(arg1)
								*arg1 = listp->value;
							break;

						case DAQ_SUB_STAVG: /* Short term average */
							x = 0;
							i = listp->adcnextupdate;
							for(j = 0 ; j < ADC_HISTORY_DEPTH; j++){
								if(debug >= 4){
									ast_log(LOG_NOTICE, "Sample for avg: %d\n",
									listp->adchistory[i]);
								}
								x += listp->adchistory[i];
								if(++i >= ADC_HISTORY_DEPTH)
									i = 0;
							}
							x /= ADC_HISTORY_DEPTH;
							if(debug >= 3)
								ast_log(LOG_NOTICE, "Average: %d\n", x);
							if(arg1)
								*arg1 = x;
							break;

						case DAQ_SUB_STMAX: /* Short term maximum */
							x = 0;
							i = listp->adcnextupdate;
							for(j = 0 ; j < ADC_HISTORY_DEPTH; j++){
								if(debug >= 4){
									ast_log(LOG_NOTICE, "Sample for max: %d\n",
									listp->adchistory[i]);
								}
								if(listp->adchistory[i] > x)
									x = listp->adchistory[i];
								if(++i >= ADC_HISTORY_DEPTH)
									i = 0;
							}
							if(debug >= 3)
								ast_log(LOG_NOTICE, "Maximum: %d\n", x);
							if(arg1)
								*arg1 = x;
							break;

						case DAQ_SUB_STMIN: /* Short term minimum */
							x = 255 ;
							i = listp->adcnextupdate;
							if(i >= ADC_HISTORY_DEPTH)
								i = 0;
							for(j = 0 ; j < ADC_HISTORY_DEPTH; j++){
								if(debug >= 4){
									ast_log(LOG_NOTICE, "Sample for min: %d\n",
									listp->adchistory[i]);
								}
								if(listp->adchistory[i] < x)
									x = listp->adchistory[i];
								if(++i >= ADC_HISTORY_DEPTH)
									i = 0;
								}
							if(debug >= 3)
								ast_log(LOG_NOTICE, "Minimum: %d\n", x);
							if(arg1)
								*arg1 = x;
							break;

						case DAQ_SUB_MAX: /* Max since start or reset */
							if(arg1)
								*arg1 = listp->valuemax;
							break;

						case DAQ_SUB_MIN: /* Min since start or reset */
							if(arg1)
								*arg1 = listp->valuemin;
							break;

						default:
							ast_mutex_unlock(&t->lock);
							return -1;
					}
				}
				else{
					if(arg1)
						*arg1 = listp->value;
				}
				ast_mutex_unlock(&t->lock);	
				return 0;
			}

			/* Don't deadlock if monitor has been previously issued for a pin */

			if(listp->state == DAQ_PS_IN_MONITOR){
				if((cmd != DAQ_CMD_MONITOR) || (exec)){
					ast_log(LOG_WARNING,
						"Monitor was previously set on pin %d, command ignored\n",listp->num);
					ast_mutex_unlock(&t->lock);
					return -1;
				}
			}

			/* Rest of commands are processed here */

			while(listp->state){
				ast_mutex_unlock(&t->lock);
				usleep(10*1000); /* Wait */
				ast_mutex_lock(&t->lock);
			}

			if(cmd == DAQ_CMD_MONITOR){
				if(arg1)
					listp->ignorefirstalarm = *arg1;
				listp->monexec = exec;
			}

			listp->command = cmd;

			if(cmd == DAQ_CMD_OUT){
				if(arg1){
					listp->value = *arg1;
				}
				else{
					ast_mutex_unlock(&t->lock);
					return 0;
				}
			}
			listp->state = DAQ_PS_START;
			if((cmd == DAQ_CMD_OUT)||(cmd == DAQ_CMD_MONITOR)){
				ast_mutex_unlock(&t->lock);
				return 0;
			}

 			while(listp->state){
				ast_mutex_unlock(&t->lock);
				usleep(10*1000); /* Wait */
				ast_mutex_lock(&t->lock);
			}
			*arg1 = listp->value;
			ast_mutex_unlock(&t->lock);
			return 0;
		}
	}
	else{ /* Pin not in list */
		if(cmd == DAQ_CMD_PINSET){
			if(arg1 && *arg1 && (*arg1 < 19)){
				/* New pin definition */
				if(!(p = (struct daq_pin_entry_tag *) malloc(sizeof(struct daq_pin_entry_tag)))){
					ast_log(LOG_ERROR,"Out of memory");
					ast_mutex_unlock(&t->lock);
					return -1;
				}
				memset(p, 0, sizeof(struct daq_pin_entry_tag));
				p->pintype = *arg1;
				p->command = DAQ_CMD_PINSET;
				p->num = pin;
				if(!listl){
					t->pinhead = p;
				}
				else{
					listl->next = p; 
				}
				p->state = DAQ_PS_START;
				ast_mutex_unlock(&t->lock);
				return 0;
			}
			else{
				ast_log(LOG_WARNING,"Invalid pin number for pinset\n");
			}
		}
		else{
			ast_log(LOG_WARNING,"Invalid pin number for pin I/O command\n");
		}
	}
	ast_mutex_unlock(&t->lock);
	return -1;
}
 
/*
 * Reset a minimum or maximum reading
 */

static int uchameleon_reset_minmax(struct daq_entry_tag *t, int pin, int minmax)
{
	struct daq_pin_entry_tag *p;

	/* Find the pin */
	p = t->pinhead;
	while(p){
		if(p->num == pin)
			break;
		p = p->next;
	}
	if(!p)
		return -1;
	ast_mutex_lock(&t->lock);
	if(minmax){
		ast_log(LOG_NOTICE, "Resetting maximum on device %s, pin %d\n",t->name, pin);
		p->valuemax = 0;
	}
	else{
		p->valuemin = 255;
		ast_log(LOG_NOTICE, "Resetting minimum on device %s, pin %d\n",t->name, pin);
	}
	ast_mutex_unlock(&t->lock);
	return 0;
}




/*
 * Queue up a tx command (used exclusively by uchameleon_monitor() )
 */

static void uchameleon_queue_tx(struct daq_entry_tag *t, char *txbuff)
{
	struct daq_tx_entry_tag *q;

	if(!t)
		return;
		
	if(!(q = (struct daq_tx_entry_tag *) ast_malloc(sizeof(struct daq_tx_entry_tag)))){
		ast_log(LOG_WARNING, "Out of memory\n");
		return;
	}

	memset(q, 0, sizeof(struct daq_tx_entry_tag));

	strncpy(q->txbuff, txbuff, 32);
	q->txbuff[31] = 0;

	if(t->txtail){
		t->txtail->next = q;
		q->prev = t->txtail;
		t->txtail = q;
	}
	else
		t->txhead = t->txtail = q;
	return;
}


/*
 * Monitor thread for Uchameleon devices
 *
 * started by uchameleon_open() and shutdown by uchameleon_close()
 *
 */
static void *uchameleon_monitor_thread(void *this)
{
	int pin = 0, sample = 0;
	int i,res,valid,adc_acquire;
	time_t now;
	char rxbuff[32];
	char txbuff[32];
	char *rxargs[4];
	struct daq_entry_tag *t = (struct daq_entry_tag *) this;
	struct daq_pin_entry_tag *p;
	struct daq_tx_entry_tag *q;



	if(debug)
		ast_log(LOG_NOTICE, "DAQ: thread started\n");

	ast_mutex_lock(&t->lock);
	t->active = 1;
	ast_mutex_unlock(&t->lock);

	for(;;){
		adc_acquire = 0;
		 /* If receive data */
		res = serial_rx(t->fd, rxbuff, sizeof(rxbuff), DAQ_RX_TIMEOUT, 0x0a);
		if(res == -1){
			ast_log(LOG_ERROR,"serial_rx failed\n");
			close(t->fd);
			ast_mutex_lock(&t->lock);
			t->fd = -1;
			t->active = 0;
			ast_mutex_unlock(&t->lock);
			return this; /* Now, we die */
		}
		if(res){
			if(debug >= 5) 
				ast_log(LOG_NOTICE, "Received: %s\n", rxbuff);
			valid = 0;
			/* Parse return string */
			i = explode_string(rxbuff, rxargs, 3, ' ', 0);
			if(i == 3){
				if(!strcmp(rxargs[0],"pin")){
					valid = 1;
					pin = atoi(rxargs[1]);
					sample = atoi(rxargs[2]);
				}
				if(!strcmp(rxargs[0],"adc")){
					valid = 2;
					pin = atoi(rxargs[1]);
					sample = atoi(rxargs[2]);
				}
			}
			if(valid){
				/* Update the correct pin list entry */
				ast_mutex_lock(&t->lock);
				p = t->pinhead;
				while(p){
					if(p->num == pin){
						if((valid == 1)&&((p->pintype == DAQ_PT_IN)||
							(p->pintype == DAQ_PT_INP)||(p->pintype == DAQ_PT_OUT))){
							p->value = sample ? 1 : 0;
							if(debug >= 3)
								ast_log(LOG_NOTICE,"Input pin %d is a %d\n",
									p->num, p->value);
							/* Exec monitor fun if state is monitor */

							if(p->state == DAQ_PS_IN_MONITOR){
								if(!p->alarmmask && !p->ignorefirstalarm && p->monexec){
									(*p->monexec)(p);
								}
								p->ignorefirstalarm = 0;
							}
							else
								p->state = DAQ_PS_IDLE;
						}
						if((valid == 2)&&(p->pintype == DAQ_PT_INADC)){
							p->value = sample;
							if(sample > p->valuemax)
								p->valuemax = sample;
							if(sample < p->valuemin)
								p->valuemin = sample;
							p->adchistory[p->adcnextupdate++] = sample;
							if(p->adcnextupdate >= ADC_HISTORY_DEPTH)
								p->adcnextupdate = 0;
							p->state = DAQ_PS_IDLE;
						}
						break;
					}
					p = p->next;
				}	 
				ast_mutex_unlock(&t->lock);
			}
		}
		

		if(time(&now) >= t->adcacqtime){
			t->adcacqtime = now + DAQ_ADC_ACQINT;
			if(debug >= 4)
				ast_log(LOG_NOTICE,"Acquiring analog data\n");
			adc_acquire = 1;
		}

		/* Go through the pin linked list looking for new work */
		ast_mutex_lock(&t->lock);		
		p = t->pinhead;
		while(p){
			/* Time to acquire all ADC channels ? */
			if((adc_acquire) && (p->pintype == DAQ_PT_INADC)){
				p->state = DAQ_PS_START;
				p->command = DAQ_CMD_ADC;
			}
			if(p->state == DAQ_PS_START){
				p->state = DAQ_PS_BUSY; /* Assume we are busy */
				switch(p->command){
					case DAQ_CMD_OUT:
						if(p->pintype == DAQ_PT_OUT){
							snprintf(txbuff,sizeof(txbuff),"pin %d %s\n", p->num, (p->value) ?
							"hi" : "lo");
							if(debug >= 3)
								ast_log(LOG_NOTICE, "DAQ_CMD_OUT: %s\n", txbuff);
							uchameleon_queue_tx(t, txbuff);
							p->state = DAQ_PS_IDLE; /* TX is considered done */ 
						}
						else{
							ast_log(LOG_WARNING,"Wrong pin type for out command\n");
							p->state = DAQ_PS_IDLE;
						}
						break;

					case DAQ_CMD_MONITOR:
						snprintf(txbuff, sizeof(txbuff), "pin %d monitor %s\n", 
						p->num, p->monexec ? "on" : "off");
						uchameleon_queue_tx(t, txbuff);
						if(!p->monexec)
							p->state = DAQ_PS_IDLE; /* Restore to idle channel */
						else{
							p->state = DAQ_PS_IN_MONITOR;
						}
						break;

					case DAQ_CMD_IN:
						if((p->pintype == DAQ_PT_IN)||
							(p->pintype == DAQ_PT_INP)||(p->pintype == DAQ_PT_OUT)){
							snprintf(txbuff,sizeof(txbuff),"pin %d state\n", p->num);
							uchameleon_queue_tx(t, txbuff);
						}
						else{
							ast_log(LOG_WARNING,"Wrong pin type for in or inp command\n");
							p->state = DAQ_PS_IDLE;
						}
						break;
					
					case DAQ_CMD_ADC:
						if(p->pintype == DAQ_PT_INADC){
							snprintf(txbuff,sizeof(txbuff),"adc %d\n", p->num);
							uchameleon_queue_tx(t, txbuff);
						}
						else{
							ast_log(LOG_WARNING,"Wrong pin type for adc command\n");
							p->state = DAQ_PS_IDLE;
						}
						break;

					case DAQ_CMD_PINSET:
						if((!p->num)||(p->num > 18)){
							ast_log(LOG_WARNING,"Invalid pin number %d\n", p->num);
							p->state = DAQ_PS_IDLE;
						}
						switch(p->pintype){
							case DAQ_PT_IN:
							case DAQ_PT_INADC:
							case DAQ_PT_INP:
								if((p->pintype == DAQ_PT_INADC) && (p->num > 8)){
									ast_log(LOG_WARNING,
									"Invalid ADC pin number %d\n", p->num);
									p->state = DAQ_PS_IDLE;
									break;
								}					
								if((p->pintype == DAQ_PT_INP) && (p->num < 9)){
									ast_log(LOG_WARNING,
									"Invalid INP pin number %d\n", p->num);
									p->state = DAQ_PS_IDLE;
									break;
								}
								snprintf(txbuff, sizeof(txbuff), "pin %d in\n", p->num);
								uchameleon_queue_tx(t, txbuff);
								if(p->num > 8){
									snprintf(txbuff, sizeof(txbuff),
									"pin %d pullup %d\n", p->num,
									(p->pintype == DAQ_PT_INP) ? 1 : 0);
									uchameleon_queue_tx(t, txbuff);
								}
								p->valuemin = 255;
								p->valuemax = 0;
								p->state = DAQ_PS_IDLE;
								break;

							case DAQ_PT_OUT:
                        					snprintf(txbuff, sizeof(txbuff), "pin %d out\n", p->num);
								uchameleon_queue_tx(t, txbuff);
								p->state = DAQ_PS_IDLE;
								break;

							default:
								break;
						}
						break;

					default:
						ast_log(LOG_WARNING,"Unrecognized uchameleon command\n");
						p->state = DAQ_PS_IDLE;
						break;
				} /* switch */
			} /* if */
		p = p->next;
		} /* while */
		
		/* Transmit queued commands */
		while(t->txhead){
			q = t->txhead;
			strncpy(txbuff,q->txbuff,sizeof(txbuff));
			txbuff[sizeof(txbuff)-1] = 0;
			t->txhead = q->next;
			if(t->txhead)
				t->txhead->prev = NULL;
			else
				t->txtail = NULL;
			ast_free(q);
			ast_mutex_unlock(&t->lock);
			if(serial_txstring(t->fd, txbuff) == -1){
				close(t->fd);
				ast_mutex_lock(&t->lock);
				t->active= 0;
				t->fd = -1;
				ast_mutex_unlock(&t->lock);
				ast_log(LOG_ERROR,"Tx failed, terminating monitor thread\n");
				return this; /* Now, we die */
			}
				
			ast_mutex_lock(&t->lock);
		}/* while */
		ast_mutex_unlock(&t->lock);
	} /* for(;;) */
	return this;
}


/*
 * **************************
 * Generic DAQ functions    *
 * **************************
 */

/*
 * Forward Decl's
 */

static int saynum(struct ast_channel *mychannel, int num);
static int sayfile(struct ast_channel *mychannel,char *fname);
static int wait_interval(struct rpt *myrpt, int type, struct ast_channel *chan);
static void rpt_telem_select(struct rpt *myrpt, int command_source, struct rpt_link *mylink);
static void rpt_telem_select(struct rpt *myrpt, int command_source, struct rpt_link *mylink);

/*
 * Open a daq device
 */

static struct daq_entry_tag *daq_open(int type, char *name, char *dev)
{
	int fd;
	struct daq_entry_tag *t;


	if(!name)
		return NULL;

        if((t = ast_malloc(sizeof(struct daq_entry_tag))) == NULL){
		ast_log(LOG_WARNING,"daq_open out of memory\n");
		return NULL;
	}


	memset(t, 0, sizeof(struct daq_entry_tag));


	/* Save the device path for open*/
	if(dev){
		strncpy(t->dev, dev, MAX_DAQ_DEV);
		t->dev[MAX_DAQ_DEV - 1] = 0;
	}



	/* Save the name*/
	strlcpy(t->name, name, MAX_DAQ_NAME);
	t->dev[MAX_DAQ_NAME - 1] = 0;


	switch(type){
		case DAQ_TYPE_UCHAMELEON:
			if((fd = uchameleon_open(t)) == -1){
				ast_free(t);
				return NULL;
			}
			break;

		default:
			ast_free(t);
			return NULL;
	}
	t->type = type;
	return t;
}

/*
 * Close a daq device
 */


static int daq_close(struct daq_entry_tag *t)
{
	int res  = -1;

	if(!t)
		return res;

	switch(t->type){
		case DAQ_TYPE_UCHAMELEON:
			res = uchameleon_close(t);
			break;
		default:
			break;
	}

	ast_free(t);
	return res;
}

/*
 * Look up a device entry for a particular device name
 */

static struct daq_entry_tag *daq_devtoentry(char *name)
{
	struct daq_entry_tag *e = daq.hw;

	while(e){
		if(!strcmp(name, e->name))
			break; 
		e = e->next;
	}
	return e;
}



/*
 * Do something with the daq subsystem
 */

static int daq_do_long( struct daq_entry_tag *t, int pin, int cmd,
void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2)
{
	int res = -1;

	switch(t->type){
		case DAQ_TYPE_UCHAMELEON:
			res = uchameleon_do_long(t, pin, cmd, exec, arg1, arg2);
			break;
		default:
			break;
	}
	return res;
}

/*
 * Short version of above
 */

static int daq_do( struct daq_entry_tag *t, int pin, int cmd, int arg1)
{
	int a1 = arg1;

	return daq_do_long(t, pin, cmd, NULL, &a1, NULL);
}


/*
 * Function to reset the long term minimum or maximum
 */

static int daq_reset_minmax(char *device, int pin, int minmax)
{
	int res = -1;
	struct daq_entry_tag *t;
	
	if(!(t = daq_devtoentry(device)))
		return -1;
	switch(t->type){
		case DAQ_TYPE_UCHAMELEON:
			res = uchameleon_reset_minmax(t, pin, minmax);
			break;
		default:
			break;
	}
	return res;
}

/*
 * Initialize DAQ subsystem
 */

static void daq_init(struct ast_config *cfg)
{
	struct ast_variable *var;
	struct daq_entry_tag **t_next, *t = NULL;
	char s[64];
	daq.ndaqs = 0;
	t_next = &daq.hw;
	var = ast_variable_browse(cfg,"daq-list");
	while(var){
		char *p;
		if(strncmp("device",var->name,6)){
			ast_log(LOG_WARNING,"Error in daq_entries stanza on line %d\n", var->lineno);
			break;
		}
		strlcpy(s,var->value,sizeof(s)-1); /* Make copy of device entry */
		if(!(p = (char *) ast_variable_retrieve(cfg,s,"hwtype"))){
			ast_log(LOG_WARNING,"hwtype variable required for %s stanza\n", s);
			break;
		}
		if(strncmp(p,"uchameleon",10)){
			ast_log(LOG_WARNING,"Type must be uchameleon for %s stanza\n", s);
			break;
		}
                if(!(p = (char *) ast_variable_retrieve(cfg,s,"devnode"))){
                        ast_log(LOG_WARNING,"devnode variable required for %s stanza\n", s);
                        break;
                }
		if(!(t = daq_open(DAQ_TYPE_UCHAMELEON, (char *) s, (char *) p))){
			ast_log(LOG_WARNING,"Cannot open device name %s\n",p);
			break;
		}
		/* Add to linked list */
		*t_next = t;
		t_next = &t->next;

		daq.ndaqs++;	
		if(daq.ndaqs >= MAX_DAQ_ENTRIES)
			break;
		var = var->next;
	}


}		

/*
 * Uninitialize DAQ Subsystem
 */

static void daq_uninit(void)
{
	struct daq_entry_tag *t_next, *t;

	/* Free daq memory */
	t = daq.hw;
	while(t){
		t_next = t->next;
		daq_close(t);
		t = t_next;
	}
	daq.hw = NULL;
}


/*
 * Parse a request METER request for telemetry thread
 * This is passed in a comma separated list of items from the function table entry
 * There should be 3 or 4 fields in the function table entry: device, channel, meter face, and  optionally: filter
 */


static int handle_meter_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *args)
{
	int i,res,files,filter,val;
	int pin = 0;
	int pintype = 0;
	int device = 0;
	int metertype = 0;
	int numranges = 0;
	int filtertype = 0;
	int rangemin,rangemax;
	float scaledval = 0.0, scalepre = 0.0, scalepost = 0.0, scalediv = 1.0, valtoround;
	char *myargs,*meter_face;
	const char *p;
	char *start, *end;
	char *sounds = NULL;
	char *rangephrase = NULL;
	char *argv[5];
	char *sound_files[MAX_METER_FILES+1];
	char *range_strings[MAX_DAQ_RANGES+1];
	char *bitphrases[3];	
	static char *filter_keywords[]={"none","max","min","stmin","stmax","stavg",NULL};
	struct daq_entry_tag *entry;
	
	if(!(myargs = ast_strdup(args))){ /* Make a local copy to slice and dice */
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	
	i = explode_string(myargs, argv, 4, ',', 0);
	if((i != 4) && (i != 3)){ /* Must have 3 or 4 substrings, no more, no less */
		ast_log(LOG_WARNING,"Wrong number of arguments for meter telemetry function is: %d s/b 3 or 4", i);
		ast_free(myargs);
		return -1;
	}
	if(debug >= 3){
		ast_log(LOG_NOTICE,"Device: %s, Pin: %s, Meter Face: %s Filter: %s\n",
		argv[0],argv[1],argv[2], argv[3]);	
	}

	if(i == 4){
		filter = matchkeyword(argv[3], NULL, filter_keywords);
		if(!filter){
			ast_log(LOG_WARNING,"Unsupported filter type: %s\n",argv[3]);
			ast_free(myargs);
			return -1;
		}
		filter--;
	}
	else
		filter = DAQ_SUB_CUR;	
	
	/* Find our device */
	if(!(entry = daq_devtoentry(argv[0]))){
		ast_log(LOG_WARNING,"Cannot find device %s in daq-list\n",argv[0]);
		ast_free(myargs);
		return -1;
	}

	/* Check for compatible pin type */
	if(!(p = ast_variable_retrieve(myrpt->cfg,argv[0],argv[1]))){
		ast_log(LOG_WARNING,"Channel %s not defined for %s\n", argv[1], argv[0]);
		ast_free(myargs);
		return -1;
	}

	if(!strcmp("inadc",p))
		pintype = 1;
	if((!strcmp("inp",p))||(!strcmp("in",p)||(!strcmp("out", p))))
		pintype = 2;
	if(!pintype){
		ast_log(LOG_WARNING,"Pin type must be one of inadc, inp, in, or out for channel %s\n",argv[1]);
		ast_free(myargs);
		return -1;
	}
	if(debug >= 3)
		ast_log(LOG_NOTICE,"Pintype = %d\n",pintype);

	pin = atoi(argv[1]);

	/*
 	Look up and parse the meter face

	[meter-faces]
	batvolts=scale(0,12.8,0),thevoltage,is,volts
	winddir=range(0-33:north,34-96:west,97-160:south,161-224:east,225-255:north),thewindis,?
	door=bit(closed,open),thedooris,?

	*/

	if(!(p = ast_variable_retrieve(myrpt->cfg,"meter-faces", argv[2]))){
		ast_log(LOG_WARNING,"Meter face %s not found", argv[2]);
		ast_free(myargs);		
		return -1;
	}

	if(!(meter_face = ast_strdup(p))){
		ast_log(LOG_WARNING,"Out of memory");
		ast_free(myargs);
		return -1;
	}
	
	if(!strncmp("scale", meter_face, 5)){ /* scale function? */
		metertype = 1;
		if((!(end = strchr(meter_face,')')))||
			(!(start = strchr(meter_face, '(')))||
			(!end[1])||(!end[2])||(end[1] != ',')){ /* Properly formed? */
			ast_log(LOG_WARNING,"Syntax error in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
		*start++ = 0; /* Points to comma delimited scaling values */
		*end = 0;
		sounds = end + 2; /* Start of sounds part */
		if(sscanf(start,"%f,%f,%f",&scalepre, &scalediv, &scalepost) != 3){
			ast_log(LOG_WARNING,"Scale must have 3 args in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
		if(scalediv < 1.0){
			ast_log(LOG_WARNING,"scalediv must be >= 1\n");
			ast_free(myargs);
			ast_free(meter_face);
			return -1;

		}		
	}
	else if(!strncmp("range", meter_face, 5)){ /* range function */
		metertype = 2;
		if((!(end = strchr(meter_face,')')))||
			(!(start = strchr(meter_face, '(')))||
			(!end[1])||(!end[2])||(end[1] != ',')){ /* Properly formed? */
			ast_log(LOG_WARNING,"Syntax error in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
		*start++ = 0;
		*end = 0;
		sounds = end + 2; 
		/*
 		* Parse range entries
 		*/
		if((numranges = explode_string(start, range_strings, MAX_DAQ_RANGES, ',', 0)) < 2 ){
			ast_log(LOG_WARNING, "At least 2 ranges required for range() in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}

	}
	else if(!strncmp("bit", meter_face, 3)){ /* bit function */
		metertype = 3;
		if((!(end = strchr(meter_face,')')))||
			(!(start = strchr(meter_face, '(')))||
			(!end[1])||(!end[2])||(end[1] != ',')){ /* Properly formed? */
			ast_log(LOG_WARNING,"Syntax error in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
		*start++ = 0;
		*end = 0;
		sounds = end + 2;
		if(2 != explode_string(start, bitphrases, 2, ',', 0)){
			ast_log(LOG_WARNING, "2 phrases required for bit() in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}		
	}
	else{
		ast_log(LOG_WARNING,"Meter face %s needs to specify one of scale, range or bit\n", argv[2]);
		ast_free(myargs);
		ast_free(meter_face);
		return -1;
	}

	/*
 	* Acquire 
 	*/

	val = 0;
	if(pintype == 1){
		res = daq_do_long(entry, pin, DAQ_CMD_ADC, NULL, &val, &filter);
		if(!res)
			scaledval = ((val + scalepre)/scalediv) + scalepost;
	}
	else{
		res = daq_do_long(entry, pin, DAQ_CMD_IN, NULL, &val, NULL);
	}

	if(res){ /* DAQ Subsystem is down */
		ast_free(myargs);
		ast_free(meter_face);
		return res;
	}

	/*
 	* Select Range
 	*/

	if(metertype == 2){
		for(i = 0; i < numranges; i++){
			if(2 != sscanf(range_strings[i],"%u-%u:", &rangemin, &rangemax)){
				ast_log(LOG_WARNING,"Range variable error on meter face %s\n", argv[2]);
				ast_free(myargs);
				ast_free(meter_face);
				return -1;
			}
			if((!(rangephrase = strchr(range_strings[i],':')) || (!rangephrase[1]))){
				ast_log(LOG_WARNING,"Range phrase missing on meter face %s\n", argv[2]);
				ast_free(myargs);
				ast_free(meter_face);
				return -1;
			}
			rangephrase++;
			if((val >= rangemin) && (val <= rangemax))
				break;
		}
		if(i == numranges){
			ast_log(LOG_WARNING,"Range missing on meter face %s for value %d\n", argv[2], val);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
	}

	if(debug >= 3){ /* Spew the variables */
		ast_log(LOG_NOTICE,"device = %d, pin = %d, pintype = %d, metertype = %d\n",device, pin, pintype, metertype);
		ast_log(LOG_NOTICE,"raw value = %d\n", val);
		if(metertype == 1){
			ast_log(LOG_NOTICE,"scalepre = %f, scalediv = %f, scalepost = %f\n",scalepre, scalediv, scalepost);
			ast_log(LOG_NOTICE,"scaled value = %f\n", scaledval);
		}
		if(metertype == 2){
			ast_log(LOG_NOTICE,"Range phrase is: %s for meter face %s\n", rangephrase, argv[2]);
		ast_log(LOG_NOTICE,"filtertype = %d\n", filtertype);
		}
		ast_log(LOG_NOTICE,"sounds = %s\n", sounds);

 	}
	
	/* Wait the normal telemetry delay time */
	
	if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) goto done;
	

	/* Split up the sounds string */
	
	files = explode_string(sounds, sound_files, MAX_METER_FILES, ',', 0);
	if(files == 0){
		ast_log(LOG_WARNING,"No sound files to say for meter %s\n",argv[2]);
		ast_free(myargs);
		ast_free(meter_face);
		return -1;
	}
	/* Say the files one by one acting specially on the ? character */
	res = 0;
	for(i = 0; i < files && !res; i++){
		if(sound_files[i][0] == '?'){ /* Insert sample */
			if(metertype == 1){
				int integer, decimal, precision = 0;
				if((scalediv >= 10) && (scalediv < 100)) /* Adjust precision of decimal places */
					precision = 10; 
				else if(scalediv >= 100)
					precision = 100;
				integer = (int) scaledval;
				valtoround = ((scaledval - integer) * precision);
				 /* grrr.. inline lroundf doesn't work with uClibc! */
				decimal = (int) ((valtoround + ((valtoround >= 0) ? 0.5 : -0.5)));
				if((precision) && (decimal == precision)){
					decimal = 0;
					integer++;
				}
				if(debug)
					ast_log(LOG_NOTICE,"integer = %d, decimal = %d\n", integer, decimal);
				res = saynum(mychannel, integer);
				if(!res && precision && decimal){
					res = sayfile(mychannel,"point");
					if(!res)
						res = saynum(mychannel, decimal);
				}
			}
			if(metertype == 2){
				res = sayfile(mychannel, rangephrase);
			}
			if(metertype == 3){
				res = sayfile(mychannel, bitphrases[(val) ? 1: 0]);
			}
	
		}
		else{
			res = sayfile(mychannel, sound_files[i]); /* Say the next word in the list */
		}					
	}
done: 	
	/* Done */
	ast_free(myargs);
	ast_free(meter_face);
	return 0;
}

/*
 * Handle USEROUT telemetry
 */

static int handle_userout_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *args)
{
	int argc, i, pin, reqstate, res;
	char *myargs;
	char *argv[11];
	struct daq_entry_tag *t;

	if(!(myargs = ast_strdup(args))){ /* Make a local copy to slice and dice */
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}

	if(debug >= 3)
		ast_log(LOG_NOTICE, "String: %s\n", myargs);

	argc = explode_string(myargs, argv, 10, ',', 0);
	if(argc < 4){ /* Must have at least 4 arguments */
		ast_log(LOG_WARNING,"Incorrect number of arguments for USEROUT function");
		ast_free(myargs);
		return -1;
	}
	if(debug >= 3){
		ast_log(LOG_NOTICE,"USEROUT Device: %s, Pin: %s, Requested state: %s\n",
		argv[0],argv[1],argv[2]);	
	}
	pin = atoi(argv[1]);
	reqstate = atoi(argv[2]);

	/* Find our device */
	if(!(t = daq_devtoentry(argv[0]))){
		ast_log(LOG_WARNING,"Cannot find device %s in daq-list\n",argv[0]);
		ast_free(myargs);
		return -1;
	}

	if(debug >= 3){
		ast_log(LOG_NOTICE, "Output to pin %d a value of %d with argc = %d\n", pin, reqstate, argc);
	}

	/* Set or reset the bit */

	res = daq_do( t, pin, DAQ_CMD_OUT, reqstate);
	
	/* Wait the normal telemetry delay time */
	
	if(!res)
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) goto done;

	/* Say the files one by one at argc index 3 */
	for(i = 3; i < argc && !res; i++){
		res = sayfile(mychannel, argv[i]); /* Say the next word in the list */
	}					
	
done:
	ast_free(myargs);
	return 0;
}


/*
*  Playback a meter reading
*/

static int function_meter(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{

	if (myrpt->remote)
		return DC_ERROR;

	if(debug)
		ast_log(LOG_NOTICE, "meter param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);
	
	rpt_telem_select(myrpt,command_source,mylink);
	rpt_telemetry(myrpt,METER,param);
	return DC_COMPLETE;
}



/*
*  Set or reset a USER Output bit
*/

static int function_userout(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{

	if (myrpt->remote)
		return DC_ERROR;

	ast_log(LOG_NOTICE, "userout param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);
	
	rpt_telem_select(myrpt,command_source,mylink);
	rpt_telemetry(myrpt,USEROUT,param);
	return DC_COMPLETE;
}


/*
*  Execute shell command
*/

static int function_cmd(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
	char *cp;

	if (myrpt->remote)
		return DC_ERROR;

	ast_log(LOG_NOTICE, "cmd param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);
	
	if (param) {
		if (*param == '#') /* to execute asterisk cli command */
		{
			ast_cli_command(nullfd,param + 1);
		}
		else
		{			
			cp = ast_malloc(strlen(param) + 10);
			if (!cp)
			{
				ast_log(LOG_NOTICE,"Unable to alloc");
				return DC_ERROR;
			}
			memset(cp,0,strlen(param) + 10);
			sprintf(cp,"%s &",param);
			ast_safe_system(cp);
			free(cp);
		}
	}
	return DC_COMPLETE;
}


/*
 ********************** 
* End of DAQ functions*
* *********************
*/


static long diskavail(struct rpt *myrpt)
{
struct	statfs statfsbuf;

	if (!myrpt->p.archivedir) return(0);
	if (statfs(myrpt->p.archivedir,&statfsbuf) == -1)
	{
		ast_log(LOG_WARNING,"Cannot get filesystem size for %s node %s\n",
			myrpt->p.archivedir,myrpt->name);
		return(-1);
	}
	return(statfsbuf.f_bavail);
}

static void flush_telem(struct rpt *myrpt)
{
	struct rpt_tele *telem;
	if(debug > 2)
		ast_log(LOG_NOTICE, "flush_telem()!!");
	rpt_mutex_lock(&myrpt->lock);
	telem = myrpt->tele.next;
	while(telem != &myrpt->tele)
	{
		if (telem->mode != SETREMOTE) ast_softhangup(telem->chan,AST_SOFTHANGUP_DEV);
		telem = telem->next;
	}
	rpt_mutex_unlock(&myrpt->lock);
}
/*
	return via error priority
*/
static int priority_jump(struct rpt *myrpt, struct ast_channel *chan)
{
	int res=0;

	// if (ast_test_flag(&flags,OPT_JUMP) && ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101) == 0){
	if (ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101) == 0){
		res = 0;
	} else {
		res = -1;
	}
	return res;
}
/*
*/

static void init_linkmode(struct rpt *myrpt, struct rpt_link *mylink, int linktype)
{

	if (!myrpt) return;
	if (!mylink) return;
	switch(myrpt->p.linkmode[linktype])
	{
	    case LINKMODE_OFF:
		mylink->linkmode = 0;
		break;
	    case LINKMODE_ON:
		mylink->linkmode = 0x7fffffff;
		break;
	    case LINKMODE_FOLLOW:
		mylink->linkmode = 0x7ffffffe;
		break;
	    case LINKMODE_DEMAND:
		mylink->linkmode = 1;
		break;
	}
	return;
}

static void set_linkmode(struct rpt_link *mylink, int linkmode)
{

	if (!mylink) return;
 	switch(linkmode)
	{
	    case LINKMODE_OFF:
		mylink->linkmode = 0;
		break;
	    case LINKMODE_ON:
		mylink->linkmode = 0x7fffffff;
		break;
	    case LINKMODE_FOLLOW:
		mylink->linkmode = 0x7ffffffe;
		break;
	    case LINKMODE_DEMAND:
		mylink->linkmode = 1;
		break;
	}
	return;
}

static void rpt_telem_select(struct rpt *myrpt, int command_source, struct rpt_link *mylink)
{
int	src;

	if (mylink && mylink->chan)
	{
		src = LINKMODE_GUI;
		if (mylink->phonemode) src = LINKMODE_PHONE;
		else if (!strncasecmp(mylink->chan->name,"echolink",8)) src = LINKMODE_ECHOLINK;
		else if (!strncasecmp(mylink->chan->name,"tlb",8)) src = LINKMODE_TLB;
		if (myrpt->p.linkmodedynamic[src] && (mylink->linkmode >= 1) && 
		    (mylink->linkmode < 0x7ffffffe))
				mylink->linkmode = LINK_HANG_TIME;
	}
	if (!myrpt->p.telemdynamic) return;
	if (myrpt->telemmode == 0) return;
	if (myrpt->telemmode == 0x7fffffff) return;
	myrpt->telemmode = TELEM_HANG_TIME;
	return;
}

static int altlink(struct rpt *myrpt,struct rpt_link *mylink)
{
	if (!myrpt) return(0);
	if (!mylink) return(0);
	if (!mylink->chan) return(0);
	if ((myrpt->p.duplex == 3) && mylink->phonemode && myrpt->keyed) return(0);
	/* if doesnt qual as a foreign link */
	if ((mylink->name[0] > '0') && (mylink->name[0] <= '9') &&
	    (!mylink->phonemode) &&
	    strncasecmp(mylink->chan->name,"echolink",8) &&
		strncasecmp(mylink->chan->name,"tlb",3)) return(0);
	if ((myrpt->p.duplex < 2) && (myrpt->tele.next == &myrpt->tele)) return(0);
	if (mylink->linkmode < 2) return(0);
	if (mylink->linkmode == 0x7fffffff) return(1);
	if (mylink->linkmode < 0x7ffffffe) return(1);
	if (myrpt->telemmode > 1) return(1);
	return(0);
}

static int altlink1(struct rpt *myrpt,struct rpt_link *mylink)
{
struct  rpt_tele *tlist;
int	nonlocals;

	if (!myrpt) return(0);
	if (!mylink) return(0);
	if (!mylink->chan) return(0);
	nonlocals = 0;
	tlist = myrpt->tele.next;
        if (tlist != &myrpt->tele)
        {
                while(tlist != &myrpt->tele)
		{
                        if ((tlist->mode == PLAYBACK) || 
			    (tlist->mode == STATS_GPS_LEGACY) ||
			      (tlist->mode == ID1) || 
				(tlist->mode == TEST_TONE)) nonlocals++;
			tlist = tlist->next;
		}
	}
	if ((!myrpt->p.duplex) || (!nonlocals)) return(0);
	/* if doesnt qual as a foreign link */
	if ((mylink->name[0] > '0') && (mylink->name[0] <= '9') &&
	    (!mylink->phonemode) &&
	    strncasecmp(mylink->chan->name,"echolink",8) &&
		strncasecmp(mylink->chan->name,"tlb",3)) return(1);
	if (mylink->linkmode < 2) return(0);
	if (mylink->linkmode == 0x7fffffff) return(1);
	if (mylink->linkmode < 0x7ffffffe) return(1);
	if (myrpt->telemmode > 1) return(1);
	return(0);
}

static void rpt_qwrite(struct rpt_link *l,struct ast_frame *f)
{
struct	ast_frame *f1;

	if (!l->chan) return;
	f1 = ast_frdup(f);
	memset(&f1->frame_list,0,sizeof(f1->frame_list));
	AST_LIST_INSERT_TAIL(&l->textq,f1,frame_list);
	return;
}

static int linkcount(struct rpt *myrpt)
{
	struct	rpt_link *l;
 	int numoflinks;

	numoflinks = 0;
	l = myrpt->links.next;
	while(l && (l != &myrpt->links)){
		if(numoflinks >= MAX_STAT_LINKS){
			ast_log(LOG_WARNING,
			"maximum number of links exceeds %d in rpt_do_stats()!",MAX_STAT_LINKS);
			break;
		}
		//if (l->name[0] == '0'){ /* Skip '0' nodes */
		//	l = l->next;
		//	continue;
		//}
		numoflinks++;
	 
		l = l->next;
	}
//	ast_log(LOG_NOTICE, "numoflinks=%i\n",numoflinks);
	return numoflinks;
}
/* Considers repeater received RSSI and all voter link RSSI information and
   set values in myrpt structure.
*/
static int FindBestRssi(struct rpt *myrpt)
{
	struct	rpt_link *l;
	struct	rpt_link *bl;
	int maxrssi;
 	int numoflinks;
	char newboss;

	bl=NULL;
	maxrssi=0;
	numoflinks=0;
	newboss=0;

	myrpt->voted_rssi=0;
	if(myrpt->votewinner&&myrpt->rxchankeyed)
		myrpt->voted_rssi=myrpt->rxrssi;
	else if(myrpt->voted_link!=NULL && myrpt->voted_link->lastrealrx)
		myrpt->voted_rssi=myrpt->voted_link->rssi;

	if(myrpt->rxchankeyed)
		maxrssi=myrpt->rxrssi;

	l = myrpt->links.next;
	while(l && (l != &myrpt->links)){
		if(numoflinks >= MAX_STAT_LINKS){
			ast_log(LOG_WARNING,
			"[%s] number of links exceeds limit of %d \n",myrpt->name,MAX_STAT_LINKS);
			break;
		}
		if(l->lastrealrx && (l->rssi > maxrssi))
		{
			maxrssi=l->rssi;
			bl=l;
		}
		l->votewinner=0;
		numoflinks++;
		l = l->next;
	}

	if( !myrpt->voted_rssi ||
	    (myrpt->voted_link==NULL && !myrpt->votewinner) ||
	    (maxrssi>(myrpt->voted_rssi+myrpt->p.votermargin))
	  )
	{
        newboss=1;
        myrpt->votewinner=0;
		if(bl==NULL && myrpt->rxchankeyed)
			myrpt->votewinner=1;
		else if(bl!=NULL)
			bl->votewinner=1;
		myrpt->voted_link=bl;
		myrpt->voted_rssi=maxrssi;
    }

	if(debug>4)
	    ast_log(LOG_NOTICE,"[%s] links=%i best rssi=%i from %s%s\n",
	        myrpt->name,numoflinks,maxrssi,bl==NULL?"rpt":bl->name,newboss?"*":"");

	return numoflinks;
}
/*
 * Retrieve a memory channel
 * Return 0 if sucessful,
 * -1 if channel not found,
 *  1 if parse error
 */
static int retrieve_memory(struct rpt *myrpt, char *memory)
{
	char tmp[15], *s, *s1, *s2, *val;

	if (debug)ast_log(LOG_NOTICE, "memory=%s block=%s\n",memory,myrpt->p.memory);

	val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.memory, memory);
	if (!val){
		return -1;
	}			
	strncpy(tmp,val,sizeof(tmp) - 1);
	tmp[sizeof(tmp)-1] = 0;

	s = strchr(tmp,',');
	if (!s)
		return 1; 
	*s++ = 0;
	s1 = strchr(s,',');
	if (!s1)
		return 1;
	*s1++ = 0;
	s2 = strchr(s1,',');
	if (!s2) s2 = s1;
	else *s2++ = 0;
	strlcpy(myrpt->freq, tmp, sizeof(myrpt->freq) - 1);
	strlcpy(myrpt->rxpl, s, sizeof(myrpt->rxpl) - 1);
	strlcpy(myrpt->txpl, s, sizeof(myrpt->rxpl) - 1);
	myrpt->remmode = REM_MODE_FM;
	myrpt->offset = REM_SIMPLEX;
	myrpt->powerlevel = REM_MEDPWR;
	myrpt->txplon = myrpt->rxplon = 0;
	myrpt->splitkhz = 0;
	if (s2 != s1) myrpt->splitkhz = atoi(s1);
	while(*s2){
		switch(*s2++){
			case 'A':
			case 'a':
				strcpy(myrpt->rxpl, "100.0");
				strcpy(myrpt->txpl, "100.0");
				myrpt->remmode = REM_MODE_AM;	
				break;
			case 'B':
			case 'b':
				strcpy(myrpt->rxpl, "100.0");
				strcpy(myrpt->txpl, "100.0");
				myrpt->remmode = REM_MODE_LSB;
				break;
			case 'F':
				myrpt->remmode = REM_MODE_FM;
				break;
			case 'L':
			case 'l':
				myrpt->powerlevel = REM_LOWPWR;
				break;					
			case 'H':
			case 'h':
				myrpt->powerlevel = REM_HIPWR;
				break;
					
			case 'M':
			case 'm':
				myrpt->powerlevel = REM_MEDPWR;
				break;
						
			case '-':
				myrpt->offset = REM_MINUS;
				break;
						
			case '+':
				myrpt->offset = REM_PLUS;
				break;
						
			case 'S':
			case 's':
				myrpt->offset = REM_SIMPLEX;
				break;
						
			case 'T':
			case 't':
				myrpt->txplon = 1;
				break;
						
			case 'R':
			case 'r':
				myrpt->rxplon = 1;
				break;

			case 'U':
			case 'u':
				strcpy(myrpt->rxpl, "100.0");
				strcpy(myrpt->txpl, "100.0");
				myrpt->remmode = REM_MODE_USB;
				break;
			default:
				return 1;
		}
	}
	return 0;
}
/*

*/

/*
 * Routine that hangs up all links and frees all threads related to them
 * hence taking a "bird bath".  Makes a lot of noise/cleans up the mess
 */
static void birdbath(struct rpt *myrpt)
{
	struct rpt_tele *telem;
	if(debug > 2)
		ast_log(LOG_NOTICE, "birdbath!!");
	rpt_mutex_lock(&myrpt->lock);
	telem = myrpt->tele.next;
	while(telem != &myrpt->tele)
	{
		if (telem->mode == PARROT) ast_softhangup(telem->chan,AST_SOFTHANGUP_DEV);
		telem = telem->next;
	}
	rpt_mutex_unlock(&myrpt->lock);
}

/* must be called locked */
static void cancel_pfxtone(struct rpt *myrpt)
{
	struct rpt_tele *telem;
	if(debug > 2)
		ast_log(LOG_NOTICE, "cancel_pfxfone!!");
	telem = myrpt->tele.next;
	while(telem != &myrpt->tele)
	{
		if (telem->mode == PFXTONE) ast_softhangup(telem->chan,AST_SOFTHANGUP_DEV);
		telem = telem->next;
	}
}

static void do_dtmf_phone(struct rpt *myrpt, struct rpt_link *mylink, char c)
{
struct        rpt_link *l;

       l = myrpt->links.next;
       /* go thru all the links */
       while(l != &myrpt->links)
       {
               if (!l->phonemode)
               {
                       l = l->next;
                       continue;
               }
               /* dont send to self */
               if (mylink && (l == mylink))
               {
                       l = l->next;
                       continue;
               }
#ifdef	NEW_ASTERISK
               if (l->chan) ast_senddigit(l->chan,c,0);
#else
               if (l->chan) ast_senddigit(l->chan,c);
#endif
               l = l->next;
       }
       return;
}

/* node logging function */
static void donodelog(struct rpt *myrpt,char *str)
{
struct nodelog *nodep;
char	datestr[100];

	if (!myrpt->p.archivedir) return;
	nodep = (struct nodelog *)ast_malloc(sizeof(struct nodelog));
	if (nodep == NULL)
	{
		ast_log(LOG_ERROR,"Cannot get memory for node log");
		return;
	}
	time(&nodep->timestamp);
	strncpy(nodep->archivedir,myrpt->p.archivedir,
		sizeof(nodep->archivedir) - 1);
	strftime(datestr,sizeof(datestr) - 1,"%Y%m%d%H%M%S",
		localtime(&nodep->timestamp));
	snprintf(nodep->str,sizeof(nodep->str) - 1,"%s %s,%s\n",
		myrpt->name,datestr,str);
	ast_mutex_lock(&nodeloglock);
	insque((struct qelem *) nodep, (struct qelem *) nodelog.prev);
	ast_mutex_unlock(&nodeloglock);
}

/* must be called locked */
static void do_dtmf_local(struct rpt *myrpt, char c)
{
int	i;
char	digit;

	if (c)
	{
		snprintf(myrpt->dtmf_local_str + strlen(myrpt->dtmf_local_str),sizeof(myrpt->dtmf_local_str) - 1,"%c",c);
		if (!myrpt->dtmf_local_timer) 
			 myrpt->dtmf_local_timer = DTMF_LOCAL_STARTTIME;
	}
	/* if at timeout */
	if (myrpt->dtmf_local_timer == 1)
	{
		if(debug > 6)
			ast_log(LOG_NOTICE,"time out dtmf_local_timer=%i\n",myrpt->dtmf_local_timer);

		/* if anything in the string */
		if (myrpt->dtmf_local_str[0])
		{
			digit = myrpt->dtmf_local_str[0];
			myrpt->dtmf_local_str[0] = 0;
			for(i = 1; myrpt->dtmf_local_str[i]; i++)
			{
				myrpt->dtmf_local_str[i - 1] =
					myrpt->dtmf_local_str[i];
			}
			myrpt->dtmf_local_str[i - 1] = 0;
			myrpt->dtmf_local_timer = DTMF_LOCAL_TIME;
			rpt_mutex_unlock(&myrpt->lock);
			if (!strncasecmp(myrpt->txchannel->name,"rtpdir",6))
			{
#ifdef	NEW_ASTERISK
				ast_senddigit(myrpt->txchannel,digit,0);
#else
				ast_senddigit(myrpt->txchannel,digit);
#endif
			} 
			else
			{
				if (digit >= '0' && digit <='9')
					ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[digit-'0'], 0);
				else if (digit >= 'A' && digit <= 'D')
					ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[digit-'A'+10], 0);
				else if (digit == '*')
					ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[14], 0);
				else if (digit == '#')
					ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[15], 0);
				else {
					/* not handled */
					ast_log(LOG_DEBUG, "Unable to generate DTMF tone '%c' for '%s'\n", digit, myrpt->txchannel->name);
				}
			}
			rpt_mutex_lock(&myrpt->lock);
		}
		else
		{
			myrpt->dtmf_local_timer = 0;
		}
	}
}

/*
 * Routine to set the Data Terminal Ready (DTR) pin on a serial interface
*/

static int setdtr(struct rpt *myrpt,int fd, int enable)
{
struct termios mode;

	if (fd < 0) return -1;
	if (tcgetattr(fd, &mode)) {
		ast_log(LOG_WARNING, "Unable to get serial parameters for dtr: %s\n", strerror(errno));
		return -1;
	}
	if (enable)
	{
		cfsetspeed(&mode, myrpt->p.iospeed);
	}
	else
	{
		cfsetspeed(&mode, B0);
		usleep(100000);
	}
	if (tcsetattr(fd, TCSADRAIN, &mode)) {
		ast_log(LOG_WARNING, "Unable to set serial parameters for dtr: %s\n", strerror(errno));
		return -1;
	}
	if (enable) usleep(100000);
	return 0;
}

/* 
 * open the serial port
 */
 
static int openserial(struct rpt *myrpt,char *fname)
{
	struct termios mode;
	int fd;

	fd = open(fname,O_RDWR);
	if (fd == -1)
	{
		ast_log(LOG_WARNING,"Cannot open serial port %s\n",fname);
		return -1;
	}
	memset(&mode, 0, sizeof(mode));
	if (tcgetattr(fd, &mode)) {
		ast_log(LOG_WARNING, "Unable to get serial parameters on %s: %s\n", fname, strerror(errno));
		return -1;
	}
#ifndef	SOLARIS
	cfmakeraw(&mode);
#else
        mode.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                        |INLCR|IGNCR|ICRNL|IXON);
        mode.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
        mode.c_cflag &= ~(CSIZE|PARENB|CRTSCTS);
        mode.c_cflag |= CS8;
	mode.c_cc[VTIME] = 3;
	mode.c_cc[VMIN] = 1; 
#endif

	cfsetispeed(&mode, myrpt->p.iospeed);
	cfsetospeed(&mode, myrpt->p.iospeed);
	if (tcsetattr(fd, TCSANOW, &mode)) 
		ast_log(LOG_WARNING, "Unable to set serial parameters on %s: %s\n", fname, strerror(errno));
	if(!strcmp(myrpt->remoterig, remote_rig_kenwood)) setdtr(myrpt,fd,0); 
	usleep(100000);
	if (debug)ast_log(LOG_NOTICE,"Opened serial port %s\n",fname);
	return(fd);	
}


/*
 * Process DTMF keys passed
 */ 

static void local_dtmfkey_helper(struct rpt *myrpt,char c)
{
int	i;
char	*val;

	i = strlen(myrpt->dtmfkeybuf);
	if (i >= (sizeof(myrpt->dtmfkeybuf) - 1)) return;
	myrpt->dtmfkeybuf[i++] = c;
	myrpt->dtmfkeybuf[i] = 0;
	val = (char *) ast_variable_retrieve(myrpt->cfg, 
		myrpt->p.dtmfkeys,myrpt->dtmfkeybuf);
	if (!val) return;
	strncpy(myrpt->curdtmfuser,val,MAXNODESTR - 1);
	myrpt->dtmfkeyed = 1;
	myrpt->dtmfkeybuf[0] = 0;
	return;
}

static void mdc1200_notify(struct rpt *myrpt,char *fromnode, char *data)
{
	FILE *fp;
	char str[50];
	struct flock fl;
	time_t	t;

	rpt_manager_trigger(myrpt, "MDC-1200", data);

	if (!fromnode)
	{
		ast_verbose("Got MDC-1200 data %s from local system (%s)\n",
			data,myrpt->name);
		if (myrpt->p.mdclog) 
		{
			fp = fopen(myrpt->p.mdclog,"a");
			if (!fp)
			{
				ast_log(LOG_ERROR,"Cannot open MDC1200 log file %s\n",myrpt->p.mdclog);
				return;
			}
			fl.l_type = F_WRLCK;
			fl.l_whence = SEEK_SET;
			fl.l_start = 0;
			fl.l_len = 0;
			fl.l_pid = pthread_self();
			if (fcntl(fileno(fp),F_SETLKW,&fl) == -1)
			{
				ast_log(LOG_ERROR,"Cannot get lock on MDC1200 log file %s\n",myrpt->p.mdclog);			
				fclose(fp);
				return;
			}
			time(&t);
			strftime(str,sizeof(str) - 1,"%Y%m%d%H%M%S",localtime(&t));
			fprintf(fp,"%s %s %s\n",str ,myrpt->name,data);
			fl.l_type = F_UNLCK;
			fcntl(fileno(fp),F_SETLK,&fl);
			fclose(fp);
		}
	}
	else
	{
		ast_verbose("Got MDC-1200 data %s from node %s (%s)\n",
			data,fromnode,myrpt->name);
	}
}

#ifdef	_MDC_DECODE_H_

static void mdc1200_send(struct rpt *myrpt, char *data)
{
struct rpt_link *l;
struct	ast_frame wf;
char	str[200];


	if (!myrpt->keyed) return;

	sprintf(str,"I %s %s",myrpt->name,data);

	wf.frametype = AST_FRAME_TEXT;
	wf.subclass = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	AST_FRAME_DATA(wf) = str; 
	wf.datalen = strlen(str) + 1;  // Isuani, 20141001
	wf.samples = 0;
	wf.src = "mdc1200_send";


	l = myrpt->links.next;
	/* otherwise, send it to all of em */
	while(l != &myrpt->links)
	{
		/* Dont send to IAXRPT client, unless main channel is Voter */
		if (((l->name[0] == '0') && strncasecmp(myrpt->rxchannel->name,"voter/", 6)) || (l->phonemode))
		{
			l = l->next;
			continue;
		}
		if (l->chan) rpt_qwrite(l,&wf); 
		l = l->next;
	}
	return;
}

/*
	rssi_send() Send rx rssi out on all links.
*/
static void rssi_send(struct rpt *myrpt)
{
        struct rpt_link *l;
        struct	ast_frame wf;
        char	str[200];
	sprintf(str,"R %i",myrpt->rxrssi);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	wf.src = "rssi_send";

	l = myrpt->links.next;
	/* otherwise, send it to all of em */
	while(l != &myrpt->links)
	{
		if (l->name[0] == '0')
		{
			l = l->next;
			continue;
		}
		wf.data = str;
		if(debug>5)ast_log(LOG_NOTICE, "[%s] rssi=%i to %s\n", myrpt->name,myrpt->rxrssi,l->name);
		if (l->chan) rpt_qwrite(l,&wf);
		l = l->next;
	}
}

static void mdc1200_cmd(struct rpt *myrpt, char *data)
{
	char busy,*myval;
	int i;

	busy = 0;
	if ((data[0] == 'I') && (!strcmp(data,myrpt->lastmdc))) return;
	myval = (char *) my_variable_match(myrpt->cfg, myrpt->p.mdcmacro, data);
	if (myval) 
	{
		if (option_verbose) ast_verbose("MDCMacro for %s doing %s on node %s\n",data,myval,myrpt->name);
		if ((*myval == 'K') || (*myval == 'k'))
		{
			if (!myrpt->keyed)
			{
				for(i = 1; myval[i]; i++) local_dtmfkey_helper(myrpt,myval[i]);
			}
			return;
		}
		if (!myrpt->keyed) return;
		rpt_mutex_lock(&myrpt->lock);
		if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(myval))
		{
			rpt_mutex_unlock(&myrpt->lock);
			busy=1;
		}
		if(!busy)
		{
			myrpt->macrotimer = MACROTIME;
			strncat(myrpt->macrobuf,myval,MAXMACRO - 1);
		}
		rpt_mutex_unlock(&myrpt->lock);
	}
 	if ((data[0] == 'I') && (!busy)) strcpy(myrpt->lastmdc,data);
	return;
}

#ifdef	_MDC_ENCODE_H_

static void mdc1200_ack_status(struct rpt *myrpt, short UnitID)
{
struct	mdcparams *mdcp;

	mdcp = ast_calloc(1,sizeof(struct mdcparams));
	if (!mdcp)
	{
		ast_log(LOG_ERROR,"Cannot alloc!!\n");
		return;
	}
	memset(mdcp,0,sizeof(&mdcp));
	mdcp->type[0] = 'A';
	mdcp->UnitID = UnitID;
	rpt_telemetry(myrpt,MDC1200,(void *)mdcp);
	return;
}

#endif
#endif

/*
 * Translate function
 */
 
static char func_xlat(struct rpt *myrpt,char c,struct rpt_xlat *xlat)
{
time_t	now;
int	gotone;

	time(&now);
	gotone = 0;
	/* if too much time, reset the skate machine */
	if ((now - xlat->lastone) > MAXXLATTIME)
	{
		xlat->funcindex = xlat->endindex = 0;
	}
	if (xlat->funccharseq[0] && (c == xlat->funccharseq[xlat->funcindex++]))
	{
		time(&xlat->lastone);
		gotone = 1;
		if (!xlat->funccharseq[xlat->funcindex])
		{
			xlat->funcindex = xlat->endindex = 0;
			return(myrpt->p.funcchar);
		}
	} else xlat->funcindex = 0;
	if (xlat->endcharseq[0] && (c == xlat->endcharseq[xlat->endindex++]))
	{
		time(&xlat->lastone);
		gotone = 1;
		if (!xlat->endcharseq[xlat->endindex])
		{
			xlat->funcindex = xlat->endindex = 0;
			return(myrpt->p.endchar);
		}
	} else xlat->endindex = 0;
	/* if in middle of decode seq, send nothing back */
	if (gotone) return(0);
	/* if no pass chars specified, return em all */
	if (!xlat->passchars[0]) return(c);
	/* if a "pass char", pass it */
	if (strchr(xlat->passchars,c)) return(c);
	return(0);
}

/*
 * Return a pointer to the first non-whitespace character
 */

static char *eatwhite(char *s)
{
	while((*s == ' ') || (*s == 0x09)){ /* get rid of any leading white space */
		if(!*s)
			break;
		s++;
	}
	return s;
}

/*
 * Function to translate characters to APRSTT data
 */

static char aprstt_xlat(char *instr,char *outstr)
{
int	i,j;
char	b,c,lastnum,overlay,cksum;
static char a_xlat[] = {0,0,'A','D','G','J','M','P','T','W'};
static char b_xlat[] = {0,0,'B','E','H','K','N','Q','U','X'};
static char c_xlat[] = {0,0,'C','F','I','L','O','R','V','Y'};
static char d_xlat[] = {0,0,0,0,0,0,0,'S',0,'Z'};

	if (strlen(instr) < 4) return 0;
	lastnum = 0;
	for(i = 1; instr[i + 2]; i++)
	{
		c = instr[i];
		switch (c)
		{
		    case 'A' :
			if (!lastnum) return 0;
			b = a_xlat[lastnum - '0'];
			if (!b) return 0;
			*outstr++ = b;
			lastnum = 0;
			break;
		    case 'B' :
			if (!lastnum) return 0;
			b = b_xlat[lastnum - '0'];
			if (!b) return 0;
			*outstr++ = b;
			lastnum = 0;
			break;
		    case 'C' :
			if (!lastnum) return 0;
			b = c_xlat[lastnum - '0'];
			if (!b) return 0;
			*outstr++ = b;
			lastnum = 0;
			break;
		    case 'D' :
			if (!lastnum) return 0;
			b = d_xlat[lastnum - '0'];
			if (!b) return 0;
			*outstr++ = b;
			lastnum = 0;
			break;
		    case '0':
		    case '1':
		    case '2':
		    case '3':
		    case '4':
		    case '5':
		    case '6':
		    case '7':
		    case '8':
		    case '9':
			if (lastnum) *outstr++ = lastnum;
			lastnum = c;
			break;
		    default:
			return 0;
		}
	}
	*outstr = 0;
	overlay = instr[i++];
	cksum = instr[i];	
	for(i = 0,j = 0; instr[i + 1]; i++)
	{
		if ((instr[i] >= '0') && (instr[i] <= '9')) j += (instr[i] - '0');
		else if ((instr[i] >= 'A') && (instr[i] <= 'D')) j += (instr[i] - 'A') + 10;
	}
	if ((cksum - '0') != (j % 10)) return 0;
	return overlay;
}

static char *strupr(char *instr)
{
char *str = instr;
        while (*str)
           {
                *str = toupper(*str);
                str++;
           }
        return(instr);
}

/*
* Break up a delimited string into a table of substrings
*
* str - delimited string ( will be modified )
* strp- list of pointers to substrings (this is built by this function), NULL will be placed at end of list
* limit- maximum number of substrings to process
* delim- user specified delimeter
* quote- user specified quote for escaping a substring. Set to zero to escape nothing.
*
* Note: This modifies the string str, be suer to save an intact copy if you need it later.
*
* Returns number of substrings found.
*/
	

static int explode_string(char *str, char *strp[], int limit, char delim, char quote)
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
		if(quote)
		{
                	if (*str == quote)
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
		}	
                if ((*str == delim) && (!inquo))
                {
                        *str = 0;
			l++;
                        strp[i++] = str + 1;
                }
        }
        strp[i] = 0;
        return(i);

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
	return explode_string(str, strp, limit, DELIMCHR, QUOTECHR);
}

static char *string_toupper(char *str)
{
int	i;

	for(i = 0; str[i]; i++)
		if (islower(str[i])) str[i] = toupper(str[i]);
	return str;
}

static int elink_cmd(char *cmd, char *outstr, int outlen)
{
FILE	*tf;

	tf = tmpfile();
	if (!tf) return -1;
	if (debug)  ast_log(LOG_DEBUG,"elink_cmd sent %s\n",cmd);
	ast_cli_command(fileno(tf),cmd);
	rewind(tf);
	outstr[0] = 0;
	if (!fgets(outstr,outlen,tf)) 
	{
		fclose(tf);
		return 0;
	}
	fclose(tf);
	if (!strlen(outstr)) return 0;
	if (outstr[strlen(outstr) - 1] == '\n')
		outstr[strlen(outstr) - 1] = 0;
	if (debug)  ast_log(LOG_DEBUG,"elink_cmd ret. %s\n",outstr);
	return(strlen(outstr));
}

static int elink_db_get(char *lookup, char c, char *nodenum,char *callsign, char *ipaddr)
{
char	str[512],str1[100],*strs[5];
int	n;

	snprintf(str,sizeof(str) - 1,"echolink dbget %c %s",c,lookup);
	n = elink_cmd(str,str1,sizeof(str1));
	if (n < 1) return(n);
	n = explode_string(str1, strs, 5, '|', '\"');
	if (n < 3) return(0);
	if (nodenum) strcpy(nodenum,strs[0]);
	if (callsign) strcpy(callsign,strs[1]);
	if (ipaddr) strcpy(ipaddr,strs[2]);
	return(1);
}

static int tlb_node_get(char *lookup, char c, char *nodenum,char *callsign, char *ipaddr, char *port)
{
char	str[100],str1[100],*strs[6];
int	n;

	snprintf(str,sizeof(str) - 1,"tlb nodeget %c %s",c,lookup);
	n = elink_cmd(str,str1,sizeof(str1));
	if (n < 1) return(n);
	n = explode_string(str1, strs, 6, '|', '\"');
	if (n < 4) return(0);
	if (nodenum) strcpy(nodenum,strs[0]);
	if (callsign) strcpy(callsign,strs[1]);
	if (ipaddr) strcpy(ipaddr,strs[2]);
	if (port) strcpy(port,strs[3]);
	return(1);
}

/*
	send asterisk frame text message on the current tx channel
*/
static int send_usb_txt(struct rpt *myrpt, char *txt) 
{
	struct ast_frame wf;
 
	/* if (debug) */ ast_log(LOG_NOTICE, "send_usb_txt %s\n",txt);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(txt) + 1;
	AST_FRAME_DATA(wf) = txt;
	wf.samples = 0;
	wf.src = "send_usb_txt";
	ast_write(myrpt->txchannel,&wf); 
	return 0;
}
/*
	send asterisk frame text message on the current tx channel
*/
static int send_link_pl(struct rpt *myrpt, char *txt) 
{
	struct ast_frame wf;
	struct	rpt_link *l;
	char	str[300];
 
	if (!strcmp(myrpt->p.ctgroup,"0")) return 0;
	snprintf(str, sizeof(str), "C %s %s %s", myrpt->name, myrpt->p.ctgroup, txt);
/* if (debug) */ ast_log(LOG_NOTICE, "send_link_pl %s\n",str);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	AST_FRAME_DATA(wf) = str;
	wf.samples = 0;
	wf.src = "send_link_pl";
	l = myrpt->links.next;
	while(l && (l != &myrpt->links))
	{
		if ((l->chan) && l->name[0] && (l->name[0] != '0'))
		{
			ast_write(l->chan,&wf); 
		}
		l = l->next;
	}
	return 0;
}

/* must be called locked */
static void __mklinklist(struct rpt *myrpt, struct rpt_link *mylink, char *buf,int flag)
{
struct rpt_link *l;
char mode;
int	i,spos;

	buf[0] = 0; /* clear output buffer */
	if (myrpt->remote) return;
	/* go thru all links */
	for(l = myrpt->links.next; l != &myrpt->links; l = l->next)
	{
		/* if is not a real link, ignore it */
		if (l->name[0] == '0') continue;
		if (l->mode > 1) continue; /* dont report local modes */
		/* dont count our stuff */
		if (l == mylink) continue;
		if (mylink && (!strcmp(l->name,mylink->name))) continue;
		/* figure out mode to report */
		mode = 'T'; /* use Tranceive by default */
		if (!l->mode) mode = 'R'; /* indicate RX for our mode */
		if (!l->thisconnected) 	mode = 'C'; /* indicate connecting */
		spos = strlen(buf); /* current buf size (b4 we add our stuff) */
		if (spos)
		{
			strcat(buf,",");
			spos++;
		}
		if (flag)
		{
			snprintf(buf + spos,MAXLINKLIST - spos,
				"%s%c%c",l->name,mode,(l->lastrx1) ? 'K' : 'U');
		}
		else
		{
			/* add nodes into buffer */
			if (l->linklist[0])
			{
				snprintf(buf + spos,MAXLINKLIST - spos,
					"%c%s,%s",mode,l->name,l->linklist);
			}
			else /* if no nodes, add this node into buffer */
			{
				snprintf(buf + spos,MAXLINKLIST - spos,
					"%c%s",mode,l->name);
			}	
		}
		/* if we are in tranceive mode, let all modes stand */
		if (mode == 'T') continue;
		/* downgrade everyone on this node if appropriate */
		for(i = spos; buf[i]; i++)
		{
			if (buf[i] == 'T') buf[i] = mode;
			if ((buf[i] == 'R') && (mode == 'C')) buf[i] = mode;
		}
	}
	return;
}

/* must be called locked */
static void __kickshort(struct rpt *myrpt)
{
struct rpt_link *l;

	for(l = myrpt->links.next; l != &myrpt->links; l = l->next)
	{
		/* if is not a real link, ignore it */
		if (l->name[0] == '0') continue;
		l->linklisttimer = LINKLISTSHORTTIME;
	}
	myrpt->linkposttimer = LINKPOSTSHORTTIME;
	myrpt->lastgpstime = 0;
	return;
}

/*
 * Routine to process events for rpt_master threads
 */

static void rpt_event_process(struct rpt *myrpt)
{
char	*myval,*argv[5],*cmpvar,*var,*var1,*cmd,c;
char	buf[1000],valbuf[500],action;
int	i,l,argc,varp,var1p,thisAction,maxActions;
struct ast_variable *v;
struct ast_var_t *newvariable;


	if (!starttime) return;
	for (v = ast_variable_browse(myrpt->cfg, myrpt->p.events); v; v = v->next)
	{
		/* make a local copy of the value of this entry */
		myval = ast_strdupa(v->value);
		/* separate out specification into pipe-delimited fields */
		argc = ast_app_separate_args(myval, '|', argv, sizeof(argv) / sizeof(argv[0]));
		if (argc < 1) continue;
		if (argc != 3)
		{
			ast_log(LOG_ERROR,"event exec item malformed: %s\n",v->value);
			continue;
		}
		action = toupper(*argv[0]);
		if (!strchr("VGFCS",action))
		{
			ast_log(LOG_ERROR,"Unrecognized event action (%c) in exec item malformed: %s\n",action,v->value);
			continue;
		}
		/* start indicating no command to do */
		cmd = NULL;
		c = toupper(*argv[1]);
		if (c == 'E') /* if to merely evaluate the statement */
		{
			if (!strncasecmp(v->name,"RPT",3))
			{
				ast_log(LOG_ERROR,"%s is not a valid name for an event variable!!!!\n",v->name);
				continue;
			}
			if (!strncasecmp(v->name,"XX_",3))
			{
				ast_log(LOG_ERROR,"%s is not a valid name for an event variable!!!!\n",v->name);
				continue;
			}
			/* see if this var exists yet */
			myval = (char *) pbx_builtin_getvar_helper(myrpt->rxchannel,v->name);
			/* if not, set it to zero, in case of the value being self-referenced */
			if (!myval) pbx_builtin_setvar_helper(myrpt->rxchannel,v->name,"0");
			snprintf(valbuf,sizeof(valbuf) - 1,"$[ %s ]",argv[2]);
			buf[0] = 0;
			pbx_substitute_variables_helper(myrpt->rxchannel,
				valbuf,buf,sizeof(buf) - 1);
			if (pbx_checkcondition(buf)) cmd = "TRUE";
		}
		else
		{
			var = (char *) pbx_builtin_getvar_helper(myrpt->rxchannel,argv[2]);
			if (!var)
			{
				ast_log(LOG_ERROR,"Event variable %s not found\n",argv[2]);
				continue;
			}
			/* set to 1 if var is true */
			varp = ((pbx_checkcondition(var) > 0));
			for(i = 0; (!cmd) && (c = *(argv[1] + i)); i++)
			{
				cmpvar = (char *)ast_malloc(strlen(argv[2]) + 10);
				if (!cmpvar)
				{
					ast_log(LOG_NOTICE,"Cannot malloc()\n");
					return;
				}
				sprintf(cmpvar,"XX_%s",argv[2]);
				var1 = (char *) pbx_builtin_getvar_helper(myrpt->rxchannel,cmpvar);
				var1p = !varp; /* start with it being opposite */
				if (var1)
				{
					/* set to 1 if var is true */
					var1p = ((pbx_checkcondition(var1) > 0));
				}
//				pbx_builtin_setvar_helper(myrpt->rxchannel,cmpvar,var);
				ast_free(cmpvar);			
				c = toupper(c);
				if (!strchr("TFNI",c))
				{
					ast_log(LOG_ERROR,"Unrecognized event type (%c) in exec item malformed: %s\n",c,v->value);
					continue;
				}
				switch(c)
				{
				    case 'N': /* if no change */
					if (var1 && (varp == var1p)) cmd = (char *)v->name;
					break;
				    case 'I': /* if didnt exist (initial state) */
					if (!var1) cmd = (char *)v->name;
					break;
				    case 'F': /* transition to false */
					if (var1 && (var1p == 1) && (varp == 0)) cmd = (char *)v->name;
					break;
				    case 'T': /* transition to true */
					if ((var1p == 0) && (varp == 1)) cmd = (char *)v->name;
					break;
				}
			}
		}
		if (action == 'V') /* set a variable */
		{
			pbx_builtin_setvar_helper(myrpt->rxchannel,v->name,(cmd) ? "1" : "0");
			continue;
		}
		if (action == 'G') /* set a global variable */
		{
			pbx_builtin_setvar_helper(NULL,v->name,(cmd) ? "1" : "0");
			continue;
		}
		/* if not command to execute, go to next one */
		if (!cmd) continue;
		if (action == 'F') /* excecute a function */
		{
			rpt_mutex_lock(&myrpt->lock);
			if ((MAXMACRO - strlen(myrpt->macrobuf)) >= strlen(cmd))
			{
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Event on node %s doing macro %s for condition %s\n",
						myrpt->name,cmd,v->value);
				myrpt->macrotimer = MACROTIME;
				strncat(myrpt->macrobuf,cmd,MAXMACRO - 1);
			}
			else
			{
				ast_log(LOG_NOTICE,"Could not execute event %s for %s: Macro buffer overflow\n",cmd,argv[1]);
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
		if (action == 'C') /* excecute a command */
		{

			/* make a local copy of the value of this entry */
			myval = ast_strdupa(cmd);
			/* separate out specification into comma-delimited fields */
			argc = ast_app_separate_args(myval, ',', argv, sizeof(argv) / sizeof(argv[0]));
			if (argc < 1)
			{
				ast_log(LOG_ERROR,"event exec rpt command item malformed: %s\n",cmd);
				continue;
			}
			/* Look up the action */
			l = strlen(argv[0]);
			thisAction = -1;
			maxActions = sizeof(function_table)/sizeof(struct function_table_tag);
			for(i = 0 ; i < maxActions; i++)
			{
				if(!strncasecmp(argv[0], function_table[i].action, l))
				{
					thisAction = i;
					break;
				} 
			} 
			if (thisAction < 0)
			{
				ast_log(LOG_ERROR, "Unknown action name %s.\n", argv[0]);
				continue;
			} 
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Event on node %s doing rpt command %s for condition %s\n",
					myrpt->name,cmd,v->value);
			rpt_mutex_lock(&myrpt->lock);
			if (myrpt->cmdAction.state == CMD_STATE_IDLE)
			{
				myrpt->cmdAction.state = CMD_STATE_BUSY;
				myrpt->cmdAction.functionNumber = thisAction;
				myrpt->cmdAction.param[0] = 0;
				if (argc > 1)
					strlcpy(myrpt->cmdAction.param, argv[1], MAXDTMF-1);
				myrpt->cmdAction.digits[0] = 0;
				if (argc > 2)
					strlcpy(myrpt->cmdAction.digits, argv[2], MAXDTMF-1);
				myrpt->cmdAction.command_source = SOURCE_RPT;
				myrpt->cmdAction.state = CMD_STATE_READY;
			} 
			else
			{
				ast_log(LOG_NOTICE,"Could not execute event %s for %s: Command buffer in use\n",cmd,argv[1]);
			}
			rpt_mutex_unlock(&myrpt->lock);
			continue;
		}
		if (action == 'S') /* excecute a shell command */
		{
			char *cp;

			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Event on node %s doing shell command %s for condition %s\n",
					myrpt->name,cmd,v->value);
			cp = ast_malloc(strlen(cmd) + 10);
			if (!cp)
			{
				ast_log(LOG_NOTICE,"Unable to alloc");
				return;
			}
			memset(cp,0,strlen(cmd) + 10);
			sprintf(cp,"%s &",cmd);
			ast_safe_system(cp);
			free(cp);
			continue;
		}
	}
	for (v = ast_variable_browse(myrpt->cfg, myrpt->p.events); v; v = v->next)
	{
		/* make a local copy of the value of this entry */
		myval = ast_strdupa(v->value);
		/* separate out specification into pipe-delimited fields */
		argc = ast_app_separate_args(myval, '|', argv, sizeof(argv) / sizeof(argv[0]));
		if (argc != 3) continue;
		action = toupper(*argv[0]);
		if (!strchr("VGFCS",action)) continue;
		c = *argv[1];
		if (c == 'E') continue;
		var = (char *) pbx_builtin_getvar_helper(myrpt->rxchannel,argv[2]);
		if (!var) continue;
		/* set to 1 if var is true */
		varp = ((pbx_checkcondition(var) > 0));
		cmpvar = (char *)ast_malloc(strlen(argv[2]) + 10);
		if (!cmpvar)
		{
			ast_log(LOG_NOTICE,"Cannot malloc()\n");
			return;
		}
		sprintf(cmpvar,"XX_%s",argv[2]);
		var1 = (char *) pbx_builtin_getvar_helper(myrpt->rxchannel,cmpvar);
		pbx_builtin_setvar_helper(myrpt->rxchannel,cmpvar,var);
		ast_free(cmpvar);			
	}
	if (option_verbose < 5) return;
	i = 0;
	ast_verbose("Node Variable dump for node %s:\n",myrpt->name);
	ast_channel_lock(myrpt->rxchannel);
	AST_LIST_TRAVERSE (&myrpt->rxchannel->varshead, newvariable, entries) {
		i++;
		ast_verbose("   %s=%s\n", ast_var_name(newvariable), ast_var_value(newvariable));
	}
	ast_channel_unlock(myrpt->rxchannel);
	ast_verbose("    -- %d variables\n", i);
	return;
}

/*
 * Routine to update boolean values used in currently referenced rpt structure
 */
 

static void rpt_update_boolean(struct rpt *myrpt,char *varname, int newval)
{
char	buf[10];

	if ((!varname) || (!*varname)) return;
	buf[0] = '0';
	buf[1] = 0;
	if (newval > 0) buf[0] = '1';
	pbx_builtin_setvar_helper(myrpt->rxchannel, varname, buf);
	rpt_manager_trigger(myrpt, varname, buf);
	if (newval >= 0) rpt_event_process(myrpt);
	return;
}

/*
 * Updates the active links (channels) list that that the repeater has
 */
 

static void rpt_update_links(struct rpt *myrpt)
{
char buf[MAXLINKLIST],obuf[MAXLINKLIST + 20],*strs[MAXLINKLIST];
int	n;

	ast_mutex_lock(&myrpt->lock);
	__mklinklist(myrpt,NULL,buf,1);
	ast_mutex_unlock(&myrpt->lock);
	/* parse em */
	n = finddelim(strdupa(buf),strs,MAXLINKLIST);
	if (n) snprintf(obuf,sizeof(obuf) - 1,"%d,%s",n,buf);
	else strcpy(obuf,"0");
	pbx_builtin_setvar_helper(myrpt->rxchannel,"RPT_ALINKS",obuf);
	rpt_manager_trigger(myrpt, "RPT_ALINKS", obuf);
	snprintf(obuf,sizeof(obuf) - 1,"%d",n);
	pbx_builtin_setvar_helper(myrpt->rxchannel,"RPT_NUMALINKS",obuf);
	rpt_manager_trigger(myrpt, "RPT_NUMALINKS", obuf);
	ast_mutex_lock(&myrpt->lock);
	__mklinklist(myrpt,NULL,buf,0);
	ast_mutex_unlock(&myrpt->lock);
	/* parse em */
	n = finddelim(strdupa(buf),strs,MAXLINKLIST);
	if (n) snprintf(obuf,sizeof(obuf) - 1,"%d,%s",n,buf);
	else strcpy(obuf,"0");
	pbx_builtin_setvar_helper(myrpt->rxchannel,"RPT_LINKS",obuf);
	rpt_manager_trigger(myrpt, "RPT_LINKS", obuf);
	snprintf(obuf,sizeof(obuf) - 1,"%d",n);
	pbx_builtin_setvar_helper(myrpt->rxchannel,"RPT_NUMLINKS",obuf);
	rpt_manager_trigger(myrpt, "RPT_NUMLINKS", obuf);
	rpt_event_process(myrpt);
	return;
}

static void dodispgm(struct rpt *myrpt,char *them)
{
char 	*a;
int	i;

	if (!myrpt->p.discpgm) return;
	i = strlen(them) + strlen(myrpt->p.discpgm) + 100;
	a = ast_malloc(i);
	if (!a) 
	{
		ast_log(LOG_NOTICE,"Unable to alloc");
		return;
	}
	memset(a,0,i);
	sprintf(a,"%s %s %s &",myrpt->p.discpgm,
		myrpt->name,them);
	ast_safe_system(a);
	free(a);
	return;
}

static void doconpgm(struct rpt *myrpt,char *them)
{
char 	*a;
int	i;

	if (!myrpt->p.connpgm) return;
	i = strlen(them) + strlen(myrpt->p.connpgm) +  + 100;
	a = ast_malloc(i);
	if (!a) 
	{
		ast_log(LOG_NOTICE,"Unable to alloc");
		return;
	}
	memset(a,0,i);
	sprintf(a,"%s %s %s &",myrpt->p.connpgm,
		myrpt->name,them);
	ast_safe_system(a);
	ast_free(a);
	return;
}


static void statpost(struct rpt *myrpt,char *pairs)
{
	char *str;
	int	pid;
	time_t	now;
	unsigned int seq;
	CURL *curl;
	int *rescode;

	if (!myrpt->p.statpost_url) return;
	str = ast_malloc(strlen(pairs) + strlen(myrpt->p.statpost_url) + 200);
	ast_mutex_lock(&myrpt->statpost_lock);
	seq = ++myrpt->statpost_seqno;
	ast_mutex_unlock(&myrpt->statpost_lock);
	time(&now);
	sprintf(str,"%s?node=%s&time=%u&seqno=%u",myrpt->p.statpost_url,
		myrpt->name,(unsigned int) now,seq);
	if (pairs) sprintf(str + strlen(str),"&%s",pairs);
	if (!(pid = fork()))
	{
		curl = curl_easy_init();
		if(curl) {
			curl_easy_setopt(curl, CURLOPT_URL, str);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, ASTERISK_VERSION_HTTP);
			curl_easy_perform(curl);
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &rescode);
			curl_easy_cleanup(curl);
			curl_global_cleanup();
		}
		if(*rescode == 200) return;
		ast_log(LOG_ERROR, "statpost failed\n");
		perror("asterisk");
		exit(0);
	}
	ast_free(str);
	return;
}


/* 
 * Function stream data 
 */
 
static void startoutstream(struct rpt *myrpt)
{
char *str;
char *strs[100];
int	n;

	if (!myrpt->p.outstreamcmd) return;
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "app_rpt node %s starting output stream %s\n",
			myrpt->name,myrpt->p.outstreamcmd);
	str = ast_strdup(myrpt->p.outstreamcmd);
	if (!str)
	{
		ast_log(LOG_ERROR,"Malloc Failed!\n");
		return;
	}
	n = finddelim(str,strs,100);
	if (n < 1) return;
	if (pipe(myrpt->outstreampipe) == -1)
	{
		ast_log(LOG_ERROR,"pipe() failed!\n");
		ast_free(str);
		return;
	}
	if (fcntl(myrpt->outstreampipe[1],F_SETFL,O_NONBLOCK) == -1)
	{
		ast_log(LOG_ERROR,"Error: cannot set pipe to NONBLOCK");
		ast_free(str);
		return;
	}
	if (!(myrpt->outstreampid = fork()))
	{
		close(myrpt->outstreampipe[1]);
		if (dup2(myrpt->outstreampipe[0],fileno(stdin)) == -1)
		{
			ast_log(LOG_ERROR,"Error: cannot dup2() stdin");
			exit(0);
		}
		if (dup2(nullfd,fileno(stdout)) == -1)
		{
			ast_log(LOG_ERROR,"Error: cannot dup2() stdout");
			exit(0);
		}
		if (dup2(nullfd,fileno(stderr)) == -1)
		{
			ast_log(LOG_ERROR,"Error: cannot dup2() stderr");
			exit(0);
		}
		execv(strs[0],strs);
		ast_log(LOG_ERROR, "exec of %s failed.\n", strs[0]);
		perror("asterisk");
		exit(0);
	}
	ast_free(str);
	close(myrpt->outstreampipe[0]);
	if (myrpt->outstreampid == -1)
	{
		ast_log(LOG_ERROR,"fork() failed!!\n");
		close(myrpt->outstreampipe[1]);
		return;
	}
	return;
}

/* 
 * AllStar Network node lookup function.  This function will take the nodelist that has been read into memory
 * and try to match the node number that was passed to it.  If it is found, the function requested will succeed.
 * If not, it will fail.  Called when a connection to a remote node is requested.
 */

static int node_lookup(struct rpt *myrpt,char *digitbuf,char *str, int strmax, int wilds)
{

char *val;
int longestnode,i,j,found;
struct stat mystat;
struct ast_config *ourcfg;
struct ast_variable *vp;

	/* try to look it up locally first */
	val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.nodes, digitbuf);
	if (val)
	{
		if (str && strmax)
			snprintf(str,strmax,val,digitbuf);
		return(1);
	}
	if (wilds)
	{
		vp = ast_variable_browse(myrpt->cfg, myrpt->p.nodes);
		while(vp)
		{
			if (ast_extension_match(vp->name, digitbuf))
			{
				if (str && strmax)
					snprintf(str,strmax,vp->value,digitbuf);
				return(1);
			}
			vp = vp->next;
		}
	}
	ast_mutex_lock(&nodelookuplock);
	if (!myrpt->p.extnodefilesn) 
	{
		ast_mutex_unlock(&nodelookuplock);
		return(0);
	}
	/* determine longest node length again */		
	longestnode = 0;
	vp = ast_variable_browse(myrpt->cfg, myrpt->p.nodes);
	while(vp)
	{
		j = strlen(vp->name);
		if (*vp->name == '_') j--;
		if (j > longestnode)
			longestnode = j;
		vp = vp->next;
	}
	found = 0;
	for(i = 0; i < myrpt->p.extnodefilesn; i++)
	{
#ifdef	NEW_ASTERISK
		ourcfg = ast_config_load(myrpt->p.extnodefiles[i],config_flags);
#else
		ourcfg = ast_config_load(myrpt->p.extnodefiles[i]);
#endif
		/* if file does not exist */
		if (stat(myrpt->p.extnodefiles[i],&mystat) == -1) continue;
		/* if file not there, try next */
		if (!ourcfg) continue;
		vp = ast_variable_browse(ourcfg, myrpt->p.extnodes);
		while(vp)
		{
			j = strlen(vp->name);
			if (*vp->name == '_') j--;
			if (j > longestnode)
				longestnode = j;
			vp = vp->next;
		}
		if (!found)
		{
			val = (char *) ast_variable_retrieve(ourcfg, myrpt->p.extnodes, digitbuf);
			if (val)
			{
				found = 1;
				if (str && strmax)
					snprintf(str,strmax,val,digitbuf);
			}
		}
		ast_config_destroy(ourcfg);
	}
	myrpt->longestnode = longestnode;
	ast_mutex_unlock(&nodelookuplock);
	return(found);
}

static char *forward_node_lookup(struct rpt *myrpt,char *digitbuf, struct ast_config *cfg)
{

char *val,*efil,*enod,*strs[100];
int i,n;
struct stat mystat;
static struct ast_config *ourcfg;

	val = (char *) ast_variable_retrieve(cfg, "proxy", "extnodefile");
	if (!val) val = EXTNODEFILE;
	enod = (char *) ast_variable_retrieve(cfg, "proxy", "extnodes");
	if (!enod) enod = EXTNODES;
	ast_mutex_lock(&nodelookuplock);
	efil = ast_strdup(val);
	if (!efil) 
	{
		ast_config_destroy(ourcfg);
		if (ourcfg) ast_config_destroy(ourcfg);
		ourcfg = NULL;
		ast_mutex_unlock(&nodelookuplock);
		return NULL;
	}
	n = finddelim(efil,strs,100);
	if (n < 1)
	{
		ast_free(efil);
		ast_config_destroy(ourcfg);
		if (ourcfg) ast_config_destroy(ourcfg);
		ourcfg = NULL;
		ast_mutex_unlock(&nodelookuplock);
		return NULL;
	}
	if (ourcfg) ast_config_destroy(ourcfg);
	val = NULL;
	for(i = 0; i < n; i++)
	{
		/* if file does not exist */
		if (stat(strs[i],&mystat) == -1) continue;
#ifdef	NEW_ASTERISK
		ourcfg = ast_config_load(strs[i],config_flags);
#else
		ourcfg = ast_config_load(strs[i]);
#endif
		/* if file not there, try next */
		if (!ourcfg) continue;
		if (!val) val = (char *) ast_variable_retrieve(ourcfg, enod, digitbuf);
	}
        if (!val)
        {
                if (ourcfg) ast_config_destroy(ourcfg);
                ourcfg = NULL;
        }
	ast_mutex_unlock(&nodelookuplock);
	ast_free(efil);
	return(val);
}

/*
* Match a keyword in a list, and return index of string plus 1 if there was a match,* else return 0.
* If param is passed in non-null, then it will be set to the first character past the match
*/

static int matchkeyword(char *string, char **param, char *keywords[])
{
int	i,ls;
	for( i = 0 ; keywords[i] ; i++){
		ls = strlen(keywords[i]);
		if(!ls){
			if(param)
				*param = NULL;
			return 0;
		}
		if(!strncmp(string, keywords[i], ls)){
			if(param)
				*param = string + ls;
			return i + 1; 
		}
	}
	if(param)
		*param = NULL;
	return 0;
}

/*
* Skip characters in string which are in charlist, and return a pointer to the
* first non-matching character
*/

static char *skipchars(char *string, char *charlist)
{
int i;	
	while(*string){
		for(i = 0; charlist[i] ; i++){
			if(*string == charlist[i]){
				string++;
				break;
			}
		}
		if(!charlist[i])
			return string;
	}
	return string;
}	
					


static int myatoi(char *str)
{
int	ret;

	if (str == NULL) return -1;
	/* leave this %i alone, non-base-10 input is useful here */
	if (sscanf(str,"%i",&ret) != 1) return -1;
	return ret;
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

static int topcompar(const void *a, const void *b)
{
struct rpt_topkey *x = (struct rpt_topkey *) a;
struct rpt_topkey *y = (struct rpt_topkey *) b;

	return(x->timesince - y->timesince);
}

#ifdef	__RPT_NOTCH

/* rpt filter routine */
static void rpt_filter(struct rpt *myrpt, volatile short *buf, int len)
{
int	i,j;
struct	rptfilter *f;

	for(i = 0; i < len; i++)
	{
		for(j = 0; j < MAXFILTERS; j++)
		{
			f = &myrpt->filters[j];
			if (!*f->desc) continue;
			f->x0 = f->x1; f->x1 = f->x2;
		        f->x2 = ((float)buf[i]) / f->gain;
		        f->y0 = f->y1; f->y1 = f->y2;
		        f->y2 =   (f->x0 + f->x2) +   f->const0 * f->x1
		                     + (f->const1 * f->y0) + (f->const2 * f->y1);
			buf[i] = (short)f->y2;
		}
	}
}

#endif


/*
 Get the time for the machine's time zone
 Note: Asterisk requires a copy of localtime
 in the /etc directory for this to work properly.
 If /etc/localtime is not present, you will get
 GMT time! This is especially important on systems
 running embedded linux distributions as they don't usually
 have support for locales. 

 If OLD_ASTERISK is defined, then the older localtime_r
 function will be used. The /etc/localtime file is not
 required in this case. This provides backward compatibility
 with Asterisk 1.2 systems.

*/

#ifdef	NEW_ASTERISK
static void rpt_localtime( time_t *t, struct ast_tm *lt, char *tz)
{
struct timeval tv;

	tv.tv_sec = *t;
	tv.tv_usec = 0;
	ast_localtime(&tv, lt, tz);

}

static time_t rpt_mktime(struct ast_tm *tm,char *zone)
{
struct timeval now;

	now = ast_mktime(tm,zone);
	return now.tv_sec;
}


#else

static void rpt_localtime( time_t *t, struct tm *lt, char *tz)
{
#ifdef OLD_ASTERISK
	localtime_r(t, lt);
#else
	ast_localtime(t, lt, tz);
#endif
}

static time_t rpt_mktime(struct tm *tm,char *zone)
{
#ifdef OLD_ASTERISK
	return(mktime(tm));
#else
	return(ast_mktime(tm,zone));
#endif
}

#endif

/* Retrieve an int from a config file */
                                                                                
static int retrieve_astcfgint(struct rpt *myrpt,char *category, char *name, int min, int max, int defl)
{
        char *var;
        int ret;
	char include_zero = 0;

	if(min < 0){ /* If min is negative, this means include 0 as a valid entry */
		min = -min;
		include_zero = 1;
	}           
                                                                     
        var = (char *) ast_variable_retrieve(myrpt->cfg, category, name);
        if(var){
                ret = myatoi(var);
		if(include_zero && !ret)
			return 0;
                if(ret < min)
                        ret = min;
                if(ret > max)
                        ret = max;
        }
        else
                ret = defl;
        return ret;
}

/* 
 * This is the initialization function.  This routine takes the data in rpt.conf and setup up the variables needed for each of
 * the repeaters that it finds.  There is some minor sanity checking done on the data passed, but not much.
 * 
 * Note that this is kind of a mess to read.  It uses the asterisk native function to read config files and pass back values assigned to
 * keywords.
 */

static void load_rpt_vars(int n,int init)
{
char *this,*val;
int	i,j,longestnode;
struct ast_variable *vp;
struct ast_config *cfg;
char *strs[100];
char s1[256];
static char *cs_keywords[] = {"rptena","rptdis","apena","apdis","lnkena","lnkdis","totena","totdis","skena","skdis",
				"ufena","ufdis","atena","atdis","noice","noicd","slpen","slpds",NULL};

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "%s config for repeater %s\n",
			(init) ? "Loading initial" : "Re-Loading",rpt_vars[n].name);
	ast_mutex_lock(&rpt_vars[n].lock);
	if (rpt_vars[n].cfg) ast_config_destroy(rpt_vars[n].cfg);
#ifdef	NEW_ASTERISK
	cfg = ast_config_load("rpt.conf",config_flags);
#else
	cfg = ast_config_load("rpt.conf");
#endif
	if (!cfg) {
		ast_mutex_unlock(&rpt_vars[n].lock);
 		ast_log(LOG_NOTICE, "Unable to open radio repeater configuration rpt.conf.  Radio Repeater disabled.\n");
		pthread_exit(NULL);
	}
	rpt_vars[n].cfg = cfg; 
	this = rpt_vars[n].name;
 	memset(&rpt_vars[n].p,0,sizeof(rpt_vars[n].p));
	if (init)
	{
		char *cp;
		int savearea = (char *)&rpt_vars[n].p - (char *)&rpt_vars[n];

		cp = (char *) &rpt_vars[n].p;
		memset(cp + sizeof(rpt_vars[n].p),0,
			sizeof(rpt_vars[n]) - (sizeof(rpt_vars[n].p) + savearea));
		rpt_vars[n].tele.next = &rpt_vars[n].tele;
		rpt_vars[n].tele.prev = &rpt_vars[n].tele;
		rpt_vars[n].rpt_thread = AST_PTHREADT_NULL;
		rpt_vars[n].tailmessagen = 0;
	}
#ifdef	__RPT_NOTCH
	/* zot out filters stuff */
	memset(&rpt_vars[n].filters,0,sizeof(rpt_vars[n].filters));
#endif
	val = (char *) ast_variable_retrieve(cfg,this,"context");
	if (val) rpt_vars[n].p.ourcontext = val;
	else rpt_vars[n].p.ourcontext = this;
	val = (char *) ast_variable_retrieve(cfg,this,"callerid");
	if (val) rpt_vars[n].p.ourcallerid = val;
	val = (char *) ast_variable_retrieve(cfg,this,"accountcode");
	if (val) rpt_vars[n].p.acctcode = val;
	val = (char *) ast_variable_retrieve(cfg,this,"idrecording");
	if (val) rpt_vars[n].p.ident = val;
	val = (char *) ast_variable_retrieve(cfg,this,"hangtime");
	if (val) rpt_vars[n].p.hangtime = atoi(val);
		else rpt_vars[n].p.hangtime = (ISRANGER(rpt_vars[n].name) ? 1 : HANGTIME);
	if (rpt_vars[n].p.hangtime < 1) rpt_vars[n].p.hangtime = 1;
	val = (char *) ast_variable_retrieve(cfg,this,"althangtime");
	if (val) rpt_vars[n].p.althangtime = atoi(val);
		else rpt_vars[n].p.althangtime = (ISRANGER(rpt_vars[n].name) ? 1 : HANGTIME);
	if (rpt_vars[n].p.althangtime < 1) rpt_vars[n].p.althangtime = 1;
	val = (char *) ast_variable_retrieve(cfg,this,"totime");
	if (val) rpt_vars[n].p.totime = atoi(val);
		else rpt_vars[n].p.totime = (ISRANGER(rpt_vars[n].name) ? 9999999 : TOTIME);
	val = (char *) ast_variable_retrieve(cfg,this,"voxtimeout");
	if (val) rpt_vars[n].p.voxtimeout_ms = atoi(val);
		else rpt_vars[n].p.voxtimeout_ms = VOX_TIMEOUT_MS;
	val = (char *) ast_variable_retrieve(cfg,this,"voxrecover");
	if (val) rpt_vars[n].p.voxrecover_ms = atoi(val);
		else rpt_vars[n].p.voxrecover_ms = VOX_RECOVER_MS;
	val = (char *) ast_variable_retrieve(cfg,this,"simplexpatchdelay");
	if (val) rpt_vars[n].p.simplexpatchdelay = atoi(val);
		else rpt_vars[n].p.simplexpatchdelay = SIMPLEX_PATCH_DELAY;
	val = (char *) ast_variable_retrieve(cfg,this,"simplexphonedelay");
	if (val) rpt_vars[n].p.simplexphonedelay = atoi(val);
		else rpt_vars[n].p.simplexphonedelay = SIMPLEX_PHONE_DELAY;
	val = (char *) ast_variable_retrieve(cfg,this,"statpost_program");
	if (val) rpt_vars[n].p.statpost_program = val;
		else rpt_vars[n].p.statpost_program = STATPOST_PROGRAM;
	rpt_vars[n].p.statpost_url = 
		(char *) ast_variable_retrieve(cfg,this,"statpost_url");
	rpt_vars[n].p.tailmessagetime = retrieve_astcfgint(&rpt_vars[n],this, "tailmessagetime", 0, 200000000, 0);		
	rpt_vars[n].p.tailsquashedtime = retrieve_astcfgint(&rpt_vars[n],this, "tailsquashedtime", 0, 200000000, 0);		
	rpt_vars[n].p.duplex = retrieve_astcfgint(&rpt_vars[n],this,"duplex",0,4,(ISRANGER(rpt_vars[n].name) ? 0 : 2));
	rpt_vars[n].p.idtime = retrieve_astcfgint(&rpt_vars[n],this, "idtime", -60000, 2400000, IDTIME);	/* Enforce a min max including zero */
	rpt_vars[n].p.politeid = retrieve_astcfgint(&rpt_vars[n],this, "politeid", 30000, 300000, POLITEID); /* Enforce a min max */
	j  = retrieve_astcfgint(&rpt_vars[n],this, "elke", 0, 40000000, 0);
	rpt_vars[n].p.elke  = j * 1210;
	val = (char *) ast_variable_retrieve(cfg,this,"tonezone");
	if (val) rpt_vars[n].p.tonezone = val;
	rpt_vars[n].p.tailmessages[0] = 0;
	rpt_vars[n].p.tailmessagemax = 0;
	val = (char *) ast_variable_retrieve(cfg,this,"tailmessagelist");
	if (val) rpt_vars[n].p.tailmessagemax = finddelim(val, rpt_vars[n].p.tailmessages, 500);
	rpt_vars[n].p.aprstt = (char *) ast_variable_retrieve(cfg,this,"aprstt");
	val = (char *) ast_variable_retrieve(cfg,this,"memory");
	if (!val) val = MEMORY;
	rpt_vars[n].p.memory = val;
	val = (char *) ast_variable_retrieve(cfg,this,"morse");
	if (!val) val = MORSE;
	rpt_vars[n].p.morse = val;
	val = (char *) ast_variable_retrieve(cfg,this,"telemetry");
	if (!val) val = TELEMETRY;
	rpt_vars[n].p.telemetry = val;
	val = (char *) ast_variable_retrieve(cfg,this,"macro");
	if (!val) val = MACRO;
	rpt_vars[n].p.macro = val;
	val = (char *) ast_variable_retrieve(cfg,this,"tonemacro");
	if (!val) val = TONEMACRO;
	rpt_vars[n].p.tonemacro = val;
	val = (char *) ast_variable_retrieve(cfg,this,"mdcmacro");
	if (!val) val = MDCMACRO;
	rpt_vars[n].p.mdcmacro = val;
	val = (char *) ast_variable_retrieve(cfg,this,"startup_macro");
	if (val) rpt_vars[n].p.startupmacro = val;
	val = (char *) ast_variable_retrieve(cfg,this,"iobase");
	/* do not use atoi() here, we need to be able to have
		the input specified in hex or decimal so we use
		sscanf with a %i */
	if ((!val) || (sscanf(val,"%i",&rpt_vars[n].p.iobase) != 1))
		rpt_vars[n].p.iobase = DEFAULT_IOBASE;
	val = (char *) ast_variable_retrieve(cfg,this,"ioport");
	rpt_vars[n].p.ioport = val;
	val = (char *) ast_variable_retrieve(cfg,this,"functions");
	if (!val)
		{
			val = FUNCTIONS;
			rpt_vars[n].p.simple = 1;
		} 
	rpt_vars[n].p.functions = val;
	val =  (char *) ast_variable_retrieve(cfg,this,"link_functions");
	if (val) rpt_vars[n].p.link_functions = val;
	else 
		rpt_vars[n].p.link_functions = rpt_vars[n].p.functions;
	val = (char *) ast_variable_retrieve(cfg,this,"phone_functions");
	if (val) rpt_vars[n].p.phone_functions = val;
	else if (ISRANGER(rpt_vars[n].name)) rpt_vars[n].p.phone_functions = rpt_vars[n].p.functions;
	val = (char *) ast_variable_retrieve(cfg,this,"dphone_functions");
	if (val) rpt_vars[n].p.dphone_functions = val;
	else if (ISRANGER(rpt_vars[n].name)) rpt_vars[n].p.dphone_functions = rpt_vars[n].p.functions;
	val = (char *) ast_variable_retrieve(cfg,this,"alt_functions");
	if (val) rpt_vars[n].p.alt_functions = val;
	val = (char *) ast_variable_retrieve(cfg,this,"funcchar");
	if (!val) rpt_vars[n].p.funcchar = FUNCCHAR; else 
		rpt_vars[n].p.funcchar = *val;		
	val = (char *) ast_variable_retrieve(cfg,this,"endchar");
	if (!val) rpt_vars[n].p.endchar = ENDCHAR; else 
		rpt_vars[n].p.endchar = *val;		
	val = (char *) ast_variable_retrieve(cfg,this,"nobusyout");
	if (val) rpt_vars[n].p.nobusyout = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg,this,"notelemtx");
	if (val) rpt_vars[n].p.notelemtx = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg,this,"propagate_dtmf");
	if (val) rpt_vars[n].p.propagate_dtmf = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg,this,"propagate_phonedtmf");
	if (val) rpt_vars[n].p.propagate_phonedtmf = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg,this,"linktolink");
	if (val) rpt_vars[n].p.linktolink = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg,this,"nodes");
	if (!val) val = NODES;
	rpt_vars[n].p.nodes = val;
	val = (char *) ast_variable_retrieve(cfg,this,"extnodes");
	if (!val) val = EXTNODES;
	rpt_vars[n].p.extnodes = val;
	val = (char *) ast_variable_retrieve(cfg,this,"extnodefile");
	if (!val) val = EXTNODEFILE;
	rpt_vars[n].p.extnodefilesn = 
	    explode_string(val,rpt_vars[n].p.extnodefiles,MAX_EXTNODEFILES,',',0);
	val = (char *) ast_variable_retrieve(cfg,this,"locallinknodes");
	if (val) rpt_vars[n].p.locallinknodesn = explode_string(ast_strdup(val),rpt_vars[n].p.locallinknodes,MAX_LOCALLINKNODES,',',0);
	val = (char *) ast_variable_retrieve(cfg,this,"lconn");
	if (val) rpt_vars[n].p.nlconn = explode_string(strupr(ast_strdup(val)),rpt_vars[n].p.lconn,MAX_LSTUFF,',',0);
	val = (char *) ast_variable_retrieve(cfg,this,"ldisc");
	if (val) rpt_vars[n].p.nldisc = explode_string(strupr(ast_strdup(val)),rpt_vars[n].p.ldisc,MAX_LSTUFF,',',0);
	val = (char *) ast_variable_retrieve(cfg,this,"patchconnect");
	rpt_vars[n].p.patchconnect = val;
	val = (char *) ast_variable_retrieve(cfg,this,"archivedir");
	if (val) rpt_vars[n].p.archivedir = val;
	val = (char *) ast_variable_retrieve(cfg,this,"authlevel");
	if (val) rpt_vars[n].p.authlevel = atoi(val); 
	else rpt_vars[n].p.authlevel = 0;
	val = (char *) ast_variable_retrieve(cfg,this,"parrot");
	if (val) rpt_vars[n].p.parrotmode = (ast_true(val)) ? 2 : 0;
	else rpt_vars[n].p.parrotmode = 0;
	val = (char *) ast_variable_retrieve(cfg,this,"parrottime");
	if (val) rpt_vars[n].p.parrottime = atoi(val); 
	else rpt_vars[n].p.parrottime = PARROTTIME;
	val = (char *) ast_variable_retrieve(cfg,this,"rptnode");
	rpt_vars[n].p.rptnode = val;
	val = (char *) ast_variable_retrieve(cfg,this,"mars");
	if (val) rpt_vars[n].p.remote_mars = atoi(val); 
	else rpt_vars[n].p.remote_mars = 0;
	val = (char *) ast_variable_retrieve(cfg,this,"monminblocks");
	if (val) rpt_vars[n].p.monminblocks = atol(val); 
	else rpt_vars[n].p.monminblocks = DEFAULT_MONITOR_MIN_DISK_BLOCKS;
	val = (char *) ast_variable_retrieve(cfg,this,"remote_inact_timeout");
	if (val) rpt_vars[n].p.remoteinacttimeout = atoi(val); 
	else rpt_vars[n].p.remoteinacttimeout = DEFAULT_REMOTE_INACT_TIMEOUT;
	val = (char *) ast_variable_retrieve(cfg,this,"civaddr");
	if (val) rpt_vars[n].p.civaddr = atoi(val); 
	else rpt_vars[n].p.civaddr = DEFAULT_CIV_ADDR;
	val = (char *) ast_variable_retrieve(cfg,this,"remote_timeout");
	if (val) rpt_vars[n].p.remotetimeout = atoi(val); 
	else rpt_vars[n].p.remotetimeout = DEFAULT_REMOTE_TIMEOUT;
	val = (char *) ast_variable_retrieve(cfg,this,"remote_timeout_warning");
	if (val) rpt_vars[n].p.remotetimeoutwarning = atoi(val); 
	else rpt_vars[n].p.remotetimeoutwarning = DEFAULT_REMOTE_TIMEOUT_WARNING;
	val = (char *) ast_variable_retrieve(cfg,this,"remote_timeout_warning_freq");
	if (val) rpt_vars[n].p.remotetimeoutwarningfreq = atoi(val); 
	else rpt_vars[n].p.remotetimeoutwarningfreq = DEFAULT_REMOTE_TIMEOUT_WARNING_FREQ;
	val = (char *) ast_variable_retrieve(cfg,this,"erxgain");
	if (!val) val = DEFAULT_ERXGAIN;
	rpt_vars[n].p.erxgain = pow(10.0,atof(val) / 20.0); 
	val = (char *) ast_variable_retrieve(cfg,this,"etxgain");
	if (!val) val = DEFAULT_ETXGAIN;
	rpt_vars[n].p.etxgain = pow(10.0,atof(val) / 20.0);
	val = (char *) ast_variable_retrieve(cfg,this,"eannmode");
	if (val) rpt_vars[n].p.eannmode = atoi(val);
	else rpt_vars[n].p.eannmode = DEFAULT_EANNMODE;
	if (rpt_vars[n].p.eannmode < 0) rpt_vars[n].p.eannmode = 0;
	if (rpt_vars[n].p.eannmode > 3) rpt_vars[n].p.eannmode = 3;
	val = (char *) ast_variable_retrieve(cfg,this,"trxgain");
	if (!val) val = DEFAULT_TRXGAIN;
	rpt_vars[n].p.trxgain = pow(10.0,atof(val) / 20.0); 
	val = (char *) ast_variable_retrieve(cfg,this,"ttxgain");
	if (!val) val = DEFAULT_TTXGAIN;
	rpt_vars[n].p.ttxgain = pow(10.0,atof(val) / 20.0);
	val = (char *) ast_variable_retrieve(cfg,this,"tannmode");
	if (val) rpt_vars[n].p.tannmode = atoi(val);
	else rpt_vars[n].p.tannmode = DEFAULT_TANNMODE;
	if (rpt_vars[n].p.tannmode < 1) rpt_vars[n].p.tannmode = 1;
	if (rpt_vars[n].p.tannmode > 3) rpt_vars[n].p.tannmode = 3;
	val = (char *) ast_variable_retrieve(cfg,this,"linkmongain");
	if (!val) val = DEFAULT_LINKMONGAIN;
	rpt_vars[n].p.linkmongain = pow(10.0,atof(val) / 20.0);
	val = (char *) ast_variable_retrieve(cfg,this,"discpgm");
	rpt_vars[n].p.discpgm = val; 
	val = (char *) ast_variable_retrieve(cfg,this,"connpgm");
	rpt_vars[n].p.connpgm = val;
	val = (char *) ast_variable_retrieve(cfg,this,"mdclog");
	rpt_vars[n].p.mdclog = val;
	val = (char *) ast_variable_retrieve(cfg,this,"lnkactenable");
	if (val) rpt_vars[n].p.lnkactenable = ast_true(val);
	else rpt_vars[n].p.lnkactenable = 0;
	rpt_vars[n].p.lnkacttime = retrieve_astcfgint(&rpt_vars[n],this, "lnkacttime", -120, 90000, 0);	/* Enforce a min max including zero */
	val = (char *) ast_variable_retrieve(cfg, this, "lnkactmacro");
	rpt_vars[n].p.lnkactmacro = val;
	val = (char *) ast_variable_retrieve(cfg, this, "lnkacttimerwarn");
	rpt_vars[n].p.lnkacttimerwarn = val;
	val = (char *) ast_variable_retrieve(cfg, this, "nolocallinkct");
	rpt_vars[n].p.nolocallinkct = ast_true(val);
	rpt_vars[n].p.rptinacttime = retrieve_astcfgint(&rpt_vars[n],this, "rptinacttime", -120, 90000, 0);	/* Enforce a min max including zero */
	val = (char *) ast_variable_retrieve(cfg, this, "rptinactmacro");
	rpt_vars[n].p.rptinactmacro = val;
	val = (char *) ast_variable_retrieve(cfg, this, "nounkeyct");
	rpt_vars[n].p.nounkeyct = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg, this, "holdofftelem");
	rpt_vars[n].p.holdofftelem = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg, this, "beaconing");
	rpt_vars[n].p.beaconing = ast_true(val); 
	val = (char *) ast_variable_retrieve(cfg,this,"rxburstfreq");
	if (val) rpt_vars[n].p.rxburstfreq = atoi(val);
	else rpt_vars[n].p.rxburstfreq = 0;
	val = (char *) ast_variable_retrieve(cfg,this,"rxbursttime");
	if (val) rpt_vars[n].p.rxbursttime = atoi(val);
	else rpt_vars[n].p.rxbursttime = DEFAULT_RXBURST_TIME;
	val = (char *) ast_variable_retrieve(cfg,this,"rxburstthreshold");
	if (val) rpt_vars[n].p.rxburstthreshold = atoi(val);
	else rpt_vars[n].p.rxburstthreshold = DEFAULT_RXBURST_THRESHOLD;
	val = (char *) ast_variable_retrieve(cfg,this,"litztime");
	if (val) rpt_vars[n].p.litztime = atoi(val);
	else rpt_vars[n].p.litztime = DEFAULT_LITZ_TIME;
	val = (char *) ast_variable_retrieve(cfg,this,"litzchar");
	if (!val) val = DEFAULT_LITZ_CHAR;
	rpt_vars[n].p.litzchar = val;
	val = (char *) ast_variable_retrieve(cfg,this,"litzcmd");
	rpt_vars[n].p.litzcmd = val;
	val = (char *) ast_variable_retrieve(cfg,this,"itxctcss");
	if (val) rpt_vars[n].p.itxctcss = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg,this,"gpsfeet");
	if (val) rpt_vars[n].p.gpsfeet = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg,this,"split2m");
	if (val) rpt_vars[n].p.default_split_2m = atoi(val);
	else rpt_vars[n].p.default_split_2m = DEFAULT_SPLIT_2M;
	val = (char *) ast_variable_retrieve(cfg,this,"split70cm");
	if (val) rpt_vars[n].p.default_split_70cm = atoi(val);
	else rpt_vars[n].p.default_split_70cm = DEFAULT_SPLIT_70CM;
	val = (char *) ast_variable_retrieve(cfg,this,"dtmfkey");
	if (val) rpt_vars[n].p.dtmfkey = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg,this,"dtmfkeys");
	if (!val) val = DTMFKEYS;
	rpt_vars[n].p.dtmfkeys = val;
	val = (char *) ast_variable_retrieve(cfg,this,"outstreamcmd");
	rpt_vars[n].p.outstreamcmd = val;
	val = (char *) ast_variable_retrieve(cfg,this,"eloutbound");
	rpt_vars[n].p.eloutbound = val;
	val = (char *) ast_variable_retrieve(cfg,this,"events");
	if (!val) val = "events";
	rpt_vars[n].p.events = val;
	val = (char *) ast_variable_retrieve(cfg,this,"timezone");
	rpt_vars[n].p.timezone = val;

#ifdef	__RPT_NOTCH
	val = (char *) ast_variable_retrieve(cfg,this,"rxnotch");
	if (val) {
		i = finddelim(val,strs,MAXFILTERS * 2);
		i &= ~1; /* force an even number, rounded down */
		if (i >= 2) for(j = 0; j < i; j += 2)
		{
			rpt_mknotch(atof(strs[j]),atof(strs[j + 1]),
			  &rpt_vars[n].filters[j >> 1].gain,
			    &rpt_vars[n].filters[j >> 1].const0,
				&rpt_vars[n].filters[j >> 1].const1,
				    &rpt_vars[n].filters[j >> 1].const2);
			sprintf(rpt_vars[n].filters[j >> 1].desc,"%s Hz, BW = %s",
				strs[j],strs[j + 1]);
		}

	}
#endif
	val = (char *) ast_variable_retrieve(cfg,this,"votertype");
	if (!val) val = "0";
	rpt_vars[n].p.votertype=atoi(val);

	val = (char *) ast_variable_retrieve(cfg,this,"votermode");
	if (!val) val = "0";
	rpt_vars[n].p.votermode=atoi(val);

	val = (char *) ast_variable_retrieve(cfg,this,"votermargin");
	if (!val) val = "10";
	rpt_vars[n].p.votermargin=atoi(val);

	val = (char *) ast_variable_retrieve(cfg,this,"telemnomdb");
	if (!val) val = "0";
	rpt_vars[n].p.telemnomgain = pow(10.0,atof(val) / 20.0);
	val = (char *) ast_variable_retrieve(cfg,this,"telemduckdb");
	if (!val) val = DEFAULT_TELEMDUCKDB;
	rpt_vars[n].p.telemduckgain = pow(10.0,atof(val) / 20.0);
	val = (char *) ast_variable_retrieve(cfg,this,"telemdefault");
	if (val) rpt_vars[n].p.telemdefault = atoi(val);
	else rpt_vars[n].p.telemdefault = DEFAULT_RPT_TELEMDEFAULT;
	val = (char *) ast_variable_retrieve(cfg,this,"telemdynamic");
	if (val) rpt_vars[n].p.telemdynamic = ast_true(val);
	else rpt_vars[n].p.telemdynamic = DEFAULT_RPT_TELEMDYNAMIC;
	if (!rpt_vars[n].p.telemdefault) 
		rpt_vars[n].telemmode = 0;
	else if (rpt_vars[n].p.telemdefault == 2)
		rpt_vars[n].telemmode = 1;
	else rpt_vars[n].telemmode = 0x7fffffff;

	val = (char *) ast_variable_retrieve(cfg,this,"guilinkdefault");
	if (val) rpt_vars[n].p.linkmode[LINKMODE_GUI] = atoi(val);
	else rpt_vars[n].p.linkmode[LINKMODE_GUI] = DEFAULT_GUI_LINK_MODE;
	val = (char *) ast_variable_retrieve(cfg,this,"guilinkdynamic");
	if (val) rpt_vars[n].p.linkmodedynamic[LINKMODE_GUI] = ast_true(val);
	else rpt_vars[n].p.linkmodedynamic[LINKMODE_GUI] = DEFAULT_GUI_LINK_MODE_DYNAMIC;

	val = (char *) ast_variable_retrieve(cfg,this,"phonelinkdefault");
	if (val) rpt_vars[n].p.linkmode[LINKMODE_PHONE] = atoi(val);
	else rpt_vars[n].p.linkmode[LINKMODE_PHONE] = DEFAULT_PHONE_LINK_MODE;
	val = (char *) ast_variable_retrieve(cfg,this,"phonelinkdynamic");
	if (val) rpt_vars[n].p.linkmodedynamic[LINKMODE_PHONE] = ast_true(val);
	else rpt_vars[n].p.linkmodedynamic[LINKMODE_PHONE] = DEFAULT_PHONE_LINK_MODE_DYNAMIC;

	val = (char *) ast_variable_retrieve(cfg,this,"echolinkdefault");
	if (val) rpt_vars[n].p.linkmode[LINKMODE_ECHOLINK] = atoi(val);
	else rpt_vars[n].p.linkmode[LINKMODE_ECHOLINK] = DEFAULT_ECHOLINK_LINK_MODE;
	val = (char *) ast_variable_retrieve(cfg,this,"echolinkdynamic");
	if (val) rpt_vars[n].p.linkmodedynamic[LINKMODE_ECHOLINK] = ast_true(val);
	else rpt_vars[n].p.linkmodedynamic[LINKMODE_ECHOLINK] = DEFAULT_ECHOLINK_LINK_MODE_DYNAMIC;

	val = (char *) ast_variable_retrieve(cfg,this,"tlbdefault");
	if (val) rpt_vars[n].p.linkmode[LINKMODE_TLB] = atoi(val);
	else rpt_vars[n].p.linkmode[LINKMODE_TLB] = DEFAULT_TLB_LINK_MODE;
	val = (char *) ast_variable_retrieve(cfg,this,"tlbdynamic");
	if (val) rpt_vars[n].p.linkmodedynamic[LINKMODE_TLB] = ast_true(val);
	else rpt_vars[n].p.linkmodedynamic[LINKMODE_TLB] = DEFAULT_TLB_LINK_MODE_DYNAMIC;

	val = (char *) ast_variable_retrieve(cfg,this,"locallist");
	if (val) {
		memset(rpt_vars[n].p.locallist,0,sizeof(rpt_vars[n].p.locallist));
		rpt_vars[n].p.nlocallist = finddelim(val,rpt_vars[n].p.locallist,16);
	}

	val = (char *) ast_variable_retrieve(cfg,this,"ctgroup");
	if (val) {
		strncpy(rpt_vars[n].p.ctgroup,val,sizeof(rpt_vars[n].p.ctgroup) - 1);
	} else strcpy(rpt_vars[n].p.ctgroup,"0");

	val = (char *) ast_variable_retrieve(cfg,this,"inxlat");
	if (val) {
		memset(&rpt_vars[n].p.inxlat,0,sizeof(struct rpt_xlat));
		i = finddelim(val,strs,3);
		if (i) strncpy(rpt_vars[n].p.inxlat.funccharseq,strs[0],MAXXLAT - 1);
		if (i > 1) strncpy(rpt_vars[n].p.inxlat.endcharseq,strs[1],MAXXLAT - 1);
		if (i > 2) strncpy(rpt_vars[n].p.inxlat.passchars,strs[2],MAXXLAT - 1);
		if (i > 3) rpt_vars[n].p.dopfxtone = ast_true(strs[3]);
	}
	val = (char *) ast_variable_retrieve(cfg,this,"outxlat");
	if (val) {
		memset(&rpt_vars[n].p.outxlat,0,sizeof(struct rpt_xlat));
		i = finddelim(val,strs,3);
		if (i) strncpy(rpt_vars[n].p.outxlat.funccharseq,strs[0],MAXXLAT - 1);
		if (i > 1) strncpy(rpt_vars[n].p.outxlat.endcharseq,strs[1],MAXXLAT - 1);
		if (i > 2) strncpy(rpt_vars[n].p.outxlat.passchars,strs[2],MAXXLAT - 1);
	}
	val = (char *) ast_variable_retrieve(cfg,this,"sleeptime");
	if (val) rpt_vars[n].p.sleeptime = atoi(val);
	else rpt_vars[n].p.sleeptime = SLEEPTIME;
	/* retrieve the stanza name for the control states if there is one */
	val = (char *) ast_variable_retrieve(cfg,this,"controlstates");
	rpt_vars[n].p.csstanzaname = val;
		
	/* retrieve the stanza name for the scheduler if there is one */
	val = (char *) ast_variable_retrieve(cfg,this,"scheduler");
	rpt_vars[n].p.skedstanzaname = val;

	/* retrieve the stanza name for the txlimits */
	val = (char *) ast_variable_retrieve(cfg,this,"txlimits");
	rpt_vars[n].p.txlimitsstanzaname = val;

	rpt_vars[n].p.iospeed = B9600;
	if (!strcasecmp(rpt_vars[n].remoterig,remote_rig_ft950))
		rpt_vars[n].p.iospeed = B38400;
	if (!strcasecmp(rpt_vars[n].remoterig,remote_rig_ft100))
		rpt_vars[n].p.iospeed = B4800;
	if (!strcasecmp(rpt_vars[n].remoterig,remote_rig_ft897))
		rpt_vars[n].p.iospeed = B4800;
	val = (char *) ast_variable_retrieve(cfg,this,"dias");
	if (val) rpt_vars[n].p.dias = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg,this,"dusbabek");
	if (val) rpt_vars[n].p.dusbabek = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg,this,"iospeed");
	if (val)
	{
	    switch(atoi(val))
	    {
		case 2400:
			rpt_vars[n].p.iospeed = B2400;
			break;
		case 4800:
			rpt_vars[n].p.iospeed = B4800;
			break;
		case 19200:
			rpt_vars[n].p.iospeed = B19200;
			break;
		case 38400:
			rpt_vars[n].p.iospeed = B38400;
			break;
		case 57600:
			rpt_vars[n].p.iospeed = B57600;
			break;
		default:
			ast_log(LOG_ERROR,"%s is not valid baud rate for iospeed\n",val);
			break;
	    }
	}

	longestnode = 0;

	vp = ast_variable_browse(cfg, rpt_vars[n].p.nodes);
		
	while(vp){
		j = strlen(vp->name);
		if (j > longestnode)
			longestnode = j;
		vp = vp->next;
	}

	rpt_vars[n].longestnode = longestnode;
		
	/*
	* For this repeater, Determine the length of the longest function 
	*/
	rpt_vars[n].longestfunc = 0;
	vp = ast_variable_browse(cfg, rpt_vars[n].p.functions);
	while(vp){
		j = strlen(vp->name);
		if (j > rpt_vars[n].longestfunc)
			rpt_vars[n].longestfunc = j;
		vp = vp->next;
	}
	/*
	* For this repeater, Determine the length of the longest function 
	*/
	rpt_vars[n].link_longestfunc = 0;
	vp = ast_variable_browse(cfg, rpt_vars[n].p.link_functions);
	while(vp){
		j = strlen(vp->name);
		if (j > rpt_vars[n].link_longestfunc)
			rpt_vars[n].link_longestfunc = j;
		vp = vp->next;
	}
	rpt_vars[n].phone_longestfunc = 0;
	if (rpt_vars[n].p.phone_functions)
	{
		vp = ast_variable_browse(cfg, rpt_vars[n].p.phone_functions);
		while(vp){
			j = strlen(vp->name);
			if (j > rpt_vars[n].phone_longestfunc)
				rpt_vars[n].phone_longestfunc = j;
			vp = vp->next;
		}
	}
	rpt_vars[n].dphone_longestfunc = 0;
	if (rpt_vars[n].p.dphone_functions)
	{
		vp = ast_variable_browse(cfg, rpt_vars[n].p.dphone_functions);
		while(vp){
			j = strlen(vp->name);
			if (j > rpt_vars[n].dphone_longestfunc)
				rpt_vars[n].dphone_longestfunc = j;
			vp = vp->next;
		}
	}
	rpt_vars[n].alt_longestfunc = 0;
	if (rpt_vars[n].p.alt_functions)
	{
		vp = ast_variable_browse(cfg, rpt_vars[n].p.alt_functions);
		while(vp){
			j = strlen(vp->name);
			if (j > rpt_vars[n].alt_longestfunc)
				rpt_vars[n].alt_longestfunc = j;
			vp = vp->next;
		}
	}
	rpt_vars[n].macro_longest = 1;
	vp = ast_variable_browse(cfg, rpt_vars[n].p.macro);
	while(vp){
		j = strlen(vp->name);
		if (j > rpt_vars[n].macro_longest)
			rpt_vars[n].macro_longest = j;
		vp = vp->next;
	}
	
	/* Browse for control states */
	if(rpt_vars[n].p.csstanzaname)
		vp = ast_variable_browse(cfg, rpt_vars[n].p.csstanzaname);
	else
		vp = NULL;
	for( i = 0 ; vp && (i < MAX_SYSSTATES) ; i++){ /* Iterate over the number of control state lines in the stanza */
		int k,nukw,statenum;
		statenum=atoi(vp->name);
		strncpy(s1, vp->value, 255);
		s1[255] = 0;
		nukw  = finddelim(s1,strs,32);
		
		for (k = 0 ; k < nukw ; k++){ /* for each user specified keyword */	
			for(j = 0 ; cs_keywords[j] != NULL ; j++){ /* try to match to one in our internal table */
				if(!strcmp(strs[k],cs_keywords[j])){
					switch(j){
						case 0: /* rptena */
							rpt_vars[n].p.s[statenum].txdisable = 0;
							break;
						case 1: /* rptdis */
							rpt_vars[n].p.s[statenum].txdisable = 1;
							break;
			
						case 2: /* apena */
							rpt_vars[n].p.s[statenum].autopatchdisable = 0;
							break;

						case 3: /* apdis */
							rpt_vars[n].p.s[statenum].autopatchdisable = 1;
							break;

						case 4: /* lnkena */
							rpt_vars[n].p.s[statenum].linkfundisable = 0;
							break;
	
						case 5: /* lnkdis */
							rpt_vars[n].p.s[statenum].linkfundisable = 1;
							break;

						case 6: /* totena */
							rpt_vars[n].p.s[statenum].totdisable = 0;
							break;
					
						case 7: /* totdis */
							rpt_vars[n].p.s[statenum].totdisable = 1;
							break;

						case 8: /* skena */
							rpt_vars[n].p.s[statenum].schedulerdisable = 0;
							break;

						case 9: /* skdis */
							rpt_vars[n].p.s[statenum].schedulerdisable = 1;
							break;

						case 10: /* ufena */
							rpt_vars[n].p.s[statenum].userfundisable = 0;
							break;

						case 11: /* ufdis */
							rpt_vars[n].p.s[statenum].userfundisable = 1;
							break;

						case 12: /* atena */
							rpt_vars[n].p.s[statenum].alternatetail = 1;
							break;

						case 13: /* atdis */
							rpt_vars[n].p.s[statenum].alternatetail = 0;
							break;

						case 14: /* noice */
							rpt_vars[n].p.s[statenum].noincomingconns = 1;
							break;

						case 15: /* noicd */
							rpt_vars[n].p.s[statenum].noincomingconns = 0;
							break;

						case 16: /* slpen */
							rpt_vars[n].p.s[statenum].sleepena = 1;
							break;

						case 17: /* slpds */
							rpt_vars[n].p.s[statenum].sleepena = 0;
							break;

						default:
							ast_log(LOG_WARNING,
								"Unhandled control state keyword %s", cs_keywords[i]);
							break;
					}
				}
			}
		}
		vp = vp->next;
	}
	ast_mutex_unlock(&rpt_vars[n].lock);
}

/*
* Enable or disable debug output at a given level at the console
*/
                                                                                                                                 
static int rpt_do_debug(int fd, int argc, char *argv[])
{
	int newlevel;

        if (argc != 4)
                return RESULT_SHOWUSAGE;
        newlevel = myatoi(argv[3]);
        if((newlevel < 0) || (newlevel > 7))
                return RESULT_SHOWUSAGE;
        if(newlevel)
                ast_cli(fd, "app_rpt Debugging enabled, previous level: %d, new level: %d\n", debug, newlevel);
        else
                ast_cli(fd, "app_rpt Debugging disabled\n");

        debug = newlevel;                                                                                                                          
        return RESULT_SUCCESS;
}

/*
* Dump rpt struct debugging onto console
*/
                                                                                                                                 
static int rpt_do_dump(int fd, int argc, char *argv[])
{
	int i;

        if (argc != 3)
                return RESULT_SHOWUSAGE;

	for(i = 0; i < nrpts; i++)
	{
		if (!strcmp(argv[2],rpt_vars[i].name))
		{
			rpt_vars[i].disgorgetime = time(NULL) + 10; /* Do it 10 seconds later */
		        ast_cli(fd, "app_rpt struct dump requested for node %s\n",argv[2]);
		        return RESULT_SUCCESS;
		}
	}
	return RESULT_FAILURE;
}

/*
* Dump statistics onto console
*/

static int rpt_do_stats(int fd, int argc, char *argv[])
{
	int i,j,numoflinks;
	int dailytxtime, dailykerchunks;
	time_t now;
	int totalkerchunks, dailykeyups, totalkeyups, timeouts;
	int totalexecdcommands, dailyexecdcommands, hours, minutes, seconds;
	int uptime;
	long long totaltxtime;
	struct	rpt_link *l;
	char *listoflinks[MAX_STAT_LINKS];	
	char *lastdtmfcommand,*parrot_ena;
	char *tot_state, *ider_state, *patch_state;
	char *reverse_patch_state, *sys_ena, *tot_ena, *link_ena, *patch_ena;
	char *sch_ena, *input_signal, *called_number, *user_funs, *tail_type;
	char *iconns;
	struct rpt *myrpt;

	static char *not_applicable = "N/A";

	if(argc != 3)
		return RESULT_SHOWUSAGE;

	tot_state = ider_state = 
	patch_state = reverse_patch_state = 
	input_signal = not_applicable;
	called_number = lastdtmfcommand = NULL;

	time(&now);
	for(i = 0; i < nrpts; i++)
	{
		if (!strcmp(argv[2],rpt_vars[i].name)){
			/* Make a copy of all stat variables while locked */
			myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock); /* LOCK */
			uptime = (int)(now - starttime);
			dailytxtime = myrpt->dailytxtime;
			totaltxtime = myrpt->totaltxtime;
			dailykeyups = myrpt->dailykeyups;
			totalkeyups = myrpt->totalkeyups;
			dailykerchunks = myrpt->dailykerchunks;
			totalkerchunks = myrpt->totalkerchunks;
			dailyexecdcommands = myrpt->dailyexecdcommands;
			totalexecdcommands = myrpt->totalexecdcommands;
			timeouts = myrpt->timeouts;

			/* Traverse the list of connected nodes */
			reverse_patch_state = "DOWN";
			numoflinks = 0;
			l = myrpt->links.next;
			while(l && (l != &myrpt->links)){
				if(numoflinks >= MAX_STAT_LINKS){
					ast_log(LOG_NOTICE,
					"maximum number of links exceeds %d in rpt_do_stats()!",MAX_STAT_LINKS);
					break;
				}
				if (l->name[0] == '0'){ /* Skip '0' nodes */
					reverse_patch_state = "UP";
					l = l->next;
					continue;
				}
				listoflinks[numoflinks] = ast_strdup(l->name);
				if(listoflinks[numoflinks] == NULL){
					break;
				}
				else{
					numoflinks++;
				}
				l = l->next;
			}

			if(myrpt->keyed)
				input_signal = "YES";
			else
				input_signal = "NO";

			if(myrpt->p.parrotmode)
				parrot_ena = "ENABLED";
			else
				parrot_ena = "DISABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].txdisable)
				sys_ena = "DISABLED";
			else
				sys_ena = "ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].totdisable)
				tot_ena = "DISABLED";
			else
				tot_ena = "ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].linkfundisable)
				link_ena = "DISABLED";
			else
				link_ena = "ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].autopatchdisable)
				patch_ena = "DISABLED";
			else
				patch_ena = "ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].schedulerdisable)
				sch_ena = "DISABLED";
			else
				sch_ena = "ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].userfundisable)
				user_funs = "DISABLED";
			else
				user_funs = "ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].alternatetail)
				tail_type = "ALTERNATE";
			else
				tail_type = "STANDARD";

			if(myrpt->p.s[myrpt->p.sysstate_cur].noincomingconns)
				iconns = "DISABLED";
			else
				iconns = "ENABLED";

			if(!myrpt->totimer)
				tot_state = "TIMED OUT!";
			else if(myrpt->totimer != myrpt->p.totime)
				tot_state = "ARMED";
			else
				tot_state = "RESET";

			if(myrpt->tailid)
				ider_state = "QUEUED IN TAIL";
			else if(myrpt->mustid)
				ider_state = "QUEUED FOR CLEANUP";
			else
				ider_state = "CLEAN";

			switch(myrpt->callmode){
				case 1:
					patch_state = "DIALING";
					break;
				case 2:
					patch_state = "CONNECTING";
					break;
				case 3:
					patch_state = "UP";
					break;

				case 4:
					patch_state = "CALL FAILED";
					break;

				default:
					patch_state = "DOWN";
			}

			if(strlen(myrpt->exten)){
				called_number = ast_strdup(myrpt->exten);
			}

			if(strlen(myrpt->lastdtmfcommand)){
				lastdtmfcommand = ast_strdup(myrpt->lastdtmfcommand);
			}
			rpt_mutex_unlock(&myrpt->lock); /* UNLOCK */

			ast_cli(fd, "************************ NODE %s STATISTICS *************************\n\n", myrpt->name);
			ast_cli(fd, "Selected system state............................: %d\n", myrpt->p.sysstate_cur);
			ast_cli(fd, "Signal on input..................................: %s\n", input_signal);
			ast_cli(fd, "System...........................................: %s\n", sys_ena);
			ast_cli(fd, "Parrot Mode......................................: %s\n", parrot_ena);
			ast_cli(fd, "Scheduler........................................: %s\n", sch_ena);
			ast_cli(fd, "Tail Time........................................: %s\n", tail_type);
			ast_cli(fd, "Time out timer...................................: %s\n", tot_ena);
			ast_cli(fd, "Incoming connections.............................: %s\n", iconns);
			ast_cli(fd, "Time out timer state.............................: %s\n", tot_state);
			ast_cli(fd, "Time outs since system initialization............: %d\n", timeouts);
			ast_cli(fd, "Identifier state.................................: %s\n", ider_state);
			ast_cli(fd, "Kerchunks today..................................: %d\n", dailykerchunks);
			ast_cli(fd, "Kerchunks since system initialization............: %d\n", totalkerchunks);
			ast_cli(fd, "Keyups today.....................................: %d\n", dailykeyups);
			ast_cli(fd, "Keyups since system initialization...............: %d\n", totalkeyups);
			ast_cli(fd, "DTMF commands today..............................: %d\n", dailyexecdcommands);
			ast_cli(fd, "DTMF commands since system initialization........: %d\n", totalexecdcommands);
			ast_cli(fd, "Last DTMF command executed.......................: %s\n",
			(lastdtmfcommand && strlen(lastdtmfcommand)) ? lastdtmfcommand : not_applicable);
			hours = dailytxtime/3600000;
			dailytxtime %= 3600000;
			minutes = dailytxtime/60000;
			dailytxtime %= 60000;
			seconds = dailytxtime/1000;
			dailytxtime %= 1000;

			ast_cli(fd, "TX time today....................................: %02d:%02d:%02d:%02d\n",
				hours, minutes, seconds, dailytxtime);

			hours = (int) totaltxtime/3600000;
			totaltxtime %= 3600000;
			minutes = (int) totaltxtime/60000;
			totaltxtime %= 60000;
			seconds = (int)  totaltxtime/1000;
			totaltxtime %= 1000;

			ast_cli(fd, "TX time since system initialization..............: %02d:%02d:%02d:%02d\n",
				 hours, minutes, seconds, (int) totaltxtime);

                       	hours = uptime/3600;
                        uptime %= 3600;
                        minutes = uptime/60;
                        uptime %= 60;

                        ast_cli(fd, "Uptime...........................................: %02d:%02d:%02d\n",
                                hours, minutes, uptime);

			ast_cli(fd, "Nodes currently connected to us..................: ");
                        if(!numoflinks){
  	                      ast_cli(fd,"<NONE>");
                        }
			else{
				for(j = 0 ;j < numoflinks; j++){
					ast_cli(fd, "%s", listoflinks[j]);
					if(j % 4 == 3){
						ast_cli(fd, "\n");
						ast_cli(fd, "                                                 : ");
					}	
					else{
						if((numoflinks - 1) - j  > 0)
							ast_cli(fd, ", ");
					}
				}
			}
			ast_cli(fd,"\n");

			ast_cli(fd, "Autopatch........................................: %s\n", patch_ena);
			ast_cli(fd, "Autopatch state..................................: %s\n", patch_state);
			ast_cli(fd, "Autopatch called number..........................: %s\n",
			(called_number && strlen(called_number)) ? called_number : not_applicable);
			ast_cli(fd, "Reverse patch/IAXRPT connected...................: %s\n", reverse_patch_state);
			ast_cli(fd, "User linking commands............................: %s\n", link_ena);
			ast_cli(fd, "User functions...................................: %s\n\n", user_funs);

			for(j = 0; j < numoflinks; j++){ /* ast_free() all link names */
				ast_free(listoflinks[j]);
			}
			if(called_number){
				ast_free(called_number);
			}
			if(lastdtmfcommand){
				ast_free(lastdtmfcommand);
			}
		        return RESULT_SUCCESS;
		}
	}
	return RESULT_FAILURE;
}

/*
* Link stats function
*/

static int rpt_do_lstats(int fd, int argc, char *argv[])
{
	int i;
	char *connstate;
	struct rpt *myrpt;
	struct rpt_link *l;
	struct rpt_lstat *s,*t;
	struct rpt_lstat s_head;
	if(argc != 3)
		return RESULT_SHOWUSAGE;

	s = NULL;
	s_head.next = &s_head;
	s_head.prev = &s_head;

	for(i = 0; i < nrpts; i++)
	{
		if (!strcmp(argv[2],rpt_vars[i].name)){
			/* Make a copy of all stat variables while locked */
			myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock); /* LOCK */
			/* Traverse the list of connected nodes */
			l = myrpt->links.next;
			while(l && (l != &myrpt->links)){
				if (l->name[0] == '0'){ /* Skip '0' nodes */
					l = l->next;
					continue;
				}
				if((s = (struct rpt_lstat *) ast_malloc(sizeof(struct rpt_lstat))) == NULL){
					ast_log(LOG_ERROR, "Malloc failed in rpt_do_lstats\n");
					rpt_mutex_unlock(&myrpt->lock); /* UNLOCK */
					return RESULT_FAILURE;
				}
				memset(s, 0, sizeof(struct rpt_lstat));
				strlcpy(s->name, l->name, MAXNODESTR - 1);
				if (l->chan) pbx_substitute_variables_helper(l->chan, "${IAXPEER(CURRENTCHANNEL)}", s->peer, MAXPEERSTR - 1);
				else strcpy(s->peer,"(none)");
				s->mode = l->mode;
				s->outbound = l->outbound;
				s->reconnects = l->reconnects;
				s->connecttime = l->connecttime;
				s->thisconnected = l->thisconnected;
				memcpy(s->chan_stat,l->chan_stat,NRPTSTAT * sizeof(struct rpt_chan_stat));
				insque((struct qelem *) s, (struct qelem *) s_head.next);
				memset(l->chan_stat,0,NRPTSTAT * sizeof(struct rpt_chan_stat));
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock); /* UNLOCK */
			ast_cli(fd, "NODE      PEER                RECONNECTS  DIRECTION  CONNECT TIME        CONNECT STATE\n");
			ast_cli(fd, "----      ----                ----------  ---------  ------------        -------------\n");

			for(s = s_head.next; s != &s_head; s = s->next){
				int hours, minutes, seconds;
				long long connecttime = s->connecttime;
				char conntime[21];
				hours = connecttime/3600000L;
				connecttime %= 3600000L;
				minutes =  connecttime/60000L;
				connecttime %= 60000L;
				seconds =  connecttime/1000L;
				connecttime %= 1000L;
				snprintf(conntime, 20, "%02d:%02d:%02d:%02d",
					hours, minutes, seconds, (int) connecttime);
				conntime[20] = 0;
				if(s->thisconnected)
					connstate  = "ESTABLISHED";
				else
					connstate = "CONNECTING";
				ast_cli(fd, "%-10s%-20s%-12d%-11s%-20s%-20s\n",
					s->name, s->peer, s->reconnects, (s->outbound)? "OUT":"IN", conntime, connstate);
			}	
			/* destroy our local link queue */
			s = s_head.next;
			while(s != &s_head){
				t = s;
				s = s->next;
				remque((struct qelem *)t);
				ast_free(t);
			}			
			return RESULT_SUCCESS;
		}
	}
	return RESULT_FAILURE;
}

static int rpt_do_xnode(int fd, int argc, char *argv[])
{
	int i,j;
	char ns;
	char lbuf[MAXLINKLIST],*strs[MAXLINKLIST];
	struct rpt *myrpt;
	struct ast_var_t *newvariable;
	char *connstate;
	struct rpt_link *l;
	struct rpt_lstat *s,*t;
	struct rpt_lstat s_head;
	if(argc != 3)
		return RESULT_SHOWUSAGE;

	s = NULL;
	s_head.next = &s_head;
	s_head.prev = &s_head;


	char *parrot_ena, *sys_ena, *tot_ena, *link_ena, *patch_ena, *patch_state;
	char *sch_ena, *user_funs, *tail_type, *iconns, *tot_state, *ider_state, *tel_mode; 

	for(i = 0; i < nrpts; i++)
	{
		if (!strcmp(argv[2],rpt_vars[i].name)){
			/* Make a copy of all stat variables while locked */
			myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock); /* LOCK */

//### GET RPT STATUS STATES WHILE LOCKED ########################
			if(myrpt->p.parrotmode)
				parrot_ena = "1";	//"ENABLED";
			else
				parrot_ena = "0";	//"DISABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].txdisable)
				sys_ena = "0";	//"DISABLED";
			else
				sys_ena = "1";	//"ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].totdisable)
				tot_ena = "0";	//"DISABLED";
			else
				tot_ena = "1";	//"ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].linkfundisable)
				link_ena = "0";	//"DISABLED";
			else
				link_ena = "1";	//"ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].autopatchdisable)
				patch_ena = "0";	//"DISABLED";
			else
				patch_ena = "1";	//"ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].schedulerdisable)
				sch_ena = "0";	//"DISABLED";
			else
				sch_ena = "1";	//"ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].userfundisable)
				user_funs = "0";	//"DISABLED";
			else
				user_funs = "1";	//"ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].alternatetail)
				tail_type = "1";	//"ALTERNATE";
			else
				tail_type = "0";	//"STANDARD";

			if(myrpt->p.s[myrpt->p.sysstate_cur].noincomingconns)
				iconns = "0";	//"DISABLED";
			else
				iconns = "1";	//"ENABLED";

			if(!myrpt->totimer)
				tot_state = "0";	//"TIMED OUT!";
			else if(myrpt->totimer != myrpt->p.totime)
				tot_state = "1";	//"ARMED";
			else
				tot_state = "2";	//"RESET";

			if(myrpt->tailid)
				ider_state = "0";	//"QUEUED IN TAIL";
			else if(myrpt->mustid)
				ider_state = "1";	//"QUEUED FOR CLEANUP";
			else
				ider_state = "2";	//"CLEAN";


			switch(myrpt->callmode){
				case 1:
					patch_state = "0";		//"DIALING";
					break;
				case 2:
					patch_state = "1";		//"CONNECTING";
					break;
				case 3:
					patch_state = "2";		//"UP";
					break;

				case 4:
					patch_state = "3";		//"CALL FAILED";
					break;

				default:
					patch_state = "4";		//"DOWN";
			}


			if (myrpt->p.telemdynamic)
			{
				if(myrpt->telemmode == 0x7fffffff)
					tel_mode = "1";
				else if(myrpt->telemmode == 0x00)
					tel_mode = "0";
				else
					tel_mode = "2";
			}
			else{
				tel_mode= "3";
			}


//### GET CONNECTED NODE INFO ####################
			// Traverse the list of connected nodes 

			__mklinklist(myrpt,NULL,lbuf,0);

			j = 0;
			l = myrpt->links.next;
			while(l && (l != &myrpt->links)){
				if (l->name[0] == '0'){ // Skip '0' nodes 
					l = l->next;
					continue;
				}
				if((s = (struct rpt_lstat *) ast_malloc(sizeof(struct rpt_lstat))) == NULL){
					ast_log(LOG_ERROR, "Malloc failed in rpt_do_lstats\n");
					rpt_mutex_unlock(&myrpt->lock); // UNLOCK 
					return RESULT_FAILURE;
				}
				memset(s, 0, sizeof(struct rpt_lstat));
				strlcpy(s->name, l->name, MAXNODESTR - 1);
				if (l->chan) pbx_substitute_variables_helper(l->chan, "${IAXPEER(CURRENTCHANNEL)}", s->peer, MAXPEERSTR - 1);
				else strcpy(s->peer,"(none)");
				s->mode = l->mode;
				s->outbound = l->outbound;
				s->reconnects = l->reconnects;
				s->connecttime = l->connecttime;
				s->thisconnected = l->thisconnected;
				memcpy(s->chan_stat,l->chan_stat,NRPTSTAT * sizeof(struct rpt_chan_stat));
				insque((struct qelem *) s, (struct qelem *) s_head.next);
				memset(l->chan_stat,0,NRPTSTAT * sizeof(struct rpt_chan_stat));
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock); // UNLOCK 
			for(s = s_head.next; s != &s_head; s = s->next){
				int hours, minutes, seconds;
				long long connecttime = s->connecttime;
				char conntime[21];
				hours = connecttime/3600000L;
				connecttime %= 3600000L;
				minutes = connecttime/60000L;
				connecttime %= 60000L;
				seconds = (int)  connecttime/1000L;
				connecttime %= 1000;
				snprintf(conntime, 20, "%02d:%02d:%02d",
					hours, minutes, seconds);
				conntime[20] = 0;
				if(s->thisconnected)
					connstate  = "ESTABLISHED";
				else
					connstate = "CONNECTING";
				ast_cli(fd, "%-10s%-20s%-12d%-11s%-20s%-20s~",
					s->name, s->peer, s->reconnects, (s->outbound)? "OUT":"IN", conntime, connstate);
			}	
			ast_cli(fd,"\n\n");
			// destroy our local link queue 
			s = s_head.next;
			while(s != &s_head){
				t = s;
				s = s->next;
				remque((struct qelem *)t);
				ast_free(t);
			}	

//### GET ALL LINKED NODES INFO ####################
			/* parse em */
			ns = finddelim(lbuf,strs,MAXLINKLIST);
			/* sort em */
			if (ns) qsort((void *)strs,ns,sizeof(char *),mycompar);
			for(j = 0 ;; j++){
				if(!strs[j]){
					if(!j){
						ast_cli(fd,"<NONE>");
					}
					break;
				}
				ast_cli(fd, "%s", strs[j]);
				if(strs[j + 1])
					ast_cli(fd, ", ");

			}
			ast_cli(fd,"\n\n");

//### GET VARIABLES INFO ####################
			j = 0;
			ast_channel_lock(rpt_vars[i].rxchannel);
			AST_LIST_TRAVERSE (&rpt_vars[i].rxchannel->varshead, newvariable, entries) {
				j++;
				ast_cli(fd,"%s=%s\n", ast_var_name(newvariable), ast_var_value(newvariable));
			}
			ast_channel_unlock(rpt_vars[i].rxchannel);
			ast_cli(fd,"\n");

//### OUTPUT RPT STATUS STATES ##############
			ast_cli(fd, "parrot_ena=%s\n", parrot_ena);
			ast_cli(fd, "sys_ena=%s\n", sys_ena);
			ast_cli(fd, "tot_ena=%s\n", tot_ena);
			ast_cli(fd, "link_ena=%s\n", link_ena);
			ast_cli(fd, "patch_ena=%s\n", patch_ena);
			ast_cli(fd, "patch_state=%s\n", patch_state);
			ast_cli(fd, "sch_ena=%s\n", sch_ena);
			ast_cli(fd, "user_funs=%s\n", user_funs);
			ast_cli(fd, "tail_type=%s\n", tail_type);
			ast_cli(fd, "iconns=%s\n", iconns);
			ast_cli(fd, "tot_state=%s\n", tot_state);
			ast_cli(fd, "ider_state=%s\n", ider_state);
			ast_cli(fd, "tel_mode=%s\n\n", tel_mode);

			return RESULT_SUCCESS;
		}
	}
	return RESULT_FAILURE;
}

/*
* List all nodes connected, directly or indirectly
*/

static int rpt_do_nodes(int fd, int argc, char *argv[])
{
	int i,j;
	char ns;
	char lbuf[MAXLINKLIST],*strs[MAXLINKLIST];
	struct rpt *myrpt;
	if(argc != 3)
		return RESULT_SHOWUSAGE;

	for(i = 0; i < nrpts; i++)
	{
		if (!strcmp(argv[2],rpt_vars[i].name)){
			/* Make a copy of all stat variables while locked */
			myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock); /* LOCK */
			__mklinklist(myrpt,NULL,lbuf,0);
			rpt_mutex_unlock(&myrpt->lock); /* UNLOCK */
			/* parse em */
			ns = finddelim(lbuf,strs,MAXLINKLIST);
			/* sort em */
			if (ns) qsort((void *)strs,ns,sizeof(char *),mycompar);
			ast_cli(fd,"\n");
			ast_cli(fd, "************************* CONNECTED NODES *************************\n\n");
			for(j = 0 ;; j++){
				if(!strs[j]){
					if(!j){
						ast_cli(fd,"<NONE>");
					}
					break;
				}
				ast_cli(fd, "%s", strs[j]);
				if(j % 8 == 7){
					ast_cli(fd, "\n");
				}
				else{
					if(strs[j + 1])
						ast_cli(fd, ", ");
				}
			}
			ast_cli(fd,"\n\n");
			return RESULT_SUCCESS;
		}
	}
	return RESULT_FAILURE;
}

/*
* List all locally configured nodes
*/

static int rpt_do_local_nodes(int fd, int argc, char *argv[])
{

    int i;
    ast_cli(fd, "                         \nNode\n----\n");
    for (i=0; i< nrpts; i++)
    {
	if (rpt_vars[i].name[0])
	        ast_cli(fd, "%s\n", rpt_vars[i].name);        
    } /* for i */
    ast_cli(fd,"\n");
    return RESULT_SUCCESS;
} 


/*
* reload vars 
*/

static int rpt_do_reload(int fd, int argc, char *argv[])
{
        if (argc > 2) return RESULT_SHOWUSAGE;

	reload();

	return RESULT_FAILURE;
}

/*
* restart app_rpt
*/
                                                                                                                                 
static int rpt_do_restart(int fd, int argc, char *argv[])
{
int	i;

        if (argc > 2) return RESULT_SHOWUSAGE;
	for(i = 0; i < nrpts; i++)
	{
		if (rpt_vars[i].rxchannel) ast_softhangup(rpt_vars[i].rxchannel,AST_SOFTHANGUP_DEV);
	}
	return RESULT_FAILURE;
}


/*
* send an app_rpt DTMF function from the CLI
*/
                                                                                                                                 
static int rpt_do_fun(int fd, int argc, char *argv[])
{
	int	i,busy=0;

        if (argc != 4) return RESULT_SHOWUSAGE;

	for(i = 0; i < nrpts; i++){
		if(!strcmp(argv[2], rpt_vars[i].name)){
			struct rpt *myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock);
			if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(argv[3])){
				rpt_mutex_unlock(&myrpt->lock);
				busy=1;
			}
			if(!busy){
				myrpt->macrotimer = MACROTIME;
				strncat(myrpt->macrobuf,argv[3],MAXMACRO - 1);
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
	}
	if(busy){
		ast_cli(fd, "Function decoder busy");
	}
	return RESULT_FAILURE;
}
/*
* send an Audio File from the CLI
*/
                                                                                                                                 
static int rpt_do_playback(int fd, int argc, char *argv[])
{
	int	i;

        if (argc != 4) return RESULT_SHOWUSAGE;

	for(i = 0; i < nrpts; i++){
		if(!strcmp(argv[2], rpt_vars[i].name)){
			struct rpt *myrpt = &rpt_vars[i];
			rpt_telemetry(myrpt,PLAYBACK,argv[3]);			
		}
	}
	return RESULT_SUCCESS;
}

static int rpt_do_localplay(int fd, int argc, char *argv[])
{
        int     i;

        if (argc != 4) return RESULT_SHOWUSAGE;

        for(i = 0; i < nrpts; i++){
                if(!strcmp(argv[2], rpt_vars[i].name)){
                        struct rpt *myrpt = &rpt_vars[i];
                        rpt_telemetry(myrpt,LOCALPLAY,argv[3]);
                }
        }
        return RESULT_SUCCESS;
}

static int rpt_do_sendtext(int fd, int argc, char *argv[])
{
        int     i;
	struct	rpt_link *l;
	char str[MAX_TEXTMSG_SIZE];

        if (argc < 5) return RESULT_SHOWUSAGE;

	string_toupper(argv[2]);
	string_toupper(argv[3]);
	snprintf(str,sizeof(str) - 1,"M %s %s ",argv[2],argv[3]);
	for(i = 4; i < argc; i++)
	{
		if (i > 3) strncat(str," ",sizeof(str) - 1);
		strncat(str,argv[i],sizeof(str) - 1);
	}	
        for(i = 0; i < nrpts; i++)
	{
                if(!strcmp(argv[2], rpt_vars[i].name))
		{
                        struct rpt *myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
			/* otherwise, send it to all of em */
			while(l != &myrpt->links)
			{
				if (l->name[0] == '0') 
				{
					l = l->next;
					continue;
				}
				if (l->chan) ast_sendtext(l->chan,str);
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
	}
        return RESULT_SUCCESS;
}

//## Paging function

static int rpt_do_page(int fd, int argc, char *argv[])
{
        int     i;
	char str[MAX_TEXTMSG_SIZE];
	struct rpt_tele *telem;

        if (argc < 7) return RESULT_SHOWUSAGE;

	string_toupper(argv[2]);
	string_toupper(argv[3]);
	string_toupper(argv[4]);
	string_toupper(argv[5]);
	snprintf(str,sizeof(str) - 1,"PAGE %s %s %s ",argv[3],argv[4],argv[5]);
	for(i = 6; i < argc; i++)
	{
		if (i > 5) strncat(str," ",sizeof(str) - 1);
		strncat(str,argv[i],sizeof(str) - 1);
	}	
        for(i = 0; i < nrpts; i++)
	{
                if(!strcmp(argv[2], rpt_vars[i].name))
		{
                        struct rpt *myrpt = &rpt_vars[i];
			/* ignore if not a USB channel */
			if ((strncasecmp(myrpt->rxchannel->name,"radio/", 6) == 0) &&
			    (strncasecmp(myrpt->rxchannel->name,"voter/", 6) == 0) &&
			    (strncasecmp(myrpt->rxchannel->name,"simpleusb/", 10) == 0)) return RESULT_SUCCESS;
			telem = myrpt->tele.next;
			while(telem != &myrpt->tele)
			{
				if (((telem->mode == ID) || (telem->mode == ID1) || 
					(telem->mode == IDTALKOVER)) && (!telem->killed))
				{
					if (telem->chan) ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV); /* Whoosh! */
					telem->killed = 1;
					myrpt->deferid = 1;
				}
				telem = telem->next;
			}
			gettimeofday(&myrpt->paging,NULL);
			ast_sendtext(myrpt->rxchannel,str);
		}
	}
        return RESULT_SUCCESS;
}

//## Send to all nodes

static int rpt_do_sendall(int fd, int argc, char *argv[])
{
        int     i;
	struct	rpt_link *l;
	char str[MAX_TEXTMSG_SIZE];

        if (argc < 4) return RESULT_SHOWUSAGE;

	string_toupper(argv[2]);
	snprintf(str,sizeof(str) - 1,"M %s 0 ",argv[2]);
	for(i = 3; i < argc; i++)
	{
		if (i > 3) strncat(str," ",sizeof(str) - 1);
		strncat(str,argv[i],sizeof(str) - 1);
	}	
        for(i = 0; i < nrpts; i++)
	{
                if(!strcmp(argv[2], rpt_vars[i].name))
		{
                        struct rpt *myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
			/* otherwise, send it to all of em */
			while(l != &myrpt->links)
			{
				if (l->name[0] == '0') 
				{
					l = l->next;
					continue;
				}
				if (l->chan) ast_sendtext(l->chan,str);
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
	}
        return RESULT_SUCCESS;
}


/*
	the convention is that macros in the data from the rpt( application
	are all at the end of the data, separated by the | and start with a *
	when put into the macro buffer, the characters have their high bit
	set so the macro processor knows they came from the application data
	and to use the alt-functions table.
	sph:
*/
static int rpt_push_alt_macro(struct rpt *myrpt, char *sptr)
{
	int	busy=0;

	rpt_mutex_lock(&myrpt->lock);
	if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(sptr)){
		rpt_mutex_unlock(&myrpt->lock);
		busy=1;
	}
	if(!busy){
		int x;
		if (debug)ast_log(LOG_NOTICE, "rpt_push_alt_macro %s\n",sptr);
		myrpt->macrotimer = MACROTIME;
		for(x = 0; *(sptr + x); x++)
		    myrpt->macrobuf[x] = *(sptr + x) | 0x80;
		*(sptr + x) = 0;
	}
	rpt_mutex_unlock(&myrpt->lock);

	if(busy)ast_log(LOG_WARNING, "Function decoder busy on app_rpt command macro.\n");

	return busy;
}
/*
	allows us to test rpt() application data commands
*/
static int rpt_do_fun1(int fd, int argc, char *argv[])
{
	int	i;

    if (argc != 4) return RESULT_SHOWUSAGE;

	for(i = 0; i < nrpts; i++){
		if(!strcmp(argv[2], rpt_vars[i].name)){
			struct rpt *myrpt = &rpt_vars[i];
			rpt_push_alt_macro(myrpt,argv[3]);
		}
	}
	return RESULT_FAILURE;
}
/*
* send an app_rpt **command** from the CLI
*/

static int rpt_do_cmd(int fd, int argc, char *argv[])
{
	int i, l;
	int busy=0;
	int maxActions = sizeof(function_table)/sizeof(struct function_table_tag);

	int thisRpt = -1;
	int thisAction = -1;
	struct rpt *myrpt = NULL;
	if (argc != 6) return RESULT_SHOWUSAGE;
	
	for(i = 0; i < nrpts; i++)
	{
		if(!strcmp(argv[2], rpt_vars[i].name))
		{
			thisRpt = i;
			myrpt = &rpt_vars[i];
			break;
		} /* if !strcmp... */
	} /* for i */

	if (thisRpt < 0)
	{
		ast_cli(fd, "Unknown node number %s.\n", argv[2]);
		return RESULT_FAILURE;
	} /* if thisRpt < 0 */
	
	/* Look up the action */
	l = strlen(argv[3]);
	for(i = 0 ; i < maxActions; i++)
	{
		if(!strncasecmp(argv[3], function_table[i].action, l))
		{
			thisAction = i;
			break;
		} /* if !strncasecmp... */
	} /* for i */
	
	if (thisAction < 0)
	{
		ast_cli(fd, "Unknown action name %s.\n", argv[3]);
		return RESULT_FAILURE;
	} /* if thisAction < 0 */

	/* at this point, it looks like all the arguments make sense... */

	rpt_mutex_lock(&myrpt->lock);

	if (rpt_vars[thisRpt].cmdAction.state == CMD_STATE_IDLE)
	{
		rpt_vars[thisRpt].cmdAction.state = CMD_STATE_BUSY;
		rpt_vars[thisRpt].cmdAction.functionNumber = thisAction;
		snprintf(rpt_vars[thisRpt].cmdAction.param, MAXDTMF, "%s,%s",argv[4], argv[5]);
		strncpy(rpt_vars[thisRpt].cmdAction.digits, argv[5], MAXDTMF);
		rpt_vars[thisRpt].cmdAction.command_source = SOURCE_RPT;
		rpt_vars[thisRpt].cmdAction.state = CMD_STATE_READY;
	} /* if (rpt_vars[thisRpt].cmdAction.state == CMD_STATE_IDLE */
	else
	{
		busy = 1;
	} /* if (rpt_vars[thisRpt].cmdAction.state == CMD_STATE_IDLE */
	rpt_mutex_unlock(&myrpt->lock);

	return (busy ? RESULT_FAILURE : RESULT_SUCCESS);
} /* rpt_do_cmd() */

/*
* set a node's main channel variable from the command line 
*/
static int rpt_do_setvar(int fd, int argc, char *argv[])
{
	char *name, *value;
	int i,x,thisRpt = -1;

	if (argc < 4) return RESULT_SHOWUSAGE;
	for(i = 0; i < nrpts; i++)
	{
		if(!strcmp(argv[2], rpt_vars[i].name))
		{
			thisRpt = i;
			break;
		} 
	} 

	if (thisRpt < 0)
	{
		ast_cli(fd, "Unknown node number %s.\n", argv[2]);
		return RESULT_FAILURE;
	} 
	
	for (x = 3; x < argc; x++) {
		name = argv[x];
		if ((value = strchr(name, '='))) {
			*value++ = '\0';
			pbx_builtin_setvar_helper(rpt_vars[thisRpt].rxchannel, name, value);
		} else
			ast_log(LOG_WARNING, "Ignoring entry '%s' with no = \n", name);
	}
	return(0);
}

/*
* Display a node's main channel variables from the command line 
*/
static int rpt_do_showvars(int fd, int argc, char *argv[])
{
	int i,thisRpt = -1;
	struct ast_var_t *newvariable;

	if (argc != 3) return RESULT_SHOWUSAGE;
	for(i = 0; i < nrpts; i++)
	{
		if(!strcmp(argv[2], rpt_vars[i].name))
		{
			thisRpt = i;
			break;
		} 
	} 

	if (thisRpt < 0)
	{
		ast_cli(fd, "Unknown node number %s.\n", argv[2]);
		return RESULT_FAILURE;
	} 
	i = 0;
	ast_cli(fd,"Variable listing for node %s:\n",argv[2]);
	ast_channel_lock(rpt_vars[thisRpt].rxchannel);
	AST_LIST_TRAVERSE (&rpt_vars[thisRpt].rxchannel->varshead, newvariable, entries) {
		i++;
		ast_cli(fd,"   %s=%s\n", ast_var_name(newvariable), ast_var_value(newvariable));
	}
	ast_channel_unlock(rpt_vars[thisRpt].rxchannel);
	ast_cli(fd,"    -- %d variables\n", i);
	return(0);
}

static int rpt_do_lookup(int fd, int argc, char *argv[])
{
	struct rpt *myrpt;
	char tmp[300]="";
	int i;
	if (argc != 3) return RESULT_SHOWUSAGE;
	for( i=0; i<nrpts; i++){
		myrpt = &rpt_vars[i];
		node_lookup(myrpt,argv[2],tmp,sizeof(tmp) - 1,1);
		if(strlen(tmp))
			ast_cli(fd, "Node: %-10.10s Data: %-70.70s\n", myrpt->name, tmp);
	}
	return RESULT_SUCCESS;
}

static int play_tone_pair(struct ast_channel *chan, int f1, int f2, int duration, int amplitude)
{
	int res;

        if ((res = ast_tonepair_start(chan, f1, f2, duration, amplitude)))
                return res;
                                                                                                                                            
        while(chan->generatordata) {
		if (ast_safe_sleep(chan,1)) return -1;
	}

        return 0;
}

static int play_tone(struct ast_channel *chan, int freq, int duration, int amplitude)
{
	return play_tone_pair(chan, freq, 0, duration, amplitude);
}

#ifdef	NEW_ASTERISK

/*
 *  Hooks for CLI functions
 */
 
static char *res2cli(int r)

{
	switch (r)
	{
	    case RESULT_SUCCESS:
		return(CLI_SUCCESS);
	    case RESULT_SHOWUSAGE:
		return(CLI_SHOWUSAGE);
	    default:
		return(CLI_SUCCESS);
	}
}

static char *handle_cli_debug(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt debug level";
                e->usage = debug_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_debug(a->fd,a->argc,a->argv));
}

static char *handle_cli_dump(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt dump level";
                e->usage = dump_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_dump(a->fd,a->argc,a->argv));
}


static char *handle_cli_stats(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt stats";
                e->usage = dump_stats;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_stats(a->fd,a->argc,a->argv));
}

static char *handle_cli_nodes(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt nodes";
                e->usage = dump_nodes;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_nodes(a->fd,a->argc,a->argv));
}

static char *handle_cli_xnode(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt xnode";
                e->usage = dump_xnode;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_xnode(a->fd,a->argc,a->argv));
}

static char *handle_cli_local_nodes(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt localnodes";
                e->usage = usage_local_nodes;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_local_nodes(a->fd,a->argc,a->argv));
}

static char *handle_cli_lstats(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt lstats";
                e->usage = dump_lstats;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_lstats(a->fd,a->argc,a->argv));
}

static char *handle_cli_reload(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt reload";
                e->usage = reload_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_reload(a->fd,a->argc,a->argv));
}

static char *handle_cli_restart(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt restart";
                e->usage = restart_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_restart(a->fd,a->argc,a->argv));
}

static char *handle_cli_fun(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt fun";
                e->usage = fun_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_fun(a->fd,a->argc,a->argv));
}

static char *handle_cli_playback(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt playback";
                e->usage = playback_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_playback(a->fd,a->argc,a->argv));
}

static char *handle_cli_fun1(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt fun1";
                e->usage = fun_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_fun1(a->fd,a->argc,a->argv));
}

static char *handle_cli_cmd(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt cmd";
                e->usage = cmd_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_cmd(a->fd,a->argc,a->argv));
}

static char *handle_cli_setvar(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt setvar";
                e->usage = setvar_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_setvar(a->fd,a->argc,a->argv));
}

static char *handle_cli_showvars(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt showvars";
                e->usage = showvars_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_showvars(a->fd,a->argc,a->argv));
}

static char *handle_cli_lookup(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt lookup";
		e->usage = rpt_usage;
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_lookup(a->fd, a->argc, a->argv));
}

static char *handle_cli_localplay(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt localplay";
                e->usage = localplay_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_localplay(a->fd,a->argc,a->argv));
}

static char *handle_cli_sendall(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt sendall";
                e->usage = sendall_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_sendall(a->fd,a->argc,a->argv));
}

static char *handle_cli_sendtext(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt sendtext";
                e->usage = sendtext_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_sendtext(a->fd,a->argc,a->argv));
}

static char *handle_cli_page(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "rpt page";
                e->usage = page_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(rpt_do_page(a->fd,a->argc,a->argv));
}

static struct ast_cli_entry rpt_cli[] = {
	AST_CLI_DEFINE(handle_cli_debug,"Enable app_rpt debugging"),
	AST_CLI_DEFINE(handle_cli_dump,"Dump app_rpt structs for debugging"),
	AST_CLI_DEFINE(handle_cli_stats,"Dump node statistics"),
	AST_CLI_DEFINE(handle_cli_nodes,"Dump node list"),
	AST_CLI_DEFINE(handle_cli_xnode,"Dump extended node info"),
	AST_CLI_DEFINE(handle_cli_local_nodes,	"Dump list of local node numbers"),
	AST_CLI_DEFINE(handle_cli_lstats,"Dump link statistics"),
	AST_CLI_DEFINE(handle_cli_reload,"Reload app_rpt config"),
	AST_CLI_DEFINE(handle_cli_restart,"Restart app_rpt"),
	AST_CLI_DEFINE(handle_cli_playback,"Play Back an Audio File"),
	AST_CLI_DEFINE(handle_cli_fun,"Execute a DTMF function"),
	AST_CLI_DEFINE(handle_cli_fun1,"Execute a DTMF function"),
	AST_CLI_DEFINE(handle_cli_cmd,"Execute a DTMF function"),
	AST_CLI_DEFINE(handle_cli_setvar,"Set an Asterisk channel variable"),
	AST_CLI_DEFINE(handle_cli_showvars,"Display Asterisk channel variables"),
	AST_CLI_DEFINE(handle_cli_localplay,"Playback an audio file (local)"),
	AST_CLI_DEFINE(handle_cli_sendall,"Send a Text message to all connected nodes"),
	AST_CLI_DEFINE(handle_cli_sendtext,"Send a Text message to a specified nodes"),
	AST_CLI_DEFINE(handle_cli_page,"Send a page to a user on a node"),
	AST_CLI_DEFINE(handle_cli_lookup,"Lookup Allstar nodes")
};

#endif

/*
 * End of CLI hooks
 */

static int morse_cat(char *str, int freq, int duration)
{
	char *p;
	int len;

	if(!str)
		return -1;

	len = strlen(str);	
	p = str+len;

	if(len){
		*p++ = ',';
		*p = '\0';
	}

	snprintf(p, 62,"!%d/%d", freq, duration);
 
	return 0;
}


//## Convert string into morse code

static int send_morse(struct ast_channel *chan, char *string, int speed, int freq, int amplitude)
{

static struct morse_bits mbits[] = {
		{0, 0}, /* SPACE */
		{0, 0}, 
		{6, 18},/* " */
		{0, 0},
		{7, 72},/* $ */
		{0, 0},
		{0, 0},
		{6, 30},/* ' */
		{5, 13},/* ( */
		{6, 29},/* ) */
		{0, 0},
		{5, 10},/* + */
		{6, 51},/* , */
		{6, 33},/* - */
		{6, 42},/* . */
		{5, 9}, /* / */
		{5, 31},/* 0 */
		{5, 30},/* 1 */
		{5, 28},/* 2 */
		{5, 24},/* 3 */
		{5, 16},/* 4 */
		{5, 0}, /* 5 */
		{5, 1}, /* 6 */
		{5, 3}, /* 7 */
		{5, 7}, /* 8 */
		{5, 15},/* 9 */
		{6, 7}, /* : */
		{6, 21},/* ; */
		{0, 0},
		{5, 33},/* = */
		{0, 0},
		{6, 12},/* ? */
		{0, 0},
        	{2, 2}, /* A */
 		{4, 1}, /* B */
		{4, 5}, /* C */
		{3, 1}, /* D */
		{1, 0}, /* E */
		{4, 4}, /* F */
		{3, 3}, /* G */
		{4, 0}, /* H */
		{2, 0}, /* I */
		{4, 14},/* J */
		{3, 5}, /* K */
		{4, 2}, /* L */
		{2, 3}, /* M */
		{2, 1}, /* N */
		{3, 7}, /* O */
		{4, 6}, /* P */
		{4, 11},/* Q */
		{3, 2}, /* R */
		{3, 0}, /* S */
		{1, 1}, /* T */
		{3, 4}, /* U */
		{4, 8}, /* V */
		{3, 6}, /* W */
		{4, 9}, /* X */
		{4, 13},/* Y */
		{4, 3}  /* Z */
	};


	int dottime;
	int dashtime;
	int intralettertime;
	int interlettertime;
	int interwordtime;
	int len, ddcomb;
	int res;
	int c;
	char *str = NULL;
			
	res = 0;


	str = ast_malloc(12*8*strlen(string)); /* 12 chrs/element max, 8 elements/letter max */
	if(!str)
		return -1;
	str[0] = '\0';
	
	/* Approximate the dot time from the speed arg. */
	
	dottime = 900/speed;
	
	/* Establish timing releationships */
	
	dashtime = 3 * dottime;
	intralettertime = dottime;
	interlettertime = dottime * 4 ;
	interwordtime = dottime * 7;
	
	for(;(*string) && (!res); string++){
	
		c = *string;
		
		/* Convert lower case to upper case */
		
		if((c >= 'a') && (c <= 'z'))
			c -= 0x20;
		
		/* Can't deal with any char code greater than Z, skip it */
		
		if(c  > 'Z')
			continue;
		
		/* If space char, wait the inter word time */
					
		if(c == ' '){
			if(!res){
				if((res = morse_cat(str, 0, interwordtime)))
					break;
			}
			continue;
		}
		
		/* Subtract out control char offset to match our table */
		
		c -= 0x20;
		
		/* Get the character data */
		
		len = mbits[c].len;
		ddcomb = mbits[c].ddcomb;
		
		/* Send the character */
		
		for(; len ; len--){
			if(!res)
				res = morse_cat(str, freq, (ddcomb & 1) ? dashtime : dottime);
			if(!res)
				res = morse_cat(str, 0, intralettertime);
			ddcomb >>= 1;
		}
		
		/* Wait the interletter time */
		
		if(!res)
			res = morse_cat(str, 0, interlettertime - intralettertime);

	}
	
	/* Wait for all the characters to be sent */

	if(!res){
		if(debug > 4)
			ast_log(LOG_NOTICE,"Morse string: %s\n", str);
		ast_safe_sleep(chan,100);
		ast_playtones_start(chan, amplitude, str, 0);
		while(chan->generatordata){
			if(ast_safe_sleep(chan, 20)){
				res = -1;
				break;
			}
		}		
				 
	}
	if(str)
		ast_free(str);
	return res;
}

//# Send telemetry tones

static int send_tone_telemetry(struct ast_channel *chan, char *tonestring)
{
	char *p,*stringp;
	char *tonesubset;
	int f1,f2;
	int duration;
	int amplitude;
	int res;
	int i;
	int flags;
	
	res = 0;

	if(!tonestring)
		return res;
	
	p = stringp = ast_strdup(tonestring);

	for(;tonestring;){
		tonesubset = strsep(&stringp,")");
		if(!tonesubset)
			break;
		if(sscanf(tonesubset,"(%d,%d,%d,%d", &f1, &f2, &duration, &amplitude) != 4)
			break;
		res = play_tone_pair(chan, f1, f2, duration, amplitude);
		if(res)
			break;
	}
	if(p)
		ast_free(p);
	if(!res)
		res = play_tone_pair(chan, 0, 0, 100, 0); /* This is needed to ensure the last tone segment is timed correctly */
	
	if (!res) 
		res = ast_waitstream(chan, "");

	ast_stopstream(chan);

	/*
	* Wait for the zaptel driver to physically write the tone blocks to the hardware
	*/

	for(i = 0; i < 20 ; i++){
		flags =  DAHDI_IOMUX_WRITEEMPTY | DAHDI_IOMUX_NOWAIT; 
		res = ioctl(chan->fds[0], DAHDI_IOMUX, &flags);
		if(flags & DAHDI_IOMUX_WRITEEMPTY)
			break;
		if( ast_safe_sleep(chan, 50)){
			res = -1;
			break;
		}
	}
		
	return res;
		
}

//# Say a file - streams file to output channel

static int sayfile(struct ast_channel *mychannel,char *fname)
{
int	res;

	res = ast_streamfile(mychannel, fname, mychannel->language);
	if (!res) 
		res = ast_waitstream(mychannel, "");
	else
		 ast_log(LOG_WARNING, "ast_streamfile %s failed on %s\n", fname, mychannel->name);
	ast_stopstream(mychannel);
	return res;
}

static int saycharstr(struct ast_channel *mychannel,char *str)
{
int	res;

	res = ast_say_character_str(mychannel,str,NULL,mychannel->language);
	if (!res) 
		res = ast_waitstream(mychannel, "");
	else
		 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
	ast_stopstream(mychannel);
	return res;
}

//# Say a number -- streams corresponding sound file

static int saynum(struct ast_channel *mychannel, int num)
{
	int res;
	res = ast_say_number(mychannel, num, NULL, mychannel->language, NULL);
	if(!res)
		res = ast_waitstream(mychannel, "");
	else
		ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
	ast_stopstream(mychannel);
	return res;
}

//# Say a phonetic words -- streams corresponding sound file

static int sayphoneticstr(struct ast_channel *mychannel,char *str)
{
int	res;

	res = ast_say_phonetic_str(mychannel,str,NULL,mychannel->language);
	if (!res) 
		res = ast_waitstream(mychannel, "");
	else
		 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
	ast_stopstream(mychannel);
	return res;
}

/* say a node and nodename. Try to look in dir referred to by nodenames in
config, and see if there's a custom node file to play, and if so, play it */

static int saynode(struct rpt *myrpt, struct ast_channel *mychannel, char *name)
{
int	res = 0,tgn;
char	*val,fname[300],str[100];

	if (strlen(name) < 1) return(0);
	tgn = tlb_node_get(name,'n',NULL,str,NULL,NULL);
	if (((name[0] != '3') && (tgn != 1)) || ((name[0] == '3') && (myrpt->p.eannmode != 2)) ||
		((tgn == 1) && (myrpt->p.tannmode != 2)))
	{
		val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->name, "nodenames");
		if (!val) val = NODENAMES;
		snprintf(fname,sizeof(fname) - 1,"%s/%s",val,name);
		if (ast_fileexists(fname,NULL,mychannel->language) > 0)
			return(sayfile(mychannel,fname));
		res = sayfile(mychannel,"rpt/node");
		if (!res) 
			res = ast_say_character_str(mychannel,name,NULL,mychannel->language);
	}
	if (tgn == 1)
	{
		if (myrpt->p.tannmode < 2) return res;
		return(sayphoneticstr(mychannel,str));
	}
	if (name[0] != '3') return res;
	if (myrpt->p.eannmode < 2) return res;
	sprintf(str,"%d",atoi(name + 1));	
	if (elink_db_get(str,'n',NULL,fname,NULL) < 1) return res;
	res = sayphoneticstr(mychannel,fname);
	return res;
}

static int telem_any(struct rpt *myrpt,struct ast_channel *chan, char *entry)
{
	int res;
	char c;
	
	int morsespeed;
	int morsefreq;
	int morseampl;
	int morseidfreq;
	int morseidampl;
	
	res = 0;
	
	morsespeed = retrieve_astcfgint(myrpt, myrpt->p.morse, "speed", 5, 20, 20);
       	morsefreq = retrieve_astcfgint(myrpt, myrpt->p.morse, "frequency", 300, 3000, 800);
       	morseampl = retrieve_astcfgint(myrpt, myrpt->p.morse, "amplitude", 200, 8192, 4096);
	morseidampl = retrieve_astcfgint(myrpt, myrpt->p.morse, "idamplitude", 200, 8192, 2048);
	morseidfreq = retrieve_astcfgint(myrpt, myrpt->p.morse, "idfrequency", 300, 3000, 330);	
	
	/* Is it a file, or a tone sequence? */
			
	if(entry[0] == '|'){
		c = entry[1];
		if((c >= 'a')&&(c <= 'z'))
			c -= 0x20;
	
		switch(c){
			case 'I': /* Morse ID */
				res = send_morse(chan, entry + 2, morsespeed, morseidfreq, morseidampl);
				break;
			
			case 'M': /* Morse Message */
				res = send_morse(chan, entry + 2, morsespeed, morsefreq, morseampl);
				break;
			
			case 'T': /* Tone sequence */
				res = send_tone_telemetry(chan, entry + 2);
				break;
			default:
				res = -1;
		}
	}
	else
		res = sayfile(chan, entry); /* File */
	return res;
}

/*
* This function looks up a telemetry name in the config file, and does a telemetry response as configured.
*
* 4 types of telemtry are handled: Morse ID, Morse Message, Tone Sequence, and a File containing a recording.
*/

static int telem_lookup(struct rpt *myrpt,struct ast_channel *chan, char *node, char *name)
{
	
	int res;
	int i;
	char *entry;

	res = 0;
	entry = NULL;

	entry = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.telemetry, name);
	
	/* Try to look up the telemetry name */	

	if(!entry){
		/* Telemetry name wasn't found in the config file, use the default */
		for(i = 0; i < sizeof(tele_defs)/sizeof(struct telem_defaults) ; i++){
			if(!strcasecmp(tele_defs[i].name, name))
				entry = tele_defs[i].value;
		}
	}
	if(entry){	
		if(strlen(entry))
			if (chan) telem_any(myrpt,chan, entry);
	}
	else{
		res = -1;
	}
	return res;
}

/*
* Retrieve a wait interval
*/

static int get_wait_interval(struct rpt *myrpt, int type)
{
        int interval;
        char *wait_times;
        char *wait_times_save;
                                                                                                                  
        wait_times_save = NULL;
        wait_times = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->name, "wait_times");
                                                                                                                  
        if(wait_times){
                wait_times_save = ast_strdup(wait_times);
                if(!wait_times_save)
			return 0;
                
        }
                                                                                                                  
        switch(type){
                case DLY_TELEM:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "telemwait", 500, 5000, 1000);
                        else
                                interval = 1000;
                        break;
                                                                                                                  
                case DLY_ID:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "idwait",250,5000,500);
                        else
                                interval = 500;
                        break;
                                                                                                                  
                case DLY_UNKEY:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "unkeywait",50,5000,1000);
                        else
                                interval = 1000;
                        break;
                                                                                                                  
                case DLY_LINKUNKEY:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "linkunkeywait",500,5000,1000);
                        else
                                interval = 1000;
                        break;
                                                                                                                  
                case DLY_CALLTERM:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "calltermwait",500,5000,1500);
                        else
                                interval = 1500;
                        break;
                                                                                                                  
                case DLY_COMP:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "compwait",500,5000,200);
                        else
                                interval = 200;
                        break;
                                                                                                                  
                case DLY_PARROT:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "parrotwait",500,5000,200);
                        else
                                interval = 200;
                        break;
                case DLY_MDC1200:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "mdc1200wait",500,5000,200);
                        else
                                interval = 350;
                        break;
                default:
			interval = 0;
			break;
        }
	if(wait_times_save)
       		ast_free(wait_times_save);
	return interval;
}                                                                                                                  


/*
* Wait a configurable interval of time 
*/
static int wait_interval(struct rpt *myrpt, int type, struct ast_channel *chan)
{
	int interval;

	do {
		while (myrpt->p.holdofftelem && 
			(myrpt->keyed || (myrpt->remrx && (type != DLY_ID))))
		{
			if (ast_safe_sleep(chan,100) < 0) return -1;
		}

		interval = get_wait_interval(myrpt, type);
		if(debug)
			ast_log(LOG_NOTICE,"Delay interval = %d\n", interval);
		if(interval)
			if (ast_safe_sleep(chan,interval) < 0) return -1;
		if(debug)
			ast_log(LOG_NOTICE,"Delay complete\n");
	}
	while (myrpt->p.holdofftelem && 
		(myrpt->keyed || (myrpt->remrx && (type != DLY_ID))));
	return 0;
}

static int split_freq(char *mhz, char *decimals, char *freq);


//### BEGIN TELEMETRY CODE SECTION
/*
 * Routine to process various telemetry commands that are in the myrpt structure
 * Used extensively when links and build/torn down and other events are processed by the 
 * rpt_master threads. 
 */
 
 /*
  *
  * WARNING:  YOU ARE NOW HEADED INTO ONE GIANT MAZE OF SWITCH STATEMENTS THAT DO MOST OF THE WORK FOR
  *           APP_RPT.  THE MAJORITY OF THIS IS VERY UNDOCUMENTED CODE AND CAN BE VERY HARD TO READ. 
  *           IT IS ALSO PROBABLY THE MOST ERROR PRONE PART OF THE CODE, ESPECIALLY THE PORTIONS
  *           RELATED TO THREADED OPERATIONS.
  */
 
static void handle_varcmd_tele(struct rpt *myrpt,struct ast_channel *mychannel,char *varcmd)
{
char	*strs[100],*p,buf[100],c;
int	i,j,k,n,res,vmajor,vminor;
float	f;
time_t	t;
unsigned int t1;
#ifdef	NEW_ASTERISK
struct	ast_tm localtm;
#else
struct	tm localtm;
#endif

	n = finddelim(varcmd,strs,100);
	if (n < 1) return;
	if (!strcasecmp(strs[0],"REMGO"))
	{
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			sayfile(mychannel, "rpt/remote_go");
		return;
	}
	if (!strcasecmp(strs[0],"REMALREADY"))
	{
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			sayfile(mychannel, "rpt/remote_already");
		return;
	}
	if (!strcasecmp(strs[0],"REMNOTFOUND"))
	{
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			sayfile(mychannel, "rpt/remote_notfound");
		return;
	}
	if (!strcasecmp(strs[0],"COMPLETE"))
	{
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) return;
		res = telem_lookup(myrpt,mychannel, myrpt->name, "functcomplete");
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		return;
	}
	if (!strcasecmp(strs[0],"PROC"))
	{
		wait_interval(myrpt, DLY_TELEM, mychannel);
		res = telem_lookup(myrpt, mychannel, myrpt->name, "patchup");
		if(res < 0){ /* Then default message */
			sayfile(mychannel, "rpt/callproceeding");
		}
		return;
	}
	if (!strcasecmp(strs[0],"TERM"))
	{
		/* wait a little bit longer */
		if (wait_interval(myrpt, DLY_CALLTERM, mychannel) == -1) return;
		res = telem_lookup(myrpt, mychannel, myrpt->name, "patchdown");
		if(res < 0){ /* Then default message */
			sayfile(mychannel, "rpt/callterminated");
		}
		return;
	}
	if (!strcasecmp(strs[0],"MACRO_NOTFOUND"))
	{
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			sayfile(mychannel, "rpt/macro_notfound");
		return;
	}
	if (!strcasecmp(strs[0],"MACRO_BUSY"))
	{
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			sayfile(mychannel, "rpt/macro_busy");
		return;
	}
	if (!strcasecmp(strs[0],"CONNECTED"))
	{

		if (n < 3) return;
		if (wait_interval(myrpt, DLY_TELEM,  mychannel) == -1) return;
		res = saynode(myrpt,mychannel,strs[2]);
		if (!res)
		    res = ast_streamfile(mychannel, "rpt/connected", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		res = ast_streamfile(mychannel, "digits/2", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		saynode(myrpt,mychannel,strs[1]);
		return;
	}
	if (!strcasecmp(strs[0],"CONNFAIL"))
	{

		if (n < 2) return;			
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) return;
		res = saynode(myrpt,mychannel,strs[1]);
		if (!res) 
		   sayfile(mychannel, "rpt/connection_failed");
		return;
	}
	if (!strcasecmp(strs[0],"REMDISC"))
	{

		if (n < 2) return;			
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) return;
		res = saynode(myrpt,mychannel,strs[1]);
		if (!res) 
		   sayfile(mychannel, "rpt/remote_disc");
		return;
	}
	if (!strcasecmp(strs[0],"STATS_TIME"))
	{
		if (n < 2) return;
		if (sscanf(strs[1],"%u",&t1) != 1) return;
		t = t1;
	    	if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) return;
		rpt_localtime(&t, &localtm, myrpt->p.timezone);
		t1 = rpt_mktime(&localtm,NULL);
		/* Say the phase of the day is before the time */
		if((localtm.tm_hour >= 0) && (localtm.tm_hour < 12))
			p = "rpt/goodmorning";
		else if((localtm.tm_hour >= 12) && (localtm.tm_hour < 18))
			p = "rpt/goodafternoon";
		else
			p = "rpt/goodevening";
		if (sayfile(mychannel,p) == -1) return;
		/* Say the time is ... */		
		if (sayfile(mychannel,"rpt/thetimeis") == -1) return;
		/* Say the time */				
	    	res = ast_say_time(mychannel, t1, "", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);		
		return;
	}		
	if (!strcasecmp(strs[0],"STATS_VERSION"))
	{
		if (n < 2) return;
		if(sscanf(strs[1], "%d.%d", &vmajor, &vminor) != 2) return;
    		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) return;
		/* Say "version" */
		if (sayfile(mychannel,"rpt/version") == -1) return;
		res = ast_say_number(mychannel, vmajor, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (saycharstr(mychannel,".") == -1) return;
		if(!res) /* Say "Y" */
			ast_say_number(mychannel, vminor, "", mychannel->language, (char *) NULL);
		if (!res){
			res = ast_waitstream(mychannel, "");
			ast_stopstream(mychannel);
		}	
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		return;
	}		
	if (!strcasecmp(strs[0],"STATS_GPS"))
	{
		if (n < 5) return;
	    	if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) return;
		if (saynode(myrpt,mychannel,strs[1]) == -1) return;
		if (sayfile(mychannel,"location") == -1) return;
		c = *(strs[2] + strlen(strs[2]) - 1);
		*(strs[2] + strlen(strs[2]) - 1) = 0;
		if (sscanf(strs[2],"%2d%d.%d",&i,&j,&k) != 3) return;
		res = ast_say_number(mychannel, i, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (sayfile(mychannel,"degrees") == -1) return;
		res = ast_say_number(mychannel, j, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (saycharstr(mychannel,strs[2] + 4) == -1) return;
		if (sayfile(mychannel,"minutes") == -1) return;
		if (sayfile(mychannel,(c == 'N') ? "north" : "south") == -1) return;
		if (sayfile(mychannel,"rpt/latitude") == -1) return;
		c = *(strs[3] + strlen(strs[3]) - 1);
		*(strs[3] + strlen(strs[3]) - 1) = 0;
		if (sscanf(strs[3],"%3d%d.%d",&i,&j,&k) != 3) return;
		res = ast_say_number(mychannel, i, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (sayfile(mychannel,"degrees") == -1) return;
		res = ast_say_number(mychannel, j, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (saycharstr(mychannel,strs[3] + 5) == -1) return;
		if (sayfile(mychannel,"minutes") == -1) return;
		if (sayfile(mychannel,(c == 'E') ? "east" : "west") == -1) return;
		if (sayfile(mychannel,"rpt/longitude") == -1) return;
		if (!*strs[4]) return;
		c = *(strs[4] + strlen(strs[4]) - 1);
		*(strs[4] + strlen(strs[4]) - 1) = 0;
		if (sscanf(strs[4],"%f",&f) != 1) return;
		if (myrpt->p.gpsfeet)
		{
			if (c == 'M') f *= 3.2808399;
		}
		else
		{
			if (c != 'M') f /= 3.2808399;
		}			
		sprintf(buf,"%0.1f",f);
		if (sscanf(buf,"%d.%d",&i,&j) != 2) return;
		res = ast_say_number(mychannel, i, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (saycharstr(mychannel,".") == -1) return;
		res = ast_say_number(mychannel, j, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (sayfile(mychannel,(myrpt->p.gpsfeet) ? "feet" : "meters") == -1) return;
		if (saycharstr(mychannel,"AMSL") == -1) return;
		ast_stopstream(mychannel);	
		return;
	}
	if (!strcasecmp(strs[0],"ARB_ALPHA"))
	{
		if (n < 2) return;
	    	if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) return;
	    	saycharstr(mychannel, strs[1]);
		return;
	}		
	if (!strcasecmp(strs[0],"REV_PATCH"))
	{
		/* Parts of this section taken from app_parkandannounce */
		char *tpl_working, *tpl_current, *tpl_copy;
		char *tmp[100], *myparm;
		int looptemp=0,i=0, dres = 0;

		if (n < 3) return;
	    	if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) return;
	

		tpl_working = ast_strdup(strs[2]);
		tpl_copy = tpl_working;
		myparm = strsep(&tpl_working,"^");
		tpl_current=strsep(&tpl_working, ":");
		while(tpl_current && looptemp < sizeof(tmp)) {
			tmp[looptemp]=tpl_current;
			looptemp++;
			tpl_current=strsep(&tpl_working,":");
		}
		for(i=0; i<looptemp; i++) {
			if(!strcmp(tmp[i], "PARKED")) {
				ast_say_digits(mychannel, atoi(myparm), "", mychannel->language);
			} else if(!strcmp(tmp[i], "NODE")) {
				ast_say_digits(mychannel, atoi(strs[1]), "", mychannel->language);
			} else {
				dres = ast_streamfile(mychannel, tmp[i], mychannel->language);
				if(!dres) {
					dres = ast_waitstream(mychannel, "");
				} else {
					ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n", tmp[i], mychannel->name);
					dres = 0;
				}
			}
		}
		ast_free(tpl_copy);
		return;
	}
	if (!strcasecmp(strs[0],"LASTNODEKEY"))
	{
		if (n < 2) return;
		if (!atoi(strs[1])) return;
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) return;
		saynode(myrpt,mychannel,strs[1]);
		return;
	}
	if (!strcasecmp(strs[0],"LASTUSER"))
	{
		if (n < 2) return;
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) return;
		sayphoneticstr(mychannel,strs[1]);
		if (n < 3) return;
		sayfile(mychannel,"and");
		sayphoneticstr(mychannel,strs[2]);
		return;
	}
	if (!strcasecmp(strs[0],"STATUS"))
	{
		if (n < 3) return;
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) return;
		saynode(myrpt,mychannel,strs[1]);
		if (atoi(strs[2]) > 0) sayfile(mychannel, "rpt/autopatch_on");
		else if (n == 3)
		{
			sayfile(mychannel,"rpt/repeat_only");
			return;
		}
		for(i = 3; i < n; i++)
		{
			saynode(myrpt,mychannel,strs[i] + 1);
			if (*strs[i] == 'T') sayfile(mychannel,"rpt/tranceive");
			else if (*strs[i] == 'R') sayfile(mychannel,"rpt/monitor");
			else if (*strs[i] == 'L') sayfile(mychannel,"rpt/localmonitor");
			else sayfile(mychannel,"rpt/connecting");
		}
		return;
	}
	ast_log(LOG_WARNING,"Got unknown link telemetry command: %s\n",strs[0]);
	return;
}

/*
 *  Threaded telemetry handling routines - goes hand in hand with the previous routine (see above)
 *  This routine does a lot of processing of what you "hear" when app_rpt is running.
 *  Note that this routine could probably benefit from an overhaul to make it easier to read/debug. 
 *  Many of the items here seem to have been bolted onto this routine as it app_rpt has evolved.
 */
 
static void *rpt_tele_thread(void *this)
{
struct dahdi_confinfo ci;  /* conference info */
int	res = 0,haslink,hastx,hasremote,imdone = 0, unkeys_queued, x;
struct	rpt_tele *mytele = (struct rpt_tele *)this;
struct  rpt_tele *tlist;
struct	rpt *myrpt;
struct	rpt_link *l,*l1,linkbase;
struct	ast_channel *mychannel;
int id_malloc, vmajor, vminor, m;
char *p,*ct,*ct_copy,*ident, *nodename;
time_t t,t1,was;
#ifdef	NEW_ASTERISK
struct ast_tm localtm;
#else
struct tm localtm;
#endif
char lbuf[MAXLINKLIST],*strs[MAXLINKLIST];
int	i,j,k,ns,rbimode;
unsigned int u;
char mhz[MAXREMSTR],decimals[MAXREMSTR],mystr[200];
char	lat[100],lon[100],elev[100],c;
FILE	*fp;
float	f;
struct stat mystat;
struct dahdi_params par;
#ifdef	_MDC_ENCODE_H_
struct	mdcparams *mdcp;
#endif

	/* get a pointer to myrpt */
	myrpt = mytele->rpt;

	/* Snag copies of a few key myrpt variables */
	rpt_mutex_lock(&myrpt->lock);
	nodename = ast_strdup(myrpt->name);
	if(!nodename)
	{
	    fprintf(stderr,"rpt:Sorry unable strdup nodename\n");
	    rpt_mutex_lock(&myrpt->lock);
	    remque((struct qelem *)mytele);
	    ast_log(LOG_NOTICE,"Telemetry thread aborted at line %d, mode: %d\n",__LINE__, mytele->mode); /*@@@@@@@@@@@*/
	    rpt_mutex_unlock(&myrpt->lock);
	    ast_free(mytele);
	    pthread_exit(NULL);
	}

	if (myrpt->p.ident){
		ident = ast_strdup(myrpt->p.ident);
        	if(!ident)
		{
        	        fprintf(stderr,"rpt:Sorry unable strdup ident\n");
			rpt_mutex_lock(&myrpt->lock);
                	remque((struct qelem *)mytele);
                	ast_log(LOG_NOTICE,"Telemetry thread aborted at line %d, mode: %d\n",
			__LINE__, mytele->mode); /*@@@@@@@@@@@*/
                	rpt_mutex_unlock(&myrpt->lock);
			ast_free(nodename);
                	ast_free(mytele);
                	pthread_exit(NULL);
        	}
		else{
			id_malloc = 1;
		}
	}
	else
	{
		ident = "";
		id_malloc = 0;
	}
	rpt_mutex_unlock(&myrpt->lock);
		


	/* allocate a pseudo-channel thru asterisk */
	mychannel = ast_request(DAHDI_CHANNEL_NAME,AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!mychannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		rpt_mutex_lock(&myrpt->lock);
		remque((struct qelem *)mytele);
		ast_log(LOG_NOTICE,"Telemetry thread aborted at line %d, mode: %d\n",__LINE__, mytele->mode); /*@@@@@@@@@@@*/
		rpt_mutex_unlock(&myrpt->lock);
		ast_free(nodename);
		if(id_malloc)
			ast_free(ident);
		ast_free(mytele);		
		pthread_exit(NULL);
	}
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (mychannel->cdr) 
		ast_set_flag(mychannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	ast_answer(mychannel);
	rpt_mutex_lock(&myrpt->lock);
	mytele->chan = mychannel;
	while (myrpt->active_telem && 
	    ((myrpt->active_telem->mode == PAGE) || (
		myrpt->active_telem->mode == MDC1200)))
	{
                rpt_mutex_unlock(&myrpt->lock);
		usleep(100000);
		rpt_mutex_lock(&myrpt->lock);
	}
	rpt_mutex_unlock(&myrpt->lock);
	while((mytele->mode != SETREMOTE) && (mytele->mode != UNKEY) &&
	    (mytele->mode != LINKUNKEY) && (mytele->mode != LOCUNKEY) &&
		(mytele->mode != COMPLETE) && (mytele->mode != REMGO) && 
		    (mytele->mode != REMCOMPLETE))
	{	
                rpt_mutex_lock(&myrpt->lock);
		if ((!myrpt->active_telem) &&
			(myrpt->tele.prev == mytele))
		{
			myrpt->active_telem = mytele;
	                rpt_mutex_unlock(&myrpt->lock);
			break;
		}
                rpt_mutex_unlock(&myrpt->lock);
		usleep(100000);
	}

	/* make a conference for the tx */
	ci.chan = 0;
	/* If the telemetry is only intended for a local audience, */
	/* only connect the ID audio to the local tx conference so */
	/* linked systems can't hear it */
	ci.confno = (((mytele->mode == ID1) || (mytele->mode == PLAYBACK) ||
	    (mytele->mode == TEST_TONE) || (mytele->mode == STATS_GPS_LEGACY)) ? 
		myrpt->conf : myrpt->teleconf);
	ci.confmode = DAHDI_CONF_CONFANN;
	/* first put the channel on the conference in announce mode */
	if (ioctl(mychannel->fds[0],DAHDI_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		rpt_mutex_lock(&myrpt->lock);
		myrpt->active_telem = NULL;
		remque((struct qelem *)mytele);
		rpt_mutex_unlock(&myrpt->lock);
		ast_log(LOG_NOTICE,"Telemetry thread aborted at line %d, mode: %d\n",__LINE__, mytele->mode); /*@@@@@@@@@@@*/
		ast_free(nodename);
		if(id_malloc)
			ast_free(ident);
		ast_free(mytele);		
		ast_hangup(mychannel);
		pthread_exit(NULL);
	}
	ast_stopstream(mychannel);
	res = 0;
	switch(mytele->mode)
	{
	    case USEROUT:
		handle_userout_tele(myrpt, mychannel, mytele->param);
		imdone = 1;
		break;

	    case METER:
		handle_meter_tele(myrpt, mychannel, mytele->param);
		imdone = 1;
		break;

	    case VARCMD:
		handle_varcmd_tele(myrpt,mychannel,mytele->param);
		imdone = 1;
		break;
	    case ID:
	    case ID1:
		if (*ident)
		{
			/* wait a bit */
			if (!wait_interval(myrpt, (mytele->mode == ID) ? DLY_ID : DLY_TELEM,mychannel))
				res = telem_any(myrpt,mychannel, ident); 
		}
		imdone=1;	
		break;
		
	    case TAILMSG:
		/* wait a little bit longer */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, myrpt->p.tailmessages[myrpt->tailmessagen], mychannel->language); 
		break;

	    case IDTALKOVER:
		if(debug >= 6)
			ast_log(LOG_NOTICE,"Tracepoint IDTALKOVER: in rpt_tele_thread()\n");
	    	p = (char *) ast_variable_retrieve(myrpt->cfg, nodename, "idtalkover");
	    	if(p)
			res = telem_any(myrpt,mychannel, p); 
		imdone=1;	
	    	break;
	    		
	    case PROC:
		/* wait a little bit longer */
		if (wait_interval(myrpt, DLY_TELEM, mychannel))
			res = telem_lookup(myrpt, mychannel, myrpt->name, "patchup");
		if(res < 0){ /* Then default message */
			res = ast_streamfile(mychannel, "rpt/callproceeding", mychannel->language);
		}
		break;
	    case TERM:
		/* wait a little bit longer */
		if (!wait_interval(myrpt, DLY_CALLTERM, mychannel))
			res = telem_lookup(myrpt, mychannel, myrpt->name, "patchdown");
		if(res < 0){ /* Then default message */
			res = ast_streamfile(mychannel, "rpt/callterminated", mychannel->language);
		}
		break;
	    case COMPLETE:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = telem_lookup(myrpt,mychannel, myrpt->name, "functcomplete");
		break;
	    case REMCOMPLETE:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = telem_lookup(myrpt,mychannel, myrpt->name, "remcomplete");
		break;
	    case MACRO_NOTFOUND:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/macro_notfound", mychannel->language);
		break;
	    case MACRO_BUSY:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/macro_busy", mychannel->language);
		break;
	    case PAGE:
		if (!wait_interval(myrpt, DLY_TELEM,  mychannel))
		{
			res = -1;
			if (mytele->submode.p)
			{
			myrpt->noduck=1;
				res = ast_playtones_start(myrpt->txchannel,0,
					(char *) mytele->submode.p,0);
				while(myrpt->txchannel->generatordata)
				{
					if(ast_safe_sleep(myrpt->txchannel, 50))
					{
						res = -1;
						break;
					}
				}
				free((char *)mytele->submode.p);
			}
		}
		imdone = 1;
		break;
#ifdef	_MDC_ENCODE_H_
	    case MDC1200:
		mdcp = (struct mdcparams *)mytele->submode.p;
		if (mdcp)
		{
			if (mdcp->type[0] != 'A')
			{
				if (wait_interval(myrpt, DLY_TELEM,  mychannel) == -1)
				{
					res = -1;
					imdone = 1;
					break;
				}
			}
			else
			{
				if (wait_interval(myrpt, DLY_MDC1200,  mychannel) == -1)
				{
					res = -1;
					imdone = 1;
					break;
				}
			}
			res = mdc1200gen_start(myrpt->txchannel,mdcp->type,mdcp->UnitID,mdcp->DestID,mdcp->subcode);
			ast_free(mdcp);
			while(myrpt->txchannel->generatordata)
			{
				if(ast_safe_sleep(myrpt->txchannel, 50))
				{
					res = -1;
					break;
				}
			}
		}
		imdone = 1;
		break;
#endif
	    case UNKEY:
	    case LOCUNKEY:
		if(myrpt->patchnoct && myrpt->callmode){ /* If no CT during patch configured, then don't send one */
			imdone = 1;
			break;
		}
			
		/*
		* Reset the Unkey to CT timer
		*/

		x = get_wait_interval(myrpt, DLY_UNKEY);
		rpt_mutex_lock(&myrpt->lock);
		myrpt->unkeytocttimer = x; /* Must be protected as it is changed below */
		rpt_mutex_unlock(&myrpt->lock);

		/*
		* If there's one already queued, don't do another
		*/

		tlist = myrpt->tele.next;
		unkeys_queued = 0;
                if (tlist != &myrpt->tele)
                {
                        rpt_mutex_lock(&myrpt->lock);
                        while(tlist != &myrpt->tele){
                                if ((tlist->mode == UNKEY) || 
				    (tlist->mode == LOCUNKEY)) unkeys_queued++;
                                tlist = tlist->next;
                        }
                        rpt_mutex_unlock(&myrpt->lock);
		}
		if( unkeys_queued > 1){
			imdone = 1;
			break;
		}

		/* Wait for the telemetry timer to expire */
		/* Periodically check the timer since it can be re-initialized above */
		while(myrpt->unkeytocttimer)
		{
			int ctint;
			if(myrpt->unkeytocttimer > 100)
				ctint = 100;
			else
				ctint = myrpt->unkeytocttimer;
			ast_safe_sleep(mychannel, ctint);
			rpt_mutex_lock(&myrpt->lock);
			if(myrpt->unkeytocttimer < ctint)
				myrpt->unkeytocttimer = 0;
			else
				myrpt->unkeytocttimer -= ctint;
			rpt_mutex_unlock(&myrpt->lock);
		}
	
		/*
		* Now, the carrier on the rptr rx should be gone. 
		* If it re-appeared, then forget about sending the CT
		*/
		if(myrpt->keyed){
			imdone = 1;
			break;
		}
		
		rpt_mutex_lock(&myrpt->lock); /* Update the kerchunk counters */
		myrpt->dailykerchunks++;
		myrpt->totalkerchunks++;
		rpt_mutex_unlock(&myrpt->lock);
	
treataslocal:

		rpt_mutex_lock(&myrpt->lock);
		/* get all the nodes */
		__mklinklist(myrpt,NULL,lbuf,0);
		rpt_mutex_unlock(&myrpt->lock);
		/* parse em */
		ns = finddelim(lbuf,strs,MAXLINKLIST);
		haslink = 0;
		for(i = 0; i < ns; i++)
		{
			char *cpr = strs[i] + 1;
			if (!strcmp(cpr,myrpt->name)) continue;
			if (ISRANGER(cpr)) haslink = 1;
		}

		/* if has a RANGER node connected to it, use special telemetry for RANGER mode */
		if (haslink)
		{
			res = telem_lookup(myrpt,mychannel, myrpt->name, "ranger");
			if(res)
				ast_log(LOG_WARNING, "telem_lookup:ranger failed on %s\n", mychannel->name);
		}

		if ((mytele->mode == LOCUNKEY) &&
		    ((ct = (char *) ast_variable_retrieve(myrpt->cfg, nodename, "localct")))) { /* Local override ct */
			ct_copy = ast_strdup(ct);
			if(ct_copy)
			{
			    myrpt->noduck=1;
				res = telem_lookup(myrpt,mychannel, myrpt->name, ct_copy);
				ast_free(ct_copy);
			}
			else
				res = -1;
			if(res)
			 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", mychannel->name);		
		}
		haslink = 0;
		hastx = 0;
		hasremote = 0;		
		l = myrpt->links.next;
		if (l != &myrpt->links)
		{
			rpt_mutex_lock(&myrpt->lock);
			while(l != &myrpt->links)
			{
				int v,w;

				if (l->name[0] == '0')
				{
					l = l->next;
					continue;
				}
				w = 1;
				if (myrpt->p.nolocallinkct)
				{

					for(v = 0; v < nrpts; v++)
					{
						if (&rpt_vars[v] == myrpt) continue;
						if (rpt_vars[v].remote) continue;
						if (strcmp(rpt_vars[v].name,l->name)) continue;
						w = 0;
						break;
					}
				} 
				if (myrpt->p.locallinknodesn)
				{
					for(v = 0; v < myrpt->p.locallinknodesn; v++)
					{
						if (strcmp(l->name,myrpt->p.locallinknodes[v])) continue;
						w = 0;
						break;
					}
				}
				if (w) haslink = 1;
				if (l->mode == 1) {
					hastx++;
					if (l->isremote) hasremote++;
				}
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
		if (haslink)
		{
			myrpt->noduck=1;
			res = telem_lookup(myrpt,mychannel, myrpt->name, (!hastx) ? "remotemon" : "remotetx");
			if(res)
				ast_log(LOG_WARNING, "telem_lookup:remotexx failed on %s\n", mychannel->name);
			
		
			/* if in remote cmd mode, indicate it */
			if (myrpt->cmdnode[0] && strcmp(myrpt->cmdnode,"aprstt"))
			{
				ast_safe_sleep(mychannel,200);
				res = telem_lookup(myrpt,mychannel, myrpt->name, "cmdmode");
				if(res)
				 	ast_log(LOG_WARNING, "telem_lookup:cmdmode failed on %s\n", mychannel->name);
				ast_stopstream(mychannel);
			}
		}
		else if((ct = (char *) ast_variable_retrieve(myrpt->cfg, nodename, "unlinkedct"))){ /* Unlinked Courtesy Tone */
			ct_copy = ast_strdup(ct);
			if(ct_copy)
			{
				myrpt->noduck=1;
				res = telem_lookup(myrpt,mychannel, myrpt->name, ct_copy);
				ast_free(ct_copy);
			}
			else
				res = -1;
			if(res)
			 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", mychannel->name);		
		}	
		if (hasremote && ((!myrpt->cmdnode[0]) || (!strcmp(myrpt->cmdnode,"aprstt"))))
		{
			/* set for all to hear */
			ci.chan = 0;
			ci.confno = myrpt->conf;
			ci.confmode = DAHDI_CONF_CONFANN;
			/* first put the channel on the conference in announce mode */
			if (ioctl(mychannel->fds[0],DAHDI_SETCONF,&ci) == -1)
			{
				ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
				rpt_mutex_lock(&myrpt->lock);
				myrpt->active_telem = NULL;
				remque((struct qelem *)mytele);
				rpt_mutex_unlock(&myrpt->lock);
				ast_log(LOG_NOTICE,"Telemetry thread aborted at line %d, mode: %d\n",__LINE__, mytele->mode); /*@@@@@@@@@@@*/
				ast_free(nodename);
				if(id_malloc)
					ast_free(ident);
				ast_free(mytele);		
				ast_hangup(mychannel);
				pthread_exit(NULL);
			}
			if((ct = (char *) ast_variable_retrieve(myrpt->cfg, nodename, "remotect"))){ /* Unlinked Courtesy Tone */
				ast_safe_sleep(mychannel,200);
				ct_copy = ast_strdup(ct);
				if(ct_copy)
				{
					myrpt->noduck=1;
					res = telem_lookup(myrpt,mychannel, myrpt->name, ct_copy);
					ast_free(ct_copy);
				}
				else
					res = -1;
		
				if(res)
				 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", mychannel->name);		
			}	
		}
#if	defined(_MDC_DECODE_H_) && defined(MDC_SAY_WHEN_DOING_CT)
		if (myrpt->lastunit)
		{
			char mystr[10];

			ast_safe_sleep(mychannel,200);
			/* set for all to hear */
			ci.chan = 0;
			ci.confno = myrpt->txconf;
			ci.confmode = DAHDI_CONF_CONFANN;
			/* first put the channel on the conference in announce mode */
			if (ioctl(mychannel->fds[0],DAHDI_SETCONF,&ci) == -1)
			{
				ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
				rpt_mutex_lock(&myrpt->lock);
				myrpt->active_telem = NULL;
				remque((struct qelem *)mytele);
				rpt_mutex_unlock(&myrpt->lock);
				ast_log(LOG_NOTICE,"Telemetry thread aborted at line %d, mode: %d\n",__LINE__, mytele->mode); /*@@@@@@@@@@@*/
				ast_free(nodename);
				if(id_malloc)
					ast_free(ident);
				ast_free(mytele);		
				ast_hangup(mychannel);
				pthread_exit(NULL);
			}
			sprintf(mystr,"%04x",myrpt->lastunit);
			myrpt->lastunit = 0;
			ast_say_character_str(mychannel,mystr,NULL,mychannel->language);
			break;
		}
#endif
		imdone = 1;
		break;
	    case LINKUNKEY:
		/* if voting and a voter link unkeys but the main or another voter rx is still active */
		if(myrpt->p.votertype==1 && (myrpt->rxchankeyed || myrpt->voteremrx ))
		{
			imdone = 1;
			break;
		}
		if(myrpt->patchnoct && myrpt->callmode){ /* If no CT during patch configured, then don't send one */
			imdone = 1;
			break;
		}
		if (myrpt->p.locallinknodesn)
		{
			int v,w;

			w = 0;
			for(v = 0; v < myrpt->p.locallinknodesn; v++)
			{
				if (strcmp(mytele->mylink.name,myrpt->p.locallinknodes[v])) continue;
				w = 1;
				break;
			}
			if (w) 
			{
				/*
				* If there's one already queued, don't do another
				*/

				tlist = myrpt->tele.next;
				unkeys_queued = 0;
		                if (tlist != &myrpt->tele)
		                {
		                        rpt_mutex_lock(&myrpt->lock);
		                        while(tlist != &myrpt->tele){
		                                if ((tlist->mode == UNKEY) || 
						    (tlist->mode == LOCUNKEY)) unkeys_queued++;
		                                tlist = tlist->next;
		                        }
		                        rpt_mutex_unlock(&myrpt->lock);
				}
				if( unkeys_queued > 1){
					imdone = 1;
					break;
				}

				x = get_wait_interval(myrpt, DLY_UNKEY);
				rpt_mutex_lock(&myrpt->lock);
				myrpt->unkeytocttimer = x; /* Must be protected as it is changed below */
				rpt_mutex_unlock(&myrpt->lock);

				/* Wait for the telemetry timer to expire */
				/* Periodically check the timer since it can be re-initialized above */
				while(myrpt->unkeytocttimer)
				{
					int ctint;
					if(myrpt->unkeytocttimer > 100)
						ctint = 100;
					else
						ctint = myrpt->unkeytocttimer;
					ast_safe_sleep(mychannel, ctint);
					rpt_mutex_lock(&myrpt->lock);
					if(myrpt->unkeytocttimer < ctint)
						myrpt->unkeytocttimer = 0;
					else
						myrpt->unkeytocttimer -= ctint;
					rpt_mutex_unlock(&myrpt->lock);
				}
			}
			goto treataslocal;
		}
		if (myrpt->p.nolocallinkct) /* if no CT if this guy is on local system */
		{
			int v,w;
			w = 0;
			for(v = 0; v < nrpts; v++)
			{
				if (&rpt_vars[v] == myrpt) continue;
				if (rpt_vars[v].remote) continue;
				if (strcmp(rpt_vars[v].name,
					mytele->mylink.name)) continue;
				w = 1;
				break;
			}
			if (w) 
			{
				imdone = 1;
				break;
			}
		} 
		/*
		* Reset the Unkey to CT timer
		*/

		x = get_wait_interval(myrpt, DLY_LINKUNKEY);
		mytele->mylink.linkunkeytocttimer = x; /* Must be protected as it is changed below */

		/*
		* If there's one already queued, don't do another
		*/

		tlist = myrpt->tele.next;
		unkeys_queued = 0;
                if (tlist != &myrpt->tele)
                {
                        rpt_mutex_lock(&myrpt->lock);
                        while(tlist != &myrpt->tele){
                                if (tlist->mode == LINKUNKEY) unkeys_queued++;
                                tlist = tlist->next;
                        }
                        rpt_mutex_unlock(&myrpt->lock);
		}
		if( unkeys_queued > 1){
			imdone = 1;
			break;
		}

		/* Wait for the telemetry timer to expire */
		/* Periodically check the timer since it can be re-initialized above */
		while(mytele->mylink.linkunkeytocttimer)
		{
			int ctint;
			if(mytele->mylink.linkunkeytocttimer > 100)
				ctint = 100;
			else
				ctint = mytele->mylink.linkunkeytocttimer;
			ast_safe_sleep(mychannel, ctint);
			rpt_mutex_lock(&myrpt->lock);
			if(mytele->mylink.linkunkeytocttimer < ctint)
				mytele->mylink.linkunkeytocttimer = 0;
			else
				mytele->mylink.linkunkeytocttimer -= ctint;
			rpt_mutex_unlock(&myrpt->lock);
		}
		l = myrpt->links.next;
		unkeys_queued = 0;
                rpt_mutex_lock(&myrpt->lock);
                while (l != &myrpt->links)
                {
                        if (!strcmp(l->name,mytele->mylink.name))
			{
				unkeys_queued = l->lastrx;
				break;
                        }
                        l = l->next;
		}
                rpt_mutex_unlock(&myrpt->lock);
		if( unkeys_queued ){
			imdone = 1;
			break;
		}

		if((ct = (char *) ast_variable_retrieve(myrpt->cfg, nodename, "linkunkeyct"))){ /* Unlinked Courtesy Tone */
			ct_copy = ast_strdup(ct);
			if(ct_copy){
				res = telem_lookup(myrpt,mychannel, myrpt->name, ct_copy);
				ast_free(ct_copy);
			}
			else
				res = -1;
			if(res)
			 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", mychannel->name);		
		}	
		imdone = 1;
		break;
	    case REMDISC:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		l = myrpt->links.next;
		haslink = 0;
		/* dont report if a link for this one still on system */
		if (l != &myrpt->links)
		{
			rpt_mutex_lock(&myrpt->lock);
			while(l != &myrpt->links)
			{
				if (l->name[0] == '0')
				{
					l = l->next;
					continue;
				}
				if (!strcmp(l->name,mytele->mylink.name))
				{
					haslink = 1;
					break;
				}
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
		if (haslink)
		{
			imdone = 1;
			break;
		}
		res = saynode(myrpt,mychannel,mytele->mylink.name);
		if (!res) 
		    res = ast_streamfile(mychannel, ((mytele->mylink.hasconnected) ? 
			"rpt/remote_disc" : "rpt/remote_busy"), mychannel->language);
		break;
	    case REMALREADY:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/remote_already", mychannel->language);
		break;
	    case REMNOTFOUND:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/remote_notfound", mychannel->language);
		break;
	    case REMGO:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/remote_go", mychannel->language);
		break;
	    case CONNECTED:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM,  mychannel) == -1) break;
		res = saynode(myrpt,mychannel,mytele->mylink.name);
		if (!res)
		    res = ast_streamfile(mychannel, "rpt/connected", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		res = ast_streamfile(mychannel, "digits/2", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		res = saynode(myrpt,mychannel,myrpt->name);
		imdone = 1;
		break;
	    case CONNFAIL:
		res = saynode(myrpt,mychannel,mytele->mylink.name);
		if (!res) 
		    res = ast_streamfile(mychannel, "rpt/connection_failed", mychannel->language);
		break;
	    case MEMNOTFOUND:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/memory_notfound", mychannel->language);
		break;
	    case PLAYBACK:
            case LOCALPLAY:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		res = ast_streamfile(mychannel, mytele->param, mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		imdone = 1;
		break;
	    case TOPKEY:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		for(i = 0; i < TOPKEYN; i++)
		{
			if (!myrpt->topkey[i].node[0]) continue;
			if ((!myrpt->topkeylong) && (myrpt->topkey[i].keyed)) continue;
			res = saynode(myrpt, mychannel,	myrpt->topkey[i].node);
			if (!res) res = sayfile(mychannel,(myrpt->topkey[i].keyed) ?
				"rpt/keyedfor" : "rpt/unkeyedfor");
			if (!res) res = saynum(mychannel,
				myrpt->topkey[i].timesince);
			if (!res) res = sayfile(mychannel,"rpt/seconds");
			if (!myrpt->topkeylong) break;
		}
		imdone = 1;
		break;
	    case SETREMOTE:
		ast_mutex_lock(&myrpt->remlock);
		res = 0;
		myrpt->remsetting = 1;
		if(!strcmp(myrpt->remoterig, remote_rig_ft897))
		{
			res = set_ft897(myrpt);
		}
		if(!strcmp(myrpt->remoterig, remote_rig_ft100))
		{
			res = set_ft100(myrpt);
		}
		else if(!strcmp(myrpt->remoterig, remote_rig_ft950))
		{
			res = set_ft950(myrpt);
		}
		else if(!strcmp(myrpt->remoterig, remote_rig_tm271))
		{
			setxpmr(myrpt,0);
			res = set_tm271(myrpt);
		}
		else if(!strcmp(myrpt->remoterig, remote_rig_ic706))
		{
			res = set_ic706(myrpt);
		}
		else if(!strcmp(myrpt->remoterig, remote_rig_xcat))
		{
			res = set_xcat(myrpt);
		}
		else if(!strcmp(myrpt->remoterig, remote_rig_rbi)||!strcmp(myrpt->remoterig, remote_rig_ppp16))
		{
			if (ioperm(myrpt->p.iobase,1,1) == -1)
			{
				rpt_mutex_unlock(&myrpt->lock);
				ast_log(LOG_WARNING, "Cant get io permission on IO port %x hex\n",myrpt->p.iobase);
				res = -1;
			}
			else res = setrbi(myrpt);
		}
		else if(!strcmp(myrpt->remoterig, remote_rig_kenwood))
		{
			if (myrpt->iofd >= 0) setdtr(myrpt,myrpt->iofd,1);
			res = setkenwood(myrpt);
			if (myrpt->iofd >= 0) setdtr(myrpt,myrpt->iofd,0);
			setxpmr(myrpt,0);
			if (ast_safe_sleep(mychannel,200) == -1)
			{
				myrpt->remsetting = 0;
				ast_mutex_unlock(&myrpt->remlock);
				res = -1;
				break;
			}
			if (myrpt->iofd < 0)
			{
				i = DAHDI_FLUSH_EVENT;
				if (ioctl(myrpt->zaptxchannel->fds[0],DAHDI_FLUSH,&i) == -1)
				{
					myrpt->remsetting = 0;
					ast_mutex_unlock(&myrpt->remlock);
					ast_log(LOG_ERROR,"Cant flush events");
					res = -1;
					break;
				}
				if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_GET_PARAMS,&par) == -1)
				{
					myrpt->remsetting = 0;
					ast_mutex_unlock(&myrpt->remlock);
					ast_log(LOG_ERROR,"Cant get params");
					res = -1;
					break;
				}
				myrpt->remoterx = 
					(par.rxisoffhook || (myrpt->tele.next != &myrpt->tele));
			}
		}
		else if(!strcmp(myrpt->remoterig, remote_rig_tmd700))
		{
			res = set_tmd700(myrpt);
			setxpmr(myrpt,0);
		}

		myrpt->remsetting = 0;
		ast_mutex_unlock(&myrpt->remlock);
		if (!res)
		{
			if ((!strcmp(myrpt->remoterig, remote_rig_tm271)) ||
			   (!strcmp(myrpt->remoterig, remote_rig_kenwood)))
				telem_lookup(myrpt,mychannel, myrpt->name, "functcomplete");
			break;
		}
		/* fall thru to invalid freq */
	    case INVFREQ:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/invalid-freq", mychannel->language);
		break;
	    case REMMODE:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		switch(myrpt->remmode)
		{
		    case REM_MODE_FM:
			saycharstr(mychannel,"FM");
			break;
		    case REM_MODE_USB:
			saycharstr(mychannel,"USB");
			break;
		    case REM_MODE_LSB:
			saycharstr(mychannel,"LSB");
			break;
		    case REM_MODE_AM:
			saycharstr(mychannel,"AM");
			break;
		}
		if (!wait_interval(myrpt, DLY_COMP, mychannel))
			if (!res) res = telem_lookup(myrpt,mychannel, myrpt->name, "functcomplete");
		break;
	    case LOGINREQ:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		sayfile(mychannel,"rpt/login");
		saycharstr(mychannel,myrpt->name);
		break;
	    case REMLOGIN:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		saycharstr(mychannel,myrpt->loginuser);
		saynode(myrpt,mychannel,myrpt->name);
		wait_interval(myrpt, DLY_COMP, mychannel);
		if (!res) res = telem_lookup(myrpt,mychannel, myrpt->name, "functcomplete");
		break;
	    case REMXXX:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		res = 0;
		switch(mytele->submode.i)
		{
		    case 100: /* RX PL Off */
			sayfile(mychannel, "rpt/rxpl");
			sayfile(mychannel, "rpt/off");
			break;
		    case 101: /* RX PL On */
			sayfile(mychannel, "rpt/rxpl");
			sayfile(mychannel, "rpt/on");
			break;
		    case 102: /* TX PL Off */
			sayfile(mychannel, "rpt/txpl");
			sayfile(mychannel, "rpt/off");
			break;
		    case 103: /* TX PL On */
			sayfile(mychannel, "rpt/txpl");
			sayfile(mychannel, "rpt/on");
			break;
		    case 104: /* Low Power */
			sayfile(mychannel, "rpt/lopwr");
			break;
		    case 105: /* Medium Power */
			sayfile(mychannel, "rpt/medpwr");
			break;
		    case 106: /* Hi Power */
			sayfile(mychannel, "rpt/hipwr");
			break;
		    case 113: /* Scan down slow */
			sayfile(mychannel,"rpt/down");
			sayfile(mychannel, "rpt/slow");
			break;
		    case 114: /* Scan down quick */
			sayfile(mychannel,"rpt/down");
			sayfile(mychannel, "rpt/quick");
			break;
		    case 115: /* Scan down fast */
			sayfile(mychannel,"rpt/down");
			sayfile(mychannel, "rpt/fast");
			break;
		    case 116: /* Scan up slow */
			sayfile(mychannel,"rpt/up");
			sayfile(mychannel, "rpt/slow");
			break;
		    case 117: /* Scan up quick */
			sayfile(mychannel,"rpt/up");
			sayfile(mychannel, "rpt/quick");
			break;
		    case 118: /* Scan up fast */
			sayfile(mychannel,"rpt/up");
			sayfile(mychannel, "rpt/fast");
			break;
		    default:
			res = -1;
		}
		if (strcmp(myrpt->remoterig, remote_rig_tm271) &&
		   strcmp(myrpt->remoterig, remote_rig_kenwood))
		{
			if (!wait_interval(myrpt, DLY_COMP, mychannel))
				if (!res) res = telem_lookup(myrpt,mychannel, myrpt->name, "functcomplete");
		}
		break;
	    case SCAN:
		ast_mutex_lock(&myrpt->remlock);
		if (myrpt->hfscanstop)
		{
			myrpt->hfscanstatus = 0;
			myrpt->hfscanmode = 0;
			myrpt->hfscanstop = 0;
			mytele->mode = SCANSTAT;
			ast_mutex_unlock(&myrpt->remlock);
			if (ast_safe_sleep(mychannel,1000) == -1) break;
			sayfile(mychannel, "rpt/stop"); 
			imdone = 1;
			break;
		}
		if (myrpt->hfscanstatus > -2) service_scan(myrpt);
		i = myrpt->hfscanstatus;
		myrpt->hfscanstatus = 0;
		if (i) mytele->mode = SCANSTAT;
		ast_mutex_unlock(&myrpt->remlock);
		if (i < 0) sayfile(mychannel, "rpt/stop"); 
		else if (i > 0) saynum(mychannel,i);
		imdone = 1;
		break;
	    case TUNE:
		ast_mutex_lock(&myrpt->remlock);
		if (!strcmp(myrpt->remoterig,remote_rig_ic706))
		{
			set_mode_ic706(myrpt, REM_MODE_AM);
			if(play_tone(mychannel, 800, 6000, 8192) == -1) break;
			ast_safe_sleep(mychannel,500);
			set_mode_ic706(myrpt, myrpt->remmode);
			myrpt->tunerequest = 0;
			ast_mutex_unlock(&myrpt->remlock);
			imdone = 1;
			break;
		}
		if (!strcmp(myrpt->remoterig,remote_rig_ft100))
		{
			set_mode_ft100(myrpt, REM_MODE_AM);
			simple_command_ft100(myrpt, 0x0f, 1);
			if(play_tone(mychannel, 800, 6000, 8192) == -1) break;
			simple_command_ft100(myrpt, 0x0f, 0);
			ast_safe_sleep(mychannel,500);
			set_mode_ft100(myrpt, myrpt->remmode);
			myrpt->tunerequest = 0;
			ast_mutex_unlock(&myrpt->remlock);
			imdone = 1;
			break;
		}
		ast_safe_sleep(mychannel,500);
		set_mode_ft897(myrpt, REM_MODE_AM);
		ast_safe_sleep(mychannel,500);
		myrpt->tunetx = 1;
		if (play_tone(mychannel, 800, 6000, 8192) == -1) break;
		myrpt->tunetx = 0;
		ast_safe_sleep(mychannel,500);
		set_mode_ft897(myrpt, myrpt->remmode);
		ast_playtones_stop(mychannel);
		myrpt->tunerequest = 0;
		ast_mutex_unlock(&myrpt->remlock);
		imdone = 1;
		break;
#if 0
		set_mode_ft897(myrpt, REM_MODE_AM);
		simple_command_ft897(myrpt, 8);
		if(play_tone(mychannel, 800, 6000, 8192) == -1) break;
		simple_command_ft897(myrpt, 0x88);
		ast_safe_sleep(mychannel,500);
		set_mode_ft897(myrpt, myrpt->remmode);
		myrpt->tunerequest = 0;
		ast_mutex_unlock(&myrpt->remlock);
		imdone = 1;
		break;
#endif
	    case REMSHORTSTATUS:
	    case REMLONGSTATUS:	
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		res = saynode(myrpt,mychannel,myrpt->name);
		if(!res)
			res = sayfile(mychannel,"rpt/frequency");
		if(!res)
			res = split_freq(mhz, decimals, myrpt->freq);
		if (!multimode_capable(myrpt)) 
		{
			if (decimals[4] == '0') 
			{
				decimals[4] = 0;
				if (decimals[3] == '0') decimals[3] = 0;
			}
			decimals[5] = 0;
		}
		if(!res){
			m = atoi(mhz);
			if(m < 100)
				res = saynum(mychannel, m);
			else
				res = saycharstr(mychannel, mhz);
		}
		if(!res)
			res = sayfile(mychannel, "letters/dot");
		if(!res)
			res = saycharstr(mychannel, decimals);
	
		if(res)	break;
		if(myrpt->remmode == REM_MODE_FM){ /* Mode FM? */
			switch(myrpt->offset){
	
				case REM_MINUS:
					res = sayfile(mychannel,"rpt/minus");
					break;
				
				case REM_SIMPLEX:
					res = sayfile(mychannel,"rpt/simplex");
					break;
					
				case REM_PLUS:
					res = sayfile(mychannel,"rpt/plus");
					break;
					
				default:
					break;
			}
		}
		else{ /* Must be USB, LSB, or AM */
			switch(myrpt->remmode){

				case REM_MODE_USB:
					res = saycharstr(mychannel, "USB");
					break;

				case REM_MODE_LSB:
					res = saycharstr(mychannel, "LSB");
					break;

				case REM_MODE_AM:
					res = saycharstr(mychannel, "AM");
					break;


				default:
					break;
			}
		}

		if (res == -1) break;

		if(mytele->mode == REMSHORTSTATUS){ /* Short status? */
			if (!wait_interval(myrpt, DLY_COMP, mychannel))
				if (!res) res = telem_lookup(myrpt,mychannel, myrpt->name, "functcomplete");
			break;
		}

		if (strcmp(myrpt->remoterig,remote_rig_ic706))
		{
			switch(myrpt->powerlevel){

				case REM_LOWPWR:
					res = sayfile(mychannel,"rpt/lopwr") ;
					break;
				case REM_MEDPWR:
					res = sayfile(mychannel,"rpt/medpwr");
					break;
				case REM_HIPWR:
					res = sayfile(mychannel,"rpt/hipwr"); 
					break;
				}
		}

		rbimode = ((!strncmp(myrpt->remoterig,remote_rig_rbi,3))
		  || (!strncmp(myrpt->remoterig,remote_rig_ft100,3))
		  || (!strncmp(myrpt->remoterig,remote_rig_ic706,3)));
		if (res || (sayfile(mychannel,"rpt/rxpl") == -1)) break;
		if (rbimode && (sayfile(mychannel,"rpt/txpl") == -1)) break;
		if ((sayfile(mychannel,"rpt/frequency") == -1) ||
			(saycharstr(mychannel,myrpt->rxpl) == -1)) break;
		if ((!rbimode) && ((sayfile(mychannel,"rpt/txpl") == -1) ||
			(sayfile(mychannel,"rpt/frequency") == -1) ||
			(saycharstr(mychannel,myrpt->txpl) == -1))) break;
		if(myrpt->remmode == REM_MODE_FM){ /* Mode FM? */
			if ((sayfile(mychannel,"rpt/rxpl") == -1) ||
				(sayfile(mychannel,((myrpt->rxplon) ? "rpt/on" : "rpt/off")) == -1) ||
				(sayfile(mychannel,"rpt/txpl") == -1) ||
				(sayfile(mychannel,((myrpt->txplon) ? "rpt/on" : "rpt/off")) == -1))
				{
					break;
				}
		}
		if (!wait_interval(myrpt, DLY_COMP, mychannel))
			if (!res) res = telem_lookup(myrpt,mychannel, myrpt->name, "functcomplete");
		break;
	    case STATUS:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		hastx = 0;
		linkbase.next = &linkbase;
		linkbase.prev = &linkbase;
		rpt_mutex_lock(&myrpt->lock);
		/* make our own list of links */
		l = myrpt->links.next;
		while(l != &myrpt->links)
		{
			if (l->name[0] == '0')
			{
				l = l->next;
				continue;
			}
			l1 = ast_malloc(sizeof(struct rpt_link));
			if (!l1)
			{
				ast_log(LOG_WARNING, "Cannot alloc memory on %s\n", mychannel->name);
				remque((struct qelem *)mytele);
				myrpt->active_telem = NULL;
				rpt_mutex_unlock(&myrpt->lock);
				ast_log(LOG_NOTICE,"Telemetry thread aborted at line %d, mode: %d\n",__LINE__, mytele->mode); /*@@@@@@@@@@@*/
				ast_free(nodename);
				if(id_malloc)
					ast_free(ident);
				ast_free(mytele);		
				ast_hangup(mychannel);
				pthread_exit(NULL);
			}
			memcpy(l1,l,sizeof(struct rpt_link));
			l1->next = l1->prev = NULL;
			insque((struct qelem *)l1,(struct qelem *)linkbase.next);
			l = l->next;
		}
		rpt_mutex_unlock(&myrpt->lock);
		res = saynode(myrpt,mychannel,myrpt->name);
		if (myrpt->callmode)
		{
			hastx = 1;
			res = ast_streamfile(mychannel, "rpt/autopatch_on", mychannel->language);
			if (!res) 
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		}
		l = linkbase.next;
		while(l != &linkbase)
		{
			char *s;

			hastx = 1;
			res = saynode(myrpt,mychannel,l->name);
			s = "rpt/tranceive";
			if (!l->mode) s = "rpt/monitor";
			if (l->mode > 1) s = "rpt/localmonitor";
			if (!l->thisconnected) s = "rpt/connecting";
			res = ast_streamfile(mychannel, s, mychannel->language);
			if (!res) 
				res = ast_waitstream(mychannel, "");
			else
				ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
			l = l->next;
		}			
		if (!hastx)
		{
			res = ast_streamfile(mychannel, "rpt/repeat_only", mychannel->language);
			if (!res) 
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		}
		/* destroy our local link queue */
		l = linkbase.next;
		while(l != &linkbase)
		{
			l1 = l;
			l = l->next;
			remque((struct qelem *)l1);
			ast_free(l1);
		}			
		imdone = 1;
		break;
	    case LASTUSER:		
		if (myrpt->curdtmfuser[0])
		{
			sayphoneticstr(mychannel,myrpt->curdtmfuser);
		}			
		if (myrpt->lastdtmfuser[0] && 
			strcmp(myrpt->lastdtmfuser,myrpt->curdtmfuser))
		{
			if (myrpt->curdtmfuser[0])
				sayfile(mychannel,"and");
			sayphoneticstr(mychannel,myrpt->lastdtmfuser);
		}			
		imdone = 1;
		break;
	    case FULLSTATUS:
		rpt_mutex_lock(&myrpt->lock);
		/* get all the nodes */
		__mklinklist(myrpt,NULL,lbuf,0);
		rpt_mutex_unlock(&myrpt->lock);
		/* parse em */
		ns = finddelim(lbuf,strs,MAXLINKLIST);
		/* sort em */
		if (ns) qsort((void *)strs,ns,sizeof(char *),mycompar);
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		hastx = 0;
		res = saynode(myrpt,mychannel,myrpt->name);
		if (myrpt->callmode)
		{
			hastx = 1;
			res = ast_streamfile(mychannel, "rpt/autopatch_on", mychannel->language);
			if (!res) 
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		}
		/* go thru all the nodes in list */
		for(i = 0; i < ns; i++)
		{
			char *s,mode = 'T';

			/* if a mode spec at first, handle it */
			if ((*strs[i] < '0') || (*strs[i] > '9'))
			{
				mode = *strs[i];
				strs[i]++;
			}

			hastx = 1;
			res = saynode(myrpt,mychannel,strs[i]);
			s = "rpt/tranceive";
			if (mode == 'R') s = "rpt/monitor";
			if (mode == 'C') s = "rpt/connecting";
			res = ast_streamfile(mychannel, s, mychannel->language);
			if (!res) 
				res = ast_waitstream(mychannel, "");
			else
				ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		}			
		if (!hastx)
		{
			res = ast_streamfile(mychannel, "rpt/repeat_only", mychannel->language);
			if (!res) 
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		}
		imdone = 1;
		break;

	    case LASTNODEKEY: /* Identify last node which keyed us up */
		rpt_mutex_lock(&myrpt->lock);
		if(myrpt->lastnodewhichkeyedusup){
			p = ast_strdup(myrpt->lastnodewhichkeyedusup); /* Make a local copy of the node name */
			if(!p){
				ast_log(LOG_WARNING, "ast_strdup failed in telemetery LASTNODEKEY");
				imdone = 1;
				break;
			}
		}
		else
			p = NULL;
		rpt_mutex_unlock(&myrpt->lock);
		if(!p){
			imdone = 1; /* no node previously keyed us up, or the node which did has been disconnected */
			break;
		}
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = saynode(myrpt,mychannel,p);
		ast_free(p);
		imdone = 1;
		break;		

	    case UNAUTHTX: /* Say unauthorized transmit frequency */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		res = ast_streamfile(mychannel, "rpt/unauthtx", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		imdone = 1;
		break;

	    case PARROT: /* Repeat stuff */

		sprintf(mystr,PARROTFILE,myrpt->name,mytele->parrot);
		if (ast_fileexists(mystr,NULL,mychannel->language) <= 0)
		{
			imdone = 1;
			myrpt->parrotstate = 0;
			break;
		}
		if (wait_interval(myrpt, DLY_PARROT, mychannel) == -1) break;
		sprintf(mystr,PARROTFILE,myrpt->name,mytele->parrot);
		res = ast_streamfile(mychannel, mystr, mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		sprintf(mystr,PARROTFILE,myrpt->name,mytele->parrot);
		strcat(mystr,".wav");
		unlink(mystr);			
		imdone = 1;
		myrpt->parrotstate = 0;
		myrpt->parrotonce = 0;
		break;

	    case TIMEOUT:
		res = saynode(myrpt,mychannel,myrpt->name);
		if (!res)
		   res = ast_streamfile(mychannel, "rpt/timeout", mychannel->language);
		break;
		
	    case TIMEOUT_WARNING:
		time(&t);
		res = saynode(myrpt,mychannel,myrpt->name);
		if (!res)
		   res = ast_streamfile(mychannel, "rpt/timeout-warning", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		if(!res) /* Say number of seconds */
			ast_say_number(mychannel, myrpt->p.remotetimeout - 
			    (t - myrpt->last_activity_time), 
				"", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		res = ast_streamfile(mychannel, "queue-seconds", mychannel->language);
		break;

	    case ACT_TIMEOUT_WARNING:
		time(&t);
		res = saynode(myrpt,mychannel,myrpt->name);
		if (!res)
		    res = ast_streamfile(mychannel, "rpt/act-timeout-warning", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if(!res) /* Say number of seconds */
			ast_say_number(mychannel, myrpt->p.remoteinacttimeout - 
			    (t - myrpt->last_activity_time), 
				"", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (!res)
			res = ast_streamfile(mychannel, "queue-seconds", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);		
		imdone = 1;
		break;
		
	    case STATS_TIME:
            case STATS_TIME_LOCAL:
	    	if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		t = time(NULL);
		rpt_localtime(&t, &localtm, myrpt->p.timezone);
		t1 = rpt_mktime(&localtm,NULL);
		/* Say the phase of the day is before the time */
		if((localtm.tm_hour >= 0) && (localtm.tm_hour < 12))
			p = "rpt/goodmorning";
		else if((localtm.tm_hour >= 12) && (localtm.tm_hour < 18))
			p = "rpt/goodafternoon";
		else
			p = "rpt/goodevening";
		if (sayfile(mychannel,p) == -1)
		{
			imdone = 1;
			break;
		}
		/* Say the time is ... */		
		if (sayfile(mychannel,"rpt/thetimeis") == -1)
		{
			imdone = 1;
			break;
		}
		/* Say the time */				
	    	res = ast_say_time(mychannel, t1, "", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);		
		imdone = 1;
	    	break;
	    case STATS_VERSION:
		p = strstr(tdesc, "version");	
		if(!p)
			break;	
		if(sscanf(p, "version %d.%d", &vmajor, &vminor) != 2)
			break;
    		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		/* Say "version" */
		if (sayfile(mychannel,"rpt/version") == -1)
		{
			imdone = 1;
			break;
		}
		if(!res) /* Say "X" */
			ast_say_number(mychannel, vmajor, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (saycharstr(mychannel,".") == -1)
		{
			imdone = 1;
			break;
		}
		if(!res) /* Say "Y" */
			ast_say_number(mychannel, vminor, "", mychannel->language, (char *) NULL);
		if (!res){
			res = ast_waitstream(mychannel, "");
			ast_stopstream(mychannel);
		}	
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		imdone = 1;
	    	break;
	    case STATS_GPS:
	    case STATS_GPS_LEGACY:
		fp = fopen(GPSFILE,"r");
		if (!fp) break;
		if (fstat(fileno(fp),&mystat) == -1) break;
		if (mystat.st_size >= 100) break;
		elev[0] = 0;
		if (fscanf(fp,"%u %s %s %s",&u,lat,lon,elev) < 3) break;
		fclose(fp);
		was = (time_t) u;
		time(&t);
		if ((was + GPS_VALID_SECS) < t) break;
	    	if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		if (saynode(myrpt,mychannel,myrpt->name) == -1) break;
		if (sayfile(mychannel,"location") == -1) break;
		c = lat[strlen(lat) - 1];
		lat[strlen(lat) - 1] = 0;
		if (sscanf(lat,"%2d%d.%d",&i,&j,&k) != 3) break;
		res = ast_say_number(mychannel, i, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (sayfile(mychannel,"degrees") == -1) break;
		res = ast_say_number(mychannel, j, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (saycharstr(mychannel,lat + 4) == -1) break;
		if (sayfile(mychannel,"minutes") == -1) break;
		if (sayfile(mychannel,(c == 'N') ? "north" : "south") == -1) break;
		if (sayfile(mychannel,"rpt/latitude") == -1) break;
		c = lon[strlen(lon) - 1];
		lon[strlen(lon) - 1] = 0;
		if (sscanf(lon,"%3d%d.%d",&i,&j,&k) != 3) break;
		res = ast_say_number(mychannel, i, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (sayfile(mychannel,"degrees") == -1) break;
		res = ast_say_number(mychannel, j, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (saycharstr(mychannel,lon + 5) == -1) break;
		if (sayfile(mychannel,"minutes") == -1) break;
		if (sayfile(mychannel,(c == 'E') ? "east" : "west") == -1) break;
		if (sayfile(mychannel,"rpt/longitude") == -1) break;
		if (!elev[0]) break;
		c = elev[strlen(elev) - 1];
		elev[strlen(elev) - 1] = 0;
		if (sscanf(elev,"%f",&f) != 1) break;
		if (myrpt->p.gpsfeet)
		{
			if (c == 'M') f *= 3.2808399;
		}
		else
		{
			if (c != 'M') f /= 3.2808399;
		}			
		sprintf(mystr,"%0.1f",f);
		if (sscanf(mystr,"%d.%d",&i,&j) != 2) break;
		res = ast_say_number(mychannel, i, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (saycharstr(mychannel,".") == -1) break;
		res = ast_say_number(mychannel, j, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (sayfile(mychannel,(myrpt->p.gpsfeet) ? "feet" : "meters") == -1) break;
		if (saycharstr(mychannel,"AMSL") == -1) break;
		ast_stopstream(mychannel);	
		imdone = 1;
		break;
	    case ARB_ALPHA:
	    	if (!wait_interval(myrpt, DLY_TELEM, mychannel))
		    	if(mytele->param)
		    		saycharstr(mychannel, mytele->param);
	    	imdone = 1;
		break;
	    case REV_PATCH:
	    	if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
	    	if(mytele->param) {

			/* Parts of this section taken from app_parkandannounce */
			char *tpl_working, *tpl_current;
			char *tmp[100], *myparm;
			int looptemp=0,i=0, dres = 0;
	

			tpl_working = ast_strdup(mytele->param);
			myparm = strsep(&tpl_working,",");
			tpl_current=strsep(&tpl_working, ":");

			while(tpl_current && looptemp < sizeof(tmp)) {
				tmp[looptemp]=tpl_current;
				looptemp++;
				tpl_current=strsep(&tpl_working,":");
			}

			for(i=0; i<looptemp; i++) {
				if(!strcmp(tmp[i], "PARKED")) {
					ast_say_digits(mychannel, atoi(myparm), "", mychannel->language);
				} else if(!strcmp(tmp[i], "NODE")) {
					ast_say_digits(mychannel, atoi(myrpt->name), "", mychannel->language);
				} else {
					dres = ast_streamfile(mychannel, tmp[i], mychannel->language);
					if(!dres) {
						dres = ast_waitstream(mychannel, "");
					} else {
						ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n", tmp[i], mychannel->name);
						dres = 0;
					}
				}
			}
			ast_free(tpl_working);
		}
	    	imdone = 1;
		break;
	    case TEST_TONE:
		imdone = 1;
		if (myrpt->stopgen) break;
		myrpt->stopgen = -1;
	        if ((res = ast_tonepair_start(mychannel, 1000.0, 0, 99999999, 7200.0))) 
		{
			myrpt->stopgen = 0;
			break;
		}
	        while(mychannel->generatordata && (myrpt->stopgen <= 0)) {
			if (ast_safe_sleep(mychannel,1)) break;
		    	imdone = 1;
			}
		myrpt->stopgen = 0;
		if (myrpt->remote && (myrpt->remstopgen < 0)) myrpt->remstopgen = 1;
		break;
	    case PFXTONE:
		res = telem_lookup(myrpt,mychannel, myrpt->name, "pfxtone");
		break;
	    default:
	    	break;
	}
	if (!imdone)
	{
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else {
			ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			res = 0;
		}
	}
	ast_stopstream(mychannel);
	rpt_mutex_lock(&myrpt->lock);
	if (mytele->mode == TAILMSG)
	{
		if (!res)
		{
			myrpt->tailmessagen++;
			if(myrpt->tailmessagen >= myrpt->p.tailmessagemax) myrpt->tailmessagen = 0;
		}
		else
		{
			myrpt->tmsgtimer = myrpt->p.tailsquashedtime;
		}
	}
	remque((struct qelem *)mytele);
	myrpt->active_telem = NULL;
	rpt_mutex_unlock(&myrpt->lock);
	ast_free(nodename);
	if(id_malloc)
		ast_free(ident);
	ast_free(mytele);		
	ast_hangup(mychannel);
#ifdef  APP_RPT_LOCK_DEBUG
	{
		struct lockthread *t;

		sleep(5);
		ast_mutex_lock(&locklock);
		t = get_lockthread(pthread_self());
		if (t) memset(t,0,sizeof(struct lockthread));
		ast_mutex_unlock(&locklock);
	}			
#endif
	myrpt->noduck=0;
	pthread_exit(NULL);
}

static void send_tele_link(struct rpt *myrpt,char *cmd);

/* 
 *  More repeater telemetry routines.
 */
 
static void rpt_telemetry(struct rpt *myrpt,int mode, void *data)
{
struct rpt_tele *tele;
struct rpt_link *mylink = NULL;
int res,vmajor,vminor,i,ns;
pthread_attr_t attr;
char *v1, *v2,mystr[1024],*p,haslink,lat[100],lon[100],elev[100];
char lbuf[MAXLINKLIST],*strs[MAXLINKLIST];
time_t	t,was;
unsigned int k;
FILE *fp;
struct stat mystat;
struct rpt_link *l;

	if(debug >= 6)
		ast_log(LOG_NOTICE,"Tracepoint rpt_telemetry() entered mode=%i\n",mode);


	if ((mode == ID) && is_paging(myrpt))
	{
		myrpt->deferid = 1;
		return;
	}

	switch(mode)
	{
	    case CONNECTED:
 		mylink = (struct rpt_link *) data;
		if ((mylink->name[0] == '3') && (!myrpt->p.eannmode)) return;
		break;
	    case REMDISC:
 		mylink = (struct rpt_link *) data;
		if ((mylink->name[0] == '3') && (!myrpt->p.eannmode)) return;
		if ((!mylink) || (mylink->name[0] == '0')) return;
		if ((!mylink->gott) && (!mylink->isremote) && (!mylink->outbound) &&
		    mylink->chan && strncasecmp(mylink->chan->name,"echolink",8) &&
			strncasecmp(mylink->chan->name,"tlb",3)) return;
		break;
	    case VARCMD:
		if (myrpt->telemmode < 2) return; 
		break;
	    case UNKEY:
	    case LOCUNKEY:
		/* if voting and the main rx unkeys but a voter link is still active */
		if(myrpt->p.votertype==1 && (myrpt->rxchankeyed || myrpt->voteremrx ))
		{
			return;
		}
		if (myrpt->p.nounkeyct) return;
		/* if any of the following are defined, go ahead and do it,
		   otherwise, dont bother */
		v1 = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->name, 
			"unlinkedct");
		v2 = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->name, 
			"remotect");
		if (telem_lookup(myrpt,NULL, myrpt->name, "remotemon") &&
		  telem_lookup(myrpt,NULL, myrpt->name, "remotetx") &&
		  telem_lookup(myrpt,NULL, myrpt->name, "cmdmode") &&
		  (!(v1 && telem_lookup(myrpt,NULL, myrpt->name, v1))) && 
		  (!(v2 && telem_lookup(myrpt,NULL, myrpt->name, v2)))) return;
		break;
	    case LINKUNKEY:
 		mylink = (struct rpt_link *) data;
		if (myrpt->p.locallinknodesn)
		{
			int v,w;

			w = 0;
			for(v = 0; v < myrpt->p.locallinknodesn; v++)
			{
				if (strcmp(mylink->name,myrpt->p.locallinknodes[v])) continue;
				w = 1;
				break;
			}
			if (w) break;
		}
		if (!ast_variable_retrieve(myrpt->cfg, myrpt->name, "linkunkeyct"))
			return;
		break;
	    default:
		break;
	}
	if (!myrpt->remote) /* dont do if we are a remote */
	{
		/* send appropriate commands to everyone on link(s) */
		switch(mode)
		{

		    case REMGO:
			send_tele_link(myrpt,"REMGO");
			return;
		    case REMALREADY:
			send_tele_link(myrpt,"REMALREADY");
			return;
		    case REMNOTFOUND:
			send_tele_link(myrpt,"REMNOTFOUND");
			return;
		    case COMPLETE:
			send_tele_link(myrpt,"COMPLETE");
			return;
		    case PROC:
			send_tele_link(myrpt,"PROC");
			return;
		    case TERM:
			send_tele_link(myrpt,"TERM");
			return;
		    case MACRO_NOTFOUND:
			send_tele_link(myrpt,"MACRO_NOTFOUND");
			return;
		    case MACRO_BUSY:
			send_tele_link(myrpt,"MACRO_BUSY");
			return;
		    case CONNECTED:
			mylink = (struct rpt_link *) data;
			if ((!mylink) || (mylink->name[0] == '0')) return;
			sprintf(mystr,"CONNECTED,%s,%s",myrpt->name,mylink->name);
			send_tele_link(myrpt,mystr);
			return;
		    case CONNFAIL:
			mylink = (struct rpt_link *) data;
			if ((!mylink) || (mylink->name[0] == '0')) return;
			sprintf(mystr,"CONNFAIL,%s",mylink->name);
			send_tele_link(myrpt,mystr);
			return;
		    case REMDISC:
			mylink = (struct rpt_link *) data;
			if ((!mylink) || (mylink->name[0] == '0')) return;
			l = myrpt->links.next;
			haslink = 0;
			/* dont report if a link for this one still on system */
			if (l != &myrpt->links)
			{
				rpt_mutex_lock(&myrpt->lock);
				while(l != &myrpt->links)
				{
					if (l->name[0] == '0')
					{
						l = l->next;
						continue;
					}
					if (!strcmp(l->name,mylink->name))
					{
						haslink = 1;
						break;
					}
					l = l->next;
				}
				rpt_mutex_unlock(&myrpt->lock);
			}
			if (haslink) return;
			sprintf(mystr,"REMDISC,%s",mylink->name);
			send_tele_link(myrpt,mystr);
			return;
		    case STATS_TIME:
			t = time(NULL);
			sprintf(mystr,"STATS_TIME,%u",(unsigned int) t);
			send_tele_link(myrpt,mystr);
			return;
		    case STATS_VERSION:
			p = strstr(tdesc, "version");	
			if (!p) return;
			if(sscanf(p, "version %d.%d", &vmajor, &vminor) != 2)
				return;
			sprintf(mystr,"STATS_VERSION,%d.%d",vmajor,vminor);
			send_tele_link(myrpt,mystr);
			return;
		    case STATS_GPS:
			fp = fopen(GPSFILE,"r");
			if (!fp) break;
			if (fstat(fileno(fp),&mystat) == -1) break;
			if (mystat.st_size >= 100) break;
			elev[0] = 0;
			if (fscanf(fp,"%u %s %s %s",&k,lat,lon,elev) < 3) break;
			fclose(fp);
			was = (time_t) k;
			time(&t);
			if ((was + GPS_VALID_SECS) < t) break;
			sprintf(mystr,"STATS_GPS,%s,%s,%s,%s",myrpt->name,
				lat,lon,elev);
			send_tele_link(myrpt,mystr);
			return;
		    case ARB_ALPHA:
			sprintf(mystr,"ARB_ALPHA,%s",(char *)data);
			send_tele_link(myrpt,mystr);
			return;
		    case REV_PATCH:
			p = (char *)data;
			for(i = 0; p[i]; i++) if (p[i] == ',') p[i] = '^';
			sprintf(mystr,"REV_PATCH,%s,%s",myrpt->name,p);
			send_tele_link(myrpt,mystr);
			return;
		    case LASTNODEKEY:
			if (!myrpt->lastnodewhichkeyedusup[0]) return;
			sprintf(mystr,"LASTNODEKEY,%s",myrpt->lastnodewhichkeyedusup);
			send_tele_link(myrpt,mystr);
			return;
		    case LASTUSER:
			if ((!myrpt->lastdtmfuser[0]) && (!myrpt->curdtmfuser[0])) return;
			else if (myrpt->lastdtmfuser[0] && (!myrpt->curdtmfuser[0]))
				sprintf(mystr,"LASTUSER,%s",myrpt->lastdtmfuser);
			else if ((!myrpt->lastdtmfuser[0]) && myrpt->curdtmfuser[0])
				sprintf(mystr,"LASTUSER,%s",myrpt->curdtmfuser);
			else 
			{
				if (strcmp(myrpt->curdtmfuser,myrpt->lastdtmfuser))
					sprintf(mystr,"LASTUSER,%s,%s",myrpt->curdtmfuser,myrpt->lastdtmfuser);
				else
					sprintf(mystr,"LASTUSER,%s",myrpt->curdtmfuser);
			}
			send_tele_link(myrpt,mystr);
			return;
		    case STATUS:
			rpt_mutex_lock(&myrpt->lock);
			sprintf(mystr,"STATUS,%s,%d",myrpt->name,myrpt->callmode);
			/* make our own list of links */
			l = myrpt->links.next;
			while(l != &myrpt->links)
			{
				char s;

				if (l->name[0] == '0')
				{
					l = l->next;
					continue;
				}
				s = 'T';
				if (!l->mode) s = 'R';
				if (l->mode > 1) s = 'L';
				if (!l->thisconnected) s = 'C';
				snprintf(mystr + strlen(mystr),sizeof(mystr),",%c%s",
					s,l->name);
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
			send_tele_link(myrpt,mystr);
			return;
		    case FULLSTATUS:
			rpt_mutex_lock(&myrpt->lock);
			sprintf(mystr,"STATUS,%s,%d",myrpt->name,myrpt->callmode);
			/* get all the nodes */
			__mklinklist(myrpt,NULL,lbuf,0);
			rpt_mutex_unlock(&myrpt->lock);
			/* parse em */
			ns = finddelim(lbuf,strs,MAXLINKLIST);
			/* sort em */
			if (ns) qsort((void *)strs,ns,sizeof(char *),mycompar);
			/* go thru all the nodes in list */
			for(i = 0; i < ns; i++)
			{
				char s,m = 'T';

				/* if a mode spec at first, handle it */
				if ((*strs[i] < '0') || (*strs[i] > '9'))
				{
					m = *strs[i];
					strs[i]++;
				}
				s = 'T';
				if (m == 'R') s = 'R';
				if (m == 'C') s = 'C';
				snprintf(mystr + strlen(mystr),sizeof(mystr),",%c%s",
					s,strs[i]);
			}
			send_tele_link(myrpt,mystr);
			return;
		}
	}
	tele = ast_malloc(sizeof(struct rpt_tele));
	if (!tele)
	{
		ast_log(LOG_WARNING, "Unable to allocate memory\n");
		pthread_exit(NULL);
		return;
	}
	/* zero it out */
	memset((char *)tele,0,sizeof(struct rpt_tele));
	tele->rpt = myrpt;
	tele->mode = mode;
	if (mode == PARROT) {
		tele->submode.p = data;
		tele->parrot = (unsigned int) tele->submode.i;
		tele->submode.p = 0;
	}
	else mylink = (struct rpt_link *) (void *) data;
	rpt_mutex_lock(&myrpt->lock);
	if((mode == CONNFAIL) || (mode == REMDISC) || (mode == CONNECTED) ||
	    (mode == LINKUNKEY)){
		memset(&tele->mylink,0,sizeof(struct rpt_link));
		if (mylink){
			memcpy(&tele->mylink,mylink,sizeof(struct rpt_link));
		}
	}
	else if ((mode == ARB_ALPHA) || (mode == REV_PATCH) || 
	    (mode == PLAYBACK) || (mode == LOCALPLAY) ||
            (mode == VARCMD) || (mode == METER) || (mode == USEROUT)) {
		strncpy(tele->param, (char *) data, TELEPARAMSIZE - 1);
		tele->param[TELEPARAMSIZE - 1] = 0;
	}
	if ((mode == REMXXX) || (mode == PAGE) || (mode == MDC1200)) tele->submode.p= data;
	insque((struct qelem *)tele, (struct qelem *)myrpt->tele.next);
	rpt_mutex_unlock(&myrpt->lock);
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	res = ast_pthread_create(&tele->threadid,&attr,rpt_tele_thread,(void *) tele);
	if(res < 0){
		rpt_mutex_lock(&myrpt->lock);
		remque((struct qlem *) tele); /* We don't like stuck transmitters, remove it from the queue */
		rpt_mutex_unlock(&myrpt->lock);	
		ast_log(LOG_WARNING, "Could not create telemetry thread: %s",strerror(res));
	}
	if(debug >= 6)
			ast_log(LOG_NOTICE,"Tracepoint rpt_telemetry() exit\n");

	return;
}

//## END TELEMETRY SECTION

/* 
 *  This is the main entry point from the Asterisk call handler to app_rpt when a new "call" is detected and passed off
 *  This code sets up all the necessary variables for the rpt_master threads to take over handling/processing anything
 *  related to this call.  Calls are actually channels that are passed from the pbx application to app_rpt.
 *  
 *  NOTE: DUE TO THE WAY LATER VERSIONS OF ASTERISK PASS CALLS, ANY ATTEMPTS TO USE APP_RPT.C WITHOUT ADDING BACK IN THE
 *        "MISSING" PIECES TO THE ASTERISK CALL HANDLER WILL RESULT IN APP_RPT DROPPING ALL CALLS (CHANNELS) PASSED TO IT
 *        IMMEDIATELY AFTER THIS ROUTINE ATTEMPTS TO PASS IT TO RPT_MASTER'S THREADS.
 */
 
static void *rpt_call(void *this)
{
struct dahdi_confinfo ci;  /* conference info */
struct	rpt *myrpt = (struct rpt *)this;
int	res;
int stopped,congstarted,dialtimer,lastcidx,aborted,sentpatchconnect;
struct ast_channel *mychannel,*genchannel,*c;

	myrpt->mydtmf = 0;
	/* allocate a pseudo-channel thru asterisk */
	mychannel = ast_request(DAHDI_CHANNEL_NAME,AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!mychannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		pthread_exit(NULL);
	}
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (mychannel->cdr)
		ast_set_flag(mychannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	ast_answer(mychannel);
	ci.chan = 0;
	ci.confno = myrpt->conf; /* use the pseudo conference */
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;
	/* first put the channel on the conference */
	if (ioctl(mychannel->fds[0],DAHDI_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		ast_hangup(mychannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	/* allocate a pseudo-channel thru asterisk */
	genchannel = ast_request(DAHDI_CHANNEL_NAME,AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!genchannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		ast_hangup(mychannel);
		pthread_exit(NULL);
	}
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (genchannel->cdr)
		ast_set_flag(genchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	ast_answer(genchannel);
	ci.chan = 0;
	ci.confno = myrpt->conf;
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;
	/* first put the channel on the conference */
	if (ioctl(genchannel->fds[0],DAHDI_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	if (myrpt->p.tonezone && (tone_zone_set_zone(mychannel->fds[0],myrpt->p.tonezone) == -1))
	{
		ast_log(LOG_WARNING, "Unable to set tone zone %s\n",myrpt->p.tonezone);
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	if (myrpt->p.tonezone && (tone_zone_set_zone(genchannel->fds[0],myrpt->p.tonezone) == -1))
	{
		ast_log(LOG_WARNING, "Unable to set tone zone %s\n",myrpt->p.tonezone);
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	/* start dialtone if patchquiet is 0. Special patch modes don't send dial tone */
	if ((!myrpt->patchquiet) && (!myrpt->patchexten[0]) 
		&& (tone_zone_play_tone(genchannel->fds[0],DAHDI_TONE_DIALTONE) < 0))
	{
		ast_log(LOG_WARNING, "Cannot start dialtone\n");
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	stopped = 0;
	congstarted = 0;
	dialtimer = 0;
	lastcidx = 0;
	myrpt->calldigittimer = 0;
	aborted = 0;

	if (myrpt->patchexten[0])
	{
		strcpy(myrpt->exten,myrpt->patchexten);
		myrpt->callmode = 2;
	}
	while ((myrpt->callmode == 1) || (myrpt->callmode == 4))
	{
		if((myrpt->patchdialtime)&&(myrpt->callmode == 1)&&(myrpt->cidx != lastcidx)){
			dialtimer = 0;
			lastcidx = myrpt->cidx;
		}		

		if((myrpt->patchdialtime)&&(dialtimer >= myrpt->patchdialtime)){ 
		    if(debug)
		    	ast_log(LOG_NOTICE, "dialtimer %i > patchdialtime %i\n", dialtimer,myrpt->patchdialtime);
			rpt_mutex_lock(&myrpt->lock);
			aborted = 1;
			myrpt->callmode = 0;
			rpt_mutex_unlock(&myrpt->lock);
			break;
		}
	
		if ((!myrpt->patchquiet) && (!stopped) && (myrpt->callmode == 1) && (myrpt->cidx > 0))
		{
			stopped = 1;
			/* stop dial tone */
			tone_zone_play_tone(genchannel->fds[0],-1);
		}
		if (myrpt->callmode == 1)
		{
			if(myrpt->calldigittimer > PATCH_DIALPLAN_TIMEOUT)
			{
				myrpt->callmode = 2;
				break;
			}
			/* bump timer if active */
			if (myrpt->calldigittimer) 
				myrpt->calldigittimer += MSWAIT;
		}
		if (myrpt->callmode == 4)
		{
			if(!congstarted){
				congstarted = 1;
				/* start congestion tone */
				tone_zone_play_tone(genchannel->fds[0],DAHDI_TONE_CONGESTION);
			}
		}
		res = ast_safe_sleep(mychannel, MSWAIT);
		if (res < 0)
		{
			if(debug)
		    		ast_log(LOG_NOTICE, "ast_safe_sleep=%i\n", res);
			ast_hangup(mychannel);
			ast_hangup(genchannel);
			rpt_mutex_lock(&myrpt->lock);
			myrpt->callmode = 0;
			rpt_mutex_unlock(&myrpt->lock);
			pthread_exit(NULL);
		}
		dialtimer += MSWAIT;
	}
	/* stop any tone generation */
	tone_zone_play_tone(genchannel->fds[0],-1);
	/* end if done */
	if (!myrpt->callmode)
	{
		if(debug)
			ast_log(LOG_NOTICE, "callmode==0\n");
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		rpt_mutex_lock(&myrpt->lock);
		myrpt->callmode = 0;
		myrpt->macropatch=0;
		channel_revert(myrpt);
		rpt_mutex_unlock(&myrpt->lock);
		if((!myrpt->patchquiet) && aborted)
			rpt_telemetry(myrpt, TERM, NULL);
		pthread_exit(NULL);			
	}

	if (myrpt->p.ourcallerid && *myrpt->p.ourcallerid){
		char *name, *loc, *instr;
		instr = ast_strdup(myrpt->p.ourcallerid);
		if(instr){
			ast_callerid_parse(instr, &name, &loc);
			if(loc){
				if(mychannel->cid.cid_num)
					ast_free(mychannel->cid.cid_num);
				mychannel->cid.cid_num = ast_strdup(loc);
			}
			if(name){
				if(mychannel->cid.cid_name)
					ast_free(mychannel->cid.cid_name);
				mychannel->cid.cid_name = ast_strdup(name);
			}
			ast_free(instr);
		}
	}

	ast_copy_string(mychannel->exten, myrpt->exten, sizeof(mychannel->exten) - 1);
	ast_copy_string(mychannel->context, myrpt->patchcontext, sizeof(mychannel->context) - 1);
	
	if (myrpt->p.acctcode)
		ast_cdr_setaccount(mychannel,myrpt->p.acctcode);
	mychannel->priority = 1;
	ast_channel_undefer_dtmf(mychannel);
	if (ast_pbx_start(mychannel) < 0)
	{
		ast_log(LOG_WARNING, "Unable to start PBX!!\n");
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		rpt_mutex_lock(&myrpt->lock);
	 	myrpt->callmode = 0;
		rpt_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	usleep(10000);
	rpt_mutex_lock(&myrpt->lock);
	myrpt->callmode = 3;
	/* set appropriate conference for the pseudo */
	ci.chan = 0;
	ci.confno = myrpt->conf;
	ci.confmode = (myrpt->p.duplex == 2) ? DAHDI_CONF_CONFANNMON :
		(DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER);
	if (mychannel->pbx)
	{
		/* first put the channel on the conference in announce mode */
		if (ioctl(myrpt->pchannel->fds[0],DAHDI_SETCONF,&ci) == -1)
		{
			ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
			ast_hangup(mychannel);
			ast_hangup(genchannel);
			myrpt->callmode = 0;
			rpt_mutex_unlock(&myrpt->lock);
			pthread_exit(NULL);
		}
		/* get its channel number */
		res = 0;
		if (ioctl(mychannel->fds[0],DAHDI_CHANNO,&res) == -1)
		{
			ast_log(LOG_WARNING, "Unable to get autopatch channel number\n");
			ast_hangup(mychannel);
			myrpt->callmode = 0;
			rpt_mutex_unlock(&myrpt->lock);
			pthread_exit(NULL);
		}
		ci.chan = 0;
		ci.confno = res;
		ci.confmode = DAHDI_CONF_MONITOR;
		/* put vox channel monitoring on the channel  */
		if (ioctl(myrpt->voxchannel->fds[0],DAHDI_SETCONF,&ci) == -1)
		{
			ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
			ast_hangup(mychannel);
			myrpt->callmode = 0;
			pthread_exit(NULL);
		}
	}
	sentpatchconnect = 0;
	while(myrpt->callmode)
	{
		if ((!mychannel->pbx) && (myrpt->callmode != 4))
		{
		    /* If patch is setup for far end disconnect */
			if(myrpt->patchfarenddisconnect || (myrpt->p.duplex < 2)){ 
				if(debug)ast_log(LOG_NOTICE,"callmode=%i, patchfarenddisconnect=%i, duplex=%i\n",\
						myrpt->callmode,myrpt->patchfarenddisconnect,myrpt->p.duplex);
				myrpt->callmode = 0;
				myrpt->macropatch=0;
				if(!myrpt->patchquiet){
					rpt_mutex_unlock(&myrpt->lock);
					rpt_telemetry(myrpt, TERM, NULL);
					rpt_mutex_lock(&myrpt->lock);
				}
			}
			else{ /* Send congestion until patch is downed by command */
				myrpt->callmode = 4;
				rpt_mutex_unlock(&myrpt->lock);
				/* start congestion tone */
				tone_zone_play_tone(genchannel->fds[0],DAHDI_TONE_CONGESTION);
				rpt_mutex_lock(&myrpt->lock);
			}
		}
		c = ast_bridged_channel(mychannel);
		if (c && c->_state == AST_STATE_UP)
		if ((!sentpatchconnect) &&
			myrpt->p.patchconnect && 
			c && (c->_state == AST_STATE_UP))
		{
			sentpatchconnect = 1;			
			rpt_telemetry(myrpt,PLAYBACK,myrpt->p.patchconnect);
		}
		if (myrpt->mydtmf)
		{
			struct ast_frame wf = {AST_FRAME_DTMF, } ;

			wf.subclass = myrpt->mydtmf;
			if (c && c->_state == AST_STATE_UP)
			{
				rpt_mutex_unlock(&myrpt->lock);
				ast_queue_frame(mychannel,&wf);
#ifdef	NEW_ASTERISK
				ast_senddigit(genchannel,myrpt->mydtmf,0);
#else
				ast_senddigit(genchannel,myrpt->mydtmf);
#endif
				rpt_mutex_lock(&myrpt->lock);
			}
			myrpt->mydtmf = 0;
		}
		rpt_mutex_unlock(&myrpt->lock);
		usleep(MSWAIT * 1000);
		rpt_mutex_lock(&myrpt->lock);
	}
	if(debug)
		ast_log(LOG_NOTICE, "exit channel loop\n");
	rpt_mutex_unlock(&myrpt->lock);
	tone_zone_play_tone(genchannel->fds[0],-1);
	if (mychannel->pbx) ast_softhangup(mychannel,AST_SOFTHANGUP_DEV);
	ast_hangup(genchannel);
	rpt_mutex_lock(&myrpt->lock);
	myrpt->callmode = 0;
	myrpt->macropatch=0;
	channel_revert(myrpt);
	rpt_mutex_unlock(&myrpt->lock);
	/* set appropriate conference for the pseudo */
	ci.chan = 0;
	ci.confno = myrpt->conf;
	ci.confmode = ((myrpt->p.duplex == 2) || (myrpt->p.duplex == 4)) ? DAHDI_CONF_CONFANNMON :
		(DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER);
	/* first put the channel on the conference in announce mode */
	if (ioctl(myrpt->pchannel->fds[0],DAHDI_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
	}
	pthread_exit(NULL);
}

static void send_link_dtmf(struct rpt *myrpt,char c)
{
char	str[300];
struct	ast_frame wf;
struct	rpt_link *l;


	snprintf(str, sizeof(str), "D %s %s %d %c", myrpt->cmdnode, myrpt->name, ++(myrpt->dtmfidx), c);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	wf.src = "send_link_dtmf";
	l = myrpt->links.next;
	/* first, see if our dude is there */
	while(l != &myrpt->links)
	{
		if (l->name[0] == '0') 
		{
			l = l->next;
			continue;
		}
		/* if we found it, write it and were done */
		if (!strcmp(l->name,myrpt->cmdnode))
		{
			AST_FRAME_DATA(wf) = str;
			if (l->chan) rpt_qwrite(l,&wf);
			return;
		}
		l = l->next;
	}
	l = myrpt->links.next;
	/* if not, give it to everyone */
	while(l != &myrpt->links)
	{
		AST_FRAME_DATA(wf) = str;
		if (l->chan) rpt_qwrite(l,&wf);
		l = l->next;
	}
	return;
}

static void send_link_keyquery(struct rpt *myrpt)
{
char	str[300];
struct	ast_frame wf;
struct	rpt_link *l;

	rpt_mutex_lock(&myrpt->lock);
	memset(myrpt->topkey,0,sizeof(myrpt->topkey));
	myrpt->topkeystate = 1;
	time(&myrpt->topkeytime);
	rpt_mutex_unlock(&myrpt->lock);
	snprintf(str, sizeof(str), "K? * %s 0 0", myrpt->name);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	wf.src = "send_link_keyquery";
	l = myrpt->links.next;
	/* give it to everyone */
	while(l != &myrpt->links)
	{
		AST_FRAME_DATA(wf) = str;
		if (l->chan) rpt_qwrite(l,&wf);
		l = l->next;
	}
	return;
}

static void send_tele_link(struct rpt *myrpt,char *cmd)
{
char	str[400];
struct	ast_frame wf;
struct	rpt_link *l;

	snprintf(str, sizeof(str) - 1, "T %s %s", myrpt->name,cmd);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	wf.src = "send_tele_link";
	l = myrpt->links.next;
	/* give it to everyone */
	while(l != &myrpt->links)
	{
		AST_FRAME_DATA(wf) = str;
		if (l->chan && (l->mode == 1)) rpt_qwrite(l,&wf);
		l = l->next;
	}
	rpt_telemetry(myrpt,VARCMD,cmd);
	return;
}

/* send newkey request */

static void send_newkey(struct ast_channel *chan)
{

	ast_sendtext(chan,newkey1str);
	return;
}

static void send_old_newkey(struct ast_channel *chan)
{
	ast_sendtext(chan,newkeystr);
	return;
}

/* 
 * Connect a link 
 *
 * Return values:
 * -2: Attempt to connect to self 
 * -1: No such node
 *  0: Success
 *  1: No match yet
 *  2: Already connected to this node
 */

static int connect_link(struct rpt *myrpt, char* node, int mode, int perma)
{
	char *s, *s1, *tele,*cp;
	char lstr[MAXLINKLIST],*strs[MAXLINKLIST];
	char tmp[300], deststr[300] = "",modechange = 0;
	char sx[320],*sy;
	struct rpt_link *l;
	int reconnects = 0;
	int i,n;
	int voterlink=0;
	struct dahdi_confinfo ci;  /* conference info */

	if (strlen(node) < 1) return 1;

	if (tlb_node_get(node,'n',NULL,NULL,NULL,NULL) == 1)
	{
		sprintf(tmp,"tlb/%s/%s",node,myrpt->name);
	}
	else
	{
		if (node[0] != '3')
		{
			if (!node_lookup(myrpt,node,tmp,sizeof(tmp) - 1,1))
			{
				if(strlen(node) >= myrpt->longestnode)
					return -1; /* No such node */
				return 1; /* No match yet */
			}
		}
		else
		{
			char str1[60],str2[50];

			if (strlen(node) < 7) return 1;
			sprintf(str2,"%d",atoi(node + 1));
			if (elink_db_get(str2,'n',NULL,NULL,str1) < 1) return -1;
			if (myrpt->p.eloutbound)
				sprintf(tmp,"echolink/%s/%s,%s",myrpt->p.eloutbound,str1,str1);
			else
				sprintf(tmp,"echolink/el0/%s,%s",str1,str1);
		}
	}

	if(!strcmp(myrpt->name,node)) /* Do not allow connections to self */
		return -2;
		
	if(debug > 3)
	{
		ast_log(LOG_NOTICE,"Connect attempt to node %s\n", node);
		ast_log(LOG_NOTICE,"Mode: %s\n",(mode)?"Transceive":"Monitor");
		ast_log(LOG_NOTICE,"Connection type: %s\n",(perma)?"Permalink":"Normal");
	}

	s = NULL;
	s1 = tmp;
	if (strncasecmp(tmp,"tlb",3)) /* if not tlb */
	{
		s = tmp;
		s1 = strsep(&s,",");
		if (!strchr(s1,':') && strchr(s1,'/') && strncasecmp(s1, "local/", 6) && 
			strncasecmp(s1,"echolink/",9))
		{
			sy = strchr(s1,'/');		
			*sy = 0;
			sprintf(sx,"%s:4569/%s",s1,sy + 1);
			s1 = sx;
		}
		strsep(&s,",");
        }
	if(s && strcmp(s,"VOTE")==0)
	{
		voterlink=1;
		if(debug)ast_log(LOG_NOTICE,"NODE is a VOTER.\n");
	}
	rpt_mutex_lock(&myrpt->lock);
	l = myrpt->links.next;
	/* try to find this one in queue */
	while(l != &myrpt->links){
		if (l->name[0] == '0') 
		{
			l = l->next;
			continue;
		}
	/* if found matching string */
		if (!strcmp(l->name, node))
			break;
		l = l->next;
	}
	/* if found */
	if (l != &myrpt->links){ 
	/* if already in this mode, just ignore */
		if ((l->mode == mode) || (!l->chan)) {
			rpt_mutex_unlock(&myrpt->lock);
			return 2; /* Already linked */
		}
		if ((!strncasecmp(l->chan->name,"echolink",8)) ||
			(!strncasecmp(l->chan->name,"tlb",3)))
		{
			l->mode = mode;
			strncpy(myrpt->lastlinknode,node,MAXNODESTR - 1);
			rpt_mutex_unlock(&myrpt->lock);
			return 0;
		}
		reconnects = l->reconnects;
		rpt_mutex_unlock(&myrpt->lock);
		if (l->chan) ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
		l->retries = l->max_retries + 1;
		l->disced = 2;
		modechange = 1;
	}
	else
	{
		__mklinklist(myrpt,NULL,lstr,0);
		rpt_mutex_unlock(&myrpt->lock);
		n = finddelim(lstr,strs,MAXLINKLIST);
		for(i = 0; i < n; i++)
		{
			if ((*strs[i] < '0') || 
			    (*strs[i] > '9')) strs[i]++;
			if (!strcmp(strs[i],node))
			{
				return 2; /* Already linked */
			}
		}
	}
	strncpy(myrpt->lastlinknode,node,MAXNODESTR - 1);
	/* establish call */
	l = ast_malloc(sizeof(struct rpt_link));
	if (!l)
	{
		ast_log(LOG_WARNING, "Unable to malloc\n");
		return -1;
	}
	/* zero the silly thing */
	memset((char *)l,0,sizeof(struct rpt_link));
	l->mode = mode;
	l->outbound = 1;
	l->thisconnected = 0;
	voxinit_link(l,1);
	strncpy(l->name, node, MAXNODESTR - 1);
	l->isremote = (s && ast_true(s));
	if (modechange) l->connected = 1;
	l->hasconnected = l->perma = perma;
	l->newkeytimer = NEWKEYTIME;
	l->iaxkey = 0;
	l->newkey = 2;
	l->voterlink=voterlink;
	if (strncasecmp(s1,"echolink/",9) == 0) l->newkey = 0;
#ifdef ALLOW_LOCAL_CHANNELS
	if ((strncasecmp(s1,"iax2/", 5) == 0) || (strncasecmp(s1, "local/", 6) == 0) ||
	    (strncasecmp(s1,"echolink/",9) == 0) || (strncasecmp(s1,"tlb/",4) == 0))
#else
	if ((strncasecmp(s1,"iax2/", 5) == 0) || (strncasecmp(s1,"echolink/",9) == 0) ||
		(strncasecmp(s1,"tlb/",4) == 0))
#endif
        	strlcpy(deststr, s1, sizeof(deststr)-1);
	else
	        snprintf(deststr, sizeof(deststr), "IAX2/%s", s1);
	tele = strchr(deststr, '/');
	if (!tele){
		ast_log(LOG_WARNING,"link3:Dial number (%s) must be in format tech/number\n",deststr);
		ast_free(l);
		return -1;
	}
	*tele++ = 0;
	if (!strncasecmp(deststr,"echolink",8))
	{
		char tel1[100];

		strncpy(tel1,tele,sizeof(tel1) - 1);
		cp = strchr(tel1,'/');
		if (cp) cp++; else cp = tel1;
		strcpy(cp,node + 1);
		l->chan = ast_request(deststr, AST_FORMAT_SLINEAR,  tel1,NULL);
	}
	else
	{
		l->chan = ast_request(deststr, AST_FORMAT_SLINEAR,tele,NULL);
		if (!(l->chan))
		{
			usleep(150000);
			l->chan = ast_request(deststr, AST_FORMAT_SLINEAR,tele,NULL);
		}
	}
	if (l->chan){
		ast_set_read_format(l->chan, AST_FORMAT_SLINEAR);
		ast_set_write_format(l->chan, AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
		if (l->chan->cdr)
			ast_set_flag(l->chan->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
#ifndef	NEW_ASTERISK
		l->chan->whentohangup = 0;
#endif
		l->chan->appl = "Apprpt";
		l->chan->data = "(Remote Rx)";
		if (debug > 3)
			ast_log(LOG_NOTICE, "rpt (remote) initiating call to %s/%s on %s\n",
		deststr, tele, l->chan->name);
		if(l->chan->cid.cid_num)
			ast_free(l->chan->cid.cid_num);
		l->chan->cid.cid_num = ast_strdup(myrpt->name);
		ast_call(l->chan,tele,2000);
	}
	else
	{
		if(debug > 3) 
			ast_log(LOG_NOTICE, "Unable to place call to %s/%s\n",
				deststr,tele);
		if (myrpt->p.archivedir)
		{
			char str[512];
			sprintf(str,"LINKFAIL,%s/%s",deststr,tele);
			donodelog(myrpt,str);
		}
		ast_free(l);
		return -1;
	}
	/* allocate a pseudo-channel thru asterisk */
	l->pchan = ast_request(DAHDI_CHANNEL_NAME,AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!l->pchan){
		ast_log(LOG_WARNING,"rpt connect: Sorry unable to obtain pseudo channel\n");
		ast_hangup(l->chan);
		ast_free(l);
		return -1;
	}
	ast_set_read_format(l->pchan, AST_FORMAT_SLINEAR);
	ast_set_write_format(l->pchan, AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (l->pchan->cdr)
		ast_set_flag(l->pchan->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	ast_answer(l->pchan);
	/* make a conference for the tx */
	ci.chan = 0;
	ci.confno = myrpt->conf;
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER;
	/* first put the channel on the conference in proper mode */
	if (ioctl(l->pchan->fds[0], DAHDI_SETCONF, &ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		ast_hangup(l->chan);
		ast_hangup(l->pchan);
		ast_free(l);
		return -1;
	}
	rpt_mutex_lock(&myrpt->lock);
	if (tlb_node_get(node,'n',NULL,NULL,NULL,NULL) == 1) 
		init_linkmode(myrpt,l,LINKMODE_TLB);
	else if (node[0] == '3') init_linkmode(myrpt,l,LINKMODE_ECHOLINK);
	else l->linkmode = 0;
	l->reconnects = reconnects;
	/* insert at end of queue */
	l->max_retries = MAX_RETRIES;
	if (perma)
		l->max_retries = MAX_RETRIES_PERM;
	if (l->isremote) l->retries = l->max_retries + 1;
	l->rxlingertimer = ((l->iaxkey) ? RX_LINGER_TIME_IAXKEY : RX_LINGER_TIME);
	insque((struct qelem *)l,(struct qelem *)myrpt->links.next);
	__kickshort(myrpt);
	rpt_mutex_unlock(&myrpt->lock);
	return 0;
}


/*
* Internet linking function 
*/

static int function_ilink(struct rpt *myrpt, char *param, char *digits, int command_source, struct rpt_link *mylink)
{

	char *s1,*s2,tmp[300];
	char digitbuf[MAXNODESTR],*strs[MAXLINKLIST];
	char mode,perma;
	struct rpt_link *l;
	int i,r;

	if(!param)
		return DC_ERROR;
		
			
	if (myrpt->p.s[myrpt->p.sysstate_cur].txdisable || myrpt->p.s[myrpt->p.sysstate_cur].linkfundisable )
		return DC_ERROR;

	strncpy(digitbuf,digits,MAXNODESTR - 1);

	if(debug > 6)
		printf("@@@@ ilink param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);
		
	switch(myatoi(param)){
		case 11: /* Perm Link off */
		case 1: /* Link off */
			if (strlen(digitbuf) < 1) break;
			if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
				strcpy(digitbuf,myrpt->lastlinknode);
			rpt_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
			/* try to find this one in queue */
			while(l != &myrpt->links){
				if (l->name[0] == '0') 
				{
					l = l->next;
					continue;
				}
				/* if found matching string */
				if (!strcmp(l->name, digitbuf))
					break;
				l = l->next;
			}
			if (l != &myrpt->links){ /* if found */
				struct	ast_frame wf;

				/* must use perm command on perm link */
				if ((myatoi(param) < 10) && 
				    (l->max_retries > MAX_RETRIES))
				{
					rpt_mutex_unlock(&myrpt->lock);
					return DC_COMPLETE;
				}
				strlcpy(myrpt->lastlinknode,digitbuf,MAXNODESTR - 1);
				l->retries = l->max_retries + 1;
				l->disced = 1;
				l->hasconnected = 1;
				rpt_mutex_unlock(&myrpt->lock);
				memset(&wf,0,sizeof(wf));
				wf.frametype = AST_FRAME_TEXT;
				wf.datalen = strlen(discstr) + 1;
				AST_FRAME_DATA(wf) = discstr;
				wf.src = "function_ilink:1";
				if (l->chan)
				{
					if (l->thisconnected) ast_write(l->chan,&wf);
					rpt_safe_sleep(myrpt,l->chan,250);
					ast_softhangup(l->chan,AST_SOFTHANGUP_DEV);
				}
				myrpt->linkactivityflag = 1;
				rpt_telem_select(myrpt,command_source,mylink);
				rpt_telemetry(myrpt, COMPLETE, NULL);
				return DC_COMPLETE;
			}
			rpt_mutex_unlock(&myrpt->lock);	
			break;
		case 2: /* Link Monitor */
		case 3: /* Link transceive */
		case 12: /* Link Monitor permanent */
		case 13: /* Link transceive permanent */
		case 8: /* Link Monitor Local Only */
		case 18: /* Link Monitor Local Only permanent */
			if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
				strcpy(digitbuf,myrpt->lastlinknode);
			r = atoi(param);
			/* Attempt connection  */
			perma = (r > 10) ? 1 : 0;
			mode = (r & 1) ? 1 : 0;
			if ((r == 8) || (r == 18)) mode = 2;
			r = connect_link(myrpt, digitbuf, mode, perma);
			switch(r){
				case -2: /* Attempt to connect to self */
					return DC_COMPLETE; /* Silent error */

				case 0:
					myrpt->linkactivityflag = 1;
					rpt_telem_select(myrpt,command_source,mylink);
					rpt_telemetry(myrpt, COMPLETE, NULL);
					return DC_COMPLETE;

				case 1:
					break;
				
				case 2:
					rpt_telem_select(myrpt,command_source,mylink);
					rpt_telemetry(myrpt, REMALREADY, NULL);
					return DC_COMPLETE;
				
				default:
					rpt_telem_select(myrpt,command_source,mylink);
					rpt_telemetry(myrpt, CONNFAIL, NULL);
					return DC_COMPLETE;
			}
			break;

		case 4: /* Enter Command Mode */
		
			if (strlen(digitbuf) < 1) break;
			/* if doesnt allow link cmd, or no links active, return */
			if (myrpt->links.next == &myrpt->links) return DC_COMPLETE;
 			if ((command_source != SOURCE_RPT) && 
				(command_source != SOURCE_PHONE) &&
				(command_source != SOURCE_ALT) &&
				(command_source != SOURCE_DPHONE) && mylink &&
				(!iswebtransceiver(mylink)) &&
				strncasecmp(mylink->chan->name,"echolink",8) &&
				strncasecmp(mylink->chan->name,"tlb",3))
					return DC_COMPLETE;
			
			/* if already in cmd mode, or selected self, fughetabahtit */
			if ((myrpt->cmdnode[0]) || (!strcmp(myrpt->name, digitbuf))){
			
				rpt_telem_select(myrpt,command_source,mylink);
				rpt_telemetry(myrpt, REMALREADY, NULL);
				return DC_COMPLETE;
			}
			if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
				strcpy(digitbuf,myrpt->lastlinknode);
			/* node must at least exist in list */
			if (tlb_node_get(digitbuf,'n',NULL,NULL,NULL,NULL) != 1)
			{
				if (digitbuf[0] != '3')
				{
					if (!node_lookup(myrpt,digitbuf,NULL,0,1))
					{
						if(strlen(digitbuf) >= myrpt->longestnode)
							return DC_ERROR;
						break;
				
					}
				}
				else
				{
					if (strlen(digitbuf) < 7) break;
				}
			}
			rpt_mutex_lock(&myrpt->lock);
			strcpy(myrpt->lastlinknode,digitbuf);
			strlcpy(myrpt->cmdnode, digitbuf, sizeof(myrpt->cmdnode) - 1);
			rpt_mutex_unlock(&myrpt->lock);
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, REMGO, NULL);	
			return DC_COMPLETE;
			
		case 5: /* Status */
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, STATUS, NULL);
			return DC_COMPLETE;

		case 15: /* Full Status */
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, FULLSTATUS, NULL);
			return DC_COMPLETE;
			
			
		case 6: /* All Links Off, including permalinks */
                       rpt_mutex_lock(&myrpt->lock);
			myrpt->savednodes[0] = 0;
                        l = myrpt->links.next;
                        /* loop through all links */
                        while(l != &myrpt->links){
				struct	ast_frame wf;
				char c1;
                                if ((l->name[0] <= '0') || (l->name[0] > '9'))  /* Skip any IAXRPT monitoring */
                                {
                                        l = l->next;
                                        continue;
                                }
				if (l->mode == 1) c1 = 'X';
				else if (l->mode > 1) c1 = 'L';
				else c1 = 'M';
				/* Make a string of disconnected nodes for possible restoration */
				sprintf(tmp,"%c%c%.290s",c1,(l->perma) ? 'P':'T',l->name);
				if(strlen(tmp) + strlen(myrpt->savednodes) + 1 < MAXNODESTR){ 
					if(myrpt->savednodes[0])
						strcat(myrpt->savednodes, ",");
					strcat(myrpt->savednodes, tmp);
				}
                           	l->retries = l->max_retries + 1;
                                l->disced = 2; /* Silently disconnect */
                                rpt_mutex_unlock(&myrpt->lock);
				/* ast_log(LOG_NOTICE,"dumping link %s\n",l->name); */
                                
				memset(&wf,0,sizeof(wf));
                                wf.frametype = AST_FRAME_TEXT;
                                wf.datalen = strlen(discstr) + 1;
                                AST_FRAME_DATA(wf) = discstr;
				wf.src = "function_ilink:6";
                                if (l->chan)
                                {
                                        if (l->thisconnected) ast_write(l->chan,&wf);
                                        rpt_safe_sleep(myrpt,l->chan,250); /* It's dead already, why check the return value? */
                                        ast_softhangup(l->chan,AST_SOFTHANGUP_DEV);
                                }
				rpt_mutex_lock(&myrpt->lock);
                                l = l->next;
                        }
			rpt_mutex_unlock(&myrpt->lock);
			if(debug > 3)
				ast_log(LOG_NOTICE,"Nodes disconnected: %s\n",myrpt->savednodes);
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;


		case 10: /* All RANGER Links Off */
                       rpt_mutex_lock(&myrpt->lock);
			myrpt->savednodes[0] = 0;
                        l = myrpt->links.next;
                        /* loop through all links */
                        while(l != &myrpt->links){
				struct	ast_frame wf;
				char c1;
                                if ((l->name[0] <= '0') || (l->name[0] > '9'))  /* Skip any IAXRPT monitoring */
                                {
                                        l = l->next;
                                        continue;
                                }
				/* if RANGER and not permalink */
				if ((l->max_retries <= MAX_RETRIES) && ISRANGER(l->name))
				{
					if (l->mode == 1) c1 = 'X';
					else if (l->mode > 1) c1 = 'L';
					else c1 = 'M';
					/* Make a string of disconnected nodes for possible restoration */
					sprintf(tmp,"%c%c%.290s",c1,(l->perma) ? 'P':'T',l->name);
					if(strlen(tmp) + strlen(myrpt->savednodes) + 1 < MAXNODESTR){ 
						if(myrpt->savednodes[0])
							strcat(myrpt->savednodes, ",");
						strcat(myrpt->savednodes, tmp);
					}
	                           	l->retries = l->max_retries + 1;
	                                l->disced = 2; /* Silently disconnect */
	                                rpt_mutex_unlock(&myrpt->lock);
					/* ast_log(LOG_NOTICE,"dumping link %s\n",l->name); */
                                
					memset(&wf,0,sizeof(wf));
	                                wf.frametype = AST_FRAME_TEXT;
	                                wf.datalen = strlen(discstr) + 1;
	                                AST_FRAME_DATA(wf) = discstr;
					wf.src = "function_ilink:6";
	                                if (l->chan)
	                                {
	                                        if (l->thisconnected) ast_write(l->chan,&wf);
	                                        rpt_safe_sleep(myrpt,l->chan,250); /* It's dead already, why check the return value? */
	                                        ast_softhangup(l->chan,AST_SOFTHANGUP_DEV);
	                                }
					rpt_mutex_lock(&myrpt->lock);
				}
                                l = l->next;
                        }
			rpt_mutex_unlock(&myrpt->lock);
			if(debug > 3)
				ast_log(LOG_NOTICE,"Nodes disconnected: %s\n",myrpt->savednodes);
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;

		case 7: /* Identify last node which keyed us up */
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, LASTNODEKEY, NULL);
			break;

#ifdef	_MDC_DECODE_H_
		case 17:
			myrpt->lastunit = 0xd00d;
			mdc1200_cmd(myrpt,"ID00D");
			mdc1200_notify(myrpt,NULL,"ID00D");
			mdc1200_send(myrpt,"ID00D");
			break;
#endif
		case 9: /* Send Text Message */
			if (!param) break;
			s1 = strchr(param,',');
			if (!s1) break;
			*s1 = 0;
			s2 = strchr(s1 + 1,',');
			if (!s2) break;
			*s2 = 0;
			snprintf(tmp,MAX_TEXTMSG_SIZE - 1,"M %s %s %s",
				myrpt->name,s1 + 1,s2 + 1);
			rpt_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
			/* otherwise, send it to all of em */
			while(l != &myrpt->links)
			{
				if (l->name[0] == '0') 
				{
					l = l->next;
					continue;
				}
				if (l->chan) ast_sendtext(l->chan,tmp);
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
                        rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;

		case 16: /* Restore links disconnected with "disconnect all links" command */
			strcpy(tmp, myrpt->savednodes); /* Make a copy */
			finddelim(tmp, strs, MAXLINKLIST); /* convert into substrings */
			for(i = 0; tmp[0] && strs[i] != NULL && i < MAXLINKLIST; i++){
				s1 = strs[i];
				if (s1[0] == 'X') mode = 1;
				else if (s1[0] == 'L') mode = 2;
				else mode = 0;
				perma = (s1[1] == 'P') ? 1 : 0;
				connect_link(myrpt, s1 + 2, mode, perma); /* Try to reconnect */
			}
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, COMPLETE, NULL);
			break;
	
		case 200:
		case 201:
		case 202:
		case 203:
		case 204:
		case 205:
		case 206:
		case 207:
		case 208:
		case 209:
		case 210:
		case 211:
		case 212:
		case 213:
		case 214:
		case 215:
			if (((myrpt->p.propagate_dtmf) && 
			     (command_source == SOURCE_LNK)) ||
			    ((myrpt->p.propagate_phonedtmf) &&
				((command_source == SOURCE_PHONE) ||
				  (command_source == SOURCE_ALT) ||
				    (command_source == SOURCE_DPHONE))))
					do_dtmf_local(myrpt,
						remdtmfstr[myatoi(param) - 200]);
		default:
			return DC_ERROR;
			
	}
	
	return DC_INDETERMINATE;
}	

/*
* Autopatch up
*/

static int function_autopatchup(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
	pthread_attr_t attr;
	int i, index, paramlength,nostar = 0;
	char *lparam;
	char *value = NULL;
	char *paramlist[20];

	static char *keywords[] = {
	"context",
	"dialtime",
	"farenddisconnect",
	"noct",
	"quiet",
	"voxalways",
	"exten",
	"nostar",
	NULL
	};
		
	if (myrpt->p.s[myrpt->p.sysstate_cur].txdisable || myrpt->p.s[myrpt->p.sysstate_cur].autopatchdisable)
		return DC_ERROR;
		
	if(debug)
		printf("@@@@ Autopatch up\n");

	if(!myrpt->callmode){
		/* Set defaults */
		myrpt->patchnoct = 0;
		myrpt->patchdialtime = 0;
		myrpt->patchfarenddisconnect = 0;
		myrpt->patchquiet = 0;
		myrpt->patchvoxalways = 0;
		strlcpy(myrpt->patchcontext, myrpt->p.ourcontext, MAXPATCHCONTEXT-1);
		memset(myrpt->patchexten, 0, sizeof(myrpt->patchexten));

	}
	if(param){
		/* Process parameter list */
		lparam = ast_strdup(param);
		if(!lparam){
			ast_log(LOG_ERROR,"App_rpt out of memory on line %d\n",__LINE__);
			return DC_ERROR;	
		}
		paramlength = finddelim(lparam, paramlist, 20); 			
		for(i = 0; i < paramlength; i++){
			index = matchkeyword(paramlist[i], &value, keywords);
			if(value)
				value = skipchars(value, "= ");
			if(!myrpt->callmode){
				switch(index){
					case 1: /* context */
						strncpy(myrpt->patchcontext, value, MAXPATCHCONTEXT - 1) ;
						break;
						
					case 2: /* dialtime */
						myrpt->patchdialtime = atoi(value);
						break;

					case 3: /* farenddisconnect */
						myrpt->patchfarenddisconnect = atoi(value);
						break;

					case 4:	/* noct */
						myrpt->patchnoct = atoi(value);
						break;

					case 5: /* quiet */
						myrpt->patchquiet = atoi(value);
						break;

					case 6: /* voxalways */
						myrpt->patchvoxalways = atoi(value);
						break;

					case 7: /* exten */
						strncpy(myrpt->patchexten, value, AST_MAX_EXTENSION - 1) ;
						break;

					default:
						break;
				}
			}
			else {
				switch(index){
					case 8: /* nostar */
						nostar = 1;
						break;
				}
			}
		}
		ast_free(lparam);
	}
					
	rpt_mutex_lock(&myrpt->lock);

	/* if on call, force * into current audio stream */
	
	if ((myrpt->callmode == 2) || (myrpt->callmode == 3)){
		if (!nostar) myrpt->mydtmf = myrpt->p.funcchar;
	}
	if (myrpt->callmode){
		rpt_mutex_unlock(&myrpt->lock);
		return DC_COMPLETE;
	}
	myrpt->callmode = 1;
	myrpt->cidx = 0;
	myrpt->exten[myrpt->cidx] = 0;
	rpt_mutex_unlock(&myrpt->lock);
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ast_pthread_create(&myrpt->rpt_call_thread,&attr,rpt_call,(void *) myrpt);
	return DC_COMPLETE;
}

/*
* Autopatch down
*/

static int function_autopatchdn(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
	if (myrpt->p.s[myrpt->p.sysstate_cur].txdisable || myrpt->p.s[myrpt->p.sysstate_cur].autopatchdisable)
		return DC_ERROR;
	
	if(debug)
		printf("@@@@ Autopatch down\n");
		
	rpt_mutex_lock(&myrpt->lock);
	
	myrpt->macropatch=0;

	if (!myrpt->callmode){
		rpt_mutex_unlock(&myrpt->lock);
		return DC_COMPLETE;
	}
	
	myrpt->callmode = 0;
	channel_revert(myrpt);
	rpt_mutex_unlock(&myrpt->lock);
	rpt_telem_select(myrpt,command_source,mylink);
	if (!myrpt->patchquiet) rpt_telemetry(myrpt, TERM, NULL);
	return DC_COMPLETE;
}

/*
* Status
*/

static int function_status(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
	struct rpt_tele *telem;

	if (!param)
		return DC_ERROR;

	if ((myrpt->p.s[myrpt->p.sysstate_cur].txdisable) || (myrpt->p.s[myrpt->p.sysstate_cur].userfundisable))
		return DC_ERROR;

	if(debug)
 		printf("@@@@ status param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);
	
	switch(myatoi(param)){
		case 1: /* System ID */
		        if(myrpt->p.idtime)  /* ID time must be non-zero */
			{
		                myrpt->mustid = myrpt->tailid = 0;
		                myrpt->idtimer = myrpt->p.idtime;
			}
 			telem = myrpt->tele.next;
			while(telem != &myrpt->tele)
			{
				if (((telem->mode == ID) || (telem->mode == ID1)) && (!telem->killed))
				{
					if (telem->chan) ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV); /* Whoosh! */
					telem->killed = 1;
				}
				telem = telem->next;
			}
			rpt_telemetry(myrpt, ID1, NULL);
			return DC_COMPLETE;
		case 2: /* System Time */
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, STATS_TIME, NULL);
			return DC_COMPLETE;
		case 3: /* app_rpt.c version */
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, STATS_VERSION, NULL);
			return DC_COMPLETE;
		case 4: /* GPS data */
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, STATS_GPS, NULL);
			return DC_COMPLETE;
		case 5: /* Identify last node which keyed us up */
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, LASTUSER, NULL);
			return DC_COMPLETE;
		case 11: /* System ID (local only)*/
		        if(myrpt->p.idtime)  /* ID time must be non-zero */
			{
		                myrpt->mustid = myrpt->tailid = 0;
		                myrpt->idtimer = myrpt->p.idtime;
			}
 			telem = myrpt->tele.next;
			while(telem != &myrpt->tele)
			{
				if (((telem->mode == ID) || (telem->mode == ID1)) && (!telem->killed))
				{
					if (telem->chan) ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV); /* Whoosh! */
					telem->killed = 1;
				}
				telem = telem->next;
			}
			rpt_telemetry(myrpt, ID , NULL);
			return DC_COMPLETE;
	        case 12: /* System Time (local only)*/
			rpt_telemetry(myrpt, STATS_TIME_LOCAL, NULL);
			return DC_COMPLETE;
		case 99: /* GPS data announced locally */
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, STATS_GPS_LEGACY, NULL);
			return DC_COMPLETE;
		default:
			return DC_ERROR;
	}
	return DC_INDETERMINATE;
}
/*
*  Macro-oni (without Salami)
*/
static int function_macro(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
char	*val;
int	i;
	if (myrpt->remote)
		return DC_ERROR;

	if(debug) 
		printf("@@@@ macro-oni param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);
	
	if(strlen(digitbuf) < 1) /* needs 1 digit */
		return DC_INDETERMINATE;
			
	for(i = 0 ; i < digitbuf[i] ; i++) {
		if((digitbuf[i] < '0') || (digitbuf[i] > '9'))
			return DC_ERROR;
	}
   
	if (*digitbuf == '0') val = myrpt->p.startupmacro;
	else val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.macro, digitbuf);
	/* param was 1 for local buf */
	if (!val){
                if (strlen(digitbuf) < myrpt->macro_longest)
                        return DC_INDETERMINATE;
		rpt_telem_select(myrpt,command_source,mylink);
		rpt_telemetry(myrpt, MACRO_NOTFOUND, NULL);
		return DC_COMPLETE;
	}			
	rpt_mutex_lock(&myrpt->lock);
	if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(val))
	{
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telem_select(myrpt,command_source,mylink);
		rpt_telemetry(myrpt, MACRO_BUSY, NULL);
		return DC_ERROR;
	}
	myrpt->macrotimer = MACROTIME;
	strncat(myrpt->macrobuf,val,MAXMACRO - 1);
	rpt_mutex_unlock(&myrpt->lock);
	return DC_COMPLETE;	
}

/*
*  Playback a recording globally
*/

static int function_playback(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{

	if (myrpt->remote)
		return DC_ERROR;

	if(debug) 
		printf("@@@@ playback param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);
	
	if (ast_fileexists(param,NULL,myrpt->rxchannel->language) <= 0)
		return DC_ERROR;

	rpt_telem_select(myrpt,command_source,mylink);
	rpt_telemetry(myrpt,PLAYBACK,param);
	return DC_COMPLETE;
}



/*
 * *  Playback a recording locally
 * */

static int function_localplay(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{

        if (myrpt->remote)
                return DC_ERROR;

        if(debug)
                printf("@@@@ localplay param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);

        if (ast_fileexists(param,NULL,myrpt->rxchannel->language) <= 0)
                return DC_ERROR;

        rpt_telemetry(myrpt,LOCALPLAY,param);
        return DC_COMPLETE;
}



/*
* COP - Control operator
*/

static int function_cop(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
	char string[50],fname[50];
	char paramcopy[500];
	int  argc;
	FILE *fp;
	char *argv[101],*cp;
	int i, j, k, r, src;
	struct rpt_tele *telem;
#ifdef	_MDC_ENCODE_H_
	struct mdcparams *mdcp;
#endif

	if(!param)
		return DC_ERROR;

	strncpy(paramcopy, param, sizeof(paramcopy) - 1);
	paramcopy[sizeof(paramcopy) - 1] = 0;
	argc = explode_string(paramcopy, argv, 100, ',', 0);

	if(!argc)
		return DC_ERROR;
	
	switch(myatoi(argv[0])){
		case 1: /* System reset */
			i = system("killall -9 asterisk");
			return DC_COMPLETE;

		case 2:
			myrpt->p.s[myrpt->p.sysstate_cur].txdisable = 0;
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, ARB_ALPHA, (void *) "RPTENA");
			return DC_COMPLETE;
			
		case 3:
			myrpt->p.s[myrpt->p.sysstate_cur].txdisable = 1;
			return DC_COMPLETE;
			
		case 4: /* test tone on */
			if (myrpt->stopgen < 0) 
			{
				myrpt->stopgen = 1;
			}
			else 
			{
				myrpt->stopgen = 0;
				rpt_telemetry(myrpt, TEST_TONE, NULL);
			}
			if (!myrpt->remote) return DC_COMPLETE;
			if (myrpt->remstopgen < 0)
			{
				myrpt->remstopgen = 1;
			}
			else
			{
				if (myrpt->remstopgen) break;
				myrpt->remstopgen = -1;
			        if ( ast_tonepair_start(myrpt->txchannel, 1000.0, 0, 99999999, 7200.0))
				{
					myrpt->remstopgen = 0;
					break;
				}
			}
			return DC_COMPLETE;

		case 5: /* Disgorge variables to log for debug purposes */
			myrpt->disgorgetime = time(NULL) + 10; /* Do it 10 seconds later */
			return DC_COMPLETE;

		case 6: /* Simulate COR being activated (phone only) */
			if (command_source != SOURCE_PHONE) return DC_INDETERMINATE;
			return DC_DOKEY;	


		case 7: /* Time out timer enable */
			myrpt->p.s[myrpt->p.sysstate_cur].totdisable = 0;
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, ARB_ALPHA, (void *) "TOTENA");
			return DC_COMPLETE;
			
		case 8: /* Time out timer disable */
			myrpt->p.s[myrpt->p.sysstate_cur].totdisable = 1;
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, ARB_ALPHA, (void *) "TOTDIS");
			return DC_COMPLETE;

                case 9: /* Autopatch enable */
                        myrpt->p.s[myrpt->p.sysstate_cur].autopatchdisable = 0;
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, ARB_ALPHA, (void *) "APENA");
                        return DC_COMPLETE;

                case 10: /* Autopatch disable */
                        myrpt->p.s[myrpt->p.sysstate_cur].autopatchdisable = 1;
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, ARB_ALPHA, (void *) "APDIS");
                        return DC_COMPLETE;

                case 11: /* Link Enable */
                        myrpt->p.s[myrpt->p.sysstate_cur].linkfundisable = 0;
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, ARB_ALPHA, (void *) "LNKENA");
                        return DC_COMPLETE;

                case 12: /* Link Disable */
                        myrpt->p.s[myrpt->p.sysstate_cur].linkfundisable = 1;
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, ARB_ALPHA, (void *) "LNKDIS");
                        return DC_COMPLETE;

		case 13: /* Query System State */
			string[0] = string[1] = 'S';
			string[2] = myrpt->p.sysstate_cur + '0';
			string[3] = '\0';
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, ARB_ALPHA, (void *) string);
			return DC_COMPLETE;

		case 14: /* Change System State */
			if(strlen(digitbuf) == 0)
				break;
			if((digitbuf[0] < '0') || (digitbuf[0] > '9'))
				return DC_ERROR;
			myrpt->p.sysstate_cur = digitbuf[0] - '0';
                        string[0] = string[1] = 'S';
                        string[2] = myrpt->p.sysstate_cur + '0';
                        string[3] = '\0';
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, ARB_ALPHA, (void *) string);
                        return DC_COMPLETE;

                case 15: /* Scheduler Enable */
                        myrpt->p.s[myrpt->p.sysstate_cur].schedulerdisable = 0;
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, ARB_ALPHA, (void *) "SKENA");
                        return DC_COMPLETE;

                case 16: /* Scheduler Disable */
                        myrpt->p.s[myrpt->p.sysstate_cur].schedulerdisable = 1;
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, ARB_ALPHA, (void *) "SKDIS");
                        return DC_COMPLETE;

                case 17: /* User functions Enable */
                        myrpt->p.s[myrpt->p.sysstate_cur].userfundisable = 0;
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, ARB_ALPHA, (void *) "UFENA");
                        return DC_COMPLETE;

                case 18: /* User Functions Disable */
                        myrpt->p.s[myrpt->p.sysstate_cur].userfundisable = 1;
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, ARB_ALPHA, (void *) "UFDIS");
                        return DC_COMPLETE;

                case 19: /* Alternate Tail Enable */
                        myrpt->p.s[myrpt->p.sysstate_cur].alternatetail = 1;
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, ARB_ALPHA, (void *) "ATENA");
                        return DC_COMPLETE;

                case 20: /* Alternate Tail Disable */
                        myrpt->p.s[myrpt->p.sysstate_cur].alternatetail = 0;
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, ARB_ALPHA, (void *) "ATDIS");
                        return DC_COMPLETE;

                case 21: /* Parrot Mode Enable */
			birdbath(myrpt);
			if (myrpt->p.parrotmode < 2)
			{
				myrpt->parrotonce = 0;
				myrpt->p.parrotmode = 1;
				rpt_telem_select(myrpt,command_source,mylink);
				rpt_telemetry(myrpt,COMPLETE,NULL);
				return DC_COMPLETE;
			}
			break;
                case 22: /* Parrot Mode Disable */
			birdbath(myrpt);
			if (myrpt->p.parrotmode < 2)
			{
				myrpt->p.parrotmode = 0;
				rpt_telem_select(myrpt,command_source,mylink);
				rpt_telemetry(myrpt,COMPLETE,NULL);
				return DC_COMPLETE;
			}
			break;
		case 23: /* flush parrot in progress */
			birdbath(myrpt);
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt,COMPLETE,NULL);
			return DC_COMPLETE;
		case 24: /* flush all telemetry */
			flush_telem(myrpt);
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt,COMPLETE,NULL);
			return DC_COMPLETE;
		case 25: /* request keying info (brief) */
			send_link_keyquery(myrpt);
			myrpt->topkeylong = 0;
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt,COMPLETE,NULL);
			return DC_COMPLETE;
		case 26: /* request keying info (full) */
			send_link_keyquery(myrpt);
			myrpt->topkeylong = 1;
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt,COMPLETE,NULL);
			return DC_COMPLETE;

		case 27: /* Reset DAQ minimum */
			if(argc != 3)
				return DC_ERROR;
			if(!(daq_reset_minmax(argv[1], atoi(argv[2]), 0))){
				rpt_telem_select(myrpt,command_source,mylink);
				rpt_telemetry(myrpt,COMPLETE,NULL);
				return DC_COMPLETE;
			}
			return DC_ERROR;
				
		case 28: /* Reset DAQ maximum */
			if(argc != 3)
				return DC_ERROR;
			if(!(daq_reset_minmax(argv[1], atoi(argv[2]), 1))){
				rpt_telem_select(myrpt,command_source,mylink);
				rpt_telemetry(myrpt,COMPLETE,NULL);
				return DC_COMPLETE;
			}
			return DC_ERROR;


		case 30: /* recall memory location on programmable radio */

		  	if(strlen(digitbuf) < 2) /* needs 2 digits */
				break;
			
			for(i = 0 ; i < 2 ; i++){
				if((digitbuf[i] < '0') || (digitbuf[i] > '9'))
					return DC_ERROR;
			}
	    
			r = retrieve_memory(myrpt, digitbuf);
			if (r < 0){
				rpt_telemetry(myrpt,MEMNOTFOUND,NULL);
				return DC_COMPLETE;
			}
			if (r > 0){
				return DC_ERROR;
			}
			if (setrem(myrpt) == -1) return DC_ERROR;
			return DC_COMPLETE;	

		case 31: 
		    /* set channel. note that it's going to change channel 
		       then confirm on the new channel! */
		  	if(strlen(digitbuf) < 2) /* needs 2 digits */
				break;
			
			for(i = 0 ; i < 2 ; i++){
				if((digitbuf[i] < '0') || (digitbuf[i] > '9'))
					return DC_ERROR;
			}
			channel_steer(myrpt,digitbuf);
			return DC_COMPLETE;	

		case 32: /* Touch Tone Pad Test */
			i = strlen(digitbuf);
			if(!i){
				if(debug > 3)
				ast_log(LOG_NOTICE,"Padtest entered");
				myrpt->inpadtest = 1;
				break;
			}
			else{
				if(debug > 3)
					ast_log(LOG_NOTICE,"Padtest len= %d digits=%s",i,digitbuf);
				if(digitbuf[i-1] != myrpt->p.endchar)
					break;
				rpt_telemetry(myrpt, ARB_ALPHA, digitbuf);
				myrpt->inpadtest = 0;
				if(debug > 3)
					ast_log(LOG_NOTICE,"Padtest exited");
				return DC_COMPLETE;
			}
                case 33: /* Local Telem mode Enable */
			if (myrpt->p.telemdynamic)
			{
				myrpt->telemmode = 0x7fffffff;
				rpt_telemetry(myrpt,COMPLETE,NULL);
				return DC_COMPLETE;
			}
			break;
                case 34: /* Local Telem mode Disable */
			if (myrpt->p.telemdynamic)
			{
				myrpt->telemmode = 0;
				rpt_telemetry(myrpt,COMPLETE,NULL);
				return DC_COMPLETE;
			}
			break;
                case 35: /* Local Telem mode Normal */
			if (myrpt->p.telemdynamic)
			{
				myrpt->telemmode = 1;
				rpt_telem_select(myrpt,command_source,mylink);
				rpt_telemetry(myrpt,COMPLETE,NULL);
				return DC_COMPLETE;
			}
			break;
                case 36: /* Link Output Enable */
			if (!mylink) return DC_ERROR;
			src = 0;
			if ((mylink->name[0] <= '0') || (mylink->name[0] > '9')) src = LINKMODE_GUI;
			if (mylink->phonemode) src = LINKMODE_PHONE;
			else if (!strncasecmp(mylink->chan->name,"echolink",8)) src = LINKMODE_ECHOLINK;
			else if (!strncasecmp(mylink->chan->name,"tlb",3)) src = LINKMODE_TLB;
			if (src && myrpt->p.linkmodedynamic[src])
			{
				set_linkmode(mylink,LINKMODE_ON);
				rpt_telem_select(myrpt,command_source,mylink);
				rpt_telemetry(myrpt,COMPLETE,NULL);
				return DC_COMPLETE;
			}
			break;
                case 37: /* Link Output Disable */
			if (!mylink) return DC_ERROR;
			src = 0;
			if ((mylink->name[0] <= '0') || (mylink->name[0] > '9')) src = LINKMODE_GUI;
			if (mylink->phonemode) src = LINKMODE_PHONE;
			else if (!strncasecmp(mylink->chan->name,"echolink",8)) src = LINKMODE_ECHOLINK;
			else if (!strncasecmp(mylink->chan->name,"tlb",3)) src = LINKMODE_TLB;
			if (src && myrpt->p.linkmodedynamic[src])
			{
				set_linkmode(mylink,LINKMODE_OFF);
				rpt_telem_select(myrpt,command_source,mylink);
				rpt_telemetry(myrpt,COMPLETE,NULL);
				return DC_COMPLETE;
			}
			break;
                case 38: /* Gui Link Output Follow */
			if (!mylink) return DC_ERROR;
			src = 0;
			if ((mylink->name[0] <= '0') || (mylink->name[0] > '9')) src = LINKMODE_GUI;
			if (mylink->phonemode) src = LINKMODE_PHONE;
			else if (!strncasecmp(mylink->chan->name,"echolink",8)) src = LINKMODE_ECHOLINK;
			else if (!strncasecmp(mylink->chan->name,"tlb",3)) src = LINKMODE_TLB;
			if (src && myrpt->p.linkmodedynamic[src])
			{
				set_linkmode(mylink,LINKMODE_FOLLOW);
				rpt_telem_select(myrpt,command_source,mylink);
				rpt_telemetry(myrpt,COMPLETE,NULL);
				return DC_COMPLETE;
			}
			break;
                case 39: /* Link Output Demand*/
			if (!mylink) return DC_ERROR;
			src = 0;
			if ((mylink->name[0] <= '0') || (mylink->name[0] > '9')) src = LINKMODE_GUI;
			if (mylink->phonemode) src = LINKMODE_PHONE;
			else if (!strncasecmp(mylink->chan->name,"echolink",8)) src = LINKMODE_ECHOLINK;
			else if (!strncasecmp(mylink->chan->name,"tlb",3)) src = LINKMODE_TLB;
			if (src && myrpt->p.linkmodedynamic[src])
			{
				set_linkmode(mylink,LINKMODE_DEMAND);
				rpt_telem_select(myrpt,command_source,mylink);
				rpt_telemetry(myrpt,COMPLETE,NULL);
				return DC_COMPLETE;
			}
			break;
                case 42: /* Echolink announce node # only */
			myrpt->p.eannmode = 1;
			rpt_telemetry(myrpt,COMPLETE,NULL);
			return DC_COMPLETE;
                case 43: /* Echolink announce node Callsign only */
			myrpt->p.eannmode = 2;
			rpt_telemetry(myrpt,COMPLETE,NULL);
			return DC_COMPLETE;
                case 44: /* Echolink announce node # & Callsign */
			myrpt->p.eannmode = 3;
			rpt_telemetry(myrpt,COMPLETE,NULL);
			return DC_COMPLETE;
		case 45: /* Link activity timer enable */
			if(myrpt->p.lnkacttime && myrpt->p.lnkactmacro){
				myrpt->linkactivitytimer = 0;
				myrpt->linkactivityflag = 0;
				myrpt->p.lnkactenable = 1;
				rpt_telem_select(myrpt,command_source,mylink);
				rpt_telemetry(myrpt, ARB_ALPHA, (void *) "LATENA");
			}
			return DC_COMPLETE;

		case 46: /* Link activity timer disable */
			if(myrpt->p.lnkacttime && myrpt->p.lnkactmacro){
				myrpt->linkactivitytimer = 0;
				myrpt->linkactivityflag = 0;
				myrpt->p.lnkactenable = 0;
				rpt_telem_select(myrpt,command_source,mylink);
				rpt_telemetry(myrpt, ARB_ALPHA, (void *) "LATDIS");
			}
			return DC_COMPLETE;

	
		case 47: /* Link activity flag kill */
			myrpt->linkactivitytimer = 0;
			myrpt->linkactivityflag = 0;
			return DC_COMPLETE; /* Silent for a reason (only used in macros) */

		case 48: /* play page sequence */
			j = 0;
			for(i = 1; i < argc; i++)
			{
				k = strlen(argv[i]);
				if (k != 1)
				{
					j += k + 1;
					continue;
				}
				if (*argv[i] >= '0' && *argv[i] <='9')
					argv[i] = dtmf_tones[*argv[i]-'0'];
				else if (*argv[i] >= 'A' && (*argv[i]) <= 'D')
					argv[i] = dtmf_tones[*argv[i]-'A'+10];
				else if (*argv[i] == '*')
					argv[i] = dtmf_tones[14];
				else if (*argv[i] == '#')
					argv[i] = dtmf_tones[15];
				j += strlen(argv[i]);
			}
			cp = malloc(j + 100);
			if (!cp)
			{
				ast_log(LOG_NOTICE,"cannot malloc");
				return DC_ERROR;
			}
			memset(cp,0,j + 100);
			for(i = 1; i < argc; i++)
			{
				if (i != 1) strcat(cp,",");
				strcat(cp,argv[i]);
			}
			rpt_telemetry(myrpt,PAGE,cp);
			return DC_COMPLETE;

               case 49: /* Disable Incoming connections */
                        myrpt->p.s[myrpt->p.sysstate_cur].noincomingconns = 1;
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, ARB_ALPHA, (void *) "NOICE");
                        return DC_COMPLETE;

               case 50: /*Enable Incoming connections */
                        myrpt->p.s[myrpt->p.sysstate_cur].noincomingconns = 0;
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, ARB_ALPHA, (void *) "NOICD");
                        return DC_COMPLETE;

		case 51: /* Enable Sleep Mode */
			myrpt->sleeptimer=myrpt->p.sleeptime;
			myrpt->p.s[myrpt->p.sysstate_cur].sleepena = 1;
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, ARB_ALPHA, (void *) "SLPEN");
                        return DC_COMPLETE;

		case 52: /* Disable Sleep Mode */
			myrpt->p.s[myrpt->p.sysstate_cur].sleepena = 0;
			myrpt->sleep = myrpt->sleepreq = 0;
			myrpt->sleeptimer=myrpt->p.sleeptime;
			rpt_telem_select(myrpt,command_source,mylink);
                        rpt_telemetry(myrpt, ARB_ALPHA, (void *) "SLPDS");
                        return DC_COMPLETE;

		case 53: /* Wake up from Sleep Mode */
			myrpt->sleep = myrpt->sleepreq = 0;
			myrpt->sleeptimer=myrpt->p.sleeptime;
			if(myrpt->p.s[myrpt->p.sysstate_cur].sleepena){
				rpt_telem_select(myrpt,command_source,mylink);
                        	rpt_telemetry(myrpt, ARB_ALPHA, (void *) "AWAKE");
			}
			return DC_COMPLETE;
		case 54: /* Go to sleep */
			if(myrpt->p.s[myrpt->p.sysstate_cur].sleepena){
				rpt_telem_select(myrpt,command_source,mylink);
                        	rpt_telemetry(myrpt, ARB_ALPHA, (void *) "SLEEP");
				myrpt->sleepreq = 1;
				myrpt->sleeptimer = 0;
			}
			return DC_COMPLETE;
		case 55: /* Parrot Once if parrot mode is disabled */
			if(!myrpt->p.parrotmode)
				myrpt->parrotonce = 1;
			return DC_COMPLETE;		
		case 56: /* RX CTCSS Enable */
			if ((strncasecmp(myrpt->rxchannel->name,"zap/", 4) == 0) || 
			    (strncasecmp(myrpt->rxchannel->name,"dahdi/", 6) == 0))
			{
				struct dahdi_radio_param r;

				memset(&r,0,sizeof(struct dahdi_radio_param));
				r.radpar = DAHDI_RADPAR_IGNORECT;
				r.data = 0;
				ioctl(myrpt->zaprxchannel->fds[0],DAHDI_RADIO_SETPARAM,&r);
			}
			if ((strncasecmp(myrpt->rxchannel->name,"radio/", 6) == 0) || 
			    (strncasecmp(myrpt->rxchannel->name,"simpleusb/", 10) == 0))
			{
				ast_sendtext(myrpt->rxchannel,"RXCTCSS 1");

			}
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, ARB_ALPHA, (void *) "RXPLENA");
			return DC_COMPLETE;		
		case 57: /* RX CTCSS Disable */
			if ((strncasecmp(myrpt->rxchannel->name,"zap/", 4) == 0) || 
			    (strncasecmp(myrpt->rxchannel->name,"dahdi/", 6) == 0))
			{
				struct dahdi_radio_param r;

				memset(&r,0,sizeof(struct dahdi_radio_param));
				r.radpar = DAHDI_RADPAR_IGNORECT;
				r.data = 1;
				ioctl(myrpt->zaprxchannel->fds[0],DAHDI_RADIO_SETPARAM,&r);
			}

			if ((strncasecmp(myrpt->rxchannel->name,"radio/", 6) == 0) || 
			    (strncasecmp(myrpt->rxchannel->name,"simpleusb/", 10) == 0))
			{
				ast_sendtext(myrpt->rxchannel,"RXCTCSS 0");

			}
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, ARB_ALPHA, (void *) "RXPLDIS");
			return DC_COMPLETE;		
		case 58: /* TX CTCSS on input only Enable */
			myrpt->p.itxctcss = 1;
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, ARB_ALPHA, (void *) "TXIPLENA");
			return DC_COMPLETE;		
		case 59: /* TX CTCSS on input only Disable */
			myrpt->p.itxctcss = 0;
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, ARB_ALPHA, (void *) "TXIPLDIS");
			return DC_COMPLETE;		
#ifdef	_MDC_ENCODE_H_
		case 60: /* play MDC1200 burst */
			if (argc < 3) break;
			mdcp = ast_calloc(1,sizeof(struct mdcparams));
			if (!mdcp) return DC_ERROR;
			memset(mdcp,0,sizeof(*mdcp));
			if (*argv[1] == 'C')
			{
				if (argc < 5) return DC_ERROR;
				mdcp->DestID = (short) strtol(argv[3],NULL,16);
				mdcp->subcode = (short) strtol(argv[4],NULL,16);
			}
			strncpy(mdcp->type,argv[1],sizeof(mdcp->type) - 1);
			mdcp->UnitID = (short) strtol(argv[2],NULL,16);
			rpt_telemetry(myrpt,MDC1200,(void *)mdcp);
			return DC_COMPLETE;
#endif
		case 61: /* send GPIO change */
		case 62: /* same, without function complete (quietly, oooooooh baby!) */
			if (argc < 1) break;
			/* ignore if not a USB channel */
			if ((strncasecmp(myrpt->rxchannel->name,"radio/", 6) == 0) &&
			    (strncasecmp(myrpt->rxchannel->name,"beagle/", 7) == 0) &&
			    (strncasecmp(myrpt->rxchannel->name,"simpleusb/", 10) == 0)) break;
			/* go thru all the specs */
			for(i = 1; i < argc; i++)
			{
				if (sscanf(argv[i],"GPIO%d=%d",&j,&k) == 2)
				{
					sprintf(string,"GPIO %d %d",j,k);
					ast_sendtext(myrpt->rxchannel,string);
				}
				else if (sscanf(argv[i],"PP%d=%d",&j,&k) == 2)
				{
					sprintf(string,"PP %d %d",j,k);
					ast_sendtext(myrpt->rxchannel,string);
				}
			}
			if (myatoi(argv[0]) == 61) rpt_telemetry(myrpt,COMPLETE,NULL);
			return DC_COMPLETE;
		case 63: /* send pre-configured APRSTT notification */
		case 64: 
			if (argc < 2) break;
			if (!myrpt->p.aprstt) break;
			if (!myrpt->p.aprstt[0]) ast_copy_string(fname,APRSTT_PIPE,sizeof(fname) - 1);
			else snprintf(fname,sizeof(fname) - 1,APRSTT_SUB_PIPE,myrpt->p.aprstt);
			fp = fopen(fname,"w");
			if (!fp)
			{
				ast_log(LOG_WARNING,"Can not open APRSTT pipe %s\n",fname);
				break;
			}
			if (argc > 2) fprintf(fp,"%s %c\n",argv[1],*argv[2]);
			else fprintf(fp,"%s\n",argv[1]);
			fclose(fp);
			if (myatoi(argv[0]) == 63) rpt_telemetry(myrpt, ARB_ALPHA, (void *) argv[1]);
			return DC_COMPLETE;
		case 65: /* send POCSAG page */
			if (argc < 3) break;
			/* ignore if not a USB channel */
			if ((strncasecmp(myrpt->rxchannel->name,"radio/", 6) == 0) &&
			    (strncasecmp(myrpt->rxchannel->name,"voter/", 6) == 0) &&
			    (strncasecmp(myrpt->rxchannel->name,"simpleusb/", 10) == 0)) break;
			if (argc > 5)
				sprintf(string,"PAGE %s %s %s %s %s",argv[1],argv[2],argv[3],argv[4],argv[5]);
			else
				sprintf(string,"PAGE %s %s %s",argv[1],argv[2],argv[3]);
			telem = myrpt->tele.next;
			k = 0;
			while(telem != &myrpt->tele)
			{
				if (((telem->mode == ID) || (telem->mode == ID1) || 
					(telem->mode == IDTALKOVER)) && (!telem->killed))
				{
					if (telem->chan) ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV); /* Whoosh! */
					telem->killed = 1;
					myrpt->deferid = 1;
				}
				telem = telem->next;
			}
			gettimeofday(&myrpt->paging,NULL);
			ast_sendtext(myrpt->rxchannel,string);
			return DC_COMPLETE;
	}	
	return DC_INDETERMINATE;
}
/*
* Collect digits one by one until something matches
*/
static int collect_function_digits(struct rpt *myrpt, char *digits, 
	int command_source, struct rpt_link *mylink)
{
	int i,rv;
	char *stringp,*action,*param,*functiondigits;
	char function_table_name[30] = "";
	char workstring[200];
	
	struct ast_variable *vp;
	
	if (debug > 6) ast_log(LOG_NOTICE,"digits=%s  source=%d\n",digits, command_source);

	//if(debug)	
	//	printf("@@@@ Digits collected: %s, source: %d\n", digits, command_source);
	
	if (command_source == SOURCE_DPHONE) {
		if (!myrpt->p.dphone_functions) return DC_INDETERMINATE;
		strncpy(function_table_name, myrpt->p.dphone_functions, sizeof(function_table_name) - 1);
		}
	else if (command_source == SOURCE_ALT) {
		if (!myrpt->p.alt_functions) return DC_INDETERMINATE;
		strncpy(function_table_name, myrpt->p.alt_functions, sizeof(function_table_name) - 1);
		}
	else if (command_source == SOURCE_PHONE) {
		if (!myrpt->p.phone_functions) return DC_INDETERMINATE;
		strncpy(function_table_name, myrpt->p.phone_functions, sizeof(function_table_name) - 1);
		}
	else if (command_source == SOURCE_LNK)
		strncpy(function_table_name, myrpt->p.link_functions, sizeof(function_table_name) - 1);
	else
		strncpy(function_table_name, myrpt->p.functions, sizeof(function_table_name) - 1);
    /* find context for function table in rpt.conf file */
	vp = ast_variable_browse(myrpt->cfg, function_table_name);
	while(vp) {
		if(!strncasecmp(vp->name, digits, strlen(vp->name)))
			break;
		vp = vp->next;
	}	
	/* if function context not found */
	if(!vp) {
		int n;

		n = myrpt->longestfunc;
		if (command_source == SOURCE_LNK) n = myrpt->link_longestfunc;
		else 
		if (command_source == SOURCE_PHONE) n = myrpt->phone_longestfunc;
		else 
		if (command_source == SOURCE_ALT) n = myrpt->alt_longestfunc;
		else 
		if (command_source == SOURCE_DPHONE) n = myrpt->dphone_longestfunc;
		
		if(strlen(digits) >= n)
			return DC_ERROR;
		else
			return DC_INDETERMINATE;
	}	
	/* Found a match, retrieve value part and parse */
	strncpy(workstring, vp->value, sizeof(workstring) - 1 );
	stringp = workstring;
	action = strsep(&stringp, ",");
	param = stringp;
	if(debug)
		printf("@@@@ action: %s, param = %s\n",action, (param) ? param : "(null)");
	/* Look up the action */
	for(i = 0 ; i < (sizeof(function_table)/sizeof(struct function_table_tag)); i++){
		if(!strncasecmp(action, function_table[i].action, strlen(action)))
			break;
	}
	if(debug)
		printf("@@@@ table index i = %d\n",i);
	if(i == (sizeof(function_table)/sizeof(struct function_table_tag))){
		/* Error, action not in table */
		return DC_ERROR;
	}
	if(function_table[i].function == NULL){
		/* Error, function undefined */
		if(debug)
			printf("@@@@ NULL for action: %s\n",action);
		return DC_ERROR;
	}
	functiondigits = digits + strlen(vp->name);
	rv=(*function_table[i].function)(myrpt, param, functiondigits, command_source, mylink);
	if (debug > 6) ast_log(LOG_NOTICE,"rv=%i\n",rv);
	return(rv);
}


static void handle_link_data(struct rpt *myrpt, struct rpt_link *mylink,
	char *str)
{
char	tmp[512],tmp1[512],cmd[300] = "",dest[300],src[30],c;
int	i,seq, res, ts, rest;
struct rpt_link *l;
struct	ast_frame wf;

	wf.frametype = AST_FRAME_TEXT;
	wf.subclass = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	wf.src = "handle_link_data";
 	/* put string in our buffer */
	strncpy(tmp,str,sizeof(tmp) - 1);

        if (!strcmp(tmp,discstr))
        {
                mylink->disced = 1;
		mylink->retries = mylink->max_retries + 1;
                ast_softhangup(mylink->chan,AST_SOFTHANGUP_DEV);
                return;
        }
        if (!strcmp(tmp,newkeystr))
        {
		if ((!mylink->newkey) || mylink->newkeytimer)
		{
			mylink->newkeytimer = 0;
			mylink->newkey = 1;
			send_old_newkey(mylink->chan);
		}
                return;
        }
        if (!strcmp(tmp,newkey1str))
        {
		mylink->newkeytimer = 0;
		mylink->newkey = 2;
                return;
        }
        if (!strncmp(tmp,iaxkeystr,strlen(iaxkeystr)))
        {
		mylink->iaxkey = 1;
                return;
        }
	if (tmp[0] == 'G') /* got GPS data */
	{
		/* re-distriutee it to attached nodes */
		l = myrpt->links.next;
		/* otherwise, send it to all of em */
		while(l != &myrpt->links)
		{
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name,mylink->name)))
			{
				l = l->next;
				continue;
			}
			/* send, but not to src */
			if (strcmp(l->name,src)) {
				AST_FRAME_DATA(wf)= str;
				if (l->chan) rpt_qwrite(l,&wf); 
			}
			l = l->next;
		}
		return;
	}
	if (tmp[0] == 'L')
	{
		rpt_mutex_lock(&myrpt->lock);
		strcpy(mylink->linklist,tmp + 2);
		time(&mylink->linklistreceived);
		rpt_mutex_unlock(&myrpt->lock);
		if (debug > 6) ast_log(LOG_NOTICE,"@@@@ node %s recieved node list %s from node %s\n",
			myrpt->name,tmp,mylink->name);
		return;
	}
	if (tmp[0] == 'M')
	{
		rest = 0;
		if (sscanf(tmp,"%s %s %s %n",cmd,src,dest,&rest) < 3)
		{
			ast_log(LOG_WARNING, "Unable to parse message string %s\n",str);
			return;
		}
		if (!rest) return;
		if (strlen(tmp + rest) < 2) return;
		/* if is from me, ignore */
		if (!strcmp(src,myrpt->name)) return;
		/* if is for one of my nodes, dont do too much! */
	        for(i = 0; i < nrpts; i++)
		{
	                if(!strcmp(dest, rpt_vars[i].name))
			{
				ast_verbose("Private Text Message for %s From %s: %s\n",
					rpt_vars[i].name,src,tmp + rest);
				ast_log(LOG_EVENT,"Node %s Got Private Text Message From Node %s: %s\n",
					rpt_vars[i].name,src,tmp + rest);
				return;
			}
		}
		/* if is for everyone, at least log it */
		if (!strcmp(dest,"0"))
		{
			ast_verbose("Text Message From %s: %s\n",src,tmp + rest);
			ast_log(LOG_EVENT,"Node %s Got Text Message From Node %s: %s\n",
				myrpt->name,src,tmp + rest);
		}
		l = myrpt->links.next;
		/* otherwise, send it to all of em */
		while(l != &myrpt->links)
		{
			if (l->name[0] == '0') 
			{
				l = l->next;
				continue;
			}
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name,mylink->name)))
			{
				l = l->next;
				continue;
			}
			/* send, but not to src */
			if (strcmp(l->name,src)) {
				AST_FRAME_DATA(wf) = str;
				if (l->chan) rpt_qwrite(l,&wf); 
			}
			l = l->next;
		}
		return;
	}
	if (tmp[0] == 'T')
	{
		if (sscanf(tmp,"%s %s %s",cmd,src,dest) != 3)
		{
			ast_log(LOG_WARNING, "Unable to parse telem string %s\n",str);
			return;
		}
		l = myrpt->links.next;
		/* otherwise, send it to all of em */
		while(l != &myrpt->links)
		{
			if (l->name[0] == '0') 
			{
				l = l->next;
				continue;
			}
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name,mylink->name)))
			{
				l = l->next;
				continue;
			}
			/* send, but not to src */
			if (strcmp(l->name,src)) {
				AST_FRAME_DATA(wf) = str;
				if (l->chan) rpt_qwrite(l,&wf); 
			}
			l = l->next;
		}
		/* if is from me, ignore */
		if (!strcmp(src,myrpt->name)) return;

		/* if is a RANGER node, only allow CONNECTED message that directly involve our node */
		if (ISRANGER(myrpt->name) && (strncasecmp(dest,"CONNECTED,",10) ||
			(!strstr(dest,myrpt->name)))) return;

		/* set 'got T message' flag */
		mylink->gott = 1;

		/*  If inbound telemetry from a remote node, wake up from sleep if sleep mode is enabled */
		rpt_mutex_lock(&myrpt->lock); /* LOCK */
		if(myrpt->p.s[myrpt->p.sysstate_cur].sleepena){
			myrpt->sleeptimer = myrpt->p.sleeptime;
			if(myrpt->sleep){
				myrpt->sleep = 0;
			}
		}
		rpt_mutex_unlock(&myrpt->lock); /* UNLOCK */

		rpt_telemetry(myrpt,VARCMD,dest);
		return;
	}

	if (tmp[0] == 'C')
	{
		if (sscanf(tmp,"%s %s %s %s",cmd,src,tmp1,dest) != 4)
		{
			ast_log(LOG_WARNING, "Unable to parse ctcss string %s\n",str);
			return;
		}
		if (!strcmp(myrpt->p.ctgroup,"0")) return;
		if (strcasecmp(myrpt->p.ctgroup,tmp1)) return;
		l = myrpt->links.next;
		/* otherwise, send it to all of em */
		while(l != &myrpt->links)
		{
			if (l->name[0] == '0') 
			{
				l = l->next;
				continue;
			}
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name,mylink->name)))
			{
				l = l->next;
				continue;
			}
			/* send, but not to src */
			if (strcmp(l->name,src)) {
				AST_FRAME_DATA(wf) = str;
				if (l->chan) rpt_qwrite(l,&wf); 
			}
			l = l->next;
		}
		/* if is from me, ignore */
		if (!strcmp(src,myrpt->name)) return;
		sprintf(cmd,"TXTONE %.290s",dest);
		if (IS_XPMR(myrpt)) send_usb_txt(myrpt,cmd);
		return;
	}

	if (tmp[0] == 'K')
	{
		if (sscanf(tmp,"%s %s %s %d %d",cmd,dest,src,&seq,&ts) != 5)
		{
			ast_log(LOG_WARNING, "Unable to parse keying string %s\n",str);
			return;
		}
		if (dest[0] == '0')
		{
			strcpy(dest,myrpt->name);
		}		
		/* if not for me, redistribute to all links */
		if (strcmp(dest,myrpt->name))
		{
			l = myrpt->links.next;
			/* see if this is one in list */
			while(l != &myrpt->links)
			{
				if (l->name[0] == '0') 
				{
					l = l->next;
					continue;
				}
				/* dont send back from where it came */
				if ((l == mylink) || (!strcmp(l->name,mylink->name)))
				{
					l = l->next;
					continue;
				}
				/* if it is, send it and we're done */
				if (!strcmp(l->name,dest))
				{
					/* send, but not to src */
					if (strcmp(l->name,src)) {
						AST_FRAME_DATA(wf) = str;
						if (l->chan) rpt_qwrite(l,&wf);
					}
					return;
				}
				l = l->next;
			}
		}
		/* if not for me, or is broadcast, redistribute to all links */
		if ((strcmp(dest,myrpt->name)) || (dest[0] == '*'))
		{
			l = myrpt->links.next;
			/* otherwise, send it to all of em */
			while(l != &myrpt->links)
			{
				if (l->name[0] == '0') 
				{
					l = l->next;
					continue;
				}
				/* dont send back from where it came */
				if ((l == mylink) || (!strcmp(l->name,mylink->name)))
				{
					l = l->next;
					continue;
				}
				/* send, but not to src */
				if (strcmp(l->name,src)) {
					AST_FRAME_DATA(wf) = str;
					if (l->chan) rpt_qwrite(l,&wf); 
				}
				l = l->next;
			}
		}
		/* if not for me, end here */
		if (strcmp(dest,myrpt->name) && (dest[0] != '*')) return;
		if (cmd[1] == '?')
		{
			time_t now;
			int n = 0;

			time(&now);
			if (myrpt->lastkeyedtime)
			{
				n = (int)(now - myrpt->lastkeyedtime);
			}
			sprintf(tmp1,"K %s %s %d %d",src,myrpt->name,myrpt->keyed,n);
			AST_FRAME_DATA(wf)= tmp1;
			wf.datalen = strlen(tmp1) + 1;
			if (mylink->chan) rpt_qwrite(mylink,&wf); 
			return;
		}
		if (myrpt->topkeystate != 1) return;
		rpt_mutex_lock(&myrpt->lock);
		for(i = 0; i < TOPKEYN; i++)
		{
			if (!strcmp(myrpt->topkey[i].node,src)) break;
		}
		if (i >= TOPKEYN)
		{
			for(i = 0; i < TOPKEYN; i++)
			{
				if (!myrpt->topkey[i].node[0]) break;
			}
		}
		if (i < TOPKEYN)
		{
			strlcpy(myrpt->topkey[i].node,src,TOPKEYMAXSTR - 1);
			myrpt->topkey[i].timesince = ts;
			myrpt->topkey[i].keyed = seq;
		}
		rpt_mutex_unlock(&myrpt->lock);
		return;
	}
	if (tmp[0] == 'I')
	{
		if (sscanf(tmp,"%s %s %s",cmd,src,dest) != 3)
		{
			ast_log(LOG_WARNING, "Unable to parse ident string %s\n",str);
			return;
		}
		mdc1200_notify(myrpt,src,dest);
		strcpy(dest,"*");
	}
	else
	{
		if (sscanf(tmp,"%s %s %s %d %c",cmd,dest,src,&seq,&c) != 5)
		{
			ast_log(LOG_WARNING, "Unable to parse link string %s\n",str);
			return;
		}
		if (strcmp(cmd,"D"))
		{
			ast_log(LOG_WARNING, "Unable to parse link string %s\n",str);
			return;
		}
	}
	if (dest[0] == '0')
	{
		strcpy(dest,myrpt->name);
	}		

	/* if not for me, redistribute to all links */
	if (strcmp(dest,myrpt->name))
	{
		l = myrpt->links.next;
		/* see if this is one in list */
		while(l != &myrpt->links)
		{
			if (l->name[0] == '0') 
			{
				l = l->next;
				continue;
			}
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name,mylink->name)))
			{
				l = l->next;
				continue;
			}
			/* if it is, send it and we're done */
			if (!strcmp(l->name,dest))
			{
				/* send, but not to src */
				if (strcmp(l->name,src)) {
					AST_FRAME_DATA(wf) = str;
					if (l->chan) rpt_qwrite(l,&wf);
				}
				return;
			}
			l = l->next;
		}
		l = myrpt->links.next;
		/* otherwise, send it to all of em */
		while(l != &myrpt->links)
		{
			if (l->name[0] == '0') 
			{
				l = l->next;
				continue;
			}
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name,mylink->name)))
			{
				l = l->next;
				continue;
			}
			/* send, but not to src */
			if (strcmp(l->name,src)) {
				AST_FRAME_DATA(wf) = str;
				if (l->chan) rpt_qwrite(l,&wf); 
			}
			l = l->next;
		}
		return;
	}
	if (myrpt->p.archivedir)
	{
		char str[512];

		sprintf(str,"DTMF,%s,%c",mylink->name,c);
		donodelog(myrpt,str);
	}
	c = func_xlat(myrpt,c,&myrpt->p.outxlat);
	if (!c) return;
	rpt_mutex_lock(&myrpt->lock);
	if ((iswebtransceiver(mylink)) ||  /* if a WebTransceiver node */
		(!strncasecmp(mylink->chan->name,"tlb",3)))  /* or a tlb node */
	{
		if (c == myrpt->p.endchar) myrpt->cmdnode[0] = 0;
		else if (myrpt->cmdnode[0])
		{
			cmd[0] = 0;
			if (!strcmp(myrpt->cmdnode,"aprstt"))
			{
				char overlay,aprscall[100],fname[100];
				FILE *fp;

				snprintf(cmd, sizeof(cmd) - 1,"A%s", myrpt->dtmfbuf);
				overlay = aprstt_xlat(cmd,aprscall);
				if (overlay)
				{
					if (debug) ast_log(LOG_WARNING,"aprstt got string %s call %s overlay %c\n",cmd,aprscall,overlay);
					if (!myrpt->p.aprstt[0]) ast_copy_string(fname,APRSTT_PIPE,sizeof(fname) - 1);
					else snprintf(fname,sizeof(fname) - 1,APRSTT_SUB_PIPE,myrpt->p.aprstt);
					fp = fopen(fname,"w");
					if (!fp)
					{
						ast_log(LOG_WARNING,"Can not open APRSTT pipe %s\n",fname);
					}
					else
					{
						fprintf(fp,"%s %c\n",aprscall,overlay);
						fclose(fp);
						rpt_telemetry(myrpt, ARB_ALPHA, (void *) aprscall);
					}
				}
			}
			rpt_mutex_unlock(&myrpt->lock);
			if (strcmp(myrpt->cmdnode,"aprstt")) send_link_dtmf(myrpt,c);
			return;
		}
	}		
	if (c == myrpt->p.endchar) myrpt->stopgen = 1;
	if (myrpt->callmode == 1)
	{
		myrpt->exten[myrpt->cidx++] = c;
		myrpt->exten[myrpt->cidx] = 0;
		/* if this exists */
		if (ast_exists_extension(myrpt->pchannel,myrpt->patchcontext,myrpt->exten,1,NULL))
		{
			/* if this really it, end now */
			if (!ast_matchmore_extension(myrpt->pchannel,myrpt->patchcontext,
				myrpt->exten,1,NULL)) 
			{
				myrpt->callmode = 2;
				if(!myrpt->patchquiet)
				{
					rpt_mutex_unlock(&myrpt->lock);
					rpt_telemetry(myrpt,PROC,NULL); 
					rpt_mutex_lock(&myrpt->lock);
				}
			}
			else /* othewise, reset timer */
			{
				myrpt->calldigittimer = 1;
			}
		}
		/* if can continue, do so */
		if (!ast_canmatch_extension(myrpt->pchannel,myrpt->patchcontext,myrpt->exten,1,NULL))
		{
			/* call has failed, inform user */
			myrpt->callmode = 4;
		}
	}
	if ((!myrpt->inpadtest) && myrpt->p.aprstt && (!myrpt->cmdnode[0]) && (c == 'A'))
	{
		strcpy(myrpt->cmdnode,"aprstt");
		myrpt->dtmfidx = 0;
		myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
		rpt_mutex_unlock(&myrpt->lock);
		time(&myrpt->dtmf_time);
		return;
	}
	if ((!myrpt->inpadtest) && (c == myrpt->p.funcchar))
	{
		myrpt->rem_dtmfidx = 0;
		myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
		time(&myrpt->rem_dtmf_time);
		rpt_mutex_unlock(&myrpt->lock);
		return;
	} 
	else if (myrpt->rem_dtmfidx < 0)
	{
		if ((myrpt->callmode == 2) || (myrpt->callmode == 3))
		{
			myrpt->mydtmf = c;
		}
		if (myrpt->p.propagate_dtmf) do_dtmf_local(myrpt,c);
		if (myrpt->p.propagate_phonedtmf) do_dtmf_phone(myrpt,mylink,c);
		rpt_mutex_unlock(&myrpt->lock);
		return;
	}
	else if (((myrpt->inpadtest) || (c != myrpt->p.endchar)) && (myrpt->rem_dtmfidx >= 0))
	{
		time(&myrpt->rem_dtmf_time);
		if (myrpt->rem_dtmfidx < MAXDTMF)
		{
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx++] = c;
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
			
			rpt_mutex_unlock(&myrpt->lock);
			strncpy(cmd, myrpt->rem_dtmfbuf, sizeof(cmd) - 1);
			res = collect_function_digits(myrpt, cmd, SOURCE_LNK, mylink);
			rpt_mutex_lock(&myrpt->lock);
			
			switch(res){

				case DC_INDETERMINATE:
					break;
				
				case DC_REQ_FLUSH:
					myrpt->rem_dtmfidx = 0;
					myrpt->rem_dtmfbuf[0] = 0;
					break;
				
				
				case DC_COMPLETE:
				case DC_COMPLETEQUIET:
					myrpt->totalexecdcommands++;
					myrpt->dailyexecdcommands++;
					strncpy(myrpt->lastdtmfcommand, cmd, MAXDTMF-1);
					myrpt->lastdtmfcommand[MAXDTMF-1] = '\0';
					myrpt->rem_dtmfbuf[0] = 0;
					myrpt->rem_dtmfidx = -1;
					myrpt->rem_dtmf_time = 0;
					break;
				
				case DC_ERROR:
				default:
					myrpt->rem_dtmfbuf[0] = 0;
					myrpt->rem_dtmfidx = -1;
					myrpt->rem_dtmf_time = 0;
					break;
			}
		}

	}
	rpt_mutex_unlock(&myrpt->lock);
	return;
}

static void handle_link_phone_dtmf(struct rpt *myrpt, struct rpt_link *mylink,
	char c)
{

char	cmd[300];
int	res;

	if (myrpt->p.archivedir)
	{
		char str[512];

		sprintf(str,"DTMF(P),%s,%c",mylink->name,c);
		donodelog(myrpt,str);
	}
	if (mylink->phonemonitor) return;

	rpt_mutex_lock(&myrpt->lock);

	if (mylink->phonemode == 3) /*If in simplex dumb phone mode */
	{
		if(c == myrpt->p.endchar) /* If end char */
		{
			mylink->lastrealrx = 0; /* Keying state = off */
			rpt_mutex_unlock(&myrpt->lock);
			return;
		}

		if(c == myrpt->p.funcchar) /* If lead-in char */
		{
			mylink->lastrealrx = !mylink->lastrealrx; /* Toggle keying state */
			rpt_mutex_unlock(&myrpt->lock);
			return;
		}
	}
	else
	{
		if (c == myrpt->p.endchar)
		{
			if (mylink->lastrx && 
			    strncasecmp(mylink->chan->name,"echolink",8))
			{
				mylink->lastrealrx = 0;
				rpt_mutex_unlock(&myrpt->lock);
				return;
			}
			myrpt->stopgen = 1;
			if (myrpt->cmdnode[0])
			{
				cmd[0] = 0;
				if (!strcmp(myrpt->cmdnode,"aprstt"))
				{
					char overlay,aprscall[100],fname[100];
					FILE *fp;

					snprintf(cmd, sizeof(cmd) - 1,"A%s", myrpt->dtmfbuf);
					overlay = aprstt_xlat(cmd,aprscall);
					if (overlay)
					{
						if (debug) ast_log(LOG_WARNING,"aprstt got string %s call %s overlay %c\n",cmd,aprscall,overlay);
						if (!myrpt->p.aprstt[0]) ast_copy_string(fname,APRSTT_PIPE,sizeof(fname) - 1);
						else snprintf(fname,sizeof(fname) - 1,APRSTT_SUB_PIPE,myrpt->p.aprstt);
						fp = fopen(fname,"w");
						if (!fp)
						{
							ast_log(LOG_WARNING,"Can not open APRSTT pipe %s\n",fname);
						}
						else
						{
							fprintf(fp,"%s %c\n",aprscall,overlay);
							fclose(fp);
							rpt_telemetry(myrpt, ARB_ALPHA, (void *) aprscall);
						}
					}
				}
				myrpt->cmdnode[0] = 0;
				myrpt->dtmfidx = -1;
				myrpt->dtmfbuf[0] = 0;
				rpt_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt,COMPLETE,NULL);
				return;
			}
#if 0
			if ((myrpt->rem_dtmfidx < 0) && 
			    ((myrpt->callmode == 2) || (myrpt->callmode == 3)))
			{
				myrpt->mydtmf = c;
			}
#endif
		}
	}
	if (myrpt->cmdnode[0] && strcmp(myrpt->cmdnode,"aprstt"))
	{
		rpt_mutex_unlock(&myrpt->lock);
		send_link_dtmf(myrpt,c);
		return;
	}
	if (myrpt->callmode == 1)
	{
		myrpt->exten[myrpt->cidx++] = c;
		myrpt->exten[myrpt->cidx] = 0;
		/* if this exists */
		if (ast_exists_extension(myrpt->pchannel,myrpt->patchcontext,myrpt->exten,1,NULL))
		{
			/* if this really it, end now */
			if (!ast_matchmore_extension(myrpt->pchannel,myrpt->patchcontext,
				myrpt->exten,1,NULL)) 
			{
				myrpt->callmode = 2;
				if(!myrpt->patchquiet)
				{
					rpt_mutex_unlock(&myrpt->lock);
					rpt_telemetry(myrpt,PROC,NULL); 
					rpt_mutex_lock(&myrpt->lock);
				}
			}
			else /* othewise, reset timer */
			{
				myrpt->calldigittimer = 1;
			}
		}
		/* if can continue, do so */
		if (!ast_canmatch_extension(myrpt->pchannel,myrpt->patchcontext,myrpt->exten,1,NULL))
		{
			/* call has failed, inform user */
			myrpt->callmode = 4;
		}
	}
	if ((c != myrpt->p.funcchar) && (myrpt->rem_dtmfidx < 0) &&
	  (!myrpt->inpadtest) &&
	    ((myrpt->callmode == 2) || (myrpt->callmode == 3)))
	{
		myrpt->mydtmf = c;
	}
	if ((!myrpt->inpadtest) && myrpt->p.aprstt && (!myrpt->cmdnode[0]) && (c == 'A'))
	{
		strcpy(myrpt->cmdnode,"aprstt");
		myrpt->dtmfidx = 0;
		myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
		rpt_mutex_unlock(&myrpt->lock);
		time(&myrpt->dtmf_time);
		return;
	}
	if ((!myrpt->inpadtest) && (c == myrpt->p.funcchar))
	{
		myrpt->rem_dtmfidx = 0;
		myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
		time(&myrpt->rem_dtmf_time);
		rpt_mutex_unlock(&myrpt->lock);
		return;
	} 
	else if (((myrpt->inpadtest) || (c != myrpt->p.endchar)) && (myrpt->rem_dtmfidx >= 0))
	{
		time(&myrpt->rem_dtmf_time);
		if (myrpt->rem_dtmfidx < MAXDTMF)
		{
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx++] = c;
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
			
			rpt_mutex_unlock(&myrpt->lock);
			strncpy(cmd, myrpt->rem_dtmfbuf, sizeof(cmd) - 1);
			switch(mylink->phonemode)
			{
			    case 1:
				res = collect_function_digits(myrpt, cmd, 
					SOURCE_PHONE, mylink);
				break;
			    case 2:
				res = collect_function_digits(myrpt, cmd, 
					SOURCE_DPHONE,mylink);
				break;
			    case 4:
				res = collect_function_digits(myrpt, cmd, 
					SOURCE_ALT,mylink);
				break;
			    default:
				res = collect_function_digits(myrpt, cmd, 
					SOURCE_LNK, mylink);
				break;
			}

			rpt_mutex_lock(&myrpt->lock);
			
			switch(res){

				case DC_INDETERMINATE:
					break;
				
				case DC_DOKEY:
					mylink->lastrealrx = 1;
					break;
				
				case DC_REQ_FLUSH:
					myrpt->rem_dtmfidx = 0;
					myrpt->rem_dtmfbuf[0] = 0;
					break;
				
				
				case DC_COMPLETE:
				case DC_COMPLETEQUIET:
					myrpt->totalexecdcommands++;
					myrpt->dailyexecdcommands++;
					strncpy(myrpt->lastdtmfcommand, cmd, MAXDTMF-1);
					myrpt->lastdtmfcommand[MAXDTMF-1] = '\0';
					myrpt->rem_dtmfbuf[0] = 0;
					myrpt->rem_dtmfidx = -1;
					myrpt->rem_dtmf_time = 0;
					break;
				
				case DC_ERROR:
				default:
					myrpt->rem_dtmfbuf[0] = 0;
					myrpt->rem_dtmfidx = -1;
					myrpt->rem_dtmf_time = 0;
					break;
			}
		}

	}
        else if (myrpt->p.propagate_phonedtmf) do_dtmf_local(myrpt,c);
	rpt_mutex_unlock(&myrpt->lock);
	return;
}

/* Doug Hall RBI-1 serial data definitions:
 *
 * Byte 0: Expansion external outputs 
 * Byte 1: 
 *	Bits 0-3 are BAND as follows:
 *	Bits 4-5 are POWER bits as follows:
 *		00 - Low Power
 *		01 - Hi Power
 *		02 - Med Power
 *	Bits 6-7 are always set
 * Byte 2:
 *	Bits 0-3 MHZ in BCD format
 *	Bits 4-5 are offset as follows:
 *		00 - minus
 *		01 - plus
 *		02 - simplex
 *		03 - minus minus (whatever that is)
 *	Bit 6 is the 0/5 KHZ bit
 *	Bit 7 is always set
 * Byte 3:
 *	Bits 0-3 are 10 KHZ in BCD format
 *	Bits 4-7 are 100 KHZ in BCD format
 * Byte 4: PL Tone code and encode/decode enable bits
 *	Bits 0-5 are PL tone code (comspec binary codes)
 *	Bit 6 is encode enable/disable
 *	Bit 7 is decode enable/disable
 */

/* take the frequency from the 10 mhz digits (and up) and convert it
   to a band number */

static int rbi_mhztoband(char *str)
{
int	i;

	i = atoi(str) / 10; /* get the 10's of mhz */
	switch(i)
	{
	    case 2:
		return 10;
	    case 5:
		return 11;
	    case 14:
		return 2;
	    case 22:
		return 3;
	    case 44:
		return 4;
	    case 124:
		return 0;
	    case 125:
		return 1;
	    case 126:
		return 8;
	    case 127:
		return 5;
	    case 128:
		return 6;
	    case 129:
		return 7;
	    default:
		break;
	}
	return -1;
}

/* take a PL frequency and turn it into a code */
static int rbi_pltocode(char *str)
{
int i;
char *s;

	s = strchr(str,'.');
	i = 0;
	if (s) i = atoi(s + 1);
	i += atoi(str) * 10;
	switch(i)
	{
	    case 670:
		return 0;
	    case 719:
		return 1;
	    case 744:
		return 2;
	    case 770:
		return 3;
	    case 797:
		return 4;
	    case 825:
		return 5;
	    case 854:
		return 6;
	    case 885:
		return 7;
	    case 915:
		return 8;
	    case 948:
		return 9;
	    case 974:
		return 10;
	    case 1000:
		return 11;
	    case 1035:
		return 12;
	    case 1072:
		return 13;
	    case 1109:
		return 14;
	    case 1148:
		return 15;
	    case 1188:
		return 16;
	    case 1230:
		return 17;
	    case 1273:
		return 18;
	    case 1318:
		return 19;
	    case 1365:
		return 20;
	    case 1413:
		return 21;
	    case 1462:
		return 22;
	    case 1514:
		return 23;
	    case 1567:
		return 24;
	    case 1622:
		return 25;
	    case 1679:
		return 26;
	    case 1738:
		return 27;
	    case 1799:
		return 28;
	    case 1862:
		return 29;
	    case 1928:
		return 30;
	    case 2035:
		return 31;
	    case 2107:
		return 32;
	    case 2181:
		return 33;
	    case 2257:
		return 34;
	    case 2336:
		return 35;
	    case 2418:
		return 36;
	    case 2503:
		return 37;
	}
	return -1;
}

/*
* Shift out a formatted serial bit stream
*/

static void rbi_out_parallel(struct rpt *myrpt,unsigned char *data)
    {
#ifdef __i386__
    int i,j;
    unsigned char od,d;
    static volatile long long delayvar;

    for(i = 0 ; i < 5 ; i++){
        od = *data++; 
        for(j = 0 ; j < 8 ; j++){
            d = od & 1;
            outb(d,myrpt->p.iobase);
	    /* >= 15 us */
	    for(delayvar = 1; delayvar < 15000; delayvar++); 
            od >>= 1;
            outb(d | 2,myrpt->p.iobase);
	    /* >= 30 us */
	    for(delayvar = 1; delayvar < 30000; delayvar++); 
            outb(d,myrpt->p.iobase);
	    /* >= 10 us */
	    for(delayvar = 1; delayvar < 10000; delayvar++); 
            }
        }
	/* >= 50 us */
        for(delayvar = 1; delayvar < 50000; delayvar++); 
#endif
    }

static void rbi_out(struct rpt *myrpt,unsigned char *data)
{
struct dahdi_radio_param r;

	memset(&r,0,sizeof(struct dahdi_radio_param));
	r.radpar = DAHDI_RADPAR_REMMODE;
	r.data = DAHDI_RADPAR_REM_RBI1;
	/* if setparam ioctl fails, its probably not a pciradio card */
	if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_RADIO_SETPARAM,&r) == -1)
	{
		rbi_out_parallel(myrpt,data);
		return;
	}
	r.radpar = DAHDI_RADPAR_REMCOMMAND;
	memcpy(&r.data,data,5);
	if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_RADIO_SETPARAM,&r) == -1)
	{
		ast_log(LOG_WARNING,"Cannot send RBI command for channel %s\n",myrpt->zaprxchannel->name);
		return;
	}
}

static int serial_remote_io(struct rpt *myrpt, unsigned char *txbuf, int txbytes, 
	unsigned char *rxbuf, int rxmaxbytes, int asciiflag)
{
	int i,j,index,oldmode,olddata;
	struct dahdi_radio_param prm;
	char c;

#ifdef	FAKE_SERIAL_RESPONSE
	printf("String output was %s:\n",txbuf);
#endif
	 if(debug) {
	    ast_log(LOG_NOTICE, "ioport=%s baud=%d iofd=0x%x\n",myrpt->p.ioport,myrpt->p.iospeed,myrpt->iofd);
		printf("String output was %s:\n",txbuf);
		for(i = 0; i < txbytes; i++)
			printf("%02X ", (unsigned char ) txbuf[i]);
		printf("\n");
	}

	if (myrpt->iofd >= 0)  /* if to do out a serial port */
	{
		serial_rxflush(myrpt->iofd,20);
		if ((!strcmp(myrpt->remoterig, remote_rig_tm271)) ||
		   (!strcmp(myrpt->remoterig, remote_rig_kenwood)))
		{
			for(i = 0; i < txbytes; i++)
			{
				if (write(myrpt->iofd,&txbuf[i],1) != 1) return -1;
				usleep(6666);
			}
		}
		else
		{
			if (write(myrpt->iofd,txbuf,txbytes) != txbytes)
			{
				return -1;
			}
		}
		if ((!rxmaxbytes) || (rxbuf == NULL)) 
		{
			return(0);
		}
		memset(rxbuf,0,rxmaxbytes);
		for(i = 0; i < rxmaxbytes; i++)
		{
                        j = serial_rxready(myrpt->iofd,1000);
                        if (j < 1)
                        {
#ifdef	FAKE_SERIAL_RESPONSE
				strcpy((char *)rxbuf,(char *)txbuf);
				return(strlen((char *)rxbuf));
#else
                                ast_log(LOG_WARNING,"%d Serial device not responding on node %s\n",j,myrpt->name);
                                return(j);
#endif
                        }
			j = read(myrpt->iofd,&c,1);
			if (j < 1) 
			{
				return(i);
			}
			rxbuf[i] = c;
			if (asciiflag & 1)
			{
				rxbuf[i + 1] = 0;
				if (c == '\r') break;
			}
		}					
		if(debug) {
			printf("String returned was:\n");
			for(j = 0; j < i; j++)
				printf("%02X ", (unsigned char ) rxbuf[j]);
			printf("\n");
		}
		return(i);
	}

	/* if not a zap channel, cant use pciradio stuff */
	if (myrpt->rxchannel != myrpt->zaprxchannel) return -1;	

	prm.radpar = DAHDI_RADPAR_UIOMODE;
	if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_RADIO_GETPARAM,&prm) == -1) return -1;
	oldmode = prm.data;
	prm.radpar = DAHDI_RADPAR_UIODATA;
	if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_RADIO_GETPARAM,&prm) == -1) return -1;
	olddata = prm.data;
        prm.radpar = DAHDI_RADPAR_REMMODE;
        if ((asciiflag & 1) &&
	   strcmp(myrpt->remoterig, remote_rig_tm271) &&
	      strcmp(myrpt->remoterig, remote_rig_kenwood))
		  prm.data = DAHDI_RADPAR_REM_SERIAL_ASCII;
        else prm.data = DAHDI_RADPAR_REM_SERIAL;
	if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_RADIO_SETPARAM,&prm) == -1) return -1;
	if (asciiflag & 2)
	{
		i = DAHDI_ONHOOK;
		if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_HOOK,&i) == -1) return -1;
		usleep(100000);
	}
	if ((!strcmp(myrpt->remoterig, remote_rig_tm271)) ||
	   (!strcmp(myrpt->remoterig, remote_rig_kenwood)))
	{
		for(i = 0; i < txbytes - 1; i++)
		{
			
		        prm.radpar = DAHDI_RADPAR_REMCOMMAND;
		        prm.data = 0;
		       	prm.buf[0] = txbuf[i];
		        prm.index = 1;
			if (ioctl(myrpt->zaprxchannel->fds[0],
				DAHDI_RADIO_SETPARAM,&prm) == -1) return -1;
			usleep(6666);
		}
        	prm.radpar = DAHDI_RADPAR_REMMODE;
	        if (asciiflag & 1)  prm.data = DAHDI_RADPAR_REM_SERIAL_ASCII;
	        else prm.data = DAHDI_RADPAR_REM_SERIAL;
		if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_RADIO_SETPARAM,&prm) == -1) return -1;
	        prm.radpar = DAHDI_RADPAR_REMCOMMAND;
	        prm.data = rxmaxbytes;
	       	prm.buf[0] = txbuf[i];
	        prm.index = 1;
	}
	else
	{
	        prm.radpar = DAHDI_RADPAR_REMCOMMAND;
	        prm.data = rxmaxbytes;
	        memcpy(prm.buf,txbuf,txbytes);
	        prm.index = txbytes;
	}
	if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_RADIO_SETPARAM,&prm) == -1) return -1;
        if (rxbuf)
        {
                *rxbuf = 0;
                memcpy(rxbuf,prm.buf,prm.index);
        }
	index = prm.index;
        prm.radpar = DAHDI_RADPAR_REMMODE;
        prm.data = DAHDI_RADPAR_REM_NONE;
	if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_RADIO_SETPARAM,&prm) == -1) return -1;
	if (asciiflag & 2)
	{
		i = DAHDI_OFFHOOK;
		if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_HOOK,&i) == -1) return -1;
	}
	prm.radpar = DAHDI_RADPAR_UIOMODE;
	prm.data = oldmode;
	if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_RADIO_SETPARAM,&prm) == -1) return -1;
	prm.radpar = DAHDI_RADPAR_UIODATA;
	prm.data = olddata;
	if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_RADIO_SETPARAM,&prm) == -1) return -1;
        return(index);
}

static int civ_cmd(struct rpt *myrpt,unsigned char *cmd, int cmdlen)
{
unsigned char rxbuf[100];
int	i,rv ;

	rv = serial_remote_io(myrpt,cmd,cmdlen,rxbuf,(myrpt->p.dusbabek) ? 6 : cmdlen + 6,0);
	if (rv == -1) return(-1);
	if (myrpt->p.dusbabek)
	{
		if (rxbuf[0] != 0xfe) return(1);
		if (rxbuf[1] != 0xfe) return(1);
		if (rxbuf[4] != 0xfb) return(1);
		if (rxbuf[5] != 0xfd) return(1);
		return(0);
	}
	if (rv != (cmdlen + 6)) return(1);
	for(i = 0; i < 6; i++)
		if (rxbuf[i] != cmd[i]) return(1);
	if (rxbuf[cmdlen] != 0xfe) return(1);
	if (rxbuf[cmdlen + 1] != 0xfe) return(1);
	if (rxbuf[cmdlen + 4] != 0xfb) return(1);
	if (rxbuf[cmdlen + 5] != 0xfd) return(1);
	return(0);
}

static int sendkenwood(struct rpt *myrpt,char *txstr, char *rxstr)
{
int	i;

	if (debug)  printf("Send to kenwood: %s\n",txstr);
	i = serial_remote_io(myrpt, (unsigned char *)txstr, strlen(txstr), 
		(unsigned char *)rxstr,RAD_SERIAL_BUFLEN - 1,3);
	usleep(50000);
	if (i < 0) return -1;
	if ((i > 0) && (rxstr[i - 1] == '\r'))
		rxstr[i-- - 1] = 0;
	if (debug)  printf("Got from kenwood: %s\n",rxstr);
	return(i);
}

/* take a PL frequency and turn it into a code */
static int kenwood_pltocode(char *str)
{
int i;
char *s;

	s = strchr(str,'.');
	i = 0;
	if (s) i = atoi(s + 1);
	i += atoi(str) * 10;
	switch(i)
	{
	    case 670:
		return 1;
	    case 719:
		return 3;
	    case 744:
		return 4;
	    case 770:
		return 5;
	    case 797:
		return 6;
	    case 825:
		return 7;
	    case 854:
		return 8;
	    case 885:
		return 9;
	    case 915:
		return 10;
	    case 948:
		return 11;
	    case 974:
		return 12;
	    case 1000:
		return 13;
	    case 1035:
		return 14;
	    case 1072:
		return 15;
	    case 1109:
		return 16;
	    case 1148:
		return 17;
	    case 1188:
		return 18;
	    case 1230:
		return 19;
	    case 1273:
		return 20;
	    case 1318:
		return 21;
	    case 1365:
		return 22;
	    case 1413:
		return 23;
	    case 1462:
		return 24;
	    case 1514:
		return 25;
	    case 1567:
		return 26;
	    case 1622:
		return 27;
	    case 1679:
		return 28;
	    case 1738:
		return 29;
	    case 1799:
		return 30;
	    case 1862:
		return 31;
	    case 1928:
		return 32;
	    case 2035:
		return 33;
	    case 2107:
		return 34;
	    case 2181:
		return 35;
	    case 2257:
		return 36;
	    case 2336:
		return 37;
	    case 2418:
		return 38;
	    case 2503:
		return 39;
	}
	return -1;
}

/* take a PL frequency and turn it into a code */
static int tm271_pltocode(char *str)
{
int i;
char *s;

	s = strchr(str,'.');
	i = 0;
	if (s) i = atoi(s + 1);
	i += atoi(str) * 10;
	switch(i)
	{
	    case 670:
		return 0;
	    case 693:
		return 1;
	    case 719:
		return 2;
	    case 744:
		return 3;
	    case 770:
		return 4;
	    case 797:
		return 5;
	    case 825:
		return 6;
	    case 854:
		return 7;
	    case 885:
		return 8;
	    case 915:
		return 9;
	    case 948:
		return 10;
	    case 974:
		return 11;
	    case 1000:
		return 12;
	    case 1035:
		return 13;
	    case 1072:
		return 14;
	    case 1109:
		return 15;
	    case 1148:
		return 16;
	    case 1188:
		return 17;
	    case 1230:
		return 18;
	    case 1273:
		return 19;
	    case 1318:
		return 20;
	    case 1365:
		return 21;
	    case 1413:
		return 22;
	    case 1462:
		return 23;
	    case 1514:
		return 24;
	    case 1567:
		return 25;
	    case 1622:
		return 26;
	    case 1679:
		return 27;
	    case 1738:
		return 28;
	    case 1799:
		return 29;
	    case 1862:
		return 30;
	    case 1928:
		return 31;
	    case 2035:
		return 32;
	    case 2065:
		return 33;
	    case 2107:
		return 34;
	    case 2181:
		return 35;
	    case 2257:
		return 36;
	    case 2291:
		return 37;
	    case 2336:
		return 38;
	    case 2418:
		return 39;
	    case 2503:
		return 40;
	}
	return -1;
}

/* take a PL frequency and turn it into a code */
static int ft950_pltocode(char *str)
{
int i;
char *s;

	s = strchr(str,'.');
	i = 0;
	if (s) i = atoi(s + 1);
	i += atoi(str) * 10;
	switch(i)
	{
	    case 670:
		return 0;
	    case 693:
		return 1;
	    case 719:
		return 2;
	    case 744:
		return 3;
	    case 770:
		return 4;
	    case 797:
		return 5;
	    case 825:
		return 6;
	    case 854:
		return 7;
	    case 885:
		return 8;
	    case 915:
		return 9;
	    case 948:
		return 10;
	    case 974:
		return 11;
	    case 1000:
		return 12;
	    case 1035:
		return 13;
	    case 1072:
		return 14;
	    case 1109:
		return 15;
	    case 1148:
		return 16;
	    case 1188:
		return 17;
	    case 1230:
		return 18;
	    case 1273:
		return 19;
	    case 1318:
		return 20;
	    case 1365:
		return 21;
	    case 1413:
		return 22;
	    case 1462:
		return 23;
	    case 1514:
		return 24;
	    case 1567:
		return 25;
	    case 1622:
		return 26;
	    case 1679:
		return 27;
	    case 1738:
		return 28;
	    case 1799:
		return 29;
	    case 1862:
		return 30;
	    case 1928:
		return 31;
	    case 2035:
		return 32;
	    case 2065:
		return 33;
	    case 2107:
		return 34;
	    case 2181:
		return 35;
	    case 2257:
		return 36;
	    case 2291:
		return 37;
	    case 2336:
		return 38;
	    case 2418:
		return 39;
	    case 2503:
		return 40;
	}
	return -1;
}
/* take a PL frequency and turn it into a code */
static int ft100_pltocode(char *str)
{
int i;
char *s;

	s = strchr(str,'.');
	i = 0;
	if (s) i = atoi(s + 1);
	i += atoi(str) * 10;
	switch(i)
	{
	    case 670:
		return 0;
	    case 693:
		return 1;
	    case 719:
		return 2;
	    case 744:
		return 3;
	    case 770:
		return 4;
	    case 797:
		return 5;
	    case 825:
		return 6;
	    case 854:
		return 7;
	    case 885:
		return 8;
	    case 915:
		return 9;
	    case 948:
		return 10;
	    case 974:
		return 11;
	    case 1000:
		return 12;
	    case 1035:
		return 13;
	    case 1072:
		return 14;
	    case 1109:
		return 15;
	    case 1148:
		return 16;
	    case 1188:
		return 17;
	    case 1230:
		return 18;
	    case 1273:
		return 19;
	    case 1318:
		return 20;
	    case 1365:
		return 21;
	    case 1413:
		return 22;
	    case 1462:
		return 23;
	    case 1514:
		return 24;
	    case 1567:
		return 25;
	    case 1622:
		return 26;
	    case 1679:
		return 27;
	    case 1738:
		return 28;
	    case 1799:
		return 29;
	    case 1862:
		return 30;
	    case 1928:
		return 31;
	    case 2035:
		return 32;
	    case 2107:
		return 33;
	    case 2181:
		return 34;
	    case 2257:
		return 35;
	    case 2336:
		return 36;
	    case 2418:
		return 37;
	    case 2503:
		return 38;
	}
	return -1;
}

static int sendrxkenwood(struct rpt *myrpt, char *txstr, char *rxstr, 
	char *cmpstr)
{
int	i,j;

	for(i = 0;i < KENWOOD_RETRIES;i++)
	{
		j = sendkenwood(myrpt,txstr,rxstr);
		if (j < 0) return(j);
		if (j == 0) continue;
		if (!strncmp(rxstr,cmpstr,strlen(cmpstr))) return(0);
	}
	return(-1);
}		

static int setkenwood(struct rpt *myrpt)
{
char rxstr[RAD_SERIAL_BUFLEN],txstr[RAD_SERIAL_BUFLEN],freq[20];
char mhz[MAXREMSTR],offset[20],band,decimals[MAXREMSTR],band1,band2;
int myrxpl,mysplit,step;
	
int offsets[] = {0,2,1};
int powers[] = {2,1,0};

	if (sendrxkenwood(myrpt,"VMC 0,0\r",rxstr,"VMC") < 0) return -1;
	split_freq(mhz, decimals, myrpt->freq);
	mysplit = myrpt->splitkhz;
	if (atoi(mhz) > 400)
	{
		band = '6';
		band1 = '1';
		band2 = '5';
		if (!mysplit) mysplit = myrpt->p.default_split_70cm;
	}
	else
	{
		band = '2';
		band1 = '0';
		band2 = '2';
		if (!mysplit) mysplit = myrpt->p.default_split_2m;
	}
	sprintf(offset,"%06d000",mysplit);
	strcpy(freq,"000000");
	strlcpy(freq,decimals,strlen(freq)-1);
	myrxpl = myrpt->rxplon;
	if (IS_XPMR(myrpt)) myrxpl = 0;
	step = 0;
	if ((decimals[3] != '0') || (decimals[4] != '0')) step = 1;
	sprintf(txstr,"VW %c,%05d%s,%d,%d,0,%d,%d,,%02d,,%02d,%s\r",
		band,atoi(mhz),freq,step,offsets[(int)myrpt->offset],
		(myrpt->txplon != 0),myrxpl,
		kenwood_pltocode(myrpt->txpl),kenwood_pltocode(myrpt->rxpl),
		offset);
	if (sendrxkenwood(myrpt,txstr,rxstr,"VW") < 0) return -1;
	sprintf(txstr,"RBN %c\r",band2);
	if (sendrxkenwood(myrpt,txstr,rxstr,"RBN") < 0) return -1;
	sprintf(txstr,"PC %c,%d\r",band1,powers[(int)myrpt->powerlevel]);
	if (sendrxkenwood(myrpt,txstr,rxstr,"PC") < 0) return -1;
	return 0;
}

static int set_tmd700(struct rpt *myrpt)
{
char rxstr[RAD_SERIAL_BUFLEN],txstr[RAD_SERIAL_BUFLEN],freq[20];
char mhz[MAXREMSTR],offset[20],decimals[MAXREMSTR];
int myrxpl,mysplit,step;
	
int offsets[] = {0,2,1};
int powers[] = {2,1,0};
int band;

	if (sendrxkenwood(myrpt,"BC 0,0\r",rxstr,"BC") < 0) return -1;
	split_freq(mhz, decimals, myrpt->freq);
	mysplit = myrpt->splitkhz;
	if (atoi(mhz) > 400)
	{
		band = 8;
		if (!mysplit) mysplit = myrpt->p.default_split_70cm;
	}
	else
	{
		band = 2;
		if (!mysplit) mysplit = myrpt->p.default_split_2m;
	}
	sprintf(offset,"%06d000",mysplit);
	strcpy(freq,"000000");
	strlcpy(freq,decimals,strlen(freq)-1);
	step = 0;
	if ((decimals[3] != '0') || (decimals[4] != '0')) step = 1;
	myrxpl = myrpt->rxplon;
	if (IS_XPMR(myrpt)) myrxpl = 0;
	sprintf(txstr,"VW %d,%05d%s,%d,%d,0,%d,%d,0,%02d,0010,%02d,%s,0\r",
		band,atoi(mhz),freq,step,offsets[(int)myrpt->offset],
		(myrpt->txplon != 0),myrxpl,
		kenwood_pltocode(myrpt->txpl),kenwood_pltocode(myrpt->rxpl),
		offset);
	if (sendrxkenwood(myrpt,txstr,rxstr,"VW") < 0) return -1;
	if (sendrxkenwood(myrpt,"VMC 0,0\r",rxstr,"VMC") < 0) return -1;
	sprintf(txstr,"RBN\r");
	if (sendrxkenwood(myrpt,txstr,rxstr,"RBN") < 0) return -1;
	sprintf(txstr,"RBN %d\r",band);
	if (strncmp(rxstr,txstr,5))
	{
		if (sendrxkenwood(myrpt,txstr,rxstr,"RBN") < 0) return -1;
	}
	sprintf(txstr,"PC 0,%d\r",powers[(int)myrpt->powerlevel]);
	if (sendrxkenwood(myrpt,txstr,rxstr,"PC") < 0) return -1;
	return 0;
}

static int set_tm271(struct rpt *myrpt)
{
char rxstr[RAD_SERIAL_BUFLEN],txstr[RAD_SERIAL_BUFLEN],freq[20];
char mhz[MAXREMSTR],decimals[MAXREMSTR];
int  mysplit,step;
	
int offsets[] = {0,2,1};
int powers[] = {2,1,0};

	split_freq(mhz, decimals, myrpt->freq);
	strcpy(freq,"000000");
	strlcpy(freq,decimals,strlen(freq)-1);

	if (!myrpt->splitkhz)
		mysplit = myrpt->p.default_split_2m;
	else 
		mysplit = myrpt->splitkhz;

	step = 0;
	if ((decimals[3] != '0') || (decimals[4] != '0')) step = 1;
	sprintf(txstr,"VF %04d%s,%d,%d,0,%d,0,0,%02d,00,000,%05d000,0,0\r",
		atoi(mhz),freq,step,offsets[(int)myrpt->offset],
		(myrpt->txplon != 0),tm271_pltocode(myrpt->txpl),mysplit);

	if (sendrxkenwood(myrpt,"VM 0\r",rxstr,"VM") < 0) return -1;
	if (sendrxkenwood(myrpt,txstr,rxstr,"VF") < 0) return -1;
	sprintf(txstr,"PC %d\r",powers[(int)myrpt->powerlevel]);
	if (sendrxkenwood(myrpt,txstr,rxstr,"PC") < 0) return -1;
	return 0;
}

static int setrbi(struct rpt *myrpt)
{
char tmp[MAXREMSTR] = "",*s;
unsigned char rbicmd[5];
int	band,txoffset = 0,txpower = 0,rxpl;

	/* must be a remote system */
	if (!myrpt->remoterig) return(0);
	if (!myrpt->remoterig[0]) return(0);
	/* must have rbi hardware */
	if (strncmp(myrpt->remoterig,remote_rig_rbi,3)) return(0);
	if (setrbi_check(myrpt) == -1) return(-1);
	strlcpy(tmp, myrpt->freq, sizeof(tmp) - 1);
	s = strchr(tmp,'.');
	/* if no decimal, is invalid */
	
	if (s == NULL){
		if(debug)
			printf("@@@@ Frequency needs a decimal\n");
		return -1;
	}
	
	*s++ = 0;
	if (strlen(tmp) < 2){
		if(debug)
			printf("@@@@ Bad MHz digits: %s\n", tmp);
	 	return -1;
	}
	 
	if (strlen(s) < 3){
		if(debug)
			printf("@@@@ Bad KHz digits: %s\n", s);
	 	return -1;
	}

	if ((s[2] != '0') && (s[2] != '5')){
		if(debug)
			printf("@@@@ KHz must end in 0 or 5: %c\n", s[2]);
	 	return -1;
	}
	 
	band = rbi_mhztoband(tmp);
	if (band == -1){
		if(debug)
			printf("@@@@ Bad Band: %s\n", tmp);
	 	return -1;
	}
	
	rxpl = rbi_pltocode(myrpt->rxpl);
	
	if (rxpl == -1){
		if(debug)
			printf("@@@@ Bad TX PL: %s\n", myrpt->rxpl);
	 	return -1;
	}

	
	switch(myrpt->offset)
	{
	    case REM_MINUS:
		txoffset = 0;
		break;
	    case REM_PLUS:
		txoffset = 0x10;
		break;
	    case REM_SIMPLEX:
		txoffset = 0x20;
		break;
	}
	switch(myrpt->powerlevel)
	{
	    case REM_LOWPWR:
		txpower = 0;
		break;
	    case REM_MEDPWR:
		txpower = 0x20;
		break;
	    case REM_HIPWR:
		txpower = 0x10;
		break;
	}
	rbicmd[0] = 0;
	rbicmd[1] = band | txpower | 0xc0;
	rbicmd[2] = (*(s - 2) - '0') | txoffset | 0x80;
	if (s[2] == '5') rbicmd[2] |= 0x40;
	rbicmd[3] = ((*s - '0') << 4) + (s[1] - '0');
	rbicmd[4] = rxpl;
	if (myrpt->txplon) rbicmd[4] |= 0x40;
	if (myrpt->rxplon) rbicmd[4] |= 0x80;
	rbi_out(myrpt,rbicmd);
	return 0;
}

static int setrtx(struct rpt *myrpt)
{
char tmp[MAXREMSTR] = "",*s,rigstr[200],pwr,res = 0;
int	band,rxpl,txpl,mysplit;
float ofac;
double txfreq;

	/* must be a remote system */
	if (!myrpt->remoterig) return(0);
	if (!myrpt->remoterig[0]) return(0);
	/* must have rtx hardware */
	if (!ISRIG_RTX(myrpt->remoterig)) return(0);
	/* must be a usbradio interface type */
	if (!IS_XPMR(myrpt)) return(0);
	strlcpy(tmp, myrpt->freq, sizeof(tmp) - 1);
	s = strchr(tmp,'.');
	/* if no decimal, is invalid */
	
	if(debug)printf("setrtx() %s %s\n",myrpt->name,myrpt->remoterig);

	if (s == NULL){
		if(debug)
			printf("@@@@ Frequency needs a decimal\n");
		return -1;
	}
	*s++ = 0;
	if (strlen(tmp) < 2){
		if(debug)
			printf("@@@@ Bad MHz digits: %s\n", tmp);
	 	return -1;
	}
	 
	if (strlen(s) < 3){
		if(debug)
			printf("@@@@ Bad KHz digits: %s\n", s);
	 	return -1;
	}

	if ((s[2] != '0') && (s[2] != '5')){
		if(debug)
			printf("@@@@ KHz must end in 0 or 5: %c\n", s[2]);
	 	return -1;
	}
	 
	band = rbi_mhztoband(tmp);
	if (band == -1){
		if(debug)
			printf("@@@@ Bad Band: %s\n", tmp);
	 	return -1;
	}
	
	rxpl = rbi_pltocode(myrpt->rxpl);
	
	if (rxpl == -1){
		if(debug)
			printf("@@@@ Bad RX PL: %s\n", myrpt->rxpl);
	 	return -1;
	}

	txpl = rbi_pltocode(myrpt->txpl);
	
	if (txpl == -1){
		if(debug)
			printf("@@@@ Bad TX PL: %s\n", myrpt->txpl);
	 	return -1;
	}
	
	res = setrtx_check(myrpt);
	if (res < 0) return res;
	mysplit = myrpt->splitkhz;
	if (!mysplit) 
	{
		if (!strcmp(myrpt->remoterig,remote_rig_rtx450))
			mysplit = myrpt->p.default_split_70cm;
		else
			mysplit = myrpt->p.default_split_2m;
	}
	if (myrpt->offset != REM_SIMPLEX)
		ofac = ((float) mysplit) / 1000.0;
	else ofac = 0.0;
	if (myrpt->offset == REM_MINUS) ofac = -ofac;

	txfreq = atof(myrpt->freq) +  ofac;
	pwr = 'L';
	if (myrpt->powerlevel == REM_HIPWR) pwr = 'H';
	if (!res)
	{
		sprintf(rigstr,"SETFREQ %s %f %s %s %c",myrpt->freq,txfreq,
			(myrpt->rxplon) ? myrpt->rxpl : "0.0",
			(myrpt->txplon) ? myrpt->txpl : "0.0",pwr);
		send_usb_txt(myrpt,rigstr);
		rpt_telemetry(myrpt,COMPLETE,NULL);
		res = 0;
	}
	return 0;
}

static int setxpmr(struct rpt *myrpt, int dotx)
{
	char rigstr[200];
	int rxpl,txpl;

	/* must be a remote system */
	if (!myrpt->remoterig) return(0);
	if (!myrpt->remoterig[0]) return(0);
	/* must not have rtx hardware */
	if (ISRIG_RTX(myrpt->remoterig)) return(0);
	/* must be a usbradio interface type */
	if (!IS_XPMR(myrpt)) return(0);
	
	if(debug)printf("setxpmr() %s %s\n",myrpt->name,myrpt->remoterig );

	rxpl = rbi_pltocode(myrpt->rxpl);
	
	if (rxpl == -1){
		if(debug)
			printf("@@@@ Bad RX PL: %s\n", myrpt->rxpl);
	 	return -1;
	}

	if (dotx)
	{
		txpl = rbi_pltocode(myrpt->txpl);
		if (txpl == -1){
			if(debug)
				printf("@@@@ Bad TX PL: %s\n", myrpt->txpl);
		 	return -1;
		}
		sprintf(rigstr,"SETFREQ 0.0 0.0 %s %s L",
			(myrpt->rxplon) ? myrpt->rxpl : "0.0",
			(myrpt->txplon) ? myrpt->txpl : "0.0");
	}
	else
	{
		sprintf(rigstr,"SETFREQ 0.0 0.0 %s 0.0 L",
			(myrpt->rxplon) ? myrpt->rxpl : "0.0");

	}
	send_usb_txt(myrpt,rigstr);
	return 0;
}


static int setrbi_check(struct rpt *myrpt)
{
char tmp[MAXREMSTR] = "",*s;
int	band,txpl;

	/* must be a remote system */
	if (!myrpt->remote) return(0);
	/* must have rbi hardware */
	if (strncmp(myrpt->remoterig,remote_rig_rbi,3)) return(0);
	strlcpy(tmp, myrpt->freq, sizeof(tmp) - 1);
	s = strchr(tmp,'.');
	/* if no decimal, is invalid */
	
	if (s == NULL){
		if(debug)
			printf("@@@@ Frequency needs a decimal\n");
		return -1;
	}
	
	*s++ = 0;
	if (strlen(tmp) < 2){
		if(debug)
			printf("@@@@ Bad MHz digits: %s\n", tmp);
	 	return -1;
	}
	 
	if (strlen(s) < 3){
		if(debug)
			printf("@@@@ Bad KHz digits: %s\n", s);
	 	return -1;
	}

	if ((s[2] != '0') && (s[2] != '5')){
		if(debug)
			printf("@@@@ KHz must end in 0 or 5: %c\n", s[2]);
	 	return -1;
	}
	 
	band = rbi_mhztoband(tmp);
	if (band == -1){
		if(debug)
			printf("@@@@ Bad Band: %s\n", tmp);
	 	return -1;
	}
	
	txpl = rbi_pltocode(myrpt->txpl);
	
	if (txpl == -1){
		if(debug)
			printf("@@@@ Bad TX PL: %s\n", myrpt->txpl);
	 	return -1;
	}
	return 0;
}

static int setrtx_check(struct rpt *myrpt)
{
char tmp[MAXREMSTR] = "",*s;
int	band,txpl,rxpl;

	/* must be a remote system */
	if (!myrpt->remote) return(0);
	/* must have rbi hardware */
	if (strncmp(myrpt->remoterig,remote_rig_rbi,3)) return(0);
	strlcpy(tmp, myrpt->freq, sizeof(tmp) - 1);
	s = strchr(tmp,'.');
	/* if no decimal, is invalid */
	
	if (s == NULL){
		if(debug)
			printf("@@@@ Frequency needs a decimal\n");
		return -1;
	}
	
	*s++ = 0;
	if (strlen(tmp) < 2){
		if(debug)
			printf("@@@@ Bad MHz digits: %s\n", tmp);
	 	return -1;
	}
	 
	if (strlen(s) < 3){
		if(debug)
			printf("@@@@ Bad KHz digits: %s\n", s);
	 	return -1;
	}

	if ((s[2] != '0') && (s[2] != '5')){
		if(debug)
			printf("@@@@ KHz must end in 0 or 5: %c\n", s[2]);
	 	return -1;
	}
	 
	band = rbi_mhztoband(tmp);
	if (band == -1){
		if(debug)
			printf("@@@@ Bad Band: %s\n", tmp);
	 	return -1;
	}
	
	txpl = rbi_pltocode(myrpt->txpl);
	
	if (txpl == -1){
		if(debug)
			printf("@@@@ Bad TX PL: %s\n", myrpt->txpl);
	 	return -1;
	}

	rxpl = rbi_pltocode(myrpt->rxpl);
	
	if (rxpl == -1){
		if(debug)
			printf("@@@@ Bad RX PL: %s\n", myrpt->rxpl);
	 	return -1;
	}
	return 0;
}

static int check_freq_kenwood(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if (m == 144){ /* 2 meters */
		if(d < 10100)
			return -1;
	}
	else if((m >= 145) && (m < 148)){
		;
	}
	else if((m >= 430) && (m < 450)){ /* 70 centimeters */
		;
	}
	else
		return -1;
	
	if(defmode)
		*defmode = dflmd;	


	return 0;
}


static int check_freq_tm271(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if (m == 144){ /* 2 meters */
		if(d < 10100)
			return -1;
	}
	else if((m >= 145) && (m < 148)){
		;
	}
	else	return -1;
	
	if(defmode)
		*defmode = dflmd;	


	return 0;
}


/* Check for valid rbi frequency */
/* Hard coded limits now, configurable later, maybe? */

static int check_freq_rbi(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if(m == 50){ /* 6 meters */
		if(d < 10100)
			return -1;
	}
	else if((m >= 51) && ( m < 54)){
                ;
	}
	else if(m == 144){ /* 2 meters */
		if(d < 10100)
			return -1;
	}
	else if((m >= 145) && (m < 148)){
		;
	}
 	else if((m >= 222) && (m < 225)){ /* 1.25 meters */
		;
	}
	else if((m >= 430) && (m < 450)){ /* 70 centimeters */
		;
	}
	else if((m >= 1240) && (m < 1300)){ /* 23 centimeters */
		;
	}
	else
		return -1;
	
	if(defmode)
		*defmode = dflmd;	


	return 0;
}

/* Check for valid rtx frequency */
/* Hard coded limits now, configurable later, maybe? */

static int check_freq_rtx(int m, int d, int *defmode, struct rpt *myrpt)
{
	int dflmd = REM_MODE_FM;

	if (!strcmp(myrpt->remoterig,remote_rig_rtx150))
	{

		if(m == 144){ /* 2 meters */
			if(d < 10100)
				return -1;
		}
		else if((m >= 145) && (m < 148)){
			;
		}
		else
			return -1;
	}
	else 
	{
		if((m >= 430) && (m < 450)){ /* 70 centimeters */
			;
		}
		else
			return -1;
	}
	if(defmode)
		*defmode = dflmd;	


	return 0;
}

/*
 * Convert decimals of frequency to int
 */

static int decimals2int(char *fraction)
{
	int i;
	char len = strlen(fraction);
	int multiplier = 100000;
	int res = 0;

	if(!len)
		return 0;
	for( i = 0 ; i < len ; i++, multiplier /= 10)
		res += (fraction[i] - '0') * multiplier;
	return res;
}


/*
* Split frequency into mhz and decimals
*/
 
static int split_freq(char *mhz, char *decimals, char *freq)
{
	char freq_copy[MAXREMSTR];
	char *decp;

	strlcpy(freq_copy, freq, MAXREMSTR-1);
	decp = strchr(freq_copy, '.');
	if(decp){
		*decp++ = 0;
		strncpy(mhz, freq_copy, MAXREMSTR);
		strcpy(decimals, "00000");
		strlcpy(decimals, decp, strlen(decimals)-1);
		decimals[5] = 0;
		return 0;
	}
	else
		return -1;

}
	
/*
* Split ctcss frequency into hertz and decimal
*/
 
static int split_ctcss_freq(char *hertz, char *decimal, char *freq)
{
	char freq_copy[MAXREMSTR];
	char *decp;

	decp = strchr(strncpy(freq_copy, freq, MAXREMSTR-1),'.');
	if(decp){
		*decp++ = 0;
		strlcpy(hertz, freq_copy, MAXREMSTR);
		strlcpy(decimal, decp, strlen(decp));
		decimal[strlen(decp)] = '\0';
		return 0;
	}
	else
		return -1;
}



/*
* FT-897 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


static int check_freq_ft897(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if(m == 1){ /* 160 meters */
		dflmd =	REM_MODE_LSB; 
		if(d < 80000)
			return -1;
	}
	else if(m == 3){ /* 80 meters */
		dflmd = REM_MODE_LSB;
		if(d < 50000)
			return -1;
	}
	else if(m == 7){ /* 40 meters */
		dflmd = REM_MODE_LSB;
		if(d > 30000)
			return -1;
	}
	else if(m == 14){ /* 20 meters */
		dflmd = REM_MODE_USB;
		if(d > 35000)
			return -1;
	}
	else if(m == 18){ /* 17 meters */
		dflmd = REM_MODE_USB;
		if((d < 6800) || (d > 16800))
			return -1;
	}
	else if(m == 21){ /* 15 meters */
		dflmd = REM_MODE_USB;
		if((d < 20000) || (d > 45000))
			return -1;
	}
	else if(m == 24){ /* 12 meters */
		dflmd = REM_MODE_USB;
		if((d < 89000) || (d > 99000))
			return -1;
	}
	else if(m == 28){ /* 10 meters */
		dflmd = REM_MODE_USB;
	}
	else if(m == 29){ 
		if(d >= 51000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
		if(d > 70000)
			return -1;
	}
	else if(m == 50){ /* 6 meters */
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;

	}
	else if((m >= 51) && ( m < 54)){
		dflmd = REM_MODE_FM;
	}
	else if(m == 144){ /* 2 meters */
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
	}
	else if((m >= 145) && (m < 148)){
		dflmd = REM_MODE_FM;
	}
	else if((m >= 430) && (m < 450)){ /* 70 centimeters */
		if(m  < 438)
			dflmd = REM_MODE_USB;
		else
			dflmd = REM_MODE_FM;
		;
	}
	else
		return -1;

	if(defmode)
		*defmode = dflmd;

	return 0;
}

/*
* Set a new frequency for the FT897
*/

static int set_freq_ft897(struct rpt *myrpt, char *newfreq)
{
	unsigned char cmdstr[5];
	int m,d;
	char mhz[MAXREMSTR];
	char decimals[MAXREMSTR];

	if(debug) 
		printf("New frequency: %s\n",newfreq);

	if(split_freq(mhz, decimals, newfreq))
		return -1; 

	m = atoi(mhz);
	d = atoi(decimals);

	/* The FT-897 likes packed BCD frequencies */

	cmdstr[0] = ((m / 100) << 4) + ((m % 100)/10);			/* 100MHz 10Mhz */
	cmdstr[1] = ((m % 10) << 4) + (d / 10000);			/* 1MHz 100KHz */
	cmdstr[2] = (((d % 10000)/1000) << 4) + ((d % 1000)/ 100);	/* 10KHz 1KHz */
	cmdstr[3] = (((d % 100)/10) << 4) + (d % 10);			/* 100Hz 10Hz */
	cmdstr[4] = 0x01;						/* command */

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);

}

/* ft-897 simple commands */

static int simple_command_ft897(struct rpt *myrpt, char command)
{
	unsigned char cmdstr[5];
	
	memset(cmdstr, 0, 5);

	cmdstr[4] = command;	

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);

}

/* ft-897 offset */

static int set_offset_ft897(struct rpt *myrpt, char offset)
{
	unsigned char cmdstr[5];
	int mysplit,res;
	char mhz[MAXREMSTR],decimal[MAXREMSTR];

	if(split_freq(mhz, decimal, myrpt->freq))
		return -1; 

	mysplit = myrpt->splitkhz * 1000;
	if (!mysplit)
	{
		if (atoi(mhz) > 400) 
			mysplit = myrpt->p.default_split_70cm * 1000;
		else 
			mysplit = myrpt->p.default_split_2m * 1000;
	}

	memset(cmdstr, 0, 5);

	if(debug > 6)
		ast_log(LOG_NOTICE,"split=%i\n",mysplit * 1000);

	cmdstr[0] = (mysplit / 10000000) +  ((mysplit % 10000000) / 1000000);
	cmdstr[1] = (((mysplit % 1000000) / 100000) << 4) + ((mysplit % 100000) / 10000);
	cmdstr[2] = (((mysplit % 10000) / 1000) << 4) + ((mysplit % 1000) / 100);
	cmdstr[3] = ((mysplit % 10) << 4) + ((mysplit % 100) / 10);
	cmdstr[4] = 0xf9;						/* command */
	res = serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
	if (res) return res;	

	memset(cmdstr, 0, 5);

	switch(offset){
		case	REM_SIMPLEX:
			cmdstr[0] = 0x89;
			break;

		case	REM_MINUS:
			cmdstr[0] = 0x09;
			break;
		
		case	REM_PLUS:
			cmdstr[0] = 0x49;
			break;	

		default:
			return -1;
	}

	cmdstr[4] = 0x09;	

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}

/* ft-897 mode */

static int set_mode_ft897(struct rpt *myrpt, char newmode)
{
	unsigned char cmdstr[5];
	
	memset(cmdstr, 0, 5);
	
	switch(newmode){
		case	REM_MODE_FM:
			cmdstr[0] = 0x08;
			break;

		case	REM_MODE_USB:
			cmdstr[0] = 0x01;
			break;

		case	REM_MODE_LSB:
			cmdstr[0] = 0x00;
			break;

		case	REM_MODE_AM:
			cmdstr[0] = 0x04;
			break;
		
		default:
			return -1;
	}
	cmdstr[4] = 0x07;	

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}

/* Set tone encode and decode modes */

static int set_ctcss_mode_ft897(struct rpt *myrpt, char txplon, char rxplon)
{
	unsigned char cmdstr[5];
	
	memset(cmdstr, 0, 5);
	
	if(rxplon && txplon)
		cmdstr[0] = 0x2A; /* Encode and Decode */
	else if (!rxplon && txplon)
		cmdstr[0] = 0x4A; /* Encode only */
	else if (rxplon && !txplon)
		cmdstr[0] = 0x3A; /* Encode only */
	else
		cmdstr[0] = 0x8A; /* OFF */

	cmdstr[4] = 0x0A;	

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}


/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_ft897(struct rpt *myrpt, char *txtone, char *rxtone)
{
	unsigned char cmdstr[5];
	char hertz[MAXREMSTR],decimal[MAXREMSTR];
	int h,d;	

	memset(cmdstr, 0, 5);

	if(split_ctcss_freq(hertz, decimal, txtone))
		return -1; 

	h = atoi(hertz);
	d = atoi(decimal);
	
	cmdstr[0] = ((h / 100) << 4) + (h % 100)/ 10;
	cmdstr[1] = ((h % 10) << 4) + (d % 10);
	
	if(rxtone){
	
		if(split_ctcss_freq(hertz, decimal, rxtone))
			return -1; 

		h = atoi(hertz);
		d = atoi(decimal);
	
		cmdstr[2] = ((h / 100) << 4) + (h % 100)/ 10;
		cmdstr[3] = ((h % 10) << 4) + (d % 10);
	}
	cmdstr[4] = 0x0B;	

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}	



static int set_ft897(struct rpt *myrpt)
{
	int res;
	
	if(debug > 2)
		printf("@@@@ lock on\n");
	res = simple_command_ft897(myrpt, 0x00);			/* LOCK on */	

	if(debug > 2)
		printf("@@@@ ptt off\n");
	if(!res){
		res = simple_command_ft897(myrpt, 0x88);		/* PTT off */
	}

	if(debug > 2)
		printf("Modulation mode\n");
	if(!res){
		res = set_mode_ft897(myrpt, myrpt->remmode);		/* Modulation mode */
	}

	if(debug > 2)
		printf("Split off\n");
	if(!res){
		simple_command_ft897(myrpt, 0x82);			/* Split off */
	}

	if(debug > 2)
		printf("Frequency\n");
	if(!res){
		res = set_freq_ft897(myrpt, myrpt->freq);		/* Frequency */
		usleep(FT897_SERIAL_DELAY*2);
	}
	if((myrpt->remmode == REM_MODE_FM)){
		if(debug > 2)
			printf("Offset\n");
		if(!res){
			res = set_offset_ft897(myrpt, myrpt->offset);	/* Offset if FM */
			usleep(FT897_SERIAL_DELAY);
		}
		if((!res)&&(myrpt->rxplon || myrpt->txplon)){
			usleep(FT897_SERIAL_DELAY);
			if(debug > 2)
				printf("CTCSS tone freqs.\n");
			res = set_ctcss_freq_ft897(myrpt, myrpt->txpl, myrpt->rxpl); /* CTCSS freqs if CTCSS is enabled */
			usleep(FT897_SERIAL_DELAY);
		}
		if(!res){
			if(debug > 2)
				printf("CTCSS mode\n");
			res = set_ctcss_mode_ft897(myrpt, myrpt->txplon, myrpt->rxplon); /* CTCSS mode */
			usleep(FT897_SERIAL_DELAY);
		}
	}
	if((myrpt->remmode == REM_MODE_USB)||(myrpt->remmode == REM_MODE_LSB)){
		if(debug > 2)
			printf("Clarifier off\n");
		simple_command_ft897(myrpt, 0x85);			/* Clarifier off if LSB or USB */
	}
	return res;
}

static int closerem_ft897(struct rpt *myrpt)
{
	simple_command_ft897(myrpt, 0x88); /* PTT off */
	return 0;
}	

/*
* Bump frequency up or down by a small amount 
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz 
*/

static int multimode_bump_freq_ft897(struct rpt *myrpt, int interval)
{
	int m,d;
	char mhz[MAXREMSTR], decimals[MAXREMSTR];

	if(debug)
		printf("Before bump: %s\n", myrpt->freq);

	if(split_freq(mhz, decimals, myrpt->freq))
		return -1;
	
	m = atoi(mhz);
	d = atoi(decimals);

	d += (interval / 10); /* 10Hz resolution */
	if(d < 0){
		m--;
		d += 100000;
	}
	else if(d >= 100000){
		m++;
		d -= 100000;
	}

	if(check_freq_ft897(m, d, NULL)){
		if(debug)
			printf("Bump freq invalid\n");
		return -1;
	}

	snprintf(myrpt->freq, MAXREMSTR, "%d.%05d", m, d);

	if(debug)
		printf("After bump: %s\n", myrpt->freq);

	return set_freq_ft897(myrpt, myrpt->freq);	
}
/*
* FT-100 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


static int check_freq_ft100(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if(m == 1){ /* 160 meters */
		dflmd =	REM_MODE_LSB; 
		if(d < 80000)
			return -1;
	}
	else if(m == 3){ /* 80 meters */
		dflmd = REM_MODE_LSB;
		if(d < 50000)
			return -1;
	}
	else if(m == 7){ /* 40 meters */
		dflmd = REM_MODE_LSB;
		if(d > 30000)
			return -1;
	}
	else if(m == 14){ /* 20 meters */
		dflmd = REM_MODE_USB;
		if(d > 35000)
			return -1;
	}
	else if(m == 18){ /* 17 meters */
		dflmd = REM_MODE_USB;
		if((d < 6800) || (d > 16800))
			return -1;
	}
	else if(m == 21){ /* 15 meters */
		dflmd = REM_MODE_USB;
		if((d < 20000) || (d > 45000))
			return -1;
	}
	else if(m == 24){ /* 12 meters */
		dflmd = REM_MODE_USB;
		if((d < 89000) || (d > 99000))
			return -1;
	}
	else if(m == 28){ /* 10 meters */
		dflmd = REM_MODE_USB;
	}
	else if(m == 29){ 
		if(d >= 51000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
		if(d > 70000)
			return -1;
	}
	else if(m == 50){ /* 6 meters */
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;

	}
	else if((m >= 51) && ( m < 54)){
		dflmd = REM_MODE_FM;
	}
	else if(m == 144){ /* 2 meters */
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
	}
	else if((m >= 145) && (m < 148)){
		dflmd = REM_MODE_FM;
	}
	else if((m >= 430) && (m < 450)){ /* 70 centimeters */
		if(m  < 438)
			dflmd = REM_MODE_USB;
		else
			dflmd = REM_MODE_FM;
		;
	}
	else
		return -1;

	if(defmode)
		*defmode = dflmd;

	return 0;
}

/*
* Set a new frequency for the ft100
*/

static int set_freq_ft100(struct rpt *myrpt, char *newfreq)
{
	unsigned char cmdstr[5];
	int m,d;
	char mhz[MAXREMSTR];
	char decimals[MAXREMSTR];

	if(debug) 
		printf("New frequency: %s\n",newfreq);

	if(split_freq(mhz, decimals, newfreq))
		return -1; 

	m = atoi(mhz);
	d = atoi(decimals);

	/* The FT-100 likes packed BCD frequencies */

	cmdstr[0] = (((d % 100)/10) << 4) + (d % 10);			/* 100Hz 10Hz */
	cmdstr[1] = (((d % 10000)/1000) << 4) + ((d % 1000)/ 100);	/* 10KHz 1KHz */
	cmdstr[2] = ((m % 10) << 4) + (d / 10000);			/* 1MHz 100KHz */
	cmdstr[3] = ((m / 100) << 4) + ((m % 100)/10);			/* 100MHz 10Mhz */
	cmdstr[4] = 0x0a;						/* command */

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);

}

/* ft-897 simple commands */

static int simple_command_ft100(struct rpt *myrpt, unsigned char command, unsigned char p1)
{
	unsigned char cmdstr[5];
	
	memset(cmdstr, 0, 5);
	cmdstr[3] = p1;
	cmdstr[4] = command;	


	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);

}

/* ft-897 offset */

static int set_offset_ft100(struct rpt *myrpt, char offset)
{
	unsigned char p1;

	switch(offset){
		case	REM_SIMPLEX:
			p1 = 0;
			break;

		case	REM_MINUS:
			p1 = 1;
			break;
		
		case	REM_PLUS:
			p1 = 2;
			break;	

		default:
			return -1;
	}

	return simple_command_ft100(myrpt,0x84,p1);
}

/* ft-897 mode */

static int set_mode_ft100(struct rpt *myrpt, char newmode)
{
	unsigned char p1;
	
	switch(newmode){
		case	REM_MODE_FM:
			p1 = 6;
			break;

		case	REM_MODE_USB:
			p1 = 1;
			break;

		case	REM_MODE_LSB:
			p1 = 0;
			break;

		case	REM_MODE_AM:
			p1 = 4;
			break;
		
		default:
			return -1;
	}
	return simple_command_ft100(myrpt,0x0c,p1);
}

/* Set tone encode and decode modes */

static int set_ctcss_mode_ft100(struct rpt *myrpt, char txplon, char rxplon)
{
	unsigned char p1;
	
	if(rxplon)
		p1 = 2; /* Encode and Decode */
	else if (!rxplon && txplon)
		p1 = 1; /* Encode only */
	else
		p1 = 0; /* OFF */

	return simple_command_ft100(myrpt,0x92,p1);
}


/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_ft100(struct rpt *myrpt, char *txtone, char *rxtone)
{
	unsigned char p1;

	p1 = ft100_pltocode(rxtone);
	return simple_command_ft100(myrpt,0x90,p1);
}	

static int set_ft100(struct rpt *myrpt)
{
	int res;
	

	if(debug > 2)
		printf("Modulation mode\n");
	res = set_mode_ft100(myrpt, myrpt->remmode);		/* Modulation mode */

	if(debug > 2)
		printf("Split off\n");
	if(!res){
		simple_command_ft100(myrpt, 0x01,0);			/* Split off */
	}

	if(debug > 2)
		printf("Frequency\n");
	if(!res){
		res = set_freq_ft100(myrpt, myrpt->freq);		/* Frequency */
		usleep(FT100_SERIAL_DELAY*2);
	}
	if((myrpt->remmode == REM_MODE_FM)){
		if(debug > 2)
			printf("Offset\n");
		if(!res){
			res = set_offset_ft100(myrpt, myrpt->offset);	/* Offset if FM */
			usleep(FT100_SERIAL_DELAY);
		}
		if((!res)&&(myrpt->rxplon || myrpt->txplon)){
			usleep(FT100_SERIAL_DELAY);
			if(debug > 2)
				printf("CTCSS tone freqs.\n");
			res = set_ctcss_freq_ft100(myrpt, myrpt->txpl, myrpt->rxpl); /* CTCSS freqs if CTCSS is enabled */
			usleep(FT100_SERIAL_DELAY);
		}
		if(!res){
			if(debug > 2)
				printf("CTCSS mode\n");
			res = set_ctcss_mode_ft100(myrpt, myrpt->txplon, myrpt->rxplon); /* CTCSS mode */
			usleep(FT100_SERIAL_DELAY);
		}
	}
	return res;
}

static int closerem_ft100(struct rpt *myrpt)
{
	simple_command_ft100(myrpt, 0x0f,0); /* PTT off */
	return 0;
}	

/*
* Bump frequency up or down by a small amount 
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz 
*/

static int multimode_bump_freq_ft100(struct rpt *myrpt, int interval)
{
	int m,d;
	char mhz[MAXREMSTR], decimals[MAXREMSTR];

	if(debug)
		printf("Before bump: %s\n", myrpt->freq);

	if(split_freq(mhz, decimals, myrpt->freq))
		return -1;
	
	m = atoi(mhz);
	d = atoi(decimals);

	d += (interval / 10); /* 10Hz resolution */
	if(d < 0){
		m--;
		d += 100000;
	}
	else if(d >= 100000){
		m++;
		d -= 100000;
	}

	if(check_freq_ft100(m, d, NULL)){
		if(debug)
			printf("Bump freq invalid\n");
		return -1;
	}

	snprintf(myrpt->freq, MAXREMSTR, "%d.%05d", m, d);

	if(debug)
		printf("After bump: %s\n", myrpt->freq);

	return set_freq_ft100(myrpt, myrpt->freq);	
}



/*
* FT-950 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


static int check_freq_ft950(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if(m == 1){ /* 160 meters */
		dflmd =	REM_MODE_LSB; 
		if(d < 80000)
			return -1;
	}
	else if(m == 3){ /* 80 meters */
		dflmd = REM_MODE_LSB;
		if(d < 50000)
			return -1;
	}
	else if(m == 7){ /* 40 meters */
		dflmd = REM_MODE_LSB;
		if(d > 30000)
			return -1;
	}
	else if(m == 14){ /* 20 meters */
		dflmd = REM_MODE_USB;
		if(d > 35000)
			return -1;
	}
	else if(m == 18){ /* 17 meters */
		dflmd = REM_MODE_USB;
		if((d < 6800) || (d > 16800))
			return -1;
	}
	else if(m == 21){ /* 15 meters */
		dflmd = REM_MODE_USB;
		if((d < 20000) || (d > 45000))
			return -1;
	}
	else if(m == 24){ /* 12 meters */
		dflmd = REM_MODE_USB;
		if((d < 89000) || (d > 99000))
			return -1;
	}
	else if(m == 28){ /* 10 meters */
		dflmd = REM_MODE_USB;
	}
	else if(m == 29){ 
		if(d >= 51000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
		if(d > 70000)
			return -1;
	}
	else if(m == 50){ /* 6 meters */
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;

	}
	else if((m >= 51) && ( m < 54)){
		dflmd = REM_MODE_FM;
	}
	else
		return -1;

	if(defmode)
		*defmode = dflmd;

	return 0;
}

/*
* Set a new frequency for the ft950
*/

static int set_freq_ft950(struct rpt *myrpt, char *newfreq)
{
	char cmdstr[20];
	int m,d;
	char mhz[MAXREMSTR];
	char decimals[MAXREMSTR];

	if(debug) 
		printf("New frequency: %s\n",newfreq);

	if(split_freq(mhz, decimals, newfreq))
		return -1; 

	m = atoi(mhz);
	d = atoi(decimals);


	sprintf(cmdstr,"FA%d%06d;",m,d * 10);
	return serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0);

}

/* ft-950 offset */

static int set_offset_ft950(struct rpt *myrpt, char offset)
{
	char *cmdstr;
	
	switch(offset){
		case	REM_SIMPLEX:
			cmdstr = "OS00;";
			break;

		case	REM_MINUS:
			cmdstr = "OS02;";
			break;
		
		case	REM_PLUS:
			cmdstr = "OS01;";
			break;	

		default:
			return -1;
	}

	return serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0);
}

/* ft-950 mode */

static int set_mode_ft950(struct rpt *myrpt, char newmode)
{
	char *cmdstr;
	
	switch(newmode){
		case	REM_MODE_FM:
			cmdstr = "MD04;";
			break;

		case	REM_MODE_USB:
			cmdstr = "MD02;";
			break;

		case	REM_MODE_LSB:
			cmdstr = "MD01;";
			break;

		case	REM_MODE_AM:
			cmdstr = "MD05;";
			break;
		
		default:
			return -1;
	}

	return serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0);
}

/* Set tone encode and decode modes */

static int set_ctcss_mode_ft950(struct rpt *myrpt, char txplon, char rxplon)
{
	char *cmdstr;
	
	
	if(rxplon && txplon)
		cmdstr = "CT01;";
	else if (!rxplon && txplon)
		cmdstr = "CT02;"; /* Encode only */
	else if (rxplon && !txplon)
		cmdstr = "CT02;"; /* Encode only */
	else
		cmdstr = "CT00;"; /* OFF */

	return serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0);
}


/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_ft950(struct rpt *myrpt, char *txtone, char *rxtone)
{
	char cmdstr[16];
	int c;

	c = ft950_pltocode(txtone);
	if (c < 0) return(-1);
	
	sprintf(cmdstr,"CN0%02d;",c);

	return serial_remote_io(myrpt, (unsigned char *)cmdstr, 5, NULL, 0, 0);
}	



static int set_ft950(struct rpt *myrpt)
{
	int res;
	char *cmdstr;
	
	if(debug)
		printf("ptt off\n");

	cmdstr = "MX0;";
	res = serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0); /* MOX off */

	if(debug)
		printf("select ant. 1\n");

	cmdstr = "AN01;";
	res = serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0); /* MOX off */

	if(debug)
		printf("Modulation mode\n");

	if(!res)
		res = set_mode_ft950(myrpt, myrpt->remmode);		/* Modulation mode */

	if(debug)
		printf("Split off\n");

	cmdstr = "OS00;";
	if(!res)
		res = serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0); /* Split off */

	if(debug)
		printf("VFO Modes\n");

	if (!res) 
		res = serial_remote_io(myrpt, (unsigned char *)"FR0;", 4, NULL, 0, 0);
	if (!res) 
		res = serial_remote_io(myrpt, (unsigned char *)"FT2;", 4, NULL, 0, 0);

	if(debug)
		printf("Frequency\n");

	if(!res)
		res = set_freq_ft950(myrpt, myrpt->freq);		/* Frequency */
	if((myrpt->remmode == REM_MODE_FM)){
		if(debug)
			printf("Offset\n");
		if(!res)
			res = set_offset_ft950(myrpt, myrpt->offset);	/* Offset if FM */
		if((!res)&&(myrpt->rxplon || myrpt->txplon)){
			if(debug)
				printf("CTCSS tone freqs.\n");
			res = set_ctcss_freq_ft950(myrpt, myrpt->txpl, myrpt->rxpl); /* CTCSS freqs if CTCSS is enabled */
		}
		if(!res){
			if(debug)
				printf("CTCSS mode\n");
			res = set_ctcss_mode_ft950(myrpt, myrpt->txplon, myrpt->rxplon); /* CTCSS mode */
		}
	}
	if((myrpt->remmode == REM_MODE_USB)||(myrpt->remmode == REM_MODE_LSB)){
		if(debug)
			printf("Clarifier off\n");
		cmdstr = "RT0;";
		serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0); /* Clarifier off if LSB or USB */
	}
	return res;
}

/*
* Bump frequency up or down by a small amount 
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz 
*/

static int multimode_bump_freq_ft950(struct rpt *myrpt, int interval)
{
	int m,d;
	char mhz[MAXREMSTR], decimals[MAXREMSTR];

	if(debug)
		printf("Before bump: %s\n", myrpt->freq);

	if(split_freq(mhz, decimals, myrpt->freq))
		return -1;
	
	m = atoi(mhz);
	d = atoi(decimals);

	d += (interval / 10); /* 10Hz resolution */
	if(d < 0){
		m--;
		d += 100000;
	}
	else if(d >= 100000){
		m++;
		d -= 100000;
	}

	if(check_freq_ft950(m, d, NULL)){
		if(debug)
			printf("Bump freq invalid\n");
		return -1;
	}

	snprintf(myrpt->freq, MAXREMSTR, "%d.%05d", m, d);

	if(debug)
		printf("After bump: %s\n", myrpt->freq);

	return set_freq_ft950(myrpt, myrpt->freq);	
}



/*
* IC-706 I/O handlers
*/

/* Check to see that the frequency is valid */
/* returns 0 if frequency is valid          */

static int check_freq_ic706(int m, int d, int *defmode, char mars)
{
	int dflmd = REM_MODE_FM;
	int rv=0;

	if(debug > 6)
		ast_log(LOG_NOTICE,"(%i,%i,%i,%i)\n",m,d,*defmode,mars);

	/* first test for standard amateur radio bands */

	if(m == 1){ 					/* 160 meters */
		dflmd =	REM_MODE_LSB; 
		if(d < 80000)rv=-1;
	}
	else if(m == 3){ 				/* 80 meters */
		dflmd = REM_MODE_LSB;
		if(d < 50000)rv=-1;
	}
	else if(m == 7){ 				/* 40 meters */
		dflmd = REM_MODE_LSB;
		if(d > 30000)rv=-1;
	}
	else if(m == 14){ 				/* 20 meters */
		dflmd = REM_MODE_USB;
		if(d > 35000)rv=-1;
	}
	else if(m == 18){ 							/* 17 meters */
		dflmd = REM_MODE_USB;
		if((d < 6800) || (d > 16800))rv=-1;
	}
	else if(m == 21){ /* 15 meters */
		dflmd = REM_MODE_USB;
		if((d < 20000) || (d > 45000))rv=-1;
	}
	else if(m == 24){ /* 12 meters */
		dflmd = REM_MODE_USB;
		if((d < 89000) || (d > 99000))rv=-1;
	}
	else if(m == 28){ 							/* 10 meters */
		dflmd = REM_MODE_USB;
	}
	else if(m == 29){ 
		if(d >= 51000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
		if(d > 70000)rv=-1;
	}
	else if(m == 50){ 							/* 6 meters */
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
	}
	else if((m >= 51) && ( m < 54)){
		dflmd = REM_MODE_FM;
	}
	else if(m == 144){ /* 2 meters */
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
	}
	else if((m >= 145) && (m < 148)){
		dflmd = REM_MODE_FM;
	}
	else if((m >= 430) && (m < 450)){ 			/* 70 centimeters */
		if(m  < 438)
			dflmd = REM_MODE_USB;
		else
			dflmd = REM_MODE_FM;
	}

	/* check expanded coverage */
	if(mars && rv<0){
		if((m >= 450) && (m < 470)){ 			/* LMR */
			dflmd = REM_MODE_FM;
			rv=0;
		}
		else if((m >= 148) && (m < 174)){ 		/* LMR */
			dflmd = REM_MODE_FM;
			rv=0;
		}
		else if((m >= 138) && (m < 144)){ 		/* VHF-AM AIRCRAFT */
			dflmd = REM_MODE_AM;
			rv=0;
		}
		else if((m >= 108) && (m < 138)){ 		/* VHF-AM AIRCRAFT */
			dflmd = REM_MODE_AM;
			rv=0;
		}
		else if( (m==0 && d>=55000) || (m==1 && d<=75000) ){ 	/* AM BCB*/
			dflmd = REM_MODE_AM;
			rv=0;
		}
  		else if( (m == 1 && d>75000) || (m>1 && m<30) ){ 		/* HF SWL*/
			dflmd = REM_MODE_AM;
			rv=0;
		}
	}

	if(defmode)
		*defmode = dflmd;

	if(debug > 1)
		ast_log(LOG_NOTICE,"(%i,%i,%i,%i) returning %i\n",m,d,*defmode,mars,rv);

	return rv;
}

/* take a PL frequency and turn it into a code */
static int ic706_pltocode(char *str)
{
	int i;
	char *s;
	int rv=-1;

	s = strchr(str,'.');
	i = 0;
	if (s) i = atoi(s + 1);
	i += atoi(str) * 10;
	switch(i)
	{
	    case 670:
			rv=0;
			break;
	    case 693:
			rv=1;
			break;
	    case 719:
			rv=2;
			break;
	    case 744:
			rv=3;
			break;
	    case 770:
			rv=4;
			break;
	    case 797:
			rv=5;
			break;
	    case 825:
			rv=6;
			break;
	    case 854:
			rv=7;
			break;
	    case 885:
			rv=8;
			break;
	    case 915:
			rv=9;
			break;
	    case 948:
			rv=10;
			break;
	    case 974:
			rv=11;
			break;
	    case 1000:
			rv=12;
			break;
	    case 1035:
			rv=13;
			break;
	    case 1072:
			rv=14;
			break;
	    case 1109:
			rv=15;
			break;
	    case 1148:
			rv=16;
			break;
	    case 1188:
			rv=17;
			break;
	    case 1230:
			rv=18;
			break;
	    case 1273:
			rv=19;
			break;
	    case 1318:
			rv=20;
			break;
	    case 1365:
			rv=21;
			break;
	    case 1413:
			rv=22;
			break;
	    case 1462:
			rv=23;
			break;
	    case 1514:
			rv=24;
			break;
	    case 1567:
			rv=25;
			break;
	    case 1598:
			rv=26;
			break;
	    case 1622:
			rv=27;
			break;
	    case 1655:
			rv=28;		
			break;
	    case 1679:
			rv=29;
			break;
	    case 1713:
			rv=30;
			break;
	    case 1738:
			rv=31;
			break;
	    case 1773:
			rv=32;
			break;
	    case 1799:
			rv=33;
			break;
	    case 1835:
			rv=34;
			break;
	    case 1862:
			rv=35;
			break;
	    case 1899:
			rv=36;
			break;
	    case 1928:
			rv=37;
			break;
	    case 1966:
			rv=38;
			break;
	    case 1995:
			rv=39;
			break;
	    case 2035:
			rv=40;
			break;
	    case 2065:
			rv=41;
			break;
	    case 2107:
			rv=42;
			break;
	    case 2181:
			rv=43;
			break;
	    case 2257:
			rv=44;
			break;
	    case 2291:
			rv=45;
			break;
	    case 2336:
			rv=46;
			break;
	    case 2418:
			rv=47;
			break;
	    case 2503:
			rv=48;
			break;
	    case 2541:
			rv=49;
			break;
	}
	if(debug > 1)
		ast_log(LOG_NOTICE,"%i  rv=%i\n",i, rv);

	return rv;
}

/* ic-706 simple commands */

static int simple_command_ic706(struct rpt *myrpt, char command, char subcommand)
{
	unsigned char cmdstr[10];
	
	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = command;
	cmdstr[5] = subcommand;
	cmdstr[6] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,7));
}

/*
* Set a new frequency for the ic706
*/

static int set_freq_ic706(struct rpt *myrpt, char *newfreq)
{
	unsigned char cmdstr[20];
	char mhz[MAXREMSTR], decimals[MAXREMSTR];
	int m,d;

	if(debug) 
		ast_log(LOG_NOTICE,"newfreq:%s\n",newfreq); 			

	if(split_freq(mhz, decimals, newfreq))
		return -1; 

	m = atoi(mhz);
	d = atoi(decimals);

	/* The ic-706 likes packed BCD frequencies */

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 5;
	cmdstr[5] = ((d % 10) << 4);
	cmdstr[6] = (((d % 1000)/ 100) << 4) + ((d % 100)/10);
	cmdstr[7] = ((d / 10000) << 4) + ((d % 10000)/1000);
	cmdstr[8] = (((m % 100)/10) << 4) + (m % 10);
	cmdstr[9] = (m / 100);
	cmdstr[10] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,11));
}

/* ic-706 offset */

static int set_offset_ic706(struct rpt *myrpt, char offset)
{
	unsigned char c;
	int mysplit,res;
	char mhz[MAXREMSTR],decimal[MAXREMSTR];
	unsigned char cmdstr[10];

	if(split_freq(mhz, decimal, myrpt->freq))
		return -1; 

	mysplit = myrpt->splitkhz * 10;
	if (!mysplit)
	{
		if (atoi(mhz) > 400) 
			mysplit = myrpt->p.default_split_70cm * 10;
		else 
			mysplit = myrpt->p.default_split_2m * 10;
	}

	if(debug > 6)
		ast_log(LOG_NOTICE,"split=%i\n",mysplit * 100);

	/* The ic-706 likes packed BCD data */

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x0d;
	cmdstr[5] = ((mysplit % 10) << 4) + ((mysplit % 100) / 10);
	cmdstr[6] = (((mysplit % 10000) / 1000) << 4) + ((mysplit % 1000) / 100);
	cmdstr[7] = ((mysplit / 100000) << 4) + ((mysplit % 100000) / 10000);
	cmdstr[8] = 0xfd;

	res = civ_cmd(myrpt,cmdstr,9);
	if (res) return res;

	if(debug > 6)
		ast_log(LOG_NOTICE,"offset=%i\n",offset);

	switch(offset){
		case	REM_SIMPLEX:
			c = 0x10;
			break;

		case	REM_MINUS:
			c = 0x11;
			break;
		
		case	REM_PLUS:
			c = 0x12;
			break;	

		default:
			return -1;
	}

	return simple_command_ic706(myrpt,0x0f,c);

}

/* ic-706 mode */

static int set_mode_ic706(struct rpt *myrpt, char newmode)
{
	unsigned char c;
	
	if(debug > 6)
		ast_log(LOG_NOTICE,"newmode=%i\n",newmode);

	switch(newmode){
		case	REM_MODE_FM:
			c = 5;
			break;

		case	REM_MODE_USB:
			c = 1;
			break;

		case	REM_MODE_LSB:
			c = 0;
			break;

		case	REM_MODE_AM:
			c = 2;
			break;
		
		default:
			return -1;
	}
	return simple_command_ic706(myrpt,6,c);
}

/* Set tone encode and decode modes */

static int set_ctcss_mode_ic706(struct rpt *myrpt, char txplon, char rxplon)
{
	unsigned char cmdstr[10];
	int rv;

	if(debug > 6)
		ast_log(LOG_NOTICE,"txplon=%i  rxplon=%i \n",txplon,rxplon);

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x16;
	cmdstr[5] = 0x42;
	cmdstr[6] = (txplon != 0);
	cmdstr[7] = 0xfd;

	rv = civ_cmd(myrpt,cmdstr,8);
	if (rv) return(-1);

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x16;
	cmdstr[5] = 0x43;
	cmdstr[6] = (rxplon != 0);
	cmdstr[7] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,8));
}

#if 0
/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_ic706(struct rpt *myrpt, char *txtone, char *rxtone)
{
	unsigned char cmdstr[10];
	char hertz[MAXREMSTR],decimal[MAXREMSTR];
	int h,d,rv;

	memset(cmdstr, 0, 5);

	if(debug > 6)
		ast_log(LOG_NOTICE,"txtone=%s  rxtone=%s \n",txtone,rxtone);

	if(split_ctcss_freq(hertz, decimal, txtone))
		return -1; 

	h = atoi(hertz);
	d = atoi(decimal);
	
	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x1b;
	cmdstr[5] = 0;
	cmdstr[6] = ((h / 100) << 4) + (h % 100)/ 10;
	cmdstr[7] = ((h % 10) << 4) + (d % 10);
	cmdstr[8] = 0xfd;

	rv = civ_cmd(myrpt,cmdstr,9);
	if (rv) return(-1);

	if (!rxtone) return(0);

	if(split_ctcss_freq(hertz, decimal, rxtone))
		return -1; 

	h = atoi(hertz);
	d = atoi(decimal);

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x1b;
	cmdstr[5] = 1;
	cmdstr[6] = ((h / 100) << 4) + (h % 100)/ 10;
	cmdstr[7] = ((h % 10) << 4) + (d % 10);
	cmdstr[8] = 0xfd;
	return(civ_cmd(myrpt,cmdstr,9));
}	
#endif

static int vfo_ic706(struct rpt *myrpt)
{
	unsigned char cmdstr[10];
	
	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 7;
	cmdstr[5] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,6));
}

static int mem2vfo_ic706(struct rpt *myrpt)
{
	unsigned char cmdstr[10];
	
	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x0a;
	cmdstr[5] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,6));
}

static int select_mem_ic706(struct rpt *myrpt, int slot)
{
	unsigned char cmdstr[10];
	
	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 8;
	cmdstr[5] = 0;
	cmdstr[6] = ((slot / 10) << 4) + (slot % 10);
	cmdstr[7] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,8));
}

static int set_ic706(struct rpt *myrpt)
{
	int res = 0,i;
	
	if(debug)ast_log(LOG_NOTICE, "Set to VFO A iobase=%i\n",myrpt->p.iobase);

	if (!res)
		res = simple_command_ic706(myrpt,7,0);

	if((myrpt->remmode == REM_MODE_FM))
	{
		i = ic706_pltocode(myrpt->rxpl);
		if (i == -1) return -1;
		if(debug)
			printf("Select memory number\n");
		if (!res)
			res = select_mem_ic706(myrpt,i + IC706_PL_MEMORY_OFFSET);
		if(debug)
			printf("Transfer memory to VFO\n");
		if (!res)
			res = mem2vfo_ic706(myrpt);
	}
		
	if(debug)
		printf("Set to VFO\n");

	if (!res)
		res = vfo_ic706(myrpt);

	if(debug)
		printf("Modulation mode\n");

	if (!res)
		res = set_mode_ic706(myrpt, myrpt->remmode);		/* Modulation mode */

	if(debug)
		printf("Split off\n");

	if(!res)
		simple_command_ic706(myrpt, 0x82,0);			/* Split off */

	if(debug)
		printf("Frequency\n");

	if(!res)
		res = set_freq_ic706(myrpt, myrpt->freq);		/* Frequency */
	if((myrpt->remmode == REM_MODE_FM)){
		if(debug)
			printf("Offset\n");
		if(!res)
			res = set_offset_ic706(myrpt, myrpt->offset);	/* Offset if FM */
		if(!res){
			if(debug)
				printf("CTCSS mode\n");
			res = set_ctcss_mode_ic706(myrpt, myrpt->txplon, myrpt->rxplon); /* CTCSS mode */
		}
	}
	return res;
}

/*
* Bump frequency up or down by a small amount 
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz 
*/

static int multimode_bump_freq_ic706(struct rpt *myrpt, int interval)
{
	int m,d;
	char mhz[MAXREMSTR], decimals[MAXREMSTR];
	unsigned char cmdstr[20];

	if(debug)
		printf("Before bump: %s\n", myrpt->freq);

	if(split_freq(mhz, decimals, myrpt->freq))
		return -1;
	
	m = atoi(mhz);
	d = atoi(decimals);

	d += (interval / 10); /* 10Hz resolution */
	if(d < 0){
		m--;
		d += 100000;
	}
	else if(d >= 100000){
		m++;
		d -= 100000;
	}

	if(check_freq_ic706(m, d, NULL,myrpt->p.remote_mars)){
		if(debug)
			printf("Bump freq invalid\n");
		return -1;
	}

	snprintf(myrpt->freq, MAXREMSTR, "%d.%05d", m, d);

	if(debug)
		printf("After bump: %s\n", myrpt->freq);

	/* The ic-706 likes packed BCD frequencies */

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0;
	cmdstr[5] = ((d % 10) << 4);
	cmdstr[6] = (((d % 1000)/ 100) << 4) + ((d % 100)/10);
	cmdstr[7] = ((d / 10000) << 4) + ((d % 10000)/1000);
	cmdstr[8] = (((m % 100)/10) << 4) + (m % 10);
	cmdstr[9] = (m / 100);
	cmdstr[10] = 0xfd;

	return(serial_remote_io(myrpt,cmdstr,11,NULL,0,0));
}

/*
* XCAT I/O handlers
*/

/* Check to see that the frequency is valid */
/* returns 0 if frequency is valid          */


static int check_freq_xcat(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if (m == 144){ /* 2 meters */
		if(d < 10100)
			return -1;
	}
	if (m == 29){ /* 10 meters */
		if(d > 70000)
			return -1;
	}
	else if((m >= 28) && (m < 30)){
		;
	}
	else if((m >= 50) && (m < 54)){
		;
	}
	else if((m >= 144) && (m < 148)){
		;
	}
	else if((m >= 420) && (m < 450)){ /* 70 centimeters */
		;
	}
	else
		return -1;
	
	if(defmode)
		*defmode = dflmd;	


	return 0;
}

static int simple_command_xcat(struct rpt *myrpt, char command, char subcommand)
{
	unsigned char cmdstr[10];
	
	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = command;
	cmdstr[5] = subcommand;
	cmdstr[6] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,7));
}

/*
* Set a new frequency for the xcat
*/

static int set_freq_xcat(struct rpt *myrpt, char *newfreq)
{
	unsigned char cmdstr[20];
	char mhz[MAXREMSTR], decimals[MAXREMSTR];
	int m,d;

	if(debug) 
		ast_log(LOG_NOTICE,"newfreq:%s\n",newfreq); 			

	if(split_freq(mhz, decimals, newfreq))
		return -1; 

	m = atoi(mhz);
	d = atoi(decimals);

	/* The ic-706 likes packed BCD frequencies */

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 5;
	cmdstr[5] = ((d % 10) << 4);
	cmdstr[6] = (((d % 1000)/ 100) << 4) + ((d % 100)/10);
	cmdstr[7] = ((d / 10000) << 4) + ((d % 10000)/1000);
	cmdstr[8] = (((m % 100)/10) << 4) + (m % 10);
	cmdstr[9] = (m / 100);
	cmdstr[10] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,11));
}

static int set_offset_xcat(struct rpt *myrpt, char offset)
{
	unsigned char c,cmdstr[20];
        int mysplit;
        char mhz[MAXREMSTR],decimal[MAXREMSTR];

        if(split_freq(mhz, decimal, myrpt->freq))
                return -1;

        mysplit = myrpt->splitkhz * 1000;
        if (!mysplit)
        {
                if (atoi(mhz) > 400)
                        mysplit = myrpt->p.default_split_70cm * 1000;
                else
                        mysplit = myrpt->p.default_split_2m * 1000;
        }

        cmdstr[0] = cmdstr[1] = 0xfe;
        cmdstr[2] = myrpt->p.civaddr;
        cmdstr[3] = 0xe0;
        cmdstr[4] = 0xaa;
        cmdstr[5] = 0x06;
        cmdstr[6] = mysplit & 0xff;
        cmdstr[7] = (mysplit >> 8) & 0xff;
        cmdstr[8] = (mysplit >> 16) & 0xff;
        cmdstr[9] = (mysplit >> 24) & 0xff;
        cmdstr[10] = 0xfd;

        if (civ_cmd(myrpt,cmdstr,11) < 0) return -1;

	switch(offset){
		case	REM_SIMPLEX:
			c = 0x10;
			break;

		case	REM_MINUS:
			c = 0x11;
			break;
		
		case	REM_PLUS:
			c = 0x12;
			break;	

		default:
			return -1;
	}

	return simple_command_xcat(myrpt,0x0f,c);

}

/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_xcat(struct rpt *myrpt, char *txtone, char *rxtone)
{
	unsigned char cmdstr[10];
	char hertz[MAXREMSTR],decimal[MAXREMSTR];
	int h,d,rv;

	memset(cmdstr, 0, 5);

	if(debug > 6)
		ast_log(LOG_NOTICE,"txtone=%s  rxtone=%s \n",txtone,rxtone);

	if(split_ctcss_freq(hertz, decimal, txtone))
		return -1; 

	h = atoi(hertz);
	d = atoi(decimal);
	
	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x1b;
	cmdstr[5] = 0;
	cmdstr[6] = ((h / 100) << 4) + (h % 100)/ 10;
	cmdstr[7] = ((h % 10) << 4) + (d % 10);
	cmdstr[8] = 0xfd;

	rv = civ_cmd(myrpt,cmdstr,9);
	if (rv) return(-1);

	if (!rxtone) return(0);

	if(split_ctcss_freq(hertz, decimal, rxtone))
		return -1; 

	h = atoi(hertz);
	d = atoi(decimal);

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x1b;
	cmdstr[5] = 1;
	cmdstr[6] = ((h / 100) << 4) + (h % 100)/ 10;
	cmdstr[7] = ((h % 10) << 4) + (d % 10);
	cmdstr[8] = 0xfd;
	return(civ_cmd(myrpt,cmdstr,9));
}	

static int set_xcat(struct rpt *myrpt)
{
	int res = 0;
	
	/* set Mode */
	if(debug)
		printf("Mode\n");
	if (!res)
		res = simple_command_xcat(myrpt,8,1);
        if(debug)
                printf("Offset Initial/Simplex\n");
        if(!res)
                res = set_offset_xcat(myrpt, REM_SIMPLEX);      /* Offset */
	/* set Freq */
	if(debug)
		printf("Frequency\n");
	if(!res)
		res = set_freq_xcat(myrpt, myrpt->freq);		/* Frequency */
	if(debug)
		printf("Offset\n");
	if(!res)
		res = set_offset_xcat(myrpt, myrpt->offset);	/* Offset */
	if(debug)
		printf("CTCSS\n");
	if (!res)
		res = set_ctcss_freq_xcat(myrpt, myrpt->txplon ? myrpt->txpl : "0.0", 
			myrpt->rxplon ? myrpt->rxpl : "0.0"); /* Tx/Rx CTCSS */
	/* set Freq */
	if(debug)
		printf("Frequency\n");
	if(!res)
		res = set_freq_xcat(myrpt, myrpt->freq);		/* Frequency */
	return res;
}


/*
* Dispatch to correct I/O handler 
*/
static int setrem(struct rpt *myrpt)
{
char	str[300];
char	*offsets[] = {"SIMPLEX","MINUS","PLUS"};
char	*powerlevels[] = {"LOW","MEDIUM","HIGH"};
char	*modes[] = {"FM","USB","LSB","AM"};
int	i,res = -1;

#if	0
printf("FREQ,%s,%s,%s,%s,%s,%s,%d,%d\n",myrpt->freq,
	modes[(int)myrpt->remmode],
	myrpt->txpl,myrpt->rxpl,offsets[(int)myrpt->offset],
	powerlevels[(int)myrpt->powerlevel],myrpt->txplon,
	myrpt->rxplon);
#endif
	if (myrpt->p.archivedir)
	{
		sprintf(str,"FREQ,%s,%s,%s,%s,%s,%s,%d,%d",myrpt->freq,
			modes[(int)myrpt->remmode],
			myrpt->txpl,myrpt->rxpl,offsets[(int)myrpt->offset],
			powerlevels[(int)myrpt->powerlevel],myrpt->txplon,
			myrpt->rxplon);
		donodelog(myrpt,str);
	}
	if (myrpt->remote && myrpt->remote_webtransceiver) 
	{
		if (myrpt->remmode == REM_MODE_FM)
		{
			char myfreq[MAXREMSTR],*cp;
			strcpy(myfreq,myrpt->freq);
			cp = strchr(myfreq,'.');
			for(i = strlen(myfreq) - 1; i; i--)
			{
				if (myfreq[i] != '0') break;
				myfreq[i] = 0;
			}
			if (myfreq[0] && (myfreq[strlen(myfreq) - 1] == '.')) strcat(myfreq,"0");
			sprintf(str,"J Remote Frequency\n%s FM\n%s Offset\n",
				(cp) ? myfreq : myrpt->freq,offsets[(int)myrpt->offset]);
			sprintf(str + strlen(str),"%s Power\nTX PL %s\nRX PL %s\n",
				powerlevels[(int)myrpt->powerlevel],
				(myrpt->txplon) ? myrpt->txpl : "Off",
				(myrpt->rxplon) ? myrpt->rxpl : "Off");
		}
		else
		{
			sprintf(str,"J Remote Frequency %s %s\n%s Power\n",
				myrpt->freq,modes[(int)myrpt->remmode],
				powerlevels[(int)myrpt->powerlevel]);
		}
		ast_sendtext(myrpt->remote_webtransceiver,str);
	}
	if(!strcmp(myrpt->remoterig, remote_rig_ft897))
	{
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	if(!strcmp(myrpt->remoterig, remote_rig_ft100))
	{
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	if(!strcmp(myrpt->remoterig, remote_rig_ft950))
	{
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	if(!strcmp(myrpt->remoterig, remote_rig_ic706))
	{
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	if(!strcmp(myrpt->remoterig, remote_rig_xcat))
	{
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	if(!strcmp(myrpt->remoterig, remote_rig_tm271))
	{
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	if(!strcmp(myrpt->remoterig, remote_rig_tmd700))
	{
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	else if(!strcmp(myrpt->remoterig, remote_rig_rbi))
	{
		res = setrbi_check(myrpt);
		if (!res)
		{
			rpt_telemetry(myrpt,SETREMOTE,NULL);
			res = 0;
		}
	}
	else if(ISRIG_RTX(myrpt->remoterig))
	{
		setrtx(myrpt);
		res = 0;
	}
	else if(!strcmp(myrpt->remoterig, remote_rig_kenwood)) {
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	else
		res = 0;

	if (res < 0) ast_log(LOG_ERROR,"Unable to send remote command on node %s\n",myrpt->name);

	return res;
}

static int closerem(struct rpt *myrpt)
{
	if(!strcmp(myrpt->remoterig, remote_rig_ft897))
		return closerem_ft897(myrpt);
	else if(!strcmp(myrpt->remoterig, remote_rig_ft100))
		return closerem_ft100(myrpt);
	else
		return 0;
}

/*
* Dispatch to correct RX frequency checker
*/

static int check_freq(struct rpt *myrpt, int m, int d, int *defmode)
{
	if(!strcmp(myrpt->remoterig, remote_rig_ft897))
		return check_freq_ft897(m, d, defmode);
	else if(!strcmp(myrpt->remoterig, remote_rig_ft100))
		return check_freq_ft100(m, d, defmode);
	else if(!strcmp(myrpt->remoterig, remote_rig_ft950))
		return check_freq_ft950(m, d, defmode);
	else if(!strcmp(myrpt->remoterig, remote_rig_ic706))
		return check_freq_ic706(m, d, defmode,myrpt->p.remote_mars);
	else if(!strcmp(myrpt->remoterig, remote_rig_xcat))
		return check_freq_xcat(m, d, defmode);
	else if(!strcmp(myrpt->remoterig, remote_rig_rbi))
		return check_freq_rbi(m, d, defmode);
	else if(!strcmp(myrpt->remoterig, remote_rig_kenwood))
		return check_freq_kenwood(m, d, defmode);
	else if(!strcmp(myrpt->remoterig, remote_rig_tmd700))
		return check_freq_kenwood(m, d, defmode);
	else if(!strcmp(myrpt->remoterig, remote_rig_tm271))
		return check_freq_tm271(m, d, defmode);
	else if(ISRIG_RTX(myrpt->remoterig))
		return check_freq_rtx(m, d, defmode, myrpt);
	else
		return -1;
}

/*
 * Check TX frequency before transmitting
   rv=1 if tx frequency in ok.
*/

static char check_tx_freq(struct rpt *myrpt)
{
	int i,rv=0;
	int radio_mhz, radio_decimals, ulimit_mhz, ulimit_decimals, llimit_mhz, llimit_decimals;
	char radio_mhz_char[MAXREMSTR];
	char radio_decimals_char[MAXREMSTR];
	char limit_mhz_char[MAXREMSTR];
	char limit_decimals_char[MAXREMSTR];
	char limits[256];
	char *limit_ranges[40];
	struct ast_variable *limitlist;
	
	if(debug > 3){
		ast_log(LOG_NOTICE, "myrpt->freq = %s\n", myrpt->freq);
	}

	/* Must have user logged in and tx_limits defined */

	if(!myrpt->p.txlimitsstanzaname || !myrpt->loginuser[0] || !myrpt->loginlevel[0]){
		if(debug > 3){
			ast_log(LOG_NOTICE, "No tx band table defined, or no user logged in. rv=1\n");
		}
		rv=1;
		return 1; /* Assume it's ok otherwise */
	}

	/* Retrieve the band table for the loginlevel */
	limitlist = ast_variable_browse(myrpt->cfg, myrpt->p.txlimitsstanzaname);

	if(!limitlist){
		ast_log(LOG_WARNING, "No entries in %s band table stanza. rv=0\n", myrpt->p.txlimitsstanzaname);
		rv=0;
		return 0;
	}

	split_freq(radio_mhz_char, radio_decimals_char, myrpt->freq);
	radio_mhz = atoi(radio_mhz_char);
	radio_decimals = decimals2int(radio_decimals_char);

	if(debug > 3){
		ast_log(LOG_NOTICE, "Login User = %s, login level = %s\n", myrpt->loginuser, myrpt->loginlevel);
	}

	/* Find our entry */

	for(;limitlist; limitlist=limitlist->next){
		if(!strcmp(limitlist->name, myrpt->loginlevel))
			break;
	}

	if(!limitlist){
		ast_log(LOG_WARNING, "Can't find %s entry in band table stanza %s. rv=0\n", myrpt->loginlevel, myrpt->p.txlimitsstanzaname);
		rv=0;
	    return 0;
	}
	
	if(debug > 3){
		ast_log(LOG_NOTICE, "Auth: %s = %s\n", limitlist->name, limitlist->value);
	}

	/* Parse the limits */

	strncpy(limits, limitlist->value, 256);
	limits[255] = 0;
	finddelim(limits, limit_ranges, 40);
	for(i = 0; i < 40 && limit_ranges[i] ; i++){
		char range[40];
		char *r,*s;
		strncpy(range, limit_ranges[i], 40);
		range[39] = 0;
        	if(debug > 3) 
        		ast_log(LOG_NOTICE, "Check %s within %s\n", myrpt->freq, range);
	
		r = strchr(range, '-');
		if(!r){
			ast_log(LOG_WARNING, "Malformed range in %s tx band table entry. rv=0\n", limitlist->name);
			rv=0;
			break;
		}
		*r++ = 0;
		s = eatwhite(range);
		r = eatwhite(r);
		split_freq(limit_mhz_char, limit_decimals_char, s);
		llimit_mhz = atoi(limit_mhz_char);
		llimit_decimals = decimals2int(limit_decimals_char);
		split_freq(limit_mhz_char, limit_decimals_char, r);
		ulimit_mhz = atoi(limit_mhz_char);
		ulimit_decimals = decimals2int(limit_decimals_char);
			
		if((radio_mhz >= llimit_mhz) && (radio_mhz <= ulimit_mhz)){
			if(radio_mhz == llimit_mhz){ /* CASE 1: TX freq is in llimit mhz portion of band */
				if(radio_decimals >= llimit_decimals){ /* Cannot be below llimit decimals */
					if(llimit_mhz == ulimit_mhz){ /* If bandwidth < 1Mhz, check ulimit decimals */
						if(radio_decimals <= ulimit_decimals){
							rv=1;
							break;
						}
						else{
							if(debug > 3)
								ast_log(LOG_NOTICE, "Invalid TX frequency, debug msg 1\n");
							rv=0;
							break;
						}
					}
					else{
						rv=1;
						break;
					}
				}
				else{ /* Is below llimit decimals */
					if(debug > 3)
						ast_log(LOG_NOTICE, "Invalid TX frequency, debug msg 2\n");
					rv=0;
					break;
				}
			}
			else if(radio_mhz == ulimit_mhz){ /* CASE 2: TX freq not in llimit mhz portion of band */
				if(radio_decimals <= ulimit_decimals){
					if(debug > 3)
						ast_log(LOG_NOTICE, "radio_decimals <= ulimit_decimals\n");
					rv=1;
					break;
				}
				else{ /* Is above ulimit decimals */
					if(debug > 3)
						ast_log(LOG_NOTICE, "Invalid TX frequency, debug msg 3\n");
					rv=0;
					break;
				}
			}
			else /* CASE 3: TX freq within a multi-Mhz band and ok */
				if(debug > 3)
					ast_log(LOG_NOTICE, "Valid TX freq within a multi-Mhz band and ok.\n");
			rv=1;
			break;
		}
	}
	if(debug > 3)  
		ast_log(LOG_NOTICE, "rv=%i\n",rv);

	return rv;
}


/*
* Dispatch to correct frequency bumping function
*/

static int multimode_bump_freq(struct rpt *myrpt, int interval)
{
	if(!strcmp(myrpt->remoterig, remote_rig_ft897))
		return multimode_bump_freq_ft897(myrpt, interval);
	else if(!strcmp(myrpt->remoterig, remote_rig_ft950))
		return multimode_bump_freq_ft950(myrpt, interval);
	else if(!strcmp(myrpt->remoterig, remote_rig_ic706))
		return multimode_bump_freq_ic706(myrpt, interval);
	else if(!strcmp(myrpt->remoterig, remote_rig_ft100))
		return multimode_bump_freq_ft100(myrpt, interval);
	else
		return -1;
}


/*
* Queue announcment that scan has been stopped 
*/

static void stop_scan(struct rpt *myrpt)
{
	myrpt->hfscanstop = 1;
	rpt_telemetry(myrpt,SCAN,0);
}

/*
* This is called periodically when in scan mode
*/


static int service_scan(struct rpt *myrpt)
{
	int res, interval;
	char mhz[MAXREMSTR], decimals[MAXREMSTR], k10=0i, k100=0;

	switch(myrpt->hfscanmode){

		case HF_SCAN_DOWN_SLOW:
			interval = -10; /* 100Hz /sec */
			break;

		case HF_SCAN_DOWN_QUICK:
			interval = -50; /* 500Hz /sec */
			break;

		case HF_SCAN_DOWN_FAST:
			interval = -200; /* 2KHz /sec */
			break;

		case HF_SCAN_UP_SLOW:
			interval = 10; /* 100Hz /sec */
			break;

		case HF_SCAN_UP_QUICK:
			interval = 50; /* 500 Hz/sec */
			break;

		case HF_SCAN_UP_FAST:
			interval = 200; /* 2KHz /sec */
			break;

		default:
			myrpt->hfscanmode = 0; /* Huh? */
			return -1;
	}

	res = split_freq(mhz, decimals, myrpt->freq);
		
	if(!res){
		k100 =decimals[0];
		k10 = decimals[1];
		res = multimode_bump_freq(myrpt, interval);
	}

	if(!res)
		res = split_freq(mhz, decimals, myrpt->freq);


	if(res){
		myrpt->hfscanmode = 0;
		myrpt->hfscanstatus = -2;
		return -1;
	}

	/* Announce 10KHz boundaries */
	if(k10 != decimals[1]){
		int myhund = (interval < 0) ? k100 : decimals[0];
		int myten = (interval < 0) ? k10 : decimals[1];
		myrpt->hfscanstatus = (myten == '0') ? (myhund - '0') * 100 : (myten - '0') * 10;
	} else myrpt->hfscanstatus = 0;
	return res;

}
/*
	retrieve memory setting and set radio
*/
static int get_mem_set(struct rpt *myrpt, char *digitbuf)
{
	int res=0;
	if(debug)ast_log(LOG_NOTICE," digitbuf=%s\n", digitbuf);
	res = retrieve_memory(myrpt, digitbuf);
	if(!res)res=setrem(myrpt);	
	if(debug)ast_log(LOG_NOTICE," freq=%s  res=%i\n", myrpt->freq, res);
	return res;
}
/*
	steer the radio selected channel to either one programmed into the radio
	or if the radio is VFO agile, to an rpt.conf memory location.
*/
static int channel_steer(struct rpt *myrpt, char *data)
{
	int res=0;

	if(debug)ast_log(LOG_NOTICE,"remoterig=%s, data=%s\n",myrpt->remoterig,data);
	if (!myrpt->remoterig) return(0);
	if(data<=0)
	{
		res=-1;
	}
	else
	{
		myrpt->nowchan=strtod(data,NULL);
		if(!strcmp(myrpt->remoterig, remote_rig_ppp16))
		{
			char string[16];
			sprintf(string,"SETCHAN %d ",myrpt->nowchan);
			send_usb_txt(myrpt,string);	
		}
		else
		{
			if(get_mem_set(myrpt, data))res=-1;
		}
	}
	if(debug)ast_log(LOG_NOTICE,"nowchan=%i  res=%i\n",myrpt->nowchan, res);
	return res;
}
/*
*/
static int channel_revert(struct rpt *myrpt)
{
	int res=0;
	if(debug)ast_log(LOG_NOTICE,"remoterig=%s, nowchan=%02d, waschan=%02d\n",myrpt->remoterig,myrpt->nowchan,myrpt->waschan);
	if (!myrpt->remoterig) return(0);
	if(myrpt->nowchan!=myrpt->waschan)
	{
		char data[8];
        if(debug)ast_log(LOG_NOTICE,"reverting.\n");
		sprintf(data,"%02d",myrpt->waschan);
		myrpt->nowchan=myrpt->waschan;
		channel_steer(myrpt,data);
		res=1;
	}
	return(res);
}
/*
* Remote base function
*/

static int function_remote(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
	char *s,*s1,*s2;
	int i,j,p,r,ht,k,l,ls2,m,d,offset,offsave, modesave, defmode;
	char multimode = 0;
	char oc,*cp,*cp1,*cp2;
	char tmp[15], freq[15] = "", savestr[15] = "";
	char mhz[MAXREMSTR], decimals[MAXREMSTR];
	union {
		int i;
		void *p;
		char _filler[8];
	} pu;


    if(debug > 6) {
    	ast_log(LOG_NOTICE,"%s param=%s digitbuf=%s source=%i\n",myrpt->name,param,digitbuf,command_source);
	}

	if((!param) || (command_source == SOURCE_RPT) || (command_source == SOURCE_LNK))
		return DC_ERROR;
		
	p = myatoi(param);
	pu.i = p;

	if ((p != 99) && (p != 5) && (p != 140) && myrpt->p.authlevel && 
		(!myrpt->loginlevel[0])) return DC_ERROR;
	multimode = multimode_capable(myrpt);

	switch(p){

		case 1:  /* retrieve memory */
			if(strlen(digitbuf) < 2) /* needs 2 digits */
				break;
			
			for(i = 0 ; i < 2 ; i++){
				if((digitbuf[i] < '0') || (digitbuf[i] > '9'))
					return DC_ERROR;
			}
		    	r=get_mem_set(myrpt, digitbuf);
			if (r < 0){
				rpt_telemetry(myrpt,MEMNOTFOUND,NULL);
				return DC_COMPLETE;
			}
			else if (r > 0){
				return DC_ERROR;
			}
			return DC_COMPLETE;	
			
		case 2:  /* set freq and offset */
	   
			
	    		for(i = 0, j = 0, k = 0, l = 0 ; digitbuf[i] ; i++){ /* look for M+*K+*O or M+*H+* depending on mode */
				if(digitbuf[i] == '*'){
					j++;
					continue;
				}
				if((digitbuf[i] < '0') || (digitbuf[i] > '9'))
					goto invalid_freq;
				else{
					if(j == 0)
						l++; /* # of digits before first * */
					if(j == 1)
						k++; /* # of digits after first * */
				}
			}
		
			i = strlen(digitbuf) - 1;
			if(multimode){
				if((j > 2) || (l > 3) || (k > 6))
					goto invalid_freq; /* &^@#! */
 			}
			else{
				if((j > 2) || (l > 4) || (k > 5))
					goto invalid_freq; /* &^@#! */
				if ((!narrow_capable(myrpt)) &&
					(k > 3)) goto invalid_freq;
			}

			/* Wait for M+*K+* */

			if(j < 2)
				break; /* Not yet */

			/* We have a frequency */

			strncpy(tmp, digitbuf ,sizeof(tmp) - 1);
			
			s = tmp;
			s1 = strsep(&s, "*"); /* Pick off MHz */
			s2 = strsep(&s,"*"); /* Pick off KHz and Hz */
			ls2 = strlen(s2);	
			
			switch(ls2){ /* Allow partial entry of khz and hz digits for laziness support */
				case 1:
					ht = 0;
					k = 100 * atoi(s2);
					break;
				
				case 2:
					ht = 0;
					k = 10 * atoi(s2);
					break;
					
				case 3:
					if((!narrow_capable(myrpt)) &&
					  (!multimode))
					{
						if((s2[2] != '0')&&(s2[2] != '5'))
							goto invalid_freq;
					}
					ht = 0;
					k = atoi(s2);
						break;
				case 4:
					k = atoi(s2)/10;
					ht = 10 * (atoi(s2+(ls2-1)));
					break;

				case 5:
					k = atoi(s2)/100;
					ht = (atoi(s2+(ls2-2)));
					break;
					
				default:
					goto invalid_freq;
			}

			/* Check frequency for validity and establish a default mode */
			
			snprintf(freq, sizeof(freq), "%s.%03d%02d",s1, k, ht);

 			if(debug) 
				ast_log(LOG_NOTICE, "New frequency: %s\n", freq);
	
			split_freq(mhz, decimals, freq);
			m = atoi(mhz);
			d = atoi(decimals);

			if(check_freq(myrpt, m, d, &defmode)) /* Check to see if frequency entered is legit */
			        goto invalid_freq;

 			if((defmode == REM_MODE_FM) && (digitbuf[i] == '*')) /* If FM, user must enter and additional offset digit */
				break; /* Not yet */


			offset = REM_SIMPLEX; /* Assume simplex */

			if(defmode == REM_MODE_FM){
				oc = *s; /* Pick off offset */
			
				if (oc){
					switch(oc){
						case '1':
							offset = REM_MINUS;
							break;
						
						case '2':
							offset = REM_SIMPLEX;
						break;
						
						case '3':
							offset = REM_PLUS;
							break;
						
						default:
							goto invalid_freq;
					} 
				} 
			}	
			offsave = myrpt->offset;
			modesave = myrpt->remmode;
			strlcpy(savestr, myrpt->freq, sizeof(savestr) - 1);
			strlcpy(myrpt->freq, freq, sizeof(myrpt->freq) - 1);
			myrpt->offset = offset;
			myrpt->remmode = defmode;

			if (setrem(myrpt) == -1){
				myrpt->offset = offsave;
				myrpt->remmode = modesave;
				strlcpy(myrpt->freq, savestr, sizeof(myrpt->freq) - 1);
				goto invalid_freq;
			}
			if (strcmp(myrpt->remoterig, remote_rig_tm271) &&
			   strcmp(myrpt->remoterig, remote_rig_kenwood))
				rpt_telemetry(myrpt,COMPLETE,NULL);
			return DC_COMPLETE;

invalid_freq:
			rpt_telemetry(myrpt,INVFREQ,NULL);
			return DC_ERROR; 
		
		case 3: /* set rx PL tone */
	    		for(i = 0, j = 0, k = 0, l = 0 ; digitbuf[i] ; i++){ /* look for N+*N */
				if(digitbuf[i] == '*'){
					j++;
					continue;
				}
				if((digitbuf[i] < '0') || (digitbuf[i] > '9'))
					return DC_ERROR;
				else{
					if(j)
						l++;
					else
						k++;
				}
			}
			if((j > 1) || (k > 3) || (l > 1))
				return DC_ERROR; /* &$@^! */
			i = strlen(digitbuf) - 1;
			if((j != 1) || (k < 2)|| (l != 1))
				break; /* Not yet */
			if(debug)
				printf("PL digits entered %s\n", digitbuf);
	    		
			strncpy(tmp, digitbuf, sizeof(tmp) - 1);
			/* see if we have at least 1 */
			s = strchr(tmp,'*');
			if(s)
				*s = '.';
			strlcpy(savestr, myrpt->rxpl, sizeof(savestr) - 1);
			strlcpy(myrpt->rxpl, tmp, sizeof(myrpt->rxpl) - 1);
			if ((!strcmp(myrpt->remoterig, remote_rig_rbi)) ||
			  (!strcmp(myrpt->remoterig, remote_rig_ft100)))
			{
				strlcpy(myrpt->txpl, tmp, sizeof(myrpt->txpl) - 1);
			}
			if (setrem(myrpt) == -1){
				strlcpy(myrpt->rxpl, savestr, sizeof(myrpt->rxpl) - 1);
				return DC_ERROR;
			}
			return DC_COMPLETE;
		
		case 4: /* set tx PL tone */
			/* cant set tx tone on RBI (rx tone does both) */
			if(!strcmp(myrpt->remoterig, remote_rig_rbi))
				return DC_ERROR;
			/* cant set tx tone on ft100 (rx tone does both) */
			if(!strcmp(myrpt->remoterig, remote_rig_ft100))
				return DC_ERROR;
			/*  eventually for the ic706 instead of just throwing the exception
				we can check if we are in encode only mode and allow the tx
				ctcss code to be changed. but at least the warning message is
				issued for now.
			*/
			if(!strcmp(myrpt->remoterig, remote_rig_ic706))
			{
				if(debug)
					ast_log(LOG_WARNING,"Setting IC706 Tx CTCSS Code Not Supported. Set Rx Code for both.\n");
				return DC_ERROR;
			}
	    	for(i = 0, j = 0, k = 0, l = 0 ; digitbuf[i] ; i++){ /* look for N+*N */
				if(digitbuf[i] == '*'){
					j++;
					continue;
				}
				if((digitbuf[i] < '0') || (digitbuf[i] > '9'))
					return DC_ERROR;
				else{
					if(j)
						l++;
					else
						k++;
				}
			}
			if((j > 1) || (k > 3) || (l > 1))
				return DC_ERROR; /* &$@^! */
			i = strlen(digitbuf) - 1;
			if((j != 1) || (k < 2)|| (l != 1))
				break; /* Not yet */
			if(debug)
				printf("PL digits entered %s\n", digitbuf);
	    		
			strncpy(tmp, digitbuf, sizeof(tmp) - 1);
			/* see if we have at least 1 */
			s = strchr(tmp,'*');
			if(s)
				*s = '.';
			strlcpy(savestr, myrpt->txpl, sizeof(savestr) - 1);
			strlcpy(myrpt->txpl, tmp, sizeof(myrpt->txpl) - 1);
			
			if (setrem(myrpt) == -1){
				strlcpy(myrpt->txpl, savestr, sizeof(myrpt->txpl) - 1);
				return DC_ERROR;
			}
			return DC_COMPLETE;
		

		case 6: /* MODE (FM,USB,LSB,AM) */
			if(strlen(digitbuf) < 1)
				break;

			if(!multimode)
				return DC_ERROR; /* Multimode radios only */

			switch(*digitbuf){
				case '1':
					split_freq(mhz, decimals, myrpt->freq); 
					m=atoi(mhz);
					if(m < 29) /* No FM allowed below 29MHz! */
						return DC_ERROR;
					myrpt->remmode = REM_MODE_FM;
					
					rpt_telemetry(myrpt,REMMODE,NULL);
					break;

				case '2':
					myrpt->remmode = REM_MODE_USB;
					rpt_telemetry(myrpt,REMMODE,NULL);
					break;	

				case '3':
					myrpt->remmode = REM_MODE_LSB;
					rpt_telemetry(myrpt,REMMODE,NULL);
					break;
				
				case '4':
					myrpt->remmode = REM_MODE_AM;
					rpt_telemetry(myrpt,REMMODE,NULL);
					break;
		
				default:
					return DC_ERROR;
			}

			if(setrem(myrpt))
				return DC_ERROR;
			return DC_COMPLETEQUIET;
		case 99:
			/* cant log in when logged in */
			if (myrpt->loginlevel[0]) 
				return DC_ERROR;
			*myrpt->loginuser = 0;
			myrpt->loginlevel[0] = 0;
			cp = ast_strdup(param);
			cp1 = strchr(cp,',');
			ast_mutex_lock(&myrpt->lock);
			if (cp1) 
			{
				*cp1 = 0;
				cp2 = strchr(cp1 + 1,',');
				if (cp2) 
				{
					*cp2 = 0;
					strncpy(myrpt->loginlevel,cp2 + 1,
						sizeof(myrpt->loginlevel) - 1);
				}
				strlcpy(myrpt->loginuser,cp1 + 1,sizeof(myrpt->loginuser)-1);
				ast_mutex_unlock(&myrpt->lock);
				if (myrpt->p.archivedir)
				{
					char str[100];

					sprintf(str,"LOGIN,%s,%s",
					    myrpt->loginuser,myrpt->loginlevel);
					donodelog(myrpt,str);
				}
				if (debug) 
					printf("loginuser %s level %s\n",myrpt->loginuser,myrpt->loginlevel);
				rpt_telemetry(myrpt,REMLOGIN,NULL);
			}
			ast_free(cp);
			return DC_COMPLETEQUIET;
		case 100: /* RX PL Off */
			myrpt->rxplon = 0;
			setrem(myrpt);
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 101: /* RX PL On */
			myrpt->rxplon = 1;
			setrem(myrpt);
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 102: /* TX PL Off */
			myrpt->txplon = 0;
			setrem(myrpt);
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 103: /* TX PL On */
			myrpt->txplon = 1;
			setrem(myrpt);
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 104: /* Low Power */
			if(!strcmp(myrpt->remoterig, remote_rig_ic706))
				return DC_ERROR;
			myrpt->powerlevel = REM_LOWPWR;
			setrem(myrpt);
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 105: /* Medium Power */
			if(!strcmp(myrpt->remoterig, remote_rig_ic706))
				return DC_ERROR;
			if (ISRIG_RTX(myrpt->remoterig)) return DC_ERROR;
			myrpt->powerlevel = REM_MEDPWR;
			setrem(myrpt);
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 106: /* Hi Power */
			if(!strcmp(myrpt->remoterig, remote_rig_ic706))
				return DC_ERROR;
			myrpt->powerlevel = REM_HIPWR;
			setrem(myrpt);
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 107: /* Bump down 20Hz */
			multimode_bump_freq(myrpt, -20);
			return DC_COMPLETE;
		case 108: /* Bump down 100Hz */
			multimode_bump_freq(myrpt, -100);
			return DC_COMPLETE;
		case 109: /* Bump down 500Hz */
			multimode_bump_freq(myrpt, -500);
			return DC_COMPLETE;
		case 110: /* Bump up 20Hz */
			multimode_bump_freq(myrpt, 20);
			return DC_COMPLETE;
		case 111: /* Bump up 100Hz */
			multimode_bump_freq(myrpt, 100);
			return DC_COMPLETE;
		case 112: /* Bump up 500Hz */
			multimode_bump_freq(myrpt, 500);
			return DC_COMPLETE;
		case 113: /* Scan down slow */
			myrpt->scantimer = REM_SCANTIME;
			myrpt->hfscanmode = HF_SCAN_DOWN_SLOW;
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 114: /* Scan down quick */
			myrpt->scantimer = REM_SCANTIME;
			myrpt->hfscanmode = HF_SCAN_DOWN_QUICK;
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 115: /* Scan down fast */
			myrpt->scantimer = REM_SCANTIME;
			myrpt->hfscanmode = HF_SCAN_DOWN_FAST;
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 116: /* Scan up slow */
			myrpt->scantimer = REM_SCANTIME;
			myrpt->hfscanmode = HF_SCAN_UP_SLOW;
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 117: /* Scan up quick */
			myrpt->scantimer = REM_SCANTIME;
			myrpt->hfscanmode = HF_SCAN_UP_QUICK;
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 118: /* Scan up fast */
			myrpt->scantimer = REM_SCANTIME;
			myrpt->hfscanmode = HF_SCAN_UP_FAST;
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 119: /* Tune Request */
			if(debug > 3)
				ast_log(LOG_NOTICE,"TUNE REQUEST\n");
			/* if not currently going, and valid to do */
			if((!myrpt->tunerequest) && 
			    ((!strcmp(myrpt->remoterig, remote_rig_ft897)) || 
			    (!strcmp(myrpt->remoterig, remote_rig_ft100)) || 
			    (!strcmp(myrpt->remoterig, remote_rig_ft950)) || 
				(!strcmp(myrpt->remoterig, remote_rig_ic706)) )) { 
				myrpt->remotetx = 0;
				if (strncasecmp(myrpt->txchannel->name,
					"Zap/Pseudo",10))
				{
					ast_indicate(myrpt->txchannel,
						AST_CONTROL_RADIO_UNKEY);
				}	
				myrpt->tunetx = 0;
				myrpt->tunerequest = 1;
				rpt_telemetry(myrpt,TUNE,NULL);
				return DC_COMPLETEQUIET;
			}
			return DC_ERROR;			
		case 5: /* Long Status */
			rpt_telemetry(myrpt,REMLONGSTATUS,NULL);
			return DC_COMPLETEQUIET;
		case 140: /* Short Status */
			rpt_telemetry(myrpt,REMSHORTSTATUS,NULL);
			return DC_COMPLETEQUIET;
		case 200:
		case 201:
		case 202:
		case 203:
		case 204:
		case 205:
		case 206:
		case 207:
		case 208:
		case 209:
		case 210:
		case 211:
		case 212:
		case 213:
		case 214:
		case 215:
			do_dtmf_local(myrpt,remdtmfstr[p - 200]);
			return DC_COMPLETEQUIET;
		default:
			break;
	}
	return DC_INDETERMINATE;
}


static int handle_remote_dtmf_digit(struct rpt *myrpt,char c, char *keyed, int phonemode)
{
time_t	now;
int	ret,res = 0,src;

	if(debug > 6)
		ast_log(LOG_NOTICE,"c=%c  phonemode=%i  dtmfidx=%i\n",c,phonemode,myrpt->dtmfidx);

	time(&myrpt->last_activity_time);
	/* Stop scan mode if in scan mode */
	if(myrpt->hfscanmode){
		stop_scan(myrpt);
		return 0;
	}

	time(&now);
	/* if timed-out */
	if ((myrpt->dtmf_time_rem + DTMF_TIMEOUT) < now)
	{
		myrpt->dtmfidx = -1;
		myrpt->dtmfbuf[0] = 0;
		myrpt->dtmf_time_rem = 0;
	}
	/* if decode not active */
	if (myrpt->dtmfidx == -1)
	{
		/* if not lead-in digit, dont worry */
		if (c != myrpt->p.funcchar)
		{
			if (!myrpt->p.propagate_dtmf)
			{
				rpt_mutex_lock(&myrpt->lock);
				do_dtmf_local(myrpt,c);
				rpt_mutex_unlock(&myrpt->lock);
			}
			return 0;
		}
		myrpt->dtmfidx = 0;
		myrpt->dtmfbuf[0] = 0;
		myrpt->dtmf_time_rem = now;
		return 0;
	}
	/* if too many in buffer, start over */
	if (myrpt->dtmfidx >= MAXDTMF)
	{
		myrpt->dtmfidx = 0;
		myrpt->dtmfbuf[0] = 0;
		myrpt->dtmf_time_rem = now;
	}
	if (c == myrpt->p.funcchar)
	{
		/* if star at beginning, or 2 together, erase buffer */
		if ((myrpt->dtmfidx < 1) || 
			(myrpt->dtmfbuf[myrpt->dtmfidx - 1] == myrpt->p.funcchar))
		{
			myrpt->dtmfidx = 0;
			myrpt->dtmfbuf[0] = 0;
			myrpt->dtmf_time_rem = now;
			return 0;
		}
	}
	myrpt->dtmfbuf[myrpt->dtmfidx++] = c;
	myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
	myrpt->dtmf_time_rem = now;
	
	
	src = SOURCE_RMT;
	if (phonemode == 2) src = SOURCE_DPHONE;
	else if (phonemode) src = SOURCE_PHONE;
	else if (phonemode == 4) src = SOURCE_ALT;
	ret = collect_function_digits(myrpt, myrpt->dtmfbuf, src, NULL);
	
	switch(ret){
	
		case DC_INDETERMINATE:
			res = 0;
			break;
				
		case DC_DOKEY:
			if (keyed) *keyed = 1;
			res = 0;
			break;
				
		case DC_REQ_FLUSH:
			myrpt->dtmfidx = 0;
			myrpt->dtmfbuf[0] = 0;
			res = 0;
			break;
				
				
		case DC_COMPLETE:
			res = 1;
		case DC_COMPLETEQUIET:
			myrpt->totalexecdcommands++;
			myrpt->dailyexecdcommands++;
			strncpy(myrpt->lastdtmfcommand, myrpt->dtmfbuf, MAXDTMF-1);
			myrpt->lastdtmfcommand[MAXDTMF-1] = '\0';
			myrpt->dtmfbuf[0] = 0;
			myrpt->dtmfidx = -1;
			myrpt->dtmf_time_rem = 0;
			break;
				
		case DC_ERROR:
		default:
			myrpt->dtmfbuf[0] = 0;
			myrpt->dtmfidx = -1;
			myrpt->dtmf_time_rem = 0;
			res = 0;
			break;
	}

	return res;
}

static int handle_remote_data(struct rpt *myrpt, char *str)
{
char	tmp[300],cmd[300],dest[300],src[300],c;
int	seq,res;

 	/* put string in our buffer */
	strncpy(tmp,str,sizeof(tmp) - 1);
	if (!strcmp(tmp,discstr)) return 0;
        if (!strcmp(tmp,newkeystr))
        {
		if (!myrpt->newkey) 
		{
			send_old_newkey(myrpt->rxchannel);
			myrpt->newkey = 1;
		}
                return 0;
        }
        if (!strcmp(tmp,newkey1str))
        {
		myrpt->newkey = 2;
                return 0;
        }
        if (!strncmp(tmp,iaxkeystr,strlen(iaxkeystr)))
        {
		myrpt->iaxkey = 1;
                return 0;
        }

	if (tmp[0] == 'T') return 0;

#ifndef	DO_NOT_NOTIFY_MDC1200_ON_REMOTE_BASES
	if (tmp[0] == 'I')
	{
		if (sscanf(tmp,"%s %s %s",cmd,src,dest) != 3)
		{
			ast_log(LOG_WARNING, "Unable to parse ident string %s\n",str);
			return 0;
		}
		mdc1200_notify(myrpt,src,dest);
		return 0;
	}
#endif
	if (tmp[0] == 'L') return 0;
	if (sscanf(tmp,"%s %s %s %d %c",cmd,dest,src,&seq,&c) != 5)
	{
		ast_log(LOG_WARNING, "Unable to parse link string %s\n",str);
		return 0;
	}
	if (strcmp(cmd,"D"))
	{
		ast_log(LOG_WARNING, "Unable to parse link string %s\n",str);
		return 0;
	}
	/* if not for me, ignore */
	if (strcmp(dest,myrpt->name)) return 0;
	if (myrpt->p.archivedir)
	{
		char str[100];

		sprintf(str,"DTMF,%c",c);
		donodelog(myrpt,str);
	}
	c = func_xlat(myrpt,c,&myrpt->p.outxlat);
	if (!c) return(0);
	res = handle_remote_dtmf_digit(myrpt,c, NULL, 0);
	if (res != 1)
		return res;
	if ((!strcmp(myrpt->remoterig, remote_rig_tm271)) ||
	   (!strcmp(myrpt->remoterig, remote_rig_kenwood)))
		rpt_telemetry(myrpt,REMCOMPLETE,NULL);
	else
		rpt_telemetry(myrpt,COMPLETE,NULL);
	return 0;
}

static int handle_remote_phone_dtmf(struct rpt *myrpt, char c, char *keyed, int phonemode)
{
int	res;


	if(phonemode == 3) /* simplex phonemode, funcchar key/unkey toggle */
	{
		if (keyed && *keyed && ((c == myrpt->p.funcchar) || (c == myrpt->p.endchar)))
		{
			*keyed = 0; /* UNKEY */
			return 0;
		}
		else if (keyed && !*keyed && (c == myrpt->p.funcchar))
		{
			*keyed = 1; /* KEY */
			return 0;
		}
	}
	else /* endchar unkey */
	{

		if (keyed && *keyed && (c == myrpt->p.endchar))
		{
			*keyed = 0;
			return DC_INDETERMINATE;
		}
	}
	if (myrpt->p.archivedir)
	{
		char str[100];

		sprintf(str,"DTMF(P),%c",c);
		donodelog(myrpt,str);
	}
	res = handle_remote_dtmf_digit(myrpt,c,keyed, phonemode);
	if (res != 1)
		return res;
	if ((!strcmp(myrpt->remoterig, remote_rig_tm271)) ||
	   (!strcmp(myrpt->remoterig, remote_rig_kenwood)))
		rpt_telemetry(myrpt,REMCOMPLETE,NULL);
	else
		rpt_telemetry(myrpt,COMPLETE,NULL);
	return 0;
}

static int attempt_reconnect(struct rpt *myrpt, struct rpt_link *l)
{
	char *s, *s1, *tele;
	char tmp[300], deststr[300] = "";
	char sx[320],*sy;
	struct ast_frame *f1;


	if (!node_lookup(myrpt,l->name,tmp,sizeof(tmp) - 1,1))
	{
		fprintf(stderr,"attempt_reconnect: cannot find node %s\n",l->name);
		return -1;
	}
	/* cannot apply to echolink */
	if (!strncasecmp(tmp,"echolink",8)) return 0;
	/* cannot apply to tlb */
	if (!strncasecmp(tmp,"tlb",3)) return 0;
	rpt_mutex_lock(&myrpt->lock);
	/* remove from queue */
	remque((struct qelem *) l);
	rpt_mutex_unlock(&myrpt->lock);
	s = tmp;
	s1 = strsep(&s,",");
	if (!strchr(s1,':') && strchr(s1,'/') && strncasecmp(s1, "local/", 6))
	{
		sy = strchr(s1,'/');		
		*sy = 0;
		sprintf(sx,"%s:4569/%s",s1,sy + 1);
		s1 = sx;
	}
	strsep(&s,",");
	snprintf(deststr, sizeof(deststr), "IAX2/%s", s1);
	tele = strchr(deststr, '/');
	if (!tele) {
		fprintf(stderr,"attempt_reconnect:Dial number (%s) must be in format tech/number\n",deststr);
		return -1;
	}
	*tele++ = 0;
	l->elaptime = 0;
	l->connecttime = 0;
	l->thisconnected = 0;
	l->iaxkey = 0;
	l->newkey = 0;
	l->chan = ast_request(deststr, AST_FORMAT_SLINEAR, tele,NULL);
	if (!(l->chan))
	{
		usleep(150000);
		l->chan = ast_request(deststr, AST_FORMAT_SLINEAR, tele,NULL);
	}
	l->linkmode = 0;
	l->lastrx1 = 0;
	l->lastrealrx = 0;
	l->rxlingertimer = ((l->iaxkey) ? RX_LINGER_TIME_IAXKEY : RX_LINGER_TIME);
	l->newkeytimer = NEWKEYTIME;
	l->newkey = 2;
	while((f1 = AST_LIST_REMOVE_HEAD(&l->textq,frame_list))) ast_frfree(f1);
	if (l->chan){
		ast_set_read_format(l->chan, AST_FORMAT_SLINEAR);
		ast_set_write_format(l->chan, AST_FORMAT_SLINEAR);
#ifndef	NEW_ASTERISK
		l->chan->whentohangup = 0;
#endif
		l->chan->appl = "Apprpt";
		l->chan->data = "(Remote Rx)";
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "rpt (attempt_reconnect) initiating call to %s/%s on %s\n",
				deststr, tele, l->chan->name);
		if(l->chan->cid.cid_num)
			ast_free(l->chan->cid.cid_num);
		l->chan->cid.cid_num = ast_strdup(myrpt->name);
                ast_call(l->chan,tele,999); 

	}
	else 
	{
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Unable to place call to %s/%s\n",
				deststr,tele);
		return -1;
	}
	rpt_mutex_lock(&myrpt->lock);
	/* put back in queue */
	insque((struct qelem *)l,(struct qelem *)myrpt->links.next);
	rpt_mutex_unlock(&myrpt->lock);
	ast_log(LOG_WARNING,"Reconnect Attempt to %s in process\n",l->name);
	return 0;
}

/* 0 return=continue, 1 return = break, -1 return = error */
static void local_dtmf_helper(struct rpt *myrpt,char c_in)
{
int	res;
pthread_attr_t	attr;
char	cmd[MAXDTMF+1] = "",c,tone[10];


	c = c_in & 0x7f;

	sprintf(tone,"%c",c);
	rpt_manager_trigger(myrpt, "DTMF", tone);

	if (myrpt->p.archivedir)
	{
		char str[100];

		sprintf(str,"DTMF,MAIN,%c",c);
		donodelog(myrpt,str);
	}
	if (c == myrpt->p.endchar)
	{
	/* if in simple mode, kill autopatch */
		if (myrpt->p.simple && myrpt->callmode)
		{   
			if(debug)
				ast_log(LOG_WARNING, "simple mode autopatch kill\n");
			rpt_mutex_lock(&myrpt->lock);
			myrpt->callmode = 0;
			myrpt->macropatch=0;
			channel_revert(myrpt);
			rpt_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt,TERM,NULL);
			return;
		}
		rpt_mutex_lock(&myrpt->lock);
		myrpt->stopgen = 1;
		if (myrpt->cmdnode[0])
		{
			cmd[0] = 0;
			if (!strcmp(myrpt->cmdnode,"aprstt"))
			{
				char overlay,aprscall[100],fname[100];
				FILE *fp;

				snprintf(cmd, sizeof(cmd),"A%s", myrpt->dtmfbuf);
				overlay = aprstt_xlat(cmd,aprscall);
				if (overlay)
				{
					if (debug) ast_log(LOG_WARNING,"aprstt got string %s call %s overlay %c\n",cmd,aprscall,overlay);
					if (!myrpt->p.aprstt[0]) ast_copy_string(fname,APRSTT_PIPE,sizeof(fname) - 1);
					else snprintf(fname,sizeof(fname) - 1,APRSTT_SUB_PIPE,myrpt->p.aprstt);
					fp = fopen(fname,"w");
					if (!fp)
					{
						ast_log(LOG_WARNING,"Can not open APRSTT pipe %s\n",fname);
					}
					else
					{
						fprintf(fp,"%s %c\n",aprscall,overlay);
						fclose(fp);
						rpt_telemetry(myrpt, ARB_ALPHA, (void *) aprscall);
					}
				}
			}
			myrpt->cmdnode[0] = 0;
			myrpt->dtmfidx = -1;
			myrpt->dtmfbuf[0] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			if (!cmd[0]) rpt_telemetry(myrpt,COMPLETE,NULL);
			return;
		} 
		else if(!myrpt->inpadtest)
                {
                        rpt_mutex_unlock(&myrpt->lock);
                        if (myrpt->p.propagate_phonedtmf)
                               do_dtmf_phone(myrpt,NULL,c);
			if ((myrpt->dtmfidx == -1) &&
			   ((myrpt->callmode == 2) || (myrpt->callmode == 3)))
			{
				myrpt->mydtmf = c;
			}
			return;
                }
		else
		{
			rpt_mutex_unlock(&myrpt->lock);
		}
	}
	rpt_mutex_lock(&myrpt->lock);
	if (myrpt->cmdnode[0] && strcmp(myrpt->cmdnode,"aprstt"))
	{
		rpt_mutex_unlock(&myrpt->lock);
		send_link_dtmf(myrpt,c);
		return;
	}
	if (!myrpt->p.simple)
	{
		if ((!myrpt->inpadtest) && myrpt->p.aprstt && (!myrpt->cmdnode[0]) && (c == 'A'))
		{
			strcpy(myrpt->cmdnode,"aprstt");
			myrpt->dtmfidx = 0;
			myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			time(&myrpt->dtmf_time);
			return;
		}
		if ((!myrpt->inpadtest) && (c == myrpt->p.funcchar))
		{
			if (myrpt->p.dopfxtone && (myrpt->dtmfidx == -1))
				rpt_telemetry(myrpt,PFXTONE,NULL);
			myrpt->dtmfidx = 0;
			myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			time(&myrpt->dtmf_time);
			return;
		} 
		else if (((myrpt->inpadtest) || (c != myrpt->p.endchar)) && (myrpt->dtmfidx >= 0))
		{
			time(&myrpt->dtmf_time);
			cancel_pfxtone(myrpt);
			
			if (myrpt->dtmfidx < MAXDTMF)
			{
				int src;

				myrpt->dtmfbuf[myrpt->dtmfidx++] = c;
				myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
				
				strncpy(cmd, myrpt->dtmfbuf, sizeof(cmd) - 1);
				
				rpt_mutex_unlock(&myrpt->lock);
				if (myrpt->cmdnode[0]) return;
				src = SOURCE_RPT;
				if (c_in & 0x80) src = SOURCE_ALT;
				res = collect_function_digits(myrpt, cmd, src, NULL);
				rpt_mutex_lock(&myrpt->lock);
				switch(res){
				    case DC_INDETERMINATE:
					break;
				    case DC_REQ_FLUSH:
					myrpt->dtmfidx = 0;
					myrpt->dtmfbuf[0] = 0;
					break;
				    case DC_COMPLETE:
				    case DC_COMPLETEQUIET:
					myrpt->totalexecdcommands++;
					myrpt->dailyexecdcommands++;
					strncpy(myrpt->lastdtmfcommand, cmd, MAXDTMF-1);
					myrpt->lastdtmfcommand[MAXDTMF-1] = '\0';
					myrpt->dtmfbuf[0] = 0;
					myrpt->dtmfidx = -1;
					myrpt->dtmf_time = 0;
					break;

				    case DC_ERROR:
				    default:
					myrpt->dtmfbuf[0] = 0;
					myrpt->dtmfidx = -1;
					myrpt->dtmf_time = 0;
					break;
				}
				if(res != DC_INDETERMINATE) {
					rpt_mutex_unlock(&myrpt->lock);
					return;
				}
			} 
		}
	}
	else /* if simple */
	{
		if ((!myrpt->callmode) && (c == myrpt->p.funcchar))
		{
			myrpt->callmode = 1;
			myrpt->patchnoct = 0;
			myrpt->patchquiet = 0;
			myrpt->patchfarenddisconnect = 0;
			myrpt->patchdialtime = 0;
			strlcpy(myrpt->patchcontext, myrpt->p.ourcontext, MAXPATCHCONTEXT-1);
			myrpt->cidx = 0;
			myrpt->exten[myrpt->cidx] = 0;
			rpt_mutex_unlock(&myrpt->lock);
		        pthread_attr_init(&attr);
		        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			ast_pthread_create(&myrpt->rpt_call_thread,&attr,rpt_call,(void *)myrpt);
			return;
		}
	}
	if (myrpt->callmode == 1)
	{
		myrpt->exten[myrpt->cidx++] = c;
		myrpt->exten[myrpt->cidx] = 0;
		/* if this exists */
		if (ast_exists_extension(myrpt->pchannel,myrpt->patchcontext,myrpt->exten,1,NULL))
		{
			/* if this really it, end now */
			if (!ast_matchmore_extension(myrpt->pchannel,myrpt->patchcontext,
				myrpt->exten,1,NULL)) 
			{
				myrpt->callmode = 2;
				rpt_mutex_unlock(&myrpt->lock);
				if(!myrpt->patchquiet)
					rpt_telemetry(myrpt,PROC,NULL); 
				return;
			}
			else /* othewise, reset timer */
			{
				myrpt->calldigittimer = 1;
			}
		}
		/* if can continue, do so */
		if (!ast_canmatch_extension(myrpt->pchannel,myrpt->patchcontext,myrpt->exten,1,NULL))
		{
			/* call has failed, inform user */
			myrpt->callmode = 4;
		}
		rpt_mutex_unlock(&myrpt->lock);
		return;
	}
	if (((myrpt->callmode == 2) || (myrpt->callmode == 3)) &&
		(myrpt->dtmfidx < 0))
	{
		myrpt->mydtmf = c;
	}
	rpt_mutex_unlock(&myrpt->lock);
	if ((myrpt->dtmfidx < 0) && myrpt->p.propagate_phonedtmf)
		do_dtmf_phone(myrpt,NULL,c);
	return;
}


/* place an ID event in the telemetry queue */

static void queue_id(struct rpt *myrpt)
{
	if(myrpt->p.idtime){ /* ID time must be non-zero */
		myrpt->mustid = myrpt->tailid = 0;
		myrpt->idtimer = myrpt->p.idtime; /* Reset our ID timer */
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telemetry(myrpt,ID,NULL);
		rpt_mutex_lock(&myrpt->lock);
	}
}

/* Scheduler */
/* must be called locked */

static void do_scheduler(struct rpt *myrpt)
{
	int i,res;

#ifdef	NEW_ASTERISK
	struct ast_tm tmnow;
#else
	struct tm tmnow;
#endif
	struct ast_variable *skedlist;
	char *strs[5],*vp,*val,value[100];

	memcpy(&myrpt->lasttv, &myrpt->curtv, sizeof(struct timeval));
	
	if( (res = gettimeofday(&myrpt->curtv, NULL)) < 0)
		ast_log(LOG_NOTICE, "Scheduler gettime of day returned: %s\n", strerror(res));

	/* Try to get close to a 1 second resolution */
	
	if(myrpt->lasttv.tv_sec == myrpt->curtv.tv_sec)
		return;

	/* Service the sleep timer */
	if(myrpt->p.s[myrpt->p.sysstate_cur].sleepena){ /* If sleep mode enabled */
		if(myrpt->sleeptimer)
			myrpt->sleeptimer--;
		else{
			if(!myrpt->sleep)
				myrpt->sleep = 1; /* ZZZZZZ */
		}
	}
	/* Service activity timer */
	if(myrpt->p.lnkactmacro && myrpt->p.lnkacttime && myrpt->p.lnkactenable && myrpt->linkactivityflag){
		myrpt->linkactivitytimer++;
		/* 30 second warn */
		if ((myrpt->p.lnkacttime - myrpt->linkactivitytimer == 30) && myrpt->p.lnkacttimerwarn){
			if(debug > 4)
				ast_log(LOG_NOTICE, "Warning user of activity timeout\n");
			rpt_telemetry(myrpt,LOCALPLAY, myrpt->p.lnkacttimerwarn);
		}
		if(myrpt->linkactivitytimer >= myrpt->p.lnkacttime){
			/* Execute lnkactmacro */
			if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(myrpt->p.lnkactmacro)){
				ast_log(LOG_WARNING, "Link Activity timer could not execute macro %s: Macro buffer full\n",
					myrpt->p.lnkactmacro);
			}
			else{
				if(debug > 4)
					ast_log(LOG_NOTICE, "Executing link activity timer macro %s\n", myrpt->p.lnkactmacro);
				myrpt->macrotimer = MACROTIME;
				strncat(myrpt->macrobuf,myrpt->p.lnkactmacro, MAXMACRO - 1);
			}
			myrpt->linkactivitytimer = 0;
			myrpt->linkactivityflag = 0;
		}
	}
	/* Service repeater inactivity timer */
	if(myrpt->p.rptinacttime && myrpt->rptinactwaskeyedflag){
		if(myrpt->rptinacttimer < myrpt->p.rptinacttime)
			myrpt->rptinacttimer++;
		else{
			myrpt->rptinacttimer = 0;
			myrpt->rptinactwaskeyedflag = 0;
			if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(myrpt->p.rptinactmacro)){
				ast_log(LOG_WARNING, "Rpt inactivity timer could not execute macro %s: Macro buffer full\n",
					myrpt->p.rptinactmacro);
			}
			else {
				if(debug > 4)
					ast_log(LOG_NOTICE, "Executing rpt inactivity timer macro %s\n", myrpt->p.rptinactmacro);
				myrpt->macrotimer = MACROTIME;
				strncat(myrpt->macrobuf, myrpt->p.rptinactmacro, MAXMACRO -1);
			}
		}
	}
			
	rpt_localtime(&myrpt->curtv.tv_sec, &tmnow, NULL);

	/* If midnight, then reset all daily statistics */
	
	if((tmnow.tm_hour == 0)&&(tmnow.tm_min == 0)&&(tmnow.tm_sec == 0)){
		myrpt->dailykeyups = 0;
		myrpt->dailytxtime = 0;
		myrpt->dailykerchunks = 0;
		myrpt->dailyexecdcommands = 0;
	}

	if(tmnow.tm_sec != 0)
		return;

	/* Code below only executes once per minute */


	/* Don't schedule if remote */

        if (myrpt->remote)
                return;

	/* Don't schedule if disabled */

        if(myrpt->p.s[myrpt->p.sysstate_cur].schedulerdisable){
		if(debug > 6)
			ast_log(LOG_NOTICE, "Scheduler disabled\n");
		return;
	}

	if(!myrpt->p.skedstanzaname){ /* No stanza means we do nothing */
		if(debug > 6)
			ast_log(LOG_NOTICE,"No stanza for scheduler in rpt.conf\n");
		return;
	}

    /* get pointer to linked list of scheduler entries */
    skedlist = ast_variable_browse(myrpt->cfg, myrpt->p.skedstanzaname);

	if(debug > 6){
		ast_log(LOG_NOTICE, "Time now: %02d:%02d %02d %02d %02d\n",
			tmnow.tm_hour,tmnow.tm_min,tmnow.tm_mday,tmnow.tm_mon + 1, tmnow.tm_wday); 
	}
	/* walk the list */
	for(; skedlist; skedlist = skedlist->next){
		if(debug > 6)
			ast_log(LOG_NOTICE, "Scheduler entry %s = %s being considered\n",skedlist->name, skedlist->value);
		strncpy(value,skedlist->value,99);
		value[99] = 0;
		/* point to the substrings for minute, hour, dom, month, and dow */
		for( i = 0, vp = value ; i < 5; i++){
			if(!*vp)
				break;
			while((*vp == ' ') || (*vp == 0x09)) /* get rid of any leading white space */
				vp++;
			strs[i] = vp; /* save pointer to beginning of substring */
			while((*vp != ' ') && (*vp != 0x09) && (*vp != 0)) /* skip over substring */
				vp++;
			if(*vp)
				*vp++ = 0; /* mark end of substring */
		}
		if(debug > 6)
			ast_log(LOG_NOTICE, "i = %d, min = %s, hour = %s, mday=%s, mon=%s, wday=%s\n",i,
				strs[0], strs[1], strs[2], strs[3], strs[4]); 
 		if(i == 5){
			if((*strs[0] != '*')&&(atoi(strs[0]) != tmnow.tm_min))
				continue;
			if((*strs[1] != '*')&&(atoi(strs[1]) != tmnow.tm_hour))
				continue;
			if((*strs[2] != '*')&&(atoi(strs[2]) != tmnow.tm_mday))
				continue;
			if((*strs[3] != '*')&&(atoi(strs[3]) != tmnow.tm_mon + 1))
				continue;
			if(atoi(strs[4]) == 7)
				strs[4] = "0";
			if((*strs[4] != '*')&&(atoi(strs[4]) != tmnow.tm_wday))
				continue;
			if(debug)
				ast_log(LOG_NOTICE, "Executing scheduler entry %s = %s\n", skedlist->name, skedlist->value);
			if(atoi(skedlist->name) == 0)
				return; /* Zero is reserved for the startup macro */
			val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.macro, skedlist->name);
			if (!val){
				ast_log(LOG_WARNING,"Scheduler could not find macro %s\n",skedlist->name);
				return; /* Macro not found */
			}
			if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(val)){
				ast_log(LOG_WARNING, "Scheduler could not execute macro %s: Macro buffer full\n",
					skedlist->name);
				return; /* Macro buffer full */
			}
			myrpt->macrotimer = MACROTIME;
			strncat(myrpt->macrobuf,val,MAXMACRO - 1);
		}
		else{
			ast_log(LOG_WARNING,"Malformed scheduler entry in rpt.conf: %s = %s\n",
				skedlist->name, skedlist->value);
		}
	}

}

/* single thread with one file (request) to dial */
static void *rpt(void *this)
{
struct	rpt *myrpt = (struct rpt *)this;
char *tele,*idtalkover,c,myfirst,*p;
int ms = MSWAIT,i,lasttx=0,lastexttx = 0,lastpatchup = 0,val,identqueued,othertelemqueued;
int tailmessagequeued,ctqueued,dtmfed,lastmyrx,localmsgqueued;
unsigned int u;
FILE *fp;
struct stat mystat;
struct ast_channel *who;
struct dahdi_confinfo ci;  /* conference info */
time_t	t,was;
struct rpt_link *l,*m;
struct rpt_tele *telem;
char tmpstr[512],lstr[MAXLINKLIST],lat[100],lon[100],elev[100];


	if (myrpt->p.archivedir) mkdir(myrpt->p.archivedir,0600);
	sprintf(tmpstr,"%s/%s",myrpt->p.archivedir,myrpt->name);
	mkdir(tmpstr,0600);
	myrpt->ready = 0;
	rpt_mutex_lock(&myrpt->lock);
	myrpt->remrx = 0;
	myrpt->remote_webtransceiver = 0;

	telem = myrpt->tele.next;
	while(telem != &myrpt->tele)
	{
		ast_softhangup(telem->chan,AST_SOFTHANGUP_DEV);
		telem = telem->next;
	}
	rpt_mutex_unlock(&myrpt->lock);
	/* find our index, and load the vars initially */
	for(i = 0; i < nrpts; i++)
	{
		if (&rpt_vars[i] == myrpt)
		{
			load_rpt_vars(i,0);
			break;
		}
	}

	rpt_mutex_lock(&myrpt->lock);
	while(myrpt->xlink)
	{
		myrpt->xlink = 3;
		rpt_mutex_unlock(&myrpt->lock);
		usleep(100000);
		rpt_mutex_lock(&myrpt->lock);
	}	
	if ((!strcmp(myrpt->remoterig, remote_rig_rbi)) &&
	  (ioperm(myrpt->p.iobase,1,1) == -1))
	{
		rpt_mutex_unlock(&myrpt->lock);
		ast_log(LOG_WARNING, "Cant get io permission on IO port %x hex\n",myrpt->p.iobase);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	strncpy(tmpstr,myrpt->rxchanname,sizeof(tmpstr) - 1);
	tele = strchr(tmpstr,'/');
	if (!tele)
	{
		fprintf(stderr,"rpt:Rxchannel Dial number (%s) must be in format tech/number\n",myrpt->rxchanname);
		rpt_mutex_unlock(&myrpt->lock);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	*tele++ = 0;
	myrpt->rxchannel = ast_request(tmpstr,AST_FORMAT_SLINEAR,tele,NULL);
	myrpt->zaprxchannel = NULL;
	if (!strcasecmp(tmpstr,DAHDI_CHANNEL_NAME))
		myrpt->zaprxchannel = myrpt->rxchannel;
	if (myrpt->rxchannel)
	{
		if (myrpt->rxchannel->_state == AST_STATE_BUSY)
		{
			fprintf(stderr,"rpt:Sorry unable to obtain Rx channel\n");
			rpt_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
		ast_set_read_format(myrpt->rxchannel,AST_FORMAT_SLINEAR);
		ast_set_write_format(myrpt->rxchannel,AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
		if (myrpt->rxchannel->cdr)
			ast_set_flag(myrpt->rxchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
#ifndef	NEW_ASTERISK
		myrpt->rxchannel->whentohangup = 0;
#endif
		myrpt->rxchannel->appl = "Apprpt";
		myrpt->rxchannel->data = "(Repeater Rx)";
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "rpt (Rx) initiating call to %s/%s on %s\n",
				tmpstr,tele,myrpt->rxchannel->name);
		ast_call(myrpt->rxchannel,tele,999);
		if (myrpt->rxchannel->_state != AST_STATE_UP)
		{
			rpt_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
	}
	else
	{
		fprintf(stderr,"rpt:Sorry unable to obtain Rx channel\n");
		rpt_mutex_unlock(&myrpt->lock);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	myrpt->zaptxchannel = NULL;
	if (myrpt->txchanname)
	{
		strncpy(tmpstr,myrpt->txchanname,sizeof(tmpstr) - 1);
		tele = strchr(tmpstr,'/');
		if (!tele)
		{
			fprintf(stderr,"rpt:Txchannel Dial number (%s) must be in format tech/number\n",myrpt->txchanname);
			rpt_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
		*tele++ = 0;
		myrpt->txchannel = ast_request(tmpstr,AST_FORMAT_SLINEAR,tele,NULL);
		if ((!strcasecmp(tmpstr,DAHDI_CHANNEL_NAME)) && strcasecmp(tele,"pseudo"))
			myrpt->zaptxchannel = myrpt->txchannel;
		if (myrpt->txchannel)
		{
			if (myrpt->txchannel->_state == AST_STATE_BUSY)
			{
				fprintf(stderr,"rpt:Sorry unable to obtain Tx channel\n");
				rpt_mutex_unlock(&myrpt->lock);
				ast_hangup(myrpt->txchannel);
				ast_hangup(myrpt->rxchannel);
				myrpt->rpt_thread = AST_PTHREADT_STOP;
				pthread_exit(NULL);
			}			
			ast_set_read_format(myrpt->txchannel,AST_FORMAT_SLINEAR);
			ast_set_write_format(myrpt->txchannel,AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
			if (myrpt->txchannel->cdr)
				ast_set_flag(myrpt->txchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
#ifndef	NEW_ASTERISK
			myrpt->txchannel->whentohangup = 0;
#endif
			myrpt->txchannel->appl = "Apprpt";
			myrpt->txchannel->data = "(Repeater Tx)";
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "rpt (Tx) initiating call to %s/%s on %s\n",
					tmpstr,tele,myrpt->txchannel->name);
			ast_call(myrpt->txchannel,tele,999);
			if (myrpt->rxchannel->_state != AST_STATE_UP)
			{
				rpt_mutex_unlock(&myrpt->lock);
				ast_hangup(myrpt->rxchannel);
				ast_hangup(myrpt->txchannel);
				myrpt->rpt_thread = AST_PTHREADT_STOP;
				pthread_exit(NULL);
			}
		}
		else
		{
			fprintf(stderr,"rpt:Sorry unable to obtain Tx channel\n");
			rpt_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
	}
	else
	{
		myrpt->txchannel = myrpt->rxchannel;
		if ((!strncasecmp(myrpt->rxchanname,DAHDI_CHANNEL_NAME,3)) && 
		    strcasecmp(myrpt->rxchanname,"Zap/pseudo"))
			myrpt->zaptxchannel = myrpt->txchannel;
	}
	if (strncasecmp(myrpt->txchannel->name,"Zap/Pseudo",10))
	{
		ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_KEY);
		ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_UNKEY);
	}
	/* allocate a pseudo-channel thru asterisk */
	myrpt->pchannel = ast_request(DAHDI_CHANNEL_NAME,AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!myrpt->pchannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		rpt_mutex_unlock(&myrpt->lock);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	ast_set_read_format(myrpt->pchannel, AST_FORMAT_SLINEAR);
	ast_set_write_format(myrpt->pchannel, AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (myrpt->pchannel->cdr)
		ast_set_flag(myrpt->pchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	ast_answer(myrpt->pchannel);
	if (!myrpt->zaprxchannel) myrpt->zaprxchannel = myrpt->pchannel;
	if (!myrpt->zaptxchannel)
	{
		/* allocate a pseudo-channel thru asterisk */
		myrpt->zaptxchannel = ast_request(DAHDI_CHANNEL_NAME,AST_FORMAT_SLINEAR,"pseudo",NULL);
		if (!myrpt->zaptxchannel)
		{
			fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
			rpt_mutex_unlock(&myrpt->lock);
			if (myrpt->txchannel != myrpt->rxchannel) 
				ast_hangup(myrpt->txchannel);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
		ast_set_read_format(myrpt->zaptxchannel,AST_FORMAT_SLINEAR);
		ast_set_write_format(myrpt->zaptxchannel,AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
		if (myrpt->zaptxchannel->cdr)
			ast_set_flag(myrpt->zaptxchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
		ast_answer(myrpt->zaptxchannel);
	}
	/* allocate a pseudo-channel thru asterisk */
	myrpt->monchannel = ast_request(DAHDI_CHANNEL_NAME,AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!myrpt->monchannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		rpt_mutex_unlock(&myrpt->lock);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	ast_set_read_format(myrpt->monchannel,AST_FORMAT_SLINEAR);
	ast_set_write_format(myrpt->monchannel,AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (myrpt->monchannel->cdr)
		ast_set_flag(myrpt->monchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	ast_answer(myrpt->monchannel);
	/* make a conference for the tx */
	ci.chan = 0;
	ci.confno = -1; /* make a new conf */
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_LISTENER;
	/* first put the channel on the conference in proper mode */
	if (ioctl(myrpt->zaptxchannel->fds[0],DAHDI_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		rpt_mutex_unlock(&myrpt->lock);
		ast_hangup(myrpt->pchannel);
		ast_hangup(myrpt->monchannel);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	/* save tx conference number */
	myrpt->txconf = ci.confno;
	/* make a conference for the pseudo */
	ci.chan = 0;
	ci.confno = -1; /* make a new conf */
	ci.confmode = ((myrpt->p.duplex == 2) || (myrpt->p.duplex == 4)) ? DAHDI_CONF_CONFANNMON :
		(DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER);
	/* first put the channel on the conference in announce mode */
	if (ioctl(myrpt->pchannel->fds[0],DAHDI_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		rpt_mutex_unlock(&myrpt->lock);
		ast_hangup(myrpt->pchannel);
		ast_hangup(myrpt->monchannel);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	/* save pseudo channel conference number */
	myrpt->conf = ci.confno;
	/* make a conference for the pseudo */
	ci.chan = 0;
	if ((strstr(myrpt->txchannel->name,"pseudo") == NULL) &&
		(myrpt->zaptxchannel == myrpt->txchannel))
	{
		/* get tx channel's port number */
		if (ioctl(myrpt->txchannel->fds[0],DAHDI_CHANNO,&ci.confno) == -1)
		{
			ast_log(LOG_WARNING, "Unable to set tx channel's chan number\n");
			rpt_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->pchannel);
			ast_hangup(myrpt->monchannel);
			if (myrpt->txchannel != myrpt->rxchannel) 
				ast_hangup(myrpt->txchannel);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
		ci.confmode = DAHDI_CONF_MONITORTX;
	}
	else
	{

		ci.confno = myrpt->txconf;
		ci.confmode = DAHDI_CONF_CONFANNMON;
	}
	/* first put the channel on the conference in announce mode */
	if (ioctl(myrpt->monchannel->fds[0],DAHDI_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode for monitor\n");
		rpt_mutex_unlock(&myrpt->lock);
		ast_hangup(myrpt->pchannel);
		ast_hangup(myrpt->monchannel);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	/* allocate a pseudo-channel thru asterisk */
	myrpt->parrotchannel = ast_request(DAHDI_CHANNEL_NAME,AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!myrpt->parrotchannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		rpt_mutex_unlock(&myrpt->lock);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	ast_set_read_format(myrpt->parrotchannel,AST_FORMAT_SLINEAR);
	ast_set_write_format(myrpt->parrotchannel,AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (myrpt->parrotchannel->cdr)
		ast_set_flag(myrpt->parrotchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	ast_answer(myrpt->parrotchannel);


	/* Telemetry Channel Resources */
	/* allocate a pseudo-channel thru asterisk */
	myrpt->telechannel = ast_request("dahdi",AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!myrpt->telechannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		rpt_mutex_unlock(&myrpt->lock);
		if (myrpt->txchannel != myrpt->rxchannel)
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	ast_set_read_format(myrpt->telechannel,AST_FORMAT_SLINEAR);
	ast_set_write_format(myrpt->telechannel,AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (myrpt->telechannel->cdr)
		ast_set_flag(myrpt->telechannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
 	/* make a conference for the voice/tone telemetry */
	ci.chan = 0;
	ci.confno = -1; // make a new conference
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;
 	/* put the channel on the conference in proper mode */
	if (ioctl(myrpt->telechannel->fds[0],DAHDI_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		rpt_mutex_unlock(&myrpt->lock);
		ast_hangup(myrpt->txpchannel);
		ast_hangup(myrpt->monchannel);
		if (myrpt->txchannel != myrpt->rxchannel)
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	myrpt->teleconf=ci.confno;

	/* make a channel to connect between the telemetry conference process
	   and the main tx audio conference. */
	myrpt->btelechannel = ast_request("dahdi",AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!myrpt->btelechannel)
	{
		fprintf(stderr,"rtp:Failed to obtain pseudo channel for btelechannel\n");
		rpt_mutex_unlock(&myrpt->lock);
		if (myrpt->txchannel != myrpt->rxchannel)
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	ast_set_read_format(myrpt->btelechannel,AST_FORMAT_SLINEAR);
	ast_set_write_format(myrpt->btelechannel,AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (myrpt->btelechannel->cdr)
		ast_set_flag(myrpt->btelechannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	/* make a conference linked to the main tx conference */
	ci.chan = 0;
	ci.confno = myrpt->txconf;
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER;
	/* first put the channel on the conference in proper mode */
	if (ioctl(myrpt->btelechannel->fds[0], DAHDI_SETCONF, &ci) == -1)
	{
		ast_log(LOG_ERROR, "Failed to create btelechannel.\n");
		ast_hangup(myrpt->btelechannel);
		ast_hangup(myrpt->btelechannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}






	/* allocate a pseudo-channel thru asterisk */
	myrpt->voxchannel = ast_request(DAHDI_CHANNEL_NAME,AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!myrpt->voxchannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		rpt_mutex_unlock(&myrpt->lock);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	ast_set_read_format(myrpt->voxchannel,AST_FORMAT_SLINEAR);
	ast_set_write_format(myrpt->voxchannel,AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (myrpt->voxchannel->cdr)
		ast_set_flag(myrpt->voxchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	ast_answer(myrpt->voxchannel);
	/* allocate a pseudo-channel thru asterisk */
	myrpt->txpchannel = ast_request(DAHDI_CHANNEL_NAME,AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!myrpt->txpchannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		rpt_mutex_unlock(&myrpt->lock);
		ast_hangup(myrpt->pchannel);
		ast_hangup(myrpt->monchannel);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (myrpt->txpchannel->cdr)
		ast_set_flag(myrpt->txpchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	ast_answer(myrpt->txpchannel);
	/* make a conference for the tx */
	ci.chan = 0;
	ci.confno = myrpt->txconf;
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER ;
 	/* first put the channel on the conference in proper mode */
	if (ioctl(myrpt->txpchannel->fds[0],DAHDI_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		rpt_mutex_unlock(&myrpt->lock);
		ast_hangup(myrpt->txpchannel);
		ast_hangup(myrpt->monchannel);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	/* if serial io port, open it */
	myrpt->iofd = -1;
	if (myrpt->p.ioport && ((myrpt->iofd = openserial(myrpt,myrpt->p.ioport)) == -1))
	{
		ast_log(LOG_ERROR, "Unable to open %s\n",myrpt->p.ioport);
		rpt_mutex_unlock(&myrpt->lock);
		ast_hangup(myrpt->pchannel);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		pthread_exit(NULL);
	}
	/* Now, the idea here is to copy from the physical rx channel buffer
	   into the pseudo tx buffer, and from the pseudo rx buffer into the 
	   tx channel buffer */
	myrpt->links.next = &myrpt->links;
	myrpt->links.prev = &myrpt->links;
	myrpt->tailtimer = 0;
	myrpt->totimer = myrpt->p.totime;
	myrpt->tmsgtimer = myrpt->p.tailmessagetime;
	myrpt->idtimer = myrpt->p.politeid;
	myrpt->elketimer = myrpt->p.elke;
	myrpt->mustid = myrpt->tailid = 0;
	myrpt->callmode = 0;
	myrpt->tounkeyed = 0;
	myrpt->tonotify = 0;
	myrpt->retxtimer = 0;
	myrpt->rerxtimer = 0;
	myrpt->skedtimer = 0;
	myrpt->tailevent = 0;
	lasttx = 0;
	lastexttx = 0;
	myrpt->keyed = 0;
	myrpt->txkeyed = 0;
	time(&myrpt->lastkeyedtime);
	myrpt->lastkeyedtime -= RPT_LOCKOUT_SECS;
	time(&myrpt->lasttxkeyedtime);
	myrpt->lasttxkeyedtime -= RPT_LOCKOUT_SECS;
	idtalkover = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->name, "idtalkover");
	myrpt->dtmfidx = -1;
	myrpt->dtmfbuf[0] = 0;
	myrpt->rem_dtmfidx = -1;
	myrpt->rem_dtmfbuf[0] = 0;
	myrpt->dtmf_time = 0;
	myrpt->rem_dtmf_time = 0;
	myrpt->inpadtest = 0;
	myrpt->disgorgetime = 0;
	myrpt->lastnodewhichkeyedusup[0] = '\0';
	myrpt->dailytxtime = 0;
	myrpt->totaltxtime = 0;
	myrpt->dailykeyups = 0;
	myrpt->totalkeyups = 0;
	myrpt->dailykerchunks = 0;
	myrpt->totalkerchunks = 0;
	myrpt->dailyexecdcommands = 0;
	myrpt->totalexecdcommands = 0;
	myrpt->timeouts = 0;
	myrpt->exten[0] = '\0';
	myrpt->lastdtmfcommand[0] = '\0';
	voxinit_rpt(myrpt,1);
	myrpt->wasvox = 0;
	myrpt->linkactivityflag = 0;	
	myrpt->linkactivitytimer = 0;
	myrpt->vote_counter=10;
	myrpt->rptinactwaskeyedflag = 0;
	myrpt->rptinacttimer = 0;
	if (myrpt->p.rxburstfreq)
	{
		tone_detect_init(&myrpt->burst_tone_state,myrpt->p.rxburstfreq,
			myrpt->p.rxbursttime,myrpt->p.rxburstthreshold);
	}
	if (myrpt->p.startupmacro)
	{
		snprintf(myrpt->macrobuf,MAXMACRO - 1,"PPPP%s",myrpt->p.startupmacro);
	}
	/* @@@@@@@ UNLOCK @@@@@@@ */
	rpt_mutex_unlock(&myrpt->lock);
	val = 1;
	ast_channel_setoption(myrpt->rxchannel,AST_OPTION_RELAXDTMF,&val,sizeof(char),0);
	val = 1;
	ast_channel_setoption(myrpt->rxchannel,AST_OPTION_TONE_VERIFY,&val,sizeof(char),0);
	if (myrpt->p.archivedir) donodelog(myrpt,"STARTUP");
	dtmfed = 0;
	if (myrpt->remoterig && !ISRIG_RTX(myrpt->remoterig)) setrem(myrpt);
	/* wait for telem to be done */
	while((ms >= 0) && (myrpt->tele.next != &myrpt->tele))
		if (ast_safe_sleep(myrpt->rxchannel,50) == -1) ms = -1;
	lastmyrx = 0;
	myfirst = 0;
	myrpt->lastitx = -1;
	rpt_update_boolean(myrpt,"RPT_RXKEYED",-1);
	rpt_update_boolean(myrpt,"RPT_TXKEYED",-1);
	rpt_update_boolean(myrpt,"RPT_ETXKEYED",-1);
	rpt_update_boolean(myrpt,"RPT_AUTOPATCHUP",-1);
	rpt_update_boolean(myrpt,"RPT_NUMLINKS",-1);
	rpt_update_boolean(myrpt,"RPT_LINKS",-1);
	myrpt->ready = 1;	
	while (ms >= 0)
	{
		struct ast_frame *f,*f1,*f2;
		struct ast_channel *cs[300],*cs1[300];
		int totx=0,elap=0,n,x,toexit=0;

		/* DEBUG Dump */
		if((myrpt->disgorgetime) && (time(NULL) >= myrpt->disgorgetime)){
			struct rpt_link *zl;
			struct rpt_tele *zt;

			myrpt->disgorgetime = 0;
			ast_log(LOG_NOTICE,"********** Variable Dump Start (app_rpt) **********\n");
			ast_log(LOG_NOTICE,"totx = %d\n",totx);
			ast_log(LOG_NOTICE,"myrpt->remrx = %d\n",myrpt->remrx);
			ast_log(LOG_NOTICE,"lasttx = %d\n",lasttx);
			ast_log(LOG_NOTICE,"lastexttx = %d\n",lastexttx);
			ast_log(LOG_NOTICE,"elap = %d\n",elap);
			ast_log(LOG_NOTICE,"toexit = %d\n",toexit);

			ast_log(LOG_NOTICE,"myrpt->keyed = %d\n",myrpt->keyed);
			ast_log(LOG_NOTICE,"myrpt->localtx = %d\n",myrpt->localtx);
			ast_log(LOG_NOTICE,"myrpt->callmode = %d\n",myrpt->callmode);
			ast_log(LOG_NOTICE,"myrpt->mustid = %d\n",myrpt->mustid);
			ast_log(LOG_NOTICE,"myrpt->tounkeyed = %d\n",myrpt->tounkeyed);
			ast_log(LOG_NOTICE,"myrpt->tonotify = %d\n",myrpt->tonotify);
			ast_log(LOG_NOTICE,"myrpt->retxtimer = %ld\n",myrpt->retxtimer);
			ast_log(LOG_NOTICE,"myrpt->totimer = %d\n",myrpt->totimer);
			ast_log(LOG_NOTICE,"myrpt->tailtimer = %d\n",myrpt->tailtimer);
			ast_log(LOG_NOTICE,"myrpt->tailevent = %d\n",myrpt->tailevent);
			ast_log(LOG_NOTICE,"myrpt->linkactivitytimer = %d\n",myrpt->linkactivitytimer);
			ast_log(LOG_NOTICE,"myrpt->linkactivityflag = %d\n",(int) myrpt->linkactivityflag);
			ast_log(LOG_NOTICE,"myrpt->rptinacttimer = %d\n",myrpt->rptinacttimer);
			ast_log(LOG_NOTICE,"myrpt->rptinactwaskeyedflag = %d\n",(int) myrpt->rptinactwaskeyedflag);
			ast_log(LOG_NOTICE,"myrpt->p.s[myrpt->p.sysstate_cur].sleepena = %d\n",myrpt->p.s[myrpt->p.sysstate_cur].sleepena);
			ast_log(LOG_NOTICE,"myrpt->sleeptimer = %d\n",(int) myrpt->sleeptimer);
			ast_log(LOG_NOTICE,"myrpt->sleep = %d\n",(int) myrpt->sleep);
			ast_log(LOG_NOTICE,"myrpt->sleepreq = %d\n",(int) myrpt->sleepreq);
			ast_log(LOG_NOTICE,"myrpt->p.parrotmode = %d\n",(int) myrpt->p.parrotmode);
			ast_log(LOG_NOTICE,"myrpt->parrotonce = %d\n",(int) myrpt->parrotonce);


			zl = myrpt->links.next;
              		while(zl != &myrpt->links){
				ast_log(LOG_NOTICE,"*** Link Name: %s ***\n",zl->name);
				ast_log(LOG_NOTICE,"        link->lasttx %d\n",zl->lasttx);
				ast_log(LOG_NOTICE,"        link->lastrx %d\n",zl->lastrx);
				ast_log(LOG_NOTICE,"        link->connected %d\n",zl->connected);
				ast_log(LOG_NOTICE,"        link->hasconnected %d\n",zl->hasconnected);
				ast_log(LOG_NOTICE,"        link->outbound %d\n",zl->outbound);
				ast_log(LOG_NOTICE,"        link->disced %d\n",zl->disced);
				ast_log(LOG_NOTICE,"        link->killme %d\n",zl->killme);
				ast_log(LOG_NOTICE,"        link->disctime %ld\n",zl->disctime);
				ast_log(LOG_NOTICE,"        link->retrytimer %ld\n",zl->retrytimer);
				ast_log(LOG_NOTICE,"        link->retries = %d\n",zl->retries);
				ast_log(LOG_NOTICE,"        link->reconnects = %d\n",zl->reconnects);
				ast_log(LOG_NOTICE,"        link->newkey = %d\n",zl->newkey);
                        	zl = zl->next;
                	}
                                                                                                                               
			zt = myrpt->tele.next;
			if(zt != &myrpt->tele)
				ast_log(LOG_NOTICE,"*** Telemetry Queue ***\n");
              		while(zt != &myrpt->tele){
				ast_log(LOG_NOTICE,"        Telemetry mode: %d\n",zt->mode);
                        	zt = zt->next;
                	}
			ast_log(LOG_NOTICE,"******* Variable Dump End (app_rpt) *******\n");

		}	


		if (myrpt->reload)
		{
			struct rpt_tele *telem;

			rpt_mutex_lock(&myrpt->lock);
			telem = myrpt->tele.next;
			while(telem != &myrpt->tele)
			{
				ast_softhangup(telem->chan,AST_SOFTHANGUP_DEV);
				telem = telem->next;
			}
			myrpt->reload = 0;
			rpt_mutex_unlock(&myrpt->lock);
			usleep(10000);
			/* find our index, and load the vars */
			for(i = 0; i < nrpts; i++)
			{
				if (&rpt_vars[i] == myrpt)
				{
					load_rpt_vars(i,0);
					break;
				}
			}
		}

		if (ast_check_hangup(myrpt->rxchannel)) break;
		if (ast_check_hangup(myrpt->txchannel)) break;
		if (ast_check_hangup(myrpt->pchannel)) break;
		if (ast_check_hangup(myrpt->monchannel)) break;
		if (myrpt->parrotchannel && 
			ast_check_hangup(myrpt->parrotchannel)) break;
		if (myrpt->voxchannel && 
			ast_check_hangup(myrpt->voxchannel)) break;
		if (ast_check_hangup(myrpt->txpchannel)) break;
		if (myrpt->zaptxchannel && ast_check_hangup(myrpt->zaptxchannel)) break;

		time(&t);
		while(t >= (myrpt->lastgpstime + GPS_UPDATE_SECS))
		{
			myrpt->lastgpstime = t;
			fp = fopen(GPSFILE,"r");
			if (!fp) break;
			if (fstat(fileno(fp),&mystat) == -1) break;
			if (mystat.st_size >= 100) break;
			elev[0] = 0;
			if (fscanf(fp,"%u %s %s %s",&u,lat,lon,elev) < 3) break;
			fclose(fp);
			was = (time_t) u;
			if ((was + GPS_VALID_SECS) < t) break;
			sprintf(tmpstr,"G %s %s %s %s",
				myrpt->name,lat,lon,elev);
			rpt_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
		myrpt->voteremrx = 0;		/* no voter remotes keyed */
			while(l != &myrpt->links)
			{
				if (l->chan) ast_sendtext(l->chan,tmpstr);
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
		/* @@@@@@@ LOCK @@@@@@@ */
		rpt_mutex_lock(&myrpt->lock);

		/* If someone's connected, and they're transmitting from their end to us, set remrx true */
		l = myrpt->links.next;
		myrpt->remrx = 0;
		while(l != &myrpt->links)
		{
			if (l->lastrx){
				myrpt->remrx = 1;
				if(l->voterlink)myrpt->voteremrx=1;
				if ((l->name[0] > '0') && (l->name[0] <= '9')) /* Ignore '0' nodes */
					strcpy(myrpt->lastnodewhichkeyedusup, l->name); /* Note the node which is doing the key up */
			}
			l = l->next;
		}
		if(myrpt->p.s[myrpt->p.sysstate_cur].sleepena){ /* If sleep mode enabled */
			if(myrpt->remrx){ /* signal coming from net wakes up system */
				myrpt->sleeptimer=myrpt->p.sleeptime; /* reset sleep timer */
				if(myrpt->sleep){ /* if asleep, then awake */
					myrpt->sleep = 0;
				}
			}
			else if(myrpt->keyed){ /* if signal on input */
				if(!myrpt->sleep){ /* if not sleeping */
					myrpt->sleeptimer=myrpt->p.sleeptime; /* reset sleep timer */
				}
			}
		
			if(myrpt->sleep)
				myrpt->localtx = 0; /* No RX if asleep */
			else
				myrpt->localtx = myrpt->keyed; /* Set localtx to keyed state if awake */
		}
		else{
			myrpt->localtx = myrpt->keyed; /* If sleep disabled, just copy keyed state to localrx */
		}
		/* Create a "must_id" flag for the cleanup ID */		
		if(myrpt->p.idtime) /* ID time must be non-zero */
			myrpt->mustid |= (myrpt->idtimer) && (myrpt->keyed || myrpt->remrx) ;
		if(myrpt->keyed || myrpt->remrx){
			/* Set the inactivity was keyed flag and reset its timer */
			myrpt->rptinactwaskeyedflag = 1;
			myrpt->rptinacttimer = 0;
		}
		/* Build a fresh totx from myrpt->keyed and autopatch activated */
		/* If full duplex, add local tx to totx */


		if ((myrpt->p.duplex > 1) && (!myrpt->patchvoxalways))
		{
			totx = myrpt->callmode;
		}
		else
		{
			int myrx = myrpt->localtx || myrpt->remrx || (!myrpt->callmode);

			if (lastmyrx != myrx)
			{
				if (myrpt->p.duplex < 2) voxinit_rpt(myrpt,!myrx);
				lastmyrx = myrx;
			}
			totx = 0;
			if (myrpt->callmode && (myrpt->voxtotimer <= 0))
			{
				if (myrpt->voxtostate)
				{
					myrpt->voxtotimer = myrpt->p.voxtimeout_ms;
					myrpt->voxtostate = 0;
				}				
				else
				{
					myrpt->voxtotimer = myrpt->p.voxrecover_ms;
					myrpt->voxtostate = 1;
				}
			}
			if (!myrpt->voxtostate)
				totx = myrpt->callmode && myrpt->wasvox;
		}
		if (myrpt->p.duplex > 1) 
		{
			totx = totx || myrpt->localtx;
		}

		/* Traverse the telemetry list to see what's queued */
		identqueued = 0;
		localmsgqueued = 0;
		othertelemqueued = 0;
		tailmessagequeued = 0;
		ctqueued = 0;
		telem = myrpt->tele.next;
		while(telem != &myrpt->tele)
		{
			if (telem->mode == SETREMOTE)
			{
				telem = telem->next;
				continue;
			}
			if((telem->mode == ID) || (telem->mode == IDTALKOVER)){
				identqueued = 1; /* Identification telemetry */
			}
			else if(telem->mode == TAILMSG)
			{
				tailmessagequeued = 1; /* Tail message telemetry */
			}
			else if((telem->mode == STATS_TIME_LOCAL) || (telem->mode == LOCALPLAY))
			{
				localmsgqueued = 1; /* Local message */
			}
			else
			{
				if ((telem->mode != UNKEY) && (telem->mode != LINKUNKEY))
					othertelemqueued = 1;  /* Other telemetry */
				else
					ctqueued = 1; /* Courtesy tone telemetry */
			}
			telem = telem->next;
		}
	
		/* Add in any "other" telemetry, unless specified otherwise */
		if (!myrpt->p.notelemtx) totx = totx || othertelemqueued;
		/* Update external (to links) transmitter PTT state with everything but */
		/* ID, CT, local messages, and tailmessage telemetry */
		myrpt->exttx = totx;
		if (myrpt->localoverride) totx = 1;
		totx = totx || myrpt->dtmf_local_timer;
		/* If half or 3/4 duplex, add localtx to external link tx */
		if (myrpt->p.duplex < 2) myrpt->exttx = myrpt->exttx || myrpt->localtx;
		/* Add in ID telemetry to local transmitter */
		totx = totx || myrpt->remrx;
		/* If 3/4 or full duplex, add in ident, CT telemetry, and local messages */
		if (myrpt->p.duplex > 0)
			totx = totx || identqueued || ctqueued;
		/* If 3/4 or full duplex, or half-duplex with link to link, add in localmsg ptt */
		if (myrpt->p.duplex > 0 || myrpt->p.linktolink)
			totx = totx || localmsgqueued;
		totx = totx || is_paging(myrpt);
		/* If full duplex, add local dtmf stuff active */
		if (myrpt->p.duplex > 1) 
		{
			totx = totx || /* (myrpt->dtmfidx > -1) || */
				(myrpt->cmdnode[0] && strcmp(myrpt->cmdnode,"aprstt"));
		}
		/* add in parrot stuff */
		totx = totx || (myrpt->parrotstate > 1);
		/* Reset time out timer variables if there is no activity */
		if (!totx) 
		{
			myrpt->totimer = myrpt->p.totime;
			myrpt->tounkeyed = 0;
			myrpt->tonotify = 0;
		}
		else{
			myrpt->tailtimer = myrpt->p.s[myrpt->p.sysstate_cur].alternatetail ?
				myrpt->p.althangtime : /* Initialize tail timer */
				myrpt->p.hangtime;

		}
		/* if in 1/2 or 3/4 duplex, give rx priority */
		if ((myrpt->p.duplex < 2) && (myrpt->keyed) && (!myrpt->p.linktolink) && (!myrpt->p.dias)) totx = 0;
		/* Disable the local transmitter if we are timed out */
		totx = totx && myrpt->totimer;
		/* if timed-out and not said already, say it */
		if ((!myrpt->totimer) && (!myrpt->tonotify))
		{
			myrpt->tonotify = 1;
			myrpt->timeouts++;
			rpt_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt,TIMEOUT,NULL);
			rpt_mutex_lock(&myrpt->lock);
		}

		/* If unkey and re-key, reset time out timer */
		if ((!totx) && (!myrpt->totimer) && (!myrpt->tounkeyed) && (!myrpt->keyed))
		{
			myrpt->tounkeyed = 1;
		}
		if ((!totx) && (!myrpt->totimer) && myrpt->tounkeyed && myrpt->keyed)
		{
			myrpt->totimer = myrpt->p.totime;
			myrpt->tounkeyed = 0;
			myrpt->tonotify = 0;
			rpt_mutex_unlock(&myrpt->lock);
			continue;
		}
		/* if timed-out and in circuit busy after call */
		if ((!totx) && (!myrpt->totimer) && (myrpt->callmode == 4))
		{
		    if(debug)
				ast_log(LOG_NOTICE, "timed-out and in circuit busy after call\n");
			myrpt->callmode = 0;
			myrpt->macropatch=0;
			channel_revert(myrpt);
		}
		/* get rid of tail if timed out */
		if (!myrpt->totimer) myrpt->tailtimer = 0;
		/* if not timed-out, add in tail */
		if (myrpt->totimer) totx = totx || myrpt->tailtimer;
		/* If user or links key up or are keyed up over standard ID, switch to talkover ID, if one is defined */
		/* If tail message, kill the message if someone keys up over it */ 
		if ((myrpt->keyed || myrpt->remrx || myrpt->localoverride) && ((identqueued && idtalkover) || (tailmessagequeued))) {
			int hasid = 0,hastalkover = 0;

			telem = myrpt->tele.next;
			while(telem != &myrpt->tele){
				if(telem->mode == ID && !telem->killed){
					if (telem->chan) ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV); /* Whoosh! */
					telem->killed = 1;
					hasid = 1;
				}
				if(telem->mode == TAILMSG && !telem->killed){
                                        if (telem->chan) ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV); /* Whoosh! */
					telem->killed = 1;
                                }
				if (telem->mode == IDTALKOVER) hastalkover = 1;
				telem = telem->next;
			}
			if(hasid && (!hastalkover)){
				if(debug >= 6)
					ast_log(LOG_NOTICE,"Tracepoint IDTALKOVER: in rpt()\n");
				rpt_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt, IDTALKOVER, NULL); /* Start Talkover ID */
				rpt_mutex_lock(&myrpt->lock);
			}
		}
		/* Try to be polite */
		/* If the repeater has been inactive for longer than the ID time, do an initial ID in the tail*/
		/* If within 30 seconds of the time to ID, try do it in the tail */
		/* else if at ID time limit, do it right over the top of them */
		/* If beaconing is enabled, always id when the timer expires */
		/* Lastly, if the repeater has been keyed, and the ID timer is expired, do a clean up ID */
		if(((myrpt->mustid)||(myrpt->p.beaconing)) && (!myrpt->idtimer))
			queue_id(myrpt);

		if ((myrpt->p.idtime && totx && (!myrpt->exttx) &&
			 (myrpt->idtimer <= myrpt->p.politeid) && myrpt->tailtimer)) /* ID time must be non-zero */ 
			{
				myrpt->tailid = 1;
			}

		/* If tail timer expires, then check for tail messages */

		if(myrpt->tailevent){
			myrpt->tailevent = 0;
			if(myrpt->tailid){
				totx = 1;
				queue_id(myrpt);
			}
			else if ((myrpt->p.tailmessages[0]) &&
				(myrpt->p.tailmessagetime) && (myrpt->tmsgtimer == 0)){
					totx = 1;
					myrpt->tmsgtimer = myrpt->p.tailmessagetime;	
					rpt_mutex_unlock(&myrpt->lock);
					rpt_telemetry(myrpt, TAILMSG, NULL);
					rpt_mutex_lock(&myrpt->lock);
			}	
		}

		/* Main TX control */

		/* let telemetry transmit anyway (regardless of timeout) */
		if (myrpt->p.duplex > 0) totx = totx || (myrpt->tele.next != &myrpt->tele);
		totx = totx && !myrpt->p.s[myrpt->p.sysstate_cur].txdisable;
		myrpt->txrealkeyed = totx;
		totx = totx || (!AST_LIST_EMPTY(&myrpt->txq));
		/* if in 1/2 or 3/4 duplex, give rx priority */
		if ((myrpt->p.duplex < 2) && (!myrpt->p.linktolink) && (!myrpt->p.dias) && (myrpt->keyed)) totx = 0;
		if (myrpt->p.elke && (myrpt->elketimer > myrpt->p.elke)) totx = 0;
		if (totx && (!lasttx))
		{
			char mydate[100],myfname[512];
			time_t myt;

			if (myrpt->monstream) ast_closestream(myrpt->monstream);
			myrpt->monstream = 0;
			if (myrpt->p.archivedir)
			{
				long blocksleft;

				time(&myt);
				strftime(mydate,sizeof(mydate) - 1,"%Y%m%d%H%M%S",
					localtime(&myt));
				sprintf(myfname,"%s/%s/%s",myrpt->p.archivedir,
					myrpt->name,mydate);
				myrpt->monstream = ast_writefile(myfname,"wav49",
					"app_rpt Air Archive",O_CREAT | O_APPEND,0,0600);
				if (myrpt->p.monminblocks)
				{
					blocksleft = diskavail(myrpt);
					if (blocksleft >= myrpt->p.monminblocks)
						donodelog(myrpt,"TXKEY,MAIN");
				} else donodelog(myrpt,"TXKEY,MAIN");
			}
			rpt_update_boolean(myrpt,"RPT_TXKEYED",1);
			lasttx = 1;
			myrpt->txkeyed = 1;
			time(&myrpt->lasttxkeyedtime);
			myrpt->dailykeyups++;
			myrpt->totalkeyups++;
			rpt_mutex_unlock(&myrpt->lock);
			if (strncasecmp(myrpt->txchannel->name,"Zap/Pseudo",10))
			{
				ast_indicate(myrpt->txchannel,
					AST_CONTROL_RADIO_KEY);
			}
			rpt_mutex_lock(&myrpt->lock);
		}
		if ((!totx) && lasttx)
		{
			if (myrpt->monstream) ast_closestream(myrpt->monstream);
			myrpt->monstream = NULL;

			lasttx = 0;
			myrpt->txkeyed = 0;
			time(&myrpt->lasttxkeyedtime);
			rpt_mutex_unlock(&myrpt->lock);
			if (strncasecmp(myrpt->txchannel->name,"Zap/Pseudo",10))
			{
				ast_indicate(myrpt->txchannel,
					AST_CONTROL_RADIO_UNKEY);
			}
			rpt_mutex_lock(&myrpt->lock);
			donodelog(myrpt,"TXUNKEY,MAIN");
			rpt_update_boolean(myrpt,"RPT_TXKEYED",0);
			if(myrpt->p.s[myrpt->p.sysstate_cur].sleepena){
				if(myrpt->sleepreq){
					myrpt->sleeptimer = 0;
					myrpt->sleepreq = 0;
					myrpt->sleep = 1;
				}
			}
		}
		time(&t);
		/* if DTMF timeout */
		if (((!myrpt->cmdnode[0]) || (!strcmp(myrpt->cmdnode,"aprstt"))) && 
			(myrpt->dtmfidx >= 0) && ((myrpt->dtmf_time + DTMF_TIMEOUT) < t))
		{
			cancel_pfxtone(myrpt);
			myrpt->inpadtest = 0;
			myrpt->dtmfidx = -1;
			myrpt->cmdnode[0] = 0;
			myrpt->dtmfbuf[0] = 0;
		}			
		/* if remote DTMF timeout */
		if ((myrpt->rem_dtmfidx >= 0) && ((myrpt->rem_dtmf_time + DTMF_TIMEOUT) < t))
		{
			myrpt->inpadtest = 0;
			myrpt->rem_dtmfidx = -1;
			myrpt->rem_dtmfbuf[0] = 0;
		}	

		if (myrpt->exttx && myrpt->parrotchannel && 
			(myrpt->p.parrotmode || myrpt->parrotonce) && (!myrpt->parrotstate))
		{
			char myfname[300];

			ci.confno = myrpt->conf;
			ci.confmode = DAHDI_CONF_CONFANNMON;
			ci.chan = 0;

			/* first put the channel on the conference in announce mode */
			if (ioctl(myrpt->parrotchannel->fds[0],DAHDI_SETCONF,&ci) == -1)
			{
				ast_log(LOG_WARNING, "Unable to set conference mode for parrot\n");
				ast_mutex_unlock(&myrpt->lock);
				break;
			}

			sprintf(myfname,PARROTFILE,myrpt->name,myrpt->parrotcnt);
			strcat(myfname,".wav");
			unlink(myfname);			
			sprintf(myfname,PARROTFILE,myrpt->name,myrpt->parrotcnt);
			myrpt->parrotstate = 1;
			myrpt->parrottimer = myrpt->p.parrottime;
			if (myrpt->parrotstream) 
				ast_closestream(myrpt->parrotstream);
			myrpt->parrotstream = NULL;
			myrpt->parrotstream = ast_writefile(myfname,"wav",
				"app_rpt Parrot",O_CREAT | O_TRUNC,0,0600);
		}

		if (myrpt->exttx != lastexttx)
		{
			lastexttx = myrpt->exttx;
			rpt_update_boolean(myrpt,"RPT_ETXKEYED",lastexttx);
		}

		if (((myrpt->callmode != 0)) != lastpatchup)
		{
			lastpatchup = ((myrpt->callmode != 0));
			rpt_update_boolean(myrpt,"RPT_AUTOPATCHUP",lastpatchup);
		}

		/* Reconnect */

		l = myrpt->links.next;
		while(l != &myrpt->links)
		{
			if (l->killme)
			{
				/* remove from queue */
				remque((struct qelem *) l);
				if (!strcmp(myrpt->cmdnode,l->name))
					myrpt->cmdnode[0] = 0;
				rpt_mutex_unlock(&myrpt->lock);
				/* hang-up on call to device */
				if (l->chan) ast_hangup(l->chan);
				ast_hangup(l->pchan);
				ast_free(l);
				rpt_mutex_lock(&myrpt->lock);
				/* re-start link traversal */
				l = myrpt->links.next;
				continue;
			}
			l = l->next;
		}
		x = myrpt->remrx || myrpt->localtx || myrpt->callmode || myrpt->parrotstate;
		if (x != myrpt->lastitx)
		{
			char str[16];
			
			myrpt->lastitx = x;
			if (myrpt->p.itxctcss)
			{
				if ((strncasecmp(myrpt->rxchannel->name,"zap/", 4) == 0) || 
				    (strncasecmp(myrpt->rxchannel->name,"dahdi/", 6) == 0))
				{
					struct dahdi_radio_param r;

					memset(&r,0,sizeof(struct dahdi_radio_param));
					r.radpar = DAHDI_RADPAR_NOENCODE;
					r.data = !x;
					ioctl(myrpt->zaprxchannel->fds[0],DAHDI_RADIO_SETPARAM,&r);
				}
				if ((strncasecmp(myrpt->rxchannel->name,"radio/", 6) == 0) || 
				    (strncasecmp(myrpt->rxchannel->name,"simpleusb/", 10) == 0))
				{
					sprintf(str,"TXCTCSS %d",!(!x));
					ast_sendtext(myrpt->rxchannel,str);
				}
			}
		}
		n = 0;
		cs[n++] = myrpt->rxchannel;
		cs[n++] = myrpt->pchannel;
		cs[n++] = myrpt->monchannel;
		cs[n++] = myrpt->telechannel;
		cs[n++] = myrpt->btelechannel;

		if (myrpt->parrotchannel) cs[n++] = myrpt->parrotchannel;
		if (myrpt->voxchannel) cs[n++] = myrpt->voxchannel;
		cs[n++] = myrpt->txpchannel;
		if (myrpt->txchannel != myrpt->rxchannel) cs[n++] = myrpt->txchannel;
		if (myrpt->zaptxchannel != myrpt->txchannel)
			cs[n++] = myrpt->zaptxchannel;
		l = myrpt->links.next;
		while(l != &myrpt->links)
		{
			if ((!l->killme) && (!l->disctime) && l->chan)
			{
				cs[n++] = l->chan;
				cs[n++] = l->pchan;
			}
			l = l->next;
		}
		if ((myrpt->topkeystate == 1) && 
		    ((t - myrpt->topkeytime) > TOPKEYWAIT))
		{
			myrpt->topkeystate = 2;
			qsort(myrpt->topkey,TOPKEYN,sizeof(struct rpt_topkey),
				topcompar);
		}
		/* @@@@@@ UNLOCK @@@@@@@@ */
		rpt_mutex_unlock(&myrpt->lock);

		if (myrpt->topkeystate == 2)
		{
			rpt_telemetry(myrpt,TOPKEY,NULL);
			myrpt->topkeystate = 3;
		}
		ms = MSWAIT;
		for(x = 0; x < n; x++)
		{
			int s = -(-x - myrpt->scram - 1) % n;
			cs1[x] = cs[s];
		}
		myrpt->scram++;
		who = ast_waitfor_n(cs1,n,&ms);
		if (who == NULL) ms = 0;
		elap = MSWAIT - ms;
		/* @@@@@@ LOCK @@@@@@@ */
		rpt_mutex_lock(&myrpt->lock);
		l = myrpt->links.next;
		while(l != &myrpt->links)
		{
			int myrx,mymaxct;
			
			
			if (l->chan && l->thisconnected && (!AST_LIST_EMPTY(&l->textq)))
			{
				f = AST_LIST_REMOVE_HEAD(&l->textq,frame_list);
				ast_write(l->chan,f);
				ast_frfree(f);
			}

			if (l->rxlingertimer) l->rxlingertimer -= elap;
			if (l->rxlingertimer < 0) l->rxlingertimer = 0;

			x = l->newkeytimer;
			if (l->newkeytimer) l->newkeytimer -= elap;
			if (l->newkeytimer < 0) l->newkeytimer = 0;

			if ((x > 0) && (!l->newkeytimer))
			{
				if (l->thisconnected)
				{
					if (l->newkey == 2) l->newkey = 0;
				}
				else
				{
					l->newkeytimer = NEWKEYTIME;
				}
			}
			if ((l->linkmode > 1) && (l->linkmode < 0x7ffffffe)) 
			{
				l->linkmode -= elap;
				if (l->linkmode < 1) l->linkmode = 1;
			}
			if ((l->newkey == 2) && l->lastrealrx && (!l->rxlingertimer))
			{
				l->lastrealrx = 0;
				l->rerxtimer = 0;
				if (l->lastrx1)
				{
					if (myrpt->p.archivedir)
					{
						char str[512];
							sprintf(str,"RXUNKEY,%s",l->name);
						donodelog(myrpt,str);
					}
					l->lastrx1 = 0;
					rpt_update_links(myrpt);
					time(&l->lastunkeytime);
					if(myrpt->p.duplex) 
						rpt_telemetry(myrpt,LINKUNKEY,l);
				}
			}				

			if (l->voxtotimer) l->voxtotimer -= elap;
			if (l->voxtotimer < 0) l->voxtotimer = 0;

			if (l->lasttx != l->lasttx1)
			{
				if ((!l->phonemode) || (!l->phonevox)) voxinit_link(l,!l->lasttx);
				l->lasttx1 = l->lasttx;
			}
			myrx = l->lastrealrx;
			if ((l->phonemode) && (l->phonevox))
			{
				myrx = myrx || (!AST_LIST_EMPTY(&l->rxq));
				if (l->voxtotimer <= 0)
				{
					if (l->voxtostate)
					{
						l->voxtotimer = myrpt->p.voxtimeout_ms;
						l->voxtostate = 0;
					}				
					else
					{
						l->voxtotimer = myrpt->p.voxrecover_ms;
						l->voxtostate = 1;
					}
				}
				if (!l->voxtostate)
					myrx = myrx || l->wasvox ;
			}
			l->lastrx = myrx;
			if (l->linklisttimer)
			{
				l->linklisttimer -= elap;
				if (l->linklisttimer < 0) l->linklisttimer = 0;
			}
			if ((!l->linklisttimer) && (l->name[0] != '0') && (!l->isremote))
			{
				struct	ast_frame lf;

				memset(&lf,0,sizeof(lf));
				lf.frametype = AST_FRAME_TEXT;
				lf.subclass = 0;
				lf.offset = 0;
				lf.mallocd = 0;
				lf.samples = 0;
				l->linklisttimer = LINKLISTTIME;
				strcpy(lstr,"L ");
				__mklinklist(myrpt,l,lstr + 2,0);
				if (l->chan)
				{
					lf.datalen = strlen(lstr) + 1;
					AST_FRAME_DATA(lf) = lstr;
					rpt_qwrite(l,&lf);
					if (debug > 6) ast_log(LOG_NOTICE,
						"@@@@ node %s sent node string %s to node %s\n",
							myrpt->name,lstr,l->name);
				}
			}
			if (l->newkey == 1)
			{
				if ((l->retxtimer += elap) >= REDUNDANT_TX_TIME)
				{
					l->retxtimer = 0;
					if (l->chan && l->phonemode == 0) 
					{
						if (l->lasttx)
							ast_indicate(l->chan,AST_CONTROL_RADIO_KEY);
						else
							ast_indicate(l->chan,AST_CONTROL_RADIO_UNKEY);
					}
				}
				if ((l->rerxtimer += elap) >= (REDUNDANT_TX_TIME * 5))
				{
					if (debug == 7) printf("@@@@ rx un-key\n");
					l->lastrealrx = 0;
					l->rerxtimer = 0;
					if (l->lastrx1)
					{
						if (myrpt->p.archivedir)
						{
							char str[512];
	
							sprintf(str,"RXUNKEY(T),%s",l->name);
							donodelog(myrpt,str);
						}
						if(myrpt->p.duplex) 
							rpt_telemetry(myrpt,LINKUNKEY,l);
						l->lastrx1 = 0;
						rpt_update_links(myrpt);
					}
				}
			}
			if (l->disctime) /* Disconnect timer active on a channel ? */
			{
				l->disctime -= elap;
				if (l->disctime <= 0) /* Disconnect timer expired on inbound channel ? */
					l->disctime = 0; /* Yep */
			}

			if (l->retrytimer)
			{
				l->retrytimer -= elap;
				if (l->retrytimer < 0) l->retrytimer = 0;
			}

			/* Tally connect time */
			l->connecttime += elap;

			/* ignore non-timing channels */
			if (l->elaptime < 0)
			{
				l = l->next;
				continue;
			}
			l->elaptime += elap;
			mymaxct = MAXCONNECTTIME;
			/* if connection has taken too long */
			if ((l->elaptime > mymaxct) && 
			   ((!l->chan) || (l->chan->_state != AST_STATE_UP)))
			{
				l->elaptime = 0;
				rpt_mutex_unlock(&myrpt->lock);
				if (l->chan) ast_softhangup(l->chan,AST_SOFTHANGUP_DEV);
				rpt_mutex_lock(&myrpt->lock);
				break;
			}
			if ((!l->chan) && (!l->retrytimer) && l->outbound && 
				(l->retries++ < l->max_retries) && (l->hasconnected))
			{
				if (l->chan) ast_hangup(l->chan);
				l->chan = 0;
				rpt_mutex_unlock(&myrpt->lock);
				if ((l->name[0] > '0') && (l->name[0] <= '9') && (!l->isremote))
				{
					if (attempt_reconnect(myrpt,l) == -1)
					{
						l->retrytimer = RETRY_TIMER_MS;
					} 
				}
				else 
				{
					l->retries = l->max_retries + 1;
				}

				rpt_mutex_lock(&myrpt->lock);
				break;
			}
			if ((!l->chan) && (!l->retrytimer) && l->outbound &&
				(l->retries >= l->max_retries))
			{
				/* remove from queue */
				remque((struct qelem *) l);
				if (!strcmp(myrpt->cmdnode,l->name))
					myrpt->cmdnode[0] = 0;
				rpt_mutex_unlock(&myrpt->lock);
				if (l->name[0] != '0')
				{
					if (!l->hasconnected)
						rpt_telemetry(myrpt,CONNFAIL,l);
					else rpt_telemetry(myrpt,REMDISC,l);
				}
				if (l->hasconnected) rpt_update_links(myrpt);
				if (myrpt->p.archivedir)
				{
					char str[512];

					if (!l->hasconnected)
						sprintf(str,"LINKFAIL,%s",l->name);
					else
					{
						sprintf(str,"LINKDISC,%s",l->name);
					}
					donodelog(myrpt,str);
				}
				/* hang-up on call to device */
				ast_hangup(l->pchan);
				ast_free(l);
                                rpt_mutex_lock(&myrpt->lock);
				break;
			}
            if ((!l->chan) && (!l->disctime) && (!l->outbound))
            {
		if(debug) ast_log(LOG_NOTICE, "LINKDISC AA\n");
                /* remove from queue */
                remque((struct qelem *) l);
		if (myrpt->links.next==&myrpt->links) channel_revert(myrpt);
                if (!strcmp(myrpt->cmdnode,l->name))myrpt->cmdnode[0] = 0;
                rpt_mutex_unlock(&myrpt->lock);
		if (l->name[0] != '0') 
		{
	            	rpt_telemetry(myrpt,REMDISC,l);
		}
		rpt_update_links(myrpt);
		if (myrpt->p.archivedir)
		{
			char str[512];
			sprintf(str,"LINKDISC,%s",l->name);
			donodelog(myrpt,str);
		}
		dodispgm(myrpt,l->name);
                /* hang-up on call to device */
                ast_hangup(l->pchan);
                ast_free(l);
                rpt_mutex_lock(&myrpt->lock);
                break;
            }
			l = l->next;
		}
		if (myrpt->linkposttimer)
		{
			myrpt->linkposttimer -= elap;
			if (myrpt->linkposttimer < 0) myrpt->linkposttimer = 0;
		}
		if (myrpt->linkposttimer <= 0)
		{
			int nstr;
			char lst,*str;
			time_t now;

			myrpt->linkposttimer = LINKPOSTTIME;
			nstr = 0;
			for(l = myrpt->links.next; l != &myrpt->links; l = l->next)
			{
				/* if is not a real link, ignore it */
				if (l->name[0] == '0') continue;
				nstr += strlen(l->name) + 1;
			}
			str = ast_malloc(nstr + 256);
			if (!str)
			{
				ast_log(LOG_NOTICE,"Cannot ast_malloc()\n");
				ast_mutex_unlock(&myrpt->lock);
				break;
			}
			nstr = 0;
			strcpy(str,"nodes=");
			for(l = myrpt->links.next; l != &myrpt->links; l = l->next)
			{
				/* if is not a real link, ignore it */
				if (l->name[0] == '0') continue;
				lst = 'T';
				if (!l->mode) lst = 'R';
				if (l->mode > 1) lst = 'L';
				if (!l->thisconnected) lst = 'C';
				if (nstr) strcat(str,",");
				sprintf(str + strlen(str),"%c%s",lst,l->name);
				nstr = 1;
			}
                	p = strstr(tdesc, "version");
                	if(p){
				int vmajor,vminor,vpatch;
				if(sscanf(p, "version %d.%d.%d", &vmajor, &vminor, &vpatch) == 3)
					sprintf(str + strlen(str),"&apprptvers=%d.%d.%d",vmajor,vminor,vpatch);
			}
			time(&now);
			sprintf(str + strlen(str),"&apprptuptime=%d",(int)(now-starttime));
			sprintf(str + strlen(str),
			"&totalkerchunks=%d&totalkeyups=%d&totaltxtime=%d&timeouts=%d&totalexecdcommands=%d",
			myrpt->totalkerchunks,myrpt->totalkeyups,(int) myrpt->totaltxtime/1000,
			myrpt->timeouts,myrpt->totalexecdcommands);
			rpt_mutex_unlock(&myrpt->lock);
			statpost(myrpt,str);
			rpt_mutex_lock(&myrpt->lock);
			ast_free(str);
		}
		if (myrpt->deferid && (!is_paging(myrpt)))
		{
			myrpt->deferid = 0;
			queue_id(myrpt);
		}
		if (myrpt->keyposttimer)
		{
			myrpt->keyposttimer -= elap;
			if (myrpt->keyposttimer < 0) myrpt->keyposttimer = 0;
		}
		if (myrpt->keyposttimer <= 0)
		{
			char str[100];
			int n = 0;
			time_t now;

			myrpt->keyposttimer = KEYPOSTTIME;
			time(&now);
			if (myrpt->lastkeyedtime)
			{
				n = (int)(now - myrpt->lastkeyedtime);
			}
			sprintf(str,"keyed=%d&keytime=%d",myrpt->keyed,n);
			rpt_mutex_unlock(&myrpt->lock);
			statpost(myrpt,str);
			rpt_mutex_lock(&myrpt->lock);
		}
		if(totx){
			myrpt->dailytxtime += elap;
			myrpt->totaltxtime += elap;
		}
		i = myrpt->tailtimer;
		if (myrpt->tailtimer) myrpt->tailtimer -= elap;
		if (myrpt->tailtimer < 0) myrpt->tailtimer = 0;
		if((i) && (myrpt->tailtimer == 0))
			myrpt->tailevent = 1;
		if ((!myrpt->p.s[myrpt->p.sysstate_cur].totdisable) && myrpt->totimer) myrpt->totimer -= elap;
		if (myrpt->totimer < 0) myrpt->totimer = 0;
		if (myrpt->idtimer) myrpt->idtimer -= elap;
		if (myrpt->idtimer < 0) myrpt->idtimer = 0;
		if (myrpt->tmsgtimer) myrpt->tmsgtimer -= elap;
		if (myrpt->tmsgtimer < 0) myrpt->tmsgtimer = 0;
		if (myrpt->voxtotimer) myrpt->voxtotimer -= elap;
		if (myrpt->voxtotimer < 0) myrpt->voxtotimer = 0;
		if (myrpt->keyed) myrpt->lastkeytimer = KEYTIMERTIME;
		else
		{
			if (myrpt->lastkeytimer) myrpt->lastkeytimer -= elap;
			if (myrpt->lastkeytimer < 0) myrpt->lastkeytimer = 0;
		}
		myrpt->elketimer += elap;
		if ((myrpt->telemmode != 0x7fffffff) && (myrpt->telemmode > 1))
		{
			myrpt->telemmode -= elap;
			if (myrpt->telemmode < 1) myrpt->telemmode = 1;
		}
		if (myrpt->exttx)
		{
			myrpt->parrottimer = myrpt->p.parrottime;
		}
		else
		{
			if (myrpt->parrottimer) myrpt->parrottimer -= elap;
			if (myrpt->parrottimer < 0) myrpt->parrottimer = 0;
		}
		/* do macro timers */
		if (myrpt->macrotimer) myrpt->macrotimer -= elap;
		if (myrpt->macrotimer < 0) myrpt->macrotimer = 0;
		/* do local dtmf timer */
		if (myrpt->dtmf_local_timer)
		{
			if (myrpt->dtmf_local_timer > 1) myrpt->dtmf_local_timer -= elap;
			if (myrpt->dtmf_local_timer < 1) myrpt->dtmf_local_timer = 1;
		}
		do_dtmf_local(myrpt,0);
		/* Execute scheduler appx. every 2 tenths of a second */
		if (myrpt->skedtimer <= 0){
			myrpt->skedtimer = 200;
			do_scheduler(myrpt);
		}
		else
			myrpt->skedtimer -=elap;
		if (!ms) 
		{
			rpt_mutex_unlock(&myrpt->lock);
			continue;
		}
		if ((myrpt->p.parrotmode || myrpt->parrotonce) && (myrpt->parrotstate == 1) &&
			(myrpt->parrottimer <= 0))
		{

			union {
				int i;
				void *p;
				char _filler[8];
			} pu;

			ci.confno = 0;
			ci.confmode = 0;
			ci.chan = 0;

			/* first put the channel on the conference in announce mode */
			if (ioctl(myrpt->parrotchannel->fds[0],DAHDI_SETCONF,&ci) == -1)
			{
				ast_log(LOG_WARNING, "Unable to set conference mode for parrot\n");
				break;
			}
			if (myrpt->parrotstream) 
				ast_closestream(myrpt->parrotstream);
			myrpt->parrotstream = NULL;
			myrpt->parrotstate = 2;
			pu.i = myrpt->parrotcnt++;
			rpt_telemetry(myrpt,PARROT,pu.p);
		}			
		if (myrpt->cmdAction.state == CMD_STATE_READY)
		{ /* there is a command waiting to be processed */
			myrpt->cmdAction.state = CMD_STATE_EXECUTING;
			// lose the lock
			rpt_mutex_unlock(&myrpt->lock);
			// do the function
			(*function_table[myrpt->cmdAction.functionNumber].function)(myrpt,myrpt->cmdAction.param, myrpt->cmdAction.digits, myrpt->cmdAction.command_source, NULL);
			// get the lock again
			rpt_mutex_lock(&myrpt->lock);
			myrpt->cmdAction.state = CMD_STATE_IDLE;
		} /* if myrpt->cmdAction.state == CMD_STATE_READY */
		
		c = myrpt->macrobuf[0];
		time(&t);
		if (c && (!myrpt->macrotimer) && 
			starttime && (t > (starttime + START_DELAY)))
		{
			char cin = c & 0x7f;
			myrpt->macrotimer = MACROTIME;
			memmove(myrpt->macrobuf,myrpt->macrobuf + 1,MAXMACRO - 1);
			if ((cin == 'p') || (cin == 'P'))
				myrpt->macrotimer = MACROPTIME;
			rpt_mutex_unlock(&myrpt->lock);
			if (myrpt->p.archivedir)
			{
				char str[100];

				sprintf(str,"DTMF(M),MAIN,%c",cin);
				donodelog(myrpt,str);
			}
			local_dtmf_helper(myrpt,c);
		} else rpt_mutex_unlock(&myrpt->lock);
		/* @@@@@@ UNLOCK @@@@@ */
		if (who == myrpt->rxchannel) /* if it was a read from rx */
		{
			int ismuted;

			f = ast_read(myrpt->rxchannel);
			if (!f)
			{
				if (debug) printf("@@@@ rpt:Hung Up\n");
				break;
			}

			if(f->frametype == AST_FRAME_TEXT && myrpt->rxchankeyed)
			{
				char myrxrssi[32];

				if(sscanf((char *)f->data, "R %s", myrxrssi) == 1)
				{
					myrpt->rxrssi=atoi(myrxrssi);
					if(debug>7)ast_log(LOG_NOTICE,"[%s] rxchannel rssi=%i\n",
						myrpt->name,myrpt->rxrssi);
					if(myrpt->p.votertype==2)
						rssi_send(myrpt);
				}
			}

			/* if out voted drop DTMF frames */
			if(  myrpt->p.votermode &&
			    !myrpt->votewinner &&

			    ( f->frametype == AST_FRAME_DTMF_BEGIN ||
                  f->frametype == AST_FRAME_DTMF_END
                )
			  )
			{
				rpt_mutex_unlock(&myrpt->lock);
				ast_frfree(f);
				continue;
			}

			if (f->frametype == AST_FRAME_VOICE)
			{
#ifdef	_MDC_DECODE_H_
				unsigned char ubuf[2560];
				short *sp;
				int n;
#endif
				if (myrpt->p.rxburstfreq)
				{
					if ((!myrpt->reallykeyed) || myrpt->keyed)
					{
						myrpt->lastrxburst = 0;
						goertzel_reset(&myrpt->burst_tone_state.tone);
						myrpt->burst_tone_state.last_hit = 0;
						myrpt->burst_tone_state.hit_count = 0;
						myrpt->burst_tone_state.energy = 0.0;
						
					}
					else
					{					
						i = tone_detect(&myrpt->burst_tone_state,AST_FRAME_DATAP(f),f->samples);
						ast_log(LOG_DEBUG,"Node %s got %d Hz Rx Burst\n",
							myrpt->name,myrpt->p.rxburstfreq);
						if ((!i) && myrpt->lastrxburst)
						{
							ast_log(LOG_DEBUG,"Node %s now keyed after Rx Burst\n",myrpt->name);
							myrpt->linkactivitytimer = 0;
							myrpt->keyed = 1;
							time(&myrpt->lastkeyedtime);
							myrpt->keyposttimer = KEYPOSTSHORTTIME;
						}
						myrpt->lastrxburst = i;
					}
				}
				if (myrpt->p.dtmfkey)
				{
					if ((!myrpt->reallykeyed) || myrpt->keyed)
					{
						myrpt->dtmfkeyed = 0;
						myrpt->dtmfkeybuf[0] = 0;
					}
					if (myrpt->reallykeyed && myrpt->dtmfkeyed && (!myrpt->keyed))
					{
						myrpt->dtmfkeyed = 0;
						myrpt->dtmfkeybuf[0] = 0;
						myrpt->linkactivitytimer = 0;
						myrpt->keyed = 1;
						time(&myrpt->lastkeyedtime);
						myrpt->keyposttimer = KEYPOSTSHORTTIME;
					}
				}
#ifdef	_MDC_DECODE_H_
				if (!myrpt->reallykeyed)
				{
					memset(AST_FRAME_DATAP(f),0,f->datalen);
				}
				sp = (short *) AST_FRAME_DATAP(f);
				/* convert block to unsigned char */
				for(n = 0; n < f->datalen / 2; n++)
				{
					ubuf[n] = (*sp++ >> 8) + 128;
				}
				n = mdc_decoder_process_samples(myrpt->mdc,ubuf,f->datalen / 2);
				if (n == 1)
				{

					unsigned char op,arg;
					unsigned short unitID;
					char ustr[16];

					mdc_decoder_get_packet(myrpt->mdc,&op,&arg,&unitID);
					if (option_verbose)
					{
						ast_verbose("Got MDC-1200 (single-length) packet on node %s:\n",myrpt->name);
						ast_verbose("op: %02x, arg: %02x, UnitID: %04x\n",
							op & 255,arg & 255,unitID);
					}
					/* if for PTT ID */
					if ((op == 1) && ((arg == 0) || (arg == 0x80)))
					{
						myrpt->lastunit = unitID;
						sprintf(ustr,"I%04X",unitID);
						mdc1200_notify(myrpt,NULL,ustr);
						mdc1200_send(myrpt,ustr);
						mdc1200_cmd(myrpt,ustr);
					}
					/* if for EMERGENCY */
					if ((op == 0) && ((arg == 0x81) || (arg == 0x80)))
					{
						myrpt->lastunit = unitID;
						sprintf(ustr,"E%04X",unitID);
						mdc1200_notify(myrpt,NULL,ustr);
						mdc1200_send(myrpt,ustr);
						mdc1200_cmd(myrpt,ustr);
					}
                                        /* if for Stun ACK W9CR */
                                        if ((op == 0x0b) && (arg == 0x00))
                                        {
                                                myrpt->lastunit = unitID;
                                                sprintf(ustr,"STUN ACK %04X",unitID);
					}
					/* if for STS (status)  */
					if (op == 0x46)
					{
						myrpt->lastunit = unitID;
						sprintf(ustr,"S%04X-%X",unitID,arg & 0xf);


#ifdef	_MDC_ENCODE_H_
						mdc1200_ack_status(myrpt,unitID);
#endif
						mdc1200_notify(myrpt,NULL,ustr);
						mdc1200_send(myrpt,ustr);
						mdc1200_cmd(myrpt,ustr);
					}
				}
				if (n == 2)
				{
					unsigned char op,arg,ex1,ex2,ex3,ex4;
					unsigned short unitID;
					char ustr[20];

					mdc_decoder_get_double_packet(myrpt->mdc,&op,&arg,&unitID,
						&ex1,&ex2,&ex3,&ex4);
					if (option_verbose)
					{
						ast_verbose("Got MDC-1200 (double-length) packet on node %s:\n",myrpt->name);
						ast_verbose("op: %02x, arg: %02x, UnitID: %04x\n",
							op & 255,arg & 255,unitID);
						ast_verbose("ex1: %02x, ex2: %02x, ex3: %02x, ex4: %02x\n",
							ex1 & 255, ex2 & 255, ex3 & 255, ex4 & 255);
					}
					/* if for SelCall or Alert */
					if ((op == 0x35) && (arg = 0x89))
					{
						/* if is Alert */
						if (ex1 & 1) sprintf(ustr,"A%02X%02X-%04X",ex3 & 255,ex4 & 255,unitID);
						/* otherwise is selcall */
						else  sprintf(ustr,"S%02X%02X-%04X",ex3 & 255,ex4 & 255,unitID);
						mdc1200_notify(myrpt,NULL,ustr);
						mdc1200_send(myrpt,ustr);
						mdc1200_cmd(myrpt,ustr);
					}
				}
#endif
#ifdef	__RPT_NOTCH
				/* apply inbound filters, if any */
				rpt_filter(myrpt,AST_FRAME_DATAP(f),f->datalen / 2);
#endif
				if ((!myrpt->localtx) && /* (!myrpt->p.linktolink) && */
				    (!myrpt->localoverride))
				{
					memset(AST_FRAME_DATAP(f),0,f->datalen);
				}

				if (ioctl(myrpt->zaprxchannel->fds[0], DAHDI_GETCONFMUTE, &ismuted) == -1)
				{
					ismuted = 0;
				}
				if (dtmfed) ismuted = 1;
				dtmfed = 0;

				if(myrpt->p.votertype==1)
				{
                                        if(!myrpt->rxchankeyed)
                                                myrpt->votewinner=0;

                    if(!myrpt->voteremrx)
                        myrpt->voted_link=NULL;

                    if(!myrpt->rxchankeyed&&!myrpt->voteremrx)
                    {
					    myrpt->voter_oneshot=0;
					    myrpt->voted_rssi=0;
					}
				}

				if( myrpt->p.votertype==1 && myrpt->vote_counter && (myrpt->rxchankeyed||myrpt->voteremrx)
				    && (myrpt->p.votermode==2||(myrpt->p.votermode==1&&!myrpt->voter_oneshot))
				  )
				{
					if(--myrpt->vote_counter<=0)
					{
						myrpt->vote_counter=10;
						if(debug>6)ast_log(LOG_NOTICE,"[%s] vote rxrssi=%i\n",
							myrpt->name,myrpt->rxrssi);
						FindBestRssi(myrpt);
						myrpt->voter_oneshot=1;
					}
				}
				/* if a voting rx and not the winner, mute audio */
				if(myrpt->p.votertype==1 && myrpt->voted_link!=NULL)
				{
					ismuted=1;
				}
				if (ismuted)
				{
					memset(AST_FRAME_DATAP(f),0,f->datalen);
					if (myrpt->lastf1)
						memset(AST_FRAME_DATAP(myrpt->lastf1),0,myrpt->lastf1->datalen);
					if (myrpt->lastf2)
						memset(AST_FRAME_DATAP(myrpt->lastf2),0,myrpt->lastf2->datalen);
				} 
				if (f) f2 = ast_frdup(f);
				else f2 = NULL;
				f1 = myrpt->lastf2;
				myrpt->lastf2 = myrpt->lastf1;
				myrpt->lastf1 = f2;
				if (ismuted)
				{
					if (myrpt->lastf1)
						memset(AST_FRAME_DATAP(myrpt->lastf1),0,myrpt->lastf1->datalen);
					if (myrpt->lastf2)
						memset(AST_FRAME_DATAP(myrpt->lastf2),0,myrpt->lastf2->datalen);
				}
				if (f1)
				{
					if (myrpt->localoverride)
						ast_write(myrpt->txpchannel,f1);
					else
						ast_write(myrpt->pchannel,f1);
					ast_frfree(f1);
					if ((myrpt->p.duplex < 2) && myrpt->monstream &&
					    (!myrpt->txkeyed) && myrpt->keyed)
					{
						ast_writestream(myrpt->monstream,f1);
					}
					if ((myrpt->p.duplex < 2) && myrpt->keyed &&
					    myrpt->p.outstreamcmd && (myrpt->outstreampipe[1] > 0))
					{
						write(myrpt->outstreampipe[1],AST_FRAME_DATAP(f1),f1->datalen);
					}
				}
			}
#ifndef	OLD_ASTERISK
			else if (f->frametype == AST_FRAME_DTMF_BEGIN)
			{
				if (myrpt->lastf1)
					memset(AST_FRAME_DATAP(myrpt->lastf1),0,myrpt->lastf1->datalen);
				if (myrpt->lastf2)
					memset(AST_FRAME_DATAP(myrpt->lastf2),0,myrpt->lastf2->datalen);
				dtmfed = 1;
				myrpt->lastdtmftime = ast_tvnow();
			}
#endif
			else if (f->frametype == AST_FRAME_DTMF)
			{
				c = (char) f->subclass; /* get DTMF char */
				ast_frfree(f);
#ifndef	OLD_ASTERISK
				x = ast_tvdiff_ms(ast_tvnow(),myrpt->lastdtmftime);
				if ((myrpt->p.litzcmd) && (x >= myrpt->p.litztime) &&
					strchr(myrpt->p.litzchar,c))
				{
					ast_log(LOG_NOTICE,"Doing litz command %s on node %s\n",myrpt->p.litzcmd,myrpt->name);
					rpt_mutex_lock(&myrpt->lock);
					if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(myrpt->p.litzcmd))
					{
						rpt_mutex_unlock(&myrpt->lock);
						continue;
					}
					myrpt->macrotimer = MACROTIME;
					strncat(myrpt->macrobuf,myrpt->p.litzcmd,MAXMACRO - 1);
					rpt_mutex_unlock(&myrpt->lock);

					continue;
				}			
#endif
				if (myrpt->lastf1)
					memset(AST_FRAME_DATAP(myrpt->lastf1),0,myrpt->lastf1->datalen);
				if (myrpt->lastf2)
					memset(AST_FRAME_DATAP(myrpt->lastf2),0,myrpt->lastf2->datalen);
				dtmfed = 1;
				if ((!myrpt->lastkeytimer) && (!myrpt->localoverride)) 
				{
					if (myrpt->p.dtmfkey) local_dtmfkey_helper(myrpt,c);
					continue;
				}
				c = func_xlat(myrpt,c,&myrpt->p.inxlat);
				if (c) local_dtmf_helper(myrpt,c);
				continue;
			}						
			else if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
				/* if RX key */
				if (f->subclass == AST_CONTROL_RADIO_KEY)
				{
					if ((!lasttx) || (myrpt->p.duplex > 1) || (myrpt->p.linktolink)) 
					{
						if (debug >= 6)
							ast_log(LOG_NOTICE,"**** rx key\n"); 
						myrpt->reallykeyed = 1;
						myrpt->dtmfkeybuf[0] = 0;
						myrpt->curdtmfuser[0] = 0;
						if ((!myrpt->p.rxburstfreq) && (!myrpt->p.dtmfkey))
						{
							myrpt->linkactivitytimer = 0;
							myrpt->keyed = 1;
							time(&myrpt->lastkeyedtime);
							myrpt->keyposttimer = KEYPOSTSHORTTIME;
						}
					}
					if (myrpt->p.archivedir)
					{
						if (myrpt->p.duplex < 2)
						{
							char myfname[512],mydate[100];
							long blocksleft;
							time_t myt;

							time(&myt);
							strftime(mydate,sizeof(mydate) - 1,"%Y%m%d%H%M%S",
							localtime(&myt));
							sprintf(myfname,"%s/%s/%s",myrpt->p.archivedir,
								myrpt->name,mydate);
							if (myrpt->p.monminblocks)
							{
								blocksleft = diskavail(myrpt);
								if (blocksleft >= myrpt->p.monminblocks)
								{
									myrpt->monstream = ast_writefile(myfname,"wav49",
										"app_rpt Air Archive",O_CREAT | O_APPEND,0,0600);
								}
							}
						}
						donodelog(myrpt,"RXKEY,MAIN");
					}
					rpt_update_boolean(myrpt,"RPT_RXKEYED",1);
					myrpt->elketimer = 0;
					myrpt->localoverride = 0;
					if (f->datalen && AST_FRAME_DATAP(f))
					{
						char *val, busy = 0;

						send_link_pl(myrpt,AST_FRAME_DATAP(f));

						if (myrpt->p.nlocallist)
						{
							for(x = 0; x < myrpt->p.nlocallist; x++)
							{
								if (!strcasecmp(AST_FRAME_DATAP(f),myrpt->p.locallist[x]))
								{
									myrpt->localoverride = 1;
									myrpt->keyed = 0;
									break;
								}
							}
						} 
						if (debug) ast_log(LOG_NOTICE,"Got PL %s on node %s\n",(char *)AST_FRAME_DATAP(f),myrpt->name);
						// ctcss code autopatch initiate
						if (strstr((char *)AST_FRAME_DATAP(f),"/M/")&& !myrpt->macropatch)
						{
						    char val[16];
							strcat(val,"*6");
							myrpt->macropatch=1;
							rpt_mutex_lock(&myrpt->lock);
							if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(val)){
								rpt_mutex_unlock(&myrpt->lock);
								busy=1;
							}
							if(!busy){
								myrpt->macrotimer = MACROTIME;
								strncat(myrpt->macrobuf,val,MAXMACRO - 1);
								if (!busy) strcpy(myrpt->lasttone,(char*)AST_FRAME_DATAP(f));
							}
							rpt_mutex_unlock(&myrpt->lock);
						}
						else if (strcmp((char *)AST_FRAME_DATAP(f),myrpt->lasttone))
						{
							val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.tonemacro, (char *)AST_FRAME_DATAP(f));
							if (val) 
							{
								if (debug) ast_log(LOG_NOTICE,"Tone %s doing %s on node %s\n",(char *) AST_FRAME_DATAP(f),val,myrpt->name);
								rpt_mutex_lock(&myrpt->lock);
								if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(val)){
									rpt_mutex_unlock(&myrpt->lock);
									busy=1;
								}
								if(!busy){
									myrpt->macrotimer = MACROTIME;
									strncat(myrpt->macrobuf,val,MAXMACRO - 1);
								}
								rpt_mutex_unlock(&myrpt->lock);
							}
						 	if (!busy) strcpy(myrpt->lasttone,(char*)AST_FRAME_DATAP(f));
						}
					} 
					else
					{
						myrpt->lasttone[0] = 0;
						send_link_pl(myrpt,"0");
					}
				}
				/* if RX un-key */
				if (f->subclass == AST_CONTROL_RADIO_UNKEY)
				{
					/* clear rx channel rssi	 */
					myrpt->rxrssi=0;
					char asleep = myrpt->p.s[myrpt->p.sysstate_cur].sleepena & myrpt->sleep;

					if ((!lasttx) || (myrpt->p.duplex > 1) || (myrpt->p.linktolink))
					{
						if (debug >= 6)
							ast_log(LOG_NOTICE,"**** rx un-key\n");
		
						if((!asleep) && myrpt->p.duplex && myrpt->keyed) {
							rpt_telemetry(myrpt,UNKEY,NULL);
						}
					}
					send_link_pl(myrpt,"0");
					myrpt->reallykeyed = 0;
					myrpt->keyed = 0;
					if ((myrpt->p.duplex > 1) && (!asleep) && myrpt->localoverride)
					{
						rpt_telemetry(myrpt,LOCUNKEY,NULL);
					}
					myrpt->localoverride = 0;
					time(&myrpt->lastkeyedtime);
					myrpt->keyposttimer = KEYPOSTSHORTTIME;
					myrpt->lastdtmfuser[0] = 0;
					strcpy(myrpt->lastdtmfuser,myrpt->curdtmfuser);
					myrpt->curdtmfuser[0] = 0;
					if (myrpt->monstream && (myrpt->p.duplex < 2))
					{
						ast_closestream(myrpt->monstream);
						myrpt->monstream = NULL;
					}
					if (myrpt->p.archivedir)
					{
						donodelog(myrpt,"RXUNKEY,MAIN");
					}
					rpt_update_boolean(myrpt,"RPT_RXKEYED",0);
				}
			}
			else if (f->frametype == AST_FRAME_TEXT) /* if a message from a USB device */
			{
				char buf[100];
				int j;
				/* if is a USRP device */
				if (strncasecmp(myrpt->rxchannel->name,"usrp/", 5) == 0)
				{
					char *argv[4];
					int argc = 4;
					argv[2] = myrpt->name;
					argv[3] = AST_FRAME_DATAP(f);	
					rpt_do_sendall(0,argc,argv);
				}
				/* if is a USB device */
				if ((strncasecmp(myrpt->rxchannel->name,"radio/", 6) == 0) || 
				    (strncasecmp(myrpt->rxchannel->name,"simpleusb/", 10) == 0))
				{
					/* if message parsable */
					if (sscanf(AST_FRAME_DATAP(f),"GPIO%d %d",&i,&j) >= 2)
					{
						sprintf(buf,"RPT_URI_GPIO%d",i);
						rpt_update_boolean(myrpt,buf,j);
					}
					/* if message parsable */
					else if (sscanf(AST_FRAME_DATAP(f),"PP%d %d",&i,&j) >= 2)
					{
						sprintf(buf,"RPT_PP%d",i);
						rpt_update_boolean(myrpt,buf,j);
					}
					else if (!strcmp(AST_FRAME_DATAP(f),"ENDPAGE"))
					{
						myrpt->paging.tv_sec = 0;
						myrpt->paging.tv_usec = 0;
					}
				}
				/* if is a BeagleBoard device */
				if (strncasecmp(myrpt->rxchannel->name,"beagle/", 7) == 0)
				{
					/* if message parsable */
					if (sscanf(AST_FRAME_DATAP(f),"GPIO%d %d",&i,&j) >= 2)
					{
						sprintf(buf,"RPT_BEAGLE_GPIO%d",i);
						rpt_update_boolean(myrpt,buf,j);
					}
				}
				/* if is a Voter device */
				if (strncasecmp(myrpt->rxchannel->name,"voter/", 6) == 0)
				{
					struct rpt_link *l;
					struct	ast_frame wf;
					char	str[200];


					if (!strcmp(AST_FRAME_DATAP(f),"ENDPAGE"))
					{
						myrpt->paging.tv_sec = 0;
						myrpt->paging.tv_usec = 0;
					}
					else
					{
						sprintf(str,"V %s %s",myrpt->name,(char *)AST_FRAME_DATAP(f));
						wf.frametype = AST_FRAME_TEXT;
						wf.subclass = 0;
						wf.offset = 0;
						wf.mallocd = 0;
						wf.datalen = strlen(str) + 1;
						wf.samples = 0;
						wf.src = "voter_text_send";


						l = myrpt->links.next;
						/* otherwise, send it to all of em */
						while(l != &myrpt->links)
						{
							/* Dont send to other then IAXRPT client */
							if ((l->name[0] != '0') || (l->phonemode))
							{
								l = l->next;
								continue;
							}
							AST_FRAME_DATA(wf) = str;
							if (l->chan) rpt_qwrite(l,&wf); 
							l = l->next;
						}
					}
				}
			}
			ast_frfree(f);
			continue;
		}
		if (who == myrpt->pchannel) /* if it was a read from pseudo */
		{
			f = ast_read(myrpt->pchannel);
			if (!f)
			{
				if (debug) printf("@@@@ rpt:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE)
			{
				if (!myrpt->localoverride)
				{
					ast_write(myrpt->txpchannel,f);
				}
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
		if (who == myrpt->txchannel) /* if it was a read from tx */
		{
			f = ast_read(myrpt->txchannel);
			if (!f)
			{
				if (debug) printf("@@@@ rpt:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
		if (who == myrpt->zaptxchannel) /* if it was a read from pseudo-tx */
		{
			f = ast_read(myrpt->zaptxchannel);
			if (!f)
			{
				if (debug) printf("@@@@ rpt:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE)
			{
				struct ast_frame *f1;

				if (myrpt->p.duplex < 2)
				{
					if (myrpt->txrealkeyed) 
					{
						if ((!myfirst) && myrpt->callmode)
						{
						    x = 0;
						    AST_LIST_TRAVERSE(&myrpt->txq, f1,
							frame_list) x++;
						    for(;x < myrpt->p.simplexpatchdelay; x++)
						    {
								f1 = ast_frdup(f);
								memset(AST_FRAME_DATAP(f1),0,f1->datalen);
								memset(&f1->frame_list,0,sizeof(f1->frame_list));
								AST_LIST_INSERT_TAIL(&myrpt->txq,f1,frame_list);
						    }
						    myfirst = 1;
						}
						f1 = ast_frdup(f);
						memset(&f1->frame_list,0,sizeof(f1->frame_list));
						AST_LIST_INSERT_TAIL(&myrpt->txq,
							f1,frame_list);
					} else myfirst = 0;
					x = 0;
					AST_LIST_TRAVERSE(&myrpt->txq, f1,
						frame_list) x++;
					if (!x)
					{
						memset(AST_FRAME_DATAP(f),0,f->datalen);
					}
					else
					{
						ast_frfree(f);
						f = AST_LIST_REMOVE_HEAD(&myrpt->txq,
							frame_list);
					}
				}
				else
				{	
					while((f1 = AST_LIST_REMOVE_HEAD(&myrpt->txq,
						frame_list))) ast_frfree(f1);
				}
				ast_write(myrpt->txchannel,f);
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
		toexit = 0;
		/* @@@@@ LOCK @@@@@ */
		rpt_mutex_lock(&myrpt->lock);
		l = myrpt->links.next;
		while(l != &myrpt->links)
		{
			int remnomute,remrx;
			struct timeval now;

			if (l->disctime)
			{
				l = l->next;
				continue;
			}

			remrx = 0;
			/* see if any other links are receiving */
			m = myrpt->links.next;
			while(m != &myrpt->links)
			{
				/* if not us, and not localonly count it */
				if ((m != l) && (m->lastrx) && (m->mode < 2)) remrx = 1;
				m = m->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
			now = ast_tvnow();
			if ((who == l->chan) || (!l->lastlinktv.tv_sec) ||
				(ast_tvdiff_ms(now,l->lastlinktv) >= 19))
			{

				char mycalltx;

				l->lastlinktv = now;
				remnomute = myrpt->localtx && 
				    (!(myrpt->cmdnode[0] || 
					(myrpt->dtmfidx > -1)));
				mycalltx = myrpt->callmode;
#ifdef	DONT_USE__CAUSES_CLIPPING_OF_FIRST_SYLLABLE_ON_LINK
				if (myrpt->patchvoxalways) 
					mycalltx = mycalltx && ((!myrpt->voxtostate) && myrpt->wasvox);
#endif
				totx = ((l->isremote) ? (remnomute) : 
					myrpt->localtx || mycalltx) || remrx;

				/* foop */
				if ((!l->lastrx) && altlink(myrpt,l)) totx = myrpt->txkeyed;
				if (altlink1(myrpt,l)) totx = 1;
				l->wouldtx = totx;
				if (l->mode != 1) totx = 0;
				if (l->phonemode == 0 && l->chan && (l->lasttx != totx))
				{
					if ( totx && !l->voterlink)
					{
						if (l->newkey < 2) ast_indicate(l->chan,AST_CONTROL_RADIO_KEY);
					}
					else
					{
						ast_indicate(l->chan,AST_CONTROL_RADIO_UNKEY);
					}
					if (myrpt->p.archivedir)
					{
						char str[512];

						if (totx)
							sprintf(str,"TXKEY,%s",l->name);
						else
							sprintf(str,"TXUNKEY,%s",l->name);
						donodelog(myrpt,str);
					}
				}
				l->lasttx = totx;
			}
			rpt_mutex_lock(&myrpt->lock);
			if (who == l->chan) /* if it was a read from rx */
			{
				rpt_mutex_unlock(&myrpt->lock);
				f = ast_read(l->chan);
				if (!f)
				{
					rpt_mutex_lock(&myrpt->lock);
					__kickshort(myrpt);
					rpt_mutex_unlock(&myrpt->lock);
					if (strncasecmp(l->chan->name,"echolink",8) && 
					    strncasecmp(l->chan->name,"tlb",3))
					{
						if ((!l->disced) && (!l->outbound))
						{
							if ((l->name[0] <= '0') || (l->name[0] > '9') || l->isremote)
								l->disctime = 1;
							else
								l->disctime = DISC_TIME;
							rpt_mutex_lock(&myrpt->lock);
							ast_hangup(l->chan);
							l->chan = 0;
							break;
						}
	
						if (l->retrytimer) 
						{
							ast_hangup(l->chan);
							l->chan = 0;
							rpt_mutex_lock(&myrpt->lock);
							break; 
						}
						if (l->outbound && (l->retries++ < l->max_retries) && (l->hasconnected))
						{
							rpt_mutex_lock(&myrpt->lock);
							if (l->chan) ast_hangup(l->chan);
							l->chan = 0;
							l->hasconnected = 1;
							l->retrytimer = RETRY_TIMER_MS;
							l->elaptime = 0;
							l->connecttime = 0;
							l->thisconnected = 0;
							break;
						}
					}
					rpt_mutex_lock(&myrpt->lock);
					/* remove from queue */
					remque((struct qelem *) l);
					if (!strcmp(myrpt->cmdnode,l->name))
						myrpt->cmdnode[0] = 0;
					__kickshort(myrpt);
					rpt_mutex_unlock(&myrpt->lock);
					if (!l->hasconnected)
						rpt_telemetry(myrpt,CONNFAIL,l);
					else if (l->disced != 2) rpt_telemetry(myrpt,REMDISC,l);
					if (l->hasconnected) rpt_update_links(myrpt);
					if (myrpt->p.archivedir)
					{
						char str[512];

						if (!l->hasconnected)
							sprintf(str,"LINKFAIL,%s",l->name);
						else
							sprintf(str,"LINKDISC,%s",l->name);
						donodelog(myrpt,str);
					}
					dodispgm(myrpt,l->name);
					if (l->lastf1) ast_frfree(l->lastf1);
					l->lastf1 = NULL;
					if (l->lastf2) ast_frfree(l->lastf2);
					l->lastf2 = NULL;
					/* hang-up on call to device */
					ast_hangup(l->chan);
					ast_hangup(l->pchan);
					ast_free(l);
					rpt_mutex_lock(&myrpt->lock);
					break;
				}
				if (f->frametype == AST_FRAME_VOICE)
				{
					int ismuted,n1;
					float fac,fsamp;
					struct dahdi_bufferinfo bi;

					/* This is a miserable kludge. For some unknown reason, which I dont have
					   time to properly research, buffer settings do not get applied to dahdi
					   pseudo-channels. So, if we have a need to fit more then 1 160 sample
					   buffer into the psuedo-channel at a time, and there currently is not
					   room, it increases the number of buffers to accommodate the larger number
					   of samples (version 0.257 9/3/10) */
					memset(&bi,0,sizeof(bi));
					if (ioctl(l->pchan->fds[0],DAHDI_GET_BUFINFO,&bi) != -1)
					{
						if ((f->samples > bi.bufsize) &&
							(bi.numbufs < ((f->samples / bi.bufsize) + 1)))
						{
							bi.numbufs = (f->samples / bi.bufsize) + 1;
							ioctl(l->pchan->fds[0],DAHDI_SET_BUFINFO,&bi);
						}
					}
					fac = 1.0;
					if (l->chan && (!strncasecmp(l->chan->name,"echolink",8)))
						fac = myrpt->p.erxgain;
					if (l->chan && (!strncasecmp(l->chan->name,"tlb",3)))
						fac = myrpt->p.trxgain;
					if ((myrpt->p.linkmongain != 1.0) && (l->mode != 1) && (l->wouldtx))
						fac *= myrpt->p.linkmongain;
					if (fac != 1.0)
					{
						int x1;
						short *sp;

						sp = (short *)AST_FRAME_DATAP(f);
						for(x1 = 0; x1 < f->datalen / 2; x1++)
						{
							fsamp = (float) sp[x1] * fac;
							if (fsamp > 32765.0) fsamp = 32765.0;
							if (fsamp < -32765.0) fsamp = -32765.0;
							sp[x1] = (int) fsamp;
						}
					}

					l->rxlingertimer = ((l->iaxkey) ? RX_LINGER_TIME_IAXKEY : RX_LINGER_TIME);

					if ((l->newkey == 2) && (!l->lastrealrx))
					{
						l->lastrealrx = 1;
						l->rerxtimer = 0;
						if (!l->lastrx1)
						{
							if (myrpt->p.archivedir)
							{
								char str[512];

								sprintf(str,"RXKEY,%s",l->name);
								donodelog(myrpt,str);
							}
							l->lastrx1 = 1;
							rpt_update_links(myrpt);
							time(&l->lastkeytime);
						}
					}
					if (((l->phonemode) && (l->phonevox)) || 
					    (!strncasecmp(l->chan->name,"echolink",8)) ||
					       (!strncasecmp(l->chan->name,"tlb",3)))
					{
						if (l->phonevox)
						{
							n1 = dovox(&l->vox,
								AST_FRAME_DATAP(f),f->datalen / 2);
							if (n1 != l->wasvox)
							{
								if (debug)ast_log(LOG_DEBUG,"Link Node %s, vox %d\n",l->name,n1);
								l->wasvox = n1;
								l->voxtostate = 0;
								if (n1) l->voxtotimer = myrpt->p.voxtimeout_ms;
								else l->voxtotimer = 0;
							}
							if (l->lastrealrx || n1)
							{
								if (!myfirst)
								{
								    x = 0;
								    AST_LIST_TRAVERSE(&l->rxq, f1,
									frame_list) x++;
								    for(;x < myrpt->p.simplexphonedelay; x++)
									{
										f1 = ast_frdup(f);
										memset(AST_FRAME_DATAP(f1),0,f1->datalen);
										memset(&f1->frame_list,0,sizeof(f1->frame_list));
										AST_LIST_INSERT_TAIL(&l->rxq,
											f1,frame_list);
								    }
								    myfirst = 1;
								}
								f1 = ast_frdup(f);
								memset(&f1->frame_list,0,sizeof(f1->frame_list));
								AST_LIST_INSERT_TAIL(&l->rxq,f1,frame_list);
							} else myfirst = 0; 
							x = 0;
							AST_LIST_TRAVERSE(&l->rxq, f1,frame_list) x++;
							if (!x)
							{
								memset(AST_FRAME_DATAP(f),0,f->datalen);
							}
							else
							{
								ast_frfree(f);
								f = AST_LIST_REMOVE_HEAD(&l->rxq,frame_list);
							}
						}
						if (ioctl(l->chan->fds[0], DAHDI_GETCONFMUTE, &ismuted) == -1)
						{
							ismuted = 0;
						}
						/* if not receiving, zero-out audio */
						ismuted |= (!l->lastrx);
						if (l->dtmfed && 
							(l->phonemode || 
							    (!strncasecmp(l->chan->name,"echolink",8)) ||
								(!strncasecmp(l->chan->name,"tlb",3)))) ismuted = 1;
						l->dtmfed = 0;

						/* if a voting rx link and not the winner, mute audio */
						if(myrpt->p.votertype==1 && l->voterlink && myrpt->voted_link!=l)
						{
							ismuted=1;
						}

						if (ismuted)
						{
							memset(AST_FRAME_DATAP(f),0,f->datalen);
							if (l->lastf1)
								memset(AST_FRAME_DATAP(l->lastf1),0,l->lastf1->datalen);
							if (l->lastf2)
								memset(AST_FRAME_DATAP(l->lastf2),0,l->lastf2->datalen);
						} 
						if (f) f2 = ast_frdup(f);
						else f2 = NULL;
						f1 = l->lastf2;
						l->lastf2 = l->lastf1;
						l->lastf1 = f2;
						if (ismuted)
						{
							if (l->lastf1)
								memset(AST_FRAME_DATAP(l->lastf1),0,l->lastf1->datalen);
							if (l->lastf2)
								memset(AST_FRAME_DATAP(l->lastf2),0,l->lastf2->datalen);
						}
						if (f1)
						{
							ast_write(l->pchan,f1);
							ast_frfree(f1);
						}
					}
					else
					{
						/* if a voting rx link and not the winner, mute audio */
						if(myrpt->p.votertype==1 && l->voterlink && myrpt->voted_link!=l)
							ismuted=1;
						else
							ismuted=0;

						if ( !l->lastrx || ismuted )
							memset(AST_FRAME_DATAP(f),0,f->datalen);
						ast_write(l->pchan,f);
					}
				}
#ifndef	OLD_ASTERISK
				else if (f->frametype == AST_FRAME_DTMF_BEGIN)
				{
					if (l->lastf1)
						memset(AST_FRAME_DATAP(l->lastf1),0,l->lastf1->datalen);
					if (l->lastf2)
						memset(AST_FRAME_DATAP(l->lastf2),0,l->lastf2->datalen);
					l->dtmfed = 1;
				}
#endif
				if (f->frametype == AST_FRAME_TEXT)
				{
					char *tstr = ast_malloc(f->datalen + 1);
					if (tstr) 
					{
						memcpy(tstr,AST_FRAME_DATAP(f),f->datalen);
						tstr[f->datalen] = 0;
						handle_link_data(myrpt,l,tstr);
						ast_free(tstr);
					}
				}
				if (f->frametype == AST_FRAME_DTMF)
				{
					if (l->lastf1)
						memset(AST_FRAME_DATAP(l->lastf1),0,l->lastf1->datalen);
					if (l->lastf2)
						memset(AST_FRAME_DATAP(l->lastf2),0,l->lastf2->datalen);
					l->dtmfed = 1;
					handle_link_phone_dtmf(myrpt,l,f->subclass);
				}
				if (f->frametype == AST_FRAME_CONTROL)
				{
					if (f->subclass == AST_CONTROL_ANSWER)
					{
						char lconnected = l->connected;

						__kickshort(myrpt);
						myrpt->rxlingertimer = ((myrpt->iaxkey) ? RX_LINGER_TIME_IAXKEY : RX_LINGER_TIME);
						l->connected = 1;
						l->hasconnected = 1;
						l->thisconnected = 1;
						l->elaptime = -1;
						if (!l->phonemode) send_newkey(l->chan);
						if (!l->isremote) l->retries = 0;
						if (!lconnected) 
						{
							rpt_telemetry(myrpt,CONNECTED,l);
							if (myrpt->p.archivedir)
							{
								char str[512];

								if (l->mode == 1)
									sprintf(str,"LINKTRX,%s",l->name);
								else if (l->mode > 1)
									sprintf(str,"LINKLOCALMONITOR,%s",l->name);
								else
									sprintf(str,"LINKMONITOR,%s",l->name);
								donodelog(myrpt,str);
							}
							rpt_update_links(myrpt);
							doconpgm(myrpt,l->name);
						}		
						else
							l->reconnects++;
					}
					/* if RX key */
					if ((f->subclass == AST_CONTROL_RADIO_KEY) && (l->newkey < 2))
					{
						if (debug == 7 ) printf("@@@@ rx key\n");
						l->lastrealrx = 1;
						l->rerxtimer = 0;
						if (!l->lastrx1)
						{
							if (myrpt->p.archivedir)
							{
								char str[512];

								sprintf(str,"RXKEY,%s",l->name);
								donodelog(myrpt,str);
							}
							l->lastrx1 = 1;
							rpt_update_links(myrpt);
							time(&l->lastkeytime);
						}
					}
					/* if RX un-key */
					if (f->subclass == AST_CONTROL_RADIO_UNKEY)
					{

						if (debug == 7) printf("@@@@ rx un-key\n");
						l->lastrealrx = 0;
						l->rerxtimer = 0;
						if (l->lastrx1)
						{
							if (myrpt->p.archivedir)
							{
								char str[512];

								sprintf(str,"RXUNKEY,%s",l->name);
								donodelog(myrpt,str);
							}
							l->lastrx1 = 0;
							time(&l->lastunkeytime);
							rpt_update_links(myrpt);
							if(myrpt->p.duplex)
								rpt_telemetry(myrpt,LINKUNKEY,l);
						}
					}
					if (f->subclass == AST_CONTROL_HANGUP)
					{
						ast_frfree(f);
						rpt_mutex_lock(&myrpt->lock);
						__kickshort(myrpt);
						rpt_mutex_unlock(&myrpt->lock);
						if (strncasecmp(l->chan->name,"echolink",8) && 
							strncasecmp(l->chan->name,"tlb",3))
						{
							if ((!l->outbound) && (!l->disced))
							{
								if ((l->name[0] <= '0') || (l->name[0] > '9') || l->isremote)
									l->disctime = 1;
								else
									l->disctime = DISC_TIME;
								rpt_mutex_lock(&myrpt->lock);
								ast_hangup(l->chan);
								l->chan = 0;
								break;
							}
							if (l->retrytimer) 
							{
								if (l->chan) ast_hangup(l->chan);
								l->chan = 0;
								rpt_mutex_lock(&myrpt->lock);
								break;
							}
							if (l->outbound && (l->retries++ < l->max_retries) && (l->hasconnected))
							{
								rpt_mutex_lock(&myrpt->lock);
								if (l->chan) ast_hangup(l->chan);
								l->chan = 0;
								l->hasconnected = 1;
								l->elaptime = 0;
								l->retrytimer = RETRY_TIMER_MS;
								l->connecttime = 0;
								l->thisconnected = 0;
								break;
							}
						}
						rpt_mutex_lock(&myrpt->lock);
						/* remove from queue */
						remque((struct qelem *) l);
						if (!strcmp(myrpt->cmdnode,l->name))
							myrpt->cmdnode[0] = 0;
						__kickshort(myrpt);
						rpt_mutex_unlock(&myrpt->lock);
						if (!l->hasconnected)
							rpt_telemetry(myrpt,CONNFAIL,l);
						else if (l->disced != 2) rpt_telemetry(myrpt,REMDISC,l);
						if (l->hasconnected) rpt_update_links(myrpt);
						if (myrpt->p.archivedir)
						{
							char str[512];

							if (!l->hasconnected)
								sprintf(str,"LINKFAIL,%s",l->name);
							else
								sprintf(str,"LINKDISC,%s",l->name);
							donodelog(myrpt,str);
						}
						if (l->hasconnected) dodispgm(myrpt,l->name);
						if (l->lastf1) ast_frfree(l->lastf1);
						l->lastf1 = NULL;
						if (l->lastf2) ast_frfree(l->lastf2);
						l->lastf2 = NULL;
						/* hang-up on call to device */
						ast_hangup(l->chan);
						ast_hangup(l->pchan);
						ast_free(l);
						rpt_mutex_lock(&myrpt->lock);
						break;
					}
				}
				ast_frfree(f);
				rpt_mutex_lock(&myrpt->lock);
				break;
			}
			if (who == l->pchan) 
			{
				rpt_mutex_unlock(&myrpt->lock);
				f = ast_read(l->pchan);
				if (!f)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					toexit = 1;
					rpt_mutex_lock(&myrpt->lock);
					break;
				}
				if (f->frametype == AST_FRAME_VOICE)
				{
					float fac,fsamp;

					fac = 1.0;
					if (l->chan && (!strncasecmp(l->chan->name,"echolink",8)))
						fac = myrpt->p.etxgain;
					if (l->chan && (!strncasecmp(l->chan->name,"tlb",3)))
						fac = myrpt->p.ttxgain;

					if (fac != 1.0)
					{
						int x1;
						short *sp;
						
						sp = (short *) AST_FRAME_DATAP(f);
						for(x1 = 0; x1 < f->datalen / 2; x1++)
						{
							fsamp = (float) sp[x1] * fac;
							if (fsamp > 32765.0) fsamp = 32765.0;
							if (fsamp < -32765.0) fsamp = -32765.0;
							sp[x1] = (int) fsamp;
						}
					}
					/* foop */
					if (l->chan && (l->lastrx || (!altlink(myrpt,l))) && 
					    ((l->newkey < 2) || l->lasttx ||
						strncasecmp(l->chan->name,"IAX",3)))
							ast_write(l->chan,f);
				}
				if (f->frametype == AST_FRAME_CONTROL)
				{
					if (f->subclass == AST_CONTROL_HANGUP)
					{
						if (debug) printf("@@@@ rpt:Hung Up\n");
						ast_frfree(f);
						toexit = 1;
						rpt_mutex_lock(&myrpt->lock);
						break;
					}
				}
				ast_frfree(f);
				rpt_mutex_lock(&myrpt->lock);
				break;
			}
			l = l->next;
		}
		/* @@@@@ UNLOCK @@@@@ */
		rpt_mutex_unlock(&myrpt->lock);
		if (toexit) break;
		if (who == myrpt->monchannel) 
		{
			f = ast_read(myrpt->monchannel);
			if (!f)
			{
				if (debug) printf("@@@@ rpt:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE)
			{
				struct ast_frame *fs;
				short *sp;
				int x1;
				float fac,fsamp;

				if ((myrpt->p.duplex > 1) || (myrpt->txkeyed))
				{
					if (myrpt->monstream)
						ast_writestream(myrpt->monstream,f);
				}
				if (((myrpt->p.duplex >= 2) || (!myrpt->keyed)) &&
					myrpt->p.outstreamcmd && (myrpt->outstreampipe[1] > 0))
				{
					write(myrpt->outstreampipe[1],AST_FRAME_DATAP(f),f->datalen);
				}
				fs = ast_frdup(f);
				fac = 1.0;
				if (l->chan && (!strncasecmp(l->chan->name,"echolink",8)))
					fac = myrpt->p.etxgain;
				if (fac != 1.0)
				{
					sp = (short *)AST_FRAME_DATAP(fs);
					for(x1 = 0; x1 < fs->datalen / 2; x1++)
					{
						fsamp = (float) sp[x1] * fac;
						if (fsamp > 32765.0) fsamp = 32765.0;
						if (fsamp < -32765.0) fsamp = -32765.0;
						sp[x1] = (int) fsamp;
					}
				}
				l = myrpt->links.next;
				/* go thru all the links */
				while(l != &myrpt->links)
				{
					/* foop */
					if (l->chan && altlink(myrpt,l) && (!l->lastrx) && ((l->newkey < 2) || l->lasttx ||
					    strncasecmp(l->chan->name,"IAX",3)))
					    {
						if (l->chan && 
						    (!strncasecmp(l->chan->name,"irlp",4)))
						{
							ast_write(l->chan,fs);
						} 
						else
						{
							ast_write(l->chan,f);
						}
					    }
					l = l->next;
				}
				ast_frfree(fs);
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
		if (myrpt->parrotchannel && (who == myrpt->parrotchannel))
		{
			f = ast_read(myrpt->parrotchannel);
			if (!f)
			{
				if (debug) printf("@@@@ rpt:Hung Up\n");
				break;
			}
			if (!(myrpt->p.parrotmode || myrpt->parrotonce))
			{
				char myfname[300];

				if (myrpt->parrotstream)
				{
					ast_closestream(myrpt->parrotstream);
					myrpt->parrotstream = 0;
				}
				sprintf(myfname,PARROTFILE,myrpt->name,myrpt->parrotcnt);
				strcat(myfname,".wav");
				unlink(myfname);			
			} else if (f->frametype == AST_FRAME_VOICE)
			{
				if (myrpt->parrotstream) 
					ast_writestream(myrpt->parrotstream,f);
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
		if (myrpt->voxchannel && (who == myrpt->voxchannel))
		{
			f = ast_read(myrpt->voxchannel);
			if (!f)
			{
				if (debug) printf("@@@@ rpt:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE)
			{
				n = dovox(&myrpt->vox,AST_FRAME_DATAP(f),f->datalen / 2);
				if (n != myrpt->wasvox)
				{
					if (debug) ast_log(LOG_DEBUG,"Node %s, vox %d\n",myrpt->name,n);
					myrpt->wasvox = n;
					myrpt->voxtostate = 0;
					if (n) myrpt->voxtotimer = myrpt->p.voxtimeout_ms;
					else myrpt->voxtotimer = 0;
				}
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
		if (who == myrpt->txpchannel) /* if it was a read from remote tx */
		{
			f = ast_read(myrpt->txpchannel);
			if (!f)
			{
				if (debug) printf("@@@@ rpt:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}

	    /* Handle telemetry conference output */
		if (who == myrpt->telechannel) /* if is telemetry conference output */
		{
			//if(debug)ast_log(LOG_NOTICE,"node=%s %p %p %d %d %d\n",myrpt->name,who,myrpt->telechannel,myrpt->rxchankeyed,myrpt->remrx,myrpt->noduck);
			if(debug)ast_log(LOG_NOTICE,"node=%s %p %p %d %d %d\n",myrpt->name,who,myrpt->telechannel,myrpt->keyed,myrpt->remrx,myrpt->noduck);
			f = ast_read(myrpt->telechannel);
			if (!f)
			{
				if (debug) ast_log(LOG_NOTICE,"node=%s telechannel Hung Up implied\n",myrpt->name);
				break;
			}
			if (f->frametype == AST_FRAME_VOICE)
			{
				float gain;

				//if(!myrpt->noduck&&(myrpt->rxchankeyed||myrpt->remrx)) /* This is for when/if simple voter is implemented.  It replaces the line below it. */
				if(!myrpt->noduck&&(myrpt->keyed||myrpt->remrx))
					gain = myrpt->p.telemduckgain;
				else
					gain = myrpt->p.telemnomgain;

				//ast_log(LOG_NOTICE,"node=%s %i %i telem gain set %d %d %d\n",myrpt->name,who,myrpt->telechannel,myrpt->rxchankeyed,myrpt->noduck);

				if(gain!=0)
				{
					int n,k;
					short *sp = (short *) f->data;
					for(n=0; n<f->datalen/2; n++)
					{
						k=sp[n]*gain;
						if (k > 32767) k = 32767;
						else if (k < -32767) k = -32767;
						sp[n]=k;
					}
				}
				ast_write(myrpt->btelechannel,f);
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if ( debug>5 ) ast_log(LOG_NOTICE,"node=%s telechannel Hung Up\n",myrpt->name);
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
		/* if is btelemetry conference output */
		if (who == myrpt->btelechannel)
		{
			f = ast_read(myrpt->btelechannel);
			if (!f)
			{
				if (debug) ast_log(LOG_NOTICE,"node=%s btelechannel Hung Up implied\n",myrpt->name);
				break;
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if ( debug>5 ) ast_log(LOG_NOTICE,"node=%s btelechannel Hung Up\n",myrpt->name);
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
	}
	/*
	terminate and cleanup app_rpt node instance
	*/
	myrpt->ready = 0;
	usleep(100000);
	/* wait for telem to be done */
	while(myrpt->tele.next != &myrpt->tele) usleep(50000);
	ast_hangup(myrpt->pchannel);
	ast_hangup(myrpt->monchannel);
	if (myrpt->parrotchannel) ast_hangup(myrpt->parrotchannel);
	myrpt->parrotstate = 0;
	if (myrpt->voxchannel) ast_hangup(myrpt->voxchannel);
	ast_hangup(myrpt->btelechannel);
	ast_hangup(myrpt->telechannel);
	ast_hangup(myrpt->txpchannel);
	if (myrpt->txchannel != myrpt->rxchannel) ast_hangup(myrpt->txchannel);
	if (myrpt->zaptxchannel != myrpt->txchannel) ast_hangup(myrpt->zaptxchannel);
	if (myrpt->lastf1) ast_frfree(myrpt->lastf1);
	myrpt->lastf1 = NULL;
	if (myrpt->lastf2) ast_frfree(myrpt->lastf2);
	myrpt->lastf2 = NULL;
	ast_hangup(myrpt->rxchannel);
	rpt_mutex_lock(&myrpt->lock);
	l = myrpt->links.next;
	while(l != &myrpt->links)
	{
		struct rpt_link *ll = l;
		/* remove from queue */
		remque((struct qelem *) l);
		/* hang-up on call to device */
		if (l->chan) ast_hangup(l->chan);
		ast_hangup(l->pchan);
		l = l->next;
		ast_free(ll);
	}
	if (myrpt->xlink  == 1) myrpt->xlink = 2;
	rpt_mutex_unlock(&myrpt->lock);
	if (debug) printf("@@@@ rpt:Hung up channel\n");
	myrpt->rpt_thread = AST_PTHREADT_STOP;
	if (myrpt->outstreampid) kill(myrpt->outstreampid,SIGTERM);
	myrpt->outstreampid = 0;
	pthread_exit(NULL); 
	return NULL;
}

	
static void *rpt_master(void *ignore)
{
int	i,n;
pthread_attr_t attr;
struct ast_config *cfg;
char *this,*val;

	/* init nodelog queue */
	nodelog.next = nodelog.prev = &nodelog;
	/* go thru all the specified repeaters */
	this = NULL;
	n = 0;
#ifndef OLD_ASTERISK
	/* wait until asterisk starts */
        while(!ast_test_flag(&ast_options,AST_OPT_FLAG_FULLY_BOOTED))
                usleep(250000);
#endif
#ifdef	NEW_ASTERISK
	rpt_vars[n].cfg = ast_config_load("rpt.conf",config_flags);
#else
	rpt_vars[n].cfg = ast_config_load("rpt.conf");
#endif
	cfg = rpt_vars[n].cfg;
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to open radio repeater configuration rpt.conf.  Radio Repeater disabled.\n");
		pthread_exit(NULL);
	}

	/*
 	* If there are daq devices present, open and initialize them
 	*/
	daq_init(cfg);


	while((this = ast_category_browse(cfg,this)) != NULL)
	{
		for(i = 0 ; i < strlen(this) ; i++){
			if((this[i] < '0') || (this[i] > '9'))
				break;
		}
		if (i != strlen(this)) continue; /* Not a node defn */
		if (n >= MAXRPTS)
		{
			ast_log(LOG_ERROR,"Attempting to add repeater node %s would exceed max. number of repeaters (%d)\n",this,MAXRPTS);
			continue;
		}
		memset(&rpt_vars[n],0,sizeof(rpt_vars[n]));
		rpt_vars[n].name = ast_strdup(this);
		val = (char *) ast_variable_retrieve(cfg,this,"rxchannel");
		if (val) rpt_vars[n].rxchanname = ast_strdup(val);
		val = (char *) ast_variable_retrieve(cfg,this,"txchannel");
		if (val) rpt_vars[n].txchanname = ast_strdup(val);
		rpt_vars[n].remote = 0;
		rpt_vars[n].remoterig = "";
		rpt_vars[n].p.iospeed = B9600;
		rpt_vars[n].ready = 0;
		val = (char *) ast_variable_retrieve(cfg,this,"remote");
		if (val) 
		{
			rpt_vars[n].remoterig = ast_strdup(val);
			rpt_vars[n].remote = 1;
		}
		val = (char *) ast_variable_retrieve(cfg,this,"radiotype");
		if (val) rpt_vars[n].remoterig = ast_strdup(val);
		ast_mutex_init(&rpt_vars[n].lock);
		ast_mutex_init(&rpt_vars[n].remlock);
		ast_mutex_init(&rpt_vars[n].statpost_lock);
		rpt_vars[n].tele.next = &rpt_vars[n].tele;
		rpt_vars[n].tele.prev = &rpt_vars[n].tele;
		rpt_vars[n].rpt_thread = AST_PTHREADT_NULL;
		rpt_vars[n].tailmessagen = 0;
#ifdef	_MDC_DECODE_H_
		rpt_vars[n].mdc = mdc_decoder_new(8000);
#endif
		n++;
	}
	nrpts = n;
	ast_config_destroy(cfg);

	/* start em all */
	for(i = 0; i < n; i++)
	{
		load_rpt_vars(i,1);

		/* if is a remote, dont start one for it */
		if (rpt_vars[i].remote)
		{
			if(retrieve_memory(&rpt_vars[i],"init")){ /* Try to retrieve initial memory channel */
				if ((!strcmp(rpt_vars[i].remoterig,remote_rig_rtx450)) ||
				    (!strcmp(rpt_vars[i].remoterig,remote_rig_xcat)))
					strncpy(rpt_vars[i].freq, "446.500", sizeof(rpt_vars[i].freq) - 1);
					
				else
					strncpy(rpt_vars[i].freq, "145.000", sizeof(rpt_vars[i].freq) - 1);
				strncpy(rpt_vars[i].rxpl, "100.0", sizeof(rpt_vars[i].rxpl) - 1);

				strncpy(rpt_vars[i].txpl, "100.0", sizeof(rpt_vars[i].txpl) - 1);
				rpt_vars[i].remmode = REM_MODE_FM;
				rpt_vars[i].offset = REM_SIMPLEX;
				rpt_vars[i].powerlevel = REM_LOWPWR;
				rpt_vars[i].splitkhz = 0;
			}
			continue;
		}
		else /* is a normal repeater */
		{
		    rpt_vars[i].p.memory = rpt_vars[i].name;
			if(retrieve_memory(&rpt_vars[i],"radiofreq")){ /* Try to retrieve initial memory channel */
				if (!strcmp(rpt_vars[i].remoterig,remote_rig_rtx450))
					strncpy(rpt_vars[i].freq, "446.500", sizeof(rpt_vars[i].freq) - 1);
				else if (!strcmp(rpt_vars[i].remoterig,remote_rig_rtx150))
					strncpy(rpt_vars[i].freq, "146.580", sizeof(rpt_vars[i].freq) - 1);
				strncpy(rpt_vars[i].rxpl, "100.0", sizeof(rpt_vars[i].rxpl) - 1);

				strncpy(rpt_vars[i].txpl, "100.0", sizeof(rpt_vars[i].txpl) - 1);
				rpt_vars[i].remmode = REM_MODE_FM;
				rpt_vars[i].offset = REM_SIMPLEX;
				rpt_vars[i].powerlevel = REM_LOWPWR;
				rpt_vars[i].splitkhz = 0;
			}
			ast_log(LOG_NOTICE,"Normal Repeater Init  %s  %s  %s\n",rpt_vars[i].name, rpt_vars[i].remoterig, rpt_vars[i].freq);
		}
		if (rpt_vars[i].p.ident && (!*rpt_vars[i].p.ident))
		{
			ast_log(LOG_WARNING,"Did not specify ident for node %s\n",rpt_vars[i].name);
			ast_config_destroy(cfg);
			pthread_exit(NULL);
		}
		rpt_vars[i].ready = 0;
	        pthread_attr_init(&attr);
	        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		ast_pthread_create(&rpt_vars[i].rpt_thread,&attr,rpt,(void *) &rpt_vars[i]);
	}
	usleep(500000);
	time(&starttime);
	ast_mutex_lock(&rpt_master_lock);
	for(;;)
	{
		/* Now monitor each thread, and restart it if necessary */
		for(i = 0; i < nrpts; i++)
		{ 
			int rv;
			if (rpt_vars[i].remote) continue;
			if ((rpt_vars[i].rpt_thread == AST_PTHREADT_STOP) 
				|| (rpt_vars[i].rpt_thread == AST_PTHREADT_NULL))
				rv = -1;
			else
				rv = pthread_kill(rpt_vars[i].rpt_thread,0);
			if (rv)
			{
				if (rpt_vars[i].deleted)
				{
					rpt_vars[i].name[0] = 0;
					continue;
				}
				if(time(NULL) - rpt_vars[i].lastthreadrestarttime <= 5)
				{
					if(rpt_vars[i].threadrestarts >= 5)
					{
						ast_log(LOG_ERROR,"Continual RPT thread restarts, killing Asterisk\n");
						exit(1); /* Stuck in a restart loop, kill Asterisk and start over */
					}
					else
					{
						ast_log(LOG_NOTICE,"RPT thread restarted on %s\n",rpt_vars[i].name);
						rpt_vars[i].threadrestarts++;
					}
				}
				else
					rpt_vars[i].threadrestarts = 0;

				rpt_vars[i].lastthreadrestarttime = time(NULL);
			        pthread_attr_init(&attr);
	 		        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
				ast_pthread_create(&rpt_vars[i].rpt_thread,&attr,rpt,(void *) &rpt_vars[i]);
				/* if (!rpt_vars[i].xlink) */
					ast_log(LOG_WARNING, "rpt_thread restarted on node %s\n", rpt_vars[i].name);
			}

		}
		for(i = 0; i < nrpts; i++)
		{ 
			if (rpt_vars[i].deleted) continue;
			if (rpt_vars[i].remote) continue;
			if (!rpt_vars[i].p.outstreamcmd) continue;
			if (rpt_vars[i].outstreampid && 
				(kill(rpt_vars[i].outstreampid,0) != -1)) continue;
			rpt_vars[i].outstreampid = 0;
			startoutstream(&rpt_vars[i]);
		}			
		for(;;)
		{
			struct nodelog *nodep;
			char *space,datestr[100],fname[1024];
			int fd;

			ast_mutex_lock(&nodeloglock);
			nodep = nodelog.next;
			if(nodep == &nodelog) /* if nothing in queue */
			{
				ast_mutex_unlock(&nodeloglock);
				break;
			}
			remque((struct qelem *)nodep);
			ast_mutex_unlock(&nodeloglock);
			space = strchr(nodep->str,' ');
			if (!space) 
			{
				ast_free(nodep);
				continue;
			}
			*space = 0;
			strftime(datestr,sizeof(datestr) - 1,"%Y%m%d",
				localtime(&nodep->timestamp));
			sprintf(fname,"%s/%s/%s.txt",nodep->archivedir,
				nodep->str,datestr);
			fd = open(fname,O_WRONLY | O_CREAT | O_APPEND,0600);
			if (fd == -1)
			{
				ast_log(LOG_ERROR,"Cannot open node log file %s for write",space + 1);
				ast_free(nodep);
				continue;
			}
			if (write(fd,space + 1,strlen(space + 1)) !=
				strlen(space + 1))
			{
				ast_log(LOG_ERROR,"Cannot write node log file %s for write",space + 1);
				ast_free(nodep);
				continue;
			}
			close(fd);
			ast_free(nodep);
		}
		ast_mutex_unlock(&rpt_master_lock);
		usleep(2000000);
		ast_mutex_lock(&rpt_master_lock);
	}
	ast_mutex_unlock(&rpt_master_lock);
	ast_config_destroy(cfg);
	pthread_exit(NULL);
}

static int rpt_exec(struct ast_channel *chan, void *data)
{
	int res=-1,i,x,rem_totx,rem_rx,remkeyed,n,phone_mode = 0;
	int iskenwood_pci4,authtold,authreq,setting,notremming,reming;
	int ismuted,dtmfed,phone_vox = 0, phone_monitor = 0;
#ifdef	OLD_ASTERISK
	struct localuser *u;
#endif
	char tmp[256], keyed = 0,keyed1 = 0;
	char *options,*stringp,*callstr,*tele,c,*altp,*memp;
	char sx[320],*sy,myfirst,*b,*b1;
	struct	rpt *myrpt;
	struct ast_frame *f,*f1,*f2;
	struct ast_channel *who;
	struct ast_channel *cs[20];
	struct	rpt_link *l;
	struct dahdi_confinfo ci;  /* conference info */
	struct dahdi_params par;
	int ms,elap,n1,myrx;
	time_t t,last_timeout_warning;
	struct	dahdi_radio_param z;
	struct rpt_tele *telem;
	int	numlinks;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Rpt requires an argument (system node)\n");
		return -1;
	}

	strncpy(tmp, (char *)data, sizeof(tmp)-1);
	time(&t);
	/* if time has externally shifted negative, screw it */
	if (t < starttime) t = starttime + START_DELAY;
	if ((!starttime) || (t < (starttime + START_DELAY)))
	{
		ast_log(LOG_NOTICE,"Node %s rejecting call: too soon!\n",tmp);
		ast_safe_sleep(chan,3000);
		return -1;
	}

	ast_set_read_format(chan,AST_FORMAT_SLINEAR);
	ast_set_write_format(chan,AST_FORMAT_SLINEAR);

//	ast_log(LOG_NOTICE,"parsing argument=%s \n",tmp);

	altp=strstr(tmp, "|*");
	if(altp){
		altp[0]=0;
		altp++;
    }

	memp=strstr(tmp, "|M");
	if(memp){
		memp[0]=0;
		memp+=2;
    }

	stringp=tmp;
	strsep(&stringp, "|");
	options = stringp;
	strsep(&stringp,"|");
	callstr = stringp;
	
//	ast_log(LOG_NOTICE,"options=%s \n",options);
//	if(memp>0)ast_log(LOG_NOTICE,"memp=%s \n",memp);
//	if(altp>0)ast_log(LOG_NOTICE,"altp=%s \n",altp);

	myrpt = NULL;
	/* see if we can find our specified one */
	for(i = 0; i < nrpts; i++)
	{
		/* if name matches, assign it and exit loop */
		if (!strcmp(tmp,rpt_vars[i].name))
		{
			myrpt = &rpt_vars[i];
			break;
		}
	}

	pbx_builtin_setvar_helper(chan, "RPT_STAT_ERR", "");

	if (myrpt == NULL)
	{
		char *val,*myadr,*mypfx,sx[320],*sy,*s,*s1,*s2,*s3,dstr[1024];
		char xstr[100],hisip[100],nodeip[100],tmp1[100];
		struct ast_config *cfg;
	        struct ast_hostent ahp;
	        struct hostent *hp;
	        struct in_addr ia;

		val = NULL;
		myadr = NULL;
		b1 = chan->cid.cid_num;		
		if (b1) ast_shrink_phone_number(b1);
#ifdef	NEW_ASTERISK
		cfg = ast_config_load("rpt.conf",config_flags);
#else
		cfg = ast_config_load("rpt.conf");
#endif
		if (cfg && ((!options) || (*options == 'X') || (*options == 'F')))
		{
			myadr = (char *) ast_variable_retrieve(cfg, "proxy", "ipaddr");
			if (options && (*options == 'F'))
			{
				if (b1 && myadr) 
				{
					val = forward_node_lookup(myrpt,b1,cfg);
					strncpy(xstr,val,sizeof(xstr) - 1);
					s = xstr;
					s1 = strsep(&s,",");
					if (!strchr(s1,':') && strchr(s1,'/') && strncasecmp(s1, "local/", 6))
					{
						sy = strchr(s1,'/');		
						*sy = 0;
						sprintf(sx,"%s:4569/%s",s1,sy + 1);
						s1 = sx;
					}
					s2 = strsep(&s,",");
					if (!s2)
					{
						ast_log(LOG_WARNING, "Sepcified node %s not in correct format!!\n",val);
						ast_config_destroy(cfg);
						return -1;
					}
					val = NULL;
					if (!strcmp(s2,myadr))
						val = forward_node_lookup(myrpt,tmp,cfg);
				}

			}
			else
			{
				val = forward_node_lookup(myrpt,tmp,cfg);
			}
		}
		if (b1 && val && myadr && cfg)
		{
			strncpy(xstr,val,sizeof(xstr) - 1);
			if (!options)
			{
				if (*b1 < '1')
				{
					ast_log(LOG_WARNING, "Connect Attempt from invalid node number!!\n");
					return -1;
				}
				/* get his IP from IAX2 module */
				memset(hisip,0,sizeof(hisip));
#ifdef ALLOW_LOCAL_CHANNELS
			        /* set IP address if this is a local connection*/
			        if (strncmp(chan->name,"Local",5)==0) {
					strcpy(hisip,"127.0.0.1");
			        } else {
					pbx_substitute_variables_helper(chan,"${IAXPEER(CURRENTCHANNEL)}",hisip,sizeof(hisip) - 1);
				}
#else
				pbx_substitute_variables_helper(chan,"${IAXPEER(CURRENTCHANNEL)}",hisip,sizeof(hisip) - 1);
#endif
				if (!hisip[0])
				{
					ast_log(LOG_WARNING, "Link IP address cannot be determined!!\n");
					return -1;
				}
				/* look for his reported node string */
				val = forward_node_lookup(myrpt,b1,cfg);
				if (!val)
				{
					ast_log(LOG_WARNING, "Reported node %s cannot be found!!\n",b1);
					return -1;
				}
				strncpy(tmp1,val,sizeof(tmp1) - 1);
				s = tmp1;
				s1 = strsep(&s,",");
				if (!strchr(s1,':') && strchr(s1,'/') && strncasecmp(s1, "local/", 6))
				{
					sy = strchr(s1,'/');		
					*sy = 0;
					sprintf(sx,"%s:4569/%s",s1,sy + 1);
					s1 = sx;
				}
				s2 = strsep(&s,",");
				if (!s2)
				{
					ast_log(LOG_WARNING, "Reported node %s not in correct format!!\n",b1);
					return -1;
				}
		                if (strcmp(s2,"NONE")) {
					hp = ast_gethostbyname(s2, &ahp);
					if (!hp)
					{
						ast_log(LOG_WARNING, "Reported node %s, name %s cannot be found!!\n",b1,s2);
						return -1;
					}
					memcpy(&ia,hp->h_addr,sizeof(in_addr_t));
#ifdef	OLD_ASTERISK
					ast_inet_ntoa(nodeip,sizeof(nodeip) - 1,ia);
#else
					strncpy(nodeip,ast_inet_ntoa(ia),sizeof(nodeip) - 1);
#endif
					s3 = strchr(hisip,':');
					if (s3) *s3 = 0;
					if (strcmp(hisip,nodeip))
					{
						s3 = strchr(s1,'@');
						if (s3) s1 = s3 + 1;
						s3 = strchr(s1,'/');
						if (s3) *s3 = 0;
						s3 = strchr(s1,':');
						if (s3) *s3 = 0;
						hp = ast_gethostbyname(s1, &ahp);
						if (!hp)
						{
							ast_log(LOG_WARNING, "Reported node %s, name %s cannot be found!!\n",b1,s1);
							return -1;
						}
						memcpy(&ia,hp->h_addr,sizeof(in_addr_t));
#ifdef	OLD_ASTERISK
						ast_inet_ntoa(nodeip,sizeof(nodeip) - 1,ia);
#else
						strncpy(nodeip,ast_inet_ntoa(ia),sizeof(nodeip) - 1);
#endif
						if (strcmp(hisip,nodeip))
						{
							ast_log(LOG_WARNING, "Node %s IP %s does not match link IP %s!!\n",b1,nodeip,hisip);
							return -1;
						}
					}
				}
			}
			s = xstr;
			s1 = strsep(&s,",");
			if (!strchr(s1,':') && strchr(s1,'/') && strncasecmp(s1, "local/", 6))
			{
				sy = strchr(s1,'/');		
				*sy = 0;
				sprintf(sx,"%s:4569/%s",s1,sy + 1);
				s1 = sx;
			}
			s2 = strsep(&s,",");
			if (!s2)
			{
				ast_log(LOG_WARNING, "Sepcified node %s not in correct format!!\n",val);
				ast_config_destroy(cfg);
				return -1;
			}
			if (options && (*options == 'F'))
			{
				ast_config_destroy(cfg);
				rpt_forward(chan,s1,b1);
				return -1;
			}
			if (!strcmp(myadr,s2)) /* if we have it.. */
			{
				char tmp2[512];

				strcpy(tmp2,tmp);
				if (options && callstr) snprintf(tmp2,sizeof(tmp2) - 1,"0%s%s",callstr,tmp);
				mypfx = (char *) ast_variable_retrieve(cfg, "proxy", "nodeprefix");
				if (mypfx)
					snprintf(dstr,sizeof(dstr) - 1,"radio-proxy@%s%s/%s",mypfx,tmp,tmp2);
				else
					snprintf(dstr,sizeof(dstr) - 1,"radio-proxy@%s/%s",tmp,tmp2);
				ast_config_destroy(cfg);
				rpt_forward(chan,dstr,b1);
				return -1;
			}
			ast_config_destroy(cfg);
		}				
		pbx_builtin_setvar_helper(chan, "RPT_STAT_ERR", "NODE_NOT_FOUND");
		ast_log(LOG_WARNING, "Cannot find specified system node %s\n",tmp);
		return (priority_jump(NULL,chan));
	}

	numlinks=linkcount(myrpt);

	if(options && *options == 'q')
	{
		int res=0;
	 	char buf2[128];

		if(myrpt->keyed)
			pbx_builtin_setvar_helper(chan, "RPT_STAT_RXKEYED", "1");
		else
			pbx_builtin_setvar_helper(chan, "RPT_STAT_RXKEYED", "0");	

		if(myrpt->txkeyed)
			pbx_builtin_setvar_helper(chan, "RPT_STAT_TXKEYED", "1");
		else
			pbx_builtin_setvar_helper(chan, "RPT_STAT_TXKEYED", "0");	

		snprintf(buf2,sizeof(buf2),"%s=%i", "RPT_STAT_XLINK", myrpt->xlink);
		pbx_builtin_setvar(chan, buf2);
		snprintf(buf2,sizeof(buf2),"%s=%i", "RPT_STAT_LINKS", numlinks);
		pbx_builtin_setvar(chan, buf2);
		snprintf(buf2,sizeof(buf2),"%s=%d", "RPT_STAT_WASCHAN", myrpt->waschan);
		pbx_builtin_setvar(chan, buf2);
		snprintf(buf2,sizeof(buf2),"%s=%d", "RPT_STAT_NOWCHAN", myrpt->nowchan);
		pbx_builtin_setvar(chan, buf2);
		snprintf(buf2,sizeof(buf2),"%s=%d", "RPT_STAT_DUPLEX", myrpt->p.duplex);
		pbx_builtin_setvar(chan, buf2);
		snprintf(buf2,sizeof(buf2),"%s=%d", "RPT_STAT_PARROT", myrpt->p.parrotmode);
		pbx_builtin_setvar(chan, buf2);
		//snprintf(buf2,sizeof(buf2),"%s=%d", "RPT_STAT_PHONEVOX", myrpt->phonevox);
		//pbx_builtin_setvar(chan, buf2);
		//snprintf(buf2,sizeof(buf2),"%s=%d", "RPT_STAT_CONNECTED", myrpt->connected);
		//pbx_builtin_setvar(chan, buf2);
		snprintf(buf2,sizeof(buf2),"%s=%d", "RPT_STAT_CALLMODE", myrpt->callmode);
		pbx_builtin_setvar(chan, buf2);
		snprintf(buf2,sizeof(buf2),"%s=%s", "RPT_STAT_LASTTONE", myrpt->lasttone);
		pbx_builtin_setvar(chan, buf2);

		res=priority_jump(myrpt,chan);
		return res;
	}

	if(options && (*options == 'V' || *options == 'v'))
	{
		if (callstr && myrpt->rxchannel)
		{
			pbx_builtin_setvar(myrpt->rxchannel,callstr);
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Set Asterisk channel variable %s for node %s\n",
					callstr,myrpt->name);
		}					
		return 0;
	}

	if(options && *options == 'o')
	{
		return(channel_revert(myrpt));
	}

	#if 0
	if((altp)&&(*options == 'Z'))
	{
		rpt_push_alt_macro(myrpt,altp);
		return 0;
	}
	#endif


	/* if not phone access, must be an IAX connection */
	if (options && ((*options == 'P') || (*options == 'D') || (*options == 'R') || (*options == 'S')))
	{
		int val;

		pbx_builtin_setvar_helper(chan, "RPT_STAT_BUSY", "0");
		 
		myrpt->bargechan=0;
		if(options && strstr(options, "f")>0)
		{
			myrpt->bargechan=1;		
		}

		if(memp>0)
		{
			char radiochan;
			radiochan=strtod(data,NULL);
			// if(myrpt->nowchan!=0 && radiochan!=myrpt->nowchan && !myrpt->bargechan)

			if(numlinks>0 && radiochan!=myrpt->nowchan && !myrpt->bargechan)
			{
				pbx_builtin_setvar_helper(chan, "RPT_STAT_BUSY", "1");
				ast_log(LOG_NOTICE, "Radio Channel Busy.\n");
				return (priority_jump(myrpt,chan));
			}
			else if(radiochan!=myrpt->nowchan || myrpt->bargechan)
			{
				channel_steer(myrpt,memp);	
			}
		}
		if(altp)rpt_push_alt_macro(myrpt,altp);
		phone_mode = 1;
		if (*options == 'D') phone_mode = 2;
		if (*options == 'S') phone_mode = 3;
		ast_set_callerid(chan,"0","app_rpt user","0");
		val = 1;
		ast_channel_setoption(chan,AST_OPTION_TONE_VERIFY,&val,sizeof(char),0);
		if (strchr(options + 1,'v') || strchr(options + 1,'V')) phone_vox = 1;
		if (strchr(options + 1,'m') || strchr(options + 1,'M')) phone_monitor = 1;
	}
	else
	{
#ifdef ALLOW_LOCAL_CHANNELS
	        /* Check to insure the connection is IAX2 or Local*/
	        if ( (strncmp(chan->name,"IAX2",4)) && (strncmp(chan->name,"Local",5)) &&
		  (strncasecmp(chan->name,"echolink",8)) && (strncasecmp(chan->name,"tlb",3)) ) {
	            ast_log(LOG_WARNING, "We only accept links via IAX2, Echolink, TheLinkBox or Local!!\n");
	            return -1;
	        }
#else
		if (strncmp(chan->name,"IAX2",4) && strncasecmp(chan->name,"Echolink",8) && 
			strncasecmp(chan->name,"tlb",3))
		{
			ast_log(LOG_WARNING, "We only accept links via IAX2 or Echolink!!\n");
			return -1;
		}
#endif
	        if((myrpt->p.s[myrpt->p.sysstate_cur].txdisable) || myrpt->p.s[myrpt->p.sysstate_cur].noincomingconns){ /* Do not allow incoming radio connections if disabled or noincomingconns is set */
        	        ast_log(LOG_NOTICE, "Connect attempt to node %s  with tx disabled or NOICE cop function active", myrpt->name);
                	return -1;
        	}	
	}
	if (options && (*options == 'R'))
	{
		/* Parts of this section taken from app_parkandannounce */
		char *return_context;
		int l, m, lot, timeout = 0;
		char tmp[256],*template;
		char *working, *context, *exten, *priority;
		char *s,*orig_s;

		rpt_mutex_lock(&myrpt->lock);
		m = myrpt->callmode;
		rpt_mutex_unlock(&myrpt->lock);

		if ((!myrpt->p.nobusyout) && m)
		{
			if (chan->_state != AST_STATE_UP)
			{
				ast_indicate(chan,AST_CONTROL_BUSY);
			}
			while(ast_safe_sleep(chan,10000) != -1);
			return -1;
		}

		if (chan->_state != AST_STATE_UP)
		{
			ast_answer(chan);
			if (!phone_mode) send_newkey(chan);
		}

		l=strlen(options)+2;
		orig_s=ast_malloc(l);
		if(!orig_s) {
			ast_log(LOG_WARNING, "Out of memory\n");
			return -1;
		}
		s=orig_s;
		strncpy(s,options,l);

		template=strsep(&s,"|");
		if(!template) {
			ast_log(LOG_WARNING, "An announce template must be defined\n");
			ast_free(orig_s);
			return -1;
		} 
  
		if(s) {
			timeout = atoi(strsep(&s, "|"));
			timeout *= 1000;
		}
	
		return_context = s;
  
		if(return_context != NULL) {
			/* set the return context. Code borrowed from the Goto builtin */
    
			working = return_context;
			context = strsep(&working, "|");
			exten = strsep(&working, "|");
			if(!exten) {
				/* Only a priority in this one */
				priority = context;
				exten = NULL;
				context = NULL;
			} else {
				priority = strsep(&working, "|");
				if(!priority) {
					/* Only an extension and priority in this one */
					priority = exten;
					exten = context;
					context = NULL;
			}
		}
		if(atoi(priority) < 0) {
			ast_log(LOG_WARNING, "Priority '%s' must be a number > 0\n", priority);
			ast_free(orig_s);
			return -1;
		}
		/* At this point we have a priority and maybe an extension and a context */
		chan->priority = atoi(priority);
#ifdef OLD_ASTERISK
		if(exten && strcasecmp(exten, "BYEXTENSION"))
#else
		if(exten)
#endif
			strncpy(chan->exten, exten, sizeof(chan->exten)-1);
		if(context)
			strncpy(chan->context, context, sizeof(chan->context)-1);
		} else {  /* increment the priority by default*/
			chan->priority++;
		}

		if(option_verbose > 2) {
			ast_verbose( VERBOSE_PREFIX_3 "Return Context: (%s,%s,%d) ID: %s\n", chan->context,chan->exten, chan->priority, chan->cid.cid_num);
			if(!ast_exists_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num)) {
				ast_verbose( VERBOSE_PREFIX_3 "Warning: Return Context Invalid, call will return to default|s\n");
			}
		}
  
		/* we are using masq_park here to protect * from touching the channel once we park it.  If the channel comes out of timeout
		before we are done announcing and the channel is messed with, Kablooeee.  So we use Masq to prevent this.  */

		ast_masq_park_call(chan, NULL, timeout, &lot);

		if (option_verbose > 2) ast_verbose( VERBOSE_PREFIX_3 "Call Parking Called, lot: %d, timeout: %d, context: %s\n", lot, timeout, return_context);

		snprintf(tmp,sizeof(tmp) - 1,"%d,%s",lot,template + 1);

		rpt_telemetry(myrpt,REV_PATCH,tmp);

		ast_free(orig_s);

		return 0;

	}
	i = 0;
	if (!strncasecmp(chan->name,"echolink",8)) i = 1;
	if (!strncasecmp(chan->name,"tlb",3)) i = 1;
	if ((!options) && (!i))
	{
	        struct ast_hostent ahp;
	        struct hostent *hp;
	        struct in_addr ia;
	        char hisip[100],nodeip[100], *s, *s1, *s2, *s3;

		/* look at callerid to see what node this comes from */
		if (!chan->cid.cid_num) /* if doesn't have caller id */
		{
			ast_log(LOG_WARNING, "Does not have callerid on %s\n",tmp);
			return -1;
		}
		/* get his IP from IAX2 module */
		memset(hisip,0,sizeof(hisip));
#ifdef ALLOW_LOCAL_CHANNELS
	        /* set IP address if this is a local connection*/
	        if (strncmp(chan->name,"Local",5)==0) {
	            strcpy(hisip,"127.0.0.1");
	        } else {
			pbx_substitute_variables_helper(chan,"${IAXPEER(CURRENTCHANNEL)}",hisip,sizeof(hisip) - 1);
		}
#else
		pbx_substitute_variables_helper(chan,"${IAXPEER(CURRENTCHANNEL)}",hisip,sizeof(hisip) - 1);
#endif

		if (!hisip[0])
		{
			ast_log(LOG_WARNING, "Link IP address cannot be determined!!\n");
			return -1;
		}
		b = chan->cid.cid_name;
		b1 = chan->cid.cid_num;		
		ast_shrink_phone_number(b1);
		if (!strcmp(myrpt->name,b1))
		{
			ast_log(LOG_WARNING, "Trying to link to self!!\n");
			return -1;
		}

		if (*b1 < '1')
		{
			ast_log(LOG_WARNING, "Node %s Invalid for connection here!!\n",b1);
			return -1;
		}


		/* look for his reported node string */
		if (!node_lookup(myrpt,b1,tmp,sizeof(tmp) - 1,0))
		{
			ast_log(LOG_WARNING, "Reported node %s cannot be found!!\n",b1);
			return -1;
		}
		s = tmp;
		s1 = strsep(&s,",");
		if (!strchr(s1,':') && strchr(s1,'/') && strncasecmp(s1, "local/", 6))
		{
			sy = strchr(s1,'/');		
			*sy = 0;
			sprintf(sx,"%s:4569/%s",s1,sy + 1);
			s1 = sx;
		}
		s2 = strsep(&s,",");
		if (!s2)
		{
			ast_log(LOG_WARNING, "Reported node %s not in correct format!!\n",b1);
			return -1;
		}
                if (strcmp(s2,"NONE")) {
			hp = ast_gethostbyname(s2, &ahp);
			if (!hp)
			{
				ast_log(LOG_WARNING, "Reported node %s, name %s cannot be found!!\n",b1,s2);
				return -1;
			}
			memcpy(&ia,hp->h_addr,sizeof(in_addr_t));
#ifdef	OLD_ASTERISK
			ast_inet_ntoa(nodeip,sizeof(nodeip) - 1,ia);
#else
			strncpy(nodeip,ast_inet_ntoa(ia),sizeof(nodeip) - 1);
#endif
			s3 = strchr(hisip,':');
			if (s3) *s3 = 0;
			if (strcmp(hisip,nodeip))
			{
				s3 = strchr(s1,'@');
				if (s3) s1 = s3 + 1;
				s3 = strchr(s1,'/');
				if (s3) *s3 = 0;
				s3 = strchr(s1,':');
				if (s3) *s3 = 0;
				hp = ast_gethostbyname(s1, &ahp);
				if (!hp)
				{
					ast_log(LOG_WARNING, "Reported node %s, name %s cannot be found!!\n",b1,s1);
					return -1;
				}
				memcpy(&ia,hp->h_addr,sizeof(in_addr_t));
#ifdef	OLD_ASTERISK
				ast_inet_ntoa(nodeip,sizeof(nodeip) - 1,ia);
#else
				strncpy(nodeip,ast_inet_ntoa(ia),sizeof(nodeip) - 1);
#endif
				if (strcmp(hisip,nodeip))
				{
					ast_log(LOG_WARNING, "Node %s IP %s does not match link IP %s!!\n",b1,nodeip,hisip);
					return -1;
				}
			}
		}
	}

	/* if is not a remote */
	if (!myrpt->remote)
	{
		int reconnects = 0;
		struct timeval now;

		rpt_mutex_lock(&myrpt->lock);
		i = myrpt->xlink || (!myrpt->ready);
		rpt_mutex_unlock(&myrpt->lock);
		if (i)
		{
			ast_log(LOG_WARNING, "Cannot connect to node %s, system busy\n",myrpt->name);
			return -1;
		}
		rpt_mutex_lock(&myrpt->lock);
		gettimeofday(&now,NULL);
		while ((!ast_tvzero(myrpt->lastlinktime)) && (ast_tvdiff_ms(now,myrpt->lastlinktime) < 250))
		{
			rpt_mutex_unlock(&myrpt->lock);
			if (ast_check_hangup(myrpt->rxchannel)) return -1;
			if (ast_safe_sleep(myrpt->rxchannel,100) == -1) return -1;
			rpt_mutex_lock(&myrpt->lock);
			gettimeofday(&now,NULL);
		}
		gettimeofday(&myrpt->lastlinktime,NULL);
		rpt_mutex_unlock(&myrpt->lock);
		/* look at callerid to see what node this comes from */
		if (!chan->cid.cid_num) /* if doesn't have caller id */
		{
			ast_log(LOG_WARNING, "Doesnt have callerid on %s\n",tmp);
			return -1;
		}
		if (phone_mode) 
		{
			b1 = "0";
			b = NULL;
			if (callstr) b1 = callstr;
		}
		else 
		{
			b = chan->cid.cid_name;
			b1 = chan->cid.cid_num;
			ast_shrink_phone_number(b1);
			/* if is an IAX client */
			if ((b1[0] == '0') && b && b[0] && (strlen(b) <= 8))
				b1 = b;
		}
		if (!strcmp(myrpt->name,b1))
		{
			ast_log(LOG_WARNING, "Trying to link to self!!\n");
			return -1;
		}
		for(i = 0; b1[i]; i++)
		{
			if (!isdigit(b1[i])) break;
		}
		if (!b1[i]) /* if not a call-based node number */
		{
			rpt_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
			/* try to find this one in queue */
			while(l != &myrpt->links)
			{
				if (l->name[0] == '0') 
				{
					l = l->next;
					continue;
				}
				/* if found matching string */
				if (!strcmp(l->name,b1)) break;
				l = l->next;
			}
			/* if found */
			if (l != &myrpt->links) 
			{
				l->killme = 1;
				l->retries = l->max_retries + 1;
				l->disced = 2;
				reconnects = l->reconnects;
				reconnects++;
	                        rpt_mutex_unlock(&myrpt->lock);
				usleep(500000);	
			} else 
				rpt_mutex_unlock(&myrpt->lock);
		}
		/* establish call in tranceive mode */
		l = ast_malloc(sizeof(struct rpt_link));
		if (!l)
		{
			ast_log(LOG_WARNING, "Unable to malloc\n");
			pthread_exit(NULL);
		}
		/* zero the silly thing */
		memset((char *)l,0,sizeof(struct rpt_link));
		l->mode = 1;
		strncpy(l->name,b1,MAXNODESTR - 1);
		l->isremote = 0;
		l->chan = chan;
		l->connected = 1;
		l->thisconnected = 1;
		l->hasconnected = 1;
		l->reconnects = reconnects;
		l->phonemode = phone_mode;
		l->phonevox = phone_vox;
		l->phonemonitor = phone_monitor;
		l->lastf1 = NULL;
		l->lastf2 = NULL;
		l->dtmfed = 0;
		l->gott = 0;
		l->rxlingertimer = ((l->iaxkey) ? RX_LINGER_TIME_IAXKEY : RX_LINGER_TIME);
		l->newkeytimer = NEWKEYTIME;
		l->newkey = 0;
		l->iaxkey = 0;
		if ((!phone_mode) && (l->name[0] != '0') &&
		    strncasecmp(chan->name,"echolink",8) && 
			strncasecmp(chan->name,"tlb",3)) l->newkey = 2;
		if (l->name[0] > '9') l->newkeytimer = 0;
		voxinit_link(l,1);
		if (!strncasecmp(chan->name,"echolink",8)) 
			init_linkmode(myrpt,l,LINKMODE_ECHOLINK);
		else if (!strncasecmp(chan->name,"tlb",3)) 
			init_linkmode(myrpt,l,LINKMODE_TLB);
		else if (phone_mode) init_linkmode(myrpt,l,LINKMODE_PHONE);
		else init_linkmode(myrpt,l,LINKMODE_GUI);	
		ast_set_read_format(l->chan,AST_FORMAT_SLINEAR);
		ast_set_write_format(l->chan,AST_FORMAT_SLINEAR);
		gettimeofday(&myrpt->lastlinktime,NULL);
		/* allocate a pseudo-channel thru asterisk */
		l->pchan = ast_request(DAHDI_CHANNEL_NAME,AST_FORMAT_SLINEAR,"pseudo",NULL);
		if (!l->pchan)
		{
			fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
			pthread_exit(NULL);
		}
		ast_set_read_format(l->pchan,AST_FORMAT_SLINEAR);
		ast_set_write_format(l->pchan,AST_FORMAT_SLINEAR);
		ast_answer(l->pchan);
#ifdef	AST_CDR_FLAG_POST_DISABLED
		if (l->pchan->cdr)
			ast_set_flag(l->pchan->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
		/* make a conference for the tx */
		ci.chan = 0;
		ci.confno = myrpt->conf;
		ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER;
		/* first put the channel on the conference in proper mode */
		if (ioctl(l->pchan->fds[0],DAHDI_SETCONF,&ci) == -1)
		{
			ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
			pthread_exit(NULL);
		}
		rpt_mutex_lock(&myrpt->lock);
		if ((phone_mode == 2) && (!phone_vox)) l->lastrealrx = 1;
		l->max_retries = MAX_RETRIES;
		/* insert at end of queue */
		insque((struct qelem *)l,(struct qelem *)myrpt->links.next);
		__kickshort(myrpt);
		gettimeofday(&myrpt->lastlinktime,NULL);
		rpt_mutex_unlock(&myrpt->lock);
		if (chan->_state != AST_STATE_UP)
		{
			ast_answer(chan);
			if (l->name[0] > '9')
			{
				if (ast_safe_sleep(chan,500) == -1) return -1;
			}
			else
			{
				if (!phone_mode) send_newkey(chan);
			}
		}
		rpt_update_links(myrpt);
		if (myrpt->p.archivedir)
		{
			char str[512];

			if (l->phonemode)
				sprintf(str,"LINK(P),%s",l->name);
			else
				sprintf(str,"LINK,%s",l->name);
			donodelog(myrpt,str);
		}
		doconpgm(myrpt,l->name);
		if ((!phone_mode) && (l->name[0] <=  '9'))
			send_newkey(chan);
		if ((!strncasecmp(l->chan->name,"echolink",8)) ||
		    (!strncasecmp(l->chan->name,"tlb",3)) ||
		      (l->name[0] > '9'))
			rpt_telemetry(myrpt,CONNECTED,l);
		return AST_PBX_KEEPALIVE;
	}
	/* well, then it is a remote */
	rpt_mutex_lock(&myrpt->lock);
	/* look at callerid to see what node this comes from */
	if (!chan->cid.cid_num) /* if doesn't have caller id */
	{
		b1 = "0";
		b = NULL;
	} else {
		b = chan->cid.cid_name;
		b1 = chan->cid.cid_num;
		ast_shrink_phone_number(b1);
	}
	/* if is an IAX client */
	if ((b1[0] == '0') && b && b[0] && (strlen(b) <= 8))
		b1 = b;
	if (b1 && (*b1 > '9')) myrpt->remote_webtransceiver = chan;
	/* if remote, error if anyone else already linked */
	if (myrpt->remoteon)
	{
		rpt_mutex_unlock(&myrpt->lock);
		usleep(500000);
		if (myrpt->remoteon)
		{
#ifdef	NEW_ASTERISK
			struct ast_tone_zone_sound *ts = NULL;
#else
			const struct ind_tone_zone_sound *ts = NULL;
#endif

			ast_log(LOG_WARNING, "Trying to use busy link on %s\n",tmp);
			if (myrpt->remote_webtransceiver || (b && (*b > '9')))
			{
				ts = ast_get_indication_tone(chan->zone, "busy");
				ast_playtones_start(chan,0,ts->data, 1);
				i = 0;
				while(chan->generatordata && (i < 5000))
				{
					if(ast_safe_sleep(chan, 20)) break;
					i += 20;
				}
				ast_playtones_stop(chan);
			}
#ifdef	AST_CDR_FLAG_POST_DISABLED
			if (chan->cdr)
				ast_set_flag(chan->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
			return -1;
		}		
		rpt_mutex_lock(&myrpt->lock);
	}
	if (myrpt->p.rptnode)
	{
		char killedit = 0;
		time_t now;

		time(&now);
		for(i = 0; i < nrpts; i++)
		{
			if (!strcasecmp(rpt_vars[i].name,myrpt->p.rptnode))
			{
				if ((rpt_vars[i].links.next != &rpt_vars[i].links) ||
				   rpt_vars[i].keyed ||
				    ((rpt_vars[i].lastkeyedtime + RPT_LOCKOUT_SECS) > now) ||
				     rpt_vars[i].txkeyed ||
				      ((rpt_vars[i].lasttxkeyedtime + RPT_LOCKOUT_SECS) > now))
				{
					rpt_mutex_unlock(&myrpt->lock);
					ast_log(LOG_WARNING, "Trying to use busy link (repeater node %s) on %s\n",rpt_vars[i].name,tmp);
#ifdef	AST_CDR_FLAG_POST_DISABLED
					if (chan->cdr)
						ast_set_flag(chan->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
					return -1;
				}
				while(rpt_vars[i].xlink != 3)
				{
					if (!killedit)
					{
						ast_softhangup(rpt_vars[i].rxchannel,AST_SOFTHANGUP_DEV);
						rpt_vars[i].xlink = 1;
						killedit = 1;
					}
					rpt_mutex_unlock(&myrpt->lock);
					if (ast_safe_sleep(chan,500) == -1)
					{
#ifdef	AST_CDR_FLAG_POST_DISABLED
						if (chan->cdr)
							ast_set_flag(chan->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
						return -1;
					}
					rpt_mutex_lock(&myrpt->lock);
				}
				break;
			}
		}
	}

	if ( (!strcmp(myrpt->remoterig, remote_rig_rbi)||!strcmp(myrpt->remoterig, remote_rig_ppp16)) &&
	  (ioperm(myrpt->p.iobase,1,1) == -1))
	{
		rpt_mutex_unlock(&myrpt->lock);
		ast_log(LOG_WARNING, "Can't get io permission on IO port %x hex\n",myrpt->p.iobase);
		return -1;
	}
	myrpt->remoteon = 1;
#ifdef	OLD_ASTERISK
	LOCAL_USER_ADD(u);
#endif
	voxinit_rpt(myrpt,1);
	rpt_mutex_unlock(&myrpt->lock);
	/* find our index, and load the vars initially */
	for(i = 0; i < nrpts; i++)
	{
		if (&rpt_vars[i] == myrpt)
		{
			load_rpt_vars(i,0);
			break;
		}
	}
	rpt_mutex_lock(&myrpt->lock);
	tele = strchr(myrpt->rxchanname,'/');
	if (!tele)
	{
		fprintf(stderr,"rpt:Dial number must be in format tech/number\n");
		rpt_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	*tele++ = 0;
	myrpt->rxchannel = ast_request(myrpt->rxchanname,AST_FORMAT_SLINEAR,tele,NULL);
	myrpt->zaprxchannel = NULL;
	if (!strcasecmp(myrpt->rxchanname,DAHDI_CHANNEL_NAME))
		myrpt->zaprxchannel = myrpt->rxchannel;
	if (myrpt->rxchannel)
	{
		ast_set_read_format(myrpt->rxchannel,AST_FORMAT_SLINEAR);
		ast_set_write_format(myrpt->rxchannel,AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
		if (myrpt->rxchannel->cdr)
			ast_set_flag(myrpt->rxchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
#ifndef	NEW_ASTERISK
		myrpt->rxchannel->whentohangup = 0;
#endif
		myrpt->rxchannel->appl = "Apprpt";
		myrpt->rxchannel->data = "(Link Rx)";
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "rpt (Rx) initiating call to %s/%s on %s\n",
				myrpt->rxchanname,tele,myrpt->rxchannel->name);
		rpt_mutex_unlock(&myrpt->lock);
		ast_call(myrpt->rxchannel,tele,999);
		rpt_mutex_lock(&myrpt->lock);
	}
	else
	{
		fprintf(stderr,"rpt:Sorry unable to obtain Rx channel\n");
		rpt_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	*--tele = '/';
	myrpt->zaptxchannel = NULL;
	if (myrpt->txchanname)
	{
		tele = strchr(myrpt->txchanname,'/');
		if (!tele)
		{
			fprintf(stderr,"rpt:Dial number must be in format tech/number\n");
			rpt_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			pthread_exit(NULL);
		}
		*tele++ = 0;
		myrpt->txchannel = ast_request(myrpt->txchanname,AST_FORMAT_SLINEAR,tele,NULL);
		if (!strncasecmp(myrpt->txchanname,DAHDI_CHANNEL_NAME,3))
			myrpt->zaptxchannel = myrpt->txchannel;
		if (myrpt->txchannel)
		{
			ast_set_read_format(myrpt->txchannel,AST_FORMAT_SLINEAR);
			ast_set_write_format(myrpt->txchannel,AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
			if (myrpt->txchannel->cdr)
				ast_set_flag(myrpt->txchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
#ifndef	NEW_ASTERISK
			myrpt->txchannel->whentohangup = 0;
#endif
			myrpt->txchannel->appl = "Apprpt";
			myrpt->txchannel->data = "(Link Tx)";
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "rpt (Tx) initiating call to %s/%s on %s\n",
					myrpt->txchanname,tele,myrpt->txchannel->name);
			rpt_mutex_unlock(&myrpt->lock);
			ast_call(myrpt->txchannel,tele,999);
			rpt_mutex_lock(&myrpt->lock);
		}
		else
		{
			fprintf(stderr,"rpt:Sorry unable to obtain Tx channel\n");
			rpt_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			pthread_exit(NULL);
		}
		*--tele = '/';
	}
	else
	{
		myrpt->txchannel = myrpt->rxchannel;
		if (!strncasecmp(myrpt->rxchanname,DAHDI_CHANNEL_NAME,3))
			myrpt->zaptxchannel = myrpt->rxchannel;
	}
	i = 3;
	ast_channel_setoption(myrpt->rxchannel,AST_OPTION_TONE_VERIFY,&i,sizeof(char),0);
	/* allocate a pseudo-channel thru asterisk */
	myrpt->pchannel = ast_request(DAHDI_CHANNEL_NAME,AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!myrpt->pchannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		rpt_mutex_unlock(&myrpt->lock);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		pthread_exit(NULL);
	}
	ast_set_read_format(myrpt->pchannel,AST_FORMAT_SLINEAR);
	ast_set_write_format(myrpt->pchannel,AST_FORMAT_SLINEAR);
	ast_answer(myrpt->pchannel);
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (myrpt->pchannel->cdr)
		ast_set_flag(myrpt->pchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	if (!myrpt->zaprxchannel) myrpt->zaprxchannel = myrpt->pchannel;
	if (!myrpt->zaptxchannel) myrpt->zaptxchannel = myrpt->pchannel;
	/* make a conference for the pseudo */
	ci.chan = 0;
	ci.confno = -1; /* make a new conf */
	ci.confmode = DAHDI_CONF_CONFANNMON ;
	/* first put the channel on the conference in announce/monitor mode */
	if (ioctl(myrpt->pchannel->fds[0],DAHDI_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		rpt_mutex_unlock(&myrpt->lock);
		ast_hangup(myrpt->pchannel);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		pthread_exit(NULL);
	}
	/* save pseudo channel conference number */
	myrpt->conf = myrpt->txconf = ci.confno;
	/* if serial io port, open it */
	myrpt->iofd = -1;
	if (myrpt->p.ioport && ((myrpt->iofd = openserial(myrpt,myrpt->p.ioport)) == -1))
	{
		rpt_mutex_unlock(&myrpt->lock);
		ast_hangup(myrpt->pchannel);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		pthread_exit(NULL);
	}
	iskenwood_pci4 = 0;
	memset(&z,0,sizeof(z));
	if ((myrpt->iofd < 1) && (myrpt->txchannel == myrpt->zaptxchannel))
	{
		z.radpar = DAHDI_RADPAR_REMMODE;
		z.data = DAHDI_RADPAR_REM_NONE;
		res = ioctl(myrpt->zaptxchannel->fds[0],DAHDI_RADIO_SETPARAM,&z);
		/* if PCIRADIO and kenwood selected */
		if ((!res) && (!strcmp(myrpt->remoterig,remote_rig_kenwood)))
		{
			z.radpar = DAHDI_RADPAR_UIOMODE;
			z.data = 1;
			if (ioctl(myrpt->zaptxchannel->fds[0],DAHDI_RADIO_SETPARAM,&z) == -1)
			{
				ast_log(LOG_ERROR,"Cannot set UIOMODE\n");
				return -1;
			}
			z.radpar = DAHDI_RADPAR_UIODATA;
			z.data = 3;
			if (ioctl(myrpt->zaptxchannel->fds[0],DAHDI_RADIO_SETPARAM,&z) == -1)
			{
				ast_log(LOG_ERROR,"Cannot set UIODATA\n");
				return -1;
			}
			i = DAHDI_OFFHOOK;
			if (ioctl(myrpt->zaptxchannel->fds[0],DAHDI_HOOK,&i) == -1)
			{
				ast_log(LOG_ERROR,"Cannot set hook\n");
				return -1;
			}
			iskenwood_pci4 = 1;
		}
	}
	if (myrpt->txchannel == myrpt->zaptxchannel)
	{
		i = DAHDI_ONHOOK;
		ioctl(myrpt->zaptxchannel->fds[0],DAHDI_HOOK,&i);
		/* if PCIRADIO and Yaesu ft897/ICOM IC-706 selected */
		if ((myrpt->iofd < 1) && (!res) &&
		   ((!strcmp(myrpt->remoterig,remote_rig_ft897)) ||
		    (!strcmp(myrpt->remoterig,remote_rig_ft950)) ||
		    (!strcmp(myrpt->remoterig,remote_rig_ft100)) ||
		      (!strcmp(myrpt->remoterig,remote_rig_xcat)) ||
		      (!strcmp(myrpt->remoterig,remote_rig_ic706)) ||
		         (!strcmp(myrpt->remoterig,remote_rig_tm271))))
		{
			z.radpar = DAHDI_RADPAR_UIOMODE;
			z.data = 1;
			if (ioctl(myrpt->zaptxchannel->fds[0],DAHDI_RADIO_SETPARAM,&z) == -1)
			{
				ast_log(LOG_ERROR,"Cannot set UIOMODE\n");
				return -1;
			}
			z.radpar = DAHDI_RADPAR_UIODATA;
			z.data = 3;
			if (ioctl(myrpt->zaptxchannel->fds[0],DAHDI_RADIO_SETPARAM,&z) == -1)
			{
				ast_log(LOG_ERROR,"Cannot set UIODATA\n");
				return -1;
			}
		}
	}
	if ((myrpt->p.nlconn) && ((strncasecmp(myrpt->rxchannel->name,"radio/", 6) == 0) ||
	    (strncasecmp(myrpt->rxchannel->name,"beagle/", 7) == 0) ||
		    (strncasecmp(myrpt->rxchannel->name,"simpleusb/", 10) == 0)))
	{
		/* go thru all the specs */
		for(i = 0; i < myrpt->p.nlconn; i++)
		{
			int j,k;
			char string[100];

			if (sscanf(myrpt->p.lconn[i],"GPIO%d=%d",&j,&k) == 2)
			{
				sprintf(string,"GPIO %d %d",j,k);
				ast_sendtext(myrpt->rxchannel,string);
			}
			else if (sscanf(myrpt->p.lconn[i],"PP%d=%d",&j,&k) == 2)
			{
				sprintf(string,"PP %d %d",j,k);
				ast_sendtext(myrpt->rxchannel,string);
			}
		}
	}
	myrpt->remoterx = 0;
	myrpt->remotetx = 0;
	myrpt->retxtimer = 0;
	myrpt->rerxtimer = 0;
	myrpt->remoteon = 1;
	myrpt->dtmfidx = -1;
	myrpt->dtmfbuf[0] = 0;
	myrpt->dtmf_time_rem = 0;
	myrpt->hfscanmode = 0;
	myrpt->hfscanstatus = 0;
	if (myrpt->p.startupmacro)
	{
		snprintf(myrpt->macrobuf,MAXMACRO - 1,"PPPP%s",myrpt->p.startupmacro);
	}
	time(&myrpt->start_time);
	myrpt->last_activity_time = myrpt->start_time;
	last_timeout_warning = 0;
	myrpt->reload = 0;
	myrpt->tele.next = &myrpt->tele;
	myrpt->tele.prev = &myrpt->tele;
	myrpt->newkey = 0;
	myrpt->iaxkey = 0;
	myrpt->lastitx = !myrpt->lastitx;
	myrpt->tunerequest = 0;
	myrpt->tunetx = 0;
	rpt_mutex_unlock(&myrpt->lock);
	ast_set_write_format(chan, AST_FORMAT_SLINEAR);
	ast_set_read_format(chan, AST_FORMAT_SLINEAR);
	rem_rx = 0;
	remkeyed = 0;
	/* if we are on 2w loop and are a remote, turn EC on */
	if (myrpt->remote && (myrpt->rxchannel == myrpt->txchannel))
	{
		i = 128;
		ioctl(myrpt->zaprxchannel->fds[0],DAHDI_ECHOCANCEL,&i);
	}
	if (chan->_state != AST_STATE_UP) {
		ast_answer(chan);
		if (!phone_mode) send_newkey(chan);
	}

	if (myrpt->rxchannel == myrpt->zaprxchannel)
	{
		if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_GET_PARAMS,&par) != -1)
		{
			if (par.rxisoffhook)
			{
				ast_indicate(chan,AST_CONTROL_RADIO_KEY);
				myrpt->remoterx = 1;
				remkeyed = 1;
			}
		}
	}
	/* look at callerid to see what node this comes from */
	if (!chan->cid.cid_num) /* if doesn't have caller id */
	{
		b1 = "0";
		b = NULL;
	} else {
		b = chan->cid.cid_name;
		b1 = chan->cid.cid_num;
		ast_shrink_phone_number(b1);
	}
	/* if is an IAX client */
	if ((b1[0] == '0') && b && b[0] && (strlen(b) <= 8))
		b1 = b;
	if (myrpt->p.archivedir)
	{
		char mycmd[512],mydate[100];
		time_t myt;
		long blocksleft;


		mkdir(myrpt->p.archivedir,0600);
		sprintf(mycmd,"%s/%s",myrpt->p.archivedir,myrpt->name);
		mkdir(mycmd,0600);
		time(&myt);
		strftime(mydate,sizeof(mydate) - 1,"%Y%m%d%H%M%S",
			localtime(&myt));
		sprintf(mycmd,"mixmonitor start %s %s/%s/%s.wav49 a",chan->name,
			myrpt->p.archivedir,myrpt->name,mydate);
		if (myrpt->p.monminblocks)
		{
			blocksleft = diskavail(myrpt);
			if (myrpt->p.remotetimeout)
			{
				blocksleft -= (myrpt->p.remotetimeout *
					MONITOR_DISK_BLOCKS_PER_MINUTE) / 60;
			}
			if (blocksleft >= myrpt->p.monminblocks)
				ast_cli_command(nullfd,mycmd);
		} else ast_cli_command(nullfd,mycmd);
		sprintf(mycmd,"CONNECT,%s",b1);
		donodelog(myrpt,mycmd);
		rpt_update_links(myrpt);
		doconpgm(myrpt,b1);
	}
	/* if is a webtransceiver */
	if (myrpt->remote_webtransceiver) myrpt->newkey = 2;
	myrpt->loginuser[0] = 0;
	myrpt->loginlevel[0] = 0;
	myrpt->authtelltimer = 0;
	myrpt->authtimer = 0;
	authtold = 0;
	authreq = 0;
	if (myrpt->p.authlevel > 1) authreq = 1;
	setrem(myrpt); 
	n = 0;
	dtmfed = 0;
	cs[n++] = chan;
	cs[n++] = myrpt->rxchannel;
	cs[n++] = myrpt->pchannel;
	cs[n++] = myrpt->telechannel;
        cs[n++] = myrpt->btelechannel;
	if (myrpt->rxchannel != myrpt->txchannel)
		cs[n++] = myrpt->txchannel;
	if (!phone_mode) send_newkey(chan);
	myfirst = 0;
	/* start un-locked */
	for(;;) 
	{
		if (ast_check_hangup(chan)) break;
		if (ast_check_hangup(myrpt->rxchannel)) break;
		notremming = 0;
		setting = 0;
		reming = 0;
		telem = myrpt->tele.next;
		while(telem != &myrpt->tele)
		{
			if (telem->mode == SETREMOTE) setting = 1;
			if ((telem->mode == SETREMOTE) ||
			    (telem->mode == SCAN) ||
				(telem->mode == TUNE))  reming = 1;
			else notremming = 1;
			telem = telem->next;
		}
		if (myrpt->reload)
		{
			myrpt->reload = 0;
			/* find our index, and load the vars */
			for(i = 0; i < nrpts; i++)
			{
				if (&rpt_vars[i] == myrpt)
				{
					load_rpt_vars(i,0);
					break;
				}
			}
		}
		time(&t);
		if (myrpt->p.remotetimeout)
		{ 
			time_t r;

			r = (t - myrpt->start_time);
			if (r >= myrpt->p.remotetimeout)
			{
				saynode(myrpt,chan,myrpt->name);
				sayfile(chan,"rpt/timeout");
				ast_safe_sleep(chan,1000);
				break;
			}
			if ((myrpt->p.remotetimeoutwarning) && 
			    (r >= (myrpt->p.remotetimeout -
				myrpt->p.remotetimeoutwarning)) &&
				    (r <= (myrpt->p.remotetimeout - 
				    	myrpt->p.remotetimeoutwarningfreq)))
			{
				if (myrpt->p.remotetimeoutwarningfreq)
				{
				    if ((t - last_timeout_warning) >=
					myrpt->p.remotetimeoutwarningfreq)
				    {
					time(&last_timeout_warning);
					rpt_telemetry(myrpt,TIMEOUT_WARNING,0);
				    }
				}
				else
				{
				    if (!last_timeout_warning)
				    {
					time(&last_timeout_warning);
					rpt_telemetry(myrpt,TIMEOUT_WARNING,0);
				    }
				}
			}
		}
		if (myrpt->p.remoteinacttimeout && myrpt->last_activity_time)
		{ 
			time_t r;

			r = (t - myrpt->last_activity_time);
			if (r >= myrpt->p.remoteinacttimeout)
			{
				saynode(myrpt,chan,myrpt->name);
				ast_safe_sleep(chan,1000);
				break;
			}
			if ((myrpt->p.remotetimeoutwarning) && 
			    (r >= (myrpt->p.remoteinacttimeout -
				myrpt->p.remotetimeoutwarning)) &&
				    (r <= (myrpt->p.remoteinacttimeout - 
				    	myrpt->p.remotetimeoutwarningfreq)))
			{
				if (myrpt->p.remotetimeoutwarningfreq)
				{
				    if ((t - last_timeout_warning) >=
					myrpt->p.remotetimeoutwarningfreq)
				    {
					time(&last_timeout_warning);
					rpt_telemetry(myrpt,ACT_TIMEOUT_WARNING,0);
				    }
				}
				else
				{
				    if (!last_timeout_warning)
				    {
					time(&last_timeout_warning);
					rpt_telemetry(myrpt,ACT_TIMEOUT_WARNING,0);
				    }
				}
			}
		}
		ms = MSWAIT;
		who = ast_waitfor_n(cs,n,&ms);
		if (who == NULL) ms = 0;
		elap = MSWAIT - ms;
		if (myrpt->macrotimer) myrpt->macrotimer -= elap;
		if (myrpt->macrotimer < 0) myrpt->macrotimer = 0;
		if (!ms) continue;
		/* do local dtmf timer */
		if (myrpt->dtmf_local_timer)
		{
			if (myrpt->dtmf_local_timer > 1) myrpt->dtmf_local_timer -= elap;
			if (myrpt->dtmf_local_timer < 1) myrpt->dtmf_local_timer = 1;
		}
		if (myrpt->voxtotimer) myrpt->voxtotimer -= elap;
		if (myrpt->voxtotimer < 0) myrpt->voxtotimer = 0;
		myrx = keyed;
		if (phone_mode && phone_vox)
		{
			myrx = (!AST_LIST_EMPTY(&myrpt->rxq));
			if (myrpt->voxtotimer <= 0)
			{
				if (myrpt->voxtostate)
				{
					myrpt->voxtotimer = myrpt->p.voxtimeout_ms;
					myrpt->voxtostate = 0;
				}				
				else
				{
					myrpt->voxtotimer = myrpt->p.voxrecover_ms;
					myrpt->voxtostate = 1;
				}
			}
			if (!myrpt->voxtostate)
				myrx = myrx || myrpt->wasvox ;
		}
		keyed = myrx;
		if (myrpt->rxlingertimer) myrpt->rxlingertimer -= elap;
		if (myrpt->rxlingertimer < 0) myrpt->rxlingertimer = 0;

		if ((myrpt->newkey == 2) && keyed && (!myrpt->rxlingertimer))
		{
			myrpt->rerxtimer = 0;
			keyed = 0;
		}
		rpt_mutex_lock(&myrpt->lock);
		do_dtmf_local(myrpt,0);
		rpt_mutex_unlock(&myrpt->lock);
		//
		rem_totx =  myrpt->dtmf_local_timer && (!phone_mode);
		rem_totx |= keyed && (!myrpt->tunerequest);
		rem_rx = (remkeyed && (!setting)) || (myrpt->tele.next != &myrpt->tele);
		if(!strcmp(myrpt->remoterig, remote_rig_ic706))
			rem_totx |= myrpt->tunerequest;
		if(!strcmp(myrpt->remoterig, remote_rig_ft897))
			rem_totx |= (myrpt->tunetx && myrpt->tunerequest);
		if (myrpt->remstopgen < 0) rem_totx = 1;
		if (myrpt->remsetting) rem_totx = 0;
		//
	    if((debug > 6) && rem_totx) {
	    	ast_log(LOG_NOTICE,"Set rem_totx=%i.  dtmf_local_timer=%i phone_mode=%i keyed=%i tunerequest=%i\n",rem_totx,myrpt->dtmf_local_timer,phone_mode,keyed,myrpt->tunerequest);
		}
		if (keyed && (!keyed1))
		{
			keyed1 = 1;
		}

		if (!keyed && (keyed1))
		{
			time_t myt;

			keyed1 = 0;
			time(&myt);
			/* if login necessary, and not too soon */
			if ((myrpt->p.authlevel) && 
			    (!myrpt->loginlevel[0]) &&
				(myt > (t + 3)))
			{
				authreq = 1;
				authtold = 0;
				myrpt->authtelltimer = AUTHTELLTIME - AUTHTXTIME;
			}
		}

		if (rem_rx && (!myrpt->remoterx))
		{
			myrpt->remoterx = 1;
			if (myrpt->newkey < 2) ast_indicate(chan,AST_CONTROL_RADIO_KEY);
		}
		if ((!rem_rx) && (myrpt->remoterx))
		{
			myrpt->remoterx = 0;
			ast_indicate(chan,AST_CONTROL_RADIO_UNKEY);
		}
		/* if auth requested, and not authed yet */
		if (authreq && (!myrpt->loginlevel[0]))
		{
			if ((!authtold) && ((myrpt->authtelltimer += elap)
				 >= AUTHTELLTIME))
			{
				authtold = 1;
				rpt_telemetry(myrpt,LOGINREQ,NULL);
			}
			if ((myrpt->authtimer += elap) >= AUTHLOGOUTTIME)
			{
				break; /* if not logged in, hang up after a time */
			}
		}
		if (myrpt->newkey == 1)
		{
			if ((myrpt->retxtimer += elap) >= REDUNDANT_TX_TIME)
			{
				myrpt->retxtimer = 0;
				if ((myrpt->remoterx) && (!myrpt->remotetx))
					ast_indicate(chan,AST_CONTROL_RADIO_KEY);
				else
					ast_indicate(chan,AST_CONTROL_RADIO_UNKEY);
			}

			if ((myrpt->rerxtimer += elap) >= (REDUNDANT_TX_TIME * 2))
			{
				keyed = 0;
				myrpt->rerxtimer = 0;
			}
		}
		if (rem_totx && (!myrpt->remotetx))
		{
			/* if not authed, and needed, do not transmit */
			if ((!myrpt->p.authlevel) || myrpt->loginlevel[0])
			{
				if(debug > 6)
					ast_log(LOG_NOTICE,"Handle rem_totx=%i.  dtmf_local_timer=%i  tunerequest=%i\n",rem_totx,myrpt->dtmf_local_timer,myrpt->tunerequest);

				myrpt->remotetx = 1;
				if((myrpt->remtxfreqok = check_tx_freq(myrpt)))
				{
					time(&myrpt->last_activity_time);
					telem = myrpt->tele.next;
					while(telem != &myrpt->tele)
					{
						if(telem->mode == ACT_TIMEOUT_WARNING && !telem->killed)
						{
							if (telem->chan) ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV); /* Whoosh! */
							telem->killed = 1;
						}
						telem = telem->next;
					}
					if ((iskenwood_pci4) && (myrpt->txchannel == myrpt->zaptxchannel))
					{
						z.radpar = DAHDI_RADPAR_UIODATA;
						z.data = 1;
						if (ioctl(myrpt->zaptxchannel->fds[0],DAHDI_RADIO_SETPARAM,&z) == -1)
						{
							ast_log(LOG_ERROR,"Cannot set UIODATA\n");
							return -1;
						}
					}
					else
					{
						ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_KEY);
					}
					rpt_update_boolean(myrpt,"RPT_TXKEYED",1);
					if (myrpt->p.archivedir) donodelog(myrpt,"TXKEY");
				}
			}
		}
		if ((!rem_totx) && myrpt->remotetx) /* Remote base radio TX unkey */
		{
			myrpt->remotetx = 0;
			if(!myrpt->remtxfreqok){
				rpt_telemetry(myrpt,UNAUTHTX,NULL);
			}
			if ((iskenwood_pci4) && (myrpt->txchannel == myrpt->zaptxchannel))
			{
				z.radpar = DAHDI_RADPAR_UIODATA;
				z.data = 3;
				if (ioctl(myrpt->zaptxchannel->fds[0],DAHDI_RADIO_SETPARAM,&z) == -1)
				{
					ast_log(LOG_ERROR,"Cannot set UIODATA\n");
					return -1;
				}
			}
			else
			{
				ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_UNKEY);
			}
			if (myrpt->p.archivedir) donodelog(myrpt,"TXUNKEY");
			rpt_update_boolean(myrpt,"RPT_TXKEYED",0);
		}
		if (myrpt->hfscanmode){
			myrpt->scantimer -= elap;
			if(myrpt->scantimer <= 0){
				if (!reming)
				{
					myrpt->scantimer = REM_SCANTIME;
					rpt_telemetry(myrpt,SCAN,0);
				} else myrpt->scantimer = 1;
			}
		}
		rpt_mutex_lock(&myrpt->lock);
		c = myrpt->macrobuf[0];
		if (c && (!myrpt->macrotimer))
		{
			myrpt->macrotimer = MACROTIME;
			memmove(myrpt->macrobuf,myrpt->macrobuf + 1,MAXMACRO - 1);
			if ((c == 'p') || (c == 'P'))
				myrpt->macrotimer = MACROPTIME;
			rpt_mutex_unlock(&myrpt->lock);
			if (myrpt->p.archivedir)
			{
				char str[100];
					sprintf(str,"DTMF(M),%c",c);
				donodelog(myrpt,str);
			}
			if (handle_remote_dtmf_digit(myrpt,c,&keyed,0) == -1) break;
			continue;
		} else rpt_mutex_unlock(&myrpt->lock);
		if (who == chan) /* if it was a read from incomming */
		{
			f = ast_read(chan);
			if (!f)
			{
				if (debug) printf("@@@@ link:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE)
			{
				if (myrpt->newkey == 2)
				{
					myrpt->rxlingertimer = ((myrpt->iaxkey) ? RX_LINGER_TIME_IAXKEY : RX_LINGER_TIME);
					if (!keyed)
					{
						keyed = 1;
						myrpt->rerxtimer = 0;
					}
				}
				if (phone_mode && phone_vox)
				{
					n1 = dovox(&myrpt->vox,
						AST_FRAME_DATAP(f),f->datalen / 2);
					if (n1 != myrpt->wasvox)
					{
						if (debug) ast_log(LOG_DEBUG,"Remote  vox %d\n",n1);
						myrpt->wasvox = n1;
						myrpt->voxtostate = 0;
						if (n1) myrpt->voxtotimer = myrpt->p.voxtimeout_ms;
						else myrpt->voxtotimer = 0;
					}
					if (n1)
					{
						if (!myfirst)
						{
						    x = 0;
						    AST_LIST_TRAVERSE(&myrpt->rxq, f1,
							frame_list) x++;
						    for(;x < myrpt->p.simplexphonedelay; x++)
						    {
							f1 = ast_frdup(f);
							memset(AST_FRAME_DATAP(f1),0,f1->datalen);
							memset(&f1->frame_list,0,sizeof(f1->frame_list));
							AST_LIST_INSERT_TAIL(&myrpt->rxq,
								f1,frame_list);
						    }
						    myfirst = 1;
						}
						f1 = ast_frdup(f);
						memset(&f1->frame_list,0,sizeof(f1->frame_list));
						AST_LIST_INSERT_TAIL(&myrpt->rxq,f1,frame_list);
					} else myfirst = 0; 
					x = 0;
					AST_LIST_TRAVERSE(&myrpt->rxq, f1,frame_list) x++;
					if (!x)
					{
						memset(AST_FRAME_DATAP(f),0,f->datalen);
					}
					else
					{
						ast_frfree(f);
						f = AST_LIST_REMOVE_HEAD(&myrpt->rxq,frame_list);
					}
				}
				if (ioctl(chan->fds[0], DAHDI_GETCONFMUTE, &ismuted) == -1)
				{
					ismuted = 0;
				}
				/* if not transmitting, zero-out audio */
				ismuted |= (!myrpt->remotetx);
				if (dtmfed && phone_mode) ismuted = 1;
				dtmfed = 0;
				if (ismuted)
				{
					memset(AST_FRAME_DATAP(f),0,f->datalen);
					if (myrpt->lastf1)
						memset(AST_FRAME_DATAP(myrpt->lastf1),0,myrpt->lastf1->datalen);
					if (myrpt->lastf2)
						memset(AST_FRAME_DATAP(myrpt->lastf2),0,myrpt->lastf2->datalen);
				} 
				if (f) f2 = ast_frdup(f);
				else f2 = NULL;
				f1 = myrpt->lastf2;
				myrpt->lastf2 = myrpt->lastf1;
				myrpt->lastf1 = f2;
				if (ismuted)
				{
					if (myrpt->lastf1)
						memset(AST_FRAME_DATAP(myrpt->lastf1),0,myrpt->lastf1->datalen);
					if (myrpt->lastf2)
						memset(AST_FRAME_DATAP(myrpt->lastf2),0,myrpt->lastf2->datalen);
				}
				if (f1)
				{
					if (!myrpt->remstopgen)
					{
						if (phone_mode)
							ast_write(myrpt->txchannel,f1);
						else
							ast_write(myrpt->txchannel,f);
					} 
						
					ast_frfree(f1);
				}
			}
#ifndef	OLD_ASTERISK
			else if (f->frametype == AST_FRAME_DTMF_BEGIN)
			{
				if (myrpt->lastf1)
					memset(AST_FRAME_DATAP(myrpt->lastf1),0,myrpt->lastf1->datalen);
				if (myrpt->lastf2)
					memset(AST_FRAME_DATAP(myrpt->lastf2),0,myrpt->lastf2->datalen);
				dtmfed = 1;
			}
#endif
			if (f->frametype == AST_FRAME_DTMF)
			{
				if (myrpt->lastf1)
					memset(AST_FRAME_DATAP(myrpt->lastf1),0,myrpt->lastf1->datalen);
				if (myrpt->lastf2)
					memset(AST_FRAME_DATAP(myrpt->lastf2),0,myrpt->lastf2->datalen);
				dtmfed = 1;
				if (handle_remote_phone_dtmf(myrpt,f->subclass,&keyed,phone_mode) == -1)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			if (f->frametype == AST_FRAME_TEXT)
			{
				char *tstr = ast_malloc(f->datalen + 1);
				if (tstr) 
				{
					memcpy(tstr,AST_FRAME_DATAP(f),f->datalen);
					tstr[f->datalen] = 0;
					if (handle_remote_data(myrpt,tstr) == -1)
					{
						if (debug) printf("@@@@ rpt:Hung Up\n");
						ast_frfree(f);
						break;
					}
					ast_free(tstr);
				}
			}	
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
				/* if RX key */
				if ((f->subclass == AST_CONTROL_RADIO_KEY) && (myrpt->newkey < 2))
				{
					if (debug == 7) printf("@@@@ rx key\n");
					keyed = 1;
					myrpt->rerxtimer = 0;
				}
				/* if RX un-key */
				if (f->subclass == AST_CONTROL_RADIO_UNKEY)
				{
					myrpt->rerxtimer = 0;
					if (debug == 7) printf("@@@@ rx un-key\n");
					keyed = 0;
				}
			}
			ast_frfree(f);
			continue;
		}
		if (who == myrpt->rxchannel) /* if it was a read from radio */
		{
			f = ast_read(myrpt->rxchannel);
			if (!f)
			{
				if (debug) printf("@@@@ link:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE)
			{
				int myreming = 0;

				if (myrpt->remstopgen > 0)
				{
					ast_tonepair_stop(myrpt->txchannel);
					myrpt->remstopgen = 0;
				}
				if(!strcmp(myrpt->remoterig, remote_rig_kenwood))
					myreming = reming;

				if (myreming || (!remkeyed) ||
				((myrpt->remote) && (myrpt->remotetx)) ||
				  ((myrpt->remmode != REM_MODE_FM) &&
				    notremming))
					memset(AST_FRAME_DATAP(f),0,f->datalen); 
				 ast_write(myrpt->pchannel,f);
			}
			else if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
				/* if RX key */
				if (f->subclass == AST_CONTROL_RADIO_KEY)
				{
					if (debug == 7) printf("@@@@ remote rx key\n");
					if (!myrpt->remotetx)
					{
						remkeyed = 1;
					}
				}
				/* if RX un-key */
				if (f->subclass == AST_CONTROL_RADIO_UNKEY)
				{
					if (debug == 7) printf("@@@@ remote rx un-key\n");
					if (!myrpt->remotetx) 
					{
						remkeyed = 0;
					}
				}
			}
			ast_frfree(f);
			continue;
		}

	    /* Handle telemetry conference output */
		if (who == myrpt->telechannel) /* if is telemetry conference output */
		{
			f = ast_read(myrpt->telechannel);
			if (!f)
			{
				if (debug) ast_log(LOG_NOTICE,"node=%s telechannel Hung Up implied\n",myrpt->name);
				break;
			}
			if (f->frametype == AST_FRAME_VOICE)
			{
				float gain;

				if(myrpt->keyed)
					gain = myrpt->p.telemduckgain;
				else
					gain = myrpt->p.telemnomgain;

				if(gain)
				{
					int n,k;
					short *sp = (short *) f->data;
					for(n=0; n<f->datalen/2; n++)
					{
						k=sp[n]*gain;
						if (k > 32767) k = 32767;
						else if (k < -32767) k = -32767;
						sp[n]=k;
					}
				}
				ast_write(myrpt->btelechannel,f);
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if ( debug>5 ) ast_log(LOG_NOTICE,"node=%s telechannel Hung Up\n",myrpt->name);
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
		/* if is btelemetry conference output */
		if (who == myrpt->btelechannel)
		{
			f = ast_read(myrpt->btelechannel);
			if (!f)
			{
				if (debug) ast_log(LOG_NOTICE,"node=%s btelechannel Hung Up implied\n",myrpt->name);
				break;
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if ( debug>5 ) ast_log(LOG_NOTICE,"node=%s btelechannel Hung Up\n",myrpt->name);
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}

		if (who == myrpt->pchannel) /* if is remote mix output */
		{
			f = ast_read(myrpt->pchannel);
			if (!f)
			{
				if (debug) printf("@@@@ link:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE)
			{
				if ((myrpt->newkey < 2) || myrpt->remoterx ||
				    strncasecmp(chan->name,"IAX",3))
					ast_write(chan,f);
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
		if ((myrpt->rxchannel != myrpt->txchannel) && 
			(who == myrpt->txchannel)) /* do this cuz you have to */
		{
			f = ast_read(myrpt->txchannel);
			if (!f)
			{
				if (debug) printf("@@@@ link:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
 				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
	}
	if (myrpt->p.archivedir || myrpt->p.discpgm)
	{
		char mycmd[100];

		/* look at callerid to see what node this comes from */
		if (!chan->cid.cid_num) /* if doesn't have caller id */
		{
			b1 = "0";
		} else {
			ast_callerid_parse(chan->cid.cid_num,&b,&b1);
			ast_shrink_phone_number(b1);
		}
		sprintf(mycmd,"DISCONNECT,%s",b1);
		rpt_update_links(myrpt);
		if (myrpt->p.archivedir) donodelog(myrpt,mycmd);
		dodispgm(myrpt,b1);
	}
	myrpt->remote_webtransceiver = 0;
	/* wait for telem to be done */
	while(myrpt->tele.next != &myrpt->tele) usleep(50000);
	sprintf(tmp,"mixmonitor stop %s",chan->name);
	ast_cli_command(nullfd,tmp);
	rpt_mutex_lock(&myrpt->lock);
	myrpt->hfscanmode = 0;
	myrpt->hfscanstatus = 0;
	myrpt->remoteon = 0;
	rpt_mutex_unlock(&myrpt->lock);
	if (myrpt->lastf1) ast_frfree(myrpt->lastf1);
	myrpt->lastf1 = NULL;
	if (myrpt->lastf2) ast_frfree(myrpt->lastf2);
	myrpt->lastf2 = NULL;
	if ((iskenwood_pci4) && (myrpt->txchannel == myrpt->zaptxchannel))
	{
		z.radpar = DAHDI_RADPAR_UIOMODE;
		z.data = 3;
		if (ioctl(myrpt->zaptxchannel->fds[0],DAHDI_RADIO_SETPARAM,&z) == -1)
		{
			ast_log(LOG_ERROR,"Cannot set UIOMODE\n");
			return -1;
		}
		z.radpar = DAHDI_RADPAR_UIODATA;
		z.data = 3;
		if (ioctl(myrpt->zaptxchannel->fds[0],DAHDI_RADIO_SETPARAM,&z) == -1)
		{
			ast_log(LOG_ERROR,"Cannot set UIODATA\n");
			return -1;
		}
		i = DAHDI_OFFHOOK;
		if (ioctl(myrpt->zaptxchannel->fds[0],DAHDI_HOOK,&i) == -1)
		{
			ast_log(LOG_ERROR,"Cannot set hook\n");
			return -1;
		}
	}
	if ((myrpt->p.nldisc) && ((strncasecmp(myrpt->rxchannel->name,"radio/", 6) == 0) ||
	    (strncasecmp(myrpt->rxchannel->name,"beagle/", 7) == 0) ||
		    (strncasecmp(myrpt->rxchannel->name,"simpleusb/", 10) == 0)))
	{
		/* go thru all the specs */
		for(i = 0; i < myrpt->p.nldisc; i++)
		{
			int j,k;
			char string[100];

			if (sscanf(myrpt->p.ldisc[i],"GPIO%d=%d",&j,&k) == 2)
			{
				sprintf(string,"GPIO %d %d",j,k);
				ast_sendtext(myrpt->rxchannel,string);
			}
			else if (sscanf(myrpt->p.ldisc[i],"PP%d=%d",&j,&k) == 2)
			{
				sprintf(string,"PP %d %d",j,k);
 				ast_sendtext(myrpt->rxchannel,string);
			}
		}
	}
	if (myrpt->iofd) close(myrpt->iofd);
	myrpt->iofd = -1;
	ast_hangup(myrpt->pchannel);
	if (myrpt->rxchannel != myrpt->txchannel) ast_hangup(myrpt->txchannel);
	ast_hangup(myrpt->rxchannel);
	closerem(myrpt);
	if (myrpt->p.rptnode)
	{
		rpt_mutex_lock(&myrpt->lock);
		for(i = 0; i < nrpts; i++)
		{
			if (!strcasecmp(rpt_vars[i].name,myrpt->p.rptnode))
			{
				rpt_vars[i].xlink = 0;
				break;
			}
		}
		rpt_mutex_unlock(&myrpt->lock);
	}
#ifdef	OLD_ASTERISK
	LOCAL_USER_REMOVE(u);
#endif
	return res;
}

static void rpt_manager_trigger(struct rpt *myrpt, char *event, char *value)
{
	manager_event(EVENT_FLAG_CALL, event,
		"Node: %s\r\n"
		"Channel: %s\r\n"
		"EventValue: %s\r\n"
		"LastKeyedTime: %s\r\n"
		"LastTxKeyedTime: %s\r\n",
                myrpt->name, myrpt->rxchannel->name, value,
                ctime(&myrpt->lastkeyedtime), ctime(&myrpt->lasttxkeyedtime)
        );
}

#ifndef OLD_ASTERISK
/*!\brief callback to display list of locally configured nodes
   \addtogroup Group_AMI
 */

static int manager_rpt_local_nodes(struct mansession *s, const struct message *m)
{
    int i;
    astman_append(s, "<?xml version=\"1.0\"?>\r\n");
    astman_append(s, "<nodes>\r\n");
    for (i=0; i< nrpts; i++)
    {
	if (rpt_vars[i].name[0])
	        astman_append(s, "  <node>%s</node>\r\n", rpt_vars[i].name);        
    } /* for i */
    astman_append(s, "</nodes>\r\n");
    astman_append(s, "\r\n");	/* Properly terminate Manager output */
    return RESULT_SUCCESS;
} /* manager_rpt_local_nodes() */



/*
 * Append Success and ActionID to manager response message
 */

static void rpt_manager_success(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	if (!ast_strlen_zero(id))
		astman_append(s, "ActionID: %s\r\n", id);
	astman_append(s, "Response: Success\r\n");
}


static int rpt_manager_do_sawstat(struct mansession *ses, const struct message *m, char *str)
{
	int i;
	struct rpt_link *l;
	const char *node = astman_get_header(m, "Node");
	time_t now;

	time(&now);
	for(i = 0; i < nrpts; i++)
	{
		if ((node)&&(!strcmp(node,rpt_vars[i].name))){
			rpt_manager_success(ses,m);
			astman_append(ses,"Node: %s\r\n",node);

			rpt_mutex_lock(&rpt_vars[i].lock); /* LOCK */

			l = rpt_vars[i].links.next;
			while(l && (l != &rpt_vars[i].links)){
				if (l->name[0] == '0'){ // Skip '0' nodes 
					l = l->next;
					continue;
				}
				astman_append(ses, "Conn: %s %d %d %d\r\n",l->name,l->lastrx1,
					(l->lastkeytime) ? (int)(now - l->lastkeytime) : -1,
					(l->lastunkeytime) ? (int)(now - l->lastunkeytime) : -1);
				l = l->next;
			}
			rpt_mutex_unlock(&rpt_vars[i].lock); // UNLOCK 
			astman_append(ses, "\r\n");
			return(0);
		}
	}
	astman_send_error(ses, m, "RptStatus unknown or missing node");
	return 0;
}


static int rpt_manager_do_xstat(struct mansession *ses, const struct message *m, char *str)
{
	int i,j;
	char ns;
	char lbuf[MAXLINKLIST],*strs[MAXLINKLIST];
	struct rpt *myrpt;
	struct ast_var_t *newvariable;
	char *connstate;
	struct rpt_link *l;
	struct rpt_lstat *s,*t;
	struct rpt_lstat s_head;
	const char *node = astman_get_header(m, "Node");

	s = NULL;
	s_head.next = &s_head;
	s_head.prev = &s_head;


	char *parrot_ena, *sys_ena, *tot_ena, *link_ena, *patch_ena, *patch_state;
	char *sch_ena, *user_funs, *tail_type, *iconns, *tot_state, *ider_state, *tel_mode; 

	for(i = 0; i < nrpts; i++)
	{
		if ((node)&&(!strcmp(node,rpt_vars[i].name))){
			rpt_manager_success(ses,m);
			astman_append(ses,"Node: %s\r\n",node);

			/* Make a copy of all stat variables while locked */
			myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock); /* LOCK */

//### GET RPT STATUS STATES WHILE LOCKED ########################
			if(myrpt->p.parrotmode)
				parrot_ena = "1";	//"ENABLED";
			else
				parrot_ena = "0";	//"DISABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].txdisable)
				sys_ena = "0";	//"DISABLED";
			else
				sys_ena = "1";	//"ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].totdisable)
				tot_ena = "0";	//"DISABLED";
			else
				tot_ena = "1";	//"ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].linkfundisable)
				link_ena = "0";	//"DISABLED";
			else
				link_ena = "1";	//"ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].autopatchdisable)
				patch_ena = "0";	//"DISABLED";
			else
				patch_ena = "1";	//"ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].schedulerdisable)
				sch_ena = "0";	//"DISABLED";
			else
				sch_ena = "1";	//"ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].userfundisable)
				user_funs = "0";	//"DISABLED";
			else
				user_funs = "1";	//"ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].alternatetail)
				tail_type = "1";	//"ALTERNATE";
			else
				tail_type = "0";	//"STANDARD";

			if(myrpt->p.s[myrpt->p.sysstate_cur].noincomingconns)
				iconns = "0";	//"DISABLED";
			else
				iconns = "1";	//"ENABLED";

			if(!myrpt->totimer)
				tot_state = "0";	//"TIMED OUT!";
			else if(myrpt->totimer != myrpt->p.totime)
				tot_state = "1";	//"ARMED";
			else
				tot_state = "2";	//"RESET";

			if(myrpt->tailid)
				ider_state = "0";	//"QUEUED IN TAIL";
			else if(myrpt->mustid)
				ider_state = "1";	//"QUEUED FOR CLEANUP";
			else
				ider_state = "2";	//"CLEAN";


			switch(myrpt->callmode){
				case 1:
					patch_state = "0";		//"DIALING";
					break;
				case 2:
					patch_state = "1";		//"CONNECTING";
					break;
				case 3:
					patch_state = "2";		//"UP";
					break;

				case 4:
					patch_state = "3";		//"CALL FAILED";
					break;

				default:
					patch_state = "4";		//"DOWN";
			}


			if (myrpt->p.telemdynamic)
			{
				if(myrpt->telemmode == 0x7fffffff)
					tel_mode = "1";
				else if(myrpt->telemmode == 0x00)
					tel_mode = "0";
				else
					tel_mode = "2";
			}
			else{
				tel_mode= "3";
			}


//### GET CONNECTED NODE INFO ####################
			// Traverse the list of connected nodes 

			__mklinklist(myrpt,NULL,lbuf,0);

			j = 0;
			l = myrpt->links.next;
			while(l && (l != &myrpt->links)){
				if (l->name[0] == '0'){ // Skip '0' nodes 
					l = l->next;
					continue;
				}
				if((s = (struct rpt_lstat *) ast_malloc(sizeof(struct rpt_lstat))) == NULL){
					ast_log(LOG_ERROR, "Malloc failed in rpt_do_lstats\r\n");
					rpt_mutex_unlock(&myrpt->lock); // UNLOCK 
					return -1;
				}
				memset(s, 0, sizeof(struct rpt_lstat));
				strlcpy(s->name, l->name, MAXNODESTR - 1);
				if (l->chan) pbx_substitute_variables_helper(l->chan, "${IAXPEER(CURRENTCHANNEL)}", s->peer, MAXPEERSTR - 1);
				else strcpy(s->peer,"(none)");
				s->mode = l->mode;
				s->outbound = l->outbound;
				s->reconnects = l->reconnects;
				s->connecttime = l->connecttime;
				s->thisconnected = l->thisconnected;
				memcpy(s->chan_stat,l->chan_stat,NRPTSTAT * sizeof(struct rpt_chan_stat));
				insque((struct qelem *) s, (struct qelem *) s_head.next);
				memset(l->chan_stat,0,NRPTSTAT * sizeof(struct rpt_chan_stat));
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock); // UNLOCK 
			for(s = s_head.next; s != &s_head; s = s->next){
				int hours, minutes, seconds;
				long long connecttime = s->connecttime;
				char conntime[21];
				hours = connecttime/3600000L;
				connecttime %= 3600000L;
				minutes = connecttime/60000L;
				connecttime %= 60000L;
				seconds = (int)  connecttime/1000L;
				connecttime %= 1000L;
				snprintf(conntime, 20, "%02d:%02d:%02d",
					hours, minutes, seconds);
				conntime[20] = 0;
				if(s->thisconnected)
					connstate  = "ESTABLISHED";
				else
					connstate = "CONNECTING";
				astman_append(ses, "Conn: %-10s%-20s%-12d%-11s%-20s%-20s\r\n",
					s->name, s->peer, s->reconnects, (s->outbound)? "OUT":"IN", conntime, connstate);
			}	
			// destroy our local link queue 
			s = s_head.next;
			while(s != &s_head){
				t = s;
				s = s->next;
				remque((struct qelem *)t);
				ast_free(t);
			}	

			astman_append(ses,"LinkedNodes: ");
//### GET ALL LINKED NODES INFO ####################
			/* parse em */
			ns = finddelim(lbuf,strs,MAXLINKLIST);
			/* sort em */
			if (ns) qsort((void *)strs,ns,sizeof(char *),mycompar);
			for(j = 0 ;; j++){
				if(!strs[j]){
					if(!j){
						astman_append(ses,"<NONE>");
					}
					break;
				}
				astman_append(ses, "%s", strs[j]);
				if(strs[j + 1])
					astman_append(ses, ", ");

			}
			astman_append(ses,"\r\n");

//### GET VARIABLES INFO ####################
			j = 0;
			ast_channel_lock(rpt_vars[i].rxchannel);
			AST_LIST_TRAVERSE (&rpt_vars[i].rxchannel->varshead, newvariable, entries) {
				j++;
				astman_append(ses,"Var: %s=%s\r\n", ast_var_name(newvariable), ast_var_value(newvariable));
			}
			ast_channel_unlock(rpt_vars[i].rxchannel);

//### OUTPUT RPT STATUS STATES ##############
			astman_append(ses, "parrot_ena: %s\r\n", parrot_ena);
			astman_append(ses, "sys_ena: %s\r\n", sys_ena);
			astman_append(ses, "tot_ena: %s\r\n", tot_ena);
			astman_append(ses, "link_ena: %s\r\n", link_ena);
			astman_append(ses, "patch_ena: %s\r\n", patch_ena);
			astman_append(ses, "patch_state: %s\r\n", patch_state);
			astman_append(ses, "sch_ena: %s\r\n", sch_ena);
			astman_append(ses, "user_funs: %s\r\n", user_funs);
			astman_append(ses, "tail_type: %s\r\n", tail_type);
			astman_append(ses, "iconns: %s\r\n", iconns);
			astman_append(ses, "tot_state: %s\r\n", tot_state);
			astman_append(ses, "ider_state: %s\r\n", ider_state);
			astman_append(ses, "tel_mode: %s\r\n\r\n", tel_mode);

			return 0;
		}
	}
	astman_send_error(ses, m, "RptStatus unknown or missing node");
	return 0;
}


/*
* Dump statistics to manager session
*/

static int rpt_manager_do_stats(struct mansession *s, const struct message *m, char *str)
{
	int i,j,numoflinks;
	int dailytxtime, dailykerchunks;
	time_t now;
	int totalkerchunks, dailykeyups, totalkeyups, timeouts;
	int totalexecdcommands, dailyexecdcommands, hours, minutes, seconds;
	long long totaltxtime;
	struct	rpt_link *l;
	char *listoflinks[MAX_STAT_LINKS];	
	char *lastdtmfcommand,*parrot_ena;
	char *tot_state, *ider_state, *patch_state;
	char *reverse_patch_state, *sys_ena, *tot_ena, *link_ena, *patch_ena;
	char *sch_ena, *input_signal, *called_number, *user_funs, *tail_type;
	char *transmitterkeyed;
	const char *node = astman_get_header(m, "Node");
	struct rpt *myrpt;

	static char *not_applicable = "N/A";

	tot_state = ider_state = 
	patch_state = reverse_patch_state = 
	input_signal = not_applicable;
	called_number = lastdtmfcommand = transmitterkeyed = NULL;

	time(&now);
	for(i = 0; i < nrpts; i++)
	{
		if ((node)&&(!strcmp(node,rpt_vars[i].name))){
			rpt_manager_success(s,m);

			myrpt = &rpt_vars[i];

			if(myrpt->remote){ /* Remote base ? */
				char *loginuser, *loginlevel, *freq, *rxpl, *txpl, *modestr;
				char offset = 0,powerlevel = 0,rxplon = 0,txplon = 0,remoteon,remmode = 0,reportfmstuff;
				char offsetc,powerlevelc;

				loginuser = loginlevel = freq = rxpl = txpl = NULL;
				/* Make a copy of all stat variables while locked */
				rpt_mutex_lock(&myrpt->lock); /* LOCK */
				if((remoteon = myrpt->remoteon)){
					if(!ast_strlen_zero(myrpt->loginuser))
						loginuser = ast_strdup(myrpt->loginuser);
					if(!ast_strlen_zero(myrpt->loginlevel))
						loginlevel = ast_strdup(myrpt->loginlevel);
					if(!ast_strlen_zero(myrpt->freq))
						freq = ast_strdup(myrpt->freq);
					if(!ast_strlen_zero(myrpt->rxpl))
						rxpl = ast_strdup(myrpt->rxpl);
					if(!ast_strlen_zero(myrpt->txpl))
						txpl = ast_strdup(myrpt->txpl);
					remmode = myrpt->remmode;
					offset = myrpt->offset;
					powerlevel = myrpt->powerlevel;
					rxplon = myrpt->rxplon;
					txplon = myrpt->txplon;			
				}
				rpt_mutex_unlock(&myrpt->lock); /* UNLOCK */
				astman_append(s, "IsRemoteBase: YES\r\n");
				astman_append(s, "RemoteOn: %s\r\n",(remoteon) ? "YES": "NO");
				if(remoteon){
					if(loginuser){
						astman_append(s, "LogInUser: %s\r\n", loginuser);
						ast_free(loginuser);
					}
					if(loginlevel){
						astman_append(s, "LogInLevel: %s\r\n", loginlevel);
						ast_free(loginlevel);
					}
					if(freq){
						astman_append(s, "Freq: %s\r\n", freq);
						ast_free(freq);
					}
					reportfmstuff = 0;
					switch(remmode){
						case REM_MODE_FM:
							modestr = "FM";	
							reportfmstuff = 1;
							break;
						case REM_MODE_AM:
							modestr = "AM";
							break;
						case REM_MODE_USB:
							modestr = "USB";
							break;
						default:
							modestr = "LSB";
							break;
					}
					astman_append(s, "RemMode: %s\r\n", modestr);
					if(reportfmstuff){
						switch(offset){
							case REM_SIMPLEX:
								offsetc = 'S';
								break;
							case REM_MINUS:
								offsetc = '-';
								break;
							default:
								offsetc = '+';
								break;
						}
						astman_append(s, "RemOffset: %c\r\n", offsetc);
						if(rxplon && rxpl){
							astman_append(s, "RxPl: %s\r\n",rxpl);
							free(rxpl);
						}
						if(txplon && txpl){
							astman_append(s, "TxPl: %s\r\n",txpl);
							free(txpl);
						}
					}
					switch(powerlevel){
						case REM_LOWPWR:
							powerlevelc = 'L';
							break;
						case REM_MEDPWR:
							powerlevelc = 'M';
							break;
						default:
							powerlevelc = 'H';
							break;
					}
					astman_append(s,"PowerLevel: %c\r\n", powerlevelc);
				}
				astman_append(s, "\r\n");
				return 0; /* End of remote base status reporting */
			}	

			/* ELSE Process as a repeater node */
			/* Make a copy of all stat variables while locked */
			rpt_mutex_lock(&myrpt->lock); /* LOCK */
			dailytxtime = myrpt->dailytxtime;
			totaltxtime = myrpt->totaltxtime;
			dailykeyups = myrpt->dailykeyups;
			totalkeyups = myrpt->totalkeyups;
			dailykerchunks = myrpt->dailykerchunks;
			totalkerchunks = myrpt->totalkerchunks;
			dailyexecdcommands = myrpt->dailyexecdcommands;
			totalexecdcommands = myrpt->totalexecdcommands;
			timeouts = myrpt->timeouts;


			/* Traverse the list of connected nodes */
			reverse_patch_state = "DOWN";
			numoflinks = 0;
			l = myrpt->links.next;
			while(l && (l != &myrpt->links)){
				if(numoflinks >= MAX_STAT_LINKS){
					ast_log(LOG_NOTICE,
					"maximum number of links exceeds %d in rpt_do_stats()!",MAX_STAT_LINKS);
					break;
				}
				if (l->name[0] == '0'){ /* Skip '0' nodes */
					reverse_patch_state = "UP";
					l = l->next;
					continue;
				}
				listoflinks[numoflinks] = ast_strdup(l->name);
				if(listoflinks[numoflinks] == NULL){
					break;
				}
				else{
					numoflinks++;
				}
				l = l->next;
			}

			if(myrpt->keyed)
				input_signal = "YES";
			else
				input_signal = "NO";
			
			if(myrpt->txkeyed)
				transmitterkeyed = "YES";
			else
				transmitterkeyed = "NO";

			if(myrpt->p.parrotmode)
				parrot_ena = "ENABLED";
			else
				parrot_ena = "DISABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].txdisable)
				sys_ena = "DISABLED";
			else
				sys_ena = "ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].totdisable)
				tot_ena = "DISABLED";
			else
				tot_ena = "ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].linkfundisable)
				link_ena = "DISABLED";
			else
				link_ena = "ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].autopatchdisable)
				patch_ena = "DISABLED";
			else
				patch_ena = "ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].schedulerdisable)
				sch_ena = "DISABLED";
			else
				sch_ena = "ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].userfundisable)
				user_funs = "DISABLED";
			else
				user_funs = "ENABLED";

			if(myrpt->p.s[myrpt->p.sysstate_cur].alternatetail)
				tail_type = "ALTERNATE";
			else
				tail_type = "STANDARD";

			if(!myrpt->totimer)
				tot_state = "TIMED OUT!";
			else if(myrpt->totimer != myrpt->p.totime)
				tot_state = "ARMED";
			else
				tot_state = "RESET";

			if(myrpt->tailid)
				ider_state = "QUEUED IN TAIL";
			else if(myrpt->mustid)
				ider_state = "QUEUED FOR CLEANUP";
			else
				ider_state = "CLEAN";

			switch(myrpt->callmode){
				case 1:
					patch_state = "DIALING";
					break;
				case 2:
					patch_state = "CONNECTING";
					break;
				case 3:
					patch_state = "UP";
					break;

				case 4:
					patch_state = "CALL FAILED";
					break;

				default:
					patch_state = "DOWN";
			}

			if(strlen(myrpt->exten)){
				called_number = ast_strdup(myrpt->exten);
			}

			if(strlen(myrpt->lastdtmfcommand)){
				lastdtmfcommand = ast_strdup(myrpt->lastdtmfcommand);
			}
			rpt_mutex_unlock(&myrpt->lock); /* UNLOCK */

			astman_append(s, "IsRemoteBase: NO\r\n");
			astman_append(s, "NodeState: %d\r\n", myrpt->p.sysstate_cur);
			astman_append(s, "SignalOnInput: %s\r\n", input_signal);
			astman_append(s, "TransmitterKeyed: %s\r\n", transmitterkeyed);
			astman_append(s, "Transmitter: %s\r\n", sys_ena);
			astman_append(s, "Parrot: %s\r\n", parrot_ena);
			astman_append(s, "Scheduler: %s\r\n", sch_ena);
			astman_append(s, "TailLength: %s\r\n", tail_type);
			astman_append(s, "TimeOutTimer: %s\r\n", tot_ena);
			astman_append(s, "TimeOutTimerState: %s\r\n", tot_state);
			astman_append(s, "TimeOutsSinceSystemInitialization: %d\r\n", timeouts);
			astman_append(s, "IdentifierState: %s\r\n", ider_state);
			astman_append(s, "KerchunksToday: %d\r\n", dailykerchunks);
			astman_append(s, "KerchunksSinceSystemInitialization: %d\r\n", totalkerchunks);
			astman_append(s, "KeyupsToday: %d\r\n", dailykeyups);
			astman_append(s, "KeyupsSinceSystemInitialization: %d\r\n", totalkeyups);
			astman_append(s, "DtmfCommandsToday: %d\r\n", dailyexecdcommands);
			astman_append(s, "DtmfCommandsSinceSystemInitialization: %d\r\n", totalexecdcommands);
			astman_append(s, "LastDtmfCommandExecuted: %s\r\n", 
			(lastdtmfcommand && strlen(lastdtmfcommand)) ? lastdtmfcommand : not_applicable);
			hours = dailytxtime/3600000;
			dailytxtime %= 3600000;
			minutes = dailytxtime/60000;
			dailytxtime %= 60000;
			seconds = dailytxtime/1000;
			dailytxtime %= 1000;

			astman_append(s, "TxTimeToday: %02d:%02d:%02d:%02d\r\n",
				hours, minutes, seconds, dailytxtime);

			hours = (int) totaltxtime/3600000;
			totaltxtime %= 3600000;
			minutes = (int) totaltxtime/60000;
			totaltxtime %= 60000;
			seconds = (int)  totaltxtime/1000;
			totaltxtime %= 1000;

			astman_append(s, "TxTimeSinceSystemInitialization: %02d:%02d:%02d:%02d\r\n",
				 hours, minutes, seconds, (int) totaltxtime);

  			sprintf(str, "NodesCurrentlyConnectedToUs: ");
                        if(!numoflinks){
  	                      strcat(str,"<NONE>");
                        }
			else{
				for(j = 0 ;j < numoflinks; j++){
					sprintf(str+strlen(str), "%s", listoflinks[j]);
					if(j < numoflinks - 1)
						strcat(str,",");
				}
			}
			astman_append(s,"%s\r\n", str);

			astman_append(s, "Autopatch: %s\r\n", patch_ena);
			astman_append(s, "AutopatchState: %s\r\n", patch_state);
			astman_append(s, "AutopatchCalledNumber: %s\r\n",
			(called_number && strlen(called_number)) ? called_number : not_applicable);
			astman_append(s, "ReversePatchIaxrptConnected: %s\r\n", reverse_patch_state);
			astman_append(s, "UserLinkingCommands: %s\r\n", link_ena);
			astman_append(s, "UserFunctions: %s\r\n", user_funs);

			for(j = 0; j < numoflinks; j++){ /* ast_free() all link names */
				ast_free(listoflinks[j]);
			}
			if(called_number){
				ast_free(called_number);
			}
			if(lastdtmfcommand){
				ast_free(lastdtmfcommand);
			}
			astman_append(s, "\r\n"); /* We're Done! */
		        return 0;
		}
	}
	astman_send_error(s, m, "RptStatus unknown or missing node");
	return 0;
}



/*
 * Implement the RptStatus Manager Interface
 */

static int manager_rpt_status(struct mansession *s, const struct message *m)
{
	int i,res,len,index;
	int uptime,hours,minutes;
	time_t now;
	const char *cmd = astman_get_header(m, "Command");
	char *str;
	enum {MGRCMD_RPTSTAT,MGRCMD_NODESTAT,MGRCMD_XSTAT,MGRCMD_SAWSTAT};
	struct mgrcmdtbl{
		const char *cmd;
		int index;
	};
	static struct mgrcmdtbl mct[] = {
		{"RptStat",MGRCMD_RPTSTAT},
		{"NodeStat",MGRCMD_NODESTAT},
		{"XStat",MGRCMD_XSTAT},
		{"SawStat",MGRCMD_SAWSTAT},
		{NULL,0} /* NULL marks end of command table */
	};

	time(&now);

	len = 1024; /* Allocate a working buffer */
	if(!(str = ast_malloc(len)))
		return -1;

	/* Check for Command */
	if(ast_strlen_zero(cmd)){
		astman_send_error(s, m, "RptStatus missing command");
		ast_free(str);
		return 0;
	}
	/* Try to find the command in the table */
	for(i = 0 ; mct[i].cmd ; i++){
		if(!strcmp(mct[i].cmd, cmd))
			break;
	}

	if(!mct[i].cmd){ /* Found or not found ? */
		astman_send_error(s, m, "RptStatus unknown command");
		ast_free(str);
		return 0;
	}
	else
		index = mct[i].index;

	switch(index){ /* Use the index to go to the correct command */

		case MGRCMD_RPTSTAT:
			/* Return Nodes: and a comma separated list of nodes */
			if((res = snprintf(str, len, "Nodes: ")) > -1)
				len -= res;
			else{
				ast_free(str);
				return 0;
			}
			for(i = 0; i < nrpts; i++){
				if(i < nrpts - 1){
					if((res = snprintf(str+strlen(str), len, "%s,",rpt_vars[i].name)) < 0){
						ast_free(str);
						return 0;
					}
				}
				else{
					if((res = snprintf(str+strlen(str), len, "%s",rpt_vars[i].name)) < 0){
						ast_free(str);
						return 0;
					}
				}
				len -= res;
			}

			rpt_manager_success(s,m);
			
			if(!nrpts)
				astman_append(s, "<NONE>\r\n");
			else
				astman_append(s, "%s\r\n", str);

			uptime = (int)(now - starttime);
                     	hours = uptime/3600;
                        uptime %= 3600;
                        minutes = uptime/60;
                        uptime %= 60;

                        astman_append(s, "RptUptime: %02d:%02d:%02d\r\n",
                                hours, minutes, uptime);

			astman_append(s, "\r\n");
			break;		

		case	MGRCMD_NODESTAT:
			res = rpt_manager_do_stats(s,m,str);
			ast_free(str);
			return res;

		case	MGRCMD_XSTAT:
			res = rpt_manager_do_xstat(s,m,str);
			ast_free(str);
			return res;

		case	MGRCMD_SAWSTAT:
			res = rpt_manager_do_sawstat(s,m,str);
			ast_free(str);
			return res;

		default:
			astman_send_error(s, m, "RptStatus invalid command");
			break;
	}
	ast_free(str);
	return 0;
}

#endif

#ifdef	_MDC_ENCODE_H_

static char *mdc_app = "MDC1200Gen";
static char *mdc_synopsis = "MDC1200 Generator";
static char *mdc_descrip = "  MDC1200Gen(Type|UnitID[|DestID|SubCode]):  Generates MDC-1200\n"
"  burst for given UnitID. Type is 'I' for PttID, 'E' for\n"
"  Emergency, and 'C' for Call (SelCall or Alert), or 'SX' for STS\n"
"  (status), where X is 0-F (indicating the status code). DestID and\n"
"  subcode are only specified for the 'C' type message. DestID is\n"
"  The MDC1200 ID of the radio being called, and the subcodes are\n"
"  as follows: \n\n"
"     Subcode '8205' is Voice Selective Call for Spectra ('Call')\n"
"     Subcode '8015' is Voice Selective Call for Maxtrac ('SC') or \n"
"          Astro-Saber('Call')\n"
"     Subcode '810D' is Call Alert (like Maxtrac 'CA')\\n\n";

static void mdcgen_release(struct ast_channel *chan, void *params)
{
	struct mdcgen_pvt *ps = params;
	if (chan) {
		ast_set_write_format(chan, ps->origwfmt);
	}
	if (!ps) return;
	if (ps->mdc) free(ps->mdc);
	ast_free(ps);
	return;
}

static void * mdcgen_alloc(struct ast_channel *chan, void *params)
{
	struct mdcgen_pvt *ps;
	struct mdcparams *p = (struct mdcparams *)params;

	if (!(ps = ast_calloc(1, sizeof(*ps))))
		return NULL;
	ps->origwfmt = chan->writeformat;
	ps->mdc = mdc_encoder_new(8000);
	if (!ps->mdc)
	{
		ast_log(LOG_ERROR,"Unable to make new MDC encoder!!\n");
		ast_free(ps);
		return NULL;
	}
	if (p->type[0] == 'I')
	{
		mdc_encoder_set_packet(ps->mdc,1,0x80,p->UnitID);
	}
	else if (p->type[0] == 'E')
	{
		mdc_encoder_set_packet(ps->mdc,0,0x80,p->UnitID);
	}
	else if (p->type[0] == 'S')
	{
		mdc_encoder_set_packet(ps->mdc,0x46,p->type[1] - '0',p->UnitID);
	}
	else if (p->type[0] == 'C')
	{
		mdc_encoder_set_double_packet(ps->mdc,0x35,0x89,p->DestID,p->subcode >> 8,
			p->subcode & 0xff,p->UnitID >> 8,p->UnitID & 0xff);
	}
	else if (p->type[0] == 'A')
	{
		mdc_encoder_set_packet(ps->mdc,0x23,0,p->UnitID);
	}
	else if (p->type[0] == 'K') // kill a unit W9CR
	{
		mdc_encoder_set_packet(ps->mdc,(unsigned char)0x22b,0x00,p->UnitID);
	}
	else if (p->type[0] == 'U') // UnKill a unit W9CR
	{
		mdc_encoder_set_packet(ps->mdc,0x2b,0x0c,p->UnitID);
	}
	else
	{
		ast_log(LOG_ERROR, "Dont know MDC encode type '%s'\n", p->type);
		ast_free(ps);
		return NULL;	
	}
	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR)) {
		ast_log(LOG_ERROR, "Unable to set '%s' to signed linear format (write)\n", chan->name);
		ast_free(ps);
		return NULL;
	}
	return ps;
}

static int mdcgen_generator(struct ast_channel *chan, void *data, int len, int samples)
{
	struct mdcgen_pvt *ps = data;
	short s,*sp;
	int i,n;

	if (!samples) return 1;
	if (samples > sizeof(ps->cbuf)) return -1;
	if (samples < 0) samples = 160;
	n = mdc_encoder_get_samples(ps->mdc,ps->cbuf,samples);
	if (n < 1) return 1;
	sp = (short *)(ps->buf + AST_FRIENDLY_OFFSET);
	for(i = 0; i < n; i++)
	{
		s = ((short)ps->cbuf[i]) - 128;
		*sp++ = s * 81;
	}
	ps->f.frametype = AST_FRAME_VOICE;
	ps->f.subclass = AST_FORMAT_SLINEAR;
	ps->f.datalen = n * 2;
	ps->f.samples = n;
	ps->f.offset = AST_FRIENDLY_OFFSET;
	AST_FRAME_DATA(ps->f) = ps->buf + AST_FRIENDLY_OFFSET;
	ps->f.delivery.tv_sec = 0;
	ps->f.delivery.tv_usec = 0;
	ast_write(chan, &ps->f);
	return 0;
}
	
static struct ast_generator mdcgen = {
	alloc: mdcgen_alloc,
	release: mdcgen_release,
	generate: mdcgen_generator,
};

static int mdc1200gen_start(struct ast_channel *chan, char *type, short UnitID, short destID, short subcode)
{
	struct mdcparams p;

	memset(&p,0,sizeof(p));
	strlcpy(p.type, type, sizeof(p.type));
	p.UnitID = UnitID;
	p.DestID = destID;
	p.subcode = subcode;
	if (ast_activate_generator(chan, &mdcgen, &p)) {
		return -1;
	}
	return 0;
}

static int mdc1200gen(struct ast_channel *chan, char *type, short UnitID, short destID, short subcode)
{

int	res;
struct ast_frame *f;

	res = mdc1200gen_start(chan,type,UnitID,destID,subcode);
	if (res) return res;

	while (chan->generatordata)
	{
		if (ast_check_hangup(chan)) return -1;
		res = ast_waitfor(chan, 100);
		if (res <= 0) return -1;
		f = ast_read(chan);
		if (f)
			ast_frfree(f);
		else
			return -1;
	}
	return 0;
}

static int mdcgen_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u;
	char *tmp;
	int res;
	short unitid,destid,subcode;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(type);
		AST_APP_ARG(unit);
		AST_APP_ARG(destid);
		AST_APP_ARG(subcode);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "MDC1200 requires an arguments!!\n");
		return -1;
	}

	tmp = ast_strdup(data);
	AST_STANDARD_APP_ARGS(args, tmp);

	if ((!args.type) || (!args.unit))
	{
		ast_log(LOG_WARNING, "MDC1200 requires type and unitid to be specified!!\n");
		ast_free(tmp);
		return -1;
	}

	destid = 0;
	subcode = 0;
	if (args.type[0] == 'C') 
	{
		if ((!args.destid) || (!args.subcode))
		{
			ast_log(LOG_WARNING, "MDC1200(C) requires destid and subtype to be specified!!\n");
			ast_free(tmp);
			return -1;
		}
		destid = (short) strtol(args.destid,NULL,16);
		subcode = (short) strtol(args.subcode,NULL,16);
	}
	u = ast_module_user_add(chan);
	unitid = (short) strtol(args.unit,NULL,16) & 0xffff;
	res = mdc1200gen(chan, args.type, unitid, destid, subcode);
	free(tmp);
	ast_module_user_remove(u);
	return res;
}


#endif

#ifdef	OLD_ASTERISK
int unload_module()
#else
static int unload_module(void)
#endif
{
	int i, res;

#ifdef	OLD_ASTERISK
	STANDARD_HANGUP_LOCALUSERS;
#endif

	daq_uninit();

	for(i = 0; i < nrpts; i++) {
		if (!strcmp(rpt_vars[i].name,rpt_vars[i].p.nodes)) continue;
                ast_mutex_destroy(&rpt_vars[i].lock);
                ast_mutex_destroy(&rpt_vars[i].remlock);
	}
	res = ast_unregister_application(app);
#ifdef	_MDC_ENCODE_H_
	res |= ast_unregister_application(app);
#endif

#ifdef	NEW_ASTERISK
	ast_cli_unregister_multiple(rpt_cli,sizeof(rpt_cli) / 
		sizeof(struct ast_cli_entry));
#else
	/* Unregister cli extensions */
	ast_cli_unregister(&cli_debug);
	ast_cli_unregister(&cli_dump);
	ast_cli_unregister(&cli_stats);
	ast_cli_unregister(&cli_lstats);
	ast_cli_unregister(&cli_nodes);
	ast_cli_unregister(&cli_xnode);
	ast_cli_unregister(&cli_local_nodes);
	ast_cli_unregister(&cli_reload);
	ast_cli_unregister(&cli_restart);
	ast_cli_unregister(&cli_playback);
	ast_cli_unregister(&cli_fun);
	ast_cli_unregister(&cli_fun1);
	ast_cli_unregister(&cli_setvar);
	ast_cli_unregister(&cli_showvars);
	ast_cli_unregister(&cli_page);
	ast_cli_unregister(&cli_lookup);
	res |= ast_cli_unregister(&cli_cmd);
#endif
#ifndef OLD_ASTERISK
	res |= ast_manager_unregister("RptLocalNodes");
	res |= ast_manager_unregister("RptStatus");
#endif
	close(nullfd);
	return res;
}

#ifdef	OLD_ASTERISK
int load_module()
#else
static int load_module(void)
#endif
{

	int res;

#ifndef	HAVE_DAHDI
	int fd;
	struct dahdi_versioninfo zv;
	char *cp;

	fd = open("/dev/zap/ctl",O_RDWR);
	if (fd == -1)
	{
		ast_log(LOG_ERROR,"Cannot open Zap device for probe\n");
		return -1;
	}
	if (ioctl(fd,DAHDI_GETVERSION,&zv) == -1)
	{
		ast_log(LOG_ERROR,"Cannot get ZAPTEL version info\n");
		close(fd);
		return -1;
	}
	close(fd);
	cp = strstr(zv.version,"RPT_");
	if ((!cp) || (*(cp + 4) < REQUIRED_ZAPTEL_VERSION))
	{
		ast_log(LOG_ERROR,"Zaptel version %s must at least level RPT_%c to operate\n",
			zv.version,REQUIRED_ZAPTEL_VERSION);
		return -1;
	}
#endif
	nullfd = open("/dev/null",O_RDWR);
	if (nullfd == -1)
	{
		ast_log(LOG_ERROR,"Can not open /dev/null\n");
		return -1;
	}
	ast_pthread_create(&rpt_master_thread,NULL,rpt_master,NULL);

#ifdef	NEW_ASTERISK
	ast_cli_register_multiple(rpt_cli,sizeof(rpt_cli) / 
		sizeof(struct ast_cli_entry));
	res = 0;
#else
	/* Register cli extensions */
	ast_cli_register(&cli_debug);
	ast_cli_register(&cli_dump);
	ast_cli_register(&cli_stats);
	ast_cli_register(&cli_lstats);
	ast_cli_register(&cli_nodes);
	ast_cli_register(&cli_xnode);
	ast_cli_register(&cli_local_nodes);
	ast_cli_register(&cli_reload);
	ast_cli_register(&cli_restart);
	ast_cli_register(&cli_playback);
	ast_cli_register(&cli_localplay);
	ast_cli_register(&cli_sendtext);
	ast_cli_register(&cli_sendall);
	ast_cli_register(&cli_fun);
	ast_cli_register(&cli_fun1);
	ast_cli_register(&cli_setvar);
	ast_cli_register(&cli_showvars);
	ast_cli_register(&cli_page);
	ast_cli_register(&cli_lookup);
	res = ast_cli_register(&cli_cmd);
#endif
#ifndef OLD_ASTERISK
	res |= ast_manager_register("RptLocalNodes", 0, manager_rpt_local_nodes, "List local node numbers");
	res |= ast_manager_register("RptStatus", 0, manager_rpt_status, "Return Rpt Status for CGI");
#endif
	res |= ast_register_application(app, rpt_exec, synopsis, descrip);

#ifdef	_MDC_ENCODE_H_
	res |= ast_register_application(mdc_app, mdcgen_exec, mdc_synopsis, mdc_descrip);
#endif

	return res;
}

#ifdef	OLD_ASTERISK
char *description()
{
	return tdesc;
}
int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
#endif

#ifdef	OLD_ASTERISK
int reload()
#else
static int reload(void)
#endif
{
int	i,n;
struct ast_config *cfg;
char	*val,*this;

#ifdef	NEW_ASTERISK
	cfg = ast_config_load("rpt.conf",config_flags);
#else
	cfg = ast_config_load("rpt.conf");
#endif
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to open radio repeater configuration rpt.conf.  Radio Repeater disabled.\n");
		pthread_exit(NULL);
	}

	ast_mutex_lock(&rpt_master_lock);
	for(n = 0; n < nrpts; n++) rpt_vars[n].reload1 = 0;
	this = NULL;
	while((this = ast_category_browse(cfg,this)) != NULL)
	{
		for(i = 0 ; i < strlen(this) ; i++){
			if((this[i] < '0') || (this[i] > '9'))
				break;
		}
		if (i != strlen(this)) continue; /* Not a node defn */
		for(n = 0; n < nrpts; n++) 
		{
			if (!strcmp(this,rpt_vars[n].name))
			{
				rpt_vars[n].reload1 = 1;
				break;
			}
		}
		if (n >= nrpts) /* no such node, yet */
		{
			/* find an empty hole or the next one */
			for(n = 0; n < nrpts; n++) if (rpt_vars[n].deleted) break;
			if (n >= MAXRPTS)
			{
				ast_log(LOG_ERROR,"Attempting to add repeater node %s would exceed max. number of repeaters (%d)\n",this,MAXRPTS);
				continue;
			}
			memset(&rpt_vars[n],0,sizeof(rpt_vars[n]));
			rpt_vars[n].name = ast_strdup(this);
			val = (char *) ast_variable_retrieve(cfg,this,"rxchannel");
			if (val) rpt_vars[n].rxchanname = ast_strdup(val);
			val = (char *) ast_variable_retrieve(cfg,this,"txchannel");
			if (val) rpt_vars[n].txchanname = ast_strdup(val);
			rpt_vars[n].remote = 0;
			rpt_vars[n].remoterig = "";
			rpt_vars[n].p.iospeed = B9600;
			rpt_vars[n].ready = 0;
			val = (char *) ast_variable_retrieve(cfg,this,"remote");
			if (val) 
			{
				rpt_vars[n].remoterig = ast_strdup(val);
				rpt_vars[n].remote = 1;
			}
			val = (char *) ast_variable_retrieve(cfg,this,"radiotype");
			if (val) rpt_vars[n].remoterig = ast_strdup(val);
			ast_mutex_init(&rpt_vars[n].lock);
			ast_mutex_init(&rpt_vars[n].remlock);
			ast_mutex_init(&rpt_vars[n].statpost_lock);
			rpt_vars[n].tele.next = &rpt_vars[n].tele;
			rpt_vars[n].tele.prev = &rpt_vars[n].tele;
			rpt_vars[n].rpt_thread = AST_PTHREADT_NULL;
			rpt_vars[n].tailmessagen = 0;
#ifdef	_MDC_DECODE_H_
			rpt_vars[n].mdc = mdc_decoder_new(8000);
#endif
			rpt_vars[n].reload1 = 1;
			if (n >= nrpts) nrpts = n + 1;
		}
	}
	for(n = 0; n < nrpts; n++)
	{
		if (rpt_vars[n].reload1) continue;
		if (rpt_vars[n].rxchannel) ast_softhangup(rpt_vars[n].rxchannel,AST_SOFTHANGUP_DEV);
		rpt_vars[n].deleted = 1;
	}
	for(n = 0; n < nrpts; n++) if (!rpt_vars[n].deleted) rpt_vars[n].reload = 1;
	ast_mutex_unlock(&rpt_master_lock);
	return(0);
}

/*
 * Code to handle the "old" way of registering module hooks in asterisk
*/

#ifndef	OLD_ASTERISK
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Radio Repeater/Remote Base Application",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
#endif
