
#include "asterisk.h"

#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "asterisk/channel.h"	/* required for AST_MAX_EXTENSION via app_rpt.h */
#include "asterisk/config.h"
#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/strings.h"
#include "asterisk/utils.h"

#include "app_rpt.h"
#include "rpt_auth.h"
#include "rpt_lock.h"
#include "rpt_totp.h"

#define RPT_AUTH_USER_ID_LEN 4
#define RPT_AUTH_OTP_LEN 6
#define RPT_AUTH_MAX_USERS 64
#define RPT_AUTH_USERS_SECTION "users"

#define RPT_AUTH_DEFAULT_TIMEOUT 300
#define RPT_AUTH_DEFAULT_LOCKOUT_THRESHOLD 5
#define RPT_AUTH_DEFAULT_LOCKOUT_DURATION 60
#define RPT_AUTH_DEFAULT_OTP_STEP 30
#define RPT_AUTH_DEFAULT_OTP_WINDOW 1

struct rpt_auth_user {
	char id[RPT_AUTH_USER_ID_LEN + 1];
	char *secret;
	char *command_set;
	uint64_t last_counter;
	int fail_count;
	time_t lockout_until;
};

struct rpt_auth_state {
	struct rpt_auth_user users[RPT_AUTH_MAX_USERS];
	int nusers;

	int session_active;
	char session_user[RPT_AUTH_USER_ID_LEN + 1];
	char *session_command_set;
	int session_longestfunc;
	time_t session_expires;
};

static int effective_timeout(struct rpt *myrpt)
{
	int v = myrpt->p.auth_timeout;
	if (v <= 0) {
		v = RPT_AUTH_DEFAULT_TIMEOUT;
	}
	if (v < 30) {
		v = 30;
	}
	if (v > 86400) {
		v = 86400;
	}
	return v;
}

static int effective_lockout_threshold(struct rpt *myrpt)
{
	int v = myrpt->p.auth_lockout_threshold;
	if (v < 0) {
		return RPT_AUTH_DEFAULT_LOCKOUT_THRESHOLD;
	}
	if (v > 1000) {
		v = 1000;
	}
	return v;
}

static int effective_lockout_duration(struct rpt *myrpt)
{
	int v = myrpt->p.auth_lockout_duration;
	if (v <= 0) {
		v = RPT_AUTH_DEFAULT_LOCKOUT_DURATION;
	}
	if (v > 86400) {
		v = 86400;
	}
	return v;
}

static int effective_otp_step(struct rpt *myrpt)
{
	int v = myrpt->p.auth_otp_step;
	if (v <= 0) {
		v = RPT_AUTH_DEFAULT_OTP_STEP;
	}
	if (v < 10) {
		v = 10;
	}
	if (v > 120) {
		v = 120;
	}
	return v;
}

static int effective_otp_window(struct rpt *myrpt)
{
	int v = myrpt->p.auth_otp_window;
	if (v < 0) {
		v = RPT_AUTH_DEFAULT_OTP_WINDOW;
	}
	if (v > 3) {
		v = 3;
	}
	return v;
}

static void clear_session_locked(struct rpt_auth_state *st)
{
	st->session_active = 0;
	st->session_user[0] = '\0';
	if (st->session_command_set) {
		ast_free(st->session_command_set);
		st->session_command_set = NULL;
	}
	st->session_longestfunc = 0;
	st->session_expires = 0;
}

static void free_users_locked(struct rpt_auth_state *st)
{
	int i;
	for (i = 0; i < st->nusers; i++) {
		if (st->users[i].secret) {
			ast_free(st->users[i].secret);
			st->users[i].secret = NULL;
		}
		if (st->users[i].command_set) {
			ast_free(st->users[i].command_set);
			st->users[i].command_set = NULL;
		}
	}
	st->nusers = 0;
}

static struct rpt_auth_user *find_user_locked(struct rpt_auth_state *st, const char *user_id4)
{
	int i;
	for (i = 0; i < st->nusers; i++) {
		if (!strcmp(st->users[i].id, user_id4)) {
			return &st->users[i];
		}
	}
	return NULL;
}

