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
 * \note significant rewrite for modern Asterisk by Naveen Albert <asterisk@phreaknet.org>
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
 * farenddisconnect=1	:	Automatically disconnect when called party hangs up
 * noct=1				:	Don't send repeater courtesy tone during autopatch calls
 * quiet=1				:	Don't send dial tone, or connect messages. Do not send patch down message when called party hangs up
 *
 *
 * Example: 123=autopatchup,dialtime=20000,noct=1,farenddisconnect=1
 *
 *  To send an asterisk (*) while dialing or talking on phone,
 *  use the autopatch access code.
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
	<depend>res_curl</depend>
	<depend>curl</depend>
	<depend>dahdi</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#define	START_DELAY 2

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <search.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fnmatch.h>
#include <curl/curl.h>
#include <termios.h>

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
#include "asterisk/app.h"
#include "asterisk/indications.h"
#include "asterisk/format.h"
#include "asterisk/dsp.h"

#include "app_rpt/app_rpt.h"

#ifdef HAVE_SYS_IO
#include <sys/io.h>
#endif

#include "app_rpt/rpt_mdc1200.h"

#include "app_rpt/rpt_lock.h"
#include "app_rpt/rpt_utils.h"
#include "app_rpt/rpt_daq.h"
#include "app_rpt/rpt_cli.h"
#include "app_rpt/rpt_bridging.h"
#include "app_rpt/rpt_call.h"
#include "app_rpt/rpt_capabilities.h"
#include "app_rpt/rpt_vox.h"
#include "app_rpt/rpt_serial.h" /* use serial_rxflush, serial_rxready */
#include "app_rpt/rpt_uchameleon.h"
#include "app_rpt/rpt_channel.h"
#include "app_rpt/rpt_config.h"
#include "app_rpt/rpt_telemetry.h"
#include "app_rpt/rpt_link.h"
#include "app_rpt/rpt_functions.h"
#include "app_rpt/rpt_manager.h"
#include "app_rpt/rpt_translate.h"
#include "app_rpt/rpt_xcat.h"
#include "app_rpt/rpt_rig.h"
#include "app_rpt/rpt_radio.h"

/*** DOCUMENTATION
	<application name="Rpt" language="en_US">
		<synopsis>
			Radio Remote Link or Remote Base Link Endpoint Process
		</synopsis>
		<syntax>
			<parameter name="nodename">
				<para>Node name.</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="X">
						<para>Normal endpoint mode WITHOUT security check.
						Only specify this if you have checked security already (like with an IAX2 user/password or something).</para>
					</option>
					<option name="R">
						<para>announce-string[|timeout[|timeout-destination]] - Amateur Radio</para>
						<para>Reverse Autopatch. Caller is put on hold, and announcement (as specified by the 'announce-string') is played on radio system.
						Users of radio system can access autopatch, dial specified code, and pick up call. Announce-string is list of names of
						recordings, or <literal>PARKED</literal> to substitute code for un-parking or <literal>NODE</literal> to substitute node number.</para>
					</option>
					<option name="P">
						<para>Phone Control mode. This allows a regular phone user to have full control and audio access to the radio system. For the
						user to have DTMF control, the 'phone_functions' parameter must be specified for the node in 'rpt.conf'. An additional
						function (cop,6) must be listed so that PTT control is available.</para>
					</option>
					<option name="D">
						<para>Dumb Phone Control mode. This allows a regular phone user to have full control and audio access to the radio system. In this
						mode, the PTT is activated for the entire length of the call. For the user to have DTMF control (not generally recomended in
						this mode), the 'dphone_functions' parameter must be specified for the node in 'rpt.conf'. Otherwise no DTMF control will be
						available to the phone user.</para>
					</option>
					<option name="S">
						<para>Simplex Dumb Phone Control mode. This allows a regular phone user audio-only access to the radio system. In this mode, the
						transmitter is toggled on and off when the phone user presses the funcchar (*) key on the telephone set. In addition, the transmitter
						will turn off if the endchar (#) key is pressed. When a user first calls in, the transmitter will be off, and the user can listen for
						radio traffic. When the user wants to transmit, they press the * key, start talking, then press the * key again or the # key to turn
						the transmitter off.  No other functions can be executed by the user on the phone when this mode is selected. Note: If your
						radio system is full-duplex, we recommend using either P or D modes as they provide more flexibility.</para>
					</option>
					<option name="V">
						<para>Set Asterisk channel variable for specified node (e.g. Rpt(2000|V|foo=bar))</para>
					</option>
					<option name="M">
						<para>Memory Channel Steer as MXX where XX is the memory channel number.</para>
						<para>* - Alt Macro to execute (e.g. *7 for status)</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para> Not specifying an option puts it in normal endpoint mode (where source IP and nodename are verified).</para>
		</description>
	</application>
 ***/

static char *app = "Rpt";

struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS };

AST_MUTEX_DEFINE_STATIC(rpt_master_lock);

pthread_t rpt_master_thread;
struct nodelog nodelog;
struct rpt rpt_vars[MAXRPTS];

static int shutting_down = 0;

static int debug = 7;			/* Set this >0 for extra debug output */
static int nrpts = 0;

/* general settings */
enum rpt_dns_method rpt_node_lookup_method = DEFAULT_NODE_LOOKUP_METHOD;

int max_chan_stat[] = { 22000, 1000, 22000, 100, 22000, 2000, 22000 };

int nullfd = -1;

int rpt_debug_level(void)
{
	return debug;
}

int rpt_set_debug_level(int newlevel)
{
	int old_level = debug;

	if (newlevel < 0 || newlevel > 7) {
		return -1;
	}
	debug = newlevel;
	return old_level;
}

int rpt_num_rpts(void)
{
	return nrpts;
}

int rpt_nullfd(void)
{
	return nullfd;
}

static time_t starttime = 0;

time_t rpt_starttime(void)
{
	return starttime;
}

AST_MUTEX_DEFINE_STATIC(nodeloglock);

#ifndef NATIVE_DSP
static inline void goertzel_sample(goertzel_state_t * s, short sample)
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

static inline void goertzel_update(goertzel_state_t * s, short *samps, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		goertzel_sample(s, samps[i]);
	}
}

static inline float goertzel_result(goertzel_state_t * s)
{
	goertzel_result_t r;
	r.value = (s->v3 * s->v3) + (s->v2 * s->v2);
	r.value -= ((s->v2 * s->v3) >> 15) * s->fac;
	r.power = s->chunky * 2;
	return (float) r.value * (float) (1 << r.power);
}

static inline void goertzel_init(goertzel_state_t * s, float freq, int samples)
{
	s->v2 = s->v3 = s->chunky = 0.0;
	s->fac = (int) (32768.0 * 2.0 * cos(2.0 * M_PI * freq / TONE_SAMPLE_RATE));
}

static inline void goertzel_reset(goertzel_state_t * s)
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

	ast_debug(1, "Setup tone %d Hz, %d ms, block_size=%d, hits_required=%d\n", freq, duration, s->block_size, s->hits_required);
}

