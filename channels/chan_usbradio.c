/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 * Copyright (C) 2007 - 2011, Jim Dixon
 *
 * Jim Dixon, WB6NIL <jim@lambdatel.com>
 * Steve Henke, W9SH  <w9sh@arrl.net>
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
 * 20160829      inad            added rxlpf rxhpf txlpf txhpf
 */

/*! \file
 *
 * \brief Channel driver for CM108 USB Cards with Radio Interface
 *
 * \author Jim Dixon  <jim@lambdatel.com>
 * \author Steve Henke  <w9sh@arrl.net>
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

#ifdef RADIO_XPMRX
#define HAVE_XPMRX 1
#endif

#define CHAN_USBRADIO 1 /* Used in xpmr.h to configure that module */
#define DEBUG_USBRADIO 0
#define DEBUG_CAPTURES 1
#define DEBUG_CAP_RX_OUT 0
#define DEBUG_CAP_TX_OUT 0
#define DEBUG_FILETEST 0
#define RX_CAP_RAW_FILE "/tmp/rx_cap_in.pcm"
#define RX_CAP_TRACE_FILE "/tmp/rx_trace.pcm"
#define RX_CAP_OUT_FILE "/tmp/rx_cap_out.pcm"
#define TX_CAP_RAW_FILE "/tmp/tx_cap_in.pcm"
#define TX_CAP_TRACE_FILE "/tmp/tx_trace.pcm"
#define TX_CAP_OUT_FILE "/tmp/tx_cap_out.pcm"

#define DELIMCHR ','
#define QUOTECHR 34

#define READERR_THRESHOLD 50
#define DEFAULT_ECHO_MAX 1000 /* 20 secs of echo buffer, max */
#define DEFAULT_TX_SOFT_LIMITER_SETPOINT 12000
#define PP_MASK 0xbffc
#define PP_PORT "/dev/parport0"
#define PP_IOPORT 0x378
#define RPT_TO_STRING(x) #x
#define S_FMT(x) "%" RPT_TO_STRING(x) "s "
#define N_FMT(duf) "%30" #duf				   /* Maximum sscanf conversion to numeric strings */
#define RX_ON_DELAY_MAX 60000				   /* in ms, 60000ms, 60 seconds, 1 minute */
#define TX_OFF_DELAY_MAX 60000				   /* in ms 60000ms, 60 seconds, 1 minute */
#define MS_PER_FRAME 20						   /* 20 ms frames */
#define MS_TO_FRAMES(ms) ((ms) / MS_PER_FRAME) /* convert ms to frames */

#include "./xpmr/xpmr.h"
#ifdef HAVE_XPMRX
#include "./xpmrx/xpmrx.h"
#include "./xpmrx/bitweight.h"
#endif

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

#define QUEUE_SIZE 20 /* 400 milliseconds of sound card output buffer */

#define CONFIG "usbradio.conf" /* default config file */

/* file handles for writing debug audio packets */
static FILE *frxcapraw = NULL, *frxcaptrace = NULL, *frxoutraw = NULL;
static FILE *ftxcapraw = NULL, *ftxcaptrace = NULL, *ftxoutraw = NULL;

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

/*! \brief type of signal detection used for carrier (cd) or ctcss (sd) */
static const char *const cd_signal_type[] = { "no", "dsp", "vox", "usb", "usbinvert", "pp", "ppinvert" };
static const char *const sd_signal_type[] = { "no", "usb", "usbinvert", "dsp", "pp", "ppinvert" };

/*! \brief demodulation type */
static const char *const demodulation_type[] = { "no", "speaker", "flat" };

/*! \brief mixer type */
static const char *const mixer_type[] = { "no", "voice", "tone", "composite", "auxvoice" };

/*!
 * \brief Descriptor for one of our channels.
 * There is one used for 'default' values (from the [general] entry in
 * the configuration file), and then one instance for each device
 * (the default is cloned from [general], others are only created
 * if the relevant section exists).
 */
struct chan_usbradio_pvt {
	struct chan_usbradio_pvt *next;

	char *name;		  /* the internal name of our channel */
	int devtype;	  /* actual type of device */
	int pttkick[2];	  /* ptt kick pipe */
	int total_blocks; /* total blocks in the output device */
	int sounddev;
	enum {
		M_UNSET,
		M_FULL,
		M_READ,
		M_WRITE
	} duplex;
	int hookstate;
	unsigned int queuesize; /* max fragments in queue */
	unsigned int frags;		/* parameter for SETFRAGMENT */

	int warned; /* various flags used for warnings */
#define WARN_used_blocks 1
#define WARN_speed 2
#define WARN_frag 4

	char devicenum;
	char devstr[128];
	int spkrmax;
	int micmax;
	int micplaymax;

	pthread_t hidthread;
	int stophid;

	struct ast_channel *owner;

	/* buffer used in usbradio_write, 2 per int by 2 channels by 6 times oversampling (48KS/s) */
	char usbradio_write_buf[FRAME_SIZE * 2 * 2 * 6];

	/* buffers used in usbradio_read - AST_FRIENDLY_OFFSET space for headers
	 * plus enough room for a full frame
	 */
	char usbradio_read_buf[FRAME_SIZE * (2 * 12) + AST_FRIENDLY_OFFSET]; /* 2 bytes * 2 channels * 6 for 48K */
	char usbradio_read_buf_8k[FRAME_SIZE * 2 + AST_FRIENDLY_OFFSET];
	int readpos;			 /* read position above */
	struct ast_frame read_f; /* returned by usbradio_read */

	char lastrx;
	char rxhidsq;
	char rxhidctcss;
	char rxcarrierdetect; /* status from pmr channel */
	char rxctcssdecode;	  /* status from pmr channel */
	char rxppsq;
	char rxppctcss;

	char rxkeyed; /* Indicates rx signal is present */

	char lasttx;
	char txkeyed; /* tx key request from upper layers */
	char txtestkey;

	time_t lasthidtime;
	struct ast_dsp *dsp;

	char radioduplex; /* parameter for radio duplex setting */

	char didpmrtx;
	int notxcnt;

	int tracetype;
	int tracelevel;
	char area;
	char rptnum;
	int idleinterval;
	int turnoffs;
	int txsettletime;
	int txrxblankingtime;
	char ukey[48];

	int rxdcsdecode;
	int rxlsddecode;

	int rxoncnt;	/* Counts the number of 20 ms intervals after RX activity */
	int txoffcnt;	/* Counts the number of 20 ms intervals after TX unkey */
	int rxondelay;	/* This is the value which RX is ignored after RX activity */
	int txoffdelay; /* This is the value which RX is ignored after TX unkey */

	t_pmr_chan *pmrChan;

	enum radio_rx_audio rxdemod;
	float rxgain;
	enum radio_carrier_detect rxcdtype;
	int voxhangtime; /* if rxcdtype=vox, ms to wait detecting RX audio before setting CD=0 */
	enum radio_squelch_detect rxsdtype;
	int rxsquelchadj; /* this copy needs to be here for initialization */
	int rxsqhyst;
	int rxsqvoxadj;
	int rxnoisefiltype;
	int rxsquelchdelay;
	int txslimsp;
	enum usbradio_carrier_type txtoctype;

	float txctcssgain;
	enum radio_tx_mix txmixa;
	enum radio_tx_mix txmixb;
	int rxlpf;
	int rxhpf;
	int txlpf;
	int txhpf;

	char rxctcssrelax;
	float rxctcssgain;

	char txctcssdefault[16]; /* for repeater operation */
	char rxctcssfreqs[512];	 /* a string */
	char txctcssfreqs[512];

	char txctcssfreq[32]; /* encode now */
	char rxctcssfreq[32]; /* decode now */

	char numrxctcssfreqs; /* how many */
	char numtxctcssfreqs;

	char *rxctcss[CTCSS_NUM_CODES]; /* pointers to strings */
	char *txctcss[CTCSS_NUM_CODES];

	int txfreq; /* in Hz */
	int rxfreq;

	/*      start remote operation info */
	char set_txctcssdefault[16]; /* for remote operation */
	char set_txctcssfreq[16];	 /* encode now */
	char set_rxctcssfreq[16];	 /* decode now */

	char set_numrxctcssfreqs; /* how many */
	char set_numtxctcssfreqs;

	char set_rxctcssfreqs[16]; /* a string */
	char set_txctcssfreqs[16];

	char *set_rxctcss; /* pointers to strings */
	char *set_txctcss;

	int set_txfreq; /* in Hz */
	int set_rxfreq;

	/*      end remote operation info */

	int rxmixerset;
	int txboost;
	float rxvoiceadj;
	int txmixaset;
	int txmixbset;
	int txctcssadj;

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
	unsigned int rxcap2:1;			/* indicator if receive capture 2 is enabled */
	unsigned int txcap2:1;			/* indicator if transmit capture 2 is enabled */
	unsigned int remoted:1;			/* indicator if rx/tx frequency adjusted */
	unsigned int forcetxcode:1;		/* indicator to force use of first ctcss code */
	unsigned int rxpolarity:1;		/* indicator for receive polarity */
	unsigned int txpolarity:1;		/* indicator for transmit polarity */
	unsigned int dcsrxpolarity:1;	/* indicator for dcs receive polarity */
	unsigned int dcstxpolarity:1;	/* indicator for dcs transmit polarity */
	unsigned int lsdrxpolarity:1;	/* indicator for lsd receive polarity */
	unsigned int lsdtxpolarity:1;	/* indicator for lsd transmit polarity */
	unsigned int radioactive:1;		/* indicator for active radio channel */
	unsigned int device_error:1;	/* indicator set when we cannot find the USB device */
	unsigned int newname:1;			/* indicator that we should use MIXER_PARAM_SPKR_PLAYBACK_VOL_NEW */
	unsigned int hasusb:1;			/* indicator for has a USB device */
	unsigned int usbass:1;			/* indicator for USB device assigned */
	unsigned int wanteeprom:1;		/* indicator if we should use EEPROM */
	unsigned int usedtmf:1;			/* indicator is we should decode DTMF */
	unsigned int invertptt:1;		/* indicator if we need to invert ptt */
	unsigned int rxboost:1;			/* indicator if receive boost is needed */
	unsigned int rxcpusaver:1;		/* indicator if receive cpu save is enabled */
	unsigned int txcpusaver:1;		/* indicator if transmit cpu save is enabled */
	unsigned int txprelim:1;		/* indicator if tx pre lim is enabled */
	unsigned int txlimonly:1;		/* indicator if tx lim only is enabled */
	unsigned int rxctcssoverride:1; /* indicator if receive ctcss override is enabled */
	unsigned int rx_cos_active:1;	/* indicator if cos is active - active state after processing */
	unsigned int rx_ctcss_active:1; /* indicator if ctcss is active - active state after processing */

	/* EEPROM access variables */
	unsigned short eeprom[EEPROM_USER_LEN];
	char eepromctl;
	ast_mutex_t eepromlock;

	struct usb_dev_handle *usb_handle;
	int readerrs;
	struct timeval tonetime;
	int toneflag;
	int duplex3;
	int clipledgpio; /* enables ADC Clip Detect feature to output on a specified GPIO# */

	int fever;
	int count_rssi_update;

	int32_t cur_gpios;
	char *gpios[GPIO_PINCOUNT];
	char *pps[32];
	int sendvoter;

	struct audiostatistics rxaudiostats;
	struct audiostatistics txaudiostats;

	int legacyaudioscaling;

	ast_mutex_t usblock;
};

/*!
 * \brief Default channel descriptor
 */
static struct chan_usbradio_pvt usbradio_default = {
	.sounddev = -1,
	.duplex = M_UNSET,
	.queuesize = QUEUE_SIZE,
	.frags = FRAGS,
	.readpos = AST_FRIENDLY_OFFSET, /* start here on reads */
	.wanteeprom = 1,
	.usedtmf = 1,
	.rxondelay = 0,
	.txoffdelay = 0,
	.voxhangtime = 2000,
	.area = 0,
	.rptnum = 0,
	.clipledgpio = 0,
	.rxaudiostats.index = 0,
	/* After the vast majority of existing installs have had a chance to review their
	   audio settings and the associated old scaling/clipping hacks are no longer in
	   significant use the following cfg and all related code should be deleted. */
	.legacyaudioscaling = 1,
};

/*	DECLARE FUNCTION PROTOTYPES	*/

