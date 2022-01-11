/* #define	NEW_ASTERISK */
/*
 * Asterisk -- An open source telephony toolkit.
 * * Copyright (C) 1999 - 2005, Digium, Inc.
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
        <defaultenabled>yes</defaultenabled>
 ***/

#include "asterisk.h"

/*
 * Please change this revision number when you make a edit
 * use the simple format YYMMDD
*/

ASTERISK_FILE_VERSION(__FILE__,"$Revision$")
// ASTERISK_FILE_VERSION(__FILE__,"$Revision$")

#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <sys/io.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <usb.h>
#include <search.h>
#include <alsa/asoundlib.h>
#include <linux/ppdev.h>
#include <linux/parport.h>
#include <linux/version.h>

#include "../allstar/pocsag.c"

#define DEBUG_CAPTURES	 		1

#define RX_CAP_RAW_FILE			"/tmp/rx_cap_in.pcm"
#define RX_CAP_COOKED_FILE		"/tmp/rx_cap_8k_in.pcm"
#define TX_CAP_RAW_FILE			"/tmp/tx_cap_in.pcm"

#define	MIXER_PARAM_MIC_PLAYBACK_SW "Mic Playback Switch"
#define MIXER_PARAM_MIC_PLAYBACK_VOL "Mic Playback Volume"
#define	MIXER_PARAM_MIC_CAPTURE_SW "Mic Capture Switch"
#define	MIXER_PARAM_MIC_CAPTURE_VOL "Mic Capture Volume"
#define	MIXER_PARAM_MIC_BOOST "Auto Gain Control"
#define	MIXER_PARAM_SPKR_PLAYBACK_SW "Speaker Playback Switch"
#define	MIXER_PARAM_SPKR_PLAYBACK_VOL "Speaker Playback Volume"
#define	MIXER_PARAM_SPKR_PLAYBACK_SW_NEW "Headphone Playback Switch"
#define	MIXER_PARAM_SPKR_PLAYBACK_VOL_NEW "Headphone Playback Volume"

#define	DELIMCHR ','
#define	QUOTECHR 34

#define	READERR_THRESHOLD 50


#if 0
#define traceusb1(a) {printf a;}
#else
#define traceusb1(a)
#endif

#if 0
#define traceusb2(a) {printf a;}
#else
#define traceusb2(a)
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

#ifndef	NEW_ASTERISK

/* ringtones we use */
#include "busy.h"
#include "ringtone.h"
#include "ring10.h"
#include "answer.h"

#endif

#define C108_VENDOR_ID		0x0d8c
#define C108_PRODUCT_ID  	0x000c
#define C108B_PRODUCT_ID  	0x0012
#define C108AH_PRODUCT_ID  	0x013c
#define N1KDO_PRODUCT_ID  	0x6a00
#define C119_PRODUCT_ID  	0x0008
#define C119A_PRODUCT_ID  	0x013a
#define C119B_PRODUCT_ID        0x0013
#define C108_HID_INTERFACE	3

#define HID_REPORT_GET		0x01
#define HID_REPORT_SET		0x09

#define HID_RT_INPUT		0x01
#define HID_RT_OUTPUT		0x02

#define	EEPROM_START_ADDR	6
#define	EEPROM_END_ADDR		63
#define	EEPROM_PHYSICAL_LEN	64
#define EEPROM_TEST_ADDR	EEPROM_END_ADDR
#define	EEPROM_MAGIC_ADDR	6
#define	EEPROM_MAGIC		34329
#define	EEPROM_CS_ADDR		62
#define	EEPROM_RXMIXERSET	8
#define	EEPROM_TXMIXASET	9
#define	EEPROM_TXMIXBSET	10
#define	EEPROM_RXVOICEADJ	11
#define	EEPROM_RXCTCSSADJ	13
#define	EEPROM_TXCTCSSADJ	15
#define	EEPROM_RXSQUELCHADJ	16

#define	NTAPS 31
#define	NTAPS_PL 6

#define	DEFAULT_ECHO_MAX 1000  /* 20 secs of echo buffer, max */

#define	PP_MASK 0xbffc
#define	PP_PORT "/dev/parport0"
#define	PP_IOPORT 0x378

#define	PAGER_SRC "PAGER"
#define	ENDPAGE_STR "ENDPAGE"
#define AMPVAL 12000
#define	SAMPRATE 8000 // (Sample Rate)
#define	DIVLCM 192000  // (Least Common Mult of 512,1200,2400,8000)
#define	PREAMBLE_BITS 576
#define	MESSAGE_BITS 544 // (17 * 32), 1 longword SYNC plus 16 longwords data
#define	ONEVAL -AMPVAL
#define ZEROVAL AMPVAL
#define	DIVSAMP (DIVLCM / SAMPRATE)

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
 * simpleusb.conf parameters are
START_CONFIG

[general]
    ; General config options which propigate to all devices, with
    ; default values shown. You may have as many devices as the
    ; system will allow. You must use one section per device, with
    ; [usb] generally (although its up to you) being the first device.
    ;
    ;
    ; debug = 0x0		; misc debug flags, default is 0

	; Set hardware type here
	; hdwtype = 0               ; 0=limey, 1=sph

	; rxboost = 0          ; no rx gain boost
	; txboost = 0	       ; no tx gain boost
	; carrierfrom = usb    ;no,usb,usbinvert
	; ctcssfrom = usb      ;no,usb,usbinvert

	; pager = no		;no,a,b (e.g. pager = b means "put the normal repeat audio on channel A, and the pager audio on channel B")

	; invertptt = 0

        ; duplex = 1		; duplex mode

	; duplex3 = 0		; duplex 3 gain setting (0 to disable)

	; rxondelay = 0		  ; number of 20ms intervals to hold off receiver turn-on indication

        ; eeprom = no		; no eeprom installed

    ;------------------------------ JITTER BUFFER CONFIGURATION --------------------------
    ; jbenable = yes              ; Enables the use of a jitterbuffer on the receiving side of an
                                  ; simpleusb channel. Defaults to "no". An enabled jitterbuffer will
                                  ; be used only if the sending side can create and the receiving
                                  ; side can not accept jitter. The simpleusb channel can't accept jitter,
                                  ; thus an enabled jitterbuffer on the receive simpleusb side will always
                                  ; be used if the sending side can create jitter.

    ; jbmaxsize = 200             ; Max length of the jitterbuffer in milliseconds.

    ; jbresyncthreshold = 1000    ; Jump in the frame timestamps over which the jitterbuffer is
                                  ; resynchronized. Useful to improve the quality of the voice, with
                                  ; big jumps in/broken timestamps, usualy sent from exotic devices
                                  ; and programs. Defaults to 1000.

    ; jbimpl = fixed              ; Jitterbuffer implementation, used on the receiving side of an simpleusb
                                  ; channel. Two implementations are currenlty available - "fixed"
                                  ; (with size always equals to jbmax-size) and "adaptive" (with
                                  ; variable size, actually the new jb of IAX2). Defaults to fixed.

    ; jblog = no                  ; Enables jitterbuffer frame logging. Defaults to "no".
    ;-----------------------------------------------------------------------------------

[usb]

; First channel unique config

[usb1]

; Second channel config

END_CONFIG

 */

/*
 * Helper macros to parse config arguments. They will go in a common
 * header file if their usage is globally accepted. In the meantime,
 * we define them here. Typical usage is as below.
 * Remember to open a block right before M_START (as it declares
 * some variables) and use the M_* macros WITHOUT A SEMICOLON:
 *
 *	{
 *		M_START(v->name, v->value) 
 *
 *		M_BOOL("dothis", x->flag1)
 *		M_STR("name", x->somestring)
 *		M_F("bar", some_c_code)
 *		M_END(some_final_statement)
 *		... other code in the block
 *	}
 *
 * XXX NOTE these macros should NOT be replicated in other parts of asterisk. 
 * Likely we will come up with a better way of doing config file parsing.
 */
#define M_START(var, val) \
        char *__s = var; char *__val = val;
#define M_END(x)   x;
#define M_F(tag, f)			if (!strcasecmp((__s), tag)) { f; } else
#define M_BOOL(tag, dst)	M_F(tag, (dst) = ast_true(__val) )
#define M_UINT(tag, dst)	M_F(tag, (dst) = strtoul(__val, NULL, 0) )
#define M_STR(tag, dst)		M_F(tag, ast_copy_string(dst, __val, sizeof(dst)))

/*
 * The following parameters are used in the driver:
 *
 *  FRAME_SIZE	the size of an audio frame, in samples.
 *		160 is used almost universally, so you should not change it.
 *
 *  FRAGS	the argument for the SETFRAGMENT ioctl.
 *		Overridden by the 'frags' parameter in simpleusb.conf
 *
 *		Bits 0-7 are the base-2 log of the device's block size,
 *		bits 16-31 are the number of blocks in the driver's queue.
 *		There are a lot of differences in the way this parameter
 *		is supported by different drivers, so you may need to
 *		experiment a bit with the value.
 *		A good default for linux is 30 blocks of 64 bytes, which
 *		results in 6 frames of 320 bytes (160 samples).
 *		FreeBSD works decently with blocks of 256 or 512 bytes,
 *		leaving the number unspecified.
 *		Note that this only refers to the device buffer size,
 *		this module will then try to keep the lenght of audio
 *		buffered within small constraints.
 *
 *  QUEUE_SIZE	The max number of blocks actually allowed in the device
 *		driver's buffer, irrespective of the available number.
 *		Overridden by the 'queuesize' parameter in simpleusb.conf
 *
 *		Should be >=2, and at most as large as the hw queue above
 *		(otherwise it will never be full).
 */

#define FRAME_SIZE	160
#define	QUEUE_SIZE	5

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

#if 0
#define	TRYOPEN	1				/* try to open on startup */
#endif
#define	O_CLOSE	0x444			/* special 'close' mode for device */
/* Which device to use */
#if defined( __OpenBSD__ ) || defined( __NetBSD__ )
#define DEV_DSP "/dev/audio"
#else
#define DEV_DSP "/dev/dsp"
#endif

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

static char *config = "simpleusb.conf";	/* default config file */
static char *config1 = "simpleusb_tune_%s.conf";    /* tune config file */

static FILE *frxcapraw = NULL;
static FILE *frxcapcooked = NULL;
static FILE *ftxcapraw = NULL;

static char *usb_device_list = NULL;
static int usb_device_list_size = 0;
AST_MUTEX_DEFINE_STATIC(usb_list_lock);
AST_MUTEX_DEFINE_STATIC(usb_dev_lock);
AST_MUTEX_DEFINE_STATIC(pp_lock);

static int8_t	pp_val;
static int8_t	pp_pulsemask;
static int8_t	pp_lastmask;
static	int	pp_pulsetimer[32];
static	char	haspp;
static	int	ppfd;
static 	char	pport[50];
static	int	pbase;
static 	char	stoppulser;
static	char	hasout;
pthread_t pulserid;

static int simpleusb_debug;

enum {CD_IGNORE,CD_HID,CD_HID_INVERT,CD_PP,CD_PP_INVERT};
enum {SD_IGNORE,SD_HID,SD_HID_INVERT,SD_PP,SD_PP_INVERT};    				 // no,external,externalinvert,software
enum {PAGER_NONE,PAGER_A,PAGER_B};

/*	DECLARE STRUCTURES */

/*
 * Each sound is made of 'datalen' samples of sound, repeated as needed to
 * generate 'samplen' samples of data, then followed by 'silencelen' samples
 * of silence. The loop is repeated if 'repeat' is set.
 */
struct sound {
	int ind;
	char *desc;
	short *data;
	int datalen;
	int samplen;
	int silencelen;
	int repeat;
};

#ifndef	NEW_ASTERISK

static struct sound sounds[] = {
	{ AST_CONTROL_RINGING, "RINGING", ringtone, sizeof(ringtone)/2, 16000, 32000, 1 },
	{ AST_CONTROL_BUSY, "BUSY", busy, sizeof(busy)/2, 4000, 4000, 1 },
	{ AST_CONTROL_CONGESTION, "CONGESTION", busy, sizeof(busy)/2, 2000, 2000, 1 },
	{ AST_CONTROL_RING, "RING10", ring10, sizeof(ring10)/2, 16000, 32000, 1 },
	{ AST_CONTROL_ANSWER, "ANSWER", answer, sizeof(answer)/2, 2200, 0, 0 },
	{ -1, NULL, 0, 0, 0, 0 },	/* end marker */
};

#endif

struct usbecho {
struct qelem *q_forw;
struct qelem *q_prev;
short data[FRAME_SIZE];
} ;

/*
 * descriptor for one of our channels.
 * There is one used for 'default' values (from the [general] entry in
 * the configuration file), and then one instance for each device
 * (the default is cloned from [general], others are only created
 * if the relevant section exists).
 */
struct chan_simpleusb_pvt {
	struct chan_simpleusb_pvt *next;

	char *name;
	int index;
#ifndef	NEW_ASTERISK
	/*
	 * cursound indicates which in struct sound we play. -1 means nothing,
	 * any other value is a valid sound, in which case sampsent indicates
	 * the next sample to send in [0..samplen + silencelen]
	 * nosound is set to disable the audio data from the channel
	 * (so we can play the tones etc.).
	 */
	int sndcmd[2];				/* Sound command pipe */
	int cursound;				/* index of sound to send */
	int sampsent;				/* # of sound samples sent  */
	int nosound;				/* set to block audio from the PBX */
#endif

	int devtype;				/* actual type of device */
	int pttkick[2];
	int total_blocks;			/* total blocks in the output device */
	int sounddev;
	enum { M_UNSET, M_FULL, M_READ, M_WRITE } duplex;
	short cdMethod;
	int autoanswer;
	int autohangup;
	int hookstate;
	int usedtmf;
	unsigned int queuesize;		/* max fragments in queue */
	unsigned int frags;			/* parameter for SETFRAGMENT */

	int warned;					/* various flags used for warnings */
#define WARN_used_blocks	1
#define WARN_speed		2
#define WARN_frag		4
	int w_errors;				/* overfull in the write path */
	struct timeval lastopen;

	int overridecontext;
	int mute;

	/* boost support. BOOST_SCALE * 10 ^(BOOST_MAX/20) must
	 * be representable in 16 bits to avoid overflows.
	 */
#define	BOOST_SCALE	(1<<9)
#define	BOOST_MAX	40			/* slightly less than 7 bits */
	int boost;					/* input boost, scaled by BOOST_SCALE */
	char devicenum;
	char devstr[128];
	int spkrmax;
	int micmax;
	int micplaymax;

#ifndef	NEW_ASTERISK
	pthread_t sthread;
#endif
	pthread_t hidthread;

	int stophid;
	FILE *hkickhid;

	struct ast_channel *owner;
	char ext[AST_MAX_EXTENSION];
	char ctx[AST_MAX_CONTEXT];
	char language[MAX_LANGUAGE];
	char cid_name[256];			/*XXX */
	char cid_num[256];			/*XXX */
	char mohinterpret[MAX_MUSICCLASS];

	/* buffers used in simpleusb_write, 2 per int */
	char simpleusb_write_buf[FRAME_SIZE * 2];    

	int simpleusb_write_dst;
	/* buffers used in simpleusb_read - AST_FRIENDLY_OFFSET space for headers
	 * plus enough room for a full frame
	 */
	char simpleusb_read_buf[FRAME_SIZE * 4 * 6];
	char simpleusb_read_frame_buf[FRAME_SIZE * 2 + AST_FRIENDLY_OFFSET];
#ifdef 	OLD_ASTERISK
	AST_LIST_HEAD(, ast_frame) txq;
#else
	AST_LIST_HEAD_NOLOCK(, ast_frame) txq;
#endif
	ast_mutex_t  txqlock;
	int readpos;				/* read position above */
	struct ast_frame read_f;	/* returned by simpleusb_read */

	char 	debuglevel;
	char 	radioduplex;			// 
	char    wanteeprom;

	int  	tracetype;
	int     tracelevel;

	char lastrx;
	char rxhidsq;
	char rxhidctcss;
	char rxppsq;
	char rxppctcss;

	char rxkeyed;	  			// indicates rx signal present

	char rxctcssoverride;

	char lasttx;
	char txkeyed;				// tx key request from upper layers 
	char txtestkey;
	char txclikey;

	time_t lasthidtime;
	struct ast_dsp *dsp;

	short	flpt[NTAPS + 1];
	short	flpr[NTAPS + 1];

	float	hpx[NTAPS_PL + 1];
	float	hpy[NTAPS_PL + 1];

	int32_t	destate;
	int32_t	prestate;

	char    rxcpusaver;
	char    txcpusaver;

	char 	rxcdtype;
	char 	rxsdtype;

	char	invertptt;

	int	rxondelay;
	int	rxoncnt;

	int	pager;
	int	waspager;

	int	rxboostset;
	int	rxmixerset;
	float	rxvoiceadj;
	int 	txmixaset;
	int 	txmixbset;

	int echomode;
	int echoing;
	ast_mutex_t	echolock;
	struct qelem echoq;
	int echomax;

