/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Copyright (C) 2008, Jim Dixon
 * Jim Dixon <jim@lambdatel.com>
 *
 * USRP interface Copyright (C) 2010, KA1RBI
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
 * Changes:
 * --------
 * 03/16/21 - Danny Lloyd, KB4MDD <kb4mdd@arrl.net>
 * added dtmf decode
 */

/*! \file
 *
 * \brief GNU Radio interface
 * 
 * \author Jim Dixon <jim@lambdatel.com>, KA1RBI <ikj1234i at yahoo dot-com>
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

/* Version 0.1.2, 11/22/2023
 *
 * Channel connection for Asterisk to GNU Radio/USRP
 *
 * It is invoked as usrp/HISIP:HISPORT[:MYPORT] 	 
 *	  	 
 * HISIP is the IP address (or FQDN) of the GR app
 * HISPORT is the UDP socket of the GR app
 * MYPORT (optional) is the UDP socket that Asterisk listens on for this channel 	 
*/

#include "asterisk.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <search.h>
#include <sys/ioctl.h>
#include <ctype.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/format_cache.h"

#include "chan_usrp.h"

#define	MAX_RXKEY_TIME 4
#define	KEEPALIVE_TIME 50 * 7
#define	BLOCKING_FACTOR 4
#define	SSO sizeof(unsigned long)
#define QUEUE_OVERLOAD_THRESHOLD 25

static const char tdesc[] = "USRP Driver";

static char context[AST_MAX_EXTENSION] = "default";
static char type[] = "usrp";

/* USRP creates private structures on demand */

struct usrp_rxq {
	struct usrp_rxq *qe_forw;
	struct usrp_rxq *qe_back;
	char buf[USRP_VOICE_FRAME_SIZE];
};

/*!
 * \brief Descriptor for one of our channels.
 */
 struct usrp_pvt {
	int usrp;					/* Open UDP socket */
	struct ast_channel *owner;	/* Channel we belong to, possibly NULL */
	char stream[80];			/* Our stream */
	struct sockaddr_in si_other;	/* for UDP sending */
	char txkey;					/* Indicates tx key */
	int rxkey;					/* Indicates rx key - implemented as a count down */
	struct ast_frame fr;		/* "null" frame */
	struct usrp_rxq rxq;		/* Received data queue */
	unsigned long rxseq;		/* Received packet sequence number */
	unsigned long txseq;		/* Transmit packet sequence number */
	struct ast_module_user *u;	/* Hold a reference to this module */
	unsigned long writect;		/* Number of packets written */
	unsigned long readct;		/* Number of packets read */
	struct ast_dsp *dsp;		/* Reference to dsp processor */
	/* bit fields */
	unsigned int unkey_owed:1;	/* Indicator if we sent a key up packet */
	unsigned int warned:1;		/* Indicator for warning issued on writes */
	unsigned int usedtmf:1;		/* Indicator if we decode DTMF */
};

static struct ast_channel *usrp_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids,
	const struct ast_channel *requestor, const char *data, int *cause);
static int usrp_call(struct ast_channel *ast, const char *dest, int timeout);
static int usrp_hangup(struct ast_channel *ast);
static struct ast_frame *usrp_xread(struct ast_channel *ast);
static int usrp_xwrite(struct ast_channel *ast, struct ast_frame *frame);
static int usrp_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen);
static int usrp_digit_begin(struct ast_channel *c, char digit);
static int usrp_digit_end(struct ast_channel *c, char digit, unsigned int duratiion);
static int usrp_text(struct ast_channel *c, const char *text);
static int usrp_setoption(struct ast_channel *chan, int option, void *data, int datalen);

/*!
 * \brief Asterisk channel technology struct.
 * This tells Asterisk the functions to call when
 * it needs to interact with our module.
 */
static struct ast_channel_tech usrp_tech = {
	.type = type,
	.description = tdesc,
	.requester = usrp_request,
	.call = usrp_call,
	.hangup = usrp_hangup,
	.read = usrp_xread,
	.write = usrp_xwrite,
	.indicate = usrp_indicate,
	.send_text = usrp_text,
	.send_digit_begin = usrp_digit_begin,
	.send_digit_end = usrp_digit_end,
	.setoption = usrp_setoption,
};
/*!
 * \brief Maximum number of channels supported by this module. 
*/
#define MAX_CHANS 16
static struct usrp_pvt *usrp_channels[MAX_CHANS] = {NULL};

