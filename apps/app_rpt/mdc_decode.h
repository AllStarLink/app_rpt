/*-
 * mdc_decode.h
 *  header for mdc_decode.c
 *
 * 4 October 2010 - fixed for 64-bit
 *
 * Author: Matthew Kaufman (matthew@eeph.com)
 *
 * Copyright (c) 2005, 2010  Matthew Kaufman  All rights reserved.
 * 
 *  This file is part of Matthew Kaufman's MDC Encoder/Decoder Library
 *
 *  The MDC Encoder/Decoder Library is free software; you can
 *  redistribute it and/or modify it under the terms of version 2 of
 *  the GNU General Public License as published by the Free Software
 *  Foundation.
 *
 *  If you cannot comply with the terms of this license, contact
 *  the author for alternative license arrangements or do not use
 *  or redistribute this software.
 *
 *  The MDC Encoder/Decoder Library is distributed in the hope
 *  that it will be useful, but WITHOUT ANY WARRANTY; without even the
 *  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 *  USA.
 *
 *  or see http://www.gnu.org/copyleft/gpl.html
 *
-*/

#ifndef _MDC_DECODE_H_
#define _MDC_DECODE_H_

#ifndef TWOPI
#define TWOPI (2.0 * 3.1415926535)
#endif

#define MDC_ND 4	// number of decoders
#define MDC_GDTHRESH 5  // "good bits" threshold

#define DIFFERENTIATOR

typedef struct {
	int hyst;
	double incr;
	double th[MDC_ND];
	int level;
	int lastv;
	int zc[MDC_ND];
	int xorb[MDC_ND];
	unsigned int synclow[MDC_ND];
	unsigned int synchigh[MDC_ND];
	int shstate[MDC_ND];
	int shcount[MDC_ND];
	int bits[MDC_ND][112];
	int good;
	unsigned char op;
	unsigned char arg;
	unsigned short unitID;
	unsigned short crc;
	unsigned char extra0;
	unsigned char extra1;
	unsigned char extra2;
	unsigned char extra3;
} mdc_decoder_t;
	

/*
 mdc_decoder_new
 create a new mdc_decoder object

  parameters: int sampleRate - the sampling rate in Hz

  returns: an mdc_decoder object or null if failure

*/
mdc_decoder_t * mdc_decoder_new(int sampleRate);

/*
 mdc_decoder_process_samples
 process incoming samples using an mdc_decoder object
 (currently limited to 8-bit unsigned samples)

 parameters: mdc_decoder_t *decoder - pointer to the decoder object
             unsigned char *samples - pointer to 8-bit unsigned samples
             int numSamples - count of the number of samples in buffer

 returns: 0 if more samples are needed
         -1 if an error occurs
          1 if a decoded single packet is available to read
          2 if a decoded double packet is available to read
*/
 
int mdc_decoder_process_samples(mdc_decoder_t *decoder,
                                unsigned char *samples,
                                int numSamples);


/*
 mdc_decoder_get_packet
 retrieve last successfully decoded data packet from decoder object

 parameters: mdc_decoder_t *decoder - pointer to the decoder object
             unsigned char *op      - pointer to where to store "opcode"
             unsigned char *arg     - pointer to where to store "argument"
             unsigned short *unitID - pointer to where to store "unit ID"

 returns: -1 if error, 0 otherwise
*/

int mdc_decoder_get_packet(mdc_decoder_t *decoder, 
                           unsigned char *op,
			   unsigned char *arg,
			   unsigned short *unitID);

/*
 mdc_decoder_get_double_packet
 retrieve last successfully decoded double-length packet from decoder object

 parameters: mdc_decoder_t *decoder - pointer to the decoder object
             unsigned char *op      - pointer to where to store "opcode"
             unsigned char *arg     - pointer to where to store "argument"
             unsigned short *unitID - pointer to where to store "unit ID"
             unsigned char *extra0  - pointer to where to store 1st extra byte
             unsigned char *extra1  - pointer to where to store 2nd extra byte
             unsigned char *extra2  - pointer to where to store 3rd extra byte
             unsigned char *extra3  - pointer to where to store 4th extra byte

 returns: -1 if error, 0 otherwise
*/

int mdc_decoder_get_double_packet(mdc_decoder_t *decoder, 
                           unsigned char *op,
			   unsigned char *arg,
			   unsigned short *unitID,
                           unsigned char *extra0,
                           unsigned char *extra1,
                           unsigned char *extra2,
                           unsigned char *extra3);


#endif
