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

/*! \file
 *
 * \brief RPT translate functions
 */

#include "asterisk.h"

#include "asterisk/channel.h"

#include "app_rpt.h"
#include "rpt_translate.h"

char func_xlat(struct rpt *myrpt, char c, struct rpt_xlat *xlat)
{
	time_t now;
	int gotone;

	time(&now);
	gotone = 0;
	/* if too much time, reset the skate machine */
	if ((now - xlat->lastone) > MAXXLATTIME) {
		xlat->funcindex = xlat->endindex = 0;
	}
	if (xlat->funccharseq[0] && (c == xlat->funccharseq[xlat->funcindex++])) {
		time(&xlat->lastone);
		gotone = 1;
		if (!xlat->funccharseq[xlat->funcindex]) {
			xlat->funcindex = xlat->endindex = 0;
			return (myrpt->p.funcchar);
		}
	} else
		xlat->funcindex = 0;
	if (xlat->endcharseq[0] && (c == xlat->endcharseq[xlat->endindex++])) {
		time(&xlat->lastone);
		gotone = 1;
		if (!xlat->endcharseq[xlat->endindex]) {
			xlat->funcindex = xlat->endindex = 0;
			return (myrpt->p.endchar);
		}
	} else
		xlat->endindex = 0;
	/* if in middle of decode seq, send nothing back */
	if (gotone)
		return (0);
	/* if no pass chars specified, return em all */
	if (!xlat->passchars[0])
		return (c);
	/* if a "pass char", pass it */
	if (strchr(xlat->passchars, c))
		return (c);
	return (0);
}

char aprstt_xlat(const char *instr, char *outstr)
{
	int i, j;
	char b, c, lastnum, overlay, cksum;
	static char a_xlat[] = { 0, 0, 'A', 'D', 'G', 'J', 'M', 'P', 'T', 'W' };
	static char b_xlat[] = { 0, 0, 'B', 'E', 'H', 'K', 'N', 'Q', 'U', 'X' };
	static char c_xlat[] = { 0, 0, 'C', 'F', 'I', 'L', 'O', 'R', 'V', 'Y' };
	static char d_xlat[] = { 0, 0, 0, 0, 0, 0, 0, 'S', 0, 'Z' };

	if (strlen(instr) < 4)
		return 0;
	lastnum = 0;
	for (i = 1; instr[i + 2]; i++) {
		c = instr[i];
		switch (c) {
		case 'A':
			if (!lastnum)
				return 0;
			b = a_xlat[lastnum - '0'];
			if (!b)
				return 0;
			*outstr++ = b;
			lastnum = 0;
			break;
		case 'B':
			if (!lastnum)
				return 0;
			b = b_xlat[lastnum - '0'];
			if (!b)
				return 0;
			*outstr++ = b;
			lastnum = 0;
			break;
		case 'C':
			if (!lastnum)
				return 0;
			b = c_xlat[lastnum - '0'];
			if (!b)
				return 0;
			*outstr++ = b;
			lastnum = 0;
			break;
		case 'D':
			if (!lastnum)
				return 0;
			b = d_xlat[lastnum - '0'];
			if (!b)
				return 0;
			*outstr++ = b;
			lastnum = 0;
			break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			if (lastnum)
				*outstr++ = lastnum;
			lastnum = c;
			break;
		default:
			return 0;
		}
	}
	*outstr = 0;
	overlay = instr[i++];
	cksum = instr[i];
	for (i = 0, j = 0; instr[i + 1]; i++) {
		if ((instr[i] >= '0') && (instr[i] <= '9'))
			j += (instr[i] - '0');
		else if ((instr[i] >= 'A') && (instr[i] <= 'D'))
			j += (instr[i] - 'A') + 10;
	}
	if ((cksum - '0') != (j % 10))
		return 0;
	return overlay;
}
