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

/*!
 * \file
 * \brief USB sound card resources.
 */

/*! \note <sys/io.h> is not portable to all architectures; guard parallel-port helpers with HAVE_SYS_IO */
#if __has_include(<sys/io.h>)
#define HAVE_SYS_IO
#endif

#include <libusb-1.0/libusb.h>
#include <portaudio.h>
#include <signal.h>

/*!
 * \brief CMxxx USB device identifiers.
 */
#define C108_VENDOR_ID 0x0d8c
#define C108_PRODUCT_ID 0x000c
#define C108B_PRODUCT_ID 0x0012
#define C108AH_PRODUCT_ID 0x013c
#define N1KDO_PRODUCT_ID 0x6a00
#define C119_PRODUCT_ID 0x0008
#define C119A_PRODUCT_ID 0x013a
#define C119B_PRODUCT_ID 0x0013
#define C108_HID_INTERFACE 3

/*!
 * \brief CMxxx USB HID device access values.
 */
#define HID_REPORT_GET 0x01
#define HID_REPORT_SET 0x09

#define HID_RT_INPUT 0x01
#define HID_RT_OUTPUT 0x02

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
 *	speaker calculations.
 */
#define AUDIO_ADJUSTMENT 1000

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
#define EEPROM_START_ADDR 51 /* Start after the manufacturer info */
#define EEPROM_USER_LEN 13
#define EEPROM_MAGIC 34329
#define EEPROM_USER_MAGIC_ADDR 0
#define EEPROM_USER_RXMIXERSET 1
#define EEPROM_USER_TXMIXASET 2
#define EEPROM_USER_TXMIXBSET 3
#define EEPROM_USER_RXVOICEADJ 4 /* Requires 2 memory slots, stored as a float */
#define EEPROM_USER_RXCTCSSADJ 6 /* Requires 2 memory slots, stored as a float */
#define EEPROM_USER_TXCTCSSADJ 8
#define EEPROM_USER_RXSQUELCHADJ 9
#define EEPROM_USER_TXDSPLVL 10
#define EEPROM_USER_SPARE 11 /* Reserved for future use */
#define EEPROM_USER_CS_ADDR 12

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
 */

#define FRAME_SIZE 160 /* Samples per Asterisk Frame */

/*
 * XXX text message sizes are probably 256 chars, but i am
 * not sure if there is a suitable definition anywhere.
 */
#define TEXT_SIZE 256

#define O_CLOSE 0x444 /* special 'close' mode for device */

/*!
 * \brief Optional hardware capabilities required by a channel driver
 *
 * USB capture and playback are always required and are therefore implicit.
 * Multiple flags use AND semantics: every requested capability must be
 * available for a device to match.
 */
enum ast_radio_device_capability {
	AST_RADIO_CAP_NONE = 0,
	AST_RADIO_CAP_CM108_HID = (1U << 0),
	AST_RADIO_CAP_PARALLEL = (1U << 1),
};

/*!
 * \brief Criteria supplied when requesting a USB radio device
 *
 * Selection precedence is serial, then devstr, then automatic assignment.
 * A configured selector that has no current match does not fall back to a
 * lower-priority selector.
 */
struct ast_radio_device_request {
	const char *devstr;					  /*!< Optional sysfs topology or ALSA device selector */
	const char *serial;					  /*!< Optional USB serial number; takes precedence over devstr */
	const char *owner;					  /*!< Required channel instance name used for diagnostics */
	unsigned int required_caps;			  /*!< ORed ast_radio_device_capability values; all are required */
	unsigned int minimum_input_channels;  /*!< Minimum PortAudio input channels required */
	unsigned int minimum_output_channels; /*!< Minimum PortAudio output channels required */
};

/*!
 * \brief Result of a USB radio device acquisition attempt
 */
enum ast_radio_device_result {
	AST_RADIO_DEVICE_READY,			 /*!< A viable device was exclusively leased and returned */
	AST_RADIO_DEVICE_WAIT,			 /*!< No viable, available device currently satisfies the request */
	AST_RADIO_DEVICE_CONFLICT,		 /*!< The explicitly selected device is leased to another owner */
	AST_RADIO_DEVICE_INVALID_CONFIG, /*!< The request contains malformed or unsupported selector syntax */
	AST_RADIO_DEVICE_ERROR,			 /*!< Discovery failed because of an internal or system error */
};

