/*
 * Asterisk -- An open source telephony toolkit.
 * Copyright (C) 1999 - 2005, Digium, Inc.
 * Copyright (C) 2007 - 2008, Jim Dixon
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
 * \brief Simple Channel driver for CM108 USB Cards with Radio Interface
 *
 * \author Jim Dixon  <jim@lambdatel.com>
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<depend>alsa</depend>
	<depend>res_usbradio</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdlib.h>
#include <errno.h>
#include <usb.h>
#include <search.h>
#include <alsa/asoundlib.h>
#include <linux/ppdev.h>
#include <linux/parport.h>
#include <linux/version.h>

#include "asterisk/res_usbradio.h"
#include "asterisk/rpt_chan_shared.h"

#ifdef HAVE_SYS_IO
#include <sys/io.h>
#endif

#include "../apps/app_rpt/pocsag.c"	/*! \todo this may need to be moved to a better place... */

#define DEBUG_CAPTURES	 		1

#define RX_CAP_RAW_FILE			"/tmp/rx_cap_in.pcm"
#define RX_CAP_COOKED_FILE		"/tmp/rx_cap_8k_in.pcm"
#define TX_CAP_RAW_FILE			"/tmp/tx_cap_in.pcm"

#define	DELIMCHR ','
#define	QUOTECHR 34

#define READERR_THRESHOLD 50
#define DEFAULT_ECHO_MAX 1000 /* 20 secs of echo buffer, max */
#define PP_MASK 0xbffc
#define PP_PORT "/dev/parport0"
#define PP_IOPORT 0x378
#define N_FMT(duf) "%30" #duf /* Maximum sscanf conversion to numeric strings */
#define HID_POLL_RATE 50

#ifdef __linux
#include <linux/soundcard.h>
#elif defined(__FreeBSD__)
#include <sys/soundcard.h>
#else
#include <soundcard.h>
#endif

#include "asterisk/lock.h"
#include "asterisk/frame.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/musiconhold.h"
#include "asterisk/dsp.h"
#include "asterisk/format.h"
#include "asterisk/format_cache.h"
#include "asterisk/format_compatibility.h"

/*! Global jitterbuffer configuration - by default, jb is disabled */
static struct ast_jb_conf default_jbconf = {
	.flags = 0,
	.max_size = -1,
	.resync_threshold = -1,
	.impl = "",
};

static struct ast_jb_conf global_jbconf;

#define	NTAPS 31
#define	NTAPS_PL 6

/*! Defines for constructing POSAG paging packets */
#define	PAGER_SRC "PAGER"
#define	ENDPAGE_STR "ENDPAGE"
#define AMPVAL 12000
#define	SAMPRATE 8000			// (Sample Rate)
#define	DIVLCM 192000			// (Least Common Mult of 512,1200,2400,8000)
#define	PREAMBLE_BITS 576
#define	MESSAGE_BITS 544		// (17 * 32), 1 longword SYNC plus 16 longwords data
#define	ONEVAL -AMPVAL
#define ZEROVAL AMPVAL
#define	DIVSAMP (DIVLCM / SAMPRATE)

#define	QUEUE_SIZE	5			/* 100 milliseconds of sound card output buffer */

#define CONFIG "simpleusb.conf"				   /* default config file */
#define RX_ON_DELAY_MAX 60000				   /* in ms, 60000ms, 60 seconds, 1 minute */
#define TX_OFF_DELAY_MAX 60000				   /* in ms, 60000ms, 60 seconds, 1 minute */
#define MS_PER_FRAME 20						   /* 20 ms frames */
#define MS_TO_FRAMES(ms) ((ms) / MS_PER_FRAME) /* convert ms to frames */

/* file handles for writing debug audio packets */
static FILE *frxcapraw = NULL;
static FILE *frxcapcooked = NULL;
static FILE *ftxcapraw = NULL;

AST_MUTEX_DEFINE_STATIC(usb_dev_lock);
AST_MUTEX_DEFINE_STATIC(pp_lock);

/* variables for communicating with the parallel port */
static int8_t pp_val;
static int8_t pp_pulsemask;
static int8_t pp_lastmask;
static int pp_pulsetimer[32];
static int haspp;
static int ppfd;
static char pport[50];
static int pbase;
static char stoppulser;
static char hasout;
pthread_t pulserid;

/*! \brief type of signal detection used for carrier (cos) or ctcss */
static const char *const cd_signal_type[] = { "no", "N/A", "N/A", "usb", "usbinvert", "pp", "ppinvert" };
static const char *const sd_signal_type[] = { "no", "usb", "usbinvert", "N/A", "pp", "ppinvert" };

/*!
 * \brief Descriptor for one of our channels.
 * There is one used for 'default' values (from the [general] entry in
 * the configuration file), and then one instance for each device
 * (the default is cloned from [general], others are only created
 * if the relevant section exists).
 */
struct chan_simpleusb_pvt {
	struct chan_simpleusb_pvt *next;

	char *name;					/* the internal name of our channel */
	int devtype;				/* actual type of device */
	int pttkick[2];				/* ptt kick pipe */
	int total_blocks;			/* total blocks in the output device */
	int sounddev;
	enum { M_UNSET, M_FULL, M_READ, M_WRITE } duplex;
	int hookstate;
	unsigned int queuesize;		/* max fragments in queue */
	unsigned int frags;			/* parameter for SETFRAGMENT */

	int warned;					/* various flags used for warnings */
#define WARN_used_blocks	1
#define WARN_speed			2
#define WARN_frag			4

	char devicenum;
	char devstr[128];
	int spkrmax;
	int micmax;
	int micplaymax;

	pthread_t hidthread;
	int stophid;

	struct ast_channel *owner;

	/* buffers used in simpleusb_write, 2 per int */
	char simpleusb_write_buf[FRAME_SIZE * 2];

	int simpleusb_write_dst;
	/* buffers used in simpleusb_read - AST_FRIENDLY_OFFSET space for headers
	 * plus enough room for a full frame
	 */
	char simpleusb_read_buf[FRAME_SIZE * 4 * 6];	/* 2 bytes * 2 channels * 6 for 48K */
	char simpleusb_read_frame_buf[FRAME_SIZE * 2 + AST_FRIENDLY_OFFSET];
	int readpos;				/* read position above */
	struct ast_frame read_f;	/* returned by simpleusb_read */
	
	/* queue used to hold packets to transmit */
	AST_LIST_HEAD_NOLOCK(, ast_frame) txq;
	ast_mutex_t txqlock;

	char lastrx;
	char rxhidsq;
	char rxhidctcss;
	char rxppsq;
	char rxppctcss;

	char rxkeyed;				/* Indicates rx signal is present */

	char rxctcssoverride;

	char lasttx;
	char txkeyed;				/* tx key request from upper layers */
	char txtestkey;

	time_t lasthidtime;
	struct ast_dsp *dsp;

	short flpt[NTAPS + 1];
	short flpr[NTAPS + 1];

	float hpx[NTAPS_PL + 1];
	float hpy[NTAPS_PL + 1];

	int32_t destate;			/* deemphasis state variable */
	int32_t prestate;			/* preemphasis state variable */

	enum radio_carrier_detect rxcdtype;
	enum radio_squelch_detect rxsdtype;

	int rxoncnt;				/* Counts the number of 20 ms intervals after RX activity */
	int txoffcnt;				/* Counts the number of 20 ms intervals after TX unkey */
	int rxondelay;				/* This is the value which RX is ignored after RX activity */
	int txoffdelay;				/* This is the value which RX is ignored after TX unkey */

	int pager;
	int waspager;

	int rxmixerset;
	float rxvoiceadj;
	int txmixaset;
	int txmixbset;

	/*! Settings for echoing received audio */
	int echomode;
	int echoing;
	ast_mutex_t echolock;
	struct qelem echoq;
	int echomax;

	/*! Settings for HID interface */
	int hdwtype;
	int hid_gpio_ctl;
	int hid_gpio_ctl_loc;
	int hid_io_cor;
	int hid_io_cor_loc;
	int hid_io_ctcss;
	int hid_io_ctcss_loc;
	int hid_io_ptt;
	int hid_gpio_loc;
	int32_t hid_gpio_val;
	int32_t valid_gpios;
	int32_t gpio_set;
	int32_t last_gpios_in;
	int had_gpios_in;
	int hid_gpio_pulsetimer[GPIO_PINCOUNT];
	int32_t hid_gpio_pulsemask;
	int32_t hid_gpio_lastmask;

	/*! Track parallel port values */
	int8_t last_pp_in;
	char had_pp_in;

	/* bit fields */
	unsigned int rxcapraw:1;		/* indicator if receive capture is enabled */
	unsigned int txcapraw:1;		/* indicator if transmit capture is enabled */
	unsigned int measure_enabled:1;	/* indicator if measure mode is enabled */
	unsigned int device_error:1;	/* indicator set when we cannot find the USB device */
	unsigned int newname:1;			/* indicator that we should use MIXER_PARAM_SPKR_PLAYBACK_VOL_NEW */ 
	unsigned int hasusb:1;			/* indicator for has a USB device */
	unsigned int usbass:1;			/* indicator for USB device assigned */
	unsigned int wanteeprom:1;		/* indicator if we should use EEPROM */
	unsigned int usedtmf:1;			/* indicator is we should decode DTMF */
	unsigned int invertptt:1;		/* indicator if we need to invert ptt */
	unsigned int rxboost:1;			/* indicator if receive boost is needed */
	unsigned int plfilter:1;		/* indicator if we need a pl filter */
	unsigned int deemphasis:1;		/* indicator if we need deemphasis filter */
	unsigned int preemphasis:1;		/* indicator if we need preemphasis filter */
	unsigned int rx_cos_active:1;	/* indicator if cos is active - active state after processing */
	unsigned int rx_ctcss_active:1;	/* indicator if ctcss is active - active state after processing */

	/* EEPROM access variables */
	unsigned short eeprom[EEPROM_USER_LEN];
	char eepromctl;
	ast_mutex_t eepromlock;

	struct usb_dev_handle *usb_handle;
	int readerrs;
	struct timeval tonetime;
	int toneflag;
	int duplex3;
	int clipledgpio;           /* enables ADC Clip Detect feature to output on a specified GPIO# */
	
	int32_t discfactor;
	int32_t discounterl;
	int32_t discounteru;
	int16_t amax;
	int16_t amin;
	int16_t apeak;
	
	int32_t cur_gpios;
	char *gpios[GPIO_PINCOUNT];
	char *pps[32];

	struct audiostatistics rxaudiostats;
	struct audiostatistics txaudiostats;

	int legacyaudioscaling;

	ast_mutex_t usblock;
};

/*!
 * \brief Default channel descriptor 
 */
static struct chan_simpleusb_pvt simpleusb_default = {
	.sounddev = -1,
	.duplex = M_FULL,
	.queuesize = QUEUE_SIZE,
	.frags = FRAGS,
	.readpos = 0,				/* start here on reads */
	.wanteeprom = 1,
	.usedtmf = 1,
	.rxondelay = 0,
	.txoffdelay = 0,
	.pager = PAGER_NONE,
	.clipledgpio = 0,
	.rxaudiostats.index = 0,
	/* After the vast majority of existing installs have had a chance to review their
	   audio settings and the associated old scaling/clipping hacks are no longer in
	   significant use the following cfg and all related code should be deleted. */
	.legacyaudioscaling = 1,
};

/*	DECLARE FUNCTION PROTOTYPES	*/

