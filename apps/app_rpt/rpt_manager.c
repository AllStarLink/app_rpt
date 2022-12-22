
#include "asterisk.h"

#include <search.h>

#include "asterisk/channel.h"
#include "asterisk/manager.h"
#include "asterisk/pbx.h"
#include "asterisk/cli.h" /* use RESULT_SUCCESS */

#include "app_rpt.h"
#include "rpt_lock.h"
#include "rpt_channel.h"
#include "rpt_config.h"
#include "rpt_manager.h"
#include "rpt_utils.h"
#include "rpt_link.h" /* use __mklinklist */

extern struct rpt rpt_vars[MAXRPTS];

void rpt_manager_trigger(struct rpt *myrpt, char *event, char *value)
{
	manager_event(EVENT_FLAG_CALL, event,
		"Node: %s\r\n"
		"Channel: %s\r\n"
		"EventValue: %s\r\n"
		"LastKeyedTime: %s\r\n"
		"LastTxKeyedTime: %s\r\n",
		myrpt->name, ast_channel_name(myrpt->rxchannel), value, ctime(&myrpt->lastkeyedtime),
		ctime(&myrpt->lasttxkeyedtime)
	);
}

/*!\brief callback to display list of locally configured nodes
   \addtogroup Group_AMI
 */
static int manager_rpt_local_nodes(struct mansession *s, const struct message *m)
{
	int i;
	int nrpts = rpt_num_rpts();

	astman_append(s, "<?xml version=\"1.0\"?>\r\n");
	astman_append(s, "<nodes>\r\n");
	for (i = 0; i < nrpts; i++) {
		if (rpt_vars[i].name[0])
			astman_append(s, "  <node>%s</node>\r\n", rpt_vars[i].name);
	}							/* for i */
	astman_append(s, "</nodes>\r\n");
	astman_append(s, "\r\n");	/* Properly terminate Manager output */
	return RESULT_SUCCESS;
}								/* manager_rpt_local_nodes() */

/*
 * Append Success and ActionID to manager response message
 */

static void rpt_manager_success(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	if (!ast_strlen_zero(id))
		astman_append(s, "ActionID: %s\r\n", id);
	astman_append(s, "Response: Success\r\n");
}

static int rpt_manager_do_sawstat(struct mansession *ses, const struct message *m, char *str)
{
	int i;
	int nrpts = rpt_num_rpts();
	struct rpt_link *l;
	const char *node = astman_get_header(m, "Node");
	time_t now;

	time(&now);
	for (i = 0; i < nrpts; i++) {
		if ((node) && (!strcmp(node, rpt_vars[i].name))) {
			rpt_manager_success(ses, m);
			astman_append(ses, "Node: %s\r\n", node);

			rpt_mutex_lock(&rpt_vars[i].lock);	/* LOCK */

			l = rpt_vars[i].links.next;
			while (l && (l != &rpt_vars[i].links)) {
				if (l->name[0] == '0') {	// Skip '0' nodes 
					l = l->next;
					continue;
				}
				astman_append(ses, "Conn: %s %d %d %d\r\n", l->name, l->lastrx1,
							  (l->lastkeytime) ? (int) (now - l->lastkeytime) : -1,
							  (l->lastunkeytime) ? (int) (now - l->lastunkeytime) : -1);
				l = l->next;
			}
			rpt_mutex_unlock(&rpt_vars[i].lock);	// UNLOCK 
			astman_append(ses, "\r\n");
			return (0);
		}
	}
	astman_send_error(ses, m, "RptStatus unknown or missing node");
	return 0;
}

