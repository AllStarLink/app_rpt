/*
 * Asterisk -- An open source telephony toolkit.
 * Copyright (C) 1999 - 2005, Digium, Inc.
 * Copyright (C) 2007 - 2010, Jim Dixon
 *
 * Jim Dixon, WB6NIL <jim@lambdatel.com>
 * Based upon work by Mark Spencer <markster@digium.com> and Luigi Rizzo
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
  * at the top of the source tree.
 */

/*! \file
 *
 * \brief Simple Channel driver for Native Audio/GPIO on Beagleboard
 *
 * \author Jim Dixon  <jim@lambdatel.com>
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<defaultenabled>yes</defaultenabled>
 ***/

#include "asterisk.h"

#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include <alsa/asoundlib.h>

/* These three defines are merely to get this module to build */
/* REMOVE when they are resolved */
#define SND_PCCV_FORMAT_S16_LE		SND_PCM_FORMAT_S16_LE
#define SND_PCCV_STREAM_CAPTURE		SND_PCM_STREAM_CAPTURE
#define SND_PCCV_STREAM_PLAYBACK	SND_PCM_STREAM_PLAYBACK

#define DEBUG_CAPTURES	 		1

#define RX_CAP_RAW_FILE			"/tmp/rx_cap_in.pcm"
#define TX_CAP_RAW_FILE			"/tmp/tx_cap_in.pcm"

#define	DELIMCHR ','
#define	QUOTECHR 34

#define	READERR_THRESHOLD 50

#include "asterisk/lock.h"
#include "asterisk/frame.h"
#include "asterisk/logger.h"
#include "asterisk/callerid.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/endian.h"
#include "asterisk/stringfields.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/musiconhold.h"
#include "asterisk/dsp.h"
#include "asterisk/format_cache.h"

#define	DESIRED_RATE 8000

#define	NTAPS 31
#define	NTAPS_PL 6

/*! Global jitterbuffer configuration - by default, jb is disabled */
static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = -1,
	.resync_threshold = -1,
	.impl = "",
};

static struct ast_jb_conf global_jbconf;

/*
 * The following parameters are used in the driver:
 *
 *  FRAME_SIZE	the size of an audio frame, in samples.
 *		160 is used almost universally, so you should not change it.
 *
 */

#define FRAME_SIZE	160
#define PERIOD_FRAMES 80

#define	GPIO_MASK 0x70ff0
#define	GPIO_INPUTS 0x703f0
#define	MASK_GPIOS_COR0 0x200
#define	MASK_GPIOS_CTCSS0 0x100
#define	MASK_GPIOS_PTT0 0x800
#define	MASK_GPIOS_COR1 0x80
#define	MASK_GPIOS_CTCSS1 0x40
#define	MASK_GPIOS_PTT1 0x400
#define	MASK_GPIOS_GPIO1 0x20
#define	MASK_GPIOS_GPIO2 0x10
#define	MASK_GPIOS_GPIO3 0x10000
#define	MASK_GPIOS_GPIO4 0x20000
#define	MASK_GPIOS_GPIO5 0x40000
#define	MASK_GPIOS_GPIOS 0x70030

#define	PLAYBACK_MAX 64

#if defined(__FreeBSD__)
#define	FRAGS	0x8
#else
#define	FRAGS	( ( (5 * 6) << 16 ) | 0xa )
#endif

/*
 * XXX text message sizes are probably 256 chars, but i am
 * not sure if there is a suitable definition anywhere.
 */
#define TEXT_SIZE	256

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

struct
{
	snd_pcm_t *icard, *ocard;
} alsa;

int readdev = -1;
int writedev = -1;
int w_errors = 0;				/* overfull in the write path */
int ndevices = 0;
int readpos;				/* read position above */
char beagle_read_buf[FRAME_SIZE * 4];
char beagle_write_buf[FRAME_SIZE * 4];
int beagle_write_dst;
int total_blocks;			/* total blocks in the output device */
static volatile unsigned long *gpio = NULL;
int hwfd = -1;
int has_been_open = 0;

int32_t	gpio_val = 0;		/* current value of gpios */
int32_t	gpio_ctl = 0;		/* mask of output ones */
int32_t	gpios_mask[] = {0,MASK_GPIOS_GPIO1,MASK_GPIOS_GPIO2,MASK_GPIOS_GPIO3,
	MASK_GPIOS_GPIO4,MASK_GPIOS_GPIO5} ;
int32_t gpio_lastmask = 0;
int32_t	gpio_pulsemask = 0;
int	gpio_pulsetimer[] = {0,0,0,0,0,0};
struct timeval gpio_then;
AST_MUTEX_DEFINE_STATIC(gpio_lock);

#if __BYTE_ORDER == __LITTLE_ENDIAN
static snd_pcm_format_t format = SND_PCCV_FORMAT_S16_LE;
#else
static snd_pcm_format_t format = SND_PCCV_FORMAT_S16_BE;
#endif

static char *config = "beagle.conf";	/* default config file */
#define CONFIG_TUNE_FMT "beagle_tune_%s.conf"    /* tune config file */

static FILE *frxcapraw = NULL;
static FILE *ftxcapraw = NULL;

static int beagle_debug;

enum {CD_IGNORE,CD_HID,CD_HID_INVERT};
enum {SD_IGNORE,SD_HID,SD_HID_INVERT};    				 // no,external,externalinvert,software

/*	DECLARE STRUCTURES */

/*
 * descriptor for one of our channels.
 * There is one used for 'default' values (from the [general] entry in
 * the configuration file), and then one instance for each device
 * (the default is cloned from [general], others are only created
 * if the relevant section exists).
 */
struct chan_beagle_pvt {
	char *name;

	short cdMethod;
	int hookstate;
	int usedtmf;

	int overridecontext;

	struct ast_channel *owner;
	char ext[AST_MAX_EXTENSION];
	char ctx[AST_MAX_CONTEXT];
	char language[MAX_LANGUAGE];
	char cid_name[256];			/*XXX */
	char cid_num[256];			/*XXX */
	char mohinterpret[MAX_MUSICCLASS];

	/* buffers used in beagle_read - AST_FRIENDLY_OFFSET space for headers
	 * plus enough room for a full frame
	 */
	char beagle_read_frame_buf[FRAME_SIZE * 2 + AST_FRIENDLY_OFFSET];
	AST_LIST_HEAD_NOLOCK(, ast_frame) txq;
	ast_mutex_t  txqlock;
	struct ast_frame read_f;	/* returned by beagle_read */

	char 	debuglevel;
	char 	radioduplex;			//

	int  	tracetype;
	int     tracelevel;

	char lastrx;
	char rxhidsq;
	char rxhidctcss;

	char rxkeyed;	  			// indicates rx signal present

	char lasttx;
	char txkeyed;				// tx key request from upper layers
	char txtestkey;

	struct ast_dsp *dsp;

	float	hpx[NTAPS_PL + 1];
	float	hpy[NTAPS_PL + 1];

	int32_t	destate;

	char    rxcpusaver;
	char    txcpusaver;

	char 	rxcdtype;
	char 	rxsdtype;

	char	invertptt;

	int	rxondelay;
	int	rxoncnt;

	int	rxmixerset;
	float	rxvoiceadj;
	int 	txmixerset;

	int measure_enabled;

	int32_t discfactor;
	int32_t discounterl;
	int32_t discounteru;
	int16_t amax;
	int16_t amin;
	int16_t apeak;
	int plfilter;
	int deemphasis;
	char *gpios[32];
	int32_t	last_gpios_in;
	char had_gpios_in;
	struct timeval starttime;
};

static struct chan_beagle_pvt beagle_default = {
	.ext = "s",
	.ctx = "default",
	.usedtmf = 1,
	.rxondelay = 0,
};

/*	DECLARE FUNCTION PROTOTYPES	*/

static int mixer_write(void);
static void tune_write(struct chan_beagle_pvt *o);

static char *beagle_active = NULL;	 /* the active device */

