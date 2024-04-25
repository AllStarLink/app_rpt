/*
 * AllStarLink and app_rpt is a module for Asterisk
 *
 * Copyright (C) 2002-2017, Jim Dixon, WB6NIL and AllStarLink, Inc.
 *     and contributors.
 * Copyright (C) 2018 Steve Zingman N4IRS, Michael Zingman N4IRR,
 *    AllStarLink, Inc. and contributors.
 * Copyright (C) 2018-2020 Stacy Olivas KG7QIN and contributors. 
 * Copyright (C) 2020-2024 AllStarLink, Inc., Naveen Albert, 
 *    Danny Lloyd KB4MDD, and contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License v2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 * See https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt for
 * the full license text.
 */

#include "asterisk.h"

#include <dahdi/user.h>

#include "asterisk/channel.h"

#include "app_rpt.h"
#include "rpt_bridging.h"
#include "rpt_radio.h"

static int dahdi_set_radpar(struct ast_channel *chan, int param, int data)
{
	struct dahdi_radio_param r;

	memset(&r, 0, sizeof(struct dahdi_radio_param));

	r.radpar = param;
	r.data = data;

	if (ioctl(ast_channel_fd(chan, 0), DAHDI_RADIO_SETPARAM, &r) == -1) {
		/* Don't log as error here, in case it's nothing to worry about */
		ast_debug(1, "Failed to set radio parameter on %s: %s\n", ast_channel_name(chan), strerror(errno));
		return -1;
	}
	return 0;
}

static int dahdi_radio_set_ctcss_decode(struct ast_channel *chan, int enable)
{
	int res = dahdi_set_radpar(chan, DAHDI_RADPAR_IGNORECT, enable);
	if (res) {
		ast_log(LOG_WARNING, "Failed to set ignore CTCSS/DCS decode: %s\n", strerror(errno));
	}
	return res;
}

int rpt_radio_rx_set_ctcss_decode(struct rpt *myrpt, int enable)
{
	if (!strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "DAHDI")) {
		return dahdi_radio_set_ctcss_decode(myrpt->dahdirxchannel, enable);
	}
	return 1;
}

int dahdi_radio_set_ctcss_encode(struct ast_channel *chan, int block)
{
	return dahdi_set_radpar(chan, DAHDI_RADPAR_NOENCODE, block);
}

int rpt_radio_set_param(struct ast_channel *chan, struct rpt *myrpt, enum rpt_radpar par, enum rpt_radpar_data data)
{
	/* rpt_radpar and rpt_radpar_data maps to RPT_RADPAR values exactly,
	 * so for now we can just pass them on. */
	return dahdi_set_radpar(chan, par, data);
}

int rpt_radio_set_remcommand_data(struct ast_channel *chan, struct rpt *myrpt, unsigned char *data, int len)
{
	struct dahdi_radio_param r;

	r.radpar = DAHDI_RADPAR_REMCOMMAND;
	memcpy(&r.data, data, len);
	if (ioctl(ast_channel_fd(myrpt->dahdirxchannel, 0), DAHDI_RADIO_SETPARAM, &r) == -1) {
		ast_log(LOG_WARNING, "Cannot send RBI command for channel %s: %s\n", ast_channel_name(chan), strerror(errno));
		return -1;
	}
	return 0;
}

int rpt_pciradio_serial_remote_io(struct rpt *myrpt, unsigned char *txbuf, int txbytes, unsigned char *rxbuf, int rxmaxbytes, int asciiflag)
{
	int i, index, oldmode, olddata;
	struct dahdi_radio_param prm;

	prm.radpar = DAHDI_RADPAR_UIOMODE;
	if (ioctl(ast_channel_fd(myrpt->dahdirxchannel, 0), DAHDI_RADIO_GETPARAM, &prm) == -1) {
		return -1;
	}
	oldmode = prm.data;
	prm.radpar = DAHDI_RADPAR_UIODATA;
	if (ioctl(ast_channel_fd(myrpt->dahdirxchannel, 0), DAHDI_RADIO_GETPARAM, &prm) == -1) {
		return -1;
	}
	olddata = prm.data;
	prm.radpar = DAHDI_RADPAR_REMMODE;
	if ((asciiflag & 1) && strcmp(myrpt->remoterig, REMOTE_RIG_TM271) && strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD)) {
		if (rpt_radio_set_param(myrpt->dahdirxchannel, myrpt, RPT_RADPAR_REMMODE, RPT_RADPAR_REM_SERIAL_ASCII)) {
			return -1;
		}
	} else {
		if (rpt_radio_set_param(myrpt->dahdirxchannel, myrpt, RPT_RADPAR_REMMODE, RPT_RADPAR_REM_SERIAL)) {
			return -1;
		}
	}

	if (asciiflag & 2) {
		if (dahdi_set_onhook(myrpt->dahdirxchannel)) {
			return -1;
		}
		usleep(100000);
	}
	if ((!strcmp(myrpt->remoterig, REMOTE_RIG_TM271)) || (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD))) {
		for (i = 0; i < txbytes - 1; i++) {

			prm.radpar = DAHDI_RADPAR_REMCOMMAND;
			prm.data = 0;
			prm.buf[0] = txbuf[i];
			prm.index = 1;
			if (ioctl(ast_channel_fd(myrpt->dahdirxchannel, 0), DAHDI_RADIO_SETPARAM, &prm) == -1)
				return -1;
			usleep(6666);
		}
		prm.radpar = DAHDI_RADPAR_REMMODE;
		if (asciiflag & 1)
			prm.data = DAHDI_RADPAR_REM_SERIAL_ASCII;
		else
			prm.data = DAHDI_RADPAR_REM_SERIAL;
		if (ioctl(ast_channel_fd(myrpt->dahdirxchannel, 0), DAHDI_RADIO_SETPARAM, &prm) == -1)
			return -1;
		prm.radpar = DAHDI_RADPAR_REMCOMMAND;
		prm.data = rxmaxbytes;
		prm.buf[0] = txbuf[i];
		prm.index = 1;
	} else {
		prm.radpar = DAHDI_RADPAR_REMCOMMAND;
		prm.data = rxmaxbytes;
		memcpy(prm.buf, txbuf, txbytes);
		prm.index = txbytes;
	}
	if (ioctl(ast_channel_fd(myrpt->dahdirxchannel, 0), DAHDI_RADIO_SETPARAM, &prm) == -1)
		return -1;
	if (rxbuf) {
		*rxbuf = 0;
		memcpy(rxbuf, prm.buf, prm.index);
	}
	index = prm.index;
	if (rpt_radio_set_param(myrpt->dahdirxchannel, myrpt, RPT_RADPAR_REMMODE, RPT_RADPAR_REM_NONE)) {
		return -1;
	}
	if (asciiflag & 2) {
		if (dahdi_set_offhook(myrpt->dahdirxchannel)) {
			return -1;
		}
	}
	if (rpt_radio_set_param(myrpt->dahdirxchannel, myrpt, RPT_RADPAR_UIOMODE, oldmode)) {
		return -1;
	}
	if (rpt_radio_set_param(myrpt->dahdirxchannel, myrpt, RPT_RADPAR_UIODATA, olddata)) {
		return -1;
	}
	return index;
}