static int tone_detect(tone_detect_state_t * s, int16_t * amp, int samples)
{
	float tone_energy;
	int i;
	int hit = 0;
	int limit;
	int res = 0;
	int16_t *ptr;
	int start, end;

	for (start = 0; start < samples; start = end) {
		/* Process in blocks. */
		limit = samples - start;
		if (limit > s->samples_pending) {
			limit = s->samples_pending;
		}
		end = start + limit;

		for (i = limit, ptr = amp; i > 0; i--, ptr++) {
			/* signed 32 bit int should be enough to suqare any possible signed 16 bit value */
			s->energy += (int32_t) * ptr * (int32_t) * ptr;

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
		ast_debug(10, "tone %d, Ew=%.2E, Et=%.2E, s/n=%10.2f\n", s->freq, tone_energy, s->energy, tone_energy / (s->energy - tone_energy));
		if (tone_energy > s->energy * s->threshold) {
			ast_debug(10, "Hit! count=%d\n", s->hit_count);
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
			ast_debug(1, "%d Hz tone detected\n", s->freq);
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
#endif

/*! \brief Function table */
struct function_table_tag function_table[] = {
	{ "cop", function_cop },
	{ "autopatchup", function_autopatchup },
	{ "autopatchdn", function_autopatchdn },
	{ "ilink", function_ilink },
	{ "status", function_status },
	{ "remote", function_remote },
	{ "macro", function_macro },
	{ "playback", function_playback },
	{ "localplay", function_localplay },
	{ "meter", function_meter },
	{ "userout", function_userout },
	{ "cmd", function_cmd }
};

int function_table_index(const char *s)
{
	int i, l, num_actions = sizeof(function_table) / sizeof(struct function_table_tag);

	l = strlen(s);

	for (i = 0; i < num_actions; i++) {
		if (!strncasecmp(s, function_table[i].action, l)) {
			return i;
		}
	}
	return -1;
}

/*! \brief node logging function */
void donodelog(struct rpt *myrpt, char *str)
{
	struct nodelog *nodep;
	char datestr[100];

	if (!myrpt->p.archivedir) {
		return;
	}

	nodep = (struct nodelog *) ast_malloc(sizeof(struct nodelog));
	if (!nodep) {
		return;
	}
	time(&nodep->timestamp);
	ast_copy_string(nodep->archivedir, myrpt->p.archivedir, sizeof(nodep->archivedir));
	strftime(datestr, sizeof(datestr) - 1, "%Y%m%d%H%M%S", localtime(&nodep->timestamp));
	snprintf(nodep->str, sizeof(nodep->str) - 1, "%s %s,%s\n", myrpt->name, datestr, str);
	ast_mutex_lock(&nodeloglock);
	insque((struct qelem *) nodep, (struct qelem *) nodelog.prev);
	ast_mutex_unlock(&nodeloglock);
}

void __attribute__ ((format (gnu_printf, 5, 6))) __donodelog_fmt(struct rpt *myrpt, const char *file, int lineno, const char *func, const char *fmt, ...)
{
	va_list ap;
	char *buf;
	int len;

	va_start(ap, fmt);
	len = ast_vasprintf(&buf, fmt, ap);
	va_end(ap);

	if (len > 0) {
		donodelog(myrpt, buf);
		ast_free(buf);
	}
}

/*! \brief Routine to process events for rpt_master threads */
void rpt_event_process(struct rpt *myrpt)
{
	char *myval, *argv[5], *cmpvar, *var, *var1, *cmd, c;
	char buf[1000], valbuf[500], action;
	int i, l, argc, varp, var1p, thisAction, maxActions;
	struct ast_variable *v;
	struct ast_var_t *newvariable;

	if (!starttime)
		return;
	for (v = ast_variable_browse(myrpt->cfg, myrpt->p.events); v; v = v->next) {
		/* make a local copy of the value of this entry */
		myval = ast_strdupa(v->value);
		/* separate out specification into pipe-delimited fields */
		argc = ast_app_separate_args(myval, '|', argv, sizeof(argv) / sizeof(argv[0]));
		if (argc < 1)
			continue;
		if (argc != 3) {
			ast_log(LOG_ERROR, "event exec item malformed: %s\n", v->value);
			continue;
		}
		action = toupper(*argv[0]);
		if (!strchr("VGFCS", action)) {
			ast_log(LOG_ERROR, "Unrecognized event action (%c) in exec item malformed: %s\n", action, v->value);
			continue;
		}
		/* start indicating no command to do */
		cmd = NULL;
		c = toupper(*argv[1]);
		if (c == 'E') {			/* if to merely evaluate the statement */
			if (!strncasecmp(v->name, "RPT", 3)) {
				ast_log(LOG_ERROR, "%s is not a valid name for an event variable!!!!\n", v->name);
				continue;
			}
			if (!strncasecmp(v->name, "XX_", 3)) {
				ast_log(LOG_ERROR, "%s is not a valid name for an event variable!!!!\n", v->name);
				continue;
			}
			/* see if this var exists yet */
			myval = (char *) pbx_builtin_getvar_helper(myrpt->rxchannel, v->name);
			/* if not, set it to zero, in case of the value being self-referenced */
			if (!myval)
				pbx_builtin_setvar_helper(myrpt->rxchannel, v->name, "0");
			snprintf(valbuf, sizeof(valbuf) - 1, "$[ %s ]", argv[2]);
			buf[0] = 0;
			pbx_substitute_variables_helper(myrpt->rxchannel, valbuf, buf, sizeof(buf) - 1);
			if (pbx_checkcondition(buf)) {
				cmd = "TRUE";
			}
		} else {
			var = (char *) pbx_builtin_getvar_helper(myrpt->rxchannel, argv[2]);
			if (!var) {
				ast_log(LOG_ERROR, "Event variable %s not found\n", argv[2]);
				continue;
			}
			/* set to 1 if var is true */
			varp = ((pbx_checkcondition(var) > 0));
			for (i = 0; (!cmd) && (c = *(argv[1] + i)); i++) {
				cmpvar = (char *) ast_malloc(strlen(argv[2]) + 10);
				if (!cmpvar) {
					ast_log(LOG_ERROR, "Cannot malloc()\n");
					return;
				}
				sprintf(cmpvar, "XX_%s", argv[2]);
				var1 = (char *) pbx_builtin_getvar_helper(myrpt->rxchannel, cmpvar);
				var1p = !varp;	/* start with it being opposite */
				if (var1) {
					/* set to 1 if var is true */
					var1p = ((pbx_checkcondition(var1) > 0));
				}
//                              pbx_builtin_setvar_helper(myrpt->rxchannel,cmpvar,var);
				ast_free(cmpvar);
				c = toupper(c);
				if (!strchr("TFNI", c)) {
					ast_log(LOG_ERROR, "Unrecognized event type (%c) in exec item malformed: %s\n", c, v->value);
					continue;
				}
				switch (c) {
				case 'N':		/* if no change */
					if (var1 && (varp == var1p))
						cmd = (char *) v->name;
					break;
				case 'I':		/* if didnt exist (initial state) */
					if (!var1)
						cmd = (char *) v->name;
					break;
				case 'F':		/* transition to false */
					if (var1 && (var1p == 1) && (varp == 0))
						cmd = (char *) v->name;
					break;
				case 'T':		/* transition to true */
					if ((var1p == 0) && (varp == 1))
						cmd = (char *) v->name;
					break;
				}
			}
		}
		if (action == 'V') {	/* set a variable */
			pbx_builtin_setvar_helper(myrpt->rxchannel, v->name, (cmd) ? "1" : "0");
			continue;
		} else if (action == 'G') {	/* set a global variable */
			pbx_builtin_setvar_helper(NULL, v->name, (cmd) ? "1" : "0");
			continue;
		}
		/* if not command to execute, go to next one */
		if (!cmd) {
			continue;
		}
		if (action == 'F') {	/* excecute a function */
			rpt_mutex_lock(&myrpt->lock);
			if ((MAXMACRO - strlen(myrpt->macrobuf)) >= strlen(cmd)) {
				ast_verb(3, "Event on node %s doing macro %s for condition %s\n", myrpt->name, cmd, v->value);
				myrpt->macrotimer = MACROTIME;
				strncat(myrpt->macrobuf, cmd, MAXMACRO - 1);
			} else {
				ast_log(LOG_WARNING, "Could not execute event %s for %s: Macro buffer overflow\n", cmd, argv[1]);
			}
			rpt_mutex_unlock(&myrpt->lock);
		} else if (action == 'C') {	/* excecute a command */
			/* make a local copy of the value of this entry */
			myval = ast_strdupa(cmd);
			/* separate out specification into comma-delimited fields */
			argc = ast_app_separate_args(myval, ',', argv, sizeof(argv) / sizeof(argv[0]));
			if (argc < 1) {
				ast_log(LOG_ERROR, "event exec rpt command item malformed: %s\n", cmd);
				continue;
			}
			/* Look up the action */
			l = strlen(argv[0]);
			thisAction = -1;
			maxActions = sizeof(function_table) / sizeof(struct function_table_tag);
			for (i = 0; i < maxActions; i++) {
				if (!strncasecmp(argv[0], function_table[i].action, l)) {
					thisAction = i;
					break;
				}
			}
			if (thisAction < 0) {
				ast_log(LOG_ERROR, "Unknown action name %s.\n", argv[0]);
				continue;
			}
			ast_verb(3, "Event on node %s doing rpt command %s for condition %s\n", myrpt->name, cmd, v->value);
			rpt_mutex_lock(&myrpt->lock);
			if (myrpt->cmdAction.state == CMD_STATE_IDLE) {
				myrpt->cmdAction.state = CMD_STATE_BUSY;
				myrpt->cmdAction.functionNumber = thisAction;
				myrpt->cmdAction.param[0] = 0;
				if (argc > 1)
					ast_copy_string(myrpt->cmdAction.param, argv[1], MAXDTMF - 1);
				myrpt->cmdAction.digits[0] = 0;
				if (argc > 2) {
					ast_copy_string(myrpt->cmdAction.digits, argv[2], MAXDTMF - 1);
					snprintf(myrpt->cmdAction.param, MAXDTMF - 1, "%s,%s", argv[1], argv[2]);
				}
				myrpt->cmdAction.command_source = SOURCE_RPT;
				myrpt->cmdAction.state = CMD_STATE_READY;
			} else {
				ast_log(LOG_WARNING, "Could not execute event %s for %s: Command buffer in use\n", cmd, argv[1]);
			}
			rpt_mutex_unlock(&myrpt->lock);
		} else if (action == 'S') {	/* excecute a shell command */
			char *cp;

			ast_verb(3, "Event on node %s doing shell command %s for condition %s\n", myrpt->name, cmd, v->value);
			cp = ast_malloc(strlen(cmd) + 10);
			if (!cp) {
				return;
			}
			memset(cp, 0, strlen(cmd) + 10);
			sprintf(cp, "%s &", cmd);
			ast_safe_system(cp);
			ast_free(cp);
		}
	}
	for (v = ast_variable_browse(myrpt->cfg, myrpt->p.events); v; v = v->next) {
		/* make a local copy of the value of this entry */
		myval = ast_strdupa(v->value);
		/* separate out specification into pipe-delimited fields */
		argc = ast_app_separate_args(myval, '|', argv, sizeof(argv) / sizeof(argv[0]));
		if (argc != 3)
			continue;
		action = toupper(*argv[0]);
		if (!strchr("VGFCS", action))
			continue;
		c = *argv[1];
		if (c == 'E')
			continue;
		var = (char *) pbx_builtin_getvar_helper(myrpt->rxchannel, argv[2]);
		if (!var)
			continue;
		/* set to 1 if var is true */
		varp = ((pbx_checkcondition(var) > 0));
		cmpvar = (char *) ast_malloc(strlen(argv[2]) + 10);
		if (!cmpvar) {
			return;
		}
		sprintf(cmpvar, "XX_%s", argv[2]);
		var1 = (char *) pbx_builtin_getvar_helper(myrpt->rxchannel, cmpvar);
		pbx_builtin_setvar_helper(myrpt->rxchannel, cmpvar, var);
		ast_free(cmpvar);
	}
	if (option_verbose < 5)
		return;
	i = 0;
	ast_debug(2, "Node Variable dump for node %s:\n", myrpt->name);
	ast_channel_lock(myrpt->rxchannel);
	AST_LIST_TRAVERSE(ast_channel_varshead(myrpt->rxchannel), newvariable, entries) {
		i++;
		ast_debug(2, "   %s=%s\n", ast_var_name(newvariable), ast_var_value(newvariable));
	}
	ast_channel_unlock(myrpt->rxchannel);
	ast_debug(2, "    -- %d variables\n", i);
	return;
}

static void dodispgm(struct rpt *myrpt, char *them)
{
	char *a;
	int i;

	if (!myrpt->p.discpgm)
		return;
	i = strlen(them) + strlen(myrpt->p.discpgm) + 100;
	a = ast_malloc(i);
	if (!a) {
		return;
	}
	memset(a, 0, i);
	sprintf(a, "%s %s %s &", myrpt->p.discpgm, myrpt->name, them);
	ast_safe_system(a);
	ast_free(a);
	return;
}

static void doconpgm(struct rpt *myrpt, char *them)
{
	char *a;
	int i;

	if (!myrpt->p.connpgm)
		return;
	i = strlen(them) + strlen(myrpt->p.connpgm) + +100;
	a = ast_malloc(i);
	if (!a) {
		return;
	}
	memset(a, 0, i);
	sprintf(a, "%s %s %s &", myrpt->p.connpgm, myrpt->name, them);
	ast_safe_system(a);
	ast_free(a);
	return;
}

/*! \brief Waste the output of libcurl (the OK is sent to stdout) */
static size_t writefunction(void *ptr, size_t size, size_t nmemb, void *stream)
{
	return (nmemb*size);
}

static void *perform_statpost(void *stats_url)
{
	char *str;
	long rescode = 0;
	CURL *curl = curl_easy_init();

	if (!curl) {
		return NULL;
	}

	str = (char *) stats_url;
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunction);
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(curl, CURLOPT_URL, str);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, AST_CURL_USER_AGENT);
	curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &rescode);
	curl_easy_cleanup(curl);

	if (rescode != 200) {
		ast_log(LOG_WARNING, "statpost to URL '%s' failed with code %ld\n", (char *) stats_url, rescode);
	}
	ast_free(stats_url); /* Free here since parent is not responsible for it. */
	return NULL;
}

static void statpost(struct rpt *myrpt, char *pairs)
{
	char *str;
	time_t now;
	unsigned int seq;
	int res, len;
	pthread_t statpost_thread;

	if (!myrpt->p.statpost_url) {
		return;
	}

	len = strlen(pairs) + strlen(myrpt->p.statpost_url) + 200;
	str = ast_malloc(len);

	ast_mutex_lock(&myrpt->statpost_lock);
	seq = ++myrpt->statpost_seqno;
	ast_mutex_unlock(&myrpt->statpost_lock);

	time(&now);
	snprintf(str, len, "%s?node=%s&time=%u&seqno=%u%s%s", myrpt->p.statpost_url, myrpt->name, (unsigned int)now, seq, pairs ? "&" : "", S_OR(pairs, ""));

	/* Make the actual cURL call in a separate thread, so we can continue without blocking. */
	ast_debug(4, "Making statpost to %s\n", str);
	res = ast_pthread_create_detached(&statpost_thread, NULL, perform_statpost, (void *) str);
	if (res) {
		ast_log(LOG_ERROR, "Error creating statpost thread: %s\n", strerror(res));
		ast_free(str);
	}
}

/*
 * Function stream data
 */

static void startoutstream(struct rpt *myrpt)
{
	char *str;
	char *strs[100];
	int n;

	if (!myrpt->p.outstreamcmd) {
		return;
	}
	ast_verb(3, "app_rpt node %s starting output stream %s\n", myrpt->name, myrpt->p.outstreamcmd);
	str = ast_strdup(myrpt->p.outstreamcmd);
	if (!str) {
		return;
	}
	n = finddelim(str, strs, 100);
	if (n < 1) {
		return;
	}
	if (myrpt->outstreampipe[1] != -1) {
		close(myrpt->outstreampipe[1]);
		myrpt->outstreampipe[1] = -1;
		myrpt->outstreamlasterror = 0;
	}
	if (pipe(myrpt->outstreampipe) == -1) {
		ast_log(LOG_ERROR, "pipe() failed: %s\n", strerror(errno));
		ast_free(str);
		return;
	}
	if (fcntl(myrpt->outstreampipe[1], F_SETFL, O_NONBLOCK) == -1) {
		ast_log(LOG_ERROR, "Cannot set pipe to NONBLOCK: %s", strerror(errno));
		ast_free(str);
		return;
	}
	if (!(myrpt->outstreampid = fork())) {
		close(myrpt->outstreampipe[1]);
		if (dup2(myrpt->outstreampipe[0], fileno(stdin)) == -1) {
			ast_log(LOG_ERROR, "Cannot dup2() stdin: %s", strerror(errno));
			exit(0);
		}
		if (dup2(nullfd, fileno(stdout)) == -1) {
			ast_log(LOG_ERROR, "Cannot dup2() stdout: %s", strerror(errno));
			exit(0);
		}
		if (dup2(nullfd, fileno(stderr)) == -1) {
			ast_log(LOG_ERROR, "Cannot dup2() stderr: %s", strerror(errno));
			exit(0);
		}
		execv(strs[0], strs);
		ast_log(LOG_ERROR, "exec of %s failed: %s\n", strs[0], strerror(errno));
		exit(0);
	}
	ast_free(str);
	close(myrpt->outstreampipe[0]);
	myrpt->outstreampipe[0] = -1;
	if (myrpt->outstreampid == -1) {
		ast_log(LOG_ERROR, "fork() failed: %s\n", strerror(errno));
		close(myrpt->outstreampipe[1]);
		myrpt->outstreampipe[1] = -1;
	}
}

static int topcompar(const void *a, const void *b)
{
	struct rpt_topkey *x = (struct rpt_topkey *) a;
	struct rpt_topkey *y = (struct rpt_topkey *) b;

	return (x->timesince - y->timesince);
}

#ifdef	__RPT_NOTCH

/* rpt filter routine */
static void rpt_filter(struct rpt *myrpt, volatile short *buf, int len)
{
	int i, j;
	struct rptfilter *f;

	for (i = 0; i < len; i++) {
		for (j = 0; j < MAXFILTERS; j++) {
			f = &myrpt->filters[j];
			if (!*f->desc)
				continue;
			f->x0 = f->x1;
			f->x1 = f->x2;
			f->x2 = ((float) buf[i]) / f->gain;
			f->y0 = f->y1;
			f->y1 = f->y2;
			f->y2 = (f->x0 + f->x2) + f->const0 * f->x1 + (f->const1 * f->y0) + (f->const2 * f->y1);
			buf[i] = (short) f->y2;
		}
	}
}

#endif

 /*
  *
  * WARNING:  YOU ARE NOW HEADED INTO ONE GIANT MAZE OF SWITCH STATEMENTS THAT DO MOST OF THE WORK FOR
  *           APP_RPT.  THE MAJORITY OF THIS IS VERY UNDOCUMENTED CODE AND CAN BE VERY HARD TO READ.
  *           IT IS ALSO PROBABLY THE MOST ERROR PRONE PART OF THE CODE, ESPECIALLY THE PORTIONS
  *           RELATED TO THREADED OPERATIONS.
  */

/*
 *  This is the main entry point from the Asterisk call handler to app_rpt when a new "call" is detected and passed off
 *  This code sets up all the necessary variables for the rpt_master threads to take over handling/processing anything
 *  related to this call.  Calls are actually channels that are passed from the pbx application to app_rpt.
 */

void *rpt_call(void *this)
{
	struct rpt *myrpt = (struct rpt *) this;
	int res;
	int stopped, congstarted, dialtimer, lastcidx, aborted, sentpatchconnect;
	struct ast_channel *mychannel, *genchannel;
	struct ast_format_cap *cap;

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		pthread_exit(NULL);
	}
	ast_format_cap_append(cap, ast_format_slin, 0);

	myrpt->mydtmf = 0;
	mychannel = rpt_request_pseudo_chan(cap);

	if (!mychannel) {
		ast_log(LOG_WARNING, "Unable to obtain pseudo channel\n");
		ao2_ref(cap, -1);
		pthread_exit(NULL);
	}
	ast_debug(1, "Requested channel %s\n", ast_channel_name(mychannel));
	rpt_disable_cdr(mychannel);
	ast_answer(mychannel);

	if (rpt_conf_add_speaker(mychannel, myrpt)) {
		ast_hangup(mychannel);
		myrpt->callmode = 0;
		ao2_ref(cap, -1);
		pthread_exit(NULL);
	}
	genchannel = rpt_request_pseudo_chan(cap);
	ao2_ref(cap, -1);
	if (!genchannel) {
		ast_log(LOG_WARNING, "Unable to obtain pseudo channel\n");
		ast_hangup(mychannel);
		pthread_exit(NULL);
	}
	ast_debug(1, "Requested channel %s\n", ast_channel_name(genchannel));
	rpt_disable_cdr(genchannel);
	ast_answer(genchannel);

	/* first put the channel on the conference */
	if (rpt_conf_add_speaker(genchannel, myrpt)) {
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	if (myrpt->p.tonezone && rpt_set_tone_zone(mychannel, myrpt->p.tonezone)) {
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	/* start dialtone if patchquiet is 0. Special patch modes don't send dial tone */
	if (!myrpt->patchquiet && !myrpt->patchexten[0] && rpt_play_dialtone(genchannel) < 0) {
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

	/* Reverse engineering of callmode Mar 2023 NA
	 * XXX These should be converted to enums once we're sure about these, for programmer sanity.
	 *
	 * If 1 or 4, then we wait.
	 * 0 = abort this call
	 * 1 = no auto patch extension
	 * 2 = auto patch extension exists
	 * 3 = ?
	 * 4 = congestion
	 *
	 * We wait up to patchdialtime for digits to be received.
	 * If there's no auto patch extension, then we'll wait for PATCH_DIALPLAN_TIMEOUT ms and then play an announcement.
	 */

	if (myrpt->patchexten[0]) {
		strcpy(myrpt->exten, myrpt->patchexten);
		myrpt->callmode = 2;
	}
	while ((myrpt->callmode == 1) || (myrpt->callmode == 4)) {
		if ((myrpt->patchdialtime) && (myrpt->callmode == 1) && (myrpt->cidx != lastcidx)) {
			dialtimer = 0;
			lastcidx = myrpt->cidx;
		}

		if ((myrpt->patchdialtime) && (dialtimer >= myrpt->patchdialtime)) {
			ast_debug(1, "dialtimer %i > patchdialtime %i\n", dialtimer, myrpt->patchdialtime);
			rpt_mutex_lock(&myrpt->lock);
			aborted = 1;
			myrpt->callmode = 0;
			rpt_mutex_unlock(&myrpt->lock);
			break;
		}

		if ((!myrpt->patchquiet) && (!stopped) && (myrpt->callmode == 1) && (myrpt->cidx > 0)) {
			stopped = 1;
			/* stop dial tone */
			rpt_stop_tone(genchannel);
		}
		if (myrpt->callmode == 1) {
			if (myrpt->calldigittimer > PATCH_DIALPLAN_TIMEOUT) {
				myrpt->callmode = 2;
				break;
			}
			/* bump timer if active */
			if (myrpt->calldigittimer)
				myrpt->calldigittimer += MSWAIT;
		}
		if (myrpt->callmode == 4) {
			if (!congstarted) {
				congstarted = 1;
				/* start congestion tone */
				rpt_play_congestion(genchannel);
			}
		}
		res = ast_safe_sleep(mychannel, MSWAIT);
		if (res < 0) {
			ast_debug(1, "ast_safe_sleep=%i\n", res);
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
	rpt_stop_tone(genchannel);

	/* end if done */
	if (!myrpt->callmode) {
		ast_debug(1, "callmode==0\n");
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		rpt_mutex_lock(&myrpt->lock);
		myrpt->callmode = 0;
		myrpt->macropatch = 0;
		channel_revert(myrpt);
		rpt_mutex_unlock(&myrpt->lock);
		if ((!myrpt->patchquiet) && aborted)
			rpt_telemetry(myrpt, TERM, NULL);
		pthread_exit(NULL);
	}

	if (myrpt->p.ourcallerid && *myrpt->p.ourcallerid) {
		char *name, *loc, *instr;
		instr = ast_strdup(myrpt->p.ourcallerid);
		if (instr) {
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
	if (ast_pbx_start(mychannel) < 0) {
		ast_log(LOG_ERROR, "Unable to start PBX!\n");
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

	if (ast_channel_pbx(mychannel)) {
		if (rpt_call_bridge_setup(myrpt, mychannel, genchannel)) {
			myrpt->callmode = 0;
			rpt_mutex_unlock(&myrpt->lock);
			pthread_exit(NULL);
		}
	} else {
		/* XXX Can this ever happen (since we exit if ast_pbx_start fails)? */
		ast_log(LOG_WARNING, "%s has no PBX?\n", ast_channel_name(mychannel));
	}

	sentpatchconnect = 0;
	while (myrpt->callmode) {
		if ((!ast_channel_pbx(mychannel)) && (myrpt->callmode != 4)) {
			/* If patch is setup for far end disconnect */
			if (myrpt->patchfarenddisconnect || (myrpt->p.duplex < 2)) {
				ast_debug(1, "callmode=%i, patchfarenddisconnect=%i, duplex=%i\n",
					myrpt->callmode, myrpt->patchfarenddisconnect, myrpt->p.duplex);
				myrpt->callmode = 0;
				myrpt->macropatch = 0;
				if (!myrpt->patchquiet) {
					rpt_mutex_unlock(&myrpt->lock);
					rpt_telemetry(myrpt, TERM, NULL);
					rpt_mutex_lock(&myrpt->lock);
				}
			} else {			/* Send congestion until patch is downed by command */
				myrpt->callmode = 4;
				rpt_mutex_unlock(&myrpt->lock);
				/* start congestion tone */
				rpt_play_congestion(genchannel);
				rpt_mutex_lock(&myrpt->lock);
			}
		}
		if (ast_channel_is_bridged(mychannel) && ast_channel_state(mychannel) == AST_STATE_UP)
			if ((!sentpatchconnect) && myrpt->p.patchconnect && ast_channel_is_bridged(mychannel)
				&& (ast_channel_state(mychannel) == AST_STATE_UP)) {
				sentpatchconnect = 1;
				rpt_telemetry(myrpt, PLAYBACK, (char*) myrpt->p.patchconnect);
			}
		if (myrpt->mydtmf) {
			struct ast_frame wf = { AST_FRAME_DTMF, };

			wf.subclass.integer = myrpt->mydtmf;
			if (ast_channel_is_bridged(mychannel) && ast_channel_state(mychannel) == AST_STATE_UP) {
				rpt_mutex_unlock(&myrpt->lock);
				ast_queue_frame(mychannel, &wf);
				ast_senddigit(genchannel, myrpt->mydtmf, 0);
				rpt_mutex_lock(&myrpt->lock);
			}
			myrpt->mydtmf = 0;
		}
		rpt_mutex_unlock(&myrpt->lock);
		usleep(MSWAIT * 1000);
		rpt_mutex_lock(&myrpt->lock);
	}
	ast_debug(1, "exit channel loop\n");
	rpt_mutex_unlock(&myrpt->lock);
	rpt_stop_tone(genchannel);
	if (ast_channel_pbx(mychannel))
		ast_softhangup(mychannel, AST_SOFTHANGUP_DEV);
	ast_hangup(genchannel);
	rpt_mutex_lock(&myrpt->lock);
	myrpt->callmode = 0;
	myrpt->macropatch = 0;
	channel_revert(myrpt);
	rpt_mutex_unlock(&myrpt->lock);

	/* first put the channel on the conference in announce mode */
	if (myrpt->p.duplex == 2 || myrpt->p.duplex == 4) {
		rpt_conf_add_announcer_monitor(myrpt->pchannel, myrpt);
	} else {
		rpt_conf_add_speaker(myrpt->pchannel, myrpt);
	}
	pthread_exit(NULL);
}

/*
* Collect digits one by one until something matches
*/
static int collect_function_digits(struct rpt *myrpt, char *digits, int command_source, struct rpt_link *mylink)
{
	int i, rv;
	char *stringp, *action, *param, *functiondigits;
	char function_table_name[30] = "";
	char workstring[200];

	struct ast_variable *vp;

	ast_debug(7, "digits=%s  source=%d\n", digits, command_source);

	//ast_debug(1, "@@@@ Digits collected: %s, source: %d\n", digits, command_source);

	if (command_source == SOURCE_DPHONE) {
		if (!myrpt->p.dphone_functions)
			return DC_INDETERMINATE;
		ast_copy_string(function_table_name, myrpt->p.dphone_functions, sizeof(function_table_name));
	} else if (command_source == SOURCE_ALT) {
		if (!myrpt->p.alt_functions)
			return DC_INDETERMINATE;
		ast_copy_string(function_table_name, myrpt->p.alt_functions, sizeof(function_table_name));
	} else if (command_source == SOURCE_PHONE) {
		if (!myrpt->p.phone_functions)
			return DC_INDETERMINATE;
		ast_copy_string(function_table_name, myrpt->p.phone_functions, sizeof(function_table_name));
	} else if (command_source == SOURCE_LNK) {
		ast_copy_string(function_table_name, myrpt->p.link_functions, sizeof(function_table_name));
	} else {
		ast_copy_string(function_table_name, myrpt->p.functions, sizeof(function_table_name));
	}
	/* find context for function table in rpt.conf file */
	vp = ast_variable_browse(myrpt->cfg, function_table_name);
	while (vp) {
		if (!strncasecmp(vp->name, digits, strlen(vp->name)))
			break;
		vp = vp->next;
	}
	/* if function context not found */
	if (!vp) {
		int n;

		n = myrpt->longestfunc;
		if (command_source == SOURCE_LNK)
			n = myrpt->link_longestfunc;
		else if (command_source == SOURCE_PHONE)
			n = myrpt->phone_longestfunc;
		else if (command_source == SOURCE_ALT)
			n = myrpt->alt_longestfunc;
		else if (command_source == SOURCE_DPHONE)
			n = myrpt->dphone_longestfunc;

		if (strlen(digits) >= n)
			return DC_ERROR;
		else
			return DC_INDETERMINATE;
	}
	/* Found a match, retrieve value part and parse */
	ast_copy_string(workstring, vp->value, sizeof(workstring));
	stringp = workstring;
	action = strsep(&stringp, ",");
	param = stringp;
	ast_debug(1, "@@@@ action: %s, param = %s\n", action, (param) ? param : "(null)");
	/* Look up the action */
	for (i = 0; i < (sizeof(function_table) / sizeof(struct function_table_tag)); i++) {
		if (!strncasecmp(action, function_table[i].action, strlen(action)))
			break;
	}
	ast_debug(1, "@@@@ table index i = %d\n", i);
	if (i == (sizeof(function_table) / sizeof(struct function_table_tag))) {
		/* Error, action not in table */
		return DC_ERROR;
	}
	if (function_table[i].function == NULL) {
		/* Error, function undefined */
		ast_debug(1, "@@@@ NULL for action: %s\n", action);
		return DC_ERROR;
	}
	functiondigits = digits + strlen(vp->name);
	rv = (*function_table[i].function) (myrpt, param, functiondigits, command_source, mylink);
	ast_debug(7, "rv=%i\n", rv);
	return (rv);
}

static inline void collect_function_digits_post(struct rpt *myrpt, int res, const char *cmd, struct rpt_link *mylink)
{
	switch (res) {
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
		ast_copy_string(myrpt->lastdtmfcommand, cmd, MAXDTMF);
		myrpt->lastdtmfcommand[MAXDTMF - 1] = '\0';
		myrpt->rem_dtmfbuf[0] = 0;
		myrpt->rem_dtmfidx = -1;
		myrpt->rem_dtmf_time = 0;
		break;
	case DC_DOKEY:
		if (mylink) {
			mylink->lastrealrx = 1;
			break;
		}
		/* Fall through */
	case DC_ERROR:
	default:
		myrpt->rem_dtmfbuf[0] = 0;
		myrpt->rem_dtmfidx = -1;
		myrpt->rem_dtmf_time = 0;
		break;
	}
}

static void do_aprstt(struct rpt *myrpt)
{
	char cmd[300] = "";
	char overlay, aprscall[100], fname[100];
	FILE *fp;

	snprintf(cmd, sizeof(cmd) - 1, "A%s", myrpt->dtmfbuf);
	overlay = aprstt_xlat(cmd, aprscall);
	if (overlay) {
		ast_log(LOG_WARNING, "aprstt got string %s call %s overlay %c\n", cmd, aprscall, overlay);
		if (!myrpt->p.aprstt[0])
			ast_copy_string(fname, APRSTT_PIPE, sizeof(fname) - 1);
		else
			snprintf(fname, sizeof(fname) - 1, APRSTT_SUB_PIPE, myrpt->p.aprstt);
		fp = fopen(fname, "w");
		if (!fp) {
			ast_log(LOG_WARNING, "Can not open APRSTT pipe %s: %s\n", fname, strerror(errno));
		} else {
			fprintf(fp, "%s %c\n", aprscall, overlay);
			fclose(fp);
			rpt_telemetry(myrpt, ARB_ALPHA, (void *) aprscall);
		}
	}
}

static int distribute_to_all_links(struct rpt *myrpt, struct rpt_link *mylink, const char *src, const char *dest, char *str, struct ast_frame *wf)
{
	struct rpt_link *l = myrpt->links.next;
	/* see if this is one in list */
	while (l != &myrpt->links) {
		if (l->name[0] == '0') {
			l = l->next;
			continue;
		}
		/* dont send back from where it came */
		if (l == mylink || !strcmp(l->name, mylink->name)) {
			l = l->next;
			continue;
		}
		if (!dest || !strcmp(l->name, dest)) {
			/* send, but not to src */
			if (strcmp(l->name, src)) {
				wf->data.ptr = str;
				if (l->chan) {
					rpt_qwrite(l, wf);
				}
			}
			if (dest) {
				/* if it is, send it and we're done */
				return 1;
			}
		}
		l = l->next;
	}
	return 0;
}

static inline void handle_callmode_1(struct rpt *myrpt, char c)
{
	myrpt->exten[myrpt->cidx++] = c;
	myrpt->exten[myrpt->cidx] = 0;
	/* if this exists */
	if (ast_exists_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
		/* if this really it, end now */
		if (!ast_matchmore_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
			myrpt->callmode = 2;
			if (!myrpt->patchquiet) {
				rpt_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt, PROC, NULL);
				rpt_mutex_lock(&myrpt->lock);
			}
		} else {			/* othewise, reset timer */
			myrpt->calldigittimer = 1;
		}
	}
	/* if can continue, do so */
	if (!ast_canmatch_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
		/* call has failed, inform user */
		myrpt->callmode = 4;
	}
}

static int funcchar_common(struct rpt *myrpt, char c)
{
	if (myrpt->callmode == 1) {
		handle_callmode_1(myrpt, c);
	}
	if ((!myrpt->inpadtest) && myrpt->p.aprstt && (!myrpt->cmdnode[0]) && (c == 'A')) {
		strcpy(myrpt->cmdnode, "aprstt");
		myrpt->dtmfidx = 0;
		myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
		rpt_mutex_unlock(&myrpt->lock);
		time(&myrpt->dtmf_time);
		return 1;
	}
	if ((!myrpt->inpadtest) && (c == myrpt->p.funcchar)) {
		myrpt->rem_dtmfidx = 0;
		myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
		time(&myrpt->rem_dtmf_time);
		rpt_mutex_unlock(&myrpt->lock);
		return 1;
	}
	return 0;
}

static inline void init_text_frame(struct ast_frame *wf)
{
	wf->frametype = AST_FRAME_TEXT;
	wf->subclass.format = ast_format_slin;
	wf->offset = 0;
	wf->mallocd = 0;
	wf->samples = 0;
}

static void handle_link_data(struct rpt *myrpt, struct rpt_link *mylink, char *str)
{
	char tmp[512], tmp1[512], cmd[300] = "", dest[300], src[30], c;
	int i, seq, res, ts, rest;
	struct ast_frame wf;

	init_text_frame(&wf);
	wf.datalen = strlen(str) + 1;
	wf.src = "handle_link_data";
	/* put string in our buffer */
	ast_copy_string(tmp, str, sizeof(tmp) - 1);

	ast_debug(5, "Received text over link: '%s'\n", str);

	if (!strcmp(tmp, DISCSTR)) {
		mylink->disced = 1;
		mylink->retries = mylink->max_retries + 1;
		ast_softhangup(mylink->chan, AST_SOFTHANGUP_DEV);
		return;
	}
	if (!strcmp(tmp, NEWKEYSTR)) {
		if ((!mylink->newkey) || mylink->newkeytimer) {
			mylink->newkeytimer = 0;
			mylink->newkey = 1;
			send_old_newkey(mylink->chan);
		}
		return;
	}
	if (!strcmp(tmp, NEWKEY1STR)) {
		mylink->newkeytimer = 0;
		mylink->newkey = 2;
		return;
	}
	if (!strncmp(tmp, IAXKEYSTR, strlen(IAXKEYSTR))) {
		mylink->iaxkey = 1;
		return;
	}
	if (tmp[0] == 'G') {		/* got GPS data */
		/* re-distribute it to attached nodes */
		distribute_to_all_links(myrpt, mylink, src, NULL, str, &wf);
		return;
	}
	if (tmp[0] == 'L') {
		rpt_mutex_lock(&myrpt->lock);
		strcpy(mylink->linklist, tmp + 2);
		time(&mylink->linklistreceived);
		rpt_mutex_unlock(&myrpt->lock);
		ast_debug(7, "@@@@ node %s recieved node list %s from node %s\n", myrpt->name, tmp, mylink->name);
		return;
	}
	if (tmp[0] == 'M') {
		rest = 0;
		if (sscanf(tmp, "%s %s %s %n", cmd, src, dest, &rest) < 3) {
			ast_log(LOG_WARNING, "Unable to parse message string %s\n", str);
			return;
		}
		if (!rest)
			return;
		if (strlen(tmp + rest) < 2)
			return;
		/* if is from me, ignore */
		if (!strcmp(src, myrpt->name))
			return;
		/* if is for one of my nodes, dont do too much! */
		for (i = 0; i < nrpts; i++) {
			if (!strcmp(dest, rpt_vars[i].name)) {
				ast_verb(3, "Private Text Message for %s From %s: %s\n", rpt_vars[i].name, src, tmp + rest);
				ast_debug(1, "Node %s Got Private Text Message From Node %s: %s\n", rpt_vars[i].name, src, tmp + rest);
				return;
			}
		}
		/* if is for everyone, at least log it */
		if (!strcmp(dest, "0")) {
			ast_verb(3, "Text Message From %s: %s\n", src, tmp + rest);
			ast_debug(1, "Node %s Got Text Message From Node %s: %s\n", myrpt->name, src, tmp + rest);
		}
		distribute_to_all_links(myrpt, mylink, src, NULL, str, &wf);
		return;
	}
	if (tmp[0] == 'T') {
		if (sscanf(tmp, "%s %s %s", cmd, src, dest) != 3) {
			ast_log(LOG_WARNING, "Unable to parse telem string %s\n", str);
			return;
		}
		/* otherwise, send it to all of em */
		distribute_to_all_links(myrpt, mylink, src, NULL, str, &wf);
		/* if is from me, ignore */
		if (!strcmp(src, myrpt->name))
			return;

		/* if is a RANGER node, only allow CONNECTED message that directly involve our node */
		if (ISRANGER(myrpt->name) && (strncasecmp(dest, "CONNECTED,", 10) || (!strstr(dest, myrpt->name))))
			return;

		/* set 'got T message' flag */
		mylink->gott = 1;

		/*  If inbound telemetry from a remote node, wake up from sleep if sleep mode is enabled */
		rpt_mutex_lock(&myrpt->lock);	/* LOCK */
		if (myrpt->p.s[myrpt->p.sysstate_cur].sleepena) {
			myrpt->sleeptimer = myrpt->p.sleeptime;
			if (myrpt->sleep) {
				myrpt->sleep = 0;
			}
		}
		rpt_mutex_unlock(&myrpt->lock);	/* UNLOCK */

		rpt_telemetry(myrpt, VARCMD, dest);
		return;
	}

	if (tmp[0] == 'C') {
		if (sscanf(tmp, "%s %s %s %s", cmd, src, tmp1, dest) != 4) {
			ast_log(LOG_WARNING, "Unable to parse ctcss string %s\n", str);
			return;
		}
		if (!strcmp(myrpt->p.ctgroup, "0"))
			return;
		if (strcasecmp(myrpt->p.ctgroup, tmp1))
			return;
		distribute_to_all_links(myrpt, mylink, src, NULL, str, &wf);
		/* if is from me, ignore */
		if (!strcmp(src, myrpt->name))
			return;
		sprintf(cmd, "TXTONE %.290s", dest);
		if (IS_XPMR(myrpt))
			send_usb_txt(myrpt, cmd);
		return;
	}

	if (tmp[0] == 'K') {
		if (sscanf(tmp, "%s %s %s %d %d", cmd, dest, src, &seq, &ts) != 5) {
			ast_log(LOG_WARNING, "Unable to parse keying string %s\n", str);
			return;
		}
		if (dest[0] == '0') {
			strcpy(dest, myrpt->name);
		}
		/* if not for me, redistribute to all links */
		if (strcmp(dest, myrpt->name)) {
			if (distribute_to_all_links(myrpt, mylink, src, dest, str, &wf)) {
				return;
			}
		}
		/* if not for me, or is broadcast, redistribute to all links */
		if (strcmp(dest, myrpt->name) || dest[0] == '*') {
			distribute_to_all_links(myrpt, mylink, src, NULL, str, &wf);
		}
		/* if not for me, end here */
		if (strcmp(dest, myrpt->name) && (dest[0] != '*'))
			return;
		if (cmd[1] == '?') {
			time_t now;
			int n = 0;

			time(&now);
			if (myrpt->lastkeyedtime) {
				n = (int) (now - myrpt->lastkeyedtime);
			}
			sprintf(tmp1, "K %s %s %d %d", src, myrpt->name, myrpt->keyed, n);
			wf.data.ptr = tmp1;
			wf.datalen = strlen(tmp1) + 1;
			if (mylink->chan)
				rpt_qwrite(mylink, &wf);
			return;
		}
		if (myrpt->topkeystate != 1)
			return;
		rpt_mutex_lock(&myrpt->lock);
		for (i = 0; i < TOPKEYN; i++) {
			if (!strcmp(myrpt->topkey[i].node, src))
				break;
		}
		if (i >= TOPKEYN) {
			for (i = 0; i < TOPKEYN; i++) {
				if (!myrpt->topkey[i].node[0])
					break;
			}
		}
		if (i < TOPKEYN) {
			ast_copy_string(myrpt->topkey[i].node, src, TOPKEYMAXSTR - 1);
			myrpt->topkey[i].timesince = ts;
			myrpt->topkey[i].keyed = seq;
		}
		rpt_mutex_unlock(&myrpt->lock);
		return;
	}
	if (tmp[0] == 'I') {
		if (sscanf(tmp, "%s %s %s", cmd, src, dest) != 3) {
			ast_log(LOG_WARNING, "Unable to parse ident string %s\n", str);
			return;
		}
		mdc1200_notify(myrpt, src, dest);
		strcpy(dest, "*");
	} else {
		if (sscanf(tmp, "%s %s %s %d %c", cmd, dest, src, &seq, &c) != 5) {
			ast_log(LOG_WARNING, "Unable to parse link string %s\n", str);
			return;
		}
		if (strcmp(cmd, "D")) {
			ast_log(LOG_WARNING, "Unable to parse link string %s\n", str);
			return;
		}
	}
	if (dest[0] == '0') {
		strcpy(dest, myrpt->name);
	}

	/* if not for me, redistribute to all links */
	if (strcmp(dest, myrpt->name)) {
		if (distribute_to_all_links(myrpt, mylink, src, dest, str, &wf)) {
			return;
		}
		/* otherwise, send it to all of em */
		distribute_to_all_links(myrpt, mylink, src, NULL, str, &wf);
		return;
	}
	if (myrpt->p.archivedir) {
		donodelog_fmt(myrpt, "DTMF,%s,%c", mylink->name, c);
	}
	c = func_xlat(myrpt, c, &myrpt->p.outxlat);
	if (!c) {
		return;
	}
	rpt_mutex_lock(&myrpt->lock);
	if ((iswebtransceiver(mylink)) ||	/* if a WebTransceiver node */
		(!strcasecmp(ast_channel_tech(mylink->chan)->type, "tlb"))) {	/* or a tlb node */
		if (c == myrpt->p.endchar)
			myrpt->cmdnode[0] = 0;
		else if (myrpt->cmdnode[0]) {
			cmd[0] = 0;
			if (!strcmp(myrpt->cmdnode, "aprstt")) {
				do_aprstt(myrpt);
			}
			rpt_mutex_unlock(&myrpt->lock);
			if (strcmp(myrpt->cmdnode, "aprstt"))
				send_link_dtmf(myrpt, c);
			return;
		}
	}
	if (c == myrpt->p.endchar)
		myrpt->stopgen = 1;
	if (funcchar_common(myrpt, c)) {
		return;
	}
	if (myrpt->rem_dtmfidx < 0) {
		if ((myrpt->callmode == 2) || (myrpt->callmode == 3)) {
			myrpt->mydtmf = c;
		}
		if (myrpt->p.propagate_dtmf)
			do_dtmf_local(myrpt, c);
		if (myrpt->p.propagate_phonedtmf)
			do_dtmf_phone(myrpt, mylink, c);
		rpt_mutex_unlock(&myrpt->lock);
		return;
	} else if (((myrpt->inpadtest) || (c != myrpt->p.endchar)) && (myrpt->rem_dtmfidx >= 0)) {
		time(&myrpt->rem_dtmf_time);
		if (myrpt->rem_dtmfidx < MAXDTMF) {
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx++] = c;
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;

			rpt_mutex_unlock(&myrpt->lock);
			ast_copy_string(cmd, myrpt->rem_dtmfbuf, sizeof(cmd));
			res = collect_function_digits(myrpt, cmd, SOURCE_LNK, mylink);
			rpt_mutex_lock(&myrpt->lock);
			collect_function_digits_post(myrpt, res, cmd, NULL);
		}
	}
	rpt_mutex_unlock(&myrpt->lock);
	return;
}

static inline void cmdnode_helper(struct rpt *myrpt, char *cmd)
{
	cmd[0] = 0;
	if (!strcmp(myrpt->cmdnode, "aprstt")) {
		do_aprstt(myrpt);
	}
	myrpt->cmdnode[0] = 0;
	myrpt->dtmfidx = -1;
	myrpt->dtmfbuf[0] = 0;
}

static void handle_link_phone_dtmf(struct rpt *myrpt, struct rpt_link *mylink, char c)
{

	char cmd[300];
	int res;

	if (myrpt->p.archivedir) {
		donodelog_fmt(myrpt, "DTMF(P),%s,%c", mylink->name, c);
	}
	if (mylink->phonemonitor)
		return;

	rpt_mutex_lock(&myrpt->lock);

	if (mylink->phonemode == 3) {	/*If in simplex dumb phone mode */
		if (c == myrpt->p.endchar) {	/* If end char */
			mylink->lastrealrx = 0;	/* Keying state = off */
			rpt_mutex_unlock(&myrpt->lock);
			return;
		}

		if (c == myrpt->p.funcchar) {	/* If lead-in char */
			mylink->lastrealrx = !mylink->lastrealrx;	/* Toggle keying state */
			rpt_mutex_unlock(&myrpt->lock);
			return;
		}
	} else {
		if (c == myrpt->p.endchar) {
			if (mylink->lastrx && strcasecmp(ast_channel_tech(mylink->chan)->type, "echolink")) {
				mylink->lastrealrx = 0;
				rpt_mutex_unlock(&myrpt->lock);
				return;
			}
			myrpt->stopgen = 1;
			if (myrpt->cmdnode[0]) {
				cmdnode_helper(myrpt, cmd);
				rpt_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt, COMPLETE, NULL);
				return;
			}
#if 0
			if ((myrpt->rem_dtmfidx < 0) && ((myrpt->callmode == 2) || (myrpt->callmode == 3))) {
				myrpt->mydtmf = c;
			}
#endif
		}
	}
	if (myrpt->cmdnode[0] && strcmp(myrpt->cmdnode, "aprstt")) {
		rpt_mutex_unlock(&myrpt->lock);
		send_link_dtmf(myrpt, c);
		return;
	}
	if (funcchar_common(myrpt, c)) {
		return;
	}
	if (((myrpt->inpadtest) || (c != myrpt->p.endchar)) && (myrpt->rem_dtmfidx >= 0)) {
		time(&myrpt->rem_dtmf_time);
		if (myrpt->rem_dtmfidx < MAXDTMF) {
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx++] = c;
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;

			rpt_mutex_unlock(&myrpt->lock);
			ast_copy_string(cmd, myrpt->rem_dtmfbuf, sizeof(cmd));
			switch (mylink->phonemode) {
			case 1:
				res = collect_function_digits(myrpt, cmd, SOURCE_PHONE, mylink);
				break;
			case 2:
				res = collect_function_digits(myrpt, cmd, SOURCE_DPHONE, mylink);
				break;
			case 4:
				res = collect_function_digits(myrpt, cmd, SOURCE_ALT, mylink);
				break;
			default:
				res = collect_function_digits(myrpt, cmd, SOURCE_LNK, mylink);
				break;
			}

			rpt_mutex_lock(&myrpt->lock);
			collect_function_digits_post(myrpt, res, cmd, mylink);
		}

	} else if (myrpt->p.propagate_phonedtmf)
		do_dtmf_local(myrpt, c);
	rpt_mutex_unlock(&myrpt->lock);
	return;
}

static int handle_remote_dtmf_digit(struct rpt *myrpt, char c, char *keyed, int phonemode)
{
	time_t now;
	int ret, res = 0, src;

	ast_debug(7, "c=%c  phonemode=%i  dtmfidx=%i\n", c, phonemode, myrpt->dtmfidx);

	time(&myrpt->last_activity_time);
	/* Stop scan mode if in scan mode */
	if (myrpt->hfscanmode) {
		stop_scan(myrpt);
		return 0;
	}

	time(&now);
	/* if timed-out */
	if ((myrpt->dtmf_time_rem + DTMF_TIMEOUT) < now) {
		myrpt->dtmfidx = -1;
		myrpt->dtmfbuf[0] = 0;
		myrpt->dtmf_time_rem = 0;
	}
	/* if decode not active */
	if (myrpt->dtmfidx == -1) {
		/* if not lead-in digit, dont worry */
		if (c != myrpt->p.funcchar) {
			if (!myrpt->p.propagate_dtmf) {
				rpt_mutex_lock(&myrpt->lock);
				do_dtmf_local(myrpt, c);
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
	if (myrpt->dtmfidx >= MAXDTMF) {
		myrpt->dtmfidx = 0;
		myrpt->dtmfbuf[0] = 0;
		myrpt->dtmf_time_rem = now;
	}
	if (c == myrpt->p.funcchar) {
		/* if star at beginning, or 2 together, erase buffer */
		if ((myrpt->dtmfidx < 1) || (myrpt->dtmfbuf[myrpt->dtmfidx - 1] == myrpt->p.funcchar)) {
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
	if (phonemode == 2)
		src = SOURCE_DPHONE;
	else if (phonemode)
		src = SOURCE_PHONE;
	else if (phonemode == 4)
		src = SOURCE_ALT;
	ret = collect_function_digits(myrpt, myrpt->dtmfbuf, src, NULL);

	switch (ret) {

	case DC_INDETERMINATE:
		res = 0;
		break;

	case DC_DOKEY:
		if (keyed)
			*keyed = 1;
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
		ast_copy_string(myrpt->lastdtmfcommand, myrpt->dtmfbuf, MAXDTMF);
		myrpt->lastdtmfcommand[MAXDTMF - 1] = '\0';
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
	char tmp[300], cmd[300], dest[300], src[300], c;
	int seq, res;

	/* put string in our buffer */
	ast_copy_string(tmp, str, sizeof(tmp));
	if (!strcmp(tmp, DISCSTR))
		return 0;
	if (!strcmp(tmp, NEWKEYSTR)) {
		if (!myrpt->newkey) {
			send_old_newkey(myrpt->rxchannel);
			myrpt->newkey = 1;
		}
		return 0;
	}
	if (!strcmp(tmp, NEWKEY1STR)) {
		myrpt->newkey = 2;
		return 0;
	}
	if (!strncmp(tmp, IAXKEYSTR, strlen(IAXKEYSTR))) {
		myrpt->iaxkey = 1;
		return 0;
	}

	if (tmp[0] == 'T')
		return 0;

#ifndef	DO_NOT_NOTIFY_MDC1200_ON_REMOTE_BASES
	if (tmp[0] == 'I') {
		if (sscanf(tmp, "%s %s %s", cmd, src, dest) != 3) {
			ast_log(LOG_WARNING, "Unable to parse ident string %s\n", str);
			return 0;
		}
		mdc1200_notify(myrpt, src, dest);
		return 0;
	}
#endif
	if (tmp[0] == 'L')
		return 0;
	if (sscanf(tmp, "%s %s %s %d %c", cmd, dest, src, &seq, &c) != 5) {
		ast_log(LOG_WARNING, "Unable to parse link string %s\n", str);
		return 0;
	}
	if (strcmp(cmd, "D")) {
		ast_log(LOG_WARNING, "Unable to parse link string %s\n", str);
		return 0;
	}
	/* if not for me, ignore */
	if (strcmp(dest, myrpt->name))
		return 0;
	if (myrpt->p.archivedir) {
		donodelog_fmt(myrpt, "DTMF,%c", c);
	}
	c = func_xlat(myrpt, c, &myrpt->p.outxlat);
	if (!c)
		return (0);
	res = handle_remote_dtmf_digit(myrpt, c, NULL, 0);
	if (res != 1)
		return res;
	if ((!strcmp(myrpt->remoterig, REMOTE_RIG_TM271)) || (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD)))
		rpt_telemetry(myrpt, REMCOMPLETE, NULL);
	else
		rpt_telemetry(myrpt, COMPLETE, NULL);
	return 0;
}

static int handle_remote_phone_dtmf(struct rpt *myrpt, char c, char *restrict keyed, int phonemode)
{
	int res;

	if (phonemode == 3) {		/* simplex phonemode, funcchar key/unkey toggle */
		if (keyed && *keyed && ((c == myrpt->p.funcchar) || (c == myrpt->p.endchar))) {
			*keyed = 0;			/* UNKEY */
			return 0;
		} else if (keyed && !*keyed && (c == myrpt->p.funcchar)) {
			*keyed = 1;			/* KEY */
			return 0;
		}
	} else {					/* endchar unkey */
		if (keyed && *keyed && (c == myrpt->p.endchar)) {
			*keyed = 0;
			return DC_INDETERMINATE;
		}
	}
	if (myrpt->p.archivedir) {
		donodelog_fmt(myrpt, "DTMF(P),%c", c);
	}
	res = handle_remote_dtmf_digit(myrpt, c, keyed, phonemode);
	if (res != 1)
		return res;
	if ((!strcmp(myrpt->remoterig, REMOTE_RIG_TM271)) || (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD)))
		rpt_telemetry(myrpt, REMCOMPLETE, NULL);
	else
		rpt_telemetry(myrpt, COMPLETE, NULL);
	return 0;
}

static int attempt_reconnect(struct rpt *myrpt, struct rpt_link *l)
{
	char *s, *s1, *tele;
	char tmp[300], deststr[325] = "";
	char sx[320], *sy;
	struct ast_frame *f1;
	struct ast_format_cap *cap;

	if (node_lookup(myrpt, l->name, tmp, sizeof(tmp) - 1, 1)) {
		ast_log(LOG_WARNING, "attempt_reconnect: cannot find node %s\n", l->name);
		return -1;
	}
	/* cannot apply to echolink */
	if (!strncasecmp(tmp, "echolink", 8))
		return 0;
	/* cannot apply to tlb */
	if (!strncasecmp(tmp, "tlb", 3))
		return 0;
	rpt_mutex_lock(&myrpt->lock);
	/* remove from queue */
	rpt_link_remove(myrpt, l);
	rpt_mutex_unlock(&myrpt->lock);
	s = tmp;
	s1 = strsep(&s, ",");
	if (!strchr(s1, ':') && strchr(s1, '/') && strncasecmp(s1, "local/", 6)) {
		sy = strchr(s1, '/');
		*sy = 0;
		sprintf(sx, "%s:4569/%s", s1, sy + 1);
		s1 = sx;
	}
	strsep(&s, ",");
	snprintf(deststr, sizeof(deststr), "IAX2/%s", s1);
	tele = strchr(deststr, '/');
	if (!tele) {
		ast_log(LOG_WARNING, "attempt_reconnect: Dial number (%s) must be in format tech/number\n", deststr);
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
	ao2_ref(cap, -1);
	l->linkmode = 0;
	l->lastrx1 = 0;
	l->lastrealrx = 0;
	l->rxlingertimer = ((l->iaxkey) ? RX_LINGER_TIME_IAXKEY : RX_LINGER_TIME);
	l->newkeytimer = NEWKEYTIME;
	l->newkey = 2;
	while ((f1 = AST_LIST_REMOVE_HEAD(&l->textq, frame_list)))
		ast_frfree(f1);
	if (l->chan) {
		rpt_make_call(l->chan, tele, 999, deststr, "(Remote Rx)", "attempt_reconnect", myrpt->name);
	} else {
		ast_verb(3, "Unable to place call to %s/%s\n", deststr, tele);
		return -1;
	}
	rpt_mutex_lock(&myrpt->lock);
	/* put back in queue */
	rpt_link_add(myrpt, l);
	rpt_mutex_unlock(&myrpt->lock);
	ast_log(LOG_NOTICE, "Reconnect Attempt to %s in progress\n", l->name);
	return 0;
}

/* 0 return=continue, 1 return = break, -1 return = error */
static void local_dtmf_helper(struct rpt *myrpt, char c_in)
{
	int res;
	char cmd[MAXDTMF + 1] = "", c, tone[10];

	c = c_in & 0x7f;

	sprintf(tone, "%c", c);
	rpt_manager_trigger(myrpt, "DTMF", tone);

	if (myrpt->p.archivedir) {
		donodelog_fmt(myrpt, "DTMF,MAIN,%c", c);
	}
	if (c == myrpt->p.endchar) {
		/* if in simple mode, kill autopatch */
		if (myrpt->p.simple && myrpt->callmode) {
			ast_log(LOG_WARNING, "simple mode autopatch kill\n");
			rpt_mutex_lock(&myrpt->lock);
			myrpt->callmode = 0;
			myrpt->macropatch = 0;
			channel_revert(myrpt);
			rpt_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt, TERM, NULL);
			return;
		}
		rpt_mutex_lock(&myrpt->lock);
		myrpt->stopgen = 1;
		if (myrpt->cmdnode[0]) {
			cmdnode_helper(myrpt, cmd);
			rpt_mutex_unlock(&myrpt->lock);
			if (!cmd[0])
				rpt_telemetry(myrpt, COMPLETE, NULL);
			return;
		} else if (!myrpt->inpadtest) {
			rpt_mutex_unlock(&myrpt->lock);
			if (myrpt->p.propagate_phonedtmf)
				do_dtmf_phone(myrpt, NULL, c);
			if ((myrpt->dtmfidx == -1) && ((myrpt->callmode == 2) || (myrpt->callmode == 3))) {
				myrpt->mydtmf = c;
			}
			return;
		} else {
			rpt_mutex_unlock(&myrpt->lock);
		}
	}
	rpt_mutex_lock(&myrpt->lock);
	if (myrpt->cmdnode[0] && strcmp(myrpt->cmdnode, "aprstt")) {
		rpt_mutex_unlock(&myrpt->lock);
		send_link_dtmf(myrpt, c);
		return;
	}
	if (!myrpt->p.simple) {
		if ((!myrpt->inpadtest) && myrpt->p.aprstt && (!myrpt->cmdnode[0]) && (c == 'A')) {
			strcpy(myrpt->cmdnode, "aprstt");
			myrpt->dtmfidx = 0;
			myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			time(&myrpt->dtmf_time);
			return;
		}
		if ((!myrpt->inpadtest) && (c == myrpt->p.funcchar)) {
			if (myrpt->p.dopfxtone && (myrpt->dtmfidx == -1))
				rpt_telemetry(myrpt, PFXTONE, NULL);
			myrpt->dtmfidx = 0;
			myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			time(&myrpt->dtmf_time);
			return;
		} else if (((myrpt->inpadtest) || (c != myrpt->p.endchar)) && (myrpt->dtmfidx >= 0)) {
			time(&myrpt->dtmf_time);
			cancel_pfxtone(myrpt);

			if (myrpt->dtmfidx < MAXDTMF) {
				int src;

				myrpt->dtmfbuf[myrpt->dtmfidx++] = c;
				myrpt->dtmfbuf[myrpt->dtmfidx] = 0;

				ast_copy_string(cmd, myrpt->dtmfbuf, sizeof(cmd));

				rpt_mutex_unlock(&myrpt->lock);
				if (myrpt->cmdnode[0])
					return;
				src = SOURCE_RPT;
				if (c_in & 0x80)
					src = SOURCE_ALT;
				res = collect_function_digits(myrpt, cmd, src, NULL);
				rpt_mutex_lock(&myrpt->lock);
				switch (res) {
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
					ast_copy_string(myrpt->lastdtmfcommand, cmd, MAXDTMF);
					myrpt->lastdtmfcommand[MAXDTMF - 1] = '\0';
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
				if (res != DC_INDETERMINATE) {
					rpt_mutex_unlock(&myrpt->lock);
					return;
				}
			}
		}
	} else {					/* if simple */
		if ((!myrpt->callmode) && (c == myrpt->p.funcchar)) {
			myrpt->callmode = 1;
			myrpt->patchnoct = 0;
			myrpt->patchquiet = 0;
			myrpt->patchfarenddisconnect = 0;
			myrpt->patchdialtime = 0;
			ast_copy_string(myrpt->patchcontext, myrpt->p.ourcontext, MAXPATCHCONTEXT - 1);
			myrpt->cidx = 0;
			myrpt->exten[myrpt->cidx] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			ast_pthread_create_detached(&myrpt->rpt_call_thread, NULL, rpt_call, (void *) myrpt);
			return;
		}
	}
	if (myrpt->callmode == 1) {
		myrpt->exten[myrpt->cidx++] = c;
		myrpt->exten[myrpt->cidx] = 0;
		/* if this exists */
		if (ast_exists_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
			/* if this really it, end now */
			if (!ast_matchmore_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
				myrpt->callmode = 2;
				rpt_mutex_unlock(&myrpt->lock);
				if (!myrpt->patchquiet)
					rpt_telemetry(myrpt, PROC, NULL);
				return;
			} else {			/* othewise, reset timer */
				myrpt->calldigittimer = 1;
			}
		}
		/* if can continue, do so */
		if (!ast_canmatch_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
			/* call has failed, inform user */
			myrpt->callmode = 4;
		}
		rpt_mutex_unlock(&myrpt->lock);
		return;
	}
	if (((myrpt->callmode == 2) || (myrpt->callmode == 3)) && (myrpt->dtmfidx < 0)) {
		myrpt->mydtmf = c;
	}
	rpt_mutex_unlock(&myrpt->lock);
	if ((myrpt->dtmfidx < 0) && myrpt->p.propagate_phonedtmf)
		do_dtmf_phone(myrpt, NULL, c);
	return;
}

/* place an ID event in the telemetry queue */

static void queue_id(struct rpt *myrpt)
{
	if (myrpt->p.idtime) {		/* ID time must be non-zero */
		myrpt->mustid = myrpt->tailid = 0;
		myrpt->idtimer = myrpt->p.idtime;	/* Reset our ID timer */
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telemetry(myrpt, ID, NULL);
		rpt_mutex_lock(&myrpt->lock);
	}
}

/* Scheduler */
/* must be called locked */

static void do_scheduler(struct rpt *myrpt)
{
	int i, res;

	struct ast_tm tmnow;
	struct ast_variable *skedlist;
	char *strs[5], *vp, *val, value[100];

	memcpy(&myrpt->lasttv, &myrpt->curtv, sizeof(struct timeval));

	if ((res = gettimeofday(&myrpt->curtv, NULL)) < 0)
		ast_debug(1, "Scheduler gettime of day returned: %s\n", strerror(res));

	/* Try to get close to a 1 second resolution */

	if (myrpt->lasttv.tv_sec == myrpt->curtv.tv_sec)
		return;

	/* Service the sleep timer */
	if (myrpt->p.s[myrpt->p.sysstate_cur].sleepena) {	/* If sleep mode enabled */
		if (myrpt->sleeptimer)
			myrpt->sleeptimer--;
		else {
			if (!myrpt->sleep)
				myrpt->sleep = 1;	/* ZZZZZZ */
		}
	}
	/* Service activity timer */
	if (myrpt->p.lnkactmacro && myrpt->p.lnkacttime && myrpt->p.lnkactenable && myrpt->linkactivityflag) {
		myrpt->linkactivitytimer++;
		/* 30 second warn */
		if ((myrpt->p.lnkacttime - myrpt->linkactivitytimer == 30) && myrpt->p.lnkacttimerwarn) {
			ast_debug(5, "Warning user of activity timeout\n");
			rpt_telemetry(myrpt, LOCALPLAY, (char*) myrpt->p.lnkacttimerwarn);
		}
		if (myrpt->linkactivitytimer >= myrpt->p.lnkacttime) {
			/* Execute lnkactmacro */
			if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(myrpt->p.lnkactmacro)) {
				ast_log(LOG_WARNING, "Link Activity timer could not execute macro %s: Macro buffer full\n", myrpt->p.lnkactmacro);
			} else {
				ast_debug(5, "Executing link activity timer macro %s\n", myrpt->p.lnkactmacro);
				myrpt->macrotimer = MACROTIME;
				strncat(myrpt->macrobuf, myrpt->p.lnkactmacro, MAXMACRO - 1);
			}
			myrpt->linkactivitytimer = 0;
			myrpt->linkactivityflag = 0;
		}
	}
	/* Service repeater inactivity timer */
	if (myrpt->p.rptinacttime && myrpt->rptinactwaskeyedflag) {
		if (myrpt->rptinacttimer < myrpt->p.rptinacttime)
			myrpt->rptinacttimer++;
		else {
			myrpt->rptinacttimer = 0;
			myrpt->rptinactwaskeyedflag = 0;
			if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(myrpt->p.rptinactmacro)) {
				ast_log(LOG_WARNING, "Rpt inactivity timer could not execute macro %s: Macro buffer full\n", myrpt->p.rptinactmacro);
			} else {
				ast_debug(5, "Executing rpt inactivity timer macro %s\n", myrpt->p.rptinactmacro);
				myrpt->macrotimer = MACROTIME;
				strncat(myrpt->macrobuf, myrpt->p.rptinactmacro, MAXMACRO - 1);
			}
		}
	}

	rpt_localtime(&myrpt->curtv.tv_sec, &tmnow, NULL);

	/* If midnight, then reset all daily statistics */

	if ((tmnow.tm_hour == 0) && (tmnow.tm_min == 0) && (tmnow.tm_sec == 0)) {
		myrpt->dailykeyups = 0;
		myrpt->dailytxtime = 0;
		myrpt->dailykerchunks = 0;
		myrpt->dailyexecdcommands = 0;
	}

	if (tmnow.tm_sec != 0)
		return;

	/* Code below only executes once per minute */

	/* Don't schedule if remote */

	if (myrpt->remote)
		return;

	/* Don't schedule if disabled */

	if (myrpt->p.s[myrpt->p.sysstate_cur].schedulerdisable) {
		ast_debug(7, "Scheduler disabled\n");
		return;
	}

	if (ast_strlen_zero(myrpt->p.skedstanzaname)) {	/* No stanza means we do nothing */
		ast_debug(7, "No stanza for scheduler in rpt.conf\n");
		return;
	}

	/* get pointer to linked list of scheduler entries */
	skedlist = ast_variable_browse(myrpt->cfg, myrpt->p.skedstanzaname);

	ast_debug(7, "Time now: %02d:%02d %02d %02d %02d\n", tmnow.tm_hour, tmnow.tm_min, tmnow.tm_mday, tmnow.tm_mon + 1, tmnow.tm_wday);
	/* walk the list */
	for (; skedlist; skedlist = skedlist->next) {
		ast_debug(7, "Scheduler entry %s = %s being considered\n", skedlist->name, skedlist->value);
		ast_copy_string(value, skedlist->value, sizeof(value));
		/* point to the substrings for minute, hour, dom, month, and dow */
		for (i = 0, vp = value; i < 5; i++) {
			if (!*vp)
				break;
			while ((*vp == ' ') || (*vp == 0x09))	/* get rid of any leading white space */
				vp++;
			strs[i] = vp;		/* save pointer to beginning of substring */
			while ((*vp != ' ') && (*vp != 0x09) && (*vp != 0))	/* skip over substring */
				vp++;
			if (*vp)
				*vp++ = 0;		/* mark end of substring */
		}
		ast_debug(7, "i = %d, min = %s, hour = %s, mday=%s, mon=%s, wday=%s\n", i, strs[0], strs[1], strs[2], strs[3], strs[4]);
		if (i == 5) {
			if ((*strs[0] != '*') && (atoi(strs[0]) != tmnow.tm_min))
				continue;
			if ((*strs[1] != '*') && (atoi(strs[1]) != tmnow.tm_hour))
				continue;
			if ((*strs[2] != '*') && (atoi(strs[2]) != tmnow.tm_mday))
				continue;
			if ((*strs[3] != '*') && (atoi(strs[3]) != tmnow.tm_mon + 1))
				continue;
			if (atoi(strs[4]) == 7)
				strs[4] = "0";
			if ((*strs[4] != '*') && (atoi(strs[4]) != tmnow.tm_wday))
				continue;
			ast_debug(1, "Executing scheduler entry %s = %s\n", skedlist->name, skedlist->value);
			if (atoi(skedlist->name) == 0)
				return;			/* Zero is reserved for the startup macro */
			val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.macro, skedlist->name);
			if (!val) {
				ast_log(LOG_WARNING, "Scheduler could not find macro %s\n", skedlist->name);
				return;			/* Macro not found */
			}
			if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(val)) {
				ast_log(LOG_WARNING, "Scheduler could not execute macro %s: Macro buffer full\n", skedlist->name);
				return;			/* Macro buffer full */
			}
			myrpt->macrotimer = MACROTIME;
			strncat(myrpt->macrobuf, val, MAXMACRO - 1);
		} else {
			ast_log(LOG_WARNING, "Malformed scheduler entry in rpt.conf: %s = %s\n", skedlist->name, skedlist->value);
		}
	}
}

#define load_rpt_vars_by_rpt(myrpt, force) _load_rpt_vars_by_rpt(myrpt, force);

static void _load_rpt_vars_by_rpt(struct rpt *myrpt, int force)
{
	int i;

	/* find our index, and load the vars initially */
	for (i = 0; i < nrpts; i++) {
		if (&rpt_vars[i] == myrpt) {
			if (rpt_vars[i].cfg && !force) {
				/* On startup, previously load_rpt_vars was getting called twice for
				 * every node. Avoid this by not if we already loaded the config on startup. */
				ast_debug(1, "Already have a config for %s, skipping\n", rpt_vars[i].name);
				break;
			}
			load_rpt_vars(i, 0);
			break;
		}
	}
}

void rpt_links_init(struct rpt_link *l)
{
	l->next = l;
	l->prev = l;
}

#define rpt_hangup_rx_tx(myrpt) \
	rpt_hangup(myrpt, RPT_RXCHAN); \
	if (myrpt->txchannel) { \
		rpt_hangup(myrpt, RPT_TXCHAN); \
	}

#define IS_DAHDI_CHAN(c) (!strcasecmp(ast_channel_tech(c)->type, "DAHDI"))
#define IS_DAHDI_CHAN_NAME(s) (!strncasecmp(s, "DAHDI", 5))

static int rpt_setup_channels(struct rpt *myrpt, struct ast_format_cap *cap)
{
	int res;

	if (rpt_request(myrpt, cap, RPT_RXCHAN)) {
		return -1;
	}

	if (myrpt->txchanname) {
		if (rpt_request(myrpt, cap, RPT_TXCHAN)) {
			rpt_hangup(myrpt, RPT_RXCHAN);
			return -1;
		}
	} else {
		myrpt->txchannel = myrpt->rxchannel;
		myrpt->dahditxchannel = IS_DAHDI_CHAN_NAME(myrpt->rxchanname) && !IS_PSEUDO_NAME(myrpt->rxchanname) ? myrpt->txchannel : NULL;
	}
	if (!IS_PSEUDO(myrpt->txchannel)) {
		ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_KEY);
		ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_UNKEY);
	}

	if (rpt_request_pseudo(myrpt, cap, RPT_PCHAN)) {
		rpt_hangup_rx_tx(myrpt);
		return -1;
	}

	if (!myrpt->dahditxchannel) {
		if (rpt_request_pseudo(myrpt, cap, RPT_DAHDITXCHAN)) {
			rpt_hangup_rx_tx(myrpt);
			rpt_hangup(myrpt, RPT_PCHAN);
			return -1;
		}
	}

	if (rpt_request_pseudo(myrpt, cap, RPT_MONCHAN)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_DAHDITXCHAN);
		return -1;
	}

	/* make a conference for the tx */
	if (rpt_conf_create(myrpt->dahditxchannel, myrpt, RPT_TXCONF, RPT_CONF_CONF | RPT_CONF_LISTENER)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		return -1;
	}

	if (myrpt->p.duplex == 2 || myrpt->p.duplex == 4) {
		res = rpt_conf_create(myrpt->pchannel, myrpt, RPT_CONF, RPT_CONF_CONFANNMON);
	} else {
		res = rpt_conf_create(myrpt->pchannel, myrpt, RPT_CONF, RPT_CONF_CONF | RPT_CONF_LISTENER | RPT_CONF_TALKER);
	}
	if (res) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		return -1;
	}

	if (rpt_mon_setup(myrpt)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		return -1;
	}

	if (rpt_request_pseudo(myrpt, cap, RPT_PARROTCHAN)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		return -1;
	}

	/* Telemetry Channel Resources */
	if (rpt_request_pseudo(myrpt, cap, RPT_TELECHAN)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		rpt_hangup(myrpt, RPT_PARROTCHAN);
		return -1;
	}

	/* make a conference for the voice/tone telemetry */
	if (rpt_conf_create(myrpt->telechannel, myrpt, RPT_TELECONF, RPT_CONF_CONF | RPT_CONF_TALKER | RPT_CONF_LISTENER)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		rpt_hangup(myrpt, RPT_PARROTCHAN);
		rpt_hangup(myrpt, RPT_TELECHAN);
		return -1;
	}

	/* make a channel to connect between the telemetry conference process
	   and the main tx audio conference. */
	if (rpt_request_pseudo(myrpt, cap, RPT_BTELECHAN)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		rpt_hangup(myrpt, RPT_PARROTCHAN);
		rpt_hangup(myrpt, RPT_TELECHAN);
		return -1;
	}

	/* make a conference linked to the main tx conference */
	if (rpt_tx_conf_add_speaker(myrpt->btelechannel, myrpt)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		rpt_hangup(myrpt, RPT_PARROTCHAN);
		rpt_hangup(myrpt, RPT_TELECHAN);
		rpt_hangup(myrpt, RPT_BTELECHAN);
		return -1;
	}

	if (rpt_request_pseudo(myrpt, cap, RPT_VOXCHAN)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		rpt_hangup(myrpt, RPT_PARROTCHAN);
		rpt_hangup(myrpt, RPT_TELECHAN);
		rpt_hangup(myrpt, RPT_BTELECHAN);
		return -1;
	}

	if (rpt_request_pseudo(myrpt, cap, RPT_TXPCHAN)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		rpt_hangup(myrpt, RPT_PARROTCHAN);
		rpt_hangup(myrpt, RPT_TELECHAN);
		rpt_hangup(myrpt, RPT_BTELECHAN);
		rpt_hangup(myrpt, RPT_VOXCHAN);
		return -1;
	}

	/* make a conference for the tx */
	if (rpt_conf_add(myrpt->txpchannel, myrpt, RPT_TXCONF, RPT_CONF_CONF | RPT_CONF_TALKER)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		rpt_hangup(myrpt, RPT_PARROTCHAN);
		rpt_hangup(myrpt, RPT_TELECHAN);
		rpt_hangup(myrpt, RPT_BTELECHAN);
		rpt_hangup(myrpt, RPT_VOXCHAN);
		rpt_hangup(myrpt, RPT_TXPCHAN);
		return -1;
	}

	return 0;
}

/*! \brief Permanently disable a repeater */
static int disable_rpt(struct rpt *myrpt)
{
	int n;
	/* setting rpt_vars[n].deleted = 1 is a slight hack that prevents continual thread restarts.
	 * This thread cannot successfully be resurrected, so don't even THINK about trying!
	 * (Maybe add a new var for this?) */
	for (n = 0; n < nrpts; n++) {
		if (!strcmp(myrpt->name, rpt_vars[n].name)) {
			rpt_vars[n].deleted = 1;
			ast_log(LOG_WARNING, "Disabled broken repeater %s\n", myrpt->name);
			return 0;
		}
	}
	ast_log(LOG_ERROR, "Couldn't find repeater %s\n", myrpt->name);
	return -1;
}

static inline void dump_rpt(struct rpt *myrpt, const int lasttx, const int lastexttx, const int elap, const int totx)
{
	struct rpt_link *zl;
	struct rpt_tele *zt;

	ast_debug(2, "********** Variable Dump Start (app_rpt) **********\n");
	ast_debug(2, "myrpt->remrx = %d\n", myrpt->remrx);
	ast_debug(2, "lasttx = %d\n", lasttx);
	ast_debug(2, "lastexttx = %d\n", lastexttx);
	ast_debug(2, "elap = %d\n", elap);
	ast_debug(2, "totx = %d\n", totx);

	ast_debug(2, "myrpt->keyed = %d\n", myrpt->keyed);
	ast_debug(2, "myrpt->localtx = %d\n", myrpt->localtx);
	ast_debug(2, "myrpt->callmode = %d\n", myrpt->callmode);
	ast_debug(2, "myrpt->mustid = %d\n", myrpt->mustid);
	ast_debug(2, "myrpt->tounkeyed = %d\n", myrpt->tounkeyed);
	ast_debug(2, "myrpt->tonotify = %d\n", myrpt->tonotify);
	ast_debug(2, "myrpt->retxtimer = %ld\n", myrpt->retxtimer);
	ast_debug(2, "myrpt->totimer = %d\n", myrpt->totimer);
	ast_debug(2, "myrpt->tailtimer = %d\n", myrpt->tailtimer);
	ast_debug(2, "myrpt->tailevent = %d\n", myrpt->tailevent);
	ast_debug(2, "myrpt->linkactivitytimer = %d\n", myrpt->linkactivitytimer);
	ast_debug(2, "myrpt->linkactivityflag = %d\n", (int) myrpt->linkactivityflag);
	ast_debug(2, "myrpt->rptinacttimer = %d\n", myrpt->rptinacttimer);
	ast_debug(2, "myrpt->rptinactwaskeyedflag = %d\n", (int) myrpt->rptinactwaskeyedflag);
	ast_debug(2, "myrpt->p.s[myrpt->p.sysstate_cur].sleepena = %d\n", myrpt->p.s[myrpt->p.sysstate_cur].sleepena);
	ast_debug(2, "myrpt->sleeptimer = %d\n", (int) myrpt->sleeptimer);
	ast_debug(2, "myrpt->sleep = %d\n", (int) myrpt->sleep);
	ast_debug(2, "myrpt->sleepreq = %d\n", (int) myrpt->sleepreq);
	ast_debug(2, "myrpt->p.parrotmode = %d\n", (int) myrpt->p.parrotmode);
	ast_debug(2, "myrpt->parrotonce = %d\n", (int) myrpt->parrotonce);

	zl = myrpt->links.next;
	while (zl != &myrpt->links) {
		ast_debug(2, "*** Link Name: %s ***\n", zl->name);
		ast_debug(2, "        link->lasttx %d\n", zl->lasttx);
		ast_debug(2, "        link->lastrx %d\n", zl->lastrx);
		ast_debug(2, "        link->connected %d\n", zl->connected);
		ast_debug(2, "        link->hasconnected %d\n", zl->hasconnected);
		ast_debug(2, "        link->outbound %d\n", zl->outbound);
		ast_debug(2, "        link->disced %d\n", zl->disced);
		ast_debug(2, "        link->killme %d\n", zl->killme);
		ast_debug(2, "        link->disctime %ld\n", zl->disctime);
		ast_debug(2, "        link->retrytimer %ld\n", zl->retrytimer);
		ast_debug(2, "        link->retries = %d\n", zl->retries);
		ast_debug(2, "        link->reconnects = %d\n", zl->reconnects);
		ast_debug(2, "        link->newkey = %d\n", zl->newkey);
		zl = zl->next;
	}

	zt = myrpt->tele.next;
	if (zt != &myrpt->tele) {
		ast_debug(2, "*** Telemetry Queue ***\n");
	}
	while (zt != &myrpt->tele) {
		ast_debug(2, "        Telemetry mode: %d\n", zt->mode);
		zt = zt->next;
	}
	ast_debug(2, "******* Variable Dump End (app_rpt) *******\n");
}

/*!
 * \brief Check if any channels on a node are pending hangup
 * \param myrpt
 * \retval 0 if no channels have hung up, -1 if some channel has hung up
 */
static inline int rpt_any_hangups(struct rpt *myrpt)
{
	if (ast_check_hangup(myrpt->rxchannel)) {
		return -1;
	}
	if (ast_check_hangup(myrpt->txchannel)) {
		return -1;
	}
	if (ast_check_hangup(myrpt->pchannel)) {
		return -1;
	}
	if (ast_check_hangup(myrpt->monchannel)) {
		return -1;
	}
	if (myrpt->parrotchannel && ast_check_hangup(myrpt->parrotchannel)) {
		return -1;
	}
	if (myrpt->voxchannel && ast_check_hangup(myrpt->voxchannel)) {
		return -1;
	}
	if (ast_check_hangup(myrpt->txpchannel)) {
		return -1;
	}
	if (myrpt->dahditxchannel && ast_check_hangup(myrpt->dahditxchannel)) {
		return -1;
	}
	return 0;
}

static inline void log_keyed(struct rpt *myrpt)
{
	char mydate[100], myfname[512];
	time_t myt;

	if (myrpt->monstream) {
		ast_closestream(myrpt->monstream);
	}
	myrpt->monstream = 0;
	if (myrpt->p.archivedir) {
		long blocksleft;

		time(&myt);
		strftime(mydate, sizeof(mydate) - 1, "%Y%m%d%H%M%S", localtime(&myt));
		sprintf(myfname, "%s/%s/%s", myrpt->p.archivedir, myrpt->name, mydate);
		myrpt->monstream = ast_writefile(myfname, "wav49", "app_rpt Air Archive", O_CREAT | O_APPEND, 0, 0644);
		if (myrpt->p.monminblocks) {
			blocksleft = diskavail(myrpt);
			if (blocksleft >= myrpt->p.monminblocks) {
				donodelog(myrpt, "TXKEY,MAIN");
			}
		} else {
			donodelog(myrpt, "TXKEY,MAIN");
		}
	}
	rpt_update_boolean(myrpt, "RPT_TXKEYED", 1);
	myrpt->txkeyed = 1;
	time(&myrpt->lasttxkeyedtime);
	myrpt->dailykeyups++;
	myrpt->totalkeyups++;
	rpt_mutex_unlock(&myrpt->lock);
	if (!IS_PSEUDO(myrpt->txchannel)) {
		ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_KEY);
	}
	rpt_mutex_lock(&myrpt->lock);
}

