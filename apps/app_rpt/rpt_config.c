
#include "asterisk.h"

#include <sys/stat.h>
#include <math.h>
#include <termios.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/cli.h" /* use ast_cli_command */
#include "asterisk/module.h" /* use ast_module_check */
#include "asterisk/dns_core.h" /* use for dns lookup */
#include "asterisk/dns_resolver.h" /* use for dns lookup */
#include "asterisk/dns_srv.h"	/* use for srv dns lookup */
#include "asterisk/dns_txt.h" /* user for dns lookup */
#include "asterisk/vector.h" /* required for dns */
#include "asterisk/utils.h" /* required for ARRAY_LEN */

#include "app_rpt.h"
#include <arpa/nameser.h> /* needed for dns - must be after app_rpt.h */
#include "rpt_lock.h"
#include "rpt_config.h"
#include "rpt_manager.h"
#include "rpt_utils.h" /* use myatoi */
#include "rpt_rig.h" /* use setrem */

/*! \brief Echolink queryoption for retrieving call sign */
#define ECHOLINK_QUERY_CALLSIGN 2

/*! \brief The Link Box queryoptions */
#define TLB_QUERY_NODE_EXISTS 1
#define TLB_QUERY_GET_CALLSIGN 2

/*! \brief DNS max overall and per-label sizes (RFC1035) */
#define MAX_DNS_NODE_DOMAIN_LEN 253
#define MAX_DNS_NODE_LABEL_LEN 63

extern struct rpt rpt_vars[MAXRPTS];
extern enum rpt_dns_method rpt_node_lookup_method;
extern char *rpt_dns_node_domain;
extern int rpt_max_dns_node_length;

static struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS };

AST_MUTEX_DEFINE_STATIC(nodelookuplock);

int retrieve_astcfgint(struct rpt *myrpt, const char *category, const char *name, int min, int max, int defl)
{
	const char *var;
	int ret;
	char include_zero = 0;

	if (min < 0) {				/* If min is negative, this means include 0 as a valid entry */
		min = -min;
		include_zero = 1;
	}

	var = ast_variable_retrieve(myrpt->cfg, category, name);
	if (var) {
		ret = myatoi(var);
		if (include_zero && !ret) {
			return 0;
		}
		if (ret < min) {
			ret = min;
		}
		if (ret > max) {
			ret = max;
		}
	} else {
		ret = defl;
	}
	return ret;
}

int get_wait_interval(struct rpt *myrpt, enum rpt_delay type)
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
	ast_copy_string(tmp, val, sizeof(tmp));

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
		case 'f':
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

int get_mem_set(struct rpt *myrpt, char *digitbuf)
{
	int res = 0;
	ast_debug(1, " digitbuf=%s\n", digitbuf);
	res = retrieve_memory(myrpt, digitbuf);
	if (!res)
		res = setrem(myrpt);
	ast_debug(1, " freq=%s  res=%i\n", myrpt->freq, res);
	return res;
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
	ast_copy_string(myrpt->curdtmfuser, val, sizeof(myrpt->curdtmfuser));
	myrpt->dtmfkeyed = 1;
	myrpt->dtmfkeybuf[0] = 0;
}

int elink_query_callsign(char *node, char *callsign, int callsignlen)
{
	const struct ast_channel_tech *chan_tech = NULL;
	int res = -1;

	chan_tech = ast_get_channel_tech("echolink");

	if (!chan_tech) {
		ast_log(LOG_WARNING, "chan_echolink not loaded.  Cannot query callsign.\n");
		return res;
	}

	/* data is passed to and from the query option using the callsign field */
	ast_copy_string(callsign, node, callsignlen);

	res = chan_tech->queryoption(NULL, ECHOLINK_QUERY_CALLSIGN, callsign, &callsignlen);

	return res;
}

int tlb_query_node_exists(const char *node)
{
	const struct ast_channel_tech *chan_tech = NULL;
	int res = 0;

	chan_tech = ast_get_channel_tech("tlb");

	if (!chan_tech) {
		ast_debug(5, "chan_tlb not loaded.\n");
		return res;
	}

	if (!chan_tech->queryoption(NULL, TLB_QUERY_NODE_EXISTS, (void *) node, 0)) {
		res = 1;
	}

	return res;
}

int tlb_query_callsign(const char *node, char *callsign, int callsignlen)
{
	const struct ast_channel_tech *chan_tech = NULL;
	int res = -1;

	chan_tech = ast_get_channel_tech("tlb");

	if (!chan_tech) {
		ast_debug(5, "chan_tlb not loaded. Cannot query callsign.\n");
		return res;
	}

	/* data is passed to and from the query option using the callsign field */
	ast_copy_string(callsign, node, callsignlen);

	res = chan_tech->queryoption(NULL, TLB_QUERY_GET_CALLSIGN, callsign, &callsignlen);

	return res;
}

/*!
 * \brief AllStar Network node lookup by dns.
 * Calling routine should pass a buffer for nodedata and nodedatalength
 * of sufficient length. A typical response is
 * "radio@123.123.123.123:4569/50000,123.123.123.123
 * This routine uses the SRV or TXT records provided by AllStarLink
 *
 * \note This routine can be called by app_rpt multiple times as
 * it constructs the node number.  The routine will only perform a
 * lookup after it receives 4 digits.  The actual node number may be
 * longer than 4 digits.
 *
 * \param node				Node number to lookup
 * \param nodedata			Buffer to hold the matching node information
 * \param nodedatalength	Length of the nodedata buffer
 * \retval -1 				if not successful
 * \retval 0 				if successful
 */
