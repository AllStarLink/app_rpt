/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Naveen Albert <asterisk@phreaknet.org>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 *
 * \brief app_rpt bridging
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 */

#include "asterisk.h"

#include <dahdi/user.h>
#include <dahdi/tonezone.h>		/* use tone_zone_set_zone */

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/callerid.h"
#include "asterisk/format_cache.h"

#define RPT_EXPOSE_DAHDI

#include "app_rpt.h"
#include "rpt_bridging.h"
#include "rpt_call.h"
#include "rpt_lock.h"
#include "rpt_telemetry.h"
#include "rpt_rig.h" /* use channel_revert */

#define DESTROY_CHANNEL(chan) \
	if (chan) { \
		ast_hangup(chan); \
		chan = NULL; \
	}

void rpt_hangup_all(struct rpt *myrpt)
{
	DESTROY_CHANNEL(myrpt->pchannel);
	DESTROY_CHANNEL(myrpt->monchannel);
	DESTROY_CHANNEL(myrpt->parrotchannel);
	DESTROY_CHANNEL(myrpt->telechannel);
	DESTROY_CHANNEL(myrpt->btelechannel);
	DESTROY_CHANNEL(myrpt->voxchannel);
	DESTROY_CHANNEL(myrpt->txpchannel);
	if (myrpt->txchannel != myrpt->rxchannel) {
		DESTROY_CHANNEL(myrpt->txchannel);
	}
	DESTROY_CHANNEL(myrpt->rxchannel);
}

struct ast_channel *__rpt_channel_new(struct rpt *myrpt, struct ast_format_cap *cap, const char *name, int confmodes)
{
	int newconf;
	struct ast_channel *chan;
	struct dahdi_confinfo ci; /* conference info */

	chan = ast_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
	if (!chan) {
		ast_log(LOG_ERROR, "Unable to obtain pseudo channel for %s\n", name);
		return NULL;
	}
	ast_debug(1, "Requested channel %s\n", ast_channel_name(chan));

	ast_set_read_format(chan, ast_format_slin);
	ast_set_write_format(chan, ast_format_slin);

	rpt_disable_cdr(chan);
	ast_answer(chan);

	memset(&ci, 0, sizeof(ci));
	newconf = myrpt->conf == -1;
	ci.confno = myrpt->conf;	/* use the pseudo conference (or make a new one, if -1) */
	ci.confmode = confmodes;

	if (join_dahdiconf(chan, &ci)) { /* Put the channel in the conference */
		ast_hangup(chan);
		return NULL;
	}
	if (newconf) {
		myrpt->conf = ci.confno;
	}
	return chan;
}

static struct ast_channel *rpt_request(const char *type, struct ast_format_cap *request_cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *addr, int *cause)
{
	struct ast_channel *chan = ast_request(type, request_cap, assignedids, requestor, addr, cause);
	if (!chan) {
		ast_log(LOG_ERROR, "Unable to obtain channel\n");
	} else if (ast_channel_state(chan) == AST_STATE_BUSY) {
		ast_hangup(chan);
		chan = NULL;
	}
	return chan;
}

static int parse_chan_name(struct rpt *myrpt, const char *name, char *restrict buf, size_t len, const char **restrict chan_tech, const char **restrict chan_device)
{
	char *device;

	ast_copy_string(buf, name, len);

	/* XXX Could use strsep */
	device = strchr(buf, '/');
	if (!device) {
		ast_log(LOG_WARNING, "Channel (%s) must be in format tech/number\n", myrpt->rxchanname);
		return -1;
	}
	*device++ = '\0';
	*chan_tech = buf;
	*chan_device = device;
	return 0;
}

int rpt_add_parrot_chan(struct rpt *myrpt)
{
	struct dahdi_confinfo ci;

	ci.confno = myrpt->conf;
	ci.confmode = DAHDI_CONF_CONFANNMON;

	/* first put the channel on the conference in announce mode */
	return join_dahdiconf(myrpt->parrotchannel, &ci);
}

int rpt_add_chan(struct rpt *myrpt, struct ast_channel *chan)
{
	struct dahdi_confinfo ci;

	ci.confno = 0;
	ci.confmode = 0;

	return join_dahdiconf(chan, &ci);
}

