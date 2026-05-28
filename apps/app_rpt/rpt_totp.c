
#include "asterisk.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "asterisk/logger.h"
#include "asterisk/utils.h"

#include <openssl/sha.h>

#include "rpt_totp.h"

#define HMAC_BLOCK 64
#define HMAC_HASH 20

static int hmac_sha1(const uint8_t *key, size_t keylen,
	const uint8_t *msg, size_t msglen,
	uint8_t out[HMAC_HASH])
{
	uint8_t k[HMAC_BLOCK];
	uint8_t ipad[HMAC_BLOCK];
	uint8_t opad[HMAC_BLOCK];
	uint8_t inner[HMAC_HASH];
	SHA_CTX sha;
	size_t i;

	memset(k, 0, sizeof(k));
	if (keylen > HMAC_BLOCK) {
		/* Per RFC 2104: if key longer than block, replace with H(key). */
		if (SHA1_Init(&sha) != 1 ||
			SHA1_Update(&sha, key, keylen) != 1 ||
			SHA1_Final(k, &sha) != 1) {
			return -1;
		}
	} else {
		memcpy(k, key, keylen);
	}

	for (i = 0; i < HMAC_BLOCK; i++) {
		ipad[i] = k[i] ^ 0x36;
		opad[i] = k[i] ^ 0x5c;
	}

	if (SHA1_Init(&sha) != 1 ||
		SHA1_Update(&sha, ipad, HMAC_BLOCK) != 1 ||
		SHA1_Update(&sha, msg, msglen) != 1 ||
		SHA1_Final(inner, &sha) != 1) {
		return -1;
	}

	if (SHA1_Init(&sha) != 1 ||
		SHA1_Update(&sha, opad, HMAC_BLOCK) != 1 ||
		SHA1_Update(&sha, inner, HMAC_HASH) != 1 ||
		SHA1_Final(out, &sha) != 1) {
		return -1;
	}

	return 0;
}

int rpt_base32_decode(const char *in, uint8_t *out, size_t outlen)
{
	size_t buf = 0;
	int bits = 0;
	size_t written = 0;
	const char *p;

	if (!in || !out) {
		return -1;
	}

	for (p = in; *p; p++) {
		int v;
		char c = *p;

		if (c == '=') {
			const char *q;
			for (q = p + 1; *q; q++) {
				if (*q != '=') {
					return -1;
				}
			}
			break;
		}
		if (c >= 'A' && c <= 'Z') {
			v = c - 'A';
		} else if (c >= 'a' && c <= 'z') {
			v = c - 'a';
		} else if (c >= '2' && c <= '7') {
			v = 26 + (c - '2');
		} else {
			return -1;
		}

		buf = (buf << 5) | (size_t) v;
		bits += 5;
		if (bits >= 8) {
			bits -= 8;
			if (written >= outlen) {
				return -1;
			}
			out[written++] = (uint8_t) ((buf >> bits) & 0xff);
		}
	}

	return (int) written;
}

static int totp_at(const uint8_t *key, size_t keylen, uint64_t counter)
{
	uint8_t msg[8];
	uint8_t hash[HMAC_HASH];
	int offset;
	uint32_t bin;
	int i;

	for (i = 7; i >= 0; i--) {
		msg[i] = (uint8_t) (counter & 0xff);
		counter >>= 8;
	}

	if (hmac_sha1(key, keylen, msg, sizeof(msg), hash) != 0) {
		return -1;
	}

	/* RFC 4226 dynamic truncation. */
	offset = hash[HMAC_HASH - 1] & 0x0f;
	bin = ((uint32_t) (hash[offset] & 0x7f) << 24) |
		((uint32_t) hash[offset + 1] << 16) |
		((uint32_t) hash[offset + 2] << 8) |
		((uint32_t) hash[offset + 3]);

	return (int) (bin % 1000000U);
}

