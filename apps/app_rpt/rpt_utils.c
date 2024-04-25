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

#include <sys/vfs.h> /* use statfs */

#include "asterisk/channel.h" /* includes all the locking stuff needed (lock.h doesn't) */

#include "app_rpt.h"
#include "rpt_lock.h"
#include "rpt_utils.h"

int matchkeyword(char *string, char **param, char *keywords[])
{
	int i, ls;
	for (i = 0; keywords[i]; i++) {
		ls = strlen(keywords[i]);
		if (!ls) {
			if (param)
				*param = NULL;
			return 0;
		}
		if (!strncmp(string, keywords[i], ls)) {
			if (param)
				*param = string + ls;
			return i + 1;
		}
	}
	if (param)
		*param = NULL;
	return 0;
}

int explode_string(char *str, char *strp[], int limit, char delim, char quote)
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
				} else {
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

char *strupr(char *instr)
{
	char *str = instr;
	while (*str) {
		*str = toupper(*str);
		str++;
	}
	return (instr);
}

char *string_toupper(char *str)
{
	int i;

	for (i = 0; str[i]; i++) {
		if (islower(str[i])) {
			str[i] = toupper(str[i]);
		}
	}
	return str;
}

int finddelim(char *str, char *strp[], int limit)
{
	return explode_string(str, strp, limit, DELIMCHR, QUOTECHR);
}

char *skipchars(char *string, char *charlist)
{
	int i;
	while (*string) {
		for (i = 0; charlist[i]; i++) {
			if (*string == charlist[i]) {
				string++;
				break;
			}
		}
		if (!charlist[i])
			return string;
	}
	return string;
}

char *eatwhite(char *s)
{
	while ((*s == ' ') || (*s == 0x09)) {	/* get rid of any leading white space */
		if (!*s)
			break;
		s++;
	}
	return s;
}

int myatoi(const char *str)
{
	int ret;

	if (ast_strlen_zero(str)) {
		return -1;
	}

	/* leave this %i alone, non-base-10 input is useful here */
	if (sscanf(str, "%i", &ret) != 1) {
		return -1;
	}
	return ret;
}

int decimals2int(char *fraction)
{
	int i;
	char len = strlen(fraction);
	int multiplier = 100000;
	int res = 0;

	if (!len)
		return 0;
	for (i = 0; i < len; i++, multiplier /= 10)
		res += (fraction[i] - '0') * multiplier;
	return res;
}

int split_freq(char *mhz, char *decimals, char *freq)
{
	char freq_copy[MAXREMSTR];
	char *decp;

	ast_copy_string(freq_copy, freq, MAXREMSTR - 1);
	decp = strchr(freq_copy, '.');
	if (!decp) {
		return -1;
	}

	*decp++ = 0;
	ast_copy_string(mhz, freq_copy, MAXREMSTR);
	strcpy(decimals, "00000");
	ast_copy_string(decimals, decp, strlen(decimals) - 1);
	return 0;
}

int mycompar(const void *a, const void *b)
{
	char **x = (char **) a;
	char **y = (char **) b;
	int xoff, yoff;

	if ((**x < '0') || (**x > '9'))
		xoff = 1;
	else
		xoff = 0;
	if ((**y < '0') || (**y > '9'))
		yoff = 1;
	else
		yoff = 0;
	return (strcmp((*x) + xoff, (*y) + yoff));
}

long diskavail(struct rpt *myrpt)
{
	struct statfs statfsbuf;

	if (!myrpt->p.archivedir)
		return (0);
	if (statfs(myrpt->p.archivedir, &statfsbuf) == -1) {
		ast_log(LOG_WARNING, "Cannot get filesystem size for %s node %s\n", myrpt->p.archivedir, myrpt->name);
		return (-1);
	}
	return (statfsbuf.f_bavail);
}

/*
 Get the time for the machine's time zone
 Note: Asterisk requires a copy of localtime
 in the /etc directory for this to work properly.
 If /etc/localtime is not present, you will get
 GMT time! This is especially important on systems
 running embedded linux distributions as they don't usually
 have support for locales. 
*/
void rpt_localtime(time_t * t, struct ast_tm *lt, const char *tz)
{
	struct timeval tv;

	tv.tv_sec = *t;
	tv.tv_usec = 0;
	ast_localtime(&tv, lt, tz);

}

time_t rpt_mktime(struct ast_tm *tm, const char *zone)
{
	struct timeval now;

	now = ast_mktime(tm, zone);
	return now.tv_sec;
}
