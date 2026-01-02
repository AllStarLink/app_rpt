
/*! \file
 *
 * \brief RPT functions
 */

#include "asterisk.h"

#include "asterisk/app.h" /* use ast_safe_system */
#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/pbx.h" /* functions */

#include "app_rpt.h"
#include "rpt_lock.h"
#include "rpt_utils.h"
#include "rpt_capabilities.h"
#include "rpt_config.h"
#include "rpt_mdc1200.h"
#include "rpt_channel.h"
#include "rpt_telemetry.h"
#include "rpt_link.h"
#include "rpt_daq.h"
#include "rpt_functions.h"
#include "rpt_rig.h"
#include "rpt_radio.h"

/*!
 * \brief DTMF Tones - frequency pairs used to generate them along with the required timings
 * \note not static because used extern by rpt_channel.c
 */
char *dtmf_tones[] = {
	"!941+1336/200,!0/200",		/* 0 */
	"!697+1209/200,!0/200",		/* 1 */
	"!697+1336/200,!0/200",		/* 2 */
	"!697+1477/200,!0/200",		/* 3 */
	"!770+1209/200,!0/200",		/* 4 */
	"!770+1336/200,!0/200",		/* 5 */
	"!770+1477/200,!0/200",		/* 6 */
	"!852+1209/200,!0/200",		/* 7 */
	"!852+1336/200,!0/200",		/* 8 */
	"!852+1477/200,!0/200",		/* 9 */
	"!697+1633/200,!0/200",		/* A */
	"!770+1633/200,!0/200",		/* B */
	"!852+1633/200,!0/200",		/* C */
	"!941+1633/200,!0/200",		/* D */
	"!941+1209/200,!0/200",		/* * */
	"!941+1477/200,!0/200"		/* # */
};

static char remdtmfstr[] = "0123456789*#ABCD";

int rpt_link_find_by_name(void *obj, void *arg, int flags)
{
	struct rpt_link *link = obj;
	char *str = arg;

	if (!strcmp(link->name, str)) {
		return CMP_MATCH;
	}
	return 0;
}

int rpt_sendtext_cb(void *obj, void *arg, int flags)
{
	struct rpt_link *link = obj;
	char *str = arg;

	if (link->name[0] == '0') {
		return 0;
	}
	if (link->chan) {
		ast_sendtext(link->chan, str);
	}
	return 0;
}

enum rpt_function_response function_ilink(struct rpt *myrpt, char *param, char *digits, enum rpt_command_source command_source,
	struct rpt_link *mylink)
{
	char *s1, *s2, tmp[MAXNODESTR];
	char digitbuf[MAXNODESTR], *strs[ARRAY_LEN(myrpt->savednodes)];
	char perma;
	enum link_mode mode;
	struct rpt_link *l;
	struct ao2_iterator l_it;
	int i, r;
	struct rpt_connect_data *connect_data;
	pthread_t rpt_connect_threadid;

	if (!param)
		return DC_ERROR;

	if (myrpt->p.s[myrpt->p.sysstate_cur].txdisable || myrpt->p.s[myrpt->p.sysstate_cur].linkfundisable)
		return DC_ERROR;

	ast_copy_string(digitbuf, digits, sizeof(digitbuf));

	ast_debug(7, "@@@@ ilink param = %s, digitbuf = %s\n", (param) ? param : "(null)", digitbuf);
	switch (myatoi(param)) {
	case 11:					/* Perm Link off */
	case 1:					/* Link off */
		struct ast_frame wf;
		if (strlen(digitbuf) < 1)
			break;
		if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
			strcpy(digitbuf, myrpt->lastlinknode);
		rpt_mutex_lock(&myrpt->lock);
		/* try to find this one in queue */
		l = ao2_find(myrpt->links, digitbuf, 0);
		if (!l) { /* if not found */
			rpt_mutex_unlock(&myrpt->lock);
			break;
		}
		/* if found */
		/* must use perm command on perm link */
		if ((myatoi(param) < 10) && (l->max_retries > MAX_RETRIES)) {
			rpt_mutex_unlock(&myrpt->lock);
			ao2_ref(l, -1);
			return DC_COMPLETE;
		}
		ast_copy_string(myrpt->lastlinknode, digitbuf, MAXNODESTR - 1);
		l->retries = l->max_retries + 1;
		l->disced = 1;
		l->hasconnected = 1;
		rpt_mutex_unlock(&myrpt->lock);
		init_text_frame(&wf, "function_ilink:1");
		wf.datalen = strlen(DISCSTR) + 1;
		wf.data.ptr = DISCSTR;
		if (l->chan) {
			if (l->thisconnected)
				ast_write(l->chan, &wf);
			rpt_safe_sleep(myrpt, l->chan, 250);
			ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
		}
		myrpt->linkactivityflag = 1;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, COMPLETE, NULL);
		ao2_ref(l, -1);
		return DC_COMPLETE;
	case 2:					/* Link Monitor */
	case 3:					/* Link transceive */
	case 12:					/* Link Monitor permanent */
	case 13:					/* Link transceive permanent */
	case 8:					/* Link Monitor Local Only */
	case 18:					/* Link Monitor Local Only permanent */
		if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
			strcpy(digitbuf, myrpt->lastlinknode);
		r = atoi(param);
		/* Attempt connection  */
		perma = (r > 10) ? 1 : 0;
		mode = (r & 1) ? MODE_TRANSCEIVE : MODE_MONITOR;
		if ((r == 8) || (r == 18)) {
			mode = MODE_LOCAL_MONITOR;
		}

		connect_data = ast_calloc(1, sizeof(struct rpt_connect_data));
		if (!connect_data) {
			return DC_ERROR;
		}
		connect_data->myrpt = myrpt;
		connect_data->digitbuf = ast_strdup(digitbuf);
		if (!connect_data->digitbuf) {
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, CONNFAIL, NULL);
			ast_free(connect_data);
			return DC_ERROR;
		}
		connect_data->mode = mode;
		connect_data->perma = perma;
		connect_data->command_source = command_source;
		connect_data->mylink = mylink;
		rpt_mutex_lock(&myrpt->lock);
		myrpt->connect_thread_count++;
		rpt_mutex_unlock(&myrpt->lock);
		if (ast_pthread_create_detached(&rpt_connect_threadid, NULL, rpt_link_connect, (void *) connect_data) < 0) {
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, CONNFAIL, NULL);
			rpt_mutex_lock(&myrpt->lock);
			myrpt->connect_thread_count--;
			ast_assert(myrpt->connect_thread_count >= 0);
			rpt_mutex_unlock(&myrpt->lock);
			ast_free(connect_data->digitbuf);
			ast_free(connect_data);
			return DC_COMPLETE;
		}
		break;

	case 4:					/* Enter Command Mode */

		if (strlen(digitbuf) < 1)
			break;
		/* if doesn't allow link cmd, or no links active, return */
		if (!ao2_container_count(myrpt->links)) {
			return DC_COMPLETE;
		}
		if ((command_source != SOURCE_RPT) && (command_source != SOURCE_PHONE) && (command_source != SOURCE_ALT) &&
			(command_source != SOURCE_DPHONE) && mylink && (!iswebtransceiver(mylink)) && !CHAN_TECH(mylink->chan, "echolink") &&
			!CHAN_TECH(mylink->chan, "tlb")) {
			return DC_COMPLETE;
		}

		/* if already in cmd mode, or selected self, fughetabahtit */
		if ((myrpt->cmdnode[0]) || (!strcmp(myrpt->name, digitbuf))) {

			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, REMALREADY, NULL);
			return DC_COMPLETE;
		}
		if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
			strcpy(digitbuf, myrpt->lastlinknode);
		/* node must at least exist in list */
		if (!tlb_query_node_exists(digitbuf))  {
			if (digitbuf[0] != '3') {
				if (node_lookup(myrpt, digitbuf, NULL, 0, 1)) {
					if (strlen(digitbuf) >= myrpt->longestnode)
						return DC_ERROR;
					break;

				}
			} else {
				if (strlen(digitbuf) < 7)
					break;
			}
		}
		rpt_mutex_lock(&myrpt->lock);
		strcpy(myrpt->lastlinknode, digitbuf);
		ast_copy_string(myrpt->cmdnode, digitbuf, sizeof(myrpt->cmdnode) - 1);
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, REMGO, NULL);
		return DC_COMPLETE;

	case 5:					/* Status */
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, STATUS, NULL);
		return DC_COMPLETE;

	case 15:					/* Full Status */
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, FULLSTATUS, NULL);
		return DC_COMPLETE;

	case 6:					/* All Links Off, including permalinks */
		rpt_mutex_lock(&myrpt->lock);
		myrpt->savednodes[0] = 0;
		/* loop through all links */
		RPT_LIST_TRAVERSE(myrpt->links, l, l_it) {
			struct ast_frame wf;
			char c1;
			if ((l->name[0] <= '0') || (l->name[0] > '9')) { /* Skip any IAXRPT monitoring */
				continue;
			}
			if (l->mode == MODE_TRANSCEIVE)
				c1 = 'X';
			else if (l->mode == MODE_LOCAL_MONITOR)
				c1 = 'L';
			else
				c1 = 'M';
			/* Make a string of disconnected nodes for possible restoration */
			sprintf(tmp, "%c%c%.290s", c1, (l->perma) ? 'P' : 'T', l->name);
			if (strlen(tmp) + strlen(myrpt->savednodes) + 1 < MAXNODESTR) {
				if (myrpt->savednodes[0])
					strcat(myrpt->savednodes, ",");
				strcat(myrpt->savednodes, tmp);
			}
			l->retries = l->max_retries + 1;
			l->disced = 2;		/* Silently disconnect */
			rpt_mutex_unlock(&myrpt->lock);
			ast_debug(5, "dumping link %s\n",l->name);
			init_text_frame(&wf, "function_ilink:6");
			wf.datalen = strlen(DISCSTR) + 1;
			wf.data.ptr = DISCSTR;
			if (l->chan) {
				if (l->thisconnected)
					ast_write(l->chan, &wf);
				rpt_safe_sleep(myrpt, l->chan, 250);	/* It's dead already, why check the return value? */
				ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
			}
			rpt_mutex_lock(&myrpt->lock);
		}
		ao2_iterator_destroy(&l_it);
		rpt_mutex_unlock(&myrpt->lock);
		ast_debug(1, "Nodes disconnected: %s\n", myrpt->savednodes);
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, COMPLETE, NULL);
		return DC_COMPLETE;

	case 7:					/* Identify last node which keyed us up */
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, LASTNODEKEY, NULL);
		break;

