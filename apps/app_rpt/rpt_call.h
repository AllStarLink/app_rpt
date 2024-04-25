/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Naveen Albert <asterisk@phreaknet.org>
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
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 *
 * \brief app_rpt call helper functions
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 */

/*! \brief Disable CDR for a call */
int rpt_disable_cdr(struct ast_channel *chan);

int rpt_setup_call(struct ast_channel *chan, const char *addr, int timeout, const char *driver, const char *data, const char *desc, const char *callerid);

int rpt_make_call(struct ast_channel *chan, const char *addr, int timeout, const char *driver, const char *data, const char *desc, const char *callerid);

/*! \brief Routine to forward a "call" from one channel to another */
void rpt_forward(struct ast_channel *chan, char *dialstr, char *nodefrom);
