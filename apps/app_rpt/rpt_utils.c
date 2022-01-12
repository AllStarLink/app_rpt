
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

#include "rpt_dsp.h" /* must come before app_rpt.h */
#include "app_rpt.h"
#include "rpt_utils.h"
#include "rpt_lock.h"
#include "rpt_serial.h"
#include "rpt_channels.h"
#include "rpt_uchameleon.h"

extern int debug;
extern struct ast_flags config_flags;
extern ast_mutex_t nodeloglock;
extern struct nodelog nodelog;

/*! \todo this should be a define? */
extern int nullfd;
static char *remote_rig_ppp16="ppp16";	  		// parallel port programmable 16 channels

AST_MUTEX_DEFINE_STATIC(nodelookuplock);

void donodelog(struct rpt *myrpt,char *str)
{
struct nodelog *nodep;
char	datestr[100];

	if (!myrpt->p.archivedir) return;
	nodep = (struct nodelog *)ast_malloc(sizeof(struct nodelog));
	if (nodep == NULL)
	{
		ast_log(LOG_ERROR,"Cannot get memory for node log");
		return;
	}
	time(&nodep->timestamp);
	strncpy(nodep->archivedir,myrpt->p.archivedir,
		sizeof(nodep->archivedir) - 1);
	strftime(datestr,sizeof(datestr) - 1,"%Y%m%d%H%M%S",
		localtime(&nodep->timestamp));
	snprintf(nodep->str,sizeof(nodep->str) - 1,"%s %s,%s\n",
		myrpt->name,datestr,str);
	ast_mutex_lock(&nodeloglock);
	insque((struct qelem *) nodep, (struct qelem *) nodelog.prev);
	ast_mutex_unlock(&nodeloglock);
}

void __mklinklist(struct rpt *myrpt, struct rpt_link *mylink, char *buf,int flag)
{
struct rpt_link *l;
char mode;
int	i,spos;

	buf[0] = 0; /* clear output buffer */
	if (myrpt->remote) return;
	/* go thru all links */
	for(l = myrpt->links.next; l != &myrpt->links; l = l->next)
	{
		/* if is not a real link, ignore it */
		if (l->name[0] == '0') continue;
		if (l->mode > 1) continue; /* dont report local modes */
		/* dont count our stuff */
		if (l == mylink) continue;
		if (mylink && (!strcmp(l->name,mylink->name))) continue;
		/* figure out mode to report */
		mode = 'T'; /* use Tranceive by default */
		if (!l->mode) mode = 'R'; /* indicate RX for our mode */
		if (!l->thisconnected) 	mode = 'C'; /* indicate connecting */
		spos = strlen(buf); /* current buf size (b4 we add our stuff) */
		if (spos)
		{
			strcat(buf,",");
			spos++;
		}
		if (flag)
		{
			snprintf(buf + spos,MAXLINKLIST - spos,
				"%s%c%c",l->name,mode,(l->lastrx1) ? 'K' : 'U');
		}
		else
		{
			/* add nodes into buffer */
			if (l->linklist[0])
			{
				snprintf(buf + spos,MAXLINKLIST - spos,
					"%c%s,%s",mode,l->name,l->linklist);
			}
			else /* if no nodes, add this node into buffer */
			{
				snprintf(buf + spos,MAXLINKLIST - spos,
					"%c%s",mode,l->name);
			}	
		}
		/* if we are in tranceive mode, let all modes stand */
		if (mode == 'T') continue;
		/* downgrade everyone on this node if appropriate */
		for(i = spos; buf[i]; i++)
		{
			if (buf[i] == 'T') buf[i] = mode;
			if ((buf[i] == 'R') && (mode == 'C')) buf[i] = mode;
		}
	}
	return;
}

/* 
 * AllStar Network node lookup function.  This function will take the nodelist that has been read into memory
 * and try to match the node number that was passed to it.  If it is found, the function requested will succeed.
 * If not, it will fail.  Called when a connection to a remote node is requested.
 */

