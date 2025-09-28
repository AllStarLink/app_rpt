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
 *
 * \brief Resource module for chan_usbradio and chan_simpleusb
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 */

/*** MODULEINFO
	<depend>alsa</depend>
	<support_level>extended</support_level>
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
#include <errno.h>
#include <usb.h>
#include <linux/ppdev.h>
#include <linux/parport.h>
#include <linux/version.h>
#include <alsa/asoundlib.h>

#include "asterisk/res_usbradio.h"

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
#include "asterisk/poll-compat.h" 	/* Used for polling */
#include "asterisk/config.h"

#define CONFIG_FILE "res_usbradio.conf"

AST_MUTEX_DEFINE_STATIC(usb_list_lock);

/*! 
 * \var usb_device_list
 * \brief Each device string in usb_device_list is delimited with zero.  The
 * final element is zero.
 */
static char *usb_device_list = NULL;
static int usb_device_list_size = 0;

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
	{ C108_VENDOR_ID, C108_PRODUCT_ID,   0xfffc, {NULL} },
	{ C108_VENDOR_ID, C108B_PRODUCT_ID,  0xffff, {NULL} },
	{ C108_VENDOR_ID, C108AH_PRODUCT_ID, 0xffff, {NULL} },
	{ C108_VENDOR_ID, C119A_PRODUCT_ID,  0xffff, {NULL} },
	{ C108_VENDOR_ID, C119B_PRODUCT_ID,  0xffff, {NULL} },
	{ C108_VENDOR_ID, N1KDO_PRODUCT_ID,  0xff00, {NULL} },
	{ C108_VENDOR_ID, C119_PRODUCT_ID,   0xffff, {NULL} },
};

/*! 
 * \brief Linked list of user defined usb devices.
 */
static AST_RWLIST_HEAD_STATIC(user_devices, usb_device_entry);

long ast_radio_lround(double x)
{
	return (long) ((x - ((long) x) >= 0.5f) ? (((long) x) + 1) : ((long) x));
}

int ast_radio_make_spkr_playback_value(int spkrmax, int request_value, int devtype)
{
	int v;
	
	switch (devtype) {
		case C108_PRODUCT_ID:
			v = (request_value * spkrmax) / 1000;
			return v;
			
		case C119B_PRODUCT_ID:
			v = (request_value * spkrmax) / C119B_ADJUSTMENT;
			return v;
			
		default:
			v = (request_value * spkrmax) / 1000;
			return v;
	}
}

int ast_radio_amixer_max(int devnum, char *param)
{
	int rv, type;
	char str[100];
	snd_hctl_t *hctl;
	snd_ctl_elem_id_t *id;
	snd_hctl_elem_t *elem;
	snd_ctl_elem_info_t *info;

	sprintf(str, "hw:%d", devnum);
	if (snd_hctl_open(&hctl, str, 0)) {
		return -1;
	}
	snd_hctl_load(hctl);
	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, param);
	elem = snd_hctl_find_elem(hctl, id);
	if (!elem) {
		snd_hctl_close(hctl);
		return -1;
	}
	snd_ctl_elem_info_alloca(&info);
	snd_hctl_elem_info(elem, info);
	type = snd_ctl_elem_info_get_type(info);
	rv = 0;
	switch (type) {
	case SND_CTL_ELEM_TYPE_INTEGER:
		rv = snd_ctl_elem_info_get_max(info);
		break;
	case SND_CTL_ELEM_TYPE_BOOLEAN:
		rv = 1;
		break;
	}
	snd_hctl_close(hctl);
	return rv;
}

int ast_radio_setamixer(int devnum, char *param, int v1, int v2)
{
	int type;
	char str[100];
	snd_hctl_t *hctl;
	snd_ctl_elem_id_t *id;
	snd_ctl_elem_value_t *control;
	snd_hctl_elem_t *elem;
	snd_ctl_elem_info_t *info;

	sprintf(str, "hw:%d", devnum);
	if (snd_hctl_open(&hctl, str, 0)) {
		return -1;
	}
	snd_hctl_load(hctl);
	snd_ctl_elem_id_alloca(&id);
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, param);
	elem = snd_hctl_find_elem(hctl, id);
	if (!elem) {
		snd_hctl_close(hctl);
		return -1;
	}
	snd_ctl_elem_info_alloca(&info);
	snd_hctl_elem_info(elem, info);
	type = snd_ctl_elem_info_get_type(info);
	snd_ctl_elem_value_alloca(&control);
	snd_ctl_elem_value_set_id(control, id);
	switch (type) {
	case SND_CTL_ELEM_TYPE_INTEGER:
		snd_ctl_elem_value_set_integer(control, 0, v1);
		if (v2 > 0) {
			snd_ctl_elem_value_set_integer(control, 1, v2);
		}
		break;
	case SND_CTL_ELEM_TYPE_BOOLEAN:
		snd_ctl_elem_value_set_integer(control, 0, (v1 != 0));
		break;
	}
	if (snd_hctl_elem_write(elem, control)) {
		snd_hctl_close(hctl);
		return -1;
	}
	snd_hctl_close(hctl);
	return 0;
}

