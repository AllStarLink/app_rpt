
#include "asterisk.h"

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <search.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/io.h>
#include <sys/vfs.h>
#include <math.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fnmatch.h>
#include <curl/curl.h>
#include <termios.h>

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
#include "asterisk/indications.h"

#include "rpt_dsp.h" /* must come before app_rpt.h */
#include "app_rpt.h"
#include "rpt_utils.h"
#include "rpt_lock.h"
#include "rpt_serial.h"
#include "rpt_channels.h"
#include "rpt_remotedata.h"
#include "rpt_mdc.h"
#include "rpt_telemetry.h" /* use function_meter */

extern int debug;
extern int nrpts;
extern struct rpt rpt_vars[MAXRPTS];

/*! \todo these should be #define's */
extern char *discstr;
extern char *newkeystr;
extern char *newkey1str;
extern char *iaxkeystr;
extern char* dtmf_tones[];
static char *remote_rig_kenwood="kenwood";
static char *remote_rig_tm271="tm271";

/*! \todo duplicated from app_rpt.c */
/*
* Function table
*/
static struct function_table_tag function_table[] = {
	{"cop", function_cop},
	{"autopatchup", function_autopatchup},
	{"autopatchdn", function_autopatchdn},
	{"ilink", function_ilink},
	{"status", function_status},
	{"remote", function_remote},
	{"macro", function_macro},
	{"playback", function_playback},
	{"localplay", function_localplay},
	{"meter", function_meter},
	{"userout", function_userout},
	{"cmd", function_cmd}
};

/*
* Queue announcment that scan has been stopped 
*/

static void stop_scan(struct rpt *myrpt)
{
	myrpt->hfscanstop = 1;
	rpt_telemetry(myrpt,SCAN,0);
}

/*
 * Function to translate characters to APRSTT data
 */
static char aprstt_xlat(char *instr,char *outstr)
{
int	i,j;
char	b,c,lastnum,overlay,cksum;
static char a_xlat[] = {0,0,'A','D','G','J','M','P','T','W'};
static char b_xlat[] = {0,0,'B','E','H','K','N','Q','U','X'};
static char c_xlat[] = {0,0,'C','F','I','L','O','R','V','Y'};
static char d_xlat[] = {0,0,0,0,0,0,0,'S',0,'Z'};

	if (strlen(instr) < 4) return 0;
	lastnum = 0;
	for(i = 1; instr[i + 2]; i++)
	{
		c = instr[i];
		switch (c)
		{
		    case 'A' :
			if (!lastnum) return 0;
			b = a_xlat[lastnum - '0'];
			if (!b) return 0;
			*outstr++ = b;
			lastnum = 0;
			break;
		    case 'B' :
			if (!lastnum) return 0;
			b = b_xlat[lastnum - '0'];
			if (!b) return 0;
			*outstr++ = b;
			lastnum = 0;
			break;
		    case 'C' :
			if (!lastnum) return 0;
			b = c_xlat[lastnum - '0'];
			if (!b) return 0;
			*outstr++ = b;
			lastnum = 0;
			break;
		    case 'D' :
			if (!lastnum) return 0;
			b = d_xlat[lastnum - '0'];
			if (!b) return 0;
			*outstr++ = b;
			lastnum = 0;
			break;
		    case '0':
		    case '1':
		    case '2':
		    case '3':
		    case '4':
		    case '5':
		    case '6':
		    case '7':
		    case '8':
		    case '9':
			if (lastnum) *outstr++ = lastnum;
			lastnum = c;
			break;
		    default:
			return 0;
		}
	}
	*outstr = 0;
	overlay = instr[i++];
	cksum = instr[i];	
	for(i = 0,j = 0; instr[i + 1]; i++)
	{
		if ((instr[i] >= '0') && (instr[i] <= '9')) j += (instr[i] - '0');
		else if ((instr[i] >= 'A') && (instr[i] <= 'D')) j += (instr[i] - 'A') + 10;
	}
	if ((cksum - '0') != (j % 10)) return 0;
	return overlay;
}

static void send_link_dtmf(struct rpt *myrpt,char c)
{
char	str[300];
struct	ast_frame wf;
struct	rpt_link *l;


	snprintf(str, sizeof(str), "D %s %s %d %c", myrpt->cmdnode, myrpt->name, ++(myrpt->dtmfidx), c);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass.integer = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	wf.src = "send_link_dtmf";
	l = myrpt->links.next;
	/* first, see if our dude is there */
	while(l != &myrpt->links)
	{
		if (l->name[0] == '0') 
		{
			l = l->next;
			continue;
		}
		/* if we found it, write it and were done */
		if (!strcmp(l->name,myrpt->cmdnode))
		{
			wf.data.ptr = str;
			if (l->chan) rpt_qwrite(l,&wf);
			return;
		}
		l = l->next;
	}
	l = myrpt->links.next;
	/* if not, give it to everyone */
	while(l != &myrpt->links)
	{
		wf.data.ptr = str;
		if (l->chan) rpt_qwrite(l,&wf);
		l = l->next;
	}
	return;
}

static void do_dtmf_phone(struct rpt *myrpt, struct rpt_link *mylink, char c)
{
struct        rpt_link *l;

       l = myrpt->links.next;
       /* go thru all the links */
       while(l != &myrpt->links)
       {
               if (!l->phonemode)
               {
                       l = l->next;
                       continue;
               }
               /* dont send to self */
               if (mylink && (l == mylink))
               {
                       l = l->next;
                       continue;
               }
               if (l->chan) ast_senddigit(l->chan,c,0);
               l = l->next;
       }
       return;
}

