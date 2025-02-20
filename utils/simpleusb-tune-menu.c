/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Jim Dixon, WB6NIL
 *
 * Jim Dixon <jim@lambdatel.com>
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

/*
 *
 * Simplesusb tune menu program
 *
 * This program communicates with Asterisk by sending commands to retrieve and set values
 * for the simpleusb channel driver.
 *
 * The following 'menu-support' commands are used:
 *
 * susb tune menusupport X - where X is one of the following:
 *		0 - get current settings
 *		1 - get node names that are configured in simpleusb.conf
 *		2 - print parameters
 *		3 - get node names that are configured in simpleusb.conf, except current device
 *		b - receiver tune display
 *		c - receive level
 *		f - txa level
 *		g - txb level
 *		j - save current settings for the selected node
 *		k - change echo mode
 *		l - generate test tone
 *		m - change rxboost
 *		n - change pre-emphasis
 *		o - change de-emphasis
 *		p - change plfilter
 *		q - change ptt keying mode 
 *		r - change carrierfrom setting
 *		s - change ctcss from setting
 *		t - change rx on delay
 *		u - change tx off delay
 *		v - view cos, ctcss and ptt status
 *		y - receive audio statistics display
 *
 * Most of these commands take optional parameters to set values.
 *
 */
#include "asterisk.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

#include "asterisk/utils.h"

/*! \brief type of signal detection used for carrier (cos) or ctcss */
static const char * const signal_type[] = {"no", "usb", "usbinvert", "pp", "ppinvert"};

/*! \brief command prefix for Asterisk - simpleusb channel driver access */
#define COMMAND_PREFIX "susb "

/*!
 * \brief Signal handler
 * \param sig		Signal to watch.
 */
static void ourhandler(int sig)
{
	int i;

	signal(sig, ourhandler);
	while (waitpid(-1, &i, WNOHANG) > 0);

}

/*!
 * \brief Compare for qsort - sorts strings.
 * \param a			Pointer to first string.
 * \param b			Pointer to second string.
 * \retval 0		If equal.
 * \retval -1		If less than.
 * \retval 1		If greater than.
 */
  static int qcompar(const void *a, const void *b)
{
	char **sa = (char **) a, **sb = (char **) b;
	return (strcmp(*sa, *sb));
}

/*!
 * \brief Break up a delimited string into a table of substrings
 *
 * \note This modifies the string str, be sure to save an intact copy if you need it later.
 *
 * \param str		Pointer to string to process (it will be modified).
 * \param strp		Pointer to a list of substrings created, NULL will be placed at the end of the list.
 * \param limit		Maximum number of substrings to process.
 * \param delim		Specified delimiter
 * \param quote		User specified quote for escaping a substring. Set to zero to escape nothing.
 *
 * \retval 			Returns number of substrings found.
 */
static int explode_string(char *str, char *strp[], size_t limit, char delim, char quote)
{
	int i, inquo;

	inquo = 0;
	i = 0;
	strp[i++] = str;

	if (!*str) {
		strp[0] = 0;
		return (0);
	}
	for (; *str && (i < (limit - 1)); str++) {
		if (quote) {
			if (*str == quote) {
				if (inquo) {
					*str = 0;
					inquo = 0;
				}
				else {
					strp[i - 1] = str + 1;
					inquo = 1;
				}
			}
		}
		if ((*str == delim) && (!inquo)) {
			*str = 0;
			strp[i++] = str + 1;
		}
	}
	strp[i] = 0;
	return (i);
 }

/*!
 * \brief Execute an asterisk command.
 *
 * Opens a pipe and executes 'asterisk -rx cmd'
 *
 * \param cmd		Pointer to command to execute.
 * \returns			Pipe FD or -1 on failure.
 */
