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
 * \note refactored and revamped to compile on newer Asterisk 2022 by Naveen Albert <asterisk@phreaknet.org>
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

#include "asterisk.h"

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
#include <termios.h>

#include <dahdi/user.h>
#include <dahdi/tonezone.h> /* use tone_zone_set_zone */

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
#include "asterisk/format.h"
#include "asterisk/format_compatibility.h"

/* Un-comment the following to include support decoding of MDC-1200 digital tone
   signalling protocol (using KA6SQG's GPL'ed implementation) */
#include "app_rpt/mdc_decode.c"

/* Un-comment the following to include support encoding of MDC-1200 digital tone
   signalling protocol (using KA6SQG's GPL'ed implementation) */
#include "app_rpt/mdc_encode.c"

/* Un-comment the following to include support for notch filters in the
   rx audio stream (using Tony Fisher's mknotch (mkfilter) implementation) */
/* #include "rpt_notch.c" */

AST_MUTEX_DEFINE_STATIC(nodeloglock);

#include "app_rpt/rpt_dsp.h" /* this must come before app_rpt.h */
#include "app_rpt/app_rpt.h"
#include "app_rpt/rpt_utils.h"
#include "app_rpt/rpt_lock.h"
#include "app_rpt/rpt_channels.h"
#include "app_rpt/rpt_serial.h"
#include "app_rpt/rpt_uchameleon.h"
#include "app_rpt/rpt_daq.h"
#include "app_rpt/rpt_remotedata.h"
#include "app_rpt/rpt_mdc.h"
#include "app_rpt/rpt_telemetry.h"
#include "app_rpt/rpt_stream.h"
#include "app_rpt/rpt_vox.h"
#include "app_rpt/rpt_cli.h"
#include "app_rpt/rpt_manager.h"

struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS };
int debug = 0;  /* Set this >0 for extra debug output */
static int nrpts = 0;
static struct rpt rpt_vars[MAXRPTS];
static struct nodelog nodelog;
static time_t	starttime = 0;
static  pthread_t rpt_master_thread;

AST_MUTEX_DEFINE_STATIC(rpt_master_lock);

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

static char remdtmfstr[] = "0123456789*#ABCD";

int max_chan_stat [] = {22000,1000,22000,100,22000,2000,22000};

int nullfd = -1;

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
};

