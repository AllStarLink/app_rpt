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
 * \brief app_rpt call helper functions
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 */

#include "asterisk.h"

#include "asterisk/channel.h"
#include "asterisk/format_cache.h"
#include "asterisk/core_unreal.h"

#include "app_rpt.h"
#include "rpt_call.h"

int rpt_disable_cdr(struct ast_channel *chan)
{
	struct ast_unreal_pvt *p;
	if (!CHAN_TECH(chan, "Local")) {
		if (ast_cdr_set_property(ast_channel_name(chan), AST_CDR_FLAG_DISABLE_ALL)) {
			ast_log(AST_LOG_WARNING, "Failed to disable CDR for channel %s\n", ast_channel_name(chan));
			return -1;
		}
		return 0;
	}
	/* It's a local channel */
	p = ast_channel_tech_pvt(chan);
	if (!p || !p->owner || !p->chan) {
		ast_log(AST_LOG_WARNING, "Local channel %s missing endpoints\n", ast_channel_name(chan));
		return -1;
	}
	if (!ast_channel_cdr(p->owner)) {
		ast_debug(4, "No CDR present on %s\n", ast_channel_name(p->owner));
		return 0;
	} else if (ast_cdr_set_property(ast_channel_name(p->owner), AST_CDR_FLAG_DISABLE_ALL)) {
		ast_log(AST_LOG_WARNING, "Failed to disable CDR for channel %s\n", ast_channel_name(p->owner));
	}
	if (!ast_channel_cdr(p->chan)) {
		ast_debug(4, "No CDR present on %s\n", ast_channel_name(p->chan));
	} else if (ast_cdr_set_property(ast_channel_name(p->chan), AST_CDR_FLAG_DISABLE_ALL)) {
		ast_log(AST_LOG_WARNING, "Failed to disable CDR for channel %s\n", ast_channel_name(p->chan));
	}

	return 0;
}

int rpt_setup_call(struct ast_channel *chan, const char *addr, int timeout, const char *driver, const char *data, const char *desc, const char *callerid)
{
	ast_debug(1, "Requested channel %s\n", ast_channel_name(chan));
	ast_set_read_format(chan, ast_format_slin);
	ast_set_write_format(chan, ast_format_slin);
	rpt_disable_cdr(chan);
	ast_channel_appl_set(chan, "Rpt");
	ast_channel_data_set(chan, data);

	/* Set connected to actually set outgoing Caller ID - ast_set_callerid has no effect! */
	ast_channel_connected(chan)->id.number.valid = 1;
	ast_channel_connected(chan)->id.number.str = ast_strdup(callerid);

	ast_debug(1, "rpt (%s) initiating call to %s/%s on %s\n", data, driver, addr, ast_channel_name(chan));
	return 0;
}

int rpt_make_call(struct ast_channel *chan, const char *addr, int timeout, const char *driver, const char *data, const char *desc, const char *callerid)
{
	int res = rpt_setup_call(chan, addr, timeout, driver, data, desc, callerid);
	if (res) {
		return res;
	}
	return ast_call(chan, addr, timeout);
}

void rpt_forward(struct ast_channel *chan, char *dialstr, char *nodefrom)
{
	struct ast_channel *dest, *w, *cs[2];
	struct ast_frame *f;
	int ms;
	struct ast_format_cap *cap;

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		return;
	}

	ast_format_cap_append(cap, ast_format_slin, 0);

	dest = ast_request("IAX2", cap, NULL, NULL, dialstr, NULL);
	if (!dest) {
		if (ast_safe_sleep(chan, 150) == -1) {
			ao2_ref(cap, -1);
			return;
		}
		dest = ast_request("IAX2", cap, NULL, NULL, dialstr, NULL);
		if (!dest) {
			ast_log(LOG_ERROR, "Can not create channel for rpt_forward to IAX2/%s\n", dialstr);
			ao2_ref(cap, -1);
			return;
		}
	}
	ast_debug(1, "Requested channel %s\n", ast_channel_name(dest));
	ast_set_read_format(chan, ast_format_slin);
	ast_set_write_format(chan, ast_format_slin);
	ast_set_read_format(dest, ast_format_slin);
	ast_set_write_format(dest, ast_format_slin);
	ao2_ref(cap, -1);

	/* Set connected to actually set outgoing Caller ID - ast_set_callerid has no effect! */
	ast_channel_connected(chan)->id.number.valid = 1;
	ast_channel_connected(chan)->id.number.str = ast_strdup(nodefrom);

	ast_verb(3, "rpt forwarding call from %s to %s on %s\n", nodefrom, dialstr, ast_channel_name(dest));
	ast_call(dest, dialstr, 999);
	cs[0] = chan;
	cs[1] = dest;
	for (;;) {
		if (ast_check_hangup(chan))
			break;
		if (ast_check_hangup(dest))
			break;
		ms = 100;
		w = cs[0];
		cs[0] = cs[1];
		cs[1] = w;
		w = ast_waitfor_n(cs, 2, &ms);
		if (!w)
			continue;
		if (w == chan) {
			f = ast_read(chan);
			if (!f)
				break;
			if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass.integer == AST_CONTROL_HANGUP)) {
				ast_frfree(f);
				break;
			}
			ast_write(dest, f);
			ast_frfree(f);
		}
		if (w == dest) {
			f = ast_read(dest);
			if (!f)
				break;
			if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass.integer == AST_CONTROL_HANGUP)) {
				ast_frfree(f);
				break;
			}
			ast_write(chan, f);
			ast_frfree(f);
		}

	}
	ast_hangup(dest);
}