static int doastcmd(char *cmd)
{
	int pfd[2], pid, nullfd;

	 if (pipe(pfd) == -1) {
		perror("Error: cannot open pipe");
		return -1;
	}
	if (fcntl(pfd[0], F_SETFL, O_NONBLOCK) == -1) {
		perror("Error: cannot set pipe to NONBLOCK");
		return -1;
	}

	nullfd = open("/dev/null", O_RDWR);
	if (nullfd == -1) {
		perror("Error: cannot open /dev/null");
		return -1;
	}

	pid = fork();
	if (pid == -1) {
		perror("Error: cannot fork");
		return -1;
	}
	if (pid) {		/* if this is us (the parent) */
		close(pfd[1]);
		return (pfd[0]);
	}
	close(pfd[0]);

	if (dup2(nullfd, fileno(stdin)) == -1) {
		perror("Error: cannot dup2() stdin");
		exit(0);
	}
	if (dup2(pfd[1], fileno(stdout)) == -1) {
		perror("Error: cannot dup2() stdout");
		exit(0);
	}
	if (dup2(pfd[1], fileno(stderr)) == -1) {
		perror("Error: cannot dup2() stderr");
		exit(0);
	}
	/* Execute the asterisk command */
	execl("/usr/sbin/asterisk", "asterisk", "-rx", cmd, NULL);

	exit(0);
}

/*!
 * \brief Wait on one or two fd's.
 *
 * Check to see if fd1 or fd2, if specified, is ready to read.
 * returns -1 if error, 0 if nothing ready, or ready fd + 1
 *
 * awkward, but needed to support having an fd of 0, which
 * is likely, since that's most likely stdin
 *
 * specify fd2 as -1 if not used
 *
 * \param fd1		First fd to poll.
 * \param fd2		Second fd to poll.  Specify -1 if not used.
 * \param ms		Milliseconds to wait.
 * \returns			-1 on error, 0 if nothing ready, or fd+1.
 */
static int waitfds(int fd1, int fd2, int ms)
{
	fd_set fds;
	struct timeval tv;
	int i, r;

	FD_ZERO(&fds);
	FD_SET(fd1, &fds);

	if (fd2 >= 0) {
		FD_SET(fd2, &fds);
	}
	tv.tv_usec = ms * 1000;
	tv.tv_sec = 0;

	i = fd1;
	if (fd2 > fd1) {
		i = fd2;
	}

	r = select(i + 1, &fds, NULL, NULL, &tv);
	if (r < 1) {
		return (r);
	}
	if (FD_ISSET(fd1, &fds)) {
		return (fd1 + 1);
	}
	if ((fd2 > 0) && (FD_ISSET(fd2, &fds))) {
		return (fd2 + 1);
	}
	return (0);
}

/*!
 * \brief Wait for a character.
 *
 * \param fd		fd to read.
 * \returns			-1 nothing read, or character read.
 */
static int getcharfd(int fd)
{
	char c;

	if (read(fd, &c, 1) != 1) {
		return -1;
	}
	return (c);
}

/*!
 * \brief Wait for string of characters.
 *
 * \param fd		fd to read.
 * \param str		Pointer to string buffer.
 * \param max		Maximum number of characters to read.
 * \returns			Number of characters read.
 */
static int getstrfd(int fd, char *str, int max)
{
	int i, j;
	char c;

	i = 0;
	for (i = 0; (i < max) || (!max); i++) {
		do {
			j = waitfds(fd, -1, 100);
			if (j == -1) {
				if (errno != EINTR) {
					return 0;
				}
				j = 0;
			}
		} while (!j);

		j = read(fd, &c, 1);
		if (j == 0) {
			break;
		}
		if (j == -1) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		if (c == '\n') {
			break;
		}
		if (str) {
			str[i] = c;
		}
	}
	if (str) {
		str[i] = 0;
	}
	return (i);
}

/* get 1 line of data from Asterisk */
/*!
 * \brief Get one line of data from Asterisk.
 *
 *	Send a command to asterisk and get the response.
 *
 * \param cmd		Pointer to command to send to asterisk.
 * \param str		Pointer to string buffer.
 * \param max		Size of string buffer.
 * \returns			-1 on error, 0 if successful, 1 if nothing was returned.
 */
static int astgetline(char *cmd, char *str, int max)
{
	int fd, rv;

	/* Send the command to Asterisk */
	fd = doastcmd(cmd);
	if (fd == -1) {
		perror("Error getting data from Asterisk");
		return -1;
	}

	rv = getstrfd(fd, str, max);
	close(fd);

	return rv > 0 ? 0 : 1;
}