void ast_radio_hid_set_outputs(struct usb_dev_handle *handle, unsigned char *outputs)
{
	usleep(1500);
	usb_control_msg(handle, USB_ENDPOINT_OUT + USB_TYPE_CLASS + USB_RECIP_INTERFACE, 
		HID_REPORT_SET, 0 + (HID_RT_OUTPUT << 8), C108_HID_INTERFACE, (char *) outputs, 4, 5000);
}

void ast_radio_hid_get_inputs(struct usb_dev_handle *handle, unsigned char *inputs)
{
	usleep(1500);
	usb_control_msg(handle, USB_ENDPOINT_IN + USB_TYPE_CLASS + USB_RECIP_INTERFACE, 
		HID_REPORT_GET, 0 + (HID_RT_INPUT << 8), C108_HID_INTERFACE, (char *) inputs, 4, 5000);
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
static unsigned short read_eeprom(struct usb_dev_handle *handle, int addr)
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
static void write_eeprom(struct usb_dev_handle *handle, int addr, unsigned short data)
{
	unsigned char buf[4];

	buf[0] = 0x80;
	buf[1] = data & 0xff;
	buf[2] = data >> 8;
	buf[3] = 0xc0 | (addr & 0x3f);
	
	usleep(2000);
	ast_radio_hid_set_outputs(handle, buf);
}

unsigned short ast_radio_get_eeprom(struct usb_dev_handle *handle, unsigned short *buf)
{
	int i;
	unsigned short cs;

	cs = 0xffff;
	for (i = EEPROM_START_ADDR; i <= EEPROM_START_ADDR + EEPROM_USER_CS_ADDR; i++) {
		cs += buf[i - EEPROM_START_ADDR] = read_eeprom(handle, i);
	}

	return (cs);
}

void ast_radio_put_eeprom(struct usb_dev_handle *handle, unsigned short *buf)
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
static int is_known_device(struct usb_device *dev)
{
	int index;
	int matched_entry = 0;
	
	for (index = 0; index < ARRAY_LEN(known_devices); index++) {
		if (known_devices[index].idVendor == dev->descriptor.idVendor &&
			known_devices[index].idProduct == (dev->descriptor.idProduct & known_devices[index].idMask)) {
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
static int is_user_device(struct usb_device *dev)
{
	struct usb_device_entry *device;
	
	AST_RWLIST_RDLOCK(&user_devices);
	AST_LIST_TRAVERSE(&user_devices, device, entry) {
		if (dev->descriptor.idVendor == device->idVendor && 
			dev->descriptor.idProduct == device->idProduct) {
			break;
		};
	}
	AST_RWLIST_UNLOCK(&user_devices);

	return device ? 1 : 0;
}

int ast_radio_hid_device_mklist(void)
{
	struct usb_bus *usb_bus;
	struct usb_device *dev;
	char devstr[10000], str[200], desdev[200], *cp;
	int i;
	FILE *fp;

	ast_mutex_lock(&usb_list_lock);
	/* See usb_device_list definition for the format */
	if (usb_device_list) {
		ast_free(usb_device_list);
	}
	usb_device_list = ast_calloc(1, 2);
	if (!usb_device_list) {
		ast_mutex_unlock(&usb_list_lock);
		return -1;
	}

	usb_init();
	usb_find_busses();
	usb_find_devices();
	for (usb_bus = usb_busses; usb_bus; usb_bus = usb_bus->next) {
		for (dev = usb_bus->devices; dev; dev = dev->next) {
			if (is_known_device(dev) || is_user_device(dev)) {
				sprintf(devstr, "%s/%s", usb_bus->dirname, dev->filename);
				for (i = 0; i < 32; i++) {
					sprintf(str, "/proc/asound/card%d/usbbus", i);
					fp = fopen(str, "r");
					if (!fp) {
						continue;
					}
					if ((!fgets(desdev, sizeof(desdev) - 1, fp)) || (!desdev[0])) {
						fclose(fp);
						continue;
					}
					fclose(fp);
					if (desdev[strlen(desdev) - 1] == '\n') {
						desdev[strlen(desdev) - 1] = 0;
					}
					if (strcasecmp(desdev, devstr)) {
						continue;
					}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)) && !defined(AST_BUILDOPT_LIMEY)
					sprintf(str, "/sys/class/sound/card%d/device", i);
					memset(desdev, 0, sizeof(desdev));
					if (readlink(str, desdev, sizeof(desdev) - 1) == -1) {
						continue;
					}
					cp = strrchr(desdev, '/');
					if (!cp) {
						continue;
					}
					cp++;
#else
					if (i) {
						sprintf(str, "/sys/class/sound/dsp%d/device", i);
					} else {
						strcpy(str, "/sys/class/sound/dsp/device");
					}
					memset(desdev, 0, sizeof(desdev));
					if (readlink(str, desdev, sizeof(desdev) - 1) == -1) {
						sprintf(str, "/sys/class/sound/controlC%d/device", i);
						memset(desdev, 0, sizeof(desdev));
						if (readlink(str, desdev, sizeof(desdev) - 1) == -1) {
							continue;
						}
					}
					cp = strrchr(desdev, '/');
					if (cp) {
						*cp = 0;
					} else {
						continue;
					}
					cp = strrchr(desdev, '/');
					if (!cp) {
						continue;
					}
					cp++;
#endif
					break;
				}
				if (i >= 32) {
					ast_mutex_unlock(&usb_list_lock);
					return -1;
				}
				usb_device_list = ast_realloc(usb_device_list, usb_device_list_size + 2 + strlen(cp));
				if (!usb_device_list) {
					ast_mutex_unlock(&usb_list_lock);
					return -1;
				}
				usb_device_list_size += strlen(cp) + 2;
				i = 0;
				while (usb_device_list[i]) {
					i += strlen(usb_device_list + i) + 1;
				}
				strcat(usb_device_list + i, cp);
				usb_device_list[strlen(cp) + i + 1] = 0;
			}
		}
	}
	ast_mutex_unlock(&usb_list_lock);
	return 0;
}

struct usb_device *ast_radio_hid_device_init(const char *desired_device)
{
	struct usb_bus *usb_bus;
	struct usb_device *dev;
	char devstr[10000], str[200], desdev[200], *cp;
	int i;
	FILE *fp;

	usb_init();
	usb_find_busses();
	usb_find_devices();
	for (usb_bus = usb_busses; usb_bus; usb_bus = usb_bus->next) {
		for (dev = usb_bus->devices; dev; dev = dev->next) {
			if (is_known_device(dev) || is_user_device(dev)) {
				sprintf(devstr, "%s/%s", usb_bus->dirname, dev->filename);
				for (i = 0; i < 32; i++) {
					sprintf(str, "/proc/asound/card%d/usbbus", i);
					fp = fopen(str, "r");
					if (!fp) {
						continue;
					}
					if ((!fgets(desdev, sizeof(desdev) - 1, fp)) || (!desdev[0])) {
						fclose(fp);
						continue;
					}
					fclose(fp);
					if (desdev[strlen(desdev) - 1] == '\n') {
						desdev[strlen(desdev) - 1] = 0;
					}
					if (strcasecmp(desdev, devstr)) {
						continue;
					}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)) && !defined(AST_BUILDOPT_LIMEY)
					sprintf(str, "/sys/class/sound/card%d/device", i);
					memset(desdev, 0, sizeof(desdev));
					if (readlink(str, desdev, sizeof(desdev) - 1) == -1) {
						continue;
					}
					cp = strrchr(desdev, '/');
					if (!cp) {
						continue;
					}
					cp++;
#else
					if (i) {
						sprintf(str, "/sys/class/sound/dsp%d/device", i);
					} else {
						strcpy(str, "/sys/class/sound/dsp/device");
					}
					memset(desdev, 0, sizeof(desdev));
					if (readlink(str, desdev, sizeof(desdev) - 1) == -1) {
						sprintf(str, "/sys/class/sound/controlC%d/device", i);
						memset(desdev, 0, sizeof(desdev));
						if (readlink(str, desdev, sizeof(desdev) - 1) == -1) {
							continue;
						}
					}
					cp = strrchr(desdev, '/');
					if (cp) {
						*cp = 0;
					} else {
						continue;
					}
					cp = strrchr(desdev, '/');
					if (!cp) {
						continue;
					}
					cp++;
#endif
					break;
				}
				if (i >= 32) {
					continue;
				}
				if (!strcmp(cp, desired_device)) {
					return dev;
				}
			}
		}
	}
	return NULL;
}

