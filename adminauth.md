# app_rpt TOTP DTMF Authentication — Admin & Developer Guide

This document covers the per-user TOTP (Time-based One-Time Password, RFC 6238) authentication feature added to app_rpt.

It is split into two parts:

- **Part 1 — Admin guide:** how to configure and use the feature.
- **Part 2 — Developer guide:** how the code works, why it was designed this way, and where to look in the source.

---

# Part 1 — Admin guide

## 1.1 What this feature does

By default, every DTMF function in `rpt.conf` is reachable by anyone who can key the radio. There is no notion of "this user is allowed to do that command."

This feature lets you:

1. Define an **additional, privileged DTMF command set** in `rpt.conf` (a normal `[functions-...]` stanza).
2. Map a **4-digit user-id** to a **shared TOTP secret** and one of those privileged stanzas.
3. Require the user to authenticate via DTMF using a 4-digit user-id plus a 6-digit one-time code from a standard TOTP app on their phone (Google Authenticator, Authy, FreeOTP, 1Password, etc.).
4. Once authenticated, their privileged commands become available **in addition to** the normal commands, **for that node only**, until they log out or the session times out.

It is per-node, per-user, additive, sliding-timeout, and uses standard RFC 6238 TOTP.

## 1.2 Wire format on the radio

You pick DTMF prefixes in `rpt.conf` for the auth sub-commands. Throughout this doc we use `*A1`, `*A2`, `*A3` as the example prefixes, but you can use anything (e.g. `*991`, `*992`, `*993`, etc.).

| Keyed by user            | Meaning                                                                |
|--------------------------|------------------------------------------------------------------------|
| `*A1<id4><otp6>`         | Login. `<id4>` = exactly 4 decimal digits. `<otp6>` = exactly 6 decimal digits. |
| `*A2`                    | Status — courtesy tone only, no on-air detail. Use the CLI for detail.|
| `*A3`                    | Logout (clears the session for this node).                             |

Total login length is always exactly 10 digits after the prefix. There is **no separator** between the user-id and OTP — this was a deliberate design choice to keep the format short.

Audio feedback:

- **Success** (login OK, logout OK, status query): standard COMPLETE courtesy tone.
- **Failure** (bad OTP, locked out, feature disabled, malformed input): standard error tone (whatever your node already plays for `DC_ERROR`).

Failures are deliberately indistinguishable on-air. A caller cannot tell the difference between "wrong code" and "you are locked out" by listening — that information only appears in the Asterisk log and via the CLI. This is a security property; do not change it.

## 1.3 Configuration files

There are two files involved:

1. **`rpt.conf`** — your existing config. You add a few `auth_*` keys, plus one new `[functions-...]` stanza per privileged role, plus one entry in your active `[functions]` stanza for the `auth` function itself.
2. **`rpt_auth.conf`** (new file, anywhere you choose) — maps user-ids to TOTP secrets and stanza names. Contains shared secrets, so file permissions matter.

### 1.3.1 rpt.conf — node-level keys

Add inside the `[<your-node>]` section:

```ini
auth_users             = /etc/asterisk/rpt_auth.conf
auth_timeout           = 300       ; sliding session timeout, seconds (range 30..86400, default 300)
auth_lockout_threshold = 5         ; failed logins before lockout (range 0..1000, default 5; 0 disables lockout)
auth_lockout_duration  = 60        ; lockout duration, seconds (range 0..86400, default 60)
auth_otp_step          = 30        ; TOTP time step (range 10..120, default 30 — matches Google Authenticator)
auth_otp_window        = 1         ; allowed step skew on each side (range 0..3, default 1 — ±30s tolerance)
```

Only `auth_users` is required. If you omit it, the feature is silently disabled and all login attempts return error.

### 1.3.2 rpt.conf — function table entry

The `auth` function must be reachable from the **default** functions stanza, otherwise unauthenticated users cannot log in. Add three entries to your active `[functions]`, one per sub-command:

```ini
[functions-main](!)
; ... your existing entries ...
A1 = auth,a
A2 = auth,s
A3 = auth,l
```

The `param` after the comma selects the sub-command: `a` = authenticate (login), `s` = status, `l` = logout.

