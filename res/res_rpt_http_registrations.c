/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Naveen Albert
 * Copyright (C) 2024, Allstarlink, Inc
 *
 * Naveen Albert <asterisk@phreaknet.org>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*!
 * \file
 *
 * \brief RPT HTTP Registrations
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 */

/*** MODULEINFO
	<depend>res_curl</depend>
	<depend>curl</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <curl/curl.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/dnsmgr.h"
#include "asterisk/conversions.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<configInfo name="res_rpt_http_registrations" language="en_US">
		<synopsis>Periodic HTTP registrations (DDNS-like functionality)</synopsis>
		<configFile name="rpt_http_registrations.conf">
			<configObject name="general">
				<configOption name="register_interval" default="60">
					<synopsis>Time in seconds between registration attempts</synopsis>
				</configOption>
				<configOption name="register_dual_stack" default="no">
					<synopsis>
						Register IPv4 *and* IPv6 address.
						If "register_dual_stack = yes", register for each address
						family binding.
						If "no", register one time.
					</synopsis>
				</configOption>
			</configObject>
			<configObject name="registrations">
				<synopsis>HTTP registrations to attempt periodically</synopsis>
				<configOption name="register">
					<synopsis>IAX2-formatted register string for HTTP host</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
	<function name="RPT_REGISTRY" language="en_US">
		<synopsis>
			Gets the perceived IP address for a registered RPT registry entry.
		</synopsis>
		<syntax>
			<parameter name="username" required="true">
				<para>The RPT registry username to query.</para>
			</parameter>
			<parameter name="item">
				<para>Valid items are:</para>
				<enumlist>
					<enum name="address">
						<para>(default) The perceived IP address returned by the remote server for
						the currently registered registry entry.</para>
					</enum>
				</enumlist>
			</parameter>
			<parameter name="family">
				<para>Optional family selector.</para>
				<enumlist>
					<enum name="ipv4">
						<para>Return the IPv4 perceived address, if registered.</para>
					</enum>
					<enum name="ipv6">
						<para>Return the IPv6 perceived address, if registered.</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Returns the perceived IP address only, without a port, for the specified RPT
			registry entry if it is currently registered.</para>
			<para>If no registered entry exists, an empty string is returned.</para>
			<para>If no family is specified and multiple registered addresses are associated with the
			username, the string <literal>MULTIPLE</literal> is returned.</para>
			<para>This function is channel-independent and may be queried without a live channel.</para>
		</description>
	</function>
 ***/

#define CONFIG_FILE "rpt_http_registrations.conf"
#define RPT_INITIAL_BUFFER_SIZE 512

/*! \brief Default register interval is once per minute */
#define DEFAULT_REGISTER_INTERVAL 60

static int register_interval;

struct http_registry {
	struct http_registry_req *req;	 /*!< http_registry_req we belong to; NULL if not configured */
	char registered;				 /*!< Registered==1 */
	struct ast_dnsmgr_entry *dnsmgr; /*!< DNS refresh manager */
	struct ast_sockaddr addr;		 /*!< Who we connect to for registration purposes */
	int refresh;					 /*!< How often to refresh */
	struct ast_sockaddr us;			 /*!< Who the server thinks we are */
};

struct http_registry_req {
	AST_LIST_ENTRY(http_registry_req) entry;
	struct http_registry reg1;
	struct http_registry reg2;
	char username[80];
	char secret[80];			   /*!< Password or key name in []'s */
	char hostname[MAXHOSTNAMELEN]; /*!< Registration server Hostname */
	int hostport;				   /*!< Registration server Port */
	int iaxport;				   /*!< Our IAX2 bindport */
};

#define HTTP_REGISTRY_WAITING(reg) ((reg) && (reg)->req && !(reg)->dnsmgr && ((reg)->addr.ss.ss_family != AF_UNSPEC))
#define HTTP_REGISTRY_ENABLED(reg) ((reg) && (reg)->req && (reg)->dnsmgr)
#define HTTP_REGISTRY_READY(reg) ((reg) && (reg)->req && (reg)->dnsmgr && !ast_sockaddr_isnull(&(reg)->addr))

