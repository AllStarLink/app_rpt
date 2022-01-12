
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
#include "rpt_mdc.h"

#ifdef	_MDC_DECODE_H_
static const char *my_variable_match(const struct ast_config *config, const char *category, const char *variable)
{
	struct ast_variable *v;

	if (category)
	{
		for (v = ast_variable_browse(config, category); v; v = v->next)
		{
			if (!fnmatch(v->name,variable,FNM_CASEFOLD | FNM_NOESCAPE))
				return v->value;
		}

	}
	return NULL;
}
#endif

void mdc1200_notify(struct rpt *myrpt,char *fromnode, char *data)
{
	FILE *fp;
	char str[50];
	struct flock fl;
	time_t	t;

	rpt_manager_trigger(myrpt, "MDC-1200", data);

	if (!fromnode)
	{
		ast_verbose("Got MDC-1200 data %s from local system (%s)\n",
			data,myrpt->name);
		if (myrpt->p.mdclog) 
		{
			fp = fopen(myrpt->p.mdclog,"a");
			if (!fp)
			{
				ast_log(LOG_ERROR,"Cannot open MDC1200 log file %s\n",myrpt->p.mdclog);
				return;
			}
			fl.l_type = F_WRLCK;
			fl.l_whence = SEEK_SET;
			fl.l_start = 0;
			fl.l_len = 0;
			fl.l_pid = pthread_self();
			if (fcntl(fileno(fp),F_SETLKW,&fl) == -1)
			{
				ast_log(LOG_ERROR,"Cannot get lock on MDC1200 log file %s\n",myrpt->p.mdclog);			
				fclose(fp);
				return;
			}
			time(&t);
			strftime(str,sizeof(str) - 1,"%Y%m%d%H%M%S",localtime(&t));
			fprintf(fp,"%s %s %s\n",str ,myrpt->name,data);
			fl.l_type = F_UNLCK;
			fcntl(fileno(fp),F_SETLK,&fl);
			fclose(fp);
		}
	}
	else
	{
		ast_verbose("Got MDC-1200 data %s from node %s (%s)\n",
			data,fromnode,myrpt->name);
	}
}

#ifdef	_MDC_DECODE_H_

void mdc1200_send(struct rpt *myrpt, char *data)
{
struct rpt_link *l;
struct	ast_frame wf;
char	str[200];


	if (!myrpt->keyed) return;

	sprintf(str,"I %s %s",myrpt->name,data);

	wf.frametype = AST_FRAME_TEXT;
	wf.subclass.integer = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.data.ptr = str;
	wf.datalen = strlen(str) + 1;  // Isuani, 20141001
	wf.samples = 0;
	wf.src = "mdc1200_send";


	l = myrpt->links.next;
	/* otherwise, send it to all of em */
	while(l != &myrpt->links)
	{
		/* Dont send to IAXRPT client, unless main channel is Voter */
		if (((l->name[0] == '0') && strncasecmp(ast_channel_name(myrpt->rxchannel),"voter/", 6)) || (l->phonemode))
		{
			l = l->next;
			continue;
		}
		if (l->chan) rpt_qwrite(l,&wf); 
		l = l->next;
	}
	return;
}

void rssi_send(struct rpt *myrpt)
{
        struct rpt_link *l;
        struct	ast_frame wf;
        char	str[200];
	sprintf(str,"R %i",myrpt->rxrssi);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass.integer = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	wf.src = "rssi_send";

	l = myrpt->links.next;
	/* otherwise, send it to all of em */
	while(l != &myrpt->links)
	{
		if (l->name[0] == '0')
		{
			l = l->next;
			continue;
		}
		wf.data.ptr = str;
		if(debug>5)ast_log(LOG_NOTICE, "[%s] rssi=%i to %s\n", myrpt->name,myrpt->rxrssi,l->name);
		if (l->chan) rpt_qwrite(l,&wf);
		l = l->next;
	}
}

void mdc1200_cmd(struct rpt *myrpt, char *data)
{
	char busy,*myval;
	int i;

	busy = 0;
	if ((data[0] == 'I') && (!strcmp(data,myrpt->lastmdc))) return;
	myval = (char *) my_variable_match(myrpt->cfg, myrpt->p.mdcmacro, data);
	if (myval) 
	{
		if (option_verbose) ast_verbose("MDCMacro for %s doing %s on node %s\n",data,myval,myrpt->name);
		if ((*myval == 'K') || (*myval == 'k'))
		{
			if (!myrpt->keyed)
			{
				for(i = 1; myval[i]; i++) local_dtmfkey_helper(myrpt,myval[i]);
			}
			return;
		}
		if (!myrpt->keyed) return;
		rpt_mutex_lock(&myrpt->lock);
		if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(myval))
		{
			rpt_mutex_unlock(&myrpt->lock);
			busy=1;
		}
		if(!busy)
		{
			myrpt->macrotimer = MACROTIME;
			strncat(myrpt->macrobuf,myval,MAXMACRO - 1);
		}
		rpt_mutex_unlock(&myrpt->lock);
	}
 	if ((data[0] == 'I') && (!busy)) strcpy(myrpt->lastmdc,data);
	return;
}

#ifdef	_MDC_ENCODE_H_

void mdc1200_ack_status(struct rpt *myrpt, short UnitID)
{
struct	mdcparams *mdcp;

	mdcp = ast_calloc(1,sizeof(struct mdcparams));
	if (!mdcp)
	{
		ast_log(LOG_ERROR,"Cannot alloc!!\n");
		return;
	}
	memset(mdcp,0,sizeof(&mdcp));
	mdcp->type[0] = 'A';
	mdcp->UnitID = UnitID;
	rpt_telemetry(myrpt,MDC1200,(void *)mdcp);
	return;
}

#endif
#endif