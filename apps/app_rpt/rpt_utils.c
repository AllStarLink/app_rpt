
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

#include "app_rpt.h"
#include "rpt_utils.h"
#include "rpt_lock.h"
#include "rpt_serial.h"
#include "rpt_uchameleon.h"

extern int debug;
extern struct ast_flags config_flags;
extern ast_mutex_t nodeloglock;
extern struct nodelog nodelog;

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
