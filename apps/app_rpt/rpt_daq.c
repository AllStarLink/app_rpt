
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
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/options.h"

#include "app_rpt.h"
#include "rpt_daq.h"
#include "rpt_uchameleon.h"
#include "rpt_utils.h" /* use explode_string */
#include "rpt_channel.h" /* use wait_interval, sayfile */

/*
 * DAQ variables
 */

static struct daq_tag daq;

/*
 * **************************
 * Generic DAQ functions    *
 * **************************
 */

struct daq_entry_tag *daq_open(int type, char *name, char *dev)
{
	int fd;
	struct daq_entry_tag *t;

	if (!name)
		return NULL;

	if ((t = ast_malloc(sizeof(struct daq_entry_tag))) == NULL) {
		ast_log(LOG_WARNING, "daq_open out of memory\n");
		return NULL;
	}

	memset(t, 0, sizeof(struct daq_entry_tag));

	/* Save the device path for open */
	if (dev) {
		strncpy(t->dev, dev, MAX_DAQ_DEV);
		t->dev[MAX_DAQ_DEV - 1] = 0;
	}

	/* Save the name */
	ast_copy_string(t->name, name, MAX_DAQ_NAME);
	t->dev[MAX_DAQ_NAME - 1] = 0;

	switch (type) {
	case DAQ_TYPE_UCHAMELEON:
		if ((fd = uchameleon_open(t)) == -1) {
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
	int res = -1;

	if (!t)
		return res;

	switch (t->type) {
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

	while (e) {
		if (!strcmp(name, e->name))
			break;
		e = e->next;
	}
	return e;
}

int uchameleon_reset_minmax(struct daq_entry_tag *t, int pin, int minmax)
{
	struct daq_pin_entry_tag *p;

	/* Find the pin */
	p = t->pinhead;
	while (p) {
		if (p->num == pin)
			break;
		p = p->next;
	}
	if (!p)
		return -1;
	ast_mutex_lock(&t->lock);
	if (minmax) {
		ast_log(LOG_NOTICE, "Resetting maximum on device %s, pin %d\n", t->name, pin);
		p->valuemax = 0;
	} else {
		p->valuemin = 255;
		ast_log(LOG_NOTICE, "Resetting minimum on device %s, pin %d\n", t->name, pin);
	}
	ast_mutex_unlock(&t->lock);
	return 0;
}

int daq_do_long(struct daq_entry_tag *t, int pin, int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1,
					   void *arg2)
{
	int res = -1;

	switch (t->type) {
	case DAQ_TYPE_UCHAMELEON:
		res = uchameleon_do_long(t, pin, cmd, exec, arg1, arg2);
		break;
	default:
		break;
	}
	return res;
}

int daq_do(struct daq_entry_tag *t, int pin, int cmd, int arg1)
{
	int a1 = arg1;

	return daq_do_long(t, pin, cmd, NULL, &a1, NULL);
}

int daq_reset_minmax(char *device, int pin, int minmax)
{
	int res = -1;
	struct daq_entry_tag *t;

	if (!(t = daq_devtoentry(device)))
		return -1;
	switch (t->type) {
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
	var = ast_variable_browse(cfg, "daq-list");
	while (var) {
		char *p;
		if (strncmp("device", var->name, 6)) {
			ast_log(LOG_WARNING, "Error in daq_entries stanza on line %d\n", var->lineno);
			break;
		}
		ast_copy_string(s, var->value, sizeof(s) - 1);	/* Make copy of device entry */
		if (!(p = (char *) ast_variable_retrieve(cfg, s, "hwtype"))) {
			ast_log(LOG_WARNING, "hwtype variable required for %s stanza\n", s);
			break;
		}
		if (strncmp(p, "uchameleon", 10)) {
			ast_log(LOG_WARNING, "Type must be uchameleon for %s stanza\n", s);
			break;
		}
		if (!(p = (char *) ast_variable_retrieve(cfg, s, "devnode"))) {
			ast_log(LOG_WARNING, "devnode variable required for %s stanza\n", s);
			break;
		}
		if (!(t = daq_open(DAQ_TYPE_UCHAMELEON, (char *) s, (char *) p))) {
			ast_log(LOG_WARNING, "Cannot open device name %s\n", p);
			break;
		}
		/* Add to linked list */
		*t_next = t;
		t_next = &t->next;

		daq.ndaqs++;
		if (daq.ndaqs >= MAX_DAQ_ENTRIES)
			break;
		var = var->next;
	}

}

void daq_uninit(void)
{
	struct daq_entry_tag *t_next, *t;

	/* Free daq memory */
	t = daq.hw;
	while (t) {
		t_next = t->next;
		daq_close(t);
		t = t_next;
	}
	daq.hw = NULL;
}

int handle_userout_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *args)
{
	int argc, i, pin, reqstate, res;
	char *myargs;
	char *argv[11];
	struct daq_entry_tag *t;

	if (!(myargs = ast_strdup(args))) {	/* Make a local copy to slice and dice */
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}

	ast_debug(3, "String: %s\n", myargs);

	argc = explode_string(myargs, argv, 10, ',', 0);
	if (argc < 4) {				/* Must have at least 4 arguments */
		ast_log(LOG_WARNING, "Incorrect number of arguments for USEROUT function");
		ast_free(myargs);
		return -1;
	}
	ast_debug(3, "USEROUT Device: %s, Pin: %s, Requested state: %s\n", argv[0], argv[1], argv[2]);
	pin = atoi(argv[1]);
	reqstate = atoi(argv[2]);

	/* Find our device */
	if (!(t = daq_devtoentry(argv[0]))) {
		ast_log(LOG_WARNING, "Cannot find device %s in daq-list\n", argv[0]);
		ast_free(myargs);
		return -1;
	}

	ast_debug(3, "Output to pin %d a value of %d with argc = %d\n", pin, reqstate, argc);

	/* Set or reset the bit */

	res = daq_do(t, pin, DAQ_CMD_OUT, reqstate);

	/* Wait the normal telemetry delay time */

	if (!res)
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1)
			goto done;

	/* Say the files one by one at argc index 3 */
	for (i = 3; i < argc && !res; i++) {
		res = sayfile(mychannel, argv[i]);	/* Say the next word in the list */
	}

done:
	ast_free(myargs);
	return 0;
}
