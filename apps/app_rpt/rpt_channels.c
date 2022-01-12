
#include "asterisk.h"

#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/callerid.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/features.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/config.h"
#include "asterisk/say.h"
#include "asterisk/localtime.h"
#include "asterisk/cdr.h"
#include "asterisk/options.h"
#include "asterisk/manager.h"
#include "asterisk/astdb.h"
#include "asterisk/app.h"
#include "asterisk/indications.h"
#include "asterisk/format.h"
#include "asterisk/format_compatibility.h"

#include "app_rpt.h"
#include "rpt_channels.h"

/*
 * Multi-thread safe sleep routine
*/
void rpt_safe_sleep(struct rpt *rpt,struct ast_channel *chan, int ms)
{
	struct ast_frame *f;
	struct ast_channel *cs[2],*w;

	cs[0] = rpt->rxchannel;
	cs[1] = chan;
	while (ms > 0) {
		w = ast_waitfor_n(cs,2,&ms);
		if (!w) break;
		f = ast_read(w);
		if (!f) break;
		if ((w == cs[0]) && (f->frametype != AST_FRAME_VOICE) && (f->frametype != AST_FRAME_NULL))
		{
			ast_queue_frame(rpt->rxchannel,f);
			ast_frfree(f);
			break;
		}
		ast_frfree(f);
	}
	return;
}

/*
 * Routine to forward a "call" from one channel to another
*/

void rpt_forward(struct ast_channel *chan, char *dialstr, char *nodefrom)
{

struct ast_channel *dest,*w,*cs[2];
struct ast_frame *f;
int	ms;
struct ast_format_cap *cap;

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		return;
	}

	ast_format_cap_append(cap, ast_format_slin, 0);

	dest = ast_request("IAX2", cap, NULL, NULL, dialstr ,NULL);
	if (!dest)
	{
		if (ast_safe_sleep(chan,150) == -1) return;
		dest = ast_request("IAX2", cap, NULL, NULL, dialstr ,NULL);
		if (!dest)
		{
			ast_log(LOG_ERROR,"Can not create channel for rpt_forward to IAX2/%s\n",dialstr);
			return;
		}
	}
	ast_set_read_format(chan, ast_format_slin);
	ast_set_write_format(chan, ast_format_slin);
	ast_set_read_format(dest, ast_format_slin);
	ast_set_write_format(dest, ast_format_slin);
	ao2_ref(cap, -1);

	if (option_verbose > 2)
		ast_verb(3, "rpt forwarding call from %s to %s on %s\n", nodefrom, dialstr, ast_channel_name(dest));
	ast_set_callerid(dest,nodefrom,ast_channel_caller(chan)->id.name.str,nodefrom);
	ast_call(dest,dialstr,999); 
	cs[0] = chan;
	cs[1] = dest;
	for(;;)
	{
		if (ast_check_hangup(chan)) break;
		if (ast_check_hangup(dest)) break;
		ms = 100;
		w = cs[0];
		cs[0] = cs[1];
		cs[1] = w;
		w = ast_waitfor_n(cs,2,&ms);
		if (!w) continue;
		if (w == chan)
		{
			f = ast_read(chan);
			if (!f) break;
			if ((f->frametype == AST_FRAME_CONTROL) &&
			    (f->subclass.integer == AST_CONTROL_HANGUP))
			{
				ast_frfree(f);
				break;
			}
			ast_write(dest,f);
			ast_frfree(f);
		}
		if (w == dest)
		{
			f = ast_read(dest);
			if (!f) break;
			if ((f->frametype == AST_FRAME_CONTROL) &&
			    (f->subclass.integer == AST_CONTROL_HANGUP))
			{
				ast_frfree(f);
				break;
			}
			ast_write(chan,f);
			ast_frfree(f);
		}

	}
	ast_hangup(dest);
	return;
}