	int    	hdwtype;
	int		hid_gpio_ctl;		
	int		hid_gpio_ctl_loc;	
	int		hid_io_cor; 		
	int		hid_io_cor_loc; 	
	int		hid_io_ctcss;		
	int		hid_io_ctcss_loc; 	
	int		hid_io_ptt; 		
	int		hid_gpio_loc; 		
	int32_t		hid_gpio_val;
	int32_t		valid_gpios; 		
	int32_t		gpio_set;
	int32_t		last_gpios_in;
	int		had_gpios_in;
	int		hid_gpio_pulsetimer[32];
	int32_t		hid_gpio_pulsemask;
	int32_t		hid_gpio_lastmask;

	int8_t		last_pp_in;
	char		had_pp_in;

	char	newname;

	struct {
	    unsigned rxcapraw:1;
	    unsigned txcapraw:1;
	    unsigned measure_enabled:1;
	}b;
	unsigned short eeprom[EEPROM_PHYSICAL_LEN];
	char eepromctl;
	ast_mutex_t eepromlock;

	struct usb_dev_handle *usb_handle;
	int readerrs;
	char hasusb;
	char usbass;
	int32_t discfactor;
	int32_t discounterl;
	int32_t discounteru;
	int16_t amax;
	int16_t amin;
	int16_t apeak;
	int plfilter;
	int deemphasis;
	int preemphasis;
	int duplex3;
	int32_t cur_gpios;
	char *gpios[32];
	char *pps[32];
	ast_mutex_t usblock;
};

static struct chan_simpleusb_pvt simpleusb_default = {
#ifndef	NEW_ASTERISK
	.cursound = -1,
#endif
	.sounddev = -1,
	.duplex = 1,
	.autoanswer = 1,
	.autohangup = 1,
	.queuesize = QUEUE_SIZE,
	.frags = FRAGS,
	.ext = "s",
	.ctx = "default",
	.readpos = 0,	/* start here on reads */
	.lastopen = { 0, 0 },
	.boost = BOOST_SCALE,
	.wanteeprom = 1,
	.usedtmf = 1,
	.rxondelay = 0,
	.pager = PAGER_NONE,
};

/*	DECLARE FUNCTION PROTOTYPES	*/

static int	hidhdwconfig(struct chan_simpleusb_pvt *o);
static void mixer_write(struct chan_simpleusb_pvt *o);
static void tune_write(struct chan_simpleusb_pvt *o);

static char *simpleusb_active;	 /* the active device */

static int setformat(struct chan_simpleusb_pvt *o, int mode);

static struct ast_channel *simpleusb_request(const char *type, int format, void *data
, int *cause);
static int simpleusb_digit_begin(struct ast_channel *c, char digit);
static int simpleusb_digit_end(struct ast_channel *c, char digit, unsigned int duration);
static int simpleusb_text(struct ast_channel *c, const char *text);
static int simpleusb_hangup(struct ast_channel *c);
static int simpleusb_answer(struct ast_channel *c);
static struct ast_frame *simpleusb_read(struct ast_channel *chan);
static int simpleusb_call(struct ast_channel *c, char *dest, int timeout);
static int simpleusb_write(struct ast_channel *chan, struct ast_frame *f);
static int simpleusb_indicate(struct ast_channel *chan, int cond, const void *data, size_t datalen);
static int simpleusb_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int simpleusb_setoption(struct ast_channel *chan, int option, void *data, int datalen);

static char tdesc[] = "Simple USB (CM108) Radio Channel Driver";

static const struct ast_channel_tech simpleusb_tech = {
	.type = "SimpleUSB",
	.description = tdesc,
	.capabilities = AST_FORMAT_SLINEAR,
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

int	ppinshift[] = {0,0,0,0,0,0,0,0,0,0,6,7,5,4,0,3};

/* FIR Low pass filter, 2900 Hz passband with 0.5 db ripple, 6300 Hz stopband at 60db */

static short lpass(short input,short *z)
{
    int i;
    int accum;
    
    static short h[NTAPS] = {103,136,148,74,-113,-395,-694,
	-881,-801,-331,573,1836,3265,4589,5525,5864,5525,
	4589,3265,1836,573,-331,-801,-881,-694,-395, -113,
	74,148,136,103} ;

    /* store input at the beginning of the delay line */
    z[0] = input;

    /* calc FIR and shift data */
    accum = h[NTAPS - 1] * z[NTAPS - 1];
    for (i = NTAPS - 2; i >= 0; i--) {
        accum += h[i] * z[i];
        z[i + 1] = z[i];
    }

    return(accum >> 15);
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

/* Perform standard 6db/octave pre-emphasis */
static int16_t preemph(int16_t input,int32_t *state)
{

int16_t coeff00 = 17610;
int16_t coeff01 = -17610;
int16_t adjval = 13404;
int32_t y,temp0,temp1; 

	temp0 =	*state * coeff01;
	*state = input;
	temp1 = input * coeff00;
	y = (temp0 + temp1) / adjval;
	if (y > 32767) y=32767;
	else if (y <-32767) y=-32767;
	return(y);
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

/* lround for uClibc
 *
 * wrapper for lround(x)
 */
long lround(double x)
{
    return (long) ((x - ((long)x) >= 0.5f) ? (((long)x) + 1) : ((long)x));
}

static int make_spkr_playback_value(struct chan_simpleusb_pvt *o,int val)
{
int	v,rv;

	v = (val * o->spkrmax) / 1000;
	/* if just the old one, do it the old way */
	if (o->devtype == C108_PRODUCT_ID) return v;
	rv = (o->spkrmax + lround(20.0 * log10((float)(v + 1) / (float)(o->spkrmax + 1)) / 0.25));
	if (rv < 0) rv = 0;
	return rv;	
}

/* Call with:  devnum: alsa major device number, param: ascii Formal
Parameter Name, val1, first or only value, val2 second value, or 0 
if only 1 value. Values: 0-99 (percent) or 0-1 for baboon.

Note: must add -lasound to end of linkage */

static int amixer_max(int devnum,char *param)
{
int	rv,type;
char	str[100];
snd_hctl_t *hctl;
snd_ctl_elem_id_t *id;
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
		return(-1);
	}
	snd_ctl_elem_info_alloca(&info);
	snd_hctl_elem_info(elem,info);
	type = snd_ctl_elem_info_get_type(info);
	rv = 0;
	switch(type)
	{
	    case SND_CTL_ELEM_TYPE_INTEGER:
		rv = snd_ctl_elem_info_get_max(info);
		break;
	    case SND_CTL_ELEM_TYPE_BOOLEAN:
		rv = 1;
		break;
	}
	snd_hctl_close(hctl);
	return(rv);
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
		snd_ctl_elem_value_set_integer(control, 0, v1);
		if (v2 > 0) snd_ctl_elem_value_set_integer(control, 1, v2);
		break;
	    case SND_CTL_ELEM_TYPE_BOOLEAN:
		snd_ctl_elem_value_set_integer(control, 0, (v1 != 0));
		break;
	}
	if (snd_hctl_elem_write(elem, control))
	{
		snd_hctl_close(hctl);
		return(-1);
	}
	snd_hctl_close(hctl);
	return(0);
}

static void hid_set_outputs(struct usb_dev_handle *handle,
         unsigned char *outputs)
{
	usleep(1500);
	usb_control_msg(handle,
	      USB_ENDPOINT_OUT + USB_TYPE_CLASS + USB_RECIP_INTERFACE,
	      HID_REPORT_SET,
	      0 + (HID_RT_OUTPUT << 8),
	      C108_HID_INTERFACE,
	      (char*)outputs, 4, 5000);
}

static void hid_get_inputs(struct usb_dev_handle *handle,
         unsigned char *inputs)
{
	usleep(1500);
	usb_control_msg(handle,
	      USB_ENDPOINT_IN + USB_TYPE_CLASS + USB_RECIP_INTERFACE,
	      HID_REPORT_GET,
	      0 + (HID_RT_INPUT << 8),
	      C108_HID_INTERFACE,
	      (char*)inputs, 4, 5000);
}

static unsigned short read_eeprom(struct usb_dev_handle *handle, int addr)
{
	unsigned char buf[4];

	buf[0] = 0x80;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = 0x80 | (addr & 0x3f);
	hid_set_outputs(handle,buf);
	memset(buf,0,sizeof(buf));
	hid_get_inputs(handle,buf);
	return(buf[1] + (buf[2] << 8));
}

static void write_eeprom(struct usb_dev_handle *handle, int addr, 
   unsigned short data)
{

	unsigned char buf[4];

	buf[0] = 0x80;
	buf[1] = data & 0xff;
	buf[2] = data >> 8;
	buf[3] = 0xc0 | (addr & 0x3f);
	hid_set_outputs(handle,buf);
}

static unsigned short get_eeprom(struct usb_dev_handle *handle,
	unsigned short *buf)
{
int	i;
unsigned short cs;

	cs = 0xffff;
	for(i = EEPROM_START_ADDR; i < EEPROM_END_ADDR; i++)
	{
		cs += buf[i] = read_eeprom(handle,i);
	}
	return(cs);
}

static void put_eeprom(struct usb_dev_handle *handle,unsigned short *buf)
{
int	i;
unsigned short cs;

	cs = 0xffff;
	buf[EEPROM_MAGIC_ADDR] = EEPROM_MAGIC;
	for(i = EEPROM_START_ADDR; i < EEPROM_CS_ADDR; i++)
	{
		write_eeprom(handle,i,buf[i]);
		cs += buf[i];
	}
	buf[EEPROM_CS_ADDR] = (65535 - cs) + 1;
	write_eeprom(handle,i,buf[EEPROM_CS_ADDR]);
}

static struct usb_device *hid_device_init(char *desired_device)
{
    struct usb_bus *usb_bus;
    struct usb_device *dev;
    char devstr[8194],str[200],desdev[200],*cp;
    int i;
    FILE *fp;

    usb_init();
    usb_find_busses();
    usb_find_devices();
    for (usb_bus = usb_busses;
         usb_bus;
         usb_bus = usb_bus->next) {
        for (dev = usb_bus->devices;
             dev;
             dev = dev->next) {
            if ((dev->descriptor.idVendor
                  == C108_VENDOR_ID) &&
		(((dev->descriptor.idProduct & 0xfffc) == C108_PRODUCT_ID) ||
		(dev->descriptor.idProduct == C108B_PRODUCT_ID) ||
		(dev->descriptor.idProduct == C108AH_PRODUCT_ID) ||
		(dev->descriptor.idProduct == C119A_PRODUCT_ID) ||
		(dev->descriptor.idProduct == C119B_PRODUCT_ID) ||
		((dev->descriptor.idProduct & 0xff00)  == N1KDO_PRODUCT_ID) ||
		(dev->descriptor.idProduct == C119_PRODUCT_ID)))
		{
                        sprintf(devstr,"%s/%s", usb_bus->dirname,dev->filename);
			for(i = 0; i < 32; i++)
			{
				sprintf(str,"/proc/asound/card%d/usbbus",i);
				fp = fopen(str,"r");
				if (!fp) continue;
				if ((!fgets(desdev,sizeof(desdev) - 1,fp)) || (!desdev[0]))
				{
					fclose(fp);
					continue;
				}
				fclose(fp);
				if (desdev[strlen(desdev) - 1] == '\n')
			        	desdev[strlen(desdev) -1 ] = 0;
				if (strcasecmp(desdev,devstr)) continue;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)) && !defined(AST_BUILDOPT_LIMEY)
				sprintf(str,"/sys/class/sound/card%d/device",i);
				memset(desdev,0,sizeof(desdev));
				if (readlink(str,desdev,sizeof(desdev) - 1) == -1) continue;
				cp = strrchr(desdev,'/');
				if (!cp) continue;
				cp++;
#else
				memset(desdev,0,sizeof(desdev));
				if (readlink(str,desdev,sizeof(desdev) - 1) == -1)
				{
					sprintf(str,"/sys/class/sound/controlC%d/device",i);
					memset(desdev,0,sizeof(desdev));
					if (readlink(str,desdev,sizeof(desdev) - 1) == -1) continue;
				}
				cp = strrchr(desdev,'/');
				if (cp) *cp = 0; else continue;
				cp = strrchr(desdev,'/');
				if (!cp) continue;
				cp++;

#endif
				break;
			}
			if (i >= 32) continue;
                        if (!strcmp(cp,desired_device)) return dev;
		}

        }
    }
    return NULL;
}

static int hid_device_mklist(void)
{
    struct usb_bus *usb_bus;
    struct usb_device *dev;
    char devstr[8194],str[200],desdev[200],*cp;
    int i;
    FILE *fp;

    ast_mutex_lock(&usb_list_lock);
    if (usb_device_list) ast_free(usb_device_list);
    usb_device_list = ast_malloc(2);
    if (!usb_device_list) 
    {
	ast_mutex_unlock(&usb_list_lock);
	return -1;
    }
    memset(usb_device_list,0,2);

    usb_init();
    usb_find_busses();
    usb_find_devices();
    for (usb_bus = usb_busses;
         usb_bus;
         usb_bus = usb_bus->next) {
        for (dev = usb_bus->devices;
             dev;
             dev = dev->next) {
            if ((dev->descriptor.idVendor
                  == C108_VENDOR_ID) &&
		(((dev->descriptor.idProduct & 0xfffc) == C108_PRODUCT_ID) ||
		(dev->descriptor.idProduct == C108B_PRODUCT_ID) ||
		(dev->descriptor.idProduct == C108AH_PRODUCT_ID) ||
		(dev->descriptor.idProduct == C119A_PRODUCT_ID) ||
		(dev->descriptor.idProduct == C119B_PRODUCT_ID) ||
		((dev->descriptor.idProduct & 0xff00)  == N1KDO_PRODUCT_ID) ||
		(dev->descriptor.idProduct == C119_PRODUCT_ID)))
		{
                        sprintf(devstr,"%s/%s", usb_bus->dirname,dev->filename);
			for(i = 0;i < 32; i++)
			{
				sprintf(str,"/proc/asound/card%d/usbbus",i);
				fp = fopen(str,"r");
				if (!fp) continue;
				if ((!fgets(desdev,sizeof(desdev) - 1,fp)) || (!desdev[0]))
				{
					fclose(fp);
					continue;
				}
				fclose(fp);
				if (desdev[strlen(desdev) - 1] == '\n')
			        	desdev[strlen(desdev) -1 ] = 0;
				if (strcasecmp(desdev,devstr)) continue;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)) && !defined(AST_BUILDOPT_LIMEY)
				sprintf(str,"/sys/class/sound/card%d/device",i);
				memset(desdev,0,sizeof(desdev));
				if (readlink(str,desdev,sizeof(desdev) - 1) == -1) continue;
				cp = strrchr(desdev,'/');
				if (!cp) continue;
				cp++;
#else
				sprintf(str,"/sys/class/sound/card%d/device",i);
				memset(desdev,0,sizeof(desdev));
				if (readlink(str,desdev,sizeof(desdev) - 1) == -1)
				{
					sprintf(str,"/sys/class/sound/controlC%d/device",i);
					memset(desdev,0,sizeof(desdev));
					if (readlink(str,desdev,sizeof(desdev) - 1) == -1) continue;
				}
				cp = strrchr(desdev,'/');
				if (cp) *cp = 0; else continue;
				cp = strrchr(desdev,'/');
				if (!cp) continue;
				cp++;
#endif
				break;
			}
			if (i >= 32) 
			{
				ast_mutex_unlock(&usb_list_lock);
				return -1;
			}
			usb_device_list = ast_realloc(usb_device_list,
				usb_device_list_size + 2 +
					strlen(cp));
			if (!usb_device_list) 
			{
				ast_mutex_unlock(&usb_list_lock);
				return -1;
			}
			usb_device_list_size += strlen(cp) + 2;
			i = 0;
			while(usb_device_list[i])
			{
				i += strlen(usb_device_list + i) + 1;
			}
			strcat(usb_device_list + i,cp);
			usb_device_list[strlen(cp) + i + 1] = 0;
		}

        }
    }
    ast_mutex_unlock(&usb_list_lock);
    return 0;
}

/* returns internal formatted string from external one */
static int usb_get_usbdev(char *devstr)
{
int	i;
char	str[200],desdev[200],*cp;

	for(i = 0;i < 32; i++)
	{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)) && !defined(AST_BUILDOPT_LIMEY)
		sprintf(str,"/sys/class/sound/card%d/device",i);
		memset(desdev,0,sizeof(desdev));
		if (readlink(str,desdev,sizeof(desdev) - 1) == -1) continue;
		cp = strrchr(desdev,'/');
		if (!cp) continue;
		cp++;
#else
		sprintf(str,"/sys/class/sound/card%d/device",i);
		memset(desdev,0,sizeof(desdev));
		if (readlink(str,desdev,sizeof(desdev) - 1) == -1)
		{
			sprintf(str,"/sys/class/sound/controlC%d/device",i);
			memset(desdev,0,sizeof(desdev));
			if (readlink(str,desdev,sizeof(desdev) - 1) == -1) continue;
		}
		cp = strrchr(desdev,'/');
		if (cp) *cp = 0; else continue;
		cp = strrchr(desdev,'/');
		if (!cp) continue;
		cp++;
#endif
		if (!strcasecmp(cp,devstr)) break;
	}
	if (i >= 32) return -1;
	return i;

}

static int usb_list_check(char *devstr)
{

char *s = usb_device_list;

	if (!s) return(0);
	while(*s)
	{
		if (!strcasecmp(s,devstr)) return(1);
		s += strlen(s) + 1;
	}
	return(0);
}