/*
* Collect digits one by one until something matches
*/
static int collect_function_digits(struct rpt *myrpt, char *digits, 
	int command_source, struct rpt_link *mylink)
{
	int i,rv;
	char *stringp,*action,*param,*functiondigits;
	char function_table_name[30] = "";
	char workstring[200];
	
	struct ast_variable *vp;
	
	if (debug > 6) ast_log(LOG_NOTICE,"digits=%s  source=%d\n",digits, command_source);

	//if(debug)	
	//	printf("@@@@ Digits collected: %s, source: %d\n", digits, command_source);
	
	if (command_source == SOURCE_DPHONE) {
		if (!myrpt->p.dphone_functions) return DC_INDETERMINATE;
		strncpy(function_table_name, myrpt->p.dphone_functions, sizeof(function_table_name) - 1);
		}
	else if (command_source == SOURCE_ALT) {
		if (!myrpt->p.alt_functions) return DC_INDETERMINATE;
		strncpy(function_table_name, myrpt->p.alt_functions, sizeof(function_table_name) - 1);
		}
	else if (command_source == SOURCE_PHONE) {
		if (!myrpt->p.phone_functions) return DC_INDETERMINATE;
		strncpy(function_table_name, myrpt->p.phone_functions, sizeof(function_table_name) - 1);
		}
	else if (command_source == SOURCE_LNK)
		strncpy(function_table_name, myrpt->p.link_functions, sizeof(function_table_name) - 1);
	else
		strncpy(function_table_name, myrpt->p.functions, sizeof(function_table_name) - 1);
    /* find context for function table in rpt.conf file */
	vp = ast_variable_browse(myrpt->cfg, function_table_name);
	while(vp) {
		if(!strncasecmp(vp->name, digits, strlen(vp->name)))
			break;
		vp = vp->next;
	}	
	/* if function context not found */
	if(!vp) {
		int n;

		n = myrpt->longestfunc;
		if (command_source == SOURCE_LNK) n = myrpt->link_longestfunc;
		else 
		if (command_source == SOURCE_PHONE) n = myrpt->phone_longestfunc;
		else 
		if (command_source == SOURCE_ALT) n = myrpt->alt_longestfunc;
		else 
		if (command_source == SOURCE_DPHONE) n = myrpt->dphone_longestfunc;
		
		if(strlen(digits) >= n)
			return DC_ERROR;
		else
			return DC_INDETERMINATE;
	}	
	/* Found a match, retrieve value part and parse */
	strncpy(workstring, vp->value, sizeof(workstring) - 1 );
	stringp = workstring;
	action = strsep(&stringp, ",");
	param = stringp;
	if(debug)
		printf("@@@@ action: %s, param = %s\n",action, (param) ? param : "(null)");
	/* Look up the action */
	for(i = 0 ; i < (sizeof(function_table)/sizeof(struct function_table_tag)); i++){
		if(!strncasecmp(action, function_table[i].action, strlen(action)))
			break;
	}
	if(debug)
		printf("@@@@ table index i = %d\n",i);
	if(i == (sizeof(function_table)/sizeof(struct function_table_tag))){
		/* Error, action not in table */
		return DC_ERROR;
	}
	if(function_table[i].function == NULL){
		/* Error, function undefined */
		if(debug)
			printf("@@@@ NULL for action: %s\n",action);
		return DC_ERROR;
	}
	functiondigits = digits + strlen(vp->name);
	rv=(*function_table[i].function)(myrpt, param, functiondigits, command_source, mylink);
	if (debug > 6) ast_log(LOG_NOTICE,"rv=%i\n",rv);
	return(rv);
}

/* send newkey request */
void send_newkey(struct ast_channel *chan)
{

	ast_sendtext(chan,newkey1str);
	return;
}

void send_old_newkey(struct ast_channel *chan)
{
	ast_sendtext(chan,newkeystr);
	return;
}

