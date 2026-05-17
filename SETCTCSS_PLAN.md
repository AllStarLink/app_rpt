# SETCTCSS Feature Plan

## Current Status

**All code changes are complete.** Pending: LSP diagnostics / build verification.

### Changes made:
1. ✅ `channels/chan_usbradio.c` — 4 struct fields added (~line 392)
2. ✅ `channels/chan_usbradio.c` — `SETCTCSS` text command handler (~line 1790)
3. ✅ `channels/chan_usbradio.c` — `ctcss_override_active` branch in `xpmr_config()` (~line 4848)
4. ✅ `apps/app_rpt/rpt_functions.c` — COP case 66 (~line 1859)
5. ❌ No registration needed in `rpt_config.c` (COP is a simple switch/case)
6. ⏳ LSP diagnostics — clangd was installed as `clangd-12`, needs `sudo ln -sf /usr/bin/clangd-12 /usr/bin/clangd` then restart session

### To verify after restart:
```
Run: lsp_diagnostics on channels/chan_usbradio.c and apps/app_rpt/rpt_functions.c
Then: build if build system is available
```

Branch: `changeCTCSS`

## Overview

Add a new text command `SETCTCSS` to `chan_usbradio` that allows dynamic runtime changes to TX and RX CTCSS frequencies without using remote base mode. A new COP function in `app_rpt` provides DTMF/CLI access.

## Command Format

```
SETCTCSS <rxfreq> <txfreq>
SETCTCSS default
```

- `rxfreq` = RX CTCSS frequency (e.g. `100.0`, `91.5`, or `0` for none/carrier squelch)
- `txfreq` = TX CTCSS frequency (e.g. `100.0`, or `0` for no TX tone)
- `default` = revert to `usbradio.conf` settings

## Design

### New State Fields in `chan_usbradio_pvt`

```c
unsigned int ctcss_override_active:1;  /* flag: dynamic CTCSS is in effect */
char override_rxctcssfreqs[512];       /* dynamic RX CTCSS string */
char override_txctcssfreqs[512];       /* dynamic TX CTCSS string */
char override_txctcssdefault[16];      /* dynamic TX default tone */
```

### Priority Order in `xpmr_config()`

```
1. forcetxcode          (per-transmission override — unchanged)
2. remoted              (remote base — unchanged)
3. ctcss_override_active (dynamic override — NEW)
4. config file defaults  (lowest — unchanged)
```

Insert `else if` after the existing `remoted` branch, before the config-file fallback:

```c
if (o->remoted) {
    // unchanged
    o->pmrChan->pTxCodeDefault = o->set_txctcssdefault;
    o->pmrChan->pRxCodeSrc = o->set_rxctcssfreqs;
    o->pmrChan->pTxCodeSrc = o->set_txctcssfreqs;
} else if (o->ctcss_override_active) {
    // NEW
    o->pmrChan->pTxCodeDefault = o->override_txctcssdefault;
    o->pmrChan->pRxCodeSrc = o->override_rxctcssfreqs;
    o->pmrChan->pTxCodeSrc = o->override_txctcssfreqs;
} else {
    // unchanged
    o->pmrChan->pTxCodeDefault = o->txctcssdefault;
    o->pmrChan->pRxCodeSrc = o->rxctcssfreqs;
    o->pmrChan->pTxCodeSrc = o->txctcssfreqs;
}
```

### SETCTCSS Handler in `usbradio_text()`

Placed after `TXCTCSS` handler and before the `cnt < 6` check (early return pattern):

```c
if (strcmp(cmd, "SETCTCSS") == 0) {
    if (!strcasecmp(rxs, "default")) {
        o->ctcss_override_active = 0;
    } else {
        ast_copy_string(o->override_rxctcssfreqs, rxs, sizeof(o->override_rxctcssfreqs));
        ast_copy_string(o->override_txctcssfreqs, txs, sizeof(o->override_txctcssfreqs));
        ast_copy_string(o->override_txctcssdefault, txs, sizeof(o->override_txctcssdefault));
        o->ctcss_override_active = 1;
    }
    xpmr_config(o);
    return 0;
}
```

### COP Function in `rpt_functions.c`

New case (e.g., 66) following pattern of COP 56-59:

