
#include "asterisk.h"

#include <dahdi/user.h> /* use RAD_SERIAL_BUFLEN */

#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"

#include "app_rpt.h"
#include "rpt_serial.h"
#include "rpt_channel.h" /* use send_usb_txt */
#include "rpt_rig.h"
#include "rpt_xcat.h"
#include "rpt_config.h" /* use get_mem_set */
#include "rpt_utils.h"
#include "rpt_telemetry.h"

static int sendkenwood(struct rpt *myrpt, char *txstr, char *rxstr)
{
	int i;

	ast_debug(1, "Send to kenwood: %s\n", txstr);
	i = serial_remote_io(myrpt, (unsigned char *) txstr, strlen(txstr), (unsigned char *) rxstr, RAD_SERIAL_BUFLEN - 1, 3);
	usleep(50000);
	if (i < 0)
		return -1;
	if ((i > 0) && (rxstr[i - 1] == '\r'))
		rxstr[i-- - 1] = 0;
	ast_debug(1, "Got from kenwood: %s\n", rxstr);
	return (i);
}

/* take a PL frequency and turn it into a code */
static int kenwood_pltocode(char *str)
{
	int i;
	char *s;

	s = strchr(str, '.');
	i = 0;
	if (s)
		i = atoi(s + 1);
	i += atoi(str) * 10;
	switch (i) {
	case 670:
		return 1;
	case 719:
		return 3;
	case 744:
		return 4;
	case 770:
		return 5;
	case 797:
		return 6;
	case 825:
		return 7;
	case 854:
		return 8;
	case 885:
		return 9;
	case 915:
		return 10;
	case 948:
		return 11;
	case 974:
		return 12;
	case 1000:
		return 13;
	case 1035:
		return 14;
	case 1072:
		return 15;
	case 1109:
		return 16;
	case 1148:
		return 17;
	case 1188:
		return 18;
	case 1230:
		return 19;
	case 1273:
		return 20;
	case 1318:
		return 21;
	case 1365:
		return 22;
	case 1413:
		return 23;
	case 1462:
		return 24;
	case 1514:
		return 25;
	case 1567:
		return 26;
	case 1622:
		return 27;
	case 1679:
		return 28;
	case 1738:
		return 29;
	case 1799:
		return 30;
	case 1862:
		return 31;
	case 1928:
		return 32;
	case 2035:
		return 33;
	case 2107:
		return 34;
	case 2181:
		return 35;
	case 2257:
		return 36;
	case 2336:
		return 37;
	case 2418:
		return 38;
	case 2503:
		return 39;
	}
	return -1;
}

/* take a PL frequency and turn it into a code */
static int tm271_pltocode(char *str)
{
	int i;
	char *s;

	s = strchr(str, '.');
	i = 0;
	if (s)
		i = atoi(s + 1);
	i += atoi(str) * 10;
	switch (i) {
	case 670:
		return 0;
	case 693:
		return 1;
	case 719:
		return 2;
	case 744:
		return 3;
	case 770:
		return 4;
	case 797:
		return 5;
	case 825:
		return 6;
	case 854:
		return 7;
	case 885:
		return 8;
	case 915:
		return 9;
	case 948:
		return 10;
	case 974:
		return 11;
	case 1000:
		return 12;
	case 1035:
		return 13;
	case 1072:
		return 14;
	case 1109:
		return 15;
	case 1148:
		return 16;
	case 1188:
		return 17;
	case 1230:
		return 18;
	case 1273:
		return 19;
	case 1318:
		return 20;
	case 1365:
		return 21;
	case 1413:
		return 22;
	case 1462:
		return 23;
	case 1514:
		return 24;
	case 1567:
		return 25;
	case 1622:
		return 26;
	case 1679:
		return 27;
	case 1738:
		return 28;
	case 1799:
		return 29;
	case 1862:
		return 30;
	case 1928:
		return 31;
	case 2035:
		return 32;
	case 2065:
		return 33;
	case 2107:
		return 34;
	case 2181:
		return 35;
	case 2257:
		return 36;
	case 2291:
		return 37;
	case 2336:
		return 38;
	case 2418:
		return 39;
	case 2503:
		return 40;
	}
	return -1;
}

/* take a PL frequency and turn it into a code */
static int ft950_pltocode(char *str)
{
	int i;
	char *s;

	s = strchr(str, '.');
	i = 0;
	if (s)
		i = atoi(s + 1);
	i += atoi(str) * 10;
	switch (i) {
	case 670:
		return 0;
	case 693:
		return 1;
	case 719:
		return 2;
	case 744:
		return 3;
	case 770:
		return 4;
	case 797:
		return 5;
	case 825:
		return 6;
	case 854:
		return 7;
	case 885:
		return 8;
	case 915:
		return 9;
	case 948:
		return 10;
	case 974:
		return 11;
	case 1000:
		return 12;
	case 1035:
		return 13;
	case 1072:
		return 14;
	case 1109:
		return 15;
	case 1148:
		return 16;
	case 1188:
		return 17;
	case 1230:
		return 18;
	case 1273:
		return 19;
	case 1318:
		return 20;
	case 1365:
		return 21;
	case 1413:
		return 22;
	case 1462:
		return 23;
	case 1514:
		return 24;
	case 1567:
		return 25;
	case 1622:
		return 26;
	case 1679:
		return 27;
	case 1738:
		return 28;
	case 1799:
		return 29;
	case 1862:
		return 30;
	case 1928:
		return 31;
	case 2035:
		return 32;
	case 2065:
		return 33;
	case 2107:
		return 34;
	case 2181:
		return 35;
	case 2257:
		return 36;
	case 2291:
		return 37;
	case 2336:
		return 38;
	case 2418:
		return 39;
	case 2503:
		return 40;
	}
	return -1;
}

/* take a PL frequency and turn it into a code */
static int ft100_pltocode(char *str)
{
	int i;
	char *s;

	s = strchr(str, '.');
	i = 0;
	if (s)
		i = atoi(s + 1);
	i += atoi(str) * 10;
	switch (i) {
	case 670:
		return 0;
	case 693:
		return 1;
	case 719:
		return 2;
	case 744:
		return 3;
	case 770:
		return 4;
	case 797:
		return 5;
	case 825:
		return 6;
	case 854:
		return 7;
	case 885:
		return 8;
	case 915:
		return 9;
	case 948:
		return 10;
	case 974:
		return 11;
	case 1000:
		return 12;
	case 1035:
		return 13;
	case 1072:
		return 14;
	case 1109:
		return 15;
	case 1148:
		return 16;
	case 1188:
		return 17;
	case 1230:
		return 18;
	case 1273:
		return 19;
	case 1318:
		return 20;
	case 1365:
		return 21;
	case 1413:
		return 22;
	case 1462:
		return 23;
	case 1514:
		return 24;
	case 1567:
		return 25;
	case 1622:
		return 26;
	case 1679:
		return 27;
	case 1738:
		return 28;
	case 1799:
		return 29;
	case 1862:
		return 30;
	case 1928:
		return 31;
	case 2035:
		return 32;
	case 2107:
		return 33;
	case 2181:
		return 34;
	case 2257:
		return 35;
	case 2336:
		return 36;
	case 2418:
		return 37;
	case 2503:
		return 38;
	}
	return -1;
}

static int sendrxkenwood(struct rpt *myrpt, char *txstr, char *rxstr, char *cmpstr)
{
	int i, j;

	for (i = 0; i < KENWOOD_RETRIES; i++) {
		j = sendkenwood(myrpt, txstr, rxstr);
		if (j < 0)
			return (j);
		if (j == 0)
			continue;
		if (!strncmp(rxstr, cmpstr, strlen(cmpstr)))
			return (0);
	}
	return (-1);
}