static int parse_user_value(const char *value, char **out_secret, char **out_set)
{
	const char *comma;
	const char *secret_start, *secret_end;
	const char *set_start, *set_end;
	size_t slen, clen;

	if (!value) {
		return -1;
	}
	comma = strchr(value, ',');
	if (!comma) {
		return -1;
	}

	secret_start = value;
	while (*secret_start == ' ' || *secret_start == '\t') {
		secret_start++;
	}
	secret_end = comma;
	while (secret_end > secret_start && (secret_end[-1] == ' ' || secret_end[-1] == '\t')) {
		secret_end--;
	}
	set_start = comma + 1;
	while (*set_start == ' ' || *set_start == '\t') {
		set_start++;
	}
	set_end = set_start + strlen(set_start);
	while (set_end > set_start && (set_end[-1] == ' ' || set_end[-1] == '\t')) {
		set_end--;
	}

	slen = (size_t) (secret_end - secret_start);
	clen = (size_t) (set_end - set_start);
	if (slen == 0 || clen == 0) {
		return -1;
	}

	*out_secret = ast_malloc(slen + 1);
	*out_set = ast_malloc(clen + 1);
	if (!*out_secret || !*out_set) {
		ast_free(*out_secret);
		ast_free(*out_set);
		*out_secret = NULL;
		*out_set = NULL;
		return -1;
	}
	memcpy(*out_secret, secret_start, slen);
	(*out_secret)[slen] = '\0';
	memcpy(*out_set, set_start, clen);
	(*out_set)[clen] = '\0';
	return 0;
}

static int valid_user_id(const char *name)
{
	int i;
	if (strlen(name) != RPT_AUTH_USER_ID_LEN) {
		return 0;
	}
	for (i = 0; i < RPT_AUTH_USER_ID_LEN; i++) {
		if (name[i] < '0' || name[i] > '9') {
			return 0;
		}
	}
	return 1;
}

static void load_users_locked(struct rpt *myrpt, struct rpt_auth_state *st)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { 0 };
	struct ast_variable *vp;

	free_users_locked(st);

	if (ast_strlen_zero(myrpt->p.auth_users)) {
		return;
	}

	cfg = ast_config_load(myrpt->p.auth_users, config_flags);
	if (!cfg || cfg == CONFIG_STATUS_FILEINVALID || cfg == CONFIG_STATUS_FILEUNCHANGED) {
		if (cfg == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_WARNING, "rpt_auth: cannot parse %s\n", myrpt->p.auth_users);
		} else if (!cfg) {
			ast_log(LOG_WARNING, "rpt_auth: cannot open %s\n", myrpt->p.auth_users);
		}
		return;
	}

	for (vp = ast_variable_browse(cfg, RPT_AUTH_USERS_SECTION); vp; vp = vp->next) {
		char *secret = NULL;
		char *cmdset = NULL;

		if (st->nusers >= RPT_AUTH_MAX_USERS) {
			ast_log(LOG_WARNING, "rpt_auth: %s has more than %d users; truncating\n",
				myrpt->p.auth_users, RPT_AUTH_MAX_USERS);
			break;
		}
		if (!valid_user_id(vp->name)) {
			ast_log(LOG_WARNING, "rpt_auth: skipping malformed user id '%s'\n", vp->name);
			continue;
		}
		if (parse_user_value(vp->value, &secret, &cmdset) != 0) {
			ast_log(LOG_WARNING, "rpt_auth: skipping user %s: bad value (need 'secret,command_set')\n",
				vp->name);
			continue;
		}

		ast_copy_string(st->users[st->nusers].id, vp->name, sizeof(st->users[st->nusers].id));
		st->users[st->nusers].secret = secret;
		st->users[st->nusers].command_set = cmdset;
		st->users[st->nusers].last_counter = 0;
		st->users[st->nusers].fail_count = 0;
		st->users[st->nusers].lockout_until = 0;
		st->nusers++;
	}

	ast_config_destroy(cfg);
	ast_log(LOG_NOTICE, "rpt_auth: loaded %d user(s) from %s for node %s\n",
		st->nusers, myrpt->p.auth_users, myrpt->name);
}