#ifdef	_MDC_DECODE_H_
	case 17:
		myrpt->lastunit = 0xd00d;
		mdc1200_cmd(myrpt, "ID00D");
		mdc1200_notify(myrpt, NULL, "ID00D");
		mdc1200_send(myrpt, "ID00D");
		break;
#endif
	case 9:					/* Send Text Message */
		if (!param)
			break;
		s1 = strchr(param, ',');
		if (!s1)
			break;
		*s1 = 0;
		s2 = strchr(s1 + 1, ',');
		if (!s2)
			break;
		*s2 = 0;
		snprintf(tmp, MAX_TEXTMSG_SIZE - 1, "M %s %s %s", myrpt->name, s1 + 1, s2 + 1);
		rpt_mutex_lock(&myrpt->lock);
		/* otherwise, send it to all of em */
		ao2_callback(myrpt->links, OBJ_MULTIPLE | OBJ_NODATA, rpt_sendtext_cb, tmp);

		rpt_mutex_unlock(&myrpt->lock);
		rpt_telemetry(myrpt, COMPLETE, NULL);
		return DC_COMPLETE;

	case 16:					/* Restore links disconnected with "disconnect all links" command */
		strcpy(tmp, myrpt->savednodes);	/* Make a copy */
		finddelim(tmp, strs, ARRAY_LEN(strs));	/* convert into substrings */
		for (i = 0; tmp[0] && strs[i] && i < ARRAY_LEN(strs); i++) {
			s1 = strs[i];
			if (s1[0] == 'X')
				mode = MODE_TRANSCEIVE;
			else if (s1[0] == 'L')
				mode = MODE_LOCAL_MONITOR;
			else
				mode = MODE_MONITOR;
			perma = (s1[1] == 'P') ? 1 : 0;
			connect_data = ast_calloc(1, sizeof(struct rpt_connect_data));
			if (!connect_data) {
				break;
			}
			connect_data->myrpt = myrpt;
			connect_data->digitbuf = ast_strdup(s1 + 2);
			if (!connect_data->digitbuf) {
				ast_free(connect_data);
				break;
			}
			connect_data->mode = mode;
			connect_data->perma = perma;
			connect_data->command_source = command_source;
			connect_data->mylink = mylink;
			rpt_mutex_lock(&myrpt->lock);
			myrpt->connect_thread_count++;
			rpt_mutex_unlock(&myrpt->lock);
			if (ast_pthread_create_detached(&rpt_connect_threadid, NULL, rpt_link_connect, (void *) connect_data) < 0) {
				ast_free(connect_data->digitbuf);
				ast_free(connect_data);
				rpt_mutex_lock(&myrpt->lock);
				myrpt->connect_thread_count--;
				ast_assert(myrpt->connect_thread_count >= 0);
				rpt_mutex_unlock(&myrpt->lock);
			}
		}
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, COMPLETE, NULL);
		break;

	case 200:
	case 201:
	case 202:
	case 203:
	case 204:
	case 205:
	case 206:
	case 207:
	case 208:
	case 209:
	case 210:
	case 211:
	case 212:
	case 213:
	case 214:
	case 215:
		if (((myrpt->p.propagate_dtmf) &&
			 (command_source == SOURCE_LNK)) ||
			((myrpt->p.propagate_phonedtmf) &&
			 ((command_source == SOURCE_PHONE) || (command_source == SOURCE_ALT) || (command_source == SOURCE_DPHONE))))
			do_dtmf_local(myrpt, remdtmfstr[myatoi(param) - 200]);
	default:
		return DC_ERROR;

	}

	return DC_INDETERMINATE;
}

enum rpt_function_response function_remote(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source command_source,
	struct rpt_link *mylink)
{
	char *s, *s1, *s2;
	int i, j, p, r, ht, k, l, ls2, m, d, offsave, modesave;
	enum rpt_mode defmode;
	enum rpt_offset offset;
	char multimode = 0;
	char oc, *cp, *cp1, *cp2;
	char tmp[15], freq[15] = "", savestr[15] = "";
	char mhz[MAXREMSTR], decimals[MAXREMSTR];
	union {
		int i;
		void *p;
		char _filler[8];
	} pu;

	ast_debug(7, "%s param=%s digitbuf=%s source=%i\n", myrpt->name, param, digitbuf, command_source);

	if ((!param) || (command_source == SOURCE_RPT) || (command_source == SOURCE_LNK))
		return DC_ERROR;

	p = myatoi(param);
	pu.i = p;

	if ((p != 99) && (p != 5) && (p != 140) && myrpt->p.authlevel && (!myrpt->loginlevel[0]))
		return DC_ERROR;
	multimode = multimode_capable(myrpt);