int setkenwood(struct rpt *myrpt)
{
	char rxstr[RAD_SERIAL_BUFLEN], txstr[RAD_SERIAL_BUFLEN], freq[20];
	char mhz[MAXREMSTR], offset[20], band, decimals[MAXREMSTR], band1, band2;
	int myrxpl, mysplit, step;

	int offsets[] = { 0, 2, 1 };
	int powers[] = { 2, 1, 0 };

	if (sendrxkenwood(myrpt, "VMC 0,0\r", rxstr, "VMC") < 0)
		return -1;
	split_freq(mhz, decimals, myrpt->freq);
	mysplit = myrpt->splitkhz;
	if (atoi(mhz) > 400) {
		band = '6';
		band1 = '1';
		band2 = '5';
		if (!mysplit)
			mysplit = myrpt->p.default_split_70cm;
	} else {
		band = '2';
		band1 = '0';
		band2 = '2';
		if (!mysplit)
			mysplit = myrpt->p.default_split_2m;
	}
	sprintf(offset, "%06d000", mysplit);
	strcpy(freq, "000000");
	ast_copy_string(freq, decimals, strlen(freq) - 1);
	myrxpl = myrpt->rxplon;
	if (IS_XPMR(myrpt))
		myrxpl = 0;
	step = 0;
	if ((decimals[3] != '0') || (decimals[4] != '0'))
		step = 1;
	sprintf(txstr, "VW %c,%05d%s,%d,%d,0,%d,%d,,%02d,,%02d,%s\r",
			band, atoi(mhz), freq, step, offsets[(int) myrpt->offset],
			(myrpt->txplon != 0), myrxpl, kenwood_pltocode(myrpt->txpl), kenwood_pltocode(myrpt->rxpl), offset);
	if (sendrxkenwood(myrpt, txstr, rxstr, "VW") < 0)
		return -1;
	sprintf(txstr, "RBN %c\r", band2);
	if (sendrxkenwood(myrpt, txstr, rxstr, "RBN") < 0)
		return -1;
	sprintf(txstr, "PC %c,%d\r", band1, powers[(int) myrpt->powerlevel]);
	if (sendrxkenwood(myrpt, txstr, rxstr, "PC") < 0)
		return -1;
	return 0;
}

int set_tmd700(struct rpt *myrpt)
{
	char rxstr[RAD_SERIAL_BUFLEN], txstr[RAD_SERIAL_BUFLEN], freq[20];
	char mhz[MAXREMSTR], offset[20], decimals[MAXREMSTR];
	int myrxpl, mysplit, step;

	int offsets[] = { 0, 2, 1 };
	int powers[] = { 2, 1, 0 };
	int band;

	if (sendrxkenwood(myrpt, "BC 0,0\r", rxstr, "BC") < 0)
		return -1;
	split_freq(mhz, decimals, myrpt->freq);
	mysplit = myrpt->splitkhz;
	if (atoi(mhz) > 400) {
		band = 8;
		if (!mysplit)
			mysplit = myrpt->p.default_split_70cm;
	} else {
		band = 2;
		if (!mysplit)
			mysplit = myrpt->p.default_split_2m;
	}
	sprintf(offset, "%06d000", mysplit);
	strcpy(freq, "000000");
	ast_copy_string(freq, decimals, strlen(freq) - 1);
	step = 0;
	if ((decimals[3] != '0') || (decimals[4] != '0'))
		step = 1;
	myrxpl = myrpt->rxplon;
	if (IS_XPMR(myrpt))
		myrxpl = 0;
	sprintf(txstr, "VW %d,%05d%s,%d,%d,0,%d,%d,0,%02d,0010,%02d,%s,0\r",
			band, atoi(mhz), freq, step, offsets[(int) myrpt->offset],
			(myrpt->txplon != 0), myrxpl, kenwood_pltocode(myrpt->txpl), kenwood_pltocode(myrpt->rxpl), offset);
	if (sendrxkenwood(myrpt, txstr, rxstr, "VW") < 0)
		return -1;
	if (sendrxkenwood(myrpt, "VMC 0,0\r", rxstr, "VMC") < 0)
		return -1;
	sprintf(txstr, "RBN\r");
	if (sendrxkenwood(myrpt, txstr, rxstr, "RBN") < 0)
		return -1;
	sprintf(txstr, "RBN %d\r", band);
	if (strncmp(rxstr, txstr, 5)) {
		if (sendrxkenwood(myrpt, txstr, rxstr, "RBN") < 0)
			return -1;
	}
	sprintf(txstr, "PC 0,%d\r", powers[(int) myrpt->powerlevel]);
	if (sendrxkenwood(myrpt, txstr, rxstr, "PC") < 0)
		return -1;
	return 0;
}

int set_tm271(struct rpt *myrpt)
{
	char rxstr[RAD_SERIAL_BUFLEN], txstr[RAD_SERIAL_BUFLEN], freq[20];
	char mhz[MAXREMSTR], decimals[MAXREMSTR];
	int mysplit, step;

	int offsets[] = { 0, 2, 1 };
	int powers[] = { 2, 1, 0 };

	split_freq(mhz, decimals, myrpt->freq);
	strcpy(freq, "000000");
	ast_copy_string(freq, decimals, strlen(freq) - 1);

	if (!myrpt->splitkhz)
		mysplit = myrpt->p.default_split_2m;
	else
		mysplit = myrpt->splitkhz;

	step = 0;
	if ((decimals[3] != '0') || (decimals[4] != '0'))
		step = 1;
	sprintf(txstr, "VF %04d%s,%d,%d,0,%d,0,0,%02d,00,000,%05d000,0,0\r",
			atoi(mhz), freq, step, offsets[(int) myrpt->offset], (myrpt->txplon != 0), tm271_pltocode(myrpt->txpl),
			mysplit);

	if (sendrxkenwood(myrpt, "VM 0\r", rxstr, "VM") < 0)
		return -1;
	if (sendrxkenwood(myrpt, txstr, rxstr, "VF") < 0)
		return -1;
	sprintf(txstr, "PC %d\r", powers[(int) myrpt->powerlevel]);
	if (sendrxkenwood(myrpt, txstr, rxstr, "PC") < 0)
		return -1;
	return 0;
}

static int check_freq_kenwood(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if (m == 144) {				/* 2 meters */
		if (d < 10100)
			return -1;
	} else if ((m >= 145) && (m < 148)) {
		;
	} else if ((m >= 430) && (m < 450)) {	/* 70 centimeters */
		;
	} else
		return -1;

	if (defmode)
		*defmode = dflmd;

	return 0;
}

static int check_freq_tm271(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if (m == 144) {				/* 2 meters */
		if (d < 10100)
			return -1;
	} else if ((m >= 145) && (m < 148)) {
		;
	} else
		return -1;

	if (defmode)
		*defmode = dflmd;

	return 0;
}

/* Check for valid rbi frequency */
/* Hard coded limits now, configurable later, maybe? */

static int check_freq_rbi(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if (m == 50) {				/* 6 meters */
		if (d < 10100)
			return -1;
	} else if ((m >= 51) && (m < 54)) {
		;
	} else if (m == 144) {		/* 2 meters */
		if (d < 10100)
			return -1;
	} else if ((m >= 145) && (m < 148)) {
		;
	} else if ((m >= 222) && (m < 225)) {	/* 1.25 meters */
		;
	} else if ((m >= 430) && (m < 450)) {	/* 70 centimeters */
		;
	} else if ((m >= 1240) && (m < 1300)) {	/* 23 centimeters */
		;
	} else
		return -1;

	if (defmode)
		*defmode = dflmd;

	return 0;
}

/* Check for valid rtx frequency */
/* Hard coded limits now, configurable later, maybe? */

static int check_freq_rtx(int m, int d, int *defmode, struct rpt *myrpt)
{
	int dflmd = REM_MODE_FM;

	if (!strcmp(myrpt->remoterig, REMOTE_RIG_RTX150)) {

		if (m == 144) {			/* 2 meters */
			if (d < 10100)
				return -1;
		} else if ((m >= 145) && (m < 148)) {
			;
		} else
			return -1;
	} else {
		if ((m >= 430) && (m < 450)) {	/* 70 centimeters */
			;
		} else
			return -1;
	}
	if (defmode)
		*defmode = dflmd;

	return 0;
}

int split_ctcss_freq(char *hertz, char *decimal, char *freq)
{
	char freq_copy[MAXREMSTR];
	char *decp;

	decp = strchr(strncpy(freq_copy, freq, MAXREMSTR - 1), '.');
	if (decp) {
		*decp++ = 0;
		ast_copy_string(hertz, freq_copy, MAXREMSTR);
		ast_copy_string(decimal, decp, strlen(decp));
		decimal[strlen(decp)] = '\0';
		return 0;
	} else
		return -1;
}

/*
* FT-897 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */

static int check_freq_ft897(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if (m == 1) {				/* 160 meters */
		dflmd = REM_MODE_LSB;
		if (d < 80000)
			return -1;
	} else if (m == 3) {		/* 80 meters */
		dflmd = REM_MODE_LSB;
		if (d < 50000)
			return -1;
	} else if (m == 7) {		/* 40 meters */
		dflmd = REM_MODE_LSB;
		if (d > 30000)
			return -1;
	} else if (m == 14) {		/* 20 meters */
		dflmd = REM_MODE_USB;
		if (d > 35000)
			return -1;
	} else if (m == 18) {		/* 17 meters */
		dflmd = REM_MODE_USB;
		if ((d < 6800) || (d > 16800))
			return -1;
	} else if (m == 21) {		/* 15 meters */
		dflmd = REM_MODE_USB;
		if ((d < 20000) || (d > 45000))
			return -1;
	} else if (m == 24) {		/* 12 meters */
		dflmd = REM_MODE_USB;
		if ((d < 89000) || (d > 99000))
			return -1;
	} else if (m == 28) {		/* 10 meters */
		dflmd = REM_MODE_USB;
	} else if (m == 29) {
		if (d >= 51000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
		if (d > 70000)
			return -1;
	} else if (m == 50) {		/* 6 meters */
		if (d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;

	} else if ((m >= 51) && (m < 54)) {
		dflmd = REM_MODE_FM;
	} else if (m == 144) {		/* 2 meters */
		if (d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
	} else if ((m >= 145) && (m < 148)) {
		dflmd = REM_MODE_FM;
	} else if ((m >= 430) && (m < 450)) {	/* 70 centimeters */
		if (m < 438)
			dflmd = REM_MODE_USB;
		else
			dflmd = REM_MODE_FM;
		;
	} else
		return -1;

	if (defmode)
		*defmode = dflmd;

	return 0;
}

/*
* Set a new frequency for the FT897
*/

static int set_freq_ft897(struct rpt *myrpt, char *newfreq)
{
	unsigned char cmdstr[5];
	int m, d;
	char mhz[MAXREMSTR];
	char decimals[MAXREMSTR];

	ast_debug(1, "New frequency: %s\n", newfreq);

	if (split_freq(mhz, decimals, newfreq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	/* The FT-897 likes packed BCD frequencies */

	cmdstr[0] = ((m / 100) << 4) + ((m % 100) / 10);	/* 100MHz 10Mhz */
	cmdstr[1] = ((m % 10) << 4) + (d / 10000);	/* 1MHz 100KHz */
	cmdstr[2] = (((d % 10000) / 1000) << 4) + ((d % 1000) / 100);	/* 10KHz 1KHz */
	cmdstr[3] = (((d % 100) / 10) << 4) + (d % 10);	/* 100Hz 10Hz */
	cmdstr[4] = 0x01;			/* command */

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);

}

/* ft-897 simple commands */

int simple_command_ft897(struct rpt *myrpt, char command)
{
	unsigned char cmdstr[5];

	memset(cmdstr, 0, 5);

	cmdstr[4] = command;

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);

}

/* ft-897 offset */

static int set_offset_ft897(struct rpt *myrpt, char offset)
{
	unsigned char cmdstr[5];
	int mysplit, res;
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

	memset(cmdstr, 0, 5);

	ast_debug(7, "split=%i\n", mysplit * 1000);

	cmdstr[0] = (mysplit / 10000000) + ((mysplit % 10000000) / 1000000);
	cmdstr[1] = (((mysplit % 1000000) / 100000) << 4) + ((mysplit % 100000) / 10000);
	cmdstr[2] = (((mysplit % 10000) / 1000) << 4) + ((mysplit % 1000) / 100);
	cmdstr[3] = ((mysplit % 10) << 4) + ((mysplit % 100) / 10);
	cmdstr[4] = 0xf9;			/* command */
	res = serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
	if (res)
		return res;

	memset(cmdstr, 0, 5);

	switch (offset) {
	case REM_SIMPLEX:
		cmdstr[0] = 0x89;
		break;

	case REM_MINUS:
		cmdstr[0] = 0x09;
		break;

	case REM_PLUS:
		cmdstr[0] = 0x49;
		break;

	default:
		return -1;
	}

	cmdstr[4] = 0x09;

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}

/* ft-897 mode */

int set_mode_ft897(struct rpt *myrpt, char newmode)
{
	unsigned char cmdstr[5];

	memset(cmdstr, 0, 5);

	switch (newmode) {
	case REM_MODE_FM:
		cmdstr[0] = 0x08;
		break;

	case REM_MODE_USB:
		cmdstr[0] = 0x01;
		break;

	case REM_MODE_LSB:
		cmdstr[0] = 0x00;
		break;

	case REM_MODE_AM:
		cmdstr[0] = 0x04;
		break;

	default:
		return -1;
	}
	cmdstr[4] = 0x07;

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}

/* Set tone encode and decode modes */

static int set_ctcss_mode_ft897(struct rpt *myrpt, char txplon, char rxplon)
{
	unsigned char cmdstr[5];

	memset(cmdstr, 0, 5);

	if (rxplon && txplon)
		cmdstr[0] = 0x2A;		/* Encode and Decode */
	else if (!rxplon && txplon)
		cmdstr[0] = 0x4A;		/* Encode only */
	else if (rxplon && !txplon)
		cmdstr[0] = 0x3A;		/* Encode only */
	else
		cmdstr[0] = 0x8A;		/* OFF */

	cmdstr[4] = 0x0A;

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}

/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_ft897(struct rpt *myrpt, char *txtone, char *rxtone)
{
	unsigned char cmdstr[5];
	char hertz[MAXREMSTR], decimal[MAXREMSTR];
	int h, d;

	memset(cmdstr, 0, 5);

	if (split_ctcss_freq(hertz, decimal, txtone))
		return -1;

	h = atoi(hertz);
	d = atoi(decimal);

	cmdstr[0] = ((h / 100) << 4) + (h % 100) / 10;
	cmdstr[1] = ((h % 10) << 4) + (d % 10);

	if (rxtone) {

		if (split_ctcss_freq(hertz, decimal, rxtone))
			return -1;

		h = atoi(hertz);
		d = atoi(decimal);

		cmdstr[2] = ((h / 100) << 4) + (h % 100) / 10;
		cmdstr[3] = ((h % 10) << 4) + (d % 10);
	}
	cmdstr[4] = 0x0B;

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}

int set_ft897(struct rpt *myrpt)
{
	int res;

	ast_debug(3, "@@@@ lock on\n");
	res = simple_command_ft897(myrpt, 0x00);	/* LOCK on */

	ast_debug(3, "@@@@ ptt off\n");
	if (!res) {
		res = simple_command_ft897(myrpt, 0x88);	/* PTT off */
	}

	ast_debug(3, "Modulation mode\n");
	if (!res) {
		res = set_mode_ft897(myrpt, myrpt->remmode);	/* Modulation mode */
	}

	ast_debug(3, "Split off\n");
	if (!res) {
		simple_command_ft897(myrpt, 0x82);	/* Split off */
	}

	ast_debug(3, "Frequency\n");
	if (!res) {
		res = set_freq_ft897(myrpt, myrpt->freq);	/* Frequency */
		usleep(FT897_SERIAL_DELAY * 2);
	}
	if ((myrpt->remmode == REM_MODE_FM)) {
		ast_debug(3, "Offset\n");
		if (!res) {
			res = set_offset_ft897(myrpt, myrpt->offset);	/* Offset if FM */
			usleep(FT897_SERIAL_DELAY);
		}
		if ((!res) && (myrpt->rxplon || myrpt->txplon)) {
			usleep(FT897_SERIAL_DELAY);
			ast_debug(3, "CTCSS tone freqs.\n");
			res = set_ctcss_freq_ft897(myrpt, myrpt->txpl, myrpt->rxpl);	/* CTCSS freqs if CTCSS is enabled */
			usleep(FT897_SERIAL_DELAY);
		}
		if (!res) {
			ast_debug(3, "CTCSS mode\n");
			res = set_ctcss_mode_ft897(myrpt, myrpt->txplon, myrpt->rxplon);	/* CTCSS mode */
			usleep(FT897_SERIAL_DELAY);
		}
	}
	if ((myrpt->remmode == REM_MODE_USB) || (myrpt->remmode == REM_MODE_LSB)) {
		ast_debug(3, "Clarifier off\n");
		simple_command_ft897(myrpt, 0x85);	/* Clarifier off if LSB or USB */
	}
	return res;
}

static int closerem_ft897(struct rpt *myrpt)
{
	simple_command_ft897(myrpt, 0x88);	/* PTT off */
	return 0;
}

/*
* Bump frequency up or down by a small amount 
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz 
*/

static int multimode_bump_freq_ft897(struct rpt *myrpt, int interval)
{
	int m, d;
	char mhz[MAXREMSTR], decimals[MAXREMSTR];

	ast_debug(1, "Before bump: %s\n", myrpt->freq);

	if (split_freq(mhz, decimals, myrpt->freq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	d += (interval / 10);		/* 10Hz resolution */
	if (d < 0) {
		m--;
		d += 100000;
	} else if (d >= 100000) {
		m++;
		d -= 100000;
	}

	if (check_freq_ft897(m, d, NULL)) {
		ast_log(LOG_WARNING, "Bump freq invalid\n");
		return -1;
	}

	snprintf(myrpt->freq, MAXREMSTR, "%d.%05d", m, d);

	ast_debug(1, "After bump: %s\n", myrpt->freq);

	return set_freq_ft897(myrpt, myrpt->freq);
}

/*
* FT-100 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */

static int check_freq_ft100(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if (m == 1) {				/* 160 meters */
		dflmd = REM_MODE_LSB;
		if (d < 80000)
			return -1;
	} else if (m == 3) {		/* 80 meters */
		dflmd = REM_MODE_LSB;
		if (d < 50000)
			return -1;
	} else if (m == 7) {		/* 40 meters */
		dflmd = REM_MODE_LSB;
		if (d > 30000)
			return -1;
	} else if (m == 14) {		/* 20 meters */
		dflmd = REM_MODE_USB;
		if (d > 35000)
			return -1;
	} else if (m == 18) {		/* 17 meters */
		dflmd = REM_MODE_USB;
		if ((d < 6800) || (d > 16800))
			return -1;
	} else if (m == 21) {		/* 15 meters */
		dflmd = REM_MODE_USB;
		if ((d < 20000) || (d > 45000))
			return -1;
	} else if (m == 24) {		/* 12 meters */
		dflmd = REM_MODE_USB;
		if ((d < 89000) || (d > 99000))
			return -1;
	} else if (m == 28) {		/* 10 meters */
		dflmd = REM_MODE_USB;
	} else if (m == 29) {
		if (d >= 51000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
		if (d > 70000)
			return -1;
	} else if (m == 50) {		/* 6 meters */
		if (d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;

	} else if ((m >= 51) && (m < 54)) {
		dflmd = REM_MODE_FM;
	} else if (m == 144) {		/* 2 meters */
		if (d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
	} else if ((m >= 145) && (m < 148)) {
		dflmd = REM_MODE_FM;
	} else if ((m >= 430) && (m < 450)) {	/* 70 centimeters */
		if (m < 438)
			dflmd = REM_MODE_USB;
		else
			dflmd = REM_MODE_FM;
		;
	} else
		return -1;

	if (defmode)
		*defmode = dflmd;

	return 0;
}

/*
* Set a new frequency for the ft100
*/

static int set_freq_ft100(struct rpt *myrpt, char *newfreq)
{
	unsigned char cmdstr[5];
	int m, d;
	char mhz[MAXREMSTR];
	char decimals[MAXREMSTR];

	ast_debug(1, "New frequency: %s\n", newfreq);

	if (split_freq(mhz, decimals, newfreq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	/* The FT-100 likes packed BCD frequencies */

	cmdstr[0] = (((d % 100) / 10) << 4) + (d % 10);	/* 100Hz 10Hz */
	cmdstr[1] = (((d % 10000) / 1000) << 4) + ((d % 1000) / 100);	/* 10KHz 1KHz */
	cmdstr[2] = ((m % 10) << 4) + (d / 10000);	/* 1MHz 100KHz */
	cmdstr[3] = ((m / 100) << 4) + ((m % 100) / 10);	/* 100MHz 10Mhz */
	cmdstr[4] = 0x0a;			/* command */

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);

}

/* ft-897 simple commands */

int simple_command_ft100(struct rpt *myrpt, unsigned char command, unsigned char p1)
{
	unsigned char cmdstr[5];

	memset(cmdstr, 0, 5);
	cmdstr[3] = p1;
	cmdstr[4] = command;

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);

}

/* ft-897 offset */

static int set_offset_ft100(struct rpt *myrpt, char offset)
{
	unsigned char p1;

	switch (offset) {
	case REM_SIMPLEX:
		p1 = 0;
		break;

	case REM_MINUS:
		p1 = 1;
		break;

	case REM_PLUS:
		p1 = 2;
		break;

	default:
		return -1;
	}

	return simple_command_ft100(myrpt, 0x84, p1);
}

/* ft-897 mode */

int set_mode_ft100(struct rpt *myrpt, char newmode)
{
	unsigned char p1;

	switch (newmode) {
	case REM_MODE_FM:
		p1 = 6;
		break;

	case REM_MODE_USB:
		p1 = 1;
		break;

	case REM_MODE_LSB:
		p1 = 0;
		break;

	case REM_MODE_AM:
		p1 = 4;
		break;

	default:
		return -1;
	}
	return simple_command_ft100(myrpt, 0x0c, p1);
}

/* Set tone encode and decode modes */

static int set_ctcss_mode_ft100(struct rpt *myrpt, char txplon, char rxplon)
{
	unsigned char p1;

	if (rxplon)
		p1 = 2;					/* Encode and Decode */
	else if (!rxplon && txplon)
		p1 = 1;					/* Encode only */
	else
		p1 = 0;					/* OFF */

	return simple_command_ft100(myrpt, 0x92, p1);
}

/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_ft100(struct rpt *myrpt, char *txtone, char *rxtone)
{
	unsigned char p1;

	p1 = ft100_pltocode(rxtone);
	return simple_command_ft100(myrpt, 0x90, p1);
}

int set_ft100(struct rpt *myrpt)
{
	int res;

	ast_debug(3, "Modulation mode\n");
	res = set_mode_ft100(myrpt, myrpt->remmode);	/* Modulation mode */

	ast_debug(3, "Split off\n");
	if (!res) {
		simple_command_ft100(myrpt, 0x01, 0);	/* Split off */
	}

	ast_debug(3, "Frequency\n");
	if (!res) {
		res = set_freq_ft100(myrpt, myrpt->freq);	/* Frequency */
		usleep(FT100_SERIAL_DELAY * 2);
	}
	if ((myrpt->remmode == REM_MODE_FM)) {
		ast_debug(3, "Offset\n");
		if (!res) {
			res = set_offset_ft100(myrpt, myrpt->offset);	/* Offset if FM */
			usleep(FT100_SERIAL_DELAY);
		}
		if ((!res) && (myrpt->rxplon || myrpt->txplon)) {
			usleep(FT100_SERIAL_DELAY);
			ast_debug(3, "CTCSS tone freqs.\n");
			res = set_ctcss_freq_ft100(myrpt, myrpt->txpl, myrpt->rxpl);	/* CTCSS freqs if CTCSS is enabled */
			usleep(FT100_SERIAL_DELAY);
		}
		if (!res) {
			ast_debug(3, "CTCSS mode\n");
			res = set_ctcss_mode_ft100(myrpt, myrpt->txplon, myrpt->rxplon);	/* CTCSS mode */
			usleep(FT100_SERIAL_DELAY);
		}
	}
	return res;
}

static int closerem_ft100(struct rpt *myrpt)
{
	simple_command_ft100(myrpt, 0x0f, 0);	/* PTT off */
	return 0;
}

/*
* Bump frequency up or down by a small amount 
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz 
*/

static int multimode_bump_freq_ft100(struct rpt *myrpt, int interval)
{
	int m, d;
	char mhz[MAXREMSTR], decimals[MAXREMSTR];

	ast_debug(1, "Before bump: %s\n", myrpt->freq);

	if (split_freq(mhz, decimals, myrpt->freq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	d += (interval / 10);		/* 10Hz resolution */
	if (d < 0) {
		m--;
		d += 100000;
	} else if (d >= 100000) {
		m++;
		d -= 100000;
	}

	if (check_freq_ft100(m, d, NULL)) {
		ast_log(LOG_WARNING, "Bump freq invalid\n");
		return -1;
	}

	snprintf(myrpt->freq, MAXREMSTR, "%d.%05d", m, d);

	ast_debug(1, "After bump: %s\n", myrpt->freq);

	return set_freq_ft100(myrpt, myrpt->freq);
}

/*
* FT-950 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */

static int check_freq_ft950(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if (m == 1) {				/* 160 meters */
		dflmd = REM_MODE_LSB;
		if (d < 80000)
			return -1;
	} else if (m == 3) {		/* 80 meters */
		dflmd = REM_MODE_LSB;
		if (d < 50000)
			return -1;
	} else if (m == 7) {		/* 40 meters */
		dflmd = REM_MODE_LSB;
		if (d > 30000)
			return -1;
	} else if (m == 14) {		/* 20 meters */
		dflmd = REM_MODE_USB;
		if (d > 35000)
			return -1;
	} else if (m == 18) {		/* 17 meters */
		dflmd = REM_MODE_USB;
		if ((d < 6800) || (d > 16800))
			return -1;
	} else if (m == 21) {		/* 15 meters */
		dflmd = REM_MODE_USB;
		if ((d < 20000) || (d > 45000))
			return -1;
	} else if (m == 24) {		/* 12 meters */
		dflmd = REM_MODE_USB;
		if ((d < 89000) || (d > 99000))
			return -1;
	} else if (m == 28) {		/* 10 meters */
		dflmd = REM_MODE_USB;
	} else if (m == 29) {
		if (d >= 51000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
		if (d > 70000)
			return -1;
	} else if (m == 50) {		/* 6 meters */
		if (d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;

	} else if ((m >= 51) && (m < 54)) {
		dflmd = REM_MODE_FM;
	} else
		return -1;

	if (defmode)
		*defmode = dflmd;

	return 0;
}

/*
* Set a new frequency for the ft950
*/

static int set_freq_ft950(struct rpt *myrpt, char *newfreq)
{
	char cmdstr[20];
	int m, d;
	char mhz[MAXREMSTR];
	char decimals[MAXREMSTR];

	ast_debug(1, "New frequency: %s\n", newfreq);

	if (split_freq(mhz, decimals, newfreq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	sprintf(cmdstr, "FA%d%06d;", m, d * 10);
	return serial_remote_io(myrpt, (unsigned char *) cmdstr, strlen(cmdstr), NULL, 0, 0);

}

/* ft-950 offset */

static int set_offset_ft950(struct rpt *myrpt, char offset)
{
	char *cmdstr;

	switch (offset) {
	case REM_SIMPLEX:
		cmdstr = "OS00;";
		break;

	case REM_MINUS:
		cmdstr = "OS02;";
		break;

	case REM_PLUS:
		cmdstr = "OS01;";
		break;

	default:
		return -1;
	}

	return serial_remote_io(myrpt, (unsigned char *) cmdstr, strlen(cmdstr), NULL, 0, 0);
}

/* ft-950 mode */

static int set_mode_ft950(struct rpt *myrpt, char newmode)
{
	char *cmdstr;

	switch (newmode) {
	case REM_MODE_FM:
		cmdstr = "MD04;";
		break;

	case REM_MODE_USB:
		cmdstr = "MD02;";
		break;

	case REM_MODE_LSB:
		cmdstr = "MD01;";
		break;

	case REM_MODE_AM:
		cmdstr = "MD05;";
		break;

	default:
		return -1;
	}

	return serial_remote_io(myrpt, (unsigned char *) cmdstr, strlen(cmdstr), NULL, 0, 0);
}

/* Set tone encode and decode modes */

static int set_ctcss_mode_ft950(struct rpt *myrpt, char txplon, char rxplon)
{
	char *cmdstr;

	if (rxplon && txplon)
		cmdstr = "CT01;";
	else if (!rxplon && txplon)
		cmdstr = "CT02;";		/* Encode only */
	else if (rxplon && !txplon)
		cmdstr = "CT02;";		/* Encode only */
	else
		cmdstr = "CT00;";		/* OFF */

	return serial_remote_io(myrpt, (unsigned char *) cmdstr, strlen(cmdstr), NULL, 0, 0);
}

/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_ft950(struct rpt *myrpt, char *txtone, char *rxtone)
{
	char cmdstr[16];
	int c;

	c = ft950_pltocode(txtone);
	if (c < 0)
		return (-1);

	sprintf(cmdstr, "CN0%02d;", c);

	return serial_remote_io(myrpt, (unsigned char *) cmdstr, 5, NULL, 0, 0);
}

int set_ft950(struct rpt *myrpt)
{
	int res;
	char *cmdstr;

	ast_debug(2, "ptt off\n");

	cmdstr = "MX0;";
	res = serial_remote_io(myrpt, (unsigned char *) cmdstr, strlen(cmdstr), NULL, 0, 0);	/* MOX off */

	ast_debug(2, "select ant. 1\n");

	cmdstr = "AN01;";
	res = serial_remote_io(myrpt, (unsigned char *) cmdstr, strlen(cmdstr), NULL, 0, 0);	/* MOX off */

	ast_debug(2, "Modulation mode\n");

	if (!res)
		res = set_mode_ft950(myrpt, myrpt->remmode);	/* Modulation mode */

	ast_debug(2, "Split off\n");

	cmdstr = "OS00;";
	if (!res)
		res = serial_remote_io(myrpt, (unsigned char *) cmdstr, strlen(cmdstr), NULL, 0, 0);	/* Split off */

	ast_debug(2, "VFO Modes\n");

	if (!res)
		res = serial_remote_io(myrpt, (unsigned char *) "FR0;", 4, NULL, 0, 0);
	if (!res)
		res = serial_remote_io(myrpt, (unsigned char *) "FT2;", 4, NULL, 0, 0);

	ast_debug(2, "Frequency\n");

	if (!res)
		res = set_freq_ft950(myrpt, myrpt->freq);	/* Frequency */
	if ((myrpt->remmode == REM_MODE_FM)) {
		ast_debug(2, "Offset\n");
		if (!res)
			res = set_offset_ft950(myrpt, myrpt->offset);	/* Offset if FM */
		if ((!res) && (myrpt->rxplon || myrpt->txplon)) {
			ast_debug(2, "CTCSS tone freqs.\n");
			res = set_ctcss_freq_ft950(myrpt, myrpt->txpl, myrpt->rxpl);	/* CTCSS freqs if CTCSS is enabled */
		}
		if (!res) {
			ast_debug(2, "CTCSS mode\n");
			res = set_ctcss_mode_ft950(myrpt, myrpt->txplon, myrpt->rxplon);	/* CTCSS mode */
		}
	}
	if ((myrpt->remmode == REM_MODE_USB) || (myrpt->remmode == REM_MODE_LSB)) {
		ast_debug(2, "Clarifier off\n");
		cmdstr = "RT0;";
		serial_remote_io(myrpt, (unsigned char *) cmdstr, strlen(cmdstr), NULL, 0, 0);	/* Clarifier off if LSB or USB */
	}
	return res;
}

/*
* Bump frequency up or down by a small amount 
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz 
*/

static int multimode_bump_freq_ft950(struct rpt *myrpt, int interval)
{
	int m, d;
	char mhz[MAXREMSTR], decimals[MAXREMSTR];

	ast_debug(1, "Before bump: %s\n", myrpt->freq);

	if (split_freq(mhz, decimals, myrpt->freq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	d += (interval / 10);		/* 10Hz resolution */
	if (d < 0) {
		m--;
		d += 100000;
	} else if (d >= 100000) {
		m++;
		d -= 100000;
	}

	if (check_freq_ft950(m, d, NULL)) {
		ast_log(LOG_WARNING, "Bump freq invalid\n");
		return -1;
	}

	snprintf(myrpt->freq, MAXREMSTR, "%d.%05d", m, d);

	ast_debug(1, "After bump: %s\n", myrpt->freq);

	return set_freq_ft950(myrpt, myrpt->freq);
}

/*
* IC-706 I/O handlers
*/

/* Check to see that the frequency is valid */
/* returns 0 if frequency is valid          */

static int check_freq_ic706(int m, int d, int *defmode, char mars)
{
	int dflmd = REM_MODE_FM;
	int rv = 0;

	ast_debug(7, "(%i,%i,%i,%i)\n", m, d, *defmode, mars);

	/* first test for standard amateur radio bands */

	if (m == 1) {				/* 160 meters */
		dflmd = REM_MODE_LSB;
		if (d < 80000)
			rv = -1;
	} else if (m == 3) {		/* 80 meters */
		dflmd = REM_MODE_LSB;
		if (d < 50000)
			rv = -1;
	} else if (m == 7) {		/* 40 meters */
		dflmd = REM_MODE_LSB;
		if (d > 30000)
			rv = -1;
	} else if (m == 14) {		/* 20 meters */
		dflmd = REM_MODE_USB;
		if (d > 35000)
			rv = -1;
	} else if (m == 18) {		/* 17 meters */
		dflmd = REM_MODE_USB;
		if ((d < 6800) || (d > 16800))
			rv = -1;
	} else if (m == 21) {		/* 15 meters */
		dflmd = REM_MODE_USB;
		if ((d < 20000) || (d > 45000))
			rv = -1;
	} else if (m == 24) {		/* 12 meters */
		dflmd = REM_MODE_USB;
		if ((d < 89000) || (d > 99000))
			rv = -1;
	} else if (m == 28) {		/* 10 meters */
		dflmd = REM_MODE_USB;
	} else if (m == 29) {
		if (d >= 51000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
		if (d > 70000)
			rv = -1;
	} else if (m == 50) {		/* 6 meters */
		if (d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
	} else if ((m >= 51) && (m < 54)) {
		dflmd = REM_MODE_FM;
	} else if (m == 144) {		/* 2 meters */
		if (d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
	} else if ((m >= 145) && (m < 148)) {
		dflmd = REM_MODE_FM;
	} else if ((m >= 430) && (m < 450)) {	/* 70 centimeters */
		if (m < 438)
			dflmd = REM_MODE_USB;
		else
			dflmd = REM_MODE_FM;
	}

	/* check expanded coverage */
	if (mars && rv < 0) {
		if ((m >= 450) && (m < 470)) {	/* LMR */
			dflmd = REM_MODE_FM;
			rv = 0;
		} else if ((m >= 148) && (m < 174)) {	/* LMR */
			dflmd = REM_MODE_FM;
			rv = 0;
		} else if ((m >= 138) && (m < 144)) {	/* VHF-AM AIRCRAFT */
			dflmd = REM_MODE_AM;
			rv = 0;
		} else if ((m >= 108) && (m < 138)) {	/* VHF-AM AIRCRAFT */
			dflmd = REM_MODE_AM;
			rv = 0;
		} else if ((m == 0 && d >= 55000) || (m == 1 && d <= 75000)) {	/* AM BCB */
			dflmd = REM_MODE_AM;
			rv = 0;
		} else if ((m == 1 && d > 75000) || (m > 1 && m < 30)) {	/* HF SWL */
			dflmd = REM_MODE_AM;
			rv = 0;
		}
	}

	if (defmode)
		*defmode = dflmd;

	ast_debug(2, "(%i,%i,%i,%i) returning %i\n", m, d, *defmode, mars, rv);

	return rv;
}

/* take a PL frequency and turn it into a code */
static int ic706_pltocode(char *str)
{
	int i;
	char *s;
	int rv = -1;

	s = strchr(str, '.');
	i = 0;
	if (s)
		i = atoi(s + 1);
	i += atoi(str) * 10;
	switch (i) {
	case 670:
		rv = 0;
		break;
	case 693:
		rv = 1;
		break;
	case 719:
		rv = 2;
		break;
	case 744:
		rv = 3;
		break;
	case 770:
		rv = 4;
		break;
	case 797:
		rv = 5;
		break;
	case 825:
		rv = 6;
		break;
	case 854:
		rv = 7;
		break;
	case 885:
		rv = 8;
		break;
	case 915:
		rv = 9;
		break;
	case 948:
		rv = 10;
		break;
	case 974:
		rv = 11;
		break;
	case 1000:
		rv = 12;
		break;
	case 1035:
		rv = 13;
		break;
	case 1072:
		rv = 14;
		break;
	case 1109:
		rv = 15;
		break;
	case 1148:
		rv = 16;
		break;
	case 1188:
		rv = 17;
		break;
	case 1230:
		rv = 18;
		break;
	case 1273:
		rv = 19;
		break;
	case 1318:
		rv = 20;
		break;
	case 1365:
		rv = 21;
		break;
	case 1413:
		rv = 22;
		break;
	case 1462:
		rv = 23;
		break;
	case 1514:
		rv = 24;
		break;
	case 1567:
		rv = 25;
		break;
	case 1598:
		rv = 26;
		break;
	case 1622:
		rv = 27;
		break;
	case 1655:
		rv = 28;
		break;
	case 1679:
		rv = 29;
		break;
	case 1713:
		rv = 30;
		break;
	case 1738:
		rv = 31;
		break;
	case 1773:
		rv = 32;
		break;
	case 1799:
		rv = 33;
		break;
	case 1835:
		rv = 34;
		break;
	case 1862:
		rv = 35;
		break;
	case 1899:
		rv = 36;
		break;
	case 1928:
		rv = 37;
		break;
	case 1966:
		rv = 38;
		break;
	case 1995:
		rv = 39;
		break;
	case 2035:
		rv = 40;
		break;
	case 2065:
		rv = 41;
		break;
	case 2107:
		rv = 42;
		break;
	case 2181:
		rv = 43;
		break;
	case 2257:
		rv = 44;
		break;
	case 2291:
		rv = 45;
		break;
	case 2336:
		rv = 46;
		break;
	case 2418:
		rv = 47;
		break;
	case 2503:
		rv = 48;
		break;
	case 2541:
		rv = 49;
		break;
	}
	ast_debug(2, "%i  rv=%i\n", i, rv);

	return rv;
}

/* ic-706 simple commands */

static int simple_command_ic706(struct rpt *myrpt, char command, char subcommand)
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
* Set a new frequency for the ic706
*/

static int set_freq_ic706(struct rpt *myrpt, char *newfreq)
{
	unsigned char cmdstr[20];
	char mhz[MAXREMSTR], decimals[MAXREMSTR];
	int m, d;

	ast_debug(1, "newfreq:%s\n", newfreq);

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

/* ic-706 offset */

static int set_offset_ic706(struct rpt *myrpt, char offset)
{
	unsigned char c;
	int mysplit, res;
	char mhz[MAXREMSTR], decimal[MAXREMSTR];
	unsigned char cmdstr[10];

	if (split_freq(mhz, decimal, myrpt->freq))
		return -1;

	mysplit = myrpt->splitkhz * 10;
	if (!mysplit) {
		if (atoi(mhz) > 400)
			mysplit = myrpt->p.default_split_70cm * 10;
		else
			mysplit = myrpt->p.default_split_2m * 10;
	}

	ast_debug(7, "split=%i\n", mysplit * 100);

	/* The ic-706 likes packed BCD data */

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x0d;
	cmdstr[5] = ((mysplit % 10) << 4) + ((mysplit % 100) / 10);
	cmdstr[6] = (((mysplit % 10000) / 1000) << 4) + ((mysplit % 1000) / 100);
	cmdstr[7] = ((mysplit / 100000) << 4) + ((mysplit % 100000) / 10000);
	cmdstr[8] = 0xfd;

	res = civ_cmd(myrpt, cmdstr, 9);
	if (res)
		return res;

	ast_debug(7, "offset=%i\n", offset);

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

	return simple_command_ic706(myrpt, 0x0f, c);

}

/* ic-706 mode */

int set_mode_ic706(struct rpt *myrpt, char newmode)
{
	unsigned char c;

	ast_debug(7, "newmode=%i\n", newmode);

	switch (newmode) {
	case REM_MODE_FM:
		c = 5;
		break;

	case REM_MODE_USB:
		c = 1;
		break;

	case REM_MODE_LSB:
		c = 0;
		break;

	case REM_MODE_AM:
		c = 2;
		break;

	default:
		return -1;
	}
	return simple_command_ic706(myrpt, 6, c);
}

/* Set tone encode and decode modes */

static int set_ctcss_mode_ic706(struct rpt *myrpt, char txplon, char rxplon)
{
	unsigned char cmdstr[10];
	int rv;

	ast_debug(7, "txplon=%i  rxplon=%i \n", txplon, rxplon);

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x16;
	cmdstr[5] = 0x42;
	cmdstr[6] = (txplon != 0);
	cmdstr[7] = 0xfd;

	rv = civ_cmd(myrpt, cmdstr, 8);
	if (rv)
		return (-1);

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x16;
	cmdstr[5] = 0x43;
	cmdstr[6] = (rxplon != 0);
	cmdstr[7] = 0xfd;

	return (civ_cmd(myrpt, cmdstr, 8));
}

#if 0
/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_ic706(struct rpt *myrpt, char *txtone, char *rxtone)
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
#endif

static int vfo_ic706(struct rpt *myrpt)
{
	unsigned char cmdstr[10];

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 7;
	cmdstr[5] = 0xfd;

	return (civ_cmd(myrpt, cmdstr, 6));
}

static int mem2vfo_ic706(struct rpt *myrpt)
{
	unsigned char cmdstr[10];

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x0a;
	cmdstr[5] = 0xfd;

	return (civ_cmd(myrpt, cmdstr, 6));
}

static int select_mem_ic706(struct rpt *myrpt, int slot)
{
	unsigned char cmdstr[10];

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 8;
	cmdstr[5] = 0;
	cmdstr[6] = ((slot / 10) << 4) + (slot % 10);
	cmdstr[7] = 0xfd;

	return (civ_cmd(myrpt, cmdstr, 8));
}

int set_ic706(struct rpt *myrpt)
{
	int res = 0, i;

	ast_debug(7, "Set to VFO A iobase=%i\n", myrpt->p.iobase);

	if (!res)
		res = simple_command_ic706(myrpt, 7, 0);

	if ((myrpt->remmode == REM_MODE_FM)) {
		i = ic706_pltocode(myrpt->rxpl);
		if (i == -1)
			return -1;
		ast_debug(1, "Select memory number\n");
		if (!res)
			res = select_mem_ic706(myrpt, i + IC706_PL_MEMORY_OFFSET);
		ast_debug(1, "Transfer memory to VFO\n");
		if (!res)
			res = mem2vfo_ic706(myrpt);
	}

	ast_debug(2, "Set to VFO\n");

	if (!res)
		res = vfo_ic706(myrpt);

	ast_debug(2, "Modulation mode\n");

	if (!res)
		res = set_mode_ic706(myrpt, myrpt->remmode);	/* Modulation mode */

	ast_debug(2, "Split off\n");

	if (!res)
		simple_command_ic706(myrpt, 0x82, 0);	/* Split off */

	ast_debug(2, "Frequency\n");

	if (!res)
		res = set_freq_ic706(myrpt, myrpt->freq);	/* Frequency */
	if ((myrpt->remmode == REM_MODE_FM)) {
		ast_debug(2, "Offset\n");
		if (!res)
			res = set_offset_ic706(myrpt, myrpt->offset);	/* Offset if FM */
		if (!res) {
			ast_debug(2, "CTCSS mode\n");
			res = set_ctcss_mode_ic706(myrpt, myrpt->txplon, myrpt->rxplon);	/* CTCSS mode */
		}
	}
	return res;
}

/*
* Bump frequency up or down by a small amount 
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz 
*/

static int multimode_bump_freq_ic706(struct rpt *myrpt, int interval)
{
	int m, d;
	char mhz[MAXREMSTR], decimals[MAXREMSTR];
	unsigned char cmdstr[20];

	ast_debug(1, "Before bump: %s\n", myrpt->freq);

	if (split_freq(mhz, decimals, myrpt->freq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	d += (interval / 10);		/* 10Hz resolution */
	if (d < 0) {
		m--;
		d += 100000;
	} else if (d >= 100000) {
		m++;
		d -= 100000;
	}

	if (check_freq_ic706(m, d, NULL, myrpt->p.remote_mars)) {
		ast_log(LOG_WARNING, "Bump freq invalid\n");
		return -1;
	}

	snprintf(myrpt->freq, MAXREMSTR, "%d.%05d", m, d);

	ast_debug(1, "After bump: %s\n", myrpt->freq);

	/* The ic-706 likes packed BCD frequencies */

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0;
	cmdstr[5] = ((d % 10) << 4);
	cmdstr[6] = (((d % 1000) / 100) << 4) + ((d % 100) / 10);
	cmdstr[7] = ((d / 10000) << 4) + ((d % 10000) / 1000);
	cmdstr[8] = (((m % 100) / 10) << 4) + (m % 10);
	cmdstr[9] = (m / 100);
	cmdstr[10] = 0xfd;

	return (serial_remote_io(myrpt, cmdstr, 11, NULL, 0, 0));
}

int setrem(struct rpt *myrpt)
{
	char str[300];
	char *offsets[] = { "SIMPLEX", "MINUS", "PLUS" };
	char *powerlevels[] = { "LOW", "MEDIUM", "HIGH" };
	char *modes[] = { "FM", "USB", "LSB", "AM" };
	int i, res = -1;

#if	0
	printf("FREQ,%s,%s,%s,%s,%s,%s,%d,%d\n", myrpt->freq,
		   modes[(int) myrpt->remmode],
		   myrpt->txpl, myrpt->rxpl, offsets[(int) myrpt->offset], powerlevels[(int) myrpt->powerlevel], myrpt->txplon,
		   myrpt->rxplon);
#endif
	if (myrpt->p.archivedir) {
		sprintf(str, "FREQ,%s,%s,%s,%s,%s,%s,%d,%d", myrpt->freq,
				modes[(int) myrpt->remmode],
				myrpt->txpl, myrpt->rxpl, offsets[(int) myrpt->offset], powerlevels[(int) myrpt->powerlevel],
				myrpt->txplon, myrpt->rxplon);
		donodelog(myrpt, str);
	}
	if (myrpt->remote && myrpt->remote_webtransceiver) {
		if (myrpt->remmode == REM_MODE_FM) {
			char myfreq[MAXREMSTR], *cp;
			strcpy(myfreq, myrpt->freq);
			cp = strchr(myfreq, '.');
			for (i = strlen(myfreq) - 1; i >= 0; i--) {
				if (myfreq[i] != '0')
					break;
				myfreq[i] = 0;
			}
			if (myfreq[0] && (myfreq[strlen(myfreq) - 1] == '.'))
				strcat(myfreq, "0");
			sprintf(str, "J Remote Frequency\n%s FM\n%s Offset\n", (cp) ? myfreq : myrpt->freq,
					offsets[(int) myrpt->offset]);
			sprintf(str + strlen(str), "%s Power\nTX PL %s\nRX PL %s\n", powerlevels[(int) myrpt->powerlevel],
					(myrpt->txplon) ? myrpt->txpl : "Off", (myrpt->rxplon) ? myrpt->rxpl : "Off");
		} else {
			sprintf(str, "J Remote Frequency %s %s\n%s Power\n", myrpt->freq, modes[(int) myrpt->remmode],
					powerlevels[(int) myrpt->powerlevel]);
		}
		ast_sendtext(myrpt->remote_webtransceiver, str);
	}
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT897)) {
		rpt_telemetry(myrpt, SETREMOTE, NULL);
		res = 0;
	}
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT100)) {
		rpt_telemetry(myrpt, SETREMOTE, NULL);
		res = 0;
	}
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT950)) {
		rpt_telemetry(myrpt, SETREMOTE, NULL);
		res = 0;
	}
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_IC706)) {
		rpt_telemetry(myrpt, SETREMOTE, NULL);
		res = 0;
	}
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_XCAT)) {
		rpt_telemetry(myrpt, SETREMOTE, NULL);
		res = 0;
	}
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_TM271)) {
		rpt_telemetry(myrpt, SETREMOTE, NULL);
		res = 0;
	}
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_TMD700)) {
		rpt_telemetry(myrpt, SETREMOTE, NULL);
		res = 0;
	} else if (!strcmp(myrpt->remoterig, REMOTE_RIG_RBI)) {
		res = setrbi_check(myrpt);
		if (!res) {
			rpt_telemetry(myrpt, SETREMOTE, NULL);
			res = 0;
		}
	} else if (ISRIG_RTX(myrpt->remoterig)) {
		setrtx(myrpt);
		res = 0;
	} else if (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD)) {
		rpt_telemetry(myrpt, SETREMOTE, NULL);
		res = 0;
	} else
		res = 0;

	if (res < 0)
		ast_log(LOG_ERROR, "Unable to send remote command on node %s\n", myrpt->name);

	return res;
}

int closerem(struct rpt *myrpt)
{
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT897))
		return closerem_ft897(myrpt);
	else if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT100))
		return closerem_ft100(myrpt);
	else
		return 0;
}

int check_freq(struct rpt *myrpt, int m, int d, int *defmode)
{
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT897))
		return check_freq_ft897(m, d, defmode);
	else if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT100))
		return check_freq_ft100(m, d, defmode);
	else if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT950))
		return check_freq_ft950(m, d, defmode);
	else if (!strcmp(myrpt->remoterig, REMOTE_RIG_IC706))
		return check_freq_ic706(m, d, defmode, myrpt->p.remote_mars);
	else if (!strcmp(myrpt->remoterig, REMOTE_RIG_XCAT))
		return check_freq_xcat(m, d, defmode);
	else if (!strcmp(myrpt->remoterig, REMOTE_RIG_RBI))
		return check_freq_rbi(m, d, defmode);
	else if (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD))
		return check_freq_kenwood(m, d, defmode);
	else if (!strcmp(myrpt->remoterig, REMOTE_RIG_TMD700))
		return check_freq_kenwood(m, d, defmode);
	else if (!strcmp(myrpt->remoterig, REMOTE_RIG_TM271))
		return check_freq_tm271(m, d, defmode);
	else if (ISRIG_RTX(myrpt->remoterig))
		return check_freq_rtx(m, d, defmode, myrpt);
	else
		return -1;
}