void handle_link_data(struct rpt *myrpt, struct rpt_link *mylink, char *str)
{
char	tmp[512],tmp1[512],cmd[300] = "",dest[300],src[30],c;
int	i,seq, res, ts, rest;
struct rpt_link *l;
struct	ast_frame wf;

	wf.frametype = AST_FRAME_TEXT;
	wf.subclass.integer = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	wf.src = "handle_link_data";
 	/* put string in our buffer */
	strncpy(tmp,str,sizeof(tmp) - 1);

        if (!strcmp(tmp,discstr))
        {
                mylink->disced = 1;
		mylink->retries = mylink->max_retries + 1;
                ast_softhangup(mylink->chan,AST_SOFTHANGUP_DEV);
                return;
        }
        if (!strcmp(tmp,newkeystr))
        {
		if ((!mylink->newkey) || mylink->newkeytimer)
		{
			mylink->newkeytimer = 0;
			mylink->newkey = 1;
			send_old_newkey(mylink->chan);
		}
                return;
        }
        if (!strcmp(tmp,newkey1str))
        {
		mylink->newkeytimer = 0;
		mylink->newkey = 2;
                return;
        }
        if (!strncmp(tmp,iaxkeystr,strlen(iaxkeystr)))
        {
		mylink->iaxkey = 1;
                return;
        }
	if (tmp[0] == 'G') /* got GPS data */
	{
		/* re-distriutee it to attached nodes */
		l = myrpt->links.next;
		/* otherwise, send it to all of em */
		while(l != &myrpt->links)
		{
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name,mylink->name)))
			{
				l = l->next;
				continue;
			}
			/* send, but not to src */
			if (strcmp(l->name,src)) {
				wf.data.ptr = str;
				if (l->chan) rpt_qwrite(l,&wf); 
			}
			l = l->next;
		}
		return;
	}
	if (tmp[0] == 'L')
	{
		rpt_mutex_lock(&myrpt->lock);
		strcpy(mylink->linklist,tmp + 2);
		time(&mylink->linklistreceived);
		rpt_mutex_unlock(&myrpt->lock);
		if (debug > 6) ast_log(LOG_NOTICE,"@@@@ node %s recieved node list %s from node %s\n",
			myrpt->name,tmp,mylink->name);
		return;
	}
	if (tmp[0] == 'M')
	{
		rest = 0;
		if (sscanf(tmp,"%s %s %s %n",cmd,src,dest,&rest) < 3)
		{
			ast_log(LOG_WARNING, "Unable to parse message string %s\n",str);
			return;
		}
		if (!rest) return;
		if (strlen(tmp + rest) < 2) return;
		/* if is from me, ignore */
		if (!strcmp(src,myrpt->name)) return;
		/* if is for one of my nodes, dont do too much! */
	        for(i = 0; i < nrpts; i++)
		{
	                if(!strcmp(dest, rpt_vars[i].name))
			{
				ast_verbose("Private Text Message for %s From %s: %s\n",
					rpt_vars[i].name,src,tmp + rest);
				ast_log(LOG_NOTICE,"Node %s Got Private Text Message From Node %s: %s\n",
					rpt_vars[i].name,src,tmp + rest);
				return;
			}
		}
		/* if is for everyone, at least log it */
		if (!strcmp(dest,"0"))
		{
			ast_verbose("Text Message From %s: %s\n",src,tmp + rest);
			ast_log(LOG_NOTICE,"Node %s Got Text Message From Node %s: %s\n",
				myrpt->name,src,tmp + rest);
		}
		l = myrpt->links.next;
		/* otherwise, send it to all of em */
		while(l != &myrpt->links)
		{
			if (l->name[0] == '0') 
			{
				l = l->next;
				continue;
			}
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name,mylink->name)))
			{
				l = l->next;
				continue;
			}
			/* send, but not to src */
			if (strcmp(l->name,src)) {
				wf.data.ptr = str;
				if (l->chan) rpt_qwrite(l,&wf); 
			}
			l = l->next;
		}
		return;
	}
	if (tmp[0] == 'T')
	{
		if (sscanf(tmp,"%s %s %s",cmd,src,dest) != 3)
		{
			ast_log(LOG_WARNING, "Unable to parse telem string %s\n",str);
			return;
		}
		l = myrpt->links.next;
		/* otherwise, send it to all of em */
		while(l != &myrpt->links)
		{
			if (l->name[0] == '0') 
			{
				l = l->next;
				continue;
			}
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name,mylink->name)))
			{
				l = l->next;
				continue;
			}
			/* send, but not to src */
			if (strcmp(l->name,src)) {
				wf.data.ptr = str;
				if (l->chan) rpt_qwrite(l,&wf); 
			}
			l = l->next;
		}
		/* if is from me, ignore */
		if (!strcmp(src,myrpt->name)) return;

		/* if is a RANGER node, only allow CONNECTED message that directly involve our node */
		if (ISRANGER(myrpt->name) && (strncasecmp(dest,"CONNECTED,",10) ||
			(!strstr(dest,myrpt->name)))) return;

		/* set 'got T message' flag */
		mylink->gott = 1;

		/*  If inbound telemetry from a remote node, wake up from sleep if sleep mode is enabled */
		rpt_mutex_lock(&myrpt->lock); /* LOCK */
		if(myrpt->p.s[myrpt->p.sysstate_cur].sleepena){
			myrpt->sleeptimer = myrpt->p.sleeptime;
			if(myrpt->sleep){
				myrpt->sleep = 0;
			}
		}
		rpt_mutex_unlock(&myrpt->lock); /* UNLOCK */

		rpt_telemetry(myrpt,VARCMD,dest);
		return;
	}

	if (tmp[0] == 'C')
	{
		if (sscanf(tmp,"%s %s %s %s",cmd,src,tmp1,dest) != 4)
		{
			ast_log(LOG_WARNING, "Unable to parse ctcss string %s\n",str);
			return;
		}
		if (!strcmp(myrpt->p.ctgroup,"0")) return;
		if (strcasecmp(myrpt->p.ctgroup,tmp1)) return;
		l = myrpt->links.next;
		/* otherwise, send it to all of em */
		while(l != &myrpt->links)
		{
			if (l->name[0] == '0') 
			{
				l = l->next;
				continue;
			}
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name,mylink->name)))
			{
				l = l->next;
				continue;
			}
			/* send, but not to src */
			if (strcmp(l->name,src)) {
				wf.data.ptr = str;
				if (l->chan) rpt_qwrite(l,&wf); 
			}
			l = l->next;
		}
		/* if is from me, ignore */
		if (!strcmp(src,myrpt->name)) return;
		sprintf(cmd,"TXTONE %.290s",dest);
		if (IS_XPMR(myrpt)) send_usb_txt(myrpt,cmd);
		return;
	}

	if (tmp[0] == 'K')
	{
		if (sscanf(tmp,"%s %s %s %d %d",cmd,dest,src,&seq,&ts) != 5)
		{
			ast_log(LOG_WARNING, "Unable to parse keying string %s\n",str);
			return;
		}
		if (dest[0] == '0')
		{
			strcpy(dest,myrpt->name);
		}		
		/* if not for me, redistribute to all links */
		if (strcmp(dest,myrpt->name))
		{
			l = myrpt->links.next;
			/* see if this is one in list */
			while(l != &myrpt->links)
			{
				if (l->name[0] == '0') 
				{
					l = l->next;
					continue;
				}
				/* dont send back from where it came */
				if ((l == mylink) || (!strcmp(l->name,mylink->name)))
				{
					l = l->next;
					continue;
				}
				/* if it is, send it and we're done */
				if (!strcmp(l->name,dest))
				{
					/* send, but not to src */
					if (strcmp(l->name,src)) {
						wf.data.ptr = str;
						if (l->chan) rpt_qwrite(l,&wf);
					}
					return;
				}
				l = l->next;
			}
		}
		/* if not for me, or is broadcast, redistribute to all links */
		if ((strcmp(dest,myrpt->name)) || (dest[0] == '*'))
		{
			l = myrpt->links.next;
			/* otherwise, send it to all of em */
			while(l != &myrpt->links)
			{
				if (l->name[0] == '0') 
				{
					l = l->next;
					continue;
				}
				/* dont send back from where it came */
				if ((l == mylink) || (!strcmp(l->name,mylink->name)))
				{
					l = l->next;
					continue;
				}
				/* send, but not to src */
				if (strcmp(l->name,src)) {
					wf.data.ptr = str;
					if (l->chan) rpt_qwrite(l,&wf); 
				}
				l = l->next;
			}
		}
		/* if not for me, end here */
		if (strcmp(dest,myrpt->name) && (dest[0] != '*')) return;
		if (cmd[1] == '?')
		{
			time_t now;
			int n = 0;

			time(&now);
			if (myrpt->lastkeyedtime)
			{
				n = (int)(now - myrpt->lastkeyedtime);
			}
			sprintf(tmp1,"K %s %s %d %d",src,myrpt->name,myrpt->keyed,n);
			wf.data.ptr= tmp1;
			wf.datalen = strlen(tmp1) + 1;
			if (mylink->chan) rpt_qwrite(mylink,&wf); 
			return;
		}
		if (myrpt->topkeystate != 1) return;
		rpt_mutex_lock(&myrpt->lock);
		for(i = 0; i < TOPKEYN; i++)
		{
			if (!strcmp(myrpt->topkey[i].node,src)) break;
		}
		if (i >= TOPKEYN)
		{
			for(i = 0; i < TOPKEYN; i++)
			{
				if (!myrpt->topkey[i].node[0]) break;
			}
		}
		if (i < TOPKEYN)
		{
			ast_copy_string(myrpt->topkey[i].node,src,TOPKEYMAXSTR - 1);
			myrpt->topkey[i].timesince = ts;
			myrpt->topkey[i].keyed = seq;
		}
		rpt_mutex_unlock(&myrpt->lock);
		return;
	}
	if (tmp[0] == 'I')
	{
		if (sscanf(tmp,"%s %s %s",cmd,src,dest) != 3)
		{
			ast_log(LOG_WARNING, "Unable to parse ident string %s\n",str);
			return;
		}
		mdc1200_notify(myrpt,src,dest);
		strcpy(dest,"*");
	}
	else
	{
		if (sscanf(tmp,"%s %s %s %d %c",cmd,dest,src,&seq,&c) != 5)
		{
			ast_log(LOG_WARNING, "Unable to parse link string %s\n",str);
			return;
		}
		if (strcmp(cmd,"D"))
		{
			ast_log(LOG_WARNING, "Unable to parse link string %s\n",str);
			return;
		}
	}
	if (dest[0] == '0')
	{
		strcpy(dest,myrpt->name);
	}		

	/* if not for me, redistribute to all links */
	if (strcmp(dest,myrpt->name))
	{
		l = myrpt->links.next;
		/* see if this is one in list */
		while(l != &myrpt->links)
		{
			if (l->name[0] == '0') 
			{
				l = l->next;
				continue;
			}
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name,mylink->name)))
			{
				l = l->next;
				continue;
			}
			/* if it is, send it and we're done */
			if (!strcmp(l->name,dest))
			{
				/* send, but not to src */
				if (strcmp(l->name,src)) {
					wf.data.ptr = str;
					if (l->chan) rpt_qwrite(l,&wf);
				}
				return;
			}
			l = l->next;
		}
		l = myrpt->links.next;
		/* otherwise, send it to all of em */
		while(l != &myrpt->links)
		{
			if (l->name[0] == '0') 
			{
				l = l->next;
				continue;
			}
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name,mylink->name)))
			{
				l = l->next;
				continue;
			}
			/* send, but not to src */
			if (strcmp(l->name,src)) {
				wf.data.ptr = str;
				if (l->chan) rpt_qwrite(l,&wf); 
			}
			l = l->next;
		}
		return;
	}
	if (myrpt->p.archivedir)
	{
		char str[512];

		sprintf(str,"DTMF,%s,%c",mylink->name,c);
		donodelog(myrpt,str);
	}
	c = func_xlat(myrpt,c,&myrpt->p.outxlat);
	if (!c) return;
	rpt_mutex_lock(&myrpt->lock);
	if ((iswebtransceiver(mylink)) ||  /* if a WebTransceiver node */
		(!strncasecmp(ast_channel_name(mylink->chan),"tlb",3)))  /* or a tlb node */
	{
		if (c == myrpt->p.endchar) myrpt->cmdnode[0] = 0;
		else if (myrpt->cmdnode[0])
		{
			cmd[0] = 0;
			if (!strcmp(myrpt->cmdnode,"aprstt"))
			{
				char overlay,aprscall[100],fname[100];
				FILE *fp;

				snprintf(cmd, sizeof(cmd) - 1,"A%s", myrpt->dtmfbuf);
				overlay = aprstt_xlat(cmd,aprscall);
				if (overlay)
				{
					if (debug) ast_log(LOG_WARNING,"aprstt got string %s call %s overlay %c\n",cmd,aprscall,overlay);
					if (!myrpt->p.aprstt[0]) ast_copy_string(fname,APRSTT_PIPE,sizeof(fname) - 1);
					else snprintf(fname,sizeof(fname) - 1,APRSTT_SUB_PIPE,myrpt->p.aprstt);
					fp = fopen(fname,"w");
					if (!fp)
					{
						ast_log(LOG_WARNING,"Can not open APRSTT pipe %s\n",fname);
					}
					else
					{
						fprintf(fp,"%s %c\n",aprscall,overlay);
						fclose(fp);
						rpt_telemetry(myrpt, ARB_ALPHA, (void *) aprscall);
					}
				}
			}
			rpt_mutex_unlock(&myrpt->lock);
			if (strcmp(myrpt->cmdnode,"aprstt")) send_link_dtmf(myrpt,c);
			return;
		}
	}		
	if (c == myrpt->p.endchar) myrpt->stopgen = 1;
	if (myrpt->callmode == 1)
	{
		myrpt->exten[myrpt->cidx++] = c;
		myrpt->exten[myrpt->cidx] = 0;
		/* if this exists */
		if (ast_exists_extension(myrpt->pchannel,myrpt->patchcontext,myrpt->exten,1,NULL))
		{
			/* if this really it, end now */
			if (!ast_matchmore_extension(myrpt->pchannel,myrpt->patchcontext,
				myrpt->exten,1,NULL)) 
			{
				myrpt->callmode = 2;
				if(!myrpt->patchquiet)
				{
					rpt_mutex_unlock(&myrpt->lock);
					rpt_telemetry(myrpt,PROC,NULL); 
					rpt_mutex_lock(&myrpt->lock);
				}
			}
			else /* othewise, reset timer */
			{
				myrpt->calldigittimer = 1;
			}
		}
		/* if can continue, do so */
		if (!ast_canmatch_extension(myrpt->pchannel,myrpt->patchcontext,myrpt->exten,1,NULL))
		{
			/* call has failed, inform user */
			myrpt->callmode = 4;
		}
	}
	if ((!myrpt->inpadtest) && myrpt->p.aprstt && (!myrpt->cmdnode[0]) && (c == 'A'))
	{
		strcpy(myrpt->cmdnode,"aprstt");
		myrpt->dtmfidx = 0;
		myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
		rpt_mutex_unlock(&myrpt->lock);
		time(&myrpt->dtmf_time);
		return;
	}
	if ((!myrpt->inpadtest) && (c == myrpt->p.funcchar))
	{
		myrpt->rem_dtmfidx = 0;
		myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
		time(&myrpt->rem_dtmf_time);
		rpt_mutex_unlock(&myrpt->lock);
		return;
	} 
	else if (myrpt->rem_dtmfidx < 0)
	{
		if ((myrpt->callmode == 2) || (myrpt->callmode == 3))
		{
			myrpt->mydtmf = c;
		}
		if (myrpt->p.propagate_dtmf) do_dtmf_local(myrpt,c);
		if (myrpt->p.propagate_phonedtmf) do_dtmf_phone(myrpt,mylink,c);
		rpt_mutex_unlock(&myrpt->lock);
		return;
	}
	else if (((myrpt->inpadtest) || (c != myrpt->p.endchar)) && (myrpt->rem_dtmfidx >= 0))
	{
		time(&myrpt->rem_dtmf_time);
		if (myrpt->rem_dtmfidx < MAXDTMF)
		{
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx++] = c;
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
			
			rpt_mutex_unlock(&myrpt->lock);
			strncpy(cmd, myrpt->rem_dtmfbuf, sizeof(cmd) - 1);
			res = collect_function_digits(myrpt, cmd, SOURCE_LNK, mylink);
			rpt_mutex_lock(&myrpt->lock);
			
			switch(res){

				case DC_INDETERMINATE:
					break;
				
				case DC_REQ_FLUSH:
					myrpt->rem_dtmfidx = 0;
					myrpt->rem_dtmfbuf[0] = 0;
					break;
				
				
				case DC_COMPLETE:
				case DC_COMPLETEQUIET:
					myrpt->totalexecdcommands++;
					myrpt->dailyexecdcommands++;
					strncpy(myrpt->lastdtmfcommand, cmd, MAXDTMF-1);
					myrpt->lastdtmfcommand[MAXDTMF-1] = '\0';
					myrpt->rem_dtmfbuf[0] = 0;
					myrpt->rem_dtmfidx = -1;
					myrpt->rem_dtmf_time = 0;
					break;
				
				case DC_ERROR:
				default:
					myrpt->rem_dtmfbuf[0] = 0;
					myrpt->rem_dtmfidx = -1;
					myrpt->rem_dtmf_time = 0;
					break;
			}
		}

	}
	rpt_mutex_unlock(&myrpt->lock);
	return;
}