	switch (p) {

	case 1:					/* retrieve memory */
		if (strlen(digitbuf) < 2)	/* needs 2 digits */
			break;

		for (i = 0; i < 2; i++) {
			if ((digitbuf[i] < '0') || (digitbuf[i] > '9'))
				return DC_ERROR;
		}
		r = get_mem_set(myrpt, digitbuf);
		if (r < 0) {
			rpt_telemetry(myrpt, MEMNOTFOUND, NULL);
			return DC_COMPLETE;
		} else if (r > 0) {
			return DC_ERROR;
		}
		return DC_COMPLETE;

	case 2:					/* set freq and offset */

		for (i = 0, j = 0, k = 0, l = 0; digitbuf[i]; i++) {	/* look for M+*K+*O or M+*H+* depending on mode */
			if (digitbuf[i] == '*') {
				j++;
				continue;
			}
			if ((digitbuf[i] < '0') || (digitbuf[i] > '9'))
				goto invalid_freq;
			else {
				if (j == 0)
					l++;		/* # of digits before first * */
				if (j == 1)
					k++;		/* # of digits after first * */
			}
		}

		i = strlen(digitbuf) - 1;
		if (multimode) {
			if ((j > 2) || (l > 3) || (k > 6))
				goto invalid_freq;	/* &^@#! */
		} else {
			if ((j > 2) || (l > 4) || (k > 5))
				goto invalid_freq;	/* &^@#! */
			if ((!narrow_capable(myrpt)) && (k > 3))
				goto invalid_freq;
		}

		/* Wait for M+*K+* */

		if (j < 2)
			break;				/* Not yet */

		/* We have a frequency */

		ast_copy_string(tmp, digitbuf, sizeof(tmp));

		s = tmp;
		s1 = strsep(&s, "*");	/* Pick off MHz */
		s2 = strsep(&s, "*");	/* Pick off KHz and Hz */
		ls2 = strlen(s2);

		switch (ls2) {			/* Allow partial entry of khz and hz digits for laziness support */
		case 1:
			ht = 0;
			k = 100 * atoi(s2);
			break;

		case 2:
			ht = 0;
			k = 10 * atoi(s2);
			break;

		case 3:
			if ((!narrow_capable(myrpt)) && (!multimode)) {
				if ((s2[2] != '0') && (s2[2] != '5'))
					goto invalid_freq;
			}
			ht = 0;
			k = atoi(s2);
			break;
		case 4:
			k = atoi(s2) / 10;
			ht = 10 * (atoi(s2 + (ls2 - 1)));
			break;

		case 5:
			k = atoi(s2) / 100;
			ht = (atoi(s2 + (ls2 - 2)));
			break;

		default:
			goto invalid_freq;
		}

		/* Check frequency for validity and establish a default mode */

		snprintf(freq, sizeof(freq), "%s.%03d%02d", s1, k, ht);

		ast_debug(1, "New frequency: %s\n", freq);

		split_freq(mhz, decimals, freq);
		m = atoi(mhz);
		d = atoi(decimals);

		if (check_freq(myrpt, m, d, &defmode))	/* Check to see if frequency entered is legit */
			goto invalid_freq;

		if ((defmode == REM_MODE_FM) && (digitbuf[i] == '*'))	/* If FM, user must enter and additional offset digit */
			break;				/* Not yet */

		offset = REM_SIMPLEX;	/* Assume simplex */

		if (defmode == REM_MODE_FM) {
			oc = *s;			/* Pick off offset */

			if (oc) {
				switch (oc) {
				case '1':
					offset = REM_MINUS;
					break;

				case '2':
					offset = REM_SIMPLEX;
					break;

				case '3':
					offset = REM_PLUS;
					break;

				default:
					goto invalid_freq;
				}
			}
		}
		offsave = myrpt->offset;
		modesave = myrpt->remmode;
		ast_copy_string(savestr, myrpt->freq, sizeof(savestr) - 1);
		ast_copy_string(myrpt->freq, freq, sizeof(myrpt->freq) - 1);
		myrpt->offset = offset;
		myrpt->remmode = defmode;

		if (setrem(myrpt) == -1) {
			myrpt->offset = offsave;
			myrpt->remmode = modesave;
			ast_copy_string(myrpt->freq, savestr, sizeof(myrpt->freq) - 1);
			goto invalid_freq;
		}
		if (strcmp(myrpt->remoterig, REMOTE_RIG_TM271) && strcmp(myrpt->remoterig, REMOTE_RIG_KENWOOD))
			rpt_telemetry(myrpt, COMPLETE, NULL);
		return DC_COMPLETE;

	  invalid_freq:
		rpt_telemetry(myrpt, INVFREQ, NULL);
		return DC_ERROR;

	case 3:					/* set rx PL tone */
		for (i = 0, j = 0, k = 0, l = 0; digitbuf[i]; i++) {	/* look for N+*N */
			if (digitbuf[i] == '*') {
				j++;
				continue;
			}
			if ((digitbuf[i] < '0') || (digitbuf[i] > '9'))
				return DC_ERROR;
			else {
				if (j)
					l++;
				else
					k++;
			}
		}
		if ((j > 1) || (k > 3) || (l > 1))
			return DC_ERROR;	/* &$@^! */
		i = strlen(digitbuf) - 1;
		if ((j != 1) || (k < 2) || (l != 1))
			break;				/* Not yet */
		ast_debug(1, "PL digits entered %s\n", digitbuf);

		ast_copy_string(tmp, digitbuf, sizeof(tmp));
		/* see if we have at least 1 */
		s = strchr(tmp, '*');
		if (s)
			*s = '.';
		ast_copy_string(savestr, myrpt->rxpl, sizeof(savestr) - 1);
		ast_copy_string(myrpt->rxpl, tmp, sizeof(myrpt->rxpl) - 1);
		if ((!strcmp(myrpt->remoterig, REMOTE_RIG_RBI)) || (!strcmp(myrpt->remoterig, REMOTE_RIG_FT100))) {
			ast_copy_string(myrpt->txpl, tmp, sizeof(myrpt->txpl) - 1);
		}
		if (setrem(myrpt) == -1) {
			ast_copy_string(myrpt->rxpl, savestr, sizeof(myrpt->rxpl) - 1);
			return DC_ERROR;
		}
		return DC_COMPLETE;

	case 4:					/* set tx PL tone */
		/* can't set tx tone on RBI (rx tone does both) */
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_RBI)) {
			rpt_mutex_unlock(&myrpt->lock);
			return DC_ERROR;
		}
		/* can't set tx tone on ft100 (rx tone does both) */
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT100)) {
			rpt_mutex_unlock(&myrpt->lock);
			return DC_ERROR;
		}
		/*  eventually for the ic706 instead of just throwing the exception
		   we can check if we are in encode only mode and allow the tx
		   ctcss code to be changed. but at least the warning message is
		   issued for now.
		 */
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_IC706)) {
			ast_log(LOG_WARNING, "Setting IC706 Tx CTCSS Code Not Supported. Set Rx Code for both.\n");
			rpt_mutex_unlock(&myrpt->lock);
			return DC_ERROR;
		}
		for (i = 0, j = 0, k = 0, l = 0; digitbuf[i]; i++) { /* look for N+*N */
			if (digitbuf[i] == '*') {
				j++;
				continue;
			}
			if ((digitbuf[i] < '0') || (digitbuf[i] > '9'))
				return DC_ERROR;
			else {
				if (j)
					l++;
				else
					k++;
			}
		}
		if ((j > 1) || (k > 3) || (l > 1))
			return DC_ERROR;	/* &$@^! */
		i = strlen(digitbuf) - 1;
		if ((j != 1) || (k < 2) || (l != 1))
			break;				/* Not yet */
		ast_debug(1, "PL digits entered %s\n", digitbuf);

		ast_copy_string(tmp, digitbuf, sizeof(tmp));
		/* see if we have at least 1 */
		s = strchr(tmp, '*');
		if (s)
			*s = '.';

		rpt_mutex_lock(&myrpt->lock);
		ast_copy_string(savestr, myrpt->txpl, sizeof(savestr) - 1);
		ast_copy_string(myrpt->txpl, tmp, sizeof(myrpt->txpl) - 1);
		rpt_mutex_unlock(&myrpt->lock);

		if (setrem(myrpt) == -1) {
			rpt_mutex_lock(&myrpt->lock);
			ast_copy_string(myrpt->txpl, savestr, sizeof(myrpt->txpl) - 1);
			rpt_mutex_unlock(&myrpt->lock);
			return DC_ERROR;
		}
		return DC_COMPLETE;

	case 6:					/* MODE (FM,USB,LSB,AM) */
		if (strlen(digitbuf) < 1)
			break;

		if (!multimode)
			return DC_ERROR;	/* Multimode radios only */

		switch (*digitbuf) {
		case '1':
			split_freq(mhz, decimals, myrpt->freq);
			m = atoi(mhz);
			if (m < 29)			/* No FM allowed below 29MHz! */
				return DC_ERROR;
			myrpt->remmode = REM_MODE_FM;

			rpt_telemetry(myrpt, REMMODE, NULL);
			break;

		case '2':
			myrpt->remmode = REM_MODE_USB;
			rpt_telemetry(myrpt, REMMODE, NULL);
			break;

		case '3':
			myrpt->remmode = REM_MODE_LSB;
			rpt_telemetry(myrpt, REMMODE, NULL);
			break;

		case '4':
			myrpt->remmode = REM_MODE_AM;
			rpt_telemetry(myrpt, REMMODE, NULL);
			break;

		default:
			return DC_ERROR;
		}

		if (setrem(myrpt))
			return DC_ERROR;
		return DC_COMPLETEQUIET;
	case 99:
		/* can't log in when logged in */
		if (myrpt->loginlevel[0])
			return DC_ERROR;
		rpt_mutex_lock(&myrpt->lock);
		*myrpt->loginuser = 0;
		myrpt->loginlevel[0] = 0;
		rpt_mutex_unlock(&myrpt->lock);
		cp = ast_strdup(param);
		cp1 = strchr(cp, ',');
		if (cp1) {
			rpt_mutex_lock(&myrpt->lock);
			*cp1 = 0;
			cp2 = strchr(cp1 + 1, ',');
			if (cp2) {
				*cp2 = 0;
				ast_copy_string(myrpt->loginlevel, cp2 + 1, sizeof(myrpt->loginlevel));
			}
			ast_copy_string(myrpt->loginuser, cp1 + 1, sizeof(myrpt->loginuser) - 1);
			rpt_mutex_unlock(&myrpt->lock);
			donodelog_fmt(myrpt, "LOGIN,%s,%s", myrpt->loginuser, myrpt->loginlevel);
			ast_debug(1, "loginuser %s level %s\n", myrpt->loginuser, myrpt->loginlevel);
			rpt_telemetry(myrpt, REMLOGIN, NULL);
		}

		ast_free(cp);
		return DC_COMPLETEQUIET;
	case 100:					/* RX PL Off */
		myrpt->rxplon = 0;
		setrem(myrpt);
		rpt_telemetry(myrpt, REMXXX, pu.p);
		return DC_COMPLETEQUIET;
	case 101:					/* RX PL On */
		myrpt->rxplon = 1;
		setrem(myrpt);
		rpt_telemetry(myrpt, REMXXX, pu.p);
		return DC_COMPLETEQUIET;
	case 102:					/* TX PL Off */
		myrpt->txplon = 0;
		setrem(myrpt);
		rpt_telemetry(myrpt, REMXXX, pu.p);
		return DC_COMPLETEQUIET;
	case 103:					/* TX PL On */
		myrpt->txplon = 1;
		setrem(myrpt);
		rpt_telemetry(myrpt, REMXXX, pu.p);
		return DC_COMPLETEQUIET;
	case 104:					/* Low Power */
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_IC706))
			return DC_ERROR;
		myrpt->powerlevel = REM_LOWPWR;
		setrem(myrpt);
		rpt_telemetry(myrpt, REMXXX, pu.p);
		return DC_COMPLETEQUIET;
	case 105:					/* Medium Power */
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_IC706))
			return DC_ERROR;
		if (ISRIG_RTX(myrpt->remoterig))
			return DC_ERROR;
		myrpt->powerlevel = REM_MEDPWR;
		setrem(myrpt);
		rpt_telemetry(myrpt, REMXXX, pu.p);
		return DC_COMPLETEQUIET;
	case 106:					/* Hi Power */
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_IC706))
			return DC_ERROR;
		myrpt->powerlevel = REM_HIPWR;
		setrem(myrpt);
		rpt_telemetry(myrpt, REMXXX, pu.p);
		return DC_COMPLETEQUIET;
	case 107:					/* Bump down 20Hz */
		multimode_bump_freq(myrpt, -20);
		return DC_COMPLETE;
	case 108:					/* Bump down 100Hz */
		multimode_bump_freq(myrpt, -100);
		return DC_COMPLETE;
	case 109:					/* Bump down 500Hz */
		multimode_bump_freq(myrpt, -500);
		return DC_COMPLETE;
	case 110:					/* Bump up 20Hz */
		multimode_bump_freq(myrpt, 20);
		return DC_COMPLETE;
	case 111:					/* Bump up 100Hz */
		multimode_bump_freq(myrpt, 100);
		return DC_COMPLETE;
	case 112:					/* Bump up 500Hz */
		multimode_bump_freq(myrpt, 500);
		return DC_COMPLETE;
	case 113:					/* Scan down slow */
		myrpt->scantimer = REM_SCANTIME;
		myrpt->hfscanmode = HF_SCAN_DOWN_SLOW;
		rpt_telemetry(myrpt, REMXXX, pu.p);
		return DC_COMPLETEQUIET;
	case 114:					/* Scan down quick */
		myrpt->scantimer = REM_SCANTIME;
		myrpt->hfscanmode = HF_SCAN_DOWN_QUICK;
		rpt_telemetry(myrpt, REMXXX, pu.p);
		return DC_COMPLETEQUIET;
	case 115:					/* Scan down fast */
		myrpt->scantimer = REM_SCANTIME;
		myrpt->hfscanmode = HF_SCAN_DOWN_FAST;
		rpt_telemetry(myrpt, REMXXX, pu.p);
		return DC_COMPLETEQUIET;
	case 116:					/* Scan up slow */
		myrpt->scantimer = REM_SCANTIME;
		myrpt->hfscanmode = HF_SCAN_UP_SLOW;
		rpt_telemetry(myrpt, REMXXX, pu.p);
		return DC_COMPLETEQUIET;
	case 117:					/* Scan up quick */
		myrpt->scantimer = REM_SCANTIME;
		myrpt->hfscanmode = HF_SCAN_UP_QUICK;
		rpt_telemetry(myrpt, REMXXX, pu.p);
		return DC_COMPLETEQUIET;
	case 118:					/* Scan up fast */
		myrpt->scantimer = REM_SCANTIME;
		myrpt->hfscanmode = HF_SCAN_UP_FAST;
		rpt_telemetry(myrpt, REMXXX, pu.p);
		return DC_COMPLETEQUIET;
	case 119:					/* Tune Request */
		ast_debug(4, "TUNE REQUEST\n");
		/* if not currently going, and valid to do */
		if ((!myrpt->tunerequest) &&
			((!strcmp(myrpt->remoterig, REMOTE_RIG_FT897)) ||
			 (!strcmp(myrpt->remoterig, REMOTE_RIG_FT100)) ||
			 (!strcmp(myrpt->remoterig, REMOTE_RIG_FT950)) || (!strcmp(myrpt->remoterig, REMOTE_RIG_IC706)))) {
			myrpt->remotetx = 0;
			if (!IS_PSEUDO(myrpt->txchannel)) {
				ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_UNKEY);
			}
			myrpt->tunetx = 0;
			myrpt->tunerequest = 1;
			rpt_telemetry(myrpt, TUNE, NULL);
			return DC_COMPLETEQUIET;
		}
		return DC_ERROR;
	case 5:					/* Long Status */
		rpt_telemetry(myrpt, REMLONGSTATUS, NULL);
		return DC_COMPLETEQUIET;
	case 140:					/* Short Status */
		rpt_telemetry(myrpt, REMSHORTSTATUS, NULL);
		return DC_COMPLETEQUIET;
	case 200:
	case 201:
	case 202:
	case 203:
	case 204:
	case 205:
	case 206:
	case 207:
	case 208:
	case 209:
	case 210:
	case 211:
	case 212:
	case 213:
	case 214:
	case 215:
		do_dtmf_local(myrpt, remdtmfstr[p - 200]);
		return DC_COMPLETEQUIET;
	default:
		break;
	}

	return DC_INDETERMINATE;
}

