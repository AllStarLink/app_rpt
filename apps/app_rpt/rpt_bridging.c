/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2023, Naveen Albert
 *
 * Naveen Albert <asterisk@phreaknet.org>
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
 * \brief Repeater bridging and conferencing functions
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \note This file contains bridging and conferencing code
 * that is mostly abstracted away to other files in app_rpt.
 * Anything directly dealing with DAHDI is either in rpt_bridging.c
 * or in rpt_radio.c, making this abstraction easier.
 *
 * The goal here is to allow for app_rpt to be usable without DAHDI;
 * when app_rpt was originally written by Jim Dixon, things like
 * conferencing and timing could only be provided by DAHDI. Much
 * changed during the long time that app_rpt was stagnant; conferencing
 * can now be done without DAHDI, and app_confbridge uses native Asterisk
 * bridging as opposed to the deprecated app_meetme which uses DAHDI bridging.
 *
 * Likewise, chan_voter was hardcoded to use DAHDI for timing, but now
 * uses the generic Asterisk timing API, which could use a number of timing sources.
 *
 * rpt_bridging.c has been set up so that DAHDI can continue to be used
 * for conferencing if desired, but also to allow native Asterisk bridging to be
 * used, so that DAHDI is not a hard dependency of app_rpt. Unlike DAHDI bridging,
 * native Asterisk bridging is done entirely in userspace, so when not using
 * DAHDI hardware, this is likely to be slightly more efficient.
 *
 * Certain other functionality that I am not as familiar with, in particular,
 * the radio stuff in rpt_radio.c that makes direct DAHDI system calls,
 * might possibly be less functional without DAHDI.
 *
 * ----
 * The goal is to have feature parity without DAHDI to match current functionality with DAHDI,
 * if nothing else, for flexibility.
 */

#include "asterisk.h"

/* By default, we will detect if DAHDI is available
 * and use it if so.
 * To force using DAHDI or force NOT using DAHDI,
 * the appropriate define can be used to override. */

/* Force DAHDI bridging.
 * This should mostly not be used; DAHDI will be used by default if available.
 * However, you can use this to force compilation failure if DAHDI is not present. */
/* #define RPT_FORCE_DAHDI_BRIDGING */

/* Disable DAHDI bridging completely, preventing it from being used even if DAHDI is available.
 * Useful for testing and development.
 * This will also prevent toggling DAHDI bridging on at runtime! */
/* #define RPT_FORCE_ASTERISK_BRIDGING */

/* Disable native Asterisk bridging from being used.
 * If DAHDI bridging is also disabled, this will cause compilation failure. */
/* #define RPT_DISABLE_ASTERISK_BRIDGING */

/* By default, use DAHDI if it's available */
#ifdef HAVE_DAHDI
#define DAHDI_BRIDGING_AVAILABLE 1
#else
#define DAHDI_BRIDGING_AVAILABLE 0
#endif

/* DAHDI bridging may be used only if DAHDI is available */
#ifdef RPT_FORCE_DAHDI_BRIDGING
#ifndef HAVE_DAHDI
#error "DAHDI is not available on this system"
#else
#define DAHDI_BRIDGING_AVAILABLE 1
#endif /* HAVE_DAHDI */
#endif

/* Henceforth, we refrain from using #ifdef HAVE_DAHDI
 * and use #if DAHDI_BRIDGING_AVAILABLE instead. */

/* Asterisk bridging may always be used, unless explicitly disabled */
#ifdef RPT_FORCE_ASTERISK_BRIDGING
#ifdef DAHDI_BRIDGING_AVAILABLE
#undef DAHDI_BRIDGING_AVAILABLE
#endif /* DAHDI_BRIDGING_AVAILABLE */
#define DAHDI_BRIDGING_AVAILABLE 0
#endif

#ifdef RPT_DISABLE_ASTERISK_BRIDGING
#define ASTERISK_BRIDGING_AVAILABLE 0
/* Obviously, we need some conferencing mechanism to work.
 * And logically, if !use_dahdi_bridging, this means use Asterisk bridging. */
#if !DAHDI_BRIDGING_AVAILABLE
#error "Either DAHDI or Asterisk bridging must be enabled to compile"
#endif
#else
#define ASTERISK_BRIDGING_AVAILABLE 1
#endif

/* Allow this to possibly be toggled without recompiling.
 * At runtime, this could be toggled on if DAHDI_BRIDGING_AVAILABLE,
 * and off if ASTERISK_BRIDGING_AVAILABLE. */
static int use_dahdi_bridging = DAHDI_BRIDGING_AVAILABLE;

/* A note about the convention used when naming functions here:
 * We use the ast_ prefix to denote Asterisk bridging (in contrast to DAHDI bridging),
 * but we do not begin functions with ast_ since that is typically reserved
 * for core Asterisk APIs. So we use rpt_ast_ to disambiguate. */

#if DAHDI_BRIDGING_AVAILABLE
#include <dahdi/user.h>
#include <dahdi/tonezone.h>		/* use tone_zone_set_zone */
#endif

#include "asterisk/channel.h"
#include "asterisk/indications.h"
#include "asterisk/format_cache.h" /* use ast_format_slin */

#if ASTERISK_BRIDGING_AVAILABLE
#include "asterisk/bridge.h"
#endif

#include "app_rpt.h"

#include "rpt_bridging.h"
#include "rpt_conf.h"
#include "rpt_call.h"

int rpt_set_bridging_subsystem(int usedahdi)
{
	static int set = 0;
	if (set) {
		ast_log(LOG_WARNING, "Bridging mechanism cannot be changed once set\n");
		return -1;
	}
	set = 1;
	if (usedahdi) {
#if DAHDI_BRIDGING_AVAILABLE
		use_dahdi_bridging = 1;
		return 0;
#else
		ast_log(LOG_WARNING, "app_rpt was not compiled with DAHDI support\n");
		return -1;
#endif /* DAHDI_BRIDGING_AVAILABLE */
	} else {
#if ASTERISK_BRIDGING_AVAILABLE
		use_dahdi_bridging = 0;
		return 0;
#endif /* ASTERISK_BRIDGING_AVAILABLE */
		ast_log(LOG_WARNING, "app_rpt was not compiled with non-DAHDI support\n");
		return -1;
	}
}

static const char *rpt_conf_type_str(enum rpt_conf_type type)
{
	switch (type) {
	case RPT_CONF:
		return "conf";
	case RPT_TXCONF:
		return "txconf";
	case RPT_TELECONF:
		return "teleconf";
	}
	ast_assert(0);
	return NULL;
}