int rpt_setup_channels(struct rpt *myrpt, struct ast_format_cap *cap)
{
	const char *chan_tech, *chan_device;
	char chan_name_buf[512];
	struct dahdi_confinfo ci;	/* conference info */

	if (ast_strlen_zero(myrpt->rxchanname)) {
		ast_log(LOG_ERROR, "No rxchannel specified\n");
		return -1;
	} else if (parse_chan_name(myrpt, myrpt->rxchanname, chan_name_buf, sizeof(chan_name_buf), &chan_tech, &chan_device)) {
		return -1;
	}

	myrpt->rxchannel = rpt_request(chan_tech, cap, NULL, NULL, chan_device, NULL);
	myrpt->dahdirxchannel = !strcasecmp(chan_tech, "DAHDI") ? myrpt->rxchannel : NULL;
	if (!myrpt->rxchannel) {
		return -1;
	}

	rpt_make_call(myrpt->rxchannel, chan_device, 999, chan_tech, "(Repeater Rx)", "Rx", myrpt->name);
	if (ast_channel_state(myrpt->rxchannel) != AST_STATE_UP) {
		return -1;
	}

	myrpt->dahditxchannel = NULL;
	if (!ast_strlen_zero(myrpt->txchanname)) {
		if (parse_chan_name(myrpt, myrpt->txchanname, chan_name_buf, sizeof(chan_name_buf), &chan_tech, &chan_device)) {
			return -1;
		}
		myrpt->txchannel = rpt_request(chan_tech, cap, NULL, NULL, chan_device, NULL);
		if (!myrpt->txchannel) {
			return -1;
		}
		if (!strcasecmp(chan_tech, "DAHDI") && !IS_PSEUDO_NAME(chan_device)) {
			myrpt->dahditxchannel = myrpt->txchannel;
		}
		rpt_make_call(myrpt->txchannel, chan_device, 999, chan_tech, "(Repeater Tx)", "Tx", myrpt->name);
	} else {
		myrpt->txchannel = myrpt->rxchannel;
		if (!strncasecmp(myrpt->rxchanname, "DAHDI", 3) && !IS_PSEUDO_NAME(myrpt->rxchanname)) {
			myrpt->dahditxchannel = myrpt->txchannel;
		}
	}
	if (!IS_PSEUDO(myrpt->txchannel)) {
		ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_KEY);
		ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_UNKEY);
	}

	myrpt->pchannel = rpt_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
	if (!myrpt->pchannel) {
		myrpt->deleted = 1;
		return -1;
	}
	if (!myrpt->dahdirxchannel) {
		myrpt->dahdirxchannel = myrpt->pchannel;
	}
	if (!myrpt->dahditxchannel) {
		myrpt->dahditxchannel = rpt_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
		if (!myrpt->dahditxchannel) {
			myrpt->deleted = 1;
			return -1;
		}
	}

	myrpt->monchannel = rpt_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
	if (!myrpt->monchannel) {
		myrpt->deleted = 1;
		return -1;
	}

	/* make a conference for the tx */
	ci.confno = -1; /* make a new conf */
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_LISTENER;
	if (join_dahdiconf(myrpt->dahditxchannel, &ci)) {
		return -1;
	}
	/* save tx conference number */
	myrpt->txconf = ci.confno;
	/* make a conference for the pseudo */
	ci.confno = -1;				/* make a new conf */
	ci.confmode = (myrpt->p.duplex == 2 || myrpt->p.duplex == 4) ? DAHDI_CONF_CONFANNMON : (DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER);
	/* first put the channel on the conference in announce mode */
	if (join_dahdiconf(myrpt->pchannel, &ci)) {
		return -1;
	}
	/* save pseudo channel conference number */
	myrpt->conf = ci.confno;
	/* make a conference for the pseudo */
	if (!IS_PSEUDO(myrpt->txchannel) && myrpt->dahditxchannel == myrpt->txchannel) {
		/* get tx channel's port number */
		if (ioctl(ast_channel_fd(myrpt->txchannel, 0), DAHDI_CHANNO, &ci.confno) == -1) {
			ast_log(LOG_WARNING, "Unable to set tx channel's chan number\n");
			return -1;
		}
		ci.confmode = DAHDI_CONF_MONITORTX;
	} else {
		ci.confno = myrpt->txconf;
		ci.confmode = DAHDI_CONF_CONFANNMON;
	}

	/* first put the channel on the conference in announce mode */
	if (join_dahdiconf(myrpt->monchannel, &ci)) {
		ast_log(LOG_WARNING, "Unable to set conference mode for monitor\n");
		return -1;
	}

	myrpt->parrotchannel = rpt_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
	if (!myrpt->parrotchannel) {
		myrpt->deleted = 1;
		return -1;
	}

	/* XXX telechannel and btelechannel don't need to be ast_answer'd */

	/* Telemetry Channel Resources */
	myrpt->telechannel = rpt_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
	if (!myrpt->telechannel) {
		myrpt->deleted = 1;
		return -1;
	}

	/* make a conference for the voice/tone telemetry */
	ci.confno = -1; /* make a new conference */
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;
	if (join_dahdiconf(myrpt->telechannel, &ci)) {
		return -1;
	}
	myrpt->teleconf = ci.confno;

	/* make a channel to connect between the telemetry conference process and the main tx audio conference. */
	myrpt->btelechannel = rpt_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
	if (!myrpt->btelechannel) {
		return -1;
	}

	/* make a conference linked to the main tx conference */
	ci.confno = myrpt->txconf;
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER;
	/* first put the channel on the conference in proper mode */
	if (join_dahdiconf(myrpt->btelechannel, &ci)) {
		return -1;
	}

	/* allocate a pseudo-channel thru asterisk */
	myrpt->voxchannel = rpt_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
	if (!myrpt->voxchannel) {
		myrpt->deleted = 1;
		return -1;
	}

	/* allocate a pseudo-channel thru asterisk */
	myrpt->txpchannel = rpt_request("DAHDI", cap, NULL, NULL, "pseudo", NULL);
	if (!myrpt->txpchannel) {
		myrpt->deleted = 1;
		return -1;
	}

	/* make a conference for the tx */
	ci.confno = myrpt->txconf;
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER;
	/* first put the channel on the conference in proper mode */
	if (join_dahdiconf(myrpt->txpchannel, &ci)) {
		return -1;
	}

	return 0;
}

