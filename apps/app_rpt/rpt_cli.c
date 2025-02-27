
/*! \file
 *
 * \brief RPT CLI commands
 */

#include "asterisk.h"

#include <search.h>

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/cli.h"
#include "asterisk/format_cache.h" /* use ast_format_slin */

#include "app_rpt.h"
#include "rpt_lock.h"
#include "rpt_link.h"
#include "rpt_cli.h"
#include "rpt_utils.h"
#include "rpt_config.h"
#include "rpt_manager.h"
#include "rpt_telemetry.h"

extern struct rpt rpt_vars[MAXRPTS];

/*! \brief Enable or disable debug output at a given level at the console */
static int rpt_do_debug(int fd, int argc, const char *const *argv)
{
	int oldlevel, newlevel;

	if (argc != 4) {
		return RESULT_SHOWUSAGE;
	}

	newlevel = myatoi(ast_strdupa(argv[3]));

	oldlevel = rpt_set_debug_level(newlevel);
	if (oldlevel < 0) {
		return RESULT_SHOWUSAGE;
	}

	if (newlevel) {
		ast_cli(fd, "app_rpt Debugging enabled, previous level: %d, new level: %d\n", oldlevel, newlevel);
	} else {
		ast_cli(fd, "app_rpt Debugging disabled\n");
	}
	return RESULT_SUCCESS;
}

/*! \brief Dump rpt struct debugging onto console */
static int rpt_do_dump(int fd, int argc, const char *const *argv)
{
	int i;
	int nrpts = rpt_num_rpts();

	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(argv[2], rpt_vars[i].name)) {
			rpt_vars[i].disgorgetime = time(NULL) + 10;	/* Do it 10 seconds later */
			ast_cli(fd, "app_rpt struct dump requested for node %s\n", argv[2]);
			return RESULT_SUCCESS;
		}
	}
	return RESULT_FAILURE;
}

