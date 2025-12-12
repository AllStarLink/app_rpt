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

#include "asterisk/bridge.h"
#include "asterisk/core_unreal.h"
#include "asterisk/channel.h"
#include "asterisk/indications.h"
#include "asterisk/format_cache.h" /* use ast_format_slin */
#include "asterisk/audiohook.h"

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

static const char *rpt_chan_name(struct rpt *myrpt, enum rpt_chan_type chantype)
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
	case RPT_LOCALTXCHAN:
	case RPT_MONCHAN:
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
	case RPT_LOCALTXCHAN:
	case RPT_MONCHAN:
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

	if (strcasecmp(tech, "Local")) {
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
		ast_autoservice_start(p->chan);
	}
	chanptr = rpt_chan_channel(myrpt, NULL, chantype);
	*chanptr = chan;

	switch (chantype) {
	case RPT_RXCHAN:
		myrpt->localrxchannel = !strcasecmp(tech, "Local") ? chan : NULL;
		break;
	case RPT_TXCHAN:
		if (flags & RPT_LINK_CHAN) {
			/* XXX Dunno if this difference is really necessary, but this is a literal refactor of existing logic... */
			myrpt->localtxchannel = !strcasecmp(tech, "DAHDI") ? chan : NULL;
		} else {
			myrpt->localtxchannel = !strcasecmp(tech, "DAHDI") && strcasecmp(device, "pseudo") ? chan : NULL;
		}
		break;
	default:
		break;
	}

	return 0;
}

struct ast_channel *rpt_request_pseudo_chan(struct ast_format_cap *cap, const char *exten)
{
	struct ast_channel *chan;
	struct ast_unreal_pvt *p;

	chan = ast_request("Local", cap, NULL, NULL, exten, NULL);
	if (!chan) {
		ast_log(LOG_ERROR, "Failed to request pseudo channel\n");
		return NULL;
	}

	ast_debug(1, "Requesting channel %s setup\n", ast_channel_name(chan));
	ast_set_read_format(chan, ast_format_slin);
	ast_set_write_format(chan, ast_format_slin);
	rpt_disable_cdr(chan);
	p = ast_channel_tech_pvt(chan);
	ast_debug(1, "Requested channel %s cdr disabled\n", ast_channel_name(chan));
	ast_answer(p->owner);
	ast_answer(p->chan);
	return chan;
}

int __rpt_request_pseudo(void *data, struct ast_format_cap *cap, enum rpt_chan_type chantype, enum rpt_chan_flags flags, const char *exten)
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

	chan = ast_request("Local", cap, NULL, NULL, exten, NULL);
	if (!chan) {
		ast_log(LOG_ERROR, "Failed to request local channel\n");
		return -1;
	}
	rpt_disable_cdr(chan);
	p = ast_channel_tech_pvt(chan);
	ast_answer(p->owner);
	ast_answer(p->chan);
	ast_debug(3, "Local channel p->owner %p, p->chan %p, chan %p\n", p->owner, p->chan, chan);
	ast_debug(3, "Channel states p->owner %d, p->chan %d, chan %d\n", ast_channel_state(p->owner), ast_channel_state(p->chan),
		ast_channel_state(chan));

	chanptr = rpt_chan_channel(myrpt, link, chantype);
	*chanptr = chan;

	switch (chantype) {
	case RPT_PCHAN:
		if (!(flags & RPT_LINK_CHAN)) {
			ast_assert(myrpt != NULL);
			if (!myrpt->localrxchannel) {
				myrpt->localrxchannel = chan;
			}
		}
		break;
	default:
		break;
	}

	return 0;
}

int __rpt_conf_create(struct rpt *myrpt, enum rpt_conf_type type, const char *file, int line)
{
	struct ast_bridge *conf = NULL, **confptr;
	char conference_name[64] = "";
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
		ast_debug(1, "Incorrect conference type\n");
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
	char conference_name[10] = "\0";

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
		ast_debug(3, "Incorrect conference type");
		return -1;
	}
	if (!conf) {
		ast_log(LOG_ERROR, "Conference '%s' mixing bridge is null cannot add %s.\n", conference_name, ast_channel_name(chan));
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

/*!
 * \param chan
 * \param tone DAHDI_TONE_DIALTONE, DAHDI_TONE_CONGESTION, or -1 to stop tone
 * \retval 0 on success, -1 on failure
 */
int rpt_play_tone(struct ast_channel *chan, const char *tone)
{
	int res = 0;
	struct ast_tone_zone *zone = ast_channel_zone(chan);
	struct ast_tone_zone_sound *ts = ast_get_indication_tone(zone, tone);
	if (ts) {
		res = ast_playtones_start(chan, 0, ts->data, 0);
		ts = ast_tone_zone_sound_unref(ts);
	} else {
		ast_log(LOG_WARNING, "No tone '%s' found in zone '%s'\n", tone, (zone && zone->country) ? zone->country : "default");
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