enum rpt_function_response function_autopatchup(struct rpt *myrpt, char *param, char *digitbuf,
	enum rpt_command_source command_source, struct rpt_link *mylink)
{
	int i, index, paramlength, nostar = 0;
	char *lparam;
	char *value = NULL;
	char *paramlist[20];

	static char *keywords[] = {
		"context",
		"dialtime",
		"farenddisconnect",
		"noct",
		"quiet",
		"voxalways",
		"exten",
		"nostar",
		NULL
	};

	if (myrpt->p.s[myrpt->p.sysstate_cur].txdisable || myrpt->p.s[myrpt->p.sysstate_cur].autopatchdisable)
		return DC_ERROR;

	ast_debug(1, "@@@@ Autopatch up\n");

	if (myrpt->callmode == CALLMODE_DOWN) {
		/* Set defaults */
		myrpt->patchnoct = 0;
		myrpt->patchdialtime = 0;
		myrpt->patchfarenddisconnect = 0;
		myrpt->patchquiet = 0;
		myrpt->patchvoxalways = 0;
		ast_copy_string(myrpt->patchcontext, myrpt->p.ourcontext, sizeof(myrpt->patchcontext));
		memset(myrpt->patchexten, 0, sizeof(myrpt->patchexten));
	}
	if (param) {
		/* Process parameter list */
		lparam = ast_strdup(param);
		if (!lparam) {
			return DC_ERROR;
		}
		paramlength = finddelim(lparam, paramlist, ARRAY_LEN(paramlist));
		for (i = 0; i < paramlength; i++) {
			index = matchkeyword(paramlist[i], &value, keywords);
			if (value)
				value = skipchars(value, "= ");
			if (myrpt->callmode == CALLMODE_DOWN) {
				switch (index) {
				case 1:		/* context */
					rpt_mutex_lock(&myrpt->lock);
					ast_copy_string(myrpt->patchcontext, value, sizeof(myrpt->patchcontext));
					rpt_mutex_unlock(&myrpt->lock);
					break;
				case 2:		/* dialtime */
					rpt_mutex_lock(&myrpt->lock);
					myrpt->patchdialtime = atoi(value);
					rpt_mutex_unlock(&myrpt->lock);
					break;
				case 3:		/* farenddisconnect */
					rpt_mutex_lock(&myrpt->lock);
					myrpt->patchfarenddisconnect = (atoi(value) ? 1 : 0);
					rpt_mutex_unlock(&myrpt->lock);
					break;
				case 4:		/* noct */
					rpt_mutex_lock(&myrpt->lock);
					myrpt->patchnoct = (atoi(value) ? 1 : 0);
					rpt_mutex_unlock(&myrpt->lock);
					break;
				case 5:		/* quiet */
					rpt_mutex_lock(&myrpt->lock);
					myrpt->patchquiet = (atoi(value) ? 1 : 0);
					rpt_mutex_unlock(&myrpt->lock);
					break;
				case 6:		/* voxalways */
					rpt_mutex_lock(&myrpt->lock);
					myrpt->patchvoxalways = (atoi(value) ? 1 : 0);
					rpt_mutex_unlock(&myrpt->lock);
					break;
				case 7:		/* exten */
					rpt_mutex_lock(&myrpt->lock);
					ast_copy_string(myrpt->patchexten, value, sizeof(myrpt->patchexten));
					rpt_mutex_unlock(&myrpt->lock);
					break;
				default:
					break;
				}
			} else {
				switch (index) {
				case 8:		/* nostar */
					nostar = 1;
					break;
				}
			}
		}
		ast_free(lparam);
	}