/*! \brief Dump statistics to console */
static int rpt_do_stats(int fd, int argc, const char *const *argv)
{
	int i, j, numoflinks;
	int dailytxtime, dailykerchunks;
	time_t now;
	int totalkerchunks, dailykeyups, totalkeyups, timeouts;
	int totalexecdcommands, dailyexecdcommands, hours, minutes, seconds;
	int uptime;
	long long totaltxtime;
	struct rpt_link *l;
	char *listoflinks[MAX_STAT_LINKS];
	char *lastdtmfcommand, *parrot_ena;
	char *tot_state, *ider_state, *patch_state;
	char *reverse_patch_state, *sys_ena, *tot_ena, *link_ena, *patch_ena;
	char *sch_ena, *input_signal, *called_number, *user_funs, *tail_type;
	char *iconns;
	struct rpt *myrpt;
	int nrpts = rpt_num_rpts();

	static char *not_applicable = "N/A";

	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}

	tot_state = ider_state = patch_state = reverse_patch_state = input_signal = not_applicable;
	called_number = lastdtmfcommand = NULL;

	time(&now);
	for (i = 0; i < nrpts; i++) {
		if (!strcmp(argv[2], rpt_vars[i].name)) {
			/* Make a copy of all stat variables while locked */
			myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock);	/* LOCK */
			uptime = (int) (now - rpt_starttime());
			dailytxtime = myrpt->dailytxtime;
			totaltxtime = myrpt->totaltxtime;
			dailykeyups = myrpt->dailykeyups;
			totalkeyups = myrpt->totalkeyups;
			dailykerchunks = myrpt->dailykerchunks;
			totalkerchunks = myrpt->totalkerchunks;
			dailyexecdcommands = myrpt->dailyexecdcommands;
			totalexecdcommands = myrpt->totalexecdcommands;
			timeouts = myrpt->timeouts;

			/* Traverse the list of connected nodes */
			reverse_patch_state = "DOWN";
			numoflinks = 0;
			l = myrpt->links.next;
			while (l && (l != &myrpt->links)) {
				if (numoflinks >= MAX_STAT_LINKS) {
					ast_log(LOG_WARNING, "Maximum number of links exceeds %d in rpt_do_stats()!", MAX_STAT_LINKS);
					break;
				}
				if (l->name[0] == '0') {	/* Skip '0' nodes */
					reverse_patch_state = "UP";
					l = l->next;
					continue;
				}
				listoflinks[numoflinks] = ast_strdup(l->name);
				if (listoflinks[numoflinks] == NULL) {
					break;
				} else {
					numoflinks++;
				}
				l = l->next;
			}

			if (myrpt->keyed)
				input_signal = "YES";
			else
				input_signal = "NO";

			if (myrpt->p.parrotmode)
				parrot_ena = "ENABLED";
			else
				parrot_ena = "DISABLED";

			if (myrpt->p.s[myrpt->p.sysstate_cur].txdisable)
				sys_ena = "DISABLED";
			else
				sys_ena = "ENABLED";

			if (myrpt->p.s[myrpt->p.sysstate_cur].totdisable)
				tot_ena = "DISABLED";
			else
				tot_ena = "ENABLED";

			if (myrpt->p.s[myrpt->p.sysstate_cur].linkfundisable)
				link_ena = "DISABLED";
			else
				link_ena = "ENABLED";

			if (myrpt->p.s[myrpt->p.sysstate_cur].autopatchdisable)
				patch_ena = "DISABLED";
			else
				patch_ena = "ENABLED";

			if (myrpt->p.s[myrpt->p.sysstate_cur].schedulerdisable)
				sch_ena = "DISABLED";
			else
				sch_ena = "ENABLED";

			if (myrpt->p.s[myrpt->p.sysstate_cur].userfundisable)
				user_funs = "DISABLED";
			else
				user_funs = "ENABLED";

			if (myrpt->p.s[myrpt->p.sysstate_cur].alternatetail)
				tail_type = "ALTERNATE";
			else
				tail_type = "STANDARD";

			if (myrpt->p.s[myrpt->p.sysstate_cur].noincomingconns)
				iconns = "DISABLED";
			else
				iconns = "ENABLED";

			if (!myrpt->totimer)
				tot_state = "TIMED OUT!";
			else if (myrpt->totimer != myrpt->p.totime)
				tot_state = "ARMED";
			else
				tot_state = "RESET";

			if (myrpt->tailid)
				ider_state = "QUEUED IN TAIL";
			else if (myrpt->mustid)
				ider_state = "QUEUED FOR CLEANUP";
			else
				ider_state = "CLEAN";

			switch (myrpt->callmode) {
			case 1:
				patch_state = "DIALING";
				break;
			case 2:
				patch_state = "CONNECTING";
				break;
			case 3:
				patch_state = "UP";
				break;

			case 4:
				patch_state = "CALL FAILED";
				break;

			default:
				patch_state = "DOWN";
			}

			if (strlen(myrpt->exten)) {
				called_number = ast_strdup(myrpt->exten);
			}

			if (strlen(myrpt->lastdtmfcommand)) {
				lastdtmfcommand = ast_strdup(myrpt->lastdtmfcommand);
			}
			rpt_mutex_unlock(&myrpt->lock);	/* UNLOCK */

			ast_cli(fd, "************************ NODE %s STATISTICS *************************\n\n", myrpt->name);
			ast_cli(fd, "Selected system state............................: %d\n", myrpt->p.sysstate_cur);
			ast_cli(fd, "Signal on input..................................: %s\n", input_signal);
			ast_cli(fd, "System...........................................: %s\n", sys_ena);
			ast_cli(fd, "Parrot Mode......................................: %s\n", parrot_ena);
			ast_cli(fd, "Scheduler........................................: %s\n", sch_ena);
			ast_cli(fd, "Tail Time........................................: %s\n", tail_type);
			ast_cli(fd, "Time out timer...................................: %s\n", tot_ena);
			ast_cli(fd, "Incoming connections.............................: %s\n", iconns);
			ast_cli(fd, "Time out timer state.............................: %s\n", tot_state);
			ast_cli(fd, "Time outs since system initialization............: %d\n", timeouts);
			ast_cli(fd, "Identifier state.................................: %s\n", ider_state);
			ast_cli(fd, "Kerchunks today..................................: %d\n", dailykerchunks);
			ast_cli(fd, "Kerchunks since system initialization............: %d\n", totalkerchunks);
			ast_cli(fd, "Keyups today.....................................: %d\n", dailykeyups);
			ast_cli(fd, "Keyups since system initialization...............: %d\n", totalkeyups);
			ast_cli(fd, "DTMF commands today..............................: %d\n", dailyexecdcommands);
			ast_cli(fd, "DTMF commands since system initialization........: %d\n", totalexecdcommands);
			ast_cli(fd, "Last DTMF command executed.......................: %s\n",
					(lastdtmfcommand && strlen(lastdtmfcommand)) ? lastdtmfcommand : not_applicable);
			hours = dailytxtime / 3600000;
			dailytxtime %= 3600000;
			minutes = dailytxtime / 60000;
			dailytxtime %= 60000;
			seconds = dailytxtime / 1000;
			dailytxtime %= 1000;

			ast_cli(fd, "TX time today....................................: %02d:%02d:%02d:%02d\n", hours, minutes,
					seconds, dailytxtime);

			hours = (int) totaltxtime / 3600000;
			totaltxtime %= 3600000;
			minutes = (int) totaltxtime / 60000;
			totaltxtime %= 60000;
			seconds = (int) totaltxtime / 1000;
			totaltxtime %= 1000;

			ast_cli(fd, "TX time since system initialization..............: %02d:%02d:%02d:%02d\n", hours, minutes,
					seconds, (int) totaltxtime);

			hours = uptime / 3600;
			uptime %= 3600;
			minutes = uptime / 60;
			uptime %= 60;

			ast_cli(fd, "Uptime...........................................: %02d:%02d:%02d\n", hours, minutes, uptime);

			ast_cli(fd, "Nodes currently connected to us..................: ");
			if (!numoflinks) {
				ast_cli(fd, "<NONE>");
			} else {
				for (j = 0; j < numoflinks; j++) {
					ast_cli(fd, "%s", listoflinks[j]);
					if (j % 4 == 3) {
						ast_cli(fd, "\n");
						ast_cli(fd, "                                                 : ");
					} else {
						if ((numoflinks - 1) - j > 0)
							ast_cli(fd, ", ");
					}
				}
			}
			ast_cli(fd, "\n");

			ast_cli(fd, "Autopatch........................................: %s\n", patch_ena);
			ast_cli(fd, "Autopatch state..................................: %s\n", patch_state);
			ast_cli(fd, "Autopatch called number..........................: %s\n",
					(called_number && strlen(called_number)) ? called_number : not_applicable);
			ast_cli(fd, "Reverse patch/IAXRPT connected...................: %s\n", reverse_patch_state);
			ast_cli(fd, "User linking commands............................: %s\n", link_ena);
			ast_cli(fd, "User functions...................................: %s\n\n", user_funs);

			for (j = 0; j < numoflinks; j++) {	/* Free all link names */
				ast_free(listoflinks[j]);
			}
			if (called_number) {
				ast_free(called_number);
			}
			if (lastdtmfcommand) {
				ast_free(lastdtmfcommand);
			}
			return RESULT_SUCCESS;
		}
	}
	return RESULT_FAILURE;
}

