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
 *
 * \brief Resource module for chan_usbradio and chan_simpleusb
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 */

/*** MODULEINFO
	<depend>alsa</depend>
	<depend>portaudio</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <stdio.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdlib.h>
#include <errno.h>
#include <libusb-1.0/libusb.h>
#include <linux/ppdev.h>
#include <linux/parport.h>
#include <linux/version.h>
#include <alsa/asoundlib.h>
#include <portaudio.h>

#include "asterisk/res_usbradio.h"

#define MIXER_RX_BOOST_ELEMENT "Auto Gain Control"

#ifdef HAVE_SYS_IO
#include <sys/io.h>
#endif

#ifdef __linux
#include <linux/soundcard.h>
#elif defined(__FreeBSD__)
#include <sys/soundcard.h>
#else
#include <soundcard.h>
#endif

#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/poll-compat.h" /* Used for polling */
#include "asterisk/config.h"

#define CONFIG_FILE "res_usbradio.conf"

/* Share a recently completed inventory between asynchronous channel threads */
#define DEVICE_INVENTORY_CACHE_MS 1000
#define DEVICE_REJECTION_LOG_SLOTS 16
#define FNV1A_64_OFFSET_BASIS 14695981039346656037ULL
#define FNV1A_64_PRIME 1099511628211ULL

/*
 * Add one value to an internal FNV-1a-style change signature.  The multiply
 * mixes each XORed value into the hash; see
 * https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
 */
static inline void signature_init(uint64_t *signature)
{
	*signature = FNV1A_64_OFFSET_BASIS;
}

static inline void signature_add(uint64_t *signature, uint64_t value)
{
	*signature = (*signature ^ value) * FNV1A_64_PRIME;
}

AST_MUTEX_DEFINE_STATIC(radio_device_inventory_lock);
AST_MUTEX_DEFINE_STATIC(radio_device_inventory_refresh_lock);

static struct libusb_context *usb_ctx = NULL;

static int pa_lib_acquire(void);
static void pa_lib_release(void);
static PaDeviceIndex pa_find_device(const char *hw_device, int want_input, int want_output);
static int radio_parse_alsa_hw_device(const char *s, int *card, int *dev);

/*!
 * \brief Correlated information about one discovered USB radio device
 *
 * Inventory entries own all string fields and one libusb device reference.
 * Entries do not represent an exclusive channel-driver lease.
 */
struct radio_device_inventory_entry {
	struct ast_radio_device device;
	char *owner;

	/* Mixer handle retained while the device is leased */
	snd_mixer_t *mixer;
	ast_mutex_t mixer_lock;

	unsigned int automatic:1;
	unsigned int pa_reference:1;
	AST_LIST_ENTRY(radio_device_inventory_entry) entry;
};

AST_LIST_HEAD_NOLOCK(radio_device_inventory_entries, radio_device_inventory_entry);

static void radio_device_pa_release(struct radio_device_inventory_entry *inventory_entry)
{
	if (!inventory_entry->pa_reference) {
		return;
	}
	inventory_entry->pa_reference = 0;
	pa_lib_release();
}

static void radio_device_mixer_release(struct radio_device_inventory_entry *inventory_entry)
{
	if (!inventory_entry->mixer) {
		return;
	}
	snd_mixer_close(inventory_entry->mixer);
	inventory_entry->mixer = NULL;
}

static int radio_device_mixer_acquire(struct radio_device_inventory_entry *inventory_entry)
{
	char mixer_name[32];
	snd_mixer_t *mixer;
	int result;

	snprintf(mixer_name, sizeof(mixer_name), "hw:%d", inventory_entry->device.alsa_card);
	result = snd_mixer_open(&mixer, 0);
	if (result < 0) {
		return -1;
	}
	result = snd_mixer_attach(mixer, mixer_name);
	if (result >= 0) {
		result = snd_mixer_selem_register(mixer, NULL, NULL);
	}
	if (result >= 0) {
		result = snd_mixer_load(mixer);
	}
	if (result < 0) {
		snd_mixer_close(mixer);
		return -1;
	}

	inventory_entry->mixer = mixer;
	return 0;
}

/*!
 * \brief Most recently completed device inventory snapshot
 *
 * A refresh is built separately and published under radio_device_inventory_lock so
 * callers never observe a partially populated inventory.
 */
static struct {
	struct radio_device_inventory_entries available;
	struct radio_device_inventory_entries leased;
	struct timeval refreshed_at;
	unsigned int generation;
	unsigned int logged_candidate_count;
	unsigned int logged_inventory_ready_count;
	unsigned int logged_pa_ready_count;
	uint64_t logged_signature;
	uint64_t logged_rejections[DEVICE_REJECTION_LOG_SLOTS];
	size_t logged_rejection_count;
	unsigned int log_initialized:1;
	unsigned int valid:1;
} radio_device_inventory;

/* radio_device_inventory_lock must be held by the caller. */
static int radio_device_rejection_should_log(const struct ast_radio_device *device,
	const struct ast_radio_device_request *request, unsigned int reason)
{
	const unsigned char *text;
	uint64_t signature;
	size_t index;

	signature_init(&signature);
	for (text = (const unsigned char *) device->devstr; *text; text++) {
		signature_add(&signature, *text);
	}
	for (text = (const unsigned char *) request->owner; *text; text++) {
		signature_add(&signature, *text);
	}
	signature_add(&signature, reason);
	signature_add(&signature, device->capabilities);
	signature_add(&signature, device->pa_input_channels);
	signature_add(&signature, device->pa_output_channels);
	signature_add(&signature, request->required_caps);
	signature_add(&signature, request->minimum_input_channels);
	signature_add(&signature, request->minimum_output_channels);

	for (index = 0; index < radio_device_inventory.logged_rejection_count; index++) {
		if (radio_device_inventory.logged_rejections[index] == signature) {
			return 0;
		}
	}
	if (radio_device_inventory.logged_rejection_count >= DEVICE_REJECTION_LOG_SLOTS) {
		return 0;
	}
	radio_device_inventory.logged_rejections[radio_device_inventory.logged_rejection_count++] = signature;
	return 1;
}

/*!
 * \brief Structure for defined usb devices.
 */
struct usb_device_entry {
	unsigned short idVendor;
	unsigned short idProduct;
	unsigned short idMask;
	AST_LIST_ENTRY(usb_device_entry) entry;
};

/*!
 * \brief Array of known compatible usb devices.
 */
const struct usb_device_entry known_devices[] = {
	{ C108_VENDOR_ID, C108_PRODUCT_ID, 0xfffc, { NULL } },
	{ C108_VENDOR_ID, C108B_PRODUCT_ID, 0xffff, { NULL } },
	{ C108_VENDOR_ID, C108AH_PRODUCT_ID, 0xffff, { NULL } },
	{ C108_VENDOR_ID, C119A_PRODUCT_ID, 0xffff, { NULL } },
	{ C108_VENDOR_ID, C119B_PRODUCT_ID, 0xffff, { NULL } },
	{ C108_VENDOR_ID, N1KDO_PRODUCT_ID, 0xff00, { NULL } },
	{ C108_VENDOR_ID, C119_PRODUCT_ID, 0xffff, { NULL } },
};

/*!
 * \brief Linked list of user defined usb devices.
 */
static AST_RWLIST_HEAD_STATIC(user_devices, usb_device_entry);

/*! \brief Release the resources owned by an inventory or leased device */
static void radio_device_contents_cleanup(struct ast_radio_device *device)
{
	size_t mixer_index;

	if (device->usb_device) {
		libusb_unref_device(device->usb_device);
	}

	ast_free((char *) device->devstr);
	ast_free((char *) device->serial);
	ast_free((char *) device->alsa_capture_name);
	ast_free((char *) device->alsa_playback_name);
	for (mixer_index = 0; mixer_index < device->mixer_element_count; mixer_index++) {
		ast_free((char *) device->mixer_elements[mixer_index].name);
	}
	ast_free(device->mixer_elements);
	ast_free(device->mixer_rx_paths);
	ast_free(device->mixer_tx_paths);
	ast_free(device->mixer_sidetone_paths);
	ast_free(device->mixer_rx_boost_paths);
}

/*! \brief Free one device inventory entry and its owned resources */
static void device_inventory_entry_free(struct radio_device_inventory_entry *inventory_entry)
{
	if (!inventory_entry) {
		return;
	}

	radio_device_pa_release(inventory_entry);
	radio_device_mixer_release(inventory_entry);
	radio_device_contents_cleanup(&inventory_entry->device);
	ast_free(inventory_entry->owner);
	ast_mutex_destroy(&inventory_entry->mixer_lock);
	ast_free(inventory_entry);
}

/*!
 * \brief Remove and free every entry in the published device inventory
 */
static void device_inventory_cleanup(void)
{
	struct radio_device_inventory_entry *device;

	ast_mutex_lock(&radio_device_inventory_lock);
	while ((device = AST_LIST_REMOVE_HEAD(&radio_device_inventory.available, entry))) {
		device_inventory_entry_free(device);
	}
	memset(&radio_device_inventory.refreshed_at, 0, sizeof(radio_device_inventory.refreshed_at));
	radio_device_inventory.log_initialized = 0;
	radio_device_inventory.valid = 0;
	ast_mutex_unlock(&radio_device_inventory_lock);
}

long ast_radio_lround(double x)
{
	return (long) ((x - ((long) x) >= 0.5f) ? (((long) x) + 1) : ((long) x));
}

int ast_radio_hid_set_outputs(struct libusb_device_handle *handle, unsigned char *outputs)
{
	/* This appears to prevent issues with the CM-109 chipset when switching modes too fast
	 * Originally 1500, Issues with Uno-Q and Pi5? Adjusted to 3000
	 */
	usleep(3000);
	return libusb_control_transfer(handle, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
		HID_REPORT_SET, 0 + (HID_RT_OUTPUT << 8), C108_HID_INTERFACE, outputs, 4, 20);
}