static const char *rpt_chan_type_str(enum rpt_chan_type chantype)
{
	switch (chantype) {
	case RPT_RXCHAN:
		return "rxchan";
	case RPT_TXCHAN:
		return "txchan";
	case RPT_PCHAN:
		return "pchan";
	case RPT_DAHDITXCHAN:
		return "dahditxchan";
	case RPT_MONCHAN:
		return "monchan";
	case RPT_PARROTCHAN:
		return "parrotchan";
	case RPT_TELECHAN:
		return "telechan";
	case RPT_BTELECHAN:
		return "btelechan";
	case RPT_VOXCHAN:
		return "voxchan";
	case RPT_TXPCHAN:
		return "txpchan";
	}
	ast_assert(0);
	return NULL;
}

static const char *rpt_chan_name(struct rpt *myrpt, enum rpt_chan_type chantype)
{
	switch (chantype) {
	case RPT_RXCHAN:
		return myrpt->rxchanname;
	case RPT_TXCHAN:
		return myrpt->txchanname;
	case RPT_PCHAN:
	case RPT_DAHDITXCHAN:
	case RPT_MONCHAN:
	case RPT_PARROTCHAN:
	case RPT_TELECHAN:
	case RPT_BTELECHAN:
	case RPT_VOXCHAN:
	case RPT_TXPCHAN:
		return NULL;
	}
	ast_assert(0);
	return NULL;
}

static struct ast_channel **rpt_chan_channel(struct rpt *myrpt, struct rpt_link *link, enum rpt_chan_type chantype)
{
	if (myrpt) {
		switch (chantype) {
		case RPT_RXCHAN:
			return &myrpt->rxchannel;
		case RPT_TXCHAN:
			return &myrpt->txchannel;
		case RPT_PCHAN:
			return &myrpt->pchannel;
		case RPT_DAHDITXCHAN:
			return &myrpt->dahditxchannel;
		case RPT_MONCHAN:
			return &myrpt->monchannel;
		case RPT_PARROTCHAN:
			return &myrpt->parrotchannel;
		case RPT_TELECHAN:
			return &myrpt->telechannel;
		case RPT_BTELECHAN:
			return &myrpt->btelechannel;
		case RPT_VOXCHAN:
			return &myrpt->voxchannel;
		case RPT_TXPCHAN:
			return &myrpt->txpchannel;
		}
	} else if (link) {
		switch (chantype) {
		case RPT_PCHAN:
			return &link->pchan;
		default:
			break;
		}
	}
	ast_assert(0);
	return NULL;
}

#define RPT_DIAL_TIME 999

void rpt_hangup(struct rpt *myrpt, enum rpt_chan_type chantype)
{
	const char *name;
	struct ast_channel **chanptr = rpt_chan_channel(myrpt, NULL, chantype);

	if (!*chanptr) {
		ast_log(LOG_WARNING, "No %s channel to hang up for node %s\n", rpt_chan_type_str(chantype), myrpt->name);
		return;
	}

	name = ast_channel_name(*chanptr);
	ast_debug(2, "Hanging up node %s %s channel %s\n", myrpt->name, rpt_chan_type_str(chantype), name);

	/* If RXCHAN == TXCHAN, and we hang up one, also NULL out the other one */
	switch (chantype) {
	case RPT_RXCHAN:
		if (myrpt->txchannel && myrpt->txchannel == *chanptr) {
			ast_debug(2, "Also resetting txchannel (tx == rx)\n");
			myrpt->txchannel = NULL;
		}
		break;
	case RPT_TXCHAN:
		if (myrpt->rxchannel && myrpt->rxchannel == *chanptr) {
			ast_debug(2, "Also resetting rxchannel (rx == tx)\n");
			myrpt->rxchannel = NULL;
		}
		break;
	default:
		break;
	}

	if (ast_test_flag(ast_channel_flags(*chanptr), AST_FLAG_BLOCKING)) {
		struct ast_bridge *bridge = ast_channel_get_bridge(*chanptr);
		/* This will cause an assertion if we were to go ahead
		 * and call ast_hangup right now. */
		ast_debug(1, "Hard hangup called by thread LWP %d on %s, while blocked by thread LWP %d in procedure %s\n",
			ast_get_tid(), ast_channel_name(*chanptr), ast_channel_blocker_tid(*chanptr),
			ast_channel_blockproc(*chanptr));
		/* Probably in a bridge... unbridge it */
		if (bridge) {
			ast_debug(1, "Removing channel %s from bridge %p\n", ast_channel_name(*chanptr), bridge);
			ast_bridge_suspend(bridge, *chanptr);
		}
	}

	ast_hangup(*chanptr);
	*chanptr = NULL;
}

static const char *rpt_chan_app(enum rpt_chan_type chantype, enum rpt_chan_flags flags)
{
	switch (chantype) {
	case RPT_RXCHAN:
		return flags & RPT_LINK_CHAN ? "(Link Rx)" : "(Repeater Rx)";
	case RPT_TXCHAN:
		return flags & RPT_LINK_CHAN ? "(Link Tx)" : "(Repeater Tx)";
	case RPT_PCHAN:
	case RPT_DAHDITXCHAN:
	case RPT_MONCHAN:
	case RPT_PARROTCHAN:
	case RPT_TELECHAN:
	case RPT_BTELECHAN:
	case RPT_VOXCHAN:
	case RPT_TXPCHAN:
		return NULL;
	}
	ast_assert(0);
	return NULL;
}

static const char *rpt_chan_app_data(enum rpt_chan_type chantype)
{
	switch (chantype) {
	case RPT_RXCHAN:
		return "Rx";
	case RPT_TXCHAN:
		return "Tx";
	case RPT_PCHAN:
	case RPT_DAHDITXCHAN:
	case RPT_MONCHAN:
	case RPT_PARROTCHAN:
	case RPT_TELECHAN:
	case RPT_BTELECHAN:
	case RPT_VOXCHAN:
	case RPT_TXPCHAN:
		return NULL;
	}
	ast_assert(0);
	return NULL;
}