static AST_RWLIST_HEAD_STATIC(registrations, http_registry_req);

int register_dual_stack;

static int rpt_registry_match_family(const struct http_registry *reg, const char *family)
{
	if (!family) {
		return 1;
	}

	if (!strcasecmp(family, "ipv4")) {
		return ast_sockaddr_is_ipv4(&reg->us);
	}

	if (!strcasecmp(family, "ipv6")) {
		return ast_sockaddr_is_ipv6(&reg->us);
	}

	return 0;
}

static int rpt_registry_slot_registered(const struct http_registry *reg)
{
	if (!HTTP_REGISTRY_ENABLED(reg)) {
		return 0;
	}

	if (!reg->registered) {
		return 0;
	}

	if (ast_sockaddr_isnull(&reg->us)) {
		return 0;
	}

	return 1;
}

static int rpt_registry_read_one(const char *username, const char *item, const char *family, char *buf, size_t len)
{
	struct http_registry_req *req;
	const struct http_registry *match1 = NULL;
	const struct http_registry *match2 = NULL;
	int matches = 0;

	buf[0] = '\0';

	if (ast_strlen_zero(username)) {
		return -1;
	}

	if (!ast_strlen_zero(item) && strcasecmp(item, "address")) {
		return -1;
	}

	if (!ast_strlen_zero(family) && strcasecmp(family, "ipv4") && strcasecmp(family, "ipv6")) {
		return -1;
	}

	AST_RWLIST_RDLOCK(&registrations);
	AST_LIST_TRAVERSE(&registrations, req, entry) {
		if (strcmp(req->username, username)) {
			continue;
		}

		if (rpt_registry_slot_registered(&req->reg1) && rpt_registry_match_family(&req->reg1, family)) {
			match1 = &req->reg1;
			++matches;
		}

		if (rpt_registry_slot_registered(&req->reg2) && rpt_registry_match_family(&req->reg2, family)) {
			match2 = &req->reg2;
			++matches;
		}

		break;
	}
	AST_RWLIST_UNLOCK(&registrations);

	if (!matches) {
		return 0;
	}

	if (family) {
		ast_copy_string(buf, ast_sockaddr_stringify_addr(match1 ? &match1->us : &match2->us), len);
		return 0;
	}

	if (matches > 1) {
		ast_copy_string(buf, "MULTIPLE", len);
		return 0;
	}

	ast_copy_string(buf, ast_sockaddr_stringify_addr(match1 ? &match1->us : &match2->us), len);
	return 0;
}

static int function_rpt_registry_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char *parse;
	const char *item = "address";
	const char *family = NULL;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(username);
		AST_APP_ARG(item);
		AST_APP_ARG(family);
	);

	buf[0] = '\0';

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires a username\n", cmd);
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.username)) {
		ast_log(LOG_WARNING, "%s requires a username\n", cmd);
		return -1;
	}

	if (!ast_strlen_zero(args.item)) {
		item = args.item;
	}

	if (!ast_strlen_zero(args.family)) {
		family = args.family;
	}

	if (strcasecmp(item, "address")) {
		ast_log(LOG_WARNING, "%s: unsupported item '%s'\n", cmd, item);
		return -1;
	}

	if (family && strcasecmp(family, "ipv4") && strcasecmp(family, "ipv6")) {
		ast_log(LOG_WARNING, "%s: unsupported family '%s'\n", cmd, family);
		return -1;
	}

	return rpt_registry_read_one(args.username, item, family, buf, len);
}

static struct ast_custom_function rpt_registry_function = {
	.name = "RPT_REGISTRY",
	.read = function_rpt_registry_read,
};

/* from test_res_prometheus.c */
/*! \todo should be an ast_ function for this? */
static size_t curl_write_string_callback(char *contents, size_t size, size_t nmemb, void *userdata)
{
	struct ast_str **buffer = userdata;

	return ast_str_append(buffer, 0, "%.*s", (int) (size * nmemb), contents);
}

