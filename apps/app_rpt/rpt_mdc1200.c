
/*! \file
 *
 * \brief MDC 1200
 */

#include "asterisk.h"

#include <fcntl.h>
#include <fnmatch.h>

#include "asterisk/channel.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/format_cache.h" /* use ast_format_slin */

#include "app_rpt.h"

#include "rpt_mdc1200.h"
#include "rpt_utils.h"
#include "rpt_lock.h"
#include "rpt_config.h"
#include "rpt_link.h"
#include "rpt_manager.h"
#include "rpt_telemetry.h"

#ifdef USE_MDC1200
/* Include support decoding of MDC-1200 digital tone
   signalling protocol (using KA6SQG's GPL'ed implementation) */
#include "mdc_decode.c"
#include "mdc_encode.c"
#endif

/*** DOCUMENTATION
	<application name="MDC1200Gen" language="en_US">
		<synopsis>
			MDC1200 Generator
		</synopsis>
		<syntax>
			<parameter name="type">
				<para>Type is 'I' for PttID, 'E' for Emergency, and 'C' for Call (SelCall or Alert), or 'SX' for STS
				(status), where X is 0-F (indicating the status code).</para>
				<para>DestID and subcode are only specified for the 'C' type message.</para>
			</parameter>
			<parameter name="unitid">
				<para>Unit ID.</para>
			</parameter>
			<parameter name="destid">
				<para>DestID is the MDC1200 ID of the radio being called.</para>
			</parameter>
			<parameter name="subcode">
				<para>Subcodes are as follows:</para>
				<enumlist>
					<enum name="8205">
						<para>Voice Selective Call for Spectra ('Call')</para>
					</enum>
					<enum name="8015">
						<para>Voice Selective Call for Maxtrac ('SC') or Astro-Saber('Call')</para>
					</enum>
					<enum name="810D">
						<para>Call Alert (like Maxtrac 'CA')</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Generates MDC-1200 burst for given UnitID.</para>
		</description>
	</application>
 ***/

void mdc1200_notify(struct rpt *myrpt, char *fromnode, char *data)
{
	FILE *fp;
	char str[50];
	struct flock fl;
	time_t t;

	rpt_manager_trigger(myrpt, "MDC-1200", data);

	if (!fromnode) {
		ast_verb(4, "Got MDC-1200 data %s from local system (%s)\n", data, myrpt->name);
		if (myrpt->p.mdclog) {
			fp = fopen(myrpt->p.mdclog, "a");
			if (!fp) {
				ast_log(LOG_ERROR, "Cannot open MDC1200 log file %s\n", myrpt->p.mdclog);
				return;
			}
			fl.l_type = F_WRLCK;
			fl.l_whence = SEEK_SET;
			fl.l_start = 0;
			fl.l_len = 0;
			fl.l_pid = pthread_self();
			if (fcntl(fileno(fp), F_SETLKW, &fl) == -1) {
				ast_log(LOG_ERROR, "Cannot get lock on MDC1200 log file %s\n", myrpt->p.mdclog);
				fclose(fp);
				return;
			}
			time(&t);
			strftime(str, sizeof(str) - 1, "%Y%m%d%H%M%S", localtime(&t));
			fprintf(fp, "%s %s %s\n", str, myrpt->name, data);
			fl.l_type = F_UNLCK;
			fcntl(fileno(fp), F_SETLK, &fl);
			fclose(fp);
		}
	} else {
		ast_verb(4, "Got MDC-1200 data %s from node %s (%s)\n", data, fromnode, myrpt->name);
	}
}

#ifdef	_MDC_DECODE_H_

void mdc1200_send(struct rpt *myrpt, char *data)
{
	struct rpt_link *l;
	struct ast_frame wf;
	struct ao2_iterator l_it;
	char str[200];

	if (!myrpt->keyed)
		return;

	sprintf(str, "I %s %s", myrpt->name, data);
	init_text_frame(&wf, "mdc1200_send");
	wf.data.ptr = str;
	wf.datalen = strlen(str) + 1; /* Isuani, 20141001 */

	l_it = ao2_iterator_init(myrpt->ao2_links, 0);
	/* otherwise, send it to all of em */
	for (; (l = ao2_iterator_next(&l_it)); ao2_ref(l, -1)) {
		/* Dont send to IAXRPT client, unless main channel is Voter */
		if (((l->name[0] == '0') && !CHAN_TECH(myrpt->rxchannel, "voter")) || (l->phonemode)) {
			continue;
		}
		if (l->chan) {
			rpt_qwrite(l, &wf);
		}
	}
	ao2_iterator_destroy(&l_it);
}

