
#include "asterisk.h"

#include "asterisk/app.h" /* use ast_safe_system */
#include "asterisk/channel.h"
#include "asterisk/cli.h"

#include "app_rpt.h"
#include "rpt_lock.h"
#include "rpt_utils.h"
#include "rpt_daq.h"
#include "rpt_channel.h"
#include "rpt_telemetry.h"

void rpt_telem_select(struct rpt *myrpt, int command_source, struct rpt_link *mylink)
{
	int src;

	if (mylink && mylink->chan) {
		src = LINKMODE_GUI;
		if (mylink->phonemode)
			src = LINKMODE_PHONE;
		else if (!strcasecmp(ast_channel_tech(mylink->chan)->type, "echolink"))
			src = LINKMODE_ECHOLINK;
		else if (!strcasecmp(ast_channel_tech(mylink->chan)->type, "tlb"))
			src = LINKMODE_TLB;
		if (myrpt->p.linkmodedynamic[src] && (mylink->linkmode >= 1) && (mylink->linkmode < 0x7ffffffe))
			mylink->linkmode = LINK_HANG_TIME;
	}
	if (!myrpt->p.telemdynamic)
		return;
	if (myrpt->telemmode == 0)
		return;
	if (myrpt->telemmode == 0x7fffffff)
		return;
	myrpt->telemmode = TELEM_HANG_TIME;
	return;
}