/*!
 * \brief Get a response from Asterisk and send to stdout.
 *
 *	Send a command to asterisk and output the response.
 *
 * \param cmd		Pointer to command to send to asterisk.
 * \returns			-1 on error, 0 if successful.
 */
static int astgetresp(char *cmd)
{
	int i, w, fd;
	char str[256];

	/* Send the command to Asterisk */
	fd = doastcmd(cmd);
	if (fd == -1) {
		perror("Error getting response from Asterisk");
		return -1;
	}

	/* Wait and process the response */
	for (;;) {
		w = waitfds(fileno(stdin), fd, 100);
		if (w == -1) {
			if (errno == EINTR) {
				continue;
			}
			perror("Error processing response from Asterisk");
			close(fd);
			return -1;
		}
		if (!w) {
			continue;
		}

		/* if it's our console */
		if (w == (fileno(stdin) + 1)) {
			getstrfd(fileno(stdin), str, sizeof(str) - 1);
			break;
		}

		/* if it's Asterisk */
		if (w == (fd + 1)) {
			i = getcharfd(fd);
			if (i == -1) {
				break;
			}
			putchar(i);
			fflush(stdout);
			continue;
		}
	}
	close(fd);
	return 0;
}

/*!
 * \brief Menu option to select the usb device.
 */
 static void menu_selectusb(void)
 {
	int i, n, x;
	char str[100], buf[256], *strs[100];

	printf("\n");

	/* print selected USB device */
	if (astgetresp(COMMAND_PREFIX "active")) {
		return;
	}

	/* get device list from Asterisk */
	if (astgetline(COMMAND_PREFIX "tune menu-support 1", buf, sizeof(buf) - 1)) {
		exit(255);
	}
	n = explode_string(buf, strs, ARRAY_LEN(strs), ',', 0);
	if (n < 1) {
		fprintf(stderr, "Error parsing USB device information\n");
		return;
	}
	qsort(strs, n, sizeof(char *), qcompar);

	printf("Please select from the following USB devices:\n");
	for (x = 0; x < n; x++) {
		printf("%d) Device [%s]\n", x + 1, strs[x]);
	}

	printf("0) Exit Selection\n");
	printf("Enter make your selection now: ");
	if (fgets(str, sizeof(str) - 1, stdin) == NULL) {
		printf("USB device not changed\n");
		return;
	}
	if (str[0] && (str[strlen(str) - 1] == '\n')) {
		str[strlen(str) - 1] = 0;
	}
	for (x = 0; str[x]; x++) {
		if (!isdigit(str[x])) {
			break;
		}
	}
	if (str[x] || (sscanf(str, "%d", &i) < 1) || (i < 0) || (i > n)) {
		printf("Entry Error, USB device not changed\n");
		return;
	}
	if (i < 1) {
		printf("USB device not changed\n");
		return;
	}
	snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "active %s", strs[i - 1]);
	astgetresp(str);

}

/*!
 * \brief Menu option to swap the usb device.
 */
 static void menu_swapusb(void)
 {
	int i, n, x;
	char str[100], buf[256], *strs[100];

	printf("\n");

	/* print selected USB device */
	if (astgetresp(COMMAND_PREFIX "active")) {
		return;
	}

	/* get device list from Asterisk */
	if (astgetline(COMMAND_PREFIX "tune menu-support 3", buf, sizeof(buf) - 1)) {
		exit(255);
	}
	n = explode_string(buf, strs, ARRAY_LEN(strs), ',', 0);
	if ((n < 1) || (!*strs[0])) {
		fprintf(stderr, "No additional USB devices found\n");
		return;
	}
	qsort(strs, n, sizeof(char *), qcompar);

	printf("Please select from the following USB devices:\n");
	for (x = 0; x < n; x++) {
		printf("%d) Device [%s]\n", x + 1, strs[x]);
	}
	printf("0) Exit Selection\n");
	printf("Enter make your selection now: ");
	if (fgets(str, sizeof(str) - 1, stdin) == NULL) {
		printf("USB device not changed\n");
		return;
	}
	if (str[0] && (str[strlen(str) - 1] == '\n')) {
		str[strlen(str) - 1] = 0;
	}
	for (x = 0; str[x]; x++) {
		if (!isdigit(str[x])) {
			break;
		}
	}
	if (str[x] || (sscanf(str, "%d", &i) < 1) || (i < 0) || (i > n)) {
		printf("Entry Error, USB device not swapped\n");
		return;
	}
	if (i < 1) {
		printf("USB device not swapped\n");
		return;
	}
	snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune swap %s", strs[i - 1]);
	astgetresp(str);

}

