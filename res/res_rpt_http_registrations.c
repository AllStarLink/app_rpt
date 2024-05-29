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

/*! \file
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

/*** DOCUMENTATION
	<configInfo name="res_rpt_http_registrations" language="en_US">
		<synopsis>Periodic HTTP registrations (DDNS-like functionality)</synopsis>
		<configFile name="rpt_http_registrations.conf">
			<configObject name="general">
				<configOption name="register_interval" default="60">
					<synopsis>Time in seconds between registration attempts</synopsis>
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
 ***/

#define CONFIG_FILE "rpt_http_registrations.conf"

/*! \brief Default register interval is once per minute */
#define DEFAULT_REGISTER_INTERVAL 60

static int register_interval;

struct http_registry {
	/* Registration info */
	struct ast_sockaddr addr;	/*!< Who we connect to for registration purposes */
	char username[80];
	char secret[80];			/*!< Password or key name in []'s */
	int refresh;				/*!< How often to refresh */
	char registered;			/*!< Registered==1 */
	struct ast_sockaddr us;			/*!< Who the server thinks we are */
	struct ast_dnsmgr_entry *dnsmgr;	/*!< DNS refresh manager */
	char perceived[80];
	int perceived_port;
	AST_LIST_ENTRY(http_registry) entry;
	int port;					/*!< Port of server for registration */
	int iaxport;				/*!< Our IAX2 bindport */
	char hostname[];
};

static AST_RWLIST_HEAD_STATIC(registrations, http_registry);

/* from test_res_prometheus.c */
/*! \todo should be an ast_ function for this? */
static size_t curl_write_string_callback(void *contents, size_t size, size_t nmemb, void *userdata)
{
	struct ast_str **buffer = userdata;
	size_t realsize = size * nmemb;
	char *rawdata;

	rawdata = ast_malloc(realsize + 1);
	if (!rawdata) {
		return 0;
	}
	memcpy(rawdata, contents, realsize);
	rawdata[realsize] = 0;
	ast_str_append(buffer, 0, "%s", rawdata);
	ast_free(rawdata);

	return realsize;
}

static struct ast_str *curl_post(const char *url, const char *header, const char *data)
{
	CURL **curl;
	struct ast_str *str;
	long int http_code;
	struct curl_slist *slist = NULL;
	char curl_errbuf[CURL_ERROR_SIZE + 1] = "";

