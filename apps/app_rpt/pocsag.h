/*
 * POCSAG paging protocol generator
 *
 * Copyright (C) 2002-2017, Jim Dixon, WB6NIL and AllStarLink, Inc.
 *     and contributors.
 * Copyright (C) 2017-2024 AllStarLink, Inc., Naveen Albert, 
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
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * NOTE: THIS WILL ONLY WORK FOR LITTLE-ENDIAN BYTE ORDER!!!!!!
 */

#ifndef POCSAG_H
#define POCSAG_H

#include <stdint.h>

struct pocsag_batch {
  uint32_t sc;
  uint32_t frame[8][2];
  struct pocsag_batch *next;
} ;

enum pocsag_msgtype {TONE, NUMERIC, ALPHA} ;

#define SYNCH 0x7CD215D8;
#define IDLE  0x7A89C197;

struct pocsag_batch *make_pocsag_batch(uint32_t ric,char *data, 
	int size_of_data,int type,int toneno);
void free_batch(struct pocsag_batch *batch);

#endif /* POCSAG_H */