static struct ast_channel *beagle_request(const char *type, struct ast_format_cap *cap,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static int beagle_digit_begin(struct ast_channel *c, char digit);
static int beagle_digit_end(struct ast_channel *c, char digit, unsigned int duration);
static int beagle_text(struct ast_channel *c, const char *text);
static int beagle_hangup(struct ast_channel *c);
static int beagle_answer(struct ast_channel *c);
static struct ast_frame *beagle_read(struct ast_channel *chan);
static int beagle_call(struct ast_channel *c, const char *dest, int timeout);
static int beagle_write(struct ast_channel *chan, struct ast_frame *f);
static int beagle_indicate(struct ast_channel *chan, int cond, const void *data, size_t datalen);
static int beagle_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int beagle_setoption(struct ast_channel *chan, int option, void *data, int datalen);

static char tdesc[] = "Beagleboard Radio Channel Driver";

struct chan_beagle_pvt pvts[2];

static struct ast_channel_tech beagle_tech = {
	.type = "Beagle",
	.description = tdesc,
	.requester = beagle_request,
	.send_digit_begin = beagle_digit_begin,
	.send_digit_end = beagle_digit_end,
	.send_text = beagle_text,
	.hangup = beagle_hangup,
	.answer = beagle_answer,
	.read = beagle_read,
	.call = beagle_call,
	.write = beagle_write,
	.indicate = beagle_indicate,
	.fixup = beagle_fixup,
	.setoption = beagle_setoption,
};

static unsigned long get_gpios(void)
{
	return(gpio[0x6038/4] & GPIO_INPUTS);
}

static void set_ptt(int n,int val)
{
	unsigned int mask = MASK_GPIOS_PTT0,c;

	if (n) mask = MASK_GPIOS_PTT1;
	c = gpio[0x603c/4];
	c &= ~mask;
	if (!val) c |= mask;
	gpio[0x603c/4] = c;
	return;
}

static void set_gpios(void)
{
	unsigned int c;

	c = gpio[0x603c/4];
	c &= ~MASK_GPIOS_GPIOS;
	c |= gpio_val;
	gpio[0x603c/4] = c;
	return;
}

/* IIR 6 pole High pass filter, 300 Hz corner with 0.5 db ripple */

#define GAIN1   1.745882764e+00

static int16_t hpass6(int16_t input,float *xv,float *yv)
{
	xv[0] = xv[1]; xv[1] = xv[2]; xv[2] = xv[3]; xv[3] = xv[4]; xv[4] = xv[5]; xv[5] = xv[6];
	xv[6] = ((float)input) / GAIN1;
	yv[0] = yv[1]; yv[1] = yv[2]; yv[2] = yv[3]; yv[3] = yv[4]; yv[4] = yv[5]; yv[5] = yv[6];
	yv[6] =   (xv[0] + xv[6]) - 6 * (xv[1] + xv[5]) + 15 * (xv[2] + xv[4])
				 - 20 * xv[3]
				 + ( -0.3491861578 * yv[0]) + (  2.3932556573 * yv[1])
				 + ( -6.9905126572 * yv[2]) + ( 11.0685981760 * yv[3])
				 + ( -9.9896695552 * yv[4]) + (  4.8664511065 * yv[5]);
	return((int)yv[6]);
}


/* Perform standard 6db/octave de-emphasis */
static int16_t deemph(int16_t input,int32_t *state)
{
	int16_t coeff00 = 6878;
	int16_t coeff01 = 25889;
	int32_t accum; /* 32 bit accumulator */

	accum = input;
	/* YES! The parenthesis REALLY do help on this one! */
	*state = accum + ((*state * coeff01) >> 15);
	accum = (*state * coeff00);
	/* adjust gain so that we have unity @ 1KHz */
	return((accum >> 14) + (accum >> 15));
}


/* IIR 3 pole High pass filter, 300 Hz corner with 0.5 db ripple */

static int16_t hpass(int16_t input,float *xv,float *yv)
{
#define GAIN   1.280673652e+00

	xv[0] = xv[1]; xv[1] = xv[2]; xv[2] = xv[3];
	xv[3] = ((float)input) / GAIN;
	yv[0] = yv[1]; yv[1] = yv[2]; yv[2] = yv[3];
	yv[3] =   (xv[3] - xv[0]) + 3 * (xv[1] - xv[2])
				 + (  0.5999763543 * yv[0]) + ( -2.1305919790 * yv[1])
				 + (  2.5161440793 * yv[2]);
	return((int)yv[3]);
}

static struct chan_beagle_pvt *find_pvt(const char *str)
{
	int	i;

	if (str == NULL) {
		return NULL;
	}

	for(i = 0; i < 2; i++) {
		if (!pvts[i].name) {
			continue;
		}
		if (!strcasecmp(pvts[i].name,str)) {
			return (&pvts[i]);
		}
	}
	return NULL;
}

/* Call with:  devnum: alsa major device number, param: ascii Formal
Parameter Name, val1, first or only value, val2 second value, or 0
if only 1 value. Values: 0-99 (percent) or 0-1 for baboon.

Note: must add -lasound to end of linkage */

static int setamixer(int devnum,char *param, int v1, int v2)
{
	int	type;
	char	str[100];
	snd_hctl_t *hctl;
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_value_t *control;
	snd_hctl_elem_t *elem;
	snd_ctl_elem_info_t *info;

	sprintf(str,"hw:%d",devnum);
	if (snd_hctl_open(&hctl, str, 0)) return(-1);
	snd_hctl_load(hctl);
	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, param);
	elem = snd_hctl_find_elem(hctl, id);
	if (!elem)
	{
		snd_hctl_close(hctl);
		fprintf(stderr,"Cannot find mixer element '%s'\n",param);
		return(-1);
	}
	snd_ctl_elem_info_alloca(&info);
	snd_hctl_elem_info(elem,info);
	type = snd_ctl_elem_info_get_type(info);
	snd_ctl_elem_value_alloca(&control);
	snd_ctl_elem_value_set_id(control, id);
	switch(type)
	{
	    case SND_CTL_ELEM_TYPE_INTEGER:
	    case SND_CTL_ELEM_TYPE_ENUMERATED:
		snd_ctl_elem_value_set_integer(control, 0, v1);
		if (v2 >= 0) snd_ctl_elem_value_set_integer(control, 1, v2);
		break;
	    case SND_CTL_ELEM_TYPE_BOOLEAN:
		snd_ctl_elem_value_set_integer(control, 0, (v1 != 0));
		if (v2 >= 0) snd_ctl_elem_value_set_integer(control, 1, (v2 != 0));
		break;
	}
	if (snd_hctl_elem_write(elem, control))
	{
		snd_hctl_close(hctl);
		fprintf(stderr,"Cannot set value for mixer element '%s'\n",param);
		return(-1);
	}
	snd_hctl_close(hctl);
	return(0);
}

/*
*/

/* Write an exactly FRAME_SIZE sized frame */
static int soundcard_writeframe(short *data)
{
		snd_pcm_state_t state;

#if DEBUG_CAPTURES == 1
	if (ftxcapraw)
		fwrite(((void *) data),1,FRAME_SIZE * 2 * 2,ftxcapraw);
#endif
	state = snd_pcm_state(alsa.ocard);
	if (state == SND_PCM_STATE_XRUN)
		snd_pcm_prepare(alsa.ocard);
	return(snd_pcm_writei(alsa.ocard, (void *)data, FRAME_SIZE));
}

