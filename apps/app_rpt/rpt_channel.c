
#include "asterisk.h"

#include "asterisk/channel.h"
#include "asterisk/pbx.h" /* use ast_goto_if_exists */
#include "asterisk/format_cache.h"
#include "asterisk/say.h"
#include "asterisk/indications.h"

#include "app_rpt.h"
#include "rpt_lock.h"
#include "rpt_channel.h"
#include "rpt_config.h"

extern char *dtmf_tones[];

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

void do_dtmf_local(struct rpt *myrpt, char c)
{
	int i;
	char digit;

	if (c) {
		snprintf(myrpt->dtmf_local_str + strlen(myrpt->dtmf_local_str), sizeof(myrpt->dtmf_local_str) - 1, "%c", c);
		if (!myrpt->dtmf_local_timer)
			myrpt->dtmf_local_timer = DTMF_LOCAL_STARTTIME;
	}
	/* if at timeout */
	if (myrpt->dtmf_local_timer == 1) {
		ast_debug(7, "time out dtmf_local_timer=%i\n", myrpt->dtmf_local_timer);

		/* if anything in the string */
		if (myrpt->dtmf_local_str[0]) {
			digit = myrpt->dtmf_local_str[0];
			myrpt->dtmf_local_str[0] = 0;
			for (i = 1; myrpt->dtmf_local_str[i]; i++) {
				myrpt->dtmf_local_str[i - 1] = myrpt->dtmf_local_str[i];
			}
			myrpt->dtmf_local_str[i - 1] = 0;
			myrpt->dtmf_local_timer = DTMF_LOCAL_TIME;
			rpt_mutex_unlock(&myrpt->lock);
			if (!strcasecmp(ast_channel_tech(myrpt->txchannel)->type, "rtpdir")) {
				ast_senddigit(myrpt->txchannel, digit, 0);
			} else {
				if (digit >= '0' && digit <= '9')
					ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[digit - '0'], 0);
				else if (digit >= 'A' && digit <= 'D')
					ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[digit - 'A' + 10], 0);
				else if (digit == '*')
					ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[14], 0);
				else if (digit == '#')
					ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[15], 0);
				else {
					/* not handled */
					ast_log(LOG_WARNING, "Unable to generate DTMF tone '%c' for '%s'\n", digit, ast_channel_name(myrpt->txchannel));
				}
			}
			rpt_mutex_lock(&myrpt->lock);
		} else {
			myrpt->dtmf_local_timer = 0;
		}
	}
}

int send_usb_txt(struct rpt *myrpt, char *txt)
{
	struct ast_frame wf;

	ast_debug(1, "send_usb_txt %s\n", txt);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass.format = ast_format_slin;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(txt) + 1;
	wf.data.ptr = txt;
	wf.samples = 0;
	wf.src = "send_usb_txt";
	ast_write(myrpt->txchannel, &wf);
	return 0;
}

int send_link_pl(struct rpt *myrpt, char *txt)
{
	struct ast_frame wf;
	struct rpt_link *l;
	char str[300];

	if (!strcmp(myrpt->p.ctgroup, "0"))
		return 0;
	snprintf(str, sizeof(str), "C %s %s %s", myrpt->name, myrpt->p.ctgroup, txt);
	ast_debug(1, "send_link_pl %s\n", str);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass.format = ast_format_slin;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.data.ptr = str;
	wf.samples = 0;
	wf.src = "send_link_pl";
	l = myrpt->links.next;
	while (l && (l != &myrpt->links)) {
		if ((l->chan) && l->name[0] && (l->name[0] != '0')) {
			ast_write(l->chan, &wf);
		}
		l = l->next;
	}
	return 0;
}