/*!
 * \brief Handle Asterisk CLI request for show.
 * \param e				Asterisk CLI entry.
 * \param cmd			CLI command type.
 * \param a				Asterisk CLI arguments.
 * \return	CLI success or failure.
 */
static char *handle_usrp_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char s[256];
	struct usrp_pvt *pvt;
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "usrp show";
		e->usage = "usrp show";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	for (i = 0; i < MAX_CHANS; i++) {
		pvt = usrp_channels[i];
		if (pvt) {
			sprintf(s, "Channel %s: Tx keyed %-3s, Rx keyed %d, Read %lu, Write %lu", pvt->stream, (pvt->txkey) ? "yes" : "no", pvt->rxkey,
				pvt->readct, pvt->writect);
			ast_cli(a->fd, "%s\n", s);
		}
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_usrp[] = {
	AST_CLI_DEFINE(handle_usrp_show, "Show USRP statistics"),
};

/*!
 * \brief USRP call.
 * \param ast			Asterisk channel.
 * \param dest			Destination.
 * \param timeout		Timeout.
 * \retval -1 			if not successful.
 * \retval 0 			if successful.
 */
static int usrp_call(struct ast_channel *ast, const char *dest, int timeout)
{
	if ((ast_channel_state(ast) != AST_STATE_DOWN) && (ast_channel_state(ast) != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "Called on %s, neither down nor reserved\n", ast_channel_name(ast));
		return -1;
	}
	/* When we call, it just works, really, there's no destination...  Just
	   ring the phone and wait for someone to answer */
	ast_debug(1, "Calling %s on %s\n", dest, ast_channel_name(ast));

	ast_setstate(ast, AST_STATE_UP);
	return 0;
}

/*!
 * \brief Destroy this USRP connection.
 * \param pvt		Private structure.
 */
static void usrp_destroy(struct usrp_pvt *pvt)
{
	if (pvt->usrp) {
		close(pvt->usrp);
	}
	ast_module_user_remove(pvt->u);
	ast_free(pvt);
}

/*!
 * \brief Allocate a USRP private structure.
 * \param data		Arguments for creating the stream.
 * \return			Private structure.
 */
static struct usrp_pvt *usrp_alloc(void *data)
{
	struct usrp_pvt *pvt;
	char stream[256];
	struct sockaddr_in si_me;
	struct hostent *host;
	struct ast_hostent ah;
	int o_slot;

	AST_DECLARE_APP_ARGS(args, AST_APP_ARG(hisip); AST_APP_ARG(hisport); AST_APP_ARG(myport););

	if (ast_strlen_zero(data)) {
		return NULL;
	}

	AST_NONSTANDARD_APP_ARGS(args, data, ':');

	if (ast_strlen_zero(args.hisip)) {
		args.hisip = "127.0.0.1";
	}
	if (ast_strlen_zero(args.hisport)) {
		args.hisport = "1234";
	}
	if (ast_strlen_zero(args.myport)) {
		args.myport = args.hisport;
	}

	pvt = ast_calloc(1, sizeof(struct usrp_pvt));
	if (!pvt) {
		return NULL;
	}

	snprintf(pvt->stream, sizeof(pvt->stream), "%s:%d:%d", args.hisip, atoi(args.hisport), atoi(args.myport));
	pvt->rxq.qe_forw = &pvt->rxq;
	pvt->rxq.qe_back = &pvt->rxq;

	memset(&ah, 0, sizeof(ah));
	host = ast_gethostbyname(args.hisip, &ah);
	if (!host) {
		ast_log(LOG_WARNING, "Unable to find host %s\n", args.hisip);
		ast_free(pvt);
		return NULL;
	}
	memset(&pvt->si_other, 0, sizeof(pvt->si_other));
	pvt->si_other.sin_addr = *(struct in_addr *) host->h_addr;
	pvt->si_other.sin_family = AF_INET;
	pvt->si_other.sin_port = htons(atoi(args.hisport));

	if ((pvt->usrp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		ast_log(LOG_WARNING, "Unable to create new socket for USRP connection %s\n", stream);
		ast_free(pvt);
		return NULL;
	}

	memset((char *) &si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(atoi(args.myport));
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);
	if (!strncmp(ast_inet_ntoa(pvt->si_other.sin_addr), "127.", 4))
		si_me.sin_addr.s_addr = inet_addr("127.0.0.1");
	if (bind(pvt->usrp, &si_me, sizeof(si_me)) == -1) {
		ast_log(LOG_WARNING, "Unable to bind port for USRP connection %s\n", stream);
		ast_free(pvt);
		return NULL;
	}
	// TODO: do we need locking for this?
	for (o_slot = 0; o_slot < MAX_CHANS; o_slot++) {
		if (!usrp_channels[o_slot])
			break;
	}
	if (o_slot >= MAX_CHANS) {
		ast_log(LOG_WARNING, "Unable to find empty usrp_channels[] entry\n");
		return NULL;
	}
	usrp_channels[o_slot] = pvt;
	return pvt;
}

/*!
 * \brief Asterisk hangup function.
 * \param ast		Asterisk channel.
 * \retval 0		Always returns 0.			
 */
static int usrp_hangup(struct ast_channel *ast)
{
	struct usrp_pvt *p;
	int i;

	p = ast_channel_tech_pvt(ast);
	ast_debug(1, "usrp hangup(%s)\n", ast_channel_name(ast));
	
	if (!p) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	if (p->dsp) {
		ast_dsp_free(p->dsp);
	}
	// TODO: do we need locking for this?
	for (i = 0; i < MAX_CHANS; i++) {
		if (usrp_channels[i] == p) {
			usrp_channels[i] = NULL;
			break;
		}
	}
	if (i >= MAX_CHANS) {
		ast_log(LOG_WARNING, "Unable to delete usrp_channels[] entry %s\n", ast_channel_name(ast));
	}
	usrp_destroy(p);
	ast_channel_tech_pvt_set(ast, NULL);
	ast_setstate(ast, AST_STATE_DOWN);
	return 0;
}

/*!
 * \brief Asterisk indicate function.
 * This is used to indicate tx key / unkey.
 * \param ast			Asterisk channel.
 * \param cond			Condition.
 * \param data			Data.
 * \param datalen		Data length.
 * \retval 0			If successful.
 * \retval -1			For hangup.
 */
static int usrp_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen)
{
	struct usrp_pvt *p = ast_channel_tech_pvt(ast);
	struct _chan_usrp_bufhdr bufhdr;

	switch (cond) {
	case AST_CONTROL_RADIO_KEY:
		p->txkey = 1;
		ast_debug(1, "Channel %s: ACRK TX ON.\n", ast_channel_name(ast));
		break;
	case AST_CONTROL_RADIO_UNKEY:
		p->txkey = 0;
		ast_debug(1, "Channel %s: ACRUK TX OFF.\n", ast_channel_name(ast));
		break;
	case AST_CONTROL_HANGUP:
		return -1;
	default:
		return 0;
	}
	if (p->unkey_owed) {
		p->unkey_owed = 0;
		/* tx was unkeyed - notify remote end */
		memset(&bufhdr, 0, sizeof(struct _chan_usrp_bufhdr));
		memcpy(bufhdr.eye, "USRP", 4);
		bufhdr.seq = htonl(p->txseq++);
		if (sendto(p->usrp, &bufhdr, sizeof(bufhdr), 0, &p->si_other, sizeof(p->si_other)) == -1) {
			if (!p->warned) {
				ast_log(LOG_WARNING, "Channel %s: sendto: %d\n", ast_channel_name(ast), errno);
				p->warned = 1;
			}
		}
	}

	return 0;
}

/*!
 * \brief Asterisk text function.
 * \param ast			Asterisk channel.
 * \param text			Text message to process.
 * \retval 0			If successful.
 * \retval -1			If unsuccessful.
 */
static int usrp_text(struct ast_channel *ast, const char *text)
{
	ast_debug(1, "Channel %s: Text received: %s\n", ast_channel_name(ast), text);
	return 0;
}

/*!
 * \brief Asterisk digit begin function.
 * \param ast			Asterisk channel.
 * \param digit			Digit processed.
 * \retval 0			
 */
static int usrp_digit_begin(struct ast_channel *ast, char digit)
{
	return 0;
}

/*!
 * \brief Asterisk digit end function.
 * \param ast			Asterisk channel.
 * \param digit			Digit processed.
 * \param duration		Duration of the digit.
 * \retval -1			
 */
static int usrp_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	/* no better use for received digits than print them */
	ast_verbose(" << Console Received digit %c of duration %u ms >> \n", digit, duration);
	return 0;
}

