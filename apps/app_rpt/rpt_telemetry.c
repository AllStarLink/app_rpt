
#include "asterisk.h"

#include <search.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "asterisk/channel.h"
#include "asterisk/cli.h"
#include "asterisk/pbx.h"		/* functions */
#include "asterisk/say.h"
#include "asterisk/indications.h"
#include "asterisk/format_cache.h" /* use ast_format_slin */
#include "asterisk/audiohook.h"

#include "app_rpt.h"

#ifdef HAVE_SYS_IO
#include <sys/io.h> /* use ioperm */
#endif

#include "rpt_lock.h"
#include "rpt_utils.h"
#include "rpt_daq.h"
#include "rpt_channel.h"
#include "rpt_config.h"
#include "rpt_bridging.h"
#include "rpt_call.h"
#include "rpt_link.h"
#include "rpt_serial.h"
#include "rpt_mdc1200.h"
#include "rpt_telemetry.h"
#include "rpt_capabilities.h"
#include "rpt_xcat.h"
#include "rpt_rig.h"

#define TELEM_TAIL_FILE_EXTN "TAIL"
#define TELEM_TIME_EXTN "TIME"

extern struct rpt rpt_vars[MAXRPTS];

/*** DOCUMENTATION
	<function name="RPT_TELEM_TIME" language="en_US">
		<synopsis>
			Read current time from telemetry datastore.
		</synopsis>
		<description>
			<para>Read current time for telemetry message from the telemetry datastore.
			Returns a string in the format epoch time.  Returns empty string if the datastore is not found.
			</para>
		</description>
	</function>
 ***/

/*!
 * \brief Free the datastore data
 * \param data The data to free
 *
 * This function is used to free the data stored in the telemetry datastore.
 */
static void rpt_telem_callback_destroy(void *data)
{
	ast_free(data);
}

static const struct ast_datastore_info telemetry_datastore = {
	.type = TELEM_DATASTORE,
	.destroy = rpt_telem_callback_destroy,
};

/*!
 * \brief Store the time in the telemetry datastore.
 * \param chan The channel to store the data in
 * \param t The time to store
 * \return 0 on success, -1 on failure
 */
static int rpt_telem_datastore(struct ast_channel *chan, time_t t)
{
	struct ast_datastore *datastore = ast_datastore_alloc(&telemetry_datastore, NULL);
	time_t *time_data;

	if (!datastore) {
		return -1;
	}
	time_data = ast_malloc(sizeof(time_t));
	if (!time_data) {
		return -1;
	}
	*time_data = t;
	datastore->data = time_data;
	ast_channel_datastore_add(chan, datastore);
	return 0;
}

static int rpt_telem_read_datastore(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_datastore *datastore = NULL;
	time_t *value;
	datastore = ast_channel_datastore_find(chan, &telemetry_datastore, NULL);
	if (!datastore || !datastore->data) {
		ast_copy_string(buf, "", len); /* The empty string causes SayUnixTime to "say" current time as a fallback*/
		return 0;
	}

	value = datastore->data;
	snprintf(buf, len, "%ld", (long) *value); /* Convert time_t to string for dialplan */
	return 0;
}

/*!
 * \brief Asterisk function returns time from rpt telemetry datastore
 */

static struct ast_custom_function rpt_read_telem_datastore_function = {
	.name = "RPT_TELEM_TIME",
	.read = rpt_telem_read_datastore,
};

int rpt_init_telemetry()
{
	return ast_custom_function_register(&rpt_read_telem_datastore_function);
}

int rpt_cleanup_telemetry()
{
	return ast_custom_function_unregister(&rpt_read_telem_datastore_function);
}

/* !
 * \brief determine if an extension exists in a primary or alternate context
 * \param chan The channel to check
 * \param primary The primary context to check
 * \param alternate The alternate context to check
 * \param exten The extension to check
 * \return The context if the extension exists in primary or alternate contexts, NULL otherwise
 */

static const char *rpt_telem_extension(struct ast_channel *chan, const char *primary, const char *alternate, const char *exten)
{
	if (ast_exists_extension(chan, primary, exten, 1, NULL)) {
		return primary;
	}
	if (ast_exists_extension(chan, alternate, exten, 1, NULL)) {
		return alternate;
	}
	return NULL;
}

/*!
 * \brief Say the time
 * \param mychannel The channel to speak on
 * \param t The time to say
 * \param timezone The timezone to use for the time
 */
static void rpt_say_time(struct ast_channel *mychannel, time_t t, const char *timezone)
{
	time_t t1;
	struct ast_tm localtm;
	char *p;
	int res;

	rpt_localtime(&t, &localtm, timezone);
	t1 = rpt_mktime(&localtm, NULL);
	/* Say the phase of the day is before the time */
	if ((localtm.tm_hour >= 0) && (localtm.tm_hour < 12)) {
		p = "rpt/goodmorning";
	} else if ((localtm.tm_hour >= 12) && (localtm.tm_hour < 18)) {
		p = "rpt/goodafternoon";
	} else {
		p = "rpt/goodevening";
	}
	if (sayfile(mychannel, p) == -1) {
		return;
	}
	/* Say the time is ... */
	if (sayfile(mychannel, "rpt/thetimeis") == -1) {
		return;
	}
	/* Say the time */
	res = ast_say_time(mychannel, t1, "", ast_channel_language(mychannel));
	if (!res) {
		res = ast_waitstream(mychannel, "");
	}
	ast_stopstream(mychannel);
}
/*!
 * \brief Execute dialplan on conference conference
 */
static enum ast_pbx_result rpt_do_dialplan(struct ast_channel *dpchannel, char *exten, const char *context)
{
	rpt_disable_cdr(dpchannel);
	ast_channel_context_set(dpchannel, context);
	ast_channel_exten_set(dpchannel, exten);
	return ast_pbx_run(dpchannel);
}

void rpt_telem_select(struct rpt *myrpt, int command_source, struct rpt_link *mylink)
{
	enum rpt_linkmode src;

	if (mylink && mylink->chan) {
		src = LINKMODE_GUI;
		if (mylink->phonemode) {
			src = LINKMODE_PHONE;
		} else if (CHAN_TECH(mylink->chan, "echolink")) {
			src = LINKMODE_ECHOLINK;
		} else if (CHAN_TECH(mylink->chan, "tlb")) {
			src = LINKMODE_TLB;
		}
		if (myrpt->p.linkmodedynamic[src] && (mylink->linkmode >= 1) && (mylink->linkmode < 0x7ffffffe)) {
			mylink->linkmode = LINK_HANG_TIME;
		}
	}
	if (!myrpt->p.telemdynamic) {
		return;
	}
	if (myrpt->telemmode == 0) {
		return;
	}
	if (myrpt->telemmode == 0x7fffffff) {
		return;
	}
	myrpt->telemmode = TELEM_HANG_TIME;
}

/*
 * Parse a METER request for telemetry thread.
 * This is passed in a comma separated list of items from the function table entry
 * There should be 3 or 4 fields in the function table entry: device, channel,
 * meter face, and  optionally: filter
 */