static int hidhdwconfig(struct chan_simpleusb_pvt *o);
static void mixer_write(struct chan_simpleusb_pvt *o);
static int setformat(struct chan_simpleusb_pvt *o, int mode);
static struct ast_channel *simpleusb_request(const char *type, struct ast_format_cap *cap,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static int simpleusb_digit_begin(struct ast_channel *c, char digit);
static int simpleusb_digit_end(struct ast_channel *c, char digit, unsigned int duration);
static int simpleusb_text(struct ast_channel *c, const char *text);
static int simpleusb_hangup(struct ast_channel *c);
static int simpleusb_answer(struct ast_channel *c);
static struct ast_frame *simpleusb_read(struct ast_channel *chan);
static int simpleusb_call(struct ast_channel *c, const char *dest, int timeout);
static int simpleusb_write(struct ast_channel *chan, struct ast_frame *f);
static int simpleusb_indicate(struct ast_channel *chan, int cond, const void *data, size_t datalen);
static int simpleusb_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int simpleusb_setoption(struct ast_channel *chan, int option, void *data, int datalen);
static void tune_menusupport(int fd, struct chan_simpleusb_pvt *o, const char *cmd);
static void tune_write(struct chan_simpleusb_pvt *o);
static int _send_tx_test_tone(int fd, struct chan_simpleusb_pvt *o, int ms, int intflag);

static char *simpleusb_active;	/* the active device */

static const int ppinshift[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 7, 5, 4, 0, 3 };

static const char tdesc[] = "Simple USB (CM108) Radio Channel Driver";

/*!
 * \brief Asterisk channel technology struct.
 * This tells Asterisk the functions to call when
 * it needs to interact with our module.
 */
static struct ast_channel_tech simpleusb_tech = {
	.type = "SimpleUSB",
	.description = tdesc,
	.requester = simpleusb_request,
	.send_digit_begin = simpleusb_digit_begin,
	.send_digit_end = simpleusb_digit_end,
	.send_text = simpleusb_text,
	.hangup = simpleusb_hangup,
	.answer = simpleusb_answer,
	.read = simpleusb_read,
	.call = simpleusb_call,
	.write = simpleusb_write,
	.indicate = simpleusb_indicate,
	.fixup = simpleusb_fixup,
	.setoption = simpleusb_setoption,
};

/*!
 * \brief FIR Low pass filter.
 * 2900 Hz passband with 0.5 db ripple, 6300 Hz stopband at 60db.
 * \param input		Audio value to filter.
 * \param z			Delay line.
 * \return 			Filtered value.
 * \todo	This filter needs more documentation.
 */
static short lpass(short input, short *z)
{
	register int i;
	register int accum;

	static short h[NTAPS] = { 103, 136, 148, 74, -113, -395, -694,
		-881, -801, -331, 573, 1836, 3265, 4589, 5525, 5864, 5525,
		4589, 3265, 1836, 573, -331, -801, -881, -694, -395, -113,
		74, 148, 136, 103
	};

	/* store input at the beginning of the delay line */
	z[0] = input;

	/* calc FIR and shift data */
	accum = h[NTAPS - 1] * z[NTAPS - 1];
	for (i = NTAPS - 2; i >= 0; i--) {
		accum += h[i] * z[i];
		z[i + 1] = z[i];
	}

	return (accum >> 15);
}

#define GAIN1   1.745882764e+00
/*!
 * \brief IIR High pass filter.
 * IIR 6 pole High pass filter, 300 Hz corner with 0.5 db ripple
 * \param input		Audio value to filter.
 * \param xv		Delay line.
 * \param yv		Delay line.
 * \return 			Filtered value.
 * \todo	This filter needs more documentation.
 */
static int16_t hpass6(int16_t input, float* restrict xv, float* restrict yv)
{
	xv[0] = xv[1];
	xv[1] = xv[2];
	xv[2] = xv[3];
	xv[3] = xv[4];
	xv[4] = xv[5];
	xv[5] = xv[6];
	xv[6] = ((float) input) / GAIN1;
	yv[0] = yv[1];
	yv[1] = yv[2];
	yv[2] = yv[3];
	yv[3] = yv[4];
	yv[4] = yv[5];
	yv[5] = yv[6];
	yv[6] = (xv[0] + xv[6]) - 6 * (xv[1] + xv[5]) + 15 * (xv[2] + xv[4])
		- 20 * xv[3]
		+ (-0.3491861578 * yv[0]) + (2.3932556573 * yv[1])
		+ (-6.9905126572 * yv[2]) + (11.0685981760 * yv[3])
		+ (-9.9896695552 * yv[4]) + (4.8664511065 * yv[5]);
	return ((int) yv[6]);
}

/*!
 * \brief Deemphasis filter.
 * Perform standard 6db/octave de-emphasis.
 * \param input		Audio value to filter.
 * \param state		State variable.
 * \return 			Filtered value.
 * \todo	This filter needs more documentation.
 */
static int16_t deemph(int16_t input, int32_t* restrict state)
{
	register int16_t coeff00 = 6878;
	register int16_t coeff01 = 25889;
	register int32_t accum;				/* 32 bit accumulator */

	accum = input;
	/* YES! The parenthesis REALLY do help on this one! */
	*state = accum + ((*state * coeff01) >> 15);
	accum = (*state * coeff00);
	/* adjust gain so that we have unity @ 1KHz */
	return ((accum >> 14) + (accum >> 15));
}

/*!
 * \brief Preemphasis filter.
 * Perform standard 6db/octave pre-emphasis.
 * \param input		Audio value to filter.
 * \param state		State variable.
 * \return 			Filtered value.
 * \todo	This filter needs more documentation.
 */
static int16_t preemph(int16_t input, int32_t* restrict state)
{
	int16_t coeff00 = 17610;
	int16_t coeff01 = -17610;
	int16_t adjval = 13404;
	register int32_t y, temp0, temp1;

	temp0 = *state * coeff01;
	*state = input;
	temp1 = input * coeff00;
	y = (temp0 + temp1) / adjval;
	if (y > 32767) {
		y = 32767;
	} else if (y < -32767) {
		y = -32767;
	}
	return (y);
}

/*!
 * \brief IIR High pass filter.
 * IIR 3 pole High pass filter, 300 Hz corner with 0.5 db ripple
 * \param input		Audio value to filter.
 * \param xv		Delay line.
 * \param yv		Delay line.
 * \return 			Filtered value.
 * \todo	This filter needs more documentation.
 */
static int16_t hpass(int16_t input, float* restrict xv, float* restrict yv)
{
#define GAIN   1.280673652e+00

	xv[0] = xv[1];
	xv[1] = xv[2];
	xv[2] = xv[3];
	xv[3] = ((float) input) / GAIN;
	yv[0] = yv[1];
	yv[1] = yv[2];
	yv[2] = yv[3];
	yv[3] = (xv[3] - xv[0]) + 3 * (xv[1] - xv[2])
		+ (0.5999763543 * yv[0]) + (-2.1305919790 * yv[1])
		+ (2.5161440793 * yv[2]);
	return ((int) yv[3]);
}

/*!
 * \brief Configure our private structure based on the
 * found hardware type.
 * \param o		Pointer chan_usbradio_pvt.
 * \returns 0	Always returns zero.
 */
static int hidhdwconfig(struct chan_simpleusb_pvt *o)
{
	int i;
	
/* NOTE: on the CM-108AH, GPIO2 is *not* a REAL GPIO.. it was re-purposed
 *  as a signal called "HOOK" which can only be read from the HID.
 *  Apparently, in a REAL CM-108, GPIO really works as a GPIO 
 */

	if (o->hdwtype == 1) {			//sphusb
		o->hid_gpio_ctl = 0x08;		/* set GPIO4 to output mode */
		o->hid_gpio_ctl_loc = 2;	/* For CTL of GPIO */
		o->hid_io_cor = 4;			/* GPIO3 is COR */
		o->hid_io_cor_loc = 1;		/* GPIO3 is COR */
		o->hid_io_ctcss = 2;		/* GPIO 2 is External CTCSS */
		o->hid_io_ctcss_loc = 1;	/* is GPIO 2 */
		o->hid_io_ptt = 8;			/* GPIO 4 is PTT */
		o->hid_gpio_loc = 1;		/* For ALL GPIO */
		o->valid_gpios = 1;			/* for GPIO 1 */
	} else if (o->hdwtype == 0) {	//dudeusb
		o->hid_gpio_ctl = 0x0c;		/* set GPIO 3 & 4 to output mode */
		o->hid_gpio_ctl_loc = 2;	/* For CTL of GPIO */
		o->hid_io_cor = 2;			/* VOLD DN is COR */
		o->hid_io_cor_loc = 0;		/* VOL DN COR */
		o->hid_io_ctcss = 1;		/* VOL UP External CTCSS */
		o->hid_io_ctcss_loc = 0;	/* VOL UP External CTCSS */
		o->hid_io_ptt = 4;			/* GPIO 3 is PTT */
		o->hid_gpio_loc = 1;		/* For ALL GPIO */
		o->valid_gpios = 0xfb;		/* for GPIO 1,2,4,5,6,7,8 (5,6,7,8 for CM-119 only) */
	} else if (o->hdwtype == 2) {	//NHRC (N1KDO) (dudeusb w/o user GPIO)
		o->hid_gpio_ctl = 4;		/* set GPIO 3 to output mode */
		o->hid_gpio_ctl_loc = 2;	/* For CTL of GPIO */
		o->hid_io_cor = 2;			/* VOLD DN is COR */
		o->hid_io_cor_loc = 0;		/* VOL DN COR */
		o->hid_io_ctcss = 1;		/* VOL UP is External CTCSS */
		o->hid_io_ctcss_loc = 0;	/* VOL UP CTCSS */
		o->hid_io_ptt = 4;			/* GPIO 3 is PTT */
		o->hid_gpio_loc = 1;		/* For ALL GPIO */
		o->valid_gpios = 0;			/* for GPIO 1,2,4 */
	} else if (o->hdwtype == 3) {	// custom version
		o->hid_gpio_ctl = 0x0c;		/* set GPIO 3 & 4 to output mode */
		o->hid_gpio_ctl_loc = 2;	/* For CTL of GPIO */
		o->hid_io_cor = 2;			/* VOLD DN is COR */
		o->hid_io_cor_loc = 0;		/* VOL DN COR */
		o->hid_io_ctcss = 2;		/* GPIO 2 is External CTCSS */
		o->hid_io_ctcss_loc = 1;	/* is GPIO 2 */
		o->hid_io_ptt = 4;			/* GPIO 3 is PTT */
		o->hid_gpio_loc = 1;		/* For ALL GPIO */
		o->valid_gpios = 1;			/* for GPIO 1 */
	}
	/* validate clipledgpio setting (Clip LED GPIO#) */
	if (o->clipledgpio) {
		if (o->clipledgpio >= GPIO_PINCOUNT || !(o->valid_gpios & (1 << (o->clipledgpio - 1)))) {
			ast_log(LOG_ERROR, "Channel %s: clipledgpio = GPIO%d not supported\n", o->name, o->clipledgpio);
			o->clipledgpio = 0;
		} else {
			o->hid_gpio_ctl |= 1 << (o->clipledgpio - 1); /* confirm Clip LED GPIO set to output mode */
		}
	}
	o->hid_gpio_val = 0;
	for (i = 0; i < GPIO_PINCOUNT; i++) {
		/* skip if this one not specified */
		if (!o->gpios[i]) {
			continue;
		}
		/* skip if not out */
		if (strncasecmp(o->gpios[i], "out", 3)) {
			continue;
		}
		/* skip if PTT */
		if ((1 << i) & o->hid_io_ptt) {
			ast_log(LOG_ERROR, "Channel %s: You can't specify gpio%d, since its the PTT.\n", o->name, i + 1);
			continue;
		}
		/* skip if not a valid GPIO */
		if (!(o->valid_gpios & (1 << i))) {
			ast_log(LOG_ERROR, "Channel %s: You can't specify gpio%d, it is not valid in this configuration.\n", o->name, i + 1);
			continue;
		}
		o->hid_gpio_ctl |= (1 << i);	/* set this one to output, also */
		/* if default value is 1, set it */
		if (!strcasecmp(o->gpios[i], "out1")) {
			o->hid_gpio_val |= (1 << i);
		}
	}
	if (o->invertptt) {
		o->hid_gpio_val |= o->hid_io_ptt;
	}
	return 0;
}

/*!
 * \brief Indicate that PTT is activate.
 *	This causes the hidthead to to exit from the loop timer and
 *	evaluate the gpio pins.
 * \param o		Pointer chan_usbradio_pvt.
 */
static void kickptt(const struct chan_simpleusb_pvt *o)
{
	char c = 0;
	int res;

	if (!o) {
		return;
	}
	if (o->pttkick[1] == -1) {
		return;
	}
	res = write(o->pttkick[1], &c, 1);
	if (res <= 0) {
		ast_log(LOG_ERROR, "Channel %s: Write failed: %s\n", o->name, strerror(errno));
	}
}

/*!
 * \brief Search our configured channels to find the
 *	one with the matching USB descriptor.
 *	Print a message if the descriptor was not found.
 * \param o		chan_usbradio_pvt.
 * \returns		Private structure that matches or NULL if not found.
 */
static struct chan_simpleusb_pvt *find_desc(const char *dev)
{
	struct chan_simpleusb_pvt *o = NULL;

	for (o = simpleusb_default.next; o && o->name && dev && strcmp(o->name, dev) != 0; o = o->next);
	if (!o) {
		ast_log(LOG_WARNING, "Cannot find USB descriptor <%s>.\n", dev ? dev : "-- Null Descriptor --");
		return NULL;
	}

	return o;
}

/*!
 * \brief Search our configured channels to find the
 *	one with the matching USB descriptor.
 * \param o		chan_usbradio_pvt.
 * \returns		Private structure that matches or NULL if not found.
 */
static struct chan_simpleusb_pvt *find_desc_usb(const char *devstr)
{
	struct chan_simpleusb_pvt *o = NULL;

	if (!devstr) {
		ast_log(LOG_WARNING, "USB Descriptor is null.\n");
	}

	for (o = simpleusb_default.next; o && devstr && strcmp(o->devstr, devstr) != 0; o = o->next);

	return o;
}

/*!
 * \brief Search installed devices for a match with
 *	one of our configured channels.
 * \returns		Matching device string, or NULL.
 */
static char *find_installed_usb_match(void)
{
	struct chan_simpleusb_pvt *o = NULL;
	char *match = NULL;
	
	for (o = simpleusb_default.next; o ; o = o->next) {
		if (ast_radio_usb_list_check(o->devstr)) {
			match = o->devstr;
			break;
		}
	}

	return match;
}

/*!
 * \brief Parallel port processing thread.
 *	This thread evaluates the timers configured for each
 *  configured parallel port pin.
 * \param arg	Arguments - this is always NULL.
 */
static void *pulserthread(void *arg)
{
	struct timeval now, then;
	register int i, j, k;

#ifdef HAVE_SYS_IO
	if (haspp == 2) {
		ioperm(pbase, 2, 1);
	}
#endif
	stoppulser = 0;
	pp_lastmask = 0;
	ast_mutex_lock(&pp_lock);
	ast_radio_ppwrite(haspp, ppfd, pbase, pport, pp_val);
	ast_mutex_unlock(&pp_lock);
	then = ast_radio_tvnow();
	
	while (!stoppulser) {
		usleep(50000);
		ast_mutex_lock(&pp_lock);
		now = ast_radio_tvnow();
		j = ast_tvdiff_ms(now, then);
		then = now;
		/* make output inversion mask (for pulseage) */
		pp_lastmask = pp_pulsemask;
		pp_pulsemask = 0;
		for (i = 2; i <= 9; i++) {
			k = pp_pulsetimer[i];
			if (k) {
				k -= j;
				if (k < 0) {
					k = 0;
				}
				pp_pulsetimer[i] = k;
			}
			if (k) {
				pp_pulsemask |= 1 << (i - 2);
			}
		}
		if (pp_pulsemask != pp_lastmask) {	/* if anything inverted (temporarily) */
			pp_val ^= pp_lastmask ^ pp_pulsemask;
			ast_radio_ppwrite(haspp, ppfd, pbase, pport, pp_val);
		}
		ast_mutex_unlock(&pp_lock);
	}
	pthread_exit(0);
}

/*!
 * \brief Load settings for a specific node
 * \param o
 * \param cfg If provided, will use the provided config. If NULL, cfg will be opened automatically.
 * \param reload 0 for first load, 1 for reload
 */
static int load_tune_config(struct chan_simpleusb_pvt *o, const struct ast_config *cfg, int reload)
{
	struct ast_variable *v;
	struct ast_config *cfg2;
	int opened = 0;
	int configured = 0;
	char devstr[sizeof(o->devstr)];

	o->rxmixerset = 500;
	o->txmixaset = 500;
	o->txmixbset = 500;

	devstr[0] = '\0';
	if (!reload) {
		o->devstr[0] = 0;
	}

	if (!cfg) {
		struct ast_flags zeroflag = { 0 };
		cfg2 = ast_config_load(CONFIG, zeroflag);
		if (!cfg2) {
			ast_log(LOG_WARNING, "Can't %sload settings for %s, using default parameters\n", reload ? "re": "", o->name);
			return -1;
		}
		opened = 1;
		cfg = cfg2;
	}

	for (v = ast_variable_browse(cfg, o->name); v; v = v->next) {
		configured = 1;
		CV_START(v->name, v->value);
		CV_UINT("rxmixerset", o->rxmixerset);
		CV_UINT("txmixaset", o->txmixaset);
		CV_UINT("txmixbset", o->txmixbset);
		CV_STR("devstr", devstr);
		CV_END;
	}
	if (!reload) {
		/* Using the ternary operator in CV_STR won't work, due to butchering the sizeof, so copy after if needed */
		strcpy(o->devstr, devstr); /* Safe */
	}
	if (opened) {
		ast_config_destroy(cfg2);
	}
	if (!configured) {
		ast_log(LOG_WARNING, "Can't %sload settings for %s (no section available), using default parameters\n", reload ? "re": "", o->name);
		return -1;
	}
	return 0;
}

/*!
 * \brief USB sound device GPIO processing thread
 * This thread is responsible for finding and associating the node with the
 * associated usb sound card device.  It performs setup and initialization of
 * the USB device.
 *
 * The CM-XXX USB devices can support up to 8 GPIO pins that can be input or output.
 * It continuously polls the input GPIO pins on the device to see if they have changed.
 * The default GPIOs for COS, and CTCSS provide the basic functionality. An asterisk
 * text frame is raised in the format 'GPIO%d %d' when GPIOs change. Polling generally
 * occurs every HID_POLL_RATE milliseconds.
 *
 * The output PTT (push to talk) GPIO, along with other GPIO outputs are updated as
 * required.
 *
 * If the user has enabled the parallel port for GPIOs, they are polled and updated
 * as appropriate.  An asterisk text frame is raised in the format 'PP%d %d' when
 * GPIOs change. (Parallel port support is not available for all platforms.)
 *
 * This routine also reads and writes to the EPROM attached to the USB device.  The
 * EPROM holds the configuration information (sound level settings) for this device.
 *
 * This routine updates the lasthidtimer during setup and processing.  In the event
 * that this timer update does not occur over a period of 3 seconds, app_rpt will
 * kill the node and restart everything.  This helps to detect problems with a
 * hung USB device.
 *
 * \param argv		chan_simpleusb_pvt structure associated with this thread.
 */
static void *hidthread(void *arg)
{
	unsigned char buf[4], bufsave[4], keyed, ctcssed, txreq;
	char *s, lasttxtmp;
	register int i, j, k; 
	int res;
	struct usb_device *usb_dev;
	struct usb_dev_handle *usb_handle;
	struct chan_simpleusb_pvt *o = arg, *ao;
	struct timeval then;
	struct pollfd rfds[1];

	usb_dev = NULL;
	usb_handle = NULL;
	/* enable gpio_set so that we will write GPIO information upon start up */
	o->gpio_set = 1;
	
#ifdef HAVE_SYS_IO
	if (haspp == 2) {
		ioperm(pbase, 2, 1);
	}
#endif
	/* This is the main loop for this thread.
	 * It performs setup and initialization of the usb device.
	 * After setup is complete and the device can be accessed,
	 * it enters a processing loop responsible for interacting 
	 * with the usb hid device
	 */
	while (!o->stophid) {
		ast_radio_time(&o->lasthidtime);
		ast_mutex_lock(&usb_dev_lock);
		o->hasusb = 0;
		o->usbass = 0;
		o->devicenum = 0;
		if (usb_handle) {
			usb_close(usb_handle);
		}
		usb_handle = NULL;
		usb_dev = NULL;
		ast_radio_hid_device_mklist();
		
		/* Check to see if our specified device string 
		 * matches to a device that is attached to this system, or exists 
		 * in our channel configuration.
		 *
		 * If no device string is specified, attempt to assign the first
		 * found device.
		 */
		ast_radio_time(&o->lasthidtime);
		
		/* Automatically assign a devstr if one was not specified in the configuration. */
		if (ast_strlen_zero(o->devstr)) {
			int index = 0;
			char *index_devstr = NULL;
			
			for (;;) {
				index_devstr = ast_radio_usb_get_devstr(index);
				if (ast_strlen_zero(index_devstr)) {
					if (!o->device_error) {
						ast_log(LOG_ERROR, "Channel %s: No USB devices are available for assignment.\n",  o->name);
						o->device_error = 1;
					}
					ast_mutex_unlock(&usb_dev_lock);
					usleep(500000);
					break;
				}
				/* We found an available device - see if it already in use */
				for (ao = simpleusb_default.next; ao && ao->name; ao = ao->next) {
					if (ao->usbass && (!strcmp(ao->devstr, index_devstr))) {
						break;
					}
				}
				if (ao) {
					index++;
					continue;
				}
				/* We found an unused device assign it to our node */
				ast_copy_string(o->devstr, index_devstr, sizeof(o->devstr));
				ast_log(LOG_NOTICE, "Channel %s: Automatically assigned USB device %s to SimpleUSB channel\n", o->name, o->devstr);
				break;
			}
			if (ast_strlen_zero(o->devstr)) {
				continue;
			}
		}
		
		if ((!ast_radio_usb_list_check(o->devstr)) || (!find_desc_usb(o->devstr))) {
			/* The device string did not match.
			 * Now look through the attached devices and see 
			 * one of those is associated with one of our
			 * configured channels.
			 */
			s = find_installed_usb_match();
			if (ast_strlen_zero(s)) {
				if (!o->device_error) {
					ast_log(LOG_ERROR, "Channel %s: Device string %s was not found.\n",  o->name, o->devstr);
					o->device_error = 1;
				}
				ast_mutex_unlock(&usb_dev_lock);
				usleep(500000);
				continue;
			}
			i = ast_radio_usb_get_usbdev(s);
			if (i < 0) {
				ast_mutex_unlock(&usb_dev_lock);
				usleep(500000);
				continue;
			}
			/* See if this device is already assigned to another usb channel */
			for (ao = simpleusb_default.next; ao && ao->name; ao = ao->next) {
				if (ao->usbass && (!strcmp(ao->devstr, s))) {
					break;
				}
			}
			if (ao) {
				ast_log(LOG_ERROR, "Channel %s: Device string %s is already assigned to channel %s", o->name, s, ao->name);
				ast_mutex_unlock(&usb_dev_lock);
				usleep(500000);
				continue;
			}
			ast_log(LOG_NOTICE, "Channel %s: Assigned USB device %s to simpleusb channel\n", o->name, s);
			ast_copy_string(o->devstr, s, sizeof(o->devstr));
		}
		/* Double check to see if the device string is assigned to another usb channel */
		for (ao = simpleusb_default.next; ao && ao->name; ao = ao->next) {
			if (ao->usbass && (!strcmp(ao->devstr, o->devstr)))
				break;
		}
		if (ao) {
			ast_log(LOG_ERROR, "Channel %s: Device string %s is already assigned to channel %s", o->name, o->devstr, ao->name);
			ast_mutex_unlock(&usb_dev_lock);
			usleep(500000);
			continue;
		}
		/* get the index to the device and assign it to our channel */
		i = ast_radio_usb_get_usbdev(o->devstr);
		if (i < 0) {
			ast_mutex_unlock(&usb_dev_lock);
			usleep(500000);
			continue;
		}
		o->devicenum = i;
		o->device_error = 0;
		ast_radio_time(&o->lasthidtime);
		o->usbass = 1;
		ast_mutex_unlock(&usb_dev_lock);
		/* set the audio mixer values */
		o->micmax = ast_radio_amixer_max(o->devicenum, MIXER_PARAM_MIC_CAPTURE_VOL);
		o->spkrmax = ast_radio_amixer_max(o->devicenum, MIXER_PARAM_SPKR_PLAYBACK_VOL);
		o->micplaymax = ast_radio_amixer_max(o->devicenum, MIXER_PARAM_MIC_PLAYBACK_VOL);
		if (o->spkrmax == -1) {
			o->newname = 1;
			o->spkrmax = ast_radio_amixer_max(o->devicenum, MIXER_PARAM_SPKR_PLAYBACK_VOL_NEW);
		}
		/* initialize the usb device */
		usb_dev = ast_radio_hid_device_init(o->devstr);
		if (usb_dev == NULL) {
			ast_log(LOG_ERROR, "Channel %s: Cannot initialize device %s\n", o->name, o->devstr);
			usleep(500000);
			continue;
		}
		/* open the usb device device */
		usb_handle = usb_open(usb_dev);
		if (usb_handle == NULL) {
			ast_log(LOG_ERROR, "Channel %s: Cannot open device %s\n", o->name, o->devstr);
			usleep(500000);
			continue;
		}
		/* attempt to claim the usb hid interface and detach from the kernel */
		if (usb_claim_interface(usb_handle, C108_HID_INTERFACE) < 0) {
			if (usb_detach_kernel_driver_np(usb_handle, C108_HID_INTERFACE) < 0) {
				ast_log(LOG_ERROR, "Channel %s: Is not able to detach the USB device\n", o->name);
				usleep(500000);
				continue;
			}
			if (usb_claim_interface(usb_handle, C108_HID_INTERFACE) < 0) {
				ast_log(LOG_ERROR, "Channel %s: Is not able to claim the USB device\n", o->name);
				usleep(500000);
				continue;
			}
		}
		/* write initial value to GPIO */
		memset(buf, 0, sizeof(buf));
		buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
		buf[o->hid_gpio_loc] = o->hid_gpio_val;
		ast_radio_hid_set_outputs(usb_handle, buf);
		memcpy(bufsave, buf, sizeof(buf));
		/* setup the pttkick pipe 
		 * this pipe is used for timing the main processing loop
		 * it also signaled when the ptt changes to exit the timer
		 */
		if (o->pttkick[0] != -1) {
			close(o->pttkick[0]);
			o->pttkick[0] = -1;
		}
		if (o->pttkick[1] != -1) {
			close(o->pttkick[1]);
			o->pttkick[1] = -1;
		}
		if (pipe(o->pttkick) == -1) {
			ast_log(LOG_ERROR, "Channel %s: Is not able to create a pipe\n", o->name);
			pthread_exit(NULL);
		}
		if ((usb_dev->descriptor.idProduct & 0xfffc) == C108_PRODUCT_ID) {
			o->devtype = C108_PRODUCT_ID;
		} else {
			o->devtype = usb_dev->descriptor.idProduct;
		}
		ast_debug(5, "Channel %s: Starting normally.\n", o->name);
		ast_debug(5, "Channel %s: Attached to usb device %s.\n", o->name, o->devstr);

		mixer_write(o);
		load_tune_config(o, NULL, 1);
		mixer_write(o);

		ast_mutex_lock(&o->eepromlock);
		if (o->wanteeprom) {
			o->eepromctl = 1;
		}
		ast_mutex_unlock(&o->eepromlock);
		
		setformat(o, O_RDWR);
		o->hasusb = 1;
		o->had_gpios_in = 0;
		
		memset(&rfds, 0, sizeof(rfds));
		rfds[0].fd = o->pttkick[1];
		rfds[0].events = POLLIN;
		
		ast_radio_time(&o->lasthidtime);
		/* Main processing loop for GPIO
		 * This loop process every HID_POLL_RATE milliseconds.
		 * The timer can be interrupted by writing to
		 * the pttkick pipe.
		 */
		while ((!o->stophid) && o->hasusb) {
			
			then = ast_radio_tvnow();
			/* poll the pttkick pipe - timeout after HID_POLL_RATE milliseconds */
			res = ast_poll(rfds, 1, HID_POLL_RATE);
			if (res < 0) {
				ast_log(LOG_WARNING, "Channel %s: Poll failed: %s\n", o->name, strerror(errno));
				usleep(10000);
				continue;
			}
			if (rfds[0].revents) {
				char c;

				int bytes = read(o->pttkick[0], &c, 1);
				if (bytes <= 0) {
					ast_log(LOG_ERROR, "Channel %s: pttkick read failed: %s\n", o->name, strerror(errno));
				}
			}
			/* see if we need to process an eeprom read or write */
			if (o->wanteeprom) {
				ast_mutex_lock(&o->eepromlock);
				if (o->eepromctl == 1) {	/* to read */
					/* if CS okay */
					if (!ast_radio_get_eeprom(usb_handle, o->eeprom)) {
						if (o->eeprom[EEPROM_USER_MAGIC_ADDR] != EEPROM_MAGIC) {
							ast_log(LOG_ERROR, "Channel %s: EEPROM bad magic number\n", o->name);
						} else {
							o->rxmixerset = o->eeprom[EEPROM_USER_RXMIXERSET];
							o->txmixaset = o->eeprom[EEPROM_USER_TXMIXASET];
							o->txmixbset = o->eeprom[EEPROM_USER_TXMIXBSET];
							ast_log(LOG_NOTICE, "Channel %s: EEPROM Loaded\n", o->name);
							mixer_write(o);
						}
					} else {
						ast_log(LOG_ERROR, "Channel %s: USB adapter has no EEPROM installed or Checksum is bad\n", o->name);
					}
					ast_radio_hid_set_outputs(usb_handle, bufsave);
				}
				if (o->eepromctl == 2) {	/* to write */
					ast_radio_put_eeprom(usb_handle, o->eeprom);
					ast_radio_hid_set_outputs(usb_handle, bufsave);
					ast_log(LOG_NOTICE, "Channel %s: USB parameters written to EEPROM\n", o->name);
				}
				o->eepromctl = 0;
				ast_mutex_unlock(&o->eepromlock);
			}
			ast_mutex_lock(&o->usblock);
			buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
			ast_radio_hid_get_inputs(usb_handle, buf);
			/* See if we are keyed */
			keyed = !(buf[o->hid_io_cor_loc] & o->hid_io_cor);
			if (keyed != o->rxhidsq) {
				ast_debug(2, "Channel %s: Update rxhidsq = %d\n", o->name, keyed);
				o->rxhidsq = keyed;
			}
			/* See if we are receiving ctcss */
			ctcssed = !(buf[o->hid_io_ctcss_loc] & o->hid_io_ctcss);
			if (ctcssed != o->rxhidctcss) {
				ast_debug(2, "Channel %s: Update rxhidctcss = %d\n", o->name, ctcssed);
				o->rxhidctcss = ctcssed;
			}
			ast_mutex_lock(&o->txqlock);
			txreq = !(AST_LIST_EMPTY(&o->txq));
			ast_mutex_unlock(&o->txqlock);
			txreq = txreq || o->txkeyed || o->txtestkey || o->echoing;
			if (txreq && (!o->lasttx)) {
				o->hid_gpio_val |= o->hid_io_ptt;
				if (o->invertptt) {
					o->hid_gpio_val &= ~o->hid_io_ptt;
				}
				buf[o->hid_gpio_loc] = o->hid_gpio_val;
				buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
				ast_radio_hid_set_outputs(usb_handle, buf);
				ast_debug(2, "Channel %s: update PTT = %d on channel.\n", o->name, txreq);
			} else if ((!txreq) && o->lasttx) {
				o->hid_gpio_val &= ~o->hid_io_ptt;
				if (o->invertptt) {
					o->hid_gpio_val |= o->hid_io_ptt;
				}
				buf[o->hid_gpio_loc] = o->hid_gpio_val;
				buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
				ast_radio_hid_set_outputs(usb_handle, buf);
				ast_debug(2, "Channel %s: update PTT = %d.\n", o->name, txreq);
			}
			lasttxtmp = o->lasttx;
			o->lasttx = txreq;
			ast_radio_time(&o->lasthidtime);
			/* Get the GPIO information */
			j = buf[o->hid_gpio_loc];
			/* If this device is a CM108AH, map the "HOOK" bit (which used to
			   be GPIO2 in the CM108 into the GPIO position */
			if (o->devtype == C108AH_PRODUCT_ID) {
				j |= 2;			/* set GPIO2 bit */
				/* if HOOK is asserted, clear GPIO bit */
				if (buf[o->hid_io_cor_loc] & 0x10) {
					j &= ~2;
				}
			}
			for (i = 0; i < GPIO_PINCOUNT; i++) {
				/* if a valid input bit, dont clear it */
				if ((o->gpios[i]) && (!strcasecmp(o->gpios[i], "in")) && (o->valid_gpios & (1 << i))) {
					continue;
				}
				j &= ~(1 << i);	/* clear the bit, since its not an input */
			}
			if ((!o->had_gpios_in) || (o->last_gpios_in != j)) {
				char buf1[100];
				struct ast_frame fr = {
					.frametype = AST_FRAME_TEXT,
					.src = __PRETTY_FUNCTION__,
				};

				for (i = 0; i < GPIO_PINCOUNT; i++) {
					/* skip if not specified */
					if (!o->gpios[i]) {
						continue;
					}
					/* skip if not input */
					if (strcasecmp(o->gpios[i], "in")) {
						continue;
					}
					/* skip if not a valid GPIO */
					if (!(o->valid_gpios & (1 << i))) {
						continue;
					}
					/* if bit has changed, or never reported */
					if ((!o->had_gpios_in) || ((o->last_gpios_in & (1 << i)) != (j & (1 << i)))) {
						sprintf(buf1, "GPIO%d %d\n", i + 1, (j & (1 << i)) ? 1 : 0);
						fr.data.ptr = buf1;
						fr.datalen = strlen(buf1);
						ast_queue_frame(o->owner, &fr);
					}
				}
				o->had_gpios_in = 1;
				o->last_gpios_in = j;
			}
			/* process the parallel port GPIO */
			if (haspp) {
				ast_mutex_lock(&pp_lock);
				j = k = ast_radio_ppread(haspp, ppfd, pbase, pport) ^ 0x80;	/* get PP input */
				ast_mutex_unlock(&pp_lock);
				for (i = 10; i <= 15; i++) {
					/* if a valid input bit, dont clear it */
					if ((o->pps[i]) && (!strcasecmp(o->pps[i], "in")) && (PP_MASK & (1 << i))) {
						continue;
					}
					j &= ~(1 << ppinshift[i]);	/* clear the bit, since its not an input */
				}
				if ((!o->had_pp_in) || (o->last_pp_in != j)) {
					char buf1[100];
					struct ast_frame fr = {
						.frametype = AST_FRAME_TEXT,
						.src = __PRETTY_FUNCTION__,
					};

					for (i = 10; i <= 15; i++) {
						/* skip if not specified */
						if (!o->pps[i]) {
							continue;
						}
						/* skip if not input */
						if (strcasecmp(o->pps[i], "in")) {
							continue;
						}
						/* skip if not valid */
						if (!(PP_MASK & (1 << i))) {
							continue;
						}
						/* if bit has changed, or never reported */
						if ((!o->had_pp_in) || ((o->last_pp_in & (1 << ppinshift[i])) != (j & (1 << ppinshift[i])))) {
							sprintf(buf1, "PP%d %d\n", i, (j & (1 << ppinshift[i])) ? 1 : 0);
							fr.data.ptr = buf1;
							fr.datalen = strlen(buf1);
							ast_queue_frame(o->owner, &fr);
						}
					}
					o->had_pp_in = 1;
					o->last_pp_in = j;
				}
				o->rxppsq = o->rxppctcss = 0;
				for (i = 10; i <= 15; i++) {
					if ((o->pps[i]) && (!strcasecmp(o->pps[i], "cor")) && (PP_MASK & (1 << i))) {
						j = k & (1 << ppinshift[i]);	/* set the bit accordingly */
						if (j != o->rxppsq) {
							ast_debug(2, "Channel %s: update rxppsq = %d\n", o->name, j);
							o->rxppsq = j;
						}
					} else if ((o->pps[i]) && (!strcasecmp(o->pps[i], "ctcss")) && (PP_MASK & (1 << i))) {
						o->rxppctcss = k & (1 << ppinshift[i]);	/* set the bit accordingly */
					}
				}
			}
			j = ast_tvdiff_ms(ast_radio_tvnow(), then);
			/* make output inversion mask (for pulseage) */
			o->hid_gpio_lastmask = o->hid_gpio_pulsemask;
			o->hid_gpio_pulsemask = 0;
			for (i = 0; i < GPIO_PINCOUNT; i++) {
				k = o->hid_gpio_pulsetimer[i];
				if (k) {
					k -= j;
					if (k < 0) {
						k = 0;
					}
					o->hid_gpio_pulsetimer[i] = k;
				}
				if (k) {
					o->hid_gpio_pulsemask |= 1 << i;
				}
			}
			if (o->hid_gpio_pulsemask || o->hid_gpio_lastmask) {	/* if anything inverted (temporarily) */
				buf[o->hid_gpio_loc] = o->hid_gpio_val ^ o->hid_gpio_pulsemask;
				buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
				ast_radio_hid_set_outputs(usb_handle, buf);
			}
			if (o->gpio_set) {
				o->gpio_set = 0;
				buf[o->hid_gpio_loc] = o->hid_gpio_val ^ o->hid_gpio_pulsemask;
				buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
				ast_radio_hid_set_outputs(usb_handle, buf);
			}
			k = 0;
			if (haspp) {
				for (i = 2; i <= 9; i++) {
					/* skip if this one not specified */
					if (!o->pps[i]) {
						continue;
					}
					/* skip if not ptt */
					if (strncasecmp(o->pps[i], "ptt", 3)) {
						continue;
					}
					k |= (1 << (i - 2));	/* make mask */
				}
			}
			if (o->lasttx != lasttxtmp) {
				ast_debug(2, "Channel %s: tx set to %d\n", o->name, o->lasttx);
				o->hid_gpio_val &= ~o->hid_io_ptt;
				ast_mutex_lock(&pp_lock);
				if (k) {
					pp_val &= ~k;
				}
				if (!o->invertptt) {
					if (o->lasttx) {
						o->hid_gpio_val |= o->hid_io_ptt;
						if (k) {
							pp_val |= k;
						}
					}
				} else {
					if (!o->lasttx) {
						o->hid_gpio_val |= o->hid_io_ptt;
						if (k) {
							pp_val |= k;
						}
					}
				}
				if (k) {
					ast_radio_ppwrite(haspp, ppfd, pbase, pport, pp_val);
				}
				ast_mutex_unlock(&pp_lock);
				buf[o->hid_gpio_loc] = o->hid_gpio_val ^ o->hid_gpio_pulsemask;
				buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
				memcpy(bufsave, buf, sizeof(buf));
				ast_radio_hid_set_outputs(usb_handle, buf);
			}
			ast_radio_time(&o->lasthidtime);
			ast_mutex_unlock(&o->usblock);
		}
		o->lasttx = 0;
		ast_mutex_lock(&o->usblock);
		o->hid_gpio_val &= ~o->hid_io_ptt;
		if (o->invertptt) {
			o->hid_gpio_val |= o->hid_io_ptt;
		}
		buf[o->hid_gpio_loc] = o->hid_gpio_val;
		buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
		ast_radio_hid_set_outputs(usb_handle, buf);
		ast_mutex_unlock(&o->usblock);
	}
	/* clean up before exiting the thread */
	o->lasttx = 0;
	if (usb_handle) {
		ast_mutex_lock(&o->usblock);
		o->hid_gpio_val &= ~o->hid_io_ptt;
		if (o->invertptt) {
			o->hid_gpio_val |= o->hid_io_ptt;
		}
		buf[o->hid_gpio_loc] = o->hid_gpio_val;
		buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
		ast_radio_hid_set_outputs(usb_handle, buf);
		ast_mutex_unlock(&o->usblock);
	}
	pthread_exit(0);
}

/*!
 * \brief Get the number of blocks used in the audio output channel.
 * \param o		Pointer chan_usbradio_pvt.
 * \returns		Number of blocks that have been used.
 */
static int used_blocks(struct chan_simpleusb_pvt *o)
{
	struct audio_buf_info info;

	if (ioctl(o->sounddev, SNDCTL_DSP_GETOSPACE, &info)) {
		if (!(o->warned & WARN_used_blocks)) {
			ast_log(LOG_WARNING, "Channel %s: Error reading output space.\n", o->name);
			o->warned |= WARN_used_blocks;
		}
		return 1;
	}

	/* Set the total blocks */
	if (o->total_blocks == 0) {
		ast_debug(1, "Channel %s: fragment total %d, size %d, available %d, bytes %d\n", 
			o->name, info.fragstotal, info.fragsize, info.fragments, info.bytes);
		o->total_blocks = info.fragments;
		/* Check the queue size, it cannot exceed the total fragments */
		if (o->queuesize >= info.fragstotal) {
			o->queuesize = info.fragstotal - 1;
			if (o->queuesize < 2) {
				o->queuesize = QUEUE_SIZE;
			}
			ast_debug(1, "Channel %s: Queue size reset to %d\n", o->name, o->queuesize);
		}
	}

	return o->total_blocks - info.fragments;
}

/*!
 * \brief Write a full frame of audio data to the sound card device.
 * \note The input data must be formatted as stereo at 48000 samples per second.
 *		 FRAME_SIZE * 2 * 2 * 6 (2 bytes per sample, 2 channels, 6 for upsample to 48K)
 * \param o		Pointer chan_usbradio_pvt.
 * \param data	Audio data to write.
 * \returns		Number bytes written.
 */
static int soundcard_writeframe(struct chan_simpleusb_pvt *o, short *data)
{
	int res;

	/* If the sound device is not open, setformat will open the device */
	if (o->sounddev < 0) {
		setformat(o, O_RDWR);
	}
	if (o->sounddev < 0) {
		return 0;				/* not fatal */
	}
	/*
	 * Nothing complex to manage the audio device queue.
	 * If the buffer is full just drop the extra, otherwise write.
	 * In some cases it might be useful to write anyways after
	 * a number of failures, to restart the output chain.
	 */
	res = used_blocks(o);
	if (res > o->queuesize) {	/* no room to write a block */
		ast_log(LOG_WARNING, "Channel %s: Sound device write buffer overflow - used %d blocks\n",
			o->name, res);
		return 0;
	}

	res = write(o->sounddev, ((void *) data), FRAME_SIZE * 2 * 2 * 6);
	if (res < 0) {
		ast_log(LOG_ERROR, "Channel %s: Sound card write error %s\n", o->name, strerror(errno));
	} else if (res != FRAME_SIZE * 2 * 2 * 6) {
		ast_log(LOG_ERROR, "Channel %s: Sound card wrote %d bytes of %d\n", 
			o->name, res, (FRAME_SIZE * 2 * 2 * 6));
	}

	/* Check Tx audio statistics. FRAME_SIZE define refers to 8Ksps mono which is 160 samples
	 * per 20mS USB frame. ast_radio_check_audio() takes the write buffer (48K stereo),
	 * extracts the mono 48K channel, checks amplitude and distortion characteristics,
	 * and returns true if clipping was detected. If local Tx audio is clipped it might be
	 * nice to log a warning but as this does not relate to outgoing network audio it's not
	 * a major issue. User can check the Tx Audio Stats utility if desired.
	 */
	ast_radio_check_audio(data, &o->txaudiostats, 12 * FRAME_SIZE);

	return res;
}

/*!
 * \brief Open the sound card device.
 * If the device is already open, this will close the device
 * and open it again.
 * It initializes the device based on our requirements and triggers
 * reads and writes.
 * \param o		chan_usbradio_pvt.
 * \param mode	The mode to open the file.  This is the flags argument to open.
 * \retval 0	Success.
 * \retval -1	Failed.
 */
static int setformat(struct chan_simpleusb_pvt *o, int mode)
{
	int fmt, desired, res, fd;
	char device[100];

	/* If the device is open, close it */
	if (o->sounddev >= 0) {
		ioctl(o->sounddev, SNDCTL_DSP_RESET, 0);
		close(o->sounddev);
		o->duplex = M_UNSET;
		o->sounddev = -1;
	}
	if (mode == O_CLOSE)		/* we are done */
		return 0;
		
	strcpy(device, "/dev/dsp");
	if (o->devicenum) {
		sprintf(device, "/dev/dsp%d", o->devicenum);
	}
	/* open the device */
	fd = o->sounddev = open(device, mode | O_NONBLOCK);
	if (fd < 0) {
		ast_log(LOG_ERROR, "Channel %s: Unable to open DSP device %d: %s.\n", o->name, o->devicenum, strerror(errno));
		return -1;
	}
	if (o->owner) {
		ast_channel_internal_fd_set(o->owner, 0, fd);
	}

#if __BYTE_ORDER == __LITTLE_ENDIAN
	fmt = AFMT_S16_LE;
#else
	fmt = AFMT_S16_BE;
#endif
	res = ioctl(fd, SNDCTL_DSP_SETFMT, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Channel %s: Unable to set format to 16-bit signed\n", o->name);
		return -1;
	}
	/* set our duplex mode based on the way we opened the device. */
	switch (mode) {
	case O_RDWR:
		res = ioctl(fd, SNDCTL_DSP_SETDUPLEX, 0);
		/* Check to see if duplex set (FreeBSD Bug) */
		res = ioctl(fd, SNDCTL_DSP_GETCAPS, &fmt);
		if (res == 0 && (fmt & DSP_CAP_DUPLEX)) {
			o->duplex = M_FULL;
		};
		break;
	case O_WRONLY:
		o->duplex = M_WRITE;
		break;
	case O_RDONLY:
		o->duplex = M_READ;
		break;
	}

	fmt = 1;
	res = ioctl(fd, SNDCTL_DSP_STEREO, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Channel %s: Failed to set audio device to stereo\n", o->name);
		return -1;
	}
	fmt = desired = 48000;		/* 48000 Hz desired */
	res = ioctl(fd, SNDCTL_DSP_SPEED, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Channel %s: Failed to set audio device sample rate.\n", o->name);
		return -1;
	}
	if (fmt != desired) {
		if (!(o->warned & WARN_speed)) {
			ast_log(LOG_WARNING, "Channel %s: Requested %d Hz, got %d Hz -- sound may be choppy.\n", o->name, desired, fmt);
			o->warned |= WARN_speed;
		}
	}
	/*
	 * on Freebsd, SETFRAGMENT does not work very well on some cards.
	 * Default to use 256 bytes, let the user override
	 */
	if (o->frags) {
		fmt = o->frags;
		res = ioctl(fd, SNDCTL_DSP_SETFRAGMENT, &fmt);
		if (res < 0) {
			if (!(o->warned & WARN_frag)) {
				ast_log(LOG_WARNING, "Channel %s: Unable to set fragment size -- sound may be choppy.\n", o->name);
				o->warned |= WARN_frag;
			}
		}
	}
	/* on some cards, we need SNDCTL_DSP_SETTRIGGER to start outputting */
	res = PCM_ENABLE_INPUT | PCM_ENABLE_OUTPUT;
	res = ioctl(fd, SNDCTL_DSP_SETTRIGGER, &res);
	/* it may fail if we are in half duplex, never mind */
	return 0;
}

/*!
 * \brief Asterisk digit begin function.
 * \param c				Asterisk channel.
 * \param digit			Digit processed.
 * \retval 0			
 */
static int simpleusb_digit_begin(struct ast_channel *c, char digit)
{
	return 0;
}

/*!
 * \brief Asterisk digit end function.
 * \param c				Asterisk channel.
 * \param digit			Digit processed.
 * \param duration		Duration of the digit.
 * \retval -1			
 */
static int simpleusb_digit_end(struct ast_channel *c, char digit, unsigned int duration)
{
	/* no better use for received digits than print them */
	ast_verbose(" << Console Received digit %c of duration %u ms >> \n", digit, duration);
	return 0;
}

/*!
 * \brief Make paging audio samples.
 * \param audio			Audio buffer.
 * \param data			Data to encode into audio.
 * \param audio_ptr		Audio buffer pointer.
 * \param divcnt		The running count of the number of samples encoded per bit.
 *						This tracks our samples as we create the wave form.
 * \param divdiv		The number of samples to encode per bit.
 */
static void mkpsamples(short* restrict audio, uint32_t data, int* restrict audio_ptr, int* restrict divcnt, int divdiv)
{
	register int i;
	register short value;

	for (i = 31; i >= 0; i--) {
		value = (data & (1 << i)) ? ONEVAL : ZEROVAL;
		while (*divcnt < divdiv) {
			audio[(*audio_ptr)++] = value;
			*divcnt += DIVSAMP;
		}
		if (*divcnt >= divdiv) {
			*divcnt -= divdiv;
		}
	}
}

/*!
 * \brief Asterisk text function.
 * \param c				Asterisk channel.
 * \param text			Text message to process.
 * \retval 0			If successful.
 * \retval -1			If unsuccessful.
 */
static int simpleusb_text(struct ast_channel *c, const char *text)
{
	struct chan_simpleusb_pvt *o = ast_channel_tech_pvt(c);
	char *cmd;
	int cnt, i, j, audio_samples, divcnt, divdiv, audio_ptr, baud;
	struct pocsag_batch *batch, *b;
	short *audio;
	char audio1[AST_FRIENDLY_OFFSET + (FRAME_SIZE * sizeof(short))];
	struct ast_frame *f1;

#ifdef HAVE_SYS_IO
	if (haspp == 2) {
		ioperm(pbase, 2, 1);
	}
#endif

	cmd = ast_alloca(strlen(text) + 10);

	/* print received messages */
	ast_debug(3, "Channel %s: Console Received usbradio text %s >> \n", o->name, text);

	/* set receive CTCSS */
	if (!strncmp(text, "RXCTCSS", 7)) {
		cnt = sscanf(text, "%s " N_FMT(d), cmd, &i);
		if (cnt < 2) {
			return 0;
		}
		o->rxctcssoverride = !i;
		ast_debug(3, "Channel %s: RXCTCSS cmd: %s\n", o->name, text);
		return 0;
	}

	/* GPIO command */
	if (!strncmp(text, "GPIO", 4)) {
		cnt = sscanf(text, "%s " N_FMT(d) " " N_FMT(d), cmd, &i, &j);
		if (cnt < 3) {
			return 0;
		}
		if ((i < 1) || (i > GPIO_PINCOUNT)) {
			return 0;
		}
		i--;
		/* skip if not valid */
		if (!(o->valid_gpios & (1 << i))) {
			return 0;
		}
		ast_mutex_lock(&o->usblock);
		if (j > 1) {			/* if to request pulse-age */
			o->hid_gpio_pulsetimer[i] = j - 1;
		} else {
			/* clear pulsetimer, if in the middle of running */
			o->hid_gpio_pulsetimer[i] = 0;
			o->hid_gpio_val &= ~(1 << i);
			if (j) {
				o->hid_gpio_val |= 1 << i;
			}
			o->gpio_set = 1;
		}
		ast_mutex_unlock(&o->usblock);
		kickptt(o);
		return 0;
	}

	/* Parallel port command */
	if (!strncmp(text, "PP", 2)) {
		cnt = sscanf(text, "%s " N_FMT(d) " " N_FMT(d), cmd, &i, &j);
		if (cnt < 3) {
			return 0;
		}
		if ((i < 2) || (i > 9)) {
			return 0;
		}
		/* skip if not valid */
		if (!(PP_MASK & (1 << i))) {
			return 0;
		}
		ast_mutex_lock(&pp_lock);
		if (j > 1) {			/* if to request pulse-age */
			pp_pulsetimer[i] = j - 1;
		} else {
			/* clear pulsetimer, if in the middle of running */
			pp_pulsetimer[i] = 0;
			pp_val &= ~(1 << (i - 2));
			if (j) {
				pp_val |= 1 << (i - 2);
			}
			ast_radio_ppwrite(haspp, ppfd, pbase, pport, pp_val);
		}
		ast_mutex_unlock(&pp_lock);
		return 0;
	}

	/* pager command */
	if (!strncmp(text, "PAGE", 4)) {
		cnt = sscanf(text, "%s " N_FMT(d) " " N_FMT(d) " %n", cmd, &baud, &i, &j);
		if (cnt < 3) {
			return 0;
		}
		if (strlen(text + j) < 1) {
			return 0;
		}
		switch (text[j]) {
		case 'T':				/* Tone only */
			ast_verb(3, "Channel %s: POCSAG page (%d baud, capcode=%d) TONE ONLY\n", o->name, baud, i);
			batch = make_pocsag_batch(i, NULL, 0, TONE, 0);
			break;
		case 'N':				/* Numeric */
			if (!text[j + 1]) {
				return 0;
			}
			ast_verb(3, "Channel %s: POCSAG page (%d baud, capcode=%d) NUMERIC (%s)\n", o->name, baud, i, text + j + 1);
			batch = make_pocsag_batch(i, (char *) text + j + 1, strlen(text + j + 1), NUMERIC, 0);
			break;
		case 'A':				/* Alpha */
			if (!text[j + 1])
				return 0;
			ast_verb(3, "Channel %s: POCSAG page (%d baud, capcode=%d) ALPHA (%s)\n", o->name, baud, i, text + j + 1);
			batch = make_pocsag_batch(i, (char *) text + j + 1, strlen(text + j + 1), ALPHA, 0);
			break;
		case '?':				/* Query Page Status */
			{
				struct ast_frame wf = {
					.frametype = AST_FRAME_TEXT,
					.src = __PRETTY_FUNCTION__,
				};
	
				i = 0;
				ast_mutex_lock(&o->txqlock);
				AST_LIST_TRAVERSE(&o->txq, f1, frame_list) {
				    if (f1->src && (!strcmp(f1->src, PAGER_SRC))) {
				        i++;
				    }
				}

				ast_mutex_unlock(&o->txqlock);
				cmd = (i) ? "PAGES" : "NOPAGES";
				wf.data.ptr = cmd;
				wf.datalen = strlen(cmd);
				ast_queue_frame(o->owner, &wf);
				return 0;
			}
		default:
			return 0;
		}
		if (!batch) {
			ast_log(LOG_ERROR, "Channel %s: Error creating POCSAG page.\n", o->name);
			return 0;
		}
		b = batch;
		for (i = 0; b; b = b->next) {
			i++;
		}
		/* get number of samples to alloc for audio */
		audio_samples = (SAMPRATE * (PREAMBLE_BITS + (MESSAGE_BITS * i))) / baud;
		/* pad end with 250ms of silence */
		audio_samples += SAMPRATE / 4;
		/* also pad up to FRAME_SIZE */
		audio_samples += audio_samples % FRAME_SIZE;
		audio = ast_calloc(1, (audio_samples * sizeof(short)) + 10);
		if (!audio) {
			free_batch(batch);
			return 0;
		}
		divdiv = DIVLCM / baud;
		divcnt = 0;
		audio_ptr = 0;
		for (i = 0; i < (PREAMBLE_BITS / 32); i++) {
			mkpsamples(audio, 0xaaaaaaaa, &audio_ptr, &divcnt, divdiv);
		}
		b = batch;
		while (b) {
			mkpsamples(audio, b->sc, &audio_ptr, &divcnt, divdiv);
			for (j = 0; j < 8; j++) {
				for (i = 0; i < 2; i++) {
					mkpsamples(audio, b->frame[j][i], &audio_ptr, &divcnt, divdiv);
				}
			}
			b = b->next;
		}
		free_batch(batch);
		memset(audio1, 0, sizeof(audio1));
		for (i = 0; i < audio_samples; i += FRAME_SIZE) {
			struct ast_frame wf = {
				.frametype = AST_FRAME_VOICE,
				.subclass.format = ast_format_slin,
				.samples = FRAME_SIZE,
				.data.ptr = audio1 + AST_FRIENDLY_OFFSET,
				.datalen = FRAME_SIZE * 2,
				.offset = AST_FRIENDLY_OFFSET,
				.src = PAGER_SRC,
			};

			memcpy(wf.data.ptr, (char *) (audio + i), FRAME_SIZE * 2);
			f1 = ast_frdup(&wf);
			if (!f1) {
				ast_free(audio);
				return 0;
			}
			memset(&f1->frame_list, 0, sizeof(f1->frame_list));
			ast_mutex_lock(&o->txqlock);
			AST_LIST_INSERT_TAIL(&o->txq, f1, frame_list);
			ast_mutex_unlock(&o->txqlock);
		}
		ast_free(audio);
		return 0;
	}
	ast_log(LOG_ERROR, "Channel %s: Cannot parse simpleusb cmd: %s\n", o->name, text);
	return 0;
}

/*!
 * \brief Simpleusb call.
 * \param c				Asterisk channel.
 * \param dest			Destination.
 * \param timeout		Timeout.
 * \retval -1 			if not successful.
 * \retval 0 			if successful.
 */
static int simpleusb_call(struct ast_channel *c, const char *dest, int timeout)
{
	struct chan_simpleusb_pvt *o = ast_channel_tech_pvt(c);

	o->stophid = 0;
	ast_radio_time(&o->lasthidtime);
	ast_pthread_create_background(&o->hidthread, NULL, hidthread, o);
	ast_setstate(c, AST_STATE_UP);
	return 0;
}

/*!
 * \brief Answer the call.
 * \param c				Asterisk channel.
 * \retval 0 			If successful.
 */
static int simpleusb_answer(struct ast_channel *c)
{
	ast_setstate(c, AST_STATE_UP);
	return 0;
}

/*!
 * \brief Asterisk hangup function.
 * \param c			Asterisk channel.
 * \retval 0		Always returns 0.			
 */
static int simpleusb_hangup(struct ast_channel *c)
{
	struct chan_simpleusb_pvt *o = ast_channel_tech_pvt(c);

	ast_channel_tech_pvt_set(c, NULL);
	o->owner = NULL;
	ast_module_unref(ast_module_info->self);
	if (o->hookstate) {
		o->hookstate = 0;
		setformat(o, O_CLOSE);
	}
	o->stophid = 1;
	pthread_join(o->hidthread, NULL);
	return 0;
}

/*!
 * \brief Asterisk write function.
 * This routine handles asterisk to radio frames.
 * \param ast			Asterisk channel.
 * \param frame			Asterisk frame to process.
 * \retval 0			Successful.
 */
static int simpleusb_write(struct ast_channel *c, struct ast_frame *f)
{
	struct chan_simpleusb_pvt *o = ast_channel_tech_pvt(c);
	struct ast_frame *f1;

	if (!o->hasusb) {
		return 0;
	}
	if (o->sounddev < 0) {
		setformat(o, O_RDWR);
	}
	if (o->sounddev < 0) {
		return 0;				/* not fatal */
	}
	/*
	 * we could receive a block which is not a multiple of our
	 * FRAME_SIZE, so buffer it locally and write to the device
	 * in FRAME_SIZE chunks.
	 * Keep the residue stored for future use.
	 */

#if DEBUG_CAPTURES == 1			
	/* Write input data to a file.
	 * Left channel has the audio, right channel shows txkeyed
	 */
	if (ftxcapraw && o->txcapraw) {
		short i, tbuff[f->datalen];
		for (i = 0; i < f->datalen; i += 2) {
			tbuff[i] = ((short *) (f->data.ptr))[i / 2];
			tbuff[i + 1] = o->txkeyed * 0x1000;
		}
		fwrite(tbuff, 2, f->datalen, ftxcapraw);
	}
#endif

	if ((!o->txkeyed) && (!o->txtestkey)) {
		return 0;
	}

	if ((!o->txtestkey) && o->echoing) {
		return 0;
	}

	//take the data from the network and save it for processing
	f1 = ast_frdup(f);
	if (!f1) {
		return 0;
	}
	memset(&f1->frame_list, 0, sizeof(f1->frame_list));
	ast_mutex_lock(&o->txqlock);
	AST_LIST_INSERT_TAIL(&o->txq, f1, frame_list);
	ast_mutex_unlock(&o->txqlock);

	return 0;
}

/*!
 * \brief Asterisk read function.
 * \param ast			Asterisk channel.
 * \retval 				Asterisk frame.
 */
static struct ast_frame *simpleusb_read(struct ast_channel *c)
{
	int res, cd, sd, src, num_frames, ispager, doleft, doright;
	register int i;
	struct chan_simpleusb_pvt *o = ast_channel_tech_pvt(c);
	struct ast_frame *f = &o->read_f, *f1;
	struct ast_frame wf1;
	time_t now;
	register short *sp, *sp1; 
	short outbuf[FRAME_SIZE * 2 * 6];

	/* check to the if the hid thread is still processing */
	if (o->lasthidtime) {
		ast_radio_time(&now);
		if ((now - o->lasthidtime) > 3) {
			ast_log(LOG_ERROR, "Channel %s: HID process has died or is not responding.\n", o->name);
			return NULL;
		}
	}
	/* Set frame defaults */
	memset(f, 0, sizeof(struct ast_frame));
	f->frametype = AST_FRAME_NULL;
	f->src = __PRETTY_FUNCTION__;

	/* if USB device not ready, just return NULL frame */
	if (!o->hasusb) {
		if (o->rxkeyed) {
			struct ast_frame wf = {
				.frametype = AST_FRAME_CONTROL,
				.subclass.integer = AST_CONTROL_RADIO_UNKEY,
				.src = __PRETTY_FUNCTION__,
			};

			o->lastrx = 0;
			o->rxkeyed = 0;
			ast_queue_frame(o->owner, &wf);
		}
		return &ast_null_frame;
	}

	/* If we have stopped echoing, clear the echo queue */
	if (!o->echomode) {
		struct qelem *q;

		ast_mutex_lock(&o->echolock);
		o->echoing = 0;
		while (o->echoq.q_forw != &o->echoq) {
			q = o->echoq.q_forw;
			remque(q);
			ast_free(q);
		}
		ast_mutex_unlock(&o->echolock);
	}

	/* If we are in echomode and we have stopped receiving audio
	 * queue up the packets we have stored in the echo queue
	 * for playback.
	 */
	if (o->echomode && (!o->rxkeyed)) {
		struct usbecho *u;

		ast_mutex_lock(&o->echolock);
		/* if there is something in the queue */
		if (o->echoq.q_forw != &o->echoq) {
			u = (struct usbecho *) o->echoq.q_forw;
			remque((struct qelem *) u);
			f->frametype = AST_FRAME_VOICE;
			f->subclass.format = ast_format_slin;
			f->samples = FRAME_SIZE;
			f->datalen = FRAME_SIZE * 2;
			f->offset = AST_FRIENDLY_OFFSET;
			f->data.ptr = o->simpleusb_read_frame_buf + AST_FRIENDLY_OFFSET;
			memcpy(f->data.ptr, u->data, FRAME_SIZE * 2);
			ast_free(u);
			f1 = ast_frdup(f);
			if (!f1) {
				return &ast_null_frame;
			}
			memset(&f1->frame_list, 0, sizeof(f1->frame_list));
			ast_mutex_lock(&o->txqlock);
			AST_LIST_INSERT_TAIL(&o->txq, f1, frame_list);
			ast_mutex_unlock(&o->txqlock);
			o->echoing = 1;
		} else {
			o->echoing = 0;
		}
		ast_mutex_unlock(&o->echolock);
	}

	/* Process the transmit queue */

	for (;;) {
		num_frames = 0;
		ast_mutex_lock(&o->txqlock);
		AST_LIST_TRAVERSE(&o->txq, f1, frame_list) num_frames++;
		ast_mutex_unlock(&o->txqlock);
		i = used_blocks(o);
		if (o->txkeyed) {
			ast_debug(7, "blocks used %d, Dest Buffer %d", i, o->simpleusb_write_dst);
		}
		if (num_frames && (num_frames > 3 || (!o->txkeyed && !o->txtestkey)) && i <= o->queuesize) {
			if (i == 0) { /* We are not keeping the buffer full, add 1 frame */
				memset(outbuf, 0, sizeof(outbuf));
				soundcard_writeframe(o, outbuf);
				ast_debug(7, "A null frame has been added");
			}
			ast_mutex_lock(&o->txqlock);
			f1 = AST_LIST_REMOVE_HEAD(&o->txq, frame_list);
			ast_mutex_unlock(&o->txqlock);

			src = 0;			/* read position into f1->data */
			while (src < f1->datalen) {
				/* Compute spare room in the buffer */
				int l = sizeof(o->simpleusb_write_buf) - o->simpleusb_write_dst;

				if (f1->datalen - src >= l) {	
					/* enough to fill a frame */
					memcpy(o->simpleusb_write_buf + o->simpleusb_write_dst, (char *) f1->data.ptr + src, l);
					/* Below is an attempt to match levels to the original CM108 IC which has
					 * been out of production for over 10 years. Scaling audio to 109.375% will
					 * result in clipping! Any adjustments for CM1xxx gain differences should be
					 * made in the mixer settings, not in the audio stream.
					 * TODO: After the vast majority of existing installs have had a chance to review their
					 * audio settings and these old scaling/clipping hacks are no longer in significant use
					 * the legacyaudioscaling cfg and related code should be deleted.
					 */
					/* Adjust the audio level for CM119 A/B devices */
					if (o->legacyaudioscaling && o->devtype != C108_PRODUCT_ID) {
						register int v;

						sp = (short *) o->simpleusb_write_buf;
						for (i = 0; i < FRAME_SIZE; i++) {
							v = *sp;
							v += v >> 3;   /* add *.125 giving * 1.125 */
							v -= *sp >> 5; /* subtract *.03125 giving * 1.09375 */
							if (v > 32765.0) {
								v = 32765.0;
							} else if (v < -32765.0) {
								v = -32765.0;
							}
							*sp++ = v;
						}
					}

					sp = (short *) o->simpleusb_write_buf;
					sp1 = outbuf;
					doright = 1;
					doleft = 1;
					ispager = 0;
					if (f1->src && (!strcmp(f1->src, PAGER_SRC))) {
						ispager = 1;
					}
					/* If pager audio, determine which channel to store audio */
					if (o->pager != PAGER_NONE) {
						doleft = (o->pager == PAGER_A) ? ispager : !ispager;
						doright = (o->pager == PAGER_B) ? ispager : !ispager;
					}
					/* Upsample from 8000 mono to 48000 stereo */
					for (i = 0; i < FRAME_SIZE; i++) {
						register short s, v;

						if (o->preemphasis) {
							s = preemph(sp[i], &o->prestate);
						} else {
							s = sp[i];
						}
						v = lpass(s, o->flpt);
						*sp1++ = (doleft) ? v : 0;
						*sp1++ = (doright) ? v : 0;
						v = lpass(s, o->flpt);
						*sp1++ = (doleft) ? v : 0;
						*sp1++ = (doright) ? v : 0;
						v = lpass(s, o->flpt);
						*sp1++ = (doleft) ? v : 0;
						*sp1++ = (doright) ? v : 0;
						v = lpass(s, o->flpt);
						*sp1++ = (doleft) ? v : 0;
						*sp1++ = (doright) ? v : 0;
						v = lpass(s, o->flpt);
						*sp1++ = (doleft) ? v : 0;
						*sp1++ = (doright) ? v : 0;
						v = lpass(s, o->flpt);
						*sp1++ = (doleft) ? v : 0;
						*sp1++ = (doright) ? v : 0;
					}
					soundcard_writeframe(o, outbuf);
					src += l;
					o->simpleusb_write_dst = 0;
					if (o->waspager && (!ispager)) {
						struct ast_frame wf = {
							.frametype = AST_FRAME_TEXT,
							.data.ptr = ENDPAGE_STR,
							.datalen = strlen(ENDPAGE_STR) + 1,
							.src = __PRETTY_FUNCTION__,
						};

						ast_queue_frame(o->owner, &wf);
					}
					o->waspager = ispager;
				} else {		
					/* copy residue */
					l = f1->datalen - src;
					memcpy(o->simpleusb_write_buf + o->simpleusb_write_dst, (char *) f1->data.ptr + src, l);
					src += l;	/* but really, we are done */
					o->simpleusb_write_dst += l;
				}
			}
			ast_frfree(f1);
			continue;
		}
		break;
	}
	
	/* Read audio data from the USB sound device.
	 * Sound data will arrive at 48000 samples per second
	 * in stereo format.
	 */
	res = read(o->sounddev, o->simpleusb_read_buf + o->readpos, sizeof(o->simpleusb_read_buf) - o->readpos);
	if (res < 0) { /* Audio data not ready, return a NULL frame */
		if (errno != EAGAIN) {
			o->readerrs = 0;
			o->hasusb = 0;
			return &ast_null_frame;
		}
		if (o->readerrs++ > READERR_THRESHOLD) {
			ast_log(LOG_ERROR, "Stuck USB read channel [%s], un-sticking it!\n", o->name);
			o->readerrs = 0;
			o->hasusb = 0;
			return &ast_null_frame;
		}
		if (o->readerrs == 1) {
			ast_log(LOG_WARNING, "Possibly stuck USB read channel. [%s]\n", o->name);
		}
		return &ast_null_frame;
	}

#if DEBUG_CAPTURES == 1
	if (o->rxcapraw && frxcapraw)
		fwrite(o->simpleusb_read_buf + o->readpos, 1, res, frxcapraw);
#endif

	if (o->readerrs) {
		ast_log(LOG_WARNING, "USB read channel [%s] was not stuck.\n", o->name);
	}

	o->readerrs = 0;
	o->readpos += res;
	if (o->readpos < sizeof(o->simpleusb_read_buf)) { /* not enough samples */
		return &ast_null_frame;
	}

	/* If we have been sending pager audio, see if
	 * we are finished.
	 */
	if (o->waspager) {
		num_frames = 0;
		ast_mutex_lock(&o->txqlock);
		AST_LIST_TRAVERSE(&o->txq, f1, frame_list) num_frames++;
		ast_mutex_unlock(&o->txqlock);
		if (num_frames < 1) {
			memset(&wf1, 0, sizeof(wf1));
			wf1.frametype = AST_FRAME_TEXT;
			wf1.datalen = strlen(ENDPAGE_STR) + 1;
			wf1.data.ptr = ENDPAGE_STR;
			ast_queue_frame(o->owner, &wf1);
			o->waspager = 0;
		}
	}
	
	/* Check for carrier detect - COR active */
	cd = 1;
	if ((o->rxcdtype == CD_HID) && (!o->rxhidsq)) {
		cd = 0;
	} else if ((o->rxcdtype == CD_HID_INVERT) && o->rxhidsq) {
		cd = 0;
	} else if ((o->rxcdtype == CD_PP) && (!o->rxppsq)) {
		cd = 0;
	} else if ((o->rxcdtype == CD_PP_INVERT) && o->rxppsq) {
		cd = 0;
	}

	/* Apply cd turn-on delay, if one specified */
	if (o->rxondelay && cd && (o->rxoncnt++ < o->rxondelay)) {
		cd = 0;
	} else if (!cd) {
		o->rxoncnt = 0;
	}
	o->rx_cos_active = cd;

	/* Check for SD - CTCSS active */
	sd = 1;	
	if ((o->rxsdtype == SD_HID) && (!o->rxhidctcss)) {
		sd = 0;
	} else if ((o->rxsdtype == SD_HID_INVERT) && o->rxhidctcss) {
		sd = 0;
	} else if ((o->rxsdtype == SD_PP) && (!o->rxppctcss)) {
		sd = 0;
	} else if ((o->rxsdtype == SD_PP_INVERT) && o->rxppctcss) {
		sd = 0;
	}

	/* See if we are overriding CTCSS to active */
	if (o->rxctcssoverride) {
		sd = 1;
	}
	o->rx_ctcss_active = sd;
	
	/* Special case where cd and sd have been configured for no */
	if (o->rxcdtype == CD_IGNORE && o->rxsdtype == SD_IGNORE) {
		cd = 0;
		sd = 0;
	}
	
	/* Timer for how long TX has been unkeyed - used with txoffdelay */
	if (o->txoffdelay) {
		if (o->txkeyed == 1) {
			o->txoffcnt = 0;		/* If keyed, set this to zero. */
		} else {
			o->txoffcnt++;
			if (o->txoffcnt > MS_TO_FRAMES(TX_OFF_DELAY_MAX)) {
				o->txoffcnt = MS_TO_FRAMES(TX_OFF_DELAY_MAX); /* limit count */
			}
		}
	}

	/* Check conditions and set receiver active */
	o->rxkeyed = sd && cd && ((!o->lasttx) || o->duplex) && (o->txoffcnt >= o->txoffdelay);

	/* Send a message to indicate rx signal detect conditions */
	if (o->lastrx && (!o->rxkeyed)) {
		struct ast_frame wf = {
			.frametype = AST_FRAME_CONTROL,
			.subclass.integer = AST_CONTROL_RADIO_UNKEY,
			.src = __PRETTY_FUNCTION__,
		};

		o->lastrx = 0;
		ast_queue_frame(o->owner, &wf);
		if (o->duplex3) {
			ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_PLAYBACK_SW, 0, 0);
		}
	} else if ((!o->lastrx) && (o->rxkeyed)) {
		struct ast_frame wf = {
			.frametype = AST_FRAME_CONTROL,
			.subclass.integer = AST_CONTROL_RADIO_KEY,
			.src = __PRETTY_FUNCTION__,
		};

		o->lastrx = 1;
		ast_queue_frame(o->owner, &wf);
		if (o->duplex3) {
			ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_PLAYBACK_SW, 1, 0);
		}
	}

	/* Check for ADC clipping and input audio statistics before any filtering is done.
	 * FRAME_SIZE define refers to 8Ksps mono which is 160 samples per 20mS USB frame.
	 * ast_radio_check_audio() takes the read buffer as received (48K stereo),
	 * extracts the mono 48K channel, checks amplitude and distortion characteristics,
	 * and returns true if clipping was detected.
	 */
	if (ast_radio_check_audio((short *) o->simpleusb_read_buf, &o->rxaudiostats, 12 * FRAME_SIZE)) {
		if (o->clipledgpio) {
			/* Set Clip LED GPIO pulsetimer if not already set */
			if (!o->hid_gpio_pulsetimer[o->clipledgpio - 1]) {
				o->hid_gpio_pulsetimer[o->clipledgpio - 1] = CLIP_LED_HOLD_TIME_MS;
			}
		}
	}

	/* Downsample received audio from 48000 stereo to 8000 mono */
	sp = (short *) o->simpleusb_read_buf;
	sp1 = (short *) (o->simpleusb_read_frame_buf + AST_FRIENDLY_OFFSET);
	for (i = 0; i < FRAME_SIZE; i++) {
		(void) lpass(*sp++, o->flpr);
		sp++;
		(void) lpass(*sp++, o->flpr);
		sp++;
		(void) lpass(*sp++, o->flpr);
		sp++;
		(void) lpass(*sp++, o->flpr);
		sp++;
		(void) lpass(*sp++, o->flpr);
		sp++;
		if (o->plfilter && o->deemphasis) {
			*sp1++ = hpass6(deemph(lpass(*sp++, o->flpr), &o->destate), o->hpx, o->hpy);
		} else if (o->deemphasis) {
			*sp1++ = deemph(lpass(*sp++, o->flpr), &o->destate);
		} else if (o->plfilter) {
			*sp1++ = hpass(lpass(*sp++, o->flpr), o->hpx, o->hpy);
		} else {
			*sp1++ = lpass(*sp++, o->flpr);
		}
		sp++;
	}

	/* If we are in echomode and receiving audio, store 
	 * it in the echo queue for later playback.
	 */
	if (o->echomode && o->rxkeyed && (!o->echoing)) {
		register int x;
		struct usbecho *u;

		ast_mutex_lock(&o->echolock);
		x = 0;
		/* get count of frames */
		for (u = (struct usbecho *) o->echoq.q_forw;
			 u != (struct usbecho *) &o->echoq; u = (struct usbecho *) u->q_forw)
			x++;
		if (x < o->echomax) {
			u = ast_calloc(1, sizeof(struct usbecho));
			if (u) {
				memcpy(u->data, (o->simpleusb_read_frame_buf + AST_FRIENDLY_OFFSET), FRAME_SIZE * 2);
				insque((struct qelem *) u, o->echoq.q_back);
			}
		}
		ast_mutex_unlock(&o->echolock);
	}

