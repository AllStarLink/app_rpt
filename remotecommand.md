# Remote Command Feature (ilink 20)

## User Guide

### Overview

Remote Command allows you to send DTMF command sequences from one AllStarLink node to another connected node (or all connected nodes) over the IAX link. The remote node executes the command as though someone keyed it locally on the radio. Commands are authenticated using HMAC-SHA256 so that both the sending and receiving nodes can verify they share the same code word.

### Quick Start

1. Add a `remote_cmd_code` to your node configuration (optional but recommended).
2. Map an ilink 20 command in your `[functions]` stanza.
3. Key the DTMF sequence on your radio to trigger it.

### Configuration

#### Code Word

Add `remote_cmd_code` to your node stanza in `rpt.conf`. Place it in `[node-main](!)` to apply to all nodes, or in a specific node stanza (e.g., `[1999](node-main)`) to override per node.

```ini
[node-main](!)
remote_cmd_code = MyS3cretCode
```

Both the sending and receiving nodes must have the same `remote_cmd_code` configured. If neither side sets a code word, commands still work -- they are validated using an empty key. If one side has a code word and the other does not (or they differ), the command will be rejected.

#### Function Mapping

Map DTMF sequences to remote commands in your `[functions]` stanza:

```ini
[functions](functions-main)
; ilink,20,<target node or 0 for all>,<DTMF digits to execute on remote>
980 = ilink,20,2001,*81       ; Send *81 to node 2001
981 = ilink,20,0,721          ; Send 721 (Force ID) to all connected nodes
```

The `<DTMF digits>` field is the exact sequence that will be processed on the remote node through its own `[functions]` stanza. For example, if node 2001 has `*81 = cop,1` in its functions, sending `*81` via remote command will execute `cop,1` on that node.

Use `0` as the target node to send the command to every currently connected node.

### Asterisk CLI

Two CLI methods are available:

```
rpt remotecmd <local-node> <target-node|0> <dtmf-digits>
```

Examples:

```
rpt remotecmd 2000 2001 *81       ; Send *81 to node 2001
rpt remotecmd 2000 0 *81          ; Send *81 to all connected nodes
```

The equivalent long form using `rpt cmd`:

```
rpt cmd 2000 ilink 20 2001,*81
```

### Requirements

- The target node must be currently connected (linked) to the sending node.
- If `remote_cmd_code` is configured, it must match on both sides.
- The DTMF digits sent must map to a valid function in the remote node's `[functions]` stanza.

### Error Handling

If the target node is not connected (or no nodes are connected at all), the command fails immediately with an error tone. No message is sent over the wire.

### Responses

After a remote command is sent, the receiving node sends back a response:

- **OK** -- command was authenticated and executed. A completion tone is played on the sending node.
- **DENIED** -- HMAC validation failed (code word mismatch). A connection failure tone is played on the sending node.

Responses are logged on the sending node at verbosity level 3:

```
Remote command response from 2001: OK
```

### Logging

Remote commands are logged on the receiving node using the standard node log format:

```
REMOTECMD,<source-node>,<dtmf-digits>
```

---

## Developer Reference

### Problem

There was no mechanism to programmatically trigger a function on a remote connected node. The existing `ilink 4` (command mode) requires interactive DTMF entry over the link. Operators needed a way to push commands to remote nodes from configuration or CLI, with authentication to prevent unauthorized execution.

### Design Decisions

**DTMF digit injection, not direct function dispatch.** The remote command sends raw DTMF characters (e.g., `*81`) rather than function names (e.g., `cop,1`). The receiving node processes them through its own `[functions]` stanza via `local_dtmf_helper()`. This means the remote node's function table controls what is allowed -- no new access control mechanism is needed.

**HMAC-SHA256 with optional shared secret.** Every command is HMAC-validated regardless of whether a code word is configured. When `remote_cmd_code` is empty or unset, both sides compute the HMAC with an empty key and the hashes match. When a code word is set, both sides must agree. This avoids a separate "skip auth" code path.

**Wire format modeled after existing text message protocol.** The `R` message follows the same `<prefix> <src> <dest> <payload>` structure used by `M` (text messages), `T` (telemetry), and other IAX text frame types. Using `0` as the destination for broadcast is the same convention used throughout `handle_link_data()`.

