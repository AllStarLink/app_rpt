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

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"

#include "app_rpt.h"
#include "rpt_capabilities.h"

int iswebtransceiver(struct rpt_link *l)
{
	int i;

	if (!l)
		return 0;
	for (i = 0; l->name[i]; i++) {
		if (!isdigit(l->name[i]))
			return 1;
	}
	return 0;
}

int multimode_capable(struct rpt *myrpt)
{
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT897))
		return 1;
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT100))
		return 1;
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT950))
		return 1;
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_IC706))
		return 1;
	return 0;
}

int narrow_capable(struct rpt *myrpt)
{
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD))
		return 1;
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_TMD700))
		return 1;
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_TM271))
		return 1;
	return 0;
}

char is_paging(struct rpt *myrpt)
{
	char rv = 0;

	if ((!ast_tvzero(myrpt->paging)) && (ast_tvdiff_ms(ast_tvnow(), myrpt->paging) <= 300000))
		rv = 1;
	return (rv);
}