int ast_radio_hid_get_inputs(struct libusb_device_handle *handle, unsigned char *inputs)
{
	/* This appears to prevent issues with the CM-109 chipset when switching modes too fast
	 * Originally 1500, Issues with Uno-Q and Pi5? Adjusted to 3000
	 */
	usleep(3000);
	return libusb_control_transfer(handle, LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
		HID_REPORT_GET, 0 + (HID_RT_INPUT << 8), C108_HID_INTERFACE, inputs, 4, 20);
}

/*!
 * \brief Read CM-xxx EEPROM
 * 	Read a memory position from the EEPROM attached to the CM-XXX device.
 *	One memory position is two bytes.
 *
 *	Four bytes are passed to the device to configure it for an EEPROM read.
 *	The first byte should be 0x80, the fourth byte should be 0x80 or'd with
 *	the address to read.
 *
 *	After the address has been set, a get input is done to read the returned
 *	bytes.
 *
 * \param handle		Pointer to usb_dev_handle associated with the HID.
 * \param addr			Integer address to read from the EEPROM.  The valid
 *						range is 0 to 63.
 */
static unsigned short read_eeprom(struct libusb_device_handle *handle, int addr)
{
	unsigned char buf[4];

	buf[0] = 0x80;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = 0x80 | (addr & 0x3f);

	usleep(500);
	ast_radio_hid_set_outputs(handle, buf);

	memset(buf, 0, sizeof(buf));
	usleep(500);
	ast_radio_hid_get_inputs(handle, buf);
	return (buf[1] + (buf[2] << 8));
}

/*!
 * \brief Write CM-xxx EEPROM
 * 	Write a memory position in the EEPROM attached to the CM-XXX device.
 *	One memory position is two bytes.
 *
 *	Four bytes are passed to the device to write the value.  The first byte
 *	should be 0x80, the second byte should be the lsb of the data, the third
 *	byte is the msb of the data, the fourth byte should be 0xC0 or'd with
 *	the address to write.
 *
 * \note This routine will write to any valid memory address.  Never write
 *	to address 0 to 50.  These are reserved for manufacturer data.
 *
 * \param handle		Pointer to usb_dev_handle associated with the HID.
 * \param addr			Integer address to read from the EEPROM.  The valid
 *						range is 0 to 63.
 * \param data			Unsigned short data to store.
 */
static void write_eeprom(struct libusb_device_handle *handle, int addr, unsigned short data)
{
	unsigned char buf[4];

	buf[0] = 0x80;
	buf[1] = data & 0xff;
	buf[2] = data >> 8;
	buf[3] = 0xc0 | (addr & 0x3f);

	usleep(2000);
	ast_radio_hid_set_outputs(handle, buf);
}

unsigned short ast_radio_get_eeprom(struct libusb_device_handle *handle, unsigned short *buf)
{
	int i;
	unsigned short cs;

	cs = 0xffff;

	for (i = EEPROM_START_ADDR; i <= EEPROM_START_ADDR + EEPROM_USER_CS_ADDR; i++) {
		cs += buf[i - EEPROM_START_ADDR] = read_eeprom(handle, i);
	}

	return cs;
}

void ast_radio_put_eeprom(struct libusb_device_handle *handle, unsigned short *buf)
{
	int i;
	unsigned short cs;

	cs = 0xffff;
	buf[EEPROM_USER_MAGIC_ADDR] = EEPROM_MAGIC;

	for (i = EEPROM_START_ADDR; i < EEPROM_START_ADDR + EEPROM_USER_CS_ADDR; i++) {
		write_eeprom(handle, i, buf[i - EEPROM_START_ADDR]);
		cs += buf[i - EEPROM_START_ADDR];
	}

	buf[EEPROM_USER_CS_ADDR] = (65535 - cs) + 1;
	write_eeprom(handle, i, buf[EEPROM_USER_CS_ADDR]);
}

/*!
 * \brief See if the passed device matches one of our known devices.
 *
 * \param dev	usb device
 * \return 0	does not matches
 * \return 1	matches
 */
static int is_known_device(struct libusb_device *dev)
{
	int index;
	int matched_entry = 0;
	struct libusb_device_descriptor desc;

	if (libusb_get_device_descriptor(dev, &desc) < 0) {
		return 0;
	}

	for (index = 0; index < ARRAY_LEN(known_devices); index++) {
		if (known_devices[index].idVendor == desc.idVendor &&
			known_devices[index].idProduct == (desc.idProduct & known_devices[index].idMask)) {
			matched_entry = 1;
			break;
		};
	}

	return matched_entry;
}

/*!
 * \brief See if the passed device matches one of our user defined devices.
 *
 * \param dev	usb device
 * \return 0	does not matches
 * \return 1	matches
 */
static int is_user_device(struct libusb_device *dev)
{
	struct usb_device_entry *device;
	struct libusb_device_descriptor desc;

	if (libusb_get_device_descriptor(dev, &desc) < 0) {
		return 0;
	}

	AST_RWLIST_RDLOCK(&user_devices);
	AST_LIST_TRAVERSE(&user_devices, device, entry) {
		if (desc.idVendor == device->idVendor && desc.idProduct == device->idProduct) {
			break;
		};
	}
	AST_RWLIST_UNLOCK(&user_devices);

	return device ? 1 : 0;
}

static int read_card_usbbus(int cardno, char *out, int outsz)
{
	char path[128];
	FILE *fp;
	size_t n;

	if (outsz < 2) {
		return -1;
	}

	snprintf(path, sizeof(path), "/proc/asound/card%d/usbbus", cardno);

	fp = fopen(path, "r");
	if (!fp) {
		return -1;
	}

	if (!fgets(out, outsz, fp) || !out[0]) {
		fclose(fp);
		return -1;
	}

	fclose(fp);

	/* trim trailing whitespace/newlines */
	n = strlen(out);

	while (n > 0 && isspace((unsigned char) out[n - 1])) {
		out[--n] = '\0';
	}

	return (n > 0) ? 0 : -1;
}

/*
 * Correlate a libusb device with the ALSA card and sysfs topology already
 * used by the channel drivers.  A device is not viable until all three views
 * agree that it is present.
 */
static int radio_device_find_alsa_card(struct libusb_device *usb_device, int *card, char **sysfs_device)
{
	char usb_bus[16];
	char alsa_bus[64];
	char sysfs_path[128];
	char link_target[PATH_MAX];
	char *topology;
	ssize_t link_length;
	int card_number;

	snprintf(usb_bus, sizeof(usb_bus), "%03u/%03u", libusb_get_bus_number(usb_device), libusb_get_device_address(usb_device));

	for (card_number = 0; card_number < 32; card_number++) {
		if (read_card_usbbus(card_number, alsa_bus, sizeof(alsa_bus)) || strcasecmp(alsa_bus, usb_bus)) {
			continue;
		}

		snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/sound/card%d/device", card_number);
		link_length = readlink(sysfs_path, link_target, sizeof(link_target) - 1);
		if (link_length < 0) {
			continue;
		}
		link_target[link_length] = '\0';
		topology = strrchr(link_target, '/');
		if (!topology || ast_strlen_zero(++topology)) {
			continue;
		}

		*sysfs_device = ast_strdup(topology);
		if (!*sysfs_device) {
			return -1;
		}
		*card = card_number;
		return 0;
	}

	return -1;
}

/* Check whether an ALSA PCM device supports the requested stream direction */
static int radio_device_alsa_pcm_available(snd_ctl_t *control, snd_pcm_info_t *pcm_info, int pcm_device, snd_pcm_stream_t stream)
{
	snd_pcm_info_set_device(pcm_info, pcm_device);
	snd_pcm_info_set_subdevice(pcm_info, 0);
	snd_pcm_info_set_stream(pcm_info, stream);
	if (snd_ctl_pcm_info(control, pcm_info) < 0) {
		return 0;
	}
	return 1;
}

/* Find capture and playback PCM devices through ALSA without opening either stream */
static int radio_device_find_alsa_endpoints(int card, int *capture_device, int *playback_device)
{
	char control_name[32];
	snd_ctl_t *control;
	snd_pcm_info_t *pcm_info;
	int pcm_device = -1;
	int result;

	*capture_device = -1;
	*playback_device = -1;
	snprintf(control_name, sizeof(control_name), "hw:%d", card);
	result = snd_ctl_open(&control, control_name, 0);
	if (result < 0) {
		ast_debug(5, "ALSA control interface for card %d is not yet available: %s\n", card, snd_strerror(result));
		return -1;
	}

	snd_pcm_info_alloca(&pcm_info);
	for (;;) {
		result = snd_ctl_pcm_next_device(control, &pcm_device);
		if (result < 0) {
			ast_debug(5, "Unable to enumerate PCM devices for ALSA card %d: %s\n", card, snd_strerror(result));
			break;
		}
		if (pcm_device < 0) {
			break;
		}
		if (*capture_device < 0) {
			if (radio_device_alsa_pcm_available(control, pcm_info, pcm_device, SND_PCM_STREAM_CAPTURE)) {
				*capture_device = pcm_device;
			}
		}
		if (*playback_device < 0) {
			if (radio_device_alsa_pcm_available(control, pcm_info, pcm_device, SND_PCM_STREAM_PLAYBACK)) {
				*playback_device = pcm_device;
			}
		}
		if (*capture_device >= 0) {
			if (*playback_device >= 0) {
				break;
			}
		}
	}
	snd_ctl_close(control);

	result = 0;
	if (*capture_device < 0) {
		ast_debug(5, "ALSA card %d does not have an available capture PCM device\n", card);
		result = -1;
	}
	if (*playback_device < 0) {
		ast_debug(5, "ALSA card %d does not have an available playback PCM device\n", card);
		result = -1;
	}
	return result;
}