static int	hidhdwconfig(struct chan_simpleusb_pvt *o)
{
int	i;

	if(o->hdwtype==1)	  //sphusb
	{
		o->hid_gpio_ctl		=  0x08;	/* set GPIO4 to output mode */
		o->hid_gpio_ctl_loc	=  2; 	/* For CTL of GPIO */
		o->hid_io_cor		=  4;	/* GPIO3 is COR */
		o->hid_io_cor_loc	=  1;	/* GPIO3 is COR */
		o->hid_io_ctcss		=  2;  	/* GPIO 2 is External CTCSS */
		o->hid_io_ctcss_loc =  1;	/* is GPIO 2 */
		o->hid_io_ptt 		=  8;  	/* GPIO 4 is PTT */
		o->hid_gpio_loc 	=  1;  	/* For ALL GPIO */
		o->valid_gpios		=  1;   /* for GPIO 1 */
	}
	else if(o->hdwtype==0)	//dudeusb
	{
		o->hid_gpio_ctl		=  0x0c;	/* set GPIO 3 & 4 to output mode */
		o->hid_gpio_ctl_loc	=  2; 	/* For CTL of GPIO */
		o->hid_io_cor		=  2;	/* VOLD DN is COR */
		o->hid_io_cor_loc	=  0;	/* VOL DN COR */
		o->hid_io_ctcss		=  1;  	/* VOL UP External CTCSS */
		o->hid_io_ctcss_loc =  0;	/* VOL UP Extenernal CTCSS */
		o->hid_io_ptt 		=  4;  	/* GPIO 3 is PTT */
		o->hid_gpio_loc 	=  1;  	/* For ALL GPIO */
		o->valid_gpios		=  0xfb;   /* for GPIO 1,2,4,5,6,7,8 (5,6,7,8 for CM-119 only) */
	}
	else if(o->hdwtype==2)  //NHRC (N1KDO) (dudeusb w/o user GPIO)
	{
		o->hid_gpio_ctl         =  4;   /* set GPIO 3 to output mode */
		o->hid_gpio_ctl_loc     =  2;   /* For CTL of GPIO */
		o->hid_io_cor           =  2;   /* VOLD DN is COR */
		o->hid_io_cor_loc       =  0;   /* VOL DN COR */
		o->hid_io_ctcss         =  1;   /* VOL UP is External CTCSS */
		o->hid_io_ctcss_loc     =  0;   /* VOL UP CTCSS */
		o->hid_io_ptt           =  4;   /* GPIO 3 is PTT */
		o->hid_gpio_loc         =  1;   /* For ALL GPIO */
		o->valid_gpios          =  0;   /* for GPIO 1,2,4 */
	}
	else if(o->hdwtype==3)	// custom version
	{
		o->hid_gpio_ctl		=  0x0c;	/* set GPIO 3 & 4 to output mode */
		o->hid_gpio_ctl_loc	=  2; 	/* For CTL of GPIO */
		o->hid_io_cor		=  2;	/* VOLD DN is COR */
		o->hid_io_cor_loc	=  0;	/* VOL DN COR */
		o->hid_io_ctcss		=  2;  	/* GPIO 2 is External CTCSS */
		o->hid_io_ctcss_loc =  1;	/* is GPIO 2 */
		o->hid_io_ptt 		=  4;  	/* GPIO 3 is PTT */
		o->hid_gpio_loc 	=  1;  	/* For ALL GPIO */
		o->valid_gpios		=  1;   /* for GPIO 1 */
	}
	o->hid_gpio_val = 0;
	for(i = 0; i < 32; i++)
	{
		/* skip if this one not specified */
		if (!o->gpios[i]) continue;
		/* skip if not out */
		if (strncasecmp(o->gpios[i],"out",3)) continue;
		/* skip if PTT */
		if ((1 << i) & o->hid_io_ptt)
		{
			ast_log(LOG_ERROR,"You can't specify gpio%d, since its the PTT!!!\n",i + 1);
			continue;
		}
		/* skip if not a valid GPIO */
		if (!(o->valid_gpios & (1 << i)))
		{
			ast_log(LOG_ERROR,"You can't specify gpio%d, it is not valid in this configuration\n",i + 1);
			continue;
		}
		o->hid_gpio_ctl |= (1 << i); /* set this one to output, also */
		/* if default value is 1, set it */
		if (!strcasecmp(o->gpios[i],"out1")) o->hid_gpio_val |= (1 << i);
	}
	return 0;
}
/*
*/
static void kickptt(struct chan_simpleusb_pvt *o)
{
	char c = 0;
	if (!o) return;
	if (!o->pttkick) return;
	write(o->pttkick[1],&c,1);
}

/*
 * returns a pointer to the descriptor with the given name
 */
static struct chan_simpleusb_pvt *_find_desc(char *dev)
{
	struct chan_simpleusb_pvt *o = NULL;

	if (!dev)
		ast_log(LOG_WARNING, "null dev\n");

	for (o = simpleusb_default.next; o && o->name && dev && strcmp(o->name, dev) != 0; o = o->next);
	if (!o)
	{
		ast_log(LOG_WARNING, "could not find <%s>\n", dev ? dev : "--no-device--");
		return NULL;
	}

	return o;
}

/*
 * returns a pointer to the descriptor with the given name
 */
static struct chan_simpleusb_pvt *find_desc(char *dev)
{
	struct chan_simpleusb_pvt *o;

	o = _find_desc(dev);
	if (!o) pthread_exit(NULL);
	return o;
}


static struct chan_simpleusb_pvt *find_desc_usb(char *devstr)
{
	struct chan_simpleusb_pvt *o = NULL;

	if (!devstr)
		ast_log(LOG_WARNING, "null dev\n");

	for (o = simpleusb_default.next; o && devstr && strcmp(o->devstr, devstr) != 0; o = o->next);

	return o;
}

static unsigned char ppread(void)
{
unsigned char c;

	c = 0;
	if (haspp == 1) /* if its a pp dev */
	{
		if (ioctl(ppfd, PPRSTATUS, &c) == -1)
		{
			ast_log(LOG_ERROR,"Unable to read pp dev %s\n",pport);
			c = 0;
		}
	}
	if (haspp == 2) /* if its a direct I/O */
	{
		c = inb(pbase + 1);
	}
	return(c);
}

static void ppwrite(unsigned char c)
{
	if (haspp == 1) /* if its a pp dev */
	{
		if (ioctl(ppfd, PPWDATA, &c) == -1)
		{
			ast_log(LOG_ERROR,"Unable to write pp dev %s\n",pport);
		}
	}
	if (haspp == 2) /* if its a direct I/O */
	{
		outb(c,pbase);
	}
	return;
}


static void *pulserthread(void *arg)
{
struct	timeval now,then;
int	i,j,k;

	if (haspp == 2) ioperm(pbase,2,1);
	stoppulser = 0;
	pp_lastmask = 0;
	ast_mutex_lock(&pp_lock);
	ppwrite(pp_val);
	ast_mutex_unlock(&pp_lock);
	then = ast_tvnow();
	while(!stoppulser)
	{
		usleep(50000);
		ast_mutex_lock(&pp_lock);
		now = ast_tvnow();
		j = ast_tvdiff_ms(now,then);
		then = now;
		/* make output inversion mask (for pulseage) */
		pp_lastmask = pp_pulsemask;
		pp_pulsemask = 0;
		for(i = 2; i <= 9; i++)
		{
			k = pp_pulsetimer[i];
			if (k)
			{
				k -= j;
				if (k < 0) k = 0;
				pp_pulsetimer[i] = k;
			}
			if (k) pp_pulsemask |= 1 << (i - 2);
		}
		if (pp_pulsemask != pp_lastmask) /* if anything inverted (temporarily) */
		{
			pp_val ^= pp_lastmask ^ pp_pulsemask;
			ppwrite(pp_val);
		}
		ast_mutex_unlock(&pp_lock);
	}
	pthread_exit(0);
}