#if DEBUG_CAPTURES == 1
	if (o->rxcapraw && frxcapcooked)
		fwrite(o->simpleusb_read_frame_buf + AST_FRIENDLY_OFFSET, sizeof(short), FRAME_SIZE, frxcapcooked);
#endif

	/* reset read pointer for next frame */
	o->readpos = 0;	
	/* Do not return the frame if the channel is not up */
	if (ast_channel_state(c) != AST_STATE_UP) {	
		return &ast_null_frame;
	}
	/* ok we can build and deliver the frame to the caller */
	f->frametype = AST_FRAME_VOICE;
	f->subclass.format = ast_format_slin;
	f->offset = AST_FRIENDLY_OFFSET;
	f->samples = FRAME_SIZE;
	f->datalen = FRAME_SIZE * 2;
	f->data.ptr = o->simpleusb_read_frame_buf + AST_FRIENDLY_OFFSET;
	if (!o->rxkeyed) {
		memset(f->data.ptr, 0, f->datalen);
	}
	/* Process the audio to see if contains DTMF */
	if (o->usedtmf && o->dsp) {
		f1 = ast_dsp_process(c, o->dsp, f);
		if ((f1->frametype == AST_FRAME_DTMF_END) || (f1->frametype == AST_FRAME_DTMF_BEGIN)) {
			if ((f1->subclass.integer == 'm') || (f1->subclass.integer == 'u')) {
				f1->frametype = AST_FRAME_NULL;
				f1->subclass.integer = 0;
				return (f1);
			}
			if (f1->frametype == AST_FRAME_DTMF_END) {
				f1->len = ast_tvdiff_ms(ast_radio_tvnow(), o->tonetime);
				if (option_verbose) {
					ast_log(LOG_NOTICE, "Channel %s: Got DTMF char %c duration %ld ms\n", o->name, f1->subclass.integer, f1->len);
				}
				o->toneflag = 0;
			} else {
				if (o->toneflag) {
					ast_frfree(f1);
					f1 = NULL;
				} else {
					o->tonetime = ast_radio_tvnow();
					o->toneflag = 1;
				}
			}
			if (f1) {
				return (f1);
			}
		}
	}

	/* Raw audio samples should never be clipped or scaled for any reason. Adjustments to 
	 * audio levels should be made only in the USB interface mixer settings.
	 * TODO: After the vast majority of existing installs have had a chance to review their
	 * audio settings and these old scaling/clipping hacks are no longer in significant use
	 * the legacyaudioscaling cfg and related code should be deleted.
	 */
	/* scale and clip values */
	if (o->legacyaudioscaling && o->rxvoiceadj > 1.0) {
		register int i, x;
		register float f1;
		register int16_t *p = (int16_t *) f->data.ptr;

		for (i = 0; i < f->samples; i++) {
			f1 = (float) p[i] * o->rxvoiceadj;
			x = (int) f1;
			if (x > 32767) {
				x = 32767;
			} else if (x < -32768) {
				x = -32768;
			}
			p[i] = x;
		}
	}

	/* Compute the peak signal if requested */
	if (o->measure_enabled) {
		register int i;
		register int32_t accum;
		register int16_t *p = (int16_t *) f->data.ptr;

		for (i = 0; i < f->samples; i++) {
			accum = p[i];
			if (accum > o->amax) {
				o->amax = accum;
				o->discounteru = o->discfactor;
			} else if (--o->discounteru <= 0) {
				o->discounteru = o->discfactor;
				o->amax = (int32_t) ((o->amax * 32700) / 32768);
			}
			if (accum < o->amin) {
				o->amin = accum;
				o->discounterl = o->discfactor;
			} else if (--o->discounterl <= 0) {
				o->discounterl = o->discfactor;
				o->amin = (int32_t) ((o->amin * 32700) / 32768);
			}
		}
		o->apeak = (int32_t) (o->amax - o->amin) / 2;
	}
	return f;
}

