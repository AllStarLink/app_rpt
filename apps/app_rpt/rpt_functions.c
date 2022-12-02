
/*! \file
 *
 * \brief RPT functions
 */

#include "asterisk.h"



#include "asterisk/channel.h"

#include "app_rpt.h"
#include "rpt_lock.h"
#include "rpt_utils.h"
#include "rpt_capabilities.h"
#include "rpt_config.h"
#include "rpt_mdc1200.h"
#include "rpt_channel.h"
#include "rpt_telemetry.h"
#include "rpt_link.h"
#include "rpt_functions.h"

static char remdtmfstr[] = "0123456789*#ABCD";

int function_ilink(struct rpt *myrpt, char *param, char *digits, int command_source, struct rpt_link *mylink)
{
	char *s1, *s2, tmp[300];
	char digitbuf[MAXNODESTR], *strs[MAXLINKLIST];
	char mode, perma;
	struct rpt_link *l;
	int i, r;

	if (!param)
		return DC_ERROR;

	if (myrpt->p.s[myrpt->p.sysstate_cur].txdisable || myrpt->p.s[myrpt->p.sysstate_cur].linkfundisable)
		return DC_ERROR;

	strncpy(digitbuf, digits, MAXNODESTR - 1);

	ast_debug(7, "@@@@ ilink param = %s, digitbuf = %s\n", (param) ? param : "(null)", digitbuf);

	switch (myatoi(param)) {
	case 11:					/* Perm Link off */
	case 1:					/* Link off */
		if (strlen(digitbuf) < 1)
			break;
		if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
			strcpy(digitbuf, myrpt->lastlinknode);
		rpt_mutex_lock(&myrpt->lock);
		l = myrpt->links.next;
		/* try to find this one in queue */
		while (l != &myrpt->links) {
			if (l->name[0] == '0') {
				l = l->next;
				continue;
			}
			/* if found matching string */
			if (!strcmp(l->name, digitbuf))
				break;
			l = l->next;
		}
		if (l != &myrpt->links) {	/* if found */
			struct ast_frame wf;

			/* must use perm command on perm link */
			if ((myatoi(param) < 10) && (l->max_retries > MAX_RETRIES)) {
				rpt_mutex_unlock(&myrpt->lock);
				return DC_COMPLETE;
			}
			ast_copy_string(myrpt->lastlinknode, digitbuf, MAXNODESTR - 1);
			l->retries = l->max_retries + 1;
			l->disced = 1;
			l->hasconnected = 1;
			rpt_mutex_unlock(&myrpt->lock);
			memset(&wf, 0, sizeof(wf));
			wf.frametype = AST_FRAME_TEXT;
			wf.datalen = strlen(DISCSTR) + 1;
			wf.data.ptr = DISCSTR;
			wf.src = "function_ilink:1";
			if (l->chan) {
				if (l->thisconnected)
					ast_write(l->chan, &wf);
				rpt_safe_sleep(myrpt, l->chan, 250);
				ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
			}
			myrpt->linkactivityflag = 1;
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;
		}
		rpt_mutex_unlock(&myrpt->lock);
		break;
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
		mode = (r & 1) ? 1 : 0;
		if ((r == 8) || (r == 18))
			mode = 2;
		r = connect_link(myrpt, digitbuf, mode, perma);
		switch (r) {
		case -2:				/* Attempt to connect to self */
			return DC_COMPLETE;	/* Silent error */

		case 0:
			myrpt->linkactivityflag = 1;
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;

		case 1:
			break;

		case 2:
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, REMALREADY, NULL);
			return DC_COMPLETE;

		default:
			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, CONNFAIL, NULL);
			return DC_COMPLETE;
		}
		break;

	case 4:					/* Enter Command Mode */

		if (strlen(digitbuf) < 1)
			break;
		/* if doesnt allow link cmd, or no links active, return */
		if (myrpt->links.next == &myrpt->links)
			return DC_COMPLETE;
		if ((command_source != SOURCE_RPT) &&
			(command_source != SOURCE_PHONE) &&
			(command_source != SOURCE_ALT) &&
			(command_source != SOURCE_DPHONE) && mylink &&
			(!iswebtransceiver(mylink)) && strcasecmp(ast_channel_tech(mylink->chan)->type, "echolink")
			&& strcasecmp(ast_channel_tech(mylink->chan)->type, "tlb"))
			return DC_COMPLETE;

		/* if already in cmd mode, or selected self, fughetabahtit */
		if ((myrpt->cmdnode[0]) || (!strcmp(myrpt->name, digitbuf))) {

			rpt_telem_select(myrpt, command_source, mylink);
			rpt_telemetry(myrpt, REMALREADY, NULL);
			return DC_COMPLETE;
		}
		if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
			strcpy(digitbuf, myrpt->lastlinknode);
		/* node must at least exist in list */
		if (tlb_node_get(digitbuf, 'n', NULL, NULL, NULL, NULL) != 1) {
			if (digitbuf[0] != '3') {
				if (!node_lookup(myrpt, digitbuf, NULL, 0, 1)) {
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
		l = myrpt->links.next;
		/* loop through all links */
		while (l != &myrpt->links) {
			struct ast_frame wf;
			char c1;
			if ((l->name[0] <= '0') || (l->name[0] > '9')) {	/* Skip any IAXRPT monitoring */
				l = l->next;
				continue;
			}
			if (l->mode == 1)
				c1 = 'X';
			else if (l->mode > 1)
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

			memset(&wf, 0, sizeof(wf));
			wf.frametype = AST_FRAME_TEXT;
			wf.datalen = strlen(DISCSTR) + 1;
			wf.data.ptr = DISCSTR;
			wf.src = "function_ilink:6";
			if (l->chan) {
				if (l->thisconnected)
					ast_write(l->chan, &wf);
				rpt_safe_sleep(myrpt, l->chan, 250);	/* It's dead already, why check the return value? */
				ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
			}
			rpt_mutex_lock(&myrpt->lock);
			l = l->next;
		}
		rpt_mutex_unlock(&myrpt->lock);
		ast_debug(1, "Nodes disconnected: %s\n", myrpt->savednodes);
		rpt_telem_select(myrpt, command_source, mylink);
		rpt_telemetry(myrpt, COMPLETE, NULL);
		return DC_COMPLETE;

	case 10:					/* All RANGER Links Off */
		rpt_mutex_lock(&myrpt->lock);
		myrpt->savednodes[0] = 0;
		l = myrpt->links.next;
		/* loop through all links */
		while (l != &myrpt->links) {
			struct ast_frame wf;
			char c1;
			if ((l->name[0] <= '0') || (l->name[0] > '9')) {	/* Skip any IAXRPT monitoring */
				l = l->next;
				continue;
			}
			/* if RANGER and not permalink */
			if ((l->max_retries <= MAX_RETRIES) && ISRANGER(l->name)) {
				if (l->mode == 1)
					c1 = 'X';
				else if (l->mode > 1)
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
				l->disced = 2;	/* Silently disconnect */
				rpt_mutex_unlock(&myrpt->lock);
				ast_debug(5, "dumping link %s\n",l->name);

				memset(&wf, 0, sizeof(wf));
				wf.frametype = AST_FRAME_TEXT;
				wf.datalen = strlen(DISCSTR) + 1;
				wf.data.ptr = DISCSTR;
				wf.src = "function_ilink:6";
				if (l->chan) {
					if (l->thisconnected)
						ast_write(l->chan, &wf);
					rpt_safe_sleep(myrpt, l->chan, 250);	/* It's dead already, why check the return value? */
					ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
				}
				rpt_mutex_lock(&myrpt->lock);
			}
			l = l->next;
		}
		rpt_mutex_unlock(&myrpt->lock);
		ast_debug(4, "Nodes disconnected: %s\n", myrpt->savednodes);
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
		l = myrpt->links.next;
		/* otherwise, send it to all of em */
		while (l != &myrpt->links) {
			if (l->name[0] == '0') {
				l = l->next;
				continue;
			}
			if (l->chan)
				ast_sendtext(l->chan, tmp);
			l = l->next;
		}
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telemetry(myrpt, COMPLETE, NULL);
		return DC_COMPLETE;

	case 16:					/* Restore links disconnected with "disconnect all links" command */
		strcpy(tmp, myrpt->savednodes);	/* Make a copy */
		finddelim(tmp, strs, MAXLINKLIST);	/* convert into substrings */
		for (i = 0; tmp[0] && strs[i] != NULL && i < MAXLINKLIST; i++) {
			s1 = strs[i];
			if (s1[0] == 'X')
				mode = 1;
			else if (s1[0] == 'L')
				mode = 2;
			else
				mode = 0;
			perma = (s1[1] == 'P') ? 1 : 0;
			connect_link(myrpt, s1 + 2, mode, perma);	/* Try to reconnect */
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

int function_remote(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
	char *s, *s1, *s2;
	int i, j, p, r, ht, k, l, ls2, m, d, offset, offsave, modesave, defmode;
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

		strncpy(tmp, digitbuf, sizeof(tmp) - 1);

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

		strncpy(tmp, digitbuf, sizeof(tmp) - 1);
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
		/* cant set tx tone on RBI (rx tone does both) */
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_RBI))
			return DC_ERROR;
		/* cant set tx tone on ft100 (rx tone does both) */
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_FT100))
			return DC_ERROR;
		/*  eventually for the ic706 instead of just throwing the exception
		   we can check if we are in encode only mode and allow the tx
		   ctcss code to be changed. but at least the warning message is
		   issued for now.
		 */
		if (!strcmp(myrpt->remoterig, REMOTE_RIG_IC706)) {
			ast_log(LOG_WARNING, "Setting IC706 Tx CTCSS Code Not Supported. Set Rx Code for both.\n");
			return DC_ERROR;
		}
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

		strncpy(tmp, digitbuf, sizeof(tmp) - 1);
		/* see if we have at least 1 */
		s = strchr(tmp, '*');
		if (s)
			*s = '.';
		ast_copy_string(savestr, myrpt->txpl, sizeof(savestr) - 1);
		ast_copy_string(myrpt->txpl, tmp, sizeof(myrpt->txpl) - 1);

		if (setrem(myrpt) == -1) {
			ast_copy_string(myrpt->txpl, savestr, sizeof(myrpt->txpl) - 1);
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
		/* cant log in when logged in */
		if (myrpt->loginlevel[0])
			return DC_ERROR;
		*myrpt->loginuser = 0;
		myrpt->loginlevel[0] = 0;
		cp = ast_strdup(param);
		cp1 = strchr(cp, ',');
		ast_mutex_lock(&myrpt->lock);
		if (cp1) {
			*cp1 = 0;
			cp2 = strchr(cp1 + 1, ',');
			if (cp2) {
				*cp2 = 0;
				strncpy(myrpt->loginlevel, cp2 + 1, sizeof(myrpt->loginlevel) - 1);
			}
			ast_copy_string(myrpt->loginuser, cp1 + 1, sizeof(myrpt->loginuser) - 1);
			ast_mutex_unlock(&myrpt->lock);
			if (myrpt->p.archivedir) {
				char str[100];

				sprintf(str, "LOGIN,%s,%s", myrpt->loginuser, myrpt->loginlevel);
				donodelog(myrpt, str);
			}
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
			if (strncasecmp(ast_channel_name(myrpt->txchannel), "DAHDI/pseudo", 12)) {
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