/*! \brief Link stats function */
static int rpt_do_lstats(int fd, int argc, const char *const *argv)
{
	int i;
	char *connstate;
	struct rpt *myrpt;
	struct rpt_link *l;
	struct rpt_lstat *s, *t;
	struct rpt_lstat s_head;
	int nrpts = rpt_num_rpts();

	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}

	s = NULL;
	s_head.next = &s_head;
	s_head.prev = &s_head;

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(argv[2], rpt_vars[i].name)) {
			/* Make a copy of all stat variables while locked */
			myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock);	/* LOCK */
			/* Traverse the list of connected nodes */
			l = myrpt->links.next;
			while (l && (l != &myrpt->links)) {
				if (l->name[0] == '0') {	/* Skip '0' nodes */
					l = l->next;
					continue;
				}
				if ((s = (struct rpt_lstat *) ast_malloc(sizeof(struct rpt_lstat))) == NULL) {
					ast_log(LOG_ERROR, "Malloc failed in rpt_do_lstats\n");
					rpt_mutex_unlock(&myrpt->lock);	/* UNLOCK */
					return RESULT_FAILURE;
				}
				memset(s, 0, sizeof(struct rpt_lstat));
				ast_copy_string(s->name, l->name, MAXNODESTR - 1);
				if (l->chan)
					pbx_substitute_variables_helper(l->chan, "${IAXPEER(CURRENTCHANNEL)}", s->peer, MAXPEERSTR - 1);
				else
					strcpy(s->peer, "(none)");
				s->mode = l->mode;
				s->outbound = l->outbound;
				s->reconnects = l->reconnects;
				s->connecttime = l->connecttime;
				s->thisconnected = l->thisconnected;
				memcpy(s->chan_stat, l->chan_stat, NRPTSTAT * sizeof(struct rpt_chan_stat));
				insque((struct qelem *) s, (struct qelem *) s_head.next);
				memset(l->chan_stat, 0, NRPTSTAT * sizeof(struct rpt_chan_stat));
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);	/* UNLOCK */
			ast_cli(fd, "NODE      PEER                RECONNECTS  DIRECTION  CONNECT TIME        CONNECT STATE\n");
			ast_cli(fd, "----      ----                ----------  ---------  ------------        -------------\n");

			for (s = s_head.next; s != &s_head; s = s->next) {
				int hours, minutes, seconds;
				long long connecttime = s->connecttime;
				char conntime[21];
				hours = connecttime / 3600000L;
				connecttime %= 3600000L;
				minutes = connecttime / 60000L;
				connecttime %= 60000L;
				seconds = connecttime / 1000L;
				connecttime %= 1000L;
				snprintf(conntime, 20, "%02d:%02d:%02d:%02d", hours, minutes, seconds, (int) connecttime);
				conntime[20] = 0;
				if (s->thisconnected)
					connstate = "ESTABLISHED";
				else
					connstate = "CONNECTING";
				ast_cli(fd, "%-10s%-20s%-12d%-11s%-20s%-20s\n", s->name, s->peer, s->reconnects,
						(s->outbound) ? "OUT" : "IN", conntime, connstate);
			}
			/* destroy our local link queue */
			s = s_head.next;
			while (s != &s_head) {
				t = s;
				s = s->next;
				remque((struct qelem *) t);
				ast_free(t);
			}
			return RESULT_SUCCESS;
		}
	}
	return RESULT_FAILURE;
}

static int rpt_do_xnode(int fd, int argc, const char *const *argv)
{
	int i, j;
	unsigned int ns;
	char lbuf[MAXLINKLIST], *strs[MAXLINKLIST];
	struct rpt *myrpt;
	struct ast_var_t *newvariable;
	char *connstate;
	struct rpt_link *l;
	struct rpt_lstat *s, *t;
	struct rpt_lstat s_head;
	int nrpts = rpt_num_rpts();

	char *parrot_ena, *sys_ena, *tot_ena, *link_ena, *patch_ena, *patch_state;
	char *sch_ena, *user_funs, *tail_type, *iconns, *tot_state, *ider_state, *tel_mode;

	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}

	s = NULL;
	s_head.next = &s_head;
	s_head.prev = &s_head;

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(argv[2], rpt_vars[i].name)) {
			/* Make a copy of all stat variables while locked */
			myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock);	/* LOCK */