static inline void log_unkeyed(struct rpt *myrpt)
{
	if (myrpt->monstream) {
		ast_closestream(myrpt->monstream);
	}
	myrpt->monstream = NULL;

	myrpt->txkeyed = 0;
	time(&myrpt->lasttxkeyedtime);
	rpt_mutex_unlock(&myrpt->lock);
	if (!IS_PSEUDO(myrpt->txchannel)) {
		ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_UNKEY);
	}
	rpt_mutex_lock(&myrpt->lock);
	donodelog(myrpt, "TXUNKEY,MAIN");
	rpt_update_boolean(myrpt, "RPT_TXKEYED", 0);
	if (myrpt->p.s[myrpt->p.sysstate_cur].sleepena) {
		if (myrpt->sleepreq) {
			myrpt->sleeptimer = 0;
			myrpt->sleepreq = 0;
			myrpt->sleep = 1;
		}
	}
}

static inline void rxunkey_helper(struct rpt *myrpt, struct rpt_link *l)
{
	ast_debug(7, "@@@@ rx un-key\n");
	l->lastrealrx = 0;
	l->rerxtimer = 0;
	if (l->lastrx1) {
		if (myrpt->p.archivedir) {
			donodelog_fmt(myrpt, "RXUNKEY,%s", l->name);
		}
		l->lastrx1 = 0;
		/* XXX Note in first usage, rpt_update_links is first,
		 * but in second, time was first. Don't think it matters though. */
		rpt_update_links(myrpt);
		time(&l->lastunkeytime);
		if (myrpt->p.duplex)
			rpt_telemetry(myrpt, LINKUNKEY, l);
	}
}

static inline void periodic_process_links(struct rpt *myrpt, const int elap)
{
	struct ast_frame *f;
	int x;
	struct rpt_link *l = myrpt->links.next;
	while (l != &myrpt->links) {
		int myrx, mymaxct;

		if (l->chan && l->thisconnected && !AST_LIST_EMPTY(&l->textq)) {
			f = AST_LIST_REMOVE_HEAD(&l->textq, frame_list);
			ast_write(l->chan, f);
			ast_frfree(f);
		}

		if (l->rxlingertimer)
			l->rxlingertimer -= elap;
		if (l->rxlingertimer < 0)
			l->rxlingertimer = 0;

		/* Update the timer, checking if it expired just now. */
		x = l->newkeytimer;
		if (l->newkeytimer)
			l->newkeytimer -= elap;
		if (l->newkeytimer < 0)
			l->newkeytimer = 0;

		/* Some reverse-engineering comments here from NA debugging issue #46 (inbound calls being keyed when they shouldn't be)
		 * This if statement executes if the timer just expired.
		 * This does NOT include cases like in handle_link_data where we set newkeytimer = 0 explicitly + set newkey to 1 or 2 (because then x == 0 here)
		 */
		if (x > 0 && !l->newkeytimer) {
			/* Issue #46 background:
			 *
			 * There is a kind of "handshake" that happens when setting up the IAX2 trunk between two nodes,
			 * using text frames. NEWKEY1 is part of the handshake (it does not, as the name might imply, indicate that the other
			 * side should consider either side "keyed" and transmitting... but as I explain below, the lack of sending/receiving
			 * this can actually lead to a node being improperly keyed).
			 *
			 * Ordinarily, the called node will call the send_newkey function (XXX twice, it seems, one of these may be superflous)
			 * The calling node calls this function once. What this function does is send the text frame NEWKEY1STR to the other side.
			 * Issue #46 was concerned with a case where this was slightly broken, and the below happened:
			 * (A = calling node, B = called node)
			 *
			 * A									B
			 *		<- send_newkey
			 *		<- send_newkey
			 *		send_newkey ->
			 *
			 *		<-- receive !NEWKEY1!
			 *		<-- receive !NEWKEY1!
			 *		(MISSING) received !NEWKEY1! ->
			 *
			 * Note that the above depiction separates the TX and RX, but there are only 3 text frames involved.
			 * In issue #46, 3 text frames are sent, but only 2 are really "received".
			 * And it so happens that the text frame that B doesn't get from A is exactly the text frame
			 * that is responsible for setting newkeytimer=0 and newkey=2, i.e. if this doesn't happen, then we'll hit the WARNING case in the below if statement.
			 *
			 * Note that all of these comments are from spending hours debugging this issue and reverse-engineering, but at this point I'm pretty confident
			 * about these parts of the code, even though I'm not Jim Dixon and he didn't comment any of this code originally.
			 *
			 * The issue was resolvable by setting jitterbuffer=no in iax.conf. It seems the jitterbuffer was holding received text frames in the JB queue
			 * until it got something "important" like a voice frame. This is because chan_iax2's jitter buffer was stalling improperly
			 * until it received a voice frame, because only at that point would it try to begin reading from the jitterbuffer queue. This was fixed
			 * by falling back to the format negotiated during call setup prior to receiving audio.
			 */
			if (l->thisconnected) {
				/* We're connected, but haven't received a NEWKEY1STR text frame yet...
				 * The newkeytimer expired on a connected (~answered?) node, i.e. handle_link_data hasn't yet gotten called
				 * to set newkeytimer = 0 and newkey to non-zero, i.e. we haven't received a text frame with NEWKEY1STR over the IAX2 channel yet.
				 */
				if (l->newkey == 2) {
					/* This can ripple to have consequences down the line, namely we might start writing voice frames
					 * across the IAX2 link because of this, basically causing us to be transmitting (keyed).
					 * If this happens, this indicates a problem upstream, and we should emit a warning here
					 * since undesired behavior will likely ensue.
					 */
					ast_log(LOG_WARNING, "%p newkeytimer expired on connected node, setting newkey from 2 to 0.\n", l);
					l->newkey = 0;
				}
			} else {
				/* If not connected yet (maybe a slow link connection?), wait another NEWKEYTIME ms */
				l->newkeytimer = NEWKEYTIME;
			}
		}
		if ((l->linkmode > 1) && (l->linkmode < 0x7ffffffe)) {
			l->linkmode -= elap;
			if (l->linkmode < 1)
				l->linkmode = 1;
		}
		if ((l->newkey == 2) && l->lastrealrx && (!l->rxlingertimer)) {
			rxunkey_helper(myrpt, l);
		}

		if (l->voxtotimer)
			l->voxtotimer -= elap;
		if (l->voxtotimer < 0)
			l->voxtotimer = 0;

		if (l->lasttx != l->lasttx1) {
			if ((!l->phonemode) || (!l->phonevox))
				voxinit_link(l, !l->lasttx);
			l->lasttx1 = l->lasttx;
		}
		myrx = l->lastrealrx;
		if ((l->phonemode) && (l->phonevox)) {
			myrx = myrx || (!AST_LIST_EMPTY(&l->rxq));
			if (l->voxtotimer <= 0) {
				if (l->voxtostate) {
					l->voxtotimer = myrpt->p.voxtimeout_ms;
					l->voxtostate = 0;
				} else {
					l->voxtotimer = myrpt->p.voxrecover_ms;
					l->voxtostate = 1;
				}
			}
			if (!l->voxtostate)
				myrx = myrx || l->wasvox;
		}
		l->lastrx = myrx;
		if (l->linklisttimer) {
			l->linklisttimer -= elap;
			if (l->linklisttimer < 0)
				l->linklisttimer = 0;
		}
		if ((!l->linklisttimer) && (l->name[0] != '0') && (!l->isremote)) {
			struct ast_frame lf;
			char lstr[MAXLINKLIST];

			memset(&lf, 0, sizeof(lf));
			lf.frametype = AST_FRAME_TEXT;
			lf.subclass.format = ast_format_slin;
			lf.offset = 0;
			lf.mallocd = 0;
			lf.samples = 0;
			l->linklisttimer = LINKLISTTIME;
			strcpy(lstr, "L ");
			__mklinklist(myrpt, l, lstr + 2, 0);
			if (l->chan) {
				lf.datalen = strlen(lstr) + 1;
				lf.data.ptr = lstr;
				rpt_qwrite(l, &lf);
				ast_debug(7, "@@@@ node %s sent node string %s to node %s\n", myrpt->name, lstr, l->name);
			}
		}
		if (l->newkey == 1) {
			if ((l->retxtimer += elap) >= REDUNDANT_TX_TIME) {
				l->retxtimer = 0;
				if (l->chan && l->phonemode == 0) {
					if (l->lasttx)
						ast_indicate(l->chan, AST_CONTROL_RADIO_KEY);
					else
						ast_indicate(l->chan, AST_CONTROL_RADIO_UNKEY);
				}
			}
			if ((l->rerxtimer += elap) >= (REDUNDANT_TX_TIME * 5)) {
				ast_debug(7, "@@@@ rx un-key\n");
				l->lastrealrx = 0;
				l->rerxtimer = 0;
				if (l->lastrx1) {
					if (myrpt->p.archivedir) {
						donodelog_fmt(myrpt, "RXUNKEY(T),%s", l->name);
					}
					if (myrpt->p.duplex)
						rpt_telemetry(myrpt, LINKUNKEY, l);
					l->lastrx1 = 0;
					rpt_update_links(myrpt);
				}
			}
		}
		if (l->disctime) {	/* Disconnect timer active on a channel ? */
			l->disctime -= elap;
			if (l->disctime <= 0)	/* Disconnect timer expired on inbound channel ? */
				l->disctime = 0;	/* Yep */
		}

		if (l->retrytimer) {
			l->retrytimer -= elap;
			if (l->retrytimer < 0)
				l->retrytimer = 0;
		}

		/* Tally connect time */
		l->connecttime += elap;

		/* ignore non-timing channels */
		if (l->elaptime < 0) {
			l = l->next;
			continue;
		}
		l->elaptime += elap;
		mymaxct = MAXCONNECTTIME;
		/* if connection has taken too long */
		if ((l->elaptime > mymaxct) && ((!l->chan) || (ast_channel_state(l->chan) != AST_STATE_UP))) {
			l->elaptime = 0;
			rpt_mutex_unlock(&myrpt->lock);
			if (l->chan)
				ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
			rpt_mutex_lock(&myrpt->lock);
			break;
		}
		if ((!l->chan) && (!l->retrytimer) && l->outbound && (l->retries++ < l->max_retries) && (l->hasconnected)) {
			if (l->chan)
				ast_hangup(l->chan);
			l->chan = 0;
			rpt_mutex_unlock(&myrpt->lock);
			if ((l->name[0] > '0') && (l->name[0] <= '9') && (!l->isremote)) {
				if (attempt_reconnect(myrpt, l) == -1) {
					l->retrytimer = RETRY_TIMER_MS;
				}
			} else {
				l->retries = l->max_retries + 1;
			}

			rpt_mutex_lock(&myrpt->lock);
			break;
		}
		if ((!l->chan) && (!l->retrytimer) && l->outbound && (l->retries >= l->max_retries)) {
			/* remove from queue */
			rpt_link_remove(myrpt, l);
			if (!strcmp(myrpt->cmdnode, l->name))
				myrpt->cmdnode[0] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			if (l->name[0] != '0') {
				if (!l->hasconnected)
					rpt_telemetry(myrpt, CONNFAIL, l);
				else
					rpt_telemetry(myrpt, REMDISC, l);
			}
			if (l->hasconnected)
				rpt_update_links(myrpt);
			if (myrpt->p.archivedir) {
				donodelog_fmt(myrpt, l->hasconnected ? "LINKDISC,%s" : "LINKFAIL,%s", l->name);
			}
			/* hang-up on call to device */
			ast_hangup(l->pchan);
			ast_free(l);
			rpt_mutex_lock(&myrpt->lock);
			break;
		}
		if ((!l->chan) && (!l->disctime) && (!l->outbound)) {
			ast_debug(1, "LINKDISC AA\n");
			/* remove from queue */
			rpt_link_remove(myrpt, l);
			if (myrpt->links.next == &myrpt->links)
				channel_revert(myrpt);
			if (!strcmp(myrpt->cmdnode, l->name))
				myrpt->cmdnode[0] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			if (l->name[0] != '0') {
				rpt_telemetry(myrpt, REMDISC, l);
			}
			rpt_update_links(myrpt);
			if (myrpt->p.archivedir) {
				donodelog_fmt(myrpt, "LINKDISC,%s", l->name);
			}
			dodispgm(myrpt, l->name);
			/* hang-up on call to device */
			ast_hangup(l->pchan);
			ast_free(l);
			rpt_mutex_lock(&myrpt->lock);
			break;
		}
		l = l->next;
	}
}

