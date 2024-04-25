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

/*!
 * \brief Enable or disable CTCSS/DCS decode for RX
 * \param myrpt
 * \param enable 1 to enable, 0 to disable
 * \retval -1 on failure, 0 on success, 1 if not applicable to channel tech
 */
int rpt_radio_rx_set_ctcss_decode(struct rpt *myrpt, int enable);

/*!
 * \brief Block CTCSS/DCS encode
 * \param chan
 * \param block 1 to block, 0 to not block
 * \retval -1 on failure, 0 on success, 1 if not applicable to channel tech
 */
int dahdi_radio_set_ctcss_encode(struct ast_channel *chan, int block);

/*! \note Based on DAHDI_RADPAR values in dahdi/user.h */
enum rpt_radpar {
	RPT_RADPAR_IGNORECT = 3,
	RPT_RADPAR_NOENCODE  = 4,
	RPT_RADPAR_UIODATA = 14,
	RPT_RADPAR_UIOMODE = 15,
	RPT_RADPAR_REMMODE = 16,
	RPT_RADPAR_REMCOMMAND = 17,
};

enum rpt_radpar_data {
	RPT_RADPAR_REM_NONE = 0,
	RPT_RADPAR_REM_RBI1 = 1,
	RPT_RADPAR_REM_SERIAL = 2,
	RPT_RADPAR_REM_SERIAL_ASCII = 3,
};

int rpt_radio_set_param(struct ast_channel *chan, struct rpt *myrpt, enum rpt_radpar par, enum rpt_radpar_data data);

int rpt_radio_set_remcommand_data(struct ast_channel *chan, struct rpt *myrpt, unsigned char *data, int len);

int rpt_pciradio_serial_remote_io(struct rpt *myrpt, unsigned char *txbuf, int txbytes, unsigned char *rxbuf, int rxmaxbytes, int asciiflag);
