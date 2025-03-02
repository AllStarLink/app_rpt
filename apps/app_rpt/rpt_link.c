
/*! \file
 *
 * \brief RPT link functions
 */

#include "asterisk.h"

#include <search.h>

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/format_cache.h" /* use ast_format_slin */
#include "asterisk/lock.h"

#include "app_rpt.h"
#include "rpt_utils.h"
#include "rpt_manager.h"
#include "rpt_lock.h"
#include "rpt_config.h"
#include "rpt_bridging.h"
#include "rpt_call.h"
#include "rpt_vox.h"
#include "rpt_link.h"
#include "rpt_telemetry.h"

#define OBUFSIZE(size) (size + sizeof("123456,")) /* size of buffer + room for node count + comma */
#define BUFSIZE(size) (size)

void init_linkmode(struct rpt *myrpt, struct rpt_link *mylink, int linktype)
{

	if (!myrpt)
		return;
	if (!mylink)
		return;
	switch (myrpt->p.linkmode[linktype]) {
	case LINKMODE_OFF:
		mylink->linkmode = 0;
		break;
	case LINKMODE_ON:
		mylink->linkmode = 0x7fffffff;
		break;
	case LINKMODE_FOLLOW:
		mylink->linkmode = 0x7ffffffe;
		break;
	case LINKMODE_DEMAND:
		mylink->linkmode = 1;
		break;
	}
	return;
}

void set_linkmode(struct rpt_link *mylink, int linkmode)
{

	if (!mylink)
		return;
	switch (linkmode) {
	case LINKMODE_OFF:
		mylink->linkmode = 0;
		break;
	case LINKMODE_ON:
		mylink->linkmode = 0x7fffffff;
		break;
	case LINKMODE_FOLLOW:
		mylink->linkmode = 0x7ffffffe;
		break;
	case LINKMODE_DEMAND:
		mylink->linkmode = 1;
		break;
	}
	return;
}

int altlink(struct rpt *myrpt, struct rpt_link *mylink)
{
	if (!myrpt)
		return (0);
	if (!mylink)
		return (0);
	if (!mylink->chan)
		return (0);
	if ((myrpt->p.duplex == 3) && mylink->phonemode && myrpt->keyed)
		return (0);
	/* if doesn't qual as a foreign link */
	if ((mylink->name[0] > '0') && (mylink->name[0] <= '9') &&
		(!mylink->phonemode) && strcasecmp(ast_channel_tech(mylink->chan)->type, "echolink")
		&& strcasecmp(ast_channel_tech(mylink->chan)->type, "tlb"))
		return (0);
	if ((myrpt->p.duplex < 2) && (myrpt->tele.next == &myrpt->tele))
		return (0);
	if (mylink->linkmode < 2)
		return (0);
	if (mylink->linkmode == 0x7fffffff)
		return (1);
	if (mylink->linkmode < 0x7ffffffe)
		return (1);
	if (myrpt->telemmode > 1)
		return (1);
	return (0);
}

static void check_tlink_list(struct rpt *myrpt)
{
	struct rpt_tele *tlist;

	/* This is supposed to be a doubly linked list,
	 * so make sure it's not corrupted.
	 * If it is, that should trigger the assertion.
	 * This is temporary, for troubleshooting issue #261.
	 * Once that is fixed, this should be removed.
	 */
	tlist = myrpt->tele.next;
	for (tlist = myrpt->tele.next; tlist != &myrpt->tele; tlist = tlist->next) {
		if (!tlist) {
			ast_log(LOG_ERROR, "tlist linked list is corrupted (not properly doubly linked)\n");
		}
		ast_assert(tlist != NULL);
	}
}

void tele_link_add(struct rpt *myrpt, struct rpt_tele *t)
{
	ast_assert(t != NULL);
	check_tlink_list(myrpt);
	insque(t, myrpt->tele.next);
	check_tlink_list(myrpt);
}

