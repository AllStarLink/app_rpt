
#include "asterisk.h"

#include "asterisk/channel.h"
#include "asterisk/pbx.h" /* use ast_goto_if_exists */
#include "asterisk/format_cache.h"
#include "asterisk/say.h"

#include "app_rpt.h"
#include "rpt_channel.h"
#include "rpt_config.h"

void rpt_safe_sleep(struct rpt *rpt, struct ast_channel *chan, int ms)
{
	struct ast_frame *f;
	struct ast_channel *cs[2], *w;

	cs[0] = rpt->rxchannel;
	cs[1] = chan;
	while (ms > 0) {
		w = ast_waitfor_n(cs, 2, &ms);
		if (!w)
			break;
		f = ast_read(w);
		if (!f)
			break;
		if ((w == cs[0]) && (f->frametype != AST_FRAME_VOICE) && (f->frametype != AST_FRAME_NULL)) {
			ast_queue_frame(rpt->rxchannel, f);
			ast_frfree(f);
			break;
		}
		ast_frfree(f);
	}
	return;
}

int wait_interval(struct rpt *myrpt, int type, struct ast_channel *chan)
{
	int interval;

	do {
		while (myrpt->p.holdofftelem && (myrpt->keyed || (myrpt->remrx && (type != DLY_ID)))) {
			if (ast_safe_sleep(chan, 100) < 0)
				return -1;
		}

		interval = get_wait_interval(myrpt, type);
		ast_debug(1, "Delay interval = %d\n", interval);
		if (interval)
			if (ast_safe_sleep(chan, interval) < 0)
				return -1;
		ast_debug(1, "Delay complete\n");
	}
	while (myrpt->p.holdofftelem && (myrpt->keyed || (myrpt->remrx && (type != DLY_ID))));
	return 0;
}

int priority_jump(struct rpt *myrpt, struct ast_channel *chan)
{
	int res;

	if (!ast_goto_if_exists(chan, ast_channel_context(chan), ast_channel_exten(chan), ast_channel_priority(chan) + 101)) {
		res = 0;
	} else {
		res = -1;
	}
	return res;
}

int sayfile(struct ast_channel *mychannel, char *fname)
{
	int res;

	res = ast_streamfile(mychannel, fname, ast_channel_language(mychannel));
	if (!res)
		res = ast_waitstream(mychannel, "");
	else
		ast_log(LOG_WARNING, "ast_streamfile %s failed on %s\n", fname, ast_channel_name(mychannel));
	ast_stopstream(mychannel);
	return res;
}

int saycharstr(struct ast_channel *mychannel, char *str)
{
	int res;

	res = ast_say_character_str(mychannel, str, NULL, ast_channel_language(mychannel), AST_SAY_CASE_NONE);
	if (!res)
		res = ast_waitstream(mychannel, "");
	else
		ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
	ast_stopstream(mychannel);
	return res;
}

int sayphoneticstr(struct ast_channel *mychannel, char *str)
{
	int res;

	res = ast_say_phonetic_str(mychannel, str, NULL, ast_channel_language(mychannel));
	if (!res)
		res = ast_waitstream(mychannel, "");
	else
		ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
	ast_stopstream(mychannel);
	return res;
}

int saynum(struct ast_channel *mychannel, int num)
{
	int res;
	res = ast_say_number(mychannel, num, NULL, ast_channel_language(mychannel), NULL);
	if (!res)
		res = ast_waitstream(mychannel, "");
	else
		ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
	ast_stopstream(mychannel);
	return res;
}
