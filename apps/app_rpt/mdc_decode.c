/*-
 * mdc_decode.c
 *   Decodes a specific format of 1200 BPS MSK data burst
 *   from input audio samples.
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

#include <stdlib.h>
#include "allstar/mdc_decode.h"

mdc_decoder_t * mdc_decoder_new(int sampleRate)
{
	mdc_decoder_t *decoder;
	int i;

	decoder = (mdc_decoder_t *)malloc(sizeof(mdc_decoder_t));
	if(!decoder)
		return (mdc_decoder_t *) 0L;

	decoder->hyst = 3;
	decoder->incr = (1200.0 * TWOPI) / ((double)sampleRate);
	decoder->good = 0;
	decoder->level = 0;


	for(i=0; i<MDC_ND; i++)
	{
		decoder->th[i] = 0.0 + ( ((double)i) * (TWOPI/(double)MDC_ND));
		decoder->zc[i] = 0;
		decoder->xorb[i] = 0;
		decoder->shstate[i] = 0;
		decoder->shcount[i] = 0;
	}

	return decoder;
}

static void _clearbits(mdc_decoder_t *decoder, int x)
{
	int i;
	for(i=0; i<112; i++)
		decoder->bits[x][i] = 0;
}

static unsigned int _flip(unsigned int crc, int bitnum)
{
	unsigned int i, j=1, crcout=0;

	for (i=1<<(bitnum-1); i; i>>=1)
	{
		if (crc & i)
			 crcout |= j;
		j<<= 1;
	}
	return (crcout);
}

static unsigned int docrc(unsigned char* p, int len) {

	int i, j;
	unsigned int c;
	unsigned int bit;
	unsigned int crc = 0x0000;

	for (i=0; i<len; i++)
	{
		c = (unsigned int)*p++;
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


static void _procbits(mdc_decoder_t *decoder, int x)
{
	int lbits[112];
	int lbc = 0;
	int i, j, k;
	unsigned char data[14];
	unsigned int ccrc;
	unsigned int rcrc;

	for(i=0; i<16; i++)
	{
		for(j=0; j<7; j++)
		{
			k = (j*16) + i;
			lbits[lbc] = decoder->bits[x][k];
			++lbc;
		}
	}

	for(i=0; i<14; i++)
	{
		data[i] = 0;
		for(j=0; j<8; j++)
		{
			k = (i*8)+j;

			if(lbits[k])
				data[i] |= 1<<j;
		}
	}


	ccrc = docrc(data, 4);
	rcrc = data[5] << 8 | data[4];

	if(ccrc == rcrc)
	{

		if(decoder->shstate[x] == 2)
		{
			decoder->extra0 = data[0];
			decoder->extra1 = data[1];
			decoder->extra2 = data[2];
			decoder->extra3 = data[3];

			for(k=0; k<MDC_ND; k++)
				decoder->shstate[k] = 0;

			decoder->good = 2;
		}
		else
		{
			decoder->good = 1;
			decoder->op = data[0];
			decoder->arg = data[1];
			decoder->unitID = (data[2] << 8) | data[3];
			decoder->crc = (data[4] << 8) | data[5];
	
			for(k=0; k<MDC_ND; k++)
				decoder->shstate[k] = 0;

			switch(data[0])
			{
			/* list of opcode that mean 'double packet' */
			case 0x35:
			case 0x55:
				decoder->good = 0;
				decoder->shstate[x] = 2;
				decoder->shcount[x] = 0;
				_clearbits(decoder, x);
				break;
			default:
				break;
			}
		}

	}
	else
	{
#if 0
		printf("bad: ");
		for(i=0; i<14; i++)
			printf("%02x ",data[i]);
		printf("%x\n",ccrc);
#endif

		decoder->shstate[x] = 0;
	}
}


static int _onebits(unsigned int n)
{
	int i=0;
	while(n)
	{
		++i;
		n &= (n-1);
	}
	return i;
}

