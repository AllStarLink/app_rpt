
/*! \file
 *
 * \brief Generic serial I/O routines
 */

#include "asterisk.h"

#include <termios.h>

#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"

#include "app_rpt.h"
#include "rpt_serial.h"
#include "rpt_channel.h" /* use send_usb_txt */
#include "rpt_xcat.h"
#include "rpt_telemetry.h"
#include "rpt_bridging.h"
#include "rpt_radio.h"

int serial_open(char *fname, int speed, int stop2)
{
	struct termios mode;
	int fd;

	fd = open(fname, O_RDWR);
	if (fd == -1) {
		ast_log(LOG_WARNING, "Cannot open serial port %s\n", fname);
		return -1;
	}

	memset(&mode, 0, sizeof(mode));
	if (tcgetattr(fd, &mode)) {
		ast_log(LOG_WARNING, "Unable to get serial parameters on %s: %s\n", fname, strerror(errno));
		return -1;
	}
#ifndef	SOLARIS
	cfmakeraw(&mode);
#else
	mode.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	mode.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	mode.c_cflag &= ~(CSIZE | PARENB | CRTSCTS);
	mode.c_cflag |= CS8;
	if (stop2)
		mode.c_cflag |= CSTOPB;
	mode.c_cc[VTIME] = 3;
	mode.c_cc[VMIN] = 1;
#endif

	cfsetispeed(&mode, speed);
	cfsetospeed(&mode, speed);
	if (tcsetattr(fd, TCSANOW, &mode)) {
		ast_log(LOG_WARNING, "Unable to set serial parameters on %s: %s\n", fname, strerror(errno));
		return -1;
	}
	usleep(100000);
	ast_debug(3, "Opened serial port %s\n", fname);
	return (fd);
}

int serial_rxready(int fd, int timeoutms)
{
	int myms = timeoutms;

	return (ast_waitfor_n_fd(&fd, 1, &myms, NULL));
}

int serial_rxflush(int fd, int timeoutms)
{
	int res, flushed = 0;
	char c;

	while ((res = serial_rxready(fd, timeoutms)) == 1) {
		if (read(fd, &c, 1) == -1) {
			res = -1;
			break;
			flushed++;
		}
	}
	return (res == -1) ? res : flushed;
}

int serial_rx(int fd, char *rxbuf, int rxmaxbytes, unsigned timeoutms, char termchr)
{
	char c;
	int i, j, res;

	if ((!rxmaxbytes) || (rxbuf == NULL)) {
		return 0;
	}
	memset(rxbuf, 0, rxmaxbytes);
	for (i = 0; i < rxmaxbytes; i++) {
		if (timeoutms) {
			res = serial_rxready(fd, timeoutms);
			if (res < 0)
				return -1;
			if (!res) {
				break;
			}
		}
		j = read(fd, &c, 1);
		if (j == -1) {
			ast_log(LOG_WARNING, "read failed: %s\n", strerror(errno));
			return -1;
		}
		if (j == 0)
			return i;
		rxbuf[i] = c;
		if (termchr) {
			rxbuf[i + 1] = 0;
			if (c == termchr)
				break;
		}
	}
	if (i && rpt_debug_level() >= 6) {
		ast_debug(6, "i = %d\n", i);
		ast_debug(6, "String returned was:\n");
		for (j = 0; j < i; j++)
			ast_debug(6, "%02X ", (unsigned char) rxbuf[j]);
		ast_debug(6, "\n");
	}
	return i;
}

