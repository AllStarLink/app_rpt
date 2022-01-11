/* #define	NEW_ASTERISK */
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
	<depend>ossaudio</depend>
        <depend>usb</depend>
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
#include <errno.h>
#include <usb.h>
#include <search.h>
#include <linux/ppdev.h>
#include <linux/parport.h>
#include <linux/version.h>
#include <alsa/asoundlib.h>

//#define HAVE_XPMRX				1
#ifdef RADIO_XPMRX
#define HAVE_XPMRX				1
#endif

#define CHAN_USBRADIO           1
#define DEBUG_USBRADIO          0	
#define DEBUG_CAPTURES	 		1
#define DEBUG_CAP_RX_OUT		0   		
#define DEBUG_CAP_TX_OUT	    0			
#define DEBUG_FILETEST			0			 

#define RX_CAP_RAW_FILE			"/tmp/rx_cap_in.pcm"
#define RX_CAP_TRACE_FILE		"/tmp/rx_trace.pcm"
#define RX_CAP_OUT_FILE			"/tmp/rx_cap_out.pcm"

#define TX_CAP_RAW_FILE			"/tmp/tx_cap_in.pcm"
#define TX_CAP_TRACE_FILE		"/tmp/tx_trace.pcm"
#define TX_CAP_OUT_FILE			"/tmp/tx_cap_out.pcm"

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

#define	DEFAULT_ECHO_MAX 1000  /* 20 secs of echo buffer, max */

#define	PP_MASK 0xbffc
#define	PP_PORT "/dev/parport0"
#define	PP_IOPORT 0x378

#include "./xpmr/xpmr.h"
#ifdef HAVE_XPMRX
#include "./xpmrx/xpmrx.h"
#include "./xpmrx/bitweight.h"
#endif

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
#define C119_PRODUCT_ID  	0x0008
#define C119A_PRODUCT_ID  	0x013a
#define C119B_PRODUCT_ID        0x0013
#define N1KDO_PRODUCT_ID  	0x6a00
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
 * usbradio.conf parameters are
START_CONFIG

[general]

; General config options that apply to entire channel driver.
;
    ; pport=/dev/parport0	; Specify parport device (optional)
    ; pbase=0x378		; Specify printer port I/O address (if not using parport driver) (optional)
    ;
    ; debug = 0x0		; misc debug flags, default is 0

[usb]

; First channel unique config

    ; Channel config options, with
    ; default values shown. You may have as many devices as the
    ; system will allow. You must use one section per device, with
    ; [usb] generally (although its up to you) being the first device.

	; Set the device to use for I/O
	; devicenum = 0
	; Set hardware type here
	; hdwtype=0               ; 0=limey, 1=sph

	; rxboost=0          ; no rx gain boost
	; txboost=0		; no tx gain boost
	; rxctcssrelax=1        ; reduce talkoff from radios w/o CTCSS Tx HPF
	; rxctcssfreqs=100.0,123.0      ; list of rx ctcss freq in floating point. must be in table
	; txctcssfreqs=100.0,123.0      ; list tx ctcss freq, any frequency permitted
	; txctcssdefault=100.0      ; default tx ctcss freq, any frequency permitted
	; rxctcssoverride=0	; default condition to not require rx CTCSS

	; carrierfrom=dsp     ;no,usb,usbinvert,dsp,vox,pp,ppinvert
	; ctcssfrom=dsp       ;no,usb,usbinvert,dsp,pp,ppinvert

	; rxdemod=flat            ; input type from radio: no,speaker,flat
	; txlimonly=no            ; output is limited, but not pre-emphasised
	; txprelim=yes            ; output is pre-emphasised and limited
	; txtoctype=no            ; no,phase,notone

	; txmixa=composite        ;no,voice,tone,composite,auxvoice
	; txmixb=no               ;no,voice,tone,composite,auxvoice

	; invertptt=0

	; rxondelay=0		  ; number of 20ms intervals to hold off receiver turn-on indication

	; txoffdelay=0		  ; number of 20ms intervals to hold off receiver turn-on indication (only after transmitter unkeys)
	
	; duplex3 = 0		; duplex 3 gain setting (0 to disable)

	; gpioX=in		; define input/output pin GPIO(x) in,out0,out1 (where X {1..32}) (optional)

	; ppX=out0		; printer port pin(x) [x is 2-9] initial output state [out0,out1] or 'ptt' (for output pins)
	; ppX=in		; printer port pin(x) [x is 10,12,13,15] input pin [in,cor,ctcss]


    ;------------------------------ JITTER BUFFER CONFIGURATION --------------------------
    ; jbenable = yes              ; Enables the use of a jitterbuffer on the receiving side of an
                                  ; USBRADIO channel. Defaults to "no". An enabled jitterbuffer will
                                  ; be used only if the sending side can create and the receiving
                                  ; side can not accept jitter. The USBRADIO channel can't accept jitter,
                                  ; thus an enabled jitterbuffer on the receive USBRADIO side will always
                                  ; be used if the sending side can create jitter.

    ; jbmaxsize = 200             ; Max length of the jitterbuffer in milliseconds.

    ; jbresyncthreshold = 1000    ; Jump in the frame timestamps over which the jitterbuffer is
                                  ; resynchronized. Useful to improve the quality of the voice, with
                                  ; big jumps in/broken timestamps, usualy sent from exotic devices
                                  ; and programs. Defaults to 1000.

    ; jbimpl = fixed              ; Jitterbuffer implementation, used on the receiving side of an USBRADIO
                                  ; channel. Two implementations are currenlty available - "fixed"
                                  ; (with size always equals to jbmax-size) and "adaptive" (with
                                  ; variable size, actually the new jb of IAX2). Defaults to fixed.

    ; jblog = no                  ; Enables jitterbuffer frame logging. Defaults to "no".
    ;-----------------------------------------------------------------------------------

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
 *		Overridden by the 'frags' parameter in usbradio.conf
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
 *		Overridden by the 'queuesize' parameter in usbradio.conf
 *
 *		Should be >=2, and at most as large as the hw queue above
 *		(otherwise it will never be full).
 */

#define FRAME_SIZE	160
#define	QUEUE_SIZE	2				

#if defined(__FreeBSD__)
#define	FRAGS	0x8
#else
#define	FRAGS	( ( (6 * 5) << 16 ) | 0xc )
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

static char *config = "usbradio.conf";	/* default config file */
static char *config1 = "usbradio_tune_%s.conf";    /* tune config file */

static FILE *frxcapraw = NULL, *frxcaptrace = NULL, *frxoutraw = NULL;
static FILE *ftxcapraw = NULL, *ftxcaptrace = NULL, *ftxoutraw = NULL;

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

static int usbradio_debug;
#if 0 //maw asdf sph
static int usbradio_debug_level = 0;
#endif

enum {RX_AUDIO_NONE,RX_AUDIO_SPEAKER,RX_AUDIO_FLAT};
enum {CD_IGNORE,CD_XPMR_NOISE,CD_XPMR_VOX,CD_HID,CD_HID_INVERT,CD_PP,CD_PP_INVERT};
enum {SD_IGNORE,SD_HID,SD_HID_INVERT,SD_XPMR,SD_PP,SD_PP_INVERT};		 // no,external,externalinvert,software
enum {RX_KEY_CARRIER,RX_KEY_CARRIER_CODE};
enum {TX_OUT_OFF,TX_OUT_VOICE,TX_OUT_LSD,TX_OUT_COMPOSITE,TX_OUT_AUX};
enum {TOC_NONE,TOC_PHASE,TOC_NOTONE};

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
struct chan_usbradio_pvt {
	struct chan_usbradio_pvt *next;

	char *name;
#ifndef	NEW_ASTERISK
	/*
	 * cursound indicates which in struct sound we play. -1 means nothing,
	 * any other value is a valid sound, in which case sampsent indicates
	 * the next sample to send in [0..samplen + silencelen]
	 * nosound is set to disable the audio data from the channel
	 * (so we can play the tones etc.).
	 */
	int index;				/* index within config file */
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
	i16 cdMethod;
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

	char didpmrtx;
	int notxcnt;
	int stophid;
	FILE *hkickhid;

	struct ast_channel *owner;
	char ext[AST_MAX_EXTENSION];
	char ctx[AST_MAX_CONTEXT];
	char language[MAX_LANGUAGE];
	char cid_name[256];			/*XXX */
	char cid_num[256];			/*XXX */
	char mohinterpret[MAX_MUSICCLASS];

	/* buffers used in usbradio_write, 2 per int by 2 channels by 6 times oversampling (48KS/s) */
	char usbradio_write_buf[FRAME_SIZE * 2 * 2 * 6];    
	char usbradio_write_buf_1[FRAME_SIZE * 2 * 2* 6];

	int usbradio_write_dst;
	/* buffers used in usbradio_read - AST_FRIENDLY_OFFSET space for headers
	 * plus enough room for a full frame
	 */
	char usbradio_read_buf[FRAME_SIZE * (2 * 12) + AST_FRIENDLY_OFFSET];
	char usbradio_read_buf_8k[FRAME_SIZE * 2 + AST_FRIENDLY_OFFSET];
	int readpos;				/* read position above */
	struct ast_frame read_f;	/* returned by usbradio_read */

	char 	debuglevel;
	char 	radioduplex;			// 
	char    wanteeprom;

	int  	tracetype;
	int     tracelevel;
	char    area;
	char 	rptnum;
	int     idleinterval;
	int		turnoffs;
	int  	txsettletime;
	int		txrxblankingtime;
	char    ukey[48];

	char lastrx;
	char rxhidsq;
	char rxhidctcss;
	char rxcarrierdetect;		// status from pmr channel
	char rxctcssdecode;			// status from pmr channel
	char rxppsq;
	char rxppctcss;

	int  rxdcsdecode;
	int  rxlsddecode;

	char rxkeytype;
	char rxkeyed;	  			// indicates rx signal present

	char lasttx;
	char txkeyed;				// tx key request from upper layers 
	char txchankey;
	char txtestkey;

	int rxoncnt;
	int txoffcnt;
	int rxondelay;
	int txoffdelay; // This is the value which RX is ignored after TX Unkey

	time_t lasthidtime;
    struct ast_dsp *dsp;

	t_pmr_chan	*pmrChan;

	char    rxcpusaver;
	char    txcpusaver;

	char	rxdemod;
	float	rxgain;
	char 	rxcdtype;
	char 	rxsdtype;
	int	rxsquelchadj;   /* this copy needs to be here for initialization */
	int	rxsqhyst;
	int     rxsqvoxadj;
	int	rxnoisefiltype;
	int	rxsquelchdelay;
	char	txtoctype;

	char    txprelim;
	char    txlimonly;
	float	txctcssgain;
	char 	txmixa;
	char 	txmixb;
    int		rxlpf;
    int		rxhpf;
    int		txlpf;
    int		txhpf;

	char	invertptt;

	char	rxctcssrelax;
	float	rxctcssgain;

	char    txctcssdefault[16];				// for repeater operation
	char	rxctcssfreqs[512];				// a string
	char    txctcssfreqs[512];

	char	txctcssfreq[32];				// encode now
	char	rxctcssfreq[32];				// decode now

	char    numrxctcssfreqs;	  			// how many
	char    numtxctcssfreqs;

	char    *rxctcss[CTCSS_NUM_CODES]; 		// pointers to strings
	char    *txctcss[CTCSS_NUM_CODES];

	int   	txfreq;			 				// in Hz
	int     rxfreq;

	// 		start remote operation info
	char    set_txctcssdefault[16];				// for remote operation
	char	set_txctcssfreq[16];				// encode now
	char	set_rxctcssfreq[16];				// decode now

	char    set_numrxctcssfreqs;	  			// how many
	char    set_numtxctcssfreqs;

	char	set_rxctcssfreqs[16];				// a string
	char    set_txctcssfreqs[16];

	char    *set_rxctcss; 					    // pointers to strings
	char    *set_txctcss;

	int   	set_txfreq;			 				// in Hz
	int     set_rxfreq;

	int	rxctcssoverride;
	// 		end remote operation info

	int	   	rxmixerset;	   	
	int 	rxboostset;
	int	txboostset;
	float	rxvoiceadj;
	float	rxctcssadj;
	int 	txmixaset;
	int 	txmixbset;
	int     txctcssadj;

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

	int	fever;

	int     count_rssi_update;
	char	newname;

	struct {
	    unsigned rxcapraw:1;
		unsigned txcapraw:1;
		unsigned txcap2:1;
		unsigned rxcap2:1;
		unsigned rxplmon:1;
		unsigned remoted:1;
		unsigned forcetxcode:1;
		unsigned txpolarity:1;
		unsigned rxpolarity:1;
		unsigned dcstxpolarity:1;
		unsigned dcsrxpolarity:1;
		unsigned lsdtxpolarity:1;
		unsigned lsdrxpolarity:1;
		unsigned loopback:1;
		unsigned radioactive:1;
	}b;
	unsigned short eeprom[EEPROM_PHYSICAL_LEN];
	char eepromctl;
	ast_mutex_t eepromlock;

	struct usb_dev_handle *usb_handle;
	int readerrs;
	char hasusb;
	char usbass;
	struct timeval tonetime;
	int toneflag;
	int duplex3;
	int32_t cur_gpios;
	char *gpios[32];
	char *pps[32];
	int sendvoter;
	ast_mutex_t usblock;
};

// maw add additional defaults !!!
static struct chan_usbradio_pvt usbradio_default = {
#ifndef	NEW_ASTERISK
	.cursound = -1,
#endif
	.sounddev = -1,
	.duplex = M_UNSET,			/* XXX check this */
	.autoanswer = 1,
	.autohangup = 1,
	.queuesize = QUEUE_SIZE,
	.frags = FRAGS,
	.ext = "s",
	.ctx = "default",
	.readpos = AST_FRIENDLY_OFFSET,	/* start here on reads */
	.lastopen = { 0, 0 },
	.boost = BOOST_SCALE,
	.wanteeprom = 1,
	.area = 0,
	.rptnum = 0,
	.usedtmf = 1,
	.rxondelay = 0,
	.txoffdelay = 0,
};

/*	DECLARE FUNCTION PROTOTYPES	*/

static void store_rxvoiceadj(struct chan_usbradio_pvt *o, char *s);
static void store_rxctcssadj(struct chan_usbradio_pvt *o, char *s);
static void store_txtoctype(struct chan_usbradio_pvt *o, char *s);
static int	hidhdwconfig(struct chan_usbradio_pvt *o);
static int set_txctcss_level(struct chan_usbradio_pvt *o);
static void pmrdump(struct chan_usbradio_pvt *o);
static void mult_set(struct chan_usbradio_pvt *o);
static int  mult_calc(int value);
static void mixer_write(struct chan_usbradio_pvt *o);
static void tune_rxinput(int fd, struct chan_usbradio_pvt *o, int setsql, int flag);
static void tune_rxvoice(int fd, struct chan_usbradio_pvt *o, int flag);
static void tune_menusupport(int fd, struct chan_usbradio_pvt *o, char *cmd);
static void tune_rxctcss(int fd, struct chan_usbradio_pvt *o, int flag);
static void tune_txoutput(struct chan_usbradio_pvt *o, int value, int fd, int flag);
static void tune_write(struct chan_usbradio_pvt *o);

static char *usbradio_active;	 /* the active device */

static int setformat(struct chan_usbradio_pvt *o, int mode);

static struct ast_channel *usbradio_request(const char *type, int format, void *data
, int *cause);
static int usbradio_digit_begin(struct ast_channel *c, char digit);
static int usbradio_digit_end(struct ast_channel *c, char digit, unsigned int duration);
static int usbradio_text(struct ast_channel *c, const char *text);
static int usbradio_hangup(struct ast_channel *c);
static int usbradio_answer(struct ast_channel *c);
static struct ast_frame *usbradio_read(struct ast_channel *chan);
static int usbradio_call(struct ast_channel *c, char *dest, int timeout);
static int usbradio_write(struct ast_channel *chan, struct ast_frame *f);
static int usbradio_indicate(struct ast_channel *chan, int cond, const void *data, size_t datalen);
static int usbradio_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int usbradio_setoption(struct ast_channel *chan, int option, void *data, int datalen);
static int xpmr_config(struct chan_usbradio_pvt *o);

#if	DEBUG_FILETEST == 1
static int RxTestIt(struct chan_usbradio_pvt *o);
#endif

int	ppinshift[] = {0,0,0,0,0,0,0,0,0,0,6,7,5,4,0,3};

static char tdesc[] = "USB (CM108) Radio Channel Driver";

