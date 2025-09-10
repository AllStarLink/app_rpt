
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
	if (!myrpt) {
		return;
	}
	if (!mylink) {
		return;
	}
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
	default:
	}
}

void set_linkmode(struct rpt_link *mylink, enum rpt_linkmode linkmode)
{
	if (!mylink) {
		return;
	}
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
	default:
	}
}

int altlink(struct rpt *myrpt, struct rpt_link *mylink)
{
	if (!myrpt) {
		return 0;
	}
	if (!mylink) {
		return 0;
	}
	if (!mylink->chan) {
		return 0;
	}
	if ((myrpt->p.duplex == 3) && mylink->phonemode && myrpt->keyed) {
		return 0;
	}
	if (!mylink->phonemode &&
		(mylink->name[0] > '0') && (mylink->name[0] <= '9') &&
		!CHAN_TECH(mylink->chan, "echolink") && !CHAN_TECH(mylink->chan, "tlb")) {
		/* if doesn't qual as a foreign link */
		return 0;
	}
	if ((myrpt->p.duplex < 2) && (myrpt->tele.next == &myrpt->tele)) {
		return 0;
	}
	if (mylink->linkmode < 2) {
		return 0;
	}
	if (mylink->linkmode == 0x7fffffff) {
		return 1;
	}
	if (mylink->linkmode < 0x7ffffffe) {
		return 1;
	}
	if (myrpt->telemmode > 1) {
		return 1;
	}
	return 0;
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

/*! \brief free a link structure and it's ast_str linklist if it exists
 *  \param link the link to free
 */
void rpt_link_free(struct rpt_link *link)
{
	if (link->linklist) {
		ast_free(link->linklist);
		link->linklist = NULL;
	}
	ast_free(link);
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

	if (!myrpt) {
		return 0;
	}
	if (!mylink) {
		return 0;
	}
	if (!mylink->chan) {
		return 0;
	}
	nonlocals = 0;
	tlist = myrpt->tele.next;
	check_tlink_list(myrpt);
	if (tlist != &myrpt->tele) {
		while (tlist != &myrpt->tele) {
			if ((tlist->mode == PLAYBACK) || (tlist->mode == STATS_GPS_LEGACY) || (tlist->mode == ID1) || (tlist->mode == TEST_TONE)) {
				nonlocals++;
			}
			tlist = tlist->next;
		}
	}
	if ((!myrpt->p.duplex) || (!nonlocals)) {
		return 0;
	}
	if (!mylink->phonemode &&
		(mylink->name[0] > '0') && (mylink->name[0] <= '9') &&
		!CHAN_TECH(mylink->chan, "echolink") && !CHAN_TECH(mylink->chan, "tlb")) {
		/* if doesn't qual as a foreign link */
		return 1;
	}
	if (mylink->linkmode < 2) {
		return 0;
	}
	if (mylink->linkmode == 0x7fffffff) {
		return 1;
	}
	if (mylink->linkmode < 0x7ffffffe) {
		return 1;
	}
	if (myrpt->telemmode > 1) {
		return 1;
	}
	return 0;
}

void rpt_qwrite(struct rpt_link *l, struct ast_frame *f)
{
	struct ast_frame *f1;

	if (!l->chan) {
		return;
	}
	f1 = ast_frdup(f);
	if (!f1) {
		return;
	}
	memset(&f1->frame_list, 0, sizeof(f1->frame_list));
	AST_LIST_INSERT_TAIL(&l->textq, f1, frame_list);
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
	if (myrpt->votewinner && myrpt->rxchankeyed) {
		myrpt->voted_rssi = myrpt->rxrssi;
	} else if (myrpt->voted_link != NULL && myrpt->voted_link->lastrealrx) {
		myrpt->voted_rssi = myrpt->voted_link->rssi;
	}
	if (myrpt->rxchankeyed) {
		maxrssi = myrpt->rxrssi;
	}

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
		if (bl == NULL && myrpt->rxchankeyed) {
			myrpt->votewinner = 1;
		} else if (bl != NULL) {
			bl->votewinner = 1;
		}
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
		if (l->chan) {
			ast_senddigit(l->chan, c, 0);
		}
		l = l->next;
	}
}

