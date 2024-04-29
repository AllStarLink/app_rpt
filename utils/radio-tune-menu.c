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
 * UsbRadio tune menu program
 *
 * This program communicates with Asterisk by sending commands to retrieve and set values
 * for the usbradio channel driver.
 *
 * The following 'menu-support' commands are used:
 *
 * susb tune menusupport X - where X is one of the following:
 *		0 - get current settings
 *		1 - get node names that are configured in simpleusb.conf
 *		2 - print parameters
 *		3 - get node names that are configured in simpleusb.conf, except current device
 *		a - receive rx level
 *		b - receiver tune display
 *		c - receive level
 *		d - receive ctcss level
 *		e - squelch level
 *		f - voice level
 *		g - aux level
 *		h - transmit a test tone
 *		i - tune receive level
 *		j - save current settings for the selected node
 *		k - change echo mode
 *		l - generate test tone
 *		m - change rxboost
 *		n - change txboost
 *		o - change carrier from
 *		p - change ctcss from
 *		q - change rx on delay
 *		r - change tx off delay
 *		s - change tx pre limiting
 *		t - change tx limiting only
 *		u - change rx demodulation
 *		v - view cos, ctcss and ptt status
 *		w - change tx mixer a
 *		x - change tx mixer b
 *
 * Most of these commands take optional parameters to set values.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

/*! \brief type of signal detection used for carrier (cd) or ctcss (sd) */
static const char * const cd_signal_type[] = {"no", "dsp", "vox", "usb", "usbinvert", "pp", "ppinvert"};
static const char * const sd_signal_type[] = {"no", "usb", "usbinvert", "dsp", "pp", "ppinvert"};

/*! \brief demodulation type */
static const char * const demodulation_type[] = {"no", "speaker", "flat"};

/*! \brief mixer type */
static const char * const mixer_type[] = {"no", "voice", "tone", "composite", "auxvoice"};

/*! \brief command prefix for Asterisk - simpleusb channel driver access */
#define COMMAND_PREFIX "radio "

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
 * \param delim		Specified delimeter
 * \param quote		User specified quote for escaping a substring. Set to zero to escape nothing.
 *
 * \retval 			Returns number of substrings found.
 */
