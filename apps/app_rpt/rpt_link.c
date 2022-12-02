
/*! \file
 *
 * \brief RPT link functions
 */

#include "asterisk.h"

#include "asterisk/channel.h"

#include "app_rpt.h"
#include "rpt_link.h"

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
	/* if doesnt qual as a foreign link */
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
	/* if doesnt qual as a foreign link */
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