	rpt_mutex_lock(&myrpt->lock);

	/* if on call, force * into current audio stream */

	if ((myrpt->callmode == CALLMODE_CONNECTING) || (myrpt->callmode == CALLMODE_UP)) {
		if (!nostar)
			myrpt->mydtmf = myrpt->p.funcchar;
	}
	if (myrpt->callmode != CALLMODE_DOWN) {
		rpt_mutex_unlock(&myrpt->lock);
		return DC_COMPLETE;
	}
	myrpt->callmode = CALLMODE_DIALING;
	myrpt->cidx = 0;
	myrpt->exten[myrpt->cidx] = 0;
	rpt_mutex_unlock(&myrpt->lock);
	ast_pthread_create_detached(&myrpt->rpt_call_thread, NULL, rpt_call, (void *) myrpt);
	return DC_COMPLETE;
}

enum rpt_function_response function_autopatchdn(struct rpt *myrpt, char *param, char *digitbuf,
	enum rpt_command_source command_source, struct rpt_link *mylink)
{
	if (myrpt->p.s[myrpt->p.sysstate_cur].txdisable || myrpt->p.s[myrpt->p.sysstate_cur].autopatchdisable)
		return DC_ERROR;

	ast_debug(1, "@@@@ Autopatch down\n");

	rpt_mutex_lock(&myrpt->lock);

	myrpt->macropatch = 0;

	if (myrpt->callmode == CALLMODE_DOWN) {
		rpt_mutex_unlock(&myrpt->lock);
		return DC_COMPLETE;
	}

	myrpt->callmode = CALLMODE_DOWN;
	channel_revert(myrpt);
	rpt_mutex_unlock(&myrpt->lock);
	rpt_telem_select(myrpt, command_source, mylink);
	if (!myrpt->patchquiet)
		rpt_telemetry(myrpt, TERM, NULL);
	return DC_COMPLETE;
}

enum rpt_function_response function_status(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source command_source,
	struct rpt_link *mylink)
{
	struct rpt_tele *telem;

	if (!param)
		return DC_ERROR;

	if ((myrpt->p.s[myrpt->p.sysstate_cur].txdisable) || (myrpt->p.s[myrpt->p.sysstate_cur].userfundisable))
		return DC_ERROR;

	ast_debug(1, "@@@@ status param = %s, digitbuf = %s\n", (param) ? param : "(null)", digitbuf);

	switch (myatoi(param)) {
	case 1:					/* System ID */
		if (myrpt->p.idtime) {	/* ID time must be non-zero */
			myrpt->mustid = myrpt->tailid = 0;
			myrpt->idtimer = myrpt->p.idtime;
		}
		rpt_mutex_lock(&myrpt->lock);
		telem = myrpt->tele.next;
		while (telem != &myrpt->tele) {
			if (((telem->mode == ID) || (telem->mode == ID1)) && (!telem->killed)) {
				if (telem->chan)
					ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);	/* Whoosh! */
				telem->killed = 1;
			}
			telem = telem->next;
		}
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telemetry(myrpt, ID1, NULL);
		return DC_COMPLETE;
	case 2:					/* System Time */
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, STATS_TIME, NULL);
		return DC_COMPLETE;
	case 3:					/* app_rpt.c version */
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, STATS_VERSION, NULL);
		return DC_COMPLETE;
	case 4:					/* GPS data */
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, STATS_GPS, NULL);
		return DC_COMPLETE;
	case 5:					/* Identify last node which keyed us up */
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, LASTUSER, NULL);
		return DC_COMPLETE;
	case 11:					/* System ID (local only) */
		rpt_mutex_lock(&myrpt->lock);
		if (myrpt->p.idtime) { /* ID time must be non-zero */
			myrpt->mustid = myrpt->tailid = 0;
			myrpt->idtimer = myrpt->p.idtime;
		}
		telem = myrpt->tele.next;
		while (telem != &myrpt->tele) {
			if (((telem->mode == ID) || (telem->mode == ID1)) && (!telem->killed)) {
				if (telem->chan)
					ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);	/* Whoosh! */
				telem->killed = 1;
			}
			telem = telem->next;
		}
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telemetry(myrpt, ID, NULL);
		return DC_COMPLETE;
	case 12:					/* System Time (local only) */
		rpt_telemetry(myrpt, STATS_TIME_LOCAL, NULL);
		return DC_COMPLETE;
	case 99:					/* GPS data announced locally */
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, STATS_GPS_LEGACY, NULL);
		return DC_COMPLETE;
	default:
		return DC_ERROR;
	}
	return DC_INDETERMINATE;
}

enum rpt_function_response function_macro(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source command_source,
	struct rpt_link *mylink)
{
	const char *val;
	int i;
	if (myrpt->remote)
		return DC_ERROR;

	ast_debug(1, "@@@@ macro-oni param = %s, digitbuf = %s\n", (param) ? param : "(null)", digitbuf);

	if (strlen(digitbuf) < 1)	/* needs 1 digit */
		return DC_INDETERMINATE;

	for (i = 0; i < digitbuf[i]; i++) {
		if ((digitbuf[i] < '0') || (digitbuf[i] > '9'))
			return DC_ERROR;
	}

	if (*digitbuf == '0') {
		val = myrpt->p.startupmacro;
	} else {
		val = ast_variable_retrieve(myrpt->cfg, myrpt->p.macro, digitbuf);
	}
	/* param was 1 for local buf */
	if (!val) {
		if (strlen(digitbuf) < myrpt->macro_longest) {
			return DC_INDETERMINATE;
		}
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, MACRO_NOTFOUND, NULL);
		return DC_COMPLETE;
	}
	macro_append(myrpt, val);
	return DC_COMPLETE;
}

enum rpt_function_response function_playback(struct rpt *myrpt, char *param, char *digitbuf,
	enum rpt_command_source command_source, struct rpt_link *mylink)
{
	if (myrpt->remote)
		return DC_ERROR;

	ast_debug(1, "@@@@ playback param = %s, digitbuf = %s\n", (param) ? param : "(null)", digitbuf);

	if (ast_fileexists(param, NULL, ast_channel_language(myrpt->rxchannel)) <= 0)
		return DC_ERROR;

	rpt_telem_select(myrpt, command_source, mylink);
	rpt_telemetry(myrpt, PLAYBACK, param);
	return DC_COMPLETE;
}