int serial_txstring(int fd, char *txstring)
{
	int txbytes;

	txbytes = strlen(txstring);

	ast_debug(6, "sending: %s\n", txstring);

	if (write(fd, txstring, txbytes) != txbytes) {
		ast_log(LOG_WARNING, "write failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int serial_io(int fd, const char *txbuf, char *rxbuf, int txbytes, int rxmaxbytes, unsigned int timeoutms, char termchr)
{
	int i;

	ast_debug(7, "fd = %d\n", fd);

	if ((rxmaxbytes) && (rxbuf != NULL)) {
		if ((i = serial_rxflush(fd, 10)) == -1)
			return -1;
		ast_debug(7, "%d bytes flushed prior to write\n", i);
	}

	if (write(fd, txbuf, txbytes) != txbytes) {
		ast_log(LOG_WARNING, "write failed: %s\n", strerror(errno));
		return -1;
	}

	return serial_rx(fd, rxbuf, rxmaxbytes, timeoutms, termchr);
}

int setdtr(struct rpt *myrpt, int fd, int enable)
{
	struct termios mode;

	if (fd < 0)
		return -1;
	if (tcgetattr(fd, &mode)) {
		ast_log(LOG_WARNING, "Unable to get serial parameters for dtr: %s\n", strerror(errno));
		return -1;
	}
	if (enable) {
		cfsetspeed(&mode, myrpt->p.iospeed);
	} else {
		cfsetspeed(&mode, B0);
		usleep(100000);
	}
	if (tcsetattr(fd, TCSADRAIN, &mode)) {
		ast_log(LOG_WARNING, "Unable to set serial parameters for dtr: %s\n", strerror(errno));
		return -1;
	}
	if (enable)
		usleep(100000);
	return 0;
}

int openserial(struct rpt *myrpt, const char *fname)
{
	struct termios mode;
	int fd;

	fd = open(fname, O_RDWR);
	if (fd == -1) {
		ast_log(LOG_WARNING, "Cannot open serial port %s\n", fname);
		return -1;
	}
	memset(&mode, 0, sizeof(mode));
	if (tcgetattr(fd, &mode)) {
		ast_log(LOG_WARNING, "Unable to get serial parameters on %s: %s\n", fname, strerror(errno));
		return -1;
	}
#ifndef	SOLARIS
	cfmakeraw(&mode);
#else
	mode.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	mode.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	mode.c_cflag &= ~(CSIZE | PARENB | CRTSCTS);
	mode.c_cflag |= CS8;
	mode.c_cc[VTIME] = 3;
	mode.c_cc[VMIN] = 1;
#endif

	cfsetispeed(&mode, myrpt->p.iospeed);
	cfsetospeed(&mode, myrpt->p.iospeed);
	if (tcsetattr(fd, TCSANOW, &mode))
		ast_log(LOG_WARNING, "Unable to set serial parameters on %s: %s\n", fname, strerror(errno));
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD))
		setdtr(myrpt, fd, 0);
	usleep(100000);
	ast_debug(1, "Opened serial port %s\n", fname);
	return (fd);
}

/* Doug Hall RBI-1 serial data definitions:
 *
 * Byte 0: Expansion external outputs 
 * Byte 1: 
 *	Bits 0-3 are BAND as follows:
 *	Bits 4-5 are POWER bits as follows:
 *		00 - Low Power
 *		01 - Hi Power
 *		02 - Med Power
 *	Bits 6-7 are always set
 * Byte 2:
 *	Bits 0-3 MHZ in BCD format
 *	Bits 4-5 are offset as follows:
 *		00 - minus
 *		01 - plus
 *		02 - simplex
 *		03 - minus minus (whatever that is)
 *	Bit 6 is the 0/5 KHZ bit
 *	Bit 7 is always set
 * Byte 3:
 *	Bits 0-3 are 10 KHZ in BCD format
 *	Bits 4-7 are 100 KHZ in BCD format
 * Byte 4: PL Tone code and encode/decode enable bits
 *	Bits 0-5 are PL tone code (comspec binary codes)
 *	Bit 6 is encode enable/disable
 *	Bit 7 is decode enable/disable
 */

/* take the frequency from the 10 mhz digits (and up) and convert it
   to a band number */

static int rbi_mhztoband(char *str)
{
	int i;

	i = atoi(str) / 10;			/* get the 10's of mhz */
	switch (i) {
	case 2:
		return 10;
	case 5:
		return 11;
	case 14:
		return 2;
	case 22:
		return 3;
	case 44:
		return 4;
	case 124:
		return 0;
	case 125:
		return 1;
	case 126:
		return 8;
	case 127:
		return 5;
	case 128:
		return 6;
	case 129:
		return 7;
	default:
		break;
	}
	return -1;
}

/* take a PL frequency and turn it into a code */
static int rbi_pltocode(char *str)
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
	case 719:
		return 1;
	case 744:
		return 2;
	case 770:
		return 3;
	case 797:
		return 4;
	case 825:
		return 5;
	case 854:
		return 6;
	case 885:
		return 7;
	case 915:
		return 8;
	case 948:
		return 9;
	case 974:
		return 10;
	case 1000:
		return 11;
	case 1035:
		return 12;
	case 1072:
		return 13;
	case 1109:
		return 14;
	case 1148:
		return 15;
	case 1188:
		return 16;
	case 1230:
		return 17;
	case 1273:
		return 18;
	case 1318:
		return 19;
	case 1365:
		return 20;
	case 1413:
		return 21;
	case 1462:
		return 22;
	case 1514:
		return 23;
	case 1567:
		return 24;
	case 1622:
		return 25;
	case 1679:
		return 26;
	case 1738:
		return 27;
	case 1799:
		return 28;
	case 1862:
		return 29;
	case 1928:
		return 30;
	case 2035:
		return 31;
	case 2107:
		return 32;
	case 2181:
		return 33;
	case 2257:
		return 34;
	case 2336:
		return 35;
	case 2418:
		return 36;
	case 2503:
		return 37;
	}
	return -1;
}