int handle_meter_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *args)
{
	int i, res, files, filter, val;
	int pin = 0;
	int pintype = 0;
	int device = 0;
	int metertype = 0;
	int numranges = 0;
	int filtertype = 0;
	int rangemin, rangemax;
	float scaledval = 0.0, scalepre = 0.0, scalepost = 0.0, scalediv = 1.0, valtoround;
	char *myargs, *meter_face;
	const char *p;
	char *start, *end;
	char *sounds = NULL;
	char *rangephrase = NULL;
	char *argv[5];
	char *sound_files[MAX_METER_FILES + 1];
	char *range_strings[MAX_DAQ_RANGES + 1];
	char *bitphrases[3];
	static char *filter_keywords[] = { "none", "max", "min", "stmin", "stmax", "stavg", NULL };
	struct daq_entry_tag *entry;

	if (!(myargs = ast_strdup(args))) {	/* Make a local copy to slice and dice */
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}

	i = explode_string(myargs, argv, 4, ',', 0);
	if ((i != 4) && (i != 3)) {	/* Must have 3 or 4 substrings, no more, no less */
		ast_log(LOG_WARNING, "Wrong number of arguments for meter telemetry function is: %d s/b 3 or 4", i);
		ast_free(myargs);
		return -1;
	}
	ast_debug(3, "Device: %s, Pin: %s, Meter Face: %s Filter: %s\n", argv[0], argv[1], argv[2], argv[3]);

	if (i == 4) {
		filter = matchkeyword(argv[3], NULL, filter_keywords);
		if (!filter) {
			ast_log(LOG_WARNING, "Unsupported filter type: %s\n", argv[3]);
			ast_free(myargs);
			return -1;
		}
		filter--;
	} else
		filter = DAQ_SUB_CUR;

	/* Find our device */
	if (!(entry = daq_devtoentry(argv[0]))) {
		ast_log(LOG_WARNING, "Cannot find device %s in daq-list\n", argv[0]);
		ast_free(myargs);
		return -1;
	}

	/* Check for compatible pin type */
	if (!(p = ast_variable_retrieve(myrpt->cfg, argv[0], argv[1]))) {
		ast_log(LOG_WARNING, "Channel %s not defined for %s\n", argv[1], argv[0]);
		ast_free(myargs);
		return -1;
	}

	if (!strcmp("inadc", p))
		pintype = 1;
	if ((!strcmp("inp", p)) || (!strcmp("in", p) || (!strcmp("out", p))))
		pintype = 2;
	if (!pintype) {
		ast_log(LOG_WARNING, "Pin type must be one of inadc, inp, in, or out for channel %s\n", argv[1]);
		ast_free(myargs);
		return -1;
	}
	ast_debug(3, "Pintype = %d\n", pintype);

	pin = atoi(argv[1]);

	/*
	   Look up and parse the meter face

	   [meter-faces]
	   batvolts=scale(0,12.8,0),thevoltage,is,volts
	   winddir=range(0-33:north,34-96:west,97-160:south,161-224:east,225-255:north),thewindis,?
	   door=bit(closed,open),thedooris,?

	 */

	if (!(p = ast_variable_retrieve(myrpt->cfg, "meter-faces", argv[2]))) {
		ast_log(LOG_WARNING, "Meter face %s not found", argv[2]);
		ast_free(myargs);
		return -1;
	}

	if (!(meter_face = ast_strdup(p))) {
		ast_log(LOG_WARNING, "Out of memory");
		ast_free(myargs);
		return -1;
	}

	if (!strncmp("scale", meter_face, 5)) {	/* scale function? */
		metertype = 1;
		if ((!(end = strchr(meter_face, ')'))) || (!(start = strchr(meter_face, '('))) || (!end[1]) || (!end[2]) || (end[1] != ',')) {	/* Properly formed? */
			ast_log(LOG_WARNING, "Syntax error in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
		*start++ = 0;			/* Points to comma delimited scaling values */
		*end = 0;
		sounds = end + 2;		/* Start of sounds part */
		if (sscanf(start, "%f,%f,%f", &scalepre, &scalediv, &scalepost) != 3) {
			ast_log(LOG_WARNING, "Scale must have 3 args in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
		if (scalediv < 1.0) {
			ast_log(LOG_WARNING, "scalediv must be >= 1\n");
			ast_free(myargs);
			ast_free(meter_face);
			return -1;

		}
	} else if (!strncmp("range", meter_face, 5)) {	/* range function */
		metertype = 2;
		if ((!(end = strchr(meter_face, ')'))) || (!(start = strchr(meter_face, '('))) || (!end[1]) || (!end[2]) || (end[1] != ',')) {	/* Properly formed? */
			ast_log(LOG_WARNING, "Syntax error in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
		*start++ = 0;
		*end = 0;
		sounds = end + 2;
		/*
		 * Parse range entries
		 */
		if ((numranges = explode_string(start, range_strings, MAX_DAQ_RANGES, ',', 0)) < 2) {
			ast_log(LOG_WARNING, "At least 2 ranges required for range() in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}

	} else if (!strncmp("bit", meter_face, 3)) {	/* bit function */
		metertype = 3;
		if ((!(end = strchr(meter_face, ')'))) || (!(start = strchr(meter_face, '('))) || (!end[1]) || (!end[2]) || (end[1] != ',')) {	/* Properly formed? */
			ast_log(LOG_WARNING, "Syntax error in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
		*start++ = 0;
		*end = 0;
		sounds = end + 2;
		if (2 != explode_string(start, bitphrases, 2, ',', 0)) {
			ast_log(LOG_WARNING, "2 phrases required for bit() in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
	} else {
		ast_log(LOG_WARNING, "Meter face %s needs to specify one of scale, range or bit\n", argv[2]);
		ast_free(myargs);
		ast_free(meter_face);
		return -1;
	}

	/*
	 * Acquire 
	 */

	val = 0;
	if (pintype == 1) {
		res = daq_do_long(entry, pin, DAQ_CMD_ADC, NULL, &val, &filter);
		if (!res)
			scaledval = ((val + scalepre) / scalediv) + scalepost;
	} else {
		res = daq_do_long(entry, pin, DAQ_CMD_IN, NULL, &val, NULL);
	}

	if (res) {					/* DAQ Subsystem is down */
		ast_free(myargs);
		ast_free(meter_face);
		return res;
	}

	/*
	 * Select Range
	 */

	if (metertype == 2) {
		for (i = 0; i < numranges; i++) {
			if (2 != sscanf(range_strings[i], "%u-%u:", &rangemin, &rangemax)) {
				ast_log(LOG_WARNING, "Range variable error on meter face %s\n", argv[2]);
				ast_free(myargs);
				ast_free(meter_face);
				return -1;
			}
			if ((!(rangephrase = strchr(range_strings[i], ':')) || (!rangephrase[1]))) {
				ast_log(LOG_WARNING, "Range phrase missing on meter face %s\n", argv[2]);
				ast_free(myargs);
				ast_free(meter_face);
				return -1;
			}
			rangephrase++;
			if ((val >= rangemin) && (val <= rangemax))
				break;
		}
		if (i == numranges) {
			ast_log(LOG_WARNING, "Range missing on meter face %s for value %d\n", argv[2], val);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
	}

	if (rpt_debug_level() >= 3) {			/* Spew the variables */
		ast_debug(3, "device = %d, pin = %d, pintype = %d, metertype = %d\n", device, pin, pintype, metertype);
		ast_debug(3, "raw value = %d\n", val);
		if (metertype == 1) {
			ast_debug(3, "scalepre = %f, scalediv = %f, scalepost = %f\n", scalepre, scalediv, scalepost);
			ast_debug(3, "scaled value = %f\n", scaledval);
		}
		if (metertype == 2) {
			ast_debug(3, "Range phrase is: %s for meter face %s\n", rangephrase, argv[2]);
			ast_debug(3, "filtertype = %d\n", filtertype);
		}
		ast_debug(3, "sounds = %s\n", sounds);

	}

	/* Wait the normal telemetry delay time */

	if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1)
		goto done;

	/* Split up the sounds string */

	files = explode_string(sounds, sound_files, MAX_METER_FILES, ',', 0);
	if (files == 0) {
		ast_log(LOG_WARNING, "No sound files to say for meter %s\n", argv[2]);
		ast_free(myargs);
		ast_free(meter_face);
		return -1;
	}
	/* Say the files one by one acting specially on the ? character */
	res = 0;
	for (i = 0; i < files && !res; i++) {
		if (sound_files[i][0] == '?') {	/* Insert sample */
			if (metertype == 1) {
				int integer, decimal, precision = 0;
				if ((scalediv >= 10) && (scalediv < 100))	/* Adjust precision of decimal places */
					precision = 10;
				else if (scalediv >= 100)
					precision = 100;
				integer = (int) scaledval;
				valtoround = ((scaledval - integer) * precision);
				/* grrr.. inline lroundf doesn't work with uClibc! */
				decimal = (int) ((valtoround + ((valtoround >= 0) ? 0.5 : -0.5)));
				if ((precision) && (decimal == precision)) {
					decimal = 0;
					integer++;
				}
				ast_debug(1, "integer = %d, decimal = %d\n", integer, decimal);
				res = saynum(mychannel, integer);
				if (!res && precision && decimal) {
					res = sayfile(mychannel, "point");
					if (!res)
						res = saynum(mychannel, decimal);
				}
			}
			if (metertype == 2) {
				res = sayfile(mychannel, rangephrase);
			}
			if (metertype == 3) {
				res = sayfile(mychannel, bitphrases[(val) ? 1 : 0]);
			}

		} else {
			res = sayfile(mychannel, sound_files[i]);	/* Say the next word in the list */
		}
	}
  done:
	/* Done */
	ast_free(myargs);
	ast_free(meter_face);
	return 0;
}

int function_meter(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{

	if (myrpt->remote)
		return DC_ERROR;

	ast_debug(1, "meter param = %s, digitbuf = %s\n", (param) ? param : "(null)", digitbuf);

	rpt_telem_select(myrpt, command_source, mylink);
	rpt_telemetry(myrpt, METER, param);
	return DC_COMPLETE;
}

int function_userout(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{

	if (myrpt->remote)
		return DC_ERROR;

	ast_debug(1, "userout param = %s, digitbuf = %s\n", (param) ? param : "(null)", digitbuf);

	rpt_telem_select(myrpt, command_source, mylink);
	rpt_telemetry(myrpt, USEROUT, param);
	return DC_COMPLETE;
}

int function_cmd(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
	char *cp;

	if (myrpt->remote)
		return DC_ERROR;

	ast_debug(1, "cmd param = %s, digitbuf = %s\n", (param) ? param : "(null)", digitbuf);

	if (param) {
		if (*param == '#') {	/* to execute asterisk cli command */
			ast_cli_command(rpt_nullfd(), param + 1);
		} else {
			cp = ast_malloc(strlen(param) + 10);
			if (!cp) {
				ast_log(LOG_WARNING, "Unable to malloc");
				return DC_ERROR;
			}
			memset(cp, 0, strlen(param) + 10);
			sprintf(cp, "%s &", param);
			ast_safe_system(cp);
			ast_free(cp);
		}
	}
	return DC_COMPLETE;
}

void flush_telem(struct rpt *myrpt)
{
	struct rpt_tele *telem;
	ast_debug(3, "flush_telem()!!");
	rpt_mutex_lock(&myrpt->lock);
	telem = myrpt->tele.next;
	while (telem != &myrpt->tele) {
		if (telem->mode != SETREMOTE)
			ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);
		telem = telem->next;
	}
	rpt_mutex_unlock(&myrpt->lock);
}

void birdbath(struct rpt *myrpt)
{
	struct rpt_tele *telem;
	ast_debug(3, "birdbath!!");
	rpt_mutex_lock(&myrpt->lock);
	telem = myrpt->tele.next;
	while (telem != &myrpt->tele) {
		if (telem->mode == PARROT)
			ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);
		telem = telem->next;
	}
	rpt_mutex_unlock(&myrpt->lock);
}

void cancel_pfxtone(struct rpt *myrpt)
{
	struct rpt_tele *telem;
	ast_debug(3, "cancel_pfxfone!!");
	telem = myrpt->tele.next;
	while (telem != &myrpt->tele) {
		if (telem->mode == PFXTONE)
			ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);
		telem = telem->next;
	}
}