int rpt_totp_verify(const char *secret_b32, const char *otp6,
	uint64_t *last_counter, time_t now,
	int step_seconds, int window_steps)
{
	uint8_t key[64];
	int keylen;
	uint64_t base_counter;
	int presented;
	int i;
	int w;

	if (!secret_b32 || !otp6 || !last_counter) {
		return RPT_TOTP_BAD_PARAM;
	}
	if (step_seconds <= 0 || window_steps < 0) {
		return RPT_TOTP_BAD_PARAM;
	}
	if (strlen(otp6) != 6) {
		return RPT_TOTP_BAD_PARAM;
	}
	for (i = 0; i < 6; i++) {
		if (otp6[i] < '0' || otp6[i] > '9') {
			return RPT_TOTP_BAD_PARAM;
		}
	}

	keylen = rpt_base32_decode(secret_b32, key, sizeof(key));
	if (keylen <= 0) {
		return RPT_TOTP_BAD_SECRET;
	}

	presented = (otp6[0] - '0') * 100000 +
		(otp6[1] - '0') * 10000 +
		(otp6[2] - '0') * 1000 +
		(otp6[3] - '0') * 100 +
		(otp6[4] - '0') * 10 +
		(otp6[5] - '0');

	base_counter = (uint64_t) (now / step_seconds);

	for (w = -window_steps; w <= window_steps; w++) {
		uint64_t c;
		int candidate;

		/* Avoid wrap when w is negative and would push counter below 0. */
		if (w < 0 && base_counter < (uint64_t) (-w)) {
			continue;
		}
		c = base_counter + (uint64_t) w;

		candidate = totp_at(key, (size_t) keylen, c);
		if (candidate < 0) {
			memset(key, 0, sizeof(key));
			return RPT_TOTP_BAD_PARAM;
		}
		if (candidate == presented) {
			memset(key, 0, sizeof(key));
			if (c <= *last_counter) {
				return RPT_TOTP_REPLAY;
			}
			*last_counter = c;
			return RPT_TOTP_OK;
		}
	}

	memset(key, 0, sizeof(key));
	return RPT_TOTP_BAD_OTP;
}

#ifdef RPT_TOTP_SELFTEST

/*
 * RFC 6238 Appendix B reference vectors.  The reference key is the ASCII
 * string "12345678901234567890" (20 bytes), which in base32 is
 * "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ".  Six SHA1 vectors at known times.
 */
static const struct {
	time_t t;
	const char *expected;
} rfc6238_vectors[] = {
	{          59, "287082" },
	{  1111111109, "081804" },
	{  1111111111, "050471" },
	{  1234567890, "005924" },
	{  2000000000, "279037" },
	/* RFC's 20000000000 vector exceeds 32-bit time_t; skip on those builds. */
};

int rpt_totp_selftest(void)
{
	const char *secret = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
	int failures = 0;
	size_t i;

	for (i = 0; i < ARRAY_LEN(rfc6238_vectors); i++) {
		uint64_t last = 0;
		int rc = rpt_totp_verify(secret, rfc6238_vectors[i].expected,
			&last, rfc6238_vectors[i].t, 30, 0);
		if (rc != RPT_TOTP_OK) {
			ast_log(LOG_ERROR, "rpt_totp selftest vector %zu (t=%ld) failed: rc=%d\n",
				i, (long) rfc6238_vectors[i].t, rc);
			failures++;
		}
	}

	/* Replay rejection: same OTP at same step must be rejected the second time. */
	{
		uint64_t last = 0;
		int rc1 = rpt_totp_verify(secret, "287082", &last, 59, 30, 0);
		int rc2 = rpt_totp_verify(secret, "287082", &last, 59, 30, 0);
		if (rc1 != RPT_TOTP_OK || rc2 != RPT_TOTP_REPLAY) {
			ast_log(LOG_ERROR, "rpt_totp selftest replay check failed: rc1=%d rc2=%d\n",
				rc1, rc2);
			failures++;
		}
	}

	/* Window check: previous-step OTP must be accepted with window=1, rejected with window=0. */
	{
		uint64_t last = 0;
		int prev_step_otp;
		char buf[8];
		uint8_t key[64];
		int keylen = rpt_base32_decode(secret, key, sizeof(key));
		prev_step_otp = (keylen > 0) ? totp_at(key, (size_t) keylen, 1) : -1;
		if (prev_step_otp < 0) {
			ast_log(LOG_ERROR, "rpt_totp selftest window setup failed\n");
			failures++;
		} else {
			int rc_strict, rc_window;
			snprintf(buf, sizeof(buf), "%06d", prev_step_otp);
			last = 0;
			rc_strict = rpt_totp_verify(secret, buf, &last, 59, 30, 0);
			last = 0;
			rc_window = rpt_totp_verify(secret, buf, &last, 59, 30, 1);
			if (rc_strict != RPT_TOTP_BAD_OTP || rc_window != RPT_TOTP_OK) {
				ast_log(LOG_ERROR, "rpt_totp selftest window check failed: strict=%d window=%d\n",
					rc_strict, rc_window);
				failures++;
			}
		}
	}

	if (failures == 0) {
		ast_log(LOG_NOTICE, "rpt_totp selftest: all vectors PASSED\n");
	}
	return failures;
}

#endif /* RPT_TOTP_SELFTEST */