char check_tx_freq(struct rpt *myrpt)
{
	int i, rv = 0;
	int radio_mhz, radio_decimals, ulimit_mhz, ulimit_decimals, llimit_mhz, llimit_decimals;
	char radio_mhz_char[MAXREMSTR];
	char radio_decimals_char[MAXREMSTR];
	char limit_mhz_char[MAXREMSTR];
	char limit_decimals_char[MAXREMSTR];
	char limits[256];
	char *limit_ranges[40];
	struct ast_variable *limitlist;

	ast_debug(4, "myrpt->freq = %s\n", myrpt->freq);

	/* Must have user logged in and tx_limits defined */

	if (!myrpt->p.txlimitsstanzaname || !myrpt->loginuser[0] || !myrpt->loginlevel[0]) {
		ast_debug(4, "No tx band table defined, or no user logged in. rv=1\n");
		rv = 1;
		return 1;				/* Assume it's ok otherwise */
	}

	/* Retrieve the band table for the loginlevel */
	limitlist = ast_variable_browse(myrpt->cfg, myrpt->p.txlimitsstanzaname);

	if (!limitlist) {
		ast_log(LOG_WARNING, "No entries in %s band table stanza. rv=0\n", myrpt->p.txlimitsstanzaname);
		rv = 0;
		return 0;
	}

	split_freq(radio_mhz_char, radio_decimals_char, myrpt->freq);
	radio_mhz = atoi(radio_mhz_char);
	radio_decimals = decimals2int(radio_decimals_char);

	ast_debug(4, "Login User = %s, login level = %s\n", myrpt->loginuser, myrpt->loginlevel);

	/* Find our entry */

	for (; limitlist; limitlist = limitlist->next) {
		if (!strcmp(limitlist->name, myrpt->loginlevel))
			break;
	}

	if (!limitlist) {
		ast_log(LOG_WARNING, "Can't find %s entry in band table stanza %s. rv=0\n", myrpt->loginlevel,
				myrpt->p.txlimitsstanzaname);
		rv = 0;
		return 0;
	}

	ast_debug(4, "Auth: %s = %s\n", limitlist->name, limitlist->value);

	/* Parse the limits */

	strncpy(limits, limitlist->value, 256);
	limits[255] = 0;
	finddelim(limits, limit_ranges, 40);
	for (i = 0; i < 40 && limit_ranges[i]; i++) {
		char range[40];
		char *r, *s;
		strncpy(range, limit_ranges[i], 40);
		range[39] = 0;
		ast_debug(4, "Check %s within %s\n", myrpt->freq, range);

		r = strchr(range, '-');
		if (!r) {
			ast_log(LOG_WARNING, "Malformed range in %s tx band table entry. rv=0\n", limitlist->name);
			rv = 0;
			break;
		}
		*r++ = 0;
		s = eatwhite(range);
		r = eatwhite(r);
		split_freq(limit_mhz_char, limit_decimals_char, s);
		llimit_mhz = atoi(limit_mhz_char);
		llimit_decimals = decimals2int(limit_decimals_char);
		split_freq(limit_mhz_char, limit_decimals_char, r);
		ulimit_mhz = atoi(limit_mhz_char);
		ulimit_decimals = decimals2int(limit_decimals_char);

		if ((radio_mhz >= llimit_mhz) && (radio_mhz <= ulimit_mhz)) {
			if (radio_mhz == llimit_mhz) {	/* CASE 1: TX freq is in llimit mhz portion of band */
				if (radio_decimals >= llimit_decimals) {	/* Cannot be below llimit decimals */
					if (llimit_mhz == ulimit_mhz) {	/* If bandwidth < 1Mhz, check ulimit decimals */
						if (radio_decimals <= ulimit_decimals) {
							rv = 1;
							break;
						} else {
							ast_debug(4, "Invalid TX frequency, debug msg 1\n");
							rv = 0;
							break;
						}
					} else {
						rv = 1;
						break;
					}
				} else {		/* Is below llimit decimals */
					ast_debug(4, "Invalid TX frequency, debug msg 2\n");
					rv = 0;
					break;
				}
			} else if (radio_mhz == ulimit_mhz) {	/* CASE 2: TX freq not in llimit mhz portion of band */
				if (radio_decimals <= ulimit_decimals) {
					ast_debug(4, "radio_decimals <= ulimit_decimals\n");
					rv = 1;
					break;
				} else {		/* Is above ulimit decimals */
					ast_debug(4, "Invalid TX frequency, debug msg 3\n");
					rv = 0;
					break;
				}
			} else /* CASE 3: TX freq within a multi-Mhz band and ok */
				ast_debug(4, "Valid TX freq within a multi-Mhz band and ok.\n");
			rv = 1;
			break;
		}
	}
	ast_debug(4, "rv=%i\n", rv);

	return rv;
}