int node_lookup(struct rpt *myrpt,char *digitbuf,char *str, int strmax, int wilds)
{
char *val;
int longestnode,i,j,found;
struct stat mystat;
struct ast_config *ourcfg;
struct ast_variable *vp;

	/* try to look it up locally first */
	val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.nodes, digitbuf);
	if (val)
	{
		if (str && strmax)
			//snprintf(str,strmax,val,digitbuf);
			snprintf(str, strmax, "%s%s", val, digitbuf); /*! \todo 20220111 NA. This may not actually be correct (functionality-wise). Should be verified. For now, it makes the compiler happy. */
		return(1);
	}
	if (wilds)
	{
		vp = ast_variable_browse(myrpt->cfg, myrpt->p.nodes);
		while(vp)
		{
			if (ast_extension_match(vp->name, digitbuf))
			{
				if (str && strmax)
					//snprintf(str,strmax,vp->value,digitbuf);
					snprintf(str, strmax, "%s%s", vp->value, digitbuf); // 20220111 NA. This may not actually be correct (functionality-wise). Should be verified. For now, it makes the compiler happy.
				return(1);
			}
			vp = vp->next;
		}
	}
	ast_mutex_lock(&nodelookuplock);
	if (!myrpt->p.extnodefilesn) 
	{
		ast_mutex_unlock(&nodelookuplock);
		return(0);
	}
	/* determine longest node length again */		
	longestnode = 0;
	vp = ast_variable_browse(myrpt->cfg, myrpt->p.nodes);
	while(vp)
	{
		j = strlen(vp->name);
		if (*vp->name == '_') j--;
		if (j > longestnode)
			longestnode = j;
		vp = vp->next;
	}
	found = 0;
	for(i = 0; i < myrpt->p.extnodefilesn; i++)
	{
		ourcfg = ast_config_load(myrpt->p.extnodefiles[i],config_flags);
		/* if file does not exist */
		if (stat(myrpt->p.extnodefiles[i],&mystat) == -1) continue;
		/* if file not there, try next */
		if (!ourcfg) continue;
		vp = ast_variable_browse(ourcfg, myrpt->p.extnodes);
		while(vp)
		{
			j = strlen(vp->name);
			if (*vp->name == '_') j--;
			if (j > longestnode)
				longestnode = j;
			vp = vp->next;
		}
		if (!found)
		{
			val = (char *) ast_variable_retrieve(ourcfg, myrpt->p.extnodes, digitbuf);
			if (val)
			{
				found = 1;
				if (str && strmax)
					//snprintf(str,strmax,val,digitbuf);
					snprintf(str, strmax, "%s%s", val, digitbuf); // 20220111 NA. This may not actually be correct (functionality-wise). Should be verified. For now, it makes the compiler happy.
			}
		}
		ast_config_destroy(ourcfg);
	}
	myrpt->longestnode = longestnode;
	ast_mutex_unlock(&nodelookuplock);
	return(found);
}

char *forward_node_lookup(struct rpt *myrpt,char *digitbuf, struct ast_config *cfg)
{

char *val,*efil,*enod,*strs[100];
int i,n;
struct stat mystat;
static struct ast_config *ourcfg;

	val = (char *) ast_variable_retrieve(cfg, "proxy", "extnodefile");
	if (!val) val = EXTNODEFILE;
	enod = (char *) ast_variable_retrieve(cfg, "proxy", "extnodes");
	if (!enod) enod = EXTNODES;
	ast_mutex_lock(&nodelookuplock);
	efil = ast_strdup(val);
	if (!efil) 
	{
		ast_config_destroy(ourcfg);
		if (ourcfg) ast_config_destroy(ourcfg);
		ourcfg = NULL;
		ast_mutex_unlock(&nodelookuplock);
		return NULL;
	}
	n = finddelim(efil,strs,100);
	if (n < 1)
	{
		ast_free(efil);
		ast_config_destroy(ourcfg);
		if (ourcfg) ast_config_destroy(ourcfg);
		ourcfg = NULL;
		ast_mutex_unlock(&nodelookuplock);
		return NULL;
	}
	if (ourcfg) ast_config_destroy(ourcfg);
	val = NULL;
	for(i = 0; i < n; i++)
	{
		/* if file does not exist */
		if (stat(strs[i],&mystat) == -1) continue;
		ourcfg = ast_config_load(strs[i],config_flags);
		/* if file not there, try next */
		if (!ourcfg) continue;
		if (!val) val = (char *) ast_variable_retrieve(ourcfg, enod, digitbuf);
	}
        if (!val)
        {
                if (ourcfg) ast_config_destroy(ourcfg);
                ourcfg = NULL;
        }
	ast_mutex_unlock(&nodelookuplock);
	ast_free(efil);
	return(val);
}

