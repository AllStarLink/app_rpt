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
 ***/

/* Version 0.1.1, 11/15/2019

Channel connection for Asterisk to GNU Radio/USRP

Its invoked as usrp/HISIP:HISPORT[:MYPORT] 	 
	  	 
HISIP is the IP address (or FQDN) of the GR app
HISPORT is the UDP socket of the GR app
MYPORT (optional) is the UDP socket that Asterisk listens on for this channel 	 
*/

#include "asterisk.h"

/*
 * Please change this revision number when you make a edit
 * use the simple format YYMMDD
*/

ASTERISK_FILE_VERSION(__FILE__,"$Revision$")
// ASTERISK_FILE_VERSION(__FILE__,"$Revision$")

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
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"

#include "chan_usrp.h"

#define	MAX_RXKEY_TIME 4
#define	KEEPALIVE_TIME 50 * 7

#define	BLOCKING_FACTOR 4
#define	SSO sizeof(unsigned long)

#define QUEUE_OVERLOAD_THRESHOLD 25

static const char tdesc[] = "USRP Driver";

/* Only linear is allowed */
static int prefformat = AST_FORMAT_SLINEAR;

static char context[AST_MAX_EXTENSION] = "default";
static char type[] = "usrp";

/* usrp creates private structures on demand */
   
struct usrp_rxq {
	struct usrp_rxq *qe_forw;
	struct usrp_rxq *qe_back;
	char buf[USRP_VOICE_FRAME_SIZE];
} ;

struct usrp_pvt {
 	int usrp;				/* open UDP socket */
	struct ast_channel *owner;		/* Channel we belong to, possibly NULL */
	char app[16];					/* Our app */
	char stream[80];				/* Our stream */
	struct sockaddr_in si_other;		/* for UDP sending */
	char txkey;
	int rxkey;
	int keepalive;
	struct ast_frame fr;			/* "null" frame */
	char txbuf[(USRP_VOICE_FRAME_SIZE * BLOCKING_FACTOR) + SSO];
	int txindex;
	struct usrp_rxq rxq;
	unsigned long rxseq;
	unsigned long txseq;
	struct ast_module_user *u;		/*! for holding a reference to this module */
	unsigned long writect;
	unsigned long readct;
	unsigned long send_seqno;
	int warned;
	int unkey_owed;
	struct ast_dsp *dsp;
	int usedtmf;

};

static struct ast_channel *usrp_request(const char *type, int format, void *data, int *cause);
static int usrp_call(struct ast_channel *ast, char *dest, int timeout);
static int usrp_hangup(struct ast_channel *ast);
static struct ast_frame *usrp_xread(struct ast_channel *ast);
static int usrp_xwrite(struct ast_channel *ast, struct ast_frame *frame);
static int usrp_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen);
static int usrp_digit_begin(struct ast_channel *c, char digit);
static int usrp_digit_end(struct ast_channel *c, char digit, unsigned int duratiion);
static int usrp_text(struct ast_channel *c, const char *text);
static int usrp_setoption(struct ast_channel *chan, int option, void *data, int datalen);


static const struct ast_channel_tech usrp_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = AST_FORMAT_SLINEAR,
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

#define MAX_CHANS 16
static struct usrp_pvt *usrp_channels[MAX_CHANS];

static int handle_usrp_show(int fd, int argc, char *argv[])
{
	char s[256];
	struct usrp_pvt *p;
	int i;
	// ast_cli(fd, "handle_usrp_show\n");
	for (i=0; i<MAX_CHANS; i++) {
		p = usrp_channels[i];
		if (p) {
			sprintf(s, "%s txkey %-3s rxkey %d read %lu write %lu", p->stream, (p->txkey) ? "yes" : "no", p->rxkey, p->readct, p->writect);
			ast_cli(fd, "%s\n", s);
		}
	}
	return 0;
}

static struct ast_cli_entry cli_usrp_show = {
	{ "usrp", "show", NULL },
	handle_usrp_show, NULL,
	NULL };

static int usrp_call(struct ast_channel *ast, char *dest, int timeout)
{
	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "usrp_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	/* When we call, it just works, really, there's no destination...  Just
	   ring the phone and wait for someone to answer */
	if (option_debug)
		ast_log(LOG_DEBUG, "Calling %s on %s\n", dest, ast->name);

	ast_setstate(ast,AST_STATE_UP);
	return 0;
}

static void usrp_destroy(struct usrp_pvt *p)
{
	if (p->usrp)
		close(p->usrp);
	ast_module_user_remove(p->u);
	ast_free(p);
}