int __rpt_request(void *data, struct ast_format_cap *cap, enum rpt_chan_type chantype, enum rpt_chan_flags flags)
{
	char chanstr[256];
	const char *channame;
	struct ast_channel *chan, **chanptr;
	char *tech, *device;
	struct rpt *myrpt = data;

	channame = rpt_chan_name(myrpt, chantype);

	if (ast_strlen_zero(channame)) {
		ast_log(LOG_WARNING, "No %s specified\n", rpt_chan_type_str(chantype));
		return -1;
	}

	ast_copy_string(chanstr, channame, sizeof(chanstr));

	device = chanstr;
	tech = strsep(&device, "/");

	if (ast_strlen_zero(device)) {
		ast_log(LOG_ERROR, "%s device format must be tech/device\n", rpt_chan_type_str(chantype));
		return -1;
	}

	chan = ast_request(tech, cap, NULL, NULL, device, NULL);
	if (!chan) {
		ast_log(LOG_ERROR, "Failed to request %s/%s\n", tech, device);
		return -1;
	}

	if (ast_channel_state(chan) == AST_STATE_BUSY) {
		ast_log(LOG_ERROR, "Requested channel %s is busy?\n", ast_channel_name(chan));
		ast_hangup(chan);
		return -1;
	}

	/* XXX
	 * Note: Removed in refactoring:
	 * inside rpt_make_call, we should rpt_mutex_unlock(&myrpt->lock);
	 * before ast_call
	 * and
	 * rpt_mutex_lock(&myrpt->lock);
	 * afterwards,
	 * if flags & RPT_LINK_CHAN.
	 * This might not be necessary, but if it is, this should be readded. */

	rpt_make_call(chan, device, RPT_DIAL_TIME, tech, rpt_chan_app(chantype, flags), rpt_chan_app_data(chantype), myrpt->name);
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_log(LOG_ERROR, "Requested channel %s not up?\n", ast_channel_name(chan));
		ast_hangup(chan);
		return -1;
	}

	chanptr = rpt_chan_channel(myrpt, NULL, chantype);
	*chanptr = chan;

	switch (chantype) {
	case RPT_RXCHAN:
		myrpt->dahdirxchannel = !strcasecmp(tech, "DAHDI") ? chan : NULL;
		break;
	case RPT_TXCHAN:
		if (flags & RPT_LINK_CHAN) {
			/* XXX Dunno if this difference is really necessary, but this is a literal refactor of existing logic... */
			myrpt->dahditxchannel = !strcasecmp(tech, "DAHDI") ? chan : NULL;
		} else {
			myrpt->dahditxchannel = !strcasecmp(tech, "DAHDI") && strcasecmp(device, "pseudo") ? chan : NULL;
		}
		break;
	default:
		break;
	}

	return 0;
}

/*
 * Conferencing using DAHDI also entails the use of "pseudo channels".
 * As described by Shaun Ruffell:
 *
 * "Pseudo channels are DAHDI channels that are created dynamically in order to
 * mix audio from non-dahdi channels with DAHDI using the dahdi based audio mixing engine.
 * In order to bridge channels in the kernel with the smallest amount of latency,
 * DAHDI (and Zaptel before it) always contained an  audio mixing engine...
 * To allow non-DAHDI based channels to participate in these conferences,
 * it was easiest to keep using the audio mixing engine in the kernel and
 * allow "pseudo" DAHDI channels to be created on the fly that Asterisk could
 * use to feed the audio from other channel technologies into DAHDI for conferencing."
 *
 * chan = ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL)
 * is similar to directly opening, e.g. pfd = open("/dev/dahdi/pseudo", O_RDWR)
 * in that the Asterisk channel just encapsulates this file descriptor.
 *
 * At the most basic level (e.g. as used in app_meetme),
 * suppose chan is some real channel (not necessarily DAHDI, but definitely not pseudo),
 * DAHDI_SETCONF is called on pfd to set its conference number in DAHDI.
 * Then, while chan is running (has not hung up):
 * - wait for activity on chan or pfd
 * - if activity on chan:
 *   - read frame from chan, and potentially write it to pfd (e.g. VOICE).
 *     DTMF doesn't make sense to write to pfd, but you could e.g. write
 *     these frames directly to the channels in the conference, emulate it, etc.
 * - if activity on pfd:
 *   - read frame from pfd
 *   - write frame to chan
 *
 *       Asterisk           |        DAHDI
 * CHAN ast_read  <- pseudo | <- DAHDI conference (mixes audio from many channels,
 * CHAN ast_write -> pseudo | -> DAHDI conference (including chan, excl. each channel's own)
 *
 * The important thing to note is that the regular Asterisk channel (chan) is
 * never interacting with the DAHDI conference directly. Only the pseudo channel is.
 */

struct ast_channel *rpt_request_pseudo_chan(struct ast_format_cap *cap, const char *name)
{
	struct ast_channel *chan = NULL;
#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		/* This won't fail to compile if DAHDI is not available,
		 * but it will fail at runtime since chan_dahdi cannot
		 * load without DAHDI. */
		chan = ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
#if DAHDI_BRIDGING_AVAILABLE
	else
#endif /* DAHDI_BRIDGING_AVAILABLE */
	{
		chan = ast_request("RPTpseudo", cap, NULL, NULL, name, NULL);
	}
#endif /* ASTERISK_BRIDGING_AVAILABLE */
	if (!chan) {
		ast_log(LOG_ERROR, "Failed to request pseudo channel\n");
		return NULL;
	}
	return chan;
}

int __rpt_request_pseudo(void *data, struct ast_format_cap *cap, enum rpt_chan_type chantype, enum rpt_chan_flags flags)
{
	struct rpt *myrpt = NULL;
	struct rpt_link *link = NULL;
	char name[84];
	struct ast_channel *chan, **chanptr;

	if (flags & RPT_LINK_CHAN) {
		link = data;
	} else {
		myrpt = data;
	}

	snprintf(name, sizeof(name), "%s-%s", myrpt ? myrpt->name : "link", rpt_chan_type_str(chantype));
	chan = rpt_request_pseudo_chan(cap, name);
	if (!chan) {
		ast_log(LOG_ERROR, "Failed to request pseudo channel\n");
		return -1;
	}

	ast_debug(1, "Requested channel %s\n", ast_channel_name(chan));

	/* A subset of what rpt_make_call does... */
	ast_set_read_format(chan, ast_format_slin);
	ast_set_write_format(chan, ast_format_slin);
	rpt_disable_cdr(chan);
	ast_answer(chan);

	chanptr = rpt_chan_channel(myrpt, link, chantype);
	*chanptr = chan;

	switch (chantype) {
	case RPT_PCHAN:
		if (!(flags & RPT_LINK_CHAN)) {
			ast_assert(myrpt != NULL);
			if (!myrpt->dahdirxchannel) {
				myrpt->dahdirxchannel = chan;
			}
		}
		break;
	default:
		break;
	}

	return 0;
}

