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
 * \brief RPT link functions
 */

void init_linkmode(struct rpt *myrpt, struct rpt_link *mylink, int linktype);

void set_linkmode(struct rpt_link *mylink, int linkmode);

int altlink(struct rpt *myrpt, struct rpt_link *mylink);

/*!
 * \brief Add an rpt_tele to a rpt
 * \param myrpt
 * \param t Telemetry to insert into the repeater's linked list of telemetries
 */
void tele_link_add(struct rpt *myrpt, struct rpt_tele *t);

/*!
 * \brief Remove an rpt_tele from a rpt
 * \param myrpt
 * \param t Telemetry to remove from the repeater's linked list of telemetries
 */
void tele_link_remove(struct rpt *myrpt, struct rpt_tele *t);

int altlink1(struct rpt *myrpt, struct rpt_link *mylink);

void rpt_qwrite(struct rpt_link *l, struct ast_frame *f);

int linkcount(struct rpt *myrpt);

/*! \brief Considers repeater received RSSI and all voter link RSSI information and set values in myrpt structure. */
int FindBestRssi(struct rpt *myrpt);

void do_dtmf_phone(struct rpt *myrpt, struct rpt_link *mylink, char c);

/*! \brief Send rx rssi out on all links. */
void rssi_send(struct rpt *myrpt);

void send_link_dtmf(struct rpt *myrpt, char c);

void send_link_keyquery(struct rpt *myrpt);

void send_tele_link(struct rpt *myrpt, char *cmd);

/*!
 * \brief Add an rpt_link to a rpt
 * \param myrpt
 * \param l Link to insert into the repeater's linked list of links
 */
void rpt_link_add(struct rpt *myrpt, struct rpt_link *l);

/*!
 * \brief Remove an rpt_link from a rpt
 * \param myrpt
 * \param l Link to remove from the repeater's linked list of links
 */
void rpt_link_remove(struct rpt *myrpt, struct rpt_link *l);

/*! \brief must be called locked */
void __mklinklist(struct rpt *myrpt, struct rpt_link *mylink, char *buf, int flag);

/*! \brief must be called locked */
void __kickshort(struct rpt *myrpt);

/*! \brief Updates the active links (channels) list that that the repeater has */
void rpt_update_links(struct rpt *myrpt);

/*! 
 * \brief Connect a link 
 * \retval -2 Attempt to connect to self 
 * \retval -1 No such node
 * \retval 0 Success
 * \retval 1 No match yet
 * \retval 2 Already connected to this node
 */
int connect_link(struct rpt *myrpt, char *node, int mode, int perma);