static int node_lookup_bydns(const char *node, char *nodedata, size_t nodedatalength)
{
	struct ast_dns_result *result;
	const struct ast_dns_record *record;

	char domain[256];
	int res;

	/* will will require at least a node length of 4 digits */
	if (strlen(node) < 4) {
		return -1;
	}

	/* make sure we have buffers to return the data */
	ast_assert(nodedata != NULL);
	ast_assert(nodedatalength > 0);

	/* AllStarLink supports two mechanisms to resolve node information.
	 * You can use the SRV record followed by resolving the node name or
	 * look up the information in the text record.
	 */
#if 1
	/* Resolve the node by using SRV record */
	{
		char *hostname;
		const char *ipaddress;
		unsigned short iaxport;

		/* setup the domain to lookup */
		memset(domain,0, sizeof(domain));
		res = snprintf(domain, sizeof(domain), "_iax._udp.%s.%s", node, rpt_dns_node_domain);
		if (res < 0) {
			return -1;
		}

		ast_debug(4, "Resolving DNS SRV records for: %s\n", domain);

		if (ast_dns_resolve(domain, T_SRV, C_IN, &result)) {
			ast_log(LOG_ERROR, "DNS SRV request failed\n");
			return -1;
		}
		if (!result) {
			ast_debug(4, "No SRV results returned for %s\n", domain);
			return -1;
		}

		/* get the response */
		record = ast_dns_result_get_records(result);

		if(!record) {
			ast_debug(4, "No SRV records returned for %s\n", domain);
			ast_dns_result_free(result);
			return -1;
		}

		hostname = ast_strdupa(ast_dns_srv_get_host(record));
		iaxport = ast_dns_srv_get_port(record);

		ast_debug(4, "Resolving A record for host: %s, port: %d\n", hostname, iaxport);

		ast_dns_result_free(result);

		if (ast_dns_resolve(hostname, T_A, C_IN, &result)) {
			ast_log(LOG_ERROR, "DNS resolve request failed\n");
			return -1;
		}
		if (!result) {
			ast_debug(4, "No A results returned for %s\n", hostname);
			return -1;
		}

		/* get the response */
		record = ast_dns_result_get_records(result);
		if (!record) {
			ast_debug(4, "No A records returned for %s\n", hostname);
			ast_dns_result_free(result);
			return -1;
		}

		ipaddress = ast_inet_ntoa(*(struct in_addr*)ast_dns_record_get_data(record));

		ast_dns_result_free(result);

		/* format the response */
		memset(nodedata, 0, nodedatalength);
		snprintf(nodedata, nodedatalength, "radio@%s:%d/%s,%s", ipaddress, iaxport, node, ipaddress);
	}
#else
	/* Resolve the node by using the TXT record */
	{
		char actualnode[10];
		char ipaddress[20];
		char iaxport[10];
		char tmp[100];

		struct ast_vector_string *txtrecords;
		int txtcount = 0;

		/* setup the domain to lookup */
		memset(domain, 0, sizeof(domain));
		res = snprintf(domain, sizeof(domain), "%s.%s", node, rpt_dns_node_domain);
		if (res < 0) {
			return -1;
		}

		ast_debug(4, "Resolving DNS TXT records for: %s\n", domain);

		/* resolve the domain name */
		if (ast_dns_resolve(domain, T_TXT, C_IN, &result)) {
			ast_log(LOG_ERROR, "DNS request failed\n");
			return -1;
		}
		if (!result) {
			return -1;
		}

		/* get the response */
		record = ast_dns_result_get_records(result);

		if(!record) {
			ast_dns_result_free(result);
			return -1;
		}

		/* process the text records
		text records are in the format
		"NN=2530" "RT=2023-02-21 17:33:07" "RB=0" "IP=104.153.109.212" "PIP=0" "PT=4569" "RH=register-west"
		*/
		txtrecords = ast_dns_txt_get_strings( record);

		for (txtcount = 0; txtcount < AST_VECTOR_SIZE(txtrecords); txtcount++) {
			ast_copy_string(tmp, AST_VECTOR_GET(txtrecords, txtcount), sizeof(tmp));
			if (ast_begins_with(tmp, "NN=")) {
				ast_copy_string(actualnode,tmp + 3, sizeof(actualnode));
			}
			if (ast_begins_with(tmp, "IP=")) {
				ast_copy_string(ipaddress,tmp + 3, sizeof(ipaddress));
			}
			if (ast_begins_with(tmp, "PT=")) {
				ast_copy_string(iaxport,tmp + 3, sizeof(iaxport));
			}
		}

		/* format the response */
		memset(nodedata, 0, nodedatalength);
		snprintf(nodedata, nodedatalength, "radio@%s:%s/%s,%s", ipaddress, iaxport, actualnode, ipaddress);

		ast_dns_txt_free_strings(txtrecords);
		ast_dns_result_free(result);
	}
#endif

	return 0;
}