/*
 * Rough channel architecture of app_rpt:
 *
 * == Conference Settings ==
 * rpt() -> rpt_setup_channels():
 * rxchannel ["real" device, e.g. voter, simpleusb, echolink, etc.]
 * txchannel (if specified, sometimes it's the same as rxchannel)
 * dahditxchannel = txchannel, if non-pseudo DAHDI, pseudo otherwise
 *    txconf, CONF_CONF | CONF_LISTENER
 * -- Pseudo channels:
 * pchannel: conf/txconf, CONFANNMON / CONF_CONF | CONF_LISTENER | CONF_TALKER
 * monchannel: txchannel's conf, CONF_MONITORTX or txconf, CONF_CONFANNMON 
 * parrotchannel: conf, 0, CONF_NORMAL
 * telechannel: teleconf, CONF_CONF | CONF_TALKER | CONF_LISTENER
 * btelechannel txconf, (connect between telemetry conference and main TX audio conference)
 * voxchannel: conf, CONF_MONITOR
 * txpchannel: txconf, CONF | CONF_TALKER
 *
 * == Audio Routing ==
 * Loop every 20ms - if read from:
 * rpt()
 * rxchannel: Lots of stuff... -> link channels, [ -> rxq]
 * pchannel -> txpchannel
 * txchannel -> [wait for hangup]
 * dahditxchannel -> txchannel
 * link channels...
 * monchannel -> link channels
 * parrotchannel -> [parrotstream, wait for hangup]
 * voxchannel -> [translate, wait for hangup]
 * txpchannel -> [wait for hangup]
 * telechannel -> btelechannel
 * btelechannel -> [wait for hangup]
 *
 * rpt_exec()
 * chan -> txchannel, [ -> rxq]
 * rxchannel -> pchannel
 * telechannel -> btelechannel ~
 * btelechannel -> [wait for hangup] ~
 * pchannel -> chan
 * txchannel -> [wait for hangup]
 *
 * ~ = same as rpt()
 *
 * rpt_call() -> rpt_call_bridge_setup() [while callmode > 0]
 * voxchannel: conf, CONF_MONITOR
 * mychannel: conf, (pseudo)
 * genchannel: conf, (pseudo) - conf, speaker
 */

#if DAHDI_BRIDGING_AVAILABLE
#define join_dahdiconf(chan, ci) __join_dahdiconf(chan, ci, __FILE__, __LINE__, __PRETTY_FUNCTION__)

static int __join_dahdiconf(struct ast_channel *chan, struct dahdi_confinfo *ci, const char *file, int line, const char *function)
{
	ci->chan = 0;

	/* First put the channel on the conference in proper mode */
	if (ioctl(ast_channel_fd(chan, 0), DAHDI_SETCONF, ci) == -1) {
		ast_log(LOG_WARNING, "%s:%d (%s) Unable to set conference mode on %s\n", file, line, function, ast_channel_name(chan));
		return -1;
	}
	return 0;
}

static int dahdi_conf_create(struct ast_channel *chan, int *confno, int mode)
{
	int res;
	struct dahdi_confinfo ci;	/* conference info */

	ci.confno = -1;
	ci.confmode = mode;

	res = join_dahdiconf(chan, &ci);
	if (res) {
		ast_log(LOG_WARNING, "Failed to join DAHDI conf (mode: %d)\n", mode);
	} else {
		*confno = ci.confno;
	}
	return res;
}

static int dahdi_conf_add(struct ast_channel *chan, int confno, int mode)
{
	int res;
	struct dahdi_confinfo ci;	/* conference info */

	ci.confno = confno;
	ci.confmode = mode;

	res = join_dahdiconf(chan, &ci);
	if (res) {
		ast_log(LOG_WARNING, "Failed to join DAHDI conf (mode: %d)\n", mode);
	}
	return res;
}

#define RPT_DAHDI_FLAG(r, d) \
	if (rflags & r) { \
		dflags |= d; \
	}

static int dahdi_conf_flags(enum rpt_conf_flags rflags)
{
	int dflags = 0;

	RPT_DAHDI_FLAG(RPT_CONF_NORMAL, DAHDI_CONF_NORMAL);
	RPT_DAHDI_FLAG(RPT_CONF_MONITOR, DAHDI_CONF_MONITOR);
	RPT_DAHDI_FLAG(RPT_CONF_MONITORTX, DAHDI_CONF_MONITORTX);
	RPT_DAHDI_FLAG(RPT_CONF_CONF, DAHDI_CONF_CONF);
	RPT_DAHDI_FLAG(RPT_CONF_CONFANN, DAHDI_CONF_CONFANN);
	RPT_DAHDI_FLAG(RPT_CONF_CONFMON, DAHDI_CONF_CONFMON);
	RPT_DAHDI_FLAG(RPT_CONF_CONFANNMON, DAHDI_CONF_CONFANNMON);
	RPT_DAHDI_FLAG(RPT_CONF_LISTENER, DAHDI_CONF_LISTENER);
	RPT_DAHDI_FLAG(RPT_CONF_TALKER, DAHDI_CONF_TALKER);

	return dflags;
}

static int *dahdi_confno(struct rpt *myrpt, enum rpt_conf_type type)
{
	switch (type) {
	case RPT_CONF:
		return &myrpt->rptconf.dahdiconf.conf;
	case RPT_TXCONF:
		return &myrpt->rptconf.dahdiconf.txconf;
	case RPT_TELECONF:
		return &myrpt->rptconf.dahdiconf.teleconf;
	}
	ast_assert(0);
	return NULL;
}

/*!
 * \brief Get the conference number of a DAHDI channel
 * \param chan DAHDI channel
 * \retval -1 on failure, conference number on success
 */
static int dahdi_conf_fd_confno(struct ast_channel *chan)
{
	struct dahdi_confinfo ci;

	if (ioctl(ast_channel_fd(chan, 0), DAHDI_CHANNO, &ci.confno) == -1) {
		ast_log(LOG_WARNING, "DAHDI_CHANNO failed: %s\n", strerror(errno));
		return -1;
	}

	return ci.confno;
}
#endif /* DAHDI_BRIDGING_AVAILABLE */

