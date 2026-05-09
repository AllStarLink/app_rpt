# AGENTS.md

## What This Is

Asterisk loadable modules for AllStarLink 3 — amateur radio repeater control. Pure C, no build system in this repo. Built inside the Asterisk source tree via `rpt_install.sh`, which copies files into an existing Asterisk checkout and runs `make`.

## Build

This repo has **no standalone build**. It is compiled as part of [asl3-asterisk](https://github.com/AllStarLink/asl3-asterisk). CI uses `ghcr.io/allstarlink/asl3-ci:latest` container.

```sh
# CI build (inside container with Asterisk source at /usr/src/asterisk-*)
cp -r app_rpt /usr/src && cd /usr/src/app_rpt && ./rpt_install.sh
```

The install script copies `.c`/`.h` files into the Asterisk source tree, then runs `make apps && make channels && make res && make install`.

## Tests

Tests use the Asterisk test framework (`phreaknet runtest apps/rpt`). They live in `tests/apps/rpt/` and are patched into a separate testsuite repo at install time. Cannot be run standalone.

## Pre-commit & CI

Two checks enforced on every commit and PR:

1. **clang-format** — config at `.dev/.clang-format`, run via `git clang-format --style=file:./.dev/.clang-format --staged`
2. **codespell** — ignore list at `.dev/.codespellignore`

Install hooks: `./.dev/install-hooks`

Required tools: `sudo apt install clang-format codespell`

## Formatting Rules (Critical)

- Hard tabs, 4-space width
- Linux brace style: opening brace on **same line** for control statements, **new line** for functions
- Column limit: **130**
- Pointer alignment: **right** (`int *ptr`)
- Space after C-style cast: `(int) x`
- `case` labels are **not** indented relative to `switch`
- `SortIncludes: false` — do not reorder includes

Run `clang-format -style=file:./.dev/.clang-format` to verify.

## Architecture

### Core module: `apps/app_rpt.c` (~8000 lines)

The main repeater loop. Contains `handle_link_data()` (IAX text frame dispatch), `local_dtmf_helper()` (DTMF processing), the function table, and the rpt_exec entry point.

### Submodules: `apps/app_rpt/*.c`

| File | Purpose |
|------|---------|
| `rpt_functions.c` | Function handlers: `function_ilink`, `function_cop`, etc. |
| `rpt_config.c` | Config parsing from `rpt.conf` using `RPT_CONFIG_VAR*` macros |
| `rpt_cli.c` | Asterisk CLI commands (`rpt cmd`, `rpt remotecmd`, etc.) |
| `rpt_link.c` | Link connection/management |
| `rpt_telemetry.c` | Telemetry/courtesy tones |
| `rpt_channel.c` | Channel helpers |
| `rpt_bridging.c` | Conference bridge management |

### Key header: `apps/app_rpt/app_rpt.h`

Contains `struct rpt` (the main per-node state, ~300 fields), `struct rpt.p` (parsed config), all `#define` constants, and string format macros (`S_FMT`, `N_FMT`).

### Channel drivers: `channels/`

`chan_simpleusb.c`, `chan_usbradio.c`, `chan_voter.c`, `chan_echolink.c`, `chan_tlb.c`, `chan_usrp.c` — radio interface drivers.

### Config: `configs/rpt/rpt.conf`

Asterisk-style INI with template inheritance: `[node-main](!)` is the template, `[1999](node-main)` inherits it. Functions are mapped in `[functions]` stanzas (e.g., `1 = ilink,1`).

## Code Patterns

### Adding a config variable
1. Add field to `struct rpt.p` in `app_rpt.h`
2. Add `RPT_CONFIG_VAR(fieldname, "confname")` in `rpt_config.c`
3. Document in `configs/rpt/rpt.conf`

### Adding an ilink subcommand
1. Add `case N:` in `function_ilink()` in `rpt_functions.c`
2. Update doc comment in `app_rpt.c` (line ~159)
3. Add example in `configs/rpt/rpt.conf` functions section

### Adding a CLI command
1. Add `rpt_do_xxx()` function in `rpt_cli.c`
2. Add `handle_cli_xxx()` wrapper with `CLI_INIT`/`CLI_GENERATE` cases
3. Register in `rpt_cli[]` array with `AST_CLI_DEFINE`

### IAX text frame protocol
Messages sent between nodes as `AST_FRAME_TEXT`. Dispatched by first character in `handle_link_data()`:
- `M` = text message, `T` = telemetry, `D` = DTMF, `K` = keying, `L` = node list, `R` = remote command, `RA` = remote command response

Format follows: `<prefix> <src> <dest> [payload...]` — dest `0` means all nodes.

### Executing DTMF functions programmatically
Feed chars through `local_dtmf_helper(myrpt, c)` — processes through `collect_function_digits()` against the node's `[functions]` stanza, same as radio DTMF input.

### Sending text to links
- Single link: `rpt_qwrite(link, &wf)` with `AST_FRAME_TEXT` frame
- All links: `ao2_callback(myrpt->links, OBJ_MULTIPLE | OBJ_NODATA, rpt_sendtext_cb, str)`

## Gotchas

- **No standalone compilation** — syntax errors only caught in the full Asterisk build
- **`S_FMT(x)` macro** stringifies at preprocess time — `S_FMT(SOME_DEFINE * 2)` will NOT evaluate the expression; use literal numbers or a pre-defined constant
- **Mutex discipline** — `rpt_mutex_lock(&myrpt->lock)` must be held when accessing `myrpt->links`; release before calling functions that may block
- **`ao2_find` returns ref** — must `ao2_ref(l, -1)` after use
- **Static functions in app_rpt.c** — if you need to call a function defined later in the file, add a forward declaration (e.g., `local_dtmf_helper` is at ~line 2550)
- **OpenSSL available at build time** — Asterisk links OpenSSL; `<openssl/hmac.h>` and `<openssl/evp.h>` are usable even though this repo has no direct OpenSSL references
- **Docstrings** — use Asterisk QT-style: `/*! \brief Description */`; inline field docs use `/*!< \brief ... */`