static int rpt_manager_do_xstat(struct mansession *ses, const struct message *m, char *str)
{
	int i, j;
	char ns;
	char lbuf[MAXLINKLIST], *strs[MAXLINKLIST];
	struct rpt *myrpt;
	struct ast_var_t *newvariable;
	char *connstate;
	struct rpt_link *l;
	struct rpt_lstat *s, *t;
	struct rpt_lstat s_head;
	const char *node = astman_get_header(m, "Node");
	int nrpts = rpt_num_rpts();

	char *parrot_ena, *sys_ena, *tot_ena, *link_ena, *patch_ena, *patch_state;
	char *sch_ena, *user_funs, *tail_type, *iconns, *tot_state, *ider_state, *tel_mode;

	s = NULL;
	s_head.next = &s_head;
	s_head.prev = &s_head;

	for (i = 0; i < nrpts; i++) {
		if (node && !strcmp(node, rpt_vars[i].name)) {
			struct ast_channel *rxchan;
			char rxchanname[128];
			rpt_manager_success(ses, m);
			astman_append(ses, "Node: %s\r\n", node);

			/* Make a copy of all stat variables while locked */
			myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock);	/* LOCK */

			ast_copy_string(rxchanname, myrpt->rxchanname, sizeof(rxchanname));

			/* Get RPT status states while locked */
			parrot_ena = myrpt->p.parrotmode ? "1" : "0";
			sys_ena = myrpt->p.s[myrpt->p.sysstate_cur].txdisable ? "1" : "0";
			tot_ena = myrpt->p.s[myrpt->p.sysstate_cur].totdisable ? "1" : "0";
			link_ena = myrpt->p.s[myrpt->p.sysstate_cur].linkfundisable ? "1" : "0";
			patch_ena = myrpt->p.s[myrpt->p.sysstate_cur].autopatchdisable ? "1" : "0";
			sch_ena = myrpt->p.s[myrpt->p.sysstate_cur].schedulerdisable ? "1" : "0";
			user_funs = myrpt->p.s[myrpt->p.sysstate_cur].userfundisable ? "1" : "0";
			tail_type = myrpt->p.s[myrpt->p.sysstate_cur].alternatetail ? "1" : "0";
			iconns = myrpt->p.s[myrpt->p.sysstate_cur].noincomingconns ? "1" : "0";

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

			/* Get connected node info */
			/* Traverse the list of connected nodes */

			__mklinklist(myrpt, NULL, lbuf, 0);

			j = 0;
			l = myrpt->links.next;
			while (l && (l != &myrpt->links)) {
				if (l->name[0] == '0') {	/* Skip '0' nodes */
					l = l->next;
					continue;
				}
				if (!(s = (struct rpt_lstat *) ast_malloc(sizeof(struct rpt_lstat)))) {
					ast_log(LOG_ERROR, "Malloc failed in rpt_do_lstats\r\n");
					rpt_mutex_unlock(&myrpt->lock);
					return -1;
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
			rpt_mutex_unlock(&myrpt->lock);
			for (s = s_head.next; s != &s_head; s = s->next) {
				int hours, minutes, seconds;
				long long connecttime = s->connecttime;
				char conntime[21];
				hours = connecttime / 3600000L;
				connecttime %= 3600000L;
				minutes = connecttime / 60000L;
				connecttime %= 60000L;
				seconds = (int) connecttime / 1000L;
				connecttime %= 1000L;
				snprintf(conntime, 20, "%02d:%02d:%02d", hours, minutes, seconds);
				conntime[20] = 0;
				if (s->thisconnected)
					connstate = "ESTABLISHED";
				else
					connstate = "CONNECTING";
				astman_append(ses, "Conn: %-10s%-20s%-12d%-11s%-20s%-20s\r\n",
							  s->name, s->peer, s->reconnects, (s->outbound) ? "OUT" : "IN", conntime, connstate);
			}
			/* destroy our local link queue */
			s = s_head.next;
			while (s != &s_head) {
				t = s;
				s = s->next;
				remque((struct qelem *) t);
				ast_free(t);
			}

			astman_append(ses, "LinkedNodes: ");

			/* Get all linked nodes info */

			/* parse em */
			ns = finddelim(lbuf, strs, MAXLINKLIST);
			/* sort em */
			if (ns) {
				qsort((void *) strs, ns, sizeof(char *), mycompar);
			}
			for (j = 0;; j++) {
				if (!strs[j]) {
					if (!j) {
						astman_append(ses, "<NONE>");
					}
					break;
				}
				astman_append(ses, "%s", strs[j]);
				if (strs[j + 1]) {
					astman_append(ses, ", ");
				}
			}
			astman_append(ses, "\r\n");

			/* Get variables info */
			j = 0;

			/* rxchan might've disappeared in the meantime. Verify it still exists before we try to lock it.
			 * XXX This was added to address assertions due to bad locking, but app_rpt should probably
			 * be globally ref'ing the channel and holding it until it unloads. Should be investigated. */
			rxchan = ast_channel_get_by_name(rxchanname);
			if (rxchan) {
				ast_assert(rxchan == rpt_vars[i].rxchannel);
				ast_channel_lock(rpt_vars[i].rxchannel);
				AST_LIST_TRAVERSE(ast_channel_varshead(rpt_vars[i].rxchannel), newvariable, entries) {
					j++;
					astman_append(ses, "Var: %s=%s\r\n", ast_var_name(newvariable), ast_var_value(newvariable));
				}
				ast_channel_unlock(rpt_vars[i].rxchannel);
				ast_channel_unref(rxchan);
			} else {
				ast_debug(1, "Channel %s has disappeared, cannot access variables\n", rxchanname);
			}

			/* Output RPT status states */
			astman_append(ses, "parrot_ena: %s\r\n", parrot_ena);
			astman_append(ses, "sys_ena: %s\r\n", sys_ena);
			astman_append(ses, "tot_ena: %s\r\n", tot_ena);
			astman_append(ses, "link_ena: %s\r\n", link_ena);
			astman_append(ses, "patch_ena: %s\r\n", patch_ena);
			astman_append(ses, "patch_state: %s\r\n", patch_state);
			astman_append(ses, "sch_ena: %s\r\n", sch_ena);
			astman_append(ses, "user_funs: %s\r\n", user_funs);
			astman_append(ses, "tail_type: %s\r\n", tail_type);
			astman_append(ses, "iconns: %s\r\n", iconns);
			astman_append(ses, "tot_state: %s\r\n", tot_state);
			astman_append(ses, "ider_state: %s\r\n", ider_state);
			astman_append(ses, "tel_mode: %s\r\n\r\n", tel_mode);

			return 0;
		}
	}
	astman_send_error(ses, m, "RptStatus unknown or missing node");
	return 0;
}

/*! \brief Dump statistics to manager session */
static int rpt_manager_do_stats(struct mansession *s, const struct message *m, char *str)
{
	int i, j, numoflinks;
	int dailytxtime, dailykerchunks;
	time_t now;
	int totalkerchunks, dailykeyups, totalkeyups, timeouts;
	int totalexecdcommands, dailyexecdcommands, hours, minutes, seconds;
	long long totaltxtime;
	struct rpt_link *l;
	char *listoflinks[MAX_STAT_LINKS];
	char *lastdtmfcommand, *parrot_ena;
	char *tot_state, *ider_state, *patch_state;
	char *reverse_patch_state, *sys_ena, *tot_ena, *link_ena, *patch_ena;
	char *sch_ena, *input_signal, *called_number, *user_funs, *tail_type;
	char *transmitterkeyed;
	const char *node = astman_get_header(m, "Node");
	struct rpt *myrpt;
	int nrpts = rpt_num_rpts();

	static char *not_applicable = "N/A";

	tot_state = ider_state = patch_state = reverse_patch_state = input_signal = not_applicable;
	called_number = lastdtmfcommand = transmitterkeyed = NULL;

	time(&now);
	for (i = 0; i < nrpts; i++) {
		if ((node) && (!strcmp(node, rpt_vars[i].name))) {
			rpt_manager_success(s, m);

			myrpt = &rpt_vars[i];

			if (myrpt->remote) {	/* Remote base ? */
				char *loginuser, *loginlevel, *freq, *rxpl, *txpl, *modestr;
				char offset = 0, powerlevel = 0, rxplon = 0, txplon = 0, remoteon, remmode = 0, reportfmstuff;
				char offsetc, powerlevelc;

				loginuser = loginlevel = freq = rxpl = txpl = NULL;
				/* Make a copy of all stat variables while locked */
				rpt_mutex_lock(&myrpt->lock);	/* LOCK */
				if ((remoteon = myrpt->remoteon)) {
					if (!ast_strlen_zero(myrpt->loginuser))
						loginuser = ast_strdup(myrpt->loginuser);
					if (!ast_strlen_zero(myrpt->loginlevel))
						loginlevel = ast_strdup(myrpt->loginlevel);
					if (!ast_strlen_zero(myrpt->freq))
						freq = ast_strdup(myrpt->freq);
					if (!ast_strlen_zero(myrpt->rxpl))
						rxpl = ast_strdup(myrpt->rxpl);
					if (!ast_strlen_zero(myrpt->txpl))
						txpl = ast_strdup(myrpt->txpl);
					remmode = myrpt->remmode;
					offset = myrpt->offset;
					powerlevel = myrpt->powerlevel;
					rxplon = myrpt->rxplon;
					txplon = myrpt->txplon;
				}
				rpt_mutex_unlock(&myrpt->lock);	/* UNLOCK */
				astman_append(s, "IsRemoteBase: YES\r\n");
				astman_append(s, "RemoteOn: %s\r\n", (remoteon) ? "YES" : "NO");
				if (remoteon) {
					if (loginuser) {
						astman_append(s, "LogInUser: %s\r\n", loginuser);
						ast_free(loginuser);
					}
					if (loginlevel) {
						astman_append(s, "LogInLevel: %s\r\n", loginlevel);
						ast_free(loginlevel);
					}
					if (freq) {
						astman_append(s, "Freq: %s\r\n", freq);
						ast_free(freq);
					}
					reportfmstuff = 0;
					switch (remmode) {
					case REM_MODE_FM:
						modestr = "FM";
						reportfmstuff = 1;
						break;
					case REM_MODE_AM:
						modestr = "AM";
						break;
					case REM_MODE_USB:
						modestr = "USB";
						break;
					default:
						modestr = "LSB";
						break;
					}
					astman_append(s, "RemMode: %s\r\n", modestr);
					if (reportfmstuff) {
						switch (offset) {
						case REM_SIMPLEX:
							offsetc = 'S';
							break;
						case REM_MINUS:
							offsetc = '-';
							break;
						default:
							offsetc = '+';
							break;
						}
						astman_append(s, "RemOffset: %c\r\n", offsetc);
						if (rxplon && rxpl) {
							astman_append(s, "RxPl: %s\r\n", rxpl);
							ast_free(rxpl);
						}
						if (txplon && txpl) {
							astman_append(s, "TxPl: %s\r\n", txpl);
							ast_free(txpl);
						}
					}
					switch (powerlevel) {
					case REM_LOWPWR:
						powerlevelc = 'L';
						break;
					case REM_MEDPWR:
						powerlevelc = 'M';
						break;
					default:
						powerlevelc = 'H';
						break;
					}
					astman_append(s, "PowerLevel: %c\r\n", powerlevelc);
				}
				astman_append(s, "\r\n");
				return 0;		/* End of remote base status reporting */
			}

			/* ELSE Process as a repeater node */
			/* Make a copy of all stat variables while locked */
			rpt_mutex_lock(&myrpt->lock);	/* LOCK */
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

			if (myrpt->txkeyed)
				transmitterkeyed = "YES";
			else
				transmitterkeyed = "NO";

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

			astman_append(s, "IsRemoteBase: NO\r\n");
			astman_append(s, "NodeState: %d\r\n", myrpt->p.sysstate_cur);
			astman_append(s, "SignalOnInput: %s\r\n", input_signal);
			astman_append(s, "TransmitterKeyed: %s\r\n", transmitterkeyed);
			astman_append(s, "Transmitter: %s\r\n", sys_ena);
			astman_append(s, "Parrot: %s\r\n", parrot_ena);
			astman_append(s, "Scheduler: %s\r\n", sch_ena);
			astman_append(s, "TailLength: %s\r\n", tail_type);
			astman_append(s, "TimeOutTimer: %s\r\n", tot_ena);
			astman_append(s, "TimeOutTimerState: %s\r\n", tot_state);
			astman_append(s, "TimeOutsSinceSystemInitialization: %d\r\n", timeouts);
			astman_append(s, "IdentifierState: %s\r\n", ider_state);
			astman_append(s, "KerchunksToday: %d\r\n", dailykerchunks);
			astman_append(s, "KerchunksSinceSystemInitialization: %d\r\n", totalkerchunks);
			astman_append(s, "KeyupsToday: %d\r\n", dailykeyups);
			astman_append(s, "KeyupsSinceSystemInitialization: %d\r\n", totalkeyups);
			astman_append(s, "DtmfCommandsToday: %d\r\n", dailyexecdcommands);
			astman_append(s, "DtmfCommandsSinceSystemInitialization: %d\r\n", totalexecdcommands);
			astman_append(s, "LastDtmfCommandExecuted: %s\r\n",
						  (lastdtmfcommand && strlen(lastdtmfcommand)) ? lastdtmfcommand : not_applicable);
			hours = dailytxtime / 3600000;
			dailytxtime %= 3600000;
			minutes = dailytxtime / 60000;
			dailytxtime %= 60000;
			seconds = dailytxtime / 1000;
			dailytxtime %= 1000;

			astman_append(s, "TxTimeToday: %02d:%02d:%02d:%02d\r\n", hours, minutes, seconds, dailytxtime);

			hours = (int) totaltxtime / 3600000;
			totaltxtime %= 3600000;
			minutes = (int) totaltxtime / 60000;
			totaltxtime %= 60000;
			seconds = (int) totaltxtime / 1000;
			totaltxtime %= 1000;

			astman_append(s, "TxTimeSinceSystemInitialization: %02d:%02d:%02d:%02d\r\n", hours, minutes, seconds,
						  (int) totaltxtime);

			sprintf(str, "NodesCurrentlyConnectedToUs: ");
			if (!numoflinks) {
				strcat(str, "<NONE>");
			} else {
				for (j = 0; j < numoflinks; j++) {
					sprintf(str + strlen(str), "%s", listoflinks[j]);
					if (j < numoflinks - 1)
						strcat(str, ",");
				}
			}
			astman_append(s, "%s\r\n", str);

			astman_append(s, "Autopatch: %s\r\n", patch_ena);
			astman_append(s, "AutopatchState: %s\r\n", patch_state);
			astman_append(s, "AutopatchCalledNumber: %s\r\n",
						  (called_number && strlen(called_number)) ? called_number : not_applicable);
			astman_append(s, "ReversePatchIaxrptConnected: %s\r\n", reverse_patch_state);
			astman_append(s, "UserLinkingCommands: %s\r\n", link_ena);
			astman_append(s, "UserFunctions: %s\r\n", user_funs);

			for (j = 0; j < numoflinks; j++) {	/* ast_free() all link names */
				ast_free(listoflinks[j]);
			}
			if (called_number) {
				ast_free(called_number);
			}
			if (lastdtmfcommand) {
				ast_free(lastdtmfcommand);
			}
			astman_append(s, "\r\n");	/* We're Done! */
			return 0;
		}
	}
	astman_send_error(s, m, "RptStatus unknown or missing node");
	return 0;
}

/*
 * Implement the RptStatus Manager Interface
 */

static int manager_rpt_status(struct mansession *s, const struct message *m)
{
	int i, res, len, index;
	int uptime, hours, minutes;
	time_t now;
	const char *cmd = astman_get_header(m, "Command");
	char *str;
	enum { MGRCMD_RPTSTAT, MGRCMD_NODESTAT, MGRCMD_XSTAT, MGRCMD_SAWSTAT };
	struct mgrcmdtbl {
		const char *cmd;
		int index;
	};
	static struct mgrcmdtbl mct[] = {
		{ "RptStat", MGRCMD_RPTSTAT },
		{ "NodeStat", MGRCMD_NODESTAT },
		{ "XStat", MGRCMD_XSTAT },
		{ "SawStat", MGRCMD_SAWSTAT },
		{ NULL, 0 }				/* NULL marks end of command table */
	};
	int nrpts = rpt_num_rpts();

	time(&now);

	len = 1024;					/* Allocate a working buffer */
	if (!(str = ast_malloc(len)))
		return -1;

	/* Check for Command */
	if (ast_strlen_zero(cmd)) {
		astman_send_error(s, m, "RptStatus missing command");
		ast_free(str);
		return 0;
	}
	/* Try to find the command in the table */
	for (i = 0; mct[i].cmd; i++) {
		if (!strcmp(mct[i].cmd, cmd))
			break;
	}

	if (!mct[i].cmd) {			/* Found or not found ? */
		astman_send_error(s, m, "RptStatus unknown command");
		ast_free(str);
		return 0;
	} else
		index = mct[i].index;

	switch (index) {			/* Use the index to go to the correct command */

	case MGRCMD_RPTSTAT:
		/* Return Nodes: and a comma separated list of nodes */
		if ((res = snprintf(str, len, "Nodes: ")) > -1)
			len -= res;
		else {
			ast_free(str);
			return 0;
		}
		for (i = 0; i < nrpts; i++) {
			if (i < nrpts - 1) {
				if ((res = snprintf(str + strlen(str), len, "%s,", rpt_vars[i].name)) < 0) {
					ast_free(str);
					return 0;
				}
			} else {
				if ((res = snprintf(str + strlen(str), len, "%s", rpt_vars[i].name)) < 0) {
					ast_free(str);
					return 0;
				}
			}
			len -= res;
		}

		rpt_manager_success(s, m);

		if (!nrpts)
			astman_append(s, "<NONE>\r\n");
		else
			astman_append(s, "%s\r\n", str);

		uptime = (int) (now - rpt_starttime());
		hours = uptime / 3600;
		uptime %= 3600;
		minutes = uptime / 60;
		uptime %= 60;

		astman_append(s, "RptUptime: %02d:%02d:%02d\r\n", hours, minutes, uptime);

		astman_append(s, "\r\n");
		break;

	case MGRCMD_NODESTAT:
		res = rpt_manager_do_stats(s, m, str);
		ast_free(str);
		return res;

	case MGRCMD_XSTAT:
		res = rpt_manager_do_xstat(s, m, str);
		ast_free(str);
		return res;

	case MGRCMD_SAWSTAT:
		res = rpt_manager_do_sawstat(s, m, str);
		ast_free(str);
		return res;

	default:
		astman_send_error(s, m, "RptStatus invalid command");
		break;
	}
	ast_free(str);
	return 0;
}

int rpt_manager_load(void)
{
	int res = 0;

	res |= ast_manager_register("RptLocalNodes", 0, manager_rpt_local_nodes, "List local node numbers");
	res |= ast_manager_register("RptStatus", 0, manager_rpt_status, "Return Rpt Status for CGI");

	return res;
}

int rpt_manager_unload(void)
{
	int res = 0;

	res |= ast_manager_unregister("RptLocalNodes");
	res |= ast_manager_unregister("RptStatus");

	return res;
}