static int explode_string(char *str, char *strp[], int limit, char delim, char quote)
{
	int i, l, inquo;

	inquo = 0;
	i = 0;
	strp[i++] = str;

	if (!*str) {
		strp[0] = 0;
		return (0);
	}
	for (l = 0; *str && (l < limit); str++) {
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
			l++;
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
 * akward, but needed to support having an fd of 0, which
 * is likely, since thats most likely stdin
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
	n = explode_string(buf, strs, 100, ',', 0);
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
	n = explode_string(buf, strs, 100, ',', 0);
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
 * \brief Menu option to set rxsquelch level.
 */
 static void menu_rxsquelch(void)
 {
	char str[100];
	int i, x;

	if (astgetresp(COMMAND_PREFIX "tune menu-support e")) {
		return;
	}

	printf("Enter new Squelch setting (0-999, or C/R for none): ");
	if (fgets(str, sizeof(str) - 1, stdin) == NULL) {
		printf("Rx Squelch Level setting not changed\n");
		return;
	}
	if (str[0] && (str[strlen(str) - 1] == '\n')) {
		str[strlen(str) - 1] = 0;
	}
	if (!str[0]) {
		printf("Rx Squelch Level setting not changed\n");
		return;
	}
	for (x = 0; str[x]; x++) {
		if (!isdigit(str[x])) {
			break;
		}
	}
	if (str[x] || (sscanf(str, "%d", &i) < 1) || (i < 0) || (i > 999)) {
		printf("Entry Error, Rx Squelch Level setting not changed\n");
		return;
	}
	snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support e%d", i);
	astgetresp(str);

}

/*!
 * \brief Menu option to set txvoice level.
 * \param keying	Boolean to indicate if we are currently keying.
 */
 static void menu_txvoice(int keying)
 {
	char str[100];
	int i, x;

	if (astgetresp(COMMAND_PREFIX "tune menu-support f")) {
		return;
	}

	printf("Enter new Tx Voice Level setting (0-999, or C/R for none): ");
	if (fgets(str, sizeof(str) - 1, stdin) == NULL) {
		printf("Tx Voice Level setting not changed\n");
		if (keying) {
			astgetresp(COMMAND_PREFIX "tune menu-support fK");
		}
		return;
	}
	if (str[0] && (str[strlen(str) - 1] == '\n')) {
		str[strlen(str) - 1] = 0;
	}
	if (!str[0]) {
		printf("Tx Voice Level setting not changed\n");
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
		printf("Entry Error, Tx Voice Level setting not changed\n");
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
 * \brief Menu option to set auxvoice level.
 */
 static void menu_auxvoice(void)
{
	char str[100];
	int i, x;

	if (astgetresp(COMMAND_PREFIX "tune menu-support g")) {
		return;
	}

	printf("Enter new Aux Voice Level setting (0-999, or C/R for none): ");
	if (fgets(str, sizeof(str) - 1, stdin) == NULL) {
		printf("Entry Error, Aux Voice Level setting not changed\n");
		return;
	}
	if (str[0] && (str[strlen(str) - 1] == '\n')) {
		str[strlen(str) - 1] = 0;
	}
	if (!str[0]) {
		printf("Entry Error, Aux Voice Level setting not changed\n");
		return;
	}
	for (x = 0; str[x]; x++) {
		if (!isdigit(str[x])) {
			break;
		}
	}
	if (str[x] || (sscanf(str, "%d", &i) < 1) || (i < 0) || (i > 999)) {
		printf("Entry Error, Aux Voice Level setting not changed\n");
		return;
	}
	snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support g%d", i);
	astgetresp(str);

}

/*!
 * \brief Menu option to set txtone level.
 * \param keying	Boolean to indicate if we are currently keying.
 */

 static void menu_txtone(int keying)
 {
	char str[100];
	int i, x;

	if (astgetresp(COMMAND_PREFIX "tune menu-support h")) {
		return;
	}

	printf("Enter new Tx CTCSS Modulation Level setting (0-999, or C/R for none): ");
	if (fgets(str, sizeof(str) - 1, stdin) == NULL) {
		printf("Tx CTCSS Modulation Level setting not changed\n");
		if (keying) {
			astgetresp(COMMAND_PREFIX "tune menu-support hK");
		}
		return;
	}
	if (str[0] && (str[strlen(str) - 1] == '\n')) {
		str[strlen(str) - 1] = 0;
	}
	if (!str[0]) {
		printf("Tx CTCSS Modulation Level setting not changed\n");
		if (keying) {
			astgetresp(COMMAND_PREFIX "tune menu-support hK");
		}
		return;
	}
	for (x = 0; str[x]; x++) {
		if (!isdigit(str[x])) {
			break;
		}
	}
	if (str[x] || (sscanf(str, "%d", &i) < 1) || (i < 0) || (i > 999)) {
		printf("Entry Error, Tx CTCSS Modulation Level setting not changed\n");
		return;
	}
	if (keying) {
		snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support hK%d", i);
	} else {
		snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support h%d", i);
	}
	astgetresp(str);

}

/*!
 * \brief Menu option view cos, ctcss and ptt status.
 */
 static void menu_view_status(void)
 {

	astgetresp(COMMAND_PREFIX "tune menu-support v");

}

/*!
 * \brief Menu option to select list value.
 * \param value_name	Pointer to description of item being changed.
 * \param items			Pointer to array of options.
 * \param max_items		Number of items in the items array.
 * \param selection		Current selected item.
 */
 static int menu_select_value(const char *value_name, const char * const *items, int max_items, int selection)
{
	char str[100];
	int i;

	printf("\nPlease select from the following methods for %s:\n", value_name);

	for (i = 0; i < max_items; i++) {
		printf("%d) %s %s\n", i + 1, items[i], selection == i ? "- Current" : "");
	}

	printf("Select new %s or C/R for current): ", value_name);
	if (fgets(str, sizeof(str) - 1, stdin) == NULL) {
		printf("Method not changed\n");
		return 0;
	}
	if (str[0] && (str[strlen(str) - 1] == '\n')) {
		str[strlen(str) - 1] = 0;
	}
	if (!str[0] || (str[0] < '1' || atoi(str) > max_items)) {
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
 * \brief Options menu.
 */
static void options_menu(void)
{
	int flatrx = 0, txhasctcss = 0, echomode = 0;
	int rxboost = 0, txboost = 0, carrierfrom = 0, ctcssfrom = 0;
	int rxondelay = 0, txoffdelay = 0, txprelim = 0, txlimonly = 0;
	int rxdemod = 0, txmixa = 0, txmixb = 0;
	int result;
	char str[256];

	for (;;) {
		
		/* get device parameters from Asterisk */
		if (astgetline(COMMAND_PREFIX "tune menu-support 0", str, sizeof(str) - 1)) {
			return;
		}
		if (sscanf(str, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", 
			&flatrx, &txhasctcss, &echomode, &rxboost, &txboost,
			&carrierfrom, &ctcssfrom, &rxondelay, &txoffdelay,
			&txprelim, & txlimonly, &rxdemod, &txmixa, &txmixb) != 14) {
			fprintf(stderr, "Error parsing device parameters: %s\n", str);
			return;
		}

		printf("\nOptions Menu\n");
		printf("1) Toggle RX Boost (currently '%s')\n", rxboost ? "enabled" : "disabled");
		printf("2) Toggle TX Boost (currently '%s')\n", txboost ? "enabled" : "disabled");
		printf("3) Change RX Demodulation (currently '%s')\n", demodulation_type[rxdemod]);
		printf("4) Change RX On Delay (currently '%d')\n", rxondelay);
		printf("5) Change TX Off Delay (currently '%d')\n", txoffdelay);
		printf("6) Toggle TX Prelimiting (currently '%s')\n", rxboost ? "enabled" : "disabled");
		printf("7) Toggle TX Limiting Only (currently '%s')\n", txboost ? "enabled" : "disabled");
		printf("8) Change TX Mixer A (currently '%s')\n", mixer_type[txmixa]);
		printf("9) Change Tx Mixer B (currently '%s')\n", mixer_type[txmixb]);
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
		case '1':				/* toggle rxboost */
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
		case '2':				/* toggle txboost */
			if (txboost) {
				if (astgetresp(COMMAND_PREFIX "tune menu-support n0")) {
					exit(255);
				}
			} else {
				if (astgetresp(COMMAND_PREFIX "tune menu-support n1")) {
					exit(255);
				}
			}
			break;
		case '3':				/* select rx demodulation */
			result = menu_select_value("RX Demodulation", demodulation_type, 3, rxdemod);
			if (result > 0) {
				snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support u%d", result - 1);
				astgetresp(str);
			}
			break;
		case '4':				/* set rx on delay */
			result = menu_get_delay("RX On Delay", "q", rxondelay);
			snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support q%d", result);
			astgetresp(str);
			break;
		case '5':				/* set tx off delay */
			result = menu_get_delay("TX Off Delay", "r", txoffdelay);
			snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support r%d", result);
			astgetresp(str);
			break;
		case '6':				/* toggle txprelim */
			if (txprelim) {
				if (astgetresp(COMMAND_PREFIX "tune menu-support s0")) {
					exit(255);
				}
			} else {
				if (astgetresp(COMMAND_PREFIX "tune menu-support s1")) {
					exit(255);
				}
			}
			break;
		case '7':				/* toggle txlimonly */
			if (txlimonly) {
				if (astgetresp(COMMAND_PREFIX "tune menu-support t0")) {
					exit(255);
				}
			} else {
				if (astgetresp(COMMAND_PREFIX "tune menu-support t1")) {
					exit(255);
				}
			}
			break;
		case '8':				/* select tx mixer a */
			result = menu_select_value("TX Mixer A", mixer_type, 5, txmixa);
			if (result > 0) {
				snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support w%d", result - 1);
				astgetresp(str);
			}
			break;
		case '9':				/* select tx mixer b */
			result = menu_select_value("TX Mixer B", mixer_type, 5, txmixb);
			if (result > 0) {
				snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support x%d", result - 1);
				astgetresp(str);
			}
			break;
		default:
			printf("Invalid Entry, try again\n");
			break;
		}
	}		
}

/*!
 * \brief Main program entry point.
 */
 int main(int argc, char *argv[])
{
	int flatrx = 0, txhasctcss = 0, keying = 0, echomode = 0;
	int rxboost = 0, txboost = 0, carrierfrom = 0, ctcssfrom = 0;
	int rxondelay = 0, txoffdelay = 0, txprelim = 0, txlimonly = 0;
	int rxdemod = 0, txmixa = 0, txmixb = 0;
	char str[256];
	int result;

	signal(SIGCHLD, ourhandler);

	for (;;) {

		/* get device parameters from Asterisk */
		if (astgetline(COMMAND_PREFIX "tune menu-support 0", str, sizeof(str) - 1)) {
			printf("The setup information for chan_usbradio could not be retrieved!\n\n");
			printf("Verify that Asterisk is running and chan_usbradio is loaded.\n\n");
			exit(255);
		}
		if (sscanf(str, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", 
			&flatrx, &txhasctcss, &echomode, &rxboost, &txboost,
			&carrierfrom, &ctcssfrom, &rxondelay, &txoffdelay,
			&txprelim, & txlimonly, &rxdemod, &txmixa, &txmixb) != 14) {
			fprintf(stderr, "Error parsing device parameters: %s\n", str);
			exit(255);
		}
		printf("\n");

		/* print selected USB device  at the top of our menu*/
		if (astgetresp(COMMAND_PREFIX "active")) {
			break;
		}
		printf("1) Select active USB device\n");
		if (flatrx) {
			printf("2) Auto-Detect Rx Noise Level Value (with no carrier)\n");
		} else {
			printf("2) Does not apply to this USB device configuration\n");
		}
		printf("3) Set Rx Voice Level (using display)\n");
		if (flatrx) {
			printf("4) Auto-Detect Rx CTCSS Level Value (with carrier + CTCSS)\n");
		} else {
			printf("4) Does not apply to this USB device configuration\n");
		}
		if (flatrx) {
			printf("5) Set Rx Squelch Level\n");
		} else {
			printf("5) Does not apply to this USB device configuration\n");
		}
		if (keying) {
			printf("6) Set Transmit Voice Level and send test tone (no CTCSS)\n");
		} else {
			printf("6) Set Transmit Voice Level\n");
		}
		printf("7) Set Transmit Aux Voice Level\n");
		if (txhasctcss) {
			if (keying) {
				printf("8) Set Transmit CTCSS Level and send CTCSS tone\n");
			} else {
				printf("8) Set Transmit CTCSS Level\n");
			}
		} else {
			printf("8) Does not apply to this USB device configuration\n");
		}
		if (flatrx) {
			printf("9) Auto-Detect Rx Voice Level Value (with carrier + 1KHz @ 3KHz Dev)\n");
		} else {
			printf("9) Does not apply to this USB device configuration\n");
		}
		printf("E) Toggle Echo Mode (currently '%s')\n", (echomode) ? "enabled" : "disabled");
		printf("F) Flash (Toggle PTT and Tone output several times)\n");
		printf("G) Change Carrier From (currently '%s')\n", cd_signal_type[carrierfrom]);
		printf("H) Change CTCSS From (currently '%s')\n", sd_signal_type[ctcssfrom]);
		printf("P) Print Current Parameter Values\n");
		printf("O) Options Menu\n");
		printf("S) Swap Current USB device with another USB device\n");
		printf("T) Toggle Transmit Test Tone/Keying (currently '%s')\n", (keying) ? "enabled" : "disabled");
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
			break;				/* select flatrx */
		case '2':
			if (!flatrx) {
				break;
			}
			if (astgetresp(COMMAND_PREFIX "tune menu-support a")) {
				exit(255);
			}
			break;
		case '3':				/* set receive level using display */
			menu_rxvoice();
			break;
		case '4':				/* set ctcss level */
			if (!flatrx) {
				break;
			}
			if (astgetresp(COMMAND_PREFIX "tune menu-support d")) {
				exit(255);
			}
			break;
		case '5':				/* set squelch level */
			if (!flatrx) {
				break;
			}
			menu_rxsquelch();
			break;
		case '6':				/* set tx level */
			menu_txvoice(keying);
			break;
		case '7':				/* set aux level */
			menu_auxvoice();
			break;
		case '8':				/* set ctcss level */
			if (!txhasctcss) {
				break;
			}
			menu_txtone(keying);
			break;
		case '9':				/* set auto detect rx voice level */
			if (!flatrx) {
				break;
			}
			if (astgetresp(COMMAND_PREFIX "tune menu-support i")) {
				exit(255);
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
		case 'g':				/* select carrier from */
		case 'G':
			result = menu_select_value("Carrier From", cd_signal_type, 7, carrierfrom);
			if (result > 0) {
				snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support o%d", result - 1);
				astgetresp(str);
			}
			break;
		case 'h':				/* select ctcss from */
		case 'H':
			result = menu_select_value("CTCSS From", sd_signal_type, 6, ctcssfrom);
			if (result > 0) {
				snprintf(str, sizeof(str) - 1, COMMAND_PREFIX "tune menu-support p%d", result - 1);
				astgetresp(str);
			}
			break;
		case 'o':				/* options menu */
		case 'O':
			options_menu();
			break;
		case 'p':				/* print current values */
		case 'P':
			if (astgetresp(COMMAND_PREFIX "tune menu-support 2")) {
				exit(255);
			}
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