/*
	return via error priority
*/
static int priority_jump(struct rpt *myrpt, struct ast_channel *chan)
{
	int res=0;

	// if (ast_test_flag(&flags,OPT_JUMP) && ast_goto_if_exists(chan, ast_channel_context(chan), ast_channel_exten(chan), ast_channel_priority(chan) + 101) == 0){
	if (ast_goto_if_exists(chan, ast_channel_context(chan), ast_channel_exten(chan), ast_channel_priority(chan) + 101) == 0){
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

static int altlink(struct rpt *myrpt,struct rpt_link *mylink)
{
	if (!myrpt) return(0);
	if (!mylink) return(0);
	if (!mylink->chan) return(0);
	if ((myrpt->p.duplex == 3) && mylink->phonemode && myrpt->keyed) return(0);
	/* if doesnt qual as a foreign link */
	if ((mylink->name[0] > '0') && (mylink->name[0] <= '9') &&
	    (!mylink->phonemode) &&
	    strncasecmp(ast_channel_name(mylink->chan),"echolink",8) &&
		strncasecmp(ast_channel_name(mylink->chan),"tlb",3)) return(0);
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
	    strncasecmp(ast_channel_name(mylink->chan),"echolink",8) &&
		strncasecmp(ast_channel_name(mylink->chan),"tlb",3)) return(1);
	if (mylink->linkmode < 2) return(0);
	if (mylink->linkmode == 0x7fffffff) return(1);
	if (mylink->linkmode < 0x7ffffffe) return(1);
	if (myrpt->telemmode > 1) return(1);
	return(0);
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
	wf.subclass.integer = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.data.ptr = str;
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
					ast_verb(3, "Event on node %s doing macro %s for condition %s\n",
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
				ast_verb(3, "Event on node %s doing rpt command %s for condition %s\n",
					myrpt->name,cmd,v->value);
			rpt_mutex_lock(&myrpt->lock);
			if (myrpt->cmdAction.state == CMD_STATE_IDLE)
			{
				myrpt->cmdAction.state = CMD_STATE_BUSY;
				myrpt->cmdAction.functionNumber = thisAction;
				myrpt->cmdAction.param[0] = 0;
				if (argc > 1)
					ast_copy_string(myrpt->cmdAction.param, argv[1], MAXDTMF-1);
				myrpt->cmdAction.digits[0] = 0;
				if (argc > 2)
					ast_copy_string(myrpt->cmdAction.digits, argv[2], MAXDTMF-1);
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
				ast_verb(3, "Event on node %s doing shell command %s for condition %s\n",
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
			ast_free(cp);
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
	AST_LIST_TRAVERSE (ast_channel_varshead(myrpt->rxchannel), newvariable, entries) {
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
	ast_free(a);
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
#define GLOBAL_USERAGENT "asterisk-libcurl-agent/1.0"
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
	/*! \todo this URL should be configurable in rpt.conf. Anything AllStarLink-specific does not belong in the source code. */
	sprintf(str,"%s?node=%s&time=%u&seqno=%u",myrpt->p.statpost_url,
		myrpt->name,(unsigned int) now,seq);
	if (pairs) sprintf(str + strlen(str),"&%s",pairs);
	if (!(pid = fork()))
	{
		curl = curl_easy_init();
		if(curl) {
			curl_easy_setopt(curl, CURLOPT_URL, str);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, GLOBAL_USERAGENT);
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
		ast_verb(3, "app_rpt node %s starting output stream %s\n",
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
		ast_verb(3, "%s config for repeater %s\n",
			(init) ? "Loading initial" : "Re-Loading",rpt_vars[n].name);
	ast_mutex_lock(&rpt_vars[n].lock);
	if (rpt_vars[n].cfg) ast_config_destroy(rpt_vars[n].cfg);
	cfg = ast_config_load("rpt.conf",config_flags);
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
struct	ast_tm localtm;

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
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
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
		    res = ast_streamfile(mychannel, "rpt/connected", ast_channel_language(mychannel));
		if (!res)
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
		ast_stopstream(mychannel);
		res = ast_streamfile(mychannel, "digits/2", ast_channel_language(mychannel));
		if (!res)
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
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
	    	res = ast_say_time(mychannel, t1, "", ast_channel_language(mychannel));
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
		res = ast_say_number(mychannel, vmajor, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (saycharstr(mychannel,".") == -1) return;
		if(!res) /* Say "Y" */
			ast_say_number(mychannel, vminor, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res){
			res = ast_waitstream(mychannel, "");
			ast_stopstream(mychannel);
		}
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
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
		res = ast_say_number(mychannel, i, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (sayfile(mychannel,"degrees") == -1) return;
		res = ast_say_number(mychannel, j, "", ast_channel_language(mychannel), (char *) NULL);
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
		res = ast_say_number(mychannel, i, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (sayfile(mychannel,"degrees") == -1) return;
		res = ast_say_number(mychannel, j, "", ast_channel_language(mychannel), (char *) NULL);
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
		res = ast_say_number(mychannel, i, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (saycharstr(mychannel,".") == -1) return;
		res = ast_say_number(mychannel, j, "", ast_channel_language(mychannel), (char *) NULL);
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
				ast_say_digits(mychannel, atoi(myparm), "", ast_channel_language(mychannel));
			} else if(!strcmp(tmp[i], "NODE")) {
				ast_say_digits(mychannel, atoi(strs[1]), "", ast_channel_language(mychannel));
			} else {
				dres = ast_streamfile(mychannel, tmp[i], ast_channel_language(mychannel));
				if(!dres) {
					dres = ast_waitstream(mychannel, "");
				} else {
					ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n", tmp[i], ast_channel_name(mychannel));
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
struct ast_tm localtm;
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
struct ast_format_cap *cap;

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

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		/*! \todo we should probably bail out or exit here or something... */
	}

	ast_format_cap_append(cap, ast_format_slin, 0);

	/* allocate a pseudo-channel thru asterisk */
	mychannel = ast_request("DAHDI",cap,NULL,NULL,"pseudo",NULL);
	ao2_ref(cap, -1);

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
	if (ioctl(ast_channel_fd(mychannel, 0),DAHDI_SETCONF,&ci) == -1)
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
			res = ast_streamfile(mychannel, myrpt->p.tailmessages[myrpt->tailmessagen], ast_channel_language(mychannel));
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
			res = ast_streamfile(mychannel, "rpt/callproceeding", ast_channel_language(mychannel));
		}
		break;
	    case TERM:
		/* wait a little bit longer */
		if (!wait_interval(myrpt, DLY_CALLTERM, mychannel))
			res = telem_lookup(myrpt, mychannel, myrpt->name, "patchdown");
		if(res < 0){ /* Then default message */
			res = ast_streamfile(mychannel, "rpt/callterminated", ast_channel_language(mychannel));
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
			res = ast_streamfile(mychannel, "rpt/macro_notfound", ast_channel_language(mychannel));
		break;
	    case MACRO_BUSY:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/macro_busy", ast_channel_language(mychannel));
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
				while(ast_channel_generatordata(myrpt->txchannel))
				{
					if(ast_safe_sleep(myrpt->txchannel, 50))
					{
						res = -1;
						break;
					}
				}
				ast_free((char *)mytele->submode.p);
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
			while(ast_channel_generatordata(myrpt->txchannel))
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
				ast_log(LOG_WARNING, "telem_lookup:ranger failed on %s\n", ast_channel_name(mychannel));
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
			 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", ast_channel_name(mychannel));
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
				ast_log(LOG_WARNING, "telem_lookup:remotexx failed on %s\n", ast_channel_name(mychannel));


			/* if in remote cmd mode, indicate it */
			if (myrpt->cmdnode[0] && strcmp(myrpt->cmdnode,"aprstt"))
			{
				ast_safe_sleep(mychannel,200);
				res = telem_lookup(myrpt,mychannel, myrpt->name, "cmdmode");
				if(res)
				 	ast_log(LOG_WARNING, "telem_lookup:cmdmode failed on %s\n", ast_channel_name(mychannel));
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
			 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", ast_channel_name(mychannel));
		}
		if (hasremote && ((!myrpt->cmdnode[0]) || (!strcmp(myrpt->cmdnode,"aprstt"))))
		{
			/* set for all to hear */
			ci.chan = 0;
			ci.confno = myrpt->conf;
			ci.confmode = DAHDI_CONF_CONFANN;
			/* first put the channel on the conference in announce mode */
			if (ioctl(ast_channel_fd(mychannel, 0),DAHDI_SETCONF,&ci) == -1)
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
				 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", ast_channel_name(mychannel));
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
			if (ioctl(ast_channel_fd(mychannel, 0),DAHDI_SETCONF,&ci) == -1)
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
			ast_say_character_str(mychannel,mystr,NULL,ast_channel_language(mychannel));
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
			 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", ast_channel_name(mychannel));
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
			"rpt/remote_disc" : "rpt/remote_busy"), ast_channel_language(mychannel));
		break;
	    case REMALREADY:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/remote_already", ast_channel_language(mychannel));
		break;
	    case REMNOTFOUND:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/remote_notfound", ast_channel_language(mychannel));
		break;
	    case REMGO:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/remote_go", ast_channel_language(mychannel));
		break;
	    case CONNECTED:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM,  mychannel) == -1) break;
		res = saynode(myrpt,mychannel,mytele->mylink.name);
		if (!res)
		    res = ast_streamfile(mychannel, "rpt/connected", ast_channel_language(mychannel));
		if (!res)
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
		ast_stopstream(mychannel);
		res = ast_streamfile(mychannel, "digits/2", ast_channel_language(mychannel));
		if (!res)
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
		ast_stopstream(mychannel);
		res = saynode(myrpt,mychannel,myrpt->name);
		imdone = 1;
		break;
	    case CONNFAIL:
		res = saynode(myrpt,mychannel,mytele->mylink.name);
		if (!res)
		    res = ast_streamfile(mychannel, "rpt/connection_failed", ast_channel_language(mychannel));
		break;
	    case MEMNOTFOUND:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/memory_notfound", ast_channel_language(mychannel));
		break;
	    case PLAYBACK:
            case LOCALPLAY:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		res = ast_streamfile(mychannel, mytele->param, ast_channel_language(mychannel));
		if (!res)
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
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
				if (ioctl(ast_channel_fd(myrpt->zaptxchannel, 0),DAHDI_FLUSH,&i) == -1)
				{
					myrpt->remsetting = 0;
					ast_mutex_unlock(&myrpt->remlock);
					ast_log(LOG_ERROR,"Cant flush events");
					res = -1;
					break;
				}
				if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_GET_PARAMS,&par) == -1)
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
			res = ast_streamfile(mychannel, "rpt/invalid-freq", ast_channel_language(mychannel));
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
				ast_log(LOG_WARNING, "Cannot alloc memory on %s\n", ast_channel_name(mychannel));
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
			res = ast_streamfile(mychannel, "rpt/autopatch_on", ast_channel_language(mychannel));
			if (!res)
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
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
			res = ast_streamfile(mychannel, s, ast_channel_language(mychannel));
			if (!res)
				res = ast_waitstream(mychannel, "");
			else
				ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
			ast_stopstream(mychannel);
			l = l->next;
		}
		if (!hastx)
		{
			res = ast_streamfile(mychannel, "rpt/repeat_only", ast_channel_language(mychannel));
			if (!res)
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
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
			res = ast_streamfile(mychannel, "rpt/autopatch_on", ast_channel_language(mychannel));
			if (!res)
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
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
			res = ast_streamfile(mychannel, s, ast_channel_language(mychannel));
			if (!res)
				res = ast_waitstream(mychannel, "");
			else
				ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
			ast_stopstream(mychannel);
		}
		if (!hastx)
		{
			res = ast_streamfile(mychannel, "rpt/repeat_only", ast_channel_language(mychannel));
			if (!res)
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
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
		res = ast_streamfile(mychannel, "rpt/unauthtx", ast_channel_language(mychannel));
		if (!res)
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
		ast_stopstream(mychannel);
		imdone = 1;
		break;

	    case PARROT: /* Repeat stuff */

		sprintf(mystr,PARROTFILE,myrpt->name,mytele->parrot);
		if (ast_fileexists(mystr,NULL,ast_channel_language(mychannel)) <= 0)
		{
			imdone = 1;
			myrpt->parrotstate = 0;
			break;
		}
		if (wait_interval(myrpt, DLY_PARROT, mychannel) == -1) break;
		sprintf(mystr,PARROTFILE,myrpt->name,mytele->parrot);
		res = ast_streamfile(mychannel, mystr, ast_channel_language(mychannel));
		if (!res)
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
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
		   res = ast_streamfile(mychannel, "rpt/timeout", ast_channel_language(mychannel));
		break;

	    case TIMEOUT_WARNING:
		time(&t);
		res = saynode(myrpt,mychannel,myrpt->name);
		if (!res)
		   res = ast_streamfile(mychannel, "rpt/timeout-warning", ast_channel_language(mychannel));
		if (!res)
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
		ast_stopstream(mychannel);
		if(!res) /* Say number of seconds */
			ast_say_number(mychannel, myrpt->p.remotetimeout -
			    (t - myrpt->last_activity_time),
				"", ast_channel_language(mychannel), (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		res = ast_streamfile(mychannel, "queue-seconds", ast_channel_language(mychannel));
		break;

	    case ACT_TIMEOUT_WARNING:
		time(&t);
		res = saynode(myrpt,mychannel,myrpt->name);
		if (!res)
		    res = ast_streamfile(mychannel, "rpt/act-timeout-warning", ast_channel_language(mychannel));
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if(!res) /* Say number of seconds */
			ast_say_number(mychannel, myrpt->p.remoteinacttimeout -
			    (t - myrpt->last_activity_time),
				"", ast_channel_language(mychannel), (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (!res)
			res = ast_streamfile(mychannel, "queue-seconds", ast_channel_language(mychannel));
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
	    	res = ast_say_time(mychannel, t1, "", ast_channel_language(mychannel));
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
			ast_say_number(mychannel, vmajor, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (saycharstr(mychannel,".") == -1)
		{
			imdone = 1;
			break;
		}
		if(!res) /* Say "Y" */
			ast_say_number(mychannel, vminor, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res){
			res = ast_waitstream(mychannel, "");
			ast_stopstream(mychannel);
		}
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
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
		res = ast_say_number(mychannel, i, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (sayfile(mychannel,"degrees") == -1) break;
		res = ast_say_number(mychannel, j, "", ast_channel_language(mychannel), (char *) NULL);
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
		res = ast_say_number(mychannel, i, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (sayfile(mychannel,"degrees") == -1) break;
		res = ast_say_number(mychannel, j, "", ast_channel_language(mychannel), (char *) NULL);
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
		res = ast_say_number(mychannel, i, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (saycharstr(mychannel,".") == -1) break;
		res = ast_say_number(mychannel, j, "", ast_channel_language(mychannel), (char *) NULL);
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
					ast_say_digits(mychannel, atoi(myparm), "", ast_channel_language(mychannel));
				} else if(!strcmp(tmp[i], "NODE")) {
					ast_say_digits(mychannel, atoi(myrpt->name), "", ast_channel_language(mychannel));
				} else {
					dres = ast_streamfile(mychannel, tmp[i], ast_channel_language(mychannel));
					if(!dres) {
						dres = ast_waitstream(mychannel, "");
					} else {
						ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n", tmp[i], ast_channel_name(mychannel));
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
	        while(ast_channel_generatordata(mychannel) && (myrpt->stopgen <= 0)) {
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
			ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
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

void rpt_telemetry(struct rpt *myrpt,int mode, void *data)
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
		    mylink->chan && strncasecmp(ast_channel_name(mylink->chan),"echolink",8) &&
			strncasecmp(ast_channel_name(mylink->chan),"tlb",3)) return;
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

void *rpt_call(void *this)
{
struct dahdi_confinfo ci;  /* conference info */
struct	rpt *myrpt = (struct rpt *)this;
int	res;
int stopped,congstarted,dialtimer,lastcidx,aborted,sentpatchconnect;
struct ast_channel *mychannel,*genchannel;
struct ast_format_cap *cap;

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		/*! \todo should probably bail out or something */
	}
	ast_format_cap_append(cap, ast_format_slin, 0);

	myrpt->mydtmf = 0;
	/* allocate a pseudo-channel thru asterisk */
	mychannel = ast_request("DAHDI",cap,NULL,NULL,"pseudo",NULL);


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
	if (ioctl(ast_channel_fd(mychannel, 0),DAHDI_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		ast_hangup(mychannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	/* allocate a pseudo-channel thru asterisk */
	genchannel = ast_request("DAHDI",cap,NULL,NULL,"pseudo",NULL);
	ao2_ref(cap, -1);
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
	if (ioctl(ast_channel_fd(genchannel, 0),DAHDI_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	if (myrpt->p.tonezone && (tone_zone_set_zone(ast_channel_fd(mychannel, 0),myrpt->p.tonezone) == -1))
	{
		ast_log(LOG_WARNING, "Unable to set tone zone %s\n",myrpt->p.tonezone);
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	if (myrpt->p.tonezone && (tone_zone_set_zone(ast_channel_fd(genchannel, 0),myrpt->p.tonezone) == -1))
	{
		ast_log(LOG_WARNING, "Unable to set tone zone %s\n",myrpt->p.tonezone);
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	/* start dialtone if patchquiet is 0. Special patch modes don't send dial tone */
	if ((!myrpt->patchquiet) && (!myrpt->patchexten[0])
		&& (tone_zone_play_tone(ast_channel_fd(genchannel, 0),DAHDI_TONE_DIALTONE) < 0))
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
			tone_zone_play_tone(ast_channel_fd(genchannel, 0),-1);
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
				tone_zone_play_tone(ast_channel_fd(genchannel, 0),DAHDI_TONE_CONGESTION);
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
	tone_zone_play_tone(ast_channel_fd(genchannel, 0),-1);
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
			ast_set_callerid(mychannel, loc, name, NULL);
			ast_free(instr);
		}
	}

	ast_channel_context_set(mychannel, myrpt->patchcontext);
	ast_channel_exten_set(mychannel, myrpt->exten);

	if (myrpt->p.acctcode)
		ast_channel_accountcode_set(mychannel, myrpt->p.acctcode);
	ast_channel_priority_set(mychannel, 1);
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
	if (ast_channel_pbx(mychannel))
	{
		/* first put the channel on the conference in announce mode */
		if (ioctl(ast_channel_fd(myrpt->pchannel, 0),DAHDI_SETCONF,&ci) == -1)
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
		if (ioctl(ast_channel_fd(mychannel, 0),DAHDI_CHANNO,&res) == -1)
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
		if (ioctl(ast_channel_fd(myrpt->voxchannel, 0),DAHDI_SETCONF,&ci) == -1)
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
		if ((!ast_channel_pbx(mychannel)) && (myrpt->callmode != 4))
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
				tone_zone_play_tone(ast_channel_fd(genchannel, 0),DAHDI_TONE_CONGESTION);
				rpt_mutex_lock(&myrpt->lock);
			}
		}
		if (ast_channel_is_bridged(mychannel) && ast_channel_state(mychannel) == AST_STATE_UP)
		if ((!sentpatchconnect) &&
			myrpt->p.patchconnect &&
			ast_channel_is_bridged(mychannel) && (ast_channel_state(mychannel) == AST_STATE_UP))
		{
			sentpatchconnect = 1;
			rpt_telemetry(myrpt,PLAYBACK,myrpt->p.patchconnect);
		}
		if (myrpt->mydtmf)
		{
			struct ast_frame wf = {AST_FRAME_DTMF, } ;

			wf.subclass.integer = myrpt->mydtmf;
			if (ast_channel_is_bridged(mychannel) && ast_channel_state(mychannel) == AST_STATE_UP)
			{
				rpt_mutex_unlock(&myrpt->lock);
				ast_queue_frame(mychannel,&wf);
				ast_senddigit(genchannel,myrpt->mydtmf,0);
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
	tone_zone_play_tone(ast_channel_fd(genchannel, 0),-1);
	if (ast_channel_pbx(mychannel)) ast_softhangup(mychannel,AST_SOFTHANGUP_DEV);
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
	if (ioctl(ast_channel_fd(myrpt->pchannel, 0),DAHDI_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
	}
	pthread_exit(NULL);
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
	wf.subclass.integer = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	wf.src = "send_link_keyquery";
	l = myrpt->links.next;
	/* give it to everyone */
	while(l != &myrpt->links)
	{
		wf.data.ptr = str;
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
	wf.subclass.integer = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	wf.src = "send_tele_link";
	l = myrpt->links.next;
	/* give it to everyone */
	while(l != &myrpt->links)
	{
		wf.data.ptr = str;
		if (l->chan && (l->mode == 1)) rpt_qwrite(l,&wf);
		l = l->next;
	}
	rpt_telemetry(myrpt,VARCMD,cmd);
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
	char tmp[300], deststr[325] = "",modechange = 0;
	char sx[320],*sy;
	struct rpt_link *l;
	int reconnects = 0;
	int i,n;
	int voterlink=0;
	struct dahdi_confinfo ci;  /* conference info */
	struct ast_format_cap *cap;

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
		if ((!strncasecmp(ast_channel_name(l->chan),"echolink",8)) ||
			(!strncasecmp(ast_channel_name(l->chan),"tlb",3)))
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
        	ast_copy_string(deststr, s1, sizeof(deststr)-1);
	else
	        snprintf(deststr, sizeof(deststr), "IAX2/%s", s1);
	tele = strchr(deststr, '/');
	if (!tele){
		ast_log(LOG_WARNING,"link3:Dial number (%s) must be in format tech/number\n",deststr);
		ast_free(l);
		return -1;
	}
	*tele++ = 0;

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		return -1;
	}

	ast_format_cap_append(cap, ast_format_slin, 0);

	if (!strncasecmp(deststr,"echolink",8))
	{
		char tel1[100];

		strncpy(tel1,tele,sizeof(tel1) - 1);
		cp = strchr(tel1,'/');
		if (cp) cp++; else cp = tel1;
		strcpy(cp,node + 1);
		l->chan = ast_request(deststr, cap, NULL, NULL, tel1, NULL);
	}
	else
	{
		l->chan = ast_request(deststr, cap, NULL, NULL, tele, NULL);
		if (!(l->chan))
		{
			usleep(150000);
			l->chan = ast_request(deststr, cap, NULL, NULL, tele, NULL);
		}
	}
	if (l->chan){
		ast_set_read_format(l->chan, ast_format_slin);
		ast_set_write_format(l->chan, ast_format_slin);
#ifdef	AST_CDR_FLAG_POST_DISABLED
		if (l->chan->cdr)
			ast_set_flag(l->chan->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
		ast_channel_appl_set(l->chan, "Apprpt");
		ast_channel_data_set(l->chan, "(Remote Rx)");
		if (debug > 3)
			ast_log(LOG_NOTICE, "rpt (remote) initiating call to %s/%s on %s\n",
		deststr, tele, ast_channel_name(l->chan));
		ast_set_callerid(l->chan, myrpt->name, NULL, NULL);
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
		ao2_ref(cap, -1);
		return -1;
	}
	/* allocate a pseudo-channel thru asterisk */
	l->pchan = ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
	ao2_ref(cap, -1);
	if (!l->pchan){
		ast_log(LOG_WARNING,"rpt connect: Sorry unable to obtain pseudo channel\n");
		ast_hangup(l->chan);
		ast_free(l);
		return -1;
	}
	ast_set_read_format(l->pchan, ast_format_slin);
	ast_set_write_format(l->pchan, ast_format_slin);
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
	if (ioctl(ast_channel_fd(l->pchan, 0), DAHDI_SETCONF, &ci) == -1)
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

int function_ilink(struct rpt *myrpt, char *param, char *digits, int command_source, struct rpt_link *mylink)
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
				ast_copy_string(myrpt->lastlinknode,digitbuf,MAXNODESTR - 1);
				l->retries = l->max_retries + 1;
				l->disced = 1;
				l->hasconnected = 1;
				rpt_mutex_unlock(&myrpt->lock);
				memset(&wf,0,sizeof(wf));
				wf.frametype = AST_FRAME_TEXT;
				wf.datalen = strlen(discstr) + 1;
				wf.data.ptr = discstr;
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
				strncasecmp(ast_channel_name(mylink->chan),"echolink",8) &&
				strncasecmp(ast_channel_name(mylink->chan),"tlb",3))
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
			ast_copy_string(myrpt->cmdnode, digitbuf, sizeof(myrpt->cmdnode) - 1);
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
                                wf.data.ptr = discstr;
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
	                                wf.data.ptr = discstr;
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

int function_autopatchup(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
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
		ast_copy_string(myrpt->patchcontext, myrpt->p.ourcontext, MAXPATCHCONTEXT-1);
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

int function_autopatchdn(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
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

int function_status(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
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
int function_macro(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
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

int function_playback(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{

	if (myrpt->remote)
		return DC_ERROR;

	if(debug)
		printf("@@@@ playback param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);

	if (ast_fileexists(param,NULL,ast_channel_language(myrpt->rxchannel)) <= 0)
		return DC_ERROR;

	rpt_telem_select(myrpt,command_source,mylink);
	rpt_telemetry(myrpt,PLAYBACK,param);
	return DC_COMPLETE;
}



/*
 * *  Playback a recording locally
 * */

int function_localplay(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{

        if (myrpt->remote)
                return DC_ERROR;

        if(debug)
                printf("@@@@ localplay param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);

        if (ast_fileexists(param,NULL,ast_channel_language(myrpt->rxchannel)) <= 0)
                return DC_ERROR;

        rpt_telemetry(myrpt,LOCALPLAY,param);
        return DC_COMPLETE;
}



/*
* COP - Control operator
*/

int function_cop(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
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
			else if (!strncasecmp(ast_channel_name(mylink->chan),"echolink",8)) src = LINKMODE_ECHOLINK;
			else if (!strncasecmp(ast_channel_name(mylink->chan),"tlb",3)) src = LINKMODE_TLB;
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
			else if (!strncasecmp(ast_channel_name(mylink->chan),"echolink",8)) src = LINKMODE_ECHOLINK;
			else if (!strncasecmp(ast_channel_name(mylink->chan),"tlb",3)) src = LINKMODE_TLB;
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
			else if (!strncasecmp(ast_channel_name(mylink->chan),"echolink",8)) src = LINKMODE_ECHOLINK;
			else if (!strncasecmp(ast_channel_name(mylink->chan),"tlb",3)) src = LINKMODE_TLB;
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
			else if (!strncasecmp(ast_channel_name(mylink->chan),"echolink",8)) src = LINKMODE_ECHOLINK;
			else if (!strncasecmp(ast_channel_name(mylink->chan),"tlb",3)) src = LINKMODE_TLB;
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
			cp = ast_malloc(j + 100);
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
			if ((strncasecmp(ast_channel_name(myrpt->rxchannel),"zap/", 4) == 0) ||
			    (strncasecmp(ast_channel_name(myrpt->rxchannel),"dahdi/", 6) == 0))
			{
				struct dahdi_radio_param r;

				memset(&r,0,sizeof(struct dahdi_radio_param));
				r.radpar = DAHDI_RADPAR_IGNORECT;
				r.data = 0;
				ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_RADIO_SETPARAM,&r);
			}
			if ((strncasecmp(ast_channel_name(myrpt->rxchannel),"radio/", 6) == 0) ||
			    (strncasecmp(ast_channel_name(myrpt->rxchannel),"simpleusb/", 10) == 0))
			{
				ast_sendtext(myrpt->rxchannel,"RXCTCSS 1");

			}
			rpt_telem_select(myrpt,command_source,mylink);
			rpt_telemetry(myrpt, ARB_ALPHA, (void *) "RXPLENA");
			return DC_COMPLETE;
		case 57: /* RX CTCSS Disable */
			if ((strncasecmp(ast_channel_name(myrpt->rxchannel),"zap/", 4) == 0) ||
			    (strncasecmp(ast_channel_name(myrpt->rxchannel),"dahdi/", 6) == 0))
			{
				struct dahdi_radio_param r;

				memset(&r,0,sizeof(struct dahdi_radio_param));
				r.radpar = DAHDI_RADPAR_IGNORECT;
				r.data = 1;
				ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_RADIO_SETPARAM,&r);
			}

			if ((strncasecmp(ast_channel_name(myrpt->rxchannel),"radio/", 6) == 0) ||
			    (strncasecmp(ast_channel_name(myrpt->rxchannel),"simpleusb/", 10) == 0))
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
			if ((strncasecmp(ast_channel_name(myrpt->rxchannel),"radio/", 6) == 0) &&
			    (strncasecmp(ast_channel_name(myrpt->rxchannel),"beagle/", 7) == 0) &&
			    (strncasecmp(ast_channel_name(myrpt->rxchannel),"simpleusb/", 10) == 0)) break;
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
			if ((strncasecmp(ast_channel_name(myrpt->rxchannel),"radio/", 6) == 0) &&
			    (strncasecmp(ast_channel_name(myrpt->rxchannel),"voter/", 6) == 0) &&
			    (strncasecmp(ast_channel_name(myrpt->rxchannel),"simpleusb/", 10) == 0)) break;
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
	if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_RADIO_SETPARAM,&r) == -1)
	{
		rbi_out_parallel(myrpt,data);
		return;
	}
	r.radpar = DAHDI_RADPAR_REMCOMMAND;
	memcpy(&r.data,data,5);
	if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_RADIO_SETPARAM,&r) == -1)
	{
		ast_log(LOG_WARNING,"Cannot send RBI command for channel %s\n",ast_channel_name(myrpt->zaprxchannel));
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
	if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_RADIO_GETPARAM,&prm) == -1) return -1;
	oldmode = prm.data;
	prm.radpar = DAHDI_RADPAR_UIODATA;
	if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_RADIO_GETPARAM,&prm) == -1) return -1;
	olddata = prm.data;
        prm.radpar = DAHDI_RADPAR_REMMODE;
        if ((asciiflag & 1) &&
	   strcmp(myrpt->remoterig, remote_rig_tm271) &&
	      strcmp(myrpt->remoterig, remote_rig_kenwood))
		  prm.data = DAHDI_RADPAR_REM_SERIAL_ASCII;
        else prm.data = DAHDI_RADPAR_REM_SERIAL;
	if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_RADIO_SETPARAM,&prm) == -1) return -1;
	if (asciiflag & 2)
	{
		i = DAHDI_ONHOOK;
		if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_HOOK,&i) == -1) return -1;
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
			if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),
				DAHDI_RADIO_SETPARAM,&prm) == -1) return -1;
			usleep(6666);
		}
        	prm.radpar = DAHDI_RADPAR_REMMODE;
	        if (asciiflag & 1)  prm.data = DAHDI_RADPAR_REM_SERIAL_ASCII;
	        else prm.data = DAHDI_RADPAR_REM_SERIAL;
		if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_RADIO_SETPARAM,&prm) == -1) return -1;
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
	if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_RADIO_SETPARAM,&prm) == -1) return -1;
        if (rxbuf)
        {
                *rxbuf = 0;
                memcpy(rxbuf,prm.buf,prm.index);
        }
	index = prm.index;
        prm.radpar = DAHDI_RADPAR_REMMODE;
        prm.data = DAHDI_RADPAR_REM_NONE;
	if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_RADIO_SETPARAM,&prm) == -1) return -1;
	if (asciiflag & 2)
	{
		i = DAHDI_OFFHOOK;
		if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_HOOK,&i) == -1) return -1;
	}
	prm.radpar = DAHDI_RADPAR_UIOMODE;
	prm.data = oldmode;
	if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_RADIO_SETPARAM,&prm) == -1) return -1;
	prm.radpar = DAHDI_RADPAR_UIODATA;
	prm.data = olddata;
	if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_RADIO_SETPARAM,&prm) == -1) return -1;
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

int setkenwood(struct rpt *myrpt)
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
	ast_copy_string(freq,decimals,strlen(freq)-1);
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

int set_tmd700(struct rpt *myrpt)
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
	ast_copy_string(freq,decimals,strlen(freq)-1);
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

int set_tm271(struct rpt *myrpt)
{
char rxstr[RAD_SERIAL_BUFLEN],txstr[RAD_SERIAL_BUFLEN],freq[20];
char mhz[MAXREMSTR],decimals[MAXREMSTR];
int  mysplit,step;

int offsets[] = {0,2,1};
int powers[] = {2,1,0};

	split_freq(mhz, decimals, myrpt->freq);
	strcpy(freq,"000000");
	ast_copy_string(freq,decimals,strlen(freq)-1);

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

int setrbi(struct rpt *myrpt)
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
	ast_copy_string(tmp, myrpt->freq, sizeof(tmp) - 1);
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
	ast_copy_string(tmp, myrpt->freq, sizeof(tmp) - 1);
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

int setxpmr(struct rpt *myrpt, int dotx)
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


int setrbi_check(struct rpt *myrpt)
{
char tmp[MAXREMSTR] = "",*s;
int	band,txpl;

	/* must be a remote system */
	if (!myrpt->remote) return(0);
	/* must have rbi hardware */
	if (strncmp(myrpt->remoterig,remote_rig_rbi,3)) return(0);
	ast_copy_string(tmp, myrpt->freq, sizeof(tmp) - 1);
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

int setrtx_check(struct rpt *myrpt)
{
char tmp[MAXREMSTR] = "",*s;
int	band,txpl,rxpl;

	/* must be a remote system */
	if (!myrpt->remote) return(0);
	/* must have rbi hardware */
	if (strncmp(myrpt->remoterig,remote_rig_rbi,3)) return(0);
	ast_copy_string(tmp, myrpt->freq, sizeof(tmp) - 1);
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

int simple_command_ft897(struct rpt *myrpt, char command)
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

int set_mode_ft897(struct rpt *myrpt, char newmode)
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



int set_ft897(struct rpt *myrpt)
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

int simple_command_ft100(struct rpt *myrpt, unsigned char command, unsigned char p1)
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

int set_mode_ft100(struct rpt *myrpt, char newmode)
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

int set_ft100(struct rpt *myrpt)
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



int set_ft950(struct rpt *myrpt)
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

int set_mode_ic706(struct rpt *myrpt, char newmode)
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

int set_ic706(struct rpt *myrpt)
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

int set_xcat(struct rpt *myrpt)
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
int setrem(struct rpt *myrpt)
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
			for(i = strlen(myfreq) - 1; i >= 0; i--)
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
* This is called periodically when in scan mode
*/
int service_scan(struct rpt *myrpt)
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
* Remote base function
*/
int function_remote(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
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
			ast_copy_string(savestr, myrpt->freq, sizeof(savestr) - 1);
			ast_copy_string(myrpt->freq, freq, sizeof(myrpt->freq) - 1);
			myrpt->offset = offset;
			myrpt->remmode = defmode;

			if (setrem(myrpt) == -1){
				myrpt->offset = offsave;
				myrpt->remmode = modesave;
				ast_copy_string(myrpt->freq, savestr, sizeof(myrpt->freq) - 1);
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
			ast_copy_string(savestr, myrpt->rxpl, sizeof(savestr) - 1);
			ast_copy_string(myrpt->rxpl, tmp, sizeof(myrpt->rxpl) - 1);
			if ((!strcmp(myrpt->remoterig, remote_rig_rbi)) ||
			  (!strcmp(myrpt->remoterig, remote_rig_ft100)))
			{
				ast_copy_string(myrpt->txpl, tmp, sizeof(myrpt->txpl) - 1);
			}
			if (setrem(myrpt) == -1){
				ast_copy_string(myrpt->rxpl, savestr, sizeof(myrpt->rxpl) - 1);
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
			ast_copy_string(savestr, myrpt->txpl, sizeof(savestr) - 1);
			ast_copy_string(myrpt->txpl, tmp, sizeof(myrpt->txpl) - 1);

			if (setrem(myrpt) == -1){
				ast_copy_string(myrpt->txpl, savestr, sizeof(myrpt->txpl) - 1);
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
				ast_copy_string(myrpt->loginuser,cp1 + 1,sizeof(myrpt->loginuser)-1);
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
				if (strncasecmp(ast_channel_name(myrpt->txchannel),
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

static int attempt_reconnect(struct rpt *myrpt, struct rpt_link *l)
{
	char *s, *s1, *tele;
	char tmp[300], deststr[325] = "";
	char sx[320],*sy;
	struct ast_frame *f1;
	struct ast_format_cap *cap;

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
		/*! \todo Seems wrong... IAX2 doesn't necessarily need to run on 4569, and that's the default if no port is specified. */
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

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		return -1;
	}

	ast_format_cap_append(cap, ast_format_slin, 0);

	l->chan = ast_request(deststr, cap, NULL, NULL, tele, NULL);
	if (!(l->chan))
	{
		usleep(150000);
		l->chan = ast_request(deststr, cap, NULL, NULL, tele, NULL);
	}
	ao2_ref(cap, -1);
	l->linkmode = 0;
	l->lastrx1 = 0;
	l->lastrealrx = 0;
	l->rxlingertimer = ((l->iaxkey) ? RX_LINGER_TIME_IAXKEY : RX_LINGER_TIME);
	l->newkeytimer = NEWKEYTIME;
	l->newkey = 2;
	while((f1 = AST_LIST_REMOVE_HEAD(&l->textq,frame_list))) ast_frfree(f1);
	if (l->chan){
		ast_set_read_format(l->chan, ast_format_slin);
		ast_set_write_format(l->chan, ast_format_slin);
		ast_channel_appl_set(l->chan, "Apprpt");
		ast_channel_data_set(l->chan, "(Remote Rx)");
		if (option_verbose > 2)
			ast_verb(3, "rpt (attempt_reconnect) initiating call to %s/%s on %s\n",
				deststr, tele, ast_channel_name(l->chan));
		ast_set_callerid(l->chan, myrpt->name, NULL, NULL);
        ast_call(l->chan,tele,999);
	}
	else
	{
		if (option_verbose > 2)
			ast_verb(3, "Unable to place call to %s/%s\n",
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

/* Scheduler */
/* must be called locked */
static void do_scheduler(struct rpt *myrpt)
{
	int i,res;

	struct ast_tm tmnow;
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
struct ast_format_cap *cap;

	if (myrpt->p.archivedir) mkdir(myrpt->p.archivedir,0600);
	sprintf(tmpstr,"%s/%s",myrpt->p.archivedir,myrpt->name);
	mkdir(tmpstr,0600);
	myrpt->ready = 0;
	rpt_mutex_lock(&myrpt->lock);
	myrpt->remrx = 0;
	myrpt->remote_webtransceiver = 0;

	telem = myrpt->tele.next;
	while(telem != &myrpt->tele) {
		ast_softhangup(telem->chan,AST_SOFTHANGUP_DEV);
		telem = telem->next;
	}
	rpt_mutex_unlock(&myrpt->lock);
	/* find our index, and load the vars initially */
	for(i = 0; i < nrpts; i++) {
		if (&rpt_vars[i] == myrpt) {
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

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		/*! \todo bail out or something */
	}

	ast_format_cap_append(cap, ast_format_slin, 0);
	/*! \todo call ao2_ref(cap, -1); on all exit points? */

	myrpt->rxchannel = ast_request(tmpstr, cap, NULL, NULL, tele, NULL);
	myrpt->zaprxchannel = NULL;
	if (!strcasecmp(tmpstr,"DAHDI"))
		myrpt->zaprxchannel = myrpt->rxchannel;
	if (myrpt->rxchannel) {
		if (ast_channel_state(myrpt->rxchannel) == AST_STATE_BUSY) {
			fprintf(stderr,"rpt:Sorry unable to obtain Rx channel\n");
			rpt_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
		ast_set_read_format(myrpt->rxchannel, ast_format_slin);
		ast_set_write_format(myrpt->rxchannel, ast_format_slin);
#ifdef	AST_CDR_FLAG_POST_DISABLED
		if (myrpt->rxchannel->cdr)
			ast_set_flag(myrpt->rxchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
		ast_channel_appl_set(myrpt->rxchannel, "Apprpt");
		ast_channel_data_set(myrpt->rxchannel, "(Repeater Rx)");
		if (option_verbose > 2)
			ast_verb(3, "rpt (Rx) initiating call to %s/%s on %s\n",
				tmpstr,tele,ast_channel_name(myrpt->rxchannel));
		ast_call(myrpt->rxchannel,tele,999);
		if (ast_channel_state(myrpt->rxchannel) != AST_STATE_UP)
		{
			rpt_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
	} else {
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
		myrpt->txchannel = ast_request(tmpstr, cap, NULL, NULL, tele, NULL);
		if ((!strcasecmp(tmpstr,"DAHDI")) && strcasecmp(tele,"pseudo"))
			myrpt->zaptxchannel = myrpt->txchannel;
		if (myrpt->txchannel)
		{
			if (ast_channel_state(myrpt->txchannel) == AST_STATE_BUSY)
			{
				fprintf(stderr,"rpt:Sorry unable to obtain Tx channel\n");
				rpt_mutex_unlock(&myrpt->lock);
				ast_hangup(myrpt->txchannel);
				ast_hangup(myrpt->rxchannel);
				myrpt->rpt_thread = AST_PTHREADT_STOP;
				pthread_exit(NULL);
			}
			ast_set_read_format(myrpt->txchannel, ast_format_slin);
			ast_set_write_format(myrpt->txchannel, ast_format_slin);
#ifdef	AST_CDR_FLAG_POST_DISABLED
			if (myrpt->txchannel->cdr)
				ast_set_flag(myrpt->txchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
			ast_channel_appl_set(myrpt->txchannel, "Apprpt");
			ast_channel_data_set(myrpt->txchannel, "(Repeater Tx)");
			if (option_verbose > 2)
				ast_verb(3, "rpt (Tx) initiating call to %s/%s on %s\n",
					tmpstr,tele,ast_channel_name(myrpt->txchannel));
			ast_call(myrpt->txchannel,tele,999);
			if (ast_channel_state(myrpt->rxchannel) != AST_STATE_UP)
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
	} else {
		myrpt->txchannel = myrpt->rxchannel;
		if ((!strncasecmp(myrpt->rxchanname,"DAHDI",3)) &&
		    strcasecmp(myrpt->rxchanname,"Zap/pseudo"))
			myrpt->zaptxchannel = myrpt->txchannel;
	}
	if (strncasecmp(ast_channel_name(myrpt->txchannel),"Zap/Pseudo",10))
	{
		ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_KEY);
		ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_UNKEY);
	}
	/* allocate a pseudo-channel thru asterisk */
	myrpt->pchannel = ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
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
	ast_set_read_format(myrpt->pchannel, ast_format_slin);
	ast_set_write_format(myrpt->pchannel, ast_format_slin);
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (myrpt->pchannel->cdr)
		ast_set_flag(myrpt->pchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	ast_answer(myrpt->pchannel);
	if (!myrpt->zaprxchannel) myrpt->zaprxchannel = myrpt->pchannel;
	if (!myrpt->zaptxchannel)
	{
		/* allocate a pseudo-channel thru asterisk */
		myrpt->zaptxchannel = ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
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
		ast_set_read_format(myrpt->zaptxchannel, ast_format_slin);
		ast_set_write_format(myrpt->zaptxchannel, ast_format_slin);
#ifdef	AST_CDR_FLAG_POST_DISABLED
		if (myrpt->zaptxchannel->cdr)
			ast_set_flag(myrpt->zaptxchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
		ast_answer(myrpt->zaptxchannel);
	}
	/* allocate a pseudo-channel thru asterisk */
	myrpt->monchannel = ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
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
	ast_set_read_format(myrpt->monchannel, ast_format_slin);
	ast_set_write_format(myrpt->monchannel, ast_format_slin);
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
	if (ioctl(ast_channel_fd(myrpt->zaptxchannel, 0),DAHDI_SETCONF,&ci) == -1)
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
	if (ioctl(ast_channel_fd(myrpt->pchannel, 0),DAHDI_SETCONF,&ci) == -1)
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
	if ((strstr(ast_channel_name(myrpt->txchannel),"pseudo") == NULL) &&
		(myrpt->zaptxchannel == myrpt->txchannel))
	{
		/* get tx channel's port number */
		if (ioctl(ast_channel_fd(myrpt->txchannel, 0),DAHDI_CHANNO,&ci.confno) == -1)
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
	} else {

		ci.confno = myrpt->txconf;
		ci.confmode = DAHDI_CONF_CONFANNMON;
	}
	/* first put the channel on the conference in announce mode */
	if (ioctl(ast_channel_fd(myrpt->monchannel, 0),DAHDI_SETCONF,&ci) == -1)
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
	myrpt->parrotchannel = ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
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
	ast_set_read_format(myrpt->parrotchannel, ast_format_slin);
	ast_set_write_format(myrpt->parrotchannel, ast_format_slin);
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (myrpt->parrotchannel->cdr)
		ast_set_flag(myrpt->parrotchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	ast_answer(myrpt->parrotchannel);

	/* Telemetry Channel Resources */
	/* allocate a pseudo-channel thru asterisk */
	myrpt->telechannel = ast_request("dahdi", cap, NULL, NULL, "pseudo", NULL);
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
	ast_set_read_format(myrpt->telechannel, ast_format_slin);
	ast_set_write_format(myrpt->telechannel, ast_format_slin);
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (myrpt->telechannel->cdr)
		ast_set_flag(myrpt->telechannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
 	/* make a conference for the voice/tone telemetry */
	ci.chan = 0;
	ci.confno = -1; // make a new conference
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;
 	/* put the channel on the conference in proper mode */
	if (ioctl(ast_channel_fd(myrpt->telechannel, 0),DAHDI_SETCONF,&ci) == -1)
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
	myrpt->btelechannel = ast_request("dahdi", cap, NULL, NULL, "pseudo", NULL);
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
	ast_set_read_format(myrpt->btelechannel, ast_format_slin);
	ast_set_write_format(myrpt->btelechannel, ast_format_slin);
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (myrpt->btelechannel->cdr)
		ast_set_flag(myrpt->btelechannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	/* make a conference linked to the main tx conference */
	ci.chan = 0;
	ci.confno = myrpt->txconf;
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER;
	/* first put the channel on the conference in proper mode */
	if (ioctl(ast_channel_fd(myrpt->btelechannel, 0), DAHDI_SETCONF, &ci) == -1)
	{
		ast_log(LOG_ERROR, "Failed to create btelechannel.\n");
		ast_hangup(myrpt->btelechannel);
		ast_hangup(myrpt->btelechannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}

	/* allocate a pseudo-channel thru asterisk */
	myrpt->voxchannel = ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
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
	ast_set_read_format(myrpt->voxchannel, ast_format_slin);
	ast_set_write_format(myrpt->voxchannel, ast_format_slin);
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (myrpt->voxchannel->cdr)
		ast_set_flag(myrpt->voxchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	ast_answer(myrpt->voxchannel);
	/* allocate a pseudo-channel thru asterisk */
	myrpt->txpchannel = ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
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
	if (ioctl(ast_channel_fd(myrpt->txpchannel, 0),DAHDI_SETCONF,&ci) == -1)
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
			if (strncasecmp(ast_channel_name(myrpt->txchannel),"Zap/Pseudo",10))
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
			if (strncasecmp(ast_channel_name(myrpt->txchannel),"Zap/Pseudo",10))
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
			if (ioctl(ast_channel_fd(myrpt->parrotchannel, 0),DAHDI_SETCONF,&ci) == -1)
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
				if ((strncasecmp(ast_channel_name(myrpt->rxchannel),"zap/", 4) == 0) ||
				    (strncasecmp(ast_channel_name(myrpt->rxchannel),"dahdi/", 6) == 0))
				{
					struct dahdi_radio_param r;

					memset(&r,0,sizeof(struct dahdi_radio_param));
					r.radpar = DAHDI_RADPAR_NOENCODE;
					r.data = !x;
					ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_RADIO_SETPARAM,&r);
				}
				if ((strncasecmp(ast_channel_name(myrpt->rxchannel),"radio/", 6) == 0) ||
				    (strncasecmp(ast_channel_name(myrpt->rxchannel),"simpleusb/", 10) == 0))
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
				lf.subclass.integer = 0;
				lf.offset = 0;
				lf.mallocd = 0;
				lf.samples = 0;
				l->linklisttimer = LINKLISTTIME;
				strcpy(lstr,"L ");
				__mklinklist(myrpt,l,lstr + 2,0);
				if (l->chan)
				{
					lf.datalen = strlen(lstr) + 1;
					lf.data.ptr = lstr;
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
			   ((!l->chan) || (ast_channel_state(l->chan) != AST_STATE_UP)))
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
			if (ioctl(ast_channel_fd(myrpt->parrotchannel, 0),DAHDI_SETCONF,&ci) == -1)
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

				if(sscanf((char *)f->data.ptr, "R %s", myrxrssi) == 1)
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
						i = tone_detect(&myrpt->burst_tone_state,f->data.ptr,f->samples);
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
					memset(f->data.ptr,0,f->datalen);
				}
				sp = (short *) f->data.ptr;
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
				rpt_filter(myrpt,f->data.ptr,f->datalen / 2);
#endif
				if ((!myrpt->localtx) && /* (!myrpt->p.linktolink) && */
				    (!myrpt->localoverride))
				{
					memset(f->data.ptr,0,f->datalen);
				}

				if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0), DAHDI_GETCONFMUTE, &ismuted) == -1)
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
					memset(f->data.ptr,0,f->datalen);
					if (myrpt->lastf1)
						memset(myrpt->lastf1->data.ptr,0,myrpt->lastf1->datalen);
					if (myrpt->lastf2)
						memset(myrpt->lastf2->data.ptr,0,myrpt->lastf2->datalen);
				}
				if (f) f2 = ast_frdup(f);
				else f2 = NULL;
				f1 = myrpt->lastf2;
				myrpt->lastf2 = myrpt->lastf1;
				myrpt->lastf1 = f2;
				if (ismuted)
				{
					if (myrpt->lastf1)
						memset(myrpt->lastf1->data.ptr,0,myrpt->lastf1->datalen);
					if (myrpt->lastf2)
						memset(myrpt->lastf2->data.ptr,0,myrpt->lastf2->datalen);
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
						write(myrpt->outstreampipe[1],f1->data.ptr,f1->datalen);
					}
				}
			}
			else if (f->frametype == AST_FRAME_DTMF_BEGIN)
			{
				if (myrpt->lastf1)
					memset(myrpt->lastf1->data.ptr,0,myrpt->lastf1->datalen);
				if (myrpt->lastf2)
					memset(myrpt->lastf2->data.ptr,0,myrpt->lastf2->datalen);
				dtmfed = 1;
				myrpt->lastdtmftime = ast_tvnow();
			}
			else if (f->frametype == AST_FRAME_DTMF)
			{
				c = (char) f->subclass.integer; /* get DTMF char */
				ast_frfree(f);
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
				if (myrpt->lastf1)
					memset(myrpt->lastf1->data.ptr,0,myrpt->lastf1->datalen);
				if (myrpt->lastf2)
					memset(myrpt->lastf2->data.ptr,0,myrpt->lastf2->datalen);
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
				if (f->subclass.integer == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
				/* if RX key */
				if (f->subclass.integer == AST_CONTROL_RADIO_KEY)
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
					if (f->datalen && f->data.ptr)
					{
						char *val, busy = 0;

						send_link_pl(myrpt,f->data.ptr);

						if (myrpt->p.nlocallist)
						{
							for(x = 0; x < myrpt->p.nlocallist; x++)
							{
								if (!strcasecmp(f->data.ptr,myrpt->p.locallist[x]))
								{
									myrpt->localoverride = 1;
									myrpt->keyed = 0;
									break;
								}
							}
						}
						if (debug) ast_log(LOG_NOTICE,"Got PL %s on node %s\n",(char *)f->data.ptr,myrpt->name);
						// ctcss code autopatch initiate
						if (strstr((char *)f->data.ptr,"/M/")&& !myrpt->macropatch)
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
								if (!busy) strcpy(myrpt->lasttone,(char*)f->data.ptr);
							}
							rpt_mutex_unlock(&myrpt->lock);
						}
						else if (strcmp((char *)f->data.ptr,myrpt->lasttone))
						{
							val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.tonemacro, (char *)f->data.ptr);
							if (val)
							{
								if (debug) ast_log(LOG_NOTICE,"Tone %s doing %s on node %s\n",(char *) f->data.ptr,val,myrpt->name);
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
						 	if (!busy) strcpy(myrpt->lasttone,(char*)f->data.ptr);
						}
					}
					else
					{
						myrpt->lasttone[0] = 0;
						send_link_pl(myrpt,"0");
					}
				}
				/* if RX un-key */
				if (f->subclass.integer == AST_CONTROL_RADIO_UNKEY)
				{
					char asleep;
					/* clear rx channel rssi	 */
					myrpt->rxrssi=0;
					asleep = myrpt->p.s[myrpt->p.sysstate_cur].sleepena & myrpt->sleep;

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
				if (strncasecmp(ast_channel_name(myrpt->rxchannel),"usrp/", 5) == 0)
				{
					char *argv[4];
					int argc = 4;
					argv[2] = myrpt->name;
					argv[3] = f->data.ptr;
					rpt_do_sendall2(0,argc,argv);
				}
				/* if is a USB device */
				if ((strncasecmp(ast_channel_name(myrpt->rxchannel),"radio/", 6) == 0) ||
				    (strncasecmp(ast_channel_name(myrpt->rxchannel),"simpleusb/", 10) == 0))
				{
					/* if message parsable */
					if (sscanf(f->data.ptr,"GPIO%d %d",&i,&j) >= 2)
					{
						sprintf(buf,"RPT_URI_GPIO%d",i);
						rpt_update_boolean(myrpt,buf,j);
					}
					/* if message parsable */
					else if (sscanf(f->data.ptr,"PP%d %d",&i,&j) >= 2)
					{
						sprintf(buf,"RPT_PP%d",i);
						rpt_update_boolean(myrpt,buf,j);
					}
					else if (!strcmp(f->data.ptr,"ENDPAGE"))
					{
						myrpt->paging.tv_sec = 0;
						myrpt->paging.tv_usec = 0;
					}
				}
				/* if is a BeagleBoard device */
				if (strncasecmp(ast_channel_name(myrpt->rxchannel),"beagle/", 7) == 0)
				{
					/* if message parsable */
					if (sscanf(f->data.ptr,"GPIO%d %d",&i,&j) >= 2)
					{
						sprintf(buf,"RPT_BEAGLE_GPIO%d",i);
						rpt_update_boolean(myrpt,buf,j);
					}
				}
				/* if is a Voter device */
				if (strncasecmp(ast_channel_name(myrpt->rxchannel),"voter/", 6) == 0)
				{
					struct rpt_link *l;
					struct	ast_frame wf;
					char	str[200];


					if (!strcmp(f->data.ptr,"ENDPAGE"))
					{
						myrpt->paging.tv_sec = 0;
						myrpt->paging.tv_usec = 0;
					}
					else
					{
						sprintf(str,"V %s %s",myrpt->name,(char *)f->data.ptr);
						wf.frametype = AST_FRAME_TEXT;
						wf.subclass.integer = 0;
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
							wf.data.ptr = str;
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
				if (f->subclass.integer == AST_CONTROL_HANGUP)
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
				if (f->subclass.integer == AST_CONTROL_HANGUP)
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
								memset(f1->data.ptr,0,f1->datalen);
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
						memset(f->data.ptr,0,f->datalen);
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
				if (f->subclass.integer == AST_CONTROL_HANGUP)
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
					if (strncasecmp(ast_channel_name(l->chan),"echolink",8) &&
					    strncasecmp(ast_channel_name(l->chan),"tlb",3))
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
					if (ioctl(ast_channel_fd(l->pchan, 0),DAHDI_GET_BUFINFO,&bi) != -1)
					{
						if ((f->samples > bi.bufsize) &&
							(bi.numbufs < ((f->samples / bi.bufsize) + 1)))
						{
							bi.numbufs = (f->samples / bi.bufsize) + 1;
							ioctl(ast_channel_fd(l->pchan, 0),DAHDI_SET_BUFINFO,&bi);
						}
					}
					fac = 1.0;
					if (l->chan && (!strncasecmp(ast_channel_name(l->chan),"echolink",8)))
						fac = myrpt->p.erxgain;
					if (l->chan && (!strncasecmp(ast_channel_name(l->chan),"tlb",3)))
						fac = myrpt->p.trxgain;
					if ((myrpt->p.linkmongain != 1.0) && (l->mode != 1) && (l->wouldtx))
						fac *= myrpt->p.linkmongain;
					if (fac != 1.0)
					{
						int x1;
						short *sp;

						sp = (short *)f->data.ptr;
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
					    (!strncasecmp(ast_channel_name(l->chan),"echolink",8)) ||
					       (!strncasecmp(ast_channel_name(l->chan),"tlb",3)))
					{
						if (l->phonevox)
						{
							n1 = dovox(&l->vox,
								f->data.ptr,f->datalen / 2);
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
										memset(f1->data.ptr,0,f1->datalen);
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
								memset(f->data.ptr,0,f->datalen);
							}
							else
							{
								ast_frfree(f);
								f = AST_LIST_REMOVE_HEAD(&l->rxq,frame_list);
							}
						}
						if (ioctl(ast_channel_fd(l->chan, 0), DAHDI_GETCONFMUTE, &ismuted) == -1)
						{
							ismuted = 0;
						}
						/* if not receiving, zero-out audio */
						ismuted |= (!l->lastrx);
						if (l->dtmfed &&
							(l->phonemode ||
							    (!strncasecmp(ast_channel_name(l->chan),"echolink",8)) ||
								(!strncasecmp(ast_channel_name(l->chan),"tlb",3)))) ismuted = 1;
						l->dtmfed = 0;

						/* if a voting rx link and not the winner, mute audio */
						if(myrpt->p.votertype==1 && l->voterlink && myrpt->voted_link!=l)
						{
							ismuted=1;
						}

						if (ismuted)
						{
							memset(f->data.ptr,0,f->datalen);
							if (l->lastf1)
								memset(l->lastf1->data.ptr,0,l->lastf1->datalen);
							if (l->lastf2)
								memset(l->lastf2->data.ptr,0,l->lastf2->datalen);
						}
						if (f) f2 = ast_frdup(f);
						else f2 = NULL;
						f1 = l->lastf2;
						l->lastf2 = l->lastf1;
						l->lastf1 = f2;
						if (ismuted)
						{
							if (l->lastf1)
								memset(l->lastf1->data.ptr,0,l->lastf1->datalen);
							if (l->lastf2)
								memset(l->lastf2->data.ptr,0,l->lastf2->datalen);
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
							memset(f->data.ptr,0,f->datalen);
						ast_write(l->pchan,f);
					}
				}
				else if (f->frametype == AST_FRAME_DTMF_BEGIN)
				{
					if (l->lastf1)
						memset(l->lastf1->data.ptr,0,l->lastf1->datalen);
					if (l->lastf2)
						memset(l->lastf2->data.ptr,0,l->lastf2->datalen);
					l->dtmfed = 1;
				}
				if (f->frametype == AST_FRAME_TEXT)
				{
					char *tstr = ast_malloc(f->datalen + 1);
					if (tstr)
					{
						memcpy(tstr,f->data.ptr,f->datalen);
						tstr[f->datalen] = 0;
						handle_link_data(myrpt,l,tstr);
						ast_free(tstr);
					}
				}
				if (f->frametype == AST_FRAME_DTMF)
				{
					if (l->lastf1)
						memset(l->lastf1->data.ptr,0,l->lastf1->datalen);
					if (l->lastf2)
						memset(l->lastf2->data.ptr,0,l->lastf2->datalen);
					l->dtmfed = 1;
					handle_link_phone_dtmf(myrpt,l,f->subclass.integer);
				}
				if (f->frametype == AST_FRAME_CONTROL)
				{
					if (f->subclass.integer == AST_CONTROL_ANSWER)
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
					if ((f->subclass.integer == AST_CONTROL_RADIO_KEY) && (l->newkey < 2))
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
					if (f->subclass.integer == AST_CONTROL_RADIO_UNKEY)
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
					if (f->subclass.integer == AST_CONTROL_HANGUP)
					{
						ast_frfree(f);
						rpt_mutex_lock(&myrpt->lock);
						__kickshort(myrpt);
						rpt_mutex_unlock(&myrpt->lock);
						if (strncasecmp(ast_channel_name(l->chan),"echolink",8) &&
							strncasecmp(ast_channel_name(l->chan),"tlb",3))
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
					if (l->chan && (!strncasecmp(ast_channel_name(l->chan),"echolink",8)))
						fac = myrpt->p.etxgain;
					if (l->chan && (!strncasecmp(ast_channel_name(l->chan),"tlb",3)))
						fac = myrpt->p.ttxgain;

					if (fac != 1.0)
					{
						int x1;
						short *sp;

						sp = (short *) f->data.ptr;
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
						strncasecmp(ast_channel_name(l->chan),"IAX",3)))
							ast_write(l->chan,f);
				}
				if (f->frametype == AST_FRAME_CONTROL)
				{
					if (f->subclass.integer == AST_CONTROL_HANGUP)
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
					write(myrpt->outstreampipe[1],f->data.ptr,f->datalen);
				}
				fs = ast_frdup(f);
				fac = 1.0;
				if (l->chan && (!strncasecmp(ast_channel_name(l->chan),"echolink",8)))
					fac = myrpt->p.etxgain;
				if (fac != 1.0)
				{
					sp = (short *)fs->data.ptr;
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
					    strncasecmp(ast_channel_name(l->chan),"IAX",3)))
					    {
						if (l->chan &&
						    (!strncasecmp(ast_channel_name(l->chan),"irlp",4)))
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
				if (f->subclass.integer == AST_CONTROL_HANGUP)
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
				if (f->subclass.integer == AST_CONTROL_HANGUP)
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
				n = dovox(&myrpt->vox,f->data.ptr,f->datalen / 2);
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
				if (f->subclass.integer == AST_CONTROL_HANGUP)
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
				if (f->subclass.integer == AST_CONTROL_HANGUP)
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
					short *sp = (short *) f->data.ptr;
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
				if (f->subclass.integer == AST_CONTROL_HANGUP)
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
				if (f->subclass.integer == AST_CONTROL_HANGUP)
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
	/* wait until asterisk starts */
        while(!ast_test_flag(&ast_options,AST_OPT_FLAG_FULLY_BOOTED))
                usleep(250000);
	rpt_vars[n].cfg = ast_config_load("rpt.conf",config_flags);
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

static int rpt_exec(struct ast_channel *chan, const char *data)
{
	int res=-1,i,x,rem_totx,rem_rx,remkeyed,n,phone_mode = 0;
	int iskenwood_pci4,authtold,authreq,setting,notremming,reming;
	int ismuted,dtmfed,phone_vox = 0, phone_monitor = 0;
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
	struct ast_format_cap *cap;

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

	ast_set_read_format(chan, ast_format_slin);
	ast_set_write_format(chan, ast_format_slin);

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
		//b1 = ast_channel_caller(chan)->id.number.str;
		b1 = ast_channel_caller(chan)->id.number.str;
		if (b1) ast_shrink_phone_number(b1);
		cfg = ast_config_load("rpt.conf",config_flags);
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
			        if (!strncmp(ast_channel_name(chan),"Local",5)) {
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
					strncpy(nodeip,ast_inet_ntoa(ia),sizeof(nodeip) - 1);
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
						strncpy(nodeip,ast_inet_ntoa(ia),sizeof(nodeip) - 1);
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
				ast_verb(3, "Set Asterisk channel variable %s for node %s\n",
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
	        if ( (strncmp(ast_channel_name(chan),"IAX2",4)) && (strncmp(ast_channel_name(chan),"Local",5)) &&
		  (strncasecmp(ast_channel_name(chan),"echolink",8)) && (strncasecmp(ast_channel_name(chan),"tlb",3)) ) {
	            ast_log(LOG_WARNING, "We only accept links via IAX2, Echolink, TheLinkBox or Local!!\n");
	            return -1;
	        }
#else
		if (strncmp(ast_channel_name(chan),"IAX2",4) && strncasecmp(ast_channel_name(chan),"Echolink",8) &&
			strncasecmp(ast_channel_name(chan),"tlb",3))
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
		//int l, m, lot, timeout = 0;
		int l, m, timeout = 0;
		char tmp[256],*template;
		char *working, *context, *exten, *priority;
		char *s,*orig_s;

		rpt_mutex_lock(&myrpt->lock);
		m = myrpt->callmode;
		rpt_mutex_unlock(&myrpt->lock);

		if ((!myrpt->p.nobusyout) && m)
		{
			if (ast_channel_state(chan) != AST_STATE_UP)
			{
				ast_indicate(chan,AST_CONTROL_BUSY);
			}
			while(ast_safe_sleep(chan,10000) != -1);
			return -1;
		}

		if (ast_channel_state(chan) != AST_STATE_UP)
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
		ast_channel_priority_set(chan, atoi(priority));
		if(exten)
			ast_channel_exten_set(chan, exten);
		if(context)
			ast_channel_context_set(chan, context);
		} else {  /* increment the priority by default*/
			ast_channel_priority_set(chan, ast_channel_priority(chan) + 1);
		}

		if(option_verbose > 2) {
			ast_verb(3, "Return Context: (%s,%s,%d) ID: %s\n", ast_channel_context(chan),ast_channel_exten(chan), ast_channel_priority(chan), ast_channel_caller(chan)->id.number.str);
			if(!ast_exists_extension(chan, ast_channel_context(chan), ast_channel_exten(chan), ast_channel_priority(chan), ast_channel_caller(chan)->id.number.str)) {
				ast_verb(3, "Warning: Return Context Invalid, call will return to default|s\n");
			}
		}

		/* we are using masq_park here to protect * from touching the channel once we park it.  If the channel comes out of timeout
		before we are done announcing and the channel is messed with, Kablooeee.  So we use Masq to prevent this.  */

		/*! \todo the parking API changed a while ago, this all needs to be completely redone here */
		// old way: https://github.com/asterisk/asterisk/blob/1.8/apps/app_parkandannounce.c
		// new way: https://github.com/asterisk/asterisk/blob/master/res/parking/parking_applications.c#L890

		//ast_masq_park_call(chan, NULL, timeout, &lot); // commented out to avoid compiler error.

		//if (option_verbose > 2) ast_verb(3, "Call Parking Called, lot: %d, timeout: %d, context: %s\n", lot, timeout, return_context);

		//snprintf(tmp,sizeof(tmp) - 1,"%d,%s",lot,template + 1);

		rpt_telemetry(myrpt,REV_PATCH,tmp);

		ast_free(orig_s);

		return 0;

	}
	i = 0;
	if (!strncasecmp(ast_channel_name(chan),"echolink",8)) i = 1;
	if (!strncasecmp(ast_channel_name(chan),"tlb",3)) i = 1;
	if ((!options) && (!i))
	{
	        struct ast_hostent ahp;
	        struct hostent *hp;
	        struct in_addr ia;
	        char hisip[100],nodeip[100], *s, *s1, *s2, *s3;

		/* look at callerid to see what node this comes from */
		if (!ast_channel_caller(chan)->id.number.str) /* if doesn't have caller id */
		{
			ast_log(LOG_WARNING, "Does not have callerid on %s\n",tmp);
			return -1;
		}
		/* get his IP from IAX2 module */
		memset(hisip,0,sizeof(hisip));
#ifdef ALLOW_LOCAL_CHANNELS
	        /* set IP address if this is a local connection*/
	        if (strncmp(ast_channel_name(chan),"Local",5)==0) {
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
		//b = chan->cid.cid_name;
		b = ast_channel_caller(chan)->id.name.str;
		b1 = ast_channel_caller(chan)->id.number.str;
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
			strncpy(nodeip,ast_inet_ntoa(ia),sizeof(nodeip) - 1);
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
				strncpy(nodeip,ast_inet_ntoa(ia),sizeof(nodeip) - 1);
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
		struct ast_format_cap *cap;

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
		if (!ast_channel_caller(chan)->id.number.str) /* if doesn't have caller id */
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
			//b = chan->cid.cid_name;
			b = ast_channel_caller(chan)->id.name.str;
			b1 = ast_channel_caller(chan)->id.number.str;
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
		    strncasecmp(ast_channel_name(chan),"echolink",8) &&
			strncasecmp(ast_channel_name(chan),"tlb",3)) l->newkey = 2;
		if (l->name[0] > '9') l->newkeytimer = 0;
		voxinit_link(l,1);
		if (!strncasecmp(ast_channel_name(chan),"echolink",8))
			init_linkmode(myrpt,l,LINKMODE_ECHOLINK);
		else if (!strncasecmp(ast_channel_name(chan),"tlb",3))
			init_linkmode(myrpt,l,LINKMODE_TLB);
		else if (phone_mode) init_linkmode(myrpt,l,LINKMODE_PHONE);
		else init_linkmode(myrpt,l,LINKMODE_GUI);
		ast_set_read_format(l->chan, ast_format_slin);
		ast_set_write_format(l->chan, ast_format_slin);
		gettimeofday(&myrpt->lastlinktime,NULL);

		cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (!cap) {
			ast_log(LOG_ERROR, "Failed to alloc cap\n");
			pthread_exit(NULL);
		}

		ast_format_cap_append(cap, ast_format_slin, 0);

		/* allocate a pseudo-channel thru asterisk */
		l->pchan = ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
		ao2_ref(cap, -1);
		if (!l->pchan)
		{
			fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
			pthread_exit(NULL);
		}
		ast_set_read_format(l->pchan, ast_format_slin);
		ast_set_write_format(l->pchan, ast_format_slin);
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
		if (ioctl(ast_channel_fd(l->pchan, 0),DAHDI_SETCONF,&ci) == -1)
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
		if (ast_channel_state(chan) != AST_STATE_UP)
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
		if ((!strncasecmp(ast_channel_name(l->chan),"echolink",8)) ||
		    (!strncasecmp(ast_channel_name(l->chan),"tlb",3)) ||
		      (l->name[0] > '9'))
			rpt_telemetry(myrpt,CONNECTED,l);
		//return AST_PBX_KEEPALIVE;
		return -1; /*! \todo AST_PBX_KEEPALIVE doesn't exist anymore. Figure out what we should return here. */
	}
	/* well, then it is a remote */
	rpt_mutex_lock(&myrpt->lock);
	/* look at callerid to see what node this comes from */
	if (!ast_channel_caller(chan)->id.number.str) /* if doesn't have caller id */
	{
		b1 = "0";
		b = NULL;
	} else {
		//b = chan->cid.cid_name;
		b = ast_channel_caller(chan)->id.name.str;
		b1 = ast_channel_caller(chan)->id.number.str;
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
			struct ast_tone_zone_sound *ts = NULL;

			ast_log(LOG_WARNING, "Trying to use busy link on %s\n",tmp);
			if (myrpt->remote_webtransceiver || (b && (*b > '9')))
			{
				ts = ast_get_indication_tone(ast_channel_zone(chan), "busy");
				ast_playtones_start(chan,0,ts->data, 1);
				i = 0;
				while(ast_channel_generatordata(chan) && (i < 5000))
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

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		pthread_exit(NULL);
	}

	ast_format_cap_append(cap, ast_format_slin, 0);

	myrpt->rxchannel = ast_request(myrpt->rxchanname, cap, NULL, NULL, tele, NULL);

	myrpt->zaprxchannel = NULL;
	if (!strcasecmp(myrpt->rxchanname,"DAHDI"))
		myrpt->zaprxchannel = myrpt->rxchannel;
	if (myrpt->rxchannel)
	{
		ast_set_read_format(myrpt->rxchannel, ast_format_slin);
		ast_set_write_format(myrpt->rxchannel, ast_format_slin);
#ifdef	AST_CDR_FLAG_POST_DISABLED
		if (myrpt->rxchannel->cdr)
			ast_set_flag(myrpt->rxchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
		ast_channel_appl_set(myrpt->rxchannel, "Apprpt");
		ast_channel_data_set(myrpt->rxchannel, "(Link Rx)");
		if (option_verbose > 2)
			ast_verb(3, "rpt (Rx) initiating call to %s/%s on %s\n",
				myrpt->rxchanname,tele,ast_channel_name(myrpt->rxchannel));
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
		myrpt->txchannel = ast_request(myrpt->txchanname, cap, NULL, NULL, tele, NULL);
		if (!strncasecmp(myrpt->txchanname,"DAHDI",3))
			myrpt->zaptxchannel = myrpt->txchannel;
		if (myrpt->txchannel)
		{
			ast_set_read_format(myrpt->txchannel, ast_format_slin);
			ast_set_write_format(myrpt->txchannel, ast_format_slin);
#ifdef	AST_CDR_FLAG_POST_DISABLED
			if (myrpt->txchannel->cdr)
				ast_set_flag(myrpt->txchannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
			ast_channel_appl_set(myrpt->txchannel, "Apprpt");
			ast_channel_data_set(myrpt->txchannel, "(Link Tx)");
			if (option_verbose > 2)
				ast_verb(3, "rpt (Tx) initiating call to %s/%s on %s\n",
					myrpt->txchanname,tele,ast_channel_name(myrpt->txchannel));
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
		if (!strncasecmp(myrpt->rxchanname,"DAHDI",3))
			myrpt->zaptxchannel = myrpt->rxchannel;
	}
	i = 3;
	ast_channel_setoption(myrpt->rxchannel,AST_OPTION_TONE_VERIFY,&i,sizeof(char),0);
	/* allocate a pseudo-channel thru asterisk */
	myrpt->pchannel = ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
	ao2_ref(cap, -1);
	if (!myrpt->pchannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		rpt_mutex_unlock(&myrpt->lock);
		if (myrpt->txchannel != myrpt->rxchannel)
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		pthread_exit(NULL);
	}
	ast_set_read_format(myrpt->pchannel, ast_format_slin);
	ast_set_write_format(myrpt->pchannel, ast_format_slin);
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
	if (ioctl(ast_channel_fd(myrpt->pchannel, 0),DAHDI_SETCONF,&ci) == -1)
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
		res = ioctl(ast_channel_fd(myrpt->zaptxchannel, 0),DAHDI_RADIO_SETPARAM,&z);
		/* if PCIRADIO and kenwood selected */
		if ((!res) && (!strcmp(myrpt->remoterig,remote_rig_kenwood)))
		{
			z.radpar = DAHDI_RADPAR_UIOMODE;
			z.data = 1;
			if (ioctl(ast_channel_fd(myrpt->zaptxchannel, 0),DAHDI_RADIO_SETPARAM,&z) == -1)
			{
				ast_log(LOG_ERROR,"Cannot set UIOMODE\n");
				return -1;
			}
			z.radpar = DAHDI_RADPAR_UIODATA;
			z.data = 3;
			if (ioctl(ast_channel_fd(myrpt->zaptxchannel, 0),DAHDI_RADIO_SETPARAM,&z) == -1)
			{
				ast_log(LOG_ERROR,"Cannot set UIODATA\n");
				return -1;
			}
			i = DAHDI_OFFHOOK;
			if (ioctl(ast_channel_fd(myrpt->zaptxchannel, 0),DAHDI_HOOK,&i) == -1)
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
		ioctl(ast_channel_fd(myrpt->zaptxchannel, 0),DAHDI_HOOK,&i);
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
			if (ioctl(ast_channel_fd(myrpt->zaptxchannel, 0),DAHDI_RADIO_SETPARAM,&z) == -1)
			{
				ast_log(LOG_ERROR,"Cannot set UIOMODE\n");
				return -1;
			}
			z.radpar = DAHDI_RADPAR_UIODATA;
			z.data = 3;
			if (ioctl(ast_channel_fd(myrpt->zaptxchannel, 0),DAHDI_RADIO_SETPARAM,&z) == -1)
			{
				ast_log(LOG_ERROR,"Cannot set UIODATA\n");
				return -1;
			}
		}
	}
	if ((myrpt->p.nlconn) && ((strncasecmp(ast_channel_name(myrpt->rxchannel),"radio/", 6) == 0) ||
	    (strncasecmp(ast_channel_name(myrpt->rxchannel),"beagle/", 7) == 0) ||
		    (strncasecmp(ast_channel_name(myrpt->rxchannel),"simpleusb/", 10) == 0)))
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
	ast_set_write_format(chan, ast_format_slin);
	ast_set_read_format(chan, ast_format_slin);
	rem_rx = 0;
	remkeyed = 0;
	/* if we are on 2w loop and are a remote, turn EC on */
	if (myrpt->remote && (myrpt->rxchannel == myrpt->txchannel))
	{
		i = 128;
		ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_ECHOCANCEL,&i);
	}
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_answer(chan);
		if (!phone_mode) send_newkey(chan);
	}

	if (myrpt->rxchannel == myrpt->zaprxchannel)
	{
		if (ioctl(ast_channel_fd(myrpt->zaprxchannel, 0),DAHDI_GET_PARAMS,&par) != -1)
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
	if (!ast_channel_caller(chan)->id.number.str) /* if doesn't have caller id */
	{
		b1 = "0";
		b = NULL;
	} else {
		b = ast_channel_caller(chan)->id.name.str;
		b1 = ast_channel_caller(chan)->id.number.str;
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
		sprintf(mycmd,"mixmonitor start %s %s/%s/%s.wav49 a",ast_channel_name(chan),
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
						if (ioctl(ast_channel_fd(myrpt->zaptxchannel, 0),DAHDI_RADIO_SETPARAM,&z) == -1)
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
				if (ioctl(ast_channel_fd(myrpt->zaptxchannel, 0),DAHDI_RADIO_SETPARAM,&z) == -1)
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
						f->data.ptr,f->datalen / 2);
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
							memset(f1->data.ptr,0,f1->datalen);
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
						memset(f->data.ptr,0,f->datalen);
					}
					else
					{
						ast_frfree(f);
						f = AST_LIST_REMOVE_HEAD(&myrpt->rxq,frame_list);
					}
				}
				if (ioctl(ast_channel_fd(chan, 0), DAHDI_GETCONFMUTE, &ismuted) == -1)
				{
					ismuted = 0;
				}
				/* if not transmitting, zero-out audio */
				ismuted |= (!myrpt->remotetx);
				if (dtmfed && phone_mode) ismuted = 1;
				dtmfed = 0;
				if (ismuted)
				{
					memset(f->data.ptr,0,f->datalen);
					if (myrpt->lastf1)
						memset(myrpt->lastf1->data.ptr,0,myrpt->lastf1->datalen);
					if (myrpt->lastf2)
						memset(myrpt->lastf2->data.ptr,0,myrpt->lastf2->datalen);
				}
				if (f) f2 = ast_frdup(f);
				else f2 = NULL;
				f1 = myrpt->lastf2;
				myrpt->lastf2 = myrpt->lastf1;
				myrpt->lastf1 = f2;
				if (ismuted)
				{
					if (myrpt->lastf1)
						memset(myrpt->lastf1->data.ptr,0,myrpt->lastf1->datalen);
					if (myrpt->lastf2)
						memset(myrpt->lastf2->data.ptr,0,myrpt->lastf2->datalen);
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
			else if (f->frametype == AST_FRAME_DTMF_BEGIN)
			{
				if (myrpt->lastf1)
					memset(myrpt->lastf1->data.ptr,0,myrpt->lastf1->datalen);
				if (myrpt->lastf2)
					memset(myrpt->lastf2->data.ptr,0,myrpt->lastf2->datalen);
				dtmfed = 1;
			}
			if (f->frametype == AST_FRAME_DTMF)
			{
				if (myrpt->lastf1)
					memset(myrpt->lastf1->data.ptr,0,myrpt->lastf1->datalen);
				if (myrpt->lastf2)
					memset(myrpt->lastf2->data.ptr,0,myrpt->lastf2->datalen);
				dtmfed = 1;
				if (handle_remote_phone_dtmf(myrpt,f->subclass.integer,&keyed,phone_mode) == -1)
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
					memcpy(tstr,f->data.ptr,f->datalen);
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
				if (f->subclass.integer == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
				/* if RX key */
				if ((f->subclass.integer == AST_CONTROL_RADIO_KEY) && (myrpt->newkey < 2))
				{
					if (debug == 7) printf("@@@@ rx key\n");
					keyed = 1;
					myrpt->rerxtimer = 0;
				}
				/* if RX un-key */
				if (f->subclass.integer == AST_CONTROL_RADIO_UNKEY)
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
					memset(f->data.ptr,0,f->datalen);
				 ast_write(myrpt->pchannel,f);
			}
			else if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass.integer == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
				/* if RX key */
				if (f->subclass.integer == AST_CONTROL_RADIO_KEY)
				{
					if (debug == 7) printf("@@@@ remote rx key\n");
					if (!myrpt->remotetx)
					{
						remkeyed = 1;
					}
				}
				/* if RX un-key */
				if (f->subclass.integer == AST_CONTROL_RADIO_UNKEY)
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
					short *sp = (short *) f->data.ptr;
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
				if (f->subclass.integer == AST_CONTROL_HANGUP)
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
				if (f->subclass.integer == AST_CONTROL_HANGUP)
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
				    strncasecmp(ast_channel_name(chan),"IAX",3))
					ast_write(chan,f);
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass.integer == AST_CONTROL_HANGUP)
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
 				if (f->subclass.integer == AST_CONTROL_HANGUP)
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
		if (!ast_channel_caller(chan)->id.number.str) /* if doesn't have caller id */
		{
			b1 = "0";
		} else {
			ast_callerid_parse(ast_channel_caller(chan)->id.number.str,&b,&b1);
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
	sprintf(tmp,"mixmonitor stop %s",ast_channel_name(chan));
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
		if (ioctl(ast_channel_fd(myrpt->zaptxchannel, 0),DAHDI_RADIO_SETPARAM,&z) == -1)
		{
			ast_log(LOG_ERROR,"Cannot set UIOMODE\n");
			return -1;
		}
		z.radpar = DAHDI_RADPAR_UIODATA;
		z.data = 3;
		if (ioctl(ast_channel_fd(myrpt->zaptxchannel, 0),DAHDI_RADIO_SETPARAM,&z) == -1)
		{
			ast_log(LOG_ERROR,"Cannot set UIODATA\n");
			return -1;
		}
		i = DAHDI_OFFHOOK;
		if (ioctl(ast_channel_fd(myrpt->zaptxchannel, 0),DAHDI_HOOK,&i) == -1)
		{
			ast_log(LOG_ERROR,"Cannot set hook\n");
			return -1;
		}
	}
	if ((myrpt->p.nldisc) && ((strncasecmp(ast_channel_name(myrpt->rxchannel),"radio/", 6) == 0) ||
	    (strncasecmp(ast_channel_name(myrpt->rxchannel),"beagle/", 7) == 0) ||
		    (strncasecmp(ast_channel_name(myrpt->rxchannel),"simpleusb/", 10) == 0)))
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
	return res;
}

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

static int mdcgen_exec(struct ast_channel *chan, const char *data)
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
	ast_free(tmp);
	ast_module_user_remove(u);
	return res;
}

#endif

static int unload_module(void)
{
	int i, res;

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

	ast_cli_unregister_multiple(rpt_cli,sizeof(rpt_cli) /
		sizeof(struct ast_cli_entry));
	res |= ast_manager_unregister("RptLocalNodes");
	res |= ast_manager_unregister("RptStatus");
	close(nullfd);
	return res;
}

static int load_module(void)
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

	ast_cli_register_multiple(rpt_cli,sizeof(rpt_cli) /
		sizeof(struct ast_cli_entry));
	res = 0;
	res |= ast_manager_register("RptLocalNodes", 0, manager_rpt_local_nodes, "List local node numbers");
	res |= ast_manager_register("RptStatus", 0, manager_rpt_status, "Return Rpt Status for CGI");
	res |= ast_register_application(app, rpt_exec, synopsis, descrip);

#ifdef	_MDC_ENCODE_H_
	res |= ast_register_application(mdc_app, mdcgen_exec, mdc_synopsis, mdc_descrip);
#endif

	return res;
}

int reload(void)
{
int	i,n;
struct ast_config *cfg;
char	*val,*this;

	cfg = ast_config_load("rpt.conf",config_flags);
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

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Radio Repeater/Remote Base Application",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