int node_lookup(struct rpt *myrpt, char *digitbuf, char *nodedata, size_t nodedatalength, int wilds)
{
	char *val;
	int longestnode, i, j, found = 0;
	struct stat mystat;
	struct ast_config *ourcfg;
	struct ast_variable *vp;

	/* try to look it up locally first */
	val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.nodes, digitbuf);
	if (val) {
		if (nodedata && nodedatalength) {
			//snprintf(str,strmax,val,digitbuf);
			//snprintf(str, strmax, "%s%s", val, digitbuf);	/*! \todo 20220111 NA. This may not actually be correct (functionality-wise). Should be verified. For now, it makes the compiler happy. */
			snprintf(nodedata, nodedatalength, "%s", val); // Indeed, generally we only want the first part so for now, ignore the second bit
			ast_debug(4, "Resolved by internal: node %s to %s\n", digitbuf, nodedata);
		}
		return 0;
	}
	if (wilds) {
		vp = ast_variable_browse(myrpt->cfg, myrpt->p.nodes);
		while (vp) {
			if (ast_extension_match(vp->name, digitbuf)) {
				if (nodedata && nodedatalength) {
					//snprintf(str,strmax,vp->value,digitbuf);
					//snprintf(str, strmax, "%s%s", vp->value, digitbuf);	// 20220111 NA. This may not actually be correct (functionality-wise). Should be verified. For now, it makes the compiler happy.
					snprintf(nodedata, nodedatalength, "%s", vp->value);
					ast_debug(4, "Resolved by internal/wild: node %s to %s\n", digitbuf, nodedata);
				}
				return 0;
			}
			vp = vp->next;
		}
	}

	/* try to look up the node using dns */
	if(rpt_node_lookup_method == LOOKUP_BOTH || rpt_node_lookup_method == LOOKUP_DNS) {
		if(!node_lookup_bydns(digitbuf, nodedata, nodedatalength)) {
			ast_debug(4, "Resolved by DNS: node %s to %s\n", digitbuf, nodedata);
			return 0;
		}
	}

	/* try to lookup using the external file(s) */
	if(rpt_node_lookup_method == LOOKUP_BOTH || rpt_node_lookup_method == LOOKUP_FILE) {

		/* lock the node lookup */
		ast_mutex_lock(&nodelookuplock);
		if (!myrpt->p.extnodefilesn) {
			ast_mutex_unlock(&nodelookuplock);
			return -1;
		}

		/* determine longest node length again */
		longestnode = 0;
		vp = ast_variable_browse(myrpt->cfg, myrpt->p.nodes);
		while (vp) {
			j = strlen(vp->name);
			if (*vp->name == '_') {
				j--;
			}
			longestnode = MAX(longestnode, j);
			vp = vp->next;
		}
		found = 0;

		/* process each external node file */
		for (i = 0; i < myrpt->p.extnodefilesn; i++) {

			/* see if the external node file exists */
			if (stat(myrpt->p.extnodefiles[i], &mystat) == -1) {
				continue;
			}

			ourcfg = ast_config_load(myrpt->p.extnodefiles[i], config_flags);

			/* if file is not there, try the next one */
			if (!ourcfg) {
				continue;
			}

			/* determine the longest node */
			vp = ast_variable_browse(ourcfg, myrpt->p.extnodes);
			while (vp) {
				j = strlen(vp->name);
				if (*vp->name == '_') {
					j--;
				}
				longestnode = MAX(longestnode, j);
				vp = vp->next;
			}

			/* if we have not found a match, attempt to load a matching node */
			if (!found) {
				val = (char *) ast_variable_retrieve(ourcfg, myrpt->p.extnodes, digitbuf);
				if (val) {
					found = 1;
					if (nodedata && nodedatalength) {
						//snprintf(str,strmax,val,digitbuf);
						//snprintf(str, strmax, "%s%s", val, digitbuf);	// 20220111 NA. This may not actually be correct (functionality-wise). Should be verified. For now, it makes the compiler happy.
						snprintf(nodedata, nodedatalength, "%s", val);
						ast_debug(4, "Resolved from file: node %s to %s\n", digitbuf, nodedata);
					}
				}
			}
			ast_config_destroy(ourcfg);
		}
		myrpt->longestnode = MAX(longestnode, rpt_max_dns_node_length);
		ast_mutex_unlock(&nodelookuplock);
	}

	return (found ? 0 : -1);
}

int forward_node_lookup(char *digitbuf, struct ast_config *cfg, char *nodedata, size_t nodedatalength)
{
	char *val, *efil, *enod, *strs[100];
	int i, n;
	struct stat mystat;
	struct ast_config *ourcfg;

	memset(nodedata, 0, nodedatalength);
	val = NULL;

	/* try to look up the node using dns */
	if(rpt_node_lookup_method == LOOKUP_BOTH || rpt_node_lookup_method == LOOKUP_DNS) {
		if(!node_lookup_bydns(digitbuf, nodedata, nodedatalength)) {
			ast_debug(4, "Forward lookup resolved by DNS: node %s to %s\n", digitbuf, nodedata);
			return 0;
		}
	}

	/* try to lookup using the external file(s) */
	if(rpt_node_lookup_method == LOOKUP_BOTH || rpt_node_lookup_method == LOOKUP_FILE) {
		/* see if we have extnodefile setup in the proxy section - if not use the default name */
		val = (char *) ast_variable_retrieve(cfg, "proxy", "extnodefile");
		if (!val) {
			val = EXTNODEFILE;
		}

		/* see if we have an override for the extnodes section in the proxy section */
		enod = (char *) ast_variable_retrieve(cfg, "proxy", "extnodes");
		if (!enod) {
			enod = EXTNODES;
		}

		/* prepare to lookup using the external file(s) */
		ast_mutex_lock(&nodelookuplock);
		efil = ast_strdup(val);
		if (!efil) {
			ast_mutex_unlock(&nodelookuplock);
			return -1;
		}

		/* parse the external node file name(s) - we allow for multiple files */
		n = finddelim(efil, strs, ARRAY_LEN(strs));
		if (n < 1) {
			ast_free(efil);
			ast_mutex_unlock(&nodelookuplock);
			return -1;
		}
		val = NULL;

		/* process each external node file */
		for (i = 0; i < n; i++) {

			/* see if the external node file exists */
			if (stat(strs[i], &mystat) == -1) {
				continue;
			}

			ourcfg = ast_config_load(strs[i], config_flags);
			/* if file is not there, try the next one */
			if (!ourcfg) {
				continue;
			}

			/* if we have not found a match, attempt to load a matching node */
			if (!val) {
				val = (char *) ast_variable_retrieve(ourcfg, enod, digitbuf);
			}
			ast_config_destroy(ourcfg);
		}

		if (val) {
			ast_copy_string(nodedata, val, nodedatalength);
			ast_debug(4, "Forward lookup resolved from file: node %s to %s\n", digitbuf, nodedata);
		}

		ast_mutex_unlock(&nodelookuplock);
		ast_free(efil);
	}

	return (val ? 0 : -1);
}

void load_rpt_vars(int n, int init)
{
	const char *cat, *val;
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
	if (rpt_vars[n].cfg) {
		ast_config_destroy(rpt_vars[n].cfg);
	}
	cfg = ast_config_load("rpt.conf", config_flags);
	if (!cfg) {
		ast_mutex_unlock(&rpt_vars[n].lock);
		ast_log(LOG_ERROR, "Unable to open radio repeater configuration rpt.conf.  Radio Repeater disabled.\n");
		pthread_exit(NULL);
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_mutex_unlock(&rpt_vars[n].lock);
		ast_log(LOG_ERROR, "Errors detected in the radio repeater configuration rpt.conf.  Radio Repeater disabled.\n");
		pthread_exit(NULL);
	}
	rpt_vars[n].cfg = cfg;
	cat = rpt_vars[n].name;
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
		rpt_vars[n].outstreampipe[0] = -1;
		rpt_vars[n].outstreampipe[1] = -1;
	}