enum rpt_function_response function_localplay(struct rpt *myrpt, char *param, char *digitbuf,
	enum rpt_command_source command_source, struct rpt_link *mylink)
{
	if (myrpt->remote)
		return DC_ERROR;

	ast_debug(1, "@@@@ localplay param = %s, digitbuf = %s\n", (param) ? param : "(null)", digitbuf);

	if (ast_fileexists(param, NULL, ast_channel_language(myrpt->rxchannel)) <= 0)
		return DC_ERROR;

	rpt_telemetry(myrpt, LOCALPLAY, param);
	return DC_COMPLETE;
}

enum rpt_function_response function_cop(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source command_source,
	struct rpt_link *mylink)
{
	char string[50], func[100];
	char paramcopy[500];
	int argc;
	char *argv[101], *cp;
	int i, j, k, r;
	enum rpt_linkmode src;
	struct rpt_tele *telem;
#ifdef	_MDC_ENCODE_H_
	struct mdcparams *mdcp;
#endif

	if (!param)
		return DC_ERROR;

	ast_copy_string(paramcopy, param, sizeof(paramcopy));
	argc = explode_string(paramcopy, argv, ARRAY_LEN(argv), ',', 0);

	if (!argc)
		return DC_ERROR;

	switch (myatoi(argv[0])) {
	case 1:					/* System reset */
		i = system("killall -9 asterisk");
		return DC_COMPLETE;

	case 2:
		myrpt->p.s[myrpt->p.sysstate_cur].txdisable = 0;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "RPTENA");
		return DC_COMPLETE;

	case 3:
		myrpt->p.s[myrpt->p.sysstate_cur].txdisable = 1;
		return DC_COMPLETE;

	case 4:					/* test tone on */
		if (myrpt->stopgen < 0) {
			myrpt->stopgen = 1;
		} else {
			myrpt->stopgen = 0;
			rpt_telemetry(myrpt, TEST_TONE, NULL);
		}
		if (!myrpt->remote)
			return DC_COMPLETE;
		if (myrpt->remstopgen < 0) {
			myrpt->remstopgen = 1;
		} else {
			if (myrpt->remstopgen)
				break;
			myrpt->remstopgen = -1;
			if (ast_tonepair_start(myrpt->txchannel, 1000.0, 0, 99999999, 7200.0)) {
				myrpt->remstopgen = 0;
				break;
			}
		}
		return DC_COMPLETE;

	case 5:					/* Disgorge variables to log for debug purposes */
		myrpt->disgorgetime = time(NULL) + 10;	/* Do it 10 seconds later */
		return DC_COMPLETE;

	case 6:					/* Simulate COR being activated (phone only) */
		if (command_source != SOURCE_PHONE)
			return DC_INDETERMINATE;
		return DC_DOKEY;