/*! \brief Return a printable description of a device acquisition result */
const char *ast_radio_device_result_str(enum ast_radio_device_result result);

enum ast_radio_mixer_capability {
	AST_RADIO_MIXER_CAPTURE_VOLUME = (1U << 0),
	AST_RADIO_MIXER_CAPTURE_SWITCH = (1U << 1),
	AST_RADIO_MIXER_PLAYBACK_VOLUME = (1U << 2),
	AST_RADIO_MIXER_PLAYBACK_SWITCH = (1U << 3),
};

struct ast_radio_mixer_element {
	const char *name;		   /*!< ALSA simple mixer element name */
	unsigned int index;		   /*!< ALSA simple mixer element index */
	unsigned int capabilities; /*!< ORed ast_radio_mixer_capability values */
	long capture_min;		   /*!< Minimum capture-volume value */
	long capture_max;		   /*!< Maximum capture-volume value */
	long playback_min;		   /*!< Minimum playback-volume value */
	long playback_max;		   /*!< Maximum playback-volume value */
};

struct ast_radio_mixer_path {
	unsigned int element; /*!< Index into ast_radio_device.mixer_elements */
	int channel;		  /*!< ALSA simple mixer channel identifier */
};

/*!
 * \brief Resolved USB radio device information and exclusive lease
 *
 * The identity and endpoint fields are populated by
 * ast_radio_device_acquire(). The caller may use them to open the HID and
 * audio subsystems. The caller must stop using any derived subsystem handles
 * before passing this object to ast_radio_device_release().
 *
 * String fields are owned by res_usbradio, remain valid for the lifetime of
 * the lease, and must not be modified or freed by callers.
 *
 * Fields documented as internal are owned by res_usbradio and must not be
 * inspected or modified by callers.
 */
struct ast_radio_device {
	/* Device identity and capabilities */
	const char *devstr;		   /*!< Canonical sysfs topology identity */
	const char *serial;		   /*!< USB serial number, or NULL when unavailable */
	unsigned short vendor_id;  /*!< USB vendor identifier */
	unsigned short product_id; /*!< USB product identifier */
	unsigned int capabilities; /*!< Available ast_radio_device_capability values */

	/* ALSA PCM endpoints */
	int alsa_card;					/*!< Resolved ALSA card number */
	int alsa_capture_device;		/*!< Resolved ALSA capture PCM device number */
	int alsa_playback_device;		/*!< Resolved ALSA playback PCM device number */
	const char *alsa_capture_name;	/*!< ALSA capture endpoint name */
	const char *alsa_playback_name; /*!< ALSA playback endpoint name */

	/* ALSA mixer elements and logical paths */
	struct ast_radio_mixer_element *mixer_elements;	   /*!< Unique ALSA mixer elements */
	size_t mixer_element_count;						   /*!< Number of mixer elements */
	struct ast_radio_mixer_path *mixer_rx_paths;	   /*!< Capture input paths (Mic Capture on CM108) */
	size_t mixer_rx_path_count;						   /*!< Number of RX paths */
	struct ast_radio_mixer_path *mixer_tx_paths;	   /*!< Playback output paths (Speaker/Headphone on CM108) */
	size_t mixer_tx_path_count;						   /*!< Number of TX paths */
	struct ast_radio_mixer_path *mixer_sidetone_paths; /*!< Optional capture-to-playback paths (Mic Playback on CM108) */
	size_t mixer_sidetone_path_count;				   /*!< Number of sidetone paths */
	struct ast_radio_mixer_path *mixer_rx_boost_paths; /*!< Optional input gain/AGC switch paths */
	size_t mixer_rx_boost_path_count;				   /*!< Number of receive-boost paths */