/*!
 * \brief Asterisk read function.
 * \param ast		Asterisk channel.
 * \retval 			Asterisk frame.
 */
static struct ast_frame *usrp_xread(struct ast_channel *ast)
{
	struct usrp_pvt *pvt = ast_channel_tech_pvt(ast);
	char buf[512];
	struct sockaddr_in si_them;
	unsigned int themlen;
	unsigned long seq;
	int n;
	int datalen;
	struct ast_frame fr;
	struct usrp_rxq *qp;
	struct _chan_usrp_bufhdr *bufhdrp = (struct _chan_usrp_bufhdr *) buf;
	char *bufdata = &buf[sizeof(struct _chan_usrp_bufhdr)];

	pvt->readct++;

	themlen = sizeof(struct sockaddr_in);
	/* Attempt to read a packet from the remote app */
	if ((n = recvfrom(pvt->usrp, buf, sizeof(buf), 0, &si_them, &themlen)) == -1) {
		ast_log(LOG_WARNING, "Channel %s: Cannot recvfrom()", ast_channel_name(ast));
		return NULL;
	}
#if 0
	if (memcmp(&si_them.sin_addr, &pvt->si_other.sin_addr, sizeof(si_them.sin_addr))) {
		ast_log(LOG_NOTICE, "Received packet from %s, expecting it from %s\n",
				ast_inet_ntoa(si_them.sin_addr), ast_inet_ntoa(pvt->si_other.sin_addr));
		pvt->fr.frametype = 0;
		pvt->fr.subclass.integer = 0;
		pvt->fr.datalen = 0;
		pvt->fr.samples = 0;
		pvt->fr.data.ptr = NULL;
		pvt->fr.src = type;
		pvt->fr.offset = 0;
		pvt->fr.mallocd = 0;
		pvt->fr.delivery.tv_sec = 0;
		pvt->fr.delivery.tv_usec = 0;
		return &pvt->fr;
	}
#endif
	if (n < sizeof(struct _chan_usrp_bufhdr)) {
		ast_log(LOG_NOTICE, "Channel %s: Received packet length %d too short\n", ast_channel_name(ast), n);
	} else {
		datalen = n - sizeof(struct _chan_usrp_bufhdr);
		if (memcmp(bufhdrp->eye, "USRP", 4)) {
			ast_log(LOG_NOTICE, "Channel %s: Received packet from %s with invalid data\n", ast_channel_name(ast), ast_inet_ntoa(si_them.sin_addr));
		} else {
			seq = ntohl(bufhdrp->seq);
			if (seq != pvt->rxseq && seq != 0 && pvt->rxseq != 0) {
				ast_log(LOG_NOTICE, "Channel %s: Possible data loss, expected seq %lu received %lu\n", 
					ast_channel_name(ast), pvt->rxseq, seq);
			}
			pvt->rxseq = seq + 1;
			// TODO: TEXT processing added N4IRR
			if (datalen == USRP_VOICE_FRAME_SIZE) {
				qp = ast_malloc(sizeof(struct usrp_rxq));
				if (qp) {
					/* Pass received text messages to Asterisk */
					if (bufhdrp->type == USRP_TYPE_TEXT) {
						char buf1[320];

						insque((struct qelem *) qp, (struct qelem *) pvt->rxq.qe_back);
						strcpy(buf1, bufdata);
						memset(&fr, 0, sizeof(fr));
						fr.data.ptr = buf1;
						fr.datalen = strlen(buf1) + 1;
						fr.samples = 0;
						fr.frametype = AST_FRAME_TEXT;
						fr.subclass.integer = 0;
						fr.src = "chan_usrp";
						fr.offset = 0;
						fr.mallocd = 0;
						fr.delivery.tv_sec = 0;
						fr.delivery.tv_usec = 0;
						ast_queue_frame(ast, &fr);
					} else {
						/* Queue the received voice frame for processing */
						memcpy(qp->buf, bufdata, USRP_VOICE_FRAME_SIZE);
						insque((struct qelem *) qp, (struct qelem *) pvt->rxq.qe_back);
					}
				}
			}
		}
	}
	fr.datalen = 0;
	fr.samples = 0;
	fr.frametype = 0;
	fr.subclass.integer = 0;
	fr.data.ptr = 0;
	fr.src = type;
	fr.offset = 0;
	fr.mallocd = 0;
	fr.delivery.tv_sec = 0;
	fr.delivery.tv_usec = 0;

