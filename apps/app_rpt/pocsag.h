/*
 * POCSAG paging protocol generator
 *
 * Copyright (C) 2013, Jim Dixon, WB6NIL
 *
 * Jim Dixon, WB6NIL <jim@lambdatel.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
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