static int radio_device_mixer_path_append(struct ast_radio_mixer_path **paths, size_t *path_count, unsigned int element, int channel)
{
	struct ast_radio_mixer_path *new_paths;

	/* Grow the path array and append the discovered element channel. */
	new_paths = ast_realloc(*paths, (*path_count + 1) * sizeof(*new_paths));
	if (!new_paths) {
		return -1;
	}

	*paths = new_paths;
	new_paths[*path_count].element = element;
	new_paths[*path_count].channel = channel;
	(*path_count)++;
	return 0;
}

static int radio_device_mixer_element_append(struct ast_radio_device *device, snd_mixer_elem_t *mixer_element, unsigned int *element_index)
{
	struct ast_radio_mixer_element *element;
	struct ast_radio_mixer_element *new_elements;

	/* Add storage for the discovered simple mixer element. */
	new_elements = ast_realloc(device->mixer_elements, (device->mixer_element_count + 1) * sizeof(*new_elements));
	if (!new_elements) {
		return -1;
	}

	/* Retain the element identity needed for later mixer operations. */
	device->mixer_elements = new_elements;
	element = &new_elements[device->mixer_element_count];
	memset(element, 0, sizeof(*element));
	element->name = ast_strdup(snd_mixer_selem_get_name(mixer_element));
	if (!element->name) {
		return -1;
	}
	element->index = snd_mixer_selem_get_index(mixer_element);

	/* Record capture capabilities and volume range. */
	if (snd_mixer_selem_has_capture_volume(mixer_element)) {
		element->capabilities |= AST_RADIO_MIXER_CAPTURE_VOLUME;
		snd_mixer_selem_get_capture_volume_range(mixer_element, &element->capture_min, &element->capture_max);
	}
	if (snd_mixer_selem_has_capture_switch(mixer_element)) {
		element->capabilities |= AST_RADIO_MIXER_CAPTURE_SWITCH;
	}

	/* Record playback capabilities and volume range. */
	if (snd_mixer_selem_has_playback_volume(mixer_element)) {
		element->capabilities |= AST_RADIO_MIXER_PLAYBACK_VOLUME;
		snd_mixer_selem_get_playback_volume_range(mixer_element, &element->playback_min, &element->playback_max);
	}
	if (snd_mixer_selem_has_playback_switch(mixer_element)) {
		element->capabilities |= AST_RADIO_MIXER_PLAYBACK_SWITCH;
	}

	*element_index = device->mixer_element_count;
	device->mixer_element_count++;
	return 0;
}

static int radio_device_mixer_paths_add(struct ast_radio_device *device, snd_mixer_elem_t *mixer_element, unsigned int element_index)
{
	int channel;
	int has_capture;
	int has_playback;

	/* The receive boost is a named semantic control with no volume capability. */
	if (!strcasecmp(snd_mixer_selem_get_name(mixer_element), MIXER_RX_BOOST_ELEMENT) && snd_mixer_selem_has_playback_switch(mixer_element)) {
		for (channel = 0; channel <= SND_MIXER_SCHN_LAST; channel++) {
			if (!snd_mixer_selem_has_playback_channel(mixer_element, channel)) {
				continue;
			}
			if (radio_device_mixer_path_append(&device->mixer_rx_boost_paths, &device->mixer_rx_boost_path_count, element_index, channel)) {
				return -1;
			}
		}
		return 0;
	}

	/* Classify capture-volume channels as receive paths */
	has_capture = snd_mixer_selem_has_capture_volume(mixer_element);
	if (has_capture) {
		for (channel = 0; channel <= SND_MIXER_SCHN_LAST; channel++) {
			if (!snd_mixer_selem_has_capture_channel(mixer_element, channel)) {
				continue;
			}
			if (radio_device_mixer_path_append(&device->mixer_rx_paths, &device->mixer_rx_path_count, element_index, channel)) {
				return -1;
			}
		}
	}

	/* Classify playback channels as sidetone or transmit paths */
	has_playback = snd_mixer_selem_has_playback_volume(mixer_element);
	if (!has_playback) {
		return 0;
	}

	for (channel = 0; channel <= SND_MIXER_SCHN_LAST; channel++) {
		if (!snd_mixer_selem_has_playback_channel(mixer_element, channel)) {
			continue;
		}
		if (has_capture) {
			if (radio_device_mixer_path_append(&device->mixer_sidetone_paths, &device->mixer_sidetone_path_count, element_index, channel)) {
				return -1;
			}
			continue;
		}
		if (radio_device_mixer_path_append(&device->mixer_tx_paths, &device->mixer_tx_path_count, element_index, channel)) {
			return -1;
		}
	}
	return 0;
}

/* Discover mixer elements by capability rather than device-specific names */
static int radio_device_find_mixer_paths(struct ast_radio_device *device)
{
	char mixer_name[32];
	snd_mixer_t *mixer;
	snd_mixer_elem_t *mixer_element;
	unsigned int element_index;
	int result;

	/* Open and load the card's ALSA simple mixer elements. */
	snprintf(mixer_name, sizeof(mixer_name), "hw:%d", device->alsa_card);
	result = snd_mixer_open(&mixer, 0);
	if (result < 0) {
		return -1;
	}
	result = snd_mixer_attach(mixer, mixer_name);
	if (result < 0) {
		snd_mixer_close(mixer);
		return -1;
	}
	result = snd_mixer_selem_register(mixer, NULL, NULL);
	if (result < 0) {
		snd_mixer_close(mixer);
		return -1;
	}
	result = snd_mixer_load(mixer);
	if (result < 0) {
		snd_mixer_close(mixer);
		return -1;
	}

	/* Retain active volume elements and the optional named receive-boost control. */
	for (mixer_element = snd_mixer_first_elem(mixer); mixer_element; mixer_element = snd_mixer_elem_next(mixer_element)) {
		if (!snd_mixer_selem_is_active(mixer_element)) {
			continue;
		}
		if (!snd_mixer_selem_has_capture_volume(mixer_element) && !snd_mixer_selem_has_playback_volume(mixer_element) &&
			(strcasecmp(snd_mixer_selem_get_name(mixer_element), MIXER_RX_BOOST_ELEMENT) || !snd_mixer_selem_has_playback_switch(mixer_element))) {
			continue;
		}
		if (radio_device_mixer_element_append(device, mixer_element, &element_index)) {
			result = -1;
			break;
		}
		if (radio_device_mixer_paths_add(device, mixer_element, element_index)) {
			result = -1;
			break;
		}
	}
	snd_mixer_close(mixer);

	if (result < 0) {
		return -1;
	}

	/* Receive and transmit paths are required; sidetone is optional. */
	if (!device->mixer_rx_path_count) {
		ast_debug(5, "USB radio device %s (ALSA card %d) has no capture-volume mixer path\n", device->devstr, device->alsa_card);
		return -1;
	}
	if (!device->mixer_tx_path_count) {
		ast_debug(5, "USB radio device %s (ALSA card %d) has no playback-volume mixer path\n", device->devstr, device->alsa_card);
		return -1;
	}
	ast_debug(7, "USB radio device %s mixers: RX:%zu TX:%zu sidetone:%zu boost:%zu (%zu elements)\n", device->devstr,
		device->mixer_rx_path_count, device->mixer_tx_path_count, device->mixer_sidetone_path_count,
		device->mixer_rx_boost_path_count, device->mixer_element_count);
	return 0;
}

/* Read the serial from the parent USB device without opening the hardware */
static char *radio_device_read_serial(const char *sysfs_device)
{
	char *usb_device;
	char *interface_separator;
	char path[PATH_MAX];
	char *serial = NULL;
	char *owned_serial;
	size_t serial_size = 0;
	ssize_t serial_length;
	FILE *serial_file;

	usb_device = ast_strdup(sysfs_device);
	if (!usb_device) {
		return NULL;
	}
	interface_separator = strchr(usb_device, ':');
	if (interface_separator) {
		*interface_separator = '\0';
	}

	snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/serial", usb_device);
	ast_free(usb_device);

	serial_file = fopen(path, "r");
	if (!serial_file) {
		return NULL;
	}

	serial_length = getline(&serial, &serial_size, serial_file);
	fclose(serial_file);
	if (serial_length < 0) {
		ast_std_free(serial);
		return NULL;
	}

	while (serial_length > 0 && isspace((unsigned char) serial[serial_length - 1])) {
		serial[--serial_length] = '\0';
	}
	if (!serial_length) {
		ast_std_free(serial);
		return NULL;
	}

	/* getline() uses libc allocation; copy into Asterisk-owned inventory storage. */
	owned_serial = ast_strdup(serial);
	ast_std_free(serial);
	return owned_serial;
}

static struct radio_device_inventory_entry *radio_device_inventory_entry_create(struct libusb_device *usb_device)
{
	struct radio_device_inventory_entry *inventory_entry;
	struct libusb_device_descriptor descriptor;
	char capture_name[32];
	char playback_name[32];
	int alsa_card;
	int capture_device;
	int playback_device;
	char *sysfs_device;

	if (libusb_get_device_descriptor(usb_device, &descriptor) < 0) {
		ast_debug(5, "Unable to read the descriptor for libusb device at bus %u address %u\n", libusb_get_bus_number(usb_device),
			libusb_get_device_address(usb_device));
		return NULL;
	}
	if (radio_device_find_alsa_card(usb_device, &alsa_card, &sysfs_device)) {
		ast_debug(7, "USB bus %u address %u: ALSA/sysfs match pending\n", libusb_get_bus_number(usb_device),
			libusb_get_device_address(usb_device));
		return NULL;
	}
	if (radio_device_find_alsa_endpoints(alsa_card, &capture_device, &playback_device)) {
		ast_debug(5, "USB radio device %s (ALSA card %d) does not have usable capture and playback endpoints\n", sysfs_device, alsa_card);
		ast_free(sysfs_device);
		return NULL;
	}