#if ASTERISK_BRIDGING_AVAILABLE
static struct ast_bridge **rpt_astconf(struct rpt *myrpt, enum rpt_conf_type type)
{
	switch (type) {
	case RPT_CONF:
		return &myrpt->rptconf.astconf.conf;
	case RPT_TXCONF:
		return &myrpt->rptconf.astconf.txconf;
	case RPT_TELECONF:
		return &myrpt->rptconf.astconf.teleconf;
		break;
	}
	ast_assert(0);
	return NULL;
}

/* from dahdi/user.h DAHDI_CONF_MODE_MASK */
#define RPT_CONF_MODE_MASK		0xFF		/* mask for modes */

static char *build_modes_flags(enum rpt_conf_flags rflags, char *buf)
{
	int flags;

	/* Max 8 + 10 + 5 + 8 + 8 + 11 + 9 + 7 + 1 for NUL = 67 */
#define MAX_MODE_FLAGS_LENGTH 68

	char *pos = buf;
	*pos = '\0';

	flags = rflags & RPT_CONF_MODE_MASK;

	switch (flags) {
	case RPT_CONF_MONITOR:
		pos += sprintf(pos, "|MONITOR");
		break;
	case RPT_CONF_MONITORTX:
		pos += sprintf(pos, "|MONITORTX");
		break;
	case RPT_CONF_CONF:
		pos += sprintf(pos, "|CONF");
		break;
	case RPT_CONF_CONFANN:
		pos += sprintf(pos, "|CONFANN");
		break;
	case RPT_CONF_CONFMON:
		pos += sprintf(pos, "|CONFMON");
		break;
	case RPT_CONF_CONFANNMON:
		pos += sprintf(pos, "|CONFANNMON");
		break;
	default:
		ast_assert(0);
	}

	/* And maybe one or both of these: */
	if (rflags & RPT_CONF_LISTENER) {
		pos += sprintf(pos, "|LISTENER");
	}
	if (rflags & RPT_CONF_TALKER) {
		pos += sprintf(pos, "|TALKER");
	}
	return pos > buf ? buf + 1 : buf;
}

static int rpt_ast_conf_join(struct ast_channel *chan, struct ast_bridge *bridge, enum rpt_conf_flags rflags)
{
	char flagstrbuf[MAX_MODE_FLAGS_LENGTH];
	char *flagstr;
	struct ast_bridge_features *features;

	/* Based on flags, mute accordingly. Assume no audio in either direction to start. */
	int flags;
	int mutetx = 1, muterx = 1;

	features = ast_bridge_features_new();
	if (!features) {
		return -1;
	}

	flags = rflags & RPT_CONF_MODE_MASK;
	flagstr = build_modes_flags(rflags, flagstrbuf);

	/* The RPT_CONF_ flags map to DAHDI_CONF_ flags, defined in dahdi/user.h.
	 * These are actually used in dahdi-base.c. in DAHDI Linux.
	 *
	 * This is a simplified explanation of how these flags are used:
	 *
	 * The DAHDI conference loop (_process_masterspan) calls
	 * __dahdi_receive_chunk for all spans (putbuf), and then
	 * __dahdi_transmit_chunk for all pseudos/conferenced channel receives (getbuf)
	 *
	 * pseudo_rx_audio for all pseudos/conferenced channel transmits (putbuf)
	 *		__dahdi_receive_chunk -> __dahdi_putbuf_chunk -> __putbuf_chunk
	 * __dahdi_transmit_chunk for all spans (getbuf)
	 *
	 * Receive: putbuf is what fills the buffer that is copied to userspace via dahdi_chan_read
	 * Transmit: getbuf is what drains the buffer filled from userspace via dahdi_chan_write
	 *
	 * Call paths (_dahdi_receive and _dahdi_transmit are public APIs called by card drivers).
	 * _dahdi_receive -> __dahdi_real_receive -> __dahdi_receive_chunk -> __dahdi_process_putaudio_chunk
	 * _dahdi_transmit -> __dahdi_real_transmit -> __dahdi_transmit_chunk -> __dahdi_process_getaudio_chunk
	 * For pseudos, e.g. __dahdi_receive_chunk is called directly (from pseudo_rx_audio)
	 *
	 *    Userspace |   Kernel
	 *
	 *     <--    read   <-- dahdi_chan_read  <== putbuf
	 *     -->    write  --> dahdi_chan_write ==> getbuf
	 *
	 * === Modes/Flags ===
	 * __dahdi_process_putaudio_chunk (similar for getaudio)
	 *
	 * DAHDI_CONF_NORMAL: normal, do nothing
	 *	putlin = putlin
	 * DAHDI_CONF_MONITOR (pseudo only):
	 *	putlin = conf chan RX audio
	 * DAHDI_CONF_MONITORTX (pseudo only):
	 *	putlin = conf chan TX audio
	 * DAHDI_CONF_MONITORBOTH (pseudo only):
	 *  putlin = conf chan RX + TX audio
	 * DAHDI_CONF_REALANDPSEUDO:
	 *	DAHDI_CONF_TALKER
	 *		conflast = putlin + conf_sums_next
	 *		conflast -= conf_sums_next
	 *		conf_sums_next += conflast
	 *  else, conflast = zeroed out buffer
	 *	DAHDI_CONF_PSEUDO_LISTENER:
	 *		putlin -= conflast2 (subtract out previous last sample written)
	 *		putlin += conf_sums (add in conference)
	 * 	else, putlin = zeroed out buffer
	 * DAHDI_CONF_CONF:
	 *	DAHDI_CONF_LISTENER
	 *		putlin -= conflast (subtract out last sample written)
	 *		putlin += conf_sums (add in conference)
	 *	fallthrough
	 * DAHDI_CONF_CONFANN:
	 *	DAHDI_CONF_TALKER (same as DAHDI_CONF_REALANDPSEUDO)
	 * DAHDI_CONF_CONFMON
	 * DAHDI_CONF_CONFANNMON
	 *	DAHDI_CONF_LISTENER (getaudio) - subtract last sample to conf, add conf
	 *	DAHDI_CONF_TALKER (same as DAHDI_CONF_REALANDPSEUDO, but also subtract last value)
	 *
	 * TL;DR...
	 * modes:
	 * CONF_ANN = allowed to transmit
	 * CONF_MON = allowed to monitor
	 * CONF_ANNMON = allowed to transmit and monitor
	 * CONF_MONITOR = monitor conference audio (~CONF_MON?)
	 * flags:
	 * CONF_TALKER = mix in our audio
	 * CONF_LISTENER = allow receiving audio
	 */

	/*! \todo The below is just a rough guess of how they should translate,
	 * some are probably wrong and need to be fixed */

	switch (flags) {
	case RPT_CONF_MONITOR: /* Monitor rx of other chan */
		mutetx = 1;
		muterx = 0;
		break;
	case RPT_CONF_CONF:
		break;
	case RPT_CONF_CONFANN:
		mutetx = 0;
		muterx = 1;
		break;
	case RPT_CONF_CONFMON:
		muterx = 0;
		mutetx = 1;
		break;
	case RPT_CONF_CONFANNMON:
		muterx = 0;
		mutetx = 0;
		break;
	case RPT_CONF_MONITORTX: /* Monitor tx of other chan */
		/* Not used */
		/* Fall through */
	default:
		ast_assert(0);
	}

	/* And maybe one or both of these... */
	if (rflags & RPT_CONF_LISTENER) {
		muterx = 0;
	}
	if (rflags & RPT_CONF_TALKER) {
		mutetx = 0;
	}

	ast_channel_lock(chan);
	/* XXX Might also need to block other (non-voice) frames? */
	if (muterx) {
		ast_channel_suppress(chan, AST_MUTE_DIRECTION_READ, AST_FRAME_VOICE);
	}
	ast_channel_unlock(chan);

	if (mutetx) {
		features->mute = 1;
	}

	ast_debug(2, "Adding %p (%s) to bridge %p, TX: %d, RX: %d (%s)\n", chan, ast_channel_name(chan), bridge, !mutetx, !muterx, flagstr);
	return pseudo_channel_push(chan, bridge, features);
}
#endif /* ASTERISK_BRIDGING_AVAILABLE */