void tele_link_remove(struct rpt *myrpt, struct rpt_tele *t)
{
	ast_assert(t != NULL);
	check_tlink_list(myrpt);
	remque(t);
	check_tlink_list(myrpt);
}

int altlink1(struct rpt *myrpt, struct rpt_link *mylink)
{
	struct rpt_tele *tlist;
	int nonlocals;

	if (!myrpt)
		return (0);
	if (!mylink)
		return (0);
	if (!mylink->chan)
		return (0);
	nonlocals = 0;
	tlist = myrpt->tele.next;
	check_tlink_list(myrpt);
	if (tlist != &myrpt->tele) {
		while (tlist != &myrpt->tele) {
			if ((tlist->mode == PLAYBACK) || (tlist->mode == STATS_GPS_LEGACY) || (tlist->mode == ID1)
				|| (tlist->mode == TEST_TONE))
				nonlocals++;
			tlist = tlist->next;
		}
	}
	if ((!myrpt->p.duplex) || (!nonlocals))
		return (0);
	/* if doesn't qual as a foreign link */
	if ((mylink->name[0] > '0') && (mylink->name[0] <= '9') &&
		(!mylink->phonemode) && strcasecmp(ast_channel_tech(mylink->chan)->type, "echolink")
		&& strcasecmp(ast_channel_tech(mylink->chan)->type, "tlb"))
		return (1);
	if (mylink->linkmode < 2)
		return (0);
	if (mylink->linkmode == 0x7fffffff)
		return (1);
	if (mylink->linkmode < 0x7ffffffe)
		return (1);
	if (myrpt->telemmode > 1)
		return (1);
	return (0);
}

void rpt_qwrite(struct rpt_link *l, struct ast_frame *f)
{
	struct ast_frame *f1;

	if (!l->chan)
		return;
	f1 = ast_frdup(f);
	memset(&f1->frame_list, 0, sizeof(f1->frame_list));
	AST_LIST_INSERT_TAIL(&l->textq, f1, frame_list);
	return;
}

int linkcount(struct rpt *myrpt)
{
	struct rpt_link *l;
	int numoflinks;

	numoflinks = 0;
	l = myrpt->links.next;
	while (l && (l != &myrpt->links)) {
		if (numoflinks >= MAX_STAT_LINKS) {
			ast_log(LOG_WARNING, "maximum number of links exceeds %d in rpt_do_stats()!", MAX_STAT_LINKS);
			break;
		}
#if 0
		if (l->name[0] == '0'){ /* Skip '0' nodes */
		      l = l->next;
		      continue;
		}
#endif
		numoflinks++;

		l = l->next;
	}

	return numoflinks;
}

int FindBestRssi(struct rpt *myrpt)
{
	struct rpt_link *l;
	struct rpt_link *bl;
	int maxrssi;
	int numoflinks;
	char newboss;

	bl = NULL;
	maxrssi = 0;
	numoflinks = 0;
	newboss = 0;

	myrpt->voted_rssi = 0;
	if (myrpt->votewinner && myrpt->rxchankeyed)
		myrpt->voted_rssi = myrpt->rxrssi;
	else if (myrpt->voted_link != NULL && myrpt->voted_link->lastrealrx)
		myrpt->voted_rssi = myrpt->voted_link->rssi;

	if (myrpt->rxchankeyed)
		maxrssi = myrpt->rxrssi;

	l = myrpt->links.next;
	while (l && (l != &myrpt->links)) {
		if (numoflinks >= MAX_STAT_LINKS) {
			ast_log(LOG_WARNING, "[%s] number of links exceeds limit of %d \n", myrpt->name, MAX_STAT_LINKS);
			break;
		}
		if (l->lastrealrx && (l->rssi > maxrssi)) {
			maxrssi = l->rssi;
			bl = l;
		}
		l->votewinner = 0;
		numoflinks++;
		l = l->next;
	}

	if (!myrpt->voted_rssi || (myrpt->voted_link == NULL && !myrpt->votewinner)
		|| (maxrssi > (myrpt->voted_rssi + myrpt->p.votermargin))
		) {
		newboss = 1;
		myrpt->votewinner = 0;
		if (bl == NULL && myrpt->rxchankeyed)
			myrpt->votewinner = 1;
		else if (bl != NULL)
			bl->votewinner = 1;
		myrpt->voted_link = bl;
		myrpt->voted_rssi = maxrssi;
	}

	ast_debug(5, "[%s] links=%i best rssi=%i from %s%s\n",
		myrpt->name, numoflinks, maxrssi, bl == NULL ? "rpt" : bl->name, newboss ? "*" : "");

	return numoflinks;
}

