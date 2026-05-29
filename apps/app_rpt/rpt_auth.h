/*
 * Per-user TOTP authentication for app_rpt.
 *
 * Loads users + secrets from a separate rpt_auth.conf file, manages a single
 * per-node session slot, enforces sliding timeout and per-user lockout, and
 * exposes the active command-set stanza name to collect_function_digits().
 *
 * All public functions take their own lock on myrpt->lock; callers must NOT
 * hold the lock when invoking them.
 */

#ifndef _RPT_AUTH_H
#define _RPT_AUTH_H

#include <stddef.h>

struct rpt;

/*! \brief Re-read auth_users file and clear any active session.
 *  Called from load_rpt_vars() after the new rpt.conf vars are populated.
 *  Safe to call when the feature is disabled (auth_users unset).  Idempotent. */
void rpt_auth_reload(struct rpt *myrpt);

/*! \brief Free all auth state for a node.  Called at module unload / node teardown. */
void rpt_auth_free(struct rpt *myrpt);

/*! \brief Copy the active command-set stanza name into buf, or return 0 if no session.
 *  Returns 1 if an active session exists (buf populated), 0 otherwise.
 *  Copies under lock so the returned string is caller-owned and safe from
 *  concurrent rpt_auth_logout / rpt_auth_reload on the CLI thread.
 *  Internally checks expiry and clears stale sessions. */
int rpt_auth_get_active_stanza(struct rpt *myrpt, char *buf, size_t buflen);

/*! \brief Refresh the sliding session timeout.  No-op if no active session. */
void rpt_auth_touch(struct rpt *myrpt);

/*! \brief Longest function-name length in the active session's stanza.
 *  Returns 0 if no active session.  Used by the "have we collected enough
 *  digits?" gate in collect_function_digits(). */
int rpt_auth_longestfunc(struct rpt *myrpt);

/*! \brief Result codes from rpt_auth_login. */
enum rpt_auth_login_result {
	RPT_AUTH_LOGIN_OK = 0,
	RPT_AUTH_LOGIN_BAD = -1,        /*!< unknown user, bad OTP, replay, or bad secret */
	RPT_AUTH_LOGIN_LOCKED = -2,     /*!< user is currently locked out */
	RPT_AUTH_LOGIN_DISABLED = -3,   /*!< feature disabled (no auth_users) */
};

/*! \brief Attempt login.
 *  \param user_id4 exactly 4 ASCII decimal digits
 *  \param otp6    exactly 6 ASCII decimal digits */
int rpt_auth_login(struct rpt *myrpt, const char *user_id4, const char *otp6);

/*! \brief Clear active session.  Idempotent. */
void rpt_auth_logout(struct rpt *myrpt);

/*! \brief Format human-readable status into outbuf.  Always NUL-terminates. */
void rpt_auth_status(struct rpt *myrpt, char *outbuf, size_t outlen);

#endif /* _RPT_AUTH_H */
