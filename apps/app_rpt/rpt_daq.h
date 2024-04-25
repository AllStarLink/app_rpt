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

/*
 * Open a daq device
 */

struct daq_entry_tag *daq_open(int type, char *name, char *dev);

/*
 * Close a daq device
 */

int daq_close(struct daq_entry_tag *t);

/*
 * Look up a device entry for a particular device name
 */

struct daq_entry_tag *daq_devtoentry(char *name);

/*
 * Reset a minimum or maximum reading
 */

int uchameleon_reset_minmax(struct daq_entry_tag *t, int pin, int minmax);

/*
 * Do something with the daq subsystem
 */

int daq_do_long(struct daq_entry_tag *t, int pin, int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2);

/*
 * Short version of above
 */

int daq_do(struct daq_entry_tag *t, int pin, int cmd, int arg1);

/*
 * Function to reset the long term minimum or maximum
 */

int daq_reset_minmax(char *device, int pin, int minmax);

/*
 * Initialize DAQ subsystem
 */

void daq_init(struct ast_config *cfg);

/*
 * Uninitialize DAQ Subsystem
 */

void daq_uninit(void);

/*! \brief Handle USEROUT telemetry */
int handle_userout_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *args);