	inventory_entry = ast_calloc(1, sizeof(*inventory_entry));
	if (!inventory_entry) {
		ast_free(sysfs_device);
		return NULL;
	}
	ast_mutex_init(&inventory_entry->mixer_lock);
	inventory_entry->device.devstr = sysfs_device;
	inventory_entry->device.alsa_card = alsa_card;
	if (radio_device_find_mixer_paths(&inventory_entry->device)) {
		goto allocation_failed;
	}

	snprintf(capture_name, sizeof(capture_name), "hw:%d,%d", alsa_card, capture_device);
	snprintf(playback_name, sizeof(playback_name), "hw:%d,%d", alsa_card, playback_device);
	inventory_entry->device.serial = radio_device_read_serial(sysfs_device);
	inventory_entry->device.alsa_capture_name = ast_strdup(capture_name);
	if (!inventory_entry->device.alsa_capture_name) {
		goto allocation_failed;
	}
	inventory_entry->device.alsa_playback_name = ast_strdup(playback_name);
	if (!inventory_entry->device.alsa_playback_name) {
		goto allocation_failed;
	}

	inventory_entry->device.vendor_id = descriptor.idVendor;
	inventory_entry->device.product_id = descriptor.idProduct;
	inventory_entry->device.capabilities = AST_RADIO_CAP_CM108_HID;
	inventory_entry->device.alsa_capture_device = capture_device;
	inventory_entry->device.alsa_playback_device = playback_device;
	inventory_entry->device.usb_device = libusb_ref_device(usb_device);
	inventory_entry->device.usb_bus = libusb_get_bus_number(usb_device);
	inventory_entry->device.usb_address = libusb_get_device_address(usb_device);
	inventory_entry->device.pa_input_device = paNoDevice;
	inventory_entry->device.pa_output_device = paNoDevice;

	return inventory_entry;

allocation_failed:
	device_inventory_entry_free(inventory_entry);
	return NULL;
}

static int radio_device_inventory_needs_update(void)
{
	long inventory_age;
	int needs_update;

	ast_mutex_lock(&radio_device_inventory_lock);
	inventory_age = ast_tvdiff_ms(ast_tvnow(), radio_device_inventory.refreshed_at);
	needs_update = 0;
	if (!radio_device_inventory.valid) {
		needs_update = 1;
	} else if (inventory_age >= DEVICE_INVENTORY_CACHE_MS) {
		needs_update = 1;
	}
	ast_mutex_unlock(&radio_device_inventory_lock);
	return needs_update;
}

static struct radio_device_inventory_entry *radio_device_find_lease(const struct ast_radio_device *device);

static void radio_device_pa_update_indices(struct ast_radio_device *device)
{
	const PaDeviceInfo *input_info;
	const PaDeviceInfo *output_info;

	device->pa_input_device = pa_find_device(device->alsa_capture_name, 1, 0);
	device->pa_output_device = pa_find_device(device->alsa_playback_name, 0, 1);
	device->pa_input_channels = 0;
	device->pa_output_channels = 0;

	/* Record the channel capacity of each resolved PortAudio endpoint */
	input_info = device->pa_input_device == paNoDevice ? NULL : Pa_GetDeviceInfo(device->pa_input_device);
	if (input_info && input_info->maxInputChannels > 0) {
		device->pa_input_channels = (unsigned int) input_info->maxInputChannels;
	}

	output_info = device->pa_output_device == paNoDevice ? NULL : Pa_GetDeviceInfo(device->pa_output_device);
	if (output_info && output_info->maxOutputChannels > 0) {
		device->pa_output_channels = (unsigned int) output_info->maxOutputChannels;
	}
}

static void radio_device_inventory_pa_update(struct radio_device_inventory_entries *entries)
{
	struct radio_device_inventory_entry *inventory_entry;

	if (pa_lib_acquire() < 0) {
		ast_debug(3, "PortAudio is unavailable while refreshing the USB radio device inventory\n");
		return;
	}

	AST_LIST_TRAVERSE(entries, inventory_entry, entry) {
		radio_device_pa_update_indices(&inventory_entry->device);
		if (!ast_radio_device_pa_ready(&inventory_entry->device)) {
			ast_debug(7, "USB radio device %s: PortAudio not ready\n", inventory_entry->device.devstr);
		} else {
			ast_debug(7, "USB radio device %s PortAudio: in %d/%u ch, out %d/%u ch\n", inventory_entry->device.devstr,
				inventory_entry->device.pa_input_device, inventory_entry->device.pa_input_channels,
				inventory_entry->device.pa_output_device, inventory_entry->device.pa_output_channels);
		}
	}

	pa_lib_release();
}

/* Build a complete snapshot before replacing the inventory visible to callers */
static int radio_device_inventory_refresh(int force)
{
	struct radio_device_inventory_entries refreshed_entries;
	struct radio_device_inventory_entry *inventory_entry;
	struct radio_device_inventory_entry *old_entry;
	struct libusb_device **usb_devices = NULL;
	ssize_t device_count;
	ssize_t device_index;
	unsigned int candidate_count = 0;
	unsigned int inventory_ready_count = 0;
	unsigned int pa_ready_count = 0;
	unsigned int generation;
	uint64_t signature;
	int log_inventory;

	signature_init(&signature);
	AST_LIST_HEAD_INIT_NOLOCK(&refreshed_entries);
	ast_mutex_lock(&radio_device_inventory_refresh_lock);
	if (!force) {
		if (!radio_device_inventory_needs_update()) {
			ast_mutex_unlock(&radio_device_inventory_refresh_lock);
			return 0;
		}
	}
	if (!usb_ctx) {
		ast_mutex_unlock(&radio_device_inventory_refresh_lock);
		return -1;
	}

	device_count = libusb_get_device_list(usb_ctx, &usb_devices);
	if (device_count < 0) {
		ast_mutex_unlock(&radio_device_inventory_refresh_lock);
		return -1;
	}

	for (device_index = 0; device_index < device_count; device_index++) {
		if (!is_known_device(usb_devices[device_index])) {
			if (!is_user_device(usb_devices[device_index])) {
				continue;
			}
		}
		candidate_count++;
		signature_add(&signature, libusb_get_bus_number(usb_devices[device_index]));
		signature_add(&signature, libusb_get_device_address(usb_devices[device_index]));

		inventory_entry = radio_device_inventory_entry_create(usb_devices[device_index]);
		if (!inventory_entry) {
			continue;
		}

		AST_LIST_INSERT_TAIL(&refreshed_entries, inventory_entry, entry);
		inventory_ready_count++;
		ast_debug(7, "USB radio device %s%s%s discovered: ALSA in %s, out %s (%04x:%04x)\n", inventory_entry->device.devstr,
			inventory_entry->device.serial ? " serial " : "", S_OR(inventory_entry->device.serial, ""), inventory_entry->device.alsa_capture_name,
			inventory_entry->device.alsa_playback_name, inventory_entry->device.vendor_id, inventory_entry->device.product_id);
	}
	libusb_free_device_list(usb_devices, 1);
	radio_device_inventory_pa_update(&refreshed_entries);
	AST_LIST_TRAVERSE(&refreshed_entries, inventory_entry, entry) {
		const unsigned char *identity;

		for (identity = (const unsigned char *) inventory_entry->device.devstr; *identity; identity++) {
			signature_add(&signature, *identity);
		}
		signature_add(&signature, inventory_entry->device.pa_input_channels);
		signature_add(&signature, inventory_entry->device.pa_output_channels);
		if (ast_radio_device_pa_ready(&inventory_entry->device)) {
			pa_ready_count++;
		}
	}

	ast_mutex_lock(&radio_device_inventory_lock);
	while ((old_entry = AST_LIST_REMOVE_HEAD(&radio_device_inventory.available, entry))) {
		device_inventory_entry_free(old_entry);
	}
	while ((inventory_entry = AST_LIST_REMOVE_HEAD(&refreshed_entries, entry))) {
		if (radio_device_find_lease(&inventory_entry->device)) {
			device_inventory_entry_free(inventory_entry);
		} else {
			AST_LIST_INSERT_TAIL(&radio_device_inventory.available, inventory_entry, entry);
		}
	}
	radio_device_inventory.refreshed_at = ast_tvnow();
	log_inventory = !radio_device_inventory.log_initialized || candidate_count != radio_device_inventory.logged_candidate_count ||
					inventory_ready_count != radio_device_inventory.logged_inventory_ready_count ||
					pa_ready_count != radio_device_inventory.logged_pa_ready_count || signature != radio_device_inventory.logged_signature;
	generation = radio_device_inventory.generation;
	if (log_inventory) {
		generation = ++radio_device_inventory.generation;
	}
	radio_device_inventory.logged_candidate_count = candidate_count;
	radio_device_inventory.logged_inventory_ready_count = inventory_ready_count;
	radio_device_inventory.logged_pa_ready_count = pa_ready_count;
	radio_device_inventory.logged_signature = signature;
	if (log_inventory) {
		radio_device_inventory.logged_rejection_count = 0;
	}
	radio_device_inventory.log_initialized = 1;
	radio_device_inventory.valid = 1;
	ast_mutex_unlock(&radio_device_inventory_lock);
	ast_mutex_unlock(&radio_device_inventory_refresh_lock);

	if (log_inventory) {
		ast_debug(3, "USB radio inventory generation %u: %u recognized, %u ALSA/mixer-ready, %u PortAudio-ready\n", generation,
			candidate_count, inventory_ready_count, pa_ready_count);
	}
	return 0;
}

/* radio_device_inventory_lock must be held by the caller. */
static struct radio_device_inventory_entry *radio_device_find_lease(const struct ast_radio_device *device)
{
	struct radio_device_inventory_entry *leased_entry;

	AST_LIST_TRAVERSE(&radio_device_inventory.leased, leased_entry, entry) {
		if (!strcmp(leased_entry->device.devstr, device->devstr)) {
			return leased_entry;
		}
	}
	return NULL;
}

