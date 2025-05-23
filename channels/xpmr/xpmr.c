/*
 * xpmr.c - Xelatec Private Mobile Radio Processes
 *
 * All Rights Reserved. Copyright (C)2007-2009, Xelatec, LLC
 *
 * 20070808 1235 Steven Henke, W9SH, sph@xelatec.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * This version may be optionally licenced under the GNU LGPL licence.
 *
 * A license has been granted to Digium (via disclaimer) for the use of
 * this code.

 * A license has been granted to Digium (via disclaimer) for the use of
 * this code.
 *
 * 20160829	inad	added rxlpf rxhpf txlpf txhpf
 * 20161024	inad	fixed set the number of coefficients
 * 20161027	WN3A    allow filters of different tap counts
 * 20090725 2039 sph@xelatec.com improved rxfrontend and squelch
  */

/*! \file
 *
 * \brief Private Land Mobile Radio Channel Voice and Signaling Processor
 *
 * \author Steven Henke, W9SH <sph@xelatec.com> Xelatec, LLC
 */
/*
	FYI 	= For Your Information
	PMR 	= Private Mobile Radio
	RX  	= Receive
	TX  	= Transmit
	CTCSS	= Continuous Tone Coded Squelch System
	TONE	= Same as above.
	LSD 	= Low Speed Data, subaudible signaling. May be tones or codes.
	VOX 	= Voice Operated Transmit
	DSP 	= Digital Signal Processing
	LPF 	= Low Pass Filter
	FIR 	= Finite Impulse Response (Filter)
	IIR 	= Infinite Impulse Response (Filter)
*/

// XPMR_FILE_VERSION(__FILE__, "$Revision: 491 $")

#define GCC_VERSION (__GNUC__ * 10000 \
                               + __GNUC_MINOR__ * 100 \
                               + __GNUC_PATCHLEVEL__)
#if GCC_VERSION > 40600
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsequence-point"
#endif

#define N_FMT(duf) "%30" #duf /* Maximum sscanf conversion to numeric strings */
#include "asterisk.h"

#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdlib.h>
#include <errno.h>
   
#include "xpmr.h"
#include "xpmr_coef.h"
#include "sinetabx.h"

static i16 pmrChanIndex=0;	 			// count of created pmr instances
//static i16 pmrSpsIndex=0;

#if (DTX_PROG == 1) || 	XPMR_PPTP == 1
static int ppdrvdev=0;
#endif

/*
	Trace Routines
*/
void strace(i16 point, t_sdbg *sdbg, i16 index, i16 value)
{
	// make dbg_trace buffer in structure
	if(!sdbg->mode || sdbg->point[point]<0){
		return;
    } else {
		sdbg->buffer[(index*XPMR_DEBUG_CHANS) + sdbg->point[point]] = value;
	}
}
/*

*/
void strace2(t_sdbg *sdbg)
{
	int i;
	for(i=0;i<XPMR_DEBUG_CHANS;i++)
	{
		if(sdbg->source[i])
		{
			int ii;
			for(ii=0;ii<SAMPLES_PER_BLOCK;ii++)
			{
				sdbg->buffer[ii*XPMR_DEBUG_CHANS + i] = sdbg->source[i][ii];
		    }
		}
	}
}
#if XPMR_PPTP == 1
/*
	Hardware Trace Signals via the PC Parallel Port
*/
void pptp_init(void)
{
	if (ppdrvdev == 0) {
		ppdrvdev = open("/dev/ppdrv_device", 0);
	}
	if (ppdrvdev < 0) {
		ast_log(LOG_ERROR, "open /dev/ppdrv_ppdrvdev returned %i\n", ppdrvdev);
		exit(0);
	}
	ioctl(ppdrvdev, PPDRV_IOC_PINMODE_OUT, DTX_CLK | DTX_DATA | DTX_ENABLE | DTX_TXPWR | DTX_TX | DTX_TP1 | DTX_TP2);
	ioctl(ppdrvdev, PPDRV_IOC_PINCLEAR,    DTX_CLK | DTX_DATA | DTX_ENABLE | DTX_TXPWR | DTX_TX | DTX_TP1 | DTX_TP2);
}
/*
*/
void	pptp_write(i16 bit, i16 state)
{
	if(bit==0)
	{
		if(state)ioctl(ppdrvdev,PPDRV_IOC_PINSET,DTX_TP1);
		else ioctl(ppdrvdev,PPDRV_IOC_PINCLEAR,DTX_TP1);
	}
	else
	{
		if(state)ioctl(ppdrvdev,PPDRV_IOC_PINSET,DTX_TP2);
		else ioctl(ppdrvdev,PPDRV_IOC_PINCLEAR,DTX_TP2);
	}
}
#endif
/*
	take source string allocate and copy
	copy is modified, delimiters are replaced with zeros to mark
	end of string
	count set pointers
	string_parse( char *src, char *dest, char **sub)
*/
i16 string_parse(char *src, char **dest, char ***ptrs)
{
	char *p,*pd;
	char *ptstr[1000];
	i16 i, slen, numsub;

	TRACEJ(2, "string_parse(%s)\n", src);

	slen=strlen(src);
	TRACEJ(2, " source len = %i\n", slen);

	pd=*dest;
	if(pd) ast_free(pd);
    pd=ast_calloc(slen+1,1);
	memcpy(pd,src,slen);
	*dest=pd;

	p=0;
	numsub=0;
	for(i=0;i<slen+1;i++)
	{
		TRACEJ(5, " pd[%i] = %c\n", i, pd[i]);

		if( p==0 && pd[i]!=',' && pd[i]!=' ' )
		{
			p=&(pd[i]);	
		}
		else if(pd[i]==',' || pd[i]==0 )
		{
			ptstr[numsub]=p;
			pd[i]=0;
			p=0;
			numsub++;
		}
	}

	for(i=0;i<numsub;i++)
	{
		TRACEJ(5, " ptstr[%i] = %p %s\n", i, ptstr[i], ptstr[i]);
	}

	if(*ptrs)ast_free(*ptrs);
	*ptrs=ast_calloc(numsub, sizeof(char*));
	for(i=0;i<numsub;i++)
	{
		(*ptrs)[i]=ptstr[i];
		TRACEJ(5, " %i = %s\n", i, (*ptrs)[i]);
	}
	TRACEJ(5, "string_parse()=%i\n\n", numsub);

	return numsub;
}
/*
	the parent program defines
	pRxCodeSrc and pTxCodeSrc string pointers to the list of codes
	pTxCodeDefault the default Tx Code.

*/
i16 code_string_parse(t_pmr_chan *pChan)
{
	i16 i, ii, hit, ti;
	char *p;
	float f, maxctcsstxfreq;

	t_pmr_sps  	*pSps;
	i16	maxctcssindex;

	TRACEF(1, "code_string_parse(%i)\n", 0);
	TRACEF(1, "pChan->pRxCodeSrc %s \n", pChan->pRxCodeSrc);
	TRACEF(1, "pChan->pTxCodeSrc %s \n", pChan->pTxCodeSrc);
	TRACEF(1, "pChan->pTxCodeDefault %s \n", pChan->pTxCodeDefault);

	//printf("code_string_parse() %s / %s / %s / %s \n",pChan->name, pChan->pTxCodeDefault,pChan->pTxCodeSrc,pChan->pRxCodeSrc);

 	maxctcssindex=CTCSS_NULL;
	maxctcsstxfreq=CTCSS_NULL;
	pChan->txctcssdefault_index=CTCSS_NULL;
	pChan->txctcssdefault_value=CTCSS_NULL;

	pChan->b.ctcssRxEnable=pChan->b.ctcssTxEnable=0;
	pChan->b.dcsRxEnable=pChan->b.dcsTxEnable=0;
	pChan->b.lmrRxEnable=pChan->b.lmrTxEnable=0;
	pChan->b.mdcRxEnable=pChan->b.mdcTxEnable=0;
	pChan->b.dstRxEnable=pChan->b.dstTxEnable=0;
	pChan->b.p25RxEnable=pChan->b.p25TxEnable=0;

	if(pChan->spsLsdGen){
		pChan->spsLsdGen->enabled=0;
		pChan->spsLsdGen->state=0;
	}

	TRACEF(1, "code_string_parse(%i) 05\n", 0);

	pChan->numrxcodes = string_parse( pChan->pRxCodeSrc, &(pChan->pRxCodeStr), &(pChan->pRxCode));
	pChan->numtxcodes = string_parse( pChan->pTxCodeSrc, &(pChan->pTxCodeStr), &(pChan->pTxCode));

	if (pChan->numrxcodes != pChan->numtxcodes) {
		ast_log(LOG_ERROR, "numrxcodes != numtxcodes \n");
	}
	pChan->rxCtcss->enabled=0;
	pChan->rxCtcss->gain=1*M_Q8;
	pChan->rxCtcss->limit=8192;
	pChan->rxCtcss->input=pChan->pRxLsdLimit;
	pChan->rxCtcss->decode=CTCSS_NULL;

	pChan->rxCtcss->testIndex=0;
	if(!pChan->rxCtcss->testIndex)pChan->rxCtcss->testIndex=3;

	pChan->rxctcssfreq[0]=0;	// decode now	CTCSS_RXONLY

	for(i=0;i<CTCSS_NUM_CODES;i++)
	{
		pChan->rxctcss[i]=0;
		pChan->txctcss[i]=0;
		pChan->rxCtcssMap[i]=CTCSS_NULL;
	}

	TRACEF(1, "code_string_parse(%i) 10\n", 0);

#ifdef XPMRX_H
	xpmrx(pChan,XXO_LSDCODEPARSE);
	#endif

	// Do Receive Codes String
	for(i=0;i<pChan->numrxcodes;i++)
	{
		i16 ii,ri,ti;
		float f;

		p = pChan->pStr = pChan->pRxCode[i];

#ifdef HAVE_XPMRX
		if (!xpmrx(pChan, XXO_LSDCODEPARSE_1))
#endif
		{
			sscanf(p, N_FMT(f), &f);
			ri = CtcssFreqIndex(f);
			if (ri == CTCSS_NULL) {
				ast_log(LOG_ERROR, "Invalid RX CTCSS code detected and ignored. %i %s\n", i, pChan->pRxCode[i]);

			} else if (ri > maxctcssindex) {
				maxctcssindex = ri;
			}

			if (i < pChan->numtxcodes) { /* more rx codes than tx codes */
				sscanf(pChan->pTxCode[i], N_FMT(f), &f);
				ti = CtcssFreqIndex(f);
				if (ti == CTCSS_NULL) {
					if (f != 0.0) {
						f = -1.0; /* tone freq not valid */
						ast_log(LOG_ERROR, "Invalid TX CTCSS code detected and ignored. %i %s\n", i, pChan->pTxCode[i]);
					}
				} else if (f > maxctcsstxfreq) {
					maxctcsstxfreq = f;
				}
			} else {
				ti = CTCSS_NULL;
				f = -1.0; /* tone freq not provided */
				ast_log(LOG_ERROR, "Invalid CTCSS configuration. Number of rx codes > number of tx codes\n");
			}

			if (ri > CTCSS_NULL && ti > CTCSS_NULL) {
				pChan->b.ctcssRxEnable = 1;
				pChan->b.ctcssTxEnable = 1;
				pChan->rxCtcssMap[ri] = ti;
				pChan->numrxctcssfreqs++;
				TRACEF(1, "pChan->rxctcss[%i]=%s  pChan->rxCtcssMap[%i]=%i\n", i, pChan->rxctcss[i], ri, ti);
			} else if (ri > CTCSS_NULL && f == 0) {
				pChan->b.ctcssRxEnable=1;
				pChan->rxCtcssMap[ri] = CTCSS_RXONLY;
				pChan->numrxctcssfreqs++;
				TRACEF(1, "pChan->rxctcss[%i]=%s  pChan->rxCtcssMap[%i]=%i RXONLY\n", i, pChan->rxctcss[i], ri, ti);
			} else {
				pChan->numrxctcssfreqs = 0;
				ast_log(LOG_ERROR, "Invalid CTCSS configuration. CTCSS has been disabled\n");
				for (ii = 0; ii < CTCSS_NUM_CODES; ii++) {
					pChan->rxCtcssMap[ii] = CTCSS_NULL;
				}
			}
		}
	}

	TRACEF(1, "code_string_parse() CTCSS Init Struct  %i  %i\n", pChan->b.ctcssRxEnable, pChan->b.ctcssTxEnable);
	if(pChan->b.ctcssRxEnable)
	{
		pChan->rxHpfEnable=1;
		pChan->spsRxLsdNrz->enabled=pChan->rxCenterSlicerEnable=1;
		pChan->rxCtcssDecodeEnable=1;
		pChan->rxCtcss->enabled=1;
	}
	else
	{
		pChan->rxHpfEnable=1;
		pChan->spsRxLsdNrz->enabled=pChan->rxCenterSlicerEnable=0;
		pChan->rxCtcssDecodeEnable=0;
		pChan->rxCtcss->enabled=0;
	}

	TRACEF(1, "code_string_parse() CTCSS Init Decoders \n");
	for(i=0;i<CTCSS_NUM_CODES;i++)
	{
		t_tdet *ptdet;
		ptdet=&(pChan->rxCtcss->tdet[i]);
		ptdet->counterFactor=coef_ctcss_div[i];
		ptdet->state=1;
		ptdet->setpt=(M_Q15*0.041);		   			// 0.069
		ptdet->hyst =(M_Q15*0.0130);
		ptdet->binFactor=(M_Q15*0.135);			  	// was 0.140
		ptdet->fudgeFactor=8;
	}

	// DEFAULT TX CODE
	TRACEF(1, "code_string_parse() Default Tx Code %s \n", pChan->pTxCodeDefault);
	pChan->txcodedefaultsmode = SMODE_NULL;
	p = pChan->pStr = pChan->pTxCodeDefault;

#ifdef HAVE_XPMRX
	if (!lsd_code_parse(pChan, 3))
#endif
	{
		sscanf(p, N_FMT(f), &f);
		ti = CtcssFreqIndex(f);
		if (ti == CTCSS_NULL) {
			ast_log(LOG_ERROR, "Invalid default TX CTCSS code detected and ignored. %s\n", pChan->pTxCodeDefault);
		} else if (f > maxctcsstxfreq) {
			maxctcsstxfreq = f;
		}

		if (ti > CTCSS_NULL) {
			pChan->b.ctcssTxEnable = 1;
			pChan->txctcssdefault_index = ti;
			pChan->txctcssdefault_value = f;
			pChan->spsSigGen0->freq = f * 10;
			pChan->txcodedefaultsmode = SMODE_CTCSS;
			TRACEF(1, "code_string_parse() Tx Default CTCSS = %s %i %f\n", p, ti, f);
		}
	}

	// set x for maximum length and just change pointers
	TRACEF(1, "code_string_parse() Filter Config \n");
	pSps=pChan->spsTxLsdLpf;
	if (pSps->x) {
		ast_free(pSps->x);
	}
	if (maxctcsstxfreq > 203.5) {
		pSps->ncoef = taps_fir_lpf_250_9_66;
		pSps->size_coef = 2;
		pSps->coef = (void *) coef_fir_lpf_250_9_66;
		pSps->nx = taps_fir_lpf_250_9_66;
		pSps->size_x = 2;
		pSps->x = ast_calloc(pSps->nx, pSps->size_x);
		if (pSps->x == NULL) {
			; /* XXX do something here */
		}
		pSps->calcAdjust = gain_fir_lpf_250_9_66;
		TRACEF(1, "code_string_parse() Tx Filter Freq High\n");
	} else {
		pSps->ncoef=taps_fir_lpf_215_9_88;
		pSps->size_coef=2;
		pSps->coef=(void*)coef_fir_lpf_215_9_88;
		pSps->nx=taps_fir_lpf_215_9_88;
		pSps->size_x=2;
		pSps->x = ast_calloc(pSps->nx, pSps->size_x);
		if (pSps->x == NULL) {
			; /* XXX do something here */
		}
		pSps->calcAdjust=gain_fir_lpf_215_9_88;
		TRACEF(1, "code_string_parse() Tx Filter Freq Low\n");
	}

	// CTCSS Rx Decoder Low Pass Filter
	hit=0;
	ii=	CtcssFreqIndex(203.5);
	for(i=ii;i<CTCSS_NUM_CODES;i++)
	{
		if(pChan->rxCtcssMap[i]>CTCSS_NULL)hit=1;
	}

	pSps=pChan->spsRxLsd;
	if(pSps->x)ast_free(pSps->x);
	if(hit)
	{
		pSps->ncoef=taps_fir_lpf_250_9_66;
		pSps->size_coef=2;
		pSps->coef=(void*)coef_fir_lpf_250_9_66;
		pSps->nx=taps_fir_lpf_250_9_66;
		pSps->size_x=2;
		pSps->x = ast_calloc(pSps->nx, pSps->size_x);
		if (pSps->x == NULL) {
			; /* XXX do something here */
		}
		pSps->calcAdjust=gain_fir_lpf_250_9_66;
		TRACEF(1, "code_string_parse() Rx Filter Freq High\n");
	}
	else
	{
		pSps->ncoef=taps_fir_lpf_215_9_88;
		pSps->size_coef=2;
		pSps->coef=(void*)coef_fir_lpf_215_9_88;
		pSps->nx=taps_fir_lpf_215_9_88;
		pSps->size_x=2;
		pSps->x = ast_calloc(pSps->nx, pSps->size_x);
		if (pSps->x == NULL) {
			; /* XXX do something here */
		}
		pSps->calcAdjust=gain_fir_lpf_215_9_88;
		TRACEF(1, "code_string_parse() Rx Filter Freq Low\n");
	}

	if(pChan->b.ctcssRxEnable || pChan->b.dcsRxEnable || pChan->b.lmrRxEnable)
	{
		pChan->rxCenterSlicerEnable=1;
		pSps->enabled=1;
	}
	else
	{
		pChan->rxCenterSlicerEnable=0;
		pSps->enabled=0;
	}

	#if XPMR_DEBUG0 == 1
	TRACEF(2, "code_string_parse() ctcssRxEnable = %i \n", pChan->b.ctcssRxEnable);
	TRACEF(2, "                    ctcssTxEnable = %i \n", pChan->b.ctcssTxEnable);
	TRACEF(2, "                      dcsRxEnable = %i \n", pChan->b.dcsRxEnable);
	TRACEF(2, "                      lmrRxEnable = %i \n", pChan->b.lmrRxEnable);
	TRACEF(2, "               txcodedefaultsmode = %i \n", pChan->txcodedefaultsmode);
	for(i=0;i<CTCSS_NUM_CODES;i++)
	{
		TRACEF(2, "rxCtcssMap[%i] = %i \n", i, pChan->rxCtcssMap[i]);
	}
    #endif

	#ifdef HAVE_XPMRX
	lsd_code_parse(pChan,5);
	#endif

	TRACEF(1, "code_string_parse(%i) end\n", 0);

	return 0;
}
/*
	Convert a Frequency in Hz to a zero based CTCSS Table index
*/
i16 CtcssFreqIndex(float freq)
{
	i16 i,hit=CTCSS_NULL;

	for(i=0;i<CTCSS_NUM_CODES;i++){
		if(freq==freq_ctcss[i])hit=i;
	}
	return hit;
}