	/* libusb device reference and current transport address */
	struct libusb_device *usb_device; /*!< Referenced libusb device for the lease lifetime */
	unsigned int usb_bus;			  /*!< Current libusb bus number */
	unsigned int usb_address;		  /*!< Current, potentially ephemeral USB address */

	/* PortAudio endpoints; two valid indices imply one library reference held by the lease */
	PaDeviceIndex pa_input_device;	 /*!< PortAudio input index, or paNoDevice */
	PaDeviceIndex pa_output_device;	 /*!< PortAudio output index, or paNoDevice */
	unsigned int pa_input_channels;	 /*!< Maximum PortAudio input channels */
	unsigned int pa_output_channels; /*!< Maximum PortAudio output channels */

	/* Internal lease bookkeeping */
	void *private_data; /*!< Internal res_usbradio lease bookkeeping */
};

/*!
 * \brief Determine whether an acquired device is usable through PortAudio
 */
static inline int ast_radio_device_pa_ready(const struct ast_radio_device *device)
{
	return device && device->pa_input_device != paNoDevice && device->pa_output_device != paNoDevice;
}

/*!
 * \brief Acquire an exclusive lease on a USB radio device
 *
 * \param request Device selection criteria and required capabilities
 * \param[out] device Acquired device on AST_RADIO_DEVICE_READY; NULL for every other result
 *
 * \note The caller must release a returned device with ast_radio_device_release()
 *
 * \return An ast_radio_device_result describing the acquisition attempt
 */
enum ast_radio_device_result ast_radio_device_acquire(const struct ast_radio_device_request *request, struct ast_radio_device **device);

/*! \brief Return the number of active leases created by automatic assignment */
unsigned int ast_radio_device_automatic_count(void);

/*!
 * \brief Atomically exchange two active USB radio device leases
 *
 * Callers must stop using all subsystem handles derived from both devices
 * before exchanging the leases.
 *
 * \param first First device lease pointer to exchange
 * \param second Second device lease pointer to exchange
 *
 * \retval 0 on success
 * \retval -1 when either pointer does not identify an active lease
 */
int ast_radio_device_swap(struct ast_radio_device **first, struct ast_radio_device **second);

/*!
 * \brief Release a USB radio device lease
 *
 * The caller must first stop using all subsystem handles derived from the
 * device. Passing NULL is permitted and has no effect.
 *
 * \param device Device lease returned by ast_radio_device_acquire()
 */
void ast_radio_device_release(struct ast_radio_device *device);

/*! \brief Return the mixer element referenced by a device path */
const struct ast_radio_mixer_element *ast_radio_device_mixer_element(const struct ast_radio_device *device,
	const struct ast_radio_mixer_path *path);

/*! \brief Return the maximum value for one mixer path capability */
long ast_radio_device_mixer_max(const struct ast_radio_device *device, const struct ast_radio_mixer_path *path, unsigned int capability);

/*! \brief Scale an AUDIO_ADJUSTMENT setting into one mixer path range */
long ast_radio_device_mixer_scale(const struct ast_radio_device *device, const struct ast_radio_mixer_path *path,
	unsigned int capability, int setting);

/*!
 * \brief Set one control on a discovered mixer path
 *
 * \param device Device lease returned by ast_radio_device_acquire()
 * \param path Mixer path belonging to the acquired device
 * \param capability One ast_radio_mixer_capability value selecting the control
 * \param value ALSA volume value or zero/nonzero switch state
 *
 * \retval 0 on success
 * \retval -1 for an invalid path or when the mixer control cannot be updated
 */
int ast_radio_device_set_mixer(const struct ast_radio_device *device, const struct ast_radio_mixer_path *path,
	unsigned int capability, long value);

/*!
 * \brief Set one control across a collection of discovered mixer paths
 *
 * \retval 0 when every path was updated
 * \retval -1 when any path could not be updated
 */
int ast_radio_device_set_mixer_paths(const struct ast_radio_device *device, const struct ast_radio_mixer_path *paths,
	size_t path_count, unsigned int capability, long value);

struct usbecho {
	struct qelem *q_forw;
	struct qelem *q_prev;
	short data[FRAME_SIZE];
};