static snd_pcm_t *alsa_card_init(char *dev, snd_pcm_stream_t stream)
{
	int err;
	snd_pcm_t *handle = NULL;
	snd_pcm_hw_params_t *hwparams = NULL;
	snd_pcm_sw_params_t *swparams = NULL;
	snd_pcm_uframes_t chunk_size = 0;
	unsigned period_time = 0;
	unsigned buffer_time = 80000;
	snd_pcm_uframes_t period_frames = 0;
	snd_pcm_uframes_t buffer_frames = 0;
	struct pollfd pfd;
	/* int period_bytes = 0; */
	snd_pcm_uframes_t buffer_size = 0;

	unsigned int rate = DESIRED_RATE;
	snd_pcm_uframes_t start_threshold, stop_threshold;

	err = snd_pcm_open(&handle, dev, stream, SND_PCM_NONBLOCK );
	if (err < 0) {
		ast_log(LOG_ERROR, "snd_pcm_open failed: %s\n", snd_strerror(err));
		return NULL;
	} else
		ast_log(LOG_DEBUG, "Opening device %s in %s mode\n", dev, (stream == SND_PCCV_STREAM_CAPTURE) ? "read" : "write");

	hwparams = alloca(snd_pcm_hw_params_sizeof());
	memset(hwparams, 0, snd_pcm_hw_params_sizeof());
	snd_pcm_hw_params_any(handle, hwparams);

	err = snd_pcm_hw_params_set_access(handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_access failed: %s\n", snd_strerror(err));
		return NULL;
	}
	err = snd_pcm_hw_params_set_format(handle, hwparams, format);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_format failed: %s\n", snd_strerror(err));
		return NULL;
	}

	err = snd_pcm_hw_params_set_channels(handle, hwparams, 2);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_channels failed: %s\n", snd_strerror(err));
		return NULL;
	}
	err = snd_pcm_hw_params_set_rate_near(handle, hwparams, &rate, 0);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_rate failed: %s\n", snd_strerror(err));
		return NULL;
	}

	if (buffer_time == 0 && buffer_frames == 0) {
		err = snd_pcm_hw_params_get_buffer_time_max(hwparams,
							    &buffer_time, 0);
		assert(err >= 0);
		if (buffer_time > 500000)
			buffer_time = 500000;
	}
	if (period_time == 0 && period_frames == 0) {
		if (buffer_time > 0)
			period_time = buffer_time / 4;
		else
			period_frames = buffer_frames / 4;
	}
	if (period_time > 0)
		err = snd_pcm_hw_params_set_period_time_near(handle, hwparams,
							     &period_time, 0);
	else
		err = snd_pcm_hw_params_set_period_size_near(handle, hwparams,
							     &period_frames, 0);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_period time/size failed: %s\n", snd_strerror(err));
		return NULL;
	}

	if (buffer_time > 0) {
		err = snd_pcm_hw_params_set_buffer_time_near(handle, hwparams,
							     &buffer_time, 0);
	} else {
		err = snd_pcm_hw_params_set_buffer_size_near(handle, hwparams,
							     &buffer_frames);
	}

	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_buffer time/size failed: %s\n", snd_strerror(err));
		return NULL;
	}

	err = snd_pcm_hw_params(handle, hwparams);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_hw_params failed: %s\n", snd_strerror(err));
		return NULL;
	}

	snd_pcm_hw_params_get_period_size(hwparams, &chunk_size, 0);
	snd_pcm_hw_params_get_buffer_size(hwparams, &buffer_size);
	if (chunk_size == buffer_size) {
		ast_log(LOG_ERROR,"Can't use period equal to buffer size (%lu == %lu)\n",
		      chunk_size, buffer_size);
		return NULL;
	}

	swparams = alloca(snd_pcm_sw_params_sizeof());
	memset(swparams, 0, snd_pcm_sw_params_sizeof());
	snd_pcm_sw_params_current(handle, swparams);

	err = snd_pcm_sw_params_set_avail_min(handle, swparams, chunk_size);

	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_avail_min failed: %s\n", snd_strerror(err));
		return NULL;
	}

	if (stream == SND_PCCV_STREAM_PLAYBACK)
		start_threshold = buffer_size;
	else
		start_threshold = 1;

	err = snd_pcm_sw_params_set_start_threshold(handle, swparams, start_threshold);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_start_threshold failed: %s\n", snd_strerror(err));
		return NULL;
	}

	stop_threshold = buffer_size;
	err = snd_pcm_sw_params_set_stop_threshold(handle, swparams, stop_threshold);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_stopt_threshold failed: %s\n", snd_strerror(err));
		return NULL;
	}

	err = snd_pcm_sw_params(handle, swparams);
	if (err < 0)
	{
		ast_log(LOG_ERROR, "set_sw_params failed: %s\n", snd_strerror(err));
		return NULL;
	}

	err = snd_pcm_poll_descriptors_count(handle);
	if (err <= 0)
	{
		ast_log(LOG_ERROR, "Unable to get a poll descriptors count, error is %s\n", snd_strerror(err));
		return NULL;
	}
	if (err != 1)
	{
		ast_log(LOG_ERROR, "Can't handle more than one device\n");
		return NULL;
	}
	snd_pcm_poll_descriptors(handle, &pfd, err);
	ast_log(LOG_DEBUG, "Acquired fd %d from the poll descriptor\n", pfd.fd);

	if (stream == SND_PCCV_STREAM_CAPTURE)
		readdev = pfd.fd;
	else
		writedev = pfd.fd;

	return handle;
}

/*
 * some of the standard methods supported by channels.
 */
static int beagle_digit_begin(struct ast_channel *c, char digit)
{
	return 0;
}

static int beagle_digit_end(struct ast_channel *c, char digit, unsigned int duration)
{
	/* no better use for received digits than print them */
	ast_verbose(" << Chan_beagle Received digit %c of duration %u ms >> \n",
		digit, duration);
	return 0;
}

static int beagle_text(struct ast_channel *c, const char *text)
{
	struct chan_beagle_pvt *o = ast_channel_tech_pvt(c);
	char *cmd;

	cmd = alloca(strlen(text) + 10);

	/* print received messages */
	if(o->debuglevel)ast_verbose(" << Chan_beagle Received beagle text %s >> \n", text);

	if (!strncasecmp(text,"GPIO",4))
	{
		int i,j,cnt;

		cnt = sscanf(text,"%s %d %d",cmd,&i,&j);
		if (cnt < 3) return 0;
		if ((i < 1) || (i > 5)) return 0;
		ast_mutex_lock(&gpio_lock);
		if (j > 1) /* if to request pulse-age */
		{
			gpio_pulsetimer[i] = j - 1;
		}
		else
		{
			/* clear pulsetimer, if in the middle of running */
			gpio_pulsetimer[i] = 0;
			gpio_val &= ~gpios_mask[i];
			if (j) gpio_val |= gpios_mask[i];
			set_gpios();
		}
		ast_mutex_unlock(&gpio_lock);
		return 0;
	}
	return 0;
}

/*
 * handler for incoming calls. Either autoanswer, or start ringing
 */
static int beagle_call(struct ast_channel *c, const char *dest, int timeout)
{
	ast_setstate(c, AST_STATE_UP);
	return 0;
}

/*
 * remote side answered the phone
 */
static int beagle_answer(struct ast_channel *c)
{
	ast_setstate(c, AST_STATE_UP);
	return 0;
}

static int beagle_hangup(struct ast_channel *c)
{
	struct chan_beagle_pvt *o = ast_channel_tech_pvt(c);
	int chindex = 0;

	if (o == &pvts[1]) chindex = 1;

	ast_channel_tech_pvt_set(c, NULL);
	o->owner = NULL;
	ast_module_unref(ast_module_info->self);
	if (o->hookstate) o->hookstate = 0;
	if (o->invertptt) set_ptt(chindex,1); else set_ptt(chindex,0);
	return 0;
}

/* used for data coming from the network */
static int beagle_write(struct ast_channel *c, struct ast_frame *f)
{
	struct chan_beagle_pvt *o = ast_channel_tech_pvt(c);
	struct ast_frame *f1;

	/*
	 * we could receive a block which is not a multiple of our
	 * FRAME_SIZE, so buffer it locally and write to the device
	 * in FRAME_SIZE chunks.
	 * Keep the residue stored for future use.
	 */

	if (!o->txkeyed) return 0;

	f1 = ast_frdup(f);
	memset(&f1->frame_list,0,sizeof(f1->frame_list));
	ast_mutex_lock(&o->txqlock);
	AST_LIST_INSERT_TAIL(&o->txq,f1,frame_list);
	ast_mutex_unlock(&o->txqlock);

	return 0;
}

static struct ast_frame *beagle_read(struct ast_channel *c)
{
	int res,cd,sd,i,j,k,m,n,n0,n1,ismaster,chindex;
	unsigned int gv;
	struct chan_beagle_pvt *o = ast_channel_tech_pvt(c);
	struct ast_frame *f = &o->read_f,*f1,*f0;
	struct ast_frame wf = { AST_FRAME_CONTROL };
	short *sp,*sp0,*sp1,*sp2;
		snd_pcm_state_t state;
	struct timeval tv,now;

	ismaster = 0;
	for(chindex = 0; chindex < 2; chindex++)
	{
		if (o == &pvts[chindex]) break;
	}
	if ((chindex == 0) ||
		((chindex == 1) && (pvts[0].owner == NULL))) ismaster = 1;

	/* XXX can be simplified returning &ast_null_frame */
	/* prepare a NULL frame in case we don't have enough data to return */
	bzero(f, sizeof(struct ast_frame));
	f->frametype = AST_FRAME_NULL;
	f->src = beagle_tech.type;

	if (!ismaster) return f;

	state = snd_pcm_state(alsa.icard);
	if ((state != SND_PCM_STATE_PREPARED) && (state != SND_PCM_STATE_RUNNING)) {
		snd_pcm_prepare(alsa.icard);
	}

	gettimeofday(&tv,NULL);

