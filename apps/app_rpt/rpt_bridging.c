
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

/*! \todo Make static */
int dahdi_conf_create(struct ast_channel *chan, int *confno, int mode)
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

/*! \todo eventually make this static */
int dahdi_conf_add(struct ast_channel *chan, int confno, int mode)
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
		return &myrpt->rptconf.conf;
	case RPT_TXCONF:
		return &myrpt->rptconf.txconf;
	case RPT_TELECONF:
		return &myrpt->rptconf.teleconf;
	}
	ast_assert(0);
	return NULL;
}

int __rpt_conf_create(struct ast_channel *chan, struct rpt *myrpt, enum rpt_conf_type type, enum rpt_conf_flags flags, const char *file, int line)
{
	/* Convert RPT conf flags to DAHDI conf flags... for now. */
	int *confno, dflags;

	dflags = dahdi_conf_flags(flags);
	confno = dahdi_confno(myrpt, type);

	if (dahdi_conf_create(chan, confno, dflags)) {
		ast_log(LOG_ERROR, "%s:%d: Failed to create conference using chan type %d\n", file, line, type);
		return -1;
	}
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

int dahdi_conf_fd_confno(struct ast_channel *chan)
{
	struct dahdi_confinfo ci;

	if (ioctl(ast_channel_fd(chan, 0), DAHDI_CHANNO, &ci.confno) == -1) {
		ast_log(LOG_WARNING, "DAHDI_CHANNO failed: %s\n", strerror(errno));
		return -1;
	}

	return ci.confno;
}

/*!
 * \param chan
 * \param tone 0 = congestion, 1 = dialtone
 * \note Only used in 3 places in app_rpt.c
 */
static int dahdi_play_tone(struct ast_channel *chan, int tone)
{
	if (tone_zone_play_tone(ast_channel_fd(chan, 0), tone)) {
		ast_log(LOG_WARNING, "Cannot start tone on %s\n", ast_channel_name(chan));
		return -1;
	}
	return 0;
}

int rpt_play_dialtone(struct ast_channel *chan)
{
	return dahdi_play_tone(chan, DAHDI_TONE_DIALTONE);
}

int rpt_play_congestion(struct ast_channel *chan)
{
	return dahdi_play_tone(chan, DAHDI_TONE_DIALTONE);
}

int rpt_stop_tone(struct ast_channel *chan)
{
	return dahdi_play_tone(chan, -1);
}

int rpt_set_tone_zone(struct ast_channel *chan, const char *tz)
{
	if (tone_zone_set_zone(ast_channel_fd(chan, 0), (char*) tz) == -1) {
		ast_log(LOG_WARNING, "Unable to set tone zone %s on %s\n", tz, ast_channel_name(chan));
		return -1;
	}
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
