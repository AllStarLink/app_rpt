
#include "asterisk.h"

#include "asterisk/channel.h"
#include "asterisk/pbx.h" /* use ast_goto_if_exists */
#include "asterisk/format_cache.h"
#include "asterisk/say.h"
#include "asterisk/indications.h"

#include "app_rpt.h"
#include "rpt_lock.h"
#include "rpt_link.h"
#include "rpt_channel.h"
#include "rpt_config.h"
#include "rpt_utils.h"

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
}

int wait_interval(struct rpt *myrpt, enum rpt_delay type, struct ast_channel *chan)
{
	int interval;

	/* This code does NOT wait for previous telemetry to complete!
	 * (that happens at the beginning of rpt_tele_thread).
	 * We only get here after it's our turn in the first place. */

	do {
		while (myrpt->p.holdofftelem && (myrpt->keyed || (myrpt->remrx && (type != DLY_ID)))) {
			if (ast_safe_sleep(chan, 100) < 0)
				return -1;
		}

		interval = get_wait_interval(myrpt, type);
		ast_debug(1, "Delay interval = %d on %s\n", interval, ast_channel_name(chan));
		if (interval && ast_safe_sleep(chan, interval) < 0) {
			return -1;
		}
		ast_debug(1, "Delay complete\n");
	/* This is not superfluous... it's checking the same condition, but it might have gone true again after we exited the first loop, so check. */
	} while (myrpt->p.holdofftelem && (myrpt->keyed || (myrpt->remrx && (type != DLY_ID))));
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

int sayfile(struct ast_channel *mychannel, const char *fname)
{
	return ast_stream_and_wait(mychannel, fname, "");
}

int saycharstr(struct ast_channel *mychannel, const char *str)
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

int sayphoneticstr(struct ast_channel *mychannel, const char *str)
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

int saynode(struct rpt *myrpt, struct ast_channel *mychannel, char *name)
{
	int res = 0, tgn = 0;
	char *val, fname[300], str[100];

	if (strlen(name) < 1)
		return (0);
	if (!tlb_query_callsign(name, str, sizeof(str))) {
		tgn = 1;
	}
	if (((name[0] != '3') && (tgn != 1)) || ((name[0] == '3') && (myrpt->p.eannmode != 2))
		|| ((tgn == 1) && (myrpt->p.tannmode != 2))) {
		val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->name, "nodenames");
		if (!val)
			val = NODENAMES;
		snprintf(fname, sizeof(fname) - 1, "%s/%s", val, name);
		if (ast_fileexists(fname, NULL, ast_channel_language(mychannel)) > 0)
			return (sayfile(mychannel, fname));
		res = sayfile(mychannel, "rpt/node");
		if (!res)
			res = ast_say_character_str(mychannel, name, NULL, ast_channel_language(mychannel), AST_SAY_CASE_NONE);
	}
	if (tgn == 1) {
		if (myrpt->p.tannmode < 2)
			return res;
		return (sayphoneticstr(mychannel, str));
	}
	if (name[0] != '3')
		return res;
	if (myrpt->p.eannmode < 2)
		return res;
	sprintf(str, "%d", atoi(name + 1));
	if (elink_query_callsign(str, fname, sizeof(fname))) {
		return res;
	}
	res = sayphoneticstr(mychannel, fname);
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
			if (digit >= '0' && digit <= '9') {
				ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[digit - '0'], 0);
			} else if (digit >= 'A' && digit <= 'D') {
				ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[digit - 'A' + 10], 0);
			} else if (digit == '*') {
				ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[14], 0);
			} else if (digit == '#') {
				ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[15], 0);
			} else {
				/* not handled */
				ast_log(LOG_WARNING, "Unable to generate DTMF tone '%c' for '%s'\n", digit, ast_channel_name(myrpt->txchannel));
			}
			rpt_mutex_lock(&myrpt->lock);
		} else {
			myrpt->dtmf_local_timer = 0;
		}
	}
}