static struct ast_str *curl_post(struct http_registry *reg, const char *url, const char *header, const char *data)
{
	CURL *curl;
	CURLcode res;
	struct ast_str *str;
	long int http_code;
	struct curl_slist *slist = NULL;
	char curl_errbuf[CURL_ERROR_SIZE + 1] = "";

	str = ast_str_create(RPT_INITIAL_BUFFER_SIZE);
	if (!str) {
		return NULL;
	}

	curl = curl_easy_init();
	if (!curl) {
		ast_free(str);
		return NULL;
	}

	slist = curl_slist_append(slist, header);

	curl_easy_setopt(curl, CURLOPT_USERAGENT, AST_CURL_USER_AGENT);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &str);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, -1L);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
	curl_easy_setopt(curl, CURLOPT_POST, 1L); /* CURLOPT_HEADER and CURLOPT_NOBODY are implicit */
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, ast_sockaddr_ipv4(&reg->addr) ? (long) CURL_IPRESOLVE_V4 : (long) CURL_IPRESOLVE_V6);

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		curl_slist_free_all(slist);
		if (*curl_errbuf) {
			ast_log(LOG_WARNING, "%s\n", curl_errbuf);
		} else {
			ast_log(LOG_WARNING, "%s\n", curl_easy_strerror(res));
		}
		ast_log(LOG_WARNING, "Failed to curl URL '%s'\n", url);
		curl_easy_cleanup(curl);
		ast_free(str);
		return NULL;
	}

	curl_slist_free_all(slist);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_easy_cleanup(curl);

	if (http_code / 100 != 2) {
		ast_log(LOG_ERROR, "Failed to retrieve URL '%s': HTTP response code %ld\n", url, http_code);
		ast_free(str);
		return NULL;
	}

	ast_debug(3, "Response: %s\n", ast_str_buffer(str));
	return str;
}

static struct ast_json *register_to_json(struct http_registry *reg)
{
	return ast_json_pack("{s: s, s: s, s: i}", "node", reg->req->username, "passwd", reg->req->secret, "remote", 0);
}

static char *build_request_data(struct http_registry *reg)
{
	char *str;
	struct ast_json *json, *nodes, *node;

	node = ast_json_object_create();
	if (!node) {
		return NULL;
	}

	/* ast_json_object_set steals references (even on errors), so no need to free what we set */
	if (ast_json_object_set(node, reg->req->username, register_to_json(reg))) {
		ast_json_unref(node);
		return NULL;
	}
	nodes = ast_json_object_create();
	if (!nodes) {
		ast_json_unref(node);
		return NULL;
	}
	if (ast_json_object_set(nodes, "nodes", node)) {
		ast_json_unref(nodes);
		return NULL;
	}

	json = ast_json_object_create();
	if (!json) {
		ast_json_unref(nodes); /* includes nodes, plus stolen reference to node */
		return NULL;
	}

	if (reg->req->iaxport) {
		ast_json_object_set(json, "port", ast_json_integer_create(reg->req->iaxport)); /* Our IAX2 port */
	}
	if (ast_json_object_set(json, "data", nodes)) {
		ast_json_unref(json); /* Includes json, plus stolen references to nodes + node */
		return NULL;
	}

	str = ast_json_dump_string(json);
	ast_json_unref(json); /* Includes json, plus stolen references to nodes + node */
	return str;
}

