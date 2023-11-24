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

void rpt_hangup_all(struct rpt *rpt);

#define rpt_channel_new(myrpt, cap, name) __rpt_channel_new(myrpt, cap, name, DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER)

struct ast_channel *__rpt_channel_new(struct rpt *myrpt, struct ast_format_cap *cap, const char *name, int confmodes);

/*! \note Must be called with rpt mutex held */
int rpt_add_parrot_chan(struct rpt *myrpt);

/*! \note Must be called with rpt mutex held */
int rpt_add_chan(struct rpt *myrpt, struct ast_channel *chan);

/*! \note Must be called with rpt mutex held */
int rpt_setup_channels(struct rpt *myrpt, struct ast_format_cap *caps);

#define rpt_play_dialtone(myrpt, chan, on) rpt_play_tone(myrpt, chan, DAHDI_TONE_DIALTONE, on)
#define rpt_play_congestion(myrpt, chan, on) rpt_play_tone(myrpt, chan, DAHDI_TONE_CONGESTION, on)

int rpt_play_tone(struct rpt *myrpt, struct ast_channel *chan, int tone, int on);

int rpt_set_tonezone(struct rpt *myrpt, struct ast_channel *chan);

int rpt_run(struct rpt *myrpt);