#ifndef HAVE_XPMRX_2
/*
	pmr_rx_frontend
	Takes a block of data and low pass filters it.
	Determines the amplitude of high frequency noise for carrier detect.
	Decimates input data to change the rate.
*/
i16 pmr_rx_frontend(t_pmr_sps *mySps)
{
	#define DCgainBpfNoise 	65536

	i16 samples,nx,iOutput, *input, *output, *noutput;
	i16 *x;
	i16 decimator, decimate, doNoise, fever, fev1;
	i32 i, naccum, outputGain, calcAdjust;
	i64 y, npwr;

	TRACEJ(5, "pmr_rx_frontend()\n");

	if(!mySps->enabled)return(1);

	decimator = mySps->decimator;
	decimate = mySps->decimate;

	input     = mySps->source;
	output    = mySps->sink;
	noutput   = mySps->parentChan->pRxNoise;
	fever   = mySps->parentChan->fever;

	nx        = mySps->nx;

	calcAdjust = mySps->calcAdjust;
	outputGain = mySps->outputGain;

	samples=mySps->nSamples*decimate;
	x=mySps->x;
	iOutput=0;
	npwr=0;

	if(mySps->parentChan->rxCdType!=CD_XPMR_VOX)doNoise=1;
	else doNoise=0;

	if (fever)
	    fev1 = (nx - 1) * 2;
	else
	    fev1 = nx - 1;

	for(i=0;i<samples;i++)
	{
		i16 n;

		//shift the old samples
        #if 0
	    for(n=nx-1; n>0; n--)
	       x[n] = x[n-1];
        #else
		memmove(x+1,x,fev1);
        #endif
	    x[0] = input[i*2];

#if	XPMR_TRACE_FRONTEND == 1
	    y=0;
	    for(n=0; n<nx; n++)
	        y +=  fir_rxlpf[mySps->parentChan->rxlpf].coefs[n] * x[n];

	    y=((y/calcAdjust)*outputGain)/M_Q8;
		input[i*2]=y;	 // debug output LowPass at 48KS/s
#endif

		if(doNoise)
		{
			// calculate noise filter output
			naccum=0;
			if(mySps->parentChan->rxNoiseFilType==0)
			{
			    for(n=0; n<taps_fir_bpf_noise_1; n++)
			        naccum += coef_fir_bpf_noise_1[n] * x[n];
			    naccum /= DCgainBpfNoise;
			}
			else
			{
			    for(n=0; n<taps_fir_bpf_noise_2; n++)
			        naccum += coef_fir_bpf_noise_2[n] * x[n];
			    naccum /= gain_fir_bpf_noise_2;
			}
#if	XPMR_TRACE_FRONTEND == 1
			input[i*2+1]=naccum;	 // output noise filter results
#endif
		    npwr+=naccum*naccum;
		}


		--decimator;

		if(decimator<=0)
		{
			decimator=decimate;

		    y=0;
		    for(n=0; n<nx; n++)
		        y += fir_rxlpf[mySps->parentChan->rxlpf].coefs[n] * x[n];

		    y=((y/calcAdjust)*outputGain)/M_Q8;

#if	XPMR_TRACE_OVFLW == 1
			if(y>32767)
			{
				y=32767;
				ast_log(LOG_ERROR, "pmr_rx_frontend() OVRFLW \n");
			}
			else if(y<-32767)
			{
				y=-32767;
				ast_log(LOG_ERROR, "pmr_rx_frontend() UNDFLW \n");
			}
#else
			if(y>32767)y=32767;
			else if(y<-32767)y=-32767;
#endif
		    output[iOutput++]=y;					// Rx Baseband decimated

		}  // if decimator
	}

	if(doNoise)
	{
		npwr=sqrt(npwr)/16;

		// compOut=Squelched
		if(mySps->blanking)mySps->blanking--;
		mySps->blanking=0;
		if( !mySps->compOut && 
		    ( (npwr>(mySps->setpt+mySps->hyst)) || 
		      ( (mySps->apeak<(mySps->setpt/4)) && (npwr>mySps->setpt) ) 
		    )
		   )
		{
			if(!mySps->compOut)
			{
				mySps->blanking=2;
				mySps->compOut=1;
			}
		}
        else if ((npwr<mySps->setpt)&&(!mySps->blanking))
		{
			mySps->compOut=0;
		}

		#if	XPMR_DEBUG0 == 1
		if(mySps->parentChan->tracetype)
		{
			for(i=0;i<mySps->nSamples;i++)
			{
				noutput[i] = npwr;
			}
		}
		#endif

		((t_pmr_chan *)(mySps->parentChan))->rxRssi=mySps->apeak=npwr; 
	}

	return 0;
}
#endif
/*
	pmr general purpose fir
	works on a block of samples
*/
i16 pmr_gp_fir(t_pmr_sps *mySps)
{
	i32 nsamples,inputGain,outputGain,calcAdjust;
	i16 *input, *output;
	i16 *x, *coef;
    i32 i, ii;
	i16 nx, hyst, setpt, compOut;
	i16 amax, amin, apeak=0, discounteru=0, discounterl=0, discfactor;
	i16 decimator, decimate, interpolate;
	i16 numChanOut, selChanOut, mixOut, monoOut;

	TRACEJ(5, "pmr_gp_fir() %i %i\n", mySps->index, mySps->enabled);

	if(!mySps->enabled)return(1);

	inputGain  = mySps->inputGain;
	calcAdjust = mySps->calcAdjust;
	outputGain = mySps->outputGain;

	input      = mySps->source;
	output     = mySps->sink;
	x          = mySps->x;
	nx         = mySps->nx;
	coef       = mySps->coef;

	decimator   = mySps->decimator;
	decimate 	= mySps->decimate;
	interpolate = mySps->interpolate;

	setpt	   = mySps->setpt;
	compOut    = mySps->compOut;

	inputGain  = mySps->inputGain;
	outputGain = mySps->outputGain;
	numChanOut = mySps->numChanOut;
	selChanOut = mySps->selChanOut;
	mixOut     = mySps->mixOut;
	monoOut    = mySps->monoOut;

	amax=mySps->amax;
	amin=mySps->amin;

	discfactor=mySps->discfactor;
	hyst=mySps->hyst;
	setpt=mySps->setpt;
	nsamples=mySps->nSamples;

	if(mySps->option==3)
	{
		mySps->option=0;
		mySps->enabled=0;
		for(i=0;i<nsamples;i++)
		{
			if(monoOut)
				output[(i*2)]=output[(i*2)+1]=0;
			else
				output[(i*numChanOut)+selChanOut]=0;
		}
		return 0;
	}

	ii=0;
	for(i=0;i<nsamples;i++)
	{
		int ix;

		int64_t y=0;

		if(decimate<0)
		{
			decimator=decimate;
		}

		for(ix=0;ix<interpolate;ix++)
		{
			i16 n;
			y=0;

			for(n=nx-1; n>0; n--)
				x[n] = x[n-1];
			x[0] = (input[i]*inputGain)/M_Q8;

			#if 0
			--decimator;
			if(decimator<=0)
			{
				decimator=decimate;
			    for(n=0; n<nx; n++)
			        y += coef[n] * x[n];
				y /= (outputGain*3);
				output[ii++]=y;
			}
		 	#else
			for(n=0; n<nx; n++)
		        	y += coef[n] * x[n];

		    	y=((y/calcAdjust)*outputGain)/M_Q8;

			if (y>32767)y=32767;		 			// overflow
			else if(y<-32767)y=-32767;

			if(mixOut){
				if(monoOut){
					output[(ii*2)]=output[(ii*2)+1]+=y;
				}
				else{
					output[(ii*numChanOut)+selChanOut]+=y;
				}
			}
			else{
				if(monoOut){
					output[(ii*2)]=output[(ii*2)+1]=y;
				}
				else{
					output[(ii*numChanOut)+selChanOut]=y;
				}
			}
			ii++;
		    #endif
		}

		// amplitude detector
		if(setpt)
		{
			i16 accum=y;

			if(accum>amax)
			{
				amax=accum;
				discounteru=discfactor;
			}
			else if(--discounteru<=0)
			{
				discounteru=discfactor;
				amax=(i32)((amax*32700)/32768);
			}

			if(accum<amin)
			{
				amin=accum;
				discounterl=discfactor;
			}
			else if(--discounterl<=0)
			{
				discounterl=discfactor;
				amin=(i32)((amin*32700)/32768);
			}

			apeak = (i32)(amax-amin)/2;

 			if(apeak>setpt)compOut=1;
			else if(compOut&&(apeak<(setpt-hyst)))compOut=0;
		}
	}

	mySps->decimator = decimator;

	mySps->amax=amax;
	mySps->amin=amin;
	mySps->apeak=apeak;
 	mySps->discounteru=discounteru;
	mySps->discounterl=discounterl;

	mySps->compOut=compOut;

	return 0;
}
/*
	general purpose integrator lpf
*/
i16 gp_inte_00(t_pmr_sps *mySps)
{
	i16 npoints;
 	i16 *input, *output;

	i32 outputGain;
	i32	i;
	i32 accum;

	i32 state00;
 	i16 coeff00, coeff01;

	TRACEJ(5, "gp_inte_00() %i\n", mySps->enabled);
	if(!mySps->enabled)return(1);

	input   = mySps->source;
	output	= mySps->sink;

	npoints=mySps->nSamples;

	outputGain=mySps->outputGain;

	coeff00=((i16*)mySps->coef)[0];
	coeff01=((i16*)mySps->coef)[1];
	state00=((i32*)mySps->x)[0];

	// note fixed gain of 2 to compensate for attenuation
	// in passband

	for(i=0;i<npoints;i++)
	{
		accum=input[i];
		state00 = accum + (state00*coeff01)/M_Q15;
		accum = (state00*coeff00)/(M_Q15/4);
		output[i]=(accum*outputGain)/M_Q8;
	}

	((i32*)(mySps->x))[0]=state00;

	return 0;
}
/*
	general purpose differentiator hpf
*/
i16 gp_diff(t_pmr_sps *mySps)
{
 	i16 *input, *output;
	i16 npoints;
	i32 outputGain, calcAdjust;
	i32	i;
	i32 temp0,temp1;
 	i16 x0;
	i32 y0;
	i16 a0,a1;
	i16 *coef;
	i16 *x;

	input   = mySps->source;
	output	= mySps->sink;

	npoints=mySps->nSamples;

	outputGain=mySps->outputGain;
	calcAdjust=mySps->calcAdjust;

	coef=(i16*)(mySps->coef);
	x=(i16*)(mySps->x);
	a0=coef[0];
	a1=coef[1];

	x0=x[0];

	TRACEJ(5, "gp_diff()\n");

	for (i = 0; i < npoints; i++) {
		temp0 =	x0 * a1;
		   x0 = input[i];
		temp1 = input[i] * a0;
		   y0 = (temp0 + temp1)/calcAdjust;
		   y0 =(y0*outputGain)/M_Q8;
		
		if(y0>32767)y0=32767;
		else if(y0<-32767)y0=-32767;
        output[i]=y0;
	}

	x[0]=x0;

	return 0;
}
/* 	----------------------------------------------------------------------
	CenterSlicer
*/
i16 CenterSlicer(t_pmr_sps *mySps)
{
	i16 npoints;
 	i16 *input, *output, *buff;

	i32 inputGainB;
	i32	i;
	i32 accum;

	i32  amax;			// buffer amplitude maximum
	i32  amin;			// buffer amplitude minimum
	i32  apeak;			// buffer amplitude peak
	i32  center;
	i32  setpt;			// amplitude set point for peak tracking

	i32  discounteru;	// amplitude detector integrator discharge counter upper
	i32  discounterl;	// amplitude detector integrator discharge counter lower
	i32  discfactor;	// amplitude detector integrator discharge factor

	TRACEJ(5, "CenterSlicer() %i\n", mySps->enabled);
	if(!mySps->enabled)return(1);

	input   = mySps->source;
	output	= mySps->sink;	 			// limited output
	buff    = mySps->buff;

	npoints=mySps->nSamples;

	inputGainB=mySps->inputGainB;

	amax=mySps->amax;
	amin=mySps->amin;
	setpt=mySps->setpt;
	apeak=mySps->apeak;
	discounteru=mySps->discounteru;
	discounterl=mySps->discounterl;

	discfactor=mySps->discfactor;
	npoints=mySps->nSamples;

	for(i=0;i<npoints;i++)
	{
		static i32 tfx;
		accum=input[i];

		if(accum>amax)
		{
			amax=accum;
			if(amin<(amax-setpt))
			{
				amin=(amax-setpt);
			}
		}
		else if(accum<amin)
		{
			amin=accum;
			if(amax>(amin+setpt))
			{
				amax=(amin+setpt);
			}
		}
		#if 0
		if((discounteru-=1)<=0 && amax>amin)
		{
			if((amax-=10)<amin)amax=amin;
		}

		if((discounterl-=1)<=0 && amin<amax)
		{
			if((amin+=10)>amax)amin=amax;
			lhit=1;
		}
		if(uhit)discounteru=discfactor;
		if(lhit)discounterl=discfactor;

		#else
		 
		if((amax-=discfactor)<amin)amax=amin;
		if((amin+=discfactor)>amax)amin=amax;

		#endif

		apeak = (amax-amin)/2;
		center = (amax+amin)/2;
		accum = accum - center;

		output[i]=accum;  			// sink output unlimited/centered.

		// do limiter function
		if(accum>inputGainB)accum=inputGainB;
		else if(accum<-inputGainB)accum=-inputGainB;
		buff[i]=accum;

		#if XPMR_DEBUG0 == 1
		#if 0
		mySps->parentChan->pRxLsdCen[i]=center;	  	// trace center ref
		#else
		tfx=0;
		if((tfx++/8)&1)				  				// trace min/max levels
			mySps->parentChan->pRxLsdCen[i]=amax;
		else
			mySps->parentChan->pRxLsdCen[i]=amin;
		#endif
	    #if 0
		if(mySps->parentChan->frameCountRx&0x01) mySps->parentChan->prxDebug1[i]=amax;
		else mySps->parentChan->prxDebug1[i]=amin;
		#endif
		#endif
	}

	mySps->amax=amax;
	mySps->amin=amin;
	mySps->apeak=apeak;
	mySps->discounteru=discounteru;
	mySps->discounterl=discounterl;

	return 0;
}
/* 	----------------------------------------------------------------------
	MeasureBlock
	determine peak amplitude
*/
i16 MeasureBlock(t_pmr_sps *mySps)
{
	i16 npoints;
 	i16 *input, *output;

	i32	i;
	i32 accum;

	i16  amax;			// buffer amplitude maximum
	i16  amin;			// buffer amplitude minimum
	i16  apeak=0;			// buffer amplitude peak (peak to peak)/2
	i16  setpt;			// amplitude set point for amplitude comparator

	i32  discounteru;	// amplitude detector integrator discharge counter upper
	i32  discounterl;	// amplitude detector integrator discharge counter lower
	i32  discfactor;	// amplitude detector integrator discharge factor

	TRACEJ(5, "MeasureBlock() %i\n", mySps->enabled);

	if(!mySps->enabled)return 1;

	if(mySps->option==3)
	{
		mySps->amax = mySps->amin = mySps->apeak = \
		mySps->discounteru = mySps->discounterl = \
		mySps->enabled = 0;
		return 1;
	}

	input   = mySps->source;
	output	= mySps->sink;

	npoints=mySps->nSamples;

	amax=mySps->amax;
	amin=mySps->amin;
	setpt=mySps->setpt;
	discounteru=mySps->discounteru;
	discounterl=mySps->discounterl;

	discfactor=mySps->discfactor;
	npoints=mySps->nSamples;

	for(i=0;i<npoints;i++)
	{
		accum=input[i];

		if(accum>amax)
		{
			amax=accum;
			discounteru=discfactor;
		}
		else if(--discounteru<=0)
		{
			discounteru=discfactor;
			amax=(i32)((amax*32700)/32768);
		}

		if(accum<amin)
		{
			amin=accum;
			discounterl=discfactor;
		}
		else if(--discounterl<=0)
		{
			discounterl=discfactor;
			amin=(i32)((amin*32700)/32768);
		}

		apeak = (i32)(amax-amin)/2;
		if(output)output[i]=apeak;
	}

	mySps->amax=amax;
	mySps->amin=amin;
	mySps->apeak=apeak;
	mySps->discounteru=discounteru;
	mySps->discounterl=discounterl;
	if(apeak>=setpt) mySps->compOut=1;
	else mySps->compOut=0;

	// TRACEX((1, " -MeasureBlock()=%i\n",mySps->apeak));
	return 0;
}
/*
	SoftLimiter
*/
i16 SoftLimiter(t_pmr_sps *mySps)
{
	i16 npoints;
	//i16 samples, lhit,uhit;
 	i16 *input, *output;

	i32 outputGain;
	i32	i;
	i32 accum;
	i32  tmp;

	i32  amax;			// buffer amplitude maximum
	i32  amin;			// buffer amplitude minimum
	//i32  apeak;		// buffer amplitude peak
	i32  setpt;			// amplitude set point for amplitude comparator

	input   = mySps->source;
	output	= mySps->sink;

	outputGain=mySps->outputGain;

	npoints=mySps->nSamples;

	setpt=mySps->setpt;
	amax=(setpt*124)/128;
	amin=-amax;

	TRACEJ(5, "SoftLimiter() %i %i %i) \n", amin, amax, setpt);

	for(i=0;i<npoints;i++)
	{
		accum=input[i];
		//accum=input[i]*mySps->inputGain/256;

		if(accum>setpt)
		{
		    tmp=((accum-setpt)*4)/128;
		    accum=setpt+tmp;
			if(accum>amax)accum=amax;
			accum=setpt;
		}
		else if(accum<-setpt)
		{
		    tmp=((accum+setpt)*4)/128;
		    accum=(-setpt)-tmp;
			if(accum<amin)accum=amin;
			accum=-setpt;
		}

		output[i]=(accum*outputGain)/M_Q8;
	}

	return 0;
}
/*
	SigGen() - sine, square function generator
	sps overloaded values
	discfactor  = phase factor
	discfactoru = phase index
	if source is not NULL then mix it in!

	sign table and output gain are in Q15 format (32767=.999)
*/
i16	SigGen(t_pmr_sps *mySps)
{
	#define PH_FRACT_FACT	128

	i32 ph;
	i16 i,outputgain,waveform,numChanOut,selChanOut;
	i32 accum;

	t_pmr_chan *pChan;
	pChan = mySps->parentChan;
	TRACEC(5, "SigGen(%i %i %i)\n", mySps->option, mySps->enabled, mySps->state);

	if(!mySps->freq ||!mySps->enabled)return 0;

	outputgain=mySps->outputGain;
	waveform=0;
	numChanOut=mySps->numChanOut;
	selChanOut=mySps->selChanOut;

    if(mySps->option==1)
	{
		mySps->option=0;
		mySps->state=1;
		mySps->discfactor=
			(SAMPLES_PER_SINE*mySps->freq*PH_FRACT_FACT)/mySps->sampleRate/10;

		TRACEF(5, " SigGen() discfactor = %i\n", mySps->discfactor);
		if(mySps->discounterl)mySps->state=2;
	}
	else if(mySps->option==2)
	{
		i16 shiftfactor=CTCSS_TURN_OFF_SHIFT;
		// phase shift request
		mySps->option=0;
		mySps->state=2;
		mySps->discounterl=CTCSS_TURN_OFF_TIME-(2*MS_PER_FRAME);   		//

		mySps->discounteru = \
			(mySps->discounteru + (((SAMPLES_PER_SINE*shiftfactor)/360)*PH_FRACT_FACT)) % (SAMPLES_PER_SINE*PH_FRACT_FACT);
		//printf("shiftfactor = %i\n",shiftfactor);
		//shiftfactor+=10;
	}
	else if(mySps->option==3)
	{
		// stop it and clear the output buffer
		mySps->option=0;
		mySps->state=0;
		mySps->enabled=0;
		mySps->b.mute=0;
		for(i=0;i<mySps->nSamples;i++)
			mySps->sink[(i*numChanOut)+selChanOut]=0;
		return(0);
	}
	else if(mySps->state==2)
	{
		// doing turn off
		mySps->discounterl-=MS_PER_FRAME;
		if(mySps->discounterl<=0)
		{
			mySps->option=3;
			mySps->state=2;
		}
	}
	else if(mySps->state==0)
	{
		return(0);
	}

	ph=mySps->discounteru;

	for(i=0;i<mySps->nSamples;i++)
	{
		if(!waveform)
		{
			// sine
			//tmp=(sinetablex[ph/PH_FRACT_FACT]*amplitude)/M_Q16;
			accum=sinetablex[ph/PH_FRACT_FACT];
			accum=(accum*outputgain)/M_Q8;
	    }
		else
		{
			// square
			if(ph>SAMPLES_PER_SINE/2)
				accum=outputgain/M_Q8;
			else
				accum=-outputgain/M_Q8;
		}

		if(mySps->source)accum+=mySps->source[i];

		if(mySps->b.mute) accum = 0;

		mySps->sink[(i*numChanOut)+selChanOut]=accum;

		ph=(ph+mySps->discfactor)%(SAMPLES_PER_SINE*PH_FRACT_FACT);
	}

	mySps->discounteru=ph;

	return 0;
}
/*
	adder/mixer
	takes existing buffer and adds source buffer to destination buffer
	sink buffer = (sink buffer * gain) + source buffer
*/
i16 pmrMixer(t_pmr_sps *mySps)
{
	i32 accum;
	i16 i, *input, *inputB, *output;
	i16  inputGain, inputGainB;	  	// apply to input data	 in Q7.8 format
	i16  outputGain;	// apply to output data  in Q7.8 format
	i16	 discounteru,discounterl,amax,amin,setpt,discfactor;
	i16	 npoints,uhit,lhit,apeak,measPeak;

	t_pmr_chan *pChan;
	pChan = mySps->parentChan;
	TRACEF(5, "pmrMixer()\n");

	input     = mySps->source;
	inputB    = mySps->sourceB;
	output    = mySps->sink;

	inputGain=mySps->inputGain;
	inputGainB=mySps->inputGainB;
	outputGain=mySps->outputGain;

	amax=mySps->amax;
	amin=mySps->amin;
	setpt=mySps->setpt;
	discounteru=mySps->discounteru;
	discounterl=mySps->discounteru;

	discfactor=mySps->discfactor;
	npoints=mySps->nSamples;
	measPeak=mySps->measPeak;

	for(i=0;i<npoints;i++)
	{
		if (inputB)
		{
			accum = ((input[i]*inputGain)/M_Q8) +
				((inputB[i]*inputGainB)/M_Q8);
		}
		else
		{
			accum = (input[i]*inputGain)/M_Q8;
		}
		accum=(accum*outputGain)/M_Q8;
		output[i]=accum;

		if(measPeak){
	  		lhit=uhit=0;

			if(accum>amax){
				amax=accum;
				uhit=1;
				if(amin<(amax-setpt)){
					amin=(amax-setpt);
					lhit=1;
				}
			}
			else if(accum<amin){
				amin=accum;
				lhit=1;
				if(amax>(amin+setpt)){
					amax=(amin+setpt);
					uhit=1;
				}
			}

			if(--discounteru<=0 && amax>0){
				amax--;
				uhit=1;
			}

			if(--discounterl<=0 && amin<0){
				amin++;
				lhit=1;
			}

			if(uhit)discounteru=discfactor;
			if(lhit)discounterl=discfactor;
		}
 	}

	if(measPeak){
		apeak = (amax-amin)/2;
		mySps->apeak=apeak;
		mySps->amax=amax;
		mySps->amin=amin;
		mySps->discounteru=discounteru;
		mySps->discounterl=discounterl;
	}

	return 0;
}
/*
	DelayLine
*/
i16 DelayLine(t_pmr_sps *mySps)
{
	i16 *input, *output, *buff;
	i16	 i, npoints,buffsize,inindex,outindex;

	t_pmr_chan *pChan;
	pChan = mySps->parentChan;
	TRACEF(5, " DelayLine() %i\n", mySps->enabled);

	if(!mySps->enabled || mySps->b.outzero)
	{
		if(mySps->b.dirty)
		{
			mySps->b.dirty=0; 
			mySps->buffInIndex=0;
			memset((void *)(mySps->buff),0,mySps->buffSize*2);
			memset((void *)(mySps->sink),0,mySps->nSamples*2);
		}
		return(0);
	}

	input    	= mySps->source;
	output    	= mySps->sink;
	buff     	= (i16*)(mySps->buff);
	buffsize  	= mySps->buffSize;
	npoints		= mySps->nSamples;
	inindex		= mySps->buffInIndex;
	outindex	= inindex-mySps->buffLead;

	if(outindex<0)outindex+=buffsize;
	
	for(i=0;i<npoints;i++)
	{
		inindex %= buffsize;
		outindex %= buffsize;
		buff[inindex]=input[i];
		output[i]=buff[outindex];
		inindex++;
		outindex++;
 	}
	mySps->buffInIndex=inindex;
	mySps->b.dirty=1;
 	return 0;
}
/*
	Continuous Tone Coded Squelch (CTCSS) Detector
*/
i16 ctcss_detect(t_pmr_chan *pChan)
{
	i16 i,points2do,*pInput,hit,thit,relax;
	i16 tnum, tmp,indexNow,diffpeak;
	i16 tv0,tv1,tv2,tv3,indexDebug;
	i16 points=0;
	i16 indexWas=0;

	TRACEF(5, "ctcss_detect(%p) %i %i %i %i\n", pChan, pChan->rxCtcss->enabled, 0, pChan->rxCtcss->testIndex, pChan->rxCtcss->decode);

	if(!pChan->rxCtcss->enabled)return(1);

	relax  = pChan->rxCtcss->relax;
	pInput = pChan->rxCtcss->input;

	thit=hit=-1;

	// TRACEX((1, " ctcss_detect() %i  %i  %i  %i\n", CTCSS_NUM_CODES,0,0,0));

	for(tnum=0;tnum<CTCSS_NUM_CODES;tnum++)
	{
		i32 accum, peak;
		t_tdet	*ptdet;
		i16 fudgeFactor;
		i16 binFactor;

		TRACEF(6, " ctcss_detect() tnum=%i %i\n", tnum, pChan->rxCtcssMap[tnum]);
		//if(tnum==14)printf("ctcss_detect() %i %i %i\n",tnum,pChan->rxCtcssMap[tnum], pChan->rxCtcss->decode );

		if( (pChan->rxCtcssMap[tnum]==CTCSS_NULL) ||
		    (pChan->rxCtcss->decode>CTCSS_NULL && (tnum!= pChan->rxCtcss->decode))
		  )
			continue;

		TRACEF(6, " ctcss_detect() tnum=%i\n", tnum);

		ptdet=&(pChan->rxCtcss->tdet[tnum]);
		indexDebug=0;
		points=points2do=pChan->nSamplesRx;
		fudgeFactor=ptdet->fudgeFactor;
		binFactor=ptdet->binFactor;

		while(ptdet->counter < (points2do*CTCSS_SCOUNT_MUL))
		{
			tmp=(ptdet->counter/CTCSS_SCOUNT_MUL)+1;
		    ptdet->counter-=(tmp*CTCSS_SCOUNT_MUL);
			points2do-=tmp;
			indexNow=points-points2do;

			ptdet->counter += ptdet->counterFactor;

			accum = pInput[indexNow-1];	 	// duuuude's major bug fix!

			ptdet->z[ptdet->zIndex]+=
				(((accum - ptdet->z[ptdet->zIndex])*binFactor)/M_Q15);

			peak = abs(ptdet->z[0]-ptdet->z[2]) + abs(ptdet->z[1]-ptdet->z[3]);

			if (ptdet->peak < peak)
				ptdet->peak += ( ((peak-ptdet->peak)*binFactor)/M_Q15);
			else
				ptdet->peak=peak;

			{
				static const i16 a0=13723;
				static const i16 a1=-13723;
				i32 temp0,temp1;
				i16 x0;

				//differentiate
				x0=ptdet->zd;
				temp0 =	x0 * a1;
				ptdet->zd = ptdet->peak;
				temp1 = ptdet->peak * a0;
			    diffpeak = (temp0 + temp1)/1024;
			}

			if(diffpeak<(-0.03*M_Q15))ptdet->dvd-=4;
			else if(ptdet->dvd<0)ptdet->dvd++;

			if((ptdet->dvd < -12) && diffpeak > (-0.02*M_Q15))ptdet->dvu+=2;
			else if(ptdet->dvu)ptdet->dvu--;

			tmp=ptdet->setpt;
			if(pChan->rxCtcss->decode==tnum)
			{
				if(relax)tmp=(tmp*55)/100;
				else tmp=(tmp*80)/100;
			}

			if(ptdet->peak > tmp)
			{
			    if(ptdet->decode<(fudgeFactor*32))ptdet->decode++;
			}
			else if(pChan->rxCtcss->decode==tnum)
			{
				if(ptdet->peak > ptdet->hyst)ptdet->decode--;
				else if(relax) ptdet->decode--;
				else ptdet->decode-=4;
			}
			else
			{
				ptdet->decode=0;
			}

			if((pChan->rxCtcss->decode==tnum) && !relax && (ptdet->dvu > (0.00075*M_Q15)))
			{
				ptdet->decode=0;
				ptdet->z[0]=ptdet->z[1]=ptdet->z[2]=ptdet->z[3]=ptdet->dvu=0;
				TRACEF(4, "ctcss_detect() turnoff detected by dvdt for tnum = %i.\n", tnum);
			}

			if(ptdet->decode<0 || !pChan->rxCarrierDetect)ptdet->decode=0;

			if(ptdet->decode>=fudgeFactor)
			{
				thit=tnum;
				if(pChan->rxCtcss->decode!=tnum)
				{
					ptdet->zd=ptdet->dvu=ptdet->dvd=0;	
				}
			}

			#if XPMR_DEBUG0 == 1
			if(thit>=0 && thit==tnum)
				TRACEF(6, " ctcss_detect() %i %i %i %i \n", tnum, ptdet->peak, ptdet->setpt, ptdet->hyst);

			if(ptdet->pDebug0)
			{
				tv0=ptdet->peak;
				tv1=ptdet->decode;
				tv2=tmp;
				tv3=ptdet->dvu*32;

				if(indexDebug==0)
				{
					ptdet->lasttv0=ptdet->pDebug0[points-1];
					ptdet->lasttv1=ptdet->pDebug1[points-1];
					ptdet->lasttv2=ptdet->pDebug2[points-1];
					ptdet->lasttv3=ptdet->pDebug3[points-1];
				}

				while(indexDebug<indexNow)
				{
					ptdet->pDebug0[indexDebug]=ptdet->lasttv0;
					ptdet->pDebug1[indexDebug]=ptdet->lasttv1;
					ptdet->pDebug2[indexDebug]=ptdet->lasttv2;
					ptdet->pDebug3[indexDebug]=ptdet->lasttv3;
					indexDebug++;
				}
				ptdet->lasttv0=tv0;
				ptdet->lasttv1=tv1;
				ptdet->lasttv2=tv2;
				ptdet->lasttv3=tv3;
			}
			#endif
			indexWas=indexNow;
			ptdet->zIndex=(++ptdet->zIndex)%4;
		}
		ptdet->counter-=(points2do*CTCSS_SCOUNT_MUL);

		#if XPMR_DEBUG0 == 1
		for(i=indexWas;i<points;i++)
		{
			ptdet->pDebug0[i]=ptdet->lasttv0;
			ptdet->pDebug1[i]=ptdet->lasttv1;
			ptdet->pDebug2[i]=ptdet->lasttv2;
			ptdet->pDebug3[i]=ptdet->lasttv3;
		}
		#endif
	}

	// TRACEX((1, " ctcss_detect() thit %i\n",thit));

	if(pChan->rxCtcss->BlankingTimer>0)pChan->rxCtcss->BlankingTimer-=points;
	if(pChan->rxCtcss->BlankingTimer<0)pChan->rxCtcss->BlankingTimer=0;

    if(thit>CTCSS_NULL && pChan->rxCtcss->decode<=CTCSS_NULL && !pChan->rxCtcss->BlankingTimer)
    {
		pChan->rxCtcss->decode=thit;
		sprintf(pChan->rxctcssfreq,"%.1f",freq_ctcss[thit]);
		TRACEC(1, "ctcss decode  %i  %.1f\n", thit, freq_ctcss[thit]);
	}
	else if(thit<=CTCSS_NULL && pChan->rxCtcss->decode>CTCSS_NULL)
	{
		pChan->rxCtcss->BlankingTimer=SAMPLE_RATE_NETWORK/5;
		pChan->rxCtcss->decode=CTCSS_NULL;
		strcpy(pChan->rxctcssfreq,"0");
		TRACEC(1, "ctcss decode  NULL\n");
		for(tnum=0;tnum<CTCSS_NUM_CODES;tnum++)
		{
		    t_tdet	*ptdet=NULL;
			ptdet=&(pChan->rxCtcss->tdet[tnum]);
		    ptdet->decode=0;
			ptdet->z[0]=ptdet->z[1]=ptdet->z[2]=ptdet->z[3]=0;
		}
	}
	// TRACEX((1, " ctcss_detect() thit %i %i\n",thit,pChan->rxCtcss->decode));
	return(0);
}
/*
	TxTestTone
*/
i16	TxTestTone(t_pmr_chan *pChan, i16 function)
{
	if(function==1)
	{
		pChan->spsSigGen1->enabled=1;
		pChan->spsSigGen1->option=1;
		pChan->spsSigGen1->outputGain=(.23125*M_Q8);   // to match *99 level
		pChan->spsTx->source=pChan->spsSigGen1->sink;
	}
	else
	{
		pChan->spsSigGen1->option=3;
	}
	return 0;
}