int rpt_push_alt_macro(struct rpt *myrpt, char *sptr)
{
	int	busy=0;

	rpt_mutex_lock(&myrpt->lock);
	if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(sptr)){
		rpt_mutex_unlock(&myrpt->lock);
		busy=1;
	}
	if(!busy){
		int x;
		if (debug)ast_log(LOG_NOTICE, "rpt_push_alt_macro %s\n",sptr);
		myrpt->macrotimer = MACROTIME;
		for(x = 0; *(sptr + x); x++)
		    myrpt->macrobuf[x] = *(sptr + x) | 0x80;
		*(sptr + x) = 0;
	}
	rpt_mutex_unlock(&myrpt->lock);

	if(busy)ast_log(LOG_WARNING, "Function decoder busy on app_rpt command macro.\n");

	return busy;
}

int matchkeyword(char *string, char **param, char *keywords[])
{
int	i,ls;
	for( i = 0 ; keywords[i] ; i++){
		ls = strlen(keywords[i]);
		if(!ls){
			if(param)
				*param = NULL;
			return 0;
		}
		if(!strncmp(string, keywords[i], ls)){
			if(param)
				*param = string + ls;
			return i + 1; 
		}
	}
	if(param)
		*param = NULL;
	return 0;
}

int explode_string(char *str, char *strp[], int limit, char delim, char quote)
{
int     i,l,inquo;

        inquo = 0;
        i = 0;
        strp[i++] = str;
        if (!*str)
           {
                strp[0] = 0;
                return(0);
           }
        for(l = 0; *str && (l < limit) ; str++)
        {
		if(quote)
		{
                	if (*str == quote)
                   	{	
                        	if (inquo)
                           	{
                                	*str = 0;
                                	inquo = 0;
                           	}
                        	else
                           	{
                                	strp[i - 1] = str + 1;
                                	inquo = 1;
                           	}
			}
		}	
                if ((*str == delim) && (!inquo))
                {
                        *str = 0;
			l++;
                        strp[i++] = str + 1;
                }
        }
        strp[i] = 0;
        return(i);

}

char *strupr(char *instr)
{
	char *str = instr;
	while (*str) {
		*str = toupper(*str);
		str++;
	}
	return(instr);
}

int finddelim(char *str, char *strp[], int limit)
{
	return explode_string(str, strp, limit, DELIMCHR, QUOTECHR);
}

char *string_toupper(char *str)
{
int	i;

	for(i = 0; str[i]; i++)
		if (islower(str[i])) str[i] = toupper(str[i]);
	return str;
}

char *skipchars(char *string, char *charlist)
{
	int i;	
	while(*string) {
		for(i = 0; charlist[i] ; i++){
			if(*string == charlist[i]){
				string++;
				break;
			}
		}
		if(!charlist[i])
			return string;
	}
	return string;
}	

int myatoi(char *str)
{
	int	ret;

	if (str == NULL) return -1;
	/* leave this %i alone, non-base-10 input is useful here */
	if (sscanf(str,"%i",&ret) != 1) return -1;
	return ret;
}

int mycompar(const void *a, const void *b)
{
char	**x = (char **) a;
char	**y = (char **) b;
int	xoff,yoff;

	if ((**x < '0') || (**x > '9')) xoff = 1; else xoff = 0;
	if ((**y < '0') || (**y > '9')) yoff = 1; else yoff = 0;
	return(strcmp((*x) + xoff,(*y) + yoff));
}