static inline int do_link_post(struct rpt *myrpt)
{
	int nstr;
	char lst, *str;
	time_t now;
	struct rpt_link *l;

	myrpt->linkposttimer = LINKPOSTTIME;
	nstr = 0;
	for (l = myrpt->links.next; l != &myrpt->links; l = l->next) {
		/* if is not a real link, ignore it */
		if (l->name[0] == '0') {
			continue;
		}
		nstr += strlen(l->name) + 1;
	}
	str = ast_malloc(nstr + 256);
	if (!str) {
		ast_mutex_unlock(&myrpt->lock);
		return -1;
	}
	nstr = 0;
	strcpy(str, "nodes=");
	for (l = myrpt->links.next; l != &myrpt->links; l = l->next) {
		/* if is not a real link, ignore it */
		if (l->name[0] == '0') {
			continue;
		}
		lst = 'T';
		if (!l->mode)
			lst = 'R';
		if (l->mode > 1)
			lst = 'L';
		if (!l->thisconnected)
			lst = 'C';
		if (nstr)
			strcat(str, ",");
		sprintf(str + strlen(str), "%c%s", lst, l->name);
		nstr = 1;
	}
	sprintf(str + strlen(str), "&apprptvers=%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
	time(&now);
	sprintf(str + strlen(str), "&apprptuptime=%d", (int) (now - starttime));
	sprintf(str + strlen(str),
			"&totalkerchunks=%d&totalkeyups=%d&totaltxtime=%d&timeouts=%d&totalexecdcommands=%d",
			myrpt->totalkerchunks, myrpt->totalkeyups, (int) myrpt->totaltxtime / 1000, myrpt->timeouts,
			myrpt->totalexecdcommands);
	rpt_mutex_unlock(&myrpt->lock);
	statpost(myrpt, str);
	rpt_mutex_lock(&myrpt->lock);
	ast_free(str);
	return 0;
}

/*! \brief Do macro timers */
static inline void update_macrotimer(struct rpt *myrpt, int elap)
{
	if (myrpt->macrotimer) {
		myrpt->macrotimer -= elap;
	}
	if (myrpt->macrotimer < 0) {
		myrpt->macrotimer = 0;
	}
}

/*! \brief Do local DTMF timer */
static inline void update_dtmf_local_timer(struct rpt *myrpt, int elap)
{
	if (myrpt->dtmf_local_timer) {
		if (myrpt->dtmf_local_timer > 1) {
			myrpt->dtmf_local_timer -= elap;
		}
		if (myrpt->dtmf_local_timer < 1) {
			myrpt->dtmf_local_timer = 1;
		}
	}
}

static inline void update_voxtimer(struct rpt *myrpt, int elap)
{
	if (myrpt->voxtotimer) {
		myrpt->voxtotimer -= elap;
	}
	if (myrpt->voxtotimer < 0) {
		myrpt->voxtotimer = 0;
	}
}

static inline void update_rxlingertimer(struct rpt *myrpt, int elap)
{
	if (myrpt->rxlingertimer) {
		myrpt->rxlingertimer -= elap;
	}
	if (myrpt->rxlingertimer < 0) {
		myrpt->rxlingertimer = 0;
	}
}

static inline int update_timers(struct rpt *myrpt, const int elap, const int totx)
{
	int i;
	if (myrpt->linkposttimer) {
		myrpt->linkposttimer -= elap;
		if (myrpt->linkposttimer < 0)
			myrpt->linkposttimer = 0;
	}
	if (myrpt->linkposttimer <= 0 && do_link_post(myrpt)) {
		return -1;
	}
	if (myrpt->deferid && (!is_paging(myrpt))) {
		myrpt->deferid = 0;
		queue_id(myrpt);
	}
	if (myrpt->keyposttimer) {
		myrpt->keyposttimer -= elap;
		if (myrpt->keyposttimer < 0)
			myrpt->keyposttimer = 0;
	}
	if (myrpt->keyposttimer <= 0) {
		char str[100];
		int n = 0;
		time_t now;

		myrpt->keyposttimer = KEYPOSTTIME;
		time(&now);
		if (myrpt->lastkeyedtime) {
			n = (int) (now - myrpt->lastkeyedtime);
		}
		sprintf(str, "keyed=%d&keytime=%d", myrpt->keyed, n);
		rpt_mutex_unlock(&myrpt->lock);
		statpost(myrpt, str);
		rpt_mutex_lock(&myrpt->lock);
	}
	if (totx) {
		myrpt->dailytxtime += elap;
		myrpt->totaltxtime += elap;
	}
	i = myrpt->tailtimer;
	if (myrpt->tailtimer)
		myrpt->tailtimer -= elap;
	if (myrpt->tailtimer < 0)
		myrpt->tailtimer = 0;
	if ((i) && (myrpt->tailtimer == 0))
		myrpt->tailevent = 1;
	if ((!myrpt->p.s[myrpt->p.sysstate_cur].totdisable) && myrpt->totimer)
		myrpt->totimer -= elap;
	if (myrpt->totimer < 0)
		myrpt->totimer = 0;
	if (myrpt->idtimer)
		myrpt->idtimer -= elap;
	if (myrpt->idtimer < 0)
		myrpt->idtimer = 0;
	if (myrpt->tmsgtimer)
		myrpt->tmsgtimer -= elap;
	if (myrpt->tmsgtimer < 0)
		myrpt->tmsgtimer = 0;
	update_voxtimer(myrpt, elap);
	if (myrpt->keyed)
		myrpt->lastkeytimer = KEYTIMERTIME;
	else {
		if (myrpt->lastkeytimer)
			myrpt->lastkeytimer -= elap;
		if (myrpt->lastkeytimer < 0)
			myrpt->lastkeytimer = 0;
	}
	myrpt->elketimer += elap;
	if ((myrpt->telemmode != 0x7fffffff) && (myrpt->telemmode > 1)) {
		myrpt->telemmode -= elap;
		if (myrpt->telemmode < 1)
			myrpt->telemmode = 1;
	}
	if (myrpt->exttx) {
		myrpt->parrottimer = myrpt->p.parrottime;
	} else {
		if (myrpt->parrottimer)
			myrpt->parrottimer -= elap;
		if (myrpt->parrottimer < 0)
			myrpt->parrottimer = 0;
	}
	update_macrotimer(myrpt, elap);
	update_dtmf_local_timer(myrpt, elap);

	do_dtmf_local(myrpt, 0);
	/* Execute scheduler appx. every 2 tenths of a second */
	if (myrpt->skedtimer <= 0) {
		myrpt->skedtimer = 200;
		do_scheduler(myrpt);
	} else {
		myrpt->skedtimer -= elap;
	}

	return 0;
}

static inline int update_parrot(struct rpt *myrpt)
{
	union {
		int i;
		void *p;
		char _filler[8];
	} pu;

	if (rpt_parrot_add(myrpt)) {
		return -1;
	}

	if (myrpt->parrotstream) {
		ast_closestream(myrpt->parrotstream);
	}
	myrpt->parrotstream = NULL;
	myrpt->parrotstate = 2;
	pu.i = myrpt->parrotcnt++;
	rpt_telemetry(myrpt, PARROT, pu.p);
	return 0;
}

static inline void process_command(struct rpt *myrpt)
{
	myrpt->cmdAction.state = CMD_STATE_EXECUTING;
	rpt_mutex_unlock(&myrpt->lock);
	(*function_table[myrpt->cmdAction.functionNumber].function)(myrpt, myrpt->cmdAction.param,
		myrpt->cmdAction.digits, myrpt->cmdAction.command_source, NULL);
	rpt_mutex_lock(&myrpt->lock);
	myrpt->cmdAction.state = CMD_STATE_IDLE;
}

static inline void outstream_write(struct rpt *myrpt, struct ast_frame *f)
{
	int res = write(myrpt->outstreampipe[1], f->data.ptr, f->datalen);
	/* if the write fails report the error one time
	 * if it is not resolved in 60 seconds kill
	 * the outstream process
	 */
	if (res != f->datalen) {
		time_t now;
		if (!myrpt->outstreamlasterror) {
			ast_log(LOG_WARNING, "Outstream write failed for node %s: %s\n", myrpt->name, strerror(errno));
			time(&myrpt->outstreamlasterror);
		}
		time(&now);
		if (myrpt->outstreampid && (now - myrpt->outstreamlasterror) > 59) {
			res = kill(myrpt->outstreampid, SIGTERM);
			if (res) {
				ast_log(LOG_ERROR, "Cannot kill outstream process for node %s: %s\n", myrpt->name, strerror(errno));
			}
			myrpt->outstreampid = 0;
		}
	} else {
		if (myrpt->outstreamlasterror) {
			ast_log(LOG_NOTICE, "Outstream resumed on node %s\n", myrpt->name);
			myrpt->outstreamlasterror = 0;
		}
	}
}

static inline void mute_frame_helper(struct rpt *myrpt, struct ast_frame *f, int ismuted)
{
	struct ast_frame *f2;
	if (ismuted) {
		memset(f->data.ptr, 0, f->datalen);
		if (myrpt->lastf1)
			memset(myrpt->lastf1->data.ptr, 0, myrpt->lastf1->datalen);
		if (myrpt->lastf2)
			memset(myrpt->lastf2->data.ptr, 0, myrpt->lastf2->datalen);
	}
	f2 = f ? ast_frdup(f) : NULL;
	myrpt->lastf2 = myrpt->lastf1;
	myrpt->lastf1 = f2;
	if (ismuted) {
		if (myrpt->lastf1)
			memset(myrpt->lastf1->data.ptr, 0, myrpt->lastf1->datalen);
		if (myrpt->lastf2)
			memset(myrpt->lastf2->data.ptr, 0, myrpt->lastf2->datalen);
	}
}

static inline int rxchannel_read(struct rpt *myrpt, const int lasttx)
{
	int ismuted;
	struct ast_frame *f, *f1;
	int i, dtmfed = 0;

	rpt_mutex_lock(&myrpt->blocklock);
	f = ast_read(myrpt->rxchannel);
	rpt_mutex_unlock(&myrpt->blocklock);
	if (!f) {
		ast_debug(1, "@@@@ rpt:Hung Up\n");
		return -1;
	}

	if (f->frametype == AST_FRAME_TEXT && myrpt->rxchankeyed) {
		char myrxrssi[32];

		if (sscanf((char *) f->data.ptr, "R %s", myrxrssi) == 1) {
			myrpt->rxrssi = atoi(myrxrssi);
			ast_debug(8, "[%s] rxchannel rssi=%i\n", myrpt->name, myrpt->rxrssi);
			if (myrpt->p.votertype == 2)
				rssi_send(myrpt);
		}
	}

	/* if out voted drop DTMF frames */
	if (myrpt->p.votermode && !myrpt->votewinner
		&& (f->frametype == AST_FRAME_DTMF_BEGIN || f->frametype == AST_FRAME_DTMF_END)
		) {
		rpt_mutex_unlock(&myrpt->lock);
		ast_frfree(f);
		return 0;
	}

	if (f->frametype == AST_FRAME_VOICE) {
#ifdef	_MDC_DECODE_H_
		unsigned char ubuf[2560];
		short *sp;
		int n;
#endif
		if (myrpt->p.rxburstfreq) {
			if ((!myrpt->reallykeyed) || myrpt->keyed) {
				myrpt->lastrxburst = 0;
#ifdef NATIVE_DSP
				/* this zeros out energy and lasthit, but not hit_count. If this proves to be a problem, we can add API to do that. */
				/*! \todo we need to goertzel_reset on the tone, e.g. we need to add an ast_dsp_freqreset */
				/*! \todo this may also fix the problem in app_sf where to be reliable we have to free on each match. Test and see */
				ast_dsp_digitreset(myrpt->dsp); /// NOTE: THIS IS WRONG! See comment above.
#else
				goertzel_reset(&myrpt->burst_tone_state.tone);
				myrpt->burst_tone_state.last_hit = 0;
				myrpt->burst_tone_state.hit_count = 0;
				myrpt->burst_tone_state.energy = 0.0;
#endif
			} else {
#ifdef NATIVE_DSP
				struct ast_frame *frame = NULL;

				/* leave f alone */
				frame = ast_dsp_process(myrpt->rxchannel, myrpt->dsp, ast_frdup(f));
				i= (frame->frametype == AST_FRAME_DTMF && frame->subclass.integer == 'q') ? 1 : 0; /* q indicates frequency hit */
				ast_frfree(frame);
#else
				i = tone_detect(&myrpt->burst_tone_state, f->data.ptr, f->samples);
#endif
				ast_debug(1, "Node %s got %d Hz Rx Burst\n", myrpt->name, myrpt->p.rxburstfreq);
				if ((!i) && myrpt->lastrxburst) {
					ast_debug(1, "Node %s now keyed after Rx Burst\n", myrpt->name);
					myrpt->linkactivitytimer = 0;
					myrpt->keyed = 1;
					time(&myrpt->lastkeyedtime);
					myrpt->keyposttimer = KEYPOSTSHORTTIME;
				}
				myrpt->lastrxburst = i;
			}
		}
		if (myrpt->p.dtmfkey) {
			if ((!myrpt->reallykeyed) || myrpt->keyed) {
				myrpt->dtmfkeyed = 0;
				myrpt->dtmfkeybuf[0] = 0;
			}
			if (myrpt->reallykeyed && myrpt->dtmfkeyed && (!myrpt->keyed)) {
				myrpt->dtmfkeyed = 0;
				myrpt->dtmfkeybuf[0] = 0;
				myrpt->linkactivitytimer = 0;
				myrpt->keyed = 1;
				time(&myrpt->lastkeyedtime);
				myrpt->keyposttimer = KEYPOSTSHORTTIME;
			}
		}
#ifdef	_MDC_DECODE_H_
		if (!myrpt->reallykeyed) {
			memset(f->data.ptr, 0, f->datalen);
		}
		sp = (short *) f->data.ptr;
		/* convert block to unsigned char */
		for (n = 0; n < f->datalen / 2; n++) {
			ubuf[n] = (*sp++ >> 8) + 128;
		}
		n = mdc_decoder_process_samples(myrpt->mdc, ubuf, f->datalen / 2);
		if (n == 1) {

			unsigned char op, arg;
			unsigned short unitID;
			char ustr[16];

			mdc_decoder_get_packet(myrpt->mdc, &op, &arg, &unitID);
			ast_debug(2, "Got MDC-1200 (single-length) packet on node %s:\n", myrpt->name);
			ast_debug(2, "op: %02x, arg: %02x, UnitID: %04x\n", op & 255, arg & 255, unitID);
			/* if for PTT ID */
			if ((op == 1) && ((arg == 0) || (arg == 0x80))) {
				myrpt->lastunit = unitID;
				sprintf(ustr, "I%04X", unitID);
				mdc1200_notify(myrpt, NULL, ustr);
				mdc1200_send(myrpt, ustr);
				mdc1200_cmd(myrpt, ustr);
			}
			/* if for EMERGENCY */
			if ((op == 0) && ((arg == 0x81) || (arg == 0x80))) {
				myrpt->lastunit = unitID;
				sprintf(ustr, "E%04X", unitID);
				mdc1200_notify(myrpt, NULL, ustr);
				mdc1200_send(myrpt, ustr);
				mdc1200_cmd(myrpt, ustr);
			}
			/* if for Stun ACK W9CR */
			if ((op == 0x0b) && (arg == 0x00)) {
				myrpt->lastunit = unitID;
				sprintf(ustr, "STUN ACK %04X", unitID);
			}
			/* if for STS (status)  */
			if (op == 0x46) {
				myrpt->lastunit = unitID;
				sprintf(ustr, "S%04X-%X", unitID, arg & 0xf);

#ifdef	_MDC_ENCODE_H_
				mdc1200_ack_status(myrpt, unitID);
#endif
				mdc1200_notify(myrpt, NULL, ustr);
				mdc1200_send(myrpt, ustr);
				mdc1200_cmd(myrpt, ustr);
			}
		}
		if (n == 2) {
			unsigned char op, arg, ex1, ex2, ex3, ex4;
			unsigned short unitID;
			char ustr[20];

			mdc_decoder_get_double_packet(myrpt->mdc, &op, &arg, &unitID, &ex1, &ex2, &ex3, &ex4);
			ast_debug(2, "Got MDC-1200 (double-length) packet on node %s:\n", myrpt->name);
			ast_debug(2, "op: %02x, arg: %02x, UnitID: %04x\n", op & 255, arg & 255, unitID);
			ast_debug(2, "ex1: %02x, ex2: %02x, ex3: %02x, ex4: %02x\n", ex1 & 255, ex2 & 255, ex3 & 255, ex4 & 255);
			/* if for SelCall or Alert */
			if ((op == 0x35) && (arg = 0x89)) {
				/* if is Alert */
				if (ex1 & 1)
					sprintf(ustr, "A%02X%02X-%04X", ex3 & 255, ex4 & 255, unitID);
				/* otherwise is selcall */
				else
					sprintf(ustr, "S%02X%02X-%04X", ex3 & 255, ex4 & 255, unitID);
				mdc1200_notify(myrpt, NULL, ustr);
				mdc1200_send(myrpt, ustr);
				mdc1200_cmd(myrpt, ustr);
			}
		}
#endif
#ifdef	__RPT_NOTCH
		/* apply inbound filters, if any */
		rpt_filter(myrpt, f->data.ptr, f->datalen / 2);
#endif
		if ((!myrpt->localtx) &&	/* (!myrpt->p.linktolink) && */
			(!myrpt->localoverride)) {
			memset(f->data.ptr, 0, f->datalen);
		}

		ismuted = dtmfed || rpt_conf_get_muted(myrpt->dahdirxchannel, myrpt);
		dtmfed = 0;

		if (myrpt->p.votertype == 1) {
			if (!myrpt->rxchankeyed)
				myrpt->votewinner = 0;

			if (!myrpt->voteremrx)
				myrpt->voted_link = NULL;

			if (!myrpt->rxchankeyed && !myrpt->voteremrx) {
				myrpt->voter_oneshot = 0;
				myrpt->voted_rssi = 0;
			}
		}

		if (myrpt->p.votertype == 1 && myrpt->vote_counter && (myrpt->rxchankeyed || myrpt->voteremrx)
			&& (myrpt->p.votermode == 2 || (myrpt->p.votermode == 1 && !myrpt->voter_oneshot))
			) {
			if (--myrpt->vote_counter <= 0) {
				myrpt->vote_counter = 10;
				ast_debug(7, "[%s] vote rxrssi=%i\n", myrpt->name, myrpt->rxrssi);
				FindBestRssi(myrpt);
				myrpt->voter_oneshot = 1;
			}
		}
		/* if a voting rx and not the winner, mute audio */
		if (myrpt->p.votertype == 1 && myrpt->voted_link != NULL) {
			ismuted = 1;
		}
		mute_frame_helper(myrpt, f, ismuted);
		f1 = myrpt->lastf2;
		if (f1) {
			ast_write(myrpt->localoverride ? myrpt->txpchannel : myrpt->pchannel, f1);
			ast_frfree(f1);
			myrpt->lastf2 = NULL; /* Now invalid since aliased with f1 */
			if ((myrpt->p.duplex < 2) && myrpt->monstream && (!myrpt->txkeyed) && myrpt->keyed) {
				ast_writestream(myrpt->monstream, f1);
			}
			if ((myrpt->p.duplex < 2) && myrpt->keyed && myrpt->p.outstreamcmd &&
				(myrpt->outstreampipe[1] != -1)) {
				outstream_write(myrpt, f1);
			}
		}
	} else if (f->frametype == AST_FRAME_DTMF_BEGIN) {
		if (myrpt->lastf1)
			memset(myrpt->lastf1->data.ptr, 0, myrpt->lastf1->datalen);
		if (myrpt->lastf2)
			memset(myrpt->lastf2->data.ptr, 0, myrpt->lastf2->datalen);
		dtmfed = 1;
		myrpt->lastdtmftime = ast_tvnow();
	} else if (f->frametype == AST_FRAME_DTMF) {
		int x;
		char c = (char) f->subclass.integer;	/* get DTMF char */
		ast_frfree(f);
		x = ast_tvdiff_ms(ast_tvnow(), myrpt->lastdtmftime);
		if ((myrpt->p.litzcmd) && (x >= myrpt->p.litztime) && strchr(myrpt->p.litzchar, c)) {
			ast_debug(1, "Doing litz command %s on node %s\n", myrpt->p.litzcmd, myrpt->name);
			rpt_mutex_lock(&myrpt->lock);
			if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(myrpt->p.litzcmd)) {
				rpt_mutex_unlock(&myrpt->lock);
				return 0;
			}
			myrpt->macrotimer = MACROTIME;
			strncat(myrpt->macrobuf, myrpt->p.litzcmd, MAXMACRO - 1);
			rpt_mutex_unlock(&myrpt->lock);

			return 0;
		}
		if (myrpt->lastf1)
			memset(myrpt->lastf1->data.ptr, 0, myrpt->lastf1->datalen);
		if (myrpt->lastf2)
			memset(myrpt->lastf2->data.ptr, 0, myrpt->lastf2->datalen);
		dtmfed = 1;
		if ((!myrpt->lastkeytimer) && (!myrpt->localoverride)) {
			if (myrpt->p.dtmfkey)
				local_dtmfkey_helper(myrpt, c);
			return 0;
		}
		c = func_xlat(myrpt, c, &myrpt->p.inxlat);
		if (c)
			local_dtmf_helper(myrpt, c);
		return 0;
	} else if (f->frametype == AST_FRAME_CONTROL) {
		if (f->subclass.integer == AST_CONTROL_HANGUP) {
			ast_debug(1, "@@@@ rpt:Hung Up\n");
			ast_frfree(f);
			return -1;
		}
		/* if RX key */
		if (f->subclass.integer == AST_CONTROL_RADIO_KEY) {
			if ((!lasttx) || (myrpt->p.duplex > 1) || (myrpt->p.linktolink)) {
				ast_debug(7, "**** rx key\n");
				myrpt->reallykeyed = 1;
				myrpt->dtmfkeybuf[0] = 0;
				myrpt->curdtmfuser[0] = 0;
				if ((!myrpt->p.rxburstfreq) && (!myrpt->p.dtmfkey)) {
					myrpt->linkactivitytimer = 0;
					myrpt->keyed = 1;
					time(&myrpt->lastkeyedtime);
					myrpt->keyposttimer = KEYPOSTSHORTTIME;
				}
			}
			if (myrpt->p.archivedir) {
				if (myrpt->p.duplex < 2) {
					char myfname[512], mydate[100];
					long blocksleft;
					time_t myt;

					time(&myt);
					strftime(mydate, sizeof(mydate) - 1, "%Y%m%d%H%M%S", localtime(&myt));
					sprintf(myfname, "%s/%s/%s", myrpt->p.archivedir, myrpt->name, mydate);
					if (myrpt->p.monminblocks) {
						blocksleft = diskavail(myrpt);
						if (blocksleft >= myrpt->p.monminblocks) {
							myrpt->monstream =
								ast_writefile(myfname, "wav49", "app_rpt Air Archive", O_CREAT | O_APPEND, 0,
											  0600);
						}
					}
				}
				donodelog(myrpt, "RXKEY,MAIN");
			}
			rpt_update_boolean(myrpt, "RPT_RXKEYED", 1);
			myrpt->elketimer = 0;
			myrpt->localoverride = 0;
			if (f->datalen && f->data.ptr) {
				char *val, busy = 0;

				send_link_pl(myrpt, f->data.ptr);

				if (myrpt->p.nlocallist) {
					int x;
					for (x = 0; x < myrpt->p.nlocallist; x++) {
						if (!strcasecmp(f->data.ptr, myrpt->p.locallist[x])) {
							myrpt->localoverride = 1;
							myrpt->keyed = 0;
							break;
						}
					}
				}
				ast_debug(1, "Got PL %s on node %s\n", (char *) f->data.ptr, myrpt->name);
				/* ctcss code autopatch initiate */
				if (strstr((char *) f->data.ptr, "/M/") && !myrpt->macropatch) {
					char val[16];
					strcpy(val, "*6");
					myrpt->macropatch = 1;
					rpt_mutex_lock(&myrpt->lock);
					if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(val)) {
						rpt_mutex_unlock(&myrpt->lock);
						busy = 1;
					}
					if (!busy) {
						myrpt->macrotimer = MACROTIME;
						strncat(myrpt->macrobuf, val, MAXMACRO - 1);
						if (!busy)
							strcpy(myrpt->lasttone, (char *) f->data.ptr);
					}
					rpt_mutex_unlock(&myrpt->lock);
				} else if (strcmp((char *) f->data.ptr, myrpt->lasttone)) {
					val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.tonemacro, (char *) f->data.ptr);
					if (val) {
						ast_debug(1, "Tone %s doing %s on node %s\n", (char *) f->data.ptr, val, myrpt->name);
						rpt_mutex_lock(&myrpt->lock);
						if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(val)) {
							rpt_mutex_unlock(&myrpt->lock);
							busy = 1;
						}
						if (!busy) {
							myrpt->macrotimer = MACROTIME;
							strncat(myrpt->macrobuf, val, MAXMACRO - 1);
						}
						rpt_mutex_unlock(&myrpt->lock);
					}
					if (!busy)
						strcpy(myrpt->lasttone, (char *) f->data.ptr);
				}
			} else {
				myrpt->lasttone[0] = 0;
				send_link_pl(myrpt, "0");
			}
		}
		/* if RX un-key */
		if (f->subclass.integer == AST_CONTROL_RADIO_UNKEY) {
			char asleep;
			/* clear rx channel rssi */
			myrpt->rxrssi = 0;
			asleep = myrpt->p.s[myrpt->p.sysstate_cur].sleepena & myrpt->sleep;

			if ((!lasttx) || (myrpt->p.duplex > 1) || (myrpt->p.linktolink)) {
				ast_debug(7, "**** rx un-key\n");

				if ((!asleep) && myrpt->p.duplex && myrpt->keyed) {
					rpt_telemetry(myrpt, UNKEY, NULL);
				}
			}
			send_link_pl(myrpt, "0");
			myrpt->reallykeyed = 0;
			myrpt->keyed = 0;
			if ((myrpt->p.duplex > 1) && (!asleep) && myrpt->localoverride) {
				rpt_telemetry(myrpt, LOCUNKEY, NULL);
			}
			myrpt->localoverride = 0;
			time(&myrpt->lastkeyedtime);
			myrpt->keyposttimer = KEYPOSTSHORTTIME;
			myrpt->lastdtmfuser[0] = 0;
			strcpy(myrpt->lastdtmfuser, myrpt->curdtmfuser);
			myrpt->curdtmfuser[0] = 0;
			if (myrpt->monstream && (myrpt->p.duplex < 2)) {
				ast_closestream(myrpt->monstream);
				myrpt->monstream = NULL;
			}
			if (myrpt->p.archivedir) {
				donodelog(myrpt, "RXUNKEY,MAIN");
			}
			rpt_update_boolean(myrpt, "RPT_RXKEYED", 0);
		}
	} else if (f->frametype == AST_FRAME_TEXT) {	/* if a message from a USB device */
		char buf[100];
		int j;
		/* if is a USRP device */
		if (!strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "usrp")) {
			const char *argv[4];
			int argc = 4;
			argv[2] = myrpt->name;
			argv[3] = f->data.ptr;
			rpt_do_sendall(0, argc, argv);
		}
		/* if is a USB device */
		if (!strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "radio") || !strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "simpleusb")) {
			/* if message parsable */
			if (sscanf(f->data.ptr, "GPIO%d %d", &i, &j) >= 2) {
				sprintf(buf, "RPT_URI_GPIO%d", i);
				rpt_update_boolean(myrpt, buf, j);
			}
			/* if message parsable */
			else if (sscanf(f->data.ptr, "PP%d %d", &i, &j) >= 2) {
				sprintf(buf, "RPT_PP%d", i);
				rpt_update_boolean(myrpt, buf, j);
			} else if (!strcmp(f->data.ptr, "ENDPAGE")) {
				myrpt->paging.tv_sec = 0;
				myrpt->paging.tv_usec = 0;
			}
		}
		/* if is a BeagleBoard device */
		if (!strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "beagle")) {
			/* if message parsable */
			if (sscanf(f->data.ptr, "GPIO%d %d", &i, &j) >= 2) {
				sprintf(buf, "RPT_BEAGLE_GPIO%d", i);
				rpt_update_boolean(myrpt, buf, j);
			}
		}
		/* if is a Voter device */
		if (!strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "voter")) {
			struct rpt_link *l;
			struct ast_frame wf;
			char str[200];

			if (!strcmp(f->data.ptr, "ENDPAGE")) {
				myrpt->paging.tv_sec = 0;
				myrpt->paging.tv_usec = 0;
			} else {
				sprintf(str, "V %s %s", myrpt->name, (char *) f->data.ptr);
				init_text_frame(&wf);
				wf.datalen = strlen(str) + 1;
				wf.src = "voter_text_send";

				l = myrpt->links.next;
				/* otherwise, send it to all of em */
				while (l != &myrpt->links) {
					/* Dont send to other then IAXRPT client */
					if ((l->name[0] != '0') || (l->phonemode)) {
						l = l->next;
						continue;
					}
					wf.data.ptr = str;
					if (l->chan)
						rpt_qwrite(l, &wf);
					l = l->next;
				}
			}
		}
	}
	ast_frfree(f);
	return 0;
}