	res = snd_pcm_readi(alsa.icard, beagle_read_buf,FRAME_SIZE);
	if (res < 0)				/* audio data not ready, return a NULL frame */
	{
				return f;
		}
#if DEBUG_CAPTURES == 1
	if (frxcapraw) fwrite(beagle_read_buf,1,res,frxcapraw);
#endif
	for(;;)
	{
		n0 = 0;
		ast_mutex_lock(&pvts[0].txqlock);
		AST_LIST_TRAVERSE(&pvts[0].txq, f1,frame_list) n0++;
		ast_mutex_unlock(&pvts[0].txqlock);
		n1 = 0;
		ast_mutex_lock(&pvts[1].txqlock);
		AST_LIST_TRAVERSE(&pvts[1].txq, f1,frame_list) n1++;
		ast_mutex_unlock(&pvts[1].txqlock);
		n = MAX(n0,n1);
		if (n && ((n > 3) || (!o->txkeyed)))
		{
			f0 = f1 = NULL;
			if (n0)
			{
				ast_mutex_lock(&pvts[0].txqlock);
				f0 = AST_LIST_REMOVE_HEAD(&pvts[0].txq,frame_list);
				ast_mutex_unlock(&pvts[0].txqlock);
				n = f0->datalen;
			}
			if (n1)
			{
				ast_mutex_lock(&pvts[1].txqlock);
				f1 = AST_LIST_REMOVE_HEAD(&pvts[1].txq,frame_list);
				ast_mutex_unlock(&pvts[1].txqlock);
				n = f1->datalen;
			}
			if (n0 && n1) n = MIN(f0->datalen,f1->datalen);
			if (n > (FRAME_SIZE * 2)) n = FRAME_SIZE * 2;
			sp = (short *) (beagle_write_buf);
			sp0 = sp1 = NULL;
			if (f0) sp0 = (short *) f0->data.ptr;
			if (f1) sp1 = (short *) f1->data.ptr;
			for(i = 0; i < (n / 2); i++)
			{
				if (f1) *sp++ = *sp1++;
				else *sp++ = 0;
				if (f0) *sp++ = *sp0++;
				else *sp++ = 0;
			}
			soundcard_writeframe((short *)beagle_write_buf);
			if (f0) ast_frfree(f0);
			if (f1) ast_frfree(f1);
			continue;
		}
		break;
	}
	gv = get_gpios();
	pvts[0].rxhidsq = 0;
	if (gv & MASK_GPIOS_COR0) pvts[0].rxhidsq = 1;
	pvts[0].rxhidctcss = 0;
	if (gv & MASK_GPIOS_CTCSS0) pvts[0].rxhidctcss = 1;
	pvts[1].rxhidsq = 0;
	if (gv & MASK_GPIOS_COR1) pvts[1].rxhidsq = 1;
	pvts[1].rxhidctcss = 0;
	if (gv & MASK_GPIOS_CTCSS1) pvts[1].rxhidctcss = 1;
	ast_mutex_lock(&gpio_lock);
	now = ast_tvnow();
	j = ast_tvdiff_ms(now,gpio_then);
	gpio_then = now;
	/* make output inversion mask (for pulseage) */
	gpio_lastmask = gpio_pulsemask;
	gpio_pulsemask = 0;
	for(i = 1; i <= 5; i++)
	{
		k = gpio_pulsetimer[i];
		if (k)
		{
			k -= j;
			if (k < 0) k = 0;
			gpio_pulsetimer[i] = k;
		}
		if (k) gpio_pulsemask |= gpios_mask[i];
	}
	if (gpio_pulsemask != gpio_lastmask) /* if anything inverted (temporarily) */
	{
		gpio_val ^= gpio_lastmask ^ gpio_pulsemask;
		set_gpios();
	}
	ast_mutex_unlock(&gpio_lock);

	sp = (short *)beagle_read_buf;
	sp1 = (short *)(pvts[1].beagle_read_frame_buf + AST_FRIENDLY_OFFSET);
	sp2 = (short *)(pvts[0].beagle_read_frame_buf + AST_FRIENDLY_OFFSET);
	for(n = 0; n < FRAME_SIZE; n++)
	{
		if (pvts[1].plfilter && pvts[1].deemphasis)
			*sp1++ = hpass6(deemph(*sp++,&pvts[1].destate),pvts[1].hpx,pvts[1].hpy);
		else if (pvts[1].deemphasis)
			*sp1++ = deemph(*sp++,&pvts[1].destate);
		else if (pvts[1].plfilter)
			*sp1++ = hpass(*sp++,pvts[1].hpx,pvts[1].hpy);
		else
			*sp1++ = *sp++;
		if (pvts[0].plfilter && pvts[0].deemphasis)
			*sp2++ = hpass6(deemph(*sp++,&pvts[0].destate),pvts[0].hpx,pvts[0].hpy);
		else if (pvts[0].deemphasis)
			*sp2++ = deemph(*sp++,&pvts[0].destate);
		else if (pvts[0].plfilter)
			*sp2++ = hpass(*sp++,pvts[0].hpx,pvts[0].hpy);
		else
			*sp2++ = *sp++;
	}
		readpos = 0;		       /* reset read pointer for next frame */
	for(m = 0; m < 2; m++)
	{
		o = &pvts[m];

		if (!o->owner) continue;

		cd = 1; /* assume CD */
		if ((o->rxcdtype == CD_HID) && (!o->rxhidsq)) cd = 0;
		else if ((o->rxcdtype == CD_HID_INVERT) && o->rxhidsq) cd = 0;

		/* apply cd turn-on delay, if one specified */
		if (o->rxondelay && cd && (o->rxoncnt++ < o->rxondelay)) cd = 0;
		else if (!cd) o->rxoncnt = 0;

		sd = 1; /* assume SD */
		if ((o->rxsdtype == SD_HID) && (!o->rxhidctcss)) sd = 0;
		else if ((o->rxsdtype == SD_HID_INVERT) && o->rxhidctcss) sd = 0;

		o->rxkeyed = sd && cd && ((!o->lasttx) || o->radioduplex);

		if (o->lastrx && (!o->rxkeyed))
		{
			o->lastrx = 0;
			wf.subclass.integer = AST_CONTROL_RADIO_UNKEY;
			ast_queue_frame(o->owner, &wf);
			if (o->debuglevel) ast_verbose("Channel %s RX UNKEY\n",o->name);
		}
		else if ((!o->lastrx) && (o->rxkeyed))
		{
			o->lastrx = 1;
			wf.subclass.integer = AST_CONTROL_RADIO_KEY;
			ast_queue_frame(o->owner, &wf);
			if (o->debuglevel) ast_verbose("Channel %s RX KEY\n",o->name);
		}

		n = o->txkeyed || o->txtestkey;
		if (o->lasttx && (!n))
		{
			o->lasttx = 0;
			ast_mutex_lock(&gpio_lock);
			set_ptt(m,0);
			ast_mutex_unlock(&gpio_lock);
			if (o->debuglevel) ast_verbose("Channel %s TX UNKEY\n",o->name);
		}
		else if ((!o->lasttx) && n)
		{
			o->lasttx = 1;
			ast_mutex_lock(&gpio_lock);
			set_ptt(m,1);
			ast_mutex_unlock(&gpio_lock);
			if (o->debuglevel) ast_verbose("Channel %s TX KEY\n",o->name);
		}

		gv = get_gpios() & MASK_GPIOS_GPIOS;
		for(i = 1; i <= 5; i++)
		{
			/* if a valid input bit, dont clear it */
			if ((o->gpios[i]) && (!strcasecmp(o->gpios[i],"in"))) continue;
			gv &= ~(gpios_mask[i]); /* clear the bit, since its not an input */
		}
		if (((!o->had_gpios_in) || (o->last_gpios_in != gv)) &&
			(!ast_tvzero(o->starttime)) && (ast_tvdiff_ms(ast_tvnow(),o->starttime) >= 550))
		{
			char buf1[100];
			struct ast_frame fr;

			for(i = 1; i <= 5; i++)
			{
				/* skip if not specified */
				if (!o->gpios[i]) continue;
				/* skip if not input */
				if (strcasecmp(o->gpios[i],"in")) continue;
				/* if bit has changed, or never reported */
				if ((!o->had_gpios_in) || ((o->last_gpios_in & gpios_mask[i]) != (gv & (gpios_mask[i]))))
				{
					sprintf(buf1,"GPIO%d %d",i,(gv & (gpios_mask[i])) ? 1 : 0);
					memset(&fr,0,sizeof(fr));
					fr.data.ptr =  buf1;
					fr.datalen = strlen(buf1) + 1;
					fr.samples = 0;
					fr.frametype = AST_FRAME_TEXT;
					fr.subclass.integer = 0;
					fr.src = "chan_beagle";
					fr.offset = 0;
					fr.mallocd=0;
					fr.delivery.tv_sec = 0;
					fr.delivery.tv_usec = 0;
					ast_queue_frame(o->owner,&fr);
				}
			}
			o->had_gpios_in = 1;
			o->last_gpios_in = gv;
		}
		f->offset = AST_FRIENDLY_OFFSET;
		if (ast_channel_state(c) != AST_STATE_UP) {
			continue; /* drop data if frame is not up */
		}
		/* ok we can build and deliver the frame to the caller */
		f->frametype = AST_FRAME_VOICE;
		f->subclass.format = ast_format_slin;
		f->samples = FRAME_SIZE;
		f->datalen = FRAME_SIZE * 2;
		f->data.ptr = o->beagle_read_frame_buf + AST_FRIENDLY_OFFSET;
//		if (!o->rxkeyed) memset(f->data,0,f->datalen);
		if (o->usedtmf && o->dsp)
		{
		    f1 = ast_dsp_process(c,o->dsp,f);
		    if ((f1->frametype == AST_FRAME_DTMF_END) ||
		      (f1->frametype == AST_FRAME_DTMF_BEGIN))
		    {
			if ((f1->subclass.integer == 'm') || (f1->subclass.integer == 'u')) continue;
			if (f1->frametype == AST_FRAME_DTMF_END) {
				ast_debug(1,"Got DTMF char %c\n", f1->subclass.integer);
			}
			ast_queue_frame(o->owner,f1);
			continue;
		    }
		}
		if (o->rxvoiceadj > 1.0) {  /* scale and clip values */
			int i, x;
			float f1;
			int16_t *p = f->data.ptr;

			for (i = 0; i < f->samples; i++) {
				f1 = (float) p[i] * o->rxvoiceadj;
				x = (int) f1;
				if (x > 32767)
					x = 32767;
				else if (x < -32768)
					x = -32768;
				p[i] = x;
			}
		}
		if (o->measure_enabled)
		{
			int i;
			int32_t accum;
			int16_t *p = f->data.ptr;

			for(i = 0; i < f->samples; i++)
			{
				accum = p[i];
				if (accum > o->amax)
				{
					o->amax = accum;
					o->discounteru = o->discfactor;
				}
				else if (--o->discounteru <= 0)
				{
					o->discounteru = o->discfactor;
					o->amax = (int32_t)((o->amax * 32700) / 32768);
				}
				if (accum < o->amin)
				{
					o->amin = accum;
					o->discounterl = o->discfactor;
				}
				else if (--o->discounterl <= 0)
				{
					o->discounterl = o->discfactor;
					o->amin= (int32_t)((o->amin * 32700) / 32768);
				}
			}
			o->apeak = (int32_t)(o->amax - o->amin) / 2;
		}
		f->offset = AST_FRIENDLY_OFFSET;
		ast_queue_frame(o->owner,f);
	}
	f->frametype = AST_FRAME_NULL;
		return f;
}

