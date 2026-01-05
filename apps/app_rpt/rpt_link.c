
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

#define ENABLE_CHECK_TLINK_LIST 0

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
#if ENABLE_CHECK_TLINK_LIST
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
#endif
}

void rpt_link_destroy(void *obj)
{
	struct rpt_link *doomed_link = obj;
	if (doomed_link->linklist) {
		ast_free(doomed_link->linklist);
		doomed_link->linklist = NULL;
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
	if ((!myrpt->p.duplex && !myrpt->p.linktolink) || (!nonlocals)) {
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
	return ao2_container_count(myrpt->links);
}

void FindBestRssi(struct rpt *myrpt)
{
	struct rpt_link *l;
	struct rpt_link *bl = NULL;
	int maxrssi;
	char newboss;
	struct ao2_iterator l_it;

	maxrssi = 0;
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
	RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
		if (l->lastrealrx && (l->rssi > maxrssi)) {
			maxrssi = l->rssi;
			bl = l;
		}
		l->votewinner = 0;
	}
	ao2_iterator_destroy(&l_it);

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

	ast_debug(5, "[%s] best rssi=%i from %s%s\n", myrpt->name, maxrssi, bl == NULL ? "rpt" : bl->name, newboss ? "*" : "");
}

void do_dtmf_phone(struct rpt *myrpt, struct rpt_link *mylink, char c)
{
	struct rpt_link *l;
	struct ao2_iterator l_it;

	/* go thru all the links */
	RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
		if (!l->phonemode) {
			continue;
		}
		/* dont send to self */
		if (mylink && (l == mylink)) {
			continue;
		}
		if (l->chan) {
			ast_senddigit(l->chan, c, 0);
		}
	}
	ao2_iterator_destroy(&l_it);
}

void rssi_send(struct rpt *myrpt)
{
	struct rpt_link *l;
	struct ao2_iterator l_it;
	struct ast_frame wf;
	char str[200];
	sprintf(str, "R %i", myrpt->rxrssi);
	init_text_frame(&wf, "rssi_send");
	wf.datalen = strlen(str) + 1;
	wf.data.ptr = str;
	/* otherwise, send it to all of em */
	RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
		if (l->name[0] == '0') {
			continue;
		}
		ast_debug(6, "[%s] rssi=%i to %s\n", myrpt->name, myrpt->rxrssi, l->name);
		if (l->chan) {
			rpt_qwrite(l, &wf);
		}
	}
	ao2_iterator_destroy(&l_it);
}

static int link_qwrite_cb(void *obj, void *arg, int flags)
{
	struct rpt_link *link = obj;
	struct ast_frame *wf = arg;

	if (link->chan) {
		rpt_qwrite(link, wf);
	}

	return 0;
}

void send_link_dtmf(struct rpt *myrpt, char c)
{
	char str[300];
	struct ast_frame wf;
	struct rpt_link *l;
	struct ao2_iterator l_it;

	snprintf(str, sizeof(str), "D %s %s %d %c", myrpt->cmdnode, myrpt->name, ++(myrpt->dtmfidx), c);
	init_text_frame(&wf, "send_link_dtmf");
	wf.datalen = strlen(str) + 1;
	wf.data.ptr = str;
	/* first, see if our dude is there */
	RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
		if (l->name[0] == '0') {
			continue;
		}
		/* if we found it, write it and were done */
		if (!strcmp(l->name, myrpt->cmdnode)) {
			if (l->chan) {
				rpt_qwrite(l, &wf);
			}
			ao2_ref(l, -1);
			ao2_iterator_destroy(&l_it);
			return;
		}
	}
	ao2_iterator_destroy(&l_it);
	/* if not, give it to everyone */
	ao2_callback(myrpt->links, OBJ_MULTIPLE | OBJ_NODATA, link_qwrite_cb, &wf);
}

void send_link_keyquery(struct rpt *myrpt)
{
	char str[300];
	struct ast_frame wf;

	rpt_mutex_lock(&myrpt->lock);
	memset(myrpt->topkey, 0, sizeof(myrpt->topkey));
	myrpt->topkeystate = 1;
	time(&myrpt->topkeytime);
	rpt_mutex_unlock(&myrpt->lock);
	snprintf(str, sizeof(str), "K? * %s 0 0", myrpt->name);
	init_text_frame(&wf, "send_link_keyquery");
	wf.datalen = strlen(str) + 1;
	wf.data.ptr = str;
	/* give it to everyone */
	ao2_callback(myrpt->links, OBJ_MULTIPLE | OBJ_NODATA, link_qwrite_cb, &wf);
}
void rpt_link_add(struct ao2_container *links, struct rpt_link *l)
{
	ast_assert(l != NULL);
	ao2_link(links, l);
}