static inline int pchannel_read(struct rpt *myrpt)
{
	struct ast_frame *f = ast_read(myrpt->pchannel);
	if (!f) {
		ast_debug(1, "@@@@ rpt:Hung Up\n");
		return -1;
	}
	if (f->frametype == AST_FRAME_VOICE) {
		if (!myrpt->localoverride) {
			ast_write(myrpt->txpchannel, f);
		}
	}
	if (f->frametype == AST_FRAME_CONTROL) {
		if (f->subclass.integer == AST_CONTROL_HANGUP) {
			ast_debug(1, "@@@@ rpt:Hung Up\n");
			ast_frfree(f);
			return 0;
		}
	}
	ast_frfree(f);
	return 0;
}

static inline int hangup_frame_helper(struct ast_channel *chan, const char *chantype, struct ast_frame *f)
{
	if (f->frametype == AST_FRAME_CONTROL) {
		if (f->subclass.integer == AST_CONTROL_HANGUP) {
			ast_debug(1, "%s (%s) received hangup frame\n", ast_channel_name(chan), chantype);
			ast_frfree(f);
			return -1;
		}
	}
	ast_frfree(f);
	return 0;
}

static inline int wait_for_hangup_helper(struct ast_channel *chan, const char *chantype)
{
	struct ast_frame *f = ast_read(chan);
	if (!f) {
		ast_debug(1, "No frame returned by ast_read, %s (%s) hung up\n", ast_channel_name(chan), chantype);
		return -1;
	}
	return hangup_frame_helper(chan, chantype, f);
}

static inline int txchannel_read(struct rpt *myrpt)
{
	return wait_for_hangup_helper(myrpt->txchannel, "txchannel");
}

static inline int dahditxchannel_read(struct rpt *myrpt, char *restrict myfirst)
{
	struct ast_frame *f = ast_read(myrpt->dahditxchannel);
	if (!f) {
		ast_debug(1, "@@@@ rpt:Hung Up\n");
		return -1;
	}
	if (f->frametype == AST_FRAME_VOICE) {
		struct ast_frame *f1;

		if (myrpt->p.duplex < 2) {
			int x;
			if (myrpt->txrealkeyed) {
				if (!*myfirst && myrpt->callmode) {
					x = 0;
					AST_LIST_TRAVERSE(&myrpt->txq, f1, frame_list) x++;
					for (; x < myrpt->p.simplexpatchdelay; x++) {
						f1 = ast_frdup(f);
						memset(f1->data.ptr, 0, f1->datalen);
						memset(&f1->frame_list, 0, sizeof(f1->frame_list));
						AST_LIST_INSERT_TAIL(&myrpt->txq, f1, frame_list);
					}
					*myfirst = 1;
				}
				f1 = ast_frdup(f);
				memset(&f1->frame_list, 0, sizeof(f1->frame_list));
				AST_LIST_INSERT_TAIL(&myrpt->txq, f1, frame_list);
			} else {
				*myfirst = 0;
			}
			x = 0;
			AST_LIST_TRAVERSE(&myrpt->txq, f1, frame_list) x++;
			if (!x) {
				memset(f->data.ptr, 0, f->datalen);
			} else {
				ast_frfree(f);
				f = AST_LIST_REMOVE_HEAD(&myrpt->txq, frame_list);
			}
		} else {
			while ((f1 = AST_LIST_REMOVE_HEAD(&myrpt->txq, frame_list)))
				ast_frfree(f1);
		}
		rpt_mutex_lock(&myrpt->blocklock);
		ast_write(myrpt->txchannel, f);
		rpt_mutex_unlock(&myrpt->blocklock);
	}
	return hangup_frame_helper(myrpt->dahditxchannel, "dahditxchannel", f);
}

/*!
 * \internal
 * \brief Free a frame if it exists
 */
static inline void free_frame(struct ast_frame **f)
{
	if (!*f) {
		return;
	}
	ast_frfree(*f);
	*f = NULL;
}

static void remote_hangup_helper(struct rpt *myrpt, struct rpt_link *l)
{
	rpt_mutex_lock(&myrpt->lock);
	__kickshort(myrpt);
	rpt_mutex_unlock(&myrpt->lock);
	if (strcasecmp(ast_channel_tech(l->chan)->type, "echolink")
		&& strcasecmp(ast_channel_tech(l->chan)->type, "tlb")) {
		if ((!l->disced) && (!l->outbound)) {
			if ((l->name[0] <= '0') || (l->name[0] > '9') || l->isremote)
				l->disctime = 1;
			else
				l->disctime = DISC_TIME;
			rpt_mutex_lock(&myrpt->lock);
			ast_hangup(l->chan);
			l->chan = 0;
			return;
		}

		if (l->retrytimer) {
			if (l->chan) {
				ast_hangup(l->chan);
			}
			l->chan = NULL;
			rpt_mutex_lock(&myrpt->lock);
			return;
		}
		if (l->outbound && (l->retries++ < l->max_retries) && (l->hasconnected)) {
			rpt_mutex_lock(&myrpt->lock);
			if (l->chan) {
				ast_hangup(l->chan);
			}
			l->chan = NULL;
			l->hasconnected = 1;
			l->retrytimer = RETRY_TIMER_MS;
			l->elaptime = 0;
			l->connecttime = 0;
			l->thisconnected = 0;
			return;
		}
	}
	rpt_mutex_lock(&myrpt->lock);
	/* remove from queue */
	rpt_link_remove(myrpt, l);
	if (!strcmp(myrpt->cmdnode, l->name)) {
		myrpt->cmdnode[0] = 0;
	}
	__kickshort(myrpt);
	rpt_mutex_unlock(&myrpt->lock);
	if (!l->hasconnected) {
		rpt_telemetry(myrpt, CONNFAIL, l);
	} else if (l->disced != 2) {
		rpt_telemetry(myrpt, REMDISC, l);
	}
	if (l->hasconnected) {
		rpt_update_links(myrpt);
	}
	if (myrpt->p.archivedir) {
		donodelog_fmt(myrpt, l->hasconnected ? "LINKDISC,%s" : "LINKFAIL,%s", l->name);
	}
	if (l->hasconnected) {
		dodispgm(myrpt, l->name);
	}
	free_frame(&l->lastf1);
	free_frame(&l->lastf2);
	/* hang-up on call to device */
	ast_hangup(l->chan);
	ast_hangup(l->pchan);
	ast_free(l);
	rpt_mutex_lock(&myrpt->lock);
}

static inline void fac_frame(struct ast_frame *restrict f, float fac)
{
	int x1;
	short *sp;
	float fsamp;

	sp = (short *) f->data.ptr;
	for (x1 = 0; x1 < f->datalen / 2; x1++) {
		fsamp = (float) sp[x1] * fac;
		/* Prevent clipping */
		if (fsamp > 32765.0)
			fsamp = 32765.0;
		if (fsamp < -32765.0)
			fsamp = -32765.0;
		sp[x1] = (int) fsamp;
	}
}

static inline void rxkey_helper(struct rpt *myrpt, struct rpt_link *l)
{
	ast_debug(7, "@@@@ rx key\n");
	l->lastrealrx = 1;
	l->rerxtimer = 0;
	if (!l->lastrx1) {
		if (myrpt->p.archivedir) {
			donodelog_fmt(myrpt, "RXKEY,%s", l->name);
		}
		l->lastrx1 = 1;
		rpt_update_links(myrpt);
		time(&l->lastkeytime);
	}
}

static inline int process_link_channels(struct rpt *myrpt, struct ast_channel *who, int *restrict totx, char *restrict myfirst)
{
	int toexit = 0;
	struct rpt_link *l, *m;

	/* @@@@@ LOCK @@@@@ */
	rpt_mutex_lock(&myrpt->lock);
	l = myrpt->links.next;
	while (l != &myrpt->links) {
		int remnomute, remrx;
		struct timeval now;

		if (l->disctime) {
			l = l->next;
			continue;
		}

		remrx = 0;
		/* see if any other links are receiving */
		m = myrpt->links.next;
		while (m != &myrpt->links) {
			/* if not us, and not localonly count it */
			if ((m != l) && (m->lastrx) && (m->mode < 2))
				remrx = 1;
			m = m->next;
		}
		rpt_mutex_unlock(&myrpt->lock);
		now = ast_tvnow();
		if ((who == l->chan) || (!l->lastlinktv.tv_sec) || (ast_tvdiff_ms(now, l->lastlinktv) >= 19)) {
			char mycalltx;

			l->lastlinktv = now;
			remnomute = myrpt->localtx && (!(myrpt->cmdnode[0] || (myrpt->dtmfidx > -1)));
			mycalltx = myrpt->callmode;
#ifdef	DONT_USE__CAUSES_CLIPPING_OF_FIRST_SYLLABLE_ON_LINK
			if (myrpt->patchvoxalways)
				mycalltx = mycalltx && ((!myrpt->voxtostate) && myrpt->wasvox);
#endif
			*totx = ((l->isremote) ? (remnomute) : myrpt->localtx || mycalltx) || remrx;

			/* foop */
			if ((!l->lastrx) && altlink(myrpt, l))
				*totx = myrpt->txkeyed;
			if (altlink1(myrpt, l))
				*totx = 1;
			l->wouldtx = *totx;
			if (l->mode != 1)
				*totx = 0;
			if (l->phonemode == 0 && l->chan && (l->lasttx != *totx)) {
				if (*totx && !l->voterlink) {
					if (l->newkey < 2)
						ast_indicate(l->chan, AST_CONTROL_RADIO_KEY);
				} else {
					ast_indicate(l->chan, AST_CONTROL_RADIO_UNKEY);
				}
				if (myrpt->p.archivedir) {
					donodelog_fmt(myrpt, totx ? "TXKEY,%s" : "TXUNKEY,%s", l->name);
				}
			}
			l->lasttx = *totx;
		}
		rpt_mutex_lock(&myrpt->lock);
		if (who == l->chan) {	/* if it was a read from rx */
			struct ast_frame *f;
			rpt_mutex_unlock(&myrpt->lock);
			f = ast_read(l->chan);
			if (!f) {
				remote_hangup_helper(myrpt, l);
				break;
			}
			if (f->frametype == AST_FRAME_VOICE) {
				int ismuted, n1;
				float fac;

				dahdi_bump_buffers(l->pchan, f->samples); /* Make room if needed */

				fac = 1.0;
				if (l->chan && (!strcasecmp(ast_channel_tech(l->chan)->type, "echolink")))
					fac = myrpt->p.erxgain;
				if (l->chan && (!strcasecmp(ast_channel_tech(l->chan)->type, "tlb")))
					fac = myrpt->p.trxgain;
				if ((myrpt->p.linkmongain != 1.0) && (l->mode != 1) && (l->wouldtx))
					fac *= myrpt->p.linkmongain;
				if (fac != 1.0) {
					fac_frame(f, fac);
				}

				l->rxlingertimer = ((l->iaxkey) ? RX_LINGER_TIME_IAXKEY : RX_LINGER_TIME);

				if ((l->newkey == 2) && (!l->lastrealrx)) {
					rxkey_helper(myrpt, l);
				}
				if (((l->phonemode) && (l->phonevox)) || (!strcasecmp(ast_channel_tech(l->chan)->type, "echolink"))
					|| (!strcasecmp(ast_channel_tech(l->chan)->type, "tlb"))) {
					struct ast_frame *f1, *f2;
					if (l->phonevox) {
						int x;
						n1 = dovox(&l->vox, f->data.ptr, f->datalen / 2);
						if (n1 != l->wasvox) {
							ast_debug(1, "Link Node %s, vox %d\n", l->name, n1);
							l->wasvox = n1;
							l->voxtostate = 0;
							if (n1)
								l->voxtotimer = myrpt->p.voxtimeout_ms;
							else
								l->voxtotimer = 0;
						}
						if (l->lastrealrx || n1) {
							if (!*myfirst) {
								x = 0;
								AST_LIST_TRAVERSE(&l->rxq, f1, frame_list) x++;
								for (; x < myrpt->p.simplexphonedelay; x++) {
									f1 = ast_frdup(f);
									memset(f1->data.ptr, 0, f1->datalen);
									memset(&f1->frame_list, 0, sizeof(f1->frame_list));
									AST_LIST_INSERT_TAIL(&l->rxq, f1, frame_list);
								}
								*myfirst = 1;
							}
							f1 = ast_frdup(f);
							memset(&f1->frame_list, 0, sizeof(f1->frame_list));
							AST_LIST_INSERT_TAIL(&l->rxq, f1, frame_list);
						} else {
							*myfirst = 0;
						}
						x = 0;
						AST_LIST_TRAVERSE(&l->rxq, f1, frame_list) x++;
						if (!x) {
							memset(f->data.ptr, 0, f->datalen);
						} else {
							ast_frfree(f);
							f = AST_LIST_REMOVE_HEAD(&l->rxq, frame_list);
						}
					}
					ismuted = rpt_conf_get_muted(l->chan, myrpt);
					/* if not receiving, zero-out audio */
					ismuted |= (!l->lastrx);
					if (l->dtmfed && (l->phonemode || (!strcasecmp(ast_channel_tech(l->chan)->type, "echolink"))
						|| (!strcasecmp(ast_channel_tech(l->chan)->type, "tlb")))) {
						ismuted = 1;
					}
					l->dtmfed = 0;

					/* if a voting rx link and not the winner, mute audio */
					if (myrpt->p.votertype == 1 && l->voterlink && myrpt->voted_link != l) {
						ismuted = 1;
					}

					if (ismuted) {
						memset(f->data.ptr, 0, f->datalen);
						if (l->lastf1)
							memset(l->lastf1->data.ptr, 0, l->lastf1->datalen);
						if (l->lastf2)
							memset(l->lastf2->data.ptr, 0, l->lastf2->datalen);
					}
					if (f)
						f2 = ast_frdup(f);
					else
						f2 = NULL;
					f1 = l->lastf2;
					l->lastf2 = l->lastf1;
					l->lastf1 = f2;
					if (ismuted) {
						if (l->lastf1)
							memset(l->lastf1->data.ptr, 0, l->lastf1->datalen);
						if (l->lastf2)
							memset(l->lastf2->data.ptr, 0, l->lastf2->datalen);
					}
					if (f1) {
						ast_write(l->pchan, f1);
						ast_frfree(f1);
					}
				} else {
					/* if a voting rx link and not the winner, mute audio */
					if (myrpt->p.votertype == 1 && l->voterlink && myrpt->voted_link != l)
						ismuted = 1;
					else
						ismuted = 0;

					if (!l->lastrx || ismuted)
						memset(f->data.ptr, 0, f->datalen);
					ast_write(l->pchan, f);
				}
			} else if (f->frametype == AST_FRAME_DTMF_BEGIN) {
				if (l->lastf1)
					memset(l->lastf1->data.ptr, 0, l->lastf1->datalen);
				if (l->lastf2)
					memset(l->lastf2->data.ptr, 0, l->lastf2->datalen);
				l->dtmfed = 1;
			}
			if (f->frametype == AST_FRAME_TEXT) {
				char *tstr = ast_malloc(f->datalen + 1);
				if (tstr) {
					memcpy(tstr, f->data.ptr, f->datalen);
					tstr[f->datalen] = 0;
					handle_link_data(myrpt, l, tstr);
					ast_free(tstr);
				}
			}
			if (f->frametype == AST_FRAME_DTMF) {
				if (l->lastf1)
					memset(l->lastf1->data.ptr, 0, l->lastf1->datalen);
				if (l->lastf2)
					memset(l->lastf2->data.ptr, 0, l->lastf2->datalen);
				l->dtmfed = 1;
				handle_link_phone_dtmf(myrpt, l, f->subclass.integer);
			}
			if (f->frametype == AST_FRAME_CONTROL) {
				if (f->subclass.integer == AST_CONTROL_ANSWER) {
					char lconnected = l->connected;

					__kickshort(myrpt);
					myrpt->rxlingertimer = ((myrpt->iaxkey) ? RX_LINGER_TIME_IAXKEY : RX_LINGER_TIME);
					l->connected = 1;
					l->hasconnected = 1;
					l->thisconnected = 1;
					l->elaptime = -1;
					if (!l->phonemode) {
						rpt_mutex_lock(&myrpt->blocklock);
						send_newkey(l->chan);
						rpt_mutex_unlock(&myrpt->blocklock);
					}
					if (!l->isremote)
						l->retries = 0;
					if (!lconnected) {
						rpt_telemetry(myrpt, CONNECTED, l);
						if (myrpt->p.archivedir) {
							if (l->mode == 1) {
								donodelog_fmt(myrpt, "LINKTRX,%s", l->name);
							} else if (l->mode > 1) {
								donodelog_fmt(myrpt, "LINKLOCALMONITOR,%s", l->name);
							} else {
								donodelog_fmt(myrpt, "LINKMONITOR,%s", l->name);
							}
						}
						rpt_update_links(myrpt);
						doconpgm(myrpt, l->name);
					} else
						l->reconnects++;
				}
				/* if RX key */
				if ((f->subclass.integer == AST_CONTROL_RADIO_KEY) && (l->newkey < 2)) {
					rxkey_helper(myrpt, l);
				}
				/* if RX un-key */
				if (f->subclass.integer == AST_CONTROL_RADIO_UNKEY) {
					rxunkey_helper(myrpt, l);
				}
				if (f->subclass.integer == AST_CONTROL_HANGUP) {
					ast_frfree(f);
					remote_hangup_helper(myrpt, l);
				}
			}
			ast_frfree(f);
			rpt_mutex_lock(&myrpt->lock);
			break;
		} else if (who == l->pchan) {
			struct ast_frame *f;
			rpt_mutex_unlock(&myrpt->lock);
			f = ast_read(l->pchan);
			if (!f) {
				ast_debug(1, "@@@@ rpt:Hung Up\n");
				toexit = 1;
				rpt_mutex_lock(&myrpt->lock);
				break;
			}
			if (f->frametype == AST_FRAME_VOICE) {
				float fac = 1.0;
				if (l->chan && (!strcasecmp(ast_channel_tech(l->chan)->type, "echolink")))
					fac = myrpt->p.etxgain;
				if (l->chan && (!strcasecmp(ast_channel_tech(l->chan)->type, "tlb")))
					fac = myrpt->p.ttxgain;

				if (fac != 1.0) {
					fac_frame(f, fac);
				}
				/* foop */
				if (l->chan && (l->lastrx || (!altlink(myrpt, l))) && ((l->newkey < 2) || l->lasttx || strcasecmp(ast_channel_tech(l->chan)->type, "IAX2"))) {
					/* Reverse-engineering comments from NA debugging issue #46:
					 * We may be receiving frames from channel drivers but we discard them and don't pass them on if newkey hasn't been set to 2 yet.
					 * Of course if handle_link_data is never called to set newkey to 2 and stop newkeytimer, then at some point, we'll
					 * set newkey = 0 forcibly (see comments in that part of the code for more info), which will cause us to start passing on the voice frames here.
					 * If this happens, then we're passing voice frames so we're keyed up and transmitting, essentially.
					 */
					ast_write(l->chan, f);
				}
			}
			if (f->frametype == AST_FRAME_CONTROL) {
				if (f->subclass.integer == AST_CONTROL_HANGUP) {
					ast_debug(1, "@@@@ rpt:Hung Up\n");
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
	return toexit;
}

static inline int monchannel_read(struct rpt *myrpt)
{
	struct ast_frame *f = ast_read(myrpt->monchannel);
	if (!f) {
		ast_debug(1, "@@@@ rpt:Hung Up\n");
		return -1;
	}
	if (f->frametype == AST_FRAME_VOICE) {
		struct ast_frame *fs;
		float fac;
		struct rpt_link *l = &myrpt->links;

		if ((myrpt->p.duplex > 1) || (myrpt->txkeyed)) {
			if (myrpt->monstream)
				ast_writestream(myrpt->monstream, f);
		}
		if (((myrpt->p.duplex >= 2) || (!myrpt->keyed)) && myrpt->p.outstreamcmd
			&& (myrpt->outstreampipe[1] != -1)) {
			outstream_write(myrpt, f);
		}
		fs = ast_frdup(f);
		fac = 1.0;
		if (l->chan && (!strcasecmp(ast_channel_tech(l->chan)->type, "echolink")))
			fac = myrpt->p.etxgain;
		if (fac != 1.0) {
			fac_frame(fs, fac);
		}
		l = myrpt->links.next;
		/* go thru all the links */
		while (l != &myrpt->links) {
			/* foop */
			if (l->chan && altlink(myrpt, l) && (!l->lastrx) && ((l->newkey < 2) || l->lasttx || strcasecmp(ast_channel_tech(l->chan)->type, "IAX2"))) {
				if (l->chan && (!strcasecmp(ast_channel_tech(l->chan)->type, "irlp"))) {
					ast_write(l->chan, fs);
				} else {
					ast_write(l->chan, f);
				}
			}
			l = l->next;
		}
		ast_frfree(fs);
	}
	return hangup_frame_helper(myrpt->monchannel, "monchannel", f);
}

static inline int parrotchannel_read(struct rpt *myrpt)
{
	struct ast_frame *f = ast_read(myrpt->parrotchannel);
	if (!f) {
		ast_debug(1, "@@@@ rpt:Hung Up\n");
		return -1;
	}
	if (!(myrpt->p.parrotmode || myrpt->parrotonce)) {
		char myfname[300];

		if (myrpt->parrotstream) {
			ast_closestream(myrpt->parrotstream);
			myrpt->parrotstream = 0;
		}
		sprintf(myfname, PARROTFILE, myrpt->name, myrpt->parrotcnt);
		strcat(myfname, ".wav");
		unlink(myfname);
	} else if (f->frametype == AST_FRAME_VOICE) {
		if (myrpt->parrotstream)
			ast_writestream(myrpt->parrotstream, f);
	}
	return hangup_frame_helper(myrpt->parrotchannel, "parrotchannel", f);
}

static inline int voxchannel_read(struct rpt *myrpt)
{
	struct ast_frame *f = ast_read(myrpt->voxchannel);
	if (!f) {
		ast_debug(1, "@@@@ rpt:Hung Up\n");
		return -1;
	}
	if (f->frametype == AST_FRAME_VOICE) {
		int n = dovox(&myrpt->vox, f->data.ptr, f->datalen / 2);
		if (n != myrpt->wasvox) {
			ast_debug(1, "Node %s, vox %d\n", myrpt->name, n);
			myrpt->wasvox = n;
			myrpt->voxtostate = 0;
			if (n)
				myrpt->voxtotimer = myrpt->p.voxtimeout_ms;
			else
				myrpt->voxtotimer = 0;
		}
	}
	return hangup_frame_helper(myrpt->voxchannel, "voxchannel", f);
}

static inline int txpchannel_read(struct rpt *myrpt)
{
	return wait_for_hangup_helper(myrpt->txpchannel, "txpchannel");
}

static inline int telechannel_read(struct rpt *myrpt, int complexcondition)
{
	struct ast_frame *f;

	if (debug) {
		ast_debug(10, "node=%s %p %d %d %d\n", myrpt->name, myrpt->telechannel, myrpt->keyed, myrpt->remrx, myrpt->noduck);
	}
	f = ast_read(myrpt->telechannel);
	if (!f) {
		ast_debug(1, "node=%s telechannel Hung Up implied\n", myrpt->name);
		return -1;
	}
	if (f->frametype == AST_FRAME_VOICE) {
		float gain;
		if (complexcondition) {
#ifdef SIMPLE_VOTER_IMPLEMENTED
			/* New condition once implemented: */
			/* This is for when/if simple voter is implemented.  It replaces the line below it. */
			gain = !myrpt->noduck && (myrpt->rxchankeyed || myrpt->remrx) ? myrpt->p.telemduckgain : myrpt->p.telemnomgain;
#else
			gain = !myrpt->noduck && (myrpt->keyed || myrpt->remrx) ? myrpt->p.telemduckgain : myrpt->p.telemnomgain;
#endif
		} else {
			gain = myrpt->keyed ? myrpt->p.telemduckgain : myrpt->p.telemnomgain;
		}
		if (gain != 0) {
			int n, k;
			short *sp = (short *) f->data.ptr;
			for (n = 0; n < f->datalen / 2; n++) {
				k = sp[n] * gain;
				if (k > 32767)
					k = 32767;
				else if (k < -32767)
					k = -32767;
				sp[n] = k;
			}
		}
		ast_write(myrpt->btelechannel, f);
	}
	if (f->frametype == AST_FRAME_CONTROL) {
		if (f->subclass.integer == AST_CONTROL_HANGUP) {
			ast_debug(6, "node=%s telechannel Hung Up\n", myrpt->name);
			ast_frfree(f);
			return -1;
		}
	}
	ast_frfree(f);
	return 0;
}

static inline int btelechannel_read(struct rpt *myrpt)
{
	return wait_for_hangup_helper(myrpt->btelechannel, "btelechannel");
}

static inline void voxtostate_to_voxtotimer(struct rpt *myrpt)
{
	if (myrpt->voxtostate) {
		myrpt->voxtotimer = myrpt->p.voxtimeout_ms;
		myrpt->voxtostate = 0;
	} else {
		myrpt->voxtotimer = myrpt->p.voxrecover_ms;
		myrpt->voxtostate = 1;
	}
}

/* single thread with one file (request) to dial */
static void *rpt(void *this)
{
	struct rpt *myrpt = this;
	char *idtalkover, c, myfirst;
	int ms = MSWAIT, lasttx = 0, lastexttx = 0, lastpatchup = 0, val, identqueued, othertelemqueued;
	int tailmessagequeued, ctqueued, lastmyrx, localmsgqueued;
	unsigned int u;
	FILE *fp;
	struct stat mystat;
	struct ast_channel *who;
	time_t t, was;
	struct rpt_link *l;
	struct rpt_tele *telem;
	char tmpstr[512], lat[100], lon[100], elev[100];
	struct ast_format_cap *cap;
	struct timeval looptimestart;

	if (myrpt->p.archivedir)
		mkdir(myrpt->p.archivedir, 0700);
	sprintf(tmpstr, "%s/%s", myrpt->p.archivedir, myrpt->name);
	mkdir(tmpstr, 0775);
	myrpt->ready = 0;
	rpt_mutex_lock(&myrpt->lock);
	myrpt->remrx = 0;
	myrpt->remote_webtransceiver = 0;

	telem = myrpt->tele.next;
	while (telem != &myrpt->tele) {
		ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);
		telem = telem->next;
	}
	rpt_mutex_unlock(&myrpt->lock);

	load_rpt_vars_by_rpt(myrpt, 0);

	rpt_mutex_lock(&myrpt->lock);
	while (myrpt->xlink) {
		myrpt->xlink = 3;
		rpt_mutex_unlock(&myrpt->lock);
		usleep(100000);
		rpt_mutex_lock(&myrpt->lock);
	}
#ifdef HAVE_SYS_IO
	if ((!strcmp(myrpt->remoterig, REMOTE_RIG_RBI)) && (ioperm(myrpt->p.iobase, 1, 1) == -1)) {
		rpt_mutex_unlock(&myrpt->lock);
		ast_log(LOG_WARNING, "Can't get io permission on IO port %x hex\n", myrpt->p.iobase);
#else
	if ((!strcmp(myrpt->remoterig, REMOTE_RIG_RBI))) {
		rpt_mutex_unlock(&myrpt->lock);
		ast_log(LOG_ERROR, "ioperm(%x) not supported on this architecture\n", myrpt->p.iobase);
#endif
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		rpt_mutex_unlock(&myrpt->lock);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}

	ast_format_cap_append(cap, ast_format_slin, 0);

	if (rpt_setup_channels(myrpt, cap)) {
		rpt_mutex_unlock(&myrpt->lock);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		disable_rpt(myrpt); /* Disable repeater */
		ao2_ref(cap, -1);
		pthread_exit(NULL);
	}

	ao2_ref(cap, -1);

	/* if serial io port, open it */
	myrpt->iofd = -1;
	if (myrpt->p.ioport && ((myrpt->iofd = openserial(myrpt, myrpt->p.ioport)) == -1)) {
		ast_log(LOG_ERROR, "Unable to open %s\n", myrpt->p.ioport);
		rpt_mutex_unlock(&myrpt->lock);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup_rx_tx(myrpt);
		pthread_exit(NULL);
	}
	/* Now, the idea here is to copy from the physical rx channel buffer
	   into the pseudo tx buffer, and from the pseudo rx buffer into the
	   tx channel buffer */
	rpt_links_init(&myrpt->links);
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
	voxinit_rpt(myrpt, 1);
	myrpt->wasvox = 0;
	myrpt->linkactivityflag = 0;
	myrpt->linkactivitytimer = 0;
	myrpt->vote_counter = 10;
	myrpt->rptinactwaskeyedflag = 0;
	myrpt->rptinacttimer = 0;
	if (myrpt->p.rxburstfreq) {
#ifdef NATIVE_DSP
		if (!(myrpt->dsp = ast_dsp_new())) {
			ast_log(LOG_WARNING, "Unable to allocate DSP!\n");
			rpt_hangup(myrpt, RPT_RXCHAN);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
		/* \todo At this point, we have a memory leak, because dsp needs to be freed. */
		/* \todo Find out what the right place is to free dsp, i.e. when myrpt itself goes away. */
		ast_dsp_set_features(myrpt->dsp, DSP_FEATURE_FREQ_DETECT);
		ast_dsp_set_freqmode(myrpt->dsp, myrpt->p.rxburstfreq, myrpt->p.rxbursttime, myrpt->p.rxburstthreshold, 0);
#else
		tone_detect_init(&myrpt->burst_tone_state, myrpt->p.rxburstfreq, myrpt->p.rxbursttime, myrpt->p.rxburstthreshold);
#endif
	}
	if (myrpt->p.startupmacro) {
		snprintf(myrpt->macrobuf, MAXMACRO - 1, "PPPP%s", myrpt->p.startupmacro);
	}
	/* @@@@@@@ UNLOCK @@@@@@@ */
	rpt_mutex_unlock(&myrpt->lock);
	val = 1;
	ast_channel_setoption(myrpt->rxchannel, AST_OPTION_RELAXDTMF, &val, sizeof(char), 0);
	val = 1;
	ast_channel_setoption(myrpt->rxchannel, AST_OPTION_TONE_VERIFY, &val, sizeof(char), 0);
	if (myrpt->p.archivedir)
		donodelog(myrpt, "STARTUP");
	if (myrpt->remoterig && !ISRIG_RTX(myrpt->remoterig))
		setrem(myrpt);
	/* wait for telem to be done */
	while ((ms >= 0) && (myrpt->tele.next != &myrpt->tele)) {
		rpt_mutex_lock(&myrpt->blocklock);
		if (ast_safe_sleep(myrpt->rxchannel, 50) == -1) {
			ms = -1;
		}
		rpt_mutex_unlock(&myrpt->blocklock);
	}
	lastmyrx = 0;
	myfirst = 0;
	myrpt->lastitx = -1;
	rpt_update_boolean(myrpt, "RPT_RXKEYED", -1);
	rpt_update_boolean(myrpt, "RPT_TXKEYED", -1);
	rpt_update_boolean(myrpt, "RPT_ETXKEYED", -1);
	rpt_update_boolean(myrpt, "RPT_AUTOPATCHUP", -1);
	rpt_update_boolean(myrpt, "RPT_NUMLINKS", -1);
	rpt_update_boolean(myrpt, "RPT_NUMALINKS", -1);
	rpt_update_boolean(myrpt, "RPT_LINKS", -1);
	rpt_update_boolean(myrpt, "RPT_ALINKS", -1);
	myrpt->ready = 1;
	looptimestart = ast_tvnow();

	while (ms >= 0) {
		struct ast_channel *cs[300], *cs1[300];
		int totx = 0, elap = 0, n, x;
		struct timeval looptimenow;

		if (myrpt->disgorgetime && (time(NULL) >= myrpt->disgorgetime)) {
			myrpt->disgorgetime = 0;
			dump_rpt(myrpt, lasttx, lastexttx, elap, totx); /* Debug Dump */
		}

		if (myrpt->reload) {
			struct rpt_tele *telem;

			rpt_mutex_lock(&myrpt->lock);
			telem = myrpt->tele.next;
			while (telem != &myrpt->tele) {
				ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);
				telem = telem->next;
			}
			myrpt->reload = 0;
			rpt_mutex_unlock(&myrpt->lock);
			usleep(10000);
			load_rpt_vars_by_rpt(myrpt, 1);
		}

		if (!myrpt->rxchannel) {
			ast_debug(1, "RPT rxchannel disappeared?\n"); /* This could happen if we call stop_repeaters() */
			break;
		}

		if (rpt_any_hangups(myrpt)) {
			break;
		}

		time(&t);
		while (t >= (myrpt->lastgpstime + GPS_UPDATE_SECS)) {
			myrpt->lastgpstime = t;
			fp = fopen(GPSFILE, "r");
			if (!fp)
				break;
			if (fstat(fileno(fp), &mystat) == -1)
				break;
			if (mystat.st_size >= 100)
				break;
			elev[0] = 0;
			if (fscanf(fp, "%u %s %s %s", &u, lat, lon, elev) < 3)
				break;
			fclose(fp);
			was = (time_t) u;
			if ((was + GPS_VALID_SECS) < t)
				break;
			sprintf(tmpstr, "G %s %s %s %s", myrpt->name, lat, lon, elev);
			rpt_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
			myrpt->voteremrx = 0;	/* no voter remotes keyed */
			while (l != &myrpt->links) {
				if (l->chan)
					ast_sendtext(l->chan, tmpstr);
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
		/* @@@@@@@ LOCK @@@@@@@ */
		rpt_mutex_lock(&myrpt->lock);

		/* If someone's connected, and they're transmitting from their end to us, set remrx true */
		l = myrpt->links.next;
		myrpt->remrx = 0;
		while (l != &myrpt->links) {
			if (l->lastrx) {
				myrpt->remrx = 1;
				if (l->voterlink)
					myrpt->voteremrx = 1;
				if ((l->name[0] > '0') && (l->name[0] <= '9'))	/* Ignore '0' nodes */
					strcpy(myrpt->lastnodewhichkeyedusup, l->name);	/* Note the node which is doing the key up */
			}
			l = l->next;
		}
		if (myrpt->p.s[myrpt->p.sysstate_cur].sleepena) {	/* If sleep mode enabled */
			if (myrpt->remrx) {	/* signal coming from net wakes up system */
				myrpt->sleeptimer = myrpt->p.sleeptime;	/* reset sleep timer */
				if (myrpt->sleep) {	/* if asleep, then awake */
					myrpt->sleep = 0;
				}
			} else if (myrpt->keyed) {	/* if signal on input */
				if (!myrpt->sleep) {	/* if not sleeping */
					myrpt->sleeptimer = myrpt->p.sleeptime;	/* reset sleep timer */
				}
			}

			if (myrpt->sleep)
				myrpt->localtx = 0;	/* No RX if asleep */
			else
				myrpt->localtx = myrpt->keyed;	/* Set localtx to keyed state if awake */
		} else {
			myrpt->localtx = myrpt->keyed;	/* If sleep disabled, just copy keyed state to localrx */
		}
		/* Create a "must_id" flag for the cleanup ID */
		if (myrpt->p.idtime)	/* ID time must be non-zero */
			myrpt->mustid |= (myrpt->idtimer) && (myrpt->keyed || myrpt->remrx);
		if (myrpt->keyed || myrpt->remrx) {
			/* Set the inactivity was keyed flag and reset its timer */
			myrpt->rptinactwaskeyedflag = 1;
			myrpt->rptinacttimer = 0;
		}
		/* Build a fresh totx from myrpt->keyed and autopatch activated */
		/* If full duplex, add local tx to totx */

		if ((myrpt->p.duplex > 1) && (!myrpt->patchvoxalways)) {
			totx = myrpt->callmode;
		} else {
			int myrx = myrpt->localtx || myrpt->remrx || (!myrpt->callmode);

			if (lastmyrx != myrx) {
				if (myrpt->p.duplex < 2)
					voxinit_rpt(myrpt, !myrx);
				lastmyrx = myrx;
			}
			totx = 0;
			if (myrpt->callmode && (myrpt->voxtotimer <= 0)) {
				voxtostate_to_voxtotimer(myrpt);
			}
			if (!myrpt->voxtostate)
				totx = myrpt->callmode && myrpt->wasvox;
		}
		if (myrpt->p.duplex > 1) {
			totx = totx || myrpt->localtx;
		}

		/* Traverse the telemetry list to see what's queued */
		identqueued = 0;
		localmsgqueued = 0;
		othertelemqueued = 0;
		tailmessagequeued = 0;
		ctqueued = 0;
		telem = myrpt->tele.next;
		while (telem != &myrpt->tele) {
			if (telem->mode == SETREMOTE) {
				telem = telem->next;
				continue;
			}
			if ((telem->mode == ID) || (telem->mode == IDTALKOVER)) {
				identqueued = 1;	/* Identification telemetry */
			} else if (telem->mode == TAILMSG) {
				tailmessagequeued = 1;	/* Tail message telemetry */
			} else if ((telem->mode == STATS_TIME_LOCAL) || (telem->mode == LOCALPLAY)) {
				localmsgqueued = 1;	/* Local message */
			} else {
				if ((telem->mode != UNKEY) && (telem->mode != LINKUNKEY))
					othertelemqueued = 1;	/* Other telemetry */
				else
					ctqueued = 1;	/* Courtesy tone telemetry */
			}
			telem = telem->next;
		}

		/* Add in any "other" telemetry, unless specified otherwise */
		if (!myrpt->p.notelemtx)
			totx = totx || othertelemqueued;
		/* Update external (to links) transmitter PTT state with everything but */
		/* ID, CT, local messages, and tailmessage telemetry */
		myrpt->exttx = totx;
		if (myrpt->localoverride)
			totx = 1;
		totx = totx || myrpt->dtmf_local_timer;
		/* If half or 3/4 duplex, add localtx to external link tx */
		if (myrpt->p.duplex < 2)
			myrpt->exttx = myrpt->exttx || myrpt->localtx;
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
		if (myrpt->p.duplex > 1) {
			totx = totx ||		/* (myrpt->dtmfidx > -1) || */
				(myrpt->cmdnode[0] && strcmp(myrpt->cmdnode, "aprstt"));
		}
		/* add in parrot stuff */
		totx = totx || (myrpt->parrotstate > 1);
		/* Reset time out timer variables if there is no activity */
		if (!totx) {
			myrpt->totimer = myrpt->p.totime;
			myrpt->tounkeyed = 0;
			myrpt->tonotify = 0;
		} else {
			myrpt->tailtimer = myrpt->p.s[myrpt->p.sysstate_cur].alternatetail ? myrpt->p.althangtime :	/* Initialize tail timer */
				myrpt->p.hangtime;

		}
		/* if in 1/2 or 3/4 duplex, give rx priority */
		if ((myrpt->p.duplex < 2) && (myrpt->keyed) && (!myrpt->p.linktolink) && (!myrpt->p.dias))
			totx = 0;
		/* Disable the local transmitter if we are timed out */
		totx = totx && myrpt->totimer;
		/* if timed-out and not said already, say it */
		if ((!myrpt->totimer) && (!myrpt->tonotify)) {
			myrpt->tonotify = 1;
			myrpt->timeouts++;
			rpt_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt, TIMEOUT, NULL);
			rpt_mutex_lock(&myrpt->lock);
		}

		/* If unkey and re-key, reset time out timer */
		if ((!totx) && (!myrpt->totimer) && (!myrpt->tounkeyed) && (!myrpt->keyed)) {
			myrpt->tounkeyed = 1;
		}
		if ((!totx) && (!myrpt->totimer) && myrpt->tounkeyed && myrpt->keyed) {
			myrpt->totimer = myrpt->p.totime;
			myrpt->tounkeyed = 0;
			myrpt->tonotify = 0;
			rpt_mutex_unlock(&myrpt->lock);
			continue;
		}
		/* if timed-out and in circuit busy after call */
		if ((!totx) && (!myrpt->totimer) && (myrpt->callmode == 4)) {
			ast_debug(1, "timed-out and in circuit busy after call\n");
			myrpt->callmode = 0;
			myrpt->macropatch = 0;
			channel_revert(myrpt);
		}
		/* get rid of tail if timed out or beaconing */
		if (!myrpt->totimer || (!myrpt->mustid && myrpt->p.beaconing)) myrpt->tailtimer = 0;
		/* if not timed-out, add in tail */
		if (myrpt->totimer)
			totx = totx || myrpt->tailtimer;
		/* If user or links key up or are keyed up over standard ID, switch to talkover ID, if one is defined */
		/* If tail message, kill the message if someone keys up over it */
		if ((myrpt->keyed || myrpt->remrx || myrpt->localoverride)
			&& ((identqueued && idtalkover) || (tailmessagequeued))) {
			int hasid = 0, hastalkover = 0;

			telem = myrpt->tele.next;
			while (telem != &myrpt->tele) {
				if (telem->mode == ID && !telem->killed) {
					if (telem->chan)
						ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);	/* Whoosh! */
					telem->killed = 1;
					hasid = 1;
				}
				if (telem->mode == TAILMSG && !telem->killed) {
					if (telem->chan)
						ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);	/* Whoosh! */
					telem->killed = 1;
				}
				if (telem->mode == IDTALKOVER)
					hastalkover = 1;
				telem = telem->next;
			}
			if (hasid && !hastalkover) {
				ast_debug(6, "Tracepoint IDTALKOVER\n");
				rpt_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt, IDTALKOVER, NULL);	/* Start Talkover ID */
				rpt_mutex_lock(&myrpt->lock);
			}
		}
		/* Try to be polite */
		/* If the repeater has been inactive for longer than the ID time, do an initial ID in the tail */
		/* If within 30 seconds of the time to ID, try do it in the tail */
		/* else if at ID time limit, do it right over the top of them */
		/* If beaconing is enabled, always id when the timer expires */
		/* Lastly, if the repeater has been keyed, and the ID timer is expired, do a clean up ID */
		if (((myrpt->mustid) || (myrpt->p.beaconing)) && (!myrpt->idtimer))
			queue_id(myrpt);

		if ((myrpt->p.idtime && totx && (!myrpt->exttx) && (myrpt->idtimer <= myrpt->p.politeid) && myrpt->tailtimer)) {	/* ID time must be non-zero */
			myrpt->tailid = 1;
		}

		/* If tail timer expires, then check for tail messages */

		if (myrpt->tailevent) {
			myrpt->tailevent = 0;
			if (myrpt->tailid) {
				totx = 1;
				queue_id(myrpt);
			} else if ((myrpt->p.tailmessages[0]) && (myrpt->p.tailmessagetime) && (myrpt->tmsgtimer == 0)) {
				totx = 1;
				myrpt->tmsgtimer = myrpt->p.tailmessagetime;
				rpt_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt, TAILMSG, NULL);
				rpt_mutex_lock(&myrpt->lock);
			}
		}

		/* Main TX control */

		/* let telemetry transmit anyway (regardless of timeout) */
		if (myrpt->p.duplex > 0)
			totx = totx || (myrpt->tele.next != &myrpt->tele);
		totx = totx && !myrpt->p.s[myrpt->p.sysstate_cur].txdisable;
		myrpt->txrealkeyed = totx;
		totx = totx || (!AST_LIST_EMPTY(&myrpt->txq));
		/* if in 1/2 or 3/4 duplex, give rx priority */
		if ((myrpt->p.duplex < 2) && (!myrpt->p.linktolink) && (!myrpt->p.dias) && (myrpt->keyed))
			totx = 0;
		if (myrpt->p.elke && (myrpt->elketimer > myrpt->p.elke))
			totx = 0;
		if (totx && !lasttx) {
			log_keyed(myrpt);
			lasttx = 1;
		}
		if (!totx && lasttx) {
			lasttx = 0;
			log_unkeyed(myrpt);
		}
		time(&t);
		/* if DTMF timeout */
		if (((!myrpt->cmdnode[0]) || (!strcmp(myrpt->cmdnode, "aprstt"))) && (myrpt->dtmfidx >= 0)
			&& ((myrpt->dtmf_time + DTMF_TIMEOUT) < t)) {
			cancel_pfxtone(myrpt);
			myrpt->inpadtest = 0;
			myrpt->dtmfidx = -1;
			myrpt->cmdnode[0] = 0;
			myrpt->dtmfbuf[0] = 0;
		}
		/* if remote DTMF timeout */
		if ((myrpt->rem_dtmfidx >= 0) && ((myrpt->rem_dtmf_time + DTMF_TIMEOUT) < t)) {
			myrpt->inpadtest = 0;
			myrpt->rem_dtmfidx = -1;
			myrpt->rem_dtmfbuf[0] = 0;
		}

		if (myrpt->exttx && myrpt->parrotchannel && (myrpt->p.parrotmode || myrpt->parrotonce) && (!myrpt->parrotstate)) {
			char myfname[300];

			/* first put the channel on the conference in announce mode */
			if (rpt_conf_add_announcer_monitor(myrpt->parrotchannel, myrpt)) {
				ast_mutex_unlock(&myrpt->lock);
				break;
			}

			sprintf(myfname, PARROTFILE, myrpt->name, myrpt->parrotcnt);
			strcat(myfname, ".wav");
			unlink(myfname);
			sprintf(myfname, PARROTFILE, myrpt->name, myrpt->parrotcnt);
			myrpt->parrotstate = 1;
			myrpt->parrottimer = myrpt->p.parrottime;
			if (myrpt->parrotstream)
				ast_closestream(myrpt->parrotstream);
			myrpt->parrotstream = NULL;
			myrpt->parrotstream = ast_writefile(myfname, "wav", "app_rpt Parrot", O_CREAT | O_TRUNC, 0, 0600);
		}

		if (myrpt->exttx != lastexttx) {
			lastexttx = myrpt->exttx;
			rpt_update_boolean(myrpt, "RPT_ETXKEYED", lastexttx);
		}

		if (((myrpt->callmode != 0)) != lastpatchup) {
			lastpatchup = ((myrpt->callmode != 0));
			rpt_update_boolean(myrpt, "RPT_AUTOPATCHUP", lastpatchup);
		}

		/* Reconnect */

		l = myrpt->links.next;
		while (l != &myrpt->links) {
			if (l->killme) {
				/* remove from queue */
				rpt_link_remove(myrpt, l);
				if (!strcmp(myrpt->cmdnode, l->name))
					myrpt->cmdnode[0] = 0;
				rpt_mutex_unlock(&myrpt->lock);
				/* hang-up on call to device */
				if (l->chan)
					ast_hangup(l->chan);
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
		if (x != myrpt->lastitx) {
			char str[16];

			myrpt->lastitx = x;
			if (myrpt->p.itxctcss) {
				if (IS_DAHDI_CHAN(myrpt->rxchannel)) {
					dahdi_radio_set_ctcss_encode(myrpt->dahdirxchannel, !x);
				} else if (!strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "radio") ||
					!strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "simpleusb")) {
					sprintf(str, "TXCTCSS %d", !(!x));
					ast_sendtext(myrpt->rxchannel, str);
				}
			}
		}
		n = 0;
		cs[n++] = myrpt->rxchannel;
		cs[n++] = myrpt->pchannel;
		cs[n++] = myrpt->monchannel;
		cs[n++] = myrpt->telechannel;
		cs[n++] = myrpt->btelechannel;

		if (myrpt->parrotchannel)
			cs[n++] = myrpt->parrotchannel;
		if (myrpt->voxchannel)
			cs[n++] = myrpt->voxchannel;
		cs[n++] = myrpt->txpchannel;
		if (myrpt->txchannel != myrpt->rxchannel)
			cs[n++] = myrpt->txchannel;
		if (myrpt->dahditxchannel != myrpt->txchannel)
			cs[n++] = myrpt->dahditxchannel;
		l = myrpt->links.next;
		while (l != &myrpt->links) {
			if ((!l->killme) && (!l->disctime) && l->chan) {
				cs[n++] = l->chan;
				cs[n++] = l->pchan;
			}
			l = l->next;
		}
		if ((myrpt->topkeystate == 1) && ((t - myrpt->topkeytime) > TOPKEYWAIT)) {
			myrpt->topkeystate = 2;
			qsort(myrpt->topkey, TOPKEYN, sizeof(struct rpt_topkey), topcompar);
		}
		/* @@@@@@ UNLOCK @@@@@@@@ */
		rpt_mutex_unlock(&myrpt->lock);

		if (myrpt->topkeystate == 2) {
			rpt_telemetry(myrpt, TOPKEY, NULL);
			myrpt->topkeystate = 3;
		}
		ms = MSWAIT;
		for (x = 0; x < n; x++) {
			int s = -(-x - myrpt->scram - 1) % n;
			cs1[x] = cs[s];
		}
		myrpt->scram++;
		rpt_mutex_lock(&myrpt->blocklock);
		who = ast_waitfor_n(cs1, n, &ms);
		rpt_mutex_unlock(&myrpt->blocklock);
		if (who == NULL) {
			ms = 0;
		}
		/* calculate loop time */
		looptimenow = ast_tvnow();
		elap = ast_tvdiff_ms(looptimenow, looptimestart);
		if (elap != 0) {
			looptimestart = looptimenow;
		}
		/* @@@@@@ LOCK @@@@@@@ */
		rpt_mutex_lock(&myrpt->lock);
		periodic_process_links(myrpt, elap);
		if (update_timers(myrpt, elap, totx)) {
			break;
		}
		if (!ms) {
			/* No channels had activity before the timer expired,
			 * so just continue to the next loop. */
			rpt_mutex_unlock(&myrpt->lock);
			continue;
		}
		if ((myrpt->p.parrotmode || myrpt->parrotonce) && myrpt->parrotstate == 1 && myrpt->parrottimer <= 0) {
			if (update_parrot(myrpt)) {
				break;
			}
		}
		if (myrpt->cmdAction.state == CMD_STATE_READY) {	/* there is a command waiting to be processed */
			process_command(myrpt);
		}

		c = myrpt->macrobuf[0];
		time(&t);
		if (c && (!myrpt->macrotimer) && starttime && (t > (starttime + START_DELAY))) {
			char cin = c & 0x7f;
			myrpt->macrotimer = MACROTIME;
			memmove(myrpt->macrobuf, myrpt->macrobuf + 1, MAXMACRO - 1);
			if ((cin == 'p') || (cin == 'P'))
				myrpt->macrotimer = MACROPTIME;
			rpt_mutex_unlock(&myrpt->lock);
			if (myrpt->p.archivedir) {
				donodelog_fmt(myrpt, "DTMF(M),MAIN,%c", cin);
			}
			local_dtmf_helper(myrpt, c);
		} else {
			rpt_mutex_unlock(&myrpt->lock);
		}
		/* @@@@@@ UNLOCK @@@@@ */

		if (who == myrpt->rxchannel) {	/* if it was a read from rx */
			if (rxchannel_read(myrpt, lasttx)) {
				break;
			}
			continue;
		} else if (who == myrpt->pchannel) {	/* if it was a read from pseudo */
			if (pchannel_read(myrpt)) {
				break;
			}
			continue;
		} else if (who == myrpt->txchannel) {	/* if it was a read from tx */
			if (txchannel_read(myrpt)) {
				break;
			}
			continue;
		} else if (who == myrpt->dahditxchannel) {	/* if it was a read from pseudo-tx */
			if (dahditxchannel_read(myrpt, &myfirst)) {
				break;
			}
			continue;
		}

		if (process_link_channels(myrpt, who, &totx, &myfirst)) {
			break;
		}

		if (who == myrpt->monchannel) {
			if (monchannel_read(myrpt)) {
				break;
			}
		} else if (myrpt->parrotchannel && who == myrpt->parrotchannel) {
			if (parrotchannel_read(myrpt)) {
				break;
			}
		} else if (myrpt->voxchannel && who == myrpt->voxchannel) {
			if (voxchannel_read(myrpt)) {
				break;
			}
		} else if (who == myrpt->txpchannel) {	/* if it was a read from remote tx */
			if (txpchannel_read(myrpt)) {
				break;
			}
		} else if (who == myrpt->telechannel) {	/* if is telemetry conference output */
			if (telechannel_read(myrpt, 1)) {
				break;
			}
		} else if (who == myrpt->btelechannel) { /* if is btelemetry conference output */
			if (btelechannel_read(myrpt)) {
				break;
			}
		}
	}

	/* Terminate and cleanup app_rpt node instance */
	ast_debug(1, "%s disconnected, cleaning up...\n", myrpt->name);

	myrpt->ready = 0;
	usleep(100000);
	/* wait for telem to be done */
	while (myrpt->tele.next != &myrpt->tele)
		usleep(50000);
	rpt_hangup(myrpt, RPT_PCHAN);
	rpt_hangup(myrpt, RPT_MONCHAN);
	if (myrpt->parrotchannel) {
		rpt_hangup(myrpt, RPT_PARROTCHAN);
	}
	myrpt->parrotstate = 0;
	if (myrpt->voxchannel) {
		rpt_hangup(myrpt, RPT_VOXCHAN);
	}
	rpt_hangup(myrpt, RPT_BTELECHAN);
	rpt_hangup(myrpt, RPT_TELECHAN);
	rpt_hangup(myrpt, RPT_TXPCHAN);
	if (myrpt->dahditxchannel != myrpt->txchannel) {
		rpt_hangup(myrpt, RPT_DAHDITXCHAN);
	}
	rpt_hangup_rx_tx(myrpt);
	free_frame(&myrpt->lastf1);
	free_frame(&myrpt->lastf2);

	rpt_mutex_lock(&myrpt->lock);
	l = myrpt->links.next;
	while (l != &myrpt->links) {
		struct rpt_link *ll = l;
		/* remove from queue */
		rpt_link_remove(myrpt, l);
		/* hang-up on call to device */
		if (l->chan)
			ast_hangup(l->chan);
		ast_hangup(l->pchan);
		l = l->next;
		ast_free(ll);
	}
	if (myrpt->xlink == 1)
		myrpt->xlink = 2;
	rpt_mutex_unlock(&myrpt->lock);

	ast_debug(1, "@@@@ rpt:Hung up channel\n");
	myrpt->rpt_thread = AST_PTHREADT_STOP;
	if (myrpt->outstreampid)
		kill(myrpt->outstreampid, SIGTERM);
	myrpt->outstreampid = 0;
	ast_debug(1, "%s thread now exiting...\n", myrpt->name);
	return NULL;
}

/* Forward declaration */
static int stop_repeaters(void);

static int load_config(int reload)
{
	int i, n = 0;
	struct ast_config *cfg;
	char *val, *this = NULL;

	cfg = ast_config_load("rpt.conf", config_flags);
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to open radio repeater configuration rpt.conf.  Radio Repeater disabled.\n");
		return -1;
	}

	if (reload) {
		for (n = 0; n < nrpts; n++) {
			rpt_vars[n].reload1 = 0;
		}
	} else {
		rpt_vars[n].cfg = cfg;
		/* If there are daq devices present, open and initialize them */
		daq_init(cfg);
	}

	/* load the general settings */
	val = (char *) ast_variable_retrieve(cfg, "general", "node_lookup_method");
	if (val) {
		if (!strcasecmp(val, "both")) {
			rpt_node_lookup_method = LOOKUP_BOTH;
		} else if (!strcasecmp(val, "dns")) {
			rpt_node_lookup_method = LOOKUP_DNS;
		} else if (!strcasecmp(val, "file")) {
			rpt_node_lookup_method = LOOKUP_FILE;
		} else {
			ast_log(LOG_WARNING,"Configuration error: node_lookup_method, %s, is not valid", val);
			rpt_node_lookup_method = DEFAULT_NODE_LOOKUP_METHOD;
		}
	}

	/* process the sections looking for the nodes */
	while ((this = ast_category_browse(cfg, this)) != NULL) {
		/* Node name must be fully numeric */
		for (i = 0; i < strlen(this); i++) {
			if ((this[i] < '0') || (this[i] > '9')) {
				break;
			}
		}
		if (i != strlen(this)) {
			continue; /* Not a node defn */
		}
		if (reload) {
			for (n = 0; n < nrpts; n++) {
				if (!strcmp(this, rpt_vars[n].name)) {
					rpt_vars[n].reload1 = 1;
					break;
				}
			}
			if (n < nrpts) {
				continue; /* Node already exists. */
			}
			/* No such node yet, find an empty hole or the next one */
			for (n = 0; n < nrpts; n++) {
				if (rpt_vars[n].deleted) {
					break;
				}
			}
		}
		if (n >= MAXRPTS) {
			ast_log(LOG_ERROR, "Attempting to add repeater node %s would exceed max. number of repeaters (%d)\n", this, MAXRPTS);
			continue;
		}
		memset(&rpt_vars[n], 0, sizeof(rpt_vars[n]));
		val = (char *) ast_variable_retrieve(cfg, this, "rxchannel");
		if (val) {
			char *slash, *rxchan = ast_strdup(val);
			slash = strchr(rxchan, '/');
			if (!slash) {
				ast_log(LOG_WARNING, "Channel '%s' is invalid, not adding node '%s'\n", val, this);
				ast_free(rxchan);
				continue;
			}
			slash[0] = '\0';
			if (!ast_get_channel_tech(rxchan)) {
				ast_log(LOG_WARNING, "Channel tech '%s' is not currently loaded, not adding node '%s'\n", rxchan, this);
				ast_free(rxchan);
				continue;
			}
			ast_free(rxchan);
			rpt_vars[n].rxchanname = ast_strdup(val);
		}
		rpt_vars[n].name = ast_strdup(this);
		val = (char *) ast_variable_retrieve(cfg, this, "txchannel");
		if (val) {
			rpt_vars[n].txchanname = ast_strdup(val);
		}
		rpt_vars[n].remote = 0;
		rpt_vars[n].remoterig = "";
		rpt_vars[n].p.iospeed = B9600;
		rpt_vars[n].ready = 0;
		val = (char *) ast_variable_retrieve(cfg, this, "remote");
		if (val) {
			rpt_vars[n].remoterig = ast_strdup(val);
			rpt_vars[n].remote = 1;
		}
		val = (char *) ast_variable_retrieve(cfg, this, "radiotype");
		if (val) {
			rpt_vars[n].remoterig = ast_strdup(val);
		}

		ast_mutex_init(&rpt_vars[n].lock);
		ast_mutex_init(&rpt_vars[n].remlock);
		ast_mutex_init(&rpt_vars[n].statpost_lock);
		ast_mutex_init(&rpt_vars[n].blocklock);
		rpt_vars[n].tele.next = &rpt_vars[n].tele;
		rpt_vars[n].tele.prev = &rpt_vars[n].tele;
		rpt_vars[n].rpt_thread = AST_PTHREADT_NULL;
		rpt_vars[n].tailmessagen = 0;
#ifdef	_MDC_DECODE_H_
		rpt_vars[n].mdc = mdc_decoder_new(8000);
#endif
		if (reload) {
			rpt_vars[n].reload1 = 1;
			if (n >= nrpts) {
				nrpts = n + 1;
			}
		} else {
			n++;
			nrpts = n;
		}
	}
	ast_config_destroy(cfg);
	cfg = NULL;

	return 0;
}

