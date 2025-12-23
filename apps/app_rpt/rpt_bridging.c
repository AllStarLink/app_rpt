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

#include "asterisk/bridge.h"
#include "asterisk/core_unreal.h"
#include "asterisk/channel.h"
#include "asterisk/indications.h"
#include "asterisk/format_cache.h" /* use ast_format_slin */
#include "asterisk/audiohook.h"

#include "app_rpt.h"

#include "rpt_bridging.h"
#include "rpt_call.h"

/*!
 *	\brief used to display "words" in debug messages.
 */
static const char *rpt_chan_type_str(enum rpt_chan_name chantype)
{
	switch (chantype) {
	case RPT_RXCHAN:
		return "rxchan";
	case RPT_TXCHAN:
		return "txchan";
	case RPT_PCHAN:
		return "pchan";
	case RPT_LOCALTXCHAN:
		return "localtxchan";
	case RPT_MONCHAN:
		return "monchan";
	case RPT_TXPCHAN:
		return "txpchan";
	}
	ast_assert(0);
	return NULL;
}

static const char *rpt_chan_name(struct rpt *myrpt, enum rpt_chan_name chantype)
{
	switch (chantype) {
	case RPT_RXCHAN:
		return myrpt->rxchanname;
	case RPT_TXCHAN:
		return myrpt->txchanname;
	case RPT_PCHAN:
	case RPT_LOCALTXCHAN:
	case RPT_MONCHAN:
	case RPT_TXPCHAN:
		return NULL;
	}
	ast_assert(0);
	return NULL;
}