int multimode_bump_freq(struct rpt *myrpt, int interval)
{
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT897))
		return multimode_bump_freq_ft897(myrpt, interval);
	else if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT950))
		return multimode_bump_freq_ft950(myrpt, interval);
	else if (!strcmp(myrpt->remoterig, REMOTE_RIG_IC706))
		return multimode_bump_freq_ic706(myrpt, interval);
	else if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT100))
		return multimode_bump_freq_ft100(myrpt, interval);
	else
		return -1;
}

void stop_scan(struct rpt *myrpt)
{
	myrpt->hfscanstop = 1;
	rpt_telemetry(myrpt, SCAN, 0);
}

int service_scan(struct rpt *myrpt)
{
	int res, interval;
	char mhz[MAXREMSTR], decimals[MAXREMSTR], k10 = 0i, k100 = 0;

	switch (myrpt->hfscanmode) {

	case HF_SCAN_DOWN_SLOW:
		interval = -10;			/* 100Hz /sec */
		break;

	case HF_SCAN_DOWN_QUICK:
		interval = -50;			/* 500Hz /sec */
		break;

	case HF_SCAN_DOWN_FAST:
		interval = -200;		/* 2KHz /sec */
		break;

	case HF_SCAN_UP_SLOW:
		interval = 10;			/* 100Hz /sec */
		break;

	case HF_SCAN_UP_QUICK:
		interval = 50;			/* 500 Hz/sec */
		break;

	case HF_SCAN_UP_FAST:
		interval = 200;			/* 2KHz /sec */
		break;

	default:
		myrpt->hfscanmode = 0;	/* Huh? */
		return -1;
	}

	res = split_freq(mhz, decimals, myrpt->freq);

	if (!res) {
		k100 = decimals[0];
		k10 = decimals[1];
		res = multimode_bump_freq(myrpt, interval);
	}

	if (!res)
		res = split_freq(mhz, decimals, myrpt->freq);

	if (res) {
		myrpt->hfscanmode = 0;
		myrpt->hfscanstatus = -2;
		return -1;
	}

	/* Announce 10KHz boundaries */
	if (k10 != decimals[1]) {
		int myhund = (interval < 0) ? k100 : decimals[0];
		int myten = (interval < 0) ? k10 : decimals[1];
		myrpt->hfscanstatus = (myten == '0') ? (myhund - '0') * 100 : (myten - '0') * 10;
	} else
		myrpt->hfscanstatus = 0;
	return res;

}

