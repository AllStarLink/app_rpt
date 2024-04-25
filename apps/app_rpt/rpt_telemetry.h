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

void rpt_telem_select(struct rpt *myrpt, int command_source, struct rpt_link *mylink);

/*
 * Parse a request METER request for telemetry thread
 * This is passed in a comma separated list of items from the function table entry
 * There should be 3 or 4 fields in the function table entry: device, channel, meter face, and  optionally: filter
 */

int handle_meter_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *args);

void flush_telem(struct rpt *myrpt);

/*! \brief Routine that hangs up all links and frees all threads related to them hence taking a "bird bath".  Makes a lot of noise/cleans up the mess */
void birdbath(struct rpt *myrpt);

/*! \note must be called locked */
void cancel_pfxtone(struct rpt *myrpt);

/*! Send telemetry tones */
int send_tone_telemetry(struct ast_channel *chan, char *tonestring);

int telem_any(struct rpt *myrpt, struct ast_channel *chan, char *entry);

/*! \brief This function looks up a telemetry name in the config file, and does a telemetry response as configured.
 * 4 types of telemtry are handled: Morse ID, Morse Message, Tone Sequence, and a File containing a recording. */
int telem_lookup(struct rpt *myrpt, struct ast_channel *chan, char *node, char *name);

/*! \brief Routine to process various telemetry commands that are in the myrpt structure
 * Used extensively when links and built/torn down and other events are processed by the rpt_master threads. */
void handle_varcmd_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *varcmd);

void *rpt_tele_thread(void *this);

/*! \brief More repeater telemetry routines. */
void rpt_telemetry(struct rpt *myrpt, int mode, void *data);