void do_dtmf_phone(struct rpt *myrpt, struct rpt_link *mylink, char c)
{
	struct rpt_link *l;

	l = myrpt->links.next;
	/* go thru all the links */
	while (l != &myrpt->links) {
		if (!l->phonemode) {
			l = l->next;
			continue;
		}
		/* dont send to self */
		if (mylink && (l == mylink)) {
			l = l->next;
			continue;
		}
		if (l->chan)
			ast_senddigit(l->chan, c, 0);
		l = l->next;
	}
	return;
}

void rssi_send(struct rpt *myrpt)
{
	struct rpt_link *l;
	struct ast_frame wf;
	char str[200];
	sprintf(str, "R %i", myrpt->rxrssi);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass.format = ast_format_slin;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	wf.src = "rssi_send";

	l = myrpt->links.next;
	/* otherwise, send it to all of em */
	while (l != &myrpt->links) {
		if (l->name[0] == '0') {
			l = l->next;
			continue;
		}
		wf.data.ptr = str;
		ast_debug(6, "[%s] rssi=%i to %s\n", myrpt->name, myrpt->rxrssi, l->name);
		if (l->chan)
			rpt_qwrite(l, &wf);
		l = l->next;
	}
}

void send_link_dtmf(struct rpt *myrpt, char c)
{
	char str[300];
	struct ast_frame wf;
	struct rpt_link *l;

	snprintf(str, sizeof(str), "D %s %s %d %c", myrpt->cmdnode, myrpt->name, ++(myrpt->dtmfidx), c);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass.format = ast_format_slin;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	wf.src = "send_link_dtmf";
	l = myrpt->links.next;
	/* first, see if our dude is there */
	while (l != &myrpt->links) {
		if (l->name[0] == '0') {
			l = l->next;
			continue;
		}
		/* if we found it, write it and were done */
		if (!strcmp(l->name, myrpt->cmdnode)) {
			wf.data.ptr = str;
			if (l->chan)
				rpt_qwrite(l, &wf);
			return;
		}
		l = l->next;
	}
	l = myrpt->links.next;
	/* if not, give it to everyone */
	while (l != &myrpt->links) {
		wf.data.ptr = str;
		if (l->chan)
			rpt_qwrite(l, &wf);
		l = l->next;
	}
	return;
}

void send_link_keyquery(struct rpt *myrpt)
{
	char str[300];
	struct ast_frame wf;
	struct rpt_link *l;

	rpt_mutex_lock(&myrpt->lock);
	memset(myrpt->topkey, 0, sizeof(myrpt->topkey));
	myrpt->topkeystate = 1;
	time(&myrpt->topkeytime);
	rpt_mutex_unlock(&myrpt->lock);
	snprintf(str, sizeof(str), "K? * %s 0 0", myrpt->name);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass.format = ast_format_slin;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	wf.src = "send_link_keyquery";
	l = myrpt->links.next;
	/* give it to everyone */
	while (l != &myrpt->links) {
		wf.data.ptr = str;
		if (l->chan)
			rpt_qwrite(l, &wf);
		l = l->next;
	}
	return;
}

void send_tele_link(struct rpt *myrpt, char *cmd)
{
	char str[400];
	struct ast_frame wf;
	struct rpt_link *l;

	snprintf(str, sizeof(str) - 1, "T %s %s", myrpt->name, cmd);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass.format = ast_format_slin;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	wf.src = "send_tele_link";
	l = myrpt->links.next;
	/* give it to everyone */
	while (l != &myrpt->links) {
		wf.data.ptr = str;
		if (l->chan && (l->mode == 1))
			rpt_qwrite(l, &wf);
		l = l->next;
	}
	rpt_telemetry(myrpt, VARCMD, cmd);
	return;
}