static int beagle_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct chan_beagle_pvt *o = ast_channel_tech_pvt(newchan);
	o->owner = newchan;
	return 0;
}

static int beagle_indicate(struct ast_channel *c, int cond, const void *data, size_t datalen)
{
	struct chan_beagle_pvt *o = ast_channel_tech_pvt(c);

	switch (cond) {
		case AST_CONTROL_BUSY:
		case AST_CONTROL_CONGESTION:
		case AST_CONTROL_RINGING:
			break;

		case -1:
			return 0;

		case AST_CONTROL_VIDUPDATE:
			break;
		case AST_CONTROL_HOLD:
			ast_verbose(" << Chan_beagle Has Been Placed on Hold >> \n");
			ast_moh_start(c, data, o->mohinterpret);
			break;
		case AST_CONTROL_UNHOLD:
			ast_verbose(" << Chan_beagle Has Been Retrieved from Hold >> \n");
			ast_moh_stop(c);
			break;
		case AST_CONTROL_PROCEEDING:
			ast_verbose(" << Call Proceeding... >> \n");
			ast_moh_stop(c);
			break;
		case AST_CONTROL_PROGRESS:
			ast_verbose(" << Call Progress... >> \n");
			ast_moh_stop(c);
			break;
		case AST_CONTROL_RADIO_KEY:
			o->txkeyed = 1;
			break;
		case AST_CONTROL_RADIO_UNKEY:
			o->txkeyed = 0;
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to display condition %d on %s\n", cond, ast_channel_name(c));
			return -1;
	}

	return 0;
}

static int beagle_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
	char *cp;
	struct chan_beagle_pvt *o = ast_channel_tech_pvt(chan);

	/* all supported options require data */
	if (!data || (datalen < 1)) {
		errno = EINVAL;
		return -1;
	}

	switch (option) {
	case AST_OPTION_TONE_VERIFY:
		cp = (char *) data;
		switch (*cp) {
		case 1:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: OFF(0) on %s\n", ast_channel_name(chan));
			o->usedtmf = 1;
			break;
		case 2:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: MUTECONF/MAX(2) on %s\n", ast_channel_name(chan));
			o->usedtmf = 1;
			break;
		case 3:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: DISABLE DETECT(3) on %s\n", ast_channel_name(chan));
			o->usedtmf = 0;
			break;
		default:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: OFF(0) on %s\n", ast_channel_name(chan));
			o->usedtmf = 1;
			break;
		}
		break;
	}
	errno = 0;
	return 0;
}

/*
 * allocate a new channel.
 */
static struct ast_channel *beagle_new(struct chan_beagle_pvt *o, char *ext, char *ctx, int state, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor)
{
	struct ast_channel *c;

	c = ast_channel_alloc(1, state, o->cid_num, o->cid_name, "", ext, ctx, assignedids, requestor, 0, "beagle/%s", o->name);
	if (c == NULL)
		return NULL;
	ast_channel_tech_pvt_set(c, &beagle_tech);

	if (o == &pvts[0]) {
		if (pvts[1].owner) {
			ast_channel_internal_fd_set(pvts[1].owner, 0, -1);
		}
		ast_channel_internal_fd_set(pvts[0].owner, 0, readdev);
	} else {
		if (!pvts[0].owner) {
			ast_channel_internal_fd_set(pvts[0].owner, 0, readdev);
		}
	}

	ast_channel_nativeformats_set(c, beagle_tech.capabilities);
	ast_channel_set_readformat(c, ast_format_slin);
	ast_channel_set_writeformat(c, ast_format_slin);
	ast_channel_tech_pvt_set(c, o);
	ast_channel_unlock(c);

	if (!ast_strlen_zero(o->language)) {
		ast_channel_language_set(c, o->language);
	}

	/* Don't use ast_set_callerid() here because it will
	 * generate a needless NewCallerID event */
	ast_channel_caller(c)->id.number.str = ast_strdup(o->cid_num);
	ast_channel_caller(c)->ani.number.str = ast_strdup(o->cid_num);
	ast_channel_caller(c)->id.name.str = ast_strdup(o->cid_name);
	if (!ast_strlen_zero(ext)) {
		ast_channel_dialed(c)->number.str = ast_strdup(ext);
	}

	o->owner = c;
	ast_module_ref(ast_module_info->self);
	ast_jb_configure(c, &global_jbconf);
	if (ast_channel_state(c) != AST_STATE_DOWN) {
		if (ast_pbx_start(c)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ast_channel_name(c));
			ast_hangup(c);
			o->owner = c = NULL;
			/* XXX what about the channel itself ? */
			/* XXX what about usecnt ? */
		}
	}
	return c;
}

static struct ast_channel *beagle_request(const char *type, struct ast_format_cap *cap,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	struct ast_channel *c;
	struct chan_beagle_pvt *o = find_pvt(data);

	if (o == NULL) {
		ast_log(LOG_NOTICE, "Device %s not found\n", (char *) data);
		/* XXX we could default to 'dsp' perhaps ? */
		return NULL;
	}
	if (!(ast_format_cap_iscompatible(cap, beagle_tech.capabilities))) {
		struct ast_str *cap_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		ast_log(LOG_NOTICE, "Channel requested with unsupported format(s): '%s'\n", ast_format_cap_get_names(cap, &cap_buf));
		return NULL;
	}
	if (o->owner) {
		ast_log(LOG_NOTICE, "Already have a call (chan %p) on the usb channel\n", o->owner);
		*cause = AST_CAUSE_BUSY;
		return NULL;
	}
	c = beagle_new(o, NULL, NULL, AST_STATE_DOWN, assignedids, requestor);
	if (c == NULL) {
		ast_log(LOG_WARNING, "Unable to create new usb channel\n");
		return NULL;
	}
	if (!has_been_open)
	{
		snd_pcm_prepare(alsa.icard);
		snd_pcm_start(alsa.icard);
		has_been_open = 1;
	}
	o->starttime = ast_tvnow();
	return c;
}

static int beagle_key(int fd, int argc, const char *const *argv)
{
	struct chan_beagle_pvt *o = find_pvt(beagle_active);

	if (argc != 2)
		return RESULT_SHOWUSAGE;
	o->txtestkey = 1;
	return RESULT_SUCCESS;
}

static int beagle_unkey(int fd, int argc, const char *const *argv)
{
	struct chan_beagle_pvt *o = find_pvt(beagle_active);

	if (argc != 2)
		return RESULT_SHOWUSAGE;
	o->txtestkey = 0;
	return RESULT_SUCCESS;
}

static int rad_rxwait(int fd,int ms)
{
	ast_fdset fds;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(fd,&fds);
	tv.tv_usec = ms * 1000;
	tv.tv_sec = 0;
	return(ast_select(fd + 1,&fds,NULL,NULL,&tv));
}

