# app_rpt
Refactoring and upgrade of AllStarLink's app_rpt, etc.

ASL Grant Info: https://www.ampr.org/grants-old/grant-allstarlink-radio-over-ip-roip-app-enhancements-infrastructure-improvement-phase-1/

# Debugging and Submitting Bugs

Feel free to open an issue for *any* trouble you might be experiencing with these modules. Please try to adhere to the following when submitting bugs:

- Enable debug to reproduce the issue. You can do this by running `core set debug 5 app_rpt` (less than or greater than 5 depending on the issue and how chatty the debug log level is). You can also enable debug all the time in `asterisk.conf`. To get debug output on the CLI, you will need to add the `debug` level to the `console => ` log file in `logger.conf`. A debug log from the CLI in the seconds leading immediately up to the issue should be provided.

- For segfault issues, a backtrace is needed. Use `ast_coredumper` to get a backtrace and post the relevant threads from `full.txt` (almost always Thread 1): https://wiki.asterisk.org/wiki/display/AST/Getting+a+Backtrace (you can also run `phreaknet backtrace` - make sure to adjust the paste duration from 24 hours if you link the paste link)

- Describe what led up to the issue and how it can be reproduced on our end.

- Any other context that might be helpful in fixing the issue.

Thank you!

# Development

## Helpful Resources

AST_PBX_KEEPALIVE: https://github.com/asterisk/asterisk/commit/50a25ac8474d7900ba59a68ed4fd942074082435

## Prettifying

`indent --k-and-r-style --use-tabs --tab-size4 --braces-on-if-line --cuddle-else --dont-break-function-decl-args --line-length120 --swallow-optional-blank-lines apps/app_rpt.c`

# Installing

You can use PhreakScript to install Asterisk automatically, first, then use the `rpt_install.sh` script to properly install the files from this repo.

## Automatic Installation

Step 1: Install DAHDI and Asterisk

```
cd /usr/src && wget https://docs.phreaknet.org/script/phreaknet.sh && chmod +x phreaknet.sh && ./phreaknet.sh make
phreaknet install -t -s -d # install in developer mode (for backtraces and assertions), install chan_sip (if you need it still) and DAHDI (required)
```

Step 2: Install app_rpt modules

- Download `rpt_install.sh` from this repo. The easiest way is to use wget, but since this repo is private, there's no default link: click on the file above, then click "Raw" in the upper right and use `wget` to download that URL. Or, you can manually get this file onto your system. Or, you can clone the repo using Git and run it from there.

- `chmod +x rpt_install.sh`
- `./rpt_install.sh`

## Manual Installation (not recommended)

If you want to manually install app_rpt et al., here is how:

### Pre-Reqs

`chan_simpleusb` and `chan_usbradio` require `libusb-dev` on Debian:

`apt-get install -y libusb-dev`

### Compiling

Add this near the bottom of `apps/Makefile`:

`$(call MOD_ADD_C,app_rpt,$(wildcard app_rpt/rpt_*.c))`

Add this near the bottom of `channels/Makefile`:

`chan_simpleusb.so: LIBS+=-lusb -lasound`

`chan_usbradio.so: LIBS+=-lusb -lasound`

### After DAHDI/Asterisk installed

`/dev/dsp1` needs to exist for chan_simpleusb and chan_usbradio to work.

This StackOverflow post contains the answer in an upvoted comment: https://unix.stackexchange.com/questions/103746/why-wont-linux-let-me-play-with-dev-dsp/103755#103755

Run: `modprobe snd-pcm-oss` (as root/sudo)

## Troubleshooting

One-liner to kill Asterisk if it won't cleanly stop or restart:

`kill -9 $(ps -aux | grep " asterisk" | grep -v "grep" | awk '{print $2}' )`

Alternately you can simply run `phreaknet kill` or `phreaknet restart`

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

# Files that may have changed over time