void handle_link_phone_dtmf(struct rpt *myrpt, struct rpt_link *mylink, char c)
{

char	cmd[300];
int	res;

	if (myrpt->p.archivedir)
	{
		char str[512];

		sprintf(str,"DTMF(P),%s,%c",mylink->name,c);
		donodelog(myrpt,str);
	}
	if (mylink->phonemonitor) return;

	rpt_mutex_lock(&myrpt->lock);

	if (mylink->phonemode == 3) /*If in simplex dumb phone mode */
	{
		if(c == myrpt->p.endchar) /* If end char */
		{
			mylink->lastrealrx = 0; /* Keying state = off */
			rpt_mutex_unlock(&myrpt->lock);
			return;
		}

		if(c == myrpt->p.funcchar) /* If lead-in char */
		{
			mylink->lastrealrx = !mylink->lastrealrx; /* Toggle keying state */
			rpt_mutex_unlock(&myrpt->lock);
			return;
		}
	}
	else
	{
		if (c == myrpt->p.endchar)
		{
			if (mylink->lastrx && 
			    strncasecmp(ast_channel_name(mylink->chan),"echolink",8))
			{
				mylink->lastrealrx = 0;
				rpt_mutex_unlock(&myrpt->lock);
				return;
			}
			myrpt->stopgen = 1;
			if (myrpt->cmdnode[0])
			{
				cmd[0] = 0;
				if (!strcmp(myrpt->cmdnode,"aprstt"))
				{
					char overlay,aprscall[100],fname[100];
					FILE *fp;

					snprintf(cmd, sizeof(cmd) - 1,"A%s", myrpt->dtmfbuf);
					overlay = aprstt_xlat(cmd,aprscall);
					if (overlay)
					{
						if (debug) ast_log(LOG_WARNING,"aprstt got string %s call %s overlay %c\n",cmd,aprscall,overlay);
						if (!myrpt->p.aprstt[0]) ast_copy_string(fname,APRSTT_PIPE,sizeof(fname) - 1);
						else snprintf(fname,sizeof(fname) - 1,APRSTT_SUB_PIPE,myrpt->p.aprstt);
						fp = fopen(fname,"w");
						if (!fp)
						{
							ast_log(LOG_WARNING,"Can not open APRSTT pipe %s\n",fname);
						}
						else
						{
							fprintf(fp,"%s %c\n",aprscall,overlay);
							fclose(fp);
							rpt_telemetry(myrpt, ARB_ALPHA, (void *) aprscall);
						}
					}
				}
				myrpt->cmdnode[0] = 0;
				myrpt->dtmfidx = -1;
				myrpt->dtmfbuf[0] = 0;
				rpt_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt,COMPLETE,NULL);
				return;
			}
#if 0
			if ((myrpt->rem_dtmfidx < 0) && 
			    ((myrpt->callmode == 2) || (myrpt->callmode == 3)))
			{
				myrpt->mydtmf = c;
			}
#endif
		}
	}
	if (myrpt->cmdnode[0] && strcmp(myrpt->cmdnode,"aprstt"))
	{
		rpt_mutex_unlock(&myrpt->lock);
		send_link_dtmf(myrpt,c);
		return;
	}
	if (myrpt->callmode == 1)
	{
		myrpt->exten[myrpt->cidx++] = c;
		myrpt->exten[myrpt->cidx] = 0;
		/* if this exists */
		if (ast_exists_extension(myrpt->pchannel,myrpt->patchcontext,myrpt->exten,1,NULL))
		{
			/* if this really it, end now */
			if (!ast_matchmore_extension(myrpt->pchannel,myrpt->patchcontext,
				myrpt->exten,1,NULL)) 
			{
				myrpt->callmode = 2;
				if(!myrpt->patchquiet)
				{
					rpt_mutex_unlock(&myrpt->lock);
					rpt_telemetry(myrpt,PROC,NULL); 
					rpt_mutex_lock(&myrpt->lock);
				}
			}
			else /* othewise, reset timer */
			{
				myrpt->calldigittimer = 1;
			}
		}
		/* if can continue, do so */
		if (!ast_canmatch_extension(myrpt->pchannel,myrpt->patchcontext,myrpt->exten,1,NULL))
		{
			/* call has failed, inform user */
			myrpt->callmode = 4;
		}
	}
	if ((c != myrpt->p.funcchar) && (myrpt->rem_dtmfidx < 0) &&
	  (!myrpt->inpadtest) &&
	    ((myrpt->callmode == 2) || (myrpt->callmode == 3)))
	{
		myrpt->mydtmf = c;
	}
	if ((!myrpt->inpadtest) && myrpt->p.aprstt && (!myrpt->cmdnode[0]) && (c == 'A'))
	{
		strcpy(myrpt->cmdnode,"aprstt");
		myrpt->dtmfidx = 0;
		myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
		rpt_mutex_unlock(&myrpt->lock);
		time(&myrpt->dtmf_time);
		return;
	}
	if ((!myrpt->inpadtest) && (c == myrpt->p.funcchar))
	{
		myrpt->rem_dtmfidx = 0;
		myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
		time(&myrpt->rem_dtmf_time);
		rpt_mutex_unlock(&myrpt->lock);
		return;
	} 
	else if (((myrpt->inpadtest) || (c != myrpt->p.endchar)) && (myrpt->rem_dtmfidx >= 0))
	{
		time(&myrpt->rem_dtmf_time);
		if (myrpt->rem_dtmfidx < MAXDTMF)
		{
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx++] = c;
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
			
			rpt_mutex_unlock(&myrpt->lock);
			strncpy(cmd, myrpt->rem_dtmfbuf, sizeof(cmd) - 1);
			switch(mylink->phonemode)
			{
			    case 1:
				res = collect_function_digits(myrpt, cmd, 
					SOURCE_PHONE, mylink);
				break;
			    case 2:
				res = collect_function_digits(myrpt, cmd, 
					SOURCE_DPHONE,mylink);
				break;
			    case 4:
				res = collect_function_digits(myrpt, cmd, 
					SOURCE_ALT,mylink);
				break;
			    default:
				res = collect_function_digits(myrpt, cmd, 
					SOURCE_LNK, mylink);
				break;
			}

			rpt_mutex_lock(&myrpt->lock);
			
			switch(res){

				case DC_INDETERMINATE:
					break;
				
				case DC_DOKEY:
					mylink->lastrealrx = 1;
					break;
				
				case DC_REQ_FLUSH:
					myrpt->rem_dtmfidx = 0;
					myrpt->rem_dtmfbuf[0] = 0;
					break;
				
				
				case DC_COMPLETE:
				case DC_COMPLETEQUIET:
					myrpt->totalexecdcommands++;
					myrpt->dailyexecdcommands++;
					strncpy(myrpt->lastdtmfcommand, cmd, MAXDTMF-1);
					myrpt->lastdtmfcommand[MAXDTMF-1] = '\0';
					myrpt->rem_dtmfbuf[0] = 0;
					myrpt->rem_dtmfidx = -1;
					myrpt->rem_dtmf_time = 0;
					break;
				
				case DC_ERROR:
				default:
					myrpt->rem_dtmfbuf[0] = 0;
					myrpt->rem_dtmfidx = -1;
					myrpt->rem_dtmf_time = 0;
					break;
			}
		}

	}
        else if (myrpt->p.propagate_phonedtmf) do_dtmf_local(myrpt,c);
	rpt_mutex_unlock(&myrpt->lock);
	return;
}