/*!
 * \brief Asterisk fixup function.
 * \param oldchan		Old asterisk channel.
 * \param newchan		New asterisk channel.
 * \retval 0			Always returns 0.			
 */
static int simpleusb_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct chan_simpleusb_pvt *o = ast_channel_tech_pvt(newchan);
	ast_log(LOG_WARNING, "Channel %s: Fixup received.\n", o->name);
	o->owner = newchan;
	return 0;
}

/*!
 * \brief Asterisk indicate function.
 * This is used to indicate tx key / unkey.
 * \param c				Asterisk channel.
 * \param cond			Condition.
 * \param data			Data.
 * \param datalen		Data length.
 * \retval 0			If successful.
 * \retval -1			For hangup.
 */
static int simpleusb_indicate(struct ast_channel *c, int cond, const void *data, size_t datalen)
{
	struct chan_simpleusb_pvt *o = ast_channel_tech_pvt(c);

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
		ast_verbose("Channel %s: Console has been placed on hold.\n", o->name);
		ast_moh_start(c, data, "default");
		break;
	case AST_CONTROL_UNHOLD:
		ast_verbose("Channel %s: Console has been retrieved from hold.\n", o->name);
		ast_moh_stop(c);
		break;
	case AST_CONTROL_PROCEEDING:
		ast_verbose("Channel %s: Call Proceeding.\n", o->name);
		ast_moh_stop(c);
		break;
	case AST_CONTROL_PROGRESS:
		ast_verbose("Channel %s: Call Progress.\n", o->name);
		ast_moh_stop(c);
		break;
	case AST_CONTROL_RADIO_KEY:
		o->txkeyed = 1;
		kickptt(o);
		ast_debug(1, "Channel %s: ACRK TX ON.\n", o->name);
		break;
	case AST_CONTROL_RADIO_UNKEY:
		o->txkeyed = 0;
		kickptt(o);
		ast_debug(1, "Channel %s: ACRUK TX OFF.\n", o->name);
		break;
	default:
		ast_log(LOG_WARNING, "Channel %s: Don't know how to display condition %d.\n", o->name, cond);
		return -1;
	}

	return 0;
}