### 1.3.3 rpt.conf — privileged command stanzas

Define one `[functions-...]` stanza per role. Example:

```ini
[functions-admin]
*99 = cop,5                          ; system unkey
*98 = cop,6                          ; system key
*73 = cmd,/usr/local/bin/restart-link.sh

[functions-controlop]
*55 = cop,42                         ; some control-op-only action
```

These stanzas look identical to your normal functions stanzas — same `prefix = action,param` syntax, same set of available actions. The only difference is they are reachable only when an authenticated user mapped to them is currently logged in on this node.

### 1.3.4 rpt_auth.conf — user/secret file

Create `/etc/asterisk/rpt_auth.conf` (or wherever you pointed `auth_users`). Format:

```ini
[users]
1234 = JBSWY3DPEHPK3PXPABCDEFGHIJKLMNOP, functions-admin
5678 = KRSXG5BAMFRGGZDFMZTWQ2LK,         functions-controlop
9999 = NB2HI4DTHIXS653XO4XHSZLOOQ,       functions-diag
```

Each line: `<id4> = <BASE32_SECRET>, <stanza_name>`

- **`<id4>`** — exactly 4 ASCII decimal digits. Leading zeros are significant (`0001` ≠ `1`). Must be unique within the file.
- **`<BASE32_SECRET>`** — RFC 4648 uppercase base32. Optional `=` padding. This is the same encoding every standard TOTP app consumes.
- **`<stanza_name>`** — the name of a `[functions-...]` stanza in `rpt.conf`. No leading bracket.

Whitespace around the `,` is tolerated. Comment lines start with `;`.

**Permissions are critical.** This file contains shared secrets equivalent to passwords:

```bash
sudo chown root:asterisk /etc/asterisk/rpt_auth.conf
sudo chmod 0640         /etc/asterisk/rpt_auth.conf
```

## 1.4 Generating secrets and enrolling users

### 1.4.1 Generate a fresh base32 secret

```bash
head -c 20 /dev/urandom | base32
# → e.g. JBSWY3DPEHPK3PXPABCDEFGHIJKLMNOP
```

20 bytes (160 bits) is the RFC 6238 recommendation for SHA-1 TOTP. Don't use less.

### 1.4.2 Enroll the user's authenticator app

Two options:

**Option A — paste the secret:** Most apps have a "manual entry" mode. Account name = anything (e.g. `myrepeater-1234`), key = the base32 string above, time-based, 30 s, 6 digits, SHA-1.

**Option B — QR code (much friendlier):**

```bash
# Install qrencode: apt install qrencode
SECRET=JBSWY3DPEHPK3PXPABCDEFGHIJKLMNOP
USER_ID=1234
ISSUER=myrepeater

echo "otpauth://totp/${ISSUER}:${USER_ID}?secret=${SECRET}&issuer=${ISSUER}&algorithm=SHA1&digits=6&period=30" \
    | qrencode -t ANSIUTF8
```

Have the user scan it with their authenticator app. They will then see a 6-digit code that rolls every 30 seconds — that is what they key after their user-id.

### 1.4.3 Reload the node

```bash
asterisk -rx "module reload app_rpt.so"
```

Reloading clears any active sessions and re-reads `rpt_auth.conf`.

## 1.5 Day-to-day usage from the radio

User wants to run a privileged command, e.g. `*99` which is in `[functions-admin]`:

1. User opens their authenticator app, sees current code, e.g. `849203`.
2. User keys `*A11234849203` on the radio. Hears courtesy tone — they are now authenticated for ~5 minutes (or whatever `auth_timeout` is).
3. User keys `*99`. It executes (cop,5).
4. Each successful command refreshes the timeout (sliding window). User stays logged in as long as they keep using authed commands.
5. When done, user keys `*A3` to log out explicitly. (Or just walks away — session expires after `auth_timeout` seconds of inactivity.)

If the OTP is wrong, the user hears the error tone. Each failure increments their failed-attempt counter. After `auth_lockout_threshold` failures, that user-id is locked out for `auth_lockout_duration` seconds (even if they then key the correct code).

## 1.6 CLI commands

Two new Asterisk CLI commands are available:

