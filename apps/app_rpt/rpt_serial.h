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
 * \brief Generic serial I/O routines
 */

/*! \brief Generic serial port open command */
int serial_open(char *fname, int speed, int stop2);

/*
 * Return receiver ready status
 *
 * Return 1 if an Rx byte is avalable
 * Return 0 if none was avaialable after a time out period
 * Return -1 if error
 */

int serial_rxready(int fd, int timeoutms);

/*
* Remove all RX characters in the receive buffer
*
* Return number of bytes flushed.
* or  return -1 if error
*
*/
int serial_rxflush(int fd, int timeoutms);

/*
 * Receive a string from the serial device
 */
int serial_rx(int fd, char *rxbuf, int rxmaxbytes, unsigned timeoutms, char termchr);

/*
 * Send a nul-terminated string to the serial device (without RX-flush)
 */
int serial_txstring(int fd, char *txstring);

/*
 * Write some bytes to the serial port, then optionally expect a fixed response
 */
int serial_io(int fd, char *txbuf, char *rxbuf, int txbytes, int rxmaxbytes, unsigned int timeoutms, char termchr);

/*! \brief Set the Data Terminal Ready (DTR) pin on a serial interface */
int setdtr(struct rpt *myrpt, int fd, int enable);

/*! \brief open the serial port */
int openserial(struct rpt *myrpt, const char *fname);

int serial_remote_io(struct rpt *myrpt, unsigned char *txbuf, int txbytes, unsigned char *rxbuf, int rxmaxbytes, int asciiflag);

int setrbi(struct rpt *myrpt);
int setrtx(struct rpt *myrpt);
int setxpmr(struct rpt *myrpt, int dotx);
int setrbi_check(struct rpt *myrpt);
int setrtx_check(struct rpt *myrpt);

int civ_cmd(struct rpt *myrpt, unsigned char *cmd, int cmdlen);
