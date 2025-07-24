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
 */

#include "asterisk.h"

#include <dahdi/user.h>
#include <dahdi/tonezone.h>		/* use tone_zone_set_zone */

#include "asterisk/channel.h"
#include "asterisk/indications.h"
#include "asterisk/format_cache.h" /* use ast_format_slin */

#include "app_rpt.h"

#include "rpt_bridging.h"
#include "rpt_call.h"

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
	struct ast_channel **chanptr = rpt_chan_channel(myrpt, NULL, chantype);

	if (!*chanptr) {
		ast_log(LOG_WARNING, "No %s channel to hang up\n", rpt_chan_type_str(chantype));
		return;
	}

	/* If RXCHAN == TXCHAN, and we hang up one, also NULL out the other one */

	switch (chantype) {
	case RPT_RXCHAN:
		if (myrpt->txchannel && myrpt->txchannel == *chanptr) {
			ast_debug(2, "Also resetting txchannel\n");
			myrpt->txchannel = NULL;
		}
		break;
	case RPT_TXCHAN:
		if (myrpt->rxchannel && myrpt->rxchannel == *chanptr) {
			ast_debug(2, "Also resetting rxchannel\n");
			myrpt->rxchannel = NULL;
		}
		break;
	default:
		break;
	}

	ast_debug(2, "Hanging up channel %s\n", ast_channel_name(*chanptr));
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
	 * This might not be necessary, but if it is, this should be re-added. */

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

struct ast_channel *rpt_request_pseudo_chan(struct ast_format_cap *cap)
{
	struct ast_channel *chan = ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
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
	struct ast_channel *chan, **chanptr;

	if (flags & RPT_LINK_CHAN) {
		link = data;
	} else {
		myrpt = data;
	}

	chan = ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
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
	
	ast_debug(2, "Channel %s joining conference %i", ast_channel_name(chan), confno);

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
	}
	ast_assert(0);
	return NULL;
}

/*!
 * \brief Get the channel number of a DAHDI channel
 * \param chan DAHDI channel
 * \retval -1 on failure, conference number on success
 */
static int dahdi_conf_get_channo(struct ast_channel *chan)
{
	struct dahdi_confinfo ci = {0};

	if (ioctl(ast_channel_fd(chan, 0), DAHDI_CHANNO, &ci.chan)) {
		ast_log(LOG_WARNING, "DAHDI_CHANNO failed: %s\n", strerror(errno));
		return -1;
	}

	return ci.chan;
}

int __rpt_conf_create(struct ast_channel *chan, struct rpt *myrpt, enum rpt_conf_type type, enum rpt_conf_flags flags, const char *file, int line)
{
	int *confno, dflags;
	/* Convert RPT conf flags to DAHDI conf flags... for now. */
	dflags = dahdi_conf_flags(flags);
	confno = dahdi_confno(myrpt, type);
	if (dahdi_conf_create(chan, confno, dflags)) {
		ast_log(LOG_ERROR, "%s:%d: Failed to create conference using chan type %d\n", file, line, type);
		return -1;
	}
	return 0;
}

int rpt_equate_tx_conf(struct rpt *myrpt)
{
	/* save pseudo channel conference number */
	myrpt->rptconf.dahdiconf.conf = myrpt->rptconf.dahdiconf.txconf;
	return 0;
}

int __rpt_conf_add(struct ast_channel *chan, struct rpt *myrpt, enum rpt_conf_type type, enum rpt_conf_flags flags, const char *file, int line)
{
	/* Convert RPT conf flags to DAHDI conf flags... for now. */
	int *confno, dflags;

	dflags = dahdi_conf_flags(flags);
	confno = dahdi_confno(myrpt, type);

	if (dahdi_conf_add(chan, *confno, dflags)) {
		ast_log(LOG_ERROR, "%s:%d: Failed to add to conference using chan type %d\n", file, line, type);
		return -1;
	}
	return 0;
}

int rpt_call_bridge_setup(struct rpt *myrpt, struct ast_channel *mychannel)
{
	int res;

	/* Put pchannel back on the conference in speaker mode */
	if (myrpt->p.duplex == 4 || myrpt->p.duplex == 3) {
		if (rpt_conf_add_speaker(myrpt->pchannel, myrpt)) {
			return -1;
		}
	}

	/* get its conference number */
	res = dahdi_conf_get_channo(mychannel);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to get autopatch channel number\n");
		/* Put pchannel back on the conference in announce mode */
		if (myrpt->p.duplex == 4 || myrpt->p.duplex == 3) {
			rpt_conf_add_announcer_monitor(myrpt->pchannel, myrpt);
		}
		return -1;
	}

	/* put vox channel monitoring on the channel  
	 *
	 * This uses the internal DAHDI channel number to create the 
	 * monitor conference.  This code will hang here when trying to 
	 * join the conference when the underlying version of DAHDI in use
	 * is missing a patch that allows the DAHDI_CONF_MONITOR option
	 * to monitor a pseudo channel.  This patch prevents the hardware
	 * pre-echo routines from acting on a pseudo channel.  It also
	 * prevents the DAHDI check conference routine from acting
	 * on a channel number being used as a conference.
	 */
	if (dahdi_conf_add(myrpt->voxchannel, res, DAHDI_CONF_MONITOR)) {
		/* Put pchannel back on the conference in announce mode */
		if (myrpt->p.duplex == 4 || myrpt->p.duplex == 3) {
			rpt_conf_add_announcer_monitor(myrpt->pchannel, myrpt);
		}
		return -1;
	}
	return 0;
}