static struct usrp_pvt *usrp_alloc(void *data)
{
	struct usrp_pvt *p;
	int flags = 0;
	char stream[256];
	struct sockaddr_in si_me;
	struct hostent *host;
	struct ast_hostent ah;
	int o_slot;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(hisip);
		AST_APP_ARG(hisport);
		AST_APP_ARG(myport);
	);

	if (ast_strlen_zero(data)) return NULL;

	AST_NONSTANDARD_APP_ARGS(args,data,':');

	if ((!args.hisip) || (!args.hisip[0])) args.hisip = "127.0.0.1";
	if ((!args.hisport) || (!args.hisport[0])) args.hisport = "1234";
	if ((!args.myport) || (!args.myport[0]))  args.myport = args.hisport;

	p = ast_malloc(sizeof(struct usrp_pvt));
	if (p) {
		memset(p, 0, sizeof(struct usrp_pvt));
		
		sprintf(stream,"%s:%d",args.hisip,atoi(args.hisport));
		strcpy(p->stream,stream);
		p->rxq.qe_forw = &p->rxq;
		p->rxq.qe_back = &p->rxq;

		memset(&ah,0,sizeof(ah));
		host = ast_gethostbyname(args.hisip,&ah);
		if (!host)
		{
			ast_log(LOG_WARNING, "Unable to find host %s\n", args.hisip);
			ast_free(p);
			return NULL;
		}
		memset((char *) &p->si_other, 0, sizeof(p->si_other));
		p->si_other.sin_addr = *(struct in_addr *)host->h_addr;
		p->si_other.sin_family = AF_INET;
		p->si_other.sin_port = htons(atoi(args.hisport));

		if ((p->usrp=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
		{
			ast_log(LOG_WARNING, "Unable to create new socket for usrp connection\n");
			ast_free(p);
			return(NULL);

		}
		
		memset((char *) &si_me, 0, sizeof(si_me));
		si_me.sin_family = AF_INET;
		si_me.sin_port = htons(atoi(args.myport));
		si_me.sin_addr.s_addr = htonl(INADDR_ANY);
		if (!strncmp(ast_inet_ntoa(p->si_other.sin_addr),"127.",4))
			si_me.sin_addr.s_addr = inet_addr("127.0.0.1");
		if (bind(p->usrp, &si_me, sizeof(si_me))==-1)
		{
			ast_log(LOG_WARNING, "Unable to bind port for usrp connection\n");
			ast_free(p);
			return(NULL);

		}
		if (!p->usrp) {
			ast_log(LOG_WARNING, "Unable to allocate new usrp stream '%s' with flags %d\n", stream, flags);
			ast_free(p);
			return NULL;
		}
		// TODO: do we need locking for this?
		for (o_slot=0; o_slot<MAX_CHANS; o_slot++) {
			if (!usrp_channels[o_slot]) break;
		}
		if (o_slot >= MAX_CHANS) {
			ast_log(LOG_WARNING, "Unable to find empty usrp_channels[] entry\n");
			return NULL;
		}
		usrp_channels[o_slot] = p;
	}
	return p;
}

static int usrp_hangup(struct ast_channel *ast)
{
	struct usrp_pvt *p;
	int i;
	p = ast->tech_pvt;
	if (option_debug)
		ast_log(LOG_DEBUG, "usrp_hangup(%s)\n", ast->name);
	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	if (p->dsp) ast_dsp_free(p->dsp); 
	// TODO: do we need locking for this?
	for (i=0; i<MAX_CHANS; i++) {
		if (usrp_channels[i] == p) {
			usrp_channels[i] = NULL;
			break;
		}
	}
	if (i >= MAX_CHANS)
		ast_log(LOG_WARNING, "Unable to delete usrp_channels[] entry\n");
	usrp_destroy(p);
	ast->tech_pvt = NULL;
	ast_setstate(ast, AST_STATE_DOWN);
	return 0;
}

static int usrp_indicate(struct ast_channel *ast, int cond, const void *data, size_t datalen)
{
	struct usrp_pvt *p = ast->tech_pvt;
	struct _chan_usrp_bufhdr bufhdr;

	switch (cond) {
		case AST_CONTROL_RADIO_KEY:
			p->txkey = 1;
			break;
		case AST_CONTROL_RADIO_UNKEY:
			p->txkey = 0;
			break;
		case AST_CONTROL_HANGUP:
			return -1;
		default:
			return 0;
	}
	if (p->unkey_owed) {
		p->unkey_owed = 0;
		// tx was unkeyed - notify remote end
		memset(&bufhdr, 0, sizeof(struct _chan_usrp_bufhdr));
		memcpy(bufhdr.eye, "USRP", 4);
		bufhdr.seq = htonl(p->send_seqno++);
		if (sendto(p->usrp,&bufhdr, sizeof(bufhdr),
			0,&p->si_other,sizeof(p->si_other)) == -1) {
			if (!p->warned) {
				ast_log(LOG_WARNING, "sendto: %d\n", errno);
				p->warned = 1;
			}
		}
	}

	return 0;
}

static int usrp_text(struct ast_channel *ast, const char *text)
{
	ast_log(LOG_DEBUG, "chan_usrp: FIXME: usrp_text: %s\n", text);
	return 0;
}

static int usrp_digit_begin(struct ast_channel *ast, char digit)
{
	return 0;
}

static int usrp_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	/* no better use for received digits than print them */
	ast_verbose(" << Console Received digit %c of duration %u ms >> \n", 
		digit, duration);
	return 0;
}