/*
* Shift out a formatted serial bit stream
*/

static void rbi_out_parallel(struct rpt *myrpt, unsigned char *data)
{
#ifdef HAVE_SYS_IO
#ifdef __i386__
	int i, j;
	unsigned char od, d;
	static volatile long long delayvar;

	for (i = 0; i < 5; i++) {
		od = *data++;
		for (j = 0; j < 8; j++) {
			d = od & 1;
			outb(d, myrpt->p.iobase);
			/* >= 15 us */
			for (delayvar = 1; delayvar < 15000; delayvar++);
			od >>= 1;
			outb(d | 2, myrpt->p.iobase);
			/* >= 30 us */
			for (delayvar = 1; delayvar < 30000; delayvar++);
			outb(d, myrpt->p.iobase);
			/* >= 10 us */
			for (delayvar = 1; delayvar < 10000; delayvar++);
		}
	}
	/* >= 50 us */
	for (delayvar = 1; delayvar < 50000; delayvar++);
#endif /* __i386__ */
#endif /* HAVE_SYS_IO */
}

static void rbi_out(struct rpt *myrpt, unsigned char *data)
{
	if (rpt_radio_set_param(myrpt->localrxchannel, myrpt, RPT_RADPAR_REMMODE, RPT_RADPAR_REM_RBI1)) {
		/* if setparam ioctl fails, its probably not a pciradio card */
		rbi_out_parallel(myrpt, data);
		return;
	}
	rpt_radio_set_remcommand_data(myrpt->localrxchannel, myrpt, data, 5);
}

int serial_remote_io(struct rpt *myrpt, unsigned char *txbuf, int txbytes, unsigned char *rxbuf, int rxmaxbytes, int asciiflag)
{
	int i, j;
	char c;

#ifdef	FAKE_SERIAL_RESPONSE
	printf("String output was %s:\n", txbuf);
#endif
	if (rpt_debug_level()) {
		ast_debug(7, "ioport=%s baud=%d iofd=0x%x\n", myrpt->p.ioport, myrpt->p.iospeed, myrpt->iofd);
		ast_debug(7, "String output was %s:\n", txbuf);
		for (i = 0; i < txbytes; i++)
			ast_debug(7, "%02X ", (unsigned char) txbuf[i]);
		ast_debug(7, "\n");
	}

	if (myrpt->iofd >= 0) {		/* if to do out a serial port */
		serial_rxflush(myrpt->iofd, 20);
		if ((!strcmp(myrpt->remoterig, REMOTE_RIG_TM271)) || (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD))) {
			for (i = 0; i < txbytes; i++) {
				if (write(myrpt->iofd, &txbuf[i], 1) != 1)
					return -1;
				usleep(6666);
			}
		} else {
			if (write(myrpt->iofd, txbuf, txbytes) != txbytes) {
				return -1;
			}
		}
		if ((!rxmaxbytes) || (rxbuf == NULL)) {
			return (0);
		}
		memset(rxbuf, 0, rxmaxbytes);
		for (i = 0; i < rxmaxbytes; i++) {
			j = serial_rxready(myrpt->iofd, 1000);
			if (j < 1) {
#ifdef	FAKE_SERIAL_RESPONSE
				strcpy((char *) rxbuf, (char *) txbuf);
				return (strlen((char *) rxbuf));
#else
				ast_log(LOG_WARNING, "%d Serial device not responding on node %s\n", j, myrpt->name);
				return (j);
#endif
			}
			j = read(myrpt->iofd, &c, 1);
			if (j < 1) {
				return (i);
			}
			rxbuf[i] = c;
			if (asciiflag & 1) {
				rxbuf[i + 1] = 0;
				if (c == '\r')
					break;
			}
		}
		if (rpt_debug_level()) {
			ast_debug(3, "String returned was:\n");
			for (j = 0; j < i; j++)
				ast_debug(3, "%02X ", (unsigned char) rxbuf[j]);
			ast_debug(3, "\n");
		}
		return (i);
	}

	/* if not a DAHDI channel, can't use pciradio stuff */
	if (myrpt->rxchannel != myrpt->localrxchannel) {
		return -1;
	}

	return rpt_pciradio_serial_remote_io(myrpt, txbuf, txbytes, rxbuf, rxmaxbytes, asciiflag);
}