```
Files ./apps/app_rpt.c and ../ASL-Asterisk/asterisk/apps/app_rpt.c differ
Files ./build_tools/make_version and ../ASL-Asterisk/asterisk/build_tools/make_version differ
Files ./channels/chan_dahdi.c and ../ASL-Asterisk/asterisk/channels/chan_dahdi.c differ
Files ./channels/chan_iax2.c and ../ASL-Asterisk/asterisk/channels/chan_iax2.c differ
Files ./channels/iax2.h and ../ASL-Asterisk/asterisk/channels/iax2.h differ
Files ./channels/iax2-parser.c and ../ASL-Asterisk/asterisk/channels/iax2-parser.c differ
Files ./channels/iax2-parser.h and ../ASL-Asterisk/asterisk/channels/iax2-parser.h differ
Files ./channels/iax2-provision.c and ../ASL-Asterisk/asterisk/channels/iax2-provision.c differ
Files ./channels/Makefile and ../ASL-Asterisk/asterisk/channels/Makefile differ
Files ./codecs/codec_adpcm.c and ../ASL-Asterisk/asterisk/codecs/codec_adpcm.c differ
Files ./codecs/codec_ilbc.c and ../ASL-Asterisk/asterisk/codecs/codec_ilbc.c differ
Files ./codecs/gsm/Makefile and ../ASL-Asterisk/asterisk/codecs/gsm/Makefile differ
Files ./codecs/Makefile and ../ASL-Asterisk/asterisk/codecs/Makefile differ
Files ./configs/dnsmgr.conf.sample and ../ASL-Asterisk/asterisk/configs/dnsmgr.conf.sample differ
Files ./configs/extensions.conf.sample and ../ASL-Asterisk/asterisk/configs/extensions.conf.sample differ
Files ./configs/iax.conf.sample and ../ASL-Asterisk/asterisk/configs/iax.conf.sample differ
Files ./configs/indications.conf.sample and ../ASL-Asterisk/asterisk/configs/indications.conf.sample differ
Files ./configs/manager.conf.sample and ../ASL-Asterisk/asterisk/configs/manager.conf.sample differ
Files ./configs/modules.conf.sample and ../ASL-Asterisk/asterisk/configs/modules.conf.sample differ
Files ./configs/rpt.conf.sample and ../ASL-Asterisk/asterisk/configs/rpt.conf.sample differ
Files ./configs/sip.conf.sample and ../ASL-Asterisk/asterisk/configs/sip.conf.sample differ
Files ./configure and ../ASL-Asterisk/asterisk/configure differ
Files ./configure.ac and ../ASL-Asterisk/asterisk/configure.ac differ
Files ./contrib/init.d/rc.debian.asterisk and ../ASL-Asterisk/asterisk/contrib/init.d/rc.debian.asterisk differ
Files ./contrib/init.d/rc.gentoo.asterisk and ../ASL-Asterisk/asterisk/contrib/init.d/rc.gentoo.asterisk differ
Files ./contrib/scripts/get_ilbc_source.sh and ../ASL-Asterisk/asterisk/contrib/scripts/get_ilbc_source.sh differ
Files ./formats/format_ilbc.c and ../ASL-Asterisk/asterisk/formats/format_ilbc.c differ
Files ./funcs/func_curl.c and ../ASL-Asterisk/asterisk/funcs/func_curl.c differ
Files ./include/asterisk/astobj2.h and ../ASL-Asterisk/asterisk/include/asterisk/astobj2.h differ
Files ./include/asterisk/frame.h and ../ASL-Asterisk/asterisk/include/asterisk/frame.h differ
Files ./include/asterisk/inline_api.h and ../ASL-Asterisk/asterisk/include/asterisk/inline_api.h differ
Files ./include/jitterbuf.h and ../ASL-Asterisk/asterisk/include/jitterbuf.h differ
Files ./main/abstract_jb.c and ../ASL-Asterisk/asterisk/main/abstract_jb.c differ
Files ./main/asterisk.c and ../ASL-Asterisk/asterisk/main/asterisk.c differ
Files ./main/astobj2.c and ../ASL-Asterisk/asterisk/main/astobj2.c differ
Files ./main/channel.c and ../ASL-Asterisk/asterisk/main/channel.c differ
Files ./main/config.c and ../ASL-Asterisk/asterisk/main/config.c differ
Files ./main/dnsmgr.c and ../ASL-Asterisk/asterisk/main/dnsmgr.c differ
Files ./main/dsp.c and ../ASL-Asterisk/asterisk/main/dsp.c differ
Files ./main/file.c and ../ASL-Asterisk/asterisk/main/file.c differ
Files ./main/frame.c and ../ASL-Asterisk/asterisk/main/frame.c differ
Files ./main/indications.c and ../ASL-Asterisk/asterisk/main/indications.c differ
Files ./main/jitterbuf.c and ../ASL-Asterisk/asterisk/main/jitterbuf.c differ
Files ./main/pbx.c and ../ASL-Asterisk/asterisk/main/pbx.c differ
Files ./main/utils.c and ../ASL-Asterisk/asterisk/main/utils.c differ
Files ./Makefile and ../ASL-Asterisk/asterisk/Makefile differ
Files ./res/res_features.c and ../ASL-Asterisk/asterisk/res/res_features.c differ
Files ./res/snmp/agent.c and ../ASL-Asterisk/asterisk/res/snmp/agent.c differ
Files ./sounds/Makefile and ../ASL-Asterisk/asterisk/sounds/Makefile differ
Files ./sounds/sounds.xml and ../ASL-Asterisk/asterisk/sounds/sounds.xml differ
Files ./utils/Makefile and ../ASL-Asterisk/asterisk/utils/Makefile differ
```