void rssi_send(struct rpt *myrpt)
{
	struct rpt_link *l;
	struct ast_frame wf;
	char str[200];
	sprintf(str, "R %i", myrpt->rxrssi);
	init_text_frame(&wf, "rssi_send");
	wf.datalen = strlen(str) + 1;
	wf.data.ptr = str;
	l = myrpt->links.next;
	/* otherwise, send it to all of em */
	while (l != &myrpt->links) {
		if (l->name[0] == '0') {
			l = l->next;
			continue;
		}
		ast_debug(6, "[%s] rssi=%i to %s\n", myrpt->name, myrpt->rxrssi, l->name);
		if (l->chan) {
			rpt_qwrite(l, &wf);
		}
		l = l->next;
	}
}

void send_link_dtmf(struct rpt *myrpt, char c)
{
	char str[300];
	struct ast_frame wf;
	struct rpt_link *l;

	snprintf(str, sizeof(str), "D %s %s %d %c", myrpt->cmdnode, myrpt->name, ++(myrpt->dtmfidx), c);
	init_text_frame(&wf, "send_link_dtmf");
	wf.datalen = strlen(str) + 1;
	wf.data.ptr = str;
	l = myrpt->links.next;
	/* first, see if our dude is there */
	while (l != &myrpt->links) {
		if (l->name[0] == '0') {
			l = l->next;
			continue;
		}
		/* if we found it, write it and were done */
		if (!strcmp(l->name, myrpt->cmdnode)) {
			if (l->chan) {
				rpt_qwrite(l, &wf);
			}
			return;
		}
		l = l->next;
	}
	l = myrpt->links.next;
	/* if not, give it to everyone */
	while (l != &myrpt->links) {
		if (l->chan) {
			rpt_qwrite(l, &wf);
		}
		l = l->next;
	}
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
	init_text_frame(&wf, "send_link_keyquery");
	wf.datalen = strlen(str) + 1;
	wf.data.ptr = str;
	l = myrpt->links.next;
	/* give it to everyone */
	while (l != &myrpt->links) {
		if (l->chan) {
			rpt_qwrite(l, &wf);
		}
		l = l->next;
	}
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

int __mklinklist(struct rpt *myrpt, struct rpt_link *mylink, struct ast_str **buf, int alink_format) {
	struct rpt_link *l;
	char mode, *links_buf;
	int i, spos, len, links_count = 0, one_link = 0;
	if (myrpt->remote)
		return 0;
	/* go thru all links */
	for (l = myrpt->links.next; l != &myrpt->links; l = l->next) {
		/* if is not a real link, ignore it */
		if (l->name[0] == '0') {
			continue;
		}
		if (l->mode == MODE_LOCAL_MONITOR) {
			continue; /* dont report local modes */
		}
		/* dont count our stuff */
		if (l == mylink) {
			continue;
		}
		if (mylink && !strcmp(l->name, mylink->name)) {
			continue;
		}
		/* figure out mode to report */
		mode = 'T'; /* use Transceive by default */
		if (l->mode == MODE_MONITOR) {
			mode = 'R'; /* indicate RX for our mode */
		}
		if (!l->thisconnected) {
			mode = 'C'; /* indicate connecting */
		}
		spos = ast_str_strlen(*buf); /* current buf size (b4 we add our stuff) */
		if (spos > 2) {
			ast_str_append(buf, 0, "%s", ",");
		}
	    one_link = 1;
		if (alink_format) { /* RPT_ALINK format - only show adjacent nodes*/
			ast_str_append(buf, 0, "%s%c%c", l->name, mode, (l->lastrx1) ? 'K' : 'U');
		} else { /* RPT_LINK format - show all nodes*/
			/* add nodes into buffer */
			if (ast_str_strlen(l->linklist)) {
				ast_str_append(buf, 0, "%c%s,%s", mode, l->name, ast_str_buffer(l->linklist));
			} else { /* if no nodes, add this node into buffer */
				ast_str_append(buf, 0, "%c%s", mode, l->name);
			}
		}
		/* if we are in transceive mode, let all modes stand */
		if (mode == 'T') {
			continue;
		}
		/* downgrade everyone on this node if appropriate */
		links_buf = ast_str_buffer(*buf);
		len = ast_str_strlen(*buf);
		for (i = spos; i < len; i++) {
			if (links_buf[i] == 'T') {
				links_buf[i] = mode;
			}
			if ((links_buf[i] == 'R') && (mode == 'C')) {
				links_buf[i] = mode;
			}
		}
	}
	/* After building the string, count number of nodes (commas) in buffer string. The first
	 * node doesn't have a comma, so we need to add 1 if there is at least one_link.  
	 */
	links_count = 0;
	links_buf = ast_str_buffer(*buf);
	for (i = 0; i < ast_str_strlen(*buf); i++) {
		if (links_buf[i] == ',') {
			links_count++;
		}
	}
	if (one_link) { /* The first link in the list has no comma but we have 1 link */
		links_count++;
	}

	return links_count;
}

void __kickshort(struct rpt *myrpt)
{
	struct rpt_link *l;

	for (l = myrpt->links.next; l != &myrpt->links; l = l->next) {
		/* if is not a real link, ignore it */
		if (l->name[0] == '0') {
			continue;
		}
		if (l->linklisttimer > LINKLISTSHORTTIME) {
			l->linklisttimer = LINKLISTSHORTTIME;
		}
	}
	if (myrpt->linkposttimer > LINKPOSTSHORTTIME) {
		myrpt->linkposttimer = LINKPOSTSHORTTIME;
	}
	myrpt->lastgpstime = 0;
}

void rpt_update_links(struct rpt *myrpt)
{
	struct ast_str *buf, *obuf;
	int n;

	buf = ast_str_create(RPT_AST_STR_INIT_SIZE);
	if (!buf) {
		return;
	}
	obuf = ast_str_create(RPT_AST_STR_INIT_SIZE);
	if (!obuf) {
		ast_free(buf);
		return;
	}

	ast_mutex_lock(&myrpt->lock);
	n = __mklinklist(myrpt, NULL, &buf, 1);
	ast_mutex_unlock(&myrpt->lock);
	/* parse em */
	if (n) {
		ast_str_set(&obuf, 0, "%d,%s", n, ast_str_buffer(buf));
	}
	pbx_builtin_setvar_helper(myrpt->rxchannel, "RPT_ALINKS", ast_str_buffer(obuf));
	rpt_manager_trigger(myrpt, "RPT_ALINKS", ast_str_buffer(obuf));
	ast_str_set(&obuf, 0, "%d", n);
	pbx_builtin_setvar_helper(myrpt->rxchannel, "RPT_NUMALINKS", ast_str_buffer(obuf));
	rpt_manager_trigger(myrpt, "RPT_NUMALINKS", ast_str_buffer(obuf));

	ast_str_reset(buf);
	ast_mutex_lock(&myrpt->lock);
	n = __mklinklist(myrpt, NULL, &buf, 0);
	ast_mutex_unlock(&myrpt->lock);
	if (n) {
		ast_str_set(&obuf, 0, "%d,%s", n, ast_str_buffer(buf));
	}
	pbx_builtin_setvar_helper(myrpt->rxchannel, "RPT_LINKS", ast_str_buffer(obuf));
	rpt_manager_trigger(myrpt, "RPT_LINKS", ast_str_buffer(obuf));
	ast_str_set(&obuf, 0, "%d", n);
	pbx_builtin_setvar_helper(myrpt->rxchannel, "RPT_NUMLINKS", ast_str_buffer(obuf));
	rpt_manager_trigger(myrpt, "RPT_NUMLINKS", ast_str_buffer(obuf));
	rpt_event_process(myrpt);

	ast_free(buf);
	ast_free(obuf);
}

int connect_link(struct rpt *myrpt, char *node, enum link_mode mode, int perma)
{
	char *s, *s1, *tele, *cp;
	char tmp[300], deststr[325] = "", modechange = 0;
	char sx[320], *sy;
	char **strs; /* List of pointers to links in link list string */
	struct rpt_link *l;
	struct ast_str *lstr;
	int reconnects = 0;
	int i, ns, n = 1;
	int voterlink = 0;
	struct ast_format_cap *cap;

	if (strlen(node) < 1)
		return 1;

	if (tlb_query_node_exists(node)) {
		sprintf(tmp, "tlb/%s/%s", node, myrpt->name);
	} else {
		if (node[0] != '3') {
			if (node_lookup(myrpt, node, tmp, sizeof(tmp) - 1, 1)) {
				if (strlen(node) >= myrpt->longestnode) {
					return -1;	/* No such node */
				}
				return 1;		/* No match yet */
			}
		} else {
			if (strlen(node) < 7) {
				return 1;
			}
			snprintf(tmp, sizeof(tmp), "echolink/%s/%s,%s", S_OR(myrpt->p.eloutbound, "el0"), node + 1, node + 1);		
		}
	}

	if (!strcmp(myrpt->name, node)) { /* Do not allow connections to self */
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
		if (!strcmp(l->name, node)) {
			break;
		}
		l = l->next;
	}
	/* if found */
	if (l != &myrpt->links) {
		/* if already in this mode, just ignore */
		if ((l->mode == mode) || (!l->chan)) {
			rpt_mutex_unlock(&myrpt->lock);
			return 2;			/* Already linked */
		}
		if ((CHAN_TECH(l->chan, "echolink")) || (CHAN_TECH(l->chan, "tlb"))) {
			l->mode = mode;
			ast_copy_string(myrpt->lastlinknode, node, sizeof(myrpt->lastlinknode));
			rpt_mutex_unlock(&myrpt->lock);
			return 0;
		}
		reconnects = l->reconnects;
		rpt_mutex_unlock(&myrpt->lock);
		if (l->chan) {
			ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
		}
		l->retries = l->max_retries + 1;
		l->disced = 2;
		modechange = 1;
	} else { /* Check to see if this node is already linked */
		lstr = ast_str_create(RPT_AST_STR_INIT_SIZE);
		if (!lstr) {
			rpt_mutex_unlock(&myrpt->lock);
			return -1;
		}
		n = __mklinklist(myrpt, NULL, &lstr, 0) + 1;
		rpt_mutex_unlock(&myrpt->lock);
		strs = ast_malloc(n * sizeof(char *));
		if (!strs) {
			ast_free(lstr);
			return -1;
		}
		ns = finddelim(ast_str_buffer(lstr), strs, n);
		for (i = 0; i < ns; i++) {
			if ((*strs[i] < '0') || (*strs[i] > '9')) {
				strs[i]++;
			}
			if (!strcmp(strs[i], node)) {
				ast_free(lstr);
				ast_free(strs);
				return 2; /* Already linked */
			}
		}
		ast_free(strs);
		ast_free(lstr);
	}
	ast_copy_string(myrpt->lastlinknode, node, sizeof(myrpt->lastlinknode));
	/* establish call */
	l = ast_calloc(1, sizeof(struct rpt_link));
	if (!l) {
		return -1;
	}
	l->linklist = ast_str_create(RPT_AST_STR_INIT_SIZE);
	if (!l->linklist) {
		ast_free(l);
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
		rpt_link_free(l);
		return -1;
	}
	*tele++ = 0;

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		rpt_link_free(l);
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
		donodelog_fmt(myrpt, "LINKFAIL,%s/%s", deststr, tele);
		rpt_link_free(l);
		ao2_ref(cap, -1);
		return -1;
	}

	rpt_make_call(l->chan, tele, 2000, deststr, "(Remote Rx)", "remote", myrpt->name);

	if (__rpt_request_pseudo(l, cap, RPT_PCHAN, RPT_LINK_CHAN)) {
		ao2_ref(cap, -1);
		ast_hangup(l->chan);
		rpt_link_free(l);
		return -1;
	}

	ao2_ref(cap, -1);

	/* make a conference for the tx */
	if (rpt_conf_add_speaker(l->pchan, myrpt)) {
		ast_hangup(l->chan);
		ast_hangup(l->pchan);
		rpt_link_free(l);
		return -1;
	}
	rpt_mutex_lock(&myrpt->lock);
	if (tlb_query_node_exists(node)) {
		init_linkmode(myrpt, l, LINKMODE_TLB);
	} else if (node[0] == '3') {
		init_linkmode(myrpt, l, LINKMODE_ECHOLINK);
	} else {
		l->linkmode = 0;
	}
	l->reconnects = reconnects;
	/* insert at end of queue */
	l->max_retries = MAX_RETRIES;
	if (perma) {
		l->max_retries = MAX_RETRIES_PERM;
	}
	if (l->isremote) {
		l->retries = l->max_retries + 1;
	}
	l->rxlingertimer = RX_LINGER_TIME;
	rpt_link_add(myrpt, l);
	__kickshort(myrpt);
	rpt_mutex_unlock(&myrpt->lock);
	return 0;
}