int ast_radio_usb_get_usbdev(const char *devstr)
{
	int i;
	char str[200], desdev[200], *cp;

	for (i = 0; i < 32; i++) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)) && !defined(AST_BUILDOPT_LIMEY)
		sprintf(str, "/sys/class/sound/card%d/device", i);
		memset(desdev, 0, sizeof(desdev));
		if (readlink(str, desdev, sizeof(desdev) - 1) == -1) {
			continue;
		}
		cp = strrchr(desdev, '/');
		if (!cp) {
			continue;
		}
		cp++;
#else
		if (i) {
			sprintf(str, "/sys/class/sound/dsp%d/device", i);
		} else {
			strcpy(str, "/sys/class/sound/dsp/device");
		}
		memset(desdev, 0, sizeof(desdev));
		if (readlink(str, desdev, sizeof(desdev) - 1) == -1) {
			sprintf(str, "/sys/class/sound/controlC%d/device", i);
			memset(desdev, 0, sizeof(desdev));
			if (readlink(str, desdev, sizeof(desdev) - 1) == -1) {
				continue;
			}
		}
		cp = strrchr(desdev, '/');
		if (cp) {
			*cp = 0;
		} else {
			continue;
		}
		cp = strrchr(desdev, '/');
		if (!cp) {
			continue;
		}
		cp++;