static const char *my_variable_match(const struct ast_config *config, const char *category, const char *variable)
{
	struct ast_variable *v;

	if (category) {
		for (v = ast_variable_browse(config, category); v; v = v->next) {
			if (!fnmatch(v->name, variable, FNM_CASEFOLD | FNM_NOESCAPE))
				return v->value;
		}

	}
	return NULL;
}

void mdc1200_cmd(struct rpt *myrpt, char *data)
{
	char *myval;
	int i;

	if ((data[0] == 'I') && (!strcmp(data, myrpt->lastmdc)))
		return;
	myval = (char *) my_variable_match(myrpt->cfg, myrpt->p.mdcmacro, data);
	if (myval) {
		ast_verb(4, "MDCMacro for %s doing %s on node %s\n", data, myval, myrpt->name);
		if ((*myval == 'K') || (*myval == 'k')) {
			if (!myrpt->keyed) {
				for (i = 1; myval[i]; i++)
					local_dtmfkey_helper(myrpt, myval[i]);
			}
			return;
		}
		if (!myrpt->keyed)
			return;
		macro_append(myrpt, myval);
	}
	if (data[0] == 'I') {
		ast_copy_string(myrpt->lastmdc, data, sizeof(myrpt->lastmdc));
	}
	return;
}

#ifdef _MDC_ENCODE_H_

void mdc1200_ack_status(struct rpt *myrpt, short UnitID)
{
	struct mdcparams *mdcp;

	mdcp = ast_calloc(1, sizeof(struct mdcparams));
	if (!mdcp) {
		return;
	}
	mdcp->type[0] = 'A';
	mdcp->UnitID = UnitID;
	rpt_telemetry(myrpt, MDC1200, (void *) mdcp);
}

#endif
#endif

#ifdef	_MDC_ENCODE_H_

static void mdcgen_release(struct ast_channel *chan, void *params)
{
	struct mdcgen_pvt *ps = params;
	if (chan) {
		ast_set_write_format(chan, ps->origwfmt);
	}
	if (!ps)
		return;
	if (ps->mdc)
		ast_free(ps->mdc);
	ast_free(ps);
}

static void *mdcgen_alloc(struct ast_channel *chan, void *params)
{
	struct mdcgen_pvt *ps;
	struct mdcparams *p = (struct mdcparams *) params;

	if (!(ps = ast_calloc(1, sizeof(*ps))))
		return NULL;
	ps->origwfmt = ast_channel_writeformat(chan);	/*! \todo does this need to be freed? */
	ps->mdc = mdc_encoder_new(8000);
	if (!ps->mdc) {
		ast_free(ps);
		return NULL;
	}
	if (p->type[0] == 'I') {
		mdc_encoder_set_packet(ps->mdc, 1, 0x80, p->UnitID);
	} else if (p->type[0] == 'E') {
		mdc_encoder_set_packet(ps->mdc, 0, 0x80, p->UnitID);
	} else if (p->type[0] == 'S') {
		mdc_encoder_set_packet(ps->mdc, 0x46, p->type[1] - '0', p->UnitID);
	} else if (p->type[0] == 'C') {
		mdc_encoder_set_double_packet(ps->mdc, 0x35, 0x89, p->DestID, p->subcode >> 8, p->subcode & 0xff,
									  p->UnitID >> 8, p->UnitID & 0xff);
	} else if (p->type[0] == 'A') {
		mdc_encoder_set_packet(ps->mdc, 0x23, 0, p->UnitID);
	} else if (p->type[0] == 'K')	// kill a unit W9CR
	{
		mdc_encoder_set_packet(ps->mdc, (unsigned char) 0x22b, 0x00, p->UnitID);
	} else if (p->type[0] == 'U')	// UnKill a unit W9CR
	{
		mdc_encoder_set_packet(ps->mdc, 0x2b, 0x0c, p->UnitID);
	} else {
		ast_log(LOG_ERROR, "Dont know MDC encode type '%s'\n", p->type);
		ast_free(ps);
		return NULL;
	}
	if (ast_set_write_format(chan, ast_format_slin)) {
		ast_log(LOG_ERROR, "Unable to set '%s' to signed linear format (write)\n", ast_channel_name(chan));
		ast_free(ps);
		return NULL;
	}
	return ps;
}