/* must be called locked */
void do_dtmf_local(struct rpt *myrpt, char c)
{
int	i;
char	digit;

	if (c)
	{
		snprintf(myrpt->dtmf_local_str + strlen(myrpt->dtmf_local_str),sizeof(myrpt->dtmf_local_str) - 1,"%c",c);
		if (!myrpt->dtmf_local_timer) 
			 myrpt->dtmf_local_timer = DTMF_LOCAL_STARTTIME;
	}
	/* if at timeout */
	if (myrpt->dtmf_local_timer == 1)
	{
		if(debug > 6)
			ast_log(LOG_NOTICE,"time out dtmf_local_timer=%i\n",myrpt->dtmf_local_timer);

		/* if anything in the string */
		if (myrpt->dtmf_local_str[0])
		{
			digit = myrpt->dtmf_local_str[0];
			myrpt->dtmf_local_str[0] = 0;
			for(i = 1; myrpt->dtmf_local_str[i]; i++)
			{
				myrpt->dtmf_local_str[i - 1] =
					myrpt->dtmf_local_str[i];
			}
			myrpt->dtmf_local_str[i - 1] = 0;
			myrpt->dtmf_local_timer = DTMF_LOCAL_TIME;
			rpt_mutex_unlock(&myrpt->lock);
			if (!strncasecmp(ast_channel_name(myrpt->txchannel),"rtpdir",6))
			{
				ast_senddigit(myrpt->txchannel,digit,0);
			} 
			else
			{
				if (digit >= '0' && digit <='9')
					ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[digit-'0'], 0);
				else if (digit >= 'A' && digit <= 'D')
					ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[digit-'A'+10], 0);
				else if (digit == '*')
					ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[14], 0);
				else if (digit == '#')
					ast_playtones_start(myrpt->txchannel, 0, dtmf_tones[15], 0);
				else {
					/* not handled */
					ast_log(LOG_DEBUG, "Unable to generate DTMF tone '%c' for '%s'\n", digit, ast_channel_name(myrpt->txchannel));
				}
			}
			rpt_mutex_lock(&myrpt->lock);
		}
		else
		{
			myrpt->dtmf_local_timer = 0;
		}
	}
}