/*
*/
static void *hidthread(void *arg)
{
	unsigned char buf[4],bufsave[4],keyed,ctcssed,txreq;
	char fname[200], *s, isn1kdo, lasttxtmp;
	int i,j,k,res;
	struct usb_device *usb_dev;
	struct usb_dev_handle *usb_handle;
	struct chan_simpleusb_pvt *o = (struct chan_simpleusb_pvt *) arg,*ao,**aop;
	struct timeval to,then;
	struct ast_config *cfg1;
	struct ast_variable *v;
	fd_set rfds;

        usb_dev = NULL;
        usb_handle = NULL;
	o->gpio_set = 1;
	if (haspp == 2) ioperm(pbase,2,1);
        while(!o->stophid)
        {
                time(&o->lasthidtime);
		ast_mutex_lock(&usb_dev_lock);
                o->hasusb = 0;
		o->usbass = 0;
                o->devicenum = 0;
                if (usb_handle) usb_close(usb_handle);
                usb_handle = NULL;
                usb_dev = NULL;
                hid_device_mklist();
		isn1kdo = 0;
		for(s = usb_device_list; *s; s += strlen(s) + 1)
		{
			i = usb_get_usbdev(s);
			if (i < 0) continue;
			usb_dev = hid_device_init(s);
			if (usb_dev == NULL) continue;
			if ((usb_dev->descriptor.idProduct & 0xff00) != N1KDO_PRODUCT_ID) continue;
			if (o->index != (usb_dev->descriptor.idProduct & 0xf)) continue;
			ast_log(LOG_NOTICE,"N1KDO port %d, USB device %s simpleusb channel %s\n",
				usb_dev->descriptor.idProduct & 0xf,s,o->name);
			strcpy(o->devstr,s);
			isn1kdo = 1;
			break;
		}
		/* if we are not an N1KDO, and an N1KDO has this devstr, set it to invalid */
		if (!isn1kdo) 
		{
			for(s = usb_device_list; *s; s += strlen(s) + 1)
			{
				i = usb_get_usbdev(s);
				if (i < 0) continue;
				usb_dev = hid_device_init(s);
				if (usb_dev == NULL) continue;
				if ((usb_dev->descriptor.idProduct & 0xff00) != N1KDO_PRODUCT_ID) continue;
				if (!strcmp(s,o->devstr))
				{
					strcpy(o->devstr,"XXX");
					break;
				}
			}
		}
		/* if our specified one exists in the list */
		if ((!usb_list_check(o->devstr)) || (!find_desc_usb(o->devstr)))
		{
			char *s;

			for(s = usb_device_list; *s; s += strlen(s) + 1)
			{
				if (!find_desc_usb(s)) break;
			}
			if (!*s)
			{
				ast_mutex_unlock(&usb_dev_lock);
				usleep(500000);
				continue;
			}
			usb_dev = hid_device_init(s);
			if (usb_dev == NULL) continue;
			if ((usb_dev->descriptor.idProduct & 0xff00) == N1KDO_PRODUCT_ID)
			{
				ast_mutex_unlock(&usb_dev_lock);
				usleep(500000);
				continue;
			}
			i = usb_get_usbdev(s);
			if (i < 0)
			{
				ast_mutex_unlock(&usb_dev_lock);
				usleep(500000);
				continue;
			}
			for (ao = simpleusb_default.next; ao && ao->name ; ao = ao->next)
			{
				if (ao->usbass && (!strcmp(ao->devstr,s))) break;
			}
			if (ao)
			{
				ast_mutex_unlock(&usb_dev_lock);
				usleep(500000);
				continue;
			}
			ast_log(LOG_NOTICE,"Assigned USB device %s to simpleusb channel %s\n",s,o->name);
			strcpy(o->devstr,s);
		}
		for (ao = simpleusb_default.next; ao && ao->name ; ao = ao->next)
		{
			if (ao->usbass && (!strcmp(ao->devstr,s))) break;
		}
		if (ao)
		{
			ast_mutex_unlock(&usb_dev_lock);
			usleep(500000);
			continue;
		}
		i = usb_get_usbdev(o->devstr);
		if (i < 0)
		{
			ast_mutex_unlock(&usb_dev_lock);
			usleep(500000);
			continue;
		}
		o->devicenum = i;
		for (aop = &simpleusb_default.next; *aop && (*aop)->name ; aop = &((*aop)->next))
		{
			if (strcmp((*(aop))->name,o->name)) continue;
			o->next = (*(aop))->next;
			*aop = o;
			break;
		}
		o->usbass = 1;
		ast_mutex_unlock(&usb_dev_lock);
		o->micmax = amixer_max(o->devicenum,MIXER_PARAM_MIC_CAPTURE_VOL);
		o->spkrmax = amixer_max(o->devicenum,MIXER_PARAM_SPKR_PLAYBACK_VOL);
		o->micplaymax = amixer_max(o->devicenum,MIXER_PARAM_MIC_PLAYBACK_VOL);
		if (o->spkrmax == -1) 
		{
			o->newname = 1;
			o->spkrmax = amixer_max(o->devicenum,MIXER_PARAM_SPKR_PLAYBACK_VOL_NEW);
		}

		usb_dev = hid_device_init(o->devstr);
		if (usb_dev == NULL) {
			usleep(500000);
			continue;
		}
		usb_handle = usb_open(usb_dev);
		if (usb_handle == NULL) {
			usleep(500000);
			continue;
		}
		if (usb_claim_interface(usb_handle,C108_HID_INTERFACE) < 0)
		{
			if (usb_detach_kernel_driver_np(usb_handle,C108_HID_INTERFACE) < 0) {
			        ast_log(LOG_ERROR,"Not able to detach the USB device\n");
				usleep(500000);
				continue;
			}
			if (usb_claim_interface(usb_handle,C108_HID_INTERFACE) < 0) {
			        ast_log(LOG_ERROR,"Not able to claim the USB device\n");
				usleep(500000);
				continue;
			}
		}
		memset(buf,0,sizeof(buf));
		buf[2] = o->hid_gpio_ctl;
		buf[1] = 0;
		hid_set_outputs(usb_handle,buf);
		memcpy(bufsave,buf,sizeof(buf));
		if (o->pttkick[0] != -1) close(o->pttkick[0]);
		if (o->pttkick[1] != -1) close(o->pttkick[1]);
		if (pipe(o->pttkick) == -1)
		{
		    ast_log(LOG_ERROR,"Not able to create pipe\n");
			pthread_exit(NULL);
		}
		if ((usb_dev->descriptor.idProduct & 0xfffc) == C108_PRODUCT_ID)
			o->devtype = C108_PRODUCT_ID;
		else
			o->devtype = usb_dev->descriptor.idProduct;
		traceusb1(("hidthread: Starting normally on %s!!\n",o->name));
                if (option_verbose > 1)
                       ast_verbose(VERBOSE_PREFIX_2 "Set device %s to %s\n",o->devstr,o->name);
		mixer_write(o);

		snprintf(fname,sizeof(fname) - 1,config1,o->name);
#ifdef	NEW_ASTERISK
		cfg1 = ast_config_load(fname,zeroflag);
#else
		cfg1 = ast_config_load(fname);
#endif
		o->rxmixerset = 500;
		o->txmixaset = 500;
		o->txmixbset = 500;
		if (cfg1) {
			for (v = ast_variable_browse(cfg1, o->name); v; v = v->next) {
	
				M_START((char *)v->name, (char *)v->value);
				M_UINT("rxmixerset", o->rxmixerset)
				M_UINT("txmixaset", o->txmixaset)
				M_UINT("txmixbset", o->txmixbset)
				M_END(;
				);
			}
			ast_config_destroy(cfg1);
			ast_log(LOG_WARNING,"Loaded parameters from %s for device %s .\n",fname,o->name);
		} else ast_log(LOG_WARNING,"File %s not found, device %s using default parameters.\n",fname,o->name);

		ast_mutex_lock(&o->eepromlock);
		if (o->wanteeprom) o->eepromctl = 1;
		ast_mutex_unlock(&o->eepromlock);
		mixer_write(o);
		setformat(o,O_RDWR);		// KB4FXC 2014-08-24
                o->hasusb = 1;
		while((!o->stophid) && o->hasusb)
		{
			to.tv_sec = 0;
			to.tv_usec = 50000; 

			FD_ZERO(&rfds);
			FD_SET(o->pttkick[0],&rfds);
			then = ast_tvnow();
			/* ast_select emulates linux behaviour in terms of timeout handling */
			res = ast_select(o->pttkick[0] + 1, &rfds, NULL, NULL, &to);
			if (res < 0) {
				ast_log(LOG_WARNING, "select failed: %s\n", strerror(errno));
				usleep(10000);
				continue;
			}
			if (FD_ISSET(o->pttkick[0],&rfds))
			{
				char c;

				read(o->pttkick[0],&c,1);
			}
			if(o->wanteeprom)
			{
				ast_mutex_lock(&o->eepromlock);
				if (o->eepromctl == 1)  /* to read */
				{
					/* if CS okay */
					if (!get_eeprom(usb_handle,o->eeprom))
					{
						if (o->eeprom[EEPROM_MAGIC_ADDR] != EEPROM_MAGIC)
						{
							ast_log(LOG_NOTICE,"UNSUCCESSFUL: EEPROM MAGIC NUMBER BAD on channel %s\n",o->name);
						}
						else
						{
							o->rxmixerset = o->eeprom[EEPROM_RXMIXERSET];
							o->txmixaset = 	o->eeprom[EEPROM_TXMIXASET];
							o->txmixbset = o->eeprom[EEPROM_TXMIXBSET];
							ast_log(LOG_NOTICE,"EEPROM Loaded on channel %s\n",o->name);
							mixer_write(o);
						}
					}
					else
					{
						ast_log(LOG_NOTICE,"USB Adapter has no EEPROM installed or Checksum BAD on channel %s\n",o->name);
					}
					hid_set_outputs(usb_handle,bufsave);
				} 
				if (o->eepromctl == 2) /* to write */
				{
					put_eeprom(usb_handle,o->eeprom);
					hid_set_outputs(usb_handle,bufsave);
					ast_log(LOG_NOTICE,"USB Parameters written to EEPROM on %s\n",o->name);
				}
				o->eepromctl = 0;
				ast_mutex_unlock(&o->eepromlock);
			}
			ast_mutex_lock(&o->usblock);
			buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
			hid_get_inputs(usb_handle,buf);
			keyed = !(buf[o->hid_io_cor_loc] & o->hid_io_cor);
			if (keyed != o->rxhidsq)
			{
				if(o->debuglevel)printf("chan_simpleusb() hidthread: update rxhidsq = %d\n",keyed);
				o->rxhidsq = keyed;
			}
			ctcssed = !(buf[o->hid_io_ctcss_loc] & 
				o->hid_io_ctcss);
			if (ctcssed != o->rxhidctcss)
			{
				if(o->debuglevel)printf("chan_simpleusb() hidthread: update rxhidctcss = %d\n",ctcssed);
				o->rxhidctcss = ctcssed;
			}
			ast_mutex_lock(&o->txqlock);
			txreq = !(AST_LIST_EMPTY(&o->txq));
			ast_mutex_unlock(&o->txqlock);
			txreq = txreq || o->txkeyed || o->txtestkey || o->txclikey || o->echoing;
			if (txreq && (!o->lasttx))
			{
				buf[o->hid_gpio_loc] = o->hid_io_ptt;
				if (o->invertptt) buf[o->hid_gpio_loc] = 0;
				buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
				hid_set_outputs(usb_handle,buf);
				if(o->debuglevel)printf("chan_simpleusb() hidthread: update PTT = %d\n",txreq);
			}
			else if ((!txreq) && o->lasttx)
			{
				buf[o->hid_gpio_loc] = 0;
				if (o->invertptt) buf[o->hid_gpio_loc] = o->hid_io_ptt;
				buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
				hid_set_outputs(usb_handle,buf);
				if(o->debuglevel)printf("chan_simpleusb() hidthread: update PTT = %d\n",txreq);
			}
			lasttxtmp = o->lasttx;
			o->lasttx = txreq;
			time(&o->lasthidtime);
			ast_mutex_unlock(&o->usblock);
			j = buf[o->hid_gpio_loc]; /* get the GPIO info */
			/* if is a CM108AH, map the "HOOK" bit (which used to
			   be GPIO2 in the CM108 into the GPIO position */
				if (o->devtype == C108AH_PRODUCT_ID)
			{
				j |= 2;  /* set GPIO2 bit */
				/* if HOOK is asserted, clear GPIO bit */
				if (buf[o->hid_io_cor_loc] & 0x10) j &= ~2;
			}
			for(i = 0; i < 32; i++)
			{
				/* if a valid input bit, dont clear it */
				if ((o->gpios[i]) && (!strcasecmp(o->gpios[i],"in")) &&
					(o->valid_gpios & (1 << i))) continue;
				j &= ~(1 << i); /* clear the bit, since its not an input */
			}
			if ((!o->had_gpios_in) || (o->last_gpios_in != j))
			{
				char buf1[100];
				struct ast_frame fr;

				for(i = 0; i < 32; i++)
				{
					/* skip if not specified */
					if (!o->gpios[i]) continue;
					/* skip if not input */
					if (strcasecmp(o->gpios[i],"in")) continue;
					/* skip if not a valid GPIO */
					if (!(o->valid_gpios & (1 << i))) continue;
					/* if bit has changed, or never reported */
					if ((!o->had_gpios_in) || ((o->last_gpios_in & (1 << i)) != (j & (1 << i))))
					{
						sprintf(buf1,"GPIO%d %d\n",i + 1,(j & (1 << i)) ? 1 : 0);
						memset(&fr,0,sizeof(fr));
						fr.data =  buf1;
						fr.datalen = strlen(buf1);
						fr.samples = 0;
						fr.frametype = AST_FRAME_TEXT;
						fr.subclass = 0;
						fr.src = "chan_simpleusb";
						fr.offset = 0;
						fr.mallocd=0;
						fr.delivery.tv_sec = 0;
						fr.delivery.tv_usec = 0;
						ast_queue_frame(o->owner,&fr);
					}
				}
				o->had_gpios_in = 1;
				o->last_gpios_in = j;
			}
			ast_mutex_lock(&pp_lock);
			j = k = ppread() ^ 0x80; /* get PP input */
			ast_mutex_unlock(&pp_lock);
			for(i = 10; i <= 15; i++)
			{
				/* if a valid input bit, dont clear it */
				if ((o->pps[i]) && (!strcasecmp(o->pps[i],"in")) &&
					(PP_MASK & (1 << i))) continue;
				j &= ~(1 << ppinshift[i]); /* clear the bit, since its not an input */
			}
			if ((!o->had_pp_in) || (o->last_pp_in != j))
			{
				char buf1[100];
				struct ast_frame fr;

				for(i = 10; i <= 15; i++)
				{
					/* skip if not specified */
					if (!o->pps[i]) continue;
					/* skip if not input */
					if (strcasecmp(o->pps[i],"in")) continue;
					/* skip if not valid */
					if (!(PP_MASK & (1 << i))) continue;
					/* if bit has changed, or never reported */
					if ((!o->had_pp_in) || ((o->last_pp_in & 
						(1 << ppinshift[i])) != (j & (1 << ppinshift[i]))))
					{
						sprintf(buf1,"PP%d %d\n",i,(j & (1 << ppinshift[i])) ? 1 : 0);
						memset(&fr,0,sizeof(fr));
						fr.data =  buf1;
						fr.datalen = strlen(buf1);
						fr.samples = 0;
						fr.frametype = AST_FRAME_TEXT;
						fr.subclass = 0;
						fr.src = "chan_simpleusb";
						fr.offset = 0;
						fr.mallocd=0;
						fr.delivery.tv_sec = 0;
						fr.delivery.tv_usec = 0;
						ast_queue_frame(o->owner,&fr);
					}
				}
				o->had_pp_in = 1;
				o->last_pp_in = j;
			}
			o->rxppsq = o->rxppctcss = 0;
			for(i = 10; i <= 15; i++)
			{
				if ((o->pps[i]) && (!strcasecmp(o->pps[i],"cor")) &&
					(PP_MASK & (1 << i)))
				{
					j = k & (1 << ppinshift[i]); /* set the bit accordingly */
					if (j != o->rxppsq)
					{
						if(o->debuglevel)printf("chan_simpleusb() hidthread: update rxppsq = %d\n",j);
						o->rxppsq = j;
					}
				}
				else if ((o->pps[i]) && (!strcasecmp(o->pps[i],"ctcss")) &&
					(PP_MASK & (1 << i)))
				{
					o->rxppctcss = k & (1 << ppinshift[i]); /* set the bit accordingly */
				}

			}
			j = ast_tvdiff_ms(ast_tvnow(),then);
			/* make output inversion mask (for pulseage) */
			o->hid_gpio_lastmask = o->hid_gpio_pulsemask;
			o->hid_gpio_pulsemask = 0;
			for(i = 0; i < 32; i++)
			{
				k = o->hid_gpio_pulsetimer[i];
				if (k)
				{
					k -= j;
					if (k < 0) k = 0;
					o->hid_gpio_pulsetimer[i] = k;
				}
				if (k) o->hid_gpio_pulsemask |= 1 << i;
			}
			if (o->hid_gpio_pulsemask || o->hid_gpio_lastmask) /* if anything inverted (temporarily) */
			{
				buf[o->hid_gpio_loc] = o->hid_gpio_val ^ o->hid_gpio_pulsemask;
				buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
				hid_set_outputs(usb_handle,buf);
			}
			if (o->gpio_set)
			{
				o->gpio_set = 0;
				buf[o->hid_gpio_loc] = o->hid_gpio_val ^ o->hid_gpio_pulsemask;
				buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
				hid_set_outputs(usb_handle,buf);
			}
			k = 0;
			for(i = 2; i <= 9; i++)
			{
				/* skip if this one not specified */
				if (!o->pps[i]) continue;
				/* skip if not ptt */
				if (strncasecmp(o->pps[i],"ptt",3)) continue;
				k |= (1 << (i - 2)); /* make mask */
			}
			if (lasttxtmp != o->lasttx)
			{
				if(o->debuglevel) printf("hidthread: tx set to %d\n",o->lasttx);
				o->hid_gpio_val &= ~o->hid_io_ptt;
				ast_mutex_lock(&pp_lock);
				if (k) pp_val &= ~k;
				if (!o->invertptt)
				{
					if (o->lasttx) 
					{
						buf[o->hid_gpio_loc] = o->hid_gpio_val |= o->hid_io_ptt;
						if (k) pp_val |= k;
					}
				}
				else
				{
					if (!o->lasttx)
					{
						buf[o->hid_gpio_loc] = o->hid_gpio_val |= o->hid_io_ptt;
						if (k) pp_val |= k;
					}
				}
				if (k) ppwrite(pp_val);
				ast_mutex_unlock(&pp_lock);
				buf[o->hid_gpio_loc] = o->hid_gpio_val ^ o->hid_gpio_pulsemask;
				buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
				memcpy(bufsave,buf,sizeof(buf));
				hid_set_outputs(usb_handle,buf);
			}
			ast_mutex_unlock(&o->usblock);
		}
		o->lasttx = 0;
		buf[o->hid_gpio_loc] = 0;
		if (o->invertptt) buf[o->hid_gpio_loc] = o->hid_io_ptt;
		buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
		hid_set_outputs(usb_handle,buf);
	}
	o->lasttx = 0;
        if (usb_handle)
        {
                buf[o->hid_gpio_loc] = 0;
                if (o->invertptt) buf[o->hid_gpio_loc] = o->hid_io_ptt;
                buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
                hid_set_outputs(usb_handle,buf);
        }
        pthread_exit(0);
}

/*
 * Returns the number of blocks used in the audio output channel
 */
static int used_blocks(struct chan_simpleusb_pvt *o)
{
	struct audio_buf_info info;

	if (ioctl(o->sounddev, SNDCTL_DSP_GETOSPACE, &info)) {
		if (!(o->warned & WARN_used_blocks)) {
			ast_log(LOG_WARNING, "Error reading output space\n");
			o->warned |= WARN_used_blocks;
		}
		return 1;
	}

	if (o->total_blocks == 0) {
		if (0)					/* debugging */
			ast_log(LOG_WARNING, "fragtotal %d size %d avail %d\n", info.fragstotal, info.fragsize, info.fragments);
		o->total_blocks = info.fragments;
	}

	return o->total_blocks - info.fragments;
}

/* Write an exactly FRAME_SIZE sized frame */
static int soundcard_writeframe(struct chan_simpleusb_pvt *o, short *data)
{
	int res;


	if (o->sounddev < 0)
		setformat(o, O_RDWR);
	if (o->sounddev < 0)
		return 0;				/* not fatal */
	/*
	 * Nothing complex to manage the audio device queue.
	 * If the buffer is full just drop the extra, otherwise write.
	 * XXX in some cases it might be useful to write anyways after
	 * a number of failures, to restart the output chain.
	 */
	res = used_blocks(o);
	if (res > o->queuesize) {	/* no room to write a block */
		ast_log(LOG_WARNING, "sound device write buffer overflow\n");
		if (o->w_errors++ == 0 && (simpleusb_debug & 0x4))
			ast_log(LOG_WARNING, "write: used %d blocks (%d)\n", res, o->w_errors);
		return 0;
	}
	o->w_errors = 0;

	return write(o->sounddev, ((void *) data), FRAME_SIZE * 2 * 2 * 6);
}

#ifndef	NEW_ASTERISK

/*
 * Handler for 'sound writable' events from the sound thread.
 * Builds a frame from the high level description of the sounds,
 * and passes it to the audio device.
 * The actual sound is made of 1 or more sequences of sound samples
 * (s->datalen, repeated to make s->samplen samples) followed by
 * s->silencelen samples of silence. The position in the sequence is stored
 * in o->sampsent, which goes between 0 .. s->samplen+s->silencelen.
 * In case we fail to write a frame, don't update o->sampsent.
 */
static void send_sound(struct chan_simpleusb_pvt *o)
{
	short myframe[FRAME_SIZE];
	int ofs, l, start;
	int l_sampsent = o->sampsent;
	struct sound *s;

	if (o->cursound < 0)		/* no sound to send */
		return;

	s = &sounds[o->cursound];

	for (ofs = 0; ofs < FRAME_SIZE; ofs += l) {
		l = s->samplen - l_sampsent;	/* # of available samples */
		if (l > 0) {
			start = l_sampsent % s->datalen;	/* source offset */
			if (l > FRAME_SIZE - ofs)	/* don't overflow the frame */
				l = FRAME_SIZE - ofs;
			if (l > s->datalen - start)	/* don't overflow the source */
				l = s->datalen - start;
			bcopy(s->data + start, myframe + ofs, l * 2);
			if (0)
				ast_log(LOG_WARNING, "send_sound sound %d/%d of %d into %d\n", l_sampsent, l, s->samplen, ofs);
			l_sampsent += l;
		} else {				/* end of samples, maybe some silence */
			static const short silence[FRAME_SIZE] = { 0, };

			l += s->silencelen;
			if (l > 0) {
				if (l > FRAME_SIZE - ofs)
					l = FRAME_SIZE - ofs;
				bcopy(silence, myframe + ofs, l * 2);
				l_sampsent += l;
			} else {			/* silence is over, restart sound if loop */
				if (s->repeat == 0) {	/* last block */
					o->cursound = -1;
					o->nosound = 0;	/* allow audio data */
					if (ofs < FRAME_SIZE)	/* pad with silence */
						bcopy(silence, myframe + ofs, (FRAME_SIZE - ofs) * 2);
				}
				l_sampsent = 0;
			}
		}
	}
	l = soundcard_writeframe(o, myframe);
	if (l > 0)
		o->sampsent = l_sampsent;	/* update status */
}

static void *sound_thread(void *arg)
{
	char ign[4096];
	struct chan_simpleusb_pvt *o = (struct chan_simpleusb_pvt *) arg;

	/*
	 * Just in case, kick the driver by trying to read from it.
	 * Ignore errors - this read is almost guaranteed to fail.
	 */
	read(o->sounddev, ign, sizeof(ign));
	for (;;) {
		fd_set rfds, wfds;
		int maxfd, res;

		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(o->sndcmd[0], &rfds);
		maxfd = o->sndcmd[0];	/* pipe from the main process */
		if (o->cursound > -1 && o->sounddev < 0)
			setformat(o, O_RDWR);	/* need the channel, try to reopen */
		else if (o->cursound == -1 && o->owner == NULL)
		{
			setformat(o, O_CLOSE);	/* can close */
		}
		if (o->sounddev > -1) {
			if (!o->owner) {	/* no one owns the audio, so we must drain it */
				FD_SET(o->sounddev, &rfds);
				maxfd = MAX(o->sounddev, maxfd);
			}
			if (o->cursound > -1) {
				FD_SET(o->sounddev, &wfds);
				maxfd = MAX(o->sounddev, maxfd);
			}
		}
		/* ast_select emulates linux behaviour in terms of timeout handling */
		res = ast_select(maxfd + 1, &rfds, &wfds, NULL, NULL);
		if (res < 1) {
			ast_log(LOG_WARNING, "select failed: %s\n", strerror(errno));
			sleep(1);
			continue;
		}
		if (FD_ISSET(o->sndcmd[0], &rfds)) {
			/* read which sound to play from the pipe */
			int i, what = -1;

			read(o->sndcmd[0], &what, sizeof(what));
			for (i = 0; sounds[i].ind != -1; i++) {
				if (sounds[i].ind == what) {
					o->cursound = i;
					o->sampsent = 0;
					o->nosound = 1;	/* block audio from pbx */
					break;
				}
			}
			if (sounds[i].ind == -1)
				ast_log(LOG_WARNING, "invalid sound index: %d\n", what);
		}
		if (o->sounddev > -1) {
			if (FD_ISSET(o->sounddev, &rfds))	/* read and ignore errors */
				read(o->sounddev, ign, sizeof(ign)); 
			if (FD_ISSET(o->sounddev, &wfds))
				send_sound(o);
		}
	}
	return NULL;				/* Never reached */
}

#endif

/*
 * reset and close the device if opened,
 * then open and initialize it in the desired mode,
 * trigger reads and writes so we can start using it.
 */