	case 7:					/* Time out timer enable */
		myrpt->p.s[myrpt->p.sysstate_cur].totdisable = 0;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "TOTENA");
		return DC_COMPLETE;

	case 8:					/* Time out timer disable */
		myrpt->p.s[myrpt->p.sysstate_cur].totdisable = 1;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "TOTDIS");
		return DC_COMPLETE;

	case 9:					/* Autopatch enable */
		myrpt->p.s[myrpt->p.sysstate_cur].autopatchdisable = 0;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "APENA");
		return DC_COMPLETE;

	case 10:					/* Autopatch disable */
		myrpt->p.s[myrpt->p.sysstate_cur].autopatchdisable = 1;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "APDIS");
		return DC_COMPLETE;

	case 11:					/* Link Enable */
		myrpt->p.s[myrpt->p.sysstate_cur].linkfundisable = 0;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "LNKENA");
		return DC_COMPLETE;

	case 12:					/* Link Disable */
		myrpt->p.s[myrpt->p.sysstate_cur].linkfundisable = 1;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "LNKDIS");
		return DC_COMPLETE;

	case 13:					/* Query System State */
		string[0] = string[1] = 'S';
		string[2] = myrpt->p.sysstate_cur + '0';
		string[3] = '\0';
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) string);
		return DC_COMPLETE;

	case 14:					/* Change System State */
		if (strlen(digitbuf) == 0)
			break;
		if ((digitbuf[0] < '0') || (digitbuf[0] > '9'))
			return DC_ERROR;
		myrpt->p.sysstate_cur = digitbuf[0] - '0';
		string[0] = string[1] = 'S';
		string[2] = myrpt->p.sysstate_cur + '0';
		string[3] = '\0';
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) string);
		return DC_COMPLETE;

	case 15:					/* Scheduler Enable */
		myrpt->p.s[myrpt->p.sysstate_cur].schedulerdisable = 0;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "SKENA");
		return DC_COMPLETE;

	case 16:					/* Scheduler Disable */
		myrpt->p.s[myrpt->p.sysstate_cur].schedulerdisable = 1;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "SKDIS");
		return DC_COMPLETE;

	case 17:					/* User functions Enable */
		myrpt->p.s[myrpt->p.sysstate_cur].userfundisable = 0;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "UFENA");
		return DC_COMPLETE;

	case 18:					/* User Functions Disable */
		myrpt->p.s[myrpt->p.sysstate_cur].userfundisable = 1;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "UFDIS");
		return DC_COMPLETE;

	case 19:					/* Alternate Tail Enable */
		myrpt->p.s[myrpt->p.sysstate_cur].alternatetail = 1;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "ATENA");
		return DC_COMPLETE;

	case 20:					/* Alternate Tail Disable */
		myrpt->p.s[myrpt->p.sysstate_cur].alternatetail = 0;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "ATDIS");
		return DC_COMPLETE;

	case 21: /* Parrot Mode Enable */
		birdbath(myrpt);
		if (myrpt->p.parrotmode != PARROT_MODE_ON_ALWAYS) {
			rpt_mutex_lock(&myrpt->lock);
			myrpt->parrotonce = 0;
			rpt_mutex_unlock(&myrpt->lock);
			myrpt->p.parrotmode = PARROT_MODE_ON_COMMAND;
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;
		}
		break;
	case 22: /* Parrot Mode Disable */
		birdbath(myrpt);
		if (myrpt->p.parrotmode != PARROT_MODE_ON_ALWAYS) {
			myrpt->p.parrotmode = PARROT_MODE_OFF;
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;
		}
		break;
	case 23:					/* flush parrot in progress */
		birdbath(myrpt);
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, COMPLETE, NULL);
		return DC_COMPLETE;
	case 24:					/* flush all telemetry */
		flush_telem(myrpt);
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, COMPLETE, NULL);
		return DC_COMPLETE;
	case 25:					/* request keying info (brief) */
		send_link_keyquery(myrpt);
		rpt_mutex_lock(&myrpt->lock);
		myrpt->topkeylong = 0;
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, COMPLETE, NULL);
		return DC_COMPLETE;
	case 26:					/* request keying info (full) */
		send_link_keyquery(myrpt);
		rpt_mutex_lock(&myrpt->lock);
		myrpt->topkeylong = 1;
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, COMPLETE, NULL);
		return DC_COMPLETE;

	case 27:					/* Reset DAQ minimum */
		if (argc != 3)
			return DC_ERROR;
		if (!(daq_reset_minmax(argv[1], atoi(argv[2]), 0))) {
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;
		}
		return DC_ERROR;

	case 28:					/* Reset DAQ maximum */
		if (argc != 3)
			return DC_ERROR;
		if (!(daq_reset_minmax(argv[1], atoi(argv[2]), 1))) {
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;
		}
		return DC_ERROR;

	case 30:					/* recall memory location on programmable radio */

		if (strlen(digitbuf) < 2)	/* needs 2 digits */
			break;

		for (i = 0; i < 2; i++) {
			if ((digitbuf[i] < '0') || (digitbuf[i] > '9'))
				return DC_ERROR;
		}

		r = retrieve_memory(myrpt, digitbuf);
		if (r < 0) {
			rpt_telemetry(myrpt, MEMNOTFOUND, NULL);
			return DC_COMPLETE;
		}
		if (r > 0) {
			return DC_ERROR;
		}
		if (setrem(myrpt) == -1)
			return DC_ERROR;
		return DC_COMPLETE;

	case 31:
		/* set channel. note that it's going to change channel 
		   then confirm on the new channel! */
		if (strlen(digitbuf) < 2)	/* needs 2 digits */
			break;

		for (i = 0; i < 2; i++) {
			if ((digitbuf[i] < '0') || (digitbuf[i] > '9'))
				return DC_ERROR;
		}
		channel_steer(myrpt, digitbuf);
		return DC_COMPLETE;

	case 32:					/* Touch Tone Pad Test */
		i = strlen(digitbuf);
		if (!i) {
			ast_debug(5, "Padtest entered");
			rpt_mutex_lock(&myrpt->lock);
			myrpt->inpadtest = 1;
			rpt_mutex_unlock(&myrpt->lock);
			break;
		} else {
			ast_debug(5, "Padtest len= %d digits=%s", i, digitbuf);
			if (digitbuf[i - 1] != myrpt->p.endchar)
				break;
			rpt_telemetry(myrpt, ARB_ALPHA, digitbuf);
			rpt_mutex_lock(&myrpt->lock);
			myrpt->inpadtest = 0;
			rpt_mutex_unlock(&myrpt->lock);
			ast_debug(5, "Padtest exited");
			return DC_COMPLETE;
		}
	case 33:					/* Local Telem mode Enable */
		if (myrpt->p.telemdynamic) {
			myrpt->telemmode = 0x7fffffff;
			rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;
		}
		break;
	case 34:					/* Local Telem mode Disable */
		if (myrpt->p.telemdynamic) {
			rpt_mutex_lock(&myrpt->lock);
			myrpt->telemmode = 0;
			rpt_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;
		}
		break;
	case 35:					/* Local Telem mode Normal */
		if (myrpt->p.telemdynamic) {
			rpt_mutex_lock(&myrpt->lock);
			myrpt->telemmode = 1;
			rpt_mutex_unlock(&myrpt->lock);
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;
		}
		break;
	case 36:					/* Link Output Enable */
		if (!mylink) {
			return DC_ERROR;
		}
		src = LINKMODE_OFF;
		if ((mylink->name[0] <= '0') || (mylink->name[0] > '9')) {
			src = LINKMODE_GUI;
		}
		if (mylink->phonemode) {
			src = LINKMODE_PHONE;
		} else if (CHAN_TECH(mylink->chan, "echolink")) {
			src = LINKMODE_ECHOLINK;
		} else if (CHAN_TECH(mylink->chan, "tlb")) {
			src = LINKMODE_TLB;
		}
		if (src && myrpt->p.linkmodedynamic[src]) {
			set_linkmode(mylink, LINKMODE_ON);
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;
		}
		break;
	case 37:					/* Link Output Disable */
		if (!mylink) {
			return DC_ERROR;
		}
		src = 0;
		if ((mylink->name[0] <= '0') || (mylink->name[0] > '9')) {
			src = LINKMODE_GUI;
		}
		if (mylink->phonemode) {
			src = LINKMODE_PHONE;
		} else if (CHAN_TECH(mylink->chan, "echolink")) {
			src = LINKMODE_ECHOLINK;
		} else if (CHAN_TECH(mylink->chan, "tlb")) {
			src = LINKMODE_TLB;
		}
		if (src && myrpt->p.linkmodedynamic[src]) {
			set_linkmode(mylink, LINKMODE_OFF);
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;
		}
		break;
	case 38:					/* Gui Link Output Follow */
		if (!mylink) {
			return DC_ERROR;
		}
		src = 0;
		if ((mylink->name[0] <= '0') || (mylink->name[0] > '9')) {
			src = LINKMODE_GUI;
		}
		if (mylink->phonemode) {
			src = LINKMODE_PHONE;
		} else if (CHAN_TECH(mylink->chan, "echolink")) {
			src = LINKMODE_ECHOLINK;
		} else if (CHAN_TECH(mylink->chan, "tlb")) {
			src = LINKMODE_TLB;
		}
		if (src && myrpt->p.linkmodedynamic[src]) {
			set_linkmode(mylink, LINKMODE_FOLLOW);
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;
		}
		break;
	case 39:					/* Link Output Demand */
		if (!mylink) {
			return DC_ERROR;
		}
		src = 0;
		if ((mylink->name[0] <= '0') || (mylink->name[0] > '9')) {
			src = LINKMODE_GUI;
		}
		if (mylink->phonemode) {
			src = LINKMODE_PHONE;
		} else if (CHAN_TECH(mylink->chan, "echolink")) {
			src = LINKMODE_ECHOLINK;
		} else if (CHAN_TECH(mylink->chan, "tlb")) {
			src = LINKMODE_TLB;
		}
		if (src && myrpt->p.linkmodedynamic[src]) {
			set_linkmode(mylink, LINKMODE_DEMAND);
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;
		}
		break;
	case 42:					/* Echolink announce node # only */
		myrpt->p.eannmode = 1;
		rpt_telemetry(myrpt, COMPLETE, NULL);
		return DC_COMPLETE;
	case 43:					/* Echolink announce node Callsign only */
		myrpt->p.eannmode = 2;
		rpt_telemetry(myrpt, COMPLETE, NULL);
		return DC_COMPLETE;
	case 44:					/* Echolink announce node # & Callsign */
		myrpt->p.eannmode = 3;
		rpt_telemetry(myrpt, COMPLETE, NULL);
		return DC_COMPLETE;
	case 45:					/* Link activity timer enable */
		if (myrpt->p.lnkacttime && myrpt->p.lnkactmacro) {
			myrpt->linkactivitytimer = 0;
			myrpt->linkactivityflag = 0;
			myrpt->p.lnkactenable = 1;
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, ARB_ALPHA, (void *) "LATENA");
		}
		return DC_COMPLETE;

	case 46:					/* Link activity timer disable */
		if (myrpt->p.lnkacttime && myrpt->p.lnkactmacro) {
			rpt_mutex_lock(&myrpt->lock);
			myrpt->linkactivitytimer = 0;
			myrpt->linkactivityflag = 0;
			myrpt->p.lnkactenable = 0;
			rpt_mutex_unlock(&myrpt->lock);
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, ARB_ALPHA, (void *) "LATDIS");
		}
		return DC_COMPLETE;

	case 47:					/* Link activity flag kill */
		rpt_mutex_lock(&myrpt->lock);
		myrpt->linkactivitytimer = 0;
		myrpt->linkactivityflag = 0;
		rpt_mutex_unlock(&myrpt->lock);
		return DC_COMPLETE; /* Silent for a reason (only used in macros) */

	case 48:					/* play page sequence */
		j = 0;
		for (i = 1; i < argc; i++) {
			k = strlen(argv[i]);
			if (k != 1) {
				j += k + 1;
				continue;
			}
			if (*argv[i] >= '0' && *argv[i] <= '9')
				argv[i] = dtmf_tones[*argv[i] - '0'];
			else if (*argv[i] >= 'A' && (*argv[i]) <= 'D')
				argv[i] = dtmf_tones[*argv[i] - 'A' + 10];
			else if (*argv[i] == '*')
				argv[i] = dtmf_tones[14];
			else if (*argv[i] == '#')
				argv[i] = dtmf_tones[15];
			j += strlen(argv[i]);
		}
		cp = ast_calloc(1, j + 100);
		if (!cp) {
			return DC_ERROR;
		}
		for (i = 1; i < argc; i++) {
			if (i != 1)
				strcat(cp, ",");
			strcat(cp, argv[i]);
		}
		rpt_telemetry(myrpt, PAGE, cp); /* cp is passed to rpt_telem_thread where it is free'd after use */
		return DC_COMPLETE;

	case 49:					/* Disable Incoming connections */
		myrpt->p.s[myrpt->p.sysstate_cur].noincomingconns = 1;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "NOICE");
		return DC_COMPLETE;

	case 50:					/*Enable Incoming connections */
		myrpt->p.s[myrpt->p.sysstate_cur].noincomingconns = 0;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "NOICD");
		return DC_COMPLETE;

	case 51:					/* Enable Sleep Mode */
		rpt_mutex_lock(&myrpt->lock);
		myrpt->sleeptimer = myrpt->p.sleeptime;
		rpt_mutex_unlock(&myrpt->lock);
		myrpt->p.s[myrpt->p.sysstate_cur].sleepena = 1;
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "SLPEN");
		return DC_COMPLETE;

	case 52:					/* Disable Sleep Mode */
		myrpt->p.s[myrpt->p.sysstate_cur].sleepena = 0;
		rpt_mutex_lock(&myrpt->lock);
		myrpt->sleep = myrpt->sleepreq = 0;
		myrpt->sleeptimer = myrpt->p.sleeptime;
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "SLPDS");
		return DC_COMPLETE;

	case 53:					/* Wake up from Sleep Mode */
		rpt_mutex_lock(&myrpt->lock);
		myrpt->sleep = myrpt->sleepreq = 0;
		myrpt->sleeptimer = myrpt->p.sleeptime;
		rpt_mutex_unlock(&myrpt->lock);
		if (myrpt->p.s[myrpt->p.sysstate_cur].sleepena) {
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, ARB_ALPHA, (void *) "AWAKE");
		}
		return DC_COMPLETE;
	case 54:					/* Go to sleep */
		if (myrpt->p.s[myrpt->p.sysstate_cur].sleepena) {
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, ARB_ALPHA, (void *) "SLEEP");
			rpt_mutex_lock(&myrpt->lock);
			myrpt->sleepreq = 1;
			myrpt->sleeptimer = 0;
			rpt_mutex_unlock(&myrpt->lock);
		}
		return DC_COMPLETE;
	case 55:					/* Parrot Once if parrot mode is disabled */
		if (myrpt->p.parrotmode == PARROT_MODE_OFF) {
			rpt_mutex_lock(&myrpt->lock);
			myrpt->parrotonce = 1;
			rpt_mutex_unlock(&myrpt->lock);
		}
		return DC_COMPLETE;
	case 56:					/* RX CTCSS Enable */
		rpt_radio_rx_set_ctcss_decode(myrpt, 0);
		if (CHAN_TECH(myrpt->rxchannel, "radio") || CHAN_TECH(myrpt->rxchannel, "simpleusb")) {
			ast_sendtext(myrpt->rxchannel, "RXCTCSS 1");
		}
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "RXPLENA");
		return DC_COMPLETE;
	case 57:					/* RX CTCSS Disable */
		rpt_radio_rx_set_ctcss_decode(myrpt, 1);
		if (CHAN_TECH(myrpt->rxchannel, "radio") || CHAN_TECH(myrpt->rxchannel, "simpleusb")) {
			ast_sendtext(myrpt->rxchannel, "RXCTCSS 0");
		}
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "RXPLDIS");
		return DC_COMPLETE;
	case 58:					/* TX CTCSS on input only Enable */
		rpt_mutex_lock(&myrpt->lock);
		myrpt->p.itxctcss = 1;
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "TXIPLENA");
		return DC_COMPLETE;
	case 59:					/* TX CTCSS on input only Disable */
		rpt_mutex_lock(&myrpt->lock);
		myrpt->p.itxctcss = 0;
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, ARB_ALPHA, (void *) "TXIPLDIS");
		return DC_COMPLETE;