int handle_remote_dtmf_digit(struct rpt *myrpt,char c, char *keyed, int phonemode)
{
time_t	now;
int	ret,res = 0,src;

	if(debug > 6)
		ast_log(LOG_NOTICE,"c=%c  phonemode=%i  dtmfidx=%i\n",c,phonemode,myrpt->dtmfidx);

	time(&myrpt->last_activity_time);
	/* Stop scan mode if in scan mode */
	if(myrpt->hfscanmode){
		stop_scan(myrpt);
		return 0;
	}

	time(&now);
	/* if timed-out */
	if ((myrpt->dtmf_time_rem + DTMF_TIMEOUT) < now)
	{
		myrpt->dtmfidx = -1;
		myrpt->dtmfbuf[0] = 0;
		myrpt->dtmf_time_rem = 0;
	}
	/* if decode not active */
	if (myrpt->dtmfidx == -1)
	{
		/* if not lead-in digit, dont worry */
		if (c != myrpt->p.funcchar)
		{
			if (!myrpt->p.propagate_dtmf)
			{
				rpt_mutex_lock(&myrpt->lock);
				do_dtmf_local(myrpt,c);
				rpt_mutex_unlock(&myrpt->lock);
			}
			return 0;
		}
		myrpt->dtmfidx = 0;
		myrpt->dtmfbuf[0] = 0;
		myrpt->dtmf_time_rem = now;
		return 0;
	}
	/* if too many in buffer, start over */
	if (myrpt->dtmfidx >= MAXDTMF)
	{
		myrpt->dtmfidx = 0;
		myrpt->dtmfbuf[0] = 0;
		myrpt->dtmf_time_rem = now;
	}
	if (c == myrpt->p.funcchar)
	{
		/* if star at beginning, or 2 together, erase buffer */
		if ((myrpt->dtmfidx < 1) || 
			(myrpt->dtmfbuf[myrpt->dtmfidx - 1] == myrpt->p.funcchar))
		{
			myrpt->dtmfidx = 0;
			myrpt->dtmfbuf[0] = 0;
			myrpt->dtmf_time_rem = now;
			return 0;
		}
	}
	myrpt->dtmfbuf[myrpt->dtmfidx++] = c;
	myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
	myrpt->dtmf_time_rem = now;
	
	
	src = SOURCE_RMT;
	if (phonemode == 2) src = SOURCE_DPHONE;
	else if (phonemode) src = SOURCE_PHONE;
	else if (phonemode == 4) src = SOURCE_ALT;
	ret = collect_function_digits(myrpt, myrpt->dtmfbuf, src, NULL);
	
	switch(ret){
	
		case DC_INDETERMINATE:
			res = 0;
			break;
				
		case DC_DOKEY:
			if (keyed) *keyed = 1;
			res = 0;
			break;
				
		case DC_REQ_FLUSH:
			myrpt->dtmfidx = 0;
			myrpt->dtmfbuf[0] = 0;
			res = 0;
			break;
				
				
		case DC_COMPLETE:
			res = 1;
		case DC_COMPLETEQUIET:
			myrpt->totalexecdcommands++;
			myrpt->dailyexecdcommands++;
			strncpy(myrpt->lastdtmfcommand, myrpt->dtmfbuf, MAXDTMF-1);
			myrpt->lastdtmfcommand[MAXDTMF-1] = '\0';
			myrpt->dtmfbuf[0] = 0;
			myrpt->dtmfidx = -1;
			myrpt->dtmf_time_rem = 0;
			break;
				
		case DC_ERROR:
		default:
			myrpt->dtmfbuf[0] = 0;
			myrpt->dtmfidx = -1;
			myrpt->dtmf_time_rem = 0;
			res = 0;
			break;
	}

	return res;
}