int topcompar(const void *a, const void *b)
{
struct rpt_topkey *x = (struct rpt_topkey *) a;
struct rpt_topkey *y = (struct rpt_topkey *) b;

	return(x->timesince - y->timesince);
}

char *eatwhite(char *s)
{
	while((*s == ' ') || (*s == 0x09)){ /* get rid of any leading white space */
		if(!*s)
			break;
		s++;
	}
	return s;
}

long diskavail(struct rpt *myrpt)
{
struct	statfs statfsbuf;

	if (!myrpt->p.archivedir) return(0);
	if (statfs(myrpt->p.archivedir,&statfsbuf) == -1)
	{
		ast_log(LOG_WARNING,"Cannot get filesystem size for %s node %s\n",
			myrpt->p.archivedir,myrpt->name);
		return(-1);
	}
	return(statfsbuf.f_bavail);
}

/*
 Get the time for the machine's time zone
 Note: Asterisk requires a copy of localtime
 in the /etc directory for this to work properly.
 If /etc/localtime is not present, you will get
 GMT time! This is especially important on systems
 running embedded linux distributions as they don't usually
 have support for locales. 
*/

void rpt_localtime( time_t *t, struct ast_tm *lt, char *tz)
{
struct timeval tv;

	tv.tv_sec = *t;
	tv.tv_usec = 0;
	ast_localtime(&tv, lt, tz);

}

time_t rpt_mktime(struct ast_tm *tm,char *zone)
{
struct timeval now;

	now = ast_mktime(tm,zone);
	return now.tv_sec;
}

char func_xlat(struct rpt *myrpt,char c,struct rpt_xlat *xlat)
{
time_t	now;
int	gotone;

	time(&now);
	gotone = 0;
	/* if too much time, reset the skate machine */
	if ((now - xlat->lastone) > MAXXLATTIME)
	{
		xlat->funcindex = xlat->endindex = 0;
	}
	if (xlat->funccharseq[0] && (c == xlat->funccharseq[xlat->funcindex++]))
	{
		time(&xlat->lastone);
		gotone = 1;
		if (!xlat->funccharseq[xlat->funcindex])
		{
			xlat->funcindex = xlat->endindex = 0;
			return(myrpt->p.funcchar);
		}
	} else xlat->funcindex = 0;
	if (xlat->endcharseq[0] && (c == xlat->endcharseq[xlat->endindex++]))
	{
		time(&xlat->lastone);
		gotone = 1;
		if (!xlat->endcharseq[xlat->endindex])
		{
			xlat->funcindex = xlat->endindex = 0;
			return(myrpt->p.endchar);
		}
	} else xlat->endindex = 0;
	/* if in middle of decode seq, send nothing back */
	if (gotone) return(0);
	/* if no pass chars specified, return em all */
	if (!xlat->passchars[0]) return(c);
	/* if a "pass char", pass it */
	if (strchr(xlat->passchars,c)) return(c);
	return(0);
}

int iswebtransceiver(struct  rpt_link *l)
{
int	i;

	if (!l) return 0;
	for(i = 0; l->name[i]; i++)
	{
		if (!isdigit(l->name[i])) return 1;
	}
	return 0;
}

/*
*  Execute shell command
*/

