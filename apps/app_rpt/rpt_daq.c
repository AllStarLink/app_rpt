
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

#include "rpt_dsp.h" /* must come before app_rpt.h */
#include "app_rpt.h"
#include "rpt_utils.h"
#include "rpt_lock.h"
#include "rpt_serial.h"
#include "rpt_uchameleon.h"
#include "rpt_daq.h"

/*
 * DAQ variables
 */
struct daq_tag daq;

/*
 * **************************
 * Generic DAQ functions    *
 * **************************
 */

struct daq_entry_tag *daq_open(int type, char *name, char *dev)
{
	int fd;
	struct daq_entry_tag *t;


	if(!name)
		return NULL;

        if((t = ast_malloc(sizeof(struct daq_entry_tag))) == NULL){
		ast_log(LOG_WARNING,"daq_open out of memory\n");
		return NULL;
	}


	memset(t, 0, sizeof(struct daq_entry_tag));


	/* Save the device path for open*/
	if(dev){
		strncpy(t->dev, dev, MAX_DAQ_DEV);
		t->dev[MAX_DAQ_DEV - 1] = 0;
	}



	/* Save the name*/
	ast_copy_string(t->name, name, MAX_DAQ_NAME);
	t->dev[MAX_DAQ_NAME - 1] = 0;


	switch(type){
		case DAQ_TYPE_UCHAMELEON:
			if((fd = uchameleon_open(t)) == -1){
				ast_free(t);
				return NULL;
			}
			break;

		default:
			ast_free(t);
			return NULL;
	}
	t->type = type;
	return t;
}

int daq_close(struct daq_entry_tag *t)
{
	int res  = -1;

	if(!t)
		return res;

	switch(t->type){
		case DAQ_TYPE_UCHAMELEON:
			res = uchameleon_close(t);
			break;
		default:
			break;
	}

	ast_free(t);
	return res;
}

struct daq_entry_tag *daq_devtoentry(char *name)
{
	struct daq_entry_tag *e = daq.hw;

	while(e){
		if(!strcmp(name, e->name))
			break; 
		e = e->next;
	}
	return e;
}


int daq_do_long( struct daq_entry_tag *t, int pin, int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2)
{
	int res = -1;

	switch(t->type){
		case DAQ_TYPE_UCHAMELEON:
			res = uchameleon_do_long(t, pin, cmd, exec, arg1, arg2);
			break;
		default:
			break;
	}
	return res;
}

int daq_do( struct daq_entry_tag *t, int pin, int cmd, int arg1)
{
	int a1 = arg1;

	return daq_do_long(t, pin, cmd, NULL, &a1, NULL);
}

int daq_reset_minmax(char *device, int pin, int minmax)
{
	int res = -1;
	struct daq_entry_tag *t;
	
	if(!(t = daq_devtoentry(device)))
		return -1;
	switch(t->type){
		case DAQ_TYPE_UCHAMELEON:
			res = uchameleon_reset_minmax(t, pin, minmax);
			break;
		default:
			break;
	}
	return res;
}

void daq_init(struct ast_config *cfg)
{
	struct ast_variable *var;
	struct daq_entry_tag **t_next, *t = NULL;
	char s[64];
	daq.ndaqs = 0;
	t_next = &daq.hw;
	var = ast_variable_browse(cfg,"daq-list");
	while(var){
		char *p;
		if(strncmp("device",var->name,6)){
			ast_log(LOG_WARNING,"Error in daq_entries stanza on line %d\n", var->lineno);
			break;
		}
		ast_copy_string(s,var->value,sizeof(s)-1); /* Make copy of device entry */
		if(!(p = (char *) ast_variable_retrieve(cfg,s,"hwtype"))){
			ast_log(LOG_WARNING,"hwtype variable required for %s stanza\n", s);
			break;
		}
		if(strncmp(p,"uchameleon",10)){
			ast_log(LOG_WARNING,"Type must be uchameleon for %s stanza\n", s);
			break;
		}
                if(!(p = (char *) ast_variable_retrieve(cfg,s,"devnode"))){
                        ast_log(LOG_WARNING,"devnode variable required for %s stanza\n", s);
                        break;
                }
		if(!(t = daq_open(DAQ_TYPE_UCHAMELEON, (char *) s, (char *) p))){
			ast_log(LOG_WARNING,"Cannot open device name %s\n",p);
			break;
		}
		/* Add to linked list */
		*t_next = t;
		t_next = &t->next;

		daq.ndaqs++;	
		if(daq.ndaqs >= MAX_DAQ_ENTRIES)
			break;
		var = var->next;
	}


}		

void daq_uninit(void)
{
	struct daq_entry_tag *t_next, *t;

	/* Free daq memory */
	t = daq.hw;
	while(t){
		t_next = t->next;
		daq_close(t);
		t = t_next;
	}
	daq.hw = NULL;
}