int __rpt_conf_create(struct ast_channel *chan, struct rpt *myrpt, enum rpt_conf_type type, enum rpt_conf_flags flags, const char *file, int line)
{
#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		int *confno, dflags;
		/* Convert RPT conf flags to DAHDI conf flags... for now. */
		dflags = dahdi_conf_flags(flags);
		confno = dahdi_confno(myrpt, type);
		if (dahdi_conf_create(chan, confno, dflags)) {
			ast_log(LOG_ERROR, "%s:%d: Failed to create conference using chan type %d\n", file, line, type);
			return -1;
		}
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
#if DAHDI_BRIDGING_AVAILABLE
	else
#endif /* DAHDI_BRIDGING_AVAILABLE */
	{
		struct ast_bridge **astconf = rpt_astconf(myrpt, type);
		if (!*astconf) {
			char bridge_name[128];
			snprintf(bridge_name, sizeof(bridge_name), "%s-%s", myrpt->name, rpt_conf_type_str(type));
			*astconf = rpt_pseudo_bridge(bridge_name);
		}
		if (!*astconf) {
			ast_log(LOG_ERROR, "Couldn't find bridge type %d\n", type);
			return -1;
		}
		/* Add channel to conference, using flags. */
		if (rpt_ast_conf_join(chan, *astconf, flags)) {
			ast_log(LOG_ERROR, "%s couldn't join bridge %p\n", ast_channel_name(chan), *astconf);
			return -1;
		}
	}
#endif /* ASTERISK_BRIDGING_AVAILABLE */
	return 0;
}

#if ASTERISK_BRIDGING_AVAILABLE
static void destroy_bridge_if_exists(struct rpt *myrpt, enum rpt_conf_type type)
{
	struct ast_bridge **bridge;
	bridge = rpt_astconf(myrpt, type);
	if (*bridge) {
		rpt_pseudo_bridge_unref(*bridge);
		*bridge = NULL;
	}
}
#endif /* ASTERISK_BRIDGING_AVAILABLE */

void rpt_bridge_cleanup(struct rpt *myrpt)
{
#if ASTERISK_BRIDGING_AVAILABLE
	if (!use_dahdi_bridging) {
		destroy_bridge_if_exists(myrpt, RPT_CONF);
		destroy_bridge_if_exists(myrpt, RPT_TXCONF);
		destroy_bridge_if_exists(myrpt, RPT_TELECONF);
	}
#endif /* ASTERISK_BRIDGING_AVAILABLE */
}

int rpt_equate_tx_conf(struct rpt *myrpt)
{
#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		/* save pseudo channel conference number */
		myrpt->rptconf.dahdiconf.conf = myrpt->rptconf.dahdiconf.txconf;
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
#if DAHDI_BRIDGING_AVAILABLE
	else
#endif /* DAHDI_BRIDGING_AVAILABLE */
	{
		//myrpt->rptconf.astconf.conf = myrpt->rptconf.astconf.txconf;
	}
#endif /* ASTERISK_BRIDGING_AVAILABLE */
	return 0;
}

int __rpt_conf_add(struct ast_channel *chan, struct rpt *myrpt, enum rpt_conf_type type, enum rpt_conf_flags flags, const char *file, int line)
{
#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		/* Convert RPT conf flags to DAHDI conf flags... for now. */
		int *confno, dflags;

		dflags = dahdi_conf_flags(flags);
		confno = dahdi_confno(myrpt, type);

		if (dahdi_conf_add(chan, *confno, dflags)) {
			ast_log(LOG_ERROR, "%s:%d: Failed to add to conference using chan type %d\n", file, line, type);
			return -1;
		}
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
#if DAHDI_BRIDGING_AVAILABLE
	else
#endif /* DAHDI_BRIDGING_AVAILABLE */
	{
		struct ast_bridge **astconf = rpt_astconf(myrpt, type);
		if (!*astconf) {
			ast_log(LOG_ERROR, "Couldn't find bridge type %d\n", type);
			return -1;
		}
		/* Add channel to conference, using flags. */
		if (rpt_ast_conf_join(chan, *astconf, flags)) {
			ast_log(LOG_ERROR, "%s couldn't join bridge %p\n", ast_channel_name(chan), *astconf);
			return -1;
		}
	}
#endif /* ASTERISK_BRIDGING_AVAILABLE */
	return 0;
}