int handle_remote_data(struct rpt *myrpt, char *str)
{
char	tmp[300],cmd[300],dest[300],src[300],c;
int	seq,res;

 	/* put string in our buffer */
	strncpy(tmp,str,sizeof(tmp) - 1);
	if (!strcmp(tmp,discstr)) return 0;
        if (!strcmp(tmp,newkeystr))
        {
		if (!myrpt->newkey) 
		{
			send_old_newkey(myrpt->rxchannel);
			myrpt->newkey = 1;
		}
                return 0;
        }
        if (!strcmp(tmp,newkey1str))
        {
		myrpt->newkey = 2;
                return 0;
        }
        if (!strncmp(tmp,iaxkeystr,strlen(iaxkeystr)))
        {
		myrpt->iaxkey = 1;
                return 0;
        }

	if (tmp[0] == 'T') return 0;

#ifndef	DO_NOT_NOTIFY_MDC1200_ON_REMOTE_BASES
	if (tmp[0] == 'I')
	{
		if (sscanf(tmp,"%s %s %s",cmd,src,dest) != 3)
		{
			ast_log(LOG_WARNING, "Unable to parse ident string %s\n",str);
			return 0;
		}
		mdc1200_notify(myrpt,src,dest);
		return 0;
	}
#endif
	if (tmp[0] == 'L') return 0;
	if (sscanf(tmp,"%s %s %s %d %c",cmd,dest,src,&seq,&c) != 5)
	{
		ast_log(LOG_WARNING, "Unable to parse link string %s\n",str);
		return 0;
	}
	if (strcmp(cmd,"D"))
	{
		ast_log(LOG_WARNING, "Unable to parse link string %s\n",str);
		return 0;
	}
	/* if not for me, ignore */
	if (strcmp(dest,myrpt->name)) return 0;
	if (myrpt->p.archivedir)
	{
		char str[100];

		sprintf(str,"DTMF,%c",c);
		donodelog(myrpt,str);
	}
	c = func_xlat(myrpt,c,&myrpt->p.outxlat);
	if (!c) return(0);
	res = handle_remote_dtmf_digit(myrpt,c, NULL, 0);
	if (res != 1)
		return res;
	if ((!strcmp(myrpt->remoterig, remote_rig_tm271)) ||
	   (!strcmp(myrpt->remoterig, remote_rig_kenwood)))
		rpt_telemetry(myrpt,REMCOMPLETE,NULL);
	else
		rpt_telemetry(myrpt,COMPLETE,NULL);
	return 0;
}

int handle_remote_phone_dtmf(struct rpt *myrpt, char c, char *keyed, int phonemode)
{
int	res;


	if(phonemode == 3) /* simplex phonemode, funcchar key/unkey toggle */
	{
		if (keyed && *keyed && ((c == myrpt->p.funcchar) || (c == myrpt->p.endchar)))
		{
			*keyed = 0; /* UNKEY */
			return 0;
		}
		else if (keyed && !*keyed && (c == myrpt->p.funcchar))
		{
			*keyed = 1; /* KEY */
			return 0;
		}
	}
	else /* endchar unkey */
	{

		if (keyed && *keyed && (c == myrpt->p.endchar))
		{
			*keyed = 0;
			return DC_INDETERMINATE;
		}
	}
	if (myrpt->p.archivedir)
	{
		char str[100];

		sprintf(str,"DTMF(P),%c",c);
		donodelog(myrpt,str);
	}
	res = handle_remote_dtmf_digit(myrpt,c,keyed, phonemode);
	if (res != 1)
		return res;
	if ((!strcmp(myrpt->remoterig, remote_rig_tm271)) ||
	   (!strcmp(myrpt->remoterig, remote_rig_kenwood)))
		rpt_telemetry(myrpt,REMCOMPLETE,NULL);
	else
		rpt_telemetry(myrpt,COMPLETE,NULL);
	return 0;
}

