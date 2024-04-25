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
 * \brief Uchameleon specific routines
 */

/*! \brief Start the Uchameleon monitor thread */
int uchameleon_thread_start(struct daq_entry_tag *t);

int uchameleon_connect(struct daq_entry_tag *t);

/*! \brief Uchameleon alarm handler */
void uchameleon_alarm_handler(struct daq_pin_entry_tag *p);

/*! \brief Initialize pins */
int uchameleon_pin_init(struct daq_entry_tag *t);

/*! \brief Open the serial channel and test for the uchameleon device at the end of the link */
int uchameleon_open(struct daq_entry_tag *t);

/*! \brief Close uchameleon */
int uchameleon_close(struct daq_entry_tag *t);

/*! \brief Uchameleon generic interface which supports monitor thread */
int uchameleon_do_long(struct daq_entry_tag *t, int pin, int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2);

/*! \brief Queue up a tx command (used exclusively by uchameleon_monitor()) */
void uchameleon_queue_tx(struct daq_entry_tag *t, char *txbuff);

/*! \brief Monitor thread for Uchameleon devices */
/*! \note started by uchameleon_open() and shutdown by uchameleon_close() */
void *uchameleon_monitor_thread(void *this);
