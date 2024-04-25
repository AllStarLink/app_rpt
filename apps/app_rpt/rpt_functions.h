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

/* Define function protos for function table here */

/*! \brief Internet linking function */
int function_ilink(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Autopatch up */
int function_autopatchup(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Autopatch down */
int function_autopatchdn(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Status */
int function_status(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief COP - Control operator */
int function_cop(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Remote base function */
int function_remote(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Macro-oni (without Salami) */
int function_macro(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Playback a recording globally */
int function_playback(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Playback a recording locally */
int function_localplay(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Playback a meter reading */
int function_meter(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Set or reset a USER Output bit */
int function_userout(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);

/*! \brief Execute shell command */
int function_cmd(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