static int hidhdwconfig(struct chan_usbradio_pvt *o);
static void mixer_write(struct chan_usbradio_pvt *o);
static int setformat(struct chan_usbradio_pvt *o, int mode);
static struct ast_channel *usbradio_request(const char *type, struct ast_format_cap *cap,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static int usbradio_digit_begin(struct ast_channel *c, char digit);
static int usbradio_digit_end(struct ast_channel *c, char digit, unsigned int duration);
static int usbradio_text(struct ast_channel *c, const char *text);
static int usbradio_hangup(struct ast_channel *c);
static int usbradio_answer(struct ast_channel *c);
static struct ast_frame *usbradio_read(struct ast_channel *chan);
static int usbradio_call(struct ast_channel *c, const char *dest, int timeout);
static int usbradio_write(struct ast_channel *chan, struct ast_frame *f);
static int usbradio_indicate(struct ast_channel *chan, int cond_in, const void *data, size_t datalen);
static int usbradio_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int usbradio_setoption(struct ast_channel *chan, int option, void *data, int datalen);
static void store_rxvoiceadj(struct chan_usbradio_pvt *o, const char *s);
static int set_txctcss_level(struct chan_usbradio_pvt *o);
static void pmrdump(struct chan_usbradio_pvt *o, int fd);
static void mult_set(struct chan_usbradio_pvt *o);
static int mult_calc(int value);
static void tune_rxinput(int fd, struct chan_usbradio_pvt *o, int setsql, int flag);
static void tune_rxvoice(int fd, struct chan_usbradio_pvt *o, int flag);
static void tune_menusupport(int fd, struct chan_usbradio_pvt *o, const char *cmd);
static void tune_rxctcss(int fd, struct chan_usbradio_pvt *o, int flag);
static void tune_txoutput(struct chan_usbradio_pvt *o, int value, int fd, int flag);
static void tune_write(struct chan_usbradio_pvt *o);
static int xpmr_config(struct chan_usbradio_pvt *o);
static int xpmr_set_tx_soft_limiter(struct chan_usbradio_pvt *o, int setpoint);
#if DEBUG_FILETEST == 1
static int RxTestIt(struct chan_usbradio_pvt *o);
#endif

static char *usbradio_active; /* the active device */

static const int ppinshift[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 7, 5, 4, 0, 3 };

static const char tdesc[] = "USB (CM108) Radio Channel Driver";

/*!
 * \brief Asterisk channel technology struct.
 * This tells Asterisk the functions to call when
 * it needs to interact with our module.
 */
static struct ast_channel_tech usbradio_tech = {
	.type = "Radio",
	.description = tdesc,
	.requester = usbradio_request,
	.send_digit_begin = usbradio_digit_begin,
	.send_digit_end = usbradio_digit_end,
	.send_text = usbradio_text,
	.hangup = usbradio_hangup,
	.answer = usbradio_answer,
	.read = usbradio_read,
	.call = usbradio_call,
	.write = usbradio_write,
	.indicate = usbradio_indicate,
	.fixup = usbradio_fixup,
	.setoption = usbradio_setoption,
};

/*!
 * \brief Configure our private structure based on the
 * found hardware type.
 * \param o		Pointer chan_usbradio_pvt.
 * \returns 0	Always returns zero.
 */
static int hidhdwconfig(struct chan_usbradio_pvt *o)
{
	int i;

	/* NOTE: on the CM-108AH, GPIO2 is *not* a REAL GPIO.. it was re-purposed
	 *  as a signal called "HOOK" which can only be read from the HID.
	 *  Apparently, in a REAL CM-108, GPIO really works as a GPIO
	 */

	if (o->hdwtype == 1) {		  // sphusb
		o->hid_gpio_ctl = 0x08;	  /* set GPIO4 to output mode */
		o->hid_gpio_ctl_loc = 2;  /* For CTL of GPIO */
		o->hid_io_cor = 4;		  /* GPIO3 is COR */
		o->hid_io_cor_loc = 1;	  /* GPIO3 is COR */
		o->hid_io_ctcss = 2;	  /* GPIO 2 is External CTCSS */
		o->hid_io_ctcss_loc = 1;  /* is GPIO 2 */
		o->hid_io_ptt = 8;		  /* GPIO 4 is PTT */
		o->hid_gpio_loc = 1;	  /* For ALL GPIO */
		o->valid_gpios = 1;		  /* for GPIO 1 */
	} else if (o->hdwtype == 0) { // dudeusb
		o->hid_gpio_ctl = 4;	  /* set GPIO 3 to output mode */
		o->hid_gpio_ctl_loc = 2;  /* For CTL of GPIO */
		o->hid_io_cor = 2;		  /* VOLD DN is COR */
		o->hid_io_cor_loc = 0;	  /* VOL DN COR */
		o->hid_io_ctcss = 1;	  /* VOL UP External CTCSS */
		o->hid_io_ctcss_loc = 0;  /* VOL UP External CTCSS */
		o->hid_io_ptt = 4;		  /* GPIO 3 is PTT */
		o->hid_gpio_loc = 1;	  /* For ALL GPIO */
		o->valid_gpios = 0xfb;	  /* for GPIO 1,2,4,5,6,7,8 (5,6,7,8 for CM-119 only) */
	} else if (o->hdwtype == 2) { // NHRC (N1KDO) (dudeusb w/o user GPIO)
		o->hid_gpio_ctl = 4;	  /* set GPIO 3 to output mode */
		o->hid_gpio_ctl_loc = 2;  /* For CTL of GPIO */
		o->hid_io_cor = 2;		  /* VOLD DN is COR */
		o->hid_io_cor_loc = 0;	  /* VOL DN COR */
		o->hid_io_ctcss = 1;	  /* VOL UP is External CTCSS */
		o->hid_io_ctcss_loc = 0;  /* VOL UP CTCSS */
		o->hid_io_ptt = 4;		  /* GPIO 3 is PTT */
		o->hid_gpio_loc = 1;	  /* For ALL GPIO */
		o->valid_gpios = 0;		  /* for GPIO 1,2,4 */
	} else if (o->hdwtype == 3) { // custom version
		o->hid_gpio_ctl = 0x0c;	  /* set GPIO 3 & 4 to output mode */
		o->hid_gpio_ctl_loc = 2;  /* For CTL of GPIO */
		o->hid_io_cor = 2;		  /* VOLD DN is COR */
		o->hid_io_cor_loc = 0;	  /* VOL DN COR */
		o->hid_io_ctcss = 2;	  /* GPIO 2 is External CTCSS */
		o->hid_io_ctcss_loc = 1;  /* is GPIO 2 */
		o->hid_io_ptt = 4;		  /* GPIO 3 is PTT */
		o->hid_gpio_loc = 1;	  /* For ALL GPIO */
		o->valid_gpios = 1;		  /* for GPIO 1 */
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
		o->hid_gpio_ctl |= (1 << i); /* set this one to output, also */
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
static void kickptt(const struct chan_usbradio_pvt *o)
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
static struct chan_usbradio_pvt *find_desc(const char *dev)
{
	struct chan_usbradio_pvt *o = NULL;

	for (o = usbradio_default.next; o && o->name && dev && strcmp(o->name, dev) != 0; o = o->next)
		;
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
static struct chan_usbradio_pvt *find_desc_usb(const char *devstr)
{
	struct chan_usbradio_pvt *o = NULL;

	if (!devstr) {
		ast_log(LOG_WARNING, "USB Descriptor is null.\n");
	}

	for (o = usbradio_default.next; o && devstr && strcmp(o->devstr, devstr) != 0; o = o->next)
		;

	return o;
}

/*!
 * \brief Search installed devices for a match with
 *	one of our configured channels.
 * \returns		Matching device string, or NULL.
 */
static char *find_installed_usb_match(void)
{
	struct chan_usbradio_pvt *o = NULL;
	char *match = NULL;

	for (o = usbradio_default.next; o; o = o->next) {
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
		if (pp_pulsemask != pp_lastmask) { /* if anything inverted (temporarily) */
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
static int load_tune_config(struct chan_usbradio_pvt *o, const struct ast_config *cfg, int reload)
{
	struct ast_variable *v;
	struct ast_config *cfg2;
	int opened = 0;
	int configured = 0;
	char devstr[sizeof(o->devstr)];

	/* No load defaults */
	o->rxmixerset = 500;
	o->txmixaset = 500;
	o->txmixbset = 500;
	o->rxvoiceadj = 0.5;
	o->txctcssadj = 200;
	o->rxsquelchadj = 500;
	o->txslimsp = DEFAULT_TX_SOFT_LIMITER_SETPOINT;

	devstr[0] = '\0';
	if (!reload) {
		o->devstr[0] = 0;
	}

	if (!cfg) {
		struct ast_flags zeroflag = { 0 };
		cfg2 = ast_config_load(CONFIG, zeroflag);
		if (!cfg2) {
			ast_log(LOG_WARNING, "Can't %sload settings for %s, using default parameters\n", reload ? "re" : "", o->name);
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
		CV_F("rxvoiceadj", store_rxvoiceadj(o, v->value));
		CV_UINT("txctcssadj", o->txctcssadj);
		CV_UINT("rxsquelchadj", o->rxsquelchadj);
		CV_UINT("txslimsp", o->txslimsp);
		CV_UINT("fever", o->fever);
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
		ast_log(LOG_WARNING, "Can't %sload settings for %s (no section available), using default parameters\n", reload ? "re" : "", o->name);
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
 * occurs every 50 milliseconds.
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
 * \param argv		chan_usbradio_pvt structure associated with this thread.
 */
static void *hidthread(void *arg)
{
	unsigned char buf[4], bufsave[4], keyed, ctcssed;
	char *s, lasttxtmp;
	register int i, j, k;
	int res;
	struct usb_device *usb_dev;
	struct usb_dev_handle *usb_handle;
	struct chan_usbradio_pvt *o = arg, *ao;
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
						ast_log(LOG_ERROR, "Channel %s: No USB devices are available for assignment.\n", o->name);
						o->device_error = 1;
					}
					ast_mutex_unlock(&usb_dev_lock);
					usleep(500000);
					break;
				}
				/* We found an available device - see if it already in use */
				for (ao = usbradio_default.next; ao && ao->name; ao = ao->next) {
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
				ast_log(LOG_NOTICE, "Channel %s: Automatically assigned USB device %s to USBRadio channel\n", o->name, o->devstr);
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
					ast_log(LOG_ERROR, "Channel %s: Device string %s was not found.\n", o->name, o->devstr);
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
			for (ao = usbradio_default.next; ao && ao->name; ao = ao->next) {
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
			ast_log(LOG_NOTICE, "Channel %s: Assigned USB device %s to usbradio channel\n", o->name, s);
			ast_copy_string(o->devstr, s, sizeof(o->devstr));
		}
		/* Double check to see if the device string is assigned to another usb channel */
		for (ao = usbradio_default.next; ao && ao->name; ao = ao->next) {
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
		/*! \todo this code does not appear to serve any purpose and can be removed after testing */
#if 0
		for (aop = &usbradio_default.next; *aop && (*aop)->name; aop = &((*aop)->next)) {
			if (strcmp((*(aop))->name, o->name)) {
				continue;
			}
			o->next = (*(aop))->next;
			*aop = o;
			break;
		}
#endif
		o->device_error = 0;
		ast_radio_time(&o->lasthidtime);
		o->usbass = 1;
		ast_mutex_unlock(&usb_dev_lock);
		/* set the audio mixer values */
		o->micmax = ast_radio_amixer_max(o->devicenum, MIXER_PARAM_MIC_CAPTURE_VOL);
		o->spkrmax = ast_radio_amixer_max(o->devicenum, MIXER_PARAM_SPKR_PLAYBACK_VOL);
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
		/* setup the xmpr subsystem */
		if (o->pmrChan == NULL) {
			t_pmr_chan tChan;

			// ast_log(LOG_NOTICE,"createPmrChannel() %s\n",o->name);
			memset(&tChan, 0, sizeof(t_pmr_chan));

			tChan.pTxCodeDefault = o->txctcssdefault;
			tChan.pRxCodeSrc = o->rxctcssfreqs;
			tChan.pTxCodeSrc = o->txctcssfreqs;

			tChan.rxDemod = o->rxdemod;
			tChan.rxCdType = o->rxcdtype;
			tChan.voxHangTime = o->voxhangtime;
			tChan.rxSqVoxAdj = o->rxsqvoxadj;

			if (o->txlimonly) {
				tChan.txMod = 1;
			}
			if (o->txprelim) {
				tChan.txMod = 2;
			}

			tChan.txMixA = o->txmixa;
			tChan.txMixB = o->txmixb;

			tChan.rxCpuSaver = o->rxcpusaver;
			tChan.txCpuSaver = o->txcpusaver;

			tChan.b.rxpolarity = o->rxpolarity;
			tChan.b.txpolarity = o->txpolarity;

			tChan.b.dcsrxpolarity = o->dcsrxpolarity;
			tChan.b.dcstxpolarity = o->dcstxpolarity;

			tChan.b.lsdrxpolarity = o->lsdrxpolarity;
			tChan.b.lsdtxpolarity = o->lsdtxpolarity;

			tChan.tracetype = o->tracetype;
			tChan.tracelevel = o->tracelevel;

			tChan.rptnum = o->rptnum;
			tChan.idleinterval = o->idleinterval;
			tChan.turnoffs = o->turnoffs;
			tChan.area = o->area;
			tChan.ukey = o->ukey;
			tChan.name = o->name;
			tChan.b.txboost = o->txboost;
			tChan.fever = o->fever;

			o->pmrChan = createPmrChannel(&tChan, FRAME_SIZE);

			o->pmrChan->radioDuplex = o->radioduplex;
			o->pmrChan->b.loopback = 0;
			o->pmrChan->b.radioactive = o->radioactive;
			o->pmrChan->txsettletime = o->txsettletime;
			o->pmrChan->txrxblankingtime = o->txrxblankingtime;
			o->pmrChan->rxCpuSaver = o->rxcpusaver;
			o->pmrChan->txCpuSaver = o->txcpusaver;

			*(o->pmrChan->prxSquelchAdjust) = ((999 - o->rxsquelchadj) * 32767) / AUDIO_ADJUSTMENT;
			*(o->pmrChan->prxVoiceAdjust) = o->rxvoiceadj * M_Q8;
			o->pmrChan->rxCtcss->relax = o->rxctcssrelax;
			o->pmrChan->txTocType = o->txtoctype;

			if ((o->txmixa == TX_OUT_LSD) || (o->txmixa == TX_OUT_COMPOSITE) || (o->txmixb == TX_OUT_LSD) || (o->txmixb == TX_OUT_COMPOSITE)) {
				set_txctcss_level(o);
			}

			if ((o->txmixa != TX_OUT_VOICE) && (o->txmixb != TX_OUT_VOICE) && (o->txmixa != TX_OUT_COMPOSITE) && (o->txmixb != TX_OUT_COMPOSITE)) {
				ast_log(LOG_ERROR, "Channel %s: No txvoice output configured.\n", o->name);
			}

			if (o->txctcssfreq[0] && o->txmixa != TX_OUT_LSD && o->txmixa != TX_OUT_COMPOSITE && o->txmixb != TX_OUT_LSD &&
				o->txmixb != TX_OUT_COMPOSITE) {
				ast_log(LOG_ERROR, "No txtone output configured.\n");
			}

			if (o->radioactive) {
				struct chan_usbradio_pvt *ao;
				for (ao = usbradio_default.next; ao && ao->name; ao = ao->next)
					ao->pmrChan->b.radioactive = 0;
				usbradio_active = o->name;
				o->pmrChan->b.radioactive = 1;
				ast_log(LOG_NOTICE, "radio active set to [%s]\n", o->name);
			}
		}
		xpmr_config(o);
		mixer_write(o);
		mult_set(o);

		/* reload the settings from the tune file */
		load_tune_config(o, NULL, 1);

		mixer_write(o);
		mult_set(o);
		set_txctcss_level(o);
		/* Sync soft limiter level in xpmr with what we read from the tuning config. */
		if (xpmr_set_tx_soft_limiter(o, o->txslimsp)) {
			/* Invalid setting in config file. Set default */
			ast_log(LOG_WARNING, "Invalid value for txslimsp in radio settings section of usbradio.c, using default");
			o->txslimsp = DEFAULT_TX_SOFT_LIMITER_SETPOINT;
			xpmr_set_tx_soft_limiter(o, o->txslimsp);
		}

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
		 * This loop process every 50 milliseconds.
		 * The timer can be interrupted by writing to
		 * the pttkick pipe.
		 */
		while ((!o->stophid) && o->hasusb) {
			then = ast_radio_tvnow();
			/* poll the pttkick pipe - timeout after 50 milliseconds */
			res = ast_poll(rfds, 1, 50);
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
				if (o->eepromctl == 1) { /* to read */
					/* if CS okay */
					if (!ast_radio_get_eeprom(usb_handle, o->eeprom)) {
						if (o->eeprom[EEPROM_USER_MAGIC_ADDR] != EEPROM_MAGIC) {
							ast_log(LOG_ERROR, "Channel %s: EEPROM bad magic number\n", o->name);
						} else {
							o->rxmixerset = o->eeprom[EEPROM_USER_RXMIXERSET];
							o->txmixaset = o->eeprom[EEPROM_USER_TXMIXASET];
							o->txmixbset = o->eeprom[EEPROM_USER_TXMIXBSET];
							memcpy(&o->rxvoiceadj, &o->eeprom[EEPROM_USER_RXVOICEADJ], sizeof(float));
							o->txctcssadj = o->eeprom[EEPROM_USER_TXCTCSSADJ];
							o->rxsquelchadj = o->eeprom[EEPROM_USER_RXSQUELCHADJ];
							ast_log(LOG_NOTICE, "Channel %s: EEPROM Loaded\n", o->name);
							mixer_write(o);
							mult_set(o);
							set_txctcss_level(o);
						}
					} else {
						ast_log(LOG_ERROR, "Channel %s: USB adapter has no EEPROM installed or Checksum is bad\n", o->name);
					}
					ast_radio_hid_set_outputs(usb_handle, bufsave);
				}
				if (o->eepromctl == 2) { /* to write */
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
			/* Get the GPIO information */
			j = buf[o->hid_gpio_loc];
			/* If this device is a CM108AH, map the "HOOK" bit (which used to
			   be GPIO2 in the CM108 into the GPIO position */
			if (o->devtype == C108AH_PRODUCT_ID) {
				j |= 2; /* set GPIO2 bit */
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
				j &= ~(1 << i); /* clear the bit, since its not an input */
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
						snprintf(buf1, sizeof(buf1), "GPIO%d %d\n", i + 1, (j & (1 << i)) ? 1 : 0);
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
				j = k = ast_radio_ppread(haspp, ppfd, pbase, pport) ^ 0x80; /* get PP input */
				ast_mutex_unlock(&pp_lock);
				for (i = 10; i <= 15; i++) {
					/* if a valid input bit, dont clear it */
					if ((o->pps[i]) && (!strcasecmp(o->pps[i], "in")) && (PP_MASK & (1 << i))) {
						continue;
					}
					j &= ~(1 << ppinshift[i]); /* clear the bit, since its not an input */
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
							snprintf(buf1, sizeof(buf1), "PP%d %d\n", i, (j & (1 << ppinshift[i])) ? 1 : 0);
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
						j = k & (1 << ppinshift[i]); /* set the bit accordingly */
						if (j != o->rxppsq) {
							ast_debug(2, "Channel %s: update rxppsq = %d\n", o->name, j);
							o->rxppsq = j;
						}
					} else if ((o->pps[i]) && (!strcasecmp(o->pps[i], "ctcss")) && (PP_MASK & (1 << i))) {
						o->rxppctcss = k & (1 << ppinshift[i]); /* set the bit accordingly */
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
			if (o->hid_gpio_pulsemask || o->hid_gpio_lastmask) { /* if anything inverted (temporarily) */
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
			/* if change in tx state as controlled by xpmr */
			lasttxtmp = o->pmrChan->txPttOut;

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
					k |= (1 << (i - 2)); /* make mask */
				}
			}
			if (o->lasttx != lasttxtmp) {
				o->pmrChan->txPttHid = o->lasttx = lasttxtmp;
				ast_debug(2, "Channel %s: tx set to %d\n", o->name, o->lasttx);
				o->hid_gpio_val &= ~o->hid_io_ptt;
				ast_mutex_lock(&pp_lock);
				if (k) {
					pp_val &= ~k;
				}
				if (!o->invertptt) {
					if (lasttxtmp) {
						o->hid_gpio_val |= o->hid_io_ptt;
						if (k) {
							pp_val |= k;
						}
					}
				} else {
					if (!lasttxtmp) {
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
		lasttxtmp = o->pmrChan->txPttOut = 0;
		o->lasttx = 0;
		ast_mutex_lock(&o->usblock);
		o->hid_gpio_val &= ~o->hid_io_ptt;
		if (o->invertptt) {
			o->hid_gpio_val |= o->hid_io_ptt;
		}
		buf[o->hid_gpio_loc] = o->hid_gpio_val ^ o->hid_gpio_pulsemask;
		buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
		ast_radio_hid_set_outputs(usb_handle, buf);
		ast_mutex_unlock(&o->usblock);
	}
	/* clean up before exiting the thread */
	lasttxtmp = o->pmrChan->txPttOut = 0;
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
static int used_blocks(struct chan_usbradio_pvt *o)
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
		ast_debug(1, "Channel %s: fragment total %d, size %d, available %d, bytes %d\n", o->name, info.fragstotal, info.fragsize,
			info.fragments, info.bytes);
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
 * \param o		chan_usbradio_pvt.
 * \param data	Audio data to write.
 * \returns		Number bytes written.
 */
static int soundcard_writeframe(struct chan_usbradio_pvt *o, short *data)
{
	int res;
	short outbuf[FRAME_SIZE * 2 * 6];

	/* If the sound device is not open, setformat will open the device */
	if (o->sounddev < 0) {
		setformat(o, O_RDWR);
	}
	if (o->sounddev < 0) {
		return 0; /* not fatal */
	}
	/*  This may or may not be a good thing
	 *  drop the frame if not transmitting, this keeps from gradually
	 *  filling the buffer when asterisk clock > usb sound clock
	 */
	if (!o->pmrChan->txPttIn && !o->pmrChan->txPttOut) {
		return 0;
	}
	/*
	 * Nothing complex to manage the audio device queue.
	 * If the buffer is full just drop the extra, otherwise write.
	 * In some cases it might be useful to write anyways after
	 * a number of failures, to restart the output chain.
	 */
	res = used_blocks(o);
	if (res > o->queuesize) { /* no room to write a block */
		/* Only report a buffer overflow when we are transmitting */
		if (o->pmrChan->txPttIn || o->pmrChan->txPttOut) {
			ast_log(LOG_WARNING, "Channel %s: Sound device write buffer overflow - used %d blocks\n", o->name, res);
		}
		return 0;
	}
	if (res == 0) { /* We are not keeping the buffer full, add 1 frame */
		memset(outbuf, 0, sizeof(outbuf));
		res = write(o->sounddev, ((void *) outbuf), sizeof(outbuf));
		if (res < 0) {
			ast_log(LOG_ERROR, "Channel %s: Sound card write error %s\n", o->name, strerror(errno));
		}
		ast_debug(7, "A null frame has been added");
	}
	res = write(o->sounddev, ((void *) data), FRAME_SIZE * 2 * 2 * 6);
	if (res < 0) {
		ast_log(LOG_ERROR, "Channel %s: Sound card write error %s\n", o->name, strerror(errno));
	} else if (res != FRAME_SIZE * 2 * 2 * 6) {
		ast_log(LOG_ERROR, "Channel %s: Sound card wrote %d bytes of %d\n", o->name, res, (FRAME_SIZE * 2 * 2 * 6));
	}

	return res;
}

/*!
 * \brief Open the sound card device.
 * If the device is already open, this will close the device
 * and open it again.
 * It initializes the device based on our requirements and triggers
 * reads and writes.
 * \param o		Pointer chan_usbradio_pvt.
 * \param mode	The mode to open the file.  This is the flags argument to open.
 * \retval 0	Success.
 * \retval -1	Failed.
 */
static int setformat(struct chan_usbradio_pvt *o, int mode)
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
	if (mode == O_CLOSE) /* we are done */
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
	fmt = desired = 48000; /* 48000 Hz desired */
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
static int usbradio_digit_begin(struct ast_channel *c, char digit)
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
static int usbradio_digit_end(struct ast_channel *c, char digit, unsigned int duration)
{
	/* no better use for received digits than print them */
	ast_verbose(" << Console Received digit %c of duration %u ms >> \n", digit, duration);
	return 0;
}

/*!
 * \brief Asterisk text function.
 * \note SETFREQ - sets spi programmable transceiver
 *  	 SETCHAN - sets binary parallel transceiver
 * \param c				Asterisk channel.
 * \param text			Text message to process.
 * \retval 0			If successful.
 * \retval -1			If unsuccessful.
 */
static int usbradio_text(struct ast_channel *c, const char *text)
{
	struct chan_usbradio_pvt *o = ast_channel_tech_pvt(c);
	char *cmd, pwr;
	int cnt, i, j;
	double tx, rx;
#define STR_SZ 15 /* Size of text strings */
	char rxs[STR_SZ + 1], txs[STR_SZ + 1], txpl[STR_SZ + 1], rxpl[STR_SZ + 1];

#ifdef HAVE_SYS_IO
	if (haspp == 2) {
		ioperm(pbase, 2, 1);
	}
#endif

	cmd = ast_alloca(strlen(text) + 10);

	/* print received messages */
	ast_debug(3, "Channel %s: Console Received usbradio text %s >>\n", o->name, text);

	cnt = sscanf(text, "%s " S_FMT(STR_SZ) S_FMT(STR_SZ) S_FMT(STR_SZ) S_FMT(STR_SZ) "%c", cmd, rxs, txs, rxpl, txpl, &pwr);

	/* set channel on parallel port */
	if (strcmp(cmd, "SETCHAN") == 0) {
		u8 chan;
		chan = strtod(rxs, NULL);
		ppbinout(chan);
		ast_debug(3, "Channel %s: SETCHAN cmd: %s chan: %i\n", o->name, text, chan);
		return 0;
	}

	/* set receive CTCSS */
	if (strcmp(cmd, "RXCTCSS") == 0) {
		u8 x;
		x = strtod(rxs, NULL);
		o->rxctcssoverride = !x;
		ast_debug(3, "Channel %s: RXCTCSS cmd: %s\n", o->name, text);
		return 0;
	}

	/* set transmit CTCSS */
	if (strcmp(cmd, "TXCTCSS") == 0) {
		u8 x;
		x = strtod(rxs, NULL);
		if (o && o->pmrChan) {
			o->pmrChan->b.txCtcssOff = !x;
		}
		ast_debug(3, "Channel %s: TXCTCSS cmd: %s\n", o->name, text);
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
		if (j > 1) { /* if to request pulse-age */
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
		if (j > 1) { /* if to request pulse-age */
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

	if (cnt < 6) {
		ast_log(LOG_ERROR, "Channel %s: Cannot parse usbradio text: %s\n", o->name, text);
		return 0;
	} else {
		ast_debug(3, "Channel %s: << %s %s %s %s %s %c >> \n", o->name, cmd, rxs, txs, rxpl, txpl, pwr);
	}

	/* set frequency command */
	if (strcmp(cmd, "SETFREQ") == 0) {
		ast_debug(3, "Channel %s: SETFREQ cmd: %s\n", o->name, text);
		tx = strtod(txs, NULL);
		rx = strtod(rxs, NULL);
		o->set_txfreq = round(tx * (double) 1000000);
		o->set_rxfreq = round(rx * (double) 1000000);
		o->pmrChan->txpower = (pwr == 'H');
		strcpy(o->set_rxctcssfreqs, rxpl); /* Safe */
		strcpy(o->set_txctcssfreqs, txpl); /* Safe */

		o->remoted = 1;
		xpmr_config(o);
		return 0;
	}
	ast_log(LOG_ERROR, "Channel %s: Cannot parse usbradio cmd: %s\n", o->name, text);
	return 0;
}

/*!
 * \brief USBRadio call.
 * \param c				Asterisk channel.
 * \param dest			Destination.
 * \param timeout		Timeout.
 * \retval -1 			if not successful.
 * \retval 0 			if successful.
 */
static int usbradio_call(struct ast_channel *c, const char *dest, int timeout)
{
	struct chan_usbradio_pvt *o = ast_channel_tech_pvt(c);

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
static int usbradio_answer(struct ast_channel *c)
{
	ast_setstate(c, AST_STATE_UP);
	return 0;
}

/*!
 * \brief Asterisk hangup function.
 * \param c			Asterisk channel.
 * \retval 0		Always returns 0.
 */
static int usbradio_hangup(struct ast_channel *c)
{
	struct chan_usbradio_pvt *o = ast_channel_tech_pvt(c);

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
static int usbradio_write(struct ast_channel *c, struct ast_frame *f)
{
	struct chan_usbradio_pvt *o = ast_channel_tech_pvt(c);

	if (!o->hasusb) {
		return 0;
	}
	if (o->sounddev < 0) {
		setformat(o, O_RDWR);
	}
	if (o->sounddev < 0) {
		return 0; /* not fatal */
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
			tbuff[i + 1] = o->txkeyed * M_Q13;
		}
		fwrite(tbuff, 2, f->datalen, ftxcapraw);
	}
#endif

	/* take the data from the network and save it for PmrTx processing */
	if (!o->echoing) {
		PmrTx(o->pmrChan, (short *) f->data.ptr);
		o->didpmrtx = 1;
	}

	return 0;
}

/*!
 * \brief Asterisk read function.
 * \param ast			Asterisk channel.
 * \retval 				Asterisk frame.
 */
static struct ast_frame *usbradio_read(struct ast_channel *c)
{
	int res, oldpttout;
	int cd, sd;
	struct chan_usbradio_pvt *o = ast_channel_tech_pvt(c);
	struct ast_frame *f = &o->read_f, *f1;
	time_t now;

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
			if (o->duplex3) {
				ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_PLAYBACK_SW, 0, 0);
			}
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
			PmrTx(o->pmrChan, u->data);
			o->didpmrtx = 1;
			ast_free(u);
			o->echoing = 1;
		} else {
			o->echoing = 0;
		}
		ast_mutex_unlock(&o->echolock);
	}

	/* Read audio data from the USB sound device.
	 * Sound data will arrive at 48000 samples per second
	 * in stereo format.
	 */
	res = read(o->sounddev, o->usbradio_read_buf + o->readpos, sizeof(o->usbradio_read_buf) - o->readpos);
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
		fwrite(o->usbradio_read_buf + o->readpos, 1, res, frxcapraw);
#endif

	if (o->readerrs) {
		ast_log(LOG_WARNING, "USB read channel [%s] was not stuck.\n", o->name);
	}

	o->readerrs = 0;
	o->readpos += res;
	if (o->readpos < sizeof(o->usbradio_read_buf)) { /* not enough samples */
		return &ast_null_frame;
	}

	/* Check for ADC clipping and input audio statistics before any filtering is done.
	 * FRAME_SIZE define refers to 8Ksps mono which is 160 samples per 20mS USB frame.
	 * ast_radio_check_audio() takes the read buffer as received (48K stereo),
	 * extracts the mono 48K channel, checks amplitude and distortion characteristics,
	 * and returns true if clipping was detected.
	 */
	if (ast_radio_check_audio((short *) o->usbradio_read_buf, &o->rxaudiostats, 12 * FRAME_SIZE)) {
		if (o->clipledgpio) {
			/* Set Clip LED GPIO pulsetimer if not already set */
			if (!o->hid_gpio_pulsetimer[o->clipledgpio - 1]) {
				o->hid_gpio_pulsetimer[o->clipledgpio - 1] = CLIP_LED_HOLD_TIME_MS;
			}
		}
	}

	/* Below is an attempt to match levels to the original CM108 IC which has been
	 * out of production for over 10 years. Scaling all rx audio to 80% results in a 20%
	 * loss in dynamic range, added quantization noise, a 2dB reduction in outgoing IAX
	 * audio levels, and inconsistency with Simpleusb. Adjustments for CM1xxx IC gain
	 * differences should be made in the mixer settings, not in the audio stream.
	 * TODO: After the vast majority of existing installs have had a chance to review their
	 * audio settings and these old scaling/clipping hacks are no longer in significant use
	 * the legacyaudioscaling cfg and related code should be deleted.
	 */
	/* Decrease the audio level for CM119 A/B devices */
	if (o->legacyaudioscaling && o->devtype != C108_PRODUCT_ID) {
		/* Subtract res from o->readpos in below assignment (o->readpos was incremented
		   above prior to check of if enough samples were received) */
		register short *sp = (short *) (o->usbradio_read_buf + (o->readpos - res));
		register float v;
		register int i;

		for (i = 0; i < res / 2; i++) {
			v = ((float) *sp) * 0.800;
			*sp++ = (int) v;
		}
	}

#if 1
	if (o->txkeyed || o->txtestkey || o->echoing) {
		if (!o->pmrChan->txPttIn) {
			o->pmrChan->txPttIn = 1;
			ast_debug(3, "Channel %s: txPttIn = %i.\n", o->name, o->pmrChan->txPttIn);
		}
	} else if (o->pmrChan->txPttIn) {
		o->pmrChan->txPttIn = 0;
		ast_debug(3, "Channel %s: txPttIn = %i.\n", o->name, o->pmrChan->txPttIn);
	}
	oldpttout = o->pmrChan->txPttOut;

	if (oldpttout && (!o->didpmrtx)) {
		if (o->notxcnt > 1) {
			memset(o->usbradio_write_buf, 0, sizeof(o->usbradio_write_buf));
			PmrTx(o->pmrChan, (i16 *) o->usbradio_write_buf);
		} else {
			o->notxcnt++;
		}
	} else {
		o->notxcnt = 0;
	}
	o->didpmrtx = 0;

	PmrRx(o->pmrChan, (i16 *) (o->usbradio_read_buf + AST_FRIENDLY_OFFSET),
		(i16 *) (o->usbradio_read_buf_8k + AST_FRIENDLY_OFFSET), (i16 *) (o->usbradio_write_buf));

	if (oldpttout != o->pmrChan->txPttOut) {
		ast_debug(3, "Channel %s: txPttOut = %i.\n", o->name, o->pmrChan->txPttOut);
		kickptt(o);
	}

#if 0 // to write 48KS/s stereo tx data to a file
	if (!ftxoutraw) {
		ftxoutraw = fopen(TX_CAP_OUT_FILE, "w");
	}
	if (ftxoutraw) {
		fwrite(o->usbradio_write_buf, 1, FRAME_SIZE * 2 * 6, ftxoutraw);
	}
#endif

#if DEBUG_CAPTURES == 1 && XPMR_DEBUG0 == 1
	if (o->txcap2 && ftxcaptrace) {
		fwrite((o->pmrChan->ptxDebug), 1, FRAME_SIZE * 2 * 16, ftxcaptrace);
	}
#endif

	/* Below is an attempt to match levels to the original CM108 IC which has been
	 * out of production for over 10 years. Scaling audio to 110% will result in clipping!
	 * Any adjustments for CM1xxx IC gain differences should be made in the mixer
	 * settings, not in the audio stream.
	 * TODO: After the vast majority of existing installs have had a chance to review their
	 * audio settings and these old scaling/clipping hacks are no longer in significant use
	 * the legacyaudioscaling cfg and related code should be deleted.
	 */
	/* For the CM108 adjust the audio level */
	if (o->legacyaudioscaling && o->devtype != C108_PRODUCT_ID) {
		register short *sp = (short *) o->usbradio_write_buf;
		register float v;
		register int i;

		for (i = 0; i < sizeof(o->usbradio_write_buf) / 2; i++) {
			v = ((float) *sp) * 1.10;
			if (v > 32765.0) {
				v = 32765.0;
			} else if (v < -32765.0) {
				v = -32765.0;
			}
			*sp++ = (int) v;
		}
	}

	/* Write the received audio to the sound card */
	soundcard_writeframe(o, (short *) o->usbradio_write_buf);

	/* Check Tx audio statistics. FRAME_SIZE define refers to 8Ksps mono which is 160 samples
	 * per 20mS USB frame. ast_radio_check_audio() takes the write buffer (48K stereo),
	 * extracts the mono 48K channel, checks amplitude and distortion characteristics,
	 * and returns true if clipping was detected. If local Tx audio is clipped it might be
	 * nice to log a warning but as this does not relate to outgoing network audio it's not
	 * a major issue. User can check the Tx Audio Stats utility if desired.
	 */
	ast_radio_check_audio((short *) o->usbradio_write_buf, &o->txaudiostats, 12 * FRAME_SIZE);

#else
	static FILE *hInput;
	i16 iBuff[FRAME_SIZE * 2 * 6];

	o->pmrChan->b.rxCapture = 1;

	if (!hInput) {
		hInput = fopen("/usr/src/xpmr/testdata/rx_in.pcm", "r");
		if (!hInput) {
			printf(" Input Data File Not Found.\n");
			return 0;
		}
	}

	if (0 == fread((void *) iBuff, 2, FRAME_SIZE * 2 * 6, hInput)) {
		exit;
	}

	PmrRx(o->pmrChan, (i16 *) iBuff, (i16 *) (o->usbradio_read_buf_8k + AST_FRIENDLY_OFFSET));

#endif

#if 0
	if (!frxoutraw)
		frxoutraw = fopen(RX_CAP_OUT_FILE, "w");
	if (frxoutraw)
		fwrite((o->usbradio_read_buf_8k + AST_FRIENDLY_OFFSET), 1, FRAME_SIZE * 2, frxoutraw);
#endif

#if DEBUG_CAPTURES == 1 && XPMR_DEBUG0 == 1
	if (frxcaptrace && o->rxcap2 && o->pmrChan->b.radioactive)
		fwrite((o->pmrChan->prxDebug), 1, FRAME_SIZE * 2 * 16, frxcaptrace);
#endif

	/* Check for carrier detect - COR active */
	cd = 0;
	if (o->rxcdtype == CD_HID && (o->pmrChan->rxExtCarrierDetect != o->rxhidsq)) {
		o->pmrChan->rxExtCarrierDetect = o->rxhidsq;
	}

	if (o->rxcdtype == CD_HID_INVERT && (o->pmrChan->rxExtCarrierDetect == o->rxhidsq)) {
		o->pmrChan->rxExtCarrierDetect = !o->rxhidsq;
	}

	if ((o->rxcdtype == CD_HID && o->rxhidsq) || (o->rxcdtype == CD_HID_INVERT && !o->rxhidsq) ||
		(o->rxcdtype == CD_XPMR_NOISE && o->pmrChan->rxCarrierDetect) || (o->rxcdtype == CD_PP && o->rxppsq) ||
		(o->rxcdtype == CD_PP_INVERT && !o->rxppsq) || (o->rxcdtype == CD_XPMR_VOX && o->pmrChan->rxCarrierDetect)) {
		if (!o->pmrChan->txPttOut || o->radioduplex) {
			cd = 1;
		}
	} else {
		cd = 0;
	}

	if (cd != o->rxcarrierdetect) {
		o->rxcarrierdetect = cd;
		ast_debug(3, "Channel %s: rxcarrierdetect = %i.\n", o->name, cd);
	}
	o->rx_cos_active = cd;

	if (o->pmrChan->b.ctcssRxEnable && o->pmrChan->rxCtcss->decode != o->rxctcssdecode) {
		ast_debug(3, "Channel %s: rxctcssdecode = %i.\n", o->name, o->pmrChan->rxCtcss->decode);
		o->rxctcssdecode = o->pmrChan->rxCtcss->decode;
		strcpy(o->rxctcssfreq, o->pmrChan->rxctcssfreq);
	}

	/* Check for SD - CTCSS active */
#ifndef HAVE_XPMRX
	if (!o->pmrChan->b.ctcssRxEnable ||
		(o->pmrChan->b.ctcssRxEnable && o->pmrChan->rxCtcss->decode > CTCSS_NULL && o->pmrChan->smode == SMODE_CTCSS)) {
		sd = 1;
	} else {
		sd = 0;
	}
#else
	if ((!o->pmrChan->b.ctcssRxEnable && !o->pmrChan->b.dcsRxEnable && !o->pmrChan->b.lmrRxEnable) ||
		(o->pmrChan->b.ctcssRxEnable && o->pmrChan->rxCtcss->decode > CTCSS_NULL && o->pmrChan->smode == SMODE_CTCSS) ||
		(o->pmrChan->b.dcsRxEnable && o->pmrChan->decDcs->decode > 0 && o->pmrChan->smode == SMODE_DCS)) {
		sd = 1;
	} else {
		sd = 0;
	}

	if (o->pmrChan->decDcs->decode != o->rxdcsdecode) {
		ast_debug(3, "Channel %s: rxdcsdecode = %s.\n", o->name, o->pmrChan->rxctcssfreq);
		o->rxdcsdecode = o->pmrChan->decDcs->decode;
		strcpy(o->rxctcssfreq, o->pmrChan->rxctcssfreq);
	}

	if (o->pmrChan->rptnum && (o->pmrChan->pLsdCtl->cs[o->pmrChan->rptnum].b.rxkeyed != o->rxlsddecode)) {
		ast_log(LOG_NOTICE, "Channel %s: rxLSDecode = %s.\n", o->name, o->pmrChan->rxctcssfreq);
		o->rxlsddecode = o->pmrChan->pLsdCtl->cs[o->pmrChan->rptnum].b.rxkeyed;
		strcpy(o->rxctcssfreq, o->pmrChan->rxctcssfreq);
	}

	if ((o->pmrChan->rptnum > 0 && o->pmrChan->smode == SMODE_LSD && o->pmrChan->pLsdCtl->cs[o->pmrChan->rptnum].b.rxkeyed) ||
		(o->pmrChan->smode == SMODE_DCS && o->pmrChan->decDcs->decode > 0)) {
		sd = 1;
	}
#endif
	if (o->rxsdtype == SD_HID) {
		sd = o->rxhidctcss;
	} else if (o->rxsdtype == SD_HID_INVERT) {
		sd = !o->rxhidctcss;
	} else if (o->rxsdtype == SD_PP) {
		sd = o->rxppctcss;
	} else if (o->rxsdtype == SD_PP_INVERT) {
		sd = !o->rxppctcss;
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
			o->txoffcnt = 0; /* If keyed, set this to zero. */
		} else {
			o->txoffcnt++;
			if (o->txoffcnt > MS_TO_FRAMES(TX_OFF_DELAY_MAX)) {
				o->txoffcnt = MS_TO_FRAMES(TX_OFF_DELAY_MAX); /* Limit the count */
			}
		}
	}

	/* Check conditions and set receiver active */
	if (cd && sd) {
		// if(!o->rxkeyed)o->pmrChan->dd.b.doitnow=1;
		if (!o->rxkeyed) {
			ast_debug(3, "Channel %s: o->rxkeyed = 1.\n", o->name);
		}
		if (o->rxkeyed || ((o->txoffcnt >= o->txoffdelay) && (o->rxoncnt >= o->rxondelay))) {
			o->rxkeyed = 1;
		} else {
			o->rxoncnt++;
		}
	} else {
		// if(o->rxkeyed)o->pmrChan->dd.b.doitnow=1;
		if (o->rxkeyed) {
			ast_debug(3, "Channel %s: o->rxkeyed = 0.\n", o->name);
		}
		o->rxkeyed = 0;
		o->rxoncnt = 0;
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
		for (u = (struct usbecho *) o->echoq.q_forw; u != (struct usbecho *) &o->echoq; u = (struct usbecho *) u->q_forw)
			x++;
		if (x < o->echomax) {
			u = ast_calloc(1, sizeof(struct usbecho));
			if (u) {
				memcpy(u->data, (o->usbradio_read_buf_8k + AST_FRIENDLY_OFFSET), FRAME_SIZE * 2);
				insque((struct qelem *) u, o->echoq.q_back);
			}
		}
		ast_mutex_unlock(&o->echolock);
	}

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
		if (o->rxctcssdecode) {
			wf.data.ptr = o->rxctcssfreq;
			wf.datalen = strlen(o->rxctcssfreq) + 1;
			ast_debug(7, "Radio Key - CTCSS frequency=%s.\n", o->rxctcssfreq);
		}
		ast_queue_frame(o->owner, &wf);
		o->count_rssi_update = 1;
		if (o->duplex3) {
			ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_PLAYBACK_SW, 1, 0);
		}
	}

	/* reset read pointer for next frame */
	o->readpos = AST_FRIENDLY_OFFSET;
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
	f->data.ptr = o->usbradio_read_buf_8k + AST_FRIENDLY_OFFSET;
	f->src = __PRETTY_FUNCTION__;
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

	if (o->pmrChan->b.txCtcssReady) {
		struct ast_frame wf = {
			.frametype = AST_FRAME_TEXT,
			.src = __PRETTY_FUNCTION__,
		};
		char msg[32];

		snprintf(msg, sizeof(msg), "cstx=%.26s", o->pmrChan->txctcssfreq);
		wf.data.ptr = msg;
		wf.datalen = strlen(msg) + 1;
		ast_queue_frame(o->owner, &wf);

		ast_debug(3, "Channel %s: got b.txCtcssReady %s.\n", o->name, o->pmrChan->txctcssfreq);
		o->pmrChan->b.txCtcssReady = 0;
	}
	/* report channel rssi */
	if (o->sendvoter && o->count_rssi_update && o->rxkeyed) {
		if (--o->count_rssi_update <= 0) {
			struct ast_frame wf = {
				.frametype = AST_FRAME_TEXT,
				.src = __PRETTY_FUNCTION__,
			};
			char msg[32];

			snprintf(msg, sizeof(msg), "R %i", ((32767 - o->pmrChan->rxRssi) * 1000) / 32767);
			wf.data.ptr = msg;
			wf.datalen = strlen(msg) + 1;
			ast_queue_frame(o->owner, &wf);

			o->count_rssi_update = 10;
			ast_debug(4, "Channel %s: Count_rssi_update %i\n", o->name, ((32767 - o->pmrChan->rxRssi) * 1000 / 32767));
		}
	}

	return f;
}

/*!
 * \brief Asterisk fixup function.
 * \param oldchan		Old asterisk channel.
 * \param newchan		New asterisk channel.
 * \retval 0			Always returns 0.
 */
static int usbradio_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct chan_usbradio_pvt *o = ast_channel_tech_pvt(newchan);
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
static int usbradio_indicate(struct ast_channel *c, int cond_in, const void *data, size_t datalen)
{
	struct chan_usbradio_pvt *o = ast_channel_tech_pvt(c);
	enum ast_control_frame_type cond = cond_in;

	switch (cond) {
	case AST_CONTROL_BUSY:
	case AST_CONTROL_CONGESTION:
	case AST_CONTROL_RINGING:
		break;
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
		ast_debug(1, "Channel %s: ACRK code=%s TX ON.\n", o->name, (char *) data);
		if (datalen && ((char *) (data))[0] != '0') {
			o->forcetxcode = 1;
			memset(o->set_txctcssfreq, 0, sizeof(o->set_txctcssfreq)); /* Possibly unnecessary, if this is used as a string? */
			ast_copy_string(o->set_txctcssfreq, data, sizeof(o->set_txctcssfreq));
			xpmr_config(o);
		}
		break;
	case AST_CONTROL_RADIO_UNKEY:
		o->txkeyed = 0;
		kickptt(o);
		ast_debug(1, "Channel %s: ACRUK TX OFF.\n", o->name);
		if (o->forcetxcode) {
			o->forcetxcode = 0;
			o->pmrChan->pTxCodeDefault = o->txctcssdefault;
			ast_debug(1, "Channel %s: Forced Tx Squelch Code cleared.\n", o->name);
		}
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
static int usbradio_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
	char *cp;
	struct chan_usbradio_pvt *o = ast_channel_tech_pvt(chan);

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
 * \brief Start a new usbradio call.
 * \param o				Private structure.
 * \param ext			Extension.
 * \param ctx			Context.
 * \param state			State.
 * \param assignedids	Unique ID string assigned to the channel.
 * \param requestor		Asterisk channel.
 * \return 				Asterisk channel.
 */
static struct ast_channel *usbradio_new(struct chan_usbradio_pvt *o, char *ext, char *ctx, int state,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor)
{
	struct ast_channel *c;

	c = ast_channel_alloc(1, state, NULL, NULL, "", ext, ctx, assignedids, requestor, 0, "Radio/%s", o->name);
	if (c == NULL) {
		return NULL;
	}
	ast_channel_tech_set(c, &usbradio_tech);
	if ((o->sounddev < 0) && o->hasusb) {
		setformat(o, O_RDWR);
	}
	ast_channel_internal_fd_set(c, 0, o->sounddev); /* -1 if device closed, override later */
	ast_channel_nativeformats_set(c, usbradio_tech.capabilities);
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
			/* XXX what about the channel itself ? */
		}
	}

	return c;
}

/*!
 * \brief USBRadio request from Asterisk.
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
static struct ast_channel *usbradio_request(const char *type, struct ast_format_cap *cap,
	const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	struct ast_channel *c;
	struct chan_usbradio_pvt *o = find_desc(data);

	if (!o) {
		ast_log(LOG_WARNING, "Device %s not found.\n", (char *) data);
		return NULL;
	}

	if (!(ast_format_cap_iscompatible(cap, usbradio_tech.capabilities))) {
		struct ast_str *cap_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		ast_log(LOG_NOTICE, "Channel %s: Channel requested with unsupported format(s): '%s'\n", o->name,
			ast_format_cap_get_names(cap, &cap_buf));
		return NULL;
	}

	if (o->owner) {
		ast_log(LOG_NOTICE, "Channel %s: Already have a call (chan %p) on the usb channel\n", o->name, o->owner);
		*cause = AST_CAUSE_BUSY;
		return NULL;
	}
	c = usbradio_new(o, NULL, NULL, AST_STATE_DOWN, assignedids, requestor);
	if (!c) {
		ast_log(LOG_ERROR, "Channel %s: Unable to create new usb channel\n", o->name);
		return NULL;
	}

	o->remoted = 0;
	xpmr_config(o);

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
	struct chan_usbradio_pvt *o = find_desc(usbradio_active);

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
	struct chan_usbradio_pvt *o = find_desc(usbradio_active);

	if (argc != 2) {
		return RESULT_SHOWUSAGE;
	}
	o->txtestkey = 0;
	kickptt(o);
	return RESULT_SUCCESS;
}

/*!
 * \brief Process Asterisk CLI request to show or set active USB device.
 * \param fd			Asterisk CLI fd
 * \param argc			Number of arguments
 * \param argv			Arguments
 * \return	Cli success, showusage, or failure.
 */
static int radio_active(int fd, int argc, const char *const *argv)
{
	if (argc == 2) {
		ast_cli(fd, "Active USB Radio device is [%s].\n", usbradio_active);
	} else if (argc != 3) {
		return RESULT_SHOWUSAGE;
	} else {
		struct chan_usbradio_pvt *o;
		if (!strcmp(argv[2], "show")) {
			ast_mutex_lock(&usb_dev_lock);
			for (o = usbradio_default.next; o; o = o->next) {
				ast_cli(fd, "Device [%s] exists as device=%s card=%d\n", o->name, o->devstr, ast_radio_usb_get_usbdev(o->devstr));
			}
			ast_mutex_unlock(&usb_dev_lock);
			return RESULT_SUCCESS;
		}
		o = find_desc(argv[2]);
		if (!o) {
			ast_cli(fd, "No device [%s] exists\n", argv[2]);
		} else {
			struct chan_usbradio_pvt *ao;
			for (ao = usbradio_default.next; ao && ao->name; ao = ao->next) {
				ao->pmrChan->b.radioactive = 0;
			}
			usbradio_active = o->name;
			o->pmrChan->b.radioactive = 1;
			ast_cli(fd, "Active (command) USB Radio device set to [%s]\n", usbradio_active);
		}
	}
	return RESULT_SUCCESS;
}

/*!
 * \brief Process Asterisk CLI request to swap usb devices
 * \param fd			Asterisk CLI fd
 * \param other			Other device.
 * \return	Cli success, showusage, or failure.
 */
static int usb_device_swap(int fd, const char *other)
{
	int d;
	char tmp[128];
	struct chan_usbradio_pvt *p = NULL, *o = find_desc(usbradio_active);

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
 * \param fd			Asterisk cli fd
 * \param o				Private struct.
 * \param intflag		Flag to indicate the type of wait.
 */
static void tune_flash(int fd, struct chan_usbradio_pvt *o, int intflag)
{
#define NFLASH 3

	int i;

	if (fd > 0) {
		ast_cli(fd, "Channel %s: USB Device Flash starting.\n", o->name);
	}
	for (i = 0; i < NFLASH; i++) {
		o->txtestkey = 1;
		o->pmrChan->txPttIn = 1;
		TxTestTone(o->pmrChan, 1); // generate 1KHz tone at 7200 peak
		if ((fd > 0) && intflag) {
			if (ast_radio_wait_or_poll(fd, 1000, intflag)) {
				o->pmrChan->txPttIn = 0;
				o->txtestkey = 0;
				break;
			}
		} else {
			usleep(1000000);
		}
		TxTestTone(o->pmrChan, 0);
		o->pmrChan->txPttIn = 0;
		o->txtestkey = 0;
		if (i == (NFLASH - 1)) {
			break;
		}
		if ((fd > 0) && intflag) {
			if (ast_radio_wait_or_poll(fd, 1500, intflag)) {
				o->pmrChan->txPttIn = 0;
				o->txtestkey = 0;
				break;
			}
		} else {
			usleep(1500000);
		}
	}
	if (fd > 0) {
		ast_cli(fd, "Channel %s: USB Device Flash completed.\n", o->name);
	}
	o->pmrChan->txPttIn = 0;
	o->txtestkey = 0;
}

/*!
 * \brief Process asterisk CLI request radio tune.
 * \param fd			Asterisk CLI fd
 * \param argc			Number of arguments
 * \param argv			Arguments
 * \return	CLI success, showusage, or failure.
 */
static int radio_tune(int fd, int argc, const char *const *argv)
{
	struct chan_usbradio_pvt *o = find_desc(usbradio_active);
	int i = 0;

	if ((argc < 3) || (argc > 4)) {
		return RESULT_SHOWUSAGE;
	}

	o->pmrChan->b.tuning = 1;

	if (!strcasecmp(argv[2], "dump")) {
		pmrdump(o, fd);
	} else if (!strcasecmp(argv[2], "swap")) {
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
	}

	if (!strcasecmp(argv[2], "rxnoise")) {
		tune_rxinput(fd, o, 0, 0);
	} else if (!strcasecmp(argv[2], "rxvoice")) {
		tune_rxvoice(fd, o, 0);
	} else if (!strcasecmp(argv[2], "rxtone")) {
		tune_rxctcss(fd, o, 0);
	} else if (!strcasecmp(argv[2], "flash")) {
		tune_flash(fd, o, 0);
	} else if (!strcasecmp(argv[2], "rxsquelch")) {
		if (argc == 3) {
			ast_cli(fd, "Current Signal Strength is %d\n", ((32767 - o->pmrChan->rxRssi) * 1000 / 32767));
			ast_cli(fd, "Current Squelch setting is %d\n", o->rxsquelchadj);
			// ast_cli(fd,"Current Raw RSSI        is %d\n",o->pmrChan->rxRssi);
			// ast_cli(fd,"Current (real) Squelch setting is %d\n",*(o->pmrChan->prxSquelchAdjust));
		} else {
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) {
				return RESULT_SHOWUSAGE;
			}
			ast_cli(fd, "Changed Squelch setting to %d\n", i);
			o->rxsquelchadj = i;
			*(o->pmrChan->prxSquelchAdjust) = ((999 - i) * 32767) / AUDIO_ADJUSTMENT;
		}
	} else if (!strcasecmp(argv[2], "txvoice")) {
		i = 0;

		if ((o->txmixa != TX_OUT_VOICE) && (o->txmixb != TX_OUT_VOICE) && (o->txmixa != TX_OUT_COMPOSITE) && (o->txmixb != TX_OUT_COMPOSITE)) {
			ast_log(LOG_ERROR, "No txvoice output configured.\n");
		} else if (argc == 3) {
			if ((o->txmixa == TX_OUT_VOICE) || (o->txmixa == TX_OUT_COMPOSITE))
				ast_cli(fd, "Current txvoice setting on Channel A is %d\n", o->txmixaset);
			else
				ast_cli(fd, "Current txvoice setting on Channel B is %d\n", o->txmixbset);
		} else {
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) {
				return RESULT_SHOWUSAGE;
			}

			if ((o->txmixa == TX_OUT_VOICE) || (o->txmixa == TX_OUT_COMPOSITE)) {
				o->txmixaset = i;
				ast_cli(fd, "Changed txvoice setting on Channel A to %d\n", o->txmixaset);
			} else {
				o->txmixbset = i;
				ast_cli(fd, "Changed txvoice setting on Channel B to %d\n", o->txmixbset);
			}
			mixer_write(o);
			mult_set(o);
			ast_cli(fd, "Changed Tx Voice Output setting to %d\n", i);
		}
		o->pmrChan->b.txCtcssInhibit = 1;
		tune_txoutput(o, i, fd, 0);
		o->pmrChan->b.txCtcssInhibit = 0;
	} else if (!strcasecmp(argv[2], "txall")) {
		i = 0;

		if ((o->txmixa != TX_OUT_VOICE) && (o->txmixb != TX_OUT_VOICE) && (o->txmixa != TX_OUT_COMPOSITE) && (o->txmixb != TX_OUT_COMPOSITE)) {
			ast_log(LOG_ERROR, "No txvoice output configured.\n");
		} else if (argc == 3) {
			if ((o->txmixa == TX_OUT_VOICE) || (o->txmixa == TX_OUT_COMPOSITE)) {
				ast_cli(fd, "Current txvoice setting on Channel A is %d\n", o->txmixaset);
			} else {
				ast_cli(fd, "Current txvoice setting on Channel B is %d\n", o->txmixbset);
			}
		} else {
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) {
				return RESULT_SHOWUSAGE;
			}

			if ((o->txmixa == TX_OUT_VOICE) || (o->txmixa == TX_OUT_COMPOSITE)) {
				o->txmixaset = i;
				ast_cli(fd, "Changed txvoice setting on Channel A to %d\n", o->txmixaset);
			} else {
				o->txmixbset = i;
				ast_cli(fd, "Changed txvoice setting on Channel B to %d\n", o->txmixbset);
			}
			mixer_write(o);
			mult_set(o);
			ast_cli(fd, "Changed Tx Voice Output setting to %d\n", i);
		}
		tune_txoutput(o, i, fd, 0);
	} else if (!strcasecmp(argv[2], "auxvoice")) {
		i = 0;
		if ((o->txmixa != TX_OUT_AUX) && (o->txmixb != TX_OUT_AUX)) {
			ast_log(LOG_WARNING, "No auxvoice output configured.\n");
		} else if (argc == 3) {
			if (o->txmixa == TX_OUT_AUX) {
				ast_cli(fd, "Current auxvoice setting on Channel A is %d\n", o->txmixaset);
			} else {
				ast_cli(fd, "Current auxvoice setting on Channel B is %d\n", o->txmixbset);
			}
		} else {
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) {
				return RESULT_SHOWUSAGE;
			}
			if (o->txmixa == TX_OUT_AUX) {
				o->txmixbset = i;
				ast_cli(fd, "Changed auxvoice setting on Channel A to %d\n", o->txmixaset);
			} else {
				o->txmixbset = i;
				ast_cli(fd, "Changed auxvoice setting on Channel B to %d\n", o->txmixbset);
			}
			mixer_write(o);
			mult_set(o);
		}
		// tune_auxoutput(o,i);
	} else if (!strcasecmp(argv[2], "txtone")) {
		if (argc == 3) {
			ast_cli(fd, "Current Tx CTCSS modulation setting = %d\n", o->txctcssadj);
		} else {
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) {
				return RESULT_SHOWUSAGE;
			}
			o->txctcssadj = i;
			set_txctcss_level(o);
			ast_cli(fd, "Changed Tx CTCSS modulation setting to %i\n", i);
		}
		o->txtestkey = 1;
		usleep(5000000);
		o->txtestkey = 0;
	} else if (!strcasecmp(argv[2], "nocap")) {
		ast_cli(fd, "File capture (trace) was rx=%d tx=%d and now off.\n", o->rxcap2, o->txcap2);
		ast_cli(fd, "File capture (raw)   was rx=%d tx=%d and now off.\n", o->rxcapraw, o->txcapraw);
		o->rxcapraw = o->txcapraw = o->rxcap2 = o->txcap2 = o->pmrChan->b.rxCapture = o->pmrChan->b.txCapture = 0;
		if (frxcapraw) {
			fclose(frxcapraw);
			frxcapraw = NULL;
		}
		if (frxcaptrace) {
			fclose(frxcaptrace);
			frxcaptrace = NULL;
		}
		if (frxoutraw) {
			fclose(frxoutraw);
			frxoutraw = NULL;
		}
		if (ftxcapraw) {
			fclose(ftxcapraw);
			ftxcapraw = NULL;
		}
		if (ftxcaptrace) {
			fclose(ftxcaptrace);
			ftxcaptrace = NULL;
		}
		if (ftxoutraw) {
			fclose(ftxoutraw);
			ftxoutraw = NULL;
		}
	} else if (!strcasecmp(argv[2], "rxtracecap")) {
		if (!frxcaptrace) {
			frxcaptrace = fopen(RX_CAP_TRACE_FILE, "w");
		}
		ast_cli(fd, "Trace rx on.\n");
		o->rxcap2 = o->pmrChan->b.rxCapture = 1;
	} else if (!strcasecmp(argv[2], "txtracecap")) {
		if (!ftxcaptrace) {
			ftxcaptrace = fopen(TX_CAP_TRACE_FILE, "w");
		}
		ast_cli(fd, "Trace tx on.\n");
		o->txcap2 = o->pmrChan->b.txCapture = 1;
	} else if (!strcasecmp(argv[2], "rxcap")) {
		if (!frxcapraw) {
			frxcapraw = fopen(RX_CAP_RAW_FILE, "w");
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
		ast_cli(fd, "Saved radio tuning settings to usbradio.conf\n");
	} else if (!strcasecmp(argv[2], "load")) {
		ast_mutex_lock(&o->eepromlock);
		while (o->eepromctl) {
			ast_mutex_unlock(&o->eepromlock);
			usleep(10000);
			ast_mutex_lock(&o->eepromlock);
		}
		o->eepromctl = 1; /* request a load */
		ast_mutex_unlock(&o->eepromlock);

		ast_cli(fd, "Requesting loading of tuning settings from EEPROM for channel %s\n", o->name);
	} else if (!strcasecmp(argv[2], "txslimsp")) {
		if (argc == 3) {
			ast_cli(fd, "Current tx limiter setpoint: %i\n", (int) o->txslimsp);
		} else {
			int new_slsetpoint = atoi(argv[3]);
			if (xpmr_set_tx_soft_limiter(o, new_slsetpoint)) {
				ast_cli(fd, "Limiter set point out of range, needs to be between 5000 and 13000\n");
				return RESULT_SHOWUSAGE;
			}
			o->txslimsp = new_slsetpoint;
		}
	} else {
		o->pmrChan->b.tuning = 0;
		return RESULT_SHOWUSAGE;
	}
	o->pmrChan->b.tuning = 0;
	return RESULT_SUCCESS;
}

/*!
 * \brief Set transmit CTCSS modulation level.
 *	Set the transmit CTCSS modulation level.  Adjust the mixer output or
 *	internal gain depending on the output type.
 *	Setting ranges is 0.0 to 0.9.
 *
 * \param o				chan_usbradio structure.
 * \return	0			Always returns zero.
 */
static int set_txctcss_level(struct chan_usbradio_pvt *o)
{
	if (o->txmixa == TX_OUT_LSD) {
		//      o->txmixaset=(151*o->txctcssadj) / 1000;
		o->txmixaset = o->txctcssadj;
		mixer_write(o);
		mult_set(o);
	} else if (o->txmixb == TX_OUT_LSD) {
		//      o->txmixbset=(151*o->txctcssadj) / 1000;
		o->txmixbset = o->txctcssadj;
		mixer_write(o);
		mult_set(o);
	} else {
		if (o->pmrChan->ptxCtcssAdjust) { /* Ignore if ptr not defined */
			*o->pmrChan->ptxCtcssAdjust = (o->txctcssadj * M_Q8) / AUDIO_ADJUSTMENT;
		}
	}
	return 0;
}

/*!
 * \brief Set transmit soft limiting threshold.
 * Modifies the set point in xpmr where soft limiting starts to take place.
 *
 *
 * \param o				chan_usbradio structure.
 * \param setpoint      A value which indicates the onset of soft limiting.
 * \return			    zero if successful, -1 if otherwise
 */

static int xpmr_set_tx_soft_limiter(struct chan_usbradio_pvt *o, int setpoint)
{
	/* Check for a valid pmrChan has to be done here. Data structures in xpmr are all dynamic. */

	if (o->pmrChan) {
		return SetTxSoftLimiterSetpoint(o->pmrChan, setpoint);
	} else {
		/* Not initialized yet */
		ast_debug(3, "Attempt to set soft limiter value before xpmr is initialized, request ignored\n");
		return -1;
	}
}

/*!
 * \brief Process Asterisk CLI request to set xpmr debug level.
 * \param fd			Asterisk CLI fd
 * \param argc			Number of arguments
 * \param argv			Arguments
 * \return	CLI success, showusage, or failure.
 */
static int radio_set_xpmr_debug(int fd, int argc, const char *const *argv)
{
	struct chan_usbradio_pvt *o = find_desc(usbradio_active);

	if (argc == 4) {
		int i;
		i = atoi(argv[3]);
		if ((i >= 0) && (i <= 100)) {
			o->pmrChan->tracelevel = i;
		}
	}
	// add ability to set it for a number of frames after which it reverts
	ast_cli(fd, "Channel %s: xdebug on tracelevel %i\n", o->name, o->pmrChan->tracelevel);

	return RESULT_SUCCESS;
}

/*!
 * \brief Store receive demodulator setting.
 * \param o				Private struct.
 * \param s				New setting.
 */
static void store_rxdemod(struct chan_usbradio_pvt *o, const char *s)
{
	if (!strcasecmp(s, "no")) {
		o->rxdemod = RX_AUDIO_NONE;
	} else if (!strcasecmp(s, "speaker")) {
		o->rxdemod = RX_AUDIO_SPEAKER;
	} else if (!strcasecmp(s, "flat")) {
		o->rxdemod = RX_AUDIO_FLAT;
	} else {
		ast_log(LOG_WARNING, "Unrecognized rxdemod parameter: %s\n", s);
	}
}

/*!
 * \brief Store tx mixer A setting.
 * \param o				Private struct.
 * \param s				New setting.
 */
static void store_txmixa(struct chan_usbradio_pvt *o, const char *s)
{
	if (!strcasecmp(s, "no")) {
		o->txmixa = TX_OUT_OFF;
	} else if (!strcasecmp(s, "voice")) {
		o->txmixa = TX_OUT_VOICE;
	} else if (!strcasecmp(s, "tone")) {
		o->txmixa = TX_OUT_LSD;
	} else if (!strcasecmp(s, "composite")) {
		o->txmixa = TX_OUT_COMPOSITE;
	} else if (!strcasecmp(s, "auxvoice")) {
		o->txmixa = TX_OUT_AUX;
	} else {
		ast_log(LOG_WARNING, "Unrecognized txmixa parameter: %s\n", s);
	}
}

/*!
 * \brief Store tx mixer B setting.
 * \param o				Private struct.
 * \param s				New setting.
 */
static void store_txmixb(struct chan_usbradio_pvt *o, const char *s)
{
	if (!strcasecmp(s, "no")) {
		o->txmixb = TX_OUT_OFF;
	} else if (!strcasecmp(s, "voice")) {
		o->txmixb = TX_OUT_VOICE;
	} else if (!strcasecmp(s, "tone")) {
		o->txmixb = TX_OUT_LSD;
	} else if (!strcasecmp(s, "composite")) {
		o->txmixb = TX_OUT_COMPOSITE;
	} else if (!strcasecmp(s, "auxvoice")) {
		o->txmixb = TX_OUT_AUX;
	} else {
		ast_log(LOG_WARNING, "Unrecognized txmixb parameter: %s\n", s);
	}
}

/*!
 * \brief Store receive carrier detect type.
 * \param o				Private struct.
 * \param s				New setting.
 */
static void store_rxcdtype(struct chan_usbradio_pvt *o, const char *s)
{
	if (!strcasecmp(s, "no")) {
		o->rxcdtype = CD_IGNORE;
	} else if (!strcasecmp(s, "usb")) {
		o->rxcdtype = CD_HID;
	} else if (!strcasecmp(s, "dsp")) {
		o->rxcdtype = CD_XPMR_NOISE;
	} else if (!strcasecmp(s, "vox")) {
		o->rxcdtype = CD_XPMR_VOX;
	} else if (!strcasecmp(s, "usbinvert")) {
		o->rxcdtype = CD_HID_INVERT;
	} else if (!strcasecmp(s, "pp")) {
		o->rxcdtype = CD_PP;
	} else if (!strcasecmp(s, "ppinvert")) {
		o->rxcdtype = CD_PP_INVERT;
	} else {
		ast_log(LOG_WARNING, "Unrecognized rxcdtype parameter: %s\n", s);
	}
}

static void store_rxsdtype(struct chan_usbradio_pvt *o, const char *s)
{
	if (!strcasecmp(s, "no") || !strcasecmp(s, "SD_IGNORE")) {
		o->rxsdtype = SD_IGNORE;
	} else if (!strcasecmp(s, "usb") || !strcasecmp(s, "SD_HID")) {
		o->rxsdtype = SD_HID;
	} else if (!strcasecmp(s, "usbinvert") || !strcasecmp(s, "SD_HID_INVERT")) {
		o->rxsdtype = SD_HID_INVERT;
	} else if (!strcasecmp(s, "dsp") || !strcasecmp(s, "SD_XPMR")) {
		o->rxsdtype = SD_XPMR;
	} else if (!strcasecmp(s, "pp")) {
		o->rxsdtype = SD_PP;
	} else if (!strcasecmp(s, "ppinvert")) {
		o->rxsdtype = SD_PP_INVERT;
	} else {
		ast_log(LOG_WARNING, "Unrecognized rxsdtype parameter: %s\n", s);
	}
}

/*!
 * \brief Store receiver gain setting.
 * \param o				Private struct.
 * \param s				New setting.
 */
static void store_rxgain(struct chan_usbradio_pvt *o, const char *s)
{
	float f;
	sscanf(s, N_FMT(f), &f);
	o->rxgain = f;
}

/*!
 * \brief Store receive voice adjustment.
 * \param o				Private struct.
 * \param s				New setting.
 */
static void store_rxvoiceadj(struct chan_usbradio_pvt *o, const char *s)
{
	float f;
	sscanf(s, N_FMT(f), &f);
	o->rxvoiceadj = f;
}

/*!
 * \brief Store transmit output tone turn off type.
 * \param o				Private struct.
 * \param s				New setting.
 */
static void store_txtoctype(struct chan_usbradio_pvt *o, const char *s)
{
	if (!strcasecmp(s, "no") || !strcasecmp(s, "TOC_NONE")) {
		o->txtoctype = TOC_NONE;
	} else if (!strcasecmp(s, "phase") || !strcasecmp(s, "TOC_PHASE")) {
		o->txtoctype = TOC_PHASE;
	} else if (!strcasecmp(s, "notone") || !strcasecmp(s, "TOC_NOTONE")) {
		o->txtoctype = TOC_NOTONE;
	} else {
		ast_log(LOG_WARNING, "Unrecognized txtoctype parameter: %s\n", s);
	}
}

/*!
 * \brief Send test tone.
 * \param fd			Asterisk CLI fd
 * \param o				Private struct.
 * \param intflag		Flag to indicate the type of wait.
 */
static void tune_txoutput(struct chan_usbradio_pvt *o, int value, int fd, int intflag)
{
	o->txtestkey = 1;
	o->pmrChan->txPttIn = 1;
	TxTestTone(o->pmrChan, 1); // generate 1KHz tone at 7200 peak
	if (fd > 0) {
		ast_cli(fd, "Tone output starting on channel %s...\n", o->name);
		if (ast_radio_wait_or_poll(fd, 5000, intflag)) {
			o->pmrChan->txPttIn = 0;
			o->txtestkey = 0;
		}
	} else
		usleep(5000000);
	TxTestTone(o->pmrChan, 0);
	if (fd > 0)
		ast_cli(fd, "Tone output ending on channel %s...\n", o->name);
	o->pmrChan->txPttIn = 0;
	o->txtestkey = 0;
}

/*!
 * \brief Adjust input attenuator with maximum signal input.
 *
 * \param fd			Asterisk CLI fd
 * \param o				chan_usbradio structure.
 * \param setsql		Setting for squelch.
 * \param intflag		Flag to indicate how ast_radio_wait_or_poll waits.
 */
static void tune_rxinput(int fd, struct chan_usbradio_pvt *o, int setsql, int intflag)
{
	const int settingmin = 1;
	const int settingstart = 2;
	const int maxtries = 12;

	int target;
	int tolerance = 2750;
	int setting = 0, tries = 0, tmpdiscfactor, meas, measnoise;
	float settingmax, f;

	if (o->rxdemod == RX_AUDIO_SPEAKER && o->rxcdtype == CD_XPMR_NOISE) {
		ast_cli(fd, "ERROR: usbradio.conf rxdemod=speaker vs. carrierfrom=dsp \n");
	}

	if (o->rxdemod == RX_AUDIO_FLAT) {
		target = 27000;
	} else {
		target = 23000;
	}

	settingmax = o->micmax;

	o->fever = 1;
	o->pmrChan->fever = 1;

	o->pmrChan->b.tuning = 1;

	setting = settingstart;

	ast_cli(fd, "tune rxnoise maxtries=%i, target=%i, tolerance=%i\n", maxtries, target, tolerance);

	while (tries < maxtries) {
		ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_CAPTURE_VOL, setting, 0);
		ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_BOOST, o->rxboost, 0);

		if (ast_radio_wait_or_poll(fd, 100, intflag)) {
			o->pmrChan->b.tuning = 0;
			return;
		}
		o->pmrChan->spsMeasure->source = o->pmrChan->spsRx->source;
		o->pmrChan->spsMeasure->discfactor = 2000;
		o->pmrChan->spsMeasure->enabled = 1;
		o->pmrChan->spsMeasure->amax = o->pmrChan->spsMeasure->amin = 0;
		if (ast_radio_wait_or_poll(fd, 400, intflag)) {
			o->pmrChan->b.tuning = 0;
			return;
		}
		meas = o->pmrChan->spsMeasure->apeak;
		o->pmrChan->spsMeasure->enabled = 0;

		if (!meas) {
			meas++;
		}
		ast_cli(fd, "tries=%i, setting=%i, meas=%i\n", tries, setting, meas);

		if ((meas < (target - tolerance) || meas > (target + tolerance)) && tries <= 2) {
			f = (float) (setting * target) / meas;
			setting = (int) (f + 0.5);
		} else if (meas < (target - tolerance) && tries > 2) {
			setting++;
		} else if (meas > (target + tolerance) && tries > 2) {
			setting--;
		} else if (tries > 5 && meas > (target - tolerance) && meas < (target + tolerance)) {
			break;
		}

		if (setting < settingmin) {
			setting = settingmin;
		} else if (setting > settingmax) {
			setting = settingmax;
		}

		tries++;
	}

	/* Measure HF Noise */
	tmpdiscfactor = o->pmrChan->spsRx->discfactor;
	o->pmrChan->spsRx->discfactor = (i16) 2000;
	o->pmrChan->spsRx->discounteru = o->pmrChan->spsRx->discounterl = 0;
	o->pmrChan->spsRx->amax = o->pmrChan->spsRx->amin = 0;
	if (ast_radio_wait_or_poll(fd, 200, intflag)) {
		o->pmrChan->b.tuning = 0;
		return;
	}
	measnoise = o->pmrChan->rxRssi;

	/* Measure RSSI */
	o->pmrChan->spsRx->discfactor = tmpdiscfactor;
	o->pmrChan->spsRx->discounteru = o->pmrChan->spsRx->discounterl = 0;
	o->pmrChan->spsRx->amax = o->pmrChan->spsRx->amin = 0;
	if (ast_radio_wait_or_poll(fd, 200, intflag)) {
		o->pmrChan->b.tuning = 0;
		return;
	}

	ast_cli(fd, "DONE tries=%i, setting=%i, meas=%i, sqnoise=%i\n", tries, ((setting * 1000) + (o->micmax / 2)) / o->micmax, meas, measnoise);

	if (meas < (target - tolerance) || meas > (target + tolerance)) {
		ast_cli(fd, "ERROR: RX INPUT ADJUST FAILED.\n");
	} else {
		ast_cli(fd, "INFO: RX INPUT ADJUST SUCCESS.\n");
		o->rxmixerset = ((setting * 1000) + (o->micmax / 2)) / o->micmax;

		if (o->rxcdtype == CD_XPMR_NOISE) {
			int normRssi = ((32767 - o->pmrChan->rxRssi) * AUDIO_ADJUSTMENT / 32767);

			if ((meas / (measnoise / 10)) > 26) {
				ast_cli(fd, "WARNING: Insufficient high frequency noise from receiver.\n");
				ast_cli(fd, "WARNING: Rx input point may be de-emphasized and not flat.\n");
				ast_cli(fd, "         usbradio.conf setting of 'carrierfrom=dsp' not recommended.\n");
			} else {
				ast_cli(fd, "Rx noise input seems sufficient for squelch.\n");
			}
			if (setsql) {
				o->rxsquelchadj = normRssi + 150;
				if (o->rxsquelchadj > 999) {
					o->rxsquelchadj = 999;
				}
				*(o->pmrChan->prxSquelchAdjust) = ((999 - o->rxsquelchadj) * 32767) / AUDIO_ADJUSTMENT;
				ast_cli(fd, "Rx Squelch set to %d (RSSI=%d).\n", o->rxsquelchadj, normRssi);
			} else {
				if (o->rxsquelchadj < normRssi) {
					ast_cli(fd, "WARNING: RSSI=%i SQUELCH=%i and is set too loose.\n", normRssi, o->rxsquelchadj);
					ast_cli(fd, "         Use 'radio tune rxsquelch' to adjust.\n");
				}
			}
		}
	}
	o->pmrChan->b.tuning = 0;
}

/*!
 * \brief Process Asterisk CLI request for receiver deviation display.
 * \param fd			Asterisk CLI fd
 * \param o				Private struct
 * \return	CLI success, showusage, or failure.
 */
static void tune_rxdisplay(int fd, struct chan_usbradio_pvt *o)
{
	int j, waskeyed, meas, ncols = 75;
	char str[256];

	ast_cli(fd, "RX VOICE DISPLAY:\n");
	ast_cli(fd, "                                 v -- 3KHz        v -- 5KHz\n");

	if (!o->pmrChan->spsMeasure) {
		ast_cli(fd, "ERROR: NO MEASURE BLOCK.\n");
		return;
	}

	if (!o->pmrChan->spsMeasure->source || !o->pmrChan->prxVoiceAdjust) {
		ast_cli(fd, "ERROR: NO SOURCE OR MEASURE SETTING.\n");
		return;
	}

	o->pmrChan->spsMeasure->source = o->pmrChan->spsRxOut->sink;

	o->pmrChan->spsMeasure->enabled = 1;
	o->pmrChan->spsMeasure->discfactor = 1000;

	waskeyed = !o->rxkeyed;
	for (;;) {
		o->pmrChan->spsMeasure->amax = o->pmrChan->spsMeasure->amin = 0;
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
		meas = o->pmrChan->spsMeasure->apeak;
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
	o->pmrChan->spsMeasure->enabled = 0;
}

/*!
 * \brief Process asterisk cli request for cos, ctcss, and ptt live display.
 * \param fd			Asterisk cli fd
 * \param o				Private struct
 * \return	Cli success, showusage, or failure.
 */
static void tune_rxtx_status(int fd, struct chan_usbradio_pvt *o)
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
		ast_cli(fd, " %s  | %s  | %s | %s\r", o->rxcdtype ? (o->rx_cos_active ? "Keyed" : "Clear") : "Off  ",
			o->rxsdtype ? (o->rx_ctcss_active ? "Keyed" : "Clear") : "Off  ", o->rxkeyed ? "Keyed" : "Clear",
			(o->txkeyed || o->txtestkey) ? "Keyed" : "Clear");
	}

	option_verbose = wasverbose;
}

/*!
 * \brief Set received voice level.
 * \param fd			Asterisk CLI fd.
 * \param o				chan_usbradio structure.
 * \param str			New voice level.
 */
static void _menu_rxvoice(int fd, struct chan_usbradio_pvt *o, const char *str)
{
	int i, x;
	float f, f1;
	int adjustment;

	if (!str[0]) {
		if (o->rxdemod == RX_AUDIO_FLAT) {
			ast_cli(fd, "Current Rx voice setting: %d\n", (int) ((o->rxvoiceadj * 200.0) + .5));
		} else {
			ast_cli(fd, "Current Rx voice setting: %d\n", o->rxmixerset);
		}
		return;
	}
	for (x = 0; str[x]; x++) {
		if (!isdigit(str[x])) {
			break;
		}
	}
	if (str[x] || (sscanf(str, N_FMT(d), &i) < 1) || (i < 0) || (i > 999)) {
		ast_cli(fd, "Entry Error, Rx voice setting not changed\n");
		return;
	}
	if (o->rxdemod == RX_AUDIO_FLAT) {
		o->rxvoiceadj = (float) i / 200.0;
	} else {
		o->rxmixerset = i;
		/* adjust settings based on the device */
		if (o->devtype == C119B_PRODUCT_ID) {
			o->rxboost = 1; /*rxboost is always set for this device */
		}
		adjustment = o->rxmixerset * o->micmax / AUDIO_ADJUSTMENT;
		/* get interval step size */
		f = AUDIO_ADJUSTMENT / (float) o->micmax;

		ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_CAPTURE_VOL, adjustment, 0);
		ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_BOOST, o->rxboost, 0);
		o->rxvoiceadj = 0.5 + (modff(((float) i) / f, &f1) * .093981);
	}
	*(o->pmrChan->prxVoiceAdjust) = o->rxvoiceadj * M_Q8;
	ast_cli(fd, "Changed rx voice setting to %d\n", i);
}

/*!
 * \brief Print settings.
 * \param fd			Asterisk CLI fd
 * \param o				Private struct.
 */
static void _menu_print(int fd, struct chan_usbradio_pvt *o)
{
	ast_cli(fd, "Active radio interface is [%s]\n", usbradio_active);
	ast_mutex_lock(&usb_dev_lock);
	ast_cli(fd, "Device String is %s\n", o->devstr);
	ast_mutex_unlock(&usb_dev_lock);
	ast_cli(fd, "Card is %i\n", ast_radio_usb_get_usbdev(o->devstr));
	ast_cli(fd, "Output A is currently set to ");
	if (o->txmixa == TX_OUT_COMPOSITE) {
		ast_cli(fd, "composite.\n");
	} else if (o->txmixa == TX_OUT_VOICE) {
		ast_cli(fd, "voice.\n");
	} else if (o->txmixa == TX_OUT_LSD) {
		ast_cli(fd, "tone.\n");
	} else if (o->txmixa == TX_OUT_AUX) {
		ast_cli(fd, "auxvoice.\n");
	} else {
		ast_cli(fd, "off.\n");
	}

	ast_cli(fd, "Output B is currently set to ");
	if (o->txmixb == TX_OUT_COMPOSITE) {
		ast_cli(fd, "composite.\n");
	} else if (o->txmixb == TX_OUT_VOICE) {
		ast_cli(fd, "voice.\n");
	} else if (o->txmixb == TX_OUT_LSD) {
		ast_cli(fd, "tone.\n");
	} else if (o->txmixb == TX_OUT_AUX) {
		ast_cli(fd, "auxvoice.\n");
	} else {
		ast_cli(fd, "off.\n");
	}

	if (o->rxdemod == RX_AUDIO_FLAT) {
		ast_cli(fd, "Rx Level currently set to %d\n", (int) ((o->rxvoiceadj * 200.0) + .5));
	} else {
		ast_cli(fd, "Rx Level currently set to %d\n", o->rxmixerset);
	}
	ast_cli(fd, "Rx Squelch currently set to %d\n", o->rxsquelchadj);
	ast_cli(fd, "Tx Voice Level currently set to %d\n", o->txmixaset);
	ast_cli(fd, "Tx Tone Level currently set to %d\n", o->txctcssadj);
	if (o->legacyaudioscaling) {
		ast_cli(fd, "legacyaudioscaling is enabled\n");
	}
}

/*!
 * \brief Set squelch level.
 * \param fd			Asterisk CLI fd.
 * \param o				chan_usbradio structure.
 * \param str			New squelch level.
 */
static void _menu_rxsquelch(int fd, struct chan_usbradio_pvt *o, const char *str)
{
	int i, x;

	if (!str[0]) {
		ast_cli(fd, "Current Signal Strength is %d\n", ((32767 - o->pmrChan->rxRssi) * 1000 / 32767));
		ast_cli(fd, "Current Squelch setting is %d\n", o->rxsquelchadj);
		return;
	}
	for (x = 0; str[x]; x++) {
		if (!isdigit(str[x])) {
			break;
		}
	}
	if (str[x] || (sscanf(str, N_FMT(d), &i) < 1) || (i < 0) || (i > 999)) {
		ast_cli(fd, "Entry Error, Rx Squelch Level setting not changed\n");
		return;
	}
	ast_cli(fd, "Changed Rx Squelch Level setting to %d\n", i);
	o->rxsquelchadj = i;
	/* adjust settings based on the device */
	*(o->pmrChan->prxSquelchAdjust) = ((999 - i) * 32767) / AUDIO_ADJUSTMENT;
}

/*!
 * \brief Set tx voice level.
 * \param fd			Asterisk CLI fd.
 * \param o				chan_usbradio structure.
 * \param str			New voice level.
 */
static void _menu_txvoice(int fd, struct chan_usbradio_pvt *o, const char *cstr)
{
	const char *str = cstr;
	int i, j, x, dokey, withctcss;

	if ((o->txmixa != TX_OUT_VOICE) && (o->txmixb != TX_OUT_VOICE) && (o->txmixa != TX_OUT_COMPOSITE) && (o->txmixb != TX_OUT_COMPOSITE)) {
		ast_cli(fd, "Error, No txvoice output configured.\n");
		return;
	}
	if (!str[0]) {
		if ((o->txmixa == TX_OUT_VOICE) || (o->txmixa == TX_OUT_COMPOSITE)) {
			ast_cli(fd, "Current Tx Voice Level setting on Channel A is %d\n", o->txmixaset);
		} else {
			ast_cli(fd, "Current Tx Voice Level setting on Channel B is %d\n", o->txmixbset);
		}
		return;
	}
	if ((o->txmixa == TX_OUT_VOICE) || (o->txmixa == TX_OUT_COMPOSITE)) {
		j = o->txmixaset;
	} else {
		j = o->txmixbset;
	}
	dokey = 0;
	if (str[0] == 'K') {
		dokey = 1;
		str++;
	}
	withctcss = 0;
	if (str[0] == 'C') {
		withctcss = 1;
		str++;
	}
	if (!str[0]) {
		ast_cli(fd, "Keying Transmitter and sending 1000 Hz tone for 5 seconds...\n");
		if (withctcss) {
			o->pmrChan->b.txCtcssInhibit = 1;
		}
		tune_txoutput(o, j, fd, 1);
		o->pmrChan->b.txCtcssInhibit = 0;
		ast_cli(fd, "DONE.\n");
		return;
	}
	for (x = 0; str[x]; x++) {
		if (!isdigit(str[x])) {
			break;
		}
	}
	if (str[x] || (sscanf(str, N_FMT(d), &i) < 1) || (i < 0) || (i > 999)) {
		ast_cli(fd, "Entry Error, Tx Voice Level setting not changed\n");
		return;
	}
	if ((o->txmixa == TX_OUT_VOICE) || (o->txmixa == TX_OUT_COMPOSITE)) {
		o->txmixaset = i;
		ast_cli(fd, "Changed Tx Voice Level setting on Channel A to %d\n", o->txmixaset);
	} else {
		o->txmixbset = i;
		ast_cli(fd, "Changed Tx Voice Level setting on Channel B to %d\n", o->txmixbset);
	}
	mixer_write(o);
	mult_set(o);
	if (dokey) {
		ast_cli(fd, "Keying Transmitter and sending 1000 Hz tone for 5 seconds...\n");
		if (!withctcss) {
			o->pmrChan->b.txCtcssInhibit = 1;
		}
		tune_txoutput(o, i, fd, 1);
		o->pmrChan->b.txCtcssInhibit = 0;
		ast_cli(fd, "DONE.\n");
	}
}

/*!
 * \brief Set aux voice level.
 * \param fd			Asterisk CLI fd.
 * \param o				chan_usbradio structure.
 * \param str			New voice level.
 */
static void _menu_auxvoice(int fd, struct chan_usbradio_pvt *o, const char *str)
{
	int i, x;

	if ((o->txmixa != TX_OUT_AUX) && (o->txmixb != TX_OUT_AUX)) {
		ast_cli(fd, "Error, No Auxvoice output configured.\n");
		return;
	}
	if (!str[0]) {
		if (o->txmixa == TX_OUT_AUX) {
			ast_cli(fd, "Current Aux Voice Level setting on Channel A is %d\n", o->txmixaset);
		} else {
			ast_cli(fd, "Current Aux Voice Level setting on Channel B is %d\n", o->txmixbset);
		}
		return;
	}
	for (x = 0; str[x]; x++) {
		if (!isdigit(str[x])) {
			break;
		}
	}
	if (str[x] || (sscanf(str, N_FMT(d), &i) < 1) || (i < 0) || (i > 999)) {
		ast_cli(fd, "Entry Error, Aux Voice Level setting not changed\n");
		return;
	}
	if (o->txmixa == TX_OUT_AUX) {
		o->txmixbset = i;
		ast_cli(fd, "Changed Aux Voice setting on Channel A to %d\n", o->txmixaset);
	} else {
		o->txmixbset = i;
		ast_cli(fd, "Changed Aux Voice setting on Channel B to %d\n", o->txmixbset);
	}
	mixer_write(o);
	mult_set(o);
}

/*!
 * \brief Set tx tone level.
 * \param fd			Asterisk CLI fd.
 * \param o				chan_usbradio structure.
 * \param str			New voice level.
 */
static void _menu_txtone(int fd, struct chan_usbradio_pvt *o, const char *cstr)
{
	const char *str = cstr;
	int i, x, dokey;

	if (!str[0]) {
		ast_cli(fd, "Current Tx CTCSS Modulation Level setting = %d\n", o->txctcssadj);
		return;
	}
	dokey = 0;
	if (str[0] == 'K') {
		dokey = 1;
		str++;
	}
	if (str[0]) {
		for (x = 0; str[x]; x++) {
			if (!isdigit(str[x])) {
				break;
			}
		}
		if (str[x] || (sscanf(str, N_FMT(d), &i) < 1) || (i < 0) || (i > 999)) {
			ast_cli(fd, "Entry Error, Tx CTCSS Modulation Level setting not changed\n");
			return;
		}
		o->txctcssadj = i;
		set_txctcss_level(o);
		ast_cli(fd, "Changed Tx CTCSS Modulation Level setting to %i\n", i);
	}
	if (dokey) {
		ast_cli(fd, "Keying Radio and sending CTCSS tone for 5 seconds...\n");
		o->txtestkey = 1;
		ast_radio_wait_or_poll(fd, 5000, 1);
		o->txtestkey = 0;
		ast_cli(fd, "DONE.\n");
	}
}

/*!
 * \brief Process tune menu commands.
 *
 * susb tune menusupport X - where X is one of the following:
 *		0 - get flatrx, ctcssenable, echomode
 *		1 - get node names that are configured in usbradio.conf
 *		2 - print parameters
 *		3 - get node names that are configured in usbradio.conf, except current device
 *		a - receive rx level
 *		b - receiver tune display
 *		c - receive level
 *		d - receive ctcss level
 *		e - squelch level
 *		f - voice level
 *		g - aux level
 *		h - transmit a test tone
 *		i - tune receive level
 *		j - save current settings for the selected node
 *		k - change echo mode
 *		l - generate test tone
 *		m - change rxboost
 *		n - change txboost
 *		o - change carrier from
 *		p - change ctcss from
 *		q - change rx on delay
 *		r - change tx off delay
 *		s - change tx pre limiting
 *		t - change tx limiting only
 *		u - change rx demodulation
 *		v - view cos, ctcss and ptt status
 *		w - change tx mixer a
 *		x - change tx mixer b
 *		y - receive audio statistics display
 *		z - transmit audio statistics display
 *
 * \param fd			Asterisk CLI fd
 * \param o				Private struct.
 * \param cmd			Command to process.
 */
static void tune_menusupport(int fd, struct chan_usbradio_pvt *o, const char *cmd)
{
	int x, oldverbose, flatrx, txhasctcss;
	struct chan_usbradio_pvt *oy = NULL;

	oldverbose = option_verbose;
	option_verbose = 0;
	flatrx = 0;
	if (o->rxdemod == RX_AUDIO_FLAT) {
		flatrx = 1;
	}
	txhasctcss = 0;
	if ((o->txmixa == TX_OUT_LSD) || (o->txmixa == TX_OUT_COMPOSITE) || (o->txmixb == TX_OUT_LSD) || (o->txmixb == TX_OUT_COMPOSITE)) {
		txhasctcss = 1;
	}
	switch (cmd[0]) {
	case '0': /* return audio processing configuration */
		/* note: to maintain backward compatibility for those expecting a specific # of
		   values to be returned (and in a specific order).  So, we only add to the end
		   of the returned list.  Also, once an update has been released we can't change
		   the format/content of any previously returned string */
		if (!strcmp(cmd, "0+10")) { /* With o->txslimsp tx soft limiter set point */
			ast_cli(fd, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%f,%d,%d,%d,%d,%d,%d,%d,%d\n", flatrx, txhasctcss,
				o->echomode, o->rxboost, o->txboost, o->rxcdtype, o->rxsdtype, o->rxondelay, o->txoffdelay, o->txprelim,
				o->txlimonly, o->rxdemod, o->txmixa, o->txmixb, o->rxmixerset, o->rxvoiceadj, o->rxsquelchadj, o->txmixaset,
				o->txmixbset, o->txctcssadj, o->micplaymax, o->spkrmax, o->micmax, o->txslimsp);
		} else if (!strcmp(cmd, "0+9")) {
			ast_cli(fd, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%f,%d,%d,%d,%d,%d,%d,%d\n", flatrx, txhasctcss, o->echomode,
				o->rxboost, o->txboost, o->rxcdtype, o->rxsdtype, o->rxondelay, o->txoffdelay, o->txprelim, o->txlimonly,
				o->rxdemod, o->txmixa, o->txmixb, o->rxmixerset, o->rxvoiceadj, o->rxsquelchadj, o->txmixaset, o->txmixbset,
				o->txctcssadj, o->micplaymax, o->spkrmax, o->micmax);
		} else {
			ast_cli(fd, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", flatrx, txhasctcss, o->echomode, o->rxboost, o->txboost,
				o->rxcdtype, o->rxsdtype, o->rxondelay, o->txoffdelay, o->txprelim, o->txlimonly, o->rxdemod, o->txmixa, o->txmixb);
		}
		break;
	case '1': /* return usb device name list */
		for (x = 0, oy = usbradio_default.next; oy && oy->name; oy = oy->next, x++) {
			if (x) {
				ast_cli(fd, ",");
			}
			ast_cli(fd, "%s", oy->name);
		}
		ast_cli(fd, "\n");
		break;
	case '2': /* print parameters */
		_menu_print(fd, o);
		break;
	case '3': /* return usb device name list except current */
		for (x = 0, oy = usbradio_default.next; oy && oy->name; oy = oy->next) {
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
	case 'a': /* receive tune */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		tune_rxinput(fd, o, 1, 1);
		break;
	case 'b': /* receive tune display */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		tune_rxdisplay(fd, o);
		break;
	case 'c': /* set receive voice level */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		_menu_rxvoice(fd, o, cmd + 1);
		break;
	case 'd': /* set receive ctcss level */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		tune_rxctcss(fd, o, 1);
		break;
	case 'e': /* set squelch level */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		_menu_rxsquelch(fd, o, cmd + 1);
		break;
	case 'f': /* set voice transmit level */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		_menu_txvoice(fd, o, cmd + 1);
		break;
	case 'g': /* set aux transmit level */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		_menu_auxvoice(fd, o, cmd + 1);
		break;
	case 'h': /* transmit a test tone */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		_menu_txtone(fd, o, cmd + 1);
		break;
	case 'i': /* tune receive level */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		tune_rxvoice(fd, o, 1);
		break;
	case 'j': /* save tune settings */
		tune_write(o);
		ast_cli(fd, "Saved radio tuning settings to usbradio.conf\n");
		break;
	case 'k': /* change echo mode */
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
	case 'l': /* transmit test tone */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		tune_flash(fd, o, 1);
		break;

	case 'L': /* Set TX soft limiter when operating with preemphasized and limited tx audio */
		if (cmd[1]) {
			int setpoint = atoi(cmd + 1);
			if (xpmr_set_tx_soft_limiter(o, setpoint)) {
				ast_debug(3, "TX soft limiter set failed in tune menu-support\n");
				break;
			} else {
				o->txslimsp = setpoint;
			}

			ast_cli(fd, "TX soft limiting setpoint changed to %i\n", setpoint);
		} else {
			ast_cli(fd, "TX soft limiting setpoint currently set to: %i\n", o->txslimsp);
		}
		break;

	case 'm': /* change rxboost */
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
	case 'n': /* change txboost */
		if (cmd[1]) {
			if (cmd[1] > '0') {
				o->txboost = 1;
			} else {
				o->txboost = 0;
			}
			ast_cli(fd, "TxBoost changed to %s\n", (o->txboost) ? "Enabled" : "Disabled");
		} else {
			ast_cli(fd, "TxBoost is currently %s\n", (o->txboost) ? "Enabled" : "Disabled");
		}
		break;
	case 'o': /* change carrier from */
		if (cmd[1]) {
			o->rxcdtype = atoi(&cmd[1]);
			ast_cli(fd, "Carrier From changed to %s\n", cd_signal_type[o->rxcdtype]);
		} else {
			ast_cli(fd, "Carrier From is currently %s\n", cd_signal_type[o->rxcdtype]);
		}
		break;
	case 'p': /* change ctcss from */
		if (cmd[1]) {
			o->rxsdtype = atoi(&cmd[1]);
			ast_cli(fd, "CTCSS From changed to %s\n", sd_signal_type[o->rxsdtype]);
		} else {
			ast_cli(fd, "CTCSS From is currently %s\n", sd_signal_type[o->rxsdtype]);
		}
		break;
	case 'q': /* change rx on delay */
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
	case 'r': /* change tx off delay */
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
	case 's': /* change txprelim */
		if (cmd[1]) {
			if (cmd[1] > '0') {
				o->txprelim = 1;
			} else {
				o->txprelim = 0;
			}
			ast_cli(fd, "TxPrelim changed to %s\n", (o->txprelim) ? "Enabled" : "Disabled");
		} else {
			ast_cli(fd, "TxPrelim is currently %s\n", (o->txprelim) ? "Enabled" : "Disabled");
		}
		break;
	case 't': /* change txlimonly */
		if (cmd[1]) {
			if (cmd[1] > '0') {
				o->txlimonly = 1;
			} else {
				o->txlimonly = 0;
			}
			ast_cli(fd, "TxLimonly changed to %s\n", (o->txlimonly) ? "Enabled" : "Disabled");
		} else {
			ast_cli(fd, "TxLimonly is currently %s\n", (o->txlimonly) ? "Enabled" : "Disabled");
		}
		break;
	case 'u': /* change rxdemod */
		if (cmd[1]) {
			o->rxdemod = atoi(&cmd[1]);
			ast_cli(fd, "RX Demodulation changed to %d\n", o->rxdemod);
		} else {
			ast_cli(fd, "RX Demodulation is currently %d\n", o->rxdemod);
		}
		break;
	case 'v': /* receiver/transmitter status display */
		if (!o->hasusb) {
			ast_cli(fd, USB_UNASSIGNED_FMT, o->name, o->devstr);
			break;
		}
		tune_rxtx_status(fd, o);
		break;
	case 'w': /* change txmixa */
		if (cmd[1]) {
			o->txmixa = atoi(&cmd[1]);
			ast_cli(fd, "TX Mixer A changed to %d\n", o->txmixa);
		} else {
			ast_cli(fd, "TX Mixer A is currently %d\n", o->txmixa);
		}
		break;
	case 'x': /* change txmixb */
		if (cmd[1]) {
			o->txmixb = atoi(&cmd[1]);
			ast_cli(fd, "TX Mixer B changed to %d\n", o->txmixb);
		} else {
			ast_cli(fd, "TX Mixer B is currently %d\n", o->txmixb);
		}
		break;
	case 'y': /* display receive audio statistics (interactive) */
	case 'Y': /* display receive audio statistics (once only) */
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
	o->pmrChan->b.tuning = 0;
	option_verbose = oldverbose;
}

/*!
 * \brief Tune receive voice level.
 * \param fd			Asterisk CLI fd
 * \param o				Private struct.
 * \param intflag		Flag to indicate the type of wait.
 */
static void tune_rxvoice(int fd, struct chan_usbradio_pvt *o, int intflag)
{
	const int target = 7200;   // peak
	const int tolerance = 360; // peak to peak
	const float settingmin = 0.1;
	const float settingmax = 5;
	const float settingstart = 1;
	const int maxtries = 12;

	float setting;

	int tries = 0, meas;

	ast_cli(fd, "INFO: RX VOICE ADJUST START.\n");
	ast_cli(fd, "target=%i tolerance=%i \n", target, tolerance);

	o->pmrChan->b.tuning = 1;
	if (!o->pmrChan->spsMeasure) {
		ast_cli(fd, "ERROR: NO MEASURE BLOCK.\n");
	}

	if (!o->pmrChan->spsMeasure->source || !o->pmrChan->prxVoiceAdjust) {
		ast_cli(fd, "ERROR: NO SOURCE OR MEASURE SETTING.\n");
	}

	o->pmrChan->spsMeasure->source = o->pmrChan->spsRxOut->sink;
	o->pmrChan->spsMeasure->enabled = 1;
	o->pmrChan->spsMeasure->discfactor = 1000;

	setting = settingstart;

	// ast_cli(fd,"ERROR: NO MEASURE BLOCK.\n");

	while (tries < maxtries) {
		*(o->pmrChan->prxVoiceAdjust) = setting * M_Q8;
		if (ast_radio_wait_or_poll(fd, 10, intflag)) {
			o->pmrChan->b.tuning = 0;
			return;
		}
		o->pmrChan->spsMeasure->amax = o->pmrChan->spsMeasure->amin = 0;
		if (ast_radio_wait_or_poll(fd, 1000, intflag)) {
			o->pmrChan->b.tuning = 0;
			return;
		}
		meas = o->pmrChan->spsMeasure->apeak;
		ast_cli(fd, "tries=%i, setting=%f, meas=%i\n", tries, setting, meas);

		if (meas < (target - tolerance) || meas > (target + tolerance) || tries < 3) {
			setting = setting * target / meas;
		} else if (tries > 4 && meas > (target - tolerance) && meas < (target + tolerance)) {
			break;
		}
		if (setting < settingmin) {
			setting = settingmin;
		} else if (setting > settingmax) {
			setting = settingmax;
		}

		tries++;
	}

	o->pmrChan->spsMeasure->enabled = 0;

	ast_cli(fd, "DONE tries=%i, setting=%f, meas=%f\n", tries, setting, (float) meas);
	if (meas < (target - tolerance) || meas > (target + tolerance)) {
		ast_cli(fd, "ERROR: RX VOICE GAIN ADJUST FAILED.\n");
	} else {
		ast_cli(fd, "INFO: RX VOICE GAIN ADJUST SUCCESS.\n");
		o->rxvoiceadj = setting;
	}
	o->pmrChan->b.tuning = 0;
}

/*!
 * \brief Determine the receive CTCSS level.
 * \param fd			Asterisk CLI fd.
 * \param o				chan_usbradio structure.
 * \param intflag		Flag to indicate how ast_radio_wait_or_poll waits.
 */
static void tune_rxctcss(int fd, struct chan_usbradio_pvt *o, int intflag)
{
	const int target = 2400; // was 4096 pre 20080205
	const int tolerance = 100;
	const float settingmin = 0.1;
	const float settingmax = 8;
	const float settingstart = 1;
	const int maxtries = 12;

	float setting;
	int tries = 0, meas;

	ast_cli(fd, "INFO: RX CTCSS ADJUST START.\n");
	ast_cli(fd, "target=%i tolerance=%i \n", target, tolerance);

	o->pmrChan->b.tuning = 1;
	o->pmrChan->spsMeasure->source = o->pmrChan->prxCtcssMeasure;
	o->pmrChan->spsMeasure->discfactor = 400;
	o->pmrChan->spsMeasure->enabled = 1;

	setting = settingstart;

	while (tries < maxtries) {
		*(o->pmrChan->prxCtcssAdjust) = setting * M_Q8;
		if (ast_radio_wait_or_poll(fd, 10, intflag)) {
			o->pmrChan->b.tuning = 0;
			return;
		}
		o->pmrChan->spsMeasure->amax = o->pmrChan->spsMeasure->amin = 0;
		if (ast_radio_wait_or_poll(fd, 500, intflag)) {
			o->pmrChan->b.tuning = 0;
			return;
		}
		meas = o->pmrChan->spsMeasure->apeak;
		ast_cli(fd, "tries=%i, setting=%f, meas=%i\n", tries, setting, meas);

		if (meas < (target - tolerance) || meas > (target + tolerance) || tries < 3) {
			setting = setting * target / meas;
		} else if (tries > 4 && meas > (target - tolerance) && meas < (target + tolerance)) {
			break;
		}
		if (setting < settingmin) {
			setting = settingmin;
		} else if (setting > settingmax) {
			setting = settingmax;
		}

		tries++;
	}
	o->pmrChan->spsMeasure->enabled = 0;
	ast_cli(fd, "DONE tries=%i, setting=%f, meas=%.2f\n", tries, setting, (float) meas);
	if (meas < (target - tolerance) || meas > (target + tolerance)) {
		ast_cli(fd, "ERROR: RX CTCSS GAIN ADJUST FAILED.\n");
	} else {
		ast_cli(fd, "INFO: RX CTCSS GAIN ADJUST SUCCESS.\n");
	}

	if (o->rxcdtype == CD_XPMR_NOISE) {
		int normRssi;

		if (ast_radio_wait_or_poll(fd, 200, intflag)) {
			o->pmrChan->b.tuning = 0;
			return;
		}

		normRssi = ((32767 - o->pmrChan->rxRssi) * AUDIO_ADJUSTMENT / 32767);

		if (o->rxsquelchadj > normRssi) {
			ast_cli(fd, "WARNING: RSSI=%i SQUELCH=%i and is too tight. Use 'radio tune rxsquelch'.\n", normRssi, o->rxsquelchadj);
		} else {
			ast_cli(fd, "INFO: RX RSSI=%i\n", normRssi);
		}
	}
	o->pmrChan->b.tuning = 0;
}

/*!
 * \brief Update the tune settings to the configuration file.
 * \param config	The (opened) config to use
 * \param filename	The configuration file being updated (e.g. "usbradio.conf").
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
static void tune_write(struct chan_usbradio_pvt *o)
{
	struct ast_config *cfg;
	struct ast_category *category = NULL;
	struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS | CONFIG_FLAG_NOCACHE };
	const float old_rxctcssadj = 0.5; /* for backward EEPROM format compatibility */

	if (!(cfg = ast_config_load2(CONFIG, "chan_usbradio", config_flags))) {
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

#define CONFIG_UPDATE_FLOAT(field) \
	{ \
		char _buf[15]; \
		snprintf(_buf, sizeof(_buf), "%f", o->field); \
		if (tune_variable_update(cfg, CONFIG, category, #field, _buf)) { \
			ast_log(LOG_WARNING, "Failed to update %s\n", #field); \
		} \
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
		CONFIG_UPDATE_FLOAT(rxvoiceadj);
		CONFIG_UPDATE_INT(txctcssadj);
		CONFIG_UPDATE_INT(rxsquelchadj);
		CONFIG_UPDATE_INT(fever);
		CONFIG_UPDATE_BOOL(rxboost);
		CONFIG_UPDATE_BOOL(txboost);
		CONFIG_UPDATE_SIGNAL(carrierfrom, rxcdtype, cd_signal_type);
		CONFIG_UPDATE_SIGNAL(ctcssfrom, rxsdtype, sd_signal_type);
		CONFIG_UPDATE_INT(rxondelay);
		CONFIG_UPDATE_INT(txoffdelay);
		CONFIG_UPDATE_BOOL(txprelim);
		CONFIG_UPDATE_BOOL(txlimonly);
		CONFIG_UPDATE_SIGNAL(rxdemod, rxdemod, demodulation_type);
		CONFIG_UPDATE_SIGNAL(txmixa, txmixa, mixer_type);
		CONFIG_UPDATE_SIGNAL(txmixb, txmixb, mixer_type);
		CONFIG_UPDATE_INT(txslimsp);
		if (ast_config_text_file_save2(CONFIG, cfg, "chan_usbradio", 0)) {
			ast_log(LOG_WARNING, "Failed to save config %s\n", CONFIG);
		}
	}

	ast_config_destroy(cfg);
#undef CONFIG_UPDATE_STR
#undef CONFIG_UPDATE_INT
#undef CONFIG_UPDATE_BOOL
#undef CONFIG_UPDATE_FLOAT
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
		memcpy(&o->eeprom[EEPROM_USER_RXVOICEADJ], &o->rxvoiceadj, sizeof(float));
		memcpy(&o->eeprom[EEPROM_USER_RXCTCSSADJ], &old_rxctcssadj, sizeof(float));
		o->eeprom[EEPROM_USER_TXCTCSSADJ] = o->txctcssadj;
		o->eeprom[EEPROM_USER_RXSQUELCHADJ] = o->rxsquelchadj;
		o->eepromctl = 2; /* request a write */
		ast_mutex_unlock(&o->eepromlock);
	}
}

/*!
 * \brief Update the ALSA mixer settings
 * Update the ALSA mixer settings.
 *
 * \param		chan_usbradio structure.
 */
static void mixer_write(struct chan_usbradio_pvt *o)
{
	int mic_setting;

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
	ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_CAPTURE_VOL, mic_setting, 0);
	ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_BOOST, o->rxboost, 0);
	ast_radio_setamixer(o->devicenum, MIXER_PARAM_MIC_CAPTURE_SW, 1, 0);
}

/*!
 * \brief Adjust DSP multiplier
 * Adjusts the DSP multiplier to add resolution to the tx level adjustment
 *
 * \param		chan_usbradio structure.
 */
static void mult_set(struct chan_usbradio_pvt *o)
{
	if (o->pmrChan->spsTxOutA) {
		o->pmrChan->spsTxOutA->outputGain = mult_calc((o->txmixaset * 152) / AUDIO_ADJUSTMENT);
	}
	if (o->pmrChan->spsTxOutB) {
		o->pmrChan->spsTxOutB->outputGain = mult_calc((o->txmixbset * 152) / AUDIO_ADJUSTMENT);
	}
}

/*!
 * \brief Calculate multiplier.
 * \param value		Level to calculate.
 * \returns			Multiplier.
 */
static int mult_calc(int value)
{
	const int multx = M_Q8;
	int pot, mult;

	pot = ((int) (value / 4) * 4) + 2;
	mult = multx - ((multx * (3 - (value % 4))) / (pot + 2));
	return (mult);
}

#define pd(x) \
	{ \
		ast_cli(fd, #x " = %d\n", x); \
	}
#define pp(x) \
	{ \
		ast_cli(fd, #x " = %p\n", x); \
	}
#define ps(x) \
	{ \
		ast_cli(fd, #x " = %s\n", x); \
	}
#define pf(x) \
	{ \
		ast_cli(fd, #x " = %f\n", x); \
	}

#if 0
/*
	do hid output if only requirement is ptt out
	this give fastest performance with least overhead
	where gpio inputs are not required.
*/

static int usbhider(struct chan_usbradio_pvt *o, int opt)
{
	unsigned char buf[4];
	char lastrx, txtmp;

	if (opt) {
		struct usb_device *usb_dev;

		usb_dev = ast_radio_hid_device_init(o->devstr);
		if (usb_dev == NULL) {
			ast_log(LOG_ERROR, "USB HID device not found\n");
			return -1;
		}
		o->usb_handle = usb_open(usb_dev);
		if (o->usb_handle == NULL) {
			ast_log(LOG_ERROR, "Not able to open USB device\n");
			return -1;
		}
		if (usb_claim_interface(o->usb_handle, C108_HID_INTERFACE) < 0) {
			if (usb_detach_kernel_driver_np(o->usb_handle, C108_HID_INTERFACE) < 0) {
				ast_log(LOG_ERROR, "Not able to detach the USB device\n");
				return -1;
			}
			if (usb_claim_interface(o->usb_handle, C108_HID_INTERFACE) < 0) {
				ast_log(LOG_ERROR, "Not able to claim the USB device\n");
				return -1;
			}
		}

		memset(buf, 0, sizeof(buf));
		buf[2] = o->hid_gpio_ctl;
		buf[1] = 0;
		ast_radio_hid_set_outputs(o->usb_handle, buf);
		memcpy(bufsave, buf, sizeof(buf));

		buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
		o->lasttx = 0;
	}

	/* if change in tx state as controlled by xpmr */
	txtmp = o->pmrChan->txPttOut;

	if (o->lasttx != txtmp) {
		o->pmrChan->txPttHid = o->lasttx = txtmp;
		o->hid_gpio_val &= ~o->hid_io_ptt;
		if (!o->invertptt) {
			if (txtmp)
				o->hid_gpio_val |= o->hid_io_ptt;
		} else {
			if (!txtmp)
				o->hid_gpio_val |= o->hid_io_ptt;
		}
		buf[o->hid_gpio_loc] = o->hid_gpio_val;
		buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
		ast_radio_hid_set_outputs(o->usb_handle, buf);
	}

	return (0);
}
#endif
/*!
 * \brief Dump pmr settings.
 * \param o				Private struct.
 */
static void pmrdump(struct chan_usbradio_pvt *o, int fd)
{
	t_pmr_chan *p;
	int i;

	p = o->pmrChan;

	ast_cli(fd, "\nodump()\n");

	pd(o->devicenum);
	ast_mutex_lock(&usb_dev_lock);
	ps(o->devstr);
	ast_mutex_unlock(&usb_dev_lock);

	pd(o->micmax);
	pd(o->spkrmax);

	pd(o->rxdemod);
	pd(o->rxcdtype);
	if (o->rxcdtype == CD_XPMR_VOX) {
		pd(o->voxhangtime);
	}
	pd(o->rxsdtype);
	pd(o->txtoctype);

	pd(o->rxmixerset);
	pd(o->rxboost);
	pd(o->txboost);

	pf(o->rxvoiceadj);
	pd(o->rxsquelchadj);

	ps(o->txctcssdefault);
	ps(o->txctcssfreq);

	pd(o->numrxctcssfreqs);
	pd(o->numtxctcssfreqs);
	if (o->numrxctcssfreqs > 0) {
		for (i = 0; i < o->numrxctcssfreqs; i++) {
			ast_cli(fd, " %i =  %s  %s\n", i, o->rxctcss[i], o->txctcss[i]);
		}
	}
	pd(o->rxpolarity);
	pd(o->txpolarity);

	pd(o->txlimonly);
	pd(o->txprelim);
	pd(o->txmixa);
	pd(o->txmixb);

	pd(o->txmixaset);
	pd(o->txmixbset);

	ast_cli(fd, "\npmrdump()\n");

	pd(p->devicenum);

	ast_cli(fd, "prxSquelchAdjust=%i\n", *(o->pmrChan->prxSquelchAdjust));

	pd(p->rxCarrierPoint);
	pd(p->rxCarrierHyst);

	pd(*p->prxVoiceAdjust);
	pd(*p->prxCtcssAdjust);

	pd(p->rxfreq);
	pd(p->txfreq);

	pd(p->rxCtcss->relax);
	/* pf(p->rxCtcssFreq); */
	pd(p->numrxcodes);
	if (o->pmrChan->numrxcodes > 0) {
		for (i = 0; i < o->pmrChan->numrxcodes; i++) {
			ast_cli(fd, " %i = %s\n", i, o->pmrChan->pRxCode[i]);
		}
	}

	pd(p->txTocType);
	ps(p->pTxCodeDefault);
	pd(p->txcodedefaultsmode);
	pd(p->numtxcodes);
	if (o->pmrChan->numtxcodes > 0) {
		for (i = 0; i < o->pmrChan->numtxcodes; i++) {
			ast_cli(fd, " %i = %s\n", i, o->pmrChan->pTxCode[i]);
		}
	}

	pd(p->b.rxpolarity);
	pd(p->b.txpolarity);
	pd(p->b.dcsrxpolarity);
	pd(p->b.dcstxpolarity);
	pd(p->b.lsdrxpolarity);
	pd(p->b.lsdtxpolarity);

	pd(p->txMixA);
	pd(p->txMixB);

	pd(p->rxDeEmpEnable);
	pd(p->rxCenterSlicerEnable);
	pd(p->rxCtcssDecodeEnable);
	pd(p->rxDcsDecodeEnable);
	pd(p->b.ctcssRxEnable);
	pd(p->b.dcsRxEnable);
	pd(p->b.lmrRxEnable);
	pd(p->b.dstRxEnable);
	pd(p->smode);

	pd(p->txHpfEnable);
	pd(p->txLimiterEnable);
	pd(p->txPreEmpEnable);
	pd(p->txLpfEnable);

	if (p->spsTxOutA)
		pd(p->spsTxOutA->outputGain);
	if (p->spsTxOutB)
		pd(p->spsTxOutB->outputGain);
	pd(p->txPttIn);
	pd(p->txPttOut);

	pd(p->tracetype);
	pd(p->b.radioactive);
	pd(p->b.txboost);
	pd(p->b.txCtcssOff);
}

/*
	takes data from a chan_usbradio_pvt struct (e.g. o->)
	and configures the xpmr radio layer
*/
/*!
 * \brief Configure xpmr (DSP radio) subsystem.
 * \param o			Private struct.
 * \retval 0		Success.
 * \retval 1		Failure.
 */

static int xpmr_config(struct chan_usbradio_pvt *o)
{
	if (o->pmrChan == NULL) {
		ast_log(LOG_ERROR, "pmr channel structure NULL\n");
		return 1;
	}

	o->pmrChan->rxCtcss->relax = o->rxctcssrelax;
	o->pmrChan->txpower = 0;

	if (o->remoted) {
		o->pmrChan->pTxCodeDefault = o->set_txctcssdefault;
		o->pmrChan->pRxCodeSrc = o->set_rxctcssfreqs;
		o->pmrChan->pTxCodeSrc = o->set_txctcssfreqs;

		o->pmrChan->rxfreq = o->set_rxfreq;
		o->pmrChan->txfreq = o->set_txfreq;
		/* printf(" remoted %s %s --> %s \n",o->pmrChan->txctcssdefault,
		   o->pmrChan->txctcssfreq,o->pmrChan->rxctcssfreq); */
	} else {
		// set xpmr pointers to source strings

		o->pmrChan->pTxCodeDefault = o->txctcssdefault;
		o->pmrChan->pRxCodeSrc = o->rxctcssfreqs;
		o->pmrChan->pTxCodeSrc = o->txctcssfreqs;

		o->pmrChan->rxfreq = o->rxfreq;
		o->pmrChan->txfreq = o->txfreq;
	}

	if (o->forcetxcode) {
		o->pmrChan->pTxCodeDefault = o->set_txctcssfreq;
		ast_debug(3, "Channel %s: Forced Tx Squelch Code code=%s.\n", o->name, o->pmrChan->pTxCodeDefault);
	}

	code_string_parse(o->pmrChan);
	if (o->pmrChan->rxfreq) {
		o->pmrChan->b.reprog = 1;
	}

	return 0;
}

/*!
 * \brief Store configuration.
 *	Initializes chan_usbradio and loads it with the configuration data.
 * \param cfg			ast_config structure.
 * \param ctg			Category.
 * \return				chan_usbradio_pvt.
 */
static struct chan_usbradio_pvt *store_config(const struct ast_config *cfg, const char *ctg)
{
	const struct ast_variable *v;
	struct chan_usbradio_pvt *o;
	char buf[100];
	int i;

	if (ctg == NULL) {
		o = &usbradio_default;
		ctg = "general";
	} else {
		/* "general" is also the default thing */
		if (strcmp(ctg, "general") == 0) {
			o = &usbradio_default;
		} else {
			if (!(o = ast_calloc(1, sizeof(*o)))) {
				return NULL;
			}
			*o = usbradio_default;
			o->name = ast_strdup(ctg);
			o->pttkick[0] = -1;
			o->pttkick[1] = -1;
			if (!usbradio_active) {
				usbradio_active = o->name;
			}
		}
	}
	o->echoq.q_forw = o->echoq.q_back = &o->echoq;
	ast_mutex_init(&o->echolock);
	ast_mutex_init(&o->eepromlock);
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
		CV_BOOL("rxcpusaver", o->rxcpusaver);
		CV_BOOL("txcpusaver", o->txcpusaver);
		CV_BOOL("invertptt", o->invertptt);
		CV_F("rxdemod", store_rxdemod(o, (char *) v->value));
		CV_BOOL("txlimonly", o->txlimonly);
		CV_BOOL("txprelim", o->txprelim);
		CV_F("txmixa", store_txmixa(o, (char *) v->value));
		CV_F("txmixb", store_txmixb(o, (char *) v->value));
		CV_F("carrierfrom", store_rxcdtype(o, (char *) v->value));
		CV_UINT("voxhangtime", o->voxhangtime);
		CV_F("ctcssfrom", store_rxsdtype(o, (char *) v->value));
		CV_UINT("rxsqvox", o->rxsqvoxadj);
		CV_UINT("rxsqhyst", o->rxsqhyst);
		CV_UINT("rxnoisefiltype", o->rxnoisefiltype);
		CV_UINT("rxsquelchdelay", o->rxsquelchdelay);
		CV_STR("txctcssdefault", o->txctcssdefault);
		CV_STR("rxctcssfreqs", o->rxctcssfreqs);
		CV_STR("txctcssfreqs", o->txctcssfreqs);
		CV_BOOL("rxctcssoverride", o->rxctcssoverride);
		CV_UINT("rxfreq", o->rxfreq);
		CV_UINT("txfreq", o->txfreq);
		CV_F("rxgain", store_rxgain(o, (char *) v->value));
		CV_BOOL("rxboost", o->rxboost);
		CV_BOOL("txboost", o->txboost);
		CV_UINT("rxctcssrelax", o->rxctcssrelax);
		CV_F("txtoctype", store_txtoctype(o, (char *) v->value));
		CV_UINT("hdwtype", o->hdwtype);
		CV_UINT("eeprom", o->wanteeprom);
		CV_UINT("duplex", o->radioduplex);
		CV_UINT("txsettletime", o->txsettletime);
		CV_UINT("txrxblankingtime", o->txrxblankingtime);
		CV_BOOL("rxpolarity", o->rxpolarity);
		CV_BOOL("txpolarity", o->txpolarity);
		CV_BOOL("dcsrxpolarity", o->dcsrxpolarity);
		CV_BOOL("dcstxpolarity", o->dcstxpolarity);
		CV_BOOL("lsdrxpolarity", o->lsdrxpolarity);
		CV_BOOL("lsdtxpolarity", o->lsdtxpolarity);
		CV_BOOL("radioactive", o->radioactive);
		CV_UINT("rptnum", o->rptnum);
		CV_UINT("idleinterval", o->idleinterval);
		CV_UINT("turnoffs", o->turnoffs);
		CV_UINT("tracetype", o->tracetype);
		CV_UINT("tracelevel", o->tracelevel);
		CV_UINT("rxondelay", o->rxondelay);
		if (o->rxondelay > MS_TO_FRAMES(RX_ON_DELAY_MAX)) {
			o->rxondelay = MS_TO_FRAMES(RX_ON_DELAY_MAX);
		}
		CV_UINT("txoffdelay", o->txoffdelay);
		if (o->txoffdelay > MS_TO_FRAMES(TX_OFF_DELAY_MAX)) {
			o->txoffdelay = MS_TO_FRAMES(TX_OFF_DELAY_MAX);
		}
		CV_UINT("area", o->area);
		CV_STR("ukey", o->ukey);
		CV_UINT("duplex3", o->duplex3);
		CV_UINT("rxlpf", o->rxlpf);
		CV_UINT("rxhpf", o->rxhpf);
		CV_UINT("txlpf", o->txlpf);
		CV_UINT("txhpf", o->txhpf);
		CV_UINT("sendvoter", o->sendvoter);
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

	if (o->rxsdtype != SD_XPMR) {
		o->rxctcssfreqs[0] = 0;
		o->txctcssfreqs[0] = 0;
	}

	if ((o->txmixa == TX_OUT_COMPOSITE) && (o->txmixb == TX_OUT_VOICE)) {
		ast_log(LOG_ERROR, "Invalid Configuration: Can not have B channel be Voice with A channel being Composite!!\n");
	}
	if ((o->txmixb == TX_OUT_COMPOSITE) && (o->txmixa == TX_OUT_VOICE)) {
		ast_log(LOG_ERROR, "Invalid Configuration: Can not have A channel be Voice with B channel being Composite!!\n");
	}

	if (o == &usbradio_default) { /* we are done with the default */
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

	load_tune_config(o, NULL, 0);

	/* if we are using the EEPROM, request hidthread load the EEPROM */
	if (o->wanteeprom) {
		ast_mutex_lock(&o->eepromlock);
		while (o->eepromctl) {
			ast_mutex_unlock(&o->eepromlock);
			usleep(10000);
			ast_mutex_lock(&o->eepromlock);
		}
		o->eepromctl = 1; /* request a load */
		ast_mutex_unlock(&o->eepromlock);
	}
	o->dsp = ast_dsp_new();
	if (o->dsp) {
		ast_dsp_set_features(o->dsp, DSP_FEATURE_DIGIT_DETECT);
		ast_dsp_set_digitmode(o->dsp, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
	}
	if (o->rxsqhyst == 0)
		o->rxsqhyst = 3000;

	if (o->rxsquelchdelay > RXSQDELAYBUFSIZE / 8 - 1) {
		ast_log(LOG_WARNING, "rxsquelchdelay of %i is > maximum of %i. Set to maximum.\n", o->rxsquelchdelay, RXSQDELAYBUFSIZE / 8 - 1);
		o->rxsquelchdelay = RXSQDELAYBUFSIZE / 8 - 1;
	}
	if (o->pmrChan == NULL) {
		t_pmr_chan tChan;

		// ast_log(LOG_NOTICE,"createPmrChannel() %s\n",o->name);
		memset(&tChan, 0, sizeof(t_pmr_chan));

		tChan.pTxCodeDefault = o->txctcssdefault;
		tChan.pRxCodeSrc = o->rxctcssfreqs;
		tChan.pTxCodeSrc = o->txctcssfreqs;

		tChan.rxDemod = o->rxdemod;
		tChan.rxCdType = o->rxcdtype;
		tChan.voxHangTime = o->voxhangtime;
		tChan.rxCarrierHyst = o->rxsqhyst;
		tChan.rxSqVoxAdj = o->rxsqvoxadj;
		tChan.rxSquelchDelay = o->rxsquelchdelay;

		if (o->txlimonly) {
			tChan.txMod = 1;
		}
		if (o->txprelim) {
			tChan.txMod = 2;
		}

		tChan.txMixA = o->txmixa;
		tChan.txMixB = o->txmixb;

		tChan.rxCpuSaver = o->rxcpusaver;
		tChan.txCpuSaver = o->txcpusaver;

		tChan.b.rxpolarity = o->rxpolarity;
		tChan.b.txpolarity = o->txpolarity;

		tChan.b.dcsrxpolarity = o->dcsrxpolarity;
		tChan.b.dcstxpolarity = o->dcstxpolarity;

		tChan.b.lsdrxpolarity = o->lsdrxpolarity;
		tChan.b.lsdtxpolarity = o->lsdtxpolarity;

		tChan.b.txboost = o->txboost;
		tChan.tracetype = o->tracetype;
		tChan.tracelevel = o->tracelevel;

		tChan.rptnum = o->rptnum;
		tChan.idleinterval = o->idleinterval;
		tChan.turnoffs = o->turnoffs;
		tChan.area = o->area;
		tChan.ukey = o->ukey;
		tChan.name = o->name;
		tChan.fever = o->fever;

		tChan.rxhpf = o->rxhpf;
		tChan.rxlpf = o->rxlpf;
		tChan.txhpf = o->txhpf;
		tChan.txlpf = o->txlpf;

		o->pmrChan = createPmrChannel(&tChan, FRAME_SIZE);

		o->pmrChan->radioDuplex = o->radioduplex;
		o->pmrChan->b.loopback = 0;
		o->pmrChan->b.radioactive = o->radioactive;
		o->pmrChan->txsettletime = o->txsettletime;
		o->pmrChan->txrxblankingtime = o->txrxblankingtime;
		o->pmrChan->rxCpuSaver = o->rxcpusaver;
		o->pmrChan->txCpuSaver = o->txcpusaver;

		*(o->pmrChan->prxSquelchAdjust) = ((999 - o->rxsquelchadj) * 32767) / AUDIO_ADJUSTMENT;
		*(o->pmrChan->prxVoiceAdjust) = o->rxvoiceadj * M_Q8;
		o->pmrChan->rxCtcss->relax = o->rxctcssrelax;
		o->pmrChan->txTocType = o->txtoctype;

#if 0
		if ((o->txmixa == TX_OUT_LSD) ||
			(o->txmixa == TX_OUT_COMPOSITE) || (o->txmixb == TX_OUT_LSD) || (o->txmixb == TX_OUT_COMPOSITE)) {
			set_txctcss_level(o);
		}
#endif
		if ((o->txmixa != TX_OUT_VOICE) && (o->txmixb != TX_OUT_VOICE) && (o->txmixa != TX_OUT_COMPOSITE) && (o->txmixb != TX_OUT_COMPOSITE)) {
			ast_log(LOG_ERROR, "No txvoice output configured.\n");
		}

		if (o->txctcssfreq[0] && o->txmixa != TX_OUT_LSD && o->txmixa != TX_OUT_COMPOSITE && o->txmixb != TX_OUT_LSD &&
			o->txmixb != TX_OUT_COMPOSITE) {
			ast_log(LOG_ERROR, "No txtone output configured.\n");
		}

		if (o->radioactive) {
			struct chan_usbradio_pvt *ao;
			for (ao = usbradio_default.next; ao && ao->name; ao = ao->next) {
				ao->pmrChan->b.radioactive = 0;
			}
			usbradio_active = o->name;
			o->pmrChan->b.radioactive = 1;
			ast_log(LOG_NOTICE, "radio active set to [%s]\n", o->name);
		}
	}

#if 0
	xpmr_config(o);

	TRACEO(1, ("store_config() 120\n"));
	mixer_write(o);
	TRACEO(1, ("store_config() 130\n"));
	mult_set(o);
#endif

	hidhdwconfig(o);

	/* link into list of devices */
	if (o != &usbradio_default) {
		o->next = usbradio_default.next;
		usbradio_default.next = o;
	}
	return o;
}

#if DEBUG_FILETEST == 1
/*
	Test It on a File
*/
int RxTestIt(struct chan_usbradio_pvt *o)
{
	const int numSamples = SAMPLES_PER_BLOCK;
	const int numChannels = 16;

	i16 sample, i, ii;

	i32 txHangTime;

	i16 txEnable;

	t_pmr_chan tChan;
	t_pmr_chan *pChan;

	FILE *hInput = NULL, *hOutput = NULL, *hOutputTx = NULL;

	i16 iBuff[numSamples * 2 * 6], oBuff[numSamples];

	printf("RxTestIt()\n");

	pChan = o->pmrChan;
	pChan->b.txCapture = 1;
	pChan->b.rxCapture = 1;

	txEnable = 0;

	hInput = fopen("/usr/src/xpmr/testdata/rx_in.pcm", "r");
	if (!hInput) {
		printf(" RxTestIt() File Not Found.\n");
		return 0;
	}
	hOutput = fopen("/usr/src/xpmr/testdata/rx_debug.pcm", "w");

	printf(" RxTestIt() Working...\n");

	while (!feof(hInput)) {
		fread((void *) iBuff, 2, numSamples * 2 * 6, hInput);

		if (txHangTime)
			txHangTime -= numSamples;
		if (txHangTime < 0)
			txHangTime = 0;

		if (pChan->rxCtcss->decode)
			txHangTime = (8000 / 1000 * 2000);

		if (pChan->rxCtcss->decode && !txEnable) {
			txEnable = 1;
			// pChan->inputBlanking=(8000/1000*200);
		} else if (!pChan->rxCtcss->decode && txEnable) {
			txEnable = 0;
		}

		PmrRx(pChan, iBuff, oBuff);

		fwrite((void *) pChan->prxDebug, 2, numSamples * numChannels, hOutput);
	}
	pChan->b.txCapture = 0;
	pChan->b.rxCapture = 0;

	if (hInput)
		fclose(hInput);
	if (hOutput)
		fclose(hOutput);

	printf(" RxTestIt() Complete.\n");

	return 0;
}
#endif

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
 * \param cmd			Cli command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_console_key(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "radio key";
		e->usage = "Usage: radio key\n"
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
		e->command = "radio unkey";
		e->usage = "Usage: radio unkey\n"
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
static char *handle_radio_tune(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "radio tune "
					 "{auxvoice|dump|swap|rxnoise|rxvoice|rxtone|txvoice|txtone|txall|flash|rxsquelch|nocap|rxtracecap|"
					 "txtracecap|rxcap|txcap|save|load|menu-support|txslimsp}";
		e->usage = "Usage: radio tune <function>\n"
				   "       rxnoise\n"
				   "       rxvoice\n"
				   "       rxtone\n"
				   "       rxsquelch [newsetting]\n"
				   "       txvoice [newsetting]\n"
				   "       txtone [newsetting]\n"
				   "       txslimsp [setpoint]\n"
				   "       auxvoice [newsetting]\n"
				   "       save (settings to tuning file)\n"
				   "       load (tuning settings from EEPROM)\n\n"
				   "       All [newsetting]'s are values 0-999\n"
				   "       [setpoint] is 5000 to 13000\n\n";

		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(radio_tune(a->fd, a->argc, a->argv));
}

/*!
 * \brief Handle Asterisk CLI request active device command.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_radio_active(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "radio active";
		e->usage = "Usage: radio active [device-name]\n"
				   "       If used without a parameter, displays which device is the current\n"
				   "       one being commanded.  If a device is specified, the commanded radio device is changed\n"
				   "       to the device specified.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(radio_active(a->fd, a->argc, a->argv));
}

/*!
 * \brief Handle Asterisk CLI request for radio show settings.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_show_settings(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct chan_usbradio_pvt *o;

	switch (cmd) {
	case CLI_INIT:
		e->command = "radio show settings";
		e->usage = "Usage: radio show settings\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	o = find_desc(usbradio_active);
	if (o) {
		_menu_print(a->fd, o);
	}
	return RESULT_SUCCESS;
}

/*!
 * \brief Handle Asterisk CLI request to set xdebug.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_set_xdebug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "radio set xdebug";
		e->usage = "Usage: radio set xdebug [level]\n"
				   "       Level 0 to 100.\n"
				   "       Set xpmr debug level.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(radio_set_xpmr_debug(a->fd, a->argc, a->argv));
}

static struct ast_cli_entry cli_usbradio[] = { AST_CLI_DEFINE(handle_console_key, "Simulate Rx Signal Present"),
	AST_CLI_DEFINE(handle_console_unkey, "Simulate Rx Signal Loss"), AST_CLI_DEFINE(handle_radio_tune, "Change radio settings"),
	AST_CLI_DEFINE(handle_radio_active, "Change commanded device"),
	AST_CLI_DEFINE(handle_set_xdebug, "Radio set xpmr debug level"), AST_CLI_DEFINE(handle_show_settings, "Show device settings") };

#include "./xpmr/xpmr.c"
#ifdef HAVE_XPMRX
#include "./xpmrx/xpmrx.c"
#endif

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
	if (!(usbradio_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append(usbradio_tech.capabilities, ast_format_slin, 0);

	if (ast_radio_hid_device_mklist()) {
		ast_log(LOG_ERROR, "Unable to make hid list\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	usbradio_active = NULL;

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	pp_val = 0;
	hasout = 0;

	/* load our module configuration */
	if (load_config(0)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (find_desc(usbradio_active) == NULL) {
		ast_log(LOG_NOTICE, "radio active device %s not found\n", usbradio_active);
		/* XXX we could default to 'dsp' perhaps ? */
		/* XXX should cleanup allocated memory etc. */
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_channel_register(&usbradio_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel type 'usb'\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	ast_cli_register_multiple(cli_usbradio, sizeof(cli_usbradio) / sizeof(struct ast_cli_entry));

	if (haspp && hasout) {
		ast_pthread_create_background(&pulserid, NULL, pulserthread, NULL);
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	struct chan_usbradio_pvt *o;

	stoppulser = 1;

	ast_channel_unregister(&usbradio_tech);
	ast_cli_unregister_multiple(cli_usbradio, sizeof(cli_usbradio) / sizeof(struct ast_cli_entry));

	for (o = usbradio_default.next; o; o = o->next) {
		if (o->pmrChan) {
			destroyPmrChannel(o->pmrChan);
		}

#if DEBUG_CAPTURES == 1
		if (frxcapraw) {
			fclose(frxcapraw);
			frxcapraw = NULL;
		}
		if (frxcaptrace) {
			fclose(frxcaptrace);
			frxcaptrace = NULL;
		}
		if (frxoutraw) {
			fclose(frxoutraw);
			frxoutraw = NULL;
		}
		if (ftxcapraw) {
			fclose(ftxcapraw);
			ftxcapraw = NULL;
		}
		if (ftxcaptrace) {
			fclose(ftxcaptrace);
			ftxcaptrace = NULL;
		}
		if (ftxoutraw) {
			fclose(ftxoutraw);
			ftxoutraw = NULL;
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
		if (o->owner) { /* XXX how ??? */
			return -1;
		}
		/* XXX what about the thread ? */
		/* XXX what about the memory allocated ? */
	}

	ao2_cleanup(usbradio_tech.capabilities);
	usbradio_tech.capabilities = NULL;

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "USB Console Channel Driver", .support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module, .unload = unload_module, .reload = reload_module, .requires = "res_usbradio", );
