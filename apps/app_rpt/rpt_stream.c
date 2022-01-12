
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

#include <dahdi/user.h>

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
#include "rpt_channels.h"
#include "rpt_daq.h"
#include "rpt_stream.h"

extern int debug;

//# Say a file - streams file to output channel
int sayfile(struct ast_channel *mychannel,char *fname)
{
int	res;

	res = ast_streamfile(mychannel, fname, ast_channel_language(mychannel));
	if (!res) 
		res = ast_waitstream(mychannel, "");
	else
		 ast_log(LOG_WARNING, "ast_streamfile %s failed on %s\n", fname, ast_channel_name(mychannel));
	ast_stopstream(mychannel);
	return res;
}

int saycharstr(struct ast_channel *mychannel,char *str)
{
int	res;

	res = ast_say_character_str(mychannel,str,NULL,ast_channel_language(mychannel),AST_SAY_CASE_NONE);
	if (!res) 
		res = ast_waitstream(mychannel, "");
	else
		 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
	ast_stopstream(mychannel);
	return res;
}

//# Say a number -- streams corresponding sound file
int saynum(struct ast_channel *mychannel, int num)
{
	int res;
	res = ast_say_number(mychannel, num, NULL, ast_channel_language(mychannel), NULL);
	if(!res)
		res = ast_waitstream(mychannel, "");
	else
		ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
	ast_stopstream(mychannel);
	return res;
}

int sayphoneticstr(struct ast_channel *mychannel,char *str)
{
int	res;

	res = ast_say_phonetic_str(mychannel,str,NULL,ast_channel_language(mychannel));
	if (!res) 
		res = ast_waitstream(mychannel, "");
	else
		 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
	ast_stopstream(mychannel);
	return res;
}

int saynode(struct rpt *myrpt, struct ast_channel *mychannel, char *name)
{
int	res = 0,tgn;
char	*val,fname[300],str[100];

	if (strlen(name) < 1) return(0);
	tgn = tlb_node_get(name,'n',NULL,str,NULL,NULL);
	if (((name[0] != '3') && (tgn != 1)) || ((name[0] == '3') && (myrpt->p.eannmode != 2)) ||
		((tgn == 1) && (myrpt->p.tannmode != 2)))
	{
		val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->name, "nodenames");
		if (!val) val = NODENAMES;
		snprintf(fname,sizeof(fname) - 1,"%s/%s",val,name);
		if (ast_fileexists(fname,NULL,ast_channel_language(mychannel)) > 0)
			return(sayfile(mychannel,fname));
		res = sayfile(mychannel,"rpt/node");
		if (!res) 
			res = ast_say_character_str(mychannel,name,NULL,ast_channel_language(mychannel),AST_SAY_CASE_NONE);
	}
	if (tgn == 1)
	{
		if (myrpt->p.tannmode < 2) return res;
		return(sayphoneticstr(mychannel,str));
	}
	if (name[0] != '3') return res;
	if (myrpt->p.eannmode < 2) return res;
	sprintf(str,"%d",atoi(name + 1));	
	if (elink_db_get(str,'n',NULL,fname,NULL) < 1) return res;
	res = sayphoneticstr(mychannel,fname);
	return res;
}

int play_tone_pair(struct ast_channel *chan, int f1, int f2, int duration, int amplitude)
{
	int res;

        if ((res = ast_tonepair_start(chan, f1, f2, duration, amplitude)))
                return res;
                                                                                                                                            
        while(ast_channel_generatordata(chan)) {
		if (ast_safe_sleep(chan,1)) return -1;
	}

        return 0;
}

int play_tone(struct ast_channel *chan, int freq, int duration, int amplitude)
{
	return play_tone_pair(chan, freq, 0, duration, amplitude);
}