/*
	assumes:
	sampling rate is 48KS/s
	samples are all 16 bits
    samples are filtered and decimated by 1/6th
*/
t_pmr_chan	*createPmrChannel(t_pmr_chan *tChan, i16 numSamples)
{
	i16 i, *inputTmp;
	t_pmr_chan 	*pChan;
	t_pmr_sps  	*pSps;
	t_dec_ctcss	*pDecCtcss;

	TRACEJ(1, "createPmrChannel(%p,%i)\n", tChan, numSamples);

	pChan = (t_pmr_chan *)ast_calloc(sizeof(t_pmr_chan),1);
	if(pChan==NULL)
	{
		ast_log(LOG_ERROR, "createPmrChannel() failed\n");
		return(NULL);
	}

	#if XPMR_PPTP == 1
	pptp_init();
	#endif

	pChan->index=pmrChanIndex++;
	pChan->nSamplesTx=pChan->nSamplesRx=numSamples;

	pDecCtcss = (t_dec_ctcss *)ast_calloc(sizeof(t_dec_ctcss),1);
	pChan->rxCtcss=pDecCtcss;
	pChan->rxctcssfreq[0]=0;

	#ifdef HAVE_XPMRX
	if(tChan->rptnum>=LSD_CHAN_MAX)tChan->rptnum=0;
	#endif

	if(tChan==NULL)
	{
		ast_log(LOG_WARNING, "createPmrChannel() WARNING: NULL tChan!\n");
		pChan->rxNoiseSquelchEnable=0;
		pChan->rxHpfEnable=0;
		pChan->rxDeEmpEnable=0;
		pChan->rxCenterSlicerEnable=0;
		pChan->rxCtcssDecodeEnable=0;
		pChan->rxDcsDecodeEnable=0;

		pChan->rxCarrierPoint = 17000;
		pChan->rxCarrierHyst = 2500;

		pChan->txHpfEnable=0;
		pChan->txLimiterEnable=0;
		pChan->txPreEmpEnable=0;
		pChan->txLpfEnable=1;
		pChan->txMixA=TX_OUT_VOICE;
		pChan->txMixB=TX_OUT_LSD;
	}
	else
	{
		pChan->rxDemod=tChan->rxDemod;
		pChan->rxCdType=tChan->rxCdType;
		pChan->voxHangTime=tChan->voxHangTime;
		pChan->rxSquelchPoint = tChan->rxSquelchPoint;
		pChan->rxCarrierHyst = tChan->rxCarrierHyst;
		pChan->rxSqVoxAdj=tChan->rxSqVoxAdj;
		pChan->rxSquelchDelay=tChan->rxSquelchDelay;
		pChan->rxNoiseFilType=tChan->rxNoiseFilType;

		pChan->txMod=tChan->txMod;
		pChan->txHpfEnable=1;
		pChan->txLpfEnable=1;

		pChan->pTxCodeDefault=tChan->pTxCodeDefault;
		pChan->pRxCodeSrc=tChan->pRxCodeSrc;
		pChan->pTxCodeSrc=tChan->pTxCodeSrc;

		pChan->txMixA=tChan->txMixA;
		pChan->txMixB=tChan->txMixB;
		pChan->radioDuplex=tChan->radioDuplex;
		pChan->area=tChan->area;
		pChan->rptnum=tChan->rptnum;
		pChan->idleinterval=tChan->idleinterval;
		pChan->turnoffs=tChan->turnoffs;
		pChan->b.rxpolarity=tChan->b.rxpolarity;
		pChan->b.txpolarity=tChan->b.txpolarity;
		pChan->b.dcsrxpolarity=tChan->b.dcsrxpolarity;
		pChan->b.dcstxpolarity=tChan->b.dcstxpolarity;
		pChan->b.lsdrxpolarity=tChan->b.lsdrxpolarity;
		pChan->b.lsdtxpolarity=tChan->b.lsdtxpolarity;
		pChan->b.txboost=tChan->b.txboost;

		pChan->txsettletime=tChan->txsettletime;
		pChan->tracelevel=tChan->tracelevel;
		pChan->tracetype=tChan->tracetype;
		pChan->ukey=tChan->ukey;
		pChan->name=tChan->name;
		pChan->fever = tChan->fever;

        if(tChan->rxlpf<MAX_RXLPF&&tChan->rxlpf>=0)
            pChan->rxlpf=tChan->rxlpf;
        else
            pChan->rxlpf=0;
        
        if(tChan->rxhpf<MAX_RXHPF&&tChan->rxhpf>=0)
            pChan->rxhpf=tChan->rxhpf;
        else
            pChan->rxhpf=0;
        
        if(tChan->txlpf<MAX_TXLPF&&tChan->txlpf>=0)
            pChan->txlpf=tChan->txlpf;
        else
            pChan->txlpf=0;
        
        if(tChan->txhpf<MAX_TXHPF&&tChan->txhpf>=0)
            pChan->txhpf=tChan->txhpf;
        else
            pChan->txhpf=0;
        ast_log(LOG_NOTICE,"xpmr rxlpf: %d\n",pChan->rxlpf);
        ast_log(LOG_NOTICE,"xpmr rxhpf: %d\n",pChan->rxhpf);
        ast_log(LOG_NOTICE,"xpmr txlpf: %d\n",pChan->txlpf);
        ast_log(LOG_NOTICE,"xpmr txhpf: %d\n",pChan->txhpf);

    }

	if(pChan->rxCarrierHyst==0)
		pChan->rxCarrierHyst = 3000;

	pChan->txHpfEnable=1;
	pChan->txLpfEnable=1;

	if(pChan->rxCdType==CD_XPMR_NOISE) pChan->rxNoiseSquelchEnable=1;

	if(pChan->rxDemod==RX_AUDIO_FLAT) pChan->rxDeEmpEnable=1;

	pChan->rxCarrierPoint=(pChan->rxSquelchPoint*32767)/100;
	pChan->rxCarrierHyst = 3000; //pChan->rxCarrierPoint/15;

	pChan->rxDcsDecodeEnable=0;

	if(pChan->b.ctcssRxEnable || pChan->b.dcsRxEnable || pChan->b.lmrRxEnable)
	{
		pChan->rxHpfEnable=1;
		pChan->rxCenterSlicerEnable=1;
		pChan->rxCtcssDecodeEnable=1;
	}

	if(pChan->txMod)
		pChan->txLimiterEnable=1;
	if(pChan->txMod > 1)
		pChan->txPreEmpEnable=1;

	pChan->dd.option=9;
	dedrift(pChan);

	pChan->lastrxdecode = CTCSS_NULL;

	TRACEF(1, "calloc buffers \n");

	pChan->pRxDemod 	= ast_calloc(numSamples,2);
	pChan->pRxNoise 	= ast_calloc(numSamples,2);
	pChan->pRxBase 		= ast_calloc(numSamples,2);
	pChan->pRxHpf 		= ast_calloc(numSamples,2);
	pChan->pRxLsd 		= ast_calloc(numSamples,2);
	pChan->pRxSpeaker 	= ast_calloc(numSamples,2);
	pChan->pRxCtcss 	= ast_calloc(numSamples,2);
	pChan->pRxDcTrack 	= ast_calloc(numSamples,2);
	pChan->pRxLsdLimit 	= ast_calloc(numSamples,2);

	pChan->pTxInput  	= ast_calloc(numSamples,2);
	pChan->pTxBase  	= ast_calloc(numSamples,2);
	pChan->pTxHpf	 	= ast_calloc(numSamples,2);
	pChan->pTxPreEmp 	= ast_calloc(numSamples,2);
	pChan->pTxLimiter 	= ast_calloc(numSamples,2);
	pChan->pTxLsd	 	= ast_calloc(numSamples,2);
	pChan->pTxLsdLpf    = ast_calloc(numSamples,2);
	pChan->pTxComposite	= ast_calloc(numSamples,2);
	pChan->pSigGen0		= ast_calloc(numSamples,2);
    pChan->pSigGen1		= ast_calloc(numSamples,2);
		
	pChan->prxMeasure	= ast_calloc(numSamples,2);

	pChan->pTxOut		= ast_calloc(numSamples,2*2*6);		// output buffer
    
#ifdef HAVE_XPMRX
	pChan->pLsdEnc		= ast_calloc(sizeof(t_encLsd),1);
#endif

	#if XPMR_DEBUG0 == 1
	TRACEF(1, "configure tracing\n");

	pChan->pTstTxOut	= ast_calloc(numSamples,2);
	pChan->pRxLsdCen    = ast_calloc(numSamples,2);
	pChan->prxDebug0	= ast_calloc(numSamples,2);
	pChan->prxDebug1	= ast_calloc(numSamples,2);
	pChan->prxDebug2	= ast_calloc(numSamples,2);
	pChan->prxDebug3	= ast_calloc(numSamples,2);
	pChan->ptxDebug0	= ast_calloc(numSamples,2);
	pChan->ptxDebug1	= ast_calloc(numSamples,2);
	pChan->ptxDebug2	= ast_calloc(numSamples,2);
	pChan->ptxDebug3	= ast_calloc(numSamples,2);
	pChan->pNull		= ast_calloc(numSamples,2);

	for(i=0;i<numSamples;i++)pChan->pNull[i]=((i%(numSamples/2))*8000)-4000;

	pChan->rxCtcss->pDebug0=ast_calloc(numSamples,2);
	pChan->rxCtcss->pDebug1=ast_calloc(numSamples,2);
	pChan->rxCtcss->pDebug2=ast_calloc(numSamples,2);
	pChan->rxCtcss->pDebug3=ast_calloc(numSamples,2);

	for(i=0;i<CTCSS_NUM_CODES;i++)
	{
		pChan->rxCtcss->tdet[i].pDebug0=ast_calloc(numSamples,2);
		pChan->rxCtcss->tdet[i].pDebug1=ast_calloc(numSamples,2);
		pChan->rxCtcss->tdet[i].pDebug2=ast_calloc(numSamples,2);
		pChan->rxCtcss->tdet[i].pDebug3=ast_calloc(numSamples,2);
	}

	// buffer, 2 bytes per sample, and 16 channels
	pChan->prxDebug=ast_calloc(numSamples*16,2);
	pChan->ptxDebug=ast_calloc(numSamples*16,2);

	// TSCOPE CONFIGURATION SETSCOPE configure debug traces and sources for each channel of the output
	pChan->sdbg			= (t_sdbg *)ast_calloc(sizeof(t_sdbg),1);

	for (i = 0; i < XPMR_DEBUG_CHANS; i++) {
		pChan->sdbg->trace[i] = -1;
	}

	TRACEF(1, "pChan->tracetype = %i\n", pChan->tracetype);

	if(pChan->tracetype==1)				  			// CTCSS DECODE
	{
		pChan->sdbg->source [0]=pChan->pRxDemod;
		pChan->sdbg->source [1]=pChan->pRxBase;
		pChan->sdbg->source [2]=pChan->pRxNoise;
		pChan->sdbg->trace  [3]=RX_NOISE_TRIG;
		pChan->sdbg->source [4]=pChan->pRxLsd;
		pChan->sdbg->source [5]=pChan->pRxLsdCen;
		pChan->sdbg->source [6]=pChan->pRxLsdLimit;
		pChan->sdbg->source [7]=pChan->rxCtcss->tdet[3].pDebug0;
		pChan->sdbg->trace  [8]=RX_CTCSS_DECODE;
		pChan->sdbg->trace  [9]=RX_SMODE;
		pChan->sdbg->source  [10]=pChan->pRxBase;
		pChan->sdbg->source  [11]=pChan->pRxSpeaker;
	}
	if(pChan->tracetype==2)							// CTCSS DECODE
	{
		pChan->sdbg->source [0]=pChan->pRxDemod;
		pChan->sdbg->source [1]=pChan->pRxBase;
		pChan->sdbg->trace  [2]=RX_NOISE_TRIG;
		pChan->sdbg->source [3]=pChan->pRxLsd;
		pChan->sdbg->source [4]=pChan->pRxLsdCen;
		pChan->sdbg->source [5]=pChan->pRxDcTrack;
		pChan->sdbg->source [6]=pChan->pRxLsdLimit;
		pChan->sdbg->source [7]=pChan->rxCtcss->tdet[3].pDebug0;
		pChan->sdbg->source [8]=pChan->rxCtcss->tdet[3].pDebug1;
		pChan->sdbg->source [9]=pChan->rxCtcss->tdet[3].pDebug2;
		pChan->sdbg->source [10]=pChan->rxCtcss->tdet[3].pDebug3;
		pChan->sdbg->trace  [11]=RX_CTCSS_DECODE;
		pChan->sdbg->trace  [12]=RX_SMODE;
		pChan->sdbg->trace  [13]=TX_PTT_IN;
		pChan->sdbg->trace  [14]=TX_PTT_OUT;
		pChan->sdbg->source [15]=pChan->pTxLsdLpf;
	}
	else if(pChan->tracetype==3)					// DCS DECODE
	{
		pChan->sdbg->source [0]=pChan->pRxDemod;
		pChan->sdbg->source [1]=pChan->pRxBase;
		pChan->sdbg->trace  [2]=RX_NOISE_TRIG;
		pChan->sdbg->source [3]=pChan->pRxLsd;
		pChan->sdbg->source [4]=pChan->pRxLsdCen;
		pChan->sdbg->source [5]=pChan->pRxDcTrack;
		pChan->sdbg->trace  [6]=RX_DCS_CLK;
		pChan->sdbg->trace  [7]=RX_DCS_DIN;
		pChan->sdbg->trace  [8]=RX_DCS_DEC;
		pChan->sdbg->trace  [9]=RX_SMODE;
		pChan->sdbg->trace  [10]=TX_PTT_IN;
		pChan->sdbg->trace  [11]=TX_PTT_OUT;
		pChan->sdbg->trace  [12]=TX_LSD_CLK;
		pChan->sdbg->trace  [13]=TX_LSD_DAT;
		pChan->sdbg->trace  [14]=TX_LSD_GEN;
		pChan->sdbg->source [14]=pChan->pTxLsd;
		pChan->sdbg->source [15]=pChan->pTxLsdLpf;
	}
	else if(pChan->tracetype==4)			 		// LSD DECODE
	{
		pChan->sdbg->source [0]=pChan->pRxDemod;
		pChan->sdbg->source [1]=pChan->pRxBase;
		pChan->sdbg->trace  [2]=RX_NOISE_TRIG;
		pChan->sdbg->source [3]=pChan->pRxLsd;
		pChan->sdbg->source [4]=pChan->pRxLsdCen;
		pChan->sdbg->source [5]=pChan->pRxDcTrack;
		pChan->sdbg->trace  [6]=RX_LSD_CLK;
		pChan->sdbg->trace  [7]=RX_LSD_DAT;
		pChan->sdbg->trace  [8]=RX_LSD_ERR;
		pChan->sdbg->trace  [9]=RX_LSD_SYNC;
		pChan->sdbg->trace  [10]=RX_SMODE;
		pChan->sdbg->trace  [11]=TX_PTT_IN;
		pChan->sdbg->trace  [12]=TX_PTT_OUT;
		pChan->sdbg->trace  [13]=TX_LSD_CLK;
		pChan->sdbg->trace  [14]=TX_LSD_DAT;
		//pChan->sdbg->trace  [14]=TX_LSD_GEN;
		//pChan->sdbg->source [14]=pChan->pTxLsd;
		pChan->sdbg->source [15]=pChan->pTxLsdLpf;
	}
	else if(pChan->tracetype==5)						// LSD LOGIC
	{
		pChan->sdbg->source [0]=pChan->pRxBase;
		pChan->sdbg->trace  [1]=RX_NOISE_TRIG;
		pChan->sdbg->source [2]=pChan->pRxDcTrack;
		pChan->sdbg->trace  [3]=RX_LSD_SYNC;
		pChan->sdbg->trace  [4]=RX_SMODE;
		pChan->sdbg->trace  [5]=TX_PTT_IN;
		pChan->sdbg->trace  [6]=TX_PTT_OUT;
		pChan->sdbg->source [7]=pChan->pTxLsdLpf;
	}
	else if(pChan->tracetype==6)
	{
		// tx clock skew and jitter buffer
		pChan->sdbg->source [0]=pChan->pRxDemod;
		pChan->sdbg->source  [5]=pChan->pTxBase;
		pChan->sdbg->trace   [6]=TX_DEDRIFT_LEAD;
		pChan->sdbg->trace   [7]=TX_DEDRIFT_ERR;
		pChan->sdbg->trace   [8]=TX_DEDRIFT_FACTOR;
		pChan->sdbg->trace   [9]=TX_DEDRIFT_DRIFT;
	}
	else if(pChan->tracetype==7)
	{
		// tx path
		pChan->sdbg->source [0]=pChan->pRxBase;
		pChan->sdbg->trace  [1]=RX_NOISE_TRIG;
		pChan->sdbg->source [2]=pChan->pRxLsd;
		pChan->sdbg->trace  [3]=RX_CTCSS_DECODE;
		pChan->sdbg->source [4]=pChan->pRxHpf;

		pChan->sdbg->trace  [5]=TX_PTT_IN;
		pChan->sdbg->trace  [6]=TX_PTT_OUT;

		pChan->sdbg->source [7]=pChan->pTxBase;
		pChan->sdbg->source [8]=pChan->pTxHpf;
		pChan->sdbg->source [9]=pChan->pTxPreEmp;
		pChan->sdbg->source [10]=pChan->pTxLimiter;
		pChan->sdbg->source [11]=pChan->pTxComposite;
		pChan->sdbg->source [12]=pChan->pTxLsdLpf;
	}

	for(i=0;i<XPMR_DEBUG_CHANS;i++){
		if(pChan->sdbg->trace[i]>=0)pChan->sdbg->point[pChan->sdbg->trace[i]]=i; 	
	}
	pChan->sdbg->mode=1;
 	#endif

	#ifdef XPMRX_H
	// LSD GENERATOR
 	pSps=pChan->spsLsdGen=createPmrSps(pChan);
	pSps->source=NULL;
	pSps->sink=pChan->pTxLsd;
	pSps->numChanOut=1;
	pSps->selChanOut=0;
	pSps->sigProc=LsdGen;
	pSps->nSamples=pChan->nSamplesTx;
	pSps->outputGain=(.49*M_Q8);
	pSps->option=0;
	pSps->interpolate=1;
	pSps->decimate=1;
	pSps->enabled=0;
	#endif

	// General Purpose Function Generator
	pSps=pChan->spsSigGen1=createPmrSps(pChan);
	pSps->sink=pChan->pSigGen1;
	pSps->numChanOut=1;
	pSps->selChanOut=0;
	pSps->sigProc=SigGen;
	pSps->nSamples=pChan->nSamplesTx;
	pSps->sampleRate=SAMPLE_RATE_NETWORK;
	pSps->freq=10000; 						// in increments of 0.1 Hz
	pSps->outputGain=(.25*M_Q8);
	pSps->option=0;
	pSps->interpolate=1;
	pSps->decimate=1;
	pSps->enabled=0;


	// CTCSS ENCODER
	pSps = pChan->spsSigGen0 = createPmrSps(pChan);
	pSps->sink=pChan->pTxLsd;
	pSps->sigProc=SigGen;
	pSps->numChanOut=1;
	pSps->selChanOut=0;
	pSps->nSamples=pChan->nSamplesTx;
	pSps->sampleRate=SAMPLE_RATE_NETWORK;
	pSps->freq=1000;	  					// in 0.1 Hz steps
	pSps->outputGain=(0.5*M_Q8);
	pSps->option=0;
	pSps->interpolate=1;
	pSps->decimate=1;
	pSps->enabled=0;

	// Tx LSD Low Pass Filter
	pSps=pChan->spsTxLsdLpf=createPmrSps(pChan);
	pSps->source=pChan->pTxLsd;
	pSps->sink=pChan->pTxLsdLpf;
	pSps->sigProc=pmr_gp_fir;
	pSps->enabled=0;
	pSps->numChanOut=1;
	pSps->selChanOut=0;
	pSps->nSamples=pChan->nSamplesTx;
	pSps->decimator=pSps->decimate=1;
	pSps->interpolate=1;
	pSps->inputGain=(1*M_Q8);
	pSps->outputGain=(1*M_Q8);
	 
	// configure the longer, lower cutoff filter by default
	pSps->ncoef=taps_fir_lpf_215_9_88;
	pSps->size_coef=2;
	pSps->coef=(void*)coef_fir_lpf_215_9_88;
	pSps->nx=taps_fir_lpf_215_9_88;
	pSps->size_x=2;
	pSps->x = ast_calloc(pSps->nx, pSps->size_x);
	if (pSps->x == NULL) {
		; /* XXX do something here */
	}
	pSps->calcAdjust=gain_fir_lpf_215_9_88;

	pSps->inputGain=(1*M_Q8);
	pSps->outputGain=(1*M_Q8);

	TRACEF(1, "spsTxLsdLpf = sps \n");

	// RX Process
	TRACEF(1, "create rx\n");
	pSps = NULL;

	// allocate space for first sps and set pointers
	pSps=pChan->spsRx=createPmrSps(pChan);
	pSps->source=NULL;					//set when called
	pSps->sink=pChan->pRxBase;
	pSps->sigProc=pmr_rx_frontend;
	pSps->enabled=1;
	pSps->decimator=pSps->decimate=6;
	pSps->interpolate=pSps->interpolate=1;
	pSps->nSamples=pChan->nSamplesRx;
	pSps->ncoef=fir_rxlpf[pChan->rxlpf].taps;
	pSps->size_coef=2;
	pSps->coef=(void*)fir_rxlpf[pChan->rxlpf].coefs;
	pSps->nx=fir_rxlpf[pChan->rxlpf].taps;
	pSps->size_x=2;
	pSps->x = ast_calloc(pSps->nx, pSps->size_coef);
	if (pSps->x == NULL) {
		; /* XXX do something here */
	}
	pSps->calcAdjust=(fir_rxlpf[pChan->rxlpf].gain*256)/0x0100;
	pSps->outputGain=(1.0*M_Q8);
	pSps->discfactor=2;
	pSps->hyst=pChan->rxCarrierHyst;
	pSps->setpt=pChan->rxCarrierPoint;
	pChan->prxSquelchAdjust=&pSps->setpt;
	#if XPMR_DEBUG0 == 1
	pSps->debugBuff0=pChan->pRxDemod;
	pSps->debugBuff1=pChan->pRxNoise;
	pSps->debugBuff2=pChan->prxDebug0;
	#endif


	// allocate space for next sps and set pointers
	// Rx SubAudible Decoder Low Pass Filter
	pSps=pChan->spsRxLsd=pSps->nextSps=createPmrSps(pChan);
	pSps->source=pChan->pRxBase;
	pSps->sink=pChan->pRxLsd;
	pSps->sigProc=pmr_gp_fir;
	pSps->enabled=1;
	pSps->numChanOut=1;
	pSps->selChanOut=0;
	pSps->nSamples=pChan->nSamplesRx;
	pSps->decimator=pSps->decimate=1;
	pSps->interpolate=1;

	// configure the the larger, lower cutoff filter by default
	pSps->ncoef=taps_fir_lpf_215_9_88;
	pSps->size_coef=2;
	pSps->coef=(void*)coef_fir_lpf_215_9_88;
	pSps->nx=taps_fir_lpf_215_9_88;
	pSps->size_x=2;
	pSps->x = ast_calloc(pSps->nx, pSps->size_x);
	if (pSps->x == NULL) {
		; /* XXX do something here */
	}
	pSps->calcAdjust=gain_fir_lpf_215_9_88;

	pSps->inputGain=(1*M_Q8);
	pSps->outputGain=(1*M_Q8);
	pChan->prxCtcssMeasure=pSps->sink;
	pChan->prxCtcssAdjust=&(pSps->outputGain);

	// CTCSS CenterSlicer
	pSps=pChan->spsRxLsdNrz=pSps->nextSps=createPmrSps(pChan);
	pSps->source=pChan->pRxLsd;
	pSps->sink=pChan->pRxDcTrack;
	pSps->buff=pChan->pRxLsdLimit;
	pSps->sigProc=CenterSlicer;
	pSps->nSamples=pChan->nSamplesRx;
	pSps->discfactor=LSD_DFS;		  		// centering time constant
	pSps->inputGain=(1*M_Q8);
	pSps->outputGain=(1*M_Q8);
	pSps->setpt=4900;	  			// ptp clamp for DC centering
	pSps->inputGainB=625; 			// peak output limiter clip point
	pSps->enabled=0;


	// Rx HPF
	pSps=pSps->nextSps=createPmrSps(pChan);
	pChan->spsRxHpf=pSps;
	pSps->source=pChan->pRxBase;
	pSps->sink=pChan->pRxHpf;
	pSps->sigProc=pmr_gp_fir;
	pSps->enabled=1;
	pSps->numChanOut=1;
	pSps->selChanOut=0;
	pSps->nSamples=pChan->nSamplesRx;
	pSps->decimator=pSps->decimate=1;
	pSps->interpolate=1;
	pSps->ncoef=fir_rxhpf[pChan->rxhpf].taps;
	pSps->size_coef=2;
	pSps->coef=(void*)fir_rxhpf[pChan->rxhpf].coefs;
	pSps->nx=fir_rxhpf[pChan->rxhpf].taps;
	pSps->size_x=2;
	pSps->x = ast_calloc(pSps->nx, pSps->size_x);
	if (pSps->x == NULL) {
		; /* XXX do something here */
	}
	pSps->calcAdjust=fir_rxhpf[pChan->rxhpf].gain;
	pSps->inputGain=(1*M_Q8);
	pSps->outputGain=(1*M_Q8);
	pChan->prxVoiceAdjust=&(pSps->outputGain);
	pChan->spsRxOut=pSps;

	// allocate space for next sps and set pointers
	// Rx DeEmp
	if(pChan->rxDeEmpEnable){
		pSps=pSps->nextSps=createPmrSps(pChan);
		pChan->spsRxDeEmp=pSps;
		pSps->source=pChan->pRxHpf;
		pSps->sink=pChan->pRxSpeaker;
		pChan->spsRxOut=pSps;					 // OUTPUT STRUCTURE!
		pSps->sigProc=gp_inte_00;
		pSps->enabled=1;
		pSps->nSamples=pChan->nSamplesRx;

		pSps->ncoef=taps_int_lpf_300_1_2;
		pSps->size_coef=2;
		pSps->coef=(void*)coef_int_lpf_300_1_2;

		pSps->nx=taps_int_lpf_300_1_2;
		pSps->size_x=4;
		pSps->x = ast_calloc(pSps->nx, pSps->size_x);
		if (pSps->x == NULL) {
			; /* XXX do something here */
		}
		pSps->calcAdjust=gain_int_lpf_300_1_2/2;
		pSps->inputGain=(1.0*M_Q8);
		pSps->outputGain=(1.0*M_Q8);
		pChan->prxVoiceMeasure=pSps->sink;
		pChan->prxVoiceAdjust=&(pSps->outputGain);
	} else {
		// force delay to be true
		if (pChan->rxSquelchDelay == 0)
			pChan->rxSquelchDelay = 30;
	}

	if(pChan->rxSquelchDelay>RXSQDELAYBUFSIZE/8-1)
	{
		pChan->rxSquelchDelay=RXSQDELAYBUFSIZE/8-1;	
	}
	if(pChan->rxSquelchDelay>0)
	{
		TRACEF(1, "create rx squelch delay\n");
		pSps=pChan->spsDelayLine=pSps->nextSps=createPmrSps(pChan);
		pChan->spsRxSquelchDelay=pSps;
		pSps->sigProc=DelayLine;
		if (pChan->rxDeEmpEnable)
			pSps->source=pChan->pRxSpeaker;
		else
			pSps->source=pChan->pRxHpf;
		pSps->sink=pChan->pRxSpeaker;
		pChan->spsRxOut=pSps;					 // OUTPUT STRUCTURE!
		pSps->enabled=1;
		pSps->b.outzero=0;
		pSps->inputGain=1*M_Q8;
		pSps->outputGain=1*M_Q8;
		pSps->nSamples=pChan->nSamplesRx;
		pSps->buffSize=RXSQDELAYBUFSIZE;
		pSps->buff=ast_calloc(RXSQDELAYBUFSIZE,2);	 		 
		pSps->buffLead = pChan->rxSquelchDelay*8;  // convert ms to samples
		pSps->buffInIndex=0;
		pSps->buffOutIndex=0;
	}

	if(pChan->rxCdType==CD_XPMR_VOX)
	{
		TRACEF(1, "create vox measureblock\n");
		pChan->prxVoxMeas=ast_calloc(pChan->nSamplesRx,2);

		pSps=pChan->spsRxVox=pSps->nextSps=createPmrSps(pChan);
		pSps->sigProc=MeasureBlock;
		pSps->parentChan=pChan;
		pSps->source=pChan->pRxBase;
		pSps->sink=pChan->prxVoxMeas;
		pSps->inputGain=1*M_Q8;
		pSps->outputGain=1*M_Q8;
		pSps->nSamples=pChan->nSamplesRx;
		pSps->discfactor=3;
		if(pChan->rxSqVoxAdj==0)
			pSps->setpt=(0.011*M_Q15);
		else
			pSps->setpt=(pChan->rxSqVoxAdj);
		pSps->hyst=(pSps->setpt/10);
		pSps->enabled=1;
	}

	// tuning measure block
	pSps=pChan->spsMeasure=pSps->nextSps=createPmrSps(pChan);
	pSps->source=pChan->spsRx->sink;
	pSps->sink=pChan->prxMeasure;
	pSps->sigProc=MeasureBlock;
	pSps->enabled=0;
	pSps->nSamples=pChan->nSamplesRx;
	pSps->discfactor=10;

	pSps->nextSps=NULL;		// last sps in chain RX


	// CREATE TRANSMIT CHAIN
	TRACEF(1, "create tx\n");
	inputTmp=NULL;
	pSps = NULL;

	// allocate space for first sps and set pointers

	// Tx HPF SubAudible
	if(pChan->txHpfEnable)
	{
		pSps=createPmrSps(pChan);
		pChan->spsTx=pSps;
		pSps->source=pChan->pTxBase;
		pSps->sink=pChan->pTxHpf;
		pSps->sigProc=pmr_gp_fir;
		pSps->enabled=1;
		pSps->numChanOut=1;
		pSps->selChanOut=0;
		pSps->nSamples=pChan->nSamplesTx;
		pSps->decimator=pSps->decimate=1;
		pSps->interpolate=1;
		pSps->ncoef=fir_txhpf[pChan->txhpf].taps;
		pSps->size_coef=2;
		pSps->coef=(void*)fir_txhpf[pChan->txhpf].coefs;
		pSps->nx=fir_txhpf[pChan->txhpf].taps;
		pSps->size_x=2;
		pSps->x = ast_calloc(pSps->nx, pSps->size_x);
		if (pSps->x == NULL) {
			; /* XXX do something here */
		}
		pSps->calcAdjust=fir_txhpf[pChan->txhpf].gain;
		pSps->inputGain=(1*M_Q8);
		pSps->outputGain=(1*M_Q8);
		inputTmp=pChan->pTxHpf;
	}

	// Tx PreEmphasis
	if(pChan->txPreEmpEnable)
	{
		if(pSps==NULL) pSps=pChan->spsTx=createPmrSps(pChan);
		else pSps=pSps->nextSps=createPmrSps(pChan);

		pSps->source=inputTmp;
		pSps->sink=pChan->pTxPreEmp;

		pSps->sigProc=gp_diff;
		pSps->enabled=1;
		pSps->nSamples=pChan->nSamplesTx;

		pSps->ncoef=taps_int_hpf_4000_1_2;
		pSps->size_coef=2;
		pSps->coef=(void*)coef_int_hpf_4000_1_2;

		pSps->nx=taps_int_hpf_4000_1_2;
		pSps->size_x=2;
		pSps->x = ast_calloc(pSps->nx, pSps->size_x);
		if (pSps->x == NULL) {
			; /* XXX do something here */
		}
		pSps->calcAdjust=gain_int_hpf_4000_1_2;
		pSps->inputGain=(1*M_Q8);
		pSps->outputGain=(1*M_Q8);	 // to match flat at 1KHz 
		inputTmp=pSps->sink;
	}

	// Tx Limiter
	if(pChan->txLimiterEnable)
	{
		if(pSps==NULL) pSps=pChan->spsTx=createPmrSps(pChan);
		else pSps=pSps->nextSps=createPmrSps(pChan);
		pSps->source=inputTmp;
		pSps->sink=pChan->pTxLimiter;
		pSps->sigProc=SoftLimiter;
		pSps->enabled=1;
		pSps->nSamples=pChan->nSamplesTx;
		pSps->inputGain=(1*M_Q8);
		pSps->outputGain=(1*M_Q8);
		pSps->setpt=12000;			// limiting point for 100 modulation		
		inputTmp=pSps->sink;
	}

	// Composite Mix of Voice and LSD
	if((pChan->txMixA==TX_OUT_COMPOSITE)||(pChan->txMixB==TX_OUT_COMPOSITE))
	{
		if(pSps==NULL)
			pSps=pChan->spsTx=createPmrSps(pChan);
		else
			pSps=pSps->nextSps=createPmrSps(pChan);
		pSps->source=inputTmp;
		pSps->sourceB=pChan->pTxLsdLpf;		 //asdf ??? !!! maw pTxLsdLpf
		pSps->sink=pChan->pTxComposite;
		pSps->sigProc=pmrMixer;
		pSps->enabled=1;
		pSps->nSamples=pChan->nSamplesTx;
		pSps->inputGain=2*M_Q8;
		pSps->inputGainB=1*M_Q8/8;
		pSps->outputGain=1*M_Q8;
		pSps->setpt=0;
		inputTmp=pSps->sink;
		pChan->ptxCtcssAdjust=&pSps->inputGainB;
	}
	else
	{
		if (pChan->b.txboost)
		{
			if(pSps==NULL)
				pSps=pChan->spsTx=createPmrSps(pChan);
			else
				pSps=pSps->nextSps=createPmrSps(pChan);
			pSps->source=inputTmp;
			pSps->sourceB=NULL;
			pSps->sink=pChan->pTxComposite;
			pSps->sigProc=pmrMixer;
			pSps->enabled=1;
			pSps->nSamples=pChan->nSamplesTx;
			pSps->inputGain=2*M_Q8;
			pSps->inputGainB=0;
			pSps->outputGain=1*M_Q8;
			pSps->setpt=0;
			inputTmp=pSps->sink;
		}
	}

	// Chan A Upsampler and Filter
	if(pSps==NULL) pSps=pChan->spsTx=createPmrSps(pChan);
	else pSps=pSps->nextSps=createPmrSps(pChan);

	pChan->spsTxOutA=pSps;
	if(!pChan->spsTx)pChan->spsTx=pSps;

	if(pChan->txMixA==TX_OUT_COMPOSITE)
	{
		pSps->source=pChan->pTxComposite;
	}
	else if(pChan->txMixA==TX_OUT_LSD)
	{
		pSps->source=pChan->pTxLsdLpf;
	}
	else if(pChan->txMixA==TX_OUT_VOICE)
	{
		pSps->source=inputTmp;
	}
	else if (pChan->txMixA==TX_OUT_AUX)
	{
		pSps->source=pChan->pTxHpf;
	}
	else
	{
		pSps->source=NULL;		// maw sph asdf !!!	no blow up
		pSps->source=inputTmp;
	}

	pSps->sink=pChan->pTxOut;
	pSps->sigProc=pmr_gp_fir;
	pSps->enabled=1;
	pSps->numChanOut=2;
	pSps->selChanOut=0;
	pSps->nSamples=pChan->nSamplesTx;
#ifdef	XPMR_VOTER
	pSps->interpolate=1;
	pSps->ncoef=taps_fir_lpf_3K_2;
	pSps->size_coef=2;
	pSps->coef=(void*)coef_fir_lpf_3K_2;
	pSps->nx=taps_fir_lpf_3K_2;
	pSps->calcAdjust=gain_fir_lpf_3K_2;
#else
	pSps->interpolate=6;
	pSps->ncoef=fir_txlpf[pChan->txlpf].taps;
	pSps->size_coef=2;
	pSps->coef=(void*)fir_txlpf[pChan->txlpf].coefs;
	pSps->nx=fir_txlpf[pChan->txlpf].taps;
	pSps->calcAdjust=fir_txlpf[pChan->txlpf].gain;
#endif
	pSps->size_x=2;
	pSps->x = ast_calloc(pSps->nx, pSps->size_x);
	if (pSps->x == NULL) {
		; /* XXX do something here */
	}
	pSps->inputGain=(1*M_Q8);
	pSps->outputGain=(1*M_Q8);
	if(pChan->txMixA==pChan->txMixB)pSps->monoOut=1;
	else pSps->monoOut=0;


	// Chan B Upsampler and Filter
	if((pChan->txMixA!=pChan->txMixB)&&(pChan->txMixB!=TX_OUT_OFF))
	{
		if(pSps==NULL) pSps=pChan->spsTx=createPmrSps(pChan);
		else pSps=pSps->nextSps=createPmrSps(pChan);

		pChan->spsTxOutB=pSps;
		if(pChan->txMixB==TX_OUT_COMPOSITE)
		{
			pSps->source=pChan->pTxComposite;
		}
		else if(pChan->txMixB==TX_OUT_LSD)
		{
			pSps->source=pChan->pTxLsdLpf;
			// pChan->ptxCtcssAdjust=&pSps->inputGain;
		}
		else if(pChan->txMixB==TX_OUT_VOICE)
		{
			pSps->source=inputTmp;
		}
		else if(pChan->txMixB==TX_OUT_AUX)
		{
			pSps->source=pChan->pTxHpf;
		}
		else
		{
			pSps->source=NULL;
		}

		pSps->sink=pChan->pTxOut;
		pSps->sigProc=pmr_gp_fir;
		pSps->enabled=1;
		pSps->numChanOut=2;
		pSps->selChanOut=1;
		pSps->mixOut=0;
		pSps->nSamples=pChan->nSamplesTx;
#ifdef	XPMR_VOTER
		pSps->interpolate=1;
		pSps->ncoef=taps_fir_lpf_3K_2;
		pSps->size_coef=2;
		pSps->coef=(void*)coef_fir_lpf_3K_2;
		pSps->nx=taps_fir_lpf_3K_2;
		pSps->calcAdjust=(gain_fir_lpf_3K_2);
#else
		pSps->interpolate=6;
		pSps->ncoef=fir_txlpf[pChan->txlpf].taps;
		pSps->size_coef=2;
		pSps->coef=(void*)fir_txlpf[pChan->txlpf].coefs;
		pSps->nx=fir_txlpf[pChan->txlpf].taps;
		pSps->calcAdjust=(fir_txlpf[pChan->txlpf].gain);
#endif
		pSps->size_x=2;
		pSps->x = ast_calloc(pSps->nx, pSps->size_x);
		if (pSps->x == NULL) {
			; /* XXX do something here */
		}
		pSps->inputGain=(1*M_Q8);
		pSps->outputGain=(1*M_Q8);
	}

	pSps->nextSps=NULL;

	// Configure Coded Signaling
	code_string_parse(pChan);

	pChan->smode=SMODE_NULL;
	pChan->smodewas=SMODE_NULL;
	pChan->smodetime=2500;
	pChan->smodetimer=0;
	pChan->b.smodeturnoff=0;

	pChan->txsettletimer=0;
	pChan->txrxblankingtimer=0;

	TRACEF(1, "createPmrChannel() end\n");

	return pChan;
}
/*
*/
i16 destroyPmrChannel(t_pmr_chan *pChan)
{
	t_pmr_sps  	*pmr_sps, *tmp_sps;
	i16 i;

	TRACEF(1, "destroyPmrChannel()\n");

	ast_free(pChan->pRxDemod);
	ast_free(pChan->pRxNoise);
	ast_free(pChan->pRxBase);
	ast_free(pChan->pRxHpf);
	ast_free(pChan->pRxLsd);
	ast_free(pChan->pRxSpeaker);
	ast_free(pChan->pRxDcTrack);
	if(pChan->pRxLsdLimit)ast_free(pChan->pRxLsdLimit);
	ast_free(pChan->pTxBase);
	ast_free(pChan->pTxHpf);
	ast_free(pChan->pTxPreEmp);
	ast_free(pChan->pTxLimiter);
	ast_free(pChan->pTxLsd);
	ast_free(pChan->pTxLsdLpf);
	if(pChan->pTxComposite)ast_free(pChan->pTxComposite);
	ast_free(pChan->pTxOut);

	if(pChan->prxMeasure)ast_free(pChan->prxMeasure);
	if(pChan->pSigGen0)ast_free(pChan->pSigGen0);
	if(pChan->pSigGen1)ast_free(pChan->pSigGen1);
	 

	#if XPMR_DEBUG0 == 1
	//if(pChan->prxDebug)ast_free(pChan->prxDebug);
	if(pChan->ptxDebug)ast_free(pChan->ptxDebug);
	ast_free(pChan->prxDebug0);
 	ast_free(pChan->prxDebug1);
	ast_free(pChan->prxDebug2);
	ast_free(pChan->prxDebug3);

	ast_free(pChan->ptxDebug0);
 	ast_free(pChan->ptxDebug1);
	ast_free(pChan->ptxDebug2);
	ast_free(pChan->ptxDebug3);

	ast_free(pChan->rxCtcss->pDebug0);
	ast_free(pChan->rxCtcss->pDebug1);

	for(i=0;i<CTCSS_NUM_CODES;i++)
	{
		ast_free(pChan->rxCtcss->tdet[i].pDebug0);
		ast_free(pChan->rxCtcss->tdet[i].pDebug1);
		ast_free(pChan->rxCtcss->tdet[i].pDebug2);
		ast_free(pChan->rxCtcss->tdet[i].pDebug3);
	}
	#endif

	pChan->dd.option=8;
	dedrift(pChan);

	ast_free(pChan->pRxCtcss);

	pmr_sps=pChan->spsRx;

	if(pChan->sdbg)ast_free(pChan->sdbg);

	while(pmr_sps)
	{
		tmp_sps = pmr_sps;
		pmr_sps = tmp_sps->nextSps;
		destroyPmrSps(tmp_sps);
	}

	ast_free(pChan);

	return 0;
}
/*
*/
t_pmr_sps *createPmrSps(t_pmr_chan *pChan)
{
	t_pmr_sps  *pSps;

	TRACEF(1, "createPmrSps()\n");

	pSps = (t_pmr_sps *)ast_calloc(sizeof(t_pmr_sps),1);

	if (!pSps) {
		ast_log(LOG_ERROR, "Error: createPmrSps()\n");
	}

	pSps->parentChan=pChan;
	pSps->index=pChan->spsIndex++;

	return pSps;
}
/*
*/
i16 destroyPmrSps(t_pmr_sps  *pSps)
{
	TRACEJ(1, "destroyPmrSps(%i)\n", pSps->index);

	if(pSps->x!=NULL)ast_free(pSps->x);
	ast_free(pSps);
	return 0;
}
/*
	PmrTx - takes data from network and holds it for PmrRx
*/
i16 PmrTx(t_pmr_chan *pChan, i16 *input)
{
	pChan->frameCountTx++;

	TRACEF(5, "PmrTx() start %i\n", pChan->frameCountTx);

#if XPMR_PPTP == 99
	pptp_p2^=1;
	if(pptp_p2)ioctl(ppdrvdev,PPDRV_IOC_PINSET,LP_PIN02);
	else ioctl(ppdrvdev,PPDRV_IOC_PINCLEAR,LP_PIN02);
	#endif

	if(pChan==NULL){
		ast_log(LOG_ERROR, "PmrTx() pChan == NULL\n");
		return 1;
	}

	#if XPMR_DEBUG0 == 1
	if(pChan->b.rxCapture && pChan->tracetype==5)
	{
		memcpy(pChan->pTxInput,input,pChan->nSamplesRx*2);
	}
	#endif

	dedrift_write(pChan,input);

	return 0;
}
/*
	PmrRx handles a block of data from the usb audio device
*/
i16 PmrRx(t_pmr_chan *pChan, i16 *input, i16 *outputrx, i16 *outputtx)
{
	int i,hit;
	float f=0;
	t_pmr_sps *pmr_sps;

	TRACEC(5, "PmrRx(%p %p %p %p)\n", pChan, input, outputrx, outputtx);

#if XPMR_PPTP == 1
	if(pChan->b.radioactive)
	{
		pptp_write(1,pChan->frameCountRx&0x00000001);
	}
	#endif

	if(pChan==NULL){
		ast_log(LOG_ERROR, "PmrRx() pChan == NULL\n");
		return 1;
	}

	pChan->frameCountRx++;

	#if XPMR_DEBUG0 == 1
	if(pChan->b.rxCapture)
	{
		//if(pChan->prxDebug)memset((void *)pChan->prxDebug,0,pChan->nSamplesRx*XPMR_DEBUG_CHANS*2);
		if(pChan->ptxDebug)memset((void *)pChan->ptxDebug,0,pChan->nSamplesRx*XPMR_DEBUG_CHANS*2);

		memset((void *)pChan->sdbg->buffer,0,pChan->nSamplesRx*XPMR_DEBUG_CHANS*2);
		pChan->prxDebug=pChan->sdbg->buffer;
	}
	#endif

#ifndef XPMR_VOTER
	pmr_sps=pChan->spsRx;		// first sps
	pmr_sps->source=input;

	if(outputrx!=NULL)pChan->spsRxOut->sink=outputrx;	 //last sps

	if(pChan->txrxblankingtimer>0){
		for (i = 0; i < pChan->nSamplesRx * 6; i++)
			input[i] = 0;

		pChan->txrxblankingtimer -= MS_PER_FRAME;
		if (pChan->txrxblankingtimer <= 0) {
			pChan->txrxblankingtimer = 0;
			TRACEC(1, "TXRXBLANKING TIME OUT **********\n");
		}
	}

	#if 0
	if(pChan->inputBlanking>0)
	{
		pChan->inputBlanking-=pChan->nSamplesRx;
		if(pChan->inputBlanking<0)pChan->inputBlanking=0;
		for(i=0;i<pChan->nSamplesRx*6;i++)
			input[i]=0;
	}
	#endif

	if( pChan->rxCpuSaver && !pChan->rxCarrierDetect && 
	    pChan->smode==SMODE_NULL &&
	   !pChan->txPttIn && !pChan->txPttOut)
	{
		if(!pChan->b.rxhalted)
		{
			if(pChan->spsRxHpf)pChan->spsRxHpf->enabled=0;
			if(pChan->spsRxDeEmp)pChan->spsRxDeEmp->enabled=0;
			pChan->b.rxhalted=1;
			TRACEC(1, "PmrRx() rx sps halted\n");
		}
	}
	else if(pChan->b.rxhalted)
	{
		if(pChan->spsRxHpf)pChan->spsRxHpf->enabled=1;
		if(pChan->spsRxDeEmp)pChan->spsRxDeEmp->enabled=1;
		pChan->b.rxhalted=0;
		TRACEC(1, "PmrRx() rx sps un-halted\n");
	}

	i=0;
	while(pmr_sps!=NULL && pmr_sps!=0)
	{
		TRACEC(5, "PmrRx() sps %i\n", i++);
		pmr_sps->sigProc(pmr_sps);
		pmr_sps = (t_pmr_sps *)(pmr_sps->nextSps);
		//pmr_sps=NULL;	// sph maw
	}

	if(pChan->rxCdType==CD_XPMR_VOX)
	{
		if(pChan->spsRxVox->compOut)
		{
			pChan->rxVoxTimer=pChan->voxHangTime;	/* VOX HangTime in ms */
		}
		if(pChan->rxVoxTimer>0)
		{
			pChan->rxVoxTimer-=MS_PER_FRAME;
			pChan->rxCarrierDetect=1;
		}
		else
		{
			pChan->rxVoxTimer=0;
			pChan->rxCarrierDetect=0;
		}
	}
	else
	{
		pChan->rxCarrierDetect=!pChan->spsRx->compOut;
		if(pChan->rxSquelchDelay)
			pChan->spsRxSquelchDelay->b.outzero=pChan->spsRx->compOut;
	}

	// stop and start these engines instead to eliminate falsing
	if( pChan->b.ctcssRxEnable && 
	    ( (!pChan->b.rxhalted || 
		   pChan->rxCtcss->decode!=CTCSS_NULL || pChan->smode==SMODE_CTCSS) &&
		(pChan->smode!=SMODE_DCS&&pChan->smode!=SMODE_LSD) ) 
	  )
	{
		ctcss_detect(pChan);
	}

	#if 1
	if(pChan->txPttIn!=pChan->b.pttwas)
	{
		pChan->b.pttwas=pChan->txPttIn;
		TRACEC(1, "PmrRx() txPttIn=%i\n", pChan->b.pttwas);
	}
	#endif

	#ifdef XPMRX_H
	xpmrx(pChan,XXO_RXDECODE);
	#endif

	if(pChan->smodetimer>0 && !pChan->txPttIn)
	{
		pChan->smodetimer-=MS_PER_FRAME;
	 	
		if(pChan->smodetimer<=0)
		{
			pChan->smodetimer=0;
			pChan->smodewas=pChan->smode;
			pChan->smode=SMODE_NULL;
			pChan->b.smodeturnoff=1;
			TRACEC(1, "smode timeout. smode was=%i\n", pChan->smodewas);
		}
	}

	if(pChan->rxCtcss->decode > CTCSS_NULL && 
	   (pChan->smode==SMODE_NULL||pChan->smode==SMODE_CTCSS) )
	{
		if(pChan->smode!=SMODE_CTCSS)
		{
			TRACEC(1, "smode set=%i  code=%i\n", pChan->smode, pChan->rxCtcss->decode);
			pChan->smode=pChan->smodewas=SMODE_CTCSS;
		}
		pChan->smodetimer=pChan->smodetime;
	}
	if(pChan->smode==SMODE_CTCSS)
	{
		if(pChan->rxCtcss->decode != pChan->lastrxdecode)
		{
			pChan->lastrxdecode = pChan->rxCtcss->decode;
			f = 0;
			if(pChan->rxCtcss->decode>CTCSS_NULL)
			{
				if(pChan->rxCtcssMap[pChan->rxCtcss->decode]!=CTCSS_RXONLY)
				{
					f=freq_ctcss[pChan->rxCtcssMap[pChan->rxCtcss->decode]];
				}
			}
			else
			{
				f=pChan->txctcssdefault_value;	
			}
			if (f && pChan->spsSigGen0->freq != f*10)
			{
				pChan->spsSigGen0->freq=f*10;
				pChan->spsSigGen0->option=1;
			}
		}
	}
	else
	{
		pChan->lastrxdecode = CTCSS_NULL;

	}
	#ifdef HAVE_XPMRX
	xpmrx(pChan,XXO_LSDCTL);
	#endif

#endif
	// TRACEX((1, "PmrRx() tx portion.\n"));

	// handle radio transmitter ptt input
	hit=0;
	if( !(pChan->smode==SMODE_DCS||pChan->smode==SMODE_LSD) )
	{

	if( pChan->txPttIn && (pChan->txState==CHAN_TXSTATE_IDLE ))
	{
		TRACEC(1,
			"txPttIn==1 from CHAN_TXSTATE_IDLE && !SMODE_LSD. codeindex=%i  %i \n",
			pChan->rxCtcss->decode,
			pChan->rxCtcssMap[pChan->rxCtcss->decode]);
		pChan->dd.b.doitnow=1;
		pChan->spsSigGen0->freq=0;
	    if(pChan->smode==SMODE_CTCSS && !pChan->b.txCtcssInhibit)
		{
			if(pChan->rxCtcss->decode>CTCSS_NULL)
			{
				if(pChan->rxCtcssMap[pChan->rxCtcss->decode]!=CTCSS_RXONLY)
				{
					f=freq_ctcss[pChan->rxCtcssMap[pChan->rxCtcss->decode]];
				}
			}
			else
			{
				f=pChan->txctcssdefault_value;	
			}
			TRACEC(1, "txPttIn - Start CTCSSGen  %f \n", f);
			if(f)
			{
				t_pmr_sps *pSps;
	
				pChan->spsSigGen0->freq=f*10;
				pSps=pChan->spsTxLsdLpf;
				pSps->enabled=1;

				#if 0
				if(f>203.0)
				{
					pSps->ncoef=taps_fir_lpf_250_9_66;
					pSps->size_coef=2;
					pSps->coef=(void*)coef_fir_lpf_250_9_66;
					pSps->nx=taps_fir_lpf_250_9_66;
					pSps->size_x=2;
					pSps->x= ast_calloc(pSps->nx,pSps->size_x);
					if (pSps->x == NULL) {
						; /* XXX do something here */
					}

					pSps->calcAdjust=gain_fir_lpf_250_9_66;
				}
				else
				{
					pSps->ncoef=taps_fir_lpf_215_9_88;
					pSps->size_coef=2;
					pSps->coef=(void*)coef_fir_lpf_215_9_88;
					pSps->nx=taps_fir_lpf_215_9_88;
					pSps->size_x=2;
					pSps->x= ast_calloc(pSps->nx,pSps->size_x);
					if (pSps->x == NULL) {
						; /* XXX do something here */
					}
					pSps->calcAdjust=gain_fir_lpf_215_9_88;
				}
				#endif

				pChan->spsSigGen0->option=1;
				pChan->spsSigGen0->enabled=1;
			    pChan->spsSigGen0->discounterl=0;
		    }
		}
		else if(pChan->smode==SMODE_NULL && pChan->txcodedefaultsmode==SMODE_CTCSS && !pChan->b.txCtcssInhibit)
		{
			TRACEC(1, "txPtt Encode txcodedefaultsmode==SMODE_CTCSS %f\n", pChan->txctcssdefault_value);
			f=pChan->txctcssdefault_value;
			pChan->spsSigGen0->freq=f*10;
			pChan->spsSigGen0->option=1;
		    pChan->spsSigGen0->enabled=1;
			pChan->spsSigGen0->discounterl=0;
			pChan->smode=SMODE_CTCSS;
			pChan->smodetimer=pChan->smodetime;
		}
		else if(pChan->txcodedefaultsmode==SMODE_NULL||pChan->b.txCtcssInhibit)
		{
			TRACEC(1, "txPtt Encode txcodedefaultsmode==SMODE_NULL\n");
		}
		else
		{
			TRACEC(1, "txPttIn=%i NOT HANDLED PROPERLY.\n", pChan->txPttIn);
		}

		memset(pChan->txctcssfreq,0,sizeof(pChan->txctcssfreq));
		sprintf(pChan->txctcssfreq,"%.1f",f);
		pChan->b.txCtcssReady = 1;

		pChan->txState = CHAN_TXSTATE_ACTIVE;
		pChan->txPttOut=1;

		pChan->txsettletimer=pChan->txsettletime;

		if(pChan->spsTxOutA)pChan->spsTxOutA->enabled=1;
		if(pChan->spsTxOutB)pChan->spsTxOutB->enabled=1;
		if(pChan->spsTxLsdLpf)pChan->spsTxLsdLpf->enabled=1;
		if(pChan->txfreq)pChan->b.reprog=1;
		TRACEC(1, "PmrRx() TxOn\n");
	}
	else if(pChan->txPttIn && pChan->txState==CHAN_TXSTATE_ACTIVE)
	{
		// pChan->smode=SMODE_CTCSS;
		pChan->smodetimer=pChan->smodetime;
	}
	else if(!pChan->txPttIn && pChan->txState==CHAN_TXSTATE_ACTIVE)
	{
		TRACEC(1, "txPttIn==0 from CHAN_TXSTATE_ACTIVE\n");
		if(pChan->smode==SMODE_CTCSS && !pChan->b.txCtcssInhibit)
		{
			if( pChan->txTocType==TOC_NONE || !pChan->b.ctcssTxEnable )
			{
				TRACEC(1, "Tx Off Immediate.\n");
				pChan->spsSigGen0->option=3;
				pChan->txBufferClear=3;
				pChan->txState=CHAN_TXSTATE_FINISHING;
	        }
			else if(pChan->txTocType==TOC_NOTONE)
			{
				pChan->txState=CHAN_TXSTATE_TOC;
				pChan->txHangTime=TOC_NOTONE_TIME/MS_PER_FRAME;
				pChan->spsSigGen0->option=3;
				TRACEC(1, "Tx Turn Off No Tone Start.\n");
			}
	 		else
			{
				pChan->txState=CHAN_TXSTATE_TOC;
				pChan->txHangTime=0;
				pChan->spsSigGen0->option=2;
				TRACEC(1, "Tx Turn Off Phase Shift Start.\n");
			}
	    }
		else
		{
		    pChan->txBufferClear=3;
			pChan->txState=CHAN_TXSTATE_FINISHING;
			TRACEC(1, "Tx Off No SMODE to Finish.\n");
		}
	}
	else if(pChan->txState==CHAN_TXSTATE_TOC)
	{
		if( pChan->txPttIn && pChan->smode==SMODE_CTCSS )
		{
			TRACEC(1, "Tx Key During HangTime\n");
			pChan->txState = CHAN_TXSTATE_ACTIVE;
			pChan->spsSigGen0->option=1;
			pChan->spsSigGen0->enabled=1;
			pChan->spsSigGen0->discounterl=0;
			hit=0;
		}
		else if(pChan->txHangTime)
		{
			if(--pChan->txHangTime==0)pChan->txState=CHAN_TXSTATE_FINISHING;
		}
		else if(pChan->txHangTime<=0 && pChan->spsSigGen0->state==0)
		{
			pChan->txBufferClear=3;
			pChan->txState=CHAN_TXSTATE_FINISHING;
			TRACEC(1, "Tx Off TOC.\n");
		}
	}
	else if(pChan->txState==CHAN_TXSTATE_FINISHING)
	{
		if(--pChan->txBufferClear<=0)
			pChan->txState=CHAN_TXSTATE_COMPLETE;
	}
	else if(pChan->txState==CHAN_TXSTATE_COMPLETE)
	{
		hit=1;	
	}
	}	// end of if SMODE==LSD

	if(hit)
	{
		pChan->txPttOut=0;
		pChan->spsSigGen0->option=3;
		pChan->txrxblankingtimer=pChan->txrxblankingtime;
		TRACEC(1, "PmrRx() txrxblankingtimer=%i\n", pChan->txrxblankingtimer);
		pChan->txState=CHAN_TXSTATE_IDLE;
		if(pChan->spsTxLsdLpf)pChan->spsTxLsdLpf->option=3;
		if(pChan->spsTxOutA)pChan->spsTxOutA->option=3;
		if(pChan->spsTxOutB)pChan->spsTxOutB->option=3;
		if(pChan->rxfreq||pChan->txfreq)pChan->b.reprog=1;
		memset(pChan->txctcssfreq,0,sizeof(pChan->txctcssfreq));
		pChan->b.txCtcssReady = 1;
		TRACEC(1, "Tx Off hit.\n");
	}
			  
	if(pChan->b.reprog)
	{
		pChan->b.reprog=0;	
		progdtx(pChan);
	}

	if(pChan->txsettletimer && pChan->txPttHid )
	{
		pChan->txsettletimer-=MS_PER_FRAME;
		if(pChan->txsettletimer<0)pChan->txsettletimer=0;
	}

	// enable this after we know everything else is working
	if( pChan->txCpuSaver && 
	    !pChan->txPttIn && !pChan->txPttOut && 
	    pChan->txState==CHAN_TXSTATE_IDLE &&
	    !pChan->dd.b.doitnow 
	    ) 
	{
		if(!pChan->b.txhalted)
		{
			pChan->b.txhalted=1;
			TRACEC(1, "PmrRx() tx sps halted\n");
		}
	}
	else if(pChan->b.txhalted)
	{
		pChan->dd.b.doitnow=1;
		pChan->b.txhalted=0;
		TRACEC(1, "PmrRx() tx sps un-halted\n");
	}

	if(pChan->b.txhalted)return(1);

	if(pChan->b.startSpecialTone)
	{
		pChan->b.startSpecialTone=0;
		pChan->spsSigGen1->option=1;
		pChan->spsSigGen1->enabled=1;
		pChan->b.doingSpecialTone=1;
	} 
	else if(pChan->b.stopSpecialTone)
	{
		pChan->b.stopSpecialTone=0;
		pChan->spsSigGen1->option=0;
		pChan->b.doingSpecialTone=0;
		pChan->spsSigGen1->enabled=0;
	} 
	else if(pChan->b.doingSpecialTone)
	{
		pChan->spsSigGen1->sink=outputtx;
		pChan->spsSigGen1->sigProc(pChan->spsSigGen1);
		for(i=0;i<(pChan->nSamplesTx*2*6);i+=2)outputtx[i+1]=outputtx[i];
		return 0;
	}

	if(pChan->spsSigGen0 && pChan->spsSigGen0->enabled)
	{
		pChan->spsSigGen0->b.mute = pChan->b.txCtcssOff;
		pChan->spsSigGen0->sigProc(pChan->spsSigGen0);
	}

	if(pChan->spsSigGen1 && pChan->spsSigGen1->enabled)
	{
		pChan->spsSigGen1->sigProc(pChan->spsSigGen1);
	}

	#ifdef XPMRX_H
	pChan->spsLsdGen->sigProc(pChan->spsLsdGen);	// maw sph ???
	#endif

	// Do Low Speed Data Low Pass Filter
	pChan->spsTxLsdLpf->sigProc(pChan->spsTxLsdLpf);

	// Do Voice
	pmr_sps=pChan->spsTx;

	// get tx data from de-drift process
	pChan->dd.option=0;
	pChan->dd.ptr=pChan->pTxBase;
	dedrift(pChan);

	// tx process
	if(!pChan->spsSigGen1->enabled)
	{
		pmr_sps->source=pChan->pTxBase;
	}
	else input=pmr_sps->source;

	if(outputtx!=NULL)
	{
		if(pChan->spsTxOutA)pChan->spsTxOutA->sink=outputtx;
		if(pChan->spsTxOutB)pChan->spsTxOutB->sink=outputtx;
	}

	i=0;
	while(pmr_sps!=NULL && pmr_sps!=0)
	{
		// TRACEF(1,"PmrTx() sps %i\n",i++);
		pmr_sps->sigProc(pmr_sps);
		pmr_sps = (t_pmr_sps *)(pmr_sps->nextSps);
	}

	// TRACEF(1,"PmrTx() - outputs \n");
	if(pChan->txMixA==TX_OUT_OFF || !pChan->txPttOut){
		for(i=0;i<pChan->nSamplesTx*2*6;i+=2)outputtx[i]=0;
	}

	if(pChan->txMixB==TX_OUT_OFF || !pChan->txPttOut ){
		for(i=0;i<pChan->nSamplesTx*2*6;i+=2)outputtx[i+1]=0;
	}

	#if XPMR_PPTP == 1
	if(	pChan->b.radioactive && pChan->b.pptp_p1!=pChan->txPttOut)
	{
		pChan->b.pptp_p1=pChan->txPttOut;
		pptp_write(0,pChan->b.pptp_p1);
	}
	#endif

	#if XPMR_DEBUG0 == 1
	// TRACEF(1,"PmrRx() - debug outputs \n");
	if(pChan->b.rxCapture){
		for(i=0;i<pChan->nSamplesRx;i++)
		{
			pChan->pRxDemod[i]=input[i*2*6];
			pChan->pTstTxOut[i]=outputtx[i*2*6+0]; // txa
			//pChan->pTstTxOut[i]=outputtx[i*2*6+1]; // txb
			TSCOPE((RX_NOISE_TRIG, pChan->sdbg, i, (pChan->rxCarrierDetect*XPMR_TRACE_AMP)-XPMR_TRACE_AMP/2));
			TSCOPE((RX_CTCSS_DECODE, pChan->sdbg, i, pChan->rxCtcss->decode*(M_Q14/CTCSS_NUM_CODES)));
			TSCOPE((RX_SMODE, pChan->sdbg, i, pChan->smode*(XPMR_TRACE_AMP/4)));
			TSCOPE((TX_PTT_IN, pChan->sdbg, i, (pChan->txPttIn*XPMR_TRACE_AMP)-XPMR_TRACE_AMP/2));
			TSCOPE((TX_PTT_OUT, pChan->sdbg, i, (pChan->txPttOut*XPMR_TRACE_AMP)-XPMR_TRACE_AMP/2));
			TSCOPE((TX_DEDRIFT_LEAD, pChan->sdbg, i, pChan->dd.lead*8));
			TSCOPE((TX_DEDRIFT_ERR, pChan->sdbg, i, pChan->dd.err*16));
			TSCOPE((TX_DEDRIFT_FACTOR, pChan->sdbg, i, pChan->dd.factor*16));
			TSCOPE((TX_DEDRIFT_DRIFT, pChan->sdbg, i, pChan->dd.drift*16));
		}
    }
	#endif

	strace2(pChan->sdbg);
	TRACEC(5,
		"PmrRx() return  cd=%i smode=%i  txPttIn=%i  txPttOut=%i \n",
		pChan->rxCarrierDetect,
		pChan->smode,
		pChan->txPttIn,
		pChan->txPttOut);
	return 0;
}
/*
	parallel binary programming of an RF Transceiver*/

