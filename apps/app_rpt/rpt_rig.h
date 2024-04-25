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

int set_ft897(struct rpt *myrpt);
int set_ft100(struct rpt *myrpt);
int set_ft950(struct rpt *myrpt);
int set_ic706(struct rpt *myrpt);
int setkenwood(struct rpt *myrpt);
int set_tm271(struct rpt *myrpt);
int set_tmd700(struct rpt *myrpt);

/*! \brief Split ctcss frequency into hertz and decimal */
/*! \todo should be in rpt_utils for consistency? */
int split_ctcss_freq(char *hertz, char *decimal, char *freq);

int set_mode_ft897(struct rpt *myrpt, char newmode);
int set_mode_ft100(struct rpt *myrpt, char newmode);
int set_mode_ic706(struct rpt *myrpt, char newmode);

int simple_command_ft897(struct rpt *myrpt, char command);
int simple_command_ft100(struct rpt *myrpt, unsigned char command, unsigned char p1);

/*! \brief Dispatch to correct I/O handler  */
int setrem(struct rpt *myrpt);

int closerem(struct rpt *myrpt);

/*! \brief Dispatch to correct RX frequency checker */
int check_freq(struct rpt *myrpt, int m, int d, int *defmode);

/*! \brief Check TX frequency before transmitting */
/*! \retval 1 if tx frequency in ok. */
char check_tx_freq(struct rpt *myrpt);

/*! \brief Dispatch to correct frequency bumping function */
int multimode_bump_freq(struct rpt *myrpt, int interval);

/*! \brief Queue announcment that scan has been stopped */
void stop_scan(struct rpt *myrpt);

/*! \brief This is called periodically when in scan mode */
int service_scan(struct rpt *myrpt);

/*! \brief steer the radio selected channel to either one programmed into the radio
 * or if the radio is VFO agile, to an rpt.conf memory location. */
int channel_steer(struct rpt *myrpt, char *data);

int channel_revert(struct rpt *myrpt);
