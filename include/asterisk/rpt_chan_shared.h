/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025, Mike Kasper
 *
 * Mike Kasper, N8RAW <n8raw@n8raw.org>
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
 * \brief Radio resources.
 */

enum radio_rx_audio {
	RX_AUDIO_NONE,
	RX_AUDIO_SPEAKER,
	RX_AUDIO_FLAT
};

enum radio_carrier_detect {
	CD_IGNORE,
	CD_XPMR_NOISE,
	CD_XPMR_VOX,
	CD_HID,
	CD_HID_INVERT,
	CD_PP,
	CD_PP_INVERT
};

enum radio_squelch_detect {
	SD_IGNORE,	   /* no */
	SD_HID,		   /* external */
	SD_HID_INVERT, /* external inverted */
	SD_XPMR,	   /* software */
	SD_PP,		   /* Parallel port */
	SD_PP_INVERT   /*Parallel port inverted */
};

enum radio_tx_mix {
	TX_OUT_OFF,	  /* Off */
	TX_OUT_VOICE, /* Voice */
	TX_OUT_LSD,
	TX_OUT_COMPOSITE, /* Composite */
	TX_OUT_AUX		  /* Auxiliary */
};

enum usbradio_carrier_type {
	TOC_NONE,
	TOC_PHASE,
	TOC_NOTONE
};

enum usbradio_pager {
	PAGER_NONE,
	PAGER_A,
	PAGER_B
};
