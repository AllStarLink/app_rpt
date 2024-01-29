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

#define	O_CLOSE	0x444			/* special 'close' mode for device */
/* Which sound device to use */
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
/*
 * Message definition used in usb channel drivers.
 */
#define USB_UNASSIGNED_FMT	"Device %s is selected, the associated USB device string %s was not found\n"

/*! \brief Round double number to a long
 *
 * \note lround for uClibc - wrapper for lround(x) 
 *
 * \param x			Double number to round.
 *
 * \retval 			Rounded number as a long.
*/
long ast_radio_lround(double x);

/*!
 * \brief Calculate the speaker playback volume value.
 * 	Calculates the speaker playback volume.
 *
 *	The calling routine passes the maximum setting for
 *	for the speaker output.  This routine scales the
 *	requested value against the maximum.
 *
 *	Some devices may require a different scaling divisor.
 *	This routine can be customized for the requirements
 *	for new devices.
 *
 *	In some implementations, the scaling factor has been
 *	determined by spkrmax - 20 * log(ratio) or spkrmax - 10 * log(ratio).
 *	Discussions with radio engineers indicate that we should
 *	be using a linear scale.  FM deviation is linear.
 *
 * \param spkrmax		Speaker maximum value.
 * \param request_value	Requested volume value.
 * \param devtype		USB device type.
 *
 * \retval 				The calculated volume value.
 */
int ast_radio_make_spkr_playback_value(int spkrmax, int request_value, int devtype);

// Note: must add -lasound to end of linkage

/*!
 * \brief Get mixer max value
 * 	Gets the mixer max value from ALSA for the specified device and control.
 *
 * \param devnum		The ALSA major device number to update.
 * \param param			Pointer to the string mixer device name (control) to retrieve.
 * 
 * \retval 				The maximum value.
 */
int ast_radio_amixer_max(int devnum, char *param);

/*!
 * \brief Set mixer
 * 	Sets the mixer values for the specified device and control.
 *
 * \param devnum		The ALSA major device number to update.
 * \param param			Pointer to the string mixer device name (control) to update.
 * \param v1			Value 1 to set.  Values: 0-99 (percent) or 0-1 for baboon.
 * \param v2			Value 2 to set or zero if only one value.
 */
int ast_radio_setamixer(int devnum, char *param, int v1, int v2);

/*!
 * \brief Set USB HID outputs
 * 	This routine, depending on the outputs passed can set the GPIO states 
 *	and/or setup the chip to read/write the eeprom.
 *
 *	The passed outputs should be 4 bytes.
 *
 * \param handle		Pointer to usb_dev_handle associated with the HID.
 * \param outputs		Pointer to buffer that contains the data to send to the HID.
 */
void ast_radio_hid_set_outputs(struct usb_dev_handle *handle, unsigned char *outputs);

/*!
 * \brief Get USB HID inputs
 * 	This routine will retrieve the GPIO states or data the eeprom.
 *
 *	The passed inputs should be 4 bytes.
 *
 * \param handle		Pointer to usb_dev_handle associated with the HID.
 * \param inputs		Pointer to buffer that will contain the data received from the HID.
 */
void ast_radio_hid_get_inputs(struct usb_dev_handle *handle, unsigned char *inputs);

/*!
 * \brief Read user memory segment from the CM-XXX EEPROM.
 * 	Reads the memory range associated with user data from the EEPROM.
 *
 *	The user memory segment is from address position 51 to 63.
 *	Memory positions 0 to 50 are reserved for manufacturer's data.
 *
 * \param handle		Pointer to usb_dev_handle associated with the HID.
 * \param buf			Pointer to buffer to receive the EEPROM data.  The buffer
 *						must be an array of 13 unsigned shorts.
 *
 * \retval				Checksum of the received data.  If the check sum is correct,
 *						the calculated checksum will be zero.  This indicates valid data..
 *						Any	other value indicates bad EEPROM data.
 */
unsigned short ast_radio_get_eeprom(struct usb_dev_handle *handle, unsigned short *buf);

