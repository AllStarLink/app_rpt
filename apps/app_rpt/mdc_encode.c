/*-
 * mdc_encode.c
 *  Encodes a specific format from 1200 BPS MSK data burst
 *  to output audio samples.
 *
 * Author: Matthew Kaufman (matthew@eeph.com)
 *
 * Copyright (c) 2005  Matthew Kaufman  All rights reserved.
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

#include <stdlib.h>
#include "allstar/mdc_encode.h"

static unsigned char sin8[] = {
      127, 130, 133, 136, 139, 142, 145, 148, 151, 154, 157, 160,
      163, 166, 169, 172, 175, 178, 180, 183, 186, 189, 191, 194,
      196, 199, 201, 204, 206, 209, 211, 213, 215, 218, 220, 222,
      224, 226, 227, 229, 231, 233, 234, 236, 237, 239, 240, 241,
      242, 244, 245, 246, 247, 247, 248, 249, 250, 250, 251, 251,
      251, 252, 252, 252, 252, 252, 252, 252, 251, 251, 251, 250,
      250, 249, 248, 247, 247, 246, 245, 244, 242, 241, 240, 239,
      237, 236, 234, 233, 231, 229, 227, 226, 224, 222, 220, 218,
      215, 213, 211, 209, 206, 204, 201, 199, 196, 194, 191, 189,
      186, 183, 180, 178, 175, 172, 169, 166, 163, 160, 157, 154,
      151, 148, 145, 142, 139, 136, 133, 130, 127, 124, 121, 118,
      115, 112, 109, 106, 103, 100,  97,  94,  91,  88,  85,  82,
       79,  76,  74,  71,  68,  65,  63,  60,  58,  55,  53,  50,
       48,  45,  43,  41,  39,  36,  34,  32,  30,  28,  27,  25, 
       23,  21,  20,  18,  17,  15,  14,  13,  12,  10,   9,   8,
        7,   7,   6,   5,   4,   4,   3,   3,   3,   2,   2,   2,
        2,   2,   2,   2,   3,   3,   3,   4,   4,   5,   6,   7,
        7,   8,   9,  10,  12,  13,  14,  15,  17,  18,  20,  21,
       23,  25,  27,  28,  30,  32,  34,  36,  39,  41,  43,  45,
       48,  50,  53,  55,  58,  60,  63,  65,  68,  71,  74,  76,
       79,  82,  85,  88,  91,  94,  97, 100, 103, 106, 109, 112,
      115, 118, 121, 124 };

#if 0
static short sin16[] = {
         0,    784,   1569,   2352,   3134,   3914,   4692,   5467,
      6239,   7007,   7770,   8529,   9283,  10031,  10774,  11509,
     12238,  12960,  13673,  14379,  15075,  15763,  16441,  17109,
     17767,  18414,  19051,  19675,  20288,  20889,  21477,  22052,
     22613,  23162,  23696,  24216,  24721,  25212,  25687,  26147,
     26591,  27019,  27431,  27826,  28204,  28566,  28910,  29237,
     29546,  29838,  30111,  30366,  30603,  30822,  31022,  31203,
     31366,  31510,  31634,  31740,  31827,  31894,  31942,  31971,
     31981,  31971,  31942,  31894,  31827,  31740,  31634,  31510,
     31366,  31203,  31022,  30822,  30603,  30366,  30111,  29838,
     29546,  29237,  28910,  28566,  28204,  27826,  27431,  27019,
     26591,  26147,  25687,  25212,  24721,  24216,  23696,  23162,
     22613,  22052,  21477,  20889,  20288,  19675,  19051,  18414,
     17767,  17109,  16441,  15763,  15075,  14379,  13673,  12960,
     12238,  11509,  10774,  10031,   9283,   8529,   7770,   7007,
      6239,   5467,   4692,   3914,   3134,   2352,   1569,    784,
         0,   -784,  -1569,  -2352,  -3134,  -3914,  -4692,  -5467,
     -6239,  -7007,  -7770,  -8529,  -9283, -10031, -10774, -11509,
    -12238, -12960, -13673, -14379, -15075, -15763, -16441, -17109,
    -17767, -18414, -19051, -19675, -20288, -20889, -21477, -22052,
    -22613, -23162, -23696, -24216, -24721, -25212, -25687, -26147,
    -26591, -27019, -27431, -27826, -28204, -28566, -28910, -29237,
    -29546, -29838, -30111, -30366, -30603, -30822, -31022, -31203,
    -31366, -31510, -31634, -31740, -31827, -31894, -31942, -31971,
    -31981, -31971, -31942, -31894, -31827, -31740, -31634, -31510,
    -31366, -31203, -31022, -30822, -30603, -30366, -30111, -29838,
    -29546, -29237, -28910, -28566, -28204, -27826, -27431, -27019,
    -26591, -26147, -25687, -25212, -24721, -24216, -23696, -23162,
    -22613, -22052, -21477, -20889, -20288, -19675, -19051, -18414,
    -17767, -17109, -16441, -15763, -15075, -14379, -13673, -12960,
    -12238, -11509, -10774, -10031,  -9283,  -8529,  -7770,  -7007, 
     -6239,  -5467,  -4692,  -3914,  -3134,  -2352,  -1569,   -784 };
#endif

mdc_encoder_t * mdc_encoder_new(int sampleRate)
{
	mdc_encoder_t *encoder;

	encoder = (mdc_encoder_t *)malloc(sizeof(mdc_encoder_t));
	if(!encoder)
		return (mdc_encoder_t *) 0L;

	encoder->incr = (1200.0 * TWOPI) / ((double)sampleRate);
	encoder->loaded = 0;

	return encoder;
}


#ifndef	_MDC_DECODE_H_

static unsigned long _flip(unsigned long crc, int bitnum)
{
        unsigned long i, j=1, crcout=0;

        for (i=1<<(bitnum-1); i; i>>=1)
        {
                if (crc & i)
                         crcout |= j;
                j<<= 1;
        }
        return (crcout);
}

static unsigned long docrc(unsigned char* p, int len) {

        int i, j, c;
        unsigned long bit;
        unsigned long crc = 0x0000;

        for (i=0; i<len; i++)
        {
                c = (unsigned long)*p++;
                c = _flip(c, 8);

                for (j=0x80; j; j>>=1)
                {
                        bit = crc & 0x8000;
                        crc<<= 1;
                        if (c & j)
                                bit^= 0x8000;
                        if (bit)
                                crc^= 0x1021;
                }
        }       

        crc = _flip(crc, 16);
        crc ^= 0xffff;
        crc &= 0xFFFF;;

        return(crc);
}

#endif

static unsigned char * _enc_leader(unsigned char *data)
{
	data[0] = 0x55;
	data[1] = 0x55;
	data[2] = 0x55;
	data[3] = 0x55;
	data[4] = 0x55;
	data[5] = 0x55;
	data[6] = 0x55;

	data[7] = 0x07;
	data[8] = 0x09;
	data[9] = 0x2a;
	data[10] = 0x44;
	data[11] = 0x6f;

	return &(data[12]);
}

static unsigned char * _enc_str(unsigned char *data)
{
	unsigned long ccrc;
	int i, j;
	int k;
	int m;
	int csr[7];
	int b;
	int lbits[112];

	ccrc = docrc(data, 4);

	data[4] = ccrc & 0x00ff;
	data[5] = (ccrc >> 8) & 0x00ff;

	data[6] = 0; 

	for(i=0; i<7; i++)
		csr[i] = 0;

	for(i=0; i<7; i++)
	{
		data[i+7] = 0;
		for(j=0; j<=7; j++)
		{
			for(k=6; k > 0; k--)
				csr[k] = csr[k-1];
			csr[0] = (data[i] >> j) & 0x01;
			b = csr[0] + csr[2] + csr[5] + csr[6];
			data[i+7] |= (b & 0x01) << j;
		}
	}

#if 0
	for(i=0; i<14; i++)
	{
		printf("%02x ",data[i]);
	}
	printf("\n");
#endif

	k=0;
	m=0;
	for(i=0; i<14; i++)
	{
		for(j=0; j<=7; j++)
		{
			b = 0x01 & (data[i] >> j);
			lbits[k] = b;
			k += 16; 
			if(k > 111)
				k = ++m;
		}
	}

	k = 0;
	for(i=0; i<14; i++)
	{
		data[i] = 0;
		for(j=7; j>=0; j--)
		{
			if(lbits[k])
				data[i] |= 1<<j;
			++k;
		}
	}


	return &(data[14]);
}

int mdc_encoder_set_packet(mdc_encoder_t *encoder,
                           unsigned char op,
			   unsigned char arg,
			   unsigned short unitID)
{
	unsigned char *dp;


	if(!encoder)
		return -1;

	if(encoder->loaded)
		return -1;

	encoder->state = 0;

	dp = _enc_leader(encoder->data);

	dp[0] = op;
	dp[1] = arg;
	dp[2] = (unitID >> 8) & 0x00ff;
	dp[3] = unitID & 0x00ff;

	_enc_str(dp);

	encoder->loaded = 26;

	return 0;
}

int mdc_encoder_set_double_packet(mdc_encoder_t *encoder,
                                  unsigned char op,
				  unsigned char arg,
				  unsigned short unitID,
				  unsigned char extra0,
				  unsigned char extra1,
				  unsigned char extra2,
				  unsigned char extra3)
{
	unsigned char *dp;

	if(!encoder)
		return -1;

	if(encoder->loaded)
		return -1;

	encoder->state = 0;

	dp = _enc_leader(encoder->data);

	dp[0] = op;
	dp[1] = arg;
	dp[2] = (unitID >> 8) & 0x00ff;
	dp[3] = unitID & 0x00ff;

	dp = _enc_str(dp);

	dp[0] = extra0;
	dp[1] = extra1;
	dp[2] = extra2;
	dp[3] = extra3;

	_enc_str(dp);

	encoder->loaded = 40;

	return 0;
}

static unsigned char _enc_get_samp(mdc_encoder_t *encoder)
{
	int b;
	int ofs;

	encoder->th += encoder->incr;

	if(encoder->th >= TWOPI)
	{
		encoder->th -= TWOPI;
		encoder->ipos++;
		if(encoder->ipos > 7)
		{
			encoder->ipos = 0;
			encoder->bpos++;
			if(encoder->bpos > encoder->loaded)
			{
				encoder->state = 0;
				return 127;
			}
		}

		b = 0x01 & (encoder->data[encoder->bpos] >> (7-(encoder->ipos)));

		if(b != encoder->lb)
		{
			encoder->xorb = 1;
			encoder->lb = b;
		}
		else
			encoder->xorb = 0;
	}

	if(encoder->xorb)
		encoder->tth += 1.5 * encoder->incr;
	else
		encoder->tth += 1.0 * encoder->incr;

	if(encoder->tth >= TWOPI)
		encoder->tth -= TWOPI;

	ofs = (int)(encoder->tth * (256.0 / TWOPI));

	return sin8[ofs];
}

int mdc_encoder_get_samples(mdc_encoder_t *encoder,
                            unsigned char *buffer,
			    int bufferSize)
{
	int i;

	if(!encoder)
		return -1;

	if(!(encoder->loaded))
		return 0;

	if(encoder->state == 0)
	{
		encoder->th = 0.0;
		encoder->tth = 0.0;
		encoder->bpos = 0;
		encoder->ipos = 0;
		encoder->state = 1;
		encoder->xorb = 1;
		encoder->lb = 0;
	}

	i = 0;
	while((i < bufferSize) && encoder->state)
		buffer[i++] = _enc_get_samp(encoder);

	if(encoder->state == 0)
		encoder->loaded = 0;
	return i;
}