int setrbi(struct rpt *myrpt)
{
	char tmp[MAXREMSTR] = "", *s;
	unsigned char rbicmd[5];
	int band, txoffset = 0, txpower = 0, rxpl;

	/* must be a remote system */
	if (!myrpt->remoterig)
		return (0);
	if (!myrpt->remoterig[0])
		return (0);
	/* must have rbi hardware */
	if (strncmp(myrpt->remoterig, REMOTE_RIG_RBI, 3))
		return (0);
	if (setrbi_check(myrpt) == -1)
		return (-1);
	ast_copy_string(tmp, myrpt->freq, sizeof(tmp) - 1);
	s = strchr(tmp, '.');
	/* if no decimal, is invalid */

	if (s == NULL) {
		ast_debug(1, "@@@@ Frequency needs a decimal\n");
		return -1;
	}

	*s++ = 0;
	if (strlen(tmp) < 2) {
		ast_debug(1, "@@@@ Bad MHz digits: %s\n", tmp);
		return -1;
	}

	if (strlen(s) < 3) {
		ast_debug(1, "@@@@ Bad KHz digits: %s\n", s);
		return -1;
	}

	if ((s[2] != '0') && (s[2] != '5')) {
		ast_debug(1, "@@@@ KHz must end in 0 or 5: %c\n", s[2]);
		return -1;
	}

	band = rbi_mhztoband(tmp);
	if (band == -1) {
		ast_debug(1, "@@@@ Bad Band: %s\n", tmp);
		return -1;
	}

	rxpl = rbi_pltocode(myrpt->rxpl);

	if (rxpl == -1) {
		ast_debug(1, "@@@@ Bad TX PL: %s\n", myrpt->rxpl);
		return -1;
	}

	switch (myrpt->offset) {
	case REM_MINUS:
		txoffset = 0;
		break;
	case REM_PLUS:
		txoffset = 0x10;
		break;
	case REM_SIMPLEX:
		txoffset = 0x20;
		break;
	}
	switch (myrpt->powerlevel) {
	case REM_LOWPWR:
		txpower = 0;
		break;
	case REM_MEDPWR:
		txpower = 0x20;
		break;
	case REM_HIPWR:
		txpower = 0x10;
		break;
	}
	rbicmd[0] = 0;
	rbicmd[1] = band | txpower | 0xc0;
	rbicmd[2] = (*(s - 2) - '0') | txoffset | 0x80;
	if (s[2] == '5')
		rbicmd[2] |= 0x40;
	rbicmd[3] = ((*s - '0') << 4) + (s[1] - '0');
	rbicmd[4] = rxpl;
	if (myrpt->txplon)
		rbicmd[4] |= 0x40;
	if (myrpt->rxplon)
		rbicmd[4] |= 0x80;
	rbi_out(myrpt, rbicmd);
	return 0;
}