/*!
 * \brief Asterisk setoption function.
 * \param chan			Asterisk channel.
 * \param option		Option.
 * \param data			Data.
 * \param datalen		Data length.
 * \retval 0			If successful.
 * \retval -1			If failed.
 */
static int simpleusb_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
	char *cp;
	struct chan_simpleusb_pvt *o = ast_channel_tech_pvt(chan);

	/* all supported options require data */
	if (!data || (datalen < 1)) {
		errno = EINVAL;
		return -1;
	}

	switch (option) {
	case AST_OPTION_TONE_VERIFY:
		cp = data;
		switch (*cp) {
		case 1:
			ast_log(LOG_NOTICE, "Channel %s: Set option TONE VERIFY, mode: OFF(0).\n", o->name);
			o->usedtmf = 1;
			break;
		case 2:
			ast_log(LOG_NOTICE, "Channel %s: Set option TONE VERIFY, mode: MUTECONF/MAX(2).\n", o->name);
			o->usedtmf = 1;
			break;
		case 3:
			ast_log(LOG_NOTICE, "Channel %s: Set option TONE VERIFY, mode: DISABLE DETECT(3).\n", o->name);
			o->usedtmf = 0;
			break;
		default:
			ast_log(LOG_NOTICE, "Channel %s: Set option TONE VERIFY, mode: OFF(0).\n", o->name);
			o->usedtmf = 1;
			break;
		}
		break;
	}
	errno = 0;
	return 0;
}