static void tune_rxdisplay(int fd, struct chan_beagle_pvt *o)
{
	int j,waskeyed,meas,ncols = 75,wasverbose;
	char str[256];

	for(j = 0; j < ncols; j++) str[j] = ' ';
	str[j] = 0;
	ast_cli(fd," %s \r",str);
	ast_cli(fd,"RX VOICE DISPLAY:\n");
	ast_cli(fd,"                                 v -- 3KHz        v -- 5KHz\n");

	o->measure_enabled = 1;
	o->discfactor = 1000;
	o->discounterl = o->discounteru = 0;
	wasverbose = option_verbose;
	option_verbose = 0;

	waskeyed = !o->rxkeyed;
	for(;;)
	{
		o->amax = o->amin = 0;
		if (rad_rxwait(fd,100)) break;
		if (o->rxkeyed != waskeyed)
		{
			for(j = 0; j < ncols; j++) str[j] = ' ';
			str[j] = 0;
			ast_cli(fd," %s \r",str);
		}
		waskeyed = o->rxkeyed;
		if (!o->rxkeyed) {
			ast_cli(fd,"\r");
			continue;
		}
		meas = o->apeak;
		for(j = 0; j < ncols; j++)
		{
			int thresh = (meas * ncols) / 16384;
			if (j < thresh) str[j] = '=';
			else if (j == thresh) str[j] = '>';
			else str[j] = ' ';
		}
		str[j] = 0;
		ast_cli(fd,"|%s|\r",str);
	}
	o->measure_enabled = 0;
	option_verbose = wasverbose;
}

static int beagle_tune(int fd, int argc, const char *const *argv)
{
	struct chan_beagle_pvt *o = find_pvt(beagle_active);
	int i=0;

	if ((argc < 2) || (argc > 4))
		return RESULT_SHOWUSAGE;

	if (argc == 2) /* just show stuff */
	{
		ast_cli(fd,"Active radio interface is [%s]\n",beagle_active);
		ast_cli(fd,"Rx Level currently set to %d\n",o->rxmixerset);
		ast_cli(fd,"Tx Output Level currently set to %d\n",o->txmixerset);
		return RESULT_SHOWUSAGE;
	}

	if (!strcasecmp(argv[2],"rx")) {
		i = 0;

		if (argc == 3)
		{
			ast_cli(fd,"Current setting on Rx Channel is %d\n",o->rxmixerset);
		}
		else
		{
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) return RESULT_SHOWUSAGE;
		 	o->rxmixerset = i;
			ast_cli(fd,"Changed setting on RX Channel to %d\n",o->rxmixerset);
			mixer_write();
		}
	}
	else if (!strncasecmp(argv[2],"rxd",3)) {
		tune_rxdisplay(fd,o);
	}
	else if (!strcasecmp(argv[2],"tx")) {
		i = 0;

		if (argc == 3)
		{
			ast_cli(fd,"Current setting on Tx Channel is %d\n",o->txmixerset);
		}
		else
		{
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) return RESULT_SHOWUSAGE;
		 	o->txmixerset = i;
			ast_cli(fd,"Changed setting on TX Channel to %d\n",o->txmixerset);
			mixer_write();
		}
	}
	else if (!strcasecmp(argv[2],"nocap"))
	{
		ast_cli(fd,"cap off.\n");
		if (frxcapraw) { fclose(frxcapraw); frxcapraw = NULL; }
		if (ftxcapraw) { fclose(ftxcapraw); ftxcapraw = NULL; }
	}
	else if (!strcasecmp(argv[2],"rxcap"))
	{
		if (!frxcapraw) frxcapraw = fopen(RX_CAP_RAW_FILE,"w");
		ast_cli(fd,"cap rx raw on.\n");
	}
	else if (!strcasecmp(argv[2],"txcap"))
	{
		if (!ftxcapraw) ftxcapraw = fopen(TX_CAP_RAW_FILE,"w");
		ast_cli(fd,"cap tx raw on.\n");
	}
	else if (!strcasecmp(argv[2],"save"))
	{
		tune_write(o);
		ast_cli(fd,"Saved radio tuning settings to beagle_tune_%s.conf\n",o->name);
	}
	else
	{
		return RESULT_SHOWUSAGE;
	}
	return RESULT_SUCCESS;
}

/*
	CLI debugging on and off
*/
static int beagle_set_debug(int fd, int argc, const char *const *argv)
{
	struct chan_beagle_pvt *o = find_pvt(beagle_active);

	o->debuglevel=1;
	ast_cli(fd,"beagle debug on.\n");
	return RESULT_SUCCESS;
}

static int beagle_set_debug_off(int fd, int argc, const char *const *argv)
{
	struct chan_beagle_pvt *o = find_pvt(beagle_active);

	o->debuglevel=0;
	ast_cli(fd,"beagle debug off.\n");
	return RESULT_SUCCESS;
}

static int beagle_active_cmd(int fd, int argc, const char *const *argv)
{
		if (argc == 2)
				ast_cli(fd, "active (command) Beagleboard device is [%s]\n", beagle_active);
		else if (argc != 3)
				return RESULT_SHOWUSAGE;
		else {
				struct chan_beagle_pvt *o;
				o = find_pvt(argv[2]);
				if (o == NULL)
						ast_cli(fd, "No device [%s] exists\n", argv[2]);
				else
		{
					beagle_active = o->name;
		}
		}
		return RESULT_SUCCESS;
}

static char key_usage[] =
	"Usage: beagle key\n"
	"       Simulates COR active.\n";

static char unkey_usage[] =
	"Usage: beagle unkey\n"
	"       Simulates COR un-active.\n";

static char active_usage[] =
		"Usage: beagle active [device-name]\n"
		"       If used without a parameter, displays which device is the current\n"
		"one being commanded.  If a device is specified, the commanded radio device is changed\n"
		"to the device specified.\n";

static char beagle_tune_usage[] =
	"Usage: beagle tune <function>\n"
	"       rx [newsetting]\n"
	"       rxdisplay\n"
	"       txa [newsetting]\n"
	"       txb [newsetting]\n"
	"       save (settings to tuning file)\n"
	"       load (tuning settings from EEPROM)\n"
	"\n       All [newsetting]'s are values 0-999\n\n";

static void store_rxcdtype(struct chan_beagle_pvt *o, const char *s)
{
	if (!strcasecmp(s,"no")){
		o->rxcdtype = CD_IGNORE;
	} else if (!strcasecmp(s,"hw")){
		o->rxcdtype = CD_HID;
	} else if (!strcasecmp(s,"hwinvert")){
		o->rxcdtype = CD_HID_INVERT;
	} else {
		ast_log(LOG_WARNING,"Unrecognized rxcdtype parameter: %s\n",s);
	}
}

static void store_rxsdtype(struct chan_beagle_pvt *o, const char *s)
{
	if (!strcasecmp(s,"no") || !strcasecmp(s,"SD_IGNORE")){
		o->rxsdtype = SD_IGNORE;
	} else if (!strcasecmp(s,"hw") || !strcasecmp(s,"SD_HID")){
		o->rxsdtype = SD_HID;
	} else if (!strcasecmp(s,"hwinvert") || !strcasecmp(s,"SD_HID_INVERT")){
		o->rxsdtype = SD_HID_INVERT;
	} else {
		ast_log(LOG_WARNING,"Unrecognized rxsdtype parameter: %s\n",s);
	}
}

static void tune_write(struct chan_beagle_pvt *o)
{
	FILE *fp;
	char fname[200];

	snprintf(fname,sizeof(fname) - 1,"/etc/asterisk/beagle_tune_%s.conf",o->name);
	fp = fopen(fname,"w");

	fprintf(fp,"[%s]\n",o->name);

	fprintf(fp,"; name=%s\n",o->name);
	fprintf(fp,"rxmixerset=%i\n",o->rxmixerset);
	fprintf(fp,"txmixerset=%i\n",o->txmixerset);
	fclose(fp);
}