	return &pvt->fr;
}

/*!
 * \brief Asterisk write function.
 * This routine handles asterisk to radio frames.
 * \param ast			Asterisk channel.
 * \param frame			Asterisk frame to process.
 * \retval 0			Successful.
 */
static int usrp_xwrite(struct ast_channel *ast, struct ast_frame *frame)
{
	struct usrp_pvt *pvt = ast_channel_tech_pvt(ast);
	struct ast_frame fr, *f;
	struct usrp_rxq *qp;
	int n;
	char buf[USRP_VOICE_FRAME_SIZE + AST_FRIENDLY_OFFSET + SSO];

	// buffer for constructing frame, plus two ptrs: hdr and data
	char sendbuf[sizeof(struct _chan_usrp_bufhdr) + USRP_VOICE_FRAME_SIZE];
	struct _chan_usrp_bufhdr *bufhdrp = (struct _chan_usrp_bufhdr *) sendbuf;
	char *bufdata = &sendbuf[sizeof(struct _chan_usrp_bufhdr)];

	if (ast_channel_state(ast) != AST_STATE_UP) {
		/* Don't try to end audio on-hook */
		return 0;
	}
	/* Only process voice frames */
	if (frame->frametype != AST_FRAME_VOICE) {
		return 0;
	}

	if (ast_format_cap_iscompatible_format(ast_channel_nativeformats(ast), frame->subclass.format) == AST_FORMAT_CMP_NOT_EQUAL) {
		struct ast_str *cap_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		ast_log(LOG_WARNING, "Channel %s: Asked to transmit frame type %s, while native formats is %s (read/write = (%s/%s))\n",
				ast_channel_name(ast), 
				ast_format_get_name(frame->subclass.format),
				ast_format_cap_get_names(ast_channel_nativeformats(ast), &cap_buf),
				ast_format_get_name(ast_channel_readformat(ast)),
				ast_format_get_name(ast_channel_writeformat(ast)));
		return 0;
	}

	if (frame->datalen > USRP_VOICE_FRAME_SIZE) {
		ast_log(LOG_WARNING, "Channel %s: Frame datalen %d exceeds limit\n", ast_channel_name(ast), frame->datalen);
		return 0;
	}

	/* See if we have something in the rx queue to process */
	if (pvt->rxq.qe_forw != &pvt->rxq) {
		for (n = 0, qp = pvt->rxq.qe_forw; qp != &pvt->rxq; qp = qp->qe_forw) {
			n++;
		}
		if (n > QUEUE_OVERLOAD_THRESHOLD) {
			while (pvt->rxq.qe_forw != &pvt->rxq) {
				qp = pvt->rxq.qe_forw;
				remque((struct qelem *) qp);
				ast_free(qp);
			}
			ast_debug(1, "Channel %s: Receive queue exceeds the threshold of %d\n", ast_channel_name(ast), QUEUE_OVERLOAD_THRESHOLD);
			if (pvt->rxkey) {
				pvt->rxkey = 1;
			}
		} else {
			if (!pvt->rxkey) {
				memset(&fr, 0, sizeof(fr));
				fr.datalen = 0;
				fr.samples = 0;
				fr.frametype = AST_FRAME_CONTROL;
				fr.subclass.integer = AST_CONTROL_RADIO_KEY;
				fr.data.ptr = 0;
				fr.src = type;
				fr.offset = 0;
				fr.mallocd = 0;
				fr.delivery.tv_sec = 0;
				fr.delivery.tv_usec = 0;
				ast_queue_frame(ast, &fr);
				ast_debug(1, "Channel %s: RX ON\n", ast_channel_name(ast));
			}
			pvt->rxkey = MAX_RXKEY_TIME;
			qp = pvt->rxq.qe_forw;
			remque((struct qelem *) qp);
			memcpy(buf + AST_FRIENDLY_OFFSET, qp->buf, USRP_VOICE_FRAME_SIZE);
			ast_free(qp);
			
			/* Send the voice data to Asterisk */
			memset(&fr, 0, sizeof(fr));
			fr.datalen = USRP_VOICE_FRAME_SIZE;
			fr.samples = 160;
			fr.frametype = AST_FRAME_VOICE;
			fr.subclass.format = ast_format_slin;
			fr.data.ptr = buf + AST_FRIENDLY_OFFSET;
			fr.src = type;
			fr.offset = AST_FRIENDLY_OFFSET;
			fr.mallocd = 0;
			fr.delivery.tv_sec = 0;
			fr.delivery.tv_usec = 0;
			ast_queue_frame(ast, &fr);

			/* See if we need to check for DTMF */
			if (pvt->usedtmf && pvt->dsp) {
				f = ast_dsp_process(ast, pvt->dsp, &fr);
				if ((f->frametype == AST_FRAME_DTMF_END) || (f->frametype == AST_FRAME_DTMF_BEGIN)) {
					if ((f->subclass.integer == 'm') || (f->subclass.integer == 'u')) {
						f->frametype = AST_FRAME_NULL;
						f->subclass.integer = 0;
						ast_queue_frame(ast, f);
					}
					if (f->frametype == AST_FRAME_DTMF_END) {
						ast_log(LOG_NOTICE, "Channel %s: Got DTMF char %c\n", ast_channel_name(ast), f->subclass.integer);
					}
					ast_queue_frame(ast, f);
				}
				ast_frfree(f);
			}
		}
	}
	if (pvt->rxkey == 1) {
		memset(&fr, 0, sizeof(fr));
		fr.datalen = 0;
		fr.samples = 0;
		fr.frametype = AST_FRAME_CONTROL;
		fr.subclass.integer = AST_CONTROL_RADIO_UNKEY;
		fr.data.ptr = 0;
		fr.src = type;
		fr.offset = 0;
		fr.mallocd = 0;
		fr.delivery.tv_sec = 0;
		fr.delivery.tv_usec = 0;
		ast_queue_frame(ast, &fr);
		ast_debug(1, "Channel %s: RX OFF\n", ast_channel_name(ast));
	}
	/* Decrement the receive key counter.
	 * This ensures that we do not get stuck in receive mode.
	 * The maximum is set in MAX_RXKEY_TIME.
	 */
	if (pvt->rxkey) {
		pvt->rxkey--;
	}

	if (!pvt->txkey) {
		return 0;
	}
	
	/* Send a USRP voice packet to the remote app */
	pvt->writect++;
	pvt->unkey_owed = 1;
	memcpy(bufdata, frame->data.ptr, frame->datalen);
	memset(bufhdrp, 0, sizeof(struct _chan_usrp_bufhdr));
	memcpy(bufhdrp->eye, "USRP", 4);
	bufhdrp->seq = htonl(pvt->txseq++);
	bufhdrp->keyup = htonl(1);	/* indicates key up */
	if (sendto(pvt->usrp, &sendbuf, frame->datalen + sizeof(struct _chan_usrp_bufhdr),
			   0, &pvt->si_other, sizeof(pvt->si_other)) == -1) {
		if (!pvt->warned) {
			ast_log(LOG_WARNING, "sendto: %d\n", errno);
			pvt->warned = 1;
		}
		return -1;
	}

	return 0;
}