static int handle_meter_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *args)
{
	int i, res, files, val;
	enum rpt_daq_filter filter;
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
		return -1;
	}

	i = explode_string(myargs, argv, ARRAY_LEN(argv), ',', 0);
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
		*start++ = 0; /* Points to comma delimited scaling values */
		*end = 0;
		sounds = end + 2; /* Start of sounds part */
		if (sscanf(start, N_FMT(f) "," N_FMT(f) "," N_FMT(f), &scalepre, &scalediv, &scalepost) != 3) {
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
		if ((numranges = explode_string(start, range_strings, ARRAY_LEN(range_strings), ',', 0)) < 2) {
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
		if (2 != explode_string(start, bitphrases, ARRAY_LEN(bitphrases), ',', 0)) {
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

	/* Acquire */
	val = 0;
	if (pintype == 1) {
		res = daq_do_long(entry, pin, DAQ_CMD_ADC, NULL, &val, &filter);
		if (!res) {
			scaledval = ((val + scalepre) / scalediv) + scalepost;
		}
	} else {
		res = daq_do_long(entry, pin, DAQ_CMD_IN, NULL, &val, NULL);
	}

	if (res) {					/* DAQ Subsystem is down */
		ast_free(myargs);
		ast_free(meter_face);
		return res;
	}

	/* Select Range */
	if (metertype == 2) {
		for (i = 0; i < numranges; i++) {
			if (2 != sscanf(range_strings[i], N_FMT(u) "-" N_FMT(u) ":", &rangemin, &rangemax)) {
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
	if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
		goto done;
	}

	/* Split up the sounds string */
	files = explode_string(sounds, sound_files, ARRAY_LEN(sound_files), ',', 0);
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
				if ((scalediv >= 10) && (scalediv < 100)) {	/* Adjust precision of decimal places */
					precision = 10;
				} else if (scalediv >= 100) {
					precision = 100;
				}
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
					if (!res) {
						res = saynum(mychannel, decimal);
					}
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

int priority_telemetry_pending(struct rpt *myrpt)
{
	struct rpt_tele *telem;
	int pending = 0;
	if (!myrpt) {
		return 0;
	}
	rpt_mutex_lock(&myrpt->lock);
	telem = myrpt->tele.next;
	/* Traverse the telemetry list looking for override_tot flag */
	while ((telem != &myrpt->tele) && !pending) {
		/*
		 * Add more telemetry modes here if they need
		 * to be sent during a time out condition
		 */
		switch (telem->mode) {
		case TIMEOUT:
			ast_debug(3, "Transmit override: Priority telemetry message pending (TIMEOUT).");
			pending = 1;
			break;
		default:
			break;
		}
		telem = telem->next;
	}
	rpt_mutex_unlock(&myrpt->lock);
	return pending;
}
/*! \brief Try to log setting active_telem NULL when we weren't what it was set to
 * If somebody sets active_telem to NULL when it wasn't the current telem, then
 * that can cause a queued telemetry to think the current telem is done when it isn't,
 * and things will get doubled up.
 */
#define telem_done(myrpt, telem) \
	ast_debug(5, "Ending telemetry, active_telem = %p, mytele = %p\n", myrpt->active_telem, telem); \
	if (myrpt->active_telem && myrpt->active_telem != telem) { \
		ast_log(LOG_WARNING, "Attempting to clear active_telem %p when telem is %p", myrpt->active_telem, telem); \
	} \
	if (myrpt->active_telem == telem) { \
		myrpt->active_telem = NULL; \
	}

void flush_telem(struct rpt *myrpt)
{
	struct rpt_tele *telem;
	ast_debug(3, "flush_telem()!!");
	rpt_mutex_lock(&myrpt->lock);
	telem = myrpt->tele.next;
	while (telem != &myrpt->tele) {
		if (telem->mode != SETREMOTE && telem->chan) {
			ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);
			if (myrpt->active_telem == telem) {
				/* If we are the active telemetry, we need to clean it up */
				telem_done(myrpt, telem);
			}
		}
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
		if (telem->mode == PARROT && telem->chan) {
			ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);
			if (myrpt->active_telem == telem) {
				/* If we are the active telemetry, we need to clean it up */
				telem_done(myrpt, telem);
			}
		}
		telem = telem->next;
	}
	rpt_mutex_unlock(&myrpt->lock);
}

void cancel_pfxtone(struct rpt *myrpt)
{
	struct rpt_tele *telem;

	if (!myrpt->p.dopfxtone) {
		return;
	}
	ast_debug(3, "cancel_pfxfone!!");
	rpt_mutex_lock(&myrpt->lock);
	telem = myrpt->tele.next;
	while (telem != &myrpt->tele) {
		if (telem->mode == PFXTONE && telem->chan) {
			ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);
			if (myrpt->active_telem == telem) {
				/* If we are the active telemetry, we need to clean it up */
				telem_done(myrpt, telem);
			}
		}
		telem = telem->next;
	}
	rpt_mutex_unlock(&myrpt->lock);
}
static int telm_qwrite_cb(void *obj, void *arg, int flags)
{
	struct rpt_link *link = obj;
	struct ast_frame *wf = arg;

	if (link->chan && (link->mode == MODE_TRANSCEIVE)) {
		rpt_qwrite(link, wf);
	}

	return 0;
}

static void send_tele_link(struct rpt *myrpt, char *cmd)
{
	int len;
	char *str;
	struct ast_frame wf;

	len = ast_asprintf(&str, "T %s %s", myrpt->name, cmd);
	if (len < 0) {
		return;
	}

	init_text_frame(&wf, "send_tele_link");
	wf.data.ptr = str;
	wf.datalen = len + 1;
	/* give it to everyone */
	ao2_callback(myrpt->links, OBJ_MULTIPLE | OBJ_NODATA, telm_qwrite_cb, &wf);
	ast_free(str);
	rpt_telemetry(myrpt, VARCMD, cmd);
}

/*! Send telemetry tones */
static int send_tone_telemetry(struct ast_channel *chan, const char *tonestring)
{
	char *p, *stringp;
	char *tonesubset;
	int f1, f2;
	int duration;
	int amplitude;
	int res;

	res = 0;

	if (!tonestring) {
		return res;
	}

	p = stringp = ast_strdup(tonestring);

	for (; tonestring;) {
		tonesubset = strsep(&stringp, ")");
		if (!tonesubset) {
			break;
		}
		if (sscanf(tonesubset, "(" N_FMT(d) "," N_FMT(d) "," N_FMT(d) "," N_FMT(d), &f1, &f2, &duration, &amplitude) != 4) {
			break;
		}
		res = play_tone_pair(chan, f1, f2, duration, amplitude);
		if (res) {
			break;
		}
	}
	if (p) {
		ast_free(p);
	}
	if (!res) {
		res = play_tone_pair(chan, 0, 0, 100, 0);	/* This is needed to ensure the last tone segment is timed correctly */
	}

	if (!res) {
		res = ast_waitstream(chan, "");
	}

	ast_stopstream(chan);

	return res;
}

static int telem_any(struct rpt *myrpt, struct ast_channel *chan, const char *entry, const char *why)
{
	int res;
	char c;

	int morsespeed;
	int morsefreq;
	int morseampl;
	int morseidfreq;
	int morseidampl;

	res = 0;

	morsespeed = retrieve_astcfgint(myrpt, myrpt->p.morse, "speed", 5, 35, 20);
	morsefreq = retrieve_astcfgint(myrpt, myrpt->p.morse, "frequency", 300, 3000, 800);
	morseampl = retrieve_astcfgint(myrpt, myrpt->p.morse, "amplitude", 200, 8192, 4096);
	morseidampl = retrieve_astcfgint(myrpt, myrpt->p.morse, "idamplitude", 200, 8192, 2048);
	morseidfreq = retrieve_astcfgint(myrpt, myrpt->p.morse, "idfrequency", 300, 3000, 330);

	/* Is it a file, or a tone sequence? */

	if (entry[0] == '|') {
		c = entry[1];
		if ((c >= 'a') && (c <= 'z')) {
			c -= 0x20;
		}

		switch (c) {
		case 'I':				/* Morse ID */
			donodelog_fmt(myrpt, "TELEMETRY,%s,%s(I)", myrpt->name, why);
			res = send_morse(chan, entry + 2, morsespeed, morseidfreq, morseidampl);
			break;
		case 'M':				/* Morse Message */
			donodelog_fmt(myrpt, "TELEMETRY,%s,%s(M)", myrpt->name, why);
			res = send_morse(chan, entry + 2, morsespeed, morsefreq, morseampl);
			break;
		case 'T':				/* Tone sequence */
			donodelog_fmt(myrpt, "TELEMETRY,%s,%s(T),%s", myrpt->name, why, entry + 2);
			res = send_tone_telemetry(chan, entry + 2);
			break;
		default:
			res = -1;
		}
	} else {
		donodelog_fmt(myrpt, "TELEMETRY,%s,%s,%s", myrpt->name, why, entry);
		res = sayfile(chan, entry); /* File */
	}
	return res;
}

/*! \brief Telemetry defaults */
static struct telem_defaults tele_defs[] = {
	{ "ct1", "|t(350,0,100,3072)(500,0,100,3072)(660,0,100,3072)" },
	{ "ct2", "|t(660,880,150,3072)" },
	{ "ct3", "|t(440,0,150,3072)" },
	{ "ct4", "|t(550,0,150,3072)" },
	{ "ct5", "|t(660,0,150,3072)" },
	{ "ct6", "|t(880,0,150,3072)" },
	{ "ct7", "|t(660,440,150,3072)" },
	{ "ct8", "|t(700,1100,150,3072)" },
	{ "remotemon", "|t(1600,0,75,2048)" },
	{ "remotetx", "|t(2000,0,75,2048)(0,0,75,0)(1600,0,75,2048)" },
	{ "cmdmode", "|t(900,904,200,2048)" },
	{ "functcomplete", "|t(1000,0,100,2048)(0,0,100,0)(1000,0,100,2048)" },
	{ "remcomplete", "|t(650,0,100,2048)(0,0,100,0)(650,0,100,2048)(0,0,100,0)(650,0,100,2048)" },
	{ "pfxtone", "|t(350,440,30000,3072)" }
};

/*! \brief Looks up a telemetry name in the config file, and does a telemetry response as configured.
 * 4 types of telemetry are handled: Morse ID, Morse Message, Tone Sequence, and a File containing a recording.
 */
static int telem_lookup(struct rpt *myrpt, struct ast_channel *chan, const char *name, const char *why)
{
	int res;
	const char *entry;

	res = 0;
	entry = ast_variable_retrieve(myrpt->cfg, myrpt->p.telemetry, name);
	if (!entry) {
		int i;

		/* Telemetry name wasn't found in the config file, use the default */
		for (i = 0; i < sizeof(tele_defs) / sizeof(struct telem_defaults); i++) {
			if (!strcasecmp(tele_defs[i].name, name)) {
				entry = tele_defs[i].value;
			}
		}
	}
	if (!entry) {
		res = -1;
	}

	if (!res && !ast_strlen_zero(entry) && chan) {
		res = telem_any(myrpt, chan, entry, why);
	} else {
		donodelog_fmt(myrpt, "TELEMETRY,%s,%s*", myrpt->name, why);
	}
	return res;
}

/*! \brief Send telemetry for node
 * Look up telemetry type (e.g. "unlinkedct", "linkunkeyct") for node and, if
 * available, send the associated telemetry
 */
static int telem_send_ct(struct rpt *myrpt, struct ast_channel *chan, const char *ct_key, const char *why, int delay)
{
	const char *ct;
	int res;

	ct = ast_variable_retrieve(myrpt->cfg, myrpt->name, ct_key);
	if (!ct || ast_strlen_zero(ct)) {
		donodelog_fmt(myrpt, "TELEMETRY,%s,%s*", myrpt->name, why);
		return 0;
	}

	if (delay) {
		ast_safe_sleep(chan, delay);
	}

	myrpt->noduck = 1;
	res = telem_lookup(myrpt, chan, ct, why);
	if (res) {
		ast_log(LOG_WARNING, "telem_lookup: ct \"%s\" (%s) failed on %s\n", ct_key, ct, ast_channel_name(chan));
	}
	return res;
}

/*! \brief Processes various telemetry commands that are in the myrpt structure
 * Used extensively when links are built or torn down and other events are processed
 * by the rpt_master threads.
 */
static void handle_varcmd_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *varcmd)
{
	char *strs[100], buf[100], c;
	const char *context;
	int i, j, k, n, res, vmajor, vminor;
	float f;
	unsigned int t1;
	n = finddelim(varcmd, strs, ARRAY_LEN(strs));
	if (n < 1) {
		return;
	}
	if (!strcasecmp(strs[0], "REMGO")) {
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,REMGO", myrpt->name);
		sayfile(mychannel, "rpt/remote_go");
		return;
	}
	if (!strcasecmp(strs[0], "REMALREADY")) {
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,REMALREADY", myrpt->name);
		sayfile(mychannel, "rpt/remote_already");
		return;
	}
	if (!strcasecmp(strs[0], "REMNOTFOUND")) {
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,REMNOTFOUND", myrpt->name);
		sayfile(mychannel, "rpt/remote_notfound");
		return;
	}
	if (!strcasecmp(strs[0], "COMPLETE")) {
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		res = telem_lookup(myrpt, mychannel, "functcomplete", "COMPLETE");
		if (!res) {
			res = ast_waitstream(mychannel, "");
		} else {
			ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
		}
		ast_stopstream(mychannel);
		return;
	}
	if (!strcasecmp(strs[0], "PROC")) {
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		res = telem_lookup(myrpt, mychannel, "patchup", "PROC");
		if (res < 0) { /* Then default message */
			sayfile(mychannel, "rpt/callproceeding");
		}
		return;
	}
	if (!strcasecmp(strs[0], "TERM")) {
		/* wait a little bit longer */
		if (wait_interval(myrpt, DLY_CALLTERM, mychannel) == -1) {
			return;
		}
		res = telem_lookup(myrpt, mychannel, "patchdown", "TERM");
		if (res < 0) { /* Then default message */
			sayfile(mychannel, "rpt/callterminated");
		}
		return;
	}
	if (!strcasecmp(strs[0], "MACRO_NOTFOUND")) {
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,MACRO_NOTFOUND", myrpt->name);
		sayfile(mychannel, "rpt/macro_notfound");
		return;
	}
	if (!strcasecmp(strs[0], "MACRO_BUSY")) {
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,MACRO_BUSY", myrpt->name);
		sayfile(mychannel, "rpt/macro_busy");
		return;
	}
	if (!strcasecmp(strs[0], "CONNECTED")) {
		if (n < 3) {
			return;
		}
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,CONNECTED,%s", myrpt->name, strs[2]);
		res = saynode(myrpt, mychannel, strs[2]);
		if (!res) {
			res = ast_stream_and_wait(mychannel, "rpt/connected-to", "");
		}
		saynode(myrpt, mychannel, strs[1]);
		return;
	}
	if (!strcasecmp(strs[0], "CONNFAIL")) {
		if (n < 2) {
			return;
		}
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,CONNFAIL,%s", myrpt->name, strs[1]);
		res = saynode(myrpt, mychannel, strs[1]);
		if (!res) {
			sayfile(mychannel, "rpt/connection_failed");
		}
		return;
	}
	if (!strcasecmp(strs[0], "REMDISC")) {

		if (n < 2)
			return;
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,REMDISC,%s", myrpt->name, strs[1]);
		res = saynode(myrpt, mychannel, strs[1]);
		if (!res)
			sayfile(mychannel, "rpt/remote_disc");
		return;
	}
	if (!strcasecmp(strs[0], "STATS_TIME")) {
		if (n < 2) {
			return;
		}
		if (sscanf(strs[1], N_FMT(u), &t1) != 1) {
			return;
		}
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,STATS_TIME", myrpt->name);
		context = rpt_telem_extension(mychannel, myrpt->p.telemetry, TELEMETRY, TELEM_TIME_EXTN);
		if (context) {
			if (rpt_telem_datastore(mychannel, t1) < 0) {
				return;
			}
			rpt_do_dialplan(mychannel, TELEM_TIME_EXTN, context);
			return;
		}

		rpt_say_time(mychannel, t1, myrpt->p.timezone);
		return;
	}
	if (!strcasecmp(strs[0], "STATS_VERSION")) {
		if (n < 2) {
			return;
		}
		if (sscanf(strs[1], N_FMT(d) "." N_FMT(d), &vmajor, &vminor) != 2) {
			return;
		}
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,STATS_VERSION", myrpt->name);
		/* Say "version" */
		if (sayfile(mychannel, "rpt/version") == -1) {
			return;
		}
		res = ast_say_number(mychannel, vmajor, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		if (saycharstr(mychannel, ".") == -1) {
			return;
		}
		if (!res) {				/* Say "Y" */
			ast_say_number(mychannel, vminor, "", ast_channel_language(mychannel), (char *) NULL);
		}
		if (!res) {
			res = ast_waitstream(mychannel, "");
			ast_stopstream(mychannel);
		} else {
			ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
		}
		return;
	}
	if (!strcasecmp(strs[0], "STATS_GPS")) {
		if (n < 5) {
			return;
		}
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,STATS_GPS", myrpt->name);
		if (saynode(myrpt, mychannel, strs[1]) == -1) {
			return;
		}
		if (sayfile(mychannel, "location") == -1) {
			return;
		}
		c = *(strs[2] + strlen(strs[2]) - 1);
		*(strs[2] + strlen(strs[2]) - 1) = 0;
		if (sscanf(strs[2], "%2d" N_FMT(d) "." N_FMT(d), &i, &j, &k) != 3) {
			return;
		}
		res = ast_say_number(mychannel, i, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		if (sayfile(mychannel, "degrees") == -1) {
			return;
		}
		res = ast_say_number(mychannel, j, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		if (saycharstr(mychannel, strs[2] + 4) == -1) {
			return;
		}
		if (sayfile(mychannel, "minutes") == -1) {
			return;
		}
		if (sayfile(mychannel, (c == 'N') ? "north" : "south") == -1) {
			return;
		}
		if (sayfile(mychannel, "rpt/latitude") == -1) {
			return;
		}
		c = *(strs[3] + strlen(strs[3]) - 1);
		*(strs[3] + strlen(strs[3]) - 1) = 0;
		if (sscanf(strs[3], "%3d" N_FMT(d) "." N_FMT(d), &i, &j, &k) != 3) {
			return;
		}
		res = ast_say_number(mychannel, i, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		if (sayfile(mychannel, "degrees") == -1) {
			return;
		}
		res = ast_say_number(mychannel, j, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		if (saycharstr(mychannel, strs[3] + 5) == -1) {
			return;
		}
		if (sayfile(mychannel, "minutes") == -1) {
			return;
		}
		if (sayfile(mychannel, (c == 'E') ? "east" : "west") == -1) {
			return;
		}
		if (sayfile(mychannel, "rpt/longitude") == -1) {
			return;
		}
		if (!*strs[4]) {
			return;
		}
		c = *(strs[4] + strlen(strs[4]) - 1);
		*(strs[4] + strlen(strs[4]) - 1) = 0;
		if (sscanf(strs[4], N_FMT(f), &f) != 1) {
			return;
		}
		if (myrpt->p.gpsfeet) {
			if (c == 'M') {
				f *= 3.2808399;
			}
		} else {
			if (c != 'M') {
				f /= 3.2808399;
			}
		}
		snprintf(buf, sizeof(buf), "%0.1f", f);
		if (sscanf(buf, N_FMT(d) "." N_FMT(d), &i, &j) != 2) {
			return;
		}
		res = ast_say_number(mychannel, i, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		if (saycharstr(mychannel, ".") == -1) {
			return;
		}
		res = ast_say_number(mychannel, j, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		if (sayfile(mychannel, (myrpt->p.gpsfeet) ? "feet" : "meters") == -1) {
			return;
		}
		if (saycharstr(mychannel, "AMSL") == -1) {
			return;
		}
		ast_stopstream(mychannel);
		return;
	}
	if (!strcasecmp(strs[0], "ARB_ALPHA")) {
		if (n < 2) {
			return;
		}
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,ARB_ALPHA", myrpt->name);
		saycharstr(mychannel, strs[1]);
		return;
	}
	if (!strcasecmp(strs[0], "REV_PATCH")) {
		/* Parts of this section taken from app_parkandannounce */
		char *tpl_working, *tpl_current, *tpl_copy;
		char *tmp[100], *myparm;
		int looptemp = 0, i = 0, dres = 0;

		if (n < 3) {
			return;
		}
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,REV_PATCH", myrpt->name);
		tpl_working = ast_strdup(strs[2]);
		tpl_copy = tpl_working;
		myparm = strsep(&tpl_working, "^");
		tpl_current = strsep(&tpl_working, ":");
		while (tpl_current && looptemp < sizeof(tmp)) {
			tmp[looptemp] = tpl_current;
			looptemp++;
			tpl_current = strsep(&tpl_working, ":");
		}
		for (i = 0; i < looptemp; i++) {
			if (!strcmp(tmp[i], "PARKED")) {
				ast_say_digits(mychannel, atoi(myparm), "", ast_channel_language(mychannel));
			} else if (!strcmp(tmp[i], "NODE")) {
				ast_say_digits(mychannel, atoi(strs[1]), "", ast_channel_language(mychannel));
			} else {
				dres = ast_streamfile(mychannel, tmp[i], ast_channel_language(mychannel));
				if (!dres) {
					dres = ast_waitstream(mychannel, "");
				} else {
					ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n", tmp[i], ast_channel_name(mychannel));
					dres = 0;
				}
			}
		}
		ast_free(tpl_copy);
		return;
	}
	if (!strcasecmp(strs[0], "LASTNODEKEY")) {
		if (n < 2) {
			return;
		}
		if (!atoi(strs[1])) {
			return;
		}
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,LASTNODEKEY,%s", myrpt->name, strs[1]);
		saynode(myrpt, mychannel, strs[1]);
		return;
	}
	if (!strcasecmp(strs[0], "LASTUSER")) {
		if (n < 2) {
			return;
		}
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,LASTUSER,%s", myrpt->name, strs[1]);
		sayphoneticstr(mychannel, strs[1]);
		if (n < 3) {
			return;
		}
		sayfile(mychannel, "and");
		sayphoneticstr(mychannel, strs[2]);
		return;
	}
	if (!strcasecmp(strs[0], "STATUS")) {
		if (n < 3) {
			return;
		}
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			return;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,STATUS,%s", myrpt->name, strs[1]);
		saynode(myrpt, mychannel, strs[1]);
		if (atoi(strs[2]) > 0) {
			sayfile(mychannel, "rpt/autopatch_on");
		} else if (n == 3) {
			sayfile(mychannel, "rpt/repeat_only");
			return;
		}
		for (i = 3; i < n; i++) {
			saynode(myrpt, mychannel, strs[i] + 1);
			if (*strs[i] == 'T') {
				sayfile(mychannel, "rpt/tranceive");
			} else if (*strs[i] == 'R') {
				sayfile(mychannel, "rpt/monitor");
			} else if (*strs[i] == 'L') {
				sayfile(mychannel, "rpt/localmonitor");
			} else {
				sayfile(mychannel, "rpt/connecting");
			}
		}
		return;
	}
	ast_log(LOG_WARNING, "Got unknown link telemetry command: %s\n", strs[0]);
}

/*
 * Threaded telemetry handling routines - goes hand in hand with handle_varcmd_tele (see above)
 * This routine does a lot of processing of what you "hear" when app_rpt is running.
 * Note that this routine could probably benefit from an overhaul to make it easier to read/debug.
 * Many of the items here seem to have been bolted onto this routine as app_rpt has evolved.
 */
void *rpt_tele_thread(void *this)
{
	int res = 0, pbx = 0, haslink, hastx, hasremote, imdone = 0, unkeys_queued, x, n = 1;
	struct rpt_tele *mytele = (struct rpt_tele *) this;
	struct rpt_tele *tlist;
	struct rpt *myrpt;
	struct rpt_link *l;
	struct ast_channel *mychannel = NULL;
	struct ao2_iterator l_it;
	struct ao2_container *links_copy;
	int id_malloc = 0, m;
	char *p, *ident, *nodename;
	const char *context;
	time_t t, t_mono, was_mono;
	char **strs;
	int i, j, k, ns, rbimode;
	char mhz[MAXREMSTR], decimals[MAXREMSTR], mystr[200];
	float f;
	unsigned long long u_mono;
	char gps_data[100], lat[LAT_SZ], lon[LON_SZ], elev[ELEV_SZ], c;
	struct ast_str *lbuf = NULL;
	enum rpt_conf_type type;

#ifdef	_MDC_ENCODE_H_
	struct mdcparams *mdcp;
#endif
	struct ast_format_cap *cap;

	/* get a pointer to myrpt */
	myrpt = mytele->rpt;

	/* Snag copies of a few key myrpt variables */
	rpt_mutex_lock(&myrpt->lock);
	nodename = ast_strdup(myrpt->name);
	if (!nodename) {
		goto abort3;
	}

	if (myrpt->p.ident) {
		ident = ast_strdup(myrpt->p.ident);
		if (!ident) {
			id_malloc = 0;
			goto abort2; /* Didn't set active_telem, so goto abort2, not abort. */
		} else {
			id_malloc = 1;
		}
	} else {
		ident = "";
		id_malloc = 0;
	}
	rpt_mutex_unlock(&myrpt->lock);
	rpt_mutex_lock(&myrpt->lock);
	/* Wait for previous telemetry to finish before we start so we're not speaking on top of each other. */
	ast_debug(5, "Queued telemetry, active_telem = %p, mytele = %p\n", myrpt->active_telem, mytele);
	while (myrpt->active_telem && ((myrpt->active_telem->mode == PAGE) || (myrpt->active_telem->mode == MDC1200))) {
		rpt_mutex_unlock(&myrpt->lock);
		usleep(100000);
		rpt_mutex_lock(&myrpt->lock);
	}
	rpt_mutex_unlock(&myrpt->lock);

	while ((mytele->mode != SETREMOTE) && (mytele->mode != UNKEY) && (mytele->mode != LINKUNKEY) && (mytele->mode != LOCUNKEY) &&
		   (mytele->mode != COMPLETE) && (mytele->mode != REMGO) && (mytele->mode != REMCOMPLETE)) {
		rpt_mutex_lock(&myrpt->lock);
		if ((!myrpt->active_telem) && (myrpt->tele.prev == mytele)) {
			myrpt->active_telem = mytele;
			rpt_mutex_unlock(&myrpt->lock);
			break;
		}
		rpt_mutex_unlock(&myrpt->lock);
		usleep(100000);
	}

	ast_debug(5, "Beginning telemetry, active_telem = %p, mytele = %p\n", myrpt->active_telem, mytele);

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		rpt_mutex_lock(&myrpt->lock);
		goto abort2; /* Didn't set active_telem, so goto abort2, not abort. */
	}

	ast_format_cap_append(cap, ast_format_slin, 0);
	/* allocate a local channel thru asterisk and call the correct conference */
	mychannel = rpt_request_telem_chan(cap, "Telem");
	ao2_ref(cap, -1);

	if (!mychannel) {
		ast_log(LOG_WARNING, "Unable to obtain local channel (mode: %d)\n", mytele->mode);
		rpt_mutex_lock(&myrpt->lock);
		goto abort2; /* Didn't set active_telem, so goto abort2, not abort. */
	}
	ast_debug(1, "Requested channel %s\n", ast_channel_name(mychannel));
	ast_channel_ref(mychannel); /* Create a reference to prevent channel from being freed too soon */
	mytele->chan = mychannel;

	switch (mytele->mode) {
	case ID1:
	case PLAYBACK:
	case TEST_TONE:
	case STATS_GPS_LEGACY:
		type = RPT_CONF;
		break;
	default:
		type = RPT_TXCONF;
		break;
	}

	if (rpt_conf_add(mychannel, myrpt, type)) {
		ast_log(LOG_WARNING, "Unable to join local channel to conference %s\n", type == RPT_CONF ? RPT_CONF_NAME : RPT_TXCONF_NAME);
		rpt_mutex_lock(&myrpt->lock);
		goto abort;
	}

	if (ast_audiohook_volume_set_float(mychannel, AST_AUDIOHOOK_DIRECTION_WRITE, myrpt->p.telemnomgain)) {
		ast_log(LOG_WARNING, "Setting the volume on channel %s to %2.2f failed", ast_channel_name(mychannel), myrpt->p.telemnomgain);
	}

	ast_stopstream(mychannel);
	res = 0;
	switch (mytele->mode) {
	case USEROUT:
		handle_userout_tele(myrpt, mychannel, mytele->param);
		imdone = 1;
		break;
	case METER:
		handle_meter_tele(myrpt, mychannel, mytele->param);
		imdone = 1;
		break;
	case VARCMD:
		handle_varcmd_tele(myrpt, mychannel, mytele->param);
		imdone = 1;
		break;
	case ID:
	case ID1:
		if (*ident) {
			/* wait a bit */
			if (wait_interval(myrpt, (mytele->mode == ID) ? DLY_ID : DLY_TELEM, mychannel) == -1) {
				imdone = 1;
				break;
			}
			res = telem_any(myrpt, mychannel, ident, mytele->mode == ID ? "ID" : "ID1");
		}
		imdone = 1;
		break;
	case TAILMSG:
		/* wait a little bit longer */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		context = rpt_telem_extension(mychannel, myrpt->p.telemetry, TELEMETRY, TELEM_TAIL_FILE_EXTN);
		if (context) {
			donodelog_fmt(myrpt, "TELEMETRY,%s,TAILMSG, Extension %s, Context %s", myrpt->name, TELEM_TAIL_FILE_EXTN, context);
			rpt_do_dialplan(mychannel, TELEM_TAIL_FILE_EXTN, context);
			pbx = 1;
		} else if (myrpt->p.tailmessages[0]) {
			donodelog_fmt(myrpt, "TELEMETRY,%s,TAILMSG,%s", myrpt->name, myrpt->p.tailmessages[myrpt->tailmessagen]);
			res = ast_streamfile(mychannel, myrpt->p.tailmessages[myrpt->tailmessagen], ast_channel_language(mychannel));
		}
		break;
	case IDTALKOVER:
		ast_debug(7, "Tracepoint IDTALKOVER: in rpt_tele_thread()\n");
		p = (char *) ast_variable_retrieve(myrpt->cfg, nodename, "idtalkover");
		if (p) {
			res = telem_any(myrpt, mychannel, p, "IDTALKOVER");
		}
		imdone = 1;
		break;
	case PROC:
		/* wait a little bit longer */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		res = telem_lookup(myrpt, mychannel, "patchup", "PROC");
		if (res < 0) { /* Then default message */
			res = ast_streamfile(mychannel, "rpt/callproceeding", ast_channel_language(mychannel));
		}
		break;
	case TERM:
		/* wait a little bit longer */
		if (wait_interval(myrpt, DLY_CALLTERM, mychannel) == -1) {
			break;
		}
		res = telem_lookup(myrpt, mychannel, "patchdown", "TERM");
		if (res < 0) { /* Then default message */
			res = ast_streamfile(mychannel, "rpt/callterminated", ast_channel_language(mychannel));
		}
		break;
	case COMPLETE:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		res = telem_lookup(myrpt, mychannel, "functcomplete", "COMPLETE");
		break;
	case REMCOMPLETE:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		res = telem_lookup(myrpt, mychannel, "remcomplete", "REMCOMPLETE");
		break;
	case MACRO_NOTFOUND:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,MACRO_NOTFOUND", myrpt->name);
		res = ast_streamfile(mychannel, "rpt/macro_notfound", ast_channel_language(mychannel));
		break;
	case MACRO_BUSY:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,MACRO_BUSY", myrpt->name);
		res = ast_streamfile(mychannel, "rpt/macro_busy", ast_channel_language(mychannel));
		break;
	case PAGE:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			imdone = 1;
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,PAGE", myrpt->name);
		res = -1;
		if (mytele->submode.p) {
			myrpt->noduck = 1;
			res = ast_playtones_start(mychannel, 0, (char *) mytele->submode.p, 0);
			while (ast_channel_generatordata(mychannel)) {
				if (ast_safe_sleep(mychannel, 50)) {
					res = -1;
					break;
				}
			}
			ast_free((char *) mytele->submode.p);
		}
		imdone = 1;
		break;
#ifdef	_MDC_ENCODE_H_
	case MDC1200:
		mdcp = (struct mdcparams *) mytele->submode.p;
		if (mdcp) {
			if (mdcp->type[0] != 'A') {
				if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
					res = -1;
					imdone = 1;
					break;
				}
			} else {
				if (wait_interval(myrpt, DLY_MDC1200, mychannel) == -1) {
					res = -1;
					imdone = 1;
					break;
				}
			}
			res = mdc1200gen_start(mychannel, mdcp->type, mdcp->UnitID, mdcp->DestID, mdcp->subcode);
			ast_free(mdcp);
			while (ast_channel_generatordata(mychannel)) {
				if (ast_safe_sleep(mychannel, 50)) {
					res = -1;
					break;
				}
			}
		}
		imdone = 1;
		break;
#endif
	case UNKEY:
	case LOCUNKEY:
		if (myrpt->patchnoct && (myrpt->callmode != CALLMODE_DOWN)) { /* If no CT during patch configured, then don't send one */
			imdone = 1;
			break;
		}

		/* Reset the Unkey to CT timer */
		x = get_wait_interval(myrpt, DLY_UNKEY);
		rpt_mutex_lock(&myrpt->lock);
		myrpt->unkeytocttimer = x;	/* Must be protected as it is changed below */
		rpt_mutex_unlock(&myrpt->lock);

		/* If there's one already queued, don't do another */
		tlist = myrpt->tele.next;
		unkeys_queued = 0;
		if (tlist != &myrpt->tele) {
			rpt_mutex_lock(&myrpt->lock);
			while (tlist != &myrpt->tele) {
				if ((tlist->mode == UNKEY) || (tlist->mode == LOCUNKEY)) {
					unkeys_queued++;
				}
				tlist = tlist->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
		if (unkeys_queued > 1) {
			imdone = 1;
			break;
		}

		/* Wait for the telemetry timer to expire */
		/* Periodically check the timer since it can be re-initialized above */
		while (myrpt->unkeytocttimer) {
			int ctint;
			if (myrpt->unkeytocttimer > 100) {
				ctint = 100;
			} else {
				ctint = myrpt->unkeytocttimer;
			}
			ast_safe_sleep(mychannel, ctint);
			rpt_mutex_lock(&myrpt->lock);
			update_timer(&myrpt->unkeytocttimer, ctint, 0);
			rpt_mutex_unlock(&myrpt->lock);
		}

		/*
		 * Now, the carrier on the rptr rx should be gone. 
		 * If it re-appeared, then forget about sending the CT
		 */
		if (myrpt->keyed) {
			imdone = 1;
			break;
		}

		rpt_mutex_lock(&myrpt->lock);	/* Update the kerchunk counters */
		myrpt->dailykerchunks++;
		myrpt->totalkerchunks++;
		rpt_mutex_unlock(&myrpt->lock);

treataslocal:
		lbuf = ast_str_create(RPT_AST_STR_INIT_SIZE);
		rpt_mutex_lock(&myrpt->lock);
		if (!lbuf) {
			goto abort;
		}
		/* get all the nodes */
		n = __mklinklist(myrpt, NULL, &lbuf, 0) + 1;
		rpt_mutex_unlock(&myrpt->lock);
		strs = ast_malloc(n * sizeof(char *));
		if (!strs) {
			ast_free(lbuf);
			rpt_mutex_lock(&myrpt->lock);
			goto abort;
		}
		/* parse em */
		ns = finddelim(ast_str_buffer(lbuf), strs, n);
		haslink = 0;
		for (i = 0; i < ns; i++) {
			char *cpr = strs[i] + 1;
			if (!strcmp(cpr, myrpt->name))
				continue;
		}
		ast_free(strs);
		ast_free(lbuf);

		if (mytele->mode == LOCUNKEY) {
			/* Local Override Courtesy Tone */
			res = telem_send_ct(myrpt, mychannel, "localct", "LOCUNKEY", 0);
		}
		haslink = 0;
		hastx = 0;
		hasremote = 0;
		if (ao2_container_count(myrpt->links)) {
			rpt_mutex_lock(&myrpt->lock);
			RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
				int v, w;

				if (l->name[0] == '0') {
					continue;
				}
				w = 1;
				if (myrpt->p.nolocallinkct) {
					int nrpts = rpt_num_rpts();
					for (v = 0; v < nrpts; v++) {
						if (&rpt_vars[v] == myrpt) {
							continue;
						} else if (rpt_vars[v].remote) {
							continue;
						} else if (strcmp(rpt_vars[v].name, l->name)) {
							continue;
						}
						w = 0;
						break;
					}
				}
				if (myrpt->p.locallinknodesn) {
					for (v = 0; v < myrpt->p.locallinknodesn; v++) {
						if (strcmp(l->name, myrpt->p.locallinknodes[v])) {
							continue;
						}
						w = 0;
						break;
					}
				}
				if (w) {
					haslink = 1;
				}
				if (l->mode == MODE_TRANSCEIVE) {
					hastx++;
					if (l->isremote) {
						hasremote++;
					}
				}
			}
			ao2_iterator_destroy(&l_it);
			rpt_mutex_unlock(&myrpt->lock);
		}
		if (haslink) {
			myrpt->noduck = 1;
			res = telem_lookup(myrpt, mychannel, (!hastx) ? "remotemon" : "remotetx", mytele->mode == UNKEY ? "UNKEY" : "LOCUNKEY");
			if (res) {
				ast_log(LOG_WARNING, "telem_lookup: remotexx failed on %s\n", ast_channel_name(mychannel));
			}

			/* if in remote cmd mode, indicate it */
			if (myrpt->cmdnode[0] && strcmp(myrpt->cmdnode, "aprstt")) {
				ast_safe_sleep(mychannel, 200);
				res = telem_lookup(myrpt, mychannel, "cmdmode", mytele->mode == UNKEY ? "UNKEY" : "LOCUNKEY");
				if (res) {
					ast_log(LOG_WARNING, "telem_lookup: cmdmode failed on %s\n", ast_channel_name(mychannel));
				}
				ast_stopstream(mychannel);
			}
		} else {
			/* Not Linked Unkey Courtesy Tone */
			res = telem_send_ct(myrpt, mychannel, "unlinkedct", mytele->mode == UNKEY ? "UNKEY" : "LOCUNKEY", 0);
		}
		if (hasremote && ((!myrpt->cmdnode[0]) || (!strcmp(myrpt->cmdnode, "aprstt")))) {
			/* Remote Unkey Courtesy Tone */
			res = telem_send_ct(myrpt, mychannel, "remotect", mytele->mode == UNKEY ? "UNKEY" : "LOCUNKEY", 200);
		}
#if	defined(_MDC_DECODE_H_) && defined(MDC_SAY_WHEN_DOING_CT)
		if (myrpt->lastunit) {
			char mystr[10];

			ast_safe_sleep(mychannel, 200);
			snprintf(mystr, sizeof(mystr), "%04x", myrpt->lastunit);
			myrpt->lastunit = 0;
			ast_say_character_str(mychannel, mystr, NULL, ast_channel_language(mychannel));
			break;
		}
#endif
		imdone = 1;
		break;
	case LINKUNKEY:
		/* if voting and a voter link unkeys but the main or another voter rx is still active */
		if (myrpt->p.votertype == 1 && (myrpt->rxchankeyed || myrpt->voteremrx)) {
			imdone = 1;
			break;
		}
		if (myrpt->patchnoct && (myrpt->callmode != CALLMODE_DOWN)) { /* If no CT during patch configured, then don't send one */
			imdone = 1;
			break;
		}
		if (myrpt->p.locallinknodesn) {
			int v, w;

			w = 0;
			for (v = 0; v < myrpt->p.locallinknodesn; v++) {
				if (strcmp(mytele->mylink.name, myrpt->p.locallinknodes[v])) {
					continue;
				}
				w = 1;
				break;
			}
			if (w) {
				/* If there's one already queued, don't do another */
				tlist = myrpt->tele.next;
				unkeys_queued = 0;
				if (tlist != &myrpt->tele) {
					rpt_mutex_lock(&myrpt->lock);
					while (tlist != &myrpt->tele) {
						if ((tlist->mode == UNKEY) || (tlist->mode == LOCUNKEY)) {
							unkeys_queued++;
						}
						tlist = tlist->next;
					}
					rpt_mutex_unlock(&myrpt->lock);
				}
				if (unkeys_queued > 1) {
					imdone = 1;
					break;
				}

				x = get_wait_interval(myrpt, DLY_UNKEY);
				rpt_mutex_lock(&myrpt->lock);
				myrpt->unkeytocttimer = x;	/* Must be protected as it is changed below */
				rpt_mutex_unlock(&myrpt->lock);

				/* Wait for the telemetry timer to expire */
				/* Periodically check the timer since it can be re-initialized above */
				while (myrpt->unkeytocttimer) {
					int ctint;
					if (myrpt->unkeytocttimer > 100) {
						ctint = 100;
					} else {
						ctint = myrpt->unkeytocttimer;
					}
					ast_safe_sleep(mychannel, ctint);
					rpt_mutex_lock(&myrpt->lock);
					update_timer(&myrpt->unkeytocttimer, ctint, 0);
					rpt_mutex_unlock(&myrpt->lock);
				}
			}
			goto treataslocal;
		}
		if (myrpt->p.nolocallinkct) {	/* if no CT if this guy is on local system */
			int v, w;
			int nrpts = rpt_num_rpts();
			w = 0;
			for (v = 0; v < nrpts; v++) {
				if (&rpt_vars[v] == myrpt) {
					continue;
				} else if (rpt_vars[v].remote) {
					continue;
				} else if (strcmp(rpt_vars[v].name, mytele->mylink.name)) {
					continue;
				}
				w = 1;
				break;
			}
			if (w) {
				imdone = 1;
				break;
			}
		}

		/* Reset the Unkey to CT timer */
		x = get_wait_interval(myrpt, DLY_LINKUNKEY);
		mytele->mylink.linkunkeytocttimer = x;	/* Must be protected as it is changed below */

		/* If there's one already queued, don't do another */
		tlist = myrpt->tele.next;
		unkeys_queued = 0;
		if (tlist != &myrpt->tele) {
			rpt_mutex_lock(&myrpt->lock);
			while (tlist != &myrpt->tele) {
				if (tlist->mode == LINKUNKEY) {
					unkeys_queued++;
				}
				tlist = tlist->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
		if (unkeys_queued > 1) {
			imdone = 1;
			break;
		}

		/* Wait for the telemetry timer to expire */
		/* Periodically check the timer since it can be re-initialized above */
		while (mytele->mylink.linkunkeytocttimer) {
			int ctint;
			if (mytele->mylink.linkunkeytocttimer > 100) {
				ctint = 100;
			} else {
				ctint = mytele->mylink.linkunkeytocttimer;
			}
			ast_safe_sleep(mychannel, ctint);
			rpt_mutex_lock(&myrpt->lock);
			update_timer(&mytele->mylink.linkunkeytocttimer, ctint, 0);
			rpt_mutex_unlock(&myrpt->lock);
		}
		unkeys_queued = 0;
		rpt_mutex_lock(&myrpt->lock);
		RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
			if (!strcmp(l->name, mytele->mylink.name)) {
				unkeys_queued = l->lastrx;
				ao2_ref(l, -1);
				break;
			}
		}
		ao2_iterator_destroy(&l_it);
		rpt_mutex_unlock(&myrpt->lock);
		if (unkeys_queued) {
			imdone = 1;
			break;
		}

		/* Unlinked Courtesy Tone */
		res = telem_send_ct(myrpt, mychannel, "linkunkeyct", "LINKUNKEY", 0);
		imdone = 1;
		break;
	case REMDISC:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		haslink = 0;
		/* dont report if a link for this one still on system */
		if (ao2_container_count(myrpt->links)) {
			rpt_mutex_lock(&myrpt->lock);
			RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
				if (l->name[0] == '0') {
					continue;
				}
				if (!strcmp(l->name, mytele->mylink.name)) {
					haslink = 1;
					ao2_ref(l, -1);
					break;
				}
			}
			ao2_iterator_destroy(&l_it);
			rpt_mutex_unlock(&myrpt->lock);
		}
		if (haslink) {
			imdone = 1;
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,REMDISC", myrpt->name);
		res = saynode(myrpt, mychannel, mytele->mylink.name);
		if (!res) {
			res = ast_streamfile(mychannel, ((mytele->mylink.hasconnected) ? "rpt/remote_disc" : "rpt/remote_busy"), ast_channel_language(mychannel));
		}
		break;
	case REMALREADY:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,REMALREADY", myrpt->name);
		res = ast_streamfile(mychannel, "rpt/remote_already", ast_channel_language(mychannel));
		break;
	case REMNOTFOUND:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,REMNOTFOUND", myrpt->name);
		res = ast_streamfile(mychannel, "rpt/remote_notfound", ast_channel_language(mychannel));
		break;
	case REMGO:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,REMGO", myrpt->name);
		res = ast_streamfile(mychannel, "rpt/remote_go", ast_channel_language(mychannel));
		break;
	case CONNECTED:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,CONNECTED", myrpt->name);
		res = saynode(myrpt, mychannel, mytele->mylink.name);
		if (!res) {
			res = ast_stream_and_wait(mychannel, "rpt/connected-to", "");
		}
		res = saynode(myrpt, mychannel, myrpt->name);
		imdone = 1;
		break;
	case CONNFAIL:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,CONNFAIL", myrpt->name);
		res = saynode(myrpt, mychannel, mytele->mylink.name);
		if (!res) {
			res = ast_streamfile(mychannel, "rpt/connection_failed", ast_channel_language(mychannel));
		}
		break;
	case MEMNOTFOUND:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,MEMNOTFOUND", myrpt->name);
		res = ast_streamfile(mychannel, "rpt/memory_notfound", ast_channel_language(mychannel));
		break;
	case PLAYBACK:
	case LOCALPLAY:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,%s,%s", myrpt->name, mytele->mode == PLAYBACK ? "PLAYBACK" : "LOCALPLAY", mytele->param);
		res = ast_stream_and_wait(mychannel, mytele->param, "");
		imdone = 1;
		break;
	case TOPKEY:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,TOPKEY", myrpt->name);
		for (i = 0; i < TOPKEYN; i++) {
			if (!myrpt->topkey[i].node[0]) {
				continue;
			} else if ((!myrpt->topkeylong) && (myrpt->topkey[i].keyed)) {
				continue;
			}
			res = saynode(myrpt, mychannel, myrpt->topkey[i].node);
			if (!res) {
				res = sayfile(mychannel, (myrpt->topkey[i].keyed) ? "rpt/keyedfor" : "rpt/unkeyedfor");
			}
			if (!res) {
				res = saynum(mychannel, myrpt->topkey[i].timesince);
			}
			if (!res) {
				res = sayfile(mychannel, "rpt/seconds");
			}
			if (!myrpt->topkeylong) {
				break;
			}
		}
		imdone = 1;
		break;
	case SETREMOTE:
		ast_mutex_lock(&myrpt->remlock);
		res = 0;
		myrpt->remsetting = 1;
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT897)) {
			res = set_ft897(myrpt);
		}
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT100)) {
			res = set_ft100(myrpt);
		} else if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT950)) {
			res = set_ft950(myrpt);
		} else if (!strcmp(myrpt->remoterig, REMOTE_RIG_TM271)) {
			setxpmr(myrpt, 0);
			res = set_tm271(myrpt);
		} else if (!strcmp(myrpt->remoterig, REMOTE_RIG_IC706)) {
			res = set_ic706(myrpt);
		} else if (!strcmp(myrpt->remoterig, REMOTE_RIG_XCAT)) {
			res = set_xcat(myrpt);
		} else if (!strcmp(myrpt->remoterig, REMOTE_RIG_RBI) || !strcmp(myrpt->remoterig, REMOTE_RIG_PPP16)) {
#ifdef HAVE_SYS_IO
			if (ioperm(myrpt->p.iobase, 1, 1) == -1) {
				rpt_mutex_unlock(&myrpt->lock);
				ast_log(LOG_WARNING, "Can't get io permission on IO port %x hex\n", myrpt->p.iobase);
				res = -1;
			} else {
				res = setrbi(myrpt);
			}
#else
			ast_log(LOG_ERROR, "IO not supported on this architecture\n");
			res = -1;
#endif
		} else if (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD)) {
			if (myrpt->iofd >= 0) {
				setdtr(myrpt, myrpt->iofd, 1);
			}
			res = setkenwood(myrpt);
			if (myrpt->iofd >= 0) {
				setdtr(myrpt, myrpt->iofd, 0);
			}
			setxpmr(myrpt, 0);
			if (ast_safe_sleep(mychannel, 200) == -1) {
				myrpt->remsetting = 0;
				ast_mutex_unlock(&myrpt->remlock);
				res = -1;
				break;
			}
		} else if (!strcmp(myrpt->remoterig, REMOTE_RIG_TMD700)) {
			res = set_tmd700(myrpt);
			setxpmr(myrpt, 0);
		}
		myrpt->remsetting = 0;
		ast_mutex_unlock(&myrpt->remlock);
		if (!res) {
			if ((!strcmp(myrpt->remoterig, REMOTE_RIG_TM271)) || (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD)))
				telem_lookup(myrpt, mychannel, "functcomplete", "SETREMOTE");
			break;
		}
		/* fall thru to invalid freq */
	case INVFREQ:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,INVFREQ", myrpt->name);
		res = ast_streamfile(mychannel, "rpt/invalid-freq", ast_channel_language(mychannel));
		break;
	case REMMODE:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,REMMODE", myrpt->name);
		switch (myrpt->remmode) {
		case REM_MODE_FM:
			saycharstr(mychannel, "FM");
			break;
		case REM_MODE_USB:
			saycharstr(mychannel, "USB");
			break;
		case REM_MODE_LSB:
			saycharstr(mychannel, "LSB");
			break;
		case REM_MODE_AM:
			saycharstr(mychannel, "AM");
			break;
		}
		if (wait_interval(myrpt, DLY_COMP, mychannel) == -1) {
			break;
		}
		res = telem_lookup(myrpt, mychannel, "functcomplete", "REMMODE");
		break;
	case LOGINREQ:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,LOGINREQ", myrpt->name);
		sayfile(mychannel, "rpt/login");
		saycharstr(mychannel, myrpt->name);
		break;
	case REMLOGIN:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,REMLOGIN", myrpt->name);
		saycharstr(mychannel, myrpt->loginuser);
		saynode(myrpt, mychannel, myrpt->name);
		if (wait_interval(myrpt, DLY_COMP, mychannel) == -1) {
			break;
		}
		res = telem_lookup(myrpt, mychannel, "functcomplete", "REMLOGIN");
		break;
	case REMXXX:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,REMXXX", myrpt->name);
		res = 0;
		switch (mytele->submode.i) {
		case 100:				/* RX PL Off */
			sayfile(mychannel, "rpt/rxpl");
			sayfile(mychannel, "rpt/off");
			break;
		case 101:				/* RX PL On */
			sayfile(mychannel, "rpt/rxpl");
			sayfile(mychannel, "rpt/on");
			break;
		case 102:				/* TX PL Off */
			sayfile(mychannel, "rpt/txpl");
			sayfile(mychannel, "rpt/off");
			break;
		case 103:				/* TX PL On */
			sayfile(mychannel, "rpt/txpl");
			sayfile(mychannel, "rpt/on");
			break;
		case 104:				/* Low Power */
			sayfile(mychannel, "rpt/lopwr");
			break;
		case 105:				/* Medium Power */
			sayfile(mychannel, "rpt/medpwr");
			break;
		case 106:				/* Hi Power */
			sayfile(mychannel, "rpt/hipwr");
			break;
		case 113:				/* Scan down slow */
			sayfile(mychannel, "rpt/down");
			sayfile(mychannel, "rpt/slow");
			break;
		case 114:				/* Scan down quick */
			sayfile(mychannel, "rpt/down");
			sayfile(mychannel, "rpt/quick");
			break;
		case 115:				/* Scan down fast */
			sayfile(mychannel, "rpt/down");
			sayfile(mychannel, "rpt/fast");
			break;
		case 116:				/* Scan up slow */
			sayfile(mychannel, "rpt/up");
			sayfile(mychannel, "rpt/slow");
			break;
		case 117:				/* Scan up quick */
			sayfile(mychannel, "rpt/up");
			sayfile(mychannel, "rpt/quick");
			break;
		case 118:				/* Scan up fast */
			sayfile(mychannel, "rpt/up");
			sayfile(mychannel, "rpt/fast");
			break;
		default:
			res = -1;
		}
		if (strcmp(myrpt->remoterig, REMOTE_RIG_TM271) && strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD)) {
			if (wait_interval(myrpt, DLY_COMP, mychannel) == -1) {
				break;
			}
			if (!res) {
				res = telem_lookup(myrpt, mychannel, "functcomplete", "REMXXX");
			}
		}
		break;
	case SCAN:
		ast_mutex_lock(&myrpt->remlock);
		if (myrpt->hfscanstop) {
			myrpt->hfscanstatus = 0;
			myrpt->hfscanmode = HF_SCAN_OFF;
			myrpt->hfscanstop = 0;
			mytele->mode = SCANSTAT;
			ast_mutex_unlock(&myrpt->remlock);
			if (ast_safe_sleep(mychannel, 1000) == -1) {
				break;
			}
			sayfile(mychannel, "rpt/stop");
			imdone = 1;
			break;
		}
		if (myrpt->hfscanstatus > -2) {
			service_scan(myrpt);
		}
		i = myrpt->hfscanstatus;
		myrpt->hfscanstatus = 0;
		if (i) {
			mytele->mode = SCANSTAT;
		}
		ast_mutex_unlock(&myrpt->remlock);
		if (i < 0) {
			sayfile(mychannel, "rpt/stop");
		} else if (i > 0) {
			saynum(mychannel, i);
		}
		imdone = 1;
		break;
	case TUNE:
		ast_mutex_lock(&myrpt->remlock);
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_IC706)) {
			set_mode_ic706(myrpt, REM_MODE_AM);
			if (play_tone(mychannel, 800, 6000, 8192) == -1) {
				ast_mutex_unlock(&myrpt->remlock);
				break;
			}
			ast_safe_sleep(mychannel, 500);
			set_mode_ic706(myrpt, myrpt->remmode);
			myrpt->tunerequest = 0;
			ast_mutex_unlock(&myrpt->remlock);
			imdone = 1;
			break;
		}
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT100)) {
			set_mode_ft100(myrpt, REM_MODE_AM);
			simple_command_ft100(myrpt, 0x0f, 1);
			if (play_tone(mychannel, 800, 6000, 8192) == -1) {
				ast_mutex_unlock(&myrpt->remlock);
				break;
			}
			simple_command_ft100(myrpt, 0x0f, 0);
			ast_safe_sleep(mychannel, 500);
			set_mode_ft100(myrpt, myrpt->remmode);
			myrpt->tunerequest = 0;
			ast_mutex_unlock(&myrpt->remlock);
			imdone = 1;
			break;
		}
		ast_safe_sleep(mychannel, 500);
		set_mode_ft897(myrpt, REM_MODE_AM);
		ast_safe_sleep(mychannel, 500);
		myrpt->tunetx = 1;
		if (play_tone(mychannel, 800, 6000, 8192) == -1) {
			ast_mutex_unlock(&myrpt->remlock);
			break;
		}
		myrpt->tunetx = 0;
		ast_safe_sleep(mychannel, 500);
		set_mode_ft897(myrpt, myrpt->remmode);
		ast_playtones_stop(mychannel);
		myrpt->tunerequest = 0;
		ast_mutex_unlock(&myrpt->remlock);
		imdone = 1;
		break;