void	ppbinout	(u8 chan)
{
#if(DTX_PROG == 1)
	i32	i;

	if (ppdrvdev == 0)
    	ppdrvdev = open("/dev/ppdrv_device", 0);

    if (ppdrvdev < 0)
    {
		ast_debug(LOG_ERROR, "open /dev/ppdrv_ppdrvdev returned %i\n", ppdrvdev);
		return;
	}

	i=0;
	if(chan&0x01)i|=BIN_PROG_0;
	if(chan&0x02)i|=BIN_PROG_1;
	if(chan&0x04)i|=BIN_PROG_2;
	if(chan&0x08)i|=BIN_PROG_3;

	ioctl(ppdrvdev, PPDRV_IOC_PINMODE_OUT, BIN_PROG_3|BIN_PROG_2|BIN_PROG_1|BIN_PROG_0);
	//ioctl(ppdrvdev, PPDRV_IOC_PINCLEAR,    BIN_PROG_3|BIN_PROG_2|BIN_PROG_1|BIN_PROG_0);
	//ioctl(ppdrvdev, PPDRV_IOC_PINSET, i );
	ioctl(ppdrvdev, PPDRV_IOC_PINSET,    BIN_PROG_3|BIN_PROG_2|BIN_PROG_1|BIN_PROG_0);
	ioctl(ppdrvdev, PPDRV_IOC_PINCLEAR, i );

    // ioctl(ppdrvdev, PPDRV_IOC_PINSET, BIN_PROG_3|BIN_PROG_2|BIN_PROG_1|BIN_PROG_0 ); 
    ast_log(LOG_NOTICE, "mask=%i 0x%x\n",i,i); 
#endif
}
/*
	SPI Programming of an RF Transceiver
	need to add permissions check and mutex
*/
/*
	need to add permissions check and mutex
*/
void	ppspiout	(u32 spidata)
{
#if(DTX_PROG == 1)
	static char firstrun=0;
	i32	i,ii;
	u32	bitselect;

    if (ppdrvdev < 0)
    {
		ast_debug(LOG_ERROR, "no parallel port permission ppdrvdev %i\n", ppdrvdev);
		exit(0);
	}

	ioctl(ppdrvdev, PPDRV_IOC_PINMODE_OUT, DTX_CLK | DTX_DATA | DTX_ENABLE | DTX_TXPWR | DTX_TX );
	ioctl(ppdrvdev, PPDRV_IOC_PINCLEAR, DTX_CLK | DTX_DATA | DTX_ENABLE | DTX_TXPWR | DTX_TX );

	if(firstrun==0)
	{
		firstrun=1;
		for(ii=0;ii<PP_BIT_TIME*200;ii++);	
	}
	else
	{
		for(ii=0;ii<PP_BIT_TIME*4;ii++);
	}

	bitselect=0x00080000;

	for(i=0;i<(PP_REG_LEN-12);i++)
	{
		if((bitselect&spidata))
			ioctl(ppdrvdev, PPDRV_IOC_PINSET, DTX_DATA );
		else
			ioctl(ppdrvdev, PPDRV_IOC_PINCLEAR, DTX_DATA );

		for(ii=0;ii<PP_BIT_TIME;ii++);

		ioctl(ppdrvdev, PPDRV_IOC_PINSET, DTX_CLK );
		for(ii=0;ii<PP_BIT_TIME;ii++);
		ioctl(ppdrvdev, PPDRV_IOC_PINCLEAR, DTX_CLK );
		for(ii=0;ii<PP_BIT_TIME;ii++);

		bitselect=(bitselect>>1);
	}
	ioctl(ppdrvdev, PPDRV_IOC_PINCLEAR, DTX_CLK | DTX_DATA );
	ioctl(ppdrvdev, PPDRV_IOC_PINSET, DTX_ENABLE );
	for(ii=0;ii<PP_BIT_TIME;ii++);
	ioctl(ppdrvdev, PPDRV_IOC_PINCLEAR, DTX_ENABLE );
#endif
}
/*
	mutex needed
	now assumes calling thread secures permissions
	could set up a separate thread to program the radio? yuck!

*/
void	progdtx(t_pmr_chan *pChan)
{
#if(DTX_PROG == 1)	
	//static u32	progcount=0;

	u32 reffreq;
	u32 stepfreq;
	u32 rxiffreq;
	u32 synthfreq;
	u32 shiftreg;
	u32 tmp;

	TRACEC(1, "\nprogdtx() %i %i %i\n", pChan->rxfreq, pChan->txfreq, 0);

	if (ppdrvdev == 0)
    	ppdrvdev = open("/dev/ppdrv_device", 0);

    if (ppdrvdev < 0)
    {
		ast_debug(LOG_ERROR, "open /dev/ppdrv_ppdrvdev returned %i\n", ppdrvdev);
		exit(0);
	}

	if(pChan->rxfreq>200000000)
	{
		reffreq=16012500;
		stepfreq=12500;
		rxiffreq=21400000;
	}
	else
	{
		reffreq=16000000;
		stepfreq=5000;
		rxiffreq=10700000;
	}

	shiftreg=(reffreq/stepfreq)<<1;
	shiftreg=shiftreg|0x00000001;

	ppspiout(shiftreg);

	if(pChan->txPttOut)
		synthfreq=pChan->txfreq;
	else
		synthfreq=pChan->rxfreq-rxiffreq;

	shiftreg=(synthfreq/stepfreq)<<1;
	tmp=(shiftreg&0xFFFFFF80)<<1;
	shiftreg=tmp+(shiftreg&0x0000007F);

	ppspiout(shiftreg);

	ioctl(ppdrvdev, PPDRV_IOC_PINMODE_OUT, DTX_CLK | DTX_DATA | DTX_ENABLE | DTX_TXPWR | DTX_TX );
	ioctl(ppdrvdev, PPDRV_IOC_PINCLEAR, DTX_CLK | DTX_DATA | DTX_ENABLE );

	if(pChan->txPttOut)
	{
		ioctl(ppdrvdev, PPDRV_IOC_PINCLEAR, DTX_TXPWR );
		ioctl(ppdrvdev, PPDRV_IOC_PINSET, DTX_TX );
		if(pChan->txpower && 0) ioctl(ppdrvdev, PPDRV_IOC_PINSET, DTX_TXPWR );
	}
	else
	{
		ioctl(ppdrvdev, PPDRV_IOC_PINCLEAR, DTX_TX | DTX_TXPWR );
	}
#endif
}

