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

#ifdef	_MDC_ENCODE_H_
#define	MDCGEN_BUFSIZE 2000

struct mdcgen_pvt
{
	mdc_encoder_t *mdc;
	struct ast_format *origwfmt;
	struct ast_frame f;
	char buf[(MDCGEN_BUFSIZE * 2) + AST_FRIENDLY_OFFSET];
	unsigned char cbuf[MDCGEN_BUFSIZE];
};

struct mdcparams
{
	char	type[10];
	short	UnitID;
	short	DestID;
	short	subcode;
};
#endif

void mdc1200_notify(struct rpt *myrpt, char *fromnode, char *data);

#ifdef	_MDC_DECODE_H_
void mdc1200_send(struct rpt *myrpt, char *data);
void mdc1200_cmd(struct rpt *myrpt, char *data);
#ifdef	_MDC_ENCODE_H_
void mdc1200_ack_status(struct rpt *myrpt, short UnitID);
#endif
#endif

#ifdef	_MDC_ENCODE_H_
int mdc1200gen_start(struct ast_channel *chan, char *type, short UnitID, short destID, short subcode);
int mdc1200gen(struct ast_channel *chan, char *type, short UnitID, short destID, short subcode);
#endif

int mdc1200_load(void);
int mdc1200_unload(void);