//### GET RPT STATUS STATES WHILE LOCKED ########################
			if (myrpt->p.parrotmode)
				parrot_ena = "1";	//"ENABLED";
			else
				parrot_ena = "0";	//"DISABLED";

			if (myrpt->p.s[myrpt->p.sysstate_cur].txdisable)
				sys_ena = "0";	//"DISABLED";
			else
				sys_ena = "1";	//"ENABLED";

			if (myrpt->p.s[myrpt->p.sysstate_cur].totdisable)
				tot_ena = "0";	//"DISABLED";
			else
				tot_ena = "1";	//"ENABLED";

			if (myrpt->p.s[myrpt->p.sysstate_cur].linkfundisable)
				link_ena = "0";	//"DISABLED";
			else
				link_ena = "1";	//"ENABLED";

			if (myrpt->p.s[myrpt->p.sysstate_cur].autopatchdisable)
				patch_ena = "0";	//"DISABLED";
			else
				patch_ena = "1";	//"ENABLED";

			if (myrpt->p.s[myrpt->p.sysstate_cur].schedulerdisable)
				sch_ena = "0";	//"DISABLED";
			else
				sch_ena = "1";	//"ENABLED";

			if (myrpt->p.s[myrpt->p.sysstate_cur].userfundisable)
				user_funs = "0";	//"DISABLED";
			else
				user_funs = "1";	//"ENABLED";

			if (myrpt->p.s[myrpt->p.sysstate_cur].alternatetail)
				tail_type = "1";	//"ALTERNATE";
			else
				tail_type = "0";	//"STANDARD";

			if (myrpt->p.s[myrpt->p.sysstate_cur].noincomingconns)
				iconns = "0";	//"DISABLED";
			else
				iconns = "1";	//"ENABLED";

			if (!myrpt->totimer)
				tot_state = "0";	//"TIMED OUT!";
			else if (myrpt->totimer != myrpt->p.totime)
				tot_state = "1";	//"ARMED";
			else
				tot_state = "2";	//"RESET";

			if (myrpt->tailid)
				ider_state = "0";	//"QUEUED IN TAIL";
			else if (myrpt->mustid)
				ider_state = "1";	//"QUEUED FOR CLEANUP";
			else
				ider_state = "2";	//"CLEAN";

			switch (myrpt->callmode) {
			case 1:
				patch_state = "0";	//"DIALING";
				break;
			case 2:
				patch_state = "1";	//"CONNECTING";
				break;
			case 3:
				patch_state = "2";	//"UP";
				break;

			case 4:
				patch_state = "3";	//"CALL FAILED";
				break;

			default:
				patch_state = "4";	//"DOWN";
			}

			if (myrpt->p.telemdynamic) {
				if (myrpt->telemmode == 0x7fffffff)
					tel_mode = "1";
				else if (myrpt->telemmode == 0x00)
					tel_mode = "0";
				else
					tel_mode = "2";
			} else {
				tel_mode = "3";
			}

			/* ### GET CONNECTED NODE INFO ####################
			 * Traverse the list of connected nodes
			 */

			rpt_mutex_lock(&myrpt->lock);
			__mklinklist(myrpt, NULL, lbuf, sizeof(lbuf), 0);
			rpt_mutex_unlock(&myrpt->lock);
			j = 0;
			l = myrpt->links.next;
			while (l && (l != &myrpt->links)) {
				if (l->name[0] == '0') {	// Skip '0' nodes 
					l = l->next;
					continue;
				}
				if ((s = (struct rpt_lstat *) ast_malloc(sizeof(struct rpt_lstat))) == NULL) {
					ast_log(LOG_ERROR, "Malloc failed in rpt_do_lstats\n");
					rpt_mutex_unlock(&myrpt->lock);	// UNLOCK 
					return RESULT_FAILURE;
				}
				memset(s, 0, sizeof(struct rpt_lstat));
				ast_copy_string(s->name, l->name, MAXNODESTR - 1);
				if (l->chan)
					pbx_substitute_variables_helper(l->chan, "${IAXPEER(CURRENTCHANNEL)}", s->peer, MAXPEERSTR - 1);
				else
					strcpy(s->peer, "(none)");
				s->mode = l->mode;
				s->outbound = l->outbound;
				s->reconnects = l->reconnects;
				s->connecttime = l->connecttime;
				s->thisconnected = l->thisconnected;
				memcpy(s->chan_stat, l->chan_stat, NRPTSTAT * sizeof(struct rpt_chan_stat));
				insque((struct qelem *) s, (struct qelem *) s_head.next);
				memset(l->chan_stat, 0, NRPTSTAT * sizeof(struct rpt_chan_stat));
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);	// UNLOCK 
			for (s = s_head.next; s != &s_head; s = s->next) {
				int hours, minutes, seconds;
				long long connecttime = s->connecttime;
				char conntime[21];
				hours = connecttime / 3600000L;
				connecttime %= 3600000L;
				minutes = connecttime / 60000L;
				connecttime %= 60000L;
				seconds = (int) connecttime / 1000L;
				connecttime %= 1000;
				snprintf(conntime, 20, "%02d:%02d:%02d", hours, minutes, seconds);
				conntime[20] = 0;
				if (s->thisconnected)
					connstate = "ESTABLISHED";
				else
					connstate = "CONNECTING";
				ast_cli(fd, "%-10s%-20s%-12d%-11s%-20s%-20s~", s->name, s->peer, s->reconnects,
						(s->outbound) ? "OUT" : "IN", conntime, connstate);
			}
			ast_cli(fd, "\n\n");
			// destroy our local link queue 
			s = s_head.next;
			while (s != &s_head) {
				t = s;
				s = s->next;
				remque((struct qelem *) t);
				ast_free(t);
			}

//### GET ALL LINKED NODES INFO ####################
			/* parse em */
			ns = finddelim(lbuf, strs, ARRAY_LEN(strs));
			/* sort em */
			if (ns)
				qsort((void *) strs, ns, sizeof(char *), mycompar);
			for (j = 0;; j++) {
				if (!strs[j]) {
					if (!j) {
						ast_cli(fd, "<NONE>");
					}
					break;
				}
				ast_cli(fd, "%s", strs[j]);
				if (strs[j + 1])
					ast_cli(fd, ", ");

			}
			ast_cli(fd, "\n\n");

//### GET VARIABLES INFO ####################
			j = 0;
			ast_channel_lock(rpt_vars[i].rxchannel);
			AST_LIST_TRAVERSE(ast_channel_varshead(rpt_vars[i].rxchannel), newvariable, entries) {
				j++;
				ast_cli(fd, "%s=%s\n", ast_var_name(newvariable), ast_var_value(newvariable));
			}
			ast_channel_unlock(rpt_vars[i].rxchannel);
			ast_cli(fd, "\n");

//### OUTPUT RPT STATUS STATES ##############
			ast_cli(fd, "parrot_ena=%s\n", parrot_ena);
			ast_cli(fd, "sys_ena=%s\n", sys_ena);
			ast_cli(fd, "tot_ena=%s\n", tot_ena);
			ast_cli(fd, "link_ena=%s\n", link_ena);
			ast_cli(fd, "patch_ena=%s\n", patch_ena);
			ast_cli(fd, "patch_state=%s\n", patch_state);
			ast_cli(fd, "sch_ena=%s\n", sch_ena);
			ast_cli(fd, "user_funs=%s\n", user_funs);
			ast_cli(fd, "tail_type=%s\n", tail_type);
			ast_cli(fd, "iconns=%s\n", iconns);
			ast_cli(fd, "tot_state=%s\n", tot_state);
			ast_cli(fd, "ider_state=%s\n", ider_state);
			ast_cli(fd, "tel_mode=%s\n\n", tel_mode);

			return RESULT_SUCCESS;
		}
	}
	return RESULT_FAILURE;
}