static const struct ast_channel_tech usbradio_tech = {
	.type = "Radio",
	.description = tdesc,
	.capabilities = AST_FORMAT_SLINEAR,
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

/* lround for uClibc
 *
 * wrapper for lround(x)
 */
long lround(double x)
{
    return (long) ((x - ((long)x) >= 0.5f) ? (((long)x) + 1) : ((long)x));
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

static int make_spkr_playback_value(struct chan_usbradio_pvt *o,int val)
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
    char devstr[10000],str[200],desdev[200],*cp;
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
				if (i) sprintf(str,"/sys/class/sound/dsp%d/device",i);
				else strcpy(str,"/sys/class/sound/dsp/device");
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
    char devstr[10000],str[200],desdev[200],*cp;
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
				if (i) sprintf(str,"/sys/class/sound/dsp%d/device",i);
				else strcpy(str,"/sys/class/sound/dsp/device");
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
		if (i) sprintf(str,"/sys/class/sound/dsp%d/device",i);
		else strcpy(str,"/sys/class/sound/dsp/device");
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


static int	hidhdwconfig(struct chan_usbradio_pvt *o)
{
int	i;

/* NOTE: on the CM-108AH, GPIO2 is *not* a REAL GPIO.. it was re-purposed
   as a signal called "HOOK" which can only be read from the HID.
   Apparently, in a REAL CM-108, GPIO really works as a GPIO */


	if(o->hdwtype==1)	  //sphusb
	{
		o->hid_gpio_ctl		=  8;	/* set GPIO4 to output mode */
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
		o->hid_gpio_ctl		=  4;	/* set GPIO 3 to output mode */
		o->hid_gpio_ctl_loc	=  2; 	/* For CTL of GPIO */
		o->hid_io_cor		=  2;	/* VOLD DN is COR */
		o->hid_io_cor_loc	=  0;	/* VOL DN COR */
		o->hid_io_ctcss		=  1;  	/* VOL UP is External CTCSS */
		o->hid_io_ctcss_loc 	=  0;	/* VOL UP CTCSS */
		o->hid_io_ptt 		=  4;  	/* GPIO 3 is PTT */
		o->hid_gpio_loc 	=  1;  	/* For ALL GPIO */
		o->valid_gpios		=  0xfb;   /* for GPIO 1,2,4,5,6,7,8 (5,6,7,8 for CM-119 only) */
	}
	else if(o->hdwtype==2)	//NHRC (N1KDO) (dudeusb w/o user GPIO)
	{
		o->hid_gpio_ctl		=  4;	/* set GPIO 3 to output mode */
		o->hid_gpio_ctl_loc	=  2; 	/* For CTL of GPIO */
		o->hid_io_cor		=  2;	/* VOLD DN is COR */
		o->hid_io_cor_loc	=  0;	/* VOL DN COR */
		o->hid_io_ctcss		=  1;  	/* VOL UP is External CTCSS */
		o->hid_io_ctcss_loc 	=  0;	/* VOL UP CTCSS */
		o->hid_io_ptt 		=  4;  	/* GPIO 3 is PTT */
		o->hid_gpio_loc 	=  1;  	/* For ALL GPIO */
		o->valid_gpios		=  0;   /* for GPIO 1,2,4 */
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
static void kickptt(struct chan_usbradio_pvt *o)
{
	char c = 0;
	//printf("kickptt  %i  %i  %i\n",o->txkeyed,o->txchankey,o->txtestkey);
	if (!o) return;
	if (!o->pttkick) return;
	write(o->pttkick[1],&c,1);
}

/*
 * returns a pointer to the descriptor with the given name
 */
static struct chan_usbradio_pvt *find_desc(char *dev)
{
	struct chan_usbradio_pvt *o = NULL;

	if (!dev)
		ast_log(LOG_WARNING, "null dev\n");


	for (o = usbradio_default.next; o && o->name && dev && strcmp(o->name, dev) != 0; o = o->next);
	if (!o)
	{
		ast_log(LOG_WARNING, "could not find <%s>\n", dev ? dev : "--no-device--");
		return NULL;
	}
	return o;
}

static struct chan_usbradio_pvt *find_desc_usb(char *devstr)
{
	struct chan_usbradio_pvt *o = NULL;

	if (!devstr)
		ast_log(LOG_WARNING, "null dev\n");

	for (o = usbradio_default.next; o && devstr && strcmp(o->devstr, devstr) != 0; o = o->next);

	return o;
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
	unsigned char buf[4],bufsave[4],keyed;
	char txtmp, fname[200], *s;
	int i,j,k,res;
	struct usb_device *usb_dev;
	struct usb_dev_handle *usb_handle;
	struct chan_usbradio_pvt *o = (struct chan_usbradio_pvt *) arg,*ao,**aop;
	struct timeval to,then;
	struct ast_config *cfg1;
	struct ast_variable *v;
	fd_set rfds;

        usb_dev = NULL;
        usb_handle = NULL;

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
		for(s = usb_device_list; *s; s += strlen(s) + 1)
		{
			i = usb_get_usbdev(s);
			if (i < 0) continue;
			usb_dev = hid_device_init(s);
			if (usb_dev == NULL) continue;
			if ((usb_dev->descriptor.idProduct & 0xff00) != N1KDO_PRODUCT_ID) continue;
			if (o->index != (usb_dev->descriptor.idProduct & 0xf)) continue;
			ast_log(LOG_NOTICE,"N1KDO port %d, USB device %s usbradio channel %s\n",
				usb_dev->descriptor.idProduct & 0xf,s,o->name);
			strcpy(o->devstr,s);
		}
		/* if our specified one exists in the list */
		if ((!usb_list_check(o->devstr)) || (!find_desc_usb(o->devstr)))
		{
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
			i = usb_get_usbdev(s);
			if (i < 0)
			{
				ast_mutex_unlock(&usb_dev_lock);
				usleep(500000);
				continue;
			}
			for (ao = usbradio_default.next; ao && ao->name ; ao = ao->next)
			{
				if (ao->usbass && (!strcmp(ao->devstr,s))) break;
			}
			if (ao)
			{
				ast_mutex_unlock(&usb_dev_lock);
				usleep(500000);
				continue;
			}
			ast_log(LOG_NOTICE,"Assigned USB device %s to usbradio channel %s\n",s,o->name);
			strcpy(o->devstr,s);
		}
		i = usb_get_usbdev(o->devstr);
		if (i < 0)
		{
			ast_mutex_unlock(&usb_dev_lock);
			usleep(500000);
			continue;
		}
		o->devicenum = i;
		for (aop = &usbradio_default.next; *aop && (*aop)->name ; aop = &((*aop)->next))
		{
			if (strcmp((*(aop))->name,o->name)) continue;
			o->next = (*(aop))->next;
			*aop = o;
			break;
		}
		o->usbass = 1;
		usb_dev = hid_device_init(o->devstr);
		if (usb_dev == NULL) {
			usleep(500000);
			ast_mutex_unlock(&usb_dev_lock);
			continue;
		}
		ast_mutex_unlock(&usb_dev_lock);

		o->micmax = amixer_max(o->devicenum,MIXER_PARAM_MIC_CAPTURE_VOL);
		o->spkrmax = amixer_max(o->devicenum,MIXER_PARAM_SPKR_PLAYBACK_VOL);
		if (o->spkrmax == -1) 
		{
			o->newname = 1;
			o->spkrmax = amixer_max(o->devicenum,MIXER_PARAM_SPKR_PLAYBACK_VOL_NEW);
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
		buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
		buf[o->hid_gpio_loc] = o->hid_gpio_val;
		hid_set_outputs(usb_handle,buf);
		memcpy(bufsave,buf,sizeof(buf));
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
		{
			ast_mutex_lock(&usb_dev_lock);
			ast_verbose(VERBOSE_PREFIX_2 "Set device %s to %s\n",o->devstr,o->name);
			ast_mutex_unlock(&usb_dev_lock);
		}
		if(o->pmrChan==NULL)
		{
			t_pmr_chan tChan;

			// ast_log(LOG_NOTICE,"createPmrChannel() %s\n",o->name);
			memset(&tChan,0,sizeof(t_pmr_chan));

			tChan.pTxCodeDefault = o->txctcssdefault;
			tChan.pRxCodeSrc     = o->rxctcssfreqs;
			tChan.pTxCodeSrc     = o->txctcssfreqs;

			tChan.rxDemod=o->rxdemod;
			tChan.rxCdType=o->rxcdtype;
			tChan.rxSqVoxAdj=o->rxsqvoxadj;

			if (o->txlimonly) 
				tChan.txMod = 1;
			if (o->txprelim) 
				tChan.txMod = 2;

			tChan.txMixA = o->txmixa;
			tChan.txMixB = o->txmixb;

			tChan.rxCpuSaver=o->rxcpusaver;
			tChan.txCpuSaver=o->txcpusaver;

			tChan.b.rxpolarity=o->b.rxpolarity;
			tChan.b.txpolarity=o->b.txpolarity;

			tChan.b.dcsrxpolarity=o->b.dcsrxpolarity;
			tChan.b.dcstxpolarity=o->b.dcstxpolarity;

			tChan.b.lsdrxpolarity=o->b.lsdrxpolarity;
			tChan.b.lsdtxpolarity=o->b.lsdtxpolarity;

			tChan.tracetype=o->tracetype;
			tChan.tracelevel=o->tracelevel;

			tChan.rptnum=o->rptnum;
			tChan.idleinterval=o->idleinterval;
			tChan.turnoffs=o->turnoffs;
			tChan.area=o->area;
			tChan.ukey=o->ukey;
			tChan.name=o->name;
			tChan.b.txboost = o->txboostset;
			tChan.fever = o->fever;

			o->pmrChan=createPmrChannel(&tChan,FRAME_SIZE);
										 
			o->pmrChan->radioDuplex=o->radioduplex;
			o->pmrChan->b.loopback=0; 
			o->pmrChan->b.radioactive=o->b.radioactive;
			o->pmrChan->txsettletime=o->txsettletime;
			o->pmrChan->txrxblankingtime=o->txrxblankingtime;
			o->pmrChan->rxCpuSaver=o->rxcpusaver;
			o->pmrChan->txCpuSaver=o->txcpusaver;

			*(o->pmrChan->prxSquelchAdjust) = 
				((999 - o->rxsquelchadj) * 32767) / 1000;

			*(o->pmrChan->prxVoiceAdjust)=o->rxvoiceadj*M_Q8;
			*(o->pmrChan->prxCtcssAdjust)=o->rxctcssadj*M_Q8;
			o->pmrChan->rxCtcss->relax=o->rxctcssrelax;
			o->pmrChan->txTocType = o->txtoctype;

		        if (    (o->txmixa == TX_OUT_LSD) ||
		                (o->txmixa == TX_OUT_COMPOSITE) ||
		                (o->txmixb == TX_OUT_LSD) ||
		                (o->txmixb == TX_OUT_COMPOSITE))
			        {
		                set_txctcss_level(o);
		        }

			if( (o->txmixa!=TX_OUT_VOICE) && (o->txmixb!=TX_OUT_VOICE) &&
				(o->txmixa!=TX_OUT_COMPOSITE) && (o->txmixb!=TX_OUT_COMPOSITE)
			  )
			{
				ast_log(LOG_ERROR,"No txvoice output configured.\n");
			}
		
			if( o->txctcssfreq[0] && 
			    o->txmixa!=TX_OUT_LSD && o->txmixa!=TX_OUT_COMPOSITE  &&
				o->txmixb!=TX_OUT_LSD && o->txmixb!=TX_OUT_COMPOSITE
			  )
			{
				ast_log(LOG_ERROR,"No txtone output configured.\n");
			}
			
			if(o->b.radioactive)
			{
				struct chan_usbradio_pvt *ao;
				for (ao = usbradio_default.next; ao && ao->name ; ao = ao->next)ao->pmrChan->b.radioactive=0;
				usbradio_active = o->name;
				o->pmrChan->b.radioactive=1;
				ast_log(LOG_NOTICE,"radio active set to [%s]\n",o->name);
			}
		}
		xpmr_config(o);
		mixer_write(o);
		mult_set(o);    

		snprintf(fname,sizeof(fname) - 1,config1,o->name);
#ifdef	NEW_ASTERISK
		cfg1 = ast_config_load(fname,zeroflag);
#else
		cfg1 = ast_config_load(fname);
#endif
		o->rxmixerset = 500;
		o->txmixaset = 500;
		o->txmixbset = 500;
		o->rxvoiceadj = 0.5;
		o->rxctcssadj = 0.5;
		o->txctcssadj = 200;
		o->rxsquelchadj = 500;
		if (cfg1) {
			for (v = ast_variable_browse(cfg1, o->name); v; v = v->next) {
	
				M_START((char *)v->name, (char *)v->value);
				M_UINT("rxmixerset", o->rxmixerset)
				M_UINT("txmixaset", o->txmixaset)
				M_UINT("txmixbset", o->txmixbset)
				M_F("rxvoiceadj",store_rxvoiceadj(o,(char *)v->value))
				M_F("rxctcssadj",store_rxctcssadj(o,(char *)v->value))
				M_UINT("txctcssadj",o->txctcssadj);
				M_UINT("rxsquelchadj", o->rxsquelchadj)
				M_UINT("fever", o->fever)
				M_END(;
				);
			}
			ast_config_destroy(cfg1);
			ast_log(LOG_WARNING,"Loaded parameters from %s for device %s .\n",fname,o->name);
		} else ast_log(LOG_WARNING,"File %s not found, device %s using default parameters.\n",fname,o->name);

		mixer_write(o);
		mult_set(o);    
		set_txctcss_level(o);
		ast_mutex_lock(&o->eepromlock);
		if (o->wanteeprom) o->eepromctl = 1;
		ast_mutex_unlock(&o->eepromlock);
                setformat(o,O_RDWR);
                o->hasusb = 1;
		o->had_gpios_in = 0;
 		// popen 
		while((!o->stophid) && o->hasusb)
		{
			to.tv_sec = 0;
			to.tv_usec = 50000;   // maw sph

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
							memcpy(&o->rxvoiceadj,&o->eeprom[EEPROM_RXVOICEADJ],sizeof(float));
							memcpy(&o->rxctcssadj,&o->eeprom[EEPROM_RXCTCSSADJ],sizeof(float));
							o->txctcssadj = o->eeprom[EEPROM_TXCTCSSADJ];
							o->rxsquelchadj = o->eeprom[EEPROM_RXSQUELCHADJ];
							ast_log(LOG_NOTICE,"EEPROM Loaded on channel %s\n",o->name);
							mixer_write(o);
							mult_set(o);    
							set_txctcss_level(o);
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
				if(o->debuglevel)printf("chan_usbradio() hidthread: update rxhidsq = %d\n",keyed);
				o->rxhidsq=keyed;
			}
			o->rxhidctcss = 
				!(buf[o->hid_io_ctcss_loc] & o->hid_io_ctcss);
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
						fr.src = "chan_usbradio";
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
						fr.src = "chan_usbradio";
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
						if(o->debuglevel)printf("chan_usbradio() hidthread: update rxppsq = %d\n",j);
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
			/* if change in tx state as controlled by xpmr */
			txtmp=o->pmrChan->txPttOut;
					
			k = 0;
			for(i = 2; i <= 9; i++)
			{
				/* skip if this one not specified */
				if (!o->pps[i]) continue;
				/* skip if not ptt */
				if (strncasecmp(o->pps[i],"ptt",3)) continue;
				k |= (1 << (i - 2)); /* make mask */
			}
			if (o->lasttx != txtmp)
			{
				o->pmrChan->txPttHid=o->lasttx = txtmp;
				if(o->debuglevel)printf("hidthread: tx set to %d\n",txtmp);
				o->hid_gpio_val &= ~o->hid_io_ptt;
				ast_mutex_lock(&pp_lock);
				if (k) pp_val &= ~k;
				if (!o->invertptt)
				{
					if (txtmp) 
					{
						buf[o->hid_gpio_loc] = o->hid_gpio_val |= o->hid_io_ptt;
						if (k) pp_val |= k;
					}
				}
				else
				{
					if (!txtmp)
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
			time(&o->lasthidtime);
			ast_mutex_unlock(&o->usblock);
		}
		txtmp=o->pmrChan->txPttOut = 0;
		o->lasttx = 0;
		ast_mutex_lock(&o->usblock);
		o->hid_gpio_val = ~o->hid_io_ptt;
		if (o->invertptt) o->hid_gpio_val |= o->hid_io_ptt;
		buf[o->hid_gpio_loc] = o->hid_gpio_val ^ o->hid_gpio_pulsemask;
		buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
		hid_set_outputs(usb_handle,buf);
		ast_mutex_unlock(&o->usblock);
	}
	txtmp=o->pmrChan->txPttOut = 0;
	o->lasttx = 0;
        if (usb_handle)
        {
		ast_mutex_lock(&o->usblock);
		o->hid_gpio_val = ~o->hid_io_ptt;
		if (o->invertptt) o->hid_gpio_val |= o->hid_io_ptt;
		buf[o->hid_gpio_loc] = o->hid_gpio_val;
		buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
		hid_set_outputs(usb_handle,buf);
		ast_mutex_lock(&o->usblock);
        }
        pthread_exit(0);
}

/*
 * split a string in extension-context, returns pointers to malloc'ed
 * strings.
 * If we do not have 'overridecontext' then the last @ is considered as
 * a context separator, and the context is overridden.
 * This is usually not very necessary as you can play with the dialplan,
 * and it is nice not to need it because you have '@' in SIP addresses.
 * Return value is the buffer address.
 */
#if	0
static char *ast_ext_ctx(const char *src, char **ext, char **ctx)
{
	struct chan_usbradio_pvt *o = find_desc(usbradio_active);

	if (o == NULL || ext == NULL || ctx == NULL)
		return NULL;			/* error */

	*ext = *ctx = NULL;

	if (src && *src != '\0')
		*ext = ast_strdup(src);

	if (*ext == NULL)
		return NULL;

	if (!o->overridecontext) {
		/* parse from the right */
		*ctx = strrchr(*ext, '@');
		if (*ctx)
			*(*ctx)++ = '\0';
	}

	return *ext;
}
#endif

/*
 * Returns the number of blocks used in the audio output channel
 */
static int used_blocks(struct chan_usbradio_pvt *o)
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
static int soundcard_writeframe(struct chan_usbradio_pvt *o, short *data)
{
	int res;

	if (o->sounddev < 0)
		setformat(o, O_RDWR);
	if (o->sounddev < 0)
		return 0;				/* not fatal */
	//  maw maw sph !!! may or may not be a good thing
	//  drop the frame if not transmitting, this keeps from gradually
	//  filling the buffer when asterisk clock > usb sound clock
	if(!o->pmrChan->txPttIn && !o->pmrChan->txPttOut)
	{
		//return 0;
	}
	/*
	 * Nothing complex to manage the audio device queue.
	 * If the buffer is full just drop the extra, otherwise write.
	 * XXX in some cases it might be useful to write anyways after
	 * a number of failures, to restart the output chain.
	 */
	res = used_blocks(o);
	if (res > o->queuesize) {	/* no room to write a block */
	    // ast_log(LOG_WARNING, "sound device write buffer overflow\n");
		if (o->w_errors++ == 0 && (usbradio_debug & 0x4))
			ast_log(LOG_WARNING, "write: used %d blocks (%d)\n", res, o->w_errors);
		return 0;
	}
	o->w_errors = 0;

	return write(o->sounddev, ((void *) data), FRAME_SIZE * 2 * 12);
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
static void send_sound(struct chan_usbradio_pvt *o)
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
	struct chan_usbradio_pvt *o = (struct chan_usbradio_pvt *) arg;

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
static int setformat(struct chan_usbradio_pvt *o, int mode)
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
		ast_log(LOG_WARNING, "Failed to set audio device to mono\n");
		return -1;
	}
	fmt = desired = 48000;							/* 8000 Hz desired */
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
static int usbradio_digit_begin(struct ast_channel *c, char digit)
{
	return 0;
}

static int usbradio_digit_end(struct ast_channel *c, char digit, unsigned int duration)
{
	/* no better use for received digits than print them */
	ast_verbose(" << Console Received digit %c of duration %u ms >> \n", 
		digit, duration);
	return 0;
}
/*
	SETFREQ - sets spi programmable xcvr
	SETCHAN - sets binary parallel xcvr
*/
static int usbradio_text(struct ast_channel *c, const char *text)
{
	struct chan_usbradio_pvt *o = c->tech_pvt;
	double tx,rx;
	char cnt,rxs[16],txs[16],txpl[16],rxpl[16];
	char pwr,*cmd;

	if (haspp == 2) ioperm(pbase,2,1);

	cmd = alloca(strlen(text) + 10);

	/* print received messages */
	if(o->debuglevel)ast_verbose(" << Console Received usbradio text %s >> \n", text);

	cnt=sscanf(text,"%s %s %s %s %s %c",cmd,rxs,txs,rxpl,txpl,&pwr);

    if (strcmp(cmd,"SETCHAN")==0)
    { 
		u8 chan;
		chan=strtod(rxs,NULL);
		ppbinout(chan);
        if(o->debuglevel)ast_log(LOG_NOTICE,"parse usbradio SETCHAN cmd: %s chan: %i\n",text,chan);
        return 0;
    }
	
    if (strcmp(cmd,"RXCTCSS")==0)
    {
	u8 x;
	x = strtod(rxs,NULL);
	o->rxctcssoverride = !x;
        if(o->debuglevel)ast_log(LOG_NOTICE,"parse usbradio RXCTCSS cmd: %s\n",text);
	return 0;		
    }

    if (strcmp(cmd,"TXCTCSS")==0)
    {
	u8 x;
	x = strtod(rxs,NULL);
	if (o && o->pmrChan)
		o->pmrChan->b.txCtcssOff=!x;
        if(o->debuglevel) ast_log(LOG_NOTICE,"parse usbradio TXCTCSS cmd: %s\n",text);
	return 0;		
    }

    if (strcmp(cmd,"GPIO")==0)
    {
	int i,j;

	cnt=sscanf(text,"%s %d %d",cmd,&i,&j);
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

    if (strcmp(cmd,"PP")==0)
    {
	int i,j;

	cnt=sscanf(text,"%s %d %d",cmd,&i,&j);
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

    if (cnt < 6)
    {
	    ast_log(LOG_ERROR,"Cannot parse usbradio text: %s\n",text);
	    return 0;
    }
	else
	{
		if(o->debuglevel)ast_verbose(" << %s %s %s %s %s %c >> \n", cmd,rxs,txs,rxpl,txpl,pwr);	
	}
    
    if (strcmp(cmd,"SETFREQ")==0)
    {
        if(o->debuglevel)ast_log(LOG_NOTICE,"parse usbradio SETFREQ cmd: %s\n",text);
		tx=strtod(txs,NULL);
		rx=strtod(rxs,NULL);
		o->set_txfreq = round(tx * (double)1000000);
		o->set_rxfreq = round(rx * (double)1000000);
		o->pmrChan->txpower = (pwr == 'H');
		strcpy(o->set_rxctcssfreqs,rxpl);
		strcpy(o->set_txctcssfreqs,txpl);
	
		o->b.remoted=1;
		xpmr_config(o);
        return 0;
    }
	ast_log(LOG_ERROR,"Cannot parse usbradio cmd: %s\n",text);
	return 0;
}

/* Play ringtone 'x' on device 'o' */
static void ring(struct chan_usbradio_pvt *o, int x)
{
#ifndef	NEW_ASTERISK
	write(o->sndcmd[1], &x, sizeof(x));
#endif
}

/*
 * handler for incoming calls. Either autoanswer, or start ringing
 */
static int usbradio_call(struct ast_channel *c, char *dest, int timeout)
{
	struct chan_usbradio_pvt *o = c->tech_pvt;

	o->stophid = 0;
	time(&o->lasthidtime);
	ast_pthread_create_background(&o->hidthread, NULL, hidthread, o);
	ast_setstate(c, AST_STATE_UP);
	return 0;
}

/*
 * remote side answered the phone
 */
static int usbradio_answer(struct ast_channel *c)
{
#ifndef	NEW_ASTERISK
	struct chan_usbradio_pvt *o = c->tech_pvt;
#endif

	ast_setstate(c, AST_STATE_UP);
#ifndef	NEW_ASTERISK
	o->cursound = -1;
	o->nosound = 0;
#endif
	return 0;
}

static int usbradio_hangup(struct ast_channel *c)
{
	struct chan_usbradio_pvt *o = c->tech_pvt;

	//ast_log(LOG_NOTICE, "usbradio_hangup()\n");
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
static int usbradio_write(struct ast_channel *c, struct ast_frame *f)
{
	struct chan_usbradio_pvt *o = c->tech_pvt;

	traceusb2(("usbradio_write() o->nosound= %i\n",o->nosound));

#ifndef	NEW_ASTERISK
	/* Immediately return if no sound is enabled */
	if (o->nosound)
		return 0;
	/* Stop any currently playing sound */
	o->cursound = -1;
#endif
	if (!o->hasusb) return 0;
	/*
	 * we could receive a block which is not a multiple of our
	 * FRAME_SIZE, so buffer it locally and write to the device
	 * in FRAME_SIZE chunks.
	 * Keep the residue stored for future use.
	 */

	#if DEBUG_CAPTURES == 1	// to write input data to a file   datalen=320
	if (ftxcapraw && o->b.txcapraw)
	{
		i16 i, tbuff[f->datalen];
		for(i=0;i<f->datalen;i+=2)
		{
			tbuff[i]= ((i16*)(f->data))[i/2];
			tbuff[i+1]= o->txkeyed*M_Q13;
		}
		fwrite(tbuff,2,f->datalen,ftxcapraw);
		//fwrite(f->data,1,f->datalen,ftxcapraw);
	}
	#endif

	// maw just take the data from the network and save it for PmrRx processing

	if (!o->echoing)
	{
		PmrTx(o->pmrChan,(i16*)f->data);
		o->didpmrtx = 1;
	}
	return 0;
}

static struct ast_frame *usbradio_read(struct ast_channel *c)
{
	int res, src, datalen, oldpttout;
	int cd,sd;
	struct chan_usbradio_pvt *o = c->tech_pvt;
	struct ast_frame *f = &o->read_f,*f1;
	struct ast_frame wf = { AST_FRAME_CONTROL };
	time_t now;

	traceusb2(("usbradio_read()\n"));

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
	f->src = usbradio_tech.type;

        /* if USB device not ready, just return NULL frame */
        if (!o->hasusb)
        {
		if (o->rxkeyed)
		{
			o->lastrx = 0;
			o->rxkeyed = 0;
			// printf("AST_CONTROL_RADIO_UNKEY\n");
			wf.subclass = AST_CONTROL_RADIO_UNKEY;
			ast_queue_frame(o->owner, &wf);
			if (o->duplex3)
				setamixer(o->devicenum,MIXER_PARAM_MIC_PLAYBACK_SW,0,0);
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
			PmrTx(o->pmrChan,u->data);
			o->didpmrtx = 1;
			ast_free(u);
			o->echoing = 1;
		} else o->echoing = 0;
		ast_mutex_unlock(&o->echolock);
	}

	res = read(o->sounddev, o->usbradio_read_buf + o->readpos, 
		sizeof(o->usbradio_read_buf) - o->readpos);
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
        if (o->readerrs) ast_log(LOG_WARNING,"Nope, USB read channel [%s] wasn't stuck after all.\n",o->name);
	if (o->devtype != C108_PRODUCT_ID)
	{
		short *sp = (short *)  (o->usbradio_read_buf + o->readpos);
 		float v;
		int i;
		
		for(i = 0; i < res / 2; i++)
		{
			v = ((float)*sp) * 0.800;
			*sp++ = (int) v;
		}
	}
	o->readerrs = 0;
	o->readpos += res;
	if (o->readpos < sizeof(o->usbradio_read_buf))	/* not enough samples */
		return f;

	if (o->mute)
		return f;

	#if DEBUG_CAPTURES == 1
	if (o->b.rxcapraw && frxcapraw) fwrite((o->usbradio_read_buf + AST_FRIENDLY_OFFSET),1,FRAME_SIZE * 2 * 2 * 6,frxcapraw);
	#endif

	#if 1
	if(o->txkeyed || o->txtestkey || o->echoing)
	{
		if(!o->pmrChan->txPttIn)
		{
			o->pmrChan->txPttIn=1;
			if(o->debuglevel) ast_log(LOG_NOTICE,"txPttIn = %i, chan %s\n",o->pmrChan->txPttIn,o->owner->name);
		}
	}
	else if(o->pmrChan->txPttIn)
	{
		o->pmrChan->txPttIn=0;
		if(o->debuglevel) ast_log(LOG_NOTICE,"txPttIn = %i, chan %s\n",o->pmrChan->txPttIn,o->owner->name);
	}
	oldpttout = o->pmrChan->txPttOut;

	if (oldpttout && (!o->didpmrtx)) 
	{
		if (o->notxcnt > 1)
		{
			memset(o->usbradio_write_buf_1,0,sizeof(o->usbradio_write_buf_1));
			PmrTx(o->pmrChan,(i16*)o->usbradio_write_buf_1);
		} else o->notxcnt++;
	} else o->notxcnt = 0;
	o->didpmrtx = 0;

	PmrRx(         o->pmrChan, 
		   (i16 *)(o->usbradio_read_buf + AST_FRIENDLY_OFFSET),
		   (i16 *)(o->usbradio_read_buf_8k + AST_FRIENDLY_OFFSET),
		   (i16 *)(o->usbradio_write_buf_1));

	if (oldpttout != o->pmrChan->txPttOut)
	{
		if(o->debuglevel) ast_log(LOG_NOTICE,"txPttOut = %i, chan %s\n",o->pmrChan->txPttOut,o->owner->name);
		kickptt(o);
	}

	#if 0	// to write 48KS/s stereo tx data to a file
	if (!ftxoutraw) ftxoutraw = fopen(TX_CAP_OUT_FILE,"w");
	if (ftxoutraw) fwrite(o->usbradio_write_buf_1,1,FRAME_SIZE * 2 * 6,ftxoutraw);
	#endif

	#if DEBUG_CAPTURES == 1	&& XPMR_DEBUG0 == 1
    if (o->b.txcap2 && ftxcaptrace) fwrite((o->pmrChan->ptxDebug),1,FRAME_SIZE * 2 * 16,ftxcaptrace);
	#endif
	
	// 160 samples * 2 bytes/sample * 2 chan * 6x oversampling to 48KS/s
	datalen = FRAME_SIZE * 24;  
	src = 0;					/* read position into f->data */
	while (src < datalen) 
	{
		/* Compute spare room in the buffer */
		int l = sizeof(o->usbradio_write_buf) - o->usbradio_write_dst;

		if (datalen - src >= l) 
		{	
			/* enough to fill a frame */
			memcpy(o->usbradio_write_buf + o->usbradio_write_dst, o->usbradio_write_buf_1 + src, l);
			if (o->devtype != C108_PRODUCT_ID)
			{
				short *sp = (short *)  o->usbradio_write_buf;
				float v;
				int i;
		
				for(i = 0; i < l / 2; i++)
				{
					v = ((float)*sp) * 1.10;
					if (v > 32765.0) v = 32765.0;
					if (v < -32765.0) v = -32765.0;
					*sp++ = (int) v;
				}
			}
			soundcard_writeframe(o, (short *) o->usbradio_write_buf);
			src += l;
			o->usbradio_write_dst = 0;
		} 
		else 
		{				
			/* copy residue */
			l = datalen - src;
			memcpy(o->usbradio_write_buf + o->usbradio_write_dst, o->usbradio_write_buf_1 + src, l);
			src += l;			/* but really, we are done */
			o->usbradio_write_dst += l;
		}
	}
	#else
	static FILE *hInput;
	i16 iBuff[FRAME_SIZE*2*6];

	o->pmrChan->b.rxCapture=1;

	if(!hInput)
	{
		hInput  = fopen("/usr/src/xpmr/testdata/rx_in.pcm","r");
		if(!hInput)
		{
			printf(" Input Data File Not Found.\n");
			return 0;
		}
	}

	if(0==fread((void *)iBuff,2,FRAME_SIZE*2*6,hInput))exit;

	PmrRx(  o->pmrChan, 
		   (i16 *)iBuff,
		   (i16 *)(o->usbradio_read_buf_8k + AST_FRIENDLY_OFFSET));

	#endif

	#if 0
	if (!frxoutraw) frxoutraw = fopen(RX_CAP_OUT_FILE,"w");
    if (frxoutraw) fwrite((o->usbradio_read_buf_8k + AST_FRIENDLY_OFFSET),1,FRAME_SIZE * 2,frxoutraw);
	#endif

	#if DEBUG_CAPTURES == 1 && XPMR_DEBUG0 == 1
    if (frxcaptrace && o->b.rxcap2 && o->pmrChan->b.radioactive) fwrite((o->pmrChan->prxDebug),1,FRAME_SIZE * 2 * 16,frxcaptrace);
	#endif

	cd = 0;
	if(o->rxcdtype==CD_HID && (o->pmrChan->rxExtCarrierDetect!=o->rxhidsq))
		o->pmrChan->rxExtCarrierDetect=o->rxhidsq;
	
	if(o->rxcdtype==CD_HID_INVERT && (o->pmrChan->rxExtCarrierDetect==o->rxhidsq))
		o->pmrChan->rxExtCarrierDetect=!o->rxhidsq;
		
	if( (o->rxcdtype==CD_HID        && o->rxhidsq)                  ||
		(o->rxcdtype==CD_HID_INVERT && !o->rxhidsq)                 ||
		(o->rxcdtype==CD_XPMR_NOISE && o->pmrChan->rxCarrierDetect) ||
		(o->rxcdtype==CD_PP && o->rxppsq)                 ||
		(o->rxcdtype==CD_PP_INVERT && !o->rxppsq)                 ||
		(o->rxcdtype==CD_XPMR_VOX   && o->pmrChan->rxCarrierDetect) )
	{
		if (!o->pmrChan->txPttOut || o->radioduplex)cd=1;	
	}
	else
	{
		cd=0;
	}


	if(cd!=o->rxcarrierdetect)
	{
		o->rxcarrierdetect=cd;
		if(o->debuglevel) ast_log(LOG_NOTICE,"rxcarrierdetect = %i, chan %s\n",cd,o->owner->name);
		//printf("rxcarrierdetect = %i, chan %s\n",res,o->owner->name);
	}

	if(o->pmrChan->b.ctcssRxEnable && o->pmrChan->rxCtcss->decode!=o->rxctcssdecode)
	{
		if(o->debuglevel)ast_log(LOG_NOTICE,"rxctcssdecode = %i, chan %s\n",o->pmrChan->rxCtcss->decode,o->owner->name);
		//printf("rxctcssdecode = %i, chan %s\n",o->pmrChan->rxCtcss->decode,o->owner->name);
		o->rxctcssdecode=o->pmrChan->rxCtcss->decode;
		strcpy(o->rxctcssfreq, o->pmrChan->rxctcssfreq);
	}

	#ifndef HAVE_XPMRX
	if(  !o->pmrChan->b.ctcssRxEnable ||
		( o->pmrChan->b.ctcssRxEnable && 
	      o->pmrChan->rxCtcss->decode>CTCSS_NULL && 
	      o->pmrChan->smode==SMODE_CTCSS )  
	)
	{
		sd=1;	
	}
	else
	{
		sd=0;
	}
	#else
	if( (!o->pmrChan->b.ctcssRxEnable && !o->pmrChan->b.dcsRxEnable && !o->pmrChan->b.lmrRxEnable) ||
		( o->pmrChan->b.ctcssRxEnable && 
	      o->pmrChan->rxCtcss->decode>CTCSS_NULL && 
	      o->pmrChan->smode==SMODE_CTCSS ) ||
		( o->pmrChan->b.dcsRxEnable && 
	      o->pmrChan->decDcs->decode > 0 &&
	      o->pmrChan->smode==SMODE_DCS )
	)
	{
		sd=1;	
	}
	else
	{
		sd=0;
	}

	if(o->pmrChan->decDcs->decode!=o->rxdcsdecode)
	{													
		if(o->debuglevel)ast_log(LOG_NOTICE,"rxdcsdecode = %s, chan %s\n",o->pmrChan->rxctcssfreq,o->owner->name);
		// printf("rxctcssdecode = %i, chan %s\n",o->pmrChan->rxCtcss->decode,o->owner->name);
		o->rxdcsdecode=o->pmrChan->decDcs->decode;
		strcpy(o->rxctcssfreq, o->pmrChan->rxctcssfreq);
	}																							  

	if(o->pmrChan->rptnum && (o->pmrChan->pLsdCtl->cs[o->pmrChan->rptnum].b.rxkeyed != o->rxlsddecode))
	{								
		if(o->debuglevel)ast_log(LOG_NOTICE,"rxLSDecode = %s, chan %s\n",o->pmrChan->rxctcssfreq,o->owner->name);
		o->rxlsddecode=o->pmrChan->pLsdCtl->cs[o->pmrChan->rptnum].b.rxkeyed;
		strcpy(o->rxctcssfreq, o->pmrChan->rxctcssfreq);
	}

	if( (o->pmrChan->rptnum>0 && o->pmrChan->smode==SMODE_LSD && o->pmrChan->pLsdCtl->cs[o->pmrChan->rptnum].b.rxkeyed)||
	    (o->pmrChan->smode==SMODE_DCS && o->pmrChan->decDcs->decode>0) )
	{
		sd=1;
	}
	#endif
	if(o->rxsdtype == SD_HID) sd = o->rxhidctcss;
	if(o->rxsdtype == SD_HID_INVERT) sd = !o->rxhidctcss;
	if(o->rxsdtype == SD_PP) sd = o->rxppctcss;
	if(o->rxsdtype == SD_PP_INVERT) sd = !o->rxppctcss;
	if (o->rxctcssoverride) sd = 1;
	// Timers for how long TX has ben unkeyed
	// This is used for the TX offdelay
	if (o->txkeyed == 1)
		o->txoffcnt = 0; // If keyed, set this to zero.
	if (o->txkeyed == 0)
		o->txoffcnt++; // Timer starts after radio unkeys.
	if (o->txoffcnt > 50000)
		o->txoffcnt=20000; // This prevents integer overflow
	if ( cd && sd)
	{
		//if(!o->rxkeyed)o->pmrChan->dd.b.doitnow=1;
		if(!o->rxkeyed && o->debuglevel)ast_log(LOG_NOTICE,"o->rxkeyed = 1, chan %s\n", o->owner->name);
		if (o->rxkeyed || ((o->txoffcnt >= o->txoffdelay) && ( o->rxoncnt >= o->rxondelay)))
			o->rxkeyed = 1;
		else o->rxoncnt++;
	}
	else 
	{
		//if(o->rxkeyed)o->pmrChan->dd.b.doitnow=1;
		if(o->rxkeyed && o->debuglevel)ast_log(LOG_NOTICE,"o->rxkeyed = 0, chan %s\n",o->owner->name);
		o->rxkeyed = 0;
		o->rxoncnt = 0;
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
			memcpy(u->data,(o->usbradio_read_buf_8k + AST_FRIENDLY_OFFSET),FRAME_SIZE * 2);
			if (u == NULL)

				ast_log(LOG_ERROR,"Cannot malloc\n");
			else
				insque((struct qelem *)u,o->echoq.q_back);
		}
		ast_mutex_unlock(&o->echolock);
	}


	// provide rx signal detect conditions
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
		if(o->rxctcssdecode)  	
	        {
		        wf.data = o->rxctcssfreq;
		        wf.datalen = strlen(o->rxctcssfreq) + 1;
				TRACEO(1,("AST_CONTROL_RADIO_KEY text=%s\n",o->rxctcssfreq));
	        }
		ast_queue_frame(o->owner, &wf);
		o->count_rssi_update=1;
		if (o->duplex3)
			setamixer(o->devicenum,MIXER_PARAM_MIC_PLAYBACK_SW,1,0);
	}

	o->readpos = AST_FRIENDLY_OFFSET;	/* reset read pointer for next frame */
	if (c->_state != AST_STATE_UP)	/* drop data if frame is not up */
		return f;
	/* ok we can build and deliver the frame to the caller */
	f->frametype = AST_FRAME_VOICE;
	f->subclass = AST_FORMAT_SLINEAR;
	f->samples = FRAME_SIZE;
	f->datalen = FRAME_SIZE * 2;
	f->data = o->usbradio_read_buf_8k + AST_FRIENDLY_OFFSET;
	if (o->boost != BOOST_SCALE) {	/* scale and clip values */
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
	if (!o->rxkeyed) memset(f->data,0,f->datalen);
	f->offset = AST_FRIENDLY_OFFSET;
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
		{
			f1->len = ast_tvdiff_ms(ast_tvnow(),o->tonetime);
			if (option_verbose)
				ast_log(LOG_NOTICE,"Got DTMF char %c duration %ld ms\n",f1->subclass,f1->len);
			o->toneflag  = 0;
		} else {
			if (o->toneflag)
			{
				ast_frfree(f1);
				f1 = NULL;
			}
			else
			{
				o->tonetime = ast_tvnow();
				o->toneflag = 1;
			}
		}
		if (f1) return(f1);
	    }
	}
        if ( o->pmrChan->b.txCtcssReady )
        {
                 struct ast_frame wf = { AST_FRAME_TEXT };
                 char msg[32];
                 memset(msg,0,32);

                 sprintf(msg,"cstx=%.26s",o->pmrChan->txctcssfreq);
                 if(o->debuglevel)
		 {
                         ast_log(LOG_NOTICE,
                                 "dev=%s got b.txCtcssReady %s\n",
	                          o->name,o->pmrChan->txctcssfreq);
		 }
                 o->pmrChan->b.txCtcssReady = 0;
 	         wf.data = msg;
 	         wf.datalen = strlen(msg) + 1;
                 ast_queue_frame(o->owner, &wf);
         }
	/* report channel rssi */
	if(o->sendvoter && o->count_rssi_update && o->rxkeyed)
	{
		if(--o->count_rssi_update<=0)
		{
			struct ast_frame wf = { AST_FRAME_TEXT };
			char msg[32];
			sprintf(msg,"R %i", ((32767-o->pmrChan->rxRssi)*1000)/32767 );
	        wf.data = msg;
	        wf.datalen = strlen(msg) + 1;
			ast_queue_frame(o->owner, &wf);
			o->count_rssi_update=10;
			if(o->debuglevel>3)
				ast_log(LOG_NOTICE,"[%s] count_rssi_update %i\n",
					o->name,((32767-o->pmrChan->rxRssi)*1000/32767));
		}
	}

	return f;
}

static int usbradio_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct chan_usbradio_pvt *o = newchan->tech_pvt;
	ast_log(LOG_WARNING,"usbradio_fixup()\n");
	o->owner = newchan;
	return 0;
}

static int usbradio_indicate(struct ast_channel *c, int cond, const void *data, size_t datalen)
{
	struct chan_usbradio_pvt *o = c->tech_pvt;
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
			if(o->debuglevel)ast_verbose("chan_usbradio ACRK  dev=%s  code=%s TX ON \n",o->name,(char *)data);
			if(datalen && ((char *)(data))[0]!='0')
			{
			    o->b.forcetxcode=1;
				memset(o->set_txctcssfreq,0,16);
				strncpy(o->set_txctcssfreq,data,16);
				o->set_txctcssfreq[15]=0;
				xpmr_config(o);		
			}
			break;
		case AST_CONTROL_RADIO_UNKEY:
			o->txkeyed = 0;
			if(o->debuglevel)ast_verbose("chan_usbradio ACRUK  dev=%s TX OFF >> \n",o->name);
			if(o->b.forcetxcode)
			{
				o->b.forcetxcode=0;
				o->pmrChan->pTxCodeDefault = o->txctcssdefault;
				if(o->debuglevel)ast_verbose("chan_usbradio dev=%s Forced Tx Squelch Code CLEARED\n",o->name);
			}
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to display condition %d on %s\n", cond, c->name);
			return -1;
	}

	if (res > -1)
		ring(o, res);

	return 0;
}

static int usbradio_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
	char *cp;
	struct chan_usbradio_pvt *o = chan->tech_pvt;

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
static struct ast_channel *usbradio_new(struct chan_usbradio_pvt *o, char *ext, char *ctx, int state)
{
	struct ast_channel *c;

	c = ast_channel_alloc(1, state, o->cid_num, o->cid_name, "", ext, ctx, 0, "Radio/%s", o->name);
	if (c == NULL)
		return NULL;
	c->tech = &usbradio_tech;
#if	 0
	if (o->sounddev < 0)
		setformat(o, O_RDWR);
#endif
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
	o->echoq.q_forw = o->echoq.q_back = &o->echoq;
	ast_mutex_init(&o->echolock);
	ast_mutex_init(&o->usblock);
	ast_module_ref(ast_module_info->self);
	o->echomax = DEFAULT_ECHO_MAX;
	o->echomode = 0;
	o->echoing = 0;
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
static struct ast_channel *usbradio_request(const char *type, int format, void *data, int *cause)
{
	struct ast_channel *c;
	struct chan_usbradio_pvt *o = find_desc(data);

	TRACEO(1,("usbradio_request()\n"));

	if (0)
	{
		ast_log(LOG_WARNING, "usbradio_request type <%s> data 0x%p <%s>\n", type, data, (char *) data);
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
	c = usbradio_new(o, NULL, NULL, AST_STATE_DOWN);
	if (c == NULL) {
		ast_log(LOG_WARNING, "Unable to create new usb channel\n");
		return NULL;
	}
		
	o->b.remoted=0;
	xpmr_config(o);

	return c;
}
/*
*/
static int console_key(int fd, int argc, char *argv[])
{
	struct chan_usbradio_pvt *o = find_desc(usbradio_active);

	if (argc != 2)
		return RESULT_SHOWUSAGE; 
	o->txtestkey = 1;
	return RESULT_SUCCESS;
}
/*
*/
static int console_unkey(int fd, int argc, char *argv[])
{
	struct chan_usbradio_pvt *o = find_desc(usbradio_active);

	if (argc != 2)
		return RESULT_SHOWUSAGE;
	o->txtestkey = 0;
	return RESULT_SUCCESS;
}

static int usb_device_swap(int fd,char *other)
{

int	d;
char	tmp[128];
struct chan_usbradio_pvt *p = NULL,*o = find_desc(usbradio_active);

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

static void tune_flash(int fd, struct chan_usbradio_pvt *o, int intflag)
{
#define	NFLASH 3

int	i;

	if (fd > 0)
		ast_cli(fd,"USB Device Flash starting on channel %s...\n",o->name);
	for(i = 0; i < NFLASH; i++)
	{
		o->txtestkey=1;
		o->pmrChan->txPttIn=1;
		TxTestTone(o->pmrChan, 1);	  // generate 1KHz tone at 7200 peak
		if ((fd > 0) && intflag)
		{
			if (happy_mswait(fd,1000,intflag))
			{
				o->pmrChan->txPttIn=0;
				o->txtestkey=0;
				break;
			}
		} else usleep(1000000);
		TxTestTone(o->pmrChan, 0);
		o->pmrChan->txPttIn=0;
		o->txtestkey=0;
		if (i == (NFLASH - 1)) break;
		if ((fd > 0) && intflag)
		{
			if (happy_mswait(fd,1500,intflag))
			{
				o->pmrChan->txPttIn=0;
				o->txtestkey=0;
				break;
			}
		} else usleep(1500000);
	}
	if (fd > 0)
		ast_cli(fd,"USB Device Flash ending on channel %s...\n",o->name);
	o->pmrChan->txPttIn=0;
	o->txtestkey=0;
}

static int radio_tune(int fd, int argc, char *argv[])
{
	struct chan_usbradio_pvt *o = find_desc(usbradio_active);
	int i=0;

	if ((argc < 2) || (argc > 4))
		return RESULT_SHOWUSAGE; 

	if (argc == 2) /* just show stuff */
	{
		ast_cli(fd,"Active radio interface is [%s]\n",usbradio_active);
		ast_mutex_lock(&usb_dev_lock);
		ast_cli(fd,"Device String is %s\n",o->devstr);
		ast_mutex_unlock(&usb_dev_lock);
 	  	ast_cli(fd,"Card is %i\n",usb_get_usbdev(o->devstr));
		ast_cli(fd,"Output A is currently set to ");
		if(o->txmixa==TX_OUT_COMPOSITE)ast_cli(fd,"composite.\n");
		else if (o->txmixa==TX_OUT_VOICE)ast_cli(fd,"voice.\n");
		else if (o->txmixa==TX_OUT_LSD)ast_cli(fd,"tone.\n");
		else if (o->txmixa==TX_OUT_AUX)ast_cli(fd,"auxvoice.\n");
		else ast_cli(fd,"off.\n");

		ast_cli(fd,"Output B is currently set to ");
		if(o->txmixb==TX_OUT_COMPOSITE)ast_cli(fd,"composite.\n");
		else if (o->txmixb==TX_OUT_VOICE)ast_cli(fd,"voice.\n");
		else if (o->txmixb==TX_OUT_LSD)ast_cli(fd,"tone.\n");
		else if (o->txmixb==TX_OUT_AUX)ast_cli(fd,"auxvoice.\n");
		else ast_cli(fd,"off.\n");

		ast_cli(fd,"Tx Voice Level currently set to %d\n",o->txmixaset);
		ast_cli(fd,"Tx Tone Level currently set to %d\n",o->txctcssadj);
		ast_cli(fd,"Rx Squelch currently set to %d\n",o->rxsquelchadj);
		return RESULT_SHOWUSAGE;
	}

	o->pmrChan->b.tuning=1;

	if (!strcasecmp(argv[2],"dump")) pmrdump(o);
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

	if (!strcasecmp(argv[2],"rxnoise")) tune_rxinput(fd,o,0,0);
	else if (!strcasecmp(argv[2],"rxvoice")) tune_rxvoice(fd,o,0);
	else if (!strcasecmp(argv[2],"rxtone")) tune_rxctcss(fd,o,0);
	else if (!strcasecmp(argv[2],"flash"))
	{
		tune_flash(fd,o,0);
	}
	else if (!strcasecmp(argv[2],"rxsquelch"))
	{
		if (argc == 3)
		{
		    ast_cli(fd,"Current Signal Strength is %d\n",((32767-o->pmrChan->rxRssi)*1000/32767));
		    ast_cli(fd,"Current Squelch setting is %d\n",o->rxsquelchadj);
			//ast_cli(fd,"Current Raw RSSI        is %d\n",o->pmrChan->rxRssi);
		    //ast_cli(fd,"Current (real) Squelch setting is %d\n",*(o->pmrChan->prxSquelchAdjust));
		} else {
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) return RESULT_SHOWUSAGE;
			ast_cli(fd,"Changed Squelch setting to %d\n",i);
			o->rxsquelchadj = i;
			*(o->pmrChan->prxSquelchAdjust)= ((999 - i) * 32767) / 1000;
		}
	}
	else if (!strcasecmp(argv[2],"txvoice")) {
		i = 0;

		if( (o->txmixa!=TX_OUT_VOICE) && (o->txmixb!=TX_OUT_VOICE) &&
			(o->txmixa!=TX_OUT_COMPOSITE) && (o->txmixb!=TX_OUT_COMPOSITE)
		  )
		{
			ast_log(LOG_ERROR,"No txvoice output configured.\n");
		}
		else if (argc == 3)
		{
			if((o->txmixa==TX_OUT_VOICE)||(o->txmixa==TX_OUT_COMPOSITE))
				ast_cli(fd,"Current txvoice setting on Channel A is %d\n",o->txmixaset);
			else
				ast_cli(fd,"Current txvoice setting on Channel B is %d\n",o->txmixbset);
		}
		else
		{
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) return RESULT_SHOWUSAGE;

			if((o->txmixa==TX_OUT_VOICE)||(o->txmixa==TX_OUT_COMPOSITE))
			{
			 	o->txmixaset=i;
				ast_cli(fd,"Changed txvoice setting on Channel A to %d\n",o->txmixaset);
			}
			else
			{
			 	o->txmixbset=i;   
				ast_cli(fd,"Changed txvoice setting on Channel B to %d\n",o->txmixbset);
			}
			mixer_write(o);
			mult_set(o);
			ast_cli(fd,"Changed Tx Voice Output setting to %d\n",i);
		}
		o->pmrChan->b.txCtcssInhibit=1;
		tune_txoutput(o,i,fd,0);
		o->pmrChan->b.txCtcssInhibit=0;
	}
	else if (!strcasecmp(argv[2],"txall")) {
		i = 0;

		if( (o->txmixa!=TX_OUT_VOICE) && (o->txmixb!=TX_OUT_VOICE) &&
			(o->txmixa!=TX_OUT_COMPOSITE) && (o->txmixb!=TX_OUT_COMPOSITE)
		  )
		{
			ast_log(LOG_ERROR,"No txvoice output configured.\n");
		}
		else if (argc == 3)
		{
			if((o->txmixa==TX_OUT_VOICE)||(o->txmixa==TX_OUT_COMPOSITE))
				ast_cli(fd,"Current txvoice setting on Channel A is %d\n",o->txmixaset);
			else
				ast_cli(fd,"Current txvoice setting on Channel B is %d\n",o->txmixbset);
		}
		else
		{
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) return RESULT_SHOWUSAGE;

			if((o->txmixa==TX_OUT_VOICE)||(o->txmixa==TX_OUT_COMPOSITE))
			{
			 	o->txmixaset=i;
				ast_cli(fd,"Changed txvoice setting on Channel A to %d\n",o->txmixaset);
			}
			else
			{
			 	o->txmixbset=i;   
				ast_cli(fd,"Changed txvoice setting on Channel B to %d\n",o->txmixbset);
			}
			mixer_write(o);
			mult_set(o);
			ast_cli(fd,"Changed Tx Voice Output setting to %d\n",i);
		}
		tune_txoutput(o,i,fd,0);
	}
	else if (!strcasecmp(argv[2],"auxvoice")) {
		i = 0;
		if( (o->txmixa!=TX_OUT_AUX) && (o->txmixb!=TX_OUT_AUX))
		{
			ast_log(LOG_WARNING,"No auxvoice output configured.\n");
		}
		else if (argc == 3)
		{
			if(o->txmixa==TX_OUT_AUX)
				ast_cli(fd,"Current auxvoice setting on Channel A is %d\n",o->txmixaset);
			else
				ast_cli(fd,"Current auxvoice setting on Channel B is %d\n",o->txmixbset);
		}
		else
		{
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) return RESULT_SHOWUSAGE;
			if(o->txmixa==TX_OUT_AUX)
			{
				o->txmixbset=i;
				ast_cli(fd,"Changed auxvoice setting on Channel A to %d\n",o->txmixaset);
			}
			else
			{
				o->txmixbset=i;
				ast_cli(fd,"Changed auxvoice setting on Channel B to %d\n",o->txmixbset);
			}
			mixer_write(o);
			mult_set(o);
		}
		//tune_auxoutput(o,i);
	}
	else if (!strcasecmp(argv[2],"txtone"))
	{
		if (argc == 3)
			ast_cli(fd,"Current Tx CTCSS modulation setting = %d\n",o->txctcssadj);
		else
		{
			i = atoi(argv[3]);
			if ((i < 0) || (i > 999)) return RESULT_SHOWUSAGE;
			o->txctcssadj = i;
			set_txctcss_level(o);
			ast_cli(fd,"Changed Tx CTCSS modulation setting to %i\n",i);
		}
		o->txtestkey=1;
		usleep(5000000);
		o->txtestkey=0;
	}
	else if (!strcasecmp(argv[2],"nocap")) 	
	{
		ast_cli(fd,"File capture (trace) was rx=%d tx=%d and now off.\n",o->b.rxcap2,o->b.txcap2);
		ast_cli(fd,"File capture (raw)   was rx=%d tx=%d and now off.\n",o->b.rxcapraw,o->b.txcapraw);
		o->b.rxcapraw=o->b.txcapraw=o->b.rxcap2=o->b.txcap2=o->pmrChan->b.rxCapture=o->pmrChan->b.txCapture=0;
		if (frxcapraw) { fclose(frxcapraw); frxcapraw = NULL; }
		if (frxcaptrace) { fclose(frxcaptrace); frxcaptrace = NULL; }
		if (frxoutraw) { fclose(frxoutraw); frxoutraw = NULL; }
		if (ftxcapraw) { fclose(ftxcapraw); ftxcapraw = NULL; }
		if (ftxcaptrace) { fclose(ftxcaptrace); ftxcaptrace = NULL; }
		if (ftxoutraw) { fclose(ftxoutraw); ftxoutraw = NULL; }
	}
	else if (!strcasecmp(argv[2],"rxtracecap")) 
	{
		if (!frxcaptrace) frxcaptrace= fopen(RX_CAP_TRACE_FILE,"w");
		ast_cli(fd,"Trace rx on.\n");
		o->b.rxcap2=o->pmrChan->b.rxCapture=1;
	}
	else if (!strcasecmp(argv[2],"txtracecap")) 
	{
		if (!ftxcaptrace) ftxcaptrace= fopen(TX_CAP_TRACE_FILE,"w");
		ast_cli(fd,"Trace tx on.\n");
		o->b.txcap2=o->pmrChan->b.txCapture=1;
	}
	else if (!strcasecmp(argv[2],"rxcap")) 
	{
		if (!frxcapraw) frxcapraw = fopen(RX_CAP_RAW_FILE,"w");
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
		ast_cli(fd,"Saved radio tuning settings to usbradio_tune_%s.conf\n",o->name);
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
		o->pmrChan->b.tuning=0;
		return RESULT_SHOWUSAGE;
	}
	o->pmrChan->b.tuning=0;
	return RESULT_SUCCESS;
}

/*
	set transmit ctcss modulation level
	adjust mixer output or internal gain depending on output type
	setting range is 0.0 to 0.9
*/
static int set_txctcss_level(struct chan_usbradio_pvt *o)
{							  
	if (o->txmixa == TX_OUT_LSD)
	{
//		o->txmixaset=(151*o->txctcssadj) / 1000;
		o->txmixaset=o->txctcssadj;
		mixer_write(o);
		mult_set(o);
	}
	else if (o->txmixb == TX_OUT_LSD)
	{
//		o->txmixbset=(151*o->txctcssadj) / 1000;
		o->txmixbset=o->txctcssadj;
		mixer_write(o);
		mult_set(o);
	}
	else
	{
		if(o->pmrChan->ptxCtcssAdjust){ /* Ignore if ptr not defined */
			*o->pmrChan->ptxCtcssAdjust=(o->txctcssadj * M_Q8) / 1000;
		}
	}
	return 0;
}
/*
	CLI debugging on and off
*/
static int radio_set_debug(int fd, int argc, char *argv[])
{
	struct chan_usbradio_pvt *o = find_desc(usbradio_active);

	o->debuglevel=1;
	ast_cli(fd,"usbradio debug on.\n");
	return RESULT_SUCCESS;
}

static int radio_set_debug_off(int fd, int argc, char *argv[])
{
	struct chan_usbradio_pvt *o = find_desc(usbradio_active);

	o->debuglevel=0;
	ast_cli(fd,"usbradio debug off.\n");
	return RESULT_SUCCESS;
}

static int radio_active(int fd, int argc, char *argv[])
{
        if (argc == 2)
                ast_cli(fd, "Active (command) USB Radio device is [%s]\n", usbradio_active);
        else if (argc != 3)
                return RESULT_SHOWUSAGE;
        else {
                struct chan_usbradio_pvt *o;
                if (strcmp(argv[2], "show") == 0) {
			ast_mutex_lock(&usb_dev_lock);
                        for (o = usbradio_default.next; o; o = o->next)
                                ast_cli(fd, "device [%s] exists as device=%s card=%d\n", 
					o->name,o->devstr,usb_get_usbdev(o->devstr));
			ast_mutex_unlock(&usb_dev_lock);
                        return RESULT_SUCCESS;
                }
                o = find_desc(argv[2]);
                if (o == NULL)
                        ast_cli(fd, "No device [%s] exists\n", argv[2]);
                else
		{
			struct chan_usbradio_pvt *ao;
			for (ao = usbradio_default.next; ao && ao->name ; ao = ao->next)
				ao->pmrChan->b.radioactive=0;
	                usbradio_active = o->name;
			o->pmrChan->b.radioactive=1;
			ast_cli(fd,"Active (command) USB Radio device set to [%s]\n",usbradio_active);
		}
        }
        return RESULT_SUCCESS;
}
/*
	CLI debugging on and off
*/
static int radio_set_xpmr_debug(int fd, int argc, char *argv[])
{
	struct chan_usbradio_pvt *o = find_desc(usbradio_active);

	if (argc == 4)
	{
		int i;
		i = atoi(argv[3]);
		if ((i >= 0) && (i <= 100))
		{ 
			o->pmrChan->tracelevel=i;
		}
    }
	// add ability to set it for a number of frames after which it reverts
	ast_cli(fd,"usbradio xdebug on tracelevel %i\n",o->pmrChan->tracelevel);

	return RESULT_SUCCESS;
}


static char key_usage[] =
	"Usage: radio key\n"
	"       Simulates COR active.\n";

static char unkey_usage[] =
	"Usage: radio unkey\n"
	"       Simulates COR un-active.\n";

static char active_usage[] =
        "Usage: radio active [device-name]\n"
        "       If used without a parameter, displays which device is the current\n"
        "one being commanded.  If a device is specified, the commanded radio device is changed\n"
        "to the device specified.\n";
/*
radio tune 6 3000		measured tx value
*/
static char radio_tune_usage[] =
	"Usage: radio tune <function>\n"
	"       rxnoise\n"
	"       rxvoice\n"
	"       rxtone\n"
	"       rxsquelch [newsetting]\n"
	"       txvoice [newsetting]\n"
	"       txtone [newsetting]\n"
	"       auxvoice [newsetting]\n"
	"       save (settings to tuning file)\n"
	"       load (tuning settings from EEPROM)\n"
	"\n       All [newsetting]'s are values 0-999\n\n";
					  
#ifndef	NEW_ASTERISK

static struct ast_cli_entry cli_usbradio[] = {
	{ { "radio", "key", NULL },
	console_key, "Simulate Rx Signal Present",
	key_usage, NULL, NULL},

	{ { "radio", "unkey", NULL },
	console_unkey, "Simulate Rx Signal Lusb",
	unkey_usage, NULL, NULL },

	{ { "radio", "tune", NULL },
	radio_tune, "Radio Tune",
	radio_tune_usage, NULL, NULL },

	{ { "radio", "set", "debug", NULL },
	radio_set_debug, "Radio Debug",
	radio_tune_usage, NULL, NULL },

	{ { "radio", "set", "debug", "off", NULL },
	radio_set_debug_off, "Radio Debug",
	radio_tune_usage, NULL, NULL },

	{ { "radio", "active", NULL },
	radio_active, "Change commanded device",
	active_usage, NULL, NULL },

    { { "radio", "set", "xdebug", NULL },
	radio_set_xpmr_debug, "Radio set xpmr debug level",
	active_usage, NULL, NULL },

};
#endif

/*
 * store the callerid components
 */
#if 0
static void store_callerid(struct chan_usbradio_pvt *o, char *s)
{
	ast_callerid_split(s, o->cid_name, sizeof(o->cid_name), o->cid_num, sizeof(o->cid_num));
}
#endif

static void store_rxdemod(struct chan_usbradio_pvt *o, char *s)
{
	if (!strcasecmp(s,"no")){
		o->rxdemod = RX_AUDIO_NONE;
	}
	else if (!strcasecmp(s,"speaker")){
		o->rxdemod = RX_AUDIO_SPEAKER;
	}
	else if (!strcasecmp(s,"flat")){
			o->rxdemod = RX_AUDIO_FLAT;
	}	
	else {
		ast_log(LOG_WARNING,"Unrecognized rxdemod parameter: %s\n",s);
	}

	//ast_log(LOG_WARNING, "set rxdemod = %s\n", s);
}

					   
static void store_txmixa(struct chan_usbradio_pvt *o, char *s)
{
	if (!strcasecmp(s,"no")){
		o->txmixa = TX_OUT_OFF;
	}
	else if (!strcasecmp(s,"voice")){
		o->txmixa = TX_OUT_VOICE;
	}
	else if (!strcasecmp(s,"tone")){
			o->txmixa = TX_OUT_LSD;
	}	
	else if (!strcasecmp(s,"composite")){
		o->txmixa = TX_OUT_COMPOSITE;
	}	
	else if (!strcasecmp(s,"auxvoice")){
		o->txmixa = TX_OUT_AUX;
	}	
	else {
		ast_log(LOG_WARNING,"Unrecognized txmixa parameter: %s\n",s);
	}

	//ast_log(LOG_WARNING, "set txmixa = %s\n", s);
}

static void store_txmixb(struct chan_usbradio_pvt *o, char *s)
{
	if (!strcasecmp(s,"no")){
		o->txmixb = TX_OUT_OFF;
	}
	else if (!strcasecmp(s,"voice")){
		o->txmixb = TX_OUT_VOICE;
	}
	else if (!strcasecmp(s,"tone")){
			o->txmixb = TX_OUT_LSD;
	}	
	else if (!strcasecmp(s,"composite")){
		o->txmixb = TX_OUT_COMPOSITE;
	}	
	else if (!strcasecmp(s,"auxvoice")){
		o->txmixb = TX_OUT_AUX;
	}	
	else {
		ast_log(LOG_WARNING,"Unrecognized txmixb parameter: %s\n",s);
	}

	//ast_log(LOG_WARNING, "set txmixb = %s\n", s);
}
/*
*/
static void store_rxcdtype(struct chan_usbradio_pvt *o, char *s)
{
	if (!strcasecmp(s,"no")){
		o->rxcdtype = CD_IGNORE;
	}
	else if (!strcasecmp(s,"usb")){
		o->rxcdtype = CD_HID;
	}
	else if (!strcasecmp(s,"dsp")){
		o->rxcdtype = CD_XPMR_NOISE;
	}	
	else if (!strcasecmp(s,"vox")){
		o->rxcdtype = CD_XPMR_VOX;
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
static void store_rxsdtype(struct chan_usbradio_pvt *o, char *s)
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
	else if (!strcasecmp(s,"dsp") || !strcasecmp(s,"SD_XPMR")){
		o->rxsdtype = SD_XPMR;
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

/*
*/
static void store_rxgain(struct chan_usbradio_pvt *o, char *s)
{
	float f;
	sscanf(s,"%f",&f); 
	o->rxgain = f;
	//ast_log(LOG_WARNING, "set rxgain = %f\n", f);
}
/*
*/
static void store_rxvoiceadj(struct chan_usbradio_pvt *o, char *s)
{
	float f;
	sscanf(s,"%f",&f);
	o->rxvoiceadj = f;
	//ast_log(LOG_WARNING, "set rxvoiceadj = %f\n", f);
}
/*
*/
static void store_rxctcssadj(struct chan_usbradio_pvt *o, char *s)
{
	float f;
	sscanf(s,"%f",&f);
	o->rxctcssadj = f;
	//ast_log(LOG_WARNING, "set rxctcssadj = %f\n", f);
}
/*
*/
static void store_txtoctype(struct chan_usbradio_pvt *o, char *s)
{
	if (!strcasecmp(s,"no") || !strcasecmp(s,"TOC_NONE")){
		o->txtoctype = TOC_NONE;
	}
	else if (!strcasecmp(s,"phase") || !strcasecmp(s,"TOC_PHASE")){
		o->txtoctype = TOC_PHASE;
	}
	else if (!strcasecmp(s,"notone") || !strcasecmp(s,"TOC_NOTONE")){
		o->txtoctype = TOC_NOTONE;
	}	
	else {
		ast_log(LOG_WARNING,"Unrecognized txtoctype parameter: %s\n",s);
	}
}
/*
*/
static void tune_txoutput(struct chan_usbradio_pvt *o, int value, int fd, int intflag)
{
	o->txtestkey=1;
	o->pmrChan->txPttIn=1;
	TxTestTone(o->pmrChan, 1);	  // generate 1KHz tone at 7200 peak
	if (fd > 0)
	{
		ast_cli(fd,"Tone output starting on channel %s...\n",o->name);
		if (happy_mswait(fd,5000,intflag))
		{
			o->pmrChan->txPttIn=0;
			o->txtestkey=0;
		}
	} else usleep(5000000);
	TxTestTone(o->pmrChan, 0);
	if (fd > 0) ast_cli(fd,"Tone output ending on channel %s...\n",o->name);
	o->pmrChan->txPttIn=0;
	o->txtestkey=0;
}
/*
	Adjust Input Attenuator with maximum signal input
*/
static void tune_rxinput(int fd, struct chan_usbradio_pvt *o, int setsql, int intflag)
{
	const int settingmin=1;
	const int settingstart=2;
	const int maxtries=12;

	int target;
	int tolerance=2750;
	int setting=0, tries=0, tmpdiscfactor, meas, measnoise;
	float settingmax,f;

	if(o->rxdemod==RX_AUDIO_SPEAKER && o->rxcdtype==CD_XPMR_NOISE)
	{
		ast_cli(fd,"ERROR: usbradio.conf rxdemod=speaker vs. carrierfrom=dsp \n");	
	}

	if( o->rxdemod==RX_AUDIO_FLAT )
		target=27000;
	else
		target=23000;

	settingmax = o->micmax;

	o->fever = 1;
	o->pmrChan->fever = 1;

	o->pmrChan->b.tuning=1;

	setting = settingstart;

    ast_cli(fd,"tune rxnoise maxtries=%i, target=%i, tolerance=%i\n",maxtries,target,tolerance);

	while(tries<maxtries)
	{
		setamixer(o->devicenum,MIXER_PARAM_MIC_CAPTURE_VOL,setting,0);
		setamixer(o->devicenum,MIXER_PARAM_MIC_BOOST,o->rxboostset,0);

		if (happy_mswait(fd,100,intflag)) 
		{
			o->pmrChan->b.tuning=0;
			return;
		}
		o->pmrChan->spsMeasure->source = o->pmrChan->spsRx->source;
		o->pmrChan->spsMeasure->discfactor=2000;
		o->pmrChan->spsMeasure->enabled=1;
		o->pmrChan->spsMeasure->amax = o->pmrChan->spsMeasure->amin = 0;
		if (happy_mswait(fd,400,intflag))
		{
			o->pmrChan->b.tuning=0;
			return;
		}
		meas=o->pmrChan->spsMeasure->apeak;
		o->pmrChan->spsMeasure->enabled=0;

		if(!meas)meas++;
		ast_cli(fd,"tries=%i, setting=%i, meas=%i\n",tries,setting,meas);

		if( (meas<(target-tolerance) || meas>(target+tolerance)) && tries<=2){
			f=(float)(setting*target)/meas;
			setting=(int)(f+0.5);
		}
		else if( meas<(target-tolerance) && tries>2){
			setting++;
		}
		else if( meas>(target+tolerance) && tries>2){
			setting--;
		}
		else if(tries>5 && meas>(target-tolerance) && meas<(target+tolerance) )
		{
			break;
		}

		if(setting<settingmin)setting=settingmin;
		else if(setting>settingmax)setting=settingmax;

		tries++;
	}


	/* Measure HF Noise */
	tmpdiscfactor=o->pmrChan->spsRx->discfactor;
	o->pmrChan->spsRx->discfactor=(i16)2000;
	o->pmrChan->spsRx->discounteru=o->pmrChan->spsRx->discounterl=0;
	o->pmrChan->spsRx->amax=o->pmrChan->spsRx->amin=0;
	if (happy_mswait(fd,200,intflag))
	{
		o->pmrChan->b.tuning=0;
		return;
	}
	measnoise=o->pmrChan->rxRssi;

	/* Measure RSSI */
	o->pmrChan->spsRx->discfactor=tmpdiscfactor;
	o->pmrChan->spsRx->discounteru=o->pmrChan->spsRx->discounterl=0;
	o->pmrChan->spsRx->amax=o->pmrChan->spsRx->amin=0;
	if (happy_mswait(fd,200,intflag))
	{
		o->pmrChan->b.tuning=0;
		return;
	}

	ast_cli(fd,"DONE tries=%i, setting=%i, meas=%i, sqnoise=%i\n",tries,
		((setting * 1000) + (o->micmax / 2)) / o->micmax,meas,measnoise);

	if( meas<(target-tolerance) || meas>(target+tolerance) ){
		ast_cli(fd,"ERROR: RX INPUT ADJUST FAILED.\n");
	}else{
		ast_cli(fd,"INFO: RX INPUT ADJUST SUCCESS.\n");
		o->rxmixerset=((setting * 1000) + (o->micmax / 2)) / o->micmax;

		if(o->rxcdtype==CD_XPMR_NOISE)
		{
			int normRssi=((32767-o->pmrChan->rxRssi)*1000/32767);

			if((meas/(measnoise/10))>26){
				ast_cli(fd,"WARNING: Insufficient high frequency noise from receiver.\n");
				ast_cli(fd,"WARNING: Rx input point may be de-emphasized and not flat.\n");
				ast_cli(fd,"         usbradio.conf setting of 'carrierfrom=dsp' not recommended.\n");
			}
			else
			{
				ast_cli(fd,"Rx noise input seems sufficient for squelch.\n");	
			}
			if (setsql)
			{
				o->rxsquelchadj = normRssi + 150;
				if (o->rxsquelchadj > 999) o->rxsquelchadj = 999;
				*(o->pmrChan->prxSquelchAdjust)= ((999 - o->rxsquelchadj) * 32767) / 1000;
				ast_cli(fd,"Rx Squelch set to %d (RSSI=%d).\n",o->rxsquelchadj,normRssi);
			}
			else 
			{
				if(o->rxsquelchadj<normRssi)
				{
					ast_cli(fd,"WARNING: RSSI=%i SQUELCH=%i and is set too loose.\n",normRssi,o->rxsquelchadj);
					ast_cli(fd,"         Use 'radio tune rxsquelch' to adjust.\n");
				}
			}
		}
	}
	o->pmrChan->b.tuning=0;
}
/*
*/
static void do_rxdisplay(int fd, struct chan_usbradio_pvt *o)
{
	int j,waskeyed,meas,ncols = 75;
	char str[256];

	ast_cli(fd,"RX VOICE DISPLAY:\n");
	ast_cli(fd,"                                 v -- 3KHz        v -- 5KHz\n");

	if(!o->pmrChan->spsMeasure) 
	{
		ast_cli(fd,"ERROR: NO MEASURE BLOCK.\n");
		return;
	}

	if(!o->pmrChan->spsMeasure->source || !o->pmrChan->prxVoiceAdjust )
	{
		ast_cli(fd,"ERROR: NO SOURCE OR MEASURE SETTING.\n");
		return;
	}

	o->pmrChan->spsMeasure->source=o->pmrChan->spsRxOut->sink;

	o->pmrChan->spsMeasure->enabled=1;
	o->pmrChan->spsMeasure->discfactor=1000;

	waskeyed = !o->rxkeyed;
	for(;;)
	{
	    	o->pmrChan->spsMeasure->amax = 
			o->pmrChan->spsMeasure->amin = 0;
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
		meas = o->pmrChan->spsMeasure->apeak;
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
	o->pmrChan->spsMeasure->enabled=0;
}

static void _menu_rxvoice(int fd, struct chan_usbradio_pvt *o, char *str)
{
	int i,x;
	float f,f1;

	if (!str[0])
	{
		if (o->rxdemod == RX_AUDIO_FLAT)
			ast_cli(fd,"Current Rx voice setting: %d\n",(int)((o->rxvoiceadj * 200.0) + .5));
		else
			ast_cli(fd,"Current Rx voice setting: %d\n",o->rxmixerset);
		return;
	}
	for(x = 0; str[x]; x++)
	{
		if (!isdigit(str[x])) break;
	}
	if (str[x] || (sscanf(str,"%d",&i) < 1) || (i < 0) || (i > 999))
	{
		ast_cli(fd,"Entry Error, Rx voice setting not changed\n");
		return;
	}
	if (o->rxdemod == RX_AUDIO_FLAT)
	{
	 	o->rxvoiceadj=(float)i / 200.0;
	}
	else
	{
		o->rxmixerset = i;
		setamixer(o->devicenum,MIXER_PARAM_MIC_CAPTURE_VOL,
			o->rxmixerset * o->micmax / 1000,0);
		setamixer(o->devicenum,MIXER_PARAM_MIC_BOOST,o->rxboostset,0);
		/* get interval step size */
		f = 1000.0 / (float) o->micmax;
		o->rxvoiceadj = 0.5 + (modff(((float) i) / f,&f1) * .093981);
	}
	*(o->pmrChan->prxVoiceAdjust)=o->rxvoiceadj * M_Q8;
	ast_cli(fd,"Changed rx voice setting to %d\n",i);
	return;
}

static void _menu_print(int fd, struct chan_usbradio_pvt *o)
{
	ast_cli(fd,"Active radio interface is [%s]\n",usbradio_active);
	ast_mutex_lock(&usb_dev_lock);
	ast_cli(fd,"Device String is %s\n",o->devstr);
	ast_mutex_unlock(&usb_dev_lock);
  	ast_cli(fd,"Card is %i\n",usb_get_usbdev(o->devstr));
	ast_cli(fd,"Output A is currently set to ");
	if(o->txmixa==TX_OUT_COMPOSITE)ast_cli(fd,"composite.\n");
	else if (o->txmixa==TX_OUT_VOICE)ast_cli(fd,"voice.\n");
	else if (o->txmixa==TX_OUT_LSD)ast_cli(fd,"tone.\n");
	else if (o->txmixa==TX_OUT_AUX)ast_cli(fd,"auxvoice.\n");
	else ast_cli(fd,"off.\n");

	ast_cli(fd,"Output B is currently set to ");
	if(o->txmixb==TX_OUT_COMPOSITE)ast_cli(fd,"composite.\n");
	else if (o->txmixb==TX_OUT_VOICE)ast_cli(fd,"voice.\n");
	else if (o->txmixb==TX_OUT_LSD)ast_cli(fd,"tone.\n");
	else if (o->txmixb==TX_OUT_AUX)ast_cli(fd,"auxvoice.\n");
	else ast_cli(fd,"off.\n");

	ast_cli(fd,"Tx Voice Level currently set to %d\n",o->txmixaset);
	ast_cli(fd,"Tx Tone Level currently set to %d\n",o->txctcssadj);
	ast_cli(fd,"Rx Squelch currently set to %d\n",o->rxsquelchadj);
	return;
}

static void _menu_rxsquelch(int fd, struct chan_usbradio_pvt *o, char *str)
{
int	i,x;

	if (!str[0])
	{
		ast_cli(fd,"Current Signal Strength is %d\n",((32767-o->pmrChan->rxRssi)*1000/32767));
		ast_cli(fd,"Current Squelch setting is %d\n",o->rxsquelchadj);
		return;
	}
	for(x = 0; str[x]; x++)
	{
		if (!isdigit(str[x])) break;
	}
	if (str[x] || (sscanf(str,"%d",&i) < 1) || (i < 0) || (i > 999))
	{
		ast_cli(fd,"Entry Error, Rx Squelch Level setting not changed\n");
		return;
	}
	ast_cli(fd,"Changed Rx Squelch Level setting to %d\n",i);
	o->rxsquelchadj = i;
	*(o->pmrChan->prxSquelchAdjust)= ((999 - i) * 32767) / 1000;
	return;
}

static void _menu_txvoice(int fd, struct chan_usbradio_pvt *o,char *cstr)
{
char	*str = cstr;
int	i,j,x,dokey,withctcss;

	if( (o->txmixa!=TX_OUT_VOICE) && (o->txmixb!=TX_OUT_VOICE) &&
		(o->txmixa!=TX_OUT_COMPOSITE) && (o->txmixb!=TX_OUT_COMPOSITE))
	{
		ast_cli(fd,"Error, No txvoice output configured.\n");
		return;
	}
	if (!str[0])
	{
		if((o->txmixa==TX_OUT_VOICE)||(o->txmixa==TX_OUT_COMPOSITE))
		{
			ast_cli(fd,"Current Tx Voice Level setting on Channel A is %d\n",o->txmixaset);
		}
		else
		{
			ast_cli(fd,"Current Tx Voice Level setting on Channel B is %d\n",o->txmixbset);
		}
		return;
	}
	if((o->txmixa==TX_OUT_VOICE)||(o->txmixa==TX_OUT_COMPOSITE))
	{
		j = o->txmixaset;
	}
	else
	{
		j = o->txmixbset;
	}
	dokey = 0;
	if (str[0] == 'K')
	{
		dokey = 1;
		str++;
	}
	withctcss = 0;
	if (str[0] == 'C')
	{
		withctcss = 1;
		str++;
	}
	if (!str[0])
	{
		ast_cli(fd,"Keying Transmitter and sending 1000 Hz tone for 5 seconds...\n");
		if (withctcss) o->pmrChan->b.txCtcssInhibit=1;
		tune_txoutput(o,j,fd,1);
		o->pmrChan->b.txCtcssInhibit=0;
		ast_cli(fd,"DONE.\n");
		return;
	}
	for(x = 0; str[x]; x++)
	{
		if (!isdigit(str[x])) break;
	}
	if (str[x] || (sscanf(str,"%d",&i) < 1) || (i < 0) || (i > 999))
	{
		ast_cli(fd,"Entry Error, Tx Voice Level setting not changed\n");
		return;
	}
	if((o->txmixa==TX_OUT_VOICE)||(o->txmixa==TX_OUT_COMPOSITE))
	{
	 	o->txmixaset=i;
		ast_cli(fd,"Changed Tx Voice Level setting on Channel A to %d\n",o->txmixaset);
	}
	else
	{
	 	o->txmixbset=i;   
		ast_cli(fd,"Changed Tx Voice Level setting on Channel B to %d\n",o->txmixbset);
	}
	mixer_write(o);
	mult_set(o);
	if (dokey)
	{
		ast_cli(fd,"Keying Transmitter and sending 1000 Hz tone for 5 seconds...\n");
		if (!withctcss) o->pmrChan->b.txCtcssInhibit=1;
		tune_txoutput(o,i,fd,1);
		o->pmrChan->b.txCtcssInhibit=0;
		ast_cli(fd,"DONE.\n");
	}
	return;
}


static void _menu_auxvoice(int fd, struct chan_usbradio_pvt *o, char *str)
{
int	i,x;

	if( (o->txmixa!=TX_OUT_AUX) && (o->txmixb!=TX_OUT_AUX))
	{
		ast_cli(fd,"Error, No Auxvoice output configured.\n");
		return;
	}
	if (!str[0])
	{
		if(o->txmixa==TX_OUT_AUX)
			ast_cli(fd,"Current Aux Voice Level setting on Channel A is %d\n",o->txmixaset);
		else
			ast_cli(fd,"Current Aux Voice Level setting on Channel B is %d\n",o->txmixbset);
		return;
	}
	for(x = 0; str[x]; x++)
	{
		if (!isdigit(str[x])) break;
	}
	if (str[x] || (sscanf(str,"%d",&i) < 1) || (i < 0) || (i > 999))
	{
		ast_cli(fd,"Entry Error, Aux Voice Level setting not changed\n");
		return;
	}
	if(o->txmixa==TX_OUT_AUX)
	{
		o->txmixbset=i;
		ast_cli(fd,"Changed Aux Voice setting on Channel A to %d\n",o->txmixaset);
	}
	else
	{
		o->txmixbset=i;
		ast_cli(fd,"Changed Aux Voice setting on Channel B to %d\n",o->txmixbset);
	}
	mixer_write(o);
	mult_set(o);
	return;
}

static void _menu_txtone(int fd, struct chan_usbradio_pvt *o, char *cstr)
{
char	*str = cstr;
int	i,x,dokey;

	if (!str[0])
	{
		ast_cli(fd,"Current Tx CTCSS Modulation Level setting = %d\n",o->txctcssadj);
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
		for(x = 0; str[x]; x++)
		{
			if (!isdigit(str[x])) break;
		}
		if (str[x] || (sscanf(str,"%d",&i) < 1) || (i < 0) || (i > 999))
		{
			ast_cli(fd,"Entry Error, Tx CTCSS Modulation Level setting not changed\n");
			return;
		}
		o->txctcssadj = i;
		set_txctcss_level(o);
		ast_cli(fd,"Changed Tx CTCSS Modulation Level setting to %i\n",i);
	}
	if (dokey)
	{
		ast_cli(fd,"Keying Radio and sending CTCSS tone for 5 seconds...\n");
		o->txtestkey=1;
		happy_mswait(fd,5000,1);
		o->txtestkey=0;
		ast_cli(fd,"DONE.\n");
	}
	return;
}

static void tune_menusupport(int fd, struct chan_usbradio_pvt *o, char *cmd)
{
int	x,oldverbose,flatrx,txhasctcss;
struct chan_usbradio_pvt *oy = NULL;

	oldverbose = option_verbose;
	option_verbose = 0;
	flatrx = 0;
	if (o->rxdemod == RX_AUDIO_FLAT) flatrx = 1;
	txhasctcss = 0;
	if ((o->txmixa == TX_OUT_LSD) || (o->txmixa == TX_OUT_COMPOSITE) ||
		(o->txmixb == TX_OUT_LSD) ||  (o->txmixb == TX_OUT_COMPOSITE)) txhasctcss = 1;
	switch(cmd[0])
	{
	    case '0': /* return audio processing configuration */
		ast_cli(fd,"%d,%d,%d\n",flatrx,txhasctcss,o->echomode);
		break;
	    case '1': /* return usb device name list */
		for (x = 0,oy = usbradio_default.next; oy && oy->name ; oy = oy->next, x++)
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
		for (x = 0,oy = usbradio_default.next; oy && oy->name ; oy = oy->next)
		{
			if (!strcmp(oy->name,o->name)) continue;
			if (x) ast_cli(fd,",");
			ast_cli(fd,"%s",oy->name);
			x++;
		}
		ast_cli(fd,"\n");
		break;
	    case 'a':
		if (!o->hasusb)
		{
			ast_cli(fd,"Device %s currently not active\n",o->name);
			break;
		}
		tune_rxinput(fd,o,1,1);
		break;
	    case 'b':
		if (!o->hasusb)
		{
			ast_cli(fd,"Device %s currently not active\n",o->name);
			break;
		}
		do_rxdisplay(fd,o);
		break;
	    case 'c':
		if (!o->hasusb)
		{
			ast_cli(fd,"Device %s currently not active\n",o->name);
			break;
		}
		_menu_rxvoice(fd, o, cmd + 1);
		break;
	    case 'd':
		if (!o->hasusb)
		{
			ast_cli(fd,"Device %s currently not active\n",o->name);
			break;
		}
		tune_rxctcss(fd,o,1);
		break;
	    case 'e':
		if (!o->hasusb)
		{
			ast_cli(fd,"Device %s currently not active\n",o->name);
			break;
		}
		_menu_rxsquelch(fd,o,cmd + 1);
		break;
	    case 'f':
		if (!o->hasusb)
		{
			ast_cli(fd,"Device %s currently not active\n",o->name);
			break;
		}
		_menu_txvoice(fd,o,cmd + 1);
		break;
	    case 'g':
		if (!o->hasusb)
		{
			ast_cli(fd,"Device %s currently not active\n",o->name);
			break;
		}
		_menu_auxvoice(fd,o,cmd + 1);
		break;
	    case 'h':
		if (!o->hasusb)
		{
			ast_cli(fd,"Device %s currently not active\n",o->name);
			break;
		}
		_menu_txtone(fd,o,cmd + 1);
		break;
	    case 'i':
		if (!o->hasusb)
		{
			ast_cli(fd,"Device %s currently not active\n",o->name);
			break;
		}
		tune_rxvoice(fd,o,1);
		break;
	    case 'j':
		tune_write(o);
		ast_cli(fd,"Saved radio tuning settings to usbradio_tune_%s.conf\n",o->name);
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
	o->pmrChan->b.tuning=0;
	option_verbose = oldverbose;
	return;
}


static void tune_rxvoice(int fd, struct chan_usbradio_pvt *o,int intflag)
{
	const int target=7200;	 			// peak
	const int tolerance=360;	   		// peak to peak
	const float settingmin=0.1;
	const float settingmax=5;
	const float settingstart=1;
	const int maxtries=12;

	float setting;

	int tries=0, meas;

	ast_cli(fd,"INFO: RX VOICE ADJUST START.\n");
	ast_cli(fd,"target=%i tolerance=%i \n",target,tolerance);

	o->pmrChan->b.tuning=1;
	if(!o->pmrChan->spsMeasure)
		ast_cli(fd,"ERROR: NO MEASURE BLOCK.\n");

	if(!o->pmrChan->spsMeasure->source || !o->pmrChan->prxVoiceAdjust )
		ast_cli(fd,"ERROR: NO SOURCE OR MEASURE SETTING.\n");

	o->pmrChan->spsMeasure->source=o->pmrChan->spsRxOut->sink;
	o->pmrChan->spsMeasure->enabled=1;
	o->pmrChan->spsMeasure->discfactor=1000;

	setting=settingstart;

	// ast_cli(fd,"ERROR: NO MEASURE BLOCK.\n");

	while(tries<maxtries)
	{
		*(o->pmrChan->prxVoiceAdjust)=setting*M_Q8;
		if (happy_mswait(fd,10,intflag))
		{
			o->pmrChan->b.tuning=0;
			return;
		}
    	o->pmrChan->spsMeasure->amax = o->pmrChan->spsMeasure->amin = 0;
		if (happy_mswait(fd,1000,intflag))
		{
			o->pmrChan->b.tuning=0;
			return;
		}
		meas = o->pmrChan->spsMeasure->apeak;
		ast_cli(fd,"tries=%i, setting=%f, meas=%i\n",tries,setting,meas);

		if( meas<(target-tolerance) || meas>(target+tolerance) || tries<3){
			setting=setting*target/meas;
		}
		else if(tries>4 && meas>(target-tolerance) && meas<(target+tolerance) )
		{
			break;
		}
		if(setting<settingmin)setting=settingmin;
		else if(setting>settingmax)setting=settingmax;

		tries++;
	}

	o->pmrChan->spsMeasure->enabled=0;

	ast_cli(fd,"DONE tries=%i, setting=%f, meas=%f\n",tries,setting,(float)meas);
	if( meas<(target-tolerance) || meas>(target+tolerance) ){
		ast_cli(fd,"ERROR: RX VOICE GAIN ADJUST FAILED.\n");
	}else{
		ast_cli(fd,"INFO: RX VOICE GAIN ADJUST SUCCESS.\n");
		o->rxvoiceadj=setting;
	}
	o->pmrChan->b.tuning=0;
}
/*
*/
static void tune_rxctcss(int fd, struct chan_usbradio_pvt *o,int intflag)
{
	const int target=2400;		 // was 4096 pre 20080205
	const int tolerance=100;
	const float settingmin=0.1;
	const float settingmax=8;
	const float settingstart=1;
	const int maxtries=12;

	float setting;
	int tries=0,meas;

	ast_cli(fd,"INFO: RX CTCSS ADJUST START.\n");
	ast_cli(fd,"target=%i tolerance=%i \n",target,tolerance);

	o->pmrChan->b.tuning=1;
	o->pmrChan->spsMeasure->source=o->pmrChan->prxCtcssMeasure;
	o->pmrChan->spsMeasure->discfactor=400;
	o->pmrChan->spsMeasure->enabled=1;

	setting=settingstart;

	while(tries<maxtries)
	{
		*(o->pmrChan->prxCtcssAdjust)=setting*M_Q8;
		if (happy_mswait(fd,10,intflag))
		{
			o->pmrChan->b.tuning=0;
			return;
		}
    	o->pmrChan->spsMeasure->amax = o->pmrChan->spsMeasure->amin = 0;
		if (happy_mswait(fd,500,intflag))
		{
			o->pmrChan->b.tuning=0;
			return;
		}
		meas = o->pmrChan->spsMeasure->apeak;
		ast_cli(fd,"tries=%i, setting=%f, meas=%i\n",tries,setting,meas);

		if( meas<(target-tolerance) || meas>(target+tolerance) || tries<3){
			setting=setting*target/meas;
		}
		else if(tries>4 && meas>(target-tolerance) && meas<(target+tolerance) )
		{
			break;
		}
		if(setting<settingmin)setting=settingmin;
		else if(setting>settingmax)setting=settingmax;

		tries++;
	}
	o->pmrChan->spsMeasure->enabled=0;
	ast_cli(fd,"DONE tries=%i, setting=%f, meas=%.2f\n",tries,setting,(float)meas);
	if( meas<(target-tolerance) || meas>(target+tolerance) ){
		ast_cli(fd,"ERROR: RX CTCSS GAIN ADJUST FAILED.\n");
	}else{
		ast_cli(fd,"INFO: RX CTCSS GAIN ADJUST SUCCESS.\n");
		o->rxctcssadj=setting;
	}

	if(o->rxcdtype==CD_XPMR_NOISE){
		int normRssi;

		if (happy_mswait(fd,200,intflag))
		{
			o->pmrChan->b.tuning=0;
			return;
		}
		normRssi=((32767-o->pmrChan->rxRssi)*1000/32767);

		if(o->rxsquelchadj>normRssi)
			ast_cli(fd,"WARNING: RSSI=%i SQUELCH=%i and is too tight. Use 'radio tune rxsquelch'.\n",normRssi,o->rxsquelchadj);
		else
			ast_cli(fd,"INFO: RX RSSI=%i\n",normRssi);

	}
	o->pmrChan->b.tuning=0;
}
/*
	after radio tune is performed data is serialized here 
*/
static void tune_write(struct chan_usbradio_pvt *o)
{
	FILE *fp;
	char fname[200];

 	snprintf(fname,sizeof(fname) - 1,"/etc/asterisk/usbradio_tune_%s.conf",o->name);
	fp = fopen(fname,"w");

	fprintf(fp,"[%s]\n",o->name);

	fprintf(fp,"; name=%s\n",o->name);
	fprintf(fp,"; devicenum=%i\n",o->devicenum);
	ast_mutex_lock(&usb_dev_lock);
	fprintf(fp,"devstr=%s\n",o->devstr);
	ast_mutex_unlock(&usb_dev_lock);
	fprintf(fp,"rxmixerset=%i\n",o->rxmixerset);
	fprintf(fp,"txmixaset=%i\n",o->txmixaset);
	fprintf(fp,"txmixbset=%i\n",o->txmixbset);
	fprintf(fp,"rxvoiceadj=%f\n",o->rxvoiceadj);
	fprintf(fp,"rxctcssadj=%f\n",o->rxctcssadj);
	fprintf(fp,"txctcssadj=%i\n",o->txctcssadj);
	fprintf(fp,"rxsquelchadj=%i\n",o->rxsquelchadj);
	fprintf(fp,"fever=%i\n",o->fever);
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
		memcpy(&o->eeprom[EEPROM_RXVOICEADJ],&o->rxvoiceadj,sizeof(float));
		memcpy(&o->eeprom[EEPROM_RXCTCSSADJ],&o->rxctcssadj,sizeof(float));
		o->eeprom[EEPROM_TXCTCSSADJ] = o->txctcssadj;
		o->eeprom[EEPROM_RXSQUELCHADJ] = o->rxsquelchadj;
		o->eepromctl = 2;  /* request a write */
		ast_mutex_unlock(&o->eepromlock);
	}
}
//
static void mixer_write(struct chan_usbradio_pvt *o)
{
	setamixer(o->devicenum,MIXER_PARAM_MIC_PLAYBACK_SW,0,0);
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
	setamixer(o->devicenum,(o->newname) ? MIXER_PARAM_SPKR_PLAYBACK_SW_NEW : MIXER_PARAM_SPKR_PLAYBACK_SW,1,0);
	setamixer(o->devicenum,(o->newname) ? MIXER_PARAM_SPKR_PLAYBACK_VOL_NEW : MIXER_PARAM_SPKR_PLAYBACK_VOL,
		make_spkr_playback_value(o,o->txmixaset),
		make_spkr_playback_value(o,o->txmixbset));
	setamixer(o->devicenum,MIXER_PARAM_MIC_CAPTURE_VOL,
		o->rxmixerset * o->micmax / 1000,0);
	setamixer(o->devicenum,MIXER_PARAM_MIC_BOOST,o->rxboostset,0);
	setamixer(o->devicenum,MIXER_PARAM_MIC_CAPTURE_SW,1,0);
}
/*
	adjust dsp multiplier to add resolution to tx level adjustment
*/
static void mult_set(struct chan_usbradio_pvt *o)
{

	if(o->pmrChan->spsTxOutA) {
		o->pmrChan->spsTxOutA->outputGain = 
			mult_calc((o->txmixaset * 152) / 1000);
	}
	if(o->pmrChan->spsTxOutB){
		o->pmrChan->spsTxOutB->outputGain = 
			mult_calc((o->txmixbset * 152) / 1000);
	}
}
//
// input 0 - 151 outputs are pot and multiplier
//
static int mult_calc(int value)
{
	const int multx=M_Q8;
	int pot,mult;

	pot=((int)(value/4)*4)+2;
	mult = multx-( ( multx * (3-(value%4)) ) / (pot+2) );
	return(mult);
}

#define pd(x) {printf(#x" = %d\n",x);}
#define pp(x) {printf(#x" = %p\n",x);}
#define ps(x) {printf(#x" = %s\n",x);}
#define pf(x) {printf(#x" = %f\n",x);}


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

	if(opt)
	{
		struct usb_device *usb_dev;

		usb_dev = hid_device_init(o->devstr);
		if (usb_dev == NULL) {
			ast_log(LOG_ERROR,"USB HID device not found\n");
			return -1;
		}
		o->usb_handle = usb_open(usb_dev);
		if (o->usb_handle == NULL) {
		    ast_log(LOG_ERROR,"Not able to open USB device\n");
			return -1;
		}
		if (usb_claim_interface(o->usb_handle,C108_HID_INTERFACE) < 0)
		{
		    if (usb_detach_kernel_driver_np(o->usb_handle,C108_HID_INTERFACE) < 0) {
			    ast_log(LOG_ERROR,"Not able to detach the USB device\n");
				return -1;
			}
			if (usb_claim_interface(o->usb_handle,C108_HID_INTERFACE) < 0) {
				ast_log(LOG_ERROR,"Not able to claim the USB device\n");
				return -1;
			}
		}
	
		memset(buf,0,sizeof(buf));
		buf[2] = o->hid_gpio_ctl;
		buf[1] = 0;
		hid_set_outputs(o->usb_handle,buf);
		memcpy(bufsave,buf,sizeof(buf));
	 
		buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
		o->lasttx=0;
	}

	/* if change in tx state as controlled by xpmr */
	txtmp=o->pmrChan->txPttOut;
			
	if (o->lasttx != txtmp)
	{
		o->pmrChan->txPttHid=o->lasttx = txtmp;
		if(o->debuglevel)printf("usbhid: tx set to %d\n",txtmp);
		o->hid_gpio_val &= ~o->hid_io_ptt;
		if (!o->invertptt)
		{
			if (txtmp) o->hid_gpio_val |= o->hid_io_ptt;
		}
		else
		{
			if (!txtmp) o->hid_gpio_val |= o->hid_io_ptt;
		}
		buf[o->hid_gpio_loc] = o->hid_gpio_val;
		buf[o->hid_gpio_ctl_loc] = o->hid_gpio_ctl;
		hid_set_outputs(o->usb_handle,buf);
	}

	return(0);
}
#endif
/*
*/
static void pmrdump(struct chan_usbradio_pvt *o)
{
	t_pmr_chan *p;
	int i;

	p=o->pmrChan;

	printf("\nodump()\n");

	pd(o->devicenum);
	ast_mutex_lock(&usb_dev_lock);
	ps(o->devstr);
	ast_mutex_unlock(&usb_dev_lock);

	pd(o->micmax);
	pd(o->spkrmax);

	pd(o->rxdemod);
	pd(o->rxcdtype);
	pd(o->rxsdtype);
	pd(o->txtoctype);

	pd(o->rxmixerset);
	pd(o->rxboostset);
	pd(o->txboostset);

	pf(o->rxvoiceadj);
	pf(o->rxctcssadj);
	pd(o->rxsquelchadj);

	ps(o->txctcssdefault);
	ps(o->txctcssfreq);

	pd(o->numrxctcssfreqs);
	if(o->numrxctcssfreqs>0)
	{
		for(i=0;i<o->numrxctcssfreqs;i++)
		{
			printf(" %i =  %s  %s\n",i,o->rxctcss[i],o->txctcss[i]); 
		}
	}

	pd(o->b.rxpolarity);
	pd(o->b.txpolarity);

	pd(o->txlimonly);
	pd(o->txprelim);
	pd(o->txmixa);
	pd(o->txmixb);
	
	pd(o->txmixaset);
	pd(o->txmixbset);
	
	printf("\npmrdump()\n");
 
	pd(p->devicenum);

	printf("prxSquelchAdjust=%i\n",*(o->pmrChan->prxSquelchAdjust));

	pd(p->rxCarrierPoint);
	pd(p->rxCarrierHyst);

	pd(*p->prxVoiceAdjust);
	pd(*p->prxCtcssAdjust);

	pd(p->rxfreq);
	pd(p->txfreq);

	pd(p->rxCtcss->relax);
	//pf(p->rxCtcssFreq);	
	pd(p->numrxcodes);
	if(o->pmrChan->numrxcodes>0)
	{
		for(i=0;i<o->pmrChan->numrxcodes;i++)
		{
			printf(" %i = %s\n",i,o->pmrChan->pRxCode[i]); 
		}
	}

	pd(p->txTocType);
	ps(p->pTxCodeDefault);
	pd(p->txcodedefaultsmode);
	pd(p->numtxcodes);
	if(o->pmrChan->numtxcodes>0)
	{
		for(i=0;i<o->pmrChan->numtxcodes;i++)
		{													 
			printf(" %i = %s\n",i,o->pmrChan->pTxCode[i]); 
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

	if(p->spsTxOutA)pd(p->spsTxOutA->outputGain);
	if(p->spsTxOutB)pd(p->spsTxOutB->outputGain);
	pd(p->txPttIn);
	pd(p->txPttOut);

	pd(p->tracetype);
	pd(p->b.radioactive);
	pd(p->b.txboost);
	pd(p->b.txCtcssOff);

	return;
}
/*
	takes data from a chan_usbradio_pvt struct (e.g. o->)
	and configures the xpmr radio layer
*/
static int xpmr_config(struct chan_usbradio_pvt *o)
{
	//ast_log(LOG_NOTICE,"xpmr_config()\n");

	TRACEO(1,("xpmr_config()\n"));

	if(o->pmrChan==NULL)
	{
		ast_log(LOG_ERROR,"pmr channel structure NULL\n");
		return 1;
	}

	o->pmrChan->rxCtcss->relax = o->rxctcssrelax;
	o->pmrChan->txpower=0;

	if(o->b.remoted)
	{
		o->pmrChan->pTxCodeDefault = o->set_txctcssdefault;
		o->pmrChan->pRxCodeSrc=o->set_rxctcssfreqs;
		o->pmrChan->pTxCodeSrc=o->set_txctcssfreqs;

		o->pmrChan->rxfreq=o->set_rxfreq;
		o->pmrChan->txfreq=o->set_txfreq;
		/* printf(" remoted %s %s --> %s \n",o->pmrChan->txctcssdefault,
			o->pmrChan->txctcssfreq,o->pmrChan->rxctcssfreq); */
	}
	else
	{
		// set xpmr pointers to source strings

		o->pmrChan->pTxCodeDefault = o->txctcssdefault;
		o->pmrChan->pRxCodeSrc     = o->rxctcssfreqs;
		o->pmrChan->pTxCodeSrc     = o->txctcssfreqs;
	
		o->pmrChan->rxfreq = o->rxfreq;
		o->pmrChan->txfreq = o->txfreq;
	}

	if(o->b.forcetxcode)
	{
		o->pmrChan->pTxCodeDefault = o->set_txctcssfreq;
		if(o->debuglevel)ast_verbose("chan_usbradio dev=%s Forced Tx Squelch Code code=%s\n",o->name,o->pmrChan->pTxCodeDefault);
	}
		
	code_string_parse(o->pmrChan);
	if(o->pmrChan->rxfreq) o->pmrChan->b.reprog=1;

	return 0;
}
/*
 * grab fields from the config file, init the descriptor and open the device.
 */
static struct chan_usbradio_pvt *store_config(struct ast_config *cfg, char *ctg, int *indexp)
{
	struct ast_variable *v;
	struct chan_usbradio_pvt *o;
	struct ast_config *cfg1;
	char fname[200],buf[100];
	int i;
#ifdef	NEW_ASTERISK
	struct ast_flags zeroflag = {0};
#endif
	if (ctg == NULL) {
		traceusb1((" store_config() ctg == NULL\n"));
		o = &usbradio_default;
		ctg = "general";
	} else {
		/* "general" is also the default thing */
		if (strcmp(ctg, "general") == 0) {
			o = &usbradio_default;
		} else {
		    // ast_log(LOG_NOTICE,"ast_calloc for chan_usbradio_pvt of %s\n",ctg);
			if (!(o = ast_calloc(1, sizeof(*o))))
				return NULL;
			*o = usbradio_default;
			o->name = ast_strdup(ctg);
			o->index = (*indexp)++;
			if (!usbradio_active) 
				usbradio_active = o->name;
		}
	}
	ast_mutex_init(&o->eepromlock);
	strcpy(o->mohinterpret, "default");
	/* fill other fields from configuration */
	for (v = ast_variable_browse(cfg, ctg); v; v = v->next) {
		M_START((char *)v->name, (char *)v->value);

		/* handle jb conf */
		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value))
			continue;

#if	0
			M_BOOL("autoanswer", o->autoanswer)
			M_BOOL("autohangup", o->autohangup)
			M_BOOL("overridecontext", o->overridecontext)
			M_STR("context", o->ctx)
			M_STR("language", o->language)
			M_STR("mohinterpret", o->mohinterpret)
			M_STR("extension", o->ext)
			M_F("callerid", store_callerid(o, v->value))
#endif
			M_UINT("frags", o->frags)
			M_UINT("queuesize",o->queuesize)
#if 0
			M_UINT("devicenum",o->devicenum)
#endif
			M_UINT("debug", usbradio_debug)
			M_BOOL("rxcpusaver",o->rxcpusaver)
			M_BOOL("txcpusaver",o->txcpusaver)
			M_BOOL("invertptt",o->invertptt)
			M_F("rxdemod",store_rxdemod(o,(char *)v->value))
			M_BOOL("txlimonly",o->txlimonly);
			M_BOOL("txprelim",o->txprelim);
			M_F("txmixa",store_txmixa(o,(char *)v->value))
			M_F("txmixb",store_txmixb(o,(char *)v->value))
			M_F("carrierfrom",store_rxcdtype(o,(char *)v->value))
			M_F("ctcssfrom",store_rxsdtype(o,(char *)v->value))
		        M_UINT("rxsqvox",o->rxsqvoxadj)
		        M_UINT("rxsqhyst",o->rxsqhyst)
		        M_UINT("rxnoisefiltype",o->rxnoisefiltype)
		        M_UINT("rxsquelchdelay",o->rxsquelchdelay)
			M_STR("txctcssdefault",o->txctcssdefault)
			M_STR("rxctcssfreqs",o->rxctcssfreqs)
			M_STR("txctcssfreqs",o->txctcssfreqs)
			M_BOOL("rxctcssoverride",o->rxctcssoverride);
			M_UINT("rxfreq",o->rxfreq)
			M_UINT("txfreq",o->txfreq)
			M_F("rxgain",store_rxgain(o,(char *)v->value))
 			M_BOOL("rxboost",o->rxboostset)
 			M_BOOL("txboost",o->txboostset)
			M_UINT("rxctcssrelax",o->rxctcssrelax)
			M_F("txtoctype",store_txtoctype(o,(char *)v->value))
			M_UINT("hdwtype",o->hdwtype)
			M_UINT("eeprom",o->wanteeprom)
			M_UINT("duplex",o->radioduplex)
			M_UINT("txsettletime",o->txsettletime)
			M_UINT("txrxblankingtime",o->txrxblankingtime)
			M_BOOL("rxpolarity",o->b.rxpolarity)
			M_BOOL("txpolarity",o->b.txpolarity)
			M_BOOL("dcsrxpolarity",o->b.dcsrxpolarity)
			M_BOOL("dcstxpolarity",o->b.dcstxpolarity)
			M_BOOL("lsdrxpolarity",o->b.lsdrxpolarity)
			M_BOOL("lsdtxpolarity",o->b.lsdtxpolarity)
			M_BOOL("loopback",o->b.loopback)
			M_BOOL("radioactive",o->b.radioactive)
			M_UINT("rptnum",o->rptnum)
			M_UINT("idleinterval",o->idleinterval)
			M_UINT("turnoffs",o->turnoffs)
			M_UINT("tracetype",o->tracetype)
			M_UINT("tracelevel",o->tracelevel)
			M_UINT("rxondelay",o->rxondelay);
			M_UINT("txoffdelay",o->txoffdelay);
			M_UINT("area",o->area)
			M_STR("ukey",o->ukey)
 			M_UINT("duplex3",o->duplex3)

        
            M_UINT("rxlpf",o->rxlpf)
            M_UINT("rxhpf",o->rxhpf)
            M_UINT("txlpf",o->txlpf)
            M_UINT("txhpf",o->txhpf)
//            ast_log(LOG_NOTICE,"rxlpf: %d\n",o->rxlpf);
//            ast_log(LOG_NOTICE,"rxhpf: %d\n",o->rxhpf);
//            ast_log(LOG_NOTICE,"txlpf: %d\n",o->txlpf);
//            ast_log(LOG_NOTICE,"txhpf: %d\n",o->txhpf);
        
			M_UINT("sendvoter",o->sendvoter)
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
				if (!strcmp(v->name,buf)) {
					o->pps[i] = strdup(v->value);
					haspp = 1;
				}
			}
	}

	o->debuglevel=0;

	if (o->rxsdtype != SD_XPMR)
	{
		o->rxctcssfreqs[0] = 0;
	}

	if ((o->txmixa == TX_OUT_COMPOSITE) && (o->txmixb == TX_OUT_VOICE))
	{
		ast_log(LOG_WARNING,"Invalid Configuration: Can not have B channel be Voice with A channel being Composite!!\n");
	}
	if ((o->txmixb == TX_OUT_COMPOSITE) && (o->txmixa == TX_OUT_VOICE))
	{
		ast_log(LOG_WARNING,"Invalid Configuration: Can not have A channel be Voice with B channel being Composite!!\n");
	}

	if (o == &usbradio_default)		/* we are done with the default */
		return NULL;

	for(i = 2; i <= 9; i++)
	{
		/* skip if this one not specified */
		if (!o->pps[i]) continue;
		/* skip if not out */
		if (strncasecmp(o->pps[i],"out",3)) continue;
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
	o->rxvoiceadj = 0.5;
	o->rxctcssadj = 0.5;
	o->txctcssadj = 200;
	o->rxsquelchadj = 725;
	o->devstr[0] = 0;
	if (cfg1) {
		for (v = ast_variable_browse(cfg1, o->name); v; v = v->next) {
	
			M_START((char *)v->name, (char *)v->value);
			M_UINT("rxmixerset", o->rxmixerset)
			M_UINT("txmixaset", o->txmixaset)
			M_UINT("txmixbset", o->txmixbset)
			M_F("rxvoiceadj",store_rxvoiceadj(o,(char *)v->value))
			M_F("rxctcssadj",store_rxctcssadj(o,(char *)v->value))
			M_UINT("txctcssadj",o->txctcssadj);
			M_UINT("rxsquelchadj", o->rxsquelchadj)
			M_UINT("fever", o->fever)
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
        if(o->rxsqhyst==0)
		o->rxsqhyst=3000;

        if(o->rxsquelchdelay>RXSQDELAYBUFSIZE/8-1)
	{
	        ast_log(LOG_WARNING,"rxsquelchdelay of %i is > maximum of %i. Set to maximum.\n",
			o->rxsquelchdelay,RXSQDELAYBUFSIZE/8-1);
		o->rxsquelchdelay=RXSQDELAYBUFSIZE/8-1;
        }
	if(o->pmrChan==NULL)
	{
		t_pmr_chan tChan;

		// ast_log(LOG_NOTICE,"createPmrChannel() %s\n",o->name);
		memset(&tChan,0,sizeof(t_pmr_chan));

		tChan.pTxCodeDefault = o->txctcssdefault;
		tChan.pRxCodeSrc     = o->rxctcssfreqs;
		tChan.pTxCodeSrc     = o->txctcssfreqs;

		tChan.rxDemod=o->rxdemod;
		tChan.rxCdType=o->rxcdtype;
		tChan.rxCarrierHyst=o->rxsqhyst;
		tChan.rxSqVoxAdj=o->rxsqvoxadj;
		tChan.rxSquelchDelay=o->rxsquelchdelay;

		if (o->txlimonly) 
			tChan.txMod = 1;
		if (o->txprelim) 
			tChan.txMod = 2;

		tChan.txMixA = o->txmixa;
		tChan.txMixB = o->txmixb;

		tChan.rxCpuSaver=o->rxcpusaver;
		tChan.txCpuSaver=o->txcpusaver;

		tChan.b.rxpolarity=o->b.rxpolarity;
		tChan.b.txpolarity=o->b.txpolarity;

		tChan.b.dcsrxpolarity=o->b.dcsrxpolarity;
		tChan.b.dcstxpolarity=o->b.dcstxpolarity;

		tChan.b.lsdrxpolarity=o->b.lsdrxpolarity;
		tChan.b.lsdtxpolarity=o->b.lsdtxpolarity;

		tChan.b.txboost=o->txboostset;
		tChan.tracetype=o->tracetype;
		tChan.tracelevel=o->tracelevel;

		tChan.rptnum=o->rptnum;
		tChan.idleinterval=o->idleinterval;
		tChan.turnoffs=o->turnoffs;
		tChan.area=o->area;
		tChan.ukey=o->ukey;
		tChan.name=o->name;
		tChan.fever = o->fever;

        tChan.rxhpf=o->rxhpf;
        tChan.rxlpf=o->rxlpf;
        tChan.txhpf=o->txhpf;
        tChan.txlpf=o->txlpf;
        
		o->pmrChan=createPmrChannel(&tChan,FRAME_SIZE);
									 
		o->pmrChan->radioDuplex=o->radioduplex;
		o->pmrChan->b.loopback=0; 
		o->pmrChan->b.radioactive=o->b.radioactive;
		o->pmrChan->txsettletime=o->txsettletime;
		o->pmrChan->txrxblankingtime=o->txrxblankingtime;
		o->pmrChan->rxCpuSaver=o->rxcpusaver;
		o->pmrChan->txCpuSaver=o->txcpusaver;

		*(o->pmrChan->prxSquelchAdjust) = 
			((999 - o->rxsquelchadj) * 32767) / 1000;

		*(o->pmrChan->prxVoiceAdjust)=o->rxvoiceadj*M_Q8;
		*(o->pmrChan->prxCtcssAdjust)=o->rxctcssadj*M_Q8;
		o->pmrChan->rxCtcss->relax=o->rxctcssrelax;
		o->pmrChan->txTocType = o->txtoctype;

#if 0
        if (    (o->txmixa == TX_OUT_LSD) ||
                (o->txmixa == TX_OUT_COMPOSITE) ||
                (o->txmixb == TX_OUT_LSD) ||
                (o->txmixb == TX_OUT_COMPOSITE))
        {
                set_txctcss_level(o);
        }
#endif
		if( (o->txmixa!=TX_OUT_VOICE) && (o->txmixb!=TX_OUT_VOICE) &&
			(o->txmixa!=TX_OUT_COMPOSITE) && (o->txmixb!=TX_OUT_COMPOSITE)
		  )
		{
			ast_log(LOG_ERROR,"No txvoice output configured.\n");
		}
	
		if( o->txctcssfreq[0] && 
		    o->txmixa!=TX_OUT_LSD && o->txmixa!=TX_OUT_COMPOSITE  &&
			o->txmixb!=TX_OUT_LSD && o->txmixb!=TX_OUT_COMPOSITE
		  )
		{
			ast_log(LOG_ERROR,"No txtone output configured.\n");
		}
		
		if(o->b.radioactive)
		{
			struct chan_usbradio_pvt *ao;
			for (ao = usbradio_default.next; ao && ao->name ; ao = ao->next)ao->pmrChan->b.radioactive=0;
			usbradio_active = o->name;
			o->pmrChan->b.radioactive=1;
			ast_log(LOG_NOTICE,"radio active set to [%s]\n",o->name);
		}
	}

#if 0
	xpmr_config(o);

	TRACEO(1,("store_config() 120\n"));
	mixer_write(o);
	TRACEO(1,("store_config() 130\n"));
	mult_set(o);    
#endif
	TRACEO(1,("store_config() 140\n"));
	hidhdwconfig(o);

	TRACEO(1,("store_config() 200\n"));

#ifndef	NEW_ASTERISK
	if (pipe(o->sndcmd) != 0) {
		ast_log(LOG_ERROR, "Unable to create pipe\n");
		goto error;
	}

	ast_pthread_create_background(&o->sthread, NULL, sound_thread, o);
#endif

	/* link into list of devices */
	if (o != &usbradio_default) {
		o->next = usbradio_default.next;
		usbradio_default.next = o;
	}
	TRACEO(1,("store_config() complete\n"));
	return o;
  
  error:
	if (o != &usbradio_default)
		free(o);
	return NULL;
}


#if	DEBUG_FILETEST == 1
/*
	Test It on a File
*/
int RxTestIt(struct chan_usbradio_pvt *o)
{
	const int numSamples = SAMPLES_PER_BLOCK;
	const int numChannels = 16;

	i16 sample,i,ii;
	
	i32 txHangTime;

	i16 txEnable;

	t_pmr_chan	tChan;
	t_pmr_chan *pChan;

	FILE *hInput=NULL, *hOutput=NULL, *hOutputTx=NULL;
 
	i16 iBuff[numSamples*2*6], oBuff[numSamples];
				  
	printf("RxTestIt()\n");

	pChan=o->pmrChan;
	pChan->b.txCapture=1;
	pChan->b.rxCapture=1;

	txEnable = 0;

	hInput  = fopen("/usr/src/xpmr/testdata/rx_in.pcm","r");
	if(!hInput){
		printf(" RxTestIt() File Not Found.\n");
		return 0;
	}
	hOutput = fopen("/usr/src/xpmr/testdata/rx_debug.pcm","w");

	printf(" RxTestIt() Working...\n");
			 	
	while(!feof(hInput))
	{
		fread((void *)iBuff,2,numSamples*2*6,hInput);
		 
		if(txHangTime)txHangTime-=numSamples;
		if(txHangTime<0)txHangTime=0;
		
		if(pChan->rxCtcss->decode)txHangTime=(8000/1000*2000);

		if(pChan->rxCtcss->decode && !txEnable)
		{
			txEnable=1;
			//pChan->inputBlanking=(8000/1000*200);
		}
		else if(!pChan->rxCtcss->decode && txEnable)
		{
			txEnable=0;	
		}

		PmrRx(pChan,iBuff,oBuff);

		fwrite((void *)pChan->prxDebug,2,numSamples*numChannels,hOutput);
	}
	pChan->b.txCapture=0;
	pChan->b.rxCapture=0;

	if(hInput)fclose(hInput);
	if(hOutput)fclose(hOutput);

	printf(" RxTestIt() Complete.\n");

	return 0;
}
#endif

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
                e->command = "radio key";
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
                e->command = "radio unkey";
                e->usage = unkey_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(console_unkey(a->fd,a->argc,a->argv));
}

static char *handle_radio_tune(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "radio tune";
                e->usage = radio_tune_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(radio_tune(a->fd,a->argc,a->argv));
}

static char *handle_radio_debug(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "radio debug";
                e->usage = radio_tune_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(radio_set_debug(a->fd,a->argc,a->argv));
}

static char *handle_radio_debug_off(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "radio debug off";
                e->usage = radio_tune_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(radio_set_debug_off(a->fd,a->argc,a->argv));
}

static char *handle_radio_active(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "radio active";
                e->usage = active_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(radio_active(a->fd,a->argc,a->argv));
}

static char *handle_set_xdebug(struct ast_cli_entry *e,
	int cmd, struct ast_cli_args *a)
{
        switch (cmd) {
        case CLI_INIT:
                e->command = "radio set xdebug";
                e->usage = active_usage;
                return NULL;
        case CLI_GENERATE:
                return NULL;
	}
	return res2cli(radio_set_xpmr_debug(a->fd,a->argc,a->argv));
}


static struct ast_cli_entry cli_usbradio[] = {
	AST_CLI_DEFINE(handle_console_key,"Simulate Rx Signal Present"),
	AST_CLI_DEFINE(handle_console_unkey,"Simulate Rx Signal Loss"),
	AST_CLI_DEFINE(handle_radio_tune,"Radio Tune"),
	AST_CLI_DEFINE(handle_radio_debug,"Radio Debug On"),
	AST_CLI_DEFINE(handle_radio_debug_off,"Radio Debug Off"),
	AST_CLI_DEFINE(handle_radio_active,"Change commanded device"),
	AST_CLI_DEFINE(handle_set_xdebug,"Radio set xpmr debug level")
};

#endif

#include "./xpmr/xpmr.c"
#ifdef HAVE_XPMRX
#include "./xpmrx/xpmrx.c"
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

	usbradio_active = NULL;

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	/* load config file */
#ifdef	NEW_ASTERISK
	if (!(cfg = ast_config_load(config,zeroflag))) {
#else
	if (!(cfg = ast_config_load(config))) {
#endif
		ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
		return AST_MODULE_LOAD_DECLINE;
	}

	pp_val = 0;
	hasout = 0;

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

	if (find_desc(usbradio_active) == NULL) {
		ast_log(LOG_NOTICE, "radio active device %s not found\n", usbradio_active);
		/* XXX we could default to 'dsp' perhaps ? */
		/* XXX should cleanup allocated memory etc. */
		return AST_MODULE_LOAD_FAILURE;
	}

	if (ast_channel_register(&usbradio_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel type 'usb'\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	ast_cli_register_multiple(cli_usbradio, sizeof(cli_usbradio) / sizeof(struct ast_cli_entry));

	if (haspp && hasout) ast_pthread_create_background(&pulserid, NULL, pulserthread, NULL);

	return AST_MODULE_LOAD_SUCCESS;
}
/*
*/
static int unload_module(void)
{
	struct chan_usbradio_pvt *o;

	ast_log(LOG_WARNING, "unload_module() called\n");

	stoppulser = 1;

	ast_channel_unregister(&usbradio_tech);
	ast_cli_unregister_multiple(cli_usbradio, sizeof(cli_usbradio) / sizeof(struct ast_cli_entry));

	for (o = usbradio_default.next; o; o = o->next) {

		ast_log(LOG_WARNING, "destroyPmrChannel() called\n");
		if(o->pmrChan)destroyPmrChannel(o->pmrChan);
		
		#if DEBUG_CAPTURES == 1
		if (frxcapraw) { fclose(frxcapraw); frxcapraw = NULL; }
		if (frxcaptrace) { fclose(frxcaptrace); frxcaptrace = NULL; }
		if (frxoutraw) { fclose(frxoutraw); frxoutraw = NULL; }
		if (ftxcapraw) { fclose(ftxcapraw); ftxcapraw = NULL; }
		if (ftxcaptrace) { fclose(ftxcaptrace); ftxcaptrace = NULL; }
		if (ftxoutraw) { fclose(ftxoutraw); ftxoutraw = NULL; }
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

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "usb Console Channel Driver");

/*	end of file */