int function_cmd(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
	char *cp;

	if (myrpt->remote)
		return DC_ERROR;

	ast_log(LOG_NOTICE, "cmd param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);
	
	if (param) {
		if (*param == '#') /* to execute asterisk cli command */
		{
			ast_cli_command(nullfd,param + 1);
		}
		else
		{			
			cp = ast_malloc(strlen(param) + 10);
			if (!cp)
			{
				ast_log(LOG_NOTICE,"Unable to alloc");
				return DC_ERROR;
			}
			memset(cp,0,strlen(param) + 10);
			sprintf(cp,"%s &",param);
			ast_safe_system(cp);
			ast_free(cp);
		}
	}
	return DC_COMPLETE;
}

int get_wait_interval(struct rpt *myrpt, int type)
{
        int interval;
        char *wait_times;
        char *wait_times_save;
                                                                                                                  
        wait_times_save = NULL;
        wait_times = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->name, "wait_times");
                                                                                                                  
        if(wait_times){
                wait_times_save = ast_strdup(wait_times);
                if(!wait_times_save)
			return 0;
                
        }
                                                                                                                  
        switch(type){
                case DLY_TELEM:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "telemwait", 500, 5000, 1000);
                        else
                                interval = 1000;
                        break;
                                                                                                                  
                case DLY_ID:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "idwait",250,5000,500);
                        else
                                interval = 500;
                        break;
                                                                                                                  
                case DLY_UNKEY:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "unkeywait",50,5000,1000);
                        else
                                interval = 1000;
                        break;
                                                                                                                  
                case DLY_LINKUNKEY:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "linkunkeywait",500,5000,1000);
                        else
                                interval = 1000;
                        break;
                                                                                                                  
                case DLY_CALLTERM:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "calltermwait",500,5000,1500);
                        else
                                interval = 1500;
                        break;
                                                                                                                  
                case DLY_COMP:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "compwait",500,5000,200);
                        else
                                interval = 200;
                        break;
                                                                                                                  
                case DLY_PARROT:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "parrotwait",500,5000,200);
                        else
                                interval = 200;
                        break;
                case DLY_MDC1200:
                        if(wait_times)
                                interval = retrieve_astcfgint(myrpt,wait_times_save, "mdc1200wait",500,5000,200);
                        else
                                interval = 350;
                        break;
                default:
			interval = 0;
			break;
        }
	if(wait_times_save)
       		ast_free(wait_times_save);
	return interval;
}

int wait_interval(struct rpt *myrpt, int type, struct ast_channel *chan)
{
	int interval;

	do {
		while (myrpt->p.holdofftelem && 
			(myrpt->keyed || (myrpt->remrx && (type != DLY_ID))))
		{
			if (ast_safe_sleep(chan,100) < 0) return -1;
		}

		interval = get_wait_interval(myrpt, type);
		if(debug)
			ast_log(LOG_NOTICE,"Delay interval = %d\n", interval);
		if(interval)
			if (ast_safe_sleep(chan,interval) < 0) return -1;
		if(debug)
			ast_log(LOG_NOTICE,"Delay complete\n");
	}
	while (myrpt->p.holdofftelem && 
		(myrpt->keyed || (myrpt->remrx && (type != DLY_ID))));
	return 0;
}

int retrieve_astcfgint(struct rpt *myrpt,char *category, char *name, int min, int max, int defl)
{
        char *var;
        int ret;
	char include_zero = 0;

	if(min < 0){ /* If min is negative, this means include 0 as a valid entry */
		min = -min;
		include_zero = 1;
	}           
                                                                     
        var = (char *) ast_variable_retrieve(myrpt->cfg, category, name);
        if(var){
                ret = myatoi(var);
		if(include_zero && !ret)
			return 0;
                if(ret < min)
                        ret = min;
                if(ret > max)
                        ret = max;
        }
        else
                ret = defl;
        return ret;
}

static int elink_cmd(char *cmd, char *outstr, int outlen)
{
FILE	*tf;

	tf = tmpfile();
	if (!tf) return -1;
	if (debug)  ast_log(LOG_DEBUG,"elink_cmd sent %s\n",cmd);
	ast_cli_command(fileno(tf),cmd);
	rewind(tf);
	outstr[0] = 0;
	if (!fgets(outstr,outlen,tf)) 
	{
		fclose(tf);
		return 0;
	}
	fclose(tf);
	if (!strlen(outstr)) return 0;
	if (outstr[strlen(outstr) - 1] == '\n')
		outstr[strlen(outstr) - 1] = 0;
	if (debug)  ast_log(LOG_DEBUG,"elink_cmd ret. %s\n",outstr);
	return(strlen(outstr));
}