/* Audio statistics variables. tune-menu "R" and "X" commands display
 * stats data (peak, average, min, max levels and clipped sample count).
 */
#define AUDIO_STATS_LEN 50 /* number of 20mS frames. 50 => 1 second buf len */
struct audiostatistics {
	unsigned short maxbuf[AUDIO_STATS_LEN];	 /* peak sample value per frame */
	unsigned short clipbuf[AUDIO_STATS_LEN]; /* number of clipped samples per frame */
	unsigned int pwrbuf[AUDIO_STATS_LEN];	 /* total RMS power per frame */
	short index;							 /* Index within buffers, updated as frames received */
};

/*
 * Message definition used in usb channel drivers.
 */
#define USB_UNASSIGNED_FMT "Device %s is selected, the associated USB device string %s was not found\n"

/*!
 * \brief Round double number to a long
 *
 * \note lround for uClibc - wrapper for lround(x)
 *
 * \param x			Double number to round.
 *
 * \retval 			Rounded number as a long.
 */
long ast_radio_lround(double x);

/*!
 * \brief Set USB HID outputs
 * 	This routine, depending on the outputs passed can set the GPIO states
 *	and/or setup the chip to read/write the eeprom.
 *
 *	The passed outputs should be 4 bytes.
 *
 * \param handle		Pointer to usb_dev_handle associated with the HID.
 * \param outputs		Pointer to buffer that contains the data to send to the HID.
 * \retval bytes >= 0 Success, LIBUSB_ERROR* if error
 */
int ast_radio_hid_set_outputs(struct libusb_device_handle *handle, unsigned char *outputs);

/*!
 * \brief Get USB HID inputs
 * 	This routine will retrieve the GPIO states or data the eeprom.
 *
 *	The passed inputs should be 4 bytes.
 *
 * \param handle		Pointer to usb_dev_handle associated with the HID.
 * \param inputs		Pointer to buffer that will contain the data received from the HID.
 * \retval bytes >= 0 Success, LIBUSB_ERROR* if error
 */
int ast_radio_hid_get_inputs(struct libusb_device_handle *handle, unsigned char *inputs);

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
unsigned short ast_radio_get_eeprom(struct libusb_device_handle *handle, unsigned short *buf);

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
void ast_radio_put_eeprom(struct libusb_device_handle *handle, unsigned short *buf);

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

/*!
 * \brief Get system monotonic
 * This returns the CLOCK_MONOTONIC time
 * \param second	Pointer to time_t to receive the time.
 */
void ast_radio_time(time_t *second);

/*!
 * \brief Get system monotonic timeval
 * This returns the CLOCK_MONOTONIC time as a timeval
 * \retval 	timval structure with the current monotonic time.
 */
struct timeval ast_radio_tvnow(void);

/*!
 * \brief Detect ADC clipping, collect Rx audio statistics.
 *
 * If enabled by conf settings will set a GPIO high for 500mS when clipping is
 * detected. Nodes/URIs/audio interfaces can then light a Clip LED to alert users
 * of excessive audio input levels. Because CM1xxx USB audio interface ICs have an
 * internal mixer ahead of the ADC it is not possible within the interface board
 * analog circuitry to detect clipping at the ADC input point, thus this function
 * enables the raw ADC data to be checked. Clipping is detected by looking for
 * large amplitude square waves (min. 3 samples in a row > 99% FS).
 *
 * Data collected can be displayed from the tune-menu 'R' option or AMI
 * "[susb/radio] tune menu-support y" function. This also shows average power levels
 * which can be useful for optimizing audio levels and compression/limiting.
 * In general, peak levels should be within 3-10dB of full-scale (0dBFS) and
 * average signal power levels should be 10-20dB below full-scale.
 *
 * Should be passed the raw 48Ksps stereo USB frame read buffer before any filtering
 * or downsampling has been done. Extracts the 48K mono channel and downsamples to
 * 8Ksps (as is done in [simpleusb/usbradio]_read() but without filtering).
 * Signal power calculation takes the square of each sample to measure RMS power.
 * For CPU efficiency no scaling is done here. (When stats data is printed the
 * values are scaled to dBFS.)
 *
 * Audio parameters of interest include:
 * - Peak signal level over a longer time period eg. 1+ seconds (dBFS)
 *   This defines headroom (dB) and potential for clipping
 * - Min and max signal power levels averaged within each USB frame (dBFS)
 *   These define average dynamic range (dB)
 * - Min and max signal power averaged over a longer time period (dBFS)
 *   These define total signal power and peak-to-average power ratio
 *
 * \author      	NR9V
 * \param sbuf  	Rx audio sample buffer in 48k stereo or mono
 * \param o	    	Rx Audio Stats data structure
 * \param len   	Length of data in sbuf
 * \param mono  	True if sbuf is mono, False if sbuf is stereo
 * \return 	    	1 if clipping detected, 0 otherwise
 */
