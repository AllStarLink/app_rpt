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
 * \note contributions by Lee Woldanski, VE7FET
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
 *  63 - Send pre-configred APRStt notification (cop,63,CALL[,OVERLAYCHR])
 *  64 - Send pre-configred APRStt notification, quietly (cop,64,CALL[,OVERLAYCHR])
 *  65 - Send POCSAG page (equipped channel types only)
 *
 * ilink cmds:
 *
 *  1 - Disconnect specified link
 *  2 - Connect specified link -- monitor only
 *  3 - Connect specified link -- transceive
 *  4 - Enter command mode on specified link
 *  5 - System status
 *  6 - Disconnect all links
 *  7 - Last Node to Key Up
 *  8 - Connect specified link -- local monitor only
 *  9 - Send Text Message (9,<destnodeno or 0 (for all)>,Message Text, etc.
 *  10 - unused
 *  11 - Disconnect a previously permanently connected link
 *  12 - Permanently connect specified link -- monitor only
 *  13 - Permanently connect specified link -- transceive
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
 * specify actions to take place when certain events occur.
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
 *    if the variable has just gone from 0 to 1.
 * if type is 'F' (for "going False"), var-spec is a single (already-defined) variable name, and the result will be 1
 *    if the variable has just gone from 1 to 0.
 * if type is 'N' (for "no change"), var-spec is a single (already-defined) variable name, and the result will be 1
 *    if the variable has not changed.
 *
 */

/*** MODULEINFO
	<depend>tonezone</depend>
	<depend>res_curl</depend>
	<depend>curl</depend>
	<support_level>extended</support_level>
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
#include <ctype.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fnmatch.h>
#include <curl/curl.h>
#include <termios.h>
#include <stdbool.h>

#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_channel.h"
#include "asterisk/channel.h"
#include "asterisk/callerid.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/features.h"
#include "asterisk/mixmonitor.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/config.h"
#include "asterisk/say.h"
#include "asterisk/app.h"
#include "asterisk/indications.h"
#include "asterisk/format.h"
#include "asterisk/dsp.h"
#include "asterisk/audiohook.h"
#include "asterisk/core_unreal.h"

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
						Only specify this if you have checked security already (like with an IAX2 user/password or
 something).</para>
					</option>
					<option name="R">
						<para>announce-string[|timeout[|timeout-destination]] - Amateur Radio</para>
						<para>Reverse Autopatch. Caller is put on hold, and announcement (as specified by the 'announce-string')
 is played on radio system. Users of radio system can access autopatch, dial specified code, and pick up call. Announce-string is
 list of names of recordings, or <literal>PARKED</literal> to substitute code for un-parking or <literal>NODE</literal> to
 substitute node number.</para>
					</option>
					<option name="P">
						<para>Phone Control mode. This allows a regular phone user to have full control and audio access to the
 radio system. For the user to have DTMF control, the 'phone_functions' parameter must be specified for the node in 'rpt.conf'. An
 additional function (cop,6) must be listed so that PTT control is available.</para>
					</option>
					<option name="D">
						<para>Dumb Phone Control mode. This allows a regular phone user to have full control and audio access to
 the radio system. In this mode, the PTT is activated for the entire length of the call. For the user to have DTMF control (not
 generally recommended in this mode), the 'dphone_functions' parameter must be specified for the node in 'rpt.conf'. Otherwise no
 DTMF control will be available to the phone user.</para>
					</option>
					<option name="S">
						<para>Simplex Dumb Phone Control mode. This allows a regular phone user audio-only access to the radio
 system. In this mode, the transmitter is toggled on and off when the phone user presses the funcchar (*) key on the telephone
 set. In addition, the transmitter will turn off if the endchar (#) key is pressed. When a user first calls in, the transmitter
 will be off, and the user can listen for radio traffic. When the user wants to transmit, they press the * key, start talking,
 then press the * key again or the # key to turn the transmitter off.  No other functions can be executed by the user on the phone
 when this mode is selected. Note: If your radio system is full-duplex, we recommend using either P or D modes as they provide
 more flexibility.</para>
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

static int debug = 7; /* Set this >0 for extra debug output */
static int nrpts = 0;

/* general settings */
enum rpt_dns_method rpt_node_lookup_method = DEFAULT_NODE_LOOKUP_METHOD;
const char *rpt_dns_node_domain = DEFAULT_DNS_NODE_DOMAIN;
int rpt_max_dns_node_length = 6;

int max_chan_stat[] = { 22000, 1000, 22000, 100, 22000, 2000, 22000 };

int nullfd = -1;

/*! \brief Structure used to share data with attempt_reconnect thread */
struct rpt_reconnect_data {
	struct rpt *myrpt;
	struct rpt_link *l;
};

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

/*! \brief Time at which all repeaters were launched initially. Generally avoid using, and do NOT use to check if a node is ready */
static time_t starttime = 0;

time_t rpt_starttime(void)
{
	return starttime;
}

AST_MUTEX_DEFINE_STATIC(nodeloglock);

#ifndef NATIVE_DSP
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
	return (float) r.value * (float) (1 << r.power);
}

static inline void goertzel_init(goertzel_state_t *s, float freq, int samples)
{
	s->v2 = s->v3 = s->chunky = 0.0;
	s->fac = (int) (32768.0 * 2.0 * cos(2.0 * M_PI * freq / TONE_SAMPLE_RATE));
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

	/* Make sure we will have at least 5 periods at target frequency for analysis.
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

static int tone_detect(tone_detect_state_t *s, int16_t *amp, int samples)
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
const struct function_table_tag function_table[] = {
	{ "cop", function_cop, 1 },
	{ "autopatchup", function_autopatchup, 0 },
	{ "autopatchdn", function_autopatchdn, 0 },
	{ "ilink", function_ilink, 1 },
	{ "status", function_status, 1 },
	{ "remote", function_remote, 1 },
	{ "macro", function_macro, 2 },
	{ "playback", function_playback, 1 },
	{ "localplay", function_localplay, 1 },
	{ "meter", function_meter, 1 },
	{ "userout", function_userout, 1 },
	{ "cmd", function_cmd, 0 },
};

int rpt_function_lookup(const char *f)
{
	int i;

	for (i = 0; i < ARRAY_LEN(function_table); i++) {
		if (!strcasecmp(f, function_table[i].action)) {
			return i;
		}
	}
	return -1;
}

char *rpt_complete_function_list(const char *line, const char *word, int pos, int rpos)
{
	int i;
	size_t wordlen = strlen(word);

	if (pos != rpos) {
		return NULL;
	}

	for (i = 0; i < ARRAY_LEN(function_table); i++) {
		if (!strncmp(function_table[i].action, word, wordlen)) {
			ast_cli_completion_add(ast_strdup(function_table[i].action));
		}
	}
	return NULL;
}

int rpt_function_minargs(int index)
{
	return function_table[index].minargs;
}

/*! \brief format date string for archive log/file */
static char *donode_make_datestr(char *buf, size_t bufsize, time_t *timep, const char *datefmt)
{
	struct timeval t = ast_tvnow();
	struct ast_tm tm;

	ast_localtime(&t, &tm, NULL);
	if (timep) {
		*timep = t.tv_sec;
	}
	ast_strftime(buf, bufsize, datefmt ? datefmt : "%Y%m%d%H%M%S", &tm);
	return buf;
}

/*! \brief node logging function */
void donodelog(struct rpt *myrpt, char *str)
{
	struct nodelog *nodep;
	char datestr[100];

	if (!myrpt->p.archivedir) {
		return;
	}

	nodep = ast_malloc(sizeof(struct nodelog));
	if (!nodep) {
		return;
	}
	ast_copy_string(nodep->archivedir, myrpt->p.archivedir, sizeof(nodep->archivedir));
	donode_make_datestr(datestr, sizeof(datestr), &nodep->timestamp, myrpt->p.archivedatefmt);
	snprintf(nodep->str, sizeof(nodep->str), "%s %s,%s\n", myrpt->name, datestr, str);
	ast_mutex_lock(&nodeloglock);
	insque((struct qelem *) nodep, (struct qelem *) nodelog.prev);
	ast_mutex_unlock(&nodeloglock);
}

void __attribute__((format(gnu_printf, 5, 6)))
__donodelog_fmt(struct rpt *myrpt, const char *file, int lineno, const char *func, const char *fmt, ...)
{
	va_list ap;
	char *buf;
	int len;

	if (!myrpt->p.archivedir) {
		return;
	}

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
	int i, argc, varp, var1p, thisAction;
	struct ast_variable *v;
	struct ast_var_t *newvariable;

	if (!myrpt->ready) {
		return;
	}

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
		if (c == 'E') { /* if to merely evaluate the statement */
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
				if (ast_asprintf(&cmpvar, "XX_%s", argv[2]) < 0) {
					return;
				}
				var1 = (char *) pbx_builtin_getvar_helper(myrpt->rxchannel, cmpvar);
				var1p = !varp; /* start with it being opposite */
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
				case 'N': /* if no change */
					if (var1 && (varp == var1p))
						cmd = (char *) v->name;
					break;
				case 'I': /* if didn't exist (initial state) */
					if (!var1)
						cmd = (char *) v->name;
					break;
				case 'F': /* transition to false */
					if (var1 && (var1p == 1) && (varp == 0))
						cmd = (char *) v->name;
					break;
				case 'T': /* transition to true */
					if ((var1p == 0) && (varp == 1))
						cmd = (char *) v->name;
					break;
				}
			}
		}
		if (action == 'V') { /* set a variable */
			pbx_builtin_setvar_helper(myrpt->rxchannel, v->name, cmd ? "1" : "0");
			continue;
		} else if (action == 'G') { /* set a global variable */
			pbx_builtin_setvar_helper(NULL, v->name, cmd ? "1" : "0");
			continue;
		}
		/* if not command to execute, go to next one */
		if (!cmd) {
			continue;
		}
		if (action == 'F') { /* execute a function */
			ast_verb(3, "Event on node %s doing macro %s for condition %s\n", myrpt->name, cmd, v->value);
			macro_append(myrpt, cmd);
		} else if (action == 'C') { /* execute a command */
			/* make a local copy of the value of this entry */
			myval = ast_strdupa(cmd);
			/* separate out specification into comma-delimited fields */
			argc = ast_app_separate_args(myval, ',', argv, sizeof(argv) / sizeof(argv[0]));
			if (argc < 1) {
				ast_log(LOG_ERROR, "event exec rpt command item malformed: %s\n", cmd);
				continue;
			}
			/* Look up the action */
			thisAction = rpt_function_lookup(argv[0]);
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
		} else if (action == 'S') { /* execute a shell command */
			char *cp;

			ast_verb(3, "Event on node %s doing shell command %s for condition %s\n", myrpt->name, cmd, v->value);
			if (ast_asprintf(&cp, "%s &", cmd) < 0) {
				return;
			}
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
		varp = pbx_checkcondition(var) > 0;
		if (ast_asprintf(&cmpvar, "XX_%s", argv[2]) < 0) {
			return;
		}
		var1 = (char *) pbx_builtin_getvar_helper(myrpt->rxchannel, cmpvar);
		pbx_builtin_setvar_helper(myrpt->rxchannel, cmpvar, var);
		ast_free(cmpvar);
	}
	if (option_verbose < 5)
		return;
	i = 0;
	ast_debug(2, "Node Variable dump for node %s:\n", myrpt->name);
	ast_channel_lock(myrpt->rxchannel);
	AST_LIST_TRAVERSE(ast_channel_varshead(myrpt->rxchannel), newvariable, entries)
	{
		i++;
		ast_debug(2, "   %s=%s\n", ast_var_name(newvariable), ast_var_value(newvariable));
	}
	ast_channel_unlock(myrpt->rxchannel);
	ast_debug(2, "    -- %d variables\n", i);
}

static void dodispgm(struct rpt *myrpt, char *them)
{
	char *a;

	if (!myrpt->p.discpgm) {
		return;
	}
	if (ast_asprintf(&a, "%s %s %s &", myrpt->p.discpgm, myrpt->name, them) < 0) {
		return;
	}
	ast_safe_system(a);
	ast_free(a);
}

static void doconpgm(struct rpt *myrpt, char *them)
{
	char *a;

	if (!myrpt->p.connpgm) {
		return;
	}
	if (ast_asprintf(&a, "%s %s %s &", myrpt->p.connpgm, myrpt->name, them) < 0) {
		return;
	}
	ast_safe_system(a);
	ast_free(a);
	return;
}

/*! \brief Store the output of libcurl (the OK is sent to stdout) */
static size_t writefunction(char *contents, size_t size, size_t nmemb, void *userdata)
{
	struct ast_str **buffer = userdata;

	return ast_str_append(buffer, 0, "%.*s", (int) (size * nmemb), contents);
}

/* Function to check if HTTP status code is in 2xx range */
static bool is_http_success(int code)
{
	return (code >= 200 && code <= 299);
}

static const char *http_status_text(long code)
{
	switch (code) {
	case 100:
		return "Continue";
	case 101:
		return "Switching Protocols";
	case 200:
		return "OK";
	case 201:
		return "Created";
	case 204:
		return "No Content";
	case 206:
		return "Partial Content";
	case 301:
		return "Moved Permanently";
	case 302:
		return "Found";
	case 304:
		return "Not Modified";
	case 307:
		return "Temporary Redirect";
	case 308:
		return "Permanent Redirect";
	case 400:
		return "Bad Request";
	case 401:
		return "Unauthorized";
	case 403:
		return "Forbidden";
	case 404:
		return "Not Found";
	case 405:
		return "Method Not Allowed";
	case 408:
		return "Request Timeout";
	case 429:
		return "Too Many Requests";
	case 500:
		return "Internal Server Error";
	case 501:
		return "Not Implemented";
	case 502:
		return "Bad Gateway";
	case 503:
		return "Service Unavailable";
	case 504:
		return "Gateway Timeout";
	default:
		return "Unknown Status";
	}
}

static void *perform_statpost(void *data)
{
	int failed = 0;
	long rescode = 0;
	CURL *curl = curl_easy_init();
	CURLcode res;
	char error_buffer[CURL_ERROR_SIZE];
	struct ast_str *response_msg;
	struct statpost *sp = (struct statpost *) data;
	struct rpt *myrpt = sp->myrpt;

	if (!curl) {
		ast_free(sp->stats_url);
		ast_free(sp);
		return NULL;
	}

	response_msg = ast_str_create(50);
	if (!response_msg) {
		ast_free(sp->stats_url);
		ast_free(sp);
		curl_easy_cleanup(curl);
		return NULL;
	}
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunction);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) &response_msg);
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(curl, CURLOPT_URL, sp->stats_url);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, AST_CURL_USER_AGENT);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		if (*error_buffer) { /* Anything in the error buffer? */
			failed = 1;
			if (!myrpt->last_statpost_failed) {
				ast_log(LOG_WARNING, "statpost to URL '%s' failed with error: %s\n", sp->stats_url, error_buffer);
			}
		} else {
			failed = 1;
			if (!myrpt->last_statpost_failed) {
				ast_log(LOG_WARNING, "statpost to URL '%s' failed with error: %s\n", sp->stats_url, curl_easy_strerror(res));
			}
		}
	} else {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &rescode);
		if (!is_http_success(rescode)) {
			failed = 1;
			if (!myrpt->last_statpost_failed) {
				ast_log(LOG_WARNING, "statpost to URL '%s' failed with code %ld: %s\n", sp->stats_url, rescode, http_status_text(rescode));
			}
		}
	}
	myrpt->last_statpost_failed = ((failed) ? 1 : 0);

	ast_debug(5, "Response: %s\n", ast_str_buffer(response_msg));
	ast_free(sp->stats_url); /* Free here since parent is not responsible for it. */
	ast_free(sp);
	ast_free(response_msg);
	curl_easy_cleanup(curl);
	return NULL;
}

static void statpost(struct rpt *myrpt, char *pairs)
{
	time_t now;
	unsigned int seq;
	int res, len;
	pthread_t statpost_thread;
	struct statpost *sp;

	if (!myrpt->p.statpost_url) {
		return;
	}
	sp = ast_malloc(sizeof(struct statpost));
	if (!sp) {
		return;
	}

	len = strlen(pairs) + strlen(myrpt->p.statpost_url) + 200;
	sp->stats_url = ast_malloc(len);

	ast_mutex_lock(&myrpt->statpost_lock);
	seq = ++myrpt->statpost_seqno;
	ast_mutex_unlock(&myrpt->statpost_lock);

	time(&now);
	sp->myrpt = myrpt;
	snprintf(sp->stats_url, len, "%s?node=%s&time=%u&seqno=%u%s%s", myrpt->p.statpost_url, myrpt->name, (unsigned int) now, seq,
		pairs ? "&" : "", S_OR(pairs, ""));

	/* Make the actual cURL call in a separate thread, so we can continue without blocking. */
	ast_debug(5, "Making statpost to %s\n", sp->stats_url);
	res = ast_pthread_create_detached(&statpost_thread, NULL, perform_statpost, sp);
	if (res) {
		ast_log(LOG_ERROR, "Error creating statpost thread: %s\n", strerror(res));
		ast_free(sp->stats_url);
		ast_free(sp);
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
	n = finddelim(str, strs, ARRAY_LEN(strs));
	if (n < 1) {
		ast_log(LOG_ERROR, "Could not parse string '%s'\n", myrpt->p.outstreamcmd);
		ast_free(str);
		return;
	} else if (myrpt->outstreampipe[1] != -1) {
		ast_log(LOG_ERROR, "Outstream pipe already exists? (fd %d)\n", myrpt->outstreampipe[1]);
		ast_free(str);
		close(myrpt->outstreampipe[1]);
		myrpt->outstreampipe[1] = -1;
		myrpt->outstreamlasterror = 0;
	}

	if (pipe(myrpt->outstreampipe) == -1) {
		ast_log(LOG_ERROR, "pipe() failed: %s\n", strerror(errno));
		ast_free(str);
		return;
	} else if (fcntl(myrpt->outstreampipe[1], F_SETFL, O_NONBLOCK) == -1) {
		ast_log(LOG_ERROR, "Cannot set pipe to NONBLOCK: %s", strerror(errno));
		ast_free(str);
		return;
	}
	myrpt->outstreampid = ast_safe_fork(0);
	if (myrpt->outstreampid == -1) {
		ast_log(LOG_ERROR, "fork() failed: %s\n", strerror(errno));
		ast_free(str);
		close(myrpt->outstreampipe[1]);
		myrpt->outstreampipe[1] = -1;
		return;
	} else if (!myrpt->outstreampid) {
		close(myrpt->outstreampipe[1]);
		if (dup2(myrpt->outstreampipe[0], fileno(stdin)) == -1) {
			ast_log(LOG_ERROR, "Cannot dup2() stdin: %s", strerror(errno));
			exit(0);
		} else if (dup2(nullfd, fileno(stdout)) == -1) {
			ast_log(LOG_ERROR, "Cannot dup2() stdout: %s", strerror(errno));
			exit(0);
		} else if (dup2(nullfd, fileno(stderr)) == -1) {
			ast_log(LOG_ERROR, "Cannot dup2() stderr: %s", strerror(errno));
			exit(0);
		}
		ast_close_fds_above_n(STDERR_FILENO);
		execv(strs[0], strs);
		ast_log(LOG_ERROR, "exec of %s failed: %s\n", strs[0], strerror(errno));
		exit(0);
	}
	ast_free(str);
	close(myrpt->outstreampipe[0]);
	myrpt->outstreampipe[0] = -1;
	ast_debug(3, "Forked child %d\n", (int) myrpt->outstreampid);
}

static int topcompar(const void *a, const void *b)
{
	struct rpt_topkey *x = (struct rpt_topkey *) a;
	struct rpt_topkey *y = (struct rpt_topkey *) b;

	return (x->timesince - y->timesince);
}

#ifdef __RPT_NOTCH

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

struct rpt_autopatch {
	struct rpt *myrpt;
	struct ast_channel *mychannel;
	unsigned int pbx_exited:1;
};

/*!
 * \brief Create an autopatch specific pbx run thread with no_hangup_chan = 1 arg.
 * \param data 	Structure of rpt_autopatch
 */
static void *rpt_pbx_autopatch_run(void *data)
{
	struct rpt_autopatch *autopatch = data;
	struct rpt *myrpt = autopatch->myrpt;
	enum ast_pbx_result res;

	res = ast_pbx_run(autopatch->mychannel);
	if (res) { /* could not start PBX */
		rpt_mutex_lock(&myrpt->lock);
		myrpt->callmode = CALLMODE_FAILED;
		rpt_mutex_unlock(&myrpt->lock);
	} else if (myrpt->patchfarenddisconnect || (myrpt->p.duplex < 2)) { /* PBX has finished dialplan */
		ast_debug(1, "callmode=%i, patchfarenddisconnect=%i, duplex=%i\n", myrpt->callmode, myrpt->patchfarenddisconnect, myrpt->p.duplex);
		rpt_mutex_lock(&myrpt->lock);
		myrpt->callmode = CALLMODE_DOWN;
		rpt_mutex_unlock(&myrpt->lock);
		myrpt->macropatch = 0;
		if (!myrpt->patchquiet) {
			rpt_telemetry(myrpt, TERM, NULL);
		}
	} else { /* Send congestion until patch is downed by command */
		rpt_mutex_lock(&myrpt->lock);
		myrpt->callmode = CALLMODE_FAILED;
		rpt_mutex_unlock(&myrpt->lock);
	}
	autopatch->pbx_exited = 1;
	return NULL;
}

static int rpt_handle_talker_cb(struct ast_bridge_channel *bridge_cannel, void *hook_pvt, int talking)
{
	struct rpt *myrpt = hook_pvt;

	if (!myrpt) {
		return 1;
	}
	ast_debug(1, "Autopatch callback triggered: talking=%d\n", talking);
	rpt_mutex_lock(&myrpt->lock);
	myrpt->patch_talking = talking;
	rpt_mutex_unlock(&myrpt->lock);
	return 0;
}
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
	struct rpt_autopatch *patch_thread_data;
	struct ast_bridge_channel *bridge_chan;
	struct ast_unreal_pvt *p;

	pthread_t threadid;

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		myrpt->callmode = CALLMODE_DOWN;
		return NULL;
	}
	ast_format_cap_append(cap, ast_format_slin, 0);
	myrpt->mydtmf = 0;
	mychannel = rpt_request_local_chan(cap, "Autopatch");

	if (!mychannel) {
		ast_log(LOG_WARNING, "Unable to obtain AutoPatch local channel\n");
		ao2_ref(cap, -1);
		myrpt->callmode = CALLMODE_DOWN;
		return NULL;
	}
	ast_debug(1, "Requested channel %s\n", ast_channel_name(mychannel));

	/* add talker callback */
	myrpt->patch_talking = 0; /* Initialize patch_talking flag */
	p = ast_channel_tech_pvt(mychannel);
	ast_debug(1, "Adding talker callback to channel %s, private data %p\n", ast_channel_name(mychannel), p);
	if (p) {
		bridge_chan = ast_channel_get_bridge_channel(p->chan);
		if (bridge_chan) {
			bridge_chan->tech_args.talking_threshold = DEFAULT_TALKING_THRESHOLD;
			bridge_chan->tech_args.silence_threshold = VOX_OFF_DEBOUNCE_COUNT * 20; /* VOX is in 20ms count */
			ast_bridge_talk_detector_hook(bridge_chan->features, rpt_handle_talker_cb, myrpt, NULL, AST_BRIDGE_HOOK_REMOVE_ON_PULL);
			ast_debug(1, "Got Bridge channel %p\n", bridge_chan);
			ao2_ref(bridge_chan, -1);
		} else {
			ast_log(LOG_WARNING, "Failed to get Bridge channel");
		}
	} else {
		ast_log(LOG_WARNING, "Failed to get channel tech private");
	}

	genchannel = rpt_request_local_chan(cap, "GenChannel");
	ao2_ref(cap, -1);
	if (!genchannel) {
		ast_log(LOG_WARNING, "Unable to obtain Gen local channel\n");
		ast_hangup(mychannel);
		myrpt->callmode = CALLMODE_DOWN;
		return NULL;
	}
	if (rpt_conf_add(genchannel, myrpt, RPT_CONF)) {
		ast_log(LOG_WARNING, "Unable to place Gen local channel on conference\n");
		goto cleanup;
	}

	ast_autoservice_start(genchannel);
	ast_debug(1, "Created Gen channel '%s' and added to conference bridge '%s'\n", ast_channel_name(genchannel), RPT_CONF_NAME);

	if (myrpt->p.tonezone && rpt_set_tone_zone(mychannel, myrpt->p.tonezone)) {
		goto cleanup;
	}
	if (myrpt->p.tonezone && rpt_set_tone_zone(genchannel, myrpt->p.tonezone)) {
		goto cleanup;
	}

	/* start dialtone if patchquiet is 0. Special patch modes don't send dial tone */
	if (!myrpt->patchquiet && !myrpt->patchexten[0] && rpt_play_dialtone(genchannel) < 0) {
		goto cleanup;
	}
	stopped = 0;
	congstarted = 0;
	dialtimer = 0;
	lastcidx = 0;
	aborted = 0;

	/* Reverse engineering of callmode Mar 2023 NA
	 * XXX These should be converted to enums once we're sure about these, for programmer sanity.
	 * We wait up to patchdialtime for digits to be received.
	 * If there's no auto patch extension, then we'll wait for PATCH_DIALPLAN_TIMEOUT ms and then play an announcement.
	 */

	if (myrpt->patchexten[0]) {
		strcpy(myrpt->exten, myrpt->patchexten);
		myrpt->callmode = CALLMODE_CONNECTING;
	}
	while ((myrpt->callmode == CALLMODE_DIALING) || (myrpt->callmode == CALLMODE_FAILED)) {
		if ((myrpt->patchdialtime) && (myrpt->callmode == CALLMODE_DIALING) && (myrpt->cidx != lastcidx)) {
			dialtimer = 0;
			lastcidx = myrpt->cidx;
		}

		if ((myrpt->patchdialtime) && (dialtimer >= myrpt->patchdialtime)) {
			ast_debug(1, "dialtimer %i > patchdialtime %i\n", dialtimer, myrpt->patchdialtime);
			rpt_mutex_lock(&myrpt->lock);
			aborted = 1;
			myrpt->callmode = CALLMODE_DOWN;
			rpt_mutex_unlock(&myrpt->lock);
			break;
		}

		if ((!myrpt->patchquiet) && (!stopped) && (myrpt->callmode == CALLMODE_DIALING) && (myrpt->cidx > 0)) {
			stopped = 1;
			/* stop dial tone */
			rpt_stop_tone(genchannel);
		}
		if (myrpt->callmode == CALLMODE_DIALING) {
			if (myrpt->calldigittimer > PATCH_DIALPLAN_TIMEOUT) {
				myrpt->callmode = CALLMODE_CONNECTING;
				break;
			}
			/* bump timer if active */
			if (myrpt->calldigittimer)
				myrpt->calldigittimer += MSWAIT;
		}
		if (myrpt->callmode == CALLMODE_FAILED) {
			if (!congstarted) {
				congstarted = 1;
				/* start congestion tone */
				rpt_play_congestion(genchannel);
			}
		}
		usleep(MSWAIT * 1000);
		dialtimer += MSWAIT;
	}

	/* stop any tone generation */
	rpt_stop_tone(genchannel);

	/* end if done */
	if (myrpt->callmode == CALLMODE_DOWN) {
		ast_debug(1, "callmode==0\n");
		ast_hangup(mychannel);
		ast_autoservice_stop(genchannel);
		ast_hangup(genchannel);
		rpt_mutex_lock(&myrpt->lock);
		myrpt->macropatch = 0;
		rpt_mutex_unlock(&myrpt->lock);
		if ((!myrpt->patchquiet) && aborted)
			rpt_telemetry(myrpt, TERM, NULL);
		return NULL;
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
	patch_thread_data = ast_calloc(1, sizeof(struct rpt_autopatch));
	if (!patch_thread_data) {
		goto cleanup;
	}

	if (rpt_conf_add(mychannel, myrpt, RPT_CONF)) {
		ast_log(LOG_WARNING, "Unable to place AutoPatch local channel on conference\n");
		goto cleanup;
	}
	ast_debug(1, "Autopatch channel %s placed on CONF", ast_channel_name(mychannel));
	patch_thread_data->myrpt = myrpt;
	patch_thread_data->mychannel = mychannel;
	res = ast_pthread_create(&threadid, NULL, rpt_pbx_autopatch_run, patch_thread_data);
	if (res < 0) {
		ast_log(LOG_ERROR, "Unable to start PBX!\n");
		goto cleanup;
	}

	rpt_mutex_lock(&myrpt->lock);
	myrpt->callmode = CALLMODE_UP;
	sentpatchconnect = 0;
	congstarted = 0;
	while (myrpt->callmode != CALLMODE_DOWN) {
		if (!congstarted && myrpt->callmode == CALLMODE_FAILED) { /* Send congestion until patch is downed by command */
			rpt_mutex_unlock(&myrpt->lock);
			/* start congestion tone */
			rpt_play_congestion(genchannel);
			rpt_mutex_lock(&myrpt->lock);
			congstarted = 1;
		}
		if (myrpt->callmode != CALLMODE_FAILED) {
			ast_channel_lock(mychannel);
			if ((!sentpatchconnect) && myrpt->p.patchconnect && ast_channel_is_bridged(mychannel) &&
				(ast_channel_state(mychannel) == AST_STATE_UP)) {
				ast_channel_unlock(mychannel);
				sentpatchconnect = 1;
				rpt_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt, PLAYBACK, (char *) myrpt->p.patchconnect);
				rpt_mutex_lock(&myrpt->lock);
			} else {
				ast_channel_unlock(mychannel);
			}
			if (myrpt->mydtmf) {
				struct ast_frame wf = {
					.frametype = AST_FRAME_DTMF,
					.src = __PRETTY_FUNCTION__,
				};

				wf.subclass.integer = myrpt->mydtmf;
				ast_channel_lock(mychannel);
				if (ast_channel_is_bridged(mychannel) && ast_channel_state(mychannel) == AST_STATE_UP) {
					ast_channel_unlock(mychannel);
					rpt_mutex_unlock(&myrpt->lock);
					ast_queue_frame(mychannel, &wf);
					ast_senddigit(genchannel, myrpt->mydtmf, 0);
					rpt_mutex_lock(&myrpt->lock);
				} else {
					ast_channel_unlock(mychannel);
				}
			}
		}
		myrpt->mydtmf = 0;
		rpt_mutex_unlock(&myrpt->lock);
		usleep(MSWAIT * 1000);
		rpt_mutex_lock(&myrpt->lock);
	}
	ast_debug(1, "exit channel loop mode %d\n", myrpt->callmode);
	rpt_mutex_unlock(&myrpt->lock);
	rpt_stop_tone(genchannel);

	if (!patch_thread_data->pbx_exited) {
		ast_softhangup(mychannel, AST_SOFTHANGUP_DEV);
		pthread_join(threadid, NULL);
	}
	ast_autoservice_stop(genchannel);
	ast_hangup(genchannel);

	rpt_mutex_lock(&myrpt->lock);
	myrpt->callmode = CALLMODE_DOWN;
	myrpt->macropatch = 0;
	// channel_revert(myrpt);
	rpt_mutex_unlock(&myrpt->lock);

	/* first put the channel on the conference in announce mode */
	/*	if (myrpt->p.duplex == 2 || myrpt->p.duplex == 4) {
			rpt_conf_add(myrpt->pchannel, myrpt, RPT_CONF);
		}
	*/
	ast_free(patch_thread_data);
	return NULL;