static int http_register(struct http_registry *reg)
{
	struct ast_str *str;
	struct ast_str *url = ast_str_create(RPT_INITIAL_BUFFER_SIZE);
	char *data = build_request_data(reg);

	if (!url) {
		return -1;
	}

	if (!data) {
		ast_free(url);
		return -1;
	}

	if (reg->req->hostport) {
		ast_str_set(&url, 0, "https://%s:%d/", reg->req->hostname, reg->req->hostport);
	} else {
		ast_str_set(&url, 0, "https://%s/", reg->req->hostname);
	}

	ast_debug(2, "Making request to %s with data '%s'\n", ast_str_buffer(url), data);
	str = curl_post(reg, ast_str_buffer(url), "Content-Type: application/json", data);
	ast_json_free(data);
	ast_free(url);

	if (str) {
		const char *ipaddr;
		char *data;
		int port, refresh;
		struct ast_json *json = ast_json_load_string(ast_str_buffer(str), NULL);
		ast_debug(3, "Received response data: %s\n", ast_str_buffer(str));
		ast_free(str);
		if (json) {
			ipaddr = ast_json_object_string_get(json, "ipaddr");
			port = ast_json_integer_get(ast_json_object_get(json, "port"));
			refresh = ast_json_integer_get(ast_json_object_get(json, "refresh"));
			data = ast_json_dump_string(ast_json_object_get(json, "data"));
			ast_debug(2, "Response: ipaddr=%s, port=%d, refresh=%d, data=%s\n", ipaddr, port, refresh, data);
			if (data && strstr(data, "successfully registered")) {
				ast_sockaddr_parse(&reg->us, ipaddr, 0);
				ast_sockaddr_set_port(&reg->us, port);
				reg->refresh = refresh;
				reg->registered = 1;
			} else {
				reg->registered = 0;
			}
			if (data) {
				ast_json_free(data);
			}
			ast_json_unref(json); /* Only free after done using */
			return 0;
		}
	}

	return -1;
}

static pthread_t refresh_thread = AST_PTHREADT_NULL;
static ast_mutex_t refreshlock;
static ast_cond_t refresh_condition;
static int module_unloading = 0;

/*! \brief Based on iax2_register_set_port from chan_iax2 */
static void http_register_set_port(struct http_registry *reg)
{
	int port;

	port = ast_sockaddr_port(&reg->addr);
	if (!port && !reg->req->hostport) {
		port = 443; /* HTTPS */
	} else if (reg->req->hostport) {
		port = reg->req->hostport;
	}
	ast_sockaddr_set_port(&reg->addr, port);
}

/*! \brief Based on iax2_register_start from chan_iax2 */
static int http_register_start(struct http_registry *reg)
{
	int old_flags;
	int ret;

	old_flags = ast_sockaddr_resolve_flags_suppress(AST_SOCKADDR_RESOLVE_FLAG_SUPPRESS_EAI_NONAME_LOGS);
	ret = ast_dnsmgr_lookup(reg->req->hostname, &reg->addr, &reg->dnsmgr, NULL);
	ast_sockaddr_resolve_flags_set(old_flags);

	if (ret < 0) {
		return -1;
	}

	http_register_set_port(reg);

	reg->refresh = DEFAULT_REGISTER_INTERVAL;

	return 0;
}

/*! \brief Based on iax2_register_updated_binding from chan_iax2 */
static int http_register_updated_binding(struct http_registry *reg)
{
	char buf[8];
	int bound, old_bound;

	/* get current bindings from chan_iax2 */
	buf[0] = '\0';
	if ((ast_func_read(NULL, "IAXBINDING(family)", buf, sizeof(buf)) < 0) || ast_strlen_zero(buf)) {
		/* if "chan_iax2" module not available OR not bound */
		ast_copy_string(buf, "0", sizeof(buf));
	}

	old_bound = (reg->dnsmgr != NULL);
	switch (reg->addr.ss.ss_family) {
	case AF_INET:
		bound = !strcmp(buf, "ipv4") || !strcmp(buf, "both");
		break;
	case AF_INET6:
		bound = !strcmp(buf, "ipv6") || !strcmp(buf, "both");
		break;
	default:
		bound = 1;
	}

	return (!old_bound && bound);
}

/*! \brief Single thread to periodically do all registrations */
static void *do_refresh(void *varg)
{
	struct http_registry_req *req;

	for (;;) {
		struct timeval now = ast_tvnow();
		struct timespec ts = { 0 };

		ast_debug(3, "Doing periodic registrations\n");
		AST_RWLIST_RDLOCK(&registrations);
		AST_LIST_TRAVERSE(&registrations, req, entry) {
			int bound;

			if (HTTP_REGISTRY_WAITING(&req->reg1)) {
				// re-check binding and start
				bound = http_register_updated_binding(&req->reg1);
				if (bound) {
					http_register_start(&req->reg1);
				}
			}
			if (HTTP_REGISTRY_READY(&req->reg1)) {
				http_register(&req->reg1);
			}

			if (HTTP_REGISTRY_WAITING(&req->reg2)) {
				// re-check binding and start
				bound = http_register_updated_binding(&req->reg2);
				if (bound) {
					http_register_start(&req->reg2);
				}
			}
			if (HTTP_REGISTRY_READY(&req->reg2)) {
				http_register(&req->reg2);
			}
		}
		AST_RWLIST_UNLOCK(&registrations);

		ast_mutex_lock(&refreshlock);
		if (!module_unloading) {
			ts.tv_sec = (now.tv_sec + register_interval) + 1;
			ast_cond_timedwait(&refresh_condition, &refreshlock, &ts);
		}
		ast_mutex_unlock(&refreshlock);
		if (module_unloading) {
			break;
		}
	}

	return NULL;
}