/*! \brief List all nodes connected, directly or indirectly */
static int rpt_do_nodes(int fd, int argc, const char *const *argv)
{
	int i, j;
	unsigned int ns;
	char lbuf[MAXLINKLIST], *strs[MAXLINKLIST];
	struct rpt *myrpt;
	int nrpts = rpt_num_rpts();

	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(argv[2], rpt_vars[i].name)) {
			/* Make a copy of all stat variables while locked */
			myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock);	/* LOCK */
			__mklinklist(myrpt, NULL, lbuf, sizeof(lbuf), 0);
			rpt_mutex_unlock(&myrpt->lock);	/* UNLOCK */
			/* parse em */
			ns = finddelim(lbuf, strs, ARRAY_LEN(strs));
			/* sort em */
			if (ns)
				qsort((void *) strs, ns, sizeof(char *), mycompar);
			ast_cli(fd, "\n");
			ast_cli(fd, "************************* CONNECTED NODES *************************\n\n");
			for (j = 0;; j++) {
				if (!strs[j]) {
					if (!j) {
						ast_cli(fd, "<NONE>");
					}
					break;
				}
				ast_cli(fd, "%s", strs[j]);
				if (j % 8 == 7) {
					ast_cli(fd, "\n");
				} else {
					if (strs[j + 1])
						ast_cli(fd, ", ");
				}
			}
			ast_cli(fd, "\n\n");
			return RESULT_SUCCESS;
		}
	}
	return RESULT_FAILURE;
}

/*! \brief List all locally configured nodes */
static int rpt_do_local_nodes(int fd, int argc, const char *const *argv)
{
	int i;
	int nrpts = rpt_num_rpts();

	ast_cli(fd, "                         \nNode\n----\n");
	for (i = 0; i < nrpts; i++) {
		if (rpt_vars[i].name[0]) {
			ast_cli(fd, "%s\n", rpt_vars[i].name);
		}
	}							/* for i */
	ast_cli(fd, "\n");
	return RESULT_SUCCESS;
}

/*! \brief Restart app_rpt */
static int rpt_do_restart(int fd, int argc, const char *const *argv)
{
	int i;
	int nrpts = rpt_num_rpts();

	if (argc > 2) {
		return RESULT_SHOWUSAGE;
	}
	for (i = 0; i < nrpts; i++) {
		if (rpt_vars[i].rxchannel) {
			ast_softhangup(rpt_vars[i].rxchannel, AST_SOFTHANGUP_DEV);
		}
	}
	return RESULT_FAILURE;
}

/*! \brief Send an app_rpt DTMF function from the CLI */
static int rpt_do_fun(int fd, int argc, const char *const *argv)
{
	int i, busy = 0;
	int nrpts = rpt_num_rpts();

	if (argc != 4) {
		return RESULT_SHOWUSAGE;
	}

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(argv[2], rpt_vars[i].name)) {
			struct rpt *myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock);
			if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(argv[3])) {
				rpt_mutex_unlock(&myrpt->lock);
				busy = 1;
			}
			if (!busy) {
				myrpt->macrotimer = MACROTIME;
				strncat(myrpt->macrobuf, argv[3], MAXMACRO - 1);
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
	}
	if (busy) {
		ast_cli(fd, "Function decoder busy");
	}
	return RESULT_FAILURE;
}

/*! \brief Send an Audio File from the CLI */
static int rpt_do_playback(int fd, int argc, const char *const *argv)
{
	int i;
	int nrpts = rpt_num_rpts();

	if (argc != 4) {
		return RESULT_SHOWUSAGE;
	}

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(argv[2], rpt_vars[i].name)) {
			struct rpt *myrpt = &rpt_vars[i];
			rpt_telemetry(myrpt, PLAYBACK, (void *) argv[3]);
		}
	}
	return RESULT_SUCCESS;
}

static int rpt_do_localplay(int fd, int argc, const char *const *argv)
{
	int i;
	int nrpts = rpt_num_rpts();

	if (argc != 4) {
		return RESULT_SHOWUSAGE;
	}

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(argv[2], rpt_vars[i].name)) {
			struct rpt *myrpt = &rpt_vars[i];
			rpt_telemetry(myrpt, LOCALPLAY, (void *) argv[3]);
		}
	}
	return RESULT_SUCCESS;
}