int setrtx(struct rpt *myrpt)
{
	char tmp[MAXREMSTR] = "", *s, rigstr[200], pwr, res = 0;
	int band, rxpl, txpl, mysplit;
	float ofac;
	double txfreq;

	/* must be a remote system */
	if (!myrpt->remoterig)
		return (0);
	if (!myrpt->remoterig[0])
		return (0);
	/* must have rtx hardware */
	if (!ISRIG_RTX(myrpt->remoterig))
		return (0);
	/* must be a usbradio interface type */
	if (!IS_XPMR(myrpt))
		return (0);
	ast_copy_string(tmp, myrpt->freq, sizeof(tmp) - 1);
	s = strchr(tmp, '.');
	/* if no decimal, is invalid */

	ast_debug(1, "setrtx() %s %s\n", myrpt->name, myrpt->remoterig);

	if (s == NULL) {
		ast_log(LOG_WARNING, "@@@@ Frequency needs a decimal\n");
		return -1;
	}
	*s++ = 0;
	if (strlen(tmp) < 2) {
		ast_log(LOG_WARNING, "@@@@ Bad MHz digits: %s\n", tmp);
		return -1;
	}

	if (strlen(s) < 3) {
		ast_log(LOG_WARNING, "@@@@ Bad KHz digits: %s\n", s);
		return -1;
	}

	if ((s[2] != '0') && (s[2] != '5')) {
		ast_log(LOG_WARNING, "@@@@ KHz must end in 0 or 5: %c\n", s[2]);
		return -1;
	}

	band = rbi_mhztoband(tmp);
	if (band == -1) {
		ast_log(LOG_WARNING, "@@@@ Bad Band: %s\n", tmp);
		return -1;
	}

	rxpl = rbi_pltocode(myrpt->rxpl);

	if (rxpl == -1) {
		ast_log(LOG_WARNING, "@@@@ Bad RX PL: %s\n", myrpt->rxpl);
		return -1;
	}

	txpl = rbi_pltocode(myrpt->txpl);

	if (txpl == -1) {
		ast_log(LOG_WARNING, "@@@@ Bad TX PL: %s\n", myrpt->txpl);
		return -1;
	}

	res = setrtx_check(myrpt);
	if (res < 0)
		return res;
	mysplit = myrpt->splitkhz;
	if (!mysplit) {
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_RTX450))
			mysplit = myrpt->p.default_split_70cm;
		else
			mysplit = myrpt->p.default_split_2m;
	}
	if (myrpt->offset != REM_SIMPLEX)
		ofac = ((float) mysplit) / 1000.0;
	else
		ofac = 0.0;
	if (myrpt->offset == REM_MINUS)
		ofac = -ofac;

	txfreq = atof(myrpt->freq) + ofac;
	pwr = 'L';
	if (myrpt->powerlevel == REM_HIPWR)
		pwr = 'H';
	if (!res) {
		sprintf(rigstr, "SETFREQ %s %f %s %s %c", myrpt->freq, txfreq,
				(myrpt->rxplon) ? myrpt->rxpl : "0.0", (myrpt->txplon) ? myrpt->txpl : "0.0", pwr);
		send_usb_txt(myrpt, rigstr);
		rpt_telemetry(myrpt, COMPLETE, NULL);
		res = 0;
	}
	return 0;
}

int setxpmr(struct rpt *myrpt, int dotx)
{
	char rigstr[200];
	int rxpl, txpl;

	/* must be a remote system */
	if (!myrpt->remoterig)
		return (0);
	if (!myrpt->remoterig[0])
		return (0);
	/* must not have rtx hardware */
	if (ISRIG_RTX(myrpt->remoterig))
		return (0);
	/* must be a usbradio interface type */
	if (!IS_XPMR(myrpt))
		return (0);

	ast_debug(1, "setxpmr() %s %s\n", myrpt->name, myrpt->remoterig);

	rxpl = rbi_pltocode(myrpt->rxpl);

	if (rxpl == -1) {
		ast_log(LOG_WARNING, "@@@@ Bad RX PL: %s\n", myrpt->rxpl);
		return -1;
	}

	if (dotx) {
		txpl = rbi_pltocode(myrpt->txpl);
		if (txpl == -1) {
			ast_log(LOG_WARNING, "@@@@ Bad TX PL: %s\n", myrpt->txpl);
			return -1;
		}
		sprintf(rigstr, "SETFREQ 0.0 0.0 %s %s L", (myrpt->rxplon) ? myrpt->rxpl : "0.0",
				(myrpt->txplon) ? myrpt->txpl : "0.0");
	} else {
		sprintf(rigstr, "SETFREQ 0.0 0.0 %s 0.0 L", (myrpt->rxplon) ? myrpt->rxpl : "0.0");

	}
	send_usb_txt(myrpt, rigstr);
	return 0;
}