int elink_db_get(char *lookup, char c, char *nodenum,char *callsign, char *ipaddr)
{
char	str[512],str1[100],*strs[5];
int	n;

	snprintf(str,sizeof(str) - 1,"echolink dbget %c %s",c,lookup);
	n = elink_cmd(str,str1,sizeof(str1));
	if (n < 1) return(n);
	n = explode_string(str1, strs, 5, '|', '\"');
	if (n < 3) return(0);
	if (nodenum) strcpy(nodenum,strs[0]);
	if (callsign) strcpy(callsign,strs[1]);
	if (ipaddr) strcpy(ipaddr,strs[2]);
	return(1);
}

int tlb_node_get(char *lookup, char c, char *nodenum,char *callsign, char *ipaddr, char *port)
{
char	str[100],str1[100],*strs[6];
int	n;

	snprintf(str,sizeof(str) - 1,"tlb nodeget %c %s",c,lookup);
	n = elink_cmd(str,str1,sizeof(str1));
	if (n < 1) return(n);
	n = explode_string(str1, strs, 6, '|', '\"');
	if (n < 4) return(0);
	if (nodenum) strcpy(nodenum,strs[0]);
	if (callsign) strcpy(callsign,strs[1]);
	if (ipaddr) strcpy(ipaddr,strs[2]);
	if (port) strcpy(port,strs[3]);
	return(1);
}

int morse_cat(char *str, int freq, int duration)
{
	char *p;
	int len;

	if(!str)
		return -1;

	len = strlen(str);	
	p = str+len;

	if(len){
		*p++ = ',';
		*p = '\0';
	}

	snprintf(p, 62,"!%d/%d", freq, duration);
 
	return 0;
}

int get_mem_set(struct rpt *myrpt, char *digitbuf)
{
	int res=0;
	if(debug)ast_log(LOG_NOTICE," digitbuf=%s\n", digitbuf);
	res = retrieve_memory(myrpt, digitbuf);
	if(!res)res=setrem(myrpt);
	if(debug)ast_log(LOG_NOTICE," freq=%s  res=%i\n", myrpt->freq, res);
	return res;
}

int retrieve_memory(struct rpt *myrpt, char *memory)
{
	char tmp[15], *s, *s1, *s2, *val;

	if (debug)ast_log(LOG_NOTICE, "memory=%s block=%s\n",memory,myrpt->p.memory);

	val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.memory, memory);
	if (!val){
		return -1;
	}			
	strncpy(tmp,val,sizeof(tmp) - 1);
	tmp[sizeof(tmp)-1] = 0;

	s = strchr(tmp,',');
	if (!s)
		return 1; 
	*s++ = 0;
	s1 = strchr(s,',');
	if (!s1)
		return 1;
	*s1++ = 0;
	s2 = strchr(s1,',');
	if (!s2) s2 = s1;
	else *s2++ = 0;
	ast_copy_string(myrpt->freq, tmp, sizeof(myrpt->freq) - 1);
	ast_copy_string(myrpt->rxpl, s, sizeof(myrpt->rxpl) - 1);
	ast_copy_string(myrpt->txpl, s, sizeof(myrpt->rxpl) - 1);
	myrpt->remmode = REM_MODE_FM;
	myrpt->offset = REM_SIMPLEX;
	myrpt->powerlevel = REM_MEDPWR;
	myrpt->txplon = myrpt->rxplon = 0;
	myrpt->splitkhz = 0;
	if (s2 != s1) myrpt->splitkhz = atoi(s1);
	while(*s2){
		switch(*s2++){
			case 'A':
			case 'a':
				strcpy(myrpt->rxpl, "100.0");
				strcpy(myrpt->txpl, "100.0");
				myrpt->remmode = REM_MODE_AM;	
				break;
			case 'B':
			case 'b':
				strcpy(myrpt->rxpl, "100.0");
				strcpy(myrpt->txpl, "100.0");
				myrpt->remmode = REM_MODE_LSB;
				break;
			case 'F':
				myrpt->remmode = REM_MODE_FM;
				break;
			case 'L':
			case 'l':
				myrpt->powerlevel = REM_LOWPWR;
				break;					
			case 'H':
			case 'h':
				myrpt->powerlevel = REM_HIPWR;
				break;
					
			case 'M':
			case 'm':
				myrpt->powerlevel = REM_MEDPWR;
				break;
						
			case '-':
				myrpt->offset = REM_MINUS;
				break;
						
			case '+':
				myrpt->offset = REM_PLUS;
				break;
						
			case 'S':
			case 's':
				myrpt->offset = REM_SIMPLEX;
				break;
						
			case 'T':
			case 't':
				myrpt->txplon = 1;
				break;
						
			case 'R':
			case 'r':
				myrpt->rxplon = 1;
				break;

			case 'U':
			case 'u':
				strcpy(myrpt->rxpl, "100.0");
				strcpy(myrpt->txpl, "100.0");
				myrpt->remmode = REM_MODE_USB;
				break;
			default:
				return 1;
		}
	}
	return 0;
}