static void check_link_list(struct rpt *myrpt)
{
	struct rpt_link *l;

	/* This is supposed to be a doubly linked list,
	 * so make sure it's not corrupted.
	 * If it is, that should trigger the assertion.
	 * This is temporary, for troubleshooting issue #217.
	 * Once that is fixed, this should be removed.
	 */
	l = myrpt->links.next;
	for (l = myrpt->links.next; l != &myrpt->links; l = l->next) {
		if (!l) {
			ast_log(LOG_ERROR, "Link linked list is corrupted (not properly doubly linked)\n");
		}
		ast_assert(l != NULL);
	}
}

void rpt_link_add(struct rpt *myrpt, struct rpt_link *l)
{
	ast_assert(l != NULL);
	check_link_list(myrpt);
	insque(l, myrpt->links.next);
	check_link_list(myrpt);
}

void rpt_link_remove(struct rpt *myrpt, struct rpt_link *l)
{
	ast_assert(l != NULL);
	check_link_list(myrpt);
	remque(l);
	check_link_list(myrpt);
}

/*!
 * \brief Get required buffer size for link message. 
 * \retval		buffer size.
 */

int __get_nodelist_size (struct rpt *myrpt)
{
	struct rpt_link *l;
	int buffer_size_links = 0, buffer_size_alinks = 0, link_size;

	for (l = myrpt->links.next; l != &myrpt->links; l = l->next) {
		/* if is not a real link, ignore it */
		if (l->name[0] == '0') {
			continue;
		}
		if (l->mode > 1) {
			continue;			/* Don't report local modes */
		}
		/* Calculate size of buffer required to build the list of linked repeaters.
		 * RPT_ALINKS format = <count>,1234TU,4321TK,2233RU
		 * RPT_LINKS format = <count>,T1234,R4321,T2233
		 * Buffer size ALINKS = LINKS with additional U/K characters for each.
		 */
		if (l->linklist[0]) {
			buffer_size_links += (strlen(l->linklist) + 1); /* +1: 1 for comma */
		}
		link_size = strlen(l->name);
		buffer_size_links += (link_size + 2);  /* +2: 1 for comma, 1 form mode (T/R) */
		buffer_size_alinks += (link_size + 3); /* +3: 1 for comma, 2 for mode (TU/TK/etc) */
	}

	return (buffer_size_links > buffer_size_alinks) ? buffer_size_links : buffer_size_alinks;
}

int __mklinklist(struct rpt *myrpt, struct rpt_link *mylink, char *buf, size_t bufsize, int flag)
{
	struct rpt_link *l;
	char mode;
	int i, spos, link_count, one_link;

	buf[0] = 0;					/* clear output buffer */
	link_count = 0;
	one_link = 0;
	if (myrpt->remote)
		return 0;
	/* go thru all links */
	for (l = myrpt->links.next; l != &myrpt->links; l = l->next) {
		/* if is not a real link, ignore it */
		if (l->name[0] == '0')
			continue;
		if (l->mode > 1)
			continue;			/* dont report local modes */
		/* dont count our stuff */
		if (l == mylink)
			continue;
		if (mylink && (!strcmp(l->name, mylink->name)))
			continue;
		/* figure out mode to report */
		mode = 'T';				/* use Tranceive by default */
		if (!l->mode)
			mode = 'R';			/* indicate RX for our mode */
		if (!l->thisconnected)
			mode = 'C';			/* indicate connecting */
		spos = strlen(buf);		/* current buf size (b4 we add our stuff) */
		if (spos) {
			strcat(buf, ",");
			spos++;
		}
	    one_link = 1;
		if (flag) { /* RPT_ALINK format - only show adjacent nodes*/
			snprintf(buf + spos, bufsize - spos, "%s%c%c", l->name, mode, (l->lastrx1) ? 'K' : 'U');
		} else { /* RPT_LINK format - show all nodes*/
			/* add nodes into buffer */
			if (l->linklist[0]) {
				snprintf(buf + spos, bufsize - spos, "%c%s,%s", mode, l->name, l->linklist);
			} else {			/* if no nodes, add this node into buffer */
				snprintf(buf + spos, bufsize - spos, "%c%s", mode, l->name);
			}
		}
		/* if we are in tranceive mode, let all modes stand */
		if (mode == 'T')
			continue;
		/* downgrade everyone on this node if appropriate */
		for (i = spos; buf[i]; i++) {
			if (buf[i] == 'T')
				buf[i] = mode;
			if ((buf[i] == 'R') && (mode == 'C'))
				buf[i] = mode;
		}
	}
	/* After building the string, count number of nodes (commas) in buffer string. The first
	 * node doesn't have a comma, so we need to add 1 if there is at least one_link.  
	 */
	for (link_count = 0; buf[link_count]; buf[link_count]==',' ? link_count++: *buf++);
	if (one_link) { /* The first link in the list has no comma but we have 1 link */
		link_count++;
	}

	return link_count;
}