#define FORMAT2 "%-47.47s  %-10.10s  %-47.47s %8.8s %s\n"
#define FORMAT "%-47.47s  %-10.10s  %-47.47s %8d %s\n"

/*! \brief Based on iax2_show_registry from chan_iax2 */
static void handle_show_registrations_one(struct ast_cli_args *a, struct http_registry *reg)
{
	char host[MAXHOSTNAMELEN];
	char perceived[AST_SOCKADDR_BUFLEN];

	ast_copy_string(host, ast_sockaddr_stringify(&reg->addr), sizeof(host));
	snprintf(perceived, sizeof(perceived), "%s", ast_sockaddr_isnull(&reg->us) ? "<Unregistered>" : ast_sockaddr_stringify(&reg->us));
	ast_cli(a->fd, FORMAT, host, reg->req->username, perceived, reg->refresh, reg->registered ? "Registered" : "Not Registered");
}

/*! \brief Based on iax2_show_registry from chan_iax2 */
static char *handle_show_registrations(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct http_registry_req *req = NULL;
	int counter = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt show registrations";
		e->usage = "Usage: rpt show registrations\n"
				   "       Lists all registration requests and status.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}
	ast_cli(a->fd, FORMAT2, "Host", "Username", "Perceived", "Refresh", "State");
	AST_RWLIST_RDLOCK(&registrations);
	AST_RWLIST_TRAVERSE(&registrations, req, entry) {
		if (HTTP_REGISTRY_READY(&req->reg1)) {
			handle_show_registrations_one(a, &req->reg1);
			counter++;
		}
		if (HTTP_REGISTRY_READY(&req->reg2)) {
			handle_show_registrations_one(a, &req->reg2);
			counter++;
		}
	}
	AST_RWLIST_UNLOCK(&registrations);
	ast_cli(a->fd, "%d HTTP registration%s.\n", counter, ESS(counter));
	return CLI_SUCCESS;
}

#undef FORMAT
#undef FORMAT2

static struct ast_cli_entry rpt_http_cli[] = {
	AST_CLI_DEFINE(handle_show_registrations, "Display status of registrations"),
};

#define IAX_CONFIG_FILE "iax.conf"

/*! \brief Query iax.conf for the current bindport */
static int get_bindport(void)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { 0 };
	const char *varval;

	if (!(cfg = ast_config_load(IAX_CONFIG_FILE, config_flags))) {
		ast_log(LOG_WARNING, "Config file %s not found, declining to load\n", IAX_CONFIG_FILE);
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file %s is in an invalid format. Aborting.\n", IAX_CONFIG_FILE);
		return -1;
	}

	/* general section */
	if ((varval = ast_variable_retrieve(cfg, "general", "bindport")) && !ast_strlen_zero(varval)) {
		int tmp;
		if (ast_str_to_int(varval, &tmp)) {
			ast_log(LOG_WARNING, "Invalid request interval, defaulting to %d\n", register_interval);
		} else {
			ast_config_destroy(cfg);
			ast_debug(2, "Our IAX2 bindport is %d\n", tmp);
			return tmp;
		}
	}
	ast_config_destroy(cfg);
	return -1;
}