int send_morse(struct ast_channel *chan, char *string, int speed, int freq, int amplitude)
{

static struct morse_bits mbits[] = {
		{0, 0}, /* SPACE */
		{0, 0}, 
		{6, 18},/* " */
		{0, 0},
		{7, 72},/* $ */
		{0, 0},
		{0, 0},
		{6, 30},/* ' */
		{5, 13},/* ( */
		{6, 29},/* ) */
		{0, 0},
		{5, 10},/* + */
		{6, 51},/* , */
		{6, 33},/* - */
		{6, 42},/* . */
		{5, 9}, /* / */
		{5, 31},/* 0 */
		{5, 30},/* 1 */
		{5, 28},/* 2 */
		{5, 24},/* 3 */
		{5, 16},/* 4 */
		{5, 0}, /* 5 */
		{5, 1}, /* 6 */
		{5, 3}, /* 7 */
		{5, 7}, /* 8 */
		{5, 15},/* 9 */
		{6, 7}, /* : */
		{6, 21},/* ; */
		{0, 0},
		{5, 33},/* = */
		{0, 0},
		{6, 12},/* ? */
		{0, 0},
        	{2, 2}, /* A */
 		{4, 1}, /* B */
		{4, 5}, /* C */
		{3, 1}, /* D */
		{1, 0}, /* E */
		{4, 4}, /* F */
		{3, 3}, /* G */
		{4, 0}, /* H */
		{2, 0}, /* I */
		{4, 14},/* J */
		{3, 5}, /* K */
		{4, 2}, /* L */
		{2, 3}, /* M */
		{2, 1}, /* N */
		{3, 7}, /* O */
		{4, 6}, /* P */
		{4, 11},/* Q */
		{3, 2}, /* R */
		{3, 0}, /* S */
		{1, 1}, /* T */
		{3, 4}, /* U */
		{4, 8}, /* V */
		{3, 6}, /* W */
		{4, 9}, /* X */
		{4, 13},/* Y */
		{4, 3}  /* Z */
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


	str = ast_malloc(12*8*strlen(string)); /* 12 chrs/element max, 8 elements/letter max */
	if(!str)
		return -1;
	str[0] = '\0';
	
	/* Approximate the dot time from the speed arg. */
	
	dottime = 900/speed;
	
	/* Establish timing releationships */
	
	dashtime = 3 * dottime;
	intralettertime = dottime;
	interlettertime = dottime * 4 ;
	interwordtime = dottime * 7;
	
	for(;(*string) && (!res); string++){
	
		c = *string;
		
		/* Convert lower case to upper case */
		
		if((c >= 'a') && (c <= 'z'))
			c -= 0x20;
		
		/* Can't deal with any char code greater than Z, skip it */
		
		if(c  > 'Z')
			continue;
		
		/* If space char, wait the inter word time */
					
		if(c == ' '){
			if(!res){
				if((res = morse_cat(str, 0, interwordtime)))
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
		
		for(; len ; len--){
			if(!res)
				res = morse_cat(str, freq, (ddcomb & 1) ? dashtime : dottime);
			if(!res)
				res = morse_cat(str, 0, intralettertime);
			ddcomb >>= 1;
		}
		
		/* Wait the interletter time */
		
		if(!res)
			res = morse_cat(str, 0, interlettertime - intralettertime);

	}
	
	/* Wait for all the characters to be sent */

	if(!res){
		if(debug > 4)
			ast_log(LOG_NOTICE,"Morse string: %s\n", str);
		ast_safe_sleep(chan,100);
		ast_playtones_start(chan, amplitude, str, 0);
		while(ast_channel_generatordata(chan)){
			if(ast_safe_sleep(chan, 20)){
				res = -1;
				break;
			}
		}		
				 
	}
	if(str)
		ast_free(str);
	return res;
}

int send_tone_telemetry(struct ast_channel *chan, char *tonestring)
{
	char *p,*stringp;
	char *tonesubset;
	int f1,f2;
	int duration;
	int amplitude;
	int res;
	int i;
	int flags;
	
	res = 0;

	if(!tonestring)
		return res;
	
	p = stringp = ast_strdup(tonestring);

	for(;tonestring;){
		tonesubset = strsep(&stringp,")");
		if(!tonesubset)
			break;
		if(sscanf(tonesubset,"(%d,%d,%d,%d", &f1, &f2, &duration, &amplitude) != 4)
			break;
		res = play_tone_pair(chan, f1, f2, duration, amplitude);
		if(res)
			break;
	}
	if(p)
		ast_free(p);
	if(!res)
		res = play_tone_pair(chan, 0, 0, 100, 0); /* This is needed to ensure the last tone segment is timed correctly */
	
	if (!res) 
		res = ast_waitstream(chan, "");

	ast_stopstream(chan);

	/*
	* Wait for the zaptel driver to physically write the tone blocks to the hardware
	*/

	for(i = 0; i < 20 ; i++){
		flags =  DAHDI_IOMUX_WRITEEMPTY | DAHDI_IOMUX_NOWAIT; 
		res = ioctl(ast_channel_fd(chan, 0), DAHDI_IOMUX, &flags);
		if(flags & DAHDI_IOMUX_WRITEEMPTY)
			break;
		if( ast_safe_sleep(chan, 50)){
			res = -1;
			break;
		}
	}
		
	return res;
		
}

int telem_any(struct rpt *myrpt,struct ast_channel *chan, char *entry)
{
	int res;
	char c;

	int morsespeed;
	int morsefreq;
	int morseampl;
	int morseidfreq;
	int morseidampl;

	res = 0;

	morsespeed = retrieve_astcfgint(myrpt, myrpt->p.morse, "speed", 5, 20, 20);
       	morsefreq = retrieve_astcfgint(myrpt, myrpt->p.morse, "frequency", 300, 3000, 800);
       	morseampl = retrieve_astcfgint(myrpt, myrpt->p.morse, "amplitude", 200, 8192, 4096);
	morseidampl = retrieve_astcfgint(myrpt, myrpt->p.morse, "idamplitude", 200, 8192, 2048);
	morseidfreq = retrieve_astcfgint(myrpt, myrpt->p.morse, "idfrequency", 300, 3000, 330);

	/* Is it a file, or a tone sequence? */

	if(entry[0] == '|'){
		c = entry[1];
		if((c >= 'a')&&(c <= 'z'))
			c -= 0x20;

		switch(c){
			case 'I': /* Morse ID */
				res = send_morse(chan, entry + 2, morsespeed, morseidfreq, morseidampl);
				break;

			case 'M': /* Morse Message */
				res = send_morse(chan, entry + 2, morsespeed, morsefreq, morseampl);
				break;

			case 'T': /* Tone sequence */
				res = send_tone_telemetry(chan, entry + 2);
				break;
			default:
				res = -1;
		}
	}
	else
		res = sayfile(chan, entry); /* File */
	return res;
}