static int rpt_do_sendtext(int fd, int argc, const char *const *argv)
{
	int i;
	struct rpt_link *l;
	char str[MAX_TEXTMSG_SIZE];
	char *from, *to;
	int nrpts = rpt_num_rpts();

	if (argc < 5) {
		return RESULT_SHOWUSAGE;
	}

	from = ast_strdupa(argv[2]);
	to = ast_strdupa(argv[3]);

	string_toupper(from);
	string_toupper(to);
	snprintf(str, sizeof(str) - 1, "M %s %s ", from, to);
	for (i = 4; i < argc; i++) {
		if (i > 3)
			strncat(str, " ", sizeof(str) - 1);
		strncat(str, argv[i], sizeof(str) - 1);
	}
	for (i = 0; i < nrpts; i++) {
		if (!strcmp(from, rpt_vars[i].name)) {
			struct rpt *myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
			/* otherwise, send it to all of em */
			while (l != &myrpt->links) {
				if (l->name[0] == '0') {
					l = l->next;
					continue;
				}
				if (l->chan)
					ast_sendtext(l->chan, str);
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
	}
	return RESULT_SUCCESS;
}

/*! \brief Paging function */
static int rpt_do_page(int fd, int argc, const char *const *argv)
{
	int i;
	char str[MAX_TEXTMSG_SIZE];
	struct rpt_tele *telem;
	char *nodename, *baud, *capcode, *text;
	int nrpts = rpt_num_rpts();

	if (argc < 7) {
		return RESULT_SHOWUSAGE;
	}

	nodename = ast_strdupa(argv[2]);
	baud = ast_strdupa(argv[3]);
	capcode = ast_strdupa(argv[4]);
	text = ast_strdupa(argv[5]);

	string_toupper(nodename);
	string_toupper(baud);
	string_toupper(capcode);
	string_toupper(text);
	snprintf(str, sizeof(str) - 1, "PAGE %s %s %s ", baud, capcode, text);
	for (i = 6; i < argc; i++) {
		if (i > 5) {
			strncat(str, " ", sizeof(str) - 1);
		}
		strncat(str, argv[i], sizeof(str) - 1);
	}
	for (i = 0; i < nrpts; i++) {
		if (!strcmp(nodename, rpt_vars[i].name)) {
			struct rpt *myrpt = &rpt_vars[i];
			/* ignore if not a channel that can accept the paging command */
			if (strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "voter") &&
				strcasecmp(ast_channel_tech(myrpt->rxchannel)->type, "simpleusb")) {
				return RESULT_SUCCESS;
			}
			/* if we are playing telemetry, stop it now */
			telem = myrpt->tele.next;
			while (telem != &myrpt->tele) {
				if (((telem->mode == ID) || (telem->mode == ID1) || (telem->mode == IDTALKOVER)) && (!telem->killed)) {
					if (telem->chan) {
						ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);	/* Whoosh! */
					}
					telem->killed = 1;
					myrpt->deferid = 1;
				}
				telem = telem->next;
			}
			gettimeofday(&myrpt->paging, NULL);
			rpt_mutex_lock(&myrpt->blocklock);
			ast_sendtext(myrpt->rxchannel, str);
			rpt_mutex_unlock(&myrpt->blocklock);
			break;
		}
	}
	return RESULT_SUCCESS;
}

//## Send to all nodes

int rpt_do_sendall(int fd, int argc, const char *const *argv)
{
	int i;
	struct rpt_link *l;
	char str[MAX_TEXTMSG_SIZE];
	char *nodename;
	int nrpts = rpt_num_rpts();

	if (argc < 4) {
		return RESULT_SHOWUSAGE;
	}

	nodename = ast_strdupa(argv[2]);

	string_toupper(nodename);
	snprintf(str, sizeof(str) - 1, "M %s 0 ", nodename);
	for (i = 3; i < argc; i++) {
		if (i > 3)
			strncat(str, " ", sizeof(str) - 1);
		strncat(str, argv[i], sizeof(str) - 1);
	}
	for (i = 0; i < nrpts; i++) {
		if (!strcmp(nodename, rpt_vars[i].name)) {
			struct rpt *myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
			/* otherwise, send it to all of em */
			while (l != &myrpt->links) {
				if (l->name[0] == '0') {
					l = l->next;
					continue;
				}
				if (l->chan)
					ast_sendtext(l->chan, str);
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
	}
	return RESULT_SUCCESS;
}

/*
	allows us to test rpt() application data commands
*/
static int rpt_do_fun1(int fd, int argc, const char *const *argv)
{
	int i;
	int nrpts = rpt_num_rpts();

	if (argc != 4) {
		return RESULT_SHOWUSAGE;
	}

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(argv[2], rpt_vars[i].name)) {
			struct rpt *myrpt = &rpt_vars[i];
			rpt_push_alt_macro(myrpt, (char *) argv[3]);
		}
	}
	return RESULT_FAILURE;
}

/*
* send an app_rpt **command** from the CLI
*/

static int rpt_do_cmd(int fd, int argc, const char *const *argv)
{
	int i;
	int busy = 0;
	int thisRpt = -1;
	int thisAction;
	struct rpt *myrpt = NULL;
	int nrpts = rpt_num_rpts();

	if (argc != 6) {
		return RESULT_SHOWUSAGE;
	}

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(argv[2], rpt_vars[i].name)) {
			thisRpt = i;
			myrpt = &rpt_vars[i];
			break;
		}						/* if !strcmp... */
	}							/* for i */

	if (thisRpt < 0) {
		ast_cli(fd, "Unknown node number %s.\n", argv[2]);
		return RESULT_FAILURE;
	}							/* if thisRpt < 0 */

	/* Look up the action */
	thisAction = function_table_index(argv[3]);
	if (thisAction < 0) {
		ast_cli(fd, "Unknown action name %s.\n", argv[3]);
		return RESULT_FAILURE;
	}							/* if thisAction < 0 */

	/* at this point, it looks like all the arguments make sense... */

	rpt_mutex_lock(&myrpt->lock);

	if (rpt_vars[thisRpt].cmdAction.state == CMD_STATE_IDLE) {
		rpt_vars[thisRpt].cmdAction.state = CMD_STATE_BUSY;
		rpt_vars[thisRpt].cmdAction.functionNumber = thisAction;
		snprintf(rpt_vars[thisRpt].cmdAction.param, MAXDTMF, "%s,%s", argv[4], argv[5]);
		ast_copy_string(rpt_vars[thisRpt].cmdAction.digits, argv[5], MAXDTMF);
		rpt_vars[thisRpt].cmdAction.command_source = SOURCE_RPT;
		rpt_vars[thisRpt].cmdAction.state = CMD_STATE_READY;
	}							/* if (rpt_vars[thisRpt].cmdAction.state == CMD_STATE_IDLE */
	else {
		busy = 1;
	}							/* if (rpt_vars[thisRpt].cmdAction.state == CMD_STATE_IDLE */
	rpt_mutex_unlock(&myrpt->lock);

	return (busy ? RESULT_FAILURE : RESULT_SUCCESS);
}								/* rpt_do_cmd() */