static void *rpt_master(void *ignore)
{
	int i;
	/* init nodelog queue */
	nodelog.next = nodelog.prev = &nodelog;
	/* go thru all the specified repeaters */

	/* wait until asterisk starts */
	while (!ast_test_flag(&ast_options, AST_OPT_FLAG_FULLY_BOOTED)) {
		usleep(250000);
	}

	if (load_config(0)) {
		return NULL;
	}

	/* start em all */
	for (i = 0; i < nrpts; i++) {
		load_rpt_vars(i, 1); /* Load initial config */

		/* if is a remote, dont start one for it */
		if (rpt_vars[i].remote) {
			if (retrieve_memory(&rpt_vars[i], "init")) {	/* Try to retrieve initial memory channel */
				if ((!strcmp(rpt_vars[i].remoterig, REMOTE_RIG_RTX450)) || (!strcmp(rpt_vars[i].remoterig, REMOTE_RIG_XCAT))) {
					ast_copy_string(rpt_vars[i].freq, "446.500", sizeof(rpt_vars[i].freq));
				} else {
					ast_copy_string(rpt_vars[i].freq, "145.000", sizeof(rpt_vars[i].freq));
				}
			}
			continue;
		} else { /* is a normal repeater */
			rpt_vars[i].p.memory = rpt_vars[i].name;
			if (retrieve_memory(&rpt_vars[i], "radiofreq")) {	/* Try to retrieve initial memory channel */
				if (!strcmp(rpt_vars[i].remoterig, REMOTE_RIG_RTX450)) {
					ast_copy_string(rpt_vars[i].freq, "446.500", sizeof(rpt_vars[i].freq));
				} else if (!strcmp(rpt_vars[i].remoterig, REMOTE_RIG_RTX150)) {
					ast_copy_string(rpt_vars[i].freq, "146.580", sizeof(rpt_vars[i].freq));
				}
			}
			ast_log(LOG_NOTICE, "Normal Repeater Init  %s  %s  %s\n", rpt_vars[i].name, rpt_vars[i].remoterig, rpt_vars[i].freq);
		}

		ast_copy_string(rpt_vars[i].rxpl, "100.0", sizeof(rpt_vars[i].rxpl));
		ast_copy_string(rpt_vars[i].txpl, "100.0", sizeof(rpt_vars[i].txpl));
		rpt_vars[i].remmode = REM_MODE_FM;
		rpt_vars[i].offset = REM_SIMPLEX;
		rpt_vars[i].powerlevel = REM_LOWPWR;
		rpt_vars[i].splitkhz = 0;

		if (rpt_vars[i].p.ident && (!*rpt_vars[i].p.ident)) {
			ast_log(LOG_WARNING, "Did not specify ident for node %s\n", rpt_vars[i].name);
			pthread_exit(NULL);
		}
		rpt_vars[i].ready = 0;
		ast_pthread_create_detached(&rpt_vars[i].rpt_thread, NULL, rpt, (void *) &rpt_vars[i]);
	}
	usleep(500000);
	time(&starttime);
	ast_mutex_lock(&rpt_master_lock);
	for (;;) {
		/* Now monitor each thread, and restart it if necessary */
		for (i = 0; i < nrpts; i++) {
			int rv;
			if (rpt_vars[i].remote)
				continue;
			if ((rpt_vars[i].rpt_thread == AST_PTHREADT_STOP) || (rpt_vars[i].rpt_thread == AST_PTHREADT_NULL)) {
				rv = -1;
			} else {
				rv = pthread_kill(rpt_vars[i].rpt_thread, 0);
			}
			if (rv) {
				if (rpt_vars[i].deleted) {
					rpt_vars[i].name[0] = 0;
					continue;
				}
				if (shutting_down) {
					continue; /* Don't restart thread if we're unloading the module */
				}
				if (time(NULL) - rpt_vars[i].lastthreadrestarttime <= 5) {
					if (rpt_vars[i].threadrestarts >= 5) {
						/* This is way off-nominal here. The original code just called exit(1) which
						 * is totally not cool... so this is a little bit saner. */
						ast_log(LOG_ERROR, "Continual RPT thread restarts, stopping repeaters\n");
						stop_repeaters();
						/* Not necessary to set shutting_down to 1, since we're the only thread that uses that, and we're exiting */
						ast_mutex_unlock(&rpt_master_lock);
						return NULL; /* The module will have to be unloaded and loaded again to start the repeaters */
					} else {
						ast_log(LOG_WARNING, "RPT thread restarted on %s\n", rpt_vars[i].name);
						rpt_vars[i].threadrestarts++;
					}
				} else {
					rpt_vars[i].threadrestarts = 0;
				}

				rpt_vars[i].lastthreadrestarttime = time(NULL);
				ast_pthread_create_detached(&rpt_vars[i].rpt_thread, NULL, rpt, (void *) &rpt_vars[i]);
				/* if (!rpt_vars[i].xlink) */
				ast_log(LOG_WARNING, "rpt_thread restarted on node %s\n", rpt_vars[i].name);
			}
		}
		for (i = 0; i < nrpts; i++) {
			if (rpt_vars[i].deleted || rpt_vars[i].remote || !rpt_vars[i].p.outstreamcmd) {
				continue;
			}
			if (rpt_vars[i].outstreampid && (kill(rpt_vars[i].outstreampid, 0) != -1)) {
				continue;
			}
			rpt_vars[i].outstreampid = 0;
			startoutstream(&rpt_vars[i]);
		}
		for (;;) {
			struct nodelog *nodep;
			char *space, datestr[100], fname[1024];
			int fd;

			ast_mutex_lock(&nodeloglock);
			nodep = nodelog.next;
			if (nodep == &nodelog) {	/* if nothing in queue */
				ast_mutex_unlock(&nodeloglock);
				break;
			}
			remque((struct qelem *) nodep);
			ast_mutex_unlock(&nodeloglock);
			space = strchr(nodep->str, ' ');
			if (!space) {
				ast_free(nodep);
				continue;
			}
			*space = 0;
			strftime(datestr, sizeof(datestr) - 1, "%Y%m%d", localtime(&nodep->timestamp));
			sprintf(fname, "%s/%s/%s.txt", nodep->archivedir, nodep->str, datestr);
			fd = open(fname, O_WRONLY | O_CREAT | O_APPEND, 0644);
			if (fd == -1) {
				ast_log(LOG_ERROR, "Cannot open node log file %s for write: %s", fname, strerror(errno));
				ast_free(nodep);
				continue;
			}
			if (write(fd, space + 1, strlen(space + 1)) != strlen(space + 1)) {
				ast_log(LOG_ERROR, "Cannot write node log file %s for write: %s", fname, strerror(errno));
				ast_free(nodep);
				continue;
			}
			close(fd);
			ast_free(nodep);
		}
		ast_mutex_unlock(&rpt_master_lock);
		while (shutting_down) {
			int done = 0;
			ast_debug(1, "app_rpt is unloading, master thread cleaning up %d repeater%s and exiting\n", nrpts, ESS(nrpts));
			for (i = 0; i < nrpts; i++) {
				if (rpt_vars[i].deleted) {
					ast_debug(1, "Skipping deleted thread %s\n", rpt_vars[i].name);
					done++;
					continue;
				}
				if (rpt_vars[i].remote) {
					ast_debug(1, "Skipping remote thread %s\n", rpt_vars[i].name);
					done++;
					continue;
				}
				if (rpt_vars[i].rpt_thread == AST_PTHREADT_STOP) {
					ast_debug(1, "Skipping stopped thread %s\n", rpt_vars[i].name);
					done++;
					continue;
				}
				if (rpt_vars[i].rpt_thread == AST_PTHREADT_NULL) {
					ast_debug(1, "Skipping null thread %s\n", rpt_vars[i].name);
					done++;
					continue;
				}
				if (!(rpt_vars[i].rpt_thread == AST_PTHREADT_STOP) || (rpt_vars[i].rpt_thread == AST_PTHREADT_NULL)) {
					if (pthread_join(rpt_vars[i].rpt_thread, NULL)) {
						ast_log(LOG_WARNING, "Failed to join %s thread: %s\n", rpt_vars[i].name, strerror(errno));
					} else {
						ast_debug(1, "Repeater thread %s has now exited\n", rpt_vars[i].name);
						rpt_vars[i].rpt_thread = AST_PTHREADT_NULL;
						done++;
					}
				}
			}
			ast_mutex_lock(&rpt_master_lock);
			ast_debug(1, "Joined %d/%d repeater%s so far\n", done, nrpts, ESS(nrpts));
			if (done >= nrpts) {
				goto done; /* Break out of outer loop */
			}
			ast_mutex_unlock(&rpt_master_lock);
			usleep(200000);
		}
		usleep(2000000);
		ast_mutex_lock(&rpt_master_lock);
	}

done:
	ast_mutex_unlock(&rpt_master_lock);
	ast_debug(1, "app_rpt master thread exiting\n");
	pthread_exit(NULL);
}