void __kickshort(struct rpt *myrpt)
{
	struct rpt_link *l;

	for (l = myrpt->links.next; l != &myrpt->links; l = l->next) {
		/* if is not a real link, ignore it */
		if (l->name[0] == '0')
			continue;
		l->linklisttimer = LINKLISTSHORTTIME;
	}
	myrpt->linkposttimer = LINKPOSTSHORTTIME;
	myrpt->lastgpstime = 0;
	return;
}

void rpt_update_links(struct rpt *myrpt)
{
	char *buf, *obuf;
	int buffer_size, n;
	/* figure out the RPT_LINK string size - this will be the largest size
	 * RPT_ALINK is always a subset of RPT_LINK
	 */
	ast_mutex_lock(&myrpt->lock);
	buffer_size = __get_nodelist_size(myrpt);
	buf = ast_calloc(1, BUFSIZE(buffer_size));
	if (!buf) {
		ast_mutex_unlock(&myrpt->lock);
		return;
	}
	obuf = ast_calloc(1, OBUFSIZE(buffer_size));
	if (!obuf) {
		ast_mutex_unlock(&myrpt->lock);
		ast_free(buf);
		return;
	}
	n = __mklinklist(myrpt, NULL, buf, BUFSIZE(buffer_size), 1);
	/* parse em */
	if (n) {
		snprintf(obuf, OBUFSIZE(buffer_size), "%d,%s", n, buf);
	} else {
		strcpy(obuf, "0");
	}
	pbx_builtin_setvar_helper(myrpt->rxchannel, "RPT_ALINKS", obuf);
	rpt_manager_trigger(myrpt, "RPT_ALINKS", obuf);
	snprintf(obuf, OBUFSIZE(buffer_size), "%d", n);
	pbx_builtin_setvar_helper(myrpt->rxchannel, "RPT_NUMALINKS", obuf);
	rpt_manager_trigger(myrpt, "RPT_NUMALINKS", obuf);
	n = __mklinklist(myrpt, NULL, buf, BUFSIZE(buffer_size), 0);

	ast_mutex_unlock(&myrpt->lock);
	if (n) {
		snprintf(obuf, OBUFSIZE(buffer_size), "%d,%s", n, buf);
	} else {
		strcpy(obuf, "0");
	}
	pbx_builtin_setvar_helper(myrpt->rxchannel, "RPT_LINKS", obuf);
	rpt_manager_trigger(myrpt, "RPT_LINKS", obuf);
	snprintf(obuf, OBUFSIZE(buffer_size), "%d", n);
	pbx_builtin_setvar_helper(myrpt->rxchannel, "RPT_NUMLINKS", obuf);
	rpt_manager_trigger(myrpt, "RPT_NUMLINKS", obuf);
	rpt_event_process(myrpt);

	ast_free(buf);
	ast_free(obuf);
	return;
}