/*! \brief Set a node's main channel variable from the command line */
static int rpt_do_setvar(int fd, int argc, const char *const *argv)
{
	char *value;
	int i, x, thisRpt = -1;
	int nrpts = rpt_num_rpts();

	if (argc < 5) {
		return RESULT_SHOWUSAGE;
	}

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(argv[3], rpt_vars[i].name)) {
			thisRpt = i;
			break;
		}
	}

	if (thisRpt < 0) {
		ast_cli(fd, "Unknown node number %s.\n", argv[2]);
		return RESULT_FAILURE;
	}

	for (x = 4; x < argc; x++) {
		const char *name = argv[x];
		if ((value = strchr(name, '='))) {
			*value++ = '\0';
			pbx_builtin_setvar_helper(rpt_vars[thisRpt].rxchannel, name, value);
		} else
			ast_log(LOG_WARNING, "Ignoring entry '%s' with no = \n", name);
	}
	return 0;
}

static int rpt_show_channels(int fd, int argc, const char *const *argv)
{
	int i, this_rpt = -1;
	int nrpts = rpt_num_rpts();

	if (argc != 4) {
		return RESULT_SHOWUSAGE;
	}

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(argv[3], rpt_vars[i].name)) {
			this_rpt = i;
			break;
		}
	}

	if (this_rpt < 0) {
		ast_cli(fd, "Unknown node number %s.\n", argv[3]);
		return RESULT_FAILURE;
	}

#define DUMP_CHANNEL(name) ast_cli(fd, "%-25s: %s\n", #name, rpt_vars[this_rpt].name ? ast_channel_name(rpt_vars[this_rpt].name) : "")
	rpt_mutex_lock(&rpt_vars[this_rpt].lock);
	ast_cli(fd, "RPT channels for node %s\n", argv[3]);
	DUMP_CHANNEL(rxchannel);
	DUMP_CHANNEL(txchannel);
	DUMP_CHANNEL(monchannel);
	DUMP_CHANNEL(parrotchannel);
	DUMP_CHANNEL(pchannel);
	DUMP_CHANNEL(txpchannel);
	DUMP_CHANNEL(dahdirxchannel);
	DUMP_CHANNEL(dahditxchannel);
	DUMP_CHANNEL(telechannel);
	DUMP_CHANNEL(btelechannel);
	DUMP_CHANNEL(voxchannel);
	rpt_mutex_unlock(&rpt_vars[this_rpt].lock);
#undef DUMP_CHANNEL

	return 0;
}

/*! \brief Display a node's main channel variables from the command line */
static int rpt_do_showvars(int fd, int argc, const char *const *argv)
{
	int i, thisRpt = -1;
	struct ast_var_t *newvariable;
	int nrpts = rpt_num_rpts();

	if (argc != 4) {
		return RESULT_SHOWUSAGE;
	}

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(argv[3], rpt_vars[i].name)) {
			thisRpt = i;
			break;
		}
	}

	if (thisRpt < 0) {
		ast_cli(fd, "Unknown node number %s.\n", argv[3]);
		return RESULT_FAILURE;
	}
	i = 0;
	ast_cli(fd, "Variable listing for node %s:\n", argv[3]);
	ast_channel_lock(rpt_vars[thisRpt].rxchannel);
	AST_LIST_TRAVERSE(ast_channel_varshead(rpt_vars[thisRpt].rxchannel), newvariable, entries) {
		i++;
		ast_cli(fd, "   %s=%s\n", ast_var_name(newvariable), ast_var_value(newvariable));
	}
	ast_channel_unlock(rpt_vars[thisRpt].rxchannel);
	ast_cli(fd, "    -- %d variables\n", i);
	return 0;
}

static int rpt_do_lookup(int fd, int argc, const char *const *argv)
{
	struct rpt *myrpt;
	char tmp[300] = "";
	int i;
	int nrpts = rpt_num_rpts();

	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}

	for (i = 0; i < nrpts; i++) {
		myrpt = &rpt_vars[i];
		node_lookup(myrpt, (char *) argv[2], tmp, sizeof(tmp) - 1, 1);
		if (strlen(tmp)) {
			ast_cli(fd, "Node: %-10.10s Data: %-70.70s\n", myrpt->name, tmp);
		}
	}
	return RESULT_SUCCESS;
}

/*! \brief Hooks for CLI functions */
static char *res2cli(int r)
{
	switch (r) {
	case RESULT_SUCCESS:
		return (CLI_SUCCESS);
	case RESULT_SHOWUSAGE:
		return (CLI_SHOWUSAGE);
	default:
		return (CLI_SUCCESS);
	}
}

static char *handle_cli_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt debug level";
		e->usage =
			"Usage: rpt debug level {0-7}\n"
			"	Enables debug messages in app_rpt\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_debug(a->fd, a->argc, a->argv));
}

static char *handle_cli_dump(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt dump";
		e->usage =
			"Usage: rpt dump <nodename>\n"
			"	Dumps struct debug info to log\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_dump(a->fd, a->argc, a->argv));
}

static char *handle_cli_stats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt stats";
		e->usage =
			"Usage: rpt stats <nodename>\n"
			"	Dumps node statistics to console\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_stats(a->fd, a->argc, a->argv));
}

static char *handle_cli_nodes(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt nodes";
		e->usage =
			"Usage: rpt nodes <nodename>\n"
			"	Dumps a list of directly and indirectly connected nodes to the console\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_nodes(a->fd, a->argc, a->argv));
}

static char *handle_cli_xnode(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt xnode";
		e->usage =
			"Usage: rpt xnode <nodename>\n"
			"	Dumps extended node info to the console\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_xnode(a->fd, a->argc, a->argv));
}

static char *handle_cli_local_nodes(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt localnodes";
		e->usage =
			"Usage: rpt localnodes\n"
			"	Dumps a list of the locally configured node numbers to the console.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_local_nodes(a->fd, a->argc, a->argv));
}

static char *handle_cli_lstats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt lstats";
		e->usage =
			"Usage: rpt lstats <nodename>\n"
			"	Dumps link statistics to console\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_lstats(a->fd, a->argc, a->argv));
}

