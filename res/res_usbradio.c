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
#include <search.h>
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
#include "asterisk/format_cache.h"

/*! \brief lround for uClibc - wrapper for lround(x) */
long ast_radio_lround(double x)
{
	return (long) ((x - ((long) x) >= 0.5f) ? (((long) x) + 1) : ((long) x));
}

int ast_radio_make_spkr_playback_value(int spkrmax, int val, int devtype)
{
	int v, rv;

	v = (val * spkrmax) / 1000;
	/* if just the old one, do it the old way */
	if (devtype == C108_PRODUCT_ID) {
		return v;
	}
	rv = (spkrmax + ast_radio_lround(20.0 * log10((float) (v + 1) / (float) (spkrmax + 1)) / 0.25));
	if (rv < 0) {
		rv = 0;
	}
	return rv;
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
	usb_control_msg(handle, USB_ENDPOINT_OUT + USB_TYPE_CLASS + USB_RECIP_INTERFACE, HID_REPORT_SET, 0 + (HID_RT_OUTPUT << 8), C108_HID_INTERFACE, (char *) outputs, 4, 5000);
}

void ast_radio_hid_get_inputs(struct usb_dev_handle *handle, unsigned char *inputs)
{
	usleep(1500);
	usb_control_msg(handle, USB_ENDPOINT_IN + USB_TYPE_CLASS + USB_RECIP_INTERFACE, HID_REPORT_GET, 0 + (HID_RT_INPUT << 8), C108_HID_INTERFACE, (char *) inputs, 4, 5000);
}

static unsigned short read_eeprom(struct usb_dev_handle *handle, int addr)
{
	unsigned char buf[4];

	buf[0] = 0x80;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = 0x80 | (addr & 0x3f);
	ast_radio_hid_set_outputs(handle, buf);
	memset(buf, 0, sizeof(buf));
	ast_radio_hid_get_inputs(handle, buf);
	return (buf[1] + (buf[2] << 8));
}

static void write_eeprom(struct usb_dev_handle *handle, int addr, unsigned short data)
{
	unsigned char buf[4];

	buf[0] = 0x80;
	buf[1] = data & 0xff;
	buf[2] = data >> 8;
	buf[3] = 0xc0 | (addr & 0x3f);
	ast_radio_hid_set_outputs(handle, buf);
}

unsigned short ast_radio_get_eeprom(struct usb_dev_handle *handle, unsigned short *buf)
{
	int i;
	unsigned short cs;

	cs = 0xffff;
	for (i = EEPROM_START_ADDR; i < EEPROM_END_ADDR; i++) {
		cs += buf[i] = read_eeprom(handle, i);
	}
	return (cs);
}

void ast_radio_put_eeprom(struct usb_dev_handle *handle, unsigned short *buf)
{
	int i;
	unsigned short cs;

	cs = 0xffff;
	buf[EEPROM_MAGIC_ADDR] = EEPROM_MAGIC;
	for (i = EEPROM_START_ADDR; i < EEPROM_CS_ADDR; i++) {
		write_eeprom(handle, i, buf[i]);
		cs += buf[i];
	}
	buf[EEPROM_CS_ADDR] = (65535 - cs) + 1;
	write_eeprom(handle, i, buf[EEPROM_CS_ADDR]);
}

struct usb_device *ast_radio_hid_device_init(char *desired_device)
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
			if ((dev->descriptor.idVendor
				 == C108_VENDOR_ID) &&
				(((dev->descriptor.idProduct & 0xfffc) == C108_PRODUCT_ID) ||
				 (dev->descriptor.idProduct == C108B_PRODUCT_ID) ||
				 (dev->descriptor.idProduct == C108AH_PRODUCT_ID) ||
				 (dev->descriptor.idProduct == C119A_PRODUCT_ID) ||
				 (dev->descriptor.idProduct == C119B_PRODUCT_ID) ||
				 ((dev->descriptor.idProduct & 0xff00) == N1KDO_PRODUCT_ID) ||
				 (dev->descriptor.idProduct == C119_PRODUCT_ID))) {
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

int ast_radio_usb_get_usbdev(char *devstr)
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
					ast_log(LOG_ERROR, "Cant get io permission on IO port %04x hex, disabling pp support\n", *pbase);
					*haspp = 0;
				}
				*haspp = 2;
				ast_verb(3, "Using direct IO port for pp support, since parport driver not available.\n");
#else
				ast_log(LOG_ERROR, "pp IO not supported on this architecture\n");
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
	ast_log(LOG_ERROR, "pp IO not supported on this architecture\n");
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
	ast_log(LOG_ERROR, "pp IO not supported on this architecture\n");
#endif
	return;
}

static int load_module(void)
{
	return 0;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "USB Radio Resource",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 5,
);
