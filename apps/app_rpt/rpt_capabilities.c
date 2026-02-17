
#include "asterisk.h"

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"

#include "app_rpt.h"
#include "rpt_capabilities.h"

int iswebtransceiver(struct rpt_link *l)
{
	int i;

	if (!l)
		return 0;
	for (i = 0; l->name[i]; i++) {
		if (!isdigit(l->name[i]))
			return 1;
	}
	return 0;
}

int multimode_capable(struct rpt *myrpt)
{
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT897))
		return 1;
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT100))
		return 1;
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT950))
		return 1;
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_IC706))
		return 1;
	return 0;
}

int narrow_capable(struct rpt *myrpt)
{
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD))
		return 1;
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_TMD700))
		return 1;
	if (!strcmp(myrpt->remoterig, REMOTE_RIG_TM271))
		return 1;
	return 0;
}

char is_paging(struct rpt *myrpt)
{
	char rv = 0;

	if ((!ast_tvzero(myrpt->paging)) && (ast_tvdiff_ms(ast_tvnow(), myrpt->paging) <= 300000))
		rv = 1;
	return (rv);
}