static int compute_longestfunc_locked(struct rpt *myrpt, const char *stanza)
{
	struct ast_variable *vp;
	int longest = 0;
	if (!myrpt->cfg || !stanza) {
		return 0;
	}
	for (vp = ast_variable_browse(myrpt->cfg, stanza); vp; vp = vp->next) {
		int j = (int) strlen(vp->name);
		if (j > longest) {
			longest = j;
		}
	}
	return longest;
}

static struct rpt_auth_state *ensure_state_locked(struct rpt *myrpt)
{
	if (!myrpt->auth) {
		myrpt->auth = ast_calloc(1, sizeof(*myrpt->auth));
	}
	return myrpt->auth;
}

void rpt_auth_reload(struct rpt *myrpt)
{
	struct rpt_auth_state *st;

	rpt_mutex_lock(&myrpt->lock);
	st = ensure_state_locked(myrpt);
	if (!st) {
		rpt_mutex_unlock(&myrpt->lock);
		return;
	}
	clear_session_locked(st);
	load_users_locked(myrpt, st);
	rpt_mutex_unlock(&myrpt->lock);
}

void rpt_auth_free(struct rpt *myrpt)
{
	struct rpt_auth_state *st;

	rpt_mutex_lock(&myrpt->lock);
	st = myrpt->auth;
	if (st) {
		clear_session_locked(st);
		free_users_locked(st);
		ast_free(st);
		myrpt->auth = NULL;
	}
	rpt_mutex_unlock(&myrpt->lock);
}

const char *rpt_auth_active_set(struct rpt *myrpt)
{
	struct rpt_auth_state *st;
	const char *ret = NULL;
	time_t now = time(NULL);

	rpt_mutex_lock(&myrpt->lock);
	st = myrpt->auth;
	if (st && st->session_active) {
		if (now >= st->session_expires) {
			ast_log(LOG_NOTICE, "rpt_auth: session for user %s expired on node %s\n",
				st->session_user, myrpt->name);
			clear_session_locked(st);
		} else {
			ret = st->session_command_set;
		}
	}
	rpt_mutex_unlock(&myrpt->lock);
	return ret;
}

void rpt_auth_touch(struct rpt *myrpt)
{
	struct rpt_auth_state *st;
	time_t now = time(NULL);

	rpt_mutex_lock(&myrpt->lock);
	st = myrpt->auth;
	if (st && st->session_active) {
		st->session_expires = now + effective_timeout(myrpt);
	}
	rpt_mutex_unlock(&myrpt->lock);
}

int rpt_auth_longestfunc(struct rpt *myrpt)
{
	struct rpt_auth_state *st;
	int ret = 0;

	rpt_mutex_lock(&myrpt->lock);
	st = myrpt->auth;
	if (st && st->session_active) {
		ret = st->session_longestfunc;
	}
	rpt_mutex_unlock(&myrpt->lock);
	return ret;
}

