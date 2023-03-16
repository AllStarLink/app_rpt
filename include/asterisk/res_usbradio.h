/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2023, Naveen Albert
 *
 * Based upon previous code by:
 * Jim Dixon, WB6NIL <jim@lambdatel.com>
 * Steve Henke, W9SH  <w9sh@arrl.net>
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

/*! \note <sys/io.h> is not portable to all architectures, so don't call non-portable functions if we don't have them */
#if defined(__alpha__) || defined(__x86_64__) || defined(__ia64__)
#define HAVE_SYS_IO
#else
#warning sys.io is not available on this architecture and some functionality will be disabled
#endif

#define	MIXER_PARAM_MIC_PLAYBACK_SW "Mic Playback Switch"
#define MIXER_PARAM_MIC_PLAYBACK_VOL "Mic Playback Volume"
#define	MIXER_PARAM_MIC_CAPTURE_SW "Mic Capture Switch"
#define	MIXER_PARAM_MIC_CAPTURE_VOL "Mic Capture Volume"
#define	MIXER_PARAM_MIC_BOOST "Auto Gain Control"
#define	MIXER_PARAM_SPKR_PLAYBACK_SW "Speaker Playback Switch"
#define	MIXER_PARAM_SPKR_PLAYBACK_VOL "Speaker Playback Volume"
#define	MIXER_PARAM_SPKR_PLAYBACK_SW_NEW "Headphone Playback Switch"
#define	MIXER_PARAM_SPKR_PLAYBACK_VOL_NEW "Headphone Playback Volume"

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
 *		Overridden by the 'frags' parameter.
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
 *		Overridden by the 'queuesize' parameter.
 *
 *		Should be >=2, and at most as large as the hw queue above
 *		(otherwise it will never be full).
 */

#define FRAME_SIZE	160


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

struct usbecho {
	struct qelem *q_forw;
	struct qelem *q_prev;
	short data[FRAME_SIZE];
};

long ast_radio_lround(double x);

int ast_radio_make_spkr_playback_value(int spkrmax, int val, int devtype);


// Note: must add -lasound to end of linkage

/*!
 * \param devnum alsa major device number
 * \param ascii Formal Parameter Name, val1, first or only value, val2 second value, or 0 if only 1 value. Values: 0-99 (percent) or 0-1 for baboon.
 */
int ast_radio_amixer_max(int devnum, char *param);

/*!
 * \param devnum alsa major device number
 * \param ascii Formal Parameter Name, val1, first or only value, val2 second value, or 0 if only 1 value. Values: 0-99 (percent) or 0-1 for baboon.
 */
int ast_radio_setamixer(int devnum, char *param, int v1, int v2);

void ast_radio_hid_set_outputs(struct usb_dev_handle *handle, unsigned char *outputs);

void ast_radio_hid_get_inputs(struct usb_dev_handle *handle, unsigned char *inputs);

unsigned short ast_radio_get_eeprom(struct usb_dev_handle *handle, unsigned short *buf);

void ast_radio_put_eeprom(struct usb_dev_handle *handle, unsigned short *buf);

struct usb_device *ast_radio_hid_device_init(char *desired_device);

/*! \brief Get internal formatted string from external one */
int ast_radio_usb_get_usbdev(char *devstr);

int ast_radio_load_parallel_port(int *haspp, int *ppfd, int *pbase, const char *pport, int reload);

unsigned char ast_radio_ppread(int haspp, unsigned int ppfd, unsigned int pbase, const char *pport);

void ast_radio_ppwrite(int haspp, unsigned int ppfd, unsigned int pbase, const char *pport, unsigned char c);