/*!
 * \brief Start a new simpleusb call.
 * \param o				Private structure.
 * \param ext			Extension.
 * \param ctx			Context.
 * \param state			State.
 * \param assignedids	Unique ID string assigned to the channel.
 * \param requestor		Asterisk channel.
 * \return 				Asterisk channel.
 */
static struct ast_channel *simpleusb_new(struct chan_simpleusb_pvt *o, char *ext, char *ctx, int state,
										 const struct ast_assigned_ids *assignedids,
										 const struct ast_channel *requestor)
{
	struct ast_channel *c;

	c = ast_channel_alloc(1, state, NULL, NULL, "", ext, ctx, assignedids, requestor, 0, "SimpleUSB/%s", o->name);
	if (c == NULL) {
		return NULL;
	}
	ast_channel_tech_set(c, &simpleusb_tech);
	if ((o->sounddev < 0) && o->hasusb) {
		setformat(o, O_RDWR);
	}
	ast_channel_internal_fd_set(c, 0, o->sounddev);	/* -1 if device closed, override later */
	ast_channel_nativeformats_set(c, simpleusb_tech.capabilities);
	ast_channel_set_readformat(c, ast_format_slin);
	ast_channel_set_writeformat(c, ast_format_slin);
	ast_channel_tech_pvt_set(c, o);
	ast_channel_unlock(c);

	o->owner = c;
	ast_module_ref(ast_module_info->self);
	ast_jb_configure(c, &global_jbconf);
	if (state != AST_STATE_DOWN) {
		if (ast_pbx_start(c)) {
			ast_log(LOG_WARNING, "Channel %s: Unable to start PBX.\n", ast_channel_name(c));
			ast_hangup(c);
			o->owner = c = NULL;
			/* What about the channel itself ? */
		}
	}

	return c;
}

/*!
 * \brief SimpleUSB request from Asterisk.
 * This is a standard Asterisk function - requester.
 * Asterisk calls this function to to setup private data structures.
 * \param type			Type of channel to request.
 * \param cap			Format capabilities for the channel.
 * \param assignedids	Unique ID string to assign to the channel.
 * \param requestor		Channel asking for data. 
 * \param data			Destination of the call.
 * \param cause			Cause of failure.
 * \retval NULL			Failure
 * \return				ast_channel if successful
 */
static struct ast_channel *simpleusb_request(const char *type, struct ast_format_cap *cap,
											 const struct ast_assigned_ids *assignedids,
											 const struct ast_channel *requestor, const char *data, int *cause)
{
	struct ast_channel *c;
	struct chan_simpleusb_pvt *o = find_desc(data);

	if (!o) {
		ast_log(LOG_WARNING, "Device %s not found.\n", (char *) data);
		return NULL;
	}

	if (!(ast_format_cap_iscompatible(cap, simpleusb_tech.capabilities))) {
		struct ast_str *cap_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		ast_log(LOG_NOTICE, "Channel %s: Channel requested with unsupported format(s): '%s'\n", 
			o->name, ast_format_cap_get_names(cap, &cap_buf));
		return NULL;
	}

	if (o->owner) {
		ast_log(LOG_NOTICE, "Channel %s: Already have a call (chan %p) on the usb channel\n",
			o->name, o->owner);
		*cause = AST_CAUSE_BUSY;
		return NULL;
	}
	c = simpleusb_new(o, NULL, NULL, AST_STATE_DOWN, assignedids, requestor);
	if (!c) {
		ast_log(LOG_ERROR, "Channel %s: Unable to create new usb channel\n", o->name);
		return NULL;
	}

	return c;
}

/*!
 * \brief Process Asterisk CLI request to key radio.
 * \param fd			Asterisk CLI fd
 * \param argc			Number of arguments
 * \param argv			Arguments
 * \return	CLI success, showusage, or failure.
 */
static int console_key(int fd, int argc, const char *const *argv)
{
	struct chan_simpleusb_pvt *o = find_desc(simpleusb_active);

	if (argc != 2) {
		return RESULT_SHOWUSAGE;
	}
	o->txtestkey = 1;
	kickptt(o);
	return RESULT_SUCCESS;
}

/*!
 * \brief Process Asterisk CLI request to unkey radio.
 * \param fd			Asterisk CLI fd
 * \param argc			Number of arguments
 * \param argv			Arguments
 * \return	CLI success, showusage, or failure.
 */
static int console_unkey(int fd, int argc, const char *const *argv)
{
	struct chan_simpleusb_pvt *o = find_desc(simpleusb_active);

	if (argc != 2) {
		return RESULT_SHOWUSAGE;
	}
	o->txtestkey = 0;
	kickptt(o);
	return RESULT_SUCCESS;
}

/*!
 * \brief Process asterisk cli request to show or set active USB device.
 * \param fd			Asterisk cli fd
 * \param argc			Number of arguments
 * \param argv			Arguments
 * \return	Cli success, showusage, or failure.
 */
static int susb_active(int fd, int argc, const char *const *argv)
{
	if (argc == 2) {
		ast_cli(fd, "Active Simple USB Radio device is [%s].\n", simpleusb_active);
	} else if (argc != 3) {
		return RESULT_SHOWUSAGE;
	} else {
		struct chan_simpleusb_pvt *o;
		if (!strcmp(argv[2], "show")) {
			ast_mutex_lock(&usb_dev_lock);
			for (o = simpleusb_default.next; o; o = o->next) {
				ast_cli(fd, "Device [%s] exists as device=%s card=%d\n", o->name,o->devstr,ast_radio_usb_get_usbdev(o->devstr));
			}
			ast_mutex_unlock(&usb_dev_lock);
			return RESULT_SUCCESS;
		}
		o = find_desc(argv[2]);
		if (!o) {
			ast_cli(fd, "No device [%s] exists\n", argv[2]);
		} else {
			simpleusb_active = o->name;
			ast_cli(fd, "Active (command) Simple USB Radio device set to [%s]\n", simpleusb_active);
		}
	}
	return RESULT_SUCCESS;
}

/*!
 * \brief Process Asterisk CLI request to swap usb devices
 * \param fd			Asterisk CLI fd
 * \param other			Other device.
 * \return	CLI success, showusage, or failure.
 */
static int usb_device_swap(int fd, const char *other)
{
	int d;
	char tmp[128];
	struct chan_simpleusb_pvt *p = NULL, *o = find_desc(simpleusb_active);

	if (o == NULL) {
		return -1;
	}
	if (!other) {
		return -1;
	}
	p = find_desc(other);
	if (p == NULL) {
		ast_cli(fd, "USB Device %s not found\n", other);
		return -1;
	}
	if (p == o) {
		ast_cli(fd, "You can't swap active device with itself!!\n");
		return -1;
	}
	ast_mutex_lock(&usb_dev_lock);
	strcpy(tmp, p->devstr);
	d = p->devicenum;
	strcpy(p->devstr, o->devstr);
	p->devicenum = o->devicenum;
	ast_copy_string(o->devstr, tmp, sizeof(o->devstr));
	o->devicenum = d;
	o->hasusb = 0;
	o->usbass = 0;
	p->hasusb = 0;
	p->usbass = 0;
	ast_cli(fd, "USB Devices successfully swapped.\n");
	ast_mutex_unlock(&usb_dev_lock);
	return 0;
}

/*!
 * \brief Send 3 second test tone.
 * \param fd			Asterisk CLI fd
 * \param o				Private struct.
 * \param intflag		Flag to indicate the type of wait.
 */
static void tune_flash(int fd, struct chan_simpleusb_pvt *o, int intflag)
{
#define	NFLASH 3

	int i;

	if (fd > 0) {
		ast_cli(fd, "Channel %s: USB Device Flash starting.\n", o->name);
	}
	for (i = 0; i < NFLASH; i++) {
		if (_send_tx_test_tone(fd, o, 1000, intflag)) {
			break;
		}
		if (ast_radio_wait_or_poll(fd, 1000, intflag)) {
			break;
		}
	}
	o->txtestkey = 0;
	if (fd > 0) {
		ast_cli(fd, "Channel %s: USB Device Flash completed.\n", o->name);
	}
}

/*!
 * \brief Process asterisk cli request for receiver deviation display.
 * \param fd			Asterisk cli fd
 * \param o				Private struct
 * \return	Cli success, showusage, or failure.
 */
static void tune_rxdisplay(int fd, struct chan_simpleusb_pvt *o)
{
	int j, waskeyed, meas, ncols = 75, wasverbose;
	char str[256];

	for (j = 0; j < ncols; j++) {
		str[j] = ' ';
	}
	str[j] = 0;
	ast_cli(fd, " %s \r", str);
	ast_cli(fd, "RX VOICE DISPLAY:\n");
	ast_cli(fd, "                                 v -- 3KHz        v -- 5KHz\n");

	o->measure_enabled = 1;
	o->discfactor = 1000;
	o->discounterl = o->discounteru = 0;
	wasverbose = option_verbose;
	option_verbose = 0;

	waskeyed = !o->rxkeyed;
	for (;;) {
		o->amax = o->amin = 0;
		if (ast_radio_poll_input(fd, 100)) {
			break;
		}
		if (o->rxkeyed != waskeyed) {
			for (j = 0; j < ncols; j++) {
				str[j] = ' ';
			}
			str[j] = 0;
			ast_cli(fd, " %s \r", str);
		}
		waskeyed = o->rxkeyed;
		if (!o->rxkeyed) {
			ast_cli(fd, "\r");
			continue;
		}
		meas = o->apeak;
		for (j = 0; j < ncols; j++) {
			int thresh = (meas * ncols) / 16384;
			if (j < thresh) {
				str[j] = '=';
			} else if (j == thresh) {
				str[j] = '>';
			} else {
				str[j] = ' ';
			}
		}
		str[j] = 0;
		ast_cli(fd, "|%s|\r", str);
	}
	o->measure_enabled = 0;
	option_verbose = wasverbose;
}

/*!
 * \brief Process asterisk cli request for cos, ctcss, and ptt live display.
 * \param fd			Asterisk cli fd
 * \param o				Private struct
 * \return	Cli success, showusage, or failure.
 */
static void tune_rxtx_status(int fd, struct chan_simpleusb_pvt *o)
{
	int wasverbose;

	ast_cli(fd, "Receiver/Transmitter Status Display:\n");
	ast_cli(fd, "  COS   | CTCSS  | COS   | PTT\n");
	ast_cli(fd, " Input  | Input  | Out   | Out\n");

	wasverbose = option_verbose;
	option_verbose = 0;

	for (;;) {
		/* If they press any key, exit live display */
		if (ast_radio_poll_input(fd, 200)) {
			break;
		}
		ast_cli(fd, " %s  | %s  | %s | %s\r", 
			o->rxcdtype ? (o->rx_cos_active ? "Keyed" : "Clear") : "Off  ", 
			o->rxsdtype ? (o->rx_ctcss_active ? "Keyed" : "Clear") : "Off  ", 
			o->rxkeyed ? "Keyed" : "Clear",
			(o->txkeyed || o->txtestkey) ? "Keyed" : "Clear");
	}
	
	option_verbose = wasverbose;
}

/*!
 * \brief Process Asterisk CLI request susb tune.
 * \param fd			Asterisk CLI fd
 * \param argc			Number of arguments
 * \param argv			Arguments
 * \return	CLI success, showusage, or failure.
 */
static int susb_tune(int fd, int argc, const char *const *argv)
{
	struct chan_simpleusb_pvt *o = find_desc(simpleusb_active);
	int i = 0;

	if ((argc < 3) || (argc > 4)) {
		return RESULT_SHOWUSAGE;
	}

	if (!strcasecmp(argv[2], "swap")) {
		if (argc > 3) {
			usb_device_swap(fd, argv[3]);
			return RESULT_SUCCESS;
		}
		return RESULT_SHOWUSAGE;
	} else if (!strcasecmp(argv[2], "menu-support")) {
		if (argc > 3) {
			tune_menusupport(fd, o, argv[3]);
		}
		return RESULT_SUCCESS;
	}
	if (!o->hasusb) {
		ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
		return RESULT_SUCCESS;
	} else if (!strcasecmp(argv[2], "rx")) {
		i = 0;

		if (argc == 3) {
			ast_cli(fd, "Current setting on Rx Channel is %d\n", o->rxmixerset);
		} else {
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) {
				return RESULT_SHOWUSAGE;
			}
			o->rxmixerset = i;
			ast_cli(fd, "Changed setting on RX Channel to %d\n", o->rxmixerset);
			mixer_write(o);
		}
	} else if (!strncasecmp(argv[2], "rxd", 3)) {
		tune_rxdisplay(fd, o);
	} else if (!strcasecmp(argv[2], "txa")) {
		i = 0;

		if (argc == 3) {
			ast_cli(fd, "Current setting on Tx Channel A is %d\n", o->txmixaset);
		} else {
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) {
				return RESULT_SHOWUSAGE;
			}
			o->txmixaset = i;
			ast_cli(fd, "Changed setting on TX Channel A to %d\n", o->txmixaset);
			mixer_write(o);
		}
	} else if (!strcasecmp(argv[2], "txb")) {
		i = 0;

		if (argc == 3) {
			ast_cli(fd, "Current setting on Tx Channel B is %d\n", o->txmixbset);
		} else {
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) {
				return RESULT_SHOWUSAGE;
			}
			o->txmixbset = i;
			ast_cli(fd, "Changed setting on TX Channel B to %d\n", o->txmixbset);
			mixer_write(o);
		}
	} else if (!strcasecmp(argv[2], "flash")) {
		tune_flash(fd, o, 0);
	} else if (!strcasecmp(argv[2], "nocap")) {
		ast_cli(fd, "File capture (raw)   was rx=%d tx=%d and now off.\n", o->rxcapraw, o->txcapraw);
		o->rxcapraw = o->txcapraw = 0;
		if (frxcapraw) {
			fclose(frxcapraw);
			frxcapraw = NULL;
		}
		if (frxcapcooked) {
			fclose(frxcapcooked);
			frxcapcooked = NULL;
		}
		if (ftxcapraw) {
			fclose(ftxcapraw);
			ftxcapraw = NULL;
		}
	} else if (!strcasecmp(argv[2], "rxcap")) {
		if (!frxcapraw) {
			frxcapraw = fopen(RX_CAP_RAW_FILE, "w");
		}
		if (!frxcapcooked) {
			frxcapcooked = fopen(RX_CAP_COOKED_FILE, "w");
		}
		ast_cli(fd, "cap rx raw on.\n");
		o->rxcapraw = 1;
	} else if (!strcasecmp(argv[2], "txcap")) {
		if (!ftxcapraw) {
			ftxcapraw = fopen(TX_CAP_RAW_FILE, "w");
		}
		ast_cli(fd, "cap tx raw on.\n");
		o->txcapraw = 1;
	} else if (!strcasecmp(argv[2], "save")) {
		tune_write(o);
		ast_cli(fd, "Saved radio tuning settings to simpleusb.conf\n");
	} else if (!strcasecmp(argv[2], "load")) {
		ast_mutex_lock(&o->eepromlock);
		while (o->eepromctl) {
			ast_mutex_unlock(&o->eepromlock);
			usleep(10000);
			ast_mutex_lock(&o->eepromlock);
		}
		o->eepromctl = 1;		/* request a load */
		ast_mutex_unlock(&o->eepromlock);

		ast_cli(fd, "Requesting loading of tuning settings from EEPROM for channel %s\n", o->name);
	} else {
		return RESULT_SHOWUSAGE;
	}
	return RESULT_SUCCESS;
}