static int setformat(struct chan_simpleusb_pvt *o, int mode)
{
	int fmt, desired, res, fd;
	char device[100];

	if (o->sounddev >= 0) {
		ioctl(o->sounddev, SNDCTL_DSP_RESET, 0);
		close(o->sounddev);
		o->duplex = M_UNSET;
		o->sounddev = -1;
	}
	if (mode == O_CLOSE)		/* we are done */
		return 0;
	o->lastopen = ast_tvnow();
	strcpy(device,"/dev/dsp");
	if (o->devicenum)
		sprintf(device,"/dev/dsp%d",o->devicenum);
	fd = o->sounddev = open(device, mode | O_NONBLOCK);
	if (fd < 0) {
		ast_log(LOG_WARNING, "Unable to re-open DSP device %d (%s): %s\n", o->devicenum, o->name, strerror(errno));
		return -1;
	}
	if (o->owner)
		o->owner->fds[0] = fd;

#if __BYTE_ORDER == __LITTLE_ENDIAN
	fmt = AFMT_S16_LE;
#else
	fmt = AFMT_S16_BE;
#endif
	res = ioctl(fd, SNDCTL_DSP_SETFMT, &fmt);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set format to 16-bit signed\n");
		return -1;
	}
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
		ast_log(LOG_WARNING, "Failed to set audio device to stereo\n");
		return -1;
	}
	fmt = desired = 48000;			/* 8000 Hz desired */
	res = ioctl(fd, SNDCTL_DSP_SPEED, &fmt);

	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to set audio device to mono\n");
		return -1;
	}
	if (fmt != desired) {
		if (!(o->warned & WARN_speed)) {
			ast_log(LOG_WARNING,
			    "Requested %d Hz, got %d Hz -- sound may be choppy\n",
			    desired, fmt);
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
				ast_log(LOG_WARNING,
					"Unable to set fragment size -- sound may be choppy\n");
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

/*
 * some of the standard methods supported by channels.
 */
static int simpleusb_digit_begin(struct ast_channel *c, char digit)
{
	return 0;
}

static int simpleusb_digit_end(struct ast_channel *c, char digit, unsigned int duration)
{
	/* no better use for received digits than print them */
	ast_verbose(" << Console Received digit %c of duration %u ms >> \n", 
		digit, duration);
	return 0;
}

static void mkpsamples(short *audio,uint32_t x, int *audio_ptr, int *divcnt, int divdiv)
{
int	i;

	for(i = 31; i >= 0; i--)
	{
		while(*divcnt < divdiv)
		{
			audio[(*audio_ptr)++] = (x & (1 << i)) ? ONEVAL : ZEROVAL;
			*divcnt += DIVSAMP;
		}
		if (*divcnt >= divdiv) *divcnt -= divdiv;
	}
}


static int simpleusb_text(struct ast_channel *c, const char *text)
{
	struct chan_simpleusb_pvt *o = c->tech_pvt;
	char *cmd;
	int cnt,i,j,audio_samples,divcnt,divdiv,audio_ptr,baud;
	struct pocsag_batch *batch,*b;
	short *audio;
	char audio1[AST_FRIENDLY_OFFSET + (FRAME_SIZE * sizeof(short))];
	struct ast_frame wf,*f1;

	if (haspp == 2) ioperm(pbase,2,1);

	cmd = alloca(strlen(text) + 10);

	/* print received messages */
	if(o->debuglevel) ast_verbose(" << Console Received simpleusb text %s >> \n", text);

	if (!strncmp(text,"RXCTCSS",7))
	{
		cnt = sscanf(text,"%s %d",cmd,&i);
		if (cnt < 2) return 0;
		o->rxctcssoverride = !i;
	        if(o->debuglevel)ast_log(LOG_NOTICE,"parse simpleusb RXCTCSS cmd: %s\n",text);
		return 0;		
	}

	if (!strncmp(text,"GPIO",4))
	{
		cnt = sscanf(text,"%s %d %d",cmd,&i,&j);
		if (cnt < 3) return 0;
		if ((i < 1) || (i > 32)) return 0;
		i--;
		/* skip if not valid */
		if (!(o->valid_gpios & (1 << i))) return 0;
		ast_mutex_lock(&o->usblock);
		if (j > 1) /* if to request pulse-age */
		{
			o->hid_gpio_pulsetimer[i] = j - 1;
		}
		else
		{
			/* clear pulsetimer, if in the middle of running */
			o->hid_gpio_pulsetimer[i] = 0;
			o->hid_gpio_val &= ~(1 << i);
			if (j) o->hid_gpio_val |= 1 << i;
			o->gpio_set = 1;
		}
		ast_mutex_unlock(&o->usblock);
		kickptt(o);
		return 0;
	}
	if (!strncmp(text,"PP",2))
	{
		cnt = sscanf(text,"%s %d %d",cmd,&i,&j);
		if (cnt < 3) return 0;
		if ((i < 2) || (i > 9)) return 0;
		/* skip if not valid */
		if (!(PP_MASK & (1 << i))) return 0;
		ast_mutex_lock(&pp_lock);
		if (j > 1) /* if to request pulse-age */
		{
			pp_pulsetimer[i] = j - 1;
		}
		else
		{
			/* clear pulsetimer, if in the middle of running */
			pp_pulsetimer[i] = 0;
			pp_val &= ~(1 << (i - 2));
			if (j) pp_val |= 1 << (i - 2);
			ppwrite(pp_val);
		}
		ast_mutex_unlock(&pp_lock);
		return 0;
	}

	if (!strncmp(text,"PAGE",4))
	{
		cnt = sscanf(text,"%s %d %d %n",cmd,&baud,&i,&j);
		if (cnt < 3) return 0;
		if (strlen(text + j) < 1) return 0;
		switch(text[j])
		{
		    case 'T': /* Tone only */
			if (option_verbose > 2) 
				ast_verbose(VERBOSE_PREFIX_3 "POCSAG page (%d baud, capcode=%d) TONE ONLY\n",baud,i);
			batch = make_pocsag_batch(i, NULL, 0, TONE, 0);
			break;
		    case 'N': /* Numeric */
			if (!text[j + 1]) return 0;
			if (option_verbose > 2) 
				ast_verbose(VERBOSE_PREFIX_3 "POCSAG page (%d baud, capcode=%d) NUMERIC (%s)\n",baud,i,text + j + 1);
			batch = make_pocsag_batch(i, (char *)text + j + 1, strlen(text + j + 1), NUMERIC, 0);
			break;
		    case 'A': /* Alpha */
			if (!text[j + 1]) return 0;
			if (option_verbose > 2) 
				ast_verbose(VERBOSE_PREFIX_3 "POCSAG page (%d baud, capcode=%d) ALPHA (%s)\n",baud,i,text + j + 1);
			batch = make_pocsag_batch(i, (char *)text + j + 1, strlen(text + j + 1), ALPHA, 0);
			break;
		    case '?': /* Query Page Status */
			i = 0;
			ast_mutex_lock(&o->txqlock);
			AST_LIST_TRAVERSE(&o->txq, f1,frame_list) if (f1->src && (!strcmp(f1->src,PAGER_SRC))) i++;
			ast_mutex_unlock(&o->txqlock);
			cmd = (i) ? "PAGES" : "NOPAGES" ;
			memset(&wf,0,sizeof(wf));
			wf.frametype = AST_FRAME_TEXT;
		        wf.datalen = strlen(cmd);
		        wf.data = cmd;
			ast_queue_frame(o->owner, &wf);
			return 0;
		    default:
			return 0;
		}
		if (!batch)
		{
			ast_log(LOG_ERROR,"Error creating POCSAG page!!\n");
			return 0;
		}
		b = batch;
		for(i = 0; b; b = b->next) i++;
		/* get number of samples to alloc for audio */
		audio_samples = (SAMPRATE * (PREAMBLE_BITS + (MESSAGE_BITS * i))) / baud;
		/* pad end with 250ms of silence */
		audio_samples += SAMPRATE / 4;
		/* also pad up to FRAME_SIZE */
		audio_samples += audio_samples % FRAME_SIZE;
		audio = malloc((audio_samples * sizeof(short)) + 10);
		if (!audio)
		{
			free_batch(batch);
			ast_log(LOG_ERROR,"Cant malloc() for audio buffer!!\n");
			return 0;
		}
		memset(audio,0,audio_samples * sizeof(short));
		divdiv = DIVLCM / baud;
		divcnt = 0;
		audio_ptr = 0;
		for(i = 0; i < (PREAMBLE_BITS / 32); i++)
			mkpsamples(audio,0xaaaaaaaa,&audio_ptr,&divcnt,divdiv);
		b = batch;
		while (b)
		{
			mkpsamples(audio,b->sc,&audio_ptr,&divcnt,divdiv);
			for(j = 0; j < 8; j++)
			{
				for(i = 0; i < 2; i++)
				{
					mkpsamples(audio,b->frame[j][i],&audio_ptr,&divcnt,divdiv);
				}
			}
			b = b->next;
		}
		free_batch(batch);
		memset(audio1,0,sizeof(audio1));
		for(i = 0; i < audio_samples; i += FRAME_SIZE)
		{
			memset(&wf,0,sizeof(wf));
			wf.frametype = AST_FRAME_VOICE;
		        wf.subclass = AST_FORMAT_SLINEAR;
		        wf.samples = FRAME_SIZE;
		        wf.datalen = FRAME_SIZE * 2;
			wf.offset = AST_FRIENDLY_OFFSET;
		        wf.data = audio1 + AST_FRIENDLY_OFFSET;
			wf.src = PAGER_SRC;
			memcpy(wf.data,(char *)(audio + i),FRAME_SIZE * 2);
			f1 = ast_frdup(&wf);
			memset(&f1->frame_list,0,sizeof(f1->frame_list));
			ast_mutex_lock(&o->txqlock);
			AST_LIST_INSERT_TAIL(&o->txq,f1,frame_list);
			ast_mutex_unlock(&o->txqlock);
		}
		free(audio);
		return 0;
	}
	return 0;
}

/* Play ringtone 'x' on device 'o' */
static void ring(struct chan_simpleusb_pvt *o, int x)
{
#ifndef	NEW_ASTERISK
	write(o->sndcmd[1], &x, sizeof(x));
#endif
}

/*
 * handler for incoming calls. Either autoanswer, or start ringing
 */
static int simpleusb_call(struct ast_channel *c, char *dest, int timeout)
{
	struct chan_simpleusb_pvt *o = c->tech_pvt;

	o->stophid = 0;
	time(&o->lasthidtime);
	ast_pthread_create_background(&o->hidthread, NULL, hidthread, o);
	ast_setstate(c, AST_STATE_UP);
	return 0;
}

/*
 * remote side answered the phone
 */
static int simpleusb_answer(struct ast_channel *c)
{
#ifndef	NEW_ASTERISK
	struct chan_simpleusb_pvt *o = c->tech_pvt;
#endif

	ast_setstate(c, AST_STATE_UP);
#ifndef	NEW_ASTERISK
	o->cursound = -1;
	o->nosound = 0;
#endif
	return 0;
}

static int simpleusb_hangup(struct ast_channel *c)
{
	struct chan_simpleusb_pvt *o = c->tech_pvt;

	//ast_log(LOG_NOTICE, "simpleusb_hangup()\n");
#ifndef	NEW_ASTERISK
	o->cursound = -1;
	o->nosound = 0;
#endif
	c->tech_pvt = NULL;
	o->owner = NULL;
	ast_module_unref(ast_module_info->self);
	if (o->hookstate) {
		if (o->autoanswer || o->autohangup) {
			/* Assume auto-hangup too */
			o->hookstate = 0;
			setformat(o, O_CLOSE);
		} else {
			/* Make congestion noise */
			ring(o, AST_CONTROL_CONGESTION);
		}
	}
	o->stophid = 1;
	pthread_join(o->hidthread,NULL);
	return 0;
}


/* used for data coming from the network */
static int simpleusb_write(struct ast_channel *c, struct ast_frame *f)
{
	struct chan_simpleusb_pvt *o = c->tech_pvt;
	struct ast_frame *f1;

	traceusb2(("simpleusb_write() o->nosound= %i\n",o->nosound));

#ifndef	NEW_ASTERISK
	/* Immediately return if no sound is enabled */
	if (o->nosound)
		return 0;
	/* Stop any currently playing sound */
	o->cursound = -1;
#endif
	if (!o->hasusb) return 0;
	if (o->sounddev < 0)
		setformat(o, O_RDWR);
	if (o->sounddev < 0)
		return 0;				/* not fatal */
	/*
	 * we could receive a block which is not a multiple of our
	 * FRAME_SIZE, so buffer it locally and write to the device
	 * in FRAME_SIZE chunks.
	 * Keep the residue stored for future use.
	 */

	#if DEBUG_CAPTURES == 1	// to write input data to a file   datalen=320
	if (ftxcapraw && o->b.txcapraw)
	{
		short i, tbuff[f->datalen];
		for(i=0;i<f->datalen;i+=2)
		{
			tbuff[i]= ((short*)(f->data))[i/2];
			tbuff[i+1]= o->txkeyed*0x1000;
		}
		fwrite(tbuff,2,f->datalen,ftxcapraw);
		//fwrite(f->data,1,f->datalen,ftxcapraw);
	}
	#endif

	if ((!o->txkeyed) && (!o->txtestkey)) return 0;

	if ((!o->txtestkey) && o->echoing) return 0;

	f1 = ast_frdup(f);
	memset(&f1->frame_list,0,sizeof(f1->frame_list));
	ast_mutex_lock(&o->txqlock);
	AST_LIST_INSERT_TAIL(&o->txq,f1,frame_list);
	ast_mutex_unlock(&o->txqlock);

	return 0;
}



static struct ast_frame *simpleusb_read(struct ast_channel *c)
{
	int res,cd,sd,src,i,n,ispager,doleft,doright; // Yes, like Dudley!!! :-)
	struct chan_simpleusb_pvt *o = c->tech_pvt;
	struct ast_frame *f = &o->read_f,*f1;
	struct ast_frame wf = { AST_FRAME_CONTROL }, wf1;
	time_t now;
	short *sp,*sp1,outbuf[FRAME_SIZE * 2 * 6];

	traceusb2(("simpleusb_read()\n"));

	if (o->lasthidtime)
	{
		time(&now);
		if ((now - o->lasthidtime) > 3)
		{
			ast_log(LOG_ERROR,"HID process has died or something!!\n");
			return NULL;
		}
	}
	/* XXX can be simplified returning &ast_null_frame */
	/* prepare a NULL frame in case we don't have enough data to return */
	bzero(f, sizeof(struct ast_frame));
	f->frametype = AST_FRAME_NULL;
	f->src = simpleusb_tech.type;

        /* if USB device not ready, just return NULL frame */
        if (!o->hasusb)
        {
		if (o->rxkeyed)
		{
			o->lastrx = 0;
			o->rxkeyed = 0;
			wf.subclass = AST_CONTROL_RADIO_UNKEY;
			ast_queue_frame(o->owner, &wf);
		}
                return f;
        }

	if (!o->echomode)
	{
		struct qelem *q;

		ast_mutex_lock(&o->echolock);
		o->echoing = 0;
		while(o->echoq.q_forw != &o->echoq)
		{
			q = o->echoq.q_forw;
			remque(q);
			ast_free(q);
		}
		ast_mutex_unlock(&o->echolock);
	}

	if (o->echomode && (!o->rxkeyed))
	{
		struct usbecho *u;

		ast_mutex_lock(&o->echolock);
		/* if there is something in the queue */
		if (o->echoq.q_forw != &o->echoq)
		{
			u = (struct usbecho *) o->echoq.q_forw;
			remque((struct qelem *)u);
		        f->frametype = AST_FRAME_VOICE;
		        f->subclass = AST_FORMAT_SLINEAR;
		        f->samples = FRAME_SIZE;
		        f->datalen = FRAME_SIZE * 2;
			f->offset = AST_FRIENDLY_OFFSET;
		        f->data = o->simpleusb_read_frame_buf + AST_FRIENDLY_OFFSET;
			memcpy(f->data,u->data,FRAME_SIZE * 2);
			ast_free(u);
			f1 = ast_frdup(f);
			memset(&f1->frame_list,0,sizeof(f1->frame_list));
			ast_mutex_lock(&o->txqlock);
			AST_LIST_INSERT_TAIL(&o->txq,f1,frame_list);
			ast_mutex_unlock(&o->txqlock);
			o->echoing = 1;
		} else o->echoing = 0;
		ast_mutex_unlock(&o->echolock);
	}

	res = read(o->sounddev, o->simpleusb_read_buf + o->readpos, 
		sizeof(o->simpleusb_read_buf) - o->readpos);
	if (res < 0)				/* audio data not ready, return a NULL frame */
	{
                if (errno != EAGAIN)
                {
                        o->readerrs = 0;
                        o->hasusb = 0;
                        return f;
                }
                if (o->readerrs++ > READERR_THRESHOLD)
                {
                        ast_log(LOG_ERROR,"Stuck USB read channel [%s], un-sticking it!\n",o->name);
                        o->readerrs = 0;
                        o->hasusb = 0;
                        return f;
                }
                if (o->readerrs == 1)
                        ast_log(LOG_WARNING,"Possibly stuck USB read channel. [%s]\n",o->name);
                return f;
        }
	#if DEBUG_CAPTURES == 1
	if (o->b.rxcapraw && frxcapraw) 
		fwrite(o->simpleusb_read_buf + o->readpos,1,res,frxcapraw);
	#endif

        if (o->readerrs) ast_log(LOG_WARNING,"Nope, USB read channel [%s] wasn't stuck after all.\n",o->name);
	o->readerrs = 0;
	o->readpos += res;
	if (o->readpos < sizeof(o->simpleusb_read_buf))	/* not enough samples */
		return f;

	if (o->mute)
		return f;

	for(;;)
	{
		n = 0;
		ast_mutex_lock(&o->txqlock);
		AST_LIST_TRAVERSE(&o->txq, f1,frame_list) n++;
		ast_mutex_unlock(&o->txqlock);
		i = used_blocks(o);
		if (n && ((n > 3) || ((!o->txkeyed) && (!o->txtestkey))) && 
			(i <= o->queuesize))
		{
			ast_mutex_lock(&o->txqlock);
			f1 = AST_LIST_REMOVE_HEAD(&o->txq,frame_list);
			ast_mutex_unlock(&o->txqlock);

		        src = 0;                                        /* read position into f1->data */
		        while (src < f1->datalen) {
		                /* Compute spare room in the buffer */
		                int l = sizeof(o->simpleusb_write_buf) - o->simpleusb_write_dst;

		                if (f1->datalen - src >= l) {       /* enough to fill a frame */
		                        memcpy(o->simpleusb_write_buf + o->simpleusb_write_dst, (char *)f1->data + src, l);
					if (o->devtype != C108_PRODUCT_ID)
					{
						int v;
			
						sp = (short *) o->simpleusb_write_buf;
						for(i = 0; i < FRAME_SIZE; i++)
						{
							v = *sp;
							v += v >> 3;  /* add *.125 giving * 1.125 */
							v -= *sp >> 5; /* subtract *.03125 giving * 1.09375 */
							if (v > 32765.0) v = 32765.0;
							if (v < -32765.0) v = -32765.0;
							*sp++ = v;
						}
					}
					sp = (short *) o->simpleusb_write_buf;
					sp1 = outbuf;
					doright = 1;
					doleft = 1;
					ispager = 0;
					if (f1->src && (!strcmp(f1->src,PAGER_SRC))) ispager = 1;
					if (o->pager != PAGER_NONE)
					{
						doleft = (o->pager == PAGER_A) ? ispager : !ispager;
						doright = (o->pager == PAGER_B) ? ispager : !ispager;
					}
					for(i = 0; i < FRAME_SIZE; i++)
					{
						short s,v;

						if (o->preemphasis)
							s = preemph(sp[i],&o->prestate);
						else
							s = sp[i];
						v = lpass(s,o->flpt);
						*sp1++ = (doleft) ? v : 0;
						*sp1++ = (doright) ? v : 0;
						v = lpass(s,o->flpt);
						*sp1++ = (doleft) ? v : 0;
						*sp1++ = (doright) ? v : 0;
						v = lpass(s,o->flpt);
						*sp1++ = (doleft) ? v : 0;
						*sp1++ = (doright) ? v : 0;
						v = lpass(s,o->flpt);
						*sp1++ = (doleft) ? v : 0;
						*sp1++ = (doright) ? v : 0;
						v = lpass(s,o->flpt);
						*sp1++ = (doleft) ? v : 0;
						*sp1++ = (doright) ? v : 0;
						v = lpass(s,o->flpt);
						*sp1++ = (doleft) ? v : 0;
						*sp1++ = (doright) ? v : 0;
					}				
		                        soundcard_writeframe(o, outbuf);
		                        src += l;
		                        o->simpleusb_write_dst = 0;
					if (o->waspager && (!ispager))
					{
						memset(&wf1,0,sizeof(wf1));
						wf1.frametype = AST_FRAME_TEXT;
					        wf1.datalen = strlen(ENDPAGE_STR) + 1;
					        wf1.data = ENDPAGE_STR;
						ast_queue_frame(o->owner, &wf1);
					}
					o->waspager = ispager;
		                } else {                                /* copy residue */
		                        l = f1->datalen - src;
		                        memcpy(o->simpleusb_write_buf + o->simpleusb_write_dst,
						 (char *)f1->data + src, l);
		                        src += l;                       /* but really, we are done */
		                        o->simpleusb_write_dst += l;
		                }
		        }
			ast_frfree(f1);
			continue;
		}
		break;
	}

	if (o->waspager)
	{
		n = 0;
		ast_mutex_lock(&o->txqlock);
		AST_LIST_TRAVERSE(&o->txq, f1,frame_list) n++;
		ast_mutex_unlock(&o->txqlock);
		if (n < 1)
		{
			memset(&wf1,0,sizeof(wf1));
			wf1.frametype = AST_FRAME_TEXT;
		        wf1.datalen = strlen(ENDPAGE_STR) + 1;
		        wf1.data = ENDPAGE_STR;
			ast_queue_frame(o->owner, &wf1);
			o->waspager = 0;
		}
	}			

	cd = 1; /* assume CD */
	if ((o->rxcdtype == CD_HID) && (!o->rxhidsq)) cd = 0;
	else if ((o->rxcdtype == CD_HID_INVERT) && o->rxhidsq) cd = 0;
	else if ((o->rxcdtype == CD_PP) && (!o->rxppsq)) cd = 0;
	else if ((o->rxcdtype == CD_PP_INVERT) && o->rxppsq) cd = 0;

	/* apply cd turn-on delay, if one specified */
	if (o->rxondelay && cd && (o->rxoncnt++ < o->rxondelay)) cd = 0;
	else if (!cd) o->rxoncnt = 0;

	sd = 1; /* assume SD */
	if ((o->rxsdtype == SD_HID) && (!o->rxhidctcss)) sd = 0;
	else if ((o->rxsdtype == SD_HID_INVERT) && o->rxhidctcss) sd = 0;
	else if ((o->rxsdtype == SD_PP) && (!o->rxppctcss)) sd = 0;
	else if ((o->rxsdtype == SD_PP_INVERT) && o->rxppctcss) sd = 0;

	if (o->rxctcssoverride) sd = 1;

	o->rxkeyed = sd && cd && ((!o->lasttx) || o->duplex);

	if (o->lastrx && (!o->rxkeyed))
	{
		o->lastrx = 0;
		// printf("AST_CONTROL_RADIO_UNKEY\n");
		wf.subclass = AST_CONTROL_RADIO_UNKEY;
		ast_queue_frame(o->owner, &wf);
		if (o->duplex3)
			setamixer(o->devicenum,MIXER_PARAM_MIC_PLAYBACK_SW,0,0);
	}
	else if ((!o->lastrx) && (o->rxkeyed))
	{
		o->lastrx = 1;
		//printf("AST_CONTROL_RADIO_KEY\n");
		wf.subclass = AST_CONTROL_RADIO_KEY;
		ast_queue_frame(o->owner, &wf);
		if (o->duplex3)
			setamixer(o->devicenum,MIXER_PARAM_MIC_PLAYBACK_SW,1,0);
	}

	sp = (short *)o->simpleusb_read_buf;
	sp1 = (short *)(o->simpleusb_read_frame_buf + AST_FRIENDLY_OFFSET);
	for(n = 0; n < FRAME_SIZE; n++)
	{
		(void)lpass(*sp++,o->flpr);
		sp++;
		(void)lpass(*sp++,o->flpr);
		sp++;
		(void)lpass(*sp++,o->flpr);
		sp++;
		(void)lpass(*sp++,o->flpr);
		sp++;
		(void)lpass(*sp++,o->flpr);
		sp++;
		if (o->plfilter && o->deemphasis)
			*sp1++ = hpass6(deemph(lpass(*sp++,o->flpr),&o->destate),
				o->hpx,o->hpy);
		else if (o->deemphasis)
			*sp1++ = deemph(lpass(*sp++,o->flpr),&o->destate);
		else if (o->plfilter)
			*sp1++ = hpass(lpass(*sp++,o->flpr),o->hpx,o->hpy);
		else
			*sp1++ = lpass(*sp++,o->flpr);
		sp++;
	}			

	if (o->echomode && o->rxkeyed && (!o->echoing))
	{
		int x;
		struct usbecho *u;

		ast_mutex_lock(&o->echolock);
		x = 0;
		/* get count of frames */
		for(u = (struct usbecho *) o->echoq.q_forw; 
			u != (struct usbecho *) &o->echoq; u = (struct usbecho *)u->q_forw) x++;
		if (x < o->echomax) 
		{
			u = (struct usbecho *) ast_malloc(sizeof(struct usbecho));
			memset(u,0,sizeof(struct usbecho));
			memcpy(u->data,(o->simpleusb_read_frame_buf + AST_FRIENDLY_OFFSET),FRAME_SIZE * 2);
			if (u == NULL)

				ast_log(LOG_ERROR,"Cannot malloc\n");
			else
				insque((struct qelem *)u,o->echoq.q_back);
		}
		ast_mutex_unlock(&o->echolock);
	}

	#if DEBUG_CAPTURES == 1
	if (o->b.rxcapraw && frxcapcooked) 
		fwrite(o->simpleusb_read_frame_buf + AST_FRIENDLY_OFFSET
			,sizeof(short),FRAME_SIZE,frxcapcooked);
	#endif
	f->offset = AST_FRIENDLY_OFFSET;
        o->readpos = 0;		       /* reset read pointer for next frame */
        if (c->_state != AST_STATE_UP)  /* drop data if frame is not up */
                return f;
        /* ok we can build and deliver the frame to the caller */
        f->frametype = AST_FRAME_VOICE;
        f->subclass = AST_FORMAT_SLINEAR;
        f->samples = FRAME_SIZE;
        f->datalen = FRAME_SIZE * 2;
        f->data = o->simpleusb_read_frame_buf + AST_FRIENDLY_OFFSET;
	if (!o->rxkeyed) memset(f->data,0,f->datalen);
	if (o->usedtmf && o->dsp)
	{
	    f1 = ast_dsp_process(c,o->dsp,f);
	    if ((f1->frametype == AST_FRAME_DTMF_END) ||
	      (f1->frametype == AST_FRAME_DTMF_BEGIN))
	    {
		if ((f1->subclass == 'm') || (f1->subclass == 'u'))
		{
			f1->frametype = AST_FRAME_NULL;
			f1->subclass = 0;
			return(f1);
		}
		if (f1->frametype == AST_FRAME_DTMF_END)
			ast_log(LOG_NOTICE,"Got DTMF char %c\n",f1->subclass);
		return(f1);
	    }
	}
        if (o->boost != BOOST_SCALE) {  /* scale and clip values */
                int i, x;
                int16_t *p = (int16_t *) f->data;
                for (i = 0; i < f->samples; i++) {
                        x = (p[i] * o->boost) / BOOST_SCALE;
                        if (x > 32767)
                                x = 32767;
                        else if (x < -32768)
                                x = -32768;
                        p[i] = x;
                }
        }
        if (o->rxvoiceadj > 1.0) {  /* scale and clip values */
                int i, x;
		float f1;
                int16_t *p = (int16_t *) f->data;

                for (i = 0; i < f->samples; i++) {
			f1 = (float)p[i] * o->rxvoiceadj;
			x = (int)f1;
                        if (x > 32767)
                                x = 32767;
                        else if (x < -32768)
                                x = -32768;
                        p[i] = x;
                }
        }
	if (o->b.measure_enabled)
	{
		int i;
		int32_t accum;
                int16_t *p = (int16_t *) f->data;

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
        return f;
}

static int simpleusb_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct chan_simpleusb_pvt *o = newchan->tech_pvt;
	ast_log(LOG_WARNING,"simpleusb_fixup()\n");
	o->owner = newchan;
	return 0;
}

static int simpleusb_indicate(struct ast_channel *c, int cond, const void *data, size_t datalen)
{
	struct chan_simpleusb_pvt *o = c->tech_pvt;
	int res = -1;

	switch (cond) {
		case AST_CONTROL_BUSY:
		case AST_CONTROL_CONGESTION:
		case AST_CONTROL_RINGING:
			res = cond;
			break;

		case -1:
#ifndef	NEW_ASTERISK
			o->cursound = -1;
			o->nosound = 0;		/* when cursound is -1 nosound must be 0 */
#endif
			return 0;

		case AST_CONTROL_VIDUPDATE:
			res = -1;
			break;
		case AST_CONTROL_HOLD:
			ast_verbose(" << Console Has Been Placed on Hold >> \n");
			ast_moh_start(c, data, o->mohinterpret);
			break;
		case AST_CONTROL_UNHOLD:
			ast_verbose(" << Console Has Been Retrieved from Hold >> \n");
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
			kickptt(o);
			if(o->debuglevel)ast_verbose("chan_simpleusb ACRK  dev=%s TX ON \n",o->name);
			break;
		case AST_CONTROL_RADIO_UNKEY:
			o->txkeyed = 0;
			kickptt(o);
			if(o->debuglevel)ast_verbose("chan_simpleusb ACRUK  dev=%s TX OFF >> \n",o->name);
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to display condition %d on %s\n", cond, c->name);
			return -1;
	}

	if (res > -1)
		ring(o, res);

	return 0;
}

static int simpleusb_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
	char *cp;
	struct chan_simpleusb_pvt *o = chan->tech_pvt;

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
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: OFF(0) on %s\n",chan->name);
			o->usedtmf = 1;
			break;
		case 2:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: MUTECONF/MAX(2) on %s\n",chan->name);
			o->usedtmf = 1;
			break;
		case 3:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: DISABLE DETECT(3) on %s\n",chan->name);
			o->usedtmf = 0;
			break;
		default:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: OFF(0) on %s\n",chan->name);
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
static struct ast_channel *simpleusb_new(struct chan_simpleusb_pvt *o, char *ext, char *ctx, int state)
{
	struct ast_channel *c;

	c = ast_channel_alloc(1, state, o->cid_num, o->cid_name, "", ext, ctx, 0, "SimpleUSB/%s", o->name);
	if (c == NULL)
		return NULL;
	c->tech = &simpleusb_tech;
	if ((o->sounddev < 0) && o->hasusb)
		setformat(o, O_RDWR);
	c->fds[0] = o->sounddev;	/* -1 if device closed, override later */
	c->nativeformats = AST_FORMAT_SLINEAR;
	c->readformat = AST_FORMAT_SLINEAR;
	c->writeformat = AST_FORMAT_SLINEAR;
	c->tech_pvt = o;

	if (!ast_strlen_zero(o->language))
		ast_string_field_set(c, language, o->language);
	/* Don't use ast_set_callerid() here because it will
	 * generate a needless NewCallerID event */
	c->cid.cid_num = ast_strdup(o->cid_num);
	c->cid.cid_ani = ast_strdup(o->cid_num);
	c->cid.cid_name = ast_strdup(o->cid_name);
	if (!ast_strlen_zero(ext))
		c->cid.cid_dnid = ast_strdup(ext);

	o->owner = c;
	ast_module_ref(ast_module_info->self);
	ast_jb_configure(c, &global_jbconf);
	if (state != AST_STATE_DOWN) {
		if (ast_pbx_start(c)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", c->name);
			ast_hangup(c);
			o->owner = c = NULL;
			/* XXX what about the channel itself ? */
			/* XXX what about usecnt ? */
		}
	}

	return c;
}
/*
*/
static struct ast_channel *simpleusb_request(const char *type, int format, void *data, int *cause)
{
	struct ast_channel *c;
	struct chan_simpleusb_pvt *o = _find_desc(data);

	if (0)
	{
		ast_log(LOG_WARNING, "simpleusb_request type <%s> data 0x%p <%s>\n", type, data, (char *) data);
	}
	if (o == NULL) {
		ast_log(LOG_NOTICE, "Device %s not found\n", (char *) data);
		/* XXX we could default to 'dsp' perhaps ? */
		return NULL;
	}
	if ((format & AST_FORMAT_SLINEAR) == 0) {
		ast_log(LOG_NOTICE, "Format 0x%x unsupported\n", format);
		return NULL;
	}
	if (o->owner) {
		ast_log(LOG_NOTICE, "Already have a call (chan %p) on the usb channel\n", o->owner);
		*cause = AST_CAUSE_BUSY;
		return NULL;
	}
	c = simpleusb_new(o, NULL, NULL, AST_STATE_DOWN);
	if (c == NULL) {
		ast_log(LOG_WARNING, "Unable to create new usb channel\n");
		return NULL;
	}
		
	return c;
}
/*
*/
static int console_key(int fd, int argc, char *argv[])
{
	struct chan_simpleusb_pvt *o = find_desc(simpleusb_active);

	if (argc != 2)
		return RESULT_SHOWUSAGE; 
	o->txclikey = 1;
	kickptt(o);
	return RESULT_SUCCESS;
}
/*
*/
static int console_unkey(int fd, int argc, char *argv[])
{
	struct chan_simpleusb_pvt *o = find_desc(simpleusb_active);

	if (argc != 2)
		return RESULT_SHOWUSAGE;
	o->txclikey = 0;
	kickptt(o);
	return RESULT_SUCCESS;
}

static int rad_rxwait(int fd,int ms)
{
fd_set fds;
struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(fd,&fds);
	tv.tv_usec = ms * 1000;
	tv.tv_sec = 0;
	return(select(fd + 1,&fds,NULL,NULL,&tv));
}

static void tune_rxdisplay(int fd, struct chan_simpleusb_pvt *o)
{
	int j,waskeyed,meas,ncols = 75,wasverbose;
	char str[256];

	for(j = 0; j < ncols; j++) str[j] = ' ';
	str[j] = 0;
	ast_cli(fd," %s \r",str);
	ast_cli(fd,"RX VOICE DISPLAY:\n");
	ast_cli(fd,"                                 v -- 3KHz        v -- 5KHz\n");

	o->b.measure_enabled = 1;
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
	o->b.measure_enabled = 0;
	option_verbose = wasverbose;
}

static int usb_device_swap(int fd,char *other)
{

int	d;
char	tmp[128];
struct chan_simpleusb_pvt *p = NULL,*o = find_desc(simpleusb_active);

	if (o == NULL) return -1;
	if (!other) return -1;
	p = find_desc(other);
	if (p == NULL)
	{
		ast_cli(fd,"USB Device %s not found\n",other);
		return -1;
	}
	if (p == o)
	{
		ast_cli(fd,"You cant swap active device with itself!!\n");
		return -1;
	}
	ast_mutex_lock(&usb_dev_lock);
	strcpy(tmp,p->devstr);
	d = p->devicenum;
	strcpy(p->devstr,o->devstr);
	p->devicenum = o->devicenum;
	strcpy(o->devstr,tmp);
	o->devicenum = d;
	o->hasusb = 0;
	o->usbass = 0;
	p->hasusb = 0;
	o->usbass = 0;
	ast_cli(fd,"USB Devices successfully swapped.\n");
	ast_mutex_unlock(&usb_dev_lock);
	return 0;
}

static int happy_mswait(int fd,int ms, int flag)
{
int	i;

	if (!flag)
	{
		usleep(ms * 1000);
		return(0);
	}
	i = 0;
	if (ms >= 100) for(i = 0; i < ms; i += 100)
	{
		ast_cli(fd,"\r");
		if (rad_rxwait(fd,100)) return(1);
	}
	if (rad_rxwait(fd,ms - i)) return(1);
	ast_cli(fd,"\r");
	return(0);
}

static int _send_tx_test_tone(int fd, struct chan_simpleusb_pvt *o,int ms, int intflag)
{
int	i,ret;

	ast_tonepair_stop(o->owner);
        if (ast_tonepair_start(o->owner, 1004.0, 0, 99999999, 7200.0))
	{
		if (fd >= 0) ast_cli(fd,"Error starting test tone on %s!!\n",simpleusb_active);
		return -1;
	}
	ast_clear_flag(o->owner, AST_FLAG_WRITE_INT);
	o->txtestkey = 1;	
	i = 0;
	ret = 0;
        while(o->owner->generatordata && (i < ms)) 
	{
		if (happy_mswait(fd,50,intflag)) 
		{
			ret = 1;
			break;
		}
		i += 50;
	}
	ast_tonepair_stop(o->owner);
 	ast_set_flag(o->owner, AST_FLAG_WRITE_INT);
	o->txtestkey = 0;	
	return ret;
}

static void _menu_print(int fd, struct chan_simpleusb_pvt *o)
{
	ast_cli(fd,"Active radio interface is [%s]\n",simpleusb_active);
	ast_mutex_lock(&usb_dev_lock);
	ast_cli(fd,"Device String is %s\n",o->devstr);
	ast_mutex_unlock(&usb_dev_lock);
  	ast_cli(fd,"Card is %i\n",usb_get_usbdev(o->devstr));
	ast_cli(fd,"Rx Level currently set to %d\n",o->rxmixerset);
	ast_cli(fd,"Tx A Level currently set to %d\n",o->txmixaset);
	ast_cli(fd,"Tx B Level currently set to %d\n",o->txmixbset);
	return;
}

static void _menu_rx(int fd, struct chan_simpleusb_pvt *o, char *str)
{
int	i,x;

	if (!str[0])
	{
		ast_cli(fd,"Current setting on Rx Channel is %d\n",o->rxmixerset);
		return;
	}
	for(x = 0; str[x]; x++)
	{
		if (!isdigit(str[x])) break;
	}
	if (str[x] || (sscanf(str,"%d",&i) < 1) || (i < 0) || (i > 999))
	{
		ast_cli(fd,"Entry Error, Rx Channel Level setting not changed\n");
		return;
	}
 	o->rxmixerset = i;
	ast_cli(fd,"Changed setting on RX Channel to %d\n",o->rxmixerset);
	mixer_write(o);
	return;
}

static void _menu_txa(int fd, struct chan_simpleusb_pvt *o, char *str)
{
int	i,dokey;

	if (!str[0])
	{
		ast_cli(fd,"Current setting on Tx Channel A is %d\n",o->txmixaset);
		return;
	}
	dokey = 0;
	if (str[0] == 'K')
	{
		dokey = 1;
		str++;
	}
	if (str[0])
	{
		if ((sscanf(str,"%d",&i) < 1) || (i < 0) || (i > 999))
		{
			ast_cli(fd,"Entry Error, Tx Channel A Level setting not changed\n");
			return;
		}
	 	o->txmixaset = i;
		ast_cli(fd,"Changed setting on TX Channel A to %d\n",o->txmixaset);
		mixer_write(o);
	}
	if (dokey)
	{
		if (fd >= 0) ast_cli(fd,"Keying Transmitter and sending 1000 Hz tone for 5 seconds...\n");
		 _send_tx_test_tone(fd,o,5000,1);
	}
	return;
}

static void _menu_txb(int fd, struct chan_simpleusb_pvt *o, char *str)
{
int	i,dokey;

	if (!str[0])
	{
		ast_cli(fd,"Current setting on Tx Channel B is %d\n",o->txmixbset);
		return;
	}
	dokey = 0;
	if (str[0] == 'K')
	{
		dokey = 1;
		str++;
	}
	if (str[0])
	{
		if ((sscanf(str,"%d",&i) < 1) || (i < 0) || (i > 999))
		{
			ast_cli(fd,"Entry Error, Tx Channel B Level setting not changed\n");
			return;
		}
	 	o->txmixbset = i;
		ast_cli(fd,"Changed setting on TX Channel B to %d\n",o->txmixbset);
		mixer_write(o);
	}
	if (dokey)
	{
		if (fd >= 0) ast_cli(fd,"Keying Transmitter and sending 1000 Hz tone for 5 seconds...\n");
		 _send_tx_test_tone(fd,o,5000,1);
	}
	return;
}

static void tune_flash(int fd, struct chan_simpleusb_pvt *o, int intflag)
{
#define	NFLASH 3

int	i;

	if (fd > 0)
		ast_cli(fd,"USB Device Flash starting on channel %s...\n",o->name);
	for(i = 0; i < NFLASH; i++)
	{
		if (_send_tx_test_tone(fd,o,1000,intflag)) break;
		if (happy_mswait(fd,1000,intflag)) break;
	}
	o->txtestkey = 0;
	if (fd > 0)
		ast_cli(fd,"USB Device Flash ending on channel %s...\n",o->name);
	return;
}

static void tune_menusupport(int fd, struct chan_simpleusb_pvt *o, char *cmd)
{
int	x,oldverbose;
struct chan_simpleusb_pvt *oy = NULL;

	oldverbose = option_verbose;
	option_verbose = 0;
	switch(cmd[0])
	{
	    case '0': /* return audio processing configuration */
		ast_cli(fd,"0,0,%d\n",o->echomode);
		break;
	    case '1': /* return usb device name list */
		for (x = 0,oy = simpleusb_default.next; oy && oy->name ; oy = oy->next, x++)
		{
			if (x) ast_cli(fd,",");
			ast_cli(fd,"%s",oy->name);
		}
		ast_cli(fd,"\n");
		break;
	    case '2': /* print parameters */
		_menu_print(fd,o);
		break;
	    case '3': /* return usb device name list except current */
		for (x = 0,oy = simpleusb_default.next; oy && oy->name ; oy = oy->next)
		{
			if (!strcmp(oy->name,o->name)) continue;
			if (x) ast_cli(fd,",");
			ast_cli(fd,"%s",oy->name);
			x++;
		}
		ast_cli(fd,"\n");
		break;
	    case 'b':
		if (!o->hasusb)
		{
			ast_cli(fd,"Device %s currently not active\n",o->name);
			break;
		}
		tune_rxdisplay(fd,o);
		break;
	    case 'c':
		if (!o->hasusb)
		{
			ast_cli(fd,"Device %s currently not active\n",o->name);
			break;
		}
		_menu_rx(fd, o, cmd + 1);
		break;
	    case 'f':
		if (!o->hasusb)
		{
			ast_cli(fd,"Device %s currently not active\n",o->name);
			break;
		}
		_menu_txa(fd,o,cmd + 1);
		break;
	    case 'g':
		if (!o->hasusb)
		{
			ast_cli(fd,"Device %s currently not active\n",o->name);
			break;
		}
		_menu_txb(fd,o,cmd + 1);
		break;
	    case 'j':
		tune_write(o);
		ast_cli(fd,"Saved radio tuning settings to simpleusb_tune_%s.conf\n",o->name);
		break;
	    case 'k':
		if (cmd[1])
		{
			if (cmd[1] > '0') o->echomode = 1;
			else o->echomode = 0;
			ast_cli(fd,"Echo Mode changed to %s\n",(o->echomode) ? "Enabled" : "Disabled");
		}
		else
			ast_cli(fd,"Echo Mode is currently %s\n",(o->echomode) ? "Enabled" : "Disabled");
		break;
	    case 'l':
		if (!o->hasusb)
		{
			ast_cli(fd,"Device %s currently not active\n",o->name);
			break;
		}
		tune_flash(fd,o,1);
		break;
	    default:
		ast_cli(fd,"Invalid Command\n");
		break;
	}
	option_verbose = oldverbose;
	return;
}

static int radio_tune(int fd, int argc, char *argv[])
{
	struct chan_simpleusb_pvt *o = find_desc(simpleusb_active);
	int i=0;

	if ((argc < 2) || (argc > 4))
		return RESULT_SHOWUSAGE; 

	if (argc == 2) /* just show stuff */
	{
		ast_cli(fd,"Active radio interface is [%s]\n",simpleusb_active);
		ast_cli(fd,"Device String is %s\n",o->devstr);
		ast_cli(fd,"Rx Level currently set to %d\n",o->rxmixerset);
		ast_cli(fd,"Tx Output A Level currently set to %d\n",o->txmixaset);
		ast_cli(fd,"Tx Output B Level currently set to %d\n",o->txmixbset);
		return RESULT_SHOWUSAGE;
	}

	else if (!strcasecmp(argv[2],"swap"))
	{
		if (argc > 3) 
		{
			usb_device_swap(fd,argv[3]);
			return RESULT_SUCCESS;
		}
		return RESULT_SHOWUSAGE;
	}
	else if (!strcasecmp(argv[2],"menu-support"))
	{
		if (argc > 3) tune_menusupport(fd,o,argv[3]);
		return RESULT_SUCCESS;
	}
	if (!o->hasusb)
	{
		ast_cli(fd,"Device %s currently not active\n",o->name);
		return RESULT_SUCCESS;
	}

	else if (!strcasecmp(argv[2],"rx")) {
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
			mixer_write(o);
		}
	}
	else if (!strncasecmp(argv[2],"rxd",3)) {
		tune_rxdisplay(fd,o);
	}
	else if (!strcasecmp(argv[2],"txa")) {
		i = 0;

		if (argc == 3)
		{
			ast_cli(fd,"Current setting on Tx Channel A is %d\n",o->txmixaset);
		}
		else
		{
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) return RESULT_SHOWUSAGE;
		 	o->txmixaset = i;
			ast_cli(fd,"Changed setting on TX Channel A to %d\n",o->txmixaset);
			mixer_write(o);
		}
	}
	else if (!strcasecmp(argv[2],"txb")) {
		i = 0;

		if (argc == 3)
		{
			ast_cli(fd,"Current setting on Tx Channel A is %d\n",o->txmixbset);
		}
		else
		{
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) return RESULT_SHOWUSAGE;
		 	o->txmixbset = i;
			ast_cli(fd,"Changed setting on TX Channel A to %d\n",o->txmixbset);
			mixer_write(o);
		}
	}
	else if (!strcasecmp(argv[2],"flash")) {
		tune_flash(fd,o,0);
	}
	else if (!strcasecmp(argv[2],"nocap")) 	
	{
		ast_cli(fd,"File capture (raw)   was rx=%d tx=%d and now off.\n",o->b.rxcapraw,o->b.txcapraw);
		o->b.rxcapraw=o->b.txcapraw=0;
		if (frxcapraw) { fclose(frxcapraw); frxcapraw = NULL; }
		if (frxcapcooked) { fclose(frxcapcooked); frxcapcooked = NULL; }
		if (ftxcapraw) { fclose(ftxcapraw); ftxcapraw = NULL; }
	}
	else if (!strcasecmp(argv[2],"rxcap")) 
	{
		if (!frxcapraw) frxcapraw = fopen(RX_CAP_RAW_FILE,"w");
		if (!frxcapcooked) frxcapcooked = fopen(RX_CAP_COOKED_FILE,"w");
		ast_cli(fd,"cap rx raw on.\n");
		o->b.rxcapraw=1;
	}
	else if (!strcasecmp(argv[2],"txcap")) 
	{
		if (!ftxcapraw) ftxcapraw = fopen(TX_CAP_RAW_FILE,"w");
		ast_cli(fd,"cap tx raw on.\n");
		o->b.txcapraw=1;
	}
	else if (!strcasecmp(argv[2],"save"))
	{
		tune_write(o);
		ast_cli(fd,"Saved radio tuning settings to simpleusb_tune_%s.conf\n",o->name);
	}
	else if (!strcasecmp(argv[2],"load"))
	{
		ast_mutex_lock(&o->eepromlock);
		while(o->eepromctl)
		{
			ast_mutex_unlock(&o->eepromlock);
			usleep(10000);
			ast_mutex_lock(&o->eepromlock);
		}
		o->eepromctl = 1;  /* request a load */
		ast_mutex_unlock(&o->eepromlock);

		ast_cli(fd,"Requesting loading of tuning settings from EEPROM for channel %s\n",o->name);
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
static int radio_set_debug(int fd, int argc, char *argv[])
{
	struct chan_simpleusb_pvt *o = find_desc(simpleusb_active);

	o->debuglevel=1;
	ast_cli(fd,"simpleusb debug on.\n");
	return RESULT_SUCCESS;
}

static int radio_set_debug_off(int fd, int argc, char *argv[])
{
	struct chan_simpleusb_pvt *o = find_desc(simpleusb_active);

	o->debuglevel=0;
	ast_cli(fd,"simpleusb debug off.\n");
	return RESULT_SUCCESS;
}

static int radio_active(int fd, int argc, char *argv[])
{
        if (argc == 2)
                ast_cli(fd, "active (command) USB Radio device is [%s]\n", simpleusb_active);
        else if (argc != 3)
                return RESULT_SHOWUSAGE;
        else {
                struct chan_simpleusb_pvt *o;
                if (strcmp(argv[2], "show") == 0) {
                        for (o = simpleusb_default.next; o; o = o->next)
                                ast_cli(fd, "device [%s] exists as device=%s card=%d\n", 
					o->name,o->devstr,usb_get_usbdev(o->devstr));
                        return RESULT_SUCCESS;
                }
                o = _find_desc(argv[2]);
                if (o == NULL)
                        ast_cli(fd, "No device [%s] exists\n", argv[2]);
                else
		{
                    simpleusb_active = o->name;
		}
        }
        return RESULT_SUCCESS;
}

static char key_usage[] =
	"Usage: susb key\n"
	"       Simulates COR active.\n";

static char unkey_usage[] =
	"Usage: susb unkey\n"
	"       Simulates COR un-active.\n";

static char active_usage[] =
        "Usage: susb active [device-name]\n"
        "       If used without a parameter, displays which device is the current\n"
        "one being commanded.  If a device is specified, the commanded radio device is changed\n"
        "to the device specified.\n";
/*
radio tune 6 3000		measured tx value
*/
static char radio_tune_usage[] =
	"Usage: susb tune <function>\n"
	"       rx [newsetting]\n"
	"       rxdisplay\n"
	"       txa [newsetting]\n"
	"       txb [newsetting]\n"
	"       save (settings to tuning file)\n"
	"       load (tuning settings from EEPROM)\n"
	"\n       All [newsetting]'s are values 0-999\n\n";
					  
#ifndef	NEW_ASTERISK

static struct ast_cli_entry cli_simpleusb[] = {
	{ { "susb", "key", NULL },
	console_key, "Simulate Rx Signal Present",
	key_usage, NULL, NULL},

	{ { "susb", "unkey", NULL },
	console_unkey, "Simulate Rx Signal Lusb",
	unkey_usage, NULL, NULL },

	{ { "susb", "tune", NULL },
	radio_tune, "Radio Tune",
	radio_tune_usage, NULL, NULL },

	{ { "susb", "set", "debug", NULL },
	radio_set_debug, "Radio Debug",
	radio_tune_usage, NULL, NULL },

	{ { "susb", "set", "debug", "off", NULL },
	radio_set_debug_off, "Radio Debug",
	radio_tune_usage, NULL, NULL },

	{ { "susb", "active", NULL },
	radio_active, "Change commanded device",
	active_usage, NULL, NULL },

};
#endif

static void store_rxcdtype(struct chan_simpleusb_pvt *o, char *s)
{
	if (!strcasecmp(s,"no")){
		o->rxcdtype = CD_IGNORE;
	}
	else if (!strcasecmp(s,"usb")){
		o->rxcdtype = CD_HID;
	}
	else if (!strcasecmp(s,"usbinvert")){
		o->rxcdtype = CD_HID_INVERT;
	}	
	else if (!strcasecmp(s,"pp")){
		o->rxcdtype = CD_PP;
	}	
	else if (!strcasecmp(s,"ppinvert")){
		o->rxcdtype = CD_PP_INVERT;
	}	
	else {
		ast_log(LOG_WARNING,"Unrecognized rxcdtype parameter: %s\n",s);
	}

	//ast_log(LOG_WARNING, "set rxcdtype = %s\n", s);
}
/*
*/
static void store_rxsdtype(struct chan_simpleusb_pvt *o, char *s)
{
	if (!strcasecmp(s,"no") || !strcasecmp(s,"SD_IGNORE")){
		o->rxsdtype = SD_IGNORE;
	}
	else if (!strcasecmp(s,"usb") || !strcasecmp(s,"SD_HID")){
		o->rxsdtype = SD_HID;
	}
	else if (!strcasecmp(s,"usbinvert") || !strcasecmp(s,"SD_HID_INVERT")){
		o->rxsdtype = SD_HID_INVERT;
	}	
	else if (!strcasecmp(s,"pp")) {
		o->rxsdtype = SD_PP;
	}	
	else if (!strcasecmp(s,"ppinvert")) {
		o->rxsdtype = SD_PP_INVERT;
	}	
	else {
		ast_log(LOG_WARNING,"Unrecognized rxsdtype parameter: %s\n",s);
	}

	//ast_log(LOG_WARNING, "set rxsdtype = %s\n", s);
}

static void store_pager(struct chan_simpleusb_pvt *o, char *s)
{
	if (!strcasecmp(s,"no")){
		o->pager = PAGER_NONE;
	}
	else if (!strcasecmp(s,"a")){
		o->pager = PAGER_A;
	}
	else if (!strcasecmp(s,"b")){
		o->pager = PAGER_B;
	}	
	else {
		ast_log(LOG_WARNING,"Unrecognized pager parameter: %s\n",s);
	}

	//ast_log(LOG_WARNING, "set pager = %s\n", s);
}

static void tune_write(struct chan_simpleusb_pvt *o)
{
	FILE *fp;
	char fname[200];

 	snprintf(fname,sizeof(fname) - 1,"/etc/asterisk/simpleusb_tune_%s.conf",o->name);
	fp = fopen(fname,"w");

	fprintf(fp,"[%s]\n",o->name);

	fprintf(fp,"; name=%s\n",o->name);
	fprintf(fp,"; devicenum=%i\n",o->devicenum);
	fprintf(fp,"devstr=%s\n",o->devstr);
	fprintf(fp,"rxmixerset=%i\n",o->rxmixerset);
	fprintf(fp,"txmixaset=%i\n",o->txmixaset);
	fprintf(fp,"txmixbset=%i\n",o->txmixbset);
	fclose(fp);

	if(o->wanteeprom)
	{
		ast_mutex_lock(&o->eepromlock);
		while(o->eepromctl)
		{
			ast_mutex_unlock(&o->eepromlock);
			usleep(10000);
			ast_mutex_lock(&o->eepromlock);
		}
		o->eeprom[EEPROM_RXMIXERSET] = o->rxmixerset;
		o->eeprom[EEPROM_TXMIXASET] = o->txmixaset;
		o->eeprom[EEPROM_TXMIXBSET] = o->txmixbset;
		o->eepromctl = 2;  /* request a write */
		ast_mutex_unlock(&o->eepromlock);
	}
}
//
static void mixer_write(struct chan_simpleusb_pvt *o)
{
	int x;
	float f,f1;

	if (o->duplex3)
	{
		if (o->duplex3 > o->micplaymax)
			o->duplex3 = o->micplaymax;
		setamixer(o->devicenum,MIXER_PARAM_MIC_PLAYBACK_VOL,o->duplex3,0);
	}
	else
	{
		setamixer(o->devicenum,MIXER_PARAM_MIC_PLAYBACK_VOL,0,0);
	}
	setamixer(o->devicenum,MIXER_PARAM_MIC_PLAYBACK_SW,0,0);
	setamixer(o->devicenum,(o->newname) ? MIXER_PARAM_SPKR_PLAYBACK_SW_NEW : MIXER_PARAM_SPKR_PLAYBACK_SW,1,0);
	setamixer(o->devicenum,(o->newname) ? MIXER_PARAM_SPKR_PLAYBACK_VOL_NEW : MIXER_PARAM_SPKR_PLAYBACK_VOL,
		make_spkr_playback_value(o,o->txmixaset),
		make_spkr_playback_value(o,o->txmixbset));
	x =  o->rxmixerset * o->micmax / 1000;
	setamixer(o->devicenum,MIXER_PARAM_MIC_CAPTURE_VOL,x,0);
	/* get interval step size */
	f = 1000.0 / (float) o->micmax;
	o->rxvoiceadj = 1.0 + (modff(((float) o->rxmixerset) / f,&f1) * .187962);
	setamixer(o->devicenum,MIXER_PARAM_MIC_BOOST,o->rxboostset,0);
	setamixer(o->devicenum,MIXER_PARAM_MIC_CAPTURE_SW,1,0);
}
/*
	adjust dsp multiplier to add resolution to tx level adjustment
*/


/*
 * grab fields from the config file, init the descriptor and open the device.
 */
static struct chan_simpleusb_pvt *store_config(struct ast_config *cfg, char *ctg,int *indexp)
{
	struct ast_variable *v;
	struct chan_simpleusb_pvt *o;
	struct ast_config *cfg1;
	char fname[200],buf[100];
	int i;
#ifdef	NEW_ASTERISK
	struct ast_flags zeroflag = {0};
#endif
	if (ctg == NULL) {
		traceusb1((" store_config() ctg == NULL\n"));
		o = &simpleusb_default;
		ctg = "general";
	} else {
		/* "general" is also the default thing */
		if (strcmp(ctg, "general") == 0) {
			o = &simpleusb_default;
		} else {
		    // ast_log(LOG_NOTICE,"ast_calloc for chan_simpleusb_pvt of %s\n",ctg);
			if (!(o = ast_calloc(1, sizeof(*o))))
				return NULL;
			*o = simpleusb_default;
			o->name = ast_strdup(ctg);
			o->index = (*indexp)++;
			o->pttkick[0] = -1;
			o->pttkick[1] = -1;
			if (!simpleusb_active) 
				simpleusb_active = o->name;
		}
	}
	o->echoq.q_forw = o->echoq.q_back = &o->echoq;
	ast_mutex_init(&o->echolock);
	ast_mutex_init(&o->eepromlock);
	ast_mutex_init(&o->txqlock);
	ast_mutex_init(&o->usblock);
	o->echomax = DEFAULT_ECHO_MAX;
	strcpy(o->mohinterpret, "default");
	/* fill other fields from configuration */
	for (v = ast_variable_browse(cfg, ctg); v; v = v->next) {
		M_START((char *)v->name, (char *)v->value);

		/* handle jb conf */
		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value))
			continue;

			M_UINT("frags", o->frags)
			M_UINT("queuesize",o->queuesize)
			M_UINT("debug", simpleusb_debug)
			M_BOOL("rxcpusaver",o->rxcpusaver)
			M_BOOL("txcpusaver",o->txcpusaver)
			M_BOOL("invertptt",o->invertptt)
			M_F("carrierfrom",store_rxcdtype(o,(char *)v->value))
			M_F("ctcssfrom",store_rxsdtype(o,(char *)v->value))
 			M_BOOL("rxboost",o->rxboostset)
			M_UINT("hdwtype",o->hdwtype)
			M_UINT("eeprom",o->wanteeprom)
			M_UINT("duplex",o->radioduplex)
			M_UINT("rxondelay",o->rxondelay)
			M_F("pager",store_pager(o,(char *)v->value))
 			M_BOOL("plfilter",o->plfilter)
 			M_BOOL("deemphasis",o->deemphasis)
 			M_BOOL("preemphasis",o->preemphasis)
 			M_UINT("duplex3",o->duplex3)
			M_END(;
			);
			for(i = 0; i < 32; i++)
			{
				sprintf(buf,"gpio%d",i + 1);
				if (!strcmp(v->name,buf)) o->gpios[i] = strdup(v->value);
			}
			for(i = 2; i <= 15; i++)
			{
				if (!((1 << i) & PP_MASK)) continue;
				sprintf(buf,"pp%d",i);
				if (!strcasecmp(v->name,buf)) {
					o->pps[i] = strdup(v->value);
					haspp = 1;
				}
			}
	}

	o->debuglevel=0;

	if (o == &simpleusb_default)		/* we are done with the default */
		return NULL;

	for(i = 2; i <= 9; i++)
	{
		/* skip if this one not specified */
		if (!o->pps[i]) continue;
		/* skip if not out or PTT */
		if (strncasecmp(o->pps[i],"out",3) &&
			strcasecmp(o->pps[i],"ptt")) continue;
		/* if default value is 1, set it */
		if (!strcasecmp(o->pps[i],"out1")) pp_val |= (1 << (i - 2));
		hasout = 1;
	}

	snprintf(fname,sizeof(fname) - 1,config1,o->name);