/*!
 * \brief Asterisk setoption function.
 * \param chan			Asterisk channel.
 * \param option		Option.
 * \param data			Data.
 * \param datalen		Data length.
 * \retval 0			If successful.
 * \retval -1			If failed.
 */
static int usrp_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
	char *cp;
	struct usrp_pvt *pvt = ast_channel_tech_pvt(chan);

	/* all supported options require data */
	if (!data || (datalen < 1)) {
		errno = EINVAL;
		return -1;
	}

	switch (option) {
	case AST_OPTION_TONE_VERIFY:
		cp = (char *) data;
		switch (*cp) {
		case 1:
			ast_debug(1, "Channel %s: Set option TONE VERIFY, mode: OFF(0)\n", ast_channel_name(chan));
			pvt->usedtmf = 1;
			break;
		case 2:
			ast_debug(1, "Channel %s: Set option TONE VERIFY, mode: MUTECONF/MAX(2)\n", ast_channel_name(chan));
			pvt->usedtmf = 1;
			break;
		case 3:
			ast_debug(1, "Channel %s: Set option TONE VERIFY, mode: DISABLE DETECT(3)\n", ast_channel_name(chan));
			pvt->usedtmf = 0;
			break;
		default:
			ast_debug(1, "Channel %s: Set option TONE VERIFY, mode: OFF(0)\n", ast_channel_name(chan));
			pvt->usedtmf = 1;
			break;
		}
		break;
	}
	errno = 0;
	return 0;
}