int channel_steer(struct rpt *myrpt, char *data)
{
	int res = 0;

	ast_debug(1, "remoterig=%s, data=%s\n", myrpt->remoterig, data);
	if (!myrpt->remoterig)
		return (0);
	if (data <= 0) {
		res = -1;
	} else {
		myrpt->nowchan = strtod(data, NULL);
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_PPP16)) {
			char string[16];
			sprintf(string, "SETCHAN %d ", myrpt->nowchan);
			send_usb_txt(myrpt, string);
		} else {
			if (get_mem_set(myrpt, data))
				res = -1;
		}
	}
	ast_debug(1, "nowchan=%i  res=%i\n", myrpt->nowchan, res);
	return res;
}

int channel_revert(struct rpt *myrpt)
{
	int res = 0;
	ast_debug(1, "remoterig=%s, nowchan=%02d, waschan=%02d\n", myrpt->remoterig, myrpt->nowchan, myrpt->waschan);
	if (!myrpt->remoterig)
		return (0);
	if (myrpt->nowchan != myrpt->waschan) {
		char data[8];
		ast_debug(1, "reverting.\n");
		sprintf(data, "%02d", myrpt->waschan);
		myrpt->nowchan = myrpt->waschan;
		channel_steer(myrpt, data);
		res = 1;
	}
	return (res);
}