int rpt_call_bridge_setup(struct rpt *myrpt, struct ast_channel *mychannel, struct ast_channel *genchannel)
{
	int res;

	/* first put the channel on the conference in announce mode */
	if (myrpt->p.duplex == 2) {
		res = rpt_conf_add_announcer_monitor(myrpt->pchannel, myrpt);
	} else {
		res = rpt_conf_add_speaker(myrpt->pchannel, myrpt);
	}
	if (res) {
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		return -1;
	}

#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		/* get its channel number */
		res = dahdi_conf_fd_confno(mychannel);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to get autopatch channel number\n");
			ast_hangup(mychannel);
			return -1;
		}

		/* put vox channel monitoring on the channel  */
		if (dahdi_conf_add(myrpt->voxchannel, res, DAHDI_CONF_MONITOR)) {
			ast_hangup(mychannel);
			return -1;
		}
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
#if DAHDI_BRIDGING_AVAILABLE
	else
#endif /* DAHDI_BRIDGING_AVAILABLE */
	{
		struct ast_bridge *bridge = ast_channel_get_bridge(mychannel);
		if (!bridge) {
			ast_log(LOG_WARNING, "Unable to get autopatch channel bridge\n");
			ast_hangup(mychannel);
			return -1;
		}

		/* put vox channel monitoring on the channel  */
		if (rpt_ast_conf_join(myrpt->voxchannel, bridge, RPT_CONF_MONITOR)) {
			ast_hangup(mychannel);
			return -1;
		}
	}
#endif /* ASTERISK_BRIDGING_AVAILABLE */
	return 0;
}

int rpt_mon_setup(struct rpt *myrpt)
{
	int res;

#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		if (!IS_PSEUDO(myrpt->txchannel) && myrpt->dahditxchannel == myrpt->txchannel) {
			int confno = dahdi_conf_fd_confno(myrpt->txchannel); /* get tx channel's port number */
			if (confno < 0) {
				return -1;
			}
			res = dahdi_conf_add(myrpt->monchannel, confno, DAHDI_CONF_MONITORTX);
		} else {
			/* first put the channel on the conference in announce mode */
			res = rpt_conf_add(myrpt->monchannel, myrpt, RPT_TXCONF, RPT_CONF_CONFANNMON);
		}
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
#if DAHDI_BRIDGING_AVAILABLE
	else
#endif /* DAHDI_BRIDGING_AVAILABLE */
	{
		if (!IS_PSEUDO(myrpt->txchannel) && myrpt->dahditxchannel == myrpt->txchannel) {
			struct ast_bridge *bridge = ast_channel_get_bridge(myrpt->txchannel);
			if (!bridge) {
				return -1;
			}
			res = rpt_ast_conf_join(myrpt->monchannel, bridge, RPT_CONF_MONITORTX);
		} else {
			/* first put the channel on the conference in announce mode */
			res = rpt_conf_add(myrpt->monchannel, myrpt, RPT_TXCONF, RPT_CONF_CONFANNMON);
		}
	}
#endif /* ASTERISK_BRIDGING_AVAILABLE */
	return res;
}

int rpt_parrot_add(struct rpt *myrpt)
{
#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		/* first put the channel on the conference in announce mode */
		if (dahdi_conf_add(myrpt->parrotchannel, 0, DAHDI_CONF_NORMAL)) {
			return -1;
		}
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
#if DAHDI_BRIDGING_AVAILABLE
	else
#endif /* DAHDI_BRIDGING_AVAILABLE */
	{
		struct ast_bridge *bridge = NULL;
		/*! \todo BUGBUG FIXME What would be the equivalent of "conference 0" with DAHDI???? */
		if (!bridge) {
			ast_log(LOG_ERROR, "What bridge should %s use???\n", ast_channel_name(myrpt->monchannel));
			return -1;
		}
		if (rpt_ast_conf_join(myrpt->monchannel, bridge, RPT_CONF_NORMAL)) {
			return -1;
		}
	}
#endif /* ASTERISK_BRIDGING_AVAILABLE */
	return 0;
}

#if DAHDI_BRIDGING_AVAILABLE
static int dahdi_conf_get_muted(struct ast_channel *chan)
{
	int muted;
	if (ioctl(ast_channel_fd(chan, 0), DAHDI_GETCONFMUTE, &muted) == -1) {
		ast_log(LOG_WARNING, "Couldn't get mute status on %s: %s\n", ast_channel_name(chan), strerror(errno));
		muted = 0;
	}
	return muted;
}
#endif /* DAHDI_BRIDGING_AVAILABLE */

int rpt_conf_get_muted(struct ast_channel *chan, struct rpt *myrpt)
{
#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		return dahdi_conf_get_muted(chan);
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
#if DAHDI_BRIDGING_AVAILABLE
	else
#endif /* DAHDI_BRIDGING_AVAILABLE */
	{
		int res = -1;
		struct ast_bridge_channel *bchan;

		ast_channel_lock(chan);
		bchan = ast_channel_get_bridge_channel(chan);
		if (bchan) {
			ast_bridge_channel_lock(bchan);
			if (bchan->features) {
				res = bchan->features->mute;
			}
			ast_bridge_channel_unlock(bchan);
			ao2_cleanup(bchan);
		}
		ast_channel_unlock(chan);
		return res;
	}
#endif /* ASTERISK_BRIDGING_AVAILABLE */
}

/*!
 * \param chan
 * \param tone 0 = congestion, 1 = dialtone
 */
static int rpt_play_tone(struct ast_channel *chan, int tone)
{
#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		tone = tone ? DAHDI_TONE_DIALTONE : DAHDI_TONE_CONGESTION;
		if (tone_zone_play_tone(ast_channel_fd(chan, 0), tone)) {
			ast_log(LOG_WARNING, "Cannot start tone on %s\n", ast_channel_name(chan));
			return -1;
		}
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
#if DAHDI_BRIDGING_AVAILABLE
	else
#endif /* DAHDI_BRIDGING_AVAILABLE */
	{
		int res;
		struct ast_tone_zone_sound *ts;
		const char *tz = tone ? "dial" : "congestion";
		if (tone == -1) {
			ast_playtones_stop(chan);
			return 0;
		}
		/* Require indications.conf definition */
		ts = ast_get_indication_tone(ast_channel_zone(chan), tz);
		if (!ts) {
			ast_log(LOG_ERROR, "Tonezone %s not defined for channel %s\n", tz, ast_channel_name(chan));
			return -1;
		}
		res = ast_playtones_start(chan, 0, ts->data, 0);
		ast_tone_zone_sound_unref(ts);
		return res;
	}
#endif /* ASTERISK_BRIDGING_AVAILABLE */
	return 0;
}

int rpt_play_dialtone(struct ast_channel *chan)
{
	return rpt_play_tone(chan, 1);
}