/*	dedrift
	reconciles clock differences between the usb adapter and 
	asterisk's frame rate clock	
	take out all accumulated drift error on these events:
	before transmitter on
	when ptt release from mobile units detected
*/
void dedrift(t_pmr_chan *pChan)
{
	TRACEC(5, "dedrift()\n");

	if(pChan->dd.option==9)
	{
		TRACEF(1, "dedrift(9)\n");
		pChan->dd.framesize=DDB_FRAME_SIZE;
		pChan->dd.frames=DDB_FRAMES_IN_BUFF;
		pChan->dd.buffersize = pChan->dd.frames * pChan->dd.framesize;
		pChan->dd.buff=ast_calloc(DDB_FRAME_SIZE*DDB_FRAMES_IN_BUFF,2);
		pChan->dd.modulus=DDB_ERR_MODULUS;
		pChan->dd.inputindex=0;
		pChan->dd.outputindex=0;
		pChan->dd.skew = pChan->dd.lead=0;
		pChan->dd.z1=0;
		pChan->dd.debug=0;
		pChan->dd.debugcnt=0;
		pChan->dd.lock=pChan->dd.b.txlock=pChan->dd.b.rxlock=0;
		pChan->dd.initcnt=2;
		pChan->dd.timer=10000/20;
		pChan->dd.drift=0;
		pChan->dd.factor=pChan->dd.x1 = pChan->dd.x0 = pChan->dd.y1 = pChan->dd.y0 = 0;
		pChan->dd.txframecnt=pChan->dd.rxframecnt=0;
		// clear the buffer too!
		return;
	}
	else if(pChan->dd.option==8)
	{
		ast_free(pChan->dd.buff);
		pChan->dd.lock=0;
		pChan->dd.b.txlock=pChan->dd.b.rxlock=0;
		return;
	}
	else if(pChan->dd.initcnt==0)
	{
		void *vptr;
		i16 inputindex;
		i16 indextweak;
	    i32 accum;

		// WinFilter, IIR Fs=50, Fc=0.1
		const i32 a0 =  26231;
		const i32 a1 =  26231;
		const i32 b0 =  32768;
		const i32 b1 = -32358;
		const i32 dg =	128;

		inputindex = pChan->dd.inputindex;
		pChan->dd.skew = pChan->dd.txframecnt-pChan->dd.rxframecnt;
		pChan->dd.rxframecnt++;

		// pull data from buffer
		if( (pChan->dd.outputindex + pChan->dd.framesize) > pChan->dd.buffersize )
		{
			i16 dofirst,donext;

			dofirst = pChan->dd.buffersize - pChan->dd.outputindex;
		    donext = pChan->dd.framesize - dofirst;
			vptr = (void*)(pChan->dd.ptr);
			memcpy(vptr,(void*)(pChan->dd.buff + pChan->dd.outputindex),dofirst*2);
			vptr=(void*)(pChan->dd.ptr + dofirst);
			memcpy(vptr,(void*)(pChan->dd.buff),donext*2);
		}
		else
		{
			memcpy(pChan->dd.ptr,(void*)(pChan->dd.buff + pChan->dd.outputindex),pChan->dd.framesize*2);
		}
		
		// compute clock error and correction factor
		if(pChan->dd.outputindex > inputindex)
		{
			pChan->dd.lead = (inputindex + pChan->dd.buffersize) - pChan->dd.outputindex;
		}
	    else
		{
			pChan->dd.lead = inputindex - pChan->dd.outputindex;
		}
		pChan->dd.err = pChan->dd.lead - (pChan->dd.buffersize/2);

		pChan->dd.x1 = pChan->dd.x0;
	    pChan->dd.y1 = pChan->dd.y0;
		pChan->dd.x0 = pChan->dd.err;
	    pChan->dd.y0 = a0 * pChan->dd.x0;
	    pChan->dd.y0 += (a1 * pChan->dd.x1 - (b1 * pChan->dd.y1));
	    pChan->dd.y0 /= b0;
		accum = pChan->dd.y0/dg;

		pChan->dd.factor=accum;
		indextweak=0;

		#if 1
		// event sync'd correction
		if(pChan->dd.b.doitnow)
		{
		    pChan->dd.b.doitnow=0;	
			indextweak=pChan->dd.factor;
			pChan->dd.factor = pChan->dd.x1 = pChan->dd.x0 = pChan->dd.y1 = pChan->dd.y0 = 0;
			pChan->dd.timer=20000/MS_PER_FRAME;
		}
		// coarse lead adjustment if really far out of range
		else if( pChan->dd.lead >= pChan->dd.framesize*(DDB_FRAMES_IN_BUFF-2) )
		{
			pChan->dd.factor = pChan->dd.x1 = pChan->dd.x0 = pChan->dd.y1 = pChan->dd.y0 = 0;
			indextweak += (pChan->dd.framesize*5/4);
		}
		else if(pChan->dd.lead <= pChan->dd.framesize*2 )
		{
			pChan->dd.factor = pChan->dd.x1 = pChan->dd.x0 = pChan->dd.y1 = pChan->dd.y0 = 0;
			indextweak -= (pChan->dd.framesize*5/4);
		}
	    #endif

		#if 1
		if(pChan->dd.timer>0)pChan->dd.timer--;
		if(pChan->dd.timer==0 && abs(pChan->dd.factor)>=16)
		{
			indextweak=pChan->dd.factor;
			pChan->dd.factor = pChan->dd.x1 = pChan->dd.x0 = pChan->dd.y1 = pChan->dd.y0 = 0;
			pChan->dd.timer=20000/MS_PER_FRAME;
		}
		#endif

		#if XPMR_DEBUG0 == 1
		if (indextweak != 0)
			TRACEF(4,
				"%08i indextweak  %+4i  %+4i  %+5i  %5i  %5i  %5i  %+4i\n",
				pChan->dd.rxframecnt,
				indextweak,
				pChan->dd.err,
				accum,
				inputindex,
				pChan->dd.outputindex,
				pChan->dd.lead,
				pChan->dd.skew);
#endif

		// set the output index based on lead and clock offset
		pChan->dd.outputindex = (pChan->dd.outputindex + pChan->dd.framesize + indextweak)%pChan->dd.buffersize;
	}
}
/*
*/
void dedrift_write(t_pmr_chan *pChan, i16 *src )
{
	void *vptr;

	TRACEF(5, "dedrift_write()\n");
	vptr = pChan->dd.buff + pChan->dd.inputindex;
	memcpy(vptr, src, pChan->dd.framesize*2);
	pChan->dd.inputindex = (pChan->dd.inputindex + pChan->dd.framesize) % pChan->dd.buffersize;
	pChan->dd.txframecnt++;
	if(pChan->dd.initcnt!=0)pChan->dd.initcnt--;
	pChan->dd.accum+=pChan->dd.framesize;
}

#if GCC_VERSION > 40600
#pragma GCC diagnostic pop
#endif

/* end of file */