#ifdef	_MDC_ENCODE_H_
	case 60:					/* play MDC1200 burst */
		if (argc < 3)
			break;
		mdcp = ast_calloc(1, sizeof(struct mdcparams));
		if (!mdcp) {
			return DC_ERROR;
		}
		if (*argv[1] == 'C') {
			if (argc < 5)
				return DC_ERROR;
			mdcp->DestID = (short) strtol(argv[3], NULL, 16);
			mdcp->subcode = (short) strtol(argv[4], NULL, 16);
		}
		ast_copy_string(mdcp->type, argv[1], sizeof(mdcp->type));
		mdcp->UnitID = (short) strtol(argv[2], NULL, 16);
		rpt_telemetry(myrpt, MDC1200, (void *) mdcp); /* mdcp is passed to rpt_telem_thread where it is free'd after use */
		return DC_COMPLETE;
#endif
	case 61:					/* send GPIO change */
	case 62:					/* same, without function complete (quietly) */
		if (argc < 1) {
			break;
		}
		if (!CHAN_TECH(myrpt->rxchannel, "radio") && !CHAN_TECH(myrpt->rxchannel, "simpleusb")) {
			/* ignore if not a USB channel */
			break;
		}
		/* go thru all the specs */
		for (i = 1; i < argc; i++) {
			if (sscanf(argv[i], "%*[Gg]%*[Pp]%*[Ii]%*[oO]" N_FMT(d) "%*[=:]" N_FMT(d), &j, &k) == 2) {
				sprintf(string, "GPIO %d %d", j, k);
				ast_sendtext(myrpt->rxchannel, string);
			} else if (sscanf(argv[i], "%*2[pP]" N_FMT(d) "=" N_FMT(d), &j, &k) == 2) {
				sprintf(string, "PP %d %d", j, k);
				ast_sendtext(myrpt->rxchannel, string);
			} else {
				ast_log(LOG_WARNING, "Invalid command COP %s, %s", argv[0], argv[i]);
			}
		}
		if (myatoi(argv[0]) == 61) {
			rpt_telemetry(myrpt, COMPLETE, NULL);
		}
		return DC_COMPLETE;
	case 63:					/* send pre-configured APRStt notification */
	case 64:					/* same, but quiet */
		if (argc < 2) {
			break;
		}
		
		if (!ast_custom_function_find("APRS_SENDTT")) {
			ast_log(LOG_WARNING, "app_gps is not loaded.  APRStt failed\n");
			return DC_COMPLETE;
		}
		snprintf(func, sizeof(func) -1, "APRS_SENDTT(%s,%c)", !myrpt->p.aprstt ? "general" : myrpt->p.aprstt, 
			argc > 2 ? argv[2][0] : ' ');
		/* execute the APRS_SENDTT function in app_gps*/
		if (!ast_func_write(NULL, func, argv[1])) {
			if (myatoi(argv[0]) == 63) {
				rpt_telemetry(myrpt, ARB_ALPHA, (void *) argv[1]);
			}
		}
		return DC_COMPLETE;
	case 65:					/* send POCSAG page */
		if (argc < 3) {
			break;
		}
		if (!CHAN_TECH(myrpt->rxchannel, "radio") && !CHAN_TECH(myrpt->rxchannel, "simpleusb")) {
			/* ignore if not a USB channel */
			break;
		}
		if (argc > 5) {
			sprintf(string, "PAGE %s %s %s %s %s", argv[1], argv[2], argv[3], argv[4], argv[5]);
		} else {
			sprintf(string, "PAGE %s %s %s", argv[1], argv[2], argv[3]);
		}
		rpt_mutex_lock(&myrpt->lock);
		telem = myrpt->tele.next;
		k = 0;
		while (telem != &myrpt->tele) {
			if (((telem->mode == ID) || (telem->mode == ID1) || (telem->mode == IDTALKOVER)) && (!telem->killed)) {
				if (telem->chan)
					ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);	/* Whoosh! */
				telem->killed = 1;
				myrpt->deferid = 1;
			}
			telem = telem->next;
		}
		rpt_mutex_unlock(&myrpt->lock);
		gettimeofday(&myrpt->paging, NULL);
		ast_sendtext(myrpt->rxchannel, string);
		return DC_COMPLETE;
	}
	return DC_INDETERMINATE;
}

enum rpt_function_response function_meter(
	struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source command_source, struct rpt_link *mylink)
{
	if (myrpt->remote)
		return DC_ERROR;

	ast_debug(1, "meter param = %s, digitbuf = %s\n", (param) ? param : "(null)", digitbuf);

	rpt_telem_select(myrpt, command_source, mylink);
	rpt_telemetry(myrpt, METER, param);
	return DC_COMPLETE;
}

enum rpt_function_response function_userout(struct rpt *myrpt, char *param, char *digitbuf,
	enum rpt_command_source command_source, struct rpt_link *mylink)
{
	if (myrpt->remote)
		return DC_ERROR;

	ast_debug(1, "userout param = %s, digitbuf = %s\n", (param) ? param : "(null)", digitbuf);

	rpt_telem_select(myrpt, command_source, mylink);
	rpt_telemetry(myrpt, USEROUT, param);
	return DC_COMPLETE;
}

enum rpt_function_response function_cmd(struct rpt *myrpt, char *param, char *digitbuf, enum rpt_command_source command_source,
	struct rpt_link *mylink)
{
	char *cp;

	if (myrpt->remote)
		return DC_ERROR;

	ast_debug(1, "cmd param = %s, digitbuf = %s\n", (param) ? param : "(null)", digitbuf);

	if (param) {
		if (*param == '#') {	/* to execute asterisk cli command */
			ast_cli_command(rpt_nullfd(), param + 2);
		} else {
			if (ast_asprintf(&cp, "%s &", param) < 0) {
				return DC_ERROR;
			}
			ast_safe_system(cp);
			ast_free(cp);
		}
	}
	return DC_COMPLETE;
}