int play_tone_pair(struct ast_channel *chan, int f1, int f2, int duration, int amplitude)
{
	int res;

	if ((res = ast_tonepair_start(chan, f1, f2, duration, amplitude)))
		return res;

	while (ast_channel_generatordata(chan)) {
		if (ast_safe_sleep(chan, 1))
			return -1;
	}

	return 0;
}

int play_tone(struct ast_channel *chan, int freq, int duration, int amplitude)
{
	return play_tone_pair(chan, freq, 0, duration, amplitude);
}

static int morse_cat(char *str, int freq, int duration)
{
	char *p;
	int len;

	if (!str)
		return -1;

	len = strlen(str);
	p = str + len;

	if (len) {
		*p++ = ',';
		*p = '\0';
	}

	snprintf(p, 62, "!%d/%d", freq, duration);

	return 0;
}

int send_morse(struct ast_channel *chan, const char *string, int speed, int freq, int amplitude)
{
	static struct morse_bits mbits[] = {
		{ 0, 0 },				/* SPACE */
		{ 0, 0 },
		{ 6, 18 },				/* " */
		{ 0, 0 },
		{ 7, 72 },				/* $ */
		{ 0, 0 },
		{ 0, 0 },
		{ 6, 30 },				/* ' */
		{ 5, 13 },				/* ( */
		{ 6, 29 },				/* ) */
		{ 0, 0 },
		{ 5, 10 },				/* + */
		{ 6, 51 },				/* , */
		{ 6, 33 },				/* - */
		{ 6, 42 },				/* . */
		{ 5, 9 },				/* / */
		{ 5, 31 },				/* 0 */
		{ 5, 30 },				/* 1 */
		{ 5, 28 },				/* 2 */
		{ 5, 24 },				/* 3 */
		{ 5, 16 },				/* 4 */
		{ 5, 0 },				/* 5 */
		{ 5, 1 },				/* 6 */
		{ 5, 3 },				/* 7 */
		{ 5, 7 },				/* 8 */
		{ 5, 15 },				/* 9 */
		{ 6, 7 },				/* : */
		{ 6, 21 },				/* ; */
		{ 0, 0 },
		{ 5, 33 },				/* = */
		{ 0, 0 },
		{ 6, 12 },				/* ? */
		{ 0, 0 },
		{ 2, 2 },				/* A */
		{ 4, 1 },				/* B */
		{ 4, 5 },				/* C */
		{ 3, 1 },				/* D */
		{ 1, 0 },				/* E */
		{ 4, 4 },				/* F */
		{ 3, 3 },				/* G */
		{ 4, 0 },				/* H */
		{ 2, 0 },				/* I */
		{ 4, 14 },				/* J */
		{ 3, 5 },				/* K */
		{ 4, 2 },				/* L */
		{ 2, 3 },				/* M */
		{ 2, 1 },				/* N */
		{ 3, 7 },				/* O */
		{ 4, 6 },				/* P */
		{ 4, 11 },				/* Q */
		{ 3, 2 },				/* R */
		{ 3, 0 },				/* S */
		{ 1, 1 },				/* T */
		{ 3, 4 },				/* U */
		{ 4, 8 },				/* V */
		{ 3, 6 },				/* W */
		{ 4, 9 },				/* X */
		{ 4, 13 },				/* Y */
		{ 4, 3 }				/* Z */
	};

	int dottime;
	int dashtime;
	int intralettertime;
	int interlettertime;
	int interwordtime;
	int len, ddcomb;
	int res;
	int c;
	char *str = NULL;

	res = 0;

	str = ast_malloc(12 * 8 * strlen(string));	/* 12 chrs/element max, 8 elements/letter max */
	if (!str)
		return -1;
	str[0] = '\0';

	/* Approximate the dot time from the speed arg. */

	dottime = 900 / speed;

	/* Establish timing relationships */

	dashtime = dottime * 3;
	intralettertime = dottime;
	interlettertime = dottime * 3;
	interwordtime = dottime * 7;

	for (; (*string) && (!res); string++) {

		c = *string;

		/* Convert lower case to upper case */

		if ((c >= 'a') && (c <= 'z'))
			c -= 0x20;

		/* Can't deal with any char code greater than Z, skip it */

		if (c > 'Z')
			continue;

		/* If space char, wait the inter word time */

		if (c == ' ') {
			if (!res) {
				if ((res = morse_cat(str, 0, interwordtime)))
					break;
			}
			continue;
		}

		/* Subtract out control char offset to match our table */

		c -= 0x20;

		/* Get the character data */

		len = mbits[c].len;
		ddcomb = mbits[c].ddcomb;

		/* Send the character */

		for (; len; len--) {
			if (!res)
				res = morse_cat(str, freq, (ddcomb & 1) ? dashtime : dottime);
			if (!res)
				res = morse_cat(str, 0, intralettertime);
			ddcomb >>= 1;
		}

		/* Wait the interletter time */

		if (!res)
			res = morse_cat(str, 0, interlettertime - intralettertime);

	}

	/* Wait for all the characters to be sent */

	if (!res) {
		ast_debug(5, "Morse string: %s\n", str);
		ast_safe_sleep(chan, 100);
		ast_playtones_start(chan, amplitude, str, 0);
		while (ast_channel_generatordata(chan)) {
			if (ast_safe_sleep(chan, 20)) {
				res = -1;
				break;
			}
		}

	}
	if (str)
		ast_free(str);
	return res;
}

