
#include "asterisk.h"

#include <sys/stat.h>
#include <math.h>
#include <termios.h>

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/cli.h" /* use ast_cli_command */

#include "app_rpt.h"
#include "rpt_config.h"
#include "rpt_utils.h" /* use myatoi */

extern struct rpt rpt_vars[MAXRPTS];

static struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS };

AST_MUTEX_DEFINE_STATIC(nodelookuplock);

int retrieve_astcfgint(struct rpt *myrpt, char *category, char *name, int min, int max, int defl)
{
	char *var;
	int ret;
	char include_zero = 0;

	if (min < 0) {				/* If min is negative, this means include 0 as a valid entry */
		min = -min;
		include_zero = 1;
	}

	var = (char *) ast_variable_retrieve(myrpt->cfg, category, name);
	if (var) {
		ret = myatoi(var);
		if (include_zero && !ret)
			return 0;
		if (ret < min)
			ret = min;
		if (ret > max)
			ret = max;
	} else
		ret = defl;
	return ret;
}

int get_wait_interval(struct rpt *myrpt, int type)
{
	int interval;
	char *wait_times;
	char *wait_times_save;

	wait_times_save = NULL;
	wait_times = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->name, "wait_times");

	if (wait_times) {
		wait_times_save = ast_strdup(wait_times);
		if (!wait_times_save)
			return 0;

	}

	switch (type) {
	case DLY_TELEM:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "telemwait", 500, 5000, 1000);
		else
			interval = 1000;
		break;

	case DLY_ID:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "idwait", 250, 5000, 500);
		else
			interval = 500;
		break;

	case DLY_UNKEY:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "unkeywait", 50, 5000, 1000);
		else
			interval = 1000;
		break;

	case DLY_LINKUNKEY:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "linkunkeywait", 500, 5000, 1000);
		else
			interval = 1000;
		break;

	case DLY_CALLTERM:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "calltermwait", 500, 5000, 1500);
		else
			interval = 1500;
		break;

	case DLY_COMP:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "compwait", 500, 5000, 200);
		else
			interval = 200;
		break;

	case DLY_PARROT:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "parrotwait", 500, 5000, 200);
		else
			interval = 200;
		break;
	case DLY_MDC1200:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "mdc1200wait", 500, 5000, 200);
		else
			interval = 350;
		break;
	default:
		interval = 0;
		break;
	}
	if (wait_times_save)
		ast_free(wait_times_save);
	return interval;
}

int retrieve_memory(struct rpt *myrpt, char *memory)
{
	char tmp[15], *s, *s1, *s2, *val;

	ast_debug(1, "memory=%s block=%s\n", memory, myrpt->p.memory);

	val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.memory, memory);
	if (!val) {
		return -1;
	}
	strncpy(tmp, val, sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = 0;

	s = strchr(tmp, ',');
	if (!s)
		return 1;
	*s++ = 0;
	s1 = strchr(s, ',');
	if (!s1)
		return 1;
	*s1++ = 0;
	s2 = strchr(s1, ',');
	if (!s2)
		s2 = s1;
	else
		*s2++ = 0;
	ast_copy_string(myrpt->freq, tmp, sizeof(myrpt->freq) - 1);
	ast_copy_string(myrpt->rxpl, s, sizeof(myrpt->rxpl) - 1);
	ast_copy_string(myrpt->txpl, s, sizeof(myrpt->rxpl) - 1);
	myrpt->remmode = REM_MODE_FM;
	myrpt->offset = REM_SIMPLEX;
	myrpt->powerlevel = REM_MEDPWR;
	myrpt->txplon = myrpt->rxplon = 0;
	myrpt->splitkhz = 0;
	if (s2 != s1)
		myrpt->splitkhz = atoi(s1);
	while (*s2) {
		switch (*s2++) {
		case 'A':
		case 'a':
			strcpy(myrpt->rxpl, "100.0");
			strcpy(myrpt->txpl, "100.0");
			myrpt->remmode = REM_MODE_AM;
			break;
		case 'B':
		case 'b':
			strcpy(myrpt->rxpl, "100.0");
			strcpy(myrpt->txpl, "100.0");
			myrpt->remmode = REM_MODE_LSB;
			break;
		case 'F':
			myrpt->remmode = REM_MODE_FM;
			break;
		case 'L':
		case 'l':
			myrpt->powerlevel = REM_LOWPWR;
			break;
		case 'H':
		case 'h':
			myrpt->powerlevel = REM_HIPWR;
			break;

		case 'M':
		case 'm':
			myrpt->powerlevel = REM_MEDPWR;
			break;

		case '-':
			myrpt->offset = REM_MINUS;
			break;

		case '+':
			myrpt->offset = REM_PLUS;
			break;

		case 'S':
		case 's':
			myrpt->offset = REM_SIMPLEX;
			break;

		case 'T':
		case 't':
			myrpt->txplon = 1;
			break;

		case 'R':
		case 'r':
			myrpt->rxplon = 1;
			break;

		case 'U':
		case 'u':
			strcpy(myrpt->rxpl, "100.0");
			strcpy(myrpt->txpl, "100.0");
			myrpt->remmode = REM_MODE_USB;
			break;
		default:
			return 1;
		}
	}
	return 0;
}

void local_dtmfkey_helper(struct rpt *myrpt, char c)
{
	int i;
	char *val;

	i = strlen(myrpt->dtmfkeybuf);
	if (i >= (sizeof(myrpt->dtmfkeybuf) - 1))
		return;
	myrpt->dtmfkeybuf[i++] = c;
	myrpt->dtmfkeybuf[i] = 0;
	val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.dtmfkeys, myrpt->dtmfkeybuf);
	if (!val)
		return;
	strncpy(myrpt->curdtmfuser, val, MAXNODESTR - 1);
	myrpt->dtmfkeyed = 1;
	myrpt->dtmfkeybuf[0] = 0;
	return;
}

static int elink_cmd(char *cmd, char *outstr, int outlen)
{
	FILE *tf;

	tf = tmpfile();
	if (!tf)
		return -1;
	ast_debug(1, "elink_cmd sent %s\n", cmd);
	ast_cli_command(fileno(tf), cmd);
	rewind(tf);
	outstr[0] = 0;
	if (!fgets(outstr, outlen, tf)) {
		fclose(tf);
		return 0;
	}
	fclose(tf);
	if (!strlen(outstr))
		return 0;
	if (outstr[strlen(outstr) - 1] == '\n')
		outstr[strlen(outstr) - 1] = 0;
	ast_debug(1, "elink_cmd ret. %s\n", outstr);
	return (strlen(outstr));
}