static int radio_device_matches_devstr(const struct ast_radio_device *device, const char *devstr)
{
	int card;
	int pcm_device;

	if (!strcasecmp(device->devstr, devstr)) {
		return 1;
	}
	if (!strcasecmp(device->alsa_capture_name, devstr)) {
		return 1;
	}
	if (!strcasecmp(device->alsa_playback_name, devstr)) {
		return 1;
	}

	if (!radio_parse_alsa_hw_device(devstr, &card, &pcm_device)) {
		return 0;
	}
	if (card != device->alsa_card) {
		return 0;
	}
	if (pcm_device < 0) {
		return 1;
	}
	if (pcm_device == device->alsa_capture_device) {
		return 1;
	}
	if (pcm_device == device->alsa_playback_device) {
		return 1;
	}
	return 0;
}

static int radio_device_matches_request(const struct ast_radio_device *device, const struct ast_radio_device_request *request)
{
	if (!ast_strlen_zero(request->serial)) {
		if (!device->serial) {
			return 0;
		}
		if (strcmp(device->serial, request->serial)) {
			return 0;
		}
		return 1;
	}
	if (!ast_strlen_zero(request->devstr)) {
		return radio_device_matches_devstr(device, request->devstr);
	}
	return 1;
}

static int radio_device_supports_request(const struct ast_radio_device *device, const struct ast_radio_device_request *request)
{
	if ((device->capabilities & request->required_caps) != request->required_caps) {
		return 0;
	}
	if (device->pa_input_channels < request->minimum_input_channels) {
		return 0;
	}
	if (device->pa_output_channels < request->minimum_output_channels) {
		return 0;
	}
	return 1;
}

static void radio_device_pa_acquire(struct radio_device_inventory_entry *leased_entry)
{
	struct ast_radio_device *device = &leased_entry->device;

	device->pa_input_device = paNoDevice;
	device->pa_output_device = paNoDevice;
	if (pa_lib_acquire() < 0) {
		return;
	}

	radio_device_pa_update_indices(device);
	if (!ast_radio_device_pa_ready(device)) {
		pa_lib_release();
		return;
	}
	leased_entry->pa_reference = 1;
}

const char *ast_radio_device_result_str(enum ast_radio_device_result result)
{
	switch (result) {
	case AST_RADIO_DEVICE_READY:
		return "USB radio device is ready";
	case AST_RADIO_DEVICE_WAIT:
		return "No matching USB radio device is currently available";
	case AST_RADIO_DEVICE_CONFLICT:
		return "The selected USB radio device is already in use";
	case AST_RADIO_DEVICE_INVALID_CONFIG:
		return "The configured USB radio device selector is invalid";
	case AST_RADIO_DEVICE_ERROR:
		return "USB radio device discovery failed";
	default:
		return "Unknown USB radio device result";
	}
}

enum ast_radio_device_result ast_radio_device_acquire(const struct ast_radio_device_request *request, struct ast_radio_device **device)
{
	struct radio_device_inventory_entry *inventory_entry;
	struct radio_device_inventory_entry *candidate = NULL;
	struct radio_device_inventory_entry *active_lease;
	int automatic;
	int ignored_devstr = 0;
	int result;

	if (device) {
		*device = NULL;
	}
	if (!request) {
		return AST_RADIO_DEVICE_INVALID_CONFIG;
	}
	if (!device) {
		return AST_RADIO_DEVICE_INVALID_CONFIG;
	}
	if (ast_strlen_zero(request->owner)) {
		return AST_RADIO_DEVICE_INVALID_CONFIG;
	}
	if (request->required_caps & ~(AST_RADIO_CAP_CM108_HID | AST_RADIO_CAP_PARALLEL)) {
		return AST_RADIO_DEVICE_INVALID_CONFIG;
	}

	automatic = ast_strlen_zero(request->serial) && ast_strlen_zero(request->devstr);
	if (!automatic) {
		ast_mutex_lock(&radio_device_inventory_lock);
		AST_LIST_TRAVERSE(&radio_device_inventory.leased, active_lease, entry) {
			if (!radio_device_matches_request(&active_lease->device, request)) {
				continue;
			}
			if (!radio_device_supports_request(&active_lease->device, request)) {
				continue;
			}
			ast_mutex_unlock(&radio_device_inventory_lock);
			return AST_RADIO_DEVICE_CONFLICT;
		}
		ast_mutex_unlock(&radio_device_inventory_lock);
	}

	if (radio_device_inventory_refresh(0)) {
		return AST_RADIO_DEVICE_ERROR;
	}

	ast_mutex_lock(&radio_device_inventory_lock);
	AST_LIST_TRAVERSE(&radio_device_inventory.available, inventory_entry, entry) {
		if (!radio_device_matches_request(&inventory_entry->device, request)) {
			continue;
		}
		if (!ast_radio_device_pa_ready(&inventory_entry->device)) {
			if (!radio_device_rejection_should_log(&inventory_entry->device, request, 1)) {
				continue;
			}
			ast_debug(3, "USB radio device %s rejected for %s: PortAudio not ready\n", inventory_entry->device.devstr, request->owner);
			continue;
		}
		if ((inventory_entry->device.capabilities & request->required_caps) != request->required_caps) {
			if (!radio_device_rejection_should_log(&inventory_entry->device, request, 2)) {
				continue;
			}
			ast_debug(3, "USB radio device %s rejected for %s: Need capabilities 0x%x, have 0x%x\n",
				inventory_entry->device.devstr, request->owner, request->required_caps, inventory_entry->device.capabilities);
			continue;
		}
		if (inventory_entry->device.pa_input_channels < request->minimum_input_channels ||
			inventory_entry->device.pa_output_channels < request->minimum_output_channels) {
			if (!radio_device_rejection_should_log(&inventory_entry->device, request, 3)) {
				continue;
			}
			ast_debug(3, "USB radio device %s rejected for %s: Need in:%u/out:%u, have %u/%u\n", inventory_entry->device.devstr,
				request->owner, request->minimum_input_channels, request->minimum_output_channels,
				inventory_entry->device.pa_input_channels, inventory_entry->device.pa_output_channels);
			continue;
		}

		if (!automatic) {
			candidate = inventory_entry;
			break;
		}
		if (!candidate) {
			candidate = inventory_entry;
			continue;
		}
		if (inventory_entry->device.alsa_card < candidate->device.alsa_card) {
			candidate = inventory_entry;
		}
	}

	if (!candidate) {
		ast_mutex_unlock(&radio_device_inventory_lock);
		return AST_RADIO_DEVICE_WAIT;
	}
	if (!ast_strlen_zero(request->serial)) {
		if (!ast_strlen_zero(request->devstr)) {
			if (!radio_device_matches_devstr(&candidate->device, request->devstr)) {
				ignored_devstr = 1;
			}
		}
	}

	candidate->owner = ast_strdup(request->owner);
	if (!candidate->owner) {
		ast_mutex_unlock(&radio_device_inventory_lock);
		return AST_RADIO_DEVICE_ERROR;
	}
	AST_LIST_REMOVE(&radio_device_inventory.available, candidate, entry);
	candidate->automatic = automatic;
	candidate->device.private_data = candidate;
	AST_LIST_INSERT_TAIL(&radio_device_inventory.leased, candidate, entry);
	result = radio_device_mixer_acquire(candidate);
	if (!result) {
		radio_device_pa_acquire(candidate);
	}
	if (result || !ast_radio_device_pa_ready(&candidate->device) || !radio_device_supports_request(&candidate->device, request)) {
		radio_device_pa_release(candidate);
		radio_device_mixer_release(candidate);
		AST_LIST_REMOVE(&radio_device_inventory.leased, candidate, entry);
		candidate->device.private_data = NULL;
		candidate->automatic = 0;
		ast_free(candidate->owner);
		candidate->owner = NULL;
		AST_LIST_INSERT_TAIL(&radio_device_inventory.available, candidate, entry);
		ast_mutex_unlock(&radio_device_inventory_lock);
		return AST_RADIO_DEVICE_WAIT;
	}
	ast_mutex_unlock(&radio_device_inventory_lock);

	if (ignored_devstr) {
		ast_log(LOG_WARNING, "USB radio request from %s selected serial %s; ignoring conflicting devstr %s\n", request->owner,
			request->serial, request->devstr);
	}
	*device = &candidate->device;
	ast_debug(3, "USB radio device %s acquired by %s\n", candidate->device.devstr, candidate->owner);
	return AST_RADIO_DEVICE_READY;
}

unsigned int ast_radio_device_automatic_count(void)
{
	struct radio_device_inventory_entry *leased_entry;
	unsigned int count = 0;

	ast_mutex_lock(&radio_device_inventory_lock);
	AST_LIST_TRAVERSE(&radio_device_inventory.leased, leased_entry, entry) {
		if (leased_entry->automatic) {
			count++;
		}
	}
	ast_mutex_unlock(&radio_device_inventory_lock);
	return count;
}

int ast_radio_device_swap(struct ast_radio_device **first, struct ast_radio_device **second)
{
	struct radio_device_inventory_entry *first_entry;
	struct radio_device_inventory_entry *second_entry;
	struct ast_radio_device *device;
	char *owner;
	unsigned int automatic;

	if (!first || !*first || !second || !*second || *first == *second) {
		return -1;
	}

	/* Exchange lease ownership and caller pointers as one inventory operation */
	ast_mutex_lock(&radio_device_inventory_lock);
	first_entry = (*first)->private_data;
	second_entry = (*second)->private_data;
	if (!first_entry || !second_entry || &first_entry->device != *first || &second_entry->device != *second) {
		ast_mutex_unlock(&radio_device_inventory_lock);
		return -1;
	}
	owner = first_entry->owner;
	first_entry->owner = second_entry->owner;
	second_entry->owner = owner;
	automatic = first_entry->automatic;
	first_entry->automatic = second_entry->automatic;
	second_entry->automatic = automatic;
	device = *first;
	*first = *second;
	*second = device;
	ast_debug(3, "USB radio device leases for %s and %s were exchanged\n", first_entry->owner, second_entry->owner);
	ast_mutex_unlock(&radio_device_inventory_lock);
	return 0;
}