/*!
 * \brief Write user memory segment to the CM-XXX EEPROM.
 * 	Writes the memory range associated with user data to the EEPROM.
 *
 *	The user memory segment is from address position 51 to 63.
 *	
 *  \note Memory positions 0 to 50 are reserved for manufacturer's data.  Do not
 *	write into this segment!
 *
 * \param handle		Pointer to usb_dev_handle associated with the HID.
 * \param buf			Pointer to buffer that contains the the EEPROM data.  
 *						The buffer must be an array of 13 unsigned shorts.
 */
void ast_radio_put_eeprom(struct usb_dev_handle *handle, unsigned short *buf);

/*!
 * \brief Make a list of HID devices.
 * Populates usb_device_list with a list of devices that we
 * know that are compatible.
 * \retval 0	List was created.
 * \retval -1	List was not created.
 */
int ast_radio_hid_device_mklist(void);

/*!
 * \brief Initialize a USB device.
 * 	Searches for a USB device that matches the passed device string.
 *
 * \note It will only evaluate USB devices known to work with this application.
 *
 * \param desired_device	Pointer to a string that contains the device string to find.
 * \retval 					Returns a usb_device structure with the found device.
 *							If the device was not found, it returns null.
 */
struct usb_device *ast_radio_hid_device_init(const char *desired_device);

/*!
 * \brief Get USB device number from device string
 * 	Checks the symbolic links to see if the device string exists.
 *
 * \param devstr		Pointer to a string that contains the device string to find.
 * \retval 				Returns an index for the found device number.
 * \retval -1			If the device was not found.
 */
int ast_radio_usb_get_usbdev(const char *devstr);

/*!
 * \brief See if the internal usb_device_list contains the
 * specified device string.
 * \param devstr	Device string to check.
 * \retval 0		Device string was not found.
 * \retval 1		Device string was found.
 */
int ast_radio_usb_list_check(char *devstr);

/*!
 * \brief Open the specified parallel port
 * 	Opens the parallel port if is exists.
 *
 * \note The parallel port subsystem may not be available on all systems.
 *
 * \param haspp		Pointer to an integer that indicates the type of parallel port.
 *					0 = no parallel port, 1 = use open, 2 = use ioctl.
 * \param ppfd		Pointer to opened parallel port file descriptor.
 * \param pbase		Pointer to parallel port base address.
 * \param pport		Pointer to parallel port port number.
 * \param reload	Integer flag to indicate if the port should be closed and reopened.
 * \retval 	0		Always returns zero.
 */
int ast_radio_load_parallel_port(int *haspp, int *ppfd, int *pbase, const char *pport, int reload);

/*!
 * \brief Read a character from the specified parallel port
 * 	Reads a character from the parallel port
 *
 * \note The parallel port subsystem may not be available on all systems.
 *
 * \param haspp		Pointer to an integer that indicates the type of parallel port.
 *					0 = no parallel port, 1 = use open, 2 = use ioctl.
 * \param ppfd		Parallel port file descriptor.
 * \param pbase		Parallel port base address.
 * \param pport		Pointer to parallel port port number.
 * \retval 			Character that was read.
 */
unsigned char ast_radio_ppread(int haspp, unsigned int ppfd, unsigned int pbase, const char *pport);

/*!
 * \brief Write a character to the specified parallel port
 * 	Writes a character to the parallel port
 *
 * \note The parallel port subsystem may not be available on all systems.
 *
 * \param haspp		Pointer to an integer that indicates the type of parallel port.
 *					0 = no parallel port, 1 = use open, 2 = use ioctl.
 * \param ppfd		Parallel port file descriptor.
 * \param pbase		Parallel port base address.
 * \param pport		Pointer to parallel port port number.
 * \param c			Character to write.
 */
void ast_radio_ppwrite(int haspp, unsigned int ppfd, unsigned int pbase, const char *pport, unsigned char c);

/*!
 * \brief Poll the specified fd for input for the specified milliseconds.
 * \param fd			File descriptor.
 * \param ms			Milliseconds to wait.
 * \return Result from the select.
 */
int ast_radio_poll_input(int fd, int ms);

/*!
 * \brief Wait a fixed amount or on the specified fd for the specified milliseconds.
 * \param fd			File descriptor.
 * \param ms			Milliseconds to wait.
 * \param flag			0=use usleep, !0=use select/poll on the fd.
 * \retval 0			Timer expired.
 * \retval 1			Activity occurred on the fd.
 */
int ast_radio_wait_or_poll(int fd, int ms, int flag);