static char *handle_cli_restart(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt restart";
		e->usage =
			"Usage: rpt restart\n"
			"	Restarts app_rpt\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_restart(a->fd, a->argc, a->argv));
}

static char *handle_cli_fun(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt fun";
		e->usage =
			"Usage: rpt fun <nodename> <command>\n"
			"	Send a DTMF function to a node\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_fun(a->fd, a->argc, a->argv));
}

static char *handle_cli_fun1(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt fun1";
		e->usage =
			"Usage: rpt fun1 <nodename> <command>\n"
			"	Send a DTMF function to a node\n";;
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_fun1(a->fd, a->argc, a->argv));
}

static char *handle_cli_playback(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt playback";
		e->usage =
			"Usage: rpt playback <nodename> <sound_file_base_name>\n"
			"	Send an Audio File to a node, send to all other connected nodes (global)\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_playback(a->fd, a->argc, a->argv));
}

static char *handle_cli_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt cmd";
		e->usage =
			"Usage: rpt cmd <nodename> <cmd-name> <cmd-index> <cmd-args>\n"
			"	Send a command to a node.\n"
			"	i.e. rpt cmd 2000 ilink 3 2001\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_cmd(a->fd, a->argc, a->argv));
}

static char *handle_cli_setvar(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt set variable";
		e->usage =
			"Usage: rpt set variable <nodename> <name=value> [<name=value>...]\n"
			"	Set an Asterisk channel variable for a node.\n"
			"   Note: variable names are case-sensitive.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_setvar(a->fd, a->argc, a->argv));
}

static char *handle_cli_showvars(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt show variables";
		e->usage = "Usage: rpt show variables <nodename>\n"
			"	Display all the Asterisk channel variables for a node.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_showvars(a->fd, a->argc, a->argv));
}

static char *handle_cli_show_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt show channels";
		e->usage = "Usage: rpt show channels <nodename>\n"
			"	Display all the Asterisk channels for a node.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_show_channels(a->fd, a->argc, a->argv));
}

static char *handle_cli_lookup(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt lookup";
		//e->usage = rpt_usage;
		e->usage = NULL;		/*! \todo 20220111 NA rpt_usage doesn't exist! */
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_lookup(a->fd, a->argc, a->argv));
}

static char *handle_cli_localplay(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt localplay";
		e->usage =
			"Usage: rpt localplay <nodename> <sound_file_base_name>\n"
			"	Send an audio file to a node, do not send to other connected nodes (local)\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_localplay(a->fd, a->argc, a->argv));
}

static char *handle_cli_sendall(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt sendall";
		e->usage =
			"Usage: rpt sendall <nodename> <Text Message>\n"
			"	Send a Text message to all connected nodes\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_sendall(a->fd, a->argc, a->argv));
}

static char *handle_cli_sendtext(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt sendtext";
		e->usage =
			"Usage: rpt sendtext <nodename> <destnodename> <Text Message>\n"
			"	Send a Text message to a specified node\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_sendtext(a->fd, a->argc, a->argv));
}

static char *handle_cli_page(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt page";
		e->usage =
			"Usage: rpt page <nodename> <baud> <capcode> <[ANT]Text....>\n"
			"	Send a page to a user on a node, specifying capcode and type/text\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	return res2cli(rpt_do_page(a->fd, a->argc, a->argv));
}

static char *handle_cli_show_version(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt show version";
		e->usage =
			"Usage: rpt show version\n"
			"	Show the current version of the app_rpt module\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	ast_cli(a->fd, "app_rpt version: %d.%d.%d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
	return CLI_SUCCESS;
}

static struct ast_cli_entry rpt_cli[] = {
	AST_CLI_DEFINE(handle_cli_debug, "Enable app_rpt debugging"),
	AST_CLI_DEFINE(handle_cli_dump, "Dump app_rpt structs for debugging"),
	AST_CLI_DEFINE(handle_cli_stats, "Dump node statistics"),
	AST_CLI_DEFINE(handle_cli_nodes, "Dump node list"),
	AST_CLI_DEFINE(handle_cli_xnode, "Dump extended node info"),
	AST_CLI_DEFINE(handle_cli_local_nodes, "Dump list of local node numbers"),
	AST_CLI_DEFINE(handle_cli_lstats, "Dump link statistics"),
	AST_CLI_DEFINE(handle_cli_restart, "Restart app_rpt"),
	AST_CLI_DEFINE(handle_cli_playback, "Play Back an Audio File"),
	AST_CLI_DEFINE(handle_cli_fun, "Execute a DTMF function"),
	AST_CLI_DEFINE(handle_cli_fun1, "Execute a DTMF function"),
	AST_CLI_DEFINE(handle_cli_cmd, "Execute a DTMF function"),
	AST_CLI_DEFINE(handle_cli_setvar, "Set an Asterisk channel variable for a node"),
	AST_CLI_DEFINE(handle_cli_showvars, "Display Asterisk channel variables for a node"),
	AST_CLI_DEFINE(handle_cli_show_channels, "Display Asterisk channels for a node"),
	AST_CLI_DEFINE(handle_cli_localplay, "Playback an audio file (local)"),
	AST_CLI_DEFINE(handle_cli_sendall, "Send a Text message to all connected nodes"),
	AST_CLI_DEFINE(handle_cli_sendtext, "Send a Text message to a specified nodes"),
	AST_CLI_DEFINE(handle_cli_page, "Send a page to a user on a node"),
	AST_CLI_DEFINE(handle_cli_lookup, "Lookup Allstar nodes"),
	AST_CLI_DEFINE(handle_cli_show_version, "Show app_rpt version")
};

int rpt_cli_load(void)
{
	return ast_cli_register_multiple(rpt_cli, ARRAY_LEN(rpt_cli));
}

int rpt_cli_unload(void)
{
	return ast_cli_unregister_multiple(rpt_cli, ARRAY_LEN(rpt_cli));
}