int rpt_mon_setup(struct rpt *myrpt)
{
	int res;

	if (!IS_PSEUDO(myrpt->txchannel) && myrpt->dahditxchannel == myrpt->txchannel) {
		int confno = dahdi_conf_get_channo(myrpt->txchannel); /* get tx channel's port number */
		if (confno < 0) {
			return -1;
		}
		res = dahdi_conf_add(myrpt->monchannel, confno, DAHDI_CONF_MONITORTX);
	} else {
		/* first put the channel on the conference in announce mode */
		res = rpt_conf_add(myrpt->monchannel, myrpt, RPT_TXCONF, RPT_CONF_CONFANNMON);
	}
	return res;
}

int rpt_parrot_add(struct rpt *myrpt)
{
	/* first put the channel on the conference in announce mode */
	if (dahdi_conf_add(myrpt->parrotchannel, 0, DAHDI_CONF_NORMAL)) {
		return -1;
	}
	return 0;
}

static int dahdi_conf_get_muted(struct ast_channel *chan)
{
	int muted;

	if (!CHAN_TECH(chan, "DAHDI")) {
		return 0;
	}

	if (ioctl(ast_channel_fd(chan, 0), DAHDI_GETCONFMUTE, &muted) == -1) {
		ast_log(LOG_WARNING, "Couldn't get mute status on %s: %s\n", ast_channel_name(chan), strerror(errno));
		muted = 0;
	}
	return muted;
}

int rpt_conf_get_muted(struct ast_channel *chan, struct rpt *myrpt)
{
	return dahdi_conf_get_muted(chan);
}

/*!
 * \param chan Channel to play tone on
 * \param tone tone type (e.g., "dial", "congestion")
 * \retval 0 on success, -1 on failure
 */
int rpt_play_tone(struct ast_channel *chan, const char *tone)
{
	struct ast_tone_zone_sound *ts;
	int res = 0;
	ts = ast_get_indication_tone(ast_channel_zone(chan), tone);
	if (ts) {
		res = ast_playtones_start(chan, 0, ts->data, 0);
		ts = ast_tone_zone_sound_unref(ts);
	} else {
		ast_log(LOG_WARNING, "No tone '%s' found in zone '%s'\n", tone, ast_channel_zone(chan)->country);
		return -1;
	}

	if (res) {
		ast_log(LOG_WARNING, "Cannot start tone on %s\n", ast_channel_name(chan));
		return -1;
	}
	return 0;
}

/*! \brief Stop playing tones on a channel */
int rpt_stop_tone(struct ast_channel *chan)
{
	ast_playtones_stop(chan);
	return 0;
}

/*! \brief Set the tone zone for a channel.
 *! \note Based on Asterisk func_channel.c
 */
int rpt_set_tone_zone(struct ast_channel *chan, const char *tz)
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
	return 0;
}

int dahdi_write_wait(struct ast_channel *chan)
{
	int res, i, flags;

	for (i = 0; i < 20; i++) {
		flags = DAHDI_IOMUX_WRITEEMPTY | DAHDI_IOMUX_NOWAIT;
		res = ioctl(ast_channel_fd(chan, 0), DAHDI_IOMUX, &flags);
		if (res) {
			ast_log(LOG_WARNING, "DAHDI_IOMUX failed: %s\n", strerror(errno));
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

int dahdi_flush(struct ast_channel *chan)
{
	int i = DAHDI_FLUSH_EVENT;
	if (ioctl(ast_channel_fd(chan, 0), DAHDI_FLUSH, &i) == -1) {
		ast_log(LOG_ERROR, "Can't flush events on %s: %s", ast_channel_name(chan), strerror(errno));
		return -1;
	}
	return 0;
}

int dahdi_bump_buffers(struct ast_channel *chan, int samples)
{
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

int dahdi_rx_offhook(struct ast_channel *chan)
{
	struct dahdi_params par;
	if (ioctl(ast_channel_fd(chan, 0), DAHDI_GET_PARAMS, &par) == -1) {
		ast_log(LOG_ERROR, "Can't get params on %s: %s", ast_channel_name(chan), strerror(errno));
		return -1;
	}
	return par.rxisoffhook;
}

int dahdi_set_hook(struct ast_channel *chan, int offhook)
{
	if (ioctl(ast_channel_fd(chan, 0), DAHDI_HOOK, &offhook) == -1) {
		ast_log(LOG_ERROR, "Can't set hook on %s: %s\n", ast_channel_name(chan), strerror(errno));
		return -1;
	}
	return 0;
}

int dahdi_set_echocancel(struct ast_channel *chan, int ec)
{
	if (ioctl(ast_channel_fd(chan, 0), DAHDI_ECHOCANCEL, &ec)) {
		ast_log(LOG_ERROR, "Can't set echocancel on %s: %s\n", ast_channel_name(chan), strerror(errno));
		return -1;
	}
	return 0;
}