#if 0
		set_mode_ft897(myrpt, REM_MODE_AM);
		simple_command_ft897(myrpt, 8);
		if (play_tone(mychannel, 800, 6000, 8192) == -1) {
			break;
		}
		simple_command_ft897(myrpt, 0x88);
		ast_safe_sleep(mychannel, 500);
		set_mode_ft897(myrpt, myrpt->remmode);
		myrpt->tunerequest = 0;
		ast_mutex_unlock(&myrpt->remlock);
		imdone = 1;
		break;
#endif
	case REMSHORTSTATUS:
	case REMLONGSTATUS:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,%s", myrpt->name, mytele->mode == REMSHORTSTATUS ? "REMSHORTSTATUS" : "REMLONGSTATUS");
		res = saynode(myrpt, mychannel, myrpt->name);
		if (!res) {
			res = sayfile(mychannel, "rpt/frequency");
		}
		if (!res) {
			res = split_freq(mhz, decimals, myrpt->freq);
		}
		if (!multimode_capable(myrpt)) {
			if (decimals[4] == '0') {
				decimals[4] = 0;
				if (decimals[3] == '0') {
					decimals[3] = 0;
				}
			}
			decimals[5] = 0;
		}
		if (!res) {
			m = atoi(mhz);
			if (m < 100) {
				res = saynum(mychannel, m);
			} else {
				res = saycharstr(mychannel, mhz);
			}
		}
		if (!res) {
			res = sayfile(mychannel, "letters/dot");
		}
		if (!res) {
			res = saycharstr(mychannel, decimals);
		}

		if (res) {
			break;
		}
		if (myrpt->remmode == REM_MODE_FM) {	/* Mode FM? */
			switch (myrpt->offset) {
			case REM_MINUS:
				res = sayfile(mychannel, "rpt/minus");
				break;
			case REM_SIMPLEX:
				res = sayfile(mychannel, "rpt/simplex");
				break;
			case REM_PLUS:
				res = sayfile(mychannel, "rpt/plus");
				break;
			default:
				break;
			}
		} else {				/* Must be USB, LSB, or AM */
			switch (myrpt->remmode) {
			case REM_MODE_USB:
				res = saycharstr(mychannel, "USB");
				break;
			case REM_MODE_LSB:
				res = saycharstr(mychannel, "LSB");
				break;
			case REM_MODE_AM:
				res = saycharstr(mychannel, "AM");
				break;
			default:
				break;
			}
		}

		if (res == -1) {
			break;
		}

		if (mytele->mode == REMSHORTSTATUS) { /* Short status? */
			if (wait_interval(myrpt, DLY_COMP, mychannel) == -1) {
				break;
			}
			if (!res) {
				res = telem_lookup(myrpt, mychannel, "functcomplete", "REMSHORTSTATUS");
			}
			break;
		}

		if (strcmp(myrpt->remoterig, REMOTE_RIG_IC706)) {
			switch (myrpt->powerlevel) {
			case REM_LOWPWR:
				res = sayfile(mychannel, "rpt/lopwr");
				break;
			case REM_MEDPWR:
				res = sayfile(mychannel, "rpt/medpwr");
				break;
			case REM_HIPWR:
				res = sayfile(mychannel, "rpt/hipwr");
				break;
			}
		}

		rbimode = ((!strncmp(myrpt->remoterig, REMOTE_RIG_RBI, 3))
				   || (!strncmp(myrpt->remoterig, REMOTE_RIG_FT100, 3))
				   || (!strncmp(myrpt->remoterig, REMOTE_RIG_IC706, 3)));
		if (res || (sayfile(mychannel, "rpt/rxpl") == -1)) {
			break;
		} else if (rbimode && (sayfile(mychannel, "rpt/txpl") == -1)) {
			break;
		} else if ((sayfile(mychannel, "rpt/frequency") == -1) || (saycharstr(mychannel, myrpt->rxpl) == -1)) {
			break;
		} else if ((!rbimode) && ((sayfile(mychannel, "rpt/txpl") == -1) ||
						   (sayfile(mychannel, "rpt/frequency") == -1) || (saycharstr(mychannel, myrpt->txpl) == -1))) {
			break;
		} else if (myrpt->remmode == REM_MODE_FM) {	/* Mode FM? */
			if ((sayfile(mychannel, "rpt/rxpl") == -1) ||
				(sayfile(mychannel, ((myrpt->rxplon) ? "rpt/on" : "rpt/off")) == -1) ||
				(sayfile(mychannel, "rpt/txpl") == -1)
				|| (sayfile(mychannel, ((myrpt->txplon) ? "rpt/on" : "rpt/off")) == -1)) {
				break;
			}
		}
		if (wait_interval(myrpt, DLY_COMP, mychannel) == -1) {
			break;
		}
		if (!res) {
			res = telem_lookup(myrpt, mychannel, "functcomplete", mytele->mode == REMSHORTSTATUS ? "REMSHORTSTATUS" : "REMLONGSTATUS");
		}
		break;
	case STATUS:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		hastx = 0;

		rpt_mutex_lock(&myrpt->lock);
		links_copy = ao2_container_clone(myrpt->links, OBJ_NOLOCK);
		if (!links_copy) {
			goto abort;
		}
		rpt_mutex_unlock(&myrpt->lock);

		res = saynode(myrpt, mychannel, myrpt->name);
		if (myrpt->callmode != CALLMODE_DOWN) {
			hastx = 1;
			res = ast_streamfile(mychannel, "rpt/autopatch_on", ast_channel_language(mychannel));
			if (!res) {
				res = ast_waitstream(mychannel, "");
			} else {
				ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
			}
			ast_stopstream(mychannel);
		}
		RPT_LIST_TRAVERSE(links_copy, l, l_it) {
			char *s;
			if (l->name[0] == '0') {
				continue;
			}
			hastx = 1;
			res = saynode(myrpt, mychannel, l->name);
			s = "rpt/tranceive";
			if (l->mode == MODE_MONITOR) {
				s = "rpt/monitor";
			}
			if (l->mode == MODE_LOCAL_MONITOR) {
				s = "rpt/localmonitor";
			}
			if (!l->thisconnected) {
				s = "rpt/connecting";
			}
			res = ast_stream_and_wait(mychannel, s, "");
		}
		if (!hastx) {
			res = ast_stream_and_wait(mychannel, "rpt/repeat_only", "");
		}
		/* destroy our local link queue */
		ao2_iterator_destroy(&l_it);
		ao2_cleanup(links_copy);
		imdone = 1;
		break;
	case LASTUSER:
		if (myrpt->curdtmfuser[0]) {
			sayphoneticstr(mychannel, myrpt->curdtmfuser);
		}
		if (myrpt->lastdtmfuser[0] && strcmp(myrpt->lastdtmfuser, myrpt->curdtmfuser)) {
			if (myrpt->curdtmfuser[0]) {
				sayfile(mychannel, "and");
			}
			sayphoneticstr(mychannel, myrpt->lastdtmfuser);
		}
		imdone = 1;
		break;
	case FULLSTATUS:
		lbuf = ast_str_create(RPT_AST_STR_INIT_SIZE);
		if (!lbuf) {
			rpt_mutex_lock(&myrpt->lock);
			goto abort;
		}
		rpt_mutex_lock(&myrpt->lock);
		/* get all the nodes */
		n = __mklinklist(myrpt, NULL, &lbuf, 0) + 1;
		rpt_mutex_unlock(&myrpt->lock);
		strs = ast_malloc(n * sizeof(char *));
		if (!strs) {
			ast_free(lbuf);
			rpt_mutex_lock(&myrpt->lock);
			goto abort;
		}
		/* parse em */
		ns = finddelim(ast_str_buffer(lbuf), strs, n);
		/* sort em */
		if (ns)
			qsort((void *) strs, ns, sizeof(char *), mycompar);
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			ast_free(strs);
			ast_free(lbuf);
			break;
		}
		hastx = 0;
		res = saynode(myrpt, mychannel, myrpt->name);
		if (myrpt->callmode != CALLMODE_DOWN) {
			hastx = 1;
			res = ast_stream_and_wait(mychannel, "rpt/autopatch_on", "");
		}
		/* go thru all the nodes in list */
		for (i = 0; i < ns; i++) {
			char *s, mode = 'T';

			/* if a mode spec at first, handle it */
			if ((*strs[i] < '0') || (*strs[i] > '9')) {
				mode = *strs[i];
				strs[i]++;
			}

			hastx = 1;
			res = saynode(myrpt, mychannel, strs[i]);
			s = "rpt/tranceive";
			if (mode == 'R') {
				s = "rpt/monitor";
			}
			if (mode == 'C') {
				s = "rpt/connecting";
			}
			res = ast_stream_and_wait(mychannel, s, "");
		}
		ast_free(strs);
		ast_free(lbuf);
		if (!hastx) {
			res = ast_stream_and_wait(mychannel, "rpt/repeat_only", "");
		}
		imdone = 1;
		break;
	case LASTNODEKEY:			/* Identify last node which keyed us up */
		rpt_mutex_lock(&myrpt->lock);
		if (!ast_strlen_zero(myrpt->lastnodewhichkeyedusup)) {
			p = ast_strdup(myrpt->lastnodewhichkeyedusup);	/* Make a local copy of the node name */
			if (!p) {
				imdone = 1;
				break;
			}
		} else {
			p = NULL;
		}
		rpt_mutex_unlock(&myrpt->lock);
		if (!p) {
			imdone = 1;			/* no node previously keyed us up, or the node which did has been disconnected */
			break;
		}
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			ast_free(p);
			imdone = 1;
			break;
		}
		res = saynode(myrpt, mychannel, p);
		ast_free(p);
		imdone = 1;
		break;
	case UNAUTHTX:				/* Say unauthorized transmit frequency */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		res = ast_stream_and_wait(mychannel, "rpt/unauthtx", "");
		imdone = 1;
		break;
	case PARROT:				/* Repeat stuff */
		snprintf(mystr, sizeof(mystr), PARROTFILE, myrpt->name, mytele->parrot);
		if (ast_fileexists(mystr, NULL, ast_channel_language(mychannel)) <= 0) {
			imdone = 1;
			myrpt->parrotstate = PARROT_STATE_IDLE;
			break;
		}
		if (wait_interval(myrpt, DLY_PARROT, mychannel) == -1) {
			break;
		}
		snprintf(mystr, sizeof(mystr), PARROTFILE, myrpt->name, mytele->parrot);
		res = ast_stream_and_wait(mychannel, mystr, "");
		snprintf(mystr, sizeof(mystr), PARROTFILE, myrpt->name, mytele->parrot);
		strcat(mystr, ".wav");
		unlink(mystr);
		imdone = 1;
		myrpt->parrotstate = PARROT_STATE_IDLE;
		myrpt->parrotonce = 0;
		break;
	case TIMEOUT:
		donodelog_fmt(myrpt, "TELEMETRY,%s,TIMEOUT", myrpt->name);
		myrpt->noduck = 1;
		res = saynode(myrpt, mychannel, myrpt->name);
		if (!res) {
			res = ast_streamfile(mychannel, "rpt/timeout", ast_channel_language(mychannel));
		}
		break;
	case TIMEOUT_WARNING:
		time(&t);
		donodelog_fmt(myrpt, "TELEMETRY,%s,TIMEOUT_WARNING", myrpt->name);
		res = saynode(myrpt, mychannel, myrpt->name);
		if (!res) {
			res = ast_stream_and_wait(mychannel, "rpt/timeout-warning", "");
		}
		if (!res) {				/* Say number of seconds */
			ast_say_number(mychannel, myrpt->p.remotetimeout - (t - myrpt->last_activity_time), "", ast_channel_language(mychannel), (char *) NULL);
		}
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		res = ast_streamfile(mychannel, "queue-seconds", ast_channel_language(mychannel));
		break;
	case ACT_TIMEOUT_WARNING:
		time(&t);
		donodelog_fmt(myrpt, "TELEMETRY,%s,ACT_TIMEOUT_WARNING", myrpt->name);
		res = saynode(myrpt, mychannel, myrpt->name);
		if (!res) {
			res = ast_stream_and_wait(mychannel, "rpt/act-timeout-warning", "");
		}
		if (!res) {				/* Say number of seconds */
			ast_say_number(mychannel, myrpt->p.remoteinacttimeout - (t - myrpt->last_activity_time), "", ast_channel_language(mychannel), (char *) NULL);
		}
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		if (!res) {
			res = ast_stream_and_wait(mychannel, "queue-seconds", "");
		}
		imdone = 1;
		break;
	case STATS_TIME:
	case STATS_TIME_LOCAL:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,%s", myrpt->name, mytele->mode == STATS_TIME ? "STATS_TIME" : "STATS_TIME_LOCAL");
		context = rpt_telem_extension(mychannel, myrpt->p.telemetry, TELEMETRY, TELEM_TIME_EXTN);
		if (context) {
			if (rpt_telem_datastore(mychannel, time(NULL)) < 0) {
				imdone = 1;
				break;
			}
			rpt_do_dialplan(mychannel, TELEM_TIME_EXTN, context);
			pbx = 1;
			break;
		}
		rpt_say_time(mychannel, time(NULL), myrpt->p.timezone);
		imdone = 1;
		break;
	case STATS_VERSION:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,STATS_VERSION", myrpt->name);
		/* Say "version" */
		if (sayfile(mychannel, "rpt/version") == -1) {
			imdone = 1;
			break;
		}
		if (!res) {				/* Say "X" */
			ast_say_number(mychannel, VERSION_MAJOR, "", ast_channel_language(mychannel), (char *) NULL);
		}
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		if (saycharstr(mychannel, ".") == -1) {
			imdone = 1;
			break;
		}
		if (!res)				/* Say "Y" */
			ast_say_number(mychannel, VERSION_MINOR, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res) {
			res = ast_waitstream(mychannel, "");
			ast_stopstream(mychannel);
		} else {
			ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
		}
		imdone = 1;
		break;
	case STATS_GPS:
	case STATS_GPS_LEGACY:
		/* If the app_gps custom function GPS_READ does not exist, let them know */
		if (!ast_custom_function_find("GPS_READ")) {
			if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
				break;
			}
			donodelog_fmt(myrpt, "TELEMETRY,%s,%s", myrpt->name, mytele->mode == STATS_GPS ? "STATS_GPS" : "STATS_GPS_LEGACY");
			saycharstr(mychannel, "GPS");
			sayfile(mychannel, "rpt/off");
			break;
		}
		if (ast_func_read(NULL, "GPS_READ()", gps_data, sizeof(gps_data))) {
			break;
		}

		/* gps_data format monotonic time, epoch, latitude, longitude, elevation */
		if (sscanf(gps_data, N_FMT(llu) " %*u " S_FMT(LAT_SZ) S_FMT(LON_SZ) S_FMT(ELEV_SZ), &u_mono, lat, lon, elev) != 4) {
			break;
		}
		was_mono = (time_t) u_mono;
		t_mono = rpt_time_monotonic();
		if ((was_mono + GPS_VALID_SECS) < t_mono) {
			break;
		}
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		donodelog_fmt(myrpt, "TELEMETRY,%s,%s", myrpt->name, mytele->mode == STATS_GPS ? "STATS_GPS" : "STATS_GPS_LEGACY");
		if (saynode(myrpt, mychannel, myrpt->name) == -1) {
			break;
		}
		if (sayfile(mychannel, "location") == -1) {
			break;
		}
		c = lat[strlen(lat) - 1];
		lat[strlen(lat) - 1] = 0;
		if (sscanf(lat, "%2d" N_FMT(d) "." N_FMT(d), &i, &j, &k) != 3) {
			break;
		}
		res = ast_say_number(mychannel, i, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		if (sayfile(mychannel, "degrees") == -1) {
			break;
		}
		res = ast_say_number(mychannel, j, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		if (saycharstr(mychannel, lat + 4) == -1) {
			break;
		} else if (sayfile(mychannel, "minutes") == -1) {
			break;
		} else if (sayfile(mychannel, (c == 'N') ? "north" : "south") == -1) {
			break;
		} else if (sayfile(mychannel, "rpt/latitude") == -1) {
			break;
		}
		c = lon[strlen(lon) - 1];
		lon[strlen(lon) - 1] = 0;
		if (sscanf(lon, "%3d" N_FMT(d) "." N_FMT(d), &i, &j, &k) != 3) {
			break;
		}
		res = ast_say_number(mychannel, i, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		if (sayfile(mychannel, "degrees") == -1) {
			break;
		}
		res = ast_say_number(mychannel, j, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		if (saycharstr(mychannel, lon + 5) == -1) {
			break;
		}
		if (sayfile(mychannel, "minutes") == -1) {
			break;
		}
		if (sayfile(mychannel, (c == 'E') ? "east" : "west") == -1) {
			break;
		}
		if (sayfile(mychannel, "rpt/longitude") == -1) {
			break;
		}
		if (!elev[0]) {
			break;
		}
		c = elev[strlen(elev) - 1];
		elev[strlen(elev) - 1] = 0;
		if (sscanf(elev, N_FMT(f), &f) != 1) {
			break;
		}
		if (myrpt->p.gpsfeet) {
			if (c == 'M') {
				f *= 3.2808399;
			}
		} else {
			if (c != 'M') {
				f /= 3.2808399;
			}
		}
		snprintf(mystr, sizeof(mystr), "%0.1f", f);
		if (sscanf(mystr, N_FMT(d) "." N_FMT(d), &i, &j) != 2) {
			break;
		}
		res = ast_say_number(mychannel, i, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		if (saycharstr(mychannel, ".") == -1) {
			break;
		}
		res = ast_say_number(mychannel, j, "", ast_channel_language(mychannel), (char *) NULL);
		if (!res) {
			res = ast_waitstream(mychannel, "");
		}
		ast_stopstream(mychannel);
		if (sayfile(mychannel, (myrpt->p.gpsfeet) ? "feet" : "meters") == -1) {
			break;
		}
		if (saycharstr(mychannel, "AMSL") == -1) {
			break;
		}
		ast_stopstream(mychannel);
		imdone = 1;
		break;
	case ARB_ALPHA:
		if (!ast_strlen_zero(mytele->param)) {
			if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
				imdone = 1;
				break;
			}
			donodelog_fmt(myrpt, "TELEMETRY,%s,ARB_ALPHA,%s", myrpt->name, mytele->param);
			saycharstr(mychannel, mytele->param);
		}
		imdone = 1;
		break;
	case REV_PATCH:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) {
			break;
		}
		if (!ast_strlen_zero(mytele->param)) {
			/* Parts of this section taken from app_parkandannounce */
			char *tpl_working, *tpl_current;
			char *tmp[100], *myparm;
			int looptemp = 0, i = 0, dres = 0;

			donodelog_fmt(myrpt, "TELEMETRY,%s,REV_PATCH", myrpt->name);

			tpl_working = ast_strdup(mytele->param);
			myparm = strsep(&tpl_working, ",");
			tpl_current = strsep(&tpl_working, ":");

			while (tpl_current && looptemp < sizeof(tmp)) {
				tmp[looptemp] = tpl_current;
				looptemp++;
				tpl_current = strsep(&tpl_working, ":");
			}

			for (i = 0; i < looptemp; i++) {
				if (!strcmp(tmp[i], "PARKED")) {
					ast_say_digits(mychannel, atoi(myparm), "", ast_channel_language(mychannel));
				} else if (!strcmp(tmp[i], "NODE")) {
					ast_say_digits(mychannel, atoi(myrpt->name), "", ast_channel_language(mychannel));
				} else {
					dres = ast_streamfile(mychannel, tmp[i], ast_channel_language(mychannel));
					if (!dres) {
						dres = ast_waitstream(mychannel, "");
					} else {
						ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n", tmp[i], ast_channel_name(mychannel));
						dres = 0;
					}
				}
			}
			ast_free(tpl_working);
		}
		imdone = 1;
		break;
	case TEST_TONE:
		imdone = 1;
		if (myrpt->stopgen) {
			break;
		}
		myrpt->stopgen = -1;
		if ((res = ast_tonepair_start(mychannel, 1000.0, 0, 99999999, 7200.0))) {
			myrpt->stopgen = 0;
			break;
		}
		while (ast_channel_generatordata(mychannel) && (myrpt->stopgen <= 0)) {
			if (ast_safe_sleep(mychannel, 1)) {
				break;
			}
			imdone = 1;
		}
		myrpt->stopgen = 0;
		if (myrpt->remote && (myrpt->remstopgen < 0)) {
			myrpt->remstopgen = 1;
		}
		break;
	case PFXTONE:
		res = telem_lookup(myrpt, mychannel, "pfxtone", "PFXTONE");
		break;
	default:
		break;
	}
	if (!pbx) {
		if (!imdone) {
			if (!res) {
				res = ast_waitstream(mychannel, "");
			} else {
				ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", ast_channel_name(mychannel));
				res = 0;
			}
		}
		ast_stopstream(mychannel);
		ast_hangup(mychannel);
	}
	rpt_mutex_lock(&myrpt->lock);
	if (mytele->mode == TAILMSG) {
		if (!res && !mytele->killed) { /* The tail message was not squashed (Keyed up while playing tail message)*/
			myrpt->tailmessagen++;
			if (myrpt->tailmessagen >= myrpt->p.tailmessagemax) {
				myrpt->tailmessagen = 0;
			}
		} else { /* The tail message was squashed */
			myrpt->tmsgtimer = myrpt->p.tailsquashedtime;
		}
	}
	telem_done(myrpt, mytele);
	tele_link_remove(myrpt, mytele);
	ast_channel_unref(mychannel);
	rpt_mutex_unlock(&myrpt->lock);
	ast_free(nodename);
	if (id_malloc) {
		ast_free(ident);
	}
	ast_free(mytele);
#ifdef  APP_RPT_LOCK_DEBUG
	{
		struct lockthread *t;

		sleep(5);
		ast_mutex_lock(&locklock);
		t = get_lockthread(pthread_self());
		if (t)
			memset(t, 0, sizeof(struct lockthread));
		ast_mutex_unlock(&locklock);
	}
#endif
	myrpt->noduck = 0;
	pthread_exit(NULL);
abort:
	telem_done(myrpt, mytele);
abort2:
	ast_free(nodename);
abort3:
	tele_link_remove(myrpt, mytele);
	rpt_mutex_unlock(&myrpt->lock);
	if (id_malloc) {
		ast_free(ident);
	}
	ast_free(mytele);
	if (mychannel) {
		ast_hangup(mychannel);
		ast_channel_unref(mychannel);
	}
	pthread_exit(NULL);
}

void rpt_telemetry(struct rpt *myrpt, enum rpt_tele_mode mode, void *data)
{
	struct rpt_tele *tele;
	struct rpt_link *mylink = NULL;
	int res, i, ns, n = 1;
	char *v1, *v2, mystr[1024], *p, haslink;
	char **strs;
	struct rpt_link *l;
	time_t t, t_mono, was_mono;
	unsigned long long u_mono;
	char gps_data[100], lat[25], lon[25], elev[25];
	struct ast_str *lbuf;
	struct ao2_iterator l_it;

	ast_debug(6, "Tracepoint rpt_telemetry() entered mode=%i\n", mode);

	if ((mode == ID) && is_paging(myrpt)) {
		myrpt->deferid = 1;
		return;
	}

	switch (mode) {
	case CONNECTED:
		mylink = (struct rpt_link *) data;
		if ((mylink->name[0] == '3') && (!myrpt->p.eannmode)) {
			return;
		}
		break;
	case REMDISC:
		mylink = (struct rpt_link *) data;
		if ((mylink->name[0] == '3') && (!myrpt->p.eannmode)) {
			return;
		} else if ((!mylink) || (mylink->name[0] == '0')) {
			return;
		} else if (!mylink->gott && !mylink->isremote && !mylink->outbound &&
				   mylink->chan && !CHAN_TECH(mylink->chan, "echolink") && !CHAN_TECH(mylink->chan, "tlb")) {
			return;
		}
		break;
	case VARCMD:
		if (myrpt->telemmode < 2) {
			return;
		}
		break;
	case UNKEY:
	case LOCUNKEY:
		/* if voting and the main rx unkeys but a voter link is still active */
		if (myrpt->p.votertype == 1 && (myrpt->rxchankeyed || myrpt->voteremrx)) {
			return;
		}
		if (myrpt->p.nounkeyct) {
			return;
		}
		/* if any of the following are defined, go ahead and do it,
		   otherwise, dont bother */
		v1 = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->name, "unlinkedct");
		v2 = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->name, "remotect");
		if (telem_lookup(myrpt, NULL, "remotemon", mode == UNKEY ? "UNKEY" : "LOCUNKEY") &&
			telem_lookup(myrpt, NULL, "remotetx", mode == UNKEY ? "UNKEY" : "LOCUNKEY") &&
			telem_lookup(myrpt, NULL, "cmdmode", mode == UNKEY ? "UNKEY" : "LOCUNKEY") &&
			(!(v1 && telem_lookup(myrpt, NULL, v1, mode == UNKEY ? "UNKEY" : "LOCUNKEY"))) &&
			(!(v2 && telem_lookup(myrpt, NULL, v2, mode == UNKEY ? "UNKEY" : "LOCUNKEY")))) {
			return;
		}
		break;
	case LINKUNKEY:
		mylink = (struct rpt_link *) data;
		if (myrpt->p.locallinknodesn) {
			int v, w;

			w = 0;
			for (v = 0; v < myrpt->p.locallinknodesn; v++) {
				if (strcmp(mylink->name, myrpt->p.locallinknodes[v])) {
					continue;
				}
				w = 1;
				break;
			}
			if (w) {
				break;
			}
		}
		if (!ast_variable_retrieve(myrpt->cfg, myrpt->name, "linkunkeyct")) {
			return;
		}
		break;
	default:
		break;
	}
	if (!myrpt->remote) {		/* dont do if we are a remote */
		/* send appropriate commands to everyone on link(s) */
		switch (mode) {
		case REMGO:
			send_tele_link(myrpt, "REMGO");
			return;
		case REMALREADY:
			send_tele_link(myrpt, "REMALREADY");
			return;
		case REMNOTFOUND:
			send_tele_link(myrpt, "REMNOTFOUND");
			return;
		case COMPLETE:
			send_tele_link(myrpt, "COMPLETE");
			return;
		case PROC:
			send_tele_link(myrpt, "PROC");
			return;
		case TERM:
			send_tele_link(myrpt, "TERM");
			return;
		case MACRO_NOTFOUND:
			send_tele_link(myrpt, "MACRO_NOTFOUND");
			return;
		case MACRO_BUSY:
			send_tele_link(myrpt, "MACRO_BUSY");
			return;
		case CONNECTED:
			mylink = (struct rpt_link *) data;
			if ((!mylink) || (mylink->name[0] == '0')) {
				return;
			}
			snprintf(mystr, sizeof(mystr), "CONNECTED,%s,%s", myrpt->name, mylink->name);
			send_tele_link(myrpt, mystr);
			return;
		case CONNFAIL:
			mylink = (struct rpt_link *) data;
			if ((!mylink) || (mylink->name[0] == '0')) {
				return;
			}
			snprintf(mystr, sizeof(mystr), "CONNFAIL,%s", mylink->name);
			send_tele_link(myrpt, mystr);
			return;
		case REMDISC:
			mylink = (struct rpt_link *) data;
			if ((!mylink) || (mylink->name[0] == '0')) {
				return;
			}
			haslink = 0;
			/* dont report if a link for this one still on system */
			if (ao2_container_count(myrpt->links)) {
				rpt_mutex_lock(&myrpt->lock);
				RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
					if (l->name[0] == '0') {
						continue;
					}
					if (!strcmp(l->name, mylink->name)) {
						haslink = 1;
						ao2_ref(l, -1);
						break;
					}
				}
				ao2_iterator_destroy(&l_it);
				rpt_mutex_unlock(&myrpt->lock);
			}
			if (haslink) {
				return;
			}
			snprintf(mystr, sizeof(mystr), "REMDISC,%s", mylink->name);
			send_tele_link(myrpt, mystr);
			return;
		case STATS_TIME:
			t = time(NULL);
			snprintf(mystr, sizeof(mystr), "STATS_TIME,%u", (unsigned int) t);
			send_tele_link(myrpt, mystr);
			return;
		case STATS_VERSION:
			snprintf(mystr, sizeof(mystr), "STATS_VERSION,%d.%d", VERSION_MAJOR, VERSION_MINOR);
			send_tele_link(myrpt, mystr);
			return;
		case STATS_GPS:
			
			/* If the app_gps custom function GPS_READ exists, read the GPS position */
			if (!ast_custom_function_find("GPS_READ")) {
				return;
			}
			if (ast_func_read(NULL, "GPS_READ()", gps_data, sizeof(gps_data))) {
				return;
			}

			/* gps_data format monotonic time, epoch, latitude, longitude, elevation */
			if (sscanf(gps_data, N_FMT(llu) " %*u " S_FMT(LAT_SZ) S_FMT(LON_SZ) S_FMT(ELEV_SZ), &u_mono, lat, lon, elev) != 4) {
				return;
			}

			was_mono = (time_t) u_mono;
			t_mono = rpt_time_monotonic();
			if ((was_mono + GPS_VALID_SECS) < t_mono) {
				return;
			}
			snprintf(mystr, sizeof(mystr), "STATS_GPS,%s,%s,%s,%s", myrpt->name, lat, lon, elev);
			send_tele_link(myrpt, mystr);
			return;
		case ARB_ALPHA:
			snprintf(mystr, sizeof(mystr), "ARB_ALPHA,%s", (char *) data);
			send_tele_link(myrpt, mystr);
			return;
		case REV_PATCH:
			p = (char *) data;
			for (i = 0; p[i]; i++) {
				if (p[i] == ',') {
					p[i] = '^';
				}
			}
			snprintf(mystr, sizeof(mystr), "REV_PATCH,%s,%s", myrpt->name, p);
			send_tele_link(myrpt, mystr);
			return;
		case LASTNODEKEY:
			if (!myrpt->lastnodewhichkeyedusup[0]) {
				return;
			}
			snprintf(mystr, sizeof(mystr), "LASTNODEKEY,%s", myrpt->lastnodewhichkeyedusup);
			send_tele_link(myrpt, mystr);
			return;
		case LASTUSER:
			if ((!myrpt->lastdtmfuser[0]) && (!myrpt->curdtmfuser[0])) {
				return;
			} else if (myrpt->lastdtmfuser[0] && (!myrpt->curdtmfuser[0])) {
				snprintf(mystr, sizeof(mystr), "LASTUSER,%s", myrpt->lastdtmfuser);
			} else if ((!myrpt->lastdtmfuser[0]) && myrpt->curdtmfuser[0]) {
				snprintf(mystr, sizeof(mystr), "LASTUSER,%s", myrpt->curdtmfuser);
			} else {
				if (strcmp(myrpt->curdtmfuser, myrpt->lastdtmfuser)) {
					snprintf(mystr, sizeof(mystr), "LASTUSER,%s,%s", myrpt->curdtmfuser, myrpt->lastdtmfuser);
				} else {
					snprintf(mystr, sizeof(mystr), "LASTUSER,%s", myrpt->curdtmfuser);
				}
			}
			send_tele_link(myrpt, mystr);
			return;
		case STATUS:
			rpt_mutex_lock(&myrpt->lock);
			snprintf(mystr, sizeof(mystr), "STATUS,%s,%d", myrpt->name, myrpt->callmode);
			/* make our own list of links */
			RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
				char s;

				if (l->name[0] == '0') {
					continue;
				}
				s = 'T';
				if (l->mode == MODE_MONITOR) {
					s = 'R';
				}
				if (l->mode == MODE_LOCAL_MONITOR) {
					s = 'L';
				}
				if (!l->thisconnected) {
					s = 'C';
				}
				snprintf(mystr + strlen(mystr), sizeof(mystr), ",%c%s", s, l->name);
			}
			ao2_iterator_destroy(&l_it);
			rpt_mutex_unlock(&myrpt->lock);
			send_tele_link(myrpt, mystr);
			return;
		case FULLSTATUS:
			lbuf = ast_str_create(RPT_AST_STR_INIT_SIZE);
			if (!lbuf) {
				return;
			}

			rpt_mutex_lock(&myrpt->lock);
			snprintf(mystr, sizeof(mystr), "STATUS,%s,%d", myrpt->name, myrpt->callmode);
			/* get all the nodes */
			n = __mklinklist(myrpt, NULL, &lbuf, 0) + 1;
			rpt_mutex_unlock(&myrpt->lock);
			strs = ast_malloc(n * sizeof(char *));
			if (!strs) {
				ast_free(lbuf);
				return;
			}
			/* parse em */
			ns = finddelim(ast_str_buffer(lbuf), strs, n);
			/* sort em */
			if (ns)
				qsort((void *) strs, ns, sizeof(char *), mycompar);
			/* go thru all the nodes in list */
			for (i = 0; i < ns; i++) {
				char s, m = 'T';

				/* if a mode spec at first, handle it */
				if ((*strs[i] < '0') || (*strs[i] > '9')) {
					m = *strs[i];
					strs[i]++;
				}
				s = 'T';
				if (m == 'R') {
					s = 'R';
				}
				if (m == 'C') {
					s = 'C';
				}
				snprintf(mystr + strlen(mystr), sizeof(mystr), ",%c%s", s, strs[i]);
			}
			send_tele_link(myrpt, mystr);
			ast_free(strs);
			ast_free(lbuf);
			return;
		default:
			break;
		}
	}
	tele = ast_calloc(1, sizeof(struct rpt_tele));
	if (!tele) {
		return;
	}
	tele->rpt = myrpt;
	tele->mode = mode;
	if (mode == PARROT) {
		tele->submode.p = data;
		tele->parrot = (unsigned int) tele->submode.i;
		tele->submode.p = 0;
	} else {
		mylink = (struct rpt_link *) (void *) data;
	}
	rpt_mutex_lock(&myrpt->lock);
	if ((mode == CONNFAIL) || (mode == REMDISC) || (mode == CONNECTED) || (mode == LINKUNKEY)) {
		memset(&tele->mylink, 0, sizeof(struct rpt_link));
		if (mylink) {
			memcpy(&tele->mylink, mylink, sizeof(struct rpt_link));
		}
	} else if ((mode == ARB_ALPHA) || (mode == REV_PATCH) ||
			   (mode == PLAYBACK) || (mode == LOCALPLAY) || (mode == VARCMD) || (mode == METER) || (mode == USEROUT)) {
		ast_copy_string(tele->param, (char *) data, TELEPARAMSIZE);
		tele->param[TELEPARAMSIZE - 1] = 0;
	}
	if ((mode == REMXXX) || (mode == PAGE) || (mode == MDC1200)) {
		tele->submode.p = data;
	}
	tele_link_add(myrpt, tele);
	rpt_mutex_unlock(&myrpt->lock);
	res = ast_pthread_create_detached(&tele->threadid, NULL, rpt_tele_thread, (void *) tele);
	if (res < 0) {
		rpt_mutex_lock(&myrpt->lock);
		tele_link_remove(myrpt, tele); /* We don't like stuck transmitters, remove it from the queue */
		rpt_mutex_unlock(&myrpt->lock);
		ast_free(tele);
		ast_log(LOG_WARNING, "Could not create telemetry thread: %s", strerror(res));
	}
	ast_debug(6, "Tracepoint rpt_telemetry() exit\n");
}