int elink_db_get(char *lookup, char c, char *nodenum, char *callsign, char *ipaddr)
{
	char str[512], str1[100], *strs[5];
	int n;

	snprintf(str, sizeof(str) - 1, "echolink dbget %c %s", c, lookup);
	n = elink_cmd(str, str1, sizeof(str1));
	if (n < 1)
		return (n);
	n = explode_string(str1, strs, 5, '|', '\"');
	if (n < 3)
		return (0);
	if (nodenum)
		strcpy(nodenum, strs[0]);
	if (callsign)
		strcpy(callsign, strs[1]);
	if (ipaddr)
		strcpy(ipaddr, strs[2]);
	return (1);
}

int tlb_node_get(char *lookup, char c, char *nodenum, char *callsign, char *ipaddr, char *port)
{
	char str[315], str1[100], *strs[6];
	int n;

	snprintf(str, sizeof(str) - 1, "tlb nodeget %c %s", c, lookup);
	n = elink_cmd(str, str1, sizeof(str1));
	if (n < 1)
		return (n);
	n = explode_string(str1, strs, 6, '|', '\"');
	if (n < 4)
		return (0);
	if (nodenum)
		strcpy(nodenum, strs[0]);
	if (callsign)
		strcpy(callsign, strs[1]);
	if (ipaddr)
		strcpy(ipaddr, strs[2]);
	if (port)
		strcpy(port, strs[3]);
	return (1);
}

int node_lookup(struct rpt *myrpt, char *digitbuf, char *str, int strmax, int wilds)
{

	char *val;
	int longestnode, i, j, found;
	struct stat mystat;
	struct ast_config *ourcfg;
	struct ast_variable *vp;

	/* try to look it up locally first */
	val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.nodes, digitbuf);
	if (val) {
		if (str && strmax)
			//snprintf(str,strmax,val,digitbuf);
			//snprintf(str, strmax, "%s%s", val, digitbuf);	/*! \todo 20220111 NA. This may not actually be correct (functionality-wise). Should be verified. For now, it makes the compiler happy. */
			snprintf(str, strmax, "%s", val); // Indeed, generally we only want the first part so for now, ignore the second bit
		return (1);
	}
	if (wilds) {
		vp = ast_variable_browse(myrpt->cfg, myrpt->p.nodes);
		while (vp) {
			if (ast_extension_match(vp->name, digitbuf)) {
				if (str && strmax)
					//snprintf(str,strmax,vp->value,digitbuf);
					//snprintf(str, strmax, "%s%s", vp->value, digitbuf);	// 20220111 NA. This may not actually be correct (functionality-wise). Should be verified. For now, it makes the compiler happy.
					snprintf(str, strmax, "%s", vp->value);
				return (1);
			}
			vp = vp->next;
		}
	}
	ast_mutex_lock(&nodelookuplock);
	if (!myrpt->p.extnodefilesn) {
		ast_mutex_unlock(&nodelookuplock);
		return (0);
	}
	/* determine longest node length again */
	longestnode = 0;
	vp = ast_variable_browse(myrpt->cfg, myrpt->p.nodes);
	while (vp) {
		j = strlen(vp->name);
		if (*vp->name == '_')
			j--;
		if (j > longestnode)
			longestnode = j;
		vp = vp->next;
	}
	found = 0;
	for (i = 0; i < myrpt->p.extnodefilesn; i++) {
		ourcfg = ast_config_load(myrpt->p.extnodefiles[i], config_flags);
		/* if file does not exist */
		if (stat(myrpt->p.extnodefiles[i], &mystat) == -1)
			continue;
		/* if file not there, try next */
		if (!ourcfg)
			continue;
		vp = ast_variable_browse(ourcfg, myrpt->p.extnodes);
		while (vp) {
			j = strlen(vp->name);
			if (*vp->name == '_')
				j--;
			if (j > longestnode)
				longestnode = j;
			vp = vp->next;
		}
		if (!found) {
			val = (char *) ast_variable_retrieve(ourcfg, myrpt->p.extnodes, digitbuf);
			if (val) {
				found = 1;
				if (str && strmax)
					//snprintf(str,strmax,val,digitbuf);
					//snprintf(str, strmax, "%s%s", val, digitbuf);	// 20220111 NA. This may not actually be correct (functionality-wise). Should be verified. For now, it makes the compiler happy.
					snprintf(str, strmax, "%s", val);
			}
		}
		ast_config_destroy(ourcfg);
	}
	myrpt->longestnode = longestnode;
	ast_mutex_unlock(&nodelookuplock);
	return (found);
}

char *forward_node_lookup(struct rpt *myrpt, char *digitbuf, struct ast_config *cfg)
{

	char *val, *efil, *enod, *strs[100];
	int i, n;
	struct stat mystat;
	static struct ast_config *ourcfg;

	val = (char *) ast_variable_retrieve(cfg, "proxy", "extnodefile");
	if (!val)
		val = EXTNODEFILE;
	enod = (char *) ast_variable_retrieve(cfg, "proxy", "extnodes");
	if (!enod)
		enod = EXTNODES;
	ast_mutex_lock(&nodelookuplock);
	efil = ast_strdup(val);
	if (!efil) {
		ast_config_destroy(ourcfg);
		if (ourcfg)
			ast_config_destroy(ourcfg);
		ourcfg = NULL;
		ast_mutex_unlock(&nodelookuplock);
		return NULL;
	}
	n = finddelim(efil, strs, 100);
	if (n < 1) {
		ast_free(efil);
		ast_config_destroy(ourcfg);
		if (ourcfg)
			ast_config_destroy(ourcfg);
		ourcfg = NULL;
		ast_mutex_unlock(&nodelookuplock);
		return NULL;
	}
	if (ourcfg)
		ast_config_destroy(ourcfg);
	val = NULL;
	for (i = 0; i < n; i++) {
		/* if file does not exist */
		if (stat(strs[i], &mystat) == -1)
			continue;
		ourcfg = ast_config_load(strs[i], config_flags);
		/* if file not there, try next */
		if (!ourcfg)
			continue;
		if (!val)
			val = (char *) ast_variable_retrieve(ourcfg, enod, digitbuf);
	}
	if (!val) {
		if (ourcfg)
			ast_config_destroy(ourcfg);
		ourcfg = NULL;
	}
	ast_mutex_unlock(&nodelookuplock);
	ast_free(efil);
	return (val);
}