/*!
 * \brief Menu option to set rxvoice level.
 */
 static void menu_rxvoice(void)
 {
	int i, x;
	char str[100];

	for (;;) {
		if (astgetresp(COMMAND_PREFIX "tune menu-support b")) {
			break;
		}
		if (astgetresp(COMMAND_PREFIX "tune menu-support c")) {
			break;
		}

		printf("Enter new value (0-999, or CR for none): ");
		if (fgets(str, sizeof(str) - 1, stdin) == NULL) {
			printf("Rx voice setting not changed\n");
			return;
		}
		if (str[0] && (str[strlen(str) - 1] == '\n')) {
			str[strlen(str) - 1] = 0;
		}
		if (!str[0]) {
			printf("Rx voice setting not changed\n");
			return;
		}
		for (x = 0; str[x]; x++) {
			if (!isdigit(str[x])) {
				break;
			}
		}
		if (str[x] || (sscanf(str, "%d", &i) < 1) || (i < 0) || (i > 999)) {
			printf("Entry Error, Rx voice setting not changed\n");
			continue;
		}
		snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support c%d", i);
		if (astgetresp(str)) {
			break;
		}
	}

}


/*!
 * \brief Menu option to set txa level.
 * \param keying	Boolean to indicate if we are currently keying.
 */
 static void menu_txa(int keying)
 {
	char str[100];
	int i, x;

	if (astgetresp(COMMAND_PREFIX "tune menu-support f")) {
		return;
	}

	printf("Enter new Tx A Level setting (0-999, or C/R for none): ");
	if (fgets(str, sizeof(str) - 1, stdin) == NULL) {
		printf("Tx A Level setting not changed\n");
		if (keying) {
			astgetresp(COMMAND_PREFIX "tune menu-support fK");
		}
		return;
	}
	if (str[0] && (str[strlen(str) - 1] == '\n')) {
		str[strlen(str) - 1] = 0;
	}
	if (!str[0]) {
		printf("Tx A Level setting not changed\n");
		if (keying) {
			astgetresp(COMMAND_PREFIX "tune menu-support fK");
		}
		return;
	}
	for (x = 0; str[x]; x++) {
		if (!isdigit(str[x])) {
			break;
		}
	}
	if (str[x] || (sscanf(str, "%d", &i) < 1) || (i < 0) || (i > 999)) {
		printf("Entry Error, Tx A Level setting not changed\n");
		return;
	}
	if (keying) {
		snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support fK%d", i);
	} else {
		snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support f%d", i);
	}
	astgetresp(str);

}

/*!
 * \brief Menu option to set txb level.
 * \param keying	Boolean to indicate if we are currently keying.
*/
 static void menu_txb(int keying)
{
	char str[100];
	int i, x;

	if (astgetresp(COMMAND_PREFIX "tune menu-support g")) {
		return;
	}

	printf("Enter new Tx B Level setting (0-999, or C/R for none): ");
	if (fgets(str, sizeof(str) - 1, stdin) == NULL) {
		printf("Tx B Level setting not changed\n");
		if (keying) {
			astgetresp(COMMAND_PREFIX "tune menu-support gK");
		}
		return;
	}
	if (str[0] && (str[strlen(str) - 1] == '\n')) {
		str[strlen(str) - 1] = 0;
	}
	if (!str[0]) {
		printf("Tx B Level setting not changed\n");
		if (keying) {
			astgetresp(COMMAND_PREFIX "tune menu-support gK");
		}
		return;
	}
	for (x = 0; str[x]; x++) {
		if (!isdigit(str[x])) {
			break;
		}
	}
	if (str[x] || (sscanf(str, "%d", &i) < 1) || (i < 0) || (i > 999)) {
		printf("Entry Error, Tx B Level setting not changed\n");
		return;
	}
	if (keying) {
		snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support gK%d", i);
	} else {
		snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support g%d", i);
	}
	astgetresp(str);

}

