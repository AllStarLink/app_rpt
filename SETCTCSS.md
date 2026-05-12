# SETCTCSS — Dynamic CTCSS Frequency Control

## User Guide

### What It Does

SETCTCSS lets you change the CTCSS (PL tone) frequencies used for receive decoding and transmit encoding at runtime, without restarting Asterisk or modifying config files. Changes take effect immediately and persist until changed again or reverted to defaults.

### Configuration

Add COP 66 entries to your `[functions]` stanza in `rpt.conf`. The format is:

```
cop,66,<rxfreq>,<txfreq>
```

- **rxfreq** — RX CTCSS frequency to decode (e.g. `100.0`), or `0` to disable RX CTCSS (carrier squelch)
- **txfreq** — TX CTCSS frequency to encode (e.g. `100.0`), or `0` to disable TX tone
- **default** — Pass `default` as the first argument to revert to `usbradio.conf` settings

#### Example rpt.conf entries

```ini
[functions](functions-main)
; Dynamic CTCSS control
966 = cop,66,100.0,100.0      ; Set RX and TX CTCSS to 100.0 Hz
967 = cop,66,91.5,91.5        ; Set RX and TX CTCSS to 91.5 Hz
968 = cop,66,100.0,91.5       ; Decode 100.0, encode 91.5
969 = cop,66,100.0,0           ; Decode 100.0, no TX tone
970 = cop,66,0,100.0           ; No RX decode (carrier squelch), encode 100.0
971 = cop,66,default           ; Revert to usbradio.conf settings
```

No changes are needed in `usbradio.conf`. Your existing `txctcssdefault`, `rxctcssfreqs`, `txctcssfreqs`, and `rxctcssoverride` settings continue to define the default behavior.

### Usage

#### Via DTMF

Using the example configuration above:

| DTMF Sequence | Effect |
|---------------|--------|
| `*966` | Switch to 100.0 Hz on both RX and TX |
| `*967` | Switch to 91.5 Hz on both RX and TX |
| `*968` | Decode 100.0 Hz, encode 91.5 Hz |
| `*969` | Decode 100.0 Hz, transmit with no tone |
| `*970` | Carrier squelch (no RX decode), encode 100.0 Hz |
| `*971` | Revert to config file defaults |

#### Via Asterisk CLI

```
*CLI> rpt fun 1999 *966
*CLI> rpt fun 1999 *971
```

#### Via Macros and Scheduler

You can use macros and the scheduler to automate tone changes:

```ini
[macro]
1 = *966            ; Macro 1: switch to 100.0 Hz
2 = *967            ; Macro 2: switch to 91.5 Hz

[schedule]
1 = 00 06 * * *     ; At 6:00 AM, run macro 1 (100.0 Hz)
2 = 00 22 * * *     ; At 10:00 PM, run macro 2 (91.5 Hz)
```

### Behavior Summary

| Mode | RX Behavior | TX Behavior |
|------|-------------|-------------|
| **Normal** (config file defaults) | Decodes all frequencies listed in `rxctcssfreqs`. Opens squelch when any listed tone is detected. | TX tone follows RX: the matching `txctcssfreqs` entry is used. `txctcssdefault` is used when no specific match applies. |
| **SETCTCSS 100.0 100.0** | Decodes only 100.0 Hz | Encodes 100.0 Hz |
| **SETCTCSS 100.0 91.5** | Decodes only 100.0 Hz | Encodes 91.5 Hz |
| **SETCTCSS 0 100.0** | No CTCSS decode (carrier squelch) | Encodes 100.0 Hz |
| **SETCTCSS 100.0 0** | Decodes only 100.0 Hz | No TX tone |
| **SETCTCSS default** | Reverts to `usbradio.conf` settings | Reverts to `usbradio.conf` settings |

### Important Notes

- The SETCTCSS command only works with `chan_usbradio` (`Radio/`) and `chan_simpleusb` (`SimpleUSB/`) channel types.
- Changes are not written to config files. They are lost on Asterisk restart.
- SETCTCSS does not interfere with the existing COP 56/57 (RX CTCSS Enable/Disable) or COP 58/59 (TX CTCSS on input only) commands. Those continue to work independently.
- SETCTCSS does not affect remote base operation. The remote base code path is completely separate.

---

## Code Explanation

### Overview

The feature adds three things:

1. A new `SETCTCSS` text command in `chan_usbradio`
2. A new state path in the DSP reconfiguration function `xpmr_config()`
3. A new COP function (66) in `app_rpt` to trigger the command via DTMF/CLI

### Files Changed

| File | What |
|------|------|
| `channels/chan_usbradio.c` | New struct fields, text command handler, xpmr_config branch |
| `apps/app_rpt/rpt_functions.c` | New COP case 66 |

### Data Flow

```
User dials *966
    → app_rpt dispatches to function_cop(), case 66
    → Builds text string "SETCTCSS 100.0 100.0"
    → Sends via ast_sendtext() to the rxchannel
    → chan_usbradio receives in usbradio_text()
    → SETCTCSS handler stores frequencies, sets override flag
    → Calls xpmr_config() → code_string_parse()
    → DSP decoders/encoders reconfigured with new frequencies
```

### State Management

Four new fields were added to `struct chan_usbradio_pvt`:

```c
unsigned int ctcss_override_active:1;  // Flag: dynamic override is in effect
char override_rxctcssfreqs[512];       // Dynamic RX CTCSS frequency string
char override_txctcssfreqs[512];       // Dynamic TX CTCSS frequency string
char override_txctcssdefault[16];      // Dynamic TX default tone
```

These are independent from both the config file fields (`rxctcssfreqs`, `txctcssfreqs`, `txctcssdefault`) and the remote base fields (`set_rxctcssfreqs`, `set_txctcssfreqs`, `set_txctcssdefault`).

### Priority Order in xpmr_config()

When `xpmr_config()` determines which CTCSS strings to pass to the DSP subsystem, it checks in this order:

```
1. o->remoted            → Remote base mode (unchanged, highest priority)
2. o->ctcss_override_active → Dynamic SETCTCSS override (new)
3. (default)             → Config file values (unchanged, lowest priority)
```

After the `forcetxcode` per-transmission override is applied (also unchanged), the selected strings are passed to `code_string_parse()` in the xpmr DSP subsystem, which:

- Parses the comma-separated frequency strings
- Looks up each frequency in the standard CTCSS table
- Builds an RX-to-TX mapping (`rxCtcssMap`)
- Enables/disables the CTCSS decoder and encoder
- Configures the tone generator frequency
- Selects appropriate low-pass filter coefficients

### SETCTCSS Text Command Handler

Located in `usbradio_text()`, placed after the existing `TXCTCSS` handler and before the GPIO handler. It follows the early-return pattern used by `RXCTCSS` and `TXCTCSS`:

- Parses `rxs` (first argument) and `txs` (second argument) from the text command
- If `rxs` is `"default"`, clears the override flag to revert to config file values
- Otherwise, copies the frequency strings into the override fields and sets the flag
- Calls `xpmr_config()` to apply the change immediately

### COP 66 Handler

Located in `function_cop()` in `rpt_functions.c`, case 66. It:

- Validates the channel type is `radio` or `simpleusb`
- Formats the `SETCTCSS` text command from the COP arguments
- Sends it to `myrpt->rxchannel` via `ast_sendtext()`
- Plays the function-complete telemetry tone