static struct ast_frame  *usrp_xread(struct ast_channel *ast)
{

	struct usrp_pvt *p = ast->tech_pvt;
	char buf[512];
	struct sockaddr_in si_them;
	unsigned int themlen;
	unsigned long seq;
 	int n;
	int datalen;
	struct ast_frame fr;
    struct usrp_rxq *qp;
	struct _chan_usrp_bufhdr *bufhdrp = (struct _chan_usrp_bufhdr *) buf;
	char *bufdata = &buf[ sizeof(struct _chan_usrp_bufhdr) ];

	p->readct++;

	themlen = sizeof(struct sockaddr_in);
	if ((n = recvfrom(p->usrp,buf,sizeof(buf),0,&si_them,&themlen)) == -1)
	{
		ast_log(LOG_WARNING,"Cannot recvfrom()");
		return NULL;
	}
#if 0
	if (memcmp(&si_them.sin_addr,&p->si_other.sin_addr,sizeof(si_them.sin_addr)))
	{
		ast_log(LOG_NOTICE,"Received packet from %s, expecting it from %s\n",
			ast_inet_ntoa(si_them.sin_addr),ast_inet_ntoa(p->si_other.sin_addr));
		p->fr.frametype = 0;
		p->fr.subclass = 0;
		p->fr.datalen = 0;
		p->fr.samples = 0;
		p->fr.data =  NULL;
		p->fr.src = type;
		p->fr.offset = 0;
		p->fr.mallocd=0;
		p->fr.delivery.tv_sec = 0;
		p->fr.delivery.tv_usec = 0;
		return &p->fr;
	}
#endif
	if (n < sizeof(struct _chan_usrp_bufhdr)) {
		ast_log(LOG_NOTICE,"Received packet length %d too short\n", n);
	} else {
		datalen = n - sizeof(struct _chan_usrp_bufhdr);
		if (memcmp(bufhdrp->eye, "USRP", 4))
		{
			ast_log(LOG_NOTICE,"Received packet from %s with invalid data\n",
				ast_inet_ntoa(si_them.sin_addr));
		} else {
			seq = ntohl(bufhdrp->seq);
			if (seq != p->rxseq && seq != 0 && p->rxseq != 0) {
				fprintf(stderr, "repeater_chan_usrp: possible data loss, expected seq %lu received %lu\n", p->rxseq, seq);
			}
			p->rxseq = seq + 1;
			// TODO: TEXT processing added N4IRR
			if (datalen == USRP_VOICE_FRAME_SIZE) {
				qp = ast_malloc(sizeof(struct usrp_rxq));
				if (!qp)
				{
					ast_log(LOG_NOTICE,"Cannot malloc for qp\n");
				} else {
                                       if (bufhdrp->type == USRP_TYPE_TEXT) {
					insque((struct qelem *) qp,(struct qelem *) p->rxq.qe_back);
                                               char buf1[320];
                                               strcpy(buf1, bufdata);
                                               memset(&fr,0,sizeof(fr));
                                               fr.data =  buf1;
                                               fr.datalen = strlen(buf1) + 1;
                                               fr.samples = 0;
                                               fr.frametype = AST_FRAME_TEXT;
                                               fr.subclass = 0;
                                               fr.src = "chan_usrp";
                                               fr.offset = 0;
                                               fr.mallocd=0;
                                               fr.delivery.tv_sec = 0;
                                               fr.delivery.tv_usec = 0;
                                               ast_queue_frame(ast,&fr);
                                       } else {
                                               memcpy(qp->buf,bufdata,USRP_VOICE_FRAME_SIZE);
                                               insque((struct qelem *) qp,(struct qelem *) p->rxq.qe_back);
                                       }
				}
			}
		}
	}
	fr.datalen = 0;
	fr.samples = 0;
	fr.frametype = 0;
	fr.subclass = 0;
	fr.data =  0;
	fr.src = type;
	fr.offset = 0;
	fr.mallocd=0;
	fr.delivery.tv_sec = 0;
	fr.delivery.tv_usec = 0;