static int mixer_write(void)
{
int	v1,v2;
float f,f1;

	if (setamixer(0,"AVADC Clock Priority",0,-1) == -1) return -1;
	if (setamixer(0,"Analog Capture Volume",0,0) == -1) return -1;
	if (setamixer(0,"Analog Left AUXL Capture Switch",1,-1) == -1) return -1;
	if (setamixer(0,"Analog Left Carkit Mic Capture Switch",0,-1) == -1) return -1;
	if (setamixer(0,"Analog Left Headset Mic Capture Switch",0,-1) == -1) return -1;
	if (setamixer(0,"Analog Left Main Mic Capture Switch",0,-1) == -1) return -1;
	if (setamixer(0,"Analog Right AUXR Capture Switch",1,-1) == -1) return -1;
	if (setamixer(0,"Analog Right Sub Mic Capture Switch",0,-1) == -1) return -1;
	if (setamixer(0,"Carkit Playback Volume",0,0) == -1) return -1;
	if (setamixer(0,"CarkitL Mixer AudioL1",0,-1) == -1) return -1;
	if (setamixer(0,"CarkitL Mixer AudioL2",0,-1) == -1) return -1;
	if (setamixer(0,"CarkitL Mixer Voice",0,-1) == -1) return -1;
	if (setamixer(0,"CarkitR Mixer AudioR1",0,-1) == -1) return -1;
	if (setamixer(0,"CarkitR Mixer AudioR2",0,-1) == -1) return -1;
	if (setamixer(0,"CarkitR Mixer Voice",0,-1) == -1) return -1;
	if (setamixer(0,"DAC Voice Analog Downlink Switch",0,-1) == -1) return -1;
	if (setamixer(0,"DAC Voice Analog Downlink Volume",18,-1) == -1) return -1;
	if (setamixer(0,"DAC Voice Digital Downlink Volume",0,-1) == -1) return -1;
	if (setamixer(0,"DAC1 Analog Playback Switch",0,0) == -1) return -1;
	if (setamixer(0,"DAC1 Analog Playback Volume",18,18) == -1) return -1;
	if (setamixer(0,"DAC1 Digital Coarse Playback Volume",0,0) == -1) return -1;
	if (setamixer(0,"DAC1 Digital Fine Playback Volume",50,50) == -1) return -1;
	if (setamixer(0,"DAC2 Analog Playback Switch",1,1) == -1) return -1;
	if (setamixer(0,"DAC2 Analog Playback Volume",18,18) == -1) return -1;
	if (setamixer(0,"DAC2 Digital Coarse Playback Volume",0,0) == -1) return -1;
	v1 =  pvts[0].txmixerset * PLAYBACK_MAX / 1000;
	v2 =  pvts[1].txmixerset * PLAYBACK_MAX / 1000;
	if (setamixer(0,"DAC2 Digital Fine Playback Volume",v2,v1) == -1) return -1;
	if (setamixer(0,"Digimic LR Swap",0,-1) == -1) return -1;
	if (setamixer(0,"Earpiece Mixer AudioL1",0,-1) == -1) return -1;
	if (setamixer(0,"Earpiece Mixer AudioL2",0,-1) == -1) return -1;
	if (setamixer(0,"Earpiece Mixer AudioR1",0,-1) == -1) return -1;
	if (setamixer(0,"Earpiece Mixer Voice",0,-1) == -1) return -1;
	if (setamixer(0,"Earpiece Playback Volume",0,-1) == -1) return -1;
	if (setamixer(0,"HS ramp delay",0,-1) == -1) return -1;
	if (setamixer(0,"HandsfreeL Mux",0,-1) == -1) return -1;
	if (setamixer(0,"HandsfreeL Switch",0,-1) == -1) return -1;
	if (setamixer(0,"HandsfreeR Mux",0,-1) == -1) return -1;
	if (setamixer(0,"HandsfreeR Switch",0,-1) == -1) return -1;
	if (setamixer(0,"Headset Playback Volume",3,3) == -1) return -1;
	if (setamixer(0,"HeadsetL Mixer AudioL1",1,-1) == -1) return -1;
	if (setamixer(0,"HeadsetL Mixer AudioL2",1,-1) == -1) return -1;
	if (setamixer(0,"HeadsetL Mixer Voice",0,-1) == -1) return -1;
	if (setamixer(0,"HeadsetR Mixer AudioR1",1,-1) == -1) return -1;
	if (setamixer(0,"HeadsetR Mixer AudioR2",1,-1) == -1) return -1;
	if (setamixer(0,"HeadsetR Mixer Voice",0,-1) == -1) return -1;
	if (setamixer(0,"Left Digital Loopback Volume",0,-1) == -1) return -1;
	if (setamixer(0,"Left1 Analog Loopback Switch",0,-1) == -1) return -1;
	if (setamixer(0,"Left2 Analog Loopback Switch",0,-1) == -1) return -1;
	if (setamixer(0,"PreDriv Playback Volume",0,0) == -1) return -1;
	if (setamixer(0,"PredriveL Mixer AudioL1",0,-1) == -1) return -1;
	if (setamixer(0,"PredriveL Mixer AudioL2",0,-1) == -1) return -1;
	if (setamixer(0,"PredriveL Mixer AudioR2",0,-1) == -1) return -1;
	if (setamixer(0,"PredriveL Mixer Voice",0,-1) == -1) return -1;
	if (setamixer(0,"PredriveR Mixer AudioL2",0,-1) == -1) return -1;
	if (setamixer(0,"PredriveR Mixer AudioR1",0,-1) == -1) return -1;
	if (setamixer(0,"PredriveR Mixer AudioR2",0,-1) == -1) return -1;
	if (setamixer(0,"PredriveR Mixer Voice",0,-1) == -1) return -1;
	if (setamixer(0,"Right Digital Loopback Volume",0,-1) == -1) return -1;
	if (setamixer(0,"Right1 Analog Loopback Switch",0,-1) == -1) return -1;
	if (setamixer(0,"Right2 Analog Loopback Switch",0,-1) == -1) return -1;
	if (setamixer(0,"TX1 Capture Route",0,-1) == -1) return -1;
	v1 =  pvts[0].rxmixerset * 32 / 1000;
	v2 =  pvts[1].rxmixerset * 32 / 1000;
	/* get interval step size */
	f = 1000.0 / 32.0;
	pvts[0].rxvoiceadj = 1.0 + (modff(((float) pvts[0].rxmixerset) / f,&f1) * .093981);
	pvts[1].rxvoiceadj = 1.0 + (modff(((float) pvts[1].rxmixerset) / f,&f1) * .093981);
	if (setamixer(0,"TX1 Digital Capture Volume",v2,v1) == -1) return -1;
	if (setamixer(0,"TX2 Capture Route",0,-1) == -1) return -1;
	if (setamixer(0,"TX2 Digital Capture Volume",0,0) == -1) return -1;
	if (setamixer(0,"Vibra H-bridge direction",0,-1) == -1) return -1;
	if (setamixer(0,"Vibra H-bridge mode",0,-1) == -1) return -1;
	if (setamixer(0,"Vibra Mux",0,-1) == -1) return -1;
	if (setamixer(0,"Vibra Route",0,-1) == -1) return -1;
	if (setamixer(0,"Voice Analog Loopback Switch",0,-1) == -1) return -1;
	if (setamixer(0,"Voice Digital Loopback Volume",0,-1) == -1) return -1;

	return 0;
}

/*
 * grab fields from the config file, init the descriptor and open the device.
 */
static struct chan_beagle_pvt *store_config(struct ast_config *cfg, char *ctg)
{
	struct ast_variable *v;
	struct chan_beagle_pvt *o;
	struct ast_config *cfg1;
	char fname[200],buf[100];
	int i;
	struct ast_flags zeroflag = {0};
	if (ctg == NULL) {
		o = &beagle_default;
		ctg = "general";
	} else {
		/* "general" is also the default thing */
		if (strcmp(ctg, "general") == 0) {
			o = &beagle_default;
		} else {
			if (ndevices == 2)
			{
				ast_log(LOG_ERROR,"You can only define 2 chan_beagle devices!!\n");
				goto error;
			}
			o = &pvts[ndevices++];
			memset(o,0,sizeof(struct chan_beagle_pvt));
			o->name = ast_strdup(ctg);
			if (!beagle_active)
				beagle_active = o->name;
		}
	}
	ast_mutex_init(&o->txqlock);
	strcpy(o->mohinterpret, "default");
	/* fill other fields from configuration */
	for (v = ast_variable_browse(cfg, ctg); v; v = v->next) {
		CV_START(v->name, v->value);

		/* handle jb conf */
		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value)) {
			continue;
		}

		CV_UINT("debug", beagle_debug)
		CV_BOOL("rxcpusaver",o->rxcpusaver)
		CV_BOOL("txcpusaver",o->txcpusaver)
		CV_BOOL("invertptt",o->invertptt)
		CV_F("carrierfrom",store_rxcdtype(o, v->value))
		CV_F("ctcssfrom",store_rxsdtype(o, v->value))
		CV_UINT("duplex",o->radioduplex)
		CV_BOOL("plfilter",o->plfilter)
		CV_BOOL("deemphasis",o->deemphasis)
		CV_END;
		for (i = 1; i <= 5; i++) {
			sprintf(buf,"gpio%d",i);
			if (!strcmp(v->name,buf)) {
				o->gpios[i] = ast_strdup(v->value);
			}
		}
	}

	o->debuglevel = 0;

	if (o == &beagle_default)		/* we are done with the default */
		return NULL;

	snprintf(fname, sizeof(fname) - 1, CONFIG_TUNE_FMT, o->name);

	cfg1 = ast_config_load(fname,zeroflag);
	o->rxmixerset = 500;
	o->txmixerset = 500;
	if (cfg1) {
		for (v = ast_variable_browse(cfg1, o->name); v; v = v->next) {
			CV_START(v->name, v->value);
			CV_UINT("rxmixerset", o->rxmixerset)
			CV_UINT("txmixerset", o->txmixerset)
			CV_END;
		}
		ast_config_destroy(cfg1);
	} else ast_log(LOG_WARNING,"File %s not found, using default parameters.\n",fname);

	o->dsp = ast_dsp_new();
	if (o->dsp) {
		  ast_dsp_set_features(o->dsp,DSP_FEATURE_DIGIT_DETECT);
		  ast_dsp_set_digitmode(o->dsp,DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
	}
	for(i = 1; i <= 5; i++) {
		/* skip if this one not specified */
		if (!o->gpios[i]) continue;
		/* skip if not out */
		if (strncasecmp(o->gpios[i],"out",3)) continue;
		gpio_ctl |= gpios_mask[i]; /* set this one to output, also */
		/* if default value is 1, set it */
		if (!strcasecmp(o->gpios[i],"out1")) gpio_val |= gpios_mask[i];
	}
	return o;

error:
	return NULL;
}