```
asterisk -rx "rpt auth show <node>"
```
Prints whether a session is active on `<node>`, who is logged in, time remaining, current failed-attempt count, and lockout status.

```
asterisk -rx "rpt auth logout <node>"
```
Force-clears the active session on `<node>`. Useful if a user forgets to log out, or as part of an incident-response procedure.

Both support tab completion of the node name.

## 1.7 Operational notes & gotchas

- **One session per node.** If a second user successfully logs in while a first user is already authenticated, the first session is displaced. There is no warning to the first user. This is intentional (matches the "very few users" use-case); if it bites you in practice, contact the developer.

- **Authed stanza wins on prefix collision.** If `*99` exists in both `[functions-main]` and `[functions-admin]`, the authed version is dispatched when a session is active. It is the **admin's responsibility** not to define collisions that shadow critical default commands.

- **Time sync matters.** TOTP relies on the node's system clock. If your repeater's clock drifts more than `auth_otp_window * auth_otp_step` seconds from real time (default ±30s), every login will fail. Use NTP. The default window of ±1 step is generous enough to absorb cell-phone clock drift and transmission delay.

- **Reloading clears sessions.** `module reload app_rpt.so` and any rpt.conf reload will clear all active auth sessions on all nodes. Users must re-authenticate.

- **Logs.** All auth events go to the Asterisk log: successful logins (with user-id, **never** the OTP), failed logins (with user-id and reason), lockouts triggered, lockouts expired, sessions cleared by timeout. The OTP digits themselves are never logged. The user-id is logged because you need to know which account is being attacked.

- **Disabling without reconfig.** To temporarily disable auth without touching the function table, comment out `auth_users` in `rpt.conf` and reload. All login attempts will return error. The `A1`/`A2`/`A3` prefixes will still consume DTMF input but produce only error tones.

- **What if `rpt_auth.conf` has a syntax error or is unreadable?** The feature is silently disabled at load time, a warning is logged, and login attempts return error. The node continues to run normally for unauthenticated users.

- **There is no remote-management protocol for auth.** Adding/removing users, rotating secrets, etc. all require editing `rpt_auth.conf` on disk and reloading. This is intentional — there is no network attack surface for the secret store.

## 1.8 Troubleshooting

| Symptom                                              | Likely cause                                                                   |
|------------------------------------------------------|---------------------------------------------------------------------------------|
| Every OTP attempt fails immediately                  | Clock skew. Check `date` on the node vs real time. Run `ntpdate` or enable `chronyd`. |
| Specific user always fails                           | Wrong base32 secret in `rpt_auth.conf`, or user enrolled the wrong secret in their app. Re-generate and re-enroll. |
| Login OK but `*99` still fails after authing         | Stanza name in `rpt_auth.conf` doesn't match a `[functions-...]` stanza in `rpt.conf`. Check for typos. |
| All logins fail with "feature disabled" in log       | `auth_users` not set, or path wrong, or file unreadable by Asterisk user. Check `ls -l` and the path. |
| User reports being locked out unexpectedly           | They keyed wrong codes faster than they realized. `rpt auth show <node>` shows current count. Wait for `auth_lockout_duration`, or `module reload app_rpt.so` to clear. |
| Authed command works but normal commands break       | Prefix collision — your authed stanza is shadowing a default command. Pick non-overlapping prefixes. |
| `module reload` doesn't pick up new users            | Reload errored out. Check the Asterisk log for parse errors in `rpt_auth.conf`. |

## 1.9 Quick test recipe

For a fast end-to-end smoke test on a dev node:

```bash
# 1. Generate a secret and an otpauth URL
SECRET=$(head -c 20 /dev/urandom | base32 | tr -d '=')
echo "SECRET: $SECRET"
echo "otpauth://totp/test:1234?secret=$SECRET&issuer=test&algorithm=SHA1&digits=6&period=30" \
    | qrencode -t ANSIUTF8

# 2. Configure (edit rpt.conf with the auth_* keys, A1/A2/A3 in [functions-main], and a [functions-admin] stanza)
sudo tee /etc/asterisk/rpt_auth.conf >/dev/null <<EOF
[users]
1234 = $SECRET, functions-admin
EOF
sudo chown root:asterisk /etc/asterisk/rpt_auth.conf
sudo chmod 0640         /etc/asterisk/rpt_auth.conf

# 3. Reload
asterisk -rx "module reload app_rpt.so"

# 4. Scan the QR with your phone, get a code, then simulate the DTMF via CLI
asterisk -rx "rpt fun <node> *A11234<6-digit-code-from-phone>"

# 5. Verify session
asterisk -rx "rpt auth show <node>"

# 6. Try a privileged command (whatever you put in [functions-admin])
asterisk -rx "rpt fun <node> *99"

# 7. Log out
asterisk -rx "rpt fun <node> *A3"
asterisk -rx "rpt auth show <node>"   # should show no session
```