	return &p->fr;

}

static int usrp_xwrite(struct ast_channel *ast, struct ast_frame *frame)
{
	struct usrp_pvt *p = ast->tech_pvt;
	struct ast_frame fr, *f;
	struct usrp_rxq *qp;
	int n;
	char buf[USRP_VOICE_FRAME_SIZE + AST_FRIENDLY_OFFSET + SSO];

	// buffer for constructing frame, plus two ptrs: hdr and data
	char sendbuf[ sizeof(struct _chan_usrp_bufhdr) + USRP_VOICE_FRAME_SIZE];
	struct _chan_usrp_bufhdr *bufhdrp = (struct _chan_usrp_bufhdr *) sendbuf;
	char *bufdata = &sendbuf[ sizeof(struct _chan_usrp_bufhdr) ];

	if (ast->_state != AST_STATE_UP) {
		/* Don't try tos end audio on-hook */
		return 0;
	}
	/* Write a frame of (presumably voice) data */
	if (frame->frametype != AST_FRAME_VOICE) return 0;

	if (!(frame->subclass & (AST_FORMAT_SLINEAR))) {
		ast_log(LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
		return 0;
	}

	if (frame->datalen > USRP_VOICE_FRAME_SIZE) {
		ast_log(LOG_WARNING, "Frame datalen %d exceeds limit\n", frame->datalen);
		return 0;
	}

	/* if something in rx queue */
	if (p->rxq.qe_forw != &p->rxq)
	{
		for(n = 0,qp = p->rxq.qe_forw; qp != &p->rxq; qp = qp->qe_forw)
		{
			n++;
		}
		if (n > QUEUE_OVERLOAD_THRESHOLD)
		{
			while(p->rxq.qe_forw != &p->rxq)
			{
				qp = p->rxq.qe_forw;
				remque((struct qelem *)qp);
				ast_free(qp);
			}
			if (p->rxkey) p->rxkey = 1;
		}			
		else
		{
			if (!p->rxkey)
			{
				fr.datalen = 0;
				fr.samples = 0;
				fr.frametype = AST_FRAME_CONTROL;
				fr.subclass = AST_CONTROL_RADIO_KEY;
				fr.data =  0;
				fr.src = type;
				fr.offset = 0;
				fr.mallocd=0;
				fr.delivery.tv_sec = 0;
				fr.delivery.tv_usec = 0;
				ast_queue_frame(ast,&fr);
			} 
			p->rxkey = MAX_RXKEY_TIME;
			qp = p->rxq.qe_forw;
			remque((struct qelem *) qp);
			memcpy(buf + AST_FRIENDLY_OFFSET,qp->buf,USRP_VOICE_FRAME_SIZE);
			ast_free(qp);
			
			memset(&fr,0,sizeof(fr));
			fr.datalen = USRP_VOICE_FRAME_SIZE;
			fr.samples = 160;
			fr.frametype = AST_FRAME_VOICE;
			fr.subclass = AST_FORMAT_SLINEAR;
			fr.data =  buf + AST_FRIENDLY_OFFSET;
			fr.src = type;
			fr.offset = AST_FRIENDLY_OFFSET;
			fr.mallocd=0;
			fr.delivery.tv_sec = 0;
			fr.delivery.tv_usec = 0;
			ast_queue_frame(ast,&fr);
			
			if (p->usedtmf && p->dsp)
			{
				f = ast_dsp_process(ast,p->dsp,&fr);
				if ((f->frametype == AST_FRAME_DTMF_END) || (f->frametype == AST_FRAME_DTMF_BEGIN))
				{
					if ((f->subclass == 'm') || (f->subclass == 'u'))
					{
						f->frametype = AST_FRAME_NULL;
						f->subclass = 0;
						ast_queue_frame(ast, f); 
					}
					if (f->frametype == AST_FRAME_DTMF_END)
						ast_log(LOG_NOTICE,"Got DTMF char %c\n",f->subclass);
					ast_queue_frame(ast, f); 
				}
			}
		}
	}
	if (p->rxkey == 1)
	{
		memset(&fr,0,sizeof(fr));
		fr.datalen = 0;
		fr.samples = 0;
		fr.frametype = AST_FRAME_CONTROL;
		fr.subclass = AST_CONTROL_RADIO_UNKEY;
		fr.data =  0;
		fr.src = type;
		fr.offset = 0;
		fr.mallocd=0;
		fr.delivery.tv_sec = 0;
		fr.delivery.tv_usec = 0;
		ast_queue_frame(ast,&fr);
	} 
	if (p->rxkey) p->rxkey--;

	if (!p->txkey) return 0;

	p->writect++;
	p->unkey_owed = 1;
	memcpy(bufdata, frame->data, frame->datalen);
	memset(bufhdrp, 0, sizeof(struct _chan_usrp_bufhdr));
	memcpy(bufhdrp->eye, "USRP", 4);
	bufhdrp->seq = htonl(p->send_seqno++);
	bufhdrp->keyup = htonl(1);
	if (sendto(p->usrp,&sendbuf,frame->datalen + sizeof(struct _chan_usrp_bufhdr),
		0,&p->si_other,sizeof(p->si_other)) == -1) {
		if (!p->warned) {
			ast_log(LOG_WARNING, "sendto: %d\n", errno);
			p->warned = 1;
		}
		return -1;
	}

	return 0;
}

static int usrp_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
	char *cp;
	struct usrp_pvt *o = chan->tech_pvt;

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
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: OFF(0) on %s\n",chan->name);
			o->usedtmf = 1;
			break;
		case 2:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: MUTECONF/MAX(2) on %s\n",chan->name);
			o->usedtmf = 1;
			break;
		case 3:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: DISABLE DETECT(3) on %s\n",chan->name);
			o->usedtmf = 0;
			break;
		default:
			ast_log(LOG_DEBUG, "Set option TONE VERIFY, mode: OFF(0) on %s\n",chan->name);
			o->usedtmf = 1;
			break;
		}
		break;
	}
	errno = 0;
	return 0;
}