static char *res2cli(int r)
{
	switch (r) {
	    case RESULT_SUCCESS:
			return(CLI_SUCCESS);
	    case RESULT_SHOWUSAGE:
			return(CLI_SHOWUSAGE);
	    default:
			return(CLI_FAILURE);
	}
}

static char *handle_beagle_key(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
		switch (cmd) {
		case CLI_INIT:
				e->command = "beagle key";
				e->usage = key_usage;
				return NULL;
		case CLI_GENERATE:
				return NULL;
	}
	return res2cli(beagle_key(a->fd,a->argc,a->argv));
}

static char *handle_beagle_unkey(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
		switch (cmd) {
		case CLI_INIT:
				e->command = "beagle unkey";
				e->usage = unkey_usage;
				return NULL;
		case CLI_GENERATE:
				return NULL;
	}
	return res2cli(beagle_unkey(a->fd,a->argc,a->argv));
}

static char *handle_beagle_tune(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
		switch (cmd) {
		case CLI_INIT:
				e->command = "beagle tune";
				e->usage = beagle_tune_usage;
				return NULL;
		case CLI_GENERATE:
				return NULL;
	}
	return res2cli(beagle_tune(a->fd,a->argc,a->argv));
}

static char *handle_beagle_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
		switch (cmd) {
		case CLI_INIT:
				e->command = "beagle debug";
				e->usage = beagle_tune_usage;
				return NULL;
		case CLI_GENERATE:
				return NULL;
	}
	return res2cli(beagle_set_debug(a->fd,a->argc,a->argv));
}

static char *handle_beagle_debug_off(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
		switch (cmd) {
		case CLI_INIT:
				e->command = "beagle debug off";
				e->usage = beagle_tune_usage;
				return NULL;
		case CLI_GENERATE:
				return NULL;
	}
	return res2cli(beagle_set_debug_off(a->fd,a->argc,a->argv));
}

static char *handle_beagle_active(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
		switch (cmd) {
		case CLI_INIT:
				e->command = "beagle active";
				e->usage = active_usage;
				return NULL;
		case CLI_GENERATE:
				return NULL;
	}
	return res2cli(beagle_active_cmd(a->fd,a->argc,a->argv));
}

static struct ast_cli_entry cli_beagle[] = {
	AST_CLI_DEFINE(handle_beagle_key,"Simulate Rx Signal Present"),
	AST_CLI_DEFINE(handle_beagle_unkey,"Simulate Rx Signal Loss"),
	AST_CLI_DEFINE(handle_beagle_tune,"beagle Tune"),
	AST_CLI_DEFINE(handle_beagle_debug,"beagle Debug On"),
	AST_CLI_DEFINE(handle_beagle_debug_off,"beagle Debug Off"),
	AST_CLI_DEFINE(handle_beagle_active,"Change commanded device")
};

static int load_module(void)
{
	struct ast_config *cfg = NULL;
	char *ctg = NULL;
	volatile unsigned long *pinconf;
	unsigned long c;
	struct ast_flags zeroflag = {0};

#if !defined(__ARM_ARCH_7A__)
	ast_log(LOG_WARNING,"chan_beagle declining module load since WE ARE NOT A BEAGLEBOARD!!\n");
	return AST_MODULE_LOAD_DECLINE;
#endif
	beagle_active = NULL;

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	/* load config file */
	if (!(cfg = ast_config_load(config,zeroflag))) {
		ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
		return AST_MODULE_LOAD_DECLINE;
	}

	do {
		store_config(cfg, ctg);
	} while ( (ctg = ast_category_browse(cfg, ctg)) != NULL);

	ast_config_destroy(cfg);

	if (find_pvt(beagle_active) == NULL) {
		ast_log(LOG_NOTICE, "beagle active device not found\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	hwfd = open("/dev/mem", O_RDWR | O_SYNC);
	if (hwfd < 0)
	{
		ast_log(LOG_ERROR,"Can not open /dev/mem to get to beagleboard IO pins\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	pinconf = (unsigned long *) mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED, hwfd, 0x48000000);
	if (pinconf == MAP_FAILED) {
		ast_log(LOG_ERROR,"Can not map memory for GPIO access\n");
		close(hwfd);
		return 0;
	}

	pinconf[0x215C/4] = 0x01140114;
	pinconf[0x2160/4] = 0x01140114;
	pinconf[0x2164/4] = 0x01140114;
	pinconf[0x2168/4] = 0x01140114;
	pinconf[0x216C/4] = 0x01140114;
	pinconf[0x2170/4] = 0x01140114;
	pinconf[0x2174/4] = 0x01140114;
	pinconf[0x2178/4] = 0x01040114;

	close(hwfd);

	hwfd = open("/dev/mem", O_RDWR | O_SYNC);
	if (hwfd < 0)
	{
		ast_log(LOG_ERROR,"Can not open /dev/mem to get to beagleboard IO pins\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	gpio = (unsigned long *) mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED, hwfd, 0x49050000);
	if (gpio == MAP_FAILED) {
		ast_log(LOG_ERROR,"Can not map memory for GPIO access\n");
		close(hwfd);
		return 0;
	}

	c = gpio[0x6034/4];
	c &= ~GPIO_MASK;
	c |= GPIO_INPUTS;
	c &= ~gpio_ctl;
	gpio[0x6034/4] = c;

	set_gpios();
	gpio_then = ast_tvnow();

	if (pvts[0].invertptt) set_ptt(0,1); else set_ptt(0,0);
	if (pvts[1].invertptt) set_ptt(1,1); else set_ptt(1,0);

	alsa.icard = alsa_card_init("default", SND_PCCV_STREAM_CAPTURE);
	alsa.ocard = alsa_card_init("default", SND_PCCV_STREAM_PLAYBACK);
	if (!alsa.icard || !alsa.ocard) {
		ast_log(LOG_ERROR, "Problem opening alsa I/O devices\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if (mixer_write() == -1) return AST_MODULE_LOAD_FAILURE;

	if (!(beagle_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append(beagle_tech.capabilities, ast_format_slin, 0);

	if (ast_channel_register(&beagle_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel type 'beagle'\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	ast_cli_register_multiple(cli_beagle, sizeof(cli_beagle) / sizeof(struct ast_cli_entry));

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	int i;

	ast_log(LOG_WARNING, "unload_module() called\n");

	ast_channel_unregister(&beagle_tech);
	ast_cli_unregister_multiple(cli_beagle, sizeof(cli_beagle) / sizeof(struct ast_cli_entry));

	if (alsa.icard)
		snd_pcm_close(alsa.icard);
	if (alsa.ocard)
		snd_pcm_close(alsa.ocard);

	if (hwfd != -1)
	{
		set_ptt(0,0);
		set_ptt(1,0);
		close(hwfd);
	}

#if DEBUG_CAPTURES == 1
	if (frxcapraw) { fclose(frxcapraw); frxcapraw = NULL; }
	if (ftxcapraw) { fclose(ftxcapraw); ftxcapraw = NULL; }
#endif
	for(i = 0; i < 2; i++)
	{
		if (pvts[i].dsp) ast_dsp_free(pvts[i].dsp);
		if (pvts[i].owner)
			ast_softhangup(pvts[i].owner, AST_SOFTHANGUP_APPUNLOAD);
		/* XXX what about the thread ? */
		/* XXX what about the memory allocated ? */
	}
	ao2_cleanup(beagle_tech.capabilities);
	beagle_tech.capabilities = NULL;
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Beagleboard Radio Interface Channel Driver");