void load_rpt_vars(int n, int init)
{
	char *this, *val;
	int i, j, longestnode;
	struct ast_variable *vp;
	struct ast_config *cfg;
	char *strs[100];
	char s1[256];
	static char *cs_keywords[] =
		{ "rptena", "rptdis", "apena", "apdis", "lnkena", "lnkdis", "totena", "totdis", "skena", "skdis",
		"ufena", "ufdis", "atena", "atdis", "noice", "noicd", "slpen", "slpds", NULL
	};

	ast_verb(3, "%s config for repeater %s\n", (init) ? "Loading initial" : "Re-Loading", rpt_vars[n].name);
	ast_mutex_lock(&rpt_vars[n].lock);
	if (rpt_vars[n].cfg)
		ast_config_destroy(rpt_vars[n].cfg);
	cfg = ast_config_load("rpt.conf", config_flags);
	if (!cfg) {
		ast_mutex_unlock(&rpt_vars[n].lock);
		ast_log(LOG_NOTICE, "Unable to open radio repeater configuration rpt.conf.  Radio Repeater disabled.\n");
		pthread_exit(NULL);
	}
	rpt_vars[n].cfg = cfg;
	this = rpt_vars[n].name;
	memset(&rpt_vars[n].p, 0, sizeof(rpt_vars[n].p));
	if (init) {
		char *cp;
		int savearea = (char *) &rpt_vars[n].p - (char *) &rpt_vars[n];

		cp = (char *) &rpt_vars[n].p;
		memset(cp + sizeof(rpt_vars[n].p), 0, sizeof(rpt_vars[n]) - (sizeof(rpt_vars[n].p) + savearea));
		rpt_vars[n].tele.next = &rpt_vars[n].tele;
		rpt_vars[n].tele.prev = &rpt_vars[n].tele;
		rpt_vars[n].rpt_thread = AST_PTHREADT_NULL;
		rpt_vars[n].tailmessagen = 0;
	}
#ifdef	__RPT_NOTCH
	/* zot out filters stuff */
	memset(&rpt_vars[n].filters, 0, sizeof(rpt_vars[n].filters));
#endif
	val = (char *) ast_variable_retrieve(cfg, this, "context");
	if (val)
		rpt_vars[n].p.ourcontext = val;
	else
		rpt_vars[n].p.ourcontext = this;
	val = (char *) ast_variable_retrieve(cfg, this, "callerid");
	if (val)
		rpt_vars[n].p.ourcallerid = val;
	val = (char *) ast_variable_retrieve(cfg, this, "accountcode");
	if (val)
		rpt_vars[n].p.acctcode = val;
	val = (char *) ast_variable_retrieve(cfg, this, "idrecording");
	if (val)
		rpt_vars[n].p.ident = val;
	val = (char *) ast_variable_retrieve(cfg, this, "hangtime");
	if (val)
		rpt_vars[n].p.hangtime = atoi(val);
	else
		rpt_vars[n].p.hangtime = (ISRANGER(rpt_vars[n].name) ? 1 : HANGTIME);
	if (rpt_vars[n].p.hangtime < 1)
		rpt_vars[n].p.hangtime = 1;
	val = (char *) ast_variable_retrieve(cfg, this, "althangtime");
	if (val)
		rpt_vars[n].p.althangtime = atoi(val);
	else
		rpt_vars[n].p.althangtime = (ISRANGER(rpt_vars[n].name) ? 1 : HANGTIME);
	if (rpt_vars[n].p.althangtime < 1)
		rpt_vars[n].p.althangtime = 1;
	val = (char *) ast_variable_retrieve(cfg, this, "totime");
	if (val)
		rpt_vars[n].p.totime = atoi(val);
	else
		rpt_vars[n].p.totime = (ISRANGER(rpt_vars[n].name) ? 9999999 : TOTIME);
	val = (char *) ast_variable_retrieve(cfg, this, "voxtimeout");
	if (val)
		rpt_vars[n].p.voxtimeout_ms = atoi(val);
	else
		rpt_vars[n].p.voxtimeout_ms = VOX_TIMEOUT_MS;
	val = (char *) ast_variable_retrieve(cfg, this, "voxrecover");
	if (val)
		rpt_vars[n].p.voxrecover_ms = atoi(val);
	else
		rpt_vars[n].p.voxrecover_ms = VOX_RECOVER_MS;
	val = (char *) ast_variable_retrieve(cfg, this, "simplexpatchdelay");
	if (val)
		rpt_vars[n].p.simplexpatchdelay = atoi(val);
	else
		rpt_vars[n].p.simplexpatchdelay = SIMPLEX_PATCH_DELAY;
	val = (char *) ast_variable_retrieve(cfg, this, "simplexphonedelay");
	if (val)
		rpt_vars[n].p.simplexphonedelay = atoi(val);
	else
		rpt_vars[n].p.simplexphonedelay = SIMPLEX_PHONE_DELAY;
	val = (char *) ast_variable_retrieve(cfg, this, "statpost_program");
	if (val)
		rpt_vars[n].p.statpost_program = val;
	else
		rpt_vars[n].p.statpost_program = STATPOST_PROGRAM;
	rpt_vars[n].p.statpost_url = (char *) ast_variable_retrieve(cfg, this, "statpost_url");
	rpt_vars[n].p.tailmessagetime = retrieve_astcfgint(&rpt_vars[n], this, "tailmessagetime", 0, 200000000, 0);
	rpt_vars[n].p.tailsquashedtime = retrieve_astcfgint(&rpt_vars[n], this, "tailsquashedtime", 0, 200000000, 0);
	rpt_vars[n].p.duplex = retrieve_astcfgint(&rpt_vars[n], this, "duplex", 0, 4, (ISRANGER(rpt_vars[n].name) ? 0 : 2));
	rpt_vars[n].p.idtime = retrieve_astcfgint(&rpt_vars[n], this, "idtime", -60000, 2400000, IDTIME);	/* Enforce a min max including zero */
	rpt_vars[n].p.politeid = retrieve_astcfgint(&rpt_vars[n], this, "politeid", 30000, 300000, POLITEID);	/* Enforce a min max */
	j = retrieve_astcfgint(&rpt_vars[n], this, "elke", 0, 40000000, 0);
	rpt_vars[n].p.elke = j * 1210;
	val = (char *) ast_variable_retrieve(cfg, this, "tonezone");
	if (val)
		rpt_vars[n].p.tonezone = val;
	rpt_vars[n].p.tailmessages[0] = 0;
	rpt_vars[n].p.tailmessagemax = 0;
	val = (char *) ast_variable_retrieve(cfg, this, "tailmessagelist");
	if (val)
		rpt_vars[n].p.tailmessagemax = finddelim(val, rpt_vars[n].p.tailmessages, 500);
	rpt_vars[n].p.aprstt = (char *) ast_variable_retrieve(cfg, this, "aprstt");
	val = (char *) ast_variable_retrieve(cfg, this, "memory");
	if (!val)
		val = MEMORY;
	rpt_vars[n].p.memory = val;
	val = (char *) ast_variable_retrieve(cfg, this, "morse");
	if (!val)
		val = MORSE;
	rpt_vars[n].p.morse = val;
	val = (char *) ast_variable_retrieve(cfg, this, "telemetry");
	if (!val)
		val = TELEMETRY;
	rpt_vars[n].p.telemetry = val;
	val = (char *) ast_variable_retrieve(cfg, this, "macro");
	if (!val)
		val = MACRO;
	rpt_vars[n].p.macro = val;
	val = (char *) ast_variable_retrieve(cfg, this, "tonemacro");
	if (!val)
		val = TONEMACRO;
	rpt_vars[n].p.tonemacro = val;
	val = (char *) ast_variable_retrieve(cfg, this, "mdcmacro");
	if (!val)
		val = MDCMACRO;
	rpt_vars[n].p.mdcmacro = val;
	val = (char *) ast_variable_retrieve(cfg, this, "startup_macro");
	if (val)
		rpt_vars[n].p.startupmacro = val;
	val = (char *) ast_variable_retrieve(cfg, this, "iobase");
	/* do not use atoi() here, we need to be able to have
	   the input specified in hex or decimal so we use
	   sscanf with a %i */
	if ((!val) || (sscanf(val, "%i", &rpt_vars[n].p.iobase) != 1))
		rpt_vars[n].p.iobase = DEFAULT_IOBASE;
	val = (char *) ast_variable_retrieve(cfg, this, "ioport");
	rpt_vars[n].p.ioport = val;
	val = (char *) ast_variable_retrieve(cfg, this, "functions");
	if (!val) {
		val = FUNCTIONS;
		rpt_vars[n].p.simple = 1;
	}
	rpt_vars[n].p.functions = val;
	val = (char *) ast_variable_retrieve(cfg, this, "link_functions");
	if (val)
		rpt_vars[n].p.link_functions = val;
	else
		rpt_vars[n].p.link_functions = rpt_vars[n].p.functions;
	val = (char *) ast_variable_retrieve(cfg, this, "phone_functions");
	if (val)
		rpt_vars[n].p.phone_functions = val;
	else if (ISRANGER(rpt_vars[n].name))
		rpt_vars[n].p.phone_functions = rpt_vars[n].p.functions;
	val = (char *) ast_variable_retrieve(cfg, this, "dphone_functions");
	if (val)
		rpt_vars[n].p.dphone_functions = val;
	else if (ISRANGER(rpt_vars[n].name))
		rpt_vars[n].p.dphone_functions = rpt_vars[n].p.functions;
	val = (char *) ast_variable_retrieve(cfg, this, "alt_functions");
	if (val)
		rpt_vars[n].p.alt_functions = val;
	val = (char *) ast_variable_retrieve(cfg, this, "funcchar");
	if (!val)
		rpt_vars[n].p.funcchar = FUNCCHAR;
	else
		rpt_vars[n].p.funcchar = *val;
	val = (char *) ast_variable_retrieve(cfg, this, "endchar");
	if (!val)
		rpt_vars[n].p.endchar = ENDCHAR;
	else
		rpt_vars[n].p.endchar = *val;
	val = (char *) ast_variable_retrieve(cfg, this, "nobusyout");
	if (val)
		rpt_vars[n].p.nobusyout = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg, this, "notelemtx");
	if (val)
		rpt_vars[n].p.notelemtx = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg, this, "propagate_dtmf");
	if (val)
		rpt_vars[n].p.propagate_dtmf = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg, this, "propagate_phonedtmf");
	if (val)
		rpt_vars[n].p.propagate_phonedtmf = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg, this, "linktolink");
	if (val)
		rpt_vars[n].p.linktolink = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg, this, "nodes");
	if (!val)
		val = NODES;
	rpt_vars[n].p.nodes = val;
	val = (char *) ast_variable_retrieve(cfg, this, "extnodes");
	if (!val)
		val = EXTNODES;
	rpt_vars[n].p.extnodes = val;
	val = (char *) ast_variable_retrieve(cfg, this, "extnodefile");
	if (!val)
		val = EXTNODEFILE;
	rpt_vars[n].p.extnodefilesn = explode_string(val, rpt_vars[n].p.extnodefiles, MAX_EXTNODEFILES, ',', 0);
	val = (char *) ast_variable_retrieve(cfg, this, "locallinknodes");
	if (val)
		rpt_vars[n].p.locallinknodesn =
			explode_string(ast_strdup(val), rpt_vars[n].p.locallinknodes, MAX_LOCALLINKNODES, ',', 0);
	val = (char *) ast_variable_retrieve(cfg, this, "lconn");
	if (val)
		rpt_vars[n].p.nlconn = explode_string(strupr(ast_strdup(val)), rpt_vars[n].p.lconn, MAX_LSTUFF, ',', 0);
	val = (char *) ast_variable_retrieve(cfg, this, "ldisc");
	if (val)
		rpt_vars[n].p.nldisc = explode_string(strupr(ast_strdup(val)), rpt_vars[n].p.ldisc, MAX_LSTUFF, ',', 0);
	val = (char *) ast_variable_retrieve(cfg, this, "patchconnect");
	rpt_vars[n].p.patchconnect = val;
	val = (char *) ast_variable_retrieve(cfg, this, "archivedir");
	if (val)
		rpt_vars[n].p.archivedir = val;
	val = (char *) ast_variable_retrieve(cfg, this, "authlevel");
	if (val)
		rpt_vars[n].p.authlevel = atoi(val);
	else
		rpt_vars[n].p.authlevel = 0;
	val = (char *) ast_variable_retrieve(cfg, this, "parrot");
	if (val)
		rpt_vars[n].p.parrotmode = (ast_true(val)) ? 2 : 0;
	else
		rpt_vars[n].p.parrotmode = 0;
	val = (char *) ast_variable_retrieve(cfg, this, "parrottime");
	if (val)
		rpt_vars[n].p.parrottime = atoi(val);
	else
		rpt_vars[n].p.parrottime = PARROTTIME;
	val = (char *) ast_variable_retrieve(cfg, this, "rptnode");
	rpt_vars[n].p.rptnode = val;
	val = (char *) ast_variable_retrieve(cfg, this, "mars");
	if (val)
		rpt_vars[n].p.remote_mars = atoi(val);
	else
		rpt_vars[n].p.remote_mars = 0;
	val = (char *) ast_variable_retrieve(cfg, this, "monminblocks");
	if (val)
		rpt_vars[n].p.monminblocks = atol(val);
	else
		rpt_vars[n].p.monminblocks = DEFAULT_MONITOR_MIN_DISK_BLOCKS;
	val = (char *) ast_variable_retrieve(cfg, this, "remote_inact_timeout");
	if (val)
		rpt_vars[n].p.remoteinacttimeout = atoi(val);
	else
		rpt_vars[n].p.remoteinacttimeout = DEFAULT_REMOTE_INACT_TIMEOUT;
	val = (char *) ast_variable_retrieve(cfg, this, "civaddr");
	if (val)
		rpt_vars[n].p.civaddr = atoi(val);
	else
		rpt_vars[n].p.civaddr = DEFAULT_CIV_ADDR;
	val = (char *) ast_variable_retrieve(cfg, this, "remote_timeout");
	if (val)
		rpt_vars[n].p.remotetimeout = atoi(val);
	else
		rpt_vars[n].p.remotetimeout = DEFAULT_REMOTE_TIMEOUT;
	val = (char *) ast_variable_retrieve(cfg, this, "remote_timeout_warning");
	if (val)
		rpt_vars[n].p.remotetimeoutwarning = atoi(val);
	else
		rpt_vars[n].p.remotetimeoutwarning = DEFAULT_REMOTE_TIMEOUT_WARNING;
	val = (char *) ast_variable_retrieve(cfg, this, "remote_timeout_warning_freq");
	if (val)
		rpt_vars[n].p.remotetimeoutwarningfreq = atoi(val);
	else
		rpt_vars[n].p.remotetimeoutwarningfreq = DEFAULT_REMOTE_TIMEOUT_WARNING_FREQ;
	val = (char *) ast_variable_retrieve(cfg, this, "erxgain");
	if (!val)
		val = DEFAULT_ERXGAIN;
	rpt_vars[n].p.erxgain = pow(10.0, atof(val) / 20.0);
	val = (char *) ast_variable_retrieve(cfg, this, "etxgain");
	if (!val)
		val = DEFAULT_ETXGAIN;
	rpt_vars[n].p.etxgain = pow(10.0, atof(val) / 20.0);
	val = (char *) ast_variable_retrieve(cfg, this, "eannmode");
	if (val)
		rpt_vars[n].p.eannmode = atoi(val);
	else
		rpt_vars[n].p.eannmode = DEFAULT_EANNMODE;
	if (rpt_vars[n].p.eannmode < 0)
		rpt_vars[n].p.eannmode = 0;
	if (rpt_vars[n].p.eannmode > 3)
		rpt_vars[n].p.eannmode = 3;
	val = (char *) ast_variable_retrieve(cfg, this, "trxgain");
	if (!val)
		val = DEFAULT_TRXGAIN;
	rpt_vars[n].p.trxgain = pow(10.0, atof(val) / 20.0);
	val = (char *) ast_variable_retrieve(cfg, this, "ttxgain");
	if (!val)
		val = DEFAULT_TTXGAIN;
	rpt_vars[n].p.ttxgain = pow(10.0, atof(val) / 20.0);
	val = (char *) ast_variable_retrieve(cfg, this, "tannmode");
	if (val)
		rpt_vars[n].p.tannmode = atoi(val);
	else
		rpt_vars[n].p.tannmode = DEFAULT_TANNMODE;
	if (rpt_vars[n].p.tannmode < 1)
		rpt_vars[n].p.tannmode = 1;
	if (rpt_vars[n].p.tannmode > 3)
		rpt_vars[n].p.tannmode = 3;
	val = (char *) ast_variable_retrieve(cfg, this, "linkmongain");
	if (!val)
		val = DEFAULT_LINKMONGAIN;
	rpt_vars[n].p.linkmongain = pow(10.0, atof(val) / 20.0);
	val = (char *) ast_variable_retrieve(cfg, this, "discpgm");
	rpt_vars[n].p.discpgm = val;
	val = (char *) ast_variable_retrieve(cfg, this, "connpgm");
	rpt_vars[n].p.connpgm = val;
	val = (char *) ast_variable_retrieve(cfg, this, "mdclog");
	rpt_vars[n].p.mdclog = val;
	val = (char *) ast_variable_retrieve(cfg, this, "lnkactenable");
	if (val)
		rpt_vars[n].p.lnkactenable = ast_true(val);
	else
		rpt_vars[n].p.lnkactenable = 0;
	rpt_vars[n].p.lnkacttime = retrieve_astcfgint(&rpt_vars[n], this, "lnkacttime", -120, 90000, 0);	/* Enforce a min max including zero */
	val = (char *) ast_variable_retrieve(cfg, this, "lnkactmacro");
	rpt_vars[n].p.lnkactmacro = val;
	val = (char *) ast_variable_retrieve(cfg, this, "lnkacttimerwarn");
	rpt_vars[n].p.lnkacttimerwarn = val;
	val = (char *) ast_variable_retrieve(cfg, this, "nolocallinkct");
	rpt_vars[n].p.nolocallinkct = ast_true(val);
	rpt_vars[n].p.rptinacttime = retrieve_astcfgint(&rpt_vars[n], this, "rptinacttime", -120, 90000, 0);	/* Enforce a min max including zero */
	val = (char *) ast_variable_retrieve(cfg, this, "rptinactmacro");
	rpt_vars[n].p.rptinactmacro = val;
	val = (char *) ast_variable_retrieve(cfg, this, "nounkeyct");
	rpt_vars[n].p.nounkeyct = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg, this, "holdofftelem");
	rpt_vars[n].p.holdofftelem = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg, this, "beaconing");
	rpt_vars[n].p.beaconing = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg, this, "rxburstfreq");
	if (val)
		rpt_vars[n].p.rxburstfreq = atoi(val);
	else
		rpt_vars[n].p.rxburstfreq = 0;
	val = (char *) ast_variable_retrieve(cfg, this, "rxbursttime");
	if (val)
		rpt_vars[n].p.rxbursttime = atoi(val);
	else
		rpt_vars[n].p.rxbursttime = DEFAULT_RXBURST_TIME;
	val = (char *) ast_variable_retrieve(cfg, this, "rxburstthreshold");
	if (val)
		rpt_vars[n].p.rxburstthreshold = atoi(val);
	else
		rpt_vars[n].p.rxburstthreshold = DEFAULT_RXBURST_THRESHOLD;
	val = (char *) ast_variable_retrieve(cfg, this, "litztime");
	if (val)
		rpt_vars[n].p.litztime = atoi(val);
	else
		rpt_vars[n].p.litztime = DEFAULT_LITZ_TIME;
	val = (char *) ast_variable_retrieve(cfg, this, "litzchar");
	if (!val)
		val = DEFAULT_LITZ_CHAR;
	rpt_vars[n].p.litzchar = val;
	val = (char *) ast_variable_retrieve(cfg, this, "litzcmd");
	rpt_vars[n].p.litzcmd = val;
	val = (char *) ast_variable_retrieve(cfg, this, "itxctcss");
	if (val)
		rpt_vars[n].p.itxctcss = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg, this, "gpsfeet");
	if (val)
		rpt_vars[n].p.gpsfeet = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg, this, "split2m");
	if (val)
		rpt_vars[n].p.default_split_2m = atoi(val);
	else
		rpt_vars[n].p.default_split_2m = DEFAULT_SPLIT_2M;
	val = (char *) ast_variable_retrieve(cfg, this, "split70cm");
	if (val)
		rpt_vars[n].p.default_split_70cm = atoi(val);
	else
		rpt_vars[n].p.default_split_70cm = DEFAULT_SPLIT_70CM;
	val = (char *) ast_variable_retrieve(cfg, this, "dtmfkey");
	if (val)
		rpt_vars[n].p.dtmfkey = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg, this, "dtmfkeys");
	if (!val)
		val = DTMFKEYS;
	rpt_vars[n].p.dtmfkeys = val;
	val = (char *) ast_variable_retrieve(cfg, this, "outstreamcmd");
	rpt_vars[n].p.outstreamcmd = val;
	val = (char *) ast_variable_retrieve(cfg, this, "eloutbound");
	rpt_vars[n].p.eloutbound = val;
	val = (char *) ast_variable_retrieve(cfg, this, "events");
	if (!val)
		val = "events";
	rpt_vars[n].p.events = val;
	val = (char *) ast_variable_retrieve(cfg, this, "timezone");
	rpt_vars[n].p.timezone = val;