#endif
		if (!strcasecmp(cp, devstr)) {
			break;
		}
	}
	if (i >= 32) {
		return -1;
	}
	return i;
}

int ast_radio_usb_list_check(char *devstr)
{
	/* See usb_device_list definition for the format */
	char *s = usb_device_list;
	int res = 0;

	if (!s) {
		return 0;
	}
	
	ast_mutex_lock(&usb_list_lock);

	while (*s) {
		if (!strcasecmp(s, devstr)) {
			res = 1;
			break;
		}
		s += strlen(s) + 1;
	}
	
	ast_mutex_unlock(&usb_list_lock);
	
	return res;
}

char* ast_radio_usb_get_devstr(int index)
{
	/* See usb_device_list definition for the format */
	char *s = usb_device_list;
	int devstr_index = 0;
	
	ast_mutex_lock(&usb_list_lock);

	while (*s) {
		if (index == devstr_index) {
			break;
		}
		devstr_index++;
		s += strlen(s) + 1;
	}
	
	ast_mutex_unlock(&usb_list_lock);
	
	return s;
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
	if (haspp == 1) {			/* if its a pp dev */
		if (ioctl(ppfd, PPRSTATUS, &c) == -1) {
			ast_log(LOG_ERROR, "Unable to read pp dev %s\n", pport);
			c = 0;
		}
	}
	if (haspp == 2) {			/* if its a direct I/O */
		c = inb(pbase + 1);
	}
	return (c);
#else
	ast_log(LOG_ERROR, "Parallel port I/O is not supported on this architecture\n");
	return 0;
#endif
}

void ast_radio_ppwrite(int haspp, unsigned int ppfd, unsigned int pbase, const char *pport, unsigned char c)
{
#ifdef HAVE_SYS_IO
	if (haspp == 1) {			/* if its a pp dev */
		if (ioctl(ppfd, PPWDATA, &c) == -1) {
			ast_log(LOG_ERROR, "Unable to write pp dev %s\n", pport);
		}
	}
	if (haspp == 2) {			/* if its a direct I/O */
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
	
	tv.tv_sec = ts.tv_sec;
	tv.tv_usec = ts.tv_nsec / 1000;
	
	return tv;
}

#define CLIP_SAMP_THRESH       0x7eb0
#define CLIP_EVENT_MIN_SAMPLES 3
int ast_radio_check_audio(short *sbuf, struct audiostatistics *o, short len)
{
	unsigned short i, j, val, max = 0, seq_clips = 0;
	double pwr = 0.0;
	short buf[FRAME_SIZE], last_clip = -1;

	/* validate len and index */
	if (len > 12 * FRAME_SIZE) {
		len = 12 * FRAME_SIZE;
	}
	if (o->index >= AUDIO_STATS_LEN) {
		o->index = 0;
	}
	/* Downsample from 48000 stereo to 8000 mono */
	for (i = 10, j = 0; i < len; i += 12) {
		buf[j++] = sbuf[i];
	}
	len /= 12;
	/* len should now be 160 */
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
	o->pwrbuf[o->index] = (unsigned int) (pwr / (double)len);
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
	sprintf(s1, "%sAudioStats: Pk %5.1f  Avg Pwr %3.0f  Min %3.0f  Max %3.0f  dBFS  ClipCnt %u",
			prefix_text, dpk, tpwr, dmin, dmax, clipcnt);
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
				if(!(device = ast_calloc(1, sizeof(*device)))) {
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
				ast_log(LOG_WARNING,"USB Device descriptor '%s' is in the wrong format", item);
			}
		}
	}
	
	ast_config_destroy(cfg);

	return 0;
}

static int reload_module(void)
{
	return load_config(1);
}

static int load_module(void)
{
	if (load_config(0)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return 0;
}

static int unload_module(void)
{
	cleanup_user_devices();
	
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "USB Radio Resource",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 5,
);