static int mdcgen_generator(struct ast_channel *chan, void *data, int len, int samples)
{
	struct mdcgen_pvt *ps = data;
	short s, *sp;
	int i, n;

	if (!samples)
		return 1;
	if (samples > sizeof(ps->cbuf))
		return -1;
	if (samples < 0)
		samples = 160;
	n = mdc_encoder_get_samples(ps->mdc, ps->cbuf, samples);
	if (n < 1)
		return 1;
	sp = (short *) (ps->buf + AST_FRIENDLY_OFFSET);
	for (i = 0; i < n; i++) {
		s = ((short) ps->cbuf[i]) - 128;
		*sp++ = s * 81;
	}
	ps->f.frametype = AST_FRAME_VOICE;
	ps->f.subclass.format = ast_format_slin;
	ps->f.datalen = n * 2;
	ps->f.samples = n;
	ps->f.offset = AST_FRIENDLY_OFFSET;
	ps->f.data.ptr = ps->buf + AST_FRIENDLY_OFFSET;
	ps->f.delivery = ast_tv(0, 0);
	ast_write(chan, &ps->f);
	return 0;
}

static struct ast_generator mdcgen = {
	alloc:mdcgen_alloc,
	release:mdcgen_release,
	generate:mdcgen_generator,
};

int mdc1200gen_start(struct ast_channel *chan, char *type, short UnitID, short destID, short subcode)
{
	struct mdcparams p;

	memset(&p, 0, sizeof(p));
	ast_copy_string(p.type, type, sizeof(p.type));
	p.UnitID = UnitID;
	p.DestID = destID;
	p.subcode = subcode;
	if (ast_activate_generator(chan, &mdcgen, &p)) {
		return -1;
	}
	return 0;
}

int mdc1200gen(struct ast_channel *chan, char *type, short UnitID, short destID, short subcode)
{

	int res;
	struct ast_frame *f;

	res = mdc1200gen_start(chan, type, UnitID, destID, subcode);
	if (res)
		return res;

	while (ast_channel_generatordata(chan)) {
		if (ast_check_hangup(chan))
			return -1;
		res = ast_waitfor(chan, 100);
		if (res <= 0)
			return -1;
		f = ast_read(chan);
		if (f)
			ast_frfree(f);
		else
			return -1;
	}
	return 0;
}

static int mdcgen_exec(struct ast_channel *chan, const char *data)
{
	struct ast_module_user *u;
	char *tmp;
	int res;
	short unitid, destid, subcode;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(type);
		AST_APP_ARG(unit);
		AST_APP_ARG(destid);
		AST_APP_ARG(subcode);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "MDC1200 requires an arguments!!\n");
		return -1;
	}

	tmp = ast_strdup(data);
	AST_STANDARD_APP_ARGS(args, tmp);

	if ((!args.type) || (!args.unit)) {
		ast_log(LOG_WARNING, "MDC1200 requires type and unitid to be specified!!\n");
		ast_free(tmp);
		return -1;
	}

	destid = 0;
	subcode = 0;
	if (args.type[0] == 'C') {
		if ((!args.destid) || (!args.subcode)) {
			ast_log(LOG_WARNING, "MDC1200(C) requires destid and subtype to be specified!!\n");
			ast_free(tmp);
			return -1;
		}
		destid = (short) strtol(args.destid, NULL, 16);
		subcode = (short) strtol(args.subcode, NULL, 16);
	}
	u = ast_module_user_add(chan);
	unitid = (short) strtol(args.unit, NULL, 16) & 0xffff;
	res = mdc1200gen(chan, args.type, unitid, destid, subcode);
	ast_free(tmp);
	ast_module_user_remove(u);
	return res;
}

static char *mdc_app = "MDC1200Gen";
#endif

int mdc1200_load(void)
{
#ifdef	_MDC_ENCODE_H_
	return ast_register_application_xml(mdc_app, mdcgen_exec);
#else
	return 0;
#endif
}

int mdc1200_unload(void)
{
#ifdef	_MDC_ENCODE_H_
	return ast_unregister_application(mdc_app);
#else
	return 0;
#endif
}