static struct ast_channel *usrp_new(struct usrp_pvt *i, int state)
{
	struct ast_channel *tmp;
	tmp = ast_channel_alloc(1, state, 0, 0, "", "s", context, 0, "usrp/%s", i->stream);
	if (tmp) {
		tmp->tech = &usrp_tech;
		tmp->fds[0] = i->usrp;
		tmp->nativeformats = prefformat;
		tmp->rawreadformat = prefformat;
		tmp->rawwriteformat = prefformat;
		tmp->writeformat = prefformat;
		tmp->readformat = prefformat;
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->tech_pvt = i;
		ast_copy_string(tmp->context, context, sizeof(tmp->context));
		ast_copy_string(tmp->exten, "s",  sizeof(tmp->exten));
		ast_string_field_set(tmp, language, "");
		i->owner = tmp;
		i->u = ast_module_user_add(tmp);
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
			}
		}
		i->dsp = ast_dsp_new();
		if (i->dsp)
		{
#ifdef  NEW_ASTERISK
          ast_dsp_set_features(i->dsp,DSP_FEATURE_DIGIT_DETECT);
          ast_dsp_set_digitmode(i->dsp,DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
#else
          ast_dsp_set_features(i->dsp,DSP_FEATURE_DTMF_DETECT);
          ast_dsp_digitmode(i->dsp,DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_RELAXDTMF);
#endif
		}
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	return tmp;
}

static struct ast_channel *usrp_request(const char *type, int format, void *data, int *cause)
{
	int oldformat;
	struct usrp_pvt *p;
	struct ast_channel *tmp = NULL;
	
	oldformat = format;
	format &= (AST_FORMAT_SLINEAR);
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		return NULL;
	}
	p = usrp_alloc(data);
	if (p) {
		tmp = usrp_new(p, AST_STATE_DOWN);
		if (!tmp)
			usrp_destroy(p);
	}
	return tmp;
}

static int unload_module(void)
{
	/* First, take us out of the channel loop */
	ast_channel_unregister(&usrp_tech);
	ast_cli_unregister(&cli_usrp_show);
	return 0;
}

static int load_module(void)
{
	ast_cli_register(&cli_usrp_show);
	/* Make sure we can register our channel type */
	if (ast_channel_register(&usrp_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		return -1;
	}
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "USRP Channel Module");