/*! \brief Based on iax2_append_register_one from chan_iax2 */
static int http_append_register_one(struct http_registry_req *req, struct http_registry *reg, int family)
{
	int bound;
	int ret;

	reg->addr.ss.ss_family = family;
	reg->req = req;

	bound = http_register_updated_binding(reg);
	if (!bound) {
		return 0;
	}

	/* we have a binding, start dnsmgr */
	ret = http_register_start(reg);

	return ret;
}

/*! \brief Based on iax2_append_register from chan_iax2 */
static int http_append_register(const char *hostname, const char *username, const char *secret, const char *porta)
{
	struct http_registry_req *req;
	static int iaxport;
	int nf;

	if (!(req = ast_calloc(1, sizeof(*req)))) {
		return -1;
	}

	ast_copy_string(req->username, username, sizeof(req->username));

	ast_copy_string(req->hostname, hostname, sizeof(req->hostname));

	if (secret) {
		ast_copy_string(req->secret, secret, sizeof(req->secret));
	}

	if (porta) {
		sscanf(porta, "%5d", &req->hostport);
	}

	iaxport = get_bindport();
	if (iaxport > 0) {
		req->iaxport = iaxport;
	}

	if (http_append_register_one(req, &req->reg1, register_dual_stack ? AF_INET : AF_UNSPEC) < 0) {
		ast_free(req);
		return -1;
	}
	if (register_dual_stack) {
		if (http_append_register_one(req, &req->reg2, AF_INET6) < 0) {
			ast_dnsmgr_release(req->reg1.dnsmgr);
			ast_free(req);
			return -1;
		}
	}

	nf = (!HTTP_REGISTRY_WAITING(&req->reg1) && !HTTP_REGISTRY_READY(&req->reg1));
	if (nf && register_dual_stack) {
		nf = (!HTTP_REGISTRY_WAITING(&req->reg2) && !HTTP_REGISTRY_READY(&req->reg2));
	}
	if (nf) {
		/*
		 * if we are not waiting on bindings, if we've issued DNS queries, and if we
		 * have not been able to resolve the server hostname
		 */
		ast_log(LOG_WARNING, "HTTP Registration server not available: '%s'\n", req->hostname);
	}

	AST_RWLIST_WRLOCK(&registrations);
	AST_LIST_INSERT_HEAD(&registrations, req, entry);
	AST_RWLIST_UNLOCK(&registrations);

	return 0;
}

/*! \brief Based on chan_iax2's iax2_register */
static int parse_register(const char *value, int lineno)
{
	char copy[256];
	char *username, *hostname, *secret;
	char *porta;
	char *stringp = NULL;

	ast_copy_string(copy, value, sizeof(copy));
	stringp = copy;
	username = strsep(&stringp, "@");
	hostname = strsep(&stringp, "@");

	if (!hostname) {
		ast_log(LOG_WARNING, "Format for registration is user[:secret]@host[:port] at line %d\n", lineno);
		return -1;
	}

	stringp = username;
	username = strsep(&stringp, ":");
	secret = strsep(&stringp, ":");
	stringp = hostname;
	hostname = strsep(&stringp, ":");
	porta = strsep(&stringp, ":");

	if (porta && !atoi(porta)) {
		ast_log(LOG_WARNING, "%s is not a valid port number at line %d\n", porta, lineno);
		return -1;
	}

	ast_debug(1, "Loaded HTTP registration: %s\n", value);
	return http_append_register(hostname, username, secret, porta);
}

static void cleanup_registrations(void)
{
	struct http_registry_req *req;

	/* Remove all existing ones. */
	AST_RWLIST_WRLOCK(&registrations);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&registrations, req, entry) {
		AST_LIST_REMOVE_CURRENT(entry);
		if (HTTP_REGISTRY_ENABLED(&req->reg1)) {
			if (req->reg1.dnsmgr) {
				ast_dnsmgr_release(req->reg1.dnsmgr);
			}
		}
		if (HTTP_REGISTRY_ENABLED(&req->reg2)) {
			if (req->reg2.dnsmgr) {
				ast_dnsmgr_release(req->reg2.dnsmgr);
			}
		}
		ast_free(req);
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&registrations);
}