int channel_steer(struct rpt *myrpt, char *data)
{
	int res=0;

	if(debug)ast_log(LOG_NOTICE,"remoterig=%s, data=%s\n",myrpt->remoterig,data);
	if (!myrpt->remoterig) return(0);
	if(data<=0)
	{
		res=-1;
	}
	else
	{
		myrpt->nowchan=strtod(data,NULL);
		if(!strcmp(myrpt->remoterig, remote_rig_ppp16))
		{
			char string[16];
			sprintf(string,"SETCHAN %d ",myrpt->nowchan);
			send_usb_txt(myrpt,string);
		}
		else
		{
			if(get_mem_set(myrpt, data))res=-1;
		}
	}
	if(debug)ast_log(LOG_NOTICE,"nowchan=%i  res=%i\n",myrpt->nowchan, res);
	return res;
}

int channel_revert(struct rpt *myrpt)
{
	int res=0;
	if(debug)ast_log(LOG_NOTICE,"remoterig=%s, nowchan=%02d, waschan=%02d\n",myrpt->remoterig,myrpt->nowchan,myrpt->waschan);
	if (!myrpt->remoterig) return(0);
	if(myrpt->nowchan!=myrpt->waschan)
	{
		char data[8];
        if(debug)ast_log(LOG_NOTICE,"reverting.\n");
		sprintf(data,"%02d",myrpt->waschan);
		myrpt->nowchan=myrpt->waschan;
		channel_steer(myrpt,data);
		res=1;
	}
	return(res);
}

int split_freq(char *mhz, char *decimals, char *freq)
{
	char freq_copy[MAXREMSTR];
	char *decp;

	ast_copy_string(freq_copy, freq, MAXREMSTR-1);
	decp = strchr(freq_copy, '.');
	if(decp){
		*decp++ = 0;
		strncpy(mhz, freq_copy, MAXREMSTR);
		strcpy(decimals, "00000");
		ast_copy_string(decimals, decp, strlen(decimals)-1);
		decimals[5] = 0;
		return 0;
	}
	else
		return -1;

}

int split_ctcss_freq(char *hertz, char *decimal, char *freq)
{
	char freq_copy[MAXREMSTR];
	char *decp;

	decp = strchr(strncpy(freq_copy, freq, MAXREMSTR-1),'.');
	if(decp){
		*decp++ = 0;
		ast_copy_string(hertz, freq_copy, MAXREMSTR);
		ast_copy_string(decimal, decp, strlen(decp));
		decimal[strlen(decp)] = '\0';
		return 0;
	}
	else
		return -1;
}

int decimals2int(char *fraction)
{
	int i;
	char len = strlen(fraction);
	int multiplier = 100000;
	int res = 0;

	if(!len)
		return 0;
	for( i = 0 ; i < len ; i++, multiplier /= 10)
		res += (fraction[i] - '0') * multiplier;
	return res;
}

char is_paging(struct rpt *myrpt)
{
char	rv = 0;

	if ((!ast_tvzero(myrpt->paging)) &&
		(ast_tvdiff_ms(ast_tvnow(),myrpt->paging) <= 300000)) rv = 1;
	return(rv);
}
