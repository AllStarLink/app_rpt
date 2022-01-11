/*
 * Copyright 2010, KA1RBI
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * It is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 *
 */

#define USRP_VOICE_FRAME_SIZE (160*sizeof(short))  // 0.02 * 8k

enum { USRP_TYPE_VOICE=0, USRP_TYPE_DTMF, USRP_TYPE_TEXT };

// udp data header 
struct _chan_usrp_bufhdr {
	char eye[4];		// verification string
	uint32_t seq;		// sequence counter
	uint32_t memory;	// memory ID or zero (default)
	uint32_t keyup;		// tracks PTT state
	uint32_t talkgroup;	// trunk TG id
	uint32_t type;		// see above enum
	uint32_t mpxid;		// for future use
	uint32_t reserved;	// for future use
};