cleanup:
	rpt_mutex_lock(&myrpt->lock);
	myrpt->callmode = CALLMODE_DOWN;
	rpt_mutex_unlock(&myrpt->lock);
	ast_autoservice_stop(genchannel);
	ast_hangup(mychannel);
	ast_hangup(genchannel);
	return NULL;
}

/*
 * Collect digits one by one until something matches
 */
static enum rpt_function_response collect_function_digits(struct rpt *myrpt, char *digits, int command_source, struct rpt_link *mylink)
{
	int i, rv;
	char *stringp, *action, *param, *functiondigits;
	char function_table_name[30] = "";
	char workstring[200];

	struct ast_variable *vp;

	ast_debug(7, "digits=%s  source=%d\n", digits, command_source);

	// ast_debug(1, "@@@@ Digits collected: %s, source: %d\n", digits, command_source);

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
	i = rpt_function_lookup(action);
	if (i < 0) {
		/* Error, action not in table */
		return DC_ERROR;
	}
	functiondigits = digits + strlen(vp->name);
	rv = (*function_table[i].function)(myrpt, param, functiondigits, command_source, mylink);
	ast_debug(7, "rv=%i\n", rv);
	return (rv);
}

static inline void collect_function_digits_post(struct rpt *myrpt, enum rpt_function_response res, const char *cmd, struct rpt_link *mylink)
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

/*!
 * \brief Send APRStt (Touchtone) to app_gps for processing.
 * This routine takes the received APRStt touchtone digits
 * and translates them to a callsign.  The results are
 * sent to app_gps using the APRS_SENDTT function for
 * processing and posting to the APRS-IS server.
 *
 * \param myrpt		pointer to repeater struct.
 */
static void do_aprstt(struct rpt *myrpt)
{
	char cmd[300] = "";
	char overlay, aprscall[100], func[100];

	snprintf(cmd, sizeof(cmd) - 1, "A%s", myrpt->dtmfbuf);
	/*! \todo we need to support all 4 types of APRStt
	 * we only support the 'A' type for call sign
	 */
	overlay = aprstt_xlat(cmd, aprscall);
	if (overlay) {
		ast_debug(1, "APRStt got string %s callsign %s overlay %c\n", cmd, aprscall, overlay);

		if (!ast_custom_function_find("APRS_SENDTT")) {
			ast_log(LOG_WARNING, "app_gps is not loaded.  APRStt failed\n");
		} else {
			snprintf(func, sizeof(func), "APRS_SENDTT(%s,%c)", !myrpt->p.aprstt[0] ? "general" : myrpt->p.aprstt, overlay);
			/* execute the APRS_SENDTT function in app_gps*/
			if (!ast_func_write(NULL, func, aprscall)) {
				rpt_telemetry(myrpt, ARB_ALPHA, (void *) aprscall);
			}
		}
	}
}

static int distribute_to_all_links(struct rpt *myrpt, struct rpt_link *mylink, const char *src, const char *dest, struct ast_frame *wf)
{
	struct rpt_link *l;
	struct ao2_iterator l_it;
	/* see if this is one in list */
	RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
		if (l->name[0] == '0') {
			continue;
		}
		/* dont send back from where it came */
		if (l == mylink || !strcmp(l->name, mylink->name)) {
			continue;
		}
		if (!dest || !strcmp(l->name, dest)) {
			/* send, but not to src */
			if (strcmp(l->name, src)) {
				if (l->chan) {
					rpt_qwrite(l, wf);
				}
			}
			if (dest) {
				/* if it is, send it and we're done */
				ao2_ref(l, -1);
				ao2_iterator_destroy(&l_it);
				return 1;
			}
		}
	}
	ao2_iterator_destroy(&l_it);
	return 0;
}

static inline void handle_callmode_dialing(struct rpt *myrpt, char c)
{
	myrpt->exten[myrpt->cidx++] = c;
	myrpt->exten[myrpt->cidx] = 0;
	/* if this exists */
	if (ast_exists_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
		/* if this really it, end now */
		if (!ast_matchmore_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
			myrpt->callmode = CALLMODE_CONNECTING;
			if (!myrpt->patchquiet) {
				rpt_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt, PROC, NULL);
				rpt_mutex_lock(&myrpt->lock);
			}
		} else { /* otherwise, reset timer */
			myrpt->calldigittimer = 1;
		}
	}
	/* if can continue, do so */
	if (!ast_canmatch_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
		/* call has failed, inform user */
		myrpt->callmode = CALLMODE_FAILED;
	} else { /* otherwise, reset timer */
		myrpt->calldigittimer = 1;
	}
}

/*! \brief Handle the function character. Must be called locked.
 * \param myrpt pointer to repeater struct.
 * \param c character to process.
 * \return 1 if the character was processed, 0 otherwise.
 */

static int funcchar_common(struct rpt *myrpt, char c)
{
	if (myrpt->callmode == CALLMODE_DIALING) {
		handle_callmode_dialing(myrpt, c);
	}

	if (!myrpt->inpadtest) {
		if (myrpt->p.aprstt && (!myrpt->cmdnode[0]) && (c == 'A')) {
			strcpy(myrpt->cmdnode, "aprstt");
			myrpt->dtmfidx = 0;
			myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
			time(&myrpt->dtmf_time);
			return 1;
		}
		if (c == myrpt->p.funcchar) {
			myrpt->rem_dtmfidx = 0;
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
			time(&myrpt->rem_dtmf_time);
			return 1;
		}
	}
	return 0;
}