int rpt_play_congestion(struct ast_channel *chan)
{
	return rpt_play_tone(chan, 0);
}

int rpt_stop_tone(struct ast_channel *chan)
{
	return rpt_play_tone(chan, -1);
}

int rpt_set_tone_zone(struct ast_channel *chan, const char *tz)
{
#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		if (tone_zone_set_zone(ast_channel_fd(chan, 0), (char*) tz) == -1) {
			ast_log(LOG_WARNING, "Unable to set tone zone %s on %s\n", tz, ast_channel_name(chan));
			return -1;
		}
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
#if DAHDI_BRIDGING_AVAILABLE
	else
#endif /* DAHDI_BRIDGING_AVAILABLE */
	{
		struct ast_tone_zone *new_zone;
		if (!(new_zone = ast_get_indication_zone(tz))) {
			ast_log(LOG_ERROR, "Unknown country code '%s' for tonezone. Check indications.conf for available country codes.\n", tz);
			return -1;
		}
		ast_channel_lock(chan);
		if (ast_channel_zone(chan)) {
			ast_channel_zone_set(chan, ast_tone_zone_unref(ast_channel_zone(chan)));
		}
		ast_channel_zone_set(chan, ast_tone_zone_ref(new_zone));
		ast_channel_unlock(chan);
		new_zone = ast_tone_zone_unref(new_zone);
	}
#endif /* ASTERISK_BRIDGING_AVAILABLE */
	return 0;
}

int dahdi_write_wait(struct ast_channel *chan)
{
#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		int res, i, flags;

		for (i = 0; i < 20; i++) {
			flags = DAHDI_IOMUX_WRITEEMPTY | DAHDI_IOMUX_NOWAIT;
			res = ioctl(ast_channel_fd(chan, 0), DAHDI_IOMUX, &flags);
			if (res) {
				ast_log(LOG_WARNING, "DAHDI_IOMUX failed on %s: %s\n", ast_channel_name(chan), strerror(errno));
				break;
			}
			if (flags & DAHDI_IOMUX_WRITEEMPTY) {
				break;
			}
			if (ast_safe_sleep(chan, 50)) {
				res = -1;
				break;
			}
		}
		return res;
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
	ast_debug(1, "Skipping wait on %s\n", ast_channel_name(chan));
	return 0;
#endif /* ASTERISK_BRIDGING_AVAILABLE */
}

int dahdi_flush(struct ast_channel *chan)
{
#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		int i = DAHDI_FLUSH_EVENT;
		if (ioctl(ast_channel_fd(chan, 0), DAHDI_FLUSH, &i) == -1) {
			ast_log(LOG_ERROR, "Can't flush events on %s: %s", ast_channel_name(chan), strerror(errno));
			return -1;
		}
		return 0;
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
	ast_log(LOG_WARNING, "Skipping flush of %s\n", ast_channel_name(chan));
	return 0;
#endif /* ASTERISK_BRIDGING_AVAILABLE */
}

int dahdi_bump_buffers(struct ast_channel *chan, int samples)
{
#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		struct dahdi_bufferinfo bi;

		/* This is a miserable kludge. For some unknown reason, which I dont have
		   time to properly research, buffer settings do not get applied to dahdi
		   pseudo-channels. So, if we have a need to fit more then 1 160 sample
		   buffer into the psuedo-channel at a time, and there currently is not
		   room, it increases the number of buffers to accommodate the larger number
		   of samples (version 0.257 9/3/10) */
		memset(&bi, 0, sizeof(bi));

		if (ioctl(ast_channel_fd(chan, 0), DAHDI_GET_BUFINFO, &bi) == -1) {
			ast_log(LOG_ERROR, "Failed to get buffer info on %s: %s\n", ast_channel_name(chan), strerror(errno));
			return -1;
		}
		if (samples > bi.bufsize && (bi.numbufs < ((samples / bi.bufsize) + 1))) {
			bi.numbufs = (samples / bi.bufsize) + 1;
			if (ioctl(ast_channel_fd(chan, 0), DAHDI_SET_BUFINFO, &bi)) {
				ast_log(LOG_ERROR, "Failed to set buffer info on %s: %s\n", ast_channel_name(chan), strerror(errno));
				return -1;
			}
		}
		return 0;
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
	/* XXX Not yet sure if this is problematic or not */
	ast_debug(4, "Skipping buffer increase on %s\n", ast_channel_name(chan));
	return 0;
#endif /* ASTERISK_BRIDGING_AVAILABLE */
}

int dahdi_rx_offhook(struct ast_channel *chan)
{
#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		struct dahdi_params par;
		if (ioctl(ast_channel_fd(chan, 0), DAHDI_GET_PARAMS, &par) == -1) {
			ast_log(LOG_ERROR, "Can't get params on %s: %s", ast_channel_name(chan), strerror(errno));
			return -1;
		}
		return par.rxisoffhook;
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
	ast_log(LOG_WARNING, "Don't know if %s is off hook...\n", ast_channel_name(chan));
	return 0;
#endif /* ASTERISK_BRIDGING_AVAILABLE */
}

int dahdi_set_hook(struct ast_channel *chan, int offhook)
{
#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		int i = DAHDI_OFFHOOK;
		if (ioctl(ast_channel_fd(chan, 0), DAHDI_HOOK, &i) == -1) {
			ast_log(LOG_ERROR, "Can't set hook on %s: %s\n", ast_channel_name(chan), strerror(errno));
			return -1;
		}
		return 0;
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
	ast_log(LOG_WARNING, "Don't know how to set hook on %s...\n", ast_channel_name(chan));
	return 0;
#endif /* ASTERISK_BRIDGING_AVAILABLE */
}

int dahdi_set_echocancel(struct ast_channel *chan, int ec)
{
#if DAHDI_BRIDGING_AVAILABLE
	if (use_dahdi_bridging) {
		if (ioctl(ast_channel_fd(chan, 0), DAHDI_ECHOCANCEL, &ec)) {
			ast_log(LOG_ERROR, "Can't set echocancel on %s: %s\n", ast_channel_name(chan), strerror(errno));
			return -1;
		}
		return 0;
	}
#endif /* DAHDI_BRIDGING_AVAILABLE */
#if ASTERISK_BRIDGING_AVAILABLE
	ast_log(LOG_WARNING, "Don't know how to set echo cancel on %s...\n", ast_channel_name(chan));
	return 0;
#endif /* ASTERISK_BRIDGING_AVAILABLE */
}