#ifdef	NEW_ASTERISK
	cfg1 = ast_config_load(fname,zeroflag);
#else
	cfg1 = ast_config_load(fname);
#endif
	o->rxmixerset = 500;
	o->txmixaset = 500;
	o->txmixbset = 500;
	o->devstr[0] = 0;
	if (cfg1) {
		for (v = ast_variable_browse(cfg1, o->name); v; v = v->next) {
	
			M_START((char *)v->name, (char *)v->value);
			M_UINT("rxmixerset", o->rxmixerset)
			M_UINT("txmixaset", o->txmixaset)
			M_UINT("txmixbset", o->txmixbset)
			M_STR("devstr", o->devstr)
			M_END(;
			);
		}
		ast_config_destroy(cfg1);
	} else ast_log(LOG_WARNING,"File %s not found, using default parameters.\n",fname);

	if(o->wanteeprom)
	{
		ast_mutex_lock(&o->eepromlock);
		while(o->eepromctl)
		{
			ast_mutex_unlock(&o->eepromlock);
			usleep(10000);
			ast_mutex_lock(&o->eepromlock);
		}
		o->eepromctl = 1;  /* request a load */
		ast_mutex_unlock(&o->eepromlock);
	}
	o->lastopen = ast_tvnow();	/* don't leave it 0 or tvdiff may wrap */
	o->dsp = ast_dsp_new();
	if (o->dsp)
	{
#ifdef  NEW_ASTERISK
          ast_dsp_set_features(o->dsp,DSP_FEATURE_DIGIT_DETECT);
          ast_dsp_set_digitmode(o->dsp,DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
#else
          ast_dsp_set_features(o->dsp,DSP_FEATURE_DTMF_DETECT);
          ast_dsp_digitmode(o->dsp,DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
#endif
	}
	hidhdwconfig(o);

#ifndef	NEW_ASTERISK
	if (pipe(o->sndcmd) != 0) {
		ast_log(LOG_ERROR, "Unable to create pipe\n");
		goto error;
	}

	ast_pthread_create_background(&o->sthread, NULL, sound_thread, o);
#endif

	/* link into list of devices */
	if (o != &simpleusb_default) {
		o->next = simpleusb_default.next;
		simpleusb_default.next = o;
	}
	return o;
  
  error:
	if (o != &simpleusb_default)
		free(o);
	return NULL;
}

#ifdef	NEW_ASTERISK

static char *res2cli(int r)

{
	switch (r)
	{
	    case RESULT_SUCCESS:
		return(CLI_SUCCESS);
	    case RESULT_SHOWUSAGE:
		return(CLI_SHOWUSAGE);
	    default:
		return(CLI_FAILURE);
	}
}

static char *handle_console_key(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "susb key";
                e->usage = key_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(console_key(a->fd,a->argc,a->argv));
}

static char *handle_console_unkey(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "susb unkey";
                e->usage = unkey_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(console_unkey(a->fd,a->argc,a->argv));
}

static char *handle_susb_tune(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "susb tune";
                e->usage = susb_tune_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(susb_tune(a->fd,a->argc,a->argv));
}

static char *handle_susb_debug(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "susb debug";
                e->usage = susb_tune_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(susb_set_debug(a->fd,a->argc,a->argv));
}

static char *handle_susb_debug_off(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "susb debug off";
                e->usage = susb_tune_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(susb_set_debug_off(a->fd,a->argc,a->argv));
}

static char *handle_susb_active(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "susb active";
                e->usage = active_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(susb_active(a->fd,a->argc,a->argv));
}

static struct ast_cli_entry cli_simpleusb[] = {
	AST_CLI_DEFINE(handle_console_key,"Simulate Rx Signal Present"),
	AST_CLI_DEFINE(handle_console_unkey,"Simulate Rx Signal Loss"),
	AST_CLI_DEFINE(handle_susb_tune,"susb Tune"),
	AST_CLI_DEFINE(handle_susb_debug,"susb Debug On"),
	AST_CLI_DEFINE(handle_susb_debug_off,"susb Debug Off"),
	AST_CLI_DEFINE(handle_susb_active,"Change commanded device")
};

#endif


/*
*/
static int load_module(void)
{
	struct ast_config *cfg = NULL;
	char *ctg = NULL,*val;
	int n;
#ifdef	NEW_ASTERISK
	struct ast_flags zeroflag = {0};
#endif

	if (hid_device_mklist()) {
		ast_log(LOG_NOTICE, "Unable to make hid list\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	usb_list_check("");

	simpleusb_active = NULL;

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	pp_val = 0;
	hasout = 0;

	/* load config file */
#ifdef	NEW_ASTERISK
	if (!(cfg = ast_config_load(config,zeroflag))) {
#else
	if (!(cfg = ast_config_load(config))) {
#endif
		ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
		return AST_MODULE_LOAD_DECLINE;
	}

	n = 0;
	do {
		store_config(cfg, ctg, &n);
	} while ( (ctg = ast_category_browse(cfg, ctg)) != NULL);

	ppfd = -1;
	pbase = 0;
	val = (char *) ast_variable_retrieve(cfg, "general", "pport");
	if (val) ast_copy_string(pport,val,sizeof(pport) - 1);
	else strcpy(pport,PP_PORT);
	val = (char *) ast_variable_retrieve(cfg, "general", "pbase");
	if (val) pbase = strtoul(val,NULL,0);
	if (!pbase) pbase = PP_IOPORT;
	if (haspp) /* if is to use parallel port */
	{
		if (pport[0])
		{
			ppfd = open(pport,O_RDWR);
			if (ppfd != -1)
			{
				if (ioctl(ppfd, PPCLAIM))
				{
					ast_log(LOG_ERROR,"Unable to claim printer port %s, disabling pp support\n",pport);
					close(ppfd);
					haspp = 0;
				}
			} 
			else
			{
				if (ioperm(pbase,2,1) == -1)
				{
					ast_log(LOG_ERROR,"Cant get io permission on IO port %04x hex, disabling pp support\n",pbase);
					haspp = 0;
				}
				haspp = 2;
				if (option_verbose > 2) ast_verbose(VERBOSE_PREFIX_3 "Using direct IO port for pp support, since parport driver not available.\n");
			}
		}
	}

	if (option_verbose > 2)
	{
		if (haspp == 1) ast_verbose(VERBOSE_PREFIX_3 "Parallel port is %s\n",pport);
		else if (haspp == 2) ast_verbose(VERBOSE_PREFIX_3 "Parallel port is at %04x hex\n",pbase);
	}

	ast_config_destroy(cfg);

	if (_find_desc(simpleusb_active) == NULL) {
		ast_log(LOG_NOTICE, "susb active device %s not found\n", simpleusb_active);
		/* XXX we could default to 'dsp' perhaps ? */
		/* XXX should cleanup allocated memory etc. */
		return AST_MODULE_LOAD_FAILURE;
	}

	if (ast_channel_register(&simpleusb_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel type 'usb'\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	ast_cli_register_multiple(cli_simpleusb, sizeof(cli_simpleusb) / sizeof(struct ast_cli_entry));

	if (haspp && hasout) ast_pthread_create_background(&pulserid, NULL, pulserthread, NULL);

	return AST_MODULE_LOAD_SUCCESS;
}
/*
*/
static int unload_module(void)
{
	struct chan_simpleusb_pvt *o;

	ast_log(LOG_WARNING, "unload_module() called\n");

	ast_channel_unregister(&simpleusb_tech);
	ast_cli_unregister_multiple(cli_simpleusb, sizeof(cli_simpleusb) / sizeof(struct ast_cli_entry));

	for (o = simpleusb_default.next; o; o = o->next) {

		#if DEBUG_CAPTURES == 1
		if (frxcapraw) { fclose(frxcapraw); frxcapraw = NULL; }
		if (frxcapcooked) { fclose(frxcapraw); frxcapcooked = NULL; }
		if (ftxcapraw) { fclose(ftxcapraw); ftxcapraw = NULL; }
		#endif

		close(o->sounddev);
#ifndef	NEW_ASTERISK
		if (o->sndcmd[0] > 0) {
			close(o->sndcmd[0]);
			close(o->sndcmd[1]);
		}
#endif
		if (o->dsp) ast_dsp_free(o->dsp);
		if (o->owner)
			ast_softhangup(o->owner, AST_SOFTHANGUP_APPUNLOAD);
		if (o->owner)			/* XXX how ??? */
			return -1;
		/* XXX what about the thread ? */
		/* XXX what about the memory allocated ? */
	}
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "simple usb Radio Interface Channel Driver");

/*	end of file */


