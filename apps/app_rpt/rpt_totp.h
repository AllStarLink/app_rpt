/*
 * RFC 6238 TOTP (Time-based One-Time Password) verification for app_rpt.
 *
 * Depends on OpenSSL's SHA-1 primitives (<openssl/sha.h>) and standard C.
 * Requires linking against libcrypto (-lcrypto).
 *
 * Used by rpt_auth.c to validate DTMF-entered authentication codes.
 */

#ifndef _RPT_TOTP_H
#define _RPT_TOTP_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*! \brief Result codes from rpt_totp_verify. */
enum rpt_totp_result {
	RPT_TOTP_OK = 0,            /*!< OTP matched within window. */
	RPT_TOTP_BAD_OTP = -1,      /*!< OTP did not match any step in window. */
	RPT_TOTP_REPLAY = -2,       /*!< OTP matched but counter <= last_counter. */
	RPT_TOTP_BAD_SECRET = -3,   /*!< secret_b32 failed to decode. */
	RPT_TOTP_BAD_PARAM = -4,    /*!< invalid argument (NULL, bad otp length, etc.) */
};

/*!
 * \brief Verify an RFC 6238 TOTP-SHA1 6-digit code.
 *
 * Computes TOTP for steps [now/step - window .. now/step + window] using the
 * given base32-encoded shared secret, and compares each candidate against the
 * supplied 6-digit OTP string.  On match, *last_counter is updated to the
 * matched counter so the caller can persist it and reject re-use within the
 * same or earlier step (in-window replay protection).
 *
 * \param secret_b32 Base32-encoded shared secret (RFC 4648, padding optional,
 *                   case-insensitive).  Must not be NULL.
 * \param otp6       Exactly 6 ASCII decimal digits.  Must not be NULL.
 * \param last_counter In/out.  On entry, the highest counter previously
 *                   accepted for this user (0 if none).  On RPT_TOTP_OK,
 *                   updated to the matched counter.  Unchanged on any error.
 *                   Must not be NULL.
 * \param now        Current Unix time (seconds since epoch).
 * \param step_seconds TOTP time step.  RFC 6238 default is 30.  Caller is
 *                   responsible for clamping to a sane range (e.g. 10-120).
 * \param window_steps Number of steps to accept on each side of the current
 *                   step.  0 = strict (current only); 1 = ±1 step (~3*step
 *                   seconds tolerance).  Caller should clamp to <= 3.
 * \retval RPT_TOTP_OK         match found and not a replay
 * \retval RPT_TOTP_BAD_OTP    no match in window
 * \retval RPT_TOTP_REPLAY     matched but counter already used
 * \retval RPT_TOTP_BAD_SECRET secret_b32 invalid
 * \retval RPT_TOTP_BAD_PARAM  bad argument
 */
int rpt_totp_verify(const char *secret_b32, const char *otp6,
	uint64_t *last_counter, time_t now,
	int step_seconds, int window_steps);

/*!
 * \brief Decode an RFC 4648 base32 string to bytes.
 *
 * Padding ('=') is permitted but not required.  Whitespace is rejected.
 * Decoding is case-insensitive.
 *
 * \param in     NUL-terminated base32 string.  Must not be NULL.
 * \param out    Output buffer.  Must not be NULL.
 * \param outlen Capacity of out.
 * \return Number of bytes written on success, or -1 on invalid input or if
 *         the decoded length would exceed outlen.
 */
int rpt_base32_decode(const char *in, uint8_t *out, size_t outlen);

#ifdef RPT_TOTP_SELFTEST
/*!
 * \brief Run RFC 4226/6238 reference vectors.  Returns 0 on pass, count of
 * failures on failure.  Logs each failure via ast_log.  Compiled only when
 * RPT_TOTP_SELFTEST is defined.
 */
int rpt_totp_selftest(void);
#endif

#endif /* _RPT_TOTP_H */