int rpt_auth_login(struct rpt *myrpt, const char *user_id4, const char *otp6)
{
	struct rpt_auth_state *st;
	struct rpt_auth_user *u;
	int rc;
	time_t now = time(NULL);
	int threshold, duration, step, window;

	if (!user_id4 || !otp6) {
		return RPT_AUTH_LOGIN_BAD;
	}

	rpt_mutex_lock(&myrpt->lock);
	st = myrpt->auth;
	if (!st || st->nusers == 0 || ast_strlen_zero(myrpt->p.auth_users)) {
		rpt_mutex_unlock(&myrpt->lock);
		return RPT_AUTH_LOGIN_DISABLED;
	}

	threshold = effective_lockout_threshold(myrpt);
	duration = effective_lockout_duration(myrpt);
	step = effective_otp_step(myrpt);
	window = effective_otp_window(myrpt);

	u = find_user_locked(st, user_id4);
	if (!u) {
		rpt_mutex_unlock(&myrpt->lock);
		ast_log(LOG_NOTICE, "rpt_auth: login attempt for unknown user %s on node %s\n",
			user_id4, myrpt->name);
		return RPT_AUTH_LOGIN_BAD;
	}

	if (u->lockout_until > now) {
		long remaining = (long) (u->lockout_until - now);
		rpt_mutex_unlock(&myrpt->lock);
		ast_log(LOG_NOTICE, "rpt_auth: user %s locked out for %ld more seconds on node %s\n",
			user_id4, remaining, myrpt->name);
		return RPT_AUTH_LOGIN_LOCKED;
	}

	rc = rpt_totp_verify(u->secret, otp6, &u->last_counter, now, step, window);
	if (rc != RPT_TOTP_OK) {
		u->fail_count++;
		if (threshold > 0 && u->fail_count >= threshold) {
			u->lockout_until = now + duration;
			u->fail_count = 0;
			ast_log(LOG_WARNING, "rpt_auth: user %s locked out for %d s on node %s\n",
				user_id4, duration, myrpt->name);
		}
		rpt_mutex_unlock(&myrpt->lock);
		ast_log(LOG_NOTICE, "rpt_auth: login failed for user %s on node %s (totp rc=%d)\n",
			user_id4, myrpt->name, rc);
		return RPT_AUTH_LOGIN_BAD;
	}

	clear_session_locked(st);
	st->session_active = 1;
	ast_copy_string(st->session_user, u->id, sizeof(st->session_user));
	st->session_command_set = ast_strdup(u->command_set);
	if (!st->session_command_set) {
		clear_session_locked(st);
		rpt_mutex_unlock(&myrpt->lock);
		return RPT_AUTH_LOGIN_BAD;
	}
	st->session_longestfunc = compute_longestfunc_locked(myrpt, u->command_set);
	st->session_expires = now + effective_timeout(myrpt);
	u->fail_count = 0;
	rpt_mutex_unlock(&myrpt->lock);

	ast_log(LOG_NOTICE, "rpt_auth: user %s authenticated on node %s, set=%s\n",
		user_id4, myrpt->name, u->command_set);
	return RPT_AUTH_LOGIN_OK;
}

void rpt_auth_logout(struct rpt *myrpt)
{
	struct rpt_auth_state *st;
	char user[RPT_AUTH_USER_ID_LEN + 1] = "";
	int was_active = 0;

	rpt_mutex_lock(&myrpt->lock);
	st = myrpt->auth;
	if (st && st->session_active) {
		ast_copy_string(user, st->session_user, sizeof(user));
		was_active = 1;
		clear_session_locked(st);
	}
	rpt_mutex_unlock(&myrpt->lock);

	if (was_active) {
		ast_log(LOG_NOTICE, "rpt_auth: user %s logged out on node %s\n", user, myrpt->name);
	}
}

void rpt_auth_status(struct rpt *myrpt, char *outbuf, size_t outlen)
{
	struct rpt_auth_state *st;
	time_t now = time(NULL);

	if (!outbuf || outlen == 0) {
		return;
	}
	outbuf[0] = '\0';

	rpt_mutex_lock(&myrpt->lock);
	st = myrpt->auth;
	if (!st || ast_strlen_zero(myrpt->p.auth_users)) {
		snprintf(outbuf, outlen, "auth disabled");
	} else if (!st->session_active) {
		snprintf(outbuf, outlen, "no active session (%d user(s) configured)", st->nusers);
	} else if (now >= st->session_expires) {
		snprintf(outbuf, outlen, "session expired");
	} else {
		snprintf(outbuf, outlen, "user=%s set=%s remaining=%lds",
			st->session_user,
			st->session_command_set ? st->session_command_set : "(none)",
			(long) (st->session_expires - now));
	}
	rpt_mutex_unlock(&myrpt->lock);
}