**Network propagation.** Like `M` messages, `R` commands propagate through multi-hop link chains. If node A links to B and B links to C, a command sent from A with dest `0` reaches B, which forwards it to C. Each node independently validates the HMAC with its own `remote_cmd_code`.

### Wire Protocol

Outbound command:

```
R <src-node> <dest-node> <64-char-hmac-sha256-hex> <dtmf-digits>
```

Response:

```
RA <responding-node> <original-sender> OK|DENIED
```

### Files Changed

#### `apps/app_rpt/app_rpt.h`

Added `const char *remote_cmd_code` field to the `rpt.p` config struct. This holds the per-node shared secret parsed from `rpt.conf`.

#### `apps/app_rpt/rpt_config.c`

Added `RPT_CONFIG_VAR(remote_cmd_code, "remote_cmd_code")` to parse the new configuration variable using the standard config macro pattern.

#### `apps/app_rpt/rpt_functions.c`

Added `#include <openssl/evp.h>` and `#include <openssl/hmac.h>` for HMAC computation.

Added static helper `rpt_compute_hmac()` that computes HMAC-SHA256 and writes the result as a 64-character hex string. Uses an empty string as the key when `remote_cmd_code` is NULL.

Added `case 20` to `function_ilink()`:
1. Parses destination node and DTMF digits from the comma-delimited param string (same parsing pattern as case 9 for text messages).
2. Verifies at least one link exists.
3. Computes HMAC of the digit string.
4. Formats the `R` text frame.
5. If destination is `0`, sends to all links via `ao2_callback` with `rpt_sendtext_cb`.
6. If destination is a specific node, finds it in `myrpt->links` with `ao2_find`, sends directly, and releases the reference.

#### `apps/app_rpt.c`

Added `#include <openssl/evp.h>` and `#include <openssl/hmac.h>`.

Added a forward declaration for `static void local_dtmf_helper()` since it is defined later in the file (~line 2550) but needed by the `R` handler at ~line 2030.

Added a static copy of `rpt_compute_hmac()` (same implementation as in `rpt_functions.c`, both are `static` so no linker conflict).

Added `RA` handler in `handle_link_data()` (checked before `R` using `strncmp` for the two-character prefix):
- Parses source, destination, and status using the standard `sscanf`/`S_FMT` pattern.
- Drops messages from self (anti-loop).
- Forwards to correct destination if not for this node.
- Logs the response at verbosity level 3.
- Plays `COMPLETE` telemetry tone on OK response.
- Plays `CONNFAIL` telemetry tone on DENIED response.

Added `R` handler in `handle_link_data()`:
1. Parses `cmd`, `src`, `dest` with `sscanf` and `S_FMT` macros (same pattern as `M` and `T` handlers).
2. Drops messages originating from self.
3. If dest is not this node and not `0`, forwards to the specific destination via `distribute_to_all_links()`.
4. If dest is `0`, forwards to all other links (broadcast propagation).
5. Extracts the 64-character HMAC hex string and remaining DTMF digits from the payload.
6. Computes local HMAC using `myrpt->p.remote_cmd_code` and compares.
7. On mismatch: logs a warning and sends `RA <self> <src> DENIED` back.
8. On match: logs acceptance, writes a `REMOTECMD` node log entry, feeds each digit through `local_dtmf_helper()`, and sends `RA <self> <src> OK` back.

Updated the ilink doc comment block (~line 159) to include subcode 20.

#### `apps/app_rpt/rpt_cli.c`

Added `rpt_do_remotecmd()` -- a convenience function that builds the `ilink 20` command internally. It looks up the local node, resolves the `ilink` function index, formats the param string as `20,<target>,<digits>`, and queues it through the standard `cmdAction` mechanism.

Added `handle_cli_remotecmd()` -- the CLI wrapper with `CLI_INIT`/`CLI_GENERATE` handlers, registered as `rpt remotecmd`.

Registered `handle_cli_remotecmd` in the `rpt_cli[]` array.

#### `configs/rpt/rpt.conf`

Added `remote_cmd_code` documentation in the `[node-main]` template section.

Added ilink 20 to the link commands reference list.

Added example function mappings (`980`, `981`) in the functions-main section, following the existing cop command numbering.

Added `remote_cmd_code = MyS3cretCode` as an active sample in the `[node-main]` template.