#ifdef	__RPT_NOTCH
	val = (char *) ast_variable_retrieve(cfg, this, "rxnotch");
	if (val) {
		i = finddelim(val, strs, MAXFILTERS * 2);
		i &= ~1;				/* force an even number, rounded down */
		if (i >= 2)
			for (j = 0; j < i; j += 2) {
				rpt_mknotch(atof(strs[j]), atof(strs[j + 1]),
							&rpt_vars[n].filters[j >> 1].gain,
							&rpt_vars[n].filters[j >> 1].const0, &rpt_vars[n].filters[j >> 1].const1,
							&rpt_vars[n].filters[j >> 1].const2);
				sprintf(rpt_vars[n].filters[j >> 1].desc, "%s Hz, BW = %s", strs[j], strs[j + 1]);
			}

	}
#endif
	val = (char *) ast_variable_retrieve(cfg, this, "votertype");
	if (!val)
		val = "0";
	rpt_vars[n].p.votertype = atoi(val);

	val = (char *) ast_variable_retrieve(cfg, this, "votermode");
	if (!val)
		val = "0";
	rpt_vars[n].p.votermode = atoi(val);

	val = (char *) ast_variable_retrieve(cfg, this, "votermargin");
	if (!val)
		val = "10";
	rpt_vars[n].p.votermargin = atoi(val);

	val = (char *) ast_variable_retrieve(cfg, this, "telemnomdb");
	if (!val)
		val = "0";
	rpt_vars[n].p.telemnomgain = pow(10.0, atof(val) / 20.0);
	val = (char *) ast_variable_retrieve(cfg, this, "telemduckdb");
	if (!val)
		val = DEFAULT_TELEMDUCKDB;
	rpt_vars[n].p.telemduckgain = pow(10.0, atof(val) / 20.0);
	val = (char *) ast_variable_retrieve(cfg, this, "telemdefault");
	if (val)
		rpt_vars[n].p.telemdefault = atoi(val);
	else
		rpt_vars[n].p.telemdefault = DEFAULT_RPT_TELEMDEFAULT;
	val = (char *) ast_variable_retrieve(cfg, this, "telemdynamic");
	if (val)
		rpt_vars[n].p.telemdynamic = ast_true(val);
	else
		rpt_vars[n].p.telemdynamic = DEFAULT_RPT_TELEMDYNAMIC;
	if (!rpt_vars[n].p.telemdefault)
		rpt_vars[n].telemmode = 0;
	else if (rpt_vars[n].p.telemdefault == 2)
		rpt_vars[n].telemmode = 1;
	else
		rpt_vars[n].telemmode = 0x7fffffff;

	val = (char *) ast_variable_retrieve(cfg, this, "guilinkdefault");
	if (val)
		rpt_vars[n].p.linkmode[LINKMODE_GUI] = atoi(val);
	else
		rpt_vars[n].p.linkmode[LINKMODE_GUI] = DEFAULT_GUI_LINK_MODE;
	val = (char *) ast_variable_retrieve(cfg, this, "guilinkdynamic");
	if (val)
		rpt_vars[n].p.linkmodedynamic[LINKMODE_GUI] = ast_true(val);
	else
		rpt_vars[n].p.linkmodedynamic[LINKMODE_GUI] = DEFAULT_GUI_LINK_MODE_DYNAMIC;

	val = (char *) ast_variable_retrieve(cfg, this, "phonelinkdefault");
	if (val)
		rpt_vars[n].p.linkmode[LINKMODE_PHONE] = atoi(val);
	else
		rpt_vars[n].p.linkmode[LINKMODE_PHONE] = DEFAULT_PHONE_LINK_MODE;
	val = (char *) ast_variable_retrieve(cfg, this, "phonelinkdynamic");
	if (val)
		rpt_vars[n].p.linkmodedynamic[LINKMODE_PHONE] = ast_true(val);
	else
		rpt_vars[n].p.linkmodedynamic[LINKMODE_PHONE] = DEFAULT_PHONE_LINK_MODE_DYNAMIC;

	val = (char *) ast_variable_retrieve(cfg, this, "echolinkdefault");
	if (val)
		rpt_vars[n].p.linkmode[LINKMODE_ECHOLINK] = atoi(val);
	else
		rpt_vars[n].p.linkmode[LINKMODE_ECHOLINK] = DEFAULT_ECHOLINK_LINK_MODE;
	val = (char *) ast_variable_retrieve(cfg, this, "echolinkdynamic");
	if (val)
		rpt_vars[n].p.linkmodedynamic[LINKMODE_ECHOLINK] = ast_true(val);
	else
		rpt_vars[n].p.linkmodedynamic[LINKMODE_ECHOLINK] = DEFAULT_ECHOLINK_LINK_MODE_DYNAMIC;

	val = (char *) ast_variable_retrieve(cfg, this, "tlbdefault");
	if (val)
		rpt_vars[n].p.linkmode[LINKMODE_TLB] = atoi(val);
	else
		rpt_vars[n].p.linkmode[LINKMODE_TLB] = DEFAULT_TLB_LINK_MODE;
	val = (char *) ast_variable_retrieve(cfg, this, "tlbdynamic");
	if (val)
		rpt_vars[n].p.linkmodedynamic[LINKMODE_TLB] = ast_true(val);
	else
		rpt_vars[n].p.linkmodedynamic[LINKMODE_TLB] = DEFAULT_TLB_LINK_MODE_DYNAMIC;

	val = (char *) ast_variable_retrieve(cfg, this, "locallist");
	if (val) {
		memset(rpt_vars[n].p.locallist, 0, sizeof(rpt_vars[n].p.locallist));
		rpt_vars[n].p.nlocallist = finddelim(val, rpt_vars[n].p.locallist, 16);
	}

	val = (char *) ast_variable_retrieve(cfg, this, "ctgroup");
	if (val) {
		strncpy(rpt_vars[n].p.ctgroup, val, sizeof(rpt_vars[n].p.ctgroup) - 1);
	} else
		strcpy(rpt_vars[n].p.ctgroup, "0");

	val = (char *) ast_variable_retrieve(cfg, this, "inxlat");
	if (val) {
		memset(&rpt_vars[n].p.inxlat, 0, sizeof(struct rpt_xlat));
		i = finddelim(val, strs, 3);
		if (i)
			strncpy(rpt_vars[n].p.inxlat.funccharseq, strs[0], MAXXLAT - 1);
		if (i > 1)
			strncpy(rpt_vars[n].p.inxlat.endcharseq, strs[1], MAXXLAT - 1);
		if (i > 2)
			strncpy(rpt_vars[n].p.inxlat.passchars, strs[2], MAXXLAT - 1);
		if (i > 3)
			rpt_vars[n].p.dopfxtone = ast_true(strs[3]);
	}
	val = (char *) ast_variable_retrieve(cfg, this, "outxlat");
	if (val) {
		memset(&rpt_vars[n].p.outxlat, 0, sizeof(struct rpt_xlat));
		i = finddelim(val, strs, 3);
		if (i)
			strncpy(rpt_vars[n].p.outxlat.funccharseq, strs[0], MAXXLAT - 1);
		if (i > 1)
			strncpy(rpt_vars[n].p.outxlat.endcharseq, strs[1], MAXXLAT - 1);
		if (i > 2)
			strncpy(rpt_vars[n].p.outxlat.passchars, strs[2], MAXXLAT - 1);
	}
	val = (char *) ast_variable_retrieve(cfg, this, "sleeptime");
	if (val)
		rpt_vars[n].p.sleeptime = atoi(val);
	else
		rpt_vars[n].p.sleeptime = SLEEPTIME;
	/* retrieve the stanza name for the control states if there is one */
	val = (char *) ast_variable_retrieve(cfg, this, "controlstates");
	rpt_vars[n].p.csstanzaname = val;

	/* retrieve the stanza name for the scheduler if there is one */
	val = (char *) ast_variable_retrieve(cfg, this, "scheduler");
	rpt_vars[n].p.skedstanzaname = val;

	/* retrieve the stanza name for the txlimits */
	val = (char *) ast_variable_retrieve(cfg, this, "txlimits");
	rpt_vars[n].p.txlimitsstanzaname = val;

	rpt_vars[n].p.iospeed = B9600;
	if (!strcasecmp(rpt_vars[n].remoterig, REMOTE_RIG_FT950))
		rpt_vars[n].p.iospeed = B38400;
	if (!strcasecmp(rpt_vars[n].remoterig, REMOTE_RIG_FT100))
		rpt_vars[n].p.iospeed = B4800;
	if (!strcasecmp(rpt_vars[n].remoterig, REMOTE_RIG_FT897))
		rpt_vars[n].p.iospeed = B4800;
	val = (char *) ast_variable_retrieve(cfg, this, "dias");
	if (val)
		rpt_vars[n].p.dias = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg, this, "dusbabek");
	if (val)
		rpt_vars[n].p.dusbabek = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg, this, "iospeed");
	if (val) {
		switch (atoi(val)) {
		case 2400:
			rpt_vars[n].p.iospeed = B2400;
			break;
		case 4800:
			rpt_vars[n].p.iospeed = B4800;
			break;
		case 19200:
			rpt_vars[n].p.iospeed = B19200;
			break;
		case 38400:
			rpt_vars[n].p.iospeed = B38400;
			break;
		case 57600:
			rpt_vars[n].p.iospeed = B57600;
			break;
		default:
			ast_log(LOG_ERROR, "%s is not valid baud rate for iospeed\n", val);
			break;
		}
	}

	longestnode = 0;

	vp = ast_variable_browse(cfg, rpt_vars[n].p.nodes);

	while (vp) {
		j = strlen(vp->name);
		if (j > longestnode)
			longestnode = j;
		vp = vp->next;
	}

	rpt_vars[n].longestnode = longestnode;

	/*
	 * For this repeater, Determine the length of the longest function 
	 */
	rpt_vars[n].longestfunc = 0;
	vp = ast_variable_browse(cfg, rpt_vars[n].p.functions);
	while (vp) {
		j = strlen(vp->name);
		if (j > rpt_vars[n].longestfunc)
			rpt_vars[n].longestfunc = j;
		vp = vp->next;
	}
	/*
	 * For this repeater, Determine the length of the longest function 
	 */
	rpt_vars[n].link_longestfunc = 0;
	vp = ast_variable_browse(cfg, rpt_vars[n].p.link_functions);
	while (vp) {
		j = strlen(vp->name);
		if (j > rpt_vars[n].link_longestfunc)
			rpt_vars[n].link_longestfunc = j;
		vp = vp->next;
	}
	rpt_vars[n].phone_longestfunc = 0;
	if (rpt_vars[n].p.phone_functions) {
		vp = ast_variable_browse(cfg, rpt_vars[n].p.phone_functions);
		while (vp) {
			j = strlen(vp->name);
			if (j > rpt_vars[n].phone_longestfunc)
				rpt_vars[n].phone_longestfunc = j;
			vp = vp->next;
		}
	}
	rpt_vars[n].dphone_longestfunc = 0;
	if (rpt_vars[n].p.dphone_functions) {
		vp = ast_variable_browse(cfg, rpt_vars[n].p.dphone_functions);
		while (vp) {
			j = strlen(vp->name);
			if (j > rpt_vars[n].dphone_longestfunc)
				rpt_vars[n].dphone_longestfunc = j;
			vp = vp->next;
		}
	}
	rpt_vars[n].alt_longestfunc = 0;
	if (rpt_vars[n].p.alt_functions) {
		vp = ast_variable_browse(cfg, rpt_vars[n].p.alt_functions);
		while (vp) {
			j = strlen(vp->name);
			if (j > rpt_vars[n].alt_longestfunc)
				rpt_vars[n].alt_longestfunc = j;
			vp = vp->next;
		}
	}
	rpt_vars[n].macro_longest = 1;
	vp = ast_variable_browse(cfg, rpt_vars[n].p.macro);
	while (vp) {
		j = strlen(vp->name);
		if (j > rpt_vars[n].macro_longest)
			rpt_vars[n].macro_longest = j;
		vp = vp->next;
	}

	/* Browse for control states */
	if (rpt_vars[n].p.csstanzaname)
		vp = ast_variable_browse(cfg, rpt_vars[n].p.csstanzaname);
	else
		vp = NULL;
	for (i = 0; vp && (i < MAX_SYSSTATES); i++) {	/* Iterate over the number of control state lines in the stanza */
		int k, nukw, statenum;
		statenum = atoi(vp->name);
		strncpy(s1, vp->value, 255);
		s1[255] = 0;
		nukw = finddelim(s1, strs, 32);

		for (k = 0; k < nukw; k++) {	/* for each user specified keyword */
			for (j = 0; cs_keywords[j] != NULL; j++) {	/* try to match to one in our internal table */
				if (!strcmp(strs[k], cs_keywords[j])) {
					switch (j) {
					case 0:	/* rptena */
						rpt_vars[n].p.s[statenum].txdisable = 0;
						break;
					case 1:	/* rptdis */
						rpt_vars[n].p.s[statenum].txdisable = 1;
						break;

					case 2:	/* apena */
						rpt_vars[n].p.s[statenum].autopatchdisable = 0;
						break;

					case 3:	/* apdis */
						rpt_vars[n].p.s[statenum].autopatchdisable = 1;
						break;

					case 4:	/* lnkena */
						rpt_vars[n].p.s[statenum].linkfundisable = 0;
						break;

					case 5:	/* lnkdis */
						rpt_vars[n].p.s[statenum].linkfundisable = 1;
						break;

					case 6:	/* totena */
						rpt_vars[n].p.s[statenum].totdisable = 0;
						break;

					case 7:	/* totdis */
						rpt_vars[n].p.s[statenum].totdisable = 1;
						break;

					case 8:	/* skena */
						rpt_vars[n].p.s[statenum].schedulerdisable = 0;
						break;

					case 9:	/* skdis */
						rpt_vars[n].p.s[statenum].schedulerdisable = 1;
						break;

					case 10:	/* ufena */
						rpt_vars[n].p.s[statenum].userfundisable = 0;
						break;

					case 11:	/* ufdis */
						rpt_vars[n].p.s[statenum].userfundisable = 1;
						break;

					case 12:	/* atena */
						rpt_vars[n].p.s[statenum].alternatetail = 1;
						break;

					case 13:	/* atdis */
						rpt_vars[n].p.s[statenum].alternatetail = 0;
						break;

					case 14:	/* noice */
						rpt_vars[n].p.s[statenum].noincomingconns = 1;
						break;

					case 15:	/* noicd */
						rpt_vars[n].p.s[statenum].noincomingconns = 0;
						break;

					case 16:	/* slpen */
						rpt_vars[n].p.s[statenum].sleepena = 1;
						break;

					case 17:	/* slpds */
						rpt_vars[n].p.s[statenum].sleepena = 0;
						break;

					default:
						ast_log(LOG_WARNING, "Unhandled control state keyword %s", cs_keywords[i]);
						break;
					}
				}
			}
		}
		vp = vp->next;
	}
	ast_mutex_unlock(&rpt_vars[n].lock);
}