void rpt_link_remove(struct ao2_container *links, struct rpt_link *l)
{
	ast_assert(l != NULL);
	ao2_unlink(links, l);
}

int __mklinklist(struct rpt *myrpt, struct rpt_link *mylink, struct ast_str **buf, int alink_format) {
	struct rpt_link *l;
	struct ao2_iterator l_it;
	char mode, *links_buf;
	int i, spos, len, links_count = 0, one_link = 0;
	if (myrpt->remote)
		return 0;
	/* go thru all links */
	RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
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
	ao2_iterator_destroy(&l_it);
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
static int link_set_list_timer_cb(void *obj, void *arg, int flags)
{
	struct rpt_link *link = obj;

	if (link->name[0] == '0') {
		return 0;
	}
	/* if found matching string */
	if (link->linklisttimer > LINKLISTSHORTTIME) {
		link->linklisttimer = LINKLISTSHORTTIME;
	}
	return CMP_MATCH;
}

void __kickshort(struct rpt *myrpt)
{
	/* Go through all links and set their timers to short time. */
	ao2_callback(myrpt->links, OBJ_MULTIPLE | OBJ_NODATA, link_set_list_timer_cb, NULL);
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

	rpt_mutex_lock(&myrpt->lock);
	n = __mklinklist(myrpt, NULL, &buf, 1);
	rpt_mutex_unlock(&myrpt->lock);
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
	rpt_mutex_lock(&myrpt->lock);
	n = __mklinklist(myrpt, NULL, &buf, 0);
	rpt_mutex_unlock(&myrpt->lock);
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

static int link_find_by_name_cb(void *obj, void *arg, int flags)
{
	struct rpt_link *link = obj;
	char *node = arg;

	if (link->name[0] == '0') {
		return 0;
	}
	/* if found matching string */
	if (!strcmp(link->name, node)) {
		return CMP_MATCH;
	}
	return 0;
}

void *rpt_link_connect(void *data)
{
	char *s, *s1, *tele, *cp;
	char tmp[300], deststr[325] = "", modechange = 0;
	char sx[320], *sy;
	char **strs; /* List of pointers to links in link list string */
	struct rpt_link *l = NULL;
	struct ast_str *lstr;
	int reconnects = 0;
	int i, ns, n = 1;
	int voterlink = 0;
	struct ast_format_cap *cap;
	struct rpt_connect_data *connect_data = data;
	struct rpt *myrpt = connect_data->myrpt;
	char *node = connect_data->digitbuf;

	if (ast_strlen_zero(node)) {
		goto cleanup;
	}

	if (tlb_query_node_exists(node)) {
		sprintf(tmp, "tlb/%s/%s", node, myrpt->name);
	} else {
		if (node[0] != '3') {
			if (node_lookup(myrpt, node, tmp, sizeof(tmp) - 1, 1)) {
				if (strlen(node) >= myrpt->longestnode) {
					rpt_telem_select(myrpt, connect_data->command_source, connect_data->mylink);
					rpt_telemetry(myrpt, CONNFAIL, NULL);
					goto cleanup; /* No such node */
				}
				goto cleanup; /* No match yet */
			}
		} else {
			if (strlen(node) < 7) {
				goto cleanup;
			}
			snprintf(tmp, sizeof(tmp), "echolink/%s/%s,%s", S_OR(myrpt->p.eloutbound, "el0"), node + 1, node + 1);
		}
	}

	if (!strcmp(myrpt->name, node)) { /* Do not allow connections to self */
		rpt_telem_select(myrpt, connect_data->command_source, connect_data->mylink);
		rpt_telemetry(myrpt, REMALREADY, NULL);
		goto cleanup;
	}

	ast_debug(2, "Connect attempt to node %s, Mode = %s, Connection type: %s\n", node,
		connect_data->mode ? "Transceive" : "Monitor", connect_data->perma ? "Permalink" : "Normal");

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
	/* try to find this one in queue */
	l = ao2_callback(connect_data->myrpt->links, 0, link_find_by_name_cb, node);
	/* if found */
	if (l) {
		if (l->connect_in_progress) {
			rpt_mutex_unlock(&myrpt->lock);
			ao2_ref(l, -1);
			/* We are already running a connect thread.*/
			goto cleanup;
		}
		/* if already in this mode, just ignore */
		if ((l->mode == connect_data->mode) || (!l->chan)) {
			rpt_mutex_unlock(&myrpt->lock);
			rpt_telem_select(myrpt, connect_data->command_source, connect_data->mylink);
			rpt_telemetry(myrpt, REMALREADY, NULL);
			ao2_ref(l, -1);
			goto cleanup; /* Already linked */
		}
		if ((CHAN_TECH(l->chan, "echolink")) || (CHAN_TECH(l->chan, "tlb"))) {
			l->mode = connect_data->mode;
			ast_copy_string(myrpt->lastlinknode, node, sizeof(myrpt->lastlinknode));
			rpt_mutex_unlock(&myrpt->lock);
			ao2_ref(l, -1);
			goto cleanup;
		}
		reconnects = l->reconnects;
		rpt_mutex_unlock(&myrpt->lock);
		if (l->chan) {
			ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
		}
		l->retries = l->max_retries + 1;
		l->disced = 2;
		modechange = 1;
		ao2_ref(l, -1);
	} else { /* Check to see if this node is already linked */
		lstr = ast_str_create(RPT_AST_STR_INIT_SIZE);
		if (!lstr) {
			rpt_mutex_unlock(&myrpt->lock);
			goto cleanup;
		}
		n = __mklinklist(myrpt, NULL, &lstr, 0) + 1;
		rpt_mutex_unlock(&myrpt->lock);
		strs = ast_malloc(n * sizeof(char *));
		if (!strs) {
			ast_free(lstr);
			goto cleanup;
		}
		ns = finddelim(ast_str_buffer(lstr), strs, n);
		for (i = 0; i < ns; i++) {
			if ((*strs[i] < '0') || (*strs[i] > '9')) {
				strs[i]++;
			}
			if (!strcmp(strs[i], node)) {
				ast_free(lstr);
				ast_free(strs);
				rpt_telem_select(myrpt, connect_data->command_source, connect_data->mylink);
				rpt_telemetry(myrpt, REMALREADY, NULL);
				goto cleanup; /* Already linked */
			}
		}
		ast_free(strs);
		ast_free(lstr);
	}
	ast_copy_string(myrpt->lastlinknode, node, sizeof(myrpt->lastlinknode));
	/* establish call */
	l = ao2_alloc(sizeof(struct rpt_link), rpt_link_destroy);
	if (!l) {
		goto cleanup;
	}
	l->linklist = ast_str_create(RPT_AST_STR_INIT_SIZE);
	if (!l->linklist) {
		ao2_ref(l, -1);
		goto cleanup;
	}
	l->connect_in_progress = 1;
	l->mode = connect_data->mode;
	l->outbound = 1;
	l->thisconnected = 0;
	voxinit_link(l, 1);
	ast_copy_string(l->name, node, sizeof(l->name));
	l->isremote = (s && ast_true(s));
	if (modechange)
		l->connected = 1;
	l->hasconnected = l->perma = connect_data->perma;
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
		l->connect_in_progress = 0;
		ao2_ref(l, -1);
		goto cleanup;
	}
	*tele++ = 0;

	cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!cap) {
		ast_log(LOG_ERROR, "Failed to alloc cap\n");
		l->connect_in_progress = 0;
		ao2_ref(l, -1);
		goto cleanup;
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
		donodelog_fmt(connect_data->myrpt, "LINKFAIL,%s/%s", deststr, tele);
		l->connect_in_progress = 0;
		ao2_ref(l, -1);
		ao2_ref(cap, -1);
		goto cleanup;
	}

	rpt_make_call(l->chan, tele, 2000, deststr, "(Remote Rx)", "remote", myrpt->name);

	if (__rpt_request_pseudo(l, cap, RPT_PCHAN, RPT_LINK_CHAN)) {
		ao2_ref(cap, -1);
		ast_hangup(l->chan);
		l->connect_in_progress = 0;
		ao2_ref(l, -1);
		goto cleanup;
	}

	ao2_ref(cap, -1);

	/* make a conference for the tx */
	if (rpt_conf_add_speaker(l->pchan, myrpt)) {
		ast_hangup(l->chan);
		ast_hangup(l->pchan);
		l->connect_in_progress = 0;
		ao2_ref(l, -1);
		goto cleanup;
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
	if (connect_data->perma) {
		l->max_retries = MAX_RETRIES_PERM;
	}
	if (l->isremote) {
		l->retries = l->max_retries + 1;
	}
	l->rxlingertimer = RX_LINGER_TIME;
	rpt_link_add(connect_data->myrpt->links, l);
	__kickshort(connect_data->myrpt);
	rpt_mutex_unlock(&connect_data->myrpt->lock);
	l->connect_in_progress = 0;
	ao2_ref(l, -1); /* Release our reference; container now owns it*/

cleanup:
	ast_free(connect_data->digitbuf);
	ast_free(connect_data);
	return NULL;
}
