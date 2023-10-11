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
 
 /*! \file
 * \brief USB sound card resources.
 */

/*! \note <sys/io.h> is not portable to all architectures, so don't call non-portable functions if we don't have them */
#if defined(__alpha__) || defined(__x86_64__) || defined(__ia64__)
#define HAVE_SYS_IO
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic warning "-Wcpp"
#warning sys.io is not available on this architecture and some functionality will be disabled
#pragma GCC diagnostic pop
#endif

/*!
 * \brief Defines for interacting with ALSA controls.
 */
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

/*!
 * \brief CMxxx USB device identifiers.
 */
#define C108_VENDOR_ID		0x0d8c
#define C108_PRODUCT_ID  	0x000c
#define C108B_PRODUCT_ID  	0x0012
#define C108AH_PRODUCT_ID  	0x013c
#define N1KDO_PRODUCT_ID  	0x6a00
#define C119_PRODUCT_ID  	0x0008
#define C119A_PRODUCT_ID  	0x013a
#define C119B_PRODUCT_ID    0x0013
#define C108_HID_INTERFACE	3

/*!
 * \brief CMxxx USB HID device access values.
 */
#define HID_REPORT_GET		0x01
#define HID_REPORT_SET		0x09

#define HID_RT_INPUT		0x01
#define HID_RT_OUTPUT		0x02

/*!
 * \brief CM-119B audio adjustment factor
 *	At the time of this documentation, DMK Engineering
 *	produces a sound card device that uses the CM-119B chip.
 *	They produced a couple of variations of the URIxB device.
 *	Although the CM-119B was supposed to be the same as the
 *	CM-119A, it did not function the same. As a result the 
 *	early production models required a different adjustment
 *	factor than the current production models.
 *
 *	Users with the early production units may need an
 *	adjustment factor of 750 or 870.
 *
 *	This adjustment factor is used for both microphone and
 *	speaker calcuations.
 */
#define C119B_ADJUSTMENT	1000

/*!
 * \brief EEPROM memory layout
 *	The AT93C46 eeprom has 64 addresses that contain 2 bytes (one word).
 *	The CMxxx sound card device will use this eeprom to read manuafacturer
 *	specific configuration data.
 *
 *	The CM108 and CM119 reserves memory addresses 0 to 6.
 *	The CM119A reserves memory addresses 0 to 44.
 *	The CM119B reserves memory addresses 0 to 50.
 *
 *	The usb channel drivers store user configuration information
 *	in addresses 51 to 63.
 *
 *	The user data is zero indexed to the EEPROM_START_ADDR.
 *
 *	chan_simpleusb radio does not populate all of the available fields.
 *
 * \note Some USB devices are not manufacturered with an eeprom.
 *	Never overwrite the manufacture stored information.
 */
#define	EEPROM_START_ADDR		51	/* Start after the manufacturer info */
#define	EEPROM_USER_LEN			13
#define	EEPROM_MAGIC			34329
#define	EEPROM_USER_MAGIC_ADDR	 0
#define	EEPROM_USER_RXMIXERSET	 1
#define	EEPROM_USER_TXMIXASET	 2
#define	EEPROM_USER_TXMIXBSET	 3
#define	EEPROM_USER_RXVOICEADJ	 4	/* Requires 2 memory slots, stored as a float */
#define	EEPROM_USER_RXCTCSSADJ	 6	/* Requires 2 memory slots, stored as a float */
#define	EEPROM_USER_TXCTCSSADJ	 8
#define	EEPROM_USER_RXSQUELCHADJ 9
#define EEPROM_USER_TXDSPLVL	10
#define EEPROM_USER_SPARE		11	/* Reserved for future use */
#define	EEPROM_USER_CS_ADDR		12

/*	Previous versions of this driver assumed 32 gpio pins
 *	the current and prior cm-xxx devices support a maximum of 8 gpio lines.
 *	In some hardware implementations, not all 8 gpio lines are available 
 *	to the user.
 */
#define GPIO_PINCOUNT 8


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

struct usbecho {
	struct qelem *q_forw;
	struct qelem *q_prev;
	short data[FRAME_SIZE];
};

long ast_radio_lround(double x);

int ast_radio_make_spkr_playback_value(int spkrmax, int request_value, int devtype);


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

struct usb_device *ast_radio_hid_device_init(const char *desired_device);

/*! \brief Get internal formatted string from external one */
int ast_radio_usb_get_usbdev(const char *devstr);

int ast_radio_load_parallel_port(int *haspp, int *ppfd, int *pbase, const char *pport, int reload);

unsigned char ast_radio_ppread(int haspp, unsigned int ppfd, unsigned int pbase, const char *pport);

void ast_radio_ppwrite(int haspp, unsigned int ppfd, unsigned int pbase, const char *pport, unsigned char c);