static int load_config(int reload)
{
	const char *cat = NULL;
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_variable *var;
	const char *varval;

	if (!(cfg = ast_config_load(CONFIG_FILE, config_flags))) {
		ast_log(LOG_WARNING, "Config file %s not found, declining to load\n", CONFIG_FILE);
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		ast_debug(1, "Config file %s unchanged, skipping\n", CONFIG_FILE);
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file %s is in an invalid format. Aborting.\n", CONFIG_FILE);
		return -1;
	}

	/* Set (or reset) defaults. */
	register_interval = DEFAULT_REGISTER_INTERVAL;

	/* general section */
	if ((varval = ast_variable_retrieve(cfg, "general", "register_interval")) && !ast_strlen_zero(varval)) {
		int tmp;
		if (ast_str_to_int(varval, &tmp)) {
			ast_log(LOG_WARNING, "Invalid request interval, defaulting to %d\n", register_interval);
		} else {
			register_interval = tmp;
		}
	}
	ast_debug(3, "Registration interval: %d\n", register_interval);

	register_dual_stack = 0;
	if ((varval = ast_variable_retrieve(cfg, "general", "register_dual_stack")) && !ast_strlen_zero(varval)) {
		if (ast_true(varval)) {
			register_dual_stack = 1;
		} else if (ast_false(varval)) {
			register_dual_stack = 0;
		} else {
			ast_log(LOG_WARNING, "Invalid register_dual_stack value\n");
		}
	}
	ast_debug(3, "Dual stack registration interval: %s\n", register_dual_stack ? "yes" : "no");

	if (reload) {
		cleanup_registrations();
	}

	while ((cat = ast_category_browse(cfg, cat))) {
		/* We only have the general section currently. */
		if (!strcasecmp(cat, "general")) {
			continue;
		}

		/* registrations is the only remaining section */
		if (strcasecmp(cat, "registrations")) {
			ast_log(LOG_WARNING, "Invalid config section: %s\n", cat);
			continue;
		}

		var = ast_variable_browse(cfg, cat);
		while (var) {
			if (!strcasecmp(var->name, "register") && !ast_strlen_zero(var->value)) {
				parse_register(var->value, var->lineno); /* iax.conf style register string */
			} else if (!strcasecmp(var->name, "register_dual_stack") && !ast_strlen_zero(var->value)) {
				if (ast_true(var->value)) {
					register_dual_stack = 1;
				} else if (ast_false(var->value)) {
					register_dual_stack = 0;
				} else {
					ast_log(LOG_WARNING, "Invalid register_dual_stack value at line %d.\n", var->lineno);
				}
			} else {
				ast_log(LOG_WARNING, "Unknown setting at line %d: '%s'\n", var->lineno, var->name);
			}
			var = var->next;
		}
	}

	ast_config_destroy(cfg);

	if (reload) {
		ast_cond_signal(&refresh_condition);
	}
	return 0;
}

static int reload_module(void)
{
	return load_config(1);
}

static int load_module(void)
{
	if (load_config(0)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_mutex_init(&refreshlock);
	ast_cond_init(&refresh_condition, NULL);

	if (ast_pthread_create(&refresh_thread, NULL, do_refresh, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to start refresh thread\n");
		ast_mutex_destroy(&refreshlock);
		ast_cond_destroy(&refresh_condition);
		cleanup_registrations();
		ast_mutex_destroy(&refreshlock);
		ast_cond_destroy(&refresh_condition);
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_cli_register_multiple(rpt_http_cli, ARRAY_LEN(rpt_http_cli));

	ast_custom_function_register(&rpt_registry_function);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_custom_function_unregister(&rpt_registry_function);

	ast_cli_unregister_multiple(rpt_http_cli, ARRAY_LEN(rpt_http_cli));

	ast_mutex_lock(&refreshlock);
	module_unloading = 1;
	ast_cond_signal(&refresh_condition);
	ast_mutex_unlock(&refreshlock);

	pthread_join(refresh_thread, NULL);

	cleanup_registrations();
	ast_mutex_destroy(&refreshlock);
	ast_cond_destroy(&refresh_condition);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "RPT HTTP Periodic Registrations",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.requires = "res_curl, chan_iax2",
);
