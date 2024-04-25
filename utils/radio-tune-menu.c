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
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
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
 *		0 - get echomode
 *		1 - get node names that are configured in simpleusb.conf
 *		2 - print parameters
 *		3 - get node names that are configured in simpleusb.conf, except current device
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
 *
 * Some of these commands take optional parameters to set values.
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

/*!
 * \brief Signal handler
 * \param sig		Signal to watch.
 */
static void ourhandler(int sig)
{
	int i;

	signal(sig, ourhandler);
	while (waitpid(-1, &i, WNOHANG) > 0);

	return;
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
	if (astgetresp("radio active")) {
		return;
	}

	/* get device list from Asterisk */
	if (astgetline("radio tune menu-support 1", buf, sizeof(buf) - 1)) {
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
	snprintf(str, sizeof(str) - 1, "radio active %s", strs[i - 1]);
	astgetresp(str);

	return;
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
	if (astgetresp("radio active")) {
		return;
	}

	/* get device list from Asterisk */
	if (astgetline("radio tune menu-support 3", buf, sizeof(buf) - 1)) {
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
	snprintf(str, sizeof(str) - 1, "radio tune swap %s", strs[i - 1]);
	astgetresp(str);

	return;
}

/*!
 * \brief Menu option to set rxvoice level.
 */
 static void menu_rxvoice(void)
 {
	int i, x;
	char str[100];

	for (;;) {
		if (astgetresp("radio tune menu-support b")) {
			break;
		}
		if (astgetresp("radio tune menu-support c")) {
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
		sprintf(str, "radio tune menu-support c%d", i);
		if (astgetresp(str)) {
			break;
		}
	}

	return;
}

/*!
 * \brief Menu option to set rxsquelch level.
 */
 static void menu_rxsquelch(void)
 {
	char str[100];
	int i, x;

	if (astgetresp("radio tune menu-support e")) {
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
	sprintf(str, "radio tune menu-support e%d", i);
	astgetresp(str);

	return;
}

/*!
 * \brief Menu option to set txvoice level.
 */
 static void menu_txvoice(int keying)
 {
	char str[100];
	int i, x;

	if (astgetresp("radio tune menu-support f")) {
		return;
	}

	printf("Enter new Tx Voice Level setting (0-999, or C/R for none): ");
	if (fgets(str, sizeof(str) - 1, stdin) == NULL) {
		printf("Tx Voice Level setting not changed\n");
		if (keying) {
			astgetresp("radio tune menu-support fK");
		}
		return;
	}
	if (str[0] && (str[strlen(str) - 1] == '\n')) {
		str[strlen(str) - 1] = 0;
	}
	if (!str[0]) {
		printf("Tx Voice Level setting not changed\n");
		if (keying) {
			astgetresp("radio tune menu-support fK");
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
		sprintf(str, "radio tune menu-support fK%d", i);
	} else {
		sprintf(str, "radio tune menu-support f%d", i);
	}
	astgetresp(str);

	return;
}

/*!
 * \brief Menu option to set auxvoice level.
 */
 static void menu_auxvoice(void)
{
	char str[100];
	int i, x;

	if (astgetresp("radio tune menu-support g")) {
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
	sprintf(str, "radio tune menu-support g%d", i);
	astgetresp(str);

	return;
}

/*!
 * \brief Menu option to set txtone level.
 */

 static void menu_txtone(int keying)
 {
	char str[100];
	int i, x;

	if (astgetresp("radio tune menu-support h")) {
		return;
	}

	printf("Enter new Tx CTCSS Modulation Level setting (0-999, or C/R for none): ");
	if (fgets(str, sizeof(str) - 1, stdin) == NULL) {
		printf("Tx CTCSS Modulation Level setting not changed\n");
		if (keying) {
			astgetresp("radio tune menu-support hK");
		}
		return;
	}
	if (str[0] && (str[strlen(str) - 1] == '\n')) {
		str[strlen(str) - 1] = 0;
	}
	if (!str[0]) {
		printf("Tx CTCSS Modulation Level setting not changed\n");
		if (keying) {
			astgetresp("radio tune menu-support hK");
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
		sprintf(str, "radio tune menu-support hK%d", i);
	} else {
		sprintf(str, "radio tune menu-support h%d", i);
	}
	astgetresp(str);

	return; 
}

/*!
 * \brief Main program entry point.
 */
 int main(int argc, char *argv[])
{
	int flatrx = 0, txhasctcss = 0, keying = 0, echomode = 0;
	char str[256];

	signal(SIGCHLD, ourhandler);

	for (;;) {

		/* get device parameters from Asterisk */
		if (astgetline("radio tune menu-support 0", str, sizeof(str) - 1)) {
			exit(255);
		}
		if (sscanf(str, "%d,%d,%d", &flatrx, &txhasctcss, &echomode) != 3) {
			fprintf(stderr, "Error parsing device parameters\n");
			exit(255);
		}
		printf("\n");

		/* print selected USB device */
		if (astgetresp("radio active")) {
			break;
		}
		printf("1) Select USB device\n");
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
		printf("E) Toggle Echo Mode (currently %s)\n", (echomode) ? "Enabled" : "Disabled");
		printf("F) Flash (Toggle PTT and Tone output several times)\n");
		printf("P) Print Current Parameter Values\n");
		printf("S) Swap Current USB device with another USB device\n");
		printf("T) Toggle Transmit Test Tone/Keying (currently %s)\n", (keying) ? "Enabled" : "Disabled");
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
		case '1':
			menu_selectusb();
			break;
		case '2':
			if (!flatrx) {
				break;
			}
			if (astgetresp("radio tune menu-support a")) {
				exit(255);
			}
			break;
		case '3':
			menu_rxvoice();
			break;
		case '4':
			if (!flatrx) {
				break;
			}
			if (astgetresp("radio tune menu-support d")) {
				exit(255);
			}
			break;
		case '5':
			if (!flatrx) {
				break;
			}
			menu_rxsquelch();
			break;
		case '6':
			menu_txvoice(keying);
			break;
		case '7':
			menu_auxvoice();
			break;
		case '8':
			if (!txhasctcss) {
				break;
			}
			menu_txtone(keying);
			break;
		case '9':
			if (!flatrx) {
				break;
			}
			if (astgetresp("radio tune menu-support i")) {
				exit(255);
			}
			break;
		case 'e':
		case 'E':
			if (echomode) {
				if (astgetresp("radio tune menu-support k0")) {
					exit(255);
				}
			} else {
				if (astgetresp("radio tune menu-support k1")) {
					exit(255);
				}
			}
			break;
		case 'f':
		case 'F':
			if (astgetresp("radio tune menu-support l")) {
				exit(255);
			}
			break;
		case 'p':
		case 'P':
			if (astgetresp("radio tune menu-support 2")) {
				exit(255);
			}
			break;
		case 's':
		case 'S':
			menu_swapusb();
			break;
		case 'w':
		case 'W':
			if (astgetresp("radio tune menu-support j")) {
				exit(255);
			}
			break;
		case 't':
		case 'T':
			keying = !keying;
			printf("Transmit Test Tone/Keying is now %s\n", (keying) ? "Enabled" : "Disabled");
			break;
		default:
			printf("Invalid Entry, try again\n");
			break;
		}
	}

	exit(0);
}