---

# Part 2 — Developer guide

This part explains the code: structure, design rationale, locking, and where each piece lives.

## 2.1 Design goals (from the original requirements)

These were locked in early conversation with the user and drove every subsequent choice:

1. **TOTP, not HOTP, not bespoke.** Every smartphone already has a TOTP app. Zero client tooling required.
2. **Additive command set, not replacement.** An authed user gets *more* commands, not different ones. Simplifies mental model and limits damage of misconfig.
3. **Per-node sessions.** Sessions are tied to a single node's `struct rpt`. Nodes do not share auth state.
4. **Sliding timeout.** Active users stay logged in; idle users are kicked.
5. **Separate secrets file.** `rpt.conf` is widely shared, copied, version-controlled. Secrets must not live there.
6. **Configurable lockout.** Defends against brute-forcing OTP via DTMF (the search space is only 10⁶).
7. **Fixed 4-digit user-id, no separator.** Keeps DTMF input short and unambiguous (10 digits, period).
8. **First-match-wins, admin-managed collision avoidance.** Matches the existing function-table semantics; no new tie-break logic.
9. **Minimal external dependencies.** Uses OpenSSL 3.0's EVP_MAC API (`<openssl/evp.h>`, linked via `-lcrypto`) for HMAC-SHA1. No other crypto libraries required.

## 2.2 File map

| File                                          | Role                                                            |
|-----------------------------------------------|-----------------------------------------------------------------|
| `apps/app_rpt/rpt_totp.h` / `rpt_totp.c`      | Pure TOTP/HOTP/HMAC-SHA1/base32 math. No Asterisk runtime deps. |
| `apps/app_rpt/rpt_auth.h` / `rpt_auth.c`      | Per-node session state, lockout, file loader. Bridges TOTP to `struct rpt`. |
| `apps/app_rpt/rpt_functions.{h,c}`            | New `function_auth` DTMF handler.                              |
| `apps/app_rpt/app_rpt.h`                      | New config fields on `struct rpt::p` + opaque `struct rpt_auth_state *auth`. |
| `apps/app_rpt/rpt_config.c`                   | Config-key registration and reload/free hooks.                 |
| `apps/app_rpt.c`                              | `function_table[]` registration + `collect_function_digits()` surgery. |
| `apps/app_rpt/rpt_cli.c`                      | `rpt auth show` / `rpt auth logout` CLI commands.              |
| `configs/rpt/rpt.conf`                        | Documented `auth_*` keys + example admin stanza.               |
| `configs/samples/rpt_auth.conf.sample`        | User-secret file template.                                      |

The split between `rpt_totp.*` and `rpt_auth.*` is deliberate: `rpt_totp` is a leaf module that can be unit-tested in isolation (it has its own `RPT_TOTP_SELFTEST` block validating RFC 6238 Appendix B vectors). `rpt_auth` owns all the runtime state and Asterisk integration.

## 2.3 The TOTP layer (`rpt_totp.c`)

Why we use OpenSSL rather than a bespoke crypto implementation: the only primitive needed is HMAC-SHA1, and OpenSSL's EVP_MAC API (`<openssl/evp.h>`) provides it directly with no manual HMAC construction required. OpenSSL libcrypto (`-lcrypto`) is already a build dependency of Asterisk itself, so no new library is introduced. The implementation delegates all cryptographic work to OpenSSL — `rpt_totp.c` only contains base32 decoding and the RFC 6238 verification logic.

Three things live here:

1. **`base32_decode()`** — RFC 4648 base32, case-insensitive, ignores `=` padding. Returns the raw secret bytes.
2. **`hmac_sha1()`** — HMAC-SHA1 via OpenSSL's EVP_MAC API. Output size 20 bytes.
3. **`rpt_totp_verify()`** — implements the RFC 6238 verification algorithm:
   - Compute `T = floor(now / step)`.
   - For each counter in `[T-window, T+window]`:
     - HMAC-SHA1 the 8-byte big-endian counter under the secret.
     - Apply the dynamic-truncation step (RFC 4226 §5.3).
     - Reduce mod 10⁶ to get the 6-digit candidate.
     - Compare to user input in constant time (well, as constant as `memcmp` is — see §2.9).
   - On match, write the matched counter to `*last_counter` so the caller can detect replay (next attempt must use a counter strictly greater than this).

The window parameter controls clock-skew tolerance. With default `step=30, window=1`, the node accepts codes from up to 30 seconds in the past or future, totalling a 90-second acceptance window per code. This is the same default Google Authenticator's server-side libraries use.

The `RPT_TOTP_SELFTEST` block at the bottom validates against the canonical SHA-1 test vectors from RFC 6238 Appendix B. To run it, build with `-DRPT_TOTP_SELFTEST -DTEST_MAIN` and call `rpt_totp_selftest()`. Currently no production caller invokes it; it is a build-time confidence tool.

## 2.4 The auth state layer (`rpt_auth.c`)

This module owns:

- **The opaque `struct rpt_auth_state`**, hung off `struct rpt::auth`. Allocated lazily on first `rpt_auth_reload()`.
- **The user table**, copied out of an `ast_config` load of `rpt_auth.conf`. We immediately `ast_config_destroy()` after copying the strings so we don't hold the parsed config tree (matches the `extnodefiles` pattern at `rpt_config.c:531-567`).
- **The active session slot** — at most one. Fields: user-id, last-counter (replay prevention), session-expiry timestamp, command-set stanza name (pointer into the user table).
- **The per-user failed-attempt counters and lockout timestamps** — tracked even when no session is active, indexed by user-id slot.

### 2.4.1 Why one session per node

The user explicitly accepted this constraint ("very few users would be authenticating"). The win is enormous: no need for per-channel/per-DTMF-source session tracking, no garbage-collection of dead sessions, no question of "which session do I check when this DTMF arrives." A second successful login simply overwrites the slot.

If this becomes painful (multiple control ops on the same node), the data structure to extend it is obvious — make `current_session` an array — but the dispatch logic stays the same.

### 2.4.2 Locking contract

Every public function in `rpt_auth.h` takes `myrpt->lock` internally. Callers must NOT hold the lock when calling. This was verified before writing the code: all `collect_function_digits()` callers release `myrpt->lock` before invocation (background task `bg_602072d4` confirmed this).

The reason for taking the lock inside `rpt_auth_*` rather than at the call site is that the same lock protects `myrpt->cfg`, which `rpt_auth_active_set()` callers (specifically `collect_function_digits()`) immediately walk via `ast_variable_browse()`. If the auth layer didn't hold the lock during its own state read, a concurrent reload could swap `myrpt->cfg` out from under the caller and the returned stanza-name pointer could dangle.

The pointer returned by `rpt_auth_active_set()` is documented to remain valid until the next reload/logout/expiry on this node — which is safe because all of those events also take the lock and we only call `rpt_auth_active_set()` from the DTMF dispatch path which is single-threaded per node.

### 2.4.3 The bounds-clamping policy

The user asked for sane defaults with admin override. Each numeric `auth_*` key is registered via `RPT_CONFIG_VAR_INT_DEFAULT_MIN_MAX` with the bounds documented in `rpt.conf`. For lockout specifically:

- `auth_lockout_threshold = 0` is treated as **disabled** (no lockout at all). This is a real config use-case for testing or for low-stakes nodes.
- `auth_lockout_threshold > 1000` is clamped to 1000. Also tested but practically infinite.

Sane bounds prevent the most common admin foot-guns (negative numbers, accidentally-pasted huge values) without forcing maintainers to build an elaborate validation layer.

## 2.5 The DTMF handler (`function_auth` in `rpt_functions.c`)