```c
case 66: /* Set CTCSS frequencies */
    if (argc < 2) break;
    if (!CHAN_TECH(myrpt->rxchannel, "radio") &&
        !CHAN_TECH(myrpt->rxchannel, "simpleusb")) break;
    if (argc >= 3) {
        snprintf(string, sizeof(string), "SETCTCSS %s %s", argv[1], argv[2]);
    } else {
        snprintf(string, sizeof(string), "SETCTCSS %s", argv[1]);
    }
    ast_sendtext(myrpt->rxchannel, string);
    rpt_telem_select(myrpt, command_source, mylink);
    rpt_telemetry(myrpt, COMPLETE, NULL);
    return DC_COMPLETE;
```

## Files Changed

| File | Change | ~Lines |
|------|--------|--------|
| `channels/chan_usbradio.c` struct (~line 392) | Add 4 fields | 4 |
| `channels/chan_usbradio.c` `usbradio_text()` (~line 1783) | Add `SETCTCSS` handler | 15 |
| `channels/chan_usbradio.c` `xpmr_config()` (~line 4830) | Add `ctcss_override_active` branch | 6 |
| `apps/app_rpt/rpt_functions.c` (~line 1858) | Add COP case 66 | 12 |
| `apps/app_rpt/rpt_config.c` | Register COP number 66 | 1 |

Remote base code is **completely untouched**.

## Configuration Example

### usbradio.conf — No changes needed

Existing config works as-is. Dynamic command overrides at runtime:

```ini
[1999](node-main)
txctcssdefault = 100.0
rxctcssfreqs = 100.0,91.5
txctcssfreqs = 100.0,91.5
rxctcssoverride = no
```

### rpt.conf — Add COP functions

```ini
[functions](functions-main)
; Dynamic CTCSS control
; Usage: cop,66,<rxfreq>,<txfreq>
966 = cop,66,100.0,100.0      ; Set RX and TX CTCSS to 100.0
967 = cop,66,91.5,91.5        ; Set RX and TX CTCSS to 91.5
968 = cop,66,100.0,0           ; Decode 100.0, no TX tone
969 = cop,66,0,100.0           ; No RX decode, encode 100.0
970 = cop,66,default           ; Revert to usbradio.conf settings
```

### Macros / Scheduler

```ini
[macro]
1 = *966                       ; Macro 1: daytime tone (100.0)
2 = *967                       ; Macro 2: nighttime tone (91.5)

[schedule]
1 = 00 06 * * *                ; At 6am switch to 100.0
2 = 00 22 * * *                ; At 10pm switch to 91.5
```

## Usage

### Via DTMF

| Sequence | Effect |
|----------|--------|
| `*966` | Switch to 100.0 Hz RX+TX |
| `*967` | Switch to 91.5 Hz RX+TX |
| `*968` | Decode 100.0 Hz, no TX tone |
| `*969` | Carrier squelch, encode 100.0 Hz |
| `*970` | Revert to config file defaults |

### Via Asterisk CLI

```
*CLI> rpt fun 1999 *966
*CLI> rpt fun 1999 *970
```

## Behavioral Summary

| Mode | RX | TX |
|------|----|----|
| **Normal** (config / after `default`) | Decodes 100.0 AND 91.5, opens on either | Follows RX: heard 100.0→encode 100.0, heard 91.5→encode 91.5. Default 100.0 |
| **SETCTCSS 100.0 100.0** | Decodes ONLY 100.0 | Always encodes 100.0 |
| **SETCTCSS 100.0 91.5** | Decodes ONLY 100.0 | Always encodes 91.5 |
| **SETCTCSS 0 100.0** | No CTCSS decode (carrier squelch) | Encodes 100.0 |
| **SETCTCSS 100.0 0** | Decodes ONLY 100.0 | No TX tone |
| **SETCTCSS default** | Back to config file | Back to config file |

## Thread Safety Note

`code_string_parse()` (called via `xpmr_config()`) does `ast_free()`/`ast_calloc()` on filter buffers while `hidthread` may be processing audio. This is a **pre-existing issue** — the same race exists with the current `SETFREQ` command. Not introduced by this change, but worth noting for future hardening.
