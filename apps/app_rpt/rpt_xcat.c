
/*! \file
 *
 * \brief XCAT I/O handlers
 */

#include "asterisk.h"

#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"

#include "app_rpt.h"
#include "rpt_serial.h"
#include "rpt_channel.h" /* use send_usb_txt */
#include "rpt_utils.h" /* use split_freq */
#include "rpt_serial.h"
#include "rpt_xcat.h"

int check_freq_xcat(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if (m == 144) {				/* 2 meters */
		if (d < 10100)
			return -1;
	}
	if (m == 29) {				/* 10 meters */
		if (d > 70000)
			return -1;
	} else if ((m >= 28) && (m < 30)) {
		;
	} else if ((m >= 50) && (m < 54)) {
		;
	} else if ((m >= 144) && (m < 148)) {
		;
	} else if ((m >= 420) && (m < 450)) {	/* 70 centimeters */
		;
	} else
		return -1;

	if (defmode)
		*defmode = dflmd;

	return 0;
}

static int simple_command_xcat(struct rpt *myrpt, char command, char subcommand)
{
	unsigned char cmdstr[10];

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = command;
	cmdstr[5] = subcommand;
	cmdstr[6] = 0xfd;

	return (civ_cmd(myrpt, cmdstr, 7));
}

/*
* Set a new frequency for the xcat
*/

static int set_freq_xcat(struct rpt *myrpt, char *newfreq)
{
	unsigned char cmdstr[20];
	char mhz[MAXREMSTR], decimals[MAXREMSTR];
	int m, d;

	ast_debug(7, "newfreq:%s\n", newfreq);

	if (split_freq(mhz, decimals, newfreq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	/* The ic-706 likes packed BCD frequencies */

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 5;
	cmdstr[5] = ((d % 10) << 4);
	cmdstr[6] = (((d % 1000) / 100) << 4) + ((d % 100) / 10);
	cmdstr[7] = ((d / 10000) << 4) + ((d % 10000) / 1000);
	cmdstr[8] = (((m % 100) / 10) << 4) + (m % 10);
	cmdstr[9] = (m / 100);
	cmdstr[10] = 0xfd;

	return (civ_cmd(myrpt, cmdstr, 11));
}

static int set_offset_xcat(struct rpt *myrpt, char offset)
{
	unsigned char c, cmdstr[20];
	int mysplit;
	char mhz[MAXREMSTR], decimal[MAXREMSTR];

	if (split_freq(mhz, decimal, myrpt->freq))
		return -1;

	mysplit = myrpt->splitkhz * 1000;
	if (!mysplit) {
		if (atoi(mhz) > 400)
			mysplit = myrpt->p.default_split_70cm * 1000;
		else
			mysplit = myrpt->p.default_split_2m * 1000;
	}

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0xaa;
	cmdstr[5] = 0x06;
	cmdstr[6] = mysplit & 0xff;
	cmdstr[7] = (mysplit >> 8) & 0xff;
	cmdstr[8] = (mysplit >> 16) & 0xff;
	cmdstr[9] = (mysplit >> 24) & 0xff;
	cmdstr[10] = 0xfd;

	if (civ_cmd(myrpt, cmdstr, 11) < 0)
		return -1;

	switch (offset) {
	case REM_SIMPLEX:
		c = 0x10;
		break;

	case REM_MINUS:
		c = 0x11;
		break;

	case REM_PLUS:
		c = 0x12;
		break;

	default:
		return -1;
	}

	return simple_command_xcat(myrpt, 0x0f, c);

}

/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_xcat(struct rpt *myrpt, char *txtone, char *rxtone)
{
	unsigned char cmdstr[10];
	char hertz[MAXREMSTR], decimal[MAXREMSTR];
	int h, d, rv;

	memset(cmdstr, 0, 5);

	ast_debug(7, "txtone=%s  rxtone=%s \n", txtone, rxtone);

	if (split_ctcss_freq(hertz, decimal, txtone))
		return -1;

	h = atoi(hertz);
	d = atoi(decimal);

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x1b;
	cmdstr[5] = 0;
	cmdstr[6] = ((h / 100) << 4) + (h % 100) / 10;
	cmdstr[7] = ((h % 10) << 4) + (d % 10);
	cmdstr[8] = 0xfd;

	rv = civ_cmd(myrpt, cmdstr, 9);
	if (rv)
		return (-1);

	if (!rxtone)
		return (0);

	if (split_ctcss_freq(hertz, decimal, rxtone))
		return -1;

	h = atoi(hertz);
	d = atoi(decimal);

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x1b;
	cmdstr[5] = 1;
	cmdstr[6] = ((h / 100) << 4) + (h % 100) / 10;
	cmdstr[7] = ((h % 10) << 4) + (d % 10);
	cmdstr[8] = 0xfd;
	return (civ_cmd(myrpt, cmdstr, 9));
}

int set_xcat(struct rpt *myrpt)
{
	int res = 0;

	/* set Mode */
	ast_debug(2, "Mode\n");
	if (!res)
		res = simple_command_xcat(myrpt, 8, 1);
	ast_debug(2, "Offset Initial/Simplex\n");
	if (!res)
		res = set_offset_xcat(myrpt, REM_SIMPLEX);	/* Offset */
	/* set Freq */
	ast_debug(2, "Frequency\n");
	if (!res)
		res = set_freq_xcat(myrpt, myrpt->freq);	/* Frequency */
	ast_debug(2, "Offset\n");
	if (!res)
		res = set_offset_xcat(myrpt, myrpt->offset);	/* Offset */
	ast_debug(2, "CTCSS\n");
	if (!res)
		res = set_ctcss_freq_xcat(myrpt, myrpt->txplon ? myrpt->txpl : "0.0", myrpt->rxplon ? myrpt->rxpl : "0.0");	/* Tx/Rx CTCSS */
	/* set Freq */
	ast_debug(2, "Frequency\n");
	if (!res)
		res = set_freq_xcat(myrpt, myrpt->freq);	/* Frequency */
	return res;
}