/*!
 * \brief Send test tone for the specified interval.
 * \param fd			Asterisk CLI fd
 * \param o				Private struct.
 * \param ms			Milliseconds of test tone.
 * \param intflag		Flag to indicate the type of wait.
 * \retval -1			If failure.
 * \retval 0			If success.
 */
static int _send_tx_test_tone(int fd, struct chan_simpleusb_pvt *o, int ms, int intflag)
{
	int i, ret;

	ast_tonepair_stop(o->owner);
	if (ast_tonepair_start(o->owner, 1004.0, 0, 99999999, 7200.0)) {
		if (fd >= 0)
			ast_cli(fd, "Error starting test tone on %s!!\n", simpleusb_active);
		return -1;
	}
	ast_clear_flag(ast_channel_flags(o->owner), AST_FLAG_WRITE_INT);
	o->txtestkey = 1;
	i = 0;
	ret = 0;
	while (ast_channel_generatordata(o->owner) && (i < ms)) {
		if (ast_radio_wait_or_poll(fd, 50, intflag)) {
			ret = 1;
			break;
		}
		i += 50;
	}
	ast_tonepair_stop(o->owner);
	ast_clear_flag(ast_channel_flags(o->owner), AST_FLAG_WRITE_INT);
	o->txtestkey = 0;
	return ret;
}

/*!
 * \brief Print settings.
 * \param fd			Asterisk CLI fd
 * \param o				Private struct.
 */
static void _menu_print(int fd, struct chan_simpleusb_pvt *o)
{
	ast_cli(fd, "Active radio interface is [%s]\n", simpleusb_active);
	ast_mutex_lock(&usb_dev_lock);
	ast_cli(fd, "Device String is %s\n", o->devstr);
	ast_mutex_unlock(&usb_dev_lock);
	ast_cli(fd, "Card is %i\n", ast_radio_usb_get_usbdev(o->devstr));
	ast_cli(fd, "Rx Level currently set to %d\n", o->rxmixerset);
	ast_cli(fd, "Tx A Level currently set to %d\n", o->txmixaset);
	ast_cli(fd, "Tx B Level currently set to %d\n", o->txmixbset);
	if (o->legacyaudioscaling) {
		ast_cli(fd, "legacyaudioscaling is enabled\n");
	}
}

/*!
 * \brief Set receive level.
 * \param fd			Asterisk CLI fd
 * \param o				Private struct.
 * \param str			New value.
 */
static void _menu_rx(int fd, struct chan_simpleusb_pvt *o, const char *str)
{
	int i, x;

	if (!str[0]) {
		ast_cli(fd, "Channel %s: Current setting on Rx Channel is %d\n", o->name, o->rxmixerset);
		return;
	}
	for (x = 0; str[x]; x++) {
		if (!isdigit(str[x]))
			break;
	}
	if (str[x] || (sscanf(str, N_FMT(d), &i) < 1) || (i < 0) || (i > 999)) {
		ast_cli(fd, "Channel %s: Entry Error, Rx Channel Level setting not changed\n", o->name);
		return;
	}
	o->rxmixerset = i;
	ast_cli(fd, "Channel %s: Changed setting on RX Channel to %d\n", o->name, o->rxmixerset);
	mixer_write(o);
}

/*!
 * \brief Set transmit A level.
 * \param fd			Asterisk CLI fd
 * \param o				Private struct.
 * \param str			New level.
 */
static void _menu_txa(int fd, struct chan_simpleusb_pvt *o, const char *str)
{
	int i, dokey;

	if (!str[0]) {
		ast_cli(fd, "Channel %s: Current setting on Tx Channel A is %d\n", o->name, o->txmixaset);
		return;
	}
	dokey = 0;
	if (str[0] == 'K') {
		dokey = 1;
		str++;
	}
	if (str[0]) {
		if ((sscanf(str, N_FMT(d), &i) < 1) || (i < 0) || (i > 999)) {
			ast_cli(fd, "Channel %s: Entry Error, Tx Channel A Level setting not changed\n", o->name);
			return;
		}
		o->txmixaset = i;
		ast_cli(fd, "Channel %s: Changed setting on TX Channel A to %d\n", o->name, o->txmixaset);
		mixer_write(o);
	}
	if (dokey) {
		if (fd >= 0) {
			ast_cli(fd, "Channel %s: Keying Transmitter and sending 1000 Hz tone for 5 seconds...\n", o->name);
		}
		_send_tx_test_tone(fd, o, 5000, 1);
	}
}

/*!
 * \brief Set transmit B level.
 * \param fd			Asterisk CLI fd
 * \param o				Private struct.
 * \param str			New level.
 */
static void _menu_txb(int fd, struct chan_simpleusb_pvt *o, const char *str)
{
	int i, dokey;

	if (!str[0]) {
		ast_cli(fd, "Channel %s: Current setting on Tx Channel B is %d\n", o->name, o->txmixbset);
		return;
	}
	dokey = 0;
	if (str[0] == 'K') {
		dokey = 1;
		str++;
	}
	if (str[0]) {
		if ((sscanf(str, N_FMT(d), &i) < 1) || (i < 0) || (i > 999)) {
			ast_cli(fd, "Channel %s: Entry Error, Tx Channel B Level setting not changed\n", o->name);
			return;
		}
		o->txmixbset = i;
		ast_cli(fd, "Channel %s: Changed setting on TX Channel B to %d\n", o->name, o->txmixbset);
		mixer_write(o);
	}
	if (dokey) {
		if (fd >= 0) {
			ast_cli(fd, "Channel %s: Keying Transmitter and sending 1000 Hz tone for 5 seconds...\n", o->name);
		}
		_send_tx_test_tone(fd, o, 5000, 1);
	}
}

/*!
 * \brief Update the tune settings to the configuration file.
 * \param config	The (opened) config to use
 * \param filename	The configuration file being updated (e.g. "simpleusb.conf").
 * \param category	The category being updated (e.g. "12345").
 * \param variable	The variable being updated (e.g. "rxboost").
 * \param value		The value being updated (e.g. "yes").
 * \retval 0		If successful.
 * \retval -1		If unsuccessful.
 */
static int tune_variable_update(struct ast_config *config, const char *filename, struct ast_category *category,
	const char *variable, const char *value)
{
	int res;
	struct ast_variable *v, *var = NULL;

	/* ast_variable_retrieve, but returning the variable struct */
	for (v = ast_variable_browse(config, ast_category_get_name(category)); v; v = v->next) {
		if (!strcasecmp(variable, v->name)) {
			var = v;
		}
	}

	if (var && !strcmp(var->value, value)) {
		/* no need to update a matching value */
		return 0;
	}

	if (var && !var->inherited) {
		/* the variable is defined and not inherited from a template category */
		res = ast_variable_update(category, variable, value, var->value, var->object);
		if (res == 0) {
			return 0;
		}
	}

	/* create and add the variable / value to the category */
	var = ast_variable_new(variable, value, filename);
	if (var == NULL) {
		return -1;
	}

	/* and append */
	ast_variable_append(category, var);
	return 0;
}

/*!
 * \brief Write tune settings to the configuration file. If the device EEPROM is enabled, the settings are  saved to EEPROM.
 * \param o Channel private.
 */
static void tune_write(struct chan_simpleusb_pvt *o)
{
	struct ast_config *cfg;
	struct ast_category *category = NULL;
	struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS | CONFIG_FLAG_NOCACHE };

	if (!(cfg = ast_config_load2(CONFIG, "chan_simpleusb", config_flags))) {
		ast_log(LOG_ERROR, "Config file not found: %s\n", CONFIG);
		return;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file has invalid format: %s\n", CONFIG);
		return;
	}