#ifdef	__RPT_NOTCH
	/* zot out filters stuff */
	memset(&rpt_vars[n].filters, 0, sizeof(rpt_vars[n].filters));
#endif

#define RPT_CONFIG_VAR(var, name) \
	val = ast_variable_retrieve(cfg, cat, name); \
	if (val) { \
		rpt_vars[n].p.var = val; \
	}

#define RPT_CONFIG_VAR_DEFAULT(var, name, default) \
	val = ast_variable_retrieve(cfg, cat, name); \
	if (val) { \
		rpt_vars[n].p.var = val; \
	} else { \
		rpt_vars[n].p.var = default; \
	}

#define RPT_CONFIG_VAR_COND_DEFAULT(var, name, cond, default) \
	val = ast_variable_retrieve(cfg, cat, name); \
	if (val) { \
		rpt_vars[n].p.var = val; \
	} else if ((cond)) { \
		rpt_vars[n].p.var = default; \
	}

#define RPT_CONFIG_VAR_CHAR_DEFAULT(var, name, default) \
	val = ast_variable_retrieve(cfg, cat, name); \
	if (val) { \
		rpt_vars[n].p.var = *val; \
	} else { \
		rpt_vars[n].p.var = default; \
	}

#define RPT_CONFIG_VAR_INT(var, name) \
	val = ast_variable_retrieve(cfg, cat, name); \
	if (!ast_strlen_zero(val)) { \
		rpt_vars[n].p.var = atoi(val); \
	}

#define RPT_CONFIG_VAR_FLOAT_DB_DEFAULT(var, name, default) \
	val = ast_variable_retrieve(cfg, cat, name); \
	if (!ast_strlen_zero(val)) { \
		rpt_vars[n].p.var = pow(10.0, atof(val) / 20.0); \
	} else { \
		rpt_vars[n].p.var = pow(10.0, (double) (default) / 20.0); \
	}

#define RPT_CONFIG_VAR_INT_DEFAULT(var, name, default) \
	val = ast_variable_retrieve(cfg, cat, name); \
	if (!ast_strlen_zero(val)) { \
		rpt_vars[n].p.var = atoi(val); \
	} else { \
		rpt_vars[n].p.var = default; \
	}
	
#define RPT_CONFIG_VAR_INT_DEFAULT_MIN_MAX(var, name, default_val, min_val, max_val) \
	val = ast_variable_retrieve(cfg, cat, name); \
	if (!ast_strlen_zero(val)) { \
		rpt_vars[n].p.var = atoi(val); \
		if (rpt_vars[n].p.var < min_val) { \
			rpt_vars[n].p.var = min_val; \
		} else if (rpt_vars[n].p.var > max_val) { \
			rpt_vars[n].p.var = max_val; \
		} \
	} else { \
		rpt_vars[n].p.var = default_val; \
	}

#define RPT_CONFIG_VAR_BOOL(var, name) \
	val = ast_variable_retrieve(cfg, cat, name); \
	if (!ast_strlen_zero(val)) { \
		rpt_vars[n].p.var = ast_true(val); \
	}