/*!
 * \brief Start a new USRP call.
 * \param i				Private structure.
 * \param state			State.
 * \param assignedids	Unique ID string assigned to the channel.
 * \param requestor		Asterisk channel.
 * \return 				Asterisk channel.
 */
static struct ast_channel *usrp_new(struct usrp_pvt *i, int state, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor)
{
	struct ast_channel *tmp;
	
	tmp = ast_channel_alloc(1, state, 0, 0, "", "s", context, assignedids, requestor, 0, "usrp/%s", i->stream);
	if (tmp) {
		ast_channel_tech_set(tmp, &usrp_tech);
		ast_channel_internal_fd_set(tmp, 0, i->usrp);
		ast_channel_nativeformats_set(tmp, usrp_tech.capabilities);
		ast_channel_set_rawreadformat(tmp, ast_format_slin);
		ast_channel_set_rawwriteformat(tmp, ast_format_slin);
		ast_channel_set_readformat(tmp, ast_format_slin);
		ast_channel_set_writeformat(tmp, ast_format_slin);
		if (state == AST_STATE_RING) {
			ast_channel_rings_set(tmp, 1);
		}
		ast_channel_tech_pvt_set(tmp, i);
		ast_channel_context_set(tmp, context);
		ast_channel_exten_set(tmp, "s");
		ast_channel_language_set(tmp, "");
		ast_channel_unlock(tmp);
		i->owner = tmp;
		i->u = ast_module_user_add(tmp);
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ast_channel_name(tmp));
				ast_hangup(tmp);
			}
		}
		i->dsp = ast_dsp_new();
		if (i->dsp) {
			ast_dsp_set_features(i->dsp, DSP_FEATURE_DIGIT_DETECT);
			ast_dsp_set_digitmode(i->dsp, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
		}
	} else {
		ast_log(LOG_ERROR, "Unable to allocate channel structure\n");
	}
	return tmp;
}