static struct ast_channel **rpt_chan_channel(struct rpt *myrpt, struct rpt_link *link, enum rpt_chan_name chantype)
{
	if (myrpt) {
		switch (chantype) {
		case RPT_RXCHAN:
			return &myrpt->rxchannel;
		case RPT_TXCHAN:
			return &myrpt->txchannel;
		case RPT_PCHAN:
			return &myrpt->pchannel;
		case RPT_LOCALTXCHAN:
			return &myrpt->localtxchannel;
		case RPT_MONCHAN:
			return &myrpt->monchannel;
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

#define RPT_DIAL_DURATION 999

void rpt_hangup(struct rpt *myrpt, enum rpt_chan_name chantype)
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

static const char *rpt_chan_app(enum rpt_chan_name chantype, enum rpt_chan_flags flags)
{
	switch (chantype) {
	case RPT_RXCHAN:
		return flags & RPT_LINK_CHAN ? "Link Rx" : "Repeater Rx";
	case RPT_TXCHAN:
		return flags & RPT_LINK_CHAN ? "Link Tx" : "Repeater Tx";
	case RPT_PCHAN:
	case RPT_LOCALTXCHAN:
	case RPT_MONCHAN:
	case RPT_TXPCHAN:
		return NULL;
	}
	ast_assert(0);
	return NULL;
}

static const char *rpt_chan_app_data(enum rpt_chan_name chantype)
{
	switch (chantype) {
	case RPT_RXCHAN:
		return "Rx";
	case RPT_TXCHAN:
		return "Tx";
	case RPT_PCHAN:
	case RPT_LOCALTXCHAN:
	case RPT_MONCHAN:
	case RPT_TXPCHAN:
		return NULL;
	}
	ast_assert(0);
	return NULL;
}

int __rpt_request(void *data, struct ast_format_cap *cap, enum rpt_chan_name chantype, enum rpt_chan_flags flags)
{
	char chanstr[256];
	const char *channame;
	struct ast_channel *chan, **chanptr;
	char *tech, *device;
	struct rpt *myrpt = data;
	struct ast_unreal_pvt *p;

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

	if (!IS_LOCAL_NAME(tech)) {
		rpt_make_call(chan, device, RPT_DIAL_DURATION, tech, rpt_chan_app(chantype, flags), rpt_chan_app_data(chantype), myrpt->name);
		if (ast_channel_state(chan) != AST_STATE_UP) {
			ast_log(LOG_ERROR, "Requested channel %s not up?\n", ast_channel_name(chan));
			ast_hangup(chan);
			return -1;
		}
	} else {
		p = ast_channel_tech_pvt(chan);
		ast_answer(p->chan);
		ast_answer(p->owner);
	}
	chanptr = rpt_chan_channel(myrpt, NULL, chantype);
	*chanptr = chan;

	switch (chantype) {
	case RPT_RXCHAN:
		myrpt->localrxchannel = IS_LOCAL_NAME(tech) ? chan : NULL;
		break;
	case RPT_TXCHAN:
		myrpt->localtxchannel = IS_LOCAL_NAME(tech) ? chan : NULL;
		break;
	default:
		break;
	}

	return 0;
}

struct ast_channel *__rpt_request_local_chan(struct ast_format_cap *cap, const char *exten, enum rpt_chan_type type)
{
	struct ast_channel *chan;
	struct ast_unreal_pvt *p;
	char *type_str[3] = { "Local", "Announcer", "Recorder" };

	chan = ast_request(type_str[type], cap, NULL, NULL, exten, NULL);
	if (!chan) {
		ast_log(LOG_ERROR, "Failed to request local channel\n");
		return NULL;
	}

	ast_debug(1, "Requesting channel %s setup\n", ast_channel_name(chan));
	ast_set_read_format(chan, ast_format_slin);
	ast_set_write_format(chan, ast_format_slin);
	if (type == RPT_LOCAL) {
		/* Local channel needs to be answered.
		 * Announcer channels auto answer on creation.
		 */
		rpt_disable_cdr(chan);
		ast_debug(1, "Requested channel %s cdr disabled\n", ast_channel_name(chan));
		p = ast_channel_tech_pvt(chan);
		if (!p || !p->owner || !p->chan) {
			ast_log(LOG_WARNING, "Local channel %s missing endpoints\n", ast_channel_name(chan));
			ast_hangup(chan);
			return NULL;
		}
		ast_answer(p->owner);
		ast_answer(p->chan);
	}
	return chan;
}

int __rpt_request_local(void *data, struct ast_format_cap *cap, enum rpt_chan_name chantype, enum rpt_chan_flags flags, const char *exten)
{
	struct rpt *myrpt = NULL;
	struct rpt_link *link = NULL;
	struct ast_channel *chan, **chanptr;
	struct ast_unreal_pvt *p;

	if (flags & RPT_LINK_CHAN) {
		link = data;
	} else {
		myrpt = data;
	}
	if (chantype == RPT_MONCHAN) {
		chan = ast_request("Recorder", cap, NULL, NULL, exten, NULL);
	} else {
		chan = ast_request("Local", cap, NULL, NULL, exten, NULL);
	}
	if (!chan) {
		ast_log(LOG_ERROR, "Failed to request local channel\n");
		return -1;
	}
	rpt_disable_cdr(chan);
	chanptr = rpt_chan_channel(myrpt, link, chantype);
	*chanptr = chan;

	switch (chantype) {
	case RPT_MONCHAN:
		break; /* WE don't need to answer MONCHAN */
	case RPT_PCHAN:
		if (!(flags & RPT_LINK_CHAN)) {
			ast_assert(myrpt != NULL);
			if (!myrpt->localrxchannel) {
				myrpt->localrxchannel = chan;
			}
		} /* Don't break here we want the default logic for RPT_PCHAN */
	default:
		p = ast_channel_tech_pvt(chan);
		if (!p || !p->owner || !p->chan) {
			ast_log(LOG_WARNING, "Local channel %s missing endpoints\n", ast_channel_name(chan));
			ast_hangup(chan);
			return -1;
		}
		ast_answer(p->owner);
		ast_answer(p->chan);
		ast_debug(3, "Local channel p->owner %p, p->chan %p, chan %p\n", p->owner, p->chan, chan);
		ast_debug(3, "Channel states p->owner %d, p->chan %d, chan %d\n", ast_channel_state(p->owner), ast_channel_state(p->chan),
			ast_channel_state(chan));
	}

	return 0;
}

int __rpt_conf_create(struct rpt *myrpt, enum rpt_conf_type type, const char *file, int line)
{
	struct ast_bridge *conf = NULL, **confptr;
	char conference_name[RPT_CONF_NAME_SIZE] = "";
	switch (type) {
	case RPT_CONF:
		snprintf(conference_name, sizeof(conference_name), RPT_CONF_NAME);
		confptr = &myrpt->rptconf.conf;
		break;
	case RPT_TXCONF:
		snprintf(conference_name, sizeof(conference_name), RPT_TXCONF_NAME);
		confptr = &myrpt->rptconf.txconf;
		break;
	default:
		__builtin_unreachable();
		return -1;
	}
	ast_debug(3, "Setting up conference '%s' mixing bridge \n", conference_name);
	conf = ast_bridge_base_new(AST_BRIDGE_CAPABILITY_MULTIMIX,
		AST_BRIDGE_FLAG_MASQUERADE_ONLY | AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY, "app_rpt", conference_name, NULL);
	if (!conf) {
		ast_log(LOG_ERROR, "Conference '%s' mixing bridge could not be created.\n", conference_name);
		return -1;
	}

	*confptr = conf;
	return 0;
}

int __rpt_conf_add(struct ast_channel *chan, struct rpt *myrpt, enum rpt_conf_type type, const char *file, int line)
{
	struct ast_bridge *conf = NULL;
	char conference_name[RPT_CONF_NAME_SIZE] = "";

	switch (type) {
	case RPT_CONF:
		snprintf(conference_name, sizeof(conference_name), RPT_CONF_NAME);
		conf = myrpt->rptconf.conf;
		break;
	case RPT_TXCONF:
		snprintf(conference_name, sizeof(conference_name), RPT_TXCONF_NAME);
		conf = myrpt->rptconf.txconf;
		break;
	default:
		__builtin_unreachable();
		return -1;
	}
	if (!conf) {
		ast_log(LOG_ERROR, "Conference '%s' mixing bridge doesn't exist, can't add channel %s\n", conference_name, ast_channel_name(chan));
		return -1;
	}
	ast_debug(3, "Adding channel %s to conference '%s' mixing bridge \n", ast_channel_name(chan), conference_name);
	return ast_unreal_channel_push_to_bridge(chan, conf, AST_BRIDGE_CHANNEL_FLAG_IMMOVABLE);
}

int rpt_conf_get_muted(struct ast_channel *chan, struct rpt *myrpt)
{
	/*! \todo: Do we need to check mute? What should it do?*/
	return 0;
}

int rpt_play_tone(struct ast_channel *chan, const char *tone)
{
	int res = 0;
	struct ast_tone_zone *zone = ast_channel_zone(chan);
	struct ast_tone_zone_sound *ts = ast_get_indication_tone(zone, tone);
	if (ts) {
		res = ast_playtones_start(chan, 0, ts->data, 0);
		ts = ast_tone_zone_sound_unref(ts);
	} else {
		ast_log(LOG_WARNING, "No tone '%s' found in zone '%s'\n", tone, (zone && zone->country[0]) ? zone->country : "default");
		return -1;
	}

	if (res) {
		ast_log(LOG_WARNING, "Cannot start tone on %s\n", ast_channel_name(chan));
		return -1;
	}
	return 0;
}

int rpt_stop_tone(struct ast_channel *chan)
{
	ast_playtones_stop(chan);
	return 0;
}

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