void ast_radio_device_release(struct ast_radio_device *device)
{
	struct radio_device_inventory_entry *leased_entry;

	if (!device) {
		return;
	}

	leased_entry = device->private_data;
	if (!leased_entry) {
		return;
	}

	ast_mutex_lock(&radio_device_inventory_lock);
	AST_LIST_REMOVE(&radio_device_inventory.leased, leased_entry, entry);
	/* Force the next acquisition to confirm that released hardware is present. */
	radio_device_inventory.valid = 0;
	ast_mutex_unlock(&radio_device_inventory_lock);

	ast_debug(3, "USB radio device %s released by %s\n", leased_entry->device.devstr, leased_entry->owner);
	device_inventory_entry_free(leased_entry);
}

const struct ast_radio_mixer_element *ast_radio_device_mixer_element(const struct ast_radio_device *device,
	const struct ast_radio_mixer_path *path)
{
	if (!device || !path || path->element >= device->mixer_element_count) {
		return NULL;
	}
	return &device->mixer_elements[path->element];
}

long ast_radio_device_mixer_max(const struct ast_radio_device *device, const struct ast_radio_mixer_path *path, unsigned int capability)
{
	const struct ast_radio_mixer_element *element;

	element = ast_radio_device_mixer_element(device, path);
	if (!element || (capability != AST_RADIO_MIXER_CAPTURE_VOLUME && capability != AST_RADIO_MIXER_PLAYBACK_VOLUME) ||
		!(element->capabilities & capability)) {
		return 0;
	}
	return capability == AST_RADIO_MIXER_CAPTURE_VOLUME ? element->capture_max : element->playback_max;
}

long ast_radio_device_mixer_scale(const struct ast_radio_device *device, const struct ast_radio_mixer_path *path,
	unsigned int capability, int setting)
{
	const struct ast_radio_mixer_element *element;
	long minimum;
	long maximum;

	element = ast_radio_device_mixer_element(device, path);
	if (!element || (capability != AST_RADIO_MIXER_CAPTURE_VOLUME && capability != AST_RADIO_MIXER_PLAYBACK_VOLUME) ||
		!(element->capabilities & capability)) {
		return 0;
	}
	minimum = capability == AST_RADIO_MIXER_CAPTURE_VOLUME ? element->capture_min : element->playback_min;
	maximum = capability == AST_RADIO_MIXER_CAPTURE_VOLUME ? element->capture_max : element->playback_max;
	return minimum + ((long) setting * (maximum - minimum)) / AUDIO_ADJUSTMENT;
}

static int radio_device_set_mixer_batch(const struct ast_radio_device *device, const struct ast_radio_mixer_path *paths,
	size_t path_count, unsigned int capability, long value)
{
	struct radio_device_inventory_entry *inventory_entry;
	const struct ast_radio_mixer_element *element;
	snd_mixer_elem_t *mixer_element;
	snd_mixer_selem_id_t *element_id;
	int result = 0;
	size_t path_index;

	if (!device || !device->private_data || (path_count && !paths)) {
		return -1;
	}
	if (capability != AST_RADIO_MIXER_CAPTURE_VOLUME && capability != AST_RADIO_MIXER_CAPTURE_SWITCH &&
		capability != AST_RADIO_MIXER_PLAYBACK_VOLUME && capability != AST_RADIO_MIXER_PLAYBACK_SWITCH) {
		return -1;
	}
	for (path_index = 0; path_index < path_count; path_index++) {
		if (paths[path_index].element >= device->mixer_element_count || paths[path_index].channel < 0 ||
			paths[path_index].channel > SND_MIXER_SCHN_LAST ||
			!(device->mixer_elements[paths[path_index].element].capabilities & capability)) {
			return -1;
		}
	}

	inventory_entry = device->private_data;
	ast_mutex_lock(&inventory_entry->mixer_lock);
	if (!inventory_entry->mixer) {
		ast_mutex_unlock(&inventory_entry->mixer_lock);
		return -1;
	}

	/* Apply every path through the mixer retained by the active lease */
	snd_mixer_selem_id_alloca(&element_id);
	for (path_index = 0; path_index < path_count; path_index++) {
		element = &device->mixer_elements[paths[path_index].element];
		snd_mixer_selem_id_set_name(element_id, element->name);
		snd_mixer_selem_id_set_index(element_id, element->index);
		mixer_element = snd_mixer_find_selem(inventory_entry->mixer, element_id);
		if (!mixer_element) {
			result = -1;
			continue;
		}

		switch (capability) {
		case AST_RADIO_MIXER_CAPTURE_VOLUME:
			if (snd_mixer_selem_set_capture_volume(mixer_element, paths[path_index].channel, value) < 0) {
				result = -1;
			}
			break;
		case AST_RADIO_MIXER_CAPTURE_SWITCH:
			if (snd_mixer_selem_set_capture_switch(mixer_element, paths[path_index].channel, value != 0) < 0) {
				result = -1;
			}
			break;
		case AST_RADIO_MIXER_PLAYBACK_VOLUME:
			if (snd_mixer_selem_set_playback_volume(mixer_element, paths[path_index].channel, value) < 0) {
				result = -1;
			}
			break;
		case AST_RADIO_MIXER_PLAYBACK_SWITCH:
			if (snd_mixer_selem_set_playback_switch(mixer_element, paths[path_index].channel, value != 0) < 0) {
				result = -1;
			}
			break;
		}
	}
	ast_mutex_unlock(&inventory_entry->mixer_lock);
	return result;
}

int ast_radio_device_set_mixer(const struct ast_radio_device *device, const struct ast_radio_mixer_path *path, unsigned int capability, long value)
{
	return radio_device_set_mixer_batch(device, path, 1, capability, value);
}

int ast_radio_device_set_mixer_paths(const struct ast_radio_device *device, const struct ast_radio_mixer_path *paths,
	size_t path_count, unsigned int capability, long value)
{
	return radio_device_set_mixer_batch(device, paths, path_count, capability, value);
}

int ast_radio_load_parallel_port(int *haspp, int *ppfd, int *pbase, const char *pport, int reload)
{
	if (*haspp) { /* if is to use parallel port */
		if (!ast_strlen_zero(pport)) {
			if (reload && *ppfd != -1) {
				close(*ppfd);
				*ppfd = -1;
			}
			*ppfd = open(pport, O_RDWR);
			if (*ppfd != -1) {
				if (ioctl(*ppfd, PPCLAIM)) {
					ast_log(LOG_ERROR, "Unable to claim printer port %s, disabling pp support\n", pport);
					close(*ppfd);
					*haspp = 0;
				}
			} else {
#ifdef HAVE_SYS_IO
				if (ioperm(*pbase, 2, 1) == -1) {
					ast_log(LOG_ERROR, "Can't get io permission on IO port %04x hex, disabling pp support\n", *pbase);
					*haspp = 0;
				} else {
					*haspp = 2;
					ast_verb(3, "Using direct IO port for pp support, since parport driver not available.\n");
				}
#else
				ast_log(LOG_ERROR, "Parallel port I/O is not supported on this architecture\n");
#endif
			}
		}
	}

	if (*haspp == 1) {
		ast_verb(3, "Parallel port is %s\n", pport);
	} else if (*haspp == 2) {
		ast_verb(3, "Parallel port is at %04x hex\n", *pbase);
	}
	return 0;
}

unsigned char ast_radio_ppread(int haspp, unsigned int ppfd, unsigned int pbase, const char *pport)
{
#ifdef HAVE_SYS_IO
	unsigned char c;

	c = 0;
	if (haspp == 1) { /* if its a pp dev */
		if (ioctl(ppfd, PPRSTATUS, &c) == -1) {
			ast_log(LOG_ERROR, "Unable to read pp dev %s\n", pport);
			c = 0;
		}
	}
	if (haspp == 2) { /* if its a direct I/O */
		c = inb(pbase + 1);
	}
	return c;
#else
	ast_log(LOG_ERROR, "Parallel port I/O is not supported on this architecture\n");
	return 0;
#endif
}

void ast_radio_ppwrite(int haspp, unsigned int ppfd, unsigned int pbase, const char *pport, unsigned char c)
{
#ifdef HAVE_SYS_IO
	if (haspp == 1) { /* if its a pp dev */
		if (ioctl(ppfd, PPWDATA, &c) == -1) {
			ast_log(LOG_ERROR, "Unable to write pp dev %s\n", pport);
		}
	}
	if (haspp == 2) { /* if its a direct I/O */
		outb(c, pbase);
	}
#else
	ast_log(LOG_ERROR, "Parallel port I/O is not supported on this architecture\n");
#endif
	return;
}

int ast_radio_poll_input(int fd, int ms)
{
	struct pollfd fds[1];

	memset(&fds, 0, sizeof(fds));
	fds[0].fd = fd;
	fds[0].events = POLLIN;

	return ast_poll(fds, 1, ms);
}

int ast_radio_wait_or_poll(int fd, int ms, int flag)
{
	int i;

	if (!flag) {
		usleep(ms * 1000);
		return 0;
	}
	i = 0;
	if (ms >= 100) {
		for (i = 0; i < ms; i += 100) {
			ast_cli(fd, "\r");
			if (ast_radio_poll_input(fd, 100)) {
				return 1;
			}
		}
	}
	if (ast_radio_poll_input(fd, ms - i)) {
		return 1;
	}
	ast_cli(fd, "\r");
	return 0;
}

void ast_radio_time(time_t *second)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	*second = ts.tv_sec;
}

struct timeval ast_radio_tvnow(void)
{
	struct timeval tv;
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	tv = ast_tv(ts.tv_sec, ts.tv_nsec / 1000);

	return tv;
}

#define CLIP_SAMP_THRESH 0x7eb0
#define CLIP_EVENT_MIN_SAMPLES 3
int ast_radio_check_audio(short *sbuf, struct audiostatistics *o, short len, short mono)
{
	unsigned short i, j, val, max = 0, seq_clips = 0;
	double pwr = 0.0;
	short buf[FRAME_SIZE], last_clip = -1;

	if (o->index >= AUDIO_STATS_LEN) {
		o->index = 0;
	}

	if (mono) {
		/* validate len and index for mono audio */
		if (len > 6 * FRAME_SIZE) {
			len = 6 * FRAME_SIZE;
		}

		/* Downsample from 48000 mono to 8000 mono */
		for (i = 5, j = 0; i < len; i += 6) {
			buf[j++] = sbuf[i];
		}
		len /= 6;
	} else {
		/* validate len and index for stereo audio */
		if (len > 12 * FRAME_SIZE) {
			len = 12 * FRAME_SIZE;
		}

		/* Downsample from 48000 stereo to 8000 mono */
		for (i = 10, j = 0; i < len; i += 12) {
			buf[j++] = sbuf[i];
		}
		len /= 12;
	}

	/* len should now be 160 */
	if (len == 0) {
		/* Something went wrong */
		if (++o->index >= AUDIO_STATS_LEN) {
			o->index = 0;
		}
		return 0;
	}

	for (i = 0; i < len; i++) {
		val = abs(buf[i]);
		if (val) {
			if (val > max) {
				max = val;
			}

			pwr += (double) (val * val);

			if (val > CLIP_SAMP_THRESH) {
				if (last_clip >= 0 && last_clip + 1 == i) {
					seq_clips++;
				}

				last_clip = i;
			}
		}
	}

	o->maxbuf[o->index] = max;
	o->pwrbuf[o->index] = (unsigned int) (pwr / (double) len);
	o->clipbuf[o->index] = seq_clips;

	if (++o->index >= AUDIO_STATS_LEN) {
		o->index = 0;
	}

	/* return 1 if clipping was detected */
	return (seq_clips >= CLIP_EVENT_MIN_SAMPLES);
}

void ast_radio_print_audio_stats(int fd, struct audiostatistics *o, const char *prefix_text)
{
	unsigned int i, pk = 0, pwr = 0, minpwr = 0x40000000, maxpwr = 0, clipcnt = 0;
	double dpk, dmin, dmax, scale, tpwr = 0.0;
	char s1[100];

	/* Peak    = max(maxbuf)^2
	 * Avg Pwr = avg(pwrbuf)
	 *     Min = min(pwrbuf)
	 *     Max = max(pwrbuf)
	 */
	for (i = 0; i < AUDIO_STATS_LEN; i++) {
		if (o->maxbuf[i] > pk) {
			pk = o->maxbuf[i];
		}
		pwr = o->pwrbuf[i];
		if (pwr < minpwr) {
			minpwr = pwr;
		}
		if (pwr > maxpwr) {
			maxpwr = pwr;
		}
		tpwr += pwr;
		clipcnt += o->clipbuf[i];
	}
	tpwr /= AUDIO_STATS_LEN;
	/* Convert to dBFS / dB */
	scale = 1.0 / (double) (1 << 30);
	dpk = (pk > 0.0) ? 10 * log10(pk * pk * scale) : -96.0;
	tpwr = (tpwr > 0.0) ? 10 * log10(tpwr * scale) : -96.0;
	dmin = minpwr ? 10 * log10(minpwr * scale) : -96.0;
	dmax = maxpwr ? 10 * log10(maxpwr * scale) : -96.0;
	/* Print stats */
	snprintf(s1, sizeof(s1), "%sAudioStats: Pk %5.1f  Avg Pwr %3.0f  Min %3.0f  Max %3.0f  dBFS  ClipCnt %u", prefix_text, dpk,
		tpwr, dmin, dmax, clipcnt);
	if (fd >= 0) {
		ast_cli(fd, "%s\n", s1);
	} else {
		ast_verbose("%s\n", s1);
	}
}

/*!
 * \brief Remove and free up all user devices.
 */
static void cleanup_user_devices(void)
{
	struct usb_device_entry *device;
	/* Remove all existing devices */
	AST_RWLIST_WRLOCK(&user_devices);
	while ((device = AST_LIST_REMOVE_HEAD(&user_devices, entry))) {
		ast_free(device);
	}
	AST_RWLIST_UNLOCK(&user_devices);
}

/*
 * PortAudio helpers shared by chan_simpleusb and chan_usbradio.
 */
AST_MUTEX_DEFINE_STATIC(pa_lock);
static int pa_refcount;

static int64_t pa_now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int pa_lib_acquire(void)
{
	PaError res;

	ast_mutex_lock(&pa_lock);
	if (pa_refcount == 0) {
		res = Pa_Initialize();
		if (res != paNoError) {
			ast_mutex_unlock(&pa_lock);
			ast_log(LOG_WARNING, "Failed to initialize PortAudio - (%d) %s\n", res, Pa_GetErrorText(res));
			return -1;
		}
	}
	pa_refcount++;
	ast_mutex_unlock(&pa_lock);
	return 0;
}

static void pa_lib_release(void)
{
	ast_mutex_lock(&pa_lock);
	if (pa_refcount > 0) {
		pa_refcount--;
		if (pa_refcount == 0) {
			Pa_Terminate();
		}
	}
	ast_mutex_unlock(&pa_lock);
}

#define AST_RADIO_HW_CARD_MAX 9999
#define AST_RADIO_HW_DEV_MAX 255

static int parse_hw_field(const char **q, int max_value, int *out)
{
	int value = 0;

	if (!q || !*q || !out || !isdigit((unsigned char) **q)) {
		return 0;
	}

	while (isdigit((unsigned char) **q)) {
		int digit = **q - '0';

		if (value > max_value / 10 || value * 10 + digit > max_value) {
			return 0;
		}
		value = value * 10 + digit;
		(*q)++;
	}

	*out = value;
	return 1;
}

static int hw_token_boundary_ok(char c)
{
	if (c == '\0') {
		return 1;
	}
	if (isspace((unsigned char) c)) {
		return 1;
	}

	/* Trailing punctuation in PA/ALSA device name strings, not part of hw:C,D. */
	return strchr("):],;.-", c) != NULL;
}

/*!
 * \brief Parse "hw:<card>" or "hw:<card>,<dev>" from anywhere in s.
 * \retval 1 if found; sets *card; sets *dev to parsed value or -1 if absent.
 */
static int radio_parse_alsa_hw_device(const char *s, int *card, int *dev)
{
	const char *p = s;

	if (!s || !card || !dev) {
		return 0;
	}

	while ((p = strstr(p, "hw:")) != NULL) {
		const char *q = p + 3;
		int c;
		int d = -1;

		if (!parse_hw_field(&q, AST_RADIO_HW_CARD_MAX, &c)) {
			p = q;
			continue;
		}

		if (*q == ',') {
			q++;
			if (!parse_hw_field(&q, AST_RADIO_HW_DEV_MAX, &d)) {
				p = q;
				continue;
			}
		}

		if (!hw_token_boundary_ok(*q)) {
			p = q;
			continue;
		}

		*card = c;
		*dev = d;
		return 1;
	}

	return 0;
}

static int pa_hw_match(const char *haystack, const char *needle)
{
	int haystack_c, haystack_d, needle_c, needle_d;
	const char *p = haystack ? haystack : "";

	if (!radio_parse_alsa_hw_device(needle, &needle_c, &needle_d)) {
		return 0;
	}

	while ((p = strstr(p, "hw:")) != NULL) {
		if (radio_parse_alsa_hw_device(p, &haystack_c, &haystack_d)) {
			if (haystack_c == needle_c) {
				if (needle_d < 0 || haystack_d == needle_d) {
					return 1;
				}
			}
		}
		p += 3;
	}

	return 0;
}

static int match_card_id(const char *devname, int card)
{
	char path[128], id[64];
	FILE *fp;
	size_t n;

	snprintf(path, sizeof(path), "/proc/asound/card%d/id", card);
	fp = fopen(path, "r");
	if (!fp) {
		return 0;
	}

	if (!fgets(id, sizeof(id), fp) || !id[0]) {
		fclose(fp);
		return 0;
	}
	fclose(fp);

	n = strlen(id);
	while (n > 0 && isspace((unsigned char) id[n - 1])) {
		id[--n] = '\0';
	}

	return n > 0 && strstr(devname, id) != NULL;
}

static int pa_device_matches(const PaDeviceInfo *dev, const char *hw_device, int want_input, int want_output)
{
	int card, devnum;
	int hay_card, hay_dev;

	if (!dev || !hw_device || !hw_device[0]) {
		return 0;
	}

	if (want_input && !dev->maxInputChannels) {
		return 0;
	}

	if (want_output && !dev->maxOutputChannels) {
		return 0;
	}

	if (pa_hw_match(dev->name, hw_device)) {
		return 1;
	}

	if (!radio_parse_alsa_hw_device(hw_device, &card, &devnum) || !match_card_id(dev->name, card)) {
		return 0;
	}

	/* Card-id fallback is only unambiguous for hw:C; hw:C,D must match subdevice too. */
	if (devnum < 0) {
		return 1;
	}

	if (!radio_parse_alsa_hw_device(dev->name, &hay_card, &hay_dev)) {
		return 0;
	}

	return hay_card == card && hay_dev == devnum;
}

static PaDeviceIndex pa_find_device(const char *hw_device, int want_input, int want_output)
{
	PaDeviceIndex idx, num_devices;

	num_devices = Pa_GetDeviceCount();
	if (num_devices < 0) {
		return paNoDevice;
	}

	for (idx = 0; idx < num_devices; idx++) {
		const PaDeviceInfo *dev = Pa_GetDeviceInfo(idx);

		if (pa_device_matches(dev, hw_device, want_input, want_output)) {
			return idx;
		}
	}

	return paNoDevice;
}

PaError ast_radio_pa_open_device(struct ast_radio_pa_stream *ps, const struct ast_radio_device *device)
{
	PaStreamParameters input_params;
	PaStreamParameters output_params;
	PaError res;
	const PaStreamInfo *si;

	if (!ps) {
		return paBadStreamPtr;
	}
	if (!ast_radio_device_pa_ready(device)) {
		return paDeviceUnavailable;
	}
	ps->output_channels = MIN(AST_RADIO_PA_OUTPUT_CHANNELS, device->pa_output_channels);
	if (!ps->input_channels || ps->input_channels > device->pa_input_channels || !ps->output_channels) {
		return paInvalidChannelCount;
	}

	ps->stream = NULL;
	ps->active = 0;
	if (pa_lib_acquire() < 0) {
		return paInternalError;
	}

	/* Open the exact PortAudio endpoints retained by the device lease */
	input_params.device = device->pa_input_device;
	input_params.channelCount = ps->input_channels;
	input_params.sampleFormat = paInt16;
	input_params.suggestedLatency = (1.0 / 50.0);
	input_params.hostApiSpecificStreamInfo = NULL;
	output_params.device = device->pa_output_device;
	output_params.channelCount = ps->output_channels;
	output_params.sampleFormat = paInt16;
	output_params.suggestedLatency = (1.0 / 50.0);
	output_params.hostApiSpecificStreamInfo = NULL;

	res = Pa_OpenStream(&ps->stream, &input_params, &output_params, AST_RADIO_PA_SAMPLE_RATE, AST_RADIO_PA_FRAMES_PER_BUFFER,
		paNoFlag, NULL, NULL);
	if (res != paNoError) {
		if (ps->stream) {
			Pa_CloseStream(ps->stream);
			ps->stream = NULL;
		}
		pa_lib_release();
		return res;
	}

	si = Pa_GetStreamInfo(ps->stream);
	if (si) {
		ast_debug(5, "PortAudio stream latency in %.3f ms out %.3f ms\n", si->inputLatency * 1000.0, si->outputLatency * 1000.0);
	}
	ast_debug(3, "USB radio device %s PortAudio opened: %u in/%u out\n", device->devstr, ps->input_channels, ps->output_channels);
	return res;
}

PaError ast_radio_pa_start(struct ast_radio_pa_stream *ps)
{
	PaError res;

	if (!ps || !ps->stream) {
		return paBadStreamPtr;
	}

	res = Pa_StartStream(ps->stream);
	if (res == paNoError) {
		ps->active = 1;
	}
	return res;
}

void ast_radio_pa_stop(struct ast_radio_pa_stream *ps)
{
	PaError err;

	if (!ps || !ps->stream) {
		return;
	}

	if (ps->active) {
		err = Pa_AbortStream(ps->stream);
		if (err != paNoError) {
			/* Common after USB yank; Abort/Close often fail once the host device is gone. */
			ast_debug(3, "Pa_AbortStream failed: %s\n", Pa_GetErrorText(err));
		}
	}

	err = Pa_CloseStream(ps->stream);
	if (err != paNoError) {
		ast_debug(3, "Pa_CloseStream failed: %s\n", Pa_GetErrorText(err));
	}

	ps->stream = NULL;
	ps->active = 0;
	pa_lib_release();
}

PaError ast_radio_pa_read(struct ast_radio_pa_stream *ps, short *buf, unsigned long frames, int timeout_ms, volatile sig_atomic_t *stop)
{
	int64_t start;
	PaError err;

	if (!ps || !ps->stream || !buf) {
		return paBadStreamPtr;
	}

	start = pa_now_ms();
	while (!stop || !(*stop)) {
		long avail = Pa_GetStreamReadAvailable(ps->stream);

		if (avail < 0) {
			return (PaError) avail;
		}

		if ((pa_now_ms() - start) > timeout_ms) {
			return paTimedOut;
		}

		if ((unsigned long) avail < frames) {
			usleep(500);
			continue;
		}

		err = Pa_ReadStream(ps->stream, buf, frames);
		return err;
	}

	return paTimedOut;
}

PaError ast_radio_pa_write(struct ast_radio_pa_stream *ps, const short *data, unsigned long pa_frames)
{
	PaError res;
	const short *tx = data;
	short mono_buf[AST_RADIO_PA_FRAMES_PER_BUFFER];

	if (!ps || !ps->stream || !data) {
		return paBadStreamPtr;
	}

	if (pa_frames > AST_RADIO_PA_FRAMES_PER_BUFFER) {
		ast_log(LOG_WARNING, "ast_radio_pa_write: PortAudio frames %lu exceeds buffer capacity\n", pa_frames);
		return paBufferTooBig;
	}

	/*
	 * Callers always pass stereo interleaved samples. Mono URIs (AIOC) open
	 * with one output channel; average L/R like OSS/ALSA plug conversion did.
	 */
	if (ps->output_channels == 1) {
		unsigned long i;

		for (i = 0; i < pa_frames; i++) {
			mono_buf[i] = (short) (((int) data[i * 2] + (int) data[i * 2 + 1]) / 2);
		}
		tx = mono_buf;
	}

	res = Pa_WriteStream(ps->stream, tx, pa_frames);
	if (res == paOutputUnderflowed) {
		PaError prime_res;
		/* Sized for stereo; unused half is fine when priming mono. */
		short null_buf[AST_RADIO_PA_FRAMES_PER_BUFFER * AST_RADIO_PA_OUTPUT_CHANNELS] = { 0 };
		long frames_available;

		/*
		 * Prime the stream with one silence frame so the USB buffer does not
		 * stay empty (choppy TX). See #593 / #598. Propagate a real failure
		 * from the silence write so callers can restart the stream.
		 */
		frames_available = ast_radio_pa_write_available(ps);

		if ((frames_available > 0) && (frames_available >= (long) pa_frames)) {
			ast_debug(6, "PortAudio write stream underflow, priming with %ld silence frames\n", pa_frames);
			prime_res = Pa_WriteStream(ps->stream, null_buf, pa_frames);
			if (prime_res != paNoError) {
				return prime_res;
			}
		} else {
			ast_debug(6, "PortAudio write stream underflow, Unable to prime with %ld silence frames, only %ld available\n",
				pa_frames, frames_available);
		}
	}

	return res;
}

long ast_radio_pa_write_available(struct ast_radio_pa_stream *ps)
{
	if (!ps || !ps->stream) {
		return -1;
	}

	return Pa_GetStreamWriteAvailable(ps->stream);
}

/* Load our configuration */
static int load_config(int reload)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	const char *varval;

	if (!(cfg = ast_config_load(CONFIG_FILE, config_flags))) {
		ast_log(LOG_WARNING, "Config file %s not found\n", CONFIG_FILE);
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		ast_debug(1, "Config file %s unchanged, skipping\n", CONFIG_FILE);
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file %s is in an invalid format. Aborting.\n", CONFIG_FILE);
		return -1;
	}

	if (reload) {
		cleanup_user_devices();
	}

	/* general section
	 * usb_devices format vvvv:pppp,vvvv:pppp where vvvv is usb vendor id and pppp is usb product id
	 */
	if ((varval = ast_variable_retrieve(cfg, "general", "usb_devices")) && !ast_strlen_zero(varval)) {
		struct usb_device_entry *device;
		char *item;
		char *value;
		int idVendor;
		int idProduct;

		value = ast_strdupa(varval);

		/* process the delimited list */
		while ((item = strsep(&value, ","))) {
			if (sscanf(item, "%04x:%04x", &idVendor, &idProduct) == 2) {
				/* allocate space for our device */
				if (!(device = ast_calloc(1, sizeof(*device)))) {
					break;
				}
				device->idVendor = idVendor;
				device->idProduct = idProduct;
				device->idMask = 0xffff;
				/* Add it to our list */
				AST_RWLIST_WRLOCK(&user_devices);
				AST_LIST_INSERT_HEAD(&user_devices, device, entry);
				AST_RWLIST_UNLOCK(&user_devices);

				ast_debug(1, "Loaded user defined usb device %s", item);
			} else {
				ast_log(LOG_WARNING, "USB Device descriptor '%s' is in the wrong format", item);
			}
		}
	}

	ast_config_destroy(cfg);

	return 0;
}

static int reload_module(void)
{
	if (load_config(1)) {
		return AST_MODULE_RELOAD_ERROR;
	}

	/* Apply updated device definitions during the next acquisition */
	ast_mutex_lock(&radio_device_inventory_lock);
	radio_device_inventory.valid = 0;
	ast_mutex_unlock(&radio_device_inventory_lock);

	return AST_MODULE_RELOAD_SUCCESS;
}

static int load_module(void)
{
	AST_LIST_HEAD_INIT_NOLOCK(&radio_device_inventory.available);
	AST_LIST_HEAD_INIT_NOLOCK(&radio_device_inventory.leased);

	if (libusb_init(&usb_ctx) < 0) {
		ast_log(LOG_ERROR, "Unable to initialize libusb\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if (load_config(0)) {
		libusb_exit(usb_ctx);
		usb_ctx = NULL;
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_mutex_lock(&radio_device_inventory_lock);
	if (!AST_LIST_EMPTY(&radio_device_inventory.leased)) {
		ast_mutex_unlock(&radio_device_inventory_lock);
		ast_log(LOG_ERROR, "Unable to unload res_usbradio while USB radio device leases are active\n");
		return -1;
	}
	ast_mutex_unlock(&radio_device_inventory_lock);

	cleanup_user_devices();
	device_inventory_cleanup();
	if (usb_ctx) {
		libusb_exit(usb_ctx);
		usb_ctx = NULL;
	}

	ast_mutex_lock(&pa_lock);
	ast_assert(pa_refcount == 0);
	ast_mutex_unlock(&pa_lock);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "USB Radio Resource",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 5,
);