static inline int exec_chan_read(struct rpt *myrpt, struct ast_channel *chan, char *restrict keyed, const int phone_mode, const int phone_vox, char *restrict myfirst, int *restrict dtmfed)
{
	struct ast_frame *f = ast_read(chan);
	if (!f) {
		ast_debug(1, "@@@@ link:Hung Up\n");
		return -1;
	}
	if (f->frametype == AST_FRAME_VOICE) {
		struct ast_frame *f1;
		int ismuted;
		if (myrpt->newkey == 2) {
			myrpt->rxlingertimer = ((myrpt->iaxkey) ? RX_LINGER_TIME_IAXKEY : RX_LINGER_TIME);
			if (!*keyed) {
				*keyed = 1;
				myrpt->rerxtimer = 0;
			}
		}
		if (phone_mode && phone_vox) {
			int x;
			int n1 = dovox(&myrpt->vox, f->data.ptr, f->datalen / 2);
			if (n1 != myrpt->wasvox) {
				ast_debug(1, "Remote  vox %d\n", n1);
				myrpt->wasvox = n1;
				myrpt->voxtostate = 0;
				if (n1)
					myrpt->voxtotimer = myrpt->p.voxtimeout_ms;
				else
					myrpt->voxtotimer = 0;
			}
			if (n1) {
				if (!*myfirst) {
					x = 0;
					AST_LIST_TRAVERSE(&myrpt->rxq, f1, frame_list) x++;
					for (; x < myrpt->p.simplexphonedelay; x++) {
						f1 = ast_frdup(f);
						memset(f1->data.ptr, 0, f1->datalen);
						memset(&f1->frame_list, 0, sizeof(f1->frame_list));
						AST_LIST_INSERT_TAIL(&myrpt->rxq, f1, frame_list);
					}
					*myfirst = 1;
				}
				f1 = ast_frdup(f);
				memset(&f1->frame_list, 0, sizeof(f1->frame_list));
				AST_LIST_INSERT_TAIL(&myrpt->rxq, f1, frame_list);
			} else
				*myfirst = 0;
			x = 0;
			AST_LIST_TRAVERSE(&myrpt->rxq, f1, frame_list) x++;
			if (!x) {
				memset(f->data.ptr, 0, f->datalen);
			} else {
				ast_frfree(f);
				f = AST_LIST_REMOVE_HEAD(&myrpt->rxq, frame_list);
			}
		}
		ismuted = rpt_conf_get_muted(chan, myrpt);
		/* if not transmitting, zero-out audio */
		ismuted |= (!myrpt->remotetx);
		if (*dtmfed && phone_mode)
			ismuted = 1;
		*dtmfed = 0;
		mute_frame_helper(myrpt, f, ismuted);
		f1 = myrpt->lastf2;
		if (f1) {
			if (!myrpt->remstopgen) {
				if (phone_mode)
					ast_write(myrpt->txchannel, f1);
				else
					ast_write(myrpt->txchannel, f);
			}
			ast_frfree(f1);
			myrpt->lastf2 = NULL; /* Aliased with f1, so set to NULL since this reference is no longer valid */
		}
	} else if (f->frametype == AST_FRAME_DTMF_BEGIN) {
		if (myrpt->lastf1)
			memset(myrpt->lastf1->data.ptr, 0, myrpt->lastf1->datalen);
		if (myrpt->lastf2)
			memset(myrpt->lastf2->data.ptr, 0, myrpt->lastf2->datalen);
		*dtmfed = 1;
	}
	if (f->frametype == AST_FRAME_DTMF) {
		if (myrpt->lastf1)
			memset(myrpt->lastf1->data.ptr, 0, myrpt->lastf1->datalen);
		if (myrpt->lastf2)
			memset(myrpt->lastf2->data.ptr, 0, myrpt->lastf2->datalen);
		*dtmfed = 1;
		if (handle_remote_phone_dtmf(myrpt, f->subclass.integer, keyed, phone_mode) == -1) {
			ast_debug(1, "@@@@ rpt:Hung Up\n");
			ast_frfree(f);
			return -1;
		}
	}
	if (f->frametype == AST_FRAME_TEXT) {
		char *tstr = ast_malloc(f->datalen + 1);
		if (tstr) {
			memcpy(tstr, f->data.ptr, f->datalen);
			tstr[f->datalen] = 0;
			if (handle_remote_data(myrpt, tstr) == -1) {
				ast_debug(1, "@@@@ rpt:Hung Up\n");
				ast_frfree(f);
				return -1;
			}
			ast_free(tstr);
		}
	}
	if (f->frametype == AST_FRAME_CONTROL) {
		if (f->subclass.integer == AST_CONTROL_HANGUP) {
			ast_debug(1, "@@@@ rpt:Hung Up\n");
			ast_frfree(f);
			return -1;
		}
		/* if RX key */
		if ((f->subclass.integer == AST_CONTROL_RADIO_KEY) && (myrpt->newkey < 2)) {
			ast_debug(7, "@@@@ rx key\n");
			*keyed = 1;
			myrpt->rerxtimer = 0;
		}
		/* if RX un-key */
		if (f->subclass.integer == AST_CONTROL_RADIO_UNKEY) {
			myrpt->rerxtimer = 0;
			ast_debug(7, "@@@@ rx un-key\n");
			*keyed = 0;
		}
	}
	ast_frfree(f);
	return 0;
}

static inline int exec_rxchannel_read(struct rpt *myrpt, const int reming, const int notremming, int *restrict remkeyed)
{
	struct ast_frame *f = ast_read(myrpt->rxchannel);
	if (!f) {
		ast_debug(1, "@@@@ link:Hung Up\n");
		return -1;
	}
	if (f->frametype == AST_FRAME_VOICE) {
		int myreming = 0;

		if (myrpt->remstopgen > 0) {
			ast_tonepair_stop(myrpt->txchannel);
			myrpt->remstopgen = 0;
		}
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD)) {
			myreming = reming;
		}
		if (myreming || !*remkeyed || (myrpt->remote && myrpt->remotetx) || (myrpt->remmode != REM_MODE_FM && notremming)) {
			memset(f->data.ptr, 0, f->datalen);
		}
		ast_write(myrpt->pchannel, f);
	} else if (f->frametype == AST_FRAME_CONTROL) {
		if (f->subclass.integer == AST_CONTROL_HANGUP) {
			ast_debug(1, "@@@@ rpt:Hung Up\n");
			ast_frfree(f);
			return -1;
		}
		/* if RX key */
		if (f->subclass.integer == AST_CONTROL_RADIO_KEY) {
			ast_debug(7, "@@@@ remote rx key\n");
			if (!myrpt->remotetx) {
				*remkeyed = 1;
			}
		}
		/* if RX un-key */
		if (f->subclass.integer == AST_CONTROL_RADIO_UNKEY) {
			ast_debug(7, "@@@@ remote rx un-key\n");
			if (!myrpt->remotetx) {
				*remkeyed = 0;
			}
		}
	}
	ast_frfree(f);
	return 0;
}

static inline int exec_pchannel_read(struct rpt *myrpt, struct ast_channel *chan)
{
	struct ast_frame *f = ast_read(myrpt->pchannel);
	if (!f) {
		ast_debug(1, "@@@@ link:Hung Up\n");
		return -1;
	}
	if (f->frametype == AST_FRAME_VOICE) {
		if ((myrpt->newkey < 2) || myrpt->remoterx || strcasecmp(ast_channel_tech(chan)->type, "IAX2")) {
			ast_write(chan, f);
		}
	}
	return hangup_frame_helper(myrpt->pchannel, "pchannel", f);
}

static inline int exec_txchannel_read(struct rpt *myrpt)
{
	return wait_for_hangup_helper(myrpt->txchannel, "txchannel");
}

static char *parse_node_format(char *s, char **restrict s1, char *sx)
{
	char *s2;

	*s1 = strsep(&s, ",");
	if (!strchr(*s1, ':') && strchr(*s1, '/') && strncasecmp(*s1, "local/", 6)) {
		char *sy = strchr(*s1, '/');
		*sy = 0;
		sprintf(sx, "%s:4569/%s", *s1, sy + 1);
		*s1 = sx;
	}
	s2 = strsep(&s, ",");
	if (!s2) {
		return NULL;
	}
	return s2;
}

static int parse_caller(const char *b1, const char *hisip, char *s)
{
	char sx[320];
	char *s1, *s2, *s3;

	s2 = parse_node_format(s, &s1, sx);
	if (!s2) {
		ast_log(LOG_WARNING, "Reported node %s not in correct format\n", b1);
		return -1;
	}
	if (strcmp(s2, "NONE")) {
		char nodeip[100];
		struct ast_sockaddr addr = { {0,} };
		if (ast_sockaddr_resolve_first_af(&addr, s2, PARSE_PORT_FORBID, AF_UNSPEC)) {
			ast_log(LOG_WARNING, "Reported node %s, name %s cannot be found!!\n", b1, s2);
			return -1;
		}
		ast_copy_string(nodeip, ast_sockaddr_stringify_addr(&addr), sizeof(nodeip));
		s3 = strchr(hisip, ':');
		if (s3)
			*s3 = 0;
		if (strcmp(hisip, nodeip)) {
			s3 = strchr(s1, '@');
			if (s3)
				s1 = s3 + 1;
			s3 = strchr(s1, '/');
			if (s3)
				*s3 = 0;
			s3 = strchr(s1, ':');
			if (s3)
				*s3 = 0;
			if (ast_sockaddr_resolve_first_af(&addr, s1, PARSE_PORT_FORBID, AF_UNSPEC)) {
				ast_log(LOG_WARNING, "Reported node %s, name %s cannot be found!!\n", b1, s1);
				return -1;
			}
			ast_copy_string(nodeip, ast_sockaddr_stringify_addr(&addr), sizeof(nodeip));
			if (strcmp(hisip, nodeip)) {
				ast_log(LOG_WARNING, "Node %s IP %s does not match link IP %s!!\n", b1, nodeip, hisip);
				return -1;
			}
		}
	}
	return 0;
}

static int get_his_ip(struct ast_channel *chan, char *buf, size_t len)
{
	/* get his IP from IAX2 module */
#ifdef ALLOW_LOCAL_CHANNELS
	/* set IP address if this is a local connection */
	if (!strncmp(ast_channel_name(chan), "Local", 5)) {
		strcpy(buf, "127.0.0.1");
	} else {
		pbx_substitute_variables_helper(chan, "${IAXPEER(CURRENTCHANNEL)}", buf, len - 1);
	}
#else
	pbx_substitute_variables_helper(chan, "${IAXPEER(CURRENTCHANNEL)}", buf, len - 1);
#endif
	if (ast_strlen_zero(buf)) {
		ast_log(LOG_WARNING, "Link IP address cannot be determined\n");
		return -1;
	}
	return 0;
}

static inline int kenwood_uio_helper(struct rpt *myrpt)
{
	if (rpt_radio_set_param(myrpt->dahditxchannel, myrpt, RPT_RADPAR_UIOMODE, 3)) {
		ast_log(LOG_ERROR, "Cannot set UIOMODE on %s: %s\n", ast_channel_name(myrpt->dahditxchannel), strerror(errno));
		return -1;
	}
	if (rpt_radio_set_param(myrpt->dahditxchannel, myrpt, RPT_RADPAR_UIODATA, 3)) {
		ast_log(LOG_ERROR, "Cannot set UIODATA on %s: %s\n", ast_channel_name(myrpt->dahditxchannel), strerror(errno));
		return -1;
	}
	if (dahdi_set_offhook(myrpt->dahditxchannel)) {
		return -1;
	}
	return 0;
}

static void answer_newkey_helper(struct rpt *myrpt, struct ast_channel *chan, int phone_mode)
{
	rpt_mutex_lock(&myrpt->blocklock);
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_answer(chan);
		if (!phone_mode) {
			send_newkey(chan);
		}
	}
	rpt_mutex_unlock(&myrpt->blocklock);
}

static int rpt_exec(struct ast_channel *chan, const char *data)
{
	int res = -1, i, rem_totx, rem_rx, remkeyed, n, phone_mode = 0;
	int iskenwood_pci4, authtold, authreq, setting, notremming, reming;
	int dtmfed, phone_vox = 0, phone_monitor = 0;
	char tmp[256], keyed = 0, keyed1 = 0;
	char *options, *stringp, *callstr, c, *altp, *memp;
	char sx[320], myfirst, *b, *b1;
	struct rpt *myrpt;
	struct ast_channel *who;
	struct ast_channel *cs[20];
	struct rpt_link *l;
	int ms, elap, myrx;
	time_t t, last_timeout_warning;
	struct rpt_tele *telem;
	int numlinks;
	struct ast_format_cap *cap;
	struct timeval looptimestart;
	struct timeval looptimenow;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Rpt requires an argument (system node)\n");
		return -1;
	}

	ast_copy_string(tmp, data, sizeof(tmp));
	time(&t);
	/* if time has externally shifted negative, screw it */
	if (t < starttime)
		t = starttime + START_DELAY;
	if ((!starttime) || (t < (starttime + START_DELAY))) {
		ast_log(LOG_NOTICE, "Node %s rejecting call: too soon!\n", tmp);
		ast_safe_sleep(chan, 3000);
		return -1;
	}

	ast_set_read_format(chan, ast_format_slin);
	ast_set_write_format(chan, ast_format_slin);

	altp = strstr(tmp, "|*");
	if (altp) {
		altp[0] = 0;
		altp++;
	}

	memp = strstr(tmp, "|M");
	if (memp) {
		memp[0] = 0;
		memp += 2;
	}

	stringp = tmp;
	strsep(&stringp, "|");
	options = stringp;
	strsep(&stringp, "|");
	callstr = stringp;

	myrpt = NULL;
	/* see if we can find our specified one */
	for (i = 0; i < nrpts; i++) {
		/* if name matches, assign it and exit loop */
		if (!strcmp(tmp, rpt_vars[i].name)) {
			myrpt = &rpt_vars[i];
			break;
		}
	}

	pbx_builtin_setvar_helper(chan, "RPT_STAT_ERR", "");

	if (myrpt == NULL) {
		char *myadr, *mypfx, dstr[1024];
		char *s1, *s2;
		char nodedata[100], xstr[100], tmp1[100];
		struct ast_config *cfg;

		myadr = NULL;
		b1 = ast_channel_caller(chan)->id.number.str;
		if (b1)
			ast_shrink_phone_number(b1);
		cfg = ast_config_load("rpt.conf", config_flags);
		if (cfg && ((!options) || (*options == 'X') || (*options == 'F'))) {
			myadr = (char *) ast_variable_retrieve(cfg, "proxy", "ipaddr");
			if (options && (*options == 'F')) {
				if (b1 && myadr) {
					forward_node_lookup(b1, cfg, nodedata, sizeof(nodedata));
					ast_copy_string(xstr, nodedata, sizeof(xstr));
					s2 = parse_node_format(xstr, &s1, sx);
					if (!s2) {
						ast_log(LOG_WARNING, "Specified node %s not in correct format\n", nodedata);
						ast_config_destroy(cfg);
						return -1;
					}
					nodedata[0] = '\0';
					if (!strcmp(s2, myadr)) {
						forward_node_lookup(tmp, cfg, nodedata, sizeof(nodedata));
					}
				}

			} else {
				forward_node_lookup(tmp, cfg, nodedata, sizeof(nodedata));
			}
		}
		if (b1 && !ast_strlen_zero(nodedata) && myadr && cfg) {
			ast_copy_string(xstr, nodedata, sizeof(xstr));
			if (!options) {
				char hisip[100] = "";
				if (*b1 < '1') {
					ast_log(LOG_WARNING, "Connect attempt from invalid node number\n");
					return -1;
				}
				if (get_his_ip(chan, hisip, sizeof(hisip))) {
					return -1;
				}
				/* look for his reported node string */
				forward_node_lookup(b1, cfg, nodedata, sizeof(nodedata));
				if (ast_strlen_zero(nodedata)) {
					ast_log(LOG_WARNING, "Reported node %s cannot be found!!\n", b1);
					return -1;
				}
				ast_copy_string(tmp1, nodedata, sizeof(tmp1));
				if (parse_caller(b1, hisip, tmp1)) {
					return -1;
				}
			}
			s2 = parse_node_format(xstr, &s1, sx);
			if (!s2) {
				ast_log(LOG_WARNING, "Specified node %s not in correct format\n", nodedata);
				ast_config_destroy(cfg);
				return -1;
			}
			if (options && (*options == 'F')) {
				ast_config_destroy(cfg);
				rpt_forward(chan, s1, b1);
				return -1;
			}
			if (!strcmp(myadr, s2)) {	/* if we have it.. */
				char tmp2[512];

				strcpy(tmp2, tmp);
				if (options && callstr)
					snprintf(tmp2, sizeof(tmp2) - 1, "0%s%s", callstr, tmp);
				mypfx = (char *) ast_variable_retrieve(cfg, "proxy", "nodeprefix");
				if (mypfx)
					snprintf(dstr, sizeof(dstr) - 1, "radio-proxy@%s%s/%s", mypfx, tmp, tmp2);
				else
					snprintf(dstr, sizeof(dstr) - 1, "radio-proxy@%s/%s", tmp, tmp2);
				ast_config_destroy(cfg);
				rpt_forward(chan, dstr, b1);
				return -1;
			}
			ast_config_destroy(cfg);
		}
		pbx_builtin_setvar_helper(chan, "RPT_STAT_ERR", "NODE_NOT_FOUND");
		ast_log(LOG_WARNING, "Cannot find specified system node %s\n", tmp);
		return (priority_jump(NULL, chan));
	}

	numlinks = linkcount(myrpt);

	if (options && *options == 'q') {
		int res = 0;
		char buf2[128];

		if (myrpt->keyed)
			pbx_builtin_setvar_helper(chan, "RPT_STAT_RXKEYED", "1");
		else
			pbx_builtin_setvar_helper(chan, "RPT_STAT_RXKEYED", "0");

		if (myrpt->txkeyed)
			pbx_builtin_setvar_helper(chan, "RPT_STAT_TXKEYED", "1");
		else
			pbx_builtin_setvar_helper(chan, "RPT_STAT_TXKEYED", "0");

		snprintf(buf2, sizeof(buf2), "%s=%i", "RPT_STAT_XLINK", myrpt->xlink);
		pbx_builtin_setvar(chan, buf2);
		snprintf(buf2, sizeof(buf2), "%s=%i", "RPT_STAT_LINKS", numlinks);
		pbx_builtin_setvar(chan, buf2);
		snprintf(buf2, sizeof(buf2), "%s=%d", "RPT_STAT_WASCHAN", myrpt->waschan);
		pbx_builtin_setvar(chan, buf2);
		snprintf(buf2, sizeof(buf2), "%s=%d", "RPT_STAT_NOWCHAN", myrpt->nowchan);
		pbx_builtin_setvar(chan, buf2);
		snprintf(buf2, sizeof(buf2), "%s=%d", "RPT_STAT_DUPLEX", myrpt->p.duplex);
		pbx_builtin_setvar(chan, buf2);
		snprintf(buf2, sizeof(buf2), "%s=%d", "RPT_STAT_PARROT", myrpt->p.parrotmode);
		pbx_builtin_setvar(chan, buf2);
		//snprintf(buf2,sizeof(buf2),"%s=%d", "RPT_STAT_PHONEVOX", myrpt->phonevox);
		//pbx_builtin_setvar(chan, buf2);
		//snprintf(buf2,sizeof(buf2),"%s=%d", "RPT_STAT_CONNECTED", myrpt->connected);
		//pbx_builtin_setvar(chan, buf2);
		snprintf(buf2, sizeof(buf2), "%s=%d", "RPT_STAT_CALLMODE", myrpt->callmode);
		pbx_builtin_setvar(chan, buf2);
		snprintf(buf2, sizeof(buf2), "%s=%s", "RPT_STAT_LASTTONE", myrpt->lasttone);
		pbx_builtin_setvar(chan, buf2);

		res = priority_jump(myrpt, chan);
		return res;
	}

	if (options && (*options == 'V' || *options == 'v')) {
		if (callstr && myrpt->rxchannel) {
			pbx_builtin_setvar(myrpt->rxchannel, callstr);
			ast_verb(3, "Set Asterisk channel variable %s for node %s\n", callstr, myrpt->name);
		}
		return 0;
	}

	if (options && *options == 'o') {
		return (channel_revert(myrpt));
	}

#if 0
	if ((altp) && (*options == 'Z')) {
		rpt_push_alt_macro(myrpt, altp);
		return 0;
	}