#define CLIP_LED_HOLD_TIME_MS 500
int ast_radio_check_audio(short *sbuf, struct audiostatistics *o, short len, short mono);

/*!
 * \brief Display receive audio statistics.
 *
 * Display the audio stats buffer data in normalized units. Peak value is the largest
 * sample value seen in the past AUDIO_STATS_LEN audio frames (1 second default).
 * Average, min, and max signal power levels are calculated from the total signal
 * power buffer which contains total RMS power per 20mS frame. Avg Pwr is the average
 * of the power values in the buffer, min and max are the lowest and highest average
 * power levels within the buffer. ClipCnt is the count of audio clipping events
 * detected.
 *
 * Example output message:
 *   RxAudioStats: Pk -2.1  Avg Pwr -32  Min -60  Max -12  dBFS  ClipCnt 0
 *
 * Results are scaled to double precision 0.0-1.0 and converted to log (dB)
 * ie. 10*log10(scaledVal) for power levels.
 *
 * \author  		NR9V
 * \param fd		File descriptor to print to, or if < 0 print using ast_verbose()
 * \param o 		Channel data structure
 * \return  		None
 */
void ast_radio_print_audio_stats(int fd, struct audiostatistics *o, const char *prefix_text);

#define AST_RADIO_PA_SAMPLE_RATE 48000
#define AST_RADIO_PA_FRAMES_PER_BUFFER (FRAME_SIZE * 6)
#define AST_RADIO_PA_OUTPUT_CHANNELS 2
#define AST_RADIO_PA_48K_MONO_SAMPLES (6 * FRAME_SIZE)
#define AST_RADIO_PA_48K_STEREO_SAMPLES (12 * FRAME_SIZE)

/*!
 * \brief PortAudio stream state shared by chan_simpleusb and chan_usbradio.
 *
 * Set ps->input_channels to the required RX channel count before calling
 * ast_radio_pa_open_device(). After opening, use ps->input_channels for RX
 * buffer layout and ps->output_channels for TX. Callers still pass stereo
 * interleaved TX to ast_radio_pa_write(); mono hardware is downmixed there.
 * ast_radio_pa_read() and ast_radio_pa_write() take frames per channel
 * (typically AST_RADIO_PA_FRAMES_PER_BUFFER).
 */
struct ast_radio_pa_stream {
	PaStream *stream;
	int active;
	unsigned int input_channels;  /*!< Required and actual RX channel count for ast_radio_pa_open_device() */
	unsigned int output_channels; /*!< Actual TX channel count after ast_radio_pa_open_device() */
};

/* Open a stream using the exact PortAudio endpoints held by a device lease */
PaError ast_radio_pa_open_device(struct ast_radio_pa_stream *ps, const struct ast_radio_device *device);
PaError ast_radio_pa_start(struct ast_radio_pa_stream *ps);
void ast_radio_pa_stop(struct ast_radio_pa_stream *ps);

PaError ast_radio_pa_read(struct ast_radio_pa_stream *ps, short *buf, unsigned long frames, int timeout_ms, volatile sig_atomic_t *stop);
PaError ast_radio_pa_write(struct ast_radio_pa_stream *ps, const short *data, unsigned long frames);
long ast_radio_pa_write_available(struct ast_radio_pa_stream *ps);