#define RPT_CONFIG_VAR_BOOL_DEFAULT(var, name, default) \
	val = ast_variable_retrieve(cfg, cat, name); \
	if (!ast_strlen_zero(val)) { \
		rpt_vars[n].p.var = ast_true(val); \
	} else { \
		rpt_vars[n].p.var = default; \
	}

	RPT_CONFIG_VAR(ourcontext, "context");
	if (!val) {
		rpt_vars[n].p.ourcontext = cat;
	}

	RPT_CONFIG_VAR(ourcallerid, "callerid");
	RPT_CONFIG_VAR(acctcode, "accountcode");
	RPT_CONFIG_VAR(ident, "idrecording");

	RPT_CONFIG_VAR_INT(hangtime, "hangtime");
	if (!val) {
		rpt_vars[n].p.hangtime = HANGTIME;
	}
	if (rpt_vars[n].p.hangtime < 1) {
		rpt_vars[n].p.hangtime = 1;
	}

	RPT_CONFIG_VAR_INT(althangtime, "althangtime");
	if (!val) {
		rpt_vars[n].p.althangtime = HANGTIME;
	}
	if (rpt_vars[n].p.althangtime < 1) {
		rpt_vars[n].p.althangtime = 1;
	}

	RPT_CONFIG_VAR_INT_DEFAULT(totime, "totime", TOTIME);
	RPT_CONFIG_VAR_INT_DEFAULT_MIN_MAX(time_out_reset_unkey_interval, "time_out_reset_unkey_interval", TIMEOUTRESETUNKEYINTERVAL, 0, 3000)
	RPT_CONFIG_VAR_INT_DEFAULT_MIN_MAX(time_out_reset_kerchunk_interval, "time_out_reset_kerchunk_interval",
		TIMEOUTRESETKERCHUNKINTERVAL, 0, 3000);
	RPT_CONFIG_VAR_INT_DEFAULT(voxtimeout_ms, "voxtimeout", VOX_TIMEOUT_MS);
	RPT_CONFIG_VAR_INT_DEFAULT(voxrecover_ms, "voxrecover", VOX_RECOVER_MS);
	RPT_CONFIG_VAR_INT_DEFAULT(simplexpatchdelay, "simplexpatchdelay", SIMPLEX_PATCH_DELAY);
	RPT_CONFIG_VAR_INT_DEFAULT(simplexphonedelay, "simplexphonedelay", SIMPLEX_PHONE_DELAY);
	RPT_CONFIG_VAR_DEFAULT(statpost_program, "statpost_program", STATPOST_PROGRAM);
	RPT_CONFIG_VAR(statpost_url, "statpost_url");

	rpt_vars[n].p.tailmessagetime = retrieve_astcfgint(&rpt_vars[n], cat, "tailmessagetime", 0, 200000000, 0);
	rpt_vars[n].p.tailsquashedtime = retrieve_astcfgint(&rpt_vars[n], cat, "tailsquashedtime", 0, 200000000, 0);
	rpt_vars[n].p.duplex = retrieve_astcfgint(&rpt_vars[n], cat, "duplex", 0, 4, 2);
	rpt_vars[n].p.idtime = retrieve_astcfgint(&rpt_vars[n], cat, "idtime", -60000, 2400000, IDTIME);	/* Enforce a min max including zero */
	rpt_vars[n].p.politeid = retrieve_astcfgint(&rpt_vars[n], cat, "politeid", 30000, 300000, POLITEID);	/* Enforce a min max */

	j = retrieve_astcfgint(&rpt_vars[n], cat, "elke", 0, 40000000, 0);
	rpt_vars[n].p.elke = j * 1210;

	RPT_CONFIG_VAR(tonezone, "tonezone");

	rpt_vars[n].p.tailmessages[0] = 0;
	rpt_vars[n].p.tailmessagemax = 0;

	val = ast_variable_retrieve(cfg, cat, "tailmessagelist");
	if (val) {
		rpt_vars[n].p.tailmessagemax = finddelim((char*) val, (char**) rpt_vars[n].p.tailmessages, 500); /*! \todo This is illegal, cannot cast the const away */
	}

	RPT_CONFIG_VAR(aprstt, "aprstt");
	RPT_CONFIG_VAR_DEFAULT(memory, "memory", MEMORY);
	RPT_CONFIG_VAR_DEFAULT(morse, "morse", MORSE);
	RPT_CONFIG_VAR_DEFAULT(telemetry, "telemetry", TELEMETRY);
	RPT_CONFIG_VAR_DEFAULT(macro, "macro", MACRO);
	RPT_CONFIG_VAR_DEFAULT(tonemacro, "tonemacro", TONEMACRO);
	RPT_CONFIG_VAR_DEFAULT(mdcmacro, "mdcmacro", MDCMACRO);
	RPT_CONFIG_VAR(startupmacro, "startup_macro");

	val = ast_variable_retrieve(cfg, cat, "iobase");
	/* do not use atoi() here, we need to be able to have
	   the input specified in hex or decimal so we use
	   sscanf with a %i */
	if ((!val) || (sscanf(val, N_FMT(i), &rpt_vars[n].p.iobase) != 1)) {
		rpt_vars[n].p.iobase = DEFAULT_IOBASE;
	}

	RPT_CONFIG_VAR(ioport, "ioport");

	RPT_CONFIG_VAR(functions, "functions");
	if (!val) {
		rpt_vars[n].p.functions = FUNCTIONS;
		rpt_vars[n].p.simple = 1;
	}

	RPT_CONFIG_VAR_DEFAULT(link_functions, "link_functions", rpt_vars[n].p.functions);
	RPT_CONFIG_VAR_COND_DEFAULT(phone_functions, "phone_functions", 0, rpt_vars[n].p.functions);
	RPT_CONFIG_VAR_COND_DEFAULT(dphone_functions, "dphone_functions", 0, rpt_vars[n].p.functions);
	RPT_CONFIG_VAR(alt_functions, "alt_functions");
	RPT_CONFIG_VAR_CHAR_DEFAULT(funcchar, "funcchar", FUNCCHAR);
	RPT_CONFIG_VAR_CHAR_DEFAULT(endchar, "endchar", ENDCHAR);
	RPT_CONFIG_VAR_BOOL(nobusyout, "nobusyout");
	RPT_CONFIG_VAR_BOOL(notelemtx, "notelemtx");
	RPT_CONFIG_VAR_BOOL(propagate_dtmf, "propagate_dtmf");
	RPT_CONFIG_VAR_BOOL(propagate_phonedtmf, "propagate_phonedtmf");
	RPT_CONFIG_VAR_BOOL(linktolink, "linktolink");
	RPT_CONFIG_VAR_DEFAULT(nodes, "nodes", NODES);
	RPT_CONFIG_VAR_DEFAULT(extnodes, "extnodes", EXTNODES);

	val = ast_variable_retrieve(cfg, cat, "extnodefile");
	rpt_vars[n].p.extnodefilesn = explode_string((char*) S_OR(val, EXTNODEFILE), (char**) rpt_vars[n].p.extnodefiles, ARRAY_LEN(rpt_vars[n].p.extnodefiles), ',', 0); /*! \todo Illegal cast */

	/*! \todo Is this memory properly freed? */
	val = ast_variable_retrieve(cfg, cat, "locallinknodes");
	if (val) {
		rpt_vars[n].p.locallinknodesn = explode_string(ast_strdup(val), (char**) rpt_vars[n].p.locallinknodes, ARRAY_LEN(rpt_vars[n].p.locallinknodes), ',', 0);
	}

	val = ast_variable_retrieve(cfg, cat, "lconn");
	if (val) {
		rpt_vars[n].p.nlconn = explode_string(strupr(ast_strdup(val)), (char**) rpt_vars[n].p.lconn, ARRAY_LEN(rpt_vars[n].p.lconn), ',', 0);
	}

	val = ast_variable_retrieve(cfg, cat, "ldisc");
	if (val) {
		rpt_vars[n].p.nldisc = explode_string(strupr(ast_strdup(val)), (char**) rpt_vars[n].p.ldisc, ARRAY_LEN(rpt_vars[n].p.ldisc), ',', 0);
	}

	RPT_CONFIG_VAR(patchconnect, "patchconnect");
	RPT_CONFIG_VAR(archivedir, "archivedir");
	RPT_CONFIG_VAR_BOOL_DEFAULT(archiveaudio, "archiveaudio", 1);
	RPT_CONFIG_VAR(archivedatefmt, "archivedatefmt");
	RPT_CONFIG_VAR(archiveformat, "archiveformat");
	RPT_CONFIG_VAR_INT(authlevel, "authlevel");

	val = ast_variable_retrieve(cfg, cat, "parrot");
	if (val) {
		rpt_vars[n].p.parrotmode = ast_true(val) ? PARROT_MODE_ON_ALWAYS : PARROT_MODE_OFF;
	} else {
		rpt_vars[n].p.parrotmode = PARROT_MODE_OFF;
	}

	RPT_CONFIG_VAR_INT_DEFAULT(parrottime, "parrottime", PARROTTIME);
	RPT_CONFIG_VAR(rptnode, "rptnode");
	RPT_CONFIG_VAR_INT(remote_mars, "mars");
	RPT_CONFIG_VAR_INT_DEFAULT(monminblocks, "monminblocks", DEFAULT_MONITOR_MIN_DISK_BLOCKS);
	RPT_CONFIG_VAR_INT_DEFAULT(remoteinacttimeout, "remote_inact_timeout", DEFAULT_REMOTE_INACT_TIMEOUT);
	RPT_CONFIG_VAR_INT_DEFAULT(civaddr, "civaddr", DEFAULT_CIV_ADDR);
	RPT_CONFIG_VAR_INT_DEFAULT(remotetimeout, "remote_timeout", DEFAULT_REMOTE_TIMEOUT);
	RPT_CONFIG_VAR_INT_DEFAULT(remotetimeoutwarning, "remote_timeout_warning", DEFAULT_REMOTE_TIMEOUT_WARNING);
	RPT_CONFIG_VAR_INT_DEFAULT(remotetimeoutwarningfreq, "remote_timeout_warning_freq", DEFAULT_REMOTE_TIMEOUT_WARNING_FREQ);

	RPT_CONFIG_VAR_FLOAT_DB_DEFAULT(erxgain, "erxgain", DEFAULT_ERXGAIN);
	RPT_CONFIG_VAR_FLOAT_DB_DEFAULT(etxgain, "etxgain", DEFAULT_ETXGAIN);

	RPT_CONFIG_VAR_INT_DEFAULT(eannmode, "eannmode", DEFAULT_EANNMODE);
	if (rpt_vars[n].p.eannmode < 0) {
		rpt_vars[n].p.eannmode = 0;
	} else if (rpt_vars[n].p.eannmode > 3) {
		rpt_vars[n].p.eannmode = 3;
	}

	RPT_CONFIG_VAR_FLOAT_DB_DEFAULT(trxgain, "trxgain", DEFAULT_TRXGAIN);
	RPT_CONFIG_VAR_FLOAT_DB_DEFAULT(ttxgain, "ttxgain", DEFAULT_TTXGAIN);

	RPT_CONFIG_VAR_INT_DEFAULT(tannmode, "tannmode", DEFAULT_TANNMODE);
	if (rpt_vars[n].p.tannmode < 1) {
		rpt_vars[n].p.tannmode = 1;
	} else if (rpt_vars[n].p.tannmode > 3) {
		rpt_vars[n].p.tannmode = 3;
	}

	RPT_CONFIG_VAR_FLOAT_DB_DEFAULT(linkmongain, "linkmongain", DEFAULT_LINKMONGAIN);

	RPT_CONFIG_VAR(connpgm, "connpgm");
	RPT_CONFIG_VAR(discpgm, "discpgm");
	RPT_CONFIG_VAR(mdclog, "mdclog");
	RPT_CONFIG_VAR_BOOL(lnkactenable, "lnkactenable");
	RPT_CONFIG_VAR(lnkacttimerwarn, "lnkacttimerwarn");
	rpt_vars[n].p.lnkacttime = retrieve_astcfgint(&rpt_vars[n], cat, "lnkacttime", 0, 90000, 0);	/* Enforce a min max including zero */

	RPT_CONFIG_VAR(lnkactmacro, "lnkactmacro");
	RPT_CONFIG_VAR_BOOL(nolocallinkct, "nolocallinkct");

	rpt_vars[n].p.rptinacttime = retrieve_astcfgint(&rpt_vars[n], cat, "rptinacttime", -120, 90000, 0);	/* Enforce a min max including zero */

	RPT_CONFIG_VAR(rptinactmacro, "rptinactmacro");
	RPT_CONFIG_VAR_BOOL(nounkeyct, "nounkeyct");
	RPT_CONFIG_VAR_BOOL(holdofftelem, "holdofftelem");
	RPT_CONFIG_VAR_BOOL(beaconing, "beaconing");
	RPT_CONFIG_VAR_INT(rxburstfreq, "rxburstfreq");
	RPT_CONFIG_VAR_INT_DEFAULT(rxbursttime, "rxbursttime", DEFAULT_RXBURST_TIME);
	RPT_CONFIG_VAR_INT_DEFAULT(rxburstthreshold, "rxburstthreshold", DEFAULT_RXBURST_THRESHOLD);
	RPT_CONFIG_VAR_INT_DEFAULT(litztime, "litztime", DEFAULT_LITZ_TIME);
	RPT_CONFIG_VAR_DEFAULT(litzchar, "litzchar", DEFAULT_LITZ_CHAR);
	RPT_CONFIG_VAR(litzcmd, "litzcmd");
	RPT_CONFIG_VAR_BOOL(itxctcss, "itxctcss");
	RPT_CONFIG_VAR_BOOL(gpsfeet, "gpsfeet");
	RPT_CONFIG_VAR_INT_DEFAULT(default_split_2m, "split2m", DEFAULT_SPLIT_2M);
	RPT_CONFIG_VAR_INT_DEFAULT(default_split_70cm, "split70cm", DEFAULT_SPLIT_70CM);
	RPT_CONFIG_VAR_BOOL(dtmfkey, "dtmfkey");
	RPT_CONFIG_VAR_DEFAULT(dtmfkeys, "dtmfkeys", DTMFKEYS);
	RPT_CONFIG_VAR(outstreamcmd, "outstreamcmd");
	RPT_CONFIG_VAR(eloutbound, "eloutbound");
	RPT_CONFIG_VAR_DEFAULT(events, "events", "events");
	RPT_CONFIG_VAR(timezone, "timezone");

