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

#include "app_rpt.h"
#include "rpt_call.h"

int rpt_setup_call(struct ast_channel *chan, const char *addr, int timeout, const char *driver, const char *data, const char *desc, const char *callerid)
{
	ast_debug(1, "Requested channel %s\n", ast_channel_name(chan));
	ast_set_read_format(chan, ast_format_slin);
	ast_set_write_format(chan, ast_format_slin);
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (chan->cdr)
		ast_set_flag(chan->cdr, AST_CDR_FLAG_POST_DISABLED);
#endif
	ast_channel_appl_set(chan, "Apprpt");
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
