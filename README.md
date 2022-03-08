# app_rpt
Refactoring and upgrade of AllStarLink's app_rpt, etc.

## Prettifying

`indent --k-and-r-style --use-tabs --tab-size4 --braces-on-if-line --cuddle-else --dont-break-function-decl-args --line-length120 --swallow-optional-blank-lines apps/app_rpt.c`

## Compiling

Add this near the bottom of `apps/Makefile`:

`$(call MOD_ADD_C,app_rpt,$(wildcard app_rpt/rpt_*.c))`

# Files from AllStarLink Asterisk which are in scope

```
allstar/mdc_decode.c
allstar/mdc_encode.c
allstar/pocsag.c
apps/app_gps.c
apps/app_rpt.c
channels/chan_tlb.c
channels/chan_usbradio.c
channels/chan_usrp.c
channels/chan_usrp.h
channels/chan_voter.c
channels/chan_simpleusb.c
channels/chan_echolink.c
include/allstar/mdc_decode.h
include/allstar/mdc_encode.h
include/allstar/pocsag.h
```