int connect_link(struct rpt *myrpt, char *node, int mode, int perma)
{
	char *s, *s1, *tele, *cp;
	char lstr[MAXLINKLIST], *strs[MAXLINKLIST];
	char tmp[300], deststr[325] = "", modechange = 0;
	char sx[320], *sy;
	struct rpt_link *l;
	int reconnects = 0;
	int i, n;
	int voterlink = 0;
	struct ast_format_cap *cap;

	if (strlen(node) < 1)
		return 1;

	if (tlb_query_node_exists(node)) {
		sprintf(tmp, "tlb/%s/%s", node, myrpt->name);
	} else {
		if (node[0] != '3') {
			if (node_lookup(myrpt, node, tmp, sizeof(tmp) - 1, 1)) {
				if (strlen(node) >= myrpt->longestnode)
					return -1;	/* No such node */
				return 1;		/* No match yet */
			}
		} else {
			if (strlen(node) < 7) {
				return 1;
			}
			snprintf(tmp, sizeof(tmp), "echolink/%s/%s,%s", S_OR(myrpt->p.eloutbound, "el0"), node + 1, node + 1);		
		}
	}

	if (!strcmp(myrpt->name, node)) {	/* Do not allow connections to self */
		return -2;
	}

	ast_debug(2, "Connect attempt to node %s, Mode = %s, Connection type: %s\n", node, mode ? "Transceive" : "Monitor", perma ? "Permalink" : "Normal");

	s = NULL;
	s1 = tmp;
	if (strncasecmp(tmp, "tlb", 3)) {	/* if not tlb */
		s = tmp;
		s1 = strsep(&s, ",");
		if (!strchr(s1, ':') && strchr(s1, '/') && strncasecmp(s1, "local/", 6) && strncasecmp(s1, "echolink/", 9)) {
			sy = strchr(s1, '/');
			*sy = 0;
			sprintf(sx, "%s:4569/%s", s1, sy + 1);
			s1 = sx;
		}
		strsep(&s, ",");
	}
	if (s && strcmp(s, "VOTE") == 0) {
		voterlink = 1;
		ast_debug(1, "NODE is a VOTER.\n");
	}
	rpt_mutex_lock(&myrpt->lock);
	l = myrpt->links.next;
	/* try to find this one in queue */
	while (l != &myrpt->links) {
		if (l->name[0] == '0') {
			l = l->next;
			continue;
		}
		/* if found matching string */
		if (!strcmp(l->name, node))
			break;
		l = l->next;
	}
	/* if found */
	if (l != &myrpt->links) {
		/* if already in this mode, just ignore */
		if ((l->mode == mode) || (!l->chan)) {
			rpt_mutex_unlock(&myrpt->lock);
			return 2;			/* Already linked */
		}
		if ((!strcasecmp(ast_channel_tech(l->chan)->type, "echolink"))
			|| (!strcasecmp(ast_channel_tech(l->chan)->type, "tlb"))) {
			l->mode = mode;
			ast_copy_string(myrpt->lastlinknode, node, sizeof(myrpt->lastlinknode));
			rpt_mutex_unlock(&myrpt->lock);
			return 0;
		}
		reconnects = l->reconnects;
		rpt_mutex_unlock(&myrpt->lock);
		if (l->chan)
			ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
		l->retries = l->max_retries + 1;
		l->disced = 2;
		modechange = 1;
	} else {
		__mklinklist(myrpt, NULL, lstr, sizeof(lstr), 0);
		rpt_mutex_unlock(&myrpt->lock);
		n = finddelim(lstr, strs, ARRAY_LEN(strs));
		for (i = 0; i < n; i++) {
			if ((*strs[i] < '0') || (*strs[i] > '9'))
				strs[i]++;
			if (!strcmp(strs[i], node)) {
				return 2;		/* Already linked */
			}
		}
	}
	ast_copy_string(myrpt->lastlinknode, node, sizeof(myrpt->lastlinknode));
	/* establish call */
	l = ast_calloc(1, sizeof(struct rpt_link));
	if (!l) {
		return -1;
	}
	l->mode = mode;
	l->outbound = 1;
	l->thisconnected = 0;
	voxinit_link(l, 1);
	ast_copy_string(l->name, node, sizeof(l->name));
	l->isremote = (s && ast_true(s));
	if (modechange)
		l->connected = 1;
	l->hasconnected = l->perma = perma;
	l->newkeytimer = NEWKEYTIME;
	l->iaxkey = 0;
	l->link_newkey = RADIO_KEY_NOT_ALLOWED;
	l->voterlink = voterlink;
	if (strncasecmp(s1, "echolink/", 9) == 0) {
		l->link_newkey = RADIO_KEY_ALLOWED;
	}
	if (!strncasecmp(s1, "iax2/", 5) || !strncasecmp(s1, "echolink/", 9) || !strncasecmp(s1, "tlb/", 4)
#ifdef ALLOW_LOCAL_CHANNELS
		|| !strncasecmp(s1, "local/", 6)
#endif
		) {
		ast_copy_string(deststr, s1, sizeof(deststr) - 1);
	} else {
		snprintf(deststr, sizeof(deststr), "IAX2/%s", s1);
	}
	tele = strchr(deststr, '/');
	if (!tele) {
		ast_log(LOG_WARNING, "link3:Dial number (%s) must be in format tech/number\n", deststr);
		ast_free(l);
		return -1;
	}
	*tele++ = 0;

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		ast_free(l);
		return -1;
	}

	ast_format_cap_append(cap, ast_format_slin, 0);

	if (!strncasecmp(deststr, "echolink", 8)) {
		char tel1[100];

		ast_copy_string(tel1, tele, sizeof(tel1));
		cp = strchr(tel1, '/');
		if (cp) {
			cp++;
		} else {
			cp = tel1;
		}
		strcpy(cp, node + 1);
		l->chan = ast_request(deststr, cap, NULL, NULL, tel1, NULL);
	} else {
		l->chan = ast_request(deststr, cap, NULL, NULL, tele, NULL);
	}
	if (!l->chan) {
		ast_log(LOG_WARNING, "Unable to place call to %s/%s\n", deststr, tele);
		if (myrpt->p.archivedir) {
			donodelog_fmt(myrpt, "LINKFAIL,%s/%s", deststr, tele);
		}
		ast_free(l);
		ao2_ref(cap, -1);
		return -1;
	}

	rpt_make_call(l->chan, tele, 2000, deststr, "(Remote Rx)", "remote", myrpt->name);

	if (__rpt_request_pseudo(l, cap, RPT_PCHAN, RPT_LINK_CHAN)) {
		ao2_ref(cap, -1);
		ast_hangup(l->chan);
		ast_free(l);
		return -1;
	}

	ao2_ref(cap, -1);

	/* make a conference for the tx */
	if (rpt_conf_add_speaker(l->pchan, myrpt)) {
		ast_hangup(l->chan);
		ast_hangup(l->pchan);
		ast_free(l);
		return -1;
	}
	rpt_mutex_lock(&myrpt->lock);
	if (tlb_query_node_exists(node))
		init_linkmode(myrpt, l, LINKMODE_TLB);
	else if (node[0] == '3')
		init_linkmode(myrpt, l, LINKMODE_ECHOLINK);
	else
		l->linkmode = 0;
	l->reconnects = reconnects;
	/* insert at end of queue */
	l->max_retries = MAX_RETRIES;
	if (perma)
		l->max_retries = MAX_RETRIES_PERM;
	if (l->isremote)
		l->retries = l->max_retries + 1;
	l->rxlingertimer = ((l->iaxkey) ? RX_LINGER_TIME_IAXKEY : RX_LINGER_TIME);
	rpt_link_add(myrpt, l);
	__kickshort(myrpt);
	rpt_mutex_unlock(&myrpt->lock);
	return 0;
}