#define CONFIG_UPDATE_STR(field) \
	if (tune_variable_update(cfg, CONFIG, category, #field, o->field)) { \
		ast_log(LOG_WARNING, "Failed to update %s\n", #field); \
	}

#define CONFIG_UPDATE_INT(field) \
	{ \
		char _buf[15]; \
		snprintf(_buf, sizeof(_buf), "%d", o->field); \
		if (tune_variable_update(cfg, CONFIG, category, #field, _buf)) { \
			ast_log(LOG_WARNING, "Failed to update %s\n", #field); \
		} \
	}

#define CONFIG_UPDATE_BOOL(field) \
	if (tune_variable_update(cfg, CONFIG, category, #field, o->field ? "yes" : "no")) { \
		ast_log(LOG_WARNING, "Failed to update %s\n", #field); \
	}

#define CONFIG_UPDATE_SIGNAL(key, field, signal_type) \
	if (tune_variable_update(cfg, CONFIG, category, #key, signal_type[o->field])) { \
		ast_log(LOG_WARNING, "Failed to update %s\n", #field); \
	}

	category = ast_category_get(cfg, o->name, NULL);
	if (!category) {
		ast_log(LOG_ERROR, "No category '%s' exists?\n", o->name);
	} else {
		CONFIG_UPDATE_STR(devstr);
		CONFIG_UPDATE_INT(rxmixerset);
		CONFIG_UPDATE_INT(txmixaset);
		CONFIG_UPDATE_INT(txmixbset);
		CONFIG_UPDATE_BOOL(rxboost);
		CONFIG_UPDATE_BOOL(preemphasis);
		CONFIG_UPDATE_BOOL(deemphasis);
		CONFIG_UPDATE_BOOL(plfilter);
		CONFIG_UPDATE_BOOL(invertptt);
		CONFIG_UPDATE_SIGNAL(carrierfrom, rxcdtype, cd_signal_type);
		CONFIG_UPDATE_SIGNAL(ctcssfrom, rxsdtype, sd_signal_type);
		CONFIG_UPDATE_INT(rxondelay);
		CONFIG_UPDATE_INT(txoffdelay);
		if (ast_config_text_file_save2(CONFIG, cfg, "chan_simpleusb", 0)) {
			ast_log(LOG_WARNING, "Failed to save config %s\n", CONFIG);
		}
	}

	ast_config_destroy(cfg);
#undef CONFIG_UPDATE_STR
#undef CONFIG_UPDATE_INT
#undef CONFIG_UPDATE_BOOL
#undef CONFIG_UPDATE_SIGNAL

	if (o->wanteeprom) {
		ast_mutex_lock(&o->eepromlock);
		while (o->eepromctl) {
			ast_mutex_unlock(&o->eepromlock);
			usleep(10000);
			ast_mutex_lock(&o->eepromlock);
		}
		memset(o->eeprom, 0, sizeof(o->eeprom));
		o->eeprom[EEPROM_USER_RXMIXERSET] = o->rxmixerset;
		o->eeprom[EEPROM_USER_TXMIXASET] = o->txmixaset;
		o->eeprom[EEPROM_USER_TXMIXBSET] = o->txmixbset;
		o->eepromctl = 2;		/* request a write */
		ast_mutex_unlock(&o->eepromlock);
	}
}

/*!
 * \brief Process tune menu commands.
 *
 * The following 'menu-support' commands are used:
 *
 * susb tune menusupport X - where X is one of the following:
 *		0 - get current settings
 *		1 - get node names that are configured in simpleusb.conf
 *		2 - print parameters
 *		3 - get node names that are configured in simpleusb.conf, except current device
 *		b - receiver tune display
 *		c - receive level
 *		f - txa level
 *		g - txb level
 *		j - save current settings for the selected node
 *		k - change echo mode
 *		l - generate test tone
 *		m - change rxboost
 *		n - change pre-emphasis
 *		o - change de-emphasis
 *		p - change plfilter
 *		q - change ptt keying mode 
 *		r - change carrierfrom setting
 *		s - change ctcss from setting
 *		t - change rx on delay
 *		u - change tx off delay
 *		v - view cos, ctcss and ptt status
 *		y - receive audio statistics display
 *		z - transmit audio statistics display
 *
 * \param fd			Asterisk CLI fd
 * \param o				Private struct.
 * \param cmd			Command to process.
 */
static void tune_menusupport(int fd, struct chan_simpleusb_pvt *o, const char *cmd)
{
	int x, oldverbose;
	struct chan_simpleusb_pvt *oy = NULL;

	oldverbose = option_verbose;
	option_verbose = 0;
	switch (cmd[0]) {
	case '0':					/* return audio processing configuration */
		/* note: to maintain backward compatibility for those expecting a specific # of
		   values to be returned (and in a specific order).  So, we only add to the end
		   of the returned list.  Also, once an update has been released we can't change
		   the format/content of any previously returned string */
		if (!strcmp(cmd, "0+4")) {
			ast_cli(fd, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", 
				o->txmixaset, o->txmixbset, o->echomode, o->rxboost, o->preemphasis, 
				o->deemphasis, o->plfilter, o->invertptt, o->rxcdtype, o->rxsdtype, 
				o->rxondelay, o->txoffdelay, o->rxmixerset,
				o->micplaymax, o->spkrmax, o->micmax);
		} else {
			ast_cli(fd, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", 
				o->txmixaset, o->txmixbset, o->echomode, o->rxboost, o->preemphasis, 
				o->deemphasis, o->plfilter, o->invertptt, o->rxcdtype, o->rxsdtype, 
				o->rxondelay, o->txoffdelay);
		}
		break;
	case '1':					/* return usb device name list */
		for (x = 0, oy = simpleusb_default.next; oy && oy->name; oy = oy->next, x++) {
			if (x) {
				ast_cli(fd, ",");
			}
			ast_cli(fd, "%s", oy->name);
		}
		ast_cli(fd, "\n");
		break;
	case '2':					/* print parameters */
		_menu_print(fd, o);
		break;
	case '3':					/* return usb device name list except current */
		for (x = 0, oy = simpleusb_default.next; oy && oy->name; oy = oy->next) {
			if (!strcmp(oy->name, o->name)) {
				continue;
			}
			if (x) {
				ast_cli(fd, ",");
			}
			ast_cli(fd, "%s", oy->name);
			x++;
		}
		ast_cli(fd, "\n");
		break;
	case 'b':					/* receiver tune display */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		tune_rxdisplay(fd, o);
		break;
	case 'c':					/* receive menu */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		_menu_rx(fd, o, cmd + 1);
		break;
	case 'f':					/* tx A menu */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		_menu_txa(fd, o, cmd + 1);
		break;
	case 'g':					/* tx B menu */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		_menu_txb(fd, o, cmd + 1);
		break;
	case 'j':					/* save settings */
		tune_write(o);
		ast_cli(fd, "Saved radio tuning settings to simpleusb.conf\n");
		break;
	case 'k':					/* change echo mode */
		if (cmd[1]) {
			if (cmd[1] > '0') {
				o->echomode = 1;
			} else {
				o->echomode = 0;
			}
			ast_cli(fd, "Echo Mode changed to %s\n", (o->echomode) ? "Enabled" : "Disabled");
		} else {
			ast_cli(fd, "Echo Mode is currently %s\n", (o->echomode) ? "Enabled" : "Disabled");
		}
		break;
	case 'l':					/* send test tone */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		tune_flash(fd, o, 1);
		break;
	case 'm':					/* change rxboost */
		if (cmd[1]) {
			if (cmd[1] > '0') {
				o->rxboost = 1;
			} else {
				o->rxboost = 0;
			}
			ast_cli(fd, "RxBoost changed to %s\n", (o->rxboost) ? "Enabled" : "Disabled");
		} else {
			ast_cli(fd, "RxBoost is currently %s\n", (o->rxboost) ? "Enabled" : "Disabled");
		}
		break;
	case 'n':					/* change pre-emphasis */
		if (cmd[1]) {
			if (cmd[1] > '0') {
				o->preemphasis = 1;
			} else {
				o->preemphasis = 0;
			}
			ast_cli(fd, "Pre-emphasis changed to %s\n", (o->preemphasis) ? "Enabled" : "Disabled");
		} else {
			ast_cli(fd, "Pre-emphasis is currently %s\n", (o->preemphasis) ? "Enabled" : "Disabled");
		}
		break;
	case 'o':					/* change de-emphasis */
		if (cmd[1]) {
			if (cmd[1] > '0') {
				o->deemphasis = 1;
			} else {
				o->deemphasis = 0;
			}
			ast_cli(fd, "De-emphasis changed to %s\n", (o->deemphasis) ? "Enabled" : "Disabled");
		} else {
			ast_cli(fd, "De-emphasis is currently %s\n", (o->deemphasis) ? "Enabled" : "Disabled");
		}
		break;
	case 'p':					/* change pl filter */
		if (cmd[1]) {
			if (cmd[1] > '0') {
				o->plfilter = 1;
			} else {
				o->plfilter = 0;
			}
			ast_cli(fd, "PL Filter changed to %s\n", (o->plfilter) ? "Enabled" : "Disabled");
		} else {
			ast_cli(fd, "PL Filter is currently %s\n", (o->plfilter) ? "Enabled" : "Disabled");
		}
		break;
	case 'q':					/* change ptt mode */
		if (cmd[1]) {
			if (cmd[1] > '0') {
				o->invertptt = 1;
			} else {
				o->invertptt = 0;
			}
			ast_cli(fd, "PTT mode changed to %s\n", (o->invertptt) ? "Open" : "Ground");
		} else {
			ast_cli(fd, "PTT mode is currently %s\n", (o->plfilter) ? "Open" : "Ground");
		}
		break;
	case 'r': /* change carrier from */
		if (cmd[1]) {
			o->rxcdtype = atoi(&cmd[1]);
			ast_cli(fd, "Carrier From changed to %s\n", cd_signal_type[o->rxcdtype]);
		} else {
			ast_cli(fd, "Carrier From is currently %s\n", cd_signal_type[o->rxcdtype]);
		}
		break;
	case 's': /* change ctcss from */
		if (cmd[1]) {
			o->rxsdtype = atoi(&cmd[1]);
			ast_cli(fd, "CTCSS From changed to %s\n", sd_signal_type[o->rxsdtype]);
		} else {
			ast_cli(fd, "CTCSS From is currently %s\n", sd_signal_type[o->rxsdtype]);
		}
		break;
	case 't':					/* change rx on delay */
		if (cmd[1]) {
			o->rxondelay = atoi(&cmd[1]);
			if (o->rxondelay > MS_TO_FRAMES(RX_ON_DELAY_MAX)) {
				o->rxondelay = MS_TO_FRAMES(RX_ON_DELAY_MAX);
			}
			ast_cli(fd, "RX On Delay From changed to %d\n", o->rxondelay);
		} else {
			ast_cli(fd, "RX On Delay is currently %d\n", o->rxondelay);
		}
		break;
	case 'u':					/* change tx off delay */
		if (cmd[1]) {
			o->txoffdelay = atoi(&cmd[1]);
			if (o->txoffdelay > MS_TO_FRAMES(TX_OFF_DELAY_MAX)) {
				o->txoffdelay = MS_TO_FRAMES(TX_OFF_DELAY_MAX);
			}
			ast_cli(fd, "TX Off Delay From changed to %d\n", o->txoffdelay);
		} else {
			ast_cli(fd, "TX Off Delay is currently %d\n", o->txoffdelay);
		}
		break;
	case 'v':					/* receiver/transmitter status display */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		tune_rxtx_status(fd, o);
		break;
	case 'y':					/* display receive audio statistics (interactive) */
	case 'Y':					/* display receive audio statistics (once only) */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		for (;;) {
			ast_radio_print_audio_stats(fd, &o->rxaudiostats, "Rx");
			if (cmd[0] == 'Y') {
				break;
			}
			if (ast_radio_poll_input(fd, 1000)) {
				break;
			}
		}
		break;
	case 'z': /* display transmit audio statistics (interactive) */
	case 'Z': /* display transmit audio statistics (once only) */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		x = 1;
		for (;;) {
			if (o->txkeyed) {
				ast_radio_print_audio_stats(fd, &o->txaudiostats, "Tx");
				x = 1;
			} else if (x == 1) {
				ast_cli(fd, "Tx not keyed\n");
				x = 0;
			}
			if (cmd[0] == 'Z') {
				break;
			}
			if (ast_radio_poll_input(fd, 1000)) {
				break;
			}
		}
		break;
	default:
		ast_cli(fd, "Invalid Command\n");
		break;
	}
	option_verbose = oldverbose;
}

/*!
 * \brief Store receive carrier detect.
 *	This is the carrier operated relay (COR).
 * \param o				Private struct.
 * \param s				New setting.
 */
static void store_rxcdtype(struct chan_simpleusb_pvt *o, char *s)
{
	if (!strcasecmp(s, "no")) {
		o->rxcdtype = CD_IGNORE;
	} else if (!strcasecmp(s, "usb")) {
		o->rxcdtype = CD_HID;
	} else if (!strcasecmp(s, "usbinvert")) {
		o->rxcdtype = CD_HID_INVERT;
	} else if (!strcasecmp(s, "pp")) {
		o->rxcdtype = CD_PP;
	} else if (!strcasecmp(s, "ppinvert")) {
		o->rxcdtype = CD_PP_INVERT;
	} else {
		ast_log(LOG_WARNING, "Unrecognized rxcdtype parameter: %s\n", s);
	}
	ast_debug(1, "Channel %s: Set rxcdtype = %s.\n", o->name, s);
}

/*!
 * \brief Store receive CTCSS detect.
 *	This is the CTCSS or PL input.
 * \param o				Private struct.
 * \param s				New setting.
 */
static void store_rxsdtype(struct chan_simpleusb_pvt *o, char *s)
{
	if (!strcasecmp(s, "no") || !strcasecmp(s, "SD_IGNORE")) {
		o->rxsdtype = SD_IGNORE;
	} else if (!strcasecmp(s, "usb") || !strcasecmp(s, "SD_HID")) {
		o->rxsdtype = SD_HID;
	} else if (!strcasecmp(s, "usbinvert") || !strcasecmp(s, "SD_HID_INVERT")) {
		o->rxsdtype = SD_HID_INVERT;
	} else if (!strcasecmp(s, "pp")) {
		o->rxsdtype = SD_PP;
	} else if (!strcasecmp(s, "ppinvert")) {
		o->rxsdtype = SD_PP_INVERT;
	} else {
		ast_log(LOG_WARNING, "Unrecognized rxsdtype parameter: %s\n", s);
	}
	ast_debug(1, "Channel %s: Set rxsdtype = %s.\n", o->name, s);
}

/*!
 * \brief Store pager transmit channel.
 *	This is left or right channel.
 * \param o				Private struct.
 * \param s				New setting.
 */
static void store_pager(struct chan_simpleusb_pvt *o, char *s)
{
	if (!strcasecmp(s, "no")) {
		o->pager = PAGER_NONE;
	} else if (!strcasecmp(s, "a")) {
		o->pager = PAGER_A;
	} else if (!strcasecmp(s, "b")) {
		o->pager = PAGER_B;
	} else {
		ast_log(LOG_WARNING, "Unrecognized pager parameter: %s\n", s);
	}
	ast_debug(1, "Channel %s: Set pager = %s\n", o->name, s);
}

/*!
 * \brief Update the ALSA mixer settings
 * Update the ALSA mixer settings.
 *
 * \param		chan_simpleusb structure.
 */
static void mixer_write(struct chan_simpleusb_pvt *o)
{
	int mic_setting;
	float f, f1;

	if (o->duplex3) {
		if (o->duplex3 > o->micplaymax) {
			o->duplex3 = o->micplaymax;
		}
		ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_PLAYBACK_VOL, o->duplex3, 0);
	} else {
		ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_PLAYBACK_VOL, 0, 0);
	}
	ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_PLAYBACK_SW, 0, 0);
	ast_radio_setamixer(o->devicenum, (o->newname) ? MIXER_PARAM_SPKR_PLAYBACK_SW_NEW : MIXER_PARAM_SPKR_PLAYBACK_SW, 1, 0);
	ast_radio_setamixer(o->devicenum, (o->newname) ? MIXER_PARAM_SPKR_PLAYBACK_VOL_NEW : MIXER_PARAM_SPKR_PLAYBACK_VOL,
		ast_radio_make_spkr_playback_value(o->spkrmax, o->txmixaset, o->devtype),
		ast_radio_make_spkr_playback_value(o->spkrmax, o->txmixbset, o->devtype));
	/* adjust settings based on the device */
	if (o->devtype == C119B_PRODUCT_ID) {
		o->rxboost = 1; /*rxboost is always set for this device */
	}
	mic_setting = o->rxmixerset * o->micmax / AUDIO_ADJUSTMENT;
	/* get interval step size */
	f = AUDIO_ADJUSTMENT / (float) o->micmax;

	ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_CAPTURE_VOL, mic_setting, 0);
	ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_BOOST, o->rxboost, 0);
	ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_CAPTURE_SW, 1, 0);
	/* set the received voice adjustment factor */
	o->rxvoiceadj = 1.0 + (modff(((float) o->rxmixerset) / f, &f1) * .187962);
}

/*!
 * \brief Store configuration.
 *	Initializes chan_simpleusb and loads it with the configuration data.
 * \param cfg			ast_config structure.
 * \param ctg			Category.
 * \return				chan_simpleusb_pvt.
 */
static struct chan_simpleusb_pvt *store_config(const struct ast_config *cfg, const char *ctg)
{
	const struct ast_variable *v;
	struct chan_simpleusb_pvt *o;
	char buf[100];
	int i;

	if (ctg == NULL) {
		o = &simpleusb_default;
		ctg = "general";
	} else {
		/* "general" is also the default thing */
		if (strcmp(ctg, "general") == 0) {
			o = &simpleusb_default;
		} else {
			if (!(o = ast_calloc(1, sizeof(*o)))) {
				return NULL;
			}
			*o = simpleusb_default;
			o->name = ast_strdup(ctg);
			o->pttkick[0] = -1;
			o->pttkick[1] = -1;
			if (!simpleusb_active) {
				simpleusb_active = o->name;
			}
		}
	}
	o->echoq.q_forw = o->echoq.q_back = &o->echoq;
	ast_mutex_init(&o->echolock);
	ast_mutex_init(&o->eepromlock);
	ast_mutex_init(&o->txqlock);
	ast_mutex_init(&o->usblock);
	o->echomax = DEFAULT_ECHO_MAX;
	/* fill other fields from configuration */
	for (v = ast_variable_browse(cfg, ctg); v; v = v->next) {
		CV_START((char *) v->name, (char *) v->value);

		/* handle jb conf */
		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value)) {
			continue;
		}

		CV_UINT("frags", o->frags);
		CV_UINT("queuesize", o->queuesize);
		CV_BOOL("invertptt", o->invertptt);
		CV_F("carrierfrom", store_rxcdtype(o, (char *) v->value));
		CV_F("ctcssfrom", store_rxsdtype(o, (char *) v->value));
		CV_BOOL("rxboost", o->rxboost);
		CV_UINT("hdwtype", o->hdwtype);
		CV_UINT("eeprom", o->wanteeprom);
		CV_UINT("rxondelay", o->rxondelay);
		if (o->rxondelay > MS_TO_FRAMES(RX_ON_DELAY_MAX)) {
			o->rxondelay = MS_TO_FRAMES(RX_ON_DELAY_MAX);
		}
		CV_UINT("txoffdelay", o->txoffdelay);
		if (o->txoffdelay > MS_TO_FRAMES(TX_OFF_DELAY_MAX)) {
			o->txoffdelay = MS_TO_FRAMES(TX_OFF_DELAY_MAX);
		}
		CV_F("pager", store_pager(o, (char *) v->value));
		CV_BOOL("plfilter", o->plfilter);
		CV_BOOL("deemphasis", o->deemphasis);
		CV_BOOL("preemphasis", o->preemphasis);
		CV_UINT("duplex3", o->duplex3);
		CV_UINT("clipledgpio", o->clipledgpio);
		CV_BOOL("legacyaudioscaling", o->legacyaudioscaling);
		CV_END;
		
		for (i = 0; i < GPIO_PINCOUNT; i++) {
			sprintf(buf, "gpio%d", i + 1);
			if (!strcmp(v->name, buf)) {
				o->gpios[i] = ast_strdup(v->value);
			}
		}
		for (i = 2; i <= 15; i++) {
			if (!((1 << i) & PP_MASK)) {
				continue;
			}
			sprintf(buf, "pp%d", i);
			if (!strcasecmp(v->name, buf)) {
				o->pps[i] = ast_strdup(v->value);
				haspp = 1;
			}
		}
	}

	if (o == &simpleusb_default) {	/* we are done with the default */
		return NULL;
	}

	for (i = 2; i <= 9; i++) {
		/* skip if this one not specified */
		if (!o->pps[i]) {
			continue;
		}
		/* skip if not out or PTT */
		if (strncasecmp(o->pps[i], "out", 3) && strcasecmp(o->pps[i], "ptt")) {
			continue;
		}
		/* if default value is 1, set it */
		if (!strcasecmp(o->pps[i], "out1")) {
			pp_val |= (1 << (i - 2));
		}
		hasout = 1;
	}

	load_tune_config(o, cfg, 0);

	/* if we are using the EEPROM, request hidthread load the EEPROM */
	if (o->wanteeprom) {
		ast_mutex_lock(&o->eepromlock);
		while (o->eepromctl) {
			ast_mutex_unlock(&o->eepromlock);
			usleep(10000);
			ast_mutex_lock(&o->eepromlock);
		}
		o->eepromctl = 1;		/* request a load */
		ast_mutex_unlock(&o->eepromlock);
	}
	o->dsp = ast_dsp_new();
	if (o->dsp) {
		ast_dsp_set_features(o->dsp, DSP_FEATURE_DIGIT_DETECT);
		ast_dsp_set_digitmode(o->dsp, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
	}
	
	hidhdwconfig(o);

	/* link into list of devices */
	if (o != &simpleusb_default) {
		o->next = simpleusb_default.next;
		simpleusb_default.next = o;
	}
	return o;
}

/*!
 * \brief Turns integer response to char CLI response
 * \param r				Response.
 * \return	CLI success, showusage, or failure.
 */
static char *res2cli(int r)
{
	switch (r) {
	case RESULT_SUCCESS:
		return (CLI_SUCCESS);
	case RESULT_SHOWUSAGE:
		return (CLI_SHOWUSAGE);
	default:
		return (CLI_FAILURE);
	}
}

/*!
 * \brief Handle Asterisk CLI request to key transmitter.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_console_key(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "susb key";
		e->usage = 	"Usage: susb key\n" 
					"       Simulates COR active.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(console_key(a->fd, a->argc, a->argv));
}

/*!
 * \brief Handle Asterisk CLI request to unkey transmitter.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_console_unkey(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "susb unkey";
		e->usage = 	"Usage: susb unkey\n" 
					"       Simulates COR un-active.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(console_unkey(a->fd, a->argc, a->argv));
}

/*!
 * \brief Handle Asterisk CLI request for usb tune command.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_susb_tune(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "susb tune {rx|rxd|txa|txb|flash|swap|load|save|nocap|rxcap|txcap|menu-support}";
		e->usage = 	"Usage: susb tune <function>\n"
					"       rx [newsetting]\n"
					"       rxdisplay\n"
					"       txa [newsetting]\n"
					"       txb [newsetting]\n"
					"       save (settings to tuning file)\n"
					"       load (tuning settings from EEPROM)\n\n"
					"       All [newsetting]'s are values 0-999\n\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(susb_tune(a->fd, a->argc, a->argv));
}

/*!
 * \brief Handle Asterisk CLI request active device command.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_susb_active(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "susb active";
		e->usage =	"Usage: susb active [device-name]\n"
					"       If used without a parameter, displays which device is the current\n"
					"       one being commanded.  If a device is specified, the commanded radio device is changed\n"
					"       to the device specified.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(susb_active(a->fd, a->argc, a->argv));
}

/*!
 * \brief Handle Asterisk CLI request for susb show settings.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_susb_show_settings(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_simpleusb_pvt *o;
	
	switch (cmd) {
	case CLI_INIT:
		e->command = "susb show settings";
		e->usage = 	"Usage: susb show settings\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	
	o = find_desc(simpleusb_active);
	if (o) {
		_menu_print(a->fd, o);
	}
	return RESULT_SUCCESS;
}

static struct ast_cli_entry cli_simpleusb[] = {
	AST_CLI_DEFINE(handle_console_key, "Simulate Rx Signal Present"),
	AST_CLI_DEFINE(handle_console_unkey, "Simulate Rx Signal Loss"),
	AST_CLI_DEFINE(handle_susb_tune, "Change susb settings"),
	AST_CLI_DEFINE(handle_susb_active, "Change commanded device"),
	AST_CLI_DEFINE(handle_susb_show_settings, "Show device settings")
};

/*!
 * \brief Load configuration.
 * \param reload		Flag to indicate if we are reloading.
 * \return				Success or failure.
 */
static int load_config(int reload)
{
	struct ast_config *cfg = NULL;
	char *ctg = NULL, *val;
	struct ast_flags zeroflag = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	/* load config file */
	if (!(cfg = ast_config_load(CONFIG, zeroflag))) {
		ast_log(LOG_NOTICE, "Unable to load config %s.\n", CONFIG);
		return AST_MODULE_LOAD_DECLINE;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		ast_log(LOG_NOTICE, "Config file %s unchanged, skipping.\n", CONFIG);
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file %s is in an invalid format. Aborting.\n", CONFIG);
		return -1;
	}

	/* store the configuration */
	do {
		store_config(cfg, ctg);
	} while ((ctg = ast_category_browse(cfg, ctg)) != NULL);

	/* load parallel port information */
	ppfd = -1;
	pbase = 0;
	val = (char *) ast_variable_retrieve(cfg, "general", "pport");
	if (val) {
		ast_copy_string(pport, val, sizeof(pport) - 1);
	} else {
		strcpy(pport, PP_PORT);
	}
	val = (char *) ast_variable_retrieve(cfg, "general", "pbase");
	if (val) {
		pbase = strtoul(val, NULL, 0);
	}
	if (!pbase) {
		pbase = PP_IOPORT;
	}
	ast_radio_load_parallel_port(&haspp, &ppfd, &pbase, pport, reload);
	ast_config_destroy(cfg);
	return 0;
}

static int reload_module(void)
{
	return load_config(1);
}

static int load_module(void)
{
	if (!(simpleusb_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append(simpleusb_tech.capabilities, ast_format_slin, 0);

	if (ast_radio_hid_device_mklist()) {
		ast_log(LOG_ERROR, "Unable to make hid list\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	simpleusb_active = NULL;

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	pp_val = 0;
	hasout = 0;

	/* load our module configuration */
	if (load_config(0)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (find_desc(simpleusb_active) == NULL) {
		ast_log(LOG_NOTICE, "susb active device %s not found\n", simpleusb_active);
		/* XXX we could default to 'dsp' perhaps ? */
		/* XXX should cleanup allocated memory etc. */
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_channel_register(&simpleusb_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel type 'usb'\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_cli_register_multiple(cli_simpleusb, ARRAY_LEN(cli_simpleusb));

	if (haspp && hasout) {
		ast_pthread_create_background(&pulserid, NULL, pulserthread, NULL);
	}
	
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	struct chan_simpleusb_pvt *o;

	stoppulser = 1;

	ast_channel_unregister(&simpleusb_tech);
	ast_cli_unregister_multiple(cli_simpleusb, ARRAY_LEN(cli_simpleusb));

	for (o = simpleusb_default.next; o; o = o->next) {

#if DEBUG_CAPTURES == 1
		if (frxcapraw) {
			fclose(frxcapraw);
			frxcapraw = NULL;
		}
		if (frxcapcooked) {
			fclose(frxcapraw);
			frxcapcooked = NULL;
		}
		if (ftxcapraw) {
			fclose(ftxcapraw);
			ftxcapraw = NULL;
		}
#endif

		if (o->sounddev >= 0) {
			close(o->sounddev);
			o->sounddev = -1;
		}
		if (o->dsp) {
			ast_dsp_free(o->dsp);
		}
		if (o->owner) {
			ast_softhangup(o->owner, AST_SOFTHANGUP_APPUNLOAD);
		}
		if (o->owner) {			/* XXX how ??? */
			return -1;
		}
		/* XXX what about the thread ? */
		/* XXX what about the memory allocated ? */
	}

	ao2_cleanup(simpleusb_tech.capabilities);
	simpleusb_tech.capabilities = NULL;

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "SimpleUSB Radio Interface Channel Driver",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.requires = "res_usbradio",
);