int rpt_play_tone(struct rpt *myrpt, struct ast_channel *chan, int tone, int on)
{
	int res;
	if (on) {
		res = tone_zone_play_tone(ast_channel_fd(chan, 0), tone);
	} else {
		res = tone_zone_play_tone(ast_channel_fd(chan, 0), -1);
	}
	if (res) {
		ast_log(LOG_WARNING, "Cannot %s tone on %s\n", on ? "start" : "stop", ast_channel_name(chan));
	}
	return res;
}

int rpt_set_tonezone(struct rpt *myrpt, struct ast_channel *chan)
{
	int res = tone_zone_set_zone(ast_channel_fd(chan, 0), (char*) myrpt->p.tonezone);
	if (res) {
		ast_log(LOG_WARNING, "Unable to set tone zone %s\n", myrpt->p.tonezone);
	}
	return res;
}

int rpt_run(struct rpt *myrpt)
{
	struct ast_channel *mychannel, *genchannel;
	struct ast_format_cap *cap;
	int stopped = 0, congstarted = 0, dialtimer = 0, lastcidx = 0, aborted = 0;
	int sentpatchconnect;
	struct dahdi_confinfo ci;

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc capabilities\n");
		return -1;
	}
	ast_format_cap_append(cap, ast_format_slin, 0);

	mychannel = rpt_channel_new(myrpt, cap, "mychannel");
	if (!mychannel) {
		myrpt->callmode = 0;
		ao2_ref(cap, -1);
		return -1;
	}

	genchannel = rpt_channel_new(myrpt, cap, "genchannel");
	ao2_ref(cap, -1); /* Don't need caps anymore */
	if (!genchannel) {
		ast_hangup(mychannel);
		return -1;
	}

	if (myrpt->p.tonezone && rpt_set_tonezone(myrpt, mychannel)) {
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		return -1;
	}

	/* start dialtone if patchquiet is 0. Special patch modes don't send dial tone */
	if (!myrpt->patchquiet && !ast_strlen_zero(myrpt->patchexten) && rpt_play_dialtone(myrpt, genchannel, 1)) {
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		return -1;
	}

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

	if (!ast_strlen_zero(myrpt->patchexten)) {
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
			rpt_play_dialtone(myrpt, genchannel, 0);
			
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
				rpt_play_congestion(myrpt, genchannel, 1); /* start congestion tone */
			}
		}
		if (ast_safe_sleep(mychannel, MSWAIT) < 0) {
			ast_debug(1, "ast_safe_sleep returned nonzero\n");
			ast_hangup(mychannel);
			ast_hangup(genchannel);
			rpt_mutex_lock(&myrpt->lock);
			myrpt->callmode = 0;
			rpt_mutex_unlock(&myrpt->lock);
			return -1;
		}
		dialtimer += MSWAIT;
	}

	rpt_play_dialtone(myrpt, genchannel, 0); /* stop any tone generation */

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
		if (!myrpt->patchquiet && aborted) {
			rpt_telemetry(myrpt, TERM, NULL);
		}
		return 0;
	}

	if (!ast_strlen_zero(myrpt->p.ourcallerid)) {
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

	if (myrpt->p.acctcode) {
		ast_channel_accountcode_set(mychannel, myrpt->p.acctcode);
	}
	ast_channel_priority_set(mychannel, 1);
	ast_channel_undefer_dtmf(mychannel);
	if (ast_pbx_start(mychannel) < 0) {
		ast_log(LOG_ERROR, "Unable to start PBX on %s\n", ast_channel_name(mychannel));
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		rpt_mutex_lock(&myrpt->lock);
		myrpt->callmode = 0;
		rpt_mutex_unlock(&myrpt->lock);
		return -1;
	}

	usleep(10000);

	rpt_mutex_lock(&myrpt->lock);
	myrpt->callmode = 3;

	/* set appropriate conference for the pseudo */
	memset(&ci, 0, sizeof(ci));
	ci.confno = myrpt->conf;
	ci.confmode = (myrpt->p.duplex == 2) ? DAHDI_CONF_CONFANNMON : (DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER);
	if (ast_channel_pbx(mychannel)) {
		int res;
		/* first put the channel on the conference in announce mode */
		if (join_dahdiconf(myrpt->pchannel, &ci)) {
			ast_hangup(mychannel);
			ast_hangup(genchannel);
			myrpt->callmode = 0;
			rpt_mutex_unlock(&myrpt->lock);
			pthread_exit(NULL);
		}
		/* get its channel number */
		res = 0;
		if (ioctl(ast_channel_fd(mychannel, 0), DAHDI_CHANNO, &res) == -1) {
			ast_log(LOG_WARNING, "Unable to get autopatch channel number\n");
			ast_hangup(mychannel);
			myrpt->callmode = 0;
			rpt_mutex_unlock(&myrpt->lock);
			pthread_exit(NULL);
		}
		ci.confno = res;
		ci.confmode = DAHDI_CONF_MONITOR;
		/* put vox channel monitoring on the channel  */
		if (join_dahdiconf(myrpt->voxchannel, &ci)) {
			ast_hangup(mychannel);
			myrpt->callmode = 0;
			pthread_exit(NULL);
		}
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
				tone_zone_play_tone(ast_channel_fd(genchannel, 0), DAHDI_TONE_CONGESTION);
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
	rpt_play_dialtone(myrpt, genchannel, 0);
	if (ast_channel_pbx(mychannel)) {
		ast_softhangup(mychannel, AST_SOFTHANGUP_DEV);
	}
	ast_hangup(genchannel);
	rpt_mutex_lock(&myrpt->lock);
	myrpt->callmode = 0;
	myrpt->macropatch = 0;
	channel_revert(myrpt);
	rpt_mutex_unlock(&myrpt->lock);
	/* set appropriate conference for the pseudo */
	ci.confno = myrpt->conf;
	ci.confmode = ((myrpt->p.duplex == 2) || (myrpt->p.duplex == 4)) ? DAHDI_CONF_CONFANNMON : (DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER);
	/* first put the channel on the conference in announce mode */
	join_dahdiconf(myrpt->pchannel, &ci);
	return 0;
}