static void _shiftin(mdc_decoder_t *decoder, int x)
{
	int bit = decoder->xorb[x];
	int gcount;

	switch(decoder->shstate[x])
	{
	case 0:
		decoder->synchigh[x] <<= 1;
		if(decoder->synclow[x] & 0x80000000)
			decoder->synchigh[x] |= 1;
		decoder->synclow[x] <<= 1;
		if(bit)
			decoder->synclow[x] |= 1;

		gcount = _onebits(0x000000ff & (0x00000007 ^ decoder->synchigh[x]));
		gcount += _onebits(0x092a446f ^ decoder->synclow[x]);

		if(gcount <= MDC_GDTHRESH)
		{
 // printf("sync %d  %x %x \n",gcount,decoder->synchigh[x], decoder->synclow[x]);
			decoder->shstate[x] = 1;
			decoder->shcount[x] = 0;
			_clearbits(decoder, x);
		}
		else if(gcount >= (40 - MDC_GDTHRESH))
		{
 // printf("isync %d\n",gcount);
			decoder->shstate[x] = 1;
			decoder->shcount[x] = 0;
			decoder->xorb[x] = !(decoder->xorb[x]);
			_clearbits(decoder, x);
		}
		return;
	case 1:
	case 2:
		decoder->bits[x][decoder->shcount[x]] = bit;
		decoder->shcount[x]++;
		if(decoder->shcount[x] > 111)
		{
			_procbits(decoder, x);
		}
		return;

	default:
		return;
	}
}

static void _zcproc(mdc_decoder_t *decoder, int x)
{
	switch(decoder->zc[x])
	{
	case 2:
	case 4:
		break;
	case 3:
		decoder->xorb[x] = !(decoder->xorb[x]);
		break;
	default:
		return;
	}

	_shiftin(decoder, x);
}

int mdc_decoder_process_samples(mdc_decoder_t *decoder,
                                unsigned char *samples,
                                int numSamples)
{
	int i;
	int j;
	int k;
	int v;
	int d;
	unsigned char s;

	if(!decoder)
		return -1;

	for(i = 0; i<numSamples; i++)
	{
		s = samples[i];

#ifdef DIFFERENTIATOR
		v = (int) s;

		d = v- decoder->lastv;
		decoder->lastv = v;

		if(decoder->level == 0)
		{
			if(d > decoder->hyst)
			{
				for(k=0; k<MDC_ND; k++)
					decoder->zc[k]++;
				decoder->level = 1;
			}
		}
		else
		{
			if(d < (-1 * decoder->hyst))
			{
				for(k=0; k<MDC_ND; k++)
					decoder->zc[k]++;
				decoder->level = 0;
			}
		}
#else
		if(decoder->level == 0)
		{
			if(s > 128 + decoder->hyst)
			{
				for(k=0; k<MDC_ND; k++)
					decoder->zc[k]++;
				decoder->level = 1;
			}
		}
		else
		{
			if(s < 127 - decoder->hyst)
			{
				for(k=0; k<MDC_ND; k++)
					decoder->zc[k]++;
				decoder->level = 0;
			}
		}
#endif
		

		for(j=0; j<MDC_ND; j++)
		{
			decoder->th[j] += decoder->incr;
			if(decoder->th[j] >= TWOPI)
			{
				_zcproc(decoder, j);
				decoder->th[j] -= TWOPI;
				decoder->zc[j] = 0;
			}
		}
	}

	if(decoder->good)
		return decoder->good;

	return 0;
}

int mdc_decoder_get_packet(mdc_decoder_t *decoder, 
                           unsigned char *op,
			   unsigned char *arg,
			   unsigned short *unitID)
{
	if(!decoder)
		return -1;

	if(decoder->good != 1)
		return -1;

	if(op)
		*op = decoder->op;

	if(arg)
		*arg = decoder->arg;

	if(unitID)
		*unitID = decoder->unitID;

	decoder->good = 0;

	return 0;
}

int mdc_decoder_get_double_packet(mdc_decoder_t *decoder, 
                           unsigned char *op,
			   unsigned char *arg,
			   unsigned short *unitID,
                           unsigned char *extra0,
                           unsigned char *extra1,
                           unsigned char *extra2,
                           unsigned char *extra3)
{
	if(!decoder)
		return -1;

	if(decoder->good != 2)
		return -1;

	if(op)
		*op = decoder->op;

	if(arg)
		*arg = decoder->arg;

	if(unitID)
		*unitID = decoder->unitID;

	if(extra0)
		*extra0 = decoder->extra0;
	if(extra1)
		*extra1 = decoder->extra1;
	if(extra2)
		*extra2 = decoder->extra2;
	if(extra3)
		*extra3 = decoder->extra3;

	decoder->good = 0;

	return 0;
}