#ifdef	__RPT_NOTCH
	val = ast_variable_retrieve(cfg, this, "rxnotch");
	if (val) {
		i = finddelim((char*) val, strs, MAXFILTERS * 2); /*! \todo Illegal cast away of const */
		i &= ~1;				/* force an even number, rounded down */
		if (i >= 2) {
			for (j = 0; j < i; j += 2) {
				rpt_mknotch(atof(strs[j]), atof(strs[j + 1]),
							&rpt_vars[n].filters[j >> 1].gain,
							&rpt_vars[n].filters[j >> 1].const0, &rpt_vars[n].filters[j >> 1].const1,
							&rpt_vars[n].filters[j >> 1].const2);
				sprintf(rpt_vars[n].filters[j >> 1].desc, "%s Hz, BW = %s", strs[j], strs[j + 1]);
			}
		}
	}
#endif

	RPT_CONFIG_VAR_INT(votertype, "votertype");
	RPT_CONFIG_VAR_INT(votermode, "votermode");
	RPT_CONFIG_VAR_INT_DEFAULT(votermargin, "votermargin", DEFAULT_VOTERGAIN);
	RPT_CONFIG_VAR_FLOAT_DB_DEFAULT(telemnomgain, "telemnomdb", DEFAULT_TELEMNOMDB);
	RPT_CONFIG_VAR_FLOAT_DB_DEFAULT(telemduckgain, "telemduckdb", DEFAULT_TELEMDUCKDB);
	RPT_CONFIG_VAR_INT_DEFAULT(telemdefault, "telemdefault", DEFAULT_RPT_TELEMDEFAULT);
	RPT_CONFIG_VAR_BOOL_DEFAULT(telemdynamic, "telemdynamic", DEFAULT_RPT_TELEMDYNAMIC);

	if (!rpt_vars[n].p.telemdefault) {
		rpt_vars[n].telemmode = 0;
	} else if (rpt_vars[n].p.telemdefault == 2) {
		rpt_vars[n].telemmode = 1;
	} else {
		rpt_vars[n].telemmode = 0x7fffffff;
	}

	RPT_CONFIG_VAR_INT_DEFAULT(linkmode[LINKMODE_GUI], "guilinkdefault", DEFAULT_GUI_LINK_MODE);
	RPT_CONFIG_VAR_BOOL_DEFAULT(linkmodedynamic[LINKMODE_GUI], "guilinkdynamic", DEFAULT_GUI_LINK_MODE_DYNAMIC);
	RPT_CONFIG_VAR_INT_DEFAULT(linkmode[LINKMODE_PHONE], "phonelinkdefault", DEFAULT_PHONE_LINK_MODE);
	RPT_CONFIG_VAR_BOOL_DEFAULT(linkmodedynamic[LINKMODE_PHONE], "phonelinkdynamic", DEFAULT_PHONE_LINK_MODE_DYNAMIC);
	RPT_CONFIG_VAR_INT_DEFAULT(linkmode[LINKMODE_ECHOLINK], "echolinkdefault", DEFAULT_ECHOLINK_LINK_MODE);
	RPT_CONFIG_VAR_BOOL_DEFAULT(linkmodedynamic[LINKMODE_ECHOLINK], "echolinkdynamic", DEFAULT_ECHOLINK_LINK_MODE_DYNAMIC);
	RPT_CONFIG_VAR_INT_DEFAULT(linkmode[LINKMODE_TLB], "tlbdefault", DEFAULT_TLB_LINK_MODE);
	RPT_CONFIG_VAR_BOOL_DEFAULT(linkmodedynamic[LINKMODE_TLB], "tlbdynamic", DEFAULT_TLB_LINK_MODE_DYNAMIC);

	val = ast_variable_retrieve(cfg, cat, "locallist");
	if (val) {
		memset(rpt_vars[n].p.locallist, 0, sizeof(rpt_vars[n].p.locallist));
		rpt_vars[n].p.nlocallist = finddelim((char*) val, (char**) rpt_vars[n].p.locallist, 16); /*! \todo Illegal cast */
	}

	val = ast_variable_retrieve(cfg, cat, "ctgroup");
	if (val) {
		ast_copy_string(rpt_vars[n].p.ctgroup, val, sizeof(rpt_vars[n].p.ctgroup));
	} else {
		strcpy(rpt_vars[n].p.ctgroup, "0");
	}

	val = ast_variable_retrieve(cfg, cat, "inxlat");
	if (val) {
		memset(&rpt_vars[n].p.inxlat, 0, sizeof(struct rpt_xlat));
		i = finddelim((char *) val, strs, ARRAY_LEN(strs)); /*! \todo Illegal cast */
		if (i > 3) {
			rpt_vars[n].p.dopfxtone = ast_true(strs[3]);
		}
		if (i > 2) {
			ast_copy_string(rpt_vars[n].p.inxlat.passchars, strs[2], sizeof(rpt_vars[n].p.inxlat.passchars));
		}
		if (i > 1) {
			ast_copy_string(rpt_vars[n].p.inxlat.endcharseq, strs[1], sizeof(rpt_vars[n].p.inxlat.endcharseq));
		}
		if (i) {
			ast_copy_string(rpt_vars[n].p.inxlat.funccharseq, strs[0], sizeof(rpt_vars[n].p.inxlat.funccharseq));
		}
	}

	val = ast_variable_retrieve(cfg, cat, "outxlat");
	if (val) {
		memset(&rpt_vars[n].p.outxlat, 0, sizeof(struct rpt_xlat));
		i = finddelim((char *) val, strs, ARRAY_LEN(strs)); /*! \todo Illegal cast */
		if (i > 2) {
			ast_copy_string(rpt_vars[n].p.outxlat.passchars, strs[2], sizeof(rpt_vars[n].p.outxlat.passchars));
		}
		if (i > 1) {
			ast_copy_string(rpt_vars[n].p.outxlat.endcharseq, strs[1], sizeof(rpt_vars[n].p.outxlat.endcharseq));
		}
		if (i) {
			ast_copy_string(rpt_vars[n].p.outxlat.funccharseq, strs[0], sizeof(rpt_vars[n].p.outxlat.funccharseq));
		}
	}

	RPT_CONFIG_VAR_INT_DEFAULT(sleeptime, "sleeptime", SLEEPTIME);
	RPT_CONFIG_VAR(csstanzaname, "controlstates"); /* stanza name for control states */
	RPT_CONFIG_VAR(skedstanzaname, "scheduler"); /* stanza name for scheduler */
	RPT_CONFIG_VAR(txlimitsstanzaname, "txlimits"); /* stanza name for txlimits */

	rpt_vars[n].p.iospeed = B9600;
	if (!strcasecmp(rpt_vars[n].remoterig, REMOTE_RIG_FT950)) {
		rpt_vars[n].p.iospeed = B38400;
	} else if (!strcasecmp(rpt_vars[n].remoterig, REMOTE_RIG_FT100)) {
		rpt_vars[n].p.iospeed = B4800;
	} else if (!strcasecmp(rpt_vars[n].remoterig, REMOTE_RIG_FT897)) {
		rpt_vars[n].p.iospeed = B4800;
	}

	RPT_CONFIG_VAR_BOOL(dias, "dias");
	RPT_CONFIG_VAR_BOOL(dusbabek, "dusbabek");

	val = ast_variable_retrieve(cfg, cat, "iospeed");
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
		longestnode = MAX(longestnode, j);
		vp = vp->next;
	}

	rpt_vars[n].longestnode = MAX(longestnode, rpt_max_dns_node_length);

	/* For this repeater, Determine the length of the longest function */
	rpt_vars[n].longestfunc = 0;
	vp = ast_variable_browse(cfg, rpt_vars[n].p.functions);
	while (vp) {
		j = strlen(vp->name);
		if (j > rpt_vars[n].longestfunc) {
			rpt_vars[n].longestfunc = j;
		}
		vp = vp->next;
	}

	/* For this repeater, Determine the length of the longest function */
	rpt_vars[n].link_longestfunc = 0;
	vp = ast_variable_browse(cfg, rpt_vars[n].p.link_functions);
	while (vp) {
		j = strlen(vp->name);
		if (j > rpt_vars[n].link_longestfunc) {
			rpt_vars[n].link_longestfunc = j;
		}
		vp = vp->next;
	}
	rpt_vars[n].phone_longestfunc = 0;
	if (rpt_vars[n].p.phone_functions) {
		vp = ast_variable_browse(cfg, rpt_vars[n].p.phone_functions);
		while (vp) {
			j = strlen(vp->name);
			if (j > rpt_vars[n].phone_longestfunc) {
				rpt_vars[n].phone_longestfunc = j;
			}
			vp = vp->next;
		}
	}
	rpt_vars[n].dphone_longestfunc = 0;
	if (rpt_vars[n].p.dphone_functions) {
		vp = ast_variable_browse(cfg, rpt_vars[n].p.dphone_functions);
		while (vp) {
			j = strlen(vp->name);
			if (j > rpt_vars[n].dphone_longestfunc) {
				rpt_vars[n].dphone_longestfunc = j;
			}
			vp = vp->next;
		}
	}
	rpt_vars[n].alt_longestfunc = 0;
	if (rpt_vars[n].p.alt_functions) {
		vp = ast_variable_browse(cfg, rpt_vars[n].p.alt_functions);
		while (vp) {
			j = strlen(vp->name);
			if (j > rpt_vars[n].alt_longestfunc) {
				rpt_vars[n].alt_longestfunc = j;
			}
			vp = vp->next;
		}
	}
	rpt_vars[n].macro_longest = 1;
	vp = ast_variable_browse(cfg, rpt_vars[n].p.macro);
	while (vp) {
		j = strlen(vp->name);
		if (j > rpt_vars[n].macro_longest) {
			rpt_vars[n].macro_longest = j;
		}
		vp = vp->next;
	}

	/* Browse for control states */
	if (rpt_vars[n].p.csstanzaname) {
		vp = ast_variable_browse(cfg, rpt_vars[n].p.csstanzaname);
	} else {
		vp = NULL;
	}

	for (i = 0; vp && (i < MAX_SYSSTATES); i++) {	/* Iterate over the number of control state lines in the stanza */
		int k, nukw, statenum;
		statenum = atoi(vp->name);
		ast_copy_string(s1, vp->value, sizeof(s1));
		nukw = finddelim(s1, strs, ARRAY_LEN(strs));

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

int rpt_push_alt_macro(struct rpt *myrpt, char *sptr)
{
	char *altstr, *cp;

	ast_debug(1, "rpt_push_alt_macro %s\n", sptr);
	altstr = ast_strdup(sptr);
	if (!altstr) {
		return -1;
	}
	for (cp = altstr; *cp; cp++) {
		*cp |= 0x80;
	}
	macro_append(myrpt, altstr);
	ast_free(altstr);
	return 0;
}

void rpt_update_boolean(struct rpt *myrpt, char *varname, int newval)
{
	char buf[2];

	if (!varname || !*varname) {
		return;
	}

	buf[0] = '0';
	buf[1] = '\0';
	if (newval > 0) {
		buf[0] = '1';
	}

	pbx_builtin_setvar_helper(myrpt->rxchannel, varname, buf);
	rpt_manager_trigger(myrpt, varname, buf);
	if (newval >= 0) {
		rpt_event_process(myrpt);
	}
}

int rpt_is_valid_dns_name(const char *dns_name)
{
	int label_length, label_start;

	if (!dns_name || *dns_name == '\0' || *dns_name == '.' || strlen(dns_name) > MAX_DNS_NODE_DOMAIN_LEN) {
		return 0;
	}

	label_length = 0;
	label_start = 0;

	for (const char *ptr = dns_name; *ptr; ++ptr) {
		if (*ptr == '.') {
			/* no empty labels */
			if (label_start) {
				return 0; // No empty labels
			}
			label_length = 0;
			label_start = 1;
		} else {
			/* only allow ASCII alphanumerics and hyphens per the standard */
			if (!isascii(*ptr) || (!isalnum(*ptr) && *ptr != '-')) {
				return 0;
			}
			if (*ptr == '-') {
				if (label_length == 0) {
					/* labels cannot start with a hyphen */
					return 0;
				} else if (*(ptr + 1) == '\0' || *(ptr + 1) == '.') {
					/* labels cannot end with a hyphen */
					return 0;
				}
			}
			label_length++;
			/* labels cannot exceed the max label length */
			if (label_length > MAX_DNS_NODE_LABEL_LEN) {
				return 0;
			}
			label_start = 0;
		}
	}

	return 1;
}