int send_usb_txt(struct rpt *myrpt, char *txt)
{
	struct ast_frame wf;

	ast_debug(1, "send_usb_txt %s\n", txt);
	init_text_frame(&wf, "send_usb_txt");
	wf.datalen = strlen(txt) + 1;
	wf.data.ptr = txt;
	ast_write(myrpt->txchannel, &wf);
	return 0;
}

static int rpt_qwrite_cb(void *obj, void *arg, int flags)
{
	struct rpt_link *link = obj;
	struct ast_frame *wf = arg;

	if ((link->chan) && link->name[0] && (link->name[0] != '0')) {
		rpt_qwrite(link, wf);
	}

	return 0;
}

int send_link_pl(struct rpt *myrpt, const char *txt)
{
	struct ast_frame wf;
	char str[300];

	if (!strcmp(myrpt->p.ctgroup, "0"))
		return 0;
	snprintf(str, sizeof(str), "C %s %s %s", myrpt->name, myrpt->p.ctgroup, txt);
	ast_debug(1, "send_link_pl %s\n", str);
	init_text_frame(&wf, "send_link_pl");
	wf.datalen = strlen(str) + 1;
	wf.data.ptr = str;
	rpt_mutex_lock(&myrpt->lock);
	ao2_callback(myrpt->links, OBJ_MULTIPLE | OBJ_NODATA, rpt_qwrite_cb, &wf);
	rpt_mutex_unlock(&myrpt->lock);
	return 0;
}

void send_newkey(struct ast_channel *chan)
{
	ast_assert(chan != NULL);
	/* app_sendtext locks the channel before calling ast_sendtext,
	 * do this to prevent simultaneous channel servicing which can cause an assertion. */
	ast_channel_lock(chan);
	if (ast_sendtext(chan, NEWKEY1STR)) {
		ast_log(LOG_WARNING, "Failed to send text %s on %s\n", NEWKEY1STR, ast_channel_name(chan));
	}
	ast_channel_unlock(chan);
}

void send_newkey_redundant(struct ast_channel *chan)
{
	ast_assert(chan != NULL);
	ast_channel_lock(chan);
	if (ast_sendtext(chan, NEWKEYSTR)) {
		ast_log(LOG_WARNING, "Failed to send text %s on %s\n", NEWKEYSTR, ast_channel_name(chan));
	}
	ast_channel_unlock(chan);
}