int setrbi_check(struct rpt *myrpt)
{
	char tmp[MAXREMSTR] = "", *s;
	int band, txpl;

	/* must be a remote system */
	if (!myrpt->remote)
		return (0);
	/* must have rbi hardware */
	if (strncmp(myrpt->remoterig, REMOTE_RIG_RBI, 3))
		return (0);
	ast_copy_string(tmp, myrpt->freq, sizeof(tmp) - 1);
	s = strchr(tmp, '.');
	/* if no decimal, is invalid */

	if (s == NULL) {
		ast_log(LOG_WARNING, "@@@@ Frequency needs a decimal\n");
		return -1;
	}

	*s++ = 0;
	if (strlen(tmp) < 2) {
		ast_log(LOG_WARNING, "@@@@ Bad MHz digits: %s\n", tmp);
		return -1;
	}

	if (strlen(s) < 3) {
		ast_log(LOG_WARNING, "@@@@ Bad KHz digits: %s\n", s);
		return -1;
	}

	if ((s[2] != '0') && (s[2] != '5')) {
		ast_log(LOG_WARNING, "@@@@ KHz must end in 0 or 5: %c\n", s[2]);
		return -1;
	}

	band = rbi_mhztoband(tmp);
	if (band == -1) {
		ast_log(LOG_WARNING, "@@@@ Bad Band: %s\n", tmp);
		return -1;
	}

	txpl = rbi_pltocode(myrpt->txpl);

	if (txpl == -1) {
		ast_log(LOG_WARNING, "@@@@ Bad TX PL: %s\n", myrpt->txpl);
		return -1;
	}
	return 0;
}

int setrtx_check(struct rpt *myrpt)
{
	char tmp[MAXREMSTR] = "", *s;
	int band, txpl, rxpl;

	/* must be a remote system */
	if (!myrpt->remote)
		return (0);
	/* must have rbi hardware */
	if (strncmp(myrpt->remoterig, REMOTE_RIG_RBI, 3))
		return (0);
	ast_copy_string(tmp, myrpt->freq, sizeof(tmp) - 1);
	s = strchr(tmp, '.');
	/* if no decimal, is invalid */

	if (s == NULL) {
		ast_log(LOG_WARNING, "@@@@ Frequency needs a decimal\n");
		return -1;
	}

	*s++ = 0;
	if (strlen(tmp) < 2) {
		ast_log(LOG_WARNING, "@@@@ Bad MHz digits: %s\n", tmp);
		return -1;
	}

	if (strlen(s) < 3) {
		ast_log(LOG_WARNING, "@@@@ Bad KHz digits: %s\n", s);
		return -1;
	}

	if ((s[2] != '0') && (s[2] != '5')) {
		ast_log(LOG_WARNING, "@@@@ KHz must end in 0 or 5: %c\n", s[2]);
		return -1;
	}

	band = rbi_mhztoband(tmp);
	if (band == -1) {
		ast_log(LOG_WARNING, "@@@@ Bad Band: %s\n", tmp);
		return -1;
	}

	txpl = rbi_pltocode(myrpt->txpl);

	if (txpl == -1) {
		ast_log(LOG_WARNING, "@@@@ Bad TX PL: %s\n", myrpt->txpl);
		return -1;
	}

	rxpl = rbi_pltocode(myrpt->rxpl);

	if (rxpl == -1) {
		ast_log(LOG_WARNING, "@@@@ Bad RX PL: %s\n", myrpt->rxpl);
		return -1;
	}
	return 0;
}

int civ_cmd(struct rpt *myrpt, unsigned char *cmd, int cmdlen)
{
	unsigned char rxbuf[100];
	int i, rv;

	rv = serial_remote_io(myrpt, cmd, cmdlen, rxbuf, (myrpt->p.dusbabek) ? 6 : cmdlen + 6, 0);
	if (rv == -1)
		return (-1);
	if (myrpt->p.dusbabek) {
		if (rxbuf[0] != 0xfe)
			return (1);
		if (rxbuf[1] != 0xfe)
			return (1);
		if (rxbuf[4] != 0xfb)
			return (1);
		if (rxbuf[5] != 0xfd)
			return (1);
		return (0);
	}
	if (rv != (cmdlen + 6))
		return (1);
	for (i = 0; i < 6; i++)
		if (rxbuf[i] != cmd[i])
			return (1);
	if (rxbuf[cmdlen] != 0xfe)
		return (1);
	if (rxbuf[cmdlen + 1] != 0xfe)
		return (1);
	if (rxbuf[cmdlen + 4] != 0xfb)
		return (1);
	if (rxbuf[cmdlen + 5] != 0xfd)
		return (1);
	return (0);
}