This is a simple input parser delegating to `rpt_auth_*`. It's a top-level function in the function table (Option A from the design discussion), not a sub-action of `cop`. Reasons:

- **Discoverability.** Users don't search the cop subcommand list for "the one that does login."
- **Permission scope.** Auth changes the dispatch path itself; conceptually it's not "control operator" territory.
- **Future-proofing.** If we later add `auth_token`, `auth_revoke_user`, etc., they fit naturally as siblings in `function_table[]`.

The handler uses the `param` field from `rpt.conf` to select the sub-command:

- `param="a"` (or empty/default) — **login.** Waits for exactly 10 trailing decimal digits (returns `DC_INDETERMINATE` while collecting), splits into 4-digit user-id + 6-digit OTP, calls `rpt_auth_login()`.
- `param="s"` — **status.** Fires immediately (no trailing digits needed), plays a COMPLETE courtesy tone.
- `param="l"` — **logout.** Fires immediately, calls `rpt_auth_logout()`, plays a COMPLETE courtesy tone.

This design avoids the problem of single-character prefix matching — because each sub-command has its own prefix in the function table (e.g. `A1`, `A2`, `A3`), the framework's prefix matching in `collect_function_digits` doesn't fire until the full prefix is collected. The login path then returns `DC_INDETERMINATE` until all 10 digits arrive.

A subtle but important detail: **all four failure modes (`BAD`, `LOCKED`, `DISABLED`, malformed) return identical `DC_ERROR`.** The on-air feedback is bit-for-bit indistinguishable. This prevents an attacker from learning whether a user-id is valid (vs. unknown) or whether they've been locked out (vs. just wrong code). The only place the distinction surfaces is in the logs and CLI, which live behind the system administrator's access control.

## 2.6 The dispatch surgery (`collect_function_digits` in `app_rpt.c`)

This was the highest-risk change in the whole feature, because `collect_function_digits` is called on **every DTMF event** on every node. It must be fast, must not deadlock, and must not subtly alter dispatch semantics for unauthenticated users.

The change has three pieces:

1. **Authed-stanza walk first.** Before walking the source (`functions` / `link_functions` / `phone_functions` / etc.) stanza, walk the authed stanza if a session exists. This honors the "additive, authed-first, first-match-wins" design. If a session is not active, `rpt_auth_active_set()` returns NULL and this whole block becomes a single comparison and skip. Branch-predictor-friendly.

2. **Longestfunc gate extension.** When no entry matches in either stanza, the function decides whether to wait for more digits or return `DC_ERROR`, based on whether the input is already as long as the longest registered prefix. We must extend that gate to include the authed stanza's longest prefix, otherwise an authed user with `*999` registered would never get past the 3-digit gate of an unauthenticated default stanza. `rpt_auth_longestfunc()` returns 0 when no session is active, so the gate is unchanged for unauthenticated users.

3. **Sliding-timeout touch.** On any successful function dispatch (`DC_COMPLETE`, `DC_COMPLETEQUIET`, `DC_DOKEY`), we call `rpt_auth_touch()` to refresh the session expiry. This is the implementation of "sliding timeout." It's a no-op when no session is active.

The original logic — find a match, parse value, look up action, dispatch — is left structurally untouched. This is deliberate: the smaller the diff in this critical path, the easier the code review and the lower the risk of breaking existing semantics for existing deployments that won't enable auth.

## 2.7 Configuration plumbing (`rpt_config.c`)

Six new keys are added via the existing `RPT_CONFIG_VAR_*` macros, alongside all the other rpt vars. This means:

- They are loaded at the same time as everything else.
- They are subject to the same bounds-checking machinery.
- They are reloaded automatically on `module reload`.

After `load_rpt_vars()` releases `myrpt->lock`, we call `rpt_auth_reload(myrpt)`. The reload is outside the lock because `rpt_auth_reload` takes the lock itself. (See §2.4.2.)

In `rpt_free_config_vars()`, we call `rpt_auth_free(myrpt)` to release the user table and the session state at module-unload time.