#endif

	/* if not phone access, must be an IAX connection */
	if (options && ((*options == 'P') || (*options == 'D') || (*options == 'R') || (*options == 'S'))) {
		int val;

		pbx_builtin_setvar_helper(chan, "RPT_STAT_BUSY", "0");

		myrpt->bargechan = 0;
		if (options && strstr(options, "f") > 0) {
			myrpt->bargechan = 1;
		}

		if (memp > 0) {
			char radiochan;
			radiochan = strtod(data, NULL);
			// if(myrpt->nowchan!=0 && radiochan!=myrpt->nowchan && !myrpt->bargechan)

			if (numlinks > 0 && radiochan != myrpt->nowchan && !myrpt->bargechan) {
				pbx_builtin_setvar_helper(chan, "RPT_STAT_BUSY", "1");
				ast_log(LOG_NOTICE, "Radio Channel Busy.\n");
				return (priority_jump(myrpt, chan));
			} else if (radiochan != myrpt->nowchan || myrpt->bargechan) {
				channel_steer(myrpt, memp);
			}
		}
		if (altp)
			rpt_push_alt_macro(myrpt, altp);
		phone_mode = 1;
		if (*options == 'D')
			phone_mode = 2;
		if (*options == 'S')
			phone_mode = 3;
		ast_set_callerid(chan, "0", "app_rpt user", "0");
		val = 1;
		ast_channel_setoption(chan, AST_OPTION_TONE_VERIFY, &val, sizeof(char), 0);
		if (strchr(options + 1, 'v') || strchr(options + 1, 'V'))
			phone_vox = 1;
		if (strchr(options + 1, 'm') || strchr(options + 1, 'M'))
			phone_monitor = 1;
	} else {
#ifdef ALLOW_LOCAL_CHANNELS
		/* Check to insure the connection is IAX2 or Local */
		if ((strcmp(ast_channel_tech(chan)->type, "IAX2")) && (strcmp(ast_channel_tech(chan)->type, "Local")) &&
			(strcasecmp(ast_channel_tech(chan)->type, "echolink")) && (strcasecmp(ast_channel_tech(chan)->type, "tlb"))) {
			ast_log(LOG_WARNING, "We only accept links via IAX2, Echolink, TheLinkBox or Local!!\n");
			return -1;
		}
#else
		if (strcmp(ast_channel_tech(chan)->type, "IAX2") && strcasecmp(ast_channel_tech(chan)->type, "Echolink") &&
			strcasecmp(ast_channel_tech(chan)->type, "tlb")) {
			ast_log(LOG_WARNING, "We only accept links via IAX2 or Echolink!!\n");
			return -1;
		}
#endif
		if ((myrpt->p.s[myrpt->p.sysstate_cur].txdisable) || myrpt->p.s[myrpt->p.sysstate_cur].noincomingconns) {	/* Do not allow incoming radio connections if disabled or noincomingconns is set */
			ast_log(LOG_NOTICE, "Connect attempt to node %s  with tx disabled or NOICE cop function active", myrpt->name);
			return -1;
		}
	}
	if (options && (*options == 'R')) {
		/* Parts of this section taken from app_parkandannounce */
		char *return_context;
		//int l, m, lot, timeout = 0;
		int l, m, timeout = 0;
		char tmp[256], *template;
		char *working, *context, *exten, *priority;
		char *s, *orig_s;

		rpt_mutex_lock(&myrpt->lock);
		m = myrpt->callmode;
		rpt_mutex_unlock(&myrpt->lock);

		if ((!myrpt->p.nobusyout) && m) {
			if (ast_channel_state(chan) != AST_STATE_UP) {
				ast_indicate(chan, AST_CONTROL_BUSY);
			}
			rpt_mutex_lock(&myrpt->blocklock);
			while (ast_safe_sleep(chan, 10000) != -1);
			rpt_mutex_unlock(&myrpt->blocklock);
			return -1;
		}

		answer_newkey_helper(myrpt, chan, phone_mode);

		l = strlen(options) + 2;
		orig_s = ast_malloc(l);
		if (!orig_s) {
			return -1;
		}
		s = orig_s;
		strncpy(s, options, l);

		template = strsep(&s, "|");
		if (!template) {
			ast_log(LOG_WARNING, "An announce template must be defined\n");
			ast_free(orig_s);
			return -1;
		}

		if (s) {
			timeout = atoi(strsep(&s, "|"));
			timeout *= 1000;
		}

		return_context = s;

		if (return_context != NULL) {
			/* set the return context. Code borrowed from the Goto builtin */

			working = return_context;
			context = strsep(&working, "|");
			exten = strsep(&working, "|");
			if (!exten) {
				/* Only a priority in this one */
				priority = context;
				exten = NULL;
				context = NULL;
			} else {
				priority = strsep(&working, "|");
				if (!priority) {
					/* Only an extension and priority in this one */
					priority = exten;
					exten = context;
					context = NULL;
				}
			}
			if (atoi(priority) < 0) {
				ast_log(LOG_WARNING, "Priority '%s' must be a number > 0\n", priority);
				ast_free(orig_s);
				return -1;
			}
			/* At this point we have a priority and maybe an extension and a context */
			ast_channel_priority_set(chan, atoi(priority));
			if (exten)
				ast_channel_exten_set(chan, exten);
			if (context)
				ast_channel_context_set(chan, context);
		} else {				/* increment the priority by default */
			ast_channel_priority_set(chan, ast_channel_priority(chan) + 1);
		}

		ast_verb(3, "Return Context: (%s,%s,%d) ID: %s\n", ast_channel_context(chan), ast_channel_exten(chan),
			ast_channel_priority(chan), ast_channel_caller(chan)->id.number.str);
		if (!ast_exists_extension
			(chan, ast_channel_context(chan), ast_channel_exten(chan), ast_channel_priority(chan),
			 ast_channel_caller(chan)->id.number.str)) {
			ast_log(LOG_WARNING, "Return Context Invalid, call will return to default|s\n");
		}

		/* we are using masq_park here to protect * from touching the channel once we park it.  If the channel comes out of timeout
		   before we are done announcing and the channel is messed with, Kablooeee.  So we use Masq to prevent this.  */

		/*! \todo the parking API changed a while ago, this all needs to be completely redone here */
		// old way: https://github.com/asterisk/asterisk/blob/1.8/apps/app_parkandannounce.c
		// new way: https://github.com/asterisk/asterisk/blob/master/res/parking/parking_applications.c#L890

		//ast_masq_park_call(chan, NULL, timeout, &lot); // commented out to avoid compiler error.

		//ast_verb(3, "Call Parking Called, lot: %d, timeout: %d, context: %s\n", lot, timeout, return_context);

		//snprintf(tmp,sizeof(tmp) - 1,"%d,%s",lot,template + 1);

		rpt_telemetry(myrpt, REV_PATCH, tmp);

		ast_free(orig_s);

		return 0;

	}
	i = 0;
	if (!strcasecmp(ast_channel_tech(chan)->type, "echolink")) {
		i = 1;
	}
	if (!strcasecmp(ast_channel_tech(chan)->type, "tlb")) {
		i = 1;
	}
	if (!options && !i) {
		char hisip[100] = "";
		/* look at callerid to see what node this comes from */
		if (!ast_channel_caller(chan)->id.number.str) {	/* if doesn't have caller id */
			ast_log(LOG_WARNING, "Does not have callerid on %s\n", tmp);
			return -1;
		}
		if (get_his_ip(chan, hisip, sizeof(hisip))) {
			return -1;
		}
		b = ast_channel_caller(chan)->id.name.str;
		b1 = ast_channel_caller(chan)->id.number.str;
		ast_shrink_phone_number(b1);
		if (!strcmp(myrpt->name, b1)) {
			ast_log(LOG_WARNING, "Trying to link to self?\n");
			return -1;
		}

		if (*b1 < '1') {
			ast_log(LOG_WARNING, "Node %s invalid for connection: Caller ID is not numeric\n", b1);
			return -1;
		}

		/* look for his reported node string */
		if (node_lookup(myrpt, b1, tmp, sizeof(tmp) - 1, 0)) {
			ast_log(LOG_WARNING, "Reported node %s cannot be found!!\n", b1);
			return -1;
		}
		if (parse_caller(b1, hisip, tmp)) {
			return -1;
		}
	}

	/* if is not a remote */
	if (!myrpt->remote) {
		int reconnects = 0;
		struct timeval now;
		struct ast_format_cap *cap;

		rpt_mutex_lock(&myrpt->lock);
		i = myrpt->xlink || (!myrpt->ready);
		rpt_mutex_unlock(&myrpt->lock);
		if (i) {
			ast_log(LOG_WARNING, "Cannot connect to node %s, system busy\n", myrpt->name);
			return -1;
		}
		rpt_mutex_lock(&myrpt->lock);
		gettimeofday(&now, NULL);
		while ((!ast_tvzero(myrpt->lastlinktime)) && (ast_tvdiff_ms(now, myrpt->lastlinktime) < 250)) {
			rpt_mutex_unlock(&myrpt->lock);
			if (ast_check_hangup(myrpt->rxchannel)) {
				return -1;
			}
			rpt_mutex_lock(&myrpt->blocklock);
			if (ast_safe_sleep(myrpt->rxchannel, 100) == -1) {
				rpt_mutex_unlock(&myrpt->blocklock);
				return -1;
			}
			rpt_mutex_unlock(&myrpt->blocklock);
			rpt_mutex_lock(&myrpt->lock);
			gettimeofday(&now, NULL);
		}
		gettimeofday(&myrpt->lastlinktime, NULL);
		rpt_mutex_unlock(&myrpt->lock);
		/* look at callerid to see what node this comes from */
		if (!ast_channel_caller(chan)->id.number.str) {	/* if doesn't have caller id */
			ast_log(LOG_WARNING, "Doesn't have callerid on %s\n", tmp);
			return -1;
		}
		if (phone_mode) {
			b1 = "0";
			b = NULL;
			if (callstr)
				b1 = callstr;
		} else {
			//b = chan->cid.cid_name;
			b = ast_channel_caller(chan)->id.name.str;
			b1 = ast_channel_caller(chan)->id.number.str;
			ast_shrink_phone_number(b1);
			/* if is an IAX client */
			if ((b1[0] == '0') && b && b[0] && (strlen(b) <= 8))
				b1 = b;
		}
		if (!strcmp(myrpt->name, b1)) {
			ast_log(LOG_WARNING, "Trying to link to self?\n");
			return -1;
		}
		for (i = 0; b1[i]; i++) {
			if (!isdigit(b1[i]))
				break;
		}
		if (!b1[i]) {			/* if not a call-based node number */
			rpt_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
			/* try to find this one in queue */
			while (l != &myrpt->links) {
				if (l->name[0] == '0') {
					l = l->next;
					continue;
				}
				/* if found matching string */
				if (!strcmp(l->name, b1))
					break;
				l = l->next;
			}
			/* if found */
			if (l != &myrpt->links) {
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
		l = ast_calloc(1, sizeof(struct rpt_link));
		if (!l) {
			pthread_exit(NULL);
		}
		l->mode = 1;
		ast_copy_string(l->name, b1, MAXNODESTR);
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
		if ((!phone_mode) && (l->name[0] != '0') && strcasecmp(ast_channel_tech(chan)->type, "echolink") && strcasecmp(ast_channel_tech(chan)->type, "tlb")) {
			l->newkey = 2;
		}
		ast_debug(7, "newkey: %d\n", l->newkey);
		if (l->name[0] > '9') {
			l->newkeytimer = 0;
		}
		voxinit_link(l, 1);
		if (!strcasecmp(ast_channel_tech(chan)->type, "echolink"))
			init_linkmode(myrpt, l, LINKMODE_ECHOLINK);
		else if (!strcasecmp(ast_channel_tech(chan)->type, "tlb"))
			init_linkmode(myrpt, l, LINKMODE_TLB);
		else if (phone_mode)
			init_linkmode(myrpt, l, LINKMODE_PHONE);
		else
			init_linkmode(myrpt, l, LINKMODE_GUI);
		ast_set_read_format(l->chan, ast_format_slin);
		ast_set_write_format(l->chan, ast_format_slin);
		gettimeofday(&myrpt->lastlinktime, NULL);

		cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (!cap) {
			ast_log(LOG_ERROR, "Failed to alloc cap\n");
			pthread_exit(NULL); /*! \todo This and all subsequent pthread_exit's do not clean up properly */
		}

		ast_format_cap_append(cap, ast_format_slin, 0);

		/* allocate a pseudo-channel thru asterisk */
		if (__rpt_request_pseudo(l, cap, RPT_PCHAN, RPT_LINK_CHAN)) {
			ao2_ref(cap, -1);
			pthread_exit(NULL);
		}

		ao2_ref(cap, -1);

		/* make a conference for the tx */
		if (rpt_conf_add_speaker(l->pchan, myrpt)) {
			pthread_exit(NULL);
		}
		rpt_mutex_lock(&myrpt->lock);
		if ((phone_mode == 2) && (!phone_vox))
			l->lastrealrx = 1;
		l->max_retries = MAX_RETRIES;
		/* insert at end of queue */
		rpt_link_add(myrpt, l);
		__kickshort(myrpt);
		gettimeofday(&myrpt->lastlinktime, NULL);
		rpt_mutex_lock(&myrpt->blocklock);
		if (ast_channel_state(chan) != AST_STATE_UP) {
			ast_answer(chan);
			if (l->name[0] > '9') {
				if (ast_safe_sleep(chan, 500) == -1) {
					return -1;
				}
			} else {
				if (!phone_mode) {
					send_newkey(chan);
				}
			}
		}
		rpt_mutex_unlock(&myrpt->blocklock);
		rpt_mutex_unlock(&myrpt->lock); /* Moved unlock to AFTER the if... answer block above, to prevent ast_waitfor_n assertion due to simultaneous channel access */
		rpt_update_links(myrpt);
		if (myrpt->p.archivedir) {
			donodelog_fmt(myrpt,"LINK%s,%s", l->phonemode ? "(P)" : "", l->name);
		}
		doconpgm(myrpt, l->name);
		if ((!phone_mode) && (l->name[0] <= '9')) {
			rpt_mutex_lock(&myrpt->blocklock);
			send_newkey(chan);
			rpt_mutex_unlock(&myrpt->blocklock);
		}
		if (!strcasecmp(ast_channel_tech(l->chan)->type, "echolink") || !strcasecmp(ast_channel_tech(l->chan)->type, "tlb") || (l->name[0] > '9')) {
			rpt_telemetry(myrpt, CONNECTED, l);
		}
		//return AST_PBX_KEEPALIVE;
		ast_channel_pbx_set(l->chan, NULL);
		ast_debug(1, "Stopped PBX on %s\n", ast_channel_name(l->chan));
		pthread_exit(NULL); // BUGBUG: For now, this emulates the behavior of KEEPALIVE, but this won't be a clean exit. Makes it work, but since the PBX doesn't clean up we'll leak memory. Either do what the PBX core does here or we need to somehow do KEEPALIVE handling in the core, possibly with a custom patch for now.
		//return -1;				/*! \todo AST_PBX_KEEPALIVE doesn't exist anymore. Figure out what we should return here. */
	}
	/* well, then it is a remote */
	rpt_mutex_lock(&myrpt->lock);
	/* look at callerid to see what node this comes from */
	if (!ast_channel_caller(chan)->id.number.str) {	/* if doesn't have caller id */
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
	if (b1 && (*b1 > '9'))
		myrpt->remote_webtransceiver = chan;
	/* if remote, error if anyone else already linked */
	if (myrpt->remoteon) {
		rpt_mutex_unlock(&myrpt->lock);
		usleep(500000);
		if (myrpt->remoteon) {
			struct ast_tone_zone_sound *ts = NULL;

			ast_log(LOG_WARNING, "Trying to use busy link on %s\n", tmp);
			if (myrpt->remote_webtransceiver || (b && (*b > '9'))) {
				ts = ast_get_indication_tone(ast_channel_zone(chan), "busy");
				ast_playtones_start(chan, 0, ts->data, 1);
				i = 0;
				while (ast_channel_generatordata(chan) && (i < 5000)) {
					rpt_mutex_lock(&myrpt->blocklock);
					if (ast_safe_sleep(chan, 20)) {
						rpt_mutex_unlock(&myrpt->blocklock);
						break;
					}
					rpt_mutex_unlock(&myrpt->blocklock);
					i += 20;
				}
				ast_playtones_stop(chan);
			}
			rpt_disable_cdr(chan);
			return -1;
		}
		rpt_mutex_lock(&myrpt->lock);
	}
	if (myrpt->p.rptnode) {
		char killedit = 0;
		time_t now;

		time(&now);
		for (i = 0; i < nrpts; i++) {
			if (!strcasecmp(rpt_vars[i].name, myrpt->p.rptnode)) {
				if ((rpt_vars[i].links.next != &rpt_vars[i].links) ||
					rpt_vars[i].keyed ||
					((rpt_vars[i].lastkeyedtime + RPT_LOCKOUT_SECS) > now) ||
					rpt_vars[i].txkeyed || ((rpt_vars[i].lasttxkeyedtime + RPT_LOCKOUT_SECS) > now)) {
					rpt_mutex_unlock(&myrpt->lock);
					ast_log(LOG_WARNING, "Trying to use busy link (repeater node %s) on %s\n", rpt_vars[i].name, tmp);
					rpt_disable_cdr(chan);
					return -1;
				}
				while (rpt_vars[i].xlink != 3) {
					if (!killedit) {
						ast_softhangup(rpt_vars[i].rxchannel, AST_SOFTHANGUP_DEV);
						rpt_vars[i].xlink = 1;
						killedit = 1;
					}
					rpt_mutex_unlock(&myrpt->lock);
					rpt_mutex_lock(&myrpt->blocklock);
					if (ast_safe_sleep(chan, 500) == -1) {
						rpt_mutex_unlock(&myrpt->blocklock);
						rpt_disable_cdr(chan);
						return -1;
					}
					rpt_mutex_unlock(&myrpt->blocklock);
					rpt_mutex_lock(&myrpt->lock);
				}
				break;
			}
		}
	}

	if ((!strcmp(myrpt->remoterig, REMOTE_RIG_RBI) || !strcmp(myrpt->remoterig, REMOTE_RIG_PPP16))) {
#ifdef HAVE_SYS_IO
		if (ioperm(myrpt->p.iobase, 1, 1) == -1) {
			rpt_mutex_unlock(&myrpt->lock);
			ast_log(LOG_WARNING, "Can't get io permission on IO port %x hex\n", myrpt->p.iobase);
			return -1;
		}
#else
		ast_log(LOG_ERROR, "IO port not supported on this architecture\n");
		return -1;
#endif
	}

	myrpt->remoteon = 1;
	voxinit_rpt(myrpt, 1);
	rpt_mutex_unlock(&myrpt->lock);

	load_rpt_vars_by_rpt(myrpt, 1);

	rpt_mutex_lock(&myrpt->lock);

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		pthread_exit(NULL);
	}

	ast_format_cap_append(cap, ast_format_slin, 0);

	if (__rpt_request(myrpt, cap, RPT_RXCHAN, RPT_LINK_CHAN)) {
		rpt_mutex_unlock(&myrpt->lock);
		ao2_ref(cap, -1);
		pthread_exit(NULL);
	}

	myrpt->dahditxchannel = NULL;
	if (myrpt->txchanname) {
		if (__rpt_request(myrpt, cap, RPT_TXCHAN, RPT_LINK_CHAN)) {
			rpt_mutex_unlock(&myrpt->lock);
			rpt_hangup(myrpt, RPT_RXCHAN);
			ao2_ref(cap, -1);
			pthread_exit(NULL);
		}
	} else {
		myrpt->txchannel = myrpt->rxchannel;
		if (IS_DAHDI_CHAN_NAME(myrpt->rxchanname)) {
			myrpt->dahditxchannel = myrpt->rxchannel;
		}
	}

	i = 3;
	ast_channel_setoption(myrpt->rxchannel, AST_OPTION_TONE_VERIFY, &i, sizeof(char), 0);

	if (rpt_request_pseudo(myrpt, cap, RPT_PCHAN)) {
		rpt_mutex_unlock(&myrpt->lock);
		rpt_hangup_rx_tx(myrpt);
		ao2_ref(cap, -1);
		pthread_exit(NULL);
	}

	ao2_ref(cap, -1);

	if (!myrpt->dahdirxchannel)
		myrpt->dahdirxchannel = myrpt->pchannel;
	if (!myrpt->dahditxchannel)
		myrpt->dahditxchannel = myrpt->pchannel;

	/* first put the channel on the conference in announce/monitor mode */
	if (rpt_conf_create(myrpt->pchannel, myrpt, RPT_TXCONF, RPT_CONF_CONFANNMON)) {
		rpt_mutex_unlock(&myrpt->lock);
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		pthread_exit(NULL);
	}

	rpt_equate_tx_conf(myrpt);

	/* if serial io port, open it */
	myrpt->iofd = -1;
	if (myrpt->p.ioport && ((myrpt->iofd = openserial(myrpt, myrpt->p.ioport)) == -1)) {
		rpt_mutex_unlock(&myrpt->lock);
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		ao2_ref(cap, -1);
		pthread_exit(NULL);
	}

	iskenwood_pci4 = 0;
	if ((myrpt->iofd < 1) && (myrpt->txchannel == myrpt->dahditxchannel)) {
		res = rpt_radio_set_param(myrpt->dahditxchannel, myrpt, RPT_RADPAR_REMMODE, RPT_RADPAR_REM_NONE);
		/* if PCIRADIO and kenwood selected */
		if ((!res) && (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD))) {
			if (kenwood_uio_helper(myrpt)) {
				return -1;
			}
			iskenwood_pci4 = 1;
		}
	}
	if (myrpt->txchannel == myrpt->dahditxchannel) {
		dahdi_set_onhook(myrpt->dahditxchannel);
		/* if PCIRADIO and Yaesu ft897/ICOM IC-706 selected */
		if ((myrpt->iofd < 1) && (!res) &&
			((!strcmp(myrpt->remoterig, REMOTE_RIG_FT897)) ||
			 (!strcmp(myrpt->remoterig, REMOTE_RIG_FT950)) ||
			 (!strcmp(myrpt->remoterig, REMOTE_RIG_FT100)) ||
			 (!strcmp(myrpt->remoterig, REMOTE_RIG_XCAT)) ||
			 (!strcmp(myrpt->remoterig, REMOTE_RIG_IC706)) || (!strcmp(myrpt->remoterig, REMOTE_RIG_TM271)))) {
			if (rpt_radio_set_param(myrpt->dahditxchannel, myrpt, RPT_RADPAR_UIOMODE, 1)) {
				ast_log(LOG_ERROR, "Cannot set UIOMODE on %s: %s\n", ast_channel_name(myrpt->dahditxchannel), strerror(errno));
				return -1;
			}
			if (rpt_radio_set_param(myrpt->dahditxchannel, myrpt, RPT_RADPAR_UIODATA, 3)) {
				ast_log(LOG_ERROR, "Cannot set UIODATA on %s: %s\n", ast_channel_name(myrpt->dahditxchannel), strerror(errno));
				return -1;
			}
		}
	}
	if ((myrpt->p.nlconn) && (!strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "radio") ||
		!strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "beagle") || !strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "simpleusb"))) {
		/* go thru all the specs */
		for (i = 0; i < myrpt->p.nlconn; i++) {
			int j, k;
			char string[100];

			if (sscanf(myrpt->p.lconn[i], "GPIO%d=%d", &j, &k) == 2 ||
				sscanf(myrpt->p.lconn[i], "GPIO%d:%d", &j, &k) == 2) {
				sprintf(string, "GPIO %d %d", j, k);
				ast_sendtext(myrpt->rxchannel, string);
			} else if (sscanf(myrpt->p.lconn[i], "PP%d=%d", &j, &k) == 2) {
				sprintf(string, "PP %d %d", j, k);
				ast_sendtext(myrpt->rxchannel, string);
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
	if (myrpt->p.startupmacro) {
		snprintf(myrpt->macrobuf, MAXMACRO - 1, "PPPP%s", myrpt->p.startupmacro);
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
	if (myrpt->remote && myrpt->rxchannel == myrpt->txchannel) {
		dahdi_set_echocancel(myrpt->dahdirxchannel, 128);
	}

	answer_newkey_helper(myrpt, chan, phone_mode);

	if (myrpt->rxchannel == myrpt->dahdirxchannel) {
		if (dahdi_rx_offhook(myrpt->dahdirxchannel) == 1) {
			ast_indicate(chan, AST_CONTROL_RADIO_KEY);
			myrpt->remoterx = 1;
			remkeyed = 1;
		}
	}
	/* look at callerid to see what node this comes from */
	if (!ast_channel_caller(chan)->id.number.str) {	/* if doesn't have caller id */
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
	if (myrpt->p.archivedir) {
		char mycmd[512], mydate[100];
		time_t myt;
		long blocksleft;

		mkdir(myrpt->p.archivedir, 0700);
		sprintf(mycmd, "%s/%s", myrpt->p.archivedir, myrpt->name);
		mkdir(mycmd, 0775);
		time(&myt);
		strftime(mydate, sizeof(mydate) - 1, "%Y%m%d%H%M%S", localtime(&myt));
		sprintf(mycmd, "mixmonitor start %s %s/%s/%s.wav49 a", ast_channel_name(chan), myrpt->p.archivedir, myrpt->name,
				mydate);
		if (myrpt->p.monminblocks) {
			blocksleft = diskavail(myrpt);
			if (myrpt->p.remotetimeout) {
				blocksleft -= (myrpt->p.remotetimeout * MONITOR_DISK_BLOCKS_PER_MINUTE) / 60;
			}
			if (blocksleft >= myrpt->p.monminblocks)
				ast_cli_command(nullfd, mycmd);
		} else
			ast_cli_command(nullfd, mycmd);
		sprintf(mycmd, "CONNECT,%s", b1);
		donodelog(myrpt, mycmd);
		rpt_update_links(myrpt);
		doconpgm(myrpt, b1);
	}
	/* if is a webtransceiver */
	if (myrpt->remote_webtransceiver)
		myrpt->newkey = 2;
	myrpt->loginuser[0] = 0;
	myrpt->loginlevel[0] = 0;
	myrpt->authtelltimer = 0;
	myrpt->authtimer = 0;
	authtold = 0;
	authreq = 0;
	if (myrpt->p.authlevel > 1)
		authreq = 1;
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

	rpt_mutex_lock(&myrpt->blocklock);
	if (!phone_mode) {
		send_newkey(chan);
	}
	rpt_mutex_unlock(&myrpt->blocklock);

	myfirst = 0;
	looptimestart = ast_tvnow();
	/* start un-locked */
	for (;;) {
		if (ast_check_hangup(chan) || ast_check_hangup(myrpt->rxchannel)) {
			break;
		}
		notremming = 0;
		setting = 0;
		reming = 0;
		telem = myrpt->tele.next;
		while (telem != &myrpt->tele) {
			if (telem->mode == SETREMOTE)
				setting = 1;
			if ((telem->mode == SETREMOTE) || (telem->mode == SCAN) || (telem->mode == TUNE))
				reming = 1;
			else
				notremming = 1;
			telem = telem->next;
		}
		if (myrpt->reload) {
			myrpt->reload = 0;
			load_rpt_vars_by_rpt(myrpt, 1);
		}
		time(&t);
		if (myrpt->p.remotetimeout) {
			time_t r;

			r = (t - myrpt->start_time);
			if (r >= myrpt->p.remotetimeout) {
				saynode(myrpt, chan, myrpt->name);
				sayfile(chan, "rpt/timeout");
				ast_safe_sleep(chan, 1000);
				break;
			}
			if ((myrpt->p.remotetimeoutwarning) && (r >= (myrpt->p.remotetimeout - myrpt->p.remotetimeoutwarning))
				&& (r <= (myrpt->p.remotetimeout - myrpt->p.remotetimeoutwarningfreq))) {
				if (myrpt->p.remotetimeoutwarningfreq) {
					if ((t - last_timeout_warning) >= myrpt->p.remotetimeoutwarningfreq) {
						time(&last_timeout_warning);
						rpt_telemetry(myrpt, TIMEOUT_WARNING, 0);
					}
				} else {
					if (!last_timeout_warning) {
						time(&last_timeout_warning);
						rpt_telemetry(myrpt, TIMEOUT_WARNING, 0);
					}
				}
			}
		}
		if (myrpt->p.remoteinacttimeout && myrpt->last_activity_time) {
			time_t r;

			r = (t - myrpt->last_activity_time);
			if (r >= myrpt->p.remoteinacttimeout) {
				saynode(myrpt, chan, myrpt->name);
				ast_safe_sleep(chan, 1000);
				break;
			}
			if ((myrpt->p.remotetimeoutwarning) && (r >= (myrpt->p.remoteinacttimeout - myrpt->p.remotetimeoutwarning))
				&& (r <= (myrpt->p.remoteinacttimeout - myrpt->p.remotetimeoutwarningfreq))) {
				if (myrpt->p.remotetimeoutwarningfreq) {
					if ((t - last_timeout_warning) >= myrpt->p.remotetimeoutwarningfreq) {
						time(&last_timeout_warning);
						rpt_telemetry(myrpt, ACT_TIMEOUT_WARNING, 0);
					}
				} else {
					if (!last_timeout_warning) {
						time(&last_timeout_warning);
						rpt_telemetry(myrpt, ACT_TIMEOUT_WARNING, 0);
					}
				}
			}
		}
		ms = MSWAIT;
		rpt_mutex_lock(&myrpt->blocklock);
		who = ast_waitfor_n(cs, n, &ms);
		rpt_mutex_unlock(&myrpt->blocklock);
		if (who == NULL) {
			ms = 0;
		}
		/* calculate loop time */
		looptimenow = ast_tvnow();
		elap = ast_tvdiff_ms(looptimenow, looptimestart);
		if (elap != 0) {
			looptimestart = looptimenow;
		}
		update_macrotimer(myrpt, elap);
		if (!ms) {
			/* No channels had activity. Loop again. */
			continue;
		}
		update_dtmf_local_timer(myrpt, elap);
		update_voxtimer(myrpt, elap);
		myrx = keyed;
		if (phone_mode && phone_vox) {
			myrx = (!AST_LIST_EMPTY(&myrpt->rxq));
			if (myrpt->voxtotimer <= 0) {
				voxtostate_to_voxtotimer(myrpt);
			}
			if (!myrpt->voxtostate)
				myrx = myrx || myrpt->wasvox;
		}
		keyed = myrx;
		update_rxlingertimer(myrpt, elap);
		if ((myrpt->newkey == 2) && keyed && (!myrpt->rxlingertimer)) {
			myrpt->rerxtimer = 0;
			keyed = 0;
		}
		rpt_mutex_lock(&myrpt->lock);
		do_dtmf_local(myrpt, 0);
		rpt_mutex_unlock(&myrpt->lock);
		rem_totx = myrpt->dtmf_local_timer && (!phone_mode);
		rem_totx |= keyed && (!myrpt->tunerequest);
		rem_rx = (remkeyed && (!setting)) || (myrpt->tele.next != &myrpt->tele);
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_IC706))
			rem_totx |= myrpt->tunerequest;
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT897))
			rem_totx |= (myrpt->tunetx && myrpt->tunerequest);
		if (myrpt->remstopgen < 0)
			rem_totx = 1;
		if (myrpt->remsetting)
			rem_totx = 0;
		if (rem_totx) {
			ast_debug(7, "Set rem_totx=%i.  dtmf_local_timer=%i phone_mode=%i keyed=%i tunerequest=%i\n",
					rem_totx, myrpt->dtmf_local_timer, phone_mode, keyed, myrpt->tunerequest);
		}
		if (keyed && !keyed1) {
			keyed1 = 1;
		} else if (!keyed && keyed1) {
			time_t myt;

			keyed1 = 0;
			time(&myt);
			/* if login necessary, and not too soon */
			if ((myrpt->p.authlevel) && (!myrpt->loginlevel[0]) && (myt > (t + 3))) {
				authreq = 1;
				authtold = 0;
				myrpt->authtelltimer = AUTHTELLTIME - AUTHTXTIME;
			}
		}

		if (rem_rx && !myrpt->remoterx) {
			myrpt->remoterx = 1;
			if (myrpt->newkey < 2)
				ast_indicate(chan, AST_CONTROL_RADIO_KEY);
		}
		if (!rem_rx && myrpt->remoterx) {
			myrpt->remoterx = 0;
			ast_indicate(chan, AST_CONTROL_RADIO_UNKEY);
		}
		/* if auth requested, and not authed yet */
		if (authreq && (!myrpt->loginlevel[0])) {
			if ((!authtold) && ((myrpt->authtelltimer += elap) >= AUTHTELLTIME)) {
				authtold = 1;
				rpt_telemetry(myrpt, LOGINREQ, NULL);
			}
			if ((myrpt->authtimer += elap) >= AUTHLOGOUTTIME) {
				break; /* if not logged in, hang up after a time */
			}
		}
		if (myrpt->newkey == 1) {
			if ((myrpt->retxtimer += elap) >= REDUNDANT_TX_TIME) {
				myrpt->retxtimer = 0;
				if ((myrpt->remoterx) && (!myrpt->remotetx))
					ast_indicate(chan, AST_CONTROL_RADIO_KEY);
				else
					ast_indicate(chan, AST_CONTROL_RADIO_UNKEY);
			}

			if ((myrpt->rerxtimer += elap) >= (REDUNDANT_TX_TIME * 2)) {
				keyed = 0;
				myrpt->rerxtimer = 0;
			}
		}
		if (rem_totx && (!myrpt->remotetx)) {
			/* if not authed, and needed, do not transmit */
			if ((!myrpt->p.authlevel) || myrpt->loginlevel[0]) {
				ast_debug(7, "Handle rem_totx=%i.  dtmf_local_timer=%i  tunerequest=%i\n", rem_totx,
					myrpt->dtmf_local_timer, myrpt->tunerequest);

				myrpt->remotetx = 1;
				if ((myrpt->remtxfreqok = check_tx_freq(myrpt))) {
					time(&myrpt->last_activity_time);
					telem = myrpt->tele.next;
					while (telem != &myrpt->tele) {
						if (telem->mode == ACT_TIMEOUT_WARNING && !telem->killed) {
							if (telem->chan)
								ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);	/* Whoosh! */
							telem->killed = 1;
						}
						telem = telem->next;
					}
					if (iskenwood_pci4 && myrpt->txchannel == myrpt->dahditxchannel) {
						if (rpt_radio_set_param(myrpt->dahditxchannel, myrpt, RPT_RADPAR_UIODATA, 1)) {
							ast_log(LOG_ERROR, "Cannot set UIODATA on %s: %s\n", ast_channel_name(myrpt->dahditxchannel), strerror(errno));
							return -1;
						}
					} else {
						ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_KEY);
					}
					rpt_update_boolean(myrpt, "RPT_TXKEYED", 1);
					if (myrpt->p.archivedir) {
						donodelog(myrpt, "TXKEY");
					}
				}
			}
		}
		if (!rem_totx && myrpt->remotetx) {	/* Remote base radio TX unkey */
			myrpt->remotetx = 0;
			if (!myrpt->remtxfreqok) {
				rpt_telemetry(myrpt, UNAUTHTX, NULL);
			}
			if (iskenwood_pci4 && myrpt->txchannel == myrpt->dahditxchannel) {
				if (rpt_radio_set_param(myrpt->dahditxchannel, myrpt, RPT_RADPAR_UIODATA, 3)) {
					ast_log(LOG_ERROR, "Cannot set UIODATA on %s: %s\n", ast_channel_name(myrpt->dahditxchannel), strerror(errno));
					return -1;
				}
			} else {
				ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_UNKEY);
			}
			if (myrpt->p.archivedir)
				donodelog(myrpt, "TXUNKEY");
			rpt_update_boolean(myrpt, "RPT_TXKEYED", 0);
		}
		if (myrpt->hfscanmode) {
			myrpt->scantimer -= elap;
			if (myrpt->scantimer <= 0) {
				if (!reming) {
					myrpt->scantimer = REM_SCANTIME;
					rpt_telemetry(myrpt, SCAN, 0);
				} else
					myrpt->scantimer = 1;
			}
		}
		rpt_mutex_lock(&myrpt->lock);
		c = myrpt->macrobuf[0];
		if (c && !myrpt->macrotimer) {
			myrpt->macrotimer = MACROTIME;
			memmove(myrpt->macrobuf, myrpt->macrobuf + 1, MAXMACRO - 1);
			if ((c == 'p') || (c == 'P'))
				myrpt->macrotimer = MACROPTIME;
			rpt_mutex_unlock(&myrpt->lock);
			if (myrpt->p.archivedir) {
				donodelog_fmt(myrpt, "DTMF(M),%c", c);
			}
			if (handle_remote_dtmf_digit(myrpt, c, &keyed, 0) == -1)
				break;
			continue;
		} else {
			rpt_mutex_unlock(&myrpt->lock);
		}

		if (who == chan) {		/* if it was a read from incoming */
			if (exec_chan_read(myrpt, chan, &keyed, phone_mode, phone_vox, &myfirst, &dtmfed)) {
				break;
			}
		} else if (who == myrpt->rxchannel) {	/* if it was a read from radio */
			if (exec_rxchannel_read(myrpt, reming, notremming, &remkeyed)) {
				break;
			}
		} else if (who == myrpt->telechannel) {	/* if is telemetry conference output */
			if (telechannel_read(myrpt, 0)) {
				break;
			}
		} else if (who == myrpt->btelechannel) { /* if is btelemetry conference output */
			if (btelechannel_read(myrpt)) {
				break;
			}
		} else if (who == myrpt->pchannel) {	/* if is remote mix output */
			if (exec_pchannel_read(myrpt, chan)) {
				break;
			}
		} else if (myrpt->rxchannel != myrpt->txchannel && who == myrpt->txchannel) {	/* do this cuz you have to */
			if (exec_txchannel_read(myrpt)) {
				break;
			}
		}
	}
	if (myrpt->p.archivedir || myrpt->p.discpgm) {
		char mycmd[100];

		/* look at callerid to see what node this comes from */
		if (!ast_channel_caller(chan)->id.number.str) {	/* if doesn't have caller id */
			b1 = "0";
		} else {
			ast_callerid_parse(ast_channel_caller(chan)->id.number.str, &b, &b1);
			ast_shrink_phone_number(b1);
		}
		sprintf(mycmd, "DISCONNECT,%s", b1);
		rpt_update_links(myrpt);
		if (myrpt->p.archivedir) {
			donodelog(myrpt, mycmd);
		}
		dodispgm(myrpt, b1);
	}
	myrpt->remote_webtransceiver = 0;
	/* wait for telem to be done */
	while (myrpt->tele.next != &myrpt->tele)
		usleep(50000);
	sprintf(tmp, "mixmonitor stop %s", ast_channel_name(chan));
	ast_cli_command(nullfd, tmp);
	rpt_mutex_lock(&myrpt->lock);
	myrpt->hfscanmode = 0;
	myrpt->hfscanstatus = 0;
	myrpt->remoteon = 0;
	rpt_mutex_unlock(&myrpt->lock);
	free_frame(&myrpt->lastf1);
	free_frame(&myrpt->lastf2);
	if ((iskenwood_pci4) && (myrpt->txchannel == myrpt->dahditxchannel)) {
		if (kenwood_uio_helper(myrpt)) {
			return -1;
		}
	}
	if ((myrpt->p.nldisc) && (!strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "radio") ||
		!strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "beagle") || !strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "simpleusb"))) {
		/* go thru all the specs */
		for (i = 0; i < myrpt->p.nldisc; i++) {
			int j, k;
			char string[100];

			if (sscanf(myrpt->p.ldisc[i], "GPIO%d=%d", &j, &k) == 2 ||
				sscanf(myrpt->p.ldisc[i], "GPIO%d:%d", &j, &k) == 2) {
				sprintf(string, "GPIO %d %d", j, k);
				ast_sendtext(myrpt->rxchannel, string);
			} else if (sscanf(myrpt->p.ldisc[i], "PP%d=%d", &j, &k) == 2) {
				sprintf(string, "PP %d %d", j, k);
				ast_sendtext(myrpt->rxchannel, string);
			}
		}
	}
	if (myrpt->iofd)
		close(myrpt->iofd);
	myrpt->iofd = -1;
	rpt_hangup(myrpt, RPT_PCHAN);
	rpt_hangup_rx_tx(myrpt);
	closerem(myrpt);
	if (myrpt->p.rptnode) {
		rpt_mutex_lock(&myrpt->lock);
		for (i = 0; i < nrpts; i++) {
			if (!strcasecmp(rpt_vars[i].name, myrpt->p.rptnode)) {
				rpt_vars[i].xlink = 0;
				break;
			}
		}
		rpt_mutex_unlock(&myrpt->lock);
	}
	ast_debug(1, "Finished cleaning up repeater %s, exiting with res %d\n", ast_channel_name(chan), res);
	return res;
}

static int stop_repeaters(void)
{
	int i;

	for (i = 0; i < nrpts; i++) {
		struct rpt *myrpt = &rpt_vars[i];
		if (!myrpt) {
			ast_debug(1, "No RPT at index %d?\n", i);
			continue;
		}
		if (!strcmp(rpt_vars[i].name, rpt_vars[i].p.nodes)) {
			continue;
		}
		ast_verb(3, "Hanging up repeater %s\n", rpt_vars[i].name);
		if (myrpt->rxchannel) {
			ast_verb(4, "Hanging up channel %s\n", ast_channel_name(myrpt->rxchannel));
			ast_channel_lock(myrpt->rxchannel);
			ast_softhangup(myrpt->rxchannel, AST_SOFTHANGUP_EXPLICIT); /* Hanging up one channel will signal the thread to abort */
			ast_channel_unlock(myrpt->rxchannel);
			myrpt->rxchannel = NULL; /* If we aborted the repeater but haven't unloaded, this channel handle is not valid anymore in a future call to stop_repeaters() */
		}
	}
	return 0;
}

static int unload_module(void)
{
	int i, res;

	shutting_down = 1;

	daq_uninit();

	stop_repeaters();

	ast_debug(1, "Waiting for master thread to exit\n");
	pthread_join(rpt_master_thread, NULL); /* All pseudo channels need to be hung up before we can unload the Rpt() application */
	ast_debug(1, "Master thread has now exited\n");

	/* Destroy the locks subsequently, after repeater threads have exited. Otherwise they will still be in use. */
	for (i = 0; i < nrpts; i++) {
		struct rpt *myrpt = &rpt_vars[i];
		if (!myrpt) {
			continue;
		}
		if (!strcmp(rpt_vars[i].name, rpt_vars[i].p.nodes)) {
			continue;
		}
		ast_debug(3, "Destroying locks for repeater %s\n", rpt_vars[i].name);
		ast_mutex_destroy(&rpt_vars[i].lock);
		ast_mutex_destroy(&rpt_vars[i].remlock);
		/* Lock and unlock in case somebody had the lock */
		ast_mutex_lock(&rpt_vars[i].blocklock);
		ast_mutex_unlock(&rpt_vars[i].blocklock);
		ast_mutex_destroy(&rpt_vars[i].blocklock);
	}

	res = ast_unregister_application(app);
#ifdef	_MDC_ENCODE_H_
	res |= mdc1200_unload();
#endif

	rpt_cli_unload();
	res |= rpt_manager_unload();
	close(nullfd);
	return res;
}

static int load_module(void)
{
	int res;

	nullfd = open("/dev/null", O_RDWR);
	if (nullfd == -1) {
		ast_log(LOG_ERROR, "Can not open /dev/null: %s\n", strerror(errno));
		return -1;
	}
	ast_pthread_create(&rpt_master_thread, NULL, rpt_master, NULL);

	rpt_cli_load();
	res = 0;
	res |= rpt_manager_load();
	res |= ast_register_application_xml(app, rpt_exec);

#ifdef	_MDC_ENCODE_H_
	res |= mdc1200_load();
#endif

	return res;
}

static int reload(void)
{
	int n;

	ast_mutex_lock(&rpt_master_lock);
	load_config(1);
	for (n = 0; n < nrpts; n++) {
		if (rpt_vars[n].reload1)
			continue;
		if (rpt_vars[n].rxchannel)
			ast_softhangup(rpt_vars[n].rxchannel, AST_SOFTHANGUP_DEV);
		rpt_vars[n].deleted = 1;
	}
	for (n = 0; n < nrpts; n++)
		if (!rpt_vars[n].deleted)
			rpt_vars[n].reload = 1;
	ast_mutex_unlock(&rpt_master_lock);
	return (0);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Radio Repeater/Remote Base Application",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.requires = "res_curl",
);