/*!
 * \brief Menu option to select signal type.
 * \param signal		Pointer to signal description.
 * \param selection		Current signal selection.
 */
 static int menu_signal_type(const char *signal, int selection)
{
	char str[100];
	
	printf("\nPlease select from the following methods for %s:\n", signal);
	printf("1) no %s\n", selection == 0 ? "- Current" : "");
	printf("2) usb %s\n", selection == 1 ? "- Current" : "");
	printf("3) usbinvert %s\n", selection == 2 ? "- Current" : "");
	printf("4) pp %s\n", selection == 3 ? "- Current" : "");
	printf("5) ppinvert %s\n", selection == 4 ? "- Current" : "");

	printf("Select new %s or C/R for current): ", signal);
	if (fgets(str, sizeof(str) - 1, stdin) == NULL) {
		printf("Method not changed\n");
		return 0;
	}
	if (str[0] && (str[strlen(str) - 1] == '\n')) {
		str[strlen(str) - 1] = 0;
	}
	if (!str[0] || (str[0] < '1' || str[0] > '5') || strlen(str) > 1) {
		printf("Method not changed\n");
		return 0;
	}
	return atoi(str);
}

/*!
 * \brief Menu option to set delay value.
 * \param delay_type	Pointer to the description of the delay type.
 * \param menu_option	Pointer to the menusupport option to update.
 * \param delay			The current delay setting.
 */
 static int menu_get_delay(const char *delay_type, const char *menu_option, int delay)
{
	char str[100];
	int value, x;
	
	snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support %s", menu_option);
	if (astgetresp(str)) {
		return delay;
	}

	printf("Enter new %s setting (0-999, or C/R for '%d'): ", delay_type, delay);
	if (fgets(str, sizeof(str) - 1, stdin) == NULL) {
		printf("Setting not changed\n");
		return delay;
	}
	if (str[0] && (str[strlen(str) - 1] == '\n')) {
		str[strlen(str) - 1] = 0;
	}
	if (!str[0]) {
		printf("Setting not changed\n");
		return delay;
	}
	for (x = 0; str[x]; x++) {
		if (!isdigit(str[x])) {
			break;
		}
	}
	if (str[x] || (sscanf(str, "%d", &value) < 1) || (value < 0) || (value > 999)) {
		printf("Entry Error, setting not changed\n");
		return delay;
	}

	return value;
}

/*!
 * \brief Menu option view cos, ctcss and ptt status.
 */
 static void menu_view_status(void)
 {

	astgetresp(COMMAND_PREFIX "tune menu-support v");

}