/*!
 * \brief USRP request from Asterisk.
 * This is a standard Asterisk function - requester.
 * Asterisk calls this function to to setup private data structures.
 * \param type			Type of channel to request.
 * \param cap			Format capabilities for the channel.
 * \param assignedids	Unique ID string to assign to the channel.
 * \param requestor		Channel asking for data. 
 * \param data			Destination of the call.
 * \param cause			Cause of failure.
 * \retval NULL			Failure
 * \return				ast_channel if successful
 */
static struct ast_channel *usrp_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids,
	const struct ast_channel *requestor, const char *data, int *cause)
{
	struct usrp_pvt *pvt;
	struct ast_channel *tmp = NULL;

	if (!(ast_format_cap_iscompatible(cap, usrp_tech.capabilities))) {
		struct ast_str *cap_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		ast_log(LOG_NOTICE, "Channel requested with unsupported format(s): '%s'\n", ast_format_cap_get_names(cap, &cap_buf));
		return NULL;
	}

	pvt = usrp_alloc(ast_strdupa(data));
	if (pvt) {
		tmp = usrp_new(pvt, AST_STATE_DOWN, assignedids, requestor);
		if (!tmp) {
			usrp_destroy(pvt);
		}
	}
	return tmp;
}

static int unload_module(void)
{
	/* First, take us out of the channel loop */
	ast_channel_unregister(&usrp_tech);
	ast_cli_unregister_multiple(cli_usrp, ARRAY_LEN(cli_usrp));
	ao2_cleanup(usrp_tech.capabilities);
	usrp_tech.capabilities = NULL;
	
	return 0;
}

static int load_module(void)
{
	if (!(usrp_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append(usrp_tech.capabilities, ast_format_slin, 0);
	
	/* Make sure we can register our channel type */
	if (ast_channel_register(&usrp_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		return -1;
	}
	
	ast_cli_register_multiple(cli_usrp, ARRAY_LEN(cli_usrp));
	
	return 0;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "USRP Channel Module");