Crucially, **no new fields were added to anything other than `struct rpt`**. The auth state is hung off via an opaque pointer, so nothing outside `rpt_auth.c` knows or cares about its layout. This was a small but important design choice — it means the auth feature can grow new internal state (e.g. multiple sessions, audit log, secret rotation cache) without ABI churn elsewhere.

## 2.8 The CLI (`rpt_cli.c`)

Two commands: `rpt auth show <node>` and `rpt auth logout <node>`. Both follow the existing `handle_cli_dump`/`handle_cli_stats` pattern exactly — node-name argument at position 3, tab completion via `rpt_complete_node_list`, lookup by walking `rpt_vars[]` matching `.name`. Boring on purpose.

`rpt auth show` calls `rpt_auth_status(myrpt, buf, sizeof(buf))` which formats a single-line human summary. The format is intentionally not machine-parseable — if scripting needs arise, a separate JSON-output flag would be the right addition.

`rpt auth logout` simply calls `rpt_auth_logout(myrpt)`. This is the operator's emergency override.

## 2.9 Things that are deliberately NOT in this design

- **Per-user-source restriction.** The user said "all sources allowed are fine." A user can authenticate and act from any DTMF source (radio, phone patch, link). Adding source restriction later is straightforward (one bitmask field on the user record, one check in `function_auth`) but was YAGNI.
- **OTP scrubbing in logs.** The user said no special handling needed. The OTP is never logged in the first place because we only log user-id, result code, and reason — not the raw input. If the OTP somehow appears in debug output, that's a bug in `ast_debug` calls and should be fixed.
- **Constant-time OTP comparison.** Currently uses `memcmp`. For a 6-digit OTP delivered over DTMF (which is glacially slow compared to network attacks), the timing channel is effectively non-existent. If this codebase grows other OTP comparison paths, switch to a constant-time compare. Documenting the choice explicitly here so a future reviewer doesn't waste time worrying about it.
- **Encrypted secret storage.** `rpt_auth.conf` is plaintext, protected by file permissions only. Adding encryption-at-rest would require a key-management scheme (key in another file? prompted at startup? in `/etc/security/`?) — none of which are net wins for a self-hosted radio repeater. File permissions are the right tool here.
- **Network-side auth (HTTPS, manager API).** Out of scope. The DTMF channel is the only authentication surface.

## 2.10 Things future maintainers should be careful about

- **Don't move `rpt_auth_reload()` back inside the lock.** It will deadlock — `rpt_auth_reload` takes the lock itself, and the recursive-mutex behavior is not portable here.
- **Don't change the `DC_ERROR`-uniformity in `function_auth`.** It is a deliberate security property, documented inline. Adding distinct telemetry for "locked" vs "bad code" leaks information.
- **The user array is dynamically allocated based on the number of entries in `rpt_auth.conf`.** There is no hard cap; memory is the only limit.
- **Don't read `myrpt->auth` directly from outside `rpt_auth.c`.** It is an opaque pointer. Add accessor functions to `rpt_auth.h` if you need new behavior.
- **The authed-stanza walk in `collect_function_digits` MUST come before the source walk.** Reversing them quietly breaks the "authed-first" guarantee that admins are designing their stanzas around. If you're tempted to "optimize" this, don't.
- **The dispatch path is the hottest path in app_rpt.** Any new work added to `collect_function_digits` runs on every DTMF event. The current additions are O(1) when no session is active and O(n_authed_entries) when a session exists. Keep it that way.

## 2.11 Where to extend

If/when these come up:

- **Multiple concurrent sessions per node:** turn the single session slot into an array, key dispatch by source channel ID. The lookup function in §2.4 stays a one-liner.
- **Audit log of auth events:** add a single function `rpt_auth_audit_log(struct rpt_auth_event)` and call it at every state transition in `rpt_auth.c`. Backend (file, syslog, manager event) is a separate decision.
- **HOTP support (counter-based, not time):** `rpt_totp.c` already factors out HOTP — add a `kind` field to the user record and dispatch on it.
- **Per-user TOTP step/window override:** add optional fields after the stanza name in `rpt_auth.conf`, fall back to node-wide config when absent.
- **Web/manager-API enrollment flow:** new module `rpt_auth_manager.c` that calls into `rpt_auth.c`. Keep secrets-on-disk as the source of truth.

End of document.
