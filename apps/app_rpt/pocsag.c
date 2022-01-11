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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include "allstar/pocsag.h"

/* String values for numeric paging */
static char nstr[] = "0123456789*U -()";

/* add all the extra check and parity stuff */
static uint32_t do_parity_stuff(uint32_t codeword)
{
int i,p,myword;

	myword = codeword; 
	for(i = 1; i <= 21; i++,codeword <<= 1)
	{
		if (codeword & 0x80000000) codeword ^= 0xED200000;
	}
	myword |= (codeword >> 21);
	codeword = myword;
	for(i = 1,p = 0; i <= 32; i++,codeword <<= 1)
	{
		if (codeword & 0x80000000) p++;
	}
	return myword + (p & 1);
}

/* pack a string into pocsag codewords */
static int pack_pogsag_string(uint32_t *packed, char *str, int size, int type)
{
int mysize,idx,pi,x,n,i,j;
uint32_t acc;

	switch (type)
	{
	    case TONE:
		return 0;
	    case NUMERIC: 
		if ((size % 5) == 0) mysize = size;
		else mysize = ((size / 5) + 1) * 5;
		for(idx = 0, pi = 0; idx < mysize; idx++, pi = ((idx % 5)? pi : pi + 1))
		{
		        /* convert from ASCII string */
		        for(i = 0; nstr[i]; i++) if (nstr[i] == str[idx]) break;
			/* if not found, map to space (0xc) */
			if (!nstr[i]) i = 0xc;
			/* reverse bits */
			x = (i & 1) << 3;
			x += (i & 2) << 1;
			x += (i & 4) >> 1;
			x += (i & 8) >> 3;
			if (idx<size)
				packed[pi] = (packed[pi]<<4) + x;
			else
				packed[pi] = (packed[pi]<<4) + 3;
		}
		return(mysize / 5);
	    case ALPHA:
		/* first, pack string into codewords */
		for(n = 19, acc = 0, pi = 0,idx = 0; str[idx]; idx++)
		{
			for(j = 0; j < 7; j++)
			{
				if (str[idx] & (1 << j))
					acc |= (1 << n);
				if (n-- <= 0)
				{
					packed[pi++] = acc;
					acc = 0;
					n = 19;
				}
			}
		}
		/* Then add EOT character */
		i = 4;
		for(j = 0; j < 7; j++)
		{
			if (i & (1 << j))
				acc |= (1 << n);
			if (n-- <= 0)
			{
				packed[pi++] = acc;
				acc = 0;
				n = 19;
			}
		}
		/* If necessary, pad with more EOT characters until done */
		i = 4;
		while(n >= 0)
		{
			for(j = 0; j < 7; j++)
			{
				if (i & (1 << j))
					acc |= (1 << n);
				if (n-- <= 0) break;
			}
		}
		packed[pi++] = acc;
		return(pi);
	    default:
		return 0;
	}
	return 0;
}

/* make and fill a POCSAG paging batch */
struct pocsag_batch *make_pocsag_batch(uint32_t ric,char *data, 
	int size_of_data,int type,int toneno)
{
	struct pocsag_batch *cur,*old;
	int i,ii,j,k,curaddr,mylen;
	uint32_t packed[100];

	if ((cur = malloc(sizeof(struct pocsag_batch))) == NULL)
		return NULL;
	memset(cur,0,sizeof(struct pocsag_batch));
	cur->sc = SYNCH;
	cur->next = NULL;

	for (i = 0; i < 8; i++)
	{
		for (j = 0; j < 2; j++)
		{
		      cur->frame[i][j] = IDLE;
		}
	}
	old = cur;  /* Pointer to first batch in the row */

	curaddr = ric & 7;
	cur->frame[curaddr][0] = 0;
	cur->frame[curaddr][0] = (ric >> 3) << 13;

	cur->frame[curaddr][0] &= 0xFFFFE700;
	switch(toneno)
	{
	    case 0:
		break;
	    case 1:
		cur->frame[curaddr][0] |= 0x00000800;
		break;
	    case 2:
		cur->frame[curaddr][0] |= 0x00001000;
		break;
	    case 3:
		cur->frame[curaddr][0] |= 0x00001800;
		break;
	    default:
		return NULL;
	}

	cur->frame[curaddr][0] = do_parity_stuff( cur->frame[curaddr][0] );

	if (type != TONE)
	{
		mylen = pack_pogsag_string(packed, data, size_of_data, type);
		for(i = 0, k = 1, j = curaddr; i < mylen; k=0, j++)
		{
			for(; k <= 1; k++, i++)
			{
				if (i == mylen) break;
				if (j == 8)
				{
					if ((cur->next = malloc(sizeof(struct pocsag_batch))) == NULL)
						return NULL;
					memset(cur->next,0,sizeof(struct pocsag_batch));
					cur->next->sc   = SYNCH;
					cur->next->next = NULL;
					for (ii = 0; ii < 8; ii++)
					{
						for (j = 0; j < 2; j++)
						{
							cur->next->frame[ii][j] = IDLE;
						}
					}
					j = k = 0;
					cur = cur->next;
				}
				cur->frame[j][k] = packed[i];
				cur->frame[j][k] <<= 11;
				cur->frame[j][k] |= 0x80000000;
				cur->frame[j][k] = do_parity_stuff(cur->frame[j][k]); 
			}
		}
	}
	return(old);
}

void free_batch(struct pocsag_batch *batch)
{
	if (batch != NULL)
	{
		free_batch(batch->next);
		free(batch);
	}
	return;
}