/*!
 * \brief Main program entry point.
 */
 int main(int argc, char *argv[])
{
	int txmixaset = 0, txmixbset = 0, keying = 0, echomode = 0;
	int rxboost = 0, preemphasis = 0, deemphasis = 0;
	int plfilter = 0, pttmode = 0, carrierfrom = 0, ctcssfrom = 0;
	int rxondelay = 0, txoffdelay = 0;
	int rxmixerset = 0, micplaymax = 0, spkrmax = 0, micmax = 0;
	int result;
	char str[256];
	int opt;
	const char *device = NULL;

	signal(SIGCHLD, ourhandler);

	while ((opt = getopt(argc, argv, "n:")) != -1) {
		switch (opt) {
		case 'n':
			device = optarg;
			break;
		default: /* '?' */
			fprintf(stderr, "Usage: %s [-n node#]\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	if ((device != NULL) && (strlen(device) > 0)) {
		snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "active %s", device);
		if (astgetline(str, str, sizeof(str) - 1)) {
			printf("The chan_simpleusb active device could not be set!\n\n");
			printf("Verify that Asterisk is running and chan_simpleusb is loaded.\n\n");
			exit(EXIT_FAILURE);
		}
		if (strstr(str, "Active (command) Simple USB Radio device set to ") != str) {
			printf("%s\n", str);
			exit(EXIT_FAILURE);
		}
	}

	for (;;) {

		/* get device parameters from Asterisk */
		if (astgetline(COMMAND_PREFIX "tune menu-support 0+4", str, sizeof(str) - 1)) {
			printf("The chan_simpleusb setup information could not be retrieved!\n\n");
			printf("Verify that Asterisk is running and chan_simpleusb is loaded.\n\n");
			exit(255);
		}
		if (sscanf(str, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", 
			&txmixaset, &txmixbset, &echomode, &rxboost, &preemphasis, &deemphasis, 
			&plfilter, &pttmode, &carrierfrom, &ctcssfrom, &rxondelay, &txoffdelay,
			&rxmixerset, &micplaymax, &spkrmax, &micmax) != 16) {
			fprintf(stderr, "Error parsing device parameters: %s\n", str);
			exit(255);
		}
		printf("\n");

		/* print selected USB device at the top of our menu*/
		if (astgetresp(COMMAND_PREFIX "active")) {
			break;
		}

		printf("1) Select active USB device\n");
		printf("2) Set Rx Voice Level using display (currently '%d')\n", rxmixerset);
		if (keying) {
			printf("3) Set Transmit A Level (currently '%d') and send test tone\n", txmixaset);
		} else {
			printf("3) Set Transmit A Level (currently '%d')\n", txmixaset);
		}
		if (keying) {
			printf("4) Set Transmit B Level (currently '%d') and send test tone\n", txmixbset);
		} else {
			printf("4) Set Transmit B Level (currently '%d')\n", txmixbset);
		}
		printf("B) Toggle RX Boost (currently '%s')\n", rxboost ? "enabled" : "disabled");
		printf("C) Toggle Pre-emphasis (currently '%s')\n", preemphasis ? "enabled" : "disabled");
		printf("D) Toggle De-emphasis (currently '%s')\n", deemphasis ? "enabled" : "disabled");
		printf("E) Toggle Echo Mode (currently '%s')\n", echomode ? "enabled" : "disabled");
		printf("F) Flash (Toggle PTT and Tone output several times)\n");
		printf("G) Toggle PL Filter (currently '%s')\n", plfilter ? "enabled" : "disabled");
		printf("H) Toggle PTT mode (currently '%s')\n", pttmode ? "open" : "ground");
		printf("I) Change Carrier From (currently '%s')\n", signal_type[carrierfrom]);
		printf("J) Change CTCSS From (currently '%s')\n", signal_type[ctcssfrom]);
		printf("K) Change RX On Delay (currently '%d')\n", rxondelay);
		printf("L) Change TX Off Delay (currently '%d')\n", txoffdelay);
		printf("P) Print Current Parameter Values\n");
		printf("R) View Rx Audio Statistics\n");
		printf("S) Swap Current USB device with another USB device\n");
		printf("T) Toggle Transmit Test Tone/Keying (currently '%s')\n", keying ? "enabled" : "disabled");
		printf("V) View COS, CTCSS and PTT Status\n");
		printf("W) Write (Save) Current Parameter Values\n");
		printf("0) Exit Menu\n");
		printf("\nPlease enter your selection now: ");

		if (fgets(str, sizeof(str) - 1, stdin) == NULL) {
			break;
		}
		if (strlen(str) != 2) {	/* it's 2 because of \n at end */
			printf("Invalid Entry, try again\n");
			continue;
		}

		/* if to exit */
		if (str[0] == '0') {
			break;
		}

		switch (str[0]) {
		case '1':				/* select active usb device */
			menu_selectusb();
			break;
		case '2':				/* set receive level using display */
			menu_rxvoice();
			break;
		case '3':				/* set transmit level a */
			menu_txa(keying);
			break;
		case '4':				/* set transmit level b */
			menu_txb(keying);
			break;
		case 'b':				/* toggle rxboost */
		case 'B':
			if (rxboost) {
				if (astgetresp(COMMAND_PREFIX "tune menu-support m0")) {
					exit(255);
				}
			} else {
				if (astgetresp(COMMAND_PREFIX "tune menu-support m1")) {
					exit(255);
				}
			}
			break;
		case 'c':				/* toggle pre-emphasis */
		case 'C':
			if (preemphasis) {
				if (astgetresp(COMMAND_PREFIX "tune menu-support n0")) {
					exit(255);
				}
			} else {
				if (astgetresp(COMMAND_PREFIX "tune menu-support n1")) {
					exit(255);
				}
			}
			break;
		case 'd':				/* toggle de-emphasis */
		case 'D':
			if (deemphasis) {
				if (astgetresp(COMMAND_PREFIX "tune menu-support o0")) {
					exit(255);
				}
			} else {
				if (astgetresp(COMMAND_PREFIX "tune menu-support o1")) {
					exit(255);
				}
			}
			break;
		case 'e':				/* toggle echo mode */
		case 'E':
			if (echomode) {
				if (astgetresp(COMMAND_PREFIX "tune menu-support k0")) {
					exit(255);
				}
			} else {
				if (astgetresp(COMMAND_PREFIX "tune menu-support k1")) {
					exit(255);
				}
			}
			break;
		case 'f':				/* flash - toggle ptt and tone */
		case 'F':
			if (astgetresp(COMMAND_PREFIX "tune menu-support l")) {
				exit(255);
			}
			break;
		case 'g':				/* toggle pl filter */
		case 'G':
			if (plfilter) {
				if (astgetresp(COMMAND_PREFIX "tune menu-support p0")) {
					exit(255);
				}
			} else {
				if (astgetresp(COMMAND_PREFIX "tune menu-support p1")) {
					exit(255);
				}
			}
			break;
		case 'h':				/* toggle ptt mode */
		case 'H':
			if (pttmode) {
				if (astgetresp(COMMAND_PREFIX "tune menu-support q0")) {
					exit(255);
				}
			} else {
				if (astgetresp(COMMAND_PREFIX "tune menu-support q1")) {
					exit(255);
				}
			}
			break;
		case 'i':				/* select carrier from */
		case 'I':
			result = menu_signal_type("Carrier From", carrierfrom);
			if (result > 0) {
				snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support r%d", result - 1);
				astgetresp(str);
			}
			break;
		case 'j':				/* select ctcss from */
		case 'J':
			result = menu_signal_type("CTCSS From", ctcssfrom);
			if (result > 0) {
				snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support s%d", result - 1);
				astgetresp(str);
			}
			break;
		case 'k':				/* set rx on delay */
		case 'K':
			result = menu_get_delay("RX On Delay", "t", rxondelay);
			snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support t%d", result);
			astgetresp(str);
			break;
		case 'l':				/* set tx off delay */
		case 'L':
			result = menu_get_delay("TX Off Delay", "u", txoffdelay);
			snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support u%d", result);
			astgetresp(str);
			break;
		case 'p':				/* print current values */
		case 'P':
			if (astgetresp(COMMAND_PREFIX "tune menu-support 2")) {
				exit(255);
			}
			break;
		case 'r':				/* display receive audio statistics */
		case 'R':
			astgetresp(COMMAND_PREFIX "tune menu-support y");
			break;
		case 's':				/* swap usb device with another device */
		case 'S':
			menu_swapusb();
			break;
		case 't':				/* toggle test tone */
		case 'T':
			keying = !keying;
			printf("Transmit Test Tone/Keying is now %s\n", (keying) ? "Enabled" : "Disabled");
			break;
		case 'v':				/* view cos, ctcss, and ptt status - live */
		case 'V':
			menu_view_status();
			break;
		case 'w':				/* write settings to configuration file */
		case 'W':
			if (astgetresp(COMMAND_PREFIX "tune menu-support j")) {
				exit(255);
			}
			break;
		default:
			printf("Invalid Entry, try again\n");
			break;
		}
	}

	exit(0);
}