	str = ast_str_create(512);
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
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);

	if (curl_easy_perform(curl) != CURLE_OK) {
		curl_slist_free_all(slist);
		if (*curl_errbuf) {
			ast_log(LOG_WARNING, "%s\n", curl_errbuf);
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
	return ast_json_pack("{s: s, s: s, s: i}",
		"node", reg->username,
		"passwd", reg->secret,
		"remote", 0);
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
	if (ast_json_object_set(node, reg->username, register_to_json(reg))) {
		return NULL;
	}
	nodes = ast_json_object_create();
	if (!nodes) {
		ast_json_unref(node);
		return NULL;
	}
	if (ast_json_object_set(nodes, "nodes", node)) {
		return NULL;
	}

	json = ast_json_object_create();
	if (!json) {
		ast_json_unref(nodes); /* includes nodes, plus stolen reference to node */
		return NULL;
	}

	if (reg->iaxport) {
		ast_json_object_set(json, "port", ast_json_integer_create(reg->iaxport)); /* Our IAX2 port */
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
	char url[256];
	char *data = build_request_data(reg);

	if (!data) {
		return -1;
	}

	if (reg->port) {
		snprintf(url, sizeof(url), "https://%s:%d/", reg->hostname, reg->port); /* Registrar's HTTPS port */
	} else {
		snprintf(url, sizeof(url), "https://%s/", reg->hostname);
	}

	ast_debug(2, "Making request to %s with data '%s'\n", url, data);
	str = curl_post(url, "Content-Type: application/json", data);
	ast_json_free(data);

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
			if (strstr(data, "successfully registered")) {
				ast_copy_string(reg->perceived, ipaddr, sizeof(reg->perceived));
				reg->perceived_port = port;
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

/*! \brief Single thread to periodically do all registrations */
static void *do_refresh(void *varg)
{
	struct http_registry *reg;

	for (;;) {
		struct timeval now = ast_tvnow();
		struct timespec ts = {0,};

		ast_debug(3, "Doing periodic registrations\n");
		AST_RWLIST_RDLOCK(&registrations);
		AST_LIST_TRAVERSE(&registrations, reg, entry) {
			http_register(reg);
		}
		AST_RWLIST_UNLOCK(&registrations);

		ast_mutex_lock(&refreshlock);
		ts.tv_sec = (now.tv_sec + register_interval) + 1;
		ast_cond_timedwait(&refresh_condition, &refreshlock, &ts);
		ast_mutex_unlock(&refreshlock);

		if (module_unloading) {
			break;
		}
	}

	return NULL;
}

/*! \brief Based on iax2_show_registry from chan_iax2 */
static char *handle_show_registrations(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT2 "%-45.45s  %-10.10s  %-35.35s %8.8s  %s\n"
#define FORMAT  "%-45.45s  %-10.10s  %-35.35s %8d  %s\n"

	struct http_registry *reg = NULL;
	char host[80];
	char perceived[95];
	int counter = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt show registrations";
		e->usage =
			"Usage: rpt show registrations\n"
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
	AST_RWLIST_TRAVERSE(&registrations, reg, entry) {
		if (!ast_sockaddr_isnull(&reg->addr)) {
			snprintf(perceived, sizeof(perceived), "%s:%d", reg->perceived, reg->perceived_port);
		} else {
			ast_copy_string(perceived, "<Unregistered>", sizeof(perceived));
		}
		snprintf(host, sizeof(host), "%s", ast_sockaddr_stringify(&reg->addr));
		ast_cli(a->fd, FORMAT, host, reg->username, reg->perceived_port ? perceived : "<Unregistered>", reg->refresh, reg->registered ? "Registered" : "Not Registered");
		counter++;
	}
	AST_RWLIST_UNLOCK(&registrations);
	ast_cli(a->fd, "%d HTTP registration%s.\n", counter, ESS(counter));
	return CLI_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

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

/*! \brief Based on iax2_append_register from chan_iax2 */
static int append_register(const char *hostname, const char *username, const char *secret, const char *porta)
{
	struct http_registry *reg;
	static int iaxport = 0;

	if (!(reg = ast_calloc(1, sizeof(*reg) + strlen(hostname) + 1))) {
		return -1;
	}

	reg->addr.ss.ss_family = AST_AF_UNSPEC;
	if (ast_dnsmgr_lookup(hostname, &reg->addr, &reg->dnsmgr, NULL) < 0) {
		ast_free(reg);
		return -1;
	}

	ast_copy_string(reg->username, username, sizeof(reg->username));
	strcpy(reg->hostname, hostname); /* Note: This is safe */

	if (secret) {
		ast_copy_string(reg->secret, secret, sizeof(reg->secret));
	}

	reg->refresh = 0;
	reg->port = 0;

	if (!porta && !reg->port) {
		reg->port = 0; /* Our IAX port (default 4569) */
	} else if (porta) {
		sscanf(porta, "%5d", &reg->port);
	}

	ast_sockaddr_set_port(&reg->addr, 443); /* Registrar's HTTPS port */

	if (!iaxport) {
		iaxport = get_bindport();
	}

	if (iaxport > 0) {
		reg->iaxport = iaxport;
	}

	AST_RWLIST_WRLOCK(&registrations);
	AST_LIST_INSERT_HEAD(&registrations, reg, entry);
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
	return append_register(hostname, username, secret, porta);
}

static void cleanup_registrations(void)
{
	struct http_registry *reg;
	/* Remove all existing ones. */
	AST_RWLIST_WRLOCK(&registrations);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&registrations, reg, entry) {
		AST_LIST_REMOVE_CURRENT(entry);
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

	if (ast_pthread_create_background(&refresh_thread, NULL, do_refresh, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to start refresh thread\n");
		cleanup_registrations();
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_cli_register_multiple(rpt_http_cli, ARRAY_LEN(rpt_http_cli));
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_cli_unregister_multiple(rpt_http_cli, ARRAY_LEN(rpt_http_cli));

	ast_mutex_lock(&refreshlock);
	module_unloading = 1;
	ast_cond_signal(&refresh_condition);
	ast_mutex_unlock(&refreshlock);

	pthread_join(refresh_thread, NULL);

	cleanup_registrations();
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "RPT HTTP Periodic Registrations",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.requires = "res_curl",
);