void local_dtmf_helper(struct rpt *myrpt,char c_in)
{
int	res;
pthread_attr_t	attr;
char	cmd[MAXDTMF+1] = "",c,tone[10];


	c = c_in & 0x7f;

	sprintf(tone,"%c",c);
	rpt_manager_trigger(myrpt, "DTMF", tone);

	if (myrpt->p.archivedir)
	{
		char str[100];

		sprintf(str,"DTMF,MAIN,%c",c);
		donodelog(myrpt,str);
	}
	if (c == myrpt->p.endchar)
	{
	/* if in simple mode, kill autopatch */
		if (myrpt->p.simple && myrpt->callmode)
		{   
			if(debug)
				ast_log(LOG_WARNING, "simple mode autopatch kill\n");
			rpt_mutex_lock(&myrpt->lock);
			myrpt->callmode = 0;
			myrpt->macropatch=0;
			channel_revert(myrpt);
			rpt_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt,TERM,NULL);
			return;
		}
		rpt_mutex_lock(&myrpt->lock);
		myrpt->stopgen = 1;
		if (myrpt->cmdnode[0])
		{
			cmd[0] = 0;
			if (!strcmp(myrpt->cmdnode,"aprstt"))
			{
				char overlay,aprscall[100],fname[100];
				FILE *fp;

				snprintf(cmd, sizeof(cmd),"A%s", myrpt->dtmfbuf);
				overlay = aprstt_xlat(cmd,aprscall);
				if (overlay)
				{
					if (debug) ast_log(LOG_WARNING,"aprstt got string %s call %s overlay %c\n",cmd,aprscall,overlay);
					if (!myrpt->p.aprstt[0]) ast_copy_string(fname,APRSTT_PIPE,sizeof(fname) - 1);
					else snprintf(fname,sizeof(fname) - 1,APRSTT_SUB_PIPE,myrpt->p.aprstt);
					fp = fopen(fname,"w");
					if (!fp)
					{
						ast_log(LOG_WARNING,"Can not open APRSTT pipe %s\n",fname);
					}
					else
					{
						fprintf(fp,"%s %c\n",aprscall,overlay);
						fclose(fp);
						rpt_telemetry(myrpt, ARB_ALPHA, (void *) aprscall);
					}
				}
			}
			myrpt->cmdnode[0] = 0;
			myrpt->dtmfidx = -1;
			myrpt->dtmfbuf[0] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			if (!cmd[0]) rpt_telemetry(myrpt,COMPLETE,NULL);
			return;
		} 
		else if(!myrpt->inpadtest)
                {
                        rpt_mutex_unlock(&myrpt->lock);
                        if (myrpt->p.propagate_phonedtmf)
                               do_dtmf_phone(myrpt,NULL,c);
			if ((myrpt->dtmfidx == -1) &&
			   ((myrpt->callmode == 2) || (myrpt->callmode == 3)))
			{
				myrpt->mydtmf = c;
			}
			return;
                }
		else
		{
			rpt_mutex_unlock(&myrpt->lock);
		}
	}
	rpt_mutex_lock(&myrpt->lock);
	if (myrpt->cmdnode[0] && strcmp(myrpt->cmdnode,"aprstt"))
	{
		rpt_mutex_unlock(&myrpt->lock);
		send_link_dtmf(myrpt,c);
		return;
	}
	if (!myrpt->p.simple)
	{
		if ((!myrpt->inpadtest) && myrpt->p.aprstt && (!myrpt->cmdnode[0]) && (c == 'A'))
		{
			strcpy(myrpt->cmdnode,"aprstt");
			myrpt->dtmfidx = 0;
			myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			time(&myrpt->dtmf_time);
			return;
		}
		if ((!myrpt->inpadtest) && (c == myrpt->p.funcchar))
		{
			if (myrpt->p.dopfxtone && (myrpt->dtmfidx == -1))
				rpt_telemetry(myrpt,PFXTONE,NULL);
			myrpt->dtmfidx = 0;
			myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			time(&myrpt->dtmf_time);
			return;
		} 
		else if (((myrpt->inpadtest) || (c != myrpt->p.endchar)) && (myrpt->dtmfidx >= 0))
		{
			time(&myrpt->dtmf_time);
			cancel_pfxtone(myrpt);
			
			if (myrpt->dtmfidx < MAXDTMF)
			{
				int src;

				myrpt->dtmfbuf[myrpt->dtmfidx++] = c;
				myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
				
				strncpy(cmd, myrpt->dtmfbuf, sizeof(cmd) - 1);
				
				rpt_mutex_unlock(&myrpt->lock);
				if (myrpt->cmdnode[0]) return;
				src = SOURCE_RPT;
				if (c_in & 0x80) src = SOURCE_ALT;
				res = collect_function_digits(myrpt, cmd, src, NULL);
				rpt_mutex_lock(&myrpt->lock);
				switch(res){
				    case DC_INDETERMINATE:
					break;
				    case DC_REQ_FLUSH:
					myrpt->dtmfidx = 0;
					myrpt->dtmfbuf[0] = 0;
					break;
				    case DC_COMPLETE:
				    case DC_COMPLETEQUIET:
					myrpt->totalexecdcommands++;
					myrpt->dailyexecdcommands++;
					strncpy(myrpt->lastdtmfcommand, cmd, MAXDTMF-1);
					myrpt->lastdtmfcommand[MAXDTMF-1] = '\0';
					myrpt->dtmfbuf[0] = 0;
					myrpt->dtmfidx = -1;
					myrpt->dtmf_time = 0;
					break;

				    case DC_ERROR:
				    default:
					myrpt->dtmfbuf[0] = 0;
					myrpt->dtmfidx = -1;
					myrpt->dtmf_time = 0;
					break;
				}
				if(res != DC_INDETERMINATE) {
					rpt_mutex_unlock(&myrpt->lock);
					return;
				}
			} 
		}
	}
	else /* if simple */
	{
		if ((!myrpt->callmode) && (c == myrpt->p.funcchar))
		{
			myrpt->callmode = 1;
			myrpt->patchnoct = 0;
			myrpt->patchquiet = 0;
			myrpt->patchfarenddisconnect = 0;
			myrpt->patchdialtime = 0;
			ast_copy_string(myrpt->patchcontext, myrpt->p.ourcontext, MAXPATCHCONTEXT-1);
			myrpt->cidx = 0;
			myrpt->exten[myrpt->cidx] = 0;
			rpt_mutex_unlock(&myrpt->lock);
		        pthread_attr_init(&attr);
		        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			ast_pthread_create(&myrpt->rpt_call_thread,&attr,rpt_call,(void *)myrpt);
			return;
		}
	}
	if (myrpt->callmode == 1)
	{
		myrpt->exten[myrpt->cidx++] = c;
		myrpt->exten[myrpt->cidx] = 0;
		/* if this exists */
		if (ast_exists_extension(myrpt->pchannel,myrpt->patchcontext,myrpt->exten,1,NULL))
		{
			/* if this really it, end now */
			if (!ast_matchmore_extension(myrpt->pchannel,myrpt->patchcontext,
				myrpt->exten,1,NULL)) 
			{
				myrpt->callmode = 2;
				rpt_mutex_unlock(&myrpt->lock);
				if(!myrpt->patchquiet)
					rpt_telemetry(myrpt,PROC,NULL); 
				return;
			}
			else /* othewise, reset timer */
			{
				myrpt->calldigittimer = 1;
			}
		}
		/* if can continue, do so */
		if (!ast_canmatch_extension(myrpt->pchannel,myrpt->patchcontext,myrpt->exten,1,NULL))
		{
			/* call has failed, inform user */
			myrpt->callmode = 4;
		}
		rpt_mutex_unlock(&myrpt->lock);
		return;
	}
	if (((myrpt->callmode == 2) || (myrpt->callmode == 3)) &&
		(myrpt->dtmfidx < 0))
	{
		myrpt->mydtmf = c;
	}
	rpt_mutex_unlock(&myrpt->lock);
	if ((myrpt->dtmfidx < 0) && myrpt->p.propagate_phonedtmf)
		do_dtmf_phone(myrpt,NULL,c);
	return;
}