static void handle_link_data(struct rpt *myrpt, struct rpt_link *mylink, char *str)
{
	/* XXX cmd, dst, and src should be validated. Why is remote_data src[300] in other locations?
	 * Is this a typo here?  Why would dest be any bigger than src?
	 */
	char tmp1[RPT_TMP_SZ + 1], cmd[RPT_CMD_SZ + 1] = "", dest[RPT_DEST_SZ + 1], src[RPT_SRC_SZ + 1], c;
	int i, seq, res, ts, rest;
	struct ast_frame wf;

	init_text_frame(&wf, __PRETTY_FUNCTION__);
	wf.datalen = strlen(str) + 1;
	wf.data.ptr = str;
	ast_debug(5, "Received text over link: '%s'\n", str);

	if (!strcmp(str, DISCSTR)) {
		mylink->disced = 1;
		mylink->retries = mylink->max_retries + 1;
		ast_softhangup(mylink->chan, AST_SOFTHANGUP_DEV);
		return;
	}
	if (!strcmp(str, NEWKEYSTR)) {
		if ((!mylink->link_newkey) || mylink->newkeytimer) {
			mylink->newkeytimer = 0;
			mylink->link_newkey = RADIO_KEY_ALLOWED_REDUNDANT;
			send_newkey_redundant(mylink->chan);
		}
		return;
	}
	if (!strcmp(str, NEWKEY1STR)) {
		mylink->newkeytimer = 0;
		mylink->link_newkey = RADIO_KEY_NOT_ALLOWED;
		return;
	}

	/* allow !IAXKEY! for compatibility with a no-longer-used
	   message generated by IAXRpt application. */
	if (!strncmp(str, IAXKEYSTR, sizeof(IAXKEYSTR) - 1)) {
		return;
	}
	if (*str == 'G') { /* got GPS data */
		/* re-distribute it to attached nodes */
		distribute_to_all_links(myrpt, mylink, src, NULL, &wf);
		return;
	}
	if (*str == 'L') {
		if (strlen(str) < 3) {
			return;
		}
		rpt_mutex_lock(&myrpt->lock);
		ast_str_set(&mylink->linklist, 0, "%s", str + 2); /* Dropping the "L " of the message */
		rpt_mutex_unlock(&myrpt->lock);
		ast_debug(7, "@@@@ node %s received node list %s from node %s\n", myrpt->name, str, mylink->name);
		return;
	}
	if (*str == 'M') {
		rest = 0;
		if (sscanf(str, S_FMT(RPT_CMD_SZ) S_FMT(RPT_SRC_SZ) S_FMT(RPT_DEST_SZ) "%n", cmd, src, dest, &rest) < 3) {
			ast_log(LOG_WARNING, "Unable to parse message string %s\n", str);
			return;
		}
		if (!rest)
			return;
		if (strlen(str + rest) < 2)
			return;
		/* if is from me, ignore */
		if (!strcmp(src, myrpt->name))
			return;
		/* if is for one of my nodes, dont do too much! */
		for (i = 0; i < nrpts; i++) {
			if (!strcmp(dest, rpt_vars[i].name)) {
				ast_verb(3, "Private Text Message for %s From %s: %s\n", rpt_vars[i].name, src, str + rest);
				ast_debug(1, "Node %s Got Private Text Message From Node %s: %s\n", rpt_vars[i].name, src, str + rest);
				return;
			}
		}
		/* if is for everyone, at least log it */
		if (!strcmp(dest, "0")) {
			ast_verb(3, "Text Message From %s: %s\n", src, str + rest);
			ast_debug(1, "Node %s Got Text Message From Node %s: %s\n", myrpt->name, src, str + rest);
		}
		distribute_to_all_links(myrpt, mylink, src, NULL, &wf);
		return;
	}
	if (*str == 'T') {
		if (sscanf(str, S_FMT(RPT_CMD_SZ) S_FMT(RPT_SRC_SZ) S_FMT(RPT_DEST_SZ), cmd, src, dest) != 3) {
			ast_log(LOG_WARNING, "Unable to parse telem string %s\n", str);
			return;
		}
		/* otherwise, send it to all of em */
		distribute_to_all_links(myrpt, mylink, src, NULL, &wf);
		/* if is from me, ignore */
		if (!strcmp(src, myrpt->name))
			return;

		/* set 'got T message' flag */
		mylink->gott = 1;

		/*  If inbound telemetry from a remote node, wake up from sleep if sleep mode is enabled */
		rpt_mutex_lock(&myrpt->lock); /* LOCK */
		if (myrpt->p.s[myrpt->p.sysstate_cur].sleepena) {
			myrpt->sleeptimer = myrpt->p.sleeptime;
			if (myrpt->sleep) {
				myrpt->sleep = 0;
			}
		}
		rpt_mutex_unlock(&myrpt->lock); /* UNLOCK */

		rpt_telemetry(myrpt, VARCMD, dest);
		return;
	}

	if (*str == 'C') {
		if (sscanf(str, S_FMT(RPT_CMD_SZ) S_FMT(RPT_SRC_SZ) S_FMT(RPT_TMP_SZ) S_FMT(RPT_DEST_SZ), cmd, src, tmp1, dest) != 4) {
			ast_log(LOG_WARNING, "Unable to parse ctcss string %s\n", str);
			return;
		}
		if (!strcmp(myrpt->p.ctgroup, "0"))
			return;
		if (strcasecmp(myrpt->p.ctgroup, tmp1))
			return;
		distribute_to_all_links(myrpt, mylink, src, NULL, &wf);
		/* if is from me, ignore */
		if (!strcmp(src, myrpt->name))
			return;
		snprintf(cmd, sizeof(cmd), "TXTONE %.290s", dest);
		if (IS_XPMR(myrpt))
			send_usb_txt(myrpt, cmd);
		return;
	}

	if (*str == 'K') {
		if (sscanf(str, S_FMT(RPT_CMD_SZ) S_FMT(RPT_DEST_SZ) S_FMT(RPT_SRC_SZ) N_FMT(d) N_FMT(d), cmd, dest, src, &seq, &ts) != 5) {
			ast_log(LOG_WARNING, "Unable to parse keying string %s\n", str);
			return;
		}
		if (dest[0] == '0') {
			strcpy(dest, myrpt->name);
		}
		/* if not for me, redistribute to all links */
		if (strcmp(dest, myrpt->name)) {
			if (distribute_to_all_links(myrpt, mylink, src, dest, &wf)) {
				return;
			}
		}
		/* if not for me, or is broadcast, redistribute to all links */
		if (strcmp(dest, myrpt->name) || dest[0] == '*') {
			distribute_to_all_links(myrpt, mylink, src, NULL, &wf);
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
			snprintf(tmp1, sizeof(tmp1), "K %s %s %d %d", src, myrpt->name, myrpt->keyed, n);
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
	if (*str == 'I') {
		if (sscanf(str, S_FMT(RPT_CMD_SZ) S_FMT(RPT_SRC_SZ) S_FMT(RPT_DEST_SZ), cmd, src, dest) != 3) {
			ast_log(LOG_WARNING, "Unable to parse ident string %s\n", str);
			return;
		}
		mdc1200_notify(myrpt, src, dest);
		strcpy(dest, "*");
	} else {
		if (sscanf(str, S_FMT(RPT_CMD_SZ) S_FMT(RPT_DEST_SZ) S_FMT(RPT_SRC_SZ) N_FMT(d) " %c", cmd, dest, src, &seq, &c) != 5) {
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
		if (distribute_to_all_links(myrpt, mylink, src, dest, &wf)) {
			return;
		}
		/* otherwise, send it to all of em */
		distribute_to_all_links(myrpt, mylink, src, NULL, &wf);
		return;
	}
	donodelog_fmt(myrpt, "DTMF,%s,%c", mylink->name, c);
	c = func_xlat(myrpt, c, &myrpt->p.outxlat);
	if (!c) {
		return;
	}
	rpt_mutex_lock(&myrpt->lock);
	if ((iswebtransceiver(mylink)) || CHAN_TECH(mylink->chan, "tlb")) {
		/* if a WebTransceiver node or a TLB node */
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
		rpt_mutex_unlock(&myrpt->lock);
		return;
	}
	if (myrpt->rem_dtmfidx < 0) {
		if ((myrpt->callmode == CALLMODE_CONNECTING) || (myrpt->callmode == CALLMODE_UP)) {
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

	donodelog_fmt(myrpt, "DTMF(P),%s,%c", mylink->name, c);
	if (mylink->phonemonitor)
		return;

	rpt_mutex_lock(&myrpt->lock);

	if (mylink->phonemode == RPT_PHONE_MODE_DUMB_SIMPLEX) { /*If in simplex dumb phone mode */
		if (c == myrpt->p.endchar) {						/* If end char */
			mylink->lastrealrx = 0;							/* Keying state = off */
			rpt_mutex_unlock(&myrpt->lock);
			return;
		}

		if (c == myrpt->p.funcchar) {				  /* If lead-in char */
			mylink->lastrealrx = !mylink->lastrealrx; /* Toggle keying state */
			rpt_mutex_unlock(&myrpt->lock);
			return;
		}
	} else {
		if (c == myrpt->p.endchar) {
			if (mylink->lastrx && !CHAN_TECH(mylink->chan, "echolink")) {
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
		}
	}
	if (myrpt->cmdnode[0] && strcmp(myrpt->cmdnode, "aprstt")) {
		rpt_mutex_unlock(&myrpt->lock);
		send_link_dtmf(myrpt, c);
		return;
	}
	if (funcchar_common(myrpt, c)) {
		rpt_mutex_unlock(&myrpt->lock);
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
			case RPT_PHONE_MODE_PHONE_CONTROL:
				res = collect_function_digits(myrpt, cmd, SOURCE_PHONE, mylink);
				break;
			case RPT_PHONE_MODE_DUMB_DUPLEX:
				res = collect_function_digits(myrpt, cmd, SOURCE_DPHONE, mylink);
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
}

static int handle_remote_dtmf_digit(struct rpt *myrpt, char c, char *keyed, enum rpt_phone_mode phonemode)
{
	time_t now;
	int res = 0, src;
	enum rpt_function_response ret;
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
	if (phonemode == RPT_PHONE_MODE_DUMB_DUPLEX) {
		src = SOURCE_DPHONE;
	} else if (phonemode != RPT_PHONE_MODE_NONE) {
		src = SOURCE_PHONE;
	}
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

static int handle_remote_data(struct rpt *myrpt, const char *str)
{
	/* Should src[300] be src[30] as in handle_link_data?*/
	char cmd[RPT_CMD_SZ + 1], dest[RPT_DEST_SZ + 1], src[RPT_SRC_SZ + 1], c;
	int seq, res;

	/* put string in our buffer */
	if (!strcmp(str, DISCSTR))
		return 0;
	if (!strcmp(str, NEWKEYSTR)) {
		if (!myrpt->rpt_newkey) {
			send_newkey_redundant(myrpt->rxchannel);
			myrpt->rpt_newkey = RADIO_KEY_ALLOWED_REDUNDANT;
		}
		return 0;
	}
	if (!strcmp(str, NEWKEY1STR)) {
		myrpt->rpt_newkey = RADIO_KEY_NOT_ALLOWED;
		return 0;
	}

	/* allow !IAXKEY! for compatibility with a no-longer-used
	   message generated by IAXRpt application. */
	if (!strncmp(str, IAXKEYSTR, sizeof(IAXKEYSTR) - 1)) {
		return 0;
	}

	if (*str == 'T')
		return 0;

#ifndef DO_NOT_NOTIFY_MDC1200_ON_REMOTE_BASES
	if (*str == 'I') {
		if (sscanf(str, S_FMT(RPT_CMD_SZ) S_FMT(RPT_SRC_SZ) S_FMT(RPT_DEST_SZ), cmd, src, dest) != 3) {
			ast_log(LOG_WARNING, "Unable to parse ident string %s\n", str);
			return 0;
		}
		mdc1200_notify(myrpt, src, dest);
		return 0;
	}
#endif
	if (*str == 'L') {
		return 0;
	}
	if (sscanf(str, S_FMT(RPT_CMD_SZ) S_FMT(RPT_DEST_SZ) S_FMT(RPT_SRC_SZ) N_FMT(d) " %c", cmd, dest, src, &seq, &c) != 5) {
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
	donodelog_fmt(myrpt, "DTMF,%c", c);
	c = func_xlat(myrpt, c, &myrpt->p.outxlat);
	if (!c)
		return 0;
	res = handle_remote_dtmf_digit(myrpt, c, NULL, RPT_PHONE_MODE_NONE);
	if (res != 1)
		return res;
	if ((!strcmp(myrpt->remoterig, REMOTE_RIG_TM271)) || (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD)))
		rpt_telemetry(myrpt, REMCOMPLETE, NULL);
	else
		rpt_telemetry(myrpt, COMPLETE, NULL);
	return 0;
}

static int handle_remote_phone_dtmf(struct rpt *myrpt, char c, char *restrict keyed, enum rpt_phone_mode phonemode)
{
	int res;

	if (phonemode == RPT_PHONE_MODE_DUMB_SIMPLEX) { /* simplex phonemode, funcchar key/unkey toggle */
		if (keyed && *keyed && ((c == myrpt->p.funcchar) || (c == myrpt->p.endchar))) {
			*keyed = 0; /* UNKEY */
			return 0;
		} else if (keyed && !*keyed && (c == myrpt->p.funcchar)) {
			*keyed = 1; /* KEY */
			return 0;
		}
	} else { /* endchar unkey */
		if (keyed && *keyed && (c == myrpt->p.endchar)) {
			*keyed = 0;
			return DC_INDETERMINATE;
		}
	}
	donodelog_fmt(myrpt, "DTMF(P),%c", c);
	res = handle_remote_dtmf_digit(myrpt, c, keyed, phonemode);
	if (res != 1)
		return res;
	if ((!strcmp(myrpt->remoterig, REMOTE_RIG_TM271)) || (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD)))
		rpt_telemetry(myrpt, REMCOMPLETE, NULL);
	else
		rpt_telemetry(myrpt, COMPLETE, NULL);
	return 0;
}

/*!
 * \brief Create dial string from parsed channel name
 * \param[in] s Input string
 * \param[out] s1
 * \param[out] buf
 * \param len
 * \return s2
 */
static char *parse_node_format(char *s, char **restrict s1, char *buf, size_t len)
{
	char *s2;

	*s1 = strsep(&s, ",");
	if (!strchr(*s1, ':') && strchr(*s1, '/') && strncasecmp(*s1, "local/", 6)) {
		char *sy = strchr(*s1, '/');
		*sy = 0;
		snprintf(buf, len, "%s:4569/%s", *s1, sy + 1);
		*s1 = buf;
	}
	s2 = strsep(&s, ",");
	if (!s2) {
		return NULL;
	}
	return s2;
}

static void *attempt_reconnect(void *data)
{
	char *s1, *tele;
	char tmp[300], deststr[325] = "";
	char sx[320];
	struct ast_frame *f1;
	struct ast_format_cap *cap;
	struct rpt_reconnect_data *reconnect_data = data;
	struct rpt_link *l = reconnect_data->l;
	struct rpt *myrpt = reconnect_data->myrpt;

	if (node_lookup(myrpt, l->name, tmp, sizeof(tmp) - 1, 1)) {
		ast_log(LOG_WARNING, "attempt_reconnect: cannot find node %s\n", l->name);
		rpt_mutex_lock(&myrpt->lock);
		l->retrytimer = RETRY_TIMER_MS;
		rpt_mutex_unlock(&myrpt->lock);
		goto cleanup;
	}
	/* cannot apply to echolink */
	if (!strncasecmp(tmp, "echolink", 8)) {
		goto cleanup;
	}
	/* cannot apply to tlb */
	if (!strncasecmp(tmp, "tlb", 3)) {
		goto cleanup;
	}

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		rpt_mutex_lock(&myrpt->lock);
		l->retrytimer = RETRY_TIMER_MS;
		rpt_mutex_unlock(&myrpt->lock);
		goto cleanup;
	}
	ast_format_cap_append(cap, ast_format_slin, 0);

	rpt_mutex_lock(&myrpt->lock);
	ao2_ref(l, +1);					  /* We don't want the link to free after removing from the list */
	rpt_link_remove(myrpt->links, l); /* remove from queue */		 /* Stop servicng l->pchan while we reconnect */
	ast_autoservice_start(l->pchan); /* We need to dump audio on l->chan while redialing or we recieve long voice queue warnings */
	rpt_mutex_unlock(&myrpt->lock);
	parse_node_format(tmp, &s1, sx, sizeof(sx));
	snprintf(deststr, sizeof(deststr), "IAX2/%s", s1);
	tele = strchr(deststr, '/');
	/* tele must be non-NULL here since deststr always contains at least 'IAX2/' */
	*tele++ = 0;
	l->elaptime = 0;
	l->connecttime = ast_tv(0, 0); /* not connected */
	l->thisconnected = 0;
	l->link_newkey = RADIO_KEY_ALLOWED;
	l->linkmode = 0;
	l->lastrx1 = 0;
	l->lastrealrx = 0;
	l->last_frame_sent = 0;
	l->rxlingertimer = RX_LINGER_TIME;
	l->newkeytimer = NEWKEYTIME;
	l->link_newkey = RADIO_KEY_NOT_ALLOWED;

	l->chan = ast_request(deststr, cap, NULL, NULL, tele, NULL);
	ao2_ref(cap, -1);
	while ((f1 = AST_LIST_REMOVE_HEAD(&l->textq, frame_list)))
		ast_frfree(f1);
	if (l->chan) {
		rpt_make_call(l->chan, tele, 999, deststr, "Remote Rx", "attempt_reconnect", myrpt->name);
	} else {
		ast_verb(3, "Unable to place call to %s/%s\n", deststr, tele);
		rpt_mutex_lock(&myrpt->lock);
		l->retrytimer = RETRY_TIMER_MS;
		rpt_mutex_unlock(&myrpt->lock);
	}
	rpt_mutex_lock(&myrpt->lock);
	rpt_link_add(myrpt->links, l); /* put back in queue */
	ast_autoservice_stop(l->pchan);
	ao2_ref(l, -1);				   /* and drop the extra ref we're holding */
	rpt_mutex_unlock(&myrpt->lock);
	ast_log(LOG_NOTICE, "Reconnect Attempt to %s in progress\n", l->name);
cleanup:
	l->connect_threadid = 0;
	ast_free(reconnect_data);
	return NULL;
}

/* 0 return=continue, 1 return = break, -1 return = error */
static void local_dtmf_helper(struct rpt *myrpt, char c_in)
{
	enum rpt_function_response res;
	char cmd[MAXDTMF + 1] = "", c, tone[10];

	c = c_in & 0x7f;

	snprintf(tone, sizeof(tone), "%c", c);
	rpt_manager_trigger(myrpt, "DTMF", tone);

	donodelog_fmt(myrpt, "DTMF,MAIN,%c", c);
	if (c == myrpt->p.endchar) {
		/* if in simple mode, kill autopatch */
		if (myrpt->p.simple && (myrpt->callmode != CALLMODE_DOWN)) {
			ast_log(LOG_WARNING, "simple mode autopatch kill\n");
			rpt_mutex_lock(&myrpt->lock);
			myrpt->callmode = CALLMODE_DOWN;
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
			if ((myrpt->dtmfidx == -1) && ((myrpt->callmode == CALLMODE_CONNECTING) || (myrpt->callmode == CALLMODE_UP))) {
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
				enum rpt_command_source src;

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
	} else { /* if simple */
		if ((!myrpt->callmode) && (c == myrpt->p.funcchar)) {
			myrpt->callmode = CALLMODE_DIALING;
			myrpt->patchnoct = 0;
			myrpt->patchquiet = 0;
			myrpt->patchfarenddisconnect = 0;
			myrpt->patchdialtime = 0;
			myrpt->calldigittimer = 0;
			ast_copy_string(myrpt->patchcontext, myrpt->p.ourcontext, MAXPATCHCONTEXT - 1);
			myrpt->cidx = 0;
			myrpt->exten[myrpt->cidx] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			ast_pthread_create_detached(&myrpt->rpt_call_thread, NULL, rpt_call, myrpt);
			return;
		}
	}
	if (myrpt->callmode == CALLMODE_DIALING) {
		myrpt->exten[myrpt->cidx++] = c;
		myrpt->exten[myrpt->cidx] = 0;
		/* if this exists */
		if (ast_exists_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
			/* if this really it, end now */
			if (!ast_matchmore_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
				myrpt->callmode = CALLMODE_CONNECTING;
				rpt_mutex_unlock(&myrpt->lock);
				if (!myrpt->patchquiet)
					rpt_telemetry(myrpt, PROC, NULL);
				return;
			} else { /* otherwise, reset timer */
				myrpt->calldigittimer = 1;
			}
		}
		/* if can continue, do so */
		if (!ast_canmatch_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
			/* call has failed, inform user */
			myrpt->callmode = CALLMODE_FAILED;
		}
		rpt_mutex_unlock(&myrpt->lock);
		return;
	}
	if (((myrpt->callmode == CALLMODE_CONNECTING) || (myrpt->callmode == CALLMODE_UP)) && (myrpt->dtmfidx < 0)) {
		myrpt->mydtmf = c;
	}
	rpt_mutex_unlock(&myrpt->lock);
	if ((myrpt->dtmfidx < 0) && myrpt->p.propagate_phonedtmf)
		do_dtmf_phone(myrpt, NULL, c);
}

/* place an ID event in the telemetry queue */

static void queue_id(struct rpt *myrpt)
{
	if (myrpt->p.idtime) { /* ID time must be non-zero */
		myrpt->mustid = myrpt->tailid = 0;
		myrpt->idtimer = myrpt->p.idtime; /* Reset our ID timer */
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
	if (myrpt->p.s[myrpt->p.sysstate_cur].sleepena) { /* If sleep mode enabled */
		if (myrpt->sleeptimer)
			myrpt->sleeptimer--;
		else {
			if (!myrpt->sleep)
				myrpt->sleep = 1; /* ZZZZZZ */
		}
	}
	/* Service activity timer */
	if (myrpt->p.lnkactmacro && myrpt->p.lnkacttime && myrpt->p.lnkactenable && myrpt->linkactivityflag) {
		myrpt->linkactivitytimer++;
		/* 30 second warn */
		if ((myrpt->p.lnkacttime - myrpt->linkactivitytimer == 30) && myrpt->p.lnkacttimerwarn) {
			ast_debug(5, "Warning user of activity timeout\n");
			rpt_telemetry(myrpt, LOCALPLAY, (char *) myrpt->p.lnkacttimerwarn);
		}
		if (myrpt->linkactivitytimer >= myrpt->p.lnkacttime) {
			/* Execute lnkactmacro */
			ast_debug(5, "Executing link activity timer macro %s\n", myrpt->p.lnkactmacro);
			macro_append(myrpt, myrpt->p.lnkactmacro);
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
			ast_debug(5, "Executing rpt inactivity timer macro %s\n", myrpt->p.rptinactmacro);
			macro_append(myrpt, myrpt->p.rptinactmacro);
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

	if (tmnow.tm_sec != 0) {
		return;
	}

	/* Code below only executes once per minute */
	if (myrpt->remote) { /* Don't schedule if remote */
		return;
	} else if (myrpt->p.s[myrpt->p.sysstate_cur].schedulerdisable) { /* Don't schedule if disabled */
		return;
	} else if (ast_strlen_zero(myrpt->p.skedstanzaname)) { /* No stanza means we do nothing */
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
			while ((*vp == ' ') || (*vp == 0x09)) /* get rid of any leading white space */
				vp++;
			strs[i] = vp;										/* save pointer to beginning of substring */
			while ((*vp != ' ') && (*vp != 0x09) && (*vp != 0)) /* skip over substring */
				vp++;
			if (*vp)
				*vp++ = 0; /* mark end of substring */
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
				return; /* Zero is reserved for the startup macro */
			val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.macro, skedlist->name);
			if (!val) {
				ast_log(LOG_WARNING, "Scheduler could not find macro %s\n", skedlist->name);
				return; /* Macro not found */
			}
			macro_append(myrpt, val);
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

#define rpt_hangup_rx_tx(myrpt) \
	ast_autoservice_stop(myrpt->rxchannel); \
	rpt_hangup(myrpt, RPT_RXCHAN); \
	if (myrpt->txchannel) { \
		rpt_hangup(myrpt, RPT_TXCHAN); \
	}

#define IS_DAHDI_CHAN(c) (CHAN_TECH(c, "DAHDI"))
#define IS_DAHDI_CHAN_NAME(s) (!strncasecmp(s, "DAHDI", 5))

static int rpt_setup_channels(struct rpt *myrpt, struct ast_format_cap *cap)
{
	int res = 0;

	/* make a conference for the tx */
	if (rpt_conf_create(myrpt, RPT_TXCONF)) {
		return -1;
	}

	/*! \todo Not sure what to do with types here -> need to verify what these options "mean" in ConfBridge format */
	if (myrpt->p.duplex == 2 || myrpt->p.duplex == 4) {
		res = rpt_conf_create(myrpt, RPT_CONF);
	} else {
		res = rpt_conf_create(myrpt, RPT_CONF);
	}
	if (res) {
		return -1;
	}

	if (IS_PSEUDO_NAME(myrpt->rxchanname)) {
		ast_log(LOG_ERROR, "Using DAHDI/Pseudo channel %s is depreciated. Update your rpt.conf to use Local/Pseudo.\n", myrpt->rxchanname);
		strncpy(myrpt->rxchanname, "Local/pseudo", 13);
	}
	if (rpt_request(myrpt, cap, RPT_RXCHAN)) {
		return -1;
	}
	ast_autoservice_start(myrpt->rxchannel);
	if (myrpt->txchanname) {
		if (rpt_request(myrpt, cap, RPT_TXCHAN)) {
			ast_autoservice_stop(myrpt->rxchannel);
			rpt_hangup(myrpt, RPT_RXCHAN);
			return -1;
		}
	} else {
		myrpt->txchannel = myrpt->rxchannel;
		/* If it is a DAHDI hardware channel (Not PSEUDO), use the configured txchannel. */
		myrpt->localtxchannel = IS_DAHDI_CHAN_NAME(myrpt->rxchanname) && !IS_PSEUDO_NAME(myrpt->rxchanname) ? myrpt->txchannel : NULL;
	}

	if (rpt_request_local(myrpt, cap, RPT_PCHAN, "PChan")) {
		rpt_hangup_rx_tx(myrpt);
		return -1;
	}

	if (IS_LOCAL_NAME(ast_channel_name(myrpt->txchannel))) {
		/* IF we have a local channel setup in txchannel this is a hub
		 * there is no "real" hardware and there is no listener.
		 * Autoservice is required to "dump" audio frames.
		 */
		struct ast_unreal_pvt *p = ast_channel_tech_pvt(myrpt->txchannel);
		if (!p || !p->chan) {
			ast_log(LOG_WARNING, "Local channel %s missing endpoints\n", ast_channel_name(myrpt->txchannel));
		} else {
			ast_raw_answer(p->chan);
			ast_autoservice_start(p->chan);
		}
	}

	if (!myrpt->localtxchannel) {
		if (rpt_request_local(myrpt, cap, RPT_LOCALTXCHAN, "LocalTX")) { /* Listen only link */
			rpt_hangup_rx_tx(myrpt);
			rpt_hangup(myrpt, RPT_PCHAN);
			return -1;
		}
	}

	if (rpt_conf_add(myrpt->localtxchannel, myrpt, RPT_TXCONF)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_LOCALTXCHAN);
		return -1;
	}

	if (rpt_request_local(myrpt, cap, RPT_MONCHAN, "MonChan")) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_LOCALTXCHAN);
		return -1;
	}

	if (rpt_request_local(myrpt, cap, RPT_RXPCHAN, "RXPChan")) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		return -1;
	}

	if (rpt_conf_add(myrpt->pchannel, myrpt, RPT_CONF)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_RXPCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		return -1;
	}

	if (rpt_conf_add(myrpt->rxpchannel, myrpt, RPT_CONF)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_RXPCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		return -1;
	}

	/*! \todo Need to verify always setting MONCHAN to TXCONF is "ok" or how to deal at dialtime*/

	if (rpt_conf_add(myrpt->monchannel, myrpt, RPT_TXCONF)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_RXPCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		return -1;
	}

	if (rpt_request_local(myrpt, cap, RPT_TXPCHAN, "TXPChan")) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_RXPCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
		return -1;
	}
	if (rpt_conf_add(myrpt->txpchannel, myrpt, RPT_TXCONF)) {
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_TXPCHAN);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup(myrpt, RPT_RXPCHAN);
		rpt_hangup(myrpt, RPT_MONCHAN);
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
	struct ao2_iterator l_it;

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
	ast_debug(2, "myrpt->retxtimer = %d\n", myrpt->retxtimer);
	ast_debug(2, "myrpt->totimer = %d\n", myrpt->totimer);
	ast_debug(2, "myrpt->time_out_reset_unkey_interval_timer = %d\n", myrpt->time_out_reset_unkey_interval_timer);
	ast_debug(2, "myrpt->keyed_time_ms = %d\n", myrpt->keyed_time_ms);
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
	ast_debug(2, "myrpt->rpt_newkey =%d\n", myrpt->rpt_newkey);

	RPT_LIST_TRAVERSE(myrpt->links, zl, l_it) {
		ast_debug(2, "*** Link Name: %s ***\n", zl->name);
		ast_debug(2, "        link->lasttx %d\n", zl->lasttx);
		ast_debug(2, "        link->lastrx %d\n", zl->lastrx);
		ast_debug(2, "        link->connected %d\n", zl->connected);
		ast_debug(2, "        link->hasconnected %d\n", zl->hasconnected);
		ast_debug(2, "        link->outbound %d\n", zl->outbound);
		ast_debug(2, "        link->disced %d\n", zl->disced);
		ast_debug(2, "        link->killme %d\n", zl->killme);
		ast_debug(2, "        link->disctime %d\n", zl->disctime);
		ast_debug(2, "        link->retrytimer %d\n", zl->retrytimer);
		ast_debug(2, "        link->retries = %d\n", zl->retries);
		ast_debug(2, "        link->reconnects = %d\n", zl->reconnects);
		ast_debug(2, "        link->link_newkey = %d\n", zl->link_newkey);
	}
	ao2_iterator_destroy(&l_it);
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
	if (ast_check_hangup(myrpt->txpchannel)) {
		return -1;
	}
	if (ast_check_hangup(myrpt->rxpchannel)) {
		return -1;
	}
	if (myrpt->localtxchannel && ast_check_hangup(myrpt->localtxchannel)) {
		return -1;
	}
	return 0;
}

static inline void log_keyed(struct rpt *myrpt)
{
	if (myrpt->monstream) {
		ast_closestream(myrpt->monstream);
	}
	myrpt->monstream = 0;
	if (myrpt->p.archivedir) {
		int do_archive = 0;

		if (myrpt->p.archiveaudio) {
			do_archive = 1;
			if (myrpt->p.monminblocks) {
				long blocksleft;

				blocksleft = diskavail(myrpt);
				if (blocksleft < myrpt->p.monminblocks) {
					do_archive = 0;
				}
			}
		}

		if (do_archive) {
			char mydate[100], myfname[PATH_MAX];
			const char *myformat;

			donode_make_datestr(mydate, sizeof(mydate), NULL, myrpt->p.archivedatefmt);
			snprintf(myfname, sizeof(myfname), "%s/%s/%s", myrpt->p.archivedir, myrpt->name, mydate);
			myformat = S_OR(myrpt->p.archiveformat, "wav49");
			myrpt->monstream = ast_writefile(myfname, myformat, "app_rpt Air Archive", O_CREAT | O_APPEND, 0, 0644);
		}

		donodelog(myrpt, "TXKEY,MAIN");
	}
	rpt_update_boolean(myrpt, "RPT_TXKEYED", 1);
	myrpt->txkeyed = 1;
	time(&myrpt->lasttxkeyedtime);
	myrpt->dailykeyups++;
	myrpt->totalkeyups++;
	rpt_mutex_unlock(&myrpt->lock);
	if (!IS_LOCAL(myrpt->txchannel)) {
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
	if (!IS_LOCAL(myrpt->txchannel)) {
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
		donodelog_fmt(myrpt, "RXUNKEY,%s", l->name);
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
	int newkeytimer_last, max_retries;
	struct rpt_link *l;
	struct rpt_reconnect_data *reconnect_data;
	struct ao2_iterator l_it;

	RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
		int myrx;
		if (l->chan && l->thisconnected && !AST_LIST_EMPTY(&l->textq)) {
			f = AST_LIST_REMOVE_HEAD(&l->textq, frame_list);
			ast_write(l->chan, f);
			ast_frfree(f);
		}
		update_timer(&l->rxlingertimer, elap, 0);

		/* Update the timer, checking if it expired just now. */
		newkeytimer_last = l->newkeytimer;
		update_timer(&l->newkeytimer, elap, 0);

		/* Some reverse-engineering comments here from NA debugging issue #46 (inbound calls being keyed when they shouldn't be)
		 * This if statement executes if the newkeytimer just expired.
		 * This does NOT include cases like in handle_link_data where we set newkeytimer = 0 explicitly + set newkey
		 * to RADIO_KEY_ALLOWED_REDUNDANT or RADIO_KEY_NOT_ALLOWED (because then newkeytimer_last == 0 here)
		 */
		if (newkeytimer_last > 0 && !l->newkeytimer) { /* Translation: We were timing, and it just expired */
			/* Issue #46 background:
			 *
			 * There is a kind of "handshake" that happens when setting up the IAX2 trunk between two nodes,
			 * using text frames. NEWKEY1 is part of the handshake (it does not, as the name might imply, indicate that the other
			 * side should consider either side "keyed" and transmitting... but as I explain below, the lack of sending/receiving
			 * this can actually lead to a node being improperly keyed).
			 *
			 * Ordinarily, the called node will call the send_newkey function (XXX twice, it seems, one of these may be
			 *superfluous) The calling node calls this function once. What this function does is send the text frame NEWKEY1STR to
			 *the other side. Issue #46 was concerned with a case where this was slightly broken, and the below happened: (A =
			 *calling node, B = called node)
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
			 * that is responsible for setting newkeytimer=0 and newkey=RADIO_KEY_NOT_ALLOWED, i.e. if this doesn't happen,
			 * then we'll hit the WARNING case in the below if statement.  Because this code sets l->link_newkey to
			 *RADIO_KEY_ALLOWED, There is an unintended radio keyup.
			 *
			 * Note that all of these comments are from spending hours debugging this issue and reverse-engineering, but at this
			 *point I'm pretty confident about these parts of the code, even though I'm not Jim Dixon and he didn't comment any of
			 *this code originally.
			 *
			 * The issue was resolvable by setting jitterbuffer=no in iax.conf. It seems the jitterbuffer was holding received
			 *text frames in the JB queue until it got something "important" like a voice frame. This is because chan_iax2's
			 *jitter buffer was stalling improperly until it received a voice frame, because only at that point would it try to
			 *begin reading from the jitterbuffer queue. This was fixed by falling back to the format negotiated during call setup
			 *prior to receiving audio.
			 */
			if (l->thisconnected) {
				/* We're connected, but haven't received a NEWKEY1STR text frame yet...
				 * The newkeytimer expired on a connected (~answered?) node, i.e. handle_link_data hasn't yet gotten called
				 * to set newkeytimer = 0 and newkey to RADIO_KEY_NOT_ALLOWED, i.e. we haven't received a text frame with
				 * NEWKEY1STR over the IAX2 channel yet.
				 */
				if (l->link_newkey == RADIO_KEY_NOT_ALLOWED) {
					/* This can ripple to have consequences down the line, namely we might start writing voice frames
					 * across the IAX2 link because of this, basically causing us to be transmitting (keyed).
					 * If this happens, this indicates a problem upstream, and we should emit a warning here
					 * since undesired behavior will likely ensue.
					 * We probably SHOULD just hangup the line right here if we are connected and did not receive the ~!NEWKEY1!
					 * message as something is wrong with the text messaging part of the connection.
					 */
					ast_log(LOG_WARNING, "%p newkeytimer expired on connected node, setting newkey from RADIO_KEY_NOT_ALLOWED to RADIO_KEY_ALLOWED.\n",
						l);
					l->link_newkey = RADIO_KEY_ALLOWED;
					if (l->lastrealrx) { /* We were keyed up using newkey mode, need to unkey or we will be stuck keyed up. */
						rxunkey_helper(myrpt, l);
					}
				}
			} else {
				/* If not connected yet (maybe a slow link connection?), wait another NEWKEYTIME ms (forever! - probably should
				 * limit the number of retries here)
				 */
				l->newkeytimer = NEWKEYTIME;
			}
		}
		if ((l->linkmode > 1) && (l->linkmode < 0x7ffffffe)) {
			update_timer(&l->linkmode, elap, 1);
		}
		if ((l->link_newkey == RADIO_KEY_NOT_ALLOWED) && l->lastrealrx && (!l->rxlingertimer)) {
			rxunkey_helper(myrpt, l);
		}

		update_timer(&l->voxtotimer, elap, 0);

		if (l->lasttx != l->lasttx1) {
			if ((l->phonemode == RPT_PHONE_MODE_NONE) || (!l->phonevox))
				voxinit_link(l, !l->lasttx);
			l->lasttx1 = l->lasttx;
		}
		myrx = l->lastrealrx;
		if ((l->phonemode != RPT_PHONE_MODE_NONE) && (l->phonevox)) {
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

		update_timer(&l->linklisttimer, elap, 0);

		if ((!l->linklisttimer) && (l->name[0] != '0') && (!l->isremote)) {
			struct ast_frame lf;
			struct ast_str *lstr = ast_str_create(RPT_AST_STR_INIT_SIZE);
			if (!lstr) {
				ao2_ref(l, -1);
				ao2_iterator_destroy(&l_it);
				return;
			}
			init_text_frame(&lf, __PRETTY_FUNCTION__);
			l->linklisttimer = LINKLISTTIME;
			ast_str_set(&lstr, 0, "%s", "L ");
			rpt_mutex_lock(&myrpt->lock);
			__mklinklist(myrpt, l, &lstr, 0);
			rpt_mutex_unlock(&myrpt->lock);
			if (l->chan) {
				lf.datalen = ast_str_strlen(lstr) + 1;
				lf.data.ptr = ast_str_buffer(lstr);
				rpt_qwrite(l, &lf);
				ast_debug(7, "@@@@ node %s sent node string %s to node %s\n", myrpt->name, ast_str_buffer(lstr), l->name);
			}
			ast_free(lstr);
		}
		if (l->link_newkey == RADIO_KEY_ALLOWED_REDUNDANT) {
			if ((l->retxtimer += elap) >= REDUNDANT_TX_TIME) {
				l->retxtimer = 0;
				if (l->chan && l->phonemode == RPT_PHONE_MODE_NONE) {
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
					donodelog_fmt(myrpt, "RXUNKEY(T),%s", l->name);
					if (myrpt->p.duplex)
						rpt_telemetry(myrpt, LINKUNKEY, l);
					l->lastrx1 = 0;
					rpt_update_links(myrpt);
				}
			}
		}
		update_timer(&l->disctime, elap, 0);

		update_timer(&l->retrytimer, elap, 0);

		/* start tracking connect time */
		if (ast_tvzero(l->connecttime)) {
			l->connecttime = rpt_tvnow();
		}

		/* ignore non-timing channels */
		if (l->elaptime < 0) {
			continue;
		}
		l->elaptime += elap;
		/* if connection has taken too long */
		if ((l->elaptime > MAXCONNECTTIME) && ((!l->chan) || (ast_channel_state(l->chan) != AST_STATE_UP))) {
			l->elaptime = 0;
			rpt_mutex_unlock(&myrpt->lock);
			if (l->chan)
				ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
			rpt_mutex_lock(&myrpt->lock);
			continue;
		}
		max_retries = l->retries++ >= l->max_retries && l->max_retries != MAX_RETRIES_PERM;

		if (!l->chan && !l->retrytimer && l->outbound && !max_retries && l->hasconnected) {
			rpt_mutex_unlock(&myrpt->lock);
			if ((l->name[0] > '0') && (l->name[0] <= '9') && (!l->isremote)) {
				reconnect_data = ast_calloc(1, sizeof(struct rpt_reconnect_data));
				if (!reconnect_data) {
					rpt_mutex_lock(&myrpt->lock);
					l->retrytimer = RETRY_TIMER_MS;
					continue;
				}
				reconnect_data->myrpt = myrpt;
				reconnect_data->l = l;
				if (!l->connect_threadid) {
					/* We are not currently running a connect/reconnect thread */
					if (ast_pthread_create(&l->connect_threadid, NULL, attempt_reconnect, reconnect_data) < 0) {
						ast_free(reconnect_data);
						l->connect_threadid = 0;
					}
				}
			} else {
				l->retries = l->max_retries + 1;
			}
			rpt_mutex_lock(&myrpt->lock);
			continue;
		}
		if (!l->chan && !l->retrytimer && l->outbound && max_retries) {
			ao2_ref(l, +1);					  /* prevent freeing while we finish up */
			rpt_link_remove(myrpt->links, l); /* remove from queue */
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
			donodelog_fmt(myrpt, l->hasconnected ? "LINKDISC,%s" : "LINKFAIL,%s", l->name);
			/* hang-up on call to device */
			ast_hangup(l->pchan);
			ao2_ref(l, -1); /* and drop the extra ref we're holding */
			rpt_mutex_lock(&myrpt->lock);
			continue;
		}
		if ((!l->chan) && (!l->disctime) && (!l->outbound)) {
			ast_debug(1, "LINKDISC AA\n");
			ao2_ref(l, +1);					  /* prevent freeing while we finish up */
			rpt_link_remove(myrpt->links, l); /* remove from queue */
			if (!ao2_container_count(myrpt->links)) {
				channel_revert(myrpt);
			}
			if (!strcmp(myrpt->cmdnode, l->name)) {
				myrpt->cmdnode[0] = 0;
			}
			rpt_mutex_unlock(&myrpt->lock);
			if (l->name[0] != '0') {
				rpt_telemetry(myrpt, REMDISC, l);
			}
			rpt_update_links(myrpt);
			donodelog_fmt(myrpt, "LINKDISC,%s", l->name);
			dodispgm(myrpt, l->name);
			/* hang-up on call to device */
			ast_hangup(l->pchan);
			ao2_ref(l, -1); /* and drop the extra ref we're holding */
			rpt_mutex_lock(&myrpt->lock);
			continue;
		}
	}
	ao2_iterator_destroy(&l_it);
}

/*! \brief Post keyup data to a URL configured in myrpt->p.statpost_url.
 * \note Must be called locked.  This is only called when a keypost timer
 * has been reset for a short trigger.  Otherwise this data is included
 * with a link_post message.
 * \param myrpt The rpt structure
 */
static inline void do_key_post(struct rpt *myrpt)
{
	char str[100];
	time_t now;

	time(&now);
	snprintf(str, sizeof(str), "keyed=%d&keytime=%d", myrpt->keyed, myrpt->lastkeyedtime ? ((int) (now - myrpt->lastkeyedtime)) : 0);
	rpt_mutex_unlock(&myrpt->lock);
	statpost(myrpt, str);
	rpt_mutex_lock(&myrpt->lock);
}

/*! \brief Post link data to a URL configured in myrpt->p.statpost_url.
 * \note Must be called locked.
 * \param myrpt The rpt structure
 * \retval 0 on success, -1 on failure
 */
static inline int do_link_post(struct rpt *myrpt)
{
	int nstr;
	char lst;
	struct ast_str *str;
	time_t now;
	struct rpt_link *l;
	struct ao2_iterator l_it;

	myrpt->linkposttimer = LINKPOSTTIME;

	str = ast_str_create(RPT_AST_STR_INIT_SIZE);
	if (!str) {
		return -1;
	}
	nstr = 0;
	ast_str_set(&str, 0, "%s", "nodes=");
	RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
		/* if is not a real link, ignore it */
		if (l->name[0] == '0') {
			continue;
		}
		lst = 'T';
		if (l->mode == MODE_MONITOR)
			lst = 'R';
		if (l->mode == MODE_LOCAL_MONITOR)
			lst = 'L';
		if (!l->thisconnected)
			lst = 'C';
		if (nstr)
			ast_str_append(&str, 0, "%s", ",");
		ast_str_append(&str, 0, "%c%s", lst, l->name);
		nstr = 1;
	}
	ao2_iterator_destroy(&l_it);
	time(&now);

	ast_str_append(&str, 0,
		"&apprptvers=%d.%d.%d&apprptuptime=%d&totalkerchunks=%d&totalkeyups=%d&totaltxtime=%d&timeouts=%d&totalexecdcommands=%d&"
		"keyed=%d&keytime=%d",
		VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, (int) (now - starttime), myrpt->totalkerchunks, myrpt->totalkeyups,
		(int) myrpt->totaltxtime / 1000, myrpt->timeouts, myrpt->totalexecdcommands, myrpt->keyed,
		myrpt->lastkeyedtime ? ((int) (now - myrpt->lastkeyedtime)) : 0);
	rpt_mutex_unlock(&myrpt->lock);
	statpost(myrpt, ast_str_buffer(str));
	rpt_mutex_lock(&myrpt->lock);
	ast_free(str);
	return 0;
}

static inline int update_timers(struct rpt *myrpt, const int elap, const int totx)
{
	int i;
	update_timer(&myrpt->linkposttimer, elap, 0);
	if (myrpt->linkposttimer <= 0 && do_link_post(myrpt)) {
		return -1;
	}
	if (myrpt->deferid && (!is_paging(myrpt))) {
		myrpt->deferid = 0;
		queue_id(myrpt);
	}

	/* IF a new keyup occurs, we set keypost and trigger do_key_post()
	 * otherwise, these messages are handled by do_link_post()
	 */
	if (myrpt->keypost == RPT_KEYPOST_ACTIVE) {
		myrpt->keypost = RPT_KEYPOST_NONE;
		do_key_post(myrpt);
	}

	/* Keep track of time keyed */
	if (myrpt->keyed) {
		if (myrpt->keyed_time_ms + elap < 0) {
			myrpt->keyed_time_ms = INT_MAX;
		} else {
			myrpt->keyed_time_ms += elap;
		}
	} else {
		myrpt->keyed_time_ms = 0;
	}

	if (totx) {
		myrpt->dailytxtime += elap;
		myrpt->totaltxtime += elap;
	}
	i = myrpt->tailtimer;

	update_timer(&myrpt->tailtimer, elap, 0);

	if ((i) && (myrpt->tailtimer == 0))
		myrpt->tailevent = 1;
	if (!myrpt->p.s[myrpt->p.sysstate_cur].totdisable) {
		update_timer(&myrpt->totimer, elap, 0);
	}
	update_timer(&myrpt->remote_time_out_reset_unkey_interval_timer, elap, 0);
	update_timer(&myrpt->time_out_reset_unkey_interval_timer, elap, 0);
	update_timer(&myrpt->idtimer, elap, 0);
	update_timer(&myrpt->tmsgtimer, elap, 0);
	update_timer(&myrpt->voxtotimer, elap, 0);
	if (myrpt->keyed)
		myrpt->lastkeytimer = KEYTIMERTIME;
	else {
		update_timer(&myrpt->lastkeytimer, elap, 0);
	}
	myrpt->elketimer += elap;
	if (myrpt->telemmode != 0x7fffffff) {
		update_timer(&myrpt->telemmode, elap, 1);
	}
	if (myrpt->exttx) {
		myrpt->parrottimer = myrpt->p.parrottime;
	} else {
		update_timer(&myrpt->parrottimer, elap, 0);
	}
	update_timer(&myrpt->macrotimer, elap, 0);
	update_timer(&myrpt->dtmf_local_timer, elap, 1);

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
/*!
 * \brief Update parrot channel -> parrot record timer is complete OR parrot mode changed to off
 */
static inline int update_parrot(struct rpt *myrpt)
{
	union {
		int i;
		void *p;
		char _filler[8];
	} pu;

	if (myrpt->parrotstream) {
		ast_closestream(myrpt->parrotstream);
	}
	myrpt->parrotstream = NULL;
	myrpt->parrotstate = PARROT_STATE_PLAYING;
	pu.i = myrpt->parrotcnt++;
	rpt_telemetry(myrpt, PARROT, pu.p);
	return 0;
}

static inline void process_command(struct rpt *myrpt)
{
	myrpt->cmdAction.state = CMD_STATE_EXECUTING;
	rpt_mutex_unlock(&myrpt->lock);
	(*function_table[myrpt->cmdAction.functionNumber].function)(myrpt, myrpt->cmdAction.param, myrpt->cmdAction.digits,
		myrpt->cmdAction.command_source, NULL);
	rpt_mutex_lock(&myrpt->lock);
	myrpt->cmdAction.state = CMD_STATE_IDLE;
}

static inline void stop_outstream(struct rpt *myrpt)
{
	if (myrpt->outstreampid > 0) {
		int res = kill(myrpt->outstreampid, SIGTERM);
		if (res) {
			ast_log(LOG_ERROR, "Cannot kill outstream process %d for node %s: %s\n", (int) myrpt->outstreampid, myrpt->name, strerror(errno));
		} else {
			ast_debug(3, "Sent SIGTERM to process %d\n", (int) myrpt->outstreampid);
		}
	}
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
		if ((now - myrpt->outstreamlasterror) > 59) {
			stop_outstream(myrpt);
		}
	} else {
		if (myrpt->outstreamlasterror) {
			ast_log(LOG_NOTICE, "Outstream resumed on node %s\n", myrpt->name);
			myrpt->outstreamlasterror = 0;
		}
	}
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

/*! \brief Zero data in frame_queue->lastf1 and lastf2 registers (muting audio)
 * \param frame_queue The rpt_frame_queue structure to mute
 */
static inline void rpt_frame_queue_mute(struct rpt_frame_queue *frame_queue)
{
	RPT_MUTE_FRAME(frame_queue->lastf1);
	RPT_MUTE_FRAME(frame_queue->lastf2);
}

/*! \brief Shifts frames: frame_queue->lastf2 -> return value, lastf1 -> lastf2, f -> lastf1.
 * \param frame_queue - the rpt structure
 * \param f - the frame to be stored in lastf1
 * \param mute - if true, the frame is muted by filling f, lastf1 and lastf2 with zeros
 * \note If muted, lastf1, lastf2 and f are filled with zeros before shifting the frames, resulting in a muted return frame.
 */
static inline struct ast_frame *rpt_frame_queue_helper(struct rpt_frame_queue *frame_queue, struct ast_frame *f, int mute)
{
	struct ast_frame *last_frame;

	if (mute) {
		RPT_MUTE_FRAME(f);
		rpt_frame_queue_mute(frame_queue);
	}
	last_frame = frame_queue->lastf2;
	frame_queue->lastf2 = frame_queue->lastf1;
	frame_queue->lastf1 = f ? ast_frdup(f) : NULL;
	return last_frame;
}

/*! \brief Free frame_queue frames
 * \param frame_queue The rpt_frame_queue structure to free
 */
static inline void rpt_frame_queue_free(struct rpt_frame_queue *frame_queue)
{
	free_frame(&frame_queue->lastf1);
	free_frame(&frame_queue->lastf2);
}

static int rxchannel_qwrite_cb(void *obj, void *arg, int flags)
{
	struct rpt_link *link = obj;
	struct ast_frame *wf = arg;

	/* Dont send to other then IAXRPT client */
	if ((link->name[0] != '0') || (link->phonemode)) {
		return 0;
	}
	if (link->chan) {
		rpt_qwrite(link, wf);
	}
	return 0;
}

/*! \brief Check and close parrot files if needed */
static inline void check_parrot(struct rpt *myrpt)
{
	if (!(myrpt->p.parrotmode || myrpt->parrotonce)) {
		char myfname[300];

		if (myrpt->parrotstream) {
			ast_closestream(myrpt->parrotstream);
			myrpt->parrotstream = 0;
		}

		snprintf(myfname, sizeof(myfname), PARROTFILE ".wav", myrpt->name, myrpt->parrotcnt);
		unlink(myfname);
	}
}

static inline int rxchannel_read(struct rpt *myrpt, const int lasttx)
{
	int ismuted;
	struct ast_frame *f, *f1;
	int i, dtmfed = 0;

	f = ast_read(myrpt->rxchannel);
	if (!f) {
		ast_debug(1, "@@@@ rpt:Hung Up\n");
		return -1;
	}
	check_parrot(myrpt);
	if (f->frametype == AST_FRAME_TEXT && myrpt->rxchankeyed) {
		char myrxrssi[RSSI_SZ + 1];
		if (sscanf((char *) f->data.ptr, "R " S_FMT(RSSI_SZ), myrxrssi) == 1) {
			myrpt->rxrssi = atoi(myrxrssi);
			ast_debug(8, "[%s] rxchannel rssi=%i\n", myrpt->name, myrpt->rxrssi);
			if (myrpt->p.votertype == 2)
				rssi_send(myrpt);
		}
	}

	/* if out voted drop DTMF frames */
	if (myrpt->p.votermode && !myrpt->votewinner && (f->frametype == AST_FRAME_DTMF_BEGIN || f->frametype == AST_FRAME_DTMF_END)) {
		ast_frfree(f);
		return 0;
	}

	if (f->frametype == AST_FRAME_VOICE) {
#ifdef _MDC_DECODE_H_
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
				i = (frame->frametype == AST_FRAME_DTMF && frame->subclass.integer == 'q') ? 1 : 0; /* q indicates frequency hit */
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
					myrpt->keypost = RPT_KEYPOST_ACTIVE;
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
				myrpt->keypost = RPT_KEYPOST_ACTIVE;
			}
		}
#ifdef _MDC_DECODE_H_
		if (!myrpt->reallykeyed) {
			RPT_MUTE_FRAME(f);
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
				snprintf(ustr, sizeof(ustr), "I%04X", unitID);
				mdc1200_notify(myrpt, NULL, ustr);
				mdc1200_send(myrpt, ustr);
				mdc1200_cmd(myrpt, ustr);
			}
			/* if for EMERGENCY */
			if ((op == 0) && ((arg == 0x81) || (arg == 0x80))) {
				myrpt->lastunit = unitID;
				snprintf(ustr, sizeof(ustr), "E%04X", unitID);
				mdc1200_notify(myrpt, NULL, ustr);
				mdc1200_send(myrpt, ustr);
				mdc1200_cmd(myrpt, ustr);
			}
			/* if for Stun ACK W9CR */
			if ((op == 0x0b) && (arg == 0x00)) {
				myrpt->lastunit = unitID;
				snprintf(ustr, sizeof(ustr), "STUN ACK %04X", unitID);
			}
			/* if for STS (status)  */
			if (op == 0x46) {
				myrpt->lastunit = unitID;
				snprintf(ustr, sizeof(ustr), "S%04X-%X", unitID, arg & 0xf);

#ifdef _MDC_ENCODE_H_
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
					snprintf(ustr, sizeof(ustr), "A%02X%02X-%04X", ex3 & 255, ex4 & 255, unitID);
				/* otherwise is selcall */
				else
					snprintf(ustr, sizeof(ustr), "S%02X%02X-%04X", ex3 & 255, ex4 & 255, unitID);
				mdc1200_notify(myrpt, NULL, ustr);
				mdc1200_send(myrpt, ustr);
				mdc1200_cmd(myrpt, ustr);
			}
		}
#endif
#ifdef __RPT_NOTCH
		/* apply inbound filters, if any */
		rpt_filter(myrpt, f->data.ptr, f->datalen / 2);
#endif
		if ((!myrpt->localtx) && /* (!myrpt->p.linktolink) && */
			(!myrpt->localoverride)) {
			RPT_MUTE_FRAME(f);
		}

		ismuted = dtmfed || rpt_conf_get_muted(myrpt->localrxchannel, myrpt);
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

		if (myrpt->p.votertype == 1 && myrpt->vote_counter && (myrpt->rxchankeyed || myrpt->voteremrx) &&
			(myrpt->p.votermode == 2 || (myrpt->p.votermode == 1 && !myrpt->voter_oneshot))) {
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
		f1 = rpt_frame_queue_helper(&myrpt->frame_queue, f, ismuted);
		if (f1) {
			ast_write(myrpt->localoverride ? myrpt->txpchannel : myrpt->rxpchannel, f1);
			if (((myrpt->p.duplex < 2 && !myrpt->txkeyed) || myrpt->p.duplex == 3) && myrpt->keyed) {
				if (myrpt->monstream) {
					ast_writestream(myrpt->monstream, f1);
				}
				if (myrpt->parrotstream) {
					ast_writestream(myrpt->parrotstream, f1);
				}
			}
			if ((myrpt->p.duplex < 2) && myrpt->keyed && myrpt->p.outstreamcmd && (myrpt->outstreampipe[1] != -1)) {
				outstream_write(myrpt, f1);
			}
			ast_frfree(f1);
		}
	} else if (f->frametype == AST_FRAME_DTMF_BEGIN) {
		rpt_frame_queue_mute(&myrpt->frame_queue);
		dtmfed = 1;
		myrpt->lastdtmftime = rpt_tvnow();
	} else if (f->frametype == AST_FRAME_DTMF) {
		int x;
		char c = (char) f->subclass.integer; /* get DTMF char */
		ast_frfree(f);
		x = ast_tvdiff_ms(rpt_tvnow(), myrpt->lastdtmftime);
		if ((myrpt->p.litzcmd) && (x >= myrpt->p.litztime) && strchr(myrpt->p.litzchar, c)) {
			ast_debug(1, "Doing litz command %s on node %s\n", myrpt->p.litzcmd, myrpt->name);
			macro_append(myrpt, myrpt->p.litzcmd);
			return 0;
		}
		rpt_frame_queue_mute(&myrpt->frame_queue);
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
					myrpt->keypost = RPT_KEYPOST_ACTIVE;
				}
			}
			if (myrpt->p.archivedir) {
				if (myrpt->p.duplex < 2) {
					int do_archive = 0;

					if (myrpt->p.archiveaudio) {
						do_archive = 1;
						if (myrpt->p.monminblocks) {
							long blocksleft;

							blocksleft = diskavail(myrpt);
							if (blocksleft < myrpt->p.monminblocks) {
								do_archive = 0;
							}
						}
					}

					if (do_archive) {
						char mydate[100], myfname[PATH_MAX];
						const char *myformat;

						donode_make_datestr(mydate, sizeof(mydate), NULL, myrpt->p.archivedatefmt);
						snprintf(myfname, sizeof(myfname), "%s/%s/%s", myrpt->p.archivedir, myrpt->name, mydate);
						myformat = S_OR(myrpt->p.archiveformat, "wav49");
						myrpt->monstream = ast_writefile(myfname, myformat, "app_rpt Air Archive", O_CREAT | O_APPEND, 0, 0644);
					}
				}

				donodelog(myrpt, "RXKEY,MAIN");
			}
			rpt_update_boolean(myrpt, "RPT_RXKEYED", 1);
			myrpt->elketimer = 0;
			myrpt->localoverride = 0;
			if (f->datalen && f->data.ptr) {
				int repeat = 0;
				const char *val;

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
				if (strstr(f->data.ptr, "/M/") && !myrpt->macropatch) {
					char val[16];
					strcpy(val, "*6");
					myrpt->macropatch = 1;
					macro_append(myrpt, val);
					ast_copy_string(myrpt->lasttone, f->data.ptr, sizeof(myrpt->lasttone));
				} else {
					val = ast_variable_retrieve(myrpt->cfg, myrpt->p.tonemacro, f->data.ptr);
					if (val) {
						repeat = (val[0] == TONEMACRO_REPEAT);
						/* If this is a new tone or the tone string contains the repeat command, execute the macro */
						if (repeat || strcmp(f->data.ptr, myrpt->lasttone)) {
							ast_debug(1, "Tone %s doing %s on node %s\n", (char *) f->data.ptr, repeat ? val + 1 : val, myrpt->name);
							macro_append(myrpt, repeat ? val + 1 : val); /* Drop the "R" if it's a repeat */
						}
					}
					if (!repeat) { /* Small optimization, only copy the string if we care what it was */
						ast_copy_string(myrpt->lasttone, f->data.ptr, sizeof(myrpt->lasttone));
					} else { /* otherwise do the lower cost clearing */
						myrpt->lasttone[0] = '\0';
					}
				}
			} else { /* There is no tone for this keyup */
				myrpt->lasttone[0] = '\0';
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
			myrpt->keypost = RPT_KEYPOST_ACTIVE;
			myrpt->lastdtmfuser[0] = 0;
			strcpy(myrpt->lastdtmfuser, myrpt->curdtmfuser);
			myrpt->curdtmfuser[0] = 0;
			if (myrpt->monstream && (myrpt->p.duplex < 2)) {
				ast_closestream(myrpt->monstream);
				myrpt->monstream = NULL;
			}
			donodelog(myrpt, "RXUNKEY,MAIN");
			rpt_update_boolean(myrpt, "RPT_RXKEYED", 0);
		}
	} else if (f->frametype == AST_FRAME_TEXT) { /* if a message from a USB device */
		char buf[100];
		int j;
		/* if is a USRP device */
		if (CHAN_TECH(myrpt->rxchannel, "usrp")) {
			const char *argv[4];
			int argc = 4;
			argv[2] = myrpt->name;
			argv[3] = f->data.ptr;
			rpt_do_sendall(0, argc, argv);
		}
		/* if is a USB device */
		if (CHAN_TECH(myrpt->rxchannel, "radio") || CHAN_TECH(myrpt->rxchannel, "simpleusb")) {
			/* if message parsable */
			if (sscanf(f->data.ptr, "GPIO" N_FMT(d) N_FMT(d), &i, &j) >= 2) {
				snprintf(buf, sizeof(buf), "RPT_URI_GPIO%d", i);
				rpt_update_boolean(myrpt, buf, j);
			}
			/* if message parsable */
			else if (sscanf(f->data.ptr, "PP" N_FMT(d) N_FMT(d), &i, &j) >= 2) {
				snprintf(buf, sizeof(buf), "RPT_PP%d", i);
				rpt_update_boolean(myrpt, buf, j);
			} else if (!strcmp(f->data.ptr, "ENDPAGE")) {
				myrpt->paging = ast_tv(0, 0);
			}
		}
		/* if is a Voter device */
		if (CHAN_TECH(myrpt->rxchannel, "voter")) {
			struct ast_frame wf;
			char str[200];

			if (!strcmp(f->data.ptr, "ENDPAGE")) {
				myrpt->paging = ast_tv(0, 0);
			} else {
				snprintf(str, sizeof(str), "V %s %s", myrpt->name, (char *) f->data.ptr);
				init_text_frame(&wf, "voter_text_send");
				wf.datalen = strlen(str) + 1;
				wf.data.ptr = str;
				/* otherwise, send it to all of em */
				ao2_callback(myrpt->links, OBJ_MULTIPLE | OBJ_NODATA, rxchannel_qwrite_cb, &wf);
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

static inline int localtxchannel_read(struct rpt *myrpt, char *restrict myfirst)
{
	struct ast_frame *f = ast_read(myrpt->localtxchannel);
	if (!f) {
		ast_debug(1, "@@@@ rpt:Hung Up\n");
		return -1;
	}
	if (f->frametype == AST_FRAME_VOICE) {
		struct ast_frame *f1;

		if (myrpt->p.duplex < 2) {
			int x;
			if (myrpt->txrealkeyed) {
				if (!*myfirst && (myrpt->callmode != CALLMODE_DOWN)) {
					x = 0;
					AST_LIST_TRAVERSE(&myrpt->txq, f1, frame_list) x++;
					for (; x < myrpt->p.simplexpatchdelay; x++) {
						f1 = ast_frdup(f);
						if (!f1) {
							return 0;
						}
						RPT_MUTE_FRAME(f1);
						memset(&f1->frame_list, 0, sizeof(f1->frame_list));
						AST_LIST_INSERT_TAIL(&myrpt->txq, f1, frame_list);
					}
					*myfirst = 1;
				}
				f1 = ast_frdup(f);
				if (!f1) {
					return 0;
				}
				memset(&f1->frame_list, 0, sizeof(f1->frame_list));
				AST_LIST_INSERT_TAIL(&myrpt->txq, f1, frame_list);
			} else {
				*myfirst = 0;
			}
			x = 0;
			AST_LIST_TRAVERSE(&myrpt->txq, f1, frame_list) x++;
			if (!x) {
				RPT_MUTE_FRAME(f);
			} else {
				ast_frfree(f);
				f = AST_LIST_REMOVE_HEAD(&myrpt->txq, frame_list);
			}
		} else {
			while ((f1 = AST_LIST_REMOVE_HEAD(&myrpt->txq, frame_list)))
				ast_frfree(f1);
		}
		ast_write(myrpt->txchannel, f);
	}
	return hangup_frame_helper(myrpt->localtxchannel, "localtxchannel", f);
}

/*! \brief Safely hang up any channel, even if a PBX could be running on it */
static inline void safe_hangup(struct ast_channel *chan)
{
	/* myrpt is locked here, so we can trust this will be an atomic operation,
	 * since we also lock before setting the pbx to NULL */
	if (ast_channel_pbx(chan)) {
		ast_log(LOG_WARNING, "Channel %s still has a PBX, requesting hangup for it\n", ast_channel_name(chan));
		ast_softhangup(chan, AST_SOFTHANGUP_EXPLICIT);
	} else {
		ast_debug(3, "Hard hanging up channel %s\n", ast_channel_name(chan));
		ast_hangup(chan);
	}
}

/*! \note myrpt->lock must be held when calling */
static inline void hangup_link_chan(struct rpt_link *l)
{
	if (l->chan) {
		safe_hangup(l->chan);
		l->chan = NULL;
	}
}

/*!
 * \internal
 * \brief Final cleanup of link prior to node termination
 */
static void remote_hangup_helper(struct rpt *myrpt, struct rpt_link *l)
{
	rpt_mutex_lock(&myrpt->lock);
	__kickshort(myrpt);
	rpt_mutex_unlock(&myrpt->lock);

	if (!CHAN_TECH(l->chan, "echolink") && !CHAN_TECH(l->chan, "tlb")) {
		/* If neither echolink nor tlb */
		if ((!l->disced) && (!l->outbound)) {
			if ((l->name[0] <= '0') || (l->name[0] > '9') || l->isremote)
				l->disctime = 1;
			else
				l->disctime = DISC_TIME;
			rpt_mutex_lock(&myrpt->lock);
			hangup_link_chan(l);
			rpt_mutex_unlock(&myrpt->lock);
			return;
		}

		if (l->retrytimer) {
			rpt_mutex_lock(&myrpt->lock);
			hangup_link_chan(l);
			rpt_mutex_unlock(&myrpt->lock);
			return;
		}
		if (l->outbound && (l->retries++ < l->max_retries) && (l->hasconnected)) {
			rpt_mutex_lock(&myrpt->lock);
			hangup_link_chan(l);
			l->hasconnected = 1; /*! \todo BUGBUG XXX l->hasconnected has to be true to get here, why set it again? Is this a typo? */
			l->retrytimer = RETRY_TIMER_MS;
			l->elaptime = 0;
			l->connecttime = ast_tv(0, 0); /* no longer connected */
			l->thisconnected = 0;
			rpt_mutex_unlock(&myrpt->lock);
			return;
		}
	}

	rpt_mutex_lock(&myrpt->lock);
	ao2_ref(l, +1);					  /* prevent freeing while we finish up */
	rpt_link_remove(myrpt->links, l); /* remove from queue */
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
	donodelog_fmt(myrpt, l->hasconnected ? "LINKDISC,%s" : "LINKFAIL,%s", l->name);
	if (l->hasconnected) {
		dodispgm(myrpt, l->name);
	}
	rpt_frame_queue_free(&l->frame_queue);

	rpt_mutex_lock(&myrpt->lock);
	/* hang-up on call to device */
	hangup_link_chan(l);
	rpt_mutex_unlock(&myrpt->lock);

	ast_hangup(l->pchan);
	ao2_ref(l, -1); /* and drop the extra ref we're holding */
}

static inline void rxkey_helper(struct rpt *myrpt, struct rpt_link *l)
{
	ast_debug(7, "@@@@ rx key\n");
	l->lastrealrx = 1;
	l->rerxtimer = 0;
	if (!l->lastrx1) {
		donodelog_fmt(myrpt, "RXKEY,%s", l->name);
		l->lastrx1 = 1;
		rpt_update_links(myrpt);
		time(&l->lastkeytime);
	}
}

/*! \retval -1 to exit and terminate the node, 0 to continue */
static inline int process_link_channels(struct rpt *myrpt, struct ast_channel *who, char *restrict myfirst)
{
	struct rpt_link *l, *m;
	struct ast_frame wf = {
		.frametype = AST_FRAME_CNG,
		.src = __PRETTY_FUNCTION__,
	};
	struct ao2_iterator l_it, l_it2;
	int totx;
	/* @@@@@ LOCK @@@@@ */
	rpt_mutex_lock(&myrpt->lock);

	RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
		int remnomute, remrx;
		struct timeval now;

		if (l->disctime) {
			/* We are disconnected but still need to read and discard frames */
			if (who == l->pchan) {
				struct ast_frame *f;
				f = ast_read(l->pchan);
				if (!f) {
					ast_debug(1, "@@@@ rpt:Hung Up\n");
					ast_mutex_unlock(&myrpt->lock);
					return -1;
				}
				ast_frfree(f);
				ast_mutex_unlock(&myrpt->lock);
				return 0;
			} else {
					continue;
			}
		}

		remrx = 0;
		/* see if any other links are receiving */
		RPT_LIST_TRAVERSE(myrpt->links, m, l_it2) {
			/* if not the link we are currently processing, and not localonly count it */
			if ((m != l) && (m->lastrx) && (m->mode < 2)) {
				remrx = 1;
			}
		}
		ao2_iterator_destroy(&l_it2);
		rpt_mutex_unlock(&myrpt->lock);

		now = rpt_tvnow();
		if ((who == l->chan) || (!l->lastlinktv.tv_sec) || (ast_tvdiff_ms(now, l->lastlinktv) >= 19)) {
			char mycalltx;

			l->lastlinktv = now;
			remnomute = myrpt->localtx && (!(myrpt->cmdnode[0] || (myrpt->dtmfidx > -1)));
			mycalltx = myrpt->callmode;
#ifdef DONT_USE__CAUSES_CLIPPING_OF_FIRST_SYLLABLE_ON_LINK
			if (myrpt->patchvoxalways)
				mycalltx = mycalltx && ((!myrpt->voxtostate) && myrpt->wasvox);
#endif
			totx = ((l->isremote) ? (remnomute) : (myrpt->localtx && myrpt->totimer) || mycalltx) || remrx;

			/* foop */
			if ((!l->lastrx) && altlink(myrpt, l))
				totx = myrpt->txkeyed;
			if (altlink1(myrpt, l))
				totx = 1;
			l->wouldtx = totx;
			if (l->mode != MODE_TRANSCEIVE)
				totx = 0;
			if (l->phonemode == RPT_PHONE_MODE_NONE && l->chan && (l->lasttx != totx)) {
				if (totx && !l->voterlink) {
					if (l->link_newkey != RADIO_KEY_NOT_ALLOWED)
						ast_indicate(l->chan, AST_CONTROL_RADIO_KEY);
				} else {
					ast_indicate(l->chan, AST_CONTROL_RADIO_UNKEY);
					if (l->last_frame_sent) {
						ast_write(l->chan, &wf);
						l->last_frame_sent = 0;
					}
				}
				donodelog_fmt(myrpt, totx ? "TXKEY,%s" : "TXUNKEY,%s", l->name);
			}
			l->lasttx = totx;
		}

		rpt_mutex_lock(&myrpt->lock);
		if (who == l->chan) { /* if it was a read from rx */
			struct ast_frame *f;
			rpt_mutex_unlock(&myrpt->lock);
			f = ast_read(l->chan);
			if (!f) {
				ast_debug(3, "Failed to read frame on %s, must've hung up\n", ast_channel_name(l->chan));
				remote_hangup_helper(myrpt, l);
				ao2_ref(l, -1);
				ao2_iterator_destroy(&l_it);
				return 0;
			}
			if (f->frametype == AST_FRAME_VOICE) {
				int ismuted, n1;
				float fac;

				fac = 1.0;
				if (l->chan) {
					if (CHAN_TECH(l->chan, "echolink")) {
						fac = myrpt->p.erxgain;
					} else if (CHAN_TECH(l->chan, "tlb")) {
						fac = myrpt->p.trxgain;
					}
				}
				if ((myrpt->p.linkmongain != 1.0) && (l->mode != MODE_TRANSCEIVE) && (l->wouldtx))
					fac *= myrpt->p.linkmongain;
				if (fac != 1.0) {
					if (f->data.ptr && (f->samples == f->datalen / 2)) {
						ast_frame_adjust_volume_float(f, fac);
					} else {
						ast_debug(3, "Skip volume adjust on %s, fac = %f, data = %p, datalen = %d, samples = %d, src = %s\n",
							ast_channel_name(l->chan), fac, f->data.ptr, f->datalen, f->samples, f->src ? f->src : "(nil)");
					}
				}

				l->rxlingertimer = RX_LINGER_TIME;

				if ((l->link_newkey == RADIO_KEY_NOT_ALLOWED) && (!l->lastrealrx)) {
					rxkey_helper(myrpt, l);
				}
				if (((l->phonemode != RPT_PHONE_MODE_NONE) && (l->phonevox)) || (CHAN_TECH(l->chan, "echolink")) ||
					(CHAN_TECH(l->chan, "tlb"))) {
					struct ast_frame *f1;
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
									if (!f1) {
										ao2_ref(l, -1);
										ao2_iterator_destroy(&l_it);
										return 0;
									}
									RPT_MUTE_FRAME(f1);
									memset(&f1->frame_list, 0, sizeof(f1->frame_list));
									AST_LIST_INSERT_TAIL(&l->rxq, f1, frame_list);
								}
								*myfirst = 1;
							}
							f1 = ast_frdup(f);
							if (!f1) {
								ao2_ref(l, -1);
								ao2_iterator_destroy(&l_it);
								return 0;
							}
							memset(&f1->frame_list, 0, sizeof(f1->frame_list));
							AST_LIST_INSERT_TAIL(&l->rxq, f1, frame_list);
						} else {
							*myfirst = 0;
						}
						x = 0;
						AST_LIST_TRAVERSE(&l->rxq, f1, frame_list) x++;
						if (!x) {
							RPT_MUTE_FRAME(f);
						} else {
							ast_frfree(f);
							f = AST_LIST_REMOVE_HEAD(&l->rxq, frame_list);
						}
					}
					ismuted = rpt_conf_get_muted(l->chan, myrpt);
					/* if not receiving, zero-out audio */
					ismuted |= (!l->lastrx);
					if (l->dtmfed &&
						((l->phonemode != RPT_PHONE_MODE_NONE) || (CHAN_TECH(l->chan, "echolink")) || (CHAN_TECH(l->chan, "tlb")))) {
						ismuted = 1;
					}
					l->dtmfed = 0;

					/* if a voting rx link and not the winner, mute audio */
					if (myrpt->p.votertype == 1 && l->voterlink && myrpt->voted_link != l) {
						ismuted = 1;
					}

					f1 = rpt_frame_queue_helper(&l->frame_queue, f, ismuted);
					if (f1) {
						ast_write(l->pchan, f1);
						ast_frfree(f1);
					}
				} else {
					/* if a voting rx link and not the winner, mute audio */
					ismuted = (myrpt->p.votertype == 1) && l->voterlink && (myrpt->voted_link != l);
					if (!l->lastrx || ismuted)
						RPT_MUTE_FRAME(f);
					ast_write(l->pchan, f);
				}
			} else if (f->frametype == AST_FRAME_DTMF_BEGIN) {
				rpt_frame_queue_mute(&l->frame_queue);
				l->dtmfed = 1;
			} else if (f->frametype == AST_FRAME_TEXT) {
				char *tstr = ast_malloc(f->datalen + 1);
				if (tstr) {
					memcpy(tstr, f->data.ptr, f->datalen);
					tstr[f->datalen] = 0;
					handle_link_data(myrpt, l, tstr);
					ast_free(tstr);
				}
			} else if (f->frametype == AST_FRAME_DTMF) {
				rpt_frame_queue_mute(&l->frame_queue);
				l->dtmfed = 1;
				handle_link_phone_dtmf(myrpt, l, f->subclass.integer);
			} else if (f->frametype == AST_FRAME_CONTROL) {
				if (f->subclass.integer == AST_CONTROL_ANSWER) {
					char lconnected = l->connected;

					__kickshort(myrpt);
					myrpt->rxlingertimer = RX_LINGER_TIME;
					l->connected = 1;
					l->hasconnected = 1;
					l->thisconnected = 1;
					l->elaptime = -1;
					if (l->phonemode == RPT_PHONE_MODE_NONE) {
						send_newkey(l->chan);
					}
					if (!l->isremote)
						l->retries = 0;
					if (!lconnected) {
						rpt_telemetry(myrpt, CONNECTED, l);
						if (l->mode == MODE_TRANSCEIVE) {
							donodelog_fmt(myrpt, "LINKTRX,%s", l->name);
						} else if (l->mode == MODE_LOCAL_MONITOR) {
							donodelog_fmt(myrpt, "LINKLOCALMONITOR,%s", l->name);
						} else {
							donodelog_fmt(myrpt, "LINKMONITOR,%s", l->name);
						}
						rpt_update_links(myrpt);
						doconpgm(myrpt, l->name);
					} else
						l->reconnects++;
				}
				/* if RX key */
				if ((f->subclass.integer == AST_CONTROL_RADIO_KEY) && (l->link_newkey != RADIO_KEY_NOT_ALLOWED)) {
					rxkey_helper(myrpt, l);
				}
				/* if RX un-key */
				if (f->subclass.integer == AST_CONTROL_RADIO_UNKEY) {
					rxunkey_helper(myrpt, l);
				}
				if (f->subclass.integer == AST_CONTROL_HANGUP) {
					ast_frfree(f);
					ast_debug(3, "Received hangup frame on %s\n", ast_channel_name(l->chan));
					remote_hangup_helper(myrpt, l);
					ao2_ref(l, -1);
					ao2_iterator_destroy(&l_it);
					return 0;
				}
			}
			ast_frfree(f);
			ao2_ref(l, -1);
			ao2_iterator_destroy(&l_it);
			return 0;
		} else if (who == l->pchan) {
			struct ast_frame *f;
			rpt_mutex_unlock(&myrpt->lock);
			f = ast_read(l->pchan);
			if (!f) {
				ast_debug(1, "@@@@ rpt:Hung Up\n");
				ao2_ref(l, -1);
				ao2_iterator_destroy(&l_it);
				return -1;
			}
			if (f->frametype == AST_FRAME_VOICE) {
				float fac = 1.0;
				if (l->chan) {
					if (CHAN_TECH(l->chan, "echolink")) {
						fac = myrpt->p.etxgain;
					} else if (CHAN_TECH(l->chan, "tlb")) {
						fac = myrpt->p.ttxgain;
					}
				}
				if (fac != 1.0) {
					if (f->data.ptr && (f->samples == f->datalen / 2)) {
						ast_frame_adjust_volume_float(f, fac);
					} else {
						ast_debug(3, "Skip volume adjust on %s, fac = %f, data = %p, datalen = %d, samples = %d, src = %s\n",
							ast_channel_name(l->chan), fac, f->data.ptr, f->datalen, f->samples, f->src ? f->src : "(nil)");
					}
				}
				/* foop */
				if (l->chan && (l->lastrx || (!altlink(myrpt, l))) &&
					((l->link_newkey != RADIO_KEY_NOT_ALLOWED) || l->lasttx || !CHAN_TECH(l->chan, "IAX2"))) {
					/* Reverse-engineering comments from NA debugging issue #46:
					 * We may be receiving frames from channel drivers but we discard them and don't pass them on if newkey is set
					 * to != RADIO_KEY_NOT_ALLOWED yet. This happens when the reset code forces it to RADIO_ALLOWED. Of course if
					 * handle_link_data is never called to set newkey to RADIO_KEY_NOT_ALLOWED and stop newkeytimer, then at some
					 * point, we'll set newkey = RADIO_KEY_ALLOWED forcibly (see comments in that part of the code for more info),
					 * If this happens, we're passing voice frames and now sending AST_READIO_KEY messages
					 * so we're keyed up and transmitting, essentially, which we don't want to happen.
					 *
					 */
					ast_write(l->chan, f);
					l->last_frame_sent = 1;
				}
			}
			if (f->frametype == AST_FRAME_CONTROL && f->subclass.integer == AST_CONTROL_HANGUP) {
				ast_debug(1, "@@@@ rpt:Hung Up\n");
				ast_frfree(f);
				ao2_ref(l, -1);
				ao2_iterator_destroy(&l_it);
				return -1;
			}
			ast_frfree(f);
			ao2_ref(l, -1);
			ao2_iterator_destroy(&l_it);
			return 0;
		}
	}
	ao2_iterator_destroy(&l_it);
	rpt_mutex_unlock(&myrpt->lock);
	return 0;
}

static inline int monchannel_read(struct rpt *myrpt)
{
	struct ast_frame *f = ast_read(myrpt->monchannel);
	struct ao2_iterator l_it;

	if (!f) {
		ast_debug(1, "@@@@ rpt:Hung Up\n");
		return -1;
	}
	check_parrot(myrpt);

	if (f->frametype == AST_FRAME_VOICE) {
		struct rpt_link *l;

		if ((myrpt->p.duplex > 1 && myrpt->p.duplex != 3) || (myrpt->txkeyed && !myrpt->keyed)) {
			if (myrpt->monstream) {
				ast_writestream(myrpt->monstream, f);
			}
			if (myrpt->parrotstream) {
				ast_writestream(myrpt->parrotstream, f);
			}
		}
		if (((myrpt->p.duplex >= 2) || (!myrpt->keyed)) && myrpt->p.outstreamcmd && (myrpt->outstreampipe[1] != -1)) {
			outstream_write(myrpt, f);
		}
		/* go thru all the links */
		RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
			/* IF we are an altlink() -> !altlink() handled elsewhere */
			if (l->chan && altlink(myrpt, l) && (!l->lastrx) &&
				((l->link_newkey != RADIO_KEY_NOT_ALLOWED) || l->lasttx || !CHAN_TECH(l->chan, "IAX2"))) {
				ast_write(l->chan, f);
			}
		}
		ao2_iterator_destroy(&l_it);
	}
	return hangup_frame_helper(myrpt->monchannel, "monchannel", f);
}
static inline int rxpchannel_read(struct rpt *myrpt)
{
	struct ast_frame *f = ast_read(myrpt->rxpchannel);
	if (!f) {
		ast_debug(1, "@@@@ rpt:Hung Up\n");
		return -1;
	}
	return hangup_frame_helper(myrpt->rxpchannel, "rxpchannel", f);
}

static inline int txpchannel_read(struct rpt *myrpt)
{
	struct ast_frame *f = ast_read(myrpt->txpchannel);
	if (!f) {
		ast_debug(1, "@@@@ rpt:Hung Up\n");
		return -1;
	}
	return hangup_frame_helper(myrpt->txpchannel, "txpchannel", f);
	/* for now, read the channel, but when done, this should never "hear" anything */
	//	return wait_for_hangup_helper(myrpt->txpchannel, "txpchannel");
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

static int sendtext_cb(void *obj, void *arg, int flags)
{
	struct rpt_link *link = obj;
	const char *str = arg;

	if (link->chan) {
		ast_sendtext(link->chan, str);
	}
	return 0;
}

/* single thread with one file (request) to dial */
static void *rpt(void *this)
{
	struct rpt *myrpt = this;
	char *idtalkover, c, myfirst, *str;
	int len, lastduck = 0;
	int ms = MSWAIT, lasttx = 0, lastexttx = 0, lastpatchup = 0, val, identqueued, othertelemqueued;
	int tailmessagequeued, ctqueued, lastmyrx, localmsgqueued;
	struct rpt_link *l;
	struct rpt_tele *telem, *last_telem = NULL;
	char tmpstr[512];
	struct ast_format_cap *cap;
	struct timeval looptimestart;
	struct ao2_iterator l_it;

	if (myrpt->p.archivedir) {
		mkdir(myrpt->p.archivedir, 0700);
		snprintf(tmpstr, sizeof(tmpstr), "%s/%s", myrpt->p.archivedir, myrpt->name);
		mkdir(tmpstr, 0775);
	}
	myrpt->ready = 0;
	if (!myrpt->macrobuf) {
		myrpt->macrobuf = ast_str_create(MAXMACRO);
		if (!myrpt->macrobuf) {
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			return NULL;
		}
	}
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
		return NULL;
	}

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		rpt_mutex_unlock(&myrpt->lock);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		return NULL;
	}

	ast_format_cap_append(cap, ast_format_slin, 0);
	ast_debug(1, "Setting up channels");
	if (rpt_setup_channels(myrpt, cap)) {
		rpt_mutex_unlock(&myrpt->lock);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		disable_rpt(myrpt); /* Disable repeater */
		ao2_ref(cap, -1);
		return NULL;
	}

	ao2_ref(cap, -1);

	/* if serial io port, open it */
	myrpt->iofd = -1;
	if (myrpt->p.ioport && ((myrpt->iofd = openserial(myrpt, myrpt->p.ioport)) == -1)) {
		ast_log(LOG_ERROR, "Unable to open %s\n", myrpt->p.ioport);
		rpt_mutex_unlock(&myrpt->lock);
		rpt_hangup(myrpt, RPT_PCHAN);
		rpt_hangup_rx_tx(myrpt);
		return NULL;
	}
	myrpt->links = ao2_container_alloc_list(0, /* AO2 object flags. 0 means to use the default behavior */
		AO2_CONTAINER_ALLOC_OPT_INSERT_BEGIN,  /* AO2 container flags. New items should be added to the front of the list */
		NULL,								   /* Sorting function. NULL means the list will not be sorted */
		rpt_link_find_by_name);				   /* Comparison function */

	if (!myrpt->links) {
		rpt_mutex_unlock(&myrpt->lock);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		disable_rpt(myrpt); /* Disable repeater */
		pthread_exit(NULL);
	}

	/* Now, the idea here is to copy from the physical rx channel buffer
	   into the pseudo tx buffer, and from the pseudo rx buffer into the
	   tx channel buffer */
	myrpt->tailtimer = 0;
	myrpt->totimer = myrpt->p.totime;
	myrpt->tmsgtimer = myrpt->p.tailmessagetime;
	myrpt->idtimer = myrpt->p.politeid;
	myrpt->elketimer = myrpt->p.elke;
	myrpt->mustid = myrpt->tailid = 0;
	myrpt->callmode = CALLMODE_DOWN;
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
	myrpt->keyed_time_ms = 0;

	if (myrpt->p.rxburstfreq) {
#ifdef NATIVE_DSP
		if (!(myrpt->dsp = ast_dsp_new())) {
			rpt_hangup(myrpt, RPT_RXCHAN);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			return NULL;
		}
		/*! \todo At this point, we have a memory leak, because dsp needs to be freed. */
		/*! \todo Find out what the right place is to free dsp, i.e. when myrpt itself goes away. */
		ast_dsp_set_features(myrpt->dsp, DSP_FEATURE_FREQ_DETECT);
		ast_dsp_set_freqmode(myrpt->dsp, myrpt->p.rxburstfreq, myrpt->p.rxbursttime, myrpt->p.rxburstthreshold, 0);
#else
		tone_detect_init(&myrpt->burst_tone_state, myrpt->p.rxburstfreq, myrpt->p.rxbursttime, myrpt->p.rxburstthreshold);
#endif
	}
	if (myrpt->p.startupmacro) {
		ast_str_set(&myrpt->macrobuf, 0, "PPPP%s", myrpt->p.startupmacro);
	}
	/* @@@@@@@ UNLOCK @@@@@@@ */
	rpt_mutex_unlock(&myrpt->lock);

	val = 1;
	ast_channel_setoption(myrpt->rxchannel, AST_OPTION_RELAXDTMF, &val, sizeof(char), 0);
	ast_channel_setoption(myrpt->rxchannel, AST_OPTION_TONE_VERIFY, &val, sizeof(char), 0);

	donodelog(myrpt, "STARTUP");
	if (myrpt->remoterig && !ISRIG_RTX(myrpt->remoterig))
		setrem(myrpt);
	/* wait for telem to be done */
	while ((ms >= 0) && (myrpt->tele.next != &myrpt->tele)) {
		if (ast_safe_sleep(myrpt->rxchannel, 50) == -1) {
			ms = -1;
		}
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
	looptimestart = rpt_tvnow();
	ast_autoservice_stop(myrpt->rxchannel);
	while (ms >= 0) {
		struct ast_channel *who;
		struct ast_channel *cs[300], *cs1[300];
		int totx = 0, elap = 0, n, x;
		time_t t, t_mono;
		struct rpt_link *l;

		myrpt->lastthreadupdatetime = rpt_time_monotonic(); /* update the thread active timestamp. */
		if (myrpt->disgorgetime && (time(NULL) >= myrpt->disgorgetime)) {
			myrpt->disgorgetime = 0;
			dump_rpt(myrpt, lasttx, lastexttx, elap, totx); /* Debug Dump */
		}

		if (myrpt->reload) {
			flush_telem(myrpt);
			myrpt->reload = 0;
			usleep(10000);
			load_rpt_vars_by_rpt(myrpt, 1);
		}

		if (!myrpt->rxchannel) {
			ast_debug(1, "RPT rxchannel disappeared?\n"); /* This could happen if we call stop_repeaters() */
			break;
		}

		if (ast_shutting_down() || rpt_any_hangups(myrpt)) {
			break;
		}

		t_mono = rpt_time_monotonic();
		while (t_mono >= (myrpt->lastgpstime + GPS_UPDATE_SECS)) {
			unsigned long long u_mono;
			time_t was_mono;
			char gps_data[100];
			char lat[LAT_SZ + 1], lon[LON_SZ + 1], elev[ELEV_SZ + 1];

			myrpt->lastgpstime = t_mono;

			/* If the app_gps custom function GPS_READ exists, read the GPS position */
			if (!ast_custom_function_find("GPS_READ")) {
				break;
			}
			if (ast_func_read(NULL, "GPS_READ()", gps_data, sizeof(gps_data))) {
				break;
			}

			/* gps_data format monotonic time, epoch, latitude, longitude, elevation */
			if (sscanf(gps_data, N_FMT(llu) " %*u " S_FMT(LAT_SZ) S_FMT(LON_SZ) S_FMT(ELEV_SZ), &u_mono, lat, lon, elev) != 4) {
				break;
			}
			was_mono = (time_t) u_mono;
			if ((was_mono + GPS_VALID_SECS) < t_mono) {
				break;
			}
			snprintf(tmpstr, sizeof(tmpstr), "G %s %s %s %s", myrpt->name, lat, lon, elev);

			rpt_mutex_lock(&myrpt->lock);
			myrpt->voteremrx = 0; /* no voter remotes keyed */
			ao2_callback(myrpt->links, OBJ_MULTIPLE | OBJ_NODATA, sendtext_cb, &tmpstr);
			rpt_mutex_unlock(&myrpt->lock);
		}
		rpt_mutex_lock(&myrpt->lock);

		/* If someone's connected, and they're transmitting from their end to us, set remrx true */
		myrpt->remrx = 0;
		RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
			if (l->lastrx) {
				myrpt->remrx = 1;
				if (l->voterlink)
					myrpt->voteremrx = 1;
				if ((l->name[0] > '0') && (l->name[0] <= '9'))		/* Ignore '0' nodes */
					strcpy(myrpt->lastnodewhichkeyedusup, l->name); /* Note the node which is doing the key up */
			}
		}
		ao2_iterator_destroy(&l_it);
		if (myrpt->p.s[myrpt->p.sysstate_cur].sleepena) { /* If sleep mode enabled */
			if (myrpt->remrx) {							  /* signal coming from net wakes up system */
				myrpt->sleeptimer = myrpt->p.sleeptime;	  /* reset sleep timer */
				if (myrpt->sleep) {						  /* if asleep, then awake */
					myrpt->sleep = 0;
				}
			} else if (myrpt->keyed) {						/* if signal on input */
				if (!myrpt->sleep) {						/* if not sleeping */
					myrpt->sleeptimer = myrpt->p.sleeptime; /* reset sleep timer */
				}
			}

			if (myrpt->sleep)
				myrpt->localtx = 0; /* No RX if asleep */
			else
				myrpt->localtx = myrpt->keyed; /* Set localtx to keyed state if awake */
		} else {
			myrpt->localtx = myrpt->keyed; /* If sleep disabled, just copy keyed state to localrx */
		}
		/* Create a "must_id" flag for the cleanup ID */
		if (myrpt->p.idtime) /* ID time must be non-zero */
			myrpt->mustid |= (myrpt->idtimer) && (myrpt->keyed || myrpt->remrx);
		if (myrpt->keyed || myrpt->remrx) {
			/* Set the inactivity was keyed flag and reset its timer */
			myrpt->rptinactwaskeyedflag = 1;
			myrpt->rptinacttimer = 0;
		}
		/* Build a fresh totx from myrpt->keyed and autopatch activated */
		/* If full duplex, add local tx to totx */

		if ((myrpt->p.duplex > 1) && (!myrpt->patchvoxalways)) {
			totx = (myrpt->callmode != CALLMODE_DOWN);
		} else {
			int myrx = myrpt->localtx || myrpt->remrx || (myrpt->callmode == CALLMODE_DOWN);

			if (lastmyrx != myrx) {
				if (myrpt->p.duplex < 2)
					voxinit_rpt(myrpt, !myrx);
				lastmyrx = myrx;
			}
			totx = 0;
			if ((myrpt->callmode != CALLMODE_DOWN) && (myrpt->voxtotimer <= 0)) {
				voxtostate_to_voxtotimer(myrpt);
			}
			if (!myrpt->voxtostate)
				totx = (myrpt->callmode != CALLMODE_DOWN) && myrpt->wasvox;
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
				identqueued = 1; /* Identification telemetry */
			} else if (telem->mode == TAILMSG) {
				tailmessagequeued = 1; /* Tail message telemetry */
			} else if ((telem->mode == STATS_TIME_LOCAL) || (telem->mode == LOCALPLAY)) {
				localmsgqueued = 1; /* Local message */
			} else {
				if ((telem->mode != UNKEY) && (telem->mode != LINKUNKEY))
					othertelemqueued = 1; /* Other telemetry */
				else
					ctqueued = 1; /* Courtesy tone telemetry */
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
			totx = totx || /* (myrpt->dtmfidx > -1) || */
				   (myrpt->cmdnode[0] && strcmp(myrpt->cmdnode, "aprstt"));
		}
		/* add in parrot stuff */
		totx = totx || (myrpt->parrotstate == PARROT_STATE_PLAYING);
		if (!totx && myrpt->totimer) {
			/*
			 * This is the execution path taken when a user unkeys (!totx) and not
			 * yet timed out (myrpt->totimer > 0).  Here, we intentionally reset the
			 * time out timer.
			 *
			 * Note: This is called every time through the loop when not wanting to
			 * transmit and not in the timed out condition
			 */
			myrpt->totimer = myrpt->p.totime;
			myrpt->tounkeyed = 0;
			myrpt->tonotify = 0;
		} else {
			myrpt->tailtimer = myrpt->p.s[myrpt->p.sysstate_cur].alternatetail ? myrpt->p.althangtime : /* Initialize tail timer */
								   myrpt->p.hangtime;
		}
		if ((myrpt->p.duplex < 2) && myrpt->keyed && !myrpt->p.linktolink && !myrpt->p.dias) {
			/* if in 1/2 or 3/4 duplex, give rx priority */
			totx = 0;
		}
		if (!myrpt->totimer && myrpt->remrx) {
			/* reset the remote_time_out_reset_unkey_interval_timer.
			 * remote_time_out_reset_unkey_interval_timer is a filter for noisy/short remote unkey.
			 */
			myrpt->remote_time_out_reset_unkey_interval_timer = myrpt->p.time_out_reset_unkey_interval;
		}
		if (!myrpt->totimer && !myrpt->tounkeyed && !myrpt->keyed) {
			/* If unkey, set the tounkeyed flag and start the unkey timer.
			 * tounkeyed indicates a leading edge of unkeying and remains true
			 * once the time_out_reset_unkey_interval_timer has expired.
			 * If unkeyed before the timer expires, tounkeyed resets.
			 */
			myrpt->time_out_reset_unkey_interval_timer = myrpt->p.time_out_reset_unkey_interval;
			myrpt->tounkeyed = 1;
		}
		if (myrpt->tounkeyed && myrpt->keyed && myrpt->time_out_reset_unkey_interval_timer) {
			/* if keyed up and the unkey timer is not expired, reset the tounkeyed flag */
			myrpt->tounkeyed = 0;
		}
		if (!myrpt->totimer && myrpt->tounkeyed) {
			/* If the user rekeys at any time after a time out condition, the time out timer
			 * will be reset here.  unkey/key times depend on time_out_reset_unkey_interval and
			 * time_out_reset_kerchunk_interval configuration parameters.  If time_out_reset_unkey_interval is
			 * not configured, the time out timer will be reset immediately on a local rekey.
			 *
			 *  NB: The time between the time out condition and when the user rekeys could be a very, very long time!
			 */
			int do_tot_reset = 0;
			if (myrpt->p.time_out_reset_unkey_interval) {
				if (myrpt->remrx) {
					/*
					 * If time_out_reset_unkey_interval is configured
					 * Test for remote link traffic during time out condition. A local rekey which lasts for
					 *  time_out_reset_kerchunk_interval after the time_out_reset_unkey_interval_timer is satisfied will
					 *  reset the timeout timer. Additionally, if both time_out_reset_unkey_interval_timer and
					 * remote_time_out_reset_unkey_interval_timer are expired (aka no keyups from any source),
					 * the timeout timer will be reset.
					 *
					 * Note: remrx-override doesn't have any effect when myrpt->p.time_out_reset_unkey_interval is set to zero.
					 *  In that case the traditional time out timer reset behaviour will apply,
					 *  and the timeout timer will get reset with no delay when the user unkeys and rekeys in a time out condition
					 *  with a active signal from any link.
					 */
					if (myrpt->p.time_out_reset_kerchunk_interval && !myrpt->time_out_reset_unkey_interval_timer) {
						if (myrpt->keyed_time_ms >= myrpt->p.time_out_reset_kerchunk_interval) {
							do_tot_reset = 1;
						}
					}
				} else if (!myrpt->time_out_reset_unkey_interval_timer && !myrpt->remote_time_out_reset_unkey_interval_timer) {
					do_tot_reset = 1;
				}
			} else if (!totx || (myrpt->tounkeyed && myrpt->keyed)) {
				/* time_out_reset_unkey_interval is not configured (backward compatibility) */
				do_tot_reset = 1;
			}
			if (do_tot_reset) {
				myrpt->totimer = myrpt->p.totime;
				myrpt->tounkeyed = 0;
				myrpt->tonotify = 0;
				/* Note that in this case, the rest of the loop body doesn't get executed,
				 * and instead we start again at the top of the loop
				 */
				rpt_mutex_unlock(&myrpt->lock);
				continue;
			}
		}
		/* Disable the local transmitter if we are timed out
		 *  ***** From this point forward, totx will be FALSE
		 *  if in the timed out condition
		 */
		totx = totx && myrpt->totimer;
		if (!myrpt->totimer && !myrpt->tonotify) {
			/* if timed-out and not said already, say it */
			myrpt->tonotify = 1;
			myrpt->timeouts++;
			rpt_mutex_unlock(&myrpt->lock);
			/* Flush pending telemetry messages */
			flush_telem(myrpt);
			/* Insert time out message which will have priority */
			/* and keep the TX up during the time it is being sent out */
			rpt_telemetry(myrpt, TIMEOUT, NULL);
			rpt_mutex_lock(&myrpt->lock);
		}
		if (!totx && !myrpt->totimer && (myrpt->callmode == CALLMODE_FAILED)) {
			/* If timed-out and in circuit busy after call, teardown the call */
			ast_debug(1, "timed-out and in circuit busy after call\n");
			myrpt->callmode = CALLMODE_DOWN;
			myrpt->macropatch = 0;
			channel_revert(myrpt);
		}
		if (!myrpt->totimer || (!myrpt->mustid && myrpt->p.beaconing)) {
			/* get rid of tail if timed out or beaconing */
			myrpt->tailtimer = 0;
		}
		if (myrpt->totimer) {
			/* if not timed-out, add in tail */
			totx = totx || myrpt->tailtimer;
		}
		if ((myrpt->keyed || myrpt->remrx || myrpt->localoverride) && ((identqueued && idtalkover) || (tailmessagequeued))) {
			/* If user or links key up or are keyed up over standard ID, switch to talkover ID, if one is defined */
			/* If tail message, kill the message if someone keys up over it */
			int hasid = 0, hastalkover = 0;

			telem = myrpt->tele.next;
			while (telem != &myrpt->tele) {
				if (telem->mode == ID && !telem->killed) {
					telem->killed = 1;
					hasid = 1;
					if (telem->chan) {
						ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV); /* Whoosh! */
					}
				}
				if (telem->mode == TAILMSG && !telem->killed) {
					telem->killed = 1;
					if (telem->chan) {
						ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV); /* Whoosh! */
					}
				}
				if (telem->mode == IDTALKOVER) {
					hastalkover = 1;
				}
				telem = telem->next;
			}
			if (hasid && !hastalkover) {
				ast_debug(6, "Tracepoint IDTALKOVER\n");
				rpt_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt, IDTALKOVER, NULL); /* Start Talkover ID */
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

		if ((myrpt->p.idtime && totx && (!myrpt->exttx) && (myrpt->idtimer <= myrpt->p.politeid) && myrpt->tailtimer)) { /* ID time must be non-zero */
			myrpt->tailid = 1;
		}

		/* If tail timer expires, then check for tail messages */

		if (myrpt->tailevent) {
			myrpt->tailevent = 0;
			if (myrpt->tailid) {
				totx = 1;
				queue_id(myrpt);
			} else if (myrpt->p.tailmessagetime && myrpt->tmsgtimer == 0) {
				totx = 1;
				myrpt->tmsgtimer = myrpt->p.tailmessagetime;
				rpt_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt, TAILMSG, NULL);
				rpt_mutex_lock(&myrpt->lock);
			}
		}

		/* Main TX control */
		/* Handling  of telemetry during a time out condition */
		if (myrpt->p.duplex > 0) {
			/* If timed out, we only want to keep the TX keyed if there
			 * is a message queued which is configured to override
			 * the time out condition
			 */
			if (!myrpt->totimer || myrpt->tounkeyed) {
				totx = totx || priority_telemetry_pending(myrpt);
			} else {
				totx = totx || (myrpt->tele.next != &myrpt->tele);
			}
		}
		totx = totx && !myrpt->p.s[myrpt->p.sysstate_cur].txdisable;
		myrpt->txrealkeyed = totx;
		/* Control op tx disable overrides everything prior to this. */
		/* Hold up the TX as long as there are frames in the tx queue */
		totx = totx || (!AST_LIST_EMPTY(&myrpt->txq));
		/* if in 1/2 or 3/4 duplex, give rx priority */
		if ((myrpt->p.duplex < 2) && (!myrpt->p.linktolink) && (!myrpt->p.dias) && (myrpt->keyed)) {
			totx = 0;
		}
		/* Disable TX if Elke timer is enabled and it expires. */
		if (myrpt->p.elke && (myrpt->elketimer > myrpt->p.elke)) {
			totx = 0;
		}
		/* Detect and log unkeyed to keyed transition point */
		if (totx && !lasttx) {
			log_keyed(myrpt);
			lasttx = 1;
		}
		/* Detect and log keyed to unkeyed transition point */
		if (!totx && lasttx) {
			lasttx = 0;
			log_unkeyed(myrpt);
		}
		time(&t);
		/* if DTMF timeout */
		if (((!myrpt->cmdnode[0]) || (!strcmp(myrpt->cmdnode, "aprstt"))) && (myrpt->dtmfidx >= 0) &&
			((myrpt->dtmf_time + DTMF_TIMEOUT) < t)) {
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

		if (myrpt->exttx && ((myrpt->p.parrotmode != PARROT_MODE_OFF) || myrpt->parrotonce) && (myrpt->parrotstate == PARROT_STATE_IDLE)) {
			char myfname[300];
			/* setup audiohook to spy on the pchannel. */
			ast_verb(4, "Parrot attached to %s\n", ast_channel_name(myrpt->pchannel));
			snprintf(myfname, sizeof(myfname), PARROTFILE ".wav", myrpt->name, myrpt->parrotcnt);
			unlink(myfname);
			myrpt->parrotstate = PARROT_STATE_RECORDING;
			myrpt->parrottimer = myrpt->p.parrottime;
			if (myrpt->parrotstream)
				ast_closestream(myrpt->parrotstream);
			myrpt->parrotstream = NULL;
			snprintf(myfname, sizeof(myfname), PARROTFILE, myrpt->name, myrpt->parrotcnt);
			myrpt->parrotstream = ast_writefile(myfname, "wav", "app_rpt Parrot", O_CREAT | O_TRUNC, 0, 0600);
		}

		if (myrpt->exttx != lastexttx) {
			lastexttx = myrpt->exttx;
			rpt_update_boolean(myrpt, "RPT_ETXKEYED", lastexttx);
		}

		if ((myrpt->callmode != CALLMODE_DOWN) != lastpatchup) {
			lastpatchup = (myrpt->callmode != CALLMODE_DOWN);
			rpt_update_boolean(myrpt, "RPT_AUTOPATCHUP", lastpatchup);
		}

		/* Reconnect */

		RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
			if (l->killme) {
				ao2_ref(l, +1);					  /* prevent freeing while we finish up */
				rpt_link_remove(myrpt->links, l); /* remove from queue */
				if (!strcmp(myrpt->cmdnode, l->name))
					myrpt->cmdnode[0] = 0;
				rpt_mutex_unlock(&myrpt->lock);
				/* hang-up on call to device */
				if (l->chan)
					ast_hangup(l->chan);
				ast_hangup(l->pchan);
				rpt_mutex_lock(&myrpt->lock);
				/* re-start link traversal */
				ao2_ref(l, -1); /* and drop the extra ref we're holding */
				continue;
			}
		}
		ao2_iterator_destroy(&l_it);
		x = myrpt->remrx || myrpt->localtx || (myrpt->callmode != CALLMODE_DOWN) || myrpt->parrotstate;
		if (x != myrpt->lastitx) {
			char str[16];

			myrpt->lastitx = x;
			if (myrpt->p.itxctcss) {
				if (IS_DAHDI_CHAN(myrpt->rxchannel)) {
					dahdi_radio_set_ctcss_encode(myrpt->localrxchannel, !x);
				} else if (CHAN_TECH(myrpt->rxchannel, "radio") || CHAN_TECH(myrpt->rxchannel, "simpleusb")) {
					snprintf(str, sizeof(str), "TXCTCSS %d", !(!x));
					ast_sendtext(myrpt->rxchannel, str);
				}
			}
		}
		/* If we have a new active telemetry message and a channel */
		if (!myrpt->noduck && myrpt->active_telem && myrpt->active_telem->chan) {
			/* If we have a new telmetry message or we have changed keyup state to keyup */
			if (((myrpt->active_telem != last_telem) || !lastduck) && (myrpt->keyed || myrpt->remrx)) {
				if (ast_audiohook_volume_set_float(myrpt->active_telem->chan, AST_AUDIOHOOK_DIRECTION_WRITE, myrpt->p.telemduckgain)) {
					ast_log(LOG_WARNING, "Setting the volume on channel %s to %2.2f failed",
						ast_channel_name(myrpt->active_telem->chan), myrpt->p.telemduckgain);
				}
				lastduck = 1;
			}
			/* If we have a new telemetry message or we have already adjusted ducking and we are not keyed up */
			if (((myrpt->active_telem != last_telem) || lastduck) && !myrpt->keyed && !myrpt->remrx) {
				if (ast_audiohook_volume_set_float(myrpt->active_telem->chan, AST_AUDIOHOOK_DIRECTION_WRITE, myrpt->p.telemnomgain)) {
					ast_log(LOG_WARNING, "Setting the volume on channel %s to %2.2f failed",
						ast_channel_name(myrpt->active_telem->chan), myrpt->p.telemnomgain);
				}
				lastduck = 0;
			}
		}
		last_telem = myrpt->active_telem;

		n = 0;
		cs[n++] = myrpt->rxchannel;
		cs[n++] = myrpt->pchannel;
		cs[n++] = myrpt->txpchannel;
		cs[n++] = myrpt->rxpchannel;
		if (myrpt->monchannel)
			cs[n++] = myrpt->monchannel;
		if (myrpt->txchannel != myrpt->rxchannel)
			cs[n++] = myrpt->txchannel;
		if (myrpt->localtxchannel != myrpt->txchannel)
			cs[n++] = myrpt->localtxchannel;
		RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
			if (!l->killme) {
				if (l->chan) {
					cs[n++] = l->chan;
				}
				cs[n++] = l->pchan;
			}
		}
		ao2_iterator_destroy(&l_it);
		if ((myrpt->topkeystate == 1) && ((t - myrpt->topkeytime) > TOPKEYWAIT)) {
			myrpt->topkeystate = 2;
			qsort(myrpt->topkey, TOPKEYN, sizeof(struct rpt_topkey), topcompar);
		}
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
		who = ast_waitfor_n(cs1, n, &ms);
		if (who == NULL) {
			ms = 0;
		}
		elap = rpt_time_elapsed(&looptimestart); /* calculate loop time */
		rpt_mutex_lock(&myrpt->lock);
		periodic_process_links(myrpt, elap);
		if (update_timers(myrpt, elap, totx)) {
			rpt_mutex_unlock(&myrpt->lock);
			break;
		}
		if (!ms) {
			/* No channels had activity before the timer expired,
			 * so just continue to the next loop. */
			rpt_mutex_unlock(&myrpt->lock);
			continue;
		}
		if (((myrpt->p.parrotmode != PARROT_MODE_OFF) || myrpt->parrotonce) && myrpt->parrotstate == PARROT_STATE_RECORDING &&
			myrpt->parrottimer <= 0) {
			if (update_parrot(myrpt)) {
				rpt_mutex_unlock(&myrpt->lock);
				break;
			}
		}
		if (myrpt->cmdAction.state == CMD_STATE_READY) { /* there is a command waiting to be processed */
			process_command(myrpt);
		}

		str = ast_str_buffer(myrpt->macrobuf);
		len = ast_str_strlen(myrpt->macrobuf);
		c = str[0];
		time(&t);
		if (myrpt->patch_talking != myrpt->wasvox) {
			/* Autopatch vox has changed states. */
			ast_debug(1, "Node %s, vox %d\n", myrpt->name, myrpt->patch_talking);
			myrpt->wasvox = myrpt->patch_talking;
			myrpt->voxtostate = 0;
			if (myrpt->patch_talking) {
				myrpt->voxtotimer = myrpt->p.voxtimeout_ms;
			} else {
				myrpt->voxtotimer = 0;
			}
		}
		if (c && !myrpt->macrotimer && starttime && t > starttime) {
			char cin = c & 0x7f;
			myrpt->macrotimer = MACROTIME;
			ast_copy_string(str, str + 1, len);
			ast_str_truncate(myrpt->macrobuf, len - 1);
			if ((cin == 'p') || (cin == 'P')) {
				myrpt->macrotimer = MACROPTIME;
			}
			rpt_mutex_unlock(&myrpt->lock);
			donodelog_fmt(myrpt, "DTMF(M),MAIN,%c", cin);
			local_dtmf_helper(myrpt, c);
		} else {
			rpt_mutex_unlock(&myrpt->lock);
		}
		/* @@@@@@ UNLOCK @@@@@ */

		if (who == myrpt->rxchannel) { /* if it was a read from rx */
			if (rxchannel_read(myrpt, lasttx)) {
				break;
			}
			continue;
		} else if (who == myrpt->pchannel) { /* if it was a read from pseudo */
			if (pchannel_read(myrpt)) {
				break;
			}
			continue;
		} else if (who == myrpt->rxpchannel) {
			if (rxpchannel_read(myrpt)) {
				break;
			}
		} else if (who == myrpt->txchannel) { /* if it was a read from tx - Note if rxchannel = txchannel, we won't get here */
			if (txchannel_read(myrpt)) {
				break;
			}
			continue;
		} else if (who == myrpt->localtxchannel) { /* if it was a read from local-tx */
			if (localtxchannel_read(myrpt, &myfirst)) {
				break;
			}
			continue;
		} else if (who == myrpt->txpchannel) { /* if it was a read from remote tx */
			if (txpchannel_read(myrpt)) {
				break;
			}
		} else if (who == myrpt->monchannel) {
			if (monchannel_read(myrpt)) {
				break;
			}
		} else if (process_link_channels(myrpt, who, &myfirst)) {
			break;
		}
	}

	/* Terminate and cleanup app_rpt node instance */
	ast_debug(1, "%s disconnected, cleaning up...\n", myrpt->name);

	myrpt->ready = 0;
	usleep(100000);
	while (myrpt->tele.next != &myrpt->tele) {
		/* wait for telem to be done */
		usleep(50000);
	}
	rpt_hangup(myrpt, RPT_PCHAN);
	if (myrpt->monchannel) {
		rpt_hangup(myrpt, RPT_MONCHAN);
	}
	myrpt->parrotstate = PARROT_STATE_IDLE;
	rpt_hangup(myrpt, RPT_TXPCHAN);
	rpt_hangup(myrpt, RPT_RXPCHAN);
	if (myrpt->localtxchannel != myrpt->txchannel) {
		rpt_hangup(myrpt, RPT_LOCALTXCHAN);
	}
	rpt_hangup_rx_tx(myrpt);
	rpt_frame_queue_free(&myrpt->frame_queue);

	rpt_mutex_lock(&myrpt->lock);
	RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
		ao2_ref(l, +1);					  /* prevent freeing while we finish up */
		rpt_link_remove(myrpt->links, l); /* remove from queue */
		/* hang-up on call to device */
		if (l->chan)
			ast_hangup(l->chan);
		ast_hangup(l->pchan);
		if (l->connect_threadid) {
			/* Wait for any connections to finish */
			rpt_mutex_unlock(&myrpt->lock);
			pthread_join(l->connect_threadid, NULL);
			rpt_mutex_lock(&myrpt->lock);
		}
		ao2_ref(l, -1); /* and drop the extra ref we're holding */
	}
	ao2_iterator_destroy(&l_it);
	if (myrpt->xlink == 1)
		myrpt->xlink = 2;
	rpt_mutex_unlock(&myrpt->lock);
	ao2_cleanup(myrpt->links);
	ast_debug(1, "@@@@ rpt:Hung up channel\n");
	myrpt->rpt_thread = AST_PTHREADT_STOP;
	stop_outstream(myrpt);
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
	const char *cval;

	cfg = ast_config_load("rpt.conf", config_flags);
	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to open radio repeater configuration rpt.conf.  Radio Repeater disabled.\n");
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Errors detected in the radio repeater configuration rpt.conf.  Radio Repeater disabled.\n");
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
			ast_log(LOG_WARNING, "Configuration error: node_lookup_method, %s, is not valid", val);
			rpt_node_lookup_method = DEFAULT_NODE_LOOKUP_METHOD;
		}
	}

	cval = ast_variable_retrieve(cfg, "general", "dns_node_domain");
	if (cval) {
		if (rpt_is_valid_dns_name(val)) {
			rpt_dns_node_domain = val;
		} else {
			ast_log(LOG_ERROR, "Configuration error: dns_node_domain value %s is not a valid format", cval);
			rpt_dns_node_domain = DEFAULT_DNS_NODE_DOMAIN;
		}
	} else {
		rpt_dns_node_domain = DEFAULT_DNS_NODE_DOMAIN;
	}
	ast_log(LOG_NOTICE, "Domain used for DNS node lookup is: %s", rpt_dns_node_domain);
	val = (char *) ast_variable_retrieve(cfg, "general", "max_dns_node_length");
	if (val) {
		i = atoi(val);
		if (i < 4) {
			i = 4;
		}
		if (i > 63) {
			i = 63;
		}
		rpt_max_dns_node_length = i;
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
		if (rpt_vars[n].macrobuf) {
			ast_free(rpt_vars[n].macrobuf);
			rpt_vars[n].macrobuf = NULL;
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
		rpt_vars[n].tele.next = &rpt_vars[n].tele;
		rpt_vars[n].tele.prev = &rpt_vars[n].tele;
		rpt_vars[n].rpt_thread = AST_PTHREADT_NULL;
		rpt_vars[n].tailmessagen = 0;
#ifdef _MDC_DECODE_H_
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
	bool thread_hung[MAXRPTS] = { false };
	time_t last_thread_time[MAXRPTS] = { 0 };
	time_t current_time = rpt_time_monotonic();
	/* init nodelog queue */
	nodelog.next = nodelog.prev = &nodelog;
	/* go thru all the specified repeaters */

	/* wait until asterisk starts */
	while (!ast_fully_booted) {
		usleep(250000);
	}

	if (load_config(0)) {
		return NULL;
	}

	/* start a rpt() thread for each repeater that is not a remote */
	for (i = 0; i < nrpts; i++) {
		load_rpt_vars(i, 1); /* Load initial config */

		/* if is a remote, dont start a rpt() thread for it */
		if (rpt_vars[i].remote) {
			rpt_vars[i].ready = 1;
			if (retrieve_memory(&rpt_vars[i], "init")) { /* Try to retrieve initial memory channel */
				if ((!strcmp(rpt_vars[i].remoterig, REMOTE_RIG_RTX450)) || (!strcmp(rpt_vars[i].remoterig, REMOTE_RIG_XCAT))) {
					ast_copy_string(rpt_vars[i].freq, "446.500", sizeof(rpt_vars[i].freq));
				} else {
					ast_copy_string(rpt_vars[i].freq, "145.000", sizeof(rpt_vars[i].freq));
				}
			}
			continue;
		} else { /* is a normal repeater */
			rpt_vars[i].p.memory = rpt_vars[i].name;
			if (retrieve_memory(&rpt_vars[i], "radiofreq")) { /* Try to retrieve initial memory channel */
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
			return NULL;
		}
		rpt_vars[i].ready = 0;
		rpt_vars[i].lastthreadupdatetime = current_time;
		ast_pthread_create_detached(&rpt_vars[i].rpt_thread, NULL, rpt, &rpt_vars[i]);
	}
	time(&starttime);
	ast_mutex_lock(&rpt_master_lock);
	for (;;) {
		/* Now monitor each thread, and restart it if necessary */
		time_t current_loop_time;
		current_time = rpt_time_monotonic();
		for (i = 0; i < nrpts; i++) {
			int rv;
			if (rpt_vars[i].remote)
				continue;

			current_loop_time = current_time - rpt_vars[i].lastthreadupdatetime;
			if (rpt_vars[i].lastthreadupdatetime != last_thread_time[i]) {
				/*! \todo Implement thread kill/recovery mechanism */
				if (thread_hung[i]) { /* We were hung and a new update time */
					thread_hung[i] = false;
					ast_log(LOG_WARNING, "RPT thread on %s has recovered after %ld seconds.\n", rpt_vars[i].name,
						current_time - last_thread_time[i]);
				}
				last_thread_time[i] = rpt_vars[i].lastthreadupdatetime; /* Only log message one time */
			}
			if (current_loop_time > RPT_THREAD_TIMEOUT && !thread_hung[i]) {
				thread_hung[i] = true;
				ast_log(LOG_WARNING, "RPT thread on %s is hung for %ld seconds.\n", rpt_vars[i].name, current_loop_time);
			}
			if ((rpt_vars[i].rpt_thread == AST_PTHREADT_STOP) || (rpt_vars[i].rpt_thread == AST_PTHREADT_NULL)) {
				rv = -1;
			} else {
				rv = pthread_kill(rpt_vars[i].rpt_thread, 0); /* Check thread status by sending signal 0 */
			}
			if (rv) {
				if (rpt_vars[i].deleted) {
					rpt_vars[i].name[0] = 0;
					continue;
				}
				if (ast_shutting_down() || shutting_down) {
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
				rpt_vars[i].lastthreadupdatetime = current_time;
				ast_pthread_create_detached(&rpt_vars[i].rpt_thread, NULL, rpt, &rpt_vars[i]);
				/* if (!rpt_vars[i].xlink) */
				ast_log(LOG_WARNING, "rpt_thread restarted on node %s\n", rpt_vars[i].name);
			}
		}
		for (i = 0; i < nrpts; i++) {
			if (rpt_vars[i].deleted || rpt_vars[i].remote || !rpt_vars[i].p.outstreamcmd) {
				continue;
			}
			if (!rpt_vars[i].outstreampid) {
				ast_debug(3, "No outstreampid exists yet\n");
			} else if (kill(rpt_vars[i].outstreampid, 0) == -1) {
				ast_debug(3, "PID %d not currently running\n", (int) rpt_vars[i].outstreampid);
				/* The outstreamcmd has exited (probably because it failed).
				 * Clean up the child before moving on, and don't reattempt if we fail twice. */
				time(&rpt_vars[i].outstreamlasterror); /* Keep track that it just exited */
				rpt_vars[i].outstreampid = 0;		   /* In case we continue, reset */
			} else {
				continue;
			}
			if (rpt_vars[i].outstreamlasterror && time(NULL) < rpt_vars[i].outstreamlasterror + 1) {
				/* Command exited immediately. It probably doesn't work, no point in continuously
				 * restarting it in a loop. */
				ast_log(LOG_ERROR, "outstreamcmd '%s' appears to be broken, disabling\n", rpt_vars[i].p.outstreamcmd);
				rpt_vars[i].p.outstreamcmd = NULL;
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
			if (nodep == &nodelog) { /* if nothing in queue */
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
			snprintf(fname, sizeof(fname), "%s/%s/%s.txt", nodep->archivedir, nodep->str, datestr);
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
	return NULL;
}

static inline int exec_chan_read(struct rpt *myrpt, struct ast_channel *chan, char *restrict keyed,
	const enum rpt_phone_mode phone_mode, const int phone_vox, char *restrict myfirst, int *restrict dtmfed)
{
	struct ast_frame *f = ast_read(chan);
	if (!f) {
		ast_debug(1, "@@@@ link:Hung Up\n");
		return -1;
	}
	if (f->frametype == AST_FRAME_VOICE) {
		struct ast_frame *f1;
		int ismuted;
		if (myrpt->rpt_newkey == RADIO_KEY_NOT_ALLOWED) {
			myrpt->rxlingertimer = RX_LINGER_TIME;
			if (!*keyed) {
				*keyed = 1;
				myrpt->rerxtimer = 0;
			}
		}
		if ((phone_mode != RPT_PHONE_MODE_NONE) && phone_vox) {
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
						if (!f1) {
							ast_frfree(f);
							return -1;
						}
						RPT_MUTE_FRAME(f1);
						memset(&f1->frame_list, 0, sizeof(f1->frame_list));
						AST_LIST_INSERT_TAIL(&myrpt->rxq, f1, frame_list);
					}
					*myfirst = 1;
				}
				f1 = ast_frdup(f);
				if (!f1) {
					ast_frfree(f);
					return -1;
				}
				memset(&f1->frame_list, 0, sizeof(f1->frame_list));
				AST_LIST_INSERT_TAIL(&myrpt->rxq, f1, frame_list);
			} else
				*myfirst = 0;
			x = 0;
			AST_LIST_TRAVERSE(&myrpt->rxq, f1, frame_list) x++;
			if (!x) {
				RPT_MUTE_FRAME(f);
			} else {
				ast_frfree(f);
				f = AST_LIST_REMOVE_HEAD(&myrpt->rxq, frame_list);
			}
		}
		ismuted = rpt_conf_get_muted(chan, myrpt);
		/* if not transmitting, zero-out audio */
		ismuted |= (!myrpt->remotetx);
		if (*dtmfed && (phone_mode != RPT_PHONE_MODE_NONE)) {
			ismuted = 1;
		}
		*dtmfed = 0;
		f1 = rpt_frame_queue_helper(&myrpt->frame_queue, f, ismuted);
		if (!myrpt->remstopgen) {
			if (phone_mode == RPT_PHONE_MODE_NONE) {
				ast_write(myrpt->txchannel, f); /* write frame w/no delay */
			} else if (f1) {
				ast_write(myrpt->txchannel, f1); /* write delayed frame */
			}
		}
		ast_frfree(f1);
	} else if (f->frametype == AST_FRAME_DTMF_BEGIN) {
		rpt_frame_queue_mute(&myrpt->frame_queue);
		*dtmfed = 1;
	}
	if (f->frametype == AST_FRAME_DTMF) {
		rpt_frame_queue_mute(&myrpt->frame_queue);
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
		if ((f->subclass.integer == AST_CONTROL_RADIO_KEY) && (myrpt->rpt_newkey != RADIO_KEY_NOT_ALLOWED)) {
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
			RPT_MUTE_FRAME(f);
		}
		if (myrpt->totimer) { /* Don't send local RX voice frames if the local repeater is timed out */
			ast_write(myrpt->pchannel, f);
		}
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
		if ((myrpt->rpt_newkey != RADIO_KEY_NOT_ALLOWED) || myrpt->remoterx || !CHAN_TECH(chan, "IAX2")) {
			ast_write(chan, f);
		}
	}
	return hangup_frame_helper(myrpt->pchannel, "pchannel", f);
}

static inline int exec_txchannel_read(struct rpt *myrpt)
{
	return wait_for_hangup_helper(myrpt->txchannel, "txchannel");
}

static int parse_caller(const char *b1, const char *hisip, char *s)
{
	char sx[320];
	char *s1, *s2, *s3;

	s2 = parse_node_format(s, &s1, sx, sizeof(sx));
	if (!s2) {
		ast_log(LOG_WARNING, "Reported node %s not in correct format\n", b1);
		return -1;
	}
	if (strcmp(s2, "NONE")) {
		char nodeip[100];
		struct ast_sockaddr addr = { {
			0,
		} };
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
	if (rpt_radio_set_param(myrpt->localtxchannel, myrpt, RPT_RADPAR_UIOMODE, 3)) {
		ast_log(LOG_ERROR, "Cannot set UIOMODE on %s: %s\n", ast_channel_name(myrpt->localtxchannel), strerror(errno));
		return -1;
	}
	if (rpt_radio_set_param(myrpt->localtxchannel, myrpt, RPT_RADPAR_UIODATA, 3)) {
		ast_log(LOG_ERROR, "Cannot set UIODATA on %s: %s\n", ast_channel_name(myrpt->localtxchannel), strerror(errno));
		return -1;
	}
	if (dahdi_set_offhook(myrpt->localtxchannel)) {
		return -1;
	}
	return 0;
}

static void answer_newkey_helper(struct rpt *myrpt, struct ast_channel *chan, enum rpt_phone_mode phone_mode)
{
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_answer(chan);
		if (phone_mode == RPT_PHONE_MODE_NONE) {
			send_newkey(chan);
		}
	}
}

static int rpt_exec(struct ast_channel *chan, const char *data)
{
	int res = -1, i, rem_totx, rem_rx, remkeyed, n;
	enum rpt_phone_mode phone_mode = RPT_PHONE_MODE_NONE;
	int iskenwood_pci4, authtold, authreq, setting, notremming, reming;
	int dtmfed, phone_vox = 0, phone_monitor = 0;
	char *use_pipe;
	char tmp[TMP_SIZE], keyed = 0, keyed1 = 0;
	char *options, *stringp, *callstr, c, *altp, *memp, *str;
	char sx[320], myfirst, *b, *b1, *separator = "|";
	struct rpt *myrpt;
	struct ast_channel *cs[20];
	struct rpt_link *l;
	int ms, elap, myrx, len;
	time_t last_timeout_warning;
	struct rpt_tele *telem;
	int numlinks;
	struct ast_format_cap *cap;
	struct timeval looptimestart;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Rpt requires an argument (system node)\n");
		return -1;
	}

	ast_copy_string(tmp, data, sizeof(tmp));

	ast_set_read_format(chan, ast_format_slin);
	ast_set_write_format(chan, ast_format_slin);

	/*! \todo issue #133 - use "," - first step allow "|"" or ","" but not both
	 * eventually remove | as an option after most installations have moved
	 */
	use_pipe = strchr(tmp, '|');
	if (use_pipe) {
		separator = "|";
		ast_log(LOG_WARNING, "Pipe (|) delimiter is deprecated and will be removed in a future release, please use comma (,).\n");
	} else {
		separator = ",";
	}
	altp = use_pipe ? strstr(tmp, "|*") : strstr(tmp, ",*");
	if (altp) {
		altp[0] = 0;
		altp++;
	}

	memp = use_pipe ? strstr(tmp, "|M") : strstr(tmp, ",M");
	if (memp) {
		memp[0] = 0;
		memp += 2;
	}

	stringp = tmp;
	strsep(&stringp, separator);
	options = stringp;
	strsep(&stringp, separator);
	callstr = stringp;

	myrpt = NULL;
	/* see if we can find our specified one */
	for (i = 0; i < nrpts; i++) {
		/* if name matches, assign it and exit loop */
		if (!strcmp(tmp, rpt_vars[i].name)) {
			myrpt = &rpt_vars[i];
			if (!myrpt->ready) {
				ast_log(LOG_WARNING, "Node %s is not ready yet, rejecting call on %s\n", rpt_vars[i].name, ast_channel_name(chan));
				return -1;
			}
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
		if (cfg == CONFIG_STATUS_FILEINVALID) {
			cfg = NULL;
		}
		if (cfg && ((!options) || (*options == 'X') || (*options == 'F'))) {
			myadr = (char *) ast_variable_retrieve(cfg, "proxy", "ipaddr");
			if (options && (*options == 'F')) {
				if (b1 && myadr) {
					forward_node_lookup(b1, cfg, nodedata, sizeof(nodedata));
					ast_copy_string(xstr, nodedata, sizeof(xstr));
					s2 = parse_node_format(xstr, &s1, sx, sizeof(sx));
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
			s2 = parse_node_format(xstr, &s1, sx, sizeof(sx));
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
			if (!strcmp(myadr, s2)) { /* if we have it.. */
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

		pbx_builtin_setvar_helper(chan, "RPT_STAT_RXKEYED", myrpt->keyed ? "1" : "0");
		pbx_builtin_setvar_helper(chan, "RPT_STAT_TXKEYED", myrpt->txkeyed ? "1" : "0");

#define rpt_set_numeric_var_helper(chan, varname, varvalue) \
	snprintf(buf2, sizeof(buf2), "%d", varvalue); \
	pbx_builtin_setvar_helper(chan, varname, buf2);

		rpt_set_numeric_var_helper(chan, "RPT_STAT_XLINK", myrpt->xlink);
		rpt_set_numeric_var_helper(chan, "RPT_STAT_LINKS", numlinks);
		rpt_set_numeric_var_helper(chan, "RPT_STAT_WASCHAN", myrpt->waschan);
		rpt_set_numeric_var_helper(chan, "RPT_STAT_NOWCHAN", myrpt->nowchan);
		rpt_set_numeric_var_helper(chan, "RPT_STAT_DUPLEX", myrpt->p.duplex);
		rpt_set_numeric_var_helper(chan, "RPT_STAT_PARROT", myrpt->p.parrotmode);
#if 0
		rpt_set_numeric_var_helper(chan, "RPT_STAT_PHONEVOX", myrpt->phonevox);
		rpt_set_numeric_var_helper(chan, "RPT_STAT_CONNECTED", myrpt->connected);
#endif
		rpt_set_numeric_var_helper(chan, "RPT_STAT_CALLMODE", myrpt->callmode);
		pbx_builtin_setvar_helper(chan, "RPT_STAT_LASTTONE", myrpt->lasttone);
#undef rpt_set_numeric_var_helper

		res = priority_jump(myrpt, chan);
		return res;
	}

	if (options && (*options == 'V' || *options == 'v')) {
		if (callstr && myrpt->rxchannel) {
			pbx_builtin_setvar(myrpt->rxchannel, callstr);
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
		phone_mode = RPT_PHONE_MODE_PHONE_CONTROL;
		if (*options == 'D') {
			phone_mode = RPT_PHONE_MODE_DUMB_DUPLEX;
		}
		if (*options == 'S') {
			phone_mode = RPT_PHONE_MODE_DUMB_SIMPLEX;
		}
		ast_set_callerid(chan, "0", "app_rpt user", "0");
		val = 1;
		ast_channel_setoption(chan, AST_OPTION_TONE_VERIFY, &val, sizeof(char), 0);
		if (strchr(options + 1, 'v') || strchr(options + 1, 'V'))
			phone_vox = 1;
		if (strchr(options + 1, 'm') || strchr(options + 1, 'M'))
			phone_monitor = 1;
	} else {
#ifdef ALLOW_LOCAL_CHANNELS
		/* Check to ensure the connection is IAX2 or Local */
		if (!CHAN_TECH(chan, "IAX2") && !CHAN_TECH(chan, "echolink") && !CHAN_TECH(chan, "tlb") && !CHAN_TECH(chan, "Local")) {
			ast_log(LOG_WARNING, "We only accept links via IAX2, EchoLink, TheLinkBox or Local!!\n");
			return -1;
		}
#else
		if (!CHAN_TECH(chan, "IAX2") && !CHAN_TECH(chan, "echolink") && !CHAN_TECH(chan, "tlb")) {
			ast_log(LOG_WARNING, "We only accept links via IAX2, EchoLink, or TheLinkBox!!\n");
			return -1;
		}
#endif
		if ((myrpt->p.s[myrpt->p.sysstate_cur].txdisable) ||
			myrpt->p.s[myrpt->p.sysstate_cur].noincomingconns) { /* Do not allow incoming radio connections if disabled or noincomingconns is set */
			ast_log(LOG_NOTICE, "Connect attempt to node %s  with tx disabled or NOICE cop function active", myrpt->name);
			return -1;
		}
	}
	if (options && (*options == 'R')) {
		/* Parts of this section taken from app_parkandannounce */
		char *return_context;
		// int l, m, lot, timeout = 0;
		int l, timeout = 0;
		enum patch_call_mode callmode;
		char tmp[256], *template;
		char *working, *context, *exten, *priority;
		char *s, *orig_s;

		rpt_mutex_lock(&myrpt->lock);
		callmode = myrpt->callmode;
		rpt_mutex_unlock(&myrpt->lock);

		if ((!myrpt->p.nobusyout) && (callmode != CALLMODE_DOWN)) {
			if (ast_channel_state(chan) != AST_STATE_UP) {
				ast_indicate(chan, AST_CONTROL_BUSY);
			}
			while (ast_safe_sleep(chan, 10000) != -1) {
			}
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

		template = strsep(&s, separator);
		if (!template) {
			ast_log(LOG_WARNING, "An announce template must be defined\n");
			ast_free(orig_s);
			return -1;
		}

		if (s) {
			timeout = atoi(strsep(&s, separator));
			timeout *= 1000;
		}

		return_context = s;

		if (return_context != NULL) {
			/* set the return context. Code borrowed from the Goto builtin */

			working = return_context;
			context = strsep(&working, separator);
			exten = strsep(&working, separator);
			if (!exten) {
				/* Only a priority in this one */
				priority = context;
				exten = NULL;
				context = NULL;
			} else {
				priority = strsep(&working, separator);
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
		} else { /* increment the priority by default */
			ast_channel_priority_set(chan, ast_channel_priority(chan) + 1);
		}

		ast_verb(3, "Return Context: (%s,%s,%d) ID: %s\n", ast_channel_context(chan), ast_channel_exten(chan),
			ast_channel_priority(chan), ast_channel_caller(chan)->id.number.str);
		if (!ast_exists_extension(chan, ast_channel_context(chan), ast_channel_exten(chan), ast_channel_priority(chan),
				ast_channel_caller(chan)->id.number.str)) {
			ast_log(LOG_WARNING, "Return Context Invalid, call will return to default|s\n");
		}

		/* we are using masq_park here to protect * from touching the channel once we park it.  If the channel comes out of
		   timeout before we are done announcing and the channel is messed with, Kablooeee.  So we use Masq to prevent this.  */

		/*! \todo the parking API changed a while ago, this all needs to be completely redone here */
		// old way: https://github.com/asterisk/asterisk/blob/1.8/apps/app_parkandannounce.c
		// new way: https://github.com/asterisk/asterisk/blob/master/res/parking/parking_applications.c#L890

		// ast_masq_park_call(chan, NULL, timeout, &lot); // commented out to avoid compiler error.

		// ast_verb(3, "Call Parking Called, lot: %d, timeout: %d, context: %s\n", lot, timeout, return_context);

		// snprintf(tmp,sizeof(tmp) - 1,"%d,%s",lot,template + 1);

		rpt_telemetry(myrpt, REV_PATCH, tmp);

		ast_free(orig_s);

		return 0;
	}
	i = 0;
	if (CHAN_TECH(chan, "echolink") || CHAN_TECH(chan, "tlb")) {
		i = 1;
	}
	if (!options && !i) {
		char hisip[100] = "";
		/* look at callerid to see what node this comes from */
		if (!ast_channel_caller(chan)->id.number.str) { /* if doesn't have caller id */
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
		struct ast_format_cap *cap;

		rpt_mutex_lock(&myrpt->lock);
		i = myrpt->xlink || (!myrpt->ready);
		rpt_mutex_unlock(&myrpt->lock);
		if (i) {
			ast_log(LOG_WARNING, "Cannot connect to node %s, system busy\n", myrpt->name);
			return -1;
		}
		rpt_mutex_lock(&myrpt->lock);
		while ((!ast_tvzero(myrpt->lastlinktime)) && (ast_tvdiff_ms(rpt_tvnow(), myrpt->lastlinktime) < 250)) {
			rpt_mutex_unlock(&myrpt->lock);
			if (ast_check_hangup(myrpt->rxchannel)) {
				return -1;
			}
			if (ast_safe_sleep(myrpt->rxchannel, 100) == -1) {
				return -1;
			}
			rpt_mutex_lock(&myrpt->lock);
		}
		myrpt->lastlinktime = rpt_tvnow();
		rpt_mutex_unlock(&myrpt->lock);
		/* look at callerid to see what node this comes from */
		if (!ast_channel_caller(chan)->id.number.str) { /* if doesn't have caller id */
			ast_log(LOG_WARNING, "Doesn't have callerid on %s\n", tmp);
			return -1;
		}
		if (phone_mode != RPT_PHONE_MODE_NONE) {
			b1 = "0";
			b = NULL;
			if (callstr)
				b1 = callstr;
		} else {
			// b = chan->cid.cid_name;
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
		if (!b1[i]) { /* if not a call-based node number */
			rpt_mutex_lock(&myrpt->lock);
			/* try to find this one in queue */
			l = ao2_find(myrpt->links, b1, 0);
			/* if found */
			if (l != NULL) {
				l->killme = 1;
				l->retries = l->max_retries + 1;
				l->disced = 2;
				reconnects = l->reconnects;
				reconnects++;
				ao2_ref(l, -1);
				rpt_mutex_unlock(&myrpt->lock);
				usleep(500000);
			} else {
				rpt_mutex_unlock(&myrpt->lock);
			}
		}
		/* establish call in transceive mode */
		l = ao2_alloc(sizeof(struct rpt_link), rpt_link_destroy);
		if (!l) {
			return -1;
		}
		l->linklist = ast_str_create(RPT_AST_STR_INIT_SIZE);
		if (!l->linklist) {
			ao2_ref(l, -1);
			return -1;
		}
		l->mode = MODE_TRANSCEIVE;
		ast_copy_string(l->name, b1, MAXNODESTR);
		l->chan = chan;
		l->last_frame_sent = 0;
		l->connected = 1;
		l->thisconnected = 1;
		l->hasconnected = 1;
		l->reconnects = reconnects;
		l->phonemode = phone_mode;
		l->phonevox = phone_vox;
		l->phonemonitor = phone_monitor;
		l->rxlingertimer = RX_LINGER_TIME;
		l->newkeytimer = NEWKEYTIME;
		l->link_newkey = RADIO_KEY_ALLOWED;
		if ((phone_mode == RPT_PHONE_MODE_NONE) && (l->name[0] != '0') && !CHAN_TECH(chan, "echolink") && !CHAN_TECH(chan, "tlb")) {
			l->link_newkey = RADIO_KEY_NOT_ALLOWED;
		}
		ast_debug(7, "newkey: %d\n", l->link_newkey);
		if (l->name[0] > '9') {
			l->newkeytimer = 0;
		}
		voxinit_link(l, 1);
		/*! \todo XXX These init_linkmode() calls do nothing.
		 * After using enum, the compiler pointed it out.
		 * Need to research what they used to do in ASL2...
		 */
		if (CHAN_TECH(chan, "echolink")) {
			init_linkmode(myrpt, l, LINKMODE_ECHOLINK);
		} else if (CHAN_TECH(chan, "tlb")) {
			init_linkmode(myrpt, l, LINKMODE_TLB);
		} else if (phone_mode != RPT_PHONE_MODE_NONE) {
			init_linkmode(myrpt, l, LINKMODE_PHONE);
		} else {
			init_linkmode(myrpt, l, LINKMODE_GUI);
		}
		ast_set_read_format(l->chan, ast_format_slin);
		ast_set_write_format(l->chan, ast_format_slin);
		myrpt->lastlinktime = rpt_tvnow();

		cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (!cap) {
			ast_log(LOG_ERROR, "Failed to alloc cap\n");
			ao2_ref(l, -1);
			return -1;
		}

		ast_format_cap_append(cap, ast_format_slin, 0);

		/* allocate a pseudo-channel thru asterisk */
		if (__rpt_request_local(l, cap, RPT_PCHAN, RPT_LINK_CHAN, "IAXLink")) {
			ao2_ref(cap, -1);
			ao2_ref(l, -1);
			return -1;
		}

		ao2_ref(cap, -1);

		/* make a conference for the tx */
		if (rpt_conf_add(l->pchan, myrpt, RPT_CONF)) {
			ao2_ref(l, -1);
			return -1;
		}

		if ((phone_mode == RPT_PHONE_MODE_DUMB_DUPLEX) && (!phone_vox))
			l->lastrealrx = 1;
		l->max_retries = MAX_RETRIES;

		if (ast_channel_state(chan) != AST_STATE_UP) {
			ast_answer(chan);
			if (l->name[0] > '9') {
				if (ast_safe_sleep(chan, 500) == -1) {
					ast_debug(3, "Channel %s hung up\n", ast_channel_name(chan));
					ao2_ref(l, -1);
					return -1;
				}
			}
		}

		donodelog_fmt(myrpt, "LINK%s,%s", l->phonemode ? "(P)" : "", l->name);
		doconpgm(myrpt, l->name);
		if ((phone_mode == RPT_PHONE_MODE_NONE) && (l->name[0] <= '9')) {
			send_newkey(chan);
		}

		if (CHAN_TECH(chan, "echolink") || CHAN_TECH(chan, "tlb") || (l->name[0] > '9')) {
			rpt_telemetry(myrpt, CONNECTED, l);
		}

		/* The way things work here is that other threads in app_rpt service the channel,
		 * and the PBX thread which initially gave us this channel is going to exit momentarily.
		 * Originally, we would return AST_PBX_KEEPALIVE to tell the PBX not to hangup
		 * the channel when terminating the PBX.
		 * This was removed in Asterisk commit 50a25ac8474d7900ba59a68ed4fd942074082435
		 *
		 * The new way things are done does not work for us out of the box, because the PBX
		 * needs to be told from the get-go not to hangup the channel, and by the time
		 * dialplan is running, it's already too late.
		 *
		 * Instead, we masquerade the channel here to force the old pointer to the channel
		 * to become invalid. The old channel, now dead, can then get hung up by the
		 * PBX thread as normal, while the new channel is what we insert into the list. */
		chan = ast_channel_yank(chan);
		if (!chan) {
			/* l->chan still points to the original chan */
			ast_log(LOG_ERROR, "Failed to masquerade channel %s\n", ast_channel_name(l->chan));
			ao2_ref(l, -1);
			return -1;
		}
		l->chan = chan; /* Update pointer to the masqueraded channel. The original channel is dead. */

		/* insert at end of queue */
		rpt_mutex_lock(&myrpt->lock);
		rpt_link_add(myrpt->links, l); /* After putting the link in the link list, other threads can start using it */
		__kickshort(myrpt);
		myrpt->lastlinktime = rpt_tvnow();
		rpt_mutex_unlock(&myrpt->lock);
		rpt_update_links(myrpt);
		return -1; /* We can now safely return -1 to the PBX, as the old channel pre-masquerade is what will get killed off */
	}
	/* well, then it is a remote */
	rpt_mutex_lock(&myrpt->lock);
	/* look at callerid to see what node this comes from */
	if (!ast_channel_caller(chan)->id.number.str) { /* if doesn't have caller id */
		b1 = "0";
		b = NULL;
	} else {
		// b = chan->cid.cid_name;
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
					if (ast_safe_sleep(chan, 20)) {
						break;
					}
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
				if (ao2_container_count(rpt_vars[i].links) || rpt_vars[i].keyed || ((rpt_vars[i].lastkeyedtime + RPT_LOCKOUT_SECS) > now) ||
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
					if (ast_safe_sleep(chan, 500) == -1) {
						rpt_disable_cdr(chan);
						return -1;
					}
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
		rpt_mutex_unlock(&myrpt->lock);
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		return -1;
	}

	ast_format_cap_append(cap, ast_format_slin, 0);

	if (__rpt_request(myrpt, cap, RPT_RXCHAN, RPT_LINK_CHAN)) {
		rpt_mutex_unlock(&myrpt->lock);
		ao2_ref(cap, -1);
		return -1;
	}

	myrpt->localtxchannel = NULL;
	if (myrpt->txchanname) {
		if (__rpt_request(myrpt, cap, RPT_TXCHAN, RPT_LINK_CHAN)) {
			rpt_mutex_unlock(&myrpt->lock);
			rpt_hangup(myrpt, RPT_RXCHAN);
			ao2_ref(cap, -1);
			return -1;
		}
	} else {
		myrpt->txchannel = myrpt->rxchannel;
		if (IS_LOCAL_NAME(myrpt->rxchanname)) {
			myrpt->localtxchannel = myrpt->rxchannel;
		}
	}

	i = 3;
	ast_channel_setoption(myrpt->rxchannel, AST_OPTION_TONE_VERIFY, &i, sizeof(char), 0);

	if (rpt_request_local(myrpt, cap, RPT_PCHAN, "PChan")) {
		rpt_mutex_unlock(&myrpt->lock);
		rpt_hangup_rx_tx(myrpt);
		ao2_ref(cap, -1);
		return -1;
	}

	ao2_ref(cap, -1);

	if (!myrpt->localrxchannel)
		myrpt->localrxchannel = myrpt->rxpchannel;
	if (!myrpt->localtxchannel)
		myrpt->localtxchannel = myrpt->pchannel;

	/* first put the channel on the conference in announce/monitor mode */
	if (rpt_conf_create(myrpt, RPT_TXCONF)) {
		rpt_mutex_unlock(&myrpt->lock);
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		return -1;
	}

	if (rpt_conf_add(myrpt->pchannel, myrpt, RPT_TXCONF)) {
		rpt_mutex_unlock(&myrpt->lock);
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		return -1;
	}
	/* if serial io port, open it */
	myrpt->iofd = -1;
	if (myrpt->p.ioport && ((myrpt->iofd = openserial(myrpt, myrpt->p.ioport)) == -1)) {
		rpt_mutex_unlock(&myrpt->lock);
		rpt_hangup_rx_tx(myrpt);
		rpt_hangup(myrpt, RPT_PCHAN);
		ao2_ref(cap, -1);
		return -1;
	}

	iskenwood_pci4 = 0;
	if ((myrpt->iofd < 1) && (myrpt->txchannel == myrpt->localtxchannel)) {
		res = rpt_radio_set_param(myrpt->localtxchannel, myrpt, RPT_RADPAR_REMMODE, RPT_RADPAR_REM_NONE);
		/* if PCIRADIO and kenwood selected */
		if ((!res) && (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD))) {
			if (kenwood_uio_helper(myrpt)) {
				rpt_mutex_unlock(&myrpt->lock);
				return -1;
			}
			iskenwood_pci4 = 1;
		}
	}
	if (myrpt->txchannel == myrpt->localtxchannel) {
		dahdi_set_onhook(myrpt->localtxchannel);
		/* if PCIRADIO and Yaesu ft897/ICOM IC-706 selected */
		if ((myrpt->iofd < 1) && (!res) &&
			((!strcmp(myrpt->remoterig, REMOTE_RIG_FT897)) || (!strcmp(myrpt->remoterig, REMOTE_RIG_FT950)) ||
				(!strcmp(myrpt->remoterig, REMOTE_RIG_FT100)) || (!strcmp(myrpt->remoterig, REMOTE_RIG_XCAT)) ||
				(!strcmp(myrpt->remoterig, REMOTE_RIG_IC706)) || (!strcmp(myrpt->remoterig, REMOTE_RIG_TM271)))) {
			if (rpt_radio_set_param(myrpt->localtxchannel, myrpt, RPT_RADPAR_UIOMODE, 1)) {
				ast_log(LOG_ERROR, "Cannot set UIOMODE on %s: %s\n", ast_channel_name(myrpt->localtxchannel), strerror(errno));
				rpt_mutex_unlock(&myrpt->lock);
				return -1;
			}
			if (rpt_radio_set_param(myrpt->localtxchannel, myrpt, RPT_RADPAR_UIODATA, 3)) {
				ast_log(LOG_ERROR, "Cannot set UIODATA on %s: %s\n", ast_channel_name(myrpt->localtxchannel), strerror(errno));
				rpt_mutex_unlock(&myrpt->lock);
				return -1;
			}
		}
	}
	if ((myrpt->p.nlconn) && (CHAN_TECH(myrpt->rxchannel, "radio") || CHAN_TECH(myrpt->rxchannel, "simpleusb"))) {
		/* go thru all the specs */
		for (i = 0; i < myrpt->p.nlconn; i++) {
			int j, k;
			char string[100];

			if (sscanf(myrpt->p.lconn[i], "GPIO" N_FMT(d) "=" N_FMT(d), &j, &k) == 2 ||
				sscanf(myrpt->p.lconn[i], "GPIO" N_FMT(d) ":" N_FMT(d), &j, &k) == 2) {
				snprintf(string, sizeof(string), "GPIO %d %d", j, k);
				ast_sendtext(myrpt->rxchannel, string);
			} else if (sscanf(myrpt->p.lconn[i], "PP" N_FMT(d) "=" N_FMT(d), &j, &k) == 2) {
				snprintf(string, sizeof(string), "PP %d %d", j, k);
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
	myrpt->hfscanmode = HF_SCAN_OFF;
	myrpt->hfscanstatus = 0;
	if (myrpt->p.startupmacro) {
		ast_str_set(&myrpt->macrobuf, 0, "PPPP%s", myrpt->p.startupmacro);
	}
	time(&myrpt->start_time);
	myrpt->last_activity_time = myrpt->start_time;
	last_timeout_warning = 0;
	myrpt->reload = 0;
	myrpt->tele.next = &myrpt->tele;
	myrpt->tele.prev = &myrpt->tele;
	myrpt->rpt_newkey = RADIO_KEY_ALLOWED;
	myrpt->lastitx = !myrpt->lastitx;
	myrpt->tunerequest = 0;
	myrpt->tunetx = 0;
	if (!myrpt->macrobuf) {
		myrpt->macrobuf = ast_str_create(MAXMACRO);
		if (!myrpt->macrobuf) {
			rpt_mutex_unlock(&myrpt->lock);
			rpt_hangup_rx_tx(myrpt);
			rpt_hangup(myrpt, RPT_PCHAN);
			ao2_ref(cap, -1);
			pthread_exit(NULL);
		}
	}
	rpt_mutex_unlock(&myrpt->lock);
	ast_set_write_format(chan, ast_format_slin);
	ast_set_read_format(chan, ast_format_slin);
	rem_rx = 0;
	remkeyed = 0;

	answer_newkey_helper(myrpt, chan, phone_mode);

	if (myrpt->rxchannel == myrpt->localrxchannel) {
		if (dahdi_rx_offhook(myrpt->localrxchannel) == 1) {
			ast_indicate(chan, AST_CONTROL_RADIO_KEY);
			myrpt->remoterx = 1;
			remkeyed = 1;
		}
	}
	/* look at callerid to see what node this comes from */
	if (!ast_channel_caller(chan)->id.number.str) { /* if doesn't have caller id */
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
		int do_archive = 0;

		if (myrpt->p.archiveaudio) {
			do_archive = 1;
			if (myrpt->p.monminblocks) {
				long blocksleft;

				blocksleft = diskavail(myrpt);
				if (myrpt->p.remotetimeout) {
					blocksleft -= (myrpt->p.remotetimeout * MONITOR_DISK_BLOCKS_PER_MINUTE) / 60;
				}
				if (blocksleft < myrpt->p.monminblocks) {
					do_archive = 0;
				}
			}
		}

		if (do_archive) {
			char mydate[100], myfname[PATH_MAX];
			const char *myformat;

			donode_make_datestr(mydate, sizeof(mydate), NULL, myrpt->p.archivedatefmt);
			myformat = S_OR(myrpt->p.archiveformat, "wav49");
			snprintf(myfname, sizeof(myfname), "%s/%s/%s.%s", myrpt->p.archivedir, myrpt->name, mydate, myformat);
			ast_start_mixmonitor(chan, myfname, "a");
		}

		donodelog_fmt(myrpt, "CONNECT,%s", b1);
		rpt_update_links(myrpt);
		doconpgm(myrpt, b1);
	}
	/* if is a webtransceiver */
	if (myrpt->remote_webtransceiver)
		myrpt->rpt_newkey = RADIO_KEY_NOT_ALLOWED;
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
	if (myrpt->rxchannel != myrpt->txchannel)
		cs[n++] = myrpt->txchannel;

	if (phone_mode == RPT_PHONE_MODE_NONE) {
		send_newkey(chan);
	}

	myfirst = 0;
	looptimestart = rpt_tvnow();
	/* start un-locked */
	for (;;) {
		struct ast_channel *who;
		time_t t;
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
			if ((myrpt->p.remotetimeoutwarning) && (r >= (myrpt->p.remotetimeout - myrpt->p.remotetimeoutwarning)) &&
				(r <= (myrpt->p.remotetimeout - myrpt->p.remotetimeoutwarningfreq))) {
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
			if ((myrpt->p.remotetimeoutwarning) && (r >= (myrpt->p.remoteinacttimeout - myrpt->p.remotetimeoutwarning)) &&
				(r <= (myrpt->p.remoteinacttimeout - myrpt->p.remotetimeoutwarningfreq))) {
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
		who = ast_waitfor_n(cs, n, &ms);
		elap = rpt_time_elapsed(&looptimestart); /* calculate loop time */
		update_timer(&myrpt->macrotimer, elap, 0);
		if (who == NULL) {
			/* No channels had activity. Loop again. */
			continue;
		}
		update_timer(&myrpt->dtmf_local_timer, elap, 1);
		update_timer(&myrpt->voxtotimer, elap, 0);
		myrx = keyed;
		if (phone_mode != RPT_PHONE_MODE_NONE && phone_vox) {
			myrx = (!AST_LIST_EMPTY(&myrpt->rxq));
			if (myrpt->voxtotimer <= 0) {
				voxtostate_to_voxtotimer(myrpt);
			}
			if (!myrpt->voxtostate)
				myrx = myrx || myrpt->wasvox;
		}
		keyed = myrx;
		update_timer(&myrpt->rxlingertimer, elap, 0);
		if ((myrpt->rpt_newkey == RADIO_KEY_NOT_ALLOWED) && keyed && (!myrpt->rxlingertimer)) {
			myrpt->rerxtimer = 0;
			keyed = 0;
		}
		rpt_mutex_lock(&myrpt->lock);
		do_dtmf_local(myrpt, 0);
		rpt_mutex_unlock(&myrpt->lock);
		rem_totx = myrpt->dtmf_local_timer && (phone_mode == RPT_PHONE_MODE_NONE);
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
			ast_debug(7, "Set rem_totx=%i.  dtmf_local_timer=%i phone_mode=%i keyed=%i tunerequest=%i\n", rem_totx,
				myrpt->dtmf_local_timer, phone_mode, keyed, myrpt->tunerequest);
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
			if (myrpt->rpt_newkey != RADIO_KEY_NOT_ALLOWED)
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
		if (myrpt->rpt_newkey == RADIO_KEY_ALLOWED_REDUNDANT) {
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
				ast_debug(7, "Handle rem_totx=%i.  dtmf_local_timer=%i  tunerequest=%i\n", rem_totx, myrpt->dtmf_local_timer,
					myrpt->tunerequest);

				myrpt->remotetx = 1;
				if ((myrpt->remtxfreqok = check_tx_freq(myrpt))) {
					time(&myrpt->last_activity_time);
					telem = myrpt->tele.next;
					while (telem != &myrpt->tele) {
						if (telem->mode == ACT_TIMEOUT_WARNING && !telem->killed) {
							if (telem->chan)
								ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV); /* Whoosh! */
							telem->killed = 1;
						}
						telem = telem->next;
					}
					if (iskenwood_pci4 && myrpt->txchannel == myrpt->localtxchannel) {
						if (rpt_radio_set_param(myrpt->localtxchannel, myrpt, RPT_RADPAR_UIODATA, 1)) {
							ast_log(LOG_ERROR, "Cannot set UIODATA on %s: %s\n", ast_channel_name(myrpt->localtxchannel), strerror(errno));
							return -1;
						}
					} else {
						ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_KEY);
					}
					rpt_update_boolean(myrpt, "RPT_TXKEYED", 1);
					donodelog(myrpt, "TXKEY");
				}
			}
		}
		if (!rem_totx && myrpt->remotetx) { /* Remote base radio TX unkey */
			myrpt->remotetx = 0;
			if (!myrpt->remtxfreqok) {
				rpt_telemetry(myrpt, UNAUTHTX, NULL);
			}
			if (iskenwood_pci4 && myrpt->txchannel == myrpt->localtxchannel) {
				if (rpt_radio_set_param(myrpt->localtxchannel, myrpt, RPT_RADPAR_UIODATA, 3)) {
					ast_log(LOG_ERROR, "Cannot set UIODATA on %s: %s\n", ast_channel_name(myrpt->localtxchannel), strerror(errno));
					return -1;
				}
			} else {
				ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_UNKEY);
			}
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
		str = ast_str_buffer(myrpt->macrobuf);
		len = ast_str_strlen(myrpt->macrobuf);
		c = str[0];
		if (c && !myrpt->macrotimer) {
			myrpt->macrotimer = MACROTIME;
			ast_copy_string(str, str + 1, len);
			ast_str_truncate(myrpt->macrobuf, len - 1);
			if ((c == 'p') || (c == 'P'))
				myrpt->macrotimer = MACROPTIME;
			rpt_mutex_unlock(&myrpt->lock);
			donodelog_fmt(myrpt, "DTMF(M),%c", c);
			if (handle_remote_dtmf_digit(myrpt, c, &keyed, RPT_PHONE_MODE_NONE) == -1)
				break;
			continue;
		} else {
			rpt_mutex_unlock(&myrpt->lock);
		}

		if (who == chan) { /* if it was a read from incoming */
			if (exec_chan_read(myrpt, chan, &keyed, phone_mode, phone_vox, &myfirst, &dtmfed)) {
				break;
			}
		} else if (who == myrpt->rxchannel) { /* if it was a read from radio */
			if (exec_rxchannel_read(myrpt, reming, notremming, &remkeyed)) {
				break;
			}
		} else if (who == myrpt->pchannel) { /* if is remote mix output */
			if (exec_pchannel_read(myrpt, chan)) {
				break;
			}
		} else if (myrpt->rxchannel != myrpt->txchannel && who == myrpt->txchannel) { /* do this cuz you have to */
			if (exec_txchannel_read(myrpt)) {
				break;
			}
		}
	}
	if (myrpt->p.archivedir || myrpt->p.discpgm) {
		/* look at callerid to see what node this comes from */
		if (!ast_channel_caller(chan)->id.number.str) { /* if doesn't have caller id */
			b1 = "0";
		} else {
			ast_callerid_parse(ast_channel_caller(chan)->id.number.str, &b, &b1);
			ast_shrink_phone_number(b1);
		}
		rpt_update_links(myrpt);
		donodelog_fmt(myrpt, "DISCONNECT,%s", b1);
		dodispgm(myrpt, b1);
	}
	myrpt->remote_webtransceiver = 0;
	/* wait for telem to be done */
	while (myrpt->tele.next != &myrpt->tele)
		usleep(50000);
	ast_stop_mixmonitor(chan, NULL);
	rpt_mutex_lock(&myrpt->lock);
	myrpt->hfscanmode = HF_SCAN_OFF;
	myrpt->hfscanstatus = 0;
	myrpt->remoteon = 0;
	rpt_mutex_unlock(&myrpt->lock);
	rpt_frame_queue_free(&myrpt->frame_queue);
	if ((iskenwood_pci4) && (myrpt->txchannel == myrpt->localtxchannel)) {
		if (kenwood_uio_helper(myrpt)) {
			return -1;
		}
	}
	if ((myrpt->p.nldisc) && (CHAN_TECH(myrpt->rxchannel, "radio") || CHAN_TECH(myrpt->rxchannel, "simpleusb"))) {
		/* go thru all the specs */
		for (i = 0; i < myrpt->p.nldisc; i++) {
			int j, k;
			char string[100];

			if (sscanf(myrpt->p.ldisc[i], "GPIO" N_FMT(d) "=" N_FMT(d), &j, &k) == 2 ||
				sscanf(myrpt->p.ldisc[i], "GPIO" N_FMT(d) ":" N_FMT(d), &j, &k) == 2) {
				snprintf(string, sizeof(string), "GPIO %d %d", j, k);
				ast_sendtext(myrpt->rxchannel, string);
			} else if (sscanf(myrpt->p.ldisc[i], "PP" N_FMT(d) "=" N_FMT(d), &j, &k) == 2) {
				snprintf(string, sizeof(string), "PP %d %d", j, k);
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
			myrpt->rxchannel = NULL; /* If we aborted the repeater but haven't unloaded, this channel handle is not valid anymore
										in a future call to stop_repeaters() */
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
	}

	res = ast_unregister_application(app);
	res |= rpt_cleanup_telemetry();

#ifdef _MDC_ENCODE_H_
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
	res |= rpt_init_telemetry();

#ifdef _MDC_ENCODE_H_
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

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Radio Repeater/Remote Base Application", .support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module, .unload = unload_module, .reload = reload, .requires = "res_curl